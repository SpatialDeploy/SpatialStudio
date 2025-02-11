#include "py_splv_encoder.hpp"

#include "spatialstudio/splv_vox_utils.h"
#include "spatialstudio/splv_nvdb_utils.h"
#include "spatialstudio/splv_utils.h"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <iostream>
#include <vector>

//-------------------------------------------//

PySPLVencoder::PySPLVencoder(uint32_t width, uint32_t height, uint32_t depth, float framerate, 
                             uint32_t gopSize, uint32_t maxBrickGroupSize, std::string outPath)
{
	//validate:
	//---------------
	if(width == 0 || height == 0 || depth == 0)
	{
		std::cout << "ERROR: dimensions must be positive\n";
		throw std::runtime_error("");
	}

	if(width % SPLV_BRICK_SIZE != 0 || height % SPLV_BRICK_SIZE != 0 || depth % SPLV_BRICK_SIZE != 0)
	{
		std::cout << "ERROR: dimensions must be multiples of SPLV_BRICK_SIZE (" << SPLV_BRICK_SIZE << ")\n";
		throw std::runtime_error("");
	}

	if(framerate <= 0.0f)
	{
		std::cout << "ERROR: framerate must be positive\n";
		throw std::runtime_error("");
	}

	if(gopSize == 0)
	{
		std::cout << "ERROR: GOP size must be positive\n";
		throw std::runtime_error("");
	}

	//create encoder:
	//---------------
	SPLVencodingParams encodingParams = {0};
	encodingParams.gopSize = gopSize;
	encodingParams.maxBrickGroupSize = maxBrickGroupSize;

	SPLVerror encoderError = splv_encoder_create(&m_encoder, width, height, depth, framerate, encodingParams, outPath.c_str());
	if(encoderError != SPLV_SUCCESS)
	{
		std::cout << "ERROR: failed to create SPLVencoder with code " <<
			encoderError << " (" << splv_get_error_string(encoderError) << ")\n";
		throw std::runtime_error("");
	}
}

void PySPLVencoder::encode_nvdb_frame(std::string path, int32_t minX, int32_t minY, int32_t minZ, 
                                      int32_t maxX, int32_t maxY, int32_t maxZ, std::string lrAxisStr, 
                                      std::string udAxisStr, std::string fbAxisStr, bool removeNonvisible)
{
	//parse + validate:
	//---------------
	SPLVaxis lrAxis = parse_axis(lrAxisStr);
	SPLVaxis udAxis = parse_axis(udAxisStr);
	SPLVaxis fbAxis = parse_axis(fbAxisStr);

	validate_bounding_box(minX, minY, minZ, maxX, maxY, maxZ);
	validate_axes(lrAxis, udAxis, fbAxis);

	//create frame, encode:
	//---------------
	SPLVboundingBox boundingBox = { minX, minY, minZ, maxX, maxY, maxZ };

	SPLVframe frame;
	SPLVerror nvdbError = splv_nvdb_load(((std::string)path).c_str(), &frame, &boundingBox, lrAxis, udAxis, fbAxis);
	if(nvdbError != SPLV_SUCCESS)
	{
		std::cout << "ERROR: failed to create nvdb frame with code " <<
			nvdbError << " (" << splv_get_error_string(nvdbError) << ")\n";
		throw std::runtime_error("");
	}

	m_activeFrames.push_back(frame);
	encode_frame(&frame, removeNonvisible);
}

void PySPLVencoder::encode_vox_frame(std::string path, int32_t minX, int32_t minY, int32_t minZ, 
                                     int32_t maxX, int32_t maxY, int32_t maxZ, bool removeNonvisible)
{
	//validate:
	//--------------
	validate_bounding_box(minX, minY, minZ, maxX, maxY, maxZ);

	//create frame, encode:
	//---------------
	SPLVboundingBox boundingBox = { minX, minY, minZ, maxX, maxY, maxZ };

	uint32_t numFrames;
	SPLVframe** frames;
	SPLVerror voxError = splv_vox_load(((std::string)path).c_str(), &frames, &numFrames, &boundingBox);
	if(voxError != SPLV_SUCCESS)
	{
		std::cout << "ERROR: failed to create vox frames with code " <<
			voxError << " (" << splv_get_error_string(voxError) << ")\n";
		throw std::runtime_error("");
	}

	for(uint32_t i = 0; i < numFrames; i++)
		encode_frame(frames[i], removeNonvisible);

	m_activeVoxFrames.push_back({ frames, numFrames });
}

