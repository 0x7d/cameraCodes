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

#ifndef CAMERA_PROPERTIES_H
#define CAMERA_PROPERTIES_H

#include <utils/KeyedVector.h>
#include <utils/String8.h>
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "cutils/properties.h"

namespace android {

#define MAX_CAMERAS_SUPPORTED 1
#define MAX_SIMUL_CAMERAS_SUPPORTED 1
#define MAX_PROP_NAME_LENGTH 50
#define MAX_PROP_VALUE_LENGTH 2048

#define EXIF_MAKE_DEFAULT "default_make"
#define EXIF_MODEL_DEFAULT "default_model"

// Class that handles the Camera Properties
class CameraProperties {
public:
	CameraProperties();
	~CameraProperties();

	// container class passed around for accessing properties
	class Properties {
	public:
		Properties() {
			mProperties = new DefaultKeyedVector<String8, String8>(String8(""));
		}
		~Properties() {
			delete mProperties;
		}
		ssize_t set(const char *prop, const char *value);
		ssize_t set(const char *prop, int value);
		const char* get(const char * prop);
		void dump();

	protected:
		const char* keyAt(unsigned int);
		const char* valueAt(unsigned int);

	private:
		DefaultKeyedVector<String8, String8>* mProperties;

	};

	///Initializes the CameraProperties class
	status_t initialize();
	status_t loadProperties();
	int camerasSupported();
	int getProperties(int cameraIndex, Properties** properties);

private:

	uint32_t mCamerasSupported;
	int mInitialized;
	mutable Mutex mLock;

	Properties *mCameraProps;
};

}
;

#endif //CAMERA_PROPERTIES_H
