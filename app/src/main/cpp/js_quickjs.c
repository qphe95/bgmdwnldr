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

/* Storage for captured URLs from last execution */
static JsCapturedUrl g_captured_urls[JS_CAPTURED_URLS_MAX];
static int g_captured_count = 0;

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
    g_captured_count = 0;
}

/* Helper: Extract captured URLs from JS context */
static int extract_captured_urls(JSContext *js_ctx, JsCapturedUrl *out_urls, int max_urls) {
    if (!js_ctx || !out_urls || max_urls <= 0) return 0;
    
    int count = 0;
    JSValue global = JS_GetGlobalObject(js_ctx);
    
    /* Get __capturedUrls from global scope */
    JSValue captured = JS_GetPropertyStr(js_ctx, global, "__capturedUrls");
    if (!JS_IsUndefined(captured) && !JS_IsNull(captured)) {
        JSValue length_val = JS_GetPropertyStr(js_ctx, captured, "length");
        int length = 0;
        if (JS_IsNumber(length_val)) {
            JS_ToInt32(js_ctx, &length, length_val);
        }
        JS_FreeValue(js_ctx, length_val);
        
        for (int i = 0; i < length && count < max_urls; i++) {
            JSValue item = JS_GetPropertyUint32(js_ctx, captured, i);
            if (!JS_IsUndefined(item) && !JS_IsNull(item)) {
                JSValue url_val = JS_GetPropertyStr(js_ctx, item, "url");
                JSValue method_val = JS_GetPropertyStr(js_ctx, item, "method");
                JSValue type_val = JS_GetPropertyStr(js_ctx, item, "type");
                
                const char *url = JS_ToCString(js_ctx, url_val);
                const char *method = JS_ToCString(js_ctx, method_val);
                const char *type = JS_ToCString(js_ctx, type_val);
                
                if (url) {
                    strncpy(out_urls[count].url, url, JS_CAPTURED_URL_LEN - 1);
                    out_urls[count].url[JS_CAPTURED_URL_LEN - 1] = '\0';
                    
                    if (method) {
                        strncpy(out_urls[count].method, method, 15);
                        out_urls[count].method[15] = '\0';
                    } else {
                        strcpy(out_urls[count].method, "GET");
                    }
                    
                    if (type) {
                        strncpy(out_urls[count].type, type, 15);
                        out_urls[count].type[15] = '\0';
                    } else {
                        strcpy(out_urls[count].type, "xhr");
                    }
                    
                    count++;
                }
                
                JS_FreeCString(js_ctx, url);
                JS_FreeCString(js_ctx, method);
                JS_FreeCString(js_ctx, type);
                
                JS_FreeValue(js_ctx, url_val);
                JS_FreeValue(js_ctx, method_val);
                JS_FreeValue(js_ctx, type_val);
            }
            JS_FreeValue(js_ctx, item);
        }
    }
    
    JS_FreeValue(js_ctx, captured);
    JS_FreeValue(js_ctx, global);
    
    return count;
}

/* Helper: Clear captured URLs in JS context */
static void clear_captured_urls_js(JSContext *js_ctx) {
    if (!js_ctx) return;
    JSValue global = JS_GetGlobalObject(js_ctx);
    JSValue clear_fn = JS_GetPropertyStr(js_ctx, global, "__clearCapturedUrls");
    if (JS_IsFunction(js_ctx, clear_fn)) {
        JS_Call(js_ctx, clear_fn, global, 0, NULL);
    }
    JS_FreeValue(js_ctx, clear_fn);
    JS_FreeValue(js_ctx, global);
}

