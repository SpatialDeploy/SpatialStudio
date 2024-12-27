#include "splv_encoder.hpp"

#include <vector>
#include "morton_lut.hpp"

#define PRINT_INFO 0

#if PRINT_INFO
	#include <iostream>
#endif

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

void SPLVEncoder::add_nvdb_frame(nanovdb::Vec3fGrid* grid, nanovdb::CoordBBox boundingBox)
{
	if(!m_valid)
		throw std::invalid_argument("encoder is not valid (invalid params or finished encoding)");

	std::unique_ptr<Frame> frame = create_frame(grid, boundingBox);
	encode_frame(std::move(frame));

	m_frameCount++;
}

std::unique_ptr<SPLVEncoder::Frame> SPLVEncoder::create_frame(nanovdb::Vec3fGrid* grid, nanovdb::CoordBBox boundingBox)
{
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
	uint32_t mapXSize = xSize / BRICK_SIZE;
	uint32_t mapYSize = ySize / BRICK_SIZE;
	uint32_t mapZSize = zSize / BRICK_SIZE;
	
	uint32_t mapLen = mapXSize * mapYSize * mapZSize;

	std::unique_ptr<Frame> frame = std::make_unique<Frame>();
	frame->map = std::unique_ptr<uint32_t[]>(new uint32_t[mapLen]);

	//TODO fix this!!!!!

	uint32_t mapSize[3] = {mapXSize, mapYSize, mapZSize};

	uint32_t mapWidth  = mapSize[(uint32_t)m_lrAxis];
	uint32_t mapHeight = mapSize[(uint32_t)m_udAxis];
	uint32_t mapDepth  = mapSize[(uint32_t)m_fbAxis];

	//generate frame:
	//---------------
	auto accessor = grid->getAccessor();

	for(uint32_t mapZ = 0; mapZ < mapDepth;  mapZ++)
	for(uint32_t mapY = 0; mapY < mapHeight; mapY++)
	for(uint32_t mapX = 0; mapX < mapWidth;  mapX++)
	{
		Brick brick;
		bool brickCreated = false;

		for(uint32_t z = 0; z < BRICK_SIZE; z++)
		for(uint32_t y = 0; y < BRICK_SIZE; y++)
		for(uint32_t x = 0; x < BRICK_SIZE; x++)
		{
			int32_t readX = mapX * BRICK_SIZE + x + minX;
			int32_t readY = mapY * BRICK_SIZE + y + minY;
			int32_t readZ = mapZ * BRICK_SIZE + z + minZ;

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
		
		if(brickCreated)
		{
			frame->map[mapIdx] = (uint32_t)frame->bricks.size();
			frame->bricks.push_back(std::move(brick));
		}
		else
			frame->map[mapIdx] = EMPTY_BRICK;
	}

	//return:
	//---------------
	return frame;
}

void SPLVEncoder::encode_frame(std::unique_ptr<Frame> frame)
{
	//compress map (convert to bitmap):
	//---------------
	uint32_t mapXSize = m_xSize / BRICK_SIZE;
	uint32_t mapYSize = m_ySize / BRICK_SIZE;
	uint32_t mapZSize = m_zSize / BRICK_SIZE;
	
	uint32_t mapLenBitmap = mapXSize * mapYSize * mapZSize;
	mapLenBitmap = (mapLenBitmap + 31) & (~31); //round up to multiple of 32 (sizeof(uint32_t))
	mapLenBitmap /= 4; //4 bytes per uint32_t
	mapLenBitmap /= 8; //8 bits per byte

	//automatically 0-initialized
	std::unique_ptr<uint32_t[]> mapBitmap = std::unique_ptr<uint32_t[]>(new uint32_t[mapLenBitmap]());
	std::vector<std::reference_wrapper<Brick>> bricksOrdered = {};

	uint32_t mapSize[3] = {mapXSize, mapYSize, mapZSize};

	uint32_t mapWidth  = mapSize[(uint32_t)m_lrAxis];
	uint32_t mapHeight = mapSize[(uint32_t)m_udAxis];
	uint32_t mapDepth  = mapSize[(uint32_t)m_fbAxis];

	//we are writing bricks in xyz order, we MUST make sure to read them back in the same order
	for(uint32_t mapX = 0; mapX < mapWidth;  mapX++)
	for(uint32_t mapY = 0; mapY < mapHeight; mapY++)
	for(uint32_t mapZ = 0; mapZ < mapDepth;  mapZ++)
	{
		uint32_t mapIdx = mapX + mapWidth * (mapY + mapHeight * mapZ);
		uint32_t mapIdxArr = mapIdx / 32;
		uint32_t mapIdxBit = mapIdx % 32;
		
		if(frame->map[mapIdx] != EMPTY_BRICK)
		{
			mapBitmap[mapIdxArr] |= (1u << mapIdxBit);
			bricksOrdered.push_back(frame->bricks[frame->map[mapIdx]]);
		}
		else
			mapBitmap[mapIdxArr] &= ~(1u << mapIdxBit);
	}

	//sanity check
	if(frame->bricks.size() != bricksOrdered.size())
		throw std::runtime_error("number of bricks in raw frame and encoded frame do not match!");

	//write frame:
	//---------------
	#if PRINT_INFO
	{
		uint32_t numBricks = (uint32_t)bricksOrdered.size();

		m_outFile.write((const char*)&numBricks, sizeof(uint32_t));
		m_outFile.write((const char*)mapBitmap.get(), mapLenBitmap * sizeof(uint32_t));

		uint32_t mapBytes = mapLenBitmap * sizeof(uint32_t);
		uint32_t brickVoxelCounts = 0;
		uint32_t brickBytes = 0;
		uint32_t brickBytesBitmap = 0;
		uint32_t brickBytesColors = 0;
		for(uint32_t i = 0; i < bricksOrdered.size(); i++)
		{
			uint32_t voxelCount;
			uint32_t size;
			uint32_t sizeBitmap;
			uint32_t sizeColors;

			bricksOrdered[i].get().serialize_verbose(m_outFile, voxelCount, size, sizeBitmap, sizeColors);

			brickVoxelCounts += voxelCount;
			brickBytes += size;
			brickBytesBitmap += sizeBitmap;
			brickBytesColors += sizeColors;
		}

		uint32_t totalBytes = brickBytes + mapBytes;

		float brickBytesAvg = (float)brickBytes / (float)numBricks;
		float brickBytesAvgBitmap = (float)brickBytesBitmap / (float)numBricks;
		float brickBytesAvgColors = (float)brickBytesColors / (float)numBricks;

		std::cout << "FRAME " << m_frameCount << "\n";
		std::cout << "\t- Number of Bricks: " << numBricks << "\n";
		std::cout << "\t- Map Bytes: " << mapBytes << " (" << ((float)mapBytes / (float)totalBytes) * 100.0 << "%)\n";
		std::cout << "\t- Total Brick Bytes: " << brickBytes << " (" << ((float)brickBytes / (float)totalBytes) * 100.0 << "%)\n";
		std::cout << "\t\t- From Bitmaps: " << brickBytesBitmap << " (" << ((float)brickBytesBitmap / (float)brickBytes) * 100.0 << "%)\n";
		std::cout << "\t\t- From Colors: " << brickBytesColors << " (" << ((float)brickBytesColors / (float)brickBytes) * 100.0 << "%)\n";
		std::cout << "\t- Average Brick Bytes: " << brickBytesAvg << " (" << ((float)brickBytesAvg / (float)totalBytes) * 100.0 << "%)\n";
		std::cout << "\t\t- From Bitmaps: " << brickBytesAvgBitmap << " (" << (brickBytesAvgBitmap / brickBytesAvg) * 100.0 << "%)\n";
		std::cout << "\t\t- From Colors: " << brickBytesAvgColors << " (" << (brickBytesAvgColors / brickBytesAvg) * 100.0 << "%)\n";
	}
	#else
	{
		uint32_t numBricks = (uint32_t)bricksOrdered.size();

		m_outFile.write((const char*)&numBricks, sizeof(uint32_t));
		m_outFile.write((const char*)mapBitmap.get(), mapLenBitmap * sizeof(uint32_t));
		for(uint32_t i = 0; i < bricksOrdered.size(); i++)
			bricksOrdered[i].get().serialize(m_outFile);
	}
	#endif
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
	m_outFile.close();

	m_valid = false;
}