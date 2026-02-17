#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <android/log.h>
#include "quickjs_gc_unified.h"
#include "quickjs.h"

#define ALIGN16(size) (((size) + 15) & ~15)
#define MIN_OBJECT_SIZE (sizeof(GCHeader) + 16)

GCState g_gc = {0};

static void gc_run_internal(void);
static void gc_maybe_run(void);

bool gc_init(void) {
    if (g_gc.initialized) return true;
    
    g_gc.heap = malloc(GC_HEAP_SIZE);
    if (!g_gc.heap) return false;
    
    g_gc.heap_size = GC_HEAP_SIZE;
    g_gc.bump.base = g_gc.heap;
    atomic_store(&g_gc.bump.offset, 0);
    g_gc.bump.capacity = GC_HEAP_SIZE;
    
    size_t handle_table_size = ALIGN16(GC_INITIAL_HANDLES * sizeof(void*));
    g_gc.handles.ptrs = (void**)g_gc.heap;
    memset(g_gc.handles.ptrs, 0, handle_table_size);
    g_gc.handles.capacity = GC_INITIAL_HANDLES;
    g_gc.handles.count = 1;
    
    size_t root_capacity = 1024;
    size_t root_size = ALIGN16(root_capacity * sizeof(GCHandle));
    g_gc.root_set.roots = (GCHandle*)(g_gc.heap + handle_table_size);
    g_gc.root_set.capacity = root_capacity;
    g_gc.root_set.count = 0;
    
    atomic_store(&g_gc.bump.offset, handle_table_size + root_size);
    g_gc.bytes_allocated = 0;
    g_gc.gc_threshold = GC_DEFAULT_THRESHOLD;
    g_gc.rt = NULL;
    
    g_gc.initialized = true;
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
}

void gc_set_runtime(JSRuntime *rt) {
    g_gc.rt = rt;
}

void gc_set_handle_finalizer(GCHandle handle, GCFinalizerFunc *finalizer) {
    if (handle == GC_HANDLE_NULL || handle >= g_gc.handles.count) return;
    
    void *ptr = g_gc.handles.ptrs[handle];
    if (!ptr) return;
    
    GCHeader *hdr = gc_header(ptr);
    if (!hdr) return;
    
    hdr->finalizer = finalizer;
}

GCFinalizerFunc *gc_get_handle_finalizer(GCHandle handle) {
    if (handle == GC_HANDLE_NULL || handle >= g_gc.handles.count) return NULL;
    
    void *ptr = g_gc.handles.ptrs[handle];
    if (!ptr) return NULL;
    
    GCHeader *hdr = gc_header(ptr);
    if (!hdr) return NULL;
    
    return hdr->finalizer;
}

void gc_run_finalizer(JSRuntime *rt, GCHandle handle) {
    if (handle == GC_HANDLE_NULL || handle >= g_gc.handles.count) return;
    
    void *ptr = g_gc.handles.ptrs[handle];
    if (!ptr) return;
    
    GCHeader *hdr = gc_header(ptr);
    if (!hdr) return;
    
    /* Call per-handle finalizer if registered */
    if (hdr->finalizer) {
        hdr->finalizer(rt, handle, ptr);
    }
}

static void *bump_alloc(size_t size) {
    size_t total_size = sizeof(GCHeader) + ALIGN16(size);
    
    size_t old_offset = atomic_load(&g_gc.bump.offset);
    size_t new_offset;
    
    do {
        new_offset = old_offset + total_size;
        if (new_offset > g_gc.heap_size) return NULL;
    } while (!atomic_compare_exchange_weak(&g_gc.bump.offset, &old_offset, new_offset));
    
    uint8_t *ptr = g_gc.heap + old_offset;
    GCHeader *hdr = (GCHeader*)ptr;
    
    hdr->ref_count_unused = 0;
    hdr->gc_obj_type = 0;
    hdr->mark = 0;
    hdr->dummy0 = 0;
    hdr->dummy1 = 0;
    hdr->dummy2 = 0;
    hdr->link.next = NULL;
    hdr->link.prev = NULL;
    hdr->handle = GC_HANDLE_NULL;
    hdr->size = total_size;
    hdr->flags = 0;
    hdr->pad = 0;
    hdr->finalizer = NULL;  /* No finalizer by default */
    
    memset(ptr + sizeof(GCHeader), 0, ALIGN16(size));
    return ptr + sizeof(GCHeader);
}