void PySPLVencoder::encode_numpy_frame_float(py::array_t<float> arr, std::string lrAxis, std::string udAxis, 
                                             std::string fbAxis,  bool removeNonvisible)
{
	encode_numpy_frame(&arr, nullptr, lrAxis, udAxis, fbAxis, removeNonvisible);
}

void PySPLVencoder::encode_numpy_frame_byte(py::array_t<uint8_t> arr, std::string lrAxis, std::string udAxis, 
                                            std::string fbAxis, bool removeNonvisible)
{
	encode_numpy_frame(nullptr, &arr, lrAxis, udAxis, fbAxis, removeNonvisible);
}

void PySPLVencoder::finish()
{
	SPLVerror finishError = splv_encoder_finish(&m_encoder);
	if(finishError != SPLV_SUCCESS)
	{
		free_frames();

		std::cout << "ERROR: failed to finish encoding with code " << 
			finishError << " (" << splv_get_error_string(finishError) << ")\n";
		throw std::runtime_error("");
	}

	free_frames();
}

void PySPLVencoder::abort()
{
	splv_encoder_abort(&m_encoder);

	free_frames();
}

//-------------------------------------------//

void PySPLVencoder::encode_numpy_frame(py::array_t<float>* floatArr, py::array_t<uint8_t>* byteArr, std::string lrAxisStr, 
                                       std::string udAxisStr, std::string fbAxisStr, bool removeNonvisible)
{
	//validate buffer is correct shape:
	//---------------
	bool isFloat;
	py::buffer_info buf;

	if(floatArr)
	{
		buf = floatArr->request();
		isFloat = true;
	}
	else if(byteArr)
	{
		buf = byteArr->request();
		isFloat = false;
	}
	else
	{
		std::cout << "ERROR (internal): both arrays passed to encode_numpy_frame were null!\n";
		throw std::runtime_error("");
	}

	if(buf.ndim != 4)
	{
		std::cout << "ERROR: input must be 4-dimensional (3 dimensional grid of vec4's)\n";
		throw std::runtime_error("");
	}

	if(buf.shape[3] != 4)
	{
		std::cout << "ERROR: last dimension of input must be 4 (for vec4)\n";
		throw std::runtime_error("");
	}

	uint32_t xSize = (uint32_t)buf.shape[0];
	uint32_t ySize = (uint32_t)buf.shape[1];
	uint32_t zSize = (uint32_t)buf.shape[2];

	if(xSize % SPLV_BRICK_SIZE != 0 || ySize % SPLV_BRICK_SIZE != 0 || zSize % SPLV_BRICK_SIZE != 0)
	{
		std::cout << "ERROR: frame dimensions must be multiples of SPLV_BRICK_SIZE (" << SPLV_BRICK_SIZE << ")\n";
		throw std::runtime_error("");
	}

	//parse + validate axes:
	//---------------
	SPLVaxis lrAxis = parse_axis(lrAxisStr);
	SPLVaxis udAxis = parse_axis(udAxisStr);
	SPLVaxis fbAxis = parse_axis(fbAxisStr);

	validate_axes(lrAxis, udAxis, fbAxis);

	//create frame:
	//---------------
	uint32_t sizes[3] = {xSize, ySize, zSize};
	uint32_t widthMap  = sizes[(uint32_t)lrAxis] / SPLV_BRICK_SIZE;
	uint32_t heightMap = sizes[(uint32_t)udAxis] / SPLV_BRICK_SIZE;
	uint32_t depthMap  = sizes[(uint32_t)fbAxis] / SPLV_BRICK_SIZE;

	SPLVframe frame;
	SPLVerror frameError = splv_frame_create(&frame, widthMap, heightMap, depthMap, 0);
	if(frameError != SPLV_SUCCESS)
	{
		std::cout << "ERROR: failed to create frame with code " << 
			frameError << " (" << splv_get_error_string(frameError) << ")\n";
		throw std::runtime_error("");
	}

	for(uint32_t i = 0; i < widthMap * heightMap * depthMap; i++)
		frame.map[i] = SPLV_BRICK_IDX_EMPTY;

	uint8_t* arr = (uint8_t*)buf.ptr;

	for(uint32_t x = 0; x < xSize; x++)
	for(uint32_t y = 0; y < ySize; y++)
	for(uint32_t z = 0; z < zSize; z++)
	{
		//read voxel
		uint8_t* elem = arr + x * buf.strides[0] + y * buf.strides[1] + z * buf.strides[2];

		uint8_t r, g, b, a;
		if(isFloat)
		{
			float rFloat = *(float*)(elem);
			float gFloat = *(float*)(elem + buf.strides[3]);
			float bFloat = *(float*)(elem + 2 * buf.strides[3]);
			float aFloat = *(float*)(elem + 3 * buf.strides[3]);

			r = (uint8_t)(std::min(std::max(rFloat, 0.0f), 1.0f) * 255.0f);
			g = (uint8_t)(std::min(std::max(gFloat, 0.0f), 1.0f) * 255.0f);
			b = (uint8_t)(std::min(std::max(bFloat, 0.0f), 1.0f) * 255.0f);
			a = (uint8_t)(std::min(std::max(aFloat, 0.0f), 1.0f) * 255.0f);
		}
		else
		{
			r = *(elem);
			g = *(elem + buf.strides[3]);
			b = *(elem + 2 * buf.strides[3]);
			a = *(elem + 3 * buf.strides[3]);
		}

		//continue if voxel empty
		if(a == 0)
			continue;

		//write voxel
		uint32_t readCoord[3] = {x, y, z};
		uint32_t xWrite = readCoord[(uint32_t)lrAxis];
		uint32_t yWrite = readCoord[(uint32_t)udAxis];
		uint32_t zWrite = readCoord[(uint32_t)fbAxis];

		uint32_t xMap   = xWrite / SPLV_BRICK_SIZE;
		uint32_t yMap   = yWrite / SPLV_BRICK_SIZE;
		uint32_t zMap   = zWrite / SPLV_BRICK_SIZE;
		uint32_t xBrick = xWrite % SPLV_BRICK_SIZE;
		uint32_t yBrick = yWrite % SPLV_BRICK_SIZE;
		uint32_t zBrick = zWrite % SPLV_BRICK_SIZE;

		uint32_t mapIdx = splv_frame_get_map_idx(&frame, xMap, yMap, zMap);

		SPLVbrick* brick;
		if(frame.map[mapIdx] == SPLV_BRICK_IDX_EMPTY)
		{
			brick = splv_frame_get_next_brick(&frame);
			splv_brick_clear(brick);

			SPLVerror pushError = splv_frame_push_next_brick(&frame, xMap, yMap, zMap);
			if(pushError != SPLV_SUCCESS)
			{
				splv_frame_destroy(&frame);

				std::cout << "ERROR: failed to push brick to frame with code " << 
					frameError << " (" << splv_get_error_string(frameError) << ")\n";
				throw std::runtime_error("");
			}
		}
		else
			brick = &frame.bricks[frame.map[mapIdx]];

		splv_brick_set_voxel_filled(brick, xBrick, yBrick, zBrick, r, g, b);
	}

	//encode frame + cleanup:
	//---------------
	m_activeFrames.push_back(frame);
	encode_frame(&frame, removeNonvisible);
}

