#include "spatialstudio/splv_brick.h"

#include "spatialstudio/splv_frame.h"
#include "spatialstudio/splv_log.h"
#include "splv_morton_lut.h"
#include <string.h>
#include <math.h>

//-------------------------------------------//

#define SPLV_BRICK_GEOM_DIFF_SIZE (1 + 3 * SPLV_BRICK_SIZE_LOG_2)

//TODO: finetune
#define SPLV_BRICK_BLOCK_MATCH_GEOM_MISMATCH_COST 256 * 3
#define SPLV_BRICK_BLOCK_MATCH_SEARCH_PARAM 7

//-------------------------------------------//

typedef enum SPLVbrickEncodingType
{
	SPLV_BRICK_ENCODING_TYPE_I = 0,
	SPLV_BRICK_ENCODING_TYPE_P = 1
} SPLVbrickEncodingType;

//-------------------------------------------//

static SPLVerror _splv_brick_decode_intra(SPLVbufferReader* reader, SPLVbrick* out, uint32_t* outVoxels, uint64_t outVoxelsLen, uint32_t* numVoxels);
static SPLVerror _splv_brick_decode_predictive(SPLVbufferReader* in, SPLVbrick* out, uint32_t* outVoxels, uint64_t outVoxelsLen,
                                               uint32_t xMap, uint32_t yMap, uint32_t zMap, SPLVframe* lastFrame, uint32_t* numVoxels);

static SPLVerror _splv_brick_decode_intra_legacy(SPLVbufferReader* reader, SPLVbrick* out);
static SPLVerror _splv_brick_decode_predictive_legacy(SPLVbufferReader* reader, SPLVbrick* out, uint32_t xMap, uint32_t yMap, uint32_t zMap, SPLVframe* lastFrame); 

static inline splv_bool_t _splv_frame_get_voxel(SPLVframe* frame, int32_t x, int32_t y, int32_t z, uint8_t* r, uint8_t* g, uint8_t* b);
static uint64_t _splv_brick_block_match_cost(SPLVbrick* brick, uint32_t xMap, uint32_t yMap, uint32_t zMap, SPLVframe* lastFrame, int32_t offX, int32_t offY, int32_t offZ);
static void _splv_brick_block_match_neighborhood(SPLVbrick* brick, uint32_t xMap, uint32_t yMap, uint32_t zMap,
                                                 SPLVframe* lastFrame, int32_t centerX, int32_t centerY, int32_t centerZ, uint32_t searchDist, splv_bool_t includeCenter,
                                                 uint64_t* minCost, int32_t* bestOffX, int32_t* bestOffY, int32_t* bestOffZ);
static void _splv_brick_compute_motion_vector(SPLVbrick* brick, uint32_t xMap, uint32_t yMap, uint32_t zMap, SPLVframe* lastFrame, int32_t* offX, int32_t* offY, int32_t* offZ);

static inline uint8_t _splv_brick_geom_diff_position_decode(uint8_t* buf, uint32_t* bitIdx);
static inline void _splv_brick_diff_encode(splv_bool_t add, uint32_t x, uint32_t y, uint32_t z, uint8_t* buf, uint32_t* bitIdx);

//-------------------------------------------//

void splv_brick_set_voxel_filled(SPLVbrick* brick, uint32_t x, uint32_t y, uint32_t z, uint8_t colorR, uint8_t colorG, uint8_t colorB)
{
	SPLV_ASSERT(x < SPLV_BRICK_SIZE && y < SPLV_BRICK_SIZE && z < SPLV_BRICK_SIZE, "brick coordinates out of bounds");

	uint32_t idx = x | (y << SPLV_BRICK_SIZE_LOG_2) | (z << SPLV_BRICK_SIZE_2_LOG_2);	
	brick->bitmap[idx >> 5] |= 1 << (idx & 31);

	uint32_t color = (colorR << 24) | (colorG << 16) | (colorB << 8) | 255;
	brick->color[idx] = color;
}

void splv_brick_set_voxel_empty(SPLVbrick* brick, uint32_t x, uint32_t y, uint32_t z)
{
	SPLV_ASSERT(x < SPLV_BRICK_SIZE && y < SPLV_BRICK_SIZE && z < SPLV_BRICK_SIZE, "brick coordinates out of bounds");

	uint32_t idx = x | (y << SPLV_BRICK_SIZE_LOG_2) | (z << SPLV_BRICK_SIZE_2_LOG_2);
	
	brick->bitmap[idx >> 5] &= ~(1 << (idx & 31));
}

splv_bool_t splv_brick_get_voxel(SPLVbrick* brick, uint32_t x, uint32_t y, uint32_t z)
{
	SPLV_ASSERT(x < SPLV_BRICK_SIZE && y < SPLV_BRICK_SIZE && z < SPLV_BRICK_SIZE, "brick coordinates out of bounds");

	uint32_t idx = x | (y << SPLV_BRICK_SIZE_LOG_2) | (z << SPLV_BRICK_SIZE_2_LOG_2);
	return (brick->bitmap[idx >> 5] & (1 << (idx & 31))) != 0;
}

