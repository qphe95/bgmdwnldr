# QuickJS Handle-Based GC Integration Guide

## Overview

This replaces QuickJS's linked-list GC with a handle-based stack allocator + compaction system.

## Key Changes

### 1. JSRuntime Structure (quickjs.c line 238)

**Before:**
```c
struct JSRuntime {
    ...
    struct list_head gc_obj_list;        /* Linked list of GC objects */
    struct list_head gc_zero_ref_count_list;
    struct list_head tmp_obj_list;
    JSGCPhaseEnum gc_phase : 8;
    ...
};
```

**After:**
```c
struct JSRuntime {
    ...
    JSHandleGC handle_gc;                /* New handle-based GC */
    /* Remove: gc_obj_list, gc_zero_ref_count_list, tmp_obj_list, gc_phase */
    ...
};
```

### 2. JSGCObjectHeader (quickjs.c line 355)

**Before:**
```c
struct JSGCObjectHeader {
    int ref_count;
    JSGCObjectTypeEnum gc_obj_type : 4;
    uint8_t mark : 1;
    uint8_t dummy0: 3;
    uint8_t dummy1;
    uint16_t dummy2;
    struct list_head link;               /* Linked list pointer */
};
```

**After:**
```c
struct JSGCObjectHeader {
    int ref_count;
    JSObjectHandle handle;               /* My handle ID */
    JSGCObjectTypeEnum gc_obj_type : 4;
    uint8_t mark : 1;
    uint8_t flags: 3;
    uint16_t size;                       /* Object size for compaction */
    /* Remove: list_head link, dummy fields */
};
```

### 3. Object Allocation (add_gc_object)

**Before:**
```c
static void add_gc_object(JSRuntime *rt, JSGCObjectHeader *h, JSGCObjectTypeEnum type) {
    h->mark = 0;
    h->gc_obj_type = type;
    list_add_tail(&h->link, &rt->gc_obj_list);
}
```

**After:**
```c
static JSObjectHandle add_gc_object(JSRuntime *rt, size_t size, JSGCObjectTypeEnum type) {
    return js_handle_alloc(&rt->handle_gc, size, type);
}
```

### 4. Object References

**Before (pointers):**
```c
struct JSObject {
    JSGCObjectHeader header;
    JSShape *shape;                      /* Pointer - needs fixup */
    JSObject *prototype;                 /* Pointer - needs fixup */
    JSValue *prop_values;                /* Pointer array */
    ...
};
```

**After (handles):**
```c
struct JSObject {
    JSGCObjectHeader header;
    JSObjectHandle shape_handle;         /* Handle - no fixup needed */
    JSObjectHandle prototype_handle;     /* Handle - no fixup needed */
    JSObjectHandle *prop_value_handles;  /* Handle array */
    ...
};
```

### 5. Dereferencing

**Before:**
```c
JSObject *p = (JSObject *)gp;          /* Direct pointer */
JSShape *sh = p->shape;                 /* Direct access */
```

**After:**
```c
JSObjectHandle h = /* handle */;
JSObject *p = (JSObject *)js_handle_ptr(&rt->handle_gc, h);
JSShape *sh = (JSShape *)js_handle_ptr(&rt->handle_gc, p->shape_handle);
```

### 6. GC Collection

**Before (mark-sweep with cycles):**
```c
static void gc_decref(JSRuntime *rt);      /* Decrement refcounts */
static void gc_scan(JSRuntime *rt);        /* Restore refcounts */
static void gc_free_cycles(JSRuntime *rt); /* Free unreachable */
```

**After (mark-compact):**
```c
void js_handle_gc_collect(JSHandleGC *gc) {
    js_handle_mark(gc);     /* Mark reachable */
    js_handle_compact(gc);  /* Compact, update handle table only */
}
```

### 7. JS_FreeRuntime (quickjs.c line 2036)

**Before:**
```c
JS_FreeRuntime(JSRuntime *rt) {
    ...
    assert(list_empty(&rt->gc_obj_list));  /* Must free all objects first */
    ...
}
```

**After:**
```c
JS_FreeRuntime(JSRuntime *rt) {
    ...
    /* No assertion needed - just free the whole stack */
    js_handle_gc_free(&rt->handle_gc);
    ...
}
```

## API Changes

### For C Extensions

**Before:**
```c
JSValue obj = JS_NewObject(ctx);
JSObject *p = JS_VALUE_GET_OBJ(obj);  /* Get pointer */
p->prop = value;                       /* Access directly */
```

**After:**
```c
JSValue obj = JS_NewObject(ctx);
JSObjectHandle h = JS_VALUE_GET_HANDLE(obj);  /* Get handle */
JSObject *p = js_handle_ptr(&rt->handle_gc, h); /* Deref when needed */
/* Or use accessor macros */
```

## Performance Characteristics

| Operation | Before | After |
|-----------|--------|-------|
| **Allocation** | malloc() | Bump pointer (O(1)) |
| **Dereference** | Direct | Table lookup |
| **GC Mark** | O(objects + refs) | O(live objects) |
| **GC Sweep** | O(objects) | O(objects) |
| **Compaction** | N/A | O(objects), cache-friendly |
| **Fragmentation** | Yes | No |

## Migration Strategy

1. **Phase 1**: Add handle system alongside existing GC
2. **Phase 2**: Migrate object headers to use handles
3. **Phase 3**: Replace pointer refs with handle refs
4. **Phase 4**: Remove old list-based GC
5. **Phase 5**: Optimize hot paths (inline handle dereference)

## Files to Modify

- `quickjs.c`: Core runtime, allocation, GC
- `quickjs.h`: Public API (JSValue representation unchanged)
- Object types: `JSObject`, `JSShape`, `JSFunctionBytecode`, etc.

## Testing

The handle-based GC should pass all existing QuickJS tests plus:
1. Stress tests with many allocations
2. Cycle creation/destruction tests
3. Long-running allocation patterns
4. Memory leak detection
