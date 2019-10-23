#include "LLZ4.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <memory.h>

#if defined (_MSC_VER)
#   pragma intrinsic(memset, memcpy)
#   pragma intrinsic(_BitScanForward)
#endif /* _MSC_VER */

#if !defined(LLZ4_FORCE_INLINE)
#  if defined (_MSC_VER)
#    define LLZ4_FORCE_INLINE __forceinline
#  elif defined(__GNUC__) && defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L   /* C99 */
#    define LLZ4_FORCE_INLINE inline __attribute__((always_inline))
#  else
#    define LLZ4_FORCE_INLINE inline
#  endif
#endif /* LLZ4_FORCE_INLINE */

#define LLZ4_MAX_BLOCK_SIZE 0x7E000000

#define LLZ4_MAX_OFFSET 65535
#define LLZ4_ACCELERATION_TRIGGER 6
#define LLZ4_HASH_SIZE 14

#define LLZ4_STR2(x) #x
#define LLZ4_STR(x) LLZ4_STR2(x)
#define LLZ4_VERSION_STRING LLZ4_STR(LLZ4_VERSION_MAJOR) "." LLZ4_STR(LLZ4_VERSION_MINOR) "." LLZ4_STR(LLZ4_VERSION_RELEASE)

static const char stringVersion[] = LLZ4_VERSION_STRING;

struct HashBuffer
{
    const uint8_t* data[1 << LLZ4_HASH_SIZE];
};

struct LLZ4_compressContext
{
    LLZ4_allocator_t allocator;
    struct HashBuffer hashBuffer;
};

struct LLZ4_decompressContext
{
    LLZ4_allocator_t allocator;
};

/* Internal routines */

static void* internalAlloc(void* _userData, size_t _itemsCount, size_t _itemSize)
{
    return malloc(_itemSize * _itemsCount);
}

static void internalFree(void* _userData, void* _pointer)
{
    free(_pointer);
}

static LLZ4_FORCE_INLINE void wildCopy(uint8_t* _dst, const uint8_t* _src, size_t _length)
{
    memcpy(_dst, _src, _length);
}

static LLZ4_FORCE_INLINE void wildCopy8(uint8_t* _dst, const uint8_t* _src, size_t _length)
{
    const uint8_t* end = _src + _length;

    do
    {
        memcpy(_dst, _src, 8);
        _dst += 8;
        _src += 8;
    } while (_src < end);
}

static LLZ4_FORCE_INLINE void wildCopy16(uint8_t* _dst, const uint8_t* _src, size_t _length)
{
    const uint8_t* end = _src + _length;

    do
    {
        memcpy(_dst, _src, 8);
        memcpy(_dst + 8, _src + 8, 8);
        _dst += 16;
        _src += 16;
    } while (_src < end);
}

static LLZ4_FORCE_INLINE size_t blockReadOffset(const uint8_t* _ptr, const uint8_t** _out)
{
    size_t value = _ptr[0] | (_ptr[1] << 8);
    *_out = _ptr + 2;
    return value;
}

static LLZ4_FORCE_INLINE uint8_t* blockWriteOffset(uint8_t* _ptr, size_t _offset)
{
    _ptr[0] = (uint8_t)_offset;
    _ptr[1] = (uint8_t)(_offset >> 8);
    return _ptr + 2;
}

static LLZ4_FORCE_INLINE size_t blockReadLength(const uint8_t* _ptr, const uint8_t* _end, const uint8_t** _out)
{
    size_t length = 0;
    size_t value = 0;

    while (_ptr < _end)
    {
        value = *_ptr++;
        length += value;

        if (value < 255)
            break;
    };

    *_out = _ptr;
    return length;
}

static LLZ4_FORCE_INLINE uint8_t* blockWriteLength(uint8_t* _ptr, size_t _length)
{
    while (_length >= 255)
    {
        *_ptr++ = 255;
        _length -= 255;
    }

    *_ptr++ = (uint8_t)_length;
    return _ptr;
}


/* Compress routines */

static LLZ4_FORCE_INLINE const uint8_t* updateHash(const uint8_t* _ptr, struct HashBuffer* _buffer)
{
    uint32_t value;

    memcpy(&value, _ptr, sizeof(value));
    const uint32_t hash = (value * 2654435761) >> (32 - LLZ4_HASH_SIZE);
    const uint8_t* result = _buffer->data[hash];

    _buffer->data[hash] = _ptr;
    return result;
}

