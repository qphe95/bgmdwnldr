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

/* Forward declarations for QuickJS types */
struct JSRuntime;
typedef struct JSRuntime JSRuntime;
struct JSContext;
typedef struct JSContext JSContext;

/* Note: JSValue is defined in quickjs.h - must include that header to use
 * functions that take/return JSValue. We don't define it here to avoid
 * conflicts. Shadow stack functions take void* for value_slot which will
 * be cast to JSValue* by the macros in js_value_helpers.h. */

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

/* ============================================================================
 * SHADOW STACK - For tracking C-level JSValue references
 * ============================================================================
 * 
 * The shadow stack tracks JSValue variables held by C code. When the GC runs,
 * it treats these as additional roots, preventing collection of values that
 * are still in use by C code.
 * 
 * Usage:
 *   JSValue val = JS_GetPropertyStr(ctx, obj, "foo");
 *   JS_GC_PUSH(ctx, val);  // Register as GC root
 *   // ... use val ...
 *   JS_GC_POP(ctx, val);   // Unregister when done
 * 
 * Or use the scoped helper:
 *   JS_GC_SCOPED(ctx, val, JS_GetPropertyStr(ctx, obj, "foo"));
 *   // ... use val ...
 *   // automatically popped when val goes out of scope
 */

/* Shadow stack entry - tracks a single JSValue variable */
/* Note: value_slot is void* to avoid needing JSValue definition here.
 * The actual type is JSValue* - casts are handled by macros. */
typedef struct GCShadowStackEntry {
    void *value_slot;              /* Pointer to the C variable holding JSValue */
    struct GCShadowStackEntry *next;  /* Next entry in stack (linked list) */
    struct GCShadowStackEntry *pool_next; /* Next entry in free pool */
    #ifdef GC_DEBUG
    const char *file;              /* Source file where push occurred */
    int line;                      /* Line number */
    const char *var_name;          /* Variable name */
    #endif
} GCShadowStackEntry;

/* Shadow stack statistics */
typedef struct GCShadowStackStats {
    uint32_t current_depth;        /* Current stack depth */
    uint32_t max_depth;            /* Maximum depth reached */
    uint32_t total_pushes;         /* Total push operations */
    uint32_t total_pops;           /* Total pop operations */
    uint32_t pool_hits;            /* Allocations from pool */
    uint32_t pool_misses;          /* Allocations from malloc */
} GCShadowStackStats;

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
    
    /* Shadow stack for tracking C-level JSValue roots */
    GCShadowStackEntry *shadow_stack;      /* Head of active stack */
    GCShadowStackEntry *shadow_stack_pool; /* Free list for reuse */
    GCShadowStackStats shadow_stats;       /* Statistics */
    
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

/* Get or create a handle for an existing GC pointer (for JSValue storage) */
GCHandle gc_alloc_handle_for_ptr(void *ptr);

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

/* Helper: get header from user pointer.
 * Allocations return user_ptr (after GCHeader), so we subtract to get header. */
static inline GCHeader *gc_header(void *user_ptr) {
    if (!user_ptr) return NULL;
    return (GCHeader*)((uint8_t*)user_ptr - sizeof(GCHeader));
}

/* Helper: get total allocation size from user pointer */
static inline size_t gc_alloc_size(void *ptr) {
    if (!ptr) return 0;
    GCHeader *hdr = gc_header(ptr);
    return hdr->size;
}

/* ============================================================================
 * Shadow Stack API
 * ============================================================================
 */

/* Initialize shadow stack (called by gc_init) */
void gc_shadow_stack_init(void);

/* Cleanup shadow stack (called by gc_cleanup) */
void gc_shadow_stack_cleanup(void);

/* Push a JSValue onto the shadow stack (registers as GC root) */
/* value_slot should point to a JSValue variable */
void gc_push_jsvalue(JSContext *ctx, void *value_slot, const char *file, int line, const char *var_name);

/* Pop a JSValue from the shadow stack */
void gc_pop_jsvalue(JSContext *ctx, void *value_slot);

/* Mark all shadow stack entries during GC (internal use) */
void gc_mark_shadow_stack(void);

/* Get shadow stack statistics */
void gc_shadow_stack_stats(GCShadowStackStats *stats);

/* Validate shadow stack consistency (for debugging) */
bool gc_shadow_stack_validate(char *error_buffer, size_t error_len);

/* Auto-cleanup helper for scoped values (used by JS_GC_SCOPED macro) */
/* Takes void** to avoid needing JSValue definition in this header */
void gc_auto_pop_helper(void **slot);

/* Internal auto-pop wrapper for attribute cleanup */
static inline void gc_auto_pop_wrapper(void ***slot_ptr) {
    if (slot_ptr && *slot_ptr) {
        gc_auto_pop_helper(*slot_ptr);
    }
}

/* ============================================================================
 * Convenience Macros
 * ============================================================================
 */

/* Push a JSValue onto the shadow stack */
#define JS_GC_PUSH(ctx, var) \
    gc_push_jsvalue(ctx, &var, __FILE__, __LINE__, #var)

/* Pop a JSValue from the shadow stack */
#define JS_GC_POP(ctx, var) \
    gc_pop_jsvalue(ctx, &var)

/* Scoped JSValue that auto-pops when going out of scope
 * Usage: JS_GC_SCOPED(ctx, myval, JS_GetPropertyStr(ctx, obj, "foo"));
 */
#define JS_GC_SCOPED(ctx, name, init_expr) \
    JSValue name = (init_expr); \
    JS_GC_PUSH(ctx, name); \
    __attribute__((cleanup(gc_auto_pop_wrapper))) JSValue * _gc_scope_ptr_##name = &_gc_scope_##name; \
    JSValue * _gc_scope_##name = &name; \
    (void)_gc_scope_ptr_##name; \
    (void)_gc_scope_##name

#ifdef __cplusplus
}
#endif

#endif /* QUICKJS_GC_UNIFIED_H */
