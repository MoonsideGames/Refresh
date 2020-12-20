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

#include <stddef.h>

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
typedef struct REFRESH_Buffer REFRESH_Buffer;
typedef struct REFRESH_Texture REFRESH_Texture;
typedef struct REFRESH_DepthStencilTexture REFRESH_DepthStencilTexture;
typedef struct REFRESH_Sampler REFRESH_Sampler;
typedef struct REFRESH_ColorTarget REFRESH_ColorTarget;
typedef struct REFRESH_DepthStencilTarget REFRESH_DepthStencilTarget;
typedef struct REFRESH_Framebuffer REFRESH_Framebuffer;
typedef struct REFRESH_ShaderModule REFRESH_ShaderModule;
typedef struct REFRESH_RenderPass REFRESH_RenderPass;
typedef struct REFRESH_GraphicsPipeline REFRESH_GraphicsPipeline;

typedef enum REFRESH_PresentMode
{
	REFRESH_PRESENTMODE_IMMEDIATE,
	REFRESH_PRESENTMODE_MAILBOX,
	REFRESH_PRESENTMODE_FIFO,
	REFRESH_PRESENTMODE_FIFO_RELAXED
} REFRESH_PresentMode;

typedef enum REFRESH_PrimitiveType
{
    REFRESH_PRIMITIVETYPE_POINTLIST,
	REFRESH_PRIMITIVETYPE_LINELIST,
	REFRESH_PRIMITIVETYPE_LINESTRIP,
	REFRESH_PRIMITIVETYPE_TRIANGLELIST,
	REFRESH_PRIMITIVETYPE_TRIANGLESTRIP
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
	REFRESH_DEPTHFORMAT_D16_UNORM,
	REFRESH_DEPTHFORMAT_D32_SFLOAT,
    REFRESH_DEPTHFORMAT_D16_UNORM_S8_UINT,
    REFRESH_DEPTHFORMAT_D24_UNORM_S8_UINT,
    REFRESH_DEPTHFORMAT_D32_SFLOAT_S8_UINT
} REFRESH_DepthFormat;

typedef enum REFRESH_SampleCount
{
	REFRESH_SAMPLECOUNT_1,
	REFRESH_SAMPLECOUNT_2,
	REFRESH_SAMPLECOUNT_4,
	REFRESH_SAMPLECOUNT_8,
	REFRESH_SAMPLECOUNT_16,
	REFRESH_SAMPLECOUNT_32,
	REFRESH_SAMPLECOUNT_64
} REFRESH_SampleCount;

typedef enum REFRESH_CubeMapFace
{
    REFRESH_CUBEMAPFACE_POSITIVEX,
    REFRESH_CUBEMAPFACE_NEGATIVEX,
    REFRESH_CUBEMAPFACE_POSITIVEY,
    REFRESH_CUBEMAPFACE_NEGATIVEY,
    REFRESH_CUBEMAPFACE_POSITIVEZ,
    REFRESH_CUBEMAPFACE_NEGATIVEZ
} REFRESH_CubeMapFace;

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
	REFRESH_BLENDOP_ADD,
	REFRESH_BLENDOP_SUBTRACT,
	REFRESH_BLENDOP_REVERSE_SUBTRACT,
	REFRESH_BLENDOP_MIN,
	REFRESH_BLENDOP_MAX
} REFRESH_BlendOp;

typedef enum REFRESH_LogicOp
{
	REFRESH_LOGICOP_CLEAR = 0,
    REFRESH_LOGICOP_AND = 1,
    REFRESH_LOGICOP_AND_REVERSE = 2,
    REFRESH_LOGICOP_COPY = 3,
    REFRESH_LOGICOP_AND_INVERTED = 4,
    REFRESH_LOGICOP_NO_OP = 5,
    REFRESH_LOGICOP_XOR = 6,
    REFRESH_LOGICOP_OR = 7,
    REFRESH_LOGICOP_NOR = 8,
    REFRESH_LOGICOP_EQUIVALENT = 9,
    REFRESH_LOGICOP_INVERT = 10,
    REFRESH_LOGICOP_OR_REVERSE = 11,
    REFRESH_LOGICOP_COPY_INVERTED = 12,
    REFRESH_LOGICOP_OR_INVERTED = 13,
    REFRESH_LOGICOP_NAND = 14,
    REFRESH_LOGICOP_SET = 15
} REFRESH_LogicOp;

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

