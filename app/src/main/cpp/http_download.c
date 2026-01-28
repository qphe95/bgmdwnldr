#include "http_download.h"
#include "tls_client.h"
#include "url_analyzer.h"

#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <android/log.h>

#define LOG_TAG "http_download"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define CHUNK_SIZE 8192
#define MAX_REDIRECTS 5

/* Simple HTTP response parser */
typedef struct HttpResponse {
    int status_code;
    char *body;
    size_t body_len;
    size_t body_capacity;
    char location[2048];
    char cookies[4096];  /* Store cookies from response */
} HttpResponse;

/* Forward declarations */
static bool http_request_with_cookies(const char *url, HttpBuffer *outBuffer,
                                      char *err, size_t errLen, const char *cookies);
static bool http_request(const char *url, HttpBuffer *outBuffer,
                         char *err, size_t errLen);

static bool parse_url(const char *url, char *host, size_t host_len,
                      char *path, size_t path_len, char *port, size_t port_len) {
    const char *p = url;
    
    /* Skip scheme */
    if (strncmp(p, "https://", 8) == 0) {
        p += 8;
        strncpy(port, "443", port_len);
    } else if (strncmp(p, "http://", 7) == 0) {
        p += 7;
        strncpy(port, "80", port_len);
    } else {
        strncpy(port, "443", port_len);
    }
    
    /* Extract host and path */
    const char *slash = strchr(p, '/');
    if (slash) {
        size_t host_sz = (size_t)(slash - p);
        if (host_sz >= host_len) host_sz = host_len - 1;
        memcpy(host, p, host_sz);
        host[host_sz] = '\0';
        
        strncpy(path, slash, path_len - 1);
        path[path_len - 1] = '\0';
    } else {
        strncpy(host, p, host_len - 1);
        host[host_len - 1] = '\0';
        strncpy(path, "/", path_len);
    }
    
    return true;
}

/* Global context to pass cookies between requests */
static char g_youtube_cookies[4096] = {0};

void http_set_youtube_cookies(const char *cookies) {
    if (cookies) {
        strncpy(g_youtube_cookies, cookies, sizeof(g_youtube_cookies) - 1);
        g_youtube_cookies[sizeof(g_youtube_cookies) - 1] = '\0';
        LOGI("Set YouTube cookies: %.100s...", g_youtube_cookies);
    }
}

const char* http_get_youtube_cookies(void) {
    return g_youtube_cookies[0] ? g_youtube_cookies : NULL;
}

void http_clear_youtube_cookies(void) {
    g_youtube_cookies[0] = '\0';
}

