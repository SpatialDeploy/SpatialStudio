#include "brick.hpp"
#include <memory>

//-------------------------------------------//

Brick::Brick() : m_voxelCount(0)
{
	m_bitmap = std::unique_ptr<uint32_t[]>(new uint32_t[bitmap_len()]()); //0-initializes bitmap
	m_colors = {};
}

void Brick::set_voxel(uint32_t x, uint32_t y, uint32_t z, const Color& color)
{
	uint32_t packedColor = (color.r << 24) | (color.g << 16) | (color.b << 8) | color.a;

	uint32_t idx = x + BRICK_SIZE * (y + BRICK_SIZE * z);
	m_bitmap[idx / 32] |= 1 << (idx % 32);
	m_colors.push_back(packedColor);

	m_voxelCount++;
}

void Brick::serialize(std::ofstream& file)
{
	file.write((const char*)&m_voxelCount, sizeof(uint32_t));
	file.write((const char*)m_bitmap.get(), bitmap_len() * sizeof(uint32_t));
	file.write((const char*)m_colors.data(), m_voxelCount * sizeof(uint32_t));
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
	return bitmap_len() * sizeof(uint32_t);
}

uint32_t Brick::serialized_size_colors()
{
	return sizeof(uint32_t) + m_voxelCount * sizeof(uint32_t);
}

uint32_t Brick::bitmap_len()
{
	uint32_t brickLen = BRICK_SIZE * BRICK_SIZE * BRICK_SIZE;

	uint32_t bitmapLen = (brickLen + 31) & (~31); //align up to 32 bits (so fits in array of uint32s)
	bitmapLen /= 4; //4 bytes per uint
	bitmapLen /= 8; //8 bits per byte

	return bitmapLen;
}