/* splv_frame_compact.h
 *
 * contains the definition of a more compacted frame class,
 * ideal for GPU upload and rendering
 */

#ifndef SPLV_FRAME_COMPACT_H
#define SPLV_FRAME_COMPACT_H

#include "splv_error.h"
#include "splv_brick.h"
#include "splv_frame.h"

//-------------------------------------------//

/**
 * a BRICK_SIZE^3 bitmap of voxels, with a ptr to actual voxel data
 */
typedef struct SPLVbrickCompact
{
	uint32_t bitmap[SPLV_BRICK_LEN / 32];
	uint32_t voxelsOffset;
} SPLVbrickCompact;

/**
 * a single frame of a spatial, in a more compact format
 */
typedef struct SPLVframeCompact
{
	//in bricks, not voxels
	uint32_t width;
	uint32_t height;
	uint32_t depth;

	uint32_t* map;

	uint32_t numBricks;
	SPLVbrickCompact* bricks;

	uint64_t numVoxels;
	uint32_t* voxels;
} SPLVframeCompact;

//-------------------------------------------//

/**
 * creates a new compact frame. DOES NOT CLEAR THE MAP TO BE EMPTY. call splv_frame_compact_destroy() to free
 */
SPLV_API SPLVerror splv_frame_compact_create(SPLVframeCompact* frame, uint32_t width, uint32_t height, uint32_t depth, uint32_t numBricks, uint64_t numVoxels);

/**
 * frees all resources allocated from splv_frame_compact_create()
 */
SPLV_API void splv_frame_compact_destroy(SPLVframeCompact* frame);

#endif