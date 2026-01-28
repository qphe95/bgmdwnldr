#include "js_engine.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Arena Allocator - Fast, linear allocation
 * ============================================================================ */

void *js_arena_alloc(JsArena *arena, size_t size) {
    size = (size + 7) & ~7; /* Align to 8 bytes */
    if (arena->used + size > arena->size) {
        return NULL; /* Out of memory */
    }
    void *ptr = arena->base + arena->used;
    arena->used += size;
    memset(ptr, 0, size);
    return ptr;
}

static void arena_init(JsArena *arena, void *buffer, size_t size) {
    arena->base = (uint8_t *)buffer;
    arena->size = size;
    arena->used = 0;
    arena->prev_used = 0;
}

static void arena_reset(JsArena *arena) {
    arena->used = 0;
}

/* ============================================================================
 * Value Creation Helpers
 * ============================================================================ */

JsValue js_make_undefined(void) {
    JsValue v = {0};
    v.type = JS_TYPE_UNDEFINED;
    return v;
}

JsValue js_make_null(void) {
    JsValue v = {0};
    v.type = JS_TYPE_NULL;
    return v;
}

JsValue js_make_bool(bool b) {
    JsValue v = {0};
    v.type = JS_TYPE_BOOL;
    v.data.heap.payload.boolean = b;
    return v;
}

JsValue js_make_number(double n) {
    JsValue v = {0};
    v.type = JS_TYPE_NUMBER;
    v.data.num = n;
    return v;
}

JsValue js_make_string_nocopy(const char *str, size_t len) {
    JsValue v = {0};
    v.type = JS_TYPE_STRING;
    v.data.heap.payload.str_ptr = str;
    v.data.heap.tag = (uint32_t)len;
    return v;
}

JsValue js_make_string(JsContext *ctx, const char *str, size_t len) {
    if (len == 0) len = strlen(str);
    
    char *copy = (char *)js_arena_alloc(&ctx->arena, len + 1);
    if (!copy) return js_make_undefined();
    
    memcpy(copy, str, len);
    copy[len] = '\0';
    
    return js_make_string_nocopy(copy, len);
}

JsValue js_make_array(JsContext *ctx, size_t capacity) {
    JsValue v = {0};
    v.type = JS_TYPE_ARRAY;
    
    JsArray *arr = JS_ARENA_ALLOC(ctx, JsArray);
    if (!arr) return js_make_undefined();
    
    arr->items = JS_ARENA_ALLOC_N(ctx, JsValue, capacity);
    if (!arr->items) return js_make_undefined();
    
    arr->capacity = capacity;
    arr->count = 0;
    
    v.data.heap.payload.arr_ptr = arr;
    return v;
}

JsValue js_make_object(JsContext *ctx) {
    JsValue v = {0};
    v.type = JS_TYPE_OBJECT;
    
    JsObject *obj = JS_ARENA_ALLOC(ctx, JsObject);
    if (!obj) return js_make_undefined();
    
    obj->props = NULL;
    obj->prop_count = 0;
    obj->prop_capacity = 0;
    
    v.data.heap.payload.obj_ptr = obj;
    return v;
}

/* ============================================================================
 * String Operations
 * ============================================================================ */

JsString js_string_from_literal(const char *str, size_t len) {
    JsString s = {0};
    s.data = str;
    s.len = len > 0 ? len : strlen(str);
    s.owned = false;
    return s;
}

bool js_string_equal(JsString a, JsString b) {
    if (a.len != b.len) return false;
    return memcmp(a.data, b.data, a.len) == 0;
}

JsString js_string_concat(JsContext *ctx, JsString a, JsString b) {
    size_t total = a.len + b.len;
    char *buf = (char *)js_arena_alloc(&ctx->arena, total + 1);
    if (!buf) return js_string_from_literal("", 0);
    
    memcpy(buf, a.data, a.len);
    memcpy(buf + a.len, b.data, b.len);
    buf[total] = '\0';
    
    return js_string_from_literal(buf, total);
}

static bool string_starts_with(JsString s, const char *prefix) {
    size_t prefix_len = strlen(prefix);
    if (s.len < prefix_len) return false;
    return memcmp(s.data, prefix, prefix_len) == 0;
}

static bool string_equals_literal(JsString s, const char *lit) {
    size_t lit_len = strlen(lit);
    if (s.len != lit_len) return false;
    return memcmp(s.data, lit, lit_len) == 0;
}

/* ============================================================================
 * Value Conversions (JavaScript semantics)
 * ============================================================================ */

bool js_is_truthy(JsValue val) {
    switch (val.type) {
        case JS_TYPE_UNDEFINED:
        case JS_TYPE_NULL:
            return false;
        case JS_TYPE_BOOL:
            return JS_AS_BOOL(val);
        case JS_TYPE_NUMBER:
            return JS_AS_NUMBER(val) != 0 && !isnan(JS_AS_NUMBER(val));
        case JS_TYPE_STRING:
            return JS_AS_STRING(val) != NULL && strlen(JS_AS_STRING(val)) > 0;
        case JS_TYPE_ARRAY:
            return JS_AS_ARRAY(val)->count > 0;
        case JS_TYPE_OBJECT:
            return JS_AS_OBJECT(val)->prop_count > 0;
        default:
            return true;
    }
}

double js_to_number(JsValue val) {
    switch (val.type) {
        case JS_TYPE_UNDEFINED:
            return NAN;
        case JS_TYPE_NULL:
            return 0;
        case JS_TYPE_BOOL:
            return JS_AS_BOOL(val) ? 1 : 0;
        case JS_TYPE_NUMBER:
            return JS_AS_NUMBER(val);
        case JS_TYPE_STRING: {
            const char *s = JS_AS_STRING(val);
            if (!s || strlen(s) == 0) return 0;
            char *end;
            double d = strtod(s, &end);
            if (*end != '\0') return NAN;
            return d;
        }
        default:
            return NAN;
    }
}

JsString js_to_string(JsContext *ctx, JsValue val) {
    char buf[64];
    
    switch (val.type) {
        case JS_TYPE_UNDEFINED:
            return js_string_from_literal("undefined", 9);
        case JS_TYPE_NULL:
            return js_string_from_literal("null", 4);
        case JS_TYPE_BOOL:
            return JS_AS_BOOL(val) ? 
                js_string_from_literal("true", 4) : 
                js_string_from_literal("false", 5);
        case JS_TYPE_NUMBER: {
            double n = JS_AS_NUMBER(val);
            if (isnan(n)) return js_string_from_literal("NaN", 3);
            if (isinf(n)) return n < 0 ? 
                js_string_from_literal("-Infinity", 9) : 
                js_string_from_literal("Infinity", 8);
            int len = snprintf(buf, sizeof(buf), "%g", n);
            return js_make_string(ctx, buf, len).type == JS_TYPE_STRING ?
                js_string_from_literal(JS_AS_STRING(js_make_string(ctx, buf, len)), 0) :
                js_string_from_literal("", 0);
        }
        case JS_TYPE_STRING: {
            const char *s = JS_AS_STRING(val);
            return js_string_from_literal(s, strlen(s));
        }
        default:
            return js_string_from_literal("[object Object]", 15);
    }
}