static LLZ4_FORCE_INLINE size_t matchCount(const uint8_t* _matchPtr, const uint8_t* _matchEnd, const uint8_t* _offsetPtr)
{
    const uint8_t* ptr = _matchPtr;
    uint32_t diff = 0, a, b;
    size_t count = 0;

    while (ptr < _matchEnd)
    {
        memcpy(&a, ptr, sizeof(diff));
        memcpy(&b, _offsetPtr, sizeof(diff));

        diff = a ^ b;

        if (diff)
        {
#if defined(_MSC_VER)
            unsigned long bits;
            _BitScanForward(&bits, diff);
            count = bits >> 3;
#elif defined (__clang__) || defined (__GNUC__)
            count = __builtin_ctz(diff) >> 3;
#else
#   error "Compiler is not compatible"
#endif
            break;
        }

        ptr += sizeof(diff);
        _offsetPtr += sizeof(diff);
    }

    count += ptr - _matchPtr;
    return count;
}

static int compressBlock(const uint8_t* _inBegin, const uint8_t* _inEnd, uint8_t* _outBegin, uint8_t* _outEnd, const uint8_t** _inNext, uint8_t** _outNext, struct HashBuffer* _buffer, int _acceleration, bool _thorough)
{
    const uint8_t* inPtr = _inBegin;
    uint8_t* outPtr = _outBegin;
    int errorCode = LLZ4_ERROR_OK;

    if (_acceleration > 0 && (inPtr + 13) < _inEnd)
    {
        const uint8_t* endLimit = _inEnd - 13;
        const uint8_t* endMatch = _inEnd - 5;
        const uint8_t* inLast = inPtr++;

        const int initialStepSize = _acceleration << LLZ4_ACCELERATION_TRIGGER;
        int stepSize = initialStepSize;
        int step = 1;

        memset(_buffer, 0, sizeof(struct HashBuffer));

        updateHash(inLast, _buffer);

        while (inPtr < endLimit)
        {
            const uint8_t* matchPtr = updateHash(inPtr, _buffer);

            if (matchPtr && (matchPtr + LLZ4_MAX_OFFSET + 1) > inPtr && memcmp(matchPtr, inPtr, 4) == 0)
            {
                const size_t matchLength = matchCount(inPtr + 4, endMatch, matchPtr + 4);
                const size_t literalsLength = inPtr - inLast;
                const size_t fullLength = 1 + (literalsLength + 240) / 255 + literalsLength + 2 + (matchLength + 240) / 255 + 5;

                if ((outPtr + fullLength) > _outEnd)
                {
                    errorCode = LLZ4_ERROR_NOT_ENOUGH_OF_SPACE;
                    break;
                }

                uint8_t token = matchLength < 15 ? (uint8_t)matchLength : 15;

                if (literalsLength == 0)
                {
                    *outPtr++ = token;
                }
                else if (literalsLength < 15)
                {
                    token |= ((uint8_t)literalsLength) << 4;
                    *outPtr++ = token;
                    wildCopy8(outPtr, inLast, literalsLength);
                    outPtr += literalsLength;
                }
                else
                {
                    token |= 15 << 4;
                    *outPtr++ = token;
                    outPtr = blockWriteLength(outPtr, literalsLength - 15);
                    wildCopy8(outPtr, inLast, literalsLength);
                    outPtr += literalsLength;
                }

                outPtr = blockWriteOffset(outPtr, inPtr - matchPtr);

                if (matchLength >= 15)
                    outPtr = blockWriteLength(outPtr, matchLength - 15);

                const uint8_t* nextPtr = inPtr + matchLength + 4;

                if (_thorough)
                {
                    stepSize = initialStepSize;
                    step = 1;

                    if ((inPtr + LLZ4_MAX_OFFSET + 1) > nextPtr)
                        inPtr++;
                    else
                        inPtr = nextPtr - LLZ4_MAX_OFFSET;

                    while (inPtr < nextPtr)
                    {
                        updateHash(inPtr, _buffer);

                        inPtr += step;
                        step = (stepSize++) >> LLZ4_ACCELERATION_TRIGGER;
                    }
                }

                inLast = nextPtr;
                inPtr = nextPtr;

                stepSize = initialStepSize;
                step = 1;
            }
            else
            {
                inPtr += step;
                step = (stepSize++) >> LLZ4_ACCELERATION_TRIGGER;
            }
        }

        inPtr = inLast;
    }

    if (errorCode == LLZ4_ERROR_OK)
    {
        const size_t literalsLength = _inEnd - inPtr;
        const size_t fullLength = 1 + (literalsLength + 240) / 255 + literalsLength;

        if ((outPtr + fullLength) <= _outEnd)
        {
            if (literalsLength < 15)
            {
                *outPtr++ = (uint8_t)(literalsLength << 4);
            }
            else
            {
                *outPtr++ = 15 << 4;
                outPtr = blockWriteLength(outPtr, literalsLength - 15);
            }

            wildCopy(outPtr, inPtr, literalsLength);
            outPtr += literalsLength;
        }
        else
        {
            errorCode = LLZ4_ERROR_NOT_ENOUGH_OF_SPACE;
        }
    }

    *_inNext = inPtr;
    *_outNext = outPtr;

    return errorCode;
}