splv_bool_t splv_brick_get_voxel_color(SPLVbrick* brick, uint32_t x, uint32_t y, uint32_t z, uint8_t* colorR, uint8_t* colorG, uint8_t* colorB)
{
	SPLV_ASSERT(x < SPLV_BRICK_SIZE && y < SPLV_BRICK_SIZE && z < SPLV_BRICK_SIZE, "brick coordinates out of bounds");

	uint32_t idx = x | (y << SPLV_BRICK_SIZE_LOG_2) | (z << SPLV_BRICK_SIZE_2_LOG_2);

	uint32_t color = brick->color[idx];
	*colorR = color >> 24;
	*colorG = (color >> 16) & 0xFF;
	*colorB = (color >> 8 ) & 0xFF;

	return (brick->bitmap[idx >> 5] & (1 << (idx & 31))) != 0;
}

void splv_brick_clear(SPLVbrick* brick)
{
	memset(brick->bitmap, 0, sizeof(brick->bitmap));
}

SPLVerror splv_brick_encode_intra(SPLVbrick* brick, SPLVbufferWriter* out, uint32_t* numVoxels)
{
	//initialize bytes:
	//---------------
	uint32_t voxelCount = 0;

	uint8_t bitmapBytes[SPLV_BRICK_LEN]; //1 byte per voxel (worst case)
	uint32_t numBitmapBytes = 0;
	uint8_t colorBytes[SPLV_BRICK_LEN * 3]; //all colors stored (worst case)
	uint32_t numColorBytes = 0;

	uint8_t curBitmapByte; //for RLE
	if((brick->bitmap[0] & 1) != 0)
		curBitmapByte = 0x80;
	else
		curBitmapByte = 0x00;

	//iterate over brick, perform RLE on bitmap and get colors:
	//---------------
	uint8_t rChannels[SPLV_BRICK_LEN];
	uint8_t gChannels[SPLV_BRICK_LEN];
	uint8_t bChannels[SPLV_BRICK_LEN];

	//we do RLE in linear order, we MUST make sure to read it back in the same order
	for(uint32_t i = 0; i < SPLV_BRICK_LEN; i++)
	{
		uint32_t idxArr = i >> 5;
		uint32_t idxBit = i & 31;

		//update RLE
		uint8_t filled = (brick->bitmap[idxArr] & (1u << idxBit)) != 0;
		if(filled != ((curBitmapByte & (1 << 7u)) != 0) || (curBitmapByte & 0x7f) == 127)
		{
			bitmapBytes[numBitmapBytes++] = curBitmapByte;

			if(filled)
				curBitmapByte = 0x80;
			else
				curBitmapByte = 0x00;
		}

		curBitmapByte++;

		//add color if filled
		if(filled)
		{
			uint32_t color = brick->color[i];
			uint8_t r = (uint8_t)(color >> 24);
			uint8_t g = (uint8_t)((color >> 16) & 0xFF);
			uint8_t b = (uint8_t)((color >> 8 ) & 0xFF);

			rChannels[voxelCount] = r;
			gChannels[voxelCount] = g;
			bChannels[voxelCount] = b;

			voxelCount++;
		}
	}

	bitmapBytes[numBitmapBytes++] = curBitmapByte;

	//encode each color as a difference from previous:
	//---------------
	numColorBytes = voxelCount * 3;

	colorBytes[0] = rChannels[0];
	colorBytes[1] = gChannels[0];
	colorBytes[2] = bChannels[0];

	for(uint32_t i = 1; i < voxelCount; i++)
	{
		colorBytes[i * 3 + 0] = rChannels[i] - rChannels[i - 1];
		colorBytes[i * 3 + 1] = gChannels[i] - gChannels[i - 1];
		colorBytes[i * 3 + 2] = bChannels[i] - bChannels[i - 1];
	}

	//write:
	//---------------
	uint8_t encodingType = (uint8_t)SPLV_BRICK_ENCODING_TYPE_I;
	SPLV_ERROR_PROPAGATE(splv_buffer_writer_write(out, sizeof(uint8_t), &encodingType));

	SPLV_ERROR_PROPAGATE(splv_buffer_writer_write(out, numBitmapBytes * sizeof(uint8_t), bitmapBytes));
	SPLV_ERROR_PROPAGATE(splv_buffer_writer_write(out, numColorBytes * sizeof(uint8_t), colorBytes));

	//return:
	//---------------
	*numVoxels = voxelCount;

	return SPLV_SUCCESS;
}

