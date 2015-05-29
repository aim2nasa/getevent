LOCAL_PATH := $(call my-dir)

#Executable
include $(CLEAR_VARS)

LOCAL_MODULE := gevt 
LOCAL_SRC_FILES := main.cpp
LOCAL_LDFLAGS:=-fPIE -pie
LOCAL_C_INCLUDES += /home/skwak/android-ndk-r8e/platforms/android-14/arch-arm/usr/include

include $(BUILD_EXECUTABLE)
