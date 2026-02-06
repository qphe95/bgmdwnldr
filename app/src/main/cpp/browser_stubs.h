/*
 * Browser Stubs - C implementation of DOM/Browser APIs for QuickJS
 */
#ifndef BROWSER_STUBS_H
#define BROWSER_STUBS_H

#include <quickjs.h>

// Initialize all browser stubs
void init_browser_stubs(JSContext *ctx, JSValue global);

#endif // BROWSER_STUBS_H
