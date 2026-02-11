# BGMDWNLD Bug Investigation Report - Version 3

**Date:** 2026-02-10  
**App Version:** Latest (after user's second set of changes)  
**Platform:** Android 14 (API 34)  
**Device:** Android Emulator (arm64)  

---

## Executive Summary

**The critical crash STILL OCCURS** after your latest changes. The crash signature is identical:
- Fault address: `0x20f` (same as before)
- Crash location: `JS_SetPropertyFunctionList+1172`
- Cause: Null pointer dereference

Your attempt to fix the issue by pre-allocating `atom_array` in `JS_InitAtoms()` did not resolve the crash.

---

## 🔴 Bug #1: CRITICAL - Native Crash (STILL NOT FIXED)

### Crash Details
```
signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x000000000000020f
Cause: null pointer dereference
```

**Stack Trace (identical to before):**
```
#01 pc 00000000000c4cc0  libminimalvulkan.so (JS_SetPropertyFunctionList+1172)
#02 pc 00000000000c5dec  libminimalvulkan.so
#03 pc 00000000000acd18  libminimalvulkan.so (JS_AddIntrinsicBaseObjects+956)
#04 pc 00000000000ac938  libminimalvulkan.so (JS_NewContext+28)
```

---

## Root Cause Analysis

### What You Changed

You modified `JS_InitAtoms()` in `quickjs.c` to pre-allocate `atom_array`:

```c
/* Allocate atom_array upfront to avoid NULL pointer dereference */
init_size = 711;
rt->atom_array = js_mallocz_rt(rt, sizeof(JSAtomStruct *) * init_size);
if (!rt->atom_array)
    return -1;
rt->atom_size = init_size;

/* Initialize atom 0 (JS_ATOM_NULL) */
atom_null = js_mallocz_rt(rt, sizeof(JSAtomStruct));
...
rt->atom_array[0] = atom_null;
rt->atom_count = 1;
rt->atom_free_index = 1;
```

You also added a defensive check in `find_atom()`:
```c
if (unlikely(!ctx->rt->atom_array))
    return JS_ATOM_NULL;
```

### Why It Still Crashes

**The pre-allocation IS happening successfully** (GC initializes with 512MB heap), but the crash persists with the **same fault address**.

**The Real Problem:**

The fault address `0x20f` corresponds to accessing `atom_array[65]` when `atom_array` is at address near-NULL:
- `atom_array[65]` = offset `520` = `0x208`
- `atom_array[66]` = offset `528` = `0x210`
- Fault at `0x20f` is right between these

This means **your pre-allocated `atom_array` pointer is NOT being used** when the crash occurs. The crash is still accessing a NULL/near-NULL pointer.

**Hypothesis: The `JSRuntime` structure itself is corrupted or partially initialized.**

Looking at the code flow:
1. `JS_NewRuntime()` allocates the runtime using `gc_alloc_raw()`
2. It initializes the runtime structure with `memset(rt, 0, sizeof(*rt))`
3. `JS_InitAtoms()` is called and (with your fix) allocates `atom_array`
4. `JS_NewContext()` is called later
5. `JS_AddIntrinsicBaseObjects()` tries to set up built-in objects
6. **Crash** in `JS_SetPropertyFunctionList` when accessing `atom_array`

The fact that the crash still happens at the **same address** suggests:
1. Your `atom_array` allocation succeeds but the pointer isn't being stored in `rt->atom_array`
2. OR something is overwriting `rt->atom_array` back to NULL after your initialization
3. OR there's a different `JSRuntime` instance being used (unlikely)

### Evidence from Testing

```
02-10 23:00:14.538  GCUnified: Unified GC initialized: 512 MB heap, 8192 initial handles
02-10 23:00:14.538  GCUnified: GC reset
```

The GC is initializing successfully. The allocation should work.

But the crash happens at the **same instruction offset** (+1172) and **same fault address** (0x20f) as before.

---

## Potential Causes of Fix Failure

### 1. Code Path Mismatch
Your fix is in `JS_InitAtoms()`, but the crash might be happening in a **different code path** that bypasses your changes. Check if:
- There's another initialization function that also sets up atoms
- The `JSRuntime` is being re-initialized or reset somewhere

### 2. Memory Corruption
Something might be corrupting `rt->atom_array` after your initialization:
- Buffer overflow in another part of the code
- Use-after-free of the runtime structure
- Thread safety issue (multiple threads accessing the runtime)

### 3. Allocation Success but Pointer Not Stored
The `js_mallocz_rt` might succeed but the assignment to `rt->atom_array` might not be taking effect:
- Could be a compiler optimization issue
- Could be that `rt` pointer itself is invalid

### 4. Defensive Check Not Reached
The crash happens in `JS_SetPropertyFunctionList`, not `find_atom`. Your defensive check in `find_atom` won't help if the crash is in a different function.

---

## What Actually Works ✅

1. App builds successfully
2. Vulkan initialization works
3. TLS/HTTPS connections to YouTube succeed
4. HTML fetching retrieves ~3MB of data
5. Script extraction finds 49 scripts
6. All external scripts download successfully

---

## Recommendations

### Debug the Fix

1. **Add logging** right after your allocation:
```c
rt->atom_array = js_mallocz_rt(rt, sizeof(JSAtomStruct *) * init_size);
LOG_INFO("atom_array allocated at %p, size %u", rt->atom_array, init_size);
if (!rt->atom_array)
    return -1;
```

2. **Add logging** in `JS_SetPropertyFunctionList` before the crash:
```c
LOG_INFO("JS_SetPropertyFunctionList: atom_array = %p", ctx->rt->atom_array);
```

3. **Verify the runtime pointer** is the same in both places:
```c
LOG_INFO("JSRuntime pointer: %p", rt);
```

### Alternative Fix Approaches

If pre-allocation isn't working, consider:

1. **Use standard malloc** for QuickJS runtime structures instead of the unified GC
2. **Initialize atoms lazily** when first accessed
3. **Add NULL check at every atom_array access** (tedious but safe)

---

## Conclusion

Your fix to pre-allocate `atom_array` in `JS_InitAtoms()` is the **right approach**, but it's not being effective. The crash persists because:

1. Either the allocated pointer isn't being stored properly
2. Or it's being corrupted/overwritten before use
3. Or there's a different code path bypassing your fix

**Next step:** Add verbose logging to trace the exact value of `rt->atom_array` from initialization through to the crash point.

---

*Report generated from re-testing on Android Emulator (arm64) running Android 14.*
