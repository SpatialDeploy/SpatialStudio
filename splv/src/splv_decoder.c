#include "spatialstudio/splv_decoder.h"

#include "splv_range_coder.h"
#include "spatialstudio/splv_log.h"
#include <math.h>

//-------------------------------------------//

static SPLVerror _splv_decoder_create(SPLVdecoder* decoder);

static inline SPLVerror _splv_decoder_read(SPLVdecoder* decoder, uint64_t size, void* dst);
static inline SPLVerror _splv_decoder_seek(SPLVdecoder* decoder, uint64_t pos);

//-------------------------------------------//

SPLV_API SPLVerror splv_decoder_create_from_mem(SPLVdecoder* decoder, uint64_t encodedBufLen, uint8_t* encodedBuf)
{
	//initialize buffer reader:
	//---------------
	SPLVbufferReader reader;
	SPLV_ERROR_PROPAGATE(splv_buffer_reader_create(&reader, encodedBuf, encodedBufLen));

	//create decoder:
	//---------------
	memset(decoder, 0, sizeof(SPLVdecoder)); //clear any ptrs to NULL

	decoder->fromFile = 0;
	decoder->inBuf = reader;

	return _splv_decoder_create(decoder);
}

SPLV_API SPLVerror splv_decoder_create_from_file(SPLVdecoder* decoder, const char* path)
{
	//open file:
	//---------------
	FILE* file = fopen(path, "rb");
	if(!file)
	{
		SPLV_LOG_ERROR("failed to open input file for decoding");
		return SPLV_ERROR_FILE_OPEN;
	}

	//create decoder:
	//---------------
	memset(decoder, 0, sizeof(SPLVdecoder));

	decoder->fromFile = 1;
	decoder->inFile = file;

	return _splv_decoder_create(decoder);
}

SPLV_API SPLVerror splv_decoder_get_frame_dependencies(SPLVdecoder* decoder, uint64_t idx, uint64_t* numDependencies, uint64_t* dependencies, uint8_t recursive)
{
	//currently theres only a single-frame lookback

	SPLV_ASSERT(idx < decoder->frameCount, "out of bounds frame index");

	SPLVframeEncodingType encodingType = (SPLVframeEncodingType)(decoder->frameTable[idx] >> 56);
	if(encodingType == SPLV_FRAME_ENCODING_TYPE_I)
		*numDependencies = 0;
	else if(encodingType == SPLV_FRAME_ENCODING_TYPE_P)
	{
		if(idx == 0)
		{
			SPLV_LOG_ERROR("invalid SPLV file - first frame cannot be a p-frame");
			return SPLV_ERROR_INVALID_INPUT;
		}

		if(recursive)
		{
			int32_t prevIframe = splv_decoder_get_prev_i_frame_idx(decoder, idx);
			if(prevIframe < 0)
			{
				SPLV_LOG_ERROR("invalid SPLV file - first frame cannot be a p-frame");
				return SPLV_ERROR_INVALID_INPUT;
			}

			*numDependencies = idx - prevIframe;
			if(dependencies)
			{
				for(uint32_t i = prevIframe; i < idx; i++)
					dependencies[i - prevIframe] = i;
			}
		}
		else
		{
			*numDependencies = 1;
			if(dependencies)
				dependencies[0] = idx - 1;
		}
	}
	else
	{
		SPLV_LOG_ERROR("invalid SPLV file - unknown frame encoding type");
		return SPLV_ERROR_INVALID_INPUT;
	}

	return SPLV_SUCCESS;
}

