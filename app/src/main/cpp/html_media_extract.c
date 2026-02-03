#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <pthread.h>
#include <android/log.h>
#include <stdint.h>
#include "cJSON.h"
#include "html_media_extract.h"
#include "http_download.h"
#include "js_quickjs.h"

// HTML Entity decoding helper - converts HTML entities to actual characters
// Handles: &lt; &gt; &amp; &quot; &apos; &#123; (decimal) &#x7B; (hex)
static int decode_html_entity(const char *input, char *output, size_t output_len) {
    if (!input || !output || output_len == 0) return 0;
    
    const char *p = input;
    char *out = output;
    size_t remaining = output_len - 1;  // Reserve space for null terminator
    
    while (*p && remaining > 0) {
        if (*p == '&') {
            const char *end = strchr(p, ';');
            if (end && end - p < 20) {  // Reasonable entity length
                size_t entity_len = end - p - 1;  // Length without '&' and ';'
                const char *entity = p + 1;
                char decoded = 0;
                int valid_entity = 0;
                
                // Named entities
                if (strncmp(entity, "lt", entity_len) == 0 && entity_len == 2) {
                    decoded = '<';
                    valid_entity = 1;
                } else if (strncmp(entity, "gt", entity_len) == 0 && entity_len == 2) {
                    decoded = '>';
                    valid_entity = 1;
                } else if (strncmp(entity, "amp", entity_len) == 0 && entity_len == 3) {
                    decoded = '&';
                    valid_entity = 1;
                } else if (strncmp(entity, "quot", entity_len) == 0 && entity_len == 4) {
                    decoded = '"';
                    valid_entity = 1;
                } else if (strncmp(entity, "apos", entity_len) == 0 && entity_len == 4) {
                    decoded = '\'';
                    valid_entity = 1;
                } else if (strncmp(entity, "nbsp", entity_len) == 0 && entity_len == 4) {
                    decoded = ' ';
                    valid_entity = 1;
                }
                // Numeric entities: &#123; (decimal)
                else if (*entity == '#' && entity_len > 1) {
                    const char *num_start = entity + 1;
                    if (*num_start == 'x' || *num_start == 'X') {
                        // Hex entity: &#x3b; or &#x7B;
                        long val = strtol(num_start + 1, NULL, 16);
                        if (val > 0 && val <= 0xFF) {
                            decoded = (char)val;
                            valid_entity = 1;
                        }
                    } else {
                        // Decimal entity: &#59;
                        long val = strtol(num_start, NULL, 10);
                        if (val > 0 && val <= 0xFF) {
                            decoded = (char)val;
                            valid_entity = 1;
                        }
                    }
                }
                
                if (valid_entity) {
                    *out++ = decoded;
                    remaining--;
                    p = end + 1;  // Skip past the entity
                    continue;
                }
            }
        }
        
        // Not an entity or entity too long, copy as-is
        *out++ = *p++;
        remaining--;
    }
    
    *out = '\0';
    return (int)(out - output);
}

// Decode hex-escaped content (\x3b -> ;)
// Handles \xNN format escape sequences commonly found in YouTube's JSON
static int decode_hex_escapes(const char *input, char *output, size_t output_len) {
    if (!input || !output || output_len == 0) return 0;
    
    const char *p = input;
    char *out = output;
    size_t remaining = output_len - 1;
    
    while (*p && remaining > 0) {
        // Check for \xNN pattern (hex escape sequence)
        if (*p == '\\' && *(p + 1) == 'x' && 
            isxdigit((unsigned char)*(p + 2)) && 
            isxdigit((unsigned char)*(p + 3))) {
            // Decode hex value
            int val1 = tolower((unsigned char)*(p + 2));
            int val2 = tolower((unsigned char)*(p + 3));
            int hex_val = ((val1 >= 'a' ? val1 - 'a' + 10 : val1 - '0') << 4) |
                          (val2 >= 'a' ? val2 - 'a' + 10 : val2 - '0');
            
            // Accept any valid byte value (0x00-0xFF) that's not null
            // This includes all printable ASCII, common symbols like = ; & %, etc.
            if (hex_val != 0) {
                *out++ = (char)hex_val;
                remaining--;
                p += 4;  // Skip entire \xNN sequence
                continue;
            }
        }
        
        // Regular character copy
        *out++ = *p++;
        remaining--;
    }
    
    *out = '\0';
    return (int)(out - output);
}

// Full HTML unescape - combines entity and hex decoding
static char* html_unescape(const char *input, size_t input_len) {
    if (!input || input_len == 0) return NULL;
    
    // Allocate output buffer (same size as input, will be smaller or equal)
    char *output = malloc(input_len + 1);
    if (!output) return NULL;
    
    // First pass: decode HTML entities
    char *temp = malloc(input_len + 1);
    if (!temp) {
        free(output);
        return NULL;
    }
    
    decode_html_entity(input, temp, input_len + 1);
    
    // Second pass: decode hex escapes
    decode_hex_escapes(temp, output, input_len + 1);
    
    free(temp);
    return output;
}

