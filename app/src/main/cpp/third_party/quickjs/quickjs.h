/*
 * QuickJS Javascript Engine
 *
 * Copyright (c) 2017-2021 Fabrice Bellard
 * Copyright (c) 2017-2021 Charlie Gordon
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef QUICKJS_H
#define QUICKJS_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Include unified GC for allocation functions */
#include "quickjs_gc_unified.h"

/* Handle type - alias for GCHandle for backward compatibility */
typedef GCHandle JSGCHandle;
#define JS_GC_HANDLE_NULL GC_HANDLE_NULL

#if defined(__GNUC__) || defined(__clang__)
#define js_likely(x)          __builtin_expect(!!(x), 1)
#define js_unlikely(x)        __builtin_expect(!!(x), 0)
#define js_force_inline       inline __attribute__((always_inline))
#define __js_printf_like(f, a)   __attribute__((format(printf, f, a)))
#else
#define js_likely(x)     (x)
#define js_unlikely(x)   (x)
#define js_force_inline  inline
#define __js_printf_like(a, b)
#endif

#define JS_BOOL int

typedef struct JSRuntime JSRuntime;
typedef struct JSContext JSContext;
typedef struct JSClass JSClass;
typedef uint32_t JSClassID;
typedef uint32_t JSAtom;

#if INTPTR_MAX >= INT64_MAX
#define JS_PTR64
#define JS_PTR64_DEF(a) a
#else
#define JS_PTR64_DEF(a)
#endif

#ifndef JS_PTR64
#define JS_NAN_BOXING
#endif

#if defined(__SIZEOF_INT128__) && (INTPTR_MAX >= INT64_MAX)
#define JS_LIMB_BITS 64
#else
#define JS_LIMB_BITS 32
#endif

#define JS_SHORT_BIG_INT_BITS JS_LIMB_BITS
    
enum {
    /* all tags with a reference count are negative */
    JS_TAG_FIRST       = -9, /* first negative tag */
    JS_TAG_BIG_INT     = -9,
    JS_TAG_SYMBOL      = -8,
    JS_TAG_STRING      = -7,
    JS_TAG_STRING_ROPE = -6,
    JS_TAG_MODULE      = -3, /* used internally */
    JS_TAG_FUNCTION_BYTECODE = -2, /* used internally */
    JS_TAG_OBJECT      = -1,

    JS_TAG_INT         = 0,
    JS_TAG_BOOL        = 1,
    JS_TAG_NULL        = 2,
    JS_TAG_UNDEFINED   = 3,
    JS_TAG_UNINITIALIZED = 4,
    JS_TAG_CATCH_OFFSET = 5,
    JS_TAG_EXCEPTION   = 6,
    JS_TAG_SHORT_BIG_INT = 7,
    JS_TAG_FLOAT64     = 8,
    /* any larger tag is FLOAT64 if JS_NAN_BOXING */
};

/* Note: GCHeader is defined in quickjs_gc_unified.h */
struct GCHeader;

#define JS_FLOAT64_NAN NAN

#ifdef CONFIG_CHECK_JSVALUE
/* GCValue consistency checking mode */
typedef struct __GCValue *GCValue;
typedef const struct __GCValue *GCValueConst;

#define GC_VALUE_GET_TAG(v) (int)((uintptr_t)(v) & 0xf)
#define GC_VALUE_GET_NORM_TAG(v) GC_VALUE_GET_TAG(v)
#define GC_VALUE_GET_INT(v) (int)((intptr_t)(v) >> 4)
#define GC_VALUE_GET_BOOL(v) GC_VALUE_GET_INT(v)
#define GC_VALUE_GET_FLOAT64(v) (double)GC_VALUE_GET_INT(v)
#define GC_VALUE_GET_SHORT_BIG_INT(v) GC_VALUE_GET_INT(v)

/* GC_VALUE_GET_PTR for CONFIG_CHECK_JSVALUE - needs handle indirection */
static inline void *gc_value_get_ptr_check(GCValue v) {
    unsigned int tag = (unsigned int)((uintptr_t)(v) & 0xf);
    if (tag >= 0x7) {  /* Reference types */
        GCHandle handle = (GCHandle)((uintptr_t)(v) >> 4);
        if (handle == 0) {
            extern void __android_log_print(int prio, const char *tag, const char *fmt, ...);
            __android_log_print(6 /* ANDROID_LOG_ERROR */, "quickjs",
                "GC_VALUE_GET_PTR: handle=0 for v=%p tag=%u", (void*)v, tag);
            return NULL;
        }
        void *ptr = gc_deref(handle);
        if (!ptr) {
            extern void __android_log_print(int prio, const char *tag, const char *fmt, ...);
            __android_log_print(6 /* ANDROID_LOG_ERROR */, "quickjs",
                "GC_VALUE_GET_PTR: gc_deref returned NULL for handle=%u v=%p", handle, (void*)v);
        }
        return ptr;
    }
    return (void *)((intptr_t)(v) & ~0xf);
}
#define GC_VALUE_GET_PTR(v) gc_value_get_ptr_check(v)

#define GC_MKVAL(tag, val) (GCValue)(intptr_t)(((val) << 4) | (tag))

/* GC_MKPTR for CONFIG_CHECK_JSVALUE - stores handle instead of pointer */
static inline GCValue gc_mkptr_check(int tag, void *p) {
    if (p == NULL) {
        return (GCValue)(intptr_t)(tag);
    }
    if (tag < 0) {
        GCHandle handle = gc_alloc_handle_for_ptr(p);
        if (handle == 0) {
            extern void __android_log_print(int prio, const char *tag, const char *fmt, ...);
            __android_log_print(6 /* ANDROID_LOG_ERROR */, "quickjs", 
                "GC_MKPTR: FAILED to allocate handle for ptr=%p tag=%d", p, tag);
        }
        GCValue result = (GCValue)(intptr_t)(((uintptr_t)handle << 4) | (uintptr_t)(tag));
        if (((uintptr_t)result >> 4) == 0) {
            extern void __android_log_print(int prio, const char *tag, const char *fmt, ...);
            __android_log_print(6 /* ANDROID_LOG_ERROR */, "quickjs",
                "GC_MKPTR: WARNING handle=0 for ptr=%p tag=%d result=%p", p, tag, (void*)result);
        }
        return result;
    }
    return (GCValue)((intptr_t)(p) | (tag));
}
#define GC_MKPTR(tag, p) gc_mkptr_check(tag, p)

#define GC_TAG_IS_FLOAT64(tag) ((unsigned)(tag) == JS_TAG_FLOAT64)

#define GC_NAN GC_MKVAL(JS_TAG_FLOAT64, 1)

static inline GCValue GC_NewFloat64ConfigCheck(double d)
{
    return GC_MKVAL(JS_TAG_FLOAT64, (int)d);
}

static inline JS_BOOL GC_VALUE_IS_NAN_CHECK(GCValue v)
{
    return 0;
}

static inline GCValue GC_NewShortBigIntConfigCheck(int32_t d)
{
    return GC_MKVAL(JS_TAG_SHORT_BIG_INT, d);
}

#elif defined(JS_NAN_BOXING)

/* NaN boxing implementation for GCValue */
typedef uint64_t GCValue;
typedef uint64_t GCValueConst;

#define GC_VALUE_GET_TAG(v) (int)((v) >> 32)
#define GC_VALUE_GET_INT(v) (int)(v)
#define GC_VALUE_GET_BOOL(v) (int)(v)
#define GC_VALUE_GET_SHORT_BIG_INT(v) (int)(v)

/* For NaN boxing, we use the lower 32 bits to store handle for reference types */
static inline void *gc_value_get_ptr_nan(GCValue v) {
    int tag = (int)(v >> 32);
    GCHandle handle = (GCHandle)(v & 0xFFFFFFFF);
    if (tag < 0) {
        void *ptr = gc_deref(handle);
        if (!ptr) {
            extern void __android_log_print(int prio, const char *tag, const char *fmt, ...);
            __android_log_print(ANDROID_LOG_ERROR, "quickjs", "gc_value_get_ptr_nan: handle=%u returned NULL (v=0x%llx)", 
                               handle, (unsigned long long)v);
        }
        return ptr;
    }
    return (void *)(intptr_t)(v & 0xFFFFFFFF);
}
#define GC_VALUE_GET_PTR(v) gc_value_get_ptr_nan(v)

#define GC_MKVAL_NAN(tag, val) (((uint64_t)(tag) << 32) | (uint32_t)(val))

static inline GCValue gc_mkptr_nan(int tag, void *p) {
    if (p == NULL) {
        return ((uint64_t)(tag) << 32);
    }
    if (tag < 0) {
        /* GC-managed object - store handle in lower 32 bits */
        GCHandle handle = gc_alloc_handle_for_ptr(p);
        if (handle == 0) {
            extern void __android_log_print(int prio, const char *tag, const char *fmt, ...);
            __android_log_print(ANDROID_LOG_ERROR, "quickjs", "gc_mkptr_nan: gc_alloc_handle_for_ptr returned 0 for ptr=%p", p);
        }
        return (((uint64_t)(tag) << 32) | handle);
    }
    /* Non-GC pointer - store directly (shouldn't happen for tagged ptrs) */
    return (((uint64_t)(tag) << 32) | (uintptr_t)(p));
}
#define GC_MKPTR_NAN(tag, p) gc_mkptr_nan(tag, p)

#define GC_FLOAT64_TAG_ADDEND (0x7ff80000 - JS_TAG_FIRST + 1) /* quiet NaN encoding */

static inline double GC_VALUE_GET_FLOAT64_NAN(GCValue v)
{
    union {
        GCValue v;
        double d;
    } u;
    u.v = v;
    u.v += (uint64_t)GC_FLOAT64_TAG_ADDEND << 32;
    return u.d;
}

#define GC_NAN_NAN (0x7ff8000000000000 - ((uint64_t)GC_FLOAT64_TAG_ADDEND << 32))

static inline GCValue GC_NewFloat64Nan(double d)
{
    union {
        double d;
        uint64_t u64;
    } u;
    GCValue v;
    u.d = d;
    if (js_unlikely((u.u64 & 0x7fffffffffffffff) > 0x7ff0000000000000))
        v = GC_NAN_NAN;
    else
        v = u.u64 - ((uint64_t)GC_FLOAT64_TAG_ADDEND << 32);
    return v;
}

/* same as GC_VALUE_GET_TAG, but return JS_TAG_FLOAT64 with NaN boxing */
static inline int GC_VALUE_GET_NORM_TAG_NAN(GCValue v)
{
    uint32_t tag;
    tag = GC_VALUE_GET_TAG(v);
    if (GC_TAG_IS_FLOAT64(tag))
        return JS_TAG_FLOAT64;
    else
        return tag;
}

static inline JS_BOOL GC_VALUE_IS_NAN_NAN(GCValue v)
{
    uint32_t tag;
    tag = GC_VALUE_GET_TAG(v);
    return tag == (GC_NAN_NAN >> 32);
}

static inline GCValue GC_NewShortBigIntNan(int32_t d)
{
    return GC_MKVAL_NAN(JS_TAG_SHORT_BIG_INT, d);
}

