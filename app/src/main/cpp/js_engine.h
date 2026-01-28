#ifndef JS_ENGINE_H
#define JS_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * JavaScript Engine for BGMDWLDR
 * Ported from yt-dlp's Python jsinterp.py
 * 
 * Game Dev Style C - Design Principles:
 * - Arena allocation for minimal fragmentation
 * - Data-oriented structures
 * - Stack-friendly where possible
 * - Clear state machines for parsing
 * ============================================================================ */

#define JS_MAX_CODE_SIZE        (256 * 1024)    /* 256KB max JS code */
#define JS_MAX_STACK_DEPTH      100             /* Max recursion depth */
#define JS_MAX_VARS             256             /* Max variables per scope */
#define JS_MAX_ARRAY_SIZE       1024            /* Max array elements */
#define JS_MAX_STRING_LEN       4096            /* Max string length */
#define JS_MAX_SCOPES           32              /* Max nested scopes */

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

typedef struct JsContext JsContext;
typedef struct JsValue JsValue;
typedef struct JsObject JsObject;
typedef struct JsArray JsArray;
typedef struct JsFunction JsFunction;

/* ============================================================================
 * Value Types
 * ============================================================================ */

typedef enum JsType {
    JS_TYPE_UNDEFINED = 0,
    JS_TYPE_NULL,
    JS_TYPE_BOOL,
    JS_TYPE_NUMBER,
    JS_TYPE_STRING,
    JS_TYPE_ARRAY,
    JS_TYPE_OBJECT,
    JS_TYPE_FUNCTION,
    JS_TYPE_REGEX,
} JsType;

/* String representation - points into code or arena */
typedef struct JsString {
    const char *data;
    size_t len;
    bool owned;     /* If true, needs freeing */
} JsString;

/* Function types */
typedef enum JsFuncType {
    JS_FUNC_NATIVE,           /* C function */
    JS_FUNC_INTERPRETED,      /* JavaScript code */
} JsFuncType;

/* Forward declarations for function pointer */
struct JsValue;

/* Native function callback */
typedef JsValue (*JsNativeFunc)(JsContext *ctx, JsValue *this_obj, 
                                 JsValue *args, size_t arg_count);

/* The Value - NaN tagging for efficiency (64-bit) */
struct JsValue {
    union {
        double num;
        struct {
            uint32_t tag;
            union {
                bool boolean;
                const char *str_ptr;
                struct JsArray *arr_ptr;
                struct JsObject *obj_ptr;
                struct JsFunction *func_ptr;
                void *ptr;
            } payload;
        } heap;
    } data;
    JsType type;
};

/* Array representation */
struct JsArray {
    JsValue *items;
    size_t count;
    size_t capacity;
};

/* Object property */
typedef struct JsProperty {
    JsString key;
    JsValue value;
    struct JsProperty *next;  /* For hash collision chaining */
} JsProperty;

/* Object representation - simple hash table */
struct JsObject {
    JsProperty *props;        /* Flat array of properties */
    size_t prop_count;
    size_t prop_capacity;
};

/* Function representation */
struct JsFunction {
    JsFuncType type;
    union {
        struct {
            const char *code;       /* Pointer to JS code */
            size_t code_len;
            JsString *params;
            size_t param_count;
        } js;
        struct {
            JsNativeFunc fn;
            const char *name;
        } native;
    } u;
    JsString name;
};

/* ============================================================================
 * Memory Arena
 * ============================================================================ */

typedef struct JsArena {
    uint8_t *base;
    size_t size;
    size_t used;
    size_t prev_used;  /* For rollback */
} JsArena;

/* ============================================================================
 * Variable Scope
 * ============================================================================ */

typedef struct JsVar {
    JsString name;
    JsValue value;
    bool is_local;
} JsVar;

typedef struct JsScope {
    JsVar vars[JS_MAX_VARS];
    size_t var_count;
    struct JsScope *parent;
} JsScope;

/* ============================================================================
 * Parser State
 * ============================================================================ */

typedef enum JsTokenType {
    TOK_EOF = 0,
    TOK_NUMBER,
    TOK_STRING,
    TOK_IDENTIFIER,
    TOK_KEYWORD,
    TOK_OPERATOR,
    TOK_PUNCT,
    TOK_REGEX,
} JsTokenType;