// UTF-8 validation and repair
// Fixes common UTF-8 encoding issues like truncated sequences or invalid bytes
static char* repair_utf8(const char *input, size_t input_len) {
    if (!input || input_len == 0) return NULL;
    
    char *output = malloc(input_len + 1);
    if (!output) return NULL;
    
    const uint8_t *p = (const uint8_t *)input;
    char *out = output;
    size_t remaining = input_len;
    
    while (remaining > 0) {
        uint8_t c = *p;
        
        // Single-byte ASCII (0x00-0x7F)
        if ((c & 0x80) == 0) {
            *out++ = c;
            p++;
            remaining--;
        }
        // Two-byte sequence (0xC2-0xDF, 0x80-0xBF)
        else if ((c & 0xE0) == 0xC0) {
            if (remaining >= 2 && (p[1] & 0xC0) == 0x80) {
                // Valid 2-byte sequence
                *out++ = c;
                *out++ = p[1];
                p += 2;
                remaining -= 2;
            } else {
                // Truncated or invalid, skip
                p++;
                remaining--;
            }
        }
        // Three-byte sequence (0xE0-0xEF, 0x80-0xBF, 0x80-0xBF)
        else if ((c & 0xF0) == 0xE0) {
            if (remaining >= 3 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
                // Valid 3-byte sequence
                *out++ = c;
                *out++ = p[1];
                *out++ = p[2];
                p += 3;
                remaining -= 3;
            } else {
                // Truncated or invalid, skip
                p++;
                remaining--;
            }
        }
        // Four-byte sequence (0xF0-0xF4, 0x80-0xBF, 0x80-0xBF, 0x80-0xBF)
        else if ((c & 0xF8) == 0xF0) {
            if (remaining >= 4 && (p[1] & 0xC0) == 0x80 && 
                (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
                // Valid 4-byte sequence
                *out++ = c;
                *out++ = p[1];
                *out++ = p[2];
                *out++ = p[3];
                p += 4;
                remaining -= 4;
            } else {
                // Truncated or invalid, skip
                p++;
                remaining--;
            }
        }
        // Invalid byte (continuation byte without start, or invalid start byte)
        else {
            // Skip invalid byte
            p++;
            remaining--;
        }
    }
    
    *out = '\0';
    return output;
}

// Clean and decode extracted JSON content
// Handles all three issues: HTML entities, hex escapes, and UTF-8 issues
static char* clean_json_content(const char *input, size_t input_len) {
    if (!input || input_len == 0) return NULL;
    
    // Step 1: Decode HTML entities and hex escapes
    char *decoded = html_unescape(input, input_len);
    if (!decoded) return NULL;
    
    // Step 2: Repair UTF-8 sequences
    size_t decoded_len = strlen(decoded);
    char *repaired = repair_utf8(decoded, decoded_len);
    free(decoded);
    
    return repaired;
}

#define LOG_TAG "html_extract"
#define LOG_INFO(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOG_ERROR(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOG_WARN(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

#define MAX_SCRIPT_URLS 32
#define SCRIPT_URL_MAX_LEN 512
#define MAX_SCRIPTS 64  // Total scripts (external + inline)
#define MAX_HTML_SIZE (20 * 1024 * 1024)  // 20MB max for large YouTube pages with big JSON payloads

// Script types
typedef enum {
    SCRIPT_TYPE_EXTERNAL,
    SCRIPT_TYPE_INLINE
} ScriptType;

// Script info with parse order tracking
typedef struct {
    int parse_order;           // Order in which script appears in HTML (0 = first)
    ScriptType type;           // External or inline
    char url[SCRIPT_URL_MAX_LEN];  // For external scripts: URL to fetch
    char *content;             // For inline scripts: content; for external: fetched content
    size_t content_len;        // Length of content
} ScriptInfo;

// Media stream structure
typedef struct MediaStream {
    char url[2048];
    char mime_type[128];
    char quality[32];
    int width;
    int height;
    int itag;
    bool has_cipher;  // True if URL needs signature decryption
} MediaStream;

typedef struct {
    const char *html;
    size_t html_len;
    MediaStream *streams;
    int max_streams;
    int stream_count;
} ExtractContext;

static char *url_normalize(const char *base, const char *rel, char *out, size_t out_len) {
    if (!rel || !out || out_len == 0) return NULL;
    
    // Already absolute
    if (strncmp(rel, "http://", 7) == 0 || strncmp(rel, "https://", 8) == 0) {
        strncpy(out, rel, out_len - 1);
        out[out_len - 1] = '\0';
        return out;
    }
    
    // Protocol-relative
    if (strncmp(rel, "//", 2) == 0) {
        snprintf(out, out_len, "https:%s", rel);
        return out;
    }
    
    // Relative to base
    if (base) {
        const char *base_end = base;
        const char *last_slash = strrchr(base, '/');
        if (last_slash) {
            size_t base_len = last_slash - base + 1;
            snprintf(out, out_len, "%.*s%s", (int)base_len, base, rel);
        } else {
            snprintf(out, out_len, "%s/%s", base, rel);
        }
        return out;
    }
    
    strncpy(out, rel, out_len - 1);
    out[out_len - 1] = '\0';
    return out;
}

// URL decode helper
static char *url_decode(const char *src) {
    if (!src) return NULL;
    
    size_t len = strlen(src);
    char *decoded = malloc(len + 1);
    if (!decoded) return NULL;
    
    char *dst = decoded;
    for (size_t i = 0; i < len; i++) {
        if (src[i] == '%' && i + 2 < len) {
            int hex1 = tolower(src[i + 1]);
            int hex2 = tolower(src[i + 2]);
            int val1 = (hex1 >= 'a') ? (hex1 - 'a' + 10) : (hex1 - '0');
            int val2 = (hex2 >= 'a') ? (hex2 - 'a' + 10) : (hex2 - '0');
            if (val1 >= 0 && val1 < 16 && val2 >= 0 && val2 < 16) {
                *dst++ = (char)((val1 << 4) | val2);
                i += 2;
                continue;
            }
        }
        *dst++ = src[i];
    }
    *dst = '\0';
    return decoded;
}

// Parse signatureCipher and extract URL + signature
static bool parse_signature_cipher(const char *cipher_text, char *out_url, size_t out_url_len, 
                                    char *out_sig, size_t out_sig_len) {
    if (!cipher_text || !out_url) return false;
    
    out_url[0] = '\0';
    if (out_sig) out_sig[0] = '\0';
    
    // signatureCipher format: url=XXX&sig=YYY or url=XXX&s=YYY
    const char *url_start = strstr(cipher_text, "url=");
    if (!url_start) return false;
    
    url_start += 4;
    const char *url_end = strchr(url_start, '&');
    
    char *decoded_url;
    if (url_end) {
        char encoded[1024];
        size_t len = url_end - url_start;
        if (len >= sizeof(encoded)) len = sizeof(encoded) - 1;
        strncpy(encoded, url_start, len);
        encoded[len] = '\0';
        decoded_url = url_decode(encoded);
    } else {
        char encoded[1024];
        strncpy(encoded, url_start, sizeof(encoded) - 1);
        encoded[sizeof(encoded) - 1] = '\0';
        decoded_url = url_decode(encoded);
    }
    
    if (!decoded_url) return false;
    
    strncpy(out_url, decoded_url, out_url_len - 1);
    out_url[out_url_len - 1] = '\0';
    free(decoded_url);
    
    // Extract signature if present
    if (out_sig) {
        const char *sig_start = strstr(cipher_text, "sig=");
        if (!sig_start) sig_start = strstr(cipher_text, "s=");
        if (sig_start) {
            sig_start = strchr(sig_start, '=');
            if (sig_start) {
                sig_start++;
                const char *sig_end = strchr(sig_start, '&');
                if (sig_end) {
                    size_t len = sig_end - sig_start;
                    if (len >= out_sig_len) len = out_sig_len - 1;
                    strncpy(out_sig, sig_start, len);
                    out_sig[len] = '\0';
                } else {
                    strncpy(out_sig, sig_start, out_sig_len - 1);
                    out_sig[out_sig_len - 1] = '\0';
                }
            }
        }
    }
    
    return out_url[0] != '\0';
}

// Parse a single format object using cJSON
static void parse_format_object(cJSON *format, MediaStream *stream) {
    if (!format || !stream) return;
    
    memset(stream, 0, sizeof(MediaStream));
    
    // Get itag
    cJSON *itag = cJSON_GetObjectItemCaseSensitive(format, "itag");
    if (cJSON_IsNumber(itag)) {
        stream->itag = itag->valueint;
    }
    
    // Get mimeType
    cJSON *mime = cJSON_GetObjectItemCaseSensitive(format, "mimeType");
    if (cJSON_IsString(mime)) {
        strncpy(stream->mime_type, mime->valuestring, sizeof(stream->mime_type) - 1);
    }
    
    // Get width/height
    cJSON *width = cJSON_GetObjectItemCaseSensitive(format, "width");
    if (cJSON_IsNumber(width)) {
        stream->width = width->valueint;
    }
    
    cJSON *height = cJSON_GetObjectItemCaseSensitive(format, "height");
    if (cJSON_IsNumber(height)) {
        stream->height = height->valueint;
    }
    
    // Get qualityLabel
    cJSON *quality = cJSON_GetObjectItemCaseSensitive(format, "qualityLabel");
    if (cJSON_IsString(quality)) {
        strncpy(stream->quality, quality->valuestring, sizeof(stream->quality) - 1);
    }
    
    // Try to get URL directly
    cJSON *url = cJSON_GetObjectItemCaseSensitive(format, "url");
    if (cJSON_IsString(url)) {
        strncpy(stream->url, url->valuestring, sizeof(stream->url) - 1);
        stream->has_cipher = false;
    } else {
        // Try signatureCipher
        cJSON *cipher = cJSON_GetObjectItemCaseSensitive(format, "signatureCipher");
        if (!cipher) {
            cipher = cJSON_GetObjectItemCaseSensitive(format, "cipher");
        }
        if (cJSON_IsString(cipher)) {
            char decoded_url[2048];
            char sig[512];
            if (parse_signature_cipher(cipher->valuestring, decoded_url, sizeof(decoded_url), 
                                        sig, sizeof(sig))) {
                strncpy(stream->url, decoded_url, sizeof(stream->url) - 1);
                stream->has_cipher = true;
                LOG_INFO("Parsed cipher for itag=%d, has sig=%s", stream->itag, sig[0] ? "yes" : "no");
            }
        }
    }
    
    LOG_INFO("Parsed stream: itag=%d, url=%.50s%s, mime=%s, has_cipher=%d", 
             stream->itag, 
             stream->url[0] ? stream->url : "(empty)",
             strlen(stream->url) > 50 ? "..." : "",
             stream->mime_type[0] ? stream->mime_type : "(empty)",
             stream->has_cipher);
}

// Parse ytInitialPlayerResponse using cJSON
static int parse_yt_player_response(const char *json, MediaStream *streams, int max_streams) {
    if (!json || !streams || max_streams <= 0) return 0;
    
    LOG_INFO("Parsing player response with cJSON (%zu bytes)", strlen(json));
    
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr) {
            size_t error_pos = error_ptr - json;
            LOG_ERROR("cJSON parse error at position %zu: %.50s", error_pos, error_ptr);
            // Log context around error
            if (error_pos > 30) {
                LOG_ERROR("Context before error: ...%.30s", error_ptr - 30);
            }
            // Also log the character code at error position
            LOG_ERROR("Character at error: 0x%02X (decimal %d)", (unsigned char)*error_ptr, (unsigned char)*error_ptr);
        } else {
            LOG_ERROR("cJSON parse error (unknown position)");
        }
        return 0;
    }
    
    // Navigate to streamingData
    cJSON *streaming_data = cJSON_GetObjectItemCaseSensitive(root, "streamingData");
    if (!streaming_data) {
        LOG_WARN("No streamingData found in player response");
        cJSON_Delete(root);
        return 0;
    }
    
    int count = 0;
    
    // Parse formats array
    cJSON *formats = cJSON_GetObjectItemCaseSensitive(streaming_data, "formats");
    if (cJSON_IsArray(formats)) {
        int format_count = cJSON_GetArraySize(formats);
        LOG_INFO("Found %d formats", format_count);
        
        for (int i = 0; i < format_count && count < max_streams; i++) {
            cJSON *format = cJSON_GetArrayItem(formats, i);
            if (format) {
                parse_format_object(format, &streams[count]);
                if (streams[count].itag > 0) {
                    count++;
                }
            }
        }
    }
    
    // Parse adaptiveFormats array
    cJSON *adaptive_formats = cJSON_GetObjectItemCaseSensitive(streaming_data, "adaptiveFormats");
    if (cJSON_IsArray(adaptive_formats)) {
        int adaptive_count = cJSON_GetArraySize(adaptive_formats);
        LOG_INFO("Found %d adaptive formats", adaptive_count);
        
        for (int i = 0; i < adaptive_count && count < max_streams; i++) {
            cJSON *format = cJSON_GetArrayItem(adaptive_formats, i);
            if (format) {
                parse_format_object(format, &streams[count]);
                if (streams[count].itag > 0) {
                    count++;
                }
            }
        }
    }
    
    cJSON_Delete(root);
    
    LOG_INFO("Parsed %d total streams", count);
    return count;
}

