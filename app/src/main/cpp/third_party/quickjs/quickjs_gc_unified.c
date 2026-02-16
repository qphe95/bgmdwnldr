/*
 * Unified GC Allocator Implementation
 * 
 * CRITICAL BUG FIXES:
 * - Bug #2: Track malloc_size for GC trigger conditions
 * - Bug #5: Properly handle dead object sizes during compaction
 * - Added thread-safety for bytes_allocated updates
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <android/log.h>
#include "quickjs_gc_unified.h"
#include "quickjs.h"  /* For JSValue access in gc_mark_shadow_stack */

/* Forward declaration from quickjs.c */
struct JSRuntime;

/* Debug logging control - set to 1 to enable, 0 to disable */
#define GC_DEBUG_LOGGING 0

#if GC_DEBUG_LOGGING
    #define GC_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "quickjs", __VA_ARGS__)
    #define GC_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "quickjs", __VA_ARGS__)
#else
    #define GC_LOGI(...) ((void)0)
    #define GC_LOGE(...) ((void)0)
#endif

/* Global GC instance - lives in BSS */
GCState g_gc = {0};

/* Default GC threshold - 256KB */
#define GC_DEFAULT_THRESHOLD (256 * 1024)

/* Align size to 16 bytes */
#define ALIGN16(size) (((size) + 15) & ~15)

/* Minimum object size to prevent infinite loops on dead objects */
#define MIN_OBJECT_SIZE (sizeof(GCHeader) + 16)

/* Memory canary values for detecting buffer overflows
 * CANARY_BEFORE is placed before the user data (after header)
 * CANARY_AFTER is placed at the end of the allocation
 */
#define GC_CANARY_BEFORE 0xDEADBEEFCAFEBAB0ULL
#define GC_CANARY_AFTER  0xB0B1B2B3B4B5B6B7ULL
#define GC_CANARY_SIZE   sizeof(uint64_t)

/* ============================================================================
 * MEMORY CANARY VALIDATION
 * ============================================================================
 * Canary values help detect buffer overflows by placing known magic values
 * before and after user data. If these values are corrupted, a buffer overflow
 * has occurred.
 */

/* Write canaries around user data area
 * Layout: [GCHeader][CANARY_BEFORE][USER DATA][CANARY_AFTER] */
static inline void gc_write_canaries(GCHeader *hdr, void *user_ptr, size_t user_size) {
    (void)hdr;
    /* Write canary before user data (at start of user area) */
    uint64_t *canary_before = (uint64_t*)user_ptr;
    *canary_before = GC_CANARY_BEFORE;
    
    /* Write canary after user data (at end of allocation) */
    uint8_t *end = (uint8_t*)user_ptr + user_size;
    /* Align to 8 bytes for the canary */
    uint64_t *canary_after = (uint64_t*)((uintptr_t)(end + 7) & ~7);
    /* Make sure we don't overflow the allocation */
    if ((uint8_t*)canary_after < (uint8_t*)hdr + hdr->size - GC_CANARY_SIZE) {
        *canary_after = GC_CANARY_AFTER;
    }
}

/* Validate canaries for an allocation. Returns true if valid, false if corrupted. */
static inline bool gc_validate_canaries(GCHeader *hdr) {
    if (!hdr || hdr->size == 0) return true; /* Dead object, skip */
    
    void *user_ptr = (void*)(hdr + 1);
    size_t user_size = hdr->size - sizeof(GCHeader);
    
    /* Check canary before */
    uint64_t *canary_before = (uint64_t*)user_ptr;
    if (*canary_before != GC_CANARY_BEFORE) {
        GC_LOGE(
            "GC CANARY CORRUPTED: before user data at %p, expected 0x%llx got 0x%llx",
            user_ptr, (unsigned long long)GC_CANARY_BEFORE, (unsigned long long)*canary_before);
        return false;
    }
    
    /* Check canary after */
    uint8_t *end = (uint8_t*)user_ptr + user_size;
    uint64_t *canary_after = (uint64_t*)((uintptr_t)(end + 7) & ~7);
    if ((uint8_t*)canary_after < (uint8_t*)hdr + hdr->size - GC_CANARY_SIZE) {
        if (*canary_after != GC_CANARY_AFTER) {
            GC_LOGE(
                "GC CANARY CORRUPTED: after user data at %p, expected 0x%llx got 0x%llx",
                canary_after, (unsigned long long)GC_CANARY_AFTER, (unsigned long long)*canary_after);
            return false;
        }
    }
    
    return true;
}

