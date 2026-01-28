#include "http_download.h"
#include "tls_client.h"

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#include <android/log.h>

#define HEADER_LIMIT (64 * 1024)
#define READ_CHUNK 4096

#define LOG_TAG "minimalvulkan"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

typedef struct UrlParts {
    char scheme[8];
    char host[256];
    char port[6];
    char path[2048];
} UrlParts;

static void set_err(char *err, size_t errLen, const char *msg) {
    if (err && errLen > 0) {
        snprintf(err, errLen, "%s", msg);
    }
}

static bool parse_url(const char *url, UrlParts *parts, char *err, size_t errLen) {
    if (!url || !parts) {
        set_err(err, errLen, "Invalid URL");
        return false;
    }
    memset(parts, 0, sizeof(*parts));
    const char *schemeEnd = strstr(url, "://");
    if (!schemeEnd) {
        set_err(err, errLen, "URL missing scheme");
        return false;
    }
    size_t schemeLen = (size_t)(schemeEnd - url);
    if (schemeLen >= sizeof(parts->scheme)) {
        set_err(err, errLen, "Scheme too long");
        return false;
    }
    memcpy(parts->scheme, url, schemeLen);
    parts->scheme[schemeLen] = '\0';
    const char *hostStart = schemeEnd + 3;
    const char *pathStart = strchr(hostStart, '/');
    const char *hostEnd = pathStart ? pathStart : url + strlen(url);
    const char *portStart = memchr(hostStart, ':', (size_t)(hostEnd - hostStart));
    if (portStart) {
        size_t hostLen = (size_t)(portStart - hostStart);
        if (hostLen >= sizeof(parts->host)) {
            set_err(err, errLen, "Host too long");
            return false;
        }
        memcpy(parts->host, hostStart, hostLen);
        parts->host[hostLen] = '\0';
        size_t portLen = (size_t)(hostEnd - portStart - 1);
        if (portLen >= sizeof(parts->port)) {
            set_err(err, errLen, "Port too long");
            return false;
        }
        memcpy(parts->port, portStart + 1, portLen);
        parts->port[portLen] = '\0';
    } else {
        size_t hostLen = (size_t)(hostEnd - hostStart);
        if (hostLen >= sizeof(parts->host)) {
            set_err(err, errLen, "Host too long");
            return false;
        }
        memcpy(parts->host, hostStart, hostLen);
        parts->host[hostLen] = '\0';
    }
    if (pathStart) {
        snprintf(parts->path, sizeof(parts->path), "%s", pathStart);
    } else {
        snprintf(parts->path, sizeof(parts->path), "/");
    }
    if (parts->port[0] == '\0') {
        if (strcmp(parts->scheme, "https") == 0) {
            snprintf(parts->port, sizeof(parts->port), "443");
        } else {
            snprintf(parts->port, sizeof(parts->port), "80");
        }
    }
    return true;
}

static int open_tcp_socket(const char *host, const char *port, char *err, size_t errLen) {
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int ret = getaddrinfo(host, port, &hints, &result);
    if (ret != 0) {
        set_err(err, errLen, "DNS lookup failed");
        return -1;
    }
    int sock = -1;
    for (struct addrinfo *rp = result; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == -1) {
            continue;
        }
        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(sock);
        sock = -1;
    }
    freeaddrinfo(result);
    if (sock == -1) {
        set_err(err, errLen, "TCP connect failed");
    }
    return sock;
}

static ssize_t socket_read(int sock, unsigned char *buf, size_t len) {
    ssize_t ret = recv(sock, buf, len, 0);
    return ret;
}

static ssize_t socket_write(int sock, const unsigned char *buf, size_t len) {
    ssize_t ret = send(sock, buf, len, 0);
    return ret;
}

typedef struct HttpStream {
    bool useTls;
    int sock;
    TlsClient tls;
} HttpStream;

static bool stream_open(HttpStream *stream, const UrlParts *parts,
                        char *err, size_t errLen) {
    if (strcmp(parts->scheme, "https") == 0) {
        stream->useTls = true;
        if (!tls_client_connect(&stream->tls, parts->host, parts->port, err, errLen)) {
            return false;
        }
        return true;
    }
    stream->useTls = false;
    stream->sock = open_tcp_socket(parts->host, parts->port, err, errLen);
    return stream->sock >= 0;
}

