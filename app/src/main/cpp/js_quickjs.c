#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <ctype.h>
#include <android/log.h>
#include "js_quickjs.h"
#include "cutils.h"
#include "quickjs.h"
#include "browser_stubs.h"

#define LOG_TAG "js_quickjs"
#define LOG_INFO(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOG_ERROR(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOG_WARN(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

#define MAX_CAPTURED_URLS 64
#define URL_MAX_LEN 2048

// Forward declarations
static JSValue js_dummy_function(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

static JSClassID js_http_response_class_id;
static JSClassID js_xhr_class_id;
static JSClassID js_video_class_id;

// Sanitize JSON string for QuickJS by handling invalid UTF-8 and control characters
static char* sanitize_json_for_qjs(const char *json, size_t len) {
    if (!json) return NULL;
    
    // Allocate extra space for replacements
    char *sanitized = malloc(len * 2 + 1);
    if (!sanitized) return NULL;
    
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)json[i];
        
        // ASCII control characters (0x00-0x1F) - keep tab, newline, carriage return
        if (c < 0x20) {
            if (c == '\t' || c == '\n' || c == '\r') {
                sanitized[j++] = c;
            } else {
                sanitized[j++] = ' ';
            }
        }
        // ASCII printable (0x20-0x7E)
        else if (c <= 0x7E) {
            sanitized[j++] = c;
        }
        // High bytes - need to validate UTF-8
        else {
            // Check for valid UTF-8 multi-byte sequence
            // 2-byte sequence: 110xxxxx 10xxxxxx
            if ((c & 0xE0) == 0xC0 && i + 1 < len) {
                unsigned char c2 = (unsigned char)json[i + 1];
                if ((c2 & 0xC0) == 0x80) {
                    // Valid 2-byte sequence
                    sanitized[j++] = c;
                    sanitized[j++] = c2;
                    i++;
                    continue;
                }
            }
            // 3-byte sequence: 1110xxxx 10xxxxxx 10xxxxxx
            else if ((c & 0xF0) == 0xE0 && i + 2 < len) {
                unsigned char c2 = (unsigned char)json[i + 1];
                unsigned char c3 = (unsigned char)json[i + 2];
                if ((c2 & 0xC0) == 0x80 && (c3 & 0xC0) == 0x80) {
                    // Valid 3-byte sequence
                    sanitized[j++] = c;
                    sanitized[j++] = c2;
                    sanitized[j++] = c3;
                    i += 2;
                    continue;
                }
            }
            // 4-byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
            else if ((c & 0xF8) == 0xF0 && i + 3 < len) {
                unsigned char c2 = (unsigned char)json[i + 1];
                unsigned char c3 = (unsigned char)json[i + 2];
                unsigned char c4 = (unsigned char)json[i + 3];
                if ((c2 & 0xC0) == 0x80 && (c3 & 0xC0) == 0x80 && (c4 & 0xC0) == 0x80) {
                    // Valid 4-byte sequence
                    sanitized[j++] = c;
                    sanitized[j++] = c2;
                    sanitized[j++] = c3;
                    sanitized[j++] = c4;
                    i += 3;
                    continue;
                }
            }
            // Invalid UTF-8 sequence - replace with space
            sanitized[j++] = ' ';
        }
    }
    sanitized[j] = '\0';
    return sanitized;
}

// Global state for URL capture
static char g_captured_urls[MAX_CAPTURED_URLS][URL_MAX_LEN];
static int g_captured_url_count = 0;
static pthread_mutex_t g_url_mutex = PTHREAD_MUTEX_INITIALIZER;

// Record a captured URL
void record_captured_url(const char *url) {
    if (!url || strlen(url) == 0) return;
    
    pthread_mutex_lock(&g_url_mutex);
    
    // Check for duplicates
    for (int i = 0; i < g_captured_url_count; i++) {
        if (strcmp(g_captured_urls[i], url) == 0) {
            pthread_mutex_unlock(&g_url_mutex);
            return;
        }
    }
    
    // Add new URL
    if (g_captured_url_count < MAX_CAPTURED_URLS) {
        strncpy(g_captured_urls[g_captured_url_count], url, URL_MAX_LEN - 1);
        g_captured_urls[g_captured_url_count][URL_MAX_LEN - 1] = '\0';
        g_captured_url_count++;
        LOG_INFO("Captured URL: %.100s...", url);
    }
    
    pthread_mutex_unlock(&g_url_mutex);
}

// XMLHttpRequest implementation with full event simulation
typedef struct {
    char url[2048];
    char method[16];
    int ready_state;
    int status;
    char response_text[8192];
    char response_headers[2048];
    JSValue onload;
    JSValue onerror;
    JSValue onreadystatechange;
    JSValue headers;
    JSContext *ctx;
} XMLHttpRequest;

static void js_xhr_finalizer(JSRuntime *rt, JSValue val) {
    XMLHttpRequest *xhr = JS_GetOpaque(val, js_xhr_class_id);
    if (xhr) {
        JS_FreeValueRT(rt, xhr->onload);
        JS_FreeValueRT(rt, xhr->onerror);
        JS_FreeValueRT(rt, xhr->onreadystatechange);
        JS_FreeValueRT(rt, xhr->headers);
        free(xhr);
    }
}

static JSValue js_xhr_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    XMLHttpRequest *xhr = calloc(1, sizeof(XMLHttpRequest));
    if (!xhr) return JS_EXCEPTION;
    
    xhr->ctx = ctx;
    xhr->ready_state = 0;
    xhr->headers = JS_NewObject(ctx);
    xhr->onload = JS_NULL;
    xhr->onerror = JS_NULL;
    xhr->onreadystatechange = JS_NULL;
    
    JSValue obj = JS_NewObjectClass(ctx, js_xhr_class_id);
    JS_SetOpaque(obj, xhr);
    return obj;
}

static JSValue js_xhr_open(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    XMLHttpRequest *xhr = JS_GetOpaque2(ctx, this_val, js_xhr_class_id);
    if (!xhr) return JS_EXCEPTION;
    
    const char *method = JS_ToCString(ctx, argv[0]);
    const char *url = JS_ToCString(ctx, argv[1]);
    
    if (method && url) {
        strncpy(xhr->method, method, sizeof(xhr->method) - 1);
        strncpy(xhr->url, url, sizeof(xhr->url) - 1);
        xhr->ready_state = 1; // OPENED
        
        // Capture the URL - this is where we intercept requests
        record_captured_url(url);
    }
    
    JS_FreeCString(ctx, method);
    JS_FreeCString(ctx, url);
    return JS_UNDEFINED;
}

static JSValue js_xhr_send(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    XMLHttpRequest *xhr = JS_GetOpaque2(ctx, this_val, js_xhr_class_id);
    if (!xhr) return JS_EXCEPTION;
    
    xhr->ready_state = 4; // DONE
    xhr->status = 200;
    strcpy(xhr->response_text, "{}");
    
    // Fire onreadystatechange
    if (!JS_IsNull(xhr->onreadystatechange)) {
        JS_Call(ctx, xhr->onreadystatechange, this_val, 0, NULL);
    }
    
    // Fire onload
    if (!JS_IsNull(xhr->onload)) {
        JS_Call(ctx, xhr->onload, this_val, 0, NULL);
    }
    
    return JS_UNDEFINED;
}

static JSValue js_xhr_set_request_header(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

static JSValue js_xhr_get_response_header(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NULL;
}

static JSValue js_xhr_get_all_response_headers(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    XMLHttpRequest *xhr = JS_GetOpaque2(ctx, this_val, js_xhr_class_id);
    if (!xhr) return JS_EXCEPTION;
    return JS_NewString(ctx, xhr->response_headers);
}

static JSValue js_xhr_get_ready_state(JSContext *ctx, JSValueConst this_val) {
    XMLHttpRequest *xhr = JS_GetOpaque2(ctx, this_val, js_xhr_class_id);
    if (!xhr) return JS_EXCEPTION;
    return JS_NewInt32(ctx, xhr->ready_state);
}

static JSValue js_xhr_get_status(JSContext *ctx, JSValueConst this_val) {
    XMLHttpRequest *xhr = JS_GetOpaque2(ctx, this_val, js_xhr_class_id);
    if (!xhr) return JS_EXCEPTION;
    return JS_NewInt32(ctx, xhr->status);
}

static JSValue js_xhr_get_response_text(JSContext *ctx, JSValueConst this_val) {
    XMLHttpRequest *xhr = JS_GetOpaque2(ctx, this_val, js_xhr_class_id);
    if (!xhr) return JS_EXCEPTION;
    return JS_NewString(ctx, xhr->response_text);
}

static const JSCFunctionListEntry js_xhr_proto_funcs[] = {
    JS_CFUNC_DEF("open", 3, js_xhr_open),
    JS_CFUNC_DEF("send", 1, js_xhr_send),
    JS_CFUNC_DEF("setRequestHeader", 2, js_xhr_set_request_header),
    JS_CFUNC_DEF("getResponseHeader", 1, js_xhr_get_response_header),
    JS_CFUNC_DEF("getAllResponseHeaders", 0, js_xhr_get_all_response_headers),
    JS_CGETSET_DEF("readyState", js_xhr_get_ready_state, NULL),
    JS_CGETSET_DEF("status", js_xhr_get_status, NULL),
    JS_CGETSET_DEF("responseText", js_xhr_get_response_text, NULL),
    JS_PROP_STRING_DEF("responseType", "", JS_PROP_WRITABLE),
    JS_PROP_STRING_DEF("response", "", JS_PROP_WRITABLE),
};

// HTMLVideoElement implementation
typedef struct {
    char id[256];        // Element id for tracking
    char src[2048];
    int ready_state;
    int network_state;
    double current_time;
    double duration;
    int paused;
    int ended;
    int autoplay;
    JSValue onloadstart;
    JSValue onloadedmetadata;
    JSValue oncanplay;
    JSValue onplay;
    JSValue onplaying;
    JSValue onerror;
    JSContext *ctx;
} HTMLVideoElement;

static void js_video_finalizer(JSRuntime *rt, JSValue val) {
    HTMLVideoElement *vid = JS_GetOpaque(val, js_video_class_id);
    if (vid) {
        JS_FreeValueRT(rt, vid->onloadstart);
        JS_FreeValueRT(rt, vid->onloadedmetadata);
        JS_FreeValueRT(rt, vid->oncanplay);
        JS_FreeValueRT(rt, vid->onplay);
        JS_FreeValueRT(rt, vid->onplaying);
        JS_FreeValueRT(rt, vid->onerror);
        free(vid);
    }
}

static JSValue js_video_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    HTMLVideoElement *vid = calloc(1, sizeof(HTMLVideoElement));
    if (!vid) return JS_EXCEPTION;
    
    vid->ctx = ctx;
    vid->ready_state = 0; // HAVE_NOTHING
    vid->network_state = 0; // NETWORK_EMPTY
    vid->paused = 1;
    vid->duration = 0;
    vid->onloadstart = JS_NULL;
    vid->onloadedmetadata = JS_NULL;
    vid->oncanplay = JS_NULL;
    vid->onplay = JS_NULL;
    vid->onplaying = JS_NULL;
    vid->onerror = JS_NULL;
    
    JSValue obj = JS_NewObjectClass(ctx, js_video_class_id);
    JS_SetOpaque(obj, vid);
    return obj;
}

static JSValue js_video_load(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    HTMLVideoElement *vid = JS_GetOpaque2(ctx, this_val, js_video_class_id);
    if (!vid) return JS_EXCEPTION;
    
    vid->network_state = 2; // NETWORK_LOADING
    vid->ready_state = 1;   // HAVE_METADATA
    
    // Trigger events that YouTube player expects
    if (!JS_IsNull(vid->onloadstart)) {
        JS_Call(ctx, vid->onloadstart, this_val, 0, NULL);
    }
    if (!JS_IsNull(vid->onloadedmetadata)) {
        JS_Call(ctx, vid->onloadedmetadata, this_val, 0, NULL);
    }
    
    return JS_UNDEFINED;
}

static JSValue js_video_play(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    HTMLVideoElement *vid = JS_GetOpaque2(ctx, this_val, js_video_class_id);
    if (!vid) return JS_EXCEPTION;
    
    vid->paused = 0;
    vid->ready_state = 4; // HAVE_ENOUGH_DATA
    
    if (!JS_IsNull(vid->onplay)) {
        JS_Call(ctx, vid->onplay, this_val, 0, NULL);
    }
    if (!JS_IsNull(vid->onplaying)) {
        JS_Call(ctx, vid->onplaying, this_val, 0, NULL);
    }
    if (!JS_IsNull(vid->oncanplay)) {
        JS_Call(ctx, vid->oncanplay, this_val, 0, NULL);
    }
    
    return JS_UNDEFINED;
}

static JSValue js_video_pause(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    HTMLVideoElement *vid = JS_GetOpaque2(ctx, this_val, js_video_class_id);
    if (!vid) return JS_EXCEPTION;
    vid->paused = 1;
    return JS_UNDEFINED;
}

