#include "spatialstudio/splv_buffer_io.h"

//-------------------------------------------//

SPLVerror splv_buffer_reader_create(SPLVbufferReader* reader, uint8_t* buf, uint64_t len)
{
	reader->len = len;
	reader->buf = buf;
	reader->readPos = 0;

	return SPLV_SUCCESS;
}

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