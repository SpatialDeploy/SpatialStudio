/* splv_nvdb_utils.h
 *
 * contains utility functions for NanoVDB files (.nvdb)
 */

#ifndef SPLV_NVDB_UTILS_H
#define SPLV_NVDB_UTILS_H

#include "splv_frame.h"
#include "splv_error.h"
#include "splv_global.h"

//-------------------------------------------//

/**
 * loads a frame from a .nvdb file, call splv_frame_destroy() to free allocated memory
 */
SPLV_API SPLVerror splv_nvdb_load(const char* path, SPLVframe* outFrame, SPLVboundingBox* bbox, SPLVaxis lrAxis, SPLVaxis udAxis, SPLVaxis fbAxis);

/**
 * saves a frame into a .nvdb file. The voxels are written in the bounding box (0, 0, 0) to (width - 1, height - 1, depth - 1)
 */
SPLV_API SPLVerror splv_nvdb_save(SPLVframe* frame, const char* outPath);

#endif //#ifndef SPLV_NVDB_UTILS_H