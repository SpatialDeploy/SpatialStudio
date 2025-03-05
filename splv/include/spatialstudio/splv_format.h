/* splv_format.h
 *
 * contains definitions/constants related to the .splv file format
 */

#ifndef SPLV_FORMAT_H
#define SPLV_FORMAT_H

#include <stdint.h>
#include "splv_error.h"
#include "splv_global.h"

//-------------------------------------------//

#define SPLV_MAKE_VERSION(major, minor, patch, subpatch) (((major) << 24) | ((minor) << 16) | ((patch) << 8) | (subpatch))

#define SPLV_MAGIC_WORD (('s' << 24) | ('p' << 16) | ('l' << 8) | ('v'))
#define SPLV_VERSION (SPLV_MAKE_VERSION(0, 2, 1, 0))

//-------------------------------------------//

/**
 * parameters used to control the encoding of SPLV
 */
typedef struct SPLVencodingParams
{
	uint32_t gopSize;
	uint32_t maxBrickGroupSize;
	splv_bool_t motionVectors;
} SPLVencodingParams;

/**
 * header containing all metadata in an splv file
 */
typedef struct SPLVfileHeader
{
	uint32_t magicWord;
	uint32_t version;

	uint32_t width;
	uint32_t height;
	uint32_t depth;

	float framerate;
	uint32_t frameCount;
	float duration;
	
	SPLVencodingParams encodingParams;

	uint64_t frameTablePtr;
} SPLVfileHeader;

/**
 * different types of frame encodings
 */
typedef enum SPLVframeEncodingType
{
	SPLV_FRAME_ENCODING_TYPE_I = 0,
	SPLV_FRAME_ENCODING_TYPE_P = 1
} SPLVframeEncodingType;

#endif //#ifndef SPLV_FORMAT_H