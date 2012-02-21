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
* @file OMXCapture.cpp
*
* This file contains functionality for handling image capture.
*
*/

#undef LOG_TAG

#define LOG_TAG "CameraHAL"

#include "CameraHal.h"
#include "OMXCameraAdapter.h"
#include "ErrorUtils.h"


namespace android {

status_t OMXCameraAdapter::setParametersCapture(const CameraParameters &params,
                                                BaseCameraAdapter::AdapterState state)
{
    status_t ret = NO_ERROR;
    const char *str = NULL;
    int w, h;
    OMX_COLOR_FORMATTYPE pixFormat;
    const char *valstr = NULL;

    LOG_FUNCTION_NAME;

    OMXCameraPortParameters *cap;
    cap = &mCameraAdapterParameters.mCameraPortParams[mCameraAdapterParameters.mImagePortIndex];

    params.getPictureSize(&w, &h);

    if ( ( w != ( int ) cap->mWidth ) ||
          ( h != ( int ) cap->mHeight ) )
        {
        mPendingCaptureSettings |= SetFormat;
        }

    cap->mWidth = w;
    cap->mHeight = h;
    //TODO: Support more pixelformats
    cap->mStride = 2;

    LOGE("Image: cap.mWidth = %d", (int)cap->mWidth);
    LOGE("Image: cap.mHeight = %d", (int)cap->mHeight);

    if ( (valstr = params.getPictureFormat()) != NULL )
        {
        if (strcmp(valstr, (const char *) CameraParameters::PIXEL_FORMAT_YUV422I) == 0)
            {
            LOGE("CbYCrY format selected");
            pixFormat = OMX_COLOR_FormatCbYCrY;
            }
        else if(strcmp(valstr, (const char *) CameraParameters::PIXEL_FORMAT_YUV420SP) == 0)
            {
            LOGE("YUV420SP format selected");
            pixFormat = OMX_COLOR_FormatYUV420SemiPlanar;
            }
        else if(strcmp(valstr, (const char *) CameraParameters::PIXEL_FORMAT_RGB565) == 0)
            {
            LOGE("RGB565 format selected");
            pixFormat = OMX_COLOR_Format16bitRGB565;
            }
        else if(strcmp(valstr, (const char *) CameraParameters::PIXEL_FORMAT_JPEG) == 0)
            {
            LOGE("JPEG format selected");
            pixFormat = OMX_COLOR_FormatUnused;
            mCodingMode = CodingNone;
            }
        else if(strcmp(valstr, (const char *) TICameraParameters::PIXEL_FORMAT_JPS) == 0)
            {
            LOGE("JPS format selected");
            pixFormat = OMX_COLOR_FormatUnused;
            mCodingMode = CodingJPS;
            }
        else if(strcmp(valstr, (const char *) TICameraParameters::PIXEL_FORMAT_MPO) == 0)
            {
            LOGE("MPO format selected");
            pixFormat = OMX_COLOR_FormatUnused;
            mCodingMode = CodingMPO;
            }
        else if(strcmp(valstr, (const char *) TICameraParameters::PIXEL_FORMAT_RAW_JPEG) == 0)
            {
            LOGE("RAW + JPEG format selected");
            pixFormat = OMX_COLOR_FormatUnused;
            mCodingMode = CodingRAWJPEG;
            }
        else if(strcmp(valstr, (const char *) TICameraParameters::PIXEL_FORMAT_RAW_MPO) == 0)
            {
            LOGE("RAW + MPO format selected");
            pixFormat = OMX_COLOR_FormatUnused;
            mCodingMode = CodingRAWMPO;
            }
        else if(strcmp(valstr, (const char *) TICameraParameters::PIXEL_FORMAT_RAW) == 0)
            {
            LOGE("RAW Picture format selected");
            pixFormat = OMX_COLOR_FormatRawBayer10bit;
            }
        else
            {
            LOGE("Invalid format, JPEG format selected as default");
            pixFormat = OMX_COLOR_FormatUnused;
            }
        }
    else
        {
        LOGE("Picture format is NULL, defaulting to JPEG");
        pixFormat = OMX_COLOR_FormatUnused;
        }

    // JPEG capture is not supported in video mode by OMX Camera
    // Set capture format to yuv422i...jpeg encode will
    // be done on A9
    valstr = params.get(TICameraParameters::KEY_CAP_MODE);
    if ( (valstr && !strcmp(valstr, (const char *) TICameraParameters::VIDEO_MODE)) &&
         (pixFormat == OMX_COLOR_FormatUnused) ) {
        LOGE("Capturing in video mode...selecting yuv422i");
        pixFormat = OMX_COLOR_FormatCbYCrY;
    }

    if ( pixFormat != cap->mColorFormat )
        {
        mPendingCaptureSettings |= SetFormat;
        cap->mColorFormat = pixFormat;
        }

    str = params.get(TICameraParameters::KEY_EXP_BRACKETING_RANGE);
    if ( NULL != str ) {
        parseExpRange(str, mExposureBracketingValues, EXP_BRACKET_RANGE, mExposureBracketingValidEntries);
    } else {
        // if bracketing was previously set...we set again before capturing to clear
        if (mExposureBracketingValidEntries) mPendingCaptureSettings |= SetExpBracket;
        mExposureBracketingValidEntries = 0;
    }

    if ( params.getInt(CameraParameters::KEY_ROTATION) != -1 )
        {
        if (params.getInt(CameraParameters::KEY_ROTATION) != mPictureRotation) {
            mPendingCaptureSettings |= SetRotation;
        }
        mPictureRotation = params.getInt(CameraParameters::KEY_ROTATION);
        }
    else
        {
        if (mPictureRotation) mPendingCaptureSettings |= SetRotation;
        mPictureRotation = 0;
        }

    LOGE("Picture Rotation set %d", mPictureRotation);

    // Read Sensor Orientation and set it based on perating mode

     if (( params.getInt(TICameraParameters::KEY_SENSOR_ORIENTATION) != -1 ) && (mCapMode == OMXCameraAdapter::VIDEO_MODE))
        {
         mSensorOrientation = params.getInt(TICameraParameters::KEY_SENSOR_ORIENTATION);
         if (mSensorOrientation == 270 ||mSensorOrientation==90)
             {
             LOGE(" Orientation is 270/90. So setting counter rotation  to Ducati");
             mSensorOrientation +=180;
             mSensorOrientation%=360;
              }
         }
     else
        {
         mSensorOrientation = 0;
        }

     LOGE("Sensor Orientation  set : %d", mSensorOrientation);

    if ( params.getInt(TICameraParameters::KEY_BURST)  >= 1 )
        {
        if (params.getInt(TICameraParameters::KEY_BURST) != mBurstFrames) {
            mPendingCaptureSettings |= SetExpBracket;
        }
        mBurstFrames = params.getInt(TICameraParameters::KEY_BURST);
        }
    else
        {
        if (mBurstFrames != 1) mPendingCaptureSettings |= SetExpBracket;
        mBurstFrames = 1;
        }

    LOGE("Burst Frames set %d", mBurstFrames);

    if ( ( params.getInt(CameraParameters::KEY_JPEG_QUALITY)  >= MIN_JPEG_QUALITY ) &&
         ( params.getInt(CameraParameters::KEY_JPEG_QUALITY)  <= MAX_JPEG_QUALITY ) )
        {
        if (params.getInt(CameraParameters::KEY_JPEG_QUALITY) != mPictureQuality) {
            mPendingCaptureSettings |= SetQuality;
        }
        mPictureQuality = params.getInt(CameraParameters::KEY_JPEG_QUALITY);
        }
    else
        {
        if (mPictureQuality != MAX_JPEG_QUALITY) mPendingCaptureSettings |= SetQuality;
        mPictureQuality = MAX_JPEG_QUALITY;
        }

    LOGE("Picture Quality set %d", mPictureQuality);

    if ( params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH)  >= 0 )
        {
        if (params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH) != mThumbWidth) {
            mPendingCaptureSettings |= SetThumb;
        }
        mThumbWidth = params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH);
        }
    else
        {
        if (mThumbWidth != DEFAULT_THUMB_WIDTH) mPendingCaptureSettings |= SetThumb;
        mThumbWidth = DEFAULT_THUMB_WIDTH;
        }


    LOGE("Picture Thumb width set %d", mThumbWidth);

    if ( params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT)  >= 0 )
        {
        if (params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT) != mThumbHeight) {
            mPendingCaptureSettings |= SetThumb;
        }
        mThumbHeight = params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT);
        }
    else
        {
        if (mThumbHeight != DEFAULT_THUMB_HEIGHT) mPendingCaptureSettings |= SetThumb;
        mThumbHeight = DEFAULT_THUMB_HEIGHT;
        }


    LOGE("Picture Thumb height set %d", mThumbHeight);

    if ( ( params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY)  >= MIN_JPEG_QUALITY ) &&
         ( params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY)  <= MAX_JPEG_QUALITY ) )
        {
        if (params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY) != mThumbQuality) {
            mPendingCaptureSettings |= SetThumb;
        }
        mThumbQuality = params.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY);
        }
    else
        {
        if (mThumbQuality != MAX_JPEG_QUALITY) mPendingCaptureSettings |= SetThumb;
        mThumbQuality = MAX_JPEG_QUALITY;
        }

    LOGE("Thumbnail Quality set %d", mThumbQuality);

    if (mFirstTimeInit) {
        mPendingCaptureSettings = ECapturesettingsAll;
    }

    if (mPendingCaptureSettings) {
        disableImagePort();
        if ( NULL != mReleaseImageBuffersCallback ) {
            mReleaseImageBuffersCallback(mReleaseData);
        }
    }

    LOG_FUNCTION_NAME_EXIT;

    return ret;
}