/* ============================================================================
 * Variable Scope Management
 * ============================================================================ */

void scope_push(JsContext *ctx) {
    if (ctx->scope_depth >= JS_MAX_SCOPES) return;
    
    JsScope *scope = &ctx->scopes[ctx->scope_depth];
    scope->var_count = 0;
    scope->parent = ctx->scope_depth > 0 ? 
        &ctx->scopes[ctx->scope_depth - 1] : NULL;
    
    ctx->scope_depth++;
}

void scope_pop(JsContext *ctx) {
    if (ctx->scope_depth > 0) {
        ctx->scope_depth--;
    }
}

JsVar *scope_find(JsContext *ctx, JsString name) {
    for (int i = (int)ctx->scope_depth - 1; i >= 0; i--) {
        JsScope *scope = &ctx->scopes[i];
        for (size_t j = 0; j < scope->var_count; j++) {
            if (js_string_equal(scope->vars[j].name, name)) {
                return &scope->vars[j];
            }
        }
    }
    return NULL;
}

JsVar *scope_define(JsContext *ctx, JsString name, JsValue value) {
    if (ctx->scope_depth == 0) return NULL;
    
    JsScope *scope = &ctx->scopes[ctx->scope_depth - 1];
    if (scope->var_count >= JS_MAX_VARS) return NULL;
    
    JsVar *var = &scope->vars[scope->var_count++];
    var->name = name;
    var->value = value;
    var->is_local = true;
    return var;
}

static void scope_set(JsContext *ctx, JsString name, JsValue value) {
    JsVar *var = scope_find(ctx, name);
    if (var) {
        var->value = value;
    } else {
        scope_define(ctx, name, value);
    }
}

/* ============================================================================
 * Lexer - Tokenize JavaScript
 * ============================================================================ */

static const char *keywords[] = {
    "var", "let", "const", "function", "return",
    "if", "else", "while", "for", "break", "continue",
    "switch", "case", "default", "try", "catch", "finally",
    "throw", "new", "this", "true", "false", "null",
    "undefined", "void", "typeof", "instanceof", "in", "of",
    NULL
};

static const int keyword_ids[] = {
    KW_VAR, KW_LET, KW_CONST, KW_FUNCTION, KW_RETURN,
    KW_IF, KW_ELSE, KW_WHILE, KW_FOR, KW_BREAK, KW_CONTINUE,
    KW_SWITCH, KW_CASE, KW_DEFAULT, KW_TRY, KW_CATCH, KW_FINALLY,
    KW_THROW, KW_NEW, KW_THIS, KW_TRUE, KW_FALSE, KW_NULL,
    KW_UNDEFINED, KW_VOID, KW_TYPEOF, KW_INSTANCEOF, KW_IN, KW_OF,
};

static void lexer_skip_whitespace(JsLexer *lex) {
    while (lex->pos < lex->code_len) {
        char c = lex->code[lex->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            lex->pos++;
        } else if (c == '/' && lex->pos + 1 < lex->code_len) {
            char next = lex->code[lex->pos + 1];
            if (next == '/') {
                /* Single-line comment */
                lex->pos += 2;
                while (lex->pos < lex->code_len && lex->code[lex->pos] != '\n') {
                    lex->pos++;
                }
            } else if (next == '*') {
                /* Multi-line comment */
                lex->pos += 2;
                while (lex->pos + 1 < lex->code_len) {
                    if (lex->code[lex->pos] == '*' && lex->code[lex->pos + 1] == '/') {
                        lex->pos += 2;
                        break;
                    }
                    lex->pos++;
                }
            } else {
                break;
            }
        } else {
            break;
        }
    }
}

static bool is_ident_start(char c) {
    return isalpha(c) || c == '_' || c == '$';
}

static bool is_ident_char(char c) {
    return isalnum(c) || c == '_' || c == '$';
}

static int get_keyword_id(const char *str, size_t len) {
    for (int i = 0; keywords[i] != NULL; i++) {
        if (strlen(keywords[i]) == len && memcmp(keywords[i], str, len) == 0) {
            return keyword_ids[i];
        }
    }
    return 0;
}

static JsToken lexer_next_token(JsLexer *lex) {
    lexer_skip_whitespace(lex);
    
    JsToken tok = {0};
    tok.type = TOK_EOF;
    tok.start = lex->code + lex->pos;
    tok.len = 0;
    
    if (lex->pos >= lex->code_len) {
        return tok;
    }
    
    char c = lex->code[lex->pos];
    
    /* Number */
    if (isdigit(c) || (c == '.' && lex->pos + 1 < lex->code_len && isdigit(lex->code[lex->pos + 1]))) {
        tok.type = TOK_NUMBER;
        size_t start = lex->pos;
        bool has_dot = false;
        
        while (lex->pos < lex->code_len) {
            c = lex->code[lex->pos];
            if (isdigit(c)) {
                lex->pos++;
            } else if (c == '.' && !has_dot) {
                has_dot = true;
                lex->pos++;
            } else if (c == 'e' || c == 'E') {
                lex->pos++;
                if (lex->pos < lex->code_len && (lex->code[lex->pos] == '+' || lex->code[lex->pos] == '-')) {
                    lex->pos++;
                }
            } else {
                break;
            }
        }
        
        tok.len = lex->pos - start;
        char *end;
        tok.value.num_val = strtod(tok.start, &end);
        return tok;
    }
    
    /* String */
    if (c == '"' || c == '\'' || c == '`') {
        char quote = c;
        tok.type = TOK_STRING;
        lex->pos++; /* Skip opening quote */
        size_t start = lex->pos;
        
        while (lex->pos < lex->code_len && lex->code[lex->pos] != quote) {
            if (lex->code[lex->pos] == '\\' && lex->pos + 1 < lex->code_len) {
                lex->pos += 2;
            } else {
                lex->pos++;
            }
        }
        
        tok.len = lex->pos - start;
        if (lex->pos < lex->code_len) {
            lex->pos++; /* Skip closing quote */
        }
        return tok;
    }
    
    /* Identifier/Keyword */
    if (is_ident_start(c)) {
        tok.type = TOK_IDENTIFIER;
        size_t start = lex->pos;
        
        while (lex->pos < lex->code_len && is_ident_char(lex->code[lex->pos])) {
            lex->pos++;
        }
        
        tok.len = lex->pos - start;
        
        /* Check if keyword */
        int kw = get_keyword_id(tok.start, tok.len);
        if (kw) {
            tok.type = TOK_KEYWORD;
            tok.value.keyword = kw;
        }
        
        return tok;
    }
    
    /* Operators and punctuation */
    static const struct {
        const char *op;
        int id;
        size_t len;
    } operators[] = {
        {"===", OP_STRICT_EQ, 3}, {"!==", OP_STRICT_NE, 3},
        {"**", OP_POW, 2}, {"==", OP_EQ, 2}, {"!=", OP_NE, 2},
        {"<=", OP_LE, 2}, {">=", OP_GE, 2}, {"++", OP_PRE_INC, 2},
        {"--", OP_PRE_DEC, 2}, {"&&", OP_AND, 2}, {"||", OP_OR, 2},
        {"??", OP_NULLISH_COALESCE, 2}, {"<<", OP_SHL, 2},
        {">>", OP_SHR, 2}, {">>>", OP_USHR, 3},
        {"+=", OP_ADD_ASSIGN, 2}, {"-=" , OP_SUB_ASSIGN, 2},
        {"*=", OP_MUL_ASSIGN, 2}, {"/=", OP_DIV_ASSIGN, 2},
        {"%=", OP_MOD_ASSIGN, 2}, {"&=", OP_BIT_AND, 1},
        {"|=", OP_BIT_OR, 1}, {"^=", OP_BIT_XOR, 1},
        {"=", OP_ASSIGN, 1}, {"+", OP_ADD, 1}, {"-", OP_SUB, 1},
        {"*", OP_MUL, 1}, {"/", OP_DIV, 1}, {"%", OP_MOD, 1},
        {"&", OP_BIT_AND, 1}, {"|", OP_BIT_OR, 1}, {"^", OP_BIT_XOR, 1},
        {"~", OP_BIT_NOT, 1}, {"<", OP_LT, 1}, {">", OP_GT, 1},
        {"!", OP_NOT, 1}, {"?", OP_TERNARY, 1},
        {NULL, 0, 0}
    };
    
    for (int i = 0; operators[i].op != NULL; i++) {
        size_t oplen = operators[i].len;
        if (lex->pos + oplen <= lex->code_len &&
            memcmp(lex->code + lex->pos, operators[i].op, oplen) == 0) {
            tok.type = TOK_OPERATOR;
            tok.value.op = operators[i].id;
            tok.len = oplen;
            lex->pos += oplen;
            return tok;
        }
    }
    
    /* Single character punctuation */
    tok.type = TOK_PUNCT;
    tok.len = 1;
    lex->pos++;
    
    return tok;
}

