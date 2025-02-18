/* splv_error.h
 *
 * contains an enum representing all possible error states
 */

#ifndef SPLV_ERROR_H
#define SPLV_ERROR_H

#include "splv_global.h"

//-------------------------------------------//

/**
 * all possible error states
 */
typedef enum SPLVerror
{
	SPLV_SUCCESS = 0,
	SPLV_ERROR_INVALID_ARGUMENTS,
	SPLV_ERROR_INVALID_INPUT,
	SPLV_ERROR_OUT_OF_MEMORY,
	SPLV_ERROR_FILE_OPEN,
	SPLV_ERROR_FILE_READ,
	SPLV_ERROR_FILE_WRITE,
	SPLV_ERROR_RUNTIME,
	SPLV_ERROR_INTERNAL,
	SPLV_ERROR_THREADING
} SPLVerror;

//-------------------------------------------//

/**
 * retruns a string describing an error
 */
SPLV_API const char* splv_get_error_string(SPLVerror error);

#endif //#ifndef SPLV_ERROR_H