//-------------------------------------------//

void PySPLVencoder::encode_frame(SPLVframe* frame, bool removeNonvisible)
{
	//validate:
	//---------------
	if(frame->width  * SPLV_BRICK_SIZE != m_encoder.width  || 
	   frame->height * SPLV_BRICK_SIZE != m_encoder.height || 
	   frame->depth  * SPLV_BRICK_SIZE != m_encoder.depth)
	{
		std::cout << "ERROR: frame dimensions do not match encoder's\n";
		throw std::runtime_error("");
	}

	//preprocess frame:
	//---------------
	SPLVframe processedFrame;
	if(removeNonvisible)
	{
		SPLVerror processingError = splv_frame_remove_nonvisible_voxels(frame, &processedFrame);
		if(processingError != SPLV_SUCCESS)
		{
			std::cout << "ERROR: failed to remove nonvisible voxels with code " <<
				processingError << " (" << splv_get_error_string(processingError) << ")\n";
			throw std::runtime_error("");
		}

		m_activeFrames.push_back(processedFrame);
		frame = &processedFrame;
	}

	//encode:
	//---------------
	splv_bool_t canRemove;
	SPLVerror encodeError = splv_encoder_encode_frame(&m_encoder, frame, &canRemove);
	if(encodeError != SPLV_SUCCESS)
	{
		std::cout << "ERROR: failed to encode frame with code " 
			<< encodeError << " (" << splv_get_error_string(encodeError) << ")\n";
		throw std::runtime_error("");
	}

	//free active frames:
	//---------------
	if(canRemove)
		free_frames();
}

