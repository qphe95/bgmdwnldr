/*
 * Browser Stubs - C implementation of DOM/Browser APIs for QuickJS
 */
/*
 * Browser Stubs - C implementation of DOM/Browser APIs for QuickJS
 */
#include <string.h>
#include <stdlib.h>
#include <quickjs.h>
#include "browser_stubs.h"

// External symbols from js_quickjs.c
extern JSValue js_document_create_element(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

// Forward declarations for internal functions
static JSValue js_dummy_function(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_dummy_function_true(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

// Basic stub function definitions (must be before use in function lists)
static JSValue js_undefined(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return JS_UNDEFINED;
}

static JSValue js_null(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return JS_NULL;
}

static JSValue js_empty_array(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewArray(ctx);
}

static JSValue js_empty_string(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewString(ctx, "");
}

static JSValue js_false(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return JS_FALSE;
}

static JSValue js_true(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return JS_TRUE;
}

static JSValue js_zero(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewInt32(ctx, 0);
}

static JSValue js_console_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return JS_UNDEFINED;
}

static JSValue js_dummy_function_true(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return JS_TRUE;
}

// Generic dummy function that returns undefined
static JSValue js_dummy_function(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    return JS_UNDEFINED;
}
extern JSClassID js_xhr_class_id;
extern JSClassID js_video_class_id;
extern JSValue js_xhr_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv);
extern JSValue js_video_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv);
extern JSValue js_fetch(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
extern const JSCFunctionListEntry js_xhr_proto_funcs[];
extern const JSCFunctionListEntry js_video_proto_funcs[];
extern const size_t js_xhr_proto_funcs_count;
extern const size_t js_video_proto_funcs_count;

// Class IDs for new APIs
JSClassID js_shadow_root_class_id = 0;
JSClassID js_animation_class_id = 0;
JSClassID js_keyframe_effect_class_id = 0;
JSClassID js_font_face_class_id = 0;
JSClassID js_font_face_set_class_id = 0;
JSClassID js_custom_element_registry_class_id = 0;
JSClassID js_mutation_observer_class_id = 0;
JSClassID js_resize_observer_class_id = 0;
JSClassID js_intersection_observer_class_id = 0;
JSClassID js_performance_class_id = 0;
JSClassID js_performance_entry_class_id = 0;
JSClassID js_performance_observer_class_id = 0;
JSClassID js_dom_rect_class_id = 0;
JSClassID js_dom_rect_read_only_class_id = 0;

// Helper macros
#define DEF_FUNC(ctx, parent, name, func, argc) \
    JS_SetPropertyStr(ctx, parent, name, JS_NewCFunction(ctx, func, name, argc))

#define DEF_PROP_STR(ctx, obj, name, value) \
    JS_SetPropertyStr(ctx, obj, name, JS_NewString(ctx, value))

#define DEF_PROP_INT(ctx, obj, name, value) \
    JS_SetPropertyStr(ctx, obj, name, JS_NewInt32(ctx, value))

#define DEF_PROP_BOOL(ctx, obj, name, value) \
    JS_SetPropertyStr(ctx, obj, name, JS_NewBool(ctx, value))

#define DEF_PROP_FLOAT(ctx, obj, name, value) \
    JS_SetPropertyStr(ctx, obj, name, JS_NewFloat64(ctx, value))

#define DEF_PROP_UNDEFINED(ctx, obj, name) \
    JS_SetPropertyStr(ctx, obj, name, JS_UNDEFINED)

// Helper to create a resolved Promise
static JSValue js_create_resolved_promise(JSContext *ctx, JSValue value) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue promise_ctor = JS_GetPropertyStr(ctx, global, "Promise");
    JSValue resolve_func = JS_GetPropertyStr(ctx, promise_ctor, "resolve");
    JSValue result = JS_Call(ctx, resolve_func, JS_UNDEFINED, 1, &value);
    JS_FreeValue(ctx, resolve_func);
    JS_FreeValue(ctx, promise_ctor);
    JS_FreeValue(ctx, global);
    return result;
}

// Helper to create an empty resolved Promise
static JSValue js_create_empty_resolved_promise(JSContext *ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue promise_ctor = JS_GetPropertyStr(ctx, global, "Promise");
    JSValue resolve_func = JS_GetPropertyStr(ctx, promise_ctor, "resolve");
    JSValue result = JS_Call(ctx, resolve_func, JS_UNDEFINED, 0, NULL);
    JS_FreeValue(ctx, resolve_func);
    JS_FreeValue(ctx, promise_ctor);
    JS_FreeValue(ctx, global);
    return result;
}

// ============================================================================
// Shadow DOM Implementation
// ============================================================================

typedef struct {
    JSValue host;           // The element that hosts this shadow root
    char mode[16];          // "open" or "closed"
    JSValue innerHTML;      // Shadow root content (as string for stub)
} ShadowRootData;

static void js_shadow_root_finalizer(JSRuntime *rt, JSValue val) {
    ShadowRootData *sr = JS_GetOpaque(val, js_shadow_root_class_id);
    if (sr) {
        JS_FreeValueRT(rt, sr->host);
        JS_FreeValueRT(rt, sr->innerHTML);
        free(sr);
    }
}

static JSClassDef js_shadow_root_class_def = {
    "ShadowRoot",
    .finalizer = js_shadow_root_finalizer,
};

static JSValue js_shadow_root_get_host(JSContext *ctx, JSValueConst this_val) {
    ShadowRootData *sr = JS_GetOpaque2(ctx, this_val, js_shadow_root_class_id);
    if (!sr) return JS_EXCEPTION;
    return JS_DupValue(ctx, sr->host);
}

static JSValue js_shadow_root_get_mode(JSContext *ctx, JSValueConst this_val) {
    ShadowRootData *sr = JS_GetOpaque2(ctx, this_val, js_shadow_root_class_id);
    if (!sr) return JS_EXCEPTION;
    return JS_NewString(ctx, sr->mode);
}

static JSValue js_shadow_root_get_innerHTML(JSContext *ctx, JSValueConst this_val) {
    ShadowRootData *sr = JS_GetOpaque2(ctx, this_val, js_shadow_root_class_id);
    if (!sr) return JS_EXCEPTION;
    return JS_DupValue(ctx, sr->innerHTML);
}

static JSValue js_shadow_root_set_innerHTML(JSContext *ctx, JSValueConst this_val, JSValueConst val) {
    ShadowRootData *sr = JS_GetOpaque2(ctx, this_val, js_shadow_root_class_id);
    if (!sr) return JS_EXCEPTION;
    JS_FreeValue(ctx, sr->innerHTML);
    sr->innerHTML = JS_DupValue(ctx, val);
    return JS_UNDEFINED;
}

static JSValue js_shadow_root_querySelector(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NULL;
}

static JSValue js_shadow_root_querySelectorAll(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NewArray(ctx);
}

static JSValue js_shadow_root_getElementById(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NULL;
}

static const JSCFunctionListEntry js_shadow_root_proto_funcs[] = {
    JS_CGETSET_DEF("host", js_shadow_root_get_host, NULL),
    JS_CGETSET_DEF("mode", js_shadow_root_get_mode, NULL),
    JS_CGETSET_DEF("innerHTML", js_shadow_root_get_innerHTML, js_shadow_root_set_innerHTML),
    JS_CFUNC_DEF("querySelector", 1, js_shadow_root_querySelector),
    JS_CFUNC_DEF("querySelectorAll", 1, js_shadow_root_querySelectorAll),
    JS_CFUNC_DEF("getElementById", 1, js_shadow_root_getElementById),
    JS_CFUNC_DEF("addEventListener", 2, js_dummy_function),
    JS_CFUNC_DEF("removeEventListener", 2, js_dummy_function),
    JS_CFUNC_DEF("dispatchEvent", 1, js_dummy_function_true),
    JS_PROP_STRING_DEF("nodeType", "11", JS_PROP_ENUMERABLE),  // DOCUMENT_FRAGMENT_NODE
};

// Element.prototype.attachShadow()
static JSValue js_element_attach_shadow(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1 || !JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(ctx, "attachShadow requires an init object");
    }
    
    // Get mode from init object
    JSValue mode_val = JS_GetPropertyStr(ctx, argv[0], "mode");
    const char *mode = JS_ToCString(ctx, mode_val);
    if (!mode) mode = "closed";
    
    // Create ShadowRoot instance
    ShadowRootData *sr = calloc(1, sizeof(ShadowRootData));
    if (!sr) {
        JS_FreeCString(ctx, mode);
        JS_FreeValue(ctx, mode_val);
        return JS_EXCEPTION;
    }
    
    strncpy(sr->mode, mode, sizeof(sr->mode) - 1);
    sr->mode[sizeof(sr->mode) - 1] = '\0';
    sr->host = JS_DupValue(ctx, this_val);
    sr->innerHTML = JS_NewString(ctx, "");
    
    JSValue shadow_root = JS_NewObjectClass(ctx, js_shadow_root_class_id);
    JS_SetOpaque(shadow_root, sr);
    
    // Store shadowRoot reference on the element (internal property __shadowRoot)
    JS_SetPropertyStr(ctx, this_val, "__shadowRoot", JS_DupValue(ctx, shadow_root));
    
    JS_FreeCString(ctx, mode);
    JS_FreeValue(ctx, mode_val);
    
    return shadow_root;
}

// Element.prototype.shadowRoot getter
static JSValue js_element_get_shadow_root(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    JSValue shadow = JS_GetPropertyStr(ctx, this_val, "__shadowRoot");
    if (JS_IsUndefined(shadow)) {
        JS_FreeValue(ctx, shadow);
        return JS_NULL;
    }
    
    // Check if mode is "open" - if closed, return null
    ShadowRootData *sr = JS_GetOpaque(shadow, js_shadow_root_class_id);
    if (sr && strcmp(sr->mode, "closed") == 0) {
        JS_FreeValue(ctx, shadow);
        return JS_NULL;
    }
    
    return shadow;
}

// Element.prototype.querySelector
static JSValue js_element_querySelector(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NULL;
}

// Element.prototype.querySelectorAll
static JSValue js_element_querySelectorAll(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NewArray(ctx);
}

// ============================================================================
// EventTarget Implementation
// ============================================================================

static JSValue js_event_target_addEventListener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    // Store event handlers on the element for dispatch
    if (argc < 2) return JS_UNDEFINED;
    
    const char *event = JS_ToCString(ctx, argv[0]);
    if (event) {
        char prop[128];
        snprintf(prop, sizeof(prop), "__on%s", event);
        JS_SetPropertyStr(ctx, this_val, prop, JS_DupValue(ctx, argv[1]));
    }
    JS_FreeCString(ctx, event);
    return JS_UNDEFINED;
}

static JSValue js_event_target_removeEventListener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    // Stub - just return undefined
    return JS_UNDEFINED;
}

static JSValue js_event_target_dispatchEvent(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_TRUE;
}

