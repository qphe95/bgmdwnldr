#include "js_quickjs.h"
#include "third_party/quickjs/quickjs.h"
#include "browser_stubs.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>

#define LOG_TAG "js_quickjs"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

/* Helper to dump detailed exception info */
static void log_exception(JSContext *ctx, JSValue exception, const char *context) {
    const char *error_str = JS_ToCString(ctx, exception);
    LOGE("[%s] Exception: %s", context, error_str ? error_str : "unknown");
    
    /* Try to get stack trace */
    JSValue stack = JS_GetPropertyStr(ctx, exception, "stack");
    if (!JS_IsUndefined(stack)) {
        const char *stack_str = JS_ToCString(ctx, stack);
        if (stack_str && strlen(stack_str) > 0) {
            LOGE("[%s] Stack trace:\n%s", context, stack_str);
        }
        JS_FreeCString(ctx, stack_str);
    }
    JS_FreeValue(ctx, stack);
    
    /* Try to get additional properties */
    JSValue fileName = JS_GetPropertyStr(ctx, exception, "fileName");
    JSValue lineNumber = JS_GetPropertyStr(ctx, exception, "lineNumber");
    JSValue columnNumber = JS_GetPropertyStr(ctx, exception, "columnNumber");
    
    if (!JS_IsUndefined(fileName)) {
        const char *fn = JS_ToCString(ctx, fileName);
        LOGE("[%s] File: %s", context, fn ? fn : "unknown");
        JS_FreeCString(ctx, fn);
    }
    if (!JS_IsUndefined(lineNumber)) {
        int32_t ln;
        if (JS_ToInt32(ctx, &ln, lineNumber) == 0) {
            LOGE("[%s] Line: %d", context, ln);
        }
    }
    if (!JS_IsUndefined(columnNumber)) {
        int32_t cn;
        if (JS_ToInt32(ctx, &cn, columnNumber) == 0) {
            LOGE("[%s] Column: %d", context, cn);
        }
    }
    
    JS_FreeValue(ctx, fileName);
    JS_FreeValue(ctx, lineNumber);
    JS_FreeValue(ctx, columnNumber);
    JS_FreeCString(ctx, error_str);
}

static JSRuntime *rt = NULL;
static JSContext *ctx = NULL;

/* Use browser stubs from header file */
static const char *browser_stubs = BROWSER_STUBS_JS;

bool js_quickjs_init(void) {
    if (rt) return true;
    
    rt = JS_NewRuntime();
    if (!rt) {
        LOGE("Failed to create QuickJS runtime");
        return false;
    }
    
    ctx = JS_NewContext(rt);
    if (!ctx) {
        LOGE("Failed to create QuickJS context");
        JS_FreeRuntime(rt);
        rt = NULL;
        return false;
    }
    
    LOGI("QuickJS initialized");
    return true;
}

void js_quickjs_cleanup(void) {
    if (ctx) {
        JS_FreeContext(ctx);
        ctx = NULL;
    }
    if (rt) {
        JS_FreeRuntime(rt);
        rt = NULL;
    }
    LOGI("QuickJS cleaned up");
}

char *js_quickjs_eval(const char *code, size_t code_len, size_t *out_len) {
    if (!ctx && !js_quickjs_init()) {
        *out_len = 0;
        return NULL;
    }
    
    if (code_len == 0) code_len = strlen(code);
    
    JSValue result = JS_Eval(ctx, code, code_len, "<input>", 0);
    
    if (JS_IsException(result)) {
        JSValue exception = JS_GetException(ctx);
        const char *error_str = JS_ToCString(ctx, exception);
        LOGE("JS Exception: %s", error_str ? error_str : "unknown");
        JS_FreeCString(ctx, error_str);
        JS_FreeValue(ctx, exception);
        JS_FreeValue(ctx, result);
        *out_len = 0;
        return NULL;
    }
    
    const char *str = JS_ToCString(ctx, result);
    if (!str) {
        JS_FreeValue(ctx, result);
        *out_len = 0;
        return NULL;
    }
    
    size_t len = strlen(str);
    char *ret = (char *)malloc(len + 1);
    if (ret) {
        memcpy(ret, str, len + 1);
        *out_len = len;
    }
    
    JS_FreeCString(ctx, str);
    JS_FreeValue(ctx, result);
    
    return ret;
}

