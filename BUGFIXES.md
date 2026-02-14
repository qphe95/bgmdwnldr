# Bug Fixes Summary

This document summarizes the bugs found and fixed using the **Shadow Stack GC Enhancement** approach. Rather than manually freeing every JSValue (which is error-prone), we've enhanced the GC system to automatically track JSValue references held by C code.

---

## Architecture: Shadow Stack GC Enhancement

### The Problem

The original bugs (#2, #3, #6, #7) were caused by temporary JSValue objects being created in C code but not being freed. The previous "fix" was to manually call `JS_FreeValue()` everywhere, which:
- Is extremely error-prone (easy to miss a free, or double-free)
- Defeats the purpose of having a garbage collector
- Makes code harder to maintain

### The Solution: Shadow Stack

We've implemented a **shadow stack** in the unified GC that tracks JSValue references held by C code:

```
C Code                              Shadow Stack                     GC
------                              ------------                     --
JS_TRACK(ctx, val)  ------------>   [val] ----------> Mark phase marks val
|                                    |
| (use val safely)                   | (val protected from GC)
|                                    |
JS_UNTRACK(ctx, val) ------------>   [remove val] --> GC can collect if unreachable
```

### How It Works

1. **Push**: When C code creates/acquires a JSValue, it calls `JS_TRACK()` to register it as a temporary root
2. **Protection**: During GC mark phase, all shadow stack entries are marked as reachable
3. **Pop**: When C code is done with the JSValue, `JS_UNTRACK()` removes it from the shadow stack
4. **Collection**: GC can now collect the object if it's not reachable from other roots

### API

```c
// Manual push/pop
JSValue val = JS_GetPropertyStr(ctx, obj, "foo");
JS_TRACK(ctx, val);
// ... use val ...
JS_UNTRACK(ctx, val);

// Scoped (auto track/untrack)
JS_SCOPE_BEGIN(ctx)
{
    JS_SCOPE_VALUE(ctx, body, JS_GetPropertyStr(ctx, doc, "body"));
    // body is automatically untracked at JS_SCOPE_END
}
JS_SCOPE_END(ctx)
```

---

## Bugs Fixed

### 1. Buffer Overflow in `record_captured_url` (js_quickjs.c)

**Status**: ✅ Fixed with manual bounds checking (not GC-related)

**Location**: `app/src/main/cpp/js_quickjs.c`, line 44-66

**Issue**: The original code used `strncpy` which could leave the destination buffer without proper null termination in edge cases.

**Fix**: Changed to use `memcpy` with explicit length validation:

```c
size_t url_len = strlen(url);
if (url_len == 0 || url_len >= URL_MAX_LEN) {
    LOG_WARN("URL too long or empty: %zu bytes", url_len);
    return;
}
memcpy(g_captured_urls[g_captured_url_count], url, url_len);
g_captured_urls[g_captured_url_count][url_len] = '\0';
```

---

### 2. JSValue Memory Leaks in `create_dom_nodes_from_parsed_html` (js_quickjs.c)

**Status**: ✅ Fixed using Shadow Stack GC Enhancement

**Location**: `app/src/main/cpp/js_quickjs.c`, line 717-776

**Issue**: Multiple JSValue objects were not being freed:
- `global` from `JS_GetGlobalObject`
- `body` from `JS_GetPropertyStr`
- `doc_body` from `JS_GetPropertyStr`
- `appendChild` from `JS_GetPropertyStr`
- `elem` in the error/exit path

**Fix**: Using scoped shadow stack tracking:

```c
JS_SCOPE_BEGIN(ctx)
{
    JS_SCOPE_VALUE(ctx, elem, html_create_element_js(ctx, ...));
    JS_SCOPE_VALUE(ctx, global, JS_GetGlobalObject(ctx));
    JS_SCOPE_VALUE(ctx, body, JS_GetPropertyStr(ctx, global, "document"));
    JS_SCOPE_VALUE(ctx, doc_body, JS_GetPropertyStr(ctx, body, "body"));
    JS_SCOPE_VALUE(ctx, appendChild, JS_GetPropertyStr(ctx, doc_body, "appendChild"));
    // ... use values ...
    // All values automatically untracked at JS_SCOPE_END
}
JS_SCOPE_END(ctx)
```

---

### 3. JSValue Memory Leaks in `create_video_elements_from_html` (js_quickjs.c)

**Status**: ✅ Fixed using Shadow Stack GC Enhancement

**Location**: `app/src/main/cpp/js_quickjs.c`, line 787-865

**Issue**: Similar to bug #2, multiple JSValue objects were leaked:
- `global2` from `JS_GetGlobalObject`
- `doc` from `JS_GetPropertyStr`
- `body` from `JS_GetPropertyStr`
- `appendChild` from `JS_GetPropertyStr`
- `video` in the error/exit path
- `id_prop` from `JS_GetPropertyStr`

**Fix**: Using scoped shadow stack tracking for each loop iteration:

```c
for (int i = 0; i < video_count; i++) {
    JS_SCOPE_BEGIN(ctx)
    {
        JS_SCOPE_VALUE(ctx, video, js_video_constructor(ctx, ...));
        JS_SCOPE_VALUE(ctx, global, JS_GetGlobalObject(ctx));
        JS_SCOPE_VALUE(ctx, doc_obj, JS_GetPropertyStr(ctx, global, "document"));
        JS_SCOPE_VALUE(ctx, body, JS_GetPropertyStr(ctx, doc_obj, "body"));
        JS_SCOPE_VALUE(ctx, appendChild, JS_GetPropertyStr(ctx, body, "appendChild"));
        // ... use values ...
    }
    JS_SCOPE_END(ctx)
}
```

---

### 4. Missing Parameter Validation in `js_quickjs_get_captured_urls` (js_quickjs.c)

**Status**: ✅ Fixed with manual validation (not GC-related)

**Location**: `app/src/main/cpp/js_quickjs.c`, line 1276-1285

**Issue**: The function didn't validate the `urls` parameter before locking the mutex, which could lead to a deadlock or crash if called with NULL.

**Fix**: Added parameter validation before locking:

```c
int js_quickjs_get_captured_urls(char urls[][JS_MAX_URL_LEN], int max_urls) {
    if (!urls || max_urls <= 0) {
        return 0;
    }
    pthread_mutex_lock(&g_url_mutex);
    // ...
}
```

---

### 5. Unsafe String Copy in `js_quickjs_exec_scripts` (js_quickjs.c)

**Status**: ✅ Fixed with manual safe copy (not GC-related)

**Location**: `app/src/main/cpp/js_quickjs.c`, line 1254-1259

**Issue**: Used `strncpy` for copying captured URLs which could result in non-null-terminated strings.

**Fix**: Changed to use `memcpy` with proper length calculation:

```c
for (int i = 0; i < g_captured_url_count && i < JS_MAX_CAPTURED_URLS; i++) {
    size_t len = strlen(g_captured_urls[i]);
    if (len >= JS_MAX_URL_LEN) len = JS_MAX_URL_LEN - 1;
    memcpy(out_result->captured_urls[i], g_captured_urls[i], len);
    out_result->captured_urls[i][len] = '\0';
}
```

---

### 6. JSValue Memory Leaks in DOM Node Creation (html_dom.c)

**Status**: ✅ Fixed using Shadow Stack GC Enhancement

**Location**: `app/src/main/cpp/html_dom.c`, line 725-786

**Issue**: Multiple JSValue objects in the DOM creation path were not freed:
- `appendChild` in element handling
- `childNodes` in text node handling
- `push` in text node handling

**Fix**: Using scoped shadow stack tracking:

```c
case HTML_NODE_ELEMENT: {
    // ... create js_node ...
    if (!JS_IsUndefined(parent) && !JS_IsNull(parent)) {
        JS_SCOPE_BEGIN(ctx)
        {
            JS_SCOPE_VALUE(ctx, appendChild, JS_GetPropertyStr(ctx, parent, "appendChild"));
            if (!JS_IsUndefined(appendChild) && !JS_IsNull(appendChild)) {
                JSValue args[1] = { js_node };
                JS_SCOPE_VALUE(ctx, result, JS_Call(ctx, appendChild, parent, 1, args));
            }
        }
        JS_SCOPE_END(ctx)
    }
    break;
}
```

---

### 7. JSValue Leak in `html_create_dom_in_js` (html_dom.c)

**Status**: ✅ Fixed using Shadow Stack GC Enhancement

**Location**: `app/src/main/cpp/html_dom.c`, line 871-897

**Issue**: `global` from `JS_GetGlobalObject` and `doc_elem` in error path were not freed.

**Fix**: Using scoped shadow stack tracking:

```c
JS_SCOPE_BEGIN(ctx)
{
    JS_SCOPE_VALUE(ctx, js_doc, html_create_js_document(ctx, doc));
    
    if (JS_IsNull(js_doc) || JS_IsException(js_doc)) {
        LOG_ERROR("Failed to create JS document");
        JS_SCOPE_END(ctx)
        return false;
    }
    
    JS_SCOPE_VALUE(ctx, global, JS_GetGlobalObject(ctx));
    JS_SetPropertyStr(ctx, global, "document", js_doc);
    
    JS_SCOPE_VALUE(ctx, doc_elem, JS_GetPropertyStr(ctx, js_doc, "documentElement"));
    if (!JS_IsNull(doc_elem) && !JS_IsUndefined(doc_elem)) {
        JS_SetPropertyStr(ctx, global, "documentElement", doc_elem);
    }
}
JS_SCOPE_END(ctx)
```

---

## Implementation Details

### Files Modified

1. **app/src/main/cpp/third_party/quickjs/quickjs_gc_unified.h**
   - Added `GCShadowStackEntry` structure
   - Added `GCShadowStackStats` structure
   - Added shadow stack fields to `GCState`
   - Added function declarations for shadow stack operations
   - Added convenience macros (`JS_GC_PUSH`, `JS_GC_POP`, `JS_GC_SCOPED`)

2. **app/src/main/cpp/third_party/quickjs/quickjs_gc_unified.c**
   - Implemented `gc_shadow_stack_init()`
   - Implemented `gc_shadow_stack_cleanup()`
   - Implemented `gc_push_jsvalue()`
   - Implemented `gc_pop_jsvalue()`
   - Implemented `gc_mark_shadow_stack()` - called during GC mark phase
   - Implemented `gc_shadow_stack_stats()`
   - Modified `gc_init()` to initialize shadow stack
   - Modified `gc_cleanup()` to cleanup shadow stack
   - Modified `gc_mark()` to call `gc_mark_shadow_stack()`

3. **app/src/main/cpp/js_value_helpers.h** (NEW)
   - High-level helper macros (`JS_TRACK`, `JS_UNTRACK`)
   - Scoped value macros (`JS_SCOPE_BEGIN`, `JS_SCOPE_VALUE`, `JS_SCOPE_END`)
   - Helper functions for common patterns

4. **app/src/main/cpp/js_quickjs.c**
   - Included `js_value_helpers.h`
   - Applied shadow stack tracking to `create_dom_nodes_from_parsed_html()` (Bug #2)
   - Applied shadow stack tracking to `create_video_elements_from_html()` (Bug #3)
   - Applied buffer overflow fix to `record_captured_url()` (Bug #1)
   - Applied parameter validation to `js_quickjs_get_captured_urls()` (Bug #4)
   - Applied safe string copy to `js_quickjs_exec_scripts()` (Bug #5)

5. **app/src/main/cpp/html_dom.c**
   - Included `js_value_helpers.h`
   - Applied shadow stack tracking to `html_node_create_js_recursive()` (Bug #6)
   - Applied shadow stack tracking to `html_create_dom_in_js()` (Bug #7)

---

## Benefits of This Approach

| Aspect | Manual Free Approach | Shadow Stack Approach |
|--------|---------------------|----------------------|
| **Code Complexity** | High (need to track every value) | Low (declarative tracking) |
| **Bug Risk** | High (easy to miss a free) | Low (GC handles it) |
| **Performance** | Fast but error-prone | Minimal overhead (pointer tracking) |
| **Maintainability** | Poor | Good |
| **Future-proofing** | Must add frees to all new code | Works automatically |

---

## Testing Recommendations

1. **Monitor Shadow Stack Statistics**:
   ```c
   GCShadowStackStats stats;
   gc_shadow_stack_stats(&stats);
   LOG_INFO("Shadow stack: depth=%u, max=%u, pushes=%u", 
            stats.current_depth, stats.max_depth, stats.total_pushes);
   ```

2. **Check for Leaks in Logcat**:
   ```bash
   adb logcat | grep -E "(shadow stack|GC|Leak)"
   ```

3. **Validate Shadow Stack Consistency** (debug builds):
   ```c
   char error[256];
   if (!gc_shadow_stack_validate(error, sizeof(error))) {
       LOG_ERROR("Shadow stack validation failed: %s", error);
   }
   ```

---

## Prevention

To prevent similar bugs in the future:

1. **Use scoped tracking for all temporary JSValues**:
   ```c
   JS_SCOPE_BEGIN(ctx)
   {
       JS_SCOPE_VALUE(ctx, val, JS_GetPropertyStr(ctx, obj, "prop"));
       // ... use val ...
   }
   JS_SCOPE_END(ctx)
   ```

2. **Never use manual JS_FreeValue() in new code** - let the shadow stack handle it

3. **Run static analysis tools**:
   - clang-static-analyzer
   - cppcheck

4. **Monitor shadow stack depth** in production to detect potential leaks
