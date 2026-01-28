#include "html_media_extract.h"
#include "http_download.h"
#include "js_quickjs.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>

#define LOG_TAG "html_extract"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* ============================================================================
 * QuickJS Integration for Signature Deciphering
 * ============================================================================ */

/* Fetch player JS and decrypt signature using QuickJS */
static bool fetch_and_decrypt_signature(const char *player_js_url, const char *encrypted_sig,
                                         char *out_decrypted, size_t out_len) {
    if (!player_js_url || !player_js_url[0] || !encrypted_sig) {
        return false;
    }
    
    LOGI("Fetching player JS: %s", player_js_url);
    
    /* Fetch the player JS */
    HttpBuffer player_js = {0};
    char err[512] = {0};
    
    if (!http_get_to_memory(player_js_url, &player_js, err, sizeof(err))) {
        LOGE("Failed to fetch player JS: %s", err);
        return false;
    }
    
    LOGI("Player JS fetched: %zu bytes", player_js.size);
    
    /* Use QuickJS to decrypt signature */
    if (!js_quickjs_init()) {
        LOGE("Failed to initialize QuickJS");
        http_free_buffer(&player_js);
        return false;
    }
    
    size_t decrypted_len = 0;
    char *decrypted = js_quickjs_decrypt_signature(player_js.data, player_js.size,
                                                    encrypted_sig, &decrypted_len);
    
    http_free_buffer(&player_js);
    
    if (!decrypted || decrypted_len == 0) {
        LOGE("Failed to decrypt signature");
        return false;
    }
    
    if (decrypted_len >= out_len) {
        decrypted_len = out_len - 1;
    }
    
    memcpy(out_decrypted, decrypted, decrypted_len);
    out_decrypted[decrypted_len] = '\0';
    
    free(decrypted);
    LOGI("Successfully decrypted signature with QuickJS");
    return true;
}

/* Extract player JavaScript URL from YouTube page */
static bool extract_player_js_url(const char *html, char *out_url, size_t out_len) {
    LOGI("Looking for player JS URL in HTML...");
    
    /* Look for player base.js URL pattern - common patterns in YouTube HTML */
    const char *patterns[] = {
        "/s/player/",
        "/player/",
        "player",
        NULL
    };
    
    for (int i = 0; patterns[i] != NULL; i++) {
        const char *p = html;
        
        while ((p = strstr(p, patterns[i])) != NULL) {
            LOGI("Found pattern '%s' at position %zu", patterns[i], (size_t)(p - html));
            
            /* Look backwards for script src or URL start */
            const char *start = p;
            while (start > html && start > p - 300) {
                if (strncmp(start, "https://", 8) == 0 ||
                    strncmp(start, "//", 2) == 0) {
                    break;
                }
                if (*start == '"' || *start == '\'') {
                    start++; /* Move past quote */
                    if (strncmp(start, "https://", 8) == 0 ||
                        strncmp(start, "//", 2) == 0 ||
                        strncmp(start, "/", 1) == 0) {
                        break;
                    }
                    start--; /* Move back */
                }
                start--;
            }
            
            /* Look forwards for end of URL */
            const char *end = p + 6;
            while (*end && *end != '"' && *end != '\'' && *end != ' ' && *end > 32) {
                end++;
            }
            
            if (end > start && (size_t)(end - start) > 10 && (size_t)(end - start) < out_len) {
                /* Check if it looks like a player JS file */
                size_t len = (size_t)(end - start);
                char temp[512];
                if (len >= sizeof(temp)) len = sizeof(temp) - 1;
                memcpy(temp, start, len);
                temp[len] = '\0';
                
                if (strstr(temp, "player") && (strstr(temp, ".js") || strstr(temp, "base.js"))) {
                    /* Fix protocol-relative URLs */
                    if (strncmp(temp, "//", 2) == 0) {
                        snprintf(out_url, out_len, "https:%s", temp);
                    } else {
                        memcpy(out_url, temp, len + 1);
                    }
                    LOGI("Found player JS URL: %s", out_url);
                    return true;
                }
            }
            p++;
        }
    }
    
    return false;
}

/* Note: The old custom JS engine has been removed.
 * We now use QuickJS for signature decryption.
 * See fetch_and_decrypt_signature() below.
 */

static const char *find_case_insensitive(const char *haystack, const char *needle) {
    size_t needleLen = strlen(needle);
    if (needleLen == 0) {
        return haystack;
    }
    for (const char *p = haystack; *p; ++p) {
        size_t i = 0;
        while (p[i] && i < needleLen &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            ++i;
        }
        if (i == needleLen) {
            return p;
        }
    }
    return NULL;
}