// ============================================================================
// Node Implementation
// ============================================================================

static JSValue js_node_appendChild(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_NULL;
    // Return the appended child
    return JS_DupValue(ctx, argv[0]);
}

static JSValue js_node_insertBefore(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_NULL;
    return JS_DupValue(ctx, argv[0]);
}

static JSValue js_node_removeChild(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_NULL;
    return JS_DupValue(ctx, argv[0]);
}

static JSValue js_node_cloneNode(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    // Return a new empty object as cloned node
    return JS_NewObject(ctx);
}

static JSValue js_node_contains(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_FALSE;
}

// ============================================================================
// Custom Elements API Implementation
// ============================================================================

typedef struct {
    JSValue registry;  // Map of tag names to constructor functions
} CustomElementRegistryData;

static void js_custom_element_registry_finalizer(JSRuntime *rt, JSValue val) {
    CustomElementRegistryData *cer = JS_GetOpaque(val, js_custom_element_registry_class_id);
    if (cer) {
        JS_FreeValueRT(rt, cer->registry);
        free(cer);
    }
}

static JSClassDef js_custom_element_registry_class_def = {
    "CustomElementRegistry",
    .finalizer = js_custom_element_registry_finalizer,
};

// customElements.define(name, constructor, options)
static JSValue js_custom_elements_define(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "define requires at least 2 arguments");
    }
    
    const char *name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_EXCEPTION;
    
    // Validate name format (must contain hyphen)
    if (strchr(name, '-') == NULL) {
        JS_FreeCString(ctx, name);
        return JS_ThrowTypeError(ctx, "Custom element name must contain a hyphen");
    }
    
    // Store in registry (the this_val should be the customElements object)
    JS_SetPropertyStr(ctx, this_val, name, JS_DupValue(ctx, argv[1]));
    
    JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
}

// customElements.get(name)
static JSValue js_custom_elements_get(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_UNDEFINED;
    
    const char *name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_UNDEFINED;
    
    JSValue ctor = JS_GetPropertyStr(ctx, this_val, name);
    
    JS_FreeCString(ctx, name);
    
    if (JS_IsUndefined(ctor)) {
        return JS_UNDEFINED;
    }
    return ctor;
}

// customElements.whenDefined(name)
static JSValue js_custom_elements_when_defined(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    // Return a resolved promise
    return js_create_empty_resolved_promise(ctx);
}

// ============================================================================
// Web Animations API Implementation
// ============================================================================

typedef struct {
    double current_time;
    double duration;
    int play_state;  // 0=idle, 1=running, 2=paused, 3=finished
    JSValue onfinish;
    JSValue effect;
    JSContext *ctx;
} AnimationData;

typedef struct {
    JSValue target;
    JSValue keyframes;
    double duration;
    char easing[32];
} KeyFrameEffectData;

static void js_animation_finalizer(JSRuntime *rt, JSValue val) {
    AnimationData *anim = JS_GetOpaque(val, js_animation_class_id);
    if (anim) {
        JS_FreeValueRT(rt, anim->onfinish);
        JS_FreeValueRT(rt, anim->effect);
        free(anim);
    }
}

static void js_keyframe_effect_finalizer(JSRuntime *rt, JSValue val) {
    KeyFrameEffectData *effect = JS_GetOpaque(val, js_keyframe_effect_class_id);
    if (effect) {
        JS_FreeValueRT(rt, effect->target);
        JS_FreeValueRT(rt, effect->keyframes);
        free(effect);
    }
}

static JSClassDef js_animation_class_def = {
    "Animation",
    .finalizer = js_animation_finalizer,
};

static JSClassDef js_keyframe_effect_class_def = {
    "KeyframeEffect",
    .finalizer = js_keyframe_effect_finalizer,
};

// Animation constructor
static JSValue js_animation_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    AnimationData *anim = calloc(1, sizeof(AnimationData));
    if (!anim) return JS_EXCEPTION;
    
    anim->ctx = ctx;
    anim->current_time = 0;
    anim->duration = 0;
    anim->play_state = 0;  // idle
    anim->onfinish = JS_NULL;
    anim->effect = JS_NULL;
    
    if (argc > 0) {
        anim->effect = JS_DupValue(ctx, argv[0]);
        // Try to get duration from effect
        if (JS_IsObject(argv[0])) {
            JSValue duration_val = JS_GetPropertyStr(ctx, argv[0], "duration");
            double duration;
            if (!JS_IsException(duration_val) && !JS_ToFloat64(ctx, &duration, duration_val)) {
                anim->duration = duration;
            }
            JS_FreeValue(ctx, duration_val);
        }
    }
    
    JSValue obj = JS_NewObjectClass(ctx, js_animation_class_id);
    JS_SetOpaque(obj, anim);
    return obj;
}

// Animation.prototype.play()
static JSValue js_animation_play(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AnimationData *anim = JS_GetOpaque2(ctx, this_val, js_animation_class_id);
    if (!anim) return JS_EXCEPTION;
    anim->play_state = 1;  // running
    return JS_UNDEFINED;
}

// Animation.prototype.pause()
static JSValue js_animation_pause(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AnimationData *anim = JS_GetOpaque2(ctx, this_val, js_animation_class_id);
    if (!anim) return JS_EXCEPTION;
    anim->play_state = 2;  // paused
    return JS_UNDEFINED;
}

// Animation.prototype.finish()
static JSValue js_animation_finish(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AnimationData *anim = JS_GetOpaque2(ctx, this_val, js_animation_class_id);
    if (!anim) return JS_EXCEPTION;
    anim->play_state = 3;  // finished
    anim->current_time = anim->duration;
    
    // Call onfinish callback if set
    if (!JS_IsNull(anim->onfinish) && JS_IsFunction(ctx, anim->onfinish)) {
        JS_Call(ctx, anim->onfinish, this_val, 0, NULL);
    }
    return JS_UNDEFINED;
}

// Animation.prototype.cancel()
static JSValue js_animation_cancel(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    AnimationData *anim = JS_GetOpaque2(ctx, this_val, js_animation_class_id);
    if (!anim) return JS_EXCEPTION;
    anim->play_state = 0;  // idle
    anim->current_time = 0;
    return JS_UNDEFINED;
}

// Animation.prototype.reverse()
static JSValue js_animation_reverse(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

// Animation.playState getter
static JSValue js_animation_get_play_state(JSContext *ctx, JSValueConst this_val) {
    AnimationData *anim = JS_GetOpaque2(ctx, this_val, js_animation_class_id);
    if (!anim) return JS_EXCEPTION;
    const char *states[] = {"idle", "running", "paused", "finished"};
    return JS_NewString(ctx, states[anim->play_state]);
}

// Animation.currentTime getter
static JSValue js_animation_get_current_time(JSContext *ctx, JSValueConst this_val) {
    AnimationData *anim = JS_GetOpaque2(ctx, this_val, js_animation_class_id);
    if (!anim) return JS_EXCEPTION;
    return JS_NewFloat64(ctx, anim->current_time);
}

// Animation.effect getter
static JSValue js_animation_get_effect(JSContext *ctx, JSValueConst this_val) {
    AnimationData *anim = JS_GetOpaque2(ctx, this_val, js_animation_class_id);
    if (!anim) return JS_EXCEPTION;
    if (JS_IsNull(anim->effect)) return JS_NULL;
    return JS_DupValue(ctx, anim->effect);
}

// Animation.onfinish getter/setter
static JSValue js_animation_get_onfinish(JSContext *ctx, JSValueConst this_val) {
    AnimationData *anim = JS_GetOpaque2(ctx, this_val, js_animation_class_id);
    if (!anim) return JS_EXCEPTION;
    return JS_DupValue(ctx, anim->onfinish);
}

static JSValue js_animation_set_onfinish(JSContext *ctx, JSValueConst this_val, JSValueConst val) {
    AnimationData *anim = JS_GetOpaque2(ctx, this_val, js_animation_class_id);
    if (!anim) return JS_EXCEPTION;
    JS_FreeValue(ctx, anim->onfinish);
    anim->onfinish = JS_DupValue(ctx, val);
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry js_animation_proto_funcs[] = {
    JS_CFUNC_DEF("play", 0, js_animation_play),
    JS_CFUNC_DEF("pause", 0, js_animation_pause),
    JS_CFUNC_DEF("finish", 0, js_animation_finish),
    JS_CFUNC_DEF("cancel", 0, js_animation_cancel),
    JS_CFUNC_DEF("reverse", 0, js_animation_reverse),
    JS_CGETSET_DEF("playState", js_animation_get_play_state, NULL),
    JS_CGETSET_DEF("currentTime", js_animation_get_current_time, NULL),
    JS_CGETSET_DEF("effect", js_animation_get_effect, NULL),
    JS_CGETSET_DEF("onfinish", js_animation_get_onfinish, js_animation_set_onfinish),
};

// KeyframeEffect constructor
static JSValue js_keyframe_effect_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    KeyFrameEffectData *effect = calloc(1, sizeof(KeyFrameEffectData));
    if (!effect) return JS_EXCEPTION;
    
    effect->target = JS_NULL;
    effect->keyframes = JS_NULL;
    effect->duration = 0;
    strcpy(effect->easing, "linear");
    
    if (argc > 0) {
        effect->target = JS_DupValue(ctx, argv[0]);
    }
    if (argc > 1) {
        effect->keyframes = JS_DupValue(ctx, argv[1]);
    }
    if (argc > 2 && JS_IsObject(argv[2])) {
        JSValue duration_val = JS_GetPropertyStr(ctx, argv[2], "duration");
        double duration;
        if (!JS_IsException(duration_val) && !JS_ToFloat64(ctx, &duration, duration_val)) {
            effect->duration = duration;
        }
        JS_FreeValue(ctx, duration_val);
        
        JSValue easing_val = JS_GetPropertyStr(ctx, argv[2], "easing");
        const char *easing = JS_ToCString(ctx, easing_val);
        if (easing) {
            strncpy(effect->easing, easing, sizeof(effect->easing) - 1);
            effect->easing[sizeof(effect->easing) - 1] = '\0';
        }
        JS_FreeCString(ctx, easing);
        JS_FreeValue(ctx, easing_val);
    }
    
    JSValue obj = JS_NewObjectClass(ctx, js_keyframe_effect_class_id);
    JS_SetOpaque(obj, effect);
    return obj;
}

// KeyframeEffect.target getter
static JSValue js_keyframe_effect_get_target(JSContext *ctx, JSValueConst this_val) {
    KeyFrameEffectData *effect = JS_GetOpaque2(ctx, this_val, js_keyframe_effect_class_id);
    if (!effect) return JS_EXCEPTION;
    if (JS_IsNull(effect->target)) return JS_NULL;
    return JS_DupValue(ctx, effect->target);
}

// KeyframeEffect.duration getter
static JSValue js_keyframe_effect_get_duration(JSContext *ctx, JSValueConst this_val) {
    KeyFrameEffectData *effect = JS_GetOpaque2(ctx, this_val, js_keyframe_effect_class_id);
    if (!effect) return JS_EXCEPTION;
    return JS_NewFloat64(ctx, effect->duration);
}

static const JSCFunctionListEntry js_keyframe_effect_proto_funcs[] = {
    JS_CGETSET_DEF("target", js_keyframe_effect_get_target, NULL),
    JS_CGETSET_DEF("duration", js_keyframe_effect_get_duration, NULL),
};

// Element.prototype.animate(keyframes, options)
static JSValue js_element_animate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    // Create KeyframeEffect
    JSValue effect_args[3];
    effect_args[0] = JS_DupValue(ctx, this_val);  // target
    effect_args[1] = argc > 0 ? JS_DupValue(ctx, argv[0]) : JS_NULL;  // keyframes
    effect_args[2] = argc > 1 ? JS_DupValue(ctx, argv[1]) : JS_NULL;  // options
    
    JSValue effect = js_keyframe_effect_constructor(ctx, JS_UNDEFINED, 3, effect_args);
    
    JS_FreeValue(ctx, effect_args[0]);
    JS_FreeValue(ctx, effect_args[1]);
    JS_FreeValue(ctx, effect_args[2]);
    
    if (JS_IsException(effect)) {
        return effect;
    }
    
    // Create Animation with the effect
    JSValue anim_args[1];
    anim_args[0] = effect;
    JSValue animation = js_animation_constructor(ctx, JS_UNDEFINED, 1, anim_args);
    JS_FreeValue(ctx, effect);
    
    if (JS_IsException(animation)) {
        return animation;
    }
    
    // Auto-play the animation
    AnimationData *anim = JS_GetOpaque(animation, js_animation_class_id);
    if (anim) {
        anim->play_state = 1;  // running
    }
    
    return animation;
}

