#include "splv_encoder.hpp"
#include <vector>

//-------------------------------------------//

struct SPLVHeader
{
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	float framerate;
	uint32_t frameCount;
	float duration;
};

//-------------------------------------------//

SPLVEncoder::SPLVEncoder(uint32_t xSize, uint32_t ySize, uint32_t zSize, Axis lrAxis, Axis udAxis, Axis fbAxis, float framerate, std::ofstream& outFile) : 
	m_xSize(xSize), m_ySize(ySize), m_zSize(zSize), m_udAxis(udAxis), m_lrAxis(lrAxis), m_fbAxis(fbAxis), m_framerate(framerate), m_frameCount(0), m_valid(false), m_outFile(outFile)
{
	//validate params:
	//---------------
	if(xSize == 0 || ySize == 0 || zSize == 0)
		throw std::invalid_argument("volume dimensions must be positive");

	if(xSize % BRICK_SIZE > 0 || ySize % BRICK_SIZE > 0 || zSize % BRICK_SIZE > 0)
		throw std::invalid_argument("volume dimensions must be a multiple of the brick size (" + std::to_string(BRICK_SIZE) + ")");

	if(lrAxis == udAxis || lrAxis == fbAxis || udAxis == fbAxis)
		throw std::invalid_argument("axes must be distinct");

	if(framerate <= 0.0f)
		throw std::invalid_argument("framerate must be positive");

	//write empty header (will write over with complete header when encoding is finished):
	//---------------
	SPLVHeader emptyHeader = {};
	m_outFile.write((const char*)&emptyHeader, sizeof(emptyHeader));

	//set valid:
	//---------------
	m_valid = true;
}