// Find the true end of a script tag, handling strings and comments
// This prevents premature termination when </script> appears inside JS strings
static const char* find_script_end(const char *content_start) {
    if (!content_start) return NULL;
    
    const char *p = content_start;
    bool in_string = false;
    char string_char = 0;
    bool escape = false;
    int comment_state = 0;  // 0=none, 1=maybe single-line, 2=single-line, 3=maybe multi, 4=multi
    
    while (*p) {
        // Handle comments
        if (!in_string) {
            if (comment_state == 0) {
                if (*p == '/') {
                    comment_state = 1;  // Maybe starting comment
                }
            } else if (comment_state == 1) {
                if (*p == '/') {
                    comment_state = 2;  // Single-line comment started
                } else if (*p == '*') {
                    comment_state = 4;  // Multi-line comment started
                } else {
                    comment_state = 0;  // Not a comment
                }
            } else if (comment_state == 2) {
                if (*p == '\n') {
                    comment_state = 0;  // End single-line comment
                }
            } else if (comment_state == 4) {
                if (*p == '*') {
                    comment_state = 3;  // Maybe ending multi-line
                }
            } else if (comment_state == 3) {
                if (*p == '/') {
                    comment_state = 0;  // End multi-line comment
                } else if (*p != '*') {
                    comment_state = 4;  // Still in multi-line comment
                }
            }
            
            // Check for </script> when not in string and not in comment
            if (comment_state == 0 && *p == '<') {
                if (strncasecmp(p, "</script>", 9) == 0) {
                    return p;  // Found actual script end
                }
            }
        }
        
        // Handle strings
        if (comment_state == 0) {
            if (escape) {
                escape = false;
            } else if (*p == '\\') {
                escape = true;
            } else if (!in_string) {
                if (*p == '"' || *p == '\'' || *p == '`') {
                    in_string = true;
                    string_char = *p;
                }
            } else {
                if (*p == string_char) {
                    in_string = false;
                    string_char = 0;
                }
            }
        }
        
        p++;
    }
    
    return NULL;  // No closing tag found
}

