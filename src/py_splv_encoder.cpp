#include "py_splv_encoder.hpp"

#define NANOVDB_USE_BLOSC
#include <nanovdb/NanoVDB.h>
#include <nanovdb/util/IO.h>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

//-------------------------------------------//

PySPLVEncoder::PySPLVEncoder(uint32_t xSize, uint32_t ySize, uint32_t zSize, std::string lrAxis, std::string udAxis, std::string fbAxis, float framerate, std::string outPath) :
	m_outFile(outPath, std::ofstream::binary)
{
	if(!m_outFile.is_open())
		throw std::runtime_error("ERROR: failed to open output file for writing: " + outPath);

	m_encoder = std::make_unique<SPLVEncoder>(xSize, ySize, zSize, parse_axis(lrAxis), parse_axis(udAxis), parse_axis(fbAxis), framerate, m_outFile);
}

void PySPLVEncoder::add_nvdb_frame(std::string path, uint32_t minX, uint32_t minY, uint32_t minZ, uint32_t maxX, uint32_t maxY, uint32_t maxZ, bool removeNonvisible)
{
	try
	{
		auto file = nanovdb::io::readGrid(path);
		auto* grid = file.grid<nanovdb::Vec3f>();
		if(!grid)
			throw std::runtime_error("NVDB file specified did not contain a Vec3f grid");

		m_encoder->add_nvdb_frame(grid, nanovdb::CoordBBox(nanovdb::Coord(minX, minY, minZ), nanovdb::Coord(maxX, maxY, maxZ)), removeNonvisible);
	}
	catch(std::exception e)
	{
		throw std::runtime_error(e.what());
	}
}

void PySPLVEncoder::add_vox_frame(std::string path, bool removeNonvisible)
{
	try
	{
		m_encoder->add_vox_frame(path, removeNonvisible);
	}
	catch(std::exception e)
	{
		throw std::runtime_error(e.what());
	}
}

void PySPLVEncoder::finish()
{
	m_encoder->finish();
}

Axis PySPLVEncoder::parse_axis(std::string s)
{
	if(s == "x")
		return Axis::X;
	else if(s == "y")
		return Axis::Y;
	else if(s == "z")
		return Axis::Z;
	else
		throw std::invalid_argument("invalid axis, must be one of \"x\", \"y\", or \"z\"");
}

//-------------------------------------------//

namespace py = pybind11;

PYBIND11_MODULE(py_splv_encoder, m) {
	m.doc() = "SPLV Encoder";

    py::class_<PySPLVEncoder>(m, "SPLVEncoder")
        .def(py::init<uint32_t, uint32_t, uint32_t, const std::string&, const std::string&, const std::string&, float, const std::string&>(),
             py::arg("xSize"),
             py::arg("ySize"),
             py::arg("zSize"),
			 py::arg("lrAxis"),
			 py::arg("udAxis"),
			 py::arg("fbAxis"),
             py::arg("framerate"),
             py::arg("outputPath"),
             "Create a new SPLVEncoder instance")
        .def("add_nvdb_frame", &PySPLVEncoder::add_nvdb_frame,
             py::arg("path"),
			 py::arg("minX"),
			 py::arg("minY"),
			 py::arg("minZ"),
			 py::arg("maxX"),
			 py::arg("maxY"),
			 py::arg("maxZ"),
			 py::arg("removeNonvisible") = false,
             "Add a frame from an NVDB file")
		.def("add_vox_frame", &PySPLVEncoder::add_vox_frame,
			py::arg("path"),
			py::arg("removeNonvisible") = false,
			"Add a frame from a MagicaVoxel .vox file")
        .def("finish", &PySPLVEncoder::finish,
             "Finish encoding and close the output file");
}