typedef enum REFRESH_ShaderStageType
{
	REFRESH_SHADERSTAGE_VERTEX,
	REFRESH_SHADERSTAGE_FRAGMENT
} REFRESH_ShaderStageType;

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

typedef struct REFRESH_DepthStencilValue
{
	float depth;
	uint32_t stencil;
} REFRESH_DepthStencilValue;

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
	float x;
	float y;
	float w;
	float h;
	float minDepth;
	float maxDepth;
} REFRESH_Viewport;

typedef struct REFRESH_TextureSlice
{
	REFRESH_Texture *texture;
	uint32_t layer; /* 0-5 for cube, or z-slice for 3D */
} REFRESH_TextureSlice;

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

typedef struct REFRESH_PipelineLayoutCreateInfo
{
	uint32_t vertexSamplerBindingCount;
	const uint32_t *vertexSamplerBindings;
	uint32_t fragmentSamplerBindingCount;
	const uint32_t *fragmentSamplerBindings;
} REFRESH_PipelineLayoutCreateInfo;

typedef struct REFRESH_ColorTargetDescription
{
	REFRESH_SurfaceFormat format;
	REFRESH_SampleCount multisampleCount;
	REFRESH_LoadOp loadOp;
	REFRESH_StoreOp storeOp;
} REFRESH_ColorTargetDescription;

typedef struct REFRESH_DepthTargetDescription
{
	REFRESH_DepthFormat depthFormat;
	REFRESH_LoadOp loadOp;
	REFRESH_StoreOp storeOp;
	REFRESH_LoadOp stencilLoadOp;
	REFRESH_StoreOp stencilStoreOp;
} REFRESH_DepthTargetDescription;

typedef struct REFRESH_RenderPassCreateInfo
{
	const REFRESH_ColorTargetDescription *colorTargetDescriptions;
	uint32_t colorTargetCount;
	const REFRESH_DepthTargetDescription *depthTargetDescription; /* can be NULL */
} REFRESH_RenderPassCreateInfo;

typedef struct REFRESH_ShaderModuleCreateInfo
{
	size_t codeSize;
	const uint32_t *byteCode;
} REFRESH_ShaderModuleCreateInfo;

/* Pipeline state structures */

typedef struct REFRESH_ShaderStageState
{
	REFRESH_ShaderModule *shaderModule;
	const char* entryPointName;
} REFRESH_ShaderStageState;

typedef struct REFRESH_TopologyState
{
	REFRESH_PrimitiveType topology;
} REFRESH_TopologyState;

typedef struct REFRESH_ViewportState
{
	const REFRESH_Viewport *viewports;
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
	REFRESH_SampleCount multisampleCount;
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
	REFRESH_LogicOp logicOp;
	const REFRESH_RenderTargetBlendState *blendStates;
	uint32_t blendStateCount;
	float blendConstants[4];
} REFRESH_ColorBlendState;

typedef struct REFRESH_GraphicsPipelineCreateInfo
{
	const REFRESH_ShaderStageState vertexShaderState;
	const REFRESH_ShaderStageState fragmentShaderState;
	const REFRESH_VertexInputState vertexInputState;
	const REFRESH_TopologyState topologyState;
	const REFRESH_ViewportState viewportState;
	const REFRESH_RasterizerState rasterizerState;
	const REFRESH_MultisampleState multisampleState;
	const REFRESH_DepthStencilState depthStencilState;
	const REFRESH_ColorBlendState colorBlendState;
	REFRESH_PipelineLayoutCreateInfo pipelineLayoutCreateInfo;
	REFRESH_RenderPass *renderPass;
} REFRESH_GraphicsPipelineCreateInfo;

typedef struct REFRESH_FramebufferCreateInfo
{
	REFRESH_RenderPass *renderPass;
	const REFRESH_ColorTarget **pColorTargets;
	uint32_t colorTargetCount;
	const REFRESH_DepthStencilTarget *pDepthTarget;
	uint32_t width;
	uint32_t height;
} REFRESH_FramebufferCreateInfo;

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