status_t OMXCameraAdapter::getPictureBufferSize(size_t &length, size_t bufferCount)
{
    status_t ret = NO_ERROR;
    OMXCameraPortParameters *imgCaptureData = NULL;
    OMX_ERRORTYPE eError = OMX_ErrorNone;

    LOG_FUNCTION_NAME;

    if ( NO_ERROR == ret )
        {
        imgCaptureData = &mCameraAdapterParameters.mCameraPortParams[mCameraAdapterParameters.mImagePortIndex];

        imgCaptureData->mNumBufs = bufferCount;

        // check if image port is already configured...
        // if it already configured then we don't have to query again
        if (!mCaptureConfigured) {
            ret = setFormat(OMX_CAMERA_PORT_IMAGE_OUT_IMAGE, *imgCaptureData);
        }

        if ( ret == NO_ERROR )
            {
            length = imgCaptureData->mBufSize;
            }
        else
            {
            LOGE("setFormat() failed 0x%x", ret);
            length = 0;
            }
        }

    LOGE("getPictureBufferSize %d", length);

    LOG_FUNCTION_NAME_EXIT;

    return ret;
}

status_t OMXCameraAdapter::parseExpRange(const char *rangeStr,
                                         int * expRange,
                                         size_t count,
                                         size_t &validEntries)
{
    status_t ret = NO_ERROR;
    char *ctx, *expVal;
    char *tmp = NULL;
    size_t i = 0;

    LOG_FUNCTION_NAME;

    if ( NULL == rangeStr )
        {
        return -EINVAL;
        }

    if ( NULL == expRange )
        {
        return -EINVAL;
        }

    if ( NO_ERROR == ret )
        {
        tmp = ( char * ) malloc( strlen(rangeStr) + 1 );

        if ( NULL == tmp )
            {
            LOGE("No resources for temporary buffer");
            return -1;
            }
        memset(tmp, '\0', strlen(rangeStr) + 1);

        }

    if ( NO_ERROR == ret )
        {
        strncpy(tmp, rangeStr, strlen(rangeStr) );
        expVal = strtok_r( (char *) tmp, CameraHal::PARAMS_DELIMITER, &ctx);

        i = 0;
        while ( ( NULL != expVal ) && ( i < count ) )
            {
            expRange[i] = atoi(expVal);
            expVal = strtok_r(NULL, CameraHal::PARAMS_DELIMITER, &ctx);
            i++;
            }
        validEntries = i;
        }

    if ( NULL != tmp )
        {
        free(tmp);
        }

    LOG_FUNCTION_NAME_EXIT;

    return ret;
}

