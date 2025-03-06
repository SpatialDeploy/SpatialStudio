#include "spatialstudio/splv_encoder.h"

#include "spatialstudio/splv_range_coder.h"
#include "spatialstudio/splv_log.h"
#include "spatialstudio/splv_buffer_io.h"

//-------------------------------------------//

#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))

//-------------------------------------------//

/**
 * all info needed for a thread to encode a brick group
 */
typedef struct SPLVbrickGroupEncodeInfo
{
	SPLVencoder* encoder;

	SPLVframe* frame; 
	SPLVframeEncodingType frameType; 
	
	uint32_t numBricks; 
	SPLVbrick** bricks;
	SPLVcoordinate* brickPositions;
	
	SPLVbufferWriter* outBuf;
	uint64_t* numVoxels;
} SPLVbrickGroupEncodeInfo;

//-------------------------------------------//

static SPLVerror _splv_encoder_encode_brick_group(void* info);

static void _splv_encoder_destroy(SPLVencoder* encoder);

//-------------------------------------------//

SPLVerror splv_encoder_create(SPLVencoder* encoder, uint32_t width, uint32_t height, uint32_t depth, float framerate, SPLVencodingParams encodingParams, const char* outPath)
{
	//validate params:
	//---------------
	SPLV_ASSERT(width > 0 && height > 0 && depth > 0, 
		"volume dimensions must be positive");
	SPLV_ASSERT(width % SPLV_BRICK_SIZE == 0 && height % SPLV_BRICK_SIZE == 0 && depth == SPLV_BRICK_SIZE == 0, 
		"volume dimensions must be a multiple of SPLV_BRICK_SIZE");
	SPLV_ASSERT(framerate > 0.0f, "framerate must be positive");
	SPLV_ASSERT(encodingParams.gopSize > 0, "gop size must be positive");
	SPLV_ASSERT(encodingParams.maxRegionDim % SPLV_BRICK_SIZE == 0, "maxRegionDim must be a multiple of SPLV_BRICK_SIZE");

	if(encodingParams.maxBrickGroupSize > 0 && encodingParams.maxBrickGroupSize < 128)
		SPLV_LOG_WARNING("small values of maxBrickGroupSize can significantly reduce efficiency and decoding speed");

	//initialize:
	//---------------
	memset(encoder, 0, sizeof(SPLVencoder)); //clear any ptrs to NULL

	encoder->width = width;
	encoder->height = height;
	encoder->depth = depth;
	encoder->framerate = framerate;
	encoder->frameCount = 0;
	encoder->frameTable = (SPLVdynArrayUint64){0};
	encoder->encodingParams = encodingParams;

	//create frame table:
	//---------------
	SPLVerror dynArrayError = splv_dyn_array_uint64_create(&encoder->frameTable, 0);
	if(dynArrayError != SPLV_SUCCESS)
	{
		_splv_encoder_destroy(encoder);

		SPLV_LOG_ERROR("failed to create frame table");
		return dynArrayError;
	}

	//open output file:
	//---------------
	encoder->outFile = fopen(outPath, "wb");
	if(!encoder->outFile)
	{
		_splv_encoder_destroy(encoder);

		SPLV_LOG_ERROR("failed to open output file");
		return SPLV_ERROR_FILE_OPEN;
	}

	//allocate scratch buffers:
	//---------------
	uint32_t widthMap  = encoder->width  / SPLV_BRICK_SIZE;
	uint32_t heightMap = encoder->height / SPLV_BRICK_SIZE;
	uint32_t depthMap  = encoder->depth  / SPLV_BRICK_SIZE;

	uint32_t mapLen = widthMap * heightMap * depthMap;
	uint32_t mapLenBitmap = mapLen;
	mapLenBitmap = (mapLenBitmap + 31) & (~31); //round up to multiple of 32 (sizeof(uint32_t))
	mapLenBitmap /= 4; //4 bytes per uint32_t
	mapLenBitmap /= 8; //8 bits per byte

	uint32_t maxBrickGroups;
	if(encoder->encodingParams.maxBrickGroupSize == 0)
		maxBrickGroups = 1;
	else
		maxBrickGroups = (mapLen + encoder->encodingParams.maxBrickGroupSize - 1) / encoder->encodingParams.maxBrickGroupSize;

	encoder->mapBitmapLen = mapLenBitmap;

	encoder->scratchBufMapBitmap = (uint32_t*)SPLV_MALLOC(mapLenBitmap * sizeof(uint32_t));
	encoder->scratchBufBricks = (SPLVbrick**)SPLV_MALLOC(mapLen * sizeof(SPLVbrick*));
	encoder->scratchBufBrickPositions = (SPLVcoordinate*)SPLV_MALLOC(mapLen * sizeof(SPLVcoordinate));
	encoder->scratchBufBrickGroupWriters = (SPLVbufferWriter*)SPLV_MALLOC(maxBrickGroups * sizeof(SPLVbufferWriter));
	encoder->scratchBufVoxelCounts = (uint64_t*)SPLV_MALLOC(maxBrickGroups * sizeof(uint64_t));
	
	if(!encoder->scratchBufMapBitmap || !encoder->scratchBufBricks || !encoder->scratchBufBrickPositions || !encoder->scratchBufBrickGroupWriters)
	{
		_splv_encoder_destroy(encoder);

		SPLV_LOG_ERROR("failed to allocate encoder scratch buffers");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	//create thread pool:
	//---------------
	SPLVerror threadPoolError = splv_thread_pool_create(
		&encoder->threadPool, SPLV_ENCODER_THREAD_POOL_SIZE,
		_splv_encoder_encode_brick_group, sizeof(SPLVbrickGroupEncodeInfo)
	);
	if(threadPoolError != SPLV_SUCCESS)
	{
		_splv_encoder_destroy(encoder);

		SPLV_LOG_ERROR("failed to create encoder thread pool");
		return threadPoolError;	
	}

	//write empty header (will write over with complete header when encoding is finished):
	//---------------
	SPLVfileHeader emptyHeader = {0};
	if(fwrite(&emptyHeader, sizeof(SPLVfileHeader), 1, encoder->outFile) < 1)
	{
		_splv_encoder_destroy(encoder);

		SPLV_LOG_ERROR("failed to write empty header to output file");
		return SPLV_ERROR_FILE_WRITE;
	}

	return SPLV_SUCCESS;
}

SPLVerror splv_encoder_encode_frame(SPLVencoder* encoder, SPLVframe* frame, splv_bool_t* canFree)
{
	//validate:
	//---------------
	uint32_t widthMap  = encoder->width  / SPLV_BRICK_SIZE;
	uint32_t heightMap = encoder->height / SPLV_BRICK_SIZE;
	uint32_t depthMap  = encoder->depth  / SPLV_BRICK_SIZE;

	SPLV_ASSERT(widthMap == frame->width && heightMap == frame->height && depthMap == frame->depth,
		"frame dimensions must match those specified in splv_encoder_create()");

	//determine frame type:
	//---------------
	SPLVframeEncodingType frameType;
	if(encoder->frameCount % encoder->encodingParams.gopSize == 0)
		frameType = SPLV_FRAME_ENCODING_TYPE_I;
	else
		frameType = SPLV_FRAME_ENCODING_TYPE_P;

	//get frame ptr:
	//---------------
	long framePtr = ftell(encoder->outFile);
	if(framePtr	== -1L)
	{
		SPLV_LOG_ERROR("error getting file write position");
		return SPLV_ERROR_FILE_WRITE;
	}

	uint64_t frameTableEntry = ((uint64_t)frameType << 56) | (uint64_t)framePtr;
	SPLV_ERROR_PROPAGATE(splv_dyn_array_uint64_push(&encoder->frameTable, frameTableEntry));

	//compress map (convert to bitmap):
	//---------------
	uint32_t numBricksOrdered = 0;

	//we are writing bricks in xyz order, we MUST make sure to read them back in the same order
	for(uint32_t xMap = 0; xMap < widthMap ; xMap++)
	for(uint32_t yMap = 0; yMap < heightMap; yMap++)
	for(uint32_t zMap = 0; zMap < depthMap ; zMap++)
	{
		uint32_t mapIdxRead = splv_frame_get_map_idx(frame, xMap, yMap, zMap);

		uint32_t mapIdxWrite = xMap + widthMap * (yMap + heightMap * zMap);
		uint32_t mapIdxWriteArr = mapIdxWrite / 32;
		uint32_t mapIdxWriteBit = mapIdxWrite % 32;
		
		if(frame->map[mapIdxRead] != SPLV_BRICK_IDX_EMPTY)
		{
			encoder->scratchBufMapBitmap[mapIdxWriteArr] |= (1u << mapIdxWriteBit);

			encoder->scratchBufBricks[numBricksOrdered] = &frame->bricks[frame->map[mapIdxRead]];
			encoder->scratchBufBrickPositions[numBricksOrdered] = (SPLVcoordinate){ xMap, yMap, zMap };
			numBricksOrdered++;
		}
		else
			encoder->scratchBufMapBitmap[mapIdxWriteArr] &= ~(1u << mapIdxWriteBit);
	}

	//sanity check
	SPLV_ASSERT(frame->bricksLen == numBricksOrdered, "number of ordered bricks does not match original brick count, sanity check failed");

	//encode each brick group:
	//---------------
	uint32_t maxBrickGroupSize;
	if(encoder->encodingParams.maxBrickGroupSize == 0)
		maxBrickGroupSize = max(numBricksOrdered, 1);
	else
		maxBrickGroupSize = encoder->encodingParams.maxBrickGroupSize;

	uint32_t numBrickGroups = (numBricksOrdered + maxBrickGroupSize - 1) / maxBrickGroupSize;
	uint32_t baseBrickGroupSize      = numBricksOrdered / max(numBrickGroups, 1);
	uint32_t brickGroupSizeRemainder = numBricksOrdered % max(numBrickGroups, 1);

	for(uint32_t i = 0; i < numBrickGroups; i++)
	{
		uint32_t startBrick = i * baseBrickGroupSize + min(i, brickGroupSizeRemainder);
		uint32_t numBricks = baseBrickGroupSize + (i < brickGroupSizeRemainder ? 1 : 0);

		SPLVbrickGroupEncodeInfo encodeInfo;
		encodeInfo.encoder = encoder;
		encodeInfo.frame = frame;
		encodeInfo.frameType = frameType;
		encodeInfo.numBricks = numBricks;
		encodeInfo.bricks = &encoder->scratchBufBricks[startBrick];
		encodeInfo.brickPositions = &encoder->scratchBufBrickPositions[startBrick];
		encodeInfo.outBuf = &encoder->scratchBufBrickGroupWriters[i];
		encodeInfo.numVoxels = &encoder->scratchBufVoxelCounts[i];

		SPLVerror addWorkError = splv_thread_pool_add_work(encoder->threadPool, &encodeInfo);
		if(addWorkError != SPLV_SUCCESS)
		{
			SPLV_LOG_ERROR("failed to add work to thread pool");
			return addWorkError;
		}
	}

	//wait for encoding threads:
	//---------------
	SPLVerror waitError = splv_thread_pool_wait(encoder->threadPool);
	if(waitError != SPLV_SUCCESS)
	{
		SPLV_LOG_ERROR("failed to wait on thread pool");
		return waitError;
	}

	//get total voxel count:
	//---------------
	uint64_t numVoxels = 0;
	for(uint32_t i = 0; i < numBrickGroups; i++)
		numVoxels += encoder->scratchBufVoxelCounts[i];

	//write num bricks, num voxels, map:
	//---------------
	if(fwrite(&numBricksOrdered, sizeof(uint32_t), 1, encoder->outFile) < 1)
	{
		SPLV_LOG_ERROR("error writing brick count to output file");
		return SPLV_ERROR_FILE_WRITE;
	}

	if(fwrite(&numVoxels, sizeof(uint64_t), 1, encoder->outFile) < 1)
	{
		SPLV_LOG_ERROR("error writing voxel count to output file");
		return SPLV_ERROR_FILE_WRITE;
	}

	if(fwrite(encoder->scratchBufMapBitmap, encoder->mapBitmapLen * sizeof(uint32_t), 1, encoder->outFile) < 1)
	{
		SPLV_LOG_ERROR("error writing map bitmap to output file");
		return SPLV_ERROR_FILE_WRITE;
	}

	//write encoded groups to output file:
	//---------------
	uint64_t curGroupOffset = 0;

	for(uint32_t i = 0; i < numBrickGroups; i++)
	{
		if(fwrite(&curGroupOffset, sizeof(uint64_t), 1, encoder->outFile) < 1)
		{
			SPLV_LOG_ERROR("failed to write group offset to output file");
			return SPLV_ERROR_FILE_WRITE;
		}

		uint64_t numVoxelsGroup = encoder->scratchBufVoxelCounts[i];
		if(fwrite(&numVoxelsGroup, sizeof(uint64_t), 1, encoder->outFile) < 1)
		{
			SPLV_LOG_ERROR("failed to write group voxel count to output file");
			return SPLV_ERROR_FILE_WRITE;
		}

		curGroupOffset += encoder->scratchBufBrickGroupWriters[i].writePos;
	}

	for(uint32_t i = 0; i < numBrickGroups; i++)
	{
		uint8_t* buf  = encoder->scratchBufBrickGroupWriters[i].buf;
		uint64_t size = encoder->scratchBufBrickGroupWriters[i].writePos;

		if(fwrite(buf, size, 1, encoder->outFile) < 1)
		{
			SPLV_LOG_ERROR("failed to write brick group to output file");
			return SPLV_ERROR_FILE_WRITE;
		}

		//cleanup
		splv_buffer_writer_destroy(&encoder->scratchBufBrickGroupWriters[i]);
	}

	//cleanup + return:
	//---------------
	encoder->frameCount++;
	encoder->lastFrame = *frame;

	//can only free previous frames if at end of GOP
	*canFree = (encoder->frameCount % encoder->encodingParams.gopSize) == 0;
	return SPLV_SUCCESS;
}

SPLVerror splv_encoder_finish(SPLVencoder* encoder)
{
	//write frame table:
	//---------------
	long frameTablePtr = ftell(encoder->outFile);
	if(frameTablePtr == -1L)
	{
		SPLV_LOG_ERROR("error getting file write position");
		return SPLV_ERROR_FILE_WRITE;
	}

	uint64_t frameTableSize = encoder->frameCount * sizeof(uint64_t);
	if(fwrite(encoder->frameTable.arr, frameTableSize, 1, encoder->outFile) < 1)
	{
		SPLV_LOG_ERROR("failed writing frame table to file");
		return SPLV_ERROR_FILE_WRITE;
	}

	//write header:
	//---------------
	SPLVfileHeader header = {0};
	header.magicWord = SPLV_MAGIC_WORD;
	header.version = SPLV_VERSION;
	header.width = encoder->width;
	header.height = encoder->height;
	header.depth = encoder->depth;
	header.framerate = encoder->framerate;
	header.frameCount = encoder->frameCount;
	header.duration = (float)encoder->frameCount / encoder->framerate;
	header.frameTablePtr = (uint64_t)frameTablePtr;
	header.encodingParams = encoder->encodingParams;

	if(fseek(encoder->outFile, 0, SEEK_SET) != 0)
	{
		SPLV_LOG_ERROR("error seeking to beginning of output file");
		return SPLV_ERROR_FILE_WRITE;
	}

	if(fwrite(&header, sizeof(SPLVfileHeader), 1, encoder->outFile) < 1)
	{
		SPLV_LOG_ERROR("failed writing header to output file");
		return SPLV_ERROR_FILE_WRITE;
	}

	//cleanup + return:
	//---------------
	if(fclose(encoder->outFile) != 0)
	{
		SPLV_LOG_ERROR("error closing output file");
		return SPLV_ERROR_FILE_WRITE;
	}

	encoder->outFile = NULL;
	_splv_encoder_destroy(encoder);

	return SPLV_SUCCESS;
}

void splv_encoder_abort(SPLVencoder* encoder)
{
	_splv_encoder_destroy(encoder);
}

//-------------------------------------------//

static SPLVerror _splv_encoder_encode_brick_group(void* arg)
{
	//get info:
	//-----------------
	SPLVbrickGroupEncodeInfo* info = (SPLVbrickGroupEncodeInfo*)arg;

	//encode bricks:
	//---------------
	SPLVbufferWriter brickWriter;
	SPLVerror brickWriterError = splv_buffer_writer_create(&brickWriter, 0);
	if(brickWriterError != SPLV_SUCCESS)
		return brickWriterError;

	*info->numVoxels = 0;

	for(uint32_t i = 0; i < info->numBricks; i++)
	{
		SPLVerror brickEncodeError;
		uint32_t brickNumVoxels;

		if(info->frameType == SPLV_FRAME_ENCODING_TYPE_P)
		{
			SPLVcoordinate brickPos = info->brickPositions[i];
			brickEncodeError = splv_brick_encode_predictive(
				info->bricks[i], brickPos.x, brickPos.y, brickPos.z, 
				&brickWriter, &info->encoder->lastFrame, &brickNumVoxels,
				info->encoder->encodingParams.motionVectors
			);
		}
		else
		{
			brickEncodeError = splv_brick_encode_intra(
				info->bricks[i], &brickWriter, &brickNumVoxels
			);
		}

		if(brickEncodeError != SPLV_SUCCESS)
		{
			splv_buffer_writer_destroy(&brickWriter);

			SPLV_LOG_ERROR("error encoding brick");
			return brickEncodeError;
		}

		*info->numVoxels += brickNumVoxels;
	}

	//entropy code group:
	//---------------
	SPLVerror encodedWriterError = splv_buffer_writer_create(info->outBuf, 0);
	if(encodedWriterError != SPLV_SUCCESS)
	{
		splv_buffer_writer_destroy(&brickWriter);

		return encodedWriterError;
	}

	SPLVerror encodeError = splv_rc_encode(
		brickWriter.writePos, brickWriter.buf, info->outBuf
	);

	if(encodeError != SPLV_SUCCESS)
	{
		splv_buffer_writer_destroy(info->outBuf);
		splv_buffer_writer_destroy(&brickWriter);

		SPLV_LOG_ERROR("error range coding brick group");
		return encodeError;
	}

	//cleanup + return:
	//---------------
	splv_buffer_writer_destroy(&brickWriter);

	return SPLV_SUCCESS;
}

//-------------------------------------------//

static void _splv_encoder_destroy(SPLVencoder* encoder)
{
	if(encoder->scratchBufMapBitmap)
		SPLV_FREE(encoder->scratchBufMapBitmap);
	if(encoder->scratchBufBricks)
		SPLV_FREE(encoder->scratchBufBricks);
	if(encoder->scratchBufBrickPositions)
		SPLV_FREE(encoder->scratchBufBrickPositions);
	if(encoder->scratchBufBrickGroupWriters)
		SPLV_FREE(encoder->scratchBufBrickGroupWriters);
	if(encoder->scratchBufVoxelCounts)
		SPLV_FREE(encoder->scratchBufVoxelCounts);

	if(encoder->outFile)
		fclose(encoder->outFile);

	splv_dyn_array_uint64_destroy(&encoder->frameTable);
}