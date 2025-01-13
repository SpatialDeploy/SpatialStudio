/* splv_vox_utils.h
 *
 * contains utility functions for MagicaVoxel files (.vox)
 */

#ifndef SPLV_VOX_UTILS_H
#define SPLV_VOX_UTILS_H

#include "splv_frame.h"
#include "splv_error.h"
#include "splv_global.h"

//-------------------------------------------//

/**
 * loads all frames from a .vox file with an animation, call splv_vox_frames_destroy() to free allocated memory
 * 
 * note that the dimensions of bbox must be multiples of SPLV_BRICK_SIZE
 * 
 * TODO: triple pointer :(
 */
SPLVerror splv_vox_load(const char* path, SPLVframe*** outFrames, uint32_t* numOutFrames, SPLVboundingBox* bbox);

/**
 * destroys all frames allocated from splv_vox_load()
 */
void splv_vox_frames_destroy(SPLVframe** frames, uint32_t numFrames);

/**
 * returns the maximum dimension out of all frames in a .vox file.
 * 
 * note that .vox files are z-up, so their y/z axes are swapped relative to splv
 */
SPLVerror splv_vox_get_max_dimensions(const char* path, uint32_t* width, uint32_t* height, uint32_t* depth);

#endif //#ifndef SPLV_VOX_UTILS_H