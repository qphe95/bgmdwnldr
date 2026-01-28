#ifndef URL_ANALYZER_H
#define URL_ANALYZER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEDIA_URL_MAX 2048
#define MEDIA_MIME_MAX 64

typedef struct MediaUrl {
    char url[MEDIA_URL_MAX];
    char mime[MEDIA_MIME_MAX];
} MediaUrl;

bool url_analyze(const char *inputUrl, MediaUrl *outMedia, char *err, size_t errLen);

#ifdef __cplusplus
}
#endif

#endif
