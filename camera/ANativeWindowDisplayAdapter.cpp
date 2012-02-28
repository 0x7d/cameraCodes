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


#include "ANativeWindowDisplayAdapter.h"
#include <OMX_IVCommon.h>
#include <ui/GraphicBuffer.h>
#include <ui/GraphicBufferMapper.h>
#include <hal_public.h>
#include <ui/Region.h>
#include <ui/egl/android_natives.h>
#include <utils/RefBase.h>

namespace android {

	///Constant declarations
	///@todo Check the time units
	const int ANativeWindowDisplayAdapter::DISPLAY_TIMEOUT = 1000;  // seconds

	//Suspends buffers after given amount of failed dq's
	const int ANativeWindowDisplayAdapter::FAILED_DQS_TO_SUSPEND = 3;


	const char* getPixFormatConstant(const char* parameters_format)
	{
		const char* pixFormat;

		if ( parameters_format != NULL )
		{
			if (strcmp(parameters_format, (const char *) CameraParameters::PIXEL_FORMAT_YUV422I) == 0)
			{
				LOGINFO("CbYCrY format selected");
				pixFormat = (const char *) CameraParameters::PIXEL_FORMAT_YUV422I;
			}
			else if(strcmp(parameters_format, (const char *) CameraParameters::PIXEL_FORMAT_YUV420SP) == 0 ||
					strcmp(parameters_format, (const char *) CameraParameters::PIXEL_FORMAT_YUV420P) == 0)
			{
				// TODO(XXX): We are treating YV12 the same as YUV420SP
				LOGINFO("YUV420SP format selected");
				pixFormat = (const char *) CameraParameters::PIXEL_FORMAT_YUV420SP;
			}
			else if(strcmp(parameters_format, (const char *) CameraParameters::PIXEL_FORMAT_RGB565) == 0)
			{
				LOGINFO("RGB565 format selected");
				pixFormat = (const char *) CameraParameters::PIXEL_FORMAT_RGB565;
			}
			else
			{
				LOGINFO("Invalid format, CbYCrY format selected as default");
				pixFormat = (const char *) CameraParameters::PIXEL_FORMAT_YUV422I;
			}
		}
		else
		{
			LOGINFO("Preview format is NULL, defaulting to CbYCrY");
			pixFormat = (const char *) CameraParameters::PIXEL_FORMAT_YUV422I;
		}

		return pixFormat;
	}

	const size_t getBufSize(const char* parameters_format, int width, int height)
	{
		int buf_size;

		if ( parameters_format != NULL ) {
			if (strcmp(parameters_format,
						(const char *) CameraParameters::PIXEL_FORMAT_YUV422I) == 0) {
				buf_size = width * height * 2;
			}
			else if((strcmp(parameters_format, CameraParameters::PIXEL_FORMAT_YUV420SP) == 0) ||
					(strcmp(parameters_format, CameraParameters::PIXEL_FORMAT_YUV420P) == 0)) {
				buf_size = width * height * 3 / 2;
			}
			else if(strcmp(parameters_format,
						(const char *) CameraParameters::PIXEL_FORMAT_RGB565) == 0) {
				buf_size = width * height * 2;
			} else {
				LOGINFO("Invalid format");
				buf_size = 0;
			}
		} else {
			LOGINFO("Preview format is NULL");
			buf_size = 0;
		}

		return buf_size;
	}

	/**
	 * Display Adapter class STARTS here..
	 */
	ANativeWindowDisplayAdapter::ANativeWindowDisplayAdapter():mDisplayThread(NULL),
	mDisplayState(ANativeWindowDisplayAdapter::DISPLAY_INIT),
	mDisplayEnabled(false),
	mBufferCount(0)



	{
		LOG_FUNCTION_NAME;
		mPixelFormat = NULL;
		mBufferHandleMap = NULL;
		mGrallocHandleMap = NULL;
		mOffsetsMap = NULL;
		mFrameProvider = NULL;
		mANativeWindow = NULL;

		mFrameWidth = 0;
		mFrameHeight = 0;
		mPreviewWidth = 0;
		mPreviewHeight = 0;

		mSuspend = false;
		mFailedDQs = 0;

		mPaused = false;
		mXOff = 0;
		mYOff = 0;
		mFirstInit = false;

		mFD = -1;

		LOG_FUNCTION_NAME_EXIT;
	}