// Extract inline scripts from HTML (scripts without src attribute)
// These contain initialization code like ytcfg, ytInitialData, etc.
// NOTE: This function is kept for backward compatibility but not used by the new parse-order system.
// Use extract_scripts_in_order() instead for proper script execution order.
static int extract_inline_scripts(const char *html, char **out_scripts, int max_scripts) {
    if (!html || !out_scripts || max_scripts <= 0) return 0;
    
    int count = 0;
    const char *p = html;
    
    while ((p = strstr(p, "<script")) != NULL && count < max_scripts) {
        // Find end of opening tag properly (handling quotes)
        const char *tag_start = p;
        const char *tag_end = tag_start + 7; // Skip "<script"
        bool in_quote = false;
        char quote_char = 0;
        
        while (*tag_end) {
            if (!in_quote) {
                if (*tag_end == '"' || *tag_end == '\'') {
                    in_quote = true;
                    quote_char = *tag_end;
                } else if (*tag_end == '>') {
                    break;
                }
            } else {
                if (*tag_end == quote_char) {
                    in_quote = false;
                }
            }
            tag_end++;
        }
        
        if (*tag_end != '>') break; // No closing bracket found
        
        // Check for src= in the tag (must be outside quotes)
        bool has_src = false;
        const char *check = tag_start;
        in_quote = false;
        quote_char = 0;
        
        while (check < tag_end) {
            if (!in_quote) {
                if (*check == '"' || *check == '\'') {
                    in_quote = true;
                    quote_char = *check;
                } else if (strncmp(check, "src=", 4) == 0) {
                    has_src = true;
                    break;
                }
            } else {
                if (*check == quote_char) {
                    in_quote = false;
                }
            }
            check++;
        }
        
        if (has_src) {
            // This is an external script, skip
            p = tag_end + 1;
            continue;
        }
        
        // Check for type="text/javascript" or no type (default)
        bool is_js = true;
        const char *type_attr = tag_start;
        while ((type_attr = strstr(type_attr, "type=")) != NULL && type_attr < tag_end) {
            // Check if this type= is inside quotes
            bool type_in_quote = false;
            char type_quote_char = 0;
            for (const char *c = tag_start; c < type_attr; c++) {
                if (!type_in_quote) {
                    if (*c == '"' || *c == '\'') {
                        type_in_quote = true;
                        type_quote_char = *c;
                    }
                } else {
                    if (*c == type_quote_char) {
                        type_in_quote = false;
                    }
                }
            }
            
            if (!type_in_quote) {
                // Check if it's JavaScript type
                const char *type_val = type_attr + 5;
                while (*type_val && isspace((unsigned char)*type_val)) type_val++;
                char quote = *type_val;
                if (quote == '"' || quote == '\'') {
                    type_val++;
                    if (strncmp(type_val, "text/javascript", 15) != 0 &&
                        strncmp(type_val, "application/javascript", 22) != 0 &&
                        strncmp(type_val, "module", 6) != 0) {
                        // Not JavaScript, skip
                        is_js = false;
                        break;
                    }
                }
            }
            type_attr++;
        }
        
        if (!is_js) {
            p = tag_end + 1;
            continue;
        }
        
        // Find the closing </script> tag using robust parser (handles strings)
        const char *content_start = tag_end + 1;
        const char *script_end = find_script_end(content_start);
        
        if (!script_end) {
            LOG_WARN("No closing </script> tag found");
            break;
        }
        
        size_t content_len = script_end - content_start;
        
        // Skip empty scripts or very short ones
        if (content_len < 50) {
            p = script_end + 9;
            continue;
        }
        
        // Skip scripts that are likely not initialization code
        bool skip = false;
        const char *first_nonspace = content_start;
        while (*first_nonspace && isspace((unsigned char)*first_nonspace)) first_nonspace++;
        if (*first_nonspace == '{' || *first_nonspace == '[') {
            skip = true; // Might be JSON, skip
        }
        
        if (skip) {
            p = script_end + 9;
            continue;
        }
        
        // Extract the script content with proper handling for large scripts
        char *script = malloc(content_len + 1);
        if (script) {
            memcpy(script, content_start, content_len);
            script[content_len] = '\0';
            
            // Only keep script if it has meaningful content
            if (content_len > 50) {
                out_scripts[count] = script;
                LOG_INFO("Extracted inline script %d: %zu bytes", count, content_len);
                count++;
            } else {
                free(script);
            }
        } else {
            LOG_ERROR("Failed to allocate %zu bytes for script", content_len + 1);
        }
        
        p = script_end + 9;
    }
    
    LOG_INFO("Extracted %d inline initialization scripts", count);
    return count;
}