static GCHandle allocate_handle(void *ptr) {
    if (!ptr) return GC_HANDLE_NULL;
    
    for (uint32_t i = 1; i < g_gc.handles.count; i++) {
        if (g_gc.handles.ptrs[i] == NULL) {
            g_gc.handles.ptrs[i] = ptr;
            return i;
        }
    }
    
    if (g_gc.handles.count >= g_gc.handles.capacity) {
        return GC_HANDLE_NULL;
    }
    
    GCHandle handle = g_gc.handles.count++;
    g_gc.handles.ptrs[handle] = ptr;
    return handle;
}

bool gc_ptr_is_valid(void *ptr) {
    if (!ptr) return false;
    if (!g_gc.initialized) return false;
    /* Check if pointer is within heap range */
    if ((uint8_t*)ptr < g_gc.heap || (uint8_t*)ptr >= g_gc.heap + g_gc.heap_size)
        return false;
    return true;
}

GCHandle gc_alloc_handle_for_ptr(void *ptr) {
    return allocate_handle(ptr);
}

GCHandle gc_alloc(size_t size, JSGCObjectTypeEnum gc_obj_type) {
    return gc_alloc_ex(size, gc_obj_type, GC_HANDLE_ARRAY_GC);
}

GCHandle gc_alloc_ex(size_t size, JSGCObjectTypeEnum gc_obj_type,
                     GCHandleArrayType array_type) {
    (void)array_type;
    
    if (!g_gc.initialized) return GC_HANDLE_NULL;
    gc_maybe_run();
    
    void *ptr = bump_alloc(size);
    if (!ptr) {
        gc_run_internal();
        ptr = bump_alloc(size);
        if (!ptr) return GC_HANDLE_NULL;
    }
    
    GCHeader *hdr = gc_header(ptr);
    hdr->gc_obj_type = gc_obj_type;
    
    GCHandle handle = allocate_handle(ptr);
    if (handle == GC_HANDLE_NULL) {
        hdr->size = 0;
        return GC_HANDLE_NULL;
    }
    hdr->handle = handle;
    g_gc.bytes_allocated += hdr->size;
    
    return handle;
}

GCHandle gc_realloc(GCHandle handle, size_t new_size) {
    if (handle == GC_HANDLE_NULL) {
        return gc_alloc(new_size, JS_GC_OBJ_TYPE_DATA);
    }
    
    if (new_size == 0) return GC_HANDLE_NULL;
    
    void *old_ptr = gc_deref(handle);
    if (!old_ptr) return gc_alloc(new_size, JS_GC_OBJ_TYPE_DATA);
    
    GCHeader *old_hdr = gc_header(old_ptr);
    JSGCObjectTypeEnum old_type = old_hdr->gc_obj_type;
    
    GCHandle new_handle = gc_alloc(new_size, old_type);
    if (new_handle == GC_HANDLE_NULL) return GC_HANDLE_NULL;
    
    void *new_ptr = gc_deref(new_handle);
    size_t old_user_size = old_hdr->size - sizeof(GCHeader);
    size_t copy_size = old_user_size < new_size ? old_user_size : new_size;
    memcpy(new_ptr, old_ptr, copy_size);
    
    old_hdr->size = 0;
    g_gc.handles.ptrs[handle] = NULL;
    
    return new_handle;
}

GCHandle gc_realloc2(GCHandle handle, size_t new_size, size_t *pslack) {
    GCHandle new_handle = gc_realloc(handle, new_size);
    if (pslack && new_handle != GC_HANDLE_NULL) {
        size_t usable = gc_usable_size(new_handle);
        *pslack = (usable > new_size) ? (usable - new_size) : 0;
    }
    return new_handle;
}

void *gc_deref(GCHandle handle) {
    if (handle == GC_HANDLE_NULL || handle >= g_gc.handles.count) {
        return NULL;
    }
    return g_gc.handles.ptrs[handle];
}

bool gc_handle_is_valid(GCHandle handle) {
    if (handle == GC_HANDLE_NULL || handle >= g_gc.handles.count) {
        return false;
    }
    void *ptr = g_gc.handles.ptrs[handle];
    if (!ptr) return false;
    GCHeader *hdr = gc_header(ptr);
    return hdr->size > 0;
}

JSGCObjectTypeEnum gc_handle_get_type(GCHandle handle) {
    void *ptr = gc_deref(handle);
    if (!ptr) return JS_GC_OBJ_TYPE_COUNT;
    GCHeader *hdr = gc_header(ptr);
    if (hdr->size == 0) return JS_GC_OBJ_TYPE_COUNT;
    return (JSGCObjectTypeEnum)hdr->gc_obj_type;
}

