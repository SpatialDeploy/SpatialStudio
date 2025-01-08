#include "splv_encoder.hpp"

#include <vector>
#include <queue>
#include <iostream>
#include <unordered_map>
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

#define VOX_CHUNK_ID(a, b, c, d) ((a) | ((b) << 8) | ((c) << 16) | ((d) << 24))

struct VoxChunk
{
	uint32_t id;
	uint32_t len;
	uint32_t childLen;
	uint32_t endPtr;
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

	std::shared_ptr<Frame> frame = create_nvdb_frame(grid, boundingBox);
	if(removeNonvisible)
		frame = remove_nonvisible_voxels(frame);
	
	encode_frame(frame);
	m_frameCount++;
}

void SPLVEncoder::add_vox_frame(const std::string& path, bool removeNonvisible)
{
	if(!m_valid)
		throw std::invalid_argument("encoder is not valid (invalid params or finished encoding)");

	//open file + validate:
	//---------------
	std::ifstream file(path, std::ios::binary);
	if(!file.is_open())
		throw std::runtime_error("failed to open .vox file \"" + path + "\"");

	uint32_t id;
	file.read((char*)&id, sizeof(uint32_t));
	if(id != VOX_CHUNK_ID('V', 'O', 'X', ' '))
	{
		throw std::runtime_error("invalid .vox file");
		file.close();
	}

	//TODO: ensure version is supported
	uint32_t version;
	file.read((char*)&version, sizeof(uint32_t));

	//declare helper functions:
	//---------------
	auto read_chunk = [&]() -> VoxChunk {
		VoxChunk chunk;
		file.read((char*)&chunk, 3 * sizeof(uint32_t)); //read id, size, childSize
		chunk.endPtr = (uint32_t)file.tellg() + chunk.len + chunk.childLen;

		return chunk;
	};

	auto read_dict = [&]() -> std::unordered_map<std::string, std::string> {
		uint32_t numEntries;
		file.read((char*)&numEntries, sizeof(uint32_t));

		std::unordered_map<std::string, std::string> entries;
		for(uint32_t i = 0; i < numEntries; i++)
		{
			uint32_t strLenKey;
			file.read((char*)&strLenKey, sizeof(uint32_t));
			std::string key;
			key.resize(strLenKey);
			file.read(key.data(), strLenKey);

			uint32_t strLenVal;
			file.read((char*)&strLenVal, sizeof(uint32_t));
			std::string val;
			val.resize(strLenVal);
			file.read(val.data(), strLenVal);

			entries.insert({ key, val });
		}

		return entries;
	};

	//read + parse chunks:
	//---------------
	std::vector<uint32_t> sizePtrs;
	std::vector<uint32_t> xyziPtrs;
	uint32_t palette[256] = { //default palette
		0x00000000, 0xffffffff, 0xffccffff, 0xff99ffff, 0xff66ffff, 0xff33ffff, 0xff00ffff, 0xffffccff, 0xffccccff, 0xff99ccff, 0xff66ccff, 0xff33ccff, 0xff00ccff, 0xffff99ff, 0xffcc99ff, 0xff9999ff,
		0xff6699ff, 0xff3399ff, 0xff0099ff, 0xffff66ff, 0xffcc66ff, 0xff9966ff, 0xff6666ff, 0xff3366ff, 0xff0066ff, 0xffff33ff, 0xffcc33ff, 0xff9933ff, 0xff6633ff, 0xff3333ff, 0xff0033ff, 0xffff00ff,
		0xffcc00ff, 0xff9900ff, 0xff6600ff, 0xff3300ff, 0xff0000ff, 0xffffffcc, 0xffccffcc, 0xff99ffcc, 0xff66ffcc, 0xff33ffcc, 0xff00ffcc, 0xffffcccc, 0xffcccccc, 0xff99cccc, 0xff66cccc, 0xff33cccc,
		0xff00cccc, 0xffff99cc, 0xffcc99cc, 0xff9999cc, 0xff6699cc, 0xff3399cc, 0xff0099cc, 0xffff66cc, 0xffcc66cc, 0xff9966cc, 0xff6666cc, 0xff3366cc, 0xff0066cc, 0xffff33cc, 0xffcc33cc, 0xff9933cc,
		0xff6633cc, 0xff3333cc, 0xff0033cc, 0xffff00cc, 0xffcc00cc, 0xff9900cc, 0xff6600cc, 0xff3300cc, 0xff0000cc, 0xffffff99, 0xffccff99, 0xff99ff99, 0xff66ff99, 0xff33ff99, 0xff00ff99, 0xffffcc99,
		0xffcccc99, 0xff99cc99, 0xff66cc99, 0xff33cc99, 0xff00cc99, 0xffff9999, 0xffcc9999, 0xff999999, 0xff669999, 0xff339999, 0xff009999, 0xffff6699, 0xffcc6699, 0xff996699, 0xff666699, 0xff336699,
		0xff006699, 0xffff3399, 0xffcc3399, 0xff993399, 0xff663399, 0xff333399, 0xff003399, 0xffff0099, 0xffcc0099, 0xff990099, 0xff660099, 0xff330099, 0xff000099, 0xffffff66, 0xffccff66, 0xff99ff66,
		0xff66ff66, 0xff33ff66, 0xff00ff66, 0xffffcc66, 0xffcccc66, 0xff99cc66, 0xff66cc66, 0xff33cc66, 0xff00cc66, 0xffff9966, 0xffcc9966, 0xff999966, 0xff669966, 0xff339966, 0xff009966, 0xffff6666,
		0xffcc6666, 0xff996666, 0xff666666, 0xff336666, 0xff006666, 0xffff3366, 0xffcc3366, 0xff993366, 0xff663366, 0xff333366, 0xff003366, 0xffff0066, 0xffcc0066, 0xff990066, 0xff660066, 0xff330066,
		0xff000066, 0xffffff33, 0xffccff33, 0xff99ff33, 0xff66ff33, 0xff33ff33, 0xff00ff33, 0xffffcc33, 0xffcccc33, 0xff99cc33, 0xff66cc33, 0xff33cc33, 0xff00cc33, 0xffff9933, 0xffcc9933, 0xff999933,
		0xff669933, 0xff339933, 0xff009933, 0xffff6633, 0xffcc6633, 0xff996633, 0xff666633, 0xff336633, 0xff006633, 0xffff3333, 0xffcc3333, 0xff993333, 0xff663333, 0xff333333, 0xff003333, 0xffff0033,
		0xffcc0033, 0xff990033, 0xff660033, 0xff330033, 0xff000033, 0xffffff00, 0xffccff00, 0xff99ff00, 0xff66ff00, 0xff33ff00, 0xff00ff00, 0xffffcc00, 0xffcccc00, 0xff99cc00, 0xff66cc00, 0xff33cc00,
		0xff00cc00, 0xffff9900, 0xffcc9900, 0xff999900, 0xff669900, 0xff339900, 0xff009900, 0xffff6600, 0xffcc6600, 0xff996600, 0xff666600, 0xff336600, 0xff006600, 0xffff3300, 0xffcc3300, 0xff993300,
		0xff663300, 0xff333300, 0xff003300, 0xffff0000, 0xffcc0000, 0xff990000, 0xff660000, 0xff330000, 0xff0000ee, 0xff0000dd, 0xff0000bb, 0xff0000aa, 0xff000088, 0xff000077, 0xff000055, 0xff000044,
		0xff000022, 0xff000011, 0xff00ee00, 0xff00dd00, 0xff00bb00, 0xff00aa00, 0xff008800, 0xff007700, 0xff005500, 0xff004400, 0xff002200, 0xff001100, 0xffee0000, 0xffdd0000, 0xffbb0000, 0xffaa0000,
		0xff880000, 0xff770000, 0xff550000, 0xff440000, 0xff220000, 0xff110000, 0xffeeeeee, 0xffdddddd, 0xffbbbbbb, 0xffaaaaaa, 0xff888888, 0xff777777, 0xff555555, 0xff444444, 0xff222222, 0xff111111
	};

	bool foundShapeNode = false;
	std::vector<std::pair<uint32_t, uint32_t>> modelIndices;

	VoxChunk mainChunk = read_chunk();

	while(file.tellg() < mainChunk.endPtr)
	{
		if(!file.good())
		{
			file.close();
			throw std::runtime_error("error reading file");
		}

		VoxChunk chunk = read_chunk();

		switch(chunk.id)
		{
		case VOX_CHUNK_ID('S', 'I', 'Z', 'E'):
		{
			sizePtrs.push_back((uint32_t)file.tellg());
			break;
		}
		case VOX_CHUNK_ID('X', 'Y', 'Z', 'I'):
		{
			xyziPtrs.push_back((uint32_t)file.tellg());
			break;
		}
		case VOX_CHUNK_ID('R', 'G', 'B', 'A'):
		{
			file.read((char*)palette, 256 * sizeof(uint32_t));
			break;
		}
		case VOX_CHUNK_ID('n', 'S', 'H', 'P'):
		{
			//we only process the 1st shape node, we dont support multiple
			if(foundShapeNode)
			{
				file.close();
				throw std::runtime_error("multiple shape nodes found, SPLV only supports encoding a single model");
			}

			uint32_t nodeId;
			file.read((char*)&nodeId, sizeof(uint32_t));

			std::unordered_map<std::string, std::string> nodeAttribs = read_dict();

			uint32_t numModels;
			file.read((char*)&numModels, sizeof(uint32_t));

			for(uint32_t i = 0; i < numModels; i++)
			{
				uint32_t modelId;
				file.read((char*)&modelId, sizeof(uint32_t));

				std::unordered_map<std::string, std::string> modelAttribs = read_dict();
				if(modelAttribs.find("_f") == modelAttribs.end())
				{
					file.close();
					throw std::runtime_error("model attributes did not contain frame index");
				}

				uint32_t frameIdx;
				try
				{
					frameIdx = (uint32_t)std::stoi(modelAttribs["_f"]);
				}
				catch(std::exception e)
				{
					file.close();
					throw std::runtime_error("invalid model frame index");
				}

				modelIndices.push_back({ frameIdx, modelId });
			}

			foundShapeNode = true;
			break;
		}
		default:
			break;
		}

		file.seekg(chunk.endPtr, std::ios::beg);
	}

	//validate:
	//---------------
	if(sizePtrs.size() != xyziPtrs.size())
	{
		file.close();
		throw std::runtime_error("mismatched SIZE and XYZI chunks");
	}

	if(sizePtrs.size() == 0)
	{
		file.close();
		throw std::runtime_error("no voxel models found");
	}

	if(!foundShapeNode || modelIndices.size() == 0)
	{
		file.close();
		throw std::runtime_error("no shape node containing animation data found");
	}

	//add models:
	//---------------
	std::shared_ptr<Frame> frame;

	for(uint32_t i = 0; i < modelIndices.size(); i++)
	{
		uint32_t frameIdx = modelIndices[i].first;
		uint32_t modelIdx = modelIndices[i].second;

		if(i > 0)
		{
			uint32_t prevFrameIdx = modelIndices[i - 1].first;
			for(uint32_t j = 0; j < frameIdx - prevFrameIdx - 1; j++)
			{
				encode_frame(frame);
				m_frameCount++;
			}
		}

		frame = create_vox_frame(file, sizePtrs[modelIdx], xyziPtrs[modelIdx], palette);
		if(removeNonvisible)
			frame = remove_nonvisible_voxels(frame);
		
		encode_frame(frame);
		m_frameCount++;
	}
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

std::shared_ptr<SPLVEncoder::Frame> SPLVEncoder::create_nvdb_frame(nanovdb::Vec3fGrid* grid, nanovdb::CoordBBox boundingBox)
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

	std::shared_ptr<Frame> frame = std::make_shared<Frame>();
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

std::shared_ptr<SPLVEncoder::Frame> SPLVEncoder::create_vox_frame(std::ifstream& file, uint32_t sizePtr, uint32_t xyziPtr, uint32_t* palette)
{
	//parse size:
	//---------------
	file.seekg(sizePtr);

	uint32_t readWidth;
	uint32_t readHeight;
	uint32_t readDepth;

	file.read((char*)&readWidth, sizeof(uint32_t));
	file.read((char*)&readHeight, sizeof(uint32_t));
	file.read((char*)&readDepth, sizeof(uint32_t));

	if(readWidth > m_xSize || readHeight > m_ySize || readDepth > m_zSize)
		throw std::runtime_error("frame is too large");

	//we swap z and y axes, .vox files are z-up
	uint32_t xOffset = (m_xSize - readWidth ) / 2;
	uint32_t yOffset = (m_ySize - readDepth ) / 2;
	uint32_t zOffset = (m_zSize - readHeight) / 2;

	//create frame:
	//---------------
	uint32_t widthMap  = m_xSize / BRICK_SIZE;
	uint32_t heightMap = m_ySize / BRICK_SIZE;
	uint32_t depthMap  = m_zSize / BRICK_SIZE;
	uint32_t mapLen = widthMap * heightMap * depthMap;

	std::shared_ptr<Frame> frame = std::make_shared<Frame>();
	frame->map = std::unique_ptr<uint32_t[]>(new uint32_t[mapLen]);
	for(uint32_t i = 0; i < mapLen; i++)
		frame->map[i] = EMPTY_BRICK;

	//parse voxels:
	//---------------
	file.seekg(xyziPtr);

	uint32_t numVoxels = 0;
	file.read((char*)&numVoxels, sizeof(uint32_t));

	for(uint32_t i = 0; i < numVoxels; i++)
	{
		uint32_t xyzi;
		file.read((char*)&xyzi, sizeof(uint32_t));

		uint32_t x = (xyzi & 0xFF) + xOffset;
		uint32_t y = ((xyzi >> 16) & 0xFF) + yOffset;
		uint32_t z = ((xyzi >>  8) & 0xFF) + zOffset;

		uint32_t xMap = x / BRICK_SIZE;
		uint32_t yMap = y / BRICK_SIZE;
		uint32_t zMap = z / BRICK_SIZE;
		uint32_t idxMap = xMap + widthMap * (yMap + heightMap * zMap);

		if(frame->map[idxMap] == EMPTY_BRICK)
		{
			frame->map[idxMap] = (uint32_t)frame->bricks.size();
			frame->bricks.push_back(Brick());
		}

		Brick& brick = frame->bricks[frame->map[idxMap]];
		uint32_t xBrick = x % BRICK_SIZE;
		uint32_t yBrick = y % BRICK_SIZE;
		uint32_t zBrick = z % BRICK_SIZE;

		uint32_t color = palette[((xyzi >> 24) & 0xFF) - 1];

		brick.set_voxel(xBrick, yBrick, zBrick, Color(color & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF));
	}

	//return:
	//---------------
	return frame;
}

std::shared_ptr<SPLVEncoder::Frame> SPLVEncoder::remove_nonvisible_voxels(std::shared_ptr<Frame> frame)
{
	//NOTE: this function considers a voxel nonvisible if all 6 of its neighbors
	//are filled. However, there can exist nonvisible voxels not satisfying this
	//(e.g. voxels inside a hollow sphere). We may want to change this back to a flood
	//fill if these situations are common

	//define helper functions:
	//---------------
	uint32_t mapXSize = m_xSize / BRICK_SIZE;
	uint32_t mapYSize = m_ySize / BRICK_SIZE;
	uint32_t mapZSize = m_zSize / BRICK_SIZE;
	uint32_t mapLen = mapXSize * mapYSize * mapZSize;

	auto voxel_set = [&](int32_t x, int32_t y, int32_t z) -> bool {
		if(x < 0 || x >= (int32_t)m_xSize ||
		   y < 0 || y >= (int32_t)m_ySize ||
		   x < 0 || z >= (int32_t)m_zSize)
		   	return false;
		
		uint32_t xMap = x / BRICK_SIZE;
		uint32_t yMap = y / BRICK_SIZE;
		uint32_t zMap = z / BRICK_SIZE;
		uint32_t mapIdx = xMap + mapXSize * (yMap + mapYSize * zMap);

		if(frame->map[mapIdx] == EMPTY_BRICK)
			return false;

		Brick& brick = frame->bricks[frame->map[mapIdx]];
		uint32_t xBrick = x % BRICK_SIZE;
		uint32_t yBrick = y % BRICK_SIZE;
		uint32_t zBrick = z % BRICK_SIZE;

		return brick.get_voxel(xBrick, yBrick, zBrick);
	};

	//create new frame:
	//---------------
	std::shared_ptr<Frame> newFrame = std::make_shared<Frame>();
	newFrame->map = std::unique_ptr<uint32_t[]>(new uint32_t[mapLen]);

	//add visible voxels to new frame:
	//---------------
	for(uint32_t zMap = 0; zMap < mapZSize; zMap++)
	for(uint32_t yMap = 0; yMap < mapYSize; yMap++)
	for(uint32_t xMap = 0; xMap < mapXSize; xMap++)
	{
		uint32_t mapIdx = xMap + mapXSize * (yMap + mapYSize * zMap);

		if(frame->map[mapIdx] == EMPTY_BRICK)
		{
			newFrame->map[mapIdx] = EMPTY_BRICK;
			continue;
		}
		
		Brick& brick = frame->bricks[frame->map[mapIdx]];
		
		bool newBrickEmpty = true;
		Brick newBrick;

		for(uint32_t zBrick = 0; zBrick < BRICK_SIZE; zBrick++)
		for(uint32_t yBrick = 0; yBrick < BRICK_SIZE; yBrick++)
		for(uint32_t xBrick = 0; xBrick < BRICK_SIZE; xBrick++)
		{
			Color color;
			if(!brick.get_voxel(xBrick, yBrick, zBrick, color))
				continue;

			int32_t x = xMap * BRICK_SIZE + xBrick;
			int32_t y = yMap * BRICK_SIZE + yBrick;
			int32_t z = zMap * BRICK_SIZE + zBrick;

			bool visible = false;
			visible = visible || !voxel_set(x - 1, y, z);
			visible = visible || !voxel_set(x + 1, y, z);
			visible = visible || !voxel_set(x, y - 1, z);
			visible = visible || !voxel_set(x, y + 1, z);
			visible = visible || !voxel_set(x, y, z - 1);
			visible = visible || !voxel_set(x, y, z + 1);

			if(visible)
			{
				newBrick.set_voxel(xBrick, yBrick, zBrick, color);
				newBrickEmpty = false;
			}
		}

		if(newBrickEmpty)
			newFrame->map[mapIdx] = EMPTY_BRICK;
		else
		{
			newFrame->map[mapIdx] = (uint32_t)newFrame->bricks.size();
			newFrame->bricks.push_back(std::move(newBrick));
		}
	}

	return newFrame;
}

void SPLVEncoder::encode_frame(std::shared_ptr<Frame> frame)
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