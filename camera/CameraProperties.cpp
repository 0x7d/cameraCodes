/*
 * Copyright (C) Texas Instruments - http://www.ti.com/
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
* @file CameraProperties.cpp
*
* This file maps the CameraHardwareInterface to the Camera interfaces on OMAP4 (mainly OMX).
*
*/

//#include "CameraHal.h"
#include <utils/threads.h>

#include "CameraHal.h"
#include "DebugUtils.h"
#include "CameraProperties.h"

#define CAMERA_ROOT         "CameraRoot"
#define CAMERA_INSTANCE     "CameraInstance"

namespace android {

const char CameraProperties::INVALID[]="invalid-key";
const char CameraProperties::CAMERA_NAME[]="camera-name";
const char CameraProperties::CAMERA_SENSOR_INDEX[]="sensor-index";
const char CameraProperties::ORIENTATION_INDEX[]="orientation";
const char CameraProperties::FACING_INDEX[]="facing";
const char CameraProperties::S3D_SUPPORTED[]="s3d-supported";
const char CameraProperties::SUPPORTED_PREVIEW_SIZES[] = "preview-size-values";
const char CameraProperties::SUPPORTED_PREVIEW_FORMATS[] = "preview-format-values";
const char CameraProperties::SUPPORTED_PREVIEW_FRAME_RATES[] = "preview-frame-rate-values";
const char CameraProperties::SUPPORTED_PICTURE_SIZES[] = "picture-size-values";
const char CameraProperties::SUPPORTED_PICTURE_FORMATS[] = "picture-format-values";
const char CameraProperties::SUPPORTED_THUMBNAIL_SIZES[] = "jpeg-thumbnail-size-values";
const char CameraProperties::SUPPORTED_WHITE_BALANCE[] = "whitebalance-values";
const char CameraProperties::SUPPORTED_EFFECTS[] = "effect-values";
const char CameraProperties::SUPPORTED_ANTIBANDING[] = "antibanding-values";
const char CameraProperties::SUPPORTED_EXPOSURE_MODES[] = "exposure-mode-values";
const char CameraProperties::SUPPORTED_EV_MAX[] = "ev-compensation-max";
const char CameraProperties::SUPPORTED_EV_MIN[] = "ev-compensation-min";
const char CameraProperties::SUPPORTED_EV_STEP[] = "ev-compensation-step";
const char CameraProperties::SUPPORTED_ISO_VALUES[] = "iso-mode-values";
const char CameraProperties::SUPPORTED_SCENE_MODES[] = "scene-mode-values";
const char CameraProperties::SUPPORTED_FLASH_MODES[] = "flash-mode-values";
const char CameraProperties::SUPPORTED_FOCUS_MODES[] = "focus-mode-values";
const char CameraProperties::REQUIRED_PREVIEW_BUFS[] = "required-preview-bufs";
const char CameraProperties::REQUIRED_IMAGE_BUFS[] = "required-image-bufs";
const char CameraProperties::SUPPORTED_ZOOM_RATIOS[] = "zoom-ratios";
const char CameraProperties::SUPPORTED_ZOOM_STAGES[] = "zoom-stages";
const char CameraProperties::SUPPORTED_IPP_MODES[] = "ipp-values";
const char CameraProperties::SMOOTH_ZOOM_SUPPORTED[] = "smooth-zoom-supported";
const char CameraProperties::ZOOM_SUPPORTED[] = "zoom-supported";
const char CameraProperties::PREVIEW_SIZE[] = "preview-size-default";
const char CameraProperties::PREVIEW_FORMAT[] = "preview-format-default";
const char CameraProperties::PREVIEW_FRAME_RATE[] = "preview-frame-rate-default";
const char CameraProperties::ZOOM[] = "zoom-default";
const char CameraProperties::PICTURE_SIZE[] = "picture-size-default";
const char CameraProperties::PICTURE_FORMAT[] = "picture-format-default";
const char CameraProperties::JPEG_THUMBNAIL_SIZE[] = "jpeg-thumbnail-size-default";
const char CameraProperties::WHITEBALANCE[] = "whitebalance-default";
const char CameraProperties::EFFECT[] = "effect-default";
const char CameraProperties::ANTIBANDING[] = "antibanding-default";
const char CameraProperties::EXPOSURE_MODE[] = "exposure-mode-default";
const char CameraProperties::EV_COMPENSATION[] = "ev-compensation-default";
const char CameraProperties::ISO_MODE[] = "iso-mode-default";
const char CameraProperties::FOCUS_MODE[] = "focus-mode-default";
const char CameraProperties::SCENE_MODE[] = "scene-mode-default";
const char CameraProperties::FLASH_MODE[] = "flash-mode-default";
const char CameraProperties::JPEG_QUALITY[] = "jpeg-quality-default";
const char CameraProperties::CONTRAST[] = "contrast-default";
const char CameraProperties::BRIGHTNESS[] = "brightness-default";
const char CameraProperties::SATURATION[] = "saturation-default";
const char CameraProperties::SHARPNESS[] = "sharpness-default";
const char CameraProperties::IPP[] = "ipp-default";
const char CameraProperties::GBCE[] = "gbce-default";
const char CameraProperties::S3D2D_PREVIEW[] = "s3d2d-preview";
const char CameraProperties::S3D2D_PREVIEW_MODES[] = "s3d2d-preview-values";
const char CameraProperties::AUTOCONVERGENCE[] = "auto-convergence";
const char CameraProperties::AUTOCONVERGENCE_MODE[] = "auto-convergence-mode";
const char CameraProperties::MANUALCONVERGENCE_VALUES[] = "manual-convergence-values";
const char CameraProperties::VSTAB[] = "vstab-default";
const char CameraProperties::VSTAB_SUPPORTED[] = "vstab-supported";
const char CameraProperties::REVISION[] = "revision";
const char CameraProperties::FOCAL_LENGTH[] = "focal-length";
const char CameraProperties::HOR_ANGLE[] = "horizontal-angle";
const char CameraProperties::VER_ANGLE[] = "vertical-angle";
const char CameraProperties::FRAMERATE_RANGE[] = "framerate-range-default";
const char CameraProperties::FRAMERATE_RANGE_IMAGE[] = "framerate-range-image-default";
const char CameraProperties::FRAMERATE_RANGE_VIDEO[]="framerate-range-video-default";
const char CameraProperties::FRAMERATE_RANGE_SUPPORTED[]="framerate-range-values";
const char CameraProperties::SENSOR_ORIENTATION[]= "sensor-orientation";
const char CameraProperties::SENSOR_ORIENTATION_VALUES[]= "sensor-orientation-values";
const char CameraProperties::EXIF_MAKE[] = "exif-make";
const char CameraProperties::EXIF_MODEL[] = "exif-model";
const char CameraProperties::JPEG_THUMBNAIL_QUALITY[] = "jpeg-thumbnail-quality-default";
const char CameraProperties::MAX_FOCUS_AREAS[] = "max-focus-areas";
const char CameraProperties::MAX_FD_HW_FACES[] = "max-fd-hw-faces";
const char CameraProperties::MAX_FD_SW_FACES[] = "max-fd-sw-faces";
const char CameraProperties::AUTO_EXPOSURE_LOCK[] = "auto-exposure-lock";
const char CameraProperties::AUTO_EXPOSURE_LOCK_SUPPORTED[] = "auto-exposure-lock-supported";
const char CameraProperties::AUTO_WHITEBALANCE_LOCK[] = "auto-whitebalance-lock";
const char CameraProperties::AUTO_WHITEBALANCE_LOCK_SUPPORTED[] = "auto-whitebalance-lock-supported";
const char CameraProperties::MAX_NUM_METERING_AREAS[] = "max-num-metering-areas";
const char CameraProperties::METERING_AREAS[] = "metering-areas";
const char CameraProperties::VIDEO_SNAPSHOT_SUPPORTED[] = "video-snapshot-supported";
const char CameraProperties::VIDEO_SIZE[] = "video-size";
const char CameraProperties::SUPPORTED_VIDEO_SIZES[] = "video-size-values";
const char CameraProperties::PREFERRED_PREVIEW_SIZE_FOR_VIDEO[] = "preferred-preview-size-for-video";


const char CameraProperties::DEFAULT_VALUE[] = "";
const char CameraProperties::PARAMS_DELIMITER []= ",";


extern "C" int CameraAdapter_Capabilities(CameraProperties::Properties* properties_array,
                                          const unsigned int starting_camera,
                                          const unsigned int max_camera);


CameraProperties::CameraProperties()
{
    LOG_FUNCTION_NAME;

    mCamerasSupported = 0;
    mInitialized = 0;

    LOG_FUNCTION_NAME_EXIT;
}

CameraProperties::~CameraProperties()
{
    LOG_FUNCTION_NAME;

    LOG_FUNCTION_NAME_EXIT;
}


// Initializes the CameraProperties class
status_t CameraProperties::initialize()
{
    LOG_FUNCTION_NAME;

    status_t ret;

    Mutex::Autolock lock(mLock);

    if(mInitialized)
        return NO_ERROR;

    ret = loadProperties();

    mInitialized = 1;

    LOG_FUNCTION_NAME_EXIT;

    return ret;
}

///Loads all the Camera related properties
status_t CameraProperties::loadProperties()
{
    LOG_FUNCTION_NAME;

    status_t ret = NO_ERROR;

    // adapter updates capabilities and we update camera count
    mCamerasSupported = CameraAdapter_Capabilities(mCameraProps, mCamerasSupported, MAX_CAMERAS_SUPPORTED);

    if((int)mCamerasSupported < 0) {
        LOGINFO("error while getting capabilities");
        ret = UNKNOWN_ERROR;
    } else if (mCamerasSupported > MAX_CAMERAS_SUPPORTED) {
        LOGINFO("returned too many adapaters");
        ret = UNKNOWN_ERROR;
    } else {
        LOGINFO("num_cameras = %d", mCamerasSupported);

        for (unsigned int i = 0; i < mCamerasSupported; i++) {
            mCameraProps[i].set(CAMERA_SENSOR_INDEX, i);
			mCameraProps[i].set(CameraParameters::KEY_PREVIEW_FRAME_RATE, 16);
			mCameraProps[i].set(CameraParameters::KEY_JPEG_QUALITY, 95);
			mCameraProps[i].set(CameraParameters::KEY_PICTURE_SIZE, "640x480");
			mCameraProps[i].set(CameraParameters::KEY_PICTURE_FORMAT, "yuv422i-yuyv");
			mCameraProps[i].set(CameraParameters::KEY_PREVIEW_FORMAT, "yuv422i-yuyv");
			mCameraProps[i].set(CameraParameters::KEY_PREVIEW_SIZE, "640x480");
			mCameraProps[i].set(CameraParameters::KEY_FOCUS_MODE, "infinity");
			mCameraProps[i].set(CameraParameters::KEY_SCENE_MODE, "auto");
			mCameraProps[i].set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES, "640x480");
			mCameraProps[i].set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES, "640x480");

			mCameraProps[i].set(CameraProperties::REQUIRED_PREVIEW_BUFS, 8);
			
            mCameraProps[i].dump();
        }
    }

    LOGINFO("mCamerasSupported = %d", mCamerasSupported);
    LOG_FUNCTION_NAME_EXIT;
    return ret;
}

