LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := silk

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/silk/interface \
    $(LOCAL_PATH)/silk/src \
    $(LOCAL_PATH)/lame

LOCAL_CFLAGS := \
    -O3 \
    -fPIC \
    -ffunction-sections \
    -fdata-sections \
    -fvisibility=hidden \
    -flto \
    -DNO_ASM \
    -D_ANDROID \
    -DEMBEDDED_ARM=0 \
    -DSTDC_HEADERS \
    -DHAVE_STDLIB_H \
    -DHAVE_STRING_H \
    -Wno-shift-negative-value \
    -Wno-tautological-pointer-compare \
    -Wno-absolute-value

LOCAL_LDFLAGS := \
    -Wl,--gc-sections \
    -Wl,--strip-all \
    -flto

SILK_SRC := $(wildcard $(LOCAL_PATH)/silk/src/*.c)
LAME_SRC := $(wildcard $(LOCAL_PATH)/lame/*.c)

LOCAL_SRC_FILES := \
    silk_codec.cpp \
    $(SILK_SRC:$(LOCAL_PATH)/%=%) \
    $(LAME_SRC:$(LOCAL_PATH)/%=%)

LOCAL_LDLIBS := -llog

include $(BUILD_SHARED_LIBRARY)