static bool http_request_with_cookies(const char *url, HttpBuffer *outBuffer,
                         char *err, size_t errLen, const char *cookies) {
    char host[256] = {0};
    char path[2048] = {0};
    char port[8] = {0};
    
    if (!parse_url(url, host, sizeof(host), path, sizeof(path), port, sizeof(port))) {
        snprintf(err, errLen, "Failed to parse URL");
        return false;
    }
    
    LOGI("Connecting to %s:%s%s", host, port, path);
    
    TlsClient client = {0};
    if (!tls_client_connect(&client, host, port, err, errLen)) {
        return false;
    }
    
    /* Build HTTP request with YouTube-compatible headers */
    char request[8192];
    int req_len = snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\n"
             "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8\r\n"
             "Accept-Language: en-US,en;q=0.9\r\n"
             "Accept-Encoding: identity\r\n"
             "DNT: 1\r\n"
             "Connection: close\r\n"
             "Upgrade-Insecure-Requests: 1\r\n"
             "Sec-Fetch-Dest: document\r\n"
             "Sec-Fetch-Mode: navigate\r\n"
             "Sec-Fetch-Site: none\r\n"
             "Cache-Control: max-age=0\r\n",
             path, host);
    
    /* Add Referer for googlevideo.com */
    if (strstr(host, "googlevideo.com")) {
        req_len += snprintf(request + req_len, sizeof(request) - req_len,
                           "Referer: https://www.youtube.com/\r\n");
    }
    
    /* Add cookies if provided */
    if (cookies && cookies[0]) {
        req_len += snprintf(request + req_len, sizeof(request) - req_len,
                           "Cookie: %s\r\n", cookies);
        LOGI("Adding cookies to request for %s", host);
    }
    
    /* Add final CRLF */
    req_len += snprintf(request + req_len, sizeof(request) - req_len, "\r\n");
    
    ssize_t sent = tls_client_write(&client, (unsigned char *)request, strlen(request));
    if (sent < 0) {
        snprintf(err, errLen, "Failed to send request");
        tls_client_close(&client);
        return false;
    }
    
    /* Read response */
    outBuffer->data = malloc(CHUNK_SIZE);
    outBuffer->size = 0;
    if (!outBuffer->data) {
        snprintf(err, errLen, "Out of memory");
        tls_client_close(&client);
        return false;
    }
    
    size_t capacity = CHUNK_SIZE;
    unsigned char buf[CHUNK_SIZE];
    ssize_t n;
    
    while ((n = tls_client_read(&client, buf, sizeof(buf))) > 0) {
        if (outBuffer->size + (size_t)n > capacity) {
            capacity *= 2;
            char *new_data = realloc(outBuffer->data, capacity);
            if (!new_data) {
                snprintf(err, errLen, "Out of memory");
                free(outBuffer->data);
                outBuffer->data = NULL;
                tls_client_close(&client);
                return false;
            }
            outBuffer->data = new_data;
        }
        memcpy(outBuffer->data + outBuffer->size, buf, (size_t)n);
        outBuffer->size += (size_t)n;
    }
    
    tls_client_close(&client);
    
    /* Parse HTTP response */
    char *header_end = strstr(outBuffer->data, "\r\n\r\n");
    if (!header_end) {
        snprintf(err, errLen, "Invalid HTTP response");
        free(outBuffer->data);
        outBuffer->data = NULL;
        return false;
    }
    
    /* Extract status code */
    int status = 0;
    sscanf(outBuffer->data, "HTTP/%*s %d", &status);
    
    LOGI("HTTP status: %d", status);
    
    /* Log what cookies we're sending (for debugging) */
    if (cookies && cookies[0]) {
        LOGI("Sending cookies: %.200s...", cookies);
    }
    
    /* Extract cookies from response headers */
    /* Format: Set-Cookie: NAME=VALUE; Domain=...; Path=...; Expires=... */
    /* We only want NAME=VALUE pairs */
    char *set_cookie = strstr(outBuffer->data, "Set-Cookie:");
    while (set_cookie) {
        set_cookie += 11; /* Skip "Set-Cookie:" */
        while (*set_cookie == ' ') set_cookie++;
        
        /* Find end of cookie definition (end of line) */
        char *line_end = strchr(set_cookie, '\r');
        if (!line_end) line_end = strchr(set_cookie, '\n');
        if (!line_end) line_end = set_cookie + strlen(set_cookie);
        
        /* Find the NAME=VALUE part (before first semicolon) */
        char *first_semi = strchr(set_cookie, ';');
        if (first_semi && first_semi < line_end) {
            /* Extract just the NAME=VALUE part */
            size_t nv_len = (size_t)(first_semi - set_cookie);
            if (nv_len > 0 && nv_len < 2000) {
                char name_value[2048];
                memcpy(name_value, set_cookie, nv_len);
                name_value[nv_len] = '\0';
                
                /* Check for = sign */
                char *eq = strchr(name_value, '=');
                if (eq && eq > name_value) {
                    *eq = '\0';
                    char *cookie_name = name_value;
                    char *cookie_value = eq + 1;
                    
                    /* Skip cookie attributes that sometimes appear as separate Set-Cookie values */
                    if (strcmp(cookie_name, "Domain") != 0 && 
                        strcmp(cookie_name, "Path") != 0 &&
                        strcmp(cookie_name, "Expires") != 0 &&
                        strcmp(cookie_name, "Max-Age") != 0 &&
                        strcmp(cookie_name, "HttpOnly") != 0 &&
                        strcmp(cookie_name, "Secure") != 0 &&
                        strcmp(cookie_name, "SameSite") != 0) {
                        
                        /* Check for duplicate */
                        bool duplicate = false;
                        char *search = g_youtube_cookies;
                        while ((search = strstr(search, cookie_name)) != NULL) {
                            if (search == g_youtube_cookies || search[-1] == ' ' && search[-2] == ';') {
                                if (search[strlen(cookie_name)] == '=') {
                                    duplicate = true;
                                    break;
                                }
                            }
                            search++;
                        }
                        
                        if (!duplicate) {
                            if (g_youtube_cookies[0]) {
                                strncat(g_youtube_cookies, "; ", sizeof(g_youtube_cookies) - strlen(g_youtube_cookies) - 1);
                            }
                            strncat(g_youtube_cookies, cookie_name, sizeof(g_youtube_cookies) - strlen(g_youtube_cookies) - 1);
                            strncat(g_youtube_cookies, "=", sizeof(g_youtube_cookies) - strlen(g_youtube_cookies) - 1);
                            strncat(g_youtube_cookies, cookie_value, sizeof(g_youtube_cookies) - strlen(g_youtube_cookies) - 1);
                            LOGI("Captured cookie: %s=...", cookie_name);
                        }
                    }
                }
            }
        }
        /* Look for next Set-Cookie */
        set_cookie = strstr(line_end, "Set-Cookie:");
    }
    
    /* Handle redirects */
    if (status >= 300 && status < 400) {
        char *location = strstr(outBuffer->data, "Location:");
        if (location) {
            location += 9;
            while (*location == ' ') location++;
            char redirect_url[2048];
            size_t i = 0;
            while (*location && *location != '\r' && *location != '\n' && i < sizeof(redirect_url) - 1) {
                redirect_url[i++] = *location++;
            }
            redirect_url[i] = '\0';
            
            LOGI("Redirect to: %s", redirect_url);
            free(outBuffer->data);
            outBuffer->data = NULL;
            return http_request_with_cookies(redirect_url, outBuffer, err, errLen, NULL);
        }
    }
    
    if (status < 200 || status >= 300) {
        snprintf(err, errLen, "HTTP error %d", status);
        free(outBuffer->data);
        outBuffer->data = NULL;
        return false;
    }
    
    /* Move body to start of buffer */
    size_t header_len = (size_t)(header_end - outBuffer->data) + 4;
    size_t body_len = outBuffer->size - header_len;
    memmove(outBuffer->data, outBuffer->data + header_len, body_len);
    outBuffer->data[body_len] = '\0';
    outBuffer->size = body_len;
    
    return true;
}

