/* splv_brick.h
 *
 * contains the definition of the brick class which represents a
 * fixed-size grid
 */

#ifndef SPLV_BRICK_H
#define SPLV_BRICK_H

#include "splv_error.h"
#include "splv_global.h"
#include "splv_buffer_io.h"
#include <stdint.h>

//-------------------------------------------//

#define SPLV_BRICK_SIZE_LOG_2 3
#define SPLV_BRICK_SIZE_2_LOG_2 2 * SPLV_BRICK_SIZE_LOG_2
#define SPLV_BRICK_SIZE (1 << SPLV_BRICK_SIZE_LOG_2)
#define SPLV_BRICK_LEN (SPLV_BRICK_SIZE * SPLV_BRICK_SIZE * SPLV_BRICK_SIZE)

#if SPLV_BRICK_SIZE_LOG_2 > 8
	#error Brick size should be small enough to fit coordintes in a single byte
#endif

//-------------------------------------------//

/**
 * a BRICK_SIZE^3 grid of voxels
 */
typedef struct SPLVbrick
{
	uint32_t bitmap[SPLV_BRICK_LEN / 32];
	uint8_t color[SPLV_BRICK_LEN * 3]; //3 channel RGB
} SPLVbrick;

//-------------------------------------------//

/**
 * sets a given voxel to be filled, having a given color
 */
SPLV_API inline void splv_brick_set_voxel_filled(SPLVbrick* brick, uint32_t x, uint32_t y, uint32_t z, uint8_t colorR, uint8_t colorG, uint8_t colorB)
{
	SPLV_ASSERT(x < SPLV_BRICK_SIZE && y < SPLV_BRICK_SIZE && z < SPLV_BRICK_SIZE, "brick coordinates out of bounds");

	uint32_t idx = x | (y << SPLV_BRICK_SIZE_LOG_2) | (z << SPLV_BRICK_SIZE_2_LOG_2);	
	brick->bitmap[idx >> 5] |= 1 << (idx & 31);

	uint32_t colorIdx = idx * 3;
	brick->color[colorIdx + 0] = colorR;
	brick->color[colorIdx + 1] = colorG;
	brick->color[colorIdx + 2] = colorB;
}

/**
 * sets a given voxel to be empty
 */
SPLV_API inline void splv_brick_set_voxel_empty(SPLVbrick* brick, uint32_t x, uint32_t y, uint32_t z)
{
	SPLV_ASSERT(x < SPLV_BRICK_SIZE && y < SPLV_BRICK_SIZE && z < SPLV_BRICK_SIZE, "brick coordinates out of bounds");

	uint32_t idx = x | (y << SPLV_BRICK_SIZE_LOG_2) | (z << SPLV_BRICK_SIZE_2_LOG_2);
	
	brick->bitmap[idx >> 5] &= ~(1 << (idx & 31));
}

/**
 * returns whether the voxel at the given location is filled
 */
SPLV_API inline splv_bool_t splv_brick_get_voxel(SPLVbrick* brick, uint32_t x, uint32_t y, uint32_t z)
{
	SPLV_ASSERT(x < SPLV_BRICK_SIZE && y < SPLV_BRICK_SIZE && z < SPLV_BRICK_SIZE, "brick coordinates out of bounds");

	uint32_t idx = x | (y << SPLV_BRICK_SIZE_LOG_2) | (z << SPLV_BRICK_SIZE_2_LOG_2);
	return (brick->bitmap[idx >> 5] & (1 << (idx & 31))) != 0;
}

/**
 * returns whether the voxel at the given location is filled, as well as its color
 */
SPLV_API inline splv_bool_t splv_brick_get_voxel_color(SPLVbrick* brick, uint32_t x, uint32_t y, uint32_t z, uint8_t* colorR, uint8_t* colorG, uint8_t* colorB)
{
	SPLV_ASSERT(x < SPLV_BRICK_SIZE && y < SPLV_BRICK_SIZE && z < SPLV_BRICK_SIZE, "brick coordinates out of bounds");

	uint32_t idx = x | (y << SPLV_BRICK_SIZE_LOG_2) | (z << SPLV_BRICK_SIZE_2_LOG_2);

	uint32_t colorIdx = idx * 3;
	*colorR = brick->color[colorIdx + 0];
	*colorG = brick->color[colorIdx + 1];
	*colorB = brick->color[colorIdx + 2];

	return (brick->bitmap[idx >> 5] & (1 << (idx & 31))) != 0;
}

/**
 * clears a brick to be completely empty
 */
SPLV_API void splv_brick_clear(SPLVbrick* brick);

/**
 * serializes a brick to the given buffer writer
 */
SPLV_API SPLVerror splv_brick_serialize(SPLVbrick* brick, SPLVbufferWriter* out);

typedef struct SPLVframe SPLVframe;

/**
 * serializes a brick into the given buffer writer, using information from the previous frame to predict
 */
SPLV_API SPLVerror splv_brick_serialize_predictive(SPLVbrick* brick, uint32_t xMap, uint32_t yMap, uint32_t zMap, SPLVbufferWriter* out, SPLVframe* lastFrame);

#endif //#ifndef SPLV_BRICK_H