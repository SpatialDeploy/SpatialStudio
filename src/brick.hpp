#ifndef BRICK_H
#define BRICK_H

#include "helper.hpp"
#include <fstream>
#include <memory>
#include <vector>

//-------------------------------------------//

#define BRICK_SIZE 8
#define EMPTY_BRICK UINT32_MAX

#define NUM_COLOR_COMPONENTS 3

//-------------------------------------------//

class Brick
{
public:
	Brick();

	//NOTE: colors are only stored for set voxels, and are stored in the order that the voxels are set
	//make sure you call this function in an order that makes sense
	void set_voxel(uint32_t x, uint32_t y, uint32_t z, const Color& color);
	void serialize(std::ofstream& file);

	uint32_t get_voxel_count();
	uint32_t serialized_size();

	//for additional data collection
	uint32_t serialized_size_bitmap();
	uint32_t serialized_size_colors();

private:
	std::unique_ptr<uint32_t[]> m_bitmap;
	std::vector<uint8_t> m_colors;
	uint32_t m_voxelCount;

	uint32_t serialize_bitmap(std::ofstream* file);

	static uint32_t bitmap_len();
};

#endif //#ifndef BRICK_H