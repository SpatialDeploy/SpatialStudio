# SPLV Encoder
This project is capable of encoding a stream of NanoVDB (`nvdb`) files into a voxel-based spatial (4D video, `.splv`). Currently, every frame is simply an uncompressed I-frame generated from an `nvdb`, but proper compression will be added in the future.

The project contains both a command line interface and python bindings.

Note that the spatial frames are stored as brickmaps, and so the dimensions must always be a multiple of the brick size, which is 8.

## Usage (Python)
An example python usage may look like:
```python
import py_splv_encoder as splv

encoder = splv.SPLVEncoder(
	xSize=64,
	ySize=64,
	zSize=64,
	lrAxis="x",
	udAxis="y",
	fbAxis="z",
	framerate=60.0,
	outputPath="my_spatial.splv"
)

for i in range(0, 60):
	encoder.add_frame(
		path="my_frame_{0}.nvdb".format(i),
		minX=0,
		minY=0,
		minZ=0,
		maxX=63,
		maxY=63,
		maxZ=63,
		scaleToFit=False
	)

encoder.finish()
```

An encoder is first created with `splv.SPLVEncoder(xSize, ySize, zSize, lrAxis, udAxis, fbAxis, framerate, outputPath)`. 
- `xSize`, `ySize`, and `zSize` define the dimensions of the spatial. 
- `lrAxis`, `udAxis`, and `fbAxis` define which axes correspond to the left/right, up/down, and front/back directions respectively. They must be distinct and one of `"x"`, `"y"`, or `"z"`.
- `framerate` defines the frames per second. 
- `outputPath` defines the path to the output spatial file.

A frame is added using the `splv.Encoder.add_frame(path, minX, minY, minZ, maxX, maxY, maxZ, scaleToFit)` function. 
- `path` defines the path to the `nvdb` file to add. 
- `min*` and `max*` define the bounding box of the frame within the `nvdb`
- `scaleToFit` defines whether to linearly scale the content of the `nvdb` if it does not match the dimensions of the spatial.

Once all frames have been added, you must call `splv.Encoder.finish()` to complete encoding. After `finish()` has been called, the encoder is invalid and no more frames can be added.

## Usage (CLI)
The CLI must be called with `./splv_encoder -d [xSize] [ySize] [zSize] -a [lrAxis] [udAxis] [fbAxis] -f [framerate] -o [outputPath]`. 
- `xSize`, `ySize`, and `zSize` define the dimensions of the spatial.
- `lrAxis`, `udAxis`, and `fbAxis` define which axes correspond to the left/right, up/down, and front/back directions respectively. They must be distinct and one of `"x"`, `"y"`, or `"z"`.
- `framerate` defines the frames per second. 
- `outputPath` defines the path to the output spatial file.

Once in the CLI, a frame can be added be entering `a [pathToNVDB]`, where `pathToNVDB` is the path to the `nvdb` you wish to add. The bounding box within the `nvdb` to add can be set with the command `b [minX] [minY] [minZ] [maxX] [maxY] [maxZ]`, which sets the bounding box for all subsequent frames. The default is `b 0 0 0 width-1 height-1 depth-1`. If you wish to add a frame of different dimensions than was specified when calling into the CLI, you must use `s [on/off]` which turns linear scaling on or off.

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

Once the project has build successfully, the CLI executable as well as the python library (`.pyd` file) can be found under the `bin` directory. There isn't yet support for installing the python bindings system-wide, so you will have to do this manually.