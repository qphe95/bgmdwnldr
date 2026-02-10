/*
 * Test Harness for QuickJS Handle-Based GC
 * 
 * This exercises the handle-based GC system with various scenarios.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include "quickjs_gc_rewrite.h"

/* Test result tracking */
typedef struct {
    int passed;
    int failed;
    const char *current_test;
} TestState;

static TestState g_test = {0, 0, NULL};

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    g_test.current_test = #name; \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    printf("PASSED\n"); \
    g_test.passed++; \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAILED\n  Assertion failed: %s\n  at %s:%d\n", \
               #cond, __FILE__, __LINE__); \
        g_test.failed++; \
        longjmp(g_test_jmp, 1); \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_NULL(p) ASSERT((p) == NULL)
#define ASSERT_NOT_NULL(p) ASSERT((p) != NULL)

#include <setjmp.h>
static jmp_buf g_test_jmp;

/* ===== BASIC ALLOCATION TESTS ===== */

TEST(basic_allocation) {
    JSHandleGCState gc;
    js_handle_gc_init(&gc, 1024 * 1024);  /* 1MB stack */
    
    /* Allocate some objects */
    JSObjHandle h1 = js_handle_alloc(&gc, 64, JS_GC_OBJ_TYPE_JS_OBJECT);
    JSObjHandle h2 = js_handle_alloc(&gc, 128, JS_GC_OBJ_TYPE_JS_OBJECT);
    JSObjHandle h3 = js_handle_alloc(&gc, 256, JS_GC_OBJ_TYPE_JS_OBJECT);
    
    ASSERT_NE(h1, JS_OBJ_HANDLE_NULL);
    ASSERT_NE(h2, JS_OBJ_HANDLE_NULL);
    ASSERT_NE(h3, JS_OBJ_HANDLE_NULL);
    ASSERT_NE(h1, h2);
    ASSERT_NE(h2, h3);
    
    /* Verify dereferencing - js_handle_deref returns user data, get header via js_handle_header */
    void *data1 = js_handle_deref(&gc, h1);
    void *data2 = js_handle_deref(&gc, h2);
    void *data3 = js_handle_deref(&gc, h3);
    
    ASSERT_NOT_NULL(data1);
    ASSERT_NOT_NULL(data2);
    ASSERT_NOT_NULL(data3);
    
    JSGCObjectHeader *obj1 = js_handle_header(data1);
    JSGCObjectHeader *obj2 = js_handle_header(data2);
    JSGCObjectHeader *obj3 = js_handle_header(data3);
    
    /* Verify handle in object header matches */
    ASSERT_EQ(obj1->handle, h1);
    ASSERT_EQ(obj2->handle, h2);
    ASSERT_EQ(obj3->handle, h3);
    
    /* Verify generation is current (objects are live) */
    ASSERT_EQ(gc.handles[h1].generation, gc.generation);
    ASSERT_EQ(gc.handles[h2].generation, gc.generation);
    ASSERT_EQ(gc.handles[h3].generation, gc.generation);
    
    js_handle_gc_free(&gc);
}

TEST(refcounting) {
    JSHandleGCState gc;
    js_handle_gc_init(&gc, 1024 * 1024);
    
    JSObjHandle h = js_handle_alloc(&gc, 64, JS_GC_OBJ_TYPE_JS_OBJECT);
    ASSERT_NOT_NULL(js_handle_deref(&gc, h));
    
    /* Verify handle is valid by checking object on stack */
    JSGCObjectHeader *obj = js_handle_header(js_handle_deref(&gc, h));
    ASSERT_EQ(obj->handle, h);
    
    /* Retain/release are no-ops in mark-and-sweep GC, but shouldn't crash */
    js_handle_retain(&gc, h);
    js_handle_retain(&gc, h);
    js_handle_release(&gc, h);
    js_handle_release(&gc, h);
    
    /* Handle still valid until GC collects unreachable objects */
    ASSERT_NOT_NULL(js_handle_deref(&gc, h));
    
    /* Root the object so it survives GC */
    js_handle_add_root(&gc, h);
    js_handle_gc_run(&gc);
    ASSERT_NOT_NULL(js_handle_deref(&gc, h));
    
    /* Remove root and run GC - object will be collected */
    js_handle_remove_root(&gc, h);
    js_handle_release(&gc, h);  /* Release root reference */
    js_handle_gc_run(&gc);
    
    /* Handle is now invalid (stale generation) */
    ASSERT_NULL(js_handle_deref(&gc, h));
    
    js_handle_gc_free(&gc);
}