	ANativeWindowDisplayAdapter::~ANativeWindowDisplayAdapter()
	{
		Semaphore sem;
		TIUTILS::Message msg;

		LOG_FUNCTION_NAME;

		///If Frame provider exists
		if (mFrameProvider) {
			// Unregister with the frame provider
			mFrameProvider->disableFrameNotification(CameraFrame::ALL_FRAMES);
			delete mFrameProvider;
			mFrameProvider = NULL;
		}

		///The ANativeWindow object will get destroyed here
		destroy();

		///If Display thread exists
		if(mDisplayThread.get())
		{
			///Kill the display thread
			sem.Create();
			msg.command = DisplayThread::DISPLAY_EXIT;

			// Send the semaphore to signal once the command is completed
			msg.arg1 = &sem;

			///Post the message to display thread
			mDisplayThread->msgQ().put(&msg);

			///Wait for the ACK - implies that the thread is now started and waiting for frames
			sem.Wait();

			// Exit and cleanup the thread
			mDisplayThread->requestExitAndWait();

			// Delete the display thread
			mDisplayThread.clear();
		}

		LOG_FUNCTION_NAME_EXIT;

	}

	status_t ANativeWindowDisplayAdapter::initialize()
	{
		LOG_FUNCTION_NAME;

		///Create the display thread
		mDisplayThread = new DisplayThread(this);
		if ( !mDisplayThread.get() )
		{
			LOGINFO("Couldn't create display thread");
			LOG_FUNCTION_NAME_EXIT;
			return NO_MEMORY;
		}

		///Start the display thread
		status_t ret = mDisplayThread->run("DisplayThread", PRIORITY_URGENT_DISPLAY);
		if ( ret != NO_ERROR )
		{
			LOGINFO("Couldn't run display thread");
			LOG_FUNCTION_NAME_EXIT;
			return ret;
		}

		LOG_FUNCTION_NAME_EXIT;

		return ret;
	}

	int ANativeWindowDisplayAdapter::setPreviewWindow(preview_stream_ops_t* window)
	{
		LOG_FUNCTION_NAME;
		///Note that Display Adapter cannot work without a valid window object
		if ( !window)
		{
			LOGINFO("NULL window object passed to DisplayAdapter");
			LOG_FUNCTION_NAME_EXIT;
			return BAD_VALUE;
		}

		///Destroy the existing window object, if it exists
		destroy();

		///Move to new window obj
		mANativeWindow = window;

		LOG_FUNCTION_NAME_EXIT;

		return NO_ERROR;
	}

	int ANativeWindowDisplayAdapter::setFrameProvider(FrameNotifier *frameProvider)
	{
		LOG_FUNCTION_NAME;

		// Check for NULL pointer
		if ( !frameProvider ) {
			LOGINFO("NULL passed for frame provider");
			LOG_FUNCTION_NAME_EXIT;
			return BAD_VALUE;
		}

		//Release any previous frame providers
		if ( NULL != mFrameProvider ) {
			delete mFrameProvider;
		}

		/** Dont do anything here, Just save the pointer for use when display is
		  actually enabled or disabled
		  */
		mFrameProvider = new FrameProvider(frameProvider, this, frameCallbackRelay);

		LOG_FUNCTION_NAME_EXIT;

		return NO_ERROR;
	}

	int ANativeWindowDisplayAdapter::setErrorHandler(ErrorNotifier *errorNotifier)
	{
		status_t ret = NO_ERROR;

		LOG_FUNCTION_NAME;

		if ( NULL == errorNotifier )
		{
			LOGINFO("Invalid Error Notifier reference");
			ret = -EINVAL;
		}

		if ( NO_ERROR == ret )
		{
			mErrorNotifier = errorNotifier;
		}

		LOG_FUNCTION_NAME_EXIT;

		return ret;
	}

