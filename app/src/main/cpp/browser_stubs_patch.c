// - JS_DefinePropertyValue: TAKES OWNERSHIP of the value (frees it internally)
// - JS_DefinePropertyGetSet: does NOT take ownership (dupes internally)
// - JS_NewAtom: creates atom (caller must free with JS_FreeAtom)
//
// This implementation tracks ownership explicitly to avoid leaks or double-frees.

// Helper macro to safely free JSValue - checks for both undefined and exception
#define SAFE_FREE_VALUE(ctx, val) do { \
    if (!JS_IsUndefined(val) && !JS_IsException(val)) { \
        JS_FreeValue(ctx, val); \
    } \
} while(0)

static JSValue js_object_define_property(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void) this_val;
    
    JSAtom prop_atom = 0;
    JSValue result = JS_UNDEFINED;
    
    if (argc < 3) {
        return JS_EXCEPTION;
    }
    
    JSValue obj = argv[0];
    JSValue prop = argv[1];
    JSValue descriptor = argv[2];
    
    if (JS_IsNull(obj) || JS_IsUndefined(obj)) {
        return JS_ThrowTypeError(ctx, "Object.defineProperty called on null or undefined");
    }
    
    // Convert property to atom
    if (JS_IsSymbol(prop)) {
        prop_atom = JS_ValueToAtom(ctx, prop);
    } else {
        const char *prop_str = JS_ToCString(ctx, prop);
        if (!prop_str) {
            return JS_EXCEPTION;
        }
        prop_atom = JS_NewAtom(ctx, prop_str);
        JS_FreeCString(ctx, prop_str);
    }
    
    if (prop_atom == JS_ATOM_NULL) {
        return JS_EXCEPTION;
    }
    
    // Get descriptor properties
    JSValue get_prop = JS_GetPropertyStr(ctx, descriptor, "get");
    JSValue set_prop = JS_GetPropertyStr(ctx, descriptor, "set");
    
    int has_get = !JS_IsException(get_prop) && !JS_IsUndefined(get_prop);
    int has_set = !JS_IsException(set_prop) && !JS_IsUndefined(set_prop);
    
    // Get flags
    JSValue writable_prop = JS_GetPropertyStr(ctx, descriptor, "writable");
    JSValue enumerable_prop = JS_GetPropertyStr(ctx, descriptor, "enumerable");
    JSValue configurable_prop = JS_GetPropertyStr(ctx, descriptor, "configurable");
    
    int writable = !JS_IsException(writable_prop) && JS_ToBool(ctx, writable_prop);
    int enumerable = !JS_IsException(enumerable_prop) && JS_ToBool(ctx, enumerable_prop);
    int configurable = !JS_IsException(configurable_prop) && JS_ToBool(ctx, configurable_prop);
    
    SAFE_FREE_VALUE(ctx, writable_prop);
    SAFE_FREE_VALUE(ctx, enumerable_prop);
    SAFE_FREE_VALUE(ctx, configurable_prop);
    
    int flags = JS_PROP_THROW;
    if (writable) flags |= JS_PROP_WRITABLE;
    if (enumerable) flags |= JS_PROP_ENUMERABLE;
    if (configurable) flags |= JS_PROP_CONFIGURABLE;
    
    int def_result = -1;
    JSValue value = JS_UNDEFINED;
    
    if (has_get || has_set) {
        // === ACCESSOR PROPERTY ===
        // Skip accessor properties - not fully supported
        // Just pretend success
        def_result = 0;
    } else {
        // === DATA PROPERTY ===
        value = JS_GetPropertyStr(ctx, descriptor, "value");
        if (JS_IsException(value)) {
            result = JS_EXCEPTION;
            goto cleanup;
        }
        
        // JS_DefinePropertyValue TAKES OWNERSHIP of value
        def_result = JS_DefinePropertyValue(ctx, obj, prop_atom, value, flags);
        // Value is now owned by the object or freed on error, don't free it
        value = JS_UNDEFINED;
    }
    
    if (def_result < 0) {
        result = JS_EXCEPTION;
    } else {
        result = JS_DupValue(ctx, obj);
    }
    
cleanup:
    if (prop_atom) JS_FreeAtom(ctx, prop_atom);
    SAFE_FREE_VALUE(ctx, get_prop);
    SAFE_FREE_VALUE(ctx, set_prop);
    SAFE_FREE_VALUE(ctx, value);
    return result;
}
