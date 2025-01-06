#include "splv_encoder.hpp"

#include <vector>
#include <queue>
#include <iostream>
#include "morton_lut.hpp"
#include "uint8_vector_stream.hpp"

#define QC_IMPLEMENTATION
#include "quickcompress.h"

//-------------------------------------------//

struct SPLVHeader
{
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	float framerate;
	uint32_t frameCount;
	float duration;
	uint64_t frameTablePtr;
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

	//swap sizes to respect axes:
	//---------------
	uint32_t sizes[3] = {m_xSize, m_ySize, m_zSize};
	m_xSize = sizes[(uint32_t)m_lrAxis];
	m_ySize = sizes[(uint32_t)m_udAxis];
	m_zSize = sizes[(uint32_t)m_fbAxis];

	//write empty header (will write over with complete header when encoding is finished):
	//---------------
	SPLVHeader emptyHeader = {};
	m_outFile.write((const char*)&emptyHeader, sizeof(emptyHeader));

	//set valid:
	//---------------
	m_valid = true;
}

void SPLVEncoder::add_nvdb_frame(nanovdb::Vec3fGrid* grid, nanovdb::CoordBBox boundingBox, bool removeNonvisible)
{
	if(!m_valid)
		throw std::invalid_argument("encoder is not valid (invalid params or finished encoding)");

	std::unique_ptr<Frame> frame = create_frame(grid, boundingBox);

	if(removeNonvisible)
		frame = remove_nonvisible_voxels(std::move(frame));
	
	encode_frame(std::move(frame));

	m_frameCount++;
}

