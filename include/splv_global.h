/**
 * splv_global.h
 * 
 * contains various constants and structs that are used globally
 */

#ifndef SPLV_GLOBAL_H
#define SPLV_GLOBAL_H

#include <stdint.h>

//-------------------------------------------//

#ifdef __cplusplus
	#define SPLV_NOMANGLE extern "C"
#else
	#define SPLV_NOMANGLE
#endif

#ifdef _WIN32
	#define SPLV_API SPLV_NOMANGLE __declspec(dllexport)
#else
    #define SPLV_API SPLV_NOMANGLE __attribute__((visibility("default")))
#endif

//-------------------------------------------//

#if !defined(SPLV) || !defined(SPLV_FREE) || !defined(QOBJ_FREE)
	#include <stdlib.h>

	#define SPLV_MALLOC(s) malloc(s)
	#define SPLV_FREE(p) free(p)
	#define SPLV_REALLOC(p, s) realloc(p, s)
#endif

//-------------------------------------------//

typedef uint8_t splv_bool_t;

#define SPLV_TRUE 1
#define SPLV_FALSE 0

//-------------------------------------------//

/**
 * a 3d coordinate
 */
typedef struct SPLVcoordinate
{
	uint32_t x;
	uint32_t y;
	uint32_t z;
} SPLVcoordinate;

/**
 * a 3d bounding box
 */
typedef struct SPLVboundingBox
{
	int32_t xMin;
	int32_t yMin;
	int32_t zMin;
	int32_t xMax;
	int32_t yMax;
	int32_t zMax;
} SPLVboundingBox;

/**
 * a 3d axis
 */
typedef enum SPLVaxis
{
	SPLV_AXIS_X = 0,
	SPLV_AXIS_Y = 1,
	SPLV_AXIS_Z = 2
} SPLVaxis;

#endif //#ifndef SPLV_GLOBAL_H