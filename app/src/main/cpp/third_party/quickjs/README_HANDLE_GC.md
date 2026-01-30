# QuickJS Handle-Based Garbage Collector

## Overview

This is a complete implementation of a handle-based garbage collector designed to replace QuickJS's current linked-list GC system. The key innovation is using **integer handles** instead of raw pointers for object references, enabling efficient **mark-compact collection** without expensive pointer fixup.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      Handle Table                           │
├───────────┬─────────────────────────────────────────────────┤
│ Handle 0  │ NULL (reserved)                                 │
│ Handle 1  │ Pointer ──────┐                                 │
│ Handle 2  │ Pointer ──┐   │                                 │
│ Handle 3  │ NULL      │   │                                 │
│ Handle 4  │ Pointer ──┼───┼───┐                             │
│   ...     │   ...     │   │   │                             │
└───────────┴───────────┼───┼───┼─────────────────────────────┘
                        │   │   │
                        ▼   ▼   ▼
┌─────────────────────────────────────────────────────────────┐
│                    Object Stack                             │
├─────────────────────────────────────────────────────────────┤
│ [ObjHeader+Data] [ObjHeader+Data] [ObjHeader+Data] [...]    │
│   (handle=1)       (handle=2)       (handle=4)              │
│      Live             Live            Live                  │
│                                                             │
│   Dead objects are removed, live objects compacted left     │
└─────────────────────────────────────────────────────────────┘
```

## Core Innovation: No Pointer Fixup

Traditional compacting GCs must scan the entire object graph and update every pointer when objects move. This is expensive and complex.

With handles:
- Object references are **integer IDs** (handles), not pointers
- When an object moves during compaction, only the **handle table entry** is updated
- No need to scan object graphs or know object layouts
- Compaction is O(objects) not O(objects × references)

## Files

| File | Description | Lines |
|------|-------------|-------|
| `quickjs_gc_rewrite.h` | Header with data structures and API | ~150 |
| `quickjs_gc_rewrite.c` | Full implementation | ~400 |
| `test_handle_gc.c` | Comprehensive test harness | ~600 |
| `Makefile.gc` | Build configuration | ~40 |
| `INTEGRATION_GUIDE.md` | How to integrate into QuickJS | ~200 |
| `IMPLEMENTATION_SUMMARY.md` | Design decisions and performance | ~300 |

## Quick Start

```bash
# Build and run tests
cd third_party/quickjs
make -f Makefile.gc test

# Run with valgrind for memory checking
make -f Makefile.gc valgrind

# Performance benchmark
make -f Makefile.gc perf
```

## Test Coverage

The test harness includes 15 tests covering:

- ✅ Basic allocation and dereferencing
- ✅ Reference counting lifecycle
- ✅ Handle reuse from free list
- ✅ Stack bump allocator alignment
- ✅ Out-of-memory handling
- ✅ Root management and growth
- ✅ Mark phase correctness
- ✅ Compaction and pointer stability
- ✅ Handle table updates
- ✅ 10,000 object stress test
- ✅ Fragmentation elimination
- ✅ Repeated GC cycles
- ✅ State validation
- ✅ Memory statistics

## Key API

```c
// Initialize with 64MB stack
JSHandleGCState gc;
js_handle_gc_init(&gc, 64 * 1024 * 1024);

// Allocate object
JSObjHandle handle = js_handle_alloc(&gc, object_size, type);

// Dereference (get pointer)
MyObject *obj = JS_HANDLE_DEREF(&gc, handle, MyObject);

// Reference counting
js_handle_retain(&gc, handle);
js_handle_release(&gc, handle);

// Add to GC roots (always reachable)
js_handle_add_root(&gc, handle);

// Run garbage collection
js_handle_gc_run(&gc);

// Cleanup
js_handle_gc_free(&gc);
```

## Integration Status

**Completed:**
- ✅ Handle table implementation
- ✅ Stack allocator
- ✅ Mark-compact GC
- ✅ Reference counting
- ✅ Root management
- ✅ Validation and debugging
- ✅ Comprehensive tests

**Not Completed (QuickJS Integration):**
- ⬜ Modify JSRuntime structure
- ⬜ Update all object types to use handles
- ⬜ Replace all pointer references with handle IDs
- ⬜ Integrate with QuickJS value representation
- ⬜ Run QuickJS test suite

## Performance

| Metric | Original | Handle-Based | Improvement |
|--------|----------|--------------|-------------|
| Allocation | malloc() O(1) | Bump pointer O(1) | Same |
| GC Time | O(objects × refs) | O(objects) | Better |
| Fragmentation | Yes | None | Better |
| Cache locality | Poor | Excellent | Better |
| Memory overhead | ~4 bytes/obj | ~8 bytes/obj | Worse |

## Design Philosophy

1. **Simplicity over micro-optimization**: Clear, correct code that's easy to debug
2. **Contiguous memory**: Objects stay together for cache efficiency
3. **Explicit handles**: Make object lifetime and movement explicit
4. **Testability**: Comprehensive test harness from day one

## Why This Approach?

1. **No fragmentation**: Compaction eliminates memory waste
2. **Predictable performance**: No malloc fragmentation causing OOM
3. **Easier debugging**: Handle IDs are more meaningful than pointers
4. **Safer**: Can't have dangling pointers to moved objects
5. **GC simplification**: No complex pointer fixup logic

## Future Work

1. **Incremental compaction**: Do compaction in slices to reduce pause time
2. **Generational GC**: Separate nursery for short-lived objects
3. **Parallel marking**: Use threads for marking phase
4. **Weak references**: Support for weakly-referenced handles
5. **Full QuickJS integration**: Apply to actual QuickJS codebase

## License

Same as QuickJS - MIT License

## Credits

Design inspired by:
- Lua's incremental GC
- V8's pointer compression
- JVM's handle-based object references
- Game engine memory management techniques
