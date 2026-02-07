/*
 * Browser Stubs - C implementation of DOM/Browser APIs for QuickJS
 */
#ifndef BROWSER_STUBS_H
#define BROWSER_STUBS_H

#include <quickjs.h>

// Class IDs for new APIs
extern JSClassID js_shadow_root_class_id;
extern JSClassID js_animation_class_id;
extern JSClassID js_keyframe_effect_class_id;
extern JSClassID js_font_face_class_id;
extern JSClassID js_font_face_set_class_id;

// Initialize all browser stubs
void init_browser_stubs(JSContext *ctx, JSValue global);

#endif // BROWSER_STUBS_H
