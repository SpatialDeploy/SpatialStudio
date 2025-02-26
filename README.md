# SPLV Encoder
This project is capable of encoding voxel-based spatials (4D video, `.splv`). It supports encoding frames from NanoVDB files (`.nvdb`), MagicaVoxel project files (`.vox`), and `numpy` arrays.

The project contains both a command line interface and python bindings.

Note that the spatial frames are stored as brickmaps, and so the target dimensions must always be a multiple of the brick size, which is 8.

## Usage (Python)
An example python usage may look like:
```python
import spatialstudio as splv

# initialize encoder
encoder = splv.SPLVencoder(
	width=64,
	height=64,
	depth=64,
	framerate=60.0,
	gopSize=10,
	maxBrickGroupSize=512,
	outputPath="my_spatial.splv"
)

# add some frames from NanoVDBs
for i in range(0, 60):
	encoder.encode_nvdb_frame(
		path="my_frame_{0}.nvdb".format(i),
		minX=0,
		minY=0,
		minZ=0,
		maxX=63,
		maxY=63,
		maxZ=63,
		lrAxis="x",
		udAxis="z",
		fbAxis="y",
		removeNonvisible=False
	)

# finish encoding, flush everything to disk
encoder.finish()
```

An encoder is first created with `splv.SPLVencoder(width, height, depth, framerate, outputPath, gopSize, maxBrickGroupSize)`. 
- `xSize`, `ySize`, and `zSize` define the dimensions of the spatial. 
- `framerate` defines the frames per second. 
- `gopSize` defines the group-of-pictures size for this encoder. If this is set to some number `n`, then every `n`th frame is an I-frame, and the rest are P-frames.
- `maxBrickGroupSize` defines the maximum number of bricks that get encoded independently in each frame. Essentially, this controls the parallelizeabliltiy of encoding/decoding. A good default is 512. A value of 0 means that all bricks will be encoded in a single group.
- `outputPath` defines the path to the output spatial file.

A frame from an `nvdb` is encoded using the `splv.SPLVencoder.encode_nvdb_frame(path, minX, minY, minZ, maxX, maxY, maxZ, lrAxis, udAxis, fbAxis removeNonvisible=False)` function. 
- `path` defines the path to the `nvdb` file to add. 
- `min*` and `max*` define the bounding box of the frame within the `nvdb`. Note that these must correspond to a box with dimensions that are multiples of the brick size (8).
- `lrAxis`, `udAxis`, and `fbAxis` define which axes correspond to the left/right, up/down, and front/back directions respectively. They must be distinct and one of `"x"`, `"y"`, or `"z"`.
- `removeNonvisible` controls whether the encoder automatically detects and removes non-visible voxels before encoding. This can increase encoding time, so only enable it if your frames have many non-visible voxels (i.e. a solid volume).

A set of frames from a `vox` file animation are encoded using the `splv.SPLVencoder.encode_vox_frame(path, removeNonvisible=False)` function.
- `path` defines the path to the `vox` file to add.
- `min*` and `max*` define the bounding box of the frame within the `vox`. Note that `vox` files are z-up.
- `removeNonvisible` controls whether the encoder automatically detects and removes non-visible voxels before encoding. This can increase encoding time, so only enable it if your frames have many non-visible voxels (i.e. a solid volume).

A frame from a `numpy` array is encoded using the `splv.SPLVencoder.encode_numpy_array_float(arr, lrAxis, udAxis, fbAxis, removeNonvisible=False)`, or the analogous `encode_numpy_array_byte()`.
- `arr` is the numpy array to encode. It must be a 4-dimensional array, with the last dimension having size 4. It represents an 3D array of RGBA colors, where an alpha of `0` represents an empty voxel (the voxel is fully opaque otherwise). For `encode_numpy_array_float`, these are floating point values in the range `[0.0, 1.0]`. For `encode_numpy_array_byte`, these are `uint8`s in the range `[0, 255]`.
- `lrAxis`, `udAxis`, and `fbAxis` define which axes correspond to the left/right, up/down, and front/back directions respectively. They must be distinct and one of `"x"`, `"y"`, or `"z"`.
- `removeNonvisible` controls whether the encoder automatically detects and removes non-visible voxels before encoding. This can increase encoding time, so only enable it if your frames have many non-visible voxels (i.e. a solid volume).

