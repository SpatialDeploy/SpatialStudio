#include "spatialstudio/splv_range_coder.h"

#include "spatialstudio/splv_log.h"
#include <string.h>

//----------------------------------------------------------------------//

#define SPLV_RC_STATE_BITS 40
#define SPLV_RC_STATE_MASK ((1ull << SPLV_RC_STATE_BITS) - 1)
#define SPLV_RC_PROB_BITS 24

#define SPLV_RC_MAX_RANGE ((1ull << SPLV_RC_STATE_BITS) - 1)
#define SPLV_RC_MIN_RANGE ((1ull << SPLV_RC_PROB_BITS) - 1)
#define SPLV_RC_MAX_SYMBOLS SPLV_RC_MIN_RANGE

#define SPLV_RC_NUM_DIGIT_BITS 8
#define SPLV_RC_NORM_SHIFT (SPLV_RC_STATE_BITS - SPLV_RC_NUM_DIGIT_BITS)
#define SPLV_RC_NORM_MASK ((1ull << SPLV_RC_NORM_SHIFT) - 1)

#define SPLV_RC_NUM_SYMBOLS 257 //256 possible bytes + EOF
#define SPLV_RC_EOF 256

//----------------------------------------------------------------------//

typedef struct SPLVrcFreqTable
{
	uint32_t total;

	uint32_t frequencies[SPLV_RC_NUM_SYMBOLS];
	uint32_t cumulative[SPLV_RC_NUM_SYMBOLS + 1];
} SPLVrcFreqTable;

typedef struct SPLVrcEncoder
{
	uint64_t low;
	uint64_t range;

	uint64_t startWriteIdx;
} SPLVrcEncoder;

typedef struct SPLVrcDecoder
{
	uint64_t low;
	uint64_t range;
	uint64_t code;

	uint64_t bytesRead;
	uint64_t totalBytes;
} SPLVrcDecoder;

//----------------------------------------------------------------------//

static inline void _splv_rc_freq_table_init(SPLVrcFreqTable* table);
static inline void _splv_rc_freq_table_calculate_cdf(SPLVrcFreqTable* table);

static inline void _splv_rc_encoder_init(SPLVrcEncoder* enc);
static inline SPLVerror _splv_rc_encoder_start(SPLVrcEncoder* enc, SPLVbufferWriter* out);
static inline SPLVerror _splv_rc_encoder_encode(SPLVrcEncoder* enc, SPLVrcFreqTable* table, SPLVbufferWriter* out, uint32_t symbol);
static inline SPLVerror _splv_rc_encoder_finish(SPLVrcEncoder* enc, SPLVbufferWriter* out);

static inline void _splv_rc_decoder_init(SPLVrcDecoder* dec);
static inline uint8_t _splv_rc_decoder_read_digit(SPLVrcDecoder* dec, uint8_t* inBuf);
static inline SPLVerror _splv_rc_decoder_start(SPLVrcDecoder* dec, uint64_t inBufLen, uint8_t** inBuf);
static inline uint32_t _splv_rc_decoder_decode(SPLVrcDecoder* dec, SPLVrcFreqTable* table, uint8_t* inBuf);

//----------------------------------------------------------------------//

SPLVerror splv_rc_encode(uint64_t inBufLen, uint8_t* inBuf, SPLVbufferWriter* out)
{
	//validate:
	//-----------------
	if(inBufLen > SPLV_RC_MAX_SYMBOLS)
	{
		SPLV_LOG_ERROR("data is too large to encode, must have size less than SPLV_RC_MAX_SYMBOLS");
		return SPLV_ERROR_INVALID_INPUT;
	}

	//generate frequency table:
	//-----------------
	SPLVrcFreqTable table;
	_splv_rc_freq_table_init(&table);

	for(uint32_t i = 0; i < inBufLen; i++)
	{
		uint8_t symbol = inBuf[i];
		table.frequencies[symbol]++;
	}
	
	table.frequencies[SPLV_RC_EOF] = 1;

	_splv_rc_freq_table_calculate_cdf(&table);

	//write frequency table:
	//-----------------
	SPLV_ERROR_PROPAGATE(splv_buffer_writer_write(out, SPLV_RC_NUM_SYMBOLS * sizeof(uint32_t), table.frequencies));

	//encode:
	//-----------------
	SPLVrcEncoder enc;
	_splv_rc_encoder_init(&enc);

	SPLV_ERROR_PROPAGATE(_splv_rc_encoder_start(&enc, out));

	for(uint32_t i = 0; i < inBufLen; i++)
	{
		uint32_t symbol = (uint32_t)inBuf[i];
		SPLV_ERROR_PROPAGATE(_splv_rc_encoder_encode(&enc, &table, out, symbol));
	}

	SPLV_ERROR_PROPAGATE(_splv_rc_encoder_encode(&enc, &table, out, SPLV_RC_EOF));
	SPLV_ERROR_PROPAGATE(_splv_rc_encoder_finish(&enc, out));

	return SPLV_SUCCESS;
}

