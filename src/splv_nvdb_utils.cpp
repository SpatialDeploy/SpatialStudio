#include "splv_nvdb_utils.h"

#define NANOVDB_USE_BLOSC
#include "nanovdb/util/IO.h"
#include "splv_log.h"

//-------------------------------------------//

SPLVerror splv_nvdb_load(const char* path, SPLVframe** outFrame, SPLVboundingBox* bbox, SPLVaxis lrAxis, SPLVaxis udAxis, SPLVaxis fbAxis)
{
	//get size of region + validate:
	//---------------
	uint32_t xSize = bbox->xMax - bbox->xMin + 1;
	uint32_t ySize = bbox->yMax - bbox->yMin + 1;
	uint32_t zSize = bbox->zMax - bbox->zMin + 1;

	if(xSize % SPLV_BRICK_SIZE != 0 || ySize % SPLV_BRICK_SIZE != 0 || zSize % SPLV_BRICK_SIZE != 0)
	{
		SPLV_LOG_ERROR("frame dimensions must be a multiple of SPLV_BRICK_SIZE");
		return SPLV_ERROR_INVALID_ARGUMENTS;
	}

	if(lrAxis == udAxis || lrAxis == fbAxis || udAxis == fbAxis)
	{
		SPLV_LOG_ERROR("axes must be distinct");
		return SPLV_ERROR_INVALID_ARGUMENTS;
	}

	uint32_t sizes[3] = {xSize, ySize, zSize};
	uint32_t width  = sizes[(uint32_t)lrAxis];
	uint32_t height = sizes[(uint32_t)udAxis];
	uint32_t depth  = sizes[(uint32_t)fbAxis];

	//open file:
	//---------------
	nanovdb::GridHandle file;
	nanovdb::Vec3fGrid* grid;

	try
	{
		file = nanovdb::io::readGrid(path);
		grid = file.grid<nanovdb::Vec3f>();
		if(!grid)
		{
			SPLV_LOG_ERROR("nvdb file did not contain a vec3f grid");
			return SPLV_ERROR_INVALID_INPUT;
		}
	}
	catch(std::exception e)
	{
		SPLV_LOG_ERROR("failed to open nvdb file");
		return SPLV_ERROR_FILE_OPEN;
	}

	//initialize map and bricks vector:
	//---------------
	uint32_t widthMap  = width  / SPLV_BRICK_SIZE;
	uint32_t heightMap = height / SPLV_BRICK_SIZE;
	uint32_t depthMap  = depth  / SPLV_BRICK_SIZE;

	SPLV_ERROR_PROPAGATE(splv_frame_create(outFrame, widthMap, heightMap, depthMap));

	//generate frame:
	//---------------
	auto accessor = grid->getAccessor();

	for(uint32_t zMap = 0; zMap < depthMap ; zMap++)
	for(uint32_t yMap = 0; yMap < heightMap; yMap++)
	for(uint32_t xMap = 0; xMap < widthMap ; xMap++)
	{
		SPLVbrick* brick = splv_frame_get_next_brick(*outFrame);
		bool brickCreated = false;

		for(uint32_t zBrick = 0; zBrick < SPLV_BRICK_SIZE; zBrick++)
		for(uint32_t yBrick = 0; yBrick < SPLV_BRICK_SIZE; yBrick++)
		for(uint32_t xBrick = 0; xBrick < SPLV_BRICK_SIZE; xBrick++)
		{
			int32_t readCoord[3];
			readCoord[(uint32_t)lrAxis] = xMap * SPLV_BRICK_SIZE + xBrick + bbox->xMin;
			readCoord[(uint32_t)udAxis] = yMap * SPLV_BRICK_SIZE + yBrick + bbox->yMin;
			readCoord[(uint32_t)fbAxis] = zMap * SPLV_BRICK_SIZE + zBrick + bbox->zMin;

			nanovdb::Coord readCoordNVDB(
				readCoord[0], 
				readCoord[1], 
				readCoord[2]
			);

			if(!accessor.isActive(readCoordNVDB))
			{
				splv_brick_set_voxel_empty(brick, xBrick, yBrick, zBrick);
				continue;
			}

			brickCreated = true;
			
			nanovdb::Vec3f normColor = accessor.getValue(readCoordNVDB);
			uint8_t r = (uint8_t)roundf(normColor[0] * 255.0f);
			uint8_t g = (uint8_t)roundf(normColor[1] * 255.0f);
			uint8_t b = (uint8_t)roundf(normColor[2] * 255.0f);

			splv_brick_set_voxel_filled(brick, xBrick, yBrick, zBrick, r, g, b);
		}

		if(brickCreated)
		{
			SPLVerror pushError = splv_frame_push_next_brick(*outFrame, xMap, yMap, zMap);
			if(pushError != SPLV_SUCCESS)
				return pushError;
		}
		else
		{
			uint32_t mapIdx = splv_frame_get_map_idx(*outFrame, xMap, yMap, zMap);
			(*outFrame)->map[mapIdx] = SPLV_BRICK_IDX_EMPTY;
		}
	}

	//return:
	//---------------
	return SPLV_SUCCESS;
}