/* Decompress routines */

static int decompressBlock(const uint8_t* _inBegin, const uint8_t* _inEnd, uint8_t* _outBegin, uint8_t* _outEnd, const uint8_t** _inNext, uint8_t** _outNext)
{
    const uint8_t* inPtr = _inBegin;
    uint8_t* outPtr = _outBegin;
    int errorCode = LLZ4_ERROR_INVALID_DATA;

    while (inPtr < _inEnd)
    {
        const int token = *inPtr++;
        size_t length = token >> 4;
        
        if (length == 15)
            length = blockReadLength(inPtr, _inEnd - 15, &inPtr) + 15;

        if ((inPtr + length + 15) <= _inEnd && (outPtr + length + 15) <= _outEnd)
        {
            wildCopy16(outPtr, inPtr, length);
            inPtr += length;
            outPtr += length;
        }
        else if ((inPtr + length + 2) < _inEnd && (outPtr + length) <= _outEnd)
        {
            wildCopy(outPtr, inPtr, length);
            inPtr += length;
            outPtr += length;
        }
        else if ((inPtr + length) == _inEnd && (outPtr + length) <= _outEnd)
        {
            wildCopy(outPtr, inPtr, length);
            inPtr = _inEnd;
            outPtr += length;
            errorCode = LLZ4_ERROR_OK;
            break;
        }
        else if ((outPtr + length) > _outEnd)
        {
            errorCode = LLZ4_ERROR_NOT_ENOUGH_OF_SPACE;
            break;
        }
        else
        {
            break;
        }

        const size_t offset = blockReadOffset(inPtr, &inPtr);
        if (offset == 0 || (outPtr + offset) < _outBegin)
            break;

        length = (token & 15) + 4;

        if (length == 19)
            length = blockReadLength(inPtr, _inEnd, &inPtr) + 19;

        if (length <= offset && (outPtr + length + 15) <= _outEnd)
        {
            wildCopy16(outPtr, outPtr - offset, length);
            outPtr += length;
        }
        else if (length <= offset && (outPtr + length) <= _outEnd)
        {
            wildCopy(outPtr, outPtr - offset, length);
            outPtr += length;
        }
        else if ((outPtr + length) > _outEnd)
        {
            errorCode = LLZ4_ERROR_NOT_ENOUGH_OF_SPACE;
            break;
        }
        else
        {
            const uint8_t* from = outPtr;
            const uint8_t* end = outPtr + length;

            wildCopy(outPtr, outPtr - offset, offset);
            outPtr += offset;

            do
            {
                *outPtr++ = *from++;
            } while (outPtr < end);
        }
    }

    *_inNext = inPtr;
    *_outNext = outPtr;

    return errorCode;
}


/* Library routines */

int LLZ4_getVersion(void)
{
    return LLZ4_VERSION_NUMBER;
}

const char* LLZ4_getVersionString(void)
{
    return stringVersion;
}

int LLZ4_createCompressContext(LLZ4_compressContext_t** _context, LLZ4_allocator_t* _allocator)
{
    LLZ4_compressContext_t* context = NULL;
    LLZ4_allocator_t allocator = { 0 };

    if (_context == NULL)
        return LLZ4_ERROR_INVALID_CONTEXT;

    if (_allocator != NULL)
    {
        allocator = *_allocator;
    }
    else
    {
        allocator.allocFunc = &internalAlloc;
        allocator.freeFunc = &internalFree;
        allocator.opaque = NULL;
    }

    if (allocator.allocFunc == NULL || allocator.freeFunc == NULL)
        return LLZ4_ERROR_INVALID_PARAMS;

    context = (LLZ4_compressContext_t*)allocator.allocFunc(allocator.opaque, 1, sizeof(LLZ4_compressContext_t));
    if (context == NULL)
        return LLZ4_ERROR_NOT_ENOUGH_OF_MEMORY;

    context->allocator = allocator;
    *_context = context;
    return LLZ4_ERROR_OK;
}

int LLZ4_destroyCompressContext(LLZ4_compressContext_t* _context)
{
    LLZ4_allocator_t allocator = { 0 };

    if (_context == NULL)
        return LLZ4_ERROR_INVALID_CONTEXT;

    allocator = _context->allocator;
    allocator.freeFunc(allocator.opaque, _context);
    return LLZ4_ERROR_OK;
}

