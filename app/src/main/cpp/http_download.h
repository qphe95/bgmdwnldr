#ifndef HTTP_DOWNLOAD_H
#define HTTP_DOWNLOAD_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*DownloadProgressCallback)(size_t downloaded, size_t total, void *user);

typedef struct HttpBuffer {
    char *data;
    size_t size;
} HttpBuffer;

bool http_get_to_memory(const char *url, HttpBuffer *outBuffer,
                        char *err, size_t errLen);

bool http_download_to_file(const char *url, const char *filePath,
                           DownloadProgressCallback progress, void *user,
                           char *err, size_t errLen);

void http_free_buffer(HttpBuffer *buffer);

#ifdef __cplusplus
}
#endif

#endif
