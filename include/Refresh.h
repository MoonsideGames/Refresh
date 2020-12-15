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

typedef struct REFRESH_Device REFRESH_Device;
typedef struct REFRESH_Texture REFRESH_Texture;
typedef struct REFRESH_Buffer REFRESH_Buffer;
typedef struct REFRESH_Renderbuffer REFRESH_Renderbuffer;

typedef enum REFRESH_PresentMode
{
    REFRESH_PRESENTMODE_IMMEDIATE,
    REFRESH_PRESENTMODE_MAILBOX,
    REFRESH_PRESENTMODE_FIFO,
    REFRESH_PRESENTMODE_FIFO_RELAXED
} REFRESH_PresentMode;

typedef enum REFRESH_ClearOptions
{
    REFRESH_CLEAROPTIONS_TARGET  = 1,
    REFRESH_CLEAROPTIONS_DEPTH   = 2,
    REFRESH_CLEAROPTIONS_STENCIL = 4,
} REFRESH_ClearOptions;

typedef enum REFRESH_PrimitiveType
{
    REFRESH_PRIMITIVETYPE_POINTLIST,
    REFRESH_PRIMITIVETYPE_LINELIST,
    REFRESH_PRIMITIVETYPE_LINESTRIP,
    REFRESH_PRIMITIVETYPE_TRIANGLELIST,
    REFRESH_PRIMITIVETYPE_TRIANGLESTRIP
} REFRESH_PrimitiveType;

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

typedef enum REFRESH_Blend
{
    REFRESH_BLEND_ONE,
    REFRESH_BLEND_ZERO,
    REFRESH_BLEND_SOURCECOLOR,
    REFRESH_BLEND_INVERSESOURCECOLOR,
    REFRESH_BLEND_SOURCEALPHA,
    REFRESH_BLEND_INVERSESOURCEALPHA,
    REFRESH_BLEND_DESTINATIONCOLOR,
    REFRESH_BLEND_INVERSEDESTINATIONCOLOR,
    REFRESH_BLEND_DESTINATIONALPHA,
    REFRESH_BLEND_INVERSEDESTINATIONALPHA,
    REFRESH_BLEND_BLENDFACTOR,
    REFRESH_BLEND_INVERSEBLENDFACTOR,
    REFRESH_BLEND_SOURCEALPHASATURATION
} REFRESH_Blend;

typedef enum REFRESH_BlendFunction
{
    REFRESH_BLENDFUNCTION_ADD,
    REFRESH_BLENDFUNCTION_SUBTRACT,
    REFRESH_BLENDFUNCTION_REVERSESUBTRACT,
    REFRESH_BLENDFUNCTION_MAX,
    REFRESH_BLENDFUNCTION_MIN
} REFRESH_BlendFunction;

typedef enum REFRESH_ColorWriteChannels
{
    REFRESH_COLORWRITECHANNELS_NONE  = 0,
    REFRESH_COLORWRITECHANNELS_RED   = 1,
    REFRESH_COLORWRITECHANNELS_GREEN = 2,
    REFRESH_COLORWRITECHANNELS_BLUE  = 4,
    REFRESH_COLORWRITECHANNELS_ALPHA = 8,
    REFRESH_COLORWRITECHANNELS_ALL   = 15
} REFRESH_ColorWriteChannels;

typedef enum REFRESH_StencilOperation
{
	REFRESH_STENCILOPERATION_KEEP,
	REFRESH_STENCILOPERATION_ZERO,
	REFRESH_STENCILOPERATION_REPLACE,
	REFRESH_STENCILOPERATION_INCREMENT,
	REFRESH_STENCILOPERATION_DECREMENT,
	REFRESH_STENCILOPERATION_INCREMENTSATURATION,
	REFRESH_STENCILOPERATION_DECREMENTSATURATION,
	REFRESH_STENCILOPERATION_INVERT
} REFRESH_StencilOperation;

typedef enum REFRESH_CompareFunction
{
	REFRESH_COMPAREFUNCTION_ALWAYS,
	REFRESH_COMPAREFUNCTION_NEVER,
	REFRESH_COMPAREFUNCTION_LESS,
	REFRESH_COMPAREFUNCTION_LESSEQUAL,
	REFRESH_COMPAREFUNCTION_EQUAL,
	REFRESH_COMPAREFUNCTION_GREATEREQUAL,
	REFRESH_COMPAREFUNCTION_GREATER,
	REFRESH_COMPAREFUNCTION_NOTEQUAL
} REFRESH_CompareFunction;

typedef enum REFRESH_CullMode
{
	REFRESH_CULLMODE_NONE,
	REFRESH_CULLMODE_CULLCLOCKWISEFACE,
	REFRESH_CULLMODE_CULLCOUNTERCLOCKWISEFACE
} REFRESH_CullMode;

typedef enum REFRESH_FillMode
{
	REFRESH_FILLMODE_SOLID,
	REFRESH_FILLMODE_WIREFRAME
} REFRESH_FillMode;