status_t OMXCameraAdapter::setExposureBracketing(int *evValues,
                                                 size_t evCount,
                                                 size_t frameCount)
{
    status_t ret = NO_ERROR;
    OMX_ERRORTYPE eError = OMX_ErrorNone;
    OMX_CONFIG_CAPTUREMODETYPE expCapMode;
    OMX_CONFIG_EXTCAPTUREMODETYPE extExpCapMode;

    LOG_FUNCTION_NAME;

    if ( OMX_StateInvalid == mComponentState )
        {
        LOGE("OMX component is in invalid state");
        ret = -EINVAL;
        }

    if ( NULL == evValues )
        {
        LOGE("Exposure compensation values pointer is invalid");
        ret = -EINVAL;
        }

    if ( NO_ERROR == ret )
        {
        OMX_INIT_STRUCT_PTR (&expCapMode, OMX_CONFIG_CAPTUREMODETYPE);
        expCapMode.nPortIndex = mCameraAdapterParameters.mImagePortIndex;

        /// If frameCount>0 but evCount<=0, then this is the case of HQ burst.
        //Otherwise, it is normal HQ capture
        ///If frameCount>0 and evCount>0 then this is the cause of HQ Exposure bracketing.
        if ( 0 == evCount && 0 == frameCount )
            {
            expCapMode.bFrameLimited = OMX_FALSE;
            }
        else
            {
            expCapMode.bFrameLimited = OMX_TRUE;
            expCapMode.nFrameLimit = frameCount;
            }

        eError =  OMX_SetConfig(mCameraAdapterParameters.mHandleComp,
                                OMX_IndexConfigCaptureMode,
                                &expCapMode);
        if ( OMX_ErrorNone != eError )
            {
            LOGE("Error while configuring capture mode 0x%x", eError);
            }
        else
            {
            LOGE("Camera capture mode configured successfully");
            }
        }

    if ( NO_ERROR == ret )
        {
        OMX_INIT_STRUCT_PTR (&extExpCapMode, OMX_CONFIG_EXTCAPTUREMODETYPE);
        extExpCapMode.nPortIndex = mCameraAdapterParameters.mImagePortIndex;

        if ( 0 == evCount )
            {
            extExpCapMode.bEnableBracketing = OMX_FALSE;
            }
        else
            {
            extExpCapMode.bEnableBracketing = OMX_TRUE;
            extExpCapMode.tBracketConfigType.eBracketMode = OMX_BracketExposureRelativeInEV;
            extExpCapMode.tBracketConfigType.nNbrBracketingValues = evCount - 1;
            }

        for ( unsigned int i = 0 ; i < evCount ; i++ )
            {
            extExpCapMode.tBracketConfigType.nBracketValues[i]  =  ( evValues[i] * ( 1 << Q16_OFFSET ) )  / 10;
            }

        eError =  OMX_SetConfig(mCameraAdapterParameters.mHandleComp,
                                ( OMX_INDEXTYPE ) OMX_IndexConfigExtCaptureMode,
                                &extExpCapMode);
        if ( OMX_ErrorNone != eError )
            {
            LOGE("Error while configuring extended capture mode 0x%x", eError);
            }
        else
            {
            LOGE("Extended camera capture mode configured successfully");
            }
        }

    LOG_FUNCTION_NAME_EXIT;

    return ret;
}