static JSValue js_video_set_src(JSContext *ctx, JSValueConst this_val, JSValueConst val) {
    HTMLVideoElement *vid = JS_GetOpaque2(ctx, this_val, js_video_class_id);
    if (!vid) return JS_EXCEPTION;
    
    const char *src = JS_ToCString(ctx, val);
    if (src) {
        LOG_INFO("HTMLVideoElement: video.src SET to: %.100s%s (element id=%s)",
                 src, strlen(src) > 100 ? "..." : "",
                 vid->id ? vid->id : "(none)");
        strncpy(vid->src, src, sizeof(vid->src) - 1);
        vid->src[sizeof(vid->src) - 1] = '\0';
        record_captured_url(src);
    } else {
        LOG_INFO("HTMLVideoElement: video.src SET to null/empty");
        vid->src[0] = '\0';
    }
    JS_FreeCString(ctx, src);
    return JS_UNDEFINED;
}

static JSValue js_video_get_src(JSContext *ctx, JSValueConst this_val) {
    HTMLVideoElement *vid = JS_GetOpaque2(ctx, this_val, js_video_class_id);
    if (!vid) return JS_EXCEPTION;
    return JS_NewString(ctx, vid->src);
}

static JSValue js_video_get_id(JSContext *ctx, JSValueConst this_val) {
    HTMLVideoElement *vid = JS_GetOpaque2(ctx, this_val, js_video_class_id);
    if (!vid) return JS_EXCEPTION;
    return JS_NewString(ctx, vid->id);
}

static JSValue js_video_set_id(JSContext *ctx, JSValueConst this_val, JSValueConst val) {
    HTMLVideoElement *vid = JS_GetOpaque2(ctx, this_val, js_video_class_id);
    if (!vid) return JS_EXCEPTION;
    const char *id = JS_ToCString(ctx, val);
    if (id) {
        strncpy(vid->id, id, sizeof(vid->id) - 1);
        vid->id[sizeof(vid->id) - 1] = '\0';
    }
    JS_FreeCString(ctx, id);
    return JS_UNDEFINED;
}

static JSValue js_video_get_current_time(JSContext *ctx, JSValueConst this_val) {
    HTMLVideoElement *vid = JS_GetOpaque2(ctx, this_val, js_video_class_id);
    if (!vid) return JS_EXCEPTION;
    return JS_NewFloat64(ctx, vid->current_time);
}

static JSValue js_video_set_current_time(JSContext *ctx, JSValueConst this_val, JSValueConst val) {
    HTMLVideoElement *vid = JS_GetOpaque2(ctx, this_val, js_video_class_id);
    if (!vid) return JS_EXCEPTION;
    JS_ToFloat64(ctx, &vid->current_time, val);
    return JS_UNDEFINED;
}

static JSValue js_video_get_duration(JSContext *ctx, JSValueConst this_val) {
    HTMLVideoElement *vid = JS_GetOpaque2(ctx, this_val, js_video_class_id);
    if (!vid) return JS_EXCEPTION;
    return JS_NewFloat64(ctx, vid->duration);
}

static JSValue js_video_get_paused(JSContext *ctx, JSValueConst this_val) {
    HTMLVideoElement *vid = JS_GetOpaque2(ctx, this_val, js_video_class_id);
    if (!vid) return JS_EXCEPTION;
    return JS_NewBool(ctx, vid->paused);
}

static JSValue js_video_get_ready_state(JSContext *ctx, JSValueConst this_val) {
    HTMLVideoElement *vid = JS_GetOpaque2(ctx, this_val, js_video_class_id);
    if (!vid) return JS_EXCEPTION;
    return JS_NewInt32(ctx, vid->ready_state);
}

static JSValue js_video_get_network_state(JSContext *ctx, JSValueConst this_val) {
    HTMLVideoElement *vid = JS_GetOpaque2(ctx, this_val, js_video_class_id);
    if (!vid) return JS_EXCEPTION;
    return JS_NewInt32(ctx, vid->network_state);
}

static const JSCFunctionListEntry js_video_proto_funcs[] = {
    JS_CFUNC_DEF("load", 0, js_video_load),
    JS_CFUNC_DEF("play", 0, js_video_play),
    JS_CFUNC_DEF("pause", 0, js_video_pause),
    JS_CGETSET_DEF("id", js_video_get_id, js_video_set_id),
    JS_CGETSET_DEF("src", js_video_get_src, js_video_set_src),
    JS_CGETSET_DEF("currentSrc", js_video_get_src, NULL),
    JS_CGETSET_DEF("currentTime", js_video_get_current_time, js_video_set_current_time),
    JS_CGETSET_DEF("duration", js_video_get_duration, NULL),
    JS_CGETSET_DEF("paused", js_video_get_paused, NULL),
    JS_CGETSET_DEF("readyState", js_video_get_ready_state, NULL),
    JS_CGETSET_DEF("networkState", js_video_get_network_state, NULL),
    JS_CGETSET_DEF("buffered", NULL, NULL),
    JS_CGETSET_DEF("played", NULL, NULL),
    JS_CGETSET_DEF("seekable", NULL, NULL),
    JS_CGETSET_DEF("ended", NULL, NULL),
    JS_CGETSET_DEF("autoplay", NULL, NULL),
    JS_CGETSET_DEF("loop", NULL, NULL),
    JS_CGETSET_DEF("muted", NULL, NULL),
    JS_CGETSET_DEF("volume", NULL, NULL),
    JS_CGETSET_DEF("playbackRate", NULL, NULL),
    JS_CGETSET_DEF("defaultPlaybackRate", NULL, NULL),
    JS_CGETSET_DEF("preload", NULL, NULL),
    JS_CGETSET_DEF("crossOrigin", NULL, NULL),
};

// Global fetch implementation
static JSValue js_fetch(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *url = JS_ToCString(ctx, argv[0]);
    if (url) {
        record_captured_url(url);
    }
    JS_FreeCString(ctx, url);
    
    // Return a mock promise - use global Promise constructor
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue promise_ctor = JS_GetPropertyStr(ctx, global, "Promise");
    JSValue promise = JS_CallConstructor(ctx, promise_ctor, 0, NULL);
    JS_FreeValue(ctx, promise_ctor);
    JS_FreeValue(ctx, global);
    
    return promise;
}

// Document and Element stubs with createElement support
static JSValue js_document_create_element(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *tag = JS_ToCString(ctx, argv[0]);
    JSValue elem = JS_NULL;
    
    if (tag) {
        // Create proper video element
        if (strcasecmp(tag, "video") == 0) {
            elem = js_video_constructor(ctx, JS_NULL, 0, NULL);
        } else {
            // Generic element
            elem = JS_NewObject(ctx);
        }
    }
    
    JS_FreeCString(ctx, tag);
    return elem;
}

static JSValue js_document_get_element_by_id(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    // Return null - YouTube will create its own elements
    return JS_NULL;
}

static JSValue js_document_query_selector(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NULL;
}

static JSValue js_document_query_selector_all(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NewArray(ctx);
}

static JSValue js_document_get_head(JSContext *ctx, JSValueConst this_val) {
    JSValue head = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, head, "appendChild", JS_NewCFunction(ctx, js_dummy_function, "appendChild", 1));
    return head;
}

static JSValue js_document_get_body(JSContext *ctx, JSValueConst this_val) {
    JSValue body = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, body, "appendChild", JS_NewCFunction(ctx, js_dummy_function, "appendChild", 1));
    return body;
}

static JSValue js_document_get_document_element(JSContext *ctx, JSValueConst this_val) {
    JSValue html = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, html, "getAttribute", JS_NewCFunction(ctx, js_dummy_function, "getAttribute", 1));
    return html;
}

static JSValue js_element_set_attribute(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

static JSValue js_element_get_attribute(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NULL;
}

static JSValue js_element_add_event_listener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    // Store event handlers on the element for dispatch
    const char *event = JS_ToCString(ctx, argv[0]);
    if (event) {
        char prop[128];
        snprintf(prop, sizeof(prop), "__on%s", event);
        JS_SetPropertyStr(ctx, this_val, prop, JS_DupValue(ctx, argv[1]));
    }
    JS_FreeCString(ctx, event);
    return JS_UNDEFINED;
}

static JSValue js_element_remove_event_listener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

static JSValue js_dummy_function(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

// Native logging function for JavaScript debugging
static JSValue js_bgmdwnldr_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc > 0) {
        const char *msg = JS_ToCString(ctx, argv[0]);
        if (msg) {
            __android_log_print(ANDROID_LOG_INFO, "js_debug", "%s", msg);
            JS_FreeCString(ctx, msg);
        }
    }
    return JS_UNDEFINED;
}

static JSValue js_console_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    for (int i = 0; i < argc; i++) {
        const char *str = JS_ToCString(ctx, argv[i]);
        if (str) {
            LOG_INFO("JS: %s", str);
            JS_FreeCString(ctx, str);
        }
    }
    return JS_UNDEFINED;
}

