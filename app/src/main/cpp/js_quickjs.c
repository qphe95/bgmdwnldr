#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <ctype.h>
#include <android/log.h>
#include "js_quickjs.h"
#include "cutils.h"
#include "quickjs.h"
#include "quickjs_gc_unified.h"
#include "browser_stubs.h"
#include "html_dom.h"
#include "js_value_helpers.h"
/* Using unified GC allocator from quickjs_gc_unified.h */

/* Debug logging removed - using LLDB for debugging */

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
JSClassID js_xhr_class_id = 0;
JSClassID js_video_class_id = 0;

// Global state for URL capture
static char g_captured_urls[MAX_CAPTURED_URLS][URL_MAX_LEN];
static int g_captured_url_count = 0;
static pthread_mutex_t g_url_mutex = PTHREAD_MUTEX_INITIALIZER;

// Record a captured URL
// BUG FIX #1: Fixed buffer overflow using memcpy with explicit length validation
void record_captured_url(const char *url) {
    if (!url) return;
    
    size_t url_len = strlen(url);
    if (url_len == 0 || url_len >= URL_MAX_LEN) {
        return;
    }
    
    pthread_mutex_lock(&g_url_mutex);
    
    // Check for duplicates
    for (int i = 0; i < g_captured_url_count; i++) {
        if (strcmp(g_captured_urls[i], url) == 0) {
            pthread_mutex_unlock(&g_url_mutex);
            return;
        }
    }
    
    // Add new URL using memcpy for safe copy
    if (g_captured_url_count < MAX_CAPTURED_URLS) {
        memcpy(g_captured_urls[g_captured_url_count], url, url_len);
        g_captured_urls[g_captured_url_count][url_len] = '\0';
        g_captured_url_count++;
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
        // Free JSValue fields to prevent memory leaks
        JS_FreeValueRT(rt, xhr->headers);
        JS_FreeValueRT(rt, xhr->onload);
        JS_FreeValueRT(rt, xhr->onerror);
        JS_FreeValueRT(rt, xhr->onreadystatechange);
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
        // Free all event handler references that were duped in setters
        // These must be freed to prevent memory leaks
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
        strncpy(vid->src, src, sizeof(vid->src) - 1);
        vid->src[sizeof(vid->src) - 1] = '\0';
        record_captured_url(src);
    } else {
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

// Generic getter/setter for event callbacks
// NOTE: Proper reference counting is critical here. When storing a JSValue
// in the C struct, we must dup it to prevent use-after-free when the
// original value is garbage collected.
#define DEFINE_VIDEO_EVENT_HANDLER(name, field) \
    static JSValue js_video_get_##name(JSContext *ctx, JSValueConst this_val) { \
        HTMLVideoElement *vid = JS_GetOpaque2(ctx, this_val, js_video_class_id); \
        if (!vid) return JS_EXCEPTION; \
        return JS_DupValue(ctx, vid->field); \
    } \
    static JSValue js_video_set_##name(JSContext *ctx, JSValueConst this_val, JSValueConst val) { \
        HTMLVideoElement *vid = JS_GetOpaque2(ctx, this_val, js_video_class_id); \
        if (!vid) return JS_EXCEPTION; \
        JS_FreeValue(ctx, vid->field); \
        vid->field = JS_DupValue(ctx, val); \
        return JS_UNDEFINED; \
    }

DEFINE_VIDEO_EVENT_HANDLER(onloadstart, onloadstart)
DEFINE_VIDEO_EVENT_HANDLER(onloadedmetadata, onloadedmetadata)
DEFINE_VIDEO_EVENT_HANDLER(oncanplay, oncanplay)
DEFINE_VIDEO_EVENT_HANDLER(onplay, onplay)
DEFINE_VIDEO_EVENT_HANDLER(onplaying, onplaying)
DEFINE_VIDEO_EVENT_HANDLER(onerror, onerror)

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
    JS_CGETSET_DEF("onloadstart", js_video_get_onloadstart, js_video_set_onloadstart),
    JS_CGETSET_DEF("onloadedmetadata", js_video_get_onloadedmetadata, js_video_set_onloadedmetadata),
    JS_CGETSET_DEF("oncanplay", js_video_get_oncanplay, js_video_set_oncanplay),
    JS_CGETSET_DEF("onplay", js_video_get_onplay, js_video_set_onplay),
    JS_CGETSET_DEF("onplaying", js_video_get_onplaying, js_video_set_onplaying),
    JS_CGETSET_DEF("onerror", js_video_get_onerror, js_video_set_onerror),
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
        JS_SetPropertyStr(ctx, this_val, prop, argv[1]);
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

