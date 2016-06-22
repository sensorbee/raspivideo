#include <pthread.h>
#include <memory.h>
#include "bcm_host.h"
#include "interface/mmal/mmal.h"
#include "interface/mmal/util/mmal_default_components.h"
#include "interface/mmal/util/mmal_util.h"
#include "interface/mmal/util/mmal_util_params.h"
#include "interface/vcos/vcos.h"

// TODO: implement zero copy version

struct ImageBuffer {
    char* buffer;
    size_t capacity;
    size_t size;
};

struct RaspivideoCamera {
    int widht, height;
    RaspivideoFormat format;
    MMAL_COMPONENT_T* camera;
    MMAL_POOL_T* pool;

    pthread_mutex_t mutex;
    pthread_condition_t cond;

    // current is the buffer that is being written by the callback.
    ImageBuffer current;

    // capturedBuffer contains the complete frame data.
    ImageBuffer captured;

    // ready is non-zero if captured has a valid contents. If it's zero, the
    // caller needs to wait until this value turns non-zero.
    int ready;

    int finishing;
    int waiting;
};

static int createCameraComponent(RaspivideoCamera* camera);

void RaspivideoInitialize() {
    // Go code ensures that this is only called once.
    bcm_host_init();
}

int RaspivideoCreateCamera(RaspivideoCamera** camera, int width, int height, Format format) {
    RaspivideoErrorCode ec;
    int mutexInit = 0, condInit = 0;
    RaspivideoCamera* c = (RaspivideoCamera*) malloc(sizeof(RaspivideoCamera));
    if (c == NULL) {
        return RaspivideoNoMemory;
    }
    memset(c, 0, sizeof(RaspivideoCamera));

    if (pthread_mutex_init(&c->mutex, NULL) != 0) {
        ec = RaspivideoCannotInitMutex;
        goto cleanup;
    }
    mutexInit = 1;

    if (pthread_cond_init(&c->cond, NULL) != 0) {
        ec = RaspivideoCannotInitCond;
        goto cleanup;
    }
    condInit = 1;

    c->width = width;
    c->height = height;
    c->format = format;
    ec = createCameraComponent(c);
    if (ec) goto cleanup;
    *camera = c;
    return RaspivideoSuccess;

cleanup:
    if (condInit && mutexInit) {
        RaspivideoDestroyCamera(c);
    }
    if (condInit) {
        pthread_cond_destroy(&c->cond);
    }
    if (mutexInit) {
        pthread_mutex_destroy(&c->mutex);
    }
    free(c);
    return ec;
}

void RaspivideoLockFrame(RaspivideoCamera* c) {
    pthread_mutex_lock(&c->mutex);
    c->waiting++;
}

void RaspivideoUnlockFrame(RaspivideoCamera* c) {
    c->waiting--;
    pthread_cond_broadcast(&c->cond);
    pthread_mutex_unlock(&c->mutex);
}

int RaspivideoRetrieveFrame(RaspivideoCamera* c, char* buffer) {
    while (!c->ready && !c->finishing) {
        ptherad_cond_wait(&c->cond, &c->mutex);
    }
    if (c->ready && !c->finishing) {
        memcpy(buffer, c->captured.buffer, c->captured.size);
        c->ready = 0;
        // pthread_cond_broadcast will be called in RaspivideoUnlockFrame.
    }
    return c->finishing ? RaspivideoCameraDestroyed : RaspivideoSuccess;
}

size_t RaspivideoFrameSize(RaspivideoCamera* c) {
    while (!c->ready && !c->finishing) {
        ptherad_cond_wait(&c->cond, &c->mutex);
    }
    return c->finishing ? 0 : c->captured.size;
}

void RaspivideoDestroyCamera(RaspivideoCamera* c) {
    pthread_mutex_lock(&c->mutex);
    c->finishing = 1;
    pthread_cond_broadcast(&c->cond);
    while (c->waiting > 0) {
        ptherad_cond_wait(&c->cond, &c->mutex);
    }

    // TODO: destroy pool if it isn't actually destroyed by mmal_component_destroy(c->camera)
    if (c->camera) mmal_component_destroy(c->camera);
    free(c);
    pthread_mutex_unlock(&c->mutex);
}

static void cameraOutputCallback(MMAL_PORT_T* port, MMAL_BUFFER_HEADER_T* buffer) {
    RaspivideoCamera* c = (RaspivideoCamera*) port->userdata;
    mmal_buffer_header_mem_lock(buffer);

    // Expand buffer. c->current is only accessed from this callback and doesn't
    // have to be locked.
    if (c->current.capacity < c->current.size + buffer->length) {
        // TODO: make this more efficient when supporting JPEG.
        char* newBuffer = (char*) malloc(c->current.size + buffer->length);
        if (newBuffer == NULL) {
            // TODO: need some way to notify this error through RaspivideoCamera*
            mmal_buffer_header_mem_unlock(buffer);
            mmal_buffer_header_release(buffer);
            return;
        }
        memcpy(newBuffer, c->current.buffer, c->current.size);
        free(c->current.buffer);
        c->current.buffer = newBuffer;
        c->current.capacity = c->current.size + buffer->length;
    }
    memcpy(c->current.buffer + c->current.size, buffer->data + buffer->offset, buffer->length);
    c->current.size += buffer->length;

    // Check the end of the frame
    if (buffer->flag & (MMAL_BUFFER_HEADER_FLAG_FRAME_END | MMAL_BUFFER_HEADER_FLAG_TRANSMISSION_FAILED)) {
        // TODO: notify transmission failure
        pthread_mutex_lock(&c->mutex);
        ImageBuffer tmp = c->current;
        c->current = c->captured;
        c->captured = tmp;
        c->current->size = 0;
        c->ready = 1;
        pthread_cond_broadcast(&c->cond);
        pthread_mutex_unlock(&c->mutex);
    }
    mmal_buffer_header_mem_unlock(buffer);
    mmal_buffer_header_release(buffer);

    // Re-send buffer
    if (port->is_enabled) {
        MMAL_BUFFER_HEADER_T* newBuffer = mmal_queue_get(c->pool);
        if (newBuffer) {
            if (mmal_port_send_buffer(port, newBuffer) != MMAL_SUCCESS) {
                // TODO: notify this error
            }
        }
    }
}

