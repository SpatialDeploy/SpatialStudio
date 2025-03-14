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
	uint32_t color[SPLV_BRICK_LEN]; //4 channel RGBA
} SPLVbrick;

//-------------------------------------------//

/**
 * sets a given voxel to be filled, having a given color
 */
SPLV_API void splv_brick_set_voxel_filled(SPLVbrick* brick, uint32_t x, uint32_t y, uint32_t z, uint8_t colorR, uint8_t colorG, uint8_t colorB);

/**
 * sets a given voxel to be empty
 */
SPLV_API void splv_brick_set_voxel_empty(SPLVbrick* brick, uint32_t x, uint32_t y, uint32_t z);

/**
 * returns whether the voxel at the given location is filled
 */
SPLV_API splv_bool_t splv_brick_get_voxel(SPLVbrick* brick, uint32_t x, uint32_t y, uint32_t z);

/**
 * returns whether the voxel at the given location is filled, as well as its color
 */
SPLV_API splv_bool_t splv_brick_get_voxel_color(SPLVbrick* brick, uint32_t x, uint32_t y, uint32_t z, uint8_t* colorR, uint8_t* colorG, uint8_t* colorB);

/**
 * clears a brick to be completely empty
 */
SPLV_API void splv_brick_clear(SPLVbrick* brick);

/**
 * encodes a brick to the given buffer writer, using only intra-frame encoding
 */
SPLV_API SPLVerror splv_brick_encode_intra(SPLVbrick* brick, SPLVbufferWriter* out, uint32_t* numVoxels);

typedef struct SPLVframe SPLVframe;

/**
 * encodes a brick into the given buffer writer, using information from the previous frame to predict
 */
SPLV_API SPLVerror splv_brick_encode_predictive(SPLVbrick* brick, uint32_t xMap, uint32_t yMap, uint32_t zMap, SPLVbufferWriter* out, SPLVframe* lastFrame, uint32_t* numVoxels, splv_bool_t motionVectors);

/**
 * decodes a brick from an input reader into the given pointer
 */
SPLV_API SPLVerror splv_brick_decode(SPLVbufferReader* in, SPLVbrick* out, uint32_t* outVoxels, uint64_t outVoxelsLen,
                                     uint32_t xMap, uint32_t yMap, uint32_t zMap, SPLVframe* lastFrame, uint32_t* numVoxels);

/**
 * decodes a brick from an input reader into the given pointer. Works for SPLVs in the previous version (not ALL previous versions)
 */
SPLV_API SPLVerror splv_brick_decode_legacy(SPLVbufferReader* in, SPLVbrick* out, uint32_t xMap, uint32_t yMap, uint32_t zMap, SPLVframe* lastFrame);

/**
 * returns the number of filled voxels in a brick
 */
SPLV_API uint32_t splv_brick_get_num_voxels(SPLVbrick* brick);

#endif //#ifndef SPLV_BRICK_H