// Initialize browser environment
static void init_browser_environment(JSContext *ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    
    // Register native logging function FIRST so it's available for all debugging
    JS_SetPropertyStr(ctx, global, "__bgmdwnldr_log", JS_NewCFunction(ctx, js_bgmdwnldr_log, "__bgmdwnldr_log", 1));
    
    // === NATIVE SETUP (moved here to be available before BROWSER_STUBS_JS) ===
    // Create XMLHttpRequest class
    JSValue xhr_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, xhr_proto, js_xhr_proto_funcs, countof(js_xhr_proto_funcs));
    JS_SetClassProto(ctx, js_xhr_class_id, xhr_proto);
    
    JSValue xhr_ctor = JS_NewCFunction2(ctx, js_xhr_constructor, "XMLHttpRequest", 1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, xhr_ctor, xhr_proto);
    JS_SetPropertyStr(ctx, global, "XMLHttpRequest", xhr_ctor);
    
    // UNSENT, OPENED, HEADERS_RECEIVED, LOADING, DONE
    JS_SetPropertyStr(ctx, xhr_ctor, "UNSENT", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, xhr_ctor, "OPENED", JS_NewInt32(ctx, 1));
    JS_SetPropertyStr(ctx, xhr_ctor, "HEADERS_RECEIVED", JS_NewInt32(ctx, 2));
    JS_SetPropertyStr(ctx, xhr_ctor, "LOADING", JS_NewInt32(ctx, 3));
    JS_SetPropertyStr(ctx, xhr_ctor, "DONE", JS_NewInt32(ctx, 4));
    
    // Create HTMLVideoElement class
    JSValue video_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, video_proto, js_video_proto_funcs, countof(js_video_proto_funcs));
    JS_SetClassProto(ctx, js_video_class_id, video_proto);
    
    JSValue video_ctor = JS_NewCFunction2(ctx, js_video_constructor, "HTMLVideoElement", 1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, video_ctor, video_proto);
    JS_SetPropertyStr(ctx, global, "HTMLVideoElement", video_ctor);
    
    // HAVE_NOTHING, HAVE_METADATA, HAVE_CURRENT_DATA, HAVE_FUTURE_DATA, HAVE_ENOUGH_DATA
    JS_SetPropertyStr(ctx, video_ctor, "HAVE_NOTHING", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, video_ctor, "HAVE_METADATA", JS_NewInt32(ctx, 1));
    JS_SetPropertyStr(ctx, video_ctor, "HAVE_CURRENT_DATA", JS_NewInt32(ctx, 2));
    JS_SetPropertyStr(ctx, video_ctor, "HAVE_FUTURE_DATA", JS_NewInt32(ctx, 3));
    JS_SetPropertyStr(ctx, video_ctor, "HAVE_ENOUGH_DATA", JS_NewInt32(ctx, 4));
    
    // NETWORK_EMPTY, NETWORK_IDLE, NETWORK_LOADING, NETWORK_NO_SOURCE
    JS_SetPropertyStr(ctx, video_ctor, "NETWORK_EMPTY", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, video_ctor, "NETWORK_IDLE", JS_NewInt32(ctx, 1));
    JS_SetPropertyStr(ctx, video_ctor, "NETWORK_LOADING", JS_NewInt32(ctx, 2));
    JS_SetPropertyStr(ctx, video_ctor, "NETWORK_NO_SOURCE", JS_NewInt32(ctx, 3));
    
    // fetch
    JS_SetPropertyStr(ctx, global, "fetch", JS_NewCFunction(ctx, js_fetch, "fetch", 2));
    
    // document
    JSValue document = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, document, "createElement", JS_NewCFunction(ctx, js_document_create_element, "createElement", 1));
    JS_SetPropertyStr(ctx, document, "getElementById", JS_NewCFunction(ctx, js_document_get_element_by_id, "getElementById", 1));
    JS_SetPropertyStr(ctx, document, "querySelector", JS_NewCFunction(ctx, js_document_query_selector, "querySelector", 1));
    JS_SetPropertyStr(ctx, document, "querySelectorAll", JS_NewCFunction(ctx, js_document_query_selector_all, "querySelectorAll", 1));
    JS_SetPropertyStr(ctx, document, "addEventListener", JS_NewCFunction(ctx, js_dummy_function, "addEventListener", 2));
    JS_SetPropertyStr(ctx, document, "removeEventListener", JS_NewCFunction(ctx, js_dummy_function, "removeEventListener", 2));
    JS_SetPropertyStr(ctx, document, "head", js_document_get_head(ctx, document));
    JS_SetPropertyStr(ctx, document, "body", js_document_get_body(ctx, document));
    JS_SetPropertyStr(ctx, document, "documentElement", js_document_get_document_element(ctx, document));
    JS_SetPropertyStr(ctx, document, "createTextNode", JS_NewCFunction(ctx, js_dummy_function, "createTextNode", 1));
    JS_SetPropertyStr(ctx, document, "createComment", JS_NewCFunction(ctx, js_dummy_function, "createComment", 1));
    JS_SetPropertyStr(ctx, document, "createElementNS", JS_NewCFunction(ctx, js_document_create_element, "createElementNS", 2));
    JS_SetPropertyStr(ctx, document, "createDocumentFragment", JS_NewCFunction(ctx, js_dummy_function, "createDocumentFragment", 0));
    JS_SetPropertyStr(ctx, document, "requestStorageAccessFor", JS_NewCFunction(ctx, js_dummy_function, "requestStorageAccessFor", 1));
    JS_SetPropertyStr(ctx, document, "write", JS_NewCFunction(ctx, js_dummy_function, "write", 1));
    JS_SetPropertyStr(ctx, document, "writeln", JS_NewCFunction(ctx, js_dummy_function, "writeln", 1));
    JS_SetPropertyStr(ctx, document, "location", JS_NewObject(ctx));
    JS_SetPropertyStr(ctx, document, "referrer", JS_NewString(ctx, "https://www.youtube.com/"));
    JS_SetPropertyStr(ctx, document, "cookie", JS_NewString(ctx, ""));
    JS_SetPropertyStr(ctx, document, "domain", JS_NewString(ctx, "www.youtube.com"));
    JS_SetPropertyStr(ctx, document, "readyState", JS_NewString(ctx, "complete"));
    JS_SetPropertyStr(ctx, document, "characterSet", JS_NewString(ctx, "UTF-8"));
    JS_SetPropertyStr(ctx, document, "charset", JS_NewString(ctx, "UTF-8"));
    JS_SetPropertyStr(ctx, document, "contentType", JS_NewString(ctx, "text/html"));
    // createTreeWalker and createNodeIterator - set up after BROWSER_STUBS_JS defines TreeWalker
    // These will be overridden by the JS stubs below, but provide safe fallbacks
    JS_SetPropertyStr(ctx, document, "createTreeWalker", JS_NewCFunction(ctx, js_dummy_function, "createTreeWalker", 3));
    JS_SetPropertyStr(ctx, document, "createNodeIterator", JS_NewCFunction(ctx, js_dummy_function, "createNodeIterator", 3));
    // document.defaultView should point to window
    // This will be set after window is created
    JS_SetPropertyStr(ctx, global, "document", document);
    
    // window - use existing window from browser stubs if available, don't overwrite
    JSValue window = JS_GetPropertyStr(ctx, global, "window");
    if (JS_IsUndefined(window)) {
        window = JS_NewObject(ctx);
    }
    JS_SetPropertyStr(ctx, window, "addEventListener", JS_NewCFunction(ctx, js_dummy_function, "addEventListener", 2));
    JS_SetPropertyStr(ctx, window, "removeEventListener", JS_NewCFunction(ctx, js_dummy_function, "removeEventListener", 2));
    JS_SetPropertyStr(ctx, window, "setTimeout", JS_NewCFunction(ctx, js_dummy_function, "setTimeout", 2));
    JS_SetPropertyStr(ctx, window, "setInterval", JS_NewCFunction(ctx, js_dummy_function, "setInterval", 2));
    JS_SetPropertyStr(ctx, window, "clearTimeout", JS_NewCFunction(ctx, js_dummy_function, "clearTimeout", 1));
    JS_SetPropertyStr(ctx, window, "clearInterval", JS_NewCFunction(ctx, js_dummy_function, "clearInterval", 1));
    JS_SetPropertyStr(ctx, window, "requestAnimationFrame", JS_NewCFunction(ctx, js_dummy_function, "requestAnimationFrame", 1));
    JS_SetPropertyStr(ctx, window, "cancelAnimationFrame", JS_NewCFunction(ctx, js_dummy_function, "cancelAnimationFrame", 1));
    // Also set on global for scripts that access it as a global function
    JS_SetPropertyStr(ctx, global, "requestAnimationFrame", JS_NewCFunction(ctx, js_dummy_function, "requestAnimationFrame", 1));
    JS_SetPropertyStr(ctx, global, "cancelAnimationFrame", JS_NewCFunction(ctx, js_dummy_function, "cancelAnimationFrame", 1));
    JS_SetPropertyStr(ctx, window, "postMessage", JS_NewCFunction(ctx, js_dummy_function, "postMessage", 2));
    JS_SetPropertyStr(ctx, window, "alert", JS_NewCFunction(ctx, js_dummy_function, "alert", 1));
    JS_SetPropertyStr(ctx, window, "confirm", JS_NewCFunction(ctx, js_dummy_function, "confirm", 1));
    JS_SetPropertyStr(ctx, window, "prompt", JS_NewCFunction(ctx, js_dummy_function, "prompt", 2));
    JS_SetPropertyStr(ctx, window, "open", JS_NewCFunction(ctx, js_dummy_function, "open", 3));
    JS_SetPropertyStr(ctx, window, "close", JS_NewCFunction(ctx, js_dummy_function, "close", 0));
    JS_SetPropertyStr(ctx, window, "focus", JS_NewCFunction(ctx, js_dummy_function, "focus", 0));
    JS_SetPropertyStr(ctx, window, "blur", JS_NewCFunction(ctx, js_dummy_function, "blur", 0));
    JS_SetPropertyStr(ctx, window, "scrollTo", JS_NewCFunction(ctx, js_dummy_function, "scrollTo", 2));
    JS_SetPropertyStr(ctx, window, "scrollBy", JS_NewCFunction(ctx, js_dummy_function, "scrollBy", 2));
    // localStorage and sessionStorage - create as JS objects and set on window
    const char *storage_class_js = 
        "function Storage() { this._data = {}; }"
        "Storage.prototype.getItem = function(k) { return this._data.hasOwnProperty(k) ? this._data[k] : null; };"
        "Storage.prototype.setItem = function(k, v) { this._data[k] = String(v); };"
        "Storage.prototype.removeItem = function(k) { delete this._data[k]; };"
        "Storage.prototype.clear = function() { this._data = {}; };"
        "Storage.prototype.key = function(n) { var keys = Object.keys(this._data); return n >= 0 && n < keys.length ? keys[n] : null; };"
        "Object.defineProperty(Storage.prototype, 'length', { get: function() { return Object.keys(this._data).length; } });"
    ;
    JS_Eval(ctx, storage_class_js, strlen(storage_class_js), "<storage_class>", 0);
    
    // Create localStorage and sessionStorage instances and set them on window
    const char *local_storage_create = "new Storage();";
    const char *session_storage_create = "new Storage();";
    JSValue local_storage_val = JS_Eval(ctx, local_storage_create, strlen(local_storage_create), "<localStorage>", 0);
    JSValue session_storage_val = JS_Eval(ctx, session_storage_create, strlen(session_storage_create), "<sessionStorage>", 0);
    JS_SetPropertyStr(ctx, window, "localStorage", local_storage_val);
    JS_SetPropertyStr(ctx, window, "sessionStorage", session_storage_val);
    
    JS_SetPropertyStr(ctx, window, "indexedDB", JS_NewObject(ctx));
    JS_SetPropertyStr(ctx, window, "location", JS_NewObject(ctx));
    JS_SetPropertyStr(ctx, window, "navigator", JS_NewObject(ctx));
    JS_SetPropertyStr(ctx, window, "screen", JS_NewObject(ctx));
    JS_SetPropertyStr(ctx, window, "history", JS_NewObject(ctx));
    JS_SetPropertyStr(ctx, window, "innerWidth", JS_NewInt32(ctx, 1920));
    JS_SetPropertyStr(ctx, window, "innerHeight", JS_NewInt32(ctx, 1080));
    JS_SetPropertyStr(ctx, window, "outerWidth", JS_NewInt32(ctx, 1920));
    JS_SetPropertyStr(ctx, window, "outerHeight", JS_NewInt32(ctx, 1080));
    JS_SetPropertyStr(ctx, window, "screenX", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, window, "screenY", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, window, "devicePixelRatio", JS_NewFloat64(ctx, 1.0));
    JS_SetPropertyStr(ctx, window, "XMLHttpRequest", JS_DupValue(ctx, xhr_ctor));
    JS_SetPropertyStr(ctx, window, "HTMLVideoElement", JS_DupValue(ctx, video_ctor));
    JS_SetPropertyStr(ctx, window, "document", JS_DupValue(ctx, document));
    // Don't overwrite window - it already has DOM constructors from browser stubs
    // JS_SetPropertyStr(ctx, global, "window", window);
    
    // document.defaultView points to window
    JS_SetPropertyStr(ctx, document, "defaultView", JS_DupValue(ctx, window));
    
    // Location object
    JSValue location = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, location, "href", JS_NewString(ctx, "https://www.youtube.com/watch?v=dQw4w9WgXcQ"));
    JS_SetPropertyStr(ctx, location, "protocol", JS_NewString(ctx, "https:"));
    JS_SetPropertyStr(ctx, location, "host", JS_NewString(ctx, "www.youtube.com"));
    JS_SetPropertyStr(ctx, location, "hostname", JS_NewString(ctx, "www.youtube.com"));
    JS_SetPropertyStr(ctx, location, "port", JS_NewString(ctx, ""));
    JS_SetPropertyStr(ctx, location, "pathname", JS_NewString(ctx, "/watch"));
    JS_SetPropertyStr(ctx, location, "search", JS_NewString(ctx, "?v=dQw4w9WgXcQ"));
    JS_SetPropertyStr(ctx, location, "hash", JS_NewString(ctx, ""));
    // Add toString method
    JS_SetPropertyStr(ctx, location, "toString", JS_NewCFunction(ctx, js_dummy_function, "toString", 0));
    JS_SetPropertyStr(ctx, window, "location", location);
    JS_SetPropertyStr(ctx, document, "location", JS_DupValue(ctx, location));
    
    // URL class
    const char *url_js = 
        "function URL(url, base) {"
        "  var a = document.createElement('a');"
        "  a.href = url;"
        "  this.href = a.href || url;"
        "  this.protocol = a.protocol || 'https:';"
        "  this.host = a.host || '';"
        "  this.hostname = a.hostname || '';"
        "  this.port = a.port || '';"
        "  this.pathname = a.pathname || '';"
        "  this.search = a.search || '';"
        "  this.hash = a.hash || '';"
        "  this.username = '';"
        "  this.password = '';"
        "  this.origin = this.protocol + '//' + this.host;"
        "}"
        "URL.prototype.toString = function() { return this.href; };"
        "window.URL = URL;"
        ""
        // Ensure window.location.href works properly
        "window.location.href = window.location.href || 'https://www.youtube.com/watch?v=dQw4w9WgXcQ';"
        "window.location.toString = function() { return this.href; };"
        ""
        // Window getters
        "if (!window.top) window.top = window;"
        "if (!window.parent) window.parent = window;"
        "if (!window.self) window.self = window;"
        "if (!window.opener) window.opener = null;"
        ""
        // Origin
        "window.origin = window.location.origin || 'https://www.youtube.com';"
    ;
    JS_Eval(ctx, url_js, strlen(url_js), "<url>", 0);
    
    // Navigator object with Chrome fingerprint
    JSValue navigator = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, navigator, "userAgent", JS_NewString(ctx, "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"));
    JS_SetPropertyStr(ctx, navigator, "appName", JS_NewString(ctx, "Netscape"));
    JS_SetPropertyStr(ctx, navigator, "appVersion", JS_NewString(ctx, "5.0 (X11; Linux x86_64) AppleWebKit/537.36"));
    JS_SetPropertyStr(ctx, navigator, "appCodeName", JS_NewString(ctx, "Mozilla"));
    JS_SetPropertyStr(ctx, navigator, "platform", JS_NewString(ctx, "Linux x86_64"));
    JS_SetPropertyStr(ctx, navigator, "product", JS_NewString(ctx, "Gecko"));
    JS_SetPropertyStr(ctx, navigator, "productSub", JS_NewString(ctx, "20030107"));
    JS_SetPropertyStr(ctx, navigator, "vendor", JS_NewString(ctx, "Google Inc."));
    JS_SetPropertyStr(ctx, navigator, "vendorSub", JS_NewString(ctx, ""));
    JS_SetPropertyStr(ctx, navigator, "language", JS_NewString(ctx, "en-US"));
    JS_SetPropertyStr(ctx, navigator, "languages", JS_NewObject(ctx));
    JS_SetPropertyStr(ctx, navigator, "onLine", JS_NewBool(ctx, 1));
    JS_SetPropertyStr(ctx, navigator, "cookieEnabled", JS_NewBool(ctx, 1));
    JS_SetPropertyStr(ctx, navigator, "hardwareConcurrency", JS_NewInt32(ctx, 8));
    JS_SetPropertyStr(ctx, navigator, "maxTouchPoints", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, navigator, "pdfViewerEnabled", JS_NewBool(ctx, 1));
    JS_SetPropertyStr(ctx, navigator, "webdriver", JS_NewBool(ctx, 0));
    JS_SetPropertyStr(ctx, navigator, "bluetooth", JS_NewObject(ctx));
    JS_SetPropertyStr(ctx, navigator, "clipboard", JS_NewObject(ctx));
    JS_SetPropertyStr(ctx, navigator, "credentials", JS_NewObject(ctx));
    JS_SetPropertyStr(ctx, navigator, "keyboard", JS_NewObject(ctx));
    JS_SetPropertyStr(ctx, navigator, "mediaCapabilities", JS_NewObject(ctx));
    JS_SetPropertyStr(ctx, navigator, "mediaDevices", JS_NewObject(ctx));
    JS_SetPropertyStr(ctx, navigator, "permissions", JS_NewObject(ctx));
    JS_SetPropertyStr(ctx, navigator, "presentation", JS_NewObject(ctx));
    JS_SetPropertyStr(ctx, navigator, "scheduling", JS_NewObject(ctx));
    JS_SetPropertyStr(ctx, navigator, "storage", JS_NewObject(ctx));
    JS_SetPropertyStr(ctx, navigator, "wakeLock", JS_NewObject(ctx));
    JS_SetPropertyStr(ctx, navigator, "webkitTemporaryStorage", JS_NewObject(ctx));
    JS_SetPropertyStr(ctx, window, "navigator", navigator);
    
    // Screen object
    JSValue screen = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, screen, "width", JS_NewInt32(ctx, 1920));
    JS_SetPropertyStr(ctx, screen, "height", JS_NewInt32(ctx, 1080));
    JS_SetPropertyStr(ctx, screen, "availWidth", JS_NewInt32(ctx, 1920));
    JS_SetPropertyStr(ctx, screen, "availHeight", JS_NewInt32(ctx, 1040));
    JS_SetPropertyStr(ctx, screen, "colorDepth", JS_NewInt32(ctx, 24));
    JS_SetPropertyStr(ctx, screen, "pixelDepth", JS_NewInt32(ctx, 24));
    JS_SetPropertyStr(ctx, window, "screen", screen);
    
    // History object
    JSValue history = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, history, "length", JS_NewInt32(ctx, 2));
    JS_SetPropertyStr(ctx, window, "history", history);
    
    // console
    JSValue console = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, console, "log", JS_NewCFunction(ctx, js_console_log, "log", 1));
    JS_SetPropertyStr(ctx, console, "error", JS_NewCFunction(ctx, js_console_log, "error", 1));
    JS_SetPropertyStr(ctx, console, "warn", JS_NewCFunction(ctx, js_console_log, "warn", 1));
    JS_SetPropertyStr(ctx, console, "info", JS_NewCFunction(ctx, js_console_log, "info", 1));
    JS_SetPropertyStr(ctx, console, "debug", JS_NewCFunction(ctx, js_console_log, "debug", 1));
    JS_SetPropertyStr(ctx, console, "trace", JS_NewCFunction(ctx, js_console_log, "trace", 1));
    JS_SetPropertyStr(ctx, global, "console", console);
    
    // === THEN LOAD BROWSER_STUBS_JS ===
    LOG_INFO("Setting up basic browser environment...");
    JSValue stubs_result = JS_Eval(ctx, BROWSER_STUBS_JS, strlen(BROWSER_STUBS_JS), "<browser_stubs>", 0);
    if (JS_IsException(stubs_result)) {
        JSValue exc = JS_GetException(ctx);
        const char *exc_str = JS_ToCString(ctx, exc);
        LOG_ERROR("Failed to load browser stubs: %s", exc_str ? exc_str : "(unknown)");
        JS_FreeCString(ctx, exc_str);
        JS_FreeValue(ctx, exc);
    } else {
        LOG_INFO("JS: Basic browser environment ready");
    }
    JS_FreeValue(ctx, stubs_result);
    
    // Set up additional shims
    const char *early_shim = 
        "if (typeof window === 'undefined') { this.window = this; }"
        "if (typeof globalThis === 'undefined') { this.globalThis = this; }"
        // Define generic polyfill flags on window and global
        "this.es5Shimmed = true;"
        "this.es6Shimmed = true;"
        "this._babelPolyfill = true;"
        // Ensure common constructor names exist to prevent 'prototype of undefined' errors
        "if (typeof Iterator === 'undefined') { this.Iterator = function() {}; }"
        "if (typeof Generator === 'undefined') { this.Generator = function() {}; }"
        "if (typeof Map === 'undefined') { this.Map = function() {}; }"
        "if (typeof Set === 'undefined') { this.Set = function() {}; }"
        "if (typeof WeakMap === 'undefined') { this.WeakMap = function() {}; }"
        "if (typeof WeakSet === 'undefined') { this.WeakSet = function() {}; }"
        "// Ensure constructors are also on window"
        "window.Iterator = this.Iterator;"
        "window.Generator = this.Generator;"
        "window.Map = this.Map;"
        "window.Set = this.Set;"
        "window.WeakMap = this.WeakMap;"
        "window.WeakSet = this.WeakSet;"
    ;
    JS_Eval(ctx, early_shim, strlen(early_shim), "<early_shim>", 0);
    
    // Ensure all window properties are proper objects (not undefined) for 'in' operator
    const char *window_check_js = 
        "// Ensure localStorage and sessionStorage exist on window\n"
        "if (!window.localStorage) {\n"
        "  window.localStorage = localStorage;\n"
        "}\n"
        "if (!window.sessionStorage) {\n"
        "  window.sessionStorage = sessionStorage;\n"
        "}\n"
        "// Ensure navigator, location, document are objects\n"
        "if (typeof window.navigator !== 'object') {\n"
        "  window.navigator = {};\n"
        "}\n"
        "if (typeof window.location !== 'object') {\n"
        "  window.location = {};\n"
        "}\n"
        "if (typeof window.document !== 'object') {\n"
        "  window.document = {};\n"
        "}\n"
    ;
    JS_Eval(ctx, window_check_js, strlen(window_check_js), "<window_check>", 0);
    
    // Create ALL common constructors upfront to prevent 'prototype of undefined' errors
    const char *constructors_js = 
        "function __log(msg) {"
        "  if (typeof __bgmdwnldr_log === 'function') {"
        "    __bgmdwnldr_log(msg);"
        "  }"
        "}"
        "var __ctors = ['Iterator','Generator','AsyncGenerator','Map','Set','WeakMap','WeakSet',"
        "'ArrayBuffer','SharedArrayBuffer','Uint8Array','Int8Array','Uint8ClampedArray',"
        "'Uint16Array','Int16Array','Uint32Array','Int32Array','Float32Array','Float64Array',"
        "'BigInt64Array','BigUint64Array','DataView','Promise','RegExp','Date','Error',"
        "'TypeError','ReferenceError','SyntaxError','RangeError','URIError','EvalError',"
        "'AggregateError','Symbol','Proxy','Reflect','WeakRef','FinalizationRegistry','Node','Element','Document','Window'];"
        "for (var i=0; i<__ctors.length; i++) {"
        "  var name=__ctors[i];"
        "  if (typeof window[name]==='undefined') {"
        "    __log('Creating stub: '+name);"
        "    window[name]=function(){};"
        "    window[name].prototype={};"
        "  }"
        "}"
        "__log('Constructor setup done');"
    ;
    JS_Eval(ctx, constructors_js, strlen(constructors_js), "<constructors>", 0);
    
    // Wrap window with Proxy to detect undefined property accesses
    const char *proxy_js = 
        "if (typeof Proxy !== 'undefined') {"
        "  var __loggedChains = {};"
        "  function __createTracker(chain) {"
        "    return new Proxy(function(){}, {"
        "      get: function(t, p) {"
        "        if (p === 'prototype' || p === '__proto__') return {};"
        "        var newChain = chain + '.' + p;"
        "        if (!__loggedChains[newChain]) {"
        "          __loggedChains[newChain] = true;"
        "          __log('UNDEFINED: ' + newChain);"
        "        }"
        "        return __createTracker(newChain);"
        "      },"
        "      apply: function(t, that, args) { return undefined; },"
        "      construct: function(t, args) { return {}; }"
        "    });"
        "  }"
        "  var __origWindow = window;"
        "  window = new Proxy(__origWindow, {"
        "    get: function(target, prop) {"
        "      if (prop === Symbol.unscopables) return undefined;"
        "      var val = target[prop];"
        "      if (val === undefined && typeof prop === 'string') {"
        "        var chain = 'window.' + prop;"
        "        if (!__loggedChains[chain]) {"
        "          __loggedChains[chain] = true;"
        "          __log('UNDEFINED: ' + chain);"
        "        }"
        "        return __createTracker(chain);"
        "      }"
        "      return val;"
        "    }"
        "  });"
        "  __log('Proxy installed');"
        "}"
    ;
    JS_Eval(ctx, proxy_js, strlen(proxy_js), "<proxy>", 0);
    
    // Wrap Object.defineProperty to catch prototype assignments on undefined
    const char *proto_trap_js = 
        "var __origODP = Object.defineProperty;"
        "Object.defineProperty = function(obj, prop, desc) {"
        "  if (obj === undefined || obj === null) {"
        "    __log('DEFINE_PROP_ERROR: obj=' + obj + ', prop=' + prop);"
        "    return obj;"
        "  }"
        "  return __origODP.apply(this, arguments);"
        "};"
    ;
    JS_Eval(ctx, proto_trap_js, strlen(proto_trap_js), "<proto_trap>", 0);
    
    // Event and CustomEvent constructors
    const char *event_js = 
        "function Event(type, eventInitDict) {"
        "  eventInitDict = eventInitDict || {};"
        "  this.type = type;"
        "  this.bubbles = eventInitDict.bubbles || false;"
        "  this.cancelable = eventInitDict.cancelable || false;"
        "  this.composed = eventInitDict.composed || false;"
        "  this.defaultPrevented = false;"
        "  this.isTrusted = true;"
        "  this.timeStamp = Date.now();"
        "  this.target = null;"
        "  this.currentTarget = null;"
        "  this.eventPhase = 0;"
        "}"
        "Event.prototype.preventDefault = function() { this.defaultPrevented = true; };"
        "Event.prototype.stopPropagation = function() {};"
        "Event.prototype.stopImmediatePropagation = function() {};"
        "Event.prototype.initEvent = function(type, bubbles, cancelable) {"
        "  this.type = type; this.bubbles = bubbles; this.cancelable = cancelable;"
        "};"
        "Event.NONE = 0; Event.CAPTURING_PHASE = 1; Event.AT_TARGET = 2; Event.BUBBLING_PHASE = 3;"
        "window.Event = Event;"
        ""
        "function CustomEvent(type, eventInitDict) {"
        "  Event.call(this, type, eventInitDict);"
        "  this.detail = eventInitDict && eventInitDict.detail;"
        "}"
        "CustomEvent.prototype = Object.create(Event.prototype);"
        "CustomEvent.prototype.constructor = CustomEvent;"
        "window.CustomEvent = CustomEvent;"
        ""
        // KeyboardEvent, MouseEvent stubs
        "function KeyboardEvent(type, init) { Event.call(this, type, init); this.key = (init && init.key) || ''; this.code = (init && init.code) || ''; }"
        "KeyboardEvent.prototype = Object.create(Event.prototype);"
        "window.KeyboardEvent = KeyboardEvent;"
        "function MouseEvent(type, init) { Event.call(this, type, init); this.clientX = 0; this.clientY = 0; }"
        "MouseEvent.prototype = Object.create(Event.prototype);"
        "window.MouseEvent = MouseEvent;"
        // ErrorEvent
        "function ErrorEvent(type, init) { Event.call(this, type, init); this.message = (init && init.message) || ''; this.filename = (init && init.filename) || ''; this.lineno = (init && init.lineno) || 0; this.colno = (init && init.colno) || 0; this.error = (init && init.error) || null; }"
        "ErrorEvent.prototype = Object.create(Event.prototype);"
        "window.ErrorEvent = ErrorEvent;"
        // PromiseRejectionEvent
        "function PromiseRejectionEvent(type, init) { Event.call(this, type, init); this.promise = (init && init.promise) || null; this.reason = (init && init.reason) || null; }"
        "PromiseRejectionEvent.prototype = Object.create(Event.prototype);"
        "window.PromiseRejectionEvent = PromiseRejectionEvent;"
    ;
    JS_Eval(ctx, event_js, strlen(event_js), "<event>", 0);
    
    // EventTarget polyfill
    const char *event_target_js = 
        "(function() {"
        "  var listeners = {};"
        "  window.__eventListeners = listeners;"
        "  window.addEventListener = function(type, fn, options) {"
        "    if (!listeners[type]) listeners[type] = [];"
        "    listeners[type].push(fn);"
        "  };"
        "  window.removeEventListener = function(type, fn, options) {"
        "    if (!listeners[type]) return;"
        "    var idx = listeners[type].indexOf(fn);"
        "    if (idx >= 0) listeners[type].splice(idx, 1);"
        "  };"
        "  window.dispatchEvent = function(event) {"
        "    if (!listeners[event.type]) return;"
        "    listeners[event.type].forEach(function(fn) {"
        "      try { fn(event); } catch(e) {}"
        "    });"
        "  };"
        "  document.addEventListener = window.addEventListener;"
        "  document.removeEventListener = window.removeEventListener;"
        "  document.dispatchEvent = window.dispatchEvent;"
        "})();"
    ;
    JS_Eval(ctx, event_target_js, strlen(event_target_js), "<event_target>", 0);
    
    // URL capture array and global references
    const char *init_js = 
        "var __capturedUrls = [];"
        "function __recordUrl(url) {"
        "  if (url && url.indexOf && __capturedUrls.indexOf(url) < 0) {"
        "    __capturedUrls.push(url);"
        "    if (console && console.log) console.log('Captured URL:', url.substring(0, 100));"
        "  }"
        "}"
        // Make window properties available globally
        "var navigator = window.navigator || {};"
        "var localStorage = window.localStorage;"
        "var sessionStorage = window.sessionStorage;"
        "var location = window.location;"
        "var document = window.document;"
        // Ensure DOM constructors are global
        "var Element = window.Element || function() {};"
        "var Node = window.Node || function() {};"
        "var HTMLElement = window.HTMLElement || Element;"
        "var SVGElement = window.SVGElement || Element;"
        "var HTMLVideoElement = window.HTMLVideoElement || HTMLElement;"
        "var HTMLAnchorElement = window.HTMLAnchorElement || HTMLElement;"
        "var HTMLScriptElement = window.HTMLScriptElement || HTMLElement;"
        "var Document = window.Document || function() {};"
    ;
    JS_Eval(ctx, init_js, strlen(init_js), "<init>", 0);
    
    // Web Crypto API
    const char *crypto_js = 
        "var crypto = {"
        "  getRandomValues: function(arr) {"
        "    for (var i = 0; i < arr.length; i++) {"
        "      arr[i] = Math.floor(Math.random() * 256);"
        "    }"
        "    return arr;"
        "  },"
        "  subtle: {}"
        "};"
    ;
    JS_Eval(ctx, crypto_js, strlen(crypto_js), "<crypto>", 0);
    
    // Intl API (Internationalization) - needed for YouTube
    const char *intl_js = 
        "var Intl = {"
        "  NumberFormat: function(locales, options) {"
        "    this.format = function(num) { return num.toString(); };"
        "  },"
        "  DateTimeFormat: function(locales, options) {"
        "    this.format = function(date) { return date.toString(); };"
        "  },"
        "  Collator: function(locales, options) {"
        "    this.compare = function(a, b) { return a.localeCompare(b); };"
        "  },"
        "  ListFormat: function(locales, options) {"
        "    this.format = function(list) { return list.join(', '); };"
        "  },"
        "  PluralRules: function(locales, options) {"
        "    this.select = function(n) { return 'other'; };"
        "  },"
        "  RelativeTimeFormat: function(locales, options) {"
        "    this.format = function(value, unit) { return value + ' ' + unit; };"
        "  }"
        "};"
    ;
    JS_Eval(ctx, intl_js, strlen(intl_js), "<intl>", 0);
    
    // Element and Node classes - needed for DOM operations
    const char *dom_js = 
        "function Node() {"
        "  this.childNodes = [];"
        "  this.parentNode = null;"
        "  this.nodeType = 1;"
        "}"
        "Node.prototype.appendChild = function(child) {"
        "  this.childNodes.push(child);"
        "  child.parentNode = this;"
        "  return child;"
        "};"
        "Node.prototype.removeChild = function(child) {"
        "  var idx = this.childNodes.indexOf(child);"
        "  if (idx >= 0) this.childNodes.splice(idx, 1);"
        "  child.parentNode = null;"
        "  return child;"
        "};"
        "Node.prototype.insertBefore = function(newChild, refChild) {"
        "  return this.appendChild(newChild);"
        "};"
        "Node.prototype.cloneNode = function(deep) {"
        "  return new Node();"
        "};"
        "Node.prototype.getRootNode = function(options) {"
        "  var node = this;"
        "  while (node.parentNode) node = node.parentNode;"
        "  return node;"
        "};"
        "Node.ELEMENT_NODE = 1;"
        "Node.TEXT_NODE = 3;"
        "Node.COMMENT_NODE = 8;"
        "Node.DOCUMENT_NODE = 9;"
        ""
        // NodeFilter for TreeWalker
        "var NodeFilter = {"
        "  FILTER_ACCEPT: 1, FILTER_REJECT: 2, FILTER_SKIP: 3,"
        "  SHOW_ALL: -1, SHOW_ELEMENT: 1, SHOW_ATTRIBUTE: 2, SHOW_TEXT: 4,"
        "  SHOW_CDATA_SECTION: 8, SHOW_ENTITY_REFERENCE: 16, SHOW_ENTITY: 32,"
        "  SHOW_PROCESSING_INSTRUCTION: 64, SHOW_COMMENT: 128, SHOW_DOCUMENT: 256,"
        "  SHOW_DOCUMENT_TYPE: 512, SHOW_DOCUMENT_FRAGMENT: 1024, SHOW_NOTATION: 2048"
        "};"
        "window.NodeFilter = NodeFilter;"
        ""
        "function Element() {"
        "  Node.call(this);"
        "  this.tagName = '';"
        "  this.attributes = {};"
        "  this.style = {};"
        "  this.className = '';"
        "  this.id = '';"
        "}"
        "Element.prototype = Object.create(Node.prototype);"
        "Element.prototype.constructor = Element;"
        "Element.prototype.attachShadow = function(init) {"
        "  return { mode: init && init.mode, host: this };"
        "};"
        "Element.prototype.setAttribute = function(name, value) {"
        "  this.attributes[name] = value;"
        "};"
        "Element.prototype.getAttribute = function(name) {"
        "  return this.attributes[name] || null;"
        "};"
        "Element.prototype.removeAttribute = function(name) {"
        "  delete this.attributes[name];"
        "};"
        "Element.prototype.hasAttribute = function(name) {"
        "  return name in this.attributes;"
        "};"
        "Element.prototype.getElementsByTagName = function(tagName) {"
        "  return [];"
        "};"
        "Element.prototype.querySelector = function(selector) {"
        "  return null;"
        "};"
        "Element.prototype.querySelectorAll = function(selector) {"
        "  return [];"
        "};"
        ""
        // HTMLElement extends Element
        "function HTMLElement() {"
        "  Element.call(this);"
        "}"
        "HTMLElement.prototype = Object.create(Element.prototype);"
        "HTMLElement.prototype.constructor = HTMLElement;"
        ""
        // SVGElement extends Element
        "function SVGElement() {"
        "  Element.call(this);"
        "}"
        "SVGElement.prototype = Object.create(Element.prototype);"
        "SVGElement.prototype.constructor = SVGElement;"
        ""
        // HTMLAnchorElement (for <a> tags)
        "function HTMLAnchorElement() {"
        "  HTMLElement.call(this);"
        "  this.tagName = 'A';"
        "  this.href = '';"
        "  this.protocol = 'https:';"
        "  this.host = '';"
        "  this.hostname = '';"
        "  this.port = '';"
        "  this.pathname = '';"
        "  this.search = '';"
        "  this.hash = '';"
        "  this.username = '';"
        "  this.password = '';"
        "  this.origin = '';"
        "}"
        "HTMLAnchorElement.prototype = Object.create(HTMLElement.prototype);"
        "HTMLAnchorElement.prototype.constructor = HTMLAnchorElement;"
        ""
        // HTMLScriptElement
        "function HTMLScriptElement() {"
        "  HTMLElement.call(this);"
        "  this.tagName = 'SCRIPT';"
        "  this.src = '';"
        "  this.type = '';"
        "  this.async = false;"
        "  this.defer = false;"
        "}"
        "HTMLScriptElement.prototype = Object.create(HTMLElement.prototype);"
        "HTMLScriptElement.prototype.constructor = HTMLScriptElement;"
        ""
        // HTMLDivElement
        "function HTMLDivElement() {"
        "  HTMLElement.call(this);"
        "  this.tagName = 'DIV';"
        "}"
        "HTMLDivElement.prototype = Object.create(HTMLElement.prototype);"
        "HTMLDivElement.prototype.constructor = HTMLDivElement;"
        ""
        // HTMLSpanElement
        "function HTMLSpanElement() {"
        "  HTMLElement.call(this);"
        "  this.tagName = 'SPAN';"
        "}"
        "HTMLSpanElement.prototype = Object.create(HTMLElement.prototype);"
        "HTMLSpanElement.prototype.constructor = HTMLSpanElement;"
        ""
        // Expose to window
        "window.Node = Node;"
        "window.Element = Element;"
        "window.HTMLElement = HTMLElement;"
        "window.SVGElement = SVGElement;"
        "window.HTMLAnchorElement = HTMLAnchorElement;"
        "window.HTMLScriptElement = HTMLScriptElement;"
        "window.HTMLDivElement = HTMLDivElement;"
        "window.HTMLSpanElement = HTMLSpanElement;"
        "window.HTMLVideoElement = HTMLVideoElement;"
        ""
        // Document class
        "function Document() {"
        "  Node.call(this);"
        "  this.body = null;"
        "  this.documentElement = null;"
        "  this.head = null;"
        "}"
        "Document.prototype = Object.create(Node.prototype);"
        "Document.prototype.constructor = Document;"
        "Document.prototype.createElement = function(tagName) {"
        "  tagName = String(tagName).toLowerCase();"
        "  if (tagName === 'video') return new HTMLVideoElement();"
        "  if (tagName === 'a') return new HTMLAnchorElement();"
        "  if (tagName === 'script') return new HTMLScriptElement();"
        "  if (tagName === 'div') return new HTMLDivElement();"
        "  if (tagName === 'span') return new HTMLSpanElement();"
        "  return new HTMLElement(tagName);"
        "};"
        "Document.prototype.getElementById = function(id) { return null; };"
        "Document.prototype.querySelector = function(sel) { return null; };"
        "Document.prototype.querySelectorAll = function(sel) { return []; };"
        "Document.prototype.createTextNode = function(text) { return {textContent: text}; };"
        "Document.prototype.createComment = function(data) { return {data: data}; };"
        "window.Document = Document;"
        ""
        // TreeWalker for DOM traversal
        "function TreeWalker(root, whatToShow, filter) {"
        "  this.root = root;"
        "  this.whatToShow = whatToShow;"
        "  this.filter = filter;"
        "  this.currentNode = root;"
        "}"
        "TreeWalker.prototype.nextNode = function() { return null; };"
        "TreeWalker.prototype.previousNode = function() { return null; };"
        "TreeWalker.prototype.firstChild = function() { return null; };"
        "TreeWalker.prototype.lastChild = function() { return null; };"
        "TreeWalker.prototype.nextSibling = function() { return null; };"
        "TreeWalker.prototype.previousSibling = function() { return null; };"
        "TreeWalker.prototype.parentNode = function() { return null; };"
        "window.TreeWalker = TreeWalker;"
        "document.createTreeWalker = function(root, whatToShow, filter, entityReferenceExpansion) {"
        "  return new TreeWalker(root, whatToShow, filter);"
        "};"
        "document.createNodeIterator = function(root, whatToShow, filter) {"
        "  return { nextNode: function() { return null; }, previousNode: function() { return null; } };"
        "};"
    ;
    JS_Eval(ctx, dom_js, strlen(dom_js), "<dom>", 0);
    
    // MutationObserver polyfill - needed for YouTube player
    const char *mutation_observer_js = 
        "function MutationObserver(callback) {"
        "  this.callback = callback;"
        "  this.observing = false;"
        "}"
        "MutationObserver.prototype.observe = function(target, options) {"
        "  this.observing = true;"
        "  this.target = target;"
        "};"
        "MutationObserver.prototype.disconnect = function() {"
        "  this.observing = false;"
        "};"
        "MutationObserver.prototype.takeRecords = function() {"
        "  return [];"
        "};"
        "window.MutationObserver = MutationObserver;"
    ;
    JS_Eval(ctx, mutation_observer_js, strlen(mutation_observer_js), "<mutation_observer>", 0);
    
    // Web Components API stubs
    const char *webcomponents_js = 
        "var customElements = {"
        "  _registry: {},"
        "  define: function(name, constructor, options) {"
        "    this._registry[name] = constructor;"
        "  },"
        "  get: function(name) {"
        "    return this._registry[name];"
        "  },"
        "  whenDefined: function(name) {"
        "    return Promise.resolve();"
        "  }"
        "};"
        "window.customElements = customElements;"
        "window.HTMLElement = window.HTMLElement || function() {};"
        "window.HTMLElement.prototype = window.HTMLElement.prototype || {};"
    ;
    JS_Eval(ctx, webcomponents_js, strlen(webcomponents_js), "<webcomponents>", 0);
    
    // WebVTT API stub
    const char *webvtt_js = 
        "function VTTCue(startTime, endTime, text) {"
        "  this.startTime = startTime;"
        "  this.endTime = endTime;"
        "  this.text = text;"
        "  this.id = '';"
        "  this.pauseOnExit = false;"
        "}"
        "VTTCue.prototype.getCueAsHTML = function() {"
        "  return { textContent: this.text };"
        "};"
        "window.VTTCue = VTTCue;"
    ;
    JS_Eval(ctx, webvtt_js, strlen(webvtt_js), "<webvtt>", 0);
    
    // matchMedia
    const char *matchmedia_js = 
        "function matchMedia(query) {"
        "  return {"
        "    matches: false,"
        "    media: query,"
        "    addListener: function() {},"
        "    removeListener: function() {},"
        "    addEventListener: function() {},"
        "    removeEventListener: function() {},"
        "    dispatchEvent: function() {}"
        "  };"
        "}"
        "window.matchMedia = matchMedia;"
    ;
    JS_Eval(ctx, matchmedia_js, strlen(matchmedia_js), "<matchmedia>", 0);
    
    // requestIdleCallback
    const char *requestidle_js = 
        "function requestIdleCallback(callback, options) {"
        "  return setTimeout(callback, 1);"
        "}"
        "function cancelIdleCallback(id) {"
        "  clearTimeout(id);"
        "}"
        "window.requestIdleCallback = requestIdleCallback;"
        "window.cancelIdleCallback = cancelIdleCallback;"
    ;
    JS_Eval(ctx, requestidle_js, strlen(requestidle_js), "<requestidle>", 0);
    
    // DOMParser - needed for createHTMLDocument
    const char *domparser_js = 
        "function DOMParser() {}"
        "DOMParser.prototype.parseFromString = function(str, type) {"
        "  return document;"
        "};"
        "window.DOMParser = DOMParser;"
        ""
        // DOMImplementation for createHTMLDocument
        "var domImplementation = {"
        "  createHTMLDocument: function(title) {"
        "    var doc = new Document();"
        "    doc.title = title || '';"
        "    doc.body = { appendChild: function() {}, style: {} };"
        "    doc.documentElement = { style: {} };"
        "    return doc;"
        "  },"
        "  createDocument: function() { return new Document(); },"
        "  hasFeature: function() { return true; }"
        "};"
        "document.implementation = domImplementation;"
        "window.DOMImplementation = function() {};"
    ;
    JS_Eval(ctx, domparser_js, strlen(domparser_js), "<domparser>", 0);
    
    // IntersectionObserver polyfill
    const char *intersection_js = 
        "function IntersectionObserver(callback, options) {"
        "  this.callback = callback;"
        "  this.options = options || {};"
        "}"
        "IntersectionObserver.prototype.observe = function(target) {};"
        "IntersectionObserver.prototype.unobserve = function(target) {};"
        "IntersectionObserver.prototype.disconnect = function() {};"
        "IntersectionObserver.prototype.takeRecords = function() { return []; };"
        "window.IntersectionObserver = IntersectionObserver;"
        "function IntersectionObserverEntry() {}"
        "window.IntersectionObserverEntry = IntersectionObserverEntry;"
        "// ShadyDOM stub"
        "window.ShadyDOM = { inUse: false, force: false, noPatch: false };"
        "// YouTube SPF state"
        "window._spf_state = {};"
    ;
    JS_Eval(ctx, intersection_js, strlen(intersection_js), "<intersection>", 0);
    
    // ResizeObserver polyfill
    const char *resize_js = 
        "function ResizeObserver(callback) {"
        "  this.callback = callback;"
        "}"
        "ResizeObserver.prototype.observe = function(target) {};"
        "ResizeObserver.prototype.unobserve = function(target) {};"
        "ResizeObserver.prototype.disconnect = function() {};"
        "window.ResizeObserver = ResizeObserver;"
    ;
    JS_Eval(ctx, resize_js, strlen(resize_js), "<resize>", 0);
    
    // BroadcastChannel
    const char *broadcast_js = 
        "function BroadcastChannel(name) {"
        "  this.name = name;"
        "}"
        "BroadcastChannel.prototype.postMessage = function(msg) {};"
        "BroadcastChannel.prototype.close = function() {};"
        "BroadcastChannel.prototype.addEventListener = function() {};"
        "BroadcastChannel.prototype.removeEventListener = function() {};"
        "window.BroadcastChannel = BroadcastChannel;"
    ;
    JS_Eval(ctx, broadcast_js, strlen(broadcast_js), "<broadcast>", 0);
    
    // MessageChannel
    const char *messagechannel_js = 
        "function MessageChannel() {"
        "  this.port1 = { postMessage: function() {}, onmessage: null, addEventListener: function() {} };"
        "  this.port2 = { postMessage: function() {}, onmessage: null, addEventListener: function() {} };"
        "}"
        "window.MessageChannel = MessageChannel;"
    ;
    JS_Eval(ctx, messagechannel_js, strlen(messagechannel_js), "<messagechannel>", 0);
    
    // CSS object
    const char *css_js = 
        "var CSS = {"
        "  supports: function(property, value) { return false; },"
        "  escape: function(str) { return str; },"
        "  px: function(val) { return val + 'px'; }"
        "};"
        "window.CSS = CSS;"
    ;
    JS_Eval(ctx, css_js, strlen(css_js), "<css>", 0);
    
    // Web Animations API stub
    const char *animation_js = 
        "function Animation() {}"
        "Animation.prototype.play = function() {};"
        "Animation.prototype.pause = function() {};"
        "Animation.prototype.cancel = function() {};"
        "Animation.prototype.finish = function() {};"
        "Animation.prototype.reverse = function() {};"
        "function KeyframeEffect() {}"
        "window.Animation = Animation;"
        "window.KeyframeEffect = KeyframeEffect;"
    ;
    JS_Eval(ctx, animation_js, strlen(animation_js), "<animation>", 0);
    
    // YouTube-specific stubs - needed for player scripts
    const char *youtube_stubs_js = 
        // ytplayer - main player global
        "var ytplayer = ytplayer || {};"
        "ytplayer.config = ytplayer.config || {};"
        "ytplayer.bootstrapPlayerContainer = ytplayer.bootstrapPlayerContainer || function() {};"
        "ytplayer.bootstrapWebPlayerContextConfig = ytplayer.bootstrapWebPlayerContextConfig || function() { return {}; };"
        "ytplayer.load = ytplayer.load || function() {};"
        "ytplayer.getPlayer = ytplayer.getPlayer || function() { return null; };"
        "ytplayer.setPlayer = ytplayer.setPlayer || function() {};"
        "ytplayer.destroy = ytplayer.destroy || function() {};"
        
        // Polymer - web components library used by YouTube
        "var Polymer = Polymer || function() {};"
        "Polymer.Element = Polymer.Element || function() {};"
        "Polymer.dom = Polymer.dom || function() { return { querySelector: function() { return null; } }; };"
        "Polymer.RenderStatus = Polymer.RenderStatus || {};"
        "Polymer.RenderStatus.afterNextRender = function() {};"
        
        // YouTube app globals
        "var ytcfg = ytcfg || {};"
        "ytcfg.set = ytcfg.set || function() {};"
        "ytcfg.get = ytcfg.get || function() { return null; };"
        
        // yt namespace - comprehensive stubs
        "var yt = yt || {};"
        "yt.player = yt.player || {};"
        "yt.player.Application = yt.player.Application || function() {};"
        "yt.scheduler = yt.scheduler || {};"
        "yt.scheduler.scheduleAppLoad = yt.scheduler.scheduleAppLoad || function() {};"
        "yt.scheduler.cancelAppLoad = yt.scheduler.cancelAppLoad || function() {};"
        "yt.app = yt.app || {};"
        "yt.app.application = yt.app.application || {};"
        "yt.app.application.createComponent = yt.app.application.createComponent || function() {};"
        
        // ytsignals - used for app loading
        "if (typeof window.ytsignals !== 'object') { window.ytsignals = {}; }"
        "if (!window.ytsignals.getInstance) { window.ytsignals.getInstance = function() { return { whenReady: function() { return Promise.resolve(); }, get: function() { return null; }, set: function() {} }; }; }"
        
        // Initial load commands
        "window.loadInitialCommand = window.loadInitialCommand || function() {};"
        "window.loadInitialData = window.loadInitialData || function() {};"
        "window.ytInitialData = window.ytInitialData || {};"
        "window.ytInitialReelWatchSequenceResponse = window.ytInitialReelWatchSequenceResponse || {};"
        "window.ytPreviousCsn = window.ytPreviousCsn || '';"
        "window.__shady_native_addEventListener = window.__shady_native_addEventListener || function() {};"
        "window.HTMLTemplateElement = window.HTMLTemplateElement || function() {};"
        "window.CharacterData = window.CharacterData || function() {};"
        "window.ShadyCSS = window.ShadyCSS || { prepareTemplate: function() {}, styleElement: function() {} };"
        
        // spf (Structured Page Fragments) - used by YouTube for navigation
        "var spf = spf || {};"
        "spf.init = spf.init || function() {};"
        "spf.navigate = spf.navigate || function() {};"
        "spf.load = spf.load || function() {};"
        "spf.process = spf.process || function() {};"
        "spf.prefetch = spf.prefetch || function() {};"
        
        // Google Closure library namespace
        "var goog = goog || {};"
        "goog.global = window;"
        "goog.require = goog.require || function() {};"
        "goog.provide = goog.provide || function() {};"
        "goog.module = goog.module || function() {};"
        "goog.exportSymbol = goog.exportSymbol || function() {};"
        "goog.exportProperty = goog.exportProperty || function() {};"
        "goog.inherits = goog.inherits || function() {};"
        "goog.base = goog.base || function() {};"
        
        // Common missing functions that cause "not a function" errors
        "if (typeof queueMicrotask === 'undefined') {"
        "  window.queueMicrotask = function(fn) { setTimeout(fn, 0); };"
        "}"
        "if (typeof requestIdleCallback === 'undefined') {"
        "  window.requestIdleCallback = function(fn) { return setTimeout(fn, 1); };"
        "}"
        "if (typeof cancelIdleCallback === 'undefined') {"
        "  window.cancelIdleCallback = function(id) { clearTimeout(id); };"
        "}"
        
        // Fix for "cannot read property of undefined" errors
        "window.yt = window.yt || yt;"
        "window.ytcfg = window.ytcfg || ytcfg;"
        "window.ytplayer = window.ytplayer || ytplayer;"
        "window.goog = window.goog || goog;"
        "window.spf = window.spf || spf;"
        "window.Polymer = window.Polymer || Polymer;"
        
        // Additional stubs for functions that might be called but not defined
        "window.getComputedStyle = window.getComputedStyle || function() { return {}; };"
        "window.matchMedia = window.matchMedia || function() { return { matches: false, addListener: function() {}, removeListener: function() {} }; };"
        "window.CustomEvent = window.CustomEvent || window.Event;"
        
        // Object.prototype stubs that might be missing
        "if (!Object.prototype.hasOwnProperty) { Object.prototype.hasOwnProperty = function(key) { return key in this; }; }"
        "if (!Object.prototype.toString) { Object.prototype.toString = function() { return '[object Object]'; }; }"
        
        // Array.prototype stubs
        "if (!Array.prototype.includes) { Array.prototype.includes = function(item) { return this.indexOf(item) !== -1; }; }"
        "if (!Array.prototype.find) { Array.prototype.find = function(fn) { for (var i = 0; i < this.length; i++) if (fn(this[i], i, this)) return this[i]; }; }"
        "if (!Array.prototype.findIndex) { Array.prototype.findIndex = function(fn) { for (var i = 0; i < this.length; i++) if (fn(this[i], i, this)) return i; return -1; }; }"
        
        // String.prototype stubs
        "if (!String.prototype.startsWith) { String.prototype.startsWith = function(s) { return this.indexOf(s) === 0; }; }"
        "if (!String.prototype.endsWith) { String.prototype.endsWith = function(s) { return this.slice(-s.length) === s; }; }"
        "if (!String.prototype.includes) { String.prototype.includes = function(s) { return this.indexOf(s) !== -1; }; }"
    ;
    JSValue yt_stubs_result = JS_Eval(ctx, youtube_stubs_js, strlen(youtube_stubs_js), "<youtube_stubs>", 0);
    if (JS_IsException(yt_stubs_result)) {
        JSValue exception = JS_GetException(ctx);
        const char *error = JS_ToCString(ctx, exception);
        LOG_ERROR("YouTube stubs error: %s", error ? error : "unknown");
        JS_FreeCString(ctx, error);
        JS_FreeValue(ctx, exception);
    } else {
        LOG_INFO("YouTube stubs loaded successfully");
    }
    JS_FreeValue(ctx, yt_stubs_result);
    
    // Verify ytsignals was set up
    JSValue ytsignals_check = JS_Eval(ctx, "typeof window.ytsignals", 23, "<check>", 0);
    const char *ytsignals_type = JS_ToCString(ctx, ytsignals_check);
    LOG_INFO("After stubs: window.ytsignals = %s", ytsignals_type ? ytsignals_type : "unknown");
    JS_FreeCString(ctx, ytsignals_type);
    JS_FreeValue(ctx, ytsignals_check);
    
    // Check if getInstance exists
    JSValue getinstance_check = JS_Eval(ctx, "typeof (window.ytsignals && window.ytsignals.getInstance)", 45, "<check>", 0);
    const char *getinstance_type = JS_ToCString(ctx, getinstance_check);
    LOG_INFO("After stubs: window.ytsignals.getInstance = %s", getinstance_type ? getinstance_type : "unknown");
    JS_FreeCString(ctx, getinstance_type);
    JS_FreeValue(ctx, getinstance_check);
    
    JS_FreeValue(ctx, global);
}

