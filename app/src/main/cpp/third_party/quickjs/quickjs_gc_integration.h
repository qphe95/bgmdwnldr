/*
 * QuickJS GC Integration Layer
 * 
 * This header integrates the handle-based GC into QuickJS.
 * It modifies QuickJS's internal GC structures to use handles.
 */

#ifndef QUICKJS_GC_INTEGRATION_H
#define QUICKJS_GC_INTEGRATION_H

#include "quickjs_gc.h"

/* Override QuickJS's JSGCObjectHeader to include handle */
#define JSGCObjectHeader JSGCObjectHeader_HandleGC

/* QuickJS expects these fields in JSGCObjectHeader:
 * - gc_obj_type : 4
 * - mark : 1
 * Plus a struct list_head link for the linked list
 * 
 * We'll replace the linked list with handle-based tracking.
 * Note: ref_count removed - using mark-and-sweep GC only.
 */

/* Redefine the header to include handle field */
struct JSGCObjectHeader_HandleGC {
    /* Pack type info into single byte to match QuickJS expectations */
    JSGCObjectTypeEnum gc_obj_type : 4;
    uint8_t mark : 1;
    uint8_t dummy0 : 3;  /* padding */
    
    uint8_t dummy1;      /* not used by GC - kept for compatibility */
    uint16_t dummy2;     /* not used by GC - kept for compatibility */
    
    /* Replace linked list with handle */
    JSObjHandle handle;  /* Handle ID for this object */
    
    /* Keep link for compatibility during transition, but don't use it */
    struct list_head link;
};

/* Integration functions */
void js_gc_init_runtime(struct JSRuntime *rt);
void js_gc_free_runtime(struct JSRuntime *rt);

/* Allocate a GC object using handle-based system */
JSObjHandle js_gc_alloc_object(struct JSRuntime *rt, size_t size, int type);

/* Free a GC object */
void js_gc_free_object(struct JSRuntime *rt, JSObjHandle handle);

/* Mark an object and its children */
void js_gc_mark_object(struct JSRuntime *rt, JSObjHandle handle);

/* Run full GC cycle */
void js_gc_run(struct JSRuntime *rt);

/* Get pointer from handle for QuickJS internal use */
static inline void* js_gc_handle_to_ptr(struct JSRuntime *rt, JSObjHandle handle) {
    extern struct JSHandleGCState* js_runtime_get_gc_state(struct JSRuntime *rt);
    return js_handle_deref(js_runtime_get_gc_state(rt), handle);
}

/* Get handle from pointer */
static inline JSObjHandle js_gc_ptr_to_handle(void *ptr) {
    if (!ptr) return JS_OBJ_HANDLE_NULL;
    JSGCObjectHeader *hdr = (JSGCObjectHeader*)((uint8_t*)ptr - sizeof(JSGCObjectHeader));
    return hdr->handle;
}

#endif /* QUICKJS_GC_INTEGRATION_H */