// Native logging function for JavaScript debugging (disabled - using LLDB)
static JSValue js_bgmdwnldr_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

static JSValue js_console_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    for (int i = 0; i < argc; i++) {
        const char *str = JS_ToCString(ctx, argv[i]);
        if (str) {
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
            if (!JS_IsUndefined(val)) {
                // Proper reference counting:
                // 1. JS_GetProperty returns a new reference (val)
                // 2. JS_SetProperty consumes the reference we pass to it
                // 3. We don't need val after setting, so pass it directly
                if (JS_SetProperty(ctx, window_obj, var_name, val) < 0) {
                    // SetProperty failed, free the reference to avoid leak
                    JS_FreeValue(ctx, val);
                }
                // If successful, val reference is now owned by window_obj
            } else {
                // val is undefined, free the reference
                JS_FreeValue(ctx, val);
            }
        }
    }
    
    JS_FreeCString(ctx, prop_name);
    JS_FreeValue(ctx, window_obj);
    JS_FreeValue(ctx, global);
}

// Initialize browser environment
static void init_browser_environment(JSContext *ctx, AAssetManager *asset_mgr) {
    JSValue global = JS_GetGlobalObject(ctx);
    
    // Register native logging function
    JS_SetPropertyStr(ctx, global, "__bgmdwnldr_log", 
        JS_NewCFunction(ctx, js_bgmdwnldr_log, "__bgmdwnldr_log", 1));
    
    // Initialize all browser stubs (DOM, window, document, XMLHttpRequest, etc.)
    // This sets up constructors, prototype chains, and document.body
    init_browser_stubs(ctx, global);
    
    // Note: This QuickJS uses garbage collection, no need to free values explicitly
    (void)global;  // Suppress unused warning
    
}

// Static initializer for class IDs using GCC constructor attribute
// This ensures class IDs are initialized before main() is called
static void __attribute__((constructor)) js_quickjs_init_class_ids(void) {
    if (js_xhr_class_id == 0) {
        JS_NewClassID(&js_xhr_class_id);
    }
    if (js_video_class_id == 0) {
        JS_NewClassID(&js_video_class_id);
    }
}

bool js_quickjs_init(void) {
    // Initialize unified GC first - all memory comes from here
    if (!gc_is_initialized()) {
        if (!gc_init()) {
            return false;
        }
    } else {
        /* GC already initialized - reset it for fresh state.
         * This is needed when js_quickjs_exec_scripts is called multiple times.
         * The previous run may have left the GC in an inconsistent state. */
        gc_cleanup();
        if (!gc_init()) {
            return false;
        }
    }
    return true;
}

void js_quickjs_cleanup(void) {
    pthread_mutex_lock(&g_url_mutex);
    g_captured_url_count = 0;
    pthread_mutex_unlock(&g_url_mutex);
    
    // Cleanup unified GC
    gc_cleanup();
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
    char *value = gc_alloc_raw(len + 1);
    if (value) {
        strncpy(value, attr, len);
        value[len] = '\0';
    }
    return value;
}

