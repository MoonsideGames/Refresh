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

/* Version API */

#define REFRESH_MAJOR_VERSION   1
#define REFRESH_MINOR_VERSION	15
#define REFRESH_PATCH_VERSION	4

#define REFRESH_COMPILED_VERSION ( \
	(REFRESH_MAJOR_VERSION * 100 * 100) + \
	(REFRESH_MINOR_VERSION * 100) + \
	(REFRESH_PATCH_VERSION) \
)

REFRESHAPI uint32_t Refresh_LinkedVersion(void);

/* Type Declarations */

typedef struct Refresh_Device Refresh_Device;
typedef struct Refresh_GpuBuffer Refresh_GpuBuffer;
typedef struct Refresh_CpuBuffer Refresh_CpuBuffer;
typedef struct Refresh_Texture Refresh_Texture;
typedef struct Refresh_Sampler Refresh_Sampler;
typedef struct Refresh_ShaderModule Refresh_ShaderModule;
typedef struct Refresh_ComputePipeline Refresh_ComputePipeline;
typedef struct Refresh_GraphicsPipeline Refresh_GraphicsPipeline;
typedef struct Refresh_CommandBuffer Refresh_CommandBuffer;
typedef struct Refresh_Fence Refresh_Fence;

typedef enum Refresh_PresentMode
{
	REFRESH_PRESENTMODE_IMMEDIATE,
	REFRESH_PRESENTMODE_MAILBOX,
	REFRESH_PRESENTMODE_FIFO,
	REFRESH_PRESENTMODE_FIFO_RELAXED
} Refresh_PresentMode;

typedef enum Refresh_PrimitiveType
{
	REFRESH_PRIMITIVETYPE_POINTLIST,
	REFRESH_PRIMITIVETYPE_LINELIST,
	REFRESH_PRIMITIVETYPE_LINESTRIP,
	REFRESH_PRIMITIVETYPE_TRIANGLELIST,
	REFRESH_PRIMITIVETYPE_TRIANGLESTRIP
} Refresh_PrimitiveType;

typedef enum Refresh_LoadOp
{
	REFRESH_LOADOP_LOAD,
	REFRESH_LOADOP_CLEAR,
	REFRESH_LOADOP_DONT_CARE
} Refresh_LoadOp;

typedef enum Refresh_StoreOp
{
	REFRESH_STOREOP_STORE,
	REFRESH_STOREOP_DONT_CARE
} Refresh_StoreOp;

typedef enum Refresh_IndexElementSize
{
	REFRESH_INDEXELEMENTSIZE_16BIT,
	REFRESH_INDEXELEMENTSIZE_32BIT
} Refresh_IndexElementSize;

typedef enum Refresh_TextureFormat
{
	/* Unsigned Normalized Float Color Formats */
	REFRESH_TEXTUREFORMAT_R8G8B8A8,
	REFRESH_TEXTUREFORMAT_B8G8R8A8,
	REFRESH_TEXTUREFORMAT_R5G6B5,
	REFRESH_TEXTUREFORMAT_A1R5G5B5,
	REFRESH_TEXTUREFORMAT_B4G4R4A4,
	REFRESH_TEXTUREFORMAT_A2R10G10B10,
	REFRESH_TEXTUREFORMAT_R16G16,
	REFRESH_TEXTUREFORMAT_R16G16B16A16,
	REFRESH_TEXTUREFORMAT_R8,
	/* Compressed Unsigned Normalized Float Color Formats */
	REFRESH_TEXTUREFORMAT_BC1,
	REFRESH_TEXTUREFORMAT_BC2,
	REFRESH_TEXTUREFORMAT_BC3,
	REFRESH_TEXTUREFORMAT_BC7,
	/* Signed Normalized Float Color Formats  */
	REFRESH_TEXTUREFORMAT_R8G8_SNORM,
	REFRESH_TEXTUREFORMAT_R8G8B8A8_SNORM,
	/* Signed Float Color Formats */
	REFRESH_TEXTUREFORMAT_R16_SFLOAT,
	REFRESH_TEXTUREFORMAT_R16G16_SFLOAT,
	REFRESH_TEXTUREFORMAT_R16G16B16A16_SFLOAT,
	REFRESH_TEXTUREFORMAT_R32_SFLOAT,
	REFRESH_TEXTUREFORMAT_R32G32_SFLOAT,
	REFRESH_TEXTUREFORMAT_R32G32B32A32_SFLOAT,
	/* Unsigned Integer Color Formats */
	REFRESH_TEXTUREFORMAT_R8_UINT,
	REFRESH_TEXTUREFORMAT_R8G8_UINT,
	REFRESH_TEXTUREFORMAT_R8G8B8A8_UINT,
	REFRESH_TEXTUREFORMAT_R16_UINT,
	REFRESH_TEXTUREFORMAT_R16G16_UINT,
	REFRESH_TEXTUREFORMAT_R16G16B16A16_UINT,
	/* Depth Formats */
	REFRESH_TEXTUREFORMAT_D16_UNORM,
	REFRESH_TEXTUREFORMAT_D32_SFLOAT,
	REFRESH_TEXTUREFORMAT_D16_UNORM_S8_UINT,
	REFRESH_TEXTUREFORMAT_D32_SFLOAT_S8_UINT
} Refresh_TextureFormat;