status_t OMXCameraAdapter::setShutterCallback(bool enabled)
{
    status_t ret = NO_ERROR;
    OMX_ERRORTYPE eError = OMX_ErrorNone;
    OMX_CONFIG_CALLBACKREQUESTTYPE shutterRequstCallback;

    LOG_FUNCTION_NAME;

    if ( OMX_StateExecuting != mComponentState )
        {
        LOGE("OMX component not in executing state");
        ret = -1;
        }

    if ( NO_ERROR == ret )
        {

        OMX_INIT_STRUCT_PTR (&shutterRequstCallback, OMX_CONFIG_CALLBACKREQUESTTYPE);
        shutterRequstCallback.nPortIndex = OMX_ALL;

        if ( enabled )
            {
            shutterRequstCallback.bEnable = OMX_TRUE;
            shutterRequstCallback.nIndex = ( OMX_INDEXTYPE ) OMX_TI_IndexConfigShutterCallback;
            LOGE("Enabling shutter callback");
            }
        else
            {
            shutterRequstCallback.bEnable = OMX_FALSE;
            shutterRequstCallback.nIndex = ( OMX_INDEXTYPE ) OMX_TI_IndexConfigShutterCallback;
            LOGE("Disabling shutter callback");
            }

        eError =  OMX_SetConfig(mCameraAdapterParameters.mHandleComp,
                                ( OMX_INDEXTYPE ) OMX_IndexConfigCallbackRequest,
                                &shutterRequstCallback);
        if ( OMX_ErrorNone != eError )
            {
            LOGE("Error registering shutter callback 0x%x", eError);
            ret = -1;
            }
        else
            {
            LOGE("Shutter callback for index 0x%x registered successfully",
                         OMX_TI_IndexConfigShutterCallback);
            }
        }

    LOG_FUNCTION_NAME_EXIT;

    return ret;
}

status_t OMXCameraAdapter::doBracketing(OMX_BUFFERHEADERTYPE *pBuffHeader,
                                        CameraFrame::FrameType typeOfFrame)
{
    status_t ret = NO_ERROR;
    int currentBufferIdx, nextBufferIdx;
    OMXCameraPortParameters * imgCaptureData = NULL;

    LOG_FUNCTION_NAME;

    imgCaptureData = &mCameraAdapterParameters.mCameraPortParams[mCameraAdapterParameters.mImagePortIndex];

    if ( OMX_StateExecuting != mComponentState )
        {
        LOGE("OMX component is not in executing state");
        ret = -EINVAL;
        }

    if ( NO_ERROR == ret )
        {
        currentBufferIdx = ( unsigned int ) pBuffHeader->pAppPrivate;

        if ( currentBufferIdx >= imgCaptureData->mNumBufs)
            {
            LOGE("Invalid bracketing buffer index 0x%x", currentBufferIdx);
            ret = -EINVAL;
            }
        }

    if ( NO_ERROR == ret )
        {
        mBracketingBuffersQueued[currentBufferIdx] = false;
        mBracketingBuffersQueuedCount--;

        if ( 0 >= mBracketingBuffersQueuedCount )
            {
            nextBufferIdx = ( currentBufferIdx + 1 ) % imgCaptureData->mNumBufs;
            mBracketingBuffersQueued[nextBufferIdx] = true;
            mBracketingBuffersQueuedCount++;
            mLastBracetingBufferIdx = nextBufferIdx;
            setFrameRefCount(imgCaptureData->mBufferHeader[nextBufferIdx]->pBuffer, typeOfFrame, 1);
            returnFrame(imgCaptureData->mBufferHeader[nextBufferIdx]->pBuffer, typeOfFrame);
            }
        }

    LOG_FUNCTION_NAME_EXIT;

    return ret;
}

status_t OMXCameraAdapter::sendBracketFrames()
{
    status_t ret = NO_ERROR;
    int currentBufferIdx;
    OMXCameraPortParameters * imgCaptureData = NULL;

    LOG_FUNCTION_NAME;

    imgCaptureData = &mCameraAdapterParameters.mCameraPortParams[mCameraAdapterParameters.mImagePortIndex];

    if ( OMX_StateExecuting != mComponentState )
        {
        LOGE("OMX component is not in executing state");
        ret = -EINVAL;
        }

    if ( NO_ERROR == ret )
        {

        currentBufferIdx = mLastBracetingBufferIdx;
        do
            {
            currentBufferIdx++;
            currentBufferIdx %= imgCaptureData->mNumBufs;
            if (!mBracketingBuffersQueued[currentBufferIdx] )
                {
                CameraFrame cameraFrame;
                sendCallBacks(cameraFrame,
                              imgCaptureData->mBufferHeader[currentBufferIdx],
                              imgCaptureData->mImageType,
                              imgCaptureData);
                }
            } while ( currentBufferIdx != mLastBracetingBufferIdx );

        }

    LOG_FUNCTION_NAME_EXIT;

    return ret;
}

