/*
 * Unified GC Allocator for QuickJS - CLEAN INTERFACE
 * 
 * All allocations are GC-managed objects accessed through handles.
 * No pinning, no raw memory, no manual management.
 */

#ifndef QUICKJS_GC_UNIFIED_H
#define QUICKJS_GC_UNIFIED_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

struct JSRuntime;
typedef struct JSRuntime JSRuntime;
struct JSContext;
typedef struct JSContext JSContext;

typedef uint32_t GCHandle;
#define GC_HANDLE_NULL 0

#define GC_HEAP_SIZE (512 * 1024 * 1024)
#define GC_INITIAL_HANDLES 100000
#define GC_DEFAULT_THRESHOLD (256 * 1024)

typedef enum {
    JS_GC_OBJ_TYPE_JS_OBJECT = 0,
    JS_GC_OBJ_TYPE_FUNCTION_BYTECODE,
    JS_GC_OBJ_TYPE_SHAPE,
    JS_GC_OBJ_TYPE_VAR_REF,
    JS_GC_OBJ_TYPE_ASYNC_FUNCTION,
    JS_GC_OBJ_TYPE_JS_CONTEXT,
    JS_GC_OBJ_TYPE_JS_RUNTIME,
    JS_GC_OBJ_TYPE_MODULE,
    JS_GC_OBJ_TYPE_JOB_ENTRY,
    JS_GC_OBJ_TYPE_JS_STRING,
    JS_GC_OBJ_TYPE_JS_STRING_ROPE,
    JS_GC_OBJ_TYPE_JS_BIGINT,
    JS_GC_OBJ_TYPE_DATA,
    JS_GC_OBJ_TYPE_COUNT,
} JSGCObjectTypeEnum;

/*
 * GC Finalizer function type.
 * Called during garbage collection when an object is being freed.
 * The finalizer should clean up external resources (file handles, 
 * SharedArrayBuffer data, etc.) but NOT free the object memory itself
 * (the GC handles that).
 * 
 * Parameters:
 *   rt - The JS runtime
 *   handle - The GCHandle being freed
 *   user_ptr - Pointer to the object's user data (after GCHeader)
 */
typedef void GCFinalizerFunc(JSRuntime *rt, GCHandle handle, void *user_ptr);

/*
 * Set a finalizer for a specific handle.
 * The finalizer will be called during GC sweep phase when this object is freed.
 * Set to NULL to remove the finalizer.
 */
void gc_set_handle_finalizer(GCHandle handle, GCFinalizerFunc *finalizer);

/*
 * Get the finalizer for a specific handle.
 * Returns NULL if no finalizer is registered.
 */
GCFinalizerFunc *gc_get_handle_finalizer(GCHandle handle);

/*
 * Run finalizer for a specific object if one is registered.
 * Called internally by the GC during sweep.
 */
void gc_run_finalizer(JSRuntime *rt, GCHandle handle);

typedef enum {
    GC_HANDLE_ARRAY_GC,
    GC_HANDLE_ARRAY_CONTEXT,
    GC_HANDLE_ARRAY_ATOM,
    GC_HANDLE_ARRAY_JOB,
    GC_HANDLE_ARRAY_WEAKREF,
} GCHandleArrayType;

typedef struct GCHeader {
    int ref_count_unused;
    unsigned int gc_obj_type : 4;
    unsigned int mark : 1;
    unsigned int dummy0 : 3;
    uint8_t dummy1;
    uint16_t dummy2;
    struct {
        void *next;
        void *prev;
    } link;
    uint32_t handle;
    uint32_t size;
    uint8_t flags;
    uint16_t pad;
    GCFinalizerFunc *finalizer;  /* Per-handle finalizer - called when object is freed */
} GCHeader;

bool gc_init(void);
bool gc_is_initialized(void);
void gc_cleanup(void);
void gc_set_runtime(JSRuntime *rt);

GCHandle gc_alloc(size_t size, JSGCObjectTypeEnum gc_obj_type);
GCHandle gc_alloc_ex(size_t size, JSGCObjectTypeEnum gc_obj_type,
                     GCHandleArrayType array_type);
GCHandle gc_realloc(GCHandle handle, size_t new_size);

/* Allocate a handle for an existing pointer (used during pointer-to-handle migration) */
GCHandle gc_alloc_handle_for_ptr(void *ptr);