typedef enum Refresh_TextureUsageFlagBits
{
	REFRESH_TEXTUREUSAGE_SAMPLER_BIT              = 0x00000001,
	REFRESH_TEXTUREUSAGE_COLOR_TARGET_BIT         = 0x00000002,
	REFRESH_TEXTUREUSAGE_DEPTH_STENCIL_TARGET_BIT = 0x00000004,
	REFRESH_TEXTUREUSAGE_COMPUTE_BIT              = 0X00000008
} Refresh_TextureUsageFlagBits;

typedef uint32_t Refresh_TextureUsageFlags;

typedef enum Refresh_SampleCount
{
	REFRESH_SAMPLECOUNT_1,
	REFRESH_SAMPLECOUNT_2,
	REFRESH_SAMPLECOUNT_4,
	REFRESH_SAMPLECOUNT_8
} Refresh_SampleCount;

typedef enum Refresh_CubeMapFace
{
	REFRESH_CUBEMAPFACE_POSITIVEX,
	REFRESH_CUBEMAPFACE_NEGATIVEX,
	REFRESH_CUBEMAPFACE_POSITIVEY,
	REFRESH_CUBEMAPFACE_NEGATIVEY,
	REFRESH_CUBEMAPFACE_POSITIVEZ,
	REFRESH_CUBEMAPFACE_NEGATIVEZ
} Refresh_CubeMapFace;

typedef enum Refresh_BufferUsageFlagBits
{
	REFRESH_BUFFERUSAGE_VERTEX_BIT 	 = 0x00000001,
	REFRESH_BUFFERUSAGE_INDEX_BIT  	 = 0x00000002,
	REFRESH_BUFFERUSAGE_COMPUTE_BIT  = 0x00000004,
	REFRESH_BUFFERUSAGE_INDIRECT_BIT = 0x00000008
} Refresh_BufferUsageFlagBits;

typedef uint32_t Refresh_BufferUsageFlags;

typedef enum Refresh_VertexElementFormat
{
	REFRESH_VERTEXELEMENTFORMAT_UINT,
	REFRESH_VERTEXELEMENTFORMAT_FLOAT,
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
} Refresh_VertexElementFormat;

typedef enum Refresh_VertexInputRate
{
	REFRESH_VERTEXINPUTRATE_VERTEX = 0,
	REFRESH_VERTEXINPUTRATE_INSTANCE = 1
} Refresh_VertexInputRate;

typedef enum Refresh_FillMode
{
	REFRESH_FILLMODE_FILL,
	REFRESH_FILLMODE_LINE
} Refresh_FillMode;

typedef enum Refresh_CullMode
{
	REFRESH_CULLMODE_NONE,
	REFRESH_CULLMODE_FRONT,
	REFRESH_CULLMODE_BACK
} Refresh_CullMode;

typedef enum Refresh_FrontFace
{
	REFRESH_FRONTFACE_COUNTER_CLOCKWISE,
	REFRESH_FRONTFACE_CLOCKWISE
} Refresh_FrontFace;

typedef enum Refresh_CompareOp
{
	REFRESH_COMPAREOP_NEVER,
	REFRESH_COMPAREOP_LESS,
	REFRESH_COMPAREOP_EQUAL,
	REFRESH_COMPAREOP_LESS_OR_EQUAL,
	REFRESH_COMPAREOP_GREATER,
	REFRESH_COMPAREOP_NOT_EQUAL,
	REFRESH_COMPAREOP_GREATER_OR_EQUAL,
	REFRESH_COMPAREOP_ALWAYS
} Refresh_CompareOp;

typedef enum Refresh_StencilOp
{
	REFRESH_STENCILOP_KEEP,
	REFRESH_STENCILOP_ZERO,
	REFRESH_STENCILOP_REPLACE,
	REFRESH_STENCILOP_INCREMENT_AND_CLAMP,
	REFRESH_STENCILOP_DECREMENT_AND_CLAMP,
	REFRESH_STENCILOP_INVERT,
	REFRESH_STENCILOP_INCREMENT_AND_WRAP,
	REFRESH_STENCILOP_DECREMENT_AND_WRAP
} Refresh_StencilOp;

typedef enum Refresh_BlendOp
{
	REFRESH_BLENDOP_ADD,
	REFRESH_BLENDOP_SUBTRACT,
	REFRESH_BLENDOP_REVERSE_SUBTRACT,
	REFRESH_BLENDOP_MIN,
	REFRESH_BLENDOP_MAX
} Refresh_BlendOp;

typedef enum Refresh_BlendFactor
{
	REFRESH_BLENDFACTOR_ZERO,
	REFRESH_BLENDFACTOR_ONE,
	REFRESH_BLENDFACTOR_SRC_COLOR,
	REFRESH_BLENDFACTOR_ONE_MINUS_SRC_COLOR,
	REFRESH_BLENDFACTOR_DST_COLOR,
	REFRESH_BLENDFACTOR_ONE_MINUS_DST_COLOR,
	REFRESH_BLENDFACTOR_SRC_ALPHA,
	REFRESH_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
	REFRESH_BLENDFACTOR_DST_ALPHA,
	REFRESH_BLENDFACTOR_ONE_MINUS_DST_ALPHA,
	REFRESH_BLENDFACTOR_CONSTANT_COLOR,
	REFRESH_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR,
	REFRESH_BLENDFACTOR_SRC_ALPHA_SATURATE
} Refresh_BlendFactor;

