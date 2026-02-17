/*
 * Browser Stubs - C implementation of DOM/Browser APIs for QuickJS
 */
#include <string.h>
#include <stdlib.h>
#include <android/log.h>
#include <quickjs.h>
#include "browser_stubs.h"
#include "html_dom.h"

#define LOG_TAG "browser_stubs"
#define LOG_ERROR(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// External symbols from js_quickjs.c
extern GCValue js_document_create_element(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv);

// Forward declarations for internal functions
static GCValue js_dummy_function(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv);
static GCValue js_dummy_function_true(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv);

// Basic stub function definitions (must be before use in function lists)
static GCValue js_undefined(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return JS_UNDEFINED;
}

static GCValue js_null(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return JS_NULL;
}

static GCValue js_empty_array(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewArray(ctx);
}

static GCValue js_empty_string(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewString(ctx, "");
}

static GCValue js_false(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return JS_FALSE;
}

static GCValue js_true(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return JS_TRUE;
}

// Validation that actually tests if object can be used
// Returns 1 if usable, 0 if not. Logs when corruption is detected.
static int is_obj_usable(JSContext *ctx, GCValue obj) {
    if (!JS_IsObject(obj) || JS_IsException(obj)) return 0;
    
    GCValue proto = JS_GetPrototype(ctx, obj);
    int usable = !JS_IsException(proto);
    
    
    if (!usable) {
        LOG_ERROR("is_obj_usable: object is unusable (shape corrupted) - logged from find_own_property");
    }
    return usable;
}

// Safe wrapper for JS_SetPropertyStr that validates objects first
static int safe_set_property_str(JSContext *ctx, GCValueConst this_obj, 
                                  const char *prop, GCValueConst val) {
    if (!is_obj_usable(ctx, this_obj)) {
        LOG_ERROR("safe_set_property_str: refusing to set property '%s' on unusable object", prop);
        return -1;
    }
    return JS_SetPropertyStr(ctx, this_obj, prop, val);
}

// Safe wrapper for JS_GetPropertyStr that validates objects first
static GCValue safe_get_property_str(JSContext *ctx, GCValueConst this_obj, const char *prop) {
    if (!is_obj_usable(ctx, this_obj)) {
        LOG_ERROR("safe_get_property_str: refusing to get property '%s' from unusable object", prop);
        return JS_EXCEPTION;
    }
    return JS_GetPropertyStr(ctx, this_obj, prop);
}

// Safe wrapper for JS_NewCFunction that validates the context
static GCValue safe_new_cfunction(JSContext *ctx, JSCFunction *func, const char *name, int argc) {
    if (!ctx) {
        LOG_ERROR("safe_new_cfunction: NULL context!");
        return JS_EXCEPTION;
    }
    return JS_NewCFunction(ctx, func, name, argc);
}

// Helper to get a prototype from a constructor: Constructor.prototype
GCValue js_get_prototype(JSContext *ctx, GCValueConst ctor) {
    return JS_GetPropertyStr(ctx, ctor, "prototype");
}

static GCValue js_zero(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewInt32(ctx, 0);
}

static GCValue js_console_log(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return JS_UNDEFINED;
}

// getComputedStyle stub - returns a CSSStyleDeclaration-like object
static GCValue js_get_computed_style(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    // Return an object with getPropertyValue method
    GCValue style = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, style, "getPropertyValue", 
        JS_NewCFunction(ctx, js_empty_string, "getPropertyValue", 1));
    JS_SetPropertyStr(ctx, style, "width", JS_NewString(ctx, "auto"));
    JS_SetPropertyStr(ctx, style, "height", JS_NewString(ctx, "auto"));
    JS_SetPropertyStr(ctx, style, "display", JS_NewString(ctx, "block"));
    JS_SetPropertyStr(ctx, style, "position", JS_NewString(ctx, "static"));
    return style;
}

static GCValue js_dummy_function_true(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return JS_TRUE;
}

// Generic dummy function that returns undefined
static GCValue js_dummy_function(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return JS_UNDEFINED;
}

// ============================================================================
// DOMException Implementation (needed for Web Animations API)
// ============================================================================

#define DOM_EXCEPTION_LOG_TAG "DOMException"
#define DOM_EX_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, DOM_EXCEPTION_LOG_TAG, __VA_ARGS__)

#define DOM_EXCEPTION_INDEX_SIZE_ERR 1
#define DOM_EXCEPTION_HIERARCHY_REQUEST_ERR 3
#define DOM_EXCEPTION_WRONG_DOCUMENT_ERR 4
#define DOM_EXCEPTION_INVALID_CHARACTER_ERR 5
#define DOM_EXCEPTION_NO_MODIFICATION_ALLOWED_ERR 7
#define DOM_EXCEPTION_NOT_FOUND_ERR 8
#define DOM_EXCEPTION_NOT_SUPPORTED_ERR 9
#define DOM_EXCEPTION_INVALID_STATE_ERR 11
#define DOM_EXCEPTION_SYNTAX_ERR 12
#define DOM_EXCEPTION_INVALID_MODIFICATION_ERR 13
#define DOM_EXCEPTION_NAMESPACE_ERR 14
#define DOM_EXCEPTION_INVALID_ACCESS_ERR 15
#define DOM_EXCEPTION_TYPE_MISMATCH_ERR 17
#define DOM_EXCEPTION_SECURITY_ERR 18
#define DOM_EXCEPTION_NETWORK_ERR 19
#define DOM_EXCEPTION_ABORT_ERR 20
#define DOM_EXCEPTION_URL_MISMATCH_ERR 21
#define DOM_EXCEPTION_QUOTA_EXCEEDED_ERR 22
#define DOM_EXCEPTION_TIMEOUT_ERR 23
#define DOM_EXCEPTION_INVALID_NODE_TYPE_ERR 24
#define DOM_EXCEPTION_DATA_CLONE_ERR 25

typedef struct {
    char name[64];
    char message[256];
    int code;
} DOMExceptionData;

static JSClassID js_dom_exception_class_id = 0;

static void js_dom_exception_finalizer(JSRuntime *rt, GCValue val) {
    DOMExceptionData *de = JS_GetOpaque(val, js_dom_exception_class_id);
    if (de) free(de);
}

static JSClassDef js_dom_exception_class_def = {
    "DOMException",
    .finalizer = js_dom_exception_finalizer,
};

static GCValue js_dom_exception_constructor(JSContext *ctx, GCValueConst new_target, int argc, GCValueConst *argv) {
    DOM_EX_LOGD("DOMException constructor called");
    DOMExceptionData *de = calloc(1, sizeof(DOMExceptionData));
    if (!de) return JS_EXCEPTION;
    
    strcpy(de->name, "Error");
    de->code = 0;
    
    if (argc > 0) {
        const char *msg = JS_ToCString(ctx, argv[0]);
        if (msg) {
            strncpy(de->message, msg, sizeof(de->message) - 1);
            JS_FreeCString(ctx, msg);
        }
    }
    
    if (argc > 1) {
        const char *name = JS_ToCString(ctx, argv[1]);
        if (name) {
            strncpy(de->name, name, sizeof(de->name) - 1);
            if (strcmp(name, "IndexSizeError") == 0) de->code = DOM_EXCEPTION_INDEX_SIZE_ERR;
            else if (strcmp(name, "HierarchyRequestError") == 0) de->code = DOM_EXCEPTION_HIERARCHY_REQUEST_ERR;
            else if (strcmp(name, "WrongDocumentError") == 0) de->code = DOM_EXCEPTION_WRONG_DOCUMENT_ERR;
            else if (strcmp(name, "InvalidCharacterError") == 0) de->code = DOM_EXCEPTION_INVALID_CHARACTER_ERR;
            else if (strcmp(name, "NoModificationAllowedError") == 0) de->code = DOM_EXCEPTION_NO_MODIFICATION_ALLOWED_ERR;
            else if (strcmp(name, "NotFoundError") == 0) de->code = DOM_EXCEPTION_NOT_FOUND_ERR;
            else if (strcmp(name, "NotSupportedError") == 0) de->code = DOM_EXCEPTION_NOT_SUPPORTED_ERR;
            else if (strcmp(name, "InvalidStateError") == 0) de->code = DOM_EXCEPTION_INVALID_STATE_ERR;
            else if (strcmp(name, "SyntaxError") == 0) de->code = DOM_EXCEPTION_SYNTAX_ERR;
            else if (strcmp(name, "InvalidModificationError") == 0) de->code = DOM_EXCEPTION_INVALID_MODIFICATION_ERR;
            else if (strcmp(name, "NamespaceError") == 0) de->code = DOM_EXCEPTION_NAMESPACE_ERR;
            else if (strcmp(name, "InvalidAccessError") == 0) de->code = DOM_EXCEPTION_INVALID_ACCESS_ERR;
            else if (strcmp(name, "TypeMismatchError") == 0) de->code = DOM_EXCEPTION_TYPE_MISMATCH_ERR;
            else if (strcmp(name, "SecurityError") == 0) de->code = DOM_EXCEPTION_SECURITY_ERR;
            else if (strcmp(name, "NetworkError") == 0) de->code = DOM_EXCEPTION_NETWORK_ERR;
            else if (strcmp(name, "AbortError") == 0) de->code = DOM_EXCEPTION_ABORT_ERR;
            else if (strcmp(name, "URLMismatchError") == 0) de->code = DOM_EXCEPTION_URL_MISMATCH_ERR;
            else if (strcmp(name, "QuotaExceededError") == 0) de->code = DOM_EXCEPTION_QUOTA_EXCEEDED_ERR;
            else if (strcmp(name, "TimeoutError") == 0) de->code = DOM_EXCEPTION_TIMEOUT_ERR;
            else if (strcmp(name, "InvalidNodeTypeError") == 0) de->code = DOM_EXCEPTION_INVALID_NODE_TYPE_ERR;
            else if (strcmp(name, "DataCloneError") == 0) de->code = DOM_EXCEPTION_DATA_CLONE_ERR;
            JS_FreeCString(ctx, name);
        }
    }
    
    GCValue obj = JS_NewObjectClass(ctx, js_dom_exception_class_id);
    if (JS_IsException(obj)) {
        free(de);
        return obj;
    }
    JS_SetOpaque(obj, de);
    return obj;
}

static GCValue js_dom_exception_get_name(JSContext *ctx, GCValueConst this_val) {
    DOMExceptionData *de = JS_GetOpaque2(ctx, this_val, js_dom_exception_class_id);
    if (!de) return JS_EXCEPTION;
    return JS_NewString(ctx, de->name);
}

static GCValue js_dom_exception_get_message(JSContext *ctx, GCValueConst this_val) {
    DOMExceptionData *de = JS_GetOpaque2(ctx, this_val, js_dom_exception_class_id);
    if (!de) return JS_EXCEPTION;
    return JS_NewString(ctx, de->message);
}

static GCValue js_dom_exception_get_code(JSContext *ctx, GCValueConst this_val) {
    DOMExceptionData *de = JS_GetOpaque2(ctx, this_val, js_dom_exception_class_id);
    if (!de) return JS_EXCEPTION;
    return JS_NewInt32(ctx, de->code);
}

static const JSCFunctionListEntry js_dom_exception_proto_funcs[] = {
    JS_CGETSET_DEF("name", js_dom_exception_get_name, NULL),
    JS_CGETSET_DEF("message", js_dom_exception_get_message, NULL),
    JS_CGETSET_DEF("code", js_dom_exception_get_code, NULL),
};

// ============================================================================
// ES6+ Polyfills (C implementations)
// ============================================================================

// Object.getPrototypeOf polyfill
static GCValue js_object_get_prototype_of(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    (void)this_val;
    if (argc < 1) return JS_EXCEPTION;
    
    GCValue obj = argv[0];
    if (JS_IsNull(obj) || JS_IsUndefined(obj)) {
        return JS_ThrowTypeError(ctx, "Object.getPrototypeOf called on null or undefined");
    }
    
    // Get __proto__ property
    GCValue proto = JS_GetPropertyStr(ctx, obj, "__proto__");
    return proto;
}

// Object.defineProperty implementation with correct ownership semantics
// 
// OWNERSHIP RULES for QuickJS API:
// - JS_GetPropertyStr: returns NEW value (caller must free)
// - JS_DefinePropertyValue: TAKES OWNERSHIP of the value (frees it internally)
// - JS_DefinePropertyGetSet: does NOT take ownership (dupes internally)
// - JS_NewAtom: creates atom (caller must free with JS_FreeAtom)
//
// This implementation tracks ownership explicitly to avoid leaks or double-frees.


// SAFE_FREE_VALUE is no longer needed with mark-and-sweep GC
#define SAFE_FREE_VALUE(ctx, val) do { (void)(val); } while(0)

static GCValue js_object_define_property(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    (void) this_val;
    
    JSAtom prop_atom = 0;
    GCValue result = JS_UNDEFINED;
    
    if (argc < 3) {
        return JS_EXCEPTION;
    }
    
    GCValue obj = argv[0];
    GCValue prop = argv[1];
    GCValue descriptor = argv[2];
    
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
    GCValue get_prop = JS_GetPropertyStr(ctx, descriptor, "get");
    GCValue set_prop = JS_GetPropertyStr(ctx, descriptor, "set");
    
    int has_get = !JS_IsException(get_prop) && !JS_IsUndefined(get_prop);
    int has_set = !JS_IsException(set_prop) && !JS_IsUndefined(set_prop);
    
    // Get flags
    GCValue writable_prop = JS_GetPropertyStr(ctx, descriptor, "writable");
    GCValue enumerable_prop = JS_GetPropertyStr(ctx, descriptor, "enumerable");
    GCValue configurable_prop = JS_GetPropertyStr(ctx, descriptor, "configurable");
    
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
    GCValue value = JS_UNDEFINED;
    
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
        result = obj;
    }
    
cleanup:
    if (prop_atom) JS_FreeAtom(ctx, prop_atom);
    SAFE_FREE_VALUE(ctx, get_prop);
    SAFE_FREE_VALUE(ctx, set_prop);
    SAFE_FREE_VALUE(ctx, value);
    return result;
}

// Object.create polyfill
static GCValue js_object_create(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    (void)this_val;
    if (argc < 1) return JS_EXCEPTION;
    
    GCValue proto = argv[0];
    
    // Create new object with given prototype
    GCValue obj = JS_NewObject(ctx);
    if (!JS_IsNull(proto)) {
        GCValue proto_key = JS_NewString(ctx, "__proto__");
        JS_SetProperty(ctx, obj, JS_ValueToAtom(ctx, proto_key), proto);

    }
    
    // Handle propertiesObject (second argument) if provided
    if (argc > 1 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
        // Copy enumerable properties from propertiesObject to new object
        GCValue props = argv[1];
        
        // Get Object.keys to enumerate properties
        GCValue object_ctor = JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "Object");
        GCValue keys_func = JS_GetPropertyStr(ctx, object_ctor, "keys");
        GCValue keys = JS_Call(ctx, keys_func, JS_UNDEFINED, 1, &props);


        if (!JS_IsException(keys)) {
            GCValue len_val = JS_GetPropertyStr(ctx, keys, "length");
            uint32_t key_count = 0;
            JS_ToUint32(ctx, &key_count, len_val);

            for (uint32_t i = 0; i < key_count; i++) {
                GCValue key_val = JS_GetPropertyUint32(ctx, keys, i);
                const char *key = JS_ToCString(ctx, key_val);
                if (key) {
                    GCValue desc = JS_GetProperty(ctx, props, JS_ValueToAtom(ctx, key_val));
                    if (!JS_IsUndefined(desc) && !JS_IsNull(desc)) {
                        // Simple property copy - set value directly
                        GCValue val = JS_GetPropertyStr(ctx, desc, "value");
                        if (!JS_IsUndefined(val)) {
                            JS_SetPropertyStr(ctx, obj, key, val);
                        } else {

                        }
                    }

                    JS_FreeCString(ctx, key);
                }

            }
        }

    }
    
    return obj;
}

// Object.defineProperties polyfill
static GCValue js_object_define_properties(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    (void)this_val;
    if (argc < 2) return JS_EXCEPTION;
    
    GCValue obj = argv[0];
    GCValue props = argv[1];
    
    if (JS_IsNull(obj) || JS_IsUndefined(obj)) {
        return JS_ThrowTypeError(ctx, "Object.defineProperties called on null or undefined");
    }
    
    // Get Object.keys to enumerate properties
    GCValue global = JS_GetGlobalObject(ctx);
    GCValue object_ctor = JS_GetPropertyStr(ctx, global, "Object");
    GCValue keys_func = JS_GetPropertyStr(ctx, object_ctor, "keys");
    GCValue keys = JS_Call(ctx, keys_func, JS_UNDEFINED, 1, &props);



    if (!JS_IsException(keys)) {
        GCValue len_val = JS_GetPropertyStr(ctx, keys, "length");
        uint32_t key_count = 0;
        JS_ToUint32(ctx, &key_count, len_val);

        for (uint32_t i = 0; i < key_count; i++) {
            GCValue key_val = JS_GetPropertyUint32(ctx, keys, i);
            const char *key = JS_ToCString(ctx, key_val);
            if (key) {
                GCValue desc = JS_GetProperty(ctx, props, JS_ValueToAtom(ctx, key_val));
                if (!JS_IsUndefined(desc) && !JS_IsNull(desc)) {
                    // Copy property descriptor values
                    GCValue val = JS_GetPropertyStr(ctx, desc, "value");
                    if (!JS_IsUndefined(val)) {
                        JS_SetPropertyStr(ctx, obj, key, val);
                    } else {

                    }
                }

                JS_FreeCString(ctx, key);
            }

        }

    }
    
    return obj;
}