static void gc_mark(void) {
    size_t offset = ALIGN16(GC_INITIAL_HANDLES * sizeof(void*)) + 
                    ALIGN16(1024 * sizeof(GCHandle));
    
    while (offset < atomic_load(&g_gc.bump.offset)) {
        GCHeader *hdr = (GCHeader*)(g_gc.heap + offset);
        if (hdr->size > 0) {
            hdr->mark = 0;
            offset += hdr->size;
        } else {
            offset += MIN_OBJECT_SIZE;
        }
    }
    
    for (uint32_t i = 0; i < g_gc.root_set.count; i++) {
        GCHandle h = g_gc.root_set.roots[i];
        if (h < g_gc.handles.count && g_gc.handles.ptrs[h]) {
            GCHeader *hdr = gc_header(g_gc.handles.ptrs[h]);
            if (hdr->size > 0) hdr->mark = 1;
        }
    }
}

static void gc_compact(void) {
    size_t skip = ALIGN16(GC_INITIAL_HANDLES * sizeof(void*)) + 
                  ALIGN16(1024 * sizeof(GCHandle));
    
    uint8_t *read = g_gc.heap + skip;
    uint8_t *write = read;
    size_t bump = atomic_load(&g_gc.bump.offset);
    size_t new_bytes = 0;
    JSRuntime *rt = g_gc.rt;
    
    while ((size_t)(read - g_gc.heap) < bump) {
        GCHeader *hdr = (GCHeader*)read;
        
        if (hdr->size == 0 || hdr->size < sizeof(GCHeader)) {
            read += MIN_OBJECT_SIZE;
            continue;
        }
        
        uint32_t size = hdr->size;
        
        if (hdr->mark) {
            if (read != write) {
                memmove(write, read, size);
                GCHeader *new_hdr = (GCHeader*)write;
                if (new_hdr->handle < g_gc.handles.count) {
                    g_gc.handles.ptrs[new_hdr->handle] = write + sizeof(GCHeader);
                }
            }
            write += size;
            new_bytes += size;
        } else {
            /* Object is being freed - run finalizer if registered */
            if (hdr->handle < g_gc.handles.count) {
                /* Call per-handle finalizer if one is registered */
                if (rt && hdr->finalizer) {
                    hdr->finalizer(rt, hdr->handle, read + sizeof(GCHeader));
                }
                g_gc.handles.ptrs[hdr->handle] = NULL;
            }
        }
        
        read += size;
    }
    
    atomic_store(&g_gc.bump.offset, write - g_gc.heap);
    g_gc.bytes_allocated = new_bytes;
}

static void gc_run_internal(void) {
    if (!g_gc.initialized) return;
    gc_mark();
    gc_compact();
}

static void gc_maybe_run(void) {
    if (g_gc.bytes_allocated > g_gc.gc_threshold) {
        gc_run_internal();
    }
}

void gc_run(void) {
    gc_run_internal();
}

void gc_reset(void) {
    if (!g_gc.initialized) return;
    
    size_t skip = ALIGN16(GC_INITIAL_HANDLES * sizeof(void*)) + 
                  ALIGN16(1024 * sizeof(GCHandle));
    atomic_store(&g_gc.bump.offset, skip);
    
    for (uint32_t i = 1; i < g_gc.handles.count; i++) {
        g_gc.handles.ptrs[i] = NULL;
    }
    g_gc.handles.count = 1;
    g_gc.root_set.count = 0;
    g_gc.bytes_allocated = 0;
}

void gc_reset_full(void) {
    extern void browser_stubs_reset(void);
    extern void js_quickjs_reset_class_ids(void);
    browser_stubs_reset();
    js_quickjs_reset_class_ids();
    gc_cleanup();
    gc_init();
}

void gc_add_root(GCHandle handle) {
    if (handle == GC_HANDLE_NULL) return;
    if (g_gc.root_set.count >= g_gc.root_set.capacity) return;
    g_gc.root_set.roots[g_gc.root_set.count++] = handle;
}

void gc_remove_root(GCHandle handle) {
    for (uint32_t i = 0; i < g_gc.root_set.count; i++) {
        if (g_gc.root_set.roots[i] == handle) {
            g_gc.root_set.roots[i] = g_gc.root_set.roots[--g_gc.root_set.count];
            return;
        }
    }
}

size_t gc_used_bytes(void) {
    if (!g_gc.initialized) return 0;
    return atomic_load(&g_gc.bump.offset);
}

size_t gc_available_bytes(void) {
    if (!g_gc.initialized) return 0;
    return g_gc.heap_size - atomic_load(&g_gc.bump.offset);
}

size_t gc_total_bytes(void) {
    return g_gc.heap_size;
}