typedef enum Refresh_ColorComponentFlagBits
{
	REFRESH_COLORCOMPONENT_R_BIT = 0x00000001,
	REFRESH_COLORCOMPONENT_G_BIT = 0x00000002,
	REFRESH_COLORCOMPONENT_B_BIT = 0x00000004,
	REFRESH_COLORCOMPONENT_A_BIT = 0x00000008
} Refresh_ColorComponentFlagBits;

typedef uint32_t Refresh_ColorComponentFlags;

typedef enum Refresh_Filter
{
	REFRESH_FILTER_NEAREST,
	REFRESH_FILTER_LINEAR
} Refresh_Filter;

typedef enum Refresh_SamplerMipmapMode
{
	REFRESH_SAMPLERMIPMAPMODE_NEAREST,
	REFRESH_SAMPLERMIPMAPMODE_LINEAR
} Refresh_SamplerMipmapMode;

typedef enum Refresh_SamplerAddressMode
{
	REFRESH_SAMPLERADDRESSMODE_REPEAT,
	REFRESH_SAMPLERADDRESSMODE_MIRRORED_REPEAT,
	REFRESH_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
	REFRESH_SAMPLERADDRESSMODE_CLAMP_TO_BORDER
} Refresh_SamplerAddressMode;

/* FIXME: we should probably make a library-level decision about color types */
typedef enum Refresh_BorderColor
{
	REFRESH_BORDERCOLOR_FLOAT_TRANSPARENT_BLACK,
	REFRESH_BORDERCOLOR_INT_TRANSPARENT_BLACK,
	REFRESH_BORDERCOLOR_FLOAT_OPAQUE_BLACK,
	REFRESH_BORDERCOLOR_INT_OPAQUE_BLACK,
	REFRESH_BORDERCOLOR_FLOAT_OPAQUE_WHITE,
	REFRESH_BORDERCOLOR_INT_OPAQUE_WHITE
} Refresh_BorderColor;

typedef enum Refresh_Backend
{
	REFRESH_BACKEND_DONTCARE,
	REFRESH_BACKEND_VULKAN,
	REFRESH_BACKEND_PS5,
	REFRESH_BACKEND_INVALID
} Refresh_Backend;

/* Structures */

typedef struct Refresh_DepthStencilValue
{
	float depth;
	uint32_t stencil;
} Refresh_DepthStencilValue;

typedef struct Refresh_Rect
{
	int32_t x;
	int32_t y;
	int32_t w;
	int32_t h;
} Refresh_Rect;

typedef struct Refresh_Vec4
{
	float x;
	float y;
	float z;
	float w;
} Refresh_Vec4;

typedef struct Refresh_Viewport
{
	float x;
	float y;
	float w;
	float h;
	float minDepth;
	float maxDepth;
} Refresh_Viewport;

typedef struct Refresh_TextureSlice
{
	Refresh_Texture *texture;
	uint32_t mipLevel;
	uint32_t baseLayer;
	uint32_t layerCount;
	uint32_t x;
	uint32_t y;
	uint32_t z;
	uint32_t w;
	uint32_t h;
	uint32_t d;
} Refresh_TextureSlice;

typedef struct Refresh_BufferImageCopy
{
	uint32_t bufferOffset;
	uint32_t bufferStride;
	uint32_t bufferImageHeight;
} Refresh_BufferImageCopy;

typedef struct Refresh_BufferCopy
{
	uint32_t srcOffset;
	uint32_t dstOffset;
	uint32_t size;
} Refresh_BufferCopy;

typedef struct Refresh_IndirectDrawCommand
{
	uint32_t vertexCount;
	uint32_t instanceCount;
	uint32_t firstVertex;
	uint32_t firstInstance;
} Refresh_IndirectDrawCommand;

/* State structures */

typedef struct Refresh_SamplerStateCreateInfo
{
	Refresh_Filter minFilter;
	Refresh_Filter magFilter;
	Refresh_SamplerMipmapMode mipmapMode;
	Refresh_SamplerAddressMode addressModeU;
	Refresh_SamplerAddressMode addressModeV;
	Refresh_SamplerAddressMode addressModeW;
	float mipLodBias;
	uint8_t anisotropyEnable;
	float maxAnisotropy;
	uint8_t compareEnable;
	Refresh_CompareOp compareOp;
	float minLod;
	float maxLod;
	Refresh_BorderColor borderColor;
} Refresh_SamplerStateCreateInfo;

typedef struct Refresh_VertexBinding
{
	uint32_t binding;
	uint32_t stride;
	Refresh_VertexInputRate inputRate;
} Refresh_VertexBinding;

typedef struct Refresh_VertexAttribute
{
	uint32_t location;
	uint32_t binding;
	Refresh_VertexElementFormat format;
	uint32_t offset;
} Refresh_VertexAttribute;

typedef struct Refresh_VertexInputState
{
	const Refresh_VertexBinding *vertexBindings;
	uint32_t vertexBindingCount;
	const Refresh_VertexAttribute *vertexAttributes;
	uint32_t vertexAttributeCount;
} Refresh_VertexInputState;

typedef struct Refresh_StencilOpState
{
	Refresh_StencilOp failOp;
	Refresh_StencilOp passOp;
	Refresh_StencilOp depthFailOp;
	Refresh_CompareOp compareOp;
	uint32_t compareMask;
	uint32_t writeMask;
	uint32_t reference;
} Refresh_StencilOpState;

