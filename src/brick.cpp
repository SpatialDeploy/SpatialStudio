#include "brick.hpp"
#include <memory>

#include "morton_lut.hpp"

//-------------------------------------------//

Brick::Brick()
{
	m_bitmap = std::unique_ptr<uint32_t[]>(new uint32_t[bitmap_len()]()); //0-initializes bitmap
	m_colors = std::unique_ptr<Color[]>(new Color[colors_len()]);
}

void Brick::set_voxel(uint32_t x, uint32_t y, uint32_t z, const Color& color)
{
	uint32_t idx = x + BRICK_SIZE * (y + BRICK_SIZE * z);
	m_bitmap[idx / 32] |= 1 << (idx % 32);
	m_colors[idx] = color;
}

void Brick::unset_voxel(uint32_t x, uint32_t y, uint32_t z)
{
	uint32_t idx = x + BRICK_SIZE * (y + BRICK_SIZE * z);
	m_bitmap[idx / 32] &= ~(1 << (idx % 32));
}

bool Brick::voxel_set(uint32_t x, uint32_t y, uint32_t z) const
{
	uint32_t idx = x + BRICK_SIZE * (y + BRICK_SIZE * z);
	return (m_bitmap[idx / 32] & (1 << (idx % 32))) != 0;
}

bool Brick::voxel_set(uint32_t idx) const
{
	return (m_bitmap[idx / 32] & (1 << (idx % 32))) != 0;
}

void Brick::serialize(std::ostream& out)
{
	uint32_t dummyVoxelCount;
	uint32_t dummySize;
	uint32_t dummySizeBitmap;
	uint32_t dummySizeColors;

	serialize_verbose(out, dummyVoxelCount, dummySize, dummySizeBitmap, dummySizeColors);
}

void Brick::serialize_verbose(std::ostream& out, uint32_t& voxCount, uint32_t& size, uint32_t& sizeBitmap, uint32_t& sizeColors)
{
	//initialize bytes:
	//---------------
	uint32_t voxelCount = 0;
	std::vector<uint8_t> bitmapBytes; //TODO: find more performent soln than a vector
	std::vector<uint8_t> colorBytes;

	uint8_t curBitmapByte; //for RLE
	if((m_bitmap[0] & 1) != 0)
		curBitmapByte = 0x80;
	else
		curBitmapByte = 0x00;

	//iterate over brick, perform RLE on bitmap and add colors:
	//---------------

	//we do RLE in morton order, we MUST make sure to read it back in the same order
	for(uint32_t i = 0; i < BRICK_SIZE * BRICK_SIZE * BRICK_SIZE; i++)
	{
		uint32_t x = MORTON_TO_COORDINATE[i].x;
		uint32_t y = MORTON_TO_COORDINATE[i].y;
		uint32_t z = MORTON_TO_COORDINATE[i].z;

		uint32_t idx = x + BRICK_SIZE * (y + BRICK_SIZE * z);
		uint32_t idxArr = idx / 32;
		uint32_t idxBit = idx % 32;

		//update RLE
		bool filled = (m_bitmap[idxArr] & (1u << idxBit)) != 0;

		if(filled != ((curBitmapByte & (1 << 7u)) != 0) || (curBitmapByte & 0x7f) == 127)
		{
			bitmapBytes.push_back(curBitmapByte);

			if(filled)
				curBitmapByte = 0x80;
			else
				curBitmapByte = 0x00;
		}

		curBitmapByte++;

		//add color if filled
		if(filled)
		{
			colorBytes.push_back(m_colors[idx].r);
			colorBytes.push_back(m_colors[idx].g);
			colorBytes.push_back(m_colors[idx].b);

			voxelCount++;
		}
	}

	bitmapBytes.push_back(curBitmapByte);

	//write:
	//---------------
	out.write((const char*)&voxelCount, sizeof(uint32_t));
	out.write((const char*)bitmapBytes.data(), bitmapBytes.size() * sizeof(uint8_t));
	out.write((const char*)colorBytes.data(), voxelCount * NUM_COLOR_COMPONENTS * sizeof(uint8_t));

	//output size info:
	//---------------
	voxCount = voxelCount;
	sizeBitmap = (uint32_t)bitmapBytes.size() * sizeof(uint8_t);
	sizeColors = voxelCount * NUM_COLOR_COMPONENTS * sizeof(uint8_t) + sizeof(uint32_t);
	size = sizeBitmap + sizeColors;
}

uint32_t Brick::bitmap_len()
{
	uint32_t brickLen = BRICK_SIZE * BRICK_SIZE * BRICK_SIZE;

	uint32_t bitmapLen = (brickLen + 31) & (~31); //align up to 32 bits (so fits in array of uint32s)
	bitmapLen /= 4; //4 bytes per uint
	bitmapLen /= 8; //8 bits per byte

	return bitmapLen;
}

uint32_t Brick::colors_len()
{
	return BRICK_SIZE * BRICK_SIZE * BRICK_SIZE;
}