	int ANativeWindowDisplayAdapter::enableDisplay(int width, int height, struct timeval *refTime, S3DParameters *s3dParams)
	{
		Semaphore sem;
		TIUTILS::Message msg;

		LOG_FUNCTION_NAME;

		if ( mDisplayEnabled )
		{
			LOGINFO("Display is already enabled");
			LOG_FUNCTION_NAME_EXIT;

			return NO_ERROR;
		}

		//Send START_DISPLAY COMMAND to display thread. Display thread will start and then wait for a message
		sem.Create();
		msg.command = DisplayThread::DISPLAY_START;

		// Send the semaphore to signal once the command is completed
		msg.arg1 = &sem;

		///Post the message to display thread
		mDisplayThread->msgQ().put(&msg);

		///Wait for the ACK - implies that the thread is now started and waiting for frames
		sem.Wait();

		// Register with the frame provider for frames
		mFrameProvider->enableFrameNotification(CameraFrame::PREVIEW_FRAME_SYNC);

		mDisplayEnabled = true;
		mPreviewWidth = width;
		mPreviewHeight = height;

		LOGINFO("mPreviewWidth = %d mPreviewHeight = %d", mPreviewWidth, mPreviewHeight);

		LOG_FUNCTION_NAME_EXIT;

		return NO_ERROR;
	}

	int ANativeWindowDisplayAdapter::disableDisplay(bool cancel_buffer)
	{
		LOG_FUNCTION_NAME;

		status_t ret = NO_ERROR;
		GraphicBufferMapper &mapper = GraphicBufferMapper::get();

		if(!mDisplayEnabled)
		{
			LOGINFO("Display is already disabled");
			LOG_FUNCTION_NAME_EXIT;
			return ALREADY_EXISTS;
		}

		// Unregister with the frame provider here
		mFrameProvider->disableFrameNotification(CameraFrame::PREVIEW_FRAME_SYNC);
		mFrameProvider->removeFramePointers();

		if ( NULL != mDisplayThread.get() )
		{
			//Send STOP_DISPLAY COMMAND to display thread. Display thread will stop and dequeue all messages
			// and then wait for message
			Semaphore sem;
			sem.Create();
			TIUTILS::Message msg;
			msg.command = DisplayThread::DISPLAY_STOP;

			// Send the semaphore to signal once the command is completed
			msg.arg1 = &sem;

			///Post the message to display thread
			mDisplayThread->msgQ().put(&msg);

			///Wait for the ACK for display to be disabled

			sem.Wait();

		}

		Mutex::Autolock lock(mLock);
		{
			///Reset the display enabled flag
			mDisplayEnabled = false;

			///Reset the offset values
			mXOff = 0;
			mYOff = 0;

			///Reset the frame width and height values
			mFrameWidth =0;
			mFrameHeight = 0;
			mPreviewWidth = 0;
			mPreviewHeight = 0;

			if(cancel_buffer)
			{
				// Return the buffers to ANativeWindow here, the mFramesWithCameraAdapterMap is also cleared inside
				returnBuffersToWindow();
			}
			else
			{
				mANativeWindow = NULL;
				// Clear the frames with camera adapter map
				mFramesWithCameraAdapterMap.clear();
			}
		}
		LOG_FUNCTION_NAME_EXIT;

		return NO_ERROR;
	}

	status_t ANativeWindowDisplayAdapter::pauseDisplay(bool pause)
	{
		status_t ret = NO_ERROR;

		LOG_FUNCTION_NAME;

		{
			Mutex::Autolock lock(mLock);
			mPaused = pause;
		}

		LOG_FUNCTION_NAME_EXIT;

		return ret;
	}


	void ANativeWindowDisplayAdapter::destroy()
	{
		LOG_FUNCTION_NAME;

		///Check if the display is disabled, if not disable it
		if ( mDisplayEnabled )
		{
			LOGINFO("WARNING: Calling destroy of Display adapter when display enabled. Disabling display..");
			disableDisplay(false);
		}

		mBufferCount = 0;

		LOG_FUNCTION_NAME_EXIT;
	}

