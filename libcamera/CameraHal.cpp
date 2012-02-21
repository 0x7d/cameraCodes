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
 * @file CameraHal.cpp
 *
 * This file maps the Camera Hardware Interface to V4L2.
 *
 */

#define LOG_TAG "CameraHAL"

#include "CameraHal.h"
#include "ANativeWindowDisplayAdapter.h"
#include "CameraProperties.h"
#include <cutils/properties.h>

#include <poll.h>
#include <math.h>

namespace android {

	extern "C" CameraAdapter* CameraAdapter_Factory();

	/*****************************************************************************/

	////Constant definitions and declarations
	////@todo Have a CameraProperties class to store these parameters as constants for every camera
	////       Currently, they are hard-coded
	const int CameraHal::NO_BUFFERS_PREVIEW = MAX_CAMERA_BUFFERS;
	const int CameraHal::NO_BUFFERS_IMAGE_CAPTURE = 2;

	const uint32_t MessageNotifier::EVENT_BIT_FIELD_POSITION = 0;
	const uint32_t MessageNotifier::FRAME_BIT_FIELD_POSITION = 0;
	
	/*-------------Camera Hal Interface Method definitions STARTS here--------------------*/

	/**
	  Callback function to receive orientation events from SensorListener
	  */


	void releaseImageBuffers(void *userData) {
		LOG_FUNCTION_NAME;

		if (NULL != userData) {
			CameraHal *c = reinterpret_cast<CameraHal *>(userData);
			c->freeImageBufs();
		}

		LOG_FUNCTION_NAME_EXIT;
	}


	void endImageCapture(void *userData) {
		LOG_FUNCTION_NAME;

		if (NULL != userData) {
			CameraHal *c = reinterpret_cast<CameraHal *>(userData);
			c->signalEndImageCapture();
		}

		LOG_FUNCTION_NAME_EXIT;
	}

	static void orientation_cb(uint32_t orientation, uint32_t tilt, void* cookie) {
		CameraHal *camera = NULL;

		if (cookie) {
			camera = (CameraHal*) cookie;
			camera->onOrientationEvent(orientation, tilt);
		}

	}

	void CameraHal::onOrientationEvent(uint32_t orientation, uint32_t tilt) {
		LOG_FUNCTION_NAME;

		if (NULL != mCameraAdapter) {
			mCameraAdapter->onOrientationEvent(orientation, tilt);
		}

		LOG_FUNCTION_NAME_EXIT;
	}

	/**
	  @brief Set the notification and data callbacks

	  @param[in] notify_cb Notify callback for notifying the app about events and errors
	  @param[in] data_cb   Buffer callback for sending the preview/raw frames to the app
	  @param[in] data_cb_timestamp Buffer callback for sending the video frames w/ timestamp
	  @param[in] user  Callback cookie
	  @return none

*/
	void CameraHal::setCallbacks(camera_notify_callback notify_cb,
			camera_data_callback data_cb,
			camera_data_timestamp_callback data_cb_timestamp,
			camera_request_memory get_memory, void *user) {
		LOG_FUNCTION_NAME;

		LOG_FUNCTION_NAME_EXIT;
	}

	/**
	  @brief Enable a message, or set of messages.

	  @param[in] msgtype Bitmask of the messages to enable (defined in include/ui/Camera.h)
	  @return none

*/
	void CameraHal::enableMsgType(int32_t msgType) {
		LOG_FUNCTION_NAME;

		if ((msgType & CAMERA_MSG_SHUTTER) && (!mShutterEnabled)) {
			msgType &= ~CAMERA_MSG_SHUTTER;
		}

		// ignoring enable focus message from camera service
		// we will enable internally in autoFocus call
		if (msgType & CAMERA_MSG_FOCUS) {
			msgType &= ~CAMERA_MSG_FOCUS;
		}

		{
			Mutex::Autolock lock(mLock);
			mMsgEnabled |= msgType;
		}

		if (mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) {
			if (mDisplayPaused) {
				LOGE(
						"Preview currently paused...will enable preview callback when restarted");
				msgType &= ~CAMERA_MSG_PREVIEW_FRAME;
			} else {
				LOGE("Enabling Preview Callback");
			}
		} else {
			LOGE("Preview callback not enabled %x", msgType);
		}

		LOG_FUNCTION_NAME_EXIT;
	}

	/**
	  @brief Disable a message, or set of messages.

	  @param[in] msgtype Bitmask of the messages to disable (defined in include/ui/Camera.h)
	  @return none

*/
	void CameraHal::disableMsgType(int32_t msgType) {
		LOG_FUNCTION_NAME;

		{
			Mutex::Autolock lock(mLock);
			mMsgEnabled &= ~msgType;
		}

		if (msgType & CAMERA_MSG_PREVIEW_FRAME) {
			LOGE("Disabling Preview Callback");
		}

		LOG_FUNCTION_NAME_EXIT;
	}

	/**
	  @brief Query whether a message, or a set of messages, is enabled.

	  Note that this is operates as an AND, if any of the messages queried are off, this will
	  return false.

	  @param[in] msgtype Bitmask of the messages to query (defined in include/ui/Camera.h)
	  @return true If all message types are enabled
	  false If any message type

*/
	int CameraHal::msgTypeEnabled(int32_t msgType) {
		LOG_FUNCTION_NAME;
		Mutex::Autolock lock(mLock);
		LOG_FUNCTION_NAME_EXIT;
		return (mMsgEnabled & msgType);
	}

	/**
	  @brief Set the camera parameters.

	  @param[in] params Camera parameters to configure the camera
	  @return NO_ERROR
	  @todo Define error codes

*/
	int CameraHal::setParameters(const char* parameters) {

		LOG_FUNCTION_NAME;

		CameraParameters params;

		String8 str_params(parameters);
		params.unflatten(str_params);

		LOG_FUNCTION_NAME_EXIT;

		return setParameters(params);
	}