status_t OMXCameraAdapter::startBracketing(int range)
{
    status_t ret = NO_ERROR;
    OMXCameraPortParameters * imgCaptureData = NULL;

    LOG_FUNCTION_NAME;

    imgCaptureData = &mCameraAdapterParameters.mCameraPortParams[mCameraAdapterParameters.mImagePortIndex];

    if ( OMX_StateExecuting != mComponentState )
        {
        LOGE("OMX component is not in executing state");
        ret = -EINVAL;
        }

        {
        Mutex::Autolock lock(mBracketingLock);

        if ( mBracketingEnabled )
            {
            return ret;
            }
        }

    if ( 0 == imgCaptureData->mNumBufs )
        {
        LOGE("Image capture buffers set to %d", imgCaptureData->mNumBufs);
        ret = -EINVAL;
        }

    if ( mPending3Asettings )
        apply3Asettings(mParameters3A);

    if ( NO_ERROR == ret )
        {
        Mutex::Autolock lock(mBracketingLock);

        mBracketingRange = range;
        mBracketingBuffersQueued = new bool[imgCaptureData->mNumBufs];
        if ( NULL == mBracketingBuffersQueued )
            {
            LOGE("Unable to allocate bracketing management structures");
            ret = -1;
            }

        if ( NO_ERROR == ret )
            {
            mBracketingBuffersQueuedCount = imgCaptureData->mNumBufs;
            mLastBracetingBufferIdx = mBracketingBuffersQueuedCount - 1;

            for ( int i = 0 ; i  < imgCaptureData->mNumBufs ; i++ )
                {
                mBracketingBuffersQueued[i] = true;
                }

            }
        }

    if ( NO_ERROR == ret )
        {

        ret = startImageCapture();
            {
            Mutex::Autolock lock(mBracketingLock);

            if ( NO_ERROR == ret )
                {
                mBracketingEnabled = true;
                }
            else
                {
                mBracketingEnabled = false;
                }
            }
        }

    LOG_FUNCTION_NAME_EXIT;

    return ret;
}

status_t OMXCameraAdapter::stopBracketing()
{
  status_t ret = NO_ERROR;

    LOG_FUNCTION_NAME;

    Mutex::Autolock lock(mBracketingLock);

    if ( NULL != mBracketingBuffersQueued )
    {
        delete [] mBracketingBuffersQueued;
    }

    ret = stopImageCapture();

    mBracketingBuffersQueued = NULL;
    mBracketingEnabled = false;
    mBracketingBuffersQueuedCount = 0;
    mLastBracetingBufferIdx = 0;

    LOG_FUNCTION_NAME_EXIT;

    return ret;
}

status_t OMXCameraAdapter::startImageCapture()
{
    status_t ret = NO_ERROR;
    OMX_ERRORTYPE eError = OMX_ErrorNone;
    OMXCameraPortParameters * capData = NULL;
    OMX_CONFIG_BOOLEANTYPE bOMX;

    LOG_FUNCTION_NAME;

    if(!mCaptureConfigured)
        {
        ///Image capture was cancelled before we could start
        return NO_ERROR;
        }

    if ( 0 != mStartCaptureSem.Count() )
        {
        LOGE("Error mStartCaptureSem semaphore count %d", mStartCaptureSem.Count());
        return NO_INIT;
        }

    // Camera framework doesn't expect face callbacks once capture is triggered
    pauseFaceDetection(true);

    //During bracketing image capture is already active
    {
    Mutex::Autolock lock(mBracketingLock);
    if ( mBracketingEnabled )
        {
        //Stop bracketing, activate normal burst for the remaining images
        mBracketingEnabled = false;
        mCapturedFrames = mBracketingRange;
        ret = sendBracketFrames();
        if(ret != NO_ERROR)
            goto EXIT;
        else
            return ret;
        }
    }

    if ( NO_ERROR == ret ) {
        if (mPendingCaptureSettings & SetRotation) {
            mPendingCaptureSettings &= ~SetRotation;
            ret = setPictureRotation(mPictureRotation);
            if ( NO_ERROR != ret ) {
                LOGE("Error configuring image rotation %x", ret);
            }
        }
    }

    //OMX shutter callback events are only available in hq mode
    if ( (HIGH_QUALITY == mCapMode) || (HIGH_QUALITY_ZSL== mCapMode))
        {

        if ( NO_ERROR == ret )
            {
            ret = RegisterForEvent(mCameraAdapterParameters.mHandleComp,
                                        (OMX_EVENTTYPE) OMX_EventIndexSettingChanged,
                                        OMX_ALL,
                                        OMX_TI_IndexConfigShutterCallback,
                                        mStartCaptureSem);
            }

        if ( NO_ERROR == ret )
            {
            ret = setShutterCallback(true);
            }

        }

    if ( NO_ERROR == ret ) {
        capData = &mCameraAdapterParameters.mCameraPortParams[mCameraAdapterParameters.mImagePortIndex];

        ///Queue all the buffers on capture port
        for ( int index = 0 ; index < capData->mNumBufs ; index++ ) {
            LOGE("Queuing buffer on Capture port - 0x%x",
                         ( unsigned int ) capData->mBufferHeader[index]->pBuffer);
            eError = OMX_FillThisBuffer(mCameraAdapterParameters.mHandleComp,
                        (OMX_BUFFERHEADERTYPE*)capData->mBufferHeader[index]);

            GOTO_EXIT_IF((eError!=OMX_ErrorNone), eError);
        }

        mWaitingForSnapshot = true;
        mCaptureSignalled = false;

        // Capturing command is not needed when capturing in video mode
        // Only need to queue buffers on image ports
        if (mCapMode != VIDEO_MODE) {
            OMX_INIT_STRUCT_PTR (&bOMX, OMX_CONFIG_BOOLEANTYPE);
            bOMX.bEnabled = OMX_TRUE;

            /// sending Capturing Command to the component
            eError = OMX_SetConfig(mCameraAdapterParameters.mHandleComp,
                                   OMX_IndexConfigCapturing,
                                   &bOMX);

            LOGE("Capture set - 0x%x", eError);

            GOTO_EXIT_IF((eError!=OMX_ErrorNone), eError);
        }
    }

    //OMX shutter callback events are only available in hq mode
    if ( (HIGH_QUALITY == mCapMode) || (HIGH_QUALITY_ZSL== mCapMode))
        {

        if ( NO_ERROR == ret )
            {
            ret = mStartCaptureSem.WaitTimeout(OMX_CAPTURE_TIMEOUT);
            }

        //If somethiing bad happened while we wait
        if (mComponentState == OMX_StateInvalid)
          {
            LOGE("Invalid State after Image Capture Exitting!!!");
            goto EXIT;
          }

        if ( NO_ERROR == ret )
            {
            LOGE("Shutter callback received");
            notifyShutterSubscribers();
            }
        else
            {
            ret |= RemoveEvent(mCameraAdapterParameters.mHandleComp,
                               (OMX_EVENTTYPE) OMX_EventIndexSettingChanged,
                               OMX_ALL,
                               OMX_TI_IndexConfigShutterCallback,
                               NULL);
            LOGE("Timeout expired on shutter callback");
            goto EXIT;
            }

        }

    return (ret | ErrorUtils::omxToAndroidError(eError));

EXIT:
    LOGE("Exiting function %s because of ret %d eError=%x", __FUNCTION__, ret, eError);
    mWaitingForSnapshot = false;
    mCaptureSignalled = false;
    performCleanupAfterError();
    LOG_FUNCTION_NAME_EXIT;
    return (ret | ErrorUtils::omxToAndroidError(eError));
}

