#include "splv_vox_utils.h"

#include <string.h>
#include <stdio.h>
#include "splv_log.h"
#include "splv_global.h"

//-------------------------------------------//

#define SPLV_VOX_CHUNK_ID(a, b, c, d) ((a) | ((b) << 8) | ((c) << 16) | ((d) << 24))

#define SPLV_VOX_DEFAULT_PALETTE {                                                                                                                                                                  \
	0x00000000, 0xffffffff, 0xffccffff, 0xff99ffff, 0xff66ffff, 0xff33ffff, 0xff00ffff, 0xffffccff, 0xffccccff, 0xff99ccff, 0xff66ccff, 0xff33ccff, 0xff00ccff, 0xffff99ff, 0xffcc99ff, 0xff9999ff, \
	0xff6699ff, 0xff3399ff, 0xff0099ff, 0xffff66ff, 0xffcc66ff, 0xff9966ff, 0xff6666ff, 0xff3366ff, 0xff0066ff, 0xffff33ff, 0xffcc33ff, 0xff9933ff, 0xff6633ff, 0xff3333ff, 0xff0033ff, 0xffff00ff, \
	0xffcc00ff, 0xff9900ff, 0xff6600ff, 0xff3300ff, 0xff0000ff, 0xffffffcc, 0xffccffcc, 0xff99ffcc, 0xff66ffcc, 0xff33ffcc, 0xff00ffcc, 0xffffcccc, 0xffcccccc, 0xff99cccc, 0xff66cccc, 0xff33cccc, \
	0xff00cccc, 0xffff99cc, 0xffcc99cc, 0xff9999cc, 0xff6699cc, 0xff3399cc, 0xff0099cc, 0xffff66cc, 0xffcc66cc, 0xff9966cc, 0xff6666cc, 0xff3366cc, 0xff0066cc, 0xffff33cc, 0xffcc33cc, 0xff9933cc, \
	0xff6633cc, 0xff3333cc, 0xff0033cc, 0xffff00cc, 0xffcc00cc, 0xff9900cc, 0xff6600cc, 0xff3300cc, 0xff0000cc, 0xffffff99, 0xffccff99, 0xff99ff99, 0xff66ff99, 0xff33ff99, 0xff00ff99, 0xffffcc99, \
	0xffcccc99, 0xff99cc99, 0xff66cc99, 0xff33cc99, 0xff00cc99, 0xffff9999, 0xffcc9999, 0xff999999, 0xff669999, 0xff339999, 0xff009999, 0xffff6699, 0xffcc6699, 0xff996699, 0xff666699, 0xff336699, \
	0xff006699, 0xffff3399, 0xffcc3399, 0xff993399, 0xff663399, 0xff333399, 0xff003399, 0xffff0099, 0xffcc0099, 0xff990099, 0xff660099, 0xff330099, 0xff000099, 0xffffff66, 0xffccff66, 0xff99ff66, \
	0xff66ff66, 0xff33ff66, 0xff00ff66, 0xffffcc66, 0xffcccc66, 0xff99cc66, 0xff66cc66, 0xff33cc66, 0xff00cc66, 0xffff9966, 0xffcc9966, 0xff999966, 0xff669966, 0xff339966, 0xff009966, 0xffff6666, \
	0xffcc6666, 0xff996666, 0xff666666, 0xff336666, 0xff006666, 0xffff3366, 0xffcc3366, 0xff993366, 0xff663366, 0xff333366, 0xff003366, 0xffff0066, 0xffcc0066, 0xff990066, 0xff660066, 0xff330066, \
	0xff000066, 0xffffff33, 0xffccff33, 0xff99ff33, 0xff66ff33, 0xff33ff33, 0xff00ff33, 0xffffcc33, 0xffcccc33, 0xff99cc33, 0xff66cc33, 0xff33cc33, 0xff00cc33, 0xffff9933, 0xffcc9933, 0xff999933, \
	0xff669933, 0xff339933, 0xff009933, 0xffff6633, 0xffcc6633, 0xff996633, 0xff666633, 0xff336633, 0xff006633, 0xffff3333, 0xffcc3333, 0xff993333, 0xff663333, 0xff333333, 0xff003333, 0xffff0033, \
	0xffcc0033, 0xff990033, 0xff660033, 0xff330033, 0xff000033, 0xffffff00, 0xffccff00, 0xff99ff00, 0xff66ff00, 0xff33ff00, 0xff00ff00, 0xffffcc00, 0xffcccc00, 0xff99cc00, 0xff66cc00, 0xff33cc00, \
	0xff00cc00, 0xffff9900, 0xffcc9900, 0xff999900, 0xff669900, 0xff339900, 0xff009900, 0xffff6600, 0xffcc6600, 0xff996600, 0xff666600, 0xff336600, 0xff006600, 0xffff3300, 0xffcc3300, 0xff993300, \
	0xff663300, 0xff333300, 0xff003300, 0xffff0000, 0xffcc0000, 0xff990000, 0xff660000, 0xff330000, 0xff0000ee, 0xff0000dd, 0xff0000bb, 0xff0000aa, 0xff000088, 0xff000077, 0xff000055, 0xff000044, \
	0xff000022, 0xff000011, 0xff00ee00, 0xff00dd00, 0xff00bb00, 0xff00aa00, 0xff008800, 0xff007700, 0xff005500, 0xff004400, 0xff002200, 0xff001100, 0xffee0000, 0xffdd0000, 0xffbb0000, 0xffaa0000, \
	0xff880000, 0xff770000, 0xff550000, 0xff440000, 0xff220000, 0xff110000, 0xffeeeeee, 0xffdddddd, 0xffbbbbbb, 0xffaaaaaa, 0xff888888, 0xff777777, 0xff555555, 0xff444444, 0xff222222, 0xff111111  \
}