// Create DOM nodes from parsed HTML document
// BUG FIX #2: Using shadow stack to track JSValues instead of manual JS_FreeValue
static int create_dom_nodes_from_parsed_html(JSContext *ctx, HtmlDocument *doc) {
    if (!ctx || !doc) return 0;
    
    int count = 0;
    
    /* Create elements from body children */
    if (doc->body && doc->body->first_child) {
        HtmlNode *node = doc->body->first_child;
        
        while (node) {
            if (node->type == HTML_NODE_ELEMENT) {
                /* Use scope to auto-track all JSValues */
                JS_SCOPE_BEGIN(ctx)
                {
                    /* Create the element */
                    JS_SCOPE_VALUE(ctx, elem, html_create_element_js(ctx, node->tag_name, node->attributes));
                    
                    if (!JS_IsNull(elem) && !JS_IsException(elem)) {
                        /* Check for video elements specifically */
                        if (strcasecmp(node->tag_name, "video") == 0) {
                            
                            /* Extract src attribute if present */
                            HtmlAttribute *attr = node->attributes;
                            while (attr) {
                                if (strcasecmp(attr->name, "src") == 0 && attr->value[0]) {
                                    JS_SCOPE_VALUE(ctx, src_val, JS_NewString(ctx, attr->value));
                                    JS_SetPropertyStr(ctx, elem, "src", src_val);
                                    record_captured_url(attr->value);
                                }
                                if (strcasecmp(attr->name, "id") == 0 && attr->value[0]) {
                                    JS_SCOPE_VALUE(ctx, id_val, JS_NewString(ctx, attr->value));
                                    JS_SetPropertyStr(ctx, elem, "id", id_val);
                                }
                                attr = attr->next;
                            }
                            
                            count++;
                        }
                        
                        /* Add to document.body */
                        JS_SCOPE_VALUE(ctx, global, JS_GetGlobalObject(ctx));
                        JS_SCOPE_VALUE(ctx, body, JS_GetPropertyStr(ctx, global, "document"));
                        
                        if (!JS_IsUndefined(body) && !JS_IsNull(body)) {
                            JS_SCOPE_VALUE(ctx, doc_body, JS_GetPropertyStr(ctx, body, "body"));
                            
                            if (!JS_IsUndefined(doc_body) && !JS_IsNull(doc_body)) {
                                JS_SCOPE_VALUE(ctx, appendChild, JS_GetPropertyStr(ctx, doc_body, "appendChild"));
                                
                                if (!JS_IsUndefined(appendChild) && !JS_IsNull(appendChild)) {
                                    JSValue args[1] = { elem };
                                    JS_SCOPE_VALUE(ctx, result, JS_Call(ctx, appendChild, doc_body, 1, args));
                                    (void)result; /* Result tracked by scope, ignore for logic */
                                }
                            }
                        }
                    }
                }
                JS_SCOPE_END(ctx);
            }
            node = node->next_sibling;
        }
    }
    
    return count;
}

// Parse HTML and create elements from <video> tags using the proper DOM parser
// BUG FIX #3: Using shadow stack to track JSValues instead of manual JS_FreeValue
static int create_video_elements_from_html(JSContext *ctx, const char *html) {
    if (!html) return 0;
    
    
    /* Parse the HTML document */
    HtmlDocument *doc = html_parse(html, strlen(html));
    if (!doc) {
        return 0;
    }
    
    /* Get the count of video elements in the parsed document */
    HtmlNode *video_nodes[64];
    int video_count = html_document_get_elements_by_tag(doc, "video", video_nodes, 64);
    
    
    /* Create video elements from the parsed DOM */
    int created = 0;
    for (int i = 0; i < video_count; i++) {
        HtmlNode *node = video_nodes[i];
        
        /* Use scope to auto-track all JSValues for this iteration */
        JS_SCOPE_BEGIN(ctx)
        {
            /* Create video element */
            JS_SCOPE_VALUE(ctx, video, js_video_constructor(ctx, JS_NULL, 0, NULL));
            
            if (!JS_IsException(video)) {
                /* Extract and set attributes */
                HtmlAttribute *attr = node->attributes;
                while (attr) {
                    if (strcasecmp(attr->name, "id") == 0 && attr->value[0]) {
                        JS_SCOPE_VALUE(ctx, id_val, JS_NewString(ctx, attr->value));
                        JS_SetPropertyStr(ctx, video, "id", id_val);
                    } else if (strcasecmp(attr->name, "src") == 0 && attr->value[0]) {
                        JS_SCOPE_VALUE(ctx, src_val, JS_NewString(ctx, attr->value));
                        JS_SetPropertyStr(ctx, video, "src", src_val);
                        record_captured_url(attr->value);
                    } else if (strcasecmp(attr->name, "class") == 0 && attr->value[0]) {
                        JS_SCOPE_VALUE(ctx, class_val, JS_NewString(ctx, attr->value));
                        JS_SetPropertyStr(ctx, video, "className", class_val);
                    }
                    attr = attr->next;
                }
                
                /* If no ID set but one exists in JS, use a default */
                JS_SCOPE_VALUE(ctx, id_prop, JS_GetPropertyStr(ctx, video, "id"));
                const char *current_id = JS_ToCString(ctx, id_prop);
                if (!current_id || !current_id[0]) {
                    char default_id[32];
                    snprintf(default_id, sizeof(default_id), "video_%d", i);
                    JS_SCOPE_VALUE(ctx, default_id_val, JS_NewString(ctx, default_id));
                    JS_SetPropertyStr(ctx, video, "id", default_id_val);
                }
                JS_FreeCString(ctx, current_id);

                /* Add to document.body */
                JS_SCOPE_VALUE(ctx, global, JS_GetGlobalObject(ctx));
                JS_SCOPE_VALUE(ctx, doc_obj, JS_GetPropertyStr(ctx, global, "document"));
                JS_SCOPE_VALUE(ctx, body, JS_GetPropertyStr(ctx, doc_obj, "body"));
                
                if (!JS_IsUndefined(body) && !JS_IsNull(body)) {
                    JS_SCOPE_VALUE(ctx, appendChild, JS_GetPropertyStr(ctx, body, "appendChild"));
                    
                    if (!JS_IsUndefined(appendChild) && !JS_IsNull(appendChild)) {
                        JSValue args[1] = { video };
                        JS_SCOPE_VALUE(ctx, result, JS_Call(ctx, appendChild, body, 1, args));
                        (void)result; /* Result tracked by scope */
                        
                        created++;
                    }
                }
            }
        }
        JS_SCOPE_END(ctx);
    }
    
    
    /* Free the parsed document */
    html_document_free(doc);
    
    return created;
}

