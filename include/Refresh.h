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

typedef struct REFRESH_Device REFRESH_Device;
typedef struct REFRESH_Texture REFRESH_Texture;
typedef struct REFRESH_Buffer REFRESH_Buffer;
typedef struct REFRESH_ColorTarget REFRESH_ColorTarget;
typedef struct REFRESH_DepthTarget REFRESH_DepthTarget;
typedef struct REFRESH_Framebuffer REFRESH_Framebuffer;
typedef struct REFRESH_RenderPass REFRESH_RenderPass;
typedef struct REFRESH_Pipeline REFRESH_Pipeline;

typedef enum REFRESH_PrimitiveType
{
    REFRESH_PRIMITIVETYPE_POINTLIST,
	REFRESH_PRIMITIVETYPE_LINELIST,
	REFRESH_PRIMITIVETYPE_LINESTRIP,
	REFRESH_PRIMITIVETYPE_TRIANGLELIST,
	REFRESH_PRIMITIVETYPE_TRIANGLESTRIP,
    REFRESH_PRIMITIVETYPE_TRIANGLEFAN
} REFRESH_PrimitiveType;

typedef enum REFRESH_LoadOp
{
    REFRESH_LOADOP_LOAD,
    REFRESH_LOADOP_CLEAR,
    REFRESH_LOADOP_DONT_CARE
} REFRESH_LoadOp;

typedef enum REFRESH_StoreOp
{
    REFRESH_STOREOP_STORE,
    REFRESH_STOREOP_DONT_CARE
} REFRESH_StoreOp;

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

typedef struct REFRESH_Color
{
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
} REFRESH_Color;

typedef struct REFRESH_Rect
{
	int32_t x;
	int32_t y;
	int32_t w;
	int32_t h;
} REFRESH_Rect;

typedef struct REFRESH_Vec4
{
	float x;
	float y;
	float z;
	float w;
} REFRESH_Vec4;

typedef struct REFRESH_Viewport
{
	int32_t x;
	int32_t y;
	int32_t w;
	int32_t h;
	float minDepth;
	float maxDepth;
} REFRESH_Viewport;

typedef enum REFRESH_VertexElementFormat
{
	REFRESH_VERTEXELEMENTFORMAT_SINGLE,
	REFRESH_VERTEXELEMENTFORMAT_VECTOR2,
	REFRESH_VERTEXELEMENTFORMAT_VECTOR3,
	REFRESH_VERTEXELEMENTFORMAT_VECTOR4,
	REFRESH_VERTEXELEMENTFORMAT_COLOR,
	REFRESH_VERTEXELEMENTFORMAT_BYTE4,
	REFRESH_VERTEXELEMENTFORMAT_SHORT2,
	REFRESH_VERTEXELEMENTFORMAT_SHORT4,
	REFRESH_VERTEXELEMENTFORMAT_NORMALIZEDSHORT2,
	REFRESH_VERTEXELEMENTFORMAT_NORMALIZEDSHORT4,
	REFRESH_VERTEXELEMENTFORMAT_HALFVECTOR2,
	REFRESH_VERTEXELEMENTFORMAT_HALFVECTOR4
} REFRESH_VertexElementFormat;

typedef struct REFRESH_VertexElement
{
	int32_t offset;
	REFRESH_VertexElementFormat vertexElementFormat;
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

/* Drawing */

/* Clears the active draw buffers of any previous contents.
 *
 * options:	Bitflags to specify color/depth/stencil buffers for clearing.
 * color:	The new value of the cleared color buffer.
 * depth:	The new value of the cleared depth buffer.
 * stencil:	The new value of the cleared stencil buffer.
 */
REFRESHAPI void REFRESH_Clear(
	REFRESH_Device *device,
	REFRESH_Vec4 **colors,
    uint32_t colorCount,
	float depth,
	int32_t stencil
);

/* Draws data from vertex/index buffers.
 *
 * primitiveType:	The primitive topology of the vertex data.
 * baseVertex:		The starting offset to read from the vertex buffer.
 * minVertexIndex:	The lowest index value expected from the index buffer.
 * numVertices:		The highest offset expected from the index buffer.
 * startIndex:		The starting offset to read from the index buffer.
 * primitiveCount:	The number of primitives to draw.
 * indices:		The index buffer to bind for this draw call.
 * indexElementSize:	The size of the index type for this index buffer.
 */
REFRESHAPI void REFRESH_DrawIndexedPrimitives(
	REFRESH_Device *device,
	REFRESH_PrimitiveType primitiveType,
	int32_t baseVertex,
	int32_t minVertexIndex,
	int32_t numVertices,
	int32_t startIndex,
	int32_t primitiveCount,
	REFRESH_Buffer *indices,
	REFRESH_IndexElementSize indexElementSize
);

/* Draws data from vertex/index buffers with instancing enabled.
 *
 * primitiveType:	The primitive topology of the vertex data.
 * baseVertex:		The starting offset to read from the vertex buffer.
 * minVertexIndex:	The lowest index value expected from the index buffer.
 * numVertices:		The highest offset expected from the index buffer.
 * startIndex:		The starting offset to read from the index buffer.
 * primitiveCount:	The number of primitives to draw.
 * instanceCount:	The number of instances that will be drawn.
 * indices:		The index buffer to bind for this draw call.
 * indexElementSize:	The size of the index type for this index buffer.
 */
REFRESHAPI void REFRESH_DrawInstancedPrimitives(
	REFRESH_Device *device,
	REFRESH_PrimitiveType primitiveType,
	int32_t baseVertex,
	int32_t minVertexIndex,
	int32_t numVertices,
	int32_t startIndex,
	int32_t primitiveCount,
	int32_t instanceCount,
	REFRESH_Buffer *indices,
	REFRESH_IndexElementSize indexElementSize
);

/* Draws data from vertex buffers.
 * primitiveType:	The primitive topology of the vertex data.
 * vertexStart:		The starting offset to read from the vertex buffer.
 * primitiveCount:	The number of primitives to draw.
 */
REFRESHAPI void REFRESH_DrawPrimitives(
	REFRESH_Device *device,
	REFRESH_PrimitiveType primitiveType,
	int32_t vertexStart,
	int32_t primitiveCount
);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* REFRESH_H */

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