SPLVerror splv_brick_encode_predictive(SPLVbrick* brick, uint32_t xMap, uint32_t yMap, uint32_t zMap, SPLVbufferWriter* out, SPLVframe* lastFrame, uint32_t* numVoxels, splv_bool_t motionVectors)
{
	//estimate motion:
	//---------------
	int32_t xOff = 0;
	int32_t yOff = 0;
	int32_t zOff = 0;

	if(motionVectors)
		_splv_brick_compute_motion_vector(brick, xMap, yMap, zMap, lastFrame, &xOff, &yOff, &zOff);

	//find number of diffs in geometry:
	//---------------

	//TODO: optimize with xor and bitcounting

	uint32_t numGeomDiff = 0;
	uint32_t voxelCount = 0;

	SPLVbrick lastBrick;
	splv_brick_clear(&lastBrick);

	for(uint32_t z = 0; z < SPLV_BRICK_SIZE; z++)
	for(uint32_t y = 0; y < SPLV_BRICK_SIZE; y++)
	for(uint32_t x = 0; x < SPLV_BRICK_SIZE; x++)
	{
		int32_t lastX = xMap * SPLV_BRICK_SIZE + x + xOff;
		int32_t lastY = yMap * SPLV_BRICK_SIZE + y + yOff;
		int32_t lastZ = zMap * SPLV_BRICK_SIZE + z + zOff;

		uint8_t lastR, lastG, lastB;

		splv_bool_t filled = splv_brick_get_voxel(brick, x, y, z);
		splv_bool_t wasFilled = _splv_frame_get_voxel(lastFrame, lastX, lastY, lastZ, &lastR, &lastG, &lastB);

		if(filled != wasFilled)
			numGeomDiff++;

		if(filled)
			voxelCount++;

		if(wasFilled)
			splv_brick_set_voxel_filled(&lastBrick, x, y, z, lastR, lastG, lastB);
	}

	//determine if we should encode as I-frame:
	//---------------

	//TODO: finetune this
	if(numGeomDiff >= voxelCount / 2)
		return splv_brick_encode_intra(brick, out, numVoxels);

	//encode diffs in geom + color:
	//---------------
	uint32_t numBitmapBytes = 0;
	uint8_t bitmapBytes[SPLV_BRICK_LEN]; //1 byte per voxel (worst case)
	uint32_t numColorBytes = 0;
	uint8_t colorBytes[SPLV_BRICK_LEN * 3];

	uint8_t curBitmapByte; //for RLE
	if(((brick->bitmap[0] ^ lastBrick.bitmap[0]) & 1) != 0)
		curBitmapByte = 0x80;
	else
		curBitmapByte = 0x00;

	//TODO: merge this loop with previous

	for(uint32_t z = 0; z < SPLV_BRICK_SIZE; z++)
	for(uint32_t y = 0; y < SPLV_BRICK_SIZE; y++)
	for(uint32_t x = 0; x < SPLV_BRICK_SIZE; x++)
	{
		splv_bool_t filled = splv_brick_get_voxel(brick, x, y, z);
		splv_bool_t wasFilled = splv_brick_get_voxel(&lastBrick, x, y, z);

		if(filled)
		{
			uint8_t r, g, b;
			splv_brick_get_voxel_color(brick, x, y, z, &r, &g, &b);

			uint8_t encodeR, encodeG, encodeB;
			if(wasFilled)
			{
				uint8_t lastR, lastG, lastB;
				splv_brick_get_voxel_color(&lastBrick, x, y, z, &lastR, &lastG, &lastB);

				encodeR = r - lastR;
				encodeG = g - lastG;
				encodeB = b - lastB;
			}
			else
			{
				encodeR = r;
				encodeG = g;
				encodeB = b;
			}

			colorBytes[numColorBytes++] = encodeR;
			colorBytes[numColorBytes++] = encodeG;
			colorBytes[numColorBytes++] = encodeB;
		}

		splv_bool_t diff = filled != wasFilled;
		if(diff != ((curBitmapByte & (1 << 7u)) != 0) || (curBitmapByte & 0x7f) == 127)
		{
			bitmapBytes[numBitmapBytes++] = curBitmapByte;

			if(diff)
				curBitmapByte = 0x80;
			else
				curBitmapByte = 0x00;
		}

		curBitmapByte++;
	}

	bitmapBytes[numBitmapBytes++] = curBitmapByte;

	//write:
	//---------------
	uint8_t encodingType = (uint8_t)SPLV_BRICK_ENCODING_TYPE_P;
	SPLV_ERROR_PROPAGATE(splv_buffer_writer_write(out, sizeof(uint8_t), &encodingType));

	int8_t xOffEncode = (int8_t)xOff;
	int8_t yOffEncode = (int8_t)yOff;
	int8_t zOffEncode = (int8_t)zOff;
	SPLV_ERROR_PROPAGATE(splv_buffer_writer_write(out, sizeof(int8_t), &xOffEncode));
	SPLV_ERROR_PROPAGATE(splv_buffer_writer_write(out, sizeof(int8_t), &yOffEncode));
	SPLV_ERROR_PROPAGATE(splv_buffer_writer_write(out, sizeof(int8_t), &zOffEncode));

	SPLV_ERROR_PROPAGATE(splv_buffer_writer_write(out, numBitmapBytes * sizeof(uint8_t), bitmapBytes));
	SPLV_ERROR_PROPAGATE(splv_buffer_writer_write(out, numColorBytes * sizeof(uint8_t), colorBytes));

	//return:
	//---------------
	*numVoxels = voxelCount;

	return SPLV_SUCCESS;
}

SPLVerror splv_brick_decode(SPLVbufferReader* in, SPLVbrick* out, uint32_t* outVoxels, uint64_t outVoxelsLen,
                            uint32_t xMap, uint32_t yMap, uint32_t zMap, SPLVframe* lastFrame, uint32_t* numVoxels)
{
	uint8_t encodingType;
	SPLV_ERROR_PROPAGATE(splv_buffer_reader_read(in, sizeof(uint8_t), &encodingType));

	if((SPLVbrickEncodingType)encodingType == SPLV_BRICK_ENCODING_TYPE_I)
		return _splv_brick_decode_intra(in, out, outVoxels, outVoxelsLen, numVoxels);
	else if((SPLVbrickEncodingType)encodingType == SPLV_BRICK_ENCODING_TYPE_P)
		return _splv_brick_decode_predictive(in, out, outVoxels, outVoxelsLen, xMap, yMap, zMap, lastFrame, numVoxels);
	else
	{
		SPLV_LOG_ERROR("invalid brick encoding type");
		return SPLV_ERROR_INVALID_INPUT;
	}
}