#else /* !JS_NAN_BOXING */

/* ============================================================================
 * GCValue - GC-safe value type using GCHandle
 * ============================================================================
 * 
 * GCValue is the replacement for the old GCValue which stored raw pointers.
 * The problem with raw pointers is that the GC can compact memory, moving
 * objects and invalidating pointers stored in C variables.
 * 
 * GCValue stores a GCHandle (an index into the handle table) for reference
 * types. The handle remains stable across GC compaction. The actual pointer
 * is only obtained when needed through gc_deref(), used immediately, and
 * never stored.
 * 
 * CRITICAL RULE: Never call gc_deref() and store the result in a variable.
 * Always use the GC_PROP_* macros which dereference and use in one operation.
 */

typedef union GCValueUnion {
    int32_t int32;
    double float64;
#if JS_SHORT_BIG_INT_BITS == 32
    int32_t short_big_int;
#else
    int64_t short_big_int;
#endif
    /* Handle storage for GC-managed reference types (tag < 0) */
    GCHandle handle;
} GCValueUnion;

typedef struct GCValue {
    GCValueUnion u;
    int64_t tag;
} GCValue;

/* For const-correctness */
typedef const GCValue GCValueConst;

/* Type checking macros */
#define GC_VALUE_GET_TAG(v) ((int32_t)(v).tag)
#define GC_VALUE_GET_NORM_TAG(v) GC_VALUE_GET_TAG(v)

/* Value extraction macros for non-reference types */
#define GC_VALUE_GET_INT(v) ((v).u.int32)
#define GC_VALUE_GET_BOOL(v) ((v).u.int32)
#define GC_VALUE_GET_FLOAT64(v) ((v).u.float64)
#define GC_VALUE_GET_SHORT_BIG_INT(v) ((v).u.short_big_int)

/* For reference types, you get the handle - never the pointer */
#define GC_VALUE_GET_HANDLE(v) ((v).u.handle)

/* ============================================================================
 * GCHandle-based macros for GC-safe object access
 * ============================================================================
 * These macros work directly with GCHandles instead of pointers, ensuring
 * that object references remain valid across GC compaction.
 */

/* 
 * GC_OBJ_HANDLE - Get a field of type GCHandle from an object accessed via handle.
 * Usage: GCHandle proto_handle = GC_OBJ_HANDLE(obj_handle, JSObject, shape_handle);
 */
#define GC_OBJ_HANDLE(handle, type, field) ({ \
    void *_gc_ptr = gc_deref(handle); \
    (_gc_ptr != NULL) ? ((type *)_gc_ptr)->field : GC_HANDLE_NULL; \
})

/*
 * GC_OBJ_HANDLE_SET - Set a GCHandle field in an object accessed via handle.
 * Usage: GC_OBJ_HANDLE_SET(obj_handle, JSObject, shape_handle, new_shape_handle);
 */
#define GC_OBJ_HANDLE_SET(handle, type, field, value) do { \
    void *_gc_ptr = gc_deref(handle); \
    if (_gc_ptr != NULL) { \
        ((type *)_gc_ptr)->field = (value); \
    } \
} while(0)

/*
 * GC_HANDLE_TO_VALUE - Create a GCValue from a GCHandle and tag.
 * This is the handle-based replacement for JS_MKPTR.
 * Usage: GCValue val = GC_HANDLE_TO_VALUE(JS_TAG_OBJECT, obj_handle);
 */
#define GC_HANDLE_TO_VALUE(tag, handle) GC_MKHANDLE(tag, handle)

/*
 * GC_VALUE_TO_HANDLE - Extract the GCHandle from a GCValue.
 * This is the handle-based replacement for JS_VALUE_GET_OBJ (when used for handles).
 * Usage: GCHandle obj_handle = GC_VALUE_TO_HANDLE(val);
 */
#define GC_VALUE_TO_HANDLE(v) GC_VALUE_GET_HANDLE(v)

/* ============================================================================
 * GC-safe field access macros for JSObject and related structures
 * ============================================================================
 */

/*
 * GC_PROP_GET_HANDLE - Get a GCHandle property from a GCValue object.
 * This macro accesses the property and returns the handle, not a pointer.
 */
#define GC_PROP_GET_HANDLE(ctx, obj, atom, field_type, field_name) ({ \
    GCHandle _gc_handle = GC_HANDLE_NULL; \
    int _gc_tag = GC_VALUE_GET_TAG(obj); \
    if (_gc_tag < 0) { \
        void *_gc_ptr = gc_deref((obj).u.handle); \
        if (_gc_ptr != NULL) { \
            field_type *_obj = (field_type *)_gc_ptr; \
            _gc_handle = _obj->field_name; \
        } \
    } \
    _gc_handle; \
})

/*
 * GC_PTR_TO_HANDLE - Convert a GC-managed pointer to a GCHandle.
 * This is used when transitioning from pointer-based to handle-based code.
 * Returns GC_HANDLE_NULL if ptr is NULL.
 */
#define GC_PTR_TO_HANDLE(ptr) gc_ptr_to_handle(ptr)

/*
 * ============================================================================
 * GC-Safe Field Access Macros - NEVER store the result of gc_deref()
 * ============================================================================
 * 
 * These macros access fields of GC-managed objects through handles.
 * They ensure that pointers are never stored across potential GC points.
 * 
 * CRITICAL RULE: Never do this:
 *   JSObject *p = gc_deref(handle);  // WRONG - storing pointer!
 *   p->field = value;                // p may be invalid here!
 * 
 * Instead, use these macros which access fields through handles:
 *   GC_FIELD_SET(obj_handle, JSObject, field_handle, value_handle);
 */

/*
 * GC_FIELD_GET - Get a GCHandle field from a GC-managed object.
 * The pointer is dereferenced, the field is read, and the pointer is discarded.
 * Usage: GCHandle proto = GC_FIELD_GET(obj_handle, JSObject, proto_handle);
 */
#define GC_FIELD_GET(handle, type, field) ({ \
    GCHandle _field_handle = GC_HANDLE_NULL; \
    void *_ptr = gc_deref(handle); \
    if (_ptr != NULL) { \
        _field_handle = ((type *)_ptr)->field; \
    } \
    _field_handle; \
})

/*
 * GC_FIELD_SET - Set a GCHandle field in a GC-managed object.
 * Usage: GC_FIELD_SET(obj_handle, JSObject, proto_handle, new_proto_handle);
 */
#define GC_FIELD_SET(handle, type, field, value) do { \
    void *_ptr = gc_deref(handle); \
    if (_ptr != NULL) { \
        ((type *)_ptr)->field = (value); \
    } \
} while(0)

/*
 * GC_FIELD_GET_PTR - Get a non-GC pointer field from a GC-managed object.
 * This is for data pointers (like byte_code_buf), not GC object pointers.
 * Usage: uint8_t *bytecode = GC_FIELD_GET_PTR(func_handle, JSFunctionBytecode, byte_code_buf);
 */
#define GC_FIELD_GET_PTR(handle, type, field, ptr_type) ({ \
    ptr_type *_field_ptr = NULL; \
    void *_ptr = gc_deref(handle); \
    if (_ptr != NULL) { \
        _field_ptr = ((type *)_ptr)->field; \
    } \
    _field_ptr; \
})

/*
 * GC_OBJ_DEREF - Immediately dereference a handle and call a function with the pointer.
 * This macro ensures the pointer is never stored.
 * Usage: GC_OBJ_DEREF(obj_handle, JSObject, js_object_method, ctx, arg1, arg2);
 */
#define GC_OBJ_DEREF(handle, type, func, ...) ({ \
    void *_ptr = gc_deref(handle); \
    int _result = -1; \
    if (_ptr != NULL) { \
        _result = func(__VA_ARGS__, (type *)_ptr); \
    } \
    _result; \
})

/*
 * GC_SHAPE_DEREF - Dereference a shape handle to get a temporary JSShape pointer.
 * WARNING: The pointer is only valid until the next GC point.
 * NEVER store this pointer. Only use it for immediate field access.
 */
#define GC_SHAPE_DEREF(handle) ((JSShape *)gc_deref(handle))

/*
 * GC_OBJ_GET_SHAPE - Get the shape handle from an object.
 * Usage: GCHandle shape_handle = GC_OBJ_GET_SHAPE(obj_handle);
 */
#define GC_OBJ_GET_SHAPE(obj_handle) GC_FIELD_GET(obj_handle, JSObject, shape_handle)

/*
 * ============================================================================
 * ENHANCED GC-Safe Property Access Macros
 * ============================================================================
 * These macros provide direct field access without exposing gc_deref().
 * Use these instead of calling gc_deref() directly.
 */

/* ============================================================================
 * Scalar Field Access (uint32_t, int, uint16_t, uint8_t, etc.)
 * ============================================================================
 */

/* Get scalar field value (non-handle types) */
#define GC_HANDLE_GET_UINT32(handle, type, field) ({ \
    uint32_t _val = 0; \
    void *_ptr = gc_deref(handle); \
    if (_ptr != NULL) { \
        _val = ((type *)_ptr)->field; \
    } \
    _val; \
})

#define GC_HANDLE_GET_INT(handle, type, field) ({ \
    int _val = 0; \
    void *_ptr = gc_deref(handle); \
    if (_ptr != NULL) { \
        _val = ((type *)_ptr)->field; \
    } \
    _val; \
})

#define GC_HANDLE_GET_UINT16(handle, type, field) ({ \
    uint16_t _val = 0; \
    void *_ptr = gc_deref(handle); \
    if (_ptr != NULL) { \
        _val = ((type *)_ptr)->field; \
    } \
    _val; \
})

#define GC_HANDLE_GET_UINT8(handle, type, field) ({ \
    uint8_t _val = 0; \
    void *_ptr = gc_deref(handle); \
    if (_ptr != NULL) { \
        _val = ((type *)_ptr)->field; \
    } \
    _val; \
})

/* Set scalar field value */
#define GC_HANDLE_SET_UINT32(handle, type, field, value) do { \
    void *_ptr = gc_deref(handle); \
    if (_ptr != NULL) { \
        ((type *)_ptr)->field = (uint32_t)(value); \
    } \
} while(0)

#define GC_HANDLE_SET_INT(handle, type, field, value) do { \
    void *_ptr = gc_deref(handle); \
    if (_ptr != NULL) { \
        ((type *)_ptr)->field = (int)(value); \
    } \
} while(0)

#define GC_HANDLE_SET_UINT8(handle, type, field, value) do { \
    void *_ptr = gc_deref(handle); \
    if (_ptr != NULL) { \
        ((type *)_ptr)->field = (uint8_t)(value); \
    } \
} while(0)

/* ============================================================================
 * Raw Pointer Field Access (non-GC pointers like byte_code_buf, data buffers)
 * ============================================================================
 */
#define GC_FIELD_GET_RAW_PTR(handle, type, field, ptr_type) ({ \
    ptr_type *_field_ptr = NULL; \
    void *_ptr = gc_deref(handle); \
    if (_ptr != NULL) { \
        _field_ptr = (ptr_type)((type *)_ptr)->field; \
    } \
    _field_ptr; \
})

