#include "http_download.h"
#include "tls_client.h"
#include "url_analyzer.h"

#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <android/log.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <time.h>

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
    
    /* Build HTTP request with desktop User-Agent to get full ytInitialPlayerResponse */
    char request[8192];
    int req_len = snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\n"
             "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8\r\n"
             "Accept-Language: en-US,en;q=0.9\r\n"
             "Accept-Encoding: identity\r\n"
             "Connection: close\r\n"
             "Upgrade-Insecure-Requests: 1\r\n"
             "Sec-Fetch-Dest: document\r\n"
             "Sec-Fetch-Mode: navigate\r\n"
             "Sec-Fetch-Site: none\r\n"
             "Sec-Fetch-User: ?1\r\n"
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
    LOGI("HTTP request sent: %zd bytes", sent);
    LOGI("Request headers:\n%.500s", request);
    
    /* Set socket timeout to prevent infinite hangs - shorter timeout for faster response */
    struct timeval tv;
    tv.tv_sec = 10;  // 10 second timeout for reads
    tv.tv_usec = 0;
    int sockfd = client.net.fd;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    tv.tv_sec = 5;   // 5 second timeout for writes
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    LOGI("Set socket timeout (read=10s, write=5s)");

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
    size_t total_received = 0;
    time_t start_time = time(NULL);
    const int max_read_time = 30;  // Maximum 30 seconds total read time
    int zero_reads = 0;
    const int max_zero_reads = 100;  // Max consecutive zero-byte reads
    
    LOGI("Starting to read HTTP response...");
    while ((n = tls_client_read(&client, buf, sizeof(buf))) > 0 || 
           (n == 0 && zero_reads < max_zero_reads)) {
        if (n == 0) {
            zero_reads++;
            usleep(1000);  // 1ms sleep to prevent busy-waiting
            // Check for timeout
            if (time(NULL) - start_time > max_read_time) {
                LOGE("HTTP read timeout after %d seconds", max_read_time);
                break;
            }
            continue;
        }
        zero_reads = 0;  // Reset counter on successful read
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
        total_received += (size_t)n;
        
        // Check for timeout
        if (time(NULL) - start_time > max_read_time) {
            LOGE("HTTP read timeout after %d seconds", max_read_time);
            snprintf(err, errLen, "Download timeout");
            free(outBuffer->data);
            outBuffer->data = NULL;
            tls_client_close(&client);
            return false;
        }
    }
    
    LOGI("Finished reading response: %zu bytes total", outBuffer->size);
    tls_client_close(&client);
    
    /* Parse HTTP response */
    LOGI("Received %zu bytes response", outBuffer->size);
    if (outBuffer->size > 0) {
        /* Log first 100 bytes of response for debugging */
        char debug_buf[256];
        size_t debug_len = outBuffer->size < 100 ? outBuffer->size : 100;
        for (size_t i = 0; i < debug_len && i < sizeof(debug_buf)-4; i++) {
            unsigned char c = (unsigned char)outBuffer->data[i];
            if (c >= 32 && c < 127) {
                debug_buf[i] = c;
            } else {
                debug_buf[i] = '.';
            }
        }
        debug_buf[debug_len] = '\0';
        LOGI("Response start: [%s]", debug_buf);
    }
    
    char *header_end = strstr(outBuffer->data, "\r\n\r\n");
    if (!header_end) {
        /* Try to find just \n\n as some servers use that */
        header_end = strstr(outBuffer->data, "\n\n");
    }
    if (!header_end) {
        snprintf(err, errLen, "Invalid HTTP response (received %zu bytes)", outBuffer->size);
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
    
    /* Handle redirects - but don't follow cross-domain redirects for script/resource downloads */
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
            
            /* Block redirects to authentication/login pages - these should never happen for JS files */
            if (strstr(redirect_url, "accounts.google.com") ||
                strstr(redirect_url, "ServiceLogin") ||
                strstr(redirect_url, "/signin") ||
                strstr(redirect_url, "/login")) {
                LOGE("Blocking redirect to authentication page: %s", redirect_url);
                snprintf(err, errLen, "Redirect to login page blocked");
                free(outBuffer->data);
                outBuffer->data = NULL;
                return false;
            }
            
            /* Check for cross-domain redirect from youtube.com to other domains */
            if (strstr(host, "youtube.com")) {
                char redirect_host[256] = {0};
                char temp_path[2048], temp_port[8];
                if (parse_url(redirect_url, redirect_host, sizeof(redirect_host), 
                              temp_path, sizeof(temp_path), temp_port, sizeof(temp_port))) {
                    if (!strstr(redirect_host, "youtube.com") && 
                        !strstr(redirect_host, "googlevideo.com") &&
                        !strstr(redirect_host, "ytimg.com") &&
                        !strstr(redirect_host, "googleapis.com") &&
                        !strstr(redirect_host, "gstatic.com")) {
                        LOGE("Blocking cross-domain redirect from youtube.com to %s", redirect_host);
                        snprintf(err, errLen, "Cross-domain redirect blocked");
                        free(outBuffer->data);
                        outBuffer->data = NULL;
                        return false;
                    }
                }
            }
            
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
    /* Check which delimiter was found to calculate correct header length */
    size_t delimiter_len = 4;  /* Default \r\n\r\n */
    if (header_end >= outBuffer->data + 1 && 
        header_end[-1] == '\n' && 
        (header_end == outBuffer->data || header_end[-2] != '\r')) {
        delimiter_len = 2;  /* Found \n\n */
    }
    size_t header_len = (size_t)(header_end - outBuffer->data) + delimiter_len;
    size_t body_len = outBuffer->size - header_len;
    
    /* Check for Transfer-Encoding: chunked */
    char *te_header = strstr(outBuffer->data, "Transfer-Encoding:");
    bool is_chunked = false;
    if (te_header && te_header < header_end + delimiter_len) {
        te_header += 18; /* Skip "Transfer-Encoding:" */
        while (*te_header == ' ') te_header++;
        if (strncasecmp(te_header, "chunked", 7) == 0) {
            is_chunked = true;
            LOGI("Detected chunked transfer encoding");
        }
    }
    
    if (is_chunked) {
        /* Decode chunked transfer encoding */
        char *src = outBuffer->data + header_len;
        char *dst = outBuffer->data;  /* Decode in-place */
        size_t decoded_len = 0;
        
        while (true) {
            /* Find chunk size (hex number followed by \r\n or just \n) */
            char *chunk_start = src;
            char *line_end = strstr(chunk_start, "\r\n");
            if (!line_end) line_end = strchr(chunk_start, '\n');
            if (!line_end) break;  /* Invalid chunked data */
            
            /* Parse chunk size in hex */
            size_t chunk_size = 0;
            for (char *p = chunk_start; p < line_end && *p != ';' && *p != '\r' && *p != '\n'; p++) {
                if (*p >= '0' && *p <= '9') {
                    chunk_size = chunk_size * 16 + (*p - '0');
                } else if (*p >= 'a' && *p <= 'f') {
                    chunk_size = chunk_size * 16 + (*p - 'a' + 10);
                } else if (*p >= 'A' && *p <= 'F') {
                    chunk_size = chunk_size * 16 + (*p - 'A' + 10);
                } else {
                    break;  /* Invalid hex character */
                }
            }
            
            /* Move past chunk size line */
            src = line_end;
            if (*src == '\r') src++;
            if (*src == '\n') src++;
            
            /* Check for end of chunks (size 0) */
            if (chunk_size == 0) {
                break;
            }
            
            /* Copy chunk data */
            if (decoded_len + chunk_size > outBuffer->size - header_len) {
                /* Safety check - chunk would overflow */
                LOGE("Chunk size %zu would overflow buffer", chunk_size);
                break;
            }
            memcpy(dst + decoded_len, src, chunk_size);
            decoded_len += chunk_size;
            src += chunk_size;
            
            /* Skip trailing \r\n after chunk data */
            if (*src == '\r') src++;
            if (*src == '\n') src++;
        }
        
        dst[decoded_len] = '\0';
        outBuffer->size = decoded_len;
        LOGI("Decoded chunked response: %zu bytes -> %zu bytes", body_len, decoded_len);
    } else {
        /* Not chunked, just move body to start */
        memmove(outBuffer->data, outBuffer->data + header_len, body_len);
        outBuffer->data[body_len] = '\0';
        outBuffer->size = body_len;
    }
    
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
