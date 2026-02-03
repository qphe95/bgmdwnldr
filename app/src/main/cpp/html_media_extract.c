#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <pthread.h>
#include <android/log.h>
#include "cJSON.h"
#include "html_media_extract.h"
#include "http_download.h"
#include "js_quickjs.h"

#define LOG_TAG "html_extract"
#define LOG_INFO(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOG_ERROR(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOG_WARN(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

#define MAX_SCRIPT_URLS 32
#define SCRIPT_URL_MAX_LEN 512
#define MAX_HTML_SIZE (10 * 1024 * 1024)  // 10MB max for large YouTube pages

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

// Extract video title from ytInitialPlayerResponse using cJSON
static char* extract_video_title(const char *player_response) {
    if (!player_response) return NULL;
    
    cJSON *root = cJSON_Parse(player_response);
    if (!root) return NULL;
    
    char *title = NULL;
    
    // Try videoDetails.title
    cJSON *video_details = cJSON_GetObjectItemCaseSensitive(root, "videoDetails");
    if (video_details) {
        cJSON *title_obj = cJSON_GetObjectItemCaseSensitive(video_details, "title");
        if (cJSON_IsString(title_obj)) {
            title = strdup(title_obj->valuestring);
        }
    }
    
    cJSON_Delete(root);
    return title;
}

// Extract ytInitialPlayerResponse from HTML

static char* extract_yt_player_response(const char *html) {
    if (!html) return NULL;
    
    const char *patterns[] = {
        "var ytInitialPlayerResponse = ",
        "ytInitialPlayerResponse = ",
        "window.ytInitialPlayerResponse = ",
        NULL
    };
    
    for (int p = 0; patterns[p]; p++) {
        const char *start = strstr(html, patterns[p]);
        if (!start) continue;
        
        start += strlen(patterns[p]);
        
        // Skip whitespace
        while (*start && isspace((unsigned char)*start)) start++;
        
        // Find the end - look for matching braces
        if (*start != '{') continue;
        
        const char *p_json = start;
        int brace_depth = 0;
        bool in_string = false;
        bool escape = false;
        
        while (*p_json) {
            if (!in_string) {
                if (*p_json == '{') brace_depth++;
                else if (*p_json == '}') {
                    brace_depth--;
                    if (brace_depth == 0) {
                        // Found complete JSON
                        size_t len = p_json - start + 1;
                        if (len > 10 * 1024 * 1024) {  // Sanity check: max 10MB
                            LOG_ERROR("JSON too large: %zu bytes", len);
                            return NULL;
                        }
                        char *json = malloc(len + 1);
                        if (json) {
                            memcpy(json, start, len);
                            json[len] = '\0';
                            LOG_INFO("Extracted ytInitialPlayerResponse (%zu bytes)", len);
                            return json;
                        }
                    }
                }
            }
            
            if (!escape && *p_json == '"') {
                in_string = !in_string;
            } else if (!escape && *p_json == '\\') {
                escape = true;
            } else {
                escape = false;
            }
            
            p_json++;
        }
    }
    
    return NULL;
}

// Extract all script URLs from HTML
static int extract_all_script_urls(const char *html, char out_urls[][SCRIPT_URL_MAX_LEN], int max_urls) {
    if (!html || !out_urls || max_urls <= 0) return 0;
    
    int count = 0;
    const char *p = html;
    
    while ((p = strstr(p, "<script")) != NULL && count < max_urls) {
        // Find src attribute
        const char *src = strstr(p, "src=");
        if (!src) {
            p++;
            continue;
        }
        
        src += 4;
        while (*src && isspace((unsigned char)*src)) src++;
        
        char quote = *src;
        if (quote != '"' && quote != '\'') {
            p++;
            continue;
        }
        
        src++;
        const char *end = strchr(src, quote);
        if (!end) {
            p++;
            continue;
        }
        
        size_t len = end - src;
        if (len > 0 && len < SCRIPT_URL_MAX_LEN - 1) {
            strncpy(out_urls[count], src, len);
            out_urls[count][len] = '\0';
            
            // Convert relative to absolute
            if (strncmp(out_urls[count], "//", 2) == 0) {
                char temp[SCRIPT_URL_MAX_LEN];
                snprintf(temp, sizeof(temp), "https:%s", out_urls[count]);
                strcpy(out_urls[count], temp);
            } else if (out_urls[count][0] == '/') {
                char temp[SCRIPT_URL_MAX_LEN];
                snprintf(temp, sizeof(temp), "https://www.youtube.com%s", out_urls[count]);
                strcpy(out_urls[count], temp);
            } else if (strncmp(out_urls[count], "http", 4) != 0) {
                // Skip non-HTTP URLs
                p++;
                continue;
            }
            
            LOG_INFO("Found script URL: %.80s...", out_urls[count]);
            count++;
        } else {
            LOG_WARN("Skipping script with invalid URL format");
        }
        
        p = end;
    }
    
    LOG_INFO("Extracted %d script URLs", count);
    return count;
}

