#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <pthread.h>
#include <android/log.h>
#include "html_media_extract.h"
#include "http_download.h"
#include "js_quickjs.h"

#define LOG_TAG "html_extract"
#define LOG_INFO(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOG_ERROR(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOG_WARN(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

#define MAX_SCRIPT_URLS 32
#define SCRIPT_URL_MAX_LEN 512
#define MAX_HTML_SIZE (2 * 1024 * 1024)  // 2MB max

// Media stream structure
typedef struct MediaStream {
    char url[2048];
    char mime_type[128];
    char quality[32];
    int width;
    int height;
    int itag;
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

// Parse ytInitialPlayerResponse and extract streaming data
static int parse_yt_player_response(const char *json, MediaStream *streams, int max_streams) {
    if (!json || !streams || max_streams <= 0) return 0;
    
    int count = 0;
    LOG_INFO("Parsing player response (%zu bytes)", strlen(json));
    
    // Find streamingData
    const char *streaming_data = strstr(json, "\"streamingData\"");
    if (!streaming_data) {
        LOG_WARN("No streamingData found in player response");
        return 0;
    }
    
    // Look for formats (adaptiveFormats and formats)
    const char *formats = strstr(streaming_data, "\"formats\"");
    const char *adaptive_formats = strstr(streaming_data, "\"adaptiveFormats\"");
    
    if (!formats && !adaptive_formats) {
        LOG_WARN("No formats found in streamingData");
        return 0;
    }
    
    // Parse formats array
    const char *fmt_start = formats ? formats : adaptive_formats;
    const char *array_start = strchr(fmt_start, '[');
    if (!array_start) return 0;
    
    // Simple JSON parsing for format objects
    const char *p = array_start;
    int brace_depth = 0;
    bool in_string = false;
    bool escape = false;
    
    // Buffer for current format object
    char format_buf[4096];
    int fmt_len = 0;
    
    while (*p && count < max_streams) {
        if (!in_string) {
            if (*p == '{') {
                if (brace_depth == 0) fmt_len = 0;
                brace_depth++;
            } else if (*p == '}') {
                brace_depth--;
                if (brace_depth == 0 && fmt_len > 0 && fmt_len < sizeof(format_buf) - 1) {
                    format_buf[fmt_len++] = '}';
                    format_buf[fmt_len] = '\0';
                    
                    // Extract fields from format object
                    MediaStream *stream = &streams[count];
                    memset(stream, 0, sizeof(MediaStream));
                    
                    // itag
                    const char *itag = strstr(format_buf, "\"itag\"");
                    if (itag) {
                        itag = strchr(itag, ':');
                        if (itag) stream->itag = atoi(itag + 1);
                    }
                    
                    // url
                    const char *url = strstr(format_buf, "\"url\"");
                    if (url) {
                        url = strchr(url, '"');
                        if (url) url = strchr(url + 1, '"');
                        if (url) {
                            url++;
                            const char *url_end = strchr(url, '"');
                            if (url_end && url_end - url < sizeof(stream->url) - 1) {
                                strncpy(stream->url, url, url_end - url);
                                stream->url[url_end - url] = '\0';
                            }
                        }
                    }
                    
                    // signatureCipher
                    if (!stream->url[0]) {
                        const char *cipher = strstr(format_buf, "\"signatureCipher\"");
                        if (cipher) {
                            cipher = strchr(cipher, '"');
                            if (cipher) cipher = strchr(cipher + 1, '"');
                            if (cipher) {
                                cipher++;
                                const char *cipher_end = strchr(cipher, '"');
                                if (cipher_end && cipher_end - cipher < sizeof(stream->url) - 1) {
                                    strncpy(stream->url, cipher, cipher_end - cipher);
                                    stream->url[cipher_end - cipher] = '\0';
                                }
                            }
                        }
                    }
                    
                    // mimeType
                    const char *mime = strstr(format_buf, "\"mimeType\"");
                    if (mime) {
                        mime = strchr(mime, '"');
                        if (mime) mime = strchr(mime + 1, '"');
                        if (mime) {
                            mime++;
                            const char *mime_end = strchr(mime, '"');
                            if (mime_end && mime_end - mime < sizeof(stream->mime_type) - 1) {
                                strncpy(stream->mime_type, mime, mime_end - mime);
                                stream->mime_type[mime_end - mime] = '\0';
                            }
                        }
                    }
                    
                    // width/height for video
                    const char *width = strstr(format_buf, "\"width\"");
                    if (width) {
                        width = strchr(width, ':');
                        if (width) stream->width = atoi(width + 1);
                    }
                    
                    const char *height = strstr(format_buf, "\"height\"");
                    if (height) {
                        height = strchr(height, ':');
                        if (height) stream->height = atoi(height + 1);
                    }
                    
                    // qualityLabel
                    const char *quality = strstr(format_buf, "\"qualityLabel\"");
                    if (quality) {
                        quality = strchr(quality, '"');
                        if (quality) quality = strchr(quality + 1, '"');
                        if (quality) {
                            quality++;
                            const char *quality_end = strchr(quality, '"');
                            if (quality_end && quality_end - quality < sizeof(stream->quality) - 1) {
                                strncpy(stream->quality, quality, quality_end - quality);
                                stream->quality[quality_end - quality] = '\0';
                            }
                        }
                    }
                    
                    LOG_INFO("Parsed stream: itag=%d, url=%.50s, mime=%s", 
                             stream->itag, stream->url[0] ? stream->url : "(empty)", 
                             stream->mime_type[0] ? stream->mime_type : "(empty)");
                    
                    if (stream->url[0] && stream->itag > 0) {
                        count++;
                    } else {
                        LOG_INFO("Stream skipped: url empty or no itag");
                    }
                    
                    fmt_len = 0;
                }
            }
        }
        
        if (brace_depth > 0 && fmt_len < sizeof(format_buf) - 1) {
            format_buf[fmt_len++] = *p;
        }
        
        if (!escape && *p == '"') {
            in_string = !in_string;
        } else if (!escape && *p == '\\') {
            escape = true;
        } else {
            escape = false;
        }
        
        p++;
    }
    
    LOG_INFO("Parsed %d streams from player response", count);
    return count;
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
                        char *json = malloc(len + 1);
                        if (json) {
                            strncpy(json, start, len);
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

// Extract video title from ytInitialPlayerResponse
static char* extract_video_title(const char *player_response) {
    if (!player_response) return NULL;
    
    const char *title_field = strstr(player_response, "\"title\"");
    if (!title_field) return NULL;
    
    title_field = strchr(title_field, '"');
    if (title_field) title_field = strchr(title_field + 1, '"');
    if (!title_field) return NULL;
    
    title_field++;
    const char *title_end = strchr(title_field, '"');
    if (!title_end) return NULL;
    
    size_t len = title_end - title_field;
    char *title = malloc(len + 1);
    if (title) {
        strncpy(title, title_field, len);
        title[len] = '\0';
    }
    return title;
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
        }
        
        p = end;
    }
    
    LOG_INFO("Extracted %d script URLs", count);
    return count;
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
            scripts[loaded_count] = buffer.data;
            script_lens[loaded_count] = buffer.size;
            loaded_count++;
            LOG_INFO("Loaded script %d: %zu bytes", i, buffer.size);
        } else {
            LOG_WARN("Failed to fetch script %d: %s", i, error);
            if (buffer.data) http_free_buffer(&buffer);
        }
    }
    
    if (loaded_count == 0) {
        LOG_ERROR("Failed to fetch any scripts");
        return false;
    }
    
    LOG_INFO("Successfully loaded %d scripts", loaded_count);
    
    // Extract ytInitialPlayerResponse from HTML
    char *player_response = extract_yt_player_response(html);
    
    // Execute all scripts with the player response injected
    JsExecResult js_result;
    memset(&js_result, 0, sizeof(JsExecResult));
    
    LOG_INFO("Executing %d scripts with ytInitialPlayerResponse", loaded_count);
    
    bool js_success = js_quickjs_exec_scripts_with_data(
        (const char**)scripts, script_lens, loaded_count,
        player_response, &js_result
    );
    
    // Free scripts
    for (int i = 0; i < loaded_count; i++) {
        if (scripts[i]) {
            HttpBuffer buffer = { .data = (char*)scripts[i], .size = script_lens[i] };
            http_free_buffer(&buffer);
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
    
    // Parse streams from player response
    int stream_count = parse_yt_player_response(player_response, streams, max_streams);
    
    if (stream_count == 0) {
        LOG_ERROR("No streams found in player response");
        free(player_response);
        http_free_buffer(&html_buffer);
        return 0;
    }
    
    LOG_INFO("Found %d streams in player response", stream_count);
    
    // Check if any streams need signature decryption
    bool needs_decryption = false;
    for (int i = 0; i < stream_count; i++) {
        if (strstr(streams[i].url, "&s=") || strstr(streams[i].url, "&sig=") ||
            strstr(streams[i].url, "signatureCipher=")) {
            needs_decryption = true;
            break;
        }
    }
    
    // If decryption needed, try to decrypt signatures
    if (needs_decryption) {
        LOG_INFO("Streams need signature decryption, attempting...");
        
        // Try to find and execute player scripts
        char decrypted_url[2048];
        if (decrypt_signature_with_scripts(html_buffer.data, "", decrypted_url, sizeof(decrypted_url))) {
            LOG_INFO("Successfully got decrypted URL format");
            // The URLs we got should already be decrypted from the player
        } else {
            LOG_WARN("Could not decrypt signatures using player scripts");
        }
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
    
    // Parse streams from player response
    MediaStream streams[16];
    int stream_count = parse_yt_player_response(player_response, streams, 16);
    free(player_response);
    
    if (stream_count == 0) {
        if (err && errLen > 0) {
            strncpy(err, "No streams found in player response", errLen - 1);
            err[errLen - 1] = '\0';
        }
        return false;
    }
    
    // Find the best stream (prefer video with highest quality)
    MediaStream *best = &streams[0];
    for (int i = 1; i < stream_count; i++) {
        // Prefer higher resolution
        if (streams[i].height > best->height) {
            best = &streams[i];
        }
    }
    
    // Copy to output
    strncpy(outCandidate->url, best->url, sizeof(outCandidate->url) - 1);
    outCandidate->url[sizeof(outCandidate->url) - 1] = '\0';
    strncpy(outCandidate->mime, best->mime_type, sizeof(outCandidate->mime) - 1);
    outCandidate->mime[sizeof(outCandidate->mime) - 1] = '\0';
    
    return true;
}