char *js_quickjs_decrypt_signature(const char *player_js, size_t player_js_len,
                                    const char *encrypted_sig, size_t *out_len) {
    if (!ctx && !js_quickjs_init()) {
        *out_len = 0;
        return NULL;
    }
    
    LOGI("Decrypting signature with QuickJS");
    LOGI("Player JS size: %zu bytes", player_js_len);
    LOGI("Encrypted sig: %.50s...", encrypted_sig);
    
    /* Create a new context for this decryption */
    JSContext *decrypt_ctx = JS_NewContext(rt);
    if (!decrypt_ctx) {
        LOGE("Failed to create decryption context");
        *out_len = 0;
        return NULL;
    }
    
    /* First, set up browser stubs */
    JSValue stub_result = JS_Eval(decrypt_ctx, browser_stubs, strlen(browser_stubs), "<stubs>", 0);
    if (JS_IsException(stub_result)) {
        JSValue exception = JS_GetException(decrypt_ctx);
        log_exception(decrypt_ctx, exception, "BrowserStubs");
        JS_FreeValue(decrypt_ctx, exception);
    }
    JS_FreeValue(decrypt_ctx, stub_result);
    LOGI("Browser stubs installed (%zu bytes)", strlen(browser_stubs));
    
    /* Inject debugging helper to catch property access errors */
    const char *debug_wrapper = 
        "(function() {"
        "  var _global = this;"
        "  var _undefinedCalls = [];"
        "  "
        "  /* Track undefined function calls */"
        "  var _origCall = Function.prototype.call;"
        "  Function.prototype.call = function() {"
        "    if (typeof this !== 'function') {"
        "      var err = new Error();"
        "      _undefinedCalls.push('CALL_NON_FUNCTION: typeof=' + typeof this + ' stack=' + err.stack);"
        "    }"
        "    return _origCall.apply(this, arguments);"
        "  };"
        "  "
        "  /* Proxy to catch undefined property accesses */"
        "  if (typeof Proxy !== 'undefined') {"
        "    var _origGet = Object.prototype.__lookupGetter__;"
        "    var _origSet = Object.prototype.__lookupSetter__;"
        "  }"
        "  "
        "  /* Override common constructors to catch errors */"
        "  var _origSymbol = _global.Symbol;"
        "  if (_origSymbol) {"
        "    var _origIterator = _origSymbol.iterator;"
        "    Object.defineProperty(_origSymbol, 'iterator', {"
        "      get: function() { return _origIterator; },"
        "      set: function(v) { _origIterator = v; }"
        "    });"
        "  }"
        "  "
        "  /* Log to console for debugging */"
        "  _global._getUndefinedCalls = function() { return _undefinedCalls; };"
        "})();";
    JSValue debug_result = JS_Eval(decrypt_ctx, debug_wrapper, strlen(debug_wrapper), "<debug>", 0);
    if (JS_IsException(debug_result)) {
        JSValue exception = JS_GetException(decrypt_ctx);
        log_exception(decrypt_ctx, exception, "DebugWrapper");
        JS_FreeValue(decrypt_ctx, exception);
    }
    JS_FreeValue(decrypt_ctx, debug_result);
    LOGI("Debug wrapper installed");
    
    /* Execute the player JS with detailed undefined function tracking */
    const char *player_wrapper = 
        "(function() {"
        "  var _global = this;"
        "  var _undefinedAccess = [];"
        "  "
        "  /* Track all property accesses that return undefined */"
        "  var _origDesc = Object.getOwnPropertyDescriptor;"
        "  var _origHasOwn = Object.prototype.hasOwnProperty;"
        "  "
        "  /* Wrap function calls to catch undefined */"
        "  var _origCall = Function.prototype.call;"
        "  Function.prototype.call = function() {"
        "    if (this === undefined || this === null) {"
        "      var stack = new Error().stack;"
        "      _undefinedAccess.push('CALL_ON_UNDEFINED: ' + stack);"
        "    }"
        "    return _origCall.apply(this, arguments);"
        "  };"
        "  "
        "  /* Track specific undefined function errors */"
        "  var _checkUndefined = function(name, value) {"
        "    if (value === undefined) {"
        "      var stack = new Error('Undefined: ' + name).stack;"
        "      _undefinedAccess.push('UNDEFINED_PROP[' + name + ']: ' + stack);"
        "    }"
        "    return value;"
        "  };"
        "  "
        "  /* Inject checking for common patterns */"
        "  var _origNumberFormat = Intl.NumberFormat;"
        "  if (_origNumberFormat) {"
        "    Intl.NumberFormat = function(l, o) { return { format: function(n) { return String(n); } }; };"
        "    Intl.NumberFormat.supportedLocalesOf = function(l) { return Array.isArray(l) ? l : []; };"
        "  }"
        "  "
        "  /* Expose tracking for debugging */"
        "  _global._getUndefinedAccess = function() { return _undefinedAccess; };"
        "})();";
    
    JSValue wrapper_result = JS_Eval(decrypt_ctx, player_wrapper, strlen(player_wrapper), "<wrapper>", 0);
    if (JS_IsException(wrapper_result)) {
        JSValue exception = JS_GetException(decrypt_ctx);
        log_exception(decrypt_ctx, exception, "PlayerWrapper");
        JS_FreeValue(decrypt_ctx, exception);
    }
    JS_FreeValue(decrypt_ctx, wrapper_result);
    
    /* Now execute the actual player JS */
    JSValue player_result = JS_Eval(decrypt_ctx, player_js, player_js_len, "player.js", 0);
    
    if (JS_IsException(player_result)) {
        JSValue exception = JS_GetException(decrypt_ctx);
        log_exception(decrypt_ctx, exception, "PlayerJS");
        
        /* Try to get more details by parsing the error */
        const char *err_str = JS_ToCString(decrypt_ctx, exception);
        if (err_str) {
            /* Check for common missing function patterns */
            if (strstr(err_str, "not a function") || strstr(err_str, "is not a function")) {
                LOGE("[PlayerJS] Detected 'not a function' error - likely missing browser API");
                LOGE("[PlayerJS] Common causes: missing Symbol.iterator, missing DOM method, etc.");
            }
        }
        JS_FreeCString(decrypt_ctx, err_str);
        
        /* Additional debugging: check global object state */
        LOGD("Attempting to diagnose what function is missing...");
        JSValue global_obj = JS_GetGlobalObject(decrypt_ctx);
        JSPropertyEnum *props = NULL;
        uint32_t prop_count = 0;
        
        if (JS_GetOwnPropertyNames(decrypt_ctx, &props, &prop_count, global_obj, JS_GPN_STRING_MASK) == 0) {
            LOGD("Global object has %u properties", prop_count);
            /* Free the properties */
            JS_FreePropertyEnum(decrypt_ctx, props, prop_count);
        }
        JS_FreeValue(decrypt_ctx, global_obj);
        
        JS_FreeValue(decrypt_ctx, exception);
        JS_FreeValue(decrypt_ctx, player_result);
        JS_FreeContext(decrypt_ctx);
        
        /* Fallback: return original signature */
        size_t len = strlen(encrypted_sig);
        char *fallback = (char *)malloc(len + 1);
        if (fallback) {
            memcpy(fallback, encrypted_sig, len + 1);
            *out_len = len;
        }
        return fallback;
    }
    
    JS_FreeValue(decrypt_ctx, player_result);
    LOGI("Player JS executed successfully");
    
    /* Try to find the decipher function by looking at global variables
     * YouTube typically stores the decipher function in a global object
     */
    const char *find_decipher = 
        "(function() {"
        "  var sig = arguments[0];"
        "  var global = this;"
        "  var candidates = [];"
        "  "
        "  /* Helper: Check if result looks like a valid decrypted signature */"
        "  var isValidDecryption = function(result, input) {"
        "    if (typeof result !== 'string') return false;"
        "    if (result === input) return false;"
        "    if (result.length < 10 || result.length > 150) return false;"
        "    /* Valid sig should be alphanumeric with some special chars */"
        "    if (!/^[A-Za-z0-9_=\-]+$/.test(result)) return false;"
        "    /* Should not contain URL patterns */"
        "    if (result.indexOf('http') >= 0) return false;"
        "    if (result.indexOf('www.') >= 0) return false;"
        "    if (result.indexOf('%') >= 0) return false;"
        "    return true;"
        "  };"
        "  "
        "  /* Look through all global variables */"
        "  for (var key in global) {"
        "    try {"
        "      var val = global[key];"
        "      if (typeof val === 'function') {"
        "        /* Test if this function transforms our signature */"
        "        var test = val(sig);"
        "        if (isValidDecryption(test, sig)) {"
        "          candidates.push({name: key, result: test});"
        "        }"
        "      }"
        "    } catch(e) {}"
        "  }"
        "  "
        "  /* Also try common object patterns */"
        "  var commonNames = ['a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'sig', 'signature', 'decrypt'];"
        "  for (var i = 0; i < commonNames.length; i++) {"
        "    try {"
        "      var obj = global[commonNames[i]];"
        "      if (obj && typeof obj === 'object') {"
        "        for (var meth in obj) {"
        "          if (typeof obj[meth] === 'function') {"
        "            try {"
        "              var test2 = obj[meth](sig);"
        "              if (isValidDecryption(test2, sig)) {"
        "                candidates.push({name: commonNames[i] + '.' + meth, result: test2});"
        "              }"
        "            } catch(e2) {}"
        "          }"
        "        }"
        "      }"
        "    } catch(e) {}"
        "  }"
        "  "
        "  if (candidates.length > 0) {"
        "    /* Return the shortest result (usually the correct one) */"
        "    candidates.sort(function(a, b) { return a.result.length - b.result.length; });"
        "    return candidates[0].result;"
        "  }"
        "  return null;"
        "})";
    
    /* Call the find function with our signature */
    JSValue find_func = JS_Eval(decrypt_ctx, find_decipher, strlen(find_decipher), "<find>", 0);
    
    if (JS_IsFunction(decrypt_ctx, find_func)) {
        JSValue sig_val = JS_NewString(decrypt_ctx, encrypted_sig);
        JSValue result = JS_Call(decrypt_ctx, find_func, JS_UNDEFINED, 1, &sig_val);
        JS_FreeValue(decrypt_ctx, sig_val);
        JS_FreeValue(decrypt_ctx, find_func);
        
        if (!JS_IsException(result) && !JS_IsNull(result) && !JS_IsUndefined(result)) {
            const char *str = JS_ToCString(decrypt_ctx, result);
            if (str && strlen(str) > 10) {
                LOGI("Decrypted sig: %.50s...", str);
                size_t len = strlen(str);
                char *decrypted = (char *)malloc(len + 1);
                if (decrypted) {
                    memcpy(decrypted, str, len + 1);
                    *out_len = len;
                    JS_FreeCString(decrypt_ctx, str);
                    JS_FreeValue(decrypt_ctx, result);
                    JS_FreeContext(decrypt_ctx);
                    LOGI("Successfully decrypted signature");
                    return decrypted;
                }
            }
            JS_FreeCString(decrypt_ctx, str);
        }
        JS_FreeValue(decrypt_ctx, result);
    } else {
        JS_FreeValue(decrypt_ctx, find_func);
    }
    
    JS_FreeContext(decrypt_ctx);
    
    LOGE("Could not decrypt signature - returning original");
    size_t len = strlen(encrypted_sig);
    char *fallback = (char *)malloc(len + 1);
    if (fallback) {
        memcpy(fallback, encrypted_sig, len + 1);
        *out_len = len;
    }
    return fallback;
}

bool js_quickjs_decrypt_signature_simple(const char *player_js, const char *encrypted_sig,
                                          char *out_decrypted, size_t out_len) {
    size_t result_len = 0;
    char *result = js_quickjs_decrypt_signature(player_js, strlen(player_js), encrypted_sig, &result_len);
    if (result && result_len > 0 && result_len < out_len) {
        memcpy(out_decrypted, result, result_len + 1);
        free(result);
        return true;
    }
    if (result) free(result);
    return false;
}

bool js_quickjs_decrypt_nparam(const char *player_js, const char *n_param,
                                char *out_decrypted, size_t out_len) {
    /* N-param decryption uses similar approach */
    /* TODO: Implement n-param throttling parameter decryption */
    (void)player_js;
    strncpy(out_decrypted, n_param, out_len - 1);
    out_decrypted[out_len - 1] = '\0';
    return true;
}