SPLV_API SPLVerror splv_decoder_decode_frame(SPLVdecoder* decoder, uint64_t idx, uint64_t numDependencies, SPLVframeIndexed* dependencies, SPLVframe* frame)
{
	//validate:
	//-----------------
	SPLV_ASSERT(idx < decoder->frameCount, "out of bounds frame index");

	//get frame pointer:
	//-----------------
	uint64_t frameTableEntry = decoder->frameTable[idx];
	SPLVframeEncodingType encodingType = (SPLVframeEncodingType)(frameTableEntry >> 56);
	uint64_t framePtr = frameTableEntry & 0x00FFFFFFFFFFFFFF;

	uint8_t* compressedFrame;
	uint64_t compressedFrameLen;
	if(decoder->fromFile)
	{
		//TODO!!!!!!

		SPLV_LOG_ERROR("not yet implemented");
		return SPLV_ERROR_INTERNAL;
	}
	else
	{
		SPLV_ERROR_PROPAGATE(splv_buffer_reader_seek(&decoder->inBuf, framePtr));
		compressedFrame = decoder->inBuf.buf + decoder->inBuf.readPos;
		compressedFrameLen = decoder->inBuf.len - decoder->inBuf.readPos;
	}

	//ensure dependencies are present:
	//-----------------
	SPLVframe* lastFrame = NULL;
	uint8_t foundDependencies = 0;

	if(encodingType == SPLV_FRAME_ENCODING_TYPE_I)
		foundDependencies = 1;
	else if(encodingType == SPLV_FRAME_ENCODING_TYPE_P)
	{
		if(idx == 0)
		{
			SPLV_LOG_ERROR("invalid SPLV file - first frame cannot be a p-frame");
			return SPLV_ERROR_INVALID_INPUT;
		}

		for(uint32_t i = 0; i < numDependencies; i++)
		{
			if(dependencies[i].index == idx - 1)
			{
				lastFrame = dependencies[i].frame;
				foundDependencies = 1;
				break;
			}
		}
	}
	else
	{
		SPLV_LOG_ERROR("invalid SPLV file - unknown frame encoding type");
		return SPLV_ERROR_INVALID_INPUT;
	}

	//TODO change to an assert
	if(!foundDependencies)
	{
		SPLV_LOG_ERROR("neccesary dependencies were not supplied for decoding frame");
		return SPLV_ERROR_RUNTIME;
	}

	//decompress:
	//-----------------
	uint8_t* decompressedBuf;
	uint64_t decompressedSize;

	SPLVerror decodeError = splv_rc_decode(compressedFrameLen, compressedFrame, &decompressedBuf, &decompressedSize);
	if(decodeError != SPLV_SUCCESS)
	{
		SPLV_LOG_ERROR("error decompressing frame");
		return decodeError;
	}

	SPLVbufferReader decompressedReader;
	SPLVerror readerError = splv_buffer_reader_create(&decompressedReader, decompressedBuf, decompressedSize);
	if(readerError != SPLV_SUCCESS)
	{
		splv_rc_free_output_buf(decompressedBuf);

		SPLV_LOG_ERROR("failed to create decompressed frame reader");
		return readerError;
	}

	//read total number of bricks:
	//-----------------	
	uint32_t numBricks;
	SPLV_ERROR_PROPAGATE(splv_buffer_reader_read(&decompressedReader, sizeof(uint32_t), &numBricks));

	//create frame:
	//-----------------
	uint32_t mapWidth  = decoder->width  / SPLV_BRICK_SIZE;
	uint32_t mapHeight = decoder->height / SPLV_BRICK_SIZE;
	uint32_t mapDepth  = decoder->depth  / SPLV_BRICK_SIZE;

	SPLV_ERROR_PROPAGATE(splv_frame_create(
		frame,
		mapWidth,
		mapHeight,
		mapDepth,
		numBricks
	));

	//read compressed map, generate full map:
	//-----------------	
	SPLVerror readMapError = splv_buffer_reader_read(&decompressedReader, decoder->encodedMapLen * sizeof(uint32_t), decoder->scratchBufEncodedMap);
	if(readMapError != SPLV_SUCCESS)
	{
		splv_frame_destroy(frame);
		splv_rc_free_output_buf(decompressedBuf);

		SPLV_LOG_ERROR("failed to read encoded map from decompressed stream");
		return readMapError;
	}

	uint32_t curBrickIdx = 0;
	for(uint32_t x = 0; x < mapWidth ; x++)
	for(uint32_t y = 0; y < mapHeight; y++)
	for(uint32_t z = 0; z < mapDepth ; z++)
	{
		uint32_t idx = splv_frame_get_map_idx(frame, x, y, z);
		uint32_t idxArr = idx / 32;
		uint32_t idxBit = idx % 32;

		if((decoder->scratchBufEncodedMap[idxArr] & (1u << idxBit)) != 0)
		{
			decoder->scratchBufBrickPositions[curBrickIdx] = (SPLVcoordinate){ x, y, z };
			frame->map[idx] = curBrickIdx++;
		}
		else
			frame->map[idx] = SPLV_BRICK_IDX_EMPTY;
	}

	//read each brick:
	//-----------------	
	curBrickIdx = 0;
	for(uint32_t i = 0; i < numBricks; i++)
	{
		SPLVerror brickDecodeError = splv_brick_decode(
			&decompressedReader, 
			&frame->bricks[curBrickIdx], 
			decoder->scratchBufBrickPositions[i].x, 
			decoder->scratchBufBrickPositions[i].y,
			decoder->scratchBufBrickPositions[i].z, 
			lastFrame
		);

		if(brickDecodeError != SPLV_SUCCESS)
		{
			splv_frame_destroy(frame);
			splv_rc_free_output_buf(decompressedBuf);

			SPLV_LOG_ERROR("error while decoding brick");
			return brickDecodeError;
		}

		curBrickIdx++;
	}

	//cleanup + return:
	//-----------------
	splv_rc_free_output_buf(decompressedBuf);

	return SPLV_SUCCESS;
}

