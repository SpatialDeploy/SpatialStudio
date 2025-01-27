/**
 * splv_global.h
 * 
 * contains various constants and structs that are used globally
 */

#ifndef SPLV_GLOBAL_H
#define SPLV_GLOBAL_H

#include "splv_error.h"
#include <stdint.h>

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

//-------------------------------------------//

/**
 * a dynamic array of uint64_t's
 */
typedef struct SPLVdynArrayUint64
{
	uint64_t len;
	uint64_t cap;
	uint64_t* arr;
} SPLVdynArrayUint64;

inline SPLVerror splv_dyn_array_uint64_create(SPLVdynArrayUint64* arr, uint64_t initialCap)
{
	arr->len = 0;
	arr->cap = initialCap;
	arr->arr = (uint64_t*)SPLV_MALLOC(initialCap * sizeof(uint64_t));
	if(!arr->arr)
		return SPLV_ERROR_OUT_OF_MEMORY;

	return SPLV_SUCCESS;
}

inline void splv_dyn_array_uint64_destroy(SPLVdynArrayUint64 arr)
{
	if(arr.arr)
		SPLV_FREE(arr.arr);
}

inline SPLVerror splv_dyn_array_uint64_push(SPLVdynArrayUint64* arr, uint64_t val)
{
	arr->arr[arr->len] = val;
	arr->len++;

	if(arr->len >= arr->cap)
	{
		uint64_t newCap = arr->cap * 2;
		uint64_t* newArr = (uint64_t*)SPLV_REALLOC(arr->arr, newCap * sizeof(uint64_t));
		if(!newArr)
			return SPLV_ERROR_OUT_OF_MEMORY;

		arr->cap = newCap;
		arr->arr = newArr;
	}

	return SPLV_SUCCESS;
}

#endif //#ifndef SPLV_GLOBAL_H