typedef struct JsToken {
    JsTokenType type;
    const char *start;
    size_t len;
    union {
        double num_val;
        int keyword;
        int op;
    } value;
} JsToken;

typedef struct JsLexer {
    const char *code;
    size_t code_len;
    size_t pos;
    JsToken current;
    JsToken peek;
    bool has_peek;
} JsLexer;

/* AST Node Types */
typedef enum JsAstType {
    AST_LITERAL,
    AST_IDENTIFIER,
    AST_BINARY_OP,
    AST_UNARY_OP,
    AST_ASSIGNMENT,
    AST_CALL,
    AST_MEMBER,
    AST_INDEX,
    AST_ARRAY,
    AST_OBJECT,
    AST_FUNCTION,
    AST_RETURN,
    AST_IF,
    AST_WHILE,
    AST_FOR,
    AST_BLOCK,
    AST_VAR_DECL,
    AST_TERNARY,
} JsAstType;

/* Forward declaration */
typedef struct JsAstNode JsAstNode;

/* AST Node - stored in arena */
struct JsAstNode {
    JsAstType type;
    JsValue value;           /* For literals */
    JsString name;           /* For identifiers */
    int op;                  /* Operator type */
    JsAstNode *left;         /* Left operand / condition */
    JsAstNode *right;        /* Right operand / then-branch */
    JsAstNode *extra;        /* Third operand / else-branch / body */
    JsAstNode **args;        /* Function arguments */
    size_t arg_count;
    JsAstNode **body;        /* Block statements */
    size_t body_count;
};

/* ============================================================================
 * Context
 * ============================================================================ */

typedef struct JsContext {
    /* Memory */
    JsArena arena;
    
    /* Scopes */
    JsScope scopes[JS_MAX_SCOPES];
    size_t scope_depth;
    
    /* Parser state */
    JsLexer lexer;
    
    /* Code being executed */
    const char *code;
    size_t code_len;
    
    /* Global objects */
    JsObject *global;
    JsObject *prototypes;
    
    /* Error handling */
    char error[512];
    bool had_error;
    
    /* Recursion tracking */
    int recursion_depth;
    int max_recursion;
} JsContext;

/* ============================================================================
 * Built-in Objects
 * ============================================================================ */

typedef struct JsBuiltins {
    JsValue string_proto;
    JsValue array_proto;
    JsValue math_obj;
    JsValue date_obj;
    JsValue regexp_proto;
} JsBuiltins;

/* ============================================================================
 * API Functions
 * ============================================================================ */

/* Context management */
bool js_init(JsContext *ctx, void *arena_buffer, size_t arena_size);
void js_reset(JsContext *ctx);
bool js_load_code(JsContext *ctx, const char *code, size_t len);

/* Execution */
JsValue js_eval(JsContext *ctx, const char *code, size_t len);
JsValue js_call_function(JsContext *ctx, const char *func_name, 
                          JsValue *args, size_t arg_count);
JsValue js_interpret_expr(JsContext *ctx, const char *expr);

/* Value creation */
JsValue js_make_undefined(void);
JsValue js_make_null(void);
JsValue js_make_bool(bool b);
JsValue js_make_number(double n);
JsValue js_make_string(JsContext *ctx, const char *str, size_t len);
JsValue js_make_string_nocopy(const char *str, size_t len);
JsValue js_make_array(JsContext *ctx, size_t capacity);
JsValue js_make_object(JsContext *ctx);

/* Value inspection */
bool js_is_truthy(JsValue val);
bool js_is_equal(JsValue a, JsValue b);
JsString js_to_string(JsContext *ctx, JsValue val);
double js_to_number(JsValue val);

/* String operations */
JsString js_string_from_literal(const char *str, size_t len);
bool js_string_equal(JsString a, JsString b);
JsString js_string_concat(JsContext *ctx, JsString a, JsString b);

/* Cipher operations (YouTube specific) */
char *js_cipher_unsignaturize(JsContext *ctx, const char *sig_cipher,
                               const char *player_js, size_t *out_len);
char *js_cipher_decrypt_signature(JsContext *ctx, const char *sig,
                                   const char *player_js, size_t *out_len);
char *js_cipher_get_nparam(JsContext *ctx, const char *n_param,
                            const char *player_js, size_t *out_len);

