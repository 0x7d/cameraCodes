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
 
#include "V4LCameraAdapter.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <linux/videodev.h>

#include <cutils/properties.h>
#define UNLIKELY( exp ) (__builtin_expect( (exp) != 0, false ))

#define HERE(Msg) {LOGE("--===line %d, %s===--\n", __LINE__, Msg);}

namespace android {

#undef LOG_TAG
#define LOG_TAG "V4LCameraAdapter"

	//frames skipped before recalculating the framerate
#define FPS_PERIOD 30

	Mutex gAdapterLock;
	const char *device = DEVICE;

	V4LCameraAdapter::V4LCameraAdapter() {
		LOG_FUNCTION_NAME;

		// Nothing useful to do in the constructor

		LOG_FUNCTION_NAME_EXIT;
	}

	V4LCameraAdapter::~V4LCameraAdapter() {
		LOG_FUNCTION_NAME;

		// Close the camera handle and free the video info structure
		close(mCameraHandle);

		if (mVideoInfo) {
			free(mVideoInfo);
			mVideoInfo = NULL;
		}

		LOG_FUNCTION_NAME_EXIT;
	}

	status_t V4LCameraAdapter::initialize(
			CameraProperties::Properties* properties) {
		LOG_FUNCTION_NAME;

		int ret = NO_ERROR;

		// Allocate memory for video info structure
		mVideoInfo = (struct VideoInfo *) calloc(1, sizeof(struct VideoInfo));
		if (!mVideoInfo) {
			return NO_MEMORY;
		}

		if ((mCameraHandle = open(device, O_RDWR)) == -1) {
			LOGE("Error while opening handle to V4L2 Camera: %s", strerror(errno));
			return -EINVAL;
		}

		LOGE("iiiiiii VIDIOC_QUERYCAP\n");
		ret = ioctl(mCameraHandle, VIDIOC_QUERYCAP, &mVideoInfo->cap);
		if (ret < 0) {
			LOGE("Error when querying the capabilities of Camera");
			return -EINVAL;
		}

		if ((mVideoInfo->cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0) {
			LOGE("Video capture not supported.");
			return -EINVAL;
		}

		if (!(mVideoInfo->cap.capabilities & V4L2_CAP_STREAMING)) {
			LOGE("Capture device does not support streaming");
			return -EINVAL;
		}

		LOGE("V4L Camera Adapter init over!\n");
		
		// Initialize flags
		mPreviewing = false;
		mVideoInfo->isStreaming = false;
		mRecording = false;

		LOG_FUNCTION_NAME_EXIT;

		return ret;
	}


	status_t V4LCameraAdapter::takePicture(){
		LOG_FUNCTION_NAME;
		
		status_t ret = NO_ERROR;
		int index = mBufferIndex ;
		LOGE("takePicture mBufferIndex %d", mBufferIndex);
		char *pictureName = PICNAME;
		int width = mVideoInfo->width;
		int height = mVideoInfo->height;
		int bufferSize = width * height * 2;
		LOGE("width %d, height %d", width, height);
		char *buffers = (char *)calloc(1, bufferSize);
		char *src = (char *)mVideoInfo->mem[mBufferIndex];
		LOGE("calloc buffers %x, src %x", buffers, src);

		memcpy(buffers, src, bufferSize);
		FILE *fp = fopen(pictureName, "wb");
		LOGE("open file %s, address %x", pictureName, fp);
		if (fp < 0) {
			printf("open frame data file failed\n");
			free(buffers);
			buffers = NULL;
			return -1;
		}
		fwrite(buffers, 1, bufferSize, fp);
		fclose(fp);
		free(buffers);
		buffers = NULL;

		LOG_FUNCTION_NAME_EXIT;
		return ret;
	}

	status_t V4LCameraAdapter::fillThisBuffer(void* frameBuf,
			CameraFrame::FrameType frameType) {
		LOG_FUNCTION_NAME;

		status_t ret = NO_ERROR;

		if (!mVideoInfo->isStreaming) {
			return NO_ERROR;
		}

		int i = mPreviewBufs.valueFor((unsigned int) frameBuf);
		if (i < 0) {
			return BAD_VALUE;
		}

		mVideoInfo->buf.index = i;
		mVideoInfo->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		mVideoInfo->buf.memory = V4L2_MEMORY_MMAP;

		ret = ioctl(mCameraHandle, VIDIOC_QBUF, &mVideoInfo->buf);
		if (ret < 0) {
			LOGE("Init: VIDIOC_QBUF Failed");
			return -1;
		}

		nQueued++;
		LOG_FUNCTION_NAME_EXIT;

		return ret;
	}

	status_t V4LCameraAdapter::setParameters(const CameraParameters &params) {
		LOG_FUNCTION_NAME;
		LOGE("iiiiiiii Set Camera adapter format\n");
		status_t ret = NO_ERROR;
		int width, height;

		params.getPreviewSize(&width, &height);

		LOGE("Width * Height %d x %d format 0x%x", width, height,
				DEFAULT_PIXEL_FORMAT);

		mVideoInfo->width = width;
		mVideoInfo->height = height;
		mVideoInfo->framesizeIn = (width * height << 1);
		mVideoInfo->formatIn = DEFAULT_PIXEL_FORMAT;

		mVideoInfo->format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		mVideoInfo->format.fmt.pix.width = width;
		mVideoInfo->format.fmt.pix.height = height;
		mVideoInfo->format.fmt.pix.pixelformat = DEFAULT_PIXEL_FORMAT;

		LOGE("iiiiiiii VIDIOC_S_FMT\n");
		ret = ioctl(mCameraHandle, VIDIOC_S_FMT, &mVideoInfo->format);
		if (ret < 0) {
			LOGE("VIDIOC_S_FMT Failed: %s", strerror(errno));
			return ret;
		}

		mParams = params;

		LOG_FUNCTION_NAME_EXIT;
		return ret;
	}

	void V4LCameraAdapter::getParameters(CameraParameters& params) {
		LOG_FUNCTION_NAME;
		params = mParams;
		LOG_FUNCTION_NAME_EXIT;
	}

	/*API to give the buffers to Adapter*/
	status_t V4LCameraAdapter::useBuffers(CameraMode mode, void* bufArr, int num,
			size_t length, unsigned int queueable) {
		LOG_FUNCTION_NAME;

		status_t ret = NO_ERROR;
		Mutex::Autolock lock(mLock);

		switch (mode) {
			case CAMERA_PREVIEW:
				ret = useBuffersPreview(bufArr, num);
				break;
			case CAMERA_VIDEO:
				ret = useBuffersPreview(bufArr, num);
				break;
		}

		LOG_FUNCTION_NAME_EXIT;

		return ret;
	}

	//First allocate adapter internal buffers at V4L level for USB Cam
	//These are the buffers from which we will copy the data into overlay buffers
	/* Check if camera can handle NB_BUFFER buffers */
	status_t V4LCameraAdapter::useBuffersPreview(void* bufArr, int num) {
		int ret = NO_ERROR;

		if (NULL == bufArr) {
			return BAD_VALUE;
		}

		mVideoInfo->rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		mVideoInfo->rb.memory = V4L2_MEMORY_MMAP;
		mVideoInfo->rb.count = num;

		LOGE("iiiiiiii VIDIOC_REQBUFS\n");
		ret = ioctl(mCameraHandle, VIDIOC_REQBUFS, &mVideoInfo->rb);
		if (ret < 0) {
			LOGE("VIDIOC_REQBUFS failed: %s", strerror(errno));
			return ret;
		}

		for (int i = 0; i < num; i++) {
			memset(&mVideoInfo->buf, 0, sizeof(struct v4l2_buffer));

			mVideoInfo->buf.index = i;
			mVideoInfo->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			mVideoInfo->buf.memory = V4L2_MEMORY_MMAP;

			LOGE("iiiiiiii VIDIOC_QUERYBUF\n");
			ret = ioctl(mCameraHandle, VIDIOC_QUERYBUF, &mVideoInfo->buf);
			if (ret < 0) {
				LOGE("Unable to query buffer (%s)", strerror(errno));
				return ret;
			}

			mVideoInfo->mem[i] = mmap(0, mVideoInfo->buf.length,
					PROT_READ | PROT_WRITE, MAP_SHARED, mCameraHandle,
					mVideoInfo->buf.m.offset);

			if (mVideoInfo->mem[i] == MAP_FAILED) {
				LOGE("Unable to map buffer (%s)", strerror(errno));
				return -1;
			}


			uint32_t *ptr = (uint32_t*) bufArr;
			LOGE("xxxxxxx bufArr index %d, address %x", i, ptr[i]);

			//Associate each Camera internal buffer with the one from Overlay
			mPreviewBufs.add((int)ptr[i], i);

		}

		// Update the preview buffer count
		mPreviewBufferCount = num;

		return ret;
	}

	status_t V4LCameraAdapter::startPreview() {
		status_t ret = NO_ERROR;

		Mutex::Autolock lock(mPreviewBufsLock);

		if (mPreviewing) {
			return BAD_VALUE;
		}

		for (int i = 0; i < mPreviewBufferCount; i++) {

			mVideoInfo->buf.index = i;
			mVideoInfo->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			mVideoInfo->buf.memory = V4L2_MEMORY_MMAP;

			LOGE("iiiiiiii VIDIOC_QBUF\n");
			ret = ioctl(mCameraHandle, VIDIOC_QBUF, &mVideoInfo->buf);
			if (ret < 0) {
				LOGE("VIDIOC_QBUF Failed");
				return -EINVAL;
			}
			LOGE("iiiiiiii mVideoInfo->buf length %d\n", mVideoInfo->buf.length);

			nQueued++;
		}

		enum v4l2_buf_type bufType;
		LOGE("iiiiiiii mVideoInfo->isStreaming %d", mVideoInfo->isStreaming);
		if (!mVideoInfo->isStreaming) {
			bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			LOGE("VIDIOC_STREAMON\n");
			ret = ioctl(mCameraHandle, VIDIOC_STREAMON, &bufType);
			if (ret < 0) {
				LOGE("StartStreaming: Unable to start capture: %s",
						strerror(errno));
				return ret;
			}
			mVideoInfo->isStreaming = true;
		}

		//Update the flag to indicate we are previewing
		mPreviewing = true;

		// Create and start preview thread for receiving buffers from V4L Camera

		LOGE("Created preview thread");
		mPreviewThread = new PreviewThread(this);
		return ret;
	}

	status_t V4LCameraAdapter::stopPreview() {
		enum v4l2_buf_type bufType;
		int ret = NO_ERROR;

		Mutex::Autolock lock(mPreviewBufsLock);

		if (!mPreviewing) {
			return NO_INIT;
		}

		if (mVideoInfo->isStreaming) {
			bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;

			ret = ioctl(mCameraHandle, VIDIOC_STREAMOFF, &bufType);
			if (ret < 0) {
				LOGE("StopStreaming: Unable to stop capture: %s", strerror(errno));
				return ret;
			}

			mVideoInfo->isStreaming = false;
		}

		mVideoInfo->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		mVideoInfo->buf.memory = V4L2_MEMORY_MMAP;

		nQueued = 0;
		nDequeued = 0;

		/* Unmap buffers */
		for (int i = 0; i < mPreviewBufferCount; i++)
			if (munmap(mVideoInfo->mem[i], mVideoInfo->buf.length) < 0)
				LOGE("Unmap failed");

		mPreviewBufs.clear();

		mPreviewThread->requestExitAndWait();
		mPreviewThread.clear();

		return ret;

	}

	char * V4LCameraAdapter::getFrame(int &index) {
		LOG_FUNCTION_NAME;

		int ret;

		mVideoInfo->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		mVideoInfo->buf.memory = V4L2_MEMORY_MMAP;

		/* DQ */
		ret = ioctl(mCameraHandle, VIDIOC_DQBUF, &mVideoInfo->buf);
		if (ret < 0) {
			LOGE("getFrame: VIDIOC_DQBUF Failed");
			return NULL;
		}
		nDequeued++;

		index = mVideoInfo->buf.index;
		LOGE("current buffer index %d\n", index);
		LOG_FUNCTION_NAME_EXIT;
		return (char *) mVideoInfo->mem[mVideoInfo->buf.index];
	}

	//API to get the frame size required to be allocated. This size is used to override the size passed
	//by camera service when VSTAB/VNF is turned ON for example
	//Just return the current preview size, nothing more to do here.
	status_t V4LCameraAdapter::getFrameSize(size_t &width, size_t &height) {
		status_t ret = NO_ERROR;

		mParams.getPreviewSize((int *) &width, (int *) &height);

		LOG_FUNCTION_NAME_EXIT;

		return ret;
	}

	status_t V4LCameraAdapter::getFrameDataSize(size_t &dataFrameSize,
			size_t bufferCount) {
		// We don't support meta data, so simply return
		return NO_ERROR;
	}

	status_t V4LCameraAdapter::getPictureBufferSize(size_t &length,
			size_t bufferCount) {
		// We don't support image capture yet, safely return from here without messing up
		return NO_ERROR;
	}
	
	status_t V4LCameraAdapter::recalculateFPS() {
		float currentFPS;

		mFrameCount++;

		if ((mFrameCount % FPS_PERIOD) == 0) {
			nsecs_t now = systemTime();
			nsecs_t diff = now - mLastFPSTime;
			currentFPS = ((mFrameCount - mLastFrameCount) * float(s2ns(1))) / diff;
			mLastFPSTime = now;
			mLastFrameCount = mFrameCount;

			if (1 == mIter) {
				mFPS = currentFPS;
			} else {
				//cumulative moving average
				mFPS = mLastFPS + (currentFPS - mLastFPS) / mIter;
			}

			mLastFPS = mFPS;
			mIter++;
		}

		return NO_ERROR;
	}

	void V4LCameraAdapter::onOrientationEvent(uint32_t orientation, uint32_t tilt) {
		LOG_FUNCTION_NAME;

		LOG_FUNCTION_NAME_EXIT;
	}

	/* Preview Thread */
	int V4LCameraAdapter::previewThread() {
		LOG_FUNCTION_NAME;

		status_t ret = NO_ERROR;
		int width, height;
		CameraFrame frame;

		if (mPreviewing) {
			char *fp = this->getFrame(mBufferIndex);
			LOGE("get frame at address %x\n", fp);
			if (!fp) {
				return BAD_VALUE;
			}

			char *ptr = (char*) mPreviewBufs.keyAt(mBufferIndex);
			LOGE("current preview buffer %x\n", ptr);

			int width, height;
			mParams.getPreviewSize(&width, &height);
			LOGE("preview size, width %d,height %d\n", width, height);
	        
			memcpy(ptr, fp, width * height * 2);
			
			frame.mFrameType = CameraFrame::PREVIEW_FRAME_SYNC;
			frame.mBuffer = ptr;
			frame.mLength = width * height * 2;
			frame.mAlignment = width * 2;
			frame.mOffset = 0;
			frame.mTimestamp = systemTime(SYSTEM_TIME_MONOTONIC);

			ret = sendFrameToSubscribers(&frame);
			if(ret < 0){
				LOGE("send Frame to subscribers failed!\n");
			}
			
			ret = ioctl(mCameraHandle, VIDIOC_QBUF, &mVideoInfo->buf);
			if (ret < 0) {
				LOGE("Init: VIDIOC_QBUF Failed");
				return -1;
			}
		}

		LOG_FUNCTION_NAME_EXIT;

		return ret;
	}

	extern "C" CameraAdapter* CameraAdapter_Factory() {
		CameraAdapter *adapter = NULL;
		Mutex::Autolock lock(gAdapterLock);

		LOG_FUNCTION_NAME;

		adapter = new V4LCameraAdapter();
		if (adapter) {
			LOGE("V4L Camera adapter instance created");
		} else {
			LOGE("Camera adapter create failed!");
		}

		LOG_FUNCTION_NAME_EXIT;

		return adapter;
	}

	extern "C" int CameraAdapter_Capabilities(
			CameraProperties::Properties* properties,
			const unsigned int starting_camera, const unsigned int max_camera) {
		int num_cameras_supported = 1;

		LOG_FUNCTION_NAME;
		LOGE("properties %x\n", properties);
		if (!properties) {
			return -EINVAL;
		}
		LOG_FUNCTION_NAME_EXIT;

		return num_cameras_supported;
	}
}
;