SPLV_API int64_t splv_decoder_get_prev_i_frame_idx(SPLVdecoder* decoder, uint64_t idx)
{
	SPLV_ASSERT(idx < decoder->frameCount, "out of bounds frame index");

	int32_t frameIdx = idx;
	SPLVframeEncodingType encodingType = (SPLVframeEncodingType)(decoder->frameTable[frameIdx] >> 56);

	while(encodingType != SPLV_FRAME_ENCODING_TYPE_I && frameIdx > 0)
	{
		frameIdx--;
		encodingType = (SPLVframeEncodingType)(decoder->frameTable[frameIdx] >> 56);
	}

	if(encodingType != SPLV_FRAME_ENCODING_TYPE_I)
		return -1;
	else
		return frameIdx;
}

SPLV_API int64_t splv_decoder_get_next_i_frame_idx(SPLVdecoder* decoder, uint64_t idx)
{
	SPLV_ASSERT(idx < decoder->frameCount, "out of bounds frame index");

	int32_t frameIdx = idx;
	SPLVframeEncodingType encodingType = (SPLVframeEncodingType)(decoder->frameTable[frameIdx] >> 56);

	while(encodingType != SPLV_FRAME_ENCODING_TYPE_I && frameIdx < decoder->frameCount - 1)
	{
		frameIdx++;
		encodingType = (SPLVframeEncodingType)(decoder->frameTable[frameIdx] >> 56);
	}

	if(encodingType != SPLV_FRAME_ENCODING_TYPE_I)
		return -1;
	else
		return frameIdx;
}

SPLV_API SPLVerror splv_decoder_destroy(SPLVdecoder* decoder)
{
	if(decoder->scratchBufEncodedMap)
		SPLV_FREE(decoder->scratchBufEncodedMap);
	if(decoder->scratchBufBrickPositions)
		SPLV_FREE(decoder->scratchBufBrickPositions);

	if(decoder->fromFile)
		fclose(decoder->inFile);
}

//-------------------------------------------//