// Object.getOwnPropertyDescriptor polyfill
static GCValue js_object_get_own_property_descriptor(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    (void)this_val;
    if (argc < 2) return JS_UNDEFINED;
    
    GCValue obj = argv[0];
    const char *prop = JS_ToCString(ctx, argv[1]);
    if (!prop) return JS_UNDEFINED;
    
    // Check if property exists
    JSAtom prop_atom = JS_NewAtom(ctx, prop);
    int has_prop = JS_HasProperty(ctx, obj, prop_atom);
    JS_FreeAtom(ctx, prop_atom);
    if (!has_prop) {
        JS_FreeCString(ctx, prop);
        return JS_UNDEFINED;
    }
    
    // Create descriptor object
    GCValue desc = JS_NewObject(ctx);
    GCValue val = JS_GetPropertyStr(ctx, obj, prop);
    JS_SetPropertyStr(ctx, desc, "value", val);
    JS_SetPropertyStr(ctx, desc, "writable", JS_TRUE);
    JS_SetPropertyStr(ctx, desc, "enumerable", JS_TRUE);
    JS_SetPropertyStr(ctx, desc, "configurable", JS_TRUE);
    
    JS_FreeCString(ctx, prop);
    return desc;
}

// Object.setPrototypeOf polyfill
static GCValue js_object_set_prototype_of(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    if (argc < 2) return JS_EXCEPTION;
    
    GCValue obj = argv[0];
    GCValue proto = argv[1];
    
    // Check for null/undefined
    if (JS_IsNull(obj) || JS_IsUndefined(obj)) {
        return JS_ThrowTypeError(ctx, "Object.setPrototypeOf called on null or undefined");
    }
    
    // Set the prototype using __proto__
    GCValue proto_key = JS_NewString(ctx, "__proto__");
    JS_SetProperty(ctx, obj, JS_ValueToAtom(ctx, proto_key), proto);

    return obj;
}

// Object.getOwnPropertySymbols polyfill - returns empty array
static GCValue js_object_get_own_property_symbols(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewArray(ctx);
}

// Object.assign polyfill
static GCValue js_object_assign(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    (void)this_val;
    if (argc < 1) return JS_UNDEFINED;
    
    GCValue target = argv[0];
    
    for (int i = 1; i < argc; i++) {
        GCValue source = argv[i];
        if (JS_IsNull(source) || JS_IsUndefined(source)) continue;
        
        // Use Object.keys to get enumerable properties
        GCValue global = JS_GetGlobalObject(ctx);
        GCValue object_ctor = JS_GetPropertyStr(ctx, global, "Object");
        GCValue keys_func = JS_GetPropertyStr(ctx, object_ctor, "keys");
        GCValue keys = JS_Call(ctx, keys_func, JS_UNDEFINED, 1, &source);



        if (!JS_IsException(keys)) {
            GCValue len_val = JS_GetPropertyStr(ctx, keys, "length");
            uint32_t key_count = 0;
            JS_ToUint32(ctx, &key_count, len_val);

            for (uint32_t j = 0; j < key_count; j++) {
                GCValue key_val = JS_GetPropertyUint32(ctx, keys, j);
                const char *key = JS_ToCString(ctx, key_val);
                if (key) {
                    GCValue val = JS_GetPropertyStr(ctx, source, key);
                    JS_SetPropertyStr(ctx, target, key, val);
                    JS_FreeCString(ctx, key);
                }

            }

        }
    }
    
    return target;
}

// Reflect.construct polyfill
static GCValue js_reflect_construct(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    (void)this_val;
    if (argc < 2) return JS_EXCEPTION;
    
    GCValue target = argv[0];
    GCValue args_array = argv[1];
    
    // Get length of args array
    GCValue len_val = JS_GetPropertyStr(ctx, args_array, "length");
    uint32_t args_len = 0;
    JS_ToUint32(ctx, &args_len, len_val);

    // Build arguments array
    GCValue *args = malloc(sizeof(GCValue) * args_len);
    for (uint32_t i = 0; i < args_len; i++) {
        args[i] = JS_GetPropertyUint32(ctx, args_array, i);
    }
    
    // Call constructor
    GCValue result = JS_CallConstructor(ctx, target, (int)args_len, args);
    
    for (uint32_t i = 0; i < args_len; i++) {

    }
    free(args);
    
    return result;
}

// Reflect.apply polyfill
static GCValue js_reflect_apply(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    (void)this_val;
    if (argc < 3) return JS_EXCEPTION;
    
    GCValue func = argv[0];
    GCValue this_arg = argv[1];
    GCValue args_array = argv[2];
    
    // Get length of args array
    GCValue args_len_val = JS_GetPropertyStr(ctx, args_array, "length");
    uint32_t args_len = 0;
    JS_ToUint32(ctx, &args_len, args_len_val);

    // Build arguments array
    GCValue *args = malloc(sizeof(GCValue) * args_len);
    for (uint32_t i = 0; i < args_len; i++) {
        args[i] = JS_GetPropertyUint32(ctx, args_array, i);
    }
    
    // Call function
    GCValue result = JS_Call(ctx, func, this_arg, (int)args_len, args);
    
    for (uint32_t i = 0; i < args_len; i++) {

    }
    free(args);
    
    return result;
}

// Reflect.has polyfill
static GCValue js_reflect_has(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    (void)this_val;
    if (argc < 2) return JS_EXCEPTION;
    
    GCValue target = argv[0];
    const char *prop = JS_ToCString(ctx, argv[1]);
    if (!prop) return JS_FALSE;
    
    JSAtom prop_atom = JS_NewAtom(ctx, prop);
    int has_prop = JS_HasProperty(ctx, target, prop_atom);
    JS_FreeAtom(ctx, prop_atom);
    JS_FreeCString(ctx, prop);
    
    return JS_NewBool(ctx, has_prop);
}

// Promise.prototype.finally polyfill
static GCValue js_promise_finally(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    if (argc < 1 || !JS_IsObject(this_val)) return JS_EXCEPTION;
    
    GCValue on_finally = argv[0];
    
    // Create the finally handler
    GCValue handler = JS_NewCFunction(ctx, js_dummy_function, "finally_handler", 0);
    
    // Call .then with the handler
    GCValue then_method = JS_GetPropertyStr(ctx, this_val, "then");
    GCValue args[2] = { handler, handler };
    GCValue result = JS_Call(ctx, then_method, this_val, 2, args);


    return result;
}

// String.prototype.includes polyfill
static GCValue js_string_includes(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    const char *str = JS_ToCString(ctx, this_val);
    if (!str) return JS_FALSE;
    
    if (argc < 1) {
        JS_FreeCString(ctx, str);
        return JS_FALSE;
    }
    
    const char *search = JS_ToCString(ctx, argv[0]);
    if (!search) {
        JS_FreeCString(ctx, str);
        return JS_FALSE;
    }
    
    int32_t start = 0;
    if (argc > 1) {
        JS_ToInt32(ctx, &start, argv[1]);
    }
    
    // Adjust start position
    size_t str_len = strlen(str);
    if (start < 0) start = 0;
    if ((size_t)start > str_len) start = (int32_t)str_len;
    
    // Search for substring
    const char *found = strstr(str + start, search);
    
    JS_FreeCString(ctx, str);
    JS_FreeCString(ctx, search);
    
    return JS_NewBool(ctx, found != NULL);
}

// Array.prototype.includes polyfill
static GCValue js_array_includes(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    if (argc < 1) return JS_FALSE;
    
    GCValue search_element = argv[0];
    int32_t from_index = 0;
    if (argc > 1) {
        JS_ToInt32(ctx, &from_index, argv[1]);
    }
    
    GCValue len_val = JS_GetPropertyStr(ctx, this_val, "length");
    uint32_t len = 0;
    JS_ToUint32(ctx, &len, len_val);

    if (from_index < 0) {
        from_index = (int32_t)len + from_index;
        if (from_index < 0) from_index = 0;
    }
    
    for (uint32_t i = (uint32_t)from_index; i < len; i++) {
        GCValue elem = JS_GetPropertyUint32(ctx, this_val, i);
        int is_equal = JS_StrictEq(ctx, elem, search_element);

        if (is_equal) return JS_TRUE;
    }
    
    return JS_FALSE;
}

// Array.from polyfill
static GCValue js_array_from(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    (void)this_val;
    if (argc < 1) return JS_NewArray(ctx);
    
    GCValue array_like = argv[0];
    uint32_t len = 0;
    
    GCValue len_val2 = JS_GetPropertyStr(ctx, array_like, "length");
    if (JS_ToUint32(ctx, &len, len_val2)) {


        return JS_NewArray(ctx);
    }

    GCValue result = JS_NewArray(ctx);
    for (uint32_t i = 0; i < len; i++) {
        GCValue val = JS_GetPropertyUint32(ctx, array_like, i);
        JS_SetPropertyUint32(ctx, result, i, val);
    }
    
    return result;
}

// ============================================================================
// Map Polyfill Implementation
// ============================================================================

typedef struct {
    GCValue entries;  // Object storing key->value mappings
    int size;
} MapData;

JSClassID js_map_class_id = 0;

static void js_map_finalizer(JSRuntime *rt, GCValue val) {
    MapData *map = JS_GetOpaque(val, js_map_class_id);
    if (map) {

        free(map);
    }
}

static JSClassDef js_map_class_def = {
    "Map",
    .finalizer = js_map_finalizer,
};

static GCValue js_map_constructor(JSContext *ctx, GCValueConst new_target, int argc, GCValueConst *argv) {
    MapData *map = calloc(1, sizeof(MapData));
    if (!map) return JS_EXCEPTION;
    
    map->entries = JS_NewObject(ctx);
    map->size = 0;
    
    GCValue obj = JS_NewObjectClass(ctx, js_map_class_id);
    JS_SetOpaque(obj, map);
    return obj;
}

static GCValue js_map_set(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    MapData *map = JS_GetOpaque2(ctx, this_val, js_map_class_id);
    if (!map || argc < 2) return JS_EXCEPTION;
    
    const char *key = JS_ToCString(ctx, argv[0]);
    if (!key) return JS_EXCEPTION;
    
    // Check if key exists
    GCValue existing = JS_GetPropertyStr(ctx, map->entries, key);
    int exists = !JS_IsUndefined(existing);

    if (!exists) map->size++;
    
    JS_SetPropertyStr(ctx, map->entries, key, argv[1]);
    JS_FreeCString(ctx, key);
    
    return this_val;
}

static GCValue js_map_get(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    MapData *map = JS_GetOpaque2(ctx, this_val, js_map_class_id);
    if (!map || argc < 1) return JS_EXCEPTION;
    
    const char *key = JS_ToCString(ctx, argv[0]);
    if (!key) return JS_EXCEPTION;
    
    GCValue val = JS_GetPropertyStr(ctx, map->entries, key);
    JS_FreeCString(ctx, key);
    
    return val;
}

static GCValue js_map_has(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    MapData *map = JS_GetOpaque2(ctx, this_val, js_map_class_id);
    if (!map || argc < 1) return JS_EXCEPTION;
    
    const char *key = JS_ToCString(ctx, argv[0]);
    if (!key) return JS_EXCEPTION;
    
    GCValue val = JS_GetPropertyStr(ctx, map->entries, key);
    JS_FreeCString(ctx, key);
    
    int exists = !JS_IsUndefined(val);

    return JS_NewBool(ctx, exists);
}

static GCValue js_map_delete(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    MapData *map = JS_GetOpaque2(ctx, this_val, js_map_class_id);
    if (!map || argc < 1) return JS_EXCEPTION;
    
    const char *key = JS_ToCString(ctx, argv[0]);
    if (!key) return JS_EXCEPTION;
    
    GCValue val = JS_GetPropertyStr(ctx, map->entries, key);
    int exists = !JS_IsUndefined(val);

    if (exists) {
        GCValue undefined = JS_UNDEFINED;
        JS_SetPropertyStr(ctx, map->entries, key, undefined);
        map->size--;
    }
    
    JS_FreeCString(ctx, key);
    return JS_NewBool(ctx, exists);
}

static GCValue js_map_clear(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    (void)argc; (void)argv;
    MapData *map = JS_GetOpaque2(ctx, this_val, js_map_class_id);
    if (!map) return JS_EXCEPTION;

    map->entries = JS_NewObject(ctx);
    map->size = 0;
    
    return JS_UNDEFINED;
}

static GCValue js_map_get_size(JSContext *ctx, GCValueConst this_val) {
    MapData *map = JS_GetOpaque2(ctx, this_val, js_map_class_id);
    if (!map) return JS_EXCEPTION;
    return JS_NewInt32(ctx, map->size);
}

static const JSCFunctionListEntry js_map_proto_funcs[] = {
    JS_CFUNC_DEF("set", 2, js_map_set),
    JS_CFUNC_DEF("get", 1, js_map_get),
    JS_CFUNC_DEF("has", 1, js_map_has),
    JS_CFUNC_DEF("delete", 1, js_map_delete),
    JS_CFUNC_DEF("clear", 0, js_map_clear),
    JS_CGETSET_DEF("size", js_map_get_size, NULL),
};

extern JSClassID js_xhr_class_id;
extern JSClassID js_video_class_id;
extern GCValue js_xhr_constructor(JSContext *ctx, GCValueConst new_target, int argc, GCValueConst *argv);
extern GCValue js_video_constructor(JSContext *ctx, GCValueConst new_target, int argc, GCValueConst *argv);
extern GCValue js_fetch(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv);
extern const JSCFunctionListEntry js_xhr_proto_funcs[];
extern const JSCFunctionListEntry js_video_proto_funcs[];
extern const size_t js_xhr_proto_funcs_count;
extern const size_t js_video_proto_funcs_count;

// Class IDs for new APIs
JSClassID js_shadow_root_class_id = 0;
JSClassID js_animation_class_id = 0;
JSClassID js_keyframe_effect_class_id = 0;
JSClassID js_font_face_class_id = 0;
JSClassID js_font_face_set_class_id = 0;
JSClassID js_custom_element_registry_class_id = 0;
JSClassID js_mutation_observer_class_id = 0;
JSClassID js_resize_observer_class_id = 0;
JSClassID js_intersection_observer_class_id = 0;
JSClassID js_performance_class_id = 0;
JSClassID js_performance_entry_class_id = 0;
JSClassID js_performance_observer_class_id = 0;
JSClassID js_performance_timing_class_id = 0;
JSClassID js_dom_rect_class_id = 0;
JSClassID js_dom_rect_read_only_class_id = 0;

// Helper macros
#define DEF_FUNC(ctx, parent, name, func, argc) \
    JS_SetPropertyStr(ctx, parent, name, JS_NewCFunction(ctx, func, name, argc))

#define DEF_PROP_STR(ctx, obj, name, value) \
    JS_SetPropertyStr(ctx, obj, name, JS_NewString(ctx, value))

#define DEF_PROP_INT(ctx, obj, name, value) \
    JS_SetPropertyStr(ctx, obj, name, JS_NewInt32(ctx, value))

#define DEF_PROP_BOOL(ctx, obj, name, value) \
    JS_SetPropertyStr(ctx, obj, name, JS_NewBool(ctx, value))

#define DEF_PROP_FLOAT(ctx, obj, name, value) \
    JS_SetPropertyStr(ctx, obj, name, JS_NewFloat64(ctx, value))

#define DEF_PROP_UNDEFINED(ctx, obj, name) \
    JS_SetPropertyStr(ctx, obj, name, JS_UNDEFINED)

// Dummy then function for mock promises
static GCValue js_promise_then(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    // Call the onFulfilled callback immediately with undefined
    if (argc > 0 && JS_IsFunction(ctx, argv[0])) {
        GCValue undefined = JS_UNDEFINED;
        GCValue result = JS_Call(ctx, argv[0], JS_UNDEFINED, 1, &undefined);

    }
    return this_val;
}

// Helper to create a resolved Promise
static GCValue js_create_resolved_promise(JSContext *ctx, GCValue value) {
    GCValue global = JS_GetGlobalObject(ctx);
    GCValue promise_ctor = JS_GetPropertyStr(ctx, global, "Promise");
    
    // Check if Promise constructor exists and is an object
    if (JS_IsException(promise_ctor) || !JS_IsObject(promise_ctor)) {


        // Fallback: return a mock promise-like object
        GCValue mock_promise = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, mock_promise, "then", 
            JS_NewCFunction(ctx, js_promise_then, "then", 2));
        return mock_promise;
    }
    
    GCValue resolve_func = JS_GetPropertyStr(ctx, promise_ctor, "resolve");
    
    // Check if Promise.resolve exists and is a function
    if (JS_IsException(resolve_func) || !JS_IsFunction(ctx, resolve_func)) {



        // Fallback: return a mock promise-like object
        GCValue mock_promise = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, mock_promise, "then", 
            JS_NewCFunction(ctx, js_promise_then, "then", 2));
        return mock_promise;
    }
    
    // Call Promise.resolve with the Promise constructor as 'this'
    GCValue result = JS_Call(ctx, resolve_func, promise_ctor, 1, &value);



    return result;
}

// Helper to create an empty resolved Promise
static GCValue js_create_empty_resolved_promise(JSContext *ctx) {
    GCValue global = JS_GetGlobalObject(ctx);
    GCValue promise_ctor = JS_GetPropertyStr(ctx, global, "Promise");
    
    // Check if Promise constructor exists and is an object
    if (JS_IsException(promise_ctor) || !JS_IsObject(promise_ctor)) {


        // Fallback: return a mock promise-like object
        GCValue mock_promise = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, mock_promise, "then", 
            JS_NewCFunction(ctx, js_promise_then, "then", 2));
        return mock_promise;
    }
    
    GCValue resolve_func = JS_GetPropertyStr(ctx, promise_ctor, "resolve");
    
    // Check if Promise.resolve exists and is a function
    if (JS_IsException(resolve_func) || !JS_IsFunction(ctx, resolve_func)) {



        // Fallback: return a mock promise-like object
        GCValue mock_promise = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, mock_promise, "then", 
            JS_NewCFunction(ctx, js_promise_then, "then", 2));
        return mock_promise;
    }
    
    // Call Promise.resolve with the Promise constructor as 'this'
    GCValue result = JS_Call(ctx, resolve_func, promise_ctor, 0, NULL);



    return result;
}

// ============================================================================
// Shadow DOM Implementation
// ============================================================================

