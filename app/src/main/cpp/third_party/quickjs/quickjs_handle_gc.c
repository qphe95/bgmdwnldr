/*
 * QuickJS Handle-Based GC Implementation
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "quickjs_handle_gc.h"

#define HANDLE_GROWTH_FACTOR 2
#define HANDLE_INITIAL_CAPACITY 1024
#define STACK_INITIAL_SIZE (16 * 1024 * 1024)  /* 16MB default */

void js_handle_gc_init(JSHandleGC *gc, size_t initial_stack_size, size_t max_handles) {
    memset(gc, 0, sizeof(JSHandleGC));
    
    /* Initialize handle table */
    gc->handle_capacity = max_handles > 0 ? max_handles : HANDLE_INITIAL_CAPACITY;
    gc->handles = calloc(gc->handle_capacity, sizeof(JSHandleEntry));
    assert(gc->handles != NULL);
    
    /* Handle 0 is reserved as NULL */
    gc->handle_count = 1;
    gc->free_list = JS_HANDLE_NULL;
    
    /* Initialize stack */
    size_t stack_size = initial_stack_size > 0 ? initial_stack_size : STACK_INITIAL_SIZE;
    gc->stack.base = malloc(stack_size);
    assert(gc->stack.base != NULL);
    gc->stack.top = gc->stack.base;
    gc->stack.capacity = stack_size;
    gc->stack.used = 0;
    
    /* Initialize roots array */
    gc->root_capacity = 256;
    gc->roots = malloc(gc->root_capacity * sizeof(JSObjectHandle));
    assert(gc->roots != NULL);
    gc->root_count = 0;
}

void js_handle_gc_free(JSHandleGC *gc) {
    if (!gc) return;
    
    free(gc->handles);
    free(gc->stack.base);
    free(gc->roots);
    memset(gc, 0, sizeof(JSHandleGC));
}

void* js_stack_alloc(JSStackRegion *stack, size_t size) {
    /* Align to 8 bytes */
    size_t aligned = (size + 7) & ~7;
    
    if (stack->top + aligned > stack->base + stack->capacity) {
        return NULL;  /* Out of memory */
    }
    
    void *ptr = stack->top;
    stack->top += aligned;
    stack->used += aligned;
    
    return ptr;
}

void js_stack_reset(JSStackRegion *stack) {
    stack->top = stack->base;
    stack->used = 0;
}

static JSObjectHandle alloc_handle_id(JSHandleGC *gc) {
    /* Check free list first */
    if (gc->free_list != JS_HANDLE_NULL) {
        JSObjectHandle id = gc->free_list;
        /* Next free handle is stored in the ptr field (cast to integer) */
        gc->free_list = (JSObjectHandle)(uintptr_t)gc->handles[id].ptr;
        gc->handles[id].ptr = NULL;
        gc->handles[id].flags = 0;
        return id;
    }
    
    /* Grow table if needed */
    if (gc->handle_count >= gc->handle_capacity) {
        size_t new_cap = gc->handle_capacity * HANDLE_GROWTH_FACTOR;
        JSHandleEntry *new_handles = realloc(gc->handles, new_cap * sizeof(JSHandleEntry));
        assert(new_handles != NULL);
        gc->handles = new_handles;
        /* Zero new entries */
        memset(&gc->handles[gc->handle_capacity], 0, 
               (new_cap - gc->handle_capacity) * sizeof(JSHandleEntry));
        gc->handle_capacity = new_cap;
    }
    
    return gc->handle_count++;
}

JSObjectHandle js_handle_alloc(JSHandleGC *gc, size_t size, int type) {
    /* Allocate from stack */
    size_t total_size = sizeof(JSObjHeader) + size;
    JSObjHeader *obj = js_stack_alloc(&gc->stack, total_size);
    if (!obj) return JS_HANDLE_NULL;
    
    /* Initialize header */
    obj->size = total_size;
    obj->type = type;
    obj->ref_count = 1;
    obj->mark = 0;
    obj->flags = 0;
    
    /* Allocate handle */
    JSObjectHandle handle = alloc_handle_id(gc);
    obj->handle = handle;
    gc->handles[handle].ptr = obj;
    gc->handles[handle].flags = 0;
    
    return handle;
}

void js_handle_free(JSHandleGC *gc, JSObjectHandle handle) {
    if (handle == JS_HANDLE_NULL || handle >= gc->handle_count) return;
    
    JSObjHeader *obj = gc->handles[handle].ptr;
    if (!obj) return;
    
    /* Add to free list */
    gc->handles[handle].ptr = (JSObjHeader*)(uintptr_t)gc->free_list;
    gc->handles[handle].flags = 0;
    gc->free_list = handle;
}

