#ifndef PY_ENCODER_H
#define PY_ENCODER_H

#include "splv_encoder.hpp"
#include <memory.h>

//-------------------------------------------//

class PySPLVEncoder
{
public:
	PySPLVEncoder(uint32_t width, uint32_t height, uint32_t depth, std::string lrAxis, std::string udAxis, std::string fbAxis, float framerate, std::string outPath);

	void add_nvdb_frame(std::string path, uint32_t minX, uint32_t minY, uint32_t minZ, uint32_t maxX, uint32_t maxY, uint32_t maxZ, bool scaleToFit = false);

	void finish();

private:
	std::unique_ptr<SPLVEncoder> m_encoder;
	std::ofstream m_outFile;

	static Axis parse_axis(std::string s);
};

#endif //#ifndef PY_ENCODER_H