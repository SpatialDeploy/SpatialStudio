#include "spatialstudio/splv_utils.h"

#include <math.h>
#include "spatialstudio/splv_log.h"
#include "spatialstudio/splv_encoder.h"
#include "spatialstudio/splv_decoder.h"
#include "spatialstudio/splv_decoder_legacy.h"

//-------------------------------------------//

typedef struct SPLVframeRef
{
	SPLVframe frame;
	uint64_t idx;

	uint32_t refCount;
} SPLVframeRef;

typedef struct SPLVencoderSequential
{
	SPLVencoder impl;

	uint64_t maxFrameRefs;
	uint64_t numFrameRefs;
	SPLVframeRef** frameRefs;
} SPLVencoderSequential;

typedef struct SPLVdecoderSequential
{
	uint8_t legacy;
	struct
	{
		SPLVdecoder impl;
		SPLVdecoderLegacy implLegacy;
	};

	uint64_t curFrame;

	uint64_t maxFrameRefs;
	uint64_t numFrameRefs;
	SPLVframeRef** frameRefs;
} SPLVdecoderSequential;

//-------------------------------------------//

static void _splv_frame_ref_add(SPLVframeRef* ref);
static void _splv_frame_ref_remove(SPLVframeRef* ref);

static SPLVerror _splv_encoder_sequential_create(SPLVencoderSequential* encoder, uint32_t width, uint32_t height, uint32_t depth, 
                                                 float framerate, SPLVencodingParams encodingParams, const char* outPath);
static SPLVerror _splv_encoder_sequential_finish(SPLVencoderSequential* encoder);
static void _splv_encoder_sequential_abort(SPLVencoderSequential* encoder);
static SPLVerror _splv_encoder_sequential_encode_frame(SPLVencoderSequential* encoder, SPLVframeRef* frame);

static SPLVerror _splv_decoder_sequential_create(SPLVdecoderSequential* decoder, const char* path);
static SPLVerror _splv_decoder_sequential_create_legacy(SPLVdecoderSequential* decoder, const char* path);
static void _splv_decoder_sequential_destroy(SPLVdecoderSequential* decoder);
static SPLVerror _splv_decoder_sequential_decode(SPLVdecoderSequential* decoder, SPLVframeRef** frame);

//-------------------------------------------//

SPLVerror splv_file_concat(uint32_t numPaths, const char** paths, const char* outPath)
{
	//validate:
	//---------------
	SPLV_ASSERT(numPaths > 0, "no input paths specified");

	//open first file to get metadata:
	//---------------
	SPLVdecoder firstDecoder;
	SPLVerror firstDecoderError = splv_decoder_create_from_file(&firstDecoder, paths[0]);
	if(firstDecoderError != SPLV_SUCCESS) 
		return firstDecoderError;

	uint32_t width = firstDecoder.width;
	uint32_t height = firstDecoder.height;
	uint32_t depth = firstDecoder.depth;
	float framerate = firstDecoder.framerate;
	SPLVencodingParams encodingParams = firstDecoder.encodingParams;

	splv_decoder_destroy(&firstDecoder);

	//create output encoder:
	//---------------
	SPLVencoderSequential encoder;
	SPLVerror encoderError = _splv_encoder_sequential_create(&encoder, width, height, depth, framerate, encodingParams, outPath);
	if(encoderError != SPLV_SUCCESS)
		return encoderError;

	//process files:
	//---------------
	for(uint32_t i = 0; i < numPaths; i++) 
	{
		SPLVdecoderSequential decoder;
		SPLVerror decoderError = _splv_decoder_sequential_create(&decoder, paths[i]);
		if(decoderError != SPLV_SUCCESS)
		{
			_splv_encoder_sequential_abort(&encoder);

			return decoderError;
		}

		if(decoder.impl.width != width || decoder.impl.height != height || decoder.impl.depth != depth) 
		{
			_splv_decoder_sequential_destroy(&decoder);
			_splv_encoder_sequential_abort(&encoder);

			SPLV_LOG_ERROR("input files have mismaatched dimensions");
			return SPLV_ERROR_INVALID_INPUT;
		}

		if(fabsf(decoder.impl.framerate - framerate) > 0.1f)
			SPLV_LOG_WARNING("framerate mismatch for concatenated spatials");

		for(uint32_t j = 0; j < decoder.impl.frameCount; j++) 
		{
			SPLVframeRef* frame;
			
			SPLVerror decodeError = _splv_decoder_sequential_decode(&decoder, &frame);
			if(decodeError != SPLV_SUCCESS) 
			{
				_splv_decoder_sequential_destroy(&decoder);
				_splv_encoder_sequential_abort(&encoder);

				return decodeError;
			}

			SPLVerror encodeError = _splv_encoder_sequential_encode_frame(&encoder, frame);
			if(encodeError != SPLV_SUCCESS) 
			{
				_splv_decoder_sequential_destroy(&decoder);
				_splv_encoder_sequential_abort(&encoder);

				return encodeError;
			}
		}

		_splv_decoder_sequential_destroy(&decoder);
	}

	//finish encoding:
	//---------------
	SPLVerror finishError = _splv_encoder_sequential_finish(&encoder);
	if(finishError != SPLV_SUCCESS)
		return finishError;

	return SPLV_SUCCESS;
}