void SPLVEncoder::finish()
{
	//write frame table:
	//---------------
	uint64_t frameTablePtr = m_outFile.tellp();
	m_outFile.write((const char*)m_framePtrs.data(), m_frameCount * sizeof(uint64_t));

	//write header:
	//---------------
	SPLVHeader header = {};
	header.width = m_xSize;
	header.height = m_ySize;
	header.depth = m_zSize;
	header.framerate = m_framerate;
	header.frameCount = m_frameCount;
	header.duration = (float)m_frameCount / m_framerate;
	header.frameTablePtr = frameTablePtr;

	m_outFile.seekp(std::ios::beg);
	m_outFile.write((const char*)&header, sizeof(SPLVHeader));
	m_outFile.close();

	m_valid = false;
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

	uint32_t sizes[3] = {xSize, ySize, zSize};
	xSize = sizes[(uint32_t)m_lrAxis];
	ySize = sizes[(uint32_t)m_udAxis];
	zSize = sizes[(uint32_t)m_fbAxis];

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

	//generate frame:
	//---------------
	auto accessor = grid->getAccessor();

	for(uint32_t mapZ = 0; mapZ < mapZSize; mapZ++)
	for(uint32_t mapY = 0; mapY < mapYSize; mapY++)
	for(uint32_t mapX = 0; mapX < mapXSize; mapX++)
	{
		Brick brick;
		bool brickCreated = false;

		for(uint32_t z = 0; z < BRICK_SIZE; z++)
		for(uint32_t y = 0; y < BRICK_SIZE; y++)
		for(uint32_t x = 0; x < BRICK_SIZE; x++)
		{
			int32_t readCoord[3];
			readCoord[(uint32_t)m_lrAxis] = mapX * BRICK_SIZE + x + minX;
			readCoord[(uint32_t)m_udAxis] = mapY * BRICK_SIZE + y + minY;
			readCoord[(uint32_t)m_fbAxis] = mapZ * BRICK_SIZE + z + minZ;

			nanovdb::Coord readCoordNVDB(
				readCoord[0], 
				readCoord[1], 
				readCoord[2]
			);

			if(!accessor.isActive(readCoordNVDB))
				continue;

			brickCreated = true;
			
			nanovdb::Vec3f normColor = accessor.getValue(readCoordNVDB);
			Color color((uint8_t)std::roundf(normColor[0] * 255.0f), (uint8_t)std::roundf(normColor[1] * 255.0f), (uint8_t)std::roundf(normColor[2] * 255.0f));
			brick.set_voxel(x, y, z, color);
		}

		uint32_t mapIdx = mapX + mapXSize * (mapY + mapYSize * mapZ);
		
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

std::unique_ptr<SPLVEncoder::Frame> SPLVEncoder::remove_nonvisible_voxels(std::unique_ptr<Frame> frame)
{
	//TODO: this function is a mess, refactor
	//TODO: write some legit tests for this

	//create data:
	//---------------
	uint32_t mapXSize = m_xSize / BRICK_SIZE;
	uint32_t mapYSize = m_ySize / BRICK_SIZE;
	uint32_t mapZSize = m_zSize / BRICK_SIZE;

	uint32_t mapLen = mapXSize * mapYSize * mapZSize;
	mapLen = (mapLen + 31) & (~31); //round up to multiple of 32 (sizeof(uint32_t))
	mapLen /= 4; //4 bytes per uint32_t
	mapLen /= 8; //8 bits per byte

	uint32_t brickLen = BRICK_SIZE * BRICK_SIZE * BRICK_SIZE;
	brickLen = (brickLen + 31) & (~31); //round up to multiple of 32 (sizeof(uint32_t))
	brickLen /= 4; //4 bytes per uint32_t
	brickLen /= 8; //8 bits per byte

	std::queue<Coordinate> bricksToVisit;
	std::queue<Coordinate> voxelsToVisit;

	std::unique_ptr<uint32_t[]> mapVisited = std::unique_ptr<uint32_t[]>(new uint32_t[mapLen]());
	std::vector<std::unique_ptr<uint32_t[]>> bricksVisited;
	for(uint32_t i = 0; i < frame->bricks.size(); i++)
		bricksVisited.push_back(std::unique_ptr<uint32_t[]>(new uint32_t[brickLen]()));

	//declare helper funcs:
	//---------------
	enum class Direction
	{
		POS_X = (1 << 0),
		NEG_X = (1 << 1),
		POS_Y = (1 << 2),
		NEG_Y = (1 << 3),
		POS_Z = (1 << 4),
		NEG_Z = (1 << 5)
	};

	auto maybe_visit_voxel = [&](uint32_t x, uint32_t y, uint32_t z) -> void {
		
		//get brick number from map:
		uint32_t mapX = x / BRICK_SIZE;
		uint32_t mapY = y / BRICK_SIZE;
		uint32_t mapZ = z / BRICK_SIZE;
		uint32_t mapIdx = mapX + mapXSize * (mapY + mapYSize * mapZ);

		uint32_t brickNum = frame->map[mapIdx];

		//if brick is empty and unvisited, visit that brick
		if(brickNum == EMPTY_BRICK)
		{
			if((mapVisited[mapIdx / 32] & (1 << (mapIdx % 32))) != 0)
				return;

			bricksToVisit.push({mapX, mapY, mapZ});
			mapVisited[mapIdx / 32] |= 1 << (mapIdx % 32);

			return;
		}

		//otherwise, visit voxel if unvisited, add to queue if empty
		const Brick& brick = frame->bricks[brickNum];

		uint32_t brickX = x % BRICK_SIZE;
		uint32_t brickY = y % BRICK_SIZE;
		uint32_t brickZ = z % BRICK_SIZE;
		uint32_t brickIdx = brickX + BRICK_SIZE * (brickY + BRICK_SIZE * brickZ);
		
		if((bricksVisited[brickNum][brickIdx / 32] & (1 << (brickIdx % 32))) != 0)
			return;

		bricksVisited[brickNum][brickIdx / 32] |= 1 << (brickIdx % 32);
	
		if(!brick.voxel_set(brickIdx))
			return;

		voxelsToVisit.push({x, y, z});
	};

	auto maybe_visit_brick_edges = [&](uint32_t mapX, uint32_t mapY, uint32_t mapZ, Direction from) -> void {
		
		//loop over plane of BRICK_SIZE^2 and maybe visit all voxels
		uint32_t brickX = mapX * BRICK_SIZE;
		uint32_t brickY = mapY * BRICK_SIZE;
		uint32_t brickZ = mapZ * BRICK_SIZE;
		
		for(uint32_t i = 0; i < BRICK_SIZE; i++)
		for(uint32_t j = 0; j < BRICK_SIZE; j++)
		{
			switch(from)
			{
			case Direction::POS_X:
				maybe_visit_voxel(brickX + BRICK_SIZE - 1, brickY + i, brickZ + j);
				break;
			case Direction::NEG_X:
				maybe_visit_voxel(brickX, brickY + i, brickZ + j);
				break;
			case Direction::POS_Y:
				maybe_visit_voxel(brickX + i, brickY + BRICK_SIZE - 1, brickZ + j);
				break;
			case Direction::NEG_Y:
				maybe_visit_voxel(brickX + i, brickY, brickZ + j);
				break;
			case Direction::POS_Z:
				maybe_visit_voxel(brickX + i, brickY + j, brickZ + BRICK_SIZE - 1);
				break;
			case Direction::NEG_Z:
				maybe_visit_voxel(brickX + i, brickY + j, brickZ);
				break;
			}
		}
	};

	auto maybe_visit_brick = [&](uint32_t x, uint32_t y, uint32_t z, Direction from) -> void {
		
		//if brick is empty, visit it. otherwise, visit its edge voxels:
		uint32_t idx = x + mapXSize * (y + mapYSize * z);
		
		if(frame->map[idx] == EMPTY_BRICK)
		{
			if((mapVisited[idx / 32] & (1 << (idx % 32))) != 0)
				return;
				
			bricksToVisit.push({x, y, z});
			mapVisited[idx / 32] |= 1 << (idx % 32);
		}
		else
			maybe_visit_brick_edges(x, y, z, from);
	};

	//add initial bricks/voxels:
	//---------------
	for(uint32_t z = 0; z < mapYSize; z++)
	for(uint32_t y = 0; y < mapYSize; y++)
	{
		uint32_t idxPosX = (mapXSize - 1) + mapXSize * (y + mapYSize * z);
		uint32_t idxNegX = 0 + mapXSize * (y + mapYSize * z);

		if(frame->map[idxPosX] == EMPTY_BRICK)
			bricksToVisit.push({mapXSize - 1, y, z});
		else
			maybe_visit_brick_edges(mapXSize - 1, y, z, Direction::POS_X);

		if(frame->map[idxNegX] == EMPTY_BRICK)
			bricksToVisit.push({0, y, z});
		else
			maybe_visit_brick_edges(0, y, z, Direction::NEG_X);
	}

	for(uint32_t z = 0; z < mapZSize; z++)
	for(uint32_t x = 0; x < mapXSize; x++)
	{
		uint32_t idxPosY = x + mapXSize * ((mapYSize - 1) + mapYSize * z);
		uint32_t idxNegY = x + mapXSize * (0 + mapYSize * z);

		if(frame->map[idxPosY] == EMPTY_BRICK)
			bricksToVisit.push({x, mapYSize - 1, z});
		else
			maybe_visit_brick_edges(x, mapYSize - 1, z, Direction::POS_Y);

		if(frame->map[idxNegY] == EMPTY_BRICK)
			bricksToVisit.push({x, 0, z});
		else
			maybe_visit_brick_edges(x, 0, z, Direction::NEG_Y);
	}

	for(uint32_t y = 0; y < mapYSize; y++)
	for(uint32_t x = 0; x < mapXSize; x++)
	{
		uint32_t idxPosZ = x + mapXSize * (y + mapYSize * (mapZSize - 1));
		uint32_t idxNegZ = x + mapXSize * (y + mapYSize * 0);

		if(frame->map[idxPosZ] == EMPTY_BRICK)
			bricksToVisit.push({x, y, mapZSize - 1});
		else
			maybe_visit_brick_edges(x, y, mapZSize - 1, Direction::POS_Z);

		if(frame->map[idxNegZ] == EMPTY_BRICK)
			bricksToVisit.push({x, y, 0});
		else
			maybe_visit_brick_edges(x, y, 0, Direction::NEG_Z);
	}

	//flood fill:
	//---------------
	while(!bricksToVisit.empty() || !voxelsToVisit.empty())
	{
		while(!bricksToVisit.empty())
		{
			Coordinate curBrick = bricksToVisit.front();
			bricksToVisit.pop();

			if(curBrick.x < mapXSize - 1)
				maybe_visit_brick(curBrick.x + 1, curBrick.y, curBrick.z, Direction::NEG_X);
			if(curBrick.x > 0)
				maybe_visit_brick(curBrick.x - 1, curBrick.y, curBrick.z, Direction::POS_X);

			if(curBrick.y < mapYSize - 1)
				maybe_visit_brick(curBrick.x, curBrick.y + 1, curBrick.z, Direction::NEG_Y);
			if(curBrick.y > 0)
				maybe_visit_brick(curBrick.x, curBrick.y - 1, curBrick.z, Direction::POS_Y);

			if(curBrick.z < mapZSize - 1)
				maybe_visit_brick(curBrick.x, curBrick.y, curBrick.z + 1, Direction::NEG_Z);
			if(curBrick.z > 0)
				maybe_visit_brick(curBrick.x, curBrick.y, curBrick.z - 1, Direction::POS_Z);
		}

		while(!voxelsToVisit.empty())
		{
			Coordinate curVoxel = voxelsToVisit.front();
			voxelsToVisit.pop();

			if(curVoxel.x < m_xSize - 1)
				maybe_visit_voxel(curVoxel.x + 1, curVoxel.y, curVoxel.z);
			if(curVoxel.x > 0)
				maybe_visit_voxel(curVoxel.x - 1, curVoxel.y, curVoxel.z);

			if(curVoxel.y < m_ySize - 1)
				maybe_visit_voxel(curVoxel.x, curVoxel.y + 1, curVoxel.z);
			if(curVoxel.y > 0)
				maybe_visit_voxel(curVoxel.x, curVoxel.y - 1, curVoxel.z);

			if(curVoxel.z < m_zSize - 1)
				maybe_visit_voxel(curVoxel.x, curVoxel.y, curVoxel.z + 1);
			if(curVoxel.z > 0)
				maybe_visit_voxel(curVoxel.x, curVoxel.y, curVoxel.z - 1);
		}
	}

	//loop over all bricks and unset nonvisible voxels:
	//---------------

	//TODO: remove bricks that end up with no set voxels

	for(uint32_t i = 0; i < frame->bricks.size(); i++)
	{
		Brick& brick = frame->bricks[i];

		for(uint32_t z = 0; z < BRICK_SIZE; z++)
		for(uint32_t y = 0; y < BRICK_SIZE; y++)
		for(uint32_t x = 0; x < BRICK_SIZE; x++)
		{
			uint32_t idx = x + BRICK_SIZE * (y + BRICK_SIZE * z);
			if((bricksVisited[i][idx / 32] & (1 << (idx % 32))) == 0)
				brick.unset_voxel(x, y, z);
		}
	}

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

	//we are writing bricks in xyz order, we MUST make sure to read them back in the same order
	for(uint32_t mapX = 0; mapX < mapXSize; mapX++)
	for(uint32_t mapY = 0; mapY < mapYSize; mapY++)
	for(uint32_t mapZ = 0; mapZ < mapZSize; mapZ++)
	{
		uint32_t mapIdx = mapX + mapXSize * (mapY + mapYSize * mapZ);
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

	//seralize frame:
	//---------------
	uint32_t numBricks = (uint32_t)bricksOrdered.size();

	std::vector<uint8_t> frameBuf;
	Uint8VectorOStream frameStream(frameBuf);

	frameStream.write((const char*)&numBricks, sizeof(uint32_t));
	frameStream.write((const char*)mapBitmap.get(), mapLenBitmap * sizeof(uint32_t));
	for(uint32_t i = 0; i < bricksOrdered.size(); i++)
		bricksOrdered[i].get().serialize(frameStream);

	//compress serialized frame:
	//---------------
	uint64_t framePtr = m_outFile.tellp();
	m_framePtrs.push_back(framePtr);

	Uint8VectorIStream frameStreamDecompressed(frameBuf);
	if(qc_compress(frameStreamDecompressed, m_outFile) != QC_SUCCESS)
		throw std::runtime_error("error while compressing frame!");
}