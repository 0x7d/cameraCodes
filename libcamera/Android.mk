# USB Camera Adapter
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

OMAP4_CAMERA_HAL_SRC := \
	MarvellCameraHal.cpp \
	CameraHal.cpp \
	CameraHalUtil.cpp \
	ANativeWindowDisplayAdapter.cpp \
	MemoryManager.cpp \
	CameraProperties.cpp 
	
OMAP4_CAMERA_USB_SRC:= \
	BaseCameraAdapter.cpp \
	V4LCameraAdapter.cpp


LOCAL_SRC_FILES:= \
	$(OMAP4_CAMERA_HAL_SRC) \
	$(OMAP4_CAMERA_USB_SRC) \

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
    libbinder \
    libutils \
    libcutils \
    libtiutils \
    libmm_osal \
    libcamera_client \
    libgui \
    libion \
    libjpeg \
    libexif

LOCAL_CFLAGS := -fno-short-enums -DCOPY_IMAGE_BUFFER

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE:= camera.mv88de3100
LOCAL_MODULE_TAGS:= optional

include $(BUILD_SHARED_LIBRARY)
