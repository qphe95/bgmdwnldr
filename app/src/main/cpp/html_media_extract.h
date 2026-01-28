#ifndef HTML_MEDIA_EXTRACT_H
#define HTML_MEDIA_EXTRACT_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HtmlMediaCandidate {
    char url[2048];
    char mime[64];
} HtmlMediaCandidate;

bool html_extract_media_url(const char *html, HtmlMediaCandidate *outCandidate,
                            char *err, size_t errLen);

#ifdef __cplusplus
}
#endif

#endif