SPLVerror splv_file_split(const char* path, float splitLength, const char* outDir, uint32_t* numSplits) 
{
	//validate:
	//---------------
	SPLV_ASSERT(splitLength > 0.0f, "split length must be positive");

	//open input decoder:
	//---------------
	SPLVdecoderSequential decoder;
	SPLVerror decoderError = _splv_decoder_sequential_create(&decoder, path);
	if(decoderError != SPLV_SUCCESS)
		return decoderError;

	//calculate frames per split:
	//---------------
	uint32_t framesPerSplit = (uint32_t)(splitLength * decoder.impl.framerate);
	if(framesPerSplit == 0) 
	{
		_splv_decoder_sequential_destroy(&decoder);
	
		SPLV_LOG_ERROR("split length too small, would lead to 0 frames per split");
		return SPLV_ERROR_INVALID_INPUT;
	}

	*numSplits = (decoder.impl.frameCount + framesPerSplit - 1) / framesPerSplit;

	//process each split:
	//---------------
	for(uint32_t splitIdx = 0; splitIdx < *numSplits; splitIdx++) 
	{
		char outPath[1024];
		snprintf(outPath, sizeof(outPath), "%s/split_%04d.splv", outDir, splitIdx);

		SPLVencoderSequential encoder;
		SPLVerror encoderError = _splv_encoder_sequential_create(
			&encoder,
			decoder.impl.width, decoder.impl.height, decoder.impl.depth,
			decoder.impl.framerate, decoder.impl.encodingParams,
			outPath
		);

		if(encoderError != SPLV_SUCCESS) 
		{
			_splv_decoder_sequential_destroy(&decoder);

			return encoderError;
		}

		uint32_t startFrame = splitIdx * framesPerSplit;
		uint32_t endFrame = startFrame + framesPerSplit;
		if(endFrame > decoder.impl.frameCount)
			endFrame = decoder.impl.frameCount;

		for(uint32_t frameIdx = startFrame; frameIdx < endFrame; frameIdx++) 
		{
			SPLVframeRef* frame;

			SPLVerror decodeError = _splv_decoder_sequential_decode(&decoder, &frame);
			if(decodeError != SPLV_SUCCESS) 
			{
				_splv_encoder_sequential_abort(&encoder);
				_splv_decoder_sequential_destroy(&decoder);

				return decodeError;
			}

			SPLVerror encodeError = _splv_encoder_sequential_encode_frame(&encoder, frame);
			if(encodeError != SPLV_SUCCESS) 
			{
				_splv_encoder_sequential_abort(&encoder);
				_splv_decoder_sequential_destroy(&decoder);

				return encodeError;
			}
		}

		SPLVerror finishError = _splv_encoder_sequential_finish(&encoder);
		if(finishError != SPLV_SUCCESS)
		{
			_splv_decoder_sequential_destroy(&decoder);

			return finishError;
		}
	}

	//cleanup + return:
	//---------------
	_splv_decoder_sequential_destroy(&decoder);

	return SPLV_SUCCESS;
}

