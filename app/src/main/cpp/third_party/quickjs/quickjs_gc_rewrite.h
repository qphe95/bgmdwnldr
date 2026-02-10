/*
 * QuickJS Handle-Based GC - Full Integration Header
 * 
 * This header defines the replacement GC system that uses:
 * - Handle table indirection (IDs instead of pointers)
 * - Stack allocator (bump pointer)
 * - Mark-compact collection (no pointer fixup needed)
 */

#ifndef QUICKJS_GC_REWRITE_H
#define QUICKJS_GC_REWRITE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Handle type - 0 is reserved as null */
typedef uint32_t JSObjHandle;
#define JS_OBJ_HANDLE_NULL 0

/* Object type enum */
typedef enum {
    JS_GC_OBJ_TYPE_JS_OBJECT = 0,
    JS_GC_OBJ_TYPE_FUNCTION_BYTECODE,
    JS_GC_OBJ_TYPE_ASYNC_FUNCTION,
    JS_GC_OBJ_TYPE_VAR_REF,
    JS_GC_OBJ_TYPE_SHAPE,
    JS_GC_OBJ_TYPE_MODULE,
} JSGCObjectTypeEnum;

/* New object header - embedded at start of every GC object */
typedef struct JSGCObjectHeader {
    JSObjHandle handle;               /* My handle ID - key for compaction */
    uint32_t size;                    /* Total size including header */
    JSGCObjectTypeEnum gc_obj_type : 8;
    uint8_t mark : 1;                 /* GC mark bit */
    uint8_t pinned : 1;               /* Pinned (don't move during compaction) */
    uint8_t flags : 6;                /* Reserved */
} JSGCObjectHeader;

/* Handle table entry - stores pointer to USER DATA (past header) */
typedef struct JSHandleEntry {
    void *ptr;                        /* Pointer to user data (past header) */
    uint32_t generation;              /* GC generation when handle was allocated */
} JSHandleEntry;

/* Stack region for bump allocation */
typedef struct JSMemStack {
    uint8_t *base;
    uint8_t *top;
    uint8_t *limit;
    size_t capacity;
} JSMemStack;

/* The handle-based GC state - embed in JSRuntime */
typedef struct JSHandleGCState {
    /* Handle table - resizable array */
    JSHandleEntry *handles;
    uint32_t handle_count;
    uint32_t handle_capacity;
    uint32_t generation;              /* Incremented each GC cycle */
    
    /* Object stack - bump allocator */
    JSMemStack stack;
    
    /* Root set - handles that are always reachable */
    JSObjHandle *roots;
    uint32_t root_count;
    uint32_t root_capacity;
    
    /* Stats */
    uint64_t total_allocated;
    uint64_t total_freed;
    uint32_t gc_count;
} JSHandleGCState;

/* Get header from user data pointer (internal use) */
static inline JSGCObjectHeader* js_handle_header(void *data_ptr) {
    if (!data_ptr) return NULL;
    return (JSGCObjectHeader*)((uint8_t*)data_ptr - sizeof(JSGCObjectHeader));
}

/* Initialization and cleanup */
void js_handle_gc_init(JSHandleGCState *gc, size_t initial_stack_size);
void js_handle_gc_free(JSHandleGCState *gc);

/* Object allocation */
JSObjHandle js_handle_alloc(JSHandleGCState *gc, size_t size, int type);
void js_handle_release(JSHandleGCState *gc, JSObjHandle handle);
void js_handle_retain(JSHandleGCState *gc, JSObjHandle handle);

/* Check if object is live by examining actual object on stack */
static inline bool js_handle_is_live(JSHandleGCState *gc, JSObjHandle handle) {
    if (handle == 0 || handle >= gc->handle_count) {
        return false;
    }
    void *ptr = gc->handles[handle].ptr;
    if (!ptr) {
        return false;  /* Handle has been freed */
    }
    
    /* Validate by checking the actual object header on the stack */
    JSGCObjectHeader *obj = js_handle_header(ptr);
    
    /* Object is live if:
     * 1. Its handle field matches the handle we're checking
     * 2. Its generation matches the handle's generation (not stale)
     */
    if (obj->handle != handle) {
        return false;  /* Object was overwritten or corrupted */
    }
    if (gc->handles[handle].generation != gc->generation) {
        return false;  /* Stale handle from previous GC cycle */
    }
    return true;
}

/* Dereference handle to user data pointer */
static inline void* js_handle_deref(JSHandleGCState *gc, JSObjHandle handle) {
    if (__builtin_expect(handle == 0 || handle >= gc->handle_count, 0)) {
        return NULL;
    }
    void *ptr = gc->handles[handle].ptr;
    if (__builtin_expect(!ptr, 0)) {
        return NULL;  /* Free handle */
    }
    
    /* Validate by checking the actual object on the stack */
    JSGCObjectHeader *obj = js_handle_header(ptr);
    
    /* Check that object points back to this handle */
    if (__builtin_expect(obj->handle != handle, 0)) {
        return NULL;  /* Object was overwritten or handle is stale */
    }
    
    /* Check generation to detect freed handles */
    if (__builtin_expect(gc->handles[handle].generation != gc->generation, 0)) {
        return NULL;  /* Handle was freed in a previous GC cycle */
    }
    
    return ptr;
}

/* Macro for type-safe dereference */
#define JS_HANDLE_DEREF(gc, handle, type) ((type*)js_handle_deref(gc, handle))

/* Stack allocation */
void* js_mem_stack_alloc(JSMemStack *stack, size_t size);
void js_mem_stack_reset(JSMemStack *stack);

/* Root management */
void js_handle_add_root(JSHandleGCState *gc, JSObjHandle handle);
void js_handle_remove_root(JSHandleGCState *gc, JSObjHandle handle);

/* Garbage collection */
void js_handle_gc_run(JSHandleGCState *gc);
void js_handle_gc_mark(JSHandleGCState *gc);
void js_handle_gc_compact(JSHandleGCState *gc);

/* Stats */
typedef struct JSHandleGCStats {
    uint32_t total_objects;
    uint32_t live_objects;
    uint32_t handle_count;
    uint32_t free_handles;
    size_t used_bytes;
    size_t available_bytes;
    size_t capacity_bytes;
} JSHandleGCStats;

void js_handle_gc_stats(JSHandleGCState *gc, JSHandleGCStats *stats);

/* Validation */
bool js_handle_gc_validate(JSHandleGCState *gc, char *error_buffer, size_t error_len);

#endif /* QUICKJS_GC_REWRITE_H */
