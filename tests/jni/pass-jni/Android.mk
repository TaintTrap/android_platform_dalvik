LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

# LOCAL_ARM_MODE := thumb
# LOCAL_MODULE_TAGS := optional
# LOCAL_CFLAGS += -mthumb -gdwarf-2 -g3 -O0 -Wall -march=armv7-a -mcpu=cortex-a9
LOCAL_SRC_FILES := $(call all-java-files-under, src)
# LOCAL_REQUIRED_MODULES = pass-jni

# 1. Copy .so to out/target/product/***/obj/lib
$(shell cp $(wildcard $(LOCAL_PATH)/libs/armeabi-v7a/*.so $(TARGET_OUT_INTERMEDIATE_LIBRARIES)))

# 2. Make libxxx to be packed into apk
LOCAL_JNI_SHARED_LIBRARIES := libpass-jni

LOCAL_PACKAGE_NAME := PassJNI

LOCAL_CERTIFICATE := shared

LOCAL_SDK_VERSION := current

include $(BUILD_PACKAGE)