status_t OMXCameraAdapter::stopImageCapture()
{
    status_t ret = NO_ERROR;
    OMX_ERRORTYPE eError = OMX_ErrorNone;
    OMX_CONFIG_BOOLEANTYPE bOMX;
    OMXCameraPortParameters *imgCaptureData = NULL;

    LOG_FUNCTION_NAME;

    if (!mCaptureConfigured) {
        //Capture is not ongoing, return from here
        return NO_ERROR;
    }

    if ( 0 != mStopCaptureSem.Count() ) {
        LOGE("Error mStopCaptureSem semaphore count %d", mStopCaptureSem.Count());
        goto EXIT;
    }

    //Disable the callback first
    mWaitingForSnapshot = false;
    mSnapshotCount = 0;

    // OMX shutter callback events are only available in hq mode
    if ((HIGH_QUALITY == mCapMode) || (HIGH_QUALITY_ZSL== mCapMode)) {
        //Disable the callback first
        ret = setShutterCallback(false);

        // if anybody is waiting on the shutter callback
        // signal them and then recreate the semaphore
        if ( 0 != mStartCaptureSem.Count() ) {
            for (int i = mStopCaptureSem.Count(); i > 0; i--) {
                ret |= SignalEvent(mCameraAdapterParameters.mHandleComp,
                                   (OMX_EVENTTYPE) OMX_EventIndexSettingChanged,
                                   OMX_ALL,
                                   OMX_TI_IndexConfigShutterCallback,
                                   NULL );
            }
            mStartCaptureSem.Create(0);
        }
    }

    // After capture, face detection should be disabled
    // and application needs to restart face detection
    stopFaceDetection();

    //Wait here for the capture to be done, in worst case timeout and proceed with cleanup
    ret = mCaptureSem.WaitTimeout(OMX_CAPTURE_TIMEOUT);

    //If somethiing bad happened while we wait
    if (mComponentState == OMX_StateInvalid)
      {
        LOGE("Invalid State Image Capture Stop Exitting!!!");
        goto EXIT;
      }

    if ( NO_ERROR != ret ) {
        ret |= RemoveEvent(mCameraAdapterParameters.mHandleComp,
                           (OMX_EVENTTYPE) OMX_EventIndexSettingChanged,
                           OMX_ALL,
                           OMX_TI_IndexConfigShutterCallback,
                           NULL);
        LOGE("Timeout expired on capture sem");
        goto EXIT;
    }

    // Disable image capture
    // Capturing command is not needed when capturing in video mode
    if (mCapMode != VIDEO_MODE) {
        OMX_INIT_STRUCT_PTR (&bOMX, OMX_CONFIG_BOOLEANTYPE);
        bOMX.bEnabled = OMX_FALSE;
        imgCaptureData = &mCameraAdapterParameters.mCameraPortParams[mCameraAdapterParameters.mImagePortIndex];
        eError = OMX_SetConfig(mCameraAdapterParameters.mHandleComp,
                               OMX_IndexConfigCapturing,
                               &bOMX);
        if ( OMX_ErrorNone != eError ) {
            LOGE("Error during SetConfig- 0x%x", eError);
            ret = -1;
            goto EXIT;
        }
    }
    LOGE("Capture set - 0x%x", eError);

    mCaptureSignalled = true; //set this to true if we exited because of timeout

    {
        Mutex::Autolock lock(mFrameCountMutex);
        mFrameCount = 0;
        mFirstFrameCondition.broadcast();
    }

    return (ret | ErrorUtils::omxToAndroidError(eError));

EXIT:
    LOGE("Exiting function %s because of ret %d eError=%x", __FUNCTION__, ret, eError);
    //Release image buffers
    if ( NULL != mReleaseImageBuffersCallback ) {
        mReleaseImageBuffersCallback(mReleaseData);
    }

    {
        Mutex::Autolock lock(mFrameCountMutex);
        mFrameCount = 0;
        mFirstFrameCondition.broadcast();
    }

    performCleanupAfterError();
    LOG_FUNCTION_NAME_EXIT;
    return (ret | ErrorUtils::omxToAndroidError(eError));
}

