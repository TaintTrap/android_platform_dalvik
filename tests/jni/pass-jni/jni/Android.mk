LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

# LOCAL_ARM_MODE := thumb
LOCAL_ARM_MODE := arm
# LOCAL_CFLAGS += -gdwarf-2 -g3 -O0 -fPIC -Wall -Wextra -march=armv7-a -mcpu=cortex-a9 -mfloat-abi=softfp
#LOCAL_CFLAGS += -O2 -fPIC -Wall -Wextra -march=armv7-a -mcpu=cortex-a9 -mfloat-abi=softfp
LOCAL_CFLAGS += -O0 -fPIC -Wall -Wextra -mtune=cortex-a9
#LOCAL_CFLAGS += -pg
#LOCAL_STATIC_LIBRARIES := android-ndk-profiler
# LOCAL_CFLAGS += -mthumb

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE    := pass-jni
LOCAL_SRC_FILES := pass-jni.c
LOCAL_LDLIBS := -llog

include $(BUILD_SHARED_LIBRARY)

# at the end of Android.mk
#$(call import-module,android-ndk-profiler)