typedef struct Refresh_ColorAttachmentBlendState
{
	uint8_t blendEnable;
	Refresh_BlendFactor srcColorBlendFactor;
	Refresh_BlendFactor dstColorBlendFactor;
	Refresh_BlendOp colorBlendOp;
	Refresh_BlendFactor srcAlphaBlendFactor;
	Refresh_BlendFactor dstAlphaBlendFactor;
	Refresh_BlendOp alphaBlendOp;
	Refresh_ColorComponentFlags colorWriteMask;
} Refresh_ColorAttachmentBlendState;

typedef struct Refresh_ShaderModuleCreateInfo
{
	size_t codeSize;
	const uint32_t *byteCode;
} Refresh_ShaderModuleCreateInfo;

typedef struct Refresh_TextureCreateInfo
{
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	uint8_t isCube;
	uint32_t levelCount;
	Refresh_SampleCount sampleCount;
	Refresh_TextureFormat format;
	Refresh_TextureUsageFlags usageFlags;
} Refresh_TextureCreateInfo;

/* Pipeline state structures */

typedef struct Refresh_GraphicsShaderInfo
{
	Refresh_ShaderModule *shaderModule;
	const char* entryPointName;
	uint32_t uniformBufferSize;
	uint32_t samplerBindingCount;
} Refresh_GraphicsShaderInfo;

typedef struct Refresh_ComputeShaderInfo
{
	Refresh_ShaderModule* shaderModule;
	const char* entryPointName;
	uint32_t uniformBufferSize;
	uint32_t bufferBindingCount;
	uint32_t imageBindingCount;
} Refresh_ComputeShaderInfo;

typedef struct Refresh_RasterizerState
{
	Refresh_FillMode fillMode;
	Refresh_CullMode cullMode;
	Refresh_FrontFace frontFace;
	uint8_t depthBiasEnable;
	float depthBiasConstantFactor;
	float depthBiasClamp;
	float depthBiasSlopeFactor;
} Refresh_RasterizerState;

typedef struct Refresh_MultisampleState
{
	Refresh_SampleCount multisampleCount;
	uint32_t sampleMask;
} Refresh_MultisampleState;

typedef struct Refresh_DepthStencilState
{
	uint8_t depthTestEnable;
	uint8_t depthWriteEnable;
	Refresh_CompareOp compareOp;
	uint8_t depthBoundsTestEnable;
	uint8_t stencilTestEnable;
	Refresh_StencilOpState stencilState;
	float minDepthBounds;
	float maxDepthBounds;
} Refresh_DepthStencilState;

typedef struct Refresh_ColorAttachmentDescription
{
	Refresh_TextureFormat format;
	Refresh_ColorAttachmentBlendState blendState;
} Refresh_ColorAttachmentDescription;

typedef struct Refresh_GraphicsPipelineAttachmentInfo
{
	Refresh_ColorAttachmentDescription *colorAttachmentDescriptions;
	uint32_t colorAttachmentCount;
	uint8_t hasDepthStencilAttachment;
	Refresh_TextureFormat depthStencilFormat;
} Refresh_GraphicsPipelineAttachmentInfo;

typedef struct Refresh_GraphicsPipelineCreateInfo
{
	Refresh_GraphicsShaderInfo vertexShaderInfo;
	Refresh_GraphicsShaderInfo fragmentShaderInfo;
	Refresh_VertexInputState vertexInputState;
	Refresh_PrimitiveType primitiveType;
	Refresh_RasterizerState rasterizerState;
	Refresh_MultisampleState multisampleState;
	Refresh_DepthStencilState depthStencilState;
	Refresh_GraphicsPipelineAttachmentInfo attachmentInfo;
	float blendConstants[4];
} Refresh_GraphicsPipelineCreateInfo;

/* Render pass structures */

typedef struct Refresh_ColorAttachmentInfo
{
	Refresh_Texture *texture; /* We can't use TextureSlice because render passes take a single rectangle. */
	uint32_t depth;
	uint32_t layer;
	uint32_t level;
	Refresh_Vec4 clearColor; /* Can be ignored by RenderPass */
	Refresh_LoadOp loadOp;
	Refresh_StoreOp storeOp;
} Refresh_ColorAttachmentInfo;

typedef struct Refresh_DepthStencilAttachmentInfo
{
	Refresh_Texture *texture; /* We can't use TextureSlice because render passes take a single rectangle. */
	uint32_t depth;
	uint32_t layer;
	uint32_t level;
	Refresh_DepthStencilValue depthStencilClearValue; /* Can be ignored by RenderPass */
	Refresh_LoadOp loadOp;
	Refresh_StoreOp storeOp;
	Refresh_LoadOp stencilLoadOp;
	Refresh_StoreOp stencilStoreOp;
} Refresh_DepthStencilAttachmentInfo;

/* Functions */

/* Logging */

typedef void (REFRESHCALL * Refresh_LogFunc)(const char *msg);

/* Reroutes Refresh's logging to custom logging functions.
 *
 * info:	Basic logs that might be useful to have stored for support.
 * warn:	Something went wrong, but it's really just annoying, not fatal.
 * error:	You better have this stored somewhere because it's crashing now!
 */
REFRESHAPI void Refresh_HookLogFunctions(
	Refresh_LogFunc info,
	Refresh_LogFunc warn,
	Refresh_LogFunc error
);

/* Backend selection */

/* Select the graphics API backend that Refresh should use.
 *
 * Note that Refresh is not required to select your preferred backend
 * if it detects an incompatibility.
 *
 * Returns the backend that will actually be used, and fills in a window flag bitmask.
 * This bitmask should be used to create all windows that the device claims.
 *
 * preferredBackend: The preferred backend that Refresh should select.
 * flags: A pointer to a bitflag value that will be filled in with required SDL_WindowFlags masks.
 */