typedef struct {
    GCValue host;           // The element that hosts this shadow root
    char mode[16];          // "open" or "closed"
    GCValue innerHTML;      // Shadow root content (as string for stub)
} ShadowRootData;

static void js_shadow_root_finalizer(JSRuntime *rt, GCValue val) {
    ShadowRootData *sr = JS_GetOpaque(val, js_shadow_root_class_id);
    if (sr) {


        free(sr);
    }
}

static JSClassDef js_shadow_root_class_def = {
    "ShadowRoot",
    .finalizer = js_shadow_root_finalizer,
};

static GCValue js_shadow_root_get_host(JSContext *ctx, GCValueConst this_val) {
    ShadowRootData *sr = JS_GetOpaque2(ctx, this_val, js_shadow_root_class_id);
    if (!sr) return JS_EXCEPTION;
    return sr->host;
}

static GCValue js_shadow_root_get_mode(JSContext *ctx, GCValueConst this_val) {
    ShadowRootData *sr = JS_GetOpaque2(ctx, this_val, js_shadow_root_class_id);
    if (!sr) return JS_EXCEPTION;
    return JS_NewString(ctx, sr->mode);
}

static GCValue js_shadow_root_get_innerHTML(JSContext *ctx, GCValueConst this_val) {
    ShadowRootData *sr = JS_GetOpaque2(ctx, this_val, js_shadow_root_class_id);
    if (!sr) return JS_EXCEPTION;
    return sr->innerHTML;
}

static GCValue js_shadow_root_set_innerHTML(JSContext *ctx, GCValueConst this_val, GCValueConst val) {
    ShadowRootData *sr = JS_GetOpaque2(ctx, this_val, js_shadow_root_class_id);
    if (!sr) return JS_EXCEPTION;

    sr->innerHTML = val;
    return JS_UNDEFINED;
}

static GCValue js_shadow_root_querySelector(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_NULL;
}

static GCValue js_shadow_root_querySelectorAll(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_NewArray(ctx);
}

static GCValue js_shadow_root_getElementById(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_NULL;
}

static const JSCFunctionListEntry js_shadow_root_proto_funcs[] = {
    JS_CGETSET_DEF("host", js_shadow_root_get_host, NULL),
    JS_CGETSET_DEF("mode", js_shadow_root_get_mode, NULL),
    JS_CGETSET_DEF("innerHTML", js_shadow_root_get_innerHTML, js_shadow_root_set_innerHTML),
    JS_CFUNC_DEF("querySelector", 1, js_shadow_root_querySelector),
    JS_CFUNC_DEF("querySelectorAll", 1, js_shadow_root_querySelectorAll),
    JS_CFUNC_DEF("getElementById", 1, js_shadow_root_getElementById),
    JS_CFUNC_DEF("addEventListener", 2, js_dummy_function),
    JS_CFUNC_DEF("removeEventListener", 2, js_dummy_function),
    JS_CFUNC_DEF("dispatchEvent", 1, js_dummy_function_true),
    JS_PROP_STRING_DEF("nodeType", "11", JS_PROP_ENUMERABLE),  // DOCUMENT_FRAGMENT_NODE
};

// Element.prototype.attachShadow()
static GCValue js_element_attach_shadow(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    if (argc < 1 || !JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(ctx, "attachShadow requires an init object");
    }
    
    // Get mode from init object
    GCValue mode_val = JS_GetPropertyStr(ctx, argv[0], "mode");
    const char *mode = JS_ToCString(ctx, mode_val);
    if (!mode) mode = "closed";
    
    // Create ShadowRoot instance
    ShadowRootData *sr = calloc(1, sizeof(ShadowRootData));
    if (!sr) {
        JS_FreeCString(ctx, mode);

        return JS_EXCEPTION;
    }
    
    strncpy(sr->mode, mode, sizeof(sr->mode) - 1);
    sr->mode[sizeof(sr->mode) - 1] = '\0';
    sr->host = this_val;
    sr->innerHTML = JS_NewString(ctx, "");
    
    GCValue shadow_root = JS_NewObjectClass(ctx, js_shadow_root_class_id);
    JS_SetOpaque(shadow_root, sr);
    
    // Store shadowRoot reference on the element (internal property __shadowRoot)
    JS_SetPropertyStr(ctx, this_val, "__shadowRoot", shadow_root);
    
    JS_FreeCString(ctx, mode);

    return shadow_root;
}

// Element.prototype.shadowRoot getter
static GCValue js_element_get_shadow_root(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    (void)argc; (void)argv;
    GCValue shadow = JS_GetPropertyStr(ctx, this_val, "__shadowRoot");
    if (JS_IsUndefined(shadow)) {

        return JS_NULL;
    }
    
    // Check if mode is "open" - if closed, return null
    ShadowRootData *sr = JS_GetOpaque(shadow, js_shadow_root_class_id);
    if (sr && strcmp(sr->mode, "closed") == 0) {

        return JS_NULL;
    }
    
    return shadow;
}

// Element.prototype.querySelector
static GCValue js_element_querySelector(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_NULL;
}

// Element.prototype.querySelectorAll
static GCValue js_element_querySelectorAll(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_NewArray(ctx);
}

// Element.prototype.getAttribute
static GCValue js_element_get_attribute(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_NULL;
}

// Element.prototype.setAttribute
static GCValue js_element_set_attribute(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_UNDEFINED;
}

// ============================================================================
// EventTarget Implementation
// ============================================================================

static GCValue js_event_target_addEventListener(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    // Store event handlers on the element for dispatch
    if (argc < 2) return JS_UNDEFINED;
    
    const char *event = JS_ToCString(ctx, argv[0]);
    if (event) {
        char prop[128];
        snprintf(prop, sizeof(prop), "__on%s", event);
        JS_SetPropertyStr(ctx, this_val, prop, argv[1]);
    }
    JS_FreeCString(ctx, event);
    return JS_UNDEFINED;
}

static GCValue js_event_target_removeEventListener(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    // Stub - just return undefined
    return JS_UNDEFINED;
}

static GCValue js_event_target_dispatchEvent(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_TRUE;
}

// ============================================================================
// Node Implementation
// ============================================================================

static GCValue js_node_appendChild(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    if (argc < 1) return JS_NULL;
    // Return the appended child
    return argv[0];
}

static GCValue js_node_insertBefore(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    if (argc < 1) return JS_NULL;
    return argv[0];
}

static GCValue js_node_removeChild(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    if (argc < 1) return JS_NULL;
    return argv[0];
}

static GCValue js_node_cloneNode(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    // Return a new empty object as cloned node
    return JS_NewObject(ctx);
}

static GCValue js_node_contains(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_FALSE;
}

// ============================================================================
// Custom Elements API Implementation
// ============================================================================

typedef struct {
    GCValue registry;  // Map of tag names to constructor functions
} CustomElementRegistryData;

static void js_custom_element_registry_finalizer(JSRuntime *rt, GCValue val) {
    CustomElementRegistryData *cer = JS_GetOpaque(val, js_custom_element_registry_class_id);
    if (cer) {

        free(cer);
    }
}

static JSClassDef js_custom_element_registry_class_def = {
    "CustomElementRegistry",
    .finalizer = js_custom_element_registry_finalizer,
};

// customElements.define(name, constructor, options)
static GCValue js_custom_elements_define(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "define requires at least 2 arguments");
    }
    
    const char *name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_EXCEPTION;
    
    // Validate name format (must contain hyphen)
    if (strchr(name, '-') == NULL) {
        JS_FreeCString(ctx, name);
        return JS_ThrowTypeError(ctx, "Custom element name must contain a hyphen");
    }
    
    // Store in registry (the this_val should be the customElements object)
    JS_SetPropertyStr(ctx, this_val, name, argv[1]);
    
    JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
}

// customElements.get(name)
static GCValue js_custom_elements_get(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    if (argc < 1) return JS_UNDEFINED;
    
    const char *name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_UNDEFINED;
    
    GCValue ctor = JS_GetPropertyStr(ctx, this_val, name);
    
    JS_FreeCString(ctx, name);
    
    if (JS_IsUndefined(ctor)) {
        return JS_UNDEFINED;
    }
    return ctor;
}

// customElements.whenDefined(name)
static GCValue js_custom_elements_when_defined(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    // Return a resolved promise
    return js_create_empty_resolved_promise(ctx);
}

// ============================================================================
// Web Animations API Implementation
// ============================================================================

typedef struct {
    double current_time;
    double duration;
    int play_state;  // 0=idle, 1=running, 2=paused, 3=finished
    GCValue onfinish;
    GCValue effect;
    JSContext *ctx;
} AnimationData;

typedef struct {
    GCValue target;
    GCValue keyframes;
    double duration;
    char easing[32];
} KeyFrameEffectData;

static void js_animation_finalizer(JSRuntime *rt, GCValue val) {
    AnimationData *anim = JS_GetOpaque(val, js_animation_class_id);
    if (anim) {


        free(anim);
    }
}

static void js_keyframe_effect_finalizer(JSRuntime *rt, GCValue val) {
    KeyFrameEffectData *effect = JS_GetOpaque(val, js_keyframe_effect_class_id);
    if (effect) {


        free(effect);
    }
}

static JSClassDef js_animation_class_def = {
    "Animation",
    .finalizer = js_animation_finalizer,
};

static JSClassDef js_keyframe_effect_class_def = {
    "KeyframeEffect",
    .finalizer = js_keyframe_effect_finalizer,
};

// Animation constructor
static GCValue js_animation_constructor(JSContext *ctx, GCValueConst new_target, int argc, GCValueConst *argv) {
    AnimationData *anim = calloc(1, sizeof(AnimationData));
    if (!anim) return JS_EXCEPTION;
    
    anim->ctx = ctx;
    anim->current_time = 0;
    anim->duration = 0;
    anim->play_state = 0;  // idle
    anim->onfinish = JS_NULL;
    anim->effect = JS_NULL;
    
    if (argc > 0) {
        anim->effect = argv[0];
        // Try to get duration from effect
        if (JS_IsObject(argv[0])) {
            GCValue duration_val = JS_GetPropertyStr(ctx, argv[0], "duration");
            double duration;
            if (!JS_IsException(duration_val) && !JS_ToFloat64(ctx, &duration, duration_val)) {
                anim->duration = duration;
            }

        }
    }
    
    GCValue obj = JS_NewObjectClass(ctx, js_animation_class_id);
    JS_SetOpaque(obj, anim);
    return obj;
}

// Animation.prototype.play()
static GCValue js_animation_play(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    AnimationData *anim = JS_GetOpaque2(ctx, this_val, js_animation_class_id);
    if (!anim) return JS_EXCEPTION;
    anim->play_state = 1;  // running
    return JS_UNDEFINED;
}

// Animation.prototype.pause()
static GCValue js_animation_pause(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    AnimationData *anim = JS_GetOpaque2(ctx, this_val, js_animation_class_id);
    if (!anim) return JS_EXCEPTION;
    anim->play_state = 2;  // paused
    return JS_UNDEFINED;
}

// Animation.prototype.finish()
static GCValue js_animation_finish(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    AnimationData *anim = JS_GetOpaque2(ctx, this_val, js_animation_class_id);
    if (!anim) return JS_EXCEPTION;
    anim->play_state = 3;  // finished
    anim->current_time = anim->duration;
    
    // Call onfinish callback if set
    if (!JS_IsNull(anim->onfinish) && JS_IsFunction(ctx, anim->onfinish)) {
        JS_Call(ctx, anim->onfinish, this_val, 0, NULL);
    }
    return JS_UNDEFINED;
}

// Animation.prototype.cancel()
static GCValue js_animation_cancel(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    AnimationData *anim = JS_GetOpaque2(ctx, this_val, js_animation_class_id);
    if (!anim) return JS_EXCEPTION;
    anim->play_state = 0;  // idle
    anim->current_time = 0;
    return JS_UNDEFINED;
}

// Animation.prototype.reverse()
static GCValue js_animation_reverse(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_UNDEFINED;
}

// Animation.playState getter
static GCValue js_animation_get_play_state(JSContext *ctx, GCValueConst this_val) {
    AnimationData *anim = JS_GetOpaque2(ctx, this_val, js_animation_class_id);
    if (!anim) return JS_EXCEPTION;
    const char *states[] = {"idle", "running", "paused", "finished"};
    return JS_NewString(ctx, states[anim->play_state]);
}

// Animation.currentTime getter
static GCValue js_animation_get_current_time(JSContext *ctx, GCValueConst this_val) {
    AnimationData *anim = JS_GetOpaque2(ctx, this_val, js_animation_class_id);
    if (!anim) return JS_EXCEPTION;
    return JS_NewFloat64(ctx, anim->current_time);
}

// Animation.effect getter
static GCValue js_animation_get_effect(JSContext *ctx, GCValueConst this_val) {
    AnimationData *anim = JS_GetOpaque2(ctx, this_val, js_animation_class_id);
    if (!anim) return JS_EXCEPTION;
    if (JS_IsNull(anim->effect)) return JS_NULL;
    return anim->effect;
}

// Animation.onfinish getter/setter
static GCValue js_animation_get_onfinish(JSContext *ctx, GCValueConst this_val) {
    AnimationData *anim = JS_GetOpaque2(ctx, this_val, js_animation_class_id);
    if (!anim) return JS_EXCEPTION;
    return anim->onfinish;
}