/* Like gc_realloc but returns slack (extra usable space) for array optimization */
GCHandle gc_realloc2(GCHandle handle, size_t new_size, size_t *pslack);

/* Forward declaration needed by inline functions below */
void *gc_deref(GCHandle handle);
static inline GCHeader *gc_header(void *user_ptr) {
    if (!user_ptr) return NULL;
    return (GCHeader*)((uint8_t*)user_ptr - sizeof(GCHeader));
}

/* Allocate and zero-initialize */
static inline GCHandle gc_allocz(size_t size, JSGCObjectTypeEnum gc_obj_type) {
    GCHandle handle = gc_alloc(size, gc_obj_type);
    if (handle != GC_HANDLE_NULL) {
        void *ptr = gc_deref(handle);
        if (ptr) memset(ptr, 0, size);
    }
    return handle;
}

/* Duplicate a string (null-terminated) */
static inline GCHandle gc_strdup(const char *str) {
    size_t len = str ? strlen(str) : 0;
    GCHandle handle = gc_alloc(len + 1, JS_GC_OBJ_TYPE_DATA);
    if (handle != GC_HANDLE_NULL && str) {
        char *ptr = (char *)gc_deref(handle);
        if (ptr) memcpy(ptr, str, len + 1);
    }
    return handle;
}

/* Duplicate a string with length limit */
static inline GCHandle gc_strndup(const char *s, size_t n) {
    size_t len = s ? strnlen(s, n) : 0;
    GCHandle handle = gc_alloc(len + 1, JS_GC_OBJ_TYPE_DATA);
    if (handle != GC_HANDLE_NULL) {
        char *ptr = (char *)gc_deref(handle);
        if (ptr) {
            if (s) memcpy(ptr, s, len);
            ptr[len] = '\0';
        }
    }
    return handle;
}

/* Get usable size of an allocation */
static inline size_t gc_usable_size(GCHandle handle) {
    void *ptr = gc_deref(handle);
    if (!ptr) return 0;
    GCHeader *hdr = gc_header(ptr);
    return hdr->size > sizeof(GCHeader) ? hdr->size - sizeof(GCHeader) : 0;
}
bool gc_ptr_is_valid(void *ptr);

/* Helper: allocate and return pointer directly (for internal use) */
static inline void *gc_alloc_js_object(size_t size, JSGCObjectTypeEnum gc_obj_type) {
    GCHandle handle = gc_alloc(size, gc_obj_type);
    if (handle == 0) return NULL;
    return gc_deref(handle);
}

static inline void *gc_alloc_js_object_ex(size_t size, JSGCObjectTypeEnum gc_obj_type, 
                                           GCHandleArrayType array_type) {
    GCHandle handle = gc_alloc_ex(size, gc_obj_type, array_type);
    if (handle == 0) return NULL;
    return gc_deref(handle);
}
bool gc_handle_is_valid(GCHandle handle);
JSGCObjectTypeEnum gc_handle_get_type(GCHandle handle);

void gc_run(void);
void gc_reset(void);
void gc_reset_full(void);

size_t gc_used_bytes(void);
size_t gc_available_bytes(void);
size_t gc_total_bytes(void);

void gc_add_root(GCHandle handle);
void gc_remove_root(GCHandle handle);

/*
 * Get the existing GCHandle for a GC-managed pointer.
 * This returns the handle that was allocated when the object was created.
 * Returns GC_HANDLE_NULL if ptr is NULL.
 * 
 * IMPORTANT: This should only be used during object creation or when
 * converting from the old pointer-based API to the handle-based API.
 * Never store the pointer - always store the handle.
 */
static inline GCHandle gc_ptr_to_handle(void *ptr) {
    if (!ptr) return GC_HANDLE_NULL;
    GCHeader *hdr = gc_header(ptr);
    return hdr ? hdr->handle : GC_HANDLE_NULL;
}

typedef struct GCState {
    uint8_t *heap;
    size_t heap_size;
    struct {
        uint8_t *base;
        _Atomic size_t offset;
        size_t capacity;
    } bump;
    struct {
        void **ptrs;
        uint32_t count;
        uint32_t capacity;
    } handles;
    struct {
        GCHandle *roots;
        uint32_t count;
        uint32_t capacity;
    } root_set;
    size_t bytes_allocated;
    size_t gc_threshold;
    JSRuntime *rt;
    bool initialized;
} GCState;

extern GCState g_gc;

#ifdef __cplusplus
}
#endif

#endif