REFRESHAPI Refresh_Backend Refresh_SelectBackend(Refresh_Backend preferredBackend, uint32_t *flags);

/* Device */

/* Create a rendering context for use on the calling thread.
 * You MUST have called Refresh_SelectDriver prior to calling this function.
 *
 * debugMode: Enable debug mode properties.
 */
REFRESHAPI Refresh_Device* Refresh_CreateDevice(
	uint8_t debugMode
);

/* Destroys a rendering context previously returned by Refresh_CreateDevice. */
REFRESHAPI void Refresh_DestroyDevice(Refresh_Device *device);

/* State Creation */

/* Returns an allocated ComputePipeline* object. */
REFRESHAPI Refresh_ComputePipeline* Refresh_CreateComputePipeline(
	Refresh_Device *device,
	Refresh_ComputeShaderInfo *computeShaderInfo
);

/* Returns an allocated GraphicsPipeline* object. */
REFRESHAPI Refresh_GraphicsPipeline* Refresh_CreateGraphicsPipeline(
	Refresh_Device *device,
	Refresh_GraphicsPipelineCreateInfo *pipelineCreateInfo
);

/* Returns an allocated Sampler* object. */
REFRESHAPI Refresh_Sampler* Refresh_CreateSampler(
	Refresh_Device *device,
	Refresh_SamplerStateCreateInfo *samplerStateCreateInfo
);

/* Returns an allocated ShaderModule* object. */
REFRESHAPI Refresh_ShaderModule* Refresh_CreateShaderModule(
	Refresh_Device *device,
	Refresh_ShaderModuleCreateInfo *shaderModuleCreateInfo
);

/* Returns an allocated Refresh_Texture* object. Note that the contents of
 * the texture are undefined until SetData is called.
 */
REFRESHAPI Refresh_Texture* Refresh_CreateTexture(
	Refresh_Device *device,
	Refresh_TextureCreateInfo *textureCreateInfo
);

/* Creates a GpuBuffer.
 *
 * usageFlags:	Specifies how the buffer will be used.
 * sizeInBytes:	The length of the buffer.
 */
REFRESHAPI Refresh_GpuBuffer* Refresh_CreateGpuBuffer(
	Refresh_Device *device,
	Refresh_BufferUsageFlags usageFlags,
	uint32_t sizeInBytes
);

/* Creates a CpuBuffer.
 *
 * sizeInBytes: The length of the buffer.
 */
REFRESHAPI Refresh_CpuBuffer* Refresh_CreateCpuBuffer(
	Refresh_Device *device,
	uint32_t sizeInBytes
);

/* Disposal */

/* Sends a texture to be destroyed by the renderer. Note that we call it
 * "QueueDestroy" because it may not be immediately destroyed by the renderer if
 * this is not called from the main thread (for example, if a garbage collector
 * deletes the resource instead of the programmer).
 *
 * texture: The Refresh_Texture to be destroyed.
 */
REFRESHAPI void Refresh_QueueDestroyTexture(
	Refresh_Device *device,
	Refresh_Texture *texture
);

/* Sends a sampler to be destroyed by the renderer. Note that we call it
 * "QueueDestroy" because it may not be immediately destroyed by the renderer if
 * this is not called from the main thread (for example, if a garbage collector
 * deletes the resource instead of the programmer).
 *
 * texture: The Refresh_Sampler to be destroyed.
 */
REFRESHAPI void Refresh_QueueDestroySampler(
	Refresh_Device *device,
	Refresh_Sampler *sampler
);

/* Sends a buffer to be destroyed by the renderer. Note that we call it
 * "QueueDestroy" because it may not be immediately destroyed by the renderer.
 *
 * buffer: The Refresh_GpuBuffer to be destroyed.
 */
REFRESHAPI void Refresh_QueueDestroyGpuBuffer(
	Refresh_Device *device,
	Refresh_GpuBuffer *buffer
);

/* Sends a buffer to be destroyed by the renderer. Note that we call it
 * "QueueDestroy" because it may not be immediately destroyed by the renderer.
 *
 * buffer: The Refresh_CpuBuffer to be destroyed.
 */
REFRESHAPI void Refresh_QueueDestroyCpuBuffer(
	Refresh_Device *device,
	Refresh_CpuBuffer *buffer
);

/* Sends a shader module to be destroyed by the renderer. Note that we call it
 * "QueueDestroy" because it may not be immediately destroyed by the renderer.
 *
 * shaderModule: The Refresh_ShaderModule to be destroyed.
 */
REFRESHAPI void Refresh_QueueDestroyShaderModule(
	Refresh_Device *device,
	Refresh_ShaderModule *shaderModule
);

/* Sends a compute pipeline to be destroyed by the renderer. Note that we call it
 * "QueueDestroy" because it may not be immediately destroyed by the renderer.
 *
 * computePipeline: The Refresh_ComputePipeline to be destroyed.
 */
REFRESHAPI void Refresh_QueueDestroyComputePipeline(
	Refresh_Device *device,
	Refresh_ComputePipeline *computePipeline
);

/* Sends a graphics pipeline to be destroyed by the renderer. Note that we call it
 * "QueueDestroy" because it may not be immediately destroyed by the renderer.
 *
 * graphicsPipeline: The Refresh_GraphicsPipeline to be destroyed.
 */
