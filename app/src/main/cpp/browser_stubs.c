/*
 * Browser Stubs - C implementation of DOM/Browser APIs for QuickJS
 */
#include <string.h>
#include <stdlib.h>
#include <quickjs.h>
#include "browser_stubs.h"

// External functions and variables from js_quickjs.c
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

// ===== Helper Macros =====
#define STUB_DEF_FUNC(ctx, parent, name, func, argc) \
    JS_SetPropertyStr(ctx, parent, name, JS_NewCFunction(ctx, func, name, argc))

#define STUB_DEF_PROP_STR(ctx, obj, name, value) \
    JS_SetPropertyStr(ctx, obj, name, JS_NewString(ctx, value))

#define STUB_DEF_PROP_INT(ctx, obj, name, value) \
    JS_SetPropertyStr(ctx, obj, name, JS_NewInt32(ctx, value))

#define STUB_DEF_PROP_BOOL(ctx, obj, name, value) \
    JS_SetPropertyStr(ctx, obj, name, JS_NewBool(ctx, value))

// Workaround for missing JS_DeletePropertyStr
static inline int JS_DeletePropertyStr(JSContext *ctx, JSValueConst obj, const char *prop, int flags) {
    JSAtom atom = JS_NewAtom(ctx, prop);
    int ret = JS_DeleteProperty(ctx, obj, atom, flags);
    JS_FreeAtom(ctx, atom);
    return ret;
}

#define STUB_DEF_PROP_FLOAT(ctx, obj, name, value) \
    JS_SetPropertyStr(ctx, obj, name, JS_NewFloat64(ctx, value))

// ===== Stub Functions =====
static JSValue js_stub_return_undefined(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

static JSValue js_stub_return_null(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NULL;
}

static JSValue js_stub_return_empty_array(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NewArray(ctx);
}

static JSValue js_stub_return_empty_string(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NewString(ctx, "");
}

static JSValue js_stub_return_false(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_FALSE;
}

static JSValue js_stub_return_true(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_TRUE;
}

static JSValue js_stub_return_zero(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NewInt32(ctx, 0);
}

static JSValue js_stub_return_this(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_DupValue(ctx, this_val);
}

// ===== EventTarget Implementation =====
static JSValue js_eventtarget_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    JSValue obj = JS_NewObject(ctx);
    JSValue listeners = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "_listeners", listeners);
    return obj;
}

static JSValue js_eventtarget_addEventListener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 2) return JS_UNDEFINED;
    const char *type = JS_ToCString(ctx, argv[0]);
    if (!type) return JS_UNDEFINED;
    
    JSValue listeners = JS_GetPropertyStr(ctx, this_val, "_listeners");
    JSValue arr = JS_GetPropertyStr(ctx, listeners, type);
    if (JS_IsUndefined(arr)) {
        arr = JS_NewArray(ctx);
        JS_SetPropertyStr(ctx, listeners, type, arr);
    }
    
    int32_t len = 0;
    JSValue len_val = JS_GetPropertyStr(ctx, arr, "length");
    JS_ToInt32(ctx, &len, len_val);
    JS_FreeValue(ctx, len_val);
    JS_SetPropertyUint32(ctx, arr, len, JS_DupValue(ctx, argv[1]));
    
    JS_FreeCString(ctx, type);
    JS_FreeValue(ctx, arr);
    JS_FreeValue(ctx, listeners);
    return JS_UNDEFINED;
}

static JSValue js_eventtarget_removeEventListener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

static JSValue js_eventtarget_dispatchEvent(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_TRUE;
}

// ===== Node Implementation =====
static JSValue js_node_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    JSValue obj = JS_NewObject(ctx);
    JSValue children = JS_NewArray(ctx);
    JS_SetPropertyStr(ctx, obj, "childNodes", children);
    JS_SetPropertyStr(ctx, obj, "parentNode", JS_NULL);
    STUB_DEF_PROP_INT(ctx, obj, "nodeType", 1);
    return obj;
}

