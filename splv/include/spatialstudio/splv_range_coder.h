/* splv_range_coder.h
 *
 * contains SPLV's entropy coder implementation: a range-coder
 */

#ifndef SPLV_RANGE_CODER_H
#define SPLV_RANGE_CODER_H

#include <stdint.h>
#include "spatialstudio/splv_error.h"
#include "spatialstudio/splv_log.h"
#include "spatialstudio/splv_buffer_io.h"

//----------------------------------------------------------------------//

/**
 * performs range coding encoding, reading raw data from inBuf and outputting encoded data to out
 */
SPLVerror splv_rc_encode(uint64_t inBufLen, uint8_t* inBuf, SPLVbufferWriter* out);

/**
 * performs range coding encoding, reading raw data from inBuf and outputting encoded data to out
 */
SPLVerror splv_rc_decode(uint64_t inBufLen, uint8_t* inBuf, SPLVbufferWriter* out);

#endif //#ifndef SPLV_RANGE_CODER_H