SPLVerror splv_file_upgrade(const char* path, const char* outPath)
{
	//create decoder + encoder:
	//---------------
	SPLVdecoderSequential decoder;
	SPLVerror decoderError = _splv_decoder_sequential_create_legacy(&decoder, path);
	if(decoderError != SPLV_SUCCESS)
		return decoderError;

	SPLVencoderSequential encoder;
	SPLVerror encoderError = _splv_encoder_sequential_create(
		&encoder,
		decoder.implLegacy.width, decoder.implLegacy.height, decoder.implLegacy.depth,
		decoder.implLegacy.framerate, decoder.implLegacy.encodingParams,
		outPath
	);
	if(encoderError != SPLV_SUCCESS) 
	{
		_splv_decoder_sequential_destroy(&decoder);
		return encoderError;
	}

	//reencode frames:
	//---------------
	for(uint32_t i = 0; i < decoder.implLegacy.frameCount; i++) 
	{
		SPLVframeRef* frame;

		SPLVerror decodeError = _splv_decoder_sequential_decode(&decoder, &frame);
		if(decodeError != SPLV_SUCCESS) 
		{
			_splv_decoder_sequential_destroy(&decoder);
			_splv_encoder_sequential_abort(&encoder);
			return decodeError;
		}

		SPLVerror encodeError = _splv_encoder_sequential_encode_frame(&encoder, frame);
		if(encodeError != SPLV_SUCCESS) 
		{
			_splv_decoder_sequential_destroy(&decoder);
			_splv_encoder_sequential_abort(&encoder);
			return encodeError;
		}
	}

	//finish encoding:
	//---------------
	SPLVerror finishError = _splv_encoder_sequential_finish(&encoder);
	if(finishError != SPLV_SUCCESS)
		return finishError;

	//cleanup + return:
	//---------------
	_splv_decoder_sequential_destroy(&decoder);

	return SPLV_SUCCESS;
}

//-------------------------------------------//

static void _splv_frame_ref_add(SPLVframeRef* ref) 
{
	ref->refCount++;
}

static void _splv_frame_ref_remove(SPLVframeRef* ref) 
{
	ref->refCount--;
	if (ref->refCount == 0) 
	{
		splv_frame_destroy(&ref->frame);
		SPLV_FREE(ref);
	}
}

static SPLVerror _splv_encoder_sequential_create(SPLVencoderSequential* encoder, uint32_t width, uint32_t height, uint32_t depth, 
                                                 float framerate, SPLVencodingParams encodingParams, const char* outPath) 
{
	SPLV_ERROR_PROPAGATE(splv_encoder_create(&encoder->impl, width, height, depth, framerate, encodingParams, outPath));

	encoder->numFrameRefs = 0;
	encoder->maxFrameRefs = 0;
	encoder->frameRefs = NULL;

	return SPLV_SUCCESS;
}

static SPLVerror _splv_encoder_sequential_finish(SPLVencoderSequential* encoder)
{
	if(encoder->frameRefs != NULL)
	{
		for (uint64_t i = 0; i < encoder->numFrameRefs; i++)
			_splv_frame_ref_remove(encoder->frameRefs[i]);

		SPLV_FREE(encoder->frameRefs);
	}

	return splv_encoder_finish(&encoder->impl);
}

static void _splv_encoder_sequential_abort(SPLVencoderSequential* encoder)
{
	if(encoder->frameRefs != NULL)
	{
		for (uint64_t i = 0; i < encoder->numFrameRefs; i++)
			_splv_frame_ref_remove(encoder->frameRefs[i]);

		SPLV_FREE(encoder->frameRefs);
	}

	splv_encoder_abort(&encoder->impl);
}