static bool extract_quoted_value(const char *start, char *out, size_t outLen) {
    const char *quote = strchr(start, '"');
    char quoteChar = '"';
    if (!quote) {
        quote = strchr(start, '\'');
        quoteChar = '\'';
    }
    if (!quote) {
        return false;
    }
    const char *end = strchr(quote + 1, quoteChar);
    if (!end || end <= quote + 1) {
        return false;
    }
    size_t len = (size_t)(end - (quote + 1));
    if (len + 1 > outLen) {
        len = outLen - 1;
    }
    memcpy(out, quote + 1, len);
    out[len] = '\0';
    return true;
}

static void url_decode_inplace(char *s) {
    char *w = s;
    for (const char *r = s; *r; ++r) {
        if (*r == '%' && isxdigit((unsigned char)r[1]) && isxdigit((unsigned char)r[2])) {
            char hex[3] = { r[1], r[2], '\0' };
            *w++ = (char)strtol(hex, NULL, 16);
            r += 2;
        } else if (*r == '+') {
            *w++ = ' ';
        } else {
            *w++ = *r;
        }
    }
    *w = '\0';
}

static void json_unescape(const char *in, char *out, size_t outLen) {
    size_t w = 0;
    for (size_t i = 0; in[i] && w + 1 < outLen; ++i) {
        if (in[i] == '\\') {
            char next = in[i + 1];
            if (next == 'u' &&
                isxdigit((unsigned char)in[i + 2]) &&
                isxdigit((unsigned char)in[i + 3]) &&
                isxdigit((unsigned char)in[i + 4]) &&
                isxdigit((unsigned char)in[i + 5])) {
                char hex[5] = { in[i + 2], in[i + 3], in[i + 4], in[i + 5], '\0' };
                long code = strtol(hex, NULL, 16);
                // Handle all Unicode characters, not just ASCII
                if (code > 0 && code <= 0xFF) {
                    out[w++] = (char)code;
                } else {
                    // For non-ASCII, keep the original (shouldn't happen in URLs)
                    out[w++] = in[i];
                }
                i += 5;
                continue;
            }
            if (next == '/' || next == '\\' || next == '"' || next == '\'' || next == 'n' || next == 't' || next == 'r') {
                if (next == 'n') out[w++] = '\n';
                else if (next == 't') out[w++] = '\t';
                else if (next == 'r') out[w++] = '\r';
                else out[w++] = next;
                ++i;
                continue;
            }
            // Unknown escape, keep the backslash
            out[w++] = in[i];
            continue;
        }
        out[w++] = in[i];
    }
    out[w] = '\0';
}

static void json_unescape_repeat(const char *in, char *out, size_t outLen) {
    // Apply json_unescape multiple times until no more changes
    char temp1[2048], temp2[2048];
    strncpy(temp1, in, sizeof(temp1) - 1);
    temp1[sizeof(temp1) - 1] = '\0';
    
    for (int pass = 0; pass < 5; pass++) {
        json_unescape(temp1, temp2, sizeof(temp2));
        if (strcmp(temp1, temp2) == 0) {
            // No more changes
            break;
        }
        strncpy(temp1, temp2, sizeof(temp1) - 1);
        temp1[sizeof(temp1) - 1] = '\0';
    }
    strncpy(out, temp1, outLen - 1);
    out[outLen - 1] = '\0';
}

static bool query_get_param(const char *query, const char *key, char *out, size_t outLen) {
    size_t keyLen = strlen(key);
    const char *p = query;
    while (p && *p) {
        const char *eq = strchr(p, '=');
        if (!eq) {
            break;
        }
        size_t nameLen = (size_t)(eq - p);
        if (nameLen == keyLen && strncmp(p, key, keyLen) == 0) {
            const char *valStart = eq + 1;
            const char *amp = strchr(valStart, '&');
            size_t valLen = amp ? (size_t)(amp - valStart) : strlen(valStart);
            if (valLen >= outLen) {
                valLen = outLen - 1;
            }
            memcpy(out, valStart, valLen);
            out[valLen] = '\0';
            return true;
        }
        p = strchr(eq + 1, '&');
        if (p) {
            ++p;
        }
    }
    return false;
}

