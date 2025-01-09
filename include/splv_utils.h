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
SPLVerror splv_file_concat(uint32_t numPaths, const char** paths, const char* outPath);

/**
 * splits an splv file into parts with duration of splitLength seconds
 */
SPLVerror splv_file_split(const char* path, float splitLength, const char* outDir, uint32_t* numSplits);

#endif //#ifndef SPLV_UTILS_H