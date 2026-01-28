#include "js_engine.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * JavaScript Built-in Objects
 * String, Math, Array prototypes
 * ============================================================================ */

/* ============================================================================
 * String Built-ins
 * ============================================================================ */

static JsValue string_from_char_code(JsContext *ctx, JsValue *this_obj,
                                      JsValue *args, size_t arg_count) {
    (void)this_obj;
    
    char *buf = (char *)js_arena_alloc(&ctx->arena, arg_count + 1);
    if (!buf) return js_make_undefined();
    
    for (size_t i = 0; i < arg_count; i++) {
        int code = (int)js_to_number(args[i]);
        buf[i] = (char)(code & 0xFF);
    }
    buf[arg_count] = '\0';
    
    return js_make_string_nocopy(buf, arg_count);
}

static JsValue string_char_at(JsContext *ctx, JsValue *this_obj,
                               JsValue *args, size_t arg_count) {
    if (arg_count < 1 || !JS_IS_STRING(*this_obj)) {
        return js_make_string(ctx, "", 0);
    }
    
    const char *str = JS_AS_STRING(*this_obj);
    size_t len = strlen(str);
    int idx = (int)js_to_number(args[0]);
    
    if (idx < 0 || (size_t)idx >= len) {
        return js_make_string(ctx, "", 0);
    }
    
    char ch[2] = {str[idx], '\0'};
    return js_make_string(ctx, ch, 1);
}

static JsValue string_char_code_at(JsContext *ctx, JsValue *this_obj,
                                    JsValue *args, size_t arg_count) {
    if (arg_count < 1 || !JS_IS_STRING(*this_obj)) {
        return js_make_number(NAN);
    }
    
    const char *str = JS_AS_STRING(*this_obj);
    int idx = (int)js_to_number(args[0]);
    
    if (idx < 0 || (size_t)idx >= strlen(str)) {
        return js_make_number(NAN);
    }
    
    return js_make_number((unsigned char)str[idx]);
}

static JsValue string_slice(JsContext *ctx, JsValue *this_obj,
                             JsValue *args, size_t arg_count) {
    if (!JS_IS_STRING(*this_obj)) {
        return js_make_undefined();
    }
    
    const char *str = JS_AS_STRING(*this_obj);
    size_t len = strlen(str);
    
    int start = arg_count > 0 ? (int)js_to_number(args[0]) : 0;
    int end = arg_count > 1 ? (int)js_to_number(args[1]) : (int)len;
    
    if (start < 0) start = (int)len + start;
    if (end < 0) end = (int)len + end;
    if (start < 0) start = 0;
    if (end < 0) end = 0;
    if ((size_t)start > len) start = (int)len;
    if ((size_t)end > len) end = (int)len;
    
    if (start >= end) {
        return js_make_string(ctx, "", 0);
    }
    
    return js_make_string(ctx, str + start, (size_t)(end - start));
}

static JsValue string_substr(JsContext *ctx, JsValue *this_obj,
                              JsValue *args, size_t arg_count) {
    if (!JS_IS_STRING(*this_obj)) {
        return js_make_undefined();
    }
    
    const char *str = JS_AS_STRING(*this_obj);
    size_t len = strlen(str);
    
    int start = arg_count > 0 ? (int)js_to_number(args[0]) : 0;
    int length = arg_count > 1 ? (int)js_to_number(args[1]) : (int)len;
    
    if (start < 0) start = (int)len + start;
    if (start < 0) start = 0;
    if ((size_t)start > len) start = (int)len;
    if (length < 0) length = 0;
    if (start + length > (int)len) length = (int)len - start;
    
    return js_make_string(ctx, str + start, (size_t)length);
}

static JsValue string_substring(JsContext *ctx, JsValue *this_obj,
                                 JsValue *args, size_t arg_count) {
    if (!JS_IS_STRING(*this_obj)) {
        return js_make_undefined();
    }
    
    const char *str = JS_AS_STRING(*this_obj);
    size_t len = strlen(str);
    
    int start = arg_count > 0 ? (int)js_to_number(args[0]) : 0;
    int end = arg_count > 1 ? (int)js_to_number(args[1]) : (int)len;
    
    if (start < 0) start = 0;
    if (end < 0) end = 0;
    if ((size_t)start > len) start = (int)len;
    if ((size_t)end > len) end = (int)len;
    if (start > end) {
        int tmp = start;
        start = end;
        end = tmp;
    }
    
    return js_make_string(ctx, str + start, (size_t)(end - start));
}