#define GC_FIELD_SET_RAW_PTR(handle, type, field, value) do { \
    void *_ptr = gc_deref(handle); \
    if (_ptr != NULL) { \
        ((type *)_ptr)->field = (value); \
    } \
} while(0)

/* ============================================================================
 * GCValue Field Access
 * ============================================================================
 */
#define GC_FIELD_GET_GCVALUE(handle, type, field) ({ \
    GCValue _val = GC_UNDEFINED; \
    void *_ptr = gc_deref(handle); \
    if (_ptr != NULL) { \
        _val = ((type *)_ptr)->field; \
    } \
    _val; \
})

#define GC_FIELD_SET_GCVALUE(handle, type, field, value) do { \
    void *_ptr = gc_deref(handle); \
    if (_ptr != NULL) { \
        ((type *)_ptr)->field = (value); \
    } \
} while(0)

/* ============================================================================
 * Property Array Access via Handles
 * ============================================================================
 */

/* Get JSProperty array element handle field */
#define GC_PROP_GET_HANDLE_AT(prop_handle, index, field) ({ \
    GCHandle _h = GC_HANDLE_NULL; \
    void *_ptr = gc_deref(prop_handle); \
    if (_ptr != NULL) { \
        JSProperty *_props = (JSProperty *)_ptr; \
        _h = _props[index].field; \
    } \
    _h; \
})

/* Set JSProperty array element handle field */
#define GC_PROP_SET_HANDLE_AT(prop_handle, index, field, value) do { \
    void *_ptr = gc_deref(prop_handle); \
    if (_ptr != NULL) { \
        JSProperty *_props = (JSProperty *)_ptr; \
        _props[index].field = (value); \
    } \
} while(0)

/* Get JSProperty array element GCValue */
#define GC_PROP_GET_GCVALUE_AT(prop_handle, index) ({ \
    GCValue _val = GC_UNDEFINED; \
    void *_ptr = gc_deref(prop_handle); \
    if (_ptr != NULL) { \
        JSProperty *_props = (JSProperty *)_ptr; \
        _val = _props[index].u.value; \
    } \
    _val; \
})

/* Set JSProperty array element GCValue */
#define GC_PROP_SET_GCVALUE_AT(prop_handle, index, value) do { \
    void *_ptr = gc_deref(prop_handle); \
    if (_ptr != NULL) { \
        JSProperty *_props = (JSProperty *)_ptr; \
        _props[index].u.value = (value); \
    } \
} while(0)

/* Get getter handle from property at index */
#define GC_PROP_GET_GETTER_AT(prop_handle, index) ({ \
    GCHandle _h = GC_HANDLE_NULL; \
    void *_ptr = gc_deref(prop_handle); \
    if (_ptr != NULL) { \
        JSProperty *_props = (JSProperty *)_ptr; \
        _h = _props[index].u.getset.getter_handle; \
    } \
    _h; \
})

/* Set getter handle in property at index */
#define GC_PROP_SET_GETTER_AT(prop_handle, index, h) do { \
    void *_ptr = gc_deref(prop_handle); \
    if (_ptr != NULL) { \
        JSProperty *_props = (JSProperty *)_ptr; \
        _props[index].u.getset.getter_handle = (h); \
    } \
} while(0)

/* Get setter handle from property at index */
#define GC_PROP_GET_SETTER_AT(prop_handle, index) ({ \
    GCHandle _h = GC_HANDLE_NULL; \
    void *_ptr = gc_deref(prop_handle); \
    if (_ptr != NULL) { \
        JSProperty *_props = (JSProperty *)_ptr; \
        _h = _props[index].u.getset.setter_handle; \
    } \
    _h; \
})

/* Set setter handle in property at index */
#define GC_PROP_SET_SETTER_AT(prop_handle, index, h) do { \
    void *_ptr = gc_deref(prop_handle); \
    if (_ptr != NULL) { \
        JSProperty *_props = (JSProperty *)_ptr; \
        _props[index].u.getset.setter_handle = (h); \
    } \
} while(0)

/* ============================================================================
 * Shape Property Access via Handles
 * ============================================================================
 */

/* Get JSShapeProperty field at index */
#define GC_SHAPE_PROP_GET_ATOM_AT(shape_handle, index) ({ \
    JSAtom _atom = JS_ATOM_NULL; \
    void *_ptr = gc_deref(shape_handle); \
    if (_ptr != NULL) { \
        JSShape *_sh = (JSShape *)_ptr; \
        JSShapeProperty *_props = (JSShapeProperty *)((uint8_t *)_sh + sizeof(JSShape)); \
        _atom = _props[index].atom; \
    } \
    _atom; \
})

#define GC_SHAPE_PROP_GET_FLAGS_AT(shape_handle, index) ({ \
    uint32_t _flags = 0; \
    void *_ptr = gc_deref(shape_handle); \
    if (_ptr != NULL) { \
        JSShape *_sh = (JSShape *)_ptr; \
        JSShapeProperty *_props = (JSShapeProperty *)((uint8_t *)_sh + sizeof(JSShape)); \
        _flags = _props[index].flags; \
    } \
    _flags; \
})

#define GC_SHAPE_PROP_GET_HASH_NEXT_AT(shape_handle, index) ({ \
    uint32_t _next = 0; \
    void *_ptr = gc_deref(shape_handle); \
    if (_ptr != NULL) { \
        JSShape *_sh = (JSShape *)_ptr; \
        JSShapeProperty *_props = (JSShapeProperty *)((uint8_t *)_sh + sizeof(JSShape)); \
        _next = _props[index].hash_next; \
    } \
    _next; \
})

/* ============================================================================
 * JSObject-specific Access Macros
 * ============================================================================
 */
#define GC_OBJ_GET_PROP_HANDLE(obj_handle) GC_FIELD_GET(obj_handle, JSObject, prop_handle)
#define GC_OBJ_SET_PROP_HANDLE(obj_handle, val) GC_FIELD_SET(obj_handle, JSObject, prop_handle, val)
#define GC_OBJ_GET_CLASS_ID(obj_handle) GC_HANDLE_GET_UINT16(obj_handle, JSObject, class_id)

/* ============================================================================
 * Convenience Aliases for Common Object/Shape Field Access
 * ============================================================================
 */

/* JSObject handle field accessors */
#define GC_OBJ_GET_SHAPE_HANDLE(obj_handle) GC_FIELD_GET(obj_handle, JSObject, shape_handle)
#define GC_OBJ_SET_SHAPE_HANDLE(obj_handle, val) GC_FIELD_SET(obj_handle, JSObject, shape_handle, val)
#define GC_OBJ_GET_PROP_HANDLE(obj_handle) GC_FIELD_GET(obj_handle, JSObject, prop_handle)
#define GC_OBJ_SET_PROP_HANDLE(obj_handle, val) GC_FIELD_SET(obj_handle, JSObject, prop_handle, val)

/* JSObject boolean/scalar field accessors */
#define GC_OBJ_IS_EXOTIC(obj_handle) ({ \
    uint8_t _val = GC_HANDLE_GET_UINT8(obj_handle, JSObject, is_exotic); \
    _val; \
})
#define GC_OBJ_IS_FAST_ARRAY(obj_handle) ({ \
    uint8_t _val = GC_HANDLE_GET_UINT8(obj_handle, JSObject, fast_array); \
    _val; \
})
#define GC_OBJ_IS_EXTENSIBLE(obj_handle) ({ \
    uint8_t _val = GC_HANDLE_GET_UINT8(obj_handle, JSObject, extensible); \
    _val; \
})
#define GC_OBJ_IS_CONSTRUCTOR(obj_handle) ({ \
    uint8_t _val = GC_HANDLE_GET_UINT8(obj_handle, JSObject, is_constructor); \
    _val; \
})
#define GC_OBJ_GET_WEAKREF_COUNT(obj_handle) GC_HANDLE_GET_UINT32(obj_handle, JSObject, weakref_count)

/* JSShape scalar field accessors */
#define GC_SHAPE_GET_PROP_HASH_MASK(shape_handle) GC_HANDLE_GET_UINT32(shape_handle, JSShape, prop_hash_mask)
#define GC_SHAPE_GET_PROP_SIZE(shape_handle) GC_HANDLE_GET_INT(shape_handle, JSShape, prop_size)
#define GC_SHAPE_GET_PROP_COUNT(shape_handle) GC_HANDLE_GET_INT(shape_handle, JSShape, prop_count)
#define GC_SHAPE_GET_DELETED_COUNT(shape_handle) GC_HANDLE_GET_INT(shape_handle, JSShape, deleted_prop_count)
#define GC_SHAPE_GET_HASH(shape_handle) GC_HANDLE_GET_UINT32(shape_handle, JSShape, hash)
#define GC_SHAPE_IS_HASHED(shape_handle) ({ \
    uint8_t _val = GC_HANDLE_GET_UINT8(shape_handle, JSShape, is_hashed); \
    _val; \
})
#define GC_SHAPE_SET_HASHED(shape_handle, val) GC_HANDLE_SET_UINT8(shape_handle, JSShape, is_hashed, val)

/* ============================================================================
 * JSShape-specific Access Macros
 * ============================================================================
 */
#define GC_SHAPE_GET_PROTO_HANDLE(shape_handle) GC_FIELD_GET(shape_handle, JSShape, proto_handle)
#define GC_SHAPE_SET_PROTO_HANDLE(shape_handle, val) GC_FIELD_SET(shape_handle, JSShape, proto_handle, val)
#define GC_SHAPE_GET_HASH_NEXT_HANDLE(shape_handle) GC_FIELD_GET(shape_handle, JSShape, shape_hash_next_handle)
#define GC_SHAPE_SET_HASH_NEXT_HANDLE(shape_handle, val) GC_FIELD_SET(shape_handle, JSShape, shape_hash_next_handle, val)

/* ============================================================================
 * GC Dereference Function
 * ============================================================================
 * The gc_deref() function converts a GCHandle to a pointer.
 * Use the macros above instead of calling this directly.
 */
void *gc_deref(GCHandle handle);

/* Type predicate macros */
#define GC_IS_OBJECT(v)       (GC_VALUE_GET_TAG(v) == JS_TAG_OBJECT)
#define GC_IS_NULL(v)         (GC_VALUE_GET_TAG(v) == JS_TAG_NULL)
#define GC_IS_UNDEFINED(v)    (GC_VALUE_GET_TAG(v) == JS_TAG_UNDEFINED)
#define GC_IS_BOOL(v)         (GC_VALUE_GET_TAG(v) == JS_TAG_BOOL)
#define GC_IS_INT(v)          (GC_VALUE_GET_TAG(v) == JS_TAG_INT)
#define GC_IS_FLOAT64(v)      (GC_VALUE_GET_TAG(v) == JS_TAG_FLOAT64)
#define GC_IS_STRING(v)       (GC_VALUE_GET_TAG(v) == JS_TAG_STRING || \
                               GC_VALUE_GET_TAG(v) == JS_TAG_STRING_ROPE)
#define GC_IS_SYMBOL(v)       (GC_VALUE_GET_TAG(v) == JS_TAG_SYMBOL)
#define GC_IS_BIG_INT(v)      (GC_VALUE_GET_TAG(v) == JS_TAG_BIG_INT || \
                               GC_VALUE_GET_TAG(v) == JS_TAG_SHORT_BIG_INT)