// Returns the properties class for a specific Camera
// Each value is indexed by the CameraProperties::CameraPropertyIndex enum
int CameraProperties::getProperties(int cameraIndex, CameraProperties::Properties** properties)
{
    LOG_FUNCTION_NAME;

    if((unsigned int)cameraIndex >= mCamerasSupported)
    {
        LOG_FUNCTION_NAME_EXIT;
        return -EINVAL;
    }

    *properties = mCameraProps+cameraIndex;

    LOG_FUNCTION_NAME_EXIT;
    return 0;
}

// Returns the number of Cameras found
int CameraProperties::camerasSupported()
{
    LOG_FUNCTION_NAME;
    return mCamerasSupported;
}

ssize_t CameraProperties::Properties::set(const char *prop, const char *value)
{
    if(!prop)
        return -EINVAL;
    if(!value)
        value = DEFAULT_VALUE;

    return mProperties->replaceValueFor(String8(prop), String8(value));
}

ssize_t CameraProperties::Properties::set(const char *prop, int value)
{
    char s_val[30];

    sprintf(s_val, "%d", value);

    return set(prop, s_val);
}

const char* CameraProperties::Properties::get(const char * prop)
{
    String8 value = mProperties->valueFor(String8(prop));
    return value.string();
}

void CameraProperties::Properties::dump()
{
    for (size_t i = 0; i < mProperties->size(); i++)
    {
        LOGINFO("%s = %s\n",
                        mProperties->keyAt(i).string(),
                        mProperties->valueAt(i).string());
    }
}

const char* CameraProperties::Properties::keyAt(unsigned int index)
{
    if(index < mProperties->size())
    {
        return mProperties->keyAt(index).string();
    }
    return NULL;
}

const char* CameraProperties::Properties::valueAt(unsigned int index)
{
    if(index < mProperties->size())
    {
        return mProperties->valueAt(index).string();
    }
    return NULL;
}

};
