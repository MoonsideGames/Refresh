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
typedef struct REFRESH_Sampler REFRESH_Sampler;
typedef struct REFRESH_ShaderParamBuffer REFRESH_ShaderParamBuffer;
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

typedef enum REFRESH_VertexInputRate
{
	REFRESH_VERTEXINPUTRATE_VERTEX = 0,
	REFRESH_VERTEXINPUTRATE_INSTANCE = 1
} REFRESH_VertexInputRate;

typedef enum REFRESH_FillMode
{
	REFRESH_FILLMODE_FILL,
	REFRESH_FILLMODE_LINE,
	REFRESH_FILLMODE_POINT
} REFRESH_FillMode;

typedef enum REFRESH_CullMode
{
	REFRESH_CULLMODE_NONE,
	REFRESH_CULLMODE_FRONT,
	REFRESH_CULLMODE_BACK,
	REFRESH_CULLMODE_FRONT_AND_BACK
} REFRESH_CullMode;

typedef enum REFRESH_FrontFace
{
	REFRESH_FRONTFACE_COUNTER_CLOCKWISE,
	REFRESH_FRONTFACE_CLOCKWISE
} REFRESH_FrontFace;

typedef enum REFRESH_CompareOp
{
	REFRESH_COMPAREOP_NEVER,
	REFRESH_COMPAREOP_LESS,
	REFRESH_COMPAREOP_EQUAL,
	REFRESH_COMPAREOP_LESS_OR_EQUAL,
	REFRESH_COMPAREOP_GREATER,
	REFRESH_COMPAREOP_NOT_EQUAL,
	REFRESH_COMPAREOP_GREATER_OR_EQUAL,
	REFRESH_COMPAREOP_ALWAYS
} REFRESH_CompareOp;

typedef enum REFRESH_StencilOp
{
	REFRESH_STENCILOP_KEEP,
	REFRESH_STENCILOP_ZERO,
	REFRESH_STENCILOP_REPLACE,
	REFRESH_STENCILOP_INCREMENT_AND_CLAMP,
	REFRESH_STENCILOP_DECREMENT_AND_CLAMP,
	REFRESH_STENCILOP_INVERT,
	REFRESH_STENCILOP_INCREMENT_AND_WRAP,
	REFRESH_STENCILOP_DECREMENT_AND_WRAP
} REFRESH_StencilOp;

typedef enum REFRESH_BlendOp
{
	REFRESH_BLENDOP_CLEAR = 0,
    REFRESH_BLENDOP_AND = 1,
    REFRESH_BLENDOP_AND_REVERSE = 2,
    REFRESH_BLENDOP_COPY = 3,
    REFRESH_BLENDOP_AND_INVERTED = 4,
    REFRESH_BLENDOP_NO_OP = 5,
    REFRESH_BLENDOP_XOR = 6,
    REFRESH_BLENDOP_OR = 7,
    REFRESH_BLENDOP_NOR = 8,
    REFRESH_BLENDOP_EQUIVALENT = 9,
    REFRESH_BLENDOP_INVERT = 10,
    REFRESH_BLENDOP_OR_REVERSE = 11,
    REFRESH_BLENDOP_COPY_INVERTED = 12,
    REFRESH_BLENDOP_OR_INVERTED = 13,
    REFRESH_BLENDOP_NAND = 14,
    REFRESH_BLENDOP_SET = 15
} REFRESH_BlendOp;

typedef enum REFRESH_BlendFactor
{
    REFRESH_BLENDFACTOR_ZERO = 0,
    REFRESH_BLENDFACTOR_ONE = 1,
    REFRESH_BLENDFACTOR_SRC_COLOR = 2,
    REFRESH_BLENDFACTOR_ONE_MINUS_SRC_COLOR = 3,
    REFRESH_BLENDFACTOR_DST_COLOR = 4,
    REFRESH_BLENDFACTOR_ONE_MINUS_DST_COLOR = 5,
    REFRESH_BLENDFACTOR_SRC_ALPHA = 6,
    REFRESH_BLENDFACTOR_ONE_MINUS_SRC_ALPHA = 7,
    REFRESH_BLENDFACTOR_DST_ALPHA = 8,
    REFRESH_BLENDFACTOR_ONE_MINUS_DST_ALPHA = 9,
    REFRESH_BLENDFACTOR_CONSTANT_COLOR = 10,
    REFRESH_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR = 11,
    REFRESH_BLENDFACTOR_CONSTANT_ALPHA = 12,
    REFRESH_BLENDFACTOR_ONE_MINUS_CONSTANT_ALPHA = 13,
    REFRESH_BLENDFACTOR_SRC_ALPHA_SATURATE = 14,
    REFRESH_BLENDFACTOR_SRC1_COLOR = 15,
    REFRESH_BLENDFACTOR_ONE_MINUS_SRC1_COLOR = 16,
    REFRESH_BLENDFACTOR_SRC1_ALPHA = 17,
    REFRESH_BLENDFACTOR_ONE_MINUS_SRC1_ALPHA = 18
} REFRESH_BlendFactor;