// Extract inline scripts from HTML (scripts without src attribute)
// These contain initialization code like ytcfg, ytInitialData, etc.
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
        
        // Find the closing </script> tag (case insensitive)
        const char *content_start = tag_end + 1;
        const char *script_end = NULL;
        const char *search = content_start;
        
        while ((search = strstr(search, "<")) != NULL) {
            if (strncasecmp(search, "</script>", 9) == 0) {
                script_end = search;
                break;
            }
            search++;
        }
        
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
        // Skip data payload scripts that cause parsing issues
        if (strncmp(first_nonspace, "var ytInitialPlayerResponse", 27) == 0 ||
            strncmp(first_nonspace, "window.ytInitialPlayerResponse", 30) == 0 ||
            (strncmp(first_nonspace, "(function()", 11) == 0 && strstr(first_nonspace, "window.ytAtR") != NULL)) {
            skip = true; // Data payload scripts - not needed for signature decryption
        }
        // Skip very large scripts (>500KB) as they're likely data, not code
        if (content_len > 500000) {
            skip = true;
        }
        
        if (skip) {
            p = script_end + 9;
            continue;
        }
        
        // Extract the script content
        char *script = malloc(content_len + 1);
        if (script) {
            memcpy(script, content_start, content_len);
            script[content_len] = '\0';
            
            // Note: QuickJS has been modified to handle all characters properly
            // No sanitization needed - pass raw script content
            
            // Only keep script if it has meaningful content
            if (content_len > 50) {
                out_scripts[count] = script;
                LOG_INFO("Extracted inline script %d: %zu bytes", count, content_len);
                count++;
            } else {
                free(script);
            }
        }
        
        p = script_end + 9;
    }
    
    LOG_INFO("Extracted %d inline initialization scripts", count);
    return count;
}

// Free inline scripts
static void free_inline_scripts(char **scripts, int count) {
    for (int i = 0; i < count; i++) {
        if (scripts[i]) {
            free(scripts[i]);
            scripts[i] = NULL;
        }
    }
}

// Find and extract the base.js player script (most important for signature decryption)
static bool find_base_js_url(const char *html, char *out_url, size_t out_len) {
    if (!html || !out_url || out_len == 0) return false;
    
    // Try to find the player base script
    // Look for patterns like /s/player/.../player_ias.vflset/.../base.js
    const char *p = html;
    while ((p = strstr(p, "base.js")) != NULL) {
        // Find the start of the URL
        const char *start = p;
        while (start > html && start[-1] != '"' && start[-1] != '\'') start--;
        
        if (start > html) {
            const char *end = p + 7; // len("base.js")
            if (*end == '"' || *end == '\'') {
                size_t len = end - start;
                if (len < out_len) {
                    strncpy(out_url, start, len);
                    out_url[len] = '\0';
                    
                    // Make absolute
                    if (strncmp(out_url, "//", 2) == 0) {
                        char temp[512];
                        snprintf(temp, sizeof(temp), "https:%s", out_url);
                        strncpy(out_url, temp, out_len - 1);
                        out_url[out_len - 1] = '\0';
                    } else if (out_url[0] == '/') {
                        char temp[512];
                        snprintf(temp, sizeof(temp), "https://www.youtube.com%s", out_url);
                        strncpy(out_url, temp, out_len - 1);
                        out_url[out_len - 1] = '\0';
                    }
                    
                    LOG_INFO("Found base.js: %s", out_url);
                    return true;
                }
            }
        }
        p++;
    }
    
    return false;
}

