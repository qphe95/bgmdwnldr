/*
 * Browser Stubs - C implementation of DOM/Browser APIs for QuickJS
 */
#include <string.h>
#include <stdlib.h>
#include <quickjs.h>
#include "browser_stubs.h"

// External symbols from js_quickjs.c
extern JSValue js_document_create_element(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
extern JSClassID js_xhr_class_id;
extern JSClassID js_video_class_id;
extern JSValue js_xhr_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv);
extern JSValue js_video_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv);
extern JSValue js_fetch(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
extern const JSCFunctionListEntry js_xhr_proto_funcs[];
extern const JSCFunctionListEntry js_video_proto_funcs[];
extern const size_t js_xhr_proto_funcs_count;
extern const size_t js_video_proto_funcs_count;

// Helper macros
#define DEF_FUNC(ctx, parent, name, func, argc) \
    JS_SetPropertyStr(ctx, parent, name, JS_NewCFunction(ctx, func, name, argc))

#define DEF_PROP_STR(ctx, obj, name, value) \
    JS_SetPropertyStr(ctx, obj, name, JS_NewString(ctx, value))

#define DEF_PROP_INT(ctx, obj, name, value) \
    JS_SetPropertyStr(ctx, obj, name, JS_NewInt32(ctx, value))

#define DEF_PROP_BOOL(ctx, obj, name, value) \
    JS_SetPropertyStr(ctx, obj, name, JS_NewBool(ctx, value))

#define DEF_PROP_FLOAT(ctx, obj, name, value) \
    JS_SetPropertyStr(ctx, obj, name, JS_NewFloat64(ctx, value))

// Stub functions
static JSValue js_undefined(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return JS_UNDEFINED;
}

static JSValue js_null(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return JS_NULL;
}

static JSValue js_empty_array(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewArray(ctx);
}

static JSValue js_empty_string(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewString(ctx, "");
}

static JSValue js_false(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return JS_FALSE;
}

static JSValue js_true(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return JS_TRUE;
}

static JSValue js_zero(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewInt32(ctx, 0);
}

static JSValue js_console_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return JS_UNDEFINED;
}

