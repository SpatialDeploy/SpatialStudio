/* splv_buffer_io.h
 *
 * contains helper functions for reading/writing from/to a buffer
 */

#ifndef SPLV_BUFFER_IO_H
#define SPLV_BUFFER_IO_H

#include <stdint.h>
#include <string.h>
#include "splv_error.h"
#include "splv_log.h"

//-------------------------------------------//

/**
 * a utility object for reading from a buffer
 */
typedef struct SPLVbufferReader
{
	uint64_t len;
	uint8_t* buf;

	uint64_t readPos;
} SPLVbufferReader;

/**
 * a utility object for writing to a buffer
 */
typedef struct SPLVbufferWriter
{
	uint64_t len;
	uint8_t* buf;

	uint64_t writePos;
} SPLVbufferWriter;

//-------------------------------------------//

/**
 * initializes a buffer reader
 */
SPLV_API SPLVerror splv_buffer_reader_create(SPLVbufferReader* reader, uint8_t* buf, uint64_t len);

/**
 * reads from a buffer reader
 */
inline SPLVerror splv_buffer_reader_read(SPLVbufferReader* reader, uint64_t size, void* dst)
{
	if(size + reader->readPos > reader->len)
	{
		SPLV_LOG_ERROR("trying to read past end of buffer");
		return SPLV_ERROR_FILE_READ;
	}

	memcpy(dst, &reader->buf[reader->readPos], size);
	reader->readPos += size;

	return SPLV_SUCCESS;
}

/**
 * sets the read position for a buffer reader
 */
inline SPLVerror splv_buffer_reader_seek(SPLVbufferReader* reader, uint64_t pos)
{
	if(pos >= reader->len)
	{
		SPLV_LOG_ERROR("trying to seek past end of buffer");
		return SPLV_ERROR_FILE_READ;
	}

	reader->readPos = pos;
	
	return SPLV_SUCCESS;
}

//-------------------------------------------//

/**
 * initalizes a buffer writer, must call splv_buffer_writer_destroy() when finished to free memory
 */
SPLV_API SPLVerror splv_buffer_writer_create(SPLVbufferWriter* writer, uint64_t initialLen);

/**
 * destroys a buffer writer, frees resources allocated from splv_buffer_writer_create()
 */
inline void splv_buffer_writer_destroy(SPLVbufferWriter* writer)
{
	if(writer->buf)
		SPLV_FREE(writer->buf);
}

/**
 * writes to a buffer writer
 */
inline SPLVerror splv_buffer_writer_write(SPLVbufferWriter* writer, uint64_t size, void* src)
{
	if(writer->writePos + size > writer->len)
	{
		uint64_t newLen = writer->len;
		while(writer->writePos + size > newLen)
			newLen *= 2;

		uint8_t* newBuf = (uint8_t*)SPLV_REALLOC(writer->buf, newLen);
		if(!newBuf)
		{
			SPLV_LOG_ERROR("failed to realloc buffer to write to");
			return SPLV_ERROR_OUT_OF_MEMORY;
		}

		writer->buf = newBuf;
		writer->len = newLen;
	}

	memcpy(writer->buf + writer->writePos, src, size);
	writer->writePos += size;

	return SPLV_SUCCESS;
}

inline SPLVerror splv_buffer_writer_put(SPLVbufferWriter* writer, uint8_t c)
{
	if(writer->writePos + 1 > writer->len)
	{
		uint64_t newLen = writer->len * 2;
		uint8_t* newBuf = (uint8_t*)SPLV_REALLOC(writer->buf, newLen);
		if(!newBuf)
		{
			SPLV_LOG_ERROR("failed to realloc buffer to write to");
			return SPLV_ERROR_OUT_OF_MEMORY;
		}

		writer->buf = newBuf;
		writer->len = newLen;
	}

	writer->buf[writer->writePos] = c;
	writer->writePos++;

	return SPLV_SUCCESS;
}

inline void splv_buffer_writer_reset(SPLVbufferWriter* writer)
{
	writer->writePos = 0;
}

#endif //#ifndef SPLV_BUFFER_IO_H