SPLVerror splv_rc_decode(uint64_t inBufLen, uint8_t* inBuf, SPLVbufferWriter* out)
{
	//read frequency table:
	//-----------------
	if(inBufLen < SPLV_RC_NUM_SYMBOLS * sizeof(uint32_t))
	{
		SPLV_LOG_ERROR("in buffer not large enough to hold frequency data");
		return SPLV_ERROR_INVALID_INPUT;
	}

	SPLVrcFreqTable table;

	memcpy(table.frequencies, inBuf, SPLV_RC_NUM_SYMBOLS * sizeof(uint32_t));
	inBuf += SPLV_RC_NUM_SYMBOLS * sizeof(uint32_t);
	inBufLen -= SPLV_RC_NUM_SYMBOLS * sizeof(uint32_t);

	_splv_rc_freq_table_calculate_cdf(&table);

	if(table.total > SPLV_RC_MAX_SYMBOLS)
	{
		SPLV_LOG_ERROR("data is too large to decode, must have size less than SPLV_RC_MAX_SYMBOLS");
		return SPLV_ERROR_INVALID_INPUT;
	}

	//decompress:
	//-----------------
	SPLVrcDecoder dec;
	_splv_rc_decoder_init(&dec);
	
	SPLV_ERROR_PROPAGATE(_splv_rc_decoder_start(&dec, inBufLen, &inBuf));

	while(1)
	{
		uint32_t symbol = _splv_rc_decoder_decode(&dec, &table, inBuf);
		if(symbol == SPLV_RC_EOF)
			break;

		splv_buffer_writer_put(out, (uint8_t)symbol);
	}

	return SPLV_SUCCESS;
}

//----------------------------------------------------------------------//

static inline void _splv_rc_freq_table_init(SPLVrcFreqTable* table)
{
	for(uint32_t i = 0; i < SPLV_RC_NUM_SYMBOLS; i++)
	{
		table->frequencies[i] = 0;
		table->cumulative[i] = 0;
	}

	table->cumulative[SPLV_RC_NUM_SYMBOLS] = 0;
	table->total = 0;
}

static inline void _splv_rc_freq_table_calculate_cdf(SPLVrcFreqTable* table)
{
	table->cumulative[0] = 0;
	for(uint32_t i = 1; i < SPLV_RC_NUM_SYMBOLS + 1; i++)
		table->cumulative[i] = table->cumulative[i - 1] + table->frequencies[i - 1];

	table->total = table->cumulative[SPLV_RC_NUM_SYMBOLS];
}

//----------------------------------------------------------------------//

static inline void _splv_rc_encoder_init(SPLVrcEncoder* enc)
{
	enc->low = 0;
	enc->range = SPLV_RC_MAX_RANGE;
	enc->startWriteIdx = 0;
}

static inline SPLVerror _splv_rc_encoder_start(SPLVrcEncoder* enc, SPLVbufferWriter* out)
{
	enc->startWriteIdx = out->writePos;

	uint64_t dummySize = 0;
	SPLV_ERROR_PROPAGATE(splv_buffer_writer_write(out, sizeof(uint64_t), &dummySize));

	return SPLV_SUCCESS;
}

static inline SPLVerror _splv_rc_encoder_encode(SPLVrcEncoder* enc, SPLVrcFreqTable* table, SPLVbufferWriter* out, uint32_t symbol)
{
	//get frequency table values:
	//---------------
	uint32_t symLow  = table->cumulative[symbol];
	uint32_t symHigh = table->cumulative[symbol + 1];
	uint32_t symFreq = symHigh - symLow;

	//update interval:
	//---------------
	uint64_t newLow = enc->low + (symLow * enc->range) / table->total;
	uint64_t newRange = (enc->range * symFreq) / table->total;
	enc->low = newLow;
	enc->range = newRange;

	//renorm if range too small:
	//---------------
	while(enc->range < SPLV_RC_MIN_RANGE)
	{
		uint8_t topDigit = (uint8_t)(enc->low >> SPLV_RC_NORM_SHIFT);
		SPLV_ERROR_PROPAGATE(splv_buffer_writer_put(out, topDigit));
		
		if((enc->low & SPLV_RC_NORM_MASK) + enc->range <= SPLV_RC_NORM_MASK)
		{
			enc->low   = (enc->low   << SPLV_RC_NUM_DIGIT_BITS) & SPLV_RC_STATE_MASK;
			enc->range = (enc->range << SPLV_RC_NUM_DIGIT_BITS) & SPLV_RC_STATE_MASK;
		}
		else
		{
			enc->low   = (enc->low   << SPLV_RC_NUM_DIGIT_BITS) & SPLV_RC_STATE_MASK;
			enc->range = SPLV_RC_MAX_RANGE - enc->low;
		}
	}

	return SPLV_SUCCESS;
}

