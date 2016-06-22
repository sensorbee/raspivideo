package raspivideo

/*
#cgo CPPFLAGS: -I/opt/vc/include/interface/vcos -I/opt/vc/include/interface/vcos/pthreads -I/opt/vc/include/interface/vmcs_host/linux
#cgo LDFLAGS: -L/opt/vc/lib -lmmal -lmmal_core -lmmal_util -lvcos -lbcm_host
#include "video.h"
*/
import "C"

import (
	"fmt"
	"errors"
	"gopkg.in/sensorbee/sensorbee.v0/bql"
	"gopkg.in/sensorbee/sensorbee.v0/core"
	"gopkg.in/sensorbee/sensorbee.v0/data"
	"unsafe"
)

var (
	supportedFormats = map[string]struct{}{
		"rgb": struct{}{},
		"bgr": struct{}{},
	}
)

type Source struct {
	width, height int
	format        C.RaspivideoFormat
}

func (s *Source) GenerateStream(ctx *core.Context, w core.Writer) error {
	var camera *C.RaspivideoCamera
	res := C.RaspivideoCreateCamera(&camera, C.int(s.width), C.int(s.height), s.format)
	if res != C.RaspivideoSuccess {
		err := toError(res)
		ctx.ErrLog(err).Error("Cannot start streaming video due to camera creation error")
		return err
	}
	defer func() {
		C.RaspivideoDestroyCamera(camera)
	}()

	for {
		frame, ec := func() ([]byte, C.RaspivideoErrorCode) {
			C.RaspivideoLockFrame(camera)
			defer C.RaspivideoUnlockFrame(camera)

			size := C.RaspivideoFrameSize(camera)
			if size == 0 {
				return C.RasspivideoCameraDestroyed
			}
			frame := make([]byte, size)
			res := C.RaspivideoRetrieveFrame(camera, (*C.char)(unsafe.Pointer(&frame[0])))
			return frame, nil
		}()

		if ec != C.RaspivideoSuccess {
			if ec == C.RaspivideoCameraDestroyed {
				return nil
			}
			err := toError(ec)
			ctx.ErrLog(err).Error("Cannot retrieve a frame, stopping the video stream")
			return err
		}

		t := core.NewTuple(data.Map{
			"width":  s.width,
			"height": s.height,
			"format": "cvmat",
			"mode":   s.format,
			"image":  data.Blob(frame),
		})
		if err := w.Write(ctx, t); err != nil {
			return err
		}
	}
	return nil
}

func toError(c C.RaspivideoErrorCode) error {
	return errors.New(func() string {
		switch c {
		case C.RaspivideoSuccess:
			return "no error"
		case C.RaspivideoNoMemory:
			return "cannot allocate memory"
		case C.RaspivideoCannotInitMutex:
			return "cannot init mutex"
		case C.RaspivideoCannotCreateCamera:
			return "cannot create camera object"
		case C.RaspivideoCannotSetCamera:
			return "cannot select a target camera"
		case C.RaspivideoCannotSetCameraConfig:
			return "cannot configure camera"
		case C.RaspivideoCannotCommitFormat:
			return "cannot commit camera format"
		case C.RaspivideoCannotEnableCamera:
			return "cannot enable camera"
		case C.RaspivideoCannotCreatePool:
			return "cannot create a pool of buffers"
		case C.RaspivideoCannotEnableVideoPort:
			return "cannot enable video port of the camera"
		case C.RaspivideoCannotSendBuffer:
			return "cannot send buffer to video port"
		case C.RaspivideoCannotStartCapture:
			return "cannot start capture"
		case C.RaspivideoCameraDestroyed:
			return "camera has already been destroyed"
		default:
			return "unknown error"
		}
	}())
}

func (s *Source) Stop(ctx *core.Context) error {
	// will be implemented by core.ImplementSourceStop
	return nil
}

func CreateSource(ctx *core.Context, ioParams *bql.IOParams, params data.Map) (core.Source, error) {
	s := &Source{
		width: 640,
		height: 480,
		format: "bgr",
	}

	if v, ok := params["width"]; ok {
		w, err := data.AsInt(v)
		if err != nil {
			return nil, fmt.Errorf("width parameter must be an integer: %v", err)
		}
		s.width = w
	}
	if v, ok := params["height"]; ok {
		h, err := data.AsInt(v)
		if err != nil {
			return nil, fmt.Errorf("height parameter must be an integer: %v", err)
		}
		s.height = h
	}
	if v, ok := params["format"]; ok {
		f, err := data.AsString(v)
		if err != nil {
			return nil, fmt.Errorf("format parameter must be a string: %v", err)
		}
		s.format = f
	}

	switch {
	case s.width == 640 && s.height == 480, s.width == 320 && s.height == 240:
	default:
		return nil, fmt.Errorf("unsupported frame size: %vx%v", s.width, s.height)
	}

	switch s.format {
	case "rgb", "bgr":
	default:
		return nil fmt.Errorf("unsupported format: %v", s.format)
	}
	return core.ImplementSourceStop(s), nil
}

func init() {
	C.RaspivideoInitialize()
}
