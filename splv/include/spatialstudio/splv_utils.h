/* splv_utils.h
 *
 * contains utility functions for manipulating SPLV files
 */

#ifndef SPLV_UTILS_H
#define SPLV_UTILS_H

#include "splv_error.h"
#include "splv_global.h"

//-------------------------------------------//

/**
 * concatenates a list of splv files into a single file, if they share the same dimensions + framerate
 */
SPLV_API SPLVerror splv_file_concat(uint32_t numPaths, const char** paths, const char* outPath, uint32_t gopSize);

/**
 * splits an splv file into parts with duration of splitLength seconds
 */
SPLV_API SPLVerror splv_file_split(const char* path, float splitLength, const char* outDir, uint32_t gopSize, uint32_t* numSplits);

/**
 * upgrades an splv file to the latest version
 */
SPLV_API SPLVerror splv_file_upgrade(const char* path, const char* outPath, uint32_t gopSize);

#endif //#ifndef SPLV_UTILS_H