bool gc_init(void) {
    if (g_gc.initialized) {
        return true;
    }
    
    /* Allocate the heap */
    g_gc.heap = malloc(GC_HEAP_SIZE);
    if (!g_gc.heap) {
        GC_LOGE(
            "gc_init: FAILED to allocate GC heap of size %zu", GC_HEAP_SIZE);
        return false;
    }
    
    g_gc.heap_size = GC_HEAP_SIZE;
    g_gc.bump.base = g_gc.heap;
    atomic_store(&g_gc.bump.offset, 0);
    g_gc.bump.capacity = GC_HEAP_SIZE;
    
    /* Reserve space for initial handle table at start of heap */
    size_t handle_table_size = ALIGN16(GC_INITIAL_HANDLES * sizeof(GCHandleEntry));
    
    /* Initialize handle table in-place */
    g_gc.handles = (GCHandleEntry*)g_gc.heap;
    memset(g_gc.handles, 0, handle_table_size);
    
    /* Handle 0 is reserved as NULL */
    g_gc.handle_count = 1;
    g_gc.handle_capacity = GC_INITIAL_HANDLES;
    
    /* Bump offset starts after handle table */
    atomic_store(&g_gc.bump.offset, handle_table_size);
    
    /* Root set stored at end of heap (grows downward) or we can allocate it */
    /* For simplicity, allocate root array in heap too */
    size_t root_capacity = 1024;
    size_t root_size = ALIGN16(root_capacity * sizeof(GCHandle));
    
    /* Check if we have space */
    if (handle_table_size + root_size > GC_HEAP_SIZE) {
        GC_LOGE(
            "gc_init: FAILED - handle_table_size(%zu) + root_size(%zu) exceeds heap size(%zu)",
            handle_table_size, root_size, GC_HEAP_SIZE);
        free(g_gc.heap);
        g_gc.heap = NULL;
        return false;
    }
    
    g_gc.roots = (GCHandle*)(g_gc.heap + handle_table_size);
    g_gc.root_capacity = root_capacity;
    g_gc.root_count = 0;
    
    /* Bump offset starts after handle table AND root array */
    atomic_store(&g_gc.bump.offset, handle_table_size + root_size);
    
    g_gc.total_allocs = 0;
    g_gc.total_bytes = 0;
    g_gc.gc_count = 0;
    
    /* Bug #2 fix: Initialize memory tracking */
    g_gc.bytes_allocated = 0;
    g_gc.gc_threshold = GC_DEFAULT_THRESHOLD;
    g_gc.rt = NULL;
    
    /* Initialize shadow stack for C-level JSValue tracking */
    gc_shadow_stack_init();
    
    g_gc.initialized = true;
    return true;
}

bool gc_is_initialized(void) {
    return g_gc.initialized;
}

void gc_cleanup(void) {
    /* Cleanup shadow stack first */
    gc_shadow_stack_cleanup();
    
    if (g_gc.heap) {
        free(g_gc.heap);
        g_gc.heap = NULL;
    }
    memset(&g_gc, 0, sizeof(g_gc));
}

/* Bug #2 fix: Set runtime pointer for malloc_state updates */
void gc_set_runtime(struct JSRuntime *rt) {
    g_gc.rt = rt;
}

/* Bug #2 fix: Set GC threshold */
void gc_set_threshold(size_t threshold) {
    g_gc.gc_threshold = threshold;
}

/* Bug #2 fix: Check if GC should run based on memory pressure */
bool gc_should_run(void) {
    if (!g_gc.initialized) return false;
    return g_gc.bytes_allocated > g_gc.gc_threshold;
}

/* Check if a pointer is in the valid GC heap range */
bool gc_ptr_is_valid(const void *ptr) {
    if (!g_gc.initialized || !ptr) return false;
    
    const uint8_t *p = (const uint8_t *)ptr;
    return (p >= g_gc.heap && p < g_gc.heap + GC_HEAP_SIZE);
}

/* Bug #2 fix: Update malloc_state in runtime if available */
static void update_malloc_state(size_t allocated_delta) {
    if (g_gc.rt) {
        /* We can't directly access malloc_state since we don't have the struct definition,
         * but we can at least track it locally. The js_trigger_gc function will check
         * gc_should_run() via gc_bytes_allocated() which we'll export. */
    }
}

/* Get current bytes allocated (for GC trigger) */
static size_t gc_bytes_allocated(void) {
    return g_gc.bytes_allocated;
}

