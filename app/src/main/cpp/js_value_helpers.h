/*
 * JS Value Helpers - Simplified GC-aware JSValue handling
 * 
 * This header provides convenience macros and functions for tracking JSValue
 * references held by C code, preventing garbage collection of values that
 * are still in use.
 * 
 * The shadow stack in quickjs_gc_unified.c handles the actual tracking.
 * This header provides a simpler API for common use cases.
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

/* ============================================================================
 * Basic Push/Pop Macros
 * ============================================================================
 * 
 * Use these to manually track JSValues:
 * 
 *   JSValue val = JS_GetPropertyStr(ctx, obj, "foo");
 *   JS_TRACK(ctx, val);
 *   // ... use val ...
 *   JS_UNTRACK(ctx, val);
 */

#define JS_TRACK(ctx, var) \
    do { \
        gc_push_jsvalue(ctx, &var, __FILE__, __LINE__, #var); \
    } while(0)

#define JS_UNTRACK(ctx, var) \
    do { \
        gc_pop_jsvalue(ctx, &var); \
    } while(0)

/* ============================================================================
 * Scoped Value Helper
 * ============================================================================
 * 
 * JSValue that is automatically tracked and untracked.
 * Must use JS_SCOPE_BEGIN and JS_SCOPE_END around the usage.
 * 
 * Example:
 *   JS_SCOPE_BEGIN(ctx)
 *     JS_SCOPE_VALUE(ctx, body, JS_GetPropertyStr(ctx, doc, "body"));
 *     // body is now tracked and will be untracked at JS_SCOPE_END
 *     JS_SetPropertyStr(ctx, body, "foo", JS_NewString(ctx, "bar"));
 *   JS_SCOPE_END(ctx)
 */

typedef struct JSScope {
    JSContext *ctx;
    JSValue *values[16];  /* Up to 16 tracked values per scope */
    int count;
    const char *file;
    int line;
} JSScope;

static inline void js_scope_init(JSScope *scope, JSContext *ctx, const char *file, int line) {
    scope->ctx = ctx;
    scope->count = 0;
    scope->file = file;
    scope->line = line;
}

static inline void js_scope_track(JSScope *scope, JSValue *val, const char *name) {
    if (scope->count < 16) {
        scope->values[scope->count++] = val;
        gc_push_jsvalue(scope->ctx, val);
    } else {
        LOG_WARN("JS scope full, cannot track %s", name);
    }
}

static inline void js_scope_cleanup(JSScope *scope) {
    for (int i = scope->count - 1; i >= 0; i--) {
        gc_pop_jsvalue(scope->ctx, scope->values[i]);
    }
    scope->count = 0;
}

/* Scope macros */
#define JS_SCOPE_BEGIN(ctx) \
    do { \
        JSScope _js_scope; \
        js_scope_init(&_js_scope, ctx, __FILE__, __LINE__); \
        do {

#define JS_SCOPE_VALUE(ctx, name, init) \
    JSValue name = (init); \
    js_scope_track(&_js_scope, &name, #name)

#define JS_SCOPE_END(ctx) \
        } while(0); \
        js_scope_cleanup(&_js_scope); \
    } while(0)

/* ============================================================================
 * Helper Functions for Common Patterns
 * ============================================================================
 */

/* Get property and auto-track */
static inline JSValue js_get_prop_tracked(JSContext *ctx, JSValueConst obj, 
                                           const char *prop, const char *file, int line) {
    JSValue val = JS_GetPropertyStr(ctx, obj, prop);
    gc_push_jsvalue(ctx, &val);
    return val;
}
#define JS_GET_PROP(ctx, obj, prop) \
    js_get_prop_tracked(ctx, obj, prop, __FILE__, __LINE__)

/* Get global object and auto-track */
static inline JSValue js_get_global_tracked(JSContext *ctx, const char *file, int line) {
    JSValue val = JS_GetGlobalObject(ctx);
    gc_push_jsvalue(ctx, &val);
    return val;
}
#define JS_GET_GLOBAL(ctx) \
    js_get_global_tracked(ctx, __FILE__, __LINE__)

/* Call function with tracked arguments */
static inline JSValue js_call_tracked(JSContext *ctx, JSValueConst func, 
                                       JSValueConst this_val, int argc, 
                                       JSValueConst *argv,
                                       const char *file, int line) {
    JSValue result = JS_Call(ctx, func, this_val, argc, argv);
    gc_push_jsvalue(ctx, &result);
    return result;
}
#define JS_CALL_TRACKED(ctx, func, this_val, argc, argv) \
    js_call_tracked(ctx, func, this_val, argc, argv, __FILE__, __LINE__)

#ifdef __cplusplus
}
#endif

#endif /* JS_VALUE_HELPERS_H */