// ============================================================================
// Font Loading API Implementation
// ============================================================================

typedef struct {
    char family[256];
    char source[512];
    char display[32];
} FontFaceData;

typedef struct {
    JSValue loaded_fonts;  // Array of loaded FontFace objects
} FontFaceSetData;

static void js_font_face_finalizer(JSRuntime *rt, JSValue val) {
    FontFaceData *ff = JS_GetOpaque(val, js_font_face_class_id);
    if (ff) {
        free(ff);
    }
}

static void js_font_face_set_finalizer(JSRuntime *rt, JSValue val) {
    FontFaceSetData *ffs = JS_GetOpaque(val, js_font_face_set_class_id);
    if (ffs) {
        JS_FreeValueRT(rt, ffs->loaded_fonts);
        free(ffs);
    }
}

static JSClassDef js_font_face_class_def = {
    "FontFace",
    .finalizer = js_font_face_finalizer,
};

static JSClassDef js_font_face_set_class_def = {
    "FontFaceSet",
    .finalizer = js_font_face_set_finalizer,
};

// FontFace constructor
static JSValue js_font_face_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    FontFaceData *ff = calloc(1, sizeof(FontFaceData));
    if (!ff) return JS_EXCEPTION;
    
    if (argc > 0) {
        const char *family = JS_ToCString(ctx, argv[0]);
        if (family) {
            strncpy(ff->family, family, sizeof(ff->family) - 1);
        }
        JS_FreeCString(ctx, family);
    }
    
    if (argc > 1) {
        const char *source = JS_ToCString(ctx, argv[1]);
        if (source) {
            strncpy(ff->source, source, sizeof(ff->source) - 1);
        }
        JS_FreeCString(ctx, source);
    }
    
    if (argc > 2 && JS_IsObject(argv[2])) {
        JSValue display_val = JS_GetPropertyStr(ctx, argv[2], "display");
        const char *display = JS_ToCString(ctx, display_val);
        if (display) {
            strncpy(ff->display, display, sizeof(ff->display) - 1);
        }
        JS_FreeCString(ctx, display);
        JS_FreeValue(ctx, display_val);
    }
    
    JSValue obj = JS_NewObjectClass(ctx, js_font_face_class_id);
    JS_SetOpaque(obj, ff);
    return obj;
}

// FontFace.load()
static JSValue js_font_face_load(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    // Return a resolved promise with this FontFace
    JSValue this_dup = JS_DupValue(ctx, this_val);
    JSValue result = js_create_resolved_promise(ctx, this_dup);
    JS_FreeValue(ctx, this_dup);
    return result;
}

// FontFace.loaded getter - returns a Promise that resolves to this FontFace
static JSValue js_font_face_get_loaded(JSContext *ctx, JSValueConst this_val) {
    FontFaceData *ff = JS_GetOpaque2(ctx, this_val, js_font_face_class_id);
    if (!ff) return JS_EXCEPTION;
    // Return a resolved promise with this FontFace
    JSValue this_dup = JS_DupValue(ctx, this_val);
    JSValue result = js_create_resolved_promise(ctx, this_dup);
    JS_FreeValue(ctx, this_dup);
    return result;
}

// FontFace.family getter
static JSValue js_font_face_get_family(JSContext *ctx, JSValueConst this_val) {
    FontFaceData *ff = JS_GetOpaque2(ctx, this_val, js_font_face_class_id);
    if (!ff) return JS_EXCEPTION;
    return JS_NewString(ctx, ff->family);
}

// FontFace.status getter
static JSValue js_font_face_get_status(JSContext *ctx, JSValueConst this_val) {
    return JS_NewString(ctx, "loaded");
}

static const JSCFunctionListEntry js_font_face_proto_funcs[] = {
    JS_CFUNC_DEF("load", 0, js_font_face_load),
    JS_CGETSET_DEF("family", js_font_face_get_family, NULL),
    JS_CGETSET_DEF("status", js_font_face_get_status, NULL),
    JS_CGETSET_DEF("loaded", js_font_face_get_loaded, NULL),  // Now returns a proper Promise
};

// FontFaceSet.load(fontSpec)
static JSValue js_font_face_set_load(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    // Return a resolved promise with empty array (all fonts "loaded")
    JSValue empty_array = JS_NewArray(ctx);
    JSValue result = js_create_resolved_promise(ctx, empty_array);
    JS_FreeValue(ctx, empty_array);
    return result;
}

// FontFaceSet.check(fontSpec)
static JSValue js_font_face_set_check(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    // Always return true (fonts are available)
    return JS_TRUE;
}

// FontFaceSet.ready getter
static JSValue js_font_face_set_get_ready(JSContext *ctx, JSValueConst this_val) {
    // Return a resolved promise
    return js_create_empty_resolved_promise(ctx);
}

// FontFaceSet.status getter
static JSValue js_font_face_set_get_status(JSContext *ctx, JSValueConst this_val) {
    return JS_NewString(ctx, "loaded");
}

// FontFaceSet.add(fontFace)
static JSValue js_font_face_set_add(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

// FontFaceSet.delete(fontFace)
static JSValue js_font_face_set_delete(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_TRUE;
}

// FontFaceSet.clear()
static JSValue js_font_face_set_clear(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

// FontFaceSet.has(fontFace)
static JSValue js_font_face_set_has(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_TRUE;
}

// FontFaceSet.forEach(callback)
static JSValue js_font_face_set_forEach(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

// FontFaceSet[Symbol.iterator]()
static JSValue js_font_face_set_iterator(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue array_ctor = JS_GetPropertyStr(ctx, global, "Array");
    JSValue empty_array = JS_CallConstructor(ctx, array_ctor, 0, NULL);
    JSValue result = JS_GetPropertyStr(ctx, empty_array, "values");
    JSValue iterator = JS_Call(ctx, result, empty_array, 0, NULL);
    JS_FreeValue(ctx, result);
    JS_FreeValue(ctx, empty_array);
    JS_FreeValue(ctx, array_ctor);
    JS_FreeValue(ctx, global);
    return iterator;
}

static const JSCFunctionListEntry js_font_face_set_proto_funcs[] = {
    JS_CFUNC_DEF("load", 1, js_font_face_set_load),
    JS_CFUNC_DEF("check", 1, js_font_face_set_check),
    JS_CGETSET_DEF("ready", js_font_face_set_get_ready, NULL),
    JS_CGETSET_DEF("status", js_font_face_set_get_status, NULL),
    JS_CFUNC_DEF("add", 1, js_font_face_set_add),
    JS_CFUNC_DEF("delete", 1, js_font_face_set_delete),
    JS_CFUNC_DEF("clear", 0, js_font_face_set_clear),
    JS_CFUNC_DEF("has", 1, js_font_face_set_has),
    JS_CFUNC_DEF("forEach", 1, js_font_face_set_forEach),
    JS_CFUNC_DEF("values", 0, js_font_face_set_iterator),
    JS_CFUNC_DEF("keys", 0, js_font_face_set_iterator),
    JS_CFUNC_DEF("entries", 0, js_font_face_set_iterator),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_font_face_set_iterator),
};

// ============================================================================
// MutationObserver Implementation
// ============================================================================

typedef struct {
    JSValue callback;
    JSContext *ctx;
} MutationObserverData;

static void js_mutation_observer_finalizer(JSRuntime *rt, JSValue val) {
    MutationObserverData *mo = JS_GetOpaque(val, js_mutation_observer_class_id);
    if (mo) {
        JS_FreeValueRT(rt, mo->callback);
        free(mo);
    }
}

static JSClassDef js_mutation_observer_class_def = {
    "MutationObserver",
    .finalizer = js_mutation_observer_finalizer,
};

// MutationObserver constructor
static JSValue js_mutation_observer_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "MutationObserver constructor requires a callback function");
    }
    
    MutationObserverData *mo = calloc(1, sizeof(MutationObserverData));
    if (!mo) return JS_EXCEPTION;
    
    mo->ctx = ctx;
    mo->callback = JS_DupValue(ctx, argv[0]);
    
    JSValue obj = JS_NewObjectClass(ctx, js_mutation_observer_class_id);
    JS_SetOpaque(obj, mo);
    return obj;
}

