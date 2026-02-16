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
} GCHeader;

bool gc_init(void);
bool gc_is_initialized(void);
void gc_cleanup(void);
void gc_set_runtime(JSRuntime *rt);

GCHandle gc_alloc(JSRuntime *rt, size_t size, JSGCObjectTypeEnum gc_obj_type);
GCHandle gc_alloc_ex(JSRuntime *rt, size_t size, JSGCObjectTypeEnum gc_obj_type,
                     GCHandleArrayType array_type);
GCHandle gc_realloc(JSRuntime *rt, GCHandle handle, size_t new_size);

void *gc_deref(GCHandle handle);
bool gc_ptr_is_valid(void *ptr);

/* Helper: allocate and return pointer directly (for internal use) */
static inline void *gc_alloc_js_object(size_t size, JSGCObjectTypeEnum gc_obj_type, JSRuntime *rt) {
    GCHandle handle = gc_alloc(rt, size, gc_obj_type);
    if (handle == 0) return NULL;
    return gc_deref(handle);
}

static inline void *gc_alloc_js_object_ex(size_t size, JSGCObjectTypeEnum gc_obj_type, 
                                           JSRuntime *rt, GCHandleArrayType array_type) {
    GCHandle handle = gc_alloc_ex(rt, size, gc_obj_type, array_type);
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

typedef struct GCShadowStackEntry {
    void *value_slot;
    struct GCShadowStackEntry *next;
    struct GCShadowStackEntry *pool_next;
} GCShadowStackEntry;

void gc_shadow_stack_init(void);
void gc_shadow_stack_cleanup(void);
void gc_push_jsvalue(JSContext *ctx, void *value_slot);
void gc_pop_jsvalue(JSContext *ctx, void *value_slot);
void gc_mark_shadow_stack(void);

#define JS_GC_PUSH(ctx, var) gc_push_jsvalue(ctx, &var)
#define JS_GC_POP(ctx, var) gc_pop_jsvalue(ctx, &var)

static inline GCHeader *gc_header(void *user_ptr) {
    if (!user_ptr) return NULL;
    return (GCHeader*)((uint8_t*)user_ptr - sizeof(GCHeader));
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
    GCShadowStackEntry *shadow_stack;
    GCShadowStackEntry *shadow_stack_pool;
    bool initialized;
} GCState;

extern GCState g_gc;

#ifdef __cplusplus
}
#endif

#endif
