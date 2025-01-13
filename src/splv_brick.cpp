#include "splv_brick.h"

#include "splv_morton_lut.h"

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

	//TODO: error checking
	out.write((const char*)&voxelCount, sizeof(uint32_t));
	out.write((const char*)bitmapBytes, numBitmapBytes * sizeof(uint8_t));
	out.write((const char*)colorBytes, numColorBytes * sizeof(uint8_t));

	return SPLV_SUCCESS;
}