SPLVerror splv_brick_decode_legacy(SPLVbufferReader* in, SPLVbrick* out, uint32_t xMap, uint32_t yMap, uint32_t zMap, SPLVframe* lastFrame)
{
	uint8_t encodingType;
	SPLV_ERROR_PROPAGATE(splv_buffer_reader_read(in, sizeof(uint8_t), &encodingType));

	if((SPLVbrickEncodingType)encodingType == SPLV_BRICK_ENCODING_TYPE_I)
		return _splv_brick_decode_intra_legacy(in, out);
	else if((SPLVbrickEncodingType)encodingType == SPLV_BRICK_ENCODING_TYPE_P)
		return _splv_brick_decode_predictive_legacy(in, out, xMap, yMap, zMap, lastFrame);
	else
	{
		SPLV_LOG_ERROR("invalid brick encoding type");
		return SPLV_ERROR_INVALID_INPUT;
	}
}

uint32_t splv_brick_get_num_voxels(SPLVbrick* brick)
{
	uint32_t numVoxels = 0;
	for(uint32_t i = 0; i < SPLV_BRICK_LEN; i++)
	{
		if((brick->bitmap[i >> 5] & (1 << (i & 31))) != 0)
			numVoxels++;
	}

	return numVoxels;
}

//-------------------------------------------//

static SPLVerror _splv_brick_decode_intra(SPLVbufferReader* in, SPLVbrick* out, uint32_t* outVoxels, uint64_t outVoxelsLen, uint32_t* numVoxels)
{
	//decode bitmap:
	//-----------------
	splv_brick_clear(out);
	*numVoxels = 0;

	uint32_t i = 0;
	while(i < SPLV_BRICK_SIZE * SPLV_BRICK_SIZE * SPLV_BRICK_SIZE)
	{
		uint8_t curByte;
		SPLV_ERROR_PROPAGATE(splv_buffer_reader_read(in, sizeof(uint8_t), &curByte));
		
		if((curByte & (1u << 7)) != 0)
		{
			curByte = curByte & 0x7F;

			while(curByte > 0)
			{
				uint32_t idxArr = i / 32;
				uint32_t idxBit = i % 32;

				out->bitmap[idxArr] |= 1u << idxBit;

				i++;
				curByte--;

				(*numVoxels)++;
			}
		}
		else
			i += curByte;
	}

	if(i != SPLV_BRICK_SIZE * SPLV_BRICK_SIZE * SPLV_BRICK_SIZE)
	{
		SPLV_LOG_ERROR("brick bitmap decoding had incorrect number of voxels, possibly corrupted data");
		return SPLV_ERROR_INVALID_INPUT;
	}

	if(outVoxels != NULL && *numVoxels > outVoxelsLen)
	{
		SPLV_LOG_ERROR("not enough space in out voxel array to hold brick's voxels");
		return SPLV_ERROR_INVALID_INPUT;
	}

	//loop over every voxel, add to color buffer if present
	//-----------------
	uint8_t prevRgb[3] = {0, 0, 0};

	uint32_t readVoxels = 0;
	for(uint32_t i = 0; i < SPLV_BRICK_SIZE * SPLV_BRICK_SIZE * SPLV_BRICK_SIZE; i++)
	{
		//we are reading in linear order since we encode in that order
		uint32_t arrIdx = i / 32;
		uint32_t bitIdx = i % 32;

		if((out->bitmap[arrIdx] & (uint32_t)(1 << bitIdx)) != 0)
		{
			uint8_t rgb[3];
			SPLV_ERROR_PROPAGATE(splv_buffer_reader_read(in, 3 * sizeof(uint8_t), rgb));

			rgb[0] += prevRgb[0];
			rgb[1] += prevRgb[1];
			rgb[2] += prevRgb[2];

			uint32_t packedColor = (rgb[0] << 24) | (rgb[1] << 16) | (rgb[2] << 8) | 255;
			out->color[i] = packedColor;

			if(outVoxels != NULL)
				outVoxels[readVoxels] = packedColor;

			readVoxels++;

			prevRgb[0] = rgb[0];
			prevRgb[1] = rgb[1];
			prevRgb[2] = rgb[2];
		}
	}

	if(readVoxels != *numVoxels)
	{
		SPLV_LOG_ERROR("brick had incorrect number of voxels, possibly corrupted data");
		return SPLV_ERROR_INVALID_INPUT;
	}

	return SPLV_SUCCESS;
}