static int createCameraComponent(RaspivideoCamera* c) {
    // TODO: add error handlings like RaspiVid or RaspiStill
    MMAL_COMPONENT_T* camera;
    MMAL_STATUS_T status;
    MMAL_PORT_T* video;
    status = mmal_component_create(MMAL_COMPONENT_DEFAULT_CAMERA, &c->camera);
    if (status != MMAL_SUCCESS) {
        return RaspivideoCannotCreateCamera;
    }
    camera = c->camera;
    // camera will be cleaned up by CreateCamera on failure.

    {
        MMAL_PARAMETER_INT32_T camera_num = {{MMAL_PARAMETER_CAMERA_NUM, sizeof(camera_num)}, 0}; // TODO: num should be configurable
        status = mmal_port_parameter_set(camera->control, &camera_num.hdr);
        if (status != MMAL_SUCCESS) {
            return RaspivideoCannotSetCamera;
        }
    }

    {
        MMAL_PARAMETER_CAMERA_CONFIG_T cam_config;
        cam_config.hdr.id = MMAL_PARAMETER_CAMERA_CONFIG;
        cam_config.hdr.size = sizeof(cam_config);
        cam_config.max_stills_w = c->width;
        cam_config.max_stills_h = c->height;
        cam_config.stills_yuv422 = 0;
        cam_config.one_shot_stills = 0;
        cam_config.max_preview_video_w = c->width;
        cam_config.max_preview_video_h = c->height;
        cam_config.num_preview_video_frames = 3; // TODO: configurable
        cam_config.stills_capture_circular_buffer_height = 0;
        cam_config.fast_preview_resume = 0;
        cam_config.use_stc_timestamp = MMAL_PARAM_TIMESTAMP_MODE_RAW_STC;
        status = mmal_port_parameter_set(camera->control, &cam_config.hdr);
        if (status != MMAL_SUCCESS) {
            return RaspivideoCannotSetCameraConfig;
        }
    }

    video = camera->output[1];
    video->userdata = (MMAL_PORT_USERDATA_T*) c;
    video->buffer_num = video->buffer_num_recommended;
    if (video->buffer_num < video->buffer_num_min) {
        video->buffer_num = video->buffer_num_min;
    }
    video->buffer_size = video->buffer_size_recommended;
    if (video->buffer_size < video->buffer_size_min) {
        video->buffer_size = video->buffer_size_min;
    }

    {
        MMAL_ES_FORMAT_T* f = video->format;
        switch (format) {
        case RaspivideoFormatRGB:
            f->encoding = MMAL_ENCODING_RGB24;
            f->encoding_variant = MMAL_ENCODING_RGB24;
            break;
        case RaspivideoFormatBGR:
            f->encoding = MMAL_ENCODING_BGR24:
            f->encoding_variant = MMAL_ENCODING_BGR24;

            // Assuming validation is done in Go
        }
        f->es->video.width = VCOS_ALIGN_UP(c->width, 32);
        f->es->video.height = VCOS_ALIGN_UP(c->height, 16);
        f->es->video.crop.x = 0;
        f->es->video.crop.y = 0;
        f->es->video.crop.width = f->es->video.width;
        f->es->video.crop.height = f->es->video.height;

        status = mmal_port_format_commit(video);
        if (status != MMAL_SUCCESS) {
            return RaspivideoCannotCommitFormat;
        }
    }

    status = mmal_component_enable(camera);
    if (status != MMAL_SUCCESS) {
        return RaspivideoCannotEnableCamera;
    }

    // TODO: when supporting JPEG, an encoder needs to be created for hardware encoding.

    // TODO: This pool should indirectly be destroyed by mmal_destroy_component but need to be checked.
    c->pool = mmal_port_pool_create(video, video->buffer_num, video->buffer_size);
    if (c->pool == NULL) {
        return RaspivideoCannotCreatePool;
    }
    status = mmal_port_enable(video, cameraOutputCallback);
    if (status != MMAL_SUCCESS) {
        return RaspivideoCannotEnableVideoPort;
    }

    {
        MMAL_BUFFER_HEADER_T* buffer;
        while ((buffer = mmal_queue_get(c->pool->queue)) != NULL) {
            status = mmal_port_send_buffer(video, buffer);
            if (status != MMAL_SUCCESS) {
                return RaspivideoCannotSendBuffer;
            }
        }
    }

    status = mmal_port_parameter_set_boolean(video, MMAL_PARAMETER_CAPTURE, MMAL_TRUE);
    if (status != MMAL_SUCCESS) {
        return RaspivideoCannotStartCapture;
    }
    return 0;
}