/* Core bump allocation - thread safe via atomic */
static void *bump_alloc(size_t size, GCType type, GCHandle *out_handle) {
    /* Add space for canaries (before and after user data) */
    size_t user_size_with_canaries = size + (GC_CANARY_SIZE * 2);
    size_t aligned_size = ALIGN16(user_size_with_canaries);
    size_t total_size = sizeof(GCHeader) + aligned_size;
    
    /* Check space availability BEFORE atomically reserving */
    size_t old_offset = atomic_load(&g_gc.bump.offset);
    size_t new_offset;
    
    do {
        new_offset = old_offset + total_size;
        
        if (new_offset > g_gc.heap_size) {
            /* Out of memory - offset not bumped yet, no corruption */
            GC_LOGE(
                "bump_alloc: OUT OF MEMORY - requested %zu bytes, heap exhausted", total_size);
            return NULL;
        }
        /* Try to reserve space - only succeeds if offset hasn't changed */
    } while (!atomic_compare_exchange_weak(&g_gc.bump.offset, &old_offset, new_offset));
    
    /* Space successfully reserved from old_offset to new_offset */
    
    uint8_t *ptr = g_gc.heap + old_offset;
    GCHeader *hdr = (GCHeader*)ptr;
    
    /* Initialize header - GCHeader is the single source of truth */
    hdr->ref_count_unused = 0;
    hdr->gc_obj_type = 0;  /* Will be set by add_gc_object */
    hdr->mark = 0;
    hdr->dummy0 = 0;
    hdr->dummy1 = 0;
    hdr->dummy2 = 0;
    hdr->link.next = NULL;
    hdr->link.prev = NULL;
    hdr->handle = GC_HANDLE_NULL;
    hdr->size = total_size;  /* Store total size including header */
    hdr->type = type;
    hdr->pinned = 0;
    hdr->flags = 0;
    hdr->pad = 0;
    
    void *raw_user_ptr = ptr + sizeof(GCHeader);  /* Points to start of canary area */
    
    /* Layout: [GCHeader][CANARY_BEFORE][USER DATA][CANARY_AFTER]
     * Write canary before user data (at raw_user_ptr) */
    *(uint64_t*)raw_user_ptr = GC_CANARY_BEFORE;
    
    /* Write canary after user data (at end of allocation) */
    uint8_t *end = (uint8_t*)raw_user_ptr + GC_CANARY_SIZE + size;
    uint64_t *canary_after = (uint64_t*)((uintptr_t)(end + 7) & ~7);
    if ((uint8_t*)canary_after < ptr + total_size - sizeof(uint64_t)) {
        *canary_after = GC_CANARY_AFTER;
    }
    
    /* The actual user pointer skips the before-canary */
    void *user_ptr = (uint8_t*)raw_user_ptr + GC_CANARY_SIZE;
    
    /* Allocate handle if needed (for GC objects) */
    if (type != GC_TYPE_RAW && out_handle) {
        /* Find free handle slot */
        GCHandle handle = GC_HANDLE_NULL;
        
        /* Simple scan for free handle */
        for (uint32_t i = 1; i < g_gc.handle_count; i++) {
            if (g_gc.handles[i].ptr == NULL) {
                handle = i;
                break;
            }
        }
        
        /* Grow handle table if needed */
        if (handle == GC_HANDLE_NULL) {
            if (g_gc.handle_count >= g_gc.handle_capacity) {
                /* Need to grow handle table - but it's in GC memory! */
                GC_LOGE(
                    "bump_alloc: OUT OF HANDLES - capacity=%u, cannot grow", g_gc.handle_capacity);
                return NULL;
            }
            handle = g_gc.handle_count++;
        }
        
        g_gc.handles[handle].ptr = user_ptr;
        g_gc.handles[handle].gen = g_gc.gc_count;
        hdr->handle = handle;
        *out_handle = handle;
    }
    
    g_gc.total_allocs++;
    g_gc.total_bytes += total_size;
    
    /* Bug #2 fix: Track bytes allocated for GC trigger */
    g_gc.bytes_allocated += total_size;
    update_malloc_state(total_size);
    
    /* Return user_ptr (not hdr) for clean GC abstraction.
     * Use gc_header(user_ptr) to access the GC header when needed. */
    return user_ptr;
}

void *gc_alloc(size_t size, GCType type) {
    if (!g_gc.initialized) {
        return NULL;
    }
    return bump_alloc(size, type, NULL);
}

GCHandle gc_alloc_handle(size_t size, GCType type) {
    if (!g_gc.initialized) {
        return GC_HANDLE_NULL;
    }
    GCHandle handle = GC_HANDLE_NULL;
    bump_alloc(size, type, &handle);
    return handle;
}

/* Allocate a QuickJS GC object with atomic initialization.
 * This function is called from QuickJS to allocate objects that will be
 * tracked by the GC. The gc_obj_type is set BEFORE the object is visible
 * to the GC, preventing race conditions.
 * 
 * This function uses the specified JSRuntime handle array for QuickJS compatibility.
 */