// Free script info array
static void free_script_infos(ScriptInfo *scripts, int count) {
    for (int i = 0; i < count; i++) {
        if (scripts[i].content) {
            free(scripts[i].content);
            scripts[i].content = NULL;
        }
    }
}

// Comparison function for qsort - sort by parse_order
static int compare_script_info(const void *a, const void *b) {
    const ScriptInfo *sa = (const ScriptInfo *)a;
    const ScriptInfo *sb = (const ScriptInfo *)b;
    return sa->parse_order - sb->parse_order;
}

// Extract all scripts (both external and inline) in parse order
// Returns number of scripts found, fills the scripts array
static int extract_scripts_in_order(const char *html, ScriptInfo *scripts, int max_scripts) {
    if (!html || !scripts || max_scripts <= 0) return 0;
    
    int count = 0;
    int parse_order = 0;
    const char *p = html;
    
    while ((p = strstr(p, "<script")) != NULL && count < max_scripts) {
        const char *tag_start = p;
        const char *tag_end = tag_start + 7; // Skip "<script"
        
        // Find end of opening tag properly (handling quotes)
        bool in_quote = false;
        char quote_char = 0;
        while (*tag_end) {
            if (!in_quote) {
                if (*tag_end == '"' || *tag_end == '\'') {
                    in_quote = true;
                    quote_char = *tag_end;
                } else if (*tag_end == '>') {
                    break;
                }
            } else {
                if (*tag_end == quote_char) {
                    in_quote = false;
                }
            }
            tag_end++;
        }
        
        if (*tag_end != '>') break; // No closing bracket found
        
        // Check for type attribute - must be JavaScript or module
        bool is_js = true;
        bool has_src = false;
        const char *src_start = NULL;
        size_t src_len = 0;
        
        // Parse attributes within the tag
        const char *attr = tag_start + 7;  // After "<script"
        while (attr < tag_end) {
            // Skip whitespace
            while (attr < tag_end && isspace((unsigned char)*attr)) attr++;
            if (attr >= tag_end) break;
            
            // Check for src attribute
            if (strncasecmp(attr, "src=", 4) == 0) {
                has_src = true;
                attr += 4;
                while (attr < tag_end && isspace((unsigned char)*attr)) attr++;
                if (attr < tag_end) {
                    char quote = *attr;
                    if (quote == '"' || quote == '\'') {
                        attr++;  // Skip quote
                        src_start = attr;
                        const char *end = strchr(attr, quote);
                        if (end && end < tag_end) {
                            src_len = end - attr;
                            attr = end + 1;
                        }
                    } else {
                        // Unquoted src
                        src_start = attr;
                        while (attr < tag_end && !isspace((unsigned char)*attr)) attr++;
                        src_len = attr - src_start;
                    }
                }
                continue;
            }
            
            // Check for type attribute
            if (strncasecmp(attr, "type=", 5) == 0) {
                attr += 5;
                while (attr < tag_end && isspace((unsigned char)*attr)) attr++;
                if (attr < tag_end) {
                    char quote = *attr;
                    if (quote == '"' || quote == '\'') {
                        attr++;
                        const char *type_val = attr;
                        const char *end = strchr(attr, quote);
                        if (end && end < tag_end) {
                            size_t type_len = end - type_val;
                            // Check if it's a valid JS type
                            if (type_len > 0 &&
                                strncasecmp(type_val, "text/javascript", 15) != 0 &&
                                strncasecmp(type_val, "application/javascript", 22) != 0 &&
                                strncasecmp(type_val, "module", 6) != 0) {
                                is_js = false;
                            }
                            attr = end + 1;
                        }
                    }
                }
                continue;
            }
            
            // Skip to next attribute
            attr++;
        }
        
        if (!is_js) {
            p = tag_end + 1;
            continue;
        }
        
        if (has_src && src_start && src_len > 0 && src_len < SCRIPT_URL_MAX_LEN) {
            // External script
            strncpy(scripts[count].url, src_start, src_len);
            scripts[count].url[src_len] = '\0';
            
            // Convert relative to absolute URL
            if (strncmp(scripts[count].url, "//", 2) == 0) {
                char temp[SCRIPT_URL_MAX_LEN];
                snprintf(temp, sizeof(temp), "https:%s", scripts[count].url);
                strcpy(scripts[count].url, temp);
            } else if (scripts[count].url[0] == '/') {
                char temp[SCRIPT_URL_MAX_LEN];
                snprintf(temp, sizeof(temp), "https://www.youtube.com%s", scripts[count].url);
                strcpy(scripts[count].url, temp);
            } else if (strncmp(scripts[count].url, "http", 4) != 0) {
                // Skip non-HTTP URLs
                p = tag_end + 1;
                continue;
            }
            
            scripts[count].parse_order = parse_order++;
            scripts[count].type = SCRIPT_TYPE_EXTERNAL;
            scripts[count].content = NULL;
            scripts[count].content_len = 0;
            
            LOG_INFO("Found external script [%d]: %.80s...", 
                     scripts[count].parse_order, scripts[count].url);
            count++;
            p = tag_end + 1;
            
        } else {
            // Inline script - find the closing </script> tag
            const char *content_start = tag_end + 1;
            
            // Use robust script end finder that handles strings with </script>
            const char *script_end = find_script_end(content_start);
            
            if (!script_end) {
                LOG_WARN("No closing </script> tag found");
                break;
            }
            
            size_t content_len = script_end - content_start;
            
            // Skip empty scripts or very short ones
            if (content_len < 50) {
                p = script_end + 9;
                continue;
            }
            
            // Warn about very large scripts but still process them
            if (content_len > 500000) {
                LOG_INFO("Found large inline script: %zu bytes (may be data payload)", content_len);
            }
            
            // Extract the script content with proper size handling for large payloads
            char *script_content = NULL;
            
            // For very large scripts, verify we can allocate the memory
            if (content_len > 1000000) {
                // Try to allocate, if it fails, skip this script
                script_content = malloc(content_len + 1);
                if (!script_content) {
                    LOG_ERROR("Failed to allocate %zu bytes for script content", content_len + 1);
                    p = script_end + 9;
                    continue;
                }
            } else {
                script_content = malloc(content_len + 1);
                if (!script_content) {
                    LOG_ERROR("Failed to allocate %zu bytes for script content", content_len + 1);
                    p = script_end + 9;
                    continue;
                }
            }
            
            memcpy(script_content, content_start, content_len);
            script_content[content_len] = '\0';
            
            scripts[count].url[0] = '\0';
            scripts[count].parse_order = parse_order++;
            scripts[count].type = SCRIPT_TYPE_INLINE;
            scripts[count].content = script_content;
            scripts[count].content_len = content_len;
            
            LOG_INFO("Found inline script [%d]: %zu bytes", 
                     scripts[count].parse_order, content_len);
            count++;
            
            p = script_end + 9;
        }
    }
    
    LOG_INFO("Extracted %d scripts in parse order", count);
    return count;
}

