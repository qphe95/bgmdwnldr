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

/* Execute multiple JS scripts in a browser-like environment
 * Data payload scripts (ytInitialPlayerResponse, ytInitialData, etc.) will
 * execute naturally and define global variables, just like in a real browser.
 * 
 * scripts: array of JS code strings
 * script_lens: array of script lengths  
 * script_count: number of scripts
 * html: original HTML content for parsing video elements (can be NULL)
 * out_result: output structure for captured URLs
 */
bool js_quickjs_exec_scripts(const char **scripts, const size_t *script_lens, 
                             int script_count, const char *html, 
                             JsExecResult *out_result);

/* Get captured URLs from global storage (for backward compatibility) */
int js_quickjs_get_captured_urls(char urls[][JS_MAX_URL_LEN], int max_urls);

/* Get ytInitialPlayerResponse JSON from JS context after script execution
 * Returns: malloc'd string containing the JSON, or NULL if not available
 * Caller must free the returned string
 */
char* js_quickjs_get_player_response(void);

#ifdef __cplusplus
}
#endif

#endif /* JS_QUICKJS_H */