/* Device */

/* Create a rendering context for use on the calling thread.
 *
 * deviceWindowHandle:
 * 		A handle to a window.
 * 		If this is NULL, Refresh will run in headless mode.
 * debugMode: Enable debug mode properties.
 */
REFRESHAPI REFRESH_Device* REFRESH_CreateDevice(
	void *deviceWindowHandle,
	uint8_t debugMode
);

/* Destroys a rendering context previously returned by REFRESH_CreateDevice. */
REFRESHAPI void REFRESH_DestroyDevice(REFRESH_Device *device);

/* Drawing */

/* Clears the targets of the currently bound framebuffer.
 * NOTE:
 * 		It is generally recommended to clear in BeginRenderPass
 * 		rather than by calling this function.
 *
 * options:	   Bitflags to specify color/depth/stencil buffers for clearing.
 * colors:	   The new values of the cleared color buffers.
 * colorCount: The amount of cleared color buffers.
 * depth:	   The new value of the cleared depth buffer.
 * stencil:	   The new value of the cleared stencil buffer.
 */
REFRESHAPI void REFRESH_Clear(
	REFRESH_Device *device,
	REFRESH_ClearOptions options,
	REFRESH_Vec4 **colors,
    uint32_t colorCount,
	float depth,
	int32_t stencil
);

/* Draws data from vertex/index buffers with instancing enabled.
 *
 * graphicsPipeline:	The graphics pipeline through which to draw.
 * primitiveType:		The primitive topology of the vertex data.
 * baseVertex:			The starting offset to read from the vertex buffer.
 * minVertexIndex:		The lowest index value expected from the index buffer.
 * numVertices:			The highest offset expected from the index buffer.
 * startIndex:			The starting offset to read from the index buffer.
 * primitiveCount:		The number of primitives to draw.
 * instanceCount:		The number of instances that will be drawn.
 * indices:				The index buffer to bind for this draw call.
 * indexElementSize:	The size of the index type for this index buffer.
 */
REFRESHAPI void REFRESH_DrawInstancedPrimitives(
	REFRESH_Device *device,
	REFRESH_GraphicsPipeline *graphicsPipeline,
	REFRESH_PrimitiveType primitiveType,
	uint32_t baseVertex,
	uint32_t minVertexIndex,
	uint32_t numVertices,
	uint32_t startIndex,
	uint32_t primitiveCount,
	uint32_t instanceCount,
	REFRESH_Buffer *indices,
	REFRESH_IndexElementSize indexElementSize
);

/* Draws data from vertex/index buffers.
 *
 * graphicsPipeline:	The graphics pipeline through which to draw.
 * primitiveType:		The primitive topology of the vertex data.
 * baseVertex:			The starting offset to read from the vertex buffer.
 * minVertexIndex:		The lowest index value expected from the index buffer.
 * numVertices:			The highest offset expected from the index buffer.
 * startIndex:			The starting offset to read from the index buffer.
 * primitiveCount:		The number of primitives to draw.
 * indices:				The index buffer to bind for this draw call.
 * indexElementSize:	The size of the index type for this index buffer.
 */
REFRESHAPI void REFRESH_DrawIndexedPrimitives(
	REFRESH_Device *device,
	REFRESH_GraphicsPipeline *graphicsPipeline,
	REFRESH_PrimitiveType primitiveType,
	uint32_t baseVertex,
	uint32_t minVertexIndex,
	uint32_t numVertices,
	uint32_t startIndex,
	uint32_t primitiveCount,
	REFRESH_Buffer *indices,
	REFRESH_IndexElementSize indexElementSize
);

/* Draws data from vertex buffers.
 *
 * graphicsPipeline:	The graphics pipeline through which to draw.
 * primitiveType:		The primitive topology of the vertex data.
 * vertexStart:			The starting offset to read from the vertex buffer.
 * primitiveCount:		The number of primitives to draw.
 */