// Extract YouTube video ID from URL
static bool extract_yt_video_id(const char *url, char *out_id, size_t out_len) {
    if (!url || !out_id || out_len == 0) return false;
    
    const char *patterns[] = {
        "?v=",
        "&v=",
        "/v/",
        "/embed/",
        ".be/",
        NULL
    };
    
    for (int i = 0; patterns[i]; i++) {
        const char *p = strstr(url, patterns[i]);
        if (p) {
            p += strlen(patterns[i]);
            size_t len = 0;
            while (p[len] && (isalnum((unsigned char)p[len]) || p[len] == '_' || p[len] == '-')) {
                len++;
            }
            if (len >= 11 && len < out_len) {
                strncpy(out_id, p, len);
                out_id[len] = '\0';
                return true;
            }
        }
    }
    
    return false;
}

// Execute player scripts and extract player response from JS context
// Returns malloc'd player response JSON, or NULL on failure
static char* execute_scripts_and_get_player_response(const char *html) {
    if (!html) return NULL;
    
    LOG_INFO("Executing player scripts to extract data...");
    
    // Extract all scripts in parse order
    ScriptInfo scripts[MAX_SCRIPTS];
    memset(scripts, 0, sizeof(scripts));
    int script_count = extract_scripts_in_order(html, scripts, MAX_SCRIPTS);
    
    if (script_count == 0) {
        LOG_ERROR("No scripts found in HTML");
        return NULL;
    }
    
    LOG_INFO("Found %d scripts to execute", script_count);
    
    // Fetch external scripts
    for (int i = 0; i < script_count; i++) {
        if (scripts[i].type == SCRIPT_TYPE_EXTERNAL) {
            HttpBuffer buffer;
            memset(&buffer, 0, sizeof(HttpBuffer));
            
            char error[256];
            LOG_INFO("Fetching external script [%d]: %.80s", 
                     scripts[i].parse_order, scripts[i].url);
            
            bool result = http_get_to_memory(scripts[i].url, &buffer, error, sizeof(error));
            if (result && buffer.data && buffer.size > 0) {
                // Validate it's actually JavaScript, not HTML
                const char *content = buffer.data;
                while (*content && (isspace((unsigned char)*content) || 
                       (unsigned char)*content == 0xEF || 
                       (unsigned char)*content == 0xBB || 
                       (unsigned char)*content == 0xBF)) {
                    content++;
                }
                
                bool is_html = (strncasecmp(content, "<!doctype", 9) == 0 ||
                               strncasecmp(content, "<html", 5) == 0 ||
                               strncasecmp(content, "<?xml", 5) == 0);
                
                if (is_html) {
                    LOG_WARN("Script [%d] is HTML not JS, skipping", scripts[i].parse_order);
                    http_free_buffer(&buffer);
                    scripts[i].url[0] = '\0';  // Mark as invalid
                } else {
                    scripts[i].content = buffer.data;
                    scripts[i].content_len = buffer.size;
                    LOG_INFO("Loaded external script [%d]: %zu bytes", 
                             scripts[i].parse_order, buffer.size);
                }
            } else {
                LOG_WARN("Failed to fetch script [%d]: %s", scripts[i].parse_order, error);
                if (buffer.data) http_free_buffer(&buffer);
                scripts[i].url[0] = '\0';  // Mark as invalid
            }
        }
    }
    
    // Build execution arrays
    const char *exec_scripts[MAX_SCRIPTS];
    size_t exec_script_lens[MAX_SCRIPTS];
    int exec_count = 0;
    
    for (int i = 0; i < script_count && exec_count < MAX_SCRIPTS; i++) {
        for (int j = 0; j < script_count; j++) {
            if (scripts[j].parse_order == i) {
                // Skip invalid external scripts
                if (scripts[j].type == SCRIPT_TYPE_EXTERNAL && scripts[j].url[0] == '\0') {
                    break;
                }
                // Skip empty inline scripts
                if (scripts[j].type == SCRIPT_TYPE_INLINE && 
                    (!scripts[j].content || scripts[j].content_len == 0)) {
                    break;
                }
                exec_scripts[exec_count] = scripts[j].content;
                exec_script_lens[exec_count] = scripts[j].content_len;
                exec_count++;
                break;
            }
        }
    }
    
    if (exec_count == 0) {
        LOG_ERROR("No valid scripts to execute");
        free_script_infos(scripts, script_count);
        return NULL;
    }
    
    LOG_INFO("Executing %d scripts...", exec_count);
    
    JsExecResult js_result;
    memset(&js_result, 0, sizeof(JsExecResult));
    
    bool js_success = js_quickjs_exec_scripts(
        exec_scripts, exec_script_lens, exec_count,
        html, &js_result
    );
    
    free_script_infos(scripts, script_count);
    
    if (!js_success) {
        LOG_ERROR("JavaScript execution failed");
        return NULL;
    }
    
    LOG_INFO("JavaScript execution successful");
    
    // Get player response from JS context
    char *player_response = js_quickjs_get_player_response();
    if (player_response) {
        LOG_INFO("Got player response from JS context: %zu bytes", strlen(player_response));
    } else {
        LOG_WARN("No player response available in JS context");
    }
    
    return player_response;
}

