/* splv_dyn_array.h
 *
 * contains functionality for dynamic arrays
 */

#ifndef SPLV_DYN_ARRAY_H
#define SPLV_DYN_ARRAY_H

#include "splv_error.h"
#include "splv_global.h"
#include "splv_log.h"
#include <stdint.h>

//-------------------------------------------//

/**
 * a dynamic array of uint64_t's
 */
typedef struct SPLVdynArrayUint64
{
	uint64_t cap;
	uint64_t* arr;

	uint64_t len;
} SPLVdynArrayUint64;

//-------------------------------------------//

inline SPLVerror splv_dyn_array_uint64_create(SPLVdynArrayUint64* arr, uint64_t initialCap)
{
	if(initialCap == 0)
	{
		const uint64_t DEFAULT_INITIAL_CAP = 64;
		initialCap = DEFAULT_INITIAL_CAP;
	}

	arr->len = 0;
	arr->cap = initialCap;
	arr->arr = (uint64_t*)SPLV_MALLOC(initialCap * sizeof(uint64_t));
	if(!arr->arr)
	{
		SPLV_LOG_ERROR("failed to alloc dynamic array");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	return SPLV_SUCCESS;
}

inline void splv_dyn_array_uint64_destroy(SPLVdynArrayUint64* arr)
{
	if(arr->arr)
		SPLV_FREE(arr->arr);
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
		{
			SPLV_LOG_ERROR("failed to realloc dynamic array");
			return SPLV_ERROR_OUT_OF_MEMORY;
		}

		arr->cap = newCap;
		arr->arr = newArr;
	}

	return SPLV_SUCCESS;
}

#endif //#ifndef SPLV_DYN_ARRAY_H