TEST(handle_reuse) {
    JSHandleGCState gc;
    js_handle_gc_init(&gc, 1024 * 1024);
    
    /* Allocate some objects */
    JSObjHandle h1 = js_handle_alloc(&gc, 64, JS_GC_OBJ_TYPE_JS_OBJECT);
    JSObjHandle h2 = js_handle_alloc(&gc, 64, JS_GC_OBJ_TYPE_JS_OBJECT);
    
    uint32_t gen_before = gc.generation;
    
    /* Release references (objects become unreachable but handles still valid
     * until GC runs - they have stale generation after GC) */
    js_handle_release(&gc, h1);
    js_handle_release(&gc, h2);
    
    /* Before GC: handles still point to objects on stack */
    ASSERT_NOT_NULL(js_handle_deref(&gc, h1));
    ASSERT_NOT_NULL(js_handle_deref(&gc, h2));
    
    /* Run GC to collect dead objects and increment generation */
    js_handle_gc_run(&gc);
    
    /* Generation should have been incremented */
    ASSERT_EQ(gc.generation, gen_before + 1);
    
    /* After GC: old handles are invalid (stale generation) */
    ASSERT_NULL(js_handle_deref(&gc, h1));
    ASSERT_NULL(js_handle_deref(&gc, h2));
    
    /* Allocate new objects - should reuse freed handle slots */
    JSObjHandle h4 = js_handle_alloc(&gc, 64, JS_GC_OBJ_TYPE_JS_OBJECT);
    JSObjHandle h5 = js_handle_alloc(&gc, 64, JS_GC_OBJ_TYPE_JS_OBJECT);
    
    /* Handles should be reused from freed slots (lowest index first) */
    ASSERT_EQ(h4, h1);  /* h1's slot reused */
    ASSERT_EQ(h5, h2);  /* h2's slot reused */
    
    /* New handles should have current generation */
    ASSERT_EQ(gc.handles[h4].generation, gc.generation);
    ASSERT_EQ(gc.handles[h5].generation, gc.generation);
    
    js_handle_gc_free(&gc);
}

/* ===== STACK ALLOCATION TESTS ===== */

TEST(stack_allocation) {
    JSMemStack stack;
    stack.base = malloc(1024);
    stack.top = stack.base;
    stack.limit = stack.base + 1024;
    stack.capacity = 1024;
    
    /* Allocate various sizes */
    void *p1 = js_mem_stack_alloc(&stack, 8);
    void *p2 = js_mem_stack_alloc(&stack, 7);   /* Should align to 8 */
    void *p3 = js_mem_stack_alloc(&stack, 16);
    void *p4 = js_mem_stack_alloc(&stack, 24);
    
    ASSERT_NOT_NULL(p1);
    ASSERT_NOT_NULL(p2);
    ASSERT_NOT_NULL(p3);
    ASSERT_NOT_NULL(p4);
    
    /* Check alignment - all should be 8-byte aligned */
    ASSERT_EQ(((uintptr_t)p1) % 8, 0);
    ASSERT_EQ(((uintptr_t)p2) % 8, 0);
    ASSERT_EQ(((uintptr_t)p3) % 8, 0);
    ASSERT_EQ(((uintptr_t)p4) % 8, 0);
    
    /* Check contiguity */
    ASSERT_EQ(p2, (char*)p1 + 8);
    ASSERT_EQ(p3, (char*)p2 + 8);  /* 7 rounded up to 8 */
    ASSERT_EQ(p4, (char*)p3 + 16);
    
    free(stack.base);
}

TEST(stack_out_of_memory) {
    JSMemStack stack;
    stack.base = malloc(64);
    stack.top = stack.base;
    stack.limit = stack.base + 64;
    stack.capacity = 64;
    
    /* Fill the stack */
    void *p1 = js_mem_stack_alloc(&stack, 32);
    void *p2 = js_mem_stack_alloc(&stack, 24);
    ASSERT_NOT_NULL(p1);
    ASSERT_NOT_NULL(p2);
    
    /* This should fail */
    void *p3 = js_mem_stack_alloc(&stack, 16);
    ASSERT_NULL(p3);  /* Out of memory */
    
    free(stack.base);
}