static SPLVerror _splv_encoder_sequential_encode_frame(SPLVencoderSequential* encoder, SPLVframeRef* frameRef)
{
	//encode:
	//---------------
	splv_bool_t canFree;
	SPLV_ERROR_PROPAGATE(splv_encoder_encode_frame(&encoder->impl, &frameRef->frame, &canFree));

	//add frame to refs:
	//---------------
	uint64_t newNumRefs = encoder->numFrameRefs + 1;
	if(encoder->maxFrameRefs < newNumRefs)
	{
		SPLVframeRef** newRefs;
		if(encoder->frameRefs == NULL)
			newRefs = (SPLVframeRef**)SPLV_MALLOC(newNumRefs * sizeof(SPLVframeRef*));
		else
			newRefs = (SPLVframeRef**)SPLV_REALLOC(encoder->frameRefs, newNumRefs * sizeof(SPLVframeRef*));

		if(!newRefs)
		{
			SPLV_LOG_ERROR("failed to realloc frame ref buffer");
			return SPLV_ERROR_OUT_OF_MEMORY;
		}

		encoder->maxFrameRefs = newNumRefs;
		encoder->frameRefs = newRefs;
	}

	_splv_frame_ref_add(frameRef);
	encoder->frameRefs[encoder->numFrameRefs] = frameRef;
	encoder->numFrameRefs++;

	//free if able:
	//---------------
	if(canFree) 
	{
		for(uint64_t i = 0; i < encoder->numFrameRefs; i++)
			_splv_frame_ref_remove(encoder->frameRefs[i]);
		
		encoder->numFrameRefs = 0;
	}

	return SPLV_SUCCESS;
}

static SPLVerror _splv_decoder_sequential_create(SPLVdecoderSequential* decoder, const char* path)
{
	SPLV_ERROR_PROPAGATE(splv_decoder_create_from_file(&decoder->impl, path));

	decoder->legacy = 0;
	decoder->curFrame = 0;
	decoder->numFrameRefs = 0;
	decoder->maxFrameRefs = 0;
	decoder->frameRefs = NULL;

	return SPLV_SUCCESS;
}

static SPLVerror _splv_decoder_sequential_create_legacy(SPLVdecoderSequential* decoder, const char* path) 
{
	SPLV_ERROR_PROPAGATE(splv_decoder_legacy_create_from_file(&decoder->implLegacy, path));

	decoder->legacy = 1;
	decoder->curFrame = 0;
	decoder->numFrameRefs = 0;
	decoder->maxFrameRefs = 0;
	decoder->frameRefs = NULL;

	return SPLV_SUCCESS;
}

static void _splv_decoder_sequential_destroy(SPLVdecoderSequential* decoder) 
{
 	if(decoder->frameRefs != NULL)
	{
		for(uint64_t i = 0; i < decoder->numFrameRefs; i++)
			_splv_frame_ref_remove(decoder->frameRefs[i]);

		SPLV_FREE(decoder->frameRefs);
	}

	if(decoder->legacy)
		splv_decoder_legacy_destroy(&decoder->implLegacy);
	else	
		splv_decoder_destroy(&decoder->impl);
}

