# SPLV Encoder
This project is capable of encoding voxel-based spatials (4D video, `.splv`). It supports encoding frames from NanoVDB files (`.nvdb`), and MagicaVoxel project files (`.vox`).

The project contains both a command line interface and python bindings.

Note that the spatial frames are stored as brickmaps, and so the target dimensions must always be a multiple of the brick size, which is 8.

## Usage (Python)
An example python usage may look like:
```python
import py_splv_encoder as splv

# initialize encoder
encoder = splv.SPLVencoder(
	width=64,
	height=64,
	depth=64,
	framerate=60.0,
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

# add frames from a MagicaVoxel animation
encoder.encode_vox_frame(
	path="my_frame.vox",
	minX=-32,
	minY=-32,
	minZ=-32,
	maxX=31,
	maxY=31,
	maxZ=31,
	removeNonvisible=True
)

# finish encoding, flush everything to disk
encoder.finish()
```

An encoder is first created with `splv.SPLVencoder(width, height, depth, framerate, outputPath)`. 
- `xSize`, `ySize`, and `zSize` define the dimensions of the spatial. 
- `framerate` defines the frames per second. 
- `outputPath` defines the path to the output spatial file.

A frame from an `nvdb` is encoded using the `splv.SPLVencoder.encode_nvdb_frame(path, minX, minY, minZ, maxX, maxY, maxZ, removeNonvisible=False)` function. 
- `path` defines the path to the `nvdb` file to add. 
- `min*` and `max*` define the bounding box of the frame within the `nvdb`
- `lrAxis`, `udAxis`, and `fbAxis` define which axes correspond to the left/right, up/down, and front/back directions respectively. They must be distinct and one of `"x"`, `"y"`, or `"z"`.
- `removeNonvisible` controls whether the encoder automatically detects and removes non-visible voxels before encoding. This can increase encoding time, so only enable it if your frames have many non-visible voxels (i.e. a solid volume).

A set frames from a `vox` file animation are encoded using the `splv.SPLVencoder.encode_vox_frame(path, removeNonvisible=False)` function.
- `path` defines the path to the `vox` file to add.
- `min*` and `max*` define the bounding box of the frame within the `vox`. Note that `vox` files are z-up.
- `removeNonvisible` controls whether the encoder automatically detects and removes non-visible voxels before encoding. This can increase encoding time, so only enable it if your frames have many non-visible voxels (i.e. a solid volume).

Once all frames have been added, you must call `splv.SPLVencoder.finish()` to complete encoding. After `finish()` has been called, the encoder is invalid and no more frames can be added.

## Usage (CLI)
The CLI must be called with `./splv_encoder -d [xSize] [ySize] [zSize] -f [framerate] -o [outputPath]`. 
- `xSize`, `ySize`, and `zSize` define the dimensions of the spatial.
- `framerate` defines the frames per second. 
- `outputPath` defines the path to the output spatial file.

Once in the CLI, an nvdb frame can be encoded be entering `e_nvdb [pathToNVDB]`, where `pathToNVDB` is the path to the `nvdb` you wish to add. Similarly, `e_vox [pathToVox]` adds frames from a `vox` file animation. The bounding box within the source file to encode can be set with the command `b [minX] [minY] [minZ] [maxX] [maxY] [maxZ]`, which sets the bounding box for all subsequent frames. The default is `b 0 0 0 width-1 height-1 depth-1`. For `nvdb`, the axes corresponding to left/right, up/down, and front/back can be set using the `a [lrAxis] [udAxis] [fbAxis]` command, where the axes are distinct and one of `"x"`, `"y"`, or `"z"` (this doesn't affect `vox` files, since they always use the same axes). To enable/disable the automatic removal of non-visible voxels for all subsequent frames, use the command `r [on/off]`. This can increase encoding time, so only use it if your frames have many non-visible voxels.

Once all the frames have been added, you must enter `f` to finish encoding, at which point no more frames can be added. Alternatively, if you wish to exit the CLI without finishing the encoding, you can enter `q`.

## Building
This project has the following dependencies which must be installed before building:
- `cmake`
- `pybind11`
- `blosc`

Once the dependencies have been installed, you can first generate build files from by calling
```bash
mkdir build
cd build
cmake ..
```
This will generate a `Makefile` on Unix systems, or a Visual Studio solution on Windows systems. On Unix, you can simply call `make` to build the project. On Windows you must open the VS solution and build from there.

Once the project has build successfully, the CLI executable as well as the python library (`.pyd` file) can be found under the `bin` directory. To install the python library globally, you can call:
```bash
pip install -e .
```
From the root directory.