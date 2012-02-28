LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

CAMERA_HAL_USES:= USB

CAMERA_HAL_SRC := \
	CameraHardwareInterface.cpp \
	CameraHal.cpp \
	CameraHalUtil.cpp \
	AppCallbackNotifier.cpp \
	ANativeWindowDisplayAdapter.cpp \
	CameraProperties.cpp \
	MemoryManager.cpp \
	Encoder_libjpeg.cpp \
	SensorListener.cpp  \

CAMERA_COMMON_SRC:= \
	BaseCameraAdapter.cpp \

CAMERA_USB_SRC:= \
	V4LCameraAdapter.cpp


LOCAL_SRC_FILES:= \
	$(CAMERA_HAL_SRC) \
	$(CAMERA_USB_SRC) \
	$(CAMERA_COMMON_SRC)

LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/include \
    hardware/ti/omap4xxx/include \
    hardware/ti/omap4xxx/libtiutils \
    hardware/ti/omap4xxx/hwc \
    hardware/ti/omap4xxx/tiler \
    hardware/ti/omap4xxx/ion \
    hardware/ti/omap4xxx/domx/omx_core/inc \
    hardware/ti/omap4xxx/domx/mm_osal/inc \
    frameworks/base/include/ui \
    frameworks/base/include/utils \
    frameworks/base/include/media/stagefright \
    frameworks/base/include/media/stagefright/openmax \
    external/jpeg \
    external/jhead

LOCAL_SHARED_LIBRARIES:= \
    libui \
    libgui \
    libbinder \
    libutils \
    libcutils \
    libtiutils \
    libcamera_client \
    libion \
    libjpeg \
    libexif \

LOCAL_CFLAGS := -fno-short-enums

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE:= camera.mv88de3100
LOCAL_MODULE_TAGS:= optional

include $(BUILD_SHARED_LIBRARY)
