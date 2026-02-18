/*
 * GC Value Helpers - Helper utilities for working with GCValue
 * 
 * This header provides convenience functions for working with GCVValues.
 * All memory is managed by the GC - no manual tracking needed.
 * 
 * CRITICAL RULE: Never store the result of gc_deref(). Always use the
 * GC_PROP_* macros or immediately wrap the pointer back into a GCValue
 * using GC_WRAP_PTR().
 */

#ifndef GC_VALUE_HELPERS_H
#define GC_VALUE_HELPERS_H

#include "third_party/quickjs/quickjs.h"
#include "third_party/quickjs/quickjs_gc_unified.h"
#include <android/log.h>

#define LOG_TAG "GCValueHelpers"
#define LOG_INFO(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOG_WARN(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * GCValue Safety Rules
 * ============================================================================
 * 
 * 1. NEVER store raw pointers obtained from gc_deref():
 *      // WRONG - pointer becomes invalid after GC
 *      void *ptr = gc_deref(obj.u.handle);
 *      // ... GC might run here ...
 *      JS_SetPropertyStr(ctx, make_value(ptr), "x", val);
 * 
 * 2. ALWAYS use GC_PROP_* macros for property access:
 *      // CORRECT - pointer used immediately
 *      GC_PROP_SET_STR(ctx, obj, "x", val);
 * 
 * 3. If you need to call a function taking GCValue, use GC_WRAP_PTR:
 *      // CORRECT - pointer wrapped immediately
 *      void *ptr = gc_deref(handle);
 *      GCValue val = GC_WRAP_PTR(tag, ptr);
 *      JS_SomeFunction(ctx, val);
 * 
 * 4. Keep GCValue (which contains the stable handle), not pointer:
 *      // WRONG
 *      void *obj = create_object();
 *      // store obj somewhere
 *      
 *      // CORRECT
 *      GCValue obj = GC_WRAP_PTR(JS_TAG_OBJECT, create_object());
 *      // store obj somewhere - handle remains valid across GC
 */

/*
 * ============================================================================
 * Convenience Functions
 * ============================================================================
 */

/* Check if GCValue represents a valid object (has a non-null handle) */
static inline int gc_is_valid_object(GCValue v) {
    return GC_IS_OBJECT(v) && v.u.handle != GC_HANDLE_NULL;
}

/* Check if GCValue is a valid reference type with non-null handle */
static inline int gc_is_valid_reference(GCValue v) {
    return GC_IS_REFERENCE(v) && v.u.handle != GC_HANDLE_NULL;
}

/* Safe property getter with null check - returns GC_UNDEFINED if invalid */
static inline GCValue gc_get_prop_str_safe(JSContext *ctx, GCValue obj, const char *prop) {
    if (!gc_is_valid_reference(obj)) {
        return GC_UNDEFINED;
    }
    return GC_PROP_GET_STR(ctx, obj, prop);
}

/* Safe property setter with null check */
static inline int gc_set_prop_str_safe(JSContext *ctx, GCValue obj, const char *prop, GCValue val) {
    if (!gc_is_valid_reference(obj)) {
        return -1;
    }
    return GC_PROP_SET_STR(ctx, obj, prop, val);
}

/*
 * ============================================================================
 * Type Constructors
 * ============================================================================
 */

/* Create a new GCValue from a string */
static inline GCValue gc_new_string(JSContext *ctx, const char *str) {
    return JS_NewString(ctx, str);
}

/* Create a new GCValue integer */
static inline GCValue gc_new_int32(int32_t n) {
    return GC_NewInt32(n);
}

/* Create a new GCValue boolean */
static inline GCValue gc_new_bool(JS_BOOL b) {
    return GC_NewBool(b);
}

/* Create a new GCValue float */
static inline GCValue gc_new_float64(double d) {
    return GC_NewFloat64(d);
}

/*
 * ============================================================================
 * Object Creation Helpers
 * ============================================================================
 */

/* Create a new empty object */
static inline GCValue gc_new_object(JSContext *ctx) {
    return JS_NewObject(ctx);
}

/* Create a new array */
static inline GCValue gc_new_array(JSContext *ctx) {
    return JS_NewArray(ctx);
}

/*
 * ============================================================================
 * Global Object Access
 * ============================================================================
 */

/* Get global object as GCValue */
static inline GCValue gc_get_global_object(JSContext *ctx) {
    return JS_GetGlobalObject(ctx);
}

/*
 * ============================================================================
 * Function Calling Helpers
 * ============================================================================
 */

/* 
 * Call a function stored in a GCValue property.
 * This helper dereferences handles safely without storing pointers.
 */
#define GC_CALL_METHOD(ctx, obj, method_name, argc, argv) ({ \
    GCValue _gc_method = GC_PROP_GET_STR((ctx), (obj), (method_name)); \
    GCValue _gc_result = GC_UNDEFINED; \
    if (!GC_IS_UNDEFINED(_gc_method)) { \
        int _gc_tag = GC_VALUE_GET_TAG(obj); \
        if (_gc_tag < 0) { \
            void *_gc_ptr = gc_deref_internal((obj).u.handle); \
            if (_gc_ptr != NULL) { \
                GCValue _gc_this = GC_WRAP_PTR(_gc_tag, _gc_ptr); \
                _gc_result = JS_Call((ctx), _gc_method, _gc_this, (argc), (argv)); \
            } \
        } \
    } \
    _gc_result; \
})

#ifdef __cplusplus
}
#endif

#endif /* GC_VALUE_HELPERS_H */
