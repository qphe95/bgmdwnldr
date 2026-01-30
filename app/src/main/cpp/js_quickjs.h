#ifndef JS_QUICKJS_H
#define JS_QUICKJS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of captured URLs */
#define JS_MAX_CAPTURED_URLS 64
#define JS_MAX_URL_LEN 2048

typedef enum {
    JS_EXEC_SUCCESS = 0,
    JS_EXEC_ERROR = -1,
    JS_EXEC_TIMEOUT = -2
} JsExecStatus;

/* Result of JS execution with captured URLs */
typedef struct JsExecResult {
    JsExecStatus status;
    int captured_url_count;
    char captured_urls[JS_MAX_CAPTURED_URLS][JS_MAX_URL_LEN];
} JsExecResult;

/* Initialize QuickJS runtime */
bool js_quickjs_init(void);

/* Cleanup QuickJS runtime */
void js_quickjs_cleanup(void);

/* Execute multiple JS scripts with ytInitialPlayerResponse injected
 * scripts: array of JS code strings
 * script_lens: array of script lengths  
 * script_count: number of scripts
 * player_response: ytInitialPlayerResponse JSON string (can be NULL)
 * html: original HTML content for parsing video elements (can be NULL)
 * out_result: output structure for captured URLs
 */
bool js_quickjs_exec_scripts_with_data(const char **scripts, const size_t *script_lens, 
                                       int script_count, const char *player_response,
                                       const char *html, JsExecResult *out_result);

/* Get captured URLs from global storage (for backward compatibility) */
int js_quickjs_get_captured_urls(char urls[][JS_MAX_URL_LEN], int max_urls);

#ifdef __cplusplus
}
#endif

#endif /* JS_QUICKJS_H */
