/* splv_utils.h
 *
 * contains utility functions for manipulating SPLV files
 */

#ifndef SPLV_UTILS_H
#define SPLV_UTILS_H

#include "splv_error.h"
#include "splv_global.h"
#include "splv_format.h"

//-------------------------------------------//

/**
 * all metadata encoded into an SPLV file
 */
typedef struct SPLVmetadata
{
	uint32_t width;
	uint32_t height;
	uint32_t depth;

	float framerate;
	uint32_t frameCount;
	float duration;

	SPLVencodingParams encodingParams;
} SPLVmetadata;

//-------------------------------------------//

/**
 * concatenates a list of splv files into a single file, if they share the same dimensions + framerate
 */
SPLV_API SPLVerror splv_file_concat(uint32_t numPaths, const char** paths, const char* outPath);

/**
 * splits an splv file into parts with duration of splitLength seconds
 */
SPLV_API SPLVerror splv_file_split(const char* path, float splitLength, const char* outDir, uint32_t* numSplits);

/**
 * upgrades an splv file to the latest version
 */
SPLV_API SPLVerror splv_file_upgrade(const char* path, const char* outPath);

/**
 * gets metadata from an SPLV file
 */
SPLV_API SPLVerror splv_file_get_metadata(const char* path, SPLVmetadata* metadata);

#endif //#ifndef SPLV_UTILS_H