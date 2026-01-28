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
    
    /* Extract captured URLs */
    JsCapturedUrl local_captured[JS_CAPTURED_URLS_MAX];
    int captured_count = extract_captured_urls(decrypt_ctx, local_captured, JS_CAPTURED_URLS_MAX);
    
    if (out_result) {
        out_result->captured_count = captured_count;
        memcpy(out_result->captured_urls, local_captured, sizeof(JsCapturedUrl) * captured_count);
    }
    g_captured_count = captured_count;
    memcpy(g_captured_urls, local_captured, sizeof(JsCapturedUrl) * captured_count);
    
    if (captured_count > 0) {
        LOGI("Captured %d URLs during JS execution", captured_count);
        for (int i = 0; i < captured_count && i < 5; i++) {
            LOGI("  URL %d: %.100s...", i, local_captured[i].url);
        }
    }
    
    JS_FreeContext(decrypt_ctx);
    
    /* 
     * Check captured URLs for a valid googlevideo URL with decrypted signature.
     * The player JS may have made an XHR/fetch request with the decrypted URL.
     */
    for (int i = 0; i < captured_count; i++) {
        const char *url = local_captured[i].url;
        
        /* Check if it's a googlevideo URL */
        if (strstr(url, "googlevideo.com") == NULL) {
            continue;
        }
        
        /* Look for sig or signature parameter */
        const char *sig_start = strstr(url, "sig=");
        if (!sig_start) {
            sig_start = strstr(url, "signature=");
        }
        if (!sig_start) {
            continue;
        }
        
        /* Extract the signature value */
        sig_start = strchr(sig_start, '=');
        if (!sig_start) continue;
        sig_start++; /* Move past '=' */
        
        const char *sig_end = strchr(sig_start, '&');
        size_t sig_len = sig_end ? (size_t)(sig_end - sig_start) : strlen(sig_start);
        
        /* Sanity check: signature should be reasonable length */
        if (sig_len < 20 || sig_len > 500) {
            continue;
        }
        
        /* Check if this signature is different from input (decrypted) */
        if (sig_len != strlen(encrypted_sig) || 
            strncmp(sig_start, encrypted_sig, sig_len) != 0) {
            /* Found a different signature - likely decrypted */
            char *decrypted_sig = malloc(sig_len + 1);
            if (decrypted_sig) {
                memcpy(decrypted_sig, sig_start, sig_len);
                decrypted_sig[sig_len] = '\0';
                *out_len = sig_len;
                LOGI("Found decrypted signature in captured URL %d: %.50s...", i, decrypted_sig);
                return decrypted_sig;
            }
        }
    }
    
    /* No valid decrypted signature found in captured URLs, return original */
    size_t len = strlen(encrypted_sig);
    char *result = malloc(len + 1);
    if (result) {
        memcpy(result, encrypted_sig, len + 1);
        *out_len = len;
        LOGI("No decrypted signature found in captured URLs, returning original");
    }
    return result;
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
