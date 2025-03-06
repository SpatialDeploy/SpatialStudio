#include "spatialstudio/splv_decoder.h"

#include "spatialstudio/splv_range_coder.h"
#include "spatialstudio/splv_log.h"
#include <math.h>

//-------------------------------------------//

#ifndef __EMSCRIPTEN__
	#define SPLV_DECODER_MULTITHREADING
#endif

#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))

//-------------------------------------------//

/**
 * all info needed for a thread to decode a brick group
 */
typedef struct SPLVbrickGroupDecodeInfo
{
	SPLVdecoder* decoder;

	SPLVframe* outFrame;
	SPLVframeCompact* outFrameCompact;
	
	uint64_t compressedBufLen;
	uint8_t* compressedBuf;
	
	uint32_t brickStartIdx;
	uint32_t numBricks;
	
	uint64_t voxelsStartIdx;
	uint64_t numVoxels;

	SPLVframe* lastFrame;
} SPLVbrickGroupDecodeInfo;

//-------------------------------------------//

static SPLVerror _splv_decoder_create(SPLVdecoder* decoder);

static SPLVerror _splv_decoder_decode_brick_group(void* info);

static inline SPLVerror _splv_decoder_read(SPLVdecoder* decoder, uint64_t size, void* dst);
static inline SPLVerror _splv_decoder_seek(SPLVdecoder* decoder, uint64_t pos);

//-------------------------------------------//

SPLVerror splv_decoder_create_from_mem(SPLVdecoder* decoder, uint64_t encodedBufLen, uint8_t* encodedBuf)
{
	//initialize:
	//---------------
	memset(decoder, 0, sizeof(SPLVdecoder)); //clear any ptrs to NULL

	decoder->fromFile = 0;

	//create buffer reader:
	//---------------
	SPLV_ERROR_PROPAGATE(splv_buffer_reader_create(&decoder->inBuf, encodedBuf, encodedBufLen));

	//create general decoder:
	//---------------
	return _splv_decoder_create(decoder);
}

