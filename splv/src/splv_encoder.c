#include "spatialstudio/splv_encoder.h"

#include "spatialstudio/splv_range_coder.h"
#include "spatialstudio/splv_log.h"
#include "spatialstudio/splv_buffer_io.h"
#include "splv_morton_lut.h"

//-------------------------------------------//

static void _splv_encoder_destroy(SPLVencoder* encoder);

//-------------------------------------------//

SPLVerror splv_encoder_create(SPLVencoder* encoder, uint32_t width, uint32_t height, uint32_t depth, float framerate, uint32_t gopSize, const char* outPath)
{
	//validate params:
	//---------------
	SPLV_ASSERT(width > 0 && height > 0 && depth > 0, 
		"volume dimensions must be positive");
	SPLV_ASSERT(width % SPLV_BRICK_SIZE == 0 && height % SPLV_BRICK_SIZE == 0 && depth == SPLV_BRICK_SIZE > 0, 
		"volume dimensions must be a multiple of SPLV_BRICK_SIZE");
	SPLV_ASSERT(framerate > 0.0f, "framerate must be positive");
	SPLV_ASSERT(gopSize > 0, "gop size must be positive");

	//initialize:
	//---------------
	memset(encoder, 0, sizeof(SPLVencoder)); //clear any ptrs to NULL

	encoder->width = width;
	encoder->height = height;
	encoder->depth = depth;
	encoder->framerate = framerate;
	encoder->frameCount = 0;
	encoder->frameTable = (SPLVdynArrayUint64){0};
	encoder->gopSize = gopSize;
	encoder->frameWriter = (SPLVbufferWriter){0};
	encoder->encodedFrameWriter = (SPLVbufferWriter){0};

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

	//create frame writers:
	//---------------
	SPLVerror frameWriterError = splv_buffer_writer_create(&encoder->frameWriter, 0);
	if(frameWriterError != SPLV_SUCCESS)
	{
		_splv_encoder_destroy(encoder);

		SPLV_LOG_ERROR("failed to create buffer writer for frame data");
		return frameWriterError;
	}

	SPLVerror encodedFrameWriterError = splv_buffer_writer_create(&encoder->encodedFrameWriter, 0);
	if(encodedFrameWriterError != SPLV_SUCCESS)
	{
		_splv_encoder_destroy(encoder);

		SPLV_LOG_ERROR("failed to create buffer writer for encoded frame data");
		return frameWriterError;
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

	encoder->mapBitmapLen = mapLenBitmap;

	encoder->scratchBufMapBitmap = (uint32_t*)SPLV_MALLOC(mapLenBitmap * sizeof(uint32_t));
	encoder->scratchBufBricks = (SPLVbrick**)SPLV_MALLOC(mapLen * sizeof(SPLVbrick*));
	encoder->scratchBufBrickPositions = (SPLVcoordinate*)SPLV_MALLOC(mapLen * sizeof(SPLVcoordinate));
	if(!encoder->scratchBufMapBitmap || !encoder->scratchBufBricks || !encoder->scratchBufBrickPositions)
	{
		_splv_encoder_destroy(encoder);

		SPLV_LOG_ERROR("failed to allocate encoder scratch buffers");
		return SPLV_ERROR_OUT_OF_MEMORY;
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
	if(encoder->frameCount % encoder->gopSize == 0)
		frameType = SPLV_FRAME_ENCODING_TYPE_I;
	else
		frameType = SPLV_FRAME_ENCODING_TYPE_P;

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

	//seralize frame:
	//---------------
	splv_buffer_writer_reset(&encoder->frameWriter);

	SPLV_ERROR_PROPAGATE(splv_buffer_writer_write(&encoder->frameWriter, sizeof(uint32_t), &numBricksOrdered));
	SPLV_ERROR_PROPAGATE(splv_buffer_writer_write(&encoder->frameWriter, encoder->mapBitmapLen * sizeof(uint32_t), encoder->scratchBufMapBitmap));

	for(uint32_t i = 0; i < numBricksOrdered; i++)
	{
		if(frameType == SPLV_FRAME_ENCODING_TYPE_P)
		{
			SPLVcoordinate brickPos = encoder->scratchBufBrickPositions[i];
			SPLV_ERROR_PROPAGATE(splv_brick_encode_predictive(encoder->scratchBufBricks[i], 
				brickPos.x, brickPos.y, brickPos.z, 
				&encoder->frameWriter, &encoder->lastFrame
			));
		}
		else
		{
			SPLV_ERROR_PROPAGATE(splv_brick_encode_intra(
				encoder->scratchBufBricks[i], 
				&encoder->frameWriter
			));
		}
	}

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

	//entropy code frame:
	//---------------
	splv_buffer_writer_reset(&encoder->encodedFrameWriter);

	SPLV_ERROR_PROPAGATE(splv_rc_encode(
		encoder->frameWriter.writePos, 
		encoder->frameWriter.buf, 
		&encoder->encodedFrameWriter
	));

	//write encoded frame to output file:
	//---------------
	if(fwrite(encoder->encodedFrameWriter.buf, encoder->encodedFrameWriter.writePos, 1, encoder->outFile) < 1)
	{
		SPLV_LOG_ERROR("failed to write encoded frame");
		return SPLV_ERROR_FILE_WRITE;
	}

	//cleanup + return:
	//---------------
	encoder->frameCount++;
	encoder->lastFrame = *frame;

	//can only free previous frames if at end of GOP
	*canFree = (encoder->frameCount % encoder->gopSize) == 0;
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

static void _splv_encoder_destroy(SPLVencoder* encoder)
{
	if(encoder->scratchBufMapBitmap)
		SPLV_FREE(encoder->scratchBufMapBitmap);
	if(encoder->scratchBufBricks)
		SPLV_FREE(encoder->scratchBufBricks);
	if(encoder->scratchBufBrickPositions)
		SPLV_FREE(encoder->scratchBufBrickPositions);

	splv_buffer_writer_destroy(&encoder->frameWriter);
	splv_buffer_writer_destroy(&encoder->encodedFrameWriter);

	if(encoder->outFile)
		fclose(encoder->outFile);

	splv_dyn_array_uint64_destroy(&encoder->frameTable);
}