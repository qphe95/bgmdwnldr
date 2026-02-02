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
- **Current:** 10/10 scripts executing successfully ✅
- **Previous:** 4/11 scripts executing
- **Target:** Signature decryption working

### Recent Achievements (Feb 1, 2026)

1. **Fixed Script Execution Order**
   - Moved native constructor setup BEFORE BROWSER_STUBS_JS loads
   - Native HTMLVideoElement, XMLHttpRequest, document, window now available when JS stubs execute

2. **Fixed ShadyDOM Polyfill (Script 2)**
   - Added missing DOM APIs: `Element.prototype.attachShadow`, `Node.prototype.getRootNode`
   - Implemented proper `document.createTreeWalker` returning TreeWalker with `firstChild()`, `parentNode()`, etc.
   - Added `document.implementation.createHTMLDocument`
   - Added all ShadyDOM configuration properties (`inUse`, `force`, `noPatch`, `preferPerformance`, etc.)
   - Added `window.top`, `window.parent`, `window.self` self-references

3. **Fixed Video Element Creation**
   - Native HTMLVideoElement constructor properly available to JS
   - `document.createElement('video')` now works correctly
   - No more "not a function" errors

### Remaining Issue: Signature Decryption

**Problem:** All 10 scripts execute successfully but signature decryption doesn't happen automatically.

**Root Cause:** YouTube's player.js doesn't automatically decrypt URLs when scripts load. It waits for:
- Player initialization events
- Specific API calls from the player UI
- Stream selection triggers

**Next Steps:**
1. Manually call the decipher function after scripts execute
2. Extract the decipher function from base.js and call it with cipher parameters
3. Simulate player initialization to trigger URL processing

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

### Known Missing APIs
- Full DOM traversal implementation
- window.getComputedStyle
- CSSStyleDeclaration details
- Web Storage API (localStorage/sessionStorage with actual storage)
- Some Element prototype methods

### JSON Control Character Handling
Modified `quickjs.c` to escape control characters in JSON strings instead of rejecting them. This allows YouTube's player response JSON to be parsed even with raw control characters. The sanitize_json function in html_media_extract.c replaces control characters with spaces for cJSON compatibility.

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