static void lexer_init(JsLexer *lex, const char *code, size_t len) {
    lex->code = code;
    lex->code_len = len;
    lex->pos = 0;
    lex->has_peek = false;
    lex->current = lexer_next_token(lex);
}

static JsToken lexer_peek(JsLexer *lex) {
    if (!lex->has_peek) {
        lex->peek = lexer_next_token(lex);
        lex->has_peek = true;
    }
    return lex->peek;
}

static JsToken lexer_advance(JsLexer *lex) {
    JsToken current = lex->current;
    if (lex->has_peek) {
        lex->current = lex->peek;
        lex->has_peek = false;
    } else {
        lex->current = lexer_next_token(lex);
    }
    return current;
}

static bool lexer_expect(JsLexer *lex, JsTokenType type, const char *lit) {
    JsToken tok = lexer_advance(lex);
    if (tok.type != type) return false;
    if (lit && (tok.len != strlen(lit) || memcmp(tok.start, lit, tok.len) != 0)) {
        return false;
    }
    return true;
}

/* ============================================================================
 * Context Initialization
 * ============================================================================ */

bool js_init(JsContext *ctx, void *arena_buffer, size_t arena_size) {
    memset(ctx, 0, sizeof(JsContext));
    
    arena_init(&ctx->arena, arena_buffer, arena_size);
    ctx->scope_depth = 0;
    ctx->recursion_depth = 0;
    ctx->max_recursion = JS_MAX_STACK_DEPTH;
    ctx->had_error = false;
    
    scope_push(ctx); /* Global scope */
    
    return true;
}

void js_reset(JsContext *ctx) {
    arena_reset(&ctx->arena);
    ctx->scope_depth = 0;
    ctx->recursion_depth = 0;
    ctx->had_error = false;
    ctx->error[0] = '\0';
    scope_push(ctx);
}

bool js_load_code(JsContext *ctx, const char *code, size_t len) {
    if (len == 0) len = strlen(code);
    if (len > JS_MAX_CODE_SIZE) return false;
    
    ctx->code = code;
    ctx->code_len = len;
    
    lexer_init(&ctx->lexer, code, len);
    return true;
}

const char *js_get_error(JsContext *ctx) {
    return ctx->had_error ? ctx->error : NULL;
}

void js_clear_error(JsContext *ctx) {
    ctx->had_error = false;
    ctx->error[0] = '\0';
}

static void set_error(JsContext *ctx, const char *msg) {
    ctx->had_error = true;
    strncpy(ctx->error, msg, sizeof(ctx->error) - 1);
    ctx->error[sizeof(ctx->error) - 1] = '\0';
}

/* ============================================================================
 * Expression Parser - Recursive Descent
 * ============================================================================ */

/* Forward declarations */
static JsValue eval_expr(JsContext *ctx, int min_prec);
static JsValue eval_primary(JsContext *ctx);

/* Operator precedence (higher = tighter binding) */
static int get_precedence(int op) {
    switch (op) {
        case OP_COMMA: return 1;
        case OP_ASSIGN: case OP_ADD_ASSIGN: case OP_SUB_ASSIGN:
        case OP_MUL_ASSIGN: case OP_DIV_ASSIGN: case OP_MOD_ASSIGN:
            return 2;
        case OP_TERNARY: return 3;
        case OP_OR: return 4;
        case OP_AND: return 5;
        case OP_BIT_OR: return 6;
        case OP_BIT_XOR: return 7;
        case OP_BIT_AND: return 8;
        case OP_EQ: case OP_NE: case OP_STRICT_EQ: case OP_STRICT_NE:
            return 9;
        case OP_LT: case OP_GT: case OP_LE: case OP_GE:
        case KW_INSTANCEOF: case KW_IN:
            return 10;
        case OP_SHL: case OP_SHR: case OP_USHR:
            return 11;
        case OP_ADD: case OP_SUB:
            return 12;
        case OP_MUL: case OP_DIV: case OP_MOD:
            return 13;
        case OP_POW:
            return 14;
        default:
            return 0;
    }
}

