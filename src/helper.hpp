#ifndef HELPER_H
#define HELPER_H

#include <stdint.h>

//-------------------------------------------//

struct Color
{
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;

	Color() : r(0), g(0), b(0), a(255) {}
	Color(uint8_t _r, uint8_t _g, uint8_t _b, uint8_t _a = 255) : r(_r), g(_g), b(_b), a(_a) {}
};

#endif //#ifndef HELPER_H