#include "spatialstudio/splv_frame_compact.h"

#include "spatialstudio/splv_log.h"
#include "spatialstudio/splv_global.h"

//-------------------------------------------//

SPLVerror splv_frame_compact_create(SPLVframeCompact* frame, uint32_t width, uint32_t height, uint32_t depth, uint32_t numBricks, uint64_t numVoxels)
{
	//validate params:
	//---------------
	SPLV_ASSERT(width > 0 && height > 0 && depth > 0, 
		"frame dimensions must be positive");

	//initialize:
	//---------------
	memset(frame, 0, sizeof(SPLVframeCompact)); //clear any ptrs to NULL

	frame->width  = width;
	frame->height = height;
	frame->depth  = depth;

	//allocate map, bricks, voxels:
	//---------------
	frame->map = (uint32_t*)SPLV_MALLOC(width * height * depth * sizeof(uint32_t));
	if(!frame->map)
	{
		splv_frame_compact_destroy(frame);

		SPLV_LOG_ERROR("failed to allocate frame map");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	frame->numBricks = numBricks;
	frame->bricks = (SPLVbrickCompact*)SPLV_MALLOC(numBricks * sizeof(SPLVbrickCompact));
	if(!frame->bricks)
	{
		splv_frame_compact_destroy(frame);

		SPLV_LOG_ERROR("failed to allocate frame brick array");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	frame->numVoxels = numVoxels;
	frame->voxels = (uint32_t*)SPLV_MALLOC(numVoxels * sizeof(uint32_t));
	if(!frame->voxels)
	{
		splv_frame_compact_destroy(frame);

		SPLV_LOG_ERROR("failed to allocate frame voxel array");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	return SPLV_SUCCESS;
}

void splv_frame_compact_destroy(SPLVframeCompact* frame)
{
	if(frame->map)
		SPLV_FREE(frame->map);
	if(frame->bricks)
		SPLV_FREE(frame->bricks);
	if(frame->voxels)
		SPLV_FREE(frame->voxels);
}