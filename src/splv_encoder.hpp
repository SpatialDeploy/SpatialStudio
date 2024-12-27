#ifndef SPLV_ENCODER_H
#define SPLV_ENCODER_H

#include <stdint.h>
#include <string>

#include "brick.hpp"

#define NANOVDB_USE_BLOSC
#include <nanovdb/NanoVDB.h>

//-------------------------------------------//

enum class Axis : uint32_t
{
	X = 0,
	Y = 1,
	Z = 2
};

//-------------------------------------------//

class SPLVEncoder
{
public:
	SPLVEncoder(uint32_t xSize, uint32_t ySize, uint32_t zSize, Axis lrAxis, Axis udAxis, Axis fbAxis, float framerate, std::ofstream& outFile);

	void add_nvdb_frame(nanovdb::Vec3fGrid* grid, nanovdb::CoordBBox boundingBox);
	void finish();

private:
	uint32_t m_xSize;
	uint32_t m_ySize;
	uint32_t m_zSize;

	Axis m_lrAxis;
	Axis m_udAxis;
	Axis m_fbAxis;

	float m_framerate;
	uint32_t m_frameCount;

	bool m_valid;
	std::ofstream& m_outFile;

	//-------------//

	struct Frame
	{
		std::unique_ptr<uint32_t[]> map;
		std::vector<Brick> bricks;
	};

	std::unique_ptr<Frame> create_frame(nanovdb::Vec3fGrid* grid, nanovdb::CoordBBox boundingBox);
	void encode_frame(std::unique_ptr<Frame> frame);
};

#endif //#ifndef SPLV_ENCODER_H