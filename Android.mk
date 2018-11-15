LOCAL_PATH := $(call my-dir)


include $(CLEAR_VARS)
LOCAL_MODULE := sharestream
LOCAL_SRC_FILES := sharestream.c sharestream.h
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include
LOCAL_CFLAGS := -Wall
LOCAL_LDLIBS := -llog -lz -ldl -lc
LOCAL_SHARED_LIBRARIES := libffmpeg libcutils

LOCAL_MODULE_TAGS := debug

#include $(BUILD_SHARED_LIBRARY)
include $(BUILD_EXECUTABLE)