REFRESHAPI void Refresh_QueueDestroyGraphicsPipeline(
	Refresh_Device *device,
	Refresh_GraphicsPipeline *graphicsPipeline
);

/* Graphics State */

/* Begins a render pass.
 * This will also set a default viewport and scissor state.
 *
 * colorAttachmentInfos:
 * 		A pointer to an array of Refresh_ColorAttachmentInfo structures
 * 		that contains render targets and clear values. May be NULL.
 * colorAttachmentCount: The amount of structs in the above array.
 * depthStencilAttachmentInfo: The depth/stencil render target and clear value. May be NULL.
 */
REFRESHAPI void Refresh_BeginRenderPass(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_ColorAttachmentInfo *colorAttachmentInfos,
	uint32_t colorAttachmentCount,
	Refresh_DepthStencilAttachmentInfo *depthStencilAttachmentInfo
);

/* Binds a graphics pipeline to the graphics bind point. */
REFRESHAPI void Refresh_BindGraphicsPipeline(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_GraphicsPipeline *graphicsPipeline
);

/* Sets the current viewport state. */
REFRESHAPI void Refresh_SetViewport(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Viewport *viewport
);

/* Sets the current scissor state. */
REFRESHAPI void Refresh_SetScissor(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Rect *scissor
);

/* Binds vertex buffers for use with subsequent draw calls.
 * Note that this may only be called after binding a graphics pipeline.
 */
REFRESHAPI void Refresh_BindVertexBuffers(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	uint32_t firstBinding,
	uint32_t bindingCount,
	Refresh_GpuBuffer **pBuffers,
	uint32_t *pOffsets
);

/* Binds an index buffer for use with subsequent draw calls. */
REFRESHAPI void Refresh_BindIndexBuffer(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_GpuBuffer *buffer,
	uint32_t offset,
	Refresh_IndexElementSize indexElementSize
);

/* Sets textures/samplers for use with the currently bound vertex shader.
 *
 * NOTE:
 * 		The length of the passed arrays must be equal to the number
 * 		of sampler bindings specified by the pipeline.
 *
 * textures:	A pointer to an array of textures.
 * samplers:	A pointer to an array of samplers.
 */
REFRESHAPI void Refresh_BindVertexSamplers(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Texture **pTextures,
	Refresh_Sampler **pSamplers
);

/* Sets textures/samplers for use with the currently bound fragment shader.
 *
 * NOTE:
 *		The length of the passed arrays must be equal to the number
 * 		of sampler bindings specified by the pipeline.
 *
 * textures: 	A pointer to an array of textures.
 * samplers:	A pointer to an array of samplers.
 */
REFRESHAPI void Refresh_BindFragmentSamplers(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Texture **pTextures,
	Refresh_Sampler **pSamplers
);

/* Pushes vertex shader params to the device.
 * Returns a starting offset value to be used with draw calls.
 *
 * NOTE:
 * 		A pipeline must be bound.
 * 		Will use the block size of the currently bound vertex shader.
 *
 * data: 				The client data to write into the buffer.
 * dataLengthInBytes: 	The length of the data to write.
 */
REFRESHAPI uint32_t Refresh_PushVertexShaderUniforms(
	Refresh_Device *device,
	Refresh_CommandBuffer * commandBuffer,
	void *data,
	uint32_t dataLengthInBytes
);

/* Pushes fragment shader params to the device.
 * Returns a starting offset value to be used with draw calls.
 *
 * NOTE:
 * 		A graphics pipeline must be bound.
 * 		Will use the block size of the currently bound fragment shader.
 *
 * data: 				The client data to write into the buffer.
 * dataLengthInBytes: 	The length of the data to write.
 */
REFRESHAPI uint32_t Refresh_PushFragmentShaderUniforms(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	void *data,
	uint32_t dataLengthInBytes
);

/* Drawing */

/* Draws data from vertex/index buffers with instancing enabled.
 *
 * baseVertex:          The starting offset to read from the vertex buffer.
 * startIndex:          The starting offset to read from the index buffer.
 * primitiveCount:      The number of primitives to draw.
 * instanceCount:       The number of instances that will be drawn.
 * vertexParamOffset:   The offset of the vertex shader param data.
 * fragmentParamOffset: The offset of the fragment shader param data.
 */
REFRESHAPI void Refresh_DrawInstancedPrimitives(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	uint32_t baseVertex,
	uint32_t startIndex,
	uint32_t primitiveCount,
	uint32_t instanceCount,
	uint32_t vertexParamOffset,
	uint32_t fragmentParamOffset
);

/* Draws data from vertex/index buffers.
 *
 * baseVertex:          The starting offset to read from the vertex buffer.
 * startIndex:          The starting offset to read from the index buffer.
 * primitiveCount:      The number of primitives to draw.
 * vertexParamOffset:   The offset of the vertex shader param data.
 * fragmentParamOffset: The offset of the fragment shader param data.
 */
REFRESHAPI void Refresh_DrawIndexedPrimitives(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	uint32_t baseVertex,
	uint32_t startIndex,
	uint32_t primitiveCount,
	uint32_t vertexParamOffset,
	uint32_t fragmentParamOffset
);

/* Draws data from vertex buffers.
 *
 * vertexStart:			The starting offset to read from the vertex buffer.
 * primitiveCount:		The number of primitives to draw.
 * vertexParamOffset:	The offset of the vertex shader param data.
 * fragmentParamOffset:	The offset of the fragment shader param data.
 */
