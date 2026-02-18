/*
 * GC Value Helpers - Helper utilities for working with GCValue
 * 
 * CRITICAL RULE: Never store the result of gc_deref(). Always wrap
 * pointers back into a GCValue immediately using GC_WRAP_PTR().
 */

#ifndef GC_VALUE_HELPERS_H
#define GC_VALUE_HELPERS_H

#include "third_party/quickjs/quickjs.h"
#include "third_party/quickjs/quickjs_gc_unified.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Check if GCValue represents a valid object */
static inline int gc_is_valid_object(GCValue v) {
    return GC_IS_OBJECT(v) && v.u.handle != GC_HANDLE_NULL;
}

/* Check if GCValue is a valid reference type */
static inline int gc_is_valid_reference(GCValue v) {
    return GC_IS_REFERENCE(v) && v.u.handle != GC_HANDLE_NULL;
}

/* Safe property getter with null check */
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

#ifdef __cplusplus
}
#endif

#endif /* GC_VALUE_HELPERS_H */
