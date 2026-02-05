/*
 * Comprehensive browser environment stubs for YouTube player JS execution
 * These stubs provide the minimal browser APIs needed for obfuscated JS to run
 */

#ifndef BROWSER_STUBS_H
#define BROWSER_STUBS_H

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <stdlib.h>
#include <string.h>

/* Function to load browser stubs from assets file */
static inline char* load_browser_stubs_from_assets(AAssetManager* mgr, size_t* out_len) {
    if (!mgr) return NULL;
    
    AAsset* asset = AAssetManager_open(mgr, "browser_stubs.js", AASSET_MODE_STREAMING);
    if (!asset) return NULL;
    
    off_t length = AAsset_getLength(asset);
    if (length <= 0) {
        AAsset_close(asset);
        return NULL;
    }
    
    char* buffer = (char*)malloc((size_t)length + 1);
    if (!buffer) {
        AAsset_close(asset);
        return NULL;
    }
    
    int64_t readBytes = AAsset_read(asset, buffer, (size_t)length);
    AAsset_close(asset);
    
    if (readBytes != length) {
        free(buffer);
        return NULL;
    }
    
    buffer[length] = '\0';
    if (out_len) *out_len = (size_t)length;
    return buffer;
}

/* No fallback - must load from assets */

#endif /* BROWSER_STUBS_H */
