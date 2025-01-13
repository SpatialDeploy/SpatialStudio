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
SPLVerror splv_nvdb_load(const char* path, SPLVframe** outFrame, SPLVboundingBox* bbox, SPLVaxis lrAxis, SPLVaxis udAxis, SPLVaxis fbAxis);

#endif //#ifndef SPLV_NVDB_UTILS_H