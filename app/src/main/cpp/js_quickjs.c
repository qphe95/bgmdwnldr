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
#include "html_dom.h"

#define LOG_TAG "js_quickjs"
#define LOG_INFO(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOG_ERROR(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOG_WARN(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

#define MAX_CAPTURED_URLS 64

// Global asset manager for loading browser stubs
static AAssetManager *g_asset_mgr = NULL;

// Set the global asset manager (call from main.c during startup)
void js_quickjs_set_asset_manager(AAssetManager *mgr) {
    g_asset_mgr = mgr;
}
#define URL_MAX_LEN 2048

// Forward declarations
static JSValue js_dummy_function(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

static JSClassID js_http_response_class_id;
JSClassID js_xhr_class_id;
JSClassID js_video_class_id;

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
    char response_text[2097152];  // 256KB for large JSON responses
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

JSValue js_xhr_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
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

const JSCFunctionListEntry js_xhr_proto_funcs[] = {
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
const size_t js_xhr_proto_funcs_count = sizeof(js_xhr_proto_funcs) / sizeof(js_xhr_proto_funcs[0]);

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

JSValue js_video_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
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
                 vid->id[0] != '\0' ? vid->id : "(none)");
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

const JSCFunctionListEntry js_video_proto_funcs[] = {
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
const size_t js_video_proto_funcs_count = sizeof(js_video_proto_funcs) / sizeof(js_video_proto_funcs[0]);

// Global fetch implementation
JSValue js_fetch(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
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
JSValue js_document_create_element(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
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

// Called from quickjs.c when a global var is defined
// This immediately syncs the var to window object
// Note: This function is called directly from quickjs.c via forward declaration
void js_quickjs_on_global_var_defined(JSContext *ctx, JSAtom var_name)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue window_obj = JS_GetPropertyStr(ctx, global, "window");
    
    // Skip if window doesn't exist or isn't an object
    if (JS_IsUndefined(window_obj) || JS_IsNull(window_obj) || !JS_IsObject(window_obj)) {
        JS_FreeValue(ctx, window_obj);
        JS_FreeValue(ctx, global);
        return;
    }
    
    // Skip if window === global (no need to sync to itself)
    // We check this by comparing using JS_StrictEq which returns 1 if equal
    int is_equal = JS_StrictEq(ctx, window_obj, global);
    if (is_equal == 1) {
        JS_FreeValue(ctx, window_obj);
        JS_FreeValue(ctx, global);
        return;
    }
    
    // Skip internal properties
    const char *prop_name = JS_AtomToCString(ctx, var_name);
    if (!prop_name) {
        JS_FreeValue(ctx, window_obj);
        JS_FreeValue(ctx, global);
        return;
    }
    
    // Skip properties that shouldn't be synced
    if (prop_name[0] == '_' || 
        strcmp(prop_name, "window") == 0 ||
        strcmp(prop_name, "globalThis") == 0 ||
        strcmp(prop_name, "self") == 0 ||
        strcmp(prop_name, "top") == 0 ||
        strcmp(prop_name, "parent") == 0 ||
        strcmp(prop_name, "location") == 0 ||
        strcmp(prop_name, "document") == 0 ||
        strcmp(prop_name, "console") == 0) {
        JS_FreeCString(ctx, prop_name);
        JS_FreeValue(ctx, window_obj);
        JS_FreeValue(ctx, global);
        return;
    }
    
    // Check if property already exists on window
    int has_prop = JS_HasProperty(ctx, window_obj, var_name);
    
    // If not on window, copy it from global
    if (!has_prop) {
        JSValue val = JS_GetProperty(ctx, global, var_name);
        if (!JS_IsException(val)) {
            // Skip undefined values - the callback may be called during closure
            // creation before the variable is actually initialized
            if (JS_IsUndefined(val)) {
                JS_FreeValue(ctx, val);
            } else {
                // Reference counting:
                // 1. global object holds a reference to the value
                // 2. val from JS_GetProperty is another reference (caller-owned)
                // 3. We dup val to create val_dup (third reference)
                // 4. We free val (back to global's reference + val_dup)
                // 5. JS_SetProperty consumes val_dup (back to global's reference)
                JSValue val_dup = JS_DupValue(ctx, val);
                JS_FreeValue(ctx, val);
                if (JS_SetProperty(ctx, window_obj, var_name, val_dup) < 0) {
                    // SetProperty failed, free our duped reference to avoid leak
                    JS_FreeValue(ctx, val_dup);
                }
                // If successful, val_dup reference is now owned by window_obj
            }
        }
    }
    
    JS_FreeCString(ctx, prop_name);
    JS_FreeValue(ctx, window_obj);
    JS_FreeValue(ctx, global);
}

// Helper function to set up prototype chain: Object.setPrototypeOf(childProto, parentProto)
static void js_set_prototype_chain(JSContext *ctx, JSValueConst child_proto, JSValueConst parent_proto) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue object_ctor = JS_GetPropertyStr(ctx, global, "Object");
    JSValue set_proto = JS_GetPropertyStr(ctx, object_ctor, "setPrototypeOf");
    
    JSValue args[2] = { child_proto, parent_proto };
    JSValue result = JS_Call(ctx, set_proto, JS_UNDEFINED, 2, args);
    
    JS_FreeValue(ctx, result);
    JS_FreeValue(ctx, set_proto);
    JS_FreeValue(ctx, object_ctor);
    JS_FreeValue(ctx, global);
}

// Helper to get a prototype from a constructor: Constructor.prototype
static JSValue js_get_prototype(JSContext *ctx, JSValueConst ctor) {
    return JS_GetPropertyStr(ctx, ctor, "prototype");
}

// Set up DOM prototype chains in C
static void js_setup_dom_prototypes(JSContext *ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    
    // Get constructors from global
    JSValue event_target = JS_GetPropertyStr(ctx, global, "EventTarget");
    JSValue node = JS_GetPropertyStr(ctx, global, "Node");
    JSValue element = JS_GetPropertyStr(ctx, global, "Element");
    JSValue html_element = JS_GetPropertyStr(ctx, global, "HTMLElement");
    JSValue doc_fragment = JS_GetPropertyStr(ctx, global, "DocumentFragment");
    
    // Get prototypes
    JSValue event_target_proto = js_get_prototype(ctx, event_target);
    JSValue node_proto = js_get_prototype(ctx, node);
    JSValue element_proto = js_get_prototype(ctx, element);
    JSValue html_element_proto = js_get_prototype(ctx, html_element);
    JSValue doc_fragment_proto = js_get_prototype(ctx, doc_fragment);
    
    // Set up prototype chains if all prototypes exist
    // Node.prototype -> EventTarget.prototype
    if (!JS_IsUndefined(node_proto) && !JS_IsNull(node_proto) &&
        !JS_IsUndefined(event_target_proto) && !JS_IsNull(event_target_proto)) {
        js_set_prototype_chain(ctx, node_proto, event_target_proto);
    }
    
    // Element.prototype -> Node.prototype
    if (!JS_IsUndefined(element_proto) && !JS_IsNull(element_proto) &&
        !JS_IsUndefined(node_proto) && !JS_IsNull(node_proto)) {
        js_set_prototype_chain(ctx, element_proto, node_proto);
    }
    
    // HTMLElement.prototype -> Element.prototype
    if (!JS_IsUndefined(html_element_proto) && !JS_IsNull(html_element_proto) &&
        !JS_IsUndefined(element_proto) && !JS_IsNull(element_proto)) {
        js_set_prototype_chain(ctx, html_element_proto, element_proto);
    }
    
    // DocumentFragment.prototype -> Node.prototype
    if (!JS_IsUndefined(doc_fragment_proto) && !JS_IsNull(doc_fragment_proto) &&
        !JS_IsUndefined(node_proto) && !JS_IsNull(node_proto)) {
        js_set_prototype_chain(ctx, doc_fragment_proto, node_proto);
    }
    
    // Clean up
    JS_FreeValue(ctx, event_target_proto);
    JS_FreeValue(ctx, node_proto);
    JS_FreeValue(ctx, element_proto);
    JS_FreeValue(ctx, html_element_proto);
    JS_FreeValue(ctx, doc_fragment_proto);
    
    JS_FreeValue(ctx, event_target);
    JS_FreeValue(ctx, node);
    JS_FreeValue(ctx, element);
    JS_FreeValue(ctx, html_element);
    JS_FreeValue(ctx, doc_fragment);
    
    // Ensure document.body exists
    JSValue document = JS_GetPropertyStr(ctx, global, "document");
    if (!JS_IsUndefined(document) && !JS_IsNull(document)) {
        JSValue body = JS_GetPropertyStr(ctx, document, "body");
        if (JS_IsUndefined(body) || JS_IsNull(body)) {
            // document.createElement('body') or use empty object
            JSValue create_elem = JS_GetPropertyStr(ctx, document, "createElement");
            if (!JS_IsUndefined(create_elem) && !JS_IsNull(create_elem)) {
                JSValue tag_name = JS_NewString(ctx, "body");
                JSValue args[1] = { tag_name };
                JSValue new_body = JS_Call(ctx, create_elem, document, 1, args);
                if (!JS_IsException(new_body)) {
                    JS_SetPropertyStr(ctx, document, "body", new_body);
                } else {
                    JS_FreeValue(ctx, new_body);
                    JSValue empty_obj = JS_NewObject(ctx);
                    JS_SetPropertyStr(ctx, document, "body", empty_obj);
                }
                JS_FreeValue(ctx, tag_name);
            } else {
                JSValue empty_obj = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, document, "body", empty_obj);
            }
            JS_FreeValue(ctx, create_elem);
        }
        JS_FreeValue(ctx, body);
    }
    JS_FreeValue(ctx, document);
    
    JS_FreeValue(ctx, global);
}

// Initialize browser environment
static void init_browser_environment(JSContext *ctx, AAssetManager *asset_mgr) {
    JSValue global = JS_GetGlobalObject(ctx);
    
    // Register native logging function
    JS_SetPropertyStr(ctx, global, "__bgmdwnldr_log", 
        JS_NewCFunction(ctx, js_bgmdwnldr_log, "__bgmdwnldr_log", 1));
    
    // Initialize all browser stubs (DOM, window, document, XMLHttpRequest, etc.)
    init_browser_stubs(ctx, global);
    
    JS_FreeValue(ctx, global);
    
    // Set up DOM prototype chains using C API
    js_setup_dom_prototypes(ctx);
    
    LOG_INFO("Browser environment initialized");
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

// Create DOM nodes from parsed HTML document
static int create_dom_nodes_from_parsed_html(JSContext *ctx, HtmlDocument *doc) {
    if (!ctx || !doc) return 0;
    
    int count = 0;
    
    /* Create elements from body children */
    if (doc->body && doc->body->first_child) {
        HtmlNode *node = doc->body->first_child;
        
        while (node) {
            if (node->type == HTML_NODE_ELEMENT) {
                /* Create the element */
                JSValue elem = html_create_element_js(ctx, node->tag_name, node->attributes);
                
                if (!JS_IsNull(elem) && !JS_IsException(elem)) {
                    /* Check for video elements specifically */
                    if (strcasecmp(node->tag_name, "video") == 0) {
                        LOG_INFO("Creating video element from parsed HTML");
                        
                        /* Extract src attribute if present */
                        HtmlAttribute *attr = node->attributes;
                        while (attr) {
                            if (strcasecmp(attr->name, "src") == 0 && attr->value[0]) {
                                JS_SetPropertyStr(ctx, elem, "src", JS_NewString(ctx, attr->value));
                                record_captured_url(attr->value);
                            }
                            if (strcasecmp(attr->name, "id") == 0 && attr->value[0]) {
                                JS_SetPropertyStr(ctx, elem, "id", JS_NewString(ctx, attr->value));
                            }
                            attr = attr->next;
                        }
                        
                        count++;
                    }
                    
                    /* Add to document.body */
                    JSValue body = JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "document");
                    if (!JS_IsUndefined(body) && !JS_IsNull(body)) {
                        JSValue doc_body = JS_GetPropertyStr(ctx, body, "body");
                        if (!JS_IsUndefined(doc_body) && !JS_IsNull(doc_body)) {
                            JSValue appendChild = JS_GetPropertyStr(ctx, doc_body, "appendChild");
                            if (!JS_IsUndefined(appendChild) && !JS_IsNull(appendChild)) {
                                JSValue args[1] = { elem };
                                JS_Call(ctx, appendChild, doc_body, 1, args);
                                JS_FreeValue(ctx, appendChild);
                            }
                            JS_FreeValue(ctx, doc_body);
                        }
                        JS_FreeValue(ctx, body);
                    }
                    
                    JS_FreeValue(ctx, elem);
                }
            }
            node = node->next_sibling;
        }
    }
    
    return count;
}

// Parse HTML and create elements from <video> tags using the proper DOM parser
static int create_video_elements_from_html(JSContext *ctx, const char *html) {
    if (!html) return 0;
    
    LOG_INFO("Parsing HTML with DOM parser...");
    
    /* Parse the HTML document */
    HtmlDocument *doc = html_parse(html, strlen(html));
    if (!doc) {
        LOG_ERROR("Failed to parse HTML document");
        return 0;
    }
    
    /* Get the count of video elements in the parsed document */
    HtmlNode *video_nodes[64];
    int video_count = html_document_get_elements_by_tag(doc, "video", video_nodes, 64);
    
    LOG_INFO("Found %d video elements in parsed HTML", video_count);
    
    /* Create video elements from the parsed DOM */
    int created = 0;
    for (int i = 0; i < video_count; i++) {
        HtmlNode *node = video_nodes[i];
        
        /* Create video element */
        JSValue video = js_video_constructor(ctx, JS_NULL, 0, NULL);
        
        if (!JS_IsException(video)) {
            /* Extract and set attributes */
            HtmlAttribute *attr = node->attributes;
            while (attr) {
                if (strcasecmp(attr->name, "id") == 0 && attr->value[0]) {
                    JS_SetPropertyStr(ctx, video, "id", JS_NewString(ctx, attr->value));
                } else if (strcasecmp(attr->name, "src") == 0 && attr->value[0]) {
                    JS_SetPropertyStr(ctx, video, "src", JS_NewString(ctx, attr->value));
                    record_captured_url(attr->value);
                } else if (strcasecmp(attr->name, "class") == 0 && attr->value[0]) {
                    JS_SetPropertyStr(ctx, video, "className", JS_NewString(ctx, attr->value));
                }
                attr = attr->next;
            }
            
            /* If no ID set but one exists in JS, use a default */
            JSValue id_prop = JS_GetPropertyStr(ctx, video, "id");
            const char *current_id = JS_ToCString(ctx, id_prop);
            if (!current_id || !current_id[0]) {
                char default_id[32];
                snprintf(default_id, sizeof(default_id), "video_%d", i);
                JS_SetPropertyStr(ctx, video, "id", JS_NewString(ctx, default_id));
            }
            JS_FreeCString(ctx, current_id);
            JS_FreeValue(ctx, id_prop);
            
            /* Add to document.body */
            JSValue global = JS_GetGlobalObject(ctx);
            JSValue doc = JS_GetPropertyStr(ctx, global, "document");
            JSValue body = JS_GetPropertyStr(ctx, doc, "body");
            
            if (!JS_IsUndefined(body) && !JS_IsNull(body)) {
                JSValue appendChild = JS_GetPropertyStr(ctx, body, "appendChild");
                if (!JS_IsUndefined(appendChild) && !JS_IsNull(appendChild)) {
                    JSValue args[1] = { video };
                    JS_Call(ctx, appendChild, body, 1, args);
                    JS_FreeValue(ctx, appendChild);
                    created++;
                }
            }
            
            JS_FreeValue(ctx, body);
            JS_FreeValue(ctx, doc);
            JS_FreeValue(ctx, global);
            JS_FreeValue(ctx, video);
        }
    }
    
    LOG_INFO("Created %d video elements from parsed HTML", created);
    
    /* Free the parsed document */
    html_document_free(doc);
    
    return created;
}

bool js_quickjs_exec_scripts(const char **scripts, const size_t *script_lens, 
                             int script_count, const char *html, 
                             AAssetManager *asset_mgr,
                             JsExecResult *out_result) {
    if (!scripts || script_count <= 0 || !out_result) {
        LOG_ERROR("Invalid arguments to js_quickjs_exec_scripts");
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
    init_browser_environment(ctx, asset_mgr ? asset_mgr : g_asset_mgr);
    
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
    
    // Note: Data payload scripts (ytInitialPlayerResponse, ytInitialData, etc.)
    // will execute naturally as part of the scripts array, defining global
    // variables just like in a real browser. No manual injection needed.
    
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
        // Wrap large scripts and base.js (contains signature decryption) OR web-animations polyfill
        if (script_lens[i] > 5000000 || strstr(scripts[i], "_yt_player") != NULL || strstr(scripts[i], "player") != NULL ||
            strstr(scripts[i], "web-animations") != NULL || script_lens[i] > 50000) {
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
            
            // After base.js (script 0) executes, check what it created
            if (i == 0 && script_lens[i] > 1000000) {
                const char *check_base_js = 
                    "console.log('=== BASE.JS CHECK ===');"
                    "console.log('window.yt type: ' + typeof window.yt);"
                    "console.log('window.ytcfg type: ' + typeof window.ytcfg);"
                    "console.log('window.ytplayer type: ' + typeof window.ytplayer);"
                    "if (typeof window.yt === 'object') {"
                    "  console.log('yt keys: ' + Object.keys(window.yt).join(', '));"
                    "}"
                    "if (typeof window.ytcfg === 'object') {"
                    "  console.log('ytcfg keys: ' + Object.keys(window.ytcfg).join(', '));"
                    "}"
                    "console.log('=== END BASE.JS CHECK ===');";
                JSValue check_result = JS_Eval(ctx, check_base_js, strlen(check_base_js), "<check_base>", 0);
                JS_FreeValue(ctx, check_result);
            }
        }
        JS_FreeValue(ctx, result);
    }
    
    LOG_INFO("Executed %d/%d scripts successfully", success_count, script_count);
    
    // After scripts load, dispatch DOMContentLoaded to trigger player initialization
    // The video element and ytInitialPlayerResponse were already set up before scripts loaded
    const char *init_player_js = 
        "// Debug: Log all window properties that might be player-related\n"
        "var playerKeys = Object.keys(window).filter(function(k) {\n"
        "  return k.toLowerCase().indexOf('player') >= 0 || k.toLowerCase().indexOf('yt') >= 0 || k.toLowerCase().indexOf('decrypt') >= 0;\n"
        "});\n"
        "console.log('Player/yt related globals: ' + playerKeys.join(', '));\n"
        "\n"
        "// Check for specific player objects\n"
        "console.log('window.player exists: ' + (typeof window.player));\n"
        "console.log('window.ytPlayer exists: ' + (typeof window.ytPlayer));\n"
        "console.log('window.yt exists: ' + (typeof window.yt));\n"
        "console.log('window.ytcfg exists: ' + (typeof window.ytcfg));\n"
        "console.log('window.ytsignals exists: ' + (typeof window.ytsignals));\n"
        "\n"
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
