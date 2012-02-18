LOCAL_PATH:= $(call my-dir)
##############################
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	avilib.c \
	color.c \
	gui.c \
	luvcview.c \
	utils.c \
	v4l2uvc.c

LOCAL_C_INCLUDES += \
	$(LOCAL_PATH)/include \

LOCAL_MODULE := luvcview
LOCAL_MODULE_TAGS := debug

include $(BUILD_EXECUTABLE)

###############################
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	cameraTest.cpp \

LOCAL_MODULE := cameraTest
LOCAL_MODULE_TAGS := debug

include $(BUILD_EXECUTABLE)

###############################
