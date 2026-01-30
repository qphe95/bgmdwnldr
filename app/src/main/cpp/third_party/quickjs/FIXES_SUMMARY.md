# Summary of Fixes

## Issue: `js_handle_deref` returns wrong pointer type

**Problem**: Initially `js_handle_deref` returned `JSGCObjectHeader*` but users expect to get their allocated data, not the internal header.

**Solution**: Changed `js_handle_deref` to return `void*` pointing to user data (past header), and added `js_handle_header()` helper to get header when needed.

### Changes Made:

#### 1. Header File (`quickjs_gc_rewrite.h`)
- Changed `JSHandleEntry.ptr` from `JSGCObjectHeader*` to `void*`
- Changed `js_handle_deref()` return type from `JSGCObjectHeader*` to `void*`
- Added `js_handle_header(void* data)` helper function

#### 2. Implementation (`quickjs_gc_rewrite.c`)
- `js_handle_alloc`: Store `obj + sizeof(JSGCObjectHeader)` in handle table
- `js_handle_retain/release`: Use `js_handle_header()` to access refcount
- `js_handle_gc_compact`: Update handle table with `write + sizeof(JSGCObjectHeader)`
- `js_handle_gc_validate`: Use `js_handle_header()` to validate
- `alloc_handle_id`: Fixed type cast for free list
- `mark_object`: Use `js_handle_header()` to access mark bit

#### 3. Test File (`test_handle_gc.c`)
- `basic_allocation`: Use `js_handle_header()` to access header fields
- `refcounting`: Use `js_handle_header()` to check refcount
- `gc_mark_basic`: Use `js_handle_header()` to check mark bit
- `gc_compact_basic`: Use `js_handle_header()` to check handle field
- `gc_handle_table_update`: Changed variable types to `void*`
- `stress_many_allocations`: Use `js_handle_header()` to check handle field
- `stress_gc_repeated`: Use `js_handle_header()` to check handle field

### API Usage Pattern:

```c
// Allocate object - returns handle
JSObjHandle h = js_handle_alloc(&gc, 64, type);

// Get user data pointer
void *data = js_handle_deref(&gc, h);
MyStruct *obj = (MyStruct*)data;

// Access header fields when needed
JSGCObjectHeader *header = js_handle_header(data);
header->ref_count++;  // or check header->mark, etc.
```

### Why This Design?

1. **User expectations**: When allocating N bytes, user expects a pointer to those N bytes, not to internal metadata
2. **Type safety**: `void*` forces explicit cast to user's type, preventing accidental header access
3. **Performance**: Header access is still fast via inline `js_handle_header()` function
4. **Minimal API**: Clean separation between user data and internal metadata