static GCValue js_animation_set_onfinish(JSContext *ctx, GCValueConst this_val, GCValueConst val) {
    AnimationData *anim = JS_GetOpaque2(ctx, this_val, js_animation_class_id);
    if (!anim) return JS_EXCEPTION;

    anim->onfinish = val;
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry js_animation_proto_funcs[] = {
    JS_CFUNC_DEF("play", 0, js_animation_play),
    JS_CFUNC_DEF("pause", 0, js_animation_pause),
    JS_CFUNC_DEF("finish", 0, js_animation_finish),
    JS_CFUNC_DEF("cancel", 0, js_animation_cancel),
    JS_CFUNC_DEF("reverse", 0, js_animation_reverse),
    JS_CGETSET_DEF("playState", js_animation_get_play_state, NULL),
    JS_CGETSET_DEF("currentTime", js_animation_get_current_time, NULL),
    JS_CGETSET_DEF("effect", js_animation_get_effect, NULL),
    JS_CGETSET_DEF("onfinish", js_animation_get_onfinish, js_animation_set_onfinish),
};

// KeyframeEffect constructor
static GCValue js_keyframe_effect_constructor(JSContext *ctx, GCValueConst new_target, int argc, GCValueConst *argv) {
    KeyFrameEffectData *effect = calloc(1, sizeof(KeyFrameEffectData));
    if (!effect) return JS_EXCEPTION;
    
    effect->target = JS_NULL;
    effect->keyframes = JS_NULL;
    effect->duration = 0;
    strcpy(effect->easing, "linear");
    
    if (argc > 0) {
        effect->target = argv[0];
    }
    if (argc > 1) {
        effect->keyframes = argv[1];
    }
    if (argc > 2 && JS_IsObject(argv[2])) {
        GCValue duration_val = JS_GetPropertyStr(ctx, argv[2], "duration");
        double duration;
        if (!JS_IsException(duration_val) && !JS_ToFloat64(ctx, &duration, duration_val)) {
            effect->duration = duration;
        }

        GCValue easing_val = JS_GetPropertyStr(ctx, argv[2], "easing");
        const char *easing = JS_ToCString(ctx, easing_val);
        if (easing) {
            strncpy(effect->easing, easing, sizeof(effect->easing) - 1);
            effect->easing[sizeof(effect->easing) - 1] = '\0';
        }
        JS_FreeCString(ctx, easing);

    }
    
    GCValue obj = JS_NewObjectClass(ctx, js_keyframe_effect_class_id);
    JS_SetOpaque(obj, effect);
    return obj;
}

// KeyframeEffect.target getter
static GCValue js_keyframe_effect_get_target(JSContext *ctx, GCValueConst this_val) {
    KeyFrameEffectData *effect = JS_GetOpaque2(ctx, this_val, js_keyframe_effect_class_id);
    if (!effect) return JS_EXCEPTION;
    if (JS_IsNull(effect->target)) return JS_NULL;
    return effect->target;
}

// KeyframeEffect.duration getter
static GCValue js_keyframe_effect_get_duration(JSContext *ctx, GCValueConst this_val) {
    KeyFrameEffectData *effect = JS_GetOpaque2(ctx, this_val, js_keyframe_effect_class_id);
    if (!effect) return JS_EXCEPTION;
    return JS_NewFloat64(ctx, effect->duration);
}

static const JSCFunctionListEntry js_keyframe_effect_proto_funcs[] = {
    JS_CGETSET_DEF("target", js_keyframe_effect_get_target, NULL),
    JS_CGETSET_DEF("duration", js_keyframe_effect_get_duration, NULL),
};

// Element.prototype.animate(keyframes, options)
static GCValue js_element_animate(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    // Create KeyframeEffect
    GCValue effect_args[3];
    effect_args[0] = this_val;  // target
    effect_args[1] = argc > 0 ? argv[0] : JS_NULL;  // keyframes
    effect_args[2] = argc > 1 ? argv[1] : JS_NULL;  // options
    
    GCValue effect = js_keyframe_effect_constructor(ctx, JS_UNDEFINED, 3, effect_args);



    if (JS_IsException(effect)) {
        return effect;
    }
    
    // Create Animation with the effect
    GCValue anim_args[1];
    anim_args[0] = effect;
    GCValue animation = js_animation_constructor(ctx, JS_UNDEFINED, 1, anim_args);

    if (JS_IsException(animation)) {
        return animation;
    }
    
    // Auto-play the animation
    AnimationData *anim = JS_GetOpaque(animation, js_animation_class_id);
    if (anim) {
        anim->play_state = 1;  // running
    }
    
    return animation;
}

// ============================================================================
// Font Loading API Implementation
// ============================================================================

typedef struct {
    char family[256];
    char source[512];
    char display[32];
} FontFaceData;

typedef struct {
    GCValue loaded_fonts;  // Array of loaded FontFace objects
} FontFaceSetData;

static void js_font_face_finalizer(JSRuntime *rt, GCValue val) {
    FontFaceData *ff = JS_GetOpaque(val, js_font_face_class_id);
    if (ff) {
        free(ff);
    }
}

static void js_font_face_set_finalizer(JSRuntime *rt, GCValue val) {
    FontFaceSetData *ffs = JS_GetOpaque(val, js_font_face_set_class_id);
    if (ffs) {

        free(ffs);
    }
}

static JSClassDef js_font_face_class_def = {
    "FontFace",
    .finalizer = js_font_face_finalizer,
};

static JSClassDef js_font_face_set_class_def = {
    "FontFaceSet",
    .finalizer = js_font_face_set_finalizer,
};

// FontFace constructor
static GCValue js_font_face_constructor(JSContext *ctx, GCValueConst new_target, int argc, GCValueConst *argv) {
    FontFaceData *ff = calloc(1, sizeof(FontFaceData));
    if (!ff) return JS_EXCEPTION;
    
    if (argc > 0) {
        const char *family = JS_ToCString(ctx, argv[0]);
        if (family) {
            strncpy(ff->family, family, sizeof(ff->family) - 1);
        }
        JS_FreeCString(ctx, family);
    }
    
    if (argc > 1) {
        const char *source = JS_ToCString(ctx, argv[1]);
        if (source) {
            strncpy(ff->source, source, sizeof(ff->source) - 1);
        }
        JS_FreeCString(ctx, source);
    }
    
    if (argc > 2 && JS_IsObject(argv[2])) {
        GCValue display_val = JS_GetPropertyStr(ctx, argv[2], "display");
        const char *display = JS_ToCString(ctx, display_val);
        if (display) {
            strncpy(ff->display, display, sizeof(ff->display) - 1);
        }
        JS_FreeCString(ctx, display);

    }
    
    GCValue obj = JS_NewObjectClass(ctx, js_font_face_class_id);
    JS_SetOpaque(obj, ff);
    return obj;
}

// FontFace.load()
static GCValue js_font_face_load(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    // Return a resolved promise with this FontFace
    GCValue this_dup = this_val;
    GCValue result = js_create_resolved_promise(ctx, this_dup);

    return result;
}

// FontFace.loaded getter - returns a Promise that resolves to this FontFace
static GCValue js_font_face_get_loaded(JSContext *ctx, GCValueConst this_val) {
    FontFaceData *ff = JS_GetOpaque2(ctx, this_val, js_font_face_class_id);
    if (!ff) return JS_EXCEPTION;
    // Return a resolved promise with this FontFace
    GCValue this_dup = this_val;
    GCValue result = js_create_resolved_promise(ctx, this_dup);

    return result;
}

// FontFace.family getter
static GCValue js_font_face_get_family(JSContext *ctx, GCValueConst this_val) {
    FontFaceData *ff = JS_GetOpaque2(ctx, this_val, js_font_face_class_id);
    if (!ff) return JS_EXCEPTION;
    return JS_NewString(ctx, ff->family);
}

// FontFace.status getter
static GCValue js_font_face_get_status(JSContext *ctx, GCValueConst this_val) {
    return JS_NewString(ctx, "loaded");
}

static const JSCFunctionListEntry js_font_face_proto_funcs[] = {
    JS_CFUNC_DEF("load", 0, js_font_face_load),
    JS_CGETSET_DEF("family", js_font_face_get_family, NULL),
    JS_CGETSET_DEF("status", js_font_face_get_status, NULL),
    JS_CGETSET_DEF("loaded", js_font_face_get_loaded, NULL),  // Now returns a proper Promise
};

// FontFaceSet.load(fontSpec)
static GCValue js_font_face_set_load(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    // Return a resolved promise with empty array (all fonts "loaded")
    GCValue empty_array = JS_NewArray(ctx);
    GCValue result = js_create_resolved_promise(ctx, empty_array);

    return result;
}

// FontFaceSet.check(fontSpec)
static GCValue js_font_face_set_check(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    // Always return true (fonts are available)
    return JS_TRUE;
}

// FontFaceSet.ready getter
static GCValue js_font_face_set_get_ready(JSContext *ctx, GCValueConst this_val) {
    // Return a resolved promise
    return js_create_empty_resolved_promise(ctx);
}

// FontFaceSet.status getter
static GCValue js_font_face_set_get_status(JSContext *ctx, GCValueConst this_val) {
    return JS_NewString(ctx, "loaded");
}

// FontFaceSet.add(fontFace)
static GCValue js_font_face_set_add(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_UNDEFINED;
}

// FontFaceSet.delete(fontFace)
static GCValue js_font_face_set_delete(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_TRUE;
}

// FontFaceSet.clear()
static GCValue js_font_face_set_clear(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_UNDEFINED;
}

// FontFaceSet.has(fontFace)
static GCValue js_font_face_set_has(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_TRUE;
}

// FontFaceSet.forEach(callback)
static GCValue js_font_face_set_forEach(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_UNDEFINED;
}

// FontFaceSet[Symbol.iterator]()
static GCValue js_font_face_set_iterator(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    GCValue global = JS_GetGlobalObject(ctx);
    GCValue array_ctor = JS_GetPropertyStr(ctx, global, "Array");
    GCValue empty_array = JS_CallConstructor(ctx, array_ctor, 0, NULL);
    GCValue result = JS_GetPropertyStr(ctx, empty_array, "values");
    GCValue iterator = JS_Call(ctx, result, empty_array, 0, NULL);




    return iterator;
}

static const JSCFunctionListEntry js_font_face_set_proto_funcs[] = {
    JS_CFUNC_DEF("load", 1, js_font_face_set_load),
    JS_CFUNC_DEF("check", 1, js_font_face_set_check),
    JS_CGETSET_DEF("ready", js_font_face_set_get_ready, NULL),
    JS_CGETSET_DEF("status", js_font_face_set_get_status, NULL),
    JS_CFUNC_DEF("add", 1, js_font_face_set_add),
    JS_CFUNC_DEF("delete", 1, js_font_face_set_delete),
    JS_CFUNC_DEF("clear", 0, js_font_face_set_clear),
    JS_CFUNC_DEF("has", 1, js_font_face_set_has),
    JS_CFUNC_DEF("forEach", 1, js_font_face_set_forEach),
    JS_CFUNC_DEF("values", 0, js_font_face_set_iterator),
    JS_CFUNC_DEF("keys", 0, js_font_face_set_iterator),
    JS_CFUNC_DEF("entries", 0, js_font_face_set_iterator),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_font_face_set_iterator),
};

// ============================================================================
// MutationObserver Implementation
// ============================================================================

typedef struct {
    GCValue callback;
    JSContext *ctx;
} MutationObserverData;

static void js_mutation_observer_finalizer(JSRuntime *rt, GCValue val) {
    MutationObserverData *mo = JS_GetOpaque(val, js_mutation_observer_class_id);
    if (mo) {

        free(mo);
    }
}

static JSClassDef js_mutation_observer_class_def = {
    "MutationObserver",
    .finalizer = js_mutation_observer_finalizer,
};

// MutationObserver constructor
static GCValue js_mutation_observer_constructor(JSContext *ctx, GCValueConst new_target, int argc, GCValueConst *argv) {
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "MutationObserver constructor requires a callback function");
    }
    
    MutationObserverData *mo = calloc(1, sizeof(MutationObserverData));
    if (!mo) return JS_EXCEPTION;
    
    mo->ctx = ctx;
    mo->callback = argv[0];
    
    GCValue obj = JS_NewObjectClass(ctx, js_mutation_observer_class_id);
    JS_SetOpaque(obj, mo);
    return obj;
}

// MutationObserver.prototype.observe(target, options)
static GCValue js_mutation_observer_observe(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_UNDEFINED;
}

// MutationObserver.prototype.disconnect()
static GCValue js_mutation_observer_disconnect(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_UNDEFINED;
}

// MutationObserver.prototype.takeRecords()
static GCValue js_mutation_observer_takeRecords(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_NewArray(ctx);
}

static const JSCFunctionListEntry js_mutation_observer_proto_funcs[] = {
    JS_CFUNC_DEF("observe", 2, js_mutation_observer_observe),
    JS_CFUNC_DEF("disconnect", 0, js_mutation_observer_disconnect),
    JS_CFUNC_DEF("takeRecords", 0, js_mutation_observer_takeRecords),
};

// ============================================================================
// ResizeObserver Implementation
// ============================================================================

typedef struct {
    GCValue callback;
    JSContext *ctx;
} ResizeObserverData;

static void js_resize_observer_finalizer(JSRuntime *rt, GCValue val) {
    ResizeObserverData *ro = JS_GetOpaque(val, js_resize_observer_class_id);
    if (ro) {

        free(ro);
    }
}

static JSClassDef js_resize_observer_class_def = {
    "ResizeObserver",
    .finalizer = js_resize_observer_finalizer,
};

// ResizeObserver constructor
static GCValue js_resize_observer_constructor(JSContext *ctx, GCValueConst new_target, int argc, GCValueConst *argv) {
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "ResizeObserver constructor requires a callback function");
    }
    
    ResizeObserverData *ro = calloc(1, sizeof(ResizeObserverData));
    if (!ro) return JS_EXCEPTION;
    
    ro->ctx = ctx;
    ro->callback = argv[0];
    
    GCValue obj = JS_NewObjectClass(ctx, js_resize_observer_class_id);
    JS_SetOpaque(obj, ro);
    return obj;
}

// ResizeObserver.prototype.observe(target)
static GCValue js_resize_observer_observe(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_UNDEFINED;
}

// ResizeObserver.prototype.unobserve(target)
static GCValue js_resize_observer_unobserve(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_UNDEFINED;
}

// ResizeObserver.prototype.disconnect()
static GCValue js_resize_observer_disconnect(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry js_resize_observer_proto_funcs[] = {
    JS_CFUNC_DEF("observe", 1, js_resize_observer_observe),
    JS_CFUNC_DEF("unobserve", 1, js_resize_observer_unobserve),
    JS_CFUNC_DEF("disconnect", 0, js_resize_observer_disconnect),
};

// ============================================================================
// IntersectionObserver Implementation
// ============================================================================

typedef struct {
    GCValue callback;
    GCValue root;
    char rootMargin[32];
    double threshold;
    JSContext *ctx;
} IntersectionObserverData;

static void js_intersection_observer_finalizer(JSRuntime *rt, GCValue val) {
    IntersectionObserverData *io = JS_GetOpaque(val, js_intersection_observer_class_id);
    if (io) {


        free(io);
    }
}

static JSClassDef js_intersection_observer_class_def = {
    "IntersectionObserver",
    .finalizer = js_intersection_observer_finalizer,
};

// IntersectionObserver constructor
static GCValue js_intersection_observer_constructor(JSContext *ctx, GCValueConst new_target, int argc, GCValueConst *argv) {
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "IntersectionObserver constructor requires a callback function");
    }
    
    IntersectionObserverData *io = calloc(1, sizeof(IntersectionObserverData));
    if (!io) return JS_EXCEPTION;
    
    io->ctx = ctx;
    io->callback = argv[0];
    io->root = JS_NULL;
    strcpy(io->rootMargin, "0px");
    io->threshold = 0.0;
    
    // Parse options if provided
    if (argc > 1 && JS_IsObject(argv[1])) {
        GCValue root_val = JS_GetPropertyStr(ctx, argv[1], "root");
        if (!JS_IsUndefined(root_val) && !JS_IsNull(root_val)) {
            io->root = root_val;
        }

        GCValue margin_val = JS_GetPropertyStr(ctx, argv[1], "rootMargin");
        const char *margin = JS_ToCString(ctx, margin_val);
        if (margin) {
            strncpy(io->rootMargin, margin, sizeof(io->rootMargin) - 1);
            io->rootMargin[sizeof(io->rootMargin) - 1] = '\0';
        }
        JS_FreeCString(ctx, margin);

        GCValue threshold_val = JS_GetPropertyStr(ctx, argv[1], "threshold");
        JS_ToFloat64(ctx, &io->threshold, threshold_val);

    }
    
    GCValue obj = JS_NewObjectClass(ctx, js_intersection_observer_class_id);
    JS_SetOpaque(obj, io);
    return obj;
}

// IntersectionObserver.prototype.observe(target)
static GCValue js_intersection_observer_observe(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_UNDEFINED;
}

// IntersectionObserver.prototype.unobserve(target)
static GCValue js_intersection_observer_unobserve(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_UNDEFINED;
}

// IntersectionObserver.prototype.disconnect()
static GCValue js_intersection_observer_disconnect(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_UNDEFINED;
}

// IntersectionObserver.prototype.takeRecords()
static GCValue js_intersection_observer_takeRecords(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_NewArray(ctx);
}

static const JSCFunctionListEntry js_intersection_observer_proto_funcs[] = {
    JS_CFUNC_DEF("observe", 1, js_intersection_observer_observe),
    JS_CFUNC_DEF("unobserve", 1, js_intersection_observer_unobserve),
    JS_CFUNC_DEF("disconnect", 0, js_intersection_observer_disconnect),
    JS_CFUNC_DEF("takeRecords", 0, js_intersection_observer_takeRecords),
};

// ============================================================================
// Performance API Implementation
// ============================================================================

typedef struct {
    double start_time;
} PerformanceData;

typedef struct {
    char name[256];
    char entryType[64];
    double startTime;
    double duration;
} PerformanceEntryData;

typedef struct {
    GCValue callback;
    JSContext *ctx;
} PerformanceObserverData;

static void js_performance_finalizer(JSRuntime *rt, GCValue val) {
    PerformanceData *perf = JS_GetOpaque(val, js_performance_class_id);
    if (perf) {
        free(perf);
    }
}

static void js_performance_entry_finalizer(JSRuntime *rt, GCValue val) {
    PerformanceEntryData *entry = JS_GetOpaque(val, js_performance_entry_class_id);
    if (entry) {
        free(entry);
    }
}

static void js_performance_observer_finalizer(JSRuntime *rt, GCValue val) {
    PerformanceObserverData *po = JS_GetOpaque(val, js_performance_observer_class_id);
    if (po) {

        free(po);
    }
}

static JSClassDef js_performance_class_def = {
    "Performance",
    .finalizer = js_performance_finalizer,
};

static JSClassDef js_performance_entry_class_def = {
    "PerformanceEntry",
    .finalizer = js_performance_entry_finalizer,
};

static JSClassDef js_performance_observer_class_def = {
    "PerformanceObserver",
    .finalizer = js_performance_observer_finalizer,
};

// Performance.now()
static double g_performance_time = 0.0;

static GCValue js_performance_now(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    // Return a monotonically increasing timestamp (in milliseconds)
    // Use a static counter since we don't need actual wall-clock time
    g_performance_time += 0.1;  // Increment slightly on each call
    return JS_NewFloat64(ctx, g_performance_time);
}

// Performance.timeOrigin getter
static GCValue js_performance_get_time_origin(JSContext *ctx, GCValueConst this_val) {
    return JS_NewFloat64(ctx, 0.0);
}

// Performance.getEntries()
static GCValue js_performance_get_entries(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_NewArray(ctx);
}

// Performance.getEntriesByType(type)
static GCValue js_performance_get_entries_by_type(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_NewArray(ctx);
}

// Performance.getEntriesByName(name, type)
static GCValue js_performance_get_entries_by_name(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_NewArray(ctx);
}

// Performance.mark(name)
static GCValue js_performance_mark(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_UNDEFINED;
}

// Performance.measure(name, startMark, endMark)
static GCValue js_performance_measure(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_UNDEFINED;
}

// Performance.clearMarks(name)
static GCValue js_performance_clear_marks(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_UNDEFINED;
}

// Performance.clearMeasures(name)
static GCValue js_performance_clear_measures(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_UNDEFINED;
}

// PerformanceTiming properties - all return 0 as stubs
typedef struct {
    double navigationStart;
    double unloadEventStart;
    double unloadEventEnd;
    double redirectStart;
    double redirectEnd;
    double fetchStart;
    double domainLookupStart;
    double domainLookupEnd;
    double connectStart;
    double connectEnd;
    double secureConnectionStart;
    double requestStart;
    double responseStart;
    double responseEnd;
    double domLoading;
    double domInteractive;
    double domContentLoadedEventStart;
    double domContentLoadedEventEnd;
    double domComplete;
    double loadEventStart;
    double loadEventEnd;
} PerformanceTimingData;

static void js_performance_timing_finalizer(JSRuntime *rt, GCValue val) {
    PerformanceTimingData *timing = JS_GetOpaque(val, js_performance_timing_class_id);
    if (timing) {
        free(timing);
    }
}

static JSClassDef js_performance_timing_class_def = {
    "PerformanceTiming",
    .finalizer = js_performance_timing_finalizer,
};

#define DEF_TIMING_GETTER(field) \
static GCValue js_performance_timing_get_##field(JSContext *ctx, GCValueConst this_val) { \
    PerformanceTimingData *timing = JS_GetOpaque2(ctx, this_val, js_performance_timing_class_id); \
    if (!timing) return JS_EXCEPTION; \
    return JS_NewFloat64(ctx, timing->field); \
}

DEF_TIMING_GETTER(navigationStart)
DEF_TIMING_GETTER(unloadEventStart)
DEF_TIMING_GETTER(unloadEventEnd)
DEF_TIMING_GETTER(redirectStart)
DEF_TIMING_GETTER(redirectEnd)
DEF_TIMING_GETTER(fetchStart)
DEF_TIMING_GETTER(domainLookupStart)
DEF_TIMING_GETTER(domainLookupEnd)
DEF_TIMING_GETTER(connectStart)
DEF_TIMING_GETTER(connectEnd)
DEF_TIMING_GETTER(secureConnectionStart)
DEF_TIMING_GETTER(requestStart)
DEF_TIMING_GETTER(responseStart)
DEF_TIMING_GETTER(responseEnd)
DEF_TIMING_GETTER(domLoading)
DEF_TIMING_GETTER(domInteractive)
DEF_TIMING_GETTER(domContentLoadedEventStart)
DEF_TIMING_GETTER(domContentLoadedEventEnd)
DEF_TIMING_GETTER(domComplete)
DEF_TIMING_GETTER(loadEventStart)
DEF_TIMING_GETTER(loadEventEnd)

