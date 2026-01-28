#include "url_analyzer.h"
#include "html_media_extract.h"
#include "http_download.h"

#include <stdio.h>
#include <string.h>

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
    if (!inputUrl || !outMedia) {
        set_err(err, errLen, "Invalid URL input");
        return false;
    }
    outMedia->url[0] = '\0';
    outMedia->mime[0] = '\0';
    if (has_media_extension(inputUrl)) {
        snprintf(outMedia->url, sizeof(outMedia->url), "%s", inputUrl);
        return true;
    }

    HttpBuffer html = {0};
    if (!http_get_to_memory(inputUrl, &html, err, errLen)) {
        return false;
    }

    HtmlMediaCandidate candidate;
    if (!html_extract_media_url(html.data, &candidate, err, errLen)) {
        http_free_buffer(&html);
        return false;
    }
    http_free_buffer(&html);
    resolve_url(inputUrl, candidate.url, outMedia->url, sizeof(outMedia->url));
    if (candidate.mime[0]) {
        snprintf(outMedia->mime, sizeof(outMedia->mime), "%s", candidate.mime);
    }
    return true;
}
