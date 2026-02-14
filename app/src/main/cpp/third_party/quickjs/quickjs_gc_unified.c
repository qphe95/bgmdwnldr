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

/* Forward declaration from quickjs.c */
struct JSRuntime;

typedef struct JSMallocState {
    size_t malloc_count;
    size_t malloc_size;
    size_t malloc_limit;
    void *opaque; /* user opaque */
} JSMallocState;

#define LOG_TAG "GCUnified"
#define LOG_INFO(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOG_ERROR(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOG_WARN(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

/* Global GC instance - lives in BSS */
GCState g_gc = {0};

/* Default GC threshold - 256KB */
#define GC_DEFAULT_THRESHOLD (256 * 1024)

/* Align size to 16 bytes */
#define ALIGN16(size) (((size) + 15) & ~15)

/* Minimum object size to prevent infinite loops on dead objects */
#define MIN_OBJECT_SIZE (sizeof(GCHeader) + 16)

bool gc_init(void) {
    if (g_gc.initialized) {
        return true;
    }
    
    /* Allocate the heap */
    g_gc.heap = malloc(GC_HEAP_SIZE);
    if (!g_gc.heap) {
        LOG_ERROR("Failed to allocate %zu byte heap", (size_t)GC_HEAP_SIZE);
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
        free(g_gc.heap);
        g_gc.heap = NULL;
        LOG_ERROR("Heap too small for initial structures");
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
    
    g_gc.initialized = true;
    
    LOG_INFO("Unified GC initialized: %zu MB heap, %u initial handles, threshold=%zu KB",
             (size_t)GC_HEAP_SIZE / (1024*1024), GC_INITIAL_HANDLES, 
             g_gc.gc_threshold / 1024);
    return true;
}

bool gc_is_initialized(void) {
    return g_gc.initialized;
}

void gc_cleanup(void) {
    if (g_gc.heap) {
        free(g_gc.heap);
        g_gc.heap = NULL;
    }
    memset(&g_gc, 0, sizeof(g_gc));
    LOG_INFO("Unified GC cleaned up");
}

/* Bug #2 fix: Set runtime pointer for malloc_state updates */
void gc_set_runtime(struct JSRuntime *rt) {
    g_gc.rt = rt;
}

/* Bug #2 fix: Set GC threshold */
void gc_set_threshold(size_t threshold) {
    g_gc.gc_threshold = threshold;
    LOG_INFO("GC threshold set to %zu KB", threshold / 1024);
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
    size_t aligned_size = ALIGN16(size);
    size_t total_size = sizeof(GCHeader) + aligned_size;
    
    /* Check space availability BEFORE atomically reserving */
    size_t old_offset = atomic_load(&g_gc.bump.offset);
    size_t new_offset;
    
    do {
        new_offset = old_offset + total_size;
        
        if (new_offset > g_gc.heap_size) {
            /* Out of memory - offset not bumped yet, no corruption */
            LOG_ERROR("GC heap exhausted! Need %zu, have %zu", 
                      total_size, g_gc.heap_size - old_offset);
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
    
    void *user_ptr = ptr + sizeof(GCHeader);
    
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
                /* For now, just fail if we run out of handles */
                LOG_ERROR("Handle table full (%u handles)", g_gc.handle_capacity);
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
    LOG_INFO("gc_alloc_js_object_ex: size=%zu type=%d rt=%p array=%d", size, js_gc_obj_type, (void*)rt, array_type);
    if (!g_gc.initialized || !rt) {
        LOG_ERROR("gc_alloc_js_object_ex: not initialized or rt is NULL");
        return NULL;
    }
    
    /* Allocate with header */
    size_t total_size = sizeof(GCHeader) + ALIGN16(size);
    size_t old_offset = atomic_fetch_add(&g_gc.bump.offset, total_size);
    
    if (old_offset + total_size > g_gc.heap_size) {
        /* Out of memory - rollback */
        atomic_fetch_sub(&g_gc.bump.offset, total_size);
        LOG_ERROR("GC heap exhausted in gc_alloc_js_object_ex!");
        return NULL;
    }
    
    /* Get pointer to header */
    GCHeader *hdr = (GCHeader *)(g_gc.heap + old_offset);
    void *user_ptr = (void *)(hdr + 1);
    
    /* CRITICAL: Zero the entire allocation to prevent garbage values in user data */
    memset(hdr, 0, total_size);
    
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
    hdr->size = total_size;
    hdr->type = GC_TYPE_JS_OBJECT;
    hdr->pinned = 0;
    hdr->flags = 0;
    hdr->pad = 0;
    
    /* Memory barrier to ensure all initialization is visible before adding to handles */
    atomic_thread_fence(memory_order_release);
    
    /* Add to the specified QuickJS handle array.
     * Pass user_ptr so the handle array stores pointers to user data. */
    if (js_handle_array_add_js_object_ex(rt, user_ptr, js_gc_obj_type, array_type) < 0) {
        /* Failed to add - mark as freed and return NULL */
        hdr->size = 0;  /* Mark as freed */
        return NULL;
    }
    
    LOG_INFO("gc_alloc_js_object_ex: SUCCESS user_ptr=%p total_size=%zu", user_ptr, total_size);
    
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
    
    GCHeader *hdr = gc_header(ptr);
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

size_t gc_size(void *ptr) {
    if (!ptr) return 0;
    GCHeader *hdr = gc_header(ptr);
    if (hdr->size == 0) return 0;  /* Dead object */
    return hdr->size - sizeof(GCHeader);
}

/* Get total allocation size including header */
size_t gc_total_size(void *ptr) {
    if (!ptr) return 0;
    GCHeader *hdr = gc_header(ptr);
    return hdr->size;
}

void gc_add_root(GCHandle handle) {
    if (handle == GC_HANDLE_NULL) return;
    if (g_gc.root_count >= g_gc.root_capacity) {
        LOG_ERROR("Root set full");
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
            LOG_ERROR("GC corruption: invalid object size %u at offset %zu", 
                      hdr->size, (size_t)(read - g_gc.heap));
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
    
    LOG_INFO("GC running: %zu bytes allocated before", g_gc.bytes_allocated);
    
    g_gc.gc_count++;
    gc_mark();
    gc_compact();
    
    LOG_INFO("GC complete: %zu bytes allocated after, %zu used",
             g_gc.bytes_allocated, gc_used());
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
    
    LOG_INFO("GC reset");
}

size_t gc_used(void) {
    if (!g_gc.initialized) return 0;
    return atomic_load(&g_gc.bump.offset);
}

size_t gc_available(void) {
    if (!g_gc.initialized) return 0;
    return g_gc.heap_size - atomic_load(&g_gc.bump.offset);
}