SPLVerror splv_decoder_create_from_file(SPLVdecoder* decoder, const char* path)
{
	//initialize:
	//---------------
	memset(decoder, 0, sizeof(SPLVdecoder)); //clear any ptrs to NULL

	decoder->fromFile = 1;

	//open file:
	//---------------
	decoder->inFile.file = fopen(path, "rb");
	if(!decoder->inFile.file)
	{
		SPLV_LOG_ERROR("failed to open input file for decoding");
		return SPLV_ERROR_FILE_OPEN;
	}

	//create scratch buffers:
	//---------------
	const uint64_t INITIAL_SCRATCH_BUF_LEN = 1024;
	decoder->inFile.scrathBufLen = INITIAL_SCRATCH_BUF_LEN;

	decoder->inFile.scratchBuf = (uint8_t*)SPLV_MALLOC(decoder->inFile.scrathBufLen);
	if(!decoder->inFile.scratchBuf)
	{
		fclose(decoder->inFile.file);

		SPLV_LOG_ERROR("failed to allocate decoder file scratch buffer");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	//create general decoder:
	//---------------
	return _splv_decoder_create(decoder);
}

SPLVerror splv_decoder_get_frame_dependencies(SPLVdecoder* decoder, uint64_t idx, uint64_t* numDependencies, uint64_t* dependencies, uint8_t recursive)
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
			int64_t prevIframe = splv_decoder_get_prev_i_frame_idx(decoder, idx);
			if(prevIframe < 0)
			{
				SPLV_LOG_ERROR("invalid SPLV file - first frame cannot be a p-frame");
				return SPLV_ERROR_INVALID_INPUT;
			}

			*numDependencies = idx - prevIframe;
			if(dependencies)
			{
				for(uint64_t i = prevIframe; i < idx; i++)
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

SPLVerror splv_decoder_decode_frame(SPLVdecoder* decoder, uint64_t idx, uint64_t numDependencies, SPLVframeIndexed* dependencies, SPLVframe* frame, SPLVframeCompact* compactFrame)
{
	//validate:
	//-----------------
	SPLV_ASSERT(idx < decoder->frameCount, "out of bounds frame index");

	//get frame pointer:
	//-----------------
	uint64_t frameTableEntry = decoder->frameTable[idx];
	SPLVframeEncodingType encodingType = (SPLVframeEncodingType)(frameTableEntry >> 56);
	uint64_t framePtr = frameTableEntry & 0x00FFFFFFFFFFFFFF;

	//read compressed frame data:
	//-----------------
	uint8_t* compressedFrame;
	uint64_t compressedFrameLen;
	if(decoder->fromFile)
	{
		//get ptr to next frame
		uint64_t nextFramePtr;
		if(idx == decoder->frameCount - 1)
		{
			if(fseek(decoder->inFile.file, 0, SEEK_END) != 0)
			{
				SPLV_LOG_ERROR("failed to seek to end of file");
				return SPLV_ERROR_FILE_READ;
			}

			long fileSize = ftell(decoder->inFile.file);
			if(fileSize == -1)
			{
				SPLV_LOG_ERROR("failed to get file size");
				return SPLV_ERROR_FILE_READ;
			}

			nextFramePtr = fileSize - (decoder->frameCount * sizeof(uint64_t));
		}
		else
		{
			uint64_t nextFrameTableEntry = decoder->frameTable[idx + 1];
			nextFramePtr = nextFrameTableEntry & 0x00FFFFFFFFFFFFFF;
		}

		//potentially resize scratch buffer
		compressedFrameLen = nextFramePtr - framePtr;
		if(compressedFrameLen > decoder->inFile.scrathBufLen)
		{
			uint64_t newScratchBufLen = decoder->inFile.scrathBufLen;
			while(compressedFrameLen > newScratchBufLen)
				newScratchBufLen *= 2;

			uint8_t* newScatchBuf = (uint8_t*)SPLV_REALLOC(decoder->inFile.scratchBuf, newScratchBufLen);
			if(!newScatchBuf)
			{
				SPLV_LOG_ERROR("failed to realloc decoder file scratch buf");
				return SPLV_ERROR_OUT_OF_MEMORY;
			}

			decoder->inFile.scratchBuf = newScatchBuf;
			decoder->inFile.scrathBufLen = newScratchBufLen;
		}

		//get ptrs to compressed frame
		SPLV_ERROR_PROPAGATE(_splv_decoder_seek(decoder, framePtr));
		SPLV_ERROR_PROPAGATE(_splv_decoder_read(decoder, compressedFrameLen, decoder->inFile.scratchBuf));
		compressedFrame = decoder->inFile.scratchBuf;
	}
	else
	{
		SPLV_ERROR_PROPAGATE(_splv_decoder_seek(decoder, framePtr));
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

	//create compressed reader, read num bricks:
	//-----------------
	SPLVbufferReader compressedReader;
	SPLV_ERROR_PROPAGATE(splv_buffer_reader_create(&compressedReader, compressedFrame, compressedFrameLen));

	uint32_t numBricks;
	SPLV_ERROR_PROPAGATE(splv_buffer_reader_read(&compressedReader, sizeof(uint32_t), &numBricks));

	uint64_t numVoxels;
	SPLV_ERROR_PROPAGATE(splv_buffer_reader_read(&compressedReader, sizeof(uint64_t), &numVoxels));

	if(compactFrame && numVoxels > UINT32_MAX)
	{
		SPLV_LOG_ERROR("too many voxels to fit in SPLVframeCompact, more than UINT32_MAX");
		return SPLV_ERROR_INVALID_INPUT;
	}

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

	if(compactFrame)
	{
		SPLVerror compactFrameError = splv_frame_compact_create(
			compactFrame,
			mapWidth,
			mapHeight,
			mapDepth,
			numBricks,
			numVoxels
		);

		if(compactFrameError != SPLV_SUCCESS)
		{
			splv_frame_destroy(frame);
			return compactFrameError;
		}
	}

	//read compressed map, generate full map:
	//-----------------	
	SPLVerror readMapError = splv_buffer_reader_read(
		&compressedReader, 
		decoder->encodedMapLen * sizeof(uint32_t), decoder->scratchBufEncodedMap
	);
	if(readMapError != SPLV_SUCCESS)
	{
		splv_frame_destroy(frame);
		if(compactFrame)
			splv_frame_compact_destroy(compactFrame);

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
			
			frame->map[idx] = curBrickIdx;
			if(compactFrame)
				compactFrame->map[idx] = curBrickIdx;

			curBrickIdx++;
		}
		else
		{
			frame->map[idx] = SPLV_BRICK_IDX_EMPTY;
			if(compactFrame)
				compactFrame->map[idx] = SPLV_BRICK_IDX_EMPTY;
		}
	}

	//sanity check
	if(curBrickIdx != numBricks)
	{
		splv_frame_destroy(frame);
		if(compactFrame)
			splv_frame_compact_destroy(compactFrame);

		SPLV_LOG_ERROR("invalid SPLV file - given number of bricks did not match contents of map");
		return SPLV_ERROR_INVALID_INPUT;
	}

	//decode each brick group:
	//-----------------
	uint32_t maxBrickGroupSize;
	if(decoder->encodingParams.maxBrickGroupSize == 0)
		maxBrickGroupSize = max(numBricks, 1);
	else
		maxBrickGroupSize = decoder->encodingParams.maxBrickGroupSize;

	uint32_t numBrickGroups = (numBricks + maxBrickGroupSize - 1) / maxBrickGroupSize;
	uint32_t baseBrickGroupSize      = numBricks / max(numBrickGroups, 1);
	uint32_t brickGroupSizeRemainder = numBricks % max(numBrickGroups, 1);

	uint8_t* brickGroupsStart = compressedReader.buf + compressedReader.readPos + numBrickGroups * (2 * sizeof(uint64_t));
	uint64_t brickGroupsLen   = compressedReader.len - compressedReader.readPos - numBrickGroups * (2 * sizeof(uint64_t));

	uint64_t sumVoxelsGroup = 0;

	for(uint32_t i = 0; i < numBrickGroups; i++)
	{
		uint32_t startBrick = i * baseBrickGroupSize + min(i, brickGroupSizeRemainder);
		uint32_t numBricks = baseBrickGroupSize + (i < brickGroupSizeRemainder ? 1 : 0);

		uint64_t offset;
		SPLVerror readOffsetError = splv_buffer_reader_read(&compressedReader, sizeof(uint64_t), &offset);
		if(readOffsetError != SPLV_SUCCESS)
		{
			splv_frame_destroy(frame);
			if(compactFrame)
				splv_frame_compact_destroy(compactFrame);

			SPLV_LOG_ERROR("failed to read brick group offset from decompressed stream");
			return readOffsetError;
		}

		uint64_t numVoxelsGroup;
		SPLVerror readNumVoxelsError = splv_buffer_reader_read(&compressedReader, sizeof(uint64_t), &numVoxelsGroup);
		if(readNumVoxelsError != SPLV_SUCCESS)
		{
			splv_frame_destroy(frame);
			if(compactFrame)
				splv_frame_compact_destroy(compactFrame);

			SPLV_LOG_ERROR("failed to read brick group voxel count from decompressed stream");
			return readNumVoxelsError;
		}

		uint8_t* groupBuf = brickGroupsStart + offset;
		uint64_t groupSize = brickGroupsLen - offset;

		SPLVbrickGroupDecodeInfo decodeInfo;
		decodeInfo.decoder = decoder;
		decodeInfo.outFrame = frame;
		decodeInfo.outFrameCompact = compactFrame;
		decodeInfo.compressedBufLen = groupSize;
		decodeInfo.compressedBuf = groupBuf;
		decodeInfo.brickStartIdx = startBrick;
		decodeInfo.numBricks = numBricks;
		decodeInfo.lastFrame = lastFrame;
		decodeInfo.voxelsStartIdx = sumVoxelsGroup;
		decodeInfo.numVoxels = numVoxelsGroup;

	#ifdef SPLV_DECODER_MULTITHREADING
		SPLVerror addWorkError = splv_thread_pool_add_work(decoder->threadPool, &decodeInfo);
		if(addWorkError != SPLV_SUCCESS)
		{
			splv_frame_destroy(frame);
			if(compactFrame)
				splv_frame_compact_destroy(compactFrame);

			SPLV_LOG_ERROR("failed to add work to thread pool");
			return addWorkError;
		}
	#else
		_splv_decoder_decode_brick_group(decodeInfo);
	#endif

		sumVoxelsGroup += numVoxelsGroup;
	}

	if(sumVoxelsGroup != numVoxels)
	{
		splv_frame_destroy(frame);
		if(compactFrame)
			splv_frame_compact_destroy(compactFrame);

		SPLV_LOG_ERROR("sum of group voxel counts did not match given given voxel count");
		return SPLV_ERROR_INVALID_INPUT;
	}

	//wait for all threads to exit:
	//-----------------
#ifdef SPLV_DECODER_MULTITHREADING
	SPLVerror waitError = splv_thread_pool_wait(decoder->threadPool);
	if(waitError != SPLV_SUCCESS)
	{
		splv_frame_destroy(frame);
		if(compactFrame)
			splv_frame_compact_destroy(compactFrame);

		SPLV_LOG_ERROR("failed to wait on thread pool");
		return waitError;
	}
#endif

	//return:
	//-----------------
	return SPLV_SUCCESS;
}

int64_t splv_decoder_get_prev_i_frame_idx(SPLVdecoder* decoder, uint64_t idx)
{
	SPLV_ASSERT(idx < decoder->frameCount, "out of bounds frame index");

	int64_t frameIdx = idx;
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

int64_t splv_decoder_get_next_i_frame_idx(SPLVdecoder* decoder, uint64_t idx)
{
	SPLV_ASSERT(idx < decoder->frameCount, "out of bounds frame index");

	int64_t frameIdx = idx;
	SPLVframeEncodingType encodingType = (SPLVframeEncodingType)(decoder->frameTable[frameIdx] >> 56);

	while(encodingType != SPLV_FRAME_ENCODING_TYPE_I && frameIdx < (int64_t)decoder->frameCount - 1)
	{
		frameIdx++;
		encodingType = (SPLVframeEncodingType)(decoder->frameTable[frameIdx] >> 56);
	}

	if(encodingType != SPLV_FRAME_ENCODING_TYPE_I)
		return -1;
	else
		return frameIdx;
}

void splv_decoder_destroy(SPLVdecoder* decoder)
{
#ifdef SPLV_DECODER_MULTITHREADING
	if(decoder->threadPool)
		splv_thread_pool_destroy(decoder->threadPool);
#endif

	if(decoder->scratchBufEncodedMap)
		SPLV_FREE(decoder->scratchBufEncodedMap);
	if(decoder->scratchBufBrickPositions)
		SPLV_FREE(decoder->scratchBufBrickPositions);

	if(decoder->fromFile)
	{
		fclose(decoder->inFile.file);
		SPLV_FREE(decoder->inFile.scratchBuf);
	}
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

	if(header.encodingParams.gopSize == 0)
		SPLV_LOG_WARNING("invalid GOP size - not neccesary for decoding, but indicates corrupt data");

	if(fabsf(header.duration - ((float)header.frameCount / header.framerate)) > 0.1f)
	{
		header.duration = (float)header.frameCount / header.framerate;
		SPLV_LOG_WARNING("duration did not match framerate and frameCount - potentially invalid SPLV file");
	}

	//initialize struct:
	//-----------------
	decoder->width          = header.width;
	decoder->height         = header.height;
	decoder->depth          = header.depth;
	decoder->framerate      = header.framerate;
	decoder->frameCount     = header.frameCount;
	decoder->duration       = header.duration;
	decoder->encodingParams = header.encodingParams;

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

	//initialize thread pool:
	//-----------------
#ifdef SPLV_DECODER_MULTITHREADING
	SPLVerror threadPoolError = splv_thread_pool_create(
		&decoder->threadPool, SPLV_DECODER_THREAD_POOL_SIZE, 
		_splv_decoder_decode_brick_group, sizeof(SPLVbrickGroupDecodeInfo)
	);
	if(threadPoolError != SPLV_SUCCESS)
	{
		decoder->threadPool = NULL;
		SPLV_FREE(decoder->threadPool);
		splv_decoder_destroy(decoder);

		SPLV_LOG_ERROR("failed to create decoder thread pool");
		return threadPoolError;
	}
#endif

	//return:
	//-----------------
	return SPLV_SUCCESS;
}

//-------------------------------------------//

static SPLVerror _splv_decoder_decode_brick_group(void* arg)
{
	//get info:
	//-----------------
	SPLVbrickGroupDecodeInfo* info = (SPLVbrickGroupDecodeInfo*)arg;

	//create decompressed buffer writer:
	//-----------------
	SPLVbufferWriter decompressedWriter;
	SPLVerror writerError = splv_buffer_writer_create(&decompressedWriter, 0);
	if(writerError != SPLV_SUCCESS)
		return writerError;

	//decompress:
	//-----------------
	SPLVerror decodeError = splv_rc_decode(info->compressedBufLen, info->compressedBuf, &decompressedWriter);
	if(decodeError != SPLV_SUCCESS)
	{
		splv_buffer_writer_destroy(&decompressedWriter);

		SPLV_LOG_ERROR("error decompressing frame");
		return decodeError;
	}

	SPLVbufferReader decompressedReader;
	SPLVerror readerError = splv_buffer_reader_create(&decompressedReader, decompressedWriter.buf, decompressedWriter.writePos);
	if(readerError != SPLV_SUCCESS)
	{
		splv_buffer_writer_destroy(&decompressedWriter);

		SPLV_LOG_ERROR("failed to create decompressed frame reader");
		return readerError;
	}

	//read each brick:
	//-----------------	
	uint64_t voxelsWritten = 0;

	for(uint32_t i = 0; i < info->numBricks; i++)
	{
		uint32_t idx = info->brickStartIdx + i;

		uint32_t numVoxelsBrick;
		SPLVerror brickDecodeError = splv_brick_decode(
			&decompressedReader,
			&info->outFrame->bricks[idx],
			info->outFrameCompact ? 
				&info->outFrameCompact->voxels[info->voxelsStartIdx + voxelsWritten] : NULL,
			info->numVoxels - voxelsWritten,
			info->decoder->scratchBufBrickPositions[idx].x, 
			info->decoder->scratchBufBrickPositions[idx].y,
			info->decoder->scratchBufBrickPositions[idx].z,
			info->lastFrame,
			&numVoxelsBrick
		);

		if(info->outFrameCompact)
		{
			SPLVbrick* brick = &info->outFrame->bricks[idx];
			SPLVbrickCompact* brickCompact = &info->outFrameCompact->bricks[idx];
			
			memcpy(brickCompact->bitmap, brick->bitmap, sizeof(brick->bitmap));
			brickCompact->voxelsOffset = (uint32_t)(info->voxelsStartIdx + voxelsWritten);
		}

		if(brickDecodeError != SPLV_SUCCESS)
		{
			splv_buffer_writer_destroy(&decompressedWriter);

			SPLV_LOG_ERROR("error while decoding brick");
			return brickDecodeError;
		}

		voxelsWritten += numVoxelsBrick;
	}

	//cleanup + return:
	//-----------------	
	splv_buffer_writer_destroy(&decompressedWriter);

	return SPLV_SUCCESS;
}

//-------------------------------------------//

static inline SPLVerror _splv_decoder_read(SPLVdecoder* decoder, uint64_t size, void* dst)
{
	if(decoder->fromFile)
	{
		if(fread(dst, size, 1, decoder->inFile.file) < 1)
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
		if(fseek(decoder->inFile.file, (long)pos, SEEK_SET) != 0)
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