char *js_quickjs_eval(const char *code, size_t code_len, size_t *out_len) {
    if (!ctx && !js_quickjs_init()) {
        *out_len = 0;
        return NULL;
    }
    
    JSContext *eval_ctx = JS_NewContext(rt);
    if (!eval_ctx) {
        *out_len = 0;
        return NULL;
    }
    
    /* Install browser stubs */
    JS_Eval(eval_ctx, browser_stubs, strlen(browser_stubs), "<stubs>", 0);
    
    /* Execute user code */
    JSValue result = JS_Eval(eval_ctx, code, code_len, "<eval>", 0);
    
    char *out = NULL;
    if (!JS_IsException(result) && !JS_IsNull(result) && !JS_IsUndefined(result)) {
        const char *str = JS_ToCString(eval_ctx, result);
        if (str) {
            size_t len = strlen(str);
            out = malloc(len + 1);
            if (out) {
                memcpy(out, str, len + 1);
                *out_len = len;
            }
            JS_FreeCString(eval_ctx, str);
        }
    }
    
    JS_FreeValue(eval_ctx, result);
    JS_FreeContext(eval_ctx);
    
    return out;
}

bool js_quickjs_exec_scripts(const char **scripts, const size_t *script_lens, int script_count,
                              JsExecResult *out_result) {
    if (!scripts || script_count <= 0 || !out_result) {
        return false;
    }
    
    if (!ctx && !js_quickjs_init()) {
        return false;
    }
    
    /* Clear previous captured URLs */
    g_captured_count = 0;
    memset(out_result, 0, sizeof(JsExecResult));
    
    JSContext *exec_ctx = JS_NewContext(rt);
    if (!exec_ctx) {
        return false;
    }
    
    /* Install browser stubs */
    JS_Eval(exec_ctx, browser_stubs, strlen(browser_stubs), "<stubs>", 0);
    
    /* Clear any previous captured URLs in JS */
    clear_captured_urls_js(exec_ctx);
    
    bool success = true;
    
    /* Execute each script in sequence */
    for (int i = 0; i < script_count; i++) {
        if (!scripts[i] || script_lens[i] == 0) {
            continue;
        }
        
        char filename[32];
        snprintf(filename, sizeof(filename), "script%d.js", i);
        
        LOGI("Executing script %d/%d (%zu bytes)...", i + 1, script_count, script_lens[i]);
        
        JSValue result = JS_Eval(exec_ctx, scripts[i], script_lens[i], filename, 0);
        
        if (JS_IsException(result)) {
            JSValue exc = JS_GetException(exec_ctx);
            const char *err = JS_ToCString(exec_ctx, exc);
            LOGE("Script %d error: %s", i, err ? err : "unknown");
            JS_FreeCString(exec_ctx, err);
            JS_FreeValue(exec_ctx, exc);
            success = false;
            /* Continue executing other scripts even if one fails */
        }
        
        JS_FreeValue(exec_ctx, result);
    }
    
    /* Extract captured URLs */
    out_result->captured_count = extract_captured_urls(exec_ctx, out_result->captured_urls, 
                                                        JS_CAPTURED_URLS_MAX);
    g_captured_count = out_result->captured_count;
    
    /* Copy to global storage */
    memcpy(g_captured_urls, out_result->captured_urls, 
           sizeof(JsCapturedUrl) * out_result->captured_count);
    
    JS_FreeContext(exec_ctx);
    
    return success;
}

int js_quickjs_get_captured_urls(JsCapturedUrl *out_urls, int max_urls) {
    if (!out_urls || max_urls <= 0) return 0;
    
    int count = (max_urls < g_captured_count) ? max_urls : g_captured_count;
    memcpy(out_urls, g_captured_urls, sizeof(JsCapturedUrl) * count);
    return count;
}

void js_quickjs_clear_captured_urls(void) {
    g_captured_count = 0;
    memset(g_captured_urls, 0, sizeof(g_captured_urls));
}

/* The find and decrypt function that will be called after all scripts are loaded */
static char *find_and_decrypt_signature(JSContext *decrypt_ctx, const char *encrypted_sig, size_t *out_len) {
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
        "    if (!/^[A-Za-z0-9_=-]+$/.test(s)) return false;"
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
    
    return NULL;
}

