/* splv_encoder.h
 *
 * contains all data/functions related to encoding SPLVframes to .splv files
 */

#ifndef SPLV_ENCODER_H
#define SPLV_ENCODER_H

#include "splv_frame.h"
#include "splv_global.h"
#include "splv_dyn_array.h"
#include "splv_format.h"
#include <stdio.h>

//-------------------------------------------//

/**
 * all state needed by an encoder
 */
typedef struct SPLVencoder
{
	uint32_t width;
	uint32_t height;
	uint32_t depth;

	float framerate;
	uint32_t frameCount;
	SPLVdynArrayUint64 frameTable;

	uint32_t gopSize;
	SPLVframe lastFrame;

	FILE* outFile;

	SPLVbufferWriter frameWriter;
	SPLVbufferWriter encodedFrameWriter;

	uint64_t mapBitmapLen;
	uint32_t* scratchBufMapBitmap;
	SPLVbrick** scratchBufBricks;
	SPLVcoordinate* scratchBufBrickPositions;
} SPLVencoder;

//-------------------------------------------//

/**
 * creates a new encoder, call splv_encoder_finish() or splv_encoder_abort() to free any resources
 */
SPLV_API SPLVerror splv_encoder_create(SPLVencoder* encoder, uint32_t width, uint32_t height, uint32_t depth, float framerate, uint32_t gopSize, const char* outPath);

/**
 * encodes a frame to the end of the encoded stream. You MUST keep any encoded frames in memory until
 * either this function sets canFree to SPLV_TRUE, or splv_encoder_finish() is called. Once one of these
 * conditions is met you can free all frames encoded thus far.
 */
SPLV_API SPLVerror splv_encoder_encode_frame(SPLVencoder* encoder, SPLVframe* frame, splv_bool_t* canFree);

/**
 * finishes encoding, writing metadata to the file, frees any resources from splv_encoder_create()
 */
SPLV_API SPLVerror splv_encoder_finish(SPLVencoder* encoder);

/**
 * aborts the current encoding stream, frees any resources from splv_encoder_create()
 */
SPLV_API void splv_encoder_abort(SPLVencoder* encoder);

#endif //#ifndef SPLV_ENCODER_H