REFRESHAPI void REFRESH_DrawPrimitives(
	REFRESH_Device *device,
	REFRESH_GraphicsPipeline *graphicsPipeline,
	REFRESH_PrimitiveType primitiveType,
	uint32_t vertexStart,
	uint32_t primitiveCount
);

/* State Creation */

/* Returns an allocated RenderPass* object. */
REFRESHAPI REFRESH_RenderPass* REFRESH_CreateRenderPass(
	REFRESH_Device *device,
	REFRESH_RenderPassCreateInfo *renderPassCreateInfo
);

/* Returns an allocated Pipeline* object. */
REFRESHAPI REFRESH_GraphicsPipeline* REFRESH_CreateGraphicsPipeline(
	REFRESH_Device *device,
	REFRESH_GraphicsPipelineCreateInfo *pipelineCreateInfo
);

/* Returns an allocated Sampler* object. */
REFRESHAPI REFRESH_Sampler* REFRESH_CreateSampler(
	REFRESH_Device *device,
	REFRESH_SamplerStateCreateInfo *samplerStateCreateInfo
);

/* Returns an allocated Framebuffer* object. */
REFRESHAPI REFRESH_Framebuffer* REFRESH_CreateFramebuffer(
	REFRESH_Device *device,
	REFRESH_FramebufferCreateInfo *framebufferCreateInfo
);

/* Returns an allocated ShaderModule* object. */
REFRESHAPI REFRESH_ShaderModule* REFRESH_CreateShaderModule(
	REFRESH_Device *device,
	REFRESH_ShaderModuleCreateInfo *shaderModuleCreateInfo
);

/* Creates a 2D texture.
 *
 * format:		The pixel format of the texture data.
 * width:		The width of the texture image.
 * height: 		The height of the texture image.
 * levelCount: 	The number of mipmap levels to allocate.
 *
 * Returns an allocated REFRESH_Texture* object. Note that the contents of
 * the texture are undefined until SetData is called.
 */
REFRESHAPI REFRESH_Texture* REFRESH_CreateTexture2D(
	REFRESH_Device *device,
	REFRESH_SurfaceFormat format,
	uint32_t width,
	uint32_t height,
	uint32_t levelCount,
	uint8_t canBeRenderTarget
);

/* Creates a 3D texture.
 *
 * format:		The pixel format of the texture data.
 * width:		The width of the texture image.
 * height: 		The height of the texture image.
 * depth: 		The depth of the texture image.
 * levelCount: 	The number of mipmap levels to allocate.
 *
 * Returns an allocated REFRESH_Texture* object. Note that the contents of
 * the texture are undefined until SetData is called.
 */
REFRESHAPI REFRESH_Texture* REFRESH_CreateTexture3D(
	REFRESH_Device *device,
	REFRESH_SurfaceFormat format,
	uint32_t width,
	uint32_t height,
	uint32_t depth,
	uint32_t levelCount,
	uint8_t canBeRenderTarget
);

/* Creates a texture cube.
 *
 * format:		The pixel format of the texture data.
 * size: 		The length of the cube side.
 * levelCount: 	The number of mipmap levels to allocate.
 *
 * Returns an allocated REFRESH_Texture* object. Note that the contents of
 * the texture are undefined until SetData is called.
 */
REFRESHAPI REFRESH_Texture* REFRESH_CreateTextureCube(
	REFRESH_Device *device,
	REFRESH_SurfaceFormat format,
	uint32_t size,
	uint32_t levelCount,
	uint8_t canBeRenderTarget
);

/* Creates a color target.
 *
 * multisampleCount:	The MSAA value for the color target.
 * textureSlice: 		The texture slice that the color target will resolve to.
 */
REFRESHAPI REFRESH_ColorTarget* REFRESH_GenColorTarget(
	REFRESH_Device *device,
	REFRESH_SampleCount multisampleCount,
	REFRESH_TextureSlice textureSlice
);

/* Creates a depth/stencil target.
 *
 * width:	The width of the depth/stencil target.
 * height: 	The height of the depth/stencil target.
 * format:	The storage format of the depth/stencil target.
 */
REFRESHAPI REFRESH_DepthStencilTarget* REFRESH_GenDepthStencilTarget(
	REFRESH_Device *device,
	uint32_t width,
	uint32_t height,
	REFRESH_DepthFormat format
);

