# GC Handle Fix Summary

## Problem
JSValue was storing direct pointers to GC-managed objects. When the GC compacts the heap (moving objects to reduce fragmentation), these pointers become invalid (dangling pointers), causing crashes.

## Attempted Fix

### 1. Handle-Based Indirection (Reverted)
Modified JSValue to store handles instead of direct pointers:
- `JS_VALUE_GET_PTR()` now uses `gc_deref()` to look up the pointer from the handle table
- `JS_MKPTR()` allocates a handle and stores it in the JSValue
- During GC compaction, only the handle table is updated, handles remain stable

**Status**: Reverted due to complexity of maintaining two handle systems (global GC table and per-runtime arrays).

### 2. Compaction Disabled (Current)
Since QuickJS stores many raw pointers to GC objects (e.g., `JSShape->proto`), and fixing all of them is a major undertaking, we instead disabled heap compaction:

```c
// In gc_compact() - objects are NOT moved
if (hdr->mark) {
    // Live object - count it but don't move it (treat as pinned)
    new_bytes_allocated += size;
}
```

**Status**: Active, but crashes persist due to underlying memory corruption.

## Remaining Issue
Even with direct pointers and compaction disabled, the app crashes at `JS_SetPropertyInternal+1476` with:
- Fault address: 0x7 (NULL pointer + offset 7)
- Crash happens during prototype chain traversal
- Root cause: Memory corruption in the heap, likely related to shape allocation/freeing

The crash log shows:
```
js_free_shape0: freeing memory...
js_free_shape0: DONE
...
JS_SetPropertyInternal: got proto p1=0xb40000788465a450
<crash>
```

This suggests the prototype object (p1) may have been corrupted by the shape freeing code.

## Next Steps
1. Investigate memory corruption in shape management
2. Add bounds checking and validation for proto pointers
3. Consider using a memory debugger (AddressSanitizer) to find corruption source
4. Review shape lifecycle management to ensure protos aren't corrupted

## Files Modified
- `quickjs.h` - JSValue macros (direct pointers, no handles)
- `quickjs_gc_unified.c` - Disabled compaction

## Testing
Build passes but runtime crashes persist due to memory corruption.