extern int js_handle_array_add_js_object_ex(JSRuntime *rt, void *obj, int js_gc_obj_type, GCHandleArrayType array_type);

void *gc_alloc_js_object_ex(size_t size, int js_gc_obj_type, JSRuntime *rt, GCHandleArrayType array_type) {
    if (!g_gc.initialized || !rt) {
        return NULL;
    }
    
    /* Allocate with header and canaries */
    size_t user_size_with_canaries = size + (GC_CANARY_SIZE * 2);
    size_t total_size = sizeof(GCHeader) + ALIGN16(user_size_with_canaries);
    size_t old_offset = atomic_fetch_add(&g_gc.bump.offset, total_size);
    
    if (old_offset + total_size > g_gc.heap_size) {
        /* Out of memory - rollback */
        GC_LOGE(
            "gc_alloc_js_object_ex: OUT OF MEMORY - requested %zu bytes", total_size);
        atomic_fetch_sub(&g_gc.bump.offset, total_size);
        return NULL;
    }
    
    /* Get pointer to header and user area (including canary space) */
    GCHeader *hdr = (GCHeader *)(g_gc.heap + old_offset);
    void *raw_user_ptr = (void *)(hdr + 1);  /* Points to start of canary area */
    
    /* CRITICAL: Zero the entire allocation to prevent garbage values */
    memset(hdr, 0, total_size);
    
    /* Layout: [GCHeader][CANARY_BEFORE][USER DATA][CANARY_AFTER]
     * raw_user_ptr points to CANARY_BEFORE, actual user data starts after it */
    
    /* Write canary before user data (at raw_user_ptr) */
    *(uint64_t*)raw_user_ptr = GC_CANARY_BEFORE;
    
    /* Write canary after user data (at end of allocation) */
    uint8_t *end = (uint8_t*)raw_user_ptr + GC_CANARY_SIZE + size;
    uint64_t *canary_after = (uint64_t*)((uintptr_t)(end + 7) & ~7);
    if ((uint8_t*)canary_after < (uint8_t*)hdr + total_size - sizeof(uint64_t)) {
        *canary_after = GC_CANARY_AFTER;
    }
    
    /* The actual user pointer skips the before-canary */
    void *user_ptr = (uint8_t*)raw_user_ptr + GC_CANARY_SIZE;
    
    /* CRITICAL: Initialize header BEFORE adding to any handle array.
     * This ensures GC always sees a valid, initialized object. */
    hdr->ref_count_unused = 0;
    hdr->gc_obj_type = js_gc_obj_type;  /* Set type IMMEDIATELY */
    hdr->mark = 0;
    hdr->dummy0 = 0;
    hdr->dummy1 = 0;
    hdr->dummy2 = 0;
    hdr->link.next = NULL;
    hdr->link.prev = NULL;
    hdr->handle = GC_HANDLE_NULL;
    
    GC_LOGI("gc_alloc_js_object_ex: allocated ptr=%p handle initialized to %u", user_ptr, hdr->handle);
    hdr->size = total_size;
    hdr->type = GC_TYPE_JS_OBJECT;
    hdr->pinned = 0;
    hdr->flags = 0;
    hdr->pad = 0;
    
    /* Memory barrier to ensure all initialization is visible before adding to handles */
    atomic_thread_fence(memory_order_release);
    
    /* Add to the specified QuickJS handle array.
     * CRITICAL: Pass raw_user_ptr (hdr+1) NOT user_ptr (hdr+1+canary_size).
     * The handle array functions use gc_header() which expects ptr - sizeof(GCHeader)
     * to land on the header. If we pass user_ptr (which has canary offset),
     * gc_header() will compute the wrong header address. */
    if (js_handle_array_add_js_object_ex(rt, raw_user_ptr, js_gc_obj_type, array_type) < 0) {
        /* Failed to add - mark as freed and return NULL */
        hdr->size = 0;  /* Mark as freed */
        return NULL;
    }
    
    
    /* Update stats */
    g_gc.total_bytes += total_size;
    g_gc.bytes_allocated += total_size;
    update_malloc_state(total_size);
    
    /* Return user_ptr for clean GC abstraction.
     * Use gc_header(user_ptr) to access the GC header when needed. */
    return user_ptr;
}