static SPLVerror _splv_brick_decode_predictive(SPLVbufferReader* in, SPLVbrick* out, uint32_t* outVoxels, uint64_t outVoxelsLen,
                                               uint32_t xMap, uint32_t yMap, uint32_t zMap, SPLVframe* lastFrame, uint32_t* numVoxels)
{
	//read motion vector:
	//-----------------
	int8_t xOff, yOff, zOff;
	SPLV_ERROR_PROPAGATE(splv_buffer_reader_read(in, sizeof(int8_t), &xOff));
	SPLV_ERROR_PROPAGATE(splv_buffer_reader_read(in, sizeof(int8_t), &yOff));
	SPLV_ERROR_PROPAGATE(splv_buffer_reader_read(in, sizeof(int8_t), &zOff));

	//copy last frame
	//-----------------
	memset(out, 0, sizeof(SPLVbrick));

	for(uint32_t z = 0; z < SPLV_BRICK_SIZE; z++)
	for(uint32_t y = 0; y < SPLV_BRICK_SIZE; y++)
	for(uint32_t x = 0; x < SPLV_BRICK_SIZE; x++)
	{
		uint32_t lastX = xMap * SPLV_BRICK_SIZE + x + xOff;
		uint32_t lastY = yMap * SPLV_BRICK_SIZE + y + yOff;
		uint32_t lastZ = zMap * SPLV_BRICK_SIZE + z + zOff;

		uint8_t lastR, lastG, lastB;
		splv_bool_t filled = _splv_frame_get_voxel(lastFrame, lastX, lastY, lastZ, &lastR, &lastG, &lastB);

		if(filled)
			splv_brick_set_voxel_filled(out, x, y, z, lastR, lastG, lastB);
	}

	//decode geom diffs:
	//-----------------
	uint32_t i = 0;
	while(i < SPLV_BRICK_SIZE * SPLV_BRICK_SIZE * SPLV_BRICK_SIZE)
	{
		uint8_t curByte;
		SPLV_ERROR_PROPAGATE(splv_buffer_reader_read(in, sizeof(uint8_t), &curByte));
		
		if((curByte & (1u << 7)) != 0)
		{
			curByte = curByte & 0x7F;

			while(curByte > 0)
			{
				uint32_t idxArr = i / 32;
				uint32_t idxBit = i % 32;

				out->bitmap[idxArr] ^= 1u << idxBit;

				i++;
				curByte--;
			}
		}
		else
			i += curByte;
	}

	if(i != SPLV_BRICK_SIZE * SPLV_BRICK_SIZE * SPLV_BRICK_SIZE)
	{
		SPLV_LOG_ERROR("brick bitmap decoding had incorrect number of voxels, possibly corrupted data");
		return SPLV_ERROR_INVALID_INPUT;
	}

	//read colors
	//-----------------
	*numVoxels = 0;

	for(uint32_t z = 0; z < SPLV_BRICK_SIZE; z++)
	for(uint32_t y = 0; y < SPLV_BRICK_SIZE; y++)
	for(uint32_t x = 0; x < SPLV_BRICK_SIZE; x++)
	{
		uint32_t idx = x | (y << SPLV_BRICK_SIZE_LOG_2) | (z << SPLV_BRICK_SIZE_2_LOG_2);
		if((out->bitmap[idx >> 5] & (1 << (idx & 31))) == 0)
			continue;

		uint8_t rgb[3];
		SPLV_ERROR_PROPAGATE(splv_buffer_reader_read(in, 3 * sizeof(uint8_t), rgb));

		uint32_t oldColor = out->color[idx];
		uint8_t r = (oldColor >> 24) + rgb[0];
		uint8_t g = ((oldColor >> 16) & 0xFF) + rgb[1];
		uint8_t b = ((oldColor >> 8 ) & 0xFF) + rgb[2];

		uint32_t color = (r << 24) | (g << 16) | (b << 8) | 255;
		out->color[idx] = color;

		if(outVoxels != NULL)
		{
			if(*numVoxels >= outVoxelsLen)
			{
				SPLV_LOG_ERROR("not enough space in out voxel array to hold brick's voxels");
				return SPLV_ERROR_INVALID_INPUT;
			}

			outVoxels[*numVoxels] = color;
		}

		(*numVoxels)++;
	}

	return SPLV_SUCCESS;
}

static SPLVerror _splv_brick_decode_intra_legacy(SPLVbufferReader* reader, SPLVbrick* out)
{
	//read number of voxels
	//-----------------
	uint32_t numVoxels;
	SPLV_ERROR_PROPAGATE(splv_buffer_reader_read(reader, sizeof(uint32_t), &numVoxels));

	//decode bitmap:
	//-----------------
	splv_brick_clear(out);

	//TODO: is this more optimal?

	uint32_t i = 0;
	while(i < SPLV_BRICK_SIZE * SPLV_BRICK_SIZE * SPLV_BRICK_SIZE)
	{
		uint8_t curByte;
		SPLV_ERROR_PROPAGATE(splv_buffer_reader_read(reader, sizeof(uint8_t), &curByte));
		
		if((curByte & (1u << 7)) != 0)
		{
			curByte = curByte & 0x7F;

			while(curByte > 0)
			{
				uint32_t idx = MORTON_TO_IDX[i];
				uint32_t idxArr = idx / 32;
				uint32_t idxBit = idx % 32;

				out->bitmap[idxArr] |= 1u << idxBit;

				i++;
				curByte--;
			}
		}
		else
			i += curByte;
	}

	if(i != SPLV_BRICK_SIZE * SPLV_BRICK_SIZE * SPLV_BRICK_SIZE)
	{
		SPLV_LOG_ERROR("brick bitmap decoding had incorrect number of voxels, possibly corrupted data");
		return SPLV_ERROR_INVALID_INPUT;
	}

	//loop over every voxel, add to color buffer if present
	//-----------------
	uint32_t readVoxels = 0;
	for(uint32_t i = 0; i < SPLV_BRICK_SIZE * SPLV_BRICK_SIZE * SPLV_BRICK_SIZE; i++)
	{
		//we are reading in morton order since we encode in that order
		uint32_t idx = MORTON_TO_IDX[i];
		uint32_t arrIdx = idx / 32;
		uint32_t bitIdx = idx % 32;

		if((out->bitmap[arrIdx] & (uint32_t)(1 << bitIdx)) != 0)
		{
			uint8_t rgb[3];
			SPLV_ERROR_PROPAGATE(splv_buffer_reader_read(reader, 3 * sizeof(uint8_t), rgb));

			uint32_t packedColor = (rgb[0] << 24) | (rgb[1] << 16) | (rgb[2] << 8) | 255;
			out->color[idx] = packedColor;

			readVoxels++;
		}
	}

	if(readVoxels != numVoxels)
	{
		SPLV_LOG_ERROR("brick had incorrect number of voxels, possibly corrupted data");
		return SPLV_ERROR_INVALID_INPUT;
	}

	return SPLV_SUCCESS;
}