static JsValue string_split(JsContext *ctx, JsValue *this_obj,
                             JsValue *args, size_t arg_count) {
    if (!JS_IS_STRING(*this_obj)) {
        return js_make_undefined();
    }
    
    const char *str = JS_AS_STRING(*this_obj);
    const char *sep = arg_count > 0 && JS_IS_STRING(args[0]) ? 
                      JS_AS_STRING(args[0]) : NULL;
    
    JsValue arr_val = js_make_array(ctx, 32);
    if (!JS_IS_ARRAY(arr_val)) return arr_val;
    
    JsArray *arr = JS_AS_ARRAY(arr_val);
    
    if (!sep || strlen(sep) == 0) {
        /* Split into individual characters */
        for (size_t i = 0; i < strlen(str) && arr->count < 32; i++) {
            char ch[2] = {str[i], '\0'};
            arr->items[arr->count++] = js_make_string(ctx, ch, 1);
        }
    } else {
        /* Split on delimiter */
        const char *start = str;
        const char *p = strstr(start, sep);
        size_t sep_len = strlen(sep);
        
        while (p && arr->count < 32) {
            arr->items[arr->count++] = js_make_string(ctx, start, (size_t)(p - start));
            start = p + sep_len;
            p = strstr(start, sep);
        }
        
        if (arr->count < 32) {
            arr->items[arr->count++] = js_make_string(ctx, start, strlen(start));
        }
    }
    
    return arr_val;
}

static JsValue string_index_of(JsContext *ctx, JsValue *this_obj,
                                JsValue *args, size_t arg_count) {
    if (!JS_IS_STRING(*this_obj) || arg_count < 1) {
        return js_make_number(-1);
    }
    
    const char *str = JS_AS_STRING(*this_obj);
    const char *search = JS_IS_STRING(args[0]) ? JS_AS_STRING(args[0]) : "";
    int start = arg_count > 1 ? (int)js_to_number(args[1]) : 0;
    
    size_t len = strlen(str);
    if (start < 0) start = 0;
    if ((size_t)start > len) start = (int)len;
    
    const char *found = strstr(str + start, search);
    if (found) {
        return js_make_number((double)(found - str));
    }
    return js_make_number(-1);
}

static JsValue string_replace(JsContext *ctx, JsValue *this_obj,
                               JsValue *args, size_t arg_count) {
    if (!JS_IS_STRING(*this_obj) || arg_count < 2) {
        return js_make_undefined();
    }
    
    const char *str = JS_AS_STRING(*this_obj);
    const char *search = JS_IS_STRING(args[0]) ? JS_AS_STRING(args[0]) : "";
    const char *replace = JS_IS_STRING(args[1]) ? JS_AS_STRING(args[1]) : "";
    
    const char *found = strstr(str, search);
    if (!found) {
        return *this_obj;
    }
    
    size_t before_len = (size_t)(found - str);
    size_t search_len = strlen(search);
    size_t replace_len = strlen(replace);
    size_t after_len = strlen(found + search_len);
    
    size_t total = before_len + replace_len + after_len;
    char *result = (char *)js_arena_alloc(&ctx->arena, total + 1);
    if (!result) return js_make_undefined();
    
    memcpy(result, str, before_len);
    memcpy(result + before_len, replace, replace_len);
    memcpy(result + before_len + replace_len, found + search_len, after_len);
    result[total] = '\0';
    
    return js_make_string_nocopy(result, total);
}

static JsValue string_to_lower_case(JsContext *ctx, JsValue *this_obj,
                                     JsValue *args, size_t arg_count) {
    (void)args;
    (void)arg_count;
    
    if (!JS_IS_STRING(*this_obj)) {
        return js_make_undefined();
    }
    
    const char *str = JS_AS_STRING(*this_obj);
    size_t len = strlen(str);
    
    char *result = (char *)js_arena_alloc(&ctx->arena, len + 1);
    if (!result) return js_make_undefined();
    
    for (size_t i = 0; i < len; i++) {
        result[i] = (char)tolower((unsigned char)str[i]);
    }
    result[len] = '\0';
    
    return js_make_string_nocopy(result, len);
}