	// Implementation of inherited interfaces
	void* ANativeWindowDisplayAdapter::allocateBuffer(int width, int height,
			const char* format, int &bytes, int numBufs) {
		LOG_FUNCTION_NAME;
		status_t err;
		int i = -1;
		const int lnumBufs = numBufs;
		mBufferHandleMap = new buffer_handle_t*[lnumBufs];
		mGrallocHandleMap = new IMG_native_handle_t*[lnumBufs];
		int undequeued = 0;
		GraphicBufferMapper &mapper = GraphicBufferMapper::get();
		Rect bounds;

		if (NULL == mANativeWindow) {
			return NULL;
		}

		// Set gralloc usage bits for window.
		err = mANativeWindow->set_usage(mANativeWindow, CAMHAL_GRALLOC_USAGE);
		if (err != 0) {
			LOGINFO("native_window_set_usage failed: %s (%d)", strerror(-err), -err);

			if (ENODEV == err) {
				LOGINFO("Preview surface abandoned!");
				mANativeWindow = NULL;
			}
			return NULL;
		}

		LOGINFO("Number of buffers set to ANativeWindow %d", numBufs);
		///Set the number of buffers needed for camera preview
		err = mANativeWindow->set_buffer_count(mANativeWindow, numBufs);
		if (err != 0) {
			LOGINFO("set_buffer_count failed: %s (%d)", strerror(-err), -err);

			if (ENODEV == err) {
				LOGINFO("Preview surface abandoned!");
				mANativeWindow = NULL;
			}

			return NULL;
		}
		LOGINFO("Configuring %d buffers for ANativeWindow", numBufs);
		mBufferCount = numBufs;

		// Set window geometry
		err = mANativeWindow->set_buffers_geometry(mANativeWindow,
				width,
				height,
				ANDROID_HAL_PIXEL_FORMAT_YCbCr_422_I
				); //current camera is YUYV,which same as YUY2

		if (err != 0) {
			LOGINFO("native_window_set_buffers_geometry failed: %s (%d)",
					strerror(-err), -err);

			if (ENODEV == err) {
				LOGINFO("Preview surface abandoned!");
				mANativeWindow = NULL;
			}

			return NULL;
		}

		///We just return the buffers from ANativeWindow, if the width and height are same, else (vstab, vnf case)
		///re-allocate buffers using ANativeWindow and then get them
		///@todo - Re-allocate buffers for vnf and vstab using the width, height, format, numBufs etc
		if (mBufferHandleMap == NULL) {
			LOGINFO("Couldn't create array for ANativeWindow buffers");
			LOG_FUNCTION_NAME_EXIT;
			return NULL;
		}
		bytes = getBufSize(format, width, height);

		mANativeWindow->get_min_undequeued_buffer_count(mANativeWindow, &undequeued);
		LOGINFO("mBufferCount %d, undequeued %d\n", mBufferCount, undequeued);

		// lock the initial queueable buffers
		bounds.left = 0;
		bounds.top = 0;
		bounds.right = width;
		bounds.bottom = height;

		for (i = 0; i < mBufferCount; i++) {
			buffer_handle_t* buf;
			int stride;
			void * y_uv = NULL;

			err = mANativeWindow->dequeue_buffer(mANativeWindow, &buf, &stride);

			if (err != 0) {
				LOGINFO("dequeueBuffer failed: %s (%d)", strerror(-err), -err);
				if (ENODEV == err) {
					LOGINFO("Preview surface abandoned!");
					mANativeWindow = NULL;
				}
				goto fail;
			}

			mBufferHandleMap[i] = buf;

			//if(i < mBufferCount - undequeued){
			if(true){
				mapper.lock((buffer_handle_t) *mBufferHandleMap[i], CAMHAL_GRALLOC_USAGE, bounds, &y_uv);
				mGrallocHandleMap[i] = (IMG_native_handle_t*)y_uv;
				mANativeWindow->lock_buffer(mANativeWindow, mBufferHandleMap[i]);
				mFramesWithCameraAdapterMap.add((int) mGrallocHandleMap[i], i);
				mFrameProvider->addFramePointers((void*)mGrallocHandleMap[i] , NULL);
			}
			else{
				mANativeWindow->cancel_buffer(mANativeWindow, mBufferHandleMap[i]);
				//mFramesWithCameraAdapterMap.removeItem((int) mGrallocHandleMap[i]);
				mapper.unlock((buffer_handle_t) *mBufferHandleMap[i]);
			}
		}

		mFirstInit = true;
		mPixelFormat = getPixFormatConstant(format);
		mFrameWidth = width;
		mFrameHeight = height;

		return mGrallocHandleMap;

fail:
		// need to cancel buffers if any were dequeued
		for (int start = 0; start < i && i > 0; start++) {
			int err = mANativeWindow->cancel_buffer(mANativeWindow,
					mBufferHandleMap[start]);
			if (err != 0) {
				LOGINFO("cancelBuffer failed w/ error 0x%08x", err);
				break;
			}
			mFramesWithCameraAdapterMap.removeItem((int) mGrallocHandleMap[start]);
		}

		freeBuffers(mGrallocHandleMap);

		LOGINFO("Error occurred, performing cleanup");

		if (NULL != mErrorNotifier.get()) {
			mErrorNotifier->errorNotify(-ENOMEM);
		}

		LOG_FUNCTION_NAME_EXIT;
		return NULL;

	}

