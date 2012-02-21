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

#include "DebugUtils.h"
#include "CameraProperties.h"

#define LOG_TAG "CameraProperties"

namespace android {

static const char* g_camera_adapters[] = {
#ifdef OMAP4_SUPPORT_OMX_CAMERA_ADAPTER
		"libomxcameraadapter.so",
#endif
#ifdef OMAP4_SUPPORT_USB_CAMERA_ADAPTER
		"libusbcameraadapter.so"
#endif
		};

CameraProperties::CameraProperties() :
		mCamerasSupported(0) {
	LOG_FUNCTION_NAME;

	mCamerasSupported = 0;
	mInitialized = 0;

	LOG_FUNCTION_NAME_EXIT;
}

CameraProperties::~CameraProperties() {
	LOG_FUNCTION_NAME;

	LOG_FUNCTION_NAME_EXIT;
}

// Initializes the CameraProperties class
status_t CameraProperties::initialize() {
	LOG_FUNCTION_NAME;

	status_t ret;

	Mutex::Autolock lock(mLock);

	if (mInitialized) {
		return NO_ERROR;
	}

	mCameraProps = new Properties;

	ret = loadProperties();

	mInitialized = 1;

	LOG_FUNCTION_NAME_EXIT;

	return ret;
}

extern "C" int CameraAdapter_Capabilities(
		CameraProperties::Properties* properties_array,
		const unsigned int starting_camera, const unsigned int max_camera);

status_t CameraProperties::loadProperties() {
	LOG_FUNCTION_NAME;

	status_t ret = NO_ERROR;

	// adapter updates capabilities and we update camera count
	mCamerasSupported = CameraAdapter_Capabilities(mCameraProps,
			mCamerasSupported, MAX_CAMERAS_SUPPORTED);

	if ((int) mCamerasSupported < 0) {
		LOGE("error while getting capabilities");
		ret = UNKNOWN_ERROR;
	} else if (mCamerasSupported > MAX_CAMERAS_SUPPORTED) {
		LOGE("returned too many adapaters");
		ret = UNKNOWN_ERROR;
	} else {
		LOGE("num_cameras = %d", mCamerasSupported);
		mCameraProps->dump();
	}

	LOGV("mCamerasSupported = %d", mCamerasSupported);
	LOG_FUNCTION_NAME_EXIT;
	return ret;
}

int CameraProperties::camerasSupported() {
	LOG_FUNCTION_NAME;
	return mCamerasSupported;
}

int CameraProperties::getProperties(int cameraIndex,
		CameraProperties::Properties** properties) {
	LOG_FUNCTION_NAME;

	if ((unsigned int) cameraIndex >= mCamerasSupported) {
		LOG_FUNCTION_NAME_EXIT;
		return -EINVAL;
	}

	*properties = mCameraProps + cameraIndex;

	LOG_FUNCTION_NAME_EXIT;
	return 0;
}

ssize_t CameraProperties::Properties::set(const char *prop, const char *value) {
	if (!prop)
		return -EINVAL;
	if (!value)
		value = "";

	return mProperties->replaceValueFor(String8(prop), String8(value));
}

ssize_t CameraProperties::Properties::set(const char *prop, int value) {
	char s_val[30];

	sprintf(s_val, "%d", value);

	return set(prop, s_val);
}

const char* CameraProperties::Properties::get(const char * prop) {
	String8 value = mProperties->valueFor(String8(prop));
	return value.string();
}

void CameraProperties::Properties::dump() {
	for (size_t i = 0; i < mProperties->size(); i++) {
		LOGE("%s = %s\n", mProperties->keyAt(i).string(),
				mProperties->valueAt(i).string());
	}
}

const char* CameraProperties::Properties::keyAt(unsigned int index) {
	if (index < mProperties->size()) {
		return mProperties->keyAt(index).string();
	}
	return NULL;
}

const char* CameraProperties::Properties::valueAt(unsigned int index) {
	if (index < mProperties->size()) {
		return mProperties->valueAt(index).string();
	}
	return NULL;
}
}
;