int LLZ4_createDecompressContext(LLZ4_decompressContext_t** _context, LLZ4_allocator_t* _allocator)
{
    LLZ4_decompressContext_t* context = NULL;
    LLZ4_allocator_t allocator = { 0 };

    if (_context == NULL)
        return LLZ4_ERROR_INVALID_CONTEXT;

    if (_allocator != NULL)
    {
        allocator = *_allocator;
    }
    else
    {
        allocator.allocFunc = &internalAlloc;
        allocator.freeFunc = &internalFree;
        allocator.opaque = NULL;
    }

    if (allocator.allocFunc == NULL || allocator.freeFunc == NULL)
        return LLZ4_ERROR_INVALID_PARAMS;

    context = (LLZ4_decompressContext_t*)allocator.allocFunc(allocator.opaque, 1, sizeof(LLZ4_decompressContext_t));
    if (context == NULL)
        return LLZ4_ERROR_NOT_ENOUGH_OF_MEMORY;

    context->allocator = allocator;
    *_context = context;
    return LLZ4_ERROR_OK;
}

int LLZ4_destroyDecompressContext(LLZ4_decompressContext_t* _context)
{
    LLZ4_allocator_t allocator = { 0 };

    if (_context == NULL)
        return LLZ4_ERROR_INVALID_CONTEXT;

    allocator = _context->allocator;
    allocator.freeFunc(allocator.opaque, _context);
    return LLZ4_ERROR_OK;
}

size_t LLZ4_compressBlockBound(size_t _inputSize)
{
    if (_inputSize > LLZ4_MAX_BLOCK_SIZE)
        return 0;

    return 1 + _inputSize + (_inputSize + 240) / 255;
}

int LLZ4_compressBlock(LLZ4_compressContext_t* _context, const void* _inData, size_t _inSize, void* _outBuffer, size_t _bufferSize, int _compressionLevel)
{
    if (_context == NULL)
        return LLZ4_ERROR_INVALID_CONTEXT;

    if (_inData == NULL || _inSize < 0 || _inSize > LLZ4_MAX_BLOCK_SIZE)
        return LLZ4_ERROR_INVALID_PARAMS;

    if (_outBuffer == NULL || _bufferSize < 0)
        return LLZ4_ERROR_INVALID_PARAMS;

    const uint8_t* inBegin = (const uint8_t*)_inData;
    const uint8_t* inEnd = inBegin + _inSize;
    const uint8_t* inPtr = NULL;
    uint8_t* outBegin = (uint8_t*)_outBuffer;
    uint8_t* outEnd = outBegin + _bufferSize;
    uint8_t* outPtr = NULL;
    int accelerationLevel = 1;
    bool through = false;

    if (_compressionLevel < 0)
        _compressionLevel = 1;

    switch (_compressionLevel)
    {
    case 0:
        accelerationLevel = 0;
        through = false;
        break;
    case 1:
        accelerationLevel = 8;
        through = false;
        break;
    case 2:
        accelerationLevel = 7;
        through = false;
        break;
    case 3:
        accelerationLevel = 6;
        through = false;
        break;
    case 4:
        accelerationLevel = 5;
        through = false;
        break;
    case 5:
        accelerationLevel = 4;
        through = false;
        break;
    case 6:
        accelerationLevel = 3;
        through = false;
        break;
    case 7:
        accelerationLevel = 2;
        through = false;
        break;
    case 8:
        accelerationLevel = 1;
        through = false;
        break;
    case 9:
    default:
        accelerationLevel = 1;
        through = true;
        break;
    }
    
    int errorCode = compressBlock(inBegin, inEnd, outBegin, outEnd, &inPtr, &outPtr, &_context->hashBuffer, accelerationLevel, through);

    if (errorCode != LLZ4_ERROR_OK)
        return errorCode;

    return (int)(outPtr - outBegin);
}

int LLZ4_decompressBlock(LLZ4_decompressContext_t* _context, const void* _inData, size_t _inSize, void* _outBuffer, size_t _bufferSize)
{
    if (_context == NULL)
        return LLZ4_ERROR_INVALID_CONTEXT;

    if (_inData == NULL || _inSize < 0 || _inSize > LLZ4_MAX_BLOCK_SIZE)
        return LLZ4_ERROR_INVALID_PARAMS;

    if (_outBuffer == NULL || _bufferSize < 0)
        return LLZ4_ERROR_INVALID_PARAMS;

    const uint8_t* inBegin = (const uint8_t*)_inData;
    const uint8_t* inEnd = inBegin + _inSize;
    const uint8_t* inPtr = NULL;
    uint8_t* outBegin = (uint8_t*)_outBuffer;
    uint8_t* outEnd = outBegin + _bufferSize;
    uint8_t* outPtr = NULL;

    int errorCode = decompressBlock(inBegin, inEnd, outBegin, outEnd, &inPtr, &outPtr);

    if (errorCode != LLZ4_ERROR_OK)
        return errorCode;

    return (int)(outPtr - outBegin);
}
