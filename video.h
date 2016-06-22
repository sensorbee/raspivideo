#ifndef SB_RASPIVIDEO_H_
#define SB_RASPIVIDEO_H_

#ifdef __cplusplus
extern "C" {
#endif

struct RaspivideoCamera;

enum RaspivideoFormat {
    RaspivideoFormatRGB,
    RaspivideoFormatBGR,
    // TODO: support RaspivideoFormatJpeg
};

enum RaspivideoErrorCode {
    RaspivideoSuccess,
    RaspivideoNoMemory,
    RaspivideoCannotInitMutex,
    RaspivideoCannotCreateCamera,
    RaspivideoCannotSetCamera,
    RaspivideoCannotSetCameraConfig,
    RaspivideoCannotCommitFormat,
    RaspivideoCannotEnableCamera,
    RaspivideoCannotCreatePool,
    RaspivideoCannotEnableVideoPort,
    RaspivideoCannotSendBuffer,
    RaspivideoCannotStartCapture,
    RaspivideoCameraDestroyed,
};

/*
    RaspivideoInitialize initializes the multimedia library of Raspberry Pi. It
    must not be called more than once.
*/
void RaspivideoInitialize();

/*
    RaspivideoCreateCamera creates a RaspivideoCamera component and 
    immediately starts capturing frames. This function assumes that width and
    height are validated by the caller.
*/
RaspivideoErrorCode RaspivideoCreateCamera(RaspivideoCamera** c, int width, int height, Format format);

/*
    RaspivideoLockFrame locks the frame information. It has to be called before
    calling some functions.
*/
void RaspivideoLockFrame(RaspivideoCamera* c);

/*
    RaspivideoUnlockFrame unlocks the mutex locked by RaspivideoLockFrame.
*/
void RaspivideoUnlockFrame(RaspivideoCamera* c);

/*
    RaspivideoRetrieveFrame returns the latest frame that has been captured.
    buffer's size needs to be at least the value returned from RaspivideoFrameSize.
    This function clears the ready flag, so the next call will wait until the
    next frame becomes ready.
    RaspivideoLockFrame must be called before calling this function.
*/
RaspivideoErrorCode RaspivideoRetrieveFrame(RaspivideoCamera* c, char* buffer);

/*
    RaspivideoFrameSize returns the size of the buffer required to have the
    captured frame data. RaspivideoLockFrame must be called before calling
    this function. It blocks until the new frame arrives. It returns 0 if the
    RaspivideoCamera is being or already destroyed.
*/
size_t RaspivideoFrameSize(RaspivideoCamera* c);

/*
    RaspivideoDestroyCamera destroys a RaspivideoCamera. The destroyed
    RaspivideoCamera must not be passed to any function.
*/
void RaspivideoDestroyCamera(RaspivideoCamera* c);

const char* ErrorMessage(int errorCode);

#ifdef __cplusplus
}
#endif

#endif