/* ===== ROOT MANAGEMENT TESTS ===== */

TEST(roots_basic) {
    JSHandleGCState gc;
    js_handle_gc_init(&gc, 1024 * 1024);
    
    JSObjHandle h1 = js_handle_alloc(&gc, 64, JS_GC_OBJ_TYPE_JS_OBJECT);
    JSObjHandle h2 = js_handle_alloc(&gc, 64, JS_GC_OBJ_TYPE_JS_OBJECT);
    
    /* Add roots */
    js_handle_add_root(&gc, h1);
    ASSERT_EQ(gc.root_count, 1);
    ASSERT_EQ(gc.roots[0], h1);
    
    js_handle_add_root(&gc, h2);
    ASSERT_EQ(gc.root_count, 2);
    ASSERT_EQ(gc.roots[1], h2);
    
    /* Remove root */
    js_handle_remove_root(&gc, h1);
    ASSERT_EQ(gc.root_count, 1);
    ASSERT_EQ(gc.roots[0], h2);  /* h2 moved to slot 0 */
    
    js_handle_gc_free(&gc);
}

TEST(roots_growth) {
    JSHandleGCState gc;
    js_handle_gc_init(&gc, 1024 * 1024);
    
    /* Add many roots to trigger growth */
    JSObjHandle handles[512];
    for (int i = 0; i < 512; i++) {
        handles[i] = js_handle_alloc(&gc, 64, JS_GC_OBJ_TYPE_JS_OBJECT);
        js_handle_add_root(&gc, handles[i]);
    }
    
    ASSERT_EQ(gc.root_count, 512);
    ASSERT(gc.root_capacity >= 512);
    
    js_handle_gc_free(&gc);
}

/* ===== GARBAGE COLLECTION TESTS ===== */

TEST(gc_mark_basic) {
    JSHandleGCState gc;
    js_handle_gc_init(&gc, 1024 * 1024);
    
    /* Create objects */
    JSObjHandle h1 = js_handle_alloc(&gc, 64, JS_GC_OBJ_TYPE_JS_OBJECT);
    JSObjHandle h2 = js_handle_alloc(&gc, 64, JS_GC_OBJ_TYPE_JS_OBJECT);
    
    /* Add one to roots */
    js_handle_add_root(&gc, h1);
    
    /* Mark */
    js_handle_gc_mark(&gc);
    
    /* h1 should be marked, h2 should not */
    ASSERT_EQ(js_handle_header(js_handle_deref(&gc, h1))->mark, 1);
    ASSERT_EQ(js_handle_header(js_handle_deref(&gc, h2))->mark, 0);
    
    js_handle_gc_free(&gc);
}

TEST(gc_compact_basic) {
    JSHandleGCState gc;
    js_handle_gc_init(&gc, 1024 * 1024);
    
    /* Record base pointer */
    uint8_t *base = gc.stack.base;
    
    /* Create and root one object */
    JSObjHandle h1 = js_handle_alloc(&gc, 64, JS_GC_OBJ_TYPE_JS_OBJECT);
    js_handle_add_root(&gc, h1);
    
    /* Create unrooted object (will be freed) */
    JSObjHandle h2 = js_handle_alloc(&gc, 64, JS_GC_OBJ_TYPE_JS_OBJECT);
    (void)h2;  /* Not used, will be GC'd */
    
    /* Get pointer before GC */
    void *data_before = js_handle_deref(&gc, h1);
    JSGCObjectHeader *obj_before = js_handle_header(data_before);
    (void)obj_before;  /* Used for debugging if needed */
    
    /* Run GC */
    js_handle_gc_run(&gc);
    
    /* Get pointer after GC */
    void *data_after = js_handle_deref(&gc, h1);
    JSGCObjectHeader *obj_after = js_handle_header(data_after);
    uintptr_t ptr_after = (uintptr_t)obj_after;
    
    /* Object should still exist */
    ASSERT_NOT_NULL(data_after);
    ASSERT_EQ(obj_after->handle, h1);
    
    /* Object should be at base of stack now (compacted) */
    ASSERT_EQ(ptr_after, (uintptr_t)base);
    
    /* Stack should be smaller now */
    size_t used_after = gc.stack.top - gc.stack.base;
    ASSERT(used_after < 200);  /* Just h1, not h2 */
    
    js_handle_gc_free(&gc);
}