static SPLVerror _splv_decoder_sequential_decode(SPLVdecoderSequential* decoder, SPLVframeRef** frame) 
{
	//get num dependencies:
	//---------------
	SPLVerror dependencyError;
	uint64_t numDependencies;

	if(decoder->legacy) 
	{
		dependencyError = splv_decoder_legacy_get_frame_dependencies(
			&decoder->implLegacy, decoder->curFrame,
			&numDependencies, NULL, 0
		);
	} 
	else 
	{
		dependencyError = splv_decoder_get_frame_dependencies(
			&decoder->impl, decoder->curFrame,
			&numDependencies, NULL, 0
		);
	}

	if(dependencyError != SPLV_SUCCESS) 
	{
		SPLV_LOG_ERROR("error getting frame dependency count");
		return dependencyError;
	}

	//get dependencies:
	//---------------
	uint64_t* dependencies = (uint64_t*)SPLV_MALLOC(numDependencies * sizeof(uint64_t));
	if(!dependencies && numDependencies > 0) 
	{
		SPLV_LOG_ERROR("error getting frame dependencies");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	if(decoder->legacy) 
	{
		dependencyError = splv_decoder_legacy_get_frame_dependencies(
			&decoder->implLegacy, decoder->curFrame,
			&numDependencies, dependencies, 0
		);
	} 
	else 
	{
		dependencyError = splv_decoder_get_frame_dependencies(
			&decoder->impl, decoder->curFrame,
			&numDependencies, dependencies, 0
		);
	}

	if(dependencyError != SPLV_SUCCESS) 
	{
		SPLV_FREE(dependencies);
		
		SPLV_LOG_ERROR("error getting frame dependencies count");
		return dependencyError;
	}

	//prepare dependency frames:
	//---------------
	SPLVframeIndexed* indexedFrames = (SPLVframeIndexed*)SPLV_MALLOC(numDependencies * sizeof(SPLVframeIndexed));
	if(!indexedFrames && numDependencies > 0) 
	{
		SPLV_FREE(dependencies);
		
		SPLV_LOG_ERROR("failed to allocate denendency frames array");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	for(uint64_t i = 0; i < numDependencies; i++) 
	{
		uint64_t depIdx = dependencies[i];
		SPLVframeRef* ref = NULL;
		
		for(uint64_t j = 0; j < decoder->numFrameRefs; j++) 
		{
			if(decoder->frameRefs[j]->idx == depIdx) 
			{
				ref = decoder->frameRefs[j];
				break;
			}
		}

		if(!ref)
			SPLV_ASSERT(0, "decoderSequential expects frames to be decodable in-order, this may change in the future though");	

		indexedFrames[i].index = depIdx;
		indexedFrames[i].frame = &ref->frame;
	}

	//decode:
	//---------------
	SPLVerror decodeError;
	SPLVframe decodedFrame;

	if(decoder->legacy) 
	{
		decodeError = splv_decoder_legacy_decode_frame(
			&decoder->implLegacy, decoder->curFrame,
			numDependencies, (SPLVframeIndexedLegacy*)indexedFrames, &decodedFrame
		);
	}
	else
	{
		decodeError = splv_decoder_decode_frame(
			&decoder->impl, decoder->curFrame,
			numDependencies, indexedFrames, &decodedFrame, NULL
		);
	}
	
	if(decodeError != SPLV_SUCCESS)
	{
		SPLV_FREE(dependencies);
		SPLV_FREE(indexedFrames);

		return decodeError;
	}

	//remove refs that are no longer needed:
	//---------------
	if(decoder->frameRefs != NULL)
	{
		for(uint32_t i = 0; i < decoder->numFrameRefs; i++)
		{
			uint8_t found = 0;
			for(uint32_t j = 0; j < numDependencies; j++)
			{
				if(decoder->frameRefs[i]->idx == dependencies[j])
				{
					found = 1;
					break;
				}
			}

			if(!found)
			{
				_splv_frame_ref_remove(decoder->frameRefs[i]);
				decoder->frameRefs[i] = decoder->frameRefs[decoder->numFrameRefs - 1];
				
				decoder->numFrameRefs--;
				i--;
			}
		}
	}

	//create new ref + add:
	//---------------
	*frame = (SPLVframeRef*)SPLV_MALLOC(sizeof(SPLVframeRef));
	if(!*frame)
	{
		SPLV_FREE(dependencies);
		SPLV_FREE(indexedFrames);

		SPLV_LOG_ERROR("failed to alloc frame ref");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	(*frame)->refCount = 0;
	(*frame)->idx = decoder->curFrame;
	(*frame)->frame = decodedFrame;

	uint64_t newNumRefs = decoder->numFrameRefs + 1;
	if(decoder->maxFrameRefs < newNumRefs)
	{
		SPLVframeRef** newRefs;
		if(decoder->frameRefs == NULL)
			newRefs = (SPLVframeRef**)SPLV_MALLOC(newNumRefs * sizeof(SPLVframeRef*));
		else
			newRefs = (SPLVframeRef**)SPLV_REALLOC(decoder->frameRefs, newNumRefs * sizeof(SPLVframeRef*));

		if(!newRefs)
		{
			SPLV_FREE(dependencies);
			SPLV_FREE(indexedFrames);

			SPLV_LOG_ERROR("failed to realloc frame ref buffer");
			return SPLV_ERROR_OUT_OF_MEMORY;
		}

		decoder->maxFrameRefs = newNumRefs;
		decoder->frameRefs = newRefs;
	}

	_splv_frame_ref_add(*frame);
	decoder->frameRefs[decoder->numFrameRefs] = *frame;
	decoder->numFrameRefs++;

	//cleanup + return:
	//---------------
	decoder->curFrame++;

	SPLV_FREE(dependencies);
	SPLV_FREE(indexedFrames);

	return SPLV_SUCCESS;
}