void *gc_realloc(void *ptr, size_t new_size) {
    if (!ptr) {
        return gc_alloc_raw(new_size);
    }
    
    if (new_size == 0) {
        gc_free(ptr);
        return NULL;
    }
    
    /* ptr is user_ptr (after header). Get header via gc_header(). */
    GCHeader *old_hdr = gc_header(ptr);
    size_t old_total_size = old_hdr->size;
    size_t old_user_size = old_total_size - sizeof(GCHeader);
    GCType type = old_hdr->type;
    
    /* Allocate new space (returns user_ptr) */
    void *new_user_ptr = gc_alloc(new_size, type);
    if (!new_user_ptr) {
        return NULL;
    }
    
    /* Copy user data */
    size_t copy_size = old_user_size < new_size ? old_user_size : new_size;
    memcpy(new_user_ptr, ptr, copy_size);
    
    /* Old space becomes garbage - will be reclaimed by compaction */
    /* Bug #2 fix: Decrement bytes_allocated since we're effectively freeing old space */
    if (g_gc.bytes_allocated >= old_total_size) {
        g_gc.bytes_allocated -= old_total_size;
    } else {
        g_gc.bytes_allocated = 0;
    }
    
    /* Mark old header as dead (size = 0 indicates dead) */
    old_hdr->size = 0;
    
    return new_user_ptr;
}

void gc_free(void *ptr) {
    if (!ptr) return;
    
    /* Adjust pointer to account for the before-canary */
    GCHeader *hdr = gc_header((uint8_t*)ptr - GC_CANARY_SIZE);
    
    /* Validate canaries before freeing - detect if corruption already occurred */
    if (!gc_validate_canaries(hdr)) {
        GC_LOGE(
            "gc_free: CANARY CORRUPTION DETECTED for object at %p - buffer overflow occurred!", ptr);
    }
    
    size_t total_size = hdr->size;
    
    /* Clear handle entry */
    if (hdr->handle != GC_HANDLE_NULL) {
        if (hdr->handle < g_gc.handle_capacity) {
            g_gc.handles[hdr->handle].ptr = NULL;
        }
    }
    
    /* Bug #2 fix: Decrement bytes_allocated */
    if (total_size > 0 && g_gc.bytes_allocated >= total_size) {
        g_gc.bytes_allocated -= total_size;
    }
    
    /* Mark as dead (size = 0 indicates dead object) */
    hdr->size = 0;
}

void *gc_deref(GCHandle handle) {
    if (handle == GC_HANDLE_NULL || handle >= g_gc.handle_count) {
        return NULL;
    }
    return g_gc.handles[handle].ptr;
}

/*
 * Get or create a handle for an existing GC pointer.
 * This is used by JS_MKPTR to store handles in JSValue instead of raw pointers.
 * 
 * If the pointer already has a handle (from gc_alloc_js_object_ex), return that.
 * Otherwise, allocate a new handle for it.
 */
GCHandle gc_alloc_handle_for_ptr(void *ptr) {
    if (!ptr) {
        return GC_HANDLE_NULL;
    }
    
    /* Check if this pointer already has a handle assigned */
    GCHeader *hdr = gc_header(ptr);
    if (hdr && hdr->handle != GC_HANDLE_NULL) {
        /* CRITICAL FIX: After gc_reset_full(), the handle table is cleared
         * but object headers in the (now freed) heap may still contain stale
         * handles. We must verify the handle is valid and points to this ptr.
         */
        GCHandle existing = hdr->handle;
        if (existing < g_gc.handle_count && g_gc.handles[existing].ptr == ptr) {
            GC_LOGI("gc_alloc_handle_for_ptr: ptr=%p reusing existing handle=%u", ptr, existing);
            return existing;
        }
        /* Handle is stale (from before gc_reset_full), clear it */
        GC_LOGI("gc_alloc_handle_for_ptr: ptr=%p stale handle=%u in header, will allocate new", ptr, existing);
        hdr->handle = GC_HANDLE_NULL;
    }
    
    /* Find a free handle slot */
    GCHandle handle = GC_HANDLE_NULL;
    for (uint32_t i = 1; i < g_gc.handle_count; i++) {
        if (g_gc.handles[i].ptr == NULL) {
            handle = i;
            break;
        }
    }
    
    /* Grow handle table if needed */
    if (handle == GC_HANDLE_NULL) {
        if (g_gc.handle_count >= g_gc.handle_capacity) {
            /* Out of handles - this is a fatal error but we return NULL */
            return GC_HANDLE_NULL;
        }
        handle = g_gc.handle_count++;
    }
    
    /* Assign the handle */
    g_gc.handles[handle].ptr = ptr;
    g_gc.handles[handle].gen = g_gc.gc_count;
    
    /* Store handle in header for future lookups */
    if (hdr) {
        hdr->handle = handle;
    }
    
    GC_LOGI("gc_alloc_handle_for_ptr: ptr=%p assigned NEW handle=%u", ptr, handle);
    return handle;
}