REFRESHAPI void Refresh_DrawPrimitives(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	uint32_t vertexStart,
	uint32_t primitiveCount,
	uint32_t vertexParamOffset,
	uint32_t fragmentParamOffset
);

/* Similar to Refresh_DrawPrimitives, but draw parameters are set from a buffer.
 * The buffer layout should match the layout of Refresh_IndirectDrawCommand.
 *
 * buffer:              A buffer containing draw parameters.
 * offsetInBytes:       The offset to start reading from the draw buffer.
 * drawCount:           The number of draw parameter sets that should be read from the draw buffer.
 * stride:              The byte stride between sets of draw parameters.
 * vertexParamOffset:   The offset of the vertex shader param data.
 * fragmentParamOffset:	The offset of the fragment shader param data.
 */
REFRESHAPI void Refresh_DrawPrimitivesIndirect(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_GpuBuffer *buffer,
	uint32_t offsetInBytes,
	uint32_t drawCount,
	uint32_t stride,
	uint32_t vertexParamOffset,
	uint32_t fragmentParamOffset
);

/* Ends the current render pass. */
REFRESHAPI void Refresh_EndRenderPass(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer
);


/* Begins a compute pass. */
REFRESHAPI void Refresh_BeginComputePass(Refresh_Device *device);

/* Binds a compute pipeline to the compute bind point. */
REFRESHAPI void Refresh_BindComputePipeline(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_ComputePipeline *computePipeline
);

/* Binds buffers for use with the currently bound compute pipeline.
 *
 * pBuffers: An array of buffers to bind.
 * 	Length must be equal to the number of buffers
 * 	specified by the compute pipeline.
 */
REFRESHAPI void Refresh_BindComputeBuffers(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_GpuBuffer **pBuffers
);

/* Binds textures for use with the currently bound compute pipeline.
 *
 * pTextures: An array of textures to bind.
 * 	Length must be equal to the number of buffers
 * 	specified by the compute pipeline.
 *
 * pLevels: An array of levels to bind,
 *   corresponding to the indices in pTextures.
 *   Length must be equal to the number of buffers
 *   specified by the compute pipeline.
 */
REFRESHAPI void Refresh_BindComputeTextures(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Texture **pTextures,
	uint32_t **pLevels
);

/* Pushes compute shader params to the device.
 * Returns a starting offset value to be used with draw calls.
 *
 * NOTE:
 * 	A compute pipeline must be bound.
 * 	Will use the block size of the currently bound compute shader.
 *
 * data:				The client data to write into the buffer.
 * dataLengthInBytes:	The length of the data to write.
 */
REFRESHAPI uint32_t Refresh_PushComputeShaderUniforms(
	Refresh_Device *device,
	Refresh_CommandBuffer * commandBuffer,
	void *data,
	uint32_t dataLengthInBytes
);

/* Dispatches work compute items.
 *
 * groupCountX:			Number of local workgroups to dispatch in the X dimension.
 * groupCountY:			Number of local workgroups to dispatch in the Y dimension.
 * groupCountZ:			Number of local workgroups to dispatch in the Z dimension.
 * computeParamOffset:	The offset of the compute shader param data.
 */
REFRESHAPI void Refresh_DispatchCompute(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	uint32_t groupCountX,
	uint32_t groupCountY,
	uint32_t groupCountZ,
	uint32_t computeParamOffset
);

/* Ends the current compute pass. */
REFRESHAPI void Refresh_EndComputePass(Refresh_Device *device);

/* CpuBuffer Map */

/* Maps a CpuBuffer to host memory. Returns a pointer that can be copied to.
 *
 * offsetInBytes: The offset of the buffer to map.
 * sizeInBytes: The number of bytes of memory to map.
 */
REFRESHAPI void* Refresh_MapCpuBuffer(
	Refresh_Device *device,
	Refresh_CpuBuffer *buffer,
	uint32_t offsetInBytes,
	uint32_t sizeInBytes
);

/* Unmaps a mapped CpuBuffer. Call this when you are done copying data. */
REFRESHAPI void Refresh_UnmapCpuBuffer(
	Refresh_Device *device,
	Refresh_CpuBuffer *buffer
);

/* Copy Pass */

/* Begins a copy pass. */
REFRESHAPI void Refresh_BeginCopyPass(Refresh_Device *device);

/* CPU-to-GPU copies occur on the GPU timeline.
 *
 * You MUST NOT alter the data until the command buffer
 * has finished execution.
 *
 * You MAY assume that the copy has finished for subsequent commands.
 */

/* Uploads data from a CpuBuffer to a texture. */
REFRESHAPI void Refresh_UploadToTexture(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_CpuBuffer *cpuBuffer,
	Refresh_BufferImageCopy *copyParams,
	Refresh_TextureSlice *textureSlice
);

/* Uploads data from a CpuBuffer to a GpuBuffer. */
REFRESHAPI void Refresh_UploadToBuffer(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_CpuBuffer *cpuBuffer,
	Refresh_GpuBuffer *gpuBuffer,
	Refresh_BufferCopy *copyParams
);

/* GPU-to-CPU copies occur on the GPU timeline.
 *
 * You may NOT assume that the data in the CpuBuffer is valid
 * until the command buffer has finished execution.
 */

/* Downloads data from a texture to a CpuBuffer. */
REFRESHAPI void Refresh_DownloadFromTexture(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_TextureSlice *textureSlice,
	Refresh_CpuBuffer *cpuBuffer,
	Refresh_BufferImageCopy *copyParams
);

