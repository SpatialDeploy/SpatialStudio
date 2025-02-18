#include "spatialstudio/splv_brick.h"

#include "splv_morton_lut.h"
#include "spatialstudio/splv_frame.h"
#include "spatialstudio/splv_log.h"
#include <string.h>

#define SPLV_BRICK_GEOM_DIFF_SIZE (1 + 3 * SPLV_BRICK_SIZE_LOG_2)

//-------------------------------------------//

typedef enum SPLVbrickEncodingType
{
	SPLV_BRICK_ENCODING_TYPE_I = 0,
	SPLV_BRICK_ENCODING_TYPE_P = 1
} SPLVbrickEncodingType;

//-------------------------------------------//

static SPLVerror _splv_brick_decode_intra(SPLVbufferReader* reader, SPLVbrick* out);
static SPLVerror _splv_brick_decode_predictive(SPLVbufferReader* reader, SPLVbrick* out, uint32_t xMap, uint32_t yMap, uint32_t zMap, SPLVframe* lastFrame);

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

SPLVerror splv_brick_encode_intra(SPLVbrick* brick, SPLVbufferWriter* out)
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

	//iterate over brick, perform RLE on bitmap and add colors:
	//---------------

	//we do RLE in morton order, we MUST make sure to read it back in the same order
	for(uint32_t i = 0; i < SPLV_BRICK_LEN; i++)
	{
		uint32_t idx = MORTON_TO_IDX[i];
		uint32_t idxArr = idx >> 5;
		uint32_t idxBit = idx & 31;

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
			uint32_t color = brick->color[idx];
			colorBytes[numColorBytes++] = (uint8_t)(color >> 24);
			colorBytes[numColorBytes++] = (uint8_t)((color >> 16) & 0xFF);
			colorBytes[numColorBytes++] = (uint8_t)((color >> 8 ) & 0xFF);

			voxelCount++;
		}
	}

	bitmapBytes[numBitmapBytes++] = curBitmapByte;

	//write:
	//---------------
	uint8_t encodingType = (uint8_t)SPLV_BRICK_ENCODING_TYPE_I;
	SPLV_ERROR_PROPAGATE(splv_buffer_writer_write(out, sizeof(uint8_t), &encodingType));

	SPLV_ERROR_PROPAGATE(splv_buffer_writer_write(out, sizeof(uint32_t), &voxelCount));
	SPLV_ERROR_PROPAGATE(splv_buffer_writer_write(out, numBitmapBytes * sizeof(uint8_t), bitmapBytes));
	SPLV_ERROR_PROPAGATE(splv_buffer_writer_write(out, numColorBytes * sizeof(uint8_t), colorBytes));

	return SPLV_SUCCESS;
}