#define GC_IS_EXCEPTION(v)    (GC_VALUE_GET_TAG(v) == JS_TAG_EXCEPTION)

/* Reference types have negative tags */
#define GC_IS_REFERENCE(v)    (GC_VALUE_GET_TAG(v) < 0)

/* Create GCValue for primitive types */
#define GC_MKVAL(tag, val) (GCValue){ (GCValueUnion){ .int32 = val }, tag }

/* Create GCValue from a handle for reference types */
#define GC_MKHANDLE(tag, handle_val) (GCValue){ (GCValueUnion){ .handle = handle_val }, tag }

/* 
 * GC_WRAP_PTR - Wrap a pointer in a GCValue by allocating a handle.
 * This is used when creating new objects that will be managed by the GC.
 */
static inline GCValue GC_WRAP_PTR(int tag, void *p) {
    GCValue v;
    v.tag = tag;
    if (p == NULL) {
        v.u.handle = GC_HANDLE_NULL;
    } else {
        /* Always use handle indirection for reference types */
        v.u.handle = gc_alloc_handle_for_ptr(p);
    }
    return v;
}

#define GC_TAG_IS_FLOAT64(tag) ((unsigned)(tag) == JS_TAG_FLOAT64)

/* Special values */
#define GC_NAN (GCValue){ .u.float64 = JS_FLOAT64_NAN, JS_TAG_FLOAT64 }
#define GC_NULL      GC_MKVAL(JS_TAG_NULL, 0)
#define GC_UNDEFINED GC_MKVAL(JS_TAG_UNDEFINED, 0)
#define GC_FALSE     GC_MKVAL(JS_TAG_BOOL, 0)
#define GC_TRUE      GC_MKVAL(JS_TAG_BOOL, 1)
#define GC_EXCEPTION GC_MKVAL(JS_TAG_EXCEPTION, 0)

/* Constructors for primitive types */
static inline GCValue GC_NewFloat64(double d)
{
    GCValue v;
    v.tag = JS_TAG_FLOAT64;
    v.u.float64 = d;
    return v;
}

static inline GCValue GC_NewInt32(int32_t n)
{
    return GC_MKVAL(JS_TAG_INT, n);
}

static inline GCValue GC_NewBool(JS_BOOL b)
{
    return GC_MKVAL(JS_TAG_BOOL, b ? 1 : 0);
}

static inline JS_BOOL GC_VALUE_IS_NAN(GCValue v)
{
    union {
        double d;
        uint64_t u64;
    } u;
    if (v.tag != JS_TAG_FLOAT64)
        return 0;
    u.d = v.u.float64;
    return (u.u64 & 0x7fffffffffffffff) > 0x7ff0000000000000;
}

static inline GCValue GC_NewShortBigInt(int64_t d)
{
    GCValue v;
    v.tag = JS_TAG_SHORT_BIG_INT;
    v.u.short_big_int = d;
    return v;
}

#endif /* !JS_NAN_BOXING */

/* ============================================================================
 * Reference Counting Stubs for Mark-and-Sweep GC
 * ============================================================================
 * These are no-ops since we're using mark-and-sweep GC with shadow stack
 */
struct JSRuntime;
struct JSContext;
/* Note: With GC-based memory management, GCValue doesn't need reference counting.
   Simply assign/copy GCValue directly - the GC tracks object lifetime via reachability.
   No manual Dup/Free needed - the stable handle in GCValue is automatically valid. */

#define GC_VALUE_IS_BOTH_INT(v1, v2) ((GC_VALUE_GET_TAG(v1) | GC_VALUE_GET_TAG(v2)) == 0)
#define GC_VALUE_IS_BOTH_FLOAT(v1, v2) (GC_TAG_IS_FLOAT64(GC_VALUE_GET_TAG(v1)) && GC_TAG_IS_FLOAT64(GC_VALUE_GET_TAG(v2)))

#define GC_VALUE_HAS_REF_COUNT(v) ((unsigned)GC_VALUE_GET_TAG(v) >= (unsigned)JS_TAG_FIRST)

/* Special values */
#define GC_NULL      GC_MKVAL(JS_TAG_NULL, 0)
#define GC_UNDEFINED GC_MKVAL(JS_TAG_UNDEFINED, 0)
#define GC_FALSE     GC_MKVAL(JS_TAG_BOOL, 0)
#define GC_TRUE      GC_MKVAL(JS_TAG_BOOL, 1)
#define GC_EXCEPTION GC_MKVAL(JS_TAG_EXCEPTION, 0)
#define GC_UNINITIALIZED GC_MKVAL(JS_TAG_UNINITIALIZED, 0)

/* Backward compatibility macros - map old JS_* names to GC_* names */
#define JS_NULL      GC_NULL
#define JS_UNDEFINED GC_UNDEFINED
#define JS_FALSE     GC_FALSE
#define JS_TRUE      GC_TRUE
#define JS_EXCEPTION GC_EXCEPTION
#define JS_UNINITIALIZED GC_UNINITIALIZED

/* Backward compatibility for value access macros */
#define JS_VALUE_GET_TAG(v)       GC_VALUE_GET_TAG(v)
#define JS_VALUE_GET_NORM_TAG(v)  GC_VALUE_GET_NORM_TAG(v)
#define JS_VALUE_GET_INT(v)       GC_VALUE_GET_INT(v)
#define JS_VALUE_GET_BOOL(v)      GC_VALUE_GET_BOOL(v)
#define JS_VALUE_GET_FLOAT64(v)   GC_VALUE_GET_FLOAT64(v)
#define JS_VALUE_GET_PTR(v)       GC_VALUE_GET_PTR(v)
#define JS_VALUE_HAS_REF_COUNT(v) GC_VALUE_HAS_REF_COUNT(v)
#define JS_VALUE_GET_SHORT_BIG_INT(v) GC_VALUE_GET_SHORT_BIG_INT(v)
#define JS_TAG_IS_FLOAT64(tag)    GC_TAG_IS_FLOAT64(tag)
#define JS_VALUE_IS_BOTH_INT(v1, v2) GC_VALUE_IS_BOTH_INT(v1, v2)
#define JS_VALUE_IS_BOTH_FLOAT(v1, v2) GC_VALUE_IS_BOTH_FLOAT(v1, v2)
#define JS_MKPTR(tag, p)          GC_MKPTR(tag, p)
#define JS_MKVAL(tag, val)        GC_MKVAL(tag, val)
#define JS_NAN                    GC_NAN

/* Additional backward compatibility functions */
static inline GCValue __JS_NewShortBigInt(JSContext *ctx, int64_t val)
{
    (void)ctx;
    return GC_NewShortBigInt(val);
}

/* GC_MKPTR - needs to be defined for non-NaN boxing case */
#ifndef GC_MKPTR
#define GC_MKPTR(tag, p) GC_WRAP_PTR(tag, p)
#endif

/* GC_VALUE_GET_PTR - needs to be defined for non-NaN boxing case */
#ifndef GC_VALUE_GET_PTR
#define GC_VALUE_GET_PTR(v) ({ \
    void *_ptr = NULL; \
    if (GC_VALUE_GET_TAG(v) < 0) { \
        _ptr = gc_deref(GC_VALUE_GET_HANDLE(v)); \
    } else { \
        _ptr = (void *)((intptr_t)(v).u.int32 & ~0xf); \
    } \
    _ptr; \
})
#endif

/* ============================================================================
 * GCValue Property Access Macros
 * ============================================================================
 * 
 * These macros provide safe property access for GCValue/GCValue objects.
 * They dereference the handle and access the property in one operation,
 * ensuring the pointer is never stored across potential GC points.
 * 
 * RULE: Never store the result of GC_VALUE_GET_PTR(). Always use these
 * macros which get the pointer and use it immediately.
 */

/*
 * GC_PROP_GET_STR - Get string property from a GCValue object.
 * 
 * This macro:
 * 1. Checks if the value is a reference type (tag < 0)
 * 2. Dereferences the handle to get the current pointer
 * 3. Immediately calls the property getter
 * 4. Does not store the pointer anywhere
 * 
 * Usage:
 *   GCValue obj = ...;
 *   GCValue prop = GC_PROP_GET_STR(ctx, obj, "propertyName");
 * 
 * IMPORTANT: The pointer obtained from gc_deref is used immediately within
 * the macro and never stored. This ensures GC safety.
 */
#define GC_PROP_GET_STR(ctx, obj, prop) ({ \
    GCValue _gc_result = GC_UNDEFINED; \
    int _gc_tag = GC_VALUE_GET_TAG(obj); \
    if (_gc_tag < 0) { \
        void *_gc_ptr = gc_deref((obj).u.handle); \
        if (_gc_ptr != NULL) { \
            GCValue _gc_obj = GC_WRAP_PTR(_gc_tag, _gc_ptr); \
            _gc_result = JS_GetPropertyStr((ctx), _gc_obj, (prop)); \
        } \
    } \
    _gc_result; \
})

/*
 * GC_PROP_SET_STR - Set string property on a GCValue object.
 */
#define GC_PROP_SET_STR(ctx, obj, prop, val) ({ \
    int _gc_result = -1; \
    int _gc_tag = GC_VALUE_GET_TAG(obj); \
    if (_gc_tag < 0) { \
        void *_gc_ptr = gc_deref((obj).u.handle); \
        if (_gc_ptr != NULL) { \
            GCValue _gc_obj = GC_WRAP_PTR(_gc_tag, _gc_ptr); \
            _gc_result = JS_SetPropertyStr((ctx), _gc_obj, (prop), (val)); \
        } \
    } \
    _gc_result; \
})

/*
 * GC_PROP_GET_UINT32 - Get property by numeric index.
 */
#define GC_PROP_GET_UINT32(ctx, obj, idx) ({ \
    GCValue _gc_result = GC_UNDEFINED; \
    int _gc_tag = GC_VALUE_GET_TAG(obj); \
    if (_gc_tag < 0) { \
        void *_gc_ptr = gc_deref((obj).u.handle); \
        if (_gc_ptr != NULL) { \
            GCValue _gc_obj = GC_WRAP_PTR(_gc_tag, _gc_ptr); \
            _gc_result = JS_GetPropertyUint32((ctx), _gc_obj, (idx)); \
        } \
    } \
    _gc_result; \
})

/*
 * GC_PROP_SET_UINT32 - Set property by numeric index.
 */
#define GC_PROP_SET_UINT32(ctx, obj, idx, val) ({ \
    int _gc_result = -1; \
    int _gc_tag = GC_VALUE_GET_TAG(obj); \
    if (_gc_tag < 0) { \
        void *_gc_ptr = gc_deref((obj).u.handle); \
        if (_gc_ptr != NULL) { \
            GCValue _gc_obj = GC_WRAP_PTR(_gc_tag, _gc_ptr); \
            _gc_result = JS_SetPropertyUint32((ctx), _gc_obj, (idx), (val)); \
        } \
    } \
    _gc_result; \
})

/*
 * GC_IS_OBJECT - Check if GCValue is an object.
 */
