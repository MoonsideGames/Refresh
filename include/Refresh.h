/* Refresh - XNA-inspired 3D Graphics Library with modern capabilities
 *
 * Copyright (c) 2020 Evan Hemsley
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from
 * the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software in a
 * product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * Evan "cosmonaut" Hemsley <evan@moonside.games>
 *
 */

#include <FNA3D.h>

#ifndef REFRESH_H
#define REFRESH_H

#ifdef _WIN32
#define REFRESHAPI __declspec(dllexport)
#define REFRESHCALL __cdecl
#else
#define REFRESHAPI
#define REFRESHCALL
#endif

/* -Wpedantic nameless union/struct silencing */
#ifndef REFRESHNAMELESS
#ifdef __GNUC__
#define REFRESHNAMELESS __extension__
#else
#define REFRESHNAMELESS
#endif /* __GNUC__ */
#endif /* REFRESHNAMELESS */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Type Declarations */

typedef struct REFRESH_Texture REFRESH_Texture;
typedef struct REFRESH_Buffer REFRESH_Buffer;
typedef struct REFRESH_RenderBuffer REFRESH_RenderBuffer;

typedef enum REFRESH_ClearOptions
{
    REFRESH_CLEAROPTIONS_TARGET  = 1,
    REFRESH_CLEAROPTIONS_DEPTH   = 2,
    REFRESH_CLEAROPTIONS_STENCIL = 4,
} REFRESH_ClearOptions;

typedef enum REFRESH_IndexElementSize
{
    REFRESH_INDEXELEMENTSIZE_16BIT,
    REFRESH_INDEXELEMENTSIZE_32BIT
} REFRESH_IndexElementSize;

typedef enum REFRESH_SurfaceFormat
{
    REFRESH_SURFACEFORMAT_R8G8B8A8,
    REFRESH_SURFACEFORMAT_R5G6B5,
    REFRESH_SURFACEFORMAT_A1R5G5B5,
    REFRESH_SURFACEFORMAT_B4G4R4A4,
    REFRESH_SURFACEFORMAT_BC1,
    REFRESH_SURFACEFORMAT_BC2,
    REFRESH_SURFACEFORMAT_BC3,
    REFRESH_SURFACEFORMAT_R8G8_SNORM,
    REFRESH_SURFACEFORMAT_R8G8B8A8_SNORM,
    REFRESH_SURFACEFORMAT_A2R10G10B10,
    REFRESH_SURFACEFORMAT_R16G16,
    REFRESH_SURFACEFORMAT_R16G16B16A16,
    REFRESH_SURFACEFORMAT_R8,
    REFRESH_SURFACEFORMAT_R32_SFLOAT,
    REFRESH_SURFACEFORMAT_R32G32_SFLOAT,
    REFRESH_SURFACEFORMAT_R32G32B32A32_SFLOAT,
    REFRESH_SURFACEFORMAT_R16_SFLOAT,
    REFRESH_SURFACEFORMAT_R16G16_SFLOAT,
    REFRESH_SURFACEFORMAT_R16G16B16A16_SFLOAT
} REFRESH_SurfaceFormat;

typedef enum REFRESH_DepthFormat
{
    REFRESH_DEPTHFORMAT_D16_UNORM_S8_UINT,
    REFRESH_DEPTHFORMAT_D24_UNORM_S8_UINT,
    REFRESH_DEPTHFORMAT_D32_SFLOAT_S8_UINT
} REFRESH_DepthFormat;

typedef enum REFRESH_CubeMapFace
{
    REFRESH_CUBEMAPFACE_POSITIVEX,
    REFRESH_CUBEMAPFACE_NEGATIVEX,
    REFRESH_CUBEMAPFACE_POSITIVEY,
    REFRESH_CUBEMAPFACE_NEGATIVEY,
    REFRESH_CUBEMAPFACE_POSITIVEZ,
    REFRESH_CUBEMAPFACE_NEGATIVEZ
} REFRESH_CubeMapFace;

typedef enum REFRESH_BufferUsage
{
    REFRESH_BUFFERUSAGE_STATIC,
    REFRESH_BUFFERUSAGE_DYNAMIC
} REFRESH_BufferUsage;

/* Structures */

typedef struct REFRESH_VertexElement
{
	int32_t offset;
	FNA3D_VertexElementFormat vertexElementFormat;
	int32_t usageIndex;
} REFRESH_VertexElement;

typedef struct REFRESH_VertexDeclaration
{
	int32_t vertexStride;
	int32_t elementCount;
	REFRESH_VertexElement *elements;
} REFRESH_VertexDeclaration;

typedef struct REFRESH_VertexBufferBinding
{
	REFRESH_Buffer *vertexBuffer;
	REFRESH_VertexDeclaration vertexDeclaration;
	int32_t vertexOffset;
	int32_t instanceFrequency;
} REFRESH_VertexBufferBinding;

typedef struct REFRESH_RenderTargetBinding
{
    #define REFRESH_RENDERTARGET_TYPE_2D 0
    #define REFRESH_RENDERTARGET_TYPE_3D 1
    #define REFRESH_RENDERTARGET_TYPE_CUBE 2
    uint8_t type;
    REFRESHNAMELESS union
    {
        struct
        {
            uint32_t width;
            uint32_t height;
        } twod;
        struct
        {
            uint32_t width;
            uint32_t height;
            uint32_t layer;
        } threed;
        struct
        {
            uint32_t size;
            REFRESH_CubeMapFace face;
        } cube;
    };
    uint32_t levelCount;
    uint32_t multiSampleCount;
    REFRESH_RenderBuffer *renderBuffer;
    REFRESH_Texture *texture;
} REFRESH_RenderTargetBinding;

/* Version API */

#define REFRESH_ABI_VERSION	 0
#define REFRESH_MAJOR_VERSION   0
#define REFRESH_MINOR_VERSION	1
#define REFRESH_PATCH_VERSION	0

#define REFRESH_COMPILED_VERSION ( \
	(REFRESH_ABI_VERSION * 100 * 100 * 100) + \
	(REFRESH_MAJOR_VERSION * 100 * 100) + \
	(REFRESH_MINOR_VERSION * 100) + \
	(REFRESH_PATCH_VERSION) \
)

REFRESHAPI uint32_t REFRESH_LinkedVersion(void);

/* Functions */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* REFRESH_H */

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
