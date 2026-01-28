#include "http_download.h"

#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>

#define LOG_TAG "minimalvulkan"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// JNI references for WebView communication
static JavaVM *g_javaVM = NULL;
static jobject g_mainActivity = NULL;

// WebView-based video download
void http_download_via_webview(const char *url, void *app) {
    LOGI("Requesting WebView to download video: %s", url);

    if (!g_javaVM || !g_mainActivity) {
        LOGE("JNI references not set for WebView download");
        return;
    }

    JNIEnv *env;
    if ((*g_javaVM)->GetEnv(g_javaVM, (void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        LOGE("Failed to get JNI environment");
        return;
    }

    // Find the MainActivity class
    jclass activityClass = (*env)->FindClass(env, "com/bgmdwldr/vulkan/MainActivity");
    if (!activityClass) {
        LOGE("Failed to find MainActivity class");
        return;
    }

    // Get the downloadVideo method
    jmethodID downloadMethod = (*env)->GetMethodID(env, activityClass, "downloadVideoViaWebView", "(Ljava/lang/String;)V");
    if (!downloadMethod) {
        LOGE("Failed to find downloadVideoViaWebView method");
        return;
    }

    // Create Java string from URL
    jstring urlString = (*env)->NewStringUTF(env, url);
    if (!urlString) {
        LOGE("Failed to create URL string");
        return;
    }

    // Call the Java method (WebView handles the download)
    (*env)->CallVoidMethod(env, g_mainActivity, downloadMethod, urlString);

    // Clean up
    (*env)->DeleteLocalRef(env, urlString);
    (*env)->DeleteLocalRef(env, activityClass);

    LOGI("WebView download request sent for: %s", url);
}

// Set JNI references for WebView communication
void http_download_set_jni_refs(JavaVM *vm, jobject activity) {
    g_javaVM = vm;
    if (g_mainActivity) {
        // Clean up previous reference
        JNIEnv *env;
        if ((*g_javaVM)->GetEnv(g_javaVM, (void**)&env, JNI_VERSION_1_6) == JNI_OK) {
            (*env)->DeleteGlobalRef(env, g_mainActivity);
        }
    }
    // Create global reference to activity
    JNIEnv *env;
    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) == JNI_OK) {
        g_mainActivity = (*env)->NewGlobalRef(env, activity);
    }
    LOGI("JNI references set for WebView communication");
}

// Request Java to load YouTube page in WebView for session extraction
void http_download_load_youtube_page(const char *url) {
    LOGI("Requesting WebView to load YouTube page: %s", url);

    if (!g_javaVM || !g_mainActivity) {
        LOGE("JNI references not set for WebView page loading");
        return;
    }

    JNIEnv *env;
    if ((*g_javaVM)->GetEnv(g_javaVM, (void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        LOGE("Failed to get JNI environment");
        return;
    }

    // Find the MainActivity class
    jclass activityClass = (*env)->FindClass(env, "com/bgmdwldr/vulkan/MainActivity");
    if (!activityClass) {
        LOGE("Failed to find MainActivity class");
        return;
    }

    // Get the loadYouTubePage method
    jmethodID loadMethod = (*env)->GetMethodID(env, activityClass, "loadYouTubePage", "(Ljava/lang/String;)V");
    if (!loadMethod) {
        LOGE("Failed to find loadYouTubePage method");
        return;
    }

    // Create Java string from URL
    jstring urlString = (*env)->NewStringUTF(env, url);
    if (!urlString) {
        LOGE("Failed to create URL string");
        return;
    }

    // Call the Java method (this will be asynchronous)
    (*env)->CallVoidMethod(env, g_mainActivity, loadMethod, urlString);

    // Clean up
    (*env)->DeleteLocalRef(env, urlString);
    (*env)->DeleteLocalRef(env, activityClass);
}

// Legacy functions (no longer used)
bool http_get_to_memory(const char *url, HttpBuffer *outBuffer, char *err, size_t errLen) {
    (void)url; (void)outBuffer; (void)err; (void)errLen;
    LOGE("http_get_to_memory: WebView-only mode, this function is deprecated");
    return false;
}

bool http_download_to_file(const char *url, const char *filePath, DownloadProgressCallback progress, void *user, char *err, size_t errLen) {
    (void)url; (void)filePath; (void)progress; (void)user; (void)err; (void)errLen;
    LOGE("http_download_to_file: WebView-only mode, this function is deprecated");
    return false;
}

void http_free_buffer(HttpBuffer *buffer) {
    (void)buffer;
    // No-op in WebView mode
}

void http_download_set_youtube_cookies(const char *cookies) {
    (void)cookies;
    LOGI("Setting YouTube cookies (WebView mode - no-op)");
}

void http_download_set_js_session_data(const char *session) {
    (void)session;
    LOGI("Setting JS session data (WebView mode - no-op)");
}
