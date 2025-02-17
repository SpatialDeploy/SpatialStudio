#include "spatialstudio/splv_frame.h"

#include "spatialstudio/splv_log.h"
#include "spatialstudio/splv_global.h"

//-------------------------------------------//

inline uint8_t _splv_frame_get_voxel(SPLVframe* frame, int32_t x, int32_t y, int32_t z);

//-------------------------------------------//

SPLVerror splv_frame_create(SPLVframe* frame, uint32_t width, uint32_t height, uint32_t depth, uint32_t numBricksInitial)
{
	//validate params:
	//---------------
	SPLV_ASSERT(width > 0 && height > 0 && depth > 0, 
		"frame dimensions must be positive");

	//initialize:
	//---------------
	memset(frame, 0, sizeof(SPLVframe)); //clear any ptrs to NULL

	frame->width  = width;
	frame->height = height;
	frame->depth  = depth;

	//allocate map + bricks:
	//---------------
	frame->map = (uint32_t*)SPLV_MALLOC(width * height * depth * sizeof(uint32_t));
	if(!frame->map)
	{
		splv_frame_destroy(frame);

		SPLV_LOG_ERROR("failed to allocate frame map");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	if(numBricksInitial == 0)
	{
		const uint32_t DEFAULT_BRICK_CAP_INITIAL = 16;
		frame->bricksCap = DEFAULT_BRICK_CAP_INITIAL;
	}
	else
		frame->bricksCap = numBricksInitial + 1;

	frame->bricks = (SPLVbrick*)SPLV_MALLOC(frame->bricksCap * sizeof(SPLVbrick));
	if(!frame->bricks)
	{
		splv_frame_destroy(frame);

		SPLV_LOG_ERROR("failed tok allocate frame brick array");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	frame->bricksLen = numBricksInitial;

	return SPLV_SUCCESS;
}

void splv_frame_destroy(SPLVframe* frame)
{
	if(frame->map)
		SPLV_FREE(frame->map);
	if(frame->bricks)
		SPLV_FREE(frame->bricks);
}

inline uint32_t splv_frame_get_map_idx(SPLVframe* frame, uint32_t x, uint32_t y, uint32_t z)
{
	return x + frame->width * (y + frame->height * z);
}

SPLVbrick* splv_frame_get_next_brick(SPLVframe* frame)
{
	return &frame->bricks[frame->bricksLen];
}

SPLVerror splv_frame_push_next_brick(SPLVframe* frame, uint32_t x, uint32_t y, uint32_t z)
{
	SPLV_ASSERT(x < frame->width && y < frame->height && z < frame->depth, "map coordinates out of bounds");

	uint32_t idx = x + frame->width * (y + frame->height * z);
	frame->map[idx] = frame->bricksLen;

	frame->bricksLen++;
	if(frame->bricksLen >= frame->bricksCap)
	{
		uint32_t newCap = frame->bricksCap * 2;

		SPLVbrick* newBricks = (SPLVbrick*)SPLV_REALLOC(frame->bricks, newCap * sizeof(SPLVbrick));
		if(!newBricks)
		{
			SPLV_LOG_ERROR("failed to reallocate frame brick array");
			return SPLV_ERROR_OUT_OF_MEMORY;
		}

		frame->bricksCap = newCap;
		frame->bricks = newBricks;
	}

	return SPLV_SUCCESS;
}

SPLVerror splv_frame_remove_nonvisible_voxels(SPLVframe* frame, SPLVframe* processedFrame)
{
	//NOTE: this function considers a voxel nonvisible if all 6 of its neighbors
	//are filled. However, there can exist nonvisible voxels not satisfying this
	//(e.g. voxels inside a hollow sphere). We may want to change this back to a flood
	//fill if these situations are common

	//create new frame:
	//---------------
	SPLV_ERROR_PROPAGATE(splv_frame_create(processedFrame, frame->width, frame->height, frame->depth, 0));

	//add visible voxels to new frame:
	//---------------
	for(uint32_t zMap = 0; zMap < frame->depth ; zMap++)
	for(uint32_t yMap = 0; yMap < frame->height; yMap++)
	for(uint32_t xMap = 0; xMap < frame->width ; xMap++)
	{
		uint32_t mapIdx = splv_frame_get_map_idx(frame, xMap, yMap, zMap);

		if(frame->map[mapIdx] == SPLV_BRICK_IDX_EMPTY)
		{
			processedFrame->map[mapIdx] = SPLV_BRICK_IDX_EMPTY;
			continue;
		}
		
		SPLVbrick* brick = &frame->bricks[frame->map[mapIdx]];
		
		uint8_t newBrickEmpty = 1;
		SPLVbrick* newBrick = splv_frame_get_next_brick(processedFrame);

		for(uint32_t zBrick = 0; zBrick < SPLV_BRICK_SIZE; zBrick++)
		for(uint32_t yBrick = 0; yBrick < SPLV_BRICK_SIZE; yBrick++)
		for(uint32_t xBrick = 0; xBrick < SPLV_BRICK_SIZE; xBrick++)
		{
			uint8_t r, g, b;
			if(!splv_brick_get_voxel_color(brick, xBrick, yBrick, zBrick, &r, &g, &b))
			{
				splv_brick_set_voxel_empty(newBrick, xBrick, yBrick, zBrick);
				continue;
			}

			int32_t x = xMap * SPLV_BRICK_SIZE + xBrick;
			int32_t y = yMap * SPLV_BRICK_SIZE + yBrick;
			int32_t z = zMap * SPLV_BRICK_SIZE + zBrick;

			uint8_t visible = 0;
			visible = visible || (_splv_frame_get_voxel(frame, x - 1, y, z) == 0);
			visible = visible || (_splv_frame_get_voxel(frame, x + 1, y, z) == 0);
			visible = visible || (_splv_frame_get_voxel(frame, x, y - 1, z) == 0);
			visible = visible || (_splv_frame_get_voxel(frame, x, y + 1, z) == 0);
			visible = visible || (_splv_frame_get_voxel(frame, x, y, z - 1) == 0);
			visible = visible || (_splv_frame_get_voxel(frame, x, y, z + 1) == 0);

			if(visible)
			{
				splv_brick_set_voxel_filled(newBrick, xBrick, yBrick, zBrick, r, g, b);
				newBrickEmpty = 0;
			}
			else
				splv_brick_set_voxel_empty(newBrick, xBrick, yBrick, zBrick);
		}

		if(newBrickEmpty)
			processedFrame->map[mapIdx] = SPLV_BRICK_IDX_EMPTY;
		else
		{
			SPLVerror pushError = splv_frame_push_next_brick(processedFrame, xMap, yMap, zMap);
			if(pushError != SPLV_SUCCESS)
			{
				splv_frame_destroy(processedFrame);
				return pushError;
			}
		}
	}

	return SPLV_SUCCESS;
}

uint64_t splv_frame_get_size(SPLVframe* frame)
{
	uint64_t mapSize = frame->width * frame->height * frame->depth * sizeof(uint64_t);
	uint64_t bricksSize = frame->bricksLen * sizeof(SPLVbrick);

	return mapSize + bricksSize;
}

uint64_t splv_frame_get_num_voxels(SPLVframe* frame)
{
	uint64_t numVoxels = 0;

	for(uint32_t z = 0; z < frame->depth ; z++)
	for(uint32_t y = 0; y < frame->height; y++)
	for(uint32_t x = 0; x < frame->width ; x++)
	{
		uint32_t mapIdx = splv_frame_get_map_idx(frame, x, y, z);
		uint32_t brickIdx = frame->map[mapIdx];

		if(brickIdx != SPLV_BRICK_IDX_EMPTY)
			numVoxels += splv_brick_get_num_voxels(&frame->bricks[brickIdx]);
	}

	return numVoxels;
}

//-------------------------------------------//

inline uint8_t _splv_frame_get_voxel(SPLVframe* frame, int32_t x, int32_t y, int32_t z)
{
	if(x < 0 || x >= (int32_t)frame->width  * SPLV_BRICK_SIZE ||
	   y < 0 || y >= (int32_t)frame->height * SPLV_BRICK_SIZE ||
	   z < 0 || z >= (int32_t)frame->depth  * SPLV_BRICK_SIZE)
		return 0;
	
	uint32_t xMap = x / SPLV_BRICK_SIZE;
	uint32_t yMap = y / SPLV_BRICK_SIZE;
	uint32_t zMap = z / SPLV_BRICK_SIZE;
	uint32_t mapIdx = splv_frame_get_map_idx(frame, xMap, yMap, zMap);

	if(frame->map[mapIdx] == SPLV_BRICK_IDX_EMPTY)
		return 0;

	SPLVbrick* brick = &frame->bricks[frame->map[mapIdx]];
	uint32_t xBrick = x % SPLV_BRICK_SIZE;
	uint32_t yBrick = y % SPLV_BRICK_SIZE;
	uint32_t zBrick = z % SPLV_BRICK_SIZE;

	return splv_brick_get_voxel(brick, xBrick, yBrick, zBrick) != 0;
}