TEST(gc_handle_table_update) {
    JSHandleGCState gc;
    js_handle_gc_init(&gc, 1024 * 1024);
    
    /* Create multiple objects */
    JSObjHandle h1 = js_handle_alloc(&gc, 64, JS_GC_OBJ_TYPE_JS_OBJECT);
    JSObjHandle h2 = js_handle_alloc(&gc, 64, JS_GC_OBJ_TYPE_JS_OBJECT);
    JSObjHandle h3 = js_handle_alloc(&gc, 64, JS_GC_OBJ_TYPE_JS_OBJECT);
    
    /* Root h1 and h3 */
    js_handle_add_root(&gc, h1);
    js_handle_add_root(&gc, h3);
    /* h2 is unrooted - will be freed */
    
    /* Record old pointers */
    void *old1 = js_handle_deref(&gc, h1);
    void *old3 = js_handle_deref(&gc, h3);
    
    /* Run GC */
    js_handle_gc_run(&gc);
    
    /* Get new pointers via handle table */
    void *new1 = js_handle_deref(&gc, h1);
    void *new3 = js_handle_deref(&gc, h3);
    
    /* Pointers should have changed (compaction) */
    ASSERT(new1 != old1 || new3 != old3);  /* At least one moved */
    
    /* But objects should still be valid */
    ASSERT_EQ(js_handle_header(new1)->handle, h1);
    ASSERT_EQ(js_handle_header(new3)->handle, h3);
    
    /* h2 should be freed */
    ASSERT_NULL(js_handle_deref(&gc, h2));
    
    js_handle_gc_free(&gc);
}

/* ===== STRESS TESTS ===== */

TEST(stress_many_allocations) {
    JSHandleGCState gc;
    js_handle_gc_init(&gc, 16 * 1024 * 1024);  /* 16MB */
    
    const int N = 10000;
    JSObjHandle *handles = malloc(N * sizeof(JSObjHandle));
    
    /* Allocate many objects */
    fprintf(stderr, "DEBUG: Starting allocation of %d objects\n", N);
    for (int i = 0; i < N; i++) {
        handles[i] = js_handle_alloc(&gc, 64 + (i % 256), JS_GC_OBJ_TYPE_JS_OBJECT);
        if (handles[i] == JS_OBJ_HANDLE_NULL) {
            fprintf(stderr, "DEBUG: Allocation failed at i=%d\n", i);
        }
        ASSERT_NE(handles[i], JS_OBJ_HANDLE_NULL);
        if (i % 1000 == 0) {
            fprintf(stderr, "DEBUG: Allocated %d objects\n", i);
        }
    }
    fprintf(stderr, "DEBUG: Allocation complete\n");
    
    /* Root half of them */
    fprintf(stderr, "DEBUG: Adding roots...\n");
    for (int i = 0; i < N; i += 2) {
        js_handle_add_root(&gc, handles[i]);
    }
    fprintf(stderr, "DEBUG: Roots added\n");
    
    /* Release references to all */
    fprintf(stderr, "DEBUG: Releasing references...\n");
    for (int i = 0; i < N; i++) {
        js_handle_release(&gc, handles[i]);
    }
    fprintf(stderr, "DEBUG: References released\n");
    
    /* Run GC */
    fprintf(stderr, "DEBUG: Running GC...\n");
    js_handle_gc_run(&gc);
    fprintf(stderr, "DEBUG: GC complete\n");
    
    /* Check that even-numbered handles are still valid */
    for (int i = 0; i < N; i += 2) {
        void *data = js_handle_deref(&gc, handles[i]);
        ASSERT_NOT_NULL(data);
        ASSERT_EQ(js_handle_header(data)->handle, handles[i]);
    }
    
    /* Check that odd-numbered handles are freed */
    for (int i = 1; i < N; i += 2) {
        ASSERT_NULL(js_handle_deref(&gc, handles[i]));
    }
    
    free(handles);
    js_handle_gc_free(&gc);
}