	int ANativeWindowDisplayAdapter::freeBuffers(void* buf)
	{
		LOG_FUNCTION_NAME;

		int *buffers = (int *) buf;
		status_t ret = NO_ERROR;

		Mutex::Autolock lock(mLock);

		if((int *)mGrallocHandleMap != buffers)
		{
			LOGINFO("CameraHal passed wrong set of buffers to free!!!");
			if (mGrallocHandleMap != NULL)
				delete []mGrallocHandleMap;
			mGrallocHandleMap = NULL;
		}


		returnBuffersToWindow();

		if ( NULL != buf )
		{
			delete [] buffers;
			mGrallocHandleMap = NULL;
		}

		if( mBufferHandleMap != NULL)
		{
			delete [] mBufferHandleMap;
			mBufferHandleMap = NULL;
		}

		if ( NULL != mOffsetsMap )
		{
			delete [] mOffsetsMap;
			mOffsetsMap = NULL;
		}

		if( mFD != -1)
		{
			close(mFD);  // close duped handle
			mFD = -1;
		}

		LOG_FUNCTION_NAME_EXIT;
		return NO_ERROR;
	}

	status_t ANativeWindowDisplayAdapter::returnBuffersToWindow()
	{
		LOG_FUNCTION_NAME;

		status_t ret = NO_ERROR;
		GraphicBufferMapper &mapper = GraphicBufferMapper::get();
		//Give the buffers back to display here -  sort of free it
		if (mANativeWindow){
			for(unsigned int i = 0; i < mBufferCount; i++) {

				LOGE("returnBuffersToWindow i %d\n", i);

				mapper.unlock((buffer_handle_t) *mBufferHandleMap[i]);
				ret = mANativeWindow->cancel_buffer(mANativeWindow, mBufferHandleMap[i]);

				if ( ENODEV == ret ) {
					LOGINFO("Preview surface abandoned!");
					mANativeWindow = NULL;
					return -ret;
				} else if ( NO_ERROR != ret ) {
					LOGINFO("cancel_buffer() failed: %s (%d)",
							strerror(-ret),
							-ret);
					return -ret;
				}
			}
		}
		else{
			LOGINFO("mANativeWindow is NULL");
		}

		mFramesWithCameraAdapterMap.clear();
		LOG_FUNCTION_NAME_EXIT;
		return ret;

	}