static JSValue js_node_appendChild(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_NULL;
    JSValue children = JS_GetPropertyStr(ctx, this_val, "childNodes");
    int32_t len = 0;
    JSValue len_val = JS_GetPropertyStr(ctx, children, "length");
    JS_ToInt32(ctx, &len, len_val);
    JS_FreeValue(ctx, len_val);
    JS_SetPropertyUint32(ctx, children, len, JS_DupValue(ctx, argv[0]));
    JS_SetPropertyStr(ctx, argv[0], "parentNode", JS_DupValue(ctx, this_val));
    JS_FreeValue(ctx, children);
    return JS_DupValue(ctx, argv[0]);
}

static JSValue js_node_removeChild(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NULL;
}

// ===== Element Implementation =====
static JSValue js_element_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    JSValue obj = JS_NewObject(ctx);
    JSValue attrs = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "attributes", attrs);
    JS_SetPropertyStr(ctx, obj, "className", JS_NewString(ctx, ""));
    JS_SetPropertyStr(ctx, obj, "id", JS_NewString(ctx, ""));
    
    const char *tag = argc > 0 ? JS_ToCString(ctx, argv[0]) : "";
    JS_SetPropertyStr(ctx, obj, "tagName", JS_NewString(ctx, tag ? tag : ""));
    JS_SetPropertyStr(ctx, obj, "nodeName", JS_NewString(ctx, tag ? tag : ""));
    JS_FreeCString(ctx, tag);
    
    JS_FreeValue(ctx, attrs);
    return obj;
}

// ===== HTMLElement Implementation =====
static JSValue js_htmlelement_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    JSValue obj = JS_NewObject(ctx);
    return obj;
}

// ===== HTMLScriptElement Implementation =====
static JSValue js_htmlscriptelement_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "src", JS_NewString(ctx, ""));
    JS_SetPropertyStr(ctx, obj, "type", JS_NewString(ctx, ""));
    JS_SetPropertyStr(ctx, obj, "async", JS_FALSE);
    JS_SetPropertyStr(ctx, obj, "defer", JS_FALSE);
    JS_SetPropertyStr(ctx, obj, "crossOrigin", JS_NULL);
    return obj;
}

// ===== Storage Implementation =====
static JSValue js_storage_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    JSValue obj = JS_NewObject(ctx);
    JSValue data = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "_data", data);
    JS_FreeValue(ctx, data);
    return obj;
}

static JSValue js_storage_getItem(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_NULL;
    const char *key = JS_ToCString(ctx, argv[0]);
    if (!key) return JS_NULL;
    JSValue data = JS_GetPropertyStr(ctx, this_val, "_data");
    JSValue val = JS_GetPropertyStr(ctx, data, key);
    JS_FreeCString(ctx, key);
    JS_FreeValue(ctx, data);
    if (JS_IsUndefined(val)) return JS_NULL;
    return val;
}

static JSValue js_storage_setItem(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 2) return JS_UNDEFINED;
    const char *key = JS_ToCString(ctx, argv[0]);
    const char *val = JS_ToCString(ctx, argv[1]);
    if (key && val) {
        JSValue data = JS_GetPropertyStr(ctx, this_val, "_data");
        JS_SetPropertyStr(ctx, data, key, JS_NewString(ctx, val));
        JS_FreeValue(ctx, data);
    }
    JS_FreeCString(ctx, key);
    JS_FreeCString(ctx, val);
    return JS_UNDEFINED;
}

static JSValue js_storage_removeItem(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_UNDEFINED;
    const char *key = JS_ToCString(ctx, argv[0]);
    if (key) {
        JSValue data = JS_GetPropertyStr(ctx, this_val, "_data");
        JS_DeletePropertyStr(ctx, data, key, 0);
        JS_FreeValue(ctx, data);
    }
    JS_FreeCString(ctx, key);
    return JS_UNDEFINED;
}

static JSValue js_storage_clear(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JSValue data = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, this_val, "_data", data);
    JS_FreeValue(ctx, data);
    return JS_UNDEFINED;
}

static JSValue js_storage_key(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NULL;
}

// ===== Console Implementation =====
static JSValue js_stub_console_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

