# QuickJS Shape Corruption Analysis

## Summary

The crash occurs when `JS_SetPropertyStr` is called with an object whose `shape` pointer is corrupted to `-1` (0xFFFFFFFFFFFFFFFF).

## Crash Details

From the tombstone:
```
signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x0000000000000007
```

Fault address `0x7` is calculated as:
- `shape_ptr + offsetof(JSShape, prop_hash_mask)` = `0xFFFFFFFFFFFFFFFF + 8` = `0x7` (with overflow)

This confirms `shape = -1`.

## Root Cause Analysis

### 1. Object Layout (ARM64)
```
JSObject structure:
  offset 0:  uint16_t class_id
  offset 2:  uint8_t flags
  offset 4:  uint32_t weakref_count
  offset 8:  JSShape *shape      <- CRASH: this is -1
  offset 16: JSProperty *prop
```

### 2. JSShape Structure
```
JSShape structure:
  offset 0:  uint8_t is_hashed
  offset 4:  uint32_t hash
  offset 8:  uint32_t prop_hash_mask   <- Crash when reading from here
```

### 3. Code Path
```
JS_SetPropertyStr
  -> JS_SetPropertyInternal
    -> find_own_property
      -> sh = p->shape                    (shape = -1)
      -> h = atom & sh->prop_hash_mask    (CRASH: reading from 0x7)
```

### 4. Why Validation Fails

Current validation in `find_own_property`:
```c
if (unlikely((uintptr_t)p->shape < 0x1000)) {
    // Handle invalid
}
```

This check fails because `-1` as unsigned is `0xFFFFFFFFFFFFFFFF`, which is `> 0x1000`.

The `check_object_shape` function in `debug_shape.h` DOES check for `-1`:
```c
if (shape_ptr == (uintptr_t)-1 || shape_ptr == 0 || shape_ptr < 0x1000)
```

But this check is called BEFORE `JS_SetPropertyStr`, and by the time `JS_SetPropertyStr` is called, the shape has already been corrupted.

## Hypothesis: Memory Corruption Timeline

1. Object created successfully via `JS_NewObject()`
   - Shape is valid (e.g., 0xb400007888c27568)
   - `check_object_shape()` passes

2. Some code executes between object creation and `JS_SetPropertyStr`

3. Memory corruption occurs
   - The shape pointer at offset 8 in the JSObject is overwritten with -1
   - Possible causes:
     a. Buffer overflow from adjacent object
     b. Use-after-free of another object that writes to same memory
     c. GC compaction issue
     d. Stack corruption

4. `JS_SetPropertyStr` is called
   - Reads corrupted shape pointer (-1)
   - Crash at `sh->prop_hash_mask`

## Evidence

From `check_object_shape` logs, we should see:
```
check_object_shape(reflect_obj): obj=0x..., shape=0x... (VALID)
... (some operations)
[CRASH in JS_SetPropertyStr with obj=0x...]
```

But we need to see what's happening BETWEEN the check and the crash.

## Next Steps for Investigation

### 1. Add Watchpoint Debugging
Set a watchpoint on the shape field of the object after creation:
```lldb
watchpoint set expression -w write -- (obj_addr + 8)
```

### 2. Add More Defensive Checks
Add `-1` check to `find_own_property`:
```c
if (unlikely((uintptr_t)p->shape == (uintptr_t)-1 || 
             (uintptr_t)p->shape < 0x1000)) {
    QJS_LOGE("find_own_property: corrupted shape %p", p->shape);
    *ppr = NULL;
    return NULL;
}
```

### 3. Memory Audit
Add memory barrier validation after object creation:
```c
JSValue obj = JS_NewObject(ctx);
if (JS_IsObject(obj)) {
    void* ptr = JS_VALUE_GET_PTR(obj);
    JSShape* sh = ((JSObject*)ptr)->shape;
    if (sh == (JSShape*)-1) {
        // Corruption detected immediately after creation!
    }
}
```

### 4. GC Investigation
The unified GC uses a bump allocator. Check if:
- GC is triggering during object creation
- Memory is being compacted/moved incorrectly
- Shadow stack is corrupting objects

## Potential Fixes

### Option 1: Add -1 check (immediate fix)
```c
// In find_own_property and other shape accessors
if (unlikely((uintptr_t)p->shape == (uintptr_t)-1 ||
             (uintptr_t)p->shape == 0 ||
             (uintptr_t)p->shape < 0x1000)) {
    return NULL;  // Handle gracefully
}
```

### Option 2: Magic number validation
Add a magic number field to JSObject that's checked before accessing shape.

### Option 3: Memory protection
Use guard pages or canaries around GC objects.

### Option 4: Fix root cause
Find and fix the memory corruption (requires watchpoint debugging).

## Debugging Commands

```bash
# Build with extra debugging
./rebuild.sh

# Run with LLDB
./debug_with_lldb.sh

# In LLDB:
(lldb) command script import lldb_shape_investigator.py
(lldb) shape-investigate-start
(lldb) continue

# When breakpoint hit:
(lldb) shape-check 0x<baddr>
(lldb) memory read 0x<obj_addr> -s 8 -c 4
```

## Recommended Immediate Action

1. Apply the `-1` check to `find_own_property` (defensive)
2. Add watchpoint debugging to catch the corruption in action
3. Investigate memory layout - check if adjacent objects are overflowing
