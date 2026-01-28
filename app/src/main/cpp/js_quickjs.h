#ifndef JS_QUICKJS_H
#define JS_QUICKJS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of captured URLs */
#define JS_CAPTURED_URLS_MAX 64
#define JS_CAPTURED_URL_LEN 1024

/* Captured URL entry */
typedef struct JsCapturedUrl {
    char url[JS_CAPTURED_URL_LEN];
    char method[16];
    char type[16];
} JsCapturedUrl;

/* Result of JS execution with captured URLs */
typedef struct JsExecResult {
    char *result;
    size_t result_len;
    JsCapturedUrl captured_urls[JS_CAPTURED_URLS_MAX];
    int captured_count;
} JsExecResult;

/* Initialize QuickJS runtime */
bool js_quickjs_init(void);

/* Cleanup QuickJS runtime */
void js_quickjs_cleanup(void);

/* Evaluate JavaScript code and return result (caller must free result) */
char *js_quickjs_eval(const char *code, size_t code_len, size_t *out_len);

/* Execute multiple JS scripts and get captured URLs */
/* scripts: array of JS code strings */
/* script_lens: array of script lengths */
/* script_count: number of scripts */
/* out_result: output structure for result and captured URLs (must be freed by caller if result is set) */
bool js_quickjs_exec_scripts(const char **scripts, const size_t *script_lens, int script_count,
                              JsExecResult *out_result);

/* Get captured URLs from the last JS execution context */
/* Returns number of URLs captured */
int js_quickjs_get_captured_urls(JsCapturedUrl *out_urls, int max_urls);

/* Clear captured URLs */
void js_quickjs_clear_captured_urls(void);

/* Decrypt YouTube signature using player JS (returns malloc'd string, caller must free) */
char *js_quickjs_decrypt_signature(const char *player_js, size_t player_js_len,
                                    const char *encrypted_sig, size_t *out_len);

/* Decrypt signature with additional scripts to execute */
/* additional_scripts: array of additional JS code to execute after player_js */
/* additional_lens: array of script lengths */
/* additional_count: number of additional scripts */
char *js_quickjs_decrypt_signature_ex(const char *player_js, size_t player_js_len,
                                       const char **additional_scripts, const size_t *additional_lens,
                                       int additional_count,
                                       const char *encrypted_sig, size_t *out_len,
                                       JsExecResult *out_result);

/* Simple signature decryption (writes to provided buffer) */
bool js_quickjs_decrypt_signature_simple(const char *player_js, const char *encrypted_sig,
                                          char *out_decrypted, size_t out_len);

/* Decrypt n-parameter for throttling bypass */
bool js_quickjs_decrypt_nparam(const char *player_js, const char *n_param,
                                char *out_decrypted, size_t out_len);

#ifdef __cplusplus
}
#endif

#endif /* JS_QUICKJS_H */
