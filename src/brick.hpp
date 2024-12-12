#ifndef BRICK_H
#define BRICK_H

#include <memory>

#include "helper.hpp"
#include <fstream>

//-------------------------------------------//

#define BRICK_SIZE 8
#define EMPTY_BRICK UINT32_MAX

//-------------------------------------------//

class Brick
{
public:
	Brick();

	void set_voxel(uint32_t x, uint32_t y, uint32_t z, const Color& color);
	void serialize(std::ofstream& file);

	uint32_t get_voxel_count();

	static uint32_t serialized_size();

private:
	std::unique_ptr<uint32_t[]> m_bitmap;
	std::unique_ptr<uint32_t[]> m_colors;
	uint32_t m_voxelCount;

	static uint32_t bitmap_len();
	static uint32_t colors_len();
};

#endif //#ifndef BRICK_H