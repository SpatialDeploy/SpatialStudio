#include "brick.hpp"
#include <memory>

#include "morton_lut.hpp"

//-------------------------------------------//

Brick::Brick() : m_voxelCount(0)
{
	m_bitmap = std::unique_ptr<uint32_t[]>(new uint32_t[bitmap_len()]()); //0-initializes bitmap
	m_colors = {};
}

void Brick::add_voxel(uint32_t x, uint32_t y, uint32_t z, const Color& color)
{
	uint32_t idx = x + BRICK_SIZE * (y + BRICK_SIZE * z);
	m_bitmap[idx / 32] |= 1 << (idx % 32);
	
	m_colors.push_back(color.r);
	m_colors.push_back(color.g);
	m_colors.push_back(color.b);

	m_voxelCount++;
}

void Brick::serialize(std::ofstream& file)
{
	file.write((const char*)&m_voxelCount, sizeof(uint32_t));
	serialize_bitmap(&file);
	file.write((const char*)m_colors.data(), m_voxelCount * NUM_COLOR_COMPONENTS * sizeof(uint8_t));
}

uint32_t Brick::get_voxel_count()
{
	return m_voxelCount;
}

uint32_t Brick::serialized_size()
{
	return serialized_size_bitmap() + serialized_size_colors();
}

uint32_t Brick::serialized_size_bitmap()
{
	return serialize_bitmap(nullptr);
}

uint32_t Brick::serialized_size_colors()
{
	return sizeof(uint32_t) + m_voxelCount * NUM_COLOR_COMPONENTS * sizeof(uint8_t);
}

uint32_t Brick::serialize_bitmap(std::ofstream* file)
{
	//initialize bytes:
	//---------------

	//TODO: find more performent soln than a vector
	std::vector<uint8_t> bytes;

	uint8_t curByte;
	if((m_bitmap[0] & 1) != 0)
		curByte = 0x80;
	else
		curByte = 0x00;

	//calculate RLE:
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

		bool filled = (m_bitmap[idxArr] & (1u << idxBit)) != 0;

		if(filled != ((curByte & (1 << 7u)) != 0) || (curByte & 0x7f) == 127)
		{
			bytes.push_back(curByte);

			if(filled)
				curByte = 0x80;
			else
				curByte = 0x00;
		}

		curByte++;
	}

	bytes.push_back(curByte);

	//write bytes:
	//---------------
	if(file != nullptr)
		file->write((const char*)bytes.data(), bytes.size() * sizeof(uint8_t));

	//return:
	//---------------
	return (uint32_t)bytes.size() * sizeof(uint8_t);
}

uint32_t Brick::bitmap_len()
{
	uint32_t brickLen = BRICK_SIZE * BRICK_SIZE * BRICK_SIZE;

	uint32_t bitmapLen = (brickLen + 31) & (~31); //align up to 32 bits (so fits in array of uint32s)
	bitmapLen /= 4; //4 bytes per uint
	bitmapLen /= 8; //8 bits per byte

	return bitmapLen;
}