status_t OMXCameraAdapter::disableImagePort(){
    status_t ret = NO_ERROR;
    OMX_ERRORTYPE eError = OMX_ErrorNone;
    OMXCameraPortParameters *imgCaptureData = NULL;

    if (!mCaptureConfigured) {
        return NO_ERROR;
    }

    mCaptureConfigured = false;
    imgCaptureData = &mCameraAdapterParameters.mCameraPortParams[mCameraAdapterParameters.mImagePortIndex];

    ///Register for Image port Disable event
    ret = RegisterForEvent(mCameraAdapterParameters.mHandleComp,
                                OMX_EventCmdComplete,
                                OMX_CommandPortDisable,
                                mCameraAdapterParameters.mImagePortIndex,
                                mStopCaptureSem);
    ///Disable Capture Port
    eError = OMX_SendCommand(mCameraAdapterParameters.mHandleComp,
                                OMX_CommandPortDisable,
                                mCameraAdapterParameters.mImagePortIndex,
                                NULL);

    ///Free all the buffers on capture port
    if (imgCaptureData) {
        LOGE("Freeing buffer on Capture port - %d", imgCaptureData->mNumBufs);
        for ( int index = 0 ; index < imgCaptureData->mNumBufs ; index++) {
            LOGE("Freeing buffer on Capture port - 0x%x",
                         ( unsigned int ) imgCaptureData->mBufferHeader[index]->pBuffer);
            eError = OMX_FreeBuffer(mCameraAdapterParameters.mHandleComp,
                                    mCameraAdapterParameters.mImagePortIndex,
                                    (OMX_BUFFERHEADERTYPE*)imgCaptureData->mBufferHeader[index]);

            GOTO_EXIT_IF((eError!=OMX_ErrorNone), eError);
        }
    }
    LOGE("Waiting for port disable");
    //Wait for the image port enable event
    ret = mStopCaptureSem.WaitTimeout(OMX_CMD_TIMEOUT);

    //If somethiing bad happened while we wait
    if (mComponentState == OMX_StateInvalid)
      {
        LOGE("Invalid State after Disable Image Port Exitting!!!");
        goto EXIT;
      }

    if ( NO_ERROR == ret ) {
        LOGE("Port disabled");
    } else {
        ret |= RemoveEvent(mCameraAdapterParameters.mHandleComp,
                           OMX_EventCmdComplete,
                           OMX_CommandPortDisable,
                           mCameraAdapterParameters.mImagePortIndex,
                           NULL);
        LOGE("Timeout expired on port disable");
        goto EXIT;
    }

 EXIT:
    return (ret | ErrorUtils::omxToAndroidError(eError));
}