static JsValue string_to_upper_case(JsContext *ctx, JsValue *this_obj,
                                     JsValue *args, size_t arg_count) {
    (void)args;
    (void)arg_count;
    
    if (!JS_IS_STRING(*this_obj)) {
        return js_make_undefined();
    }
    
    const char *str = JS_AS_STRING(*this_obj);
    size_t len = strlen(str);
    
    char *result = (char *)js_arena_alloc(&ctx->arena, len + 1);
    if (!result) return js_make_undefined();
    
    for (size_t i = 0; i < len; i++) {
        result[i] = (char)toupper((unsigned char)str[i]);
    }
    result[len] = '\0';
    
    return js_make_string_nocopy(result, len);
}

static JsValue string_trim(JsContext *ctx, JsValue *this_obj,
                            JsValue *args, size_t arg_count) {
    (void)args;
    (void)arg_count;
    
    if (!JS_IS_STRING(*this_obj)) {
        return js_make_undefined();
    }
    
    const char *str = JS_AS_STRING(*this_obj);
    size_t len = strlen(str);
    
    size_t start = 0;
    while (start < len && isspace((unsigned char)str[start])) start++;
    
    size_t end = len;
    while (end > start && isspace((unsigned char)str[end - 1])) end--;
    
    return js_make_string(ctx, str + start, end - start);
}

/* ============================================================================
 * Array Built-ins
 * ============================================================================ */

static JsValue array_join(JsContext *ctx, JsValue *this_obj,
                           JsValue *args, size_t arg_count) {
    if (!JS_IS_ARRAY(*this_obj)) {
        return js_make_undefined();
    }
    
    JsArray *arr = JS_AS_ARRAY(*this_obj);
    const char *sep = arg_count > 0 && JS_IS_STRING(args[0]) ? 
                      JS_AS_STRING(args[0]) : ",";
    
    /* Calculate total length */
    size_t total = 0;
    size_t sep_len = strlen(sep);
    
    for (size_t i = 0; i < arr->count; i++) {
        JsString s = js_to_string(ctx, arr->items[i]);
        total += s.len;
        if (i < arr->count - 1) total += sep_len;
    }
    
    char *result = (char *)js_arena_alloc(&ctx->arena, total + 1);
    if (!result) return js_make_undefined();
    
    size_t pos = 0;
    for (size_t i = 0; i < arr->count; i++) {
        JsString s = js_to_string(ctx, arr->items[i]);
        memcpy(result + pos, s.data, s.len);
        pos += s.len;
        if (i < arr->count - 1) {
            memcpy(result + pos, sep, sep_len);
            pos += sep_len;
        }
    }
    result[pos] = '\0';
    
    return js_make_string_nocopy(result, pos);
}

static JsValue array_reverse(JsContext *ctx, JsValue *this_obj,
                              JsValue *args, size_t arg_count) {
    (void)ctx;
    (void)args;
    (void)arg_count;
    
    if (!JS_IS_ARRAY(*this_obj)) {
        return js_make_undefined();
    }
    
    JsArray *arr = JS_AS_ARRAY(*this_obj);
    
    for (size_t i = 0; i < arr->count / 2; i++) {
        JsValue tmp = arr->items[i];
        arr->items[i] = arr->items[arr->count - 1 - i];
        arr->items[arr->count - 1 - i] = tmp;
    }
    
    return *this_obj;
}

static JsValue array_slice(JsContext *ctx, JsValue *this_obj,
                            JsValue *args, size_t arg_count) {
    if (!JS_IS_ARRAY(*this_obj)) {
        return js_make_undefined();
    }
    
    JsArray *arr = JS_AS_ARRAY(*this_obj);
    
    int start = arg_count > 0 ? (int)js_to_number(args[0]) : 0;
    int end = arg_count > 1 ? (int)js_to_number(args[1]) : (int)arr->count;
    
    if (start < 0) start = (int)arr->count + start;
    if (end < 0) end = (int)arr->count + end;
    if (start < 0) start = 0;
    if (end < 0) end = 0;
    if ((size_t)start > arr->count) start = (int)arr->count;
    if ((size_t)end > arr->count) end = (int)arr->count;
    
    JsValue result = js_make_array(ctx, (size_t)(end - start));
    if (!JS_IS_ARRAY(result)) return result;
    
    JsArray *new_arr = JS_AS_ARRAY(result);
    for (int i = start; i < end && (size_t)i < arr->count; i++) {
        new_arr->items[new_arr->count++] = arr->items[i];
    }
    
    return result;
}

