#include "splv_brick.h"

#include "splv_morton_lut.h"
#include "splv_frame.h"
#include "splv_log.h"
#include <string.h>

#define SPLV_BRICK_GEOM_DIFF_SIZE (1 + 3 * SPLV_BRICK_SIZE_LOG_2)

//-------------------------------------------//

typedef struct SPLVbrickGeomDiff
{
	splv_bool_t add;
	uint8_t x;
	uint8_t y;
	uint8_t z;
} SPLVbrickGeomDiff;

typedef enum SPLVbrickEncodingType
{
	SPLV_BRICK_ENCODING_TYPE_I = 0,
	SPLV_BRICK_ENCODING_TYPE_P = 1
} SPLVbrickEncodingType;

//-------------------------------------------//

static inline void splv_brick_diff_encode(splv_bool_t add, uint32_t x, uint32_t y, uint32_t z, uint8_t* buf, uint32_t* bitIdx);

//-------------------------------------------//

void splv_brick_clear(SPLVbrick* brick)
{
	memset(brick->bitmap, 0, sizeof(brick->bitmap));
}

SPLVerror splv_brick_serialize(SPLVbrick* brick, std::ostream& out)
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
		uint32_t x = MORTON_TO_COORDINATE[i].x;
		uint32_t y = MORTON_TO_COORDINATE[i].y;
		uint32_t z = MORTON_TO_COORDINATE[i].z;

		uint32_t idx = x | (y << SPLV_BRICK_SIZE_LOG_2) | (z << SPLV_BRICK_SIZE_2_LOG_2);
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
			uint32_t colorIdx = idx * 3;
			colorBytes[numColorBytes++] = brick->color[colorIdx + 0];
			colorBytes[numColorBytes++] = brick->color[colorIdx + 1];
			colorBytes[numColorBytes++] = brick->color[colorIdx + 2];

			voxelCount++;
		}
	}

	bitmapBytes[numBitmapBytes++] = curBitmapByte;

	//write:
	//---------------
	uint8_t encodingType = (uint8_t)SPLV_BRICK_ENCODING_TYPE_I;
	out.write((const char*)&encodingType, sizeof(uint8_t));

	out.write((const char*)&voxelCount, sizeof(uint32_t));
	out.write((const char*)bitmapBytes, numBitmapBytes * sizeof(uint8_t));
	out.write((const char*)colorBytes, numColorBytes * sizeof(uint8_t));

	if(!out.good())
	{
		SPLV_LOG_ERROR("error writing brick to output stream");
		return SPLV_ERROR_FILE_WRITE;
	}

	return SPLV_SUCCESS;
}

SPLVerror splv_brick_serialize_predictive(SPLVbrick* brick, uint32_t xMap, uint32_t yMap, uint32_t zMap, std::ostream& out, SPLVframe* lastFrame)
{
	//encode without prediction if last frame was empty:
	//---------------
	uint32_t lastIdxMap = splv_frame_get_map_idx(lastFrame, xMap, yMap, zMap);
	if(lastFrame->map[lastIdxMap] == SPLV_BRICK_IDX_EMPTY)
		return splv_brick_serialize(brick, out);

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
		return splv_brick_serialize(brick, out);

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

				splv_brick_diff_encode(SPLV_TRUE, x, y, z, geomDiffBytes, &numGeomDiffBits);
			}

			colorBytes[numColorBytes++] = encodeR;
			colorBytes[numColorBytes++] = encodeG;
			colorBytes[numColorBytes++] = encodeB;
		}
		else
		{
			if(wasFilled)
				splv_brick_diff_encode(SPLV_FALSE, x, y, z, geomDiffBytes, &numGeomDiffBits);
			else
				continue;
		}
	}

	//write:
	//---------------
	uint8_t encodingType = (uint8_t)SPLV_BRICK_ENCODING_TYPE_P;
	out.write((const char*)&encodingType, sizeof(uint8_t));
	
	uint8_t numGeomDiffEncoded = (uint8_t)numGeomDiff;
	out.write((const char*)&numGeomDiffEncoded, sizeof(uint8_t));

	out.write((const char*)geomDiffBytes, (numGeomDiffBits + 7) / 8);
	out.write((const char*)colorBytes, numColorBytes);

	return SPLV_SUCCESS;
}

//-------------------------------------------//

static inline void splv_brick_diff_encode(splv_bool_t add, uint32_t x, uint32_t y, uint32_t z, uint8_t* buf, uint32_t* bitIdx)
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