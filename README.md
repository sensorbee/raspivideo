# Raspberry Pi camera source plugin

raspivide is the SensorBee's source plugin that input video frames from
Raspberry Pi's official camera module. This plugin is only tested with Raspbian
on Raspberry Pi 3.

# Usage

Add `gopkg.in/sensorbee/raspivideo.v0/plugin` to build.yaml and build a
`sensorbee` command. Then, create a source with `raspivideo` source type:

```
> CREATE SOURCE video TYPE raspivideo;
```

It is recommended that streams selecting tuples from the source specify
`[RANGE 1 TUPLES, BUFFER SIZE 1, DROP OLDEST IF FULL]` to avoid out of memory:

```
> SELECT RSTREAM * FROM video [RANGE 1 TUPLES, BUFFER SIZE 1, DROP OLDEST IF FULL]
```

Output from the source contains following fields:

```
{
    "width": 640, // width of the image
    "height": 480, // height of the image
    "format": "raw", // data format
    "color_model": "bgr", // color model (layout) of the image (if the format is raw)
    "image": (blob of the frame data)
}
```

# Parameters

The raspivideo source has three `CREATE SOURCE` parameters: `width`, `height`,
and `format`.

## `width` and `height`

`width` and `height` parameters specify the size of video frames to be retrieved.
Currently, 640x480 and 320x240 are supported.

## `format`

Two formats are supported:

* "rgb"
* "bgr"

With "rgb", frame data will be an array of byte containing RGB value of each
pixel. R, G, and B of the pixel at `x` and `y` are located at
`image[width * y + x + 0]`, `image[width * y + x + 1]`, `image[width * y + x + 2]`,
respectively. Each scan line of the image might not be aligned by 4 when the
width of the image cannot be devided by 4.

"bgr" is same as "rgb" except that the order of R, G, and B is reversed.