void PySPLVencoder::free_frames()
{
	for(uint32_t i = 0; i < (uint32_t)m_activeFrames.size(); i++)
		splv_frame_destroy(&m_activeFrames[i]);
	m_activeFrames.clear();

	for(uint32_t i = 0; i < (uint32_t)m_activeVoxFrames.size(); i++)
		splv_vox_frames_destroy(m_activeVoxFrames[i].first, m_activeVoxFrames[i].second);
	m_activeVoxFrames.clear();
}

//-------------------------------------------//

SPLVaxis PySPLVencoder::parse_axis(std::string s)
{
	if(s == "x")
		return SPLV_AXIS_X;
	else if(s == "y")
		return SPLV_AXIS_Y;
	else if(s == "z")
		return SPLV_AXIS_Z;
	else
	{
		std::cout << "ERROR: invalid axis, must be one of \"x\", \"y\", or \"z\"\n";
		throw std::invalid_argument("");
	}
}

void PySPLVencoder::validate_axes(SPLVaxis lrAxis, SPLVaxis udAxis, SPLVaxis fbAxis)
{
	if(lrAxis == udAxis || lrAxis == fbAxis || udAxis == fbAxis)
	{
		std::cout << "ERROR: axes must be distinct" << std::endl;
		throw std::runtime_error("");
	}
}

void PySPLVencoder::validate_bounding_box(int32_t minX, int32_t minY, int32_t minZ, int32_t maxX, int32_t maxY, int32_t maxZ)
{
	int32_t xSize = maxX - minX + 1;
	int32_t ySize = maxY - minY + 1;
	int32_t zSize = maxZ - minZ + 1;
	if(xSize <= 0 || ySize <= 0 || zSize <= 0)
	{
		std::cout << "ERROR: bounding box dimensions must be positive" << std::endl;
		throw std::runtime_error("");
	}

	if(xSize % SPLV_BRICK_SIZE != 0 || ySize % SPLV_BRICK_SIZE != 0 || zSize % SPLV_BRICK_SIZE != 0)
	{
		std::cout << "ERROR: bounding box dimensions must be multiples of SPLV_BRICK_SIZE" << std::endl;
		throw std::runtime_error("");
	}
}

//-------------------------------------------//

std::tuple<uint32_t, uint32_t, uint32_t> get_vox_max_dimensions(std::string path)
{
	uint32_t xSize;
	uint32_t ySize;
	uint32_t zSize;

	SPLVerror error = splv_vox_get_max_dimensions(path.c_str(), &xSize, &ySize, &zSize);
	if(error != SPLV_SUCCESS)
	{
		std::cout << "ERROR: failed to get max .vox file dimensions with code " <<
			error << " (" << splv_get_error_string(error) << ")\n";
		throw std::runtime_error("");
	}

	return std::make_tuple(xSize, ySize, zSize);
}

void concat(const py::list& paths, const std::string& outPath)
{
    std::vector<std::string> stdPaths;
    std::vector<const char*> cPaths;

	stdPaths.reserve(paths.size());
    cPaths.reserve(paths.size());
    for(const auto& path : paths)
	{
        stdPaths.push_back(py::cast<std::string>(path));
        cPaths.push_back(stdPaths.back().c_str());
	}

	SPLVerror error = splv_file_concat((uint32_t)cPaths.size(), cPaths.data(), outPath.c_str());
	if(error != SPLV_SUCCESS)
	{
		std::cout << "ERROR: failed to concatenate splv files with code " <<
			error << " (" << splv_get_error_string(error) << ")\n";
		throw std::runtime_error("");
	}
}

