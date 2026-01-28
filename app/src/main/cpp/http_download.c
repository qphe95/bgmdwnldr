#include "http_download.h"
#include "tls_client.h"

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <android/log.h>

#define HEADER_LIMIT (64 * 1024)
#define READ_CHUNK 4096

#define LOG_TAG "minimalvulkan"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Cookie storage structure
typedef struct Cookie {
    char name[256];
    char value[2048];
    char domain[256];
    char path[256];
    time_t expires;  // 0 means session cookie
    bool secure;
    bool httpOnly;
    struct Cookie *next;
} Cookie;

// Global cookie jar
static Cookie *g_cookieJar = NULL;
static pthread_mutex_t g_cookieMutex = PTHREAD_MUTEX_INITIALIZER;

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

static time_t parse_cookie_date(const char *dateStr) {
    // Simple date parsing - handles common formats
    // Returns 0 if parsing fails (session cookie)
    // Format: "Wed, 28 Jan 2026 09:02:06 GMT"
    struct tm tm = {0};
    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    // Skip day name if present
    const char *p = dateStr;
    while (*p && (*p == ' ' || *p == ',')) p++;

    // Parse day
    tm.tm_mday = (int)strtol(p, NULL, 10);
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    // Parse month
    for (int i = 0; i < 12; i++) {
        if (strncmp(p, months[i], 3) == 0) {
            tm.tm_mon = i;
            break;
        }
    }
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    // Parse year
    tm.tm_year = (int)strtol(p, NULL, 10) - 1900;
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    // Parse time HH:MM:SS
    tm.tm_hour = (int)strtol(p, NULL, 10);
    p = strchr(p, ':');
    if (p) {
        tm.tm_min = (int)strtol(p + 1, NULL, 10);
        p = strchr(p + 1, ':');
        if (p) {
            tm.tm_sec = (int)strtol(p + 1, NULL, 10);
        }
    }

    tm.tm_isdst = -1;
    time_t result = mktime(&tm);
    return result > 0 ? result : 0;
}

static bool domain_matches(const char *cookieDomain, const char *requestHost) {
    if (!cookieDomain || !requestHost) return false;

    // Exact match
    if (strcmp(cookieDomain, requestHost) == 0) return true;

    // Domain match: .youtube.com matches www.youtube.com, youtube.com, and googlevideo.com
    if (cookieDomain[0] == '.') {
        // For YouTube cookies, also allow googlevideo.com
        if (strstr(requestHost, "googlevideo.com") && strstr(cookieDomain, "youtube.com")) {
            return true;
        }

        // Check if requestHost ends with cookieDomain (without leading dot)
        size_t domainLen = strlen(cookieDomain + 1);
        size_t hostLen = strlen(requestHost);
        if (hostLen >= domainLen) {
            const char *hostSuffix = requestHost + hostLen - domainLen;
            if (strcmp(hostSuffix, cookieDomain + 1) == 0) {
                return true;
            }
        }
    }

    return false;
}

static bool path_matches(const char *cookiePath, const char *requestPath) {
    if (!cookiePath || !requestPath) return false;
    if (cookiePath[0] == '\0') return true; // Empty path matches all

    // Cookie path must be a prefix of request path
    size_t pathLen = strlen(cookiePath);

    // Handle root path specially - "/" matches everything starting with "/"
    if (strcmp(cookiePath, "/") == 0 && requestPath[0] == '/') {
        return true;
    }

    if (strncmp(cookiePath, requestPath, pathLen) == 0) {
        // Exact match or request path continues with /
        if (requestPath[pathLen] == '\0' || requestPath[pathLen] == '/') {
            return true;
        }
    }
    return false;
}

static Cookie *find_cookie(const char *name, const char *domain, const char *path) {
    Cookie *cookie = g_cookieJar;
    while (cookie) {
        if (strcmp(cookie->name, name) == 0 &&
            domain_matches(cookie->domain, domain) &&
            path_matches(cookie->path, path) &&
            (cookie->expires == 0 || cookie->expires > time(NULL))) {
            return cookie;
        }
        cookie = cookie->next;
    }
    return NULL;
}

static void remove_cookie(const char *name, const char *domain, const char *path) {
    Cookie **prev = &g_cookieJar;
    Cookie *cookie = g_cookieJar;

    while (cookie) {
        if (strcmp(cookie->name, name) == 0 &&
            domain_matches(cookie->domain, domain) &&
            path_matches(cookie->path, path)) {
            *prev = cookie->next;
            free(cookie);
            cookie = *prev;
        } else {
            prev = &cookie->next;
            cookie = cookie->next;
        }
    }
}

