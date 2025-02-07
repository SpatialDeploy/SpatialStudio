/* splv_decoder.h
 *
 * contains all data/functions related to decoding .splv files to SPLVframes
 */

#ifndef SPLV_DECODER_H
#define SPLV_DECODER_H

#include "splv_frame.h"
#include "splv_global.h"
#include "splv_format.h"
#include "splv_buffer_io.h"

//-------------------------------------------//

/**
 * all state needed by a decoder
 */
typedef struct SPLVdecoder
{
	uint32_t width;
	uint32_t height;
	uint32_t depth;

	float framerate;
	uint32_t frameCount;
	float duration;

	uint64_t* frameTable;

	uint8_t fromFile;
	union
	{
		SPLVbufferReader inBuf;
		struct
		{
			FILE* file;

			uint64_t scrathBufLen;
			uint8_t* scratchBuf;
		} inFile;
	};

	SPLVbufferWriter decodedFrameWriter;

	uint64_t encodedMapLen;
	uint32_t* scratchBufEncodedMap;
	SPLVcoordinate* scratchBufBrickPositions;
} SPLVdecoder;

/**
 * a frame paired with an index into a stream
 */
typedef struct SPLVframeIndexed
{
	uint64_t index;
	SPLVframe* frame;
} SPLVframeIndexed;

//-------------------------------------------//

/**
 * creates a new decoder from a memory buffer, reading and verifying metadata. call splv_decoder_destroy() to free any resources
 */
SPLV_API SPLVerror splv_decoder_create_from_mem(SPLVdecoder* decoder, uint64_t encodedBufLen, uint8_t* encodedBuf);

/**
 * creates a new decoder from a file, reading and verifying metadata. call splv_decoder_destroy() to free any resources
 */
SPLV_API SPLVerror splv_decoder_create_from_file(SPLVdecoder* decoder, const char* path);

/**
 * returns the required denendant frames for decoding a given frame. These are the frames that must be passed to
 * splv_decoder_decode_frame(). Passing NULL for dependencies will just return the length, allowing you to allocate the properly size array.abort
 * 
 * to help with memory management, if a frame at index i is NOT a dependency for decoding frame j, for j>i, then frame
 * i wont be a dependency for ANY k>j. Thus, once a frame isn't a dependency, you can free it and still safely decode subsequent frames
 * 
 * if recursive == 1, the list of all frames needed to be decoded, in topological order, before decoding the given frame is returned.
 * For example if frame 2 depenends on 1, and 1 on 0. Then splv_decoder_get_frame_dependencies(2) would return [0,1]
 */
SPLV_API SPLVerror splv_decoder_get_frame_dependencies(SPLVdecoder* decoder, uint64_t idx, uint64_t* numDependencies, uint64_t* dependencies, uint8_t recursive);

/**
 * decoes a given frame. The decoded frame's ownership is transferred to the caller. All of the dependenceis returned by
 * splv_decoder_get_frame_dependencies() MUST be supplied, or decoding will not be possible.
 */
SPLV_API SPLVerror splv_decoder_decode_frame(SPLVdecoder* decoder, uint64_t index, uint64_t numDependencies, SPLVframeIndexed* dependencies, SPLVframe* frame);

/**
 * returns the frame index of the first i-frame before or at the given index
 */
SPLV_API int64_t splv_decoder_get_prev_i_frame_idx(SPLVdecoder* decoder, uint64_t idx);

/**
 * returns the frame index of the next i-frame after or at the given index
 */
SPLV_API int64_t splv_decoder_get_next_i_frame_idx(SPLVdecoder* decoder, uint64_t idx);

/**
 * destroys a decoder, freeing any resources allocated from splv_decoder_create_*()
 */
SPLV_API void splv_decoder_destroy(SPLVdecoder* decoder);

#endif //#ifndef SPLV_DECODER_H