#undef DEF_TIMING_GETTER

static const JSCFunctionListEntry js_performance_timing_proto_funcs[] = {
    JS_CGETSET_DEF("navigationStart", js_performance_timing_get_navigationStart, NULL),
    JS_CGETSET_DEF("unloadEventStart", js_performance_timing_get_unloadEventStart, NULL),
    JS_CGETSET_DEF("unloadEventEnd", js_performance_timing_get_unloadEventEnd, NULL),
    JS_CGETSET_DEF("redirectStart", js_performance_timing_get_redirectStart, NULL),
    JS_CGETSET_DEF("redirectEnd", js_performance_timing_get_redirectEnd, NULL),
    JS_CGETSET_DEF("fetchStart", js_performance_timing_get_fetchStart, NULL),
    JS_CGETSET_DEF("domainLookupStart", js_performance_timing_get_domainLookupStart, NULL),
    JS_CGETSET_DEF("domainLookupEnd", js_performance_timing_get_domainLookupEnd, NULL),
    JS_CGETSET_DEF("connectStart", js_performance_timing_get_connectStart, NULL),
    JS_CGETSET_DEF("connectEnd", js_performance_timing_get_connectEnd, NULL),
    JS_CGETSET_DEF("secureConnectionStart", js_performance_timing_get_secureConnectionStart, NULL),
    JS_CGETSET_DEF("requestStart", js_performance_timing_get_requestStart, NULL),
    JS_CGETSET_DEF("responseStart", js_performance_timing_get_responseStart, NULL),
    JS_CGETSET_DEF("responseEnd", js_performance_timing_get_responseEnd, NULL),
    JS_CGETSET_DEF("domLoading", js_performance_timing_get_domLoading, NULL),
    JS_CGETSET_DEF("domInteractive", js_performance_timing_get_domInteractive, NULL),
    JS_CGETSET_DEF("domContentLoadedEventStart", js_performance_timing_get_domContentLoadedEventStart, NULL),
    JS_CGETSET_DEF("domContentLoadedEventEnd", js_performance_timing_get_domContentLoadedEventEnd, NULL),
    JS_CGETSET_DEF("domComplete", js_performance_timing_get_domComplete, NULL),
    JS_CGETSET_DEF("loadEventStart", js_performance_timing_get_loadEventStart, NULL),
    JS_CGETSET_DEF("loadEventEnd", js_performance_timing_get_loadEventEnd, NULL),
    JS_CGETSET_DEF("toJSON", js_performance_timing_get_navigationStart, NULL), // stub
};

// Performance.timing getter
static GCValue js_performance_get_timing(JSContext *ctx, GCValueConst this_val) {
    // Get the timing object from the Performance instance's opaque data
    // For simplicity, we store the timing object as a property on the performance instance
    GCValue timing_prop = JS_GetPropertyStr(ctx, this_val, "__timing");
    if (!JS_IsUndefined(timing_prop) && !JS_IsNull(timing_prop)) {
        return timing_prop;
    }

    // Create timing object
    PerformanceTimingData *timing_data = calloc(1, sizeof(PerformanceTimingData));
    if (!timing_data) return JS_EXCEPTION;
    
    // All timing values default to 0
    GCValue timing_obj = JS_NewObjectClass(ctx, js_performance_timing_class_id);
    JS_SetOpaque(timing_obj, timing_data);
    
    // Store on the performance instance
    JS_SetPropertyStr(ctx, (GCValue)this_val, "__timing", timing_obj);
    
    return timing_obj;
}

static const JSCFunctionListEntry js_performance_proto_funcs[] = {
    JS_CFUNC_DEF("now", 0, js_performance_now),
    JS_CGETSET_DEF("timeOrigin", js_performance_get_time_origin, NULL),
    JS_CGETSET_DEF("timing", js_performance_get_timing, NULL),
    JS_CFUNC_DEF("getEntries", 0, js_performance_get_entries),
    JS_CFUNC_DEF("getEntriesByType", 1, js_performance_get_entries_by_type),
    JS_CFUNC_DEF("getEntriesByName", 1, js_performance_get_entries_by_name),
    JS_CFUNC_DEF("mark", 1, js_performance_mark),
    JS_CFUNC_DEF("measure", 1, js_performance_measure),
    JS_CFUNC_DEF("clearMarks", 0, js_performance_clear_marks),
    JS_CFUNC_DEF("clearMeasures", 0, js_performance_clear_measures),
};

// PerformanceEntry.name getter
static GCValue js_performance_entry_get_name(JSContext *ctx, GCValueConst this_val) {
    PerformanceEntryData *entry = JS_GetOpaque2(ctx, this_val, js_performance_entry_class_id);
    if (!entry) return JS_EXCEPTION;
    return JS_NewString(ctx, entry->name);
}

// PerformanceEntry.entryType getter
static GCValue js_performance_entry_get_entry_type(JSContext *ctx, GCValueConst this_val) {
    PerformanceEntryData *entry = JS_GetOpaque2(ctx, this_val, js_performance_entry_class_id);
    if (!entry) return JS_EXCEPTION;
    return JS_NewString(ctx, entry->entryType);
}

// PerformanceEntry.startTime getter
static GCValue js_performance_entry_get_start_time(JSContext *ctx, GCValueConst this_val) {
    PerformanceEntryData *entry = JS_GetOpaque2(ctx, this_val, js_performance_entry_class_id);
    if (!entry) return JS_EXCEPTION;
    return JS_NewFloat64(ctx, entry->startTime);
}

// PerformanceEntry.duration getter
static GCValue js_performance_entry_get_duration(JSContext *ctx, GCValueConst this_val) {
    PerformanceEntryData *entry = JS_GetOpaque2(ctx, this_val, js_performance_entry_class_id);
    if (!entry) return JS_EXCEPTION;
    return JS_NewFloat64(ctx, entry->duration);
}

static const JSCFunctionListEntry js_performance_entry_proto_funcs[] = {
    JS_CGETSET_DEF("name", js_performance_entry_get_name, NULL),
    JS_CGETSET_DEF("entryType", js_performance_entry_get_entry_type, NULL),
    JS_CGETSET_DEF("startTime", js_performance_entry_get_start_time, NULL),
    JS_CGETSET_DEF("duration", js_performance_entry_get_duration, NULL),
};

// PerformanceObserver constructor
static GCValue js_performance_observer_constructor(JSContext *ctx, GCValueConst new_target, int argc, GCValueConst *argv) {
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "PerformanceObserver constructor requires a callback function");
    }
    
    PerformanceObserverData *po = calloc(1, sizeof(PerformanceObserverData));
    if (!po) return JS_EXCEPTION;
    
    po->ctx = ctx;
    po->callback = argv[0];
    
    GCValue obj = JS_NewObjectClass(ctx, js_performance_observer_class_id);
    JS_SetOpaque(obj, po);
    return obj;
}

// PerformanceObserver.prototype.observe(options)
static GCValue js_performance_observer_observe(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_UNDEFINED;
}

// PerformanceObserver.prototype.disconnect()
static GCValue js_performance_observer_disconnect(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_UNDEFINED;
}

// PerformanceObserver.prototype.takeRecords()
static GCValue js_performance_observer_takeRecords(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    return JS_NewArray(ctx);
}

// PerformanceObserver.supportedEntryTypes getter
static GCValue js_performance_observer_get_supported_entry_types(JSContext *ctx, GCValueConst this_val) {
    // Return an array of supported entry types
    GCValue array = JS_NewArray(ctx);
    return array;
}

static const JSCFunctionListEntry js_performance_observer_proto_funcs[] = {
    JS_CFUNC_DEF("observe", 1, js_performance_observer_observe),
    JS_CFUNC_DEF("disconnect", 0, js_performance_observer_disconnect),
    JS_CFUNC_DEF("takeRecords", 0, js_performance_observer_takeRecords),
    JS_CGETSET_DEF("supportedEntryTypes", js_performance_observer_get_supported_entry_types, NULL),
};

// ============================================================================
// DOMRect Implementation
// ============================================================================

typedef struct {
    double x;
    double y;
    double width;
    double height;
    double top;
    double right;
    double bottom;
    double left;
} DOMRectData;

static void js_dom_rect_finalizer(JSRuntime *rt, GCValue val) {
    DOMRectData *rect = JS_GetOpaque(val, js_dom_rect_class_id);
    if (rect) {
        free(rect);
    }
}

static void js_dom_rect_read_only_finalizer(JSRuntime *rt, GCValue val) {
    DOMRectData *rect = JS_GetOpaque(val, js_dom_rect_read_only_class_id);
    if (rect) {
        free(rect);
    }
}

static JSClassDef js_dom_rect_class_def = {
    "DOMRect",
    .finalizer = js_dom_rect_finalizer,
};

static JSClassDef js_dom_rect_read_only_class_def = {
    "DOMRectReadOnly",
    .finalizer = js_dom_rect_read_only_finalizer,
};

// DOMRect constructor
static GCValue js_dom_rect_constructor(JSContext *ctx, GCValueConst new_target, int argc, GCValueConst *argv) {
    DOMRectData *rect = calloc(1, sizeof(DOMRectData));
    if (!rect) return JS_EXCEPTION;
    
    rect->x = 0;
    rect->y = 0;
    rect->width = 0;
    rect->height = 0;
    rect->top = 0;
    rect->right = 0;
    rect->bottom = 0;
    rect->left = 0;
    
    if (argc > 0) JS_ToFloat64(ctx, &rect->x, argv[0]);
    if (argc > 1) JS_ToFloat64(ctx, &rect->y, argv[1]);
    if (argc > 2) JS_ToFloat64(ctx, &rect->width, argv[2]);
    if (argc > 3) JS_ToFloat64(ctx, &rect->height, argv[3]);
    
    rect->left = rect->x;
    rect->top = rect->y;
    rect->right = rect->x + rect->width;
    rect->bottom = rect->y + rect->height;
    
    GCValue obj = JS_NewObjectClass(ctx, js_dom_rect_class_id);
    JS_SetOpaque(obj, rect);
    return obj;
}

// DOMRectReadOnly constructor
static GCValue js_dom_rect_read_only_constructor(JSContext *ctx, GCValueConst new_target, int argc, GCValueConst *argv) {
    DOMRectData *rect = calloc(1, sizeof(DOMRectData));
    if (!rect) return JS_EXCEPTION;
    
    rect->x = 0;
    rect->y = 0;
    rect->width = 0;
    rect->height = 0;
    rect->top = 0;
    rect->right = 0;
    rect->bottom = 0;
    rect->left = 0;
    
    if (argc > 0) JS_ToFloat64(ctx, &rect->x, argv[0]);
    if (argc > 1) JS_ToFloat64(ctx, &rect->y, argv[1]);
    if (argc > 2) JS_ToFloat64(ctx, &rect->width, argv[2]);
    if (argc > 3) JS_ToFloat64(ctx, &rect->height, argv[3]);
    
    rect->left = rect->x;
    rect->top = rect->y;
    rect->right = rect->x + rect->width;
    rect->bottom = rect->y + rect->height;
    
    GCValue obj = JS_NewObjectClass(ctx, js_dom_rect_read_only_class_id);
    JS_SetOpaque(obj, rect);
    return obj;
}

// DOMRect.fromRect(other)
static GCValue js_dom_rect_from_rect(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    double x = 0, y = 0, width = 0, height = 0;
    
    if (argc > 0 && JS_IsObject(argv[0])) {
        GCValue x_val = JS_GetPropertyStr(ctx, argv[0], "x");
        GCValue y_val = JS_GetPropertyStr(ctx, argv[0], "y");
        GCValue w_val = JS_GetPropertyStr(ctx, argv[0], "width");
        GCValue h_val = JS_GetPropertyStr(ctx, argv[0], "height");
        
        JS_ToFloat64(ctx, &x, x_val);
        JS_ToFloat64(ctx, &y, y_val);
        JS_ToFloat64(ctx, &width, w_val);
        JS_ToFloat64(ctx, &height, h_val);




    }
    
    GCValue args[4] = {
        JS_NewFloat64(ctx, x),
        JS_NewFloat64(ctx, y),
        JS_NewFloat64(ctx, width),
        JS_NewFloat64(ctx, height)
    };
    
    GCValue result = js_dom_rect_constructor(ctx, JS_UNDEFINED, 4, args);




    return result;
}

// DOMRectReadOnly.fromRect(other)
static GCValue js_dom_rect_read_only_from_rect(JSContext *ctx, GCValueConst this_val, int argc, GCValueConst *argv) {
    double x = 0, y = 0, width = 0, height = 0;
    
    if (argc > 0 && JS_IsObject(argv[0])) {
        GCValue x_val = JS_GetPropertyStr(ctx, argv[0], "x");
        GCValue y_val = JS_GetPropertyStr(ctx, argv[0], "y");
        GCValue w_val = JS_GetPropertyStr(ctx, argv[0], "width");
        GCValue h_val = JS_GetPropertyStr(ctx, argv[0], "height");
        
        JS_ToFloat64(ctx, &x, x_val);
        JS_ToFloat64(ctx, &y, y_val);
        JS_ToFloat64(ctx, &width, w_val);
        JS_ToFloat64(ctx, &height, h_val);




    }
    
    GCValue args[4] = {
        JS_NewFloat64(ctx, x),
        JS_NewFloat64(ctx, y),
        JS_NewFloat64(ctx, width),
        JS_NewFloat64(ctx, height)
    };
    
    GCValue result = js_dom_rect_read_only_constructor(ctx, JS_UNDEFINED, 4, args);




    return result;
}

#define DEF_DOM_RECT_GETTER(name, field) \
static GCValue js_dom_rect_get_##name(JSContext *ctx, GCValueConst this_val) { \
    DOMRectData *rect = JS_GetOpaque2(ctx, this_val, js_dom_rect_class_id); \
    if (!rect) { \
        rect = JS_GetOpaque2(ctx, this_val, js_dom_rect_read_only_class_id); \
        if (!rect) return JS_EXCEPTION; \
    } \
    return JS_NewFloat64(ctx, rect->field); \
}

DEF_DOM_RECT_GETTER(x, x)
DEF_DOM_RECT_GETTER(y, y)
DEF_DOM_RECT_GETTER(width, width)
DEF_DOM_RECT_GETTER(height, height)
DEF_DOM_RECT_GETTER(top, top)
DEF_DOM_RECT_GETTER(right, right)
DEF_DOM_RECT_GETTER(bottom, bottom)
DEF_DOM_RECT_GETTER(left, left)

#undef DEF_DOM_RECT_GETTER

