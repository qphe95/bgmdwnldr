/*
 * GC Handle Access Macros - Safe handle-based object field access
 * 
 * This header provides macros for accessing GC-managed object fields
 * through GCHandles instead of raw pointers. This ensures object
 * references remain valid across GC compaction.
 * 
 * CRITICAL RULE: Never store the result of gc_deref(). Always use
 * these macros which dereference and access in one operation.
 */

#ifndef GC_HANDLE_ACCESS_H
#define GC_HANDLE_ACCESS_H

#include "third_party/quickjs/quickjs_gc_unified.h"
#include "third_party/quickjs/quickjs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Proto-Chain Walking Macros
 * ============================================================================
 * 
 * These macros enable safe prototype chain traversal without storing
 * raw pointers that could be invalidated by GC.
 * 
 * IMPORTANT: The body code must not trigger GC. If it does, the loop
 * will restart from the beginning using the updated handles.
 */

/* 
 * GC_PROTO_CHAIN_WALK - Walk the prototype chain using handles only.
 * 
 * Parameters:
 *   start_handle - GCHandle to start the walk from (MUST be valid)
 *   current_var - Variable name to hold the current handle in the loop
 *   body - Code block to execute for each object in the chain
 * 
 * The body is executed for each object in the proto chain until:
 * - The body executes 'break' (exit successfully)
 * - The proto chain ends (current_var becomes GC_HANDLE_NULL)
 * - Max depth is reached (safety limit)
 * 
 * Example:
 *   GCHandle obj_h = GC_VALUE_TO_HANDLE(obj_val);
 *   GC_PROTO_CHAIN_WALK(obj_h, current_h, {
 *       JSShapeProperty *prs;
 *       int idx = find_own_property_handle(current_h, prop, &prs);
 *       if (idx >= 0) {
 *           result = get_property_value(current_h, idx);
 *           break;  // Exit the walk
 *       }
 *   });
 */
#define GC_PROTO_CHAIN_WALK(start_handle, current_var, body) \
    do { \
        GCHandle current_var = (start_handle); \
        int _depth = 0; \
        const int _max_depth = 1000; \
        while (current_var != GC_HANDLE_NULL && _depth < _max_depth) { \
            body \
            /* Move to next prototype */ \
            GCHandle _shape_h = GC_OBJ_GET_SHAPE_HANDLE(current_var); \
            current_var = GC_SHAPE_GET_PROTO_HANDLE(_shape_h); \
            _depth++; \
        } \
    } while(0)

/* 
 * GC_PROTO_CHAIN_WALK_WITH_SHAPE - Walk proto chain with both object and shape handles.
 * 
 * This variant provides both the object handle and its shape handle,
 * saving redundant dereferences when both are needed.
 * 
 * Parameters:
 *   start_handle - GCHandle to start from
 *   obj_var - Variable name for current object handle
 *   shape_var - Variable name for current shape handle
 *   body - Code block to execute
 */
#define GC_PROTO_CHAIN_WALK_WITH_SHAPE(start_handle, obj_var, shape_var, body) \
    do { \
        GCHandle obj_var = (start_handle); \
        GCHandle shape_var = GC_HANDLE_NULL; \
        int _depth = 0; \
        const int _max_depth = 1000; \
        while (obj_var != GC_HANDLE_NULL && _depth < _max_depth) { \
            shape_var = GC_OBJ_GET_SHAPE_HANDLE(obj_var); \
            if (shape_var == GC_HANDLE_NULL) break; \
            body \
            obj_var = GC_SHAPE_GET_PROTO_HANDLE(shape_var); \
            _depth++; \
        } \
    } while(0)

/* 
 * GC_PROTO_CHAIN_WALK_UNTIL - Walk until a condition is met.
 * 
 * Parameters:
 *   start_handle - GCHandle to start from
 *   current_var - Variable for current handle
 *   condition - Expression that returns true to stop walking
 *   result_var - Set to 1 if condition met, 0 if chain exhausted
 */
