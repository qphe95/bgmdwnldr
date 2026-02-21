#ifndef JS_QUICKJS_H
#define JS_QUICKJS_H

#include <stdbool.h>
#include <stddef.h>

/* QuickJS headers */
#include "quickjs.h"

/* Forward declaration for Android AssetManager */
struct AAssetManager;
typedef struct AAssetManager AAssetManager;

#ifdef __cplusplus
extern "C" {
#endif

/* Global QuickJS runtime and context - initialized once in android_main */
extern JSRuntime *g_js_runtime;
extern JSContext *g_js_context;

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

/* Initialize QuickJS runtime (called once in android_main) */
bool js_quickjs_init(void);

/* Create global QuickJS runtime and context (called once in android_main after js_quickjs_init) */
bool js_quickjs_create_runtime(void);

/* Set up initial DOM state (called once in android_main after js_quickjs_create_runtime) */
void js_quickjs_setup_initial_dom(void);

/* Reset class IDs (called during GC full reset) */
void js_quickjs_reset_class_ids(void);

/* Set the Android asset manager for loading browser stubs */
void js_quickjs_set_asset_manager(AAssetManager *mgr);

/* Cleanup QuickJS runtime (not needed - runtime lives for app lifetime) */
void js_quickjs_cleanup(void);

/* Execute multiple JS scripts in a browser-like environment
 * Data payload scripts (ytInitialPlayerResponse, ytInitialData, etc.) will
 * execute naturally and define global variables, just like in a real browser.
 * 
 * scripts: array of JS code strings
 * script_lens: array of script lengths  
 * script_count: number of scripts
 * html: original HTML content for parsing video elements (can be NULL)
 * asset_mgr: Android asset manager for loading browser stubs (can be NULL)
 * out_result: output structure for captured URLs
 */
bool js_quickjs_exec_scripts(const char **scripts, const size_t *script_lens, 
                             int script_count, const char *html, 
                             AAssetManager *asset_mgr,
                             JsExecResult *out_result);

/* Get captured URLs from global storage (for backward compatibility) */
int js_quickjs_get_captured_urls(char urls[][JS_MAX_URL_LEN], int max_urls);

#ifdef __cplusplus
}
#endif

#endif /* JS_QUICKJS_H */
