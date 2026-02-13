/*
 * Unified GC Allocator for QuickJS
 * 
 * All memory (interpreter structs, handle tables, JS objects) lives in
 * a single GC-managed heap. No system malloc/free during operation.
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

/* Forward declaration for JSRuntime (used by gc_alloc_js_object) */
struct JSRuntime;
typedef struct JSRuntime JSRuntime;

/* Total GC heap size - 512MB */
#define GC_HEAP_SIZE (512 * 1024 * 1024)

/* Initial handle table capacity */
#define GC_INITIAL_HANDLES 8192

/* Handle type - 0 is reserved as null */
/* Handle type - 0 is reserved as null
 * Note: QuickJS code uses JSObjHandle (typedef'd to same type)
 */
typedef uint32_t GCHandle;
#define GC_HANDLE_NULL 0

/* Object types */
typedef enum {
    GC_TYPE_RAW = 0,        /* Raw memory (not a GC object) */
    GC_TYPE_JS_OBJECT,
    GC_TYPE_JS_STRING,
    GC_TYPE_JS_SHAPE,
    GC_TYPE_JS_CONTEXT,
    GC_TYPE_JS_RUNTIME,
    GC_TYPE_ATOM_ARRAY,
    GC_TYPE_CLASS_ARRAY,
    GC_TYPE_PROP_ARRAY,
    GC_TYPE_HASH_TABLE,
    GC_TYPE_OTHER,
} GCType;

/* Object header - embedded at start of every allocation
 * This is THE single source of truth for GC object headers.
 * Used by both the unified GC allocator and QuickJS internals.
 */
typedef struct GCHeader {
    /* QuickJS compatibility fields */
    int ref_count_unused;   /* Not used - kept for compatibility */
    
    /* Bit fields - QuickJS expects these at specific offsets */
    unsigned int gc_obj_type : 4;
    unsigned int mark : 1;
    unsigned int dummy0 : 3;
    
    uint8_t dummy1;         /* padding - not used by GC */
    uint16_t dummy2;        /* padding - not used by GC */
    
    /* Linked list - QuickJS uses this in some places */
    struct {
        void *next;
        void *prev;
    } link;
    
    /* Handle ID - key for handle-based GC */
    uint32_t handle;
    
    /* Unified GC specific fields */
    uint32_t size;          /* Total allocation size including header */
    GCType type;            /* Unified GC object type */
    uint8_t pinned;         /* Don't move during compaction */
    uint8_t flags;          /* Reserved for future use */
    uint16_t pad;           /* Padding to maintain alignment */
} GCHeader;

/* Handle type - 0 is reserved as null */
typedef uint32_t GCHandle;
#define GC_HANDLE_NULL 0

/* Handle table entry */
typedef struct GCHandleEntry {
    void *ptr;              /* Pointer to user data (past header) */
    uint32_t gen;           /* Generation for debugging */
} GCHandleEntry;

/* Bump allocator region */
typedef struct GCBumpRegion {
    uint8_t *base;
    _Atomic size_t offset;
    size_t capacity;
} GCBumpRegion;

/* Forward declaration */
struct JSRuntime;

/* Unified GC state - lives in static space */
typedef struct GCState {
    /* The heap */
    uint8_t *heap;
    size_t heap_size;
    
    /* Bump allocator for new allocations */
    GCBumpRegion bump;
    
    /* Handle table - stored IN the GC heap itself! */
    GCHandleEntry *handles;     /* Pointer into GC heap */
    uint32_t handle_count;
    uint32_t handle_capacity;   /* Current max handles */
    
    /* Root set - handles that are always reachable */
    GCHandle *roots;
    uint32_t root_count;
    uint32_t root_capacity;
    
    /* Stats */
    uint64_t total_allocs;
    uint64_t total_bytes;
    uint32_t gc_count;
    
    /* Memory tracking for GC trigger (Bug #2 fix) */
    size_t bytes_allocated;     /* Current allocated bytes (for GC trigger) */
    size_t gc_threshold;        /* Threshold for triggering GC */
    struct JSRuntime *rt;       /* Pointer to runtime for updating malloc_state */
    
    /* Initialized? */
    bool initialized;
} GCState;

/* Global GC instance - lives in static BSS */
extern GCState g_gc;

/* Initialize the unified GC - MUST be called before any QuickJS operations */
bool gc_init(void);

/* Check if GC is initialized */
bool gc_is_initialized(void);

/* Cleanup */
void gc_cleanup(void);

/* 
 * Core allocation - everything goes through this
 * Returns pointer to USER DATA (past header)
 */
void *gc_alloc(size_t size, GCType type);

/* Raw allocation (no handle, for internal structures) */
static inline void *gc_alloc_raw(size_t size) {
    return gc_alloc(size, GC_TYPE_RAW);
}

/* Realloc - allocates new, copies, old becomes garbage */
void *gc_realloc(void *ptr, size_t new_size);

/* Free - just marks as free, actual reclaim during GC */
void gc_free(void *ptr);

/* Get size of allocation */
size_t gc_size(void *ptr);

/* Handle-based allocation for GC objects */
GCHandle gc_alloc_handle(size_t size, GCType type);

/* Handle array types for atomic allocation */
typedef enum {
    GC_HANDLE_ARRAY_GC,       /* General gc_handles (JS_OBJECT, SHAPE, STRING, etc.) */
    GC_HANDLE_ARRAY_CONTEXT,  /* context_handles (JSContext) */
    GC_HANDLE_ARRAY_ATOM,     /* atom_handles (JSString atoms) */
    GC_HANDLE_ARRAY_JOB,      /* job_handles (JSJobEntry) */
    GC_HANDLE_ARRAY_WEAKREF,  /* weakref_handles (weak references) */
} GCHandleArrayType;

/* Allocate and register a QuickJS GC object atomically.
 * This prevents the race condition where GC sees an uninitialized object.
 * Sets gc_obj_type immediately and adds to the specified handle array.
 */
void *gc_alloc_js_object_ex(size_t size, int js_gc_obj_type, JSRuntime *rt, GCHandleArrayType array_type);

/* Convenience macro for the most common case (gc_handles) */
#define gc_alloc_js_object(size, type, rt) gc_alloc_js_object_ex(size, type, rt, GC_HANDLE_ARRAY_GC)

/* Get pointer from handle */
void *gc_deref(GCHandle handle);

/* Add/remove roots */
void gc_add_root(GCHandle handle);
void gc_remove_root(GCHandle handle);

/* Run GC cycle (mark-compact) */
void gc_run(void);

/* Reset everything (nuclear option) */
void gc_reset(void);

/* Stats */
size_t gc_used(void);
size_t gc_available(void);

/* Check if GC should run based on memory pressure (Bug #2 fix) */
bool gc_should_run(void);

/* Check if a pointer is in the valid GC heap range */
bool gc_ptr_is_valid(const void *ptr);

/* Set the runtime pointer for malloc_state updates (Bug #2 fix) */
void gc_set_runtime(struct JSRuntime *rt);

/* Update GC threshold (like JS_SetGCThreshold) */
void gc_set_threshold(size_t threshold);

/* Helper: get header from user pointer */
static inline GCHeader *gc_header(void *ptr) {
    if (!ptr) return NULL;
    return (GCHeader*)((uint8_t*)ptr - sizeof(GCHeader));
}

/* Helper: get total allocation size from user pointer */
static inline size_t gc_alloc_size(void *ptr) {
    if (!ptr) return 0;
    GCHeader *hdr = gc_header(ptr);
    return hdr->size;
}

#ifdef __cplusplus
}
#endif

#endif /* QUICKJS_GC_UNIFIED_H */