static ssize_t stream_read(HttpStream *stream, unsigned char *buf, size_t len) {
    if (stream->useTls) {
        return tls_client_read(&stream->tls, buf, len);
    }
    return socket_read(stream->sock, buf, len);
}

static ssize_t stream_write(HttpStream *stream, const unsigned char *buf, size_t len) {
    if (stream->useTls) {
        return tls_client_write(&stream->tls, buf, len);
    }
    return socket_write(stream->sock, buf, len);
}

static void stream_close(HttpStream *stream) {
    if (stream->useTls) {
        tls_client_close(&stream->tls);
    } else if (stream->sock >= 0) {
        close(stream->sock);
        stream->sock = -1;
    }
}

static bool read_headers(HttpStream *stream, char **outHeaders, char *err, size_t errLen) {
    char *buffer = (char *)malloc(HEADER_LIMIT);
    if (!buffer) {
        set_err(err, errLen, "Header alloc failed");
        return false;
    }
    size_t total = 0;
    int emptyReads = 0;
    while (total + 1 < HEADER_LIMIT) {
        unsigned char c;
        ssize_t got = stream_read(stream, &c, 1);
        if (got == 0) {
            if (++emptyReads > 1000) {
                free(buffer);
                set_err(err, errLen, "Header read timeout");
                return false;
            }
            usleep(1000);
            continue;
        }
        if (got < 0) {
            free(buffer);
            LOGE("Header read failed: got=%zd", got);
            set_err(err, errLen, "Header read failed");
            return false;
        }
        emptyReads = 0;
        buffer[total++] = (char)c;
        if (total >= 4 &&
            buffer[total - 4] == '\r' && buffer[total - 3] == '\n' &&
            buffer[total - 2] == '\r' && buffer[total - 1] == '\n') {
            char *headers = (char *)malloc(total + 1);
            if (!headers) {
                free(buffer);
                set_err(err, errLen, "Header alloc failed");
                return false;
            }
            memcpy(headers, buffer, total);
            headers[total] = '\0';
            free(buffer);
            *outHeaders = headers;
            return true;
        }
    }
    free(buffer);
    set_err(err, errLen, "Header too large");
    return false;
}

static int parse_status(const char *headers) {
    const char *space = strchr(headers, ' ');
    if (!space) {
        return -1;
    }
    return atoi(space + 1);
}

static long parse_content_length(const char *headers) {
    const char *p = strcasestr(headers, "Content-Length:");
    if (!p) {
        return -1;
    }
    p += strlen("Content-Length:");
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    return strtol(p, NULL, 10);
}

static bool header_has_chunked(const char *headers) {
    const char *p = strcasestr(headers, "Transfer-Encoding:");
    if (!p) {
        return false;
    }
    return strcasestr(p, "chunked") != NULL;
}