static inline SPLVerror _splv_rc_encoder_finish(SPLVrcEncoder* enc, SPLVbufferWriter* out)
{
	//normalize:
	//---------------
	while((enc->low & SPLV_RC_NORM_MASK) + enc->range <= SPLV_RC_NORM_MASK) 
	{
		uint8_t topDigit = (uint8_t)(enc->low >> SPLV_RC_NORM_SHIFT);
		SPLV_ERROR_PROPAGATE(splv_buffer_writer_put(out, topDigit));

		enc->low   = (enc->low   << SPLV_RC_NUM_DIGIT_BITS) & SPLV_RC_STATE_MASK;
		enc->range = (enc->range << SPLV_RC_NUM_DIGIT_BITS) & SPLV_RC_STATE_MASK;
	}
	
	//write remaining digits of code:
	//---------------
	uint64_t code = enc->low + enc->range / 2;
	while(code > 0)
	{
		uint8_t topDigit = (uint8_t)(code >> SPLV_RC_NORM_SHIFT);
		SPLV_ERROR_PROPAGATE(splv_buffer_writer_put(out, topDigit));

		code = (code << SPLV_RC_NUM_DIGIT_BITS) & SPLV_RC_STATE_MASK;
	}

	//write total compressed size:
	//---------------
	uint64_t curPos = out->writePos;
	uint64_t size = curPos - enc->startWriteIdx - sizeof(uint64_t);

	memcpy(&out->buf[enc->startWriteIdx], &size, sizeof(uint64_t));

	return SPLV_SUCCESS;
}

//----------------------------------------------------------------------//

static inline void _splv_rc_decoder_init(SPLVrcDecoder* dec)
{
	dec->low = 0;
	dec->range = SPLV_RC_MAX_RANGE;
	dec->code = 0;
	dec->bytesRead = 0;
	dec->totalBytes = 0;
}

static inline uint8_t _splv_rc_decoder_read_digit(SPLVrcDecoder* dec, uint8_t* inBuf)
{
	if(dec->bytesRead >= dec->totalBytes)
		return 0;

	return (uint8_t)inBuf[dec->bytesRead++];
}

static inline SPLVerror _splv_rc_decoder_start(SPLVrcDecoder* dec, uint64_t inBufLen, uint8_t** inBuf)
{
	//read total bytes:
	//---------------
	if(inBufLen < sizeof(uint64_t))
	{
		SPLV_LOG_ERROR("in buffer not large enough to hold neccesary metadata for decoding");
		return SPLV_ERROR_INVALID_INPUT;
	}

	memcpy(&dec->totalBytes, *inBuf, sizeof(uint64_t));
	*inBuf += sizeof(uint64_t);

	//validate inbuf is large enough:
	//---------------
	if(inBufLen - sizeof(uint64_t) < dec->totalBytes)
	{
		SPLV_LOG_ERROR("in buffer not large enough to hold all encoded data");
		return SPLV_ERROR_OUT_OF_MEMORY;
	}

	//fill code with initial digits:
	//---------------
	dec->code = 0;
	for(int i = 0; i < SPLV_RC_STATE_BITS / SPLV_RC_NUM_DIGIT_BITS; i++)
	{
		uint8_t digit = _splv_rc_decoder_read_digit(dec, *inBuf);
			
		dec->code = (dec->code << SPLV_RC_NUM_DIGIT_BITS) | digit;
	}

	return SPLV_SUCCESS;
}

static inline uint32_t _splv_rc_decoder_decode(SPLVrcDecoder* dec, SPLVrcFreqTable* table, uint8_t* inBuf)
{
	//binary search to find symbol:
	//---------------
	uint64_t offset = dec->code - dec->low;
	uint64_t value = ((offset + 1) * table->total - 1) / dec->range;

	uint32_t start = 0;
	uint32_t end = SPLV_RC_NUM_SYMBOLS;
	while(end - start > 1) 
	{
		uint32_t middle = (start + end) >> 1;
		if (table->cumulative[middle] > value)
			end = middle;
		else
			start = middle;
	}

	uint32_t symbol = start;

	//get frequency table values:
	//---------------
	uint32_t symLow = table->cumulative[symbol];
	uint32_t symHigh = table->cumulative[symbol + 1];
	uint32_t symFreq = symHigh - symLow;

	//update interval:
	//---------------
	uint64_t newLow = dec->low + (symLow * dec->range) / table->total;
	uint64_t newRange = (dec->range * symFreq) / table->total;
	dec->low = newLow;
	dec->range = newRange;

	//renorm if range too small:
	//---------------
	while(dec->range < SPLV_RC_MIN_RANGE)
	{
		if((dec->low & SPLV_RC_NORM_MASK) + dec->range <= SPLV_RC_NORM_MASK)
		{
			dec->low = (dec->low << SPLV_RC_NUM_DIGIT_BITS) & SPLV_RC_STATE_MASK;
			dec->range = (dec->range << SPLV_RC_NUM_DIGIT_BITS) & SPLV_RC_STATE_MASK;
		}
		else
		{
			dec->low = (dec->low << SPLV_RC_NUM_DIGIT_BITS) & SPLV_RC_STATE_MASK;
			dec->range = SPLV_RC_MAX_RANGE - dec->low;
		}
		
		uint8_t digit = _splv_rc_decoder_read_digit(dec, inBuf);
		dec->code = ((dec->code << SPLV_RC_NUM_DIGIT_BITS) | digit) & SPLV_RC_STATE_MASK;
	}

	return symbol;
}