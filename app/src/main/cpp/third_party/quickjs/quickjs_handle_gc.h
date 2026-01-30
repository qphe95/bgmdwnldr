/*
 * QuickJS Handle-Based GC System
 * 
 * Replaces pointer-based GC with handle indirection:
 * - All objects allocated on contiguous stack
 * - Handle table maps IDs to pointers
 * - Compaction only updates handle table (not object graph)
 */

#ifndef QUICKJS_HANDLE_GC_H
#define QUICKJS_HANDLE_GC_H

#include <stdint.h>
#include <stdbool.h>

/* Handle is just an integer ID. 0 = null/invalid */
typedef uint32_t JSObjectHandle;
#define JS_HANDLE_NULL 0

/* Object header embedded at start of each allocation */
typedef struct JSObjHeader {
    uint32_t size;           /* Total size including header */
    JSObjectHandle handle;   /* My handle ID (for fast lookup) */
    uint16_t type;           /* Object type */
    uint16_t ref_count;      /* Ref count */
    uint8_t mark;            /* GC mark bit */
    uint8_t flags;           /* Pin, weak, etc. */
} JSObjHeader;

/* Handle table entry */
typedef struct JSHandleEntry {
    JSObjHeader *ptr;        /* Current pointer (updated during compaction) */
    uint32_t flags;
} JSHandleEntry;

/* Stack region for bump allocation */
typedef struct JSStackRegion {
    uint8_t *base;
    uint8_t *top;
    size_t capacity;
    size_t used;
} JSStackRegion;

/* Handle-based GC state (add to JSRuntime) */
typedef struct JSHandleGC {
    /* Handle table */
    JSHandleEntry *handles;
    size_t handle_count;
    size_t handle_capacity;
    JSObjectHandle free_list;  /* Linked list of free handles */
    
    /* Object stack */
    JSStackRegion stack;
    
    /* GC state */
    uint8_t *compact_read;     /* During compaction */
    uint8_t *compact_write;
    
    /* Roots */
    JSObjectHandle *roots;
    size_t root_count;
    size_t root_capacity;
} JSHandleGC;

/* API */
void js_handle_gc_init(JSHandleGC *gc, size_t initial_stack_size, size_t max_handles);
void js_handle_gc_free(JSHandleGC *gc);

/* Allocation */
JSObjectHandle js_handle_alloc(JSHandleGC *gc, size_t size, int type);
void js_handle_free(JSHandleGC *gc, JSObjectHandle handle);

/* Dereference handle to get pointer */
static inline JSObjHeader* js_handle_ptr(JSHandleGC *gc, JSObjectHandle handle) {
    if (handle == 0 || handle >= gc->handle_count) return NULL;
    return gc->handles[handle].ptr;
}

/* Stack allocation (bump pointer) */
void* js_stack_alloc(JSStackRegion *stack, size_t size);
void js_stack_reset(JSStackRegion *stack);

/* GC */
void js_handle_gc_collect(JSHandleGC *gc);
void js_handle_mark(JSHandleGC *gc);
void js_handle_compact(JSHandleGC *gc);

/* Roots */
void js_handle_add_root(JSHandleGC *gc, JSObjectHandle handle);
void js_handle_remove_root(JSHandleGC *gc, JSObjectHandle handle);

/* Stats */
typedef struct JSHandleGCStats {
    size_t total_objects;
    size_t live_objects;
    size_t used_bytes;
    size_t available_bytes;
    size_t handle_count;
    size_t free_handles;
} JSHandleGCStats;

void js_handle_gc_stats(JSHandleGC *gc, JSHandleGCStats *stats);

#endif /* QUICKJS_HANDLE_GC_H */
