#ifndef MEDIA_STORE_H
#define MEDIA_STORE_H

#include <jni.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "third_party/quickjs/quickjs_gc_unified.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MediaStoreHandle {
    int fd;
    GCHandle uri_handle;   /* Handle to JNI global ref for Uri object */
    GCHandle pfd_handle;   /* Handle to JNI global ref for ParcelFileDescriptor */
} MediaStoreHandle;

bool media_store_create_audio(JavaVM *vm, jobject activity,
                              const char *displayName, const char *mimeType,
                              MediaStoreHandle *outHandle,
                              char *err, size_t errLen);
bool media_store_finalize(JavaVM *vm, jobject activity,
                          MediaStoreHandle *handle,
                          char *err, size_t errLen);
void media_store_close(JavaVM *vm, MediaStoreHandle *handle);

#ifdef __cplusplus
}
#endif

#endif