	uint32_t * ANativeWindowDisplayAdapter::getOffsets()
	{
		const int lnumBufs = mBufferCount;

		LOG_FUNCTION_NAME;

		// TODO(XXX): Need to remove getOffsets from the API. No longer needed

		if ( NULL == mANativeWindow )
		{
			LOGINFO("mANativeWindow reference is missing");
			goto fail;
		}

		if( mBufferHandleMap == NULL)
		{
			LOGINFO("Buffers not allocated yet!!");
			goto fail;
		}

		if(mOffsetsMap == NULL)
		{
			mOffsetsMap = new uint32_t[lnumBufs];
			for(int i = 0; i < mBufferCount; i++)
			{
				IMG_native_handle_t* handle =  (IMG_native_handle_t*) *(mBufferHandleMap[i]);
				mOffsetsMap[i] = 0;
			}
		}

		LOG_FUNCTION_NAME_EXIT;

		return mOffsetsMap;

fail:

		if ( NULL != mOffsetsMap )
		{
			delete [] mOffsetsMap;
			mOffsetsMap = NULL;
		}

		if ( NULL != mErrorNotifier.get() )
		{
			mErrorNotifier->errorNotify(-ENOSYS);
		}

		LOG_FUNCTION_NAME_EXIT;

		return NULL;
	}

	int ANativeWindowDisplayAdapter::maxQueueableBuffers(unsigned int& queueable)
	{
		LOG_FUNCTION_NAME;
		int ret = NO_ERROR;
		int undequeued = 0;

		if(mBufferCount == 0)
		{
			ret = -ENOSYS;
			goto end;
		}

		if(!mANativeWindow)
		{
			ret = -ENOSYS;
			goto end;
		}

		ret = mANativeWindow->get_min_undequeued_buffer_count(mANativeWindow, &undequeued);
		if ( NO_ERROR != ret ) {
			LOGINFO("get_min_undequeued_buffer_count failed: %s (%d)", strerror(-ret), -ret);

			if ( ENODEV == ret ) {
				LOGINFO("Preview surface abandoned!");
				mANativeWindow = NULL;
			}

			return -ret;
		}

		queueable = mBufferCount - undequeued;

end:
		return ret;
		LOG_FUNCTION_NAME_EXIT;
	}

	int ANativeWindowDisplayAdapter::getFd()
	{
		LOG_FUNCTION_NAME;

		if(mFD == -1)
		{
			IMG_native_handle_t* handle =  (IMG_native_handle_t*) *(mBufferHandleMap[0]);
			// TODO: should we dup the fd? not really necessary and another thing for ANativeWindow
			// to manage and close...
			mFD = dup(handle->fd[0]);
		}

		LOG_FUNCTION_NAME_EXIT;

		return mFD;

	}

	bool ANativeWindowDisplayAdapter::supportsExternalBuffering()
	{
		return false;
	}

	int ANativeWindowDisplayAdapter::useBuffers(void *bufArr, int num)
	{
		return NO_ERROR;
	}

	void ANativeWindowDisplayAdapter::displayThread()
	{
		LOG_FUNCTION_NAME;
		bool shouldLive = true;
		int timeout = 0;
		status_t ret;

		while(shouldLive)
		{
			ret = TIUTILS::MessageQueue::waitForMsg(&mDisplayThread->msgQ()
					,  &mDisplayQ
					, NULL
					, ANativeWindowDisplayAdapter::DISPLAY_TIMEOUT);

			if ( !mDisplayThread->msgQ().isEmpty() )
			{
				///Received a message from CameraHal, process it
				shouldLive = processHalMsg();
			}
			else if( !mDisplayQ.isEmpty())
			{
				if ( mDisplayState== ANativeWindowDisplayAdapter::DISPLAY_INIT )
				{
					///If display adapter is not started, continue
					continue;
				}
				else
				{
					TIUTILS::Message msg;
					///Get the dummy msg from the displayQ
					if(mDisplayQ.get(&msg)!=NO_ERROR)
					{
						LOGINFO("Error in getting message from display Q");
						continue;
					}

					// There is a frame from ANativeWindow for us to dequeue
					// We dequeue and return the frame back to Camera adapter
					if(mDisplayState == ANativeWindowDisplayAdapter::DISPLAY_STARTED)
					{
						handleFrameReturn();
					}

					if (mDisplayState == ANativeWindowDisplayAdapter::DISPLAY_EXITED)
					{
						///we exit the thread even though there are frames still to dequeue. They will be dequeued
						///in disableDisplay
						shouldLive = false;
					}
				}
			}
		}

		LOG_FUNCTION_NAME_EXIT;
	}