uint32_t split(const std::string& path, float splitLength, const std::string& outDir)
{
    uint32_t numSplits = 0;
    SPLVerror error = splv_file_split(path.c_str(), splitLength, outDir.c_str(), &numSplits);
	if(error != SPLV_SUCCESS)
	{
		std::cout << "ERROR: failed to split splv file with code " <<
			error << " (" << splv_get_error_string(error) << ")\n";
		throw std::runtime_error("");
	}

    return numSplits;
}

void upgrade(const std::string& path, const std::string& outPath, uint32_t gopSize, uint32_t maxBrickGroupSize)
{
	SPLVencodingParams encodingParams = {0};
	encodingParams.gopSize = gopSize;
	encodingParams.maxBrickGroupSize = maxBrickGroupSize;

    SPLVerror error = splv_file_upgrade(path.c_str(), outPath.c_str(), encodingParams);
	if(error != SPLV_SUCCESS)
	{
		std::cout << "ERROR: failed to upgrade splv file with code " <<
			error << " (" << splv_get_error_string(error) << ")\n";
		throw std::runtime_error("");
	}
}

//-------------------------------------------//

PYBIND11_MODULE(splv_encoder_py, m) {
	m.doc() = "SPLV Encoder";

	py::class_<PySPLVencoder>(m, "SPLVencoder")
		.def(py::init<uint32_t, uint32_t, uint32_t, float, uint32_t, uint32_t, const std::string&>(),
			py::arg("width"),
			py::arg("height"),
			py::arg("depth"),
			py::arg("framerate"),
			py::arg("gopSize"),
			py::arg("maxBrickGroupSize"),
			py::arg("outputPath"),
			"Create a new SPLVencoder instance")
		.def("encode_nvdb_frame", &PySPLVencoder::encode_nvdb_frame,
			py::arg("path"),
			py::arg("minX"),
			py::arg("minY"),
			py::arg("minZ"),
			py::arg("maxX"),
			py::arg("maxY"),
			py::arg("maxZ"),
			py::arg("lrAxis") = "x",
			py::arg("udAxis") = "y",
			py::arg("fbAxis") = "z",
			py::arg("removeNonvisible") = false,
			"Add a frame from an NVDB file")
		.def("encode_vox_frame", &PySPLVencoder::encode_vox_frame,
			py::arg("path"),
			py::arg("minX"),
			py::arg("minY"),
			py::arg("minZ"),
			py::arg("maxX"),
			py::arg("maxY"),
			py::arg("maxZ"),
			py::arg("removeNonvisible") = false,
			"Add a frame from a MagicaVoxel .vox file")
		.def("encode_numpy_frame_float", &PySPLVencoder::encode_numpy_frame_float,
			py::arg("arr"),
			py::arg("lrAxis") = "x",
			py::arg("udAxis") = "y",
			py::arg("fbAxis") = "z",
			py::arg("removeNonvisible") = false,
			"Add a frame from an numpy array of floats")
		.def("encode_numpy_frame_byte", &PySPLVencoder::encode_numpy_frame_byte,
			py::arg("arr"),
			py::arg("lrAxis") = "x",
			py::arg("udAxis") = "y",
			py::arg("fbAxis") = "z",
			py::arg("removeNonvisible") = false,
			"Add a frame from an numpy array of bytes")
		.def("finish", &PySPLVencoder::finish,
			"Finish encoding and close the output file")
		.def("abort", &PySPLVencoder::abort,
			"Abort encoding in error and close the output file");

	m.def("get_vox_max_dimensions", &get_vox_max_dimensions,
		py::arg("path"),
		"Returns the maximum dimensions of frames in a MagicaVoxel .vox file");

	m.def("concat", &concat,
		py::arg("paths"),
		py::arg("outPath"),
		"Concatenates multiple SPLV files togethers");

	m.def("split", &split,
		py::arg("path"),
		py::arg("splitLength"),
		py::arg("outDir"),
		"Splits an SPLV file into multiple files of the specified duration");

	m.def("upgrade", &upgrade,
		py::arg("path"),
		py::arg("outPath"),
		py::arg("gopSize"),
		py::arg("maxBrickGroupSize"),
		"Upgrades an SPLV file from the previous version to the current one");
}