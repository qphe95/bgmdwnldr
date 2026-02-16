# Stale Handle Fix for GC Reset Crash

## Problem
The app crashed with `SIGSEGV at 0xc0000008` when processing YouTube URLs (second execution batch). The crash occurred in `JS_DefineProperty` during `JS_NewContext` when creating the function prototype.

## Root Cause
The `gc_reset_full()` function clears the global handle table (`memset(&g_gc, 0, ...)`) when resetting between script executions. However, the old heap memory is freed but its contents (including GC headers with handle values) may persist temporarily.

When the second execution starts:
1. New objects are allocated at the same memory addresses as the previous run
2. `gc_alloc_handle_for_ptr()` checks the GCHeader for an existing handle
3. The header still contains the stale handle from the first run
4. The function returns this stale handle without verifying it's valid
5. When `JS_VALUE_GET_PTR` dereferences the stale handle, it gets NULL
6. Accessing `p->shape` on the NULL pointer causes the crash at `0xc0000008`

## Fix
Modified `gc_alloc_handle_for_ptr()` in `quickjs_gc_unified.c` to validate that a handle from the GCHeader is actually valid before reusing it:

```c
if (hdr && hdr->handle != GC_HANDLE_NULL) {
    /* CRITICAL FIX: After gc_reset_full(), the handle table is cleared
     * but object headers in the (now freed) heap may still contain stale
     * handles. We must verify the handle is valid and points to this ptr.
     */
    GCHandle existing = hdr->handle;
    if (existing < g_gc.handle_count && g_gc.handles[existing].ptr == ptr) {
        // Handle is valid, reuse it
        return existing;
    }
    // Handle is stale, clear it and allocate new
    hdr->handle = GC_HANDLE_NULL;
}
```

## Testing
To verify the fix:
1. Start the app
2. Enter a YouTube URL (e.g., https://www.youtube.com/watch?v=dQw4w9WgXcQ)
3. Press download - first execution should work
4. Try another download (or same URL again) - second execution should now work without crash

## Expected Log Messages
When the stale handle fix triggers, you should see logs like:
```
gc_alloc_handle_for_ptr: ptr=<addr> stale handle=<N> in header, will allocate new
gc_alloc_handle_for_ptr: ptr=<addr> assigned NEW handle=<M>
```

## Files Modified
- `app/src/main/cpp/third_party/quickjs/quickjs_gc_unified.c`
  - Function: `gc_alloc_handle_for_ptr()`