/* Creates a vertex buffer to be used by Draw commands.
 *
 * sizeInBytes: The length of the vertex buffer.
 */
REFRESHAPI REFRESH_Buffer* REFRESH_GenVertexBuffer(
	REFRESH_Device *device,
	uint32_t sizeInBytes
);

/* Creates an index buffer to be used by Draw commands.
 *
 * sizeInBytes: The length of the index buffer.
 */
REFRESHAPI REFRESH_Buffer* REFRESH_GenIndexBuffer(
	REFRESH_Device *device,
	uint32_t sizeInBytes
);

/* Setters */

/* Uploads image data to a 2D texture object.
 *
 * texture:	The texture to be updated.
 * x:		The x offset of the subregion being updated.
 * y:		The y offset of the subregion being updated.
 * w:		The width of the subregion being updated.
 * h:		The height of the subregion being updated.
 * level:	The mipmap level being updated.
 * data:	A pointer to the image data.
 * dataLength:	The size of the image data in bytes.
 */
REFRESHAPI void REFRESH_SetTextureData2D(
	REFRESH_Device *device,
	REFRESH_Texture *texture,
	uint32_t x,
	uint32_t y,
	uint32_t w,
	uint32_t h,
	uint32_t level,
	void *data,
	uint32_t dataLengthInBytes
);

/* Uploads image data to a 3D texture object.
 *
 * texture:	The texture to be updated.
 * x:		The x offset of the subregion being updated.
 * y:		The y offset of the subregion being updated.
 * z:		The z offset of the subregion being updated.
 * w:		The width of the subregion being updated.
 * h:		The height of the subregion being updated.
 * d:		The depth of the subregion being updated.
 * level:	The mipmap level being updated.
 * data:	A pointer to the image data.
 * dataLength:	The size of the image data in bytes.
 */
REFRESHAPI void REFRESH_SetTextureData3D(
	REFRESH_Device *device,
	REFRESH_Texture *texture,
	uint32_t x,
	uint32_t y,
	uint32_t z,
	uint32_t w,
	uint32_t h,
	uint32_t d,
	uint32_t level,
	void* data,
	uint32_t dataLength
);

/* Uploads image data to a single face of a texture cube object.
 *
 * texture:	The texture to be updated.
 * x:		The x offset of the subregion being updated.
 * y:		The y offset of the subregion being updated.
 * w:		The width of the subregion being updated.
 * h:		The height of the subregion being updated.
 * cubeMapFace:	The face of the cube being updated.
 * level:	The mipmap level being updated.
 * data:	A pointer to the image data.
 * dataLength:	The size of the image data in bytes.
 */
REFRESHAPI void REFRESH_SetTextureDataCube(
	REFRESH_Device *device,
	REFRESH_Texture *texture,
	uint32_t x,
	uint32_t y,
	uint32_t w,
	uint32_t h,
	REFRESH_CubeMapFace cubeMapFace,
	uint32_t level,
	void* data,
	uint32_t dataLength
);

/* Uploads YUV image data to three R8 texture objects.
 *
 * y:		The texture storing the Y data.
 * u:		The texture storing the U (Cb) data.
 * v:		The texture storing the V (Cr) data.
 * yWidth:	The width of the Y plane.
 * yHeight:	The height of the Y plane.
 * uvWidth:	The width of the U/V planes.
 * uvHeight:	The height of the U/V planes.
 * data:	A pointer to the raw YUV image data.
 * dataLength:	The size of the image data in bytes.
 */
REFRESHAPI void REFRESH_SetTextureDataYUV(
	REFRESH_Device *device,
	REFRESH_Texture *y,
	REFRESH_Texture *u,
	REFRESH_Texture *v,
	uint32_t yWidth,
	uint32_t yHeight,
	uint32_t uvWidth,
	uint32_t uvHeight,
	void* data,
	uint32_t dataLength
);

