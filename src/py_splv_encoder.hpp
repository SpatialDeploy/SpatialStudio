/* py_splv_encoder.hpp
 *
 * contains data/functions necessary for the sply encoder python bindings
 */

#ifndef PY_ENCODER_H
#define PY_ENCODER_H

#include "splv_encoder.h"
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <tuple>

namespace py = pybind11;

//-------------------------------------------//

class PySPLVencoder
{
public:
	PySPLVencoder(uint32_t width, uint32_t height, uint32_t depth, float framerate, std::string outPath);

	void encode_nvdb_frame(std::string path, int32_t minX, int32_t minY, int32_t minZ,
	                       int32_t maxX, int32_t maxY, int32_t maxZ, std::string lrAxis, 
						   std::string udAxis, std::string fbAxis, bool removeNonvisible = false);
	void encode_vox_frame(std::string path, int32_t minX, int32_t minY, int32_t minZ, 
	                      int32_t maxX, int32_t maxY, int32_t maxZ, bool removeNonvisible = false);
	void encode_numpy_frame_float(py::array_t<float> arr, std::string lrAxis, std::string udAxis, 
	                              std::string fbAxis, bool removeNonvisible = false);
	void encode_numpy_frame_byte(py::array_t<uint8_t> arr, std::string lrAxis, std::string udAxis, 
	                             std::string fbAxis, bool removeNonvisible = false);

	void finish();
	void abort();

private:
	SPLVencoder* m_encoder;

	//either encodes floatArr or byteArr, depending on which is non-NULL
	void encode_numpy_frame(py::array_t<float>* floatArr, py::array_t<uint8_t>* byteArr, std::string lrAxis, 
	                        std::string udAxis, std::string fbAxis, bool removeNonvisible);
	void encode_frame(SPLVframe* frame, bool removeNonvisible);

	static SPLVaxis parse_axis(std::string s);
};

//-------------------------------------------//

std::tuple<uint32_t, uint32_t, uint32_t> get_vox_max_dimensions(std::string path);

#endif //#ifndef PY_ENCODER_H