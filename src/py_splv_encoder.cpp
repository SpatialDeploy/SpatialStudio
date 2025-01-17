#include "py_splv_encoder.hpp"

#include "splv_vox_utils.h"
#include "splv_nvdb_utils.h"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <iostream>

//-------------------------------------------//

PySPLVencoder::PySPLVencoder(uint32_t width, uint32_t height, uint32_t depth, float framerate, std::string outPath)
{
	SPLVerror encoderError = splv_encoder_create(&m_encoder, width, height, depth, framerate, outPath.c_str());
	if(encoderError != SPLV_SUCCESS)
	{
		std::cout << "ERROR: failed to create SPLVencoder with code " <<
			encoderError << "(" << splv_get_error_string << ")\n";
		throw std::runtime_error("");
	}
}

void PySPLVencoder::encode_nvdb_frame(std::string path, int32_t minX, int32_t minY, int32_t minZ, 
                                      int32_t maxX, int32_t maxY, int32_t maxZ, std::string lrAxisStr, 
                                      std::string udAxisStr, std::string fbAxisStr, bool removeNonvisible)
{
	SPLVaxis lrAxis = parse_axis(lrAxisStr);
	SPLVaxis udAxis = parse_axis(udAxisStr);
	SPLVaxis fbAxis = parse_axis(fbAxisStr);

	SPLVboundingBox boundingBox = { minX, minY, minZ, maxX, maxY, maxZ };

	SPLVframe* frame;
	SPLVerror nvdbError = splv_nvdb_load(((std::string)path).c_str(), &frame, &boundingBox, lrAxis, udAxis, fbAxis);
	if(nvdbError != SPLV_SUCCESS)
	{
		std::cout << "ERROR: failed to create nvdb frame with code " <<
			nvdbError << "(" << splv_get_error_string(nvdbError) << ")\n";
		throw std::runtime_error("");
	}

	encode_frame(frame, removeNonvisible);
	
	splv_frame_destroy(frame);
}

void PySPLVencoder::encode_vox_frame(std::string path, int32_t minX, int32_t minY, int32_t minZ, 
                                     int32_t maxX, int32_t maxY, int32_t maxZ, bool removeNonvisible)
{
	SPLVboundingBox boundingBox = { minX, minY, minZ, maxX, maxY, maxZ };

	uint32_t numFrames;
	SPLVframe** frames;
	SPLVerror voxError = splv_vox_load(((std::string)path).c_str(), &frames, &numFrames, &boundingBox);
	if(voxError != SPLV_SUCCESS)
	{
		std::cout << "ERROR: failed to create vox frames with code " <<
			voxError << "(" << splv_get_error_string(voxError) << ")\n";
		throw std::runtime_error("");
	}

	for(uint32_t i = 0; i < numFrames; i++)
		encode_frame(frames[i], removeNonvisible);

	splv_vox_frames_destroy(frames, numFrames);
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
	SPLVerror finishError = splv_encoder_finish(m_encoder);
	if(finishError != SPLV_SUCCESS)
	{
		std::cout << "ERROR: failed to finish encoding with code " << 
			finishError << "(" << splv_get_error_string(finishError) << ")\n";
		throw std::runtime_error("");
	}
}

void PySPLVencoder::abort()
{
	splv_encoder_abort(m_encoder);
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

	if(lrAxis == udAxis || lrAxis == fbAxis || udAxis == fbAxis)
	{
		std::cout << "ERROR: axes must be distinct\n";
		throw std::runtime_error("");
	}

	//create frame:
	//---------------
	uint32_t sizes[3] = {xSize, ySize, zSize};
	uint32_t widthMap  = sizes[(uint32_t)lrAxis] / SPLV_BRICK_SIZE;
	uint32_t heightMap = sizes[(uint32_t)udAxis] / SPLV_BRICK_SIZE;
	uint32_t depthMap  = sizes[(uint32_t)fbAxis] / SPLV_BRICK_SIZE;

	SPLVframe* frame;
	SPLVerror frameError = splv_frame_create(&frame, widthMap, heightMap, depthMap);
	if(frameError != SPLV_SUCCESS)
	{
		std::cout << "ERROR: failed to create frame with code " << 
			frameError << " (" << splv_get_error_string(frameError) << ")\n";
		throw std::runtime_error("");
	}

	for(uint32_t i = 0; i < widthMap * heightMap * depthMap; i++)
		frame->map[i] = SPLV_BRICK_IDX_EMPTY;

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

		uint32_t mapIdx = splv_frame_get_map_idx(frame, xMap, yMap, zMap);

		SPLVbrick* brick;
		if(frame->map[mapIdx] == SPLV_BRICK_IDX_EMPTY)
		{
			brick = splv_frame_get_next_brick(frame);
			splv_brick_clear(brick);

			SPLVerror pushError = splv_frame_push_next_brick(frame, xMap, yMap, zMap);
			if(pushError != SPLV_SUCCESS)
			{
				std::cout << "ERROR: failed to push brick to frame with code " << 
					frameError << " (" << splv_get_error_string(frameError) << ")\n";
				throw std::runtime_error("");
			}
		}
		else
			brick = &frame->bricks[frame->map[mapIdx]];

		splv_brick_set_voxel_filled(brick, xBrick, yBrick, zBrick, r, g, b);
	}

	//encode frame + cleanup:
	//---------------
	encode_frame(frame, removeNonvisible);

	splv_frame_destroy(frame);
}

void PySPLVencoder::encode_frame(SPLVframe* frame, bool removeNonvisible)
{
	SPLVframe* processedFrame;
	if(removeNonvisible)
	{
		SPLVerror processingError = splv_frame_remove_nonvisible_voxels(frame, &processedFrame);
		if(processingError != SPLV_SUCCESS)
		{
			std::cout << "ERROR: failed to remove nonvisible voxels with code " <<
				processingError << " (" << splv_get_error_string(processingError) << ")\n";
			return;
		}
	}
	else
		processedFrame = frame;

	SPLVerror encodeError = splv_encoder_encode_frame(m_encoder, processedFrame);
	if(encodeError != SPLV_SUCCESS)
	{
		std::cout << "ERROR: failed to encode frame with code " 
			<< encodeError << " (" << splv_get_error_string(encodeError) << ")\n";
	}

	if(removeNonvisible)
		splv_frame_destroy(processedFrame);
}

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
			error << "(" << splv_get_error_string(error) << ")\n";
		throw std::runtime_error("");
	}

	return std::make_tuple(xSize, ySize, zSize);
}

//-------------------------------------------//

PYBIND11_MODULE(splv_encoder_py, m) {
	m.doc() = "SPLV Encoder";

	py::class_<PySPLVencoder>(m, "SPLVencoder")
		.def(py::init<uint32_t, uint32_t, uint32_t, float, const std::string&>(),
			py::arg("width"),
			py::arg("height"),
			py::arg("depth"),
			py::arg("framerate"),
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
		"Returns the maximum dimensions of frames in a MagicaVoxel .vox file");
}