/* Sets a region of the vertex buffer with client data.
 *
 * NOTE:
 * 		Calling this function on a buffer after the buffer
 * 		has been bound by BindVertexBuffers but before
 * 		Present has been called is an error.
 *
 * buffer:		The vertex buffer to be updated.
 * offsetInBytes:	The starting offset of the buffer to write into.
 * data:		The client data to write into the buffer.
 * elementCount:	The number of elements from the client buffer to write.
 * elementSizeInBytes:	The size of each element in the client buffer.
 */
REFRESHAPI void REFRESH_SetVertexBufferData(
	REFRESH_Device *device,
	REFRESH_Buffer *buffer,
	uint32_t offsetInBytes,
	void* data,
	uint32_t elementCount,
	uint32_t elementSizeInBytes
);

/* Sets a region of the index buffer with client data.
 *
 * NOTE:
 * 		Calling this function on a buffer after the buffer
 * 		has been bound by BindIndexBuffer but before
 * 		Present has been called is an error.
 *
 * buffer:		The index buffer to be updated.
 * offsetInBytes:	The starting offset of the buffer to write into.
 * data:		The client data to write into the buffer.
 * dataLength:		The size (in bytes) of the client data.
 */
REFRESHAPI void REFRESH_SetIndexBufferData(
	REFRESH_Device *device,
	REFRESH_Buffer *buffer,
	uint32_t offsetInBytes,
	void* data,
	uint32_t dataLength
);

/* Pushes vertex shader params for subsequent draw calls.
 *
 * pipeline: 			The graphics pipeline to push shader data to.
 * data: 				The client data to write into the buffer.
 * elementCount: 		The number of elements from the client buffer to write.
 * elementSizeInBytes:	The size of each element in the client buffer.
 */
REFRESHAPI void REFRESH_PushVertexShaderParams(
	REFRESH_Device *device,
    REFRESH_GraphicsPipeline *pipeline,
	void *data,
	uint32_t elementCount,
	uint32_t elementSizeInBytes
);

/* Pushes fragment shader params for subsequent draw calls.
 *
 * pipeline: 			The graphics pipeline to push shader data to.
 * data: 				The client data to write into the buffer.
 * elementCount: 		The number of elements from the client buffer to write.
 * elementSizeInBytes:	The size of each element in the client buffer.
 */
REFRESHAPI void REFRESH_PushFragmentShaderParams(
	REFRESH_Device *device,
    REFRESH_GraphicsPipeline *pipeline,
	void *data,
	uint32_t elementCount,
	uint32_t elementSizeInBytes
);

/* Sets textures/samplers for use with the currently bound vertex shader.
 *
 * NOTE:
 * 		The length of the passed arrays must be equal to the number
 * 		of sampler bindings specified by the shader.
 *
 * pipeline:	The graphics pipeline to push shader data to.
 * textures:	A pointer to an array of textures.
 * samplers:	A pointer to an array of samplers.
 */
REFRESHAPI void REFRESH_SetVertexSamplers(
	REFRESH_Device *device,
	REFRESH_GraphicsPipeline *pipeline,
	REFRESH_Texture **pTextures,
	REFRESH_Sampler **pSamplers
);

/* Sets textures/samplers for use with the currently bound fragment shader.
 *
 * NOTE:
 *		The length of the passed arrays must be equal to the number
 * 		of sampler bindings specified by the shader.
 *
 * pipeline:	The graphics pipeline to push shader data to.
 * textures: 	A pointer to an array of textures.
 * samplers:	A pointer to an array of samplers.
 */
REFRESHAPI void REFRESH_SetFragmentSamplers(
	REFRESH_Device *device,
	REFRESH_GraphicsPipeline *pipeline,
	REFRESH_Texture **pTextures,
	REFRESH_Sampler **pSamplers
);

/* Getters */

/* Pulls image data from a 2D texture into client memory. Like any GetData,
 * this is generally asking for a massive CPU/GPU sync point, don't call this
 * unless there's absolutely no other way to use the image data!
 *
 * texture:	The texture object being read.
 * x:		The x offset of the subregion being read.
 * y:		The y offset of the subregion being read.
 * w:		The width of the subregion being read.
 * h:		The height of the subregion being read.
 * level:	The mipmap level being read.
 * data:	The pointer being filled with the image data.
 * dataLength:	The size of the image data in bytes.
 */