static bool http_request(const char *url, HttpBuffer *outBuffer,
                         char *err, size_t errLen) {
    /* For googlevideo.com URLs, use the saved cookies */
    if (strstr(url, "googlevideo.com")) {
        return http_request_with_cookies(url, outBuffer, err, errLen, g_youtube_cookies);
    }
    return http_request_with_cookies(url, outBuffer, err, errLen, NULL);
}

bool http_get_to_memory(const char *url, HttpBuffer *outBuffer,
                        char *err, size_t errLen) {
    return http_request(url, outBuffer, err, errLen);
}

void http_free_buffer(HttpBuffer *buffer) {
    if (buffer && buffer->data) {
        free(buffer->data);
        buffer->data = NULL;
        buffer->size = 0;
    }
}

bool http_download_to_file(const char *url, const char *filePath,
                           DownloadProgressCallback progress, void *user,
                           char *err, size_t errLen) {
    (void)filePath;
    (void)progress;
    (void)user;
    
    HttpBuffer buffer = {0};
    if (!http_request(url, &buffer, err, errLen)) {
        return false;
    }
    
    /* For now, just log the size */
    LOGI("Downloaded %zu bytes", buffer.size);
    
    http_free_buffer(&buffer);
    return true;
}

/* Legacy WebView functions - now no-ops */
void http_download_via_webview(const char *url, void *app) {
    (void)url;
    (void)app;
    LOGI("WebView mode disabled - using native HTTP");
}

void http_download_set_jni_refs(JavaVM *vm, jobject activity) {
    (void)vm;
    (void)activity;
}

void http_download_load_youtube_page(const char *url) {
    (void)url;
}

void http_download_set_youtube_cookies(const char *cookies) {
    (void)cookies;
}

void http_download_set_js_session_data(const char *session) {
    (void)session;
}