Once all frames have been added, you must call `splv.SPLVencoder.finish()` to complete encoding. After `finish()` has been called, the encoder is invalid and no more frames can be added.

Also included in the python bindings are some utility functions:
- `splv.concat(paths, outPath)` concatenates multiple spatials together into one. The dimensions and framerate must all match.
- `splv.split(path, splitLength, outDir)` splits a given spatial into multiple separate spatials, each with the specified duration.
- `splv.upgrade(path, outPath)` upgrades a spatial from the previous version to the current version.
- `splv.get_vox_max_dimensions(path)` returns the maximum dimensions of the frames in a given `vox` file.

## Usage (CLI)
The CLI must be called with `./splv_encoder -d [xSize] [ySize] [zSize] -f [framerate] -g [gopSize] -b [maxBrickGroupSize] -o [outputPath]`. 
- `xSize`, `ySize`, and `zSize` define the dimensions of the spatial.
- `framerate` defines the frames per second. 
- `gopSize` defines the group-of-pictures size for this encoder. If this is set to some number `n`, then every `n`th frame is an I-frame, and the rest are P-frames.
- `maxBrickGroupSize` defines the maximum number of bricks that get encoded independently in each frame. Essentially, this controls the parallelizeabliltiy of encoding/decoding. A good default is 512. A value of 0 means that all bricks will be encoded in a single group.
- `outputPath` defines the path to the output spatial file.

Once in the CLI, an nvdb frame can be encoded be entering `e_nvdb [pathToNVDB]`, where `pathToNVDB` is the path to the `nvdb` you wish to add. Similarly, `e_vox [pathToVox]` adds frames from a `vox` file animation. The bounding box within the source file to encode can be set with the command `b [minX] [minY] [minZ] [maxX] [maxY] [maxZ]`, which sets the bounding box for all subsequent frames. The default is `b 0 0 0 width-1 height-1 depth-1`. For `nvdb`, the axes corresponding to left/right, up/down, and front/back can be set using the `a [lrAxis] [udAxis] [fbAxis]` command, where the axes are distinct and one of `"x"`, `"y"`, or `"z"` (this doesn't affect `vox` files, since they always use the same axes). To enable/disable the automatic removal of non-visible voxels for all subsequent frames, use the command `r [on/off]`. This can increase encoding time, so only use it if your frames have many non-visible voxels.

Once all the frames have been added, you must enter `f` to finish encoding, at which point no more frames can be added. Alternatively, if you wish to exit the CLI without finishing the encoding, you can enter `q`.

## Installing
To install the python bindings, simply run
```bash
pip install git+https://github.com/SpatialDeploy/SpatialStudio
```
This will install the `spatialstudio` package globally.

## Building
You may wish to build the project from source, either to run the CLI executable or to build the python library manually. This project uses `cmake` as its build system. Dependencies are installed automatically. You first generate platform-specific build files from by calling
```bash
mkdir build
cd build
cmake ..
```
This will generate a `Makefile` on Unix systems, or a Visual Studio solution on Windows systems. On Unix, you can simply call `make` to build the project. On Windows you must open the VS solution and build from there.

Simply running `cmake ..` will only build the `splv_encoder` static library. In order to additionally build the CLI tool, add the flag `	-DSPLV_BUILD_CLI=ON` when configuring cmake. Similarly add the flag `-DSPLV_BUILD_PYTHON_BINDINGS` to build the python bindings.

Once the project has built successfully, the `splv_encoder` library, as well as (optionally) the CLI executable and python library, will be nested somewhere in the `build` directory (depending on yuor platform).