typedef enum REFRESH_TextureAddressMode
{
	REFRESH_TEXTUREADDRESSMODE_WRAP,
	REFRESH_TEXTUREADDRESSMODE_CLAMP,
	REFRESH_TEXTUREADDRESSMODE_MIRROR
} REFRESH_TextureAddressMode;

typedef enum REFRESH_TextureFilter
{
	REFRESH_TEXTUREFILTER_LINEAR,
	REFRESH_TEXTUREFILTER_POINT,
	REFRESH_TEXTUREFILTER_ANISOTROPIC,
	REFRESH_TEXTUREFILTER_LINEAR_MIPPOINT,
	REFRESH_TEXTUREFILTER_POINT_MIPLINEAR,
	REFRESH_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPLINEAR,
	REFRESH_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPPOINT,
	REFRESH_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPLINEAR,
	REFRESH_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPPOINT
} REFRESH_TextureFilter;

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

/* Structures, should match XNA 4.0 */

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

typedef struct REFRESH_PresentationParameters
{
    uint32_t backBufferWidth;
    uint32_t backBufferHeight;
    void* deviceWindowHandle;
    REFRESH_PresentMode presentMode;
} REFRESH_PresentationParameters;

typedef struct REFRESH_BlendState
{
	REFRESH_Blend colorSourceBlend;
	REFRESH_Blend colorDestinationBlend;
	REFRESH_BlendFunction colorBlendFunction;
	REFRESH_Blend alphaSourceBlend;
	REFRESH_Blend alphaDestinationBlend;
	REFRESH_BlendFunction alphaBlendFunction;
	REFRESH_ColorWriteChannels colorWriteEnable;
	REFRESH_ColorWriteChannels colorWriteEnable1;
	REFRESH_ColorWriteChannels colorWriteEnable2;
	REFRESH_ColorWriteChannels colorWriteEnable3;
	REFRESH_Color blendFactor;
	int32_t multiSampleMask;
} REFRESH_BlendState;

typedef struct REFRESH_DepthStencilState
{
	uint8_t depthBufferEnable;
	uint8_t depthBufferWriteEnable;
	REFRESH_CompareFunction depthBufferFunction;
	uint8_t stencilEnable;
	int32_t stencilMask;
	int32_t stencilWriteMask;
	uint8_t twoSidedStencilMode;
	REFRESH_StencilOperation stencilFail;
	REFRESH_StencilOperation stencilDepthBufferFail;
	REFRESH_StencilOperation stencilPass;
	REFRESH_CompareFunction stencilFunction;
	REFRESH_StencilOperation ccwStencilFail;
	REFRESH_StencilOperation ccwStencilDepthBufferFail;
	REFRESH_StencilOperation ccwStencilPass;
	REFRESH_CompareFunction ccwStencilFunction;
	int32_t referenceStencil;
} REFRESH_DepthStencilState;

typedef struct REFRESH_RasterizerState
{
	REFRESH_FillMode fillMode;
	REFRESH_CullMode cullMode;
	float depthBias;
	float slopeScaleDepthBias;
	uint8_t scissorTestEnable;
	uint8_t multiSampleAntiAlias;
} REFRESH_RasterizerState;

typedef struct REFRESH_SamplerState
{
	REFRESH_TextureFilter filter;
	REFRESH_TextureAddressMode addressU;
	REFRESH_TextureAddressMode addressV;
	REFRESH_TextureAddressMode addressW;
	float mipMapLevelOfDetailBias;
	int32_t maxAnisotropy;
	int32_t maxMipLevel;
} REFRESH_SamplerState;

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

/* FIXME: can we deviate from FNA3D in a nice way user-side here? */
typedef struct REFRESH_RenderTargetBinding
{
	/* Basic target information */
	#define REFRESH_RENDERTARGET_TYPE_2D 0
	#define REFRESH_RENDERTARGET_TYPE_CUBE 1
	uint8_t type;
	REFRESHNAMELESS union
	{
		struct
		{
			int32_t width;
			int32_t height;
		} twod;
		struct
		{
			int32_t size;
			REFRESH_CubeMapFace face;
		} cube;
	};

	/* If this is >1, you MUST call ResolveTarget after rendering! */
	int32_t levelCount;

	/* If this is >1, colorBuffer MUST be non-NULL! */
	int32_t multiSampleCount;

	/* Destination texture. This MUST be non-NULL! */
	REFRESH_Texture *texture;

	/* If this is non-NULL, you MUST call ResolveTarget after rendering! */
	REFRESH_Renderbuffer *colorBuffer;
} FNA3D_RenderTargetBinding;

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

/* Logging */

typedef void (REFRESHCALL * REFRESH_LogFunc)(const char *msg);

/* Reroutes Refresh's logging to custom logging functions.
 *
 * info:	Basic logs that might be useful to have stored for support.
 * warn:	Something went wrong, but it's really just annoying, not fatal.
 * error:	You better have this stored somewhere because it's crashing now!
 */
REFRESHAPI void REFRESH_HookLogFunctions(
	REFRESH_LogFunc info,
	REFRESH_LogFunc warn,
	REFRESH_LogFunc error
);



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* REFRESH_H */

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