status_t OMXCameraAdapter::UseBuffersCapture(void* bufArr, int num)
{
    LOG_FUNCTION_NAME;

    status_t ret = NO_ERROR;
    OMX_ERRORTYPE eError = OMX_ErrorNone;
    OMXCameraPortParameters * imgCaptureData = NULL;
    uint32_t *buffers = (uint32_t*)bufArr;
    OMXCameraPortParameters cap;

    imgCaptureData = &mCameraAdapterParameters.mCameraPortParams[mCameraAdapterParameters.mImagePortIndex];

    if ( 0 != mUseCaptureSem.Count() )
        {
        LOGE("Error mUseCaptureSem semaphore count %d", mUseCaptureSem.Count());
        return BAD_VALUE;
        }

    // capture is already configured...we can skip this step
    if (mCaptureConfigured) {

        if ( NO_ERROR == ret )
            {
            ret = setupEXIF();
            if ( NO_ERROR != ret )
                {
                LOGE("Error configuring EXIF Buffer %x", ret);
                }
            }

        mCapturedFrames = mBurstFrames;
        return NO_ERROR;
    }

    imgCaptureData->mNumBufs = num;

    //TODO: Support more pixelformats

    LOGE("Params Width = %d", (int)imgCaptureData->mWidth);
    LOGE("Params Height = %d", (int)imgCaptureData->mWidth);

    if (mPendingCaptureSettings & SetFormat) {
        mPendingCaptureSettings &= ~SetFormat;
        ret = setFormat(OMX_CAMERA_PORT_IMAGE_OUT_IMAGE, *imgCaptureData);
        if ( ret != NO_ERROR ) {
            LOGE("setFormat() failed %d", ret);
            LOG_FUNCTION_NAME_EXIT;
            return ret;
        }
    }

    if (mPendingCaptureSettings & SetThumb) {
        mPendingCaptureSettings &= ~SetThumb;
        ret = setThumbnailParams(mThumbWidth, mThumbHeight, mThumbQuality);
        if ( NO_ERROR != ret) {
            LOGE("Error configuring thumbnail size %x", ret);
            return ret;
        }
    }

    if (mPendingCaptureSettings & SetExpBracket) {
        mPendingCaptureSettings &= ~SetExpBracket;
        ret = setExposureBracketing( mExposureBracketingValues,
                                     mExposureBracketingValidEntries, mBurstFrames);
        if ( ret != NO_ERROR ) {
            LOGE("setExposureBracketing() failed %d", ret);
            goto EXIT;
        }
    }

    if (mPendingCaptureSettings & SetQuality) {
        mPendingCaptureSettings &= ~SetQuality;
        ret = setImageQuality(mPictureQuality);
        if ( NO_ERROR != ret) {
            LOGE("Error configuring image quality %x", ret);
            goto EXIT;
        }
    }

    ///Register for Image port ENABLE event
    ret = RegisterForEvent(mCameraAdapterParameters.mHandleComp,
                           OMX_EventCmdComplete,
                           OMX_CommandPortEnable,
                           mCameraAdapterParameters.mImagePortIndex,
                           mUseCaptureSem);

    ///Enable Capture Port
    eError = OMX_SendCommand(mCameraAdapterParameters.mHandleComp,
                             OMX_CommandPortEnable,
                             mCameraAdapterParameters.mImagePortIndex,
                             NULL);

    LOGE("OMX_UseBuffer = 0x%x", eError);
    GOTO_EXIT_IF(( eError != OMX_ErrorNone ), eError);

    for ( int index = 0 ; index < imgCaptureData->mNumBufs ; index++ )
    {
        OMX_BUFFERHEADERTYPE *pBufferHdr;
        LOGE("OMX_UseBuffer Capture address: 0x%x, size = %d",
                     (unsigned int)buffers[index],
                     (int)imgCaptureData->mBufSize);

        eError = OMX_UseBuffer(mCameraAdapterParameters.mHandleComp,
                               &pBufferHdr,
                               mCameraAdapterParameters.mImagePortIndex,
                               0,
                               mCaptureBuffersLength,
                               (OMX_U8*)buffers[index]);

        LOGE("OMX_UseBuffer = 0x%x", eError);
        GOTO_EXIT_IF(( eError != OMX_ErrorNone ), eError);

        pBufferHdr->pAppPrivate = (OMX_PTR) index;
        pBufferHdr->nSize = sizeof(OMX_BUFFERHEADERTYPE);
        pBufferHdr->nVersion.s.nVersionMajor = 1 ;
        pBufferHdr->nVersion.s.nVersionMinor = 1 ;
        pBufferHdr->nVersion.s.nRevision = 0;
        pBufferHdr->nVersion.s.nStep =  0;
        imgCaptureData->mBufferHeader[index] = pBufferHdr;
    }

    //Wait for the image port enable event
    LOGE("Waiting for port enable");
    ret = mUseCaptureSem.WaitTimeout(OMX_CMD_TIMEOUT);

    //If somethiing bad happened while we wait
    if (mComponentState == OMX_StateInvalid)
      {
        LOGE("Invalid State after Enable Image Port Exitting!!!");
        goto EXIT;
      }

    if ( ret == NO_ERROR )
        {
        LOGE("Port enabled");
        }
    else
        {
        ret |= RemoveEvent(mCameraAdapterParameters.mHandleComp,
                           OMX_EventCmdComplete,
                           OMX_CommandPortEnable,
                           mCameraAdapterParameters.mImagePortIndex,
                           NULL);
        LOGE("Timeout expired on port enable");
        goto EXIT;
        }

    if ( NO_ERROR == ret )
        {
        ret = setupEXIF();
        if ( NO_ERROR != ret )
            {
            LOGE("Error configuring EXIF Buffer %x", ret);
            }
        }

    mCapturedFrames = mBurstFrames;
    mCaptureConfigured = true;

    return (ret | ErrorUtils::omxToAndroidError(eError));

EXIT:
    LOGE("Exiting function %s because of ret %d eError=%x", __FUNCTION__, ret, eError);
    //Release image buffers
    if ( NULL != mReleaseImageBuffersCallback ) {
        mReleaseImageBuffersCallback(mReleaseData);
    }
    performCleanupAfterError();
    LOG_FUNCTION_NAME_EXIT;
    return (ret | ErrorUtils::omxToAndroidError(eError));

}

};
