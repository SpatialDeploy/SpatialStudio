/* splv_log.h
 * 
 * contains functions/macros for logging
 */

#ifndef SPLV_LOG_H
#define SPLV_LOG_H

#include <stdio.h>
#include "splv_error.h"

//-------------------------------------------//

//TODO: come up with a proper logging scheme
#define SPLV_LOG_ERROR(msg) printf("SPLV ERROR: %s\n", msg)
#define SPLV_LOG_WARNING(msg) printf("SPLV WARNING: %s\n", msg)

#ifdef SPLV_DEBUG
	#define SPLV_ASSERT(c,m) if(!(c)) { printf("SPLV ASSERTION FAIL: %s\n", m); exit(-1); }
#else
	#define SPLV_ASSERT(c,m)
#endif

//-------------------------------------------//

//macro magic, need multiple layers of indirection so __LINE__ gets evaluated BEFORE being concatenated
#define SPLV_ERRORVAR_TOKENPASTE_BASE(x, y) x ## y
#define SPLV_ERRORVAR_TOKENPASTE(x, y) SPLV_ERRORVAR_TOKENPASTE_BASE(x, y)
#define SPLV_UNIQUE_ERRORVAR SPLV_ERRORVAR_TOKENPASTE(error, __LINE__)

#define SPLV_ERROR_PROPAGATE(r) SPLVerror SPLV_UNIQUE_ERRORVAR = r;      \
                                if(SPLV_UNIQUE_ERRORVAR != SPLV_SUCCESS) \
                                    return SPLV_UNIQUE_ERRORVAR;         \

#endif //#ifndef SPLV_LOG_H