static JsValue array_splice(JsContext *ctx, JsValue *this_obj,
                             JsValue *args, size_t arg_count) {
    if (!JS_IS_ARRAY(*this_obj) || arg_count < 2) {
        return js_make_undefined();
    }
    
    JsArray *arr = JS_AS_ARRAY(*this_obj);
    
    int start = (int)js_to_number(args[0]);
    int delete_count = (int)js_to_number(args[1]);
    
    if (start < 0) start = (int)arr->count + start;
    if (start < 0) start = 0;
    if ((size_t)start > arr->count) start = (int)arr->count;
    if (delete_count < 0) delete_count = 0;
    if ((size_t)(start + delete_count) > arr->count) {
        delete_count = (int)arr->count - start;
    }
    
    /* Create result array with deleted items */
    JsValue result = js_make_array(ctx, (size_t)delete_count);
    if (!JS_IS_ARRAY(result)) return result;
    
    JsArray *del_arr = JS_AS_ARRAY(result);
    for (int i = 0; i < delete_count; i++) {
        del_arr->items[del_arr->count++] = arr->items[start + i];
    }
    
    /* Remove deleted items and shift remaining */
    size_t items_to_shift = arr->count - (size_t)(start + delete_count);
    memmove(&arr->items[start], &arr->items[start + delete_count], 
            items_to_shift * sizeof(JsValue));
    arr->count -= (size_t)delete_count;
    
    /* Insert new items if provided */
    size_t insert_count = arg_count > 2 ? arg_count - 2 : 0;
    if (insert_count > 0 && arr->count + insert_count <= arr->capacity) {
        /* Make room */
        memmove(&arr->items[start + insert_count], &arr->items[start],
                items_to_shift * sizeof(JsValue));
        
        /* Insert */
        for (size_t i = 0; i < insert_count; i++) {
            arr->items[start + i] = args[2 + i];
        }
        arr->count += insert_count;
    }
    
    return result;
}

static JsValue array_push(JsContext *ctx, JsValue *this_obj,
                           JsValue *args, size_t arg_count) {
    (void)ctx;
    
    if (!JS_IS_ARRAY(*this_obj)) {
        return js_make_number(0);
    }
    
    JsArray *arr = JS_AS_ARRAY(*this_obj);
    
    for (size_t i = 0; i < arg_count && arr->count < arr->capacity; i++) {
        arr->items[arr->count++] = args[i];
    }
    
    return js_make_number((double)arr->count);
}

static JsValue array_pop(JsContext *ctx, JsValue *this_obj,
                          JsValue *args, size_t arg_count) {
    (void)ctx;
    (void)args;
    
    if (!JS_IS_ARRAY(*this_obj)) {
        return js_make_undefined();
    }
    
    JsArray *arr = JS_AS_ARRAY(*this_obj);
    if (arr->count == 0) {
        return js_make_undefined();
    }
    
    return arr->items[--arr->count];
}

static JsValue array_shift(JsContext *ctx, JsValue *this_obj,
                            JsValue *args, size_t arg_count) {
    (void)ctx;
    (void)args;
    
    if (!JS_IS_ARRAY(*this_obj)) {
        return js_make_undefined();
    }
    
    JsArray *arr = JS_AS_ARRAY(*this_obj);
    if (arr->count == 0) {
        return js_make_undefined();
    }
    
    JsValue result = arr->items[0];
    memmove(&arr->items[0], &arr->items[1], (arr->count - 1) * sizeof(JsValue));
    arr->count--;
    
    return result;
}

static JsValue array_unshift(JsContext *ctx, JsValue *this_obj,
                              JsValue *args, size_t arg_count) {
    (void)ctx;
    
    if (!JS_IS_ARRAY(*this_obj)) {
        return js_make_number(0);
    }
    
    JsArray *arr = JS_AS_ARRAY(*this_obj);
    
    if (arg_count > 0 && arr->count + arg_count <= arr->capacity) {
        /* Make room */
        memmove(&arr->items[arg_count], &arr->items[0], 
                arr->count * sizeof(JsValue));
        
        /* Insert */
        for (size_t i = 0; i < arg_count; i++) {
            arr->items[i] = args[i];
        }
        arr->count += arg_count;
    }
    
    return js_make_number((double)arr->count);
}

