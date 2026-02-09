LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := minimalvulkan
MBEDTLS_PATH := $(LOCAL_PATH)/third_party/mbedtls
MBEDTLS_SRC := $(wildcard $(MBEDTLS_PATH)/library/*.c)
MBEDTLS_SRC := $(patsubst $(LOCAL_PATH)/%,%,$(MBEDTLS_SRC))
MBEDTLS_SRC := $(filter-out third_party/mbedtls/library/mbedtls_config.c,$(MBEDTLS_SRC))
TF_PSA_PATH := $(MBEDTLS_PATH)/tf-psa-crypto
TF_PSA_CORE := $(wildcard $(TF_PSA_PATH)/core/*.c)
TF_PSA_CORE := $(patsubst $(LOCAL_PATH)/%,%,$(TF_PSA_CORE))
TF_PSA_DRIVERS := $(wildcard $(TF_PSA_PATH)/drivers/*.c)
TF_PSA_DRIVERS := $(patsubst $(LOCAL_PATH)/%,%,$(TF_PSA_DRIVERS))
TF_PSA_BUILTIN := $(wildcard $(TF_PSA_PATH)/drivers/builtin/*.c)
TF_PSA_BUILTIN := $(patsubst $(LOCAL_PATH)/%,%,$(TF_PSA_BUILTIN))
TF_PSA_BUILTIN_SRC := $(wildcard $(TF_PSA_PATH)/drivers/builtin/src/*.c)
TF_PSA_BUILTIN_SRC := $(patsubst $(LOCAL_PATH)/%,%,$(TF_PSA_BUILTIN_SRC))
QUICKJS_PATH := $(LOCAL_PATH)/third_party/quickjs
CJSON_PATH := $(LOCAL_PATH)/third_party/cjson
LOCAL_SRC_FILES := \
    main.c \
    audio_extract.c \
    html_media_extract.c \
    html_dom.c \
    http_download.c \
    jobs.c \
    media_store.c \
    tls_client.c \
    url_analyzer.c \
    js_quickjs.c \
    browser_stubs.c \
    third_party/quickjs/quickjs.c \
    third_party/quickjs/libregexp.c \
    third_party/quickjs/libunicode.c \
    third_party/quickjs/cutils.c \
    third_party/quickjs/quickjs-libc.c \
    third_party/quickjs/dtoa.c \
    third_party/cjson/cJSON.c \
    $(MBEDTLS_SRC) \
    $(TF_PSA_CORE) \
    $(TF_PSA_DRIVERS) \
    $(TF_PSA_BUILTIN) \
    $(TF_PSA_BUILTIN_SRC)
LOCAL_C_INCLUDES := \
    $(MBEDTLS_PATH)/include \
    $(MBEDTLS_PATH) \
    $(TF_PSA_PATH)/include \
    $(TF_PSA_PATH) \
    $(TF_PSA_PATH)/drivers/builtin/include \
    $(TF_PSA_PATH)/drivers/builtin/src \
    $(TF_PSA_PATH)/core \
    $(QUICKJS_PATH) \
    $(CJSON_PATH)
# ASAN enabled for debugging
LOCAL_CFLAGS := -O1 -g -fsanitize=address -fno-omit-frame-pointer -DCONFIG_VERSION=\"2024-02-14\"
LOCAL_LDFLAGS := -fsanitize=address
LOCAL_LDLIBS := -landroid -llog -lvulkan -lmediandk -lm
LOCAL_STATIC_LIBRARIES := android_native_app_glue
include $(BUILD_SHARED_LIBRARY)

$(call import-module,android/native_app_glue)