// MutationObserver.prototype.observe(target, options)
static JSValue js_mutation_observer_observe(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

// MutationObserver.prototype.disconnect()
static JSValue js_mutation_observer_disconnect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

// MutationObserver.prototype.takeRecords()
static JSValue js_mutation_observer_takeRecords(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NewArray(ctx);
}

static const JSCFunctionListEntry js_mutation_observer_proto_funcs[] = {
    JS_CFUNC_DEF("observe", 2, js_mutation_observer_observe),
    JS_CFUNC_DEF("disconnect", 0, js_mutation_observer_disconnect),
    JS_CFUNC_DEF("takeRecords", 0, js_mutation_observer_takeRecords),
};

// ============================================================================
// ResizeObserver Implementation
// ============================================================================

typedef struct {
    JSValue callback;
    JSContext *ctx;
} ResizeObserverData;

static void js_resize_observer_finalizer(JSRuntime *rt, JSValue val) {
    ResizeObserverData *ro = JS_GetOpaque(val, js_resize_observer_class_id);
    if (ro) {
        JS_FreeValueRT(rt, ro->callback);
        free(ro);
    }
}

static JSClassDef js_resize_observer_class_def = {
    "ResizeObserver",
    .finalizer = js_resize_observer_finalizer,
};

// ResizeObserver constructor
static JSValue js_resize_observer_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "ResizeObserver constructor requires a callback function");
    }
    
    ResizeObserverData *ro = calloc(1, sizeof(ResizeObserverData));
    if (!ro) return JS_EXCEPTION;
    
    ro->ctx = ctx;
    ro->callback = JS_DupValue(ctx, argv[0]);
    
    JSValue obj = JS_NewObjectClass(ctx, js_resize_observer_class_id);
    JS_SetOpaque(obj, ro);
    return obj;
}

// ResizeObserver.prototype.observe(target)
static JSValue js_resize_observer_observe(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

// ResizeObserver.prototype.unobserve(target)
static JSValue js_resize_observer_unobserve(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

// ResizeObserver.prototype.disconnect()
static JSValue js_resize_observer_disconnect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry js_resize_observer_proto_funcs[] = {
    JS_CFUNC_DEF("observe", 1, js_resize_observer_observe),
    JS_CFUNC_DEF("unobserve", 1, js_resize_observer_unobserve),
    JS_CFUNC_DEF("disconnect", 0, js_resize_observer_disconnect),
};

// ============================================================================
// IntersectionObserver Implementation
// ============================================================================

typedef struct {
    JSValue callback;
    JSValue root;
    char rootMargin[32];
    double threshold;
    JSContext *ctx;
} IntersectionObserverData;

static void js_intersection_observer_finalizer(JSRuntime *rt, JSValue val) {
    IntersectionObserverData *io = JS_GetOpaque(val, js_intersection_observer_class_id);
    if (io) {
        JS_FreeValueRT(rt, io->callback);
        JS_FreeValueRT(rt, io->root);
        free(io);
    }
}

static JSClassDef js_intersection_observer_class_def = {
    "IntersectionObserver",
    .finalizer = js_intersection_observer_finalizer,
};

// IntersectionObserver constructor
static JSValue js_intersection_observer_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "IntersectionObserver constructor requires a callback function");
    }
    
    IntersectionObserverData *io = calloc(1, sizeof(IntersectionObserverData));
    if (!io) return JS_EXCEPTION;
    
    io->ctx = ctx;
    io->callback = JS_DupValue(ctx, argv[0]);
    io->root = JS_NULL;
    strcpy(io->rootMargin, "0px");
    io->threshold = 0.0;
    
    // Parse options if provided
    if (argc > 1 && JS_IsObject(argv[1])) {
        JSValue root_val = JS_GetPropertyStr(ctx, argv[1], "root");
        if (!JS_IsUndefined(root_val) && !JS_IsNull(root_val)) {
            io->root = JS_DupValue(ctx, root_val);
        }
        JS_FreeValue(ctx, root_val);
        
        JSValue margin_val = JS_GetPropertyStr(ctx, argv[1], "rootMargin");
        const char *margin = JS_ToCString(ctx, margin_val);
        if (margin) {
            strncpy(io->rootMargin, margin, sizeof(io->rootMargin) - 1);
            io->rootMargin[sizeof(io->rootMargin) - 1] = '\0';
        }
        JS_FreeCString(ctx, margin);
        JS_FreeValue(ctx, margin_val);
        
        JSValue threshold_val = JS_GetPropertyStr(ctx, argv[1], "threshold");
        JS_ToFloat64(ctx, &io->threshold, threshold_val);
        JS_FreeValue(ctx, threshold_val);
    }
    
    JSValue obj = JS_NewObjectClass(ctx, js_intersection_observer_class_id);
    JS_SetOpaque(obj, io);
    return obj;
}

// IntersectionObserver.prototype.observe(target)
static JSValue js_intersection_observer_observe(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

// IntersectionObserver.prototype.unobserve(target)
static JSValue js_intersection_observer_unobserve(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

// IntersectionObserver.prototype.disconnect()
static JSValue js_intersection_observer_disconnect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

// IntersectionObserver.prototype.takeRecords()
static JSValue js_intersection_observer_takeRecords(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NewArray(ctx);
}

static const JSCFunctionListEntry js_intersection_observer_proto_funcs[] = {
    JS_CFUNC_DEF("observe", 1, js_intersection_observer_observe),
    JS_CFUNC_DEF("unobserve", 1, js_intersection_observer_unobserve),
    JS_CFUNC_DEF("disconnect", 0, js_intersection_observer_disconnect),
    JS_CFUNC_DEF("takeRecords", 0, js_intersection_observer_takeRecords),
};

// ============================================================================
// Performance API Implementation
// ============================================================================

typedef struct {
    double start_time;
} PerformanceData;

typedef struct {
    char name[256];
    char entryType[64];
    double startTime;
    double duration;
} PerformanceEntryData;

typedef struct {
    JSValue callback;
    JSContext *ctx;
} PerformanceObserverData;

static void js_performance_finalizer(JSRuntime *rt, JSValue val) {
    PerformanceData *perf = JS_GetOpaque(val, js_performance_class_id);
    if (perf) {
        free(perf);
    }
}

static void js_performance_entry_finalizer(JSRuntime *rt, JSValue val) {
    PerformanceEntryData *entry = JS_GetOpaque(val, js_performance_entry_class_id);
    if (entry) {
        free(entry);
    }
}

static void js_performance_observer_finalizer(JSRuntime *rt, JSValue val) {
    PerformanceObserverData *po = JS_GetOpaque(val, js_performance_observer_class_id);
    if (po) {
        JS_FreeValueRT(rt, po->callback);
        free(po);
    }
}

static JSClassDef js_performance_class_def = {
    "Performance",
    .finalizer = js_performance_finalizer,
};

static JSClassDef js_performance_entry_class_def = {
    "PerformanceEntry",
    .finalizer = js_performance_entry_finalizer,
};

static JSClassDef js_performance_observer_class_def = {
    "PerformanceObserver",
    .finalizer = js_performance_observer_finalizer,
};

// Performance.now()
static double g_performance_time = 0.0;

static JSValue js_performance_now(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    // Return a monotonically increasing timestamp (in milliseconds)
    // Use a static counter since we don't need actual wall-clock time
    g_performance_time += 0.1;  // Increment slightly on each call
    return JS_NewFloat64(ctx, g_performance_time);
}

// Performance.timeOrigin getter
static JSValue js_performance_get_time_origin(JSContext *ctx, JSValueConst this_val) {
    return JS_NewFloat64(ctx, 0.0);
}

// Performance.getEntries()
static JSValue js_performance_get_entries(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NewArray(ctx);
}

// Performance.getEntriesByType(type)
static JSValue js_performance_get_entries_by_type(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NewArray(ctx);
}

// Performance.getEntriesByName(name, type)
static JSValue js_performance_get_entries_by_name(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NewArray(ctx);
}

// Performance.mark(name)
static JSValue js_performance_mark(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

// Performance.measure(name, startMark, endMark)
static JSValue js_performance_measure(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

// Performance.clearMarks(name)
static JSValue js_performance_clear_marks(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

// Performance.clearMeasures(name)
static JSValue js_performance_clear_measures(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry js_performance_proto_funcs[] = {
    JS_CFUNC_DEF("now", 0, js_performance_now),
    JS_CGETSET_DEF("timeOrigin", js_performance_get_time_origin, NULL),
    JS_CFUNC_DEF("getEntries", 0, js_performance_get_entries),
    JS_CFUNC_DEF("getEntriesByType", 1, js_performance_get_entries_by_type),
    JS_CFUNC_DEF("getEntriesByName", 1, js_performance_get_entries_by_name),
    JS_CFUNC_DEF("mark", 1, js_performance_mark),
    JS_CFUNC_DEF("measure", 1, js_performance_measure),
    JS_CFUNC_DEF("clearMarks", 0, js_performance_clear_marks),
    JS_CFUNC_DEF("clearMeasures", 0, js_performance_clear_measures),
};

// PerformanceEntry.name getter
static JSValue js_performance_entry_get_name(JSContext *ctx, JSValueConst this_val) {
    PerformanceEntryData *entry = JS_GetOpaque2(ctx, this_val, js_performance_entry_class_id);
    if (!entry) return JS_EXCEPTION;
    return JS_NewString(ctx, entry->name);
}

// PerformanceEntry.entryType getter
static JSValue js_performance_entry_get_entry_type(JSContext *ctx, JSValueConst this_val) {
    PerformanceEntryData *entry = JS_GetOpaque2(ctx, this_val, js_performance_entry_class_id);
    if (!entry) return JS_EXCEPTION;
    return JS_NewString(ctx, entry->entryType);
}

// PerformanceEntry.startTime getter
static JSValue js_performance_entry_get_start_time(JSContext *ctx, JSValueConst this_val) {
    PerformanceEntryData *entry = JS_GetOpaque2(ctx, this_val, js_performance_entry_class_id);
    if (!entry) return JS_EXCEPTION;
    return JS_NewFloat64(ctx, entry->startTime);
}

// PerformanceEntry.duration getter
static JSValue js_performance_entry_get_duration(JSContext *ctx, JSValueConst this_val) {
    PerformanceEntryData *entry = JS_GetOpaque2(ctx, this_val, js_performance_entry_class_id);
    if (!entry) return JS_EXCEPTION;
    return JS_NewFloat64(ctx, entry->duration);
}

static const JSCFunctionListEntry js_performance_entry_proto_funcs[] = {
    JS_CGETSET_DEF("name", js_performance_entry_get_name, NULL),
    JS_CGETSET_DEF("entryType", js_performance_entry_get_entry_type, NULL),
    JS_CGETSET_DEF("startTime", js_performance_entry_get_start_time, NULL),
    JS_CGETSET_DEF("duration", js_performance_entry_get_duration, NULL),
};

// PerformanceObserver constructor
static JSValue js_performance_observer_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "PerformanceObserver constructor requires a callback function");
    }
    
    PerformanceObserverData *po = calloc(1, sizeof(PerformanceObserverData));
    if (!po) return JS_EXCEPTION;
    
    po->ctx = ctx;
    po->callback = JS_DupValue(ctx, argv[0]);
    
    JSValue obj = JS_NewObjectClass(ctx, js_performance_observer_class_id);
    JS_SetOpaque(obj, po);
    return obj;
}

// PerformanceObserver.prototype.observe(options)
static JSValue js_performance_observer_observe(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

// PerformanceObserver.prototype.disconnect()
static JSValue js_performance_observer_disconnect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

// PerformanceObserver.prototype.takeRecords()
static JSValue js_performance_observer_takeRecords(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NewArray(ctx);
}

// PerformanceObserver.supportedEntryTypes getter
static JSValue js_performance_observer_get_supported_entry_types(JSContext *ctx, JSValueConst this_val) {
    // Return an array of supported entry types
    JSValue array = JS_NewArray(ctx);
    return array;
}

static const JSCFunctionListEntry js_performance_observer_proto_funcs[] = {
    JS_CFUNC_DEF("observe", 1, js_performance_observer_observe),
    JS_CFUNC_DEF("disconnect", 0, js_performance_observer_disconnect),
    JS_CFUNC_DEF("takeRecords", 0, js_performance_observer_takeRecords),
    JS_CGETSET_DEF("supportedEntryTypes", js_performance_observer_get_supported_entry_types, NULL),
};

// ============================================================================
// DOMRect Implementation
// ============================================================================

typedef struct {
    double x;
    double y;
    double width;
    double height;
    double top;
    double right;
    double bottom;
    double left;
} DOMRectData;

static void js_dom_rect_finalizer(JSRuntime *rt, JSValue val) {
    DOMRectData *rect = JS_GetOpaque(val, js_dom_rect_class_id);
    if (rect) {
        free(rect);
    }
}

static void js_dom_rect_read_only_finalizer(JSRuntime *rt, JSValue val) {
    DOMRectData *rect = JS_GetOpaque(val, js_dom_rect_read_only_class_id);
    if (rect) {
        free(rect);
    }
}

static JSClassDef js_dom_rect_class_def = {
    "DOMRect",
    .finalizer = js_dom_rect_finalizer,
};

static JSClassDef js_dom_rect_read_only_class_def = {
    "DOMRectReadOnly",
    .finalizer = js_dom_rect_read_only_finalizer,
};

// DOMRect constructor
static JSValue js_dom_rect_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    DOMRectData *rect = calloc(1, sizeof(DOMRectData));
    if (!rect) return JS_EXCEPTION;
    
    rect->x = 0;
    rect->y = 0;
    rect->width = 0;
    rect->height = 0;
    rect->top = 0;
    rect->right = 0;
    rect->bottom = 0;
    rect->left = 0;
    
    if (argc > 0) JS_ToFloat64(ctx, &rect->x, argv[0]);
    if (argc > 1) JS_ToFloat64(ctx, &rect->y, argv[1]);
    if (argc > 2) JS_ToFloat64(ctx, &rect->width, argv[2]);
    if (argc > 3) JS_ToFloat64(ctx, &rect->height, argv[3]);
    
    rect->left = rect->x;
    rect->top = rect->y;
    rect->right = rect->x + rect->width;
    rect->bottom = rect->y + rect->height;
    
    JSValue obj = JS_NewObjectClass(ctx, js_dom_rect_class_id);
    JS_SetOpaque(obj, rect);
    return obj;
}

// DOMRectReadOnly constructor
static JSValue js_dom_rect_read_only_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    DOMRectData *rect = calloc(1, sizeof(DOMRectData));
    if (!rect) return JS_EXCEPTION;
    
    rect->x = 0;
    rect->y = 0;
    rect->width = 0;
    rect->height = 0;
    rect->top = 0;
    rect->right = 0;
    rect->bottom = 0;
    rect->left = 0;
    
    if (argc > 0) JS_ToFloat64(ctx, &rect->x, argv[0]);
    if (argc > 1) JS_ToFloat64(ctx, &rect->y, argv[1]);
    if (argc > 2) JS_ToFloat64(ctx, &rect->width, argv[2]);
    if (argc > 3) JS_ToFloat64(ctx, &rect->height, argv[3]);
    
    rect->left = rect->x;
    rect->top = rect->y;
    rect->right = rect->x + rect->width;
    rect->bottom = rect->y + rect->height;
    
    JSValue obj = JS_NewObjectClass(ctx, js_dom_rect_read_only_class_id);
    JS_SetOpaque(obj, rect);
    return obj;
}

// DOMRect.fromRect(other)
static JSValue js_dom_rect_from_rect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    double x = 0, y = 0, width = 0, height = 0;
    
    if (argc > 0 && JS_IsObject(argv[0])) {
        JSValue x_val = JS_GetPropertyStr(ctx, argv[0], "x");
        JSValue y_val = JS_GetPropertyStr(ctx, argv[0], "y");
        JSValue w_val = JS_GetPropertyStr(ctx, argv[0], "width");
        JSValue h_val = JS_GetPropertyStr(ctx, argv[0], "height");
        
        JS_ToFloat64(ctx, &x, x_val);
        JS_ToFloat64(ctx, &y, y_val);
        JS_ToFloat64(ctx, &width, w_val);
        JS_ToFloat64(ctx, &height, h_val);
        
        JS_FreeValue(ctx, x_val);
        JS_FreeValue(ctx, y_val);
        JS_FreeValue(ctx, w_val);
        JS_FreeValue(ctx, h_val);
    }
    
    JSValue args[4] = {
        JS_NewFloat64(ctx, x),
        JS_NewFloat64(ctx, y),
        JS_NewFloat64(ctx, width),
        JS_NewFloat64(ctx, height)
    };
    
    JSValue result = js_dom_rect_constructor(ctx, JS_UNDEFINED, 4, args);
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    JS_FreeValue(ctx, args[2]);
    JS_FreeValue(ctx, args[3]);
    
    return result;
}

// DOMRectReadOnly.fromRect(other)
static JSValue js_dom_rect_read_only_from_rect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    double x = 0, y = 0, width = 0, height = 0;
    
    if (argc > 0 && JS_IsObject(argv[0])) {
        JSValue x_val = JS_GetPropertyStr(ctx, argv[0], "x");
        JSValue y_val = JS_GetPropertyStr(ctx, argv[0], "y");
        JSValue w_val = JS_GetPropertyStr(ctx, argv[0], "width");
        JSValue h_val = JS_GetPropertyStr(ctx, argv[0], "height");
        
        JS_ToFloat64(ctx, &x, x_val);
        JS_ToFloat64(ctx, &y, y_val);
        JS_ToFloat64(ctx, &width, w_val);
        JS_ToFloat64(ctx, &height, h_val);
        
        JS_FreeValue(ctx, x_val);
        JS_FreeValue(ctx, y_val);
        JS_FreeValue(ctx, w_val);
        JS_FreeValue(ctx, h_val);
    }
    
    JSValue args[4] = {
        JS_NewFloat64(ctx, x),
        JS_NewFloat64(ctx, y),
        JS_NewFloat64(ctx, width),
        JS_NewFloat64(ctx, height)
    };
    
    JSValue result = js_dom_rect_read_only_constructor(ctx, JS_UNDEFINED, 4, args);
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    JS_FreeValue(ctx, args[2]);
    JS_FreeValue(ctx, args[3]);
    
    return result;
}