// Main extraction function
int html_extract_media_streams(const char *html_url, MediaStream *streams, int max_streams) {
    if (!html_url || !streams || max_streams <= 0) {
        LOG_ERROR("Invalid arguments to html_extract_media_streams");
        return -1;
    }
    
    LOG_INFO("Extracting media streams from: %s", html_url);
    
    // Extract video ID
    char video_id[32] = {0};
    if (!extract_yt_video_id(html_url, video_id, sizeof(video_id))) {
        LOG_ERROR("Could not extract video ID from URL");
        return -1;
    }
    LOG_INFO("Video ID: %s", video_id);
    
    // Download HTML page
    char html_url_buffer[512];
    if (strstr(html_url, "?v=") || strstr(html_url, "&v=")) {
        strncpy(html_url_buffer, html_url, sizeof(html_url_buffer) - 1);
    } else {
        snprintf(html_url_buffer, sizeof(html_url_buffer), 
                 "https://www.youtube.com/watch?v=%s&hl=en", video_id);
    }
    html_url_buffer[sizeof(html_url_buffer) - 1] = '\0';
    
    HttpBuffer html_buffer;
    memset(&html_buffer, 0, sizeof(HttpBuffer));
    
    char error[256];
    LOG_INFO("Downloading HTML from: %s", html_url_buffer);
    bool result = http_get_to_memory(html_url_buffer, &html_buffer, error, sizeof(error));
    
    if (!result || !html_buffer.data || html_buffer.size == 0) {
        LOG_ERROR("Failed to download HTML: %s", error);
        if (html_buffer.data) http_free_buffer(&html_buffer);
        return -1;
    }
    
    // Ensure null termination
    if (html_buffer.size >= MAX_HTML_SIZE) {
        html_buffer.data[MAX_HTML_SIZE - 1] = '\0';
    } else {
        html_buffer.data[html_buffer.size] = '\0';
    }
    
    LOG_INFO("Downloaded %zu bytes of HTML", html_buffer.size);
    
    // Execute scripts and get player response from JS context
    char *player_response = execute_scripts_and_get_player_response(html_buffer.data);
    if (!player_response) {
        LOG_ERROR("Failed to get player response from JS context");
        http_free_buffer(&html_buffer);
        return -1;
    }
    
    // Parse streams from player response using cJSON
    int stream_count = parse_yt_player_response(player_response, streams, max_streams);
    
    if (stream_count == 0) {
        LOG_WARN("No streams found in player response");
    } else {
        LOG_INFO("Found %d streams in player response", stream_count);
    }
    
    // Extract title from parsed JSON
    cJSON *root = cJSON_Parse(player_response);
    if (root) {
        cJSON *video_details = cJSON_GetObjectItemCaseSensitive(root, "videoDetails");
        if (video_details) {
            cJSON *title_obj = cJSON_GetObjectItemCaseSensitive(video_details, "title");
            if (cJSON_IsString(title_obj)) {
                LOG_INFO("Video title: %s", title_obj->valuestring);
            }
        }
        cJSON_Delete(root);
    }
    
    free(player_response);
    http_free_buffer(&html_buffer);
    
    return stream_count;
}

