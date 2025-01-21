/* splv_encoder.h
 *
 * contains all data/functions related to encoding SPLVframes to .splv files
 */

#ifndef SPLV_ENCODER_H
#define SPLV_ENCODER_H

#include <ostream> //TODO: c-style files
#include "splv_frame.h"
#include "splv_global.h"

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
	SPLVdynArrayUint64 framePtrs;

	std::ofstream* outFile; //TODO: c-style file
} SPLVencoder;

/**
 * header containing all metadata in an splv file
 */
typedef struct SPLVfileHeader
{
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	float framerate;
	uint32_t frameCount;
	float duration;
	uint64_t frameTablePtr;
} SPLVfileHeader;

//-------------------------------------------//

/**
 * creates a new encoder, call splv_encoder_finish() or splv_encoder_abort() to free any resources
 */
SPLVerror splv_encoder_create(SPLVencoder** encoder, uint32_t width, uint32_t height, uint32_t depth, float framerate, const char* outPath);

/**
 * encodes a frame to the end of the encoded stream
 */
SPLVerror splv_encoder_encode_frame(SPLVencoder* encoder, SPLVframe* frame);

/**
 * finishes encoding, writing metadata to the file, frees any resources from splv_encoder_create()
 */
SPLVerror splv_encoder_finish(SPLVencoder* encoder);

/**
 * aborts the current encoding stream, frees any resources from splv_encoder_create()
 */
void splv_encoder_abort(SPLVencoder* encoder);

#endif //#ifndef SPLV_ENCODER_H