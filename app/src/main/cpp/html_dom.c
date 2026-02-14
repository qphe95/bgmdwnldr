/*
 * HTML DOM Parser - Implementation
 * 
 * A lightweight HTML parser that creates proper DOM nodes in the QuickJS engine.
 * This allows JavaScript running in the engine to interact with the parsed HTML
 * structure through standard DOM APIs.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <android/log.h>
#include "html_dom.h"
#include "js_value_helpers.h"

#define LOG_TAG "html_dom"
#define LOG_INFO(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOG_ERROR(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOG_WARN(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

/* List of self-closing HTML tags (void elements) */
static const char *SELF_CLOSING_TAGS[] = {
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "param", "source", "track", "wbr", NULL
};

/* List of tags that contain raw text content */
static const char *RAW_CONTENT_TAGS[] = {
    "script", "style", "textarea", "title", NULL
};

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

bool html_is_self_closing_tag(const char *tag_name) {
    if (!tag_name) return false;
    for (int i = 0; SELF_CLOSING_TAGS[i]; i++) {
        if (strcasecmp(tag_name, SELF_CLOSING_TAGS[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool html_is_raw_content_tag(const char *tag_name) {
    if (!tag_name) return false;
    for (int i = 0; RAW_CONTENT_TAGS[i]; i++) {
        if (strcasecmp(tag_name, RAW_CONTENT_TAGS[i]) == 0) {
            return true;
        }
    }
    return false;
}

void html_tag_name_normalize(char *tag_name) {
    if (!tag_name) return;
    for (char *p = tag_name; *p; p++) {
        *p = (char)tolower((unsigned char)*p);
    }
}

/* Decode common HTML entities */
int html_decode_entities(char *str, size_t len) {
    if (!str || len == 0) return 0;
    
    char *dest = str;
    const char *src = str;
    const char *end = str + len;
    
    while (*src && src < end) {
        if (*src == '&') {
            if (strncmp(src, "&lt;", 4) == 0) {
                *dest++ = '<';
                src += 4;
            } else if (strncmp(src, "&gt;", 4) == 0) {
                *dest++ = '>';
                src += 4;
            } else if (strncmp(src, "&amp;", 5) == 0) {
                *dest++ = '&';
                src += 5;
            } else if (strncmp(src, "&quot;", 6) == 0) {
                *dest++ = '"';
                src += 6;
            } else if (strncmp(src, "&apos;", 6) == 0) {
                *dest++ = '\'';
                src += 6;
            } else if (strncmp(src, "&nbsp;", 6) == 0) {
                *dest++ = ' ';
                src += 6;
            } else if (src[1] == '#' && isdigit((unsigned char)src[2])) {
                /* Numeric entity: &#123; */
                src += 2;
                int val = 0;
                while (isdigit((unsigned char)*src)) {
                    val = val * 10 + (*src - '0');
                    src++;
                }
                if (*src == ';') src++;
                *dest++ = (char)(val > 0 && val < 256 ? val : '?');
            } else {
                *dest++ = *src++;
            }
        } else {
            *dest++ = *src++;
        }
    }
    *dest = '\0';
    return (int)(dest - str);
}

/* ============================================================================
 * Memory Management
 * ============================================================================ */

static HtmlNode* html_node_create(HtmlNodeType type) {
    HtmlNode *node = calloc(1, sizeof(HtmlNode));
    if (!node) return NULL;
    
    node->type = type;
    node->js_object = JS_UNDEFINED;
    node->has_js_object = 0;
    
    return node;
}

static HtmlAttribute* html_attribute_create(const char *name, const char *value) {
    HtmlAttribute *attr = calloc(1, sizeof(HtmlAttribute));
    if (!attr) return NULL;
    
    strncpy(attr->name, name, HTML_MAX_ATTR_NAME_LEN - 1);
    attr->name[HTML_MAX_ATTR_NAME_LEN - 1] = '\0';
    
    if (value) {
        strncpy(attr->value, value, HTML_MAX_ATTR_VALUE_LEN - 1);
        attr->value[HTML_MAX_ATTR_VALUE_LEN - 1] = '\0';
        html_decode_entities(attr->value, strlen(attr->value));
    }
    
    return attr;
}

static void html_node_free(HtmlNode *node) {
    if (!node) return;
    
    /* Free attributes */
    HtmlAttribute *attr = node->attributes;
    while (attr) {
        HtmlAttribute *next = attr->next;
        free(attr);
        attr = next;
    }
    
    /* Free text content */
    if (node->text_content) {
        free(node->text_content);
    }
    
    /* Note: We don't free js_object here - that's managed by QuickJS GC */
    
    free(node);
}

void html_document_free(HtmlDocument *doc) {
    if (!doc) return;
    
    /* Free all nodes recursively starting from root */
    HtmlNode *current = doc->root;
    while (current) {
        HtmlNode *next = current->next_sibling;
        
        /* Free children recursively */
        HtmlNode *child = current->first_child;
        while (child) {
            HtmlNode *child_next = child->next_sibling;
            
            /* Recursively free grandchildren */
            if (child->first_child) {
                /* Use a simple stack-based approach for grandchildren */
                HtmlNode *stack[HTML_MAX_NESTING_DEPTH];
                int stack_top = 0;
                
                HtmlNode *gc = child->first_child;
                while (gc || stack_top > 0) {
                    if (gc) {
                        if (stack_top < HTML_MAX_NESTING_DEPTH) {
                            stack[stack_top++] = gc;
                        }
                        gc = gc->first_child;
                    } else {
                        gc = stack[--stack_top];
                        HtmlNode *gc_next = gc->next_sibling;
                        html_node_free(gc);
                        gc = gc_next;
                    }
                }
            }
            
            html_node_free(child);
            child = child_next;
        }
        
        html_node_free(current);
        current = next;
    }
    
    if (doc->title) free(doc->title);
    free(doc);
}

/* ============================================================================
 * Parser Implementation
 * ============================================================================ */

static void parser_skip_whitespace(HtmlParser *p) {
    while (p->pos < p->html_len && isspace((unsigned char)p->html[p->pos])) {
        if (p->html[p->pos] == '\n') {
            p->line++;
            p->column = 1;
        } else {
            p->column++;
        }
        p->pos++;
    }
}

static bool parser_match(HtmlParser *p, const char *str) {
    size_t len = strlen(str);
    if (p->pos + len > p->html_len) return false;
    return (strncasecmp(p->html + p->pos, str, len) == 0);
}

static char* parser_read_tag_name(HtmlParser *p) {
    static char tag_name[HTML_MAX_TAG_NAME_LEN];
    size_t i = 0;
    
    /* Skip initial whitespace */
    parser_skip_whitespace(p);
    
    /* Read tag name */
    while (p->pos < p->html_len && i < HTML_MAX_TAG_NAME_LEN - 1) {
        char c = p->html[p->pos];
        if (c == '>' || c == '/' || c == ' ' || c == '\t' || 
            c == '\n' || c == '\r' || c == '\f') {
            break;
        }
        tag_name[i++] = c;
        p->pos++;
        p->column++;
    }
    
    tag_name[i] = '\0';
    html_tag_name_normalize(tag_name);
    
    return i > 0 ? tag_name : NULL;
}

static HtmlAttribute* parser_read_attribute(HtmlParser *p) {
    parser_skip_whitespace(p);
    
    /* Check for end of tag */
    if (p->pos >= p->html_len || p->html[p->pos] == '>' || 
        (p->html[p->pos] == '/' && p->pos + 1 < p->html_len && p->html[p->pos + 1] == '>')) {
        return NULL;
    }
    
    /* Read attribute name */
    char name[HTML_MAX_ATTR_NAME_LEN];
    size_t name_len = 0;
    
    while (p->pos < p->html_len && name_len < HTML_MAX_ATTR_NAME_LEN - 1) {
        char c = p->html[p->pos];
        if (c == '=' || c == '>' || c == '/' || isspace((unsigned char)c)) {
            break;
        }
        name[name_len++] = c;
        p->pos++;
        p->column++;
    }
    name[name_len] = '\0';
    
    if (name_len == 0) return NULL;
    
    /* Check for value */
    char value[HTML_MAX_ATTR_VALUE_LEN] = {0};
    
    parser_skip_whitespace(p);
    
    if (p->pos < p->html_len && p->html[p->pos] == '=') {
        p->pos++; /* skip = */
        p->column++;
        
        parser_skip_whitespace(p);
        
        /* Read value */
        if (p->pos < p->html_len) {
            char quote = p->html[p->pos];
            if (quote == '"' || quote == '\'') {
                p->pos++; /* skip opening quote */
                p->column++;
                
                size_t val_len = 0;
                while (p->pos < p->html_len && p->html[p->pos] != quote && 
                       val_len < HTML_MAX_ATTR_VALUE_LEN - 1) {
                    value[val_len++] = p->html[p->pos++];
                    p->column++;
                }
                value[val_len] = '\0';
                
                if (p->pos < p->html_len && p->html[p->pos] == quote) {
                    p->pos++; /* skip closing quote */
                    p->column++;
                }
            } else {
                /* Unquoted value */
                size_t val_len = 0;
                while (p->pos < p->html_len && val_len < HTML_MAX_ATTR_VALUE_LEN - 1) {
                    char c = p->html[p->pos];
                    if (c == '>' || c == '/' || isspace((unsigned char)c)) {
                        break;
                    }
                    value[val_len++] = c;
                    p->pos++;
                    p->column++;
                }
                value[val_len] = '\0';
            }
        }
    }
    
    return html_attribute_create(name, value[0] ? value : NULL);
}

static HtmlNode* parser_parse_element(HtmlParser *p);
static HtmlNode* parser_parse_text(HtmlParser *p);

static void parser_skip_comment(HtmlParser *p) {
    if (parser_match(p, "<!--")) {
        p->pos += 4;
        while (p->pos < p->html_len) {
            if (parser_match(p, "-->")) {
                p->pos += 3;
                break;
            }
            p->pos++;
        }
    }
}

static void parser_skip_doctype(HtmlParser *p) {
    if (parser_match(p, "<!doctype") || parser_match(p, "<!DOCTYPE")) {
        p->pos += 9;
        while (p->pos < p->html_len && p->html[p->pos] != '>') {
            p->pos++;
        }
        if (p->pos < p->html_len) p->pos++; /* skip > */
    }
}

static char* parser_read_raw_content(HtmlParser *p, const char *end_tag) {
    size_t start = p->pos;
    size_t end_tag_len = strlen(end_tag);
    
    while (p->pos < p->html_len) {
        if (p->html[p->pos] == '<') {
            /* Check for end tag */
            if (p->pos + 1 < p->html_len && p->html[p->pos + 1] == '/') {
                if (strncasecmp(p->html + p->pos + 2, end_tag, end_tag_len) == 0) {
                    break;
                }
            }
        }
        p->pos++;
    }
    
    size_t len = p->pos - start;
    char *content = malloc(len + 1);
    if (content) {
        memcpy(content, p->html + start, len);
        content[len] = '\0';
    }
    
    return content;
}

static HtmlNode* parser_parse_element(HtmlParser *p) {
    if (p->pos >= p->html_len || p->html[p->pos] != '<') {
        return NULL;
    }
    
    p->pos++; /* skip < */
    
    /* Check for closing tag */
    if (p->pos < p->html_len && p->html[p->pos] == '/') {
        /* Closing tag - let caller handle */
        p->pos--; /* back up */
        return NULL;
    }
    
    /* Read tag name */
    char *tag_name = parser_read_tag_name(p);
    if (!tag_name) {
        /* Skip malformed tag */
        while (p->pos < p->html_len && p->html[p->pos] != '>') p->pos++;
        if (p->pos < p->html_len) p->pos++;
        return NULL;
    }
    
    /* Create element node */
    HtmlNode *node = html_node_create(HTML_NODE_ELEMENT);
    if (!node) return NULL;
    
    strncpy(node->tag_name, tag_name, HTML_MAX_TAG_NAME_LEN - 1);
    node->tag_name[HTML_MAX_TAG_NAME_LEN - 1] = '\0';
    
    /* Parse attributes */
    HtmlAttribute **attr_tail = &node->attributes;
    while (1) {
        parser_skip_whitespace(p);
        
        /* Check for self-closing or end of tag */
        if (p->pos >= p->html_len) break;
        
        if (p->html[p->pos] == '>') {
            p->pos++; /* skip > */
            break;
        }
        
        if (p->html[p->pos] == '/' && p->pos + 1 < p->html_len && p->html[p->pos + 1] == '>') {
            p->pos += 2; /* skip /> */
            node->type = HTML_NODE_ELEMENT; /* Self-closing */
            return node;
        }
        
        /* Parse attribute */
        HtmlAttribute *attr = parser_read_attribute(p);
        if (!attr) {
            /* Skip unknown character */
            if (p->pos < p->html_len && p->html[p->pos] != '>' && 
                !(p->html[p->pos] == '/' && p->pos + 1 < p->html_len && p->html[p->pos + 1] == '>')) {
                p->pos++;
            } else {
                break;
            }
            continue;
        }
        
        *attr_tail = attr;
        attr_tail = &attr->next;
        
        if (node->attributes && strcmp(attr->name, "id") == 0) {
            /* Store ID for quick access */
        }
    }
    
    /* Handle raw content tags (script, style, etc.) */
    if (html_is_raw_content_tag(node->tag_name)) {
        char end_tag[HTML_MAX_TAG_NAME_LEN + 4];
        snprintf(end_tag, sizeof(end_tag), "</%s>", node->tag_name);
        
        char *content = parser_read_raw_content(p, node->tag_name);
        if (content) {
            node->text_content = content;
            node->text_len = strlen(content);
        }
        
        /* Skip the closing tag */
        size_t end_tag_len = strlen(end_tag);
        if (p->pos + end_tag_len <= p->html_len) {
            if (strncasecmp(p->html + p->pos, end_tag, end_tag_len) == 0) {
                p->pos += end_tag_len;
            }
        }
        
        return node;
    }
    
    /* Parse children for non-self-closing tags */
    if (!html_is_self_closing_tag(node->tag_name)) {
        p->nesting_depth++;
        
        while (p->pos < p->html_len && p->nesting_depth < HTML_MAX_NESTING_DEPTH) {
            parser_skip_whitespace(p);
            
            if (p->pos >= p->html_len) break;
            
            /* Check for closing tag */
            if (p->html[p->pos] == '<' && p->pos + 1 < p->html_len && p->html[p->pos + 1] == '/') {
                /* Check if it's our closing tag */
                size_t check_pos = p->pos + 2;
                size_t tag_len = strlen(node->tag_name);
                
                if (p->html_len - check_pos >= tag_len &&
                    strncasecmp(p->html + check_pos, node->tag_name, tag_len) == 0) {
                    /* It's our closing tag - skip it */
                    p->pos = check_pos + tag_len;
                    while (p->pos < p->html_len && p->html[p->pos] != '>') p->pos++;
                    if (p->pos < p->html_len) p->pos++;
                    break;
                }
            }
            
            /* Parse child element or text */
            if (p->html[p->pos] == '<') {
                HtmlNode *child = parser_parse_element(p);
                if (child) {
                    child->parent = node;
                    if (node->last_child) {
                        node->last_child->next_sibling = child;
                        child->prev_sibling = node->last_child;
                    } else {
                        node->first_child = child;
                    }
                    node->last_child = child;
                } else {
                    /* Try to recover from parsing error */
                    p->pos++;
                }
            } else {
                HtmlNode *text = parser_parse_text(p);
                if (text) {
                    text->parent = node;
                    if (node->last_child) {
                        node->last_child->next_sibling = text;
                        text->prev_sibling = node->last_child;
                    } else {
                        node->first_child = text;
                    }
                    node->last_child = text;
                }
            }
        }
        
        p->nesting_depth--;
    }
    
    return node;
}

static HtmlNode* parser_parse_text(HtmlParser *p) {
    if (p->pos >= p->html_len) return NULL;
    
    size_t start = p->pos;
    
    while (p->pos < p->html_len && p->html[p->pos] != '<') {
        p->pos++;
    }
    
    size_t len = p->pos - start;
    if (len == 0) return NULL;
    
    /* Trim trailing whitespace but preserve leading for preformatted text */
    while (len > 0 && isspace((unsigned char)p->html[start + len - 1])) {
        len--;
    }
    
    if (len == 0) return NULL;
    
    HtmlNode *node = html_node_create(HTML_NODE_TEXT);
    if (!node) return NULL;
    
    node->text_content = malloc(len + 1);
    if (!node->text_content) {
        free(node);
        return NULL;
    }
    
    memcpy(node->text_content, p->html + start, len);
    node->text_content[len] = '\0';
    node->text_len = len;
    
    /* Decode entities */
    html_decode_entities(node->text_content, len);
    
    return node;
}

HtmlDocument* html_parse(const char *html, size_t html_len) {
    if (!html || html_len == 0) return NULL;
    
    HtmlDocument *doc = calloc(1, sizeof(HtmlDocument));
    if (!doc) return NULL;
    
    HtmlParser parser = {
        .html = html,
        .html_len = html_len,
        .pos = 0,
        .line = 1,
        .column = 1,
        .document = doc,
        .nesting_depth = 0
    };
    
    /* Skip DOCTYPE if present */
    parser_skip_doctype(&parser);
    
    /* Skip any leading whitespace/comments */
    while (parser.pos < parser.html_len) {
        parser_skip_whitespace(&parser);
        if (parser_match(&parser, "<!--")) {
            parser_skip_comment(&parser);
        } else {
            break;
        }
    }
    
    /* Parse root elements */
    HtmlNode **root_tail = &doc->root;
    
    while (parser.pos < parser.html_len) {
        parser_skip_whitespace(&parser);
        
        if (parser.pos >= parser.html_len) break;
        
        if (parser_match(&parser, "<!--")) {
            parser_skip_comment(&parser);
            continue;
        }
        
        if (parser.html[parser.pos] == '<') {
            HtmlNode *node = parser_parse_element(&parser);
            if (node) {
                *root_tail = node;
                root_tail = &node->next_sibling;
                
                /* Track head and body */
                if (strcasecmp(node->tag_name, "head") == 0) {
                    doc->head = node;
                } else if (strcasecmp(node->tag_name, "body") == 0) {
                    doc->body = node;
                } else if (strcasecmp(node->tag_name, "html") == 0) {
                    /* Look for head and body in html element children */
                    HtmlNode *child = node->first_child;
                    while (child) {
                        if (strcasecmp(child->tag_name, "head") == 0) {
                            doc->head = child;
                        } else if (strcasecmp(child->tag_name, "body") == 0) {
                            doc->body = child;
                        }
                        child = child->next_sibling;
                    }
                }
            } else {
                /* Try to recover */
                if (parser.pos < parser.html_len && parser.html[parser.pos] == '<') {
                    parser.pos++;
                }
            }
        } else {
            /* Text node at root level - usually whitespace, skip it */
            HtmlNode *text = parser_parse_text(&parser);
            if (text) {
                html_node_free(text);
            }
        }
    }
    
    LOG_INFO("Parsed HTML document: found %s, %s",
             doc->head ? "<head>" : "no <head>",
             doc->body ? "<body>" : "no <body>");
    
    return doc;
}

/* ============================================================================
 * DOM Node Creation in QuickJS
 * ============================================================================ */

/* Forward declaration from js_quickjs.c */
extern JSClassID js_video_class_id;
extern JSValue js_video_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv);

/* Create a JavaScript element for a given tag */
JSValue html_create_element_js(JSContext *ctx, const char *tag_name, HtmlAttribute *attrs) {
    if (!tag_name || !ctx) return JS_NULL;
    
    JSValue element;
    
    /* Special handling for video elements */
    if (strcasecmp(tag_name, "video") == 0) {
        extern JSValue js_video_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv);
        element = js_video_constructor(ctx, JS_NULL, 0, NULL);
    } else {
        /* Create generic element */
        element = JS_NewObject(ctx);
    }
    
    if (JS_IsException(element)) {
        return JS_NULL;
    }
    
    /* Set tagName property */
    JS_SetPropertyStr(ctx, element, "tagName", JS_NewString(ctx, tag_name));
    
    /* Create attributes map */
    JSValue attr_map = JS_NewObject(ctx);
    
    /* Set id and className if present */
    HtmlAttribute *attr = attrs;
    while (attr) {
        if (strcasecmp(attr->name, "id") == 0) {
            JS_SetPropertyStr(ctx, element, "id", JS_NewString(ctx, attr->value));
        } else if (strcasecmp(attr->name, "class") == 0) {
            JS_SetPropertyStr(ctx, element, "className", JS_NewString(ctx, attr->value));
        }
        
        /* Add to attributes map */
        JS_SetPropertyStr(ctx, attr_map, attr->name, JS_NewString(ctx, attr->value));
        
        attr = attr->next;
    }
    
    JS_SetPropertyStr(ctx, element, "attributes", attr_map);
    
    /* Set innerHTML to empty initially */
    JS_SetPropertyStr(ctx, element, "innerHTML", JS_NewString(ctx, ""));
    
    /* Add childNodes array */
    JS_SetPropertyStr(ctx, element, "childNodes", JS_NewArray(ctx));
    
    return element;
}

/* Recursively create DOM nodes in JS */
// BUG FIX #6: Using shadow stack to track JSValues instead of manual JS_FreeValue
static bool html_node_create_js_recursive(JSContext *ctx, HtmlNode *node, JSValue parent) {
    if (!ctx || !node) return false;
    
    JSValue js_node = JS_UNDEFINED;
    
    switch (node->type) {
        case HTML_NODE_ELEMENT: {
            js_node = html_create_element_js(ctx, node->tag_name, node->attributes);
            
            /* Process children */
            if (!JS_IsNull(js_node)) {
                HtmlNode *child = node->first_child;
                while (child) {
                    html_node_create_js_recursive(ctx, child, js_node);
                    child = child->next_sibling;
                }
                
                /* If we have a parent, append this element */
                if (!JS_IsUndefined(parent) && !JS_IsNull(parent)) {
                    /* Use scope for temporary JSValues */
                    JS_SCOPE_BEGIN(ctx)
                    {
                        JS_SCOPE_VALUE(ctx, appendChild, JS_GetPropertyStr(ctx, parent, "appendChild"));
                        
                        if (!JS_IsUndefined(appendChild) && !JS_IsNull(appendChild)) {
                            JSValue args[1] = { js_node };
                            JS_SCOPE_VALUE(ctx, result, JS_Call(ctx, appendChild, parent, 1, args));
                            (void)result;
                        }
                    }
                    JS_SCOPE_END(ctx);
                }
            }
            break;
        }
        
        case HTML_NODE_TEXT: {
            if (node->text_content && strlen(node->text_content) > 0) {
                /* Create text node (as a simple string for now) */
                js_node = JS_NewString(ctx, node->text_content);
                
                /* Add to parent's innerHTML or childNodes if needed */
                if (!JS_IsUndefined(parent) && !JS_IsNull(parent)) {
                    JS_SCOPE_BEGIN(ctx)
                    {
                        JS_SCOPE_VALUE(ctx, childNodes, JS_GetPropertyStr(ctx, parent, "childNodes"));
                        
                        if (JS_IsArray(ctx, childNodes)) {
                            JS_SCOPE_VALUE(ctx, push, JS_GetPropertyStr(ctx, childNodes, "push"));
                            JSValue args[1] = { js_node };
                            JS_SCOPE_VALUE(ctx, result, JS_Call(ctx, push, childNodes, 1, args));
                            (void)result;
                        }
                    }
                    JS_SCOPE_END(ctx);
                }
            }
            break;
        }
        
        default:
            break;
    }
    
    if (!JS_IsUndefined(js_node)) {
        node->js_object = js_node;
        node->has_js_object = 1;
    }
    
    return true;
}

/* Create JavaScript document object with parsed HTML structure */
JSValue html_create_js_document(JSContext *ctx, HtmlDocument *doc) {
    if (!ctx || !doc) return JS_NULL;
    
    JSValue js_doc = JS_NewObject(ctx);
    
    /* Set document properties */
    JS_SetPropertyStr(ctx, js_doc, "nodeType", JS_NewInt32(ctx, 9)); /* DOCUMENT_NODE */
    JS_SetPropertyStr(ctx, js_doc, "readyState", JS_NewString(ctx, "complete"));
    JS_SetPropertyStr(ctx, js_doc, "characterSet", JS_NewString(ctx, "UTF-8"));
    JS_SetPropertyStr(ctx, js_doc, "contentType", JS_NewString(ctx, "text/html"));
    
    /* Create documentElement (html or first root element) */
    JSValue doc_element = JS_NULL;
    if (doc->root) {
        doc_element = html_create_element_js(ctx, doc->root->tag_name, doc->root->attributes);
        
        /* Process children of root */
        HtmlNode *child = doc->root->first_child;
        while (child) {
            html_node_create_js_recursive(ctx, child, doc_element);
            child = child->next_sibling;
        }
    } else {
        /* Create a minimal html element */
        doc_element = html_create_element_js(ctx, "html", NULL);
    }
    
    JS_SetPropertyStr(ctx, js_doc, "documentElement", doc_element);
    
    /* Create body element reference */
    JSValue body_element = JS_NULL;
    if (doc->body) {
        body_element = html_create_element_js(ctx, "body", doc->body->attributes);
        
        /* Process body children */
        HtmlNode *child = doc->body->first_child;
        while (child) {
            html_node_create_js_recursive(ctx, child, body_element);
            child = child->next_sibling;
        }
    } else {
        body_element = html_create_element_js(ctx, "body", NULL);
    }
    
    JS_SetPropertyStr(ctx, js_doc, "body", body_element);
    JS_SetPropertyStr(ctx, doc_element, "body", body_element);
    
    /* Create head element reference */
    JSValue head_element = JS_NULL;
    if (doc->head) {
        head_element = html_create_element_js(ctx, "head", doc->head->attributes);
        
        /* Process head children */
        HtmlNode *child = doc->head->first_child;
        while (child) {
            html_node_create_js_recursive(ctx, child, head_element);
            child = child->next_sibling;
        }
    } else {
        head_element = html_create_element_js(ctx, "head", NULL);
    }
    
    JS_SetPropertyStr(ctx, js_doc, "head", head_element);
    JS_SetPropertyStr(ctx, doc_element, "head", head_element);
    
    /* Add document methods */
    /* Note: createElement is provided by js_quickjs.c */
    
    return js_doc;
}

/* Main entry point: parse HTML and create DOM in JS context */
// BUG FIX #7: Using shadow stack to track JSValues instead of manual JS_FreeValue
bool html_create_dom_in_js(JSContext *ctx, HtmlDocument *doc) {
    if (!ctx || !doc) return false;
    
    LOG_INFO("Creating DOM in JS context");
    
    /* Create the document object in JS first (not tracked - persistent) */
    JSValue js_doc = html_create_js_document(ctx, doc);
    
    if (JS_IsNull(js_doc) || JS_IsException(js_doc)) {
        LOG_ERROR("Failed to create JS document");
        return false;
    }
    
    /* Use scope for all temporary JSValues */
    JS_SCOPE_BEGIN(ctx)
    {
        /* Get global object and set document */
        JS_SCOPE_VALUE(ctx, global, JS_GetGlobalObject(ctx));
        JS_SetPropertyStr(ctx, global, "document", js_doc);
        
        /* Also set documentElement on window */
        JS_SCOPE_VALUE(ctx, doc_elem, JS_GetPropertyStr(ctx, js_doc, "documentElement"));
        if (!JS_IsNull(doc_elem) && !JS_IsUndefined(doc_elem)) {
            JS_SetPropertyStr(ctx, global, "documentElement", doc_elem);
        }
    }
    JS_SCOPE_END(ctx);
    
    LOG_INFO("DOM created successfully in JS context");
    return true;
}

/* Helper to get element by tag name from document */
HtmlNode* html_document_get_element_by_tag(HtmlDocument *doc, const char *tag_name) {
    if (!doc || !tag_name) return NULL;
    
    /* Check head */
    if (doc->head && strcasecmp(doc->head->tag_name, tag_name) == 0) {
        return doc->head;
    }
    
    /* Check body */
    if (doc->body && strcasecmp(doc->body->tag_name, tag_name) == 0) {
        return doc->body;
    }
    
    /* Search in root */
    HtmlNode *root = doc->root;
    if (root && strcasecmp(root->tag_name, tag_name) == 0) {
        return root;
    }
    
    /* Search recursively in children */
    /* Simple breadth-first search */
    HtmlNode *queue[HTML_MAX_NESTING_DEPTH];
    int head = 0, tail = 0;
    
    if (root) queue[tail++] = root;
    
    while (head < tail) {
        HtmlNode *current = queue[head++];
        
        if (strcasecmp(current->tag_name, tag_name) == 0) {
            return current;
        }
        
        HtmlNode *child = current->first_child;
        while (child && tail < HTML_MAX_NESTING_DEPTH) {
            queue[tail++] = child;
            child = child->next_sibling;
        }
    }
    
    return NULL;
}

/* Helper to get all elements by tag name */
int html_document_get_elements_by_tag(HtmlDocument *doc, const char *tag_name,
                                       HtmlNode **out_nodes, int max_nodes) {
    if (!doc || !tag_name || !out_nodes || max_nodes <= 0) return 0;
    
    int count = 0;
    
    /* Search recursively using a stack */
    HtmlNode *stack[HTML_MAX_NESTING_DEPTH];
    int stack_top = 0;
    
    if (doc->root) stack[stack_top++] = doc->root;
    
    while (stack_top > 0 && count < max_nodes) {
        HtmlNode *current = stack[--stack_top];
        
        if (strcasecmp(current->tag_name, tag_name) == 0) {
            out_nodes[count++] = current;
        }
        
        /* Add children to stack (in reverse order for correct order) */
        HtmlNode *child = current->first_child;
        HtmlNode *children[HTML_MAX_NESTING_DEPTH];
        int child_count = 0;
        
        while (child && child_count < HTML_MAX_NESTING_DEPTH) {
            children[child_count++] = child;
            child = child->next_sibling;
        }
        
        for (int i = child_count - 1; i >= 0 && stack_top < HTML_MAX_NESTING_DEPTH; i--) {
            stack[stack_top++] = children[i];
        }
    }
    
    return count;
}
