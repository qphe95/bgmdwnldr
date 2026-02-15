#!/bin/bash
# Simple crash debugging using logcat and tombstones

APP_PACKAGE="com.bgmdwldr.vulkan"

echo "Simple Crash Debugger for QuickJS Shape Corruption"
echo "==================================================="
echo ""

# Clear logs
adb logcat -c

# Start app
adb shell am start -n $APP_PACKAGE/.MainActivity
echo "App started"
sleep 3

# Trigger crash
echo "Triggering crash..."
adb shell input tap 540 600
sleep 1
adb shell input text "https://www.youtube.com/watch?v=dQw4w9WgXcQ"
sleep 1
adb shell input keyevent 66

echo "Waiting for crash..."
sleep 10

echo ""
echo "=== CRASH ANALYSIS ==="
adb logcat -d | grep -A 30 "F DEBUG" | head -35

echo ""
echo "=== TOMBSTONE (if available) ===
adb shell "ls -la /data/tombstones/" 2>/dev/null | head -5
