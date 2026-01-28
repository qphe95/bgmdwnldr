#ifndef HTTP_DOWNLOAD_H
#define HTTP_DOWNLOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <jni.h>

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

void http_free_buffer(HttpBuffer *buffer);

// WebView-based downloading
void http_download_via_webview(const char *url, void *app);

// WebView session management
void http_download_set_jni_refs(JavaVM *vm, jobject activity);
void http_download_load_youtube_page(const char *url);
void http_download_set_youtube_cookies(const char *cookies);
void http_download_set_js_session_data(const char *session);

// Cookie management for YouTube media downloads
void http_set_youtube_cookies(const char *cookies);
const char* http_get_youtube_cookies(void);
void http_clear_youtube_cookies(void);

#ifdef __cplusplus
}
#endif

#endif