static bool header_get_location(const char *headers, char *out, size_t outLen) {
    const char *p = strcasestr(headers, "Location:");
    if (!p) {
        return false;
    }
    p += strlen("Location:");
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    const char *end = strstr(p, "\r\n");
    if (!end) {
        return false;
    }
    size_t len = (size_t)(end - p);
    if (len >= outLen) {
        len = outLen - 1;
    }
    memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

static bool read_chunked(HttpStream *stream, FILE *outFile,
                         DownloadProgressCallback progress, void *user,
                         char *err, size_t errLen) {
    char line[64];
    size_t total = 0;
    int emptyReads = 0;
    while (true) {
        size_t lineLen = 0;
        while (lineLen + 1 < sizeof(line)) {
            unsigned char c;
            ssize_t got = stream_read(stream, &c, 1);
            if (got == 0) {
                if (++emptyReads > 1000) {
                    set_err(err, errLen, "Chunked header timeout");
                    return false;
                }
                usleep(1000);
                continue;
            }
            if (got < 0) {
                set_err(err, errLen, "Chunked read failed");
                return false;
            }
            emptyReads = 0;
            line[lineLen++] = (char)c;
            if (lineLen >= 2 && line[lineLen - 2] == '\r' && line[lineLen - 1] == '\n') {
                break;
            }
        }
        line[lineLen] = '\0';
        size_t chunkSize = (size_t)strtoul(line, NULL, 16);
        if (chunkSize == 0) {
            break;
        }
        size_t remaining = chunkSize;
        unsigned char buf[READ_CHUNK];
        while (remaining > 0) {
            size_t toRead = remaining > sizeof(buf) ? sizeof(buf) : remaining;
            ssize_t got = stream_read(stream, buf, toRead);
            if (got == 0) {
                if (++emptyReads > 1000) {
                    set_err(err, errLen, "Chunked data timeout");
                    return false;
                }
                usleep(1000);
                continue;
            }
            if (got < 0) {
                set_err(err, errLen, "Chunked data read failed");
                return false;
            }
            emptyReads = 0;
            fwrite(buf, 1, (size_t)got, outFile);
            remaining -= (size_t)got;
            total += (size_t)got;
            if (progress) {
                progress(total, 0, user);
            }
        }
        unsigned char crlf[2];
        stream_read(stream, crlf, 2);
    }
    return true;
}

static bool read_chunked_to_buffer(HttpStream *stream, HttpBuffer *outBuffer,
                                   char *err, size_t errLen) {
    size_t cap = 256 * 1024;
    outBuffer->data = (char *)malloc(cap + 1);
    outBuffer->size = 0;
    if (!outBuffer->data) {
        set_err(err, errLen, "Buffer alloc failed");
        return false;
    }
    int emptyReads = 0;
    while (true) {
        char line[64];
        size_t lineLen = 0;
        while (lineLen + 1 < sizeof(line)) {
            unsigned char c;
            ssize_t got = stream_read(stream, &c, 1);
            if (got == 0) {
                if (++emptyReads > 1000) {
                    set_err(err, errLen, "Chunked header timeout");
                    return false;
                }
                usleep(1000);
                continue;
            }
            if (got < 0) {
                set_err(err, errLen, "Chunked read failed");
                return false;
            }
            emptyReads = 0;
            line[lineLen++] = (char)c;
            if (lineLen >= 2 && line[lineLen - 2] == '\r' && line[lineLen - 1] == '\n') {
                break;
            }
        }
        line[lineLen] = '\0';
        size_t chunkSize = (size_t)strtoul(line, NULL, 16);
        if (chunkSize == 0) {
            break;
        }
        if (outBuffer->size + chunkSize + 1 > cap) {
            cap = (cap + chunkSize) * 2;
            char *next = (char *)realloc(outBuffer->data, cap + 1);
            if (!next) {
                set_err(err, errLen, "Buffer realloc failed");
                return false;
            }
            outBuffer->data = next;
        }
        size_t remaining = chunkSize;
        while (remaining > 0) {
            unsigned char buf[READ_CHUNK];
            size_t toRead = remaining > sizeof(buf) ? sizeof(buf) : remaining;
            ssize_t got = stream_read(stream, buf, toRead);
            if (got == 0) {
                if (++emptyReads > 1000) {
                    set_err(err, errLen, "Chunked data timeout");
                    return false;
                }
                usleep(1000);
                continue;
            }
            if (got < 0) {
                set_err(err, errLen, "Chunked data read failed");
                return false;
            }
            emptyReads = 0;
            memcpy(outBuffer->data + outBuffer->size, buf, (size_t)got);
            outBuffer->size += (size_t)got;
            remaining -= (size_t)got;
        }
        unsigned char crlf[2];
        stream_read(stream, crlf, 2);
    }
    outBuffer->data[outBuffer->size] = '\0';
    return true;
}

static bool perform_request(const char *url, FILE *outFile, HttpBuffer *outBuffer,
                            DownloadProgressCallback progress, void *user,
                            char *err, size_t errLen) {
    // For googlevideo.com URLs, clean up the URL to remove problematic parameters
    char cleanedUrl[4096];
    if (strstr(url, "googlevideo.com")) {
        // Copy the URL and remove problematic parameters
        strcpy(cleanedUrl, url);

        // Remove ip parameter
        char *ipParam = strstr(cleanedUrl, "&ip=");
        if (ipParam) {
            char *nextParam = strchr(ipParam + 1, '&');
            if (nextParam) {
                memmove(ipParam, nextParam, strlen(nextParam) + 1);
            } else {
                *ipParam = '\0'; // Truncate at ip parameter
            }
        }

        // Remove ei parameter (session identifier)
        char *eiParam = strstr(cleanedUrl, "&ei=");
        if (eiParam) {
            char *nextParam = strchr(eiParam + 1, '&');
            if (nextParam) {
                memmove(eiParam, nextParam, strlen(nextParam) + 1);
            } else {
                *eiParam = '\0';
            }
        }

        // Remove bui parameter (browser identifier)
        char *buiParam = strstr(cleanedUrl, "&bui=");
        if (buiParam) {
            char *nextParam = strchr(buiParam + 1, '&');
            if (nextParam) {
                memmove(buiParam, nextParam, strlen(nextParam) + 1);
            } else {
                *buiParam = '\0';
            }
        }

        // Remove spc parameter (security parameter)
        char *spcParam = strstr(cleanedUrl, "&spc=");
        if (spcParam) {
            char *nextParam = strchr(spcParam + 1, '&');
            if (nextParam) {
                memmove(spcParam, nextParam, strlen(nextParam) + 1);
            } else {
                *spcParam = '\0';
            }
        }

        url = cleanedUrl;
        LOGI("Cleaned URL: %s", url);
    }

    UrlParts parts;
    if (!parse_url(url, &parts, err, errLen)) {
        return false;
    }
    HttpStream stream = {0};
    if (!stream_open(&stream, &parts, err, errLen)) {
        return false;
    }
    char request[8192];  // Increased buffer size for long URLs
    // Add YouTube-specific headers to avoid 403 errors
    const char *extraHeaders = "";
    const char *userAgent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36";
    if (strstr(parts.host, "googlevideo.com")) {
        userAgent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36";
        extraHeaders = "Referer: https://www.youtube.com/\r\n"
                       "Origin: https://www.youtube.com\r\n"
                       "Accept: */*\r\n"
                       "Accept-Language: en-US,en;q=0.9\r\n"
                       "Accept-Encoding: identity\r\n"
                       "Sec-Fetch-Dest: video\r\n"
                       "Sec-Fetch-Mode: cors\r\n"
                       "Sec-Fetch-Site: cross-site\r\n";
    } else if (strstr(parts.host, "youtube.com") || strstr(parts.host, "youtu.be")) {
        // Use desktop user agent and complete headers for YouTube HTML pages
        extraHeaders = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9\r\n"
                       "Accept-Language: en-US,en;q=0.9\r\n"
                       "Accept-Encoding: identity\r\n"
                       "DNT: 1\r\n"
                       "Connection: keep-alive\r\n"
                       "Upgrade-Insecure-Requests: 1\r\n"
                       "Sec-Fetch-Dest: document\r\n"
                       "Sec-Fetch-Mode: navigate\r\n"
                       "Sec-Fetch-Site: none\r\n"
                       "Cache-Control: max-age=0\r\n";
    }
    int reqLen = snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n"
             "User-Agent: %s\r\n"
             "Accept: */*\r\n"
             "%s"
             "\r\n",
             parts.path, parts.host, userAgent, extraHeaders);
    if (reqLen >= (int)sizeof(request) - 1) {
        LOGE("HTTP request too long: %d bytes (max %zu)", reqLen, sizeof(request));
        stream_close(&stream);
        set_err(err, errLen, "Request too long");
        return false;
    }
    // Log first 500 chars of request for debugging
    char logBuf[512];
    snprintf(logBuf, sizeof(logBuf), "%.500s", request);
    LOGI("HTTP request (first 500 chars):\n%s", logBuf);
    ssize_t sent = stream_write(&stream, (const unsigned char *)request, strlen(request));
    if (sent <= 0) {
        stream_close(&stream);
        LOGE("Request send failed: sent=%zd url=%s", sent, url);
        set_err(err, errLen, "Request send failed");
        return false;
    }
    LOGI("HTTP request sent (%zd bytes) url=%s", sent, url);
    char *headers = NULL;
    if (!read_headers(&stream, &headers, err, errLen)) {
        stream_close(&stream);
        return false;
    }
    int status = parse_status(headers);
    LOGI("HTTP status %d url=%s", status, url);
    if (headers) {
        LOGI("HTTP headers:\n%s", headers);
    }
    if (status >= 300 && status < 400) {
        char redirect[2048];
        if (header_get_location(headers, redirect, sizeof(redirect))) {
            LOGI("HTTP redirect to %s", redirect);
            free(headers);
            stream_close(&stream);
            return perform_request(redirect, outFile, outBuffer, progress, user, err, errLen);
        }
    }
    if (status != 200) {
        free(headers);
        stream_close(&stream);
        LOGE("HTTP status not OK: %d", status);
        set_err(err, errLen, "HTTP status not OK");
        return false;
    }
    bool chunked = header_has_chunked(headers);
    long contentLength = parse_content_length(headers);
    free(headers);

    size_t total = 0;
    if (outBuffer) {
        size_t cap = contentLength > 0 ? (size_t)contentLength : (512 * 1024);
        outBuffer->data = (char *)malloc(cap + 1);
        outBuffer->size = 0;
        if (!outBuffer->data) {
            stream_close(&stream);
            set_err(err, errLen, "Buffer alloc failed");
            return false;
        }
        if (chunked) {
            bool ok = read_chunked_to_buffer(&stream, outBuffer, err, errLen);
            stream_close(&stream);
            return ok;
        }
        unsigned char buf[READ_CHUNK];
        int emptyReads = 0;
        while (true) {
            ssize_t got = stream_read(&stream, buf, sizeof(buf));
            if (got == 0) {
                if (++emptyReads > 1000) {
                    set_err(err, errLen, "Body read timeout");
                    stream_close(&stream);
                    return false;
                }
                usleep(1000);
                continue;
            }
            if (got < 0) {
                set_err(err, errLen, "Body read failed");
                stream_close(&stream);
                return false;
            }
            emptyReads = 0;
            if (outBuffer->size + (size_t)got + 1 > cap) {
                cap = (cap + (size_t)got) * 2;
                char *next = (char *)realloc(outBuffer->data, cap + 1);
                if (!next) {
                    stream_close(&stream);
                    set_err(err, errLen, "Buffer realloc failed");
                    return false;
                }
                outBuffer->data = next;
            }
            memcpy(outBuffer->data + outBuffer->size, buf, (size_t)got);
            outBuffer->size += (size_t)got;
        }
        outBuffer->data[outBuffer->size] = '\0';
        stream_close(&stream);
        return true;
    }

    if (chunked) {
        bool ok = read_chunked(&stream, outFile, progress, user, err, errLen);
        stream_close(&stream);
        return ok;
    }

    unsigned char buf[READ_CHUNK];
    int emptyReads = 0;
    while (true) {
        ssize_t got = stream_read(&stream, buf, sizeof(buf));
        if (got == 0) {
            if (++emptyReads > 1000) {
                set_err(err, errLen, "Body read timeout");
                stream_close(&stream);
                return false;
            }
            usleep(1000);
            continue;
        }
        if (got < 0) {
            set_err(err, errLen, "Body read failed");
            stream_close(&stream);
            return false;
        }
        emptyReads = 0;
        fwrite(buf, 1, (size_t)got, outFile);
        total += (size_t)got;
        if (progress) {
            progress(total, contentLength > 0 ? (size_t)contentLength : 0, user);
        }
    }
    stream_close(&stream);
    return true;
}

bool http_get_to_memory(const char *url, HttpBuffer *outBuffer,
                        char *err, size_t errLen) {
    if (!outBuffer) {
        set_err(err, errLen, "No buffer");
        return false;
    }
    outBuffer->data = NULL;
    outBuffer->size = 0;
    return perform_request(url, NULL, outBuffer, NULL, NULL, err, errLen);
}

bool http_download_to_file(const char *url, const char *filePath,
                           DownloadProgressCallback progress, void *user,
                           char *err, size_t errLen) {
    FILE *outFile = fopen(filePath, "wb");
    if (!outFile) {
        set_err(err, errLen, "Failed to open output file");
        return false;
    }
    bool ok = perform_request(url, outFile, NULL, progress, user, err, errLen);
    fclose(outFile);
    return ok;
}

void http_free_buffer(HttpBuffer *buffer) {
    if (!buffer) {
        return;
    }
    free(buffer->data);
    buffer->data = NULL;
    buffer->size = 0;
}