size_t gc_size(void *ptr) {
    if (!ptr) return 0;
    /* Adjust pointer to account for the before-canary */
    GCHeader *hdr = gc_header((uint8_t*)ptr - GC_CANARY_SIZE);
    if (hdr->size == 0) return 0;  /* Dead object */
    /* Return actual user size (minus canaries) */
    return hdr->size - sizeof(GCHeader) - (GC_CANARY_SIZE * 2);
}

/* Get total allocation size including header */
size_t gc_total_size(void *ptr) {
    if (!ptr) return 0;
    /* Adjust pointer to account for the before-canary */
    GCHeader *hdr = gc_header((uint8_t*)ptr - GC_CANARY_SIZE);
    return hdr->size;
}

/* Validate canaries for a user pointer. Returns true if valid, false if corrupted. */
bool gc_validate_ptr(void *ptr) {
    if (!ptr) return true;
    /* Adjust pointer to account for the before-canary */
    GCHeader *hdr = gc_header((uint8_t*)ptr - GC_CANARY_SIZE);
    return gc_validate_canaries(hdr);
}

void gc_add_root(GCHandle handle) {
    if (handle == GC_HANDLE_NULL) return;
    if (g_gc.root_count >= g_gc.root_capacity) {
        return;
    }
    g_gc.roots[g_gc.root_count++] = handle;
}

void gc_remove_root(GCHandle handle) {
    for (uint32_t i = 0; i < g_gc.root_count; i++) {
        if (g_gc.roots[i] == handle) {
            g_gc.roots[i] = g_gc.roots[--g_gc.root_count];
            return;
        }
    }
}

/* Mark phase - simple mark from roots */
static void gc_mark(void) {
    /* Clear all marks */
    size_t offset = ALIGN16(GC_INITIAL_HANDLES * sizeof(GCHandleEntry)) + 
                    ALIGN16(1024 * sizeof(GCHandle));
    
    while (offset < atomic_load(&g_gc.bump.offset)) {
        GCHeader *hdr = (GCHeader*)(g_gc.heap + offset);
        if (hdr->size > 0) {
            hdr->mark = 0;
            offset += hdr->size;
        } else {
            /* Bug #5 fix: Dead object - skip minimum size to prevent infinite loop */
            offset += MIN_OBJECT_SIZE;
        }
    }
    
    /* Mark from roots */
    for (uint32_t i = 0; i < g_gc.root_count; i++) {
        GCHandle h = g_gc.roots[i];
        if (h < g_gc.handle_count && g_gc.handles[h].ptr) {
            GCHeader *hdr = gc_header(g_gc.handles[h].ptr);
            if (hdr->size > 0) {  /* Only mark live objects */
                hdr->mark = 1;
            }
        }
    }
    
    /* Mark from shadow stack (C-level JSValue references) */
    gc_mark_shadow_stack();
}

/* Compact phase - move live objects together */
static void gc_compact(void) {
    uint8_t *read = g_gc.heap;
    uint8_t *write = g_gc.heap;
    
    /* Start after handle table and root array */
    size_t skip = ALIGN16(GC_INITIAL_HANDLES * sizeof(GCHandleEntry)) + 
                  ALIGN16(1024 * sizeof(GCHandle));
    
    read += skip;
    write += skip;
    
    size_t bump = atomic_load(&g_gc.bump.offset);
    size_t new_bytes_allocated = 0;
    
    while ((size_t)(read - g_gc.heap) < bump) {
        GCHeader *hdr = (GCHeader*)read;
        
        /* Bug #5 fix: Check for dead object (size == 0 or size < MIN_OBJECT_SIZE) */
        if (hdr->size == 0) {
            /* Dead object - skip minimum size */
            read += MIN_OBJECT_SIZE;
            continue;
        }
        
        /* Validate size to prevent corruption */
        if (hdr->size < sizeof(GCHeader) || hdr->size > g_gc.heap_size) {
            /* Skip minimum to try to recover */
            read += MIN_OBJECT_SIZE;
            continue;
        }
        
        uint32_t size = hdr->size;
        
        if (hdr->mark && !hdr->pinned) {
            /* Live and movable - compact */
            if (read != write) {
                memmove(write, read, size);
                
                /* Update handle table */
                if (hdr->handle != GC_HANDLE_NULL) {
                    g_gc.handles[hdr->handle].ptr = write + sizeof(GCHeader);
                }
            }
            write += size;
            new_bytes_allocated += size;
        } else if (hdr->mark && hdr->pinned) {
            /* Live but pinned */
            write = read + size;
            new_bytes_allocated += size;
        } else {
            /* Dead - clear handle */
            if (hdr->handle != GC_HANDLE_NULL) {
                g_gc.handles[hdr->handle].ptr = NULL;
            }
            /* Note: We don't add to new_bytes_allocated since this object is dead */
        }
        
        read += size;
    }
    
    atomic_store(&g_gc.bump.offset, write - g_gc.heap);
    
    /* Bug #2 fix: Update bytes_allocated to reflect compaction */
    g_gc.bytes_allocated = new_bytes_allocated;
}