#define DEF_DOM_RECT_GETTER(name, field) \
static JSValue js_dom_rect_get_##name(JSContext *ctx, JSValueConst this_val) { \
    DOMRectData *rect = JS_GetOpaque2(ctx, this_val, js_dom_rect_class_id); \
    if (!rect) { \
        rect = JS_GetOpaque2(ctx, this_val, js_dom_rect_read_only_class_id); \
        if (!rect) return JS_EXCEPTION; \
    } \
    return JS_NewFloat64(ctx, rect->field); \
}

DEF_DOM_RECT_GETTER(x, x)
DEF_DOM_RECT_GETTER(y, y)
DEF_DOM_RECT_GETTER(width, width)
DEF_DOM_RECT_GETTER(height, height)
DEF_DOM_RECT_GETTER(top, top)
DEF_DOM_RECT_GETTER(right, right)
DEF_DOM_RECT_GETTER(bottom, bottom)
DEF_DOM_RECT_GETTER(left, left)

#undef DEF_DOM_RECT_GETTER

#define DEF_DOM_RECT_SETTER(name, field) \
static JSValue js_dom_rect_set_##name(JSContext *ctx, JSValueConst this_val, JSValueConst val) { \
    DOMRectData *rect = JS_GetOpaque2(ctx, this_val, js_dom_rect_class_id); \
    if (!rect) return JS_EXCEPTION; \
    JS_ToFloat64(ctx, &rect->field, val); \
    /* Update dependent fields */ \
    if (strcmp(#field, "x") == 0) { rect->left = rect->x; rect->right = rect->x + rect->width; } \
    if (strcmp(#field, "y") == 0) { rect->top = rect->y; rect->bottom = rect->y + rect->height; } \
    if (strcmp(#field, "width") == 0) { rect->right = rect->x + rect->width; } \
    if (strcmp(#field, "height") == 0) { rect->bottom = rect->y + rect->height; } \
    return JS_UNDEFINED; \
}

DEF_DOM_RECT_SETTER(x, x)
DEF_DOM_RECT_SETTER(y, y)
DEF_DOM_RECT_SETTER(width, width)
DEF_DOM_RECT_SETTER(height, height)

#undef DEF_DOM_RECT_SETTER

static const JSCFunctionListEntry js_dom_rect_proto_funcs[] = {
    JS_CGETSET_DEF("x", js_dom_rect_get_x, js_dom_rect_set_x),
    JS_CGETSET_DEF("y", js_dom_rect_get_y, js_dom_rect_set_y),
    JS_CGETSET_DEF("width", js_dom_rect_get_width, js_dom_rect_set_width),
    JS_CGETSET_DEF("height", js_dom_rect_get_height, js_dom_rect_set_height),
    JS_CGETSET_DEF("top", js_dom_rect_get_top, NULL),
    JS_CGETSET_DEF("right", js_dom_rect_get_right, NULL),
    JS_CGETSET_DEF("bottom", js_dom_rect_get_bottom, NULL),
    JS_CGETSET_DEF("left", js_dom_rect_get_left, NULL),
    JS_CFUNC_DEF("toJSON", 0, js_empty_string),
};

static const JSCFunctionListEntry js_dom_rect_read_only_proto_funcs[] = {
    JS_CGETSET_DEF("x", js_dom_rect_get_x, NULL),
    JS_CGETSET_DEF("y", js_dom_rect_get_y, NULL),
    JS_CGETSET_DEF("width", js_dom_rect_get_width, NULL),
    JS_CGETSET_DEF("height", js_dom_rect_get_height, NULL),
    JS_CGETSET_DEF("top", js_dom_rect_get_top, NULL),
    JS_CGETSET_DEF("right", js_dom_rect_get_right, NULL),
    JS_CGETSET_DEF("bottom", js_dom_rect_get_bottom, NULL),
    JS_CGETSET_DEF("left", js_dom_rect_get_left, NULL),
    JS_CFUNC_DEF("toJSON", 0, js_empty_string),
};

// ============================================================================
// Main Initialization
// ============================================================================

void init_browser_stubs(JSContext *ctx, JSValue global) {
    // ===== Initialize Class IDs =====
    JS_NewClassID(&js_shadow_root_class_id);
    JS_NewClassID(&js_animation_class_id);
    JS_NewClassID(&js_keyframe_effect_class_id);
    JS_NewClassID(&js_font_face_class_id);
    JS_NewClassID(&js_font_face_set_class_id);
    JS_NewClassID(&js_custom_element_registry_class_id);
    JS_NewClassID(&js_mutation_observer_class_id);
    JS_NewClassID(&js_resize_observer_class_id);
    JS_NewClassID(&js_intersection_observer_class_id);
    JS_NewClassID(&js_performance_class_id);
    JS_NewClassID(&js_performance_entry_class_id);
    JS_NewClassID(&js_performance_observer_class_id);
    JS_NewClassID(&js_dom_rect_class_id);
    JS_NewClassID(&js_dom_rect_read_only_class_id);
    
    // Register classes with the runtime
    JSRuntime *rt = JS_GetRuntime(ctx);
    JS_NewClass(rt, js_shadow_root_class_id, &js_shadow_root_class_def);
    JS_NewClass(rt, js_animation_class_id, &js_animation_class_def);
    JS_NewClass(rt, js_keyframe_effect_class_id, &js_keyframe_effect_class_def);
    JS_NewClass(rt, js_font_face_class_id, &js_font_face_class_def);
    JS_NewClass(rt, js_font_face_set_class_id, &js_font_face_set_class_def);
    JS_NewClass(rt, js_custom_element_registry_class_id, &js_custom_element_registry_class_def);
    JS_NewClass(rt, js_mutation_observer_class_id, &js_mutation_observer_class_def);
    JS_NewClass(rt, js_resize_observer_class_id, &js_resize_observer_class_def);
    JS_NewClass(rt, js_intersection_observer_class_id, &js_intersection_observer_class_def);
    JS_NewClass(rt, js_performance_class_id, &js_performance_class_def);
    JS_NewClass(rt, js_performance_entry_class_id, &js_performance_entry_class_def);
    JS_NewClass(rt, js_performance_observer_class_id, &js_performance_observer_class_def);
    JS_NewClass(rt, js_dom_rect_class_id, &js_dom_rect_class_def);
    JS_NewClass(rt, js_dom_rect_read_only_class_id, &js_dom_rect_read_only_class_def);
    
    // ===== Window (global object itself) =====
    // window IS the global object - this ensures 'this' at global level refers to window
    JSValue window = global;  // Use global object as window (no new object created)
    
    DEF_PROP_INT(ctx, window, "innerWidth", 1920);
    DEF_PROP_INT(ctx, window, "innerHeight", 1080);
    DEF_PROP_INT(ctx, window, "outerWidth", 1920);
    DEF_PROP_INT(ctx, window, "outerHeight", 1080);
    DEF_PROP_INT(ctx, window, "screenX", 0);
    DEF_PROP_INT(ctx, window, "screenY", 0);
    DEF_PROP_FLOAT(ctx, window, "devicePixelRatio", 1.0);
    DEF_FUNC(ctx, window, "setTimeout", js_zero, 2);
    DEF_FUNC(ctx, window, "setInterval", js_zero, 2);
    DEF_FUNC(ctx, window, "clearTimeout", js_undefined, 1);
    DEF_FUNC(ctx, window, "clearInterval", js_undefined, 1);
    DEF_FUNC(ctx, window, "requestAnimationFrame", js_zero, 1);
    DEF_FUNC(ctx, window, "cancelAnimationFrame", js_undefined, 1);
    DEF_FUNC(ctx, window, "alert", js_undefined, 1);
    DEF_FUNC(ctx, window, "confirm", js_true, 0);
    DEF_FUNC(ctx, window, "prompt", js_empty_string, 1);
    DEF_FUNC(ctx, window, "open", js_null, 1);
    DEF_FUNC(ctx, window, "close", js_undefined, 0);
    DEF_FUNC(ctx, window, "focus", js_undefined, 0);
    DEF_FUNC(ctx, window, "blur", js_undefined, 0);
    DEF_FUNC(ctx, window, "scrollTo", js_undefined, 2);
    DEF_FUNC(ctx, window, "scrollBy", js_undefined, 2);
    DEF_FUNC(ctx, window, "postMessage", js_undefined, 2);
    DEF_FUNC(ctx, window, "addEventListener", js_undefined, 2);
    DEF_FUNC(ctx, window, "removeEventListener", js_undefined, 2);
    DEF_FUNC(ctx, window, "dispatchEvent", js_true, 1);
    
    // Set up window to reference itself (global object)
    JS_SetPropertyStr(ctx, window, "window", JS_DupValue(ctx, window));
    JS_SetPropertyStr(ctx, window, "self", JS_DupValue(ctx, window));
    JS_SetPropertyStr(ctx, window, "top", JS_DupValue(ctx, window));
    JS_SetPropertyStr(ctx, window, "parent", JS_DupValue(ctx, window));
    // globalThis also points to the same object (global = window)
    JS_SetPropertyStr(ctx, window, "globalThis", JS_DupValue(ctx, window));
    
    // ===== Document =====
    JSValue document = JS_NewObject(ctx);
    DEF_PROP_INT(ctx, document, "nodeType", 9);
    DEF_PROP_STR(ctx, document, "readyState", "complete");
    DEF_PROP_STR(ctx, document, "characterSet", "UTF-8");
    DEF_PROP_STR(ctx, document, "charset", "UTF-8");
    DEF_PROP_STR(ctx, document, "contentType", "text/html");
    DEF_PROP_STR(ctx, document, "referrer", "https://www.youtube.com/");
    DEF_PROP_STR(ctx, document, "cookie", "");
    DEF_PROP_STR(ctx, document, "domain", "www.youtube.com");
    DEF_FUNC(ctx, document, "createElement", js_document_create_element, 1);
    DEF_FUNC(ctx, document, "createElementNS", js_document_create_element, 2);
    DEF_FUNC(ctx, document, "createTextNode", js_empty_string, 1);
    DEF_FUNC(ctx, document, "createComment", js_empty_string, 1);
    DEF_FUNC(ctx, document, "createDocumentFragment", js_null, 0);
    DEF_FUNC(ctx, document, "getElementById", js_null, 1);
    DEF_FUNC(ctx, document, "querySelector", js_null, 1);
    DEF_FUNC(ctx, document, "querySelectorAll", js_empty_array, 1);
    DEF_FUNC(ctx, document, "getElementsByTagName", js_empty_array, 1);
    DEF_FUNC(ctx, document, "getElementsByClassName", js_empty_array, 1);
    DEF_FUNC(ctx, document, "getElementsByName", js_empty_array, 1);
    DEF_FUNC(ctx, document, "addEventListener", js_undefined, 2);
    DEF_FUNC(ctx, document, "removeEventListener", js_undefined, 2);
    DEF_FUNC(ctx, document, "dispatchEvent", js_true, 1);
    JS_SetPropertyStr(ctx, global, "document", document);
    JS_SetPropertyStr(ctx, document, "defaultView", JS_DupValue(ctx, window));
    
    // ===== Location =====
    JSValue location = JS_NewObject(ctx);
    DEF_PROP_STR(ctx, location, "href", "https://www.youtube.com/watch?v=dQw4w9WgXcQ");
    DEF_PROP_STR(ctx, location, "protocol", "https:");
    DEF_PROP_STR(ctx, location, "host", "www.youtube.com");
    DEF_PROP_STR(ctx, location, "hostname", "www.youtube.com");
    DEF_PROP_STR(ctx, location, "port", "");
    DEF_PROP_STR(ctx, location, "pathname", "/watch");
    DEF_PROP_STR(ctx, location, "search", "?v=dQw4w9WgXcQ");
    DEF_PROP_STR(ctx, location, "hash", "");
    DEF_PROP_STR(ctx, location, "origin", "https://www.youtube.com");
    DEF_FUNC(ctx, location, "toString", js_empty_string, 0);
    JS_SetPropertyStr(ctx, window, "location", location);
    JS_SetPropertyStr(ctx, document, "location", JS_DupValue(ctx, location));
    
    // ===== Navigator =====
    JSValue navigator = JS_NewObject(ctx);
    DEF_PROP_STR(ctx, navigator, "userAgent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    DEF_PROP_STR(ctx, navigator, "appName", "Netscape");
    DEF_PROP_STR(ctx, navigator, "appVersion", "5.0 (X11; Linux x86_64) AppleWebKit/537.36");
    DEF_PROP_STR(ctx, navigator, "appCodeName", "Mozilla");
    DEF_PROP_STR(ctx, navigator, "platform", "Linux x86_64");
    DEF_PROP_STR(ctx, navigator, "product", "Gecko");
    DEF_PROP_STR(ctx, navigator, "productSub", "20030107");
    DEF_PROP_STR(ctx, navigator, "vendor", "Google Inc.");
    DEF_PROP_STR(ctx, navigator, "vendorSub", "");
    DEF_PROP_STR(ctx, navigator, "language", "en-US");
    DEF_PROP_BOOL(ctx, navigator, "onLine", 1);
    DEF_PROP_BOOL(ctx, navigator, "cookieEnabled", 1);
    DEF_PROP_INT(ctx, navigator, "hardwareConcurrency", 8);
    DEF_PROP_INT(ctx, navigator, "maxTouchPoints", 0);
    DEF_PROP_BOOL(ctx, navigator, "pdfViewerEnabled", 1);
    DEF_PROP_BOOL(ctx, navigator, "webdriver", 0);
    JS_SetPropertyStr(ctx, window, "navigator", navigator);
    
    // ===== Screen =====
    JSValue screen = JS_NewObject(ctx);
    DEF_PROP_INT(ctx, screen, "width", 1920);
    DEF_PROP_INT(ctx, screen, "height", 1080);
    DEF_PROP_INT(ctx, screen, "availWidth", 1920);
    DEF_PROP_INT(ctx, screen, "availHeight", 1040);
    DEF_PROP_INT(ctx, screen, "colorDepth", 24);
    DEF_PROP_INT(ctx, screen, "pixelDepth", 24);
    JS_SetPropertyStr(ctx, window, "screen", screen);
    
    // ===== History =====
    JSValue history = JS_NewObject(ctx);
    DEF_PROP_INT(ctx, history, "length", 2);
    DEF_FUNC(ctx, history, "pushState", js_undefined, 3);
    DEF_FUNC(ctx, history, "replaceState", js_undefined, 3);
    DEF_FUNC(ctx, history, "back", js_undefined, 0);
    DEF_FUNC(ctx, history, "forward", js_undefined, 0);
    DEF_FUNC(ctx, history, "go", js_undefined, 1);
    JS_SetPropertyStr(ctx, window, "history", history);
    
    // ===== Storage =====
    JSValue localStorage = JS_NewObject(ctx);
    DEF_FUNC(ctx, localStorage, "getItem", js_null, 1);
    DEF_FUNC(ctx, localStorage, "setItem", js_undefined, 2);
    DEF_FUNC(ctx, localStorage, "removeItem", js_undefined, 1);
    DEF_FUNC(ctx, localStorage, "clear", js_undefined, 0);
    DEF_FUNC(ctx, localStorage, "key", js_null, 1);
    JS_SetPropertyStr(ctx, window, "localStorage", localStorage);
    JS_SetPropertyStr(ctx, window, "sessionStorage", JS_DupValue(ctx, localStorage));
    
    // ===== Console =====
    JSValue console = JS_NewObject(ctx);
    DEF_FUNC(ctx, console, "log", js_console_log, 1);
    DEF_FUNC(ctx, console, "error", js_console_log, 1);
    DEF_FUNC(ctx, console, "warn", js_console_log, 1);
    DEF_FUNC(ctx, console, "info", js_console_log, 1);
    DEF_FUNC(ctx, console, "debug", js_console_log, 1);
    DEF_FUNC(ctx, console, "trace", js_console_log, 1);
    JS_SetPropertyStr(ctx, global, "console", console);
    
    // ===== XMLHttpRequest =====
    JSValue xhr_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, xhr_proto, js_xhr_proto_funcs, js_xhr_proto_funcs_count);
    JSValue xhr_ctor = JS_NewCFunction2(ctx, js_xhr_constructor, "XMLHttpRequest", 
        1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, xhr_ctor, xhr_proto);
    JS_SetClassProto(ctx, js_xhr_class_id, xhr_proto);  // Note: JS_SetClassProto stores proto without dups
    // Do NOT free xhr_proto here - it's now owned by the class
    JS_SetPropertyStr(ctx, global, "XMLHttpRequest", xhr_ctor);
    JS_SetPropertyStr(ctx, xhr_ctor, "UNSENT", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, xhr_ctor, "OPENED", JS_NewInt32(ctx, 1));
    JS_SetPropertyStr(ctx, xhr_ctor, "HEADERS_RECEIVED", JS_NewInt32(ctx, 2));
    JS_SetPropertyStr(ctx, xhr_ctor, "LOADING", JS_NewInt32(ctx, 3));
    JS_SetPropertyStr(ctx, xhr_ctor, "DONE", JS_NewInt32(ctx, 4));
    JS_SetPropertyStr(ctx, window, "XMLHttpRequest", JS_DupValue(ctx, xhr_ctor));
    
    // ===== HTMLVideoElement =====
    JSValue video_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, video_proto, js_video_proto_funcs, js_video_proto_funcs_count);
    JSValue video_ctor = JS_NewCFunction2(ctx, js_video_constructor, "HTMLVideoElement",
        1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, video_ctor, video_proto);
    JS_SetClassProto(ctx, js_video_class_id, video_proto);  // Note: JS_SetClassProto stores proto without dups
    // Do NOT free video_proto here - it's now owned by the class
    JS_SetPropertyStr(ctx, global, "HTMLVideoElement", video_ctor);
    JS_SetPropertyStr(ctx, video_ctor, "HAVE_NOTHING", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, video_ctor, "HAVE_METADATA", JS_NewInt32(ctx, 1));
    JS_SetPropertyStr(ctx, video_ctor, "HAVE_CURRENT_DATA", JS_NewInt32(ctx, 2));
    JS_SetPropertyStr(ctx, video_ctor, "HAVE_FUTURE_DATA", JS_NewInt32(ctx, 3));
    JS_SetPropertyStr(ctx, video_ctor, "HAVE_ENOUGH_DATA", JS_NewInt32(ctx, 4));
    JS_SetPropertyStr(ctx, video_ctor, "NETWORK_EMPTY", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, video_ctor, "NETWORK_IDLE", JS_NewInt32(ctx, 1));
    JS_SetPropertyStr(ctx, video_ctor, "NETWORK_LOADING", JS_NewInt32(ctx, 2));
    JS_SetPropertyStr(ctx, video_ctor, "NETWORK_NO_SOURCE", JS_NewInt32(ctx, 3));
    JS_SetPropertyStr(ctx, window, "HTMLVideoElement", JS_DupValue(ctx, video_ctor));
    
    // ===== fetch API =====
    // fetch is set on global (which is window) - no need to duplicate
    JS_SetPropertyStr(ctx, global, "fetch", JS_NewCFunction(ctx, js_fetch, "fetch", 2));
    
    // ===== EventTarget prototype methods =====
    JSValue event_target_ctor = JS_GetPropertyStr(ctx, global, "EventTarget");
    JSValue event_target_proto = JS_GetPropertyStr(ctx, event_target_ctor, "prototype");
    if (!JS_IsUndefined(event_target_proto)) {
        JS_SetPropertyStr(ctx, event_target_proto, "addEventListener",
            JS_NewCFunction(ctx, js_event_target_addEventListener, "addEventListener", 2));
        JS_SetPropertyStr(ctx, event_target_proto, "removeEventListener",
            JS_NewCFunction(ctx, js_event_target_removeEventListener, "removeEventListener", 2));
        JS_SetPropertyStr(ctx, event_target_proto, "dispatchEvent",
            JS_NewCFunction(ctx, js_event_target_dispatchEvent, "dispatchEvent", 1));
    }
    JS_FreeValue(ctx, event_target_proto);
    JS_FreeValue(ctx, event_target_ctor);
    
    // ===== Node prototype methods =====
    JSValue node_ctor = JS_GetPropertyStr(ctx, global, "Node");
    JSValue node_proto = JS_GetPropertyStr(ctx, node_ctor, "prototype");
    if (!JS_IsUndefined(node_proto)) {
        JS_SetPropertyStr(ctx, node_proto, "appendChild",
            JS_NewCFunction(ctx, js_node_appendChild, "appendChild", 1));
        JS_SetPropertyStr(ctx, node_proto, "insertBefore",
            JS_NewCFunction(ctx, js_node_insertBefore, "insertBefore", 2));
        JS_SetPropertyStr(ctx, node_proto, "removeChild",
            JS_NewCFunction(ctx, js_node_removeChild, "removeChild", 1));
        JS_SetPropertyStr(ctx, node_proto, "cloneNode",
            JS_NewCFunction(ctx, js_node_cloneNode, "cloneNode", 1));
        JS_SetPropertyStr(ctx, node_proto, "contains",
            JS_NewCFunction(ctx, js_node_contains, "contains", 1));
    }
    JS_FreeValue(ctx, node_proto);
    JS_FreeValue(ctx, node_ctor);
    
    // ===== Shadow DOM APIs =====
    // ShadowRoot class
    JSValue shadow_root_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, shadow_root_proto, js_shadow_root_proto_funcs, 
        sizeof(js_shadow_root_proto_funcs) / sizeof(js_shadow_root_proto_funcs[0]));
    JS_SetClassProto(ctx, js_shadow_root_class_id, shadow_root_proto);
    JSValue shadow_root_ctor = JS_NewCFunction2(ctx, NULL, "ShadowRoot",
        0, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, shadow_root_ctor, shadow_root_proto);
    JS_SetPropertyStr(ctx, global, "ShadowRoot", shadow_root_ctor);
    JS_SetPropertyStr(ctx, window, "ShadowRoot", JS_DupValue(ctx, shadow_root_ctor));
    
    // Element.prototype.attachShadow and Element.prototype.shadowRoot
    // We add these to the Element constructor's prototype
    JSValue element_ctor = JS_GetPropertyStr(ctx, global, "Element");
    JSValue element_proto = JS_GetPropertyStr(ctx, element_ctor, "prototype");
    if (!JS_IsUndefined(element_proto)) {
        // attachShadow method
        JS_SetPropertyStr(ctx, element_proto, "attachShadow",
            JS_NewCFunction(ctx, js_element_attach_shadow, "attachShadow", 1));
        // shadowRoot getter
        JSValue getter = JS_NewCFunction(ctx, js_element_get_shadow_root, "get shadowRoot", 0);
        JS_DefinePropertyGetSet(ctx, element_proto, JS_NewAtom(ctx, "shadowRoot"),
            getter, JS_UNDEFINED, JS_PROP_ENUMERABLE);
        // querySelector and querySelectorAll
        JS_SetPropertyStr(ctx, element_proto, "querySelector",
            JS_NewCFunction(ctx, js_element_querySelector, "querySelector", 1));
        JS_SetPropertyStr(ctx, element_proto, "querySelectorAll",
            JS_NewCFunction(ctx, js_element_querySelectorAll, "querySelectorAll", 1));
    }
    JS_FreeValue(ctx, element_proto);
    JS_FreeValue(ctx, element_ctor);
    
    // ===== Custom Elements API =====
    JSValue custom_elements = JS_NewObjectClass(ctx, js_custom_element_registry_class_id);
    JS_SetPropertyStr(ctx, custom_elements, "define",
        JS_NewCFunction(ctx, js_custom_elements_define, "define", 2));
    JS_SetPropertyStr(ctx, custom_elements, "get",
        JS_NewCFunction(ctx, js_custom_elements_get, "get", 1));
    JS_SetPropertyStr(ctx, custom_elements, "whenDefined",
        JS_NewCFunction(ctx, js_custom_elements_when_defined, "whenDefined", 1));
    JS_SetPropertyStr(ctx, window, "customElements", custom_elements);
    
    // CustomElementRegistry constructor (for completeness)
    JSValue ce_registry_ctor = JS_NewCFunction2(ctx, NULL, "CustomElementRegistry",
        0, JS_CFUNC_constructor, 0);
    JS_SetPropertyStr(ctx, global, "CustomElementRegistry", ce_registry_ctor);
    JS_SetPropertyStr(ctx, window, "CustomElementRegistry", JS_DupValue(ctx, ce_registry_ctor));
    
    // ===== Web Animations API =====
    // Animation class
    JSValue animation_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, animation_proto, js_animation_proto_funcs,
        sizeof(js_animation_proto_funcs) / sizeof(js_animation_proto_funcs[0]));
    JS_SetClassProto(ctx, js_animation_class_id, animation_proto);
    JSValue animation_ctor = JS_NewCFunction2(ctx, js_animation_constructor, "Animation",
        1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, animation_ctor, animation_proto);
    JS_SetPropertyStr(ctx, global, "Animation", animation_ctor);
    JS_SetPropertyStr(ctx, window, "Animation", JS_DupValue(ctx, animation_ctor));
    
    // KeyframeEffect class
    JSValue keyframe_effect_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, keyframe_effect_proto, js_keyframe_effect_proto_funcs,
        sizeof(js_keyframe_effect_proto_funcs) / sizeof(js_keyframe_effect_proto_funcs[0]));
    JS_SetClassProto(ctx, js_keyframe_effect_class_id, keyframe_effect_proto);
    JSValue keyframe_effect_ctor = JS_NewCFunction2(ctx, js_keyframe_effect_constructor, "KeyframeEffect",
        3, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, keyframe_effect_ctor, keyframe_effect_proto);
    JS_SetPropertyStr(ctx, global, "KeyframeEffect", keyframe_effect_ctor);
    JS_SetPropertyStr(ctx, window, "KeyframeEffect", JS_DupValue(ctx, keyframe_effect_ctor));
    
    // Element.prototype.animate
    element_ctor = JS_GetPropertyStr(ctx, global, "Element");
    element_proto = JS_GetPropertyStr(ctx, element_ctor, "prototype");
    if (!JS_IsUndefined(element_proto)) {
        JS_SetPropertyStr(ctx, element_proto, "animate",
            JS_NewCFunction(ctx, js_element_animate, "animate", 2));
    }
    JS_FreeValue(ctx, element_proto);
    JS_FreeValue(ctx, element_ctor);
    
    // ===== Font Loading API =====
    // FontFace class
    JSValue font_face_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, font_face_proto, js_font_face_proto_funcs,
        sizeof(js_font_face_proto_funcs) / sizeof(js_font_face_proto_funcs[0]));
    JS_SetClassProto(ctx, js_font_face_class_id, font_face_proto);
    JSValue font_face_ctor = JS_NewCFunction2(ctx, js_font_face_constructor, "FontFace",
        3, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, font_face_ctor, font_face_proto);
    JS_SetPropertyStr(ctx, global, "FontFace", font_face_ctor);
    JS_SetPropertyStr(ctx, window, "FontFace", JS_DupValue(ctx, font_face_ctor));
    
    // FontFaceSet class (document.fonts)
    JSValue font_face_set_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, font_face_set_proto, js_font_face_set_proto_funcs,
        sizeof(js_font_face_set_proto_funcs) / sizeof(js_font_face_set_proto_funcs[0]));
    JS_SetClassProto(ctx, js_font_face_set_class_id, font_face_set_proto);
    JSValue font_face_set = JS_NewObjectClass(ctx, js_font_face_set_class_id);
    FontFaceSetData *ffs = calloc(1, sizeof(FontFaceSetData));
    if (ffs) {
        ffs->loaded_fonts = JS_NewArray(ctx);
        JS_SetOpaque(font_face_set, ffs);
    }
    // Add Symbol.iterator via JavaScript evaluation (QuickJS doesn't expose JS_ATOM_Symbol_iterator directly)
    const char *iterator_js = "FontFaceSet.prototype[Symbol.iterator] = FontFaceSet.prototype.values;";
    JSValue iterator_result = JS_Eval(ctx, iterator_js, strlen(iterator_js), "<iterator_setup>", 0);
    JS_FreeValue(ctx, iterator_result);
    
    JS_SetPropertyStr(ctx, document, "fonts", font_face_set);
    
    JSValue font_face_set_ctor = JS_NewCFunction2(ctx, NULL, "FontFaceSet",
        0, JS_CFUNC_constructor, 0);
    JS_SetPropertyStr(ctx, global, "FontFaceSet", font_face_set_ctor);
    JS_SetPropertyStr(ctx, window, "FontFaceSet", JS_DupValue(ctx, font_face_set_ctor));
    
    // ===== Observer APIs =====
    // MutationObserver
    JSValue mutation_observer_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, mutation_observer_proto, js_mutation_observer_proto_funcs,
        sizeof(js_mutation_observer_proto_funcs) / sizeof(js_mutation_observer_proto_funcs[0]));
    JS_SetClassProto(ctx, js_mutation_observer_class_id, mutation_observer_proto);
    JSValue mutation_observer_ctor = JS_NewCFunction2(ctx, js_mutation_observer_constructor, "MutationObserver",
        1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, mutation_observer_ctor, mutation_observer_proto);
    JS_SetPropertyStr(ctx, global, "MutationObserver", mutation_observer_ctor);
    JS_SetPropertyStr(ctx, window, "MutationObserver", JS_DupValue(ctx, mutation_observer_ctor));
    
    // ResizeObserver
    JSValue resize_observer_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, resize_observer_proto, js_resize_observer_proto_funcs,
        sizeof(js_resize_observer_proto_funcs) / sizeof(js_resize_observer_proto_funcs[0]));
    JS_SetClassProto(ctx, js_resize_observer_class_id, resize_observer_proto);
    JSValue resize_observer_ctor = JS_NewCFunction2(ctx, js_resize_observer_constructor, "ResizeObserver",
        1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, resize_observer_ctor, resize_observer_proto);
    JS_SetPropertyStr(ctx, global, "ResizeObserver", resize_observer_ctor);
    JS_SetPropertyStr(ctx, window, "ResizeObserver", JS_DupValue(ctx, resize_observer_ctor));
    
    // IntersectionObserver
    JSValue intersection_observer_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, intersection_observer_proto, js_intersection_observer_proto_funcs,
        sizeof(js_intersection_observer_proto_funcs) / sizeof(js_intersection_observer_proto_funcs[0]));
    JS_SetClassProto(ctx, js_intersection_observer_class_id, intersection_observer_proto);
    JSValue intersection_observer_ctor = JS_NewCFunction2(ctx, js_intersection_observer_constructor, "IntersectionObserver",
        1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, intersection_observer_ctor, intersection_observer_proto);
    JS_SetPropertyStr(ctx, global, "IntersectionObserver", intersection_observer_ctor);
    JS_SetPropertyStr(ctx, window, "IntersectionObserver", JS_DupValue(ctx, intersection_observer_ctor));
    
    // ===== Performance API =====
    // PerformanceEntry class
    JSValue performance_entry_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, performance_entry_proto, js_performance_entry_proto_funcs,
        sizeof(js_performance_entry_proto_funcs) / sizeof(js_performance_entry_proto_funcs[0]));
    JS_SetClassProto(ctx, js_performance_entry_class_id, performance_entry_proto);
    JSValue performance_entry_ctor = JS_NewCFunction2(ctx, NULL, "PerformanceEntry",
        0, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, performance_entry_ctor, performance_entry_proto);
    JS_SetPropertyStr(ctx, global, "PerformanceEntry", performance_entry_ctor);
    JS_SetPropertyStr(ctx, window, "PerformanceEntry", JS_DupValue(ctx, performance_entry_ctor));
    
    // PerformanceObserver class
    JSValue performance_observer_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, performance_observer_proto, js_performance_observer_proto_funcs,
        sizeof(js_performance_observer_proto_funcs) / sizeof(js_performance_observer_proto_funcs[0]));
    JS_SetClassProto(ctx, js_performance_observer_class_id, performance_observer_proto);
    JSValue performance_observer_ctor = JS_NewCFunction2(ctx, js_performance_observer_constructor, "PerformanceObserver",
        1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, performance_observer_ctor, performance_observer_proto);
    JS_SetPropertyStr(ctx, global, "PerformanceObserver", performance_observer_ctor);
    JS_SetPropertyStr(ctx, window, "PerformanceObserver", JS_DupValue(ctx, performance_observer_ctor));
    
    // Performance class
    JSValue performance_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, performance_proto, js_performance_proto_funcs,
        sizeof(js_performance_proto_funcs) / sizeof(js_performance_proto_funcs[0]));
    JS_SetClassProto(ctx, js_performance_class_id, performance_proto);
    JSValue performance_obj = JS_NewObjectClass(ctx, js_performance_class_id);
    PerformanceData *perf_data = calloc(1, sizeof(PerformanceData));
    if (perf_data) {
        perf_data->start_time = 0.0;
        JS_SetOpaque(performance_obj, perf_data);
    }
    JS_SetPropertyStr(ctx, window, "performance", performance_obj);
    
    JSValue performance_ctor = JS_NewCFunction2(ctx, NULL, "Performance",
        0, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, performance_ctor, performance_proto);
    JS_SetPropertyStr(ctx, global, "Performance", performance_ctor);
    JS_SetPropertyStr(ctx, window, "Performance", JS_DupValue(ctx, performance_ctor));
    
    // ===== DOMRect API =====
    // DOMRectReadOnly class
    JSValue dom_rect_read_only_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, dom_rect_read_only_proto, js_dom_rect_read_only_proto_funcs,
        sizeof(js_dom_rect_read_only_proto_funcs) / sizeof(js_dom_rect_read_only_proto_funcs[0]));
    JS_SetClassProto(ctx, js_dom_rect_read_only_class_id, dom_rect_read_only_proto);
    JSValue dom_rect_read_only_ctor = JS_NewCFunction2(ctx, js_dom_rect_read_only_constructor, "DOMRectReadOnly",
        4, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, dom_rect_read_only_ctor, dom_rect_read_only_proto);
    // Add fromRect static method
    JSValue from_rect_ro = JS_NewCFunction(ctx, js_dom_rect_read_only_from_rect, "fromRect", 1);
    JS_SetPropertyStr(ctx, dom_rect_read_only_ctor, "fromRect", from_rect_ro);
    JS_SetPropertyStr(ctx, global, "DOMRectReadOnly", dom_rect_read_only_ctor);
    JS_SetPropertyStr(ctx, window, "DOMRectReadOnly", JS_DupValue(ctx, dom_rect_read_only_ctor));
    
    // DOMRect class
    JSValue dom_rect_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, dom_rect_proto, js_dom_rect_proto_funcs,
        sizeof(js_dom_rect_proto_funcs) / sizeof(js_dom_rect_proto_funcs[0]));
    JS_SetClassProto(ctx, js_dom_rect_class_id, dom_rect_proto);
    JSValue dom_rect_ctor = JS_NewCFunction2(ctx, js_dom_rect_constructor, "DOMRect",
        4, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, dom_rect_ctor, dom_rect_proto);
    // Add fromRect static method
    JSValue from_rect = JS_NewCFunction(ctx, js_dom_rect_from_rect, "fromRect", 1);
    JS_SetPropertyStr(ctx, dom_rect_ctor, "fromRect", from_rect);
    JS_SetPropertyStr(ctx, global, "DOMRect", dom_rect_ctor);
    JS_SetPropertyStr(ctx, window, "DOMRect", JS_DupValue(ctx, dom_rect_ctor));
    
    // ===== DOM Constructors (stubs) =====
    JS_SetPropertyStr(ctx, global, "EventTarget", JS_NewCFunction2(ctx, NULL, "EventTarget", 0, JS_CFUNC_constructor, 0));
    JS_SetPropertyStr(ctx, global, "Node", JS_NewCFunction2(ctx, NULL, "Node", 0, JS_CFUNC_constructor, 0));
    JS_SetPropertyStr(ctx, global, "Element", JS_NewCFunction2(ctx, NULL, "Element", 1, JS_CFUNC_constructor, 0));
    JS_SetPropertyStr(ctx, global, "HTMLElement", JS_NewCFunction2(ctx, NULL, "HTMLElement", 1, JS_CFUNC_constructor, 0));
    JS_SetPropertyStr(ctx, global, "DocumentFragment", JS_NewCFunction2(ctx, NULL, "DocumentFragment", 0, JS_CFUNC_constructor, 0));
}