/* Arena allocation (internal) */
void *js_arena_alloc(JsArena *arena, size_t size);

/* Scope management (internal) */
void scope_push(JsContext *ctx);
void scope_pop(JsContext *ctx);
JsVar *scope_define(JsContext *ctx, JsString name, JsValue value);
JsVar *scope_find(JsContext *ctx, JsString name);

/* Error handling */
const char *js_get_error(JsContext *ctx);
void js_clear_error(JsContext *ctx);

/* ============================================================================
 * Constants
 * ============================================================================ */

/* Keywords - start at 100 to avoid conflicts with operators */
enum {
    KW_VAR = 100,
    KW_LET,
    KW_CONST,
    KW_FUNCTION,
    KW_RETURN,
    KW_IF,
    KW_ELSE,
    KW_WHILE,
    KW_FOR,
    KW_BREAK,
    KW_CONTINUE,
    KW_SWITCH,
    KW_CASE,
    KW_DEFAULT,
    KW_TRY,
    KW_CATCH,
    KW_FINALLY,
    KW_THROW,
    KW_NEW,
    KW_THIS,
    KW_TRUE,
    KW_FALSE,
    KW_NULL,
    KW_UNDEFINED,
    KW_VOID,
    KW_TYPEOF,
    KW_INSTANCEOF,
    KW_IN,
    KW_OF,
};

/* Operators */
enum {
    OP_NONE = 0,
    /* Assignment */
    OP_ASSIGN,
    OP_ADD_ASSIGN, OP_SUB_ASSIGN, OP_MUL_ASSIGN, OP_DIV_ASSIGN, OP_MOD_ASSIGN,
    /* Arithmetic */
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_POW,
    /* Bitwise */
    OP_BIT_AND, OP_BIT_OR, OP_BIT_XOR, OP_BIT_NOT,
    OP_SHL, OP_SHR, OP_USHR,
    /* Logical */
    OP_AND, OP_OR, OP_NOT,
    /* Comparison */
    OP_EQ, OP_NE, OP_STRICT_EQ, OP_STRICT_NE,
    OP_LT, OP_GT, OP_LE, OP_GE,
    /* Increment/Decrement */
    OP_PRE_INC, OP_PRE_DEC, OP_POST_INC, OP_POST_DEC,
    /* Member */
    OP_DOT, OP_NULLISH_COALESCE,
    /* Ternary */
    OP_TERNARY,
    /* Comma */
    OP_COMMA,
};

/* ============================================================================
 * Helper Macros (Game Dev Style)
 * ============================================================================ */

#define JS_ARENA_ALLOC(ctx, type) ((type*)js_arena_alloc(&(ctx)->arena, sizeof(type)))
#define JS_ARENA_ALLOC_N(ctx, type, n) ((type*)js_arena_alloc(&(ctx)->arena, sizeof(type) * (n)))
#define JS_ARENA_SAVE(ctx) ((ctx)->arena.used)
#define JS_ARENA_RESTORE(ctx, mark) ((ctx)->arena.used = (mark))

#define JS_IS_UNDEFINED(v) ((v).type == JS_TYPE_UNDEFINED)
#define JS_IS_NULL(v) ((v).type == JS_TYPE_NULL)
#define JS_IS_NUMBER(v) ((v).type == JS_TYPE_NUMBER)
#define JS_IS_STRING(v) ((v).type == JS_TYPE_STRING)
#define JS_IS_ARRAY(v) ((v).type == JS_TYPE_ARRAY)
#define JS_IS_OBJECT(v) ((v).type == JS_TYPE_OBJECT)
#define JS_IS_BOOL(v) ((v).type == JS_TYPE_BOOL)
#define JS_IS_FUNCTION(v) ((v).type == JS_TYPE_FUNCTION)

#define JS_AS_NUMBER(v) ((v).data.num)
#define JS_AS_BOOL(v) ((v).data.heap.payload.boolean)
#define JS_AS_STRING(v) ((v).data.heap.payload.str_ptr)
#define JS_AS_ARRAY(v) ((v).data.heap.payload.arr_ptr)
#define JS_AS_OBJECT(v) ((v).data.heap.payload.obj_ptr)
#define JS_AS_FUNCTION(v) ((v).data.heap.payload.func_ptr)

#ifdef __cplusplus
}
#endif

#endif /* JS_ENGINE_H */