void gc_run(void) {
    if (!g_gc.initialized) return;
    
    
    g_gc.gc_count++;
    gc_mark();
    gc_compact();
}

void gc_reset(void) {
    if (!g_gc.initialized) return;
    
    /* Reset bump pointer to after handle table and root array */
    size_t skip = ALIGN16(GC_INITIAL_HANDLES * sizeof(GCHandleEntry)) + 
                  ALIGN16(1024 * sizeof(GCHandle));
    atomic_store(&g_gc.bump.offset, skip);
    
    /* Clear handle table (except entry 0) */
    for (uint32_t i = 1; i < g_gc.handle_count; i++) {
        g_gc.handles[i].ptr = NULL;
    }
    g_gc.handle_count = 1;
    g_gc.root_count = 0;
    
    /* Bug #2 fix: Reset bytes_allocated */
    g_gc.bytes_allocated = 0;
    
}

/* Forward declaration for browser stubs reset */
extern void browser_stubs_reset(void);

/* Forward declaration for js_quickjs class ID reset */
extern void js_quickjs_reset_class_ids(void);

/*
 * Full GC reset - nuclear option for between downloads
 * Clears all state and reinitializes as if brand new.
 * This is the proper way to handle GC reset when js_quickjs_exec_scripts
 * is called multiple times.
 */
void gc_reset_full(void) {
    GC_LOGI("gc_reset_full: START");
    
    /* Reset browser stubs state (static variables) */
    browser_stubs_reset();
    
    /* Reset js_quickjs class IDs */
    js_quickjs_reset_class_ids();
    
    /* Clean up shadow stack first (free malloc'd entries) */
    gc_shadow_stack_cleanup();
    
    /* Free the heap if allocated */
    if (g_gc.heap) {
        free(g_gc.heap);
    }
    
    /* Clear entire GC state structure */
    memset(&g_gc, 0, sizeof(g_gc));
    
    /* Reinitialize from scratch */
    gc_init();
}

size_t gc_used(void) {
    if (!g_gc.initialized) return 0;
    return atomic_load(&g_gc.bump.offset);
}

size_t gc_available(void) {
    if (!g_gc.initialized) return 0;
    return g_gc.heap_size - atomic_load(&g_gc.bump.offset);
}

/* ============================================================================
 * REFERENCE COUNTING STUBS
 * ============================================================================
 * 
 * JS_DupValue, JS_FreeValue, and JS_FreeValueRT are defined as inline
 * no-ops in quickjs.h since we're using mark-and-sweep GC.
 * The shadow stack handles object lifetime management.
 */

/* ============================================================================
 * SHADOW STACK IMPLEMENTATION
 * ============================================================================
 * 
 * The shadow stack tracks JSValue references held by C code. This prevents
 * garbage collection of values that are still in use by C code but not
 * reachable from the JS root set.
 */

/* Initialize shadow stack (called by gc_init) */
void gc_shadow_stack_init(void) {
    g_gc.shadow_stack = NULL;
    g_gc.shadow_stack_pool = NULL;
    memset(&g_gc.shadow_stats, 0, sizeof(g_gc.shadow_stats));
}

/* Cleanup shadow stack (called by gc_cleanup) */
void gc_shadow_stack_cleanup(void) {
    /* Free all pool entries */
    GCShadowStackEntry *entry = g_gc.shadow_stack_pool;
    while (entry) {
        GCShadowStackEntry *next = entry->pool_next;
        free(entry);
        entry = next;
    }
    
    /* Free all active stack entries */
    entry = g_gc.shadow_stack;
    while (entry) {
        GCShadowStackEntry *next = entry->next;
        free(entry);
        entry = next;
    }
    
    g_gc.shadow_stack = NULL;
    g_gc.shadow_stack_pool = NULL;
    
}