char *js_quickjs_decrypt_signature_ex(const char *player_js, size_t player_js_len,
                                       const char **additional_scripts, const size_t *additional_lens,
                                       int additional_count,
                                       const char *encrypted_sig, size_t *out_len,
                                       JsExecResult *out_result) {
    if (!ctx && !js_quickjs_init()) {
        *out_len = 0;
        return NULL;
    }
    
    if (!player_js || player_js_len == 0 || !encrypted_sig) {
        *out_len = 0;
        return NULL;
    }
    
    LOGI("Running player JS with %d additional scripts...", additional_count);
    
    JSContext *decrypt_ctx = JS_NewContext(rt);
    if (!decrypt_ctx) {
        *out_len = 0;
        return NULL;
    }
    
    /* Clear captured URLs */
    g_captured_count = 0;
    if (out_result) {
        memset(out_result, 0, sizeof(JsExecResult));
    }
    
    /* Install browser stubs */
    JS_Eval(decrypt_ctx, browser_stubs, strlen(browser_stubs), "<stubs>", 0);
    clear_captured_urls_js(decrypt_ctx);
    
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
    LOGI("Player JS executed successfully");
    
    /* Execute additional scripts */
    for (int i = 0; i < additional_count; i++) {
        if (!additional_scripts[i] || additional_lens[i] == 0) {
            continue;
        }
        
        char filename[64];
        snprintf(filename, sizeof(filename), "additional%d.js", i);
        
        LOGI("Executing additional script %d/%d (%zu bytes)...", i + 1, additional_count, additional_lens[i]);
        
        JSValue add_result = JS_Eval(decrypt_ctx, additional_scripts[i], additional_lens[i], filename, 0);
        
        if (JS_IsException(add_result)) {
            JSValue exc = JS_GetException(decrypt_ctx);
            const char *err = JS_ToCString(decrypt_ctx, exc);
            LOGE("Additional script %d error: %s", i, err ? err : "unknown");
            JS_FreeCString(decrypt_ctx, err);
            JS_FreeValue(decrypt_ctx, exc);
            /* Continue with other scripts */
        } else {
            LOGI("Additional script %d executed successfully", i + 1);
        }
        
        JS_FreeValue(decrypt_ctx, add_result);
    }
    
    /* Extract captured URLs before finding the decrypt function */
    if (out_result) {
        out_result->captured_count = extract_captured_urls(decrypt_ctx, out_result->captured_urls,
                                                            JS_CAPTURED_URLS_MAX);
        g_captured_count = out_result->captured_count;
        memcpy(g_captured_urls, out_result->captured_urls,
               sizeof(JsCapturedUrl) * out_result->captured_count);
        
        if (out_result->captured_count > 0) {
            LOGI("Captured %d URLs during JS execution", out_result->captured_count);
            for (int i = 0; i < out_result->captured_count && i < 5; i++) {
                LOGI("  URL %d: %.100s...", i, out_result->captured_urls[i].url);
            }
        }
    }
    
    /* Find and call the decipher function */
    char *decrypted = find_and_decrypt_signature(decrypt_ctx, encrypted_sig, out_len);
    
    JS_FreeContext(decrypt_ctx);
    
    if (decrypted) {
        return decrypted;
    }
    
    /* Fallback */
    size_t len = strlen(encrypted_sig);
    char *fallback = malloc(len + 1);
    if (fallback) {
        memcpy(fallback, encrypted_sig, len + 1);
        *out_len = len;
    }
    return fallback;
}

char *js_quickjs_decrypt_signature(const char *player_js, size_t player_js_len,
                                    const char *encrypted_sig, size_t *out_len) {
    /* Call the extended version with no additional scripts */
    return js_quickjs_decrypt_signature_ex(player_js, player_js_len,
                                            NULL, NULL, 0,
                                            encrypted_sig, out_len, NULL);
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