static SPLVerror _splv_brick_decode_predictive_legacy(SPLVbufferReader* reader, SPLVbrick* out, uint32_t xMap, uint32_t yMap, uint32_t zMap, SPLVframe* lastFrame)
{
	//read geom diff
	//-----------------
	uint8_t numGeomDiff;
	SPLV_ERROR_PROPAGATE(splv_buffer_reader_read(reader, sizeof(uint8_t), &numGeomDiff));

	uint8_t geomDiffEncoded[(SPLV_BRICK_GEOM_DIFF_SIZE * SPLV_BRICK_LEN + 7) / 8];
	SPLV_ERROR_PROPAGATE(splv_buffer_reader_read(reader, (SPLV_BRICK_GEOM_DIFF_SIZE * (uint32_t)numGeomDiff + 7) / 8, geomDiffEncoded));

	//create brick geometry
	//-----------------
	uint32_t lastBrickIdx = lastFrame->map[splv_frame_get_map_idx(lastFrame, xMap, yMap, zMap)];
	if(lastBrickIdx == SPLV_BRICK_IDX_EMPTY)
	{
		SPLV_LOG_ERROR("p-frame brick did not exist last frame");
		return SPLV_ERROR_INVALID_INPUT;
	}

	//TODO: find fastewr way to copy?
	SPLVbrick* lastBrick = &lastFrame->bricks[lastBrickIdx];
	memcpy((void*)out, (void*)lastBrick, sizeof(SPLVbrick));

	uint32_t geomDiffBitIdx = 0;
	for(uint32_t i = 0; i < numGeomDiff; i++)
	{
		uint8_t add = (geomDiffEncoded[geomDiffBitIdx / 8] & (1 << (7 - (geomDiffBitIdx % 8)))) != 0;
		geomDiffBitIdx++;

		uint8_t x = _splv_brick_geom_diff_position_decode(geomDiffEncoded, &geomDiffBitIdx);
		uint8_t y = _splv_brick_geom_diff_position_decode(geomDiffEncoded, &geomDiffBitIdx);
		uint8_t z = _splv_brick_geom_diff_position_decode(geomDiffEncoded, &geomDiffBitIdx);
	
		if(add)
			splv_brick_set_voxel_filled(out, x, y, z, 0, 0, 0);
		else
			splv_brick_set_voxel_empty(out, x, y, z);
	}

	//read colors
	//-----------------
	for(uint32_t z = 0; z < SPLV_BRICK_SIZE; z++)
	for(uint32_t y = 0; y < SPLV_BRICK_SIZE; y++)
	for(uint32_t x = 0; x < SPLV_BRICK_SIZE; x++)
	{
		uint32_t idx = x | (y << SPLV_BRICK_SIZE_LOG_2) | (z << SPLV_BRICK_SIZE_2_LOG_2);
		if((out->bitmap[idx >> 5] & (1 << (idx & 31))) == 0)
			continue;

		uint8_t rgb[3];
		SPLV_ERROR_PROPAGATE(splv_buffer_reader_read(reader, 3 * sizeof(uint8_t), rgb));

		uint32_t oldColor = out->color[idx];
		uint8_t r = (oldColor >> 24) + rgb[0];
		uint8_t g = ((oldColor >> 16) & 0xFF) + rgb[1];
		uint8_t b = ((oldColor >> 8 ) & 0xFF) + rgb[2];

		uint32_t color = (r << 24) | (g << 16) | (b << 8) | 255;
		out->color[idx] = color;
	}

	return SPLV_SUCCESS;
}

//-------------------------------------------//

static inline splv_bool_t _splv_frame_get_voxel(SPLVframe* frame, int32_t x, int32_t y, int32_t z, uint8_t* r, uint8_t* g, uint8_t* b)
{
	//set out colors to 0 (for conveinience when decoding): 
	//-----------------
	*r = 0;
	*g = 0;
	*b = 0;

	//bounds check:
	//-----------------
	if(x < 0 || y < 0 || z < 0)
		return SPLV_FALSE;

	int32_t xMap = x / SPLV_BRICK_SIZE;
	int32_t yMap = y / SPLV_BRICK_SIZE;
	int32_t zMap = z / SPLV_BRICK_SIZE;

	if(xMap < 0 || (uint32_t)xMap >= frame->width  ||
	   yMap < 0 || (uint32_t)yMap >= frame->height ||
	   zMap < 0 || (uint32_t)zMap >= frame->depth)
		return SPLV_FALSE;

	//get brick:
	//-----------------
	uint32_t mapIdx = splv_frame_get_map_idx(frame, xMap, yMap, zMap);
	uint32_t brickIdx = frame->map[mapIdx];
	if(brickIdx == SPLV_BRICK_IDX_EMPTY)
		return SPLV_FALSE;

	//get voxel:
	//-----------------
	uint32_t xBrick = (uint32_t)x % SPLV_BRICK_SIZE;
	uint32_t yBrick = (uint32_t)y % SPLV_BRICK_SIZE;
	uint32_t zBrick = (uint32_t)z % SPLV_BRICK_SIZE;

	return splv_brick_get_voxel_color(&frame->bricks[brickIdx], xBrick, yBrick, zBrick, r, g, b);
}