TEST(stress_fragmentation) {
    JSHandleGCState gc;
    js_handle_gc_init(&gc, 8 * 1024 * 1024);  /* 8MB */
    
    const int N = 1000;
    JSObjHandle *handles = malloc(N * sizeof(JSObjHandle));
    
    /* Allocate pattern: small, large, small, large... */
    for (int i = 0; i < N; i++) {
        size_t size = (i % 2 == 0) ? 64 : 4096;
        handles[i] = js_handle_alloc(&gc, size, JS_GC_OBJ_TYPE_JS_OBJECT);
    }
    
    /* Free all large objects (odd indices) */
    for (int i = 1; i < N; i += 2) {
        js_handle_release(&gc, handles[i]);
    }
    
    /* Root small objects */
    for (int i = 0; i < N; i += 2) {
        js_handle_add_root(&gc, handles[i]);
    }
    
    /* Record stack usage before compaction */
    size_t used_before = gc.stack.top - gc.stack.base;
    
    /* Run GC - should compact and remove fragmentation */
    js_handle_gc_run(&gc);
    
    /* Stack should be much smaller now */
    size_t used_after = gc.stack.top - gc.stack.base;
    ASSERT(used_after < used_before / 2);  /* Should be significantly smaller */
    
    /* All small objects should still be valid */
    for (int i = 0; i < N; i += 2) {
        ASSERT_NOT_NULL(js_handle_deref(&gc, handles[i]));
    }
    
    free(handles);
    js_handle_gc_free(&gc);
}

TEST(stress_gc_repeated) {
    JSHandleGCState gc;
    js_handle_gc_init(&gc, 4 * 1024 * 1024);  /* 4MB */
    
    /* Simulate repeated allocate/GC cycles */
    for (int cycle = 0; cycle < 100; cycle++) {
        JSObjHandle root = js_handle_alloc(&gc, 64, JS_GC_OBJ_TYPE_JS_OBJECT);
        js_handle_add_root(&gc, root);
        
        /* Allocate temporary objects */
        for (int i = 0; i < 100; i++) {
            JSObjHandle tmp = js_handle_alloc(&gc, 128, JS_GC_OBJ_TYPE_JS_OBJECT);
            (void)tmp;  /* Temporary, will be GC'd */
        }
        
        /* Run GC */
        js_handle_gc_run(&gc);
        
        /* Verify root still valid */
        ASSERT_NOT_NULL(js_handle_deref(&gc, root));
        ASSERT_EQ(js_handle_header(js_handle_deref(&gc, root))->handle, root);
        
        /* Remove root for next cycle */
        js_handle_remove_root(&gc, root);
        js_handle_release(&gc, root);
    }
    
    js_handle_gc_free(&gc);
}

/* ===== VALIDATION TESTS ===== */

TEST(validate_empty_gc) {
    JSHandleGCState gc;
    js_handle_gc_init(&gc, 1024 * 1024);
    
    char error[256];
    ASSERT(js_handle_gc_validate(&gc, error, sizeof(error)));
    
    js_handle_gc_free(&gc);
}

TEST(validate_with_objects) {
    JSHandleGCState gc;
    js_handle_gc_init(&gc, 1024 * 1024);
    
    /* Create various objects */
    JSObjHandle h1 = js_handle_alloc(&gc, 64, JS_GC_OBJ_TYPE_JS_OBJECT);
    JSObjHandle h2 = js_handle_alloc(&gc, 128, JS_GC_OBJ_TYPE_FUNCTION_BYTECODE);
    JSObjHandle h3 = js_handle_alloc(&gc, 256, JS_GC_OBJ_TYPE_SHAPE);
    
    js_handle_add_root(&gc, h1);
    js_handle_add_root(&gc, h2);
    
    char error[256];
    ASSERT(js_handle_gc_validate(&gc, error, sizeof(error)));
    
    /* Run GC */
    js_handle_gc_run(&gc);
    
    /* Validate again */
    ASSERT(js_handle_gc_validate(&gc, error, sizeof(error)));
    
    js_handle_gc_free(&gc);
}

/* ===== STATS TESTS ===== */

TEST(stats_basic) {
    JSHandleGCState gc;
    js_handle_gc_init(&gc, 1024 * 1024);
    
    JSHandleGCStats stats;
    js_handle_gc_stats(&gc, &stats);
    
    ASSERT_EQ(stats.handle_count, 1);  /* Handle 0 reserved */
    ASSERT_EQ(stats.total_objects, 0);
    ASSERT_EQ(stats.used_bytes, 0);
    ASSERT_EQ(stats.capacity_bytes, gc.stack.capacity);
    
    /* Allocate some objects */
    JSObjHandle h1 = js_handle_alloc(&gc, 64, JS_GC_OBJ_TYPE_JS_OBJECT);
    JSObjHandle h2 = js_handle_alloc(&gc, 64, JS_GC_OBJ_TYPE_JS_OBJECT);
    (void)h1;
    (void)h2;
    
    js_handle_gc_stats(&gc, &stats);
    ASSERT_EQ(stats.total_objects, 2);
    ASSERT(stats.used_bytes >= 2 * (64 + sizeof(JSGCObjectHeader)));
    
    js_handle_gc_free(&gc);
}

