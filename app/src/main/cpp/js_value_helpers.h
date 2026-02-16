/*
 * JS Value Helpers - Simplified GC-aware JSValue handling
 * 
 * This header provides convenience functions for working with JSValues.
 * The GC automatically manages memory - no manual tracking needed.
 */

#ifndef JS_VALUE_HELPERS_H
#define JS_VALUE_HELPERS_H

#include "third_party/quickjs/quickjs.h"
#include "third_party/quickjs/quickjs_gc_unified.h"
#include <android/log.h>

#define LOG_TAG "JSHelpers"
#define LOG_INFO(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOG_WARN(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Note: The GC automatically manages all memory. JSValues that are
 * reachable from the JS runtime or from C local variables are safe.
 * When you need to keep a JSValue alive across calls, store it in
 * a JSObject property or use gc_add_root() on its handle.
 */

/*
 * Helper Functions for Common Patterns
 */

/* Get property as a new JSValue (caller should use JS_FreeValue when done) */
static inline JSValue js_get_prop(JSContext *ctx, JSValueConst obj, const char *prop) {
    return JS_GetPropertyStr(ctx, obj, prop);
}

/* Get global object (caller should use JS_FreeValue when done) */
static inline JSValue js_get_global(JSContext *ctx) {
    return JS_GetGlobalObject(ctx);
}

/* Call function with result (caller should use JS_FreeValue on result when done) */
static inline JSValue js_call(JSContext *ctx, JSValueConst func, 
                               JSValueConst this_val, int argc, 
                               JSValueConst *argv) {
    return JS_Call(ctx, func, this_val, argc, argv);
}

#ifdef __cplusplus
}
#endif

#endif /* JS_VALUE_HELPERS_H */