//-------------------------------------------//

typedef struct SPLVvoxChunk
{
	uint32_t id;
	uint32_t len;
	uint32_t childLen;
	uint32_t endPtr;
} SPLVvoxChunk;

typedef struct SPLVvoxDict
{
	uint32_t numEntries;
	char** keys;
	char** vals;
} SPLVvoxDict;

//-------------------------------------------//

SPLVerror _splv_vox_create_frame(FILE* file, SPLVframe** outFrame, uint64_t xyziPtr, uint32_t* palette, SPLVboundingBox* bbox);

SPLVvoxChunk _splv_vox_read_chunk(FILE* file);
SPLVerror _splv_vox_read_dict(FILE* file, SPLVvoxDict* dict);
void _splv_vox_free_dict(SPLVvoxDict dict);

//-------------------------------------------//

SPLVerror splv_vox_load(const char* path, SPLVframe*** outFrames, uint32_t* numOutFrames, SPLVboundingBox* bbox)
{
	//TODO: this function leaks memory on failure, add a bump allocator
	//so we can free all memory on failure

	*outFrames = NULL;
	*numOutFrames = 0;

	//validate input:
	//---------------
	uint32_t xSize = bbox->xMax - bbox->xMin + 1;
	uint32_t ySize = bbox->yMax - bbox->yMin + 1;
	uint32_t zSize = bbox->zMax - bbox->zMin + 1;

	if(xSize % SPLV_BRICK_SIZE != 0 || ySize % SPLV_BRICK_SIZE != 0 || zSize % SPLV_BRICK_SIZE != 0)
	{
		SPLV_LOG_ERROR("frame dimensions must be a multiple of SPLV_BRICK_SIZE");
		return SPLV_ERROR_INVALID_ARGUMENTS;
	}

	//open file:
	//---------------
	FILE* file = fopen(path, "rb");
	if(!file)
	{
		SPLV_LOG_ERROR("failed to open vox file");
		return SPLV_ERROR_FILE_OPEN;
	}

	uint32_t id;
	fread(&id, sizeof(uint32_t), 1, file);
	if(id != SPLV_VOX_CHUNK_ID('V', 'O', 'X', ' '))
	{
		fclose(file);

		SPLV_LOG_ERROR("failed to open vox file");
		return SPLV_ERROR_INVALID_INPUT;
	}

	//TODO: ensure version is supported
	uint32_t version;
	fread(&version, sizeof(uint32_t), 1, file);

	//allocate dynamic arrays for tracking models:
	//---------------
	uint32_t palette[256] = SPLV_VOX_DEFAULT_PALETTE;

	SPLVdynArrayUint64 xyziPtrs;
	SPLVdynArrayUint64 frameIndices;
	SPLVdynArrayUint64 modelIndices;

	const uint32_t INITIAL_MODELS_CAP = 8;

	SPLVerror xyziPtrsCreateError = splv_dyn_array_uint64_create(&xyziPtrs, INITIAL_MODELS_CAP);
	if(xyziPtrsCreateError != SPLV_SUCCESS)
	{
		fclose(file);

		SPLV_LOG_ERROR("failed to create vox file xyzi pointers array");
		return xyziPtrsCreateError;
	}

	SPLVerror frameIndicesCreateError = splv_dyn_array_uint64_create(&frameIndices, INITIAL_MODELS_CAP);
	SPLVerror modelIndicesCreateError = splv_dyn_array_uint64_create(&modelIndices, INITIAL_MODELS_CAP);
	if(frameIndicesCreateError != SPLV_SUCCESS || modelIndicesCreateError != SPLV_SUCCESS)
	{
		fclose(file);

		SPLV_LOG_ERROR("failed to create vox file frame indices array");
		return frameIndicesCreateError != SPLV_SUCCESS ? frameIndicesCreateError : modelIndicesCreateError;
	}

	//read all chunks:
	//---------------
	uint8_t foundShapeNode = 0;

	SPLVvoxChunk mainChunk = _splv_vox_read_chunk(file);
	while(ftell(file) < (int32_t)mainChunk.endPtr)
	{
		if(feof(file) || ferror(file))
		{
			SPLV_LOG_ERROR("unexpected eof or error reading vox file");
			return SPLV_ERROR_FILE_READ;
		}

		SPLVvoxChunk chunk = _splv_vox_read_chunk(file);

		switch(chunk.id)
		{
		case SPLV_VOX_CHUNK_ID('X', 'Y', 'Z', 'I'):
		{
			SPLVerror pushError = splv_dyn_array_uint64_push(&xyziPtrs, (uint64_t)ftell(file));
			if(pushError != SPLV_SUCCESS)
			{
				fclose(file);

				SPLV_LOG_ERROR("failed to push vox xyzi pointer to array");
				return pushError;
			}
			
			break;
		}
		case SPLV_VOX_CHUNK_ID('R', 'G', 'B', 'A'):
		{
			fread(palette, 256 * sizeof(uint32_t), 1, file);
			break;
		}
		case SPLV_VOX_CHUNK_ID('n', 'S', 'H', 'P'):
		{
			//we only process the 1st shape node, we dont support multiple
			if(foundShapeNode)
			{
				SPLV_LOG_WARNING("additional shape node detected in vox file; will be discarded");
				break;
			}

			uint32_t nodeId;
			fread(&nodeId, sizeof(uint32_t), 1, file);

			SPLVvoxDict nodeAttribs;
			_splv_vox_read_dict(file, &nodeAttribs); //we dont need this, so we dont care if theres an error
			_splv_vox_free_dict(nodeAttribs);

			uint32_t numModels;
			fread(&numModels, sizeof(uint32_t), 1, file);

			for(uint32_t i = 0; i < numModels; i++)
			{
				uint32_t modelId;
				fread(&modelId, sizeof(uint32_t), 1, file);

				SPLVvoxDict modelAttribs;
				SPLVerror dictError = _splv_vox_read_dict(file, &modelAttribs);
				if(dictError != SPLV_SUCCESS)
				{
					fclose(file);

					SPLV_LOG_ERROR("failed to read vox model attribs dictionary");
					return dictError;
				}

				uint32_t frameIdxIdx = 0;
				while(frameIdxIdx < modelAttribs.numEntries && strcmp(modelAttribs.keys[frameIdxIdx], "_f") != 0)
					frameIdxIdx++;

				uint32_t frameIdx;
				if(frameIdxIdx >= modelAttribs.numEntries)
				{
					SPLV_LOG_WARNING("vox file model attributes did not contain frame index");
					frameIdx = i;
				}
				else
					frameIdx = atoi(modelAttribs.vals[frameIdxIdx]);

				SPLVerror frameIdxPushError = splv_dyn_array_uint64_push(&frameIndices, frameIdx);
				SPLVerror modelIdxPushError = splv_dyn_array_uint64_push(&modelIndices, modelId);
				if(frameIdxPushError != SPLV_SUCCESS || modelIdxPushError != SPLV_SUCCESS)
				{
					fclose(file);

					SPLV_LOG_ERROR("failed to push to vox file frame index array");
					return frameIdxPushError != SPLV_SUCCESS ? frameIdxPushError : modelIdxPushError;
				}

				_splv_vox_free_dict(modelAttribs);
			}

			foundShapeNode = 1;
			break;
		}
		default:
			break;
		}

		fseek(file, chunk.endPtr, SEEK_SET);
	}

	//validate:
	//---------------
	if(xyziPtrs.len == 0)
	{
		fclose(file);
		
		SPLV_LOG_ERROR("no models found in vox file");
		return SPLV_ERROR_INVALID_INPUT;
	}

	if(!foundShapeNode || modelIndices.len == 0)
	{
		fclose(file);
		
		SPLV_LOG_ERROR("no shape node containing animation data found in vox file");
		return SPLV_ERROR_INVALID_INPUT;
	}

	//add models:
	//---------------
	uint64_t numFrames = frameIndices.arr[frameIndices.len - 1] + 1;
	*numOutFrames = 0; //we increment this iteratively
	*outFrames = (SPLVframe**)SPLV_MALLOC(numFrames * sizeof(SPLVframe*));
	if(!*outFrames)
	{
		SPLV_LOG_ERROR("failed to allocate output frame array for vox loader");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	SPLVframe* curFrame = NULL;
	for(uint64_t i = 0; i < modelIndices.len; i++)
	{
		uint64_t frameIdx = frameIndices.arr[i];
		uint64_t modelIdx = modelIndices.arr[i];

		if(i > 0)
		{
			//the same frame may be repeated multiple times in a vox file
			uint64_t prevFrameIdx = frameIndices.arr[i - 1];
			for(uint64_t j = 0; j < frameIdx - prevFrameIdx - 1; j++)
			{
				(*outFrames)[prevFrameIdx + j + 1] = curFrame;
				(*numOutFrames)++;
			}
		}

		SPLVerror frameError = _splv_vox_create_frame(file, &curFrame, xyziPtrs.arr[modelIdx], palette, bbox);
		if(frameError != SPLV_SUCCESS)
			return frameError;

		(*outFrames)[frameIdx] = curFrame;
		(*numOutFrames)++;

		if(i == 0)
		{
			for(uint64_t j = 0; j < frameIdx; j++)
			{
				(*outFrames)[j] = curFrame;
				(*numOutFrames)++;
			}
		}
	}

	//cleanup + return:
	//---------------
	splv_dyn_array_uint64_destroy(xyziPtrs);
	splv_dyn_array_uint64_destroy(modelIndices);
	splv_dyn_array_uint64_destroy(frameIndices);

	fclose(file);

	return SPLV_SUCCESS;
}

void splv_vox_frames_destroy(SPLVframe** frames, uint32_t numFrames)
{
	if(frames == NULL)
		return;

	for(uint32_t i = 0; i < numFrames; i++)
	{
		if(frames[i] == NULL)
			continue;

		uint32_t j;
		for(j = 0; j < i; j++)
		{
			if(frames[i] == frames[j])
				break;
		}

		if(i == j)
			splv_frame_destroy(frames[i]);
	}

	SPLV_FREE(frames);
}

SPLVerror splv_vox_get_max_dimensions(const char* path, uint32_t* xSize, uint32_t* ySize, uint32_t* zSize)
{
	//open file:
	//---------------
	FILE* file = fopen(path, "rb");
	if(!file)
	{
		SPLV_LOG_ERROR("failed to open vox file");
		return SPLV_ERROR_FILE_OPEN;
	}

	uint32_t id;
	fread(&id, sizeof(uint32_t), 1, file);
	if(id != SPLV_VOX_CHUNK_ID('V', 'O', 'X', ' '))
	{
		fclose(file);

		SPLV_LOG_ERROR("failed to open vox file");
		return SPLV_ERROR_INVALID_INPUT;
	}

	//TODO: ensure version is supported
	uint32_t version;
	fread(&version, sizeof(uint32_t), 1, file);

	//read all chunks, looking for size chunk:
	//---------------
	*xSize = 0;
	*ySize = 0;
	*zSize = 0;

	SPLVvoxChunk mainChunk = _splv_vox_read_chunk(file);
	while(ftell(file) < (int32_t)mainChunk.endPtr)
	{
		if(feof(file) || ferror(file))
		{
			SPLV_LOG_ERROR("unexpected eof or error reading vox file");
			return SPLV_ERROR_FILE_READ;
		}

		SPLVvoxChunk chunk = _splv_vox_read_chunk(file);

		switch(chunk.id)
		{
		case SPLV_VOX_CHUNK_ID('S', 'I', 'Z', 'E'):
		{
			uint32_t newXsize;
			uint32_t newYsize;
			uint32_t newZsize;

			fread(&newXsize, sizeof(uint32_t), 1, file);
			fread(&newYsize, sizeof(uint32_t), 1, file);
			fread(&newZsize, sizeof(uint32_t), 1, file);

			if(newXsize > *xSize)
				*xSize = newXsize;
			if(newYsize > *ySize)
				*ySize = newYsize;
			if(newZsize > *zSize)
				*zSize = newZsize;

			break;
		}
		default:
			break;
		}

		fseek(file, chunk.endPtr, SEEK_SET);
	}	

	//cleanup + return:
	//---------------
	fclose(file);

	return SPLV_SUCCESS;
}

//-------------------------------------------//

SPLVerror _splv_vox_create_frame(FILE* file, SPLVframe** outFrame, uint64_t xyziPtr, uint32_t* palette, SPLVboundingBox* bbox)
{
	//create frame:
	//---------------

	//we swap z and y axes, .vox files are z-up
	uint32_t width  = bbox->xMax - bbox->xMin + 1;
	uint32_t height = bbox->yMax - bbox->yMin + 1;
	uint32_t depth  = bbox->zMax - bbox->zMin + 1;

	uint32_t widthMap  = width  / SPLV_BRICK_SIZE;
	uint32_t heightMap = height / SPLV_BRICK_SIZE;
	uint32_t depthMap  = depth  / SPLV_BRICK_SIZE;

	SPLVerror frameError = splv_frame_create(outFrame, widthMap, heightMap, depthMap);
	if(frameError != SPLV_SUCCESS)
		return frameError;

	SPLVframe* frame = *outFrame;

	uint32_t mapLen = widthMap * heightMap * depthMap;
	for(uint32_t i = 0; i < mapLen; i++)
		frame->map[i] = SPLV_BRICK_IDX_EMPTY;

	//parse voxels:
	//---------------
	fseek(file, (long)xyziPtr, SEEK_SET);

	uint32_t numVoxels = 0;
	fread(&numVoxels, sizeof(uint32_t), 1, file);

	for(uint32_t i = 0; i < numVoxels; i++)
	{
		uint32_t xyzi;
		fread(&xyzi, sizeof(uint32_t), 1, file);

		//we swap z and y axes, .vox files are z-up
		uint32_t x = (xyzi & 0xFF);
		uint32_t y = ((xyzi >> 16) & 0xFF);
		uint32_t z = ((xyzi >>  8) & 0xFF);

		//skip out of bounds voxels
		if((int32_t)x < bbox->xMin || (int32_t)y < bbox->zMin || (int32_t)z < bbox->yMin)
			continue;
		if((int32_t)x > bbox->xMax || (int32_t)y > bbox->zMax || (int32_t)z > bbox->yMax)
			continue;

		x -= bbox->xMin;
		y -= bbox->zMin;
		z -= bbox->yMin;

		uint32_t xMap = x / SPLV_BRICK_SIZE;
		uint32_t yMap = y / SPLV_BRICK_SIZE;
		uint32_t zMap = z / SPLV_BRICK_SIZE;
		uint32_t idxMap = splv_frame_get_map_idx(frame, xMap, yMap, zMap);

		if(frame->map[idxMap] == SPLV_BRICK_IDX_EMPTY)
		{
			SPLVbrick* newBrick = splv_frame_get_next_brick(frame);
			splv_brick_clear(newBrick);

			SPLVerror pushError = splv_frame_push_next_brick(frame, xMap, yMap, zMap);
			if(pushError != SPLV_SUCCESS)
				return pushError;
		}

		SPLVbrick* brick = &frame->bricks[frame->map[idxMap]];
		uint32_t xBrick = x % SPLV_BRICK_SIZE;
		uint32_t yBrick = y % SPLV_BRICK_SIZE;
		uint32_t zBrick = z % SPLV_BRICK_SIZE;

		uint32_t color = palette[((xyzi >> 24) & 0xFF) - 1];
		uint8_t r = color & 0xFF;
		uint8_t g = (color >> 8)  & 0xFF;
		uint8_t b = (color >> 16) & 0xFF;

		splv_brick_set_voxel_filled(brick, xBrick, yBrick, zBrick, r, g, b);
	}

	//return:
	//---------------
	return SPLV_SUCCESS;
}

SPLVvoxChunk _splv_vox_read_chunk(FILE* file)
{
	SPLVvoxChunk chunk;
	fread(&chunk, 3 * sizeof(uint32_t), 1, file); //read id, size, childSize
	chunk.endPtr = (uint32_t)ftell(file) + chunk.len + chunk.childLen;

	return chunk;
}

SPLVerror _splv_vox_read_dict(FILE* file, SPLVvoxDict* dict)
{
	dict->keys = NULL;
	dict->vals = NULL;

	uint32_t numEntries;
	fread(&numEntries, sizeof(uint32_t), 1, file);

	dict->numEntries = numEntries;
	dict->keys = (char**)SPLV_MALLOC(numEntries * sizeof(char*));
	dict->vals = (char**)SPLV_MALLOC(numEntries * sizeof(char*));
	if(!dict->keys || !dict->vals)
	{
		SPLV_LOG_ERROR("failed to allocate memory for vox dictionary");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	//so we dont free any unallocated strings
	memset(dict->keys, 0, numEntries * sizeof(char*));
	memset(dict->vals, 0, numEntries * sizeof(char*));

	for(uint32_t i = 0; i < numEntries; i++)
	{
		uint32_t strLenKey;
		fread(&strLenKey, sizeof(uint32_t), 1, file);
		char* key = (char*)SPLV_MALLOC(strLenKey + 1);
		fread(key, strLenKey, 1, file);
		key[strLenKey] = '\0';

		uint32_t strLenVal;
		fread(&strLenVal, sizeof(uint32_t), 1, file);
		char* val = (char*)SPLV_MALLOC(strLenVal + 1);
		fread(val, strLenVal, 1, file);
		val[strLenVal] = '\0';

		if(!key || !val)
		{
			SPLV_LOG_ERROR("failed to allocate memory for vox dictionary entry");
			return SPLV_ERROR_OUT_OF_MEMORY;
		}

		dict->keys[i] = key;
		dict->vals[i] = val;
	}

	return SPLV_SUCCESS;
}

void _splv_vox_free_dict(SPLVvoxDict dict)
{
	if(dict.keys != NULL)
	{
		for(uint32_t i = 0; i < dict.numEntries; i++)
		{
			if(dict.keys[i] != NULL)
				SPLV_FREE(dict.keys[i]);
		}

		SPLV_FREE(dict.keys);
	}

	if(dict.vals != NULL)
	{
		for(uint32_t i = 0; i < dict.numEntries; i++)
		{
			if(dict.vals[i] != NULL)
				SPLV_FREE(dict.vals[i]);
		}

		SPLV_FREE(dict.vals);
	}
}