/* Downloads data from a GpuBuffer object. */
REFRESHAPI void Refresh_DownloadFromBuffer(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_GpuBuffer *gpuBuffer,
	Refresh_CpuBuffer *cpuBuffer,
	Refresh_BufferCopy *copyParams
);

/* GPU-to-GPU copies occur on the GPU timeline,
 * and you may assume the copy has finished in subsequent commands.
 */

/* Performs a texture-to-texture copy. */
REFRESHAPI void Refresh_CopyTextureToTexture(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_TextureSlice *source,
	Refresh_TextureSlice *destination
);

/* Copies image data from a texture slice into a buffer. */
REFRESHAPI void Refresh_CopyTextureToBuffer(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_TextureSlice *textureSlice,
	Refresh_GpuBuffer *buffer,
	Refresh_BufferImageCopy *copyParameters
);

/* Copies data from a buffer to a texture slice. */
REFRESHAPI void Refresh_CopyBufferToTexture(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_GpuBuffer *buffer,
	Refresh_BufferImageCopy *copyParams,
	Refresh_TextureSlice *textureSlice
);

/* Copies data from a buffer to a buffer. */
REFRESHAPI void Refresh_CopyBufferToBuffer(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_GpuBuffer *source,
	Refresh_GpuBuffer *destination,
	Refresh_BufferCopy *copyParams
);

/* Generate mipmaps for the given texture. */
REFRESHAPI void Refresh_GenerateMipmaps(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Texture *texture
);

REFRESHAPI void Refresh_EndCopyPass(Refresh_Device *device);

/* Ends a copy pass. */

/* Submission/Presentation */

/* Claims a window, creating a swapchain structure for it.
 * This function MUST be called before any swapchain functions
 * are called using the window.
 *
 * Returns 0 on swapchain creation failure.
 */
REFRESHAPI uint8_t Refresh_ClaimWindow(
	Refresh_Device *device,
	void *windowHandle,
	Refresh_PresentMode presentMode
);

/* Unclaims a window, destroying the swapchain structure for it.
 * It is good practice to call this when a window is closed to
 * prevent memory bloat, but windows are automatically unclaimed
 * by DestroyDevice.
 */
REFRESHAPI void Refresh_UnclaimWindow(
	Refresh_Device *device,
	void *windowHandle
);

/* Changes the present mode of the swapchain for the given window. */
REFRESHAPI void Refresh_SetSwapchainPresentMode(
	Refresh_Device *device,
	void *windowHandle,
	Refresh_PresentMode presentMode
);

/* Returns the format of the swapchain for the given window. */
REFRESHAPI Refresh_TextureFormat Refresh_GetSwapchainFormat(
	Refresh_Device *device,
	void *windowHandle
);

/* Returns an allocated Refresh_CommandBuffer* object.
 * This command buffer is managed by the implementation and
 * should NOT be freed by the user.
 *
 * NOTE:
 * 	A command buffer may only be used on the thread that
 * 	it was acquired on. Using it on any other thread is an error.
 *
 */
REFRESHAPI Refresh_CommandBuffer* Refresh_AcquireCommandBuffer(
	Refresh_Device *device
);

/* Acquires a texture to use for presentation.
 * May return NULL under certain conditions.
 * If NULL, the user must ensure to not use the texture.
 * Once a swapchain texture is acquired,
 * it will automatically be presented on command buffer submission.
 *
 * NOTE:
 * 	It is not recommended to hold a reference to this texture long term.
 *
 * pWidth: A pointer to a uint32 that will be filled with the texture width.
 * pHeight: A pointer to a uint32 that will be filled with the texture height.
 */
REFRESHAPI Refresh_Texture* Refresh_AcquireSwapchainTexture(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	void *windowHandle,
	uint32_t *pWidth,
	uint32_t *pHeight
);

/* Submits all of the enqueued commands. */
REFRESHAPI void Refresh_Submit(
	Refresh_Device* device,
	Refresh_CommandBuffer *commandBuffer
);

/* Submits a command buffer and acquires a fence.
 * You can use the fence to check if or wait until the command buffer has finished processing.
 * You are responsible for releasing this fence when you are done using it.
 */
REFRESHAPI Refresh_Fence* Refresh_SubmitAndAcquireFence(
	Refresh_Device* device,
	Refresh_CommandBuffer *commandBuffer
);

/* Waits for the device to become idle. */
REFRESHAPI void Refresh_Wait(
	Refresh_Device *device
);

/* Waits for given fences to be signaled.
 *
 * waitAll: If 0, waits for any fence to be signaled. If 1, waits for all fences to be signaled.
 * fenceCount: The number of fences being submitted.
 * pFences: An array of fences to be waited on.
 */
REFRESHAPI void Refresh_WaitForFences(
	Refresh_Device *device,
	uint8_t waitAll,
	uint32_t fenceCount,
	Refresh_Fence **pFences
);

/* Check the status of a fence. 1 means the fence is signaled. */
REFRESHAPI int Refresh_QueryFence(
	Refresh_Device *device,
	Refresh_Fence *fence
);

/* Allows the fence to be reused by future command buffer submissions.
 * If you do not release fences after acquiring them, you will cause unbounded resource growth.
 */
REFRESHAPI void Refresh_ReleaseFence(
	Refresh_Device *device,
	Refresh_Fence *fence
);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* REFRESH_H */

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
