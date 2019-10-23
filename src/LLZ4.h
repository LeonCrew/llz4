/*
    LLZ4 - Light LZ4
    Copyright (c) 2019 Alexandr Murashko. All rights reserved.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
    A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

#ifndef LLZ4_H
#define LLZ4_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stddef.h>

#define LLZ4_VERSION_MAJOR      0
#define LLZ4_VERSION_MINOR      1
#define LLZ4_VERSION_RELEASE    0

#define LLZ4_VERSION_NUMBER ((LLZ4_VERSION_MAJOR << 16) | (LLZ4_VERSION_MINOR << 8) | (LLZ4_VERSION_RELEASE))

#define LLZ4_ERROR_OK                       (0)
#define LLZ4_ERROR_NOT_ENOUGH_OF_MEMORY     (-1)
#define LLZ4_ERROR_NOT_ENOUGH_OF_SPACE      (-2)
#define LLZ4_ERROR_INVALID_PARAMS           (-3)
#define LLZ4_ERROR_INVALID_DATA             (-4)
#define LLZ4_ERROR_INVALID_CONTEXT          (-5)

#if !defined(LLZ4_EXTERN)
#   if defined(LLZ4_STATIC)
#       define LLZ4_EXTERN
#   else
#       if defined(LLZ4_DLL)
#           define LLZ4_EXTERN __declspec(dllexport)
#       else
#           define LLZ4_EXTERN __declspec(dllimport)
#       endif
#   endif /* LLZ4_STATIC */
#endif /* LLZ4_EXTERN */

#if !defined(LLZ4_APIENTRY)
#   define LLZ4_APIENTRY
#endif /* LLZ4_APIENTRY */

typedef struct LLZ4_allocator
{
    void* (*allocFunc)(void* _opaque, size_t _itemsCount, size_t _itemSize);
    void  (*freeFunc)(void* _opaque, void* _pointer);
    void* opaque;
} LLZ4_allocator_t;

typedef struct LLZ4_compressContext LLZ4_compressContext_t;
typedef struct LLZ4_decompressContext LLZ4_decompressContext_t;

LLZ4_EXTERN int LLZ4_APIENTRY LLZ4_getVersion(void);
LLZ4_EXTERN const char* LLZ4_APIENTRY LLZ4_getVersionString(void);

LLZ4_EXTERN int LLZ4_APIENTRY LLZ4_createCompressContext(LLZ4_compressContext_t** _context, LLZ4_allocator_t* _allocator);
LLZ4_EXTERN int LLZ4_APIENTRY LLZ4_destroyCompressContext(LLZ4_compressContext_t* _context);

LLZ4_EXTERN int LLZ4_APIENTRY LLZ4_createDecompressContext(LLZ4_decompressContext_t** _context, LLZ4_allocator_t* _allocator);
LLZ4_EXTERN int LLZ4_APIENTRY LLZ4_destroyDecompressContext(LLZ4_decompressContext_t* _context);

LLZ4_EXTERN size_t LLZ4_APIENTRY LLZ4_compressBlockBound(size_t _inputSize);
LLZ4_EXTERN int LLZ4_APIENTRY LLZ4_compressBlock(LLZ4_compressContext_t* _context, const void* _inData, size_t _inSize, void* _outBuffer, size_t _bufferSize, int _compressionLevel);
LLZ4_EXTERN int LLZ4_APIENTRY LLZ4_decompressBlock(LLZ4_decompressContext_t* _context, const void* _inData, size_t _inSize, void* _outBuffer, size_t _bufferSize);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* LLZ4_H */
