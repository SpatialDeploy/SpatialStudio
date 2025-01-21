#include "splv_utils.h"

#include <fstream>
#include <math.h>
#include "splv_log.h"
#include "splv_encoder.h"

//-------------------------------------------//

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
	std::ifstream firstFile(paths[0], std::ios::binary);
	if(!firstFile.is_open())
	{
		SPLV_LOG_ERROR("failed to open input file for concatenation");
		return SPLV_ERROR_FILE_OPEN;
	}

	SPLVfileHeader firstHeader;
	firstFile.read((char*)&firstHeader, sizeof(SPLVfileHeader));

	firstFile.close();

	//open output file:
	//---------------
	std::ofstream outFile(outPath, std::ios::binary);
	if(!outFile.is_open())
	{
		SPLV_LOG_ERROR("failed to open input file for concatenation");
		return SPLV_ERROR_FILE_OPEN;
	}

	SPLVfileHeader outHeader = {0};
	outFile.write((const char*)&outHeader, sizeof(SPLVfileHeader)); //write empty header, will rewrite when done

	//create frame table + scratch buf:
	//---------------
	uint64_t frameCount = 0;

	const uint64_t FRAME_PTR_INITIAL_CAP = 64;
	uint64_t framePtrCap = FRAME_PTR_INITIAL_CAP;
	uint64_t* framePtrs = (uint64_t*)SPLV_MALLOC(framePtrCap * sizeof(uint64_t));
	if(!framePtrs)
	{
		outFile.close();
		
		SPLV_LOG_ERROR("failed to create frame ptr array");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	const uint64_t SCRATCH_BUF_INITIAL_SIZE = 1000000; //1MB
	uint64_t scratchBufSize = SCRATCH_BUF_INITIAL_SIZE;
	uint8_t* scratchBuf = (uint8_t*)SPLV_MALLOC(scratchBufSize);
	if(!scratchBuf)
	{
		outFile.close();

		SPLV_LOG_ERROR("failed to create scratch buffer");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	//read each file, concatenate:
	//---------------
	uint64_t writePos = outFile.tellp();

	for(uint32_t i = 0; i < numPaths; i++)
	{
		//ensure output file is valid
		if(!outFile.good())
		{
			SPLV_LOG_ERROR("error writing to output file during concatenation");
			return SPLV_ERROR_FILE_WRITE;
		}

		//open file
		std::ifstream inFile(paths[i], std::ios::binary);
		if(!inFile.is_open())
		{
			outFile.close();

			SPLV_LOG_ERROR("failed to open input file for concatenation");
			return SPLV_ERROR_FILE_OPEN;
		}

		//read header + validate
		SPLVfileHeader inHeader;
		inFile.read((char*)&inHeader, sizeof(SPLVfileHeader));

		if(inHeader.width != firstHeader.width || inHeader.height != firstHeader.height || inHeader.depth != firstHeader.depth)
		{
			inFile.close();
			outFile.close();

			SPLV_LOG_ERROR("attempting to concatenate spatials with mismatched dimensions");
			return SPLV_ERROR_INVALID_INPUT;
		}
		if(fabsf(inHeader.framerate - firstHeader.framerate) > 0.01f)
			SPLV_LOG_WARNING("attempting to concatenate spatials with mismatched framerates, taking framerate of earlier spatial...");

		//copy frames
		inFile.seekg(0, std::ios::end);
		uint64_t totalSize = inFile.tellg();
		inFile.seekg(sizeof(SPLVfileHeader), std::ios::beg);

		uint64_t frameTableSize = inHeader.frameCount * sizeof(uint64_t);
		uint64_t framesSize = totalSize - sizeof(SPLVfileHeader) - frameTableSize;

		if(scratchBufSize < framesSize)
		{
			while(scratchBufSize < framesSize)
				scratchBufSize *= 2;
			
			scratchBuf = (uint8_t*)SPLV_REALLOC(scratchBuf, scratchBufSize);
			if(!scratchBuf)
			{
				inFile.close();
				outFile.close();

				SPLV_LOG_ERROR("failed to realloc scratch buffer for concatenation");
				return SPLV_ERROR_OUT_OF_MEMORY;
			}
		}

		inFile.read((char*)scratchBuf, framesSize);
		outFile.write((const char*)scratchBuf, framesSize);

		//copy frame table
		if(framePtrCap < frameCount + inHeader.frameCount)
		{
			while(framePtrCap < frameCount + inHeader.frameCount)
				framePtrCap *= 2;

			framePtrs = (uint64_t*)SPLV_REALLOC(framePtrs, framePtrCap * sizeof(uint64_t));
			if(!framePtrs)
			{
				inFile.close();
				outFile.close();

				SPLV_LOG_ERROR("failed to realloc frame ptr buffer for concatenation");
				return SPLV_ERROR_OUT_OF_MEMORY;
			}
		}

		inFile.read((char*)&framePtrs[frameCount], inHeader.frameCount * sizeof(uint64_t));
		for(uint64_t j = frameCount; j < frameCount + inHeader.frameCount; j++)
			framePtrs[j] += writePos - sizeof(SPLVfileHeader);

		frameCount += inHeader.frameCount;

		//close file
		writePos = outFile.tellp();
		inFile.close();
	}

	//write frame table:
	//---------------
	uint64_t frameTablePtr = outFile.tellp();
	outFile.write((const char*)framePtrs, frameCount * sizeof(uint64_t));

	//write header:
	//---------------
	outHeader.width = firstHeader.width;
	outHeader.height = firstHeader.height;
	outHeader.depth = firstHeader.depth;
	outHeader.framerate = firstHeader.framerate;
	outHeader.frameCount = (uint32_t)frameCount;
	outHeader.duration = (float)frameCount / firstHeader.framerate;
	outHeader.frameTablePtr = frameTablePtr;

	outFile.seekp(std::ios::beg);
	outFile.write((const char*)&outHeader, sizeof(SPLVfileHeader));

	//cleanup:
	//---------------
	outFile.close();

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

	//open input file, read metadata:
	//---------------
	std::ifstream inFile(path, std::ios::binary);
	if(!inFile.is_open())
	{
		SPLV_LOG_ERROR("failed to open input file for splitting");
		return SPLV_ERROR_FILE_OPEN;
	}

	SPLVfileHeader inHeader;
	inFile.read((char*)&inHeader, sizeof(SPLVfileHeader));

	//calculate frames per split:
	//---------------
	uint32_t framesPerSplit = (uint32_t)(splitLength * inHeader.framerate);
	if(framesPerSplit == 0)
	{
		inFile.close();
	  
		SPLV_LOG_ERROR("split length too small, would result in 0 frames per split");
		return SPLV_ERROR_INVALID_ARGUMENTS;
	}

	*numSplits = (inHeader.frameCount + framesPerSplit - 1) / framesPerSplit;

	//read frame table:
	//---------------
	uint64_t* framePtrs = (uint64_t*)SPLV_MALLOC((inHeader.frameCount + 1) * sizeof(uint64_t));
	if(!framePtrs)
	{
		inFile.close();

		SPLV_LOG_ERROR("failed to allocate frame pointer array");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	inFile.seekg(inHeader.frameTablePtr, std::ios::beg);
	inFile.read((char*)framePtrs, inHeader.frameCount * sizeof(uint64_t));
	
	inFile.seekg(0, std::ios::end);
	framePtrs[inHeader.frameCount] = inFile.tellg();

	//create scratch buffer:
	//---------------
	const uint64_t SCRATCH_BUF_INITIAL_SIZE = 1000000; //1MB
	uint64_t scratchBufSize = SCRATCH_BUF_INITIAL_SIZE;
	uint8_t* scratchBuf = (uint8_t*)SPLV_MALLOC(scratchBufSize);
	if(!scratchBuf)
	{
		inFile.close();

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

		std::ofstream outFile(outPath, std::ios::binary);
		if(!outFile.is_open())
		{
			inFile.close();

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
			
			scratchBuf = (uint8_t*)SPLV_REALLOC(scratchBuf, scratchBufSize);
			if(!scratchBuf)
			{
				inFile.close();
				outFile.close();

				SPLV_LOG_ERROR("failed to realloc scratch buffer for concatenation");
				return SPLV_ERROR_OUT_OF_MEMORY;
			}
		}

		//write header
		SPLVfileHeader splitHeader;
		splitHeader.width = inHeader.width;
		splitHeader.height = inHeader.height;
		splitHeader.depth = inHeader.depth;
		splitHeader.framerate = inHeader.framerate;
		splitHeader.frameCount = splitFrameCount;
		splitHeader.duration = (float)splitFrameCount / inHeader.framerate;
		splitHeader.frameTablePtr = sizeof(SPLVfileHeader) + frameDataSize;

		outFile.write((const char*)&splitHeader, sizeof(SPLVfileHeader));

		//write frame data
		inFile.seekg(framePtrs[startFrame], std::ios::beg);
		inFile.read((char*)scratchBuf, frameDataSize);
		outFile.write((const char*)scratchBuf, frameDataSize);

		//update + write frame table
		inFile.seekg(inHeader.frameTablePtr + startFrame * sizeof(uint64_t), std::ios::beg);
		inFile.read((char*)scratchBuf, frameTableSize);

		uint64_t firstPtr = *((uint64_t*)scratchBuf);

		for(uint32_t j = 0; j < splitFrameCount; j++)
			((uint64_t*)scratchBuf)[j] -= firstPtr - sizeof(SPLVfileHeader);

		outFile.write((const char*)scratchBuf, frameTableSize);

		//close output file
		outFile.close();
	}

	//cleanup:
	//---------------
	inFile.close();

	SPLV_FREE(scratchBuf);
	SPLV_FREE(framePtrs);

	return SPLV_SUCCESS;
}