// Decrypt signature using the player scripts
static bool decrypt_signature_with_scripts(const char *html, const char *encrypted_sig,
                                           char *out_decrypted, size_t out_len) {
    if (!html || !encrypted_sig || !out_decrypted || out_len == 0) {
        return false;
    }
    
    LOG_INFO("Attempting signature decryption");
    
    // Extract all script URLs
    char script_urls[MAX_SCRIPT_URLS][SCRIPT_URL_MAX_LEN];
    int script_count = extract_all_script_urls(html, script_urls, MAX_SCRIPT_URLS);
    
    if (script_count == 0) {
        LOG_ERROR("No script URLs found in HTML");
        return false;
    }
    
    // Prioritize base.js (main player script)
    char base_js[SCRIPT_URL_MAX_LEN] = {0};
    if (find_base_js_url(html, base_js, sizeof(base_js))) {
        // Move base.js to front of list
        for (int i = 0; i < script_count; i++) {
            if (strstr(script_urls[i], "base.js")) {
                char temp[SCRIPT_URL_MAX_LEN];
                strcpy(temp, script_urls[0]);
                strcpy(script_urls[0], script_urls[i]);
                strcpy(script_urls[i], temp);
                LOG_INFO("Prioritized base.js to position 0");
                break;
            }
        }
    }
    
    // Fetch all scripts using http_get_to_memory
    const char *scripts[MAX_SCRIPT_URLS];
    size_t script_lens[MAX_SCRIPT_URLS];
    int loaded_count = 0;
    
    for (int i = 0; i < script_count && loaded_count < MAX_SCRIPT_URLS; i++) {
        scripts[loaded_count] = NULL;
        script_lens[loaded_count] = 0;
        
        HttpBuffer buffer;
        memset(&buffer, 0, sizeof(HttpBuffer));
        
        char error[256];
        LOG_INFO("Fetching script %d/%d: %.80s", i + 1, script_count, script_urls[i]);
        
        bool result = http_get_to_memory(script_urls[i], &buffer, error, sizeof(error));
        if (result && buffer.data && buffer.size > 0) {
            /* Validate that the response is actually JavaScript, not HTML (e.g., a login page) */
            const char *content = buffer.data;
            
            /* Skip leading whitespace and BOM */
            while (*content && (isspace((unsigned char)*content) || 
                   (unsigned char)*content == 0xEF || 
                   (unsigned char)*content == 0xBB || 
                   (unsigned char)*content == 0xBF)) {
                content++;
            }
            
            /* Check for HTML indicators at the START of the file only
             * (JS files might contain <head>/<body> as string literals later in the code) */
            bool is_html = false;
            if (strncasecmp(content, "<!doctype", 9) == 0 ||
                strncasecmp(content, "<html", 5) == 0 ||
                strncasecmp(content, "<?xml", 5) == 0) {
                is_html = true;
            }
            
            /* Also check first 500 chars for HTML structure tags appearing at the beginning */
            if (!is_html) {
                char first_500[501];
                size_t check_len = strlen(content);
                if (check_len > 500) check_len = 500;
                memcpy(first_500, content, check_len);
                first_500[check_len] = '\0';
                
                /* Only flag as HTML if these tags appear very early in the document */
                if (strstr(first_500, "<head") != NULL || 
                    strstr(first_500, "<body") != NULL ||
                    strstr(first_500, "<title>") != NULL) {
                    is_html = true;
                }
            }
            
            if (is_html) {
                LOG_WARN("Script %d appears to be HTML, not JavaScript - skipping", i);
                LOG_WARN("Content preview: %.100s...", content);
                http_free_buffer(&buffer);
            } else {
                scripts[loaded_count] = buffer.data;
                script_lens[loaded_count] = buffer.size;
                loaded_count++;
                LOG_INFO("Loaded script %d: %zu bytes", i, buffer.size);
            }
        } else {
            LOG_WARN("Failed to fetch script %d: %s", i, error);
            if (buffer.data) http_free_buffer(&buffer);
        }
    }
    
    if (loaded_count == 0) {
        LOG_ERROR("Failed to fetch any scripts");
        return false;
    }
    
    LOG_INFO("Successfully loaded %d external scripts", loaded_count);
    
    // Note: QuickJS has been modified to handle all characters properly
    // No sanitization needed for external scripts
    
    // Extract ytInitialPlayerResponse from HTML
    char *player_response = extract_yt_player_response(html);
    
    // Extract inline scripts (initialization code like ytcfg, ytInitialData)
    char *inline_scripts[MAX_SCRIPT_URLS];
    int inline_count = extract_inline_scripts(html, inline_scripts, MAX_SCRIPT_URLS);
    
    // Combine inline and external scripts
    // Inline scripts must run FIRST to set up globals
    const char *all_scripts[MAX_SCRIPT_URLS * 2];
    size_t all_script_lens[MAX_SCRIPT_URLS * 2];
    int total_count = 0;
    
    // Add inline scripts first
    for (int i = 0; i < inline_count && total_count < MAX_SCRIPT_URLS * 2; i++) {
        all_scripts[total_count] = inline_scripts[i];
        all_script_lens[total_count] = strlen(inline_scripts[i]);
        total_count++;
    }
    
    // Add external scripts
    for (int i = 0; i < loaded_count && total_count < MAX_SCRIPT_URLS * 2; i++) {
        all_scripts[total_count] = scripts[i];
        all_script_lens[total_count] = script_lens[i];
        total_count++;
    }
    
    // Execute all scripts with the player response injected
    JsExecResult js_result;
    memset(&js_result, 0, sizeof(JsExecResult));
    
    LOG_INFO("Executing %d total scripts (%d inline + %d external) with ytInitialPlayerResponse", 
             total_count, inline_count, loaded_count);
    
    bool js_success = js_quickjs_exec_scripts_with_data(
        all_scripts, all_script_lens, total_count,
        player_response, html, &js_result
    );
    
    // Free inline scripts
    free_inline_scripts(inline_scripts, inline_count);
    
    // Free external scripts (now sanitized, allocated with malloc)
    for (int i = 0; i < loaded_count; i++) {
        if (scripts[i]) {
            free((void*)scripts[i]);
        }
    }
    
    if (!js_success) {
        LOG_ERROR("JavaScript execution failed");
        free(player_response);
        return false;
    }
    
    LOG_INFO("JavaScript execution successful, captured %d URLs", js_result.captured_url_count);
    
    // Look for decrypted URLs in captured URLs
    bool found = false;
    for (int i = 0; i < js_result.captured_url_count; i++) {
        LOG_INFO("Captured URL %d: %.100s...", i, js_result.captured_urls[i]);
        
        // Check if this is a googlevideo.com URL without signature cipher
        if (strstr(js_result.captured_urls[i], "googlevideo.com") &&
            !strstr(js_result.captured_urls[i], "&s=") &&
            !strstr(js_result.captured_urls[i], "&sig=")) {
            // This looks like a decrypted URL
            strncpy(out_decrypted, js_result.captured_urls[i], out_len - 1);
            out_decrypted[out_len - 1] = '\0';
            found = true;
            LOG_INFO("Found decrypted URL!");
            break;
        }
    }
    
    free(player_response);
    
    if (!found) {
        LOG_WARN("No decrypted URL found in captured URLs");
    }
    
    return found;
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
    
    // Extract ytInitialPlayerResponse
    char *player_response = extract_yt_player_response(html_buffer.data);
    if (!player_response) {
        LOG_ERROR("Failed to extract ytInitialPlayerResponse");
        http_free_buffer(&html_buffer);
        return -1;
    }
    
    LOG_INFO("Extracted player response: %zu bytes", strlen(player_response));
    
    // Parse streams from player response using cJSON
    int stream_count = parse_yt_player_response(player_response, streams, max_streams);
    
    // Check if any streams need signature decryption (either from cJSON or we assume they do)
    bool needs_decryption = (stream_count == 0);  // If cJSON failed, try JS anyway
    for (int i = 0; i < stream_count; i++) {
        if (streams[i].has_cipher || 
            strstr(streams[i].url, "&s=") || 
            strstr(streams[i].url, "&sig=") ||
            strstr(streams[i].url, "signatureCipher=")) {
            needs_decryption = true;
            break;
        }
    }
    
    // If decryption needed or cJSON failed, try to decrypt signatures using player scripts
    if (needs_decryption) {
        LOG_INFO("Attempting signature decryption with player scripts...");
        
        // Try to find and execute player scripts with the player response
        char decrypted_url[2048];
        if (decrypt_signature_with_scripts(html_buffer.data, "", decrypted_url, sizeof(decrypted_url))) {
            LOG_INFO("Successfully executed player scripts");
            // The JS execution captured URLs will be handled separately
        } else {
            LOG_WARN("Player script execution did not produce decrypted URLs");
        }
    }
    
    if (stream_count == 0) {
        LOG_WARN("No streams found in player response via cJSON (may need JS decryption)");
        // Don't return early - we may have captured URLs from JS execution
    } else {
        LOG_INFO("Found %d streams in player response", stream_count);
    }
    
    // Extract title
    char *title = extract_video_title(player_response);
    if (title) {
        LOG_INFO("Video title: %s", title);
        free(title);
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
    
    // Try to extract ytInitialPlayerResponse directly from the HTML
    char *player_response = extract_yt_player_response(html);
    if (!player_response) {
        if (err && errLen > 0) {
            strncpy(err, "No player response found in HTML", errLen - 1);
            err[errLen - 1] = '\0';
        }
        return false;
    }
    
    // Parse streams from player response using cJSON
    MediaStream streams[32];
    int stream_count = parse_yt_player_response(player_response, streams, 32);
    
    // If cJSON parsing failed or returned no streams, try JS execution
    if (stream_count == 0) {
        LOG_WARN("cJSON parsing returned 0 streams, trying JS execution...");
        
        // Try to decrypt signatures using player scripts
        char decrypted_url[2048];
        if (decrypt_signature_with_scripts(html, "", decrypted_url, sizeof(decrypted_url))) {
            LOG_INFO("JS execution succeeded, checking captured URLs...");
            // Check if we captured any URLs from JS execution
            // The captured URLs would be in the JsExecResult
        }
        
        // For now, still return error since we haven't captured URLs properly
        // TODO: Check captured URLs from JS execution and return success if found
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
    
    // If stream has cipher, try to decrypt signature
    if (best->has_cipher) {
        LOG_INFO("Stream has cipher, attempting signature decryption...");
        char decrypted_url[2048];
        if (decrypt_signature_with_scripts(html, best->url, decrypted_url, sizeof(decrypted_url))) {
            LOG_INFO("Successfully decrypted signature");
            strncpy(outCandidate->url, decrypted_url, sizeof(outCandidate->url) - 1);
            outCandidate->url[sizeof(outCandidate->url) - 1] = '\0';
        } else {
            LOG_WARN("Could not decrypt signature, using original URL (may fail with 403)");
            strncpy(outCandidate->url, best->url, sizeof(outCandidate->url) - 1);
            outCandidate->url[sizeof(outCandidate->url) - 1] = '\0';
        }
    } else {
        // No cipher, use URL directly
        strncpy(outCandidate->url, best->url, sizeof(outCandidate->url) - 1);
        outCandidate->url[sizeof(outCandidate->url) - 1] = '\0';
    }
    
    strncpy(outCandidate->mime, best->mime_type, sizeof(outCandidate->mime) - 1);
    outCandidate->mime[sizeof(outCandidate->mime) - 1] = '\0';
    
    LOG_INFO("Selected best stream: itag=%d, height=%d, has_cipher=%d, url=%.50s...",
             best->itag, best->height, best->has_cipher, outCandidate->url);
    
    return true;
}
