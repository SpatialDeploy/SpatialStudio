#include "splv_utils.h"

#include <math.h>
#include "splv_log.h"
#include "splv_encoder.h"

//-------------------------------------------//

//TODO: these functions DO NOT WORK. rewrite them when we have the decoder in this repo

SPLVerror splv_file_concat(uint32_t numPaths, const char** paths, const char* outPath)
{
	//validate params:
	//---------------
	if(numPaths == 0)
	{
		SPLV_LOG_ERROR("cannot concatenate 0 videos");
		return SPLV_ERROR_INVALID_ARGUMENTS;
	}

	//read first video metadata:
	//---------------
	FILE* firstFile = fopen(paths[0], "rb");
	if(!firstFile)
	{
		SPLV_LOG_ERROR("failed to open input file for concatenation");
		return SPLV_ERROR_FILE_OPEN;
	}

	//no need to validate, will be validated when reading first in file in loop
	SPLVfileHeader firstHeader;
	if(fread(&firstHeader, sizeof(SPLVfileHeader), 1, firstFile) < 1)
	{
		SPLV_LOG_ERROR("failed to read header from input file");
		return SPLV_ERROR_FILE_READ;
	}

	fclose(firstFile);

	//open output file:
	//---------------
	FILE* outFile = fopen(outPath, "wb");
	if(!outFile)
	{
		SPLV_LOG_ERROR("failed to open output file for concatenation");
		return SPLV_ERROR_FILE_OPEN;
	}

	SPLVfileHeader outHeader = {0};
	fwrite(&outHeader, sizeof(SPLVfileHeader), 1, outFile);

	//create frame table + scratch buf:
	//---------------
	uint64_t frameCount = 0;

	const uint64_t FRAME_PTR_INITIAL_CAP = 64;
	uint64_t framePtrCap = FRAME_PTR_INITIAL_CAP;
	uint64_t* framePtrs = (uint64_t*)SPLV_MALLOC(framePtrCap * sizeof(uint64_t));
	if(!framePtrs)
	{
		fclose(outFile);
		
		SPLV_LOG_ERROR("failed to create frame ptr array");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	const uint64_t SCRATCH_BUF_INITIAL_SIZE = 1000000; //1MB
	uint64_t scratchBufSize = SCRATCH_BUF_INITIAL_SIZE;
	uint8_t* scratchBuf = (uint8_t*)SPLV_MALLOC(scratchBufSize);
	if(!scratchBuf)
	{
		fclose(outFile);

		SPLV_LOG_ERROR("failed to create scratch buffer");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	//read each file, concatenate:
	//---------------
	long writePos = ftell(outFile);
	if(writePos == -1L)
	{
		fclose(outFile);

		SPLV_LOG_ERROR("failed to get write position of output file");
		return SPLV_ERROR_FILE_WRITE;
	}

	for(uint32_t i = 0; i < numPaths; i++)
	{
		//open file
		FILE* inFile = fopen(paths[i], "rb");
		if(!inFile)
		{
			fclose(outFile);

			SPLV_LOG_ERROR("failed to open input file for concatenation");
			return SPLV_ERROR_FILE_OPEN;
		}

		//read header + validate
		SPLVfileHeader inHeader;
		if(fread(&inHeader, sizeof(SPLVfileHeader), 1, inFile) < 1)
		{
			fclose(inFile);
			fclose(outFile);

			SPLV_LOG_ERROR("failed to read header from input file");
			return SPLV_ERROR_FILE_READ;
		}

		if(inHeader.magicWord != SPLV_MAGIC_WORD || inHeader.version != SPLV_VERSION)
		{
			fclose(inFile);
			fclose(outFile);

			SPLV_LOG_ERROR("invalid input SPLV, mismatched magic word or version");
			return SPLV_ERROR_INVALID_INPUT;
		}

		if(inHeader.width != firstHeader.width || inHeader.height != firstHeader.height || inHeader.depth != firstHeader.depth)
		{
			fclose(inFile);
			fclose(outFile);

			SPLV_LOG_ERROR("attempting to concatenate spatials with mismatched dimensions");
			return SPLV_ERROR_INVALID_INPUT;
		}

		if(fabsf(inHeader.framerate - firstHeader.framerate) > 0.01f)
			SPLV_LOG_WARNING("attempting to concatenate spatials with mismatched framerates, taking framerate of earlier spatial...");

		//copy frames
		if(fseek(inFile, 0, SEEK_END) != 0)
		{
			fclose(inFile);
			fclose(outFile);

			SPLV_LOG_ERROR("failed to seek to end of input file");
			return SPLV_ERROR_FILE_READ;
		}

		long totalSize = ftell(inFile);
		if(totalSize == -1L)
		{
			fclose(inFile);
			fclose(outFile);

			SPLV_LOG_ERROR("failed to get total size of input file");
			return SPLV_ERROR_FILE_READ;
		}

		if(fseek(inFile, sizeof(SPLVfileHeader), SEEK_SET) != 0)
		{
			fclose(inFile);
			fclose(outFile);

			SPLV_LOG_ERROR("failed to seek in input file");
			return SPLV_ERROR_FILE_READ;
		}

		uint64_t frameTableSize = inHeader.frameCount * sizeof(uint64_t);
		uint64_t framesSize = totalSize - sizeof(SPLVfileHeader) - frameTableSize;

		if(scratchBufSize < framesSize)
		{
			while(scratchBufSize < framesSize)
				scratchBufSize *= 2;
			
			scratchBuf = (uint8_t*)SPLV_REALLOC(scratchBuf, scratchBufSize);
			if(!scratchBuf)
			{
				fclose(inFile);
				fclose(outFile);

				SPLV_LOG_ERROR("failed to realloc scratch buffer for concatenation");
				return SPLV_ERROR_OUT_OF_MEMORY;
			}
		}

		if(fread(scratchBuf, 1, framesSize, inFile) < framesSize)
		{
			fclose(inFile);
			fclose(outFile);

			SPLV_LOG_ERROR("failed to read frames from input file");
			return SPLV_ERROR_FILE_READ;
		}

		if(fwrite(scratchBuf, 1, framesSize, outFile) < framesSize)
		{
			fclose(inFile);
			fclose(outFile);

			SPLV_LOG_ERROR("failed to write frames to output file");
			return SPLV_ERROR_FILE_WRITE;
		}

		//copy frame table
		if(framePtrCap < frameCount + inHeader.frameCount)
		{
			while(framePtrCap < frameCount + inHeader.frameCount)
				framePtrCap *= 2;

			framePtrs = (uint64_t*)SPLV_REALLOC(framePtrs, framePtrCap * sizeof(uint64_t));
			if(!framePtrs)
			{
				fclose(inFile);
				fclose(outFile);

				SPLV_LOG_ERROR("failed to realloc frame ptr buffer for concatenation");
				return SPLV_ERROR_OUT_OF_MEMORY;
			}
		}

		if(fread(&framePtrs[frameCount], sizeof(uint64_t), inHeader.frameCount, inFile) < inHeader.frameCount)
		{
			fclose(inFile);
			fclose(outFile);

			SPLV_LOG_ERROR("failed to read frame table from input file");
			return SPLV_ERROR_FILE_READ;
		}

		for(uint64_t j = frameCount; j < frameCount + inHeader.frameCount; j++)
			framePtrs[j] += writePos - sizeof(SPLVfileHeader);

		frameCount += inHeader.frameCount;

		//close file
		writePos = ftell(outFile);
		if(writePos == -1L)
		{
			fclose(inFile);
			fclose(outFile);

			SPLV_LOG_ERROR("failed to get write position of output file");
			return SPLV_ERROR_FILE_WRITE;
		}
		fclose(inFile);
	}

	//write frame table:
	//---------------
	uint64_t frameTablePtr = ftell(outFile);
	if(frameTablePtr == -1L)
	{
		fclose(outFile);

		SPLV_LOG_ERROR("failed to get frame table position");
		return SPLV_ERROR_FILE_WRITE;
	}

	if(fwrite(framePtrs, sizeof(uint64_t), frameCount, outFile) < frameCount)
	{
		fclose(outFile);

		SPLV_LOG_ERROR("failed to write frame table");
		return SPLV_ERROR_FILE_WRITE;
	}

	//write header:
	//---------------
	outHeader.magicWord = SPLV_MAGIC_WORD;
	outHeader.version = SPLV_VERSION;
	outHeader.width = firstHeader.width;
	outHeader.height = firstHeader.height;
	outHeader.depth = firstHeader.depth;
	outHeader.framerate = firstHeader.framerate;
	outHeader.frameCount = (uint32_t)frameCount;
	outHeader.duration = (float)frameCount / firstHeader.framerate;
	outHeader.frameTablePtr = frameTablePtr;

	if(fseek(outFile, 0, SEEK_SET) != 0)
	{
		fclose(outFile);

		SPLV_LOG_ERROR("failed to seek to beginning of output file");
		return SPLV_ERROR_FILE_WRITE;
	}

	if(fwrite(&outHeader, sizeof(SPLVfileHeader), 1, outFile) < 1)
	{
		fclose(outFile);

		SPLV_LOG_ERROR("failed to write header to output file");
		return SPLV_ERROR_FILE_WRITE;
	}

	//cleanup:
	//---------------
	fclose(outFile);

	SPLV_FREE(scratchBuf);
	SPLV_FREE(framePtrs);

	return SPLV_SUCCESS;
}

SPLVerror splv_file_split(const char* path, float splitLength, const char* outDir, uint32_t* numSplits)
{
	//validate params:
	//---------------
	if(splitLength <= 0.0f)
	{
		SPLV_LOG_ERROR("cannot split a spatial into non-positive time chunks");
		return SPLV_ERROR_INVALID_ARGUMENTS;
	}

	//open input file, read metadata, validate:
	//---------------
	FILE* inFile = fopen(path, "rb");
	if(!inFile)
	{
		SPLV_LOG_ERROR("failed to open input file for splitting");
		return SPLV_ERROR_FILE_OPEN;
	}

	SPLVfileHeader inHeader;
	if(fread(&inHeader, sizeof(SPLVfileHeader), 1, inFile) < 1)
	{
		fclose(inFile);
	
		SPLV_LOG_ERROR("failed to read header from input file");
		return SPLV_ERROR_FILE_READ;
	}

	if(inHeader.magicWord != SPLV_MAGIC_WORD || inHeader.version != SPLV_VERSION)
	{
		fclose(inFile);

		SPLV_LOG_ERROR("invalid input SPLV, mismatched magic word or version");
		return SPLV_ERROR_INVALID_INPUT;
	}

	//calculate frames per split:
	//---------------
	uint32_t framesPerSplit = (uint32_t)(splitLength * inHeader.framerate);
	if(framesPerSplit == 0)
	{
		fclose(inFile);

		SPLV_LOG_ERROR("split length too small, would result in 0 frames per split");
		return SPLV_ERROR_INVALID_ARGUMENTS;
	}

	*numSplits = (inHeader.frameCount + framesPerSplit - 1) / framesPerSplit;

	//read frame table:
	//---------------
	uint64_t* framePtrs = (uint64_t*)SPLV_MALLOC((inHeader.frameCount + 1) * sizeof(uint64_t));
	if(!framePtrs)
	{
		fclose(inFile);

		SPLV_LOG_ERROR("failed to allocate frame pointer array");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	fseek(inFile, (long)inHeader.frameTablePtr, SEEK_SET);
	fread(framePtrs, inHeader.frameCount * sizeof(uint64_t), 1, inFile);

	fseek(inFile, 0, SEEK_END);
	long fileEnd = ftell(inFile);
	framePtrs[inHeader.frameCount] = (uint64_t)fileEnd;

	//create scratch buffer:
	//---------------
	const uint64_t SCRATCH_BUF_INITIAL_SIZE = 1000000; //1MB
	uint64_t scratchBufSize = SCRATCH_BUF_INITIAL_SIZE;
	uint8_t* scratchBuf = (uint8_t*)SPLV_MALLOC(scratchBufSize);
	if(!scratchBuf)
	{
		fclose(inFile);

		SPLV_LOG_ERROR("failed to create scratch buffer");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	//create splits:
	//---------------
	for(uint32_t i = 0; i < *numSplits; i++)
	{
		//get frame range
		uint32_t startFrame = i * framesPerSplit;
		uint32_t endFrame;
		if(i == *numSplits - 1)
			endFrame = inHeader.frameCount;
		else
			endFrame = (startFrame + framesPerSplit) < inHeader.frameCount ? (startFrame + framesPerSplit) : inHeader.frameCount;

		uint32_t splitFrameCount = endFrame - startFrame;

		//open out file
		char outPath[512];
		snprintf(outPath, sizeof(outPath), "%s/split_%d.splv", outDir, i);

		FILE* outFile = fopen(outPath, "wb");
		if(!outFile)
		{
			fclose(inFile);

			SPLV_LOG_ERROR("failed to open output file for split");
			return SPLV_ERROR_FILE_OPEN;
		}

		//get size of frame data + frame table, resize scratch buf if needed
		uint64_t frameDataSize = framePtrs[endFrame] - framePtrs[startFrame];
		uint64_t frameTableSize = splitFrameCount * sizeof(uint64_t);

		if(scratchBufSize < frameDataSize || scratchBufSize < frameTableSize)
		{
			while(scratchBufSize < frameDataSize || scratchBufSize < frameTableSize)
				scratchBufSize *= 2;

			uint8_t* newBuf = (uint8_t*)SPLV_REALLOC(scratchBuf, scratchBufSize);
			if(!newBuf)
			{
				fclose(outFile);
				fclose(inFile);

				SPLV_LOG_ERROR("failed to realloc scratch buffer for splitting");
				return SPLV_ERROR_OUT_OF_MEMORY;
			}

			scratchBuf = newBuf;
		}

		//write header
		SPLVfileHeader splitHeader;
		splitHeader.magicWord = SPLV_MAGIC_WORD;
		splitHeader.version = SPLV_VERSION;
		splitHeader.width = inHeader.width;
		splitHeader.height = inHeader.height;
		splitHeader.depth = inHeader.depth;
		splitHeader.framerate = inHeader.framerate;
		splitHeader.frameCount = splitFrameCount;
		splitHeader.duration = (float)splitFrameCount / inHeader.framerate;
		splitHeader.frameTablePtr = sizeof(SPLVfileHeader) + frameDataSize;

		//writr frame data
		fwrite(&splitHeader, sizeof(SPLVfileHeader), 1, outFile);

		fseek(inFile, (long)framePtrs[startFrame], SEEK_SET);
		fread(scratchBuf, frameDataSize, 1, inFile);

		fwrite(scratchBuf, frameDataSize, 1, outFile);

		//update + write frame table
		fseek(inFile, (long)(inHeader.frameTablePtr + startFrame * sizeof(uint64_t)), SEEK_SET);

		fread(scratchBuf, sizeof(uint64_t), splitFrameCount, inFile);

		uint64_t firstPtr = *((uint64_t*)scratchBuf);
		for(uint32_t j = 0; j < splitFrameCount; j++)
			((uint64_t*)scratchBuf)[j] -= firstPtr - sizeof(SPLVfileHeader);

		fwrite(scratchBuf, sizeof(uint64_t), splitFrameCount, outFile);

		//ensure no errors reading/writing
		if(feof(outFile) || ferror(outFile))
		{
			fclose(outFile);
			fclose(inFile);

			SPLV_LOG_ERROR("error writing to split file");
			return SPLV_ERROR_FILE_WRITE;
		}

		if((i != *numSplits -1 && feof(inFile)) || ferror(inFile))
		{
			fclose(outFile);
			fclose(inFile);

			SPLV_LOG_ERROR("error reading file to split");
			return SPLV_ERROR_FILE_WRITE;
		}

		//close out file
		if(fclose(outFile) != 0)
		{
			fclose(outFile);
			fclose(inFile);

			SPLV_LOG_ERROR("error closing split file");
			return SPLV_ERROR_FILE_WRITE;
		}
	}

	//cleanup:
	//---------------
	SPLV_FREE(scratchBuf);
	SPLV_FREE(framePtrs);

	fclose(inFile);

	return SPLV_SUCCESS;
}