typedef enum REFRESH_ColorComponentFlagBits
{
    REFRESH_COLORCOMPONENT_R_BIT = 0x00000001,
    REFRESH_COLORCOMPONENT_G_BIT = 0x00000002,
    REFRESH_COLORCOMPONENT_B_BIT = 0x00000004,
    REFRESH_COLORCOMPONENT_A_BIT = 0x00000008,
    REFRESH_COLORCOMPONENT_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
} REFRESH_ColorComponentFlagBits;

typedef uint32_t REFRESH_ColorComponentFlags;

typedef enum REFRESH_ShaderStage
{
	REFRESH_SHADERSTAGE_VERTEX,
	REFRESH_SHADERSTAGE_FRAGMENT
} REFRESH_ShaderStage;

typedef enum REFRESH_SamplerFilter
{
	REFRESH_SAMPLERFILTER_NEAREST,
	REFRESH_SAMPLERFILTER_LINEAR
} REFRESH_SamplerFilter;

typedef enum REFRESH_SamplerMipmapMode
{
	REFRESH_SAMPLERMIPMAPMODE_NEAREST,
	REFRESH_SAMPLERMIPMAPMODE_LINEAR
} REFRESH_SamplerMipmapMode;

typedef enum REFRESH_SamplerAddressMode
{
	REFRESH_SAMPLERADDRESSMODE_REPEAT,
	REFRESH_SAMPLERADDRESSMODE_MIRRORED_REPEAT,
	REFRESH_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
	REFRESH_SAMPLERADDRESSMODE_CLAMP_TO_BORDER
} REFRESH_SamplerAddressMode;

typedef enum REFRESH_BorderColor
{
    REFRESH_BORDERCOLOR_FLOAT_TRANSPARENT_BLACK = 0,
    REFRESH_BORDERCOLOR_INT_TRANSPARENT_BLACK = 1,
    REFRESH_BORDERCOLOR_FLOAT_OPAQUE_BLACK = 2,
    REFRESH_BORDERCOLOR_INT_OPAQUE_BLACK = 3,
    REFRESH_BORDERCOLOR_FLOAT_OPAQUE_WHITE = 4,
    REFRESH_BORDERCOLOR_INT_OPAQUE_WHITE = 5
} REFRESH_BorderColor;

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

/* State structures */

typedef struct REFRESH_SamplerStateCreateInfo
{
	REFRESH_SamplerFilter minFilter;
	REFRESH_SamplerFilter magFilter;
	REFRESH_SamplerMipmapMode mipmapMode;
	REFRESH_SamplerAddressMode addressModeU;
	REFRESH_SamplerAddressMode addressModeV;
	REFRESH_SamplerAddressMode addressModeW;
	float mipLodBias;
	uint8_t anisotropyEnable;
	float maxAnisotropy;
	uint8_t compareEnable;
	REFRESH_CompareOp compareOp;
	float minLod;
	float maxLod;
	REFRESH_BorderColor borderColor;
} REFRESH_SamplerStateCreateInfo;

typedef struct REFRESH_VertexBinding
{
	uint32_t binding;
	uint32_t stride;
	REFRESH_VertexInputRate inputRate;
} REFRESH_VertexBinding;

typedef struct REFRESH_VertexAttribute
{
	uint32_t location;
	uint32_t binding;
	REFRESH_VertexElementFormat format;
	uint32_t offset;
} REFRESH_VertexAttribute;

typedef struct REFRESH_VertexInputState
{
	const REFRESH_VertexBinding *vertexBindings;
	uint32_t vertexBindingCount;
	const REFRESH_VertexAttribute *vertexAttributes;
	uint32_t vertexAttributeCount;
} REFRESH_VertexInputState;

typedef struct REFRESH_StencilOpState
{
	REFRESH_StencilOp failOp;
	REFRESH_StencilOp passOp;
	REFRESH_StencilOp depthFailOp;
	REFRESH_StencilOp compareOp;
	uint32_t compareMask;
	uint32_t writeMask;
	uint32_t reference;
} REFRESH_StencilOpState;

typedef struct REFRESH_RenderTargetBlendState
{
	uint8_t blendEnable;
	REFRESH_BlendFactor srcColorBlendFactor;
	REFRESH_BlendFactor dstColorBlendFactor;
	REFRESH_BlendOp colorBlendOp;
	REFRESH_BlendFactor srcAlphaBlendFactor;
	REFRESH_BlendFactor dstAlphaBlendFactor;
	REFRESH_BlendOp alphaBlendOp;
	REFRESH_ColorComponentFlags colorWriteMask;
} REFRESH_RenderTargetBlendState;

typedef struct REFRESH_ShaderTextureSamplerLayoutBinding
{
	uint32_t binding;
	REFRESH_ShaderStage shaderStage;
} REFRESH_ShaderSampleLayoutBinding;

typedef struct REFRESH_ShaderTextureSamplerLayout
{
	uint32_t bindingCount;
	const REFRESH_ShaderSampleLayoutBinding *bindings;
} REFRESH_ShaderTextureSamplerLayout;