bool js_quickjs_init(void) {
    JSClassDef xhr_class_def = {
        "XMLHttpRequest",
        .finalizer = js_xhr_finalizer,
    };
    
    JSClassDef video_class_def = {
        "HTMLVideoElement",
        .finalizer = js_video_finalizer,
    };
    
    JS_NewClassID(&js_xhr_class_id);
    JS_NewClassID(&js_video_class_id);
    
    return true;
}

void js_quickjs_cleanup(void) {
    pthread_mutex_lock(&g_url_mutex);
    g_captured_url_count = 0;
    pthread_mutex_unlock(&g_url_mutex);
}

// Helper to extract attribute value from HTML tag
static char* extract_attr(const char *html, const char *tag_end, const char *attr_name) {
    const char *attr = strstr(html, attr_name);
    if (!attr || attr > tag_end) return NULL;
    
    attr += strlen(attr_name);
    while (*attr && isspace((unsigned char)*attr)) attr++;
    if (*attr != '=') return NULL;
    attr++;
    while (*attr && isspace((unsigned char)*attr)) attr++;
    
    char quote = *attr;
    if (quote != '"' && quote != '\'') return NULL;
    
    attr++;
    const char *end = strchr(attr, quote);
    if (!end || end > tag_end) return NULL;
    
    size_t len = end - attr;
    char *value = malloc(len + 1);
    if (value) {
        strncpy(value, attr, len);
        value[len] = '\0';
    }
    return value;
}

