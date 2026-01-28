#ifndef JS_QUICKJS_H
#define JS_QUICKJS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize QuickJS runtime */
bool js_quickjs_init(void);

/* Cleanup QuickJS runtime */
void js_quickjs_cleanup(void);

/* Evaluate JavaScript code and return result (caller must free result) */
char *js_quickjs_eval(const char *code, size_t code_len, size_t *out_len);

/* Decrypt YouTube signature using player JS (returns malloc'd string, caller must free) */
char *js_quickjs_decrypt_signature(const char *player_js, size_t player_js_len,
                                    const char *encrypted_sig, size_t *out_len);

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
