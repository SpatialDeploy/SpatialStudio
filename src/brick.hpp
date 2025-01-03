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

	//sets a given voxel to be filled with a given color, all voxels ae empty by default
	void set_voxel(uint32_t x, uint32_t y, uint32_t z, const Color& color);
	//sets a given voxel to be empty
	void unset_voxel(uint32_t x, uint32_t y, uint32_t z);

	//checks whether a given voxel is set
	bool voxel_set(uint32_t x, uint32_t y, uint32_t z) const;
	bool voxel_set(uint32_t idx) const;

	//serializes the brick, performing any per-brick compression
	void serialize(std::ostream& out);
	void serialize_verbose(std::ostream& out, uint32_t& voxelCount, uint32_t& size, uint32_t& sizeBitmap, uint32_t& sizeColors);

private:
	std::unique_ptr<uint32_t[]> m_bitmap;
	std::unique_ptr<Color[]> m_colors;

	//uint32_t serialize_bitmap(std::ofstream* file);

	static uint32_t bitmap_len();
	static uint32_t colors_len();
};

#endif //#ifndef BRICK_H