/*
 * Unified GC Allocator Implementation
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <android/log.h>
#include "quickjs_gc_unified.h"

#define LOG_TAG "GCUnified"
#define LOG_INFO(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOG_ERROR(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* Global GC instance - lives in BSS */
GCState g_gc = {0};

/* Align size to 16 bytes */
#define ALIGN16(size) (((size) + 15) & ~15)

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
    g_gc.initialized = true;
    
    LOG_INFO("Unified GC initialized: %zu MB heap, %u initial handles",
             (size_t)GC_HEAP_SIZE / (1024*1024), GC_INITIAL_HANDLES);
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
    
    /* Initialize header */
    hdr->size = total_size;
    hdr->type = type;
    hdr->mark = 0;
    hdr->pinned = 0;
    hdr->flags = 0;
    hdr->handle = GC_HANDLE_NULL;
    
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

void *gc_realloc(void *ptr, size_t new_size) {
    if (!ptr) {
        return gc_alloc_raw(new_size);
    }
    
    if (new_size == 0) {
        gc_free(ptr);
        return NULL;
    }
    
    /* Get old info */
    GCHeader *old_hdr = gc_header(ptr);
    size_t old_user_size = old_hdr->size - sizeof(GCHeader);
    GCType type = old_hdr->type;
    GCHandle old_handle = old_hdr->handle;
    
    /* Allocate new space */
    void *new_ptr = gc_alloc(new_size, type);
    if (!new_ptr) {
        return NULL;
    }
    
    /* Copy data */
    size_t copy_size = old_user_size < new_size ? old_user_size : new_size;
    memcpy(new_ptr, ptr, copy_size);
    
    /* Update handle if present */
    if (old_handle != GC_HANDLE_NULL) {
        GCHeader *new_hdr = gc_header(new_ptr);
        new_hdr->handle = old_handle;
        g_gc.handles[old_handle].ptr = new_ptr;
        g_gc.handles[old_handle].gen = g_gc.gc_count;
    }
    
    /* Old space becomes garbage - will be reclaimed by compaction */
    old_hdr->size = 0;  /* Mark as dead */
    
    return new_ptr;
}

void gc_free(void *ptr) {
    if (!ptr) return;
    
    GCHeader *hdr = gc_header(ptr);
    
    /* Clear handle entry */
    if (hdr->handle != GC_HANDLE_NULL) {
        if (hdr->handle < g_gc.handle_capacity) {
            g_gc.handles[hdr->handle].ptr = NULL;
        }
    }
    
    /* Mark as dead */
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
    uint8_t *p = g_gc.heap;
    size_t offset = ALIGN16(GC_INITIAL_HANDLES * sizeof(GCHandleEntry)) + 
                    ALIGN16(1024 * sizeof(GCHandle));
    
    while (offset < atomic_load(&g_gc.bump.offset)) {
        GCHeader *hdr = (GCHeader*)(g_gc.heap + offset);
        if (hdr->size > 0) {
            hdr->mark = 0;
            offset += hdr->size;
        } else {
            /* Dead object, skip */
            offset += sizeof(GCHeader);
        }
    }
    
    /* Mark from roots */
    for (uint32_t i = 0; i < g_gc.root_count; i++) {
        GCHandle h = g_gc.roots[i];
        if (h < g_gc.handle_count && g_gc.handles[h].ptr) {
            GCHeader *hdr = gc_header(g_gc.handles[h].ptr);
            hdr->mark = 1;
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
    
    while ((size_t)(read - g_gc.heap) < bump) {
        GCHeader *hdr = (GCHeader*)read;
        
        if (hdr->size == 0) {
            /* Dead object, skip */
            read += sizeof(GCHeader);
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
        } else if (hdr->mark && hdr->pinned) {
            /* Live but pinned */
            write = read + size;
        } else {
            /* Dead - clear handle */
            if (hdr->handle != GC_HANDLE_NULL) {
                g_gc.handles[hdr->handle].ptr = NULL;
            }
        }
        
        read += size;
    }
    
    atomic_store(&g_gc.bump.offset, write - g_gc.heap);
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