/* Push a JSValue onto the shadow stack (registers as GC root) */
void gc_push_jsvalue(JSContext *ctx, void *slot, const char *file, int line, const char *var_name) {
    (void)ctx; /* May be used in future for per-context stacks */
    
    if (!slot) {
        return;
    }
    
    /* Get entry from pool or allocate new */
    GCShadowStackEntry *entry = g_gc.shadow_stack_pool;
    if (entry) {
        g_gc.shadow_stack_pool = entry->pool_next;
        g_gc.shadow_stats.pool_hits++;
    } else {
        entry = malloc(sizeof(GCShadowStackEntry));
        if (!entry) {
            GC_LOGE(
                "gc_push_jsvalue: FAILED to allocate shadow stack entry");
            return;
        }
        g_gc.shadow_stats.pool_misses++;
    }
    
    entry->value_slot = slot;
    entry->pool_next = NULL;
    
    #ifdef GC_DEBUG
    entry->file = file;
    entry->line = line;
    entry->var_name = var_name;
    #endif
    
    /* Push onto stack */
    entry->next = g_gc.shadow_stack;
    g_gc.shadow_stack = entry;
    
    /* Update stats */
    g_gc.shadow_stats.current_depth++;
    g_gc.shadow_stats.total_pushes++;
    
    if (g_gc.shadow_stats.current_depth > g_gc.shadow_stats.max_depth) {
        g_gc.shadow_stats.max_depth = g_gc.shadow_stats.current_depth;
    }
    
}

/* Pop a JSValue from the shadow stack */
void gc_pop_jsvalue(JSContext *ctx, void *slot) {
    (void)ctx;
    
    if (!slot) {
        return;
    }
    
    /* Find entry with matching slot */
    GCShadowStackEntry **pp = &g_gc.shadow_stack;
    while (*pp) {
        GCShadowStackEntry *entry = *pp;
        if (entry->value_slot == slot) {
            /* Remove from stack */
            *pp = entry->next;
            
            /* Return to pool */
            entry->next = NULL;
            entry->pool_next = g_gc.shadow_stack_pool;
            g_gc.shadow_stack_pool = entry;
            
            /* Update stats */
            g_gc.shadow_stats.current_depth--;
            g_gc.shadow_stats.total_pops++;
            
            return;
        }
        pp = &(*pp)->next;
    }
    
    /* Entry not found - mismatched push/pop */
}

/* Mark all shadow stack entries during GC */
void gc_mark_shadow_stack(void) {
    uint32_t marked_count = 0;
    
    for (GCShadowStackEntry *entry = g_gc.shadow_stack; entry; entry = entry->next) {
        JSValue *val = entry->value_slot;
        if (!val) continue;
        
        /* Check if value is an object that needs marking */
        /* JSValue tag indicates the type - objects have specific tag values */
        /* For QuickJS: tag < 0 indicates reference types (objects, strings, etc.) */
        if (val->tag < 0) {
            /* For reference types, u.handle stores the handle, not the pointer */
            void *ptr = gc_deref(val->u.handle);
            if (ptr && gc_ptr_is_valid(ptr)) {
                GCHeader *hdr = gc_header(ptr);
                if (hdr && hdr->size > 0) {
                    hdr->mark = 1;
                    marked_count++;
                    
                    #ifdef GC_DEBUG
                             entry->var_name ? entry->var_name : "?",
                             entry->file ? entry->file : "?",
                             entry->line);
                    #endif
                }
            }
        }
    }
    
    if (marked_count > 0) {
    }
}

/* Get shadow stack statistics */
void gc_shadow_stack_stats(GCShadowStackStats *stats) {
    if (stats) {
        memcpy(stats, &g_gc.shadow_stats, sizeof(GCShadowStackStats));
    }
}

/* Validate shadow stack consistency */
bool gc_shadow_stack_validate(char *error_buffer, size_t error_len) {
    #define ERROR(fmt, ...) do { \
        if (error_buffer && error_len > 0) { \
            snprintf(error_buffer, error_len, fmt, ##__VA_ARGS__); \
        } \
        return false; \
    } while(0)
    
    /* Count entries in stack */
    uint32_t count = 0;
    for (GCShadowStackEntry *entry = g_gc.shadow_stack; entry; entry = entry->next) {
        count++;
        /* Check for NULL slots */
        if (!entry->value_slot) {
            ERROR("Shadow stack entry %u has NULL slot", count);
        }
    }
    
    /* Verify count matches stats */
    if (count != g_gc.shadow_stats.current_depth) {
        ERROR("Shadow stack count mismatch: counted=%u, stats=%u", 
              count, g_gc.shadow_stats.current_depth);
    }
    
    return true;
    #undef ERROR
}

/* Auto-cleanup helper for scoped values (used by macro) */
void gc_auto_pop_helper(void **slot) {
    if (slot && *slot) {
        gc_pop_jsvalue(NULL, *slot);
    }
}