void SPLVEncoder::add_nvdb_frame(nanovdb::Vec3fGrid* grid, nanovdb::CoordBBox boundingBox, bool scaleToFit)
{
	if(!m_valid)
		throw std::invalid_argument("encoder is not valid (invalid params or finished encoding)");

	//scale grid and bounding box if needed:
	//---------------
	bool divScaleX = false;
	bool divScaleY = false;
	bool divScaleZ = false;

	uint32_t scaleX = 1;
	uint32_t scaleY = 1;
	uint32_t scaleZ = 1;

	if(scaleToFit)
	{
		//TOOD: make sure this still works with nvdb

		float curWidth  = (float)(boundingBox.max().asVec3d()[0] - boundingBox.min().asVec3d()[0]) + 1.0f;
		float curHeight = (float)(boundingBox.max().asVec3d()[1] - boundingBox.min().asVec3d()[1]) + 1.0f;
		float curDepth  = (float)(boundingBox.max().asVec3d()[2] - boundingBox.min().asVec3d()[2]) + 1.0f;
		
		nanovdb::Vec3d center = (boundingBox.max().asVec3d() + boundingBox.min().asVec3d()) / 2.0f;
		nanovdb::Coord newStart(
			(int32_t)std::round(center[0] - (float)m_xSize  / 2.0f),
			(int32_t)std::round(center[1] - (float)m_ySize / 2.0f),
			(int32_t)std::round(center[2] - (float)m_zSize  / 2.0f)
		);
		nanovdb::Coord newEnd(
			newStart[0] + m_xSize  - 1,
			newStart[1] + m_ySize - 1,
			newStart[2] + m_zSize  - 1
		);
		
		boundingBox = nanovdb::CoordBBox(newStart, newEnd);
		
		float curScaleX = curWidth  / (float)m_xSize;
		float curScaleY = curHeight / (float)m_ySize;
		float curScaleZ = curDepth  / (float)m_zSize;
		
		divScaleX = curScaleX < 1.0f;
		divScaleY = curScaleY < 1.0f;
		divScaleZ = curScaleZ < 1.0f;
		scaleX = (uint32_t)std::roundf(divScaleX ? (1.0f / curScaleX) : curScaleX);
		scaleY = (uint32_t)std::roundf(divScaleY ? (1.0f / curScaleY) : curScaleY);
		scaleZ = (uint32_t)std::roundf(divScaleZ ? (1.0f / curScaleZ) : curScaleZ);
	}

	//get size of region:
	//---------------
	int32_t minX = (int32_t)std::round(boundingBox.min().asVec3d()[0]);
	int32_t minY = (int32_t)std::round(boundingBox.min().asVec3d()[1]);
	int32_t minZ = (int32_t)std::round(boundingBox.min().asVec3d()[2]);

	int32_t maxX = (int32_t)std::round(boundingBox.max().asVec3d()[0]);
	int32_t maxY = (int32_t)std::round(boundingBox.max().asVec3d()[1]);
	int32_t maxZ = (int32_t)std::round(boundingBox.max().asVec3d()[2]);

	uint32_t xSize = maxX - minX + 1;
	uint32_t ySize = maxY - minY + 1;
	uint32_t zSize = maxZ - minZ + 1;

	if(xSize != m_xSize || ySize != m_ySize || zSize != m_zSize)
		throw std::invalid_argument("volume dimensions must match for every frame");

	//initialize map and bricks vector:
	//---------------
	uint32_t mapXSize = xSize  / BRICK_SIZE;
	uint32_t mapYSize = ySize / BRICK_SIZE;
	uint32_t mapZSize = zSize  / BRICK_SIZE;
	
	uint32_t mapLen = mapXSize * mapYSize * mapZSize;
	mapLen = (mapLen + 31) & (~31); //round up to multiple of 32 (sizeof(uint32_t))
	mapLen /= 4; //4 bytes per uint32_t
	mapLen /= 8; //8 bits per byte

	std::unique_ptr<uint32_t[]> map = std::unique_ptr<uint32_t[]>(new uint32_t[mapLen]);
	std::vector<Brick> bricks;

	//initialize map and bricks vector:
	//---------------
	uint32_t mapSize[3] = {mapXSize, mapYSize, mapZSize};

	uint32_t mapWidth  = mapSize[(uint32_t)m_lrAxis];
	uint32_t mapHeight = mapSize[(uint32_t)m_udAxis];
	uint32_t mapDepth  = mapSize[(uint32_t)m_fbAxis];

	//generate frame:
	//---------------
	auto accessor = grid->getAccessor();

	//we are creating bricks in xyz order, we MUST make sure to read it back in the same order
	for(uint32_t mapX = 0; mapX < mapWidth;  mapX++)
	for(uint32_t mapY = 0; mapY < mapHeight; mapY++)
	for(uint32_t mapZ = 0; mapZ < mapDepth;  mapZ++)
	{
		Brick brick;
		bool brickCreated = false;

		//we are calling set_voxel in xyz order, we MUST make sure to read it back in the same order
		for(uint32_t x = 0; x < BRICK_SIZE; x++)
		for(uint32_t y = 0; y < BRICK_SIZE; y++)
		for(uint32_t z = 0; z < BRICK_SIZE; z++)
		{
			int32_t readX = mapX * BRICK_SIZE + x + minX;
			int32_t readY = mapY * BRICK_SIZE + y + minY;
			int32_t readZ = mapZ * BRICK_SIZE + z + minZ;

			readX = divScaleX ? (readX / scaleX) : (readX * scaleX);
			readY = divScaleY ? (readY / scaleY) : (readY * scaleY);
			readZ = divScaleZ ? (readZ / scaleZ) : (readZ * scaleZ);

			int32_t readCoord[3] = {readX, readY, readZ};
			nanovdb::Coord readCoordNVDB(
				readCoord[(uint32_t)m_lrAxis], 
				readCoord[(uint32_t)m_udAxis], 
				readCoord[(uint32_t)m_fbAxis]
			);

			if(!accessor.isActive(readCoordNVDB))
				continue;

			brickCreated = true;
			
			nanovdb::Vec3f normColor = accessor.getValue(readCoordNVDB);
			Color color((uint8_t)std::roundf(normColor[0] * 255.0f), (uint8_t)std::roundf(normColor[1] * 255.0f), (uint8_t)std::roundf(normColor[2] * 255.0f));
			brick.set_voxel(x, y, z, color);
		}

		uint32_t mapIdx = mapX + mapWidth * (mapY + mapHeight * mapZ);
		uint32_t mapIdxArr = mapIdx / 32;
		uint32_t mapIdxBit = mapIdx % 32;
		
		if(brickCreated)
		{
			map[mapIdxArr] |= (1u << mapIdxBit);
			bricks.push_back(std::move(brick));
		}
		else
			map[mapIdxArr] &= ~(1u << mapIdxBit);
	}

	//write frame:
	//---------------
	uint32_t numBricks = (uint32_t)bricks.size();

	m_outFile.write((const char*)&numBricks, sizeof(uint32_t));
	m_outFile.write((const char*)map.get(), mapLen * sizeof(uint32_t));
	for(uint32_t i = 0; i < bricks.size(); i++)
		bricks[i].serialize(m_outFile);

	//increase frame count:
	//---------------
	m_frameCount++;
}

void SPLVEncoder::finish()
{
	SPLVHeader header = {};
	header.width = m_xSize;
	header.height = m_ySize;
	header.depth = m_zSize;
	header.framerate = m_framerate;
	header.frameCount = m_frameCount;
	header.duration = (float)m_frameCount / m_framerate;

	m_outFile.seekp(std::ios::beg);
	m_outFile.write((const char*)&header, sizeof(SPLVHeader));

	m_valid = false;
}