#define GC_IS_OBJECT(v) (GC_VALUE_GET_TAG(v) == JS_TAG_OBJECT)

/*
 * GC_IS_NULL - Check if GCValue is null.
 */
#define GC_IS_NULL(v) (GC_VALUE_GET_TAG(v) == JS_TAG_NULL)

/*
 * GC_IS_UNDEFINED - Check if GCValue is undefined.
 */
#define GC_IS_UNDEFINED(v) (GC_VALUE_GET_TAG(v) == JS_TAG_UNDEFINED)

/*
 * GC_IS_STRING - Check if GCValue is a string.
 */
#define GC_IS_STRING(v) (GC_VALUE_GET_TAG(v) == JS_TAG_STRING || \
                         GC_VALUE_GET_TAG(v) == JS_TAG_STRING_ROPE)

/* ============================================================================
 * Backwards compatibility: JS property functions work with GCValue
 * ============================================================================
 */

/* flags for object properties */
#define JS_PROP_CONFIGURABLE  (1 << 0)
#define JS_PROP_WRITABLE      (1 << 1)
#define JS_PROP_ENUMERABLE    (1 << 2)
#define JS_PROP_C_W_E         (JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE | JS_PROP_ENUMERABLE)
#define JS_PROP_LENGTH        (1 << 3) /* used internally in Arrays */
#define JS_PROP_TMASK         (3 << 4) /* mask for NORMAL, GETSET, VARREF, AUTOINIT */
#define JS_PROP_NORMAL         (0 << 4)
#define JS_PROP_GETSET         (1 << 4)
#define JS_PROP_VARREF         (2 << 4) /* used internally */
#define JS_PROP_AUTOINIT       (3 << 4) /* used internally */

/* flags for JS_DefineProperty */
#define JS_PROP_HAS_SHIFT        8
#define JS_PROP_HAS_CONFIGURABLE (1 << 8)
#define JS_PROP_HAS_WRITABLE     (1 << 9)
#define JS_PROP_HAS_ENUMERABLE   (1 << 10)
#define JS_PROP_HAS_GET          (1 << 11)
#define JS_PROP_HAS_SET          (1 << 12)
#define JS_PROP_HAS_VALUE        (1 << 13)

/* throw an exception if false would be returned
   (JS_DefineProperty/JS_SetProperty) */
#define JS_PROP_THROW            (1 << 14)
/* throw an exception if false would be returned in strict mode
   (JS_SetProperty) */
#define JS_PROP_THROW_STRICT     (1 << 15)

#define JS_PROP_NO_EXOTIC        (1 << 16) /* internal use */

#ifndef JS_DEFAULT_STACK_SIZE
#define JS_DEFAULT_STACK_SIZE (1024 * 1024)
#endif

/* JS_Eval() flags */
#define JS_EVAL_TYPE_GLOBAL   (0 << 0) /* global code (default) */
#define JS_EVAL_TYPE_MODULE   (1 << 0) /* module code */
#define JS_EVAL_TYPE_DIRECT   (2 << 0) /* direct call (internal use) */
#define JS_EVAL_TYPE_INDIRECT (3 << 0) /* indirect call (internal use) */
#define JS_EVAL_TYPE_MASK     (3 << 0)

#define JS_EVAL_FLAG_STRICT   (1 << 3) /* force 'strict' mode */
/* compile but do not run. The result is an object with a
   JS_TAG_FUNCTION_BYTECODE or JS_TAG_MODULE tag. It can be executed
   with JS_EvalFunction(). */
#define JS_EVAL_FLAG_COMPILE_ONLY (1 << 5)
/* don't include the stack frames before this eval in the Error() backtraces */
#define JS_EVAL_FLAG_BACKTRACE_BARRIER (1 << 6)
/* allow top-level await in normal script. JS_Eval() returns a
   promise. Only allowed with JS_EVAL_TYPE_GLOBAL */
#define JS_EVAL_FLAG_ASYNC (1 << 7)

typedef GCValue JSCFunction(JSContext *ctx, GCValue this_val, int argc, GCValue *argv);
typedef GCValue JSCFunctionMagic(JSContext *ctx, GCValue this_val, int argc, GCValue *argv, int magic);
typedef GCValue JSCFunctionData(JSContext *ctx, GCValue this_val, int argc, GCValue *argv, int magic, GCValue *func_data);

typedef struct JSMallocState {
    size_t malloc_count;
    size_t malloc_size;
    size_t malloc_limit;
    void *opaque; /* user opaque */
} JSMallocState;

typedef struct GCHeader GCHeader;

/* 
 * Create a new JS runtime.
 * NOTE: gc_init() MUST be called before this function!
 * All memory comes from the unified GC allocator.
 */
JSRuntime *JS_NewRuntime(void);
/* info lifetime must exceed that of rt */
void JS_SetRuntimeInfo(JSRuntime *rt, const char *info);
void JS_SetMemoryLimit(JSRuntime *rt, size_t limit);
void JS_SetGCThreshold(JSRuntime *rt, size_t gc_threshold);
/* use 0 to disable maximum stack size check */
void JS_SetMaxStackSize(JSRuntime *rt, size_t stack_size);
/* should be called when changing thread to update the stack top value
   used to check stack overflow. */
void JS_UpdateStackTop(JSRuntime *rt);
void JS_FreeRuntime(JSRuntime *rt);
void *JS_GetRuntimeOpaque(JSRuntime *rt);
void JS_SetRuntimeOpaque(JSRuntime *rt, void *opaque);
typedef void JS_MarkFunc(JSRuntime *rt, void *user_ptr);
void JS_MarkValue(JSRuntime *rt, GCValue val, JS_MarkFunc *mark_func);
void JS_RunGC(JSRuntime *rt);
JS_BOOL JS_IsLiveObject(JSRuntime *rt, GCValue obj);

JSContext *JS_NewContext(JSRuntime *rt);
void JS_FreeContext(JSContext *s);
JSContext *JS_DupContext(JSContext *ctx);
void *JS_GetContextOpaque(JSContext *ctx);
void JS_SetContextOpaque(JSContext *ctx, void *opaque);
JSRuntime *JS_GetRuntime(JSContext *ctx);
void JS_SetClassProto(JSContext *ctx, JSClassID class_id, GCValue obj);
GCValue JS_GetClassProto(JSContext *ctx, JSClassID class_id);

/* the following functions are used to select the intrinsic object to
   save memory */
JSContext *JS_NewContextRaw(JSRuntime *rt);
int JS_AddIntrinsicBaseObjects(JSContext *ctx);
int JS_AddIntrinsicDate(JSContext *ctx);
int JS_AddIntrinsicEval(JSContext *ctx);
int JS_AddIntrinsicStringNormalize(JSContext *ctx);
void JS_AddIntrinsicRegExpCompiler(JSContext *ctx);
int JS_AddIntrinsicRegExp(JSContext *ctx);
int JS_AddIntrinsicJSON(JSContext *ctx);
int JS_AddIntrinsicProxy(JSContext *ctx);
int JS_AddIntrinsicMapSet(JSContext *ctx);
int JS_AddIntrinsicTypedArrays(JSContext *ctx);
int JS_AddIntrinsicPromise(JSContext *ctx);
int JS_AddIntrinsicWeakRef(JSContext *ctx);

GCValue js_string_codePointRange(JSContext *ctx, GCValue this_val,
                                 int argc, GCValue *argv);

/* GC allocation functions - use gc_alloc/gc_realloc directly
 * 
 * Pattern replacements:
 *   js_malloc(ctx, size)        -> gc_alloc(size, JS_GC_OBJ_TYPE_DATA)
 *   js_mallocz(ctx, size)       -> gc_allocz(size, JS_GC_OBJ_TYPE_DATA)
 *   js_realloc(ctx, h, size)    -> gc_realloc(h, size)
 *   js_realloc2(ctx, h, sz, sl) -> gc_realloc2(h, sz, &sl)
 *   js_strdup(ctx, str)         -> gc_strdup(str)
 *   js_strndup(ctx, s, n)       -> gc_strndup(s, n)
 *   js_malloc_usable_size(...)  -> gc_usable_size(handle)
 * 
 * Note: No free functions - GC automatically reclaims unreachable objects
 */

typedef struct JSMemoryUsage {
    int64_t malloc_size, malloc_limit, memory_used_size;
    int64_t malloc_count;
    int64_t memory_used_count;
    int64_t atom_count, atom_size;
    int64_t str_count, str_size;
    int64_t obj_count, obj_size;
    int64_t prop_count, prop_size;
    int64_t shape_count, shape_size;
    int64_t js_func_count, js_func_size, js_func_code_size;
    int64_t js_func_pc2line_count, js_func_pc2line_size;
    int64_t c_func_count, array_count;
    int64_t fast_array_count, fast_array_elements;
    int64_t binary_object_count, binary_object_size;
} JSMemoryUsage;

void JS_ComputeMemoryUsage(JSRuntime *rt, JSMemoryUsage *s);
void JS_DumpMemoryUsage(FILE *fp, const JSMemoryUsage *s, JSRuntime *rt);

/* atom support */
#define JS_ATOM_NULL 0

JSAtom JS_NewAtomLen(JSContext *ctx, const char *str, size_t len);
JSAtom JS_NewAtom(JSContext *ctx, const char *str);
JSAtom JS_NewAtomUInt32(JSContext *ctx, uint32_t n);
JSAtom JS_DupAtom(JSContext *ctx, JSAtom v);
void JS_FreeAtom(JSContext *ctx, JSAtom v);
void JS_FreeAtomRT(JSRuntime *rt, JSAtom v);
GCValue JS_AtomToValue(JSContext *ctx, JSAtom atom);
GCValue JS_AtomToString(JSContext *ctx, JSAtom atom);
const char *JS_AtomToCStringLen(JSContext *ctx, size_t *plen, JSAtom atom);
static inline const char *JS_AtomToCString(JSContext *ctx, JSAtom atom)
{
    return JS_AtomToCStringLen(ctx, NULL, atom);
}
JSAtom JS_ValueToAtom(JSContext *ctx, GCValue val);

/* object class support */

typedef struct JSPropertyEnum {
    JS_BOOL is_enumerable;
    JSAtom atom;
} JSPropertyEnum;

typedef struct JSPropertyDescriptor {
    int flags;
    GCValue value;
    GCValue getter;
    GCValue setter;
} JSPropertyDescriptor;