static SPLVerror _splv_decoder_create(SPLVdecoder* decoder)
{
	//read header + validate:
	//-----------------
	SPLVfileHeader header;
	SPLVerror readHeaderError = _splv_decoder_read(decoder, sizeof(SPLVfileHeader), &header);
	if(readHeaderError != SPLV_SUCCESS)
	{
		splv_decoder_destroy(decoder);

		SPLV_LOG_ERROR("failed to read file header");
		return readHeaderError;
	}

	if(header.magicWord != SPLV_MAGIC_WORD)
	{
		splv_decoder_destroy(decoder);

		SPLV_LOG_ERROR("invalid SPLV file - mismatched magic word");
		return SPLV_ERROR_INVALID_INPUT;
	}

	if(header.version != SPLV_VERSION)
	{
		splv_decoder_destroy(decoder);

		SPLV_LOG_ERROR("invalid SPLV file - mismatched version");
		return SPLV_ERROR_INVALID_INPUT;
	}

	if(header.width == 0 || header.height == 0 || header.width == 0)
	{
		splv_decoder_destroy(decoder);

		SPLV_LOG_ERROR("invalid SPLV file - dimensions must be positive");
		return SPLV_ERROR_INVALID_INPUT;
	}

	if(header.width % SPLV_BRICK_SIZE > 0 || header.height % SPLV_BRICK_SIZE > 0 || header.width % SPLV_BRICK_SIZE > 0)
	{
		splv_decoder_destroy(decoder);

		SPLV_LOG_ERROR("invalid SPLV file - dimensions must be a multiple of SPLV_BRICK_SIZE");
		return SPLV_ERROR_INVALID_INPUT;
	}

	if(header.framerate <= 0.0f)
	{
		splv_decoder_destroy(decoder);

		SPLV_LOG_ERROR("invalid SPLV file - framerate must be positive");
		return SPLV_ERROR_INVALID_INPUT;
	}

	if(header.frameCount == 0)
	{
		splv_decoder_destroy(decoder);

		SPLV_LOG_ERROR("invalid SPLV file - framecount must be positive");
		return SPLV_ERROR_INVALID_INPUT;
	}

	if(fabsf(header.duration - ((float)header.frameCount / header.framerate)) > 0.1f)
	{
		header.duration = (float)header.frameCount / header.framerate;
		SPLV_LOG_WARNING("duration did not match framerate and frameCount - potentially invalid SPLV file");
	}

	//initialize struct:
	//-----------------
	decoder->width      = header.width;
	decoder->height     = header.height;
	decoder->depth      = header.depth;
	decoder->framerate  = header.framerate;
	decoder->frameCount = header.frameCount;
	decoder->duration   = header.duration;

	//read frame pointers:
	//-----------------
	decoder->frameTable = (uint64_t*)SPLV_MALLOC(decoder->frameCount * sizeof(uint64_t));
	if(!decoder->frameTable)
	{
		splv_decoder_destroy(decoder);

		SPLV_LOG_ERROR("failed to allocate frame table");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	SPLVerror frameTableSeekError = _splv_decoder_seek(decoder, header.frameTablePtr);
	if(frameTableSeekError != SPLV_SUCCESS)
	{
		splv_decoder_destroy(decoder);

		SPLV_LOG_ERROR("failed to seek to frame table");
		return frameTableSeekError;
	}

	SPLVerror frameTableReadError = _splv_decoder_read(decoder, decoder->frameCount * sizeof(uint64_t), decoder->frameTable);
	if(frameTableReadError != SPLV_SUCCESS)
	{
		splv_decoder_destroy(decoder);

		SPLV_LOG_ERROR("failed to read frame table");
		return frameTableReadError;
	}

	//preallocate space for compressed map + brick positions:
	//-----------------
	uint32_t mapWidth  = decoder->width  / SPLV_BRICK_SIZE;
	uint32_t mapHeight = decoder->height / SPLV_BRICK_SIZE;
	uint32_t mapDepth  = decoder->depth  / SPLV_BRICK_SIZE;

	uint64_t mapLen = mapWidth * mapHeight * mapDepth;
	uint64_t encodedMapLen = (mapLen + 31) & (~31); //round up to multiple of 32 (sizeof(uint32_t))
	encodedMapLen /= 4; //4 bytes per uint32_t
	encodedMapLen /= 8; //8 bits per byte

	decoder->encodedMapLen = encodedMapLen;

	decoder->scratchBufEncodedMap = (uint32_t*)SPLV_MALLOC(encodedMapLen * sizeof(uint32_t));
	decoder->scratchBufBrickPositions = (SPLVcoordinate*)SPLV_MALLOC(mapLen * sizeof(SPLVcoordinate));
	if(!decoder->scratchBufEncodedMap || !decoder->scratchBufBrickPositions)
	{
		splv_decoder_destroy(decoder);

		SPLV_LOG_ERROR("failed to allocate decoder scratch buffers");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	//return:
	//-----------------
	return SPLV_SUCCESS;
}

static inline SPLVerror _splv_decoder_read(SPLVdecoder* decoder, uint64_t size, void* dst)
{
	if(decoder->fromFile)
	{
		if(fread(dst, size, 1, decoder->inFile) < 1)
		{
			SPLV_LOG_ERROR("failed to read from file");
			return SPLV_ERROR_FILE_READ;
		}
		
		return SPLV_SUCCESS;
	}
	else
	{
		SPLV_ERROR_PROPAGATE(splv_buffer_reader_read(&decoder->inBuf, size, dst));
		return SPLV_SUCCESS;
	}
}

static inline SPLVerror _splv_decoder_seek(SPLVdecoder* decoder, uint64_t pos)
{
	if(decoder->fromFile)
	{
		if(fseek(decoder->inFile, pos, SEEK_SET) != 0)
		{
			SPLV_LOG_ERROR("failed to seek in file");
			return SPLV_ERROR_FILE_READ;
		}

		return SPLV_SUCCESS;
	}
	else
	{
		SPLV_ERROR_PROPAGATE(splv_buffer_reader_seek(&decoder->inBuf, pos));
		return SPLV_SUCCESS;
	}
}