/* Parse and evaluate a primary expression */
static JsValue eval_primary(JsContext *ctx) {
    JsLexer *lex = &ctx->lexer;
    JsToken tok = lexer_advance(lex);
    
    switch (tok.type) {
        case TOK_NUMBER:
            return js_make_number(tok.value.num_val);
            
        case TOK_STRING: {
            /* Need to unescape the string */
            char *buf = (char *)js_arena_alloc(&ctx->arena, tok.len + 1);
            if (!buf) return js_make_undefined();
            
            size_t j = 0;
            for (size_t i = 0; i < tok.len; i++) {
                if (tok.start[i] == '\\' && i + 1 < tok.len) {
                    char esc = tok.start[i + 1];
                    switch (esc) {
                        case 'n': buf[j++] = '\n'; i++; break;
                        case 't': buf[j++] = '\t'; i++; break;
                        case 'r': buf[j++] = '\r'; i++; break;
                        case '\\': buf[j++] = '\\'; i++; break;
                        case '"': buf[j++] = '"'; i++; break;
                        case '\'': buf[j++] = '\''; i++; break;
                        case 'x':
                            if (i + 3 < tok.len) {
                                char hex[3] = {tok.start[i+2], tok.start[i+3], '\0'};
                                buf[j++] = (char)strtol(hex, NULL, 16);
                                i += 3;
                            }
                            break;
                        case 'u':
                            if (i + 5 < tok.len && tok.start[i+2] == '{') {
                                /* \u{xxxx} */
                                const char *end = memchr(tok.start + i + 3, '}', tok.len - i - 3);
                                if (end) {
                                    /* Simplified: just skip for now */
                                    i = (size_t)(end - tok.start);
                                }
                            } else if (i + 4 < tok.len) {
                                /* \uxxxx */
                                char hex[5] = {tok.start[i+2], tok.start[i+3], tok.start[i+4], tok.start[i+5], '\0'};
                                long code = strtol(hex, NULL, 16);
                                if (code < 128) buf[j++] = (char)code;
                                i += 4;
                            }
                            break;
                        default:
                            buf[j++] = tok.start[i];
                    }
                } else {
                    buf[j++] = tok.start[i];
                }
            }
            buf[j] = '\0';
            return js_make_string_nocopy(buf, j);
        }
        
        case TOK_KEYWORD:
            switch (tok.value.keyword) {
                case KW_TRUE:
                    return js_make_bool(true);
                case KW_FALSE:
                    return js_make_bool(false);
                case KW_NULL:
                    return js_make_null();
                case KW_UNDEFINED:
                    return js_make_undefined();
                case KW_VOID: {
                    JsValue expr = eval_expr(ctx, 0);
                    (void)expr;
                    return js_make_undefined();
                }
                default:
                    set_error(ctx, "Unexpected keyword");
                    return js_make_undefined();
            }
            
        case TOK_IDENTIFIER: {
            JsString name = js_string_from_literal(tok.start, tok.len);
            
            /* Check for function call */
            if (lexer_peek(lex).type == TOK_PUNCT && 
                lexer_peek(lex).start[0] == '(') {
                lexer_advance(lex); /* consume ( */
                
                /* Parse arguments */
                JsValue args[16];
                size_t arg_count = 0;
                
                if (lexer_peek(lex).type != TOK_PUNCT || 
                    lexer_peek(lex).start[0] != ')') {
                    while (arg_count < 16) {
                        args[arg_count++] = eval_expr(ctx, 0);
                        JsToken next = lexer_advance(lex);
                        if (next.type == TOK_PUNCT && next.start[0] == ')') {
                            break;
                        }
                        if (next.type != TOK_PUNCT || next.start[0] != ',') {
                            set_error(ctx, "Expected , or )");
                            return js_make_undefined();
                        }
                    }
                } else {
                    lexer_advance(lex); /* consume ) */
                }
                
                /* Call the function */
                return js_call_function(ctx, name.data, args, arg_count);
            }
            
            /* Check for array/object access */
            if (lexer_peek(lex).type == TOK_PUNCT && lexer_peek(lex).start[0] == '[') {
                lexer_advance(lex); /* consume [ */
                JsValue idx = eval_expr(ctx, 0);
                if (!lexer_expect(lex, TOK_PUNCT, "]")) {
                    set_error(ctx, "Expected ]");
                    return js_make_undefined();
                }
                
                JsVar *var = scope_find(ctx, name);
                if (!var || !JS_IS_ARRAY(var->value)) {
                    return js_make_undefined();
                }
                
                JsArray *arr = JS_AS_ARRAY(var->value);
                int i = (int)js_to_number(idx);
                if (i < 0 || (size_t)i >= arr->count) {
                    return js_make_undefined();
                }
                return arr->items[i];
            }
            
            /* Property access */
            if (lexer_peek(lex).type == TOK_PUNCT && lexer_peek(lex).start[0] == '.') {
                lexer_advance(lex); /* consume . */
                JsToken member = lexer_advance(lex);
                if (member.type != TOK_IDENTIFIER) {
                    set_error(ctx, "Expected property name");
                    return js_make_undefined();
                }
                
                JsString member_name = js_string_from_literal(member.start, member.len);
                JsVar *var = scope_find(ctx, name);
                if (!var) return js_make_undefined();
                
                /* Handle built-in methods */
                if (JS_IS_STRING(var->value)) {
                    const char *str = JS_AS_STRING(var->value);
                    if (string_equals_literal(member_name, "length")) {
                        return js_make_number((double)strlen(str));
                    } else if (string_equals_literal(member_name, "split")) {
                        /* Expect function call */
                        if (lexer_peek(lex).type == TOK_PUNCT && lexer_peek(lex).start[0] == '(') {
                            lexer_advance(lex);
                            JsValue sep = eval_expr(ctx, 0);
                            if (!lexer_expect(lex, TOK_PUNCT, ")")) {
                                return js_make_undefined();
                            }
                            
                            const char *sep_str = JS_IS_STRING(sep) ? JS_AS_STRING(sep) : "";
                            JsValue arr_val = js_make_array(ctx, 16);
                            JsArray *arr = JS_AS_ARRAY(arr_val);
                            
                            if (sep_str[0] == '\0') {
                                /* Split into characters */
                                for (size_t i = 0; i < strlen(str); i++) {
                                    char ch[2] = {str[i], '\0'};
                                    arr->items[arr->count++] = js_make_string(ctx, ch, 1);
                                }
                            } else {
                                /* Simple split on delimiter */
                                const char *start = str;
                                const char *p = strstr(start, sep_str);
                                while (p) {
                                    arr->items[arr->count++] = js_make_string(ctx, start, (size_t)(p - start));
                                    start = p + strlen(sep_str);
                                    p = strstr(start, sep_str);
                                }
                                arr->items[arr->count++] = js_make_string(ctx, start, strlen(start));
                            }
                            return arr_val;
                        }
                    } else if (string_equals_literal(member_name, "slice")) {
                        if (lexer_peek(lex).type == TOK_PUNCT && lexer_peek(lex).start[0] == '(') {
                            lexer_advance(lex);
                            JsValue start_val = eval_expr(ctx, 0);
                            int start = (int)js_to_number(start_val);
                            int end = (int)strlen(str);
                            
                            if (lexer_peek(lex).type == TOK_PUNCT && lexer_peek(lex).start[0] == ',') {
                                lexer_advance(lex);
                                JsValue end_val = eval_expr(ctx, 0);
                                end = (int)js_to_number(end_val);
                            }
                            if (!lexer_expect(lex, TOK_PUNCT, ")")) {
                                return js_make_undefined();
                            }
                            
                            int len = (int)strlen(str);
                            if (start < 0) start = len + start;
                            if (end < 0) end = len + end;
                            if (start < 0) start = 0;
                            if (end > len) end = len;
                            
                            return js_make_string(ctx, str + start, (size_t)(end - start));
                        }
                    }
                } else if (JS_IS_ARRAY(var->value)) {
                    JsArray *arr = JS_AS_ARRAY(var->value);
                    if (string_equals_literal(member_name, "length")) {
                        return js_make_number((double)arr->count);
                    } else if (string_equals_literal(member_name, "join")) {
                        if (lexer_peek(lex).type == TOK_PUNCT && lexer_peek(lex).start[0] == '(') {
                            lexer_advance(lex);
                            JsValue sep = eval_expr(ctx, 0);
                            if (!lexer_expect(lex, TOK_PUNCT, ")")) {
                                return js_make_undefined();
                            }
                            
                            const char *sep_str = JS_IS_STRING(sep) ? JS_AS_STRING(sep) : ",";
                            
                            /* Build joined string */
                            size_t total_len = 0;
                            for (size_t i = 0; i < arr->count; i++) {
                                JsString s = js_to_string(ctx, arr->items[i]);
                                total_len += s.len;
                                if (i < arr->count - 1) total_len += strlen(sep_str);
                            }
                            
                            char *result = (char *)js_arena_alloc(&ctx->arena, total_len + 1);
                            if (!result) return js_make_undefined();
                            
                            size_t pos = 0;
                            for (size_t i = 0; i < arr->count; i++) {
                                JsString s = js_to_string(ctx, arr->items[i]);
                                memcpy(result + pos, s.data, s.len);
                                pos += s.len;
                                if (i < arr->count - 1) {
                                    memcpy(result + pos, sep_str, strlen(sep_str));
                                    pos += strlen(sep_str);
                                }
                            }
                            result[pos] = '\0';
                            return js_make_string_nocopy(result, pos);
                        }
                    } else if (string_equals_literal(member_name, "reverse")) {
                        if (lexer_peek(lex).type == TOK_PUNCT && lexer_peek(lex).start[0] == '(') {
                            lexer_advance(lex);
                            lexer_expect(lex, TOK_PUNCT, ")");
                            
                            for (size_t i = 0; i < arr->count / 2; i++) {
                                JsValue tmp = arr->items[i];
                                arr->items[i] = arr->items[arr->count - 1 - i];
                                arr->items[arr->count - 1 - i] = tmp;
                            }
                            return var->value;
                        }
                    }
                }
            }
            
            /* Variable lookup */
            JsVar *var = scope_find(ctx, name);
            if (var) {
                return var->value;
            }
            return js_make_undefined();
        }
        
        case TOK_PUNCT:
            if (tok.start[0] == '(') {
                JsValue val = eval_expr(ctx, 0);
                if (!lexer_expect(lex, TOK_PUNCT, ")")) {
                    set_error(ctx, "Expected )");
                    return js_make_undefined();
                }
                return val;
            }
            if (tok.start[0] == '[') {
                /* Array literal */
                JsValue arr_val = js_make_array(ctx, 16);
                JsArray *arr = JS_AS_ARRAY(arr_val);
                
                if (lexer_peek(lex).type == TOK_PUNCT && lexer_peek(lex).start[0] == ']') {
                    lexer_advance(lex);
                    return arr_val;
                }
                
                while (arr->count < 16) {
                    arr->items[arr->count++] = eval_expr(ctx, 0);
                    JsToken next = lexer_advance(lex);
                    if (next.type == TOK_PUNCT && next.start[0] == ']') {
                        break;
                    }
                    if (next.type != TOK_PUNCT || next.start[0] != ',') {
                        set_error(ctx, "Expected , or ]");
                        return js_make_undefined();
                    }
                }
                return arr_val;
            }
            if (tok.start[0] == '{') {
                /* Object literal - simplified */
                /* Just skip to matching } for now */
                int depth = 1;
                while (depth > 0 && lexer_peek(lex).type != TOK_EOF) {
                    JsToken t = lexer_advance(lex);
                    if (t.type == TOK_PUNCT) {
                        if (t.start[0] == '{') depth++;
                        else if (t.start[0] == '}') depth--;
                    }
                }
                return js_make_object(ctx);
            }
            /* Fall through for unary operators */
            break;
            
        default:
            break;
    }
    
    set_error(ctx, "Unexpected token");
    return js_make_undefined();
}