bool js_quickjs_exec_scripts(const char **scripts, const size_t *script_lens, 
                             int script_count, const char *html, 
                             AAssetManager *asset_mgr,
                             JsExecResult *out_result) {
    if (!scripts || script_count <= 0 || !out_result) {
        return false;
    }
    
    // Reset captured URLs
    pthread_mutex_lock(&g_url_mutex);
    g_captured_url_count = 0;
    pthread_mutex_unlock(&g_url_mutex);
    
    // Clear output
    memset(out_result, 0, sizeof(JsExecResult));
    out_result->status = JS_EXEC_ERROR;
    
    // Initialize GC first (must happen before any other QuickJS calls)
    if (!js_quickjs_init()) {
        return false;
    }
    
    // Create runtime using unified GC allocator
    
    JSRuntime *rt = JS_NewRuntime();
    if (!rt) {
        return false;
    }
    
    // Set limits after successful runtime creation
    JS_SetMemoryLimit(rt, 256 * 1024 * 1024); // 256MB
    JS_SetMaxStackSize(rt, 8 * 1024 * 1024);  // 8MB
    
    // Create context - this initializes built-in objects
    JSContext *ctx = JS_NewContext(rt);
    
    if (!ctx) {
        JS_FreeRuntime(rt);
        return false;
    }
    
    // NOW register custom classes after context is created
    JSClassDef xhr_def = {"XMLHttpRequest", .finalizer = js_xhr_finalizer};
    JSClassDef video_def = {"HTMLVideoElement", .finalizer = js_video_finalizer};
    if (JS_NewClass(rt, js_xhr_class_id, &xhr_def) < 0) {
    }
    if (JS_NewClass(rt, js_video_class_id, &video_def) < 0) {
    }
    
    // Initialize full browser environment with all necessary APIs
    init_browser_environment(ctx, asset_mgr);

    // Parse HTML and create video elements BEFORE loading scripts
    // This handles Scenario B: HTML has <video> tags directly
    if (html && strlen(html) > 0) {
        int video_count = create_video_elements_from_html(ctx, html);
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
    
    JSValue default_result = JS_Eval(ctx, default_video_js, strlen(default_video_js), "<default_video>", 0);
    if (JS_IsException(default_result)) {
        JSValue exception = JS_GetException(ctx);
        const char *error = JS_ToCString(ctx, exception);
        JS_FreeCString(ctx, error);

    }

    // Note: Data payload scripts (ytInitialPlayerResponse, ytInitialData, etc.)
    // will execute naturally as part of the scripts array, defining global
    // variables just like in a real browser. No manual injection needed.
    
    // Execute all scripts
    int success_count = 0;
    for (int i = 0; i < script_count; i++) {
        if (!scripts[i] || script_lens[i] == 0) continue;
        
        char filename[64];
        snprintf(filename, sizeof(filename), "<script_%d>", i);
        
        
        // Wrap scripts that tend to fail in try-catch so they don't crash the whole execution
        // The signature decryption function might still be set up even if some parts fail
        JSValue result;
        // Wrap large scripts and base.js (contains signature decryption) OR web-animations polyfill
        if (script_lens[i] > 5000000 || strstr(scripts[i], "_yt_player") != NULL || strstr(scripts[i], "player") != NULL ||
            strstr(scripts[i], "web-animations") != NULL || script_lens[i] > 50000) {
            // Wrap large scripts and known problematic scripts in try-catch
            size_t wrapped_size = script_lens[i] + 100;
            char *wrapped = gc_alloc_raw(wrapped_size);
            if (wrapped) {
                int header_len = snprintf(wrapped, wrapped_size, "try{");
                memcpy(wrapped + header_len, scripts[i], script_lens[i]);
                int footer_len = snprintf(wrapped + header_len + script_lens[i], wrapped_size - header_len - script_lens[i], "}catch(e){}");
                size_t total_len = header_len + script_lens[i] + footer_len;
                result = JS_Eval(ctx, wrapped, total_len, filename, JS_EVAL_TYPE_GLOBAL);
                /* GC will reclaim memory on reset - no individual free needed */
            } else {
                result = JS_Eval(ctx, scripts[i], script_lens[i], filename, JS_EVAL_TYPE_GLOBAL);
            }
        } else {
            result = JS_Eval(ctx, scripts[i], script_lens[i], filename, JS_EVAL_TYPE_GLOBAL);
        }
        
        if (JS_IsException(result)) {
            JSValue exception = JS_GetException(ctx);
            const char *error = JS_ToCString(ctx, exception);
            
            // Get stack trace for debugging
            JSValue stack_val = JS_GetPropertyStr(ctx, exception, "stack");
            const char *stack = JS_ToCString(ctx, stack_val);
            (void)stack; /* Silence unused warning when debugging disabled */
            
            // Dump script content around error position for Script 2
            if (i == 2 && error) {
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
                    
                    // Find next occurrence
                    proto_ptr = strstr(proto_ptr + 1, ".prototype");
                    count++;
                }
            }
            
            JS_FreeCString(ctx, stack);

            JS_FreeCString(ctx, error);

        } else {
            success_count++;
            
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

            }
        }

    }
    
    
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
    
    JSValue init_result = JS_Eval(ctx, init_player_js, strlen(init_player_js), "<init_player>", 0);
    if (JS_IsException(init_result)) {
        JSValue exception = JS_GetException(ctx);
        const char *error = JS_ToCString(ctx, exception);
        JS_FreeCString(ctx, error);

    }

    // Get captured URLs
    pthread_mutex_lock(&g_url_mutex);
    // BUG FIX #5: Safe string copy using memcpy with proper length validation
    out_result->captured_url_count = g_captured_url_count;
    for (int i = 0; i < g_captured_url_count && i < JS_MAX_CAPTURED_URLS; i++) {
        size_t len = strlen(g_captured_urls[i]);
        if (len >= JS_MAX_URL_LEN) {
            len = JS_MAX_URL_LEN - 1;
        }
        memcpy(out_result->captured_urls[i], g_captured_urls[i], len);
        out_result->captured_urls[i][len] = '\0';
    }
    pthread_mutex_unlock(&g_url_mutex);
    
    for (int i = 0; i < out_result->captured_url_count; i++) {
    }
    
    out_result->status = (success_count > 0) ? JS_EXEC_SUCCESS : JS_EXEC_ERROR;
    
    // Cleanup
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    
    return out_result->status == JS_EXEC_SUCCESS;
}

// BUG FIX #4: Added parameter validation before locking mutex
int js_quickjs_get_captured_urls(char urls[][JS_MAX_URL_LEN], int max_urls) {
    if (!urls || max_urls <= 0) {
        return 0;
    }
    
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