// ===== Main Initialization Function =====
void init_browser_stubs(JSContext *ctx, JSValue global) {
    // ===== 1. EventTarget =====
    JSValue eventtarget_ctor = JS_NewCFunction2(ctx, js_eventtarget_constructor, "EventTarget", 0, JS_CFUNC_constructor, 0);
    JSValue eventtarget_proto = JS_NewObject(ctx);
    STUB_DEF_FUNC(ctx, eventtarget_proto, "addEventListener", js_eventtarget_addEventListener, 2);
    STUB_DEF_FUNC(ctx, eventtarget_proto, "removeEventListener", js_eventtarget_removeEventListener, 2);
    STUB_DEF_FUNC(ctx, eventtarget_proto, "dispatchEvent", js_eventtarget_dispatchEvent, 1);
    JS_SetConstructor(ctx, eventtarget_ctor, eventtarget_proto);
    JS_SetPropertyStr(ctx, global, "EventTarget", eventtarget_ctor);
    
    // ===== 2. Node =====
    JSValue node_ctor = JS_NewCFunction2(ctx, js_node_constructor, "Node", 0, JS_CFUNC_constructor, 0);
    JSValue node_proto = JS_NewObject(ctx);
    JS_SetPrototype(ctx, node_proto, eventtarget_proto);
    STUB_DEF_PROP_INT(ctx, node_proto, "ELEMENT_NODE", 1);
    STUB_DEF_PROP_INT(ctx, node_proto, "TEXT_NODE", 3);
    STUB_DEF_PROP_INT(ctx, node_proto, "COMMENT_NODE", 8);
    STUB_DEF_PROP_INT(ctx, node_proto, "DOCUMENT_NODE", 9);
    STUB_DEF_FUNC(ctx, node_proto, "appendChild", js_node_appendChild, 1);
    STUB_DEF_FUNC(ctx, node_proto, "removeChild", js_node_removeChild, 1);
    JS_SetConstructor(ctx, node_ctor, node_proto);
    JS_SetPropertyStr(ctx, global, "Node", node_ctor);
    
    // ===== 3. Element =====
    JSValue element_ctor = JS_NewCFunction2(ctx, js_element_constructor, "Element", 1, JS_CFUNC_constructor, 0);
    JSValue element_proto = JS_NewObject(ctx);
    JS_SetPrototype(ctx, element_proto, node_proto);
    STUB_DEF_FUNC(ctx, element_proto, "getAttribute", js_stub_return_null, 1);
    STUB_DEF_FUNC(ctx, element_proto, "setAttribute", js_stub_return_undefined, 2);
    STUB_DEF_FUNC(ctx, element_proto, "removeAttribute", js_stub_return_undefined, 1);
    STUB_DEF_FUNC(ctx, element_proto, "hasAttribute", js_stub_return_false, 1);
    STUB_DEF_FUNC(ctx, element_proto, "querySelector", js_stub_return_null, 1);
    STUB_DEF_FUNC(ctx, element_proto, "querySelectorAll", js_stub_return_empty_array, 1);
    STUB_DEF_FUNC(ctx, element_proto, "getElementsByTagName", js_stub_return_empty_array, 1);
    STUB_DEF_FUNC(ctx, element_proto, "getElementsByClassName", js_stub_return_empty_array, 1);
    STUB_DEF_FUNC(ctx, element_proto, "matches", js_stub_return_false, 1);
    STUB_DEF_FUNC(ctx, element_proto, "closest", js_stub_return_null, 1);
    JS_SetConstructor(ctx, element_ctor, element_proto);
    JS_SetPropertyStr(ctx, global, "Element", element_ctor);
    
    // ===== 4. HTMLElement =====
    JSValue htmlelement_ctor = JS_NewCFunction2(ctx, js_htmlelement_constructor, "HTMLElement", 1, JS_CFUNC_constructor, 0);
    JSValue htmlelement_proto = JS_NewObject(ctx);
    JS_SetPrototype(ctx, htmlelement_proto, element_proto);
    JS_SetConstructor(ctx, htmlelement_ctor, htmlelement_proto);
    JS_SetPropertyStr(ctx, global, "HTMLElement", htmlelement_ctor);
    
    // ===== 5. HTMLScriptElement =====
    JSValue script_ctor = JS_NewCFunction2(ctx, js_htmlscriptelement_constructor, "HTMLScriptElement", 0, JS_CFUNC_constructor, 0);
    JSValue script_proto = JS_NewObject(ctx);
    JS_SetPrototype(ctx, script_proto, htmlelement_proto);
    JS_SetConstructor(ctx, script_ctor, script_proto);
    JS_SetPropertyStr(ctx, global, "HTMLScriptElement", script_ctor);
    
    // ===== 6. Document =====
    JSValue document = JS_NewObject(ctx);
    JS_SetPrototype(ctx, document, eventtarget_proto);
    STUB_DEF_PROP_INT(ctx, document, "nodeType", 9);
    STUB_DEF_PROP_STR(ctx, document, "readyState", "complete");
    STUB_DEF_PROP_STR(ctx, document, "characterSet", "UTF-8");
    STUB_DEF_PROP_STR(ctx, document, "charset", "UTF-8");
    STUB_DEF_PROP_STR(ctx, document, "contentType", "text/html");
    STUB_DEF_PROP_STR(ctx, document, "referrer", "https://www.youtube.com/");
    STUB_DEF_PROP_STR(ctx, document, "cookie", "");
    STUB_DEF_PROP_STR(ctx, document, "domain", "www.youtube.com");
    STUB_DEF_FUNC(ctx, document, "createElement", js_document_create_element, 1);
    STUB_DEF_FUNC(ctx, document, "createElementNS", js_document_create_element, 2);
    STUB_DEF_FUNC(ctx, document, "createTextNode", js_stub_return_empty_string, 1);
    STUB_DEF_FUNC(ctx, document, "createComment", js_stub_return_empty_string, 1);
    STUB_DEF_FUNC(ctx, document, "createDocumentFragment", js_stub_return_null, 0);
    STUB_DEF_FUNC(ctx, document, "getElementById", js_stub_return_null, 1);
    STUB_DEF_FUNC(ctx, document, "querySelector", js_stub_return_null, 1);
    STUB_DEF_FUNC(ctx, document, "querySelectorAll", js_stub_return_empty_array, 1);
    STUB_DEF_FUNC(ctx, document, "getElementsByTagName", js_stub_return_empty_array, 1);
    STUB_DEF_FUNC(ctx, document, "getElementsByClassName", js_stub_return_empty_array, 1);
    STUB_DEF_FUNC(ctx, document, "getElementsByName", js_stub_return_empty_array, 1);
    JS_SetPropertyStr(ctx, global, "document", document);
    
    // ===== 7. Window =====
    JSValue window = JS_NewObject(ctx);
    JS_SetPrototype(ctx, window, eventtarget_proto);
    STUB_DEF_PROP_INT(ctx, window, "innerWidth", 1920);
    STUB_DEF_PROP_INT(ctx, window, "innerHeight", 1080);
    STUB_DEF_PROP_INT(ctx, window, "outerWidth", 1920);
    STUB_DEF_PROP_INT(ctx, window, "outerHeight", 1080);
    STUB_DEF_PROP_INT(ctx, window, "screenX", 0);
    STUB_DEF_PROP_INT(ctx, window, "screenY", 0);
    STUB_DEF_PROP_FLOAT(ctx, window, "devicePixelRatio", 1.0);
    STUB_DEF_FUNC(ctx, window, "setTimeout", js_stub_return_zero, 2);
    STUB_DEF_FUNC(ctx, window, "setInterval", js_stub_return_zero, 2);
    STUB_DEF_FUNC(ctx, window, "clearTimeout", js_stub_return_undefined, 1);
    STUB_DEF_FUNC(ctx, window, "clearInterval", js_stub_return_undefined, 1);
    STUB_DEF_FUNC(ctx, window, "requestAnimationFrame", js_stub_return_zero, 1);
    STUB_DEF_FUNC(ctx, window, "cancelAnimationFrame", js_stub_return_undefined, 1);
    STUB_DEF_FUNC(ctx, window, "alert", js_stub_return_undefined, 1);
    STUB_DEF_FUNC(ctx, window, "confirm", js_stub_return_true, 0);
    STUB_DEF_FUNC(ctx, window, "prompt", js_stub_return_empty_string, 1);
    STUB_DEF_FUNC(ctx, window, "open", js_stub_return_null, 1);
    STUB_DEF_FUNC(ctx, window, "close", js_stub_return_undefined, 0);
    STUB_DEF_FUNC(ctx, window, "focus", js_stub_return_undefined, 0);
    STUB_DEF_FUNC(ctx, window, "blur", js_stub_return_undefined, 0);
    STUB_DEF_FUNC(ctx, window, "scrollTo", js_stub_return_undefined, 2);
    STUB_DEF_FUNC(ctx, window, "scrollBy", js_stub_return_undefined, 2);
    STUB_DEF_FUNC(ctx, window, "postMessage", js_stub_return_undefined, 2);
    JS_SetPropertyStr(ctx, global, "window", window);
    JS_SetPropertyStr(ctx, window, "window", JS_DupValue(ctx, window));
    JS_SetPropertyStr(ctx, window, "self", JS_DupValue(ctx, window));
    JS_SetPropertyStr(ctx, window, "top", JS_DupValue(ctx, window));
    JS_SetPropertyStr(ctx, window, "parent", JS_DupValue(ctx, window));
    JS_SetPropertyStr(ctx, document, "defaultView", JS_DupValue(ctx, window));
    
    // ===== 8. Location =====
    JSValue location = JS_NewObject(ctx);
    STUB_DEF_PROP_STR(ctx, location, "href", "https://www.youtube.com/watch?v=dQw4w9WgXcQ");
    STUB_DEF_PROP_STR(ctx, location, "protocol", "https:");
    STUB_DEF_PROP_STR(ctx, location, "host", "www.youtube.com");
    STUB_DEF_PROP_STR(ctx, location, "hostname", "www.youtube.com");
    STUB_DEF_PROP_STR(ctx, location, "port", "");
    STUB_DEF_PROP_STR(ctx, location, "pathname", "/watch");
    STUB_DEF_PROP_STR(ctx, location, "search", "?v=dQw4w9WgXcQ");
    STUB_DEF_PROP_STR(ctx, location, "hash", "");
    STUB_DEF_PROP_STR(ctx, location, "origin", "https://www.youtube.com");
    STUB_DEF_FUNC(ctx, location, "toString", js_stub_return_empty_string, 0);
    JS_SetPropertyStr(ctx, window, "location", location);
    JS_SetPropertyStr(ctx, document, "location", JS_DupValue(ctx, location));
    
    // ===== 9. Navigator =====
    JSValue navigator = JS_NewObject(ctx);
    STUB_DEF_PROP_STR(ctx, navigator, "userAgent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    STUB_DEF_PROP_STR(ctx, navigator, "appName", "Netscape");
    STUB_DEF_PROP_STR(ctx, navigator, "appVersion", "5.0 (X11; Linux x86_64) AppleWebKit/537.36");
    STUB_DEF_PROP_STR(ctx, navigator, "appCodeName", "Mozilla");
    STUB_DEF_PROP_STR(ctx, navigator, "platform", "Linux x86_64");
    STUB_DEF_PROP_STR(ctx, navigator, "product", "Gecko");
    STUB_DEF_PROP_STR(ctx, navigator, "productSub", "20030107");
    STUB_DEF_PROP_STR(ctx, navigator, "vendor", "Google Inc.");
    STUB_DEF_PROP_STR(ctx, navigator, "vendorSub", "");
    STUB_DEF_PROP_STR(ctx, navigator, "language", "en-US");
    STUB_DEF_PROP_BOOL(ctx, navigator, "onLine", 1);
    STUB_DEF_PROP_BOOL(ctx, navigator, "cookieEnabled", 1);
    STUB_DEF_PROP_INT(ctx, navigator, "hardwareConcurrency", 8);
    STUB_DEF_PROP_INT(ctx, navigator, "maxTouchPoints", 0);
    STUB_DEF_PROP_BOOL(ctx, navigator, "pdfViewerEnabled", 1);
    STUB_DEF_PROP_BOOL(ctx, navigator, "webdriver", 0);
    JS_SetPropertyStr(ctx, window, "navigator", navigator);
    
    // ===== 10. Screen =====
    JSValue screen = JS_NewObject(ctx);
    STUB_DEF_PROP_INT(ctx, screen, "width", 1920);
    STUB_DEF_PROP_INT(ctx, screen, "height", 1080);
    STUB_DEF_PROP_INT(ctx, screen, "availWidth", 1920);
    STUB_DEF_PROP_INT(ctx, screen, "availHeight", 1040);
    STUB_DEF_PROP_INT(ctx, screen, "colorDepth", 24);
    STUB_DEF_PROP_INT(ctx, screen, "pixelDepth", 24);
    JS_SetPropertyStr(ctx, window, "screen", screen);
    
    // ===== 11. History =====
    JSValue history = JS_NewObject(ctx);
    STUB_DEF_PROP_INT(ctx, history, "length", 2);
    STUB_DEF_FUNC(ctx, history, "pushState", js_stub_return_undefined, 3);
    STUB_DEF_FUNC(ctx, history, "replaceState", js_stub_return_undefined, 3);
    STUB_DEF_FUNC(ctx, history, "back", js_stub_return_undefined, 0);
    STUB_DEF_FUNC(ctx, history, "forward", js_stub_return_undefined, 0);
    STUB_DEF_FUNC(ctx, history, "go", js_stub_return_undefined, 1);
    JS_SetPropertyStr(ctx, window, "history", history);
    
    // ===== 12. Storage =====
    JSValue storage_proto = JS_NewObject(ctx);
    STUB_DEF_FUNC(ctx, storage_proto, "getItem", js_storage_getItem, 1);
    STUB_DEF_FUNC(ctx, storage_proto, "setItem", js_storage_setItem, 2);
    STUB_DEF_FUNC(ctx, storage_proto, "removeItem", js_storage_removeItem, 1);
    STUB_DEF_FUNC(ctx, storage_proto, "clear", js_storage_clear, 0);
    STUB_DEF_FUNC(ctx, storage_proto, "key", js_storage_key, 1);
    JSValue storage_ctor = JS_NewCFunction2(ctx, js_storage_constructor, "Storage", 0, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, storage_ctor, storage_proto);
    JSValue localStorage = JS_CallConstructor(ctx, storage_ctor, 0, NULL);
    JSValue sessionStorage = JS_CallConstructor(ctx, storage_ctor, 0, NULL);
    JS_SetPropertyStr(ctx, window, "localStorage", localStorage);
    JS_SetPropertyStr(ctx, window, "sessionStorage", sessionStorage);
    
    // ===== 13. Console =====
    JSValue console = JS_NewObject(ctx);
    STUB_DEF_FUNC(ctx, console, "log", js_stub_console_log, 1);
    STUB_DEF_FUNC(ctx, console, "error", js_stub_console_log, 1);
    STUB_DEF_FUNC(ctx, console, "warn", js_stub_console_log, 1);
    STUB_DEF_FUNC(ctx, console, "info", js_stub_console_log, 1);
    STUB_DEF_FUNC(ctx, console, "debug", js_stub_console_log, 1);
    STUB_DEF_FUNC(ctx, console, "trace", js_stub_console_log, 1);
    JS_SetPropertyStr(ctx, global, "console", console);
    
    // ===== 14. Native Classes (XMLHttpRequest, HTMLVideoElement) =====
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
    
    // fetch API
    JS_SetPropertyStr(ctx, global, "fetch", JS_NewCFunction(ctx, js_fetch, "fetch", 2));
    
    // Update window references to native classes
    JSValue window2 = JS_GetPropertyStr(ctx, global, "window");
    JS_SetPropertyStr(ctx, window2, "XMLHttpRequest", JS_DupValue(ctx, xhr_ctor));
    JS_SetPropertyStr(ctx, window2, "HTMLVideoElement", JS_DupValue(ctx, video_ctor));
    JS_FreeValue(ctx, window2);
}