static bool find_json_value(const char *json, const char *key, char *out, size_t outLen) {
    char searchKey[256];
    snprintf(searchKey, sizeof(searchKey), "\"%s\"", key);
    const char *keyPos = find_case_insensitive(json, searchKey);
    if (!keyPos) {
        return false;
    }
    const char *colon = strchr(keyPos, ':');
    if (!colon) {
        return false;
    }
    // Skip whitespace after colon
    const char *valueStart = colon + 1;
    while (*valueStart && (*valueStart == ' ' || *valueStart == '\t' || *valueStart == '\n' || *valueStart == '\r')) {
        valueStart++;
    }
    // Try to extract quoted value
    if (extract_quoted_value(valueStart - 1, out, outLen)) {
        return true;
    }
    // Try unquoted value (for numbers, booleans, etc.)
    const char *end = valueStart;
    while (*end && *end != ',' && *end != '}' && *end != ']' && *end != '\n' && *end != '\r') {
        end++;
    }
    size_t len = (size_t)(end - valueStart);
    if (len > 0 && len < outLen) {
        memcpy(out, valueStart, len);
        out[len] = '\0';
        return true;
    }
    return false;
}

static bool extract_youtube_audio_url(const char *html, char *outUrl, size_t outLen) {
    // Look for ytInitialPlayerResponse or similar JavaScript variables
    const char *playerResponse = find_case_insensitive(html, "ytInitialPlayerResponse");
    if (playerResponse) {
        LOGI("Found ytInitialPlayerResponse");
    } else {
        playerResponse = find_case_insensitive(html, "var ytInitialPlayerResponse");
        if (playerResponse) {
            LOGI("Found var ytInitialPlayerResponse");
        }
    }
    if (!playerResponse) {
        playerResponse = find_case_insensitive(html, "ytInitialData");
        if (playerResponse) {
            LOGI("Found ytInitialData");
        }
    }
    if (!playerResponse) {
        LOGI("No ytInitialPlayerResponse or ytInitialData found in HTML");
        // Try direct search for streamingData
        const char *streamingData = find_case_insensitive(html, "\"streamingData\"");
        if (streamingData) {
            LOGI("Found streamingData directly in HTML");
            playerResponse = streamingData - 1000; // Look backwards a bit for context
        }
    }
    
    // If we found a player response variable, try to extract JSON from it
    if (playerResponse) {
        const char *equals = strchr(playerResponse, '=');
        if (equals) {
            // Find the start of JSON (should be {)
            const char *jsonStart = equals + 1;
            while (*jsonStart && (*jsonStart == ' ' || *jsonStart == '\t' || *jsonStart == '\n')) {
                jsonStart++;
            }
            if (*jsonStart == '{') {
                // Find matching closing brace (simplified - just look for streamingData)
                const char *streamingData = find_case_insensitive(jsonStart, "\"streamingData\"");
                if (streamingData) {
                    const char *formats = find_case_insensitive(streamingData, "\"formats\"");
                    const char *adaptiveFormats = find_case_insensitive(streamingData, "\"adaptiveFormats\"");
                    const char *searchStart = adaptiveFormats ? adaptiveFormats : formats;
                    if (searchStart) {
                        // Look for audio mimeType in formats array
                        const char *cursor = searchStart;
                        while ((cursor = find_case_insensitive(cursor, "\"mimeType\"")) != NULL) {
                            char mime[256];
                            if (!find_json_value(cursor, "mimeType", mime, sizeof(mime))) {
                                cursor++;
                                continue;
                            }
                            // Accept pure audio formats OR combined video+audio formats (for direct download)
                            bool isAudio = strstr(mime, "audio/") != NULL;
                            bool isVideoWithAudio = strstr(mime, "video/") != NULL && strstr(mime, "mp4a") != NULL;
                            if (!isAudio && !isVideoWithAudio) {
                                LOGI("Skipping non-audio mimeType: %s", mime);
                                cursor++;
                                continue;
                            }
                            LOGI("Found audio mimeType: %s", mime);
                            // Found audio mimeType, now get the URL
                            // Make sure we're looking within the same object (not too far away)
                            const char *objStart = cursor;
                            while (objStart > searchStart && objStart > cursor - 500) {
                                if (*objStart == '{') {
                                    break;
                                }
                                objStart--;
                            }
                            const char *objEnd = strchr(cursor, '}');
                            if (!objEnd || objEnd > cursor + 2000) {
                                cursor++;
                                continue;
                            }
                            // Try to find "url" field first
                            const char *urlPos = find_case_insensitive(objStart, "\"url\"");
                            LOGI("Looking for url field, urlPos=%p, objEnd=%p", (void*)urlPos, (void*)objEnd);
                            if (urlPos && urlPos < objEnd) {
                                char url[2048];
                                LOGI("Found url field, extracting value...");
                                if (find_json_value(urlPos, "url", url, sizeof(url))) {
                                    LOGI("Extracted raw URL: %.100s...", url);
                                    // Validate it's a googlevideo.com URL, not a thumbnail
                                    if (strstr(url, "googlevideo.com") && !strstr(url, "i.ytimg.com")) {
                                        char unescaped[2048];
                                        json_unescape_repeat(url, unescaped, sizeof(unescaped));
                                        LOGI("After json_unescape_repeat: %s", unescaped);
                                        url_decode_inplace(unescaped);
                                        LOGI("After url_decode: %s", unescaped);
                                        // Accept audio streams, or video streams that contain audio (for direct download)
                                        bool hasAudio = strstr(unescaped, "mime=audio") ||
                                                       strstr(unescaped, "itag=140") || strstr(unescaped, "itag=251") ||
                                                       strstr(unescaped, "itag=250") || strstr(unescaped, "itag=249") ||
                                                       strstr(unescaped, "itag=171") || strstr(unescaped, "itag=172") ||
                                                       strstr(unescaped, "itag=18") || strstr(unescaped, "itag=22") ||
                                                       strstr(unescaped, "itag=43");
                                        if (hasAudio) {
                                            /* Check if URL has encrypted signature (sig parameter) */
                                            char sig[512] = {0};
                                            char sp[64] = {0};
                                            bool hasEncryptedSig = false;
                                            
                                            if (strstr(unescaped, "&sig=") || strstr(unescaped, "?sig=")) {
                                                LOGI("URL has encrypted signature, needs decryption");
                                                hasEncryptedSig = true;
                                                /* Extract sig from URL */
                                                const char *sigStart = strstr(unescaped, "sig=");
                                                if (sigStart) {
                                                    sigStart += 4;
                                                    const char *sigEnd = strchr(sigStart, '&');
                                                    size_t sigLen = sigEnd ? (size_t)(sigEnd - sigStart) : strlen(sigStart);
                                                    if (sigLen >= sizeof(sig)) sigLen = sizeof(sig) - 1;
                                                    memcpy(sig, sigStart, sigLen);
                                                    sig[sigLen] = '\0';
                                                    LOGI("Extracted encrypted sig: %.50s...", sig);
                                                }
                                                snprintf(sp, sizeof(sp), "sig");
                                                
                                                /* Try to decrypt signature */
                                                char decrypted_sig[512] = {0};
                                                char player_js_url[512] = {0};
                                                
                                                if (extract_player_js_url(html, player_js_url, sizeof(player_js_url))) {
                                                    LOGI("Found player JS URL: %s", player_js_url);
                                                    if (fetch_and_decrypt_signature(player_js_url, sig, 
                                                                                    decrypted_sig, sizeof(decrypted_sig))) {
                                                        LOGI("Signature decrypted successfully");
                                                        /* Replace encrypted sig with decrypted one in URL */
                                                        char *newUrl = malloc(strlen(unescaped) + strlen(decrypted_sig) + 10);
                                                        if (newUrl) {
                                                            /* Find position of sig= */
                                                            char *sigPos = strstr(unescaped, "sig=");
                                                            if (sigPos) {
                                                                *sigPos = '\0';
                                                                sigPos += 4;
                                                                while (*sigPos && *sigPos != '&') sigPos++;
                                                                snprintf(newUrl, strlen(unescaped) + strlen(decrypted_sig) + strlen(sigPos) + 10,
                                                                        "%ssig=%s%s", unescaped, decrypted_sig, sigPos);
                                                                strncpy(unescaped, newUrl, sizeof(unescaped) - 1);
                                                                unescaped[sizeof(unescaped) - 1] = '\0';
                                                                LOGI("URL updated with decrypted signature");
                                                            }
                                                            free(newUrl);
                                                        }
                                                    } else {
                                                        LOGE("Failed to decrypt signature");
                                                    }
                                                } else {
                                                    LOGE("Could not find player JS URL for signature decryption");
                                                }
                                            }
                                            
                                            // Replace any remaining \u0026 with & (in case unescaping missed some)
                                            char final[2048];
                                            size_t j = 0;
                                            for (size_t i = 0; unescaped[i] && j + 1 < sizeof(final); i++) {
                                                if (unescaped[i] == '\\' && unescaped[i+1] == 'u' &&
                                                    unescaped[i+2] == '0' && unescaped[i+3] == '0' &&
                                                    unescaped[i+4] == '2' && unescaped[i+5] == '6') {
                                                    final[j++] = '&';
                                                    i += 5;
                                                } else {
                                                    final[j++] = unescaped[i];
                                                }
                                            }
                                            final[j] = '\0';
                                            snprintf(outUrl, outLen, "%s", final);
                                            LOGI("Found YouTube audio URL from player response: %s", outUrl);
                                            return true;
                                        } else {
                                            LOGI("Rejected URL (no audio): %s", unescaped);
                                        }
                                    }
                                }
                            }
                            // Try signatureCipher with JavaScript decryption (must be within same object)
                            if (objEnd && objEnd < cursor + 2000) {
                                const char *sigCipher = find_case_insensitive(objStart, "\"signatureCipher\"");
                                if (sigCipher && sigCipher < objEnd) {
                                    char cipher[2048];
                                    if (find_json_value(sigCipher, "signatureCipher", cipher, sizeof(cipher))) {
                                        char unescaped[2048];
                                        json_unescape_repeat(cipher, unescaped, sizeof(unescaped));
                                        url_decode_inplace(unescaped);
                                        char url[2048] = {0};
                                        char sig[512] = {0};
                                        char sp[64] = {0};
                                        if (query_get_param(unescaped, "url", url, sizeof(url))) {
                                            // Validate it's a googlevideo.com URL
                                            if (strstr(url, "googlevideo.com") && !strstr(url, "i.ytimg.com")) {
                                                // Unescape the URL from the query parameter
                                                char urlUnescaped[2048];
                                                json_unescape_repeat(url, urlUnescaped, sizeof(urlUnescaped));
                                                url_decode_inplace(urlUnescaped);
                                                // Accept audio streams or video streams with audio
                                                bool hasAudio = strstr(urlUnescaped, "mime=audio") ||
                                                               strstr(urlUnescaped, "itag=140") || strstr(urlUnescaped, "itag=251") ||
                                                               strstr(urlUnescaped, "itag=250") || strstr(urlUnescaped, "itag=249") ||
                                                               strstr(urlUnescaped, "itag=18") || strstr(urlUnescaped, "itag=22") ||
                                                               strstr(urlUnescaped, "itag=43");
                                                if (hasAudio) {
                                                    query_get_param(unescaped, "sp", sp, sizeof(sp));
                                                    if (!query_get_param(unescaped, "sig", sig, sizeof(sig))) {
                                                        query_get_param(unescaped, "signature", sig, sizeof(sig));
                                                    }
                                                    if (sp[0] == '\0') {
                                                        snprintf(sp, sizeof(sp), "signature");
                                                    }
                                                    if (sig[0] != '\0') {
                                                        /* Try to decrypt the signature using JavaScript engine */
                                                        char decrypted_sig[512] = {0};
                                                        bool decrypted = false;
                                                        
                                                        /* Extract player JS from the page if available */
                                                        char player_js_url[512] = {0};
                                                        if (extract_player_js_url(html, player_js_url, sizeof(player_js_url))) {
                                                            /* Fetch player JS and decrypt signature */
                                                            if (fetch_and_decrypt_signature(player_js_url, sig, 
                                                                                            decrypted_sig, sizeof(decrypted_sig))) {
                                                                decrypted = true;
                                                                LOGI("Signature decrypted using player JS");
                                                            } else {
                                                                /* Fallback: use signature as-is */
                                                                strncpy(decrypted_sig, sig, sizeof(decrypted_sig) - 1);
                                                                decrypted = true;
                                                                LOGI("Using signature as-is (decryption failed)");
                                                            }
                                                        } else {
                                                            /* No player JS found, use signature as-is */
                                                            strncpy(decrypted_sig, sig, sizeof(decrypted_sig) - 1);
                                                            decrypted = true;
                                                            LOGI("No player JS found, using signature as-is");
                                                        }
                                                        
                                                        if (decrypted) {
                                                            // Replace any remaining \u0026 with &
                                                            char final[2048];
                                                            snprintf(final, sizeof(final), "%s&%s=%s", urlUnescaped, sp, decrypted_sig);
                                                            size_t j = 0;
                                                            char final2[2048];
                                                            for (size_t i = 0; final[i] && j + 1 < sizeof(final2); i++) {
                                                                if (final[i] == '\\' && final[i+1] == 'u' &&
                                                                    final[i+2] == '0' && final[i+3] == '0' &&
                                                                    final[i+4] == '2' && final[i+5] == '6') {
                                                                    final2[j++] = '&';
                                                                    i += 5;
                                                                } else {
                                                                    final2[j++] = final[i];
                                                                }
                                                            }
                                                            final2[j] = '\0';
                                                            snprintf(outUrl, outLen, "%s", final2);
                                                            LOGI("Found YouTube audio URL from signatureCipher: %s", outUrl);
                                                            return true;
                                                        }
                                                    }
                                                } else {
                                                    LOGI("Rejected signatureCipher URL (no audio): %s", urlUnescaped);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            cursor++;
                        }
                    }
                }
            }
        }
    }
    
    // Fallback: look for mimeType patterns anywhere in HTML (old method)
    const char *cursor = html;
    while ((cursor = find_case_insensitive(cursor, "\"mimeType\"")) != NULL) {
        const char *colon = strchr(cursor, ':');
        if (!colon) {
            break;
        }
        char mime[256];
        if (!extract_quoted_value(colon, mime, sizeof(mime))) {
            cursor = colon + 1;
            continue;
        }
        if (!strstr(mime, "audio/")) {
            cursor = colon + 1;
            continue;
        }
        const char *limit = colon + 2000;
        const char *urlKey = find_case_insensitive(colon, "\"url\"");
        if (urlKey && urlKey < limit) {
            char raw[2048];
            if (extract_quoted_value(urlKey, raw, sizeof(raw))) {
                char unescaped[2048];
                json_unescape_repeat(raw, unescaped, sizeof(unescaped));
                url_decode_inplace(unescaped);
                // Validate it's a googlevideo.com URL, not a thumbnail
                if (strstr(unescaped, "googlevideo.com") && !strstr(unescaped, "i.ytimg.com")) {
                    snprintf(outUrl, outLen, "%s", unescaped);
                    LOGI("Found YouTube audio URL from mimeType pattern: %s", outUrl);
                    return true;
                } else {
                    LOGI("Rejected URL (not googlevideo.com): %s", unescaped);
                }
            }
        }
        const char *sigKey = find_case_insensitive(colon, "\"signatureCipher\"");
        if (sigKey && sigKey < limit) {
            char raw[2048];
            if (!extract_quoted_value(sigKey, raw, sizeof(raw))) {
                cursor = sigKey + 1;
                continue;
            }
            char unescaped[2048];
            json_unescape_repeat(raw, unescaped, sizeof(unescaped));
            url_decode_inplace(unescaped);
            char url[2048] = {0};
            char sig[512] = {0};
            char sp[64] = {0};
            if (!query_get_param(unescaped, "url", url, sizeof(url))) {
                cursor = sigKey + 1;
                continue;
            }
            // Validate it's a googlevideo.com URL, not a thumbnail
            if (!strstr(url, "googlevideo.com") || strstr(url, "i.ytimg.com")) {
                LOGI("Rejected signatureCipher URL (not googlevideo.com): %s", url);
                cursor = sigKey + 1;
                continue;
            }
            query_get_param(unescaped, "sp", sp, sizeof(sp));
            if (!query_get_param(unescaped, "sig", sig, sizeof(sig)) &&
                !query_get_param(unescaped, "signature", sig, sizeof(sig))) {
                cursor = sigKey + 1;
                continue;
            }
            if (sp[0] == '\0') {
                snprintf(sp, sizeof(sp), "signature");
            }
            snprintf(outUrl, outLen, "%s&%s=%s", url, sp, sig);
            LOGI("Found YouTube audio URL from signatureCipher pattern: %s", outUrl);
            return true;
        }
        cursor = colon + 1;
    }
    
    // Final fallback: look for googlevideo.com URLs with audio parameters
    const char *videoCursor = html;
    while ((videoCursor = find_case_insensitive(videoCursor, "googlevideo.com")) != NULL) {
        // Look backwards to find URL start (up to 500 chars)
        const char *urlStart = videoCursor;
        int lookback = 0;
        while (lookback < 500 && urlStart > html) {
            if (strncmp(urlStart, "https://", 8) == 0) {
                // Found URL start, extract the full URL
                const char *urlEnd = strchr(urlStart, '"');
                if (!urlEnd) {
                    urlEnd = strchr(urlStart, '\'');
                }
                if (!urlEnd) {
                    urlEnd = strchr(urlStart, ' ');
                }
                if (!urlEnd) {
                    urlEnd = strchr(urlStart, '\n');
                }
                if (!urlEnd) {
                    urlEnd = strchr(urlStart, '}');
                }
                if (!urlEnd) {
                    urlEnd = strchr(urlStart, ',');
                }
                if (urlEnd && urlEnd > urlStart) {
                    size_t urlLen = (size_t)(urlEnd - urlStart);
                    if (urlLen < outLen && urlLen > 50) { // Minimum reasonable URL length
                        char tempUrl[2048];
                        if (urlLen < sizeof(tempUrl)) {
                            memcpy(tempUrl, urlStart, urlLen);
                            tempUrl[urlLen] = '\0';
                            // Unescape JSON sequences first
                            char unescaped[2048];
                            json_unescape_repeat(tempUrl, unescaped, sizeof(unescaped));
                            url_decode_inplace(unescaped);
                            // Check if it has audio-related parameters
                            bool hasAudio = strstr(unescaped, "mime=audio") ||
                                           strstr(unescaped, "itag=140") || strstr(unescaped, "itag=251") ||
                                           strstr(unescaped, "itag=250") || strstr(unescaped, "itag=249") ||
                                           strstr(unescaped, "itag=171") || strstr(unescaped, "itag=172") ||
                                           strstr(unescaped, "itag=18") || strstr(unescaped, "itag=22") ||
                                           strstr(unescaped, "itag=43");
                            if (hasAudio) {
                                /* Check if URL has encrypted signature */
                                if (strstr(unescaped, "&sig=") || strstr(unescaped, "?sig=")) {
                                    LOGI("Fallback: URL has encrypted signature, needs JS decryption");
                                    /* Try to find player JS and decrypt */
                                    char player_js_url[512] = {0};
                                    if (extract_player_js_url(html, player_js_url, sizeof(player_js_url))) {
                                        LOGI("Fallback: Found player JS URL: %s", player_js_url);
                                        
                                        /* Fix relative URLs */
                                        char full_player_js_url[512] = {0};
                                        if (strncmp(player_js_url, "//", 2) == 0) {
                                            snprintf(full_player_js_url, sizeof(full_player_js_url), 
                                                    "https:%s", player_js_url);
                                        } else if (player_js_url[0] == '/') {
                                            snprintf(full_player_js_url, sizeof(full_player_js_url),
                                                    "https://www.youtube.com%s", player_js_url);
                                        } else {
                                            strncpy(full_player_js_url, player_js_url, sizeof(full_player_js_url) - 1);
                                        }
                                        LOGI("Fallback: Full player JS URL: %s", full_player_js_url);
                                        
                                        /* Extract sig from URL */
                                        char sig[512] = {0};
                                        const char *sigStart = strstr(unescaped, "sig=");
                                        if (sigStart) {
                                            sigStart += 4;
                                            const char *sigEnd = strchr(sigStart, '&');
                                            size_t sigLen = sigEnd ? (size_t)(sigEnd - sigStart) : strlen(sigStart);
                                            if (sigLen >= sizeof(sig)) sigLen = sizeof(sig) - 1;
                                            memcpy(sig, sigStart, sigLen);
                                            sig[sigLen] = '\0';
                                            LOGI("Fallback: Extracted encrypted sig: %.50s...", sig);
                                            
                                            /* Decrypt signature */
                                            char decrypted_sig[512] = {0};
                                            if (fetch_and_decrypt_signature(full_player_js_url, sig,
                                                                            decrypted_sig, sizeof(decrypted_sig))) {
                                                LOGI("Fallback: Signature decrypted successfully");
                                                /* Replace encrypted sig with decrypted one */
                                                char *newUrl = malloc(strlen(unescaped) + strlen(decrypted_sig) + 10);
                                                if (newUrl) {
                                                    char *sigPos = strstr(unescaped, "sig=");
                                                    if (sigPos) {
                                                        *sigPos = '\0';
                                                        sigPos += 4;
                                                        while (*sigPos && *sigPos != '&') sigPos++;
                                                        snprintf(newUrl, strlen(unescaped) + strlen(decrypted_sig) + strlen(sigPos) + 10,
                                                                "%ssig=%s%s", unescaped, decrypted_sig, sigPos);
                                                        strncpy(unescaped, newUrl, sizeof(unescaped) - 1);
                                                        unescaped[sizeof(unescaped) - 1] = '\0';
                                                        LOGI("Fallback: URL updated with decrypted signature");
                                                    }
                                                    free(newUrl);
                                                }
                                            } else {
                                                LOGE("Fallback: Failed to decrypt signature");
                                            }
                                        }
                                    } else {
                                        LOGE("Fallback: Could not find player JS URL");
                                    }
                                }
                                snprintf(outUrl, outLen, "%s", unescaped);
                                LOGI("Found googlevideo.com audio URL: %s", outUrl);
                                return true;
                            }
                        }
                    }
                }
                break;
            }
            urlStart--;
            lookback++;
        }
        videoCursor++;
    }
    
    return false;
}

static bool extract_src_from_tag(const char *tagStart, char *outUrl, size_t outLen,
                                 char *outMime, size_t mimeLen) {
    const char *srcAttr = find_case_insensitive(tagStart, "src=");
    if (!srcAttr) {
        return false;
    }
    if (!extract_quoted_value(srcAttr, outUrl, outLen)) {
        return false;
    }
    const char *typeAttr = find_case_insensitive(tagStart, "type=");
    if (typeAttr) {
        extract_quoted_value(typeAttr, outMime, mimeLen);
    } else {
        outMime[0] = '\0';
    }
    return true;
}

static bool extract_og_video(const char *html, char *outUrl, size_t outLen) {
    const char *meta = find_case_insensitive(html, "property=\"og:video\"");
    if (!meta) {
        meta = find_case_insensitive(html, "property='og:video'");
    }
    if (!meta) {
        meta = find_case_insensitive(html, "property=\"og:video:secure_url\"");
    }
    if (!meta) {
        return false;
    }
    const char *content = find_case_insensitive(meta, "content=");
    if (!content) {
        return false;
    }
    return extract_quoted_value(content, outUrl, outLen);
}

static bool extract_jsonld_content_url(const char *html, char *outUrl, size_t outLen) {
    const char *contentUrl = find_case_insensitive(html, "\"contentUrl\"");
    if (!contentUrl) {
        return false;
    }
    const char *colon = strchr(contentUrl, ':');
    if (!colon) {
        return false;
    }
    return extract_quoted_value(colon, outUrl, outLen);
}

static bool is_media_url(const char *url) {
    if (!url || url[0] == '\0') {
        return false;
    }
    // Reject YouTube HTML pages
    if (strstr(url, "youtube.com/watch") || strstr(url, "youtube.com/embed") ||
        strstr(url, "youtu.be/") || strstr(url, "youtube.com/v/")) {
        return false;
    }
    // Accept URLs with media extensions or common media patterns
    const char *ext = strrchr(url, '.');
    if (ext) {
        const char *extLower = ext;
        // Check for common media extensions
        if (strstr(extLower, ".mp4") || strstr(extLower, ".webm") ||
            strstr(extLower, ".m4a") || strstr(extLower, ".mp3") ||
            strstr(extLower, ".aac") || strstr(extLower, ".ogg") ||
            strstr(extLower, ".m3u8") || strstr(extLower, ".ts")) {
            return true;
        }
    }
    // Accept URLs that look like media (have query params like itag, range, etc.)
    if (strstr(url, "itag=") || strstr(url, "mime=audio") || strstr(url, "mime=video")) {
        return true;
    }
    // Reject if it looks like an HTML page
    if (strstr(url, ".html") || strstr(url, ".htm") || strstr(url, "/watch") || strstr(url, "/embed")) {
        return false;
    }
    // Default: accept if it has http/https and doesn't look like HTML
    return strstr(url, "http://") || strstr(url, "https://");
}

bool html_extract_media_url(const char *html, HtmlMediaCandidate *outCandidate,
                            char *err, size_t errLen) {
    if (!html || !outCandidate) {
        snprintf(err, errLen, "Invalid HTML input");
        return false;
    }
    outCandidate->url[0] = '\0';
    outCandidate->mime[0] = '\0';

    const char *videoTag = find_case_insensitive(html, "<video");
    if (videoTag) {
        if (extract_src_from_tag(videoTag, outCandidate->url, sizeof(outCandidate->url),
                                 outCandidate->mime, sizeof(outCandidate->mime))) {
            return true;
        }
        const char *sourceTag = find_case_insensitive(videoTag, "<source");
        if (sourceTag &&
            extract_src_from_tag(sourceTag, outCandidate->url, sizeof(outCandidate->url),
                                 outCandidate->mime, sizeof(outCandidate->mime))) {
            return true;
        }
    }

    if (extract_youtube_audio_url(html, outCandidate->url, sizeof(outCandidate->url))) {
        LOGI("Found YouTube audio URL: %s", outCandidate->url);
        outCandidate->mime[0] = '\0';
        return true;
    } else {
        LOGI("YouTube audio URL extraction failed");
    }

    if (extract_og_video(html, outCandidate->url, sizeof(outCandidate->url))) {
        if (is_media_url(outCandidate->url)) {
            LOGI("Found og:video URL: %s", outCandidate->url);
            outCandidate->mime[0] = '\0';
            return true;
        } else {
            LOGI("Rejected og:video URL (not a media file): %s", outCandidate->url);
            outCandidate->url[0] = '\0';
        }
    }

    if (extract_jsonld_content_url(html, outCandidate->url, sizeof(outCandidate->url))) {
        if (is_media_url(outCandidate->url)) {
            LOGI("Found jsonld contentUrl: %s", outCandidate->url);
            outCandidate->mime[0] = '\0';
            return true;
        } else {
            LOGI("Rejected jsonld contentUrl (not a media file): %s", outCandidate->url);
            outCandidate->url[0] = '\0';
        }
    }

    snprintf(err, errLen, "No HTML5 media source found");
    return false;
}