	bool ANativeWindowDisplayAdapter::processHalMsg()
	{
		LOG_FUNCTION_NAME;

		TIUTILS::Message msg;
		mDisplayThread->msgQ().get(&msg);

		bool ret = true, invalidCommand = false;
		switch ( msg.command )
		{
			case DisplayThread::DISPLAY_START:
				LOGINFO("Display thread received DISPLAY_START command from Camera HAL");
				mDisplayState = ANativeWindowDisplayAdapter::DISPLAY_STARTED;
				break;
			case DisplayThread::DISPLAY_STOP:
				///@bug There is no API to disable SF without destroying it
				///@bug Buffers might still be w/ display and will get displayed
				///@remarks Ideal seqyence should be something like this
				///mOverlay->setParameter("enabled", false);
				LOGINFO("Display thread received DISPLAY_STOP command from Camera HAL");
				mDisplayState = ANativeWindowDisplayAdapter::DISPLAY_STOPPED;
				break;
			case DisplayThread::DISPLAY_EXIT:
				LOGINFO("Display thread received DISPLAY_EXIT command from Camera HAL.");
				LOGINFO("Stopping display thread...");
				mDisplayState = ANativeWindowDisplayAdapter::DISPLAY_EXITED;
				///Note that the SF can have pending buffers when we disable the display
				///This is normal and the expectation is that they may not be displayed.
				///This is to ensure that the user experience is not impacted
				ret = false;
				break;
			default:
				LOGINFO("Invalid Display Thread Command 0x%x.", msg.command);
				invalidCommand = true;
				break;
		}

		///Signal the semaphore if it is sent as part of the message
		if ( ( msg.arg1 ) && ( !invalidCommand ) )
		{
			LOGINFO("+Signalling display semaphore");
			Semaphore &sem = *((Semaphore*)msg.arg1);
			sem.Signal();
			LOGINFO("-Signalling display semaphore");
		}

		LOG_FUNCTION_NAME_EXIT;
		return ret;
	}

	bool ANativeWindowDisplayAdapter::handleFrameReturn() {
		LOG_FUNCTION_NAME;

		status_t err;
		buffer_handle_t* buf;
		int i = 0;
		int stride; // dummy variable to get stride
		GraphicBufferMapper &mapper = GraphicBufferMapper::get();
		Rect bounds;
		void *y_uv;

		if (NULL == mANativeWindow) {
			return false;
		}

		err = mANativeWindow->dequeue_buffer(mANativeWindow, &buf, &stride);
		if (err != 0) {
			LOGINFO("dequeueBuffer failed: %s (%d)", strerror(-err), -err);

			if (ENODEV == err) {
				LOGINFO("Preview surface abandoned!");
				mANativeWindow = NULL;
			}
			return false;
		}

		err = mANativeWindow->lock_buffer(mANativeWindow, buf);
		if (err != 0) {
			LOGINFO("lockbuffer failed: %s (%d)", strerror(-err), -err);

			if (ENODEV == err) {
				LOGINFO("Preview surface abandoned!");
				mANativeWindow = NULL;
			}

			return false;
		}

		for (i = 0; i < mBufferCount; i++) {
			if (mBufferHandleMap[i] == buf)
				break;
		}
		LOGINFO("HandleFrameReturn index %d\n", i);

		if(i >= mBufferCount){
			LOGINFO("Error!! index >= mBufferCount!\n");
			return -EINVAL;
		}

		// lock buffer before sending to FrameProvider for filling
		bounds.left = 0;
		bounds.top = 0;
		bounds.right = mFrameWidth;
		bounds.bottom = mFrameHeight;

		int lock_try_count = 0;
		while (mapper.lock((buffer_handle_t) *mBufferHandleMap[i],
					CAMHAL_GRALLOC_USAGE, bounds, &y_uv) < 0) {
			if (++lock_try_count > LOCK_BUFFER_TRIES) {
				if (NULL != mErrorNotifier.get()) {
					//mErrorNotifier->errorNotify(CAMERA_ERROR_UNKNOWN);
				}
				return false;
			}
			LOGINFO("Gralloc Lock FrameReturn Error: Sleeping 15ms");
			usleep(15000);
		}

		mGrallocHandleMap[i] = (IMG_native_handle_t*)y_uv;
		mFramesWithCameraAdapterMap.add((int) mGrallocHandleMap[i], i);
		mFrameProvider->returnFrame( (void*)mGrallocHandleMap[i], CameraFrame::PREVIEW_FRAME_SYNC);

		LOGINFO("handleFrameReturn: found graphic buffer %d of %d", i,
				mBufferCount - 1);

		LOG_FUNCTION_NAME_EXIT;
		return true;
	}

