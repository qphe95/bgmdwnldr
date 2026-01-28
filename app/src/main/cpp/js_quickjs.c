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

static JSRuntime *rt = NULL;
static JSContext *ctx = NULL;

static const char *browser_stubs = BROWSER_STUBS_JS;

bool js_quickjs_init(void) {
    if (rt) return true;
    rt = JS_NewRuntime();
    if (!rt) return false;
    ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        rt = NULL;
        return false;
    }
    return true;
}

void js_quickjs_cleanup(void) {
    if (ctx) { JS_FreeContext(ctx); ctx = NULL; }
    if (rt) { JS_FreeRuntime(rt); rt = NULL; }
}

char *js_quickjs_decrypt_signature(const char *player_js, size_t player_js_len,
                                    const char *encrypted_sig, size_t *out_len) {
    if (!ctx && !js_quickjs_init()) {
        *out_len = 0;
        return NULL;
    }
    
    LOGI("Running player JS...");
    
    JSContext *decrypt_ctx = JS_NewContext(rt);
    if (!decrypt_ctx) {
        *out_len = 0;
        return NULL;
    }
    
    /* Install browser stubs */
    JS_Eval(decrypt_ctx, browser_stubs, strlen(browser_stubs), "<stubs>", 0);
    
    /* Execute player JS */
    JSValue player_result = JS_Eval(decrypt_ctx, player_js, player_js_len, "player.js", 0);
    if (JS_IsException(player_result)) {
        JSValue exc = JS_GetException(decrypt_ctx);
        const char *err = JS_ToCString(decrypt_ctx, exc);
        LOGE("Player JS error: %s", err ? err : "unknown");
        JS_FreeCString(decrypt_ctx, err);
        JS_FreeValue(decrypt_ctx, exc);
        JS_FreeValue(decrypt_ctx, player_result);
        JS_FreeContext(decrypt_ctx);
        
        size_t len = strlen(encrypted_sig);
        char *fallback = malloc(len + 1);
        if (fallback) memcpy(fallback, encrypted_sig, len + 1), *out_len = len;
        return fallback;
    }
    JS_FreeValue(decrypt_ctx, player_result);
    LOGI("Player JS executed");
    
    /* Find and call the decipher function */
    const char *find_and_decrypt = 
        "(function() {"
        "  var sig = arguments[0];"
        "  var global = this;"
        "  var candidates = [];"
        "  var debugInfo = [];"
        "  var shortNamedFuncs = [];"
        "  "
        "  /* Helper: is this a valid signature result? */"
        "  var isValidResult = function(s) {"
        "    if (typeof s !== 'string') return false;"
        "    if (s === sig) return false;"
        "    if (s.length < 50 || s.length > 200) return false;"
        "    if (!/^[A-Za-z0-9_=\-]+$/.test(s)) return false;"
        "    if (s.indexOf('http') >= 0 || s.indexOf('%') >= 0) return false;"
        "    return true;"
        "  };"
        "  "
        "  var keys = Object.getOwnPropertyNames(global);"
        "  debugInfo.push('Total keys: ' + keys.length);"
        "  "
        "  /* Collect all short-named functions */"
        "  for (var i = 0; i < keys.length; i++) {"
        "    var key = keys[i];"
        "    if (key.length > 3) continue;"
        "    try {"
        "      var val = global[key];"
        "      if (typeof val === 'function') {"
        "        shortNamedFuncs.push(key);"
        "      }"
        "    } catch(e) {}"
        "  }"
        "  debugInfo.push('Short funcs: ' + shortNamedFuncs.join(','));"
        "  "
        "  /* Try all short-named global functions */"
        "  for (var i = 0; i < shortNamedFuncs.length; i++) {"
        "    var key = shortNamedFuncs[i];"
        "    try {"
        "      var fn = global[key];"
        "      var result = fn(sig);"
        "      var resultType = typeof result;"
        "      var resultPreview = resultType === 'string' ? result.substring(0, 30) : String(result);"
        "      if (isValidResult(result)) {"
        "        candidates.push({name: key, result: result});"
        "      } else if (resultType === 'string' && result !== sig) {"
        "        debugInfo.push(key + '->' + resultType + '(' + result.length + '):' + resultPreview);"
        "      }"
        "    } catch(e) {"
        "      debugInfo.push(key + ':ERROR:' + e.message);"
        "    }"
        "  }"
        "  "
        "  if (candidates.length === 0) {"
        "    return 'DEBUG_NO_CANDIDATES:' + debugInfo.slice(0, 10).join(';');"
        "  }"
        "  "
        "  candidates.sort(function(a, b) {"
        "    return Math.abs(a.result.length - sig.length) - Math.abs(b.result.length - sig.length);"
        "  });"
        "  "
        "  return candidates[0].result;"
        "})";
    
    JSValue find_func = JS_Eval(decrypt_ctx, find_and_decrypt, strlen(find_and_decrypt), "<find>", 0);
    
    if (JS_IsFunction(decrypt_ctx, find_func)) {
        JSValue sig_val = JS_NewString(decrypt_ctx, encrypted_sig);
        JSValue result = JS_Call(decrypt_ctx, find_func, JS_UNDEFINED, 1, &sig_val);
        JS_FreeValue(decrypt_ctx, sig_val);
        JS_FreeValue(decrypt_ctx, find_func);
        
        if (!JS_IsException(result) && !JS_IsNull(result) && !JS_IsUndefined(result)) {
            const char *str = JS_ToCString(decrypt_ctx, result);
            if (str) {
                if (strncmp(str, "DEBUG_NO_CANDIDATES:", 20) == 0) {
                    LOGE("No decryption candidates found. Debug: %s", str + 20);
                } else if (strlen(str) > 10) {
                    LOGI("Found decrypted sig: %.50s...", str);
                    size_t len = strlen(str);
                    char *out = malloc(len + 1);
                    if (out) {
                        memcpy(out, str, len + 1);
                        *out_len = len;
                        JS_FreeCString(decrypt_ctx, str);
                        JS_FreeValue(decrypt_ctx, result);
                        JS_FreeContext(decrypt_ctx);
                        return out;
                    }
                }
            }
            JS_FreeCString(decrypt_ctx, str);
        }
        JS_FreeValue(decrypt_ctx, result);
    } else {
        JS_FreeValue(decrypt_ctx, find_func);
    }
    
    JS_FreeContext(decrypt_ctx);
    
    /* Fallback */
    size_t len = strlen(encrypted_sig);
    char *fallback = malloc(len + 1);
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
    (void)player_js;
    strncpy(out_decrypted, n_param, out_len - 1);
    out_decrypted[out_len - 1] = '\0';
    return true;
}
