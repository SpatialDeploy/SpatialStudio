#ifndef HELPER_H
#define HELPER_H

#include <stdint.h>

//-------------------------------------------//

struct Color
{
	uint8_t r;
	uint8_t g;
	uint8_t b;

	Color() : r(0), g(0), b(0) {}
	Color(uint8_t _r, uint8_t _g, uint8_t _b) : r(_r), g(_g), b(_b) {}
};

struct Coordinate
{
	uint32_t x;
	uint32_t y;
	uint32_t z;
};

struct Coordinate8
{
	uint8_t x;
	uint8_t y;
	uint8_t z;
};

#endif //#ifndef HELPER_H