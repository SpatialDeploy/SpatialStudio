#include "splv_encoder.h"

#include "splv_morton_lut.h"
#include "splv_log.h"
#include "splv_buffer_io.h"

#define SPLV_RC_IMPLEMENTATION
#include "splv_range_coder.h"

//-------------------------------------------//

typedef enum SPLVframeType
{
	SPLV_FRAME_TYPE_I = 0,
	SPLV_FRAME_TYPE_P = 1
} SPLVframeType;

//-------------------------------------------//

SPLVerror splv_encoder_create(SPLVencoder** e, uint32_t width, uint32_t height, uint32_t depth, float framerate, uint32_t gopSize, const char* outPath)
{
	//validate params:
	//---------------
	if(width == 0 || height == 0 || depth == 0)
	{
		SPLV_LOG_ERROR("volume dimensions must be positive");
		return SPLV_ERROR_INVALID_ARGUMENTS;
	}

	if(width % SPLV_BRICK_SIZE > 0 || height % SPLV_BRICK_SIZE > 0 || depth % SPLV_BRICK_SIZE > 0)
	{
		SPLV_LOG_ERROR("volume dimensions must be a multiple of SPLV_BRICK_SIZE");
		return SPLV_ERROR_INVALID_ARGUMENTS;
	}

	if(framerate <= 0.0f)
	{
		SPLV_LOG_ERROR("framerate must be positive");
		return SPLV_ERROR_INVALID_ARGUMENTS;
	}

	if(gopSize == 0)
	{
		SPLV_LOG_ERROR("gop size must be positive");
		return SPLV_ERROR_INVALID_ARGUMENTS;
	}

	//allocate struct:
	//---------------
	*e = (SPLVencoder*)SPLV_MALLOC(sizeof(SPLVencoder));
	SPLVencoder* encoder = *e;

	if(!encoder)
	{
		SPLV_LOG_ERROR("failed to allocate SPLVencoder struct");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	encoder->width = width;
	encoder->height = height;
	encoder->depth = depth;
	encoder->framerate = framerate;
	encoder->frameCount = 0;
	encoder->gopSize = gopSize;

	const uint32_t FRAME_PTR_INITIAL_CAP = 16;
	SPLV_ERROR_PROPAGATE(splv_dyn_array_uint64_create(&encoder->frameTable, FRAME_PTR_INITIAL_CAP));

	//open output file:
	//---------------
	encoder->outFile = fopen(outPath, "wb");
	if(!encoder->outFile)
	{
		SPLV_LOG_ERROR("failed to open output file");
		return SPLV_ERROR_FILE_OPEN;
	}

	//write empty header (will write over with complete header when encoding is finished):
	//---------------
	SPLVfileHeader emptyHeader = {0};
	if(fwrite(&emptyHeader, sizeof(SPLVfileHeader), 1, encoder->outFile) < 1)
	{
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

	if(widthMap != frame->width || heightMap != frame->height || depthMap != frame->depth)
	{
		SPLV_LOG_ERROR("frame dimensions do not match encoder's");
		return SPLV_ERROR_INVALID_ARGUMENTS;
	}

	//determine frame type:
	//---------------
	SPLVframeType frameType;
	if(encoder->frameCount % encoder->gopSize == 0)
		frameType = SPLV_FRAME_TYPE_I;
	else
		frameType = SPLV_FRAME_TYPE_P;

	//compress map (convert to bitmap):
	//---------------
	uint32_t mapLenBitmap = widthMap * heightMap * depthMap;
	mapLenBitmap = (mapLenBitmap + 31) & (~31); //round up to multiple of 32 (sizeof(uint32_t))
	mapLenBitmap /= 4; //4 bytes per uint32_t
	mapLenBitmap /= 8; //8 bits per byte

	//automatically 0-initialized
	uint32_t* mapBitmap = (uint32_t*)SPLV_MALLOC(mapLenBitmap * sizeof(uint32_t));

	uint32_t numBricksOrdered = 0;
	SPLVbrick** bricksOrdered = (SPLVbrick**)SPLV_MALLOC(frame->bricksLen * sizeof(SPLVbrick*));
	SPLVcoordinate* brickPositionsOrdered = (SPLVcoordinate*)SPLV_MALLOC(frame->bricksLen * sizeof(SPLVcoordinate));

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
			mapBitmap[mapIdxWriteArr] |= (1u << mapIdxWriteBit);

			bricksOrdered[numBricksOrdered] = &frame->bricks[frame->map[mapIdxRead]];
			brickPositionsOrdered[numBricksOrdered] = (SPLVcoordinate){ xMap, yMap, zMap };
			numBricksOrdered++;
		}
		else
			mapBitmap[mapIdxWriteArr] &= ~(1u << mapIdxWriteBit);
	}

	//sanity check
	if(frame->bricksLen != numBricksOrdered)
	{
		SPLV_LOG_ERROR("number of ordered bricks does not match original brick count, sanity check failed");
		return SPLV_ERROR_RUNTIME;
	}

	//seralize frame:
	//---------------
	SPLVbufferWriter frameWriter;
	SPLV_ERROR_PROPAGATE(splv_buffer_writer_create(&frameWriter, 0));

	SPLV_ERROR_PROPAGATE(splv_buffer_writer_write(&frameWriter, sizeof(uint32_t), &numBricksOrdered));
	SPLV_ERROR_PROPAGATE(splv_buffer_writer_write(&frameWriter, mapLenBitmap * sizeof(uint32_t), mapBitmap));

	for(uint32_t i = 0; i < numBricksOrdered; i++)
	{
		if(frameType == SPLV_FRAME_TYPE_P)
		{
			SPLVcoordinate brickPos = brickPositionsOrdered[i];
			splv_brick_serialize_predictive(bricksOrdered[i], brickPos.x, brickPos.y, brickPos.z, &frameWriter, encoder->lastFrame);
		}
		else
			splv_brick_serialize(bricksOrdered[i], &frameWriter);
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
	uint8_t* encodedBuf;
	uint64_t encodedSize;

	SPLV_ERROR_PROPAGATE(splv_rc_encode(frameWriter.writePos, frameWriter.buf, &encodedBuf, &encodedSize));

	//write encoded frame to output file:
	//---------------
	if(fwrite(encodedBuf, encodedSize, 1, encoder->outFile) < 1)
	{
		SPLV_LOG_ERROR("failed to write encoded frame");
		return SPLV_ERROR_FILE_WRITE;
	}

	//cleanup + return:
	//---------------
	splv_rc_free_output_buf(encodedBuf);
	splv_buffer_writer_destroy(frameWriter);

	SPLV_FREE(mapBitmap);
	SPLV_FREE(bricksOrdered);

	encoder->frameCount++;
	encoder->lastFrame = frame;
	
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
	splv_dyn_array_uint64_destroy(encoder->frameTable);

	if(fclose(encoder->outFile) != 0)
	{
		SPLV_LOG_ERROR("error closing output file");
		return SPLV_ERROR_FILE_WRITE;
	}

	SPLV_FREE(encoder);
	
	return SPLV_SUCCESS;
}

void splv_encoder_abort(SPLVencoder* encoder)
{
	splv_dyn_array_uint64_destroy(encoder->frameTable);
	fclose(encoder->outFile);

	SPLV_FREE(encoder);
}