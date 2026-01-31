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
- **Current:** 4/11 scripts executing successfully
- **Target:** 9+/11 for signature decryption to work

### Key Blocking Errors

1. **`TypeError: cannot read property 'es5Shimmed' of undefined`** (Script 10)
   - This is the main blocker
   - Happens because code tries to access `.es5Shimmed` on an undefined object
   - Setting `Object.prototype.es5Shimmed` doesn't help because the base object is undefined
   - **Need to identify:** Which object becomes undefined before this check

2. **`SyntaxError: expecting ';'`** (Script 9)
   - Likely malformed JavaScript in the source
   - May need to skip this script or handle syntax errors gracefully

3. **`TypeError: invalid 'in' operand`** (Scripts 7, 8)
   - Code doing `'prop' in someObject` where `someObject` is not an object

4. **`TypeError: cannot read property 'prototype' of undefined`** (Script 2)
   - Some constructor/function is undefined when script tries to extend it

### PROBE System
Implemented a proxy-based logging system to detect what browser APIs scripts are checking:
- Logs all property accesses on `window`, `document`, `navigator`
- Logs when `es5Shimmed` is accessed (via PROBE_SHIM logs)
- Currently NOT catching the es5Shimmed access, meaning it happens on:
  - An object not wrapped by our proxies
  - `undefined` itself (cannot add properties to undefined)
  - An object created dynamically by scripts

## QuickJS Integration

### Browser APIs Currently Implemented
- XMLHttpRequest, HTMLVideoElement
- Event, CustomEvent, KeyboardEvent, MouseEvent, ErrorEvent, PromiseRejectionEvent
- Node, Element, HTMLElement, SVGElement, Document
- HTMLAnchorElement, HTMLScriptElement, HTMLDivElement, HTMLSpanElement
- MutationObserver, IntersectionObserver, ResizeObserver, IntersectionObserverEntry
- DOMParser, DOMImplementation
- TreeWalker, NodeFilter
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

1. **Identify the undefined object causing es5Shimmed error:**
   - Wrap more global objects in proxies (self, top, parent)
   - Add try-catch around script execution with detailed stack traces
   - Log all global object creations

2. **Fix remaining API gaps:**
   - Add missing Element prototype methods
   - Implement proper document.createElementNS
   - Add CSSStyleDeclaration

3. **Alternative approaches:**
   - Try running scripts in non-strict mode
   - Use different QuickJS flags
   - Extract decipher function statically instead of executing scripts
