#include "url_analyzer.h"
#include "html_media_extract.h"
#include "http_download.h"

#include <stdio.h>
#include <string.h>
#include <android/log.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>

#define LOG_TAG "url_analyzer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* File logging */
static void file_log(const char *fmt, ...) {
    static int log_fd = -1;
    if (log_fd < 0) {
        log_fd = open("/data/data/com.bgmdwldr.vulkan/analyzer_debug.log", 
                      O_WRONLY | O_CREAT | O_APPEND, 0644);
    }
    if (log_fd >= 0) {
        char buf[1024];
        va_list args;
        va_start(args, fmt);
        int n = vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        if (n > 0 && n < sizeof(buf) - 1) {
            buf[n++] = '\n';
            write(log_fd, buf, n);
        }
    }
}

static bool has_media_extension(const char *url) {
    const char *ext = strrchr(url, '.');
    if (!ext) {
        return false;
    }
    return strcmp(ext, ".mp4") == 0 || strcmp(ext, ".webm") == 0 ||
           strcmp(ext, ".m3u8") == 0 || strcmp(ext, ".m4a") == 0 ||
           strcmp(ext, ".mp3") == 0;
}

static void set_err(char *err, size_t errLen, const char *msg) {
    if (err && errLen > 0) {
        snprintf(err, errLen, "%s", msg);
    }
}

static void resolve_url(const char *baseUrl, const char *candidate,
                        char *out, size_t outLen) {
    if (strstr(candidate, "://")) {
        snprintf(out, outLen, "%s", candidate);
        return;
    }
    const char *schemeEnd = strstr(baseUrl, "://");
    if (!schemeEnd) {
        snprintf(out, outLen, "%s", candidate);
        return;
    }
    size_t schemeLen = (size_t)(schemeEnd - baseUrl);
    const char *hostStart = schemeEnd + 3;
    const char *pathStart = strchr(hostStart, '/');
    size_t hostLen = pathStart ? (size_t)(pathStart - hostStart) : strlen(hostStart);
    if (candidate[0] == '/' && candidate[1] == '/') {
        snprintf(out, outLen, "%.*s:%s", (int)schemeLen, baseUrl, candidate);
        return;
    }
    if (candidate[0] == '/') {
        snprintf(out, outLen, "%.*s://%.*s%s",
                 (int)schemeLen, baseUrl,
                 (int)hostLen, hostStart, candidate);
        return;
    }
    snprintf(out, outLen, "%.*s://%.*s/%s",
             (int)schemeLen, baseUrl,
             (int)hostLen, hostStart, candidate);
}

bool url_analyze(const char *inputUrl, MediaUrl *outMedia, char *err, size_t errLen) {
    LOGI("Starting URL analysis for: %.100s...", inputUrl);
    file_log("Starting URL analysis for: %.100s...", inputUrl);
    
    if (!inputUrl || !outMedia) {
        set_err(err, errLen, "Invalid URL input");
        LOGE("Invalid input: url=%p, out=%p", (void*)inputUrl, (void*)outMedia);
        return false;
    }
    outMedia->url[0] = '\0';
    outMedia->mime[0] = '\0';
    
    if (has_media_extension(inputUrl)) {
        LOGI("URL has media extension, using directly");
        file_log("URL has media extension, using directly");
        snprintf(outMedia->url, sizeof(outMedia->url), "%s", inputUrl);
        return true;
    }

    LOGI("Fetching HTML from URL...");
    file_log("Fetching HTML from URL...");
    HttpBuffer html = {0};
    if (!http_get_to_memory(inputUrl, &html, err, errLen)) {
        LOGE("HTTP fetch failed: %s", err);
        file_log("HTTP fetch failed: %s", err);
        return false;
    }
    LOGI("Received %zu bytes of HTML", html.size);
    file_log("Received %zu bytes of HTML", html.size);

    LOGI("Extracting media URL from HTML...");
    file_log("Extracting media URL from HTML...");
    HtmlMediaCandidate candidate;
    if (!html_extract_media_url(html.data, &candidate, err, errLen)) {
        LOGE("Media extraction failed: %s", err);
        file_log("Media extraction failed: %s", err);
        http_free_buffer(&html);
        return false;
    }
    LOGI("Found media URL: %.100s...", candidate.url);
    file_log("Found media URL: %.100s...", candidate.url);
    
    http_free_buffer(&html);
    resolve_url(inputUrl, candidate.url, outMedia->url, sizeof(outMedia->url));
    if (candidate.mime[0]) {
        snprintf(outMedia->mime, sizeof(outMedia->mime), "%s", candidate.mime);
    }
    LOGI("URL analysis complete");
    return true;
}
