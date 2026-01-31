#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <ctype.h>
#include <android/log.h>
#include "js_quickjs.h"
#include "cutils.h"
#include "quickjs.h"

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
    JS_SetPropertyStr(ctx, global, "document", document);
    
    // window
    JSValue window = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, window, "addEventListener", JS_NewCFunction(ctx, js_dummy_function, "addEventListener", 2));
    JS_SetPropertyStr(ctx, window, "removeEventListener", JS_NewCFunction(ctx, js_dummy_function, "removeEventListener", 2));
    JS_SetPropertyStr(ctx, window, "setTimeout", JS_NewCFunction(ctx, js_dummy_function, "setTimeout", 2));
    JS_SetPropertyStr(ctx, window, "setInterval", JS_NewCFunction(ctx, js_dummy_function, "setInterval", 2));
    JS_SetPropertyStr(ctx, window, "clearTimeout", JS_NewCFunction(ctx, js_dummy_function, "clearTimeout", 1));
    JS_SetPropertyStr(ctx, window, "clearInterval", JS_NewCFunction(ctx, js_dummy_function, "clearInterval", 1));
    JS_SetPropertyStr(ctx, window, "requestAnimationFrame", JS_NewCFunction(ctx, js_dummy_function, "requestAnimationFrame", 1));
    JS_SetPropertyStr(ctx, window, "cancelAnimationFrame", JS_NewCFunction(ctx, js_dummy_function, "cancelAnimationFrame", 1));
    JS_SetPropertyStr(ctx, window, "alert", JS_NewCFunction(ctx, js_dummy_function, "alert", 1));
    JS_SetPropertyStr(ctx, window, "confirm", JS_NewCFunction(ctx, js_dummy_function, "confirm", 1));
    JS_SetPropertyStr(ctx, window, "prompt", JS_NewCFunction(ctx, js_dummy_function, "prompt", 2));
    JS_SetPropertyStr(ctx, window, "open", JS_NewCFunction(ctx, js_dummy_function, "open", 3));
    JS_SetPropertyStr(ctx, window, "close", JS_NewCFunction(ctx, js_dummy_function, "close", 0));
    JS_SetPropertyStr(ctx, window, "focus", JS_NewCFunction(ctx, js_dummy_function, "focus", 0));
    JS_SetPropertyStr(ctx, window, "blur", JS_NewCFunction(ctx, js_dummy_function, "blur", 0));
    JS_SetPropertyStr(ctx, window, "scrollTo", JS_NewCFunction(ctx, js_dummy_function, "scrollTo", 2));
    JS_SetPropertyStr(ctx, window, "scrollBy", JS_NewCFunction(ctx, js_dummy_function, "scrollBy", 2));
    JS_SetPropertyStr(ctx, window, "localStorage", JS_NewObject(ctx));
    JS_SetPropertyStr(ctx, window, "sessionStorage", JS_NewObject(ctx));
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
    JS_SetPropertyStr(ctx, global, "window", window);
    
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
    JS_SetPropertyStr(ctx, window, "location", location);
    JS_SetPropertyStr(ctx, document, "location", JS_DupValue(ctx, location));
    
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
    
    // performance
    JSValue performance = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, performance, "now", JS_NewCFunction(ctx, js_dummy_function, "now", 0));
    JS_SetPropertyStr(ctx, performance, "timing", JS_NewObject(ctx));
    JS_SetPropertyStr(ctx, performance, "navigation", JS_NewObject(ctx));
    JS_SetPropertyStr(ctx, window, "performance", performance);
    
    // CustomEvent constructor
    const char *custom_event_js = 
        "function CustomEvent(type, eventInitDict) {"
        "  eventInitDict = eventInitDict || {};"
        "  this.type = type;"
        "  this.detail = eventInitDict.detail;"
        "  this.bubbles = eventInitDict.bubbles || false;"
        "  this.cancelable = eventInitDict.cancelable || false;"
        "  this.defaultPrevented = false;"
        "  this.isTrusted = true;"
        "  this.timeStamp = Date.now();"
        "}"
        "CustomEvent.prototype.preventDefault = function() { this.defaultPrevented = true; };"
        "CustomEvent.prototype.stopPropagation = function() {};"
        "CustomEvent.prototype.stopImmediatePropagation = function() {};"
    ;
    JS_Eval(ctx, custom_event_js, strlen(custom_event_js), "<custom_event>", 0);
    
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
    
    // URL capture array and global navigator reference
    const char *init_js = 
        "var __capturedUrls = [];"
        "function __recordUrl(url) {"
        "  if (url && url.indexOf && __capturedUrls.indexOf(url) < 0) {"
        "    __capturedUrls.push(url);"
        "    if (console && console.log) console.log('Captured URL:', url.substring(0, 100));"
        "  }"
        "}"
        // Make navigator available globally (some scripts access it directly)
        "var navigator = window.navigator || {};"
        // Make Element and Node available globally
        "var Element = window.Element;"
        "var Node = window.Node;"
        "var HTMLElement = window.HTMLElement || Element;"
        "var SVGElement = window.SVGElement || Element;"
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
        // Expose to window
        "window.Node = Node;"
        "window.Element = Element;"
        "window.HTMLElement = HTMLElement;"
        "window.SVGElement = SVGElement;"
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
        "  if (tagName.toLowerCase() === 'video') {"
        "    return new HTMLVideoElement();"
        "  }"
        "  return new Element();"
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
        "if (!document.getElementById('movie_player')) {\n"
        "  var video = document.createElement('video');\n"
        "  video.id = 'movie_player';\n"
        "  document.body.appendChild(video);\n"
        "  console.log('Created default video element with id=movie_player');\n"
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
        
        // Use JS_ParseJSON to safely parse the JSON without injection issues
        JSValue json_val = JS_ParseJSON(ctx, player_response, strlen(player_response), "<player_response>");
        if (JS_IsException(json_val)) {
            JSValue exception = JS_GetException(ctx);
            const char *error = JS_ToCString(ctx, exception);
            LOG_ERROR("Error parsing ytInitialPlayerResponse JSON: %s", error ? error : "unknown");
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
    
    // Execute all scripts
    int success_count = 0;
    for (int i = 0; i < script_count; i++) {
        if (!scripts[i] || script_lens[i] == 0) continue;
        
        char filename[64];
        snprintf(filename, sizeof(filename), "<script_%d>", i);
        
        LOG_INFO("Executing script %d/%d (%zu bytes)", i + 1, script_count, script_lens[i]);
        
        JSValue result = JS_Eval(ctx, scripts[i], script_lens[i], filename, 0);
        
        if (JS_IsException(result)) {
            JSValue exception = JS_GetException(ctx);
            const char *error = JS_ToCString(ctx, exception);
            LOG_ERROR("Script %d execution error: %s", i, error ? error : "unknown");
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
