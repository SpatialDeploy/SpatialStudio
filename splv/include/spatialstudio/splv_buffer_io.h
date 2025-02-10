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
SPLV_API inline SPLVerror splv_buffer_reader_read(SPLVbufferReader* reader, uint64_t size, void* dst);

/**
 * sets the read position for a buffer reader
 */
SPLV_API inline SPLVerror splv_buffer_reader_seek(SPLVbufferReader* reader, uint64_t pos);

//-------------------------------------------//

/**
 * initalizes a buffer writer, must call splv_buffer_writer_destroy() when finished to free memory
 */
SPLV_API SPLVerror splv_buffer_writer_create(SPLVbufferWriter* writer, uint64_t initialLen);

/**
 * destroys a buffer writer, frees resources allocated from splv_buffer_writer_create()
 */
SPLV_API inline void splv_buffer_writer_destroy(SPLVbufferWriter* writer);

/**
 * writes to a buffer writer
 */
SPLV_API inline SPLVerror splv_buffer_writer_write(SPLVbufferWriter* writer, uint64_t size, void* src);

/**
 * writes a single byte to a writer
 */
SPLV_API inline SPLVerror splv_buffer_writer_put(SPLVbufferWriter* writer, uint8_t c);

/**
 * reset's a writers write position without freeing its memory
 */
SPLV_API inline void splv_buffer_writer_reset(SPLVbufferWriter* writer);

#endif //#ifndef SPLV_BUFFER_IO_H