typedef struct JSClassExoticMethods {
    /* Return -1 if exception (can only happen in case of Proxy object),
       FALSE if the property does not exists, TRUE if it exists. If 1 is
       returned, the property descriptor 'desc' is filled if != NULL. */
    int (*get_own_property)(JSContext *ctx, JSPropertyDescriptor *desc,
                             GCValue obj, JSAtom prop);
    /* '*ptab' should hold the '*plen' property keys. Return 0 if OK,
       -1 if exception. The 'is_enumerable' field is ignored.
    */
    int (*get_own_property_names)(JSContext *ctx, JSPropertyEnum **ptab,
                                  uint32_t *plen,
                                  GCValue obj);
    /* return < 0 if exception, or TRUE/FALSE */
    int (*delete_property)(JSContext *ctx, GCValue obj, JSAtom prop);
    /* return < 0 if exception or TRUE/FALSE */
    int (*define_own_property)(JSContext *ctx, GCValue this_obj,
                               JSAtom prop, GCValue val,
                               GCValue getter, GCValue setter,
                               int flags);
    /* The following methods can be emulated with the previous ones,
       so they are usually not needed */
    /* return < 0 if exception or TRUE/FALSE */
    int (*has_property)(JSContext *ctx, GCValue obj, JSAtom atom);
    GCValue (*get_property)(JSContext *ctx, GCValue obj, JSAtom atom,
                            GCValue receiver);
    /* return < 0 if exception or TRUE/FALSE */
    int (*set_property)(JSContext *ctx, GCValue obj, JSAtom atom,
                        GCValue value, GCValue receiver, int flags);

    /* To get a consistent object behavior when get_prototype != NULL,
       get_property, set_property and set_prototype must be != NULL
       and the object must be created with a GC_NULL prototype. */
    GCValue (*get_prototype)(JSContext *ctx, GCValue obj);
    /* return < 0 if exception or TRUE/FALSE */
    int (*set_prototype)(JSContext *ctx, GCValue obj, GCValue proto_val);
    /* return < 0 if exception or TRUE/FALSE */
    int (*is_extensible)(JSContext *ctx, GCValue obj);
    /* return < 0 if exception or TRUE/FALSE */
    int (*prevent_extensions)(JSContext *ctx, GCValue obj);
} JSClassExoticMethods;

typedef void JSClassFinalizer(JSRuntime *rt, GCValue val);
typedef void JSClassGCMark(JSRuntime *rt, GCValue val,
                           JS_MarkFunc *mark_func);
#define JS_CALL_FLAG_CONSTRUCTOR (1 << 0)
typedef GCValue JSClassCall(JSContext *ctx, GCValue func_obj,
                            GCValue this_val, int argc, GCValue *argv,
                            int flags);

typedef struct JSClassDef {
    const char *class_name;
    JSClassFinalizer *finalizer;
    JSClassGCMark *gc_mark;
    /* if call != NULL, the object is a function. If (flags &
       JS_CALL_FLAG_CONSTRUCTOR) != 0, the function is called as a
       constructor. In this case, 'this_val' is new.target. A
       constructor call only happens if the object constructor bit is
       set (see JS_SetConstructorBit()). */
    JSClassCall *call;
    /* XXX: suppress this indirection ? It is here only to save memory
       because only a few classes need these methods */
    JSClassExoticMethods *exotic;
} JSClassDef;

#define JS_INVALID_CLASS_ID 0
JSClassID JS_NewClassID(JSClassID *pclass_id);
/* Returns the class ID if `v` is an object, otherwise returns JS_INVALID_CLASS_ID. */
JSClassID JS_GetClassID(GCValue v);
int JS_NewClass(JSRuntime *rt, JSClassID class_id, const JSClassDef *class_def);
int JS_IsRegisteredClass(JSRuntime *rt, JSClassID class_id);

/* value handling */

static js_force_inline GCValue JS_NewBool(JSContext *ctx, JS_BOOL val)
{
    return GC_MKVAL(JS_TAG_BOOL, (val != 0));
}

static js_force_inline GCValue JS_NewInt32(JSContext *ctx, int32_t val)
{
    return GC_MKVAL(JS_TAG_INT, val);
}

static js_force_inline GCValue JS_NewCatchOffset(JSContext *ctx, int32_t val)
{
    return GC_MKVAL(JS_TAG_CATCH_OFFSET, val);
}

/* Internal function to create a Float64 GCValue (ctx not used, for compatibility) */
static inline GCValue __JS_NewFloat64(JSContext *ctx, double d)
{
    (void)ctx;  /* ctx not used but kept for API compatibility */
    return GC_NewFloat64(d);
}

static js_force_inline GCValue JS_NewInt64(JSContext *ctx, int64_t val)
{
    GCValue v;
    if (val == (int32_t)val) {
        v = JS_NewInt32(ctx, val);
    } else {
        v = __JS_NewFloat64(ctx, val);
    }
    return v;
}

static js_force_inline GCValue JS_NewUint32(JSContext *ctx, uint32_t val)
{
    GCValue v;
    if (val <= 0x7fffffff) {
        v = JS_NewInt32(ctx, val);
    } else {
        v = __JS_NewFloat64(ctx, val);
    }
    return v;
}

GCValue JS_NewBigInt64(JSContext *ctx, int64_t v);
GCValue JS_NewBigUint64(JSContext *ctx, uint64_t v);

static js_force_inline GCValue JS_NewFloat64(JSContext *ctx, double d)
{
    int32_t val;
    union {
        double d;
        uint64_t u;
    } u, t;
    if (d >= INT32_MIN && d <= INT32_MAX) {
        u.d = d;
        val = (int32_t)d;
        t.d = val;
        /* -0 cannot be represented as integer, so we compare the bit
           representation */
        if (u.u == t.u)
            return GC_MKVAL(JS_TAG_INT, val);
    }
    return __JS_NewFloat64(ctx, d);
}

static inline JS_BOOL JS_IsNumber(GCValue v)
{
    int tag = GC_VALUE_GET_TAG(v);
    return tag == JS_TAG_INT || GC_TAG_IS_FLOAT64(tag);
}

static inline JS_BOOL JS_IsBigInt(GCValue v)
{
    int tag = GC_VALUE_GET_TAG(v);
    return tag == JS_TAG_BIG_INT || tag == JS_TAG_SHORT_BIG_INT;
}

static inline JS_BOOL JS_IsBool(GCValue v)
{
    return GC_VALUE_GET_TAG(v) == JS_TAG_BOOL;
}

static inline JS_BOOL JS_IsNull(GCValue v)
{
    return GC_VALUE_GET_TAG(v) == JS_TAG_NULL;
}

static inline JS_BOOL JS_IsUndefined(GCValue v)
{
    return GC_VALUE_GET_TAG(v) == JS_TAG_UNDEFINED;
}

static inline JS_BOOL JS_IsException(GCValue v)
{
    return js_unlikely(GC_VALUE_GET_TAG(v) == JS_TAG_EXCEPTION);
}

static inline JS_BOOL JS_IsUninitialized(GCValue v)
{
    return js_unlikely(GC_VALUE_GET_TAG(v) == JS_TAG_UNINITIALIZED);
}

static inline JS_BOOL JS_IsString(GCValue v)
{
    return GC_VALUE_GET_TAG(v) == JS_TAG_STRING ||
        GC_VALUE_GET_TAG(v) == JS_TAG_STRING_ROPE;
}

static inline JS_BOOL JS_IsSymbol(GCValue v)
{
    return GC_VALUE_GET_TAG(v) == JS_TAG_SYMBOL;
}

static inline JS_BOOL JS_IsObject(GCValue v)
{
    return GC_VALUE_GET_TAG(v) == JS_TAG_OBJECT;
}

GCValue JS_Throw(JSContext *ctx, GCValue obj);
void JS_SetUncatchableException(JSContext *ctx, JS_BOOL flag);
GCValue JS_GetException(JSContext *ctx);
JS_BOOL JS_HasException(JSContext *ctx);
JS_BOOL JS_IsError(JSContext *ctx, GCValue val);
GCValue JS_NewError(JSContext *ctx);
GCValue __js_printf_like(2, 3) JS_ThrowSyntaxError(JSContext *ctx, const char *fmt, ...);
GCValue __js_printf_like(2, 3) JS_ThrowTypeError(JSContext *ctx, const char *fmt, ...);
GCValue __js_printf_like(2, 3) JS_ThrowReferenceError(JSContext *ctx, const char *fmt, ...);
GCValue __js_printf_like(2, 3) JS_ThrowRangeError(JSContext *ctx, const char *fmt, ...);
GCValue __js_printf_like(2, 3) JS_ThrowInternalError(JSContext *ctx, const char *fmt, ...);
GCValue JS_ThrowOutOfMemory(JSContext *ctx);

/* Note: Reference counting functions (JS_FreeValue, JS_DupValue, etc.) removed.
   Using mark-and-sweep GC only. */

JS_BOOL JS_StrictEq(JSContext *ctx, GCValue op1, GCValue op2);
JS_BOOL JS_SameValue(JSContext *ctx, GCValue op1, GCValue op2);
JS_BOOL JS_SameValueZero(JSContext *ctx, GCValue op1, GCValue op2);

int JS_ToBool(JSContext *ctx, GCValue val); /* return -1 for GC_EXCEPTION */
int JS_ToInt32(JSContext *ctx, int32_t *pres, GCValue val);
static inline int JS_ToUint32(JSContext *ctx, uint32_t *pres, GCValue val)
{
    return JS_ToInt32(ctx, (int32_t*)pres, val);
}
int JS_ToInt64(JSContext *ctx, int64_t *pres, GCValue val);
int JS_ToIndex(JSContext *ctx, uint64_t *plen, GCValue val);
int JS_ToFloat64(JSContext *ctx, double *pres, GCValue val);
/* return an exception if 'val' is a Number */
int JS_ToBigInt64(JSContext *ctx, int64_t *pres, GCValue val);
/* same as JS_ToInt64() but allow BigInt */
int JS_ToInt64Ext(JSContext *ctx, int64_t *pres, GCValue val);

GCValue JS_NewStringLen(JSContext *ctx, const char *str1, size_t len1);
static inline GCValue JS_NewString(JSContext *ctx, const char *str)
{
    return JS_NewStringLen(ctx, str, strlen(str));
}
GCValue JS_NewAtomString(JSContext *ctx, const char *str);
GCValue JS_ToString(JSContext *ctx, GCValue val);
GCValue JS_ToPropertyKey(JSContext *ctx, GCValue val);
const char *JS_ToCStringLen2(JSContext *ctx, size_t *plen, GCValue val1, JS_BOOL cesu8);
static inline const char *JS_ToCStringLen(JSContext *ctx, size_t *plen, GCValue val1)
{
    return JS_ToCStringLen2(ctx, plen, val1, 0);
}
static inline const char *JS_ToCString(JSContext *ctx, GCValue val1)
{
    return JS_ToCStringLen2(ctx, NULL, val1, 0);
}
void JS_FreeCString(JSContext *ctx, const char *ptr);

GCValue JS_NewObjectProtoClass(JSContext *ctx, GCValue proto, JSClassID class_id);
GCValue JS_NewObjectClass(JSContext *ctx, int class_id);
GCValue JS_NewObjectProto(JSContext *ctx, GCValue proto);
GCValue JS_NewObject(JSContext *ctx);

JS_BOOL JS_IsFunction(JSContext* ctx, GCValue val);
JS_BOOL JS_IsConstructor(JSContext* ctx, GCValue val);
JS_BOOL JS_SetConstructorBit(JSContext *ctx, GCValue func_obj, JS_BOOL val);

GCValue JS_NewArray(JSContext *ctx);
int JS_IsArray(JSContext *ctx, GCValue val);

GCValue JS_NewDate(JSContext *ctx, double epoch_ms);

GCValue JS_GetPropertyInternal(JSContext *ctx, GCValue obj,
                               JSAtom prop, GCValue receiver,
                               JS_BOOL throw_ref_error);