static void add_cookie(const char *name, const char *value, const char *domain,
                      const char *path, time_t expires, bool secure, bool httpOnly) {
    // Remove existing cookie with same name/domain/path
    remove_cookie(name, domain, path);

    // Check expiration
    if (expires > 0 && expires <= time(NULL)) {
        return; // Expired, don't add
    }

    Cookie *cookie = (Cookie *)calloc(1, sizeof(Cookie));
    if (!cookie) return;

    snprintf(cookie->name, sizeof(cookie->name), "%s", name);
    snprintf(cookie->value, sizeof(cookie->value), "%s", value);
    if (domain) {
        snprintf(cookie->domain, sizeof(cookie->domain), "%s", domain);
    }
    if (path) {
        snprintf(cookie->path, sizeof(cookie->path), "%s", path);
    } else {
        snprintf(cookie->path, sizeof(cookie->path), "/");
    }
    cookie->expires = expires;
    cookie->secure = secure;
    cookie->httpOnly = httpOnly;

    cookie->next = g_cookieJar;
    g_cookieJar = cookie;
}

static void parse_set_cookie_header(const char *setCookie, const char *defaultDomain,
                                    const char *defaultPath) {
    if (!setCookie) return;

    // Skip "Set-Cookie:" prefix
    const char *p = setCookie;
    if (strncasecmp(p, "Set-Cookie:", 11) == 0) {
        p += 11;
    }
    while (*p == ' ' || *p == '\t') p++;

    // Extract name=value (first part)
    const char *nameStart = p;
    const char *equals = strchr(p, '=');
    if (!equals) return;

    size_t nameLen = (size_t)(equals - nameStart);
    char name[256];
    if (nameLen >= sizeof(name)) nameLen = sizeof(name) - 1;
    memcpy(name, nameStart, nameLen);
    name[nameLen] = '\0';

    // Extract value (until semicolon or end)
    const char *valueStart = equals + 1;
    const char *valueEnd = strchr(valueStart, ';');
    if (!valueEnd) valueEnd = valueStart + strlen(valueStart);

    size_t valueLen = (size_t)(valueEnd - valueStart);
    char value[2048];
    if (valueLen >= sizeof(value)) valueLen = sizeof(value) - 1;
    memcpy(value, valueStart, valueLen);
    value[valueLen] = '\0';

    // Parse attributes
    char domain[256] = {0};
    char path[256] = {0};
    time_t expires = 0;
    bool secure = false;
    bool httpOnly = false;

    p = valueEnd;
    while (*p) {
        while (*p == ';' || *p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        const char *attrStart = p;
        const char *attrEnd = strchr(p, ';');
        if (!attrEnd) attrEnd = p + strlen(p);

        // Check for Domain=
        if (strncasecmp(attrStart, "Domain=", 7) == 0) {
            size_t domainLen = (size_t)(attrEnd - attrStart - 7);
            if (domainLen < sizeof(domain)) {
                memcpy(domain, attrStart + 7, domainLen);
                domain[domainLen] = '\0';
            }
        }
        // Check for Path=
        else if (strncasecmp(attrStart, "Path=", 5) == 0) {
            size_t pathLen = (size_t)(attrEnd - attrStart - 5);
            if (pathLen < sizeof(path)) {
                memcpy(path, attrStart + 5, pathLen);
                path[pathLen] = '\0';
            }
        }
        // Check for Expires=
        else if (strncasecmp(attrStart, "Expires=", 8) == 0) {
            size_t dateLen = (size_t)(attrEnd - attrStart - 8);
            char dateStr[64];
            if (dateLen < sizeof(dateStr)) {
                memcpy(dateStr, attrStart + 8, dateLen);
                dateStr[dateLen] = '\0';
                expires = parse_cookie_date(dateStr);
            }
        }
        // Check for Max-Age=
        else if (strncasecmp(attrStart, "Max-Age=", 8) == 0) {
            long maxAge = strtol(attrStart + 8, NULL, 10);
            if (maxAge > 0) {
                expires = time(NULL) + maxAge;
            } else {
                expires = 0; // Session cookie
            }
        }
        // Check for Secure flag
        else if (strncasecmp(attrStart, "Secure", 6) == 0) {
            secure = true;
        }
        // Check for HttpOnly flag
        else if (strncasecmp(attrStart, "HttpOnly", 8) == 0) {
            httpOnly = true;
        }

        p = attrEnd;
    }

    // Use defaults if not specified
    if (domain[0] == '\0' && defaultDomain) {
        snprintf(domain, sizeof(domain), "%s", defaultDomain);
    }
    if (path[0] == '\0' && defaultPath) {
        snprintf(path, sizeof(path), "%s", defaultPath);
    } else if (path[0] == '\0') {
        snprintf(path, sizeof(path), "/");
    }

    // Normalize domain: add leading dot for domain cookies
    if (domain[0] != '\0' && domain[0] != '.') {
        char normalized[256];
        snprintf(normalized, sizeof(normalized), ".%s", domain);
        snprintf(domain, sizeof(domain), "%s", normalized);
    }

    add_cookie(name, value, domain[0] ? domain : defaultDomain,
               path[0] ? path : defaultPath, expires, secure, httpOnly);

    LOGI("Stored cookie: %s=%s (domain=%s, path=%s)", name, value,
         domain[0] ? domain : (defaultDomain ? defaultDomain : ""),
         path[0] ? path : (defaultPath ? defaultPath : "/"));
}

static void parse_all_set_cookies(const char *headers, const char *domain, const char *path) {
    LOGI("Looking for Set-Cookie headers in response...");
    const char *p = headers;
    int cookieCount = 0;
    while ((p = strcasestr(p, "Set-Cookie:")) != NULL) {
        cookieCount++;
        const char *lineEnd = strstr(p, "\r\n");
        if (!lineEnd) lineEnd = p + strlen(p);

        size_t cookieLen = (size_t)(lineEnd - p);
        char cookieLine[2048];
        if (cookieLen < sizeof(cookieLine)) {
            memcpy(cookieLine, p, cookieLen);
            cookieLine[cookieLen] = '\0';
            LOGI("Found Set-Cookie header: %.100s...", cookieLine);
            parse_set_cookie_header(cookieLine, domain, path);
        }

        p = lineEnd;
    }
    LOGI("Found %d Set-Cookie headers total", cookieCount);
}

static void build_cookie_header_internal(char *out, size_t outLen, const char *domain, const char *fullPath, const char *pathOnly) {
    out[0] = '\0';
    size_t pos = 0;
    bool first = true;
    int cookieCount = 0;

    // Count total cookies in jar
    int totalCookies = 0;
    Cookie *countCookie = g_cookieJar;
    while (countCookie) {
        totalCookies++;
        countCookie = countCookie->next;
    }

    LOGI("Building cookies for domain=%s, path=%s (total cookies in jar: %d)", domain, fullPath, totalCookies);

    Cookie *cookie = g_cookieJar;
    while (cookie) {
        bool domainMatch = domain_matches(cookie->domain, domain);
        bool pathMatch = path_matches(cookie->path, pathOnly);
        bool notExpired = (cookie->expires == 0 || cookie->expires > time(NULL));

        LOGI("Checking cookie: %s=%s (domain=%s, path=%s) - domain_match=%d, path_match=%d, not_expired=%d",
             cookie->name, cookie->value, cookie->domain, cookie->path,
             domainMatch, pathMatch, notExpired);

        // Check if cookie matches domain and path
        if (domainMatch && pathMatch && notExpired) {

            cookieCount++;
            LOGI("Including cookie: %s=%s (domain=%s, path=%s)",
                 cookie->name, cookie->value, cookie->domain, cookie->path);

            if (!first) {
                if (pos + 2 < outLen) {
                    out[pos++] = ';';
                    out[pos++] = ' ';
                }
            }
            first = false;

            // Format: name=value
            size_t nameLen = strlen(cookie->name);
            size_t valueLen = strlen(cookie->value);
            if (pos + nameLen + valueLen + 1 < outLen) {
                memcpy(out + pos, cookie->name, nameLen);
                pos += nameLen;
                out[pos++] = '=';
                memcpy(out + pos, cookie->value, valueLen);
                pos += valueLen;
            } else {
                break; // Out of space
            }
        }
        cookie = cookie->next;
    }
    out[pos] = '\0';
    LOGI("Built cookie header with %d cookies: %s", cookieCount, out[0] ? out : "(empty)");
}

static void build_cookie_header(char *out, size_t outLen, const char *domain, const char *path) {
    // Extract path part only (before query string) for cookie matching
    char pathOnly[2048] = {0};
    const char *queryStart = strchr(path, '?');
    if (queryStart) {
        size_t pathLen = (size_t)(queryStart - path);
        if (pathLen < sizeof(pathOnly)) {
            memcpy(pathOnly, path, pathLen);
            pathOnly[pathLen] = '\0';
        } else {
            snprintf(pathOnly, sizeof(pathOnly), "%s", path);
        }
    } else {
        snprintf(pathOnly, sizeof(pathOnly), "%s", path);
    }

    // Use pathOnly for cookie matching but keep original path for logging
    build_cookie_header_internal(out, outLen, domain, path, pathOnly);
}

static void cleanup_expired_cookies(void) {
    time_t now = time(NULL);
    Cookie **prev = &g_cookieJar;
    Cookie *cookie = g_cookieJar;

    while (cookie) {
        if (cookie->expires > 0 && cookie->expires <= now) {
            *prev = cookie->next;
            free(cookie);
            cookie = *prev;
        } else {
            prev = &cookie->next;
            cookie = cookie->next;
        }
    }
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
    // Don't modify googlevideo.com URLs - they have cryptographic signatures
    // that break if any parameters are changed. Use URLs exactly as extracted.
    char cleanedUrl[4096];
    if (strstr(url, "googlevideo.com")) {
        // Keep the URL exactly as extracted - don't modify any parameters
        // The signatures depend on all parameters being intact
        strcpy(cleanedUrl, url);
        url = cleanedUrl;
        LOGI("Using original googlevideo URL (no parameter modifications to preserve signatures)");
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
    char extraHeadersBuf[8192] = {0};
    const char *userAgent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36";

    // Build cookie header
    char cookieHeader[4096] = {0};
    build_cookie_header(cookieHeader, sizeof(cookieHeader), parts.host, parts.path);

    if (strstr(parts.host, "googlevideo.com")) {
        userAgent = "Mozilla/5.0 (Linux; Android 10; SM-G973F) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Mobile Safari/537.36";
        // Extract visitor ID from cookies for X-Goog-Visitor-Id header
        char visitorHeader[512] = {0};
        Cookie *currCookie = g_cookieJar;
        while (currCookie) {
            if (strcmp(currCookie->name, "VISITOR_INFO1_LIVE") == 0) {
                snprintf(visitorHeader, sizeof(visitorHeader),
                        "X-Goog-Visitor-Id: %s\r\n", currCookie->value);
                break;
            }
            currCookie = currCookie->next;
        }

        snprintf(extraHeadersBuf, sizeof(extraHeadersBuf),
                 "Referer: https://www.youtube.com/\r\n"
                 "Origin: https://www.youtube.com\r\n"
                 "Accept: */*\r\n"
                 "Accept-Language: en-US,en;q=0.9\r\n"
                 "Accept-Encoding: identity\r\n"
                 "Cache-Control: no-cache\r\n"
                 "Pragma: no-cache\r\n"
                 "Sec-Fetch-Dest: video\r\n"
                 "Sec-Fetch-Mode: cors\r\n"
                 "Sec-Fetch-Site: cross-site\r\n"
                 "DNT: 1\r\n"
                 "X-Requested-With: XMLHttpRequest\r\n"
                 "X-YouTube-Client-Name: 1\r\n"
                 "X-YouTube-Client-Version: 2.20240101.01.00\r\n"
                 "%s"
                 "X-Goog-AuthUser: 0\r\n",
                 visitorHeader);
        if (cookieHeader[0] != '\0') {
            char cookieLine[4096];
            snprintf(cookieLine, sizeof(cookieLine), "Cookie: %s\r\n", cookieHeader);
            strncat(extraHeadersBuf, cookieLine, sizeof(extraHeadersBuf) - strlen(extraHeadersBuf) - 1);
        }
    } else if (strstr(parts.host, "youtube.com") || strstr(parts.host, "youtu.be")) {
        // Use Android client headers to get ANDROID format URLs instead of WEB
        userAgent = "Mozilla/5.0 (Linux; Android 10; SM-G973F) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Mobile Safari/537.36";
        snprintf(extraHeadersBuf, sizeof(extraHeadersBuf),
                 "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9\r\n"
                 "Accept-Language: en-US,en;q=0.9\r\n"
                 "Accept-Encoding: identity\r\n"
                 "DNT: 1\r\n"
                 "Connection: keep-alive\r\n"
                 "Upgrade-Insecure-Requests: 1\r\n"
                 "Sec-Fetch-Dest: document\r\n"
                 "Sec-Fetch-Mode: navigate\r\n"
                 "Sec-Fetch-Site: none\r\n"
                 "Cache-Control: max-age=0\r\n"
                 "X-Requested-With: XMLHttpRequest\r\n"
                 "X-YouTube-Client-Name: 16\r\n"  // Android client
                 "X-YouTube-Client-Version: 17.31.35\r\n");
        if (cookieHeader[0] != '\0') {
            char cookieLine[4096];
            snprintf(cookieLine, sizeof(cookieLine), "Cookie: %s\r\n", cookieHeader);
            strncat(extraHeadersBuf, cookieLine, sizeof(extraHeadersBuf) - strlen(extraHeadersBuf) - 1);
        }
    }

    int reqLen = snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n"
             "User-Agent: %s\r\n"
             "Accept: */*\r\n"
             "%s"
             "\r\n",
             parts.path, parts.host, userAgent, extraHeadersBuf);
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
    // Parse and store cookies from response
    cleanup_expired_cookies();
    parse_all_set_cookies(headers, parts.host, parts.path);

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
