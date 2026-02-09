#!/bin/bash
# Script to setup ASAN for Android debugging

# Enable ASAN in Android.mk
sed -i '' 's/^# LOCAL_CFLAGS := -O1/LOCAL_CFLAGS := -O1/' app/src/main/cpp/Android.mk
sed -i '' 's/^# LOCAL_LDLIBS := -landroid/LOCAL_LDLIBS := -landroid/' app/src/main/cpp/Android.mk
sed -i '' 's/^LOCAL_CFLAGS := -O2/# LOCAL_CFLAGS := -O2/' app/src/main/cpp/Android.mk
sed -i '' 's/^LOCAL_LDLIBS := -landroid/# LOCAL_LDLIBS := -landroid/' app/src/main/cpp/Android.mk

echo "ASAN enabled in Android.mk"
echo "Now run: ./rebuild.sh"