/* Evaluate expression with operator precedence climbing */
static JsValue eval_expr(JsContext *ctx, int min_prec) {
    JsLexer *lex = &ctx->lexer;
    
    /* Check for prefix unary operators */
    JsToken tok = lexer_peek(lex);
    JsValue lhs;
    
    if (tok.type == TOK_OPERATOR) {
        int op = tok.value.op;
        if (op == OP_ADD || op == OP_SUB || op == OP_NOT || 
            op == OP_BIT_NOT || op == OP_PRE_INC || op == OP_PRE_DEC) {
            lexer_advance(lex);
            lhs = eval_expr(ctx, get_precedence(OP_POW)); /* Higher than postfix */
            
            double val = js_to_number(lhs);
            switch (op) {
                case OP_SUB: lhs = js_make_number(-val); break;
                case OP_ADD: lhs = js_make_number(val); break;
                case OP_NOT: lhs = js_make_bool(!js_is_truthy(lhs)); break;
                case OP_BIT_NOT: lhs = js_make_number((double)(~((int32_t)val))); break;
                case OP_PRE_INC: {
                    /* Need to handle variable reference - simplified */
                    lhs = js_make_number(val + 1);
                    break;
                }
                case OP_PRE_DEC: {
                    lhs = js_make_number(val - 1);
                    break;
                }
            }
        } else {
            lhs = eval_primary(ctx);
        }
    } else {
        lhs = eval_primary(ctx);
    }
    
    /* Infix operators */
    while (true) {
        tok = lexer_peek(lex);
        if (tok.type != TOK_OPERATOR && tok.type != TOK_KEYWORD) {
            break;
        }
        
        int op = (tok.type == TOK_KEYWORD) ? tok.value.keyword : tok.value.op;
        int prec = get_precedence(op);
        
        if (prec < min_prec) {
            break;
        }
        
        lexer_advance(lex);
        
        /* Ternary operator is right-associative */
        int next_min_prec = (op == OP_TERNARY) ? prec : prec + 1;
        
        if (op == OP_TERNARY) {
            JsValue if_true = eval_expr(ctx, 0);
            if (!lexer_expect(lex, TOK_PUNCT, ":")) {
                set_error(ctx, "Expected :");
                return js_make_undefined();
            }
            JsValue if_false = eval_expr(ctx, 0);
            lhs = js_is_truthy(lhs) ? if_true : if_false;
        } else if (op == OP_AND) {
            if (!js_is_truthy(lhs)) {
                /* Short circuit */
                eval_expr(ctx, next_min_prec); /* Still consume rhs */
                lhs = js_make_bool(false);
            } else {
                JsValue rhs = eval_expr(ctx, next_min_prec);
                lhs = js_make_bool(js_is_truthy(rhs));
            }
        } else if (op == OP_OR) {
            if (js_is_truthy(lhs)) {
                /* Short circuit */
                eval_expr(ctx, next_min_prec);
            } else {
                JsValue rhs = eval_expr(ctx, next_min_prec);
                lhs = rhs;
            }
        } else if (op == OP_NULLISH_COALESCE) {
            if (lhs.type != JS_TYPE_UNDEFINED && lhs.type != JS_TYPE_NULL) {
                eval_expr(ctx, next_min_prec);
            } else {
                lhs = eval_expr(ctx, next_min_prec);
            }
        } else {
            JsValue rhs = eval_expr(ctx, next_min_prec);
            
            /* Apply operator */
            double a = js_to_number(lhs);
            double b = js_to_number(rhs);
            int32_t ia = (int32_t)a;
            int32_t ib = (int32_t)b;
            
            switch (op) {
                case OP_ADD: {
                    if (JS_IS_STRING(lhs) || JS_IS_STRING(rhs)) {
                        JsString sa = js_to_string(ctx, lhs);
                        JsString sb = js_to_string(ctx, rhs);
                        lhs = js_make_string(ctx, sa.data, sa.len);
                        /* Actually need to concat - simplified */
                    } else {
                        lhs = js_make_number(a + b);
                    }
                    break;
                }
                case OP_SUB: lhs = js_make_number(a - b); break;
                case OP_MUL: lhs = js_make_number(a * b); break;
                case OP_DIV: lhs = js_make_number(b != 0 ? a / b : INFINITY); break;
                case OP_MOD: lhs = js_make_number(b != 0 ? fmod(a, b) : NAN); break;
                case OP_POW: lhs = js_make_number(pow(a, b)); break;
                case OP_BIT_AND: lhs = js_make_number((double)(ia & ib)); break;
                case OP_BIT_OR: lhs = js_make_number((double)(ia | ib)); break;
                case OP_BIT_XOR: lhs = js_make_number((double)(ia ^ ib)); break;
                case OP_SHL: lhs = js_make_number((double)(ia << (ib & 31))); break;
                case OP_SHR: lhs = js_make_number((double)(ia >> (ib & 31))); break;
                case OP_EQ: lhs = js_make_bool(a == b); break;
                case OP_NE: lhs = js_make_bool(a != b); break;
                case OP_STRICT_EQ: lhs = js_make_bool(lhs.type == rhs.type && a == b); break;
                case OP_STRICT_NE: lhs = js_make_bool(lhs.type != rhs.type || a != b); break;
                case OP_LT: lhs = js_make_bool(a < b); break;
                case OP_GT: lhs = js_make_bool(a > b); break;
                case OP_LE: lhs = js_make_bool(a <= b); break;
                case OP_GE: lhs = js_make_bool(a >= b); break;
                case OP_ASSIGN: {
                    lhs = rhs;
                    break;
                }
                default:
                    set_error(ctx, "Unknown operator");
                    return js_make_undefined();
            }
        }
    }
    
    return lhs;
}