static uint64_t _splv_brick_block_match_cost(SPLVbrick* brick, uint32_t xMap, uint32_t yMap, uint32_t zMap, SPLVframe* lastFrame, int32_t offX, int32_t offY, int32_t offZ)
{
	uint64_t cost = 0;

	//compute start/end brick:
	//-----------------
	int32_t mapStartX = (xMap * SPLV_BRICK_SIZE + offX);
	int32_t mapStartY = (yMap * SPLV_BRICK_SIZE + offY);
	int32_t mapStartZ = (zMap * SPLV_BRICK_SIZE + offZ);
	if(mapStartX < 0) mapStartX -= SPLV_BRICK_SIZE;
	if(mapStartY < 0) mapStartY -= SPLV_BRICK_SIZE;
	if(mapStartZ < 0) mapStartZ -= SPLV_BRICK_SIZE;
	mapStartX /= SPLV_BRICK_SIZE;
	mapStartY /= SPLV_BRICK_SIZE;
	mapStartZ /= SPLV_BRICK_SIZE;

	int32_t mapEndX = (xMap * SPLV_BRICK_SIZE + (SPLV_BRICK_SIZE - 1) + offX) / SPLV_BRICK_SIZE;
	int32_t mapEndY = (yMap * SPLV_BRICK_SIZE + (SPLV_BRICK_SIZE - 1) + offY) / SPLV_BRICK_SIZE;
	int32_t mapEndZ = (zMap * SPLV_BRICK_SIZE + (SPLV_BRICK_SIZE - 1) + offZ) / SPLV_BRICK_SIZE;

	//loop over bricks:
	//-----------------
	for(int32_t mapZ = mapStartZ; mapZ <= mapEndZ; mapZ++)
	for(int32_t mapY = mapStartY; mapY <= mapEndY; mapY++)
	for(int32_t mapX = mapStartX; mapX <= mapEndX; mapX++)
	{
		//compute start/end voxels:
		//-----------------
		int32_t voxStartX = (mapX == mapStartX) ? offX % SPLV_BRICK_SIZE : 0;
		int32_t voxStartY = (mapY == mapStartY) ? offY % SPLV_BRICK_SIZE : 0;
		int32_t voxStartZ = (mapZ == mapStartZ) ? offZ % SPLV_BRICK_SIZE : 0;
		if(voxStartX < 0) voxStartX += SPLV_BRICK_SIZE;
		if(voxStartY < 0) voxStartY += SPLV_BRICK_SIZE;
		if(voxStartZ < 0) voxStartZ += SPLV_BRICK_SIZE;
		
		int32_t voxEndX = (mapX == mapEndX) ? (mapX * SPLV_BRICK_SIZE + SPLV_BRICK_SIZE - 1 + offX) % SPLV_BRICK_SIZE : SPLV_BRICK_SIZE - 1;
		int32_t voxEndY = (mapY == mapEndY) ? (mapY * SPLV_BRICK_SIZE + SPLV_BRICK_SIZE - 1 + offY) % SPLV_BRICK_SIZE : SPLV_BRICK_SIZE - 1;
		int32_t voxEndZ = (mapZ == mapEndZ) ? (mapZ * SPLV_BRICK_SIZE + SPLV_BRICK_SIZE - 1 + offZ) % SPLV_BRICK_SIZE : SPLV_BRICK_SIZE - 1;
		
		//get brick:
		//-----------------
		SPLVbrick* lastBrick = NULL;
		if(mapX >= 0 && mapX < (int32_t)lastFrame->width  && 
		   mapY >= 0 && mapY < (int32_t)lastFrame->height && 
		   mapZ >= 0 && mapZ < (int32_t)lastFrame->depth)
		{
			uint32_t mapIdx = splv_frame_get_map_idx(lastFrame, mapX, mapY, mapZ);
			uint32_t brickIdx = lastFrame->map[mapIdx];
	
			if(brickIdx != SPLV_BRICK_IDX_EMPTY)
				lastBrick = &lastFrame->bricks[brickIdx];
		}
		
		//check each voxel:
		//-----------------
		for(int32_t z = voxStartZ; z <= voxEndZ; z++)
		for(int32_t y = voxStartY; y <= voxEndY; y++)
		for(int32_t x = voxStartX; x <= voxEndX; x++)
		{
			int32_t srcX = x - voxStartX + (SPLV_BRICK_SIZE - voxEndX - 1);
			int32_t srcY = y - voxStartY + (SPLV_BRICK_SIZE - voxEndY - 1);
			int32_t srcZ = z - voxStartZ + (SPLV_BRICK_SIZE - voxEndZ - 1);

			uint8_t r1, g1, b1;
			splv_bool_t filled1 = splv_brick_get_voxel_color(brick, srcX, srcY, srcZ, &r1, &g1, &b1);

			uint8_t r2, g2, b2;
			splv_bool_t filled2;
			if(lastBrick)
				filled2 = splv_brick_get_voxel_color(lastBrick, x, y, z, &r2, &g2, &b2);
			else
			{
				filled2 = SPLV_FALSE;
				r2 = 0;
				g2 = 0;
				b2 = 0;
			}

			if(filled1 != filled2)
				cost += SPLV_BRICK_BLOCK_MATCH_GEOM_MISMATCH_COST;
			else if(filled1 != SPLV_FALSE)
			{
				cost += llabs((int64_t)r1 - (int64_t)r2);
				cost += llabs((int64_t)g1 - (int64_t)g2);
				cost += llabs((int64_t)b1 - (int64_t)b2);
			}
		}
	}

	return cost;
}