// Main initialization
void init_browser_stubs(JSContext *ctx, JSValue global) {
    // ===== Document =====
    JSValue document = JS_NewObject(ctx);
    DEF_PROP_INT(ctx, document, "nodeType", 9);
    DEF_PROP_STR(ctx, document, "readyState", "complete");
    DEF_PROP_STR(ctx, document, "characterSet", "UTF-8");
    DEF_PROP_STR(ctx, document, "charset", "UTF-8");
    DEF_PROP_STR(ctx, document, "contentType", "text/html");
    DEF_PROP_STR(ctx, document, "referrer", "https://www.youtube.com/");
    DEF_PROP_STR(ctx, document, "cookie", "");
    DEF_PROP_STR(ctx, document, "domain", "www.youtube.com");
    DEF_FUNC(ctx, document, "createElement", js_document_create_element, 1);
    DEF_FUNC(ctx, document, "createElementNS", js_document_create_element, 2);
    DEF_FUNC(ctx, document, "createTextNode", js_empty_string, 1);
    DEF_FUNC(ctx, document, "createComment", js_empty_string, 1);
    DEF_FUNC(ctx, document, "createDocumentFragment", js_null, 0);
    DEF_FUNC(ctx, document, "getElementById", js_null, 1);
    DEF_FUNC(ctx, document, "querySelector", js_null, 1);
    DEF_FUNC(ctx, document, "querySelectorAll", js_empty_array, 1);
    DEF_FUNC(ctx, document, "getElementsByTagName", js_empty_array, 1);
    DEF_FUNC(ctx, document, "getElementsByClassName", js_empty_array, 1);
    DEF_FUNC(ctx, document, "getElementsByName", js_empty_array, 1);
    DEF_FUNC(ctx, document, "addEventListener", js_undefined, 2);
    DEF_FUNC(ctx, document, "removeEventListener", js_undefined, 2);
    DEF_FUNC(ctx, document, "dispatchEvent", js_true, 1);
    JS_SetPropertyStr(ctx, global, "document", document);
    
    // ===== Window =====
    JSValue window = JS_NewObject(ctx);
    DEF_PROP_INT(ctx, window, "innerWidth", 1920);
    DEF_PROP_INT(ctx, window, "innerHeight", 1080);
    DEF_PROP_INT(ctx, window, "outerWidth", 1920);
    DEF_PROP_INT(ctx, window, "outerHeight", 1080);
    DEF_PROP_INT(ctx, window, "screenX", 0);
    DEF_PROP_INT(ctx, window, "screenY", 0);
    DEF_PROP_FLOAT(ctx, window, "devicePixelRatio", 1.0);
    DEF_FUNC(ctx, window, "setTimeout", js_zero, 2);
    DEF_FUNC(ctx, window, "setInterval", js_zero, 2);
    DEF_FUNC(ctx, window, "clearTimeout", js_undefined, 1);
    DEF_FUNC(ctx, window, "clearInterval", js_undefined, 1);
    DEF_FUNC(ctx, window, "requestAnimationFrame", js_zero, 1);
    DEF_FUNC(ctx, window, "cancelAnimationFrame", js_undefined, 1);
    DEF_FUNC(ctx, window, "alert", js_undefined, 1);
    DEF_FUNC(ctx, window, "confirm", js_true, 0);
    DEF_FUNC(ctx, window, "prompt", js_empty_string, 1);
    DEF_FUNC(ctx, window, "open", js_null, 1);
    DEF_FUNC(ctx, window, "close", js_undefined, 0);
    DEF_FUNC(ctx, window, "focus", js_undefined, 0);
    DEF_FUNC(ctx, window, "blur", js_undefined, 0);
    DEF_FUNC(ctx, window, "scrollTo", js_undefined, 2);
    DEF_FUNC(ctx, window, "scrollBy", js_undefined, 2);
    DEF_FUNC(ctx, window, "postMessage", js_undefined, 2);
    DEF_FUNC(ctx, window, "addEventListener", js_undefined, 2);
    DEF_FUNC(ctx, window, "removeEventListener", js_undefined, 2);
    DEF_FUNC(ctx, window, "dispatchEvent", js_true, 1);
    JS_SetPropertyStr(ctx, global, "window", window);
    JS_SetPropertyStr(ctx, window, "window", JS_DupValue(ctx, window));
    JS_SetPropertyStr(ctx, window, "self", JS_DupValue(ctx, window));
    JS_SetPropertyStr(ctx, window, "top", JS_DupValue(ctx, window));
    JS_SetPropertyStr(ctx, window, "parent", JS_DupValue(ctx, window));
    JS_SetPropertyStr(ctx, document, "defaultView", JS_DupValue(ctx, window));
    
    // ===== Location =====
    JSValue location = JS_NewObject(ctx);
    DEF_PROP_STR(ctx, location, "href", "https://www.youtube.com/watch?v=dQw4w9WgXcQ");
    DEF_PROP_STR(ctx, location, "protocol", "https:");
    DEF_PROP_STR(ctx, location, "host", "www.youtube.com");
    DEF_PROP_STR(ctx, location, "hostname", "www.youtube.com");
    DEF_PROP_STR(ctx, location, "port", "");
    DEF_PROP_STR(ctx, location, "pathname", "/watch");
    DEF_PROP_STR(ctx, location, "search", "?v=dQw4w9WgXcQ");
    DEF_PROP_STR(ctx, location, "hash", "");
    DEF_PROP_STR(ctx, location, "origin", "https://www.youtube.com");
    DEF_FUNC(ctx, location, "toString", js_empty_string, 0);
    JS_SetPropertyStr(ctx, window, "location", location);
    JS_SetPropertyStr(ctx, document, "location", JS_DupValue(ctx, location));
    
    // ===== Navigator =====
    JSValue navigator = JS_NewObject(ctx);
    DEF_PROP_STR(ctx, navigator, "userAgent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    DEF_PROP_STR(ctx, navigator, "appName", "Netscape");
    DEF_PROP_STR(ctx, navigator, "appVersion", "5.0 (X11; Linux x86_64) AppleWebKit/537.36");
    DEF_PROP_STR(ctx, navigator, "appCodeName", "Mozilla");
    DEF_PROP_STR(ctx, navigator, "platform", "Linux x86_64");
    DEF_PROP_STR(ctx, navigator, "product", "Gecko");
    DEF_PROP_STR(ctx, navigator, "productSub", "20030107");
    DEF_PROP_STR(ctx, navigator, "vendor", "Google Inc.");
    DEF_PROP_STR(ctx, navigator, "vendorSub", "");
    DEF_PROP_STR(ctx, navigator, "language", "en-US");
    DEF_PROP_BOOL(ctx, navigator, "onLine", 1);
    DEF_PROP_BOOL(ctx, navigator, "cookieEnabled", 1);
    DEF_PROP_INT(ctx, navigator, "hardwareConcurrency", 8);
    DEF_PROP_INT(ctx, navigator, "maxTouchPoints", 0);
    DEF_PROP_BOOL(ctx, navigator, "pdfViewerEnabled", 1);
    DEF_PROP_BOOL(ctx, navigator, "webdriver", 0);
    JS_SetPropertyStr(ctx, window, "navigator", navigator);
    
    // ===== Screen =====
    JSValue screen = JS_NewObject(ctx);
    DEF_PROP_INT(ctx, screen, "width", 1920);
    DEF_PROP_INT(ctx, screen, "height", 1080);
    DEF_PROP_INT(ctx, screen, "availWidth", 1920);
    DEF_PROP_INT(ctx, screen, "availHeight", 1040);
    DEF_PROP_INT(ctx, screen, "colorDepth", 24);
    DEF_PROP_INT(ctx, screen, "pixelDepth", 24);
    JS_SetPropertyStr(ctx, window, "screen", screen);
    
    // ===== History =====
    JSValue history = JS_NewObject(ctx);
    DEF_PROP_INT(ctx, history, "length", 2);
    DEF_FUNC(ctx, history, "pushState", js_undefined, 3);
    DEF_FUNC(ctx, history, "replaceState", js_undefined, 3);
    DEF_FUNC(ctx, history, "back", js_undefined, 0);
    DEF_FUNC(ctx, history, "forward", js_undefined, 0);
    DEF_FUNC(ctx, history, "go", js_undefined, 1);
    JS_SetPropertyStr(ctx, window, "history", history);
    
    // ===== Storage =====
    JSValue localStorage = JS_NewObject(ctx);
    DEF_FUNC(ctx, localStorage, "getItem", js_null, 1);
    DEF_FUNC(ctx, localStorage, "setItem", js_undefined, 2);
    DEF_FUNC(ctx, localStorage, "removeItem", js_undefined, 1);
    DEF_FUNC(ctx, localStorage, "clear", js_undefined, 0);
    DEF_FUNC(ctx, localStorage, "key", js_null, 1);
    JS_SetPropertyStr(ctx, window, "localStorage", localStorage);
    JS_SetPropertyStr(ctx, window, "sessionStorage", JS_DupValue(ctx, localStorage));
    
    // ===== Console =====
    JSValue console = JS_NewObject(ctx);
    DEF_FUNC(ctx, console, "log", js_console_log, 1);
    DEF_FUNC(ctx, console, "error", js_console_log, 1);
    DEF_FUNC(ctx, console, "warn", js_console_log, 1);
    DEF_FUNC(ctx, console, "info", js_console_log, 1);
    DEF_FUNC(ctx, console, "debug", js_console_log, 1);
    DEF_FUNC(ctx, console, "trace", js_console_log, 1);
    JS_SetPropertyStr(ctx, global, "console", console);
    
    // ===== XMLHttpRequest =====
    JSValue xhr_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, xhr_proto, js_xhr_proto_funcs, js_xhr_proto_funcs_count);
    JS_SetClassProto(ctx, js_xhr_class_id, xhr_proto);
    JSValue xhr_ctor = JS_NewCFunction2(ctx, js_xhr_constructor, "XMLHttpRequest", 
        1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, xhr_ctor, xhr_proto);
    JS_SetPropertyStr(ctx, global, "XMLHttpRequest", xhr_ctor);
    JS_SetPropertyStr(ctx, xhr_ctor, "UNSENT", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, xhr_ctor, "OPENED", JS_NewInt32(ctx, 1));
    JS_SetPropertyStr(ctx, xhr_ctor, "HEADERS_RECEIVED", JS_NewInt32(ctx, 2));
    JS_SetPropertyStr(ctx, xhr_ctor, "LOADING", JS_NewInt32(ctx, 3));
    JS_SetPropertyStr(ctx, xhr_ctor, "DONE", JS_NewInt32(ctx, 4));
    JS_SetPropertyStr(ctx, window, "XMLHttpRequest", JS_DupValue(ctx, xhr_ctor));
    JS_FreeValue(ctx, xhr_proto);
    
    // ===== HTMLVideoElement =====
    JSValue video_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, video_proto, js_video_proto_funcs, js_video_proto_funcs_count);
    JS_SetClassProto(ctx, js_video_class_id, video_proto);
    JSValue video_ctor = JS_NewCFunction2(ctx, js_video_constructor, "HTMLVideoElement",
        1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, video_ctor, video_proto);
    JS_SetPropertyStr(ctx, global, "HTMLVideoElement", video_ctor);
    JS_SetPropertyStr(ctx, video_ctor, "HAVE_NOTHING", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, video_ctor, "HAVE_METADATA", JS_NewInt32(ctx, 1));
    JS_SetPropertyStr(ctx, video_ctor, "HAVE_CURRENT_DATA", JS_NewInt32(ctx, 2));
    JS_SetPropertyStr(ctx, video_ctor, "HAVE_FUTURE_DATA", JS_NewInt32(ctx, 3));
    JS_SetPropertyStr(ctx, video_ctor, "HAVE_ENOUGH_DATA", JS_NewInt32(ctx, 4));
    JS_SetPropertyStr(ctx, video_ctor, "NETWORK_EMPTY", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, video_ctor, "NETWORK_IDLE", JS_NewInt32(ctx, 1));
    JS_SetPropertyStr(ctx, video_ctor, "NETWORK_LOADING", JS_NewInt32(ctx, 2));
    JS_SetPropertyStr(ctx, video_ctor, "NETWORK_NO_SOURCE", JS_NewInt32(ctx, 3));
    JS_SetPropertyStr(ctx, window, "HTMLVideoElement", JS_DupValue(ctx, video_ctor));
    JS_FreeValue(ctx, video_proto);
    
    // ===== fetch API =====
    JS_SetPropertyStr(ctx, global, "fetch", JS_NewCFunction(ctx, js_fetch, "fetch", 2));
    JS_SetPropertyStr(ctx, window, "fetch", JS_NewCFunction(ctx, js_fetch, "fetch", 2));
    
    // ===== DOM Constructors (stubs) =====
    JS_SetPropertyStr(ctx, global, "EventTarget", JS_NewCFunction2(ctx, NULL, "EventTarget", 0, JS_CFUNC_constructor, 0));
    JS_SetPropertyStr(ctx, global, "Node", JS_NewCFunction2(ctx, NULL, "Node", 0, JS_CFUNC_constructor, 0));
    JS_SetPropertyStr(ctx, global, "Element", JS_NewCFunction2(ctx, NULL, "Element", 1, JS_CFUNC_constructor, 0));
    JS_SetPropertyStr(ctx, global, "HTMLElement", JS_NewCFunction2(ctx, NULL, "HTMLElement", 1, JS_CFUNC_constructor, 0));
}