typedef struct REFRESH_ShaderParamLayoutBinding
{
	uint32_t binding;
	REFRESH_ShaderStage shaderStage;
} REFRESH_ShaderParamLayoutBinding;

typedef struct REFRESH_ShaderParamLayout
{
	uint32_t bindingCount;
	const REFRESH_ShaderParamLayoutBinding *bindings;
} REFRESH_ShaderParamLayout;

typedef struct REFRESH_PipelineLayoutCreateInfo
{
	uint32_t shaderParamsLayoutCount;
	REFRESH_ShaderParamLayout *shaderParamLayouts;
	uint32_t shaderTextureSamplerLayoutCount;
	const REFRESH_ShaderTextureSamplerLayout *shaderTextureSamplerLayouts;
} REFRESH_PipelineLayoutCreateInfo;

typedef struct REFRESH_RenderTargetDescription
{
	REFRESH_SurfaceFormat format;
	uint32_t multisampleCount;
	REFRESH_LoadOp loadOp;
	REFRESH_StoreOp storeOp;
	REFRESH_LoadOp stencilLoadOp;
	REFRESH_StoreOp stencilStoreOp;
} REFRESH_RenderTargetDescription;

typedef struct REFRESH_RenderPassCreateInfo
{
	uint32_t renderTargetCount;
	const REFRESH_RenderTargetDescription *renderTargetDescriptions;
} REFRESH_RenderPassCreateInfo;

/* Pipeline state structures */

typedef struct REFRESH_TopologyState
{
	REFRESH_TopologyState topology;
} REFRESH_TopologyState;

typedef struct REFRESH_ViewportState
{
	const REFRESH_Viewport *viewPorts;
	uint32_t viewportCount;
	const REFRESH_Rect *scissors;
	uint32_t scissorCount;
} REFRESH_ViewportState;

typedef struct REFRESH_RasterizerState
{
	uint8_t depthClampEnable;
	REFRESH_FillMode fillMode;
	REFRESH_CullMode cullMode;
	REFRESH_FrontFace frontFace;
	uint8_t depthBiasEnable;
	float depthBiasConstantFactor;
	float depthBiasClamp;
	float depthBiasSlopeFactor;
	float lineWidth;
} REFRESH_RasterizerState;

typedef struct REFRESH_MultisampleState
{
	uint8_t multisampleCount;
	const uint32_t *sampleMask;
} REFRESH_MultisampleState;

typedef struct REFRESH_DepthStencilState
{
	uint8_t depthTestEnable;
	uint8_t depthWriteEnable;
	REFRESH_CompareOp compareOp;
	uint8_t depthBoundsTestEnable;
	uint8_t stencilTestEnable;
	REFRESH_StencilOpState frontStencilState;
	REFRESH_StencilOpState backStencilState;
	float minDepthBounds;
	float maxDepthBounds;
} REFRESH_DepthStencilState;

typedef struct REFRESH_ColorBlendState
{
	uint8_t blendOpEnable;
	REFRESH_BlendOp blendOp;
	const REFRESH_RenderTargetBlendState *blendStates;
	uint32_t blendStateCount;
	float blendConstants[4];
} REFRESH_ColorBlendState;

typedef struct REFRESH_PipelineCreateInfo
{
	const REFRESH_ShaderStage *shaderStages;
	const REFRESH_VertexInputState *vertexInputState;
	const REFRESH_TopologyState *topologyState;
	const REFRESH_ViewportState *viewportState;
	const REFRESH_RasterizerState *rasterizerState;
	const REFRESH_MultisampleState *multisampleState;
	const REFRESH_DepthStencilState *depthStencilState;
	const REFRESH_ColorBlendState *colorBlendState;
	REFRESH_PipelineLayoutCreateInfo *layoutCreateInfo;
	REFRESH_RenderPass *renderPass;
} REFRESH_PipelineCreateInfo;

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

/* State Creation */

REFRESHAPI REFRESH_RenderPass REFRESH_CreateRenderPass(
	REFRESH_Device *device,
	REFRESH_RenderPassCreateInfo *renderPassCreateInfo
);

REFRESHAPI REFRESH_Pipeline REFRESH_CreatePipeline(
	REFRESH_Device *device,
	REFRESH_PipelineCreateInfo *pipelineCreateInfo
);

REFRESHAPI REFRESH_Sampler REFRESH_CreateSampler(
	REFRESH_Device *device,
	REFRESH_SamplerStateCreateInfo *samplerStateCreateInfo
);

/* Shader State */

REFRESHAPI void REFRESH_SetSamplers(
	REFRESH_Device *device,
	uint32_t startIndex,
	REFRESH_Texture **textures,
	REFRESH_Sampler **samplers
);

REFRESHAPI void REFRESH_SetShaderParamData(
	REFRESH_Device *device,
	REFRESH_ShaderParamBuffer *shaderParamBuffer,
	uint32_t offsetInBytes,
	void *data,
	uint32_t elementCount,
	uint32_t elementSizeInBytes
);

/* Render Targets */



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* REFRESH_H */

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