/* ============================================================================
 * Statement Parser
 * ============================================================================ */

static JsValue eval_statement(JsContext *ctx);

static JsValue eval_var_decl(JsContext *ctx) {
    JsLexer *lex = &ctx->lexer;
    
    while (true) {
        JsToken name = lexer_advance(lex);
        if (name.type != TOK_IDENTIFIER) {
            set_error(ctx, "Expected variable name");
            return js_make_undefined();
        }
        
        JsString var_name = js_string_from_literal(name.start, name.len);
        JsValue val = js_make_undefined();
        
        if (lexer_peek(lex).type == TOK_OPERATOR && 
            lexer_peek(lex).value.op == OP_ASSIGN) {
            lexer_advance(lex); /* consume = */
            val = eval_expr(ctx, 0);
        }
        
        scope_define(ctx, var_name, val);
        
        if (lexer_peek(lex).type == TOK_PUNCT && lexer_peek(lex).start[0] == ',') {
            lexer_advance(lex); /* consume , */
            continue;
        }
        break;
    }
    
    return js_make_undefined();
}

static JsValue eval_statement(JsContext *ctx) {
    JsLexer *lex = &ctx->lexer;
    JsToken tok = lexer_peek(lex);
    
    if (tok.type == TOK_KEYWORD) {
        switch (tok.value.keyword) {
            case KW_VAR:
            case KW_LET:
            case KW_CONST:
                lexer_advance(lex);
                return eval_var_decl(ctx);
                
            case KW_RETURN: {
                lexer_advance(lex);
                JsValue val = js_make_undefined();
                if (lexer_peek(lex).type != TOK_EOF &&
                    !(lexer_peek(lex).type == TOK_PUNCT && lexer_peek(lex).start[0] == ';')) {
                    val = eval_expr(ctx, 0);
                }
                return val;
            }
            
            case KW_IF: {
                lexer_advance(lex);
                if (!lexer_expect(lex, TOK_PUNCT, "(")) {
                    set_error(ctx, "Expected (");
                    return js_make_undefined();
                }
                JsValue cond = eval_expr(ctx, 0);
                if (!lexer_expect(lex, TOK_PUNCT, ")")) {
                    set_error(ctx, "Expected )");
                    return js_make_undefined();
                }
                
                JsValue result = js_make_undefined();
                if (js_is_truthy(cond)) {
                    result = eval_statement(ctx);
                } else {
                    /* Skip the then branch */
                    if (lexer_peek(lex).type == TOK_PUNCT && lexer_peek(lex).start[0] == '{') {
                        int depth = 1;
                        lexer_advance(lex);
                        while (depth > 0 && lexer_peek(lex).type != TOK_EOF) {
                            JsToken t = lexer_advance(lex);
                            if (t.type == TOK_PUNCT) {
                                if (t.start[0] == '{') depth++;
                                else if (t.start[0] == '}') depth--;
                            }
                        }
                    } else {
                        eval_statement(ctx);
                    }
                    
                    /* Check for else */
                    if (lexer_peek(lex).type == TOK_KEYWORD &&
                        lexer_peek(lex).value.keyword == KW_ELSE) {
                        lexer_advance(lex);
                        result = eval_statement(ctx);
                    }
                }
                return result;
            }
            
            case KW_WHILE: {
                lexer_advance(lex);
                size_t loop_start = lex->pos;
                
                if (!lexer_expect(lex, TOK_PUNCT, "(")) {
                    set_error(ctx, "Expected (");
                    return js_make_undefined();
                }
                
                /* Save condition position */
                size_t cond_start = lex->pos;
                JsValue cond = eval_expr(ctx, 0);
                size_t cond_end = lex->pos;
                
                if (!lexer_expect(lex, TOK_PUNCT, ")")) {
                    set_error(ctx, "Expected )");
                    return js_make_undefined();
                }
                
                /* Save body position */
                size_t body_start = lex->pos;
                eval_statement(ctx); /* Execute once to find end */
                size_t body_end = lex->pos;
                
                /* Execute loop */
                while (js_is_truthy(cond)) {
                    /* Execute body */
                    lex->pos = body_start;
                    eval_statement(ctx);
                    
                    /* Re-evaluate condition */
                    lex->pos = cond_start;
                    cond = eval_expr(ctx, 0);
                }
                
                lex->pos = body_end;
                return js_make_undefined();
            }
            
            case KW_FOR: {
                lexer_advance(lex);
                if (!lexer_expect(lex, TOK_PUNCT, "(")) {
                    set_error(ctx, "Expected (");
                    return js_make_undefined();
                }
                
                /* Init */
                if (lexer_peek(lex).type == TOK_KEYWORD && 
                    (lexer_peek(lex).value.keyword == KW_VAR ||
                     lexer_peek(lex).value.keyword == KW_LET ||
                     lexer_peek(lex).value.keyword == KW_CONST)) {
                    lexer_advance(lex);
                    eval_var_decl(ctx);
                } else if (!(lexer_peek(lex).type == TOK_PUNCT && lexer_peek(lex).start[0] == ';')) {
                    eval_expr(ctx, 0);
                }
                
                if (!lexer_expect(lex, TOK_PUNCT, ";")) {
                    set_error(ctx, "Expected ;");
                    return js_make_undefined();
                }
                
                /* Condition */
                size_t cond_start = lex->pos;
                JsValue cond = js_make_bool(true);
                if (!(lexer_peek(lex).type == TOK_PUNCT && lexer_peek(lex).start[0] == ';')) {
                    cond = eval_expr(ctx, 0);
                }
                size_t cond_end = lex->pos;
                
                if (!lexer_expect(lex, TOK_PUNCT, ";")) {
                    set_error(ctx, "Expected ;");
                    return js_make_undefined();
                }
                
                /* Increment */
                size_t incr_start = lex->pos;
                if (!(lexer_peek(lex).type == TOK_PUNCT && lexer_peek(lex).start[0] == ')')) {
                    eval_expr(ctx, 0);
                }
                size_t incr_end = lex->pos;
                
                if (!lexer_expect(lex, TOK_PUNCT, ")")) {
                    set_error(ctx, "Expected )");
                    return js_make_undefined();
                }
                
                /* Body */
                size_t body_start = lex->pos;
                eval_statement(ctx);
                size_t body_end = lex->pos;
                
                /* Execute loop */
                while (js_is_truthy(cond)) {
                    /* Execute body */
                    lex->pos = body_start;
                    eval_statement(ctx);
                    
                    /* Execute increment */
                    lex->pos = incr_start;
                    if (incr_start != incr_end) {
                        eval_expr(ctx, 0);
                    }
                    
                    /* Re-evaluate condition */
                    lex->pos = cond_start;
                    if (cond_start != cond_end) {
                        cond = eval_expr(ctx, 0);
                    }
                }
                
                lex->pos = body_end;
                return js_make_undefined();
            }
            
            default:
                break;
        }
    }
    
    /* Block statement */
    if (tok.type == TOK_PUNCT && tok.start[0] == '{') {
        lexer_advance(lex);
        scope_push(ctx);
        
        JsValue result = js_make_undefined();
        while (lexer_peek(lex).type != TOK_EOF &&
               !(lexer_peek(lex).type == TOK_PUNCT && lexer_peek(lex).start[0] == '}')) {
            result = eval_statement(ctx);
            /* Consume optional semicolon */
            if (lexer_peek(lex).type == TOK_PUNCT && lexer_peek(lex).start[0] == ';') {
                lexer_advance(lex);
            }
        }
        
        lexer_expect(lex, TOK_PUNCT, "}");
        scope_pop(ctx);
        return result;
    }
    
    /* Expression statement */
    JsValue val = eval_expr(ctx, 0);
    
    /* Consume optional semicolon */
    if (lexer_peek(lex).type == TOK_PUNCT && lexer_peek(lex).start[0] == ';') {
        lexer_advance(lex);
    }
    
    return val;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

JsValue js_eval(JsContext *ctx, const char *code, size_t len) {
    if (!js_load_code(ctx, code, len)) {
        set_error(ctx, "Failed to load code");
        return js_make_undefined();
    }
    
    JsValue result = js_make_undefined();
    while (ctx->lexer.current.type != TOK_EOF) {
        result = eval_statement(ctx);
        if (ctx->had_error) {
            break;
        }
    }
    
    return result;
}

JsValue js_interpret_expr(JsContext *ctx, const char *expr) {
    return js_eval(ctx, expr, 0);
}

JsValue js_call_function(JsContext *ctx, const char *func_name,
                          JsValue *args, size_t arg_count) {
    (void)args;
    (void)arg_count;
    
    /* Look up function in scope */
    JsString name = js_string_from_literal(func_name, strlen(func_name));
    JsVar *var = scope_find(ctx, name);
    
    if (!var) {
        /* Built-in: String.fromCharCode */
        if (strcmp(func_name, "fromCharCode") == 0 && arg_count > 0) {
            char *buf = (char *)js_arena_alloc(&ctx->arena, arg_count + 1);
            if (!buf) return js_make_undefined();
            
            for (size_t i = 0; i < arg_count; i++) {
                buf[i] = (char)js_to_number(args[i]);
            }
            buf[arg_count] = '\0';
            return js_make_string_nocopy(buf, arg_count);
        }
        
        /* Built-in: Math functions */
        if (strcmp(func_name, "pow") == 0 && arg_count == 2) {
            return js_make_number(pow(js_to_number(args[0]), js_to_number(args[1])));
        }
        
        return js_make_undefined();
    }
    
    if (!JS_IS_FUNCTION(var->value)) {
        return js_make_undefined();
    }
    
    JsFunction *func = JS_AS_FUNCTION(var->value);
    if (func->type == JS_FUNC_NATIVE) {
        return func->u.native.fn(ctx, NULL, args, arg_count);
    }
    
    /* Interpreted function */
    /* Save lexer state */
    JsLexer saved_lexer = ctx->lexer;
    
    /* Set up new lexer with function code */
    lexer_init(&ctx->lexer, func->u.js.code, func->u.js.code_len);
    
    /* Create new scope with parameters */
    scope_push(ctx);
    for (size_t i = 0; i < func->u.js.param_count && i < arg_count; i++) {
        scope_define(ctx, func->u.js.params[i], args[i]);
    }
    
    /* Execute function body */
    JsValue result = js_make_undefined();
    while (ctx->lexer.current.type != TOK_EOF) {
        result = eval_statement(ctx);
        if (ctx->had_error) break;
    }
    
    scope_pop(ctx);
    
    /* Restore lexer */
    ctx->lexer = saved_lexer;
    
    return result;
}

/* ============================================================================
 * Cipher/Decryption Functions (YouTube specific)
 * ============================================================================ */

/* Extract the signature decipher function from player JS */
static char *extract_sig_function(JsContext *ctx, const char *player_js,
                                   const char **func_names, size_t name_count,
                                   size_t *out_len) {
    (void)ctx;
    /* Simple pattern matching to find the function */
    for (size_t n = 0; n < name_count; n++) {
        const char *name = func_names[n];
        size_t name_len = strlen(name);
        
        const char *p = player_js;
        while ((p = strstr(p, name)) != NULL) {
            /* Check if it's a function definition */
            const char *after = p + name_len;
            while (*after == ' ' || *after == '\t') after++;
            
            if (*after == '=' || *after == ':' || 
                (*after == '(' && after[1] == 'a')) {
                /* Found it - extract the function */
                /* Find function start */
                const char *start = p;
                while (start > player_js && *(start-1) != ';' && 
                       *(start-1) != '}' && *(start-1) != '\n') {
                    start--;
                }
                
                /* Find function end (matching braces) */
                const char *end = after;
                while (*end && *end != '{') end++;
                if (*end == '{') {
                    int depth = 1;
                    end++;
                    while (*end && depth > 0) {
                        if (*end == '{') depth++;
                        else if (*end == '}') depth--;
                        end++;
                    }
                    
                    size_t len = (size_t)(end - start);
                    char *func = (char *)malloc(len + 1);
                    if (func) {
                        memcpy(func, start, len);
                        func[len] = '\0';
                        *out_len = len;
                        return func;
                    }
                }
            }
            p = after;
        }
    }
    
    *out_len = 0;
    return NULL;
}

/* Apply signature cipher operations */
static void apply_sig_op(char *sig, size_t sig_len, const char *op, JsValue arg) {
    if (strcmp(op, "slice") == 0 || strcmp(op, "splice") == 0) {
        int start = (int)js_to_number(arg);
        if (start < 0) start = (int)sig_len + start;
        if (start < 0) start = 0;
        if ((size_t)start >= sig_len) return;
        
        memmove(sig, sig + start, sig_len - start);
        sig[sig_len - start] = '\0';
    } else if (strcmp(op, "reverse") == 0) {
        for (size_t i = 0; i < sig_len / 2; i++) {
            char tmp = sig[i];
            sig[i] = sig[sig_len - 1 - i];
            sig[sig_len - 1 - i] = tmp;
        }
    }
}

char *js_cipher_decrypt_signature(JsContext *ctx, const char *sig,
                                   const char *player_js, size_t *out_len) {
    /* Common function names used for signature deciphering */
    const char *sig_func_names[] = {
        "sig", "signature", "decrypt", " decipher",
        "a", "aa", "ab", "b", "c", "d"
    };
    
    size_t func_len;
    char *func_code = extract_sig_function(ctx, player_js, sig_func_names, 
                                            sizeof(sig_func_names)/sizeof(char*),
                                            &func_len);
    
    if (!func_code) {
        *out_len = 0;
        return NULL;
    }
    
    /* Make a mutable copy of the signature */
    size_t sig_len = strlen(sig);
    char *result = (char *)malloc(sig_len + 1);
    if (!result) {
        free(func_code);
        *out_len = 0;
        return NULL;
    }
    memcpy(result, sig, sig_len + 1);
    
    /* Parse the decipher function to extract operations */
    /* This is a simplified version - real implementation would fully parse */
    const char *p = func_code;
    while (*p) {
        /* Look for common patterns like .slice(), .reverse(), .splice() */
        if (strncmp(p, ".slice(", 7) == 0) {
            p += 7;
            char *end;
            double arg = strtod(p, &end);
            if (end != p) {
                apply_sig_op(result, strlen(result), "slice", js_make_number(arg));
            }
            p = end;
        } else if (strncmp(p, ".reverse()", 10) == 0) {
            apply_sig_op(result, strlen(result), "reverse", js_make_undefined());
            p += 10;
        } else if (strncmp(p, ".splice(", 8) == 0) {
            p += 8;
            char *end;
            double arg = strtod(p, &end);
            if (end != p) {
                apply_sig_op(result, strlen(result), "splice", js_make_number(arg));
            }
            p = end;
        } else {
            p++;
        }
    }
    
    free(func_code);
    *out_len = strlen(result);
    return result;
}

char *js_cipher_get_nparam(JsContext *ctx, const char *n_param,
                            const char *player_js, size_t *out_len) {
    (void)ctx;
    (void)n_param;
    (void)player_js;
    
    /* Simplified n-param handling */
    /* In practice, this would extract and execute the n-parameter transform */
    size_t len = strlen(n_param);
    char *result = (char *)malloc(len + 1);
    if (result) {
        memcpy(result, n_param, len + 1);
        *out_len = len;
    }
    return result;
}

char *js_cipher_unsignaturize(JsContext *ctx, const char *sig_cipher,
                               const char *player_js, size_t *out_len) {
    /* Parse signature cipher and apply transformations */
    /* Expected format: url=...&s=...&sp=... */
    
    const char *s_start = strstr(sig_cipher, "&s=");
    if (!s_start) s_start = strstr(sig_cipher, "?s=");
    
    if (!s_start) {
        /* No signature - return as is */
        size_t len = strlen(sig_cipher);
        char *result = (char *)malloc(len + 1);
        if (result) {
            memcpy(result, sig_cipher, len + 1);
            *out_len = len;
        }
        return result;
    }
    
    s_start += 3;
    const char *s_end = strchr(s_start, '&');
    if (!s_end) s_end = s_start + strlen(s_start);
    
    size_t sig_len = (size_t)(s_end - s_start);
    char *sig = (char *)malloc(sig_len + 1);
    if (!sig) {
        *out_len = 0;
        return NULL;
    }
    memcpy(sig, s_start, sig_len);
    sig[sig_len] = '\0';
    
    /* URL decode the signature */
    char *decoded = (char *)malloc(sig_len + 1);
    size_t j = 0;
    for (size_t i = 0; i < sig_len; i++) {
        if (sig[i] == '%' && i + 2 < sig_len) {
            char hex[3] = {sig[i+1], sig[i+2], '\0'};
            decoded[j++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else {
            decoded[j++] = sig[i];
        }
    }
    decoded[j] = '\0';
    free(sig);
    
    /* Decrypt the signature */
    size_t decrypted_len;
    char *decrypted = js_cipher_decrypt_signature(ctx, decoded, player_js, &decrypted_len);
    free(decoded);
    
    if (!decrypted) {
        *out_len = 0;
        return NULL;
    }
    
    /* Reconstruct the URL with the decrypted signature */
    const char *url_start = strstr(sig_cipher, "url=");
    if (!url_start) {
        /* Just return the decrypted signature */
        *out_len = decrypted_len;
        return decrypted;
    }
    
    url_start += 4;
    const char *url_end = strchr(url_start, '&');
    if (!url_end) url_end = url_start + strlen(url_start);
    
    size_t url_len = (size_t)(url_end - url_start);
    
    /* Get signature param name */
    const char *sp_start = strstr(sig_cipher, "&sp=");
    const char *sp_name = "signature";
    if (sp_start) {
        sp_start += 4;
        const char *sp_end = strchr(sp_start, '&');
        if (!sp_end) sp_end = sp_start + strlen(sp_start);
        
        size_t sp_len = (size_t)(sp_end - sp_start);
        char *sp = (char *)malloc(sp_len + 1);
        if (sp) {
            memcpy(sp, sp_start, sp_len);
            sp[sp_len] = '\0';
            sp_name = sp;
        }
    }
    
    /* URL decode the URL */
    char *url = (char *)malloc(url_len + 1);
    if (!url) {
        free(decrypted);
        *out_len = 0;
        return NULL;
    }
    j = 0;
    for (size_t i = 0; i < url_len; i++) {
        if (url_start[i] == '%' && i + 2 < url_len) {
            char hex[3] = {url_start[i+1], url_start[i+2], '\0'};
            url[j++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else if (url_start[i] == '+') {
            url[j++] = ' ';
        } else {
            url[j++] = url_start[i];
        }
    }
    url[j] = '\0';
    
    /* Build final URL */
    size_t total_len = j + strlen(sp_name) + 1 + decrypted_len + 1;
    char *result = (char *)malloc(total_len);
    if (result) {
        snprintf(result, total_len, "%s&%s=%s", url, sp_name, decrypted);
        *out_len = strlen(result);
    }
    
    free(url);
    free(decrypted);
    if (sp_start && strcmp(sp_name, "signature") != 0) free((void*)sp_name);
    
    return result;
}