	status_t ANativeWindowDisplayAdapter::postFrame(
			ANativeWindowDisplayAdapter::DisplayFrame &dispFrame) {
		LOG_FUNCTION_NAME;

		status_t ret = NO_ERROR;
		uint32_t actualFramesWithDisplay = 0;
		android_native_buffer_t *buffer = NULL;
		GraphicBufferMapper &mapper = GraphicBufferMapper::get();
		int index;

		if (!mGrallocHandleMap || !dispFrame.mBuffer) {
			LOGINFO("NULL sent to postFrame");
			return -EINVAL;
		}
		LOGINFO("mPaused %d, mSuspend %d", mPaused, mSuspend);

		for ( index = 0; index < mBufferCount; index++ )
		{
			if ( ((int) dispFrame.mBuffer ) == (int)mGrallocHandleMap[index] )
			{
				break;
			}
		}
		
		LOGINFO("postFrame index %d\n", index);
		if(index >= mBufferCount){
			LOGINFO("Error!! index >= mBufferCount!\n");
			return -EINVAL;
		}

		// unlock buffer before sending to display
		mapper.unlock((buffer_handle_t) *mBufferHandleMap[index]);
		ret = mANativeWindow->enqueue_buffer(mANativeWindow,
				mBufferHandleMap[index]);
		if (ret != 0) {
			LOGINFO("Surface::queueBuffer returned error %d", ret);
		}

		mFramesWithCameraAdapterMap.removeItem((int) dispFrame.mBuffer);

		TIUTILS::Message msg;
		mDisplayQ.put(&msg);

		ret = NO_ERROR;
		LOG_FUNCTION_NAME_EXIT;

		return ret;
	}

	void ANativeWindowDisplayAdapter::frameCallbackRelay(CameraFrame* cameraFrame)
	{
		LOG_FUNCTION_NAME;

		if ( NULL != cameraFrame )
		{
			if ( NULL != cameraFrame->mCookie )
			{
				ANativeWindowDisplayAdapter *da = (ANativeWindowDisplayAdapter*) cameraFrame->mCookie;
				da->frameCallback(cameraFrame);
			}
			else
			{
				LOGINFO("Invalid Cookie in Camera Frame = %p, Cookie = %p", cameraFrame, cameraFrame->mCookie);
			}
		}
		else
		{
			LOGINFO("Invalid Camera Frame = %p", cameraFrame);
		}
		LOG_FUNCTION_NAME_EXIT;
	}

	void ANativeWindowDisplayAdapter::frameCallback(CameraFrame* cameraFrame)
	{
		LOG_FUNCTION_NAME;

		DisplayFrame df;
		df.mBuffer = cameraFrame->mBuffer;
		df.mType = (CameraFrame::FrameType) cameraFrame->mFrameType;
		df.mOffset = cameraFrame->mOffset;
		df.mWidthStride = cameraFrame->mAlignment;
		df.mLength = cameraFrame->mLength;
		df.mWidth = cameraFrame->mWidth;
		df.mHeight = cameraFrame->mHeight;
		postFrame(df);

		LOG_FUNCTION_NAME_EXIT;
	}
}
;