#define GC_PROTO_CHAIN_WALK_UNTIL(start_handle, current_var, condition, result_var) \
    do { \
        result_var = 0; \
        GCHandle current_var = (start_handle); \
        int _depth = 0; \
        const int _max_depth = 1000; \
        while (current_var != GC_HANDLE_NULL && _depth < _max_depth) { \
            if (condition) { \
                result_var = 1; \
                break; \
            } \
            GCHandle _shape_h = GC_OBJ_GET_SHAPE_HANDLE(current_var); \
            current_var = GC_SHAPE_GET_PROTO_HANDLE(_shape_h); \
            _depth++; \
        } \
    } while(0)

/* ============================================================================
 * Property Access via Handles
 * ============================================================================
 */

/* 
 * Get property array element pointer (temporary - don't store!)
 * Returns NULL if prop_handle is invalid or index is out of bounds.
 */
#define GC_PROP_ARRAY_GET_PTR(prop_handle, index) ({ \
    JSProperty *_pr = NULL; \
    JSProperty *_props = (JSProperty *)gc_deref(prop_handle); \
    if (_props != NULL) { \
        _pr = &_props[index]; \
    } \
    _pr; \
})

/* Get property value by index (handles all property types) */
#define GC_PROP_ARRAY_GET_VALUE(prop_handle, index) ({ \
    GCValue _val = GC_UNDEFINED; \
    JSProperty *_props = (JSProperty *)gc_deref(prop_handle); \
    if (_props != NULL) { \
        _val = _props[index].u.value; \
    } \
    _val; \
})

/* Set property value by index */
#define GC_PROP_ARRAY_SET_VALUE(prop_handle, index, val) do { \
    JSProperty *_props = (JSProperty *)gc_deref(prop_handle); \
    if (_props != NULL) { \
        _props[index].u.value = (val); \
    } \
} while(0)

/* Get getter handle from property */
#define GC_PROP_ARRAY_GET_GETTER(prop_handle, index) ({ \
    GCHandle _h = GC_HANDLE_NULL; \
    JSProperty *_props = (JSProperty *)gc_deref(prop_handle); \
    if (_props != NULL) { \
        _h = _props[index].u.getset.getter_handle; \
    } \
    _h; \
})

/* Set getter handle in property */
#define GC_PROP_ARRAY_SET_GETTER(prop_handle, index, h) do { \
    JSProperty *_props = (JSProperty *)gc_deref(prop_handle); \
    if (_props != NULL) { \
        _props[index].u.getset.getter_handle = (h); \
    } \
} while(0)

/* Get setter handle from property */
#define GC_PROP_ARRAY_GET_SETTER(prop_handle, index) ({ \
    GCHandle _h = GC_HANDLE_NULL; \
    JSProperty *_props = (JSProperty *)gc_deref(prop_handle); \
    if (_props != NULL) { \
        _h = _props[index].u.getset.setter_handle; \
    } \
    _h; \
})

/* Set setter handle in property */
#define GC_PROP_ARRAY_SET_SETTER(prop_handle, index, h) do { \
    JSProperty *_props = (JSProperty *)gc_deref(prop_handle); \
    if (_props != NULL) { \
        _props[index].u.getset.setter_handle = (h); \
    } \
} while(0)

/* Get var_ref from property */
#define GC_PROP_ARRAY_GET_VAR_REF(prop_handle, index) ({ \
    JSVarRef *_ref = NULL; \
    JSProperty *_props = (JSProperty *)gc_deref(prop_handle); \
    if (_props != NULL) { \
        _ref = _props[index].u.var_ref; \
    } \
    _ref; \
})

/* ============================================================================
 * Shape Property Access via Handles
 * ============================================================================
 */

/* Get shape property atom */
#define GC_SHAPE_PROP_GET_ATOM(shape_handle, index) ({ \
    JSAtom _atom = JS_ATOM_NULL; \
    JSShape *_sh = (JSShape *)gc_deref(shape_handle); \
    if (_sh != NULL) { \
        JSShapeProperty *_props = get_shape_prop(_sh); \
        _atom = _props[index].atom; \
    } \
    _atom; \
})

/* Get shape property flags */
#define GC_SHAPE_PROP_GET_FLAGS(shape_handle, index) ({ \
    uint32_t _flags = 0; \
    JSShape *_sh = (JSShape *)gc_deref(shape_handle); \
    if (_sh != NULL) { \
        JSShapeProperty *_props = get_shape_prop(_sh); \
        _flags = _props[index].flags; \
    } \
    _flags; \
})