// Backward compatibility wrapper for html_extract_media_url
bool html_extract_media_url(const char *html, HtmlMediaCandidate *outCandidate,
                            char *err, size_t errLen) {
    if (!html || !outCandidate) {
        if (err && errLen > 0) {
            strncpy(err, "Invalid arguments", errLen - 1);
            err[errLen - 1] = '\0';
        }
        return false;
    }
    
    // Clear output
    memset(outCandidate, 0, sizeof(HtmlMediaCandidate));
    
    // Execute scripts and get player response from JS context
    char *player_response = execute_scripts_and_get_player_response(html);
    if (!player_response) {
        if (err && errLen > 0) {
            strncpy(err, "Failed to get player response from JS context", errLen - 1);
            err[errLen - 1] = '\0';
        }
        return false;
    }
    
    // Parse streams from player response using cJSON
    MediaStream streams[32];
    int stream_count = parse_yt_player_response(player_response, streams, 32);
    
    if (stream_count == 0) {
        LOG_WARN("No streams found in player response");
        free(player_response);
        if (err && errLen > 0) {
            strncpy(err, "No streams found in player response", errLen - 1);
            err[errLen - 1] = '\0';
        }
        return false;
    }
    
    free(player_response);
    
    // Find the best stream (prefer video with highest quality and direct URL)
    MediaStream *best = NULL;
    
    // First, try to find a stream without cipher (direct URL)
    for (int i = 0; i < stream_count; i++) {
        if (streams[i].url[0] && !streams[i].has_cipher) {
            // Prefer higher resolution
            if (!best || streams[i].height > best->height) {
                best = &streams[i];
            }
        }
    }
    
    // If no direct URL found, use first available with URL
    if (!best) {
        for (int i = 0; i < stream_count; i++) {
            if (streams[i].url[0]) {
                best = &streams[i];
                break;
            }
        }
    }
    
    if (!best) {
        if (err && errLen > 0) {
            strncpy(err, "No valid stream URLs found", errLen - 1);
            err[errLen - 1] = '\0';
        }
        return false;
    }
    
    // Use the stream URL (should already be decrypted from JS context)
    strncpy(outCandidate->url, best->url, sizeof(outCandidate->url) - 1);
    outCandidate->url[sizeof(outCandidate->url) - 1] = '\0';
    
    strncpy(outCandidate->mime, best->mime_type, sizeof(outCandidate->mime) - 1);
    outCandidate->mime[sizeof(outCandidate->mime) - 1] = '\0';
    
    LOG_INFO("Selected best stream: itag=%d, height=%d, has_cipher=%d, url=%.50s...",
             best->itag, best->height, best->has_cipher, outCandidate->url);
    
    return true;
}
