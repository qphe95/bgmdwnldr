#ifndef STACK_ALLOCATOR_H
#define STACK_ALLOCATOR_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "quickjs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Stack allocator configuration */
#define STACK_ALLOCATOR_SIZE (512 * 1024 * 1024)  /* 512 MB */

/* Stack allocator state - using atomics for lock-free allocation */
typedef struct StackAllocator {
    uint8_t *buffer;
    size_t size;
    _Atomic size_t offset;  /* Atomic bump pointer */
    bool initialized;
} StackAllocator;

/* Global stack allocator instance */
extern StackAllocator g_stack_allocator;

/* Initialize the stack allocator */
bool stack_allocator_init(void);

/* Cleanup the stack allocator */
void stack_allocator_cleanup(void);

/* Reset the stack allocator (free all memory at once) */
void stack_allocator_reset(void);

/* Get the default QuickJS malloc functions using stack allocator */
const JSMallocFunctions *stack_allocator_get_js_funcs(void);

/* Direct allocation from stack (not through QuickJS) */
void *stack_alloc(size_t size);
void *stack_realloc(void *ptr, size_t old_size, size_t new_size);
void stack_free(void *ptr);

/* Get current usage stats */
size_t stack_allocator_used(void);
size_t stack_allocator_available(void);

#ifdef __cplusplus
}
#endif

#endif /* STACK_ALLOCATOR_H */