static void _splv_brick_block_match_neighborhood(SPLVbrick* brick, uint32_t xMap, uint32_t yMap, uint32_t zMap,
                                                 SPLVframe* lastFrame, int32_t centerX, int32_t centerY, int32_t centerZ, uint32_t searchDist, splv_bool_t includeCenter,
                                                 uint64_t* minCost, int32_t* bestOffX, int32_t* bestOffY, int32_t* bestOffZ)
{
	for(int32_t z = -1; z <= 1; z++)
	for(int32_t y = -1; y <= 1; y++)
	for(int32_t x = -1; x <= 1; x++)
	{
		if(!includeCenter && x == 0 && y == 0 && z == 0)
			continue;

		int32_t offX = centerX + x * searchDist;
		int32_t offY = centerY + y * searchDist;
		int32_t offZ = centerZ + z * searchDist;

		uint64_t cost = _splv_brick_block_match_cost(brick, xMap, yMap, zMap, lastFrame, offX, offY, offZ);
		if(cost < *minCost || (cost == *minCost && x == 0 && y == 0 && z == 0))
		{
			*minCost = cost;
			*bestOffX = offX;
			*bestOffY = offY;
			*bestOffZ = offZ;
		}
	}
}

static void _splv_brick_compute_motion_vector(SPLVbrick* brick, uint32_t xMap, uint32_t yMap, uint32_t zMap, SPLVframe* lastFrame, int32_t* bestOffX, int32_t* bestOffY, int32_t* bestOffZ)
{
	//initialize:
	//-----------------
	const uint32_t INITIAL_SEARCH_DIST = (SPLV_BRICK_BLOCK_MATCH_SEARCH_PARAM + 1) / 2;
	uint32_t searchDist = INITIAL_SEARCH_DIST;

	uint64_t minCost = UINT64_MAX;

	//search local area:
	//-----------------
	_splv_brick_block_match_neighborhood(
		brick, xMap, yMap, zMap, lastFrame,
		0, 0, 0, 1, SPLV_TRUE,
		&minCost, bestOffX, bestOffY, bestOffZ
	);

	//search macro area:
	//-----------------
	_splv_brick_block_match_neighborhood(
		brick, xMap, yMap, zMap, lastFrame,
		0, 0, 0, searchDist, SPLV_FALSE,
		&minCost, bestOffX, bestOffY, bestOffZ
	);

	//if best cost is 0, early exit:
	//-----------------
	if(*bestOffX == 0 && *bestOffY == 0 && *bestOffZ == 0)
		return;

	//if best cost is in local area, search adjacent voxels then early exit:
	//-----------------
	if(abs(*bestOffX) <= 1 && abs(*bestOffY) <= 1 && abs(*bestOffZ) <= 1)
	{
		int32_t centerX = *bestOffX;
		int32_t centerY = *bestOffY;
		int32_t centerZ = *bestOffZ;

		for(int32_t z = -1; z <= 1; z++)
		for(int32_t y = -1; y <= 1; y++)
		for(int32_t x = -1; x <= 1; x++)
		{
			int32_t offX = centerX + x;
			int32_t offY = centerY + y;
			int32_t offZ = centerZ + z;

			//skip offsets we already checked
			if(abs(offX) <= 1 && abs(offY) <= 1 && abs(offZ) <= 1)
				continue;

			uint64_t cost = _splv_brick_block_match_cost(brick, xMap, yMap, zMap, lastFrame, offX, offY, offZ);
			if(cost < minCost)
			{
				minCost = cost;
				*bestOffX = offX;
				*bestOffY = offY;
				*bestOffZ = offZ;
			}
		}

		return;
	}

	//if best cose is in macro area, keep searching:
	//-----------------
	while(searchDist > 1)
	{
		searchDist /= 2;

		_splv_brick_block_match_neighborhood(
			brick, xMap, yMap, zMap, lastFrame,
			*bestOffX, *bestOffY, *bestOffZ, searchDist, SPLV_FALSE,
			&minCost, bestOffX, bestOffY, bestOffZ
		);
	}
}

//-------------------------------------------//

static inline void _splv_brick_diff_encode(splv_bool_t add, uint32_t x, uint32_t y, uint32_t z, uint8_t* buf, uint32_t* bitIdx)
{
	buf[*bitIdx / 8] |= (add << (7 - (*bitIdx % 8)));
	(*bitIdx)++;

	for(int i = 0; i < SPLV_BRICK_SIZE_LOG_2; i++) 
	{
		buf[*bitIdx / 8] |= (((x >> i) & 1) << (7 - (*bitIdx % 8)));
		(*bitIdx)++;
	}

	for(int i = 0; i < SPLV_BRICK_SIZE_LOG_2; i++) 
	{
		buf[*bitIdx / 8] |= (((y >> i) & 1) << (7 - (*bitIdx % 8)));
		(*bitIdx)++;
	}

	for(int i = 0; i < SPLV_BRICK_SIZE_LOG_2; i++) 
	{
		buf[*bitIdx / 8] |= (((z >> i) & 1) << (7 - (*bitIdx % 8)));
		(*bitIdx)++;
	}
}

static inline uint8_t _splv_brick_geom_diff_position_decode(uint8_t* buf, uint32_t* bitIdx)
{
	uint8_t pos = 0;
	for(int i = 0; i < SPLV_BRICK_SIZE_LOG_2; i++) 
	{
		uint8_t bit = buf[*bitIdx / 8] & (1 << (7 - (*bitIdx % 8)));
		pos |= (bit >> (7 - (*bitIdx % 8))) << i;
		(*bitIdx)++;
	}

	return pos;
}