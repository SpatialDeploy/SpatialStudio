/* splv_brick.h
 *
 * contains the definition of the brick class which represents a
 * fixed-size grid
 */

#ifndef SPLV_BRICK_H
#define SPLV_BRICK_H

#include "splv_error.h"
#include <stdint.h>
#include <fstream> //TODO: c-style file IO

//-------------------------------------------//

#define SPLV_BRICK_SIZE_LOG_2 3
#define SPLV_BRICK_SIZE_2_LOG_2 2 * SPLV_BRICK_SIZE_LOG_2
#define SPLV_BRICK_SIZE (1 << SPLV_BRICK_SIZE_LOG_2)
#define SPLV_BRICK_LEN (SPLV_BRICK_SIZE * SPLV_BRICK_SIZE * SPLV_BRICK_SIZE)

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
inline void splv_brick_set_voxel_filled(SPLVbrick* brick, uint32_t x, uint32_t y, uint32_t z, uint8_t colorR, uint8_t colorG, uint8_t colorB)
{
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
inline void splv_brick_set_voxel_empty(SPLVbrick* brick, uint32_t x, uint32_t y, uint32_t z)
{
	uint32_t idx = x | (y << SPLV_BRICK_SIZE_LOG_2) | (z << SPLV_BRICK_SIZE_2_LOG_2);
	
	brick->bitmap[idx >> 5] &= ~(1 << (idx & 31));
}

/**
 * returns whether the voxel at the given location is filled
 */
inline uint8_t splv_brick_get_voxel(SPLVbrick* brick, uint32_t x, uint32_t y, uint32_t z)
{
	uint32_t idx = x | (y << SPLV_BRICK_SIZE_LOG_2) | (z << SPLV_BRICK_SIZE_2_LOG_2);
	return (brick->bitmap[idx >> 5] & (1 << (idx & 31))) != 0;
}

/**
 * returns whether the voxel at the given location is filled, as well as its color
 */
inline uint8_t splv_brick_get_voxel_color(SPLVbrick* brick, uint32_t x, uint32_t y, uint32_t z, uint8_t* colorR, uint8_t* colorG, uint8_t* colorB)
{
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
void splv_brick_clear(SPLVbrick* brick);

/**
 * serializes a brick to the given file
 */
SPLVerror splv_brick_serialize(SPLVbrick* brick, std::ostream& out);

#endif //#ifndef SPLV_BRICK_H