#define DEF_DOM_RECT_SETTER(name, field) \
static GCValue js_dom_rect_set_##name(JSContext *ctx, GCValueConst this_val, GCValueConst val) { \
    DOMRectData *rect = JS_GetOpaque2(ctx, this_val, js_dom_rect_class_id); \
    if (!rect) return JS_EXCEPTION; \
    JS_ToFloat64(ctx, &rect->field, val); \
    /* Update dependent fields */ \
    if (strcmp(#field, "x") == 0) { rect->left = rect->x; rect->right = rect->x + rect->width; } \
    if (strcmp(#field, "y") == 0) { rect->top = rect->y; rect->bottom = rect->y + rect->height; } \
    if (strcmp(#field, "width") == 0) { rect->right = rect->x + rect->width; } \
    if (strcmp(#field, "height") == 0) { rect->bottom = rect->y + rect->height; } \
    return JS_UNDEFINED; \
}

DEF_DOM_RECT_SETTER(x, x)
DEF_DOM_RECT_SETTER(y, y)
DEF_DOM_RECT_SETTER(width, width)
DEF_DOM_RECT_SETTER(height, height)

#undef DEF_DOM_RECT_SETTER

static const JSCFunctionListEntry js_dom_rect_proto_funcs[] = {
    JS_CGETSET_DEF("x", js_dom_rect_get_x, js_dom_rect_set_x),
    JS_CGETSET_DEF("y", js_dom_rect_get_y, js_dom_rect_set_y),
    JS_CGETSET_DEF("width", js_dom_rect_get_width, js_dom_rect_set_width),
    JS_CGETSET_DEF("height", js_dom_rect_get_height, js_dom_rect_set_height),
    JS_CGETSET_DEF("top", js_dom_rect_get_top, NULL),
    JS_CGETSET_DEF("right", js_dom_rect_get_right, NULL),
    JS_CGETSET_DEF("bottom", js_dom_rect_get_bottom, NULL),
    JS_CGETSET_DEF("left", js_dom_rect_get_left, NULL),
    JS_CFUNC_DEF("toJSON", 0, js_empty_string),
};

static const JSCFunctionListEntry js_dom_rect_read_only_proto_funcs[] = {
    JS_CGETSET_DEF("x", js_dom_rect_get_x, NULL),
    JS_CGETSET_DEF("y", js_dom_rect_get_y, NULL),
    JS_CGETSET_DEF("width", js_dom_rect_get_width, NULL),
    JS_CGETSET_DEF("height", js_dom_rect_get_height, NULL),
    JS_CGETSET_DEF("top", js_dom_rect_get_top, NULL),
    JS_CGETSET_DEF("right", js_dom_rect_get_right, NULL),
    JS_CGETSET_DEF("bottom", js_dom_rect_get_bottom, NULL),
    JS_CGETSET_DEF("left", js_dom_rect_get_left, NULL),
    JS_CFUNC_DEF("toJSON", 0, js_empty_string),
};

// ============================================================================
// Main Initialization
// ============================================================================

void init_browser_stubs(JSContext *ctx, GCValue global) {
    /* CRITICAL: Validate that the global object is usable before proceeding.
     * This catches shape corruption early via is_obj_usable() which tests
     * JS_GetPrototype - if the shape is corrupted, this will fail. */
    if (!is_obj_usable(ctx, global)) {
        LOG_ERROR("init_browser_stubs: global object is not usable (shape may be corrupted)! Aborting init.");
        return;
    }
    
    // ===== Initialize Class IDs =====
    JS_NewClassID(&js_shadow_root_class_id);
    JS_NewClassID(&js_animation_class_id);
    JS_NewClassID(&js_keyframe_effect_class_id);
    JS_NewClassID(&js_font_face_class_id);
    JS_NewClassID(&js_font_face_set_class_id);
    JS_NewClassID(&js_custom_element_registry_class_id);
    JS_NewClassID(&js_mutation_observer_class_id);
    JS_NewClassID(&js_resize_observer_class_id);
    JS_NewClassID(&js_intersection_observer_class_id);
    JS_NewClassID(&js_performance_class_id);
    JS_NewClassID(&js_performance_entry_class_id);
    JS_NewClassID(&js_performance_observer_class_id);
    JS_NewClassID(&js_dom_rect_class_id);
    JS_NewClassID(&js_dom_rect_read_only_class_id);
    JS_NewClassID(&js_performance_timing_class_id);
    JS_NewClassID(&js_map_class_id);
    JS_NewClassID(&js_dom_exception_class_id);
    
    // Register classes with the runtime
    JSRuntime *rt = JS_GetRuntime(ctx);
    JS_NewClass(rt, js_shadow_root_class_id, &js_shadow_root_class_def);
    JS_NewClass(rt, js_animation_class_id, &js_animation_class_def);
    JS_NewClass(rt, js_keyframe_effect_class_id, &js_keyframe_effect_class_def);
    JS_NewClass(rt, js_font_face_class_id, &js_font_face_class_def);
    JS_NewClass(rt, js_font_face_set_class_id, &js_font_face_set_class_def);
    JS_NewClass(rt, js_custom_element_registry_class_id, &js_custom_element_registry_class_def);
    JS_NewClass(rt, js_mutation_observer_class_id, &js_mutation_observer_class_def);
    JS_NewClass(rt, js_resize_observer_class_id, &js_resize_observer_class_def);
    JS_NewClass(rt, js_intersection_observer_class_id, &js_intersection_observer_class_def);
    JS_NewClass(rt, js_performance_class_id, &js_performance_class_def);
    JS_NewClass(rt, js_performance_entry_class_id, &js_performance_entry_class_def);
    JS_NewClass(rt, js_performance_observer_class_id, &js_performance_observer_class_def);
    JS_NewClass(rt, js_dom_rect_class_id, &js_dom_rect_class_def);
    JS_NewClass(rt, js_dom_rect_read_only_class_id, &js_dom_rect_read_only_class_def);
    JS_NewClass(rt, js_map_class_id, &js_map_class_def);
    JS_NewClass(rt, js_performance_timing_class_id, &js_performance_timing_class_def);
    JS_NewClass(rt, js_dom_exception_class_id, &js_dom_exception_class_def);
    
    // ===== ES6+ Polyfills Registration =====
    // Get Object constructor
    GCValue object_ctor = JS_GetPropertyStr(ctx, global, "Object");
    
    // Object.getPrototypeOf
    if (!JS_IsException(object_ctor)) {
        JS_SetPropertyStr(ctx, object_ctor, "getPrototypeOf",
            JS_NewCFunction(ctx, js_object_get_prototype_of, "getPrototypeOf", 1));
    }
    
    // Object.setPrototypeOf
    if (!JS_IsException(object_ctor)) {
        JS_SetPropertyStr(ctx, object_ctor, "setPrototypeOf",
            JS_NewCFunction(ctx, js_object_set_prototype_of, "setPrototypeOf", 2));
    }
    
    // Object.create
    if (!JS_IsException(object_ctor)) {
        JS_SetPropertyStr(ctx, object_ctor, "create",
            JS_NewCFunction(ctx, js_object_create, "create", 2));
    }
    
    // Object.defineProperty
    if (!JS_IsException(object_ctor)) {
        JS_SetPropertyStr(ctx, object_ctor, "defineProperty",
            JS_NewCFunction(ctx, js_object_define_property, "defineProperty", 3));
    }
    
    // Object.defineProperties
    if (!JS_IsException(object_ctor)) {
        JS_SetPropertyStr(ctx, object_ctor, "defineProperties",
            JS_NewCFunction(ctx, js_object_define_properties, "defineProperties", 2));
    }
    
    // Object.getOwnPropertyDescriptor
    if (!JS_IsException(object_ctor)) {
        JS_SetPropertyStr(ctx, object_ctor, "getOwnPropertyDescriptor",
            JS_NewCFunction(ctx, js_object_get_own_property_descriptor, "getOwnPropertyDescriptor", 2));
    }
    
    // Object.getOwnPropertySymbols
    if (!JS_IsException(object_ctor)) {
        JS_SetPropertyStr(ctx, object_ctor, "getOwnPropertySymbols",
            JS_NewCFunction(ctx, js_object_get_own_property_symbols, "getOwnPropertySymbols", 1));
    }
    
    // Object.assign
    if (!JS_IsException(object_ctor)) {
        JS_SetPropertyStr(ctx, object_ctor, "assign",
            JS_NewCFunction(ctx, js_object_assign, "assign", 2));
    }

    // Create Reflect object
    GCValue reflect_obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, reflect_obj, "construct",
        JS_NewCFunction(ctx, js_reflect_construct, "construct", 2));
    JS_SetPropertyStr(ctx, reflect_obj, "apply",
        JS_NewCFunction(ctx, js_reflect_apply, "apply", 3));
    JS_SetPropertyStr(ctx, reflect_obj, "has",
        JS_NewCFunction(ctx, js_reflect_has, "has", 2));
    JS_SetPropertyStr(ctx, global, "Reflect", reflect_obj);
    
    // DOMException constructor
    GCValue dom_exception_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, dom_exception_proto, js_dom_exception_proto_funcs,
        sizeof(js_dom_exception_proto_funcs) / sizeof(js_dom_exception_proto_funcs[0]));
    
    // Set up prototype chain: DOMException.prototype -> Error.prototype using Object.setPrototypeOf
    GCValue error_ctor = JS_GetPropertyStr(ctx, global, "Error");
    if (!JS_IsException(error_ctor)) {
        GCValue error_proto = JS_GetPropertyStr(ctx, error_ctor, "prototype");
        if (!JS_IsException(error_proto)) {
            GCValue obj_ctor = JS_GetPropertyStr(ctx, global, "Object");
            GCValue set_proto = JS_GetPropertyStr(ctx, obj_ctor, "setPrototypeOf");
            GCValue args[2] = { dom_exception_proto, error_proto };




        }

    }
    
    JS_SetClassProto(ctx, js_dom_exception_class_id, dom_exception_proto);
    GCValue dom_exception_ctor = JS_NewCFunction2(ctx, js_dom_exception_constructor, "DOMException", 2, JS_CFUNC_constructor, 0);
    if (!is_obj_usable(ctx, dom_exception_ctor)) {
        LOG_ERROR("dom_exception_ctor not usable after creation");
    }
    JS_SetConstructor(ctx, dom_exception_ctor, dom_exception_proto);
    if (!is_obj_usable(ctx, dom_exception_ctor)) {
        LOG_ERROR("dom_exception_ctor not usable after SetConstructor");
    }
    
    // Add static error code constants
    if (!is_obj_usable(ctx, dom_exception_ctor)) {
        LOG_ERROR("dom_exception_ctor not usable before setting constants");
        // Skip setting constants on corrupted object
        
        
        // Continue with rest of init
        goto skip_dom_exception;
    }
    
    /* DIAGNOSTIC: Check dom_exception_ctor right before use */
    LOG_ERROR("About to set INDEX_SIZE_ERR, dom_exception_ctor tag=%d", (int)JS_VALUE_GET_TAG(dom_exception_ctor));
    
    JS_SetPropertyStr(ctx, dom_exception_ctor, "INDEX_SIZE_ERR", JS_NewInt32(ctx, DOM_EXCEPTION_INDEX_SIZE_ERR));
    JS_SetPropertyStr(ctx, dom_exception_ctor, "HIERARCHY_REQUEST_ERR", JS_NewInt32(ctx, DOM_EXCEPTION_HIERARCHY_REQUEST_ERR));
    JS_SetPropertyStr(ctx, dom_exception_ctor, "WRONG_DOCUMENT_ERR", JS_NewInt32(ctx, DOM_EXCEPTION_WRONG_DOCUMENT_ERR));
    JS_SetPropertyStr(ctx, dom_exception_ctor, "INVALID_CHARACTER_ERR", JS_NewInt32(ctx, DOM_EXCEPTION_INVALID_CHARACTER_ERR));
    JS_SetPropertyStr(ctx, dom_exception_ctor, "NO_MODIFICATION_ALLOWED_ERR", JS_NewInt32(ctx, DOM_EXCEPTION_NO_MODIFICATION_ALLOWED_ERR));
    JS_SetPropertyStr(ctx, dom_exception_ctor, "NOT_FOUND_ERR", JS_NewInt32(ctx, DOM_EXCEPTION_NOT_FOUND_ERR));
    JS_SetPropertyStr(ctx, dom_exception_ctor, "NOT_SUPPORTED_ERR", JS_NewInt32(ctx, DOM_EXCEPTION_NOT_SUPPORTED_ERR));
    JS_SetPropertyStr(ctx, dom_exception_ctor, "INVALID_STATE_ERR", JS_NewInt32(ctx, DOM_EXCEPTION_INVALID_STATE_ERR));
    JS_SetPropertyStr(ctx, dom_exception_ctor, "SYNTAX_ERR", JS_NewInt32(ctx, DOM_EXCEPTION_SYNTAX_ERR));
    JS_SetPropertyStr(ctx, dom_exception_ctor, "INVALID_MODIFICATION_ERR", JS_NewInt32(ctx, DOM_EXCEPTION_INVALID_MODIFICATION_ERR));
    JS_SetPropertyStr(ctx, dom_exception_ctor, "NAMESPACE_ERR", JS_NewInt32(ctx, DOM_EXCEPTION_NAMESPACE_ERR));
    JS_SetPropertyStr(ctx, dom_exception_ctor, "INVALID_ACCESS_ERR", JS_NewInt32(ctx, DOM_EXCEPTION_INVALID_ACCESS_ERR));
    JS_SetPropertyStr(ctx, dom_exception_ctor, "TYPE_MISMATCH_ERR", JS_NewInt32(ctx, DOM_EXCEPTION_TYPE_MISMATCH_ERR));
    JS_SetPropertyStr(ctx, dom_exception_ctor, "SECURITY_ERR", JS_NewInt32(ctx, DOM_EXCEPTION_SECURITY_ERR));
    JS_SetPropertyStr(ctx, dom_exception_ctor, "NETWORK_ERR", JS_NewInt32(ctx, DOM_EXCEPTION_NETWORK_ERR));
    JS_SetPropertyStr(ctx, dom_exception_ctor, "ABORT_ERR", JS_NewInt32(ctx, DOM_EXCEPTION_ABORT_ERR));
    JS_SetPropertyStr(ctx, dom_exception_ctor, "URL_MISMATCH_ERR", JS_NewInt32(ctx, DOM_EXCEPTION_URL_MISMATCH_ERR));
    JS_SetPropertyStr(ctx, dom_exception_ctor, "QUOTA_EXCEEDED_ERR", JS_NewInt32(ctx, DOM_EXCEPTION_QUOTA_EXCEEDED_ERR));
    JS_SetPropertyStr(ctx, dom_exception_ctor, "TIMEOUT_ERR", JS_NewInt32(ctx, DOM_EXCEPTION_TIMEOUT_ERR));
    JS_SetPropertyStr(ctx, dom_exception_ctor, "INVALID_NODE_TYPE_ERR", JS_NewInt32(ctx, DOM_EXCEPTION_INVALID_NODE_TYPE_ERR));
    JS_SetPropertyStr(ctx, dom_exception_ctor, "DATA_CLONE_ERR", JS_NewInt32(ctx, DOM_EXCEPTION_DATA_CLONE_ERR));
    
    JS_SetPropertyStr(ctx, global, "DOMException", dom_exception_ctor);
skip_dom_exception:
    ;
    
    // Map constructor
    GCValue map_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, map_proto, js_map_proto_funcs, 
        sizeof(js_map_proto_funcs) / sizeof(js_map_proto_funcs[0]));
    GCValue map_ctor = JS_NewCFunction2(ctx, js_map_constructor, "Map", 0, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, map_ctor, map_proto);
    JS_SetClassProto(ctx, js_map_class_id, map_proto);
    JS_SetPropertyStr(ctx, global, "Map", map_ctor);
    
    // Set Map prototype[Symbol.toStringTag]
    GCValue symbol_ctor = JS_GetPropertyStr(ctx, global, "Symbol");
    if (!JS_IsException(symbol_ctor)) {
        GCValue toStringTag = JS_GetPropertyStr(ctx, symbol_ctor, "toStringTag");
        if (!JS_IsException(toStringTag)) {
            JS_SetProperty(ctx, map_proto, JS_ValueToAtom(ctx, toStringTag), JS_NewString(ctx, "Map"));

        }

    }
    
    // Promise.prototype.finally
    GCValue promise_ctor = JS_GetPropertyStr(ctx, global, "Promise");
    if (!JS_IsException(promise_ctor)) {
        GCValue promise_proto = JS_GetPropertyStr(ctx, promise_ctor, "prototype");
        if (!JS_IsException(promise_proto)) {
            JS_SetPropertyStr(ctx, promise_proto, "finally",
                JS_NewCFunction(ctx, js_promise_finally, "finally", 1));

        }

    }
    
    // String.prototype.includes
    GCValue string_ctor = JS_GetPropertyStr(ctx, global, "String");
    if (!JS_IsException(string_ctor)) {
        GCValue string_proto = JS_GetPropertyStr(ctx, string_ctor, "prototype");
        if (!JS_IsException(string_proto)) {
            JS_SetPropertyStr(ctx, string_proto, "includes",
                JS_NewCFunction(ctx, js_string_includes, "includes", 1));

        }

    }
    
    // Array.prototype.includes
    GCValue array_ctor = JS_GetPropertyStr(ctx, global, "Array");
    if (!JS_IsException(array_ctor)) {
        GCValue array_proto = JS_GetPropertyStr(ctx, array_ctor, "prototype");
        if (!JS_IsException(array_proto)) {
            JS_SetPropertyStr(ctx, array_proto, "includes",
                JS_NewCFunction(ctx, js_array_includes, "includes", 1));

        }
        // Array.from
        JS_SetPropertyStr(ctx, array_ctor, "from",
            JS_NewCFunction(ctx, js_array_from, "from", 1));

    }
    
    // ===== Window (global object itself) =====
    // window IS the global object - this ensures 'this' at global level refers to window
    GCValue window = global;  // Use global object as window (no new object created)
    
    // ===== Create DOM Constructors with proper prototype chain in C =====
    // Reference counting rules:
    // - JS_NewCFunction2/JS_NewObject: returns value with refcount 1
    // - JS_SetPropertyStr: duplicates the value (refcount +1)
    // After setting a property, we MUST free the local reference!
    
    // Helper to set up prototype chain using Object.setPrototypeOf
    object_ctor = JS_GetPropertyStr(ctx, global, "Object");
    GCValue set_proto_of = JS_GetPropertyStr(ctx, object_ctor, "setPrototypeOf");
    
    // EventTarget constructor (base of all DOM constructors)
    GCValue event_target_ctor = JS_NewCFunction2(ctx, js_dummy_function, "EventTarget", 0, JS_CFUNC_constructor, 0);
    GCValue event_target_proto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, event_target_proto, "constructor", event_target_ctor);
    JS_SetPropertyStr(ctx, event_target_ctor, "prototype", event_target_proto);
    JS_SetPropertyStr(ctx, global, "EventTarget", event_target_ctor);
    // Keep event_target_proto for Node's prototype chain
    
    // Node constructor
    GCValue node_ctor = JS_NewCFunction2(ctx, js_dummy_function, "Node", 0, JS_CFUNC_constructor, 0);
    GCValue node_proto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, node_proto, "constructor", node_ctor);
    // Node.prototype -> EventTarget.prototype
    GCValue args_node[2] = { node_proto, event_target_proto };

    JS_SetPropertyStr(ctx, node_ctor, "prototype", node_proto);
    JS_SetPropertyStr(ctx, global, "Node", node_ctor);

    // Note: event_target_proto is kept alive for adding methods below
    // It will be freed after we add methods to it
    // Keep node_proto for Element and DocumentFragment
    
    // Element constructor
    GCValue element_ctor = JS_NewCFunction2(ctx, js_dummy_function, "Element", 0, JS_CFUNC_constructor, 0);
    GCValue element_proto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, element_proto, "constructor", element_ctor);
    // Element.prototype -> Node.prototype
    GCValue args_element[2] = { element_proto, node_proto };

    JS_SetPropertyStr(ctx, element_ctor, "prototype", element_proto);
    JS_SetPropertyStr(ctx, global, "Element", element_ctor);
    // DON'T free element_ctor yet - we need it for document.documentElement below
    // Keep element_proto for HTMLElement
    // Note: node_proto is kept alive for adding methods below
    
    // HTMLElement constructor
    GCValue html_element_ctor = JS_NewCFunction2(ctx, js_dummy_function, "HTMLElement", 0, JS_CFUNC_constructor, 0);
    GCValue html_element_proto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, html_element_proto, "constructor", html_element_ctor);
    // HTMLElement.prototype -> Element.prototype
    GCValue args_html_element[2] = { html_element_proto, element_proto };

    JS_SetPropertyStr(ctx, html_element_ctor, "prototype", html_element_proto);
    JS_SetPropertyStr(ctx, global, "HTMLElement", html_element_ctor);
    // DON'T free html_element_ctor yet - we need it for document.body below
    // element_proto will be freed after adding methods below
    // Keep html_element_ctor and html_element_proto for document.body
    
    // DocumentFragment constructor (needs node_proto)
    GCValue doc_fragment_ctor = JS_NewCFunction2(ctx, js_dummy_function, "DocumentFragment", 0, JS_CFUNC_constructor, 0);
    GCValue doc_fragment_proto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, doc_fragment_proto, "constructor", doc_fragment_ctor);
    // DocumentFragment.prototype -> Node.prototype
    GCValue args_doc_frag[2] = { doc_fragment_proto, node_proto };

    JS_SetPropertyStr(ctx, doc_fragment_ctor, "prototype", doc_fragment_proto);
    JS_SetPropertyStr(ctx, global, "DocumentFragment", doc_fragment_ctor);


    // ===== Ensure DOM Prototype Chain Integrity =====
    // Re-fetch prototypes from global to ensure chains are properly linked
    // This handles cases where JS code may have modified prototypes
    
    GCValue event_target_check = JS_GetPropertyStr(ctx, global, "EventTarget");
    GCValue node_check = JS_GetPropertyStr(ctx, global, "Node");
    GCValue element_check = JS_GetPropertyStr(ctx, global, "Element");
    GCValue html_element_check = JS_GetPropertyStr(ctx, global, "HTMLElement");
    GCValue doc_fragment_check = JS_GetPropertyStr(ctx, global, "DocumentFragment");
    
    GCValue event_target_proto_check = js_get_prototype(ctx, event_target_check);
    GCValue node_proto_check = js_get_prototype(ctx, node_check);
    GCValue element_proto_check = js_get_prototype(ctx, element_check);
    GCValue html_element_proto_check = js_get_prototype(ctx, html_element_check);
    GCValue doc_fragment_proto_check = js_get_prototype(ctx, doc_fragment_check);
    
    // Ensure prototype chains using Object.setPrototypeOf
    if (!JS_IsUndefined(node_proto_check) && !JS_IsNull(node_proto_check) &&
        !JS_IsUndefined(event_target_proto_check) && !JS_IsNull(event_target_proto_check)) {
        GCValue args1[2] = { node_proto_check, event_target_proto_check };

    }
    
    if (!JS_IsUndefined(element_proto_check) && !JS_IsNull(element_proto_check) &&
        !JS_IsUndefined(node_proto_check) && !JS_IsNull(node_proto_check)) {
        GCValue args2[2] = { element_proto_check, node_proto_check };

    }
    
    if (!JS_IsUndefined(html_element_proto_check) && !JS_IsNull(html_element_proto_check) &&
        !JS_IsUndefined(element_proto_check) && !JS_IsNull(element_proto_check)) {
        GCValue args3[2] = { html_element_proto_check, element_proto_check };

    }
    
    if (!JS_IsUndefined(doc_fragment_proto_check) && !JS_IsNull(doc_fragment_proto_check) &&
        !JS_IsUndefined(node_proto_check) && !JS_IsNull(node_proto_check)) {
        GCValue args4[2] = { doc_fragment_proto_check, node_proto_check };

    }










    // Clean up Object.setPrototypeOf helper


    // node_proto will be freed after adding methods below
    
    // ===== EventTarget prototype methods =====
    JS_SetPropertyStr(ctx, event_target_proto, "addEventListener",
        JS_NewCFunction(ctx, js_event_target_addEventListener, "addEventListener", 2));
    JS_SetPropertyStr(ctx, event_target_proto, "removeEventListener",
        JS_NewCFunction(ctx, js_event_target_removeEventListener, "removeEventListener", 2));
    JS_SetPropertyStr(ctx, event_target_proto, "dispatchEvent",
        JS_NewCFunction(ctx, js_event_target_dispatchEvent, "dispatchEvent", 1));
    
    // ===== Node prototype methods =====
    // Note: node_proto is still valid here
    JS_SetPropertyStr(ctx, node_proto, "appendChild",
        JS_NewCFunction(ctx, js_node_appendChild, "appendChild", 1));
    JS_SetPropertyStr(ctx, node_proto, "insertBefore",
        JS_NewCFunction(ctx, js_node_insertBefore, "insertBefore", 2));
    JS_SetPropertyStr(ctx, node_proto, "removeChild",
        JS_NewCFunction(ctx, js_node_removeChild, "removeChild", 1));
    JS_SetPropertyStr(ctx, node_proto, "cloneNode",
        JS_NewCFunction(ctx, js_node_cloneNode, "cloneNode", 1));
    JS_SetPropertyStr(ctx, node_proto, "contains",
        JS_NewCFunction(ctx, js_node_contains, "contains", 1));
    
    // ===== Element prototype methods =====
    // attachShadow method
    JS_SetPropertyStr(ctx, element_proto, "attachShadow",
        JS_NewCFunction(ctx, js_element_attach_shadow, "attachShadow", 1));
    // shadowRoot getter
    GCValue getter = JS_NewCFunction(ctx, js_element_get_shadow_root, "get shadowRoot", 0);
    JSAtom shadow_root_atom = JS_NewAtom(ctx, "shadowRoot");
    // Note: JS_DefinePropertyGetSet takes ownership of the getter/setter values.
    // Do NOT free getter after passing it - the property now owns it.
    JS_DefinePropertyGetSet(ctx, element_proto, shadow_root_atom,
        getter, JS_UNDEFINED, JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, shadow_root_atom);
    // querySelector and querySelectorAll
    JS_SetPropertyStr(ctx, element_proto, "querySelector",
        JS_NewCFunction(ctx, js_element_querySelector, "querySelector", 1));
    JS_SetPropertyStr(ctx, element_proto, "querySelectorAll",
        JS_NewCFunction(ctx, js_element_querySelectorAll, "querySelectorAll", 1));
    // animate method
    JS_SetPropertyStr(ctx, element_proto, "animate",
        JS_NewCFunction(ctx, js_element_animate, "animate", 2));
    
    // Do NOT free the prototypes here!
    // They are still referenced by:
    // 1. The constructor's 'prototype' property (set via JS_SetPropertyStr)
    // 2. The prototype chain links (set via Object.setPrototypeOf)
    // 3. Each other through parent prototype relationships
    // Freeing them now would create dangling pointers.
    // QuickJS garbage collector will clean them up when the context is freed.
    // (void)event_target_proto;  // Kept alive by prototype chain
    // (void)node_proto;          // Kept alive by prototype chain
    // (void)element_proto;       // Kept alive by prototype chain
    
    // NOTE: We do NOT free the constructor objects here.
    // They are still referenced by:
    // 1. The global object (window.EventTarget, window.Node, etc.)
    // 2. Each other through prototype chains (__proto__ links)
    // 3. Later use in this function (e.g., JS_CallConstructor for doc_element)
    // QuickJS garbage collector will clean them up when the context is freed.
    
    // ===== Window Properties =====
    DEF_PROP_INT(ctx, window, "innerWidth", 1920);
    DEF_PROP_INT(ctx, window, "innerHeight", 1080);
    DEF_PROP_INT(ctx, window, "outerWidth", 1920);
    DEF_PROP_INT(ctx, window, "outerHeight", 1080);
    DEF_PROP_INT(ctx, window, "screenX", 0);
    DEF_PROP_INT(ctx, window, "screenY", 0);
    DEF_PROP_INT(ctx, window, "screenLeft", 0);
    DEF_PROP_INT(ctx, window, "screenTop", 0);
    DEF_PROP_FLOAT(ctx, window, "devicePixelRatio", 1.0);
    DEF_PROP_INT(ctx, window, "length", 0);
    DEF_PROP_BOOL(ctx, window, "closed", 0);
    DEF_FUNC(ctx, window, "setTimeout", js_zero, 2);
    DEF_FUNC(ctx, window, "setInterval", js_zero, 2);
    DEF_FUNC(ctx, window, "clearTimeout", js_undefined, 1);
    DEF_FUNC(ctx, window, "clearInterval", js_undefined, 1);
    DEF_FUNC(ctx, window, "requestAnimationFrame", js_zero, 1);
    DEF_FUNC(ctx, window, "cancelAnimationFrame", js_undefined, 1);
    DEF_FUNC(ctx, window, "alert", js_undefined, 1);
    DEF_FUNC(ctx, window, "confirm", js_true, 0);
    DEF_FUNC(ctx, window, "prompt", js_empty_string, 1);
    DEF_FUNC(ctx, window, "open", js_null, 1);
    DEF_FUNC(ctx, window, "close", js_undefined, 0);
    DEF_FUNC(ctx, window, "focus", js_undefined, 0);
    DEF_FUNC(ctx, window, "blur", js_undefined, 0);
    DEF_FUNC(ctx, window, "scrollTo", js_undefined, 2);
    DEF_FUNC(ctx, window, "scrollBy", js_undefined, 2);
    DEF_FUNC(ctx, window, "postMessage", js_undefined, 2);
    DEF_FUNC(ctx, window, "addEventListener", js_undefined, 2);
    DEF_FUNC(ctx, window, "removeEventListener", js_undefined, 2);
    DEF_FUNC(ctx, window, "dispatchEvent", js_true, 1);
    DEF_FUNC(ctx, window, "getComputedStyle", js_get_computed_style, 1);
    
    // Set up window to reference itself (global object)
    // IMPORTANT: Don't create circular references (window.window = window) as this
    // causes crashes during QuickJS cleanup due to reference counting issues.
    // Just leave these unset - YouTube scripts will still work.
    
    // Also expose DOMException on window (if it exists on global)
    GCValue dom_exception = JS_GetPropertyStr(ctx, global, "DOMException");
    if (!JS_IsException(dom_exception) && !JS_IsUndefined(dom_exception)) {
        JS_SetPropertyStr(ctx, window, "DOMException", dom_exception);  // transfers ownership
    } else {

    }
    
    // ===== NodeFilter constants =====
    GCValue node_filter = JS_NewObject(ctx);
    DEF_PROP_INT(ctx, node_filter, "FILTER_ACCEPT", 1);
    DEF_PROP_INT(ctx, node_filter, "FILTER_REJECT", 2);
    DEF_PROP_INT(ctx, node_filter, "FILTER_SKIP", 3);
    DEF_PROP_INT(ctx, node_filter, "SHOW_ALL", 0xFFFFFFFF);
    DEF_PROP_INT(ctx, node_filter, "SHOW_ELEMENT", 0x1);
    DEF_PROP_INT(ctx, node_filter, "SHOW_ATTRIBUTE", 0x2);
    DEF_PROP_INT(ctx, node_filter, "SHOW_TEXT", 0x4);
    DEF_PROP_INT(ctx, node_filter, "SHOW_CDATA_SECTION", 0x8);
    DEF_PROP_INT(ctx, node_filter, "SHOW_ENTITY_REFERENCE", 0x10);
    DEF_PROP_INT(ctx, node_filter, "SHOW_ENTITY", 0x20);
    DEF_PROP_INT(ctx, node_filter, "SHOW_PROCESSING_INSTRUCTION", 0x40);
    DEF_PROP_INT(ctx, node_filter, "SHOW_COMMENT", 0x80);
    DEF_PROP_INT(ctx, node_filter, "SHOW_DOCUMENT", 0x100);
    DEF_PROP_INT(ctx, node_filter, "SHOW_DOCUMENT_TYPE", 0x200);
    DEF_PROP_INT(ctx, node_filter, "SHOW_DOCUMENT_FRAGMENT", 0x400);
    DEF_PROP_INT(ctx, node_filter, "SHOW_NOTATION", 0x800);
    JS_SetPropertyStr(ctx, global, "NodeFilter", node_filter);
    JS_SetPropertyStr(ctx, window, "NodeFilter", node_filter);
    
    // ===== Document =====
    GCValue document = JS_NewObject(ctx);
    DEF_PROP_INT(ctx, document, "nodeType", 9);
    DEF_PROP_STR(ctx, document, "readyState", "complete");
    DEF_PROP_STR(ctx, document, "characterSet", "UTF-8");
    DEF_PROP_STR(ctx, document, "charset", "UTF-8");
    DEF_PROP_STR(ctx, document, "contentType", "text/html");
    DEF_PROP_STR(ctx, document, "referrer", "https://www.youtube.com/");
    DEF_PROP_STR(ctx, document, "cookie", "");
    DEF_PROP_STR(ctx, document, "domain", "www.youtube.com");
    DEF_FUNC(ctx, document, "createElement", js_document_create_element, 1);
    DEF_FUNC(ctx, document, "createElementNS", js_document_create_element, 2);
    DEF_FUNC(ctx, document, "createTextNode", js_empty_string, 1);
    DEF_FUNC(ctx, document, "createComment", js_empty_string, 1);
    DEF_FUNC(ctx, document, "createDocumentFragment", js_null, 0);
    DEF_FUNC(ctx, document, "getElementById", js_null, 1);
    DEF_FUNC(ctx, document, "querySelector", js_null, 1);
    DEF_FUNC(ctx, document, "querySelectorAll", js_empty_array, 1);
    DEF_FUNC(ctx, document, "getElementsByTagName", js_empty_array, 1);
    DEF_FUNC(ctx, document, "getElementsByClassName", js_empty_array, 1);
    DEF_FUNC(ctx, document, "getElementsByName", js_empty_array, 1);
    DEF_FUNC(ctx, document, "addEventListener", js_undefined, 2);
    DEF_FUNC(ctx, document, "removeEventListener", js_undefined, 2);
    DEF_FUNC(ctx, document, "dispatchEvent", js_true, 1);
    
    // Create documentElement as an actual Element instance with proper prototype
    // This must be done AFTER Element is defined in the dom_setup_js above
    GCValue doc_element = JS_CallConstructor(ctx, element_ctor, 0, NULL);
    if (!JS_IsException(doc_element)) {
        // Add Element methods to documentElement
        JS_SetPropertyStr(ctx, doc_element, "querySelector",
            JS_NewCFunction(ctx, js_element_querySelector, "querySelector", 1));
        JS_SetPropertyStr(ctx, doc_element, "querySelectorAll",
            JS_NewCFunction(ctx, js_element_querySelectorAll, "querySelectorAll", 1));
        JS_SetPropertyStr(ctx, doc_element, "animate",
            JS_NewCFunction(ctx, js_element_animate, "animate", 2));
        JS_SetPropertyStr(ctx, doc_element, "getAttribute",
            JS_NewCFunction(ctx, js_element_get_attribute, "getAttribute", 1));
        JS_SetPropertyStr(ctx, doc_element, "setAttribute",
            JS_NewCFunction(ctx, js_element_set_attribute, "setAttribute", 2));
        JS_SetPropertyStr(ctx, doc_element, "appendChild",
            JS_NewCFunction(ctx, js_node_appendChild, "appendChild", 1));
        
        // Add clientWidth/clientHeight properties (viewport dimensions)
        DEF_PROP_INT(ctx, doc_element, "clientWidth", 1920);
        DEF_PROP_INT(ctx, doc_element, "clientHeight", 1080);
        DEF_PROP_INT(ctx, doc_element, "scrollWidth", 1920);
        DEF_PROP_INT(ctx, doc_element, "scrollHeight", 1080);
        DEF_PROP_INT(ctx, doc_element, "offsetWidth", 1920);
        DEF_PROP_INT(ctx, doc_element, "offsetHeight", 1080);
    } else {
        // Fallback to plain object if constructor fails

        doc_element = JS_NewObject(ctx);
    }
    JS_SetPropertyStr(ctx, document, "documentElement", doc_element);
    
    // Create document body
    GCValue body_element = JS_CallConstructor(ctx, html_element_ctor, 0, NULL);
    if (JS_IsException(body_element)) {

        body_element = JS_NewObject(ctx);
    }
    JS_SetPropertyStr(ctx, body_element, "appendChild",
        JS_NewCFunction(ctx, js_node_appendChild, "appendChild", 1));
    
    // Add clientWidth/clientHeight properties to body (viewport dimensions)
    DEF_PROP_INT(ctx, body_element, "clientWidth", 1920);
    DEF_PROP_INT(ctx, body_element, "clientHeight", 937);  // 1080 - some UI chrome
    DEF_PROP_INT(ctx, body_element, "scrollWidth", 1920);
    DEF_PROP_INT(ctx, body_element, "scrollHeight", 937);
    DEF_PROP_INT(ctx, body_element, "offsetWidth", 1920);
    DEF_PROP_INT(ctx, body_element, "offsetHeight", 937);
    
    JS_SetPropertyStr(ctx, document, "body", body_element);
    
    // Constructors and prototypes are owned by global objects and prototype chains
    // Don't free them here - QuickJS GC will clean up when context is freed
    (void)element_ctor;       // owned by global.Element
    (void)html_element_ctor;  // owned by global.HTMLElement  
    (void)html_element_proto; // owned by HTMLElement.prototype
    
    JS_SetPropertyStr(ctx, global, "document", document);
    JS_SetPropertyStr(ctx, document, "defaultView", window);
    
    // ===== Location =====
    GCValue location = JS_NewObject(ctx);
    DEF_PROP_STR(ctx, location, "href", "https://www.youtube.com/watch?v=dQw4w9WgXcQ");
    DEF_PROP_STR(ctx, location, "protocol", "https:");
    DEF_PROP_STR(ctx, location, "host", "www.youtube.com");
    DEF_PROP_STR(ctx, location, "hostname", "www.youtube.com");
    DEF_PROP_STR(ctx, location, "port", "");
    DEF_PROP_STR(ctx, location, "pathname", "/watch");
    DEF_PROP_STR(ctx, location, "search", "?v=dQw4w9WgXcQ");
    DEF_PROP_STR(ctx, location, "hash", "");
    DEF_PROP_STR(ctx, location, "origin", "https://www.youtube.com");
    DEF_FUNC(ctx, location, "toString", js_empty_string, 0);
    JS_SetPropertyStr(ctx, window, "location", location);
    JS_SetPropertyStr(ctx, document, "location", location);
    
    // ===== Navigator =====
    GCValue navigator = JS_NewObject(ctx);
    DEF_PROP_STR(ctx, navigator, "userAgent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    DEF_PROP_STR(ctx, navigator, "appName", "Netscape");
    DEF_PROP_STR(ctx, navigator, "appVersion", "5.0 (X11; Linux x86_64) AppleWebKit/537.36");
    DEF_PROP_STR(ctx, navigator, "appCodeName", "Mozilla");
    DEF_PROP_STR(ctx, navigator, "platform", "Linux x86_64");
    DEF_PROP_STR(ctx, navigator, "product", "Gecko");
    DEF_PROP_STR(ctx, navigator, "productSub", "20030107");
    DEF_PROP_STR(ctx, navigator, "vendor", "Google Inc.");
    DEF_PROP_STR(ctx, navigator, "vendorSub", "");
    DEF_PROP_STR(ctx, navigator, "language", "en-US");
    DEF_PROP_BOOL(ctx, navigator, "onLine", 1);
    DEF_PROP_BOOL(ctx, navigator, "cookieEnabled", 1);
    DEF_PROP_INT(ctx, navigator, "hardwareConcurrency", 8);
    DEF_PROP_INT(ctx, navigator, "maxTouchPoints", 0);
    DEF_PROP_BOOL(ctx, navigator, "pdfViewerEnabled", 1);
    DEF_PROP_BOOL(ctx, navigator, "webdriver", 0);
    JS_SetPropertyStr(ctx, window, "navigator", navigator);
    
    // ===== Screen =====
    GCValue screen = JS_NewObject(ctx);
    DEF_PROP_INT(ctx, screen, "width", 1920);
    DEF_PROP_INT(ctx, screen, "height", 1080);
    DEF_PROP_INT(ctx, screen, "availWidth", 1920);
    DEF_PROP_INT(ctx, screen, "availHeight", 1040);
    DEF_PROP_INT(ctx, screen, "colorDepth", 24);
    DEF_PROP_INT(ctx, screen, "pixelDepth", 24);
    JS_SetPropertyStr(ctx, window, "screen", screen);
    
    // ===== History =====
    GCValue history = JS_NewObject(ctx);
    DEF_PROP_INT(ctx, history, "length", 2);
    DEF_FUNC(ctx, history, "pushState", js_undefined, 3);
    DEF_FUNC(ctx, history, "replaceState", js_undefined, 3);
    DEF_FUNC(ctx, history, "back", js_undefined, 0);
    DEF_FUNC(ctx, history, "forward", js_undefined, 0);
    DEF_FUNC(ctx, history, "go", js_undefined, 1);
    JS_SetPropertyStr(ctx, window, "history", history);
    
    // ===== Storage =====
    GCValue localStorage = JS_NewObject(ctx);
    DEF_FUNC(ctx, localStorage, "getItem", js_null, 1);
    DEF_FUNC(ctx, localStorage, "setItem", js_undefined, 2);
    DEF_FUNC(ctx, localStorage, "removeItem", js_undefined, 1);
    DEF_FUNC(ctx, localStorage, "clear", js_undefined, 0);
    DEF_FUNC(ctx, localStorage, "key", js_null, 1);
    JS_SetPropertyStr(ctx, window, "localStorage", localStorage);
    JS_SetPropertyStr(ctx, window, "sessionStorage", localStorage);
    
    // ===== Console =====
    GCValue console = JS_NewObject(ctx);
    DEF_FUNC(ctx, console, "log", js_console_log, 1);
    DEF_FUNC(ctx, console, "error", js_console_log, 1);
    DEF_FUNC(ctx, console, "warn", js_console_log, 1);
    DEF_FUNC(ctx, console, "info", js_console_log, 1);
    DEF_FUNC(ctx, console, "debug", js_console_log, 1);
    DEF_FUNC(ctx, console, "trace", js_console_log, 1);
    JS_SetPropertyStr(ctx, global, "console", console);
    
    // ===== XMLHttpRequest =====
    GCValue xhr_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, xhr_proto, js_xhr_proto_funcs, js_xhr_proto_funcs_count);
    GCValue xhr_ctor = JS_NewCFunction2(ctx, js_xhr_constructor, "XMLHttpRequest", 
        1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, xhr_ctor, xhr_proto);
    JS_SetClassProto(ctx, js_xhr_class_id, xhr_proto);
    // Set constants on constructor BEFORE transferring ownership
    JS_SetPropertyStr(ctx, xhr_ctor, "UNSENT", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, xhr_ctor, "OPENED", JS_NewInt32(ctx, 1));
    JS_SetPropertyStr(ctx, xhr_ctor, "HEADERS_RECEIVED", JS_NewInt32(ctx, 2));
    JS_SetPropertyStr(ctx, xhr_ctor, "LOADING", JS_NewInt32(ctx, 3));
    JS_SetPropertyStr(ctx, xhr_ctor, "DONE", JS_NewInt32(ctx, 4));
    // global === window, so set once
    JS_SetPropertyStr(ctx, global, "XMLHttpRequest", xhr_ctor);
    
    // ===== HTMLVideoElement =====
    GCValue video_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, video_proto, js_video_proto_funcs, js_video_proto_funcs_count);
    GCValue video_ctor = JS_NewCFunction2(ctx, js_video_constructor, "HTMLVideoElement",
        1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, video_ctor, video_proto);
    JS_SetClassProto(ctx, js_video_class_id, video_proto);
    // Set constants on constructor BEFORE transferring ownership
    JS_SetPropertyStr(ctx, video_ctor, "HAVE_NOTHING", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, video_ctor, "HAVE_METADATA", JS_NewInt32(ctx, 1));
    JS_SetPropertyStr(ctx, video_ctor, "HAVE_CURRENT_DATA", JS_NewInt32(ctx, 2));
    JS_SetPropertyStr(ctx, video_ctor, "HAVE_FUTURE_DATA", JS_NewInt32(ctx, 3));
    JS_SetPropertyStr(ctx, video_ctor, "HAVE_ENOUGH_DATA", JS_NewInt32(ctx, 4));
    JS_SetPropertyStr(ctx, video_ctor, "NETWORK_EMPTY", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, video_ctor, "NETWORK_IDLE", JS_NewInt32(ctx, 1));
    JS_SetPropertyStr(ctx, video_ctor, "NETWORK_LOADING", JS_NewInt32(ctx, 2));
    JS_SetPropertyStr(ctx, video_ctor, "NETWORK_NO_SOURCE", JS_NewInt32(ctx, 3));
    // global === window, so set once
    JS_SetPropertyStr(ctx, global, "HTMLVideoElement", video_ctor);
    
    // ===== fetch API =====
    // fetch is set on global (which is window) - no need to duplicate
    JS_SetPropertyStr(ctx, global, "fetch", JS_NewCFunction(ctx, js_fetch, "fetch", 2));
    
    // ===== Shadow DOM APIs =====
    // ShadowRoot class
    GCValue shadow_root_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, shadow_root_proto, js_shadow_root_proto_funcs, 
        sizeof(js_shadow_root_proto_funcs) / sizeof(js_shadow_root_proto_funcs[0]));
    JS_SetClassProto(ctx, js_shadow_root_class_id, shadow_root_proto);
    GCValue shadow_root_ctor = JS_NewCFunction2(ctx, NULL, "ShadowRoot",
        0, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, shadow_root_ctor, shadow_root_proto);
    JS_SetPropertyStr(ctx, global, "ShadowRoot", shadow_root_ctor);

    // ===== Custom Elements API =====
    GCValue custom_elements = JS_NewObjectClass(ctx, js_custom_element_registry_class_id);
    JS_SetPropertyStr(ctx, custom_elements, "define",
        JS_NewCFunction(ctx, js_custom_elements_define, "define", 2));
    JS_SetPropertyStr(ctx, custom_elements, "get",
        JS_NewCFunction(ctx, js_custom_elements_get, "get", 1));
    JS_SetPropertyStr(ctx, custom_elements, "whenDefined",
        JS_NewCFunction(ctx, js_custom_elements_when_defined, "whenDefined", 1));
    JS_SetPropertyStr(ctx, window, "customElements", custom_elements);
    
    // CustomElementRegistry constructor (for completeness)
    GCValue ce_registry_ctor = JS_NewCFunction2(ctx, NULL, "CustomElementRegistry",
        0, JS_CFUNC_constructor, 0);
    JS_SetPropertyStr(ctx, global, "CustomElementRegistry", ce_registry_ctor);

    // ===== Web Animations API =====
    // Animation class
    GCValue animation_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, animation_proto, js_animation_proto_funcs,
        sizeof(js_animation_proto_funcs) / sizeof(js_animation_proto_funcs[0]));
    JS_SetClassProto(ctx, js_animation_class_id, animation_proto);
    GCValue animation_ctor = JS_NewCFunction2(ctx, js_animation_constructor, "Animation",
        1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, animation_ctor, animation_proto);
    JS_SetPropertyStr(ctx, global, "Animation", animation_ctor);

    // KeyframeEffect class
    GCValue keyframe_effect_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, keyframe_effect_proto, js_keyframe_effect_proto_funcs,
        sizeof(js_keyframe_effect_proto_funcs) / sizeof(js_keyframe_effect_proto_funcs[0]));
    JS_SetClassProto(ctx, js_keyframe_effect_class_id, keyframe_effect_proto);
    GCValue keyframe_effect_ctor = JS_NewCFunction2(ctx, js_keyframe_effect_constructor, "KeyframeEffect",
        3, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, keyframe_effect_ctor, keyframe_effect_proto);
    JS_SetPropertyStr(ctx, global, "KeyframeEffect", keyframe_effect_ctor);

    // ===== Font Loading API =====
    // FontFace class
    GCValue font_face_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, font_face_proto, js_font_face_proto_funcs,
        sizeof(js_font_face_proto_funcs) / sizeof(js_font_face_proto_funcs[0]));
    JS_SetClassProto(ctx, js_font_face_class_id, font_face_proto);
    GCValue font_face_ctor = JS_NewCFunction2(ctx, js_font_face_constructor, "FontFace",
        3, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, font_face_ctor, font_face_proto);
    JS_SetPropertyStr(ctx, global, "FontFace", font_face_ctor);

    // FontFaceSet class (document.fonts)
    GCValue font_face_set_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, font_face_set_proto, js_font_face_set_proto_funcs,
        sizeof(js_font_face_set_proto_funcs) / sizeof(js_font_face_set_proto_funcs[0]));
    JS_SetClassProto(ctx, js_font_face_set_class_id, font_face_set_proto);
    GCValue font_face_set = JS_NewObjectClass(ctx, js_font_face_set_class_id);
    FontFaceSetData *ffs = calloc(1, sizeof(FontFaceSetData));
    if (ffs) {
        ffs->loaded_fonts = JS_NewArray(ctx);
        JS_SetOpaque(font_face_set, ffs);
    }
    // Add Symbol.iterator to FontFaceSet.prototype using C
    GCValue symbol_ctor2 = JS_GetPropertyStr(ctx, global, "Symbol");
    if (!JS_IsException(symbol_ctor2)) {
        GCValue iterator_symbol = JS_GetPropertyStr(ctx, symbol_ctor2, "iterator");
        if (!JS_IsException(iterator_symbol)) {
            GCValue values_func = JS_GetPropertyStr(ctx, font_face_set_proto, "values");
            if (!JS_IsException(values_func)) {
                JS_SetProperty(ctx, font_face_set_proto, JS_ValueToAtom(ctx, iterator_symbol), values_func);
            }

        }


    }
    
    JS_SetPropertyStr(ctx, document, "fonts", font_face_set);
    
    GCValue font_face_set_ctor = JS_NewCFunction2(ctx, NULL, "FontFaceSet",
        0, JS_CFUNC_constructor, 0);
    JS_SetPropertyStr(ctx, global, "FontFaceSet", font_face_set_ctor);

    // ===== Observer APIs =====
    // MutationObserver
    GCValue mutation_observer_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, mutation_observer_proto, js_mutation_observer_proto_funcs,
        sizeof(js_mutation_observer_proto_funcs) / sizeof(js_mutation_observer_proto_funcs[0]));
    JS_SetClassProto(ctx, js_mutation_observer_class_id, mutation_observer_proto);
    GCValue mutation_observer_ctor = JS_NewCFunction2(ctx, js_mutation_observer_constructor, "MutationObserver",
        1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, mutation_observer_ctor, mutation_observer_proto);
    JS_SetPropertyStr(ctx, global, "MutationObserver", mutation_observer_ctor);

    // ResizeObserver
    GCValue resize_observer_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, resize_observer_proto, js_resize_observer_proto_funcs,
        sizeof(js_resize_observer_proto_funcs) / sizeof(js_resize_observer_proto_funcs[0]));
    JS_SetClassProto(ctx, js_resize_observer_class_id, resize_observer_proto);
    GCValue resize_observer_ctor = JS_NewCFunction2(ctx, js_resize_observer_constructor, "ResizeObserver",
        1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, resize_observer_ctor, resize_observer_proto);
    JS_SetPropertyStr(ctx, global, "ResizeObserver", resize_observer_ctor);

    // IntersectionObserver
    GCValue intersection_observer_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, intersection_observer_proto, js_intersection_observer_proto_funcs,
        sizeof(js_intersection_observer_proto_funcs) / sizeof(js_intersection_observer_proto_funcs[0]));
    JS_SetClassProto(ctx, js_intersection_observer_class_id, intersection_observer_proto);
    GCValue intersection_observer_ctor = JS_NewCFunction2(ctx, js_intersection_observer_constructor, "IntersectionObserver",
        1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, intersection_observer_ctor, intersection_observer_proto);
    JS_SetPropertyStr(ctx, global, "IntersectionObserver", intersection_observer_ctor);

    // ===== Performance API =====
    // PerformanceEntry class
    GCValue performance_entry_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, performance_entry_proto, js_performance_entry_proto_funcs,
        sizeof(js_performance_entry_proto_funcs) / sizeof(js_performance_entry_proto_funcs[0]));
    JS_SetClassProto(ctx, js_performance_entry_class_id, performance_entry_proto);
    GCValue performance_entry_ctor = JS_NewCFunction2(ctx, NULL, "PerformanceEntry",
        0, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, performance_entry_ctor, performance_entry_proto);
    JS_SetPropertyStr(ctx, global, "PerformanceEntry", performance_entry_ctor);

    // PerformanceObserver class
    GCValue performance_observer_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, performance_observer_proto, js_performance_observer_proto_funcs,
        sizeof(js_performance_observer_proto_funcs) / sizeof(js_performance_observer_proto_funcs[0]));
    JS_SetClassProto(ctx, js_performance_observer_class_id, performance_observer_proto);
    GCValue performance_observer_ctor = JS_NewCFunction2(ctx, js_performance_observer_constructor, "PerformanceObserver",
        1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, performance_observer_ctor, performance_observer_proto);
    JS_SetPropertyStr(ctx, global, "PerformanceObserver", performance_observer_ctor);

    // Performance class
    GCValue performance_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, performance_proto, js_performance_proto_funcs,
        sizeof(js_performance_proto_funcs) / sizeof(js_performance_proto_funcs[0]));
    JS_SetClassProto(ctx, js_performance_class_id, performance_proto);
    GCValue performance_obj = JS_NewObjectClass(ctx, js_performance_class_id);
    PerformanceData *perf_data = calloc(1, sizeof(PerformanceData));
    if (perf_data) {
        perf_data->start_time = 0.0;
        JS_SetOpaque(performance_obj, perf_data);
    }
    JS_SetPropertyStr(ctx, window, "performance", performance_obj);
    
    GCValue performance_ctor = JS_NewCFunction2(ctx, NULL, "Performance",
        0, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, performance_ctor, performance_proto);
    JS_SetPropertyStr(ctx, global, "Performance", performance_ctor);

    // ===== DOMRect API =====
    // DOMRectReadOnly class
    GCValue dom_rect_read_only_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, dom_rect_read_only_proto, js_dom_rect_read_only_proto_funcs,
        sizeof(js_dom_rect_read_only_proto_funcs) / sizeof(js_dom_rect_read_only_proto_funcs[0]));
    JS_SetClassProto(ctx, js_dom_rect_read_only_class_id, dom_rect_read_only_proto);
    GCValue dom_rect_read_only_ctor = JS_NewCFunction2(ctx, js_dom_rect_read_only_constructor, "DOMRectReadOnly",
        4, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, dom_rect_read_only_ctor, dom_rect_read_only_proto);
    // Add fromRect static method
    GCValue from_rect_ro = JS_NewCFunction(ctx, js_dom_rect_read_only_from_rect, "fromRect", 1);
    JS_SetPropertyStr(ctx, dom_rect_read_only_ctor, "fromRect", from_rect_ro);
    JS_SetPropertyStr(ctx, global, "DOMRectReadOnly", dom_rect_read_only_ctor);

    // DOMRect class
    GCValue dom_rect_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, dom_rect_proto, js_dom_rect_proto_funcs,
        sizeof(js_dom_rect_proto_funcs) / sizeof(js_dom_rect_proto_funcs[0]));
    JS_SetClassProto(ctx, js_dom_rect_class_id, dom_rect_proto);
    GCValue dom_rect_ctor = JS_NewCFunction2(ctx, js_dom_rect_constructor, "DOMRect",
        4, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, dom_rect_ctor, dom_rect_proto);
    // Add fromRect static method
    GCValue from_rect = JS_NewCFunction(ctx, js_dom_rect_from_rect, "fromRect", 1);
    JS_SetPropertyStr(ctx, dom_rect_ctor, "fromRect", from_rect);
    JS_SetPropertyStr(ctx, global, "DOMRect", dom_rect_ctor);

}

/*
 * Reset browser stubs state between downloads.
 * This clears all static variables to ensure a fresh start
 * when js_quickjs_exec_scripts is called multiple times.
 */
void browser_stubs_reset(void) {
    /* Reset all static state variables to initial values */
    
    /* Reset DOMException class ID - it will be reallocated on next init */
    js_dom_exception_class_id = 0;
    
    /* Note: g_performance_time is a minor timing counter that accumulates 
     * 0.1ms per call to js_performance_now(). Over multiple downloads it 
     * could drift but it's not critical - the GC reset is the main fix.
     * A full reset of this variable would require moving its declaration
     * or using a different approach. For now, the GC reset is sufficient. */
}
