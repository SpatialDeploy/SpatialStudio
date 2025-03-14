#include "spatialstudio/splv_nvdb_utils.h"

#define NANOVDB_USE_BLOSC
#include "nanovdb/util/IO.h"
#include "nanovdb/util/GridBuilder.h"
#include "spatialstudio/splv_log.h"

//-------------------------------------------//

SPLVerror splv_nvdb_load(const char* path, SPLVframe* outFrame, SPLVboundingBox* bbox, SPLVaxis lrAxis, SPLVaxis udAxis, SPLVaxis fbAxis)
{
	//get size of region:
	//---------------
	uint32_t xSize = bbox->xMax - bbox->xMin + 1;
	uint32_t ySize = bbox->yMax - bbox->yMin + 1;
	uint32_t zSize = bbox->zMax - bbox->zMin + 1;

	uint32_t sizes[3] = {xSize, ySize, zSize};
	uint32_t width  = sizes[(uint32_t)lrAxis];
	uint32_t height = sizes[(uint32_t)udAxis];
	uint32_t depth  = sizes[(uint32_t)fbAxis];

	//validate:
	//---------------
	SPLV_ASSERT(xSize % SPLV_BRICK_SIZE == 0 && ySize % SPLV_BRICK_SIZE == 0 && zSize % SPLV_BRICK_SIZE == 0,
		"frame dimensions must be a multiple of SPLV_BRICK_SIZE");
	SPLV_ASSERT(lrAxis != udAxis && lrAxis != fbAxis && udAxis != fbAxis, "axes must be distinct");

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

	SPLV_ERROR_PROPAGATE(splv_frame_create(outFrame, widthMap, heightMap, depthMap, 0));

	//generate frame:
	//---------------
	auto accessor = grid->getAccessor();

	for(uint32_t zMap = 0; zMap < depthMap ; zMap++)
	for(uint32_t yMap = 0; yMap < heightMap; yMap++)
	for(uint32_t xMap = 0; xMap < widthMap ; xMap++)
	{
		SPLVbrick* brick = splv_frame_get_next_brick(outFrame);
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
			SPLVerror pushError = splv_frame_push_next_brick(outFrame, xMap, yMap, zMap);
			if(pushError != SPLV_SUCCESS)
			{
				splv_frame_destroy(outFrame);
				return pushError;
			}
		}
		else
		{
			uint32_t mapIdx = splv_frame_get_map_idx(outFrame, xMap, yMap, zMap);
			outFrame->map[mapIdx] = SPLV_BRICK_IDX_EMPTY;
		}
	}

	//return:
	//---------------
	return SPLV_SUCCESS;
}

SPLVerror splv_nvdb_save(SPLVframe* frame, const char* outPath)
{
	//create grid:
	//---------------
	nanovdb::GridBuilder<nanovdb::Vec3f> builder;
	
	//write voxels:
	//---------------
	auto accessor = builder.getAccessor();

	for(uint32_t zMap = 0; zMap < frame->depth ; zMap++)
	for(uint32_t yMap = 0; yMap < frame->height; yMap++)
	for(uint32_t xMap = 0; xMap < frame->width ; xMap++)
	{
		uint32_t mapIdx = splv_frame_get_map_idx(frame, xMap, yMap, zMap);
		uint32_t brickIdx = frame->map[mapIdx];
		
		if(brickIdx == SPLV_BRICK_IDX_EMPTY)
			continue;
		
		SPLVbrick* brick = &frame->bricks[brickIdx];
		for(uint32_t zBrick = 0; zBrick < SPLV_BRICK_SIZE; zBrick++)
		for(uint32_t yBrick = 0; yBrick < SPLV_BRICK_SIZE; yBrick++)
		for(uint32_t xBrick = 0; xBrick < SPLV_BRICK_SIZE; xBrick++)
		{
			if(!splv_brick_get_voxel(brick, xBrick, yBrick, zBrick))
				continue;
			
			uint8_t r, g, b;
			splv_brick_get_voxel_color(brick, xBrick, yBrick, zBrick, &r, &g, &b);
			
			float normR = r / 255.0f;
			float normG = g / 255.0f;
			float normB = b / 255.0f;
			
			int32_t x = xMap * SPLV_BRICK_SIZE + xBrick;
			int32_t y = yMap * SPLV_BRICK_SIZE + yBrick;
			int32_t z = zMap * SPLV_BRICK_SIZE + zBrick;
			accessor.setValue(nanovdb::Coord(x, y, z), nanovdb::Vec3f(normR, normG, normB));
		}
	}
	
	//create the grid:
	//---------------
	nanovdb::GridHandle handle = builder.getHandle(1.0, nanovdb::Vec3d(0.0), "SPLVvolume");
	
	//write to file:
	//---------------
	try 
	{
		nanovdb::io::writeGrid(outPath, handle, nanovdb::io::Codec::BLOSC);
	}
	catch(std::exception&)
	{
		SPLV_LOG_ERROR("failed to write nvdb file");
		return SPLV_ERROR_FILE_WRITE;
	}
	
	return SPLV_SUCCESS;
}