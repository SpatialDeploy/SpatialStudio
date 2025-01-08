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

	void add_nvdb_frame(nanovdb::Vec3fGrid* grid, nanovdb::CoordBBox boundingBox, bool removeNonvisible);
	void add_vox_frame(const std::string& path, bool removeNonvisible);
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
	std::vector<uint64_t> m_framePtrs;

	bool m_valid;
	std::ofstream& m_outFile;

	//-------------//

	struct Frame
	{
		std::unique_ptr<uint32_t[]> map;
		std::vector<Brick> bricks;
	};

	std::shared_ptr<Frame> create_nvdb_frame(nanovdb::Vec3fGrid* grid, nanovdb::CoordBBox boundingBox);
	std::shared_ptr<Frame> create_vox_frame(std::ifstream& file, uint32_t sizePtr, uint32_t xyziPtr, uint32_t* palette);
	std::shared_ptr<Frame> remove_nonvisible_voxels(std::shared_ptr<Frame> frame);
	void encode_frame(std::shared_ptr<Frame> frame);
};

#endif //#ifndef SPLV_ENCODER_H