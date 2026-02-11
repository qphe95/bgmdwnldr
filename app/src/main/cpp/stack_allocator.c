#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <android/log.h>
#include "stack_allocator.h"

#define LOG_TAG "StackAllocator"
#define LOG_INFO(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOG_ERROR(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* Global stack allocator instance */
StackAllocator g_stack_allocator = {
    .buffer = NULL,
    .size = 0,
    .offset = ATOMIC_VAR_INIT(0),
    .initialized = false
};

/* 
 * Atomic bump allocation
 * 
 * Uses atomic_fetch_add to reserve space atomically:
 * 1. Atomically add aligned_size to offset
 * 2. The return value is the OLD offset (our allocation start)
 * 3. If offset + size exceeds buffer, we have a problem - but we already reserved it
 *    so we need to handle this case
 * 
 * For simplicity, we check bounds after reservation and return NULL if overcommitted.
 * This wastes a bit of space but keeps the code simple and lock-free.
 */
static void *stack_js_malloc(JSMallocState *s, size_t size)
{
    (void)s;
    
    if (size == 0)
        return NULL;
    
    /* Align to 16 bytes for proper memory alignment */
    size_t aligned_size = (size + 15) & ~15;
    
    /* Atomically reserve space by bumping the offset */
    size_t old_offset = atomic_fetch_add(&g_stack_allocator.offset, aligned_size);
    size_t new_offset = old_offset + aligned_size;
    
    /* Check if we exceeded the buffer size */
    if (!g_stack_allocator.initialized || new_offset > g_stack_allocator.size) {
        /* We've overcommitted - this allocation fails */
        LOG_ERROR("Stack allocator out of memory! Requested: %zu, Total would be: %zu", 
                  aligned_size, new_offset);
        return NULL;
    }
    
    /* Return pointer to reserved space */
    return g_stack_allocator.buffer + old_offset;
}

/* Free is a no-op for stack allocator - memory is freed all at once */
static void stack_js_free(JSMallocState *s, void *ptr)
{
    (void)s;
    (void)ptr;
    /* No-op: individual frees don't do anything in a stack allocator */
}

/* Realloc for stack allocator */
static void *stack_js_realloc(JSMallocState *s, void *ptr, size_t size)
{
    (void)s;
    
    if (!ptr) {
        return stack_js_malloc(s, size);
    }
    
    if (size == 0) {
        return NULL;
    }
    
    /* 
     * Stack allocator realloc strategy:
     * Since we can't shrink/grow in place easily, we always allocate new
     * and copy. This is inefficient but simple and safe.
     */
    void *new_ptr = stack_js_malloc(s, size);
    if (!new_ptr) {
        return NULL;
    }
    
    /* Copy old data */
    memcpy(new_ptr, ptr, size);
    
    return new_ptr;
}

/* Usable size - return 0 as we don't track individual sizes */
static size_t stack_js_malloc_usable_size(const void *ptr)
{
    (void)ptr;
    return 0;
}

/* Static JSMallocFunctions for QuickJS */
static const JSMallocFunctions stack_malloc_funcs = {
    .js_malloc = stack_js_malloc,
    .js_free = stack_js_free,
    .js_realloc = stack_js_realloc,
    .js_malloc_usable_size = stack_js_malloc_usable_size,
};

const JSMallocFunctions *stack_allocator_get_js_funcs(void)
{
    return &stack_malloc_funcs;
}

bool stack_allocator_init(void)
{
    /* Simple check - not thread-safe during init, but init should be called once */
    if (g_stack_allocator.initialized) {
        return true;
    }
    
    g_stack_allocator.buffer = malloc(STACK_ALLOCATOR_SIZE);
    if (!g_stack_allocator.buffer) {
        LOG_ERROR("Failed to allocate %lu bytes for stack allocator", (unsigned long)STACK_ALLOCATOR_SIZE);
        return false;
    }
    
    g_stack_allocator.size = STACK_ALLOCATOR_SIZE;
    atomic_store(&g_stack_allocator.offset, 0);
    g_stack_allocator.initialized = true;
    
    LOG_INFO("Stack allocator initialized: %lu MB buffer (lock-free)", 
             (unsigned long)(STACK_ALLOCATOR_SIZE / (1024 * 1024)));
    return true;
}

void stack_allocator_cleanup(void)
{
    if (g_stack_allocator.buffer) {
        free(g_stack_allocator.buffer);
        g_stack_allocator.buffer = NULL;
    }
    
    g_stack_allocator.size = 0;
    atomic_store(&g_stack_allocator.offset, 0);
    g_stack_allocator.initialized = false;
    
    LOG_INFO("Stack allocator cleaned up");
}

void stack_allocator_reset(void)
{
    size_t was_used = atomic_load(&g_stack_allocator.offset);
    atomic_store(&g_stack_allocator.offset, 0);
    
    LOG_INFO("Stack allocator reset: freed %lu bytes", (unsigned long)was_used);
}

/* Direct allocation functions for use outside QuickJS */
void *stack_alloc(size_t size)
{
    return stack_js_malloc(NULL, size);
}

void *stack_realloc(void *ptr, size_t old_size, size_t new_size)
{
    (void)old_size;
    return stack_js_realloc(NULL, ptr, new_size);
}

void stack_free(void *ptr)
{
    stack_js_free(NULL, ptr);
}

size_t stack_allocator_used(void)
{
    return atomic_load(&g_stack_allocator.offset);
}

size_t stack_allocator_available(void)
{
    if (!g_stack_allocator.initialized) {
        return 0;
    }
    return g_stack_allocator.size - atomic_load(&g_stack_allocator.offset);
}