/* Get shape property hash_next */
#define GC_SHAPE_PROP_GET_HASH_NEXT(shape_handle, index) ({ \
    uint32_t _next = 0; \
    JSShape *_sh = (JSShape *)gc_deref(shape_handle); \
    if (_sh != NULL) { \
        JSShapeProperty *_props = get_shape_prop(_sh); \
        _next = _props[index].hash_next; \
    } \
    _next; \
})

/* ============================================================================
 * Fast Array Access via Handles
 * ============================================================================
 * Fast arrays use JSObject.u.array for indexed storage.
 * These macros provide safe access to fast array fields.
 */

/* Get array count (u.array.count) via handle */
#define GC_OBJ_GET_ARRAY_COUNT(obj_handle) ({ \
    uint32_t _count = 0; \
    JSObject *_obj = (JSObject *)gc_deref(obj_handle); \
    if (_obj != NULL) { \
        _count = _obj->u.array.count; \
    } \
    _count; \
})

/* Set array count via handle */
#define GC_OBJ_SET_ARRAY_COUNT(obj_handle, val) do { \
    JSObject *_obj = (JSObject *)gc_deref(obj_handle); \
    if (_obj != NULL) { \
        _obj->u.array.count = (val); \
    } \
} while(0)

/* Get array values pointer (temporary - don't store across GC!) */
#define GC_OBJ_GET_ARRAY_VALUES(obj_handle) ({ \
    GCValue *_values = NULL; \
    JSObject *_obj = (JSObject *)gc_deref(obj_handle); \
    if (_obj != NULL) { \
        _values = _obj->u.array.values; \
    } \
    _values; \
})

/* Get array element at index via handle */
#define GC_OBJ_GET_ARRAY_ELEMENT(obj_handle, idx) ({ \
    GCValue _val = GC_UNDEFINED; \
    JSObject *_obj = (JSObject *)gc_deref(obj_handle); \
    if (_obj != NULL && _obj->u.array.values != NULL) { \
        _val = _obj->u.array.values[idx]; \
    } \
    _val; \
})

/* Set array element at index via handle */
#define GC_OBJ_SET_ARRAY_ELEMENT(obj_handle, idx, val) do { \
    JSObject *_obj = (JSObject *)gc_deref(obj_handle); \
    if (_obj != NULL && _obj->u.array.values != NULL) { \
        _obj->u.array.values[idx] = (val); \
    } \
} while(0)

/* ============================================================================
 * Typed Array Access via Handles
 * ============================================================================
 */

/* Get typed array buffer pointer (temporary - don't store across GC!) */
#define GC_OBJ_GET_TYPED_ARRAY_PTR(obj_handle, field, ptr_type) ({ \
    ptr_type _ptr = NULL; \
    JSObject *_obj = (JSObject *)gc_deref(obj_handle); \
    if (_obj != NULL) { \
        _ptr = _obj->u.field; \
    } \
    _ptr; \
})

/* ============================================================================
 * Convenience Functions (declarations - implementations in quickjs.c)
 * ============================================================================
 */

/* 
 * Find property using handle-based lookup. Returns property index or -1.
 * Defined in quickjs.c after prop_hash_end and get_shape_prop are available.
 */
extern int find_own_property_handle(GCHandle obj_handle, JSAtom atom,
                                     JSShapeProperty **pprs);

/* 
 * Get property pointer by index (temporary - don't store across GC!).
 * Defined in quickjs.c.
 */
extern JSProperty *get_property_by_index(GCHandle obj_handle, int prop_idx);

/* 
 * Check if object has a prototype.
 * Defined in quickjs.c.
 */
extern int gc_obj_has_prototype(GCHandle obj_handle);

/* 
 * Get prototype handle of an object.
 * Defined in quickjs.c.
 */
extern GCHandle gc_obj_get_prototype_handle(GCHandle obj_handle);

#ifdef __cplusplus
}
#endif

#endif /* GC_HANDLE_ACCESS_H */
