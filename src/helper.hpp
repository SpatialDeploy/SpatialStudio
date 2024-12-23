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

#endif //#ifndef HELPER_H