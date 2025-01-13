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

void PySPLVencoder::encode_frame(SPLVframe* frame, bool removeNonvisible)
{
	SPLVframe* processedFrame;
	if(removeNonvisible)
	{
		SPLVerror processingError = splv_frame_remove_nonvisible_voxels(frame, &processedFrame);
		if(processingError != SPLV_SUCCESS)
		{
			std::cout << "ERROR: failed to remove nonvisible voxels with code " <<
				processingError << "(" << splv_get_error_string(processingError) << ")\n";
			return;
		}
	}
	else
		processedFrame = frame;

	SPLVerror encodeError = splv_encoder_encode_frame(m_encoder, processedFrame);
	if(encodeError != SPLV_SUCCESS)
	{
		std::cout << "ERROR: failed to encoder frame with code " 
			<< encodeError << "(" << splv_get_error_string(encodeError) << ")\n";
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

namespace py = pybind11;

PYBIND11_MODULE(py_splv_encoder, m) {
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
        .def("finish", &PySPLVencoder::finish,
            "Finish encoding and close the output file")
        .def("abort", &PySPLVencoder::abort,
            "Abort encoding in error and close the output file");

	m.def("get_vox_max_dimensions", &get_vox_max_dimensions,
		"Returns the maximum dimensions of frames in a MagicaVoxel .vox file");
}