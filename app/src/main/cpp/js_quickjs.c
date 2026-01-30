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
        strncpy(vid->src, src, sizeof(vid->src) - 1);
        record_captured_url(src);
    }
    JS_FreeCString(ctx, src);
    return JS_UNDEFINED;
}

static JSValue js_video_get_src(JSContext *ctx, JSValueConst this_val) {
    HTMLVideoElement *vid = JS_GetOpaque2(ctx, this_val, js_video_class_id);
    if (!vid) return JS_EXCEPTION;
    return JS_NewString(ctx, vid->src);
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
    
    // URL capture array
    const char *init_js = 
        "var __capturedUrls = [];"
        "function __recordUrl(url) {"
        "  if (url && url.indexOf && __capturedUrls.indexOf(url) < 0) {"
        "    __capturedUrls.push(url);"
        "    if (console && console.log) console.log('Captured URL:', url.substring(0, 100));"
        "  }"
        "}"
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

bool js_quickjs_exec_scripts_with_data(const char **scripts, const size_t *script_lens, 
                                       int script_count, const char *player_response,
                                       JsExecResult *out_result) {
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
    
    // Inject ytInitialPlayerResponse if provided
    if (player_response && strlen(player_response) > 0) {
        size_t inject_len = strlen(player_response) + 64;
        char *inject_code = malloc(inject_len);
        if (inject_code) {
            snprintf(inject_code, inject_len, "var ytInitialPlayerResponse = %s;", player_response);
            LOG_INFO("Injecting ytInitialPlayerResponse (%zu bytes)", strlen(player_response));
            JSValue result = JS_Eval(ctx, inject_code, strlen(inject_code), "<player_response>", 0);
            if (JS_IsException(result)) {
                JSValue exception = JS_GetException(ctx);
                const char *error = JS_ToCString(ctx, exception);
                LOG_ERROR("Error injecting ytInitialPlayerResponse: %s", error ? error : "unknown");
                JS_FreeCString(ctx, error);
                JS_FreeValue(ctx, exception);
            } else {
                LOG_INFO("ytInitialPlayerResponse injected successfully");
            }
            JS_FreeValue(ctx, result);
            free(inject_code);
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
    
    // After scripts load, simulate player initialization
    // YouTube's player expects DOM ready and may need a video element
    const char *init_player_js = 
        "// Create video element and add to document\n"
        "var video = document.createElement('video');\n"
        "video.id = 'movie_player';\n"
        "video.autoplay = false;\n"
        "video.preload = 'auto';\n"
        "if (document.body) document.body.appendChild(video);\n"
        "console.log('Created video element');\n"
        "\n"
        "// Simulate DOM ready\n"
        "if (typeof window !== 'undefined') {\n"
        "  var readyEvent = { type: 'DOMContentLoaded', bubbles: true };\n"
        "  if (window.dispatchEvent) window.dispatchEvent(readyEvent);\n"
        "}\n"
        "\n"
        "// Check for player initialization\n"
        "if (typeof ytInitialPlayerResponse !== 'undefined' && ytInitialPlayerResponse) {\n"
        "  console.log('Player response available, videoId:', ytInitialPlayerResponse.videoDetails ? ytInitialPlayerResponse.videoDetails.videoId : 'unknown');\n"
        "  \n"
        "  // Try to find and trigger any player config\n"
        "  if (window.yt && window.yt.player && window.yt.player.Application) {\n"
        "    try {\n"
        "      console.log('Found yt.player.Application');\n"
        "      var app = window.yt.player.Application;\n"
        "      if (app.create) {\n"
        "        var player = app.create('movie_player', ytInitialPlayerResponse);\n"
        "        console.log('Created player:', typeof player);\n"
        "        if (player && player.loadVideoByPlayerVars) {\n"
        "          player.loadVideoByPlayerVars(ytInitialPlayerResponse);\n"
        "        }\n"
        "      }\n"
        "    } catch(e) {\n"
        "      console.log('Application init error:', e.message);\n"
        "    }\n"
        "  }\n"
        "  \n"
        "  // Alternative: Try yt.player.Player\n"
        "  if (window.yt && window.yt.player && window.yt.player.Player) {\n"
        "    try {\n"
        "      console.log('Found yt.player.Player');\n"
        "      var player = new window.yt.player.Player('movie_player', {\n"
        "        videoId: ytInitialPlayerResponse.videoDetails ? ytInitialPlayerResponse.videoDetails.videoId : '',\n"
        "        playerVars: { autoplay: 0 }\n"
        "      });\n"
        "    } catch(e) {\n"
        "      console.log('Player init error:', e.message);\n"
        "    }\n"
        "  }\n"
        "}\n"
    ;
    
    LOG_INFO("Triggering player initialization...");
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
