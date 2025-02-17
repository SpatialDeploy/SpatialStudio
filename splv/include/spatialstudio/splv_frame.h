/* splv_frame.h
 *
 * contains the definition of the frame class which represents a
 * single spatial frame
 */

#ifndef SPLV_FRAME_H
#define SPLV_FRAME_H

#include "splv_error.h"
#include "splv_brick.h"

//-------------------------------------------//

#define SPLV_BRICK_IDX_EMPTY UINT32_MAX

//-------------------------------------------//

/**
 * a single frame of a spatial, represented as a grid of bricks
 */
typedef struct SPLVframe
{
	//in bricks, not voxels
	uint32_t width;
	uint32_t height;
	uint32_t depth;

	uint32_t* map;

	uint32_t bricksLen;
	uint32_t bricksCap;
	SPLVbrick* bricks;
} SPLVframe;

//-------------------------------------------//

/**
 * creates a new frame. DOES NOT CLEAR THE MAP TO BE EMPTY. call splv_frame_destroy() to free
 */
SPLV_API SPLVerror splv_frame_create(SPLVframe* frame, uint32_t width, uint32_t height, uint32_t depth, uint32_t numBricksInitial);

/**
 * frees all resources allocated from splv_frame_create()
 */
SPLV_API void splv_frame_destroy(SPLVframe* frame);

/**
 * gets the index into frame->map cooresponding to the given position
 */
SPLV_API inline uint32_t splv_frame_get_map_idx(SPLVframe* frame, uint32_t x, uint32_t y, uint32_t z);

/**
 * returns a pointer to a fresh brick. does not add this brick to the map, useful as a "scratch buffer"
 */
SPLV_API SPLVbrick* splv_frame_get_next_brick(SPLVframe* frame);

/**
 * adds the brick returned from splv_frame_get_next_brick(frame) to the map at the given location
 */
SPLV_API SPLVerror splv_frame_push_next_brick(SPLVframe* frame, uint32_t x, uint32_t y, uint32_t z);

/**
 * removes all nonvisible voxels from a frame, returning a newly created frame
 */
SPLV_API SPLVerror splv_frame_remove_nonvisible_voxels(SPLVframe* frame, SPLVframe* processedFrame);

/**
 * returns the size, in bytes, of a frame in memory
 */
SPLV_API uint64_t splv_frame_get_size(SPLVframe* frame);

/**
 * returns the total number of voxels in a frame
 */
SPLV_API uint64_t splv_frame_get_num_voxels(SPLVframe* frame);

#endif