SPLVerror splv_brick_encode_predictive(SPLVbrick* brick, uint32_t xMap, uint32_t yMap, uint32_t zMap, SPLVbufferWriter* out, SPLVframe* lastFrame)
{
	//encode without prediction if last frame was empty:
	//---------------
	uint32_t lastIdxMap = splv_frame_get_map_idx(lastFrame, xMap, yMap, zMap);
	if(lastFrame->map[lastIdxMap] == SPLV_BRICK_IDX_EMPTY)
		return splv_brick_encode_intra(brick, out);

	SPLVbrick* lastBrick = &lastFrame->bricks[lastFrame->map[lastIdxMap]];

	//find number of diffs in geometry:
	//---------------

	//TODO: optimize with xor and bitcounting

	uint32_t numGeomDiff = 0;
	for(uint32_t z = 0; z < SPLV_BRICK_SIZE; z++)
	for(uint32_t y = 0; y < SPLV_BRICK_SIZE; y++)
	for(uint32_t x = 0; x < SPLV_BRICK_SIZE; x++)
	{
		splv_bool_t filled = splv_brick_get_voxel(brick, x, y, z);
		splv_bool_t wasFilled = splv_brick_get_voxel(lastBrick, x, y, z);

		if(filled != wasFilled)
			numGeomDiff++;
	}

	//determine if we should encode as I-frame:
	//---------------

	//TODO: pick a good value for this
	if(numGeomDiff > 64)
		return splv_brick_encode_intra(brick, out);

	//encode diffs in geom + color:
	//---------------
	uint32_t numGeomDiffBits = 0;
	uint8_t geomDiffBytes[(SPLV_BRICK_GEOM_DIFF_SIZE * SPLV_BRICK_LEN + 7) / 8] = {0};

	uint32_t numColorBytes = 0;
	uint8_t colorBytes[SPLV_BRICK_LEN * 3];

	for(uint32_t z = 0; z < SPLV_BRICK_SIZE; z++)
	for(uint32_t y = 0; y < SPLV_BRICK_SIZE; y++)
	for(uint32_t x = 0; x < SPLV_BRICK_SIZE; x++)
	{
		splv_bool_t filled = splv_brick_get_voxel(brick, x, y, z);
		splv_bool_t wasFilled = splv_brick_get_voxel(lastBrick, x, y, z);

		if(filled)
		{
			uint8_t r, g, b;
			splv_brick_get_voxel_color(brick, x, y, z, &r, &g, &b);

			uint8_t encodeR, encodeG, encodeB;
			if(wasFilled)
			{
				uint8_t lastR, lastG, lastB;
				splv_brick_get_voxel_color(lastBrick, x, y, z, &lastR, &lastG, &lastB);

				encodeR = r - lastR;
				encodeG = g - lastG;
				encodeB = b - lastB;
			}
			else
			{
				encodeR = r;
				encodeG = g;
				encodeB = b;

				_splv_brick_diff_encode(SPLV_TRUE, x, y, z, geomDiffBytes, &numGeomDiffBits);
			}

			colorBytes[numColorBytes++] = encodeR;
			colorBytes[numColorBytes++] = encodeG;
			colorBytes[numColorBytes++] = encodeB;
		}
		else
		{
			if(wasFilled)
				_splv_brick_diff_encode(SPLV_FALSE, x, y, z, geomDiffBytes, &numGeomDiffBits);
			else
				continue;
		}
	}

	//write:
	//---------------
	uint8_t encodingType = (uint8_t)SPLV_BRICK_ENCODING_TYPE_P;
	SPLV_ERROR_PROPAGATE(splv_buffer_writer_write(out, sizeof(uint8_t), &encodingType));
	
	uint8_t numGeomDiffEncoded = (uint8_t)numGeomDiff;
	SPLV_ERROR_PROPAGATE(splv_buffer_writer_write(out, sizeof(uint8_t), &numGeomDiffEncoded));

	SPLV_ERROR_PROPAGATE(splv_buffer_writer_write(out, (numGeomDiffBits + 7) / 8, geomDiffBytes));
	SPLV_ERROR_PROPAGATE(splv_buffer_writer_write(out, numColorBytes, colorBytes));

	return SPLV_SUCCESS;
}

SPLVerror splv_brick_decode(SPLVbufferReader* reader, SPLVbrick* out, uint32_t xMap, uint32_t yMap, uint32_t zMap, SPLVframe* lastFrame)
{
	uint8_t encodingType;
	SPLV_ERROR_PROPAGATE(splv_buffer_reader_read(reader, sizeof(uint8_t), &encodingType));

	if((SPLVbrickEncodingType)encodingType == SPLV_BRICK_ENCODING_TYPE_I)
		return _splv_brick_decode_intra(reader, out);
	else
		return _splv_brick_decode_predictive(reader, out, xMap, yMap, zMap, lastFrame);
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

static SPLVerror _splv_brick_decode_intra(SPLVbufferReader* reader, SPLVbrick* out)
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

static SPLVerror _splv_brick_decode_predictive(SPLVbufferReader* reader, SPLVbrick* out, uint32_t xMap, uint32_t yMap, uint32_t zMap, SPLVframe* lastFrame)
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