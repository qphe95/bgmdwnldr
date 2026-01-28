# BGMDWLDR Vulkan (Minimal)

Minimal NativeActivity + Vulkan hello-triangle using C and the NDK.

## Prereqs
- Android SDK + NDK installed under `$HOME/Android/sdk`
- `shaderc` installed (for `glslc`)
- ADB available (`$ANDROID_SDK_ROOT/platform-tools/adb`)

## Build
```bash
cd /Users/qingpinghe/Documents/bgmdwldr
./gradlew assembleDebug
```

This compiles shaders from `app/src/main/shaders` into `app/src/main/assets`.

## Install to a device
```bash
adb devices
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

If the device doesn’t show up:
- Enable **Developer Options** and **USB debugging**
- Approve the “Allow USB debugging” prompt
- Set USB mode to **File Transfer (MTP)** if prompted

## HUD
The app renders a simple Vulkan text HUD to confirm the text rendering pipeline.
