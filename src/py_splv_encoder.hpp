/* py_splv_encoder.hpp
 *
 * contains data/functions necessary for the sply encoder python bindings
 */

#ifndef PY_ENCODER_H
#define PY_ENCODER_H

#include "splv_encoder.h"
#include <tuple>

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

	void finish();
	void abort();

private:
	SPLVencoder* m_encoder;

	void encode_frame(SPLVframe* frame, bool removeNonvisible);

	static SPLVaxis parse_axis(std::string s);
};

//-------------------------------------------//

std::tuple<uint32_t, uint32_t, uint32_t> get_vox_max_dimensions(std::string path);

#endif //#ifndef PY_ENCODER_H