	/**
	  @brief Set the camera parameters.

	  @param[in] params Camera parameters to configure the camera
	  @return NO_ERROR
	  @todo Define error codes

*/
	int CameraHal::setParameters(const CameraParameters& params) {

		LOG_FUNCTION_NAME;

		int w, h;
		int w_orig, h_orig;
		int framerate, minframerate;
		int maxFPS, minFPS;
		int error;
		int base;
		const char *valstr = NULL;
		const char *prevFormat;
		char *af_coord;
		TIUTILS::Message msg;
		status_t ret = NO_ERROR;
		// Needed for KEY_RECORDING_HINT
		bool restartPreviewRequired = false;
		bool updateRequired = false;
		CameraParameters oldParams(mParameters.flatten());
		bool videoMode = false;
		char range[MAX_PROP_VALUE_LENGTH];

		{
			Mutex::Autolock lock(mLock);

			///Ensure that preview is not enabled when the below parameters are changed.
			if (!previewEnabled()) {

				LOGE("PreviewFormat %s", params.getPreviewFormat());

				if ((valstr = params.getPreviewFormat()) != NULL) {
					mParameters.setPreviewFormat(valstr);
				} else {
					LOGE("Invalid preview format.Supported");
					return -EINVAL;
				}
			}

			if ((valstr = params.get(CameraParameters::KEY_VIDEO_STABILIZATION))
					!= NULL) {
				// make sure we support vstab...if we don't and application is trying to set
				// vstab then return an error
				ret = -EINVAL;
			}

			params.getPreviewSize(&w, &h);
			if (w == -1 && h == -1) {
				LOGE("Unable to get preview size");
				return -EINVAL;
			}

			int oldWidth, oldHeight;
			mParameters.getPreviewSize(&oldWidth, &oldHeight);
			mParameters.setPreviewSize(w, h);
			mVideoWidth = w;
			mVideoHeight = h;

			if ((oldWidth != w) || (oldHeight != h)) {
				restartPreviewRequired |= true;
			}

			LOGE("PreviewResolution by App %d x %d", w, h);

			// Handle RECORDING_HINT to Set/Reset Video Mode Parameters
			valstr = params.get(CameraParameters::KEY_RECORDING_HINT);
			if (valstr != NULL) {
				if (strcmp(valstr, CameraParameters::TRUE) == 0) {
					LOGE("Recording Hint is set to %s", valstr);
					mParameters.set(CameraParameters::KEY_RECORDING_HINT, valstr);
					videoMode = true;
					int w, h;

					params.getPreviewSize(&w, &h);
					LOGE("%s Preview Width=%d Height=%d\n", __FUNCTION__, w, h);
					//HACK FOR MMS
					mVideoWidth = w;
					mVideoHeight = h;
					LOGE("%s Video Width=%d Height=%d\n", __FUNCTION__, mVideoWidth,
							mVideoHeight);

					setPreferredPreviewRes(w, h);
					mParameters.getPreviewSize(&w, &h);
					LOGE("%s Preview Width=%d Height=%d\n", __FUNCTION__, w, h);
					//Avoid restarting preview for MMS HACK
					if ((w != mVideoWidth) && (h != mVideoHeight)) {
						restartPreviewRequired = false;
					}

					restartPreviewRequired |= setVideoModeParameters(params);
				} else if (strcmp(valstr, CameraParameters::FALSE) == 0) {
					LOGE("Recording Hint is set to %s", valstr);
					mParameters.set(CameraParameters::KEY_RECORDING_HINT, valstr);
					restartPreviewRequired |= resetVideoModeParameters();
					params.getPreviewSize(&mVideoWidth, &mVideoHeight);
				} else {
					LOGE("Invalid RECORDING_HINT");
					return -EINVAL;
				}
			} else {
				// This check is required in following case.
				// If VideoRecording activity sets KEY_RECORDING_HINT to TRUE and
				// ImageCapture activity doesnot set KEY_RECORDING_HINT to FALSE (i.e. simply NULL),
				// then Video Mode parameters may remain present in ImageCapture activity as well.
				LOGE("Recording Hint is set to NULL");
				mParameters.set(CameraParameters::KEY_RECORDING_HINT, "");
				restartPreviewRequired |= resetVideoModeParameters();
				params.getPreviewSize(&mVideoWidth, &mVideoHeight);
			}

			if ((valstr = params.get(CameraParameters::KEY_FOCUS_MODE)) != NULL) {
				mParameters.set(CameraParameters::KEY_FOCUS_MODE, valstr);
			}

			///Below parameters can be changed when the preview is running
			if ((valstr = params.getPictureFormat()) != NULL) {
				mParameters.setPictureFormat(valstr);
			}

			params.getPictureSize(&w, &h);
			mParameters.setPictureSize(w, h);

			framerate = params.getPreviewFrameRate();
			valstr = params.get(CameraParameters::KEY_PREVIEW_FPS_RANGE);
			LOGE("FRAMERATE %d", framerate);

			LOGE("SET FRAMERATE %d", 16000);
			mParameters.setPreviewFrameRate(16000);
			mParameters.set(CameraParameters::KEY_PREVIEW_FPS_RANGE, 16);

			if ((valstr = params.get(CameraParameters::KEY_WHITE_BALANCE))
					!= NULL) {
				LOGE("White balance set %s", valstr);
				mParameters.set(CameraParameters::KEY_WHITE_BALANCE, valstr);
			}

			if ((valstr = params.get(CameraParameters::KEY_ANTIBANDING)) != NULL) {
				LOGE("Antibanding set %s", valstr);
				mParameters.set(CameraParameters::KEY_ANTIBANDING, valstr);
			}

			if ((valstr = params.get(CameraParameters::KEY_FOCUS_AREAS)) != NULL) {
				LOGE("Focus areas position set %s",
						params.get(CameraParameters::KEY_FOCUS_AREAS));
				mParameters.set(CameraParameters::KEY_FOCUS_AREAS, valstr);
			}

			if ((valstr = params.get(CameraParameters::KEY_EXPOSURE_COMPENSATION))
					!= NULL) {
				LOGE("Exposure compensation set %s",
						params.get(CameraParameters::KEY_EXPOSURE_COMPENSATION));
				mParameters.set(CameraParameters::KEY_EXPOSURE_COMPENSATION,
						valstr);
			}

			if ((valstr = params.get(CameraParameters::KEY_SCENE_MODE)) != NULL) {
				LOGE("Scene mode set %s", valstr);
				doesSetParameterNeedUpdate(valstr,
						mParameters.get(CameraParameters::KEY_SCENE_MODE),
						updateRequired);
				mParameters.set(CameraParameters::KEY_SCENE_MODE, valstr);
			}

			if ((valstr = params.get(CameraParameters::KEY_FLASH_MODE)) != NULL) {
				LOGE("Flash mode set %s", valstr);
				mParameters.set(CameraParameters::KEY_FLASH_MODE, valstr);
			}

			if ((valstr = params.get(CameraParameters::KEY_EFFECT)) != NULL) {
				mParameters.set(CameraParameters::KEY_EFFECT, valstr);
			}

			if (((valstr = params.get(CameraParameters::KEY_ROTATION)) != NULL)
					&& (params.getInt(CameraParameters::KEY_ROTATION) >= 0)) {
				LOGE("Rotation set %s", params.get(CameraParameters::KEY_ROTATION));
				mParameters.set(CameraParameters::KEY_ROTATION, valstr);
			}

			if (((valstr = params.get(CameraParameters::KEY_JPEG_QUALITY)) != NULL)
					&& (params.getInt(CameraParameters::KEY_JPEG_QUALITY) >= 0)) {
				LOGE("Jpeg quality set %s",
						params.get(CameraParameters::KEY_JPEG_QUALITY));
				mParameters.set(CameraParameters::KEY_JPEG_QUALITY, valstr);
			}

			if (((valstr = params.get(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH))
						!= NULL)
					&& (params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH)
						>= 0)) {
				LOGE("Thumbnail width set %s",
						params.get(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH));
				mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH, valstr);
			}