REFRESHAPI void REFRESH_GetTextureData2D(
	REFRESH_Device *device,
	REFRESH_Texture *texture,
	uint32_t x,
	uint32_t y,
	uint32_t w,
	uint32_t h,
	uint32_t level,
	void* data,
	uint32_t dataLength
);

/* Pulls image data from a single face of a texture cube object into client
 * memory. Like any GetData, this is generally asking for a massive CPU/GPU sync
 * point, don't call this unless there's absolutely no other way to use the
 * image data!
 *
 * texture:	The texture object being read.
 * x:		The x offset of the subregion being read.
 * y:		The y offset of the subregion being read.
 * w:		The width of the subregion being read.
 * h:		The height of the subregion being read.
 * cubeMapFace:	The face of the cube being read.
 * level:	The mipmap level being read.
 * data:	The pointer being filled with the image data.
 * dataLength:	The size of the image data in bytes.
 */
REFRESHAPI void REFRESH_GetTextureDataCube(
	REFRESH_Device *device,
	REFRESH_Texture *texture,
	uint32_t x,
	uint32_t y,
	uint32_t w,
	uint32_t h,
	REFRESH_CubeMapFace cubeMapFace,
	uint32_t level,
	void* data,
	uint32_t dataLength
);

/* Disposal */

/* Sends a texture to be destroyed by the renderer. Note that we call it
 * "AddDispose" because it may not be immediately destroyed by the renderer if
 * this is not called from the main thread (for example, if a garbage collector
 * deletes the resource instead of the programmer).
 *
 * texture: The REFRESH_Texture to be destroyed.
 */
REFRESHAPI void REFRESH_AddDisposeTexture(
	REFRESH_Device *device,
	REFRESH_Texture *texture
);

/* Sends a sampler to be destroyed by the renderer. Note that we call it
 * "AddDispose" because it may not be immediately destroyed by the renderer if
 * this is not called from the main thread (for example, if a garbage collector
 * deletes the resource instead of the programmer).
 *
 * texture: The REFRESH_Sampler to be destroyed.
 */
REFRESHAPI void REFRESH_AddDisposeSampler(
	REFRESH_Device *device,
	REFRESH_Sampler *sampler
);

/* Sends a vertex buffer to be destroyed by the renderer. Note that we call it
 * "AddDispose" because it may not be immediately destroyed by the renderer if
 * this is not called from the main thread (for example, if a garbage collector
 * deletes the resource instead of the programmer).
 *
 * buffer: The REFRESH_Buffer to be destroyed.
 */
REFRESHAPI void REFRESH_AddDisposeVertexBuffer(
	REFRESH_Device *device,
	REFRESH_Buffer *buffer
);

/* Sends an index buffer to be destroyed by the renderer. Note that we call it
 * "AddDispose" because it may not be immediately destroyed by the renderer if
 * this is not called from the main thread (for example, if a garbage collector
 * deletes the resource instead of the programmer).
 *
 * buffer: The REFRESH_Buffer to be destroyed.
 */
REFRESHAPI void REFRESH_AddDisposeIndexBuffer(
	REFRESH_Device *device,
	REFRESH_Buffer *buffer
);

/* Sends a color target to be destroyed by the renderer. Note that we call it
 * "AddDispose" because it may not be immediately destroyed by the renderer if
 * this is not called from the main thread (for example, if a garbage collector
 * deletes the resource instead of the programmer).
 *
 * colorTarget: The REFRESH_ColorTarget to be destroyed.
 */
REFRESHAPI void REFRESH_AddDisposeColorTarget(
	REFRESH_Device *device,
	REFRESH_ColorTarget *colorTarget
);

/* Sends a depth/stencil target to be destroyed by the renderer. Note that we call it
 * "AddDispose" because it may not be immediately destroyed by the renderer if
 * this is not called from the main thread (for example, if a garbage collector
 * deletes the resource instead of the programmer).
 *
 * depthStencilTarget: The REFRESH_DepthStencilTarget to be destroyed.
 */
REFRESHAPI void REFRESH_AddDisposeDepthStencilTarget(
	REFRESH_Device *device,
	REFRESH_DepthStencilTarget *depthStencilTarget
);