/* Mark all objects reachable from roots */
static void mark_object(JSHandleGC *gc, JSObjectHandle handle);

static void mark_children(JSHandleGC *gc, JSObjHeader *obj) {
    /* Object references are stored as handle IDs after the header */
    /* Format: [JSObjHeader][data...][handle refs...] */
    /* For now, assume objects have a standard layout with handle refs */
    
    JSObjectHandle *refs = (JSObjectHandle*)((uint8_t*)obj + sizeof(JSObjHeader));
    size_t num_refs = 0;  /* Would need to store this in header or derive from type */
    
    for (size_t i = 0; i < num_refs; i++) {
        if (refs[i] != JS_HANDLE_NULL) {
            mark_object(gc, refs[i]);
        }
    }
}

static void mark_object(JSHandleGC *gc, JSObjectHandle handle) {
    JSObjHeader *obj = js_handle_ptr(gc, handle);
    if (!obj || obj->mark) return;
    
    obj->mark = 1;
    mark_children(gc, obj);
}

void js_handle_mark(JSHandleGC *gc) {
    /* Clear all marks */
    for (uint8_t *p = gc->stack.base; p < gc->stack.top; ) {
        JSObjHeader *obj = (JSObjHeader*)p;
        obj->mark = 0;
        p += obj->size;
    }
    
    /* Mark from roots */
    for (size_t i = 0; i < gc->root_count; i++) {
        mark_object(gc, gc->roots[i]);
    }
}

/* Compaction: two-pointer algorithm, only update handle table */
void js_handle_compact(JSHandleGC *gc) {
    uint8_t *read = gc->stack.base;
    uint8_t *write = gc->stack.base;
    uint8_t *end = gc->stack.top;
    
    while (read < end) {
        JSObjHeader *obj = (JSObjHeader*)read;
        size_t size = obj->size;
        JSObjectHandle handle = obj->handle;
        
        if (obj->mark) {
            /* Live object */
            if (read != write) {
                /* Move to fill gap */
                memmove(write, read, size);
                
                /* Update handle table - this is the ONLY pointer update! */
                gc->handles[handle].ptr = (JSObjHeader*)write;
            }
            write += size;
        } else {
            /* Dead object - free the handle */
            js_handle_free(gc, handle);
        }
        
        read += size;
    }
    
    /* Update stack state */
    gc->stack.top = write;
    gc->stack.used = write - gc->stack.base;
}

void js_handle_gc_collect(JSHandleGC *gc) {
    /* Mark phase */
    js_handle_mark(gc);
    
    /* Compact phase */
    js_handle_compact(gc);
}

void js_handle_add_root(JSHandleGC *gc, JSObjectHandle handle) {
    if (handle == JS_HANDLE_NULL) return;
    
    /* Grow roots if needed */
    if (gc->root_count >= gc->root_capacity) {
        gc->root_capacity *= 2;
        gc->roots = realloc(gc->roots, gc->root_capacity * sizeof(JSObjectHandle));
        assert(gc->roots != NULL);
    }
    
    gc->roots[gc->root_count++] = handle;
}

void js_handle_remove_root(JSHandleGC *gc, JSObjectHandle handle) {
    for (size_t i = 0; i < gc->root_count; i++) {
        if (gc->roots[i] == handle) {
            /* Swap with last and shrink */
            gc->roots[i] = gc->roots[--gc->root_count];
            return;
        }
    }
}

void js_handle_gc_stats(JSHandleGC *gc, JSHandleGCStats *stats) {
    memset(stats, 0, sizeof(JSHandleGCStats));
    
    stats->handle_count = gc->handle_count;
    stats->free_handles = 0;
    
    /* Count free handles in free list */
    for (JSObjectHandle h = gc->free_list; h != JS_HANDLE_NULL; ) {
        stats->free_handles++;
        h = (JSObjectHandle)(uintptr_t)gc->handles[h].ptr;
    }
    
    /* Count objects in stack */
    for (uint8_t *p = gc->stack.base; p < gc->stack.top; ) {
        JSObjHeader *obj = (JSObjHeader*)p;
        stats->total_objects++;
        if (obj->mark) {
            stats->live_objects++;
        }
        p += obj->size;
    }
    
    stats->used_bytes = gc->stack.used;
    stats->available_bytes = gc->stack.capacity - gc->stack.used;
}