static js_force_inline GCValue JS_GetProperty(JSContext *ctx, GCValue this_obj,
                                              JSAtom prop)
{
    return JS_GetPropertyInternal(ctx, this_obj, prop, this_obj, 0);
}
GCValue JS_GetPropertyStr(JSContext *ctx, GCValue this_obj,
                          const char *prop);
GCValue JS_GetPropertyUint32(JSContext *ctx, GCValue this_obj,
                             uint32_t idx);

int JS_SetPropertyInternal(JSContext *ctx, GCValue obj,
                           JSAtom prop, GCValue val, GCValue this_obj,
                           int flags);
static inline int JS_SetProperty(JSContext *ctx, GCValue this_obj,
                                 JSAtom prop, GCValue val)
{
    return JS_SetPropertyInternal(ctx, this_obj, prop, val, this_obj, JS_PROP_THROW);
}
int JS_SetPropertyUint32(JSContext *ctx, GCValue this_obj,
                         uint32_t idx, GCValue val);
int JS_SetPropertyInt64(JSContext *ctx, GCValue this_obj,
                        int64_t idx, GCValue val);
int JS_SetPropertyStr(JSContext *ctx, GCValue this_obj,
                      const char *prop, GCValue val);
int JS_HasProperty(JSContext *ctx, GCValue this_obj, JSAtom prop);
int JS_IsExtensible(JSContext *ctx, GCValue obj);
int JS_PreventExtensions(JSContext *ctx, GCValue obj);
int JS_DeleteProperty(JSContext *ctx, GCValue obj, JSAtom prop, int flags);
int JS_SetPrototype(JSContext *ctx, GCValue obj, GCValue proto_val);
GCValue JS_GetPrototype(JSContext *ctx, GCValue val);

#define JS_GPN_STRING_MASK  (1 << 0)
#define JS_GPN_SYMBOL_MASK  (1 << 1)
#define JS_GPN_PRIVATE_MASK (1 << 2)
/* only include the enumerable properties */
#define JS_GPN_ENUM_ONLY    (1 << 4)
/* set theJSPropertyEnum.is_enumerable field */
#define JS_GPN_SET_ENUM     (1 << 5)

int JS_GetOwnPropertyNames(JSContext *ctx, JSPropertyEnum **ptab,
                           uint32_t *plen, GCValue obj, int flags);
void JS_FreePropertyEnum(JSContext *ctx, JSPropertyEnum *tab,
                         uint32_t len);
int JS_GetOwnProperty(JSContext *ctx, JSPropertyDescriptor *desc,
                      GCValue obj, JSAtom prop);

GCValue JS_Call(JSContext *ctx, GCValue func_obj, GCValue this_obj,
                int argc, GCValue *argv);
GCValue JS_Invoke(JSContext *ctx, GCValue this_val, JSAtom atom,
                  int argc, GCValue *argv);
GCValue JS_CallConstructor(JSContext *ctx, GCValue func_obj,
                           int argc, GCValue *argv);
GCValue JS_CallConstructor2(JSContext *ctx, GCValue func_obj,
                            GCValue new_target,
                            int argc, GCValue *argv);
JS_BOOL JS_DetectModule(const char *input, size_t input_len);
/* 'input' must be zero terminated i.e. input[input_len] = '\0'. */
GCValue JS_Eval(JSContext *ctx, const char *input, size_t input_len,
                const char *filename, int eval_flags);
/* same as JS_Eval() but with an explicit 'this_obj' parameter */
GCValue JS_EvalThis(JSContext *ctx, GCValue this_obj,
                    const char *input, size_t input_len,
                    const char *filename, int eval_flags);
GCValue JS_GetGlobalObject(JSContext *ctx);
int JS_IsInstanceOf(JSContext *ctx, GCValue val, GCValue obj);
int JS_DefineProperty(JSContext *ctx, GCValue this_obj,
                      JSAtom prop, GCValue val,
                      GCValue getter, GCValue setter, int flags);
int JS_DefinePropertyValue(JSContext *ctx, GCValue this_obj,
                           JSAtom prop, GCValue val, int flags);
int JS_DefinePropertyValueUint32(JSContext *ctx, GCValue this_obj,
                                 uint32_t idx, GCValue val, int flags);
int JS_DefinePropertyValueStr(JSContext *ctx, GCValue this_obj,
                              const char *prop, GCValue val, int flags);
int JS_DefinePropertyGetSet(JSContext *ctx, GCValue this_obj,
                            JSAtom prop, GCValue getter, GCValue setter,
                            int flags);
void JS_SetOpaque(GCValue obj, void *opaque);
void *JS_GetOpaque(GCValue obj, JSClassID class_id);
void *JS_GetOpaque2(JSContext *ctx, GCValue obj, JSClassID class_id);
void *JS_GetAnyOpaque(GCValue obj, JSClassID *class_id);

/* 'buf' must be zero terminated i.e. buf[buf_len] = '\0'. */
GCValue JS_ParseJSON(JSContext *ctx, const char *buf, size_t buf_len,
                     const char *filename);
#define JS_PARSE_JSON_EXT (1 << 0) /* allow extended JSON */
GCValue JS_ParseJSON2(JSContext *ctx, const char *buf, size_t buf_len,
                      const char *filename, int flags);
GCValue JS_JSONStringify(JSContext *ctx, GCValue obj,
                         GCValue replacer, GCValue space0);

typedef void JSFreeArrayBufferDataFunc(JSRuntime *rt, void *opaque, void *ptr);
GCValue JS_NewArrayBuffer(JSContext *ctx, uint8_t *buf, size_t len,
                          JSFreeArrayBufferDataFunc *free_func, void *opaque,
                          JS_BOOL is_shared);
GCValue JS_NewArrayBufferCopy(JSContext *ctx, const uint8_t *buf, size_t len);
void JS_DetachArrayBuffer(JSContext *ctx, GCValue obj);
uint8_t *JS_GetArrayBuffer(JSContext *ctx, size_t *psize, GCValue obj);

typedef enum JSTypedArrayEnum {
    JS_TYPED_ARRAY_UINT8C = 0,
    JS_TYPED_ARRAY_INT8,
    JS_TYPED_ARRAY_UINT8,
    JS_TYPED_ARRAY_INT16,
    JS_TYPED_ARRAY_UINT16,
    JS_TYPED_ARRAY_INT32,
    JS_TYPED_ARRAY_UINT32,
    JS_TYPED_ARRAY_BIG_INT64,
    JS_TYPED_ARRAY_BIG_UINT64,
    JS_TYPED_ARRAY_FLOAT16,
    JS_TYPED_ARRAY_FLOAT32,
    JS_TYPED_ARRAY_FLOAT64,
} JSTypedArrayEnum;

GCValue JS_NewTypedArray(JSContext *ctx, int argc, GCValue *argv,
                         JSTypedArrayEnum array_type);
GCValue JS_GetTypedArrayBuffer(JSContext *ctx, GCValue obj,
                               size_t *pbyte_offset,
                               size_t *pbyte_length,
                               size_t *pbytes_per_element);
typedef struct {
    void *(*sab_alloc)(void *opaque, size_t size);
    void (*sab_free)(void *opaque, void *ptr);
    void (*sab_dup)(void *opaque, void *ptr);
    void *sab_opaque;
} JSSharedArrayBufferFunctions;
void JS_SetSharedArrayBufferFunctions(JSRuntime *rt,
                                      const JSSharedArrayBufferFunctions *sf);

typedef enum JSPromiseStateEnum {
    JS_PROMISE_PENDING,
    JS_PROMISE_FULFILLED,
    JS_PROMISE_REJECTED,
} JSPromiseStateEnum;

GCValue JS_NewPromiseCapability(JSContext *ctx, GCValue *resolving_funcs);
JSPromiseStateEnum JS_PromiseState(JSContext *ctx, GCValue promise);
GCValue JS_PromiseResult(JSContext *ctx, GCValue promise);

/* is_handled = TRUE means that the rejection is handled */
typedef void JSHostPromiseRejectionTracker(JSContext *ctx, GCValue promise,
                                           GCValue reason,
                                           JS_BOOL is_handled, void *opaque);
void JS_SetHostPromiseRejectionTracker(JSRuntime *rt, JSHostPromiseRejectionTracker *cb, void *opaque);

/* if can_block is TRUE, Atomics.wait() can be used */
void JS_SetCanBlock(JSRuntime *rt, JS_BOOL can_block);
/* select which debug info is stripped from the compiled code */
#define JS_STRIP_SOURCE (1 << 0) /* strip source code */
#define JS_STRIP_DEBUG  (1 << 1) /* strip all debug info including source code */
void JS_SetStripInfo(JSRuntime *rt, int flags);
int JS_GetStripInfo(JSRuntime *rt);

/* set the [IsHTMLDDA] internal slot */
void JS_SetIsHTMLDDA(JSContext *ctx, GCValue obj);

typedef struct JSModuleDef JSModuleDef;

/* return the module specifier (allocated with gc_alloc) or NULL if
   exception */
typedef char *JSModuleNormalizeFunc(JSContext *ctx,
                                    const char *module_base_name,
                                    const char *module_name, void *opaque);
typedef JSModuleDef *JSModuleLoaderFunc(JSContext *ctx,
                                        const char *module_name, void *opaque);
typedef JSModuleDef *JSModuleLoaderFunc2(JSContext *ctx,
                                         const char *module_name, void *opaque,
                                         GCValue attributes);
/* return -1 if exception, 0 if OK */
typedef int JSModuleCheckSupportedImportAttributes(JSContext *ctx, void *opaque,
                                                   GCValue attributes);
                                                   
/* module_normalize = NULL is allowed and invokes the default module
   filename normalizer */
void JS_SetModuleLoaderFunc(JSRuntime *rt,
                            JSModuleNormalizeFunc *module_normalize,
                            JSModuleLoaderFunc *module_loader, void *opaque);
/* same as JS_SetModuleLoaderFunc but with attributes. if
   module_check_attrs = NULL, no attribute checking is done. */
void JS_SetModuleLoaderFunc2(JSRuntime *rt,
                             JSModuleNormalizeFunc *module_normalize,
                             JSModuleLoaderFunc2 *module_loader,
                             JSModuleCheckSupportedImportAttributes *module_check_attrs,
                             void *opaque);
/* return the import.meta object of a module */
GCValue JS_GetImportMeta(JSContext *ctx, JSModuleDef *m);
JSAtom JS_GetModuleName(JSContext *ctx, JSModuleDef *m);
GCValue JS_GetModuleNamespace(JSContext *ctx, JSModuleDef *m);

/* JS Job support */

typedef GCValue JSJobFunc(JSContext *ctx, int argc, GCValue *argv);
int JS_EnqueueJob(JSContext *ctx, JSJobFunc *job_func, int argc, GCValue *argv);

JS_BOOL JS_IsJobPending(JSRuntime *rt);
int JS_ExecutePendingJob(JSRuntime *rt, JSContext **pctx);

