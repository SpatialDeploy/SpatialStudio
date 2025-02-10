#include "spatialstudio/splv_buffer_io.h"

//-------------------------------------------//

SPLVerror splv_buffer_reader_create(SPLVbufferReader* reader, uint8_t* buf, uint64_t len)
{
	reader->len = len;
	reader->buf = buf;
	reader->readPos = 0;

	return SPLV_SUCCESS;
}

SPLVerror splv_buffer_reader_read(SPLVbufferReader* reader, uint64_t size, void* dst)
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

SPLVerror splv_buffer_reader_seek(SPLVbufferReader* reader, uint64_t pos)
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

SPLVerror splv_buffer_writer_create(SPLVbufferWriter* writer, uint64_t initialLen)
{
	if(initialLen == 0)
	{
		const uint64_t DEFUALT_INITIAL_LEN = 1024;
		initialLen = DEFUALT_INITIAL_LEN;
	}

	writer->buf = (uint8_t*)SPLV_MALLOC(initialLen);
	if(!writer->buf)
	{
		SPLV_LOG_ERROR("failed to alloc buffer to write to");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	writer->len = initialLen;
	writer->writePos = 0;

	return SPLV_SUCCESS;
}

void splv_buffer_writer_destroy(SPLVbufferWriter* writer)
{
	if(writer->buf)
		SPLV_FREE(writer->buf);
}

SPLVerror splv_buffer_writer_write(SPLVbufferWriter* writer, uint64_t size, void* src)
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

SPLVerror splv_buffer_writer_put(SPLVbufferWriter* writer, uint8_t c)
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

void splv_buffer_writer_reset(SPLVbufferWriter* writer)
{
	writer->writePos = 0;
}