/* Sends a framebuffer to be destroyed by the renderer. Note that we call it
 * "AddDispose" because it may not be immediately destroyed by the renderer if
 * this is not called from the main thread (for example, if a garbage collector
 * deletes the resource instead of the programmer).
 *
 * framebuffer: The REFRESH_Framebuffer to be destroyed.
 */
REFRESHAPI void REFRESH_AddDisposeFramebuffer(
	REFRESH_Device *device,
	REFRESH_Framebuffer *frameBuffer
);

/* Sends a shader module to be destroyed by the renderer. Note that we call it
 * "AddDispose" because it may not be immediately destroyed by the renderer if
 * this is not called from the main thread (for example, if a garbage collector
 * deletes the resource instead of the programmer).
 *
 * shaderModule: The REFRESH_ShaderModule to be destroyed.
 */
REFRESHAPI void REFRESH_AddDisposeShaderModule(
	REFRESH_Device *device,
	REFRESH_ShaderModule *shaderModule
);

/* Sends a render pass to be destroyed by the renderer. Note that we call it
 * "AddDispose" because it may not be immediately destroyed by the renderer if
 * this is not called from the main thread (for example, if a garbage collector
 * deletes the resource instead of the programmer).
 *
 * renderPass: The REFRESH_RenderPass to be destroyed.
 */
REFRESHAPI void REFRESH_AddDisposeRenderPass(
	REFRESH_Device *device,
	REFRESH_RenderPass *renderPass
);

/* Sends a graphics pipeline to be destroyed by the renderer. Note that we call it
 * "AddDispose" because it may not be immediately destroyed by the renderer if
 * this is not called from the main thread (for example, if a garbage collector
 * deletes the resource instead of the programmer).
 *
 * graphicsPipeline: The REFRESH_GraphicsPipeline to be destroyed.
 */
REFRESHAPI void REFRESH_AddDisposeGraphicsPipeline(
	REFRESH_Device *device,
	REFRESH_GraphicsPipeline *graphicsPipeline
);

/* Graphics State */

/* Begins a render pass.
 *
 * renderPass: The renderpass to begin.
 * framebuffer: The framebuffer to bind for the render pass.
 * renderArea:
 * 		The area affected by the render pass.
 * 		All load, store and resolve operations are restricted
 * 		to the given rectangle.
 * clearValues:
 * 		A pointer to an array of REFRESH_Color structures
 * 		that contains clear values for each color target in the
 * 		framebuffer. May be NULL.
 * clearCount: The amount of color structs in the above array.
 * depthStencilClearValue: The depth/stencil clear value. May be NULL.
 */
REFRESHAPI void REFRESH_BeginRenderPass(
	REFRESH_Device *device,
	REFRESH_RenderPass *renderPass,
	REFRESH_Framebuffer *framebuffer,
	REFRESH_Rect renderArea,
	REFRESH_Color *pColorClearValues,
	uint32_t colorClearCount,
	REFRESH_DepthStencilValue *depthStencilClearValue
);

/* Ends the current render pass. */
REFRESHAPI void REFRESH_EndRenderPass(
	REFRESH_Device *device
);

/* Binds a pipeline to the graphics bind point. */
REFRESHAPI void REFRESH_BindGraphicsPipeline(
	REFRESH_Device *device,
	REFRESH_GraphicsPipeline *graphicsPipeline
);

REFRESHAPI void REFRESH_BindVertexBuffers(
	REFRESH_Device *device,
	uint32_t firstBinding,
	uint32_t bindingCount,
	REFRESH_Buffer **pBuffers,
	uint64_t *pOffsets
);

REFRESHAPI void REFRESH_BindIndexBuffer(
	REFRESH_Device *device,
	REFRESH_Buffer *buffer,
	uint64_t offset,
	REFRESH_IndexElementSize indexElementSize
);

/* Presentation */

REFRESHAPI void REFRESH_Present(
	REFRESH_Device *device,
	REFRESH_Rect *sourceRectangle,
	REFRESH_Rect *destinationRectangle
);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* REFRESH_H */

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