/* Object Writer/Reader (currently only used to handle precompiled code) */
#define JS_WRITE_OBJ_BYTECODE  (1 << 0) /* allow function/module */
#define JS_WRITE_OBJ_BSWAP     (1 << 1) /* byte swapped output */
#define JS_WRITE_OBJ_SAB       (1 << 2) /* allow SharedArrayBuffer */
#define JS_WRITE_OBJ_REFERENCE (1 << 3) /* allow object references to
                                           encode arbitrary object
                                           graph */
uint8_t *JS_WriteObject(JSContext *ctx, size_t *psize, GCValue obj,
                        int flags);
uint8_t *JS_WriteObject2(JSContext *ctx, size_t *psize, GCValue obj,
                         int flags, uint8_t ***psab_tab, size_t *psab_tab_len);

#define JS_READ_OBJ_BYTECODE  (1 << 0) /* allow function/module */
#define JS_READ_OBJ_ROM_DATA  (1 << 1) /* avoid duplicating 'buf' data */
#define JS_READ_OBJ_SAB       (1 << 2) /* allow SharedArrayBuffer */
#define JS_READ_OBJ_REFERENCE (1 << 3) /* allow object references */
GCValue JS_ReadObject(JSContext *ctx, const uint8_t *buf, size_t buf_len,
                      int flags);
/* instantiate and evaluate a bytecode function. Only used when
   reading a script or module with JS_ReadObject() */
GCValue JS_EvalFunction(JSContext *ctx, GCValue fun_obj);
/* load the dependencies of the module 'obj'. Useful when JS_ReadObject()
   returns a module. */
int JS_ResolveModule(JSContext *ctx, GCValue obj);

/* only exported for os.Worker() */
JSAtom JS_GetScriptOrModuleName(JSContext *ctx, int n_stack_levels);
/* only exported for os.Worker() */
GCValue JS_LoadModule(JSContext *ctx, const char *basename,
                      const char *filename);

/* C function definition */
typedef enum JSCFunctionEnum {  /* XXX: should rename for namespace isolation */
    JS_CFUNC_generic,
    JS_CFUNC_generic_magic,
    JS_CFUNC_constructor,
    JS_CFUNC_constructor_magic,
    JS_CFUNC_constructor_or_func,
    JS_CFUNC_constructor_or_func_magic,
    JS_CFUNC_f_f,
    JS_CFUNC_f_f_f,
    JS_CFUNC_getter,
    JS_CFUNC_setter,
    JS_CFUNC_getter_magic,
    JS_CFUNC_setter_magic,
    JS_CFUNC_iterator_next,
} JSCFunctionEnum;

typedef union JSCFunctionType {
    JSCFunction *generic;
    GCValue (*generic_magic)(JSContext *ctx, GCValue this_val, int argc, GCValue *argv, int magic);
    JSCFunction *constructor;
    GCValue (*constructor_magic)(JSContext *ctx, GCValue new_target, int argc, GCValue *argv, int magic);
    JSCFunction *constructor_or_func;
    double (*f_f)(double);
    double (*f_f_f)(double, double);
    GCValue (*getter)(JSContext *ctx, GCValue this_val);
    GCValue (*setter)(JSContext *ctx, GCValue this_val, GCValue val);
    GCValue (*getter_magic)(JSContext *ctx, GCValue this_val, int magic);
    GCValue (*setter_magic)(JSContext *ctx, GCValue this_val, GCValue val, int magic);
    GCValue (*iterator_next)(JSContext *ctx, GCValue this_val,
                             int argc, GCValue *argv, int *pdone, int magic);
} JSCFunctionType;

GCValue JS_NewCFunction2(JSContext *ctx, JSCFunction *func,
                         const char *name,
                         int length, JSCFunctionEnum cproto, int magic);
GCValue JS_NewCFunctionData(JSContext *ctx, JSCFunctionData *func,
                            int length, int magic, int data_len,
                            GCValue *data);

static inline GCValue JS_NewCFunction(JSContext *ctx, JSCFunction *func, const char *name,
                                      int length)
{
    return JS_NewCFunction2(ctx, func, name, length, JS_CFUNC_generic, 0);
}

static inline GCValue JS_NewCFunctionMagic(JSContext *ctx, JSCFunctionMagic *func,
                                           const char *name,
                                           int length, JSCFunctionEnum cproto, int magic)
{
    /* Used to squelch a -Wcast-function-type warning. */
    JSCFunctionType ft = { .generic_magic = func };
    return JS_NewCFunction2(ctx, ft.generic, name, length, cproto, magic);
}
int JS_SetConstructor(JSContext *ctx, GCValue func_obj,
                      GCValue proto);

/* C property definition */

typedef struct JSCFunctionListEntry {
    const char *name;
    uint8_t prop_flags;
    uint8_t def_type;
    int16_t magic;
    union {
        struct {
            uint8_t length; /* XXX: should move outside union */
            uint8_t cproto; /* XXX: should move outside union */
            JSCFunctionType cfunc;
        } func;
        struct {
            JSCFunctionType get;
            JSCFunctionType set;
        } getset;
        struct {
            const char *name;
            int base;
        } alias;
        struct {
            const struct JSCFunctionListEntry *tab;
            int len;
        } prop_list;
        const char *str;
        int32_t i32;
        int64_t i64;
        double f64;
    } u;
} JSCFunctionListEntry;

#define JS_DEF_CFUNC          0
#define JS_DEF_CGETSET        1
#define JS_DEF_CGETSET_MAGIC  2
#define JS_DEF_PROP_STRING    3
#define JS_DEF_PROP_INT32     4
#define JS_DEF_PROP_INT64     5
#define JS_DEF_PROP_DOUBLE    6
#define JS_DEF_PROP_UNDEFINED 7
#define JS_DEF_OBJECT         8
#define JS_DEF_ALIAS          9
#define JS_DEF_PROP_ATOM     10
#define JS_DEF_PROP_BOOL     11

/* Note: c++ does not like nested designators */
#define JS_CFUNC_DEF(name, length, func1) { name, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE, JS_DEF_CFUNC, 0, .u = { .func = { length, JS_CFUNC_generic, { .generic = func1 } } } }
#define JS_CFUNC_MAGIC_DEF(name, length, func1, magic) { name, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE, JS_DEF_CFUNC, magic, .u = { .func = { length, JS_CFUNC_generic_magic, { .generic_magic = func1 } } } }
#define JS_CFUNC_SPECIAL_DEF(name, length, cproto, func1) { name, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE, JS_DEF_CFUNC, 0, .u = { .func = { length, JS_CFUNC_ ## cproto, { .cproto = func1 } } } }
#define JS_ITERATOR_NEXT_DEF(name, length, func1, magic) { name, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE, JS_DEF_CFUNC, magic, .u = { .func = { length, JS_CFUNC_iterator_next, { .iterator_next = func1 } } } }
#define JS_CGETSET_DEF(name, fgetter, fsetter) { name, JS_PROP_CONFIGURABLE, JS_DEF_CGETSET, 0, .u = { .getset = { .get = { .getter = fgetter }, .set = { .setter = fsetter } } } }
#define JS_CGETSET_MAGIC_DEF(name, fgetter, fsetter, magic) { name, JS_PROP_CONFIGURABLE, JS_DEF_CGETSET_MAGIC, magic, .u = { .getset = { .get = { .getter_magic = fgetter }, .set = { .setter_magic = fsetter } } } }
#define JS_PROP_STRING_DEF(name, cstr, prop_flags) { name, prop_flags, JS_DEF_PROP_STRING, 0, .u = { .str = cstr } }
#define JS_PROP_INT32_DEF(name, val, prop_flags) { name, prop_flags, JS_DEF_PROP_INT32, 0, .u = { .i32 = val } }
#define JS_PROP_INT64_DEF(name, val, prop_flags) { name, prop_flags, JS_DEF_PROP_INT64, 0, .u = { .i64 = val } }
#define JS_PROP_DOUBLE_DEF(name, val, prop_flags) { name, prop_flags, JS_DEF_PROP_DOUBLE, 0, .u = { .f64 = val } }
#define JS_PROP_UNDEFINED_DEF(name, prop_flags) { name, prop_flags, JS_DEF_PROP_UNDEFINED, 0, .u = { .i32 = 0 } }
#define JS_PROP_ATOM_DEF(name, val, prop_flags) { name, prop_flags, JS_DEF_PROP_ATOM, 0, .u = { .i32 = val } }
#define JS_PROP_BOOL_DEF(name, val, prop_flags) { name, prop_flags, JS_DEF_PROP_BOOL, 0, .u = { .i32 = val } }
#define JS_OBJECT_DEF(name, tab, len, prop_flags) { name, prop_flags, JS_DEF_OBJECT, 0, .u = { .prop_list = { tab, len } } }
#define JS_ALIAS_DEF(name, from) { name, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE, JS_DEF_ALIAS, 0, .u = { .alias = { from, -1 } } }
#define JS_ALIAS_BASE_DEF(name, from, base) { name, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE, JS_DEF_ALIAS, 0, .u = { .alias = { from, base } } }

int JS_SetPropertyFunctionList(JSContext *ctx, GCValue obj,
                               const JSCFunctionListEntry *tab,
                               int len);

/* C module definition */

typedef int JSModuleInitFunc(JSContext *ctx, JSModuleDef *m);

JSModuleDef *JS_NewCModule(JSContext *ctx, const char *name_str,
                           JSModuleInitFunc *func);
/* can only be called before the module is instantiated */
int JS_AddModuleExport(JSContext *ctx, JSModuleDef *m, const char *name_str);
int JS_AddModuleExportList(JSContext *ctx, JSModuleDef *m,
                           const JSCFunctionListEntry *tab, int len);
/* can only be called after the module is instantiated */
int JS_SetModuleExport(JSContext *ctx, JSModuleDef *m, const char *export_name,
                       GCValue val);
int JS_SetModuleExportList(JSContext *ctx, JSModuleDef *m,
                           const JSCFunctionListEntry *tab, int len);
/* associate a GCValue to a C module */
int JS_SetModulePrivateValue(JSContext *ctx, JSModuleDef *m, GCValue val);
GCValue JS_GetModulePrivateValue(JSContext *ctx, JSModuleDef *m);
                        
/* debug value output */

typedef struct {
    JS_BOOL show_hidden : 8; /* only show enumerable properties */
    JS_BOOL raw_dump : 8; /* avoid doing autoinit and avoid any malloc() call (for internal use) */
    uint32_t max_depth; /* recurse up to this depth, 0 = no limit */
    uint32_t max_string_length; /* print no more than this length for
                                   strings, 0 = no limit */
    uint32_t max_item_count; /*  print no more than this count for
                                 arrays or objects, 0 = no limit */
} JSPrintValueOptions;

typedef void JSPrintValueWrite(void *opaque, const char *buf, size_t len);

void JS_PrintValueSetDefaultOptions(JSPrintValueOptions *options);
void JS_PrintValueRT(JSRuntime *rt, JSPrintValueWrite *write_func, void *write_opaque,
                     GCValue val, const JSPrintValueOptions *options);
void JS_PrintValue(JSContext *ctx, JSPrintValueWrite *write_func, void *write_opaque,
                   GCValue val, const JSPrintValueOptions *options);

#undef js_unlikely
#undef js_force_inline

#ifdef __cplusplus
} /* extern "C" { */
#endif

#endif /* QUICKJS_H */
