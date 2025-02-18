#include "spatialstudio/splv_error.h"

//-------------------------------------------//

const char* splv_get_error_string(SPLVerror error)
{
	switch(error)
	{
	case SPLV_SUCCESS:
		return "success";
	case SPLV_ERROR_INVALID_ARGUMENTS:
		return "invalid arguments";
	case SPLV_ERROR_INVALID_INPUT:
		return "invalid input";
	case SPLV_ERROR_OUT_OF_MEMORY:
		return "out of memory";
	case SPLV_ERROR_FILE_OPEN:
		return "file open";
	case SPLV_ERROR_FILE_READ:
		return "file read";
	case SPLV_ERROR_FILE_WRITE:
		return "file write";
	case SPLV_ERROR_RUNTIME:
		return "runtime";
	case SPLV_ERROR_INTERNAL:
		return "internal";
	default:
		return "unknown error";
	}
}