// Parse HTML and create video elements from <video> tags
static int create_video_elements_from_html(JSContext *ctx, const char *html) {
    if (!html) return 0;
    
    int count = 0;
    const char *p = html;
    
    LOG_INFO("Parsing HTML for video elements...");
    
    while ((p = strstr(p, "<video")) != NULL) {
        // Find end of opening tag
        const char *tag_end = strchr(p, '>');
        if (!tag_end) break;
        
        // Check if it's a self-closing tag or has separate closing tag
        int is_self_closing = (tag_end[-1] == '/');
        
        LOG_INFO("Found <video> tag at position %zu", p - html);
        
        // Extract attributes
        char *id = extract_attr(p, tag_end, "id");
        char *src = extract_attr(p, tag_end, "src");
        char *data_src = extract_attr(p, tag_end, "data-src");
        char *class_attr = extract_attr(p, tag_end, "class");
        
        // Build JS to create element
        char js_code[4096];
        snprintf(js_code, sizeof(js_code),
            "(function() {\n"
            "  var video = document.createElement('video');\n"
            "  if (video) {\n"
            "    video.id = '%s';\n"
            "    video.className = '%s';\n"
            "    video.src = '%s';\n"
            "    if (document.body) document.body.appendChild(video);\n"
            "    console.log('Created video element from HTML: id=' + video.id + ', src=' + video.src.substring(0, 50));\n"
            "  }\n"
            "})();",
            id ? id : "",
            class_attr ? class_attr : "",
            src ? src : (data_src ? data_src : "")
        );
        
        JSValue result = JS_Eval(ctx, js_code, strlen(js_code), "<html_video>", 0);
        if (JS_IsException(result)) {
            JSValue exception = JS_GetException(ctx);
            const char *error = JS_ToCString(ctx, exception);
            LOG_ERROR("Error creating video element from HTML: %s", error ? error : "unknown");
            JS_FreeCString(ctx, error);
            JS_FreeValue(ctx, exception);
        } else {
            LOG_INFO("Created video element from HTML: id=%s, src=%.50s%s",
                     id ? id : "(none)",
                     src ? src : (data_src ? data_src : "(none)"),
                     (src && strlen(src) > 50) || (data_src && strlen(data_src) > 50) ? "..." : "");
            count++;
        }
        JS_FreeValue(ctx, result);
        
        free(id);
        free(src);
        free(data_src);
        free(class_attr);
        
        p = tag_end + 1;
    }
    
    LOG_INFO("Created %d video elements from HTML", count);
    return count;
}

bool js_quickjs_exec_scripts_with_data(const char **scripts, const size_t *script_lens, 
                                       int script_count, const char *player_response,
                                       const char *html, JsExecResult *out_result) {
    if (!scripts || script_count <= 0 || !out_result) {
        LOG_ERROR("Invalid arguments to js_quickjs_exec_scripts_with_data");
        return false;
    }
    
    // Reset captured URLs
    pthread_mutex_lock(&g_url_mutex);
    g_captured_url_count = 0;
    pthread_mutex_unlock(&g_url_mutex);
    
    // Clear output
    memset(out_result, 0, sizeof(JsExecResult));
    out_result->status = JS_EXEC_ERROR;
    
    // Create runtime and context
    JSRuntime *rt = JS_NewRuntime();
    if (!rt) {
        LOG_ERROR("Failed to create QuickJS runtime");
        return false;
    }
    
    JS_SetMemoryLimit(rt, 256 * 1024 * 1024); // 256MB
    JS_SetMaxStackSize(rt, 8 * 1024 * 1024);  // 8MB
    
    JSContext *ctx = JS_NewContext(rt);
    if (!ctx) {
        LOG_ERROR("Failed to create QuickJS context");
        JS_FreeRuntime(rt);
        return false;
    }
    
    // Initialize classes
    JSClassDef xhr_def = {"XMLHttpRequest", .finalizer = js_xhr_finalizer};
    JSClassDef video_def = {"HTMLVideoElement", .finalizer = js_video_finalizer};
    JS_NewClass(JS_GetRuntime(ctx), js_xhr_class_id, &xhr_def);
    JS_NewClass(JS_GetRuntime(ctx), js_video_class_id, &video_def);
    
    // Initialize browser environment
    init_browser_environment(ctx);
    
    // Create basic browser environment
    const char *setup_js = 
        "// Create body element for appendChild\n"
        "document.body = document.createElement('body');\n"
        "console.log('Basic browser environment ready');\n"
    ;
    
    LOG_INFO("Setting up basic browser environment...");
    JSValue setup_result = JS_Eval(ctx, setup_js, strlen(setup_js), "<setup>", 0);
    if (JS_IsException(setup_result)) {
        JSValue exception = JS_GetException(ctx);
        const char *error = JS_ToCString(ctx, exception);
        LOG_ERROR("Setup error: %s", error ? error : "unknown");
        JS_FreeCString(ctx, error);
        JS_FreeValue(ctx, exception);
    }
    JS_FreeValue(ctx, setup_result);
    
    // Parse HTML and create video elements BEFORE loading scripts
    // This handles Scenario B: HTML has <video> tags directly
    if (html && strlen(html) > 0) {
        int video_count = create_video_elements_from_html(ctx, html);
        LOG_INFO("Created %d video elements from HTML parsing", video_count);
    }
    
    // Also create default video element for Scenario A: JS creates video element
    const char *default_video_js = 
        "// Create default video element if none exists\n"
        "if (typeof document !== 'undefined' && document.body) {\n"
        "  try {\n"
        "    if (!document.getElementById('movie_player')) {\n"
        "      var video = document.createElement('video');\n"
        "      if (video) {\n"
        "        video.id = 'movie_player';\n"
        "        document.body.appendChild(video);\n"
        "        console.log('Created default video element with id=movie_player');\n"
        "      }\n"
        "    }\n"
        "  } catch(e) {\n"
        "    console.log('Error creating default video: ' + e.message);\n"
        "  }\n"
        "}\n"
    ;
    
    LOG_INFO("Creating default video element...");
    JSValue default_result = JS_Eval(ctx, default_video_js, strlen(default_video_js), "<default_video>", 0);
    if (JS_IsException(default_result)) {
        JSValue exception = JS_GetException(ctx);
        const char *error = JS_ToCString(ctx, exception);
        LOG_ERROR("Default video error: %s", error ? error : "unknown");
        JS_FreeCString(ctx, error);
        JS_FreeValue(ctx, exception);
    }
    JS_FreeValue(ctx, default_result);
    
    // Inject ytInitialPlayerResponse if provided
    if (player_response && strlen(player_response) > 0) {
        LOG_INFO("Injecting ytInitialPlayerResponse (%zu bytes)", strlen(player_response));
        
        // Try to parse the JSON as-is
        JSValue json_val = JS_ParseJSON(ctx, player_response, strlen(player_response), "<player_response>");
        
        if (JS_IsException(json_val)) {
            JSValue exception = JS_GetException(ctx);
            const char *error = JS_ToCString(ctx, exception);
            LOG_WARN("Could not parse ytInitialPlayerResponse JSON: %s", error ? error : "unknown");
            LOG_WARN("Continuing without player response injection - scripts may still work");
            JS_FreeCString(ctx, error);
            JS_FreeValue(ctx, exception);
        } else {
            // Set as global variable
            JSValue global = JS_GetGlobalObject(ctx);
            JS_SetPropertyStr(ctx, global, "ytInitialPlayerResponse", json_val);
            JS_FreeValue(ctx, global);
            LOG_INFO("ytInitialPlayerResponse injected successfully");
        }
    }
    
    // Note: base.js has bundled Polymer that manages its own state
    // We cannot easily patch it since it uses local variables, not window.Polymer
    
    // Pre-execution stubs - ensure critical functions exist before running scripts
    const char *pre_exec_stubs = 
        // Ensure scheduleAppLoad and related functions exist
        "if (typeof yt === 'undefined') window.yt = {};"
        "if (!yt.scheduler) yt.scheduler = {};"
        "if (!yt.scheduler.scheduleAppLoad) yt.scheduler.scheduleAppLoad = function() {};"
        "if (!yt.scheduler.cancelAppLoad) yt.scheduler.cancelAppLoad = function() {};"
        "if (!yt.app) yt.app = {};"
        "if (!yt.app.application) yt.app.application = {};"
        "if (!yt.app.application.createComponent) yt.app.application.createComponent = function() {};"
        // Ensure spf exists
        "if (typeof spf === 'undefined') window.spf = {};"
        "if (!spf.init) spf.init = function() {};"
        "if (!spf.navigate) spf.navigate = function() {};"
        "if (!spf.load) spf.load = function() {};"
        "if (!spf.process) spf.process = function() {};"
        // Ensure Polymer methods exist
        "if (typeof Polymer === 'undefined') window.Polymer = function() {};"
        "if (!Polymer.RenderStatus) Polymer.RenderStatus = {};"
        "if (!Polymer.RenderStatus.afterNextRender) Polymer.RenderStatus.afterNextRender = function() {};"
        "if (!Polymer.dom) Polymer.dom = function() { return { querySelector: function() { return null; } }; };"
        // Ensure Closure library functions exist
        "if (typeof goog === 'undefined') window.goog = {};"
        "if (!goog.exportSymbol) goog.exportSymbol = function() {};"
        "if (!goog.exportProperty) goog.exportProperty = function() {};"
        "if (!goog.inherits) goog.inherits = function() {};"
        "if (!goog.base) goog.base = function() {};"
        "if (!goog.require) goog.require = function() {};"
        "if (!goog.provide) goog.provide = function() {};"
        "if (!goog.module) goog.module = function() {};"
        "if (!goog.define) goog.define = function(name, val) { return val; };"
        "if (!goog.global) goog.global = window;"
        // Ensure ytsignals exists (used for app loading)
        "if (typeof window.ytsignals === 'undefined') window.ytsignals = {};"
        "if (!window.ytsignals.getInstance) window.ytsignals.getInstance = function() { return { whenReady: function() { return Promise.resolve(); }, get: function() { return null; }, set: function() {} }; };"
    ;
    JSValue pre_exec_result = JS_Eval(ctx, pre_exec_stubs, strlen(pre_exec_stubs), "<pre_exec_stubs>", 0);
    if (JS_IsException(pre_exec_result)) {
        JSValue ex = JS_GetException(ctx);
        const char *ex_str = JS_ToCString(ctx, ex);
        LOG_WARN("Pre-exec stubs error: %s", ex_str ? ex_str : "unknown");
        JS_FreeCString(ctx, ex_str);
        JS_FreeValue(ctx, ex);
    }
    JS_FreeValue(ctx, pre_exec_result);
    
    // Execute all scripts
    int success_count = 0;
    for (int i = 0; i < script_count; i++) {
        if (!scripts[i] || script_lens[i] == 0) continue;
        
        char filename[64];
        snprintf(filename, sizeof(filename), "<script_%d>", i);
        
        LOG_INFO("Executing script %d/%d (%zu bytes)", i + 1, script_count, script_lens[i]);
        
        // Wrap scripts that tend to fail in try-catch so they don't crash the whole execution
        // The signature decryption function might still be set up even if some parts fail
        JSValue result;
        // Wrap large scripts and base.js (contains signature decryption)
        if (script_lens[i] > 5000000 || strstr(scripts[i], "_yt_player") != NULL || strstr(scripts[i], "player") != NULL) {
            // Wrap large scripts and known problematic scripts in try-catch
            size_t wrapped_size = script_lens[i] + 100;
            char *wrapped = malloc(wrapped_size);
            if (wrapped) {
                int header_len = snprintf(wrapped, wrapped_size, "try{");
                memcpy(wrapped + header_len, scripts[i], script_lens[i]);
                int footer_len = snprintf(wrapped + header_len + script_lens[i], wrapped_size - header_len - script_lens[i], "}catch(e){}");
                size_t total_len = header_len + script_lens[i] + footer_len;
                result = JS_Eval(ctx, wrapped, total_len, filename, JS_EVAL_TYPE_GLOBAL);
                free(wrapped);
            } else {
                result = JS_Eval(ctx, scripts[i], script_lens[i], filename, JS_EVAL_TYPE_GLOBAL);
            }
        } else {
            result = JS_Eval(ctx, scripts[i], script_lens[i], filename, JS_EVAL_TYPE_GLOBAL);
        }
        
        if (JS_IsException(result)) {
            JSValue exception = JS_GetException(ctx);
            const char *error = JS_ToCString(ctx, exception);
            LOG_ERROR("Script %d execution error: %s", i, error ? error : "unknown");
            
            // Log first 200 chars of failing script for debugging
            if (script_lens[i] > 0) {
                char preview[201];
                size_t preview_len = script_lens[i] < 200 ? script_lens[i] : 200;
                memcpy(preview, scripts[i], preview_len);
                preview[preview_len] = '\0';
                // Replace newlines with spaces for single-line log
                for (size_t j = 0; j < preview_len; j++) {
                    if (preview[j] == '\n' || preview[j] == '\r') preview[j] = ' ';
                }
                LOG_ERROR("Script %d content: %.200s%s", i, preview, script_lens[i] > 200 ? "..." : "");
            }
            
            // Get stack trace for debugging
            JSValue stack_val = JS_GetPropertyStr(ctx, exception, "stack");
            const char *stack = JS_ToCString(ctx, stack_val);
            if (stack && strstr(error, "es5Shimmed")) {
                LOG_ERROR("=== STACK TRACE for es5Shimmed error (Script %d) ===", i);
                LOG_ERROR("%s", stack);
                LOG_ERROR("=== END STACK TRACE ===");
            } else if (stack) {
                LOG_ERROR("Stack trace (Script %d): %.500s%s", i, stack, strlen(stack) > 500 ? "..." : "");
            }
            
            // Dump script content around error position for Script 2
            if (i == 2 && error) {
                LOG_ERROR("=== SCRIPT %d CONTENT (find .prototype) ===", i);
                // Search for .prototype in the script
                char *proto_ptr = strstr(scripts[i], ".prototype");
                int count = 0;
                while (proto_ptr && count < 20) {
                    size_t offset = proto_ptr - scripts[i];
                    char context[101];
                    // Get 50 chars before and after .prototype
                    size_t start = (offset > 50) ? offset - 50 : 0;
                    size_t len = (script_lens[i] - start > 100) ? 100 : script_lens[i] - start;
                    memcpy(context, scripts[i] + start, len);
                    context[len] = '\0';
                    for (size_t j = 0; j < len; j++) {
                        if (context[j] == '\n' || context[j] == '\r') context[j] = ' ';
                    }
                    LOG_ERROR("SCRIPT%d-PROTO%d: %s", i, count, context);
                    
                    // Find next occurrence
                    proto_ptr = strstr(proto_ptr + 1, ".prototype");
                    count++;
                }
                LOG_ERROR("=== END SCRIPT %d PROTO search ===", i);
            }
            
            // For prototype errors, analyze stack trace
            if (error && strstr(error, "prototype of undefined") && stack) {
                LOG_ERROR("=== ANALYZING PROTOTYPE ERROR for Script %d ===", i);
                LOG_ERROR("Stack: %s", stack);
                
                // Try to find line number in stack trace (format: <script_2>:101:199)
                char line_num_str[32] = {0};
                const char *script_tag = strstr(stack, filename);
                if (script_tag) {
                    const char *colon = strchr(script_tag, ':');
                    if (colon) {
                        int line_num = atoi(colon + 1);
                        LOG_ERROR("Error at line %d", line_num);
                        
                        // Extract that line from script
                        if (line_num > 0 && scripts[i]) {
                            const char *p = scripts[i];
                            int current_line = 1;
                            while (*p && current_line < line_num) {
                                if (*p == '\n') current_line++;
                                p++;
                            }
                            if (current_line == line_num) {
                                const char *line_end = strchr(p, '\n');
                                if (!line_end) line_end = p + strlen(p);
                                size_t line_len = line_end - p;
                                if (line_len > 200) line_len = 200;
                                char line_buf[201];
                                memcpy(line_buf, p, line_len);
                                line_buf[line_len] = '\0';
                                LOG_ERROR("Line %d content: %s", line_num, line_buf);
                            }
                        }
                    }
                }
            }
            // For Script 9 (main player), dump context around errors
            if (i == 9 && error) {
                LOG_ERROR("=== SCRIPT 9 ERROR ANALYSIS ===");
                // Dump at multiple offsets to find the error location
                // Error is at line 7875:45, which is approximately character position ~280000
                size_t error_pos = 289574; // From previous touchAction search
                if (error_pos < script_lens[i]) {
                    // Dump 200 chars before and after error position
                    size_t start = (error_pos > 200) ? error_pos - 200 : 0;
                    size_t len = (script_lens[i] - start > 400) ? 400 : script_lens[i] - start;
                    char context[401];
                    memcpy(context, scripts[i] + start, len);
                    context[len] = '\0';
                    for (size_t j = 0; j < len; j++) {
                        if (context[j] == '\n' || context[j] == '\r') context[j] = ' ';
                    }
                    LOG_ERROR("SCRIPT9-CONTEXT: %s", context);
                }
                LOG_ERROR("=== END SCRIPT 9 ANALYSIS ===");
            }
            
            JS_FreeCString(ctx, stack);
            JS_FreeValue(ctx, stack_val);
            
            JS_FreeCString(ctx, error);
            JS_FreeValue(ctx, exception);
        } else {
            success_count++;
            LOG_INFO("Script %d executed successfully", i);
        }
        JS_FreeValue(ctx, result);
    }
    
    LOG_INFO("Executed %d/%d scripts successfully", success_count, script_count);
    
    // After scripts load, dispatch DOMContentLoaded to trigger player initialization
    // The video element and ytInitialPlayerResponse were already set up before scripts loaded
    const char *init_player_js = 
        "// Dispatch DOMContentLoaded to trigger any player initialization\n"
        "if (typeof window !== 'undefined' && window.dispatchEvent) {\n"
        "  var readyEvent = { type: 'DOMContentLoaded', bubbles: true };\n"
        "  window.dispatchEvent(readyEvent);\n"
        "  console.log('Dispatched DOMContentLoaded');\n"
        "}\n"
        "\n"
        "// Log what we have available\n"
        "if (typeof ytInitialPlayerResponse !== 'undefined') {\n"
        "  console.log('ytInitialPlayerResponse is available');\n"
        "}\n"
        "if (document.getElementById('movie_player')) {\n"
        "  console.log('movie_player element exists');\n"
        "}\n"
        "\n"
        "// === DISCOVER PLAYER APIS ===\n"
        "console.log('=== DISCOVERING PLAYER APIS ===');\n"
        "\n"
        "// Check for yt object\n"
        "if (typeof yt !== 'undefined') {\n"
        "  console.log('yt object found');\n"
        "  for (var key in yt) {\n"
        "    console.log('  yt.' + key + ' = ' + typeof yt[key]);\n"
        "  }\n"
        "  if (yt.player) {\n"
        "    console.log('yt.player found');\n"
        "    for (var key in yt.player) {\n"
        "      console.log('  yt.player.' + key + ' = ' + typeof yt.player[key]);\n"
        "    }\n"
        "  }\n"
        "} else {\n"
        "  console.log('yt object NOT found');\n"
        "}\n"
        "\n"
        "// Check for player-related globals\n"
        "var playerGlobals = ['player', 'ytPlayer', 'ytplayer', 'Player'];\n"
        "for (var i = 0; i < playerGlobals.length; i++) {\n"
        "  var name = playerGlobals[i];\n"
        "  if (typeof window[name] !== 'undefined') {\n"
        "    console.log('window.' + name + ' = ' + typeof window[name]);\n"
        "  }\n"
        "}\n"
        "\n"
        "// Check for decipher-related functions\n"
        "console.log('=== LOOKING FOR DECIPHER FUNCTIONS ===');\n"
        "var funcCount = 0;\n"
        "for (var key in window) {\n"
        "  if (typeof window[key] === 'function' && key.length < 10) {\n"
        "    try {\n"
        "      var fnStr = window[key].toString();\n"
        "      // Look for signature manipulation patterns\n"
        "      if (fnStr.indexOf('split') > -1 && fnStr.length < 500) {\n"
        "        console.log('Potential func ' + key + ': ' + fnStr.substring(0, 100));\n"
        "        funcCount++;\n"
        "        if (funcCount > 5) break;\n"
        "      }\n"
        "    } catch(e) {}\n"
        "  }\n"
        "}\n"
        "console.log('=== END DISCOVERY ===');\n"
    ;
    
    LOG_INFO("Triggering DOMContentLoaded...");
    JSValue init_result = JS_Eval(ctx, init_player_js, strlen(init_player_js), "<init_player>", 0);
    if (JS_IsException(init_result)) {
        JSValue exception = JS_GetException(ctx);
        const char *error = JS_ToCString(ctx, exception);
        LOG_ERROR("Player init error: %s", error ? error : "unknown");
        JS_FreeCString(ctx, error);
        JS_FreeValue(ctx, exception);
    }
    JS_FreeValue(ctx, init_result);
    
    // Get captured URLs
    pthread_mutex_lock(&g_url_mutex);
    out_result->captured_url_count = g_captured_url_count;
    for (int i = 0; i < g_captured_url_count && i < JS_MAX_CAPTURED_URLS; i++) {
        strncpy(out_result->captured_urls[i], g_captured_urls[i], JS_MAX_URL_LEN - 1);
        out_result->captured_urls[i][JS_MAX_URL_LEN - 1] = '\0';
    }
    pthread_mutex_unlock(&g_url_mutex);
    
    LOG_INFO("Captured %d URLs from JS execution", out_result->captured_url_count);
    for (int i = 0; i < out_result->captured_url_count; i++) {
        LOG_INFO("  URL %d: %.100s...", i, out_result->captured_urls[i]);
    }
    
    out_result->status = (success_count > 0) ? JS_EXEC_SUCCESS : JS_EXEC_ERROR;
    
    // Cleanup
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    
    return out_result->status == JS_EXEC_SUCCESS;
}

int js_quickjs_get_captured_urls(char urls[][JS_MAX_URL_LEN], int max_urls) {
    pthread_mutex_lock(&g_url_mutex);
    int count = 0;
    for (int i = 0; i < g_captured_url_count && i < max_urls && i < MAX_CAPTURED_URLS; i++) {
        strncpy(urls[i], g_captured_urls[i], JS_MAX_URL_LEN - 1);
        urls[i][JS_MAX_URL_LEN - 1] = '\0';
        count++;
    }
    pthread_mutex_unlock(&g_url_mutex);
    return count;
}