			if (((valstr = params.get(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT))
						!= NULL)
					&& (params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT)
						>= 0)) {
				LOGE("Thumbnail width set %s",
						params.get(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT));
				mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT,
						valstr);
			}

			if (((valstr = params.get(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY))
						!= NULL)
					&& (params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY)
						>= 0)) {
				LOGE("Thumbnail quality set %s",
						params.get(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY));
				mParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY,
						valstr);
			}

			if ((valstr = params.get(CameraParameters::KEY_GPS_LATITUDE)) != NULL) {
				LOGE("GPS latitude set %s",
						params.get(CameraParameters::KEY_GPS_LATITUDE));
				mParameters.set(CameraParameters::KEY_GPS_LATITUDE, valstr);
			} else {
				mParameters.remove(CameraParameters::KEY_GPS_LATITUDE);
			}

			if ((valstr = params.get(CameraParameters::KEY_GPS_LONGITUDE))
					!= NULL) {
				LOGE("GPS longitude set %s",
						params.get(CameraParameters::KEY_GPS_LONGITUDE));
				mParameters.set(CameraParameters::KEY_GPS_LONGITUDE, valstr);
			} else {
				mParameters.remove(CameraParameters::KEY_GPS_LONGITUDE);
			}

			if ((valstr = params.get(CameraParameters::KEY_GPS_ALTITUDE)) != NULL) {
				LOGE("GPS altitude set %s",
						params.get(CameraParameters::KEY_GPS_ALTITUDE));
				mParameters.set(CameraParameters::KEY_GPS_ALTITUDE, valstr);
			} else {
				mParameters.remove(CameraParameters::KEY_GPS_ALTITUDE);
			}

			if ((valstr = params.get(CameraParameters::KEY_GPS_TIMESTAMP))
					!= NULL) {
				LOGE("GPS timestamp set %s",
						params.get(CameraParameters::KEY_GPS_TIMESTAMP));
				mParameters.set(CameraParameters::KEY_GPS_TIMESTAMP, valstr);
			} else {
				mParameters.remove(CameraParameters::KEY_GPS_TIMESTAMP);
			}

			if ((valstr = params.get(CameraParameters::KEY_GPS_PROCESSING_METHOD))
					!= NULL) {
				LOGE("GPS processing method set %s",
						params.get(CameraParameters::KEY_GPS_PROCESSING_METHOD));
				mParameters.set(CameraParameters::KEY_GPS_PROCESSING_METHOD,
						valstr);
			} else {
				mParameters.remove(CameraParameters::KEY_GPS_PROCESSING_METHOD);
			}

			if ((valstr = params.get(CameraParameters::KEY_ZOOM)) != NULL) {
				if ((params.getInt(CameraParameters::KEY_ZOOM) >= 0)
						&& (params.getInt(CameraParameters::KEY_ZOOM)
							<= mMaxZoomSupported)) {
					LOGE("Zoom set %s", valstr);
					doesSetParameterNeedUpdate(valstr,
							mParameters.get(CameraParameters::KEY_ZOOM),
							updateRequired);
					mParameters.set(CameraParameters::KEY_ZOOM, valstr);
				} else {
					LOGE("ERROR: Invalid Zoom: %s", valstr);
					ret = -EINVAL;
				}
			}

			if ((valstr = params.get(CameraParameters::KEY_AUTO_EXPOSURE_LOCK))
					!= NULL) {
				LOGE("Auto Exposure Lock set %s",
						params.get(CameraParameters::KEY_AUTO_EXPOSURE_LOCK));
				doesSetParameterNeedUpdate(valstr,
						mParameters.get(CameraParameters::KEY_AUTO_EXPOSURE_LOCK),
						updateRequired);
				mParameters.set(CameraParameters::KEY_AUTO_EXPOSURE_LOCK, valstr);
			}

			if ((valstr = params.get(CameraParameters::KEY_AUTO_WHITEBALANCE_LOCK))
					!= NULL) {
				LOGE("Auto WhiteBalance Lock set %s",
						params.get(CameraParameters::KEY_AUTO_WHITEBALANCE_LOCK));
				doesSetParameterNeedUpdate(
						valstr,
						mParameters.get(
							CameraParameters::KEY_AUTO_WHITEBALANCE_LOCK),
						updateRequired);
				mParameters.set(CameraParameters::KEY_AUTO_WHITEBALANCE_LOCK,
						valstr);
			}
			if ((valstr = params.get(CameraParameters::KEY_METERING_AREAS))
					!= NULL) {
				LOGE("Metering areas position set %s",
						params.get(CameraParameters::KEY_METERING_AREAS));
				mParameters.set(CameraParameters::KEY_METERING_AREAS, valstr);
			}

			CameraParameters adapterParams = mParameters;

			// Only send parameters to adapter if preview is already
			// enabled or doesSetParameterNeedUpdate says so. Initial setParameters to camera adapter,
			// will be called in startPreview()
			// TODO(XXX): Need to identify other parameters that need update from camera adapter
			if ((NULL != mCameraAdapter) && (mPreviewEnabled || updateRequired)) {
				//ret |= mCameraAdapter->setParameters(adapterParams);
			}
		}

		//On fail restore old parameters
		if (NO_ERROR != ret) {
			mParameters.unflatten(oldParams.flatten());
		}

		// Restart Preview if needed by KEY_RECODING_HINT only if preview is already running.
		// If preview is not started yet, Video Mode parameters will take effect on next startPreview()
		if (restartPreviewRequired && previewEnabled() && !mRecordingEnabled) {
			LOGE("Restarting Preview");
			ret = restartPreview();
		} else if (restartPreviewRequired && !previewEnabled() && mDisplayPaused
				&& !mRecordingEnabled) {
			LOGE("Stopping Preview");
			forceStopPreview();
		}

		if (ret != NO_ERROR) {
			LOGE("Failed to restart Preview");
			return ret;
		}

		LOG_FUNCTION_NAME_EXIT;

		return ret;
	}

	status_t CameraHal::allocPreviewBufs(int width, int height,
			const char* previewFormat, unsigned int buffercount,
			unsigned int &max_queueable) {
		status_t ret = NO_ERROR;

		LOG_FUNCTION_NAME;

		if (mDisplayAdapter.get() == NULL) {
			// Memory allocation of preview buffers is now placed in gralloc
			// CameraHal should not allocate preview buffers without DisplayAdapter
			return NO_MEMORY;
		}

		if (!mPreviewBufs) {
			///@todo Pluralise the name of this method to allocateBuffers
			mPreviewLength = 0;
			mPreviewBufs = (int32_t *) mDisplayAdapter->allocateBuffer(width,
					height, previewFormat, mPreviewLength, buffercount);

			if (NULL == mPreviewBufs) {
				LOGE("Couldn't allocate preview buffers");
				return NO_MEMORY;
			}

			mPreviewOffsets = (uint32_t *) mDisplayAdapter->getOffsets();
			if (NULL == mPreviewOffsets) {
				LOGE("Buffer mapping failed");
				return BAD_VALUE;
			}

			mPreviewFd = mDisplayAdapter->getFd();
			if (-1 == mPreviewFd) {
				LOGE("Invalid handle");
				return BAD_VALUE;
			}

			mBufProvider = (BufferProvider*) mDisplayAdapter.get();

			ret = mDisplayAdapter->maxQueueableBuffers(max_queueable);
			if (ret != NO_ERROR) {
				return ret;
			}

		}

		LOG_FUNCTION_NAME_EXIT;

		return ret;

	}

	status_t CameraHal::freePreviewBufs() {
		status_t ret = NO_ERROR;
		LOG_FUNCTION_NAME;

		LOGE("mPreviewBufs = 0x%x", (unsigned int) mPreviewBufs);
		if (mPreviewBufs) {
			///@todo Pluralise the name of this method to freeBuffers
			ret = mBufProvider->freeBuffer(mPreviewBufs);
			mPreviewBufs = NULL;
			LOG_FUNCTION_NAME_EXIT;
			return ret;
		}
		LOG_FUNCTION_NAME_EXIT;
		return ret;
	}

	status_t CameraHal::allocPreviewDataBufs(size_t size, size_t bufferCount) {
		status_t ret = NO_ERROR;
		int bytes;

		LOG_FUNCTION_NAME;

		bytes = size;

		if (NO_ERROR == ret) {
			if (NULL != mPreviewDataBufs) {
				ret = freePreviewDataBufs();
			}
		}

		if (NO_ERROR == ret) {
			bytes = ((bytes + 4095) / 4096) * 4096;
			mPreviewDataBufs = (int32_t *) mMemoryManager->allocateBuffer(0, 0,
					NULL, bytes, bufferCount);

			LOGE("Size of Preview data buffer = %d", bytes);
			if (NULL == mPreviewDataBufs) {
				LOGE("Couldn't allocate image buffers using memory manager");
				ret = -NO_MEMORY;
			} else {
				bytes = size;
			}
		}

		if (NO_ERROR == ret) {
			mPreviewDataFd = mMemoryManager->getFd();
			mPreviewDataLength = bytes;
			mPreviewDataOffsets = mMemoryManager->getOffsets();
		} else {
			mPreviewDataFd = -1;
			mPreviewDataLength = 0;
			mPreviewDataOffsets = NULL;
		}

		LOG_FUNCTION_NAME;

		return ret;
	}

	status_t CameraHal::freePreviewDataBufs() {
		status_t ret = NO_ERROR;

		LOG_FUNCTION_NAME;

		if (NO_ERROR == ret) {

			if (NULL != mPreviewDataBufs) {

				///@todo Pluralise the name of this method to freeBuffers
				ret = mMemoryManager->freeBuffer(mPreviewDataBufs);
				mPreviewDataBufs = NULL;

			}
		}

		LOG_FUNCTION_NAME_EXIT;

		return ret;
	}

	status_t CameraHal::allocImageBufs(unsigned int width, unsigned int height,
			size_t size, const char* previewFormat, unsigned int bufferCount) {
		status_t ret = NO_ERROR;
		int bytes;

		LOG_FUNCTION_NAME;

		bytes = size;

		// allocate image buffers only if not already allocated
		if (NULL != mImageBufs) {
			return NO_ERROR;
		}

		if (NO_ERROR == ret) {
			bytes = ((bytes + 4095) / 4096) * 4096;
			mImageBufs = (int32_t *) mMemoryManager->allocateBuffer(0, 0,
					previewFormat, bytes, bufferCount);

			LOGE("Size of Image cap buffer = %d", bytes);
			if (NULL == mImageBufs) {
				LOGE("Couldn't allocate image buffers using memory manager");
				ret = -NO_MEMORY;
			} else {
				bytes = size;
			}
		}

		if (NO_ERROR == ret) {
			mImageFd = mMemoryManager->getFd();
			mImageLength = bytes;
			mImageOffsets = mMemoryManager->getOffsets();
		} else {
			mImageFd = -1;
			mImageLength = 0;
			mImageOffsets = NULL;
		}

		LOG_FUNCTION_NAME;

		return ret;
	}

	status_t CameraHal::allocVideoBufs(uint32_t width, uint32_t height,
			uint32_t bufferCount) {
		status_t ret = NO_ERROR;
		LOG_FUNCTION_NAME;

		if (NULL != mVideoBufs) {
			ret = freeVideoBufs(mVideoBufs);
			mVideoBufs = NULL;
		}

		if (NO_ERROR == ret) {
			int32_t stride;
			buffer_handle_t *bufsArr = new buffer_handle_t[bufferCount];

			if (bufsArr != NULL) {
				for (int i = 0; i < bufferCount; i++) {
					GraphicBufferAllocator &GrallocAlloc =
						GraphicBufferAllocator::get();
					buffer_handle_t buf;
					ret = GrallocAlloc.alloc(width, height, HAL_PIXEL_FORMAT_NV12,
							CAMHAL_GRALLOC_USAGE, &buf, &stride);
					if (ret != NO_ERROR) {
						LOGE("Couldn't allocate video buffers using Gralloc");
						ret = -NO_MEMORY;
						for (int j = 0; j < i; j++) {
							buf = (buffer_handle_t) bufsArr[j];
							LOGE("Freeing Gralloc Buffer 0x%x", buf);
							GrallocAlloc.free(buf);
						}
						delete[] bufsArr;
						goto exit;
					}
					bufsArr[i] = buf;
					LOGE("*** Gralloc Handle =0x%x ***", buf);
				}

				mVideoBufs = (int32_t *) bufsArr;
			} else {
				LOGE("Couldn't allocate video buffers ");
				ret = -NO_MEMORY;
			}
		}

exit: LOG_FUNCTION_NAME;

      return ret;
	}

	
	status_t CameraHal::signalEndImageCapture() {
		status_t ret = NO_ERROR;
		int w, h;
		CameraParameters adapterParams = mParameters;
		Mutex::Autolock lock(mLock);

		LOG_FUNCTION_NAME;

		if (mBracketingRunning) {
			stopImageBracketing();
		} else {
			mCameraAdapter->sendCommand(CameraAdapter::CAMERA_STOP_IMAGE_CAPTURE);
		}

		LOG_FUNCTION_NAME_EXIT;

		return ret;
	}

	status_t CameraHal::freeImageBufs() {
		status_t ret = NO_ERROR;

		LOG_FUNCTION_NAME;

		if (NO_ERROR == ret) {

			if (NULL != mImageBufs) {

				///@todo Pluralise the name of this method to freeBuffers
				ret = mMemoryManager->freeBuffer(mImageBufs);
				mImageBufs = NULL;

			} else {
				ret = -EINVAL;
			}

		}

		LOG_FUNCTION_NAME_EXIT;

		return ret;
	}

	status_t CameraHal::freeVideoBufs(void *bufs) {
		status_t ret = NO_ERROR;

		LOG_FUNCTION_NAME;

		buffer_handle_t *pBuf = (buffer_handle_t*) bufs;
		int count = 4;
		if (pBuf == NULL) {
			LOGE("NULL pointer passed to freeVideoBuffer");
			LOG_FUNCTION_NAME_EXIT;
			return BAD_VALUE;
		}

		GraphicBufferAllocator &GrallocAlloc = GraphicBufferAllocator::get();

		for (int i = 0; i < count; i++) {
			buffer_handle_t ptr = *pBuf++;
			LOGE("Free Video Gralloc Handle 0x%x", ptr);
			GrallocAlloc.free(ptr);
		}

		LOG_FUNCTION_NAME_EXIT;

		return ret;
	}

	/**
	  @brief Start preview mode.

	  @param none
	  @return NO_ERROR Camera switched to VF mode
	  @todo Update function header with the different errors that are possible

*/
	status_t CameraHal::startPreview() {

		status_t ret = NO_ERROR;
		CameraAdapter::BuffersDescriptor desc;
		CameraFrame frame;
		const char *valstr = NULL;
		unsigned int required_buffer_count;
		unsigned int max_queueble_buffers;

		LOG_FUNCTION_NAME;

		if (mPreviewEnabled) {
			LOGE("Preview already running");
			LOG_FUNCTION_NAME_EXIT;
			return ALREADY_EXISTS;
		}

		if (NULL != mCameraAdapter) {
			ret = mCameraAdapter->setParameters(mParameters);
		}

		if ((mPreviewStartInProgress == false) && (mDisplayPaused == false)) {
			ret = mCameraAdapter->sendCommand(
					CameraAdapter::CAMERA_QUERY_RESOLUTION_PREVIEW, (int) &frame);
			if (NO_ERROR != ret) {
				LOGE("Error: CAMERA_QUERY_RESOLUTION_PREVIEW %d", ret);
				return ret;
			}

			///Update the current preview width and height
			mPreviewWidth = frame.mWidth;
			mPreviewHeight = frame.mHeight;
		}

		///If we don't have the preview callback enabled and display adapter,
		if (!mSetPreviewWindowCalled || (mDisplayAdapter.get() == NULL)) {
			LOGE("Preview not started. Preview in progress flag set");
			mPreviewStartInProgress = true;
			ret = mCameraAdapter->sendCommand(
					CameraAdapter::CAMERA_SWITCH_TO_EXECUTING);
			if (NO_ERROR != ret) {
				LOGE("Error: CAMERA_SWITCH_TO_EXECUTING %d", ret);
				return ret;
			}
			return NO_ERROR;
		}

		if ((mDisplayAdapter.get() != NULL) && (!mPreviewEnabled)
				&& (mDisplayPaused)) {
			LOGE("Preview is in paused state");

			mDisplayPaused = false;
			mPreviewEnabled = true;
			if (NO_ERROR == ret) {
				ret = mDisplayAdapter->pauseDisplay(mDisplayPaused);

				if (NO_ERROR != ret) {
					LOGE("Display adapter resume failed %x", ret);
				}
			}
			return ret;
		}

		required_buffer_count = 4;
		///Allocate the preview buffers
		ret = allocPreviewBufs(mPreviewWidth, mPreviewHeight,
				mParameters.getPreviewFormat(), required_buffer_count,
				max_queueble_buffers);

		if (NO_ERROR != ret) {
			LOGE("Couldn't allocate buffers for Preview");
			goto error;
		}

		if (mMeasurementEnabled) {

			ret = mCameraAdapter->sendCommand(
					CameraAdapter::CAMERA_QUERY_BUFFER_SIZE_PREVIEW_DATA,
					(int) &frame, required_buffer_count);
			if (NO_ERROR != ret) {
				return ret;
			}

			///Allocate the preview data buffers
			ret = allocPreviewDataBufs(frame.mLength, required_buffer_count);
			if (NO_ERROR != ret) {
				LOGE("Couldn't allocate preview data buffers");
				goto error;
			}

			if (NO_ERROR == ret) {
				desc.mBuffers = mPreviewDataBufs;
				desc.mOffsets = mPreviewDataOffsets;
				desc.mFd = mPreviewDataFd;
				desc.mLength = mPreviewDataLength;
				desc.mCount = (size_t) required_buffer_count;
				desc.mMaxQueueable = (size_t) required_buffer_count;

				mCameraAdapter->sendCommand(
						CameraAdapter::CAMERA_USE_BUFFERS_PREVIEW_DATA,
						(int) &desc);
			}

		}

		///Pass the buffers to Camera Adapter
		desc.mBuffers = mPreviewBufs;
		desc.mOffsets = mPreviewOffsets;
		desc.mFd = mPreviewFd;
		desc.mLength = mPreviewLength;
		desc.mCount = (size_t) required_buffer_count;
		desc.mMaxQueueable = (size_t) max_queueble_buffers;

		ret = mCameraAdapter->sendCommand(CameraAdapter::CAMERA_USE_BUFFERS_PREVIEW,
				(int) &desc);

		if (NO_ERROR != ret) {
			LOGE("Failed to register preview buffers: 0x%x", ret);
			freePreviewBufs();
			return ret;
		}
		if (ALREADY_EXISTS == ret) {
			//Already running, do nothing
			LOGE("AppCallbackNotifier already running");
			ret = NO_ERROR;
		} else if (NO_ERROR == ret) {
			LOGE("Started AppCallbackNotifier..");
		} else {
			LOGE("Couldn't start AppCallbackNotifier");
			goto error;
		}

		///Enable the display adapter if present, actual overlay enable happens when we post the buffer
		if (mDisplayAdapter.get() != NULL) {
			LOGE("Enabling display");
			bool isS3d = false;
			DisplayAdapter::S3DParameters s3dParams;
			int width, height;
			mParameters.getPreviewSize(&width, &height);

			ret = mDisplayAdapter->enableDisplay(width, height, NULL,
					isS3d ? &s3dParams : NULL);
			if (ret != NO_ERROR) {
				LOGE("Couldn't enable display");
				goto error;
			}

		}

		///Send START_PREVIEW command to adapter
		LOGE("Starting CameraAdapter preview mode");

		ret = mCameraAdapter->sendCommand(CameraAdapter::CAMERA_START_PREVIEW);

		if (ret != NO_ERROR) {
			LOGE("Couldn't start preview w/ CameraAdapter");
			goto error;
		}
		LOGE("Started preview");

		mPreviewEnabled = true;
		mPreviewStartInProgress = false;
		return ret;

error:

		LOGE("Performing cleanup after error");

		//Do all the cleanup
		freePreviewBufs();
		mCameraAdapter->sendCommand(CameraAdapter::CAMERA_STOP_PREVIEW);
		if (mDisplayAdapter.get() != NULL) {
			mDisplayAdapter->disableDisplay(false);
		}
		mPreviewStartInProgress = false;
		mPreviewEnabled = false;
		LOG_FUNCTION_NAME_EXIT;

		return ret;
	}

	/**
	  @brief Sets ANativeWindow object.

	  Preview buffers provided to CameraHal via this object. DisplayAdapter will be interfacing with it
	  to render buffers to display.

	  @param[in] window The ANativeWindow object created by Surface flinger
	  @return NO_ERROR If the ANativeWindow object passes validation criteria
	  @todo Define validation criteria for ANativeWindow object. Define error codes for scenarios

*/
	status_t CameraHal::setPreviewWindow(struct preview_stream_ops *window) {
		LOG_FUNCTION_NAME;
		
		status_t ret = NO_ERROR;
		CameraAdapter::BuffersDescriptor desc;

		mSetPreviewWindowCalled = true;
		if (!window) {
			LOGE("NULL window passed, destroying display adapter");
			if (mDisplayAdapter.get() != NULL) {
				mDisplayAdapter.clear();
				mSetPreviewWindowCalled = false;
			}
			return NO_ERROR;
		} else if (mDisplayAdapter.get() == NULL) {
			mDisplayAdapter = new ANativeWindowDisplayAdapter();
			ret = NO_ERROR;
			if (!mDisplayAdapter.get()
					|| ((ret = mDisplayAdapter->initialize()) != NO_ERROR)) {
				if (ret != NO_ERROR) {
					mDisplayAdapter.clear();
					LOGE("DisplayAdapter initialize failed");
					LOG_FUNCTION_NAME_EXIT;
					return ret;
				} else {
					LOGE("Couldn't create DisplayAdapter");
					LOG_FUNCTION_NAME_EXIT;
					return NO_MEMORY;
				}
			}

			mDisplayAdapter->setFrameProvider(mCameraAdapter);
			
			ret = mDisplayAdapter->setPreviewWindow(window);
			if (ret != NO_ERROR) {
				LOGE("DisplayAdapter setPreviewWindow returned error %d", ret);
			}
			
			LOGE("check if preview in progress %d\n", mPreviewStartInProgress);
			if (mPreviewStartInProgress) {
				ret = startPreview();
			}
		}	
		LOG_FUNCTION_NAME_EXIT;

		return ret;
	}

	/**
	  @brief Stop a previously started preview.

	  @param none
	  @return none

*/
	void CameraHal::stopPreview() {
		LOG_FUNCTION_NAME;

		if ((!previewEnabled() && !mDisplayPaused) || mRecordingEnabled) {
			LOG_FUNCTION_NAME_EXIT;
			return;
		}

		bool imageCaptureRunning = (mCameraAdapter->getState()
				== CameraAdapter::CAPTURE_STATE)
			&& (mCameraAdapter->getNextState() != CameraAdapter::PREVIEW_STATE);
		if (mDisplayPaused && !imageCaptureRunning) {
			// Display is paused, which essentially means there is no preview active.
			// Note: this is done so that when stopPreview is called by client after
			// an image capture, we do not de-initialize the camera adapter and
			// restart over again.

			return;
		}

		forceStopPreview();

		// Reset Capture-Mode to default, so that when we switch from VideoRecording
		// to ImageCapture, CAPTURE_MODE is not left to VIDEO_MODE.
		LOGE("Resetting Capture-Mode to default");

		LOG_FUNCTION_NAME_EXIT;
	}

	/**
	  @brief Returns true if preview is enabled

	  @param none
	  @return true If preview is running currently
	  false If preview has been stopped

*/
	bool CameraHal::previewEnabled() {
		LOG_FUNCTION_NAME;

		return (mPreviewEnabled || mPreviewStartInProgress);
	}

	/**
	  @brief Start record mode.

	  When a record image is available a CAMERA_MSG_VIDEO_FRAME message is sent with
	  the corresponding frame. Every record frame must be released by calling
	  releaseRecordingFrame().

	  @param none
	  @return NO_ERROR If recording could be started without any issues
	  @todo Update the header with possible error values in failure scenarios

*/
	status_t CameraHal::startRecording() {
		int w, h;
		const char *valstr = NULL;
		bool restartPreviewRequired = false;
		status_t ret = NO_ERROR;

		LOG_FUNCTION_NAME;

		if (!previewEnabled()) {
			return NO_INIT;
		}

		// set internal recording hint in case camera adapter needs to make some
		// decisions....(will only be sent to camera adapter if camera restart is required)
		// if application starts recording in continuous focus picture mode...
		// then we need to force default capture mode (as opposed to video mode)
		if (((valstr = mParameters.get(CameraParameters::KEY_FOCUS_MODE)) != NULL)
				&& (strcmp(valstr, CameraParameters::FOCUS_MODE_CONTINUOUS_PICTURE)
					== 0)) {
			restartPreviewRequired = resetVideoModeParameters();
		}

		// only need to check recording hint if preview restart is not already needed
		valstr = mParameters.get(CameraParameters::KEY_RECORDING_HINT);
		if (!restartPreviewRequired
				&& (!valstr
					|| (valstr && (strcmp(valstr, CameraParameters::TRUE) != 0)))) {
			restartPreviewRequired = setVideoModeParameters(mParameters);
		}

		if (restartPreviewRequired) {
			ret = restartPreview();
		}

		if (NO_ERROR == ret) {
			int count = 4;
			mParameters.getPreviewSize(&w, &h);
			LOGE("%s Video Width=%d Height=%d", __FUNCTION__, mVideoWidth,
					mVideoHeight);

			if ((w != mVideoWidth) && (h != mVideoHeight)) {
				ret = allocVideoBufs(mVideoWidth, mVideoHeight, count);
				if (NO_ERROR != ret) {
					LOGE("allocImageBufs returned error 0x%x", ret);
					return ret;
				}
			} else {
			}
		}

		if (NO_ERROR == ret) {
		}

		if (NO_ERROR == ret) {
			///Buffers for video capture (if different from preview) are expected to be allocated within CameraAdapter
			ret = mCameraAdapter->sendCommand(CameraAdapter::CAMERA_START_VIDEO);
		}

		if (NO_ERROR == ret) {
			mRecordingEnabled = true;
		}

		LOG_FUNCTION_NAME_EXIT;

		return ret;
	}

	/**
	  @brief Set the camera parameters specific to Video Recording.

	  This function checks for the camera parameters which have to be set for recording.
	  Video Recording needs CAPTURE_MODE to be VIDEO_MODE. This function sets it.
	  This function also enables Video Recording specific functions like VSTAB & VNF.

	  @param none
	  @return true if preview needs to be restarted for VIDEO_MODE parameters to take effect.
	  @todo Modify the policies for enabling VSTAB & VNF usecase based later.

*/
	bool CameraHal::setVideoModeParameters(const CameraParameters& params) {
		const char *valstr = NULL;
		bool restartPreviewRequired = false;
		status_t ret = NO_ERROR;

		LOG_FUNCTION_NAME;
		LOG_FUNCTION_NAME_EXIT;

		return restartPreviewRequired;
	}

	/**
	  @brief Reset the camera parameters specific to Video Recording.

	  This function resets CAPTURE_MODE and disables Recording specific functions like VSTAB & VNF.

	  @param none
	  @return true if preview needs to be restarted for VIDEO_MODE parameters to take effect.

*/
	bool CameraHal::resetVideoModeParameters() {
		const char *valstr = NULL;
		bool restartPreviewRequired = false;
		status_t ret = NO_ERROR;

		LOG_FUNCTION_NAME;

		LOG_FUNCTION_NAME_EXIT;

		return restartPreviewRequired;
	}

	/**
	  @brief Restart the preview with setParameter.

	  This function restarts preview, for some VIDEO_MODE parameters to take effect.

	  @param none
	  @return NO_ERROR If recording parameters could be set without any issues

*/
	status_t CameraHal::restartPreview() {
		const char *valstr = NULL;
		char tmpvalstr[30];
		status_t ret = NO_ERROR;

		LOG_FUNCTION_NAME;

		// Retain CAPTURE_MODE before calling stopPreview(), since it is reset in stopPreview().
		tmpvalstr[0] = 0;

		forceStopPreview();

		{
			Mutex::Autolock lock(mLock);
			mCameraAdapter->setParameters(mParameters);
		}

		ret = startPreview();

		LOG_FUNCTION_NAME_EXIT;

		return ret;
	}

	/**
	  @brief Stop a previously started recording.

	  @param none
	  @return none

*/
	void CameraHal::stopRecording() {
		CameraAdapter::AdapterState currentState;

		LOG_FUNCTION_NAME;

		Mutex::Autolock lock(mLock);

		if (!mRecordingEnabled) {
			return;
		}

		currentState = mCameraAdapter->getState();
		if (currentState == CameraAdapter::VIDEO_CAPTURE_STATE) {
			mCameraAdapter->sendCommand(CameraAdapter::CAMERA_STOP_IMAGE_CAPTURE);
		}

		mCameraAdapter->sendCommand(CameraAdapter::CAMERA_STOP_VIDEO);

		mRecordingEnabled = false;

		// reset internal recording hint in case camera adapter needs to make some
		// decisions....(will only be sent to camera adapter if camera restart is required)

		LOG_FUNCTION_NAME_EXIT;
	}

	/**
	  @brief Returns true if recording is enabled.

	  @param none
	  @return true If recording is currently running
	  false If recording has been stopped

*/
	int CameraHal::recordingEnabled() {
		LOG_FUNCTION_NAME;

		LOG_FUNCTION_NAME_EXIT;

		return mRecordingEnabled;
	}

	/**
	  @brief Release a record frame previously returned by CAMERA_MSG_VIDEO_FRAME.

	  @param[in] mem MemoryBase pointer to the frame being released. Must be one of the buffers
	  previously given by CameraHal
	  @return none

*/
	void CameraHal::releaseRecordingFrame(const void* mem) {
		LOG_FUNCTION_NAME;

		//LOGE(" 0x%x", mem->pointer());
		LOG_FUNCTION_NAME_EXIT;

		return;
	}

	/**
	  @brief Start auto focus

	  This call asynchronous.
	  The notification callback routine is called with CAMERA_MSG_FOCUS once when
	  focusing is complete. autoFocus() will be called again if another auto focus is
	  needed.

	  @param none
	  @return NO_ERROR
	  @todo Define the error codes if the focus is not locked

*/
	status_t CameraHal::autoFocus() {
		status_t ret = NO_ERROR;

		LOG_FUNCTION_NAME;

		{
			Mutex::Autolock lock(mLock);
			mMsgEnabled |= CAMERA_MSG_FOCUS;
		}

		if (NULL != mCameraAdapter) {


			ret = mCameraAdapter->sendCommand(
					CameraAdapter::CAMERA_PERFORM_AUTOFOCUS);

		} else {
			ret = -1;
		}

		LOG_FUNCTION_NAME_EXIT;

		return ret;
	}

	/**
	  @brief Cancels auto-focus function.

	  If the auto-focus is still in progress, this function will cancel it.
	  Whether the auto-focus is in progress or not, this function will return the
	  focus position to the default. If the camera does not support auto-focus, this is a no-op.


	  @param none
	  @return NO_ERROR If the cancel succeeded
	  @todo Define error codes if cancel didnt succeed

*/
	status_t CameraHal::cancelAutoFocus() {
		LOG_FUNCTION_NAME;

		Mutex::Autolock lock(mLock);
		CameraParameters adapterParams = mParameters;
		mMsgEnabled &= ~CAMERA_MSG_FOCUS;

		if (NULL != mCameraAdapter) {
			mCameraAdapter->setParameters(adapterParams);
			mCameraAdapter->sendCommand(CameraAdapter::CAMERA_CANCEL_AUTOFOCUS);
		}

		LOG_FUNCTION_NAME_EXIT;
		return NO_ERROR;
	}

	void CameraHal::setEventProvider(int32_t eventMask,
			MessageNotifier * eventNotifier) {

		LOG_FUNCTION_NAME;

		LOG_FUNCTION_NAME_EXIT;
	}

	void CameraHal::eventCallbackRelay(CameraHalEvent* event) {
		LOG_FUNCTION_NAME;

		CameraHal *appcbn = (CameraHal *) (event->mCookie);
		appcbn->eventCallback(event);

		LOG_FUNCTION_NAME_EXIT;
	}

	void CameraHal::eventCallback(CameraHalEvent* event) {
		LOG_FUNCTION_NAME;

		if (NULL != event) {
			switch (event->mEventType) {
			case CameraHalEvent::EVENT_FOCUS_LOCKED:
			case CameraHalEvent::EVENT_FOCUS_ERROR: {
									if (mBracketingEnabled) {
										startImageBracketing();
									}
									break;
								}
			default: {
					 break;
				 }
			};
		}

		LOG_FUNCTION_NAME_EXIT;
	}

	status_t CameraHal::startImageBracketing() {
		status_t ret = NO_ERROR;
		CameraFrame frame;
		CameraAdapter::BuffersDescriptor desc;

		LOG_FUNCTION_NAME;

		if (!previewEnabled() && !mDisplayPaused) {
			LOG_FUNCTION_NAME_EXIT;
			return NO_INIT;
		}

		if (!mBracketingEnabled) {
			return ret;
		}

		if (NO_ERROR == ret) {
			mBracketingRunning = true;
		}

		if ((NO_ERROR == ret) && (NULL != mCameraAdapter)) {
			ret = mCameraAdapter->sendCommand(
					CameraAdapter::CAMERA_QUERY_BUFFER_SIZE_IMAGE_CAPTURE,
					(int) &frame, (mBracketRangeNegative + 1));

			if (NO_ERROR != ret) {
				LOGE("CAMERA_QUERY_BUFFER_SIZE_IMAGE_CAPTURE returned error 0x%x",
						ret);
			}
		}

		if (NO_ERROR == ret) {
		}

		if (NO_ERROR == ret) {
			mParameters.getPictureSize((int *) &frame.mWidth,
					(int *) &frame.mHeight);

			ret = allocImageBufs(frame.mWidth, frame.mHeight, frame.mLength,
					mParameters.getPictureFormat(), (mBracketRangeNegative + 1));
			if (NO_ERROR != ret) {
				LOGE("allocImageBufs returned error 0x%x", ret);
			}
		}

		if ((NO_ERROR == ret) && (NULL != mCameraAdapter)) {

			desc.mBuffers = mImageBufs;
			desc.mOffsets = mImageOffsets;
			desc.mFd = mImageFd;
			desc.mLength = mImageLength;
			desc.mCount = (size_t)(mBracketRangeNegative + 1);
			desc.mMaxQueueable = (size_t)(mBracketRangeNegative + 1);

			ret = mCameraAdapter->sendCommand(
					CameraAdapter::CAMERA_USE_BUFFERS_IMAGE_CAPTURE, (int) &desc);

			if (NO_ERROR == ret) {

				ret = mCameraAdapter->sendCommand(
						CameraAdapter::CAMERA_START_BRACKET_CAPTURE,
						(mBracketRangePositive + 1));
			}
		}

		return ret;
	}

	status_t CameraHal::stopImageBracketing() {
		status_t ret = NO_ERROR;

		LOG_FUNCTION_NAME;

		if (!previewEnabled()) {
			return NO_INIT;
		}

		mBracketingRunning = false;

		ret = mCameraAdapter->sendCommand(
				CameraAdapter::CAMERA_STOP_BRACKET_CAPTURE);

		LOG_FUNCTION_NAME_EXIT;

		return ret;
	}

	/**
	  @brief Take a picture.

	  @param none
	  @return NO_ERROR If able to switch to image capture
	  @todo Define error codes if unable to switch to image capture

*/
	status_t CameraHal::takePicture() {
		status_t ret = NO_ERROR;
		CameraFrame frame;
		CameraAdapter::BuffersDescriptor desc;
		int burst;
		const char *valstr = NULL;
		unsigned int bufferCount = 1;

		Mutex::Autolock lock(mLock);
		LOG_FUNCTION_NAME;

		if (!previewEnabled() && !mDisplayPaused) {
			LOG_FUNCTION_NAME_EXIT;
			LOGE("Preview not started...");
			return NO_INIT;
		}

		// return error if we are already capturing
		if ((mCameraAdapter->getState() == CameraAdapter::CAPTURE_STATE
					&& mCameraAdapter->getNextState() != CameraAdapter::PREVIEW_STATE)
				|| (mCameraAdapter->getState() == CameraAdapter::VIDEO_CAPTURE_STATE
					&& mCameraAdapter->getNextState()
					!= CameraAdapter::VIDEO_STATE)) {
			LOGE("Already capturing an image...");
			return NO_INIT;
		}

		// we only support video snapshot if we are in video mode (recording hint is set)
		if ((mCameraAdapter->getState() == CameraAdapter::VIDEO_STATE)) {
			LOGE("Trying to capture while recording without recording hint set...");
			return INVALID_OPERATION;
		}

		if (!mBracketingRunning) {

			if (NO_ERROR == ret) {
			}

			//Allocate all buffers only in burst capture case
			if (burst > 1) {
				bufferCount = CameraHal::NO_BUFFERS_IMAGE_CAPTURE;
			} else {
			}

			// pause preview during normal image capture
			// do not pause preview if recording (video state)
			if (NO_ERROR == ret && NULL != mDisplayAdapter.get() && burst < 1) {
				if (mCameraAdapter->getState() != CameraAdapter::VIDEO_STATE) {
					mDisplayPaused = true;
					mPreviewEnabled = false;
					ret = mDisplayAdapter->pauseDisplay(mDisplayPaused);
					// since preview is paused we should stop sending preview frames too
					if (mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) {
					}
				}
			}

			// if we taking video snapshot...
			if ((NO_ERROR == ret)
					&& (mCameraAdapter->getState() == CameraAdapter::VIDEO_STATE)) {
				// enable post view frames if not already enabled so we can internally
				// save snapshot frames for generating thumbnail
				if ((mMsgEnabled & CAMERA_MSG_POSTVIEW_FRAME) == 0) {
				}
			}

			if ((NO_ERROR == ret) && (NULL != mCameraAdapter)) {
				if (NO_ERROR == ret)
					ret = mCameraAdapter->sendCommand(
							CameraAdapter::CAMERA_QUERY_BUFFER_SIZE_IMAGE_CAPTURE,
							(int) &frame, bufferCount);

				if (NO_ERROR != ret) {
					LOGE(
							"CAMERA_QUERY_BUFFER_SIZE_IMAGE_CAPTURE returned error 0x%x",
							ret);
				}
			}

			if (NO_ERROR == ret) {
				mParameters.getPictureSize((int *) &frame.mWidth,
						(int *) &frame.mHeight);

				ret = allocImageBufs(frame.mWidth, frame.mHeight, frame.mLength,
						mParameters.getPictureFormat(), bufferCount);
				if (NO_ERROR != ret) {
					LOGE("allocImageBufs returned error 0x%x", ret);
				}
			}

			if ((NO_ERROR == ret) && (NULL != mCameraAdapter)) {
				desc.mBuffers = mImageBufs;
				desc.mOffsets = mImageOffsets;
				desc.mFd = mImageFd;
				desc.mLength = mImageLength;
				desc.mCount = (size_t) bufferCount;
				desc.mMaxQueueable = (size_t) bufferCount;

				ret = mCameraAdapter->sendCommand(
						CameraAdapter::CAMERA_USE_BUFFERS_IMAGE_CAPTURE,
						(int) &desc);
			}
		}

		if ((NO_ERROR == ret) && (NULL != mCameraAdapter)) {
			ret = mCameraAdapter->sendCommand(
					CameraAdapter::CAMERA_START_IMAGE_CAPTURE);
		}

		return ret;
	}

	/**
	  @brief Cancel a picture that was started with takePicture.

	  Calling this method when no picture is being taken is a no-op.

	  @param none
	  @return NO_ERROR If cancel succeeded. Cancel can succeed if image callback is not sent
	  @todo Define error codes

*/
	status_t CameraHal::cancelPicture() {
		LOG_FUNCTION_NAME;

		Mutex::Autolock lock(mLock);

		mCameraAdapter->sendCommand(CameraAdapter::CAMERA_STOP_IMAGE_CAPTURE);

		return NO_ERROR;
	}

	/**
	  @brief Return the camera parameters.

	  @param none
	  @return Currently configured camera parameters

*/
	char* CameraHal::getParameters() {
		String8 params_str8;
		char* params_string;
		const char * valstr = NULL;

		LOG_FUNCTION_NAME;

		if (NULL != mCameraAdapter) {
			mCameraAdapter->getParameters(mParameters);
		}

		CameraParameters mParams = mParameters;

		// Handle RECORDING_HINT to Set/Reset Video Mode Parameters
		valstr = mParameters.get(CameraParameters::KEY_RECORDING_HINT);
		if (valstr != NULL) {
			if (strcmp(valstr, CameraParameters::TRUE) == 0) {
				//HACK FOR MMS MODE
				resetPreviewRes(&mParams, mVideoWidth, mVideoHeight);
			}
		}

		params_str8 = mParams.flatten();

		// camera service frees this string...
		params_string = (char*) malloc(sizeof(char) * (params_str8.length() + 1));
		strcpy(params_string, params_str8.string());

		LOG_FUNCTION_NAME_EXIT;

		///Return the current set of parameters

		return params_string;
	}

	void CameraHal::putParameters(char *parms) {
		free(parms);
	}

	/**
	  @brief Send command to camera driver.

	  @param none
	  @return NO_ERROR If the command succeeds
	  @todo Define the error codes that this function can return

*/
	status_t CameraHal::sendCommand(int32_t cmd, int32_t arg1, int32_t arg2) {
		status_t ret = NO_ERROR;

		LOG_FUNCTION_NAME;

		if (NULL == mCameraAdapter) {
			LOGE("No CameraAdapter instance");
			ret = -EINVAL;
		}

		if (!previewEnabled()) {
			LOGE("Preview is not running");
			ret = -EINVAL;
		}

		LOGE("sendCommand cmd %d, arg1 %d, arg2 %d\n",cmd, arg1, arg2);		

		/*switch (cmd) {
		case CAMERA_CMD_START_SMOOTH_ZOOM:
			ret = mCameraAdapter->sendCommand(
					CameraAdapter::CAMERA_START_SMOOTH_ZOOM, arg1);
			break;
			
		case CAMERA_CMD_STOP_SMOOTH_ZOOM:
			ret = mCameraAdapter->sendCommand(
					CameraAdapter::CAMERA_STOP_SMOOTH_ZOOM);
			break;

		case CAMERA_CMD_START_FACE_DETECTION:
			ret = mCameraAdapter->sendCommand(CameraAdapter::CAMERA_START_FD);
			break;

		case CAMERA_CMD_STOP_FACE_DETECTION:
			ret = mCameraAdapter->sendCommand(CameraAdapter::CAMERA_STOP_FD);
			break;

		default:
			break;
		};*/

		LOG_FUNCTION_NAME_EXIT;

		return ret;
	}

	/**
	  @brief Release the hardware resources owned by this object.

	  Note that this is *not* done in the destructor.

	  @param none
	  @return none

*/
	void CameraHal::release() {
		LOG_FUNCTION_NAME;
		///@todo Investigate on how release is used by CameraService. Vaguely remember that this is called
		///just before CameraHal object destruction
		deinitialize();
		LOG_FUNCTION_NAME_EXIT;
	}

	/**
	  @brief Dump state of the camera hardware

	  @param[in] fd    File descriptor
	  @param[in] args  Arguments
	  @return NO_ERROR Dump succeeded
	  @todo  Error codes for dump fail

*/
	status_t CameraHal::dump(int fd) const {
		LOG_FUNCTION_NAME;
		///Implement this method when the h/w dump function is supported on Ducati side
		return NO_ERROR;
	}

	/*-------------Camera Hal Interface Method definitions ENDS here--------------------*/

	/*-------------Camera Hal Internal Method definitions STARTS here--------------------*/

	/**
	  @brief Constructor of CameraHal

	  Member variables are initialized here.  No allocations should be done here as we
	  don't use c++ exceptions in the code.

*/
	CameraHal::CameraHal(int cameraId) {
		LOG_FUNCTION_NAME;

		///Initialize all the member variables to their defaults
		mPreviewEnabled = false;
		mPreviewBufs = NULL;
		mImageBufs = NULL;
		mBufProvider = NULL;
		mPreviewStartInProgress = false;
		mVideoBufs = NULL;
		mVideoBufProvider = NULL;
		mRecordingEnabled = false;
		mDisplayPaused = false;
		mSetPreviewWindowCalled = false;
		mMsgEnabled = 0;
		mMemoryManager = NULL;
		mCameraAdapter = NULL;
		mBracketingEnabled = false;
		mBracketingRunning = false;
		mBracketRangePositive = 1;
		mBracketRangeNegative = 1;
		mMaxZoomSupported = 0;
		mShutterEnabled = true;
		mMeasurementEnabled = false;
		mPreviewDataBufs = NULL;
		mCameraProperties = NULL;
		mCurrentTime = 0;
		mFalsePreview = 0;
		mImageOffsets = NULL;
		mImageLength = 0;
		mImageFd = 0;
		mVideoOffsets = NULL;
		mVideoFd = 0;
		mVideoLength = 0;
		mPreviewDataOffsets = NULL;
		mPreviewDataFd = 0;
		mPreviewDataLength = 0;
		mPreviewFd = 0;
		mPreviewWidth = 0;
		mPreviewHeight = 0;
		mPreviewLength = 0;
		mPreviewOffsets = NULL;
		mPreviewRunning = 0;
		mPreviewStateOld = 0;
		mRecordingEnabled = 0;
		mRecordEnabled = 0;
//		mSensorListener = NULL;
		mVideoWidth = 0;
		mVideoHeight = 0;
		mCameraIndex = cameraId;

		LOG_FUNCTION_NAME_EXIT;
	}

	/**
	  @brief Destructor of CameraHal

	  This function simply calls deinitialize() to free up memory allocate during construct
	  phase
	  */
	CameraHal::~CameraHal() {
		LOG_FUNCTION_NAME;

		///Call de-initialize here once more - it is the last chance for us to relinquish all the h/w and s/w resources
		deinitialize();
		/// Free the callback notifier

		/// Free the display adapter
		mDisplayAdapter.clear();

		if (NULL != mCameraAdapter) {
			int strongCount = mCameraAdapter->getStrongCount();

			mCameraAdapter->decStrong(mCameraAdapter);

			mCameraAdapter = NULL;
		}

		freeImageBufs();

		/// Free the memory manager
		mMemoryManager.clear();

		LOG_FUNCTION_NAME_EXIT;
	}

	/**
	  @brief Initialize the Camera HAL

	  Creates CameraAdapter, AppCallbackNotifier, DisplayAdapter and MemoryManager

	  @param None
	  @return NO_ERROR - On success
	  NO_MEMORY - On failure to allocate memory for any of the objects
	  @remarks Camera Hal internal function

*/

	status_t CameraHal::initialize(CameraProperties::Properties* properties) {
		LOG_FUNCTION_NAME;

		///Initialize the event mask used for registering an event provider for AppCallbackNotifier
		///Currently, registering all events as to be coming from CameraAdapter
		int32_t eventMask = CameraHalEvent::ALL_EVENTS;

		// Get my camera properties
		mCameraProperties = properties;

		if (!mCameraProperties) {
			goto fail_loop;
		}

		// Dump the properties of this Camera
		// will only print if DEBUG macro is defined
		mCameraProperties->dump();

		mCameraAdapter = CameraAdapter_Factory();
		if ((NULL == mCameraAdapter)
				|| (mCameraAdapter->initialize(properties) != NO_ERROR)) {
			LOGE("Unable to create or initialize CameraAdapter");
			mCameraAdapter = NULL;
			goto fail_loop;
		}

		mCameraAdapter->incStrong(mCameraAdapter);
		mCameraAdapter->registerImageReleaseCallback(releaseImageBuffers,
				(void *) this);
		mCameraAdapter->registerEndCaptureCallback(endImageCapture, (void *) this);

		if (!mMemoryManager.get()) {
			/// Create Memory Manager
			mMemoryManager = new MemoryManager();
			if ((NULL == mMemoryManager.get())
					|| (mMemoryManager->initialize() != NO_ERROR)) {
				LOGE("Unable to create or initialize MemoryManager");
				goto fail_loop;
			}
		}

		///Setup the class dependencies...

		///AppCallbackNotifier has to know where to get the Camera frames and the events like auto focus lock etc from.
		///CameraAdapter is the one which provides those events
		///Set it as the frame and event providers for AppCallbackNotifier
		///@remarks  setEventProvider API takes in a bit mask of events for registering a provider for the different events
		///         That way, if events can come from DisplayAdapter in future, we will be able to add it as provider
		///         for any event

		///Any dynamic errors that happen during the camera use case has to be propagated back to the application
		///via CAMERA_MSG_ERROR. AppCallbackNotifier is the class that  notifies such errors to the application
		///Set it as the error handler for CameraAdapter

		///Start the callback notifier
		///Initialize default parameters
		initDefaultParameters();

		if (setParameters(mParameters) != NO_ERROR) {
			LOGE("Failed to set default parameters?!");
		}

		LOG_FUNCTION_NAME_EXIT;

		return NO_ERROR;

fail_loop:

		///Free up the resources because we failed somewhere up
		deinitialize();
		LOG_FUNCTION_NAME_EXIT;

		return NO_MEMORY;

	}

	bool CameraHal::isResolutionValid(unsigned int width, unsigned int height,
			const char *supportedResolutions) {
		bool ret = true;
		return ret;
		status_t status = NO_ERROR;
		char tmpBuffer[PARAM_BUFFER + 1];
		char *pos = NULL;

		LOG_FUNCTION_NAME;

		if (NULL == supportedResolutions) {
			LOGE("Invalid supported resolutions string");
			ret = false;
			goto exit;
		}

		status = snprintf(tmpBuffer, PARAM_BUFFER, "%dx%d", width, height);
		if (0 > status) {
			LOGE("Error encountered while generating validation string");
			ret = false;
			goto exit;
		}

		pos = strstr(supportedResolutions, tmpBuffer);
		if (NULL == pos) {
			ret = false;
		} else {
			ret = true;
		}

exit:

		LOG_FUNCTION_NAME_EXIT;

		return ret;
	}

	bool CameraHal::isParameterValid(const char *param,
			const char *supportedParams) {
		bool ret = true;
		char *pos = NULL;

		LOG_FUNCTION_NAME;

		if (NULL == supportedParams) {
			LOGE("Invalid supported parameters string");
			ret = false;
			goto exit;
		}

		if (NULL == param) {
			LOGE("Invalid parameter string");
			ret = false;
			goto exit;
		}

		pos = strstr(supportedParams, param);
		if (NULL == pos) {
			ret = false;
		} else {
			ret = true;
		}

exit:

		LOG_FUNCTION_NAME_EXIT;

		return ret;
	}

	bool CameraHal::isParameterValid(int param, const char *supportedParams) {
		bool ret = true;
		return true;
		char *pos = NULL;
		status_t status;
		char tmpBuffer[PARAM_BUFFER + 1];

		LOG_FUNCTION_NAME;

		if (NULL == supportedParams) {
			LOGE("Invalid supported parameters string");
			ret = false;
			goto exit;
		}

		status = snprintf(tmpBuffer, PARAM_BUFFER, "%d", param);
		if (0 > status) {
			LOGE("Error encountered while generating validation string");
			ret = false;
			goto exit;
		}

		pos = strstr(supportedParams, tmpBuffer);
		if (NULL == pos) {
			ret = false;
		} else {
			ret = true;
		}

exit:

		LOG_FUNCTION_NAME_EXIT;

		return ret;
	}

	status_t CameraHal::doesSetParameterNeedUpdate(const char* new_param,
			const char* old_param, bool& update) {
		if (!new_param || !old_param) {
			return -EINVAL;
		}

		// if params mismatch we should update parameters for camera adapter
		if ((strcmp(new_param, old_param) != 0)) {
			update = true;
		}

		return NO_ERROR;
	}

	status_t CameraHal::parseResolution(const char *resStr, int &width,
			int &height) {
		status_t ret = NO_ERROR;
		char *ctx, *pWidth, *pHeight;
		const char *sep = "x";
		char *tmp = NULL;

		LOG_FUNCTION_NAME;

		if (NULL == resStr) {
			return -EINVAL;
		}

		//This fixes "Invalid input resolution"
		char *resStr_copy = (char *) malloc(strlen(resStr) + 1);
		if (NULL != resStr_copy) {
			if (NO_ERROR == ret) {
				strcpy(resStr_copy, resStr);
				pWidth = strtok_r((char *) resStr_copy, sep, &ctx);

				if (NULL != pWidth) {
					width = atoi(pWidth);
				} else {
					LOGE("Invalid input resolution %s", resStr);
					ret = -EINVAL;
				}
			}

			if (NO_ERROR == ret) {
				pHeight = strtok_r(NULL, sep, &ctx);

				if (NULL != pHeight) {
					height = atoi(pHeight);
				} else {
					LOGE("Invalid input resolution %s", resStr);
					ret = -EINVAL;
				}
			}

			free(resStr_copy);
			resStr_copy = NULL;
		}
		LOG_FUNCTION_NAME_EXIT;

		return ret;
	}

	void CameraHal::initDefaultParameters() {
		CameraParameters &p = mParameters;
		int currentRevision, adapterRevision;
		status_t ret = NO_ERROR;
		int width, height;

		LOG_FUNCTION_NAME;

		//Set default values
		p.setPreviewFrameRate(16);
		p.set(CameraParameters::KEY_JPEG_QUALITY, 95);
		p.set(CameraParameters::KEY_PICTURE_SIZE, "640x480");
		p.set(CameraParameters::KEY_PICTURE_FORMAT, "yuv422i-yuyv");
		p.set(CameraParameters::KEY_PREVIEW_FORMAT, "yuv422i-yuyv");
		p.set(CameraParameters::KEY_PREVIEW_SIZE, "640x480");
		p.set(CameraParameters::KEY_FOCUS_MODE, "infinity");
		p.set(CameraParameters::KEY_SCENE_MODE, "auto");
		p.set("picture-size-values", "640x480");
		p.set("preview-size-values", "640x480");

		mCameraAdapter->setParameters(mParameters);

		LOG_FUNCTION_NAME_EXIT;
	}

	/**
	  @brief Stop a previously started preview.
	  @param none
	  @return none

*/
	void CameraHal::forceStopPreview() {
		LOG_FUNCTION_NAME;

		// stop bracketing if it is running
		stopImageBracketing();

		if (mDisplayAdapter.get() != NULL) {
			///Stop the buffer display first
			mDisplayAdapter->disableDisplay();
		}
		
		if (NULL != mCameraAdapter) {
			CameraAdapter::AdapterState currentState;
			CameraAdapter::AdapterState nextState;

			currentState = mCameraAdapter->getState();
			nextState = mCameraAdapter->getNextState();

			// since prerequisite for capturing is for camera system
			// to be previewing...cancel all captures before stopping
			// preview
			if ((currentState == CameraAdapter::CAPTURE_STATE)
					&& (nextState != CameraAdapter::PREVIEW_STATE)) {
				mCameraAdapter->sendCommand(
						CameraAdapter::CAMERA_STOP_IMAGE_CAPTURE);
			}

			// only need to send these control commands to state machine if we are
			// passed the LOADED_PREVIEW_STATE
			if (currentState > CameraAdapter::LOADED_PREVIEW_STATE) {
				// according to javadoc...FD should be stopped in stopPreview
				// and application needs to call startFaceDection again
				// to restart FD
				mCameraAdapter->sendCommand(CameraAdapter::CAMERA_STOP_FD);
				mCameraAdapter->sendCommand(CameraAdapter::CAMERA_CANCEL_AUTOFOCUS);
			}

			// only need to send these control commands to state machine if we are
			// passed the INITIALIZED_STATE
			if (currentState > CameraAdapter::INTIALIZED_STATE) {
				//Stop the source of frames
				mCameraAdapter->sendCommand(CameraAdapter::CAMERA_STOP_PREVIEW);
			}
		}

		freePreviewBufs();
		freePreviewDataBufs();

		mPreviewEnabled = false;
		mDisplayPaused = false;
		mPreviewStartInProgress = false;

		LOG_FUNCTION_NAME_EXIT;
	}

	/**
	  @brief Deallocates memory for all the resources held by Camera HAL.

	  Frees the following objects- CameraAdapter, AppCallbackNotifier, DisplayAdapter,
	  and Memory Manager

	  @param none
	  @return none

*/
	void CameraHal::deinitialize() {
		LOG_FUNCTION_NAME;

		if (mPreviewEnabled || mDisplayPaused) {
			forceStopPreview();
		}

		mSetPreviewWindowCalled = false;

/*
		if (mSensorListener.get()) {
			mSensorListener->disableSensor(SensorListener::SENSOR_ORIENTATION);
			mSensorListener.clear();
			mSensorListener = NULL;
		}
*/

		LOG_FUNCTION_NAME_EXIT;

	}

	status_t CameraHal::storeMetaDataInBuffers(bool enable) {
		LOG_FUNCTION_NAME;
		return NULL;
		LOG_FUNCTION_NAME_EXIT;
	}

	void CameraHal::selectFPSRange(int framerate, int *min_fps, int *max_fps) {
		char * ptr;
		char supported[MAX_PROP_VALUE_LENGTH];
		int fpsrangeArray[2];
		int i = 0;

		LOG_FUNCTION_NAME;

		ptr = strtok(supported, " (,)");

		while (ptr != NULL) {
			fpsrangeArray[i] = atoi(ptr) / CameraHal::VFR_SCALE;
			if (i == 1) {
				if (framerate == fpsrangeArray[i]) {
					LOGE("SETTING FPS RANGE min = %d max = %d \n", fpsrangeArray[0],
							fpsrangeArray[1]);
					*min_fps = fpsrangeArray[0] * CameraHal::VFR_SCALE;
					*max_fps = fpsrangeArray[1] * CameraHal::VFR_SCALE;
					break;
				}
			}
			ptr = strtok(NULL, " (,)");
			i++;
			i %= 2;
		}

		LOG_FUNCTION_NAME_EXIT;

	}

	void CameraHal::setPreferredPreviewRes(int width, int height) {
		LOG_FUNCTION_NAME;

		if ((width == 320) && (height == 240)) {
			mParameters.setPreviewSize(640, 480);
		}
		if ((width == 176) && (height == 144)) {
			mParameters.setPreviewSize(704, 576);
		}

		LOG_FUNCTION_NAME_EXIT;
	}

	void CameraHal::resetPreviewRes(CameraParameters *mParams, int width,
			int height) {
		LOG_FUNCTION_NAME;

		if ((width <= 320) && (height <= 240)) {
			mParams->setPreviewSize(mVideoWidth, mVideoHeight);
		}

		LOG_FUNCTION_NAME_EXIT;
	}

}
;

