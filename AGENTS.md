# Agent Instructions for bgmdwnldr

## Testing Procedures

### Prerequisites
- Android device connected via ADB
- App installed (via `./rebuild.sh`)

### Proper App Launch Sequence

**IMPORTANT:** Always verify the correct app is running before testing:

```bash
# 1. Kill any running instances
adb shell am force-stop com.bgmdwldr.vulkan
adb shell am force-stop com.oneplus.backuprestore  # Clone Phone often interferes

# 2. Clear logs
adb logcat -c

# 3. Start the app explicitly
adb shell am start -n com.bgmdwldr.vulkan/.MainActivity

# 4. Verify it's running
adb shell pidof com.bgmdwldr.vulkan

# 5. Check focused activity (should show bgmdwldr.vulkan)
adb shell dumpsys activity activities | grep mFocusedApp
```

### Entering Test URL

```bash
# Tap input field (center of text box)
adb shell input tap 540 600

# Clear existing text
adb shell input keyevent --longpress 67 67 67 67 67 67 67 67

# Enter URL
adb shell input text "https://www.youtube.com/watch?v=dQw4w9WgXcQ"

# Submit
adb shell input keyevent 66

# Wait for processing (10-15 seconds)
sleep 12
```

### Viewing Results

```bash
# Get app PID
APP_PID=$(adb shell pidof com.bgmdwldr.vulkan)

# View js_quickjs logs
adb logcat -d --pid=$APP_PID | grep -E "js_quickjs:|HtmlExtract:|Executed|Captured"
```

## Current Status

### Script Execution
- **Current:** 40/40 scripts (100%) executing successfully ✅
- **Previous:** 38/42 scripts executing
- **Target:** Signature decryption working

### Recent Achievements (Feb 2, 2026)

1. **Fixed Data Payload Script Parsing**
   - Removed script sanitization that was corrupting large data payloads
   - Fixed QuickJS string parser to handle null bytes and escape sequences properly
   - Increased MAX_HTML_SIZE from 2MB to 10MB to handle large YouTube pages

2. **Fixed ytsignals Undefined Error**
   - Changed `var ytsignals` to `window.ytsignals` in stubs to ensure global scope
   - Added comprehensive ytsignals stub with getInstance(), whenReady(), get(), set() methods

3. **Improved Script Wrapping**
   - Base.js and player scripts now wrapped in try-catch to allow partial execution
   - Scripts that fail no longer crash the entire execution chain

4. **Removed Problematic Script Filtering**
   - Data payload scripts (ytInitialPlayerResponse, window.ytAtR) are now parsed correctly
   - No more "unexpected end of string" errors

### Remaining Issue: Signature Decryption

**Problem:** All 40 scripts execute successfully but signature decryption still doesn't happen automatically.

**Root Cause:** YouTube's player.js doesn't automatically decrypt URLs when scripts load. It waits for:
- Player initialization events
- Specific API calls from the player UI
- Stream selection triggers

**Next Steps:**
1. Manually call the decipher function after scripts execute
2. Extract the decipher function from base.js and call it with cipher parameters
3. Simulate player initialization to trigger URL processing
4. Hook into the player API to intercept decryption calls

## QuickJS Integration

### Browser APIs Currently Implemented
- XMLHttpRequest, HTMLVideoElement (native C implementations)
- Event, CustomEvent, KeyboardEvent, MouseEvent, ErrorEvent, PromiseRejectionEvent
- Node, Element, HTMLElement, SVGElement, Document
- HTMLAnchorElement, HTMLScriptElement, HTMLDivElement, HTMLSpanElement
- MutationObserver, IntersectionObserver, ResizeObserver, IntersectionObserverEntry
- DOMParser, DOMImplementation
- TreeWalker, NodeFilter (with full traversal methods)
- fetch (mock), matchMedia
- URL, Location, postMessage
- Navigator, Screen, History
- console, performance
- Intl, crypto
- BroadcastChannel, MessageChannel
- requestIdleCallback, cancelIdleCallback
- ShadyDOM, _spf_state (YouTube specific)
- CustomElements
- document.createElementNS, createDocumentFragment, requestStorageAccessFor
- Element.prototype.attachShadow, Node.prototype.getRootNode

### YouTube-Specific Stubs Implemented
- `yt` namespace (scheduler, player, app)
- `ytcfg` configuration object
- `ytsignals` with getInstance(), whenReady(), get(), set()
- `Polymer` and Polymer.Element
- `spf` (Structured Page Fragments)
- `goog` (Closure library namespace)
- `var _yt_player` base.js container

### Known Missing APIs
- Full DOM traversal implementation
- window.getComputedStyle
- CSSStyleDeclaration details
- Web Storage API (localStorage/sessionStorage with actual storage)
- Some Element prototype methods

### JSON Control Character Handling
Modified `quickjs.c` to handle control characters and null bytes in JavaScript string literals:
- `\0` (null byte) escape sequence now properly produces null character
- Backslash at EOF treated as literal backslash (lenient parsing)
- Control characters in strings are preserved (not replaced)

Removed script sanitization in html_extract.c - QuickJS now handles raw script content directly.

## Build System

```bash
# Full rebuild and install
./rebuild.sh

# Monitor logs during development
adb logcat -c && adb logcat -s js_quickjs:* bgmdwldr:* *:S
```

## Next Steps for Debugging

### Signature Decryption
1. **Extract decipher function:**
   - Look for function patterns in base.js (typically named something like `sig`, `decrypt`, or obfuscated)
   - Call it manually with the cipher string from stream URLs

2. **Trigger player initialization:**
   - Simulate player events that trigger stream URL resolution
   - Call internal player methods that process stream data

3. **Alternative approach:**
   - Extract decipher algorithm statically from base.js
   - Implement it in C without needing full JS execution

### Code Architecture Notes
- Native constructors must be set up BEFORE BROWSER_STUBS_JS loads
- BROWSER_STUBS_JS now runs after native setup and can use native implementations
- Video element creation happens in two places:
  1. From HTML parsing (create_video_elements_from_html)
  2. Default video element creation (movie_player)