static JsValue array_index_of(JsContext *ctx, JsValue *this_obj,
                               JsValue *args, size_t arg_count) {
    if (!JS_IS_ARRAY(*this_obj) || arg_count < 1) {
        return js_make_number(-1);
    }
    
    JsArray *arr = JS_AS_ARRAY(*this_obj);
    int start = arg_count > 1 ? (int)js_to_number(args[1]) : 0;
    
    if (start < 0) start = 0;
    
    for (size_t i = (size_t)start; i < arr->count; i++) {
        /* Simple equality check */
        if (arr->items[i].type == args[0].type) {
            if (JS_IS_NUMBER(arr->items[i]) && JS_IS_NUMBER(args[0])) {
                if (JS_AS_NUMBER(arr->items[i]) == JS_AS_NUMBER(args[0])) {
                    return js_make_number((double)i);
                }
            } else if (JS_IS_STRING(arr->items[i]) && JS_IS_STRING(args[0])) {
                if (strcmp(JS_AS_STRING(arr->items[i]), JS_AS_STRING(args[0])) == 0) {
                    return js_make_number((double)i);
                }
            }
        }
    }
    
    return js_make_number(-1);
}

/* ============================================================================
 * Math Built-ins
 * ============================================================================ */

static JsValue math_abs(JsContext *ctx, JsValue *this_obj,
                         JsValue *args, size_t arg_count) {
    (void)ctx;
    (void)this_obj;
    
    if (arg_count < 1) return js_make_number(NAN);
    return js_make_number(fabs(js_to_number(args[0])));
}

static JsValue math_floor(JsContext *ctx, JsValue *this_obj,
                           JsValue *args, size_t arg_count) {
    (void)ctx;
    (void)this_obj;
    
    if (arg_count < 1) return js_make_number(NAN);
    return js_make_number(floor(js_to_number(args[0])));
}

static JsValue math_ceil(JsContext *ctx, JsValue *this_obj,
                          JsValue *args, size_t arg_count) {
    (void)ctx;
    (void)this_obj;
    
    if (arg_count < 1) return js_make_number(NAN);
    return js_make_number(ceil(js_to_number(args[0])));
}

static JsValue math_round(JsContext *ctx, JsValue *this_obj,
                           JsValue *args, size_t arg_count) {
    (void)ctx;
    (void)this_obj;
    
    if (arg_count < 1) return js_make_number(NAN);
    return js_make_number(round(js_to_number(args[0])));
}

static JsValue math_max(JsContext *ctx, JsValue *this_obj,
                         JsValue *args, size_t arg_count) {
    (void)ctx;
    (void)this_obj;
    
    if (arg_count == 0) return js_make_number(-INFINITY);
    
    double max = js_to_number(args[0]);
    for (size_t i = 1; i < arg_count; i++) {
        double val = js_to_number(args[i]);
        if (val > max) max = val;
    }
    return js_make_number(max);
}

static JsValue math_min(JsContext *ctx, JsValue *this_obj,
                         JsValue *args, size_t arg_count) {
    (void)ctx;
    (void)this_obj;
    
    if (arg_count == 0) return js_make_number(INFINITY);
    
    double min = js_to_number(args[0]);
    for (size_t i = 1; i < arg_count; i++) {
        double val = js_to_number(args[i]);
        if (val < min) min = val;
    }
    return js_make_number(min);
}

static JsValue math_pow(JsContext *ctx, JsValue *this_obj,
                         JsValue *args, size_t arg_count) {
    (void)ctx;
    (void)this_obj;
    
    if (arg_count < 2) return js_make_number(NAN);
    return js_make_number(pow(js_to_number(args[0]), js_to_number(args[1])));
}

static JsValue math_sqrt(JsContext *ctx, JsValue *this_obj,
                          JsValue *args, size_t arg_count) {
    (void)ctx;
    (void)this_obj;
    
    if (arg_count < 1) return js_make_number(NAN);
    return js_make_number(sqrt(js_to_number(args[0])));
}

static JsValue math_random(JsContext *ctx, JsValue *this_obj,
                            JsValue *args, size_t arg_count) {
    (void)ctx;
    (void)this_obj;
    (void)args;
    
    return js_make_number((double)rand() / RAND_MAX);
}

/* ============================================================================
 * Register Built-ins
 * ============================================================================ */

void js_register_builtins(JsContext *ctx) {
    /* Register String constructor */
    JsValue string_obj = js_make_object(ctx);
    
    /* Register Math object */
    JsValue math_obj = js_make_object(ctx);
    
    /* Add Math constants and functions to global scope */
    scope_define(ctx, js_string_from_literal("Math", 4), math_obj);
    scope_define(ctx, js_string_from_literal("String", 6), string_obj);
}

/* Math constants */
#define MATH_PI 3.14159265358979323846
#define MATH_E 2.71828182845904523536