/* ===== MAIN ===== */

static void print_stats(JSHandleGCState *gc) {
    JSHandleGCStats stats;
    js_handle_gc_stats(gc, &stats);
    
    printf("\nGC Statistics:\n");
    printf("  Total objects: %u\n", stats.total_objects);
    printf("  Live objects: %u\n", stats.live_objects);
    printf("  Handle count: %u\n", stats.handle_count);
    printf("  Free handles: %u\n", stats.free_handles);
    printf("  Used bytes: %zu\n", stats.used_bytes);
    printf("  Available bytes: %zu\n", stats.available_bytes);
    printf("  Capacity: %zu\n", stats.capacity_bytes);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    printf("=== QuickJS Handle-Based GC Test Suite ===\n\n");
    
    /* Run all tests */
    printf("Basic Allocation Tests:\n");
    if (setjmp(g_test_jmp) == 0) RUN_TEST(basic_allocation);
    if (setjmp(g_test_jmp) == 0) RUN_TEST(refcounting);
    if (setjmp(g_test_jmp) == 0) RUN_TEST(handle_reuse);
    
    printf("\nStack Allocation Tests:\n");
    if (setjmp(g_test_jmp) == 0) RUN_TEST(stack_allocation);
    if (setjmp(g_test_jmp) == 0) RUN_TEST(stack_out_of_memory);
    
    printf("\nRoot Management Tests:\n");
    if (setjmp(g_test_jmp) == 0) RUN_TEST(roots_basic);
    if (setjmp(g_test_jmp) == 0) RUN_TEST(roots_growth);
    
    printf("\nGarbage Collection Tests:\n");
    if (setjmp(g_test_jmp) == 0) RUN_TEST(gc_mark_basic);
    if (setjmp(g_test_jmp) == 0) RUN_TEST(gc_compact_basic);
    if (setjmp(g_test_jmp) == 0) RUN_TEST(gc_handle_table_update);
    
    printf("\nStress Tests:\n");
    if (setjmp(g_test_jmp) == 0) RUN_TEST(stress_many_allocations);
    if (setjmp(g_test_jmp) == 0) RUN_TEST(stress_fragmentation);
    if (setjmp(g_test_jmp) == 0) RUN_TEST(stress_gc_repeated);
    
    printf("\nValidation Tests:\n");
    if (setjmp(g_test_jmp) == 0) RUN_TEST(validate_empty_gc);
    if (setjmp(g_test_jmp) == 0) RUN_TEST(validate_with_objects);
    
    printf("\nStats Tests:\n");
    if (setjmp(g_test_jmp) == 0) RUN_TEST(stats_basic);
    
    /* Summary */
    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", g_test.passed);
    printf("Failed: %d\n", g_test.failed);
    
    /* Demo: Run GC and show stats */
    printf("\n=== GC Demo ===\n");
    JSHandleGCState gc;
    js_handle_gc_init(&gc, 8 * 1024 * 1024);
    
    printf("Initial state:\n");
    print_stats(&gc);
    
    /* Allocate some objects */
    printf("\nAllocating 1000 objects...\n");
    JSObjHandle handles[1000];
    for (int i = 0; i < 1000; i++) {
        handles[i] = js_handle_alloc(&gc, 64 + (i % 256), 0);
    }
    print_stats(&gc);
    
    /* Root half */
    printf("\nRooting 500 objects...\n");
    for (int i = 0; i < 1000; i += 2) {
        js_handle_add_root(&gc, handles[i]);
    }
    
    /* Release all */
    printf("\nReleasing all references...\n");
    for (int i = 0; i < 1000; i++) {
        js_handle_release(&gc, handles[i]);
    }
    print_stats(&gc);
    
    /* Run GC */
    printf("\nRunning GC...\n");
    js_handle_gc_run(&gc);
    print_stats(&gc);
    
    js_handle_gc_free(&gc);
    
    return g_test.failed > 0 ? 1 : 0;
}
