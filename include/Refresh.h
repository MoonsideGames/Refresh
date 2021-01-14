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

#define VK_DEFINE_HANDLE(object) typedef struct object##_T* object;

VK_DEFINE_HANDLE(VkInstance)
VK_DEFINE_HANDLE(VkDevice)
VK_DEFINE_HANDLE(VkPhysicalDevice)

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Type Declarations */

typedef struct Refresh_Device Refresh_Device;
typedef struct Refresh_Buffer Refresh_Buffer;
typedef struct Refresh_Texture Refresh_Texture;
typedef struct Refresh_Sampler Refresh_Sampler;
typedef struct Refresh_ColorTarget Refresh_ColorTarget;
typedef struct Refresh_DepthStencilTarget Refresh_DepthStencilTarget;
typedef struct Refresh_Framebuffer Refresh_Framebuffer;
typedef struct Refresh_ShaderModule Refresh_ShaderModule;
typedef struct Refresh_RenderPass Refresh_RenderPass;
typedef struct Refresh_ComputePipeline Refresh_ComputePipeline;
typedef struct Refresh_GraphicsPipeline Refresh_GraphicsPipeline;
typedef struct Refresh_CommandBuffer Refresh_CommandBuffer;

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

typedef enum Refresh_ClearOptionsBits
{
    REFRESH_CLEAROPTIONS_COLOR   = 0x00000001,
    REFRESH_CLEAROPTIONS_DEPTH   = 0x00000002,
    REFRESH_CLEAROPTIONS_STENCIL = 0x00000004,
} Refresh_ClearOptionsBits;

typedef uint32_t Refresh_ClearOptions;

typedef enum Refresh_IndexElementSize
{
    REFRESH_INDEXELEMENTSIZE_16BIT,
    REFRESH_INDEXELEMENTSIZE_32BIT
} Refresh_IndexElementSize;

typedef enum Refresh_ColorFormat
{
    REFRESH_COLORFORMAT_R8G8B8A8,
    REFRESH_COLORFORMAT_R5G6B5,
    REFRESH_COLORFORMAT_A1R5G5B5,
    REFRESH_COLORFORMAT_B4G4R4A4,
    REFRESH_COLORFORMAT_BC1,
    REFRESH_COLORFORMAT_BC2,
    REFRESH_COLORFORMAT_BC3,
    REFRESH_COLORFORMAT_R8G8_SNORM,
    REFRESH_COLORFORMAT_R8G8B8A8_SNORM,
    REFRESH_COLORFORMAT_A2R10G10B10,
    REFRESH_COLORFORMAT_R16G16,
    REFRESH_COLORFORMAT_R16G16B16A16,
    REFRESH_COLORFORMAT_R8,
    REFRESH_COLORFORMAT_R32_SFLOAT,
    REFRESH_COLORFORMAT_R32G32_SFLOAT,
    REFRESH_COLORFORMAT_R32G32B32A32_SFLOAT,
    REFRESH_COLORFORMAT_R16_SFLOAT,
    REFRESH_COLORFORMAT_R16G16_SFLOAT,
    REFRESH_COLORFORMAT_R16G16B16A16_SFLOAT
} Refresh_ColorFormat;

typedef enum Refresh_DepthFormat
{
	REFRESH_DEPTHFORMAT_D16_UNORM,
	REFRESH_DEPTHFORMAT_D32_SFLOAT,
    REFRESH_DEPTHFORMAT_D16_UNORM_S8_UINT,
    REFRESH_DEPTHFORMAT_D32_SFLOAT_S8_UINT
} Refresh_DepthFormat;

typedef enum Refresh_TextureUsageFlagBits
{
	REFRESH_TEXTUREUSAGE_SAMPLER_BIT          = 0x00000001,
	REFRESH_TEXTUREUSAGE_COLOR_TARGET_BIT     = 0x00000002
} Refresh_TextureUsageFlagBits;

typedef uint32_t Refresh_TextureUsageFlags;

typedef enum Refresh_SampleCount
{
	REFRESH_SAMPLECOUNT_1,
	REFRESH_SAMPLECOUNT_2,
	REFRESH_SAMPLECOUNT_4,
	REFRESH_SAMPLECOUNT_8,
	REFRESH_SAMPLECOUNT_16,
	REFRESH_SAMPLECOUNT_32,
	REFRESH_SAMPLECOUNT_64
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
	REFRESH_BUFFERUSAGE_VERTEX_BIT 	=	0x00000001,
	REFRESH_BUFFERUSAGE_INDEX_BIT  	=	0x00000002,
	REFRESH_BUFFERUSAGE_COMPUTE_BIT =	0x00000004
} Refresh_BufferUsageFlagBits;

typedef uint32_t Refresh_BufferUsageFlags;

typedef enum Refresh_VertexElementFormat
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
} Refresh_VertexElementFormat;

typedef enum Refresh_VertexInputRate
{
	REFRESH_VERTEXINPUTRATE_VERTEX = 0,
	REFRESH_VERTEXINPUTRATE_INSTANCE = 1
} Refresh_VertexInputRate;

typedef enum Refresh_FillMode
{
	REFRESH_FILLMODE_FILL,
	REFRESH_FILLMODE_LINE,
	REFRESH_FILLMODE_POINT
} Refresh_FillMode;

typedef enum Refresh_CullMode
{
	REFRESH_CULLMODE_NONE,
	REFRESH_CULLMODE_FRONT,
	REFRESH_CULLMODE_BACK,
	REFRESH_CULLMODE_FRONT_AND_BACK
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

typedef enum Refresh_LogicOp
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
} Refresh_LogicOp;

typedef enum Refresh_BlendFactor
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
} Refresh_BlendFactor;

typedef enum Refresh_ColorComponentFlagBits
{
    REFRESH_COLORCOMPONENT_R_BIT = 0x00000001,
    REFRESH_COLORCOMPONENT_G_BIT = 0x00000002,
    REFRESH_COLORCOMPONENT_B_BIT = 0x00000004,
    REFRESH_COLORCOMPONENT_A_BIT = 0x00000008
} Refresh_ColorComponentFlagBits;

typedef uint32_t Refresh_ColorComponentFlags;

typedef enum Refresh_ShaderStageType
{
	REFRESH_SHADERSTAGE_VERTEX,
	REFRESH_SHADERSTAGE_FRAGMENT
} Refresh_ShaderStageType;

typedef enum Refresh_Filter
{
	REFRESH_FILTER_NEAREST,
	REFRESH_FILTER_LINEAR,
	REFRESH_FILTER_CUBIC
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
    REFRESH_BORDERCOLOR_FLOAT_TRANSPARENT_BLACK = 0,
    REFRESH_BORDERCOLOR_INT_TRANSPARENT_BLACK = 1,
    REFRESH_BORDERCOLOR_FLOAT_OPAQUE_BLACK = 2,
    REFRESH_BORDERCOLOR_INT_OPAQUE_BLACK = 3,
    REFRESH_BORDERCOLOR_FLOAT_OPAQUE_WHITE = 4,
    REFRESH_BORDERCOLOR_INT_OPAQUE_WHITE = 5
} Refresh_BorderColor;

/* Structures */

typedef struct Refresh_Color
{
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
} Refresh_Color;

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
	Refresh_Rect rectangle;
	uint32_t depth; /* 0 unless 3D */
	uint32_t layer; /* 0 unless cube */
	uint32_t level;
} Refresh_TextureSlice;

typedef struct Refresh_PresentationParameters
{
	void* deviceWindowHandle;
	Refresh_PresentMode presentMode;
} Refresh_PresentationParameters;

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

typedef struct Refresh_ColorTargetBlendState
{
	uint8_t blendEnable;
	Refresh_BlendFactor srcColorBlendFactor;
	Refresh_BlendFactor dstColorBlendFactor;
	Refresh_BlendOp colorBlendOp;
	Refresh_BlendFactor srcAlphaBlendFactor;
	Refresh_BlendFactor dstAlphaBlendFactor;
	Refresh_BlendOp alphaBlendOp;
	Refresh_ColorComponentFlags colorWriteMask;
} Refresh_ColorTargetBlendState;

typedef struct Refresh_ComputePipelineLayoutCreateInfo
{
	uint32_t bufferBindingCount;
	uint32_t imageBindingCount;
} Refresh_ComputePipelineLayoutCreateInfo;

typedef struct Refresh_GraphicsPipelineLayoutCreateInfo
{
	uint32_t vertexSamplerBindingCount;
	uint32_t fragmentSamplerBindingCount;
} Refresh_GraphicsPipelineLayoutCreateInfo;

typedef struct Refresh_ColorTargetDescription
{
	Refresh_ColorFormat format;
	Refresh_SampleCount multisampleCount;
	Refresh_LoadOp loadOp;
	Refresh_StoreOp storeOp;
} Refresh_ColorTargetDescription;

typedef struct Refresh_DepthStencilTargetDescription
{
	Refresh_DepthFormat depthFormat;
	Refresh_LoadOp loadOp;
	Refresh_StoreOp storeOp;
	Refresh_LoadOp stencilLoadOp;
	Refresh_StoreOp stencilStoreOp;
} Refresh_DepthStencilTargetDescription;

typedef struct Refresh_RenderPassCreateInfo
{
	const Refresh_ColorTargetDescription *colorTargetDescriptions;
	uint32_t colorTargetCount;
	const Refresh_DepthStencilTargetDescription *depthTargetDescription; /* can be NULL */
} Refresh_RenderPassCreateInfo;

typedef struct Refresh_ShaderModuleCreateInfo
{
	size_t codeSize;
	const uint32_t *byteCode;
} Refresh_ShaderModuleCreateInfo;

/* Pipeline state structures */

typedef struct Refresh_ShaderStageState
{
	Refresh_ShaderModule *shaderModule;
	const char* entryPointName;
	uint64_t uniformBufferSize;
} Refresh_ShaderStageState;

/* FIXME: this is awkwardly named */
typedef struct Refresh_TopologyState
{
	Refresh_PrimitiveType topology;
} Refresh_TopologyState;

typedef struct Refresh_ViewportState
{
	const Refresh_Viewport *viewports;
	uint32_t viewportCount;
	const Refresh_Rect *scissors;
	uint32_t scissorCount;
} Refresh_ViewportState;

typedef struct Refresh_RasterizerState
{
	uint8_t depthClampEnable;
	Refresh_FillMode fillMode;
	Refresh_CullMode cullMode;
	Refresh_FrontFace frontFace;
	uint8_t depthBiasEnable;
	float depthBiasConstantFactor;
	float depthBiasClamp;
	float depthBiasSlopeFactor;
	float lineWidth;
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
	Refresh_StencilOpState frontStencilState;
	Refresh_StencilOpState backStencilState;
	float minDepthBounds;
	float maxDepthBounds;
} Refresh_DepthStencilState;

typedef struct Refresh_ColorBlendState
{
	uint8_t logicOpEnable;
	Refresh_LogicOp logicOp;
	const Refresh_ColorTargetBlendState *blendStates;
	uint32_t blendStateCount;
	float blendConstants[4];
} Refresh_ColorBlendState;

typedef struct Refresh_ComputePipelineCreateInfo
{
	Refresh_ShaderStageState computeShaderState;
	Refresh_ComputePipelineLayoutCreateInfo pipelineLayoutCreateInfo;
} Refresh_ComputePipelineCreateInfo;

typedef struct Refresh_GraphicsPipelineCreateInfo
{
	Refresh_ShaderStageState vertexShaderState;
	Refresh_ShaderStageState fragmentShaderState;
	Refresh_VertexInputState vertexInputState;
	Refresh_TopologyState topologyState;
	Refresh_ViewportState viewportState;
	Refresh_RasterizerState rasterizerState;
	Refresh_MultisampleState multisampleState;
	Refresh_DepthStencilState depthStencilState;
	Refresh_ColorBlendState colorBlendState;
	Refresh_GraphicsPipelineLayoutCreateInfo pipelineLayoutCreateInfo;
	Refresh_RenderPass *renderPass;
} Refresh_GraphicsPipelineCreateInfo;

typedef struct Refresh_FramebufferCreateInfo
{
	Refresh_RenderPass *renderPass;
	Refresh_ColorTarget **pColorTargets;
	uint32_t colorTargetCount;
	Refresh_DepthStencilTarget *pDepthStencilTarget;
	uint32_t width;
	uint32_t height;
} Refresh_FramebufferCreateInfo;

/* Interop Structs */

typedef enum Refresh_SysRendererType
{
	REFRESH_RENDERER_TYPE_VULKAN
} Refresh_SysRendererType;

typedef struct Refresh_SysRenderer
{
	Refresh_SysRendererType rendererType;

	union
	{
#if REFRESH_DRIVER_VULKAN
		struct
		{
			VkInstance instance;
			VkPhysicalDevice physicalDevice;
			VkDevice logicalDevice;
			uint32_t queueFamilyIndex;
		} vulkan;
#endif /* REFRESH_DRIVER_VULKAN */
		uint8_t filler[64];
	} renderer;
} Refresh_SysRenderer;

typedef struct Refresh_TextureHandles
{
	Refresh_SysRendererType rendererType;

	union
	{
#if REFRESH_DRIVER_VULKAN

#if defined(__LP64__) || defined(_WIN64) || defined(__x86_64__) || defined(_M_X64) || defined(__ia64) || defined (_M_IA64) || defined(__aarch64__) || defined(__powerpc64__)
#define REFRESH_VULKAN_HANDLE_TYPE void*
#else
#define REFRESH_VULKAN_HANDLE_TYPE uint64_t
#endif

		struct
		{
			REFRESH_VULKAN_HANDLE_TYPE image;	/* VkImage */
			REFRESH_VULKAN_HANDLE_TYPE view;	/* VkImageView */
		} vulkan;
#endif /* REFRESH_DRIVER_VULKAN */
		uint8_t filler[64];
	} texture;
} Refresh_TextureHandles;

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

REFRESHAPI uint32_t Refresh_LinkedVersion(void);

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

/* Device */

/* Create a rendering context for use on the calling thread.
 *
 * presentationParameters:
 * 		If the windowHandle is NULL, Refresh will run in headless mode.
 * debugMode: Enable debug mode properties.
 */
REFRESHAPI Refresh_Device* Refresh_CreateDevice(
	Refresh_PresentationParameters *presentationParameters,
	uint8_t debugMode
);

/* Create a rendering context by taking an externally-initialized VkDevice.
 * Only valid with Vulkan backend.
 * Useful for piggybacking on a separate graphics library like FNA3D.
 *
 * sysRenderer: Externally-initialized device info.
 * debugMode: Enable debug mode properties.
 */
REFRESHAPI Refresh_Device* Refresh_CreateDeviceUsingExternal(
	Refresh_SysRenderer *sysRenderer,
	uint8_t debugMode
);

/* Destroys a rendering context previously returned by Refresh_CreateDevice. */
REFRESHAPI void Refresh_DestroyDevice(Refresh_Device *device);

/* Drawing */

/* Clears the targets of the currently bound framebuffer.
 * If fewer colors are passed than the number of color targets in the
 * framebuffer, this function will clear the first n color targets.
 *
 * NOTE:
 * 		It is generally recommended to clear in BeginRenderPass
 * 		rather than by calling this function unless necessary.
 *
 * clearRect:	Area to clear.
 * options:		Bitflags to specify color/depth/stencil buffers for clearing.
 * colors:		An array of color values for the cleared color buffers.
 * colorCount:	The number of colors in the above array.
 * depth:		The new value of the cleared depth buffer.
 * stencil:		The new value of the cleared stencil buffer.
 */
REFRESHAPI void Refresh_Clear(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Rect *clearRect,
	Refresh_ClearOptions options,
	Refresh_Color *colors,
	uint32_t colorCount,
	float depth,
	int32_t stencil
);

/* Draws data from vertex/index buffers with instancing enabled.
 *
 * baseVertex:			The starting offset to read from the vertex buffer.
 * startIndex:			The starting offset to read from the index buffer.
 * primitiveCount:		The number of primitives to draw.
 * instanceCount:		The number of instances that will be drawn.
 * vertexParamOffset:	The offset of the vertex shader param data.
 * fragmentParamOffset:	The offset of the fragment shader param data.
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
 * baseVertex:			The starting offset to read from the vertex buffer.
 * startIndex:			The starting offset to read from the index buffer.
 * primitiveCount:		The number of primitives to draw.
 * vertexParamOffset:	The offset of the vertex shader param data.
 * fragmentParamOffset:	The offset of the fragment shader param data.
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
 * vertexStart:				The starting offset to read from the vertex buffer.
 * primitiveCount:			The number of primitives to draw.
 * vertexParamOffset:		The offset of the vertex shader param data.
 * fragmentParamOffset:		The offset of the fragment shader param data.
 */
REFRESHAPI void Refresh_DrawPrimitives(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	uint32_t vertexStart,
	uint32_t primitiveCount,
	uint32_t vertexParamOffset,
	uint32_t fragmentParamOffset
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

/* State Creation */

/* Returns an allocated RenderPass* object. */
REFRESHAPI Refresh_RenderPass* Refresh_CreateRenderPass(
	Refresh_Device *device,
	Refresh_RenderPassCreateInfo *renderPassCreateInfo
);

/* Returns an allocated ComputePipeline* object. */
REFRESHAPI Refresh_ComputePipeline* Refresh_CreateComputePipeline(
	Refresh_Device *device,
	Refresh_ComputePipelineCreateInfo *pipelineCreateInfo
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

/* Returns an allocated Framebuffer* object. */
REFRESHAPI Refresh_Framebuffer* Refresh_CreateFramebuffer(
	Refresh_Device *device,
	Refresh_FramebufferCreateInfo *framebufferCreateInfo
);

/* Returns an allocated ShaderModule* object. */
REFRESHAPI Refresh_ShaderModule* Refresh_CreateShaderModule(
	Refresh_Device *device,
	Refresh_ShaderModuleCreateInfo *shaderModuleCreateInfo
);

/* Creates a 2D texture.
 *
 * format:		The pixel format of the texture data.
 * width:		The width of the texture image.
 * height: 		The height of the texture image.
 * levelCount:	The number of mipmap levels to allocate.
 * usageFlags:	Specifies how the texture will be used.
 *
 * Returns an allocated Refresh_Texture* object. Note that the contents of
 * the texture are undefined until SetData is called.
 */
REFRESHAPI Refresh_Texture* Refresh_CreateTexture2D(
	Refresh_Device *device,
	Refresh_ColorFormat format,
	uint32_t width,
	uint32_t height,
	uint32_t levelCount,
	Refresh_TextureUsageFlags usageFlags
);

/* Creates a 3D texture.
 *
 * format:		The pixel format of the texture data.
 * width:		The width of the texture image.
 * height: 		The height of the texture image.
 * depth: 		The depth of the texture image.
 * levelCount: 	The number of mipmap levels to allocate.
 * usageFlags:	Specifies how the texture will be used.
 *
 * Returns an allocated Refresh_Texture* object. Note that the contents of
 * the texture are undefined until SetData is called.
 */
REFRESHAPI Refresh_Texture* Refresh_CreateTexture3D(
	Refresh_Device *device,
	Refresh_ColorFormat format,
	uint32_t width,
	uint32_t height,
	uint32_t depth,
	uint32_t levelCount,
	Refresh_TextureUsageFlags usageFlags
);

/* Creates a texture cube.
 *
 * format:		The pixel format of the texture data.
 * size: 		The length of the cube side.
 * levelCount: 	The number of mipmap levels to allocate.
 * usageFlags:	Specifies how the texture will be used.
 *
 * Returns an allocated Refresh_Texture* object. Note that the contents of
 * the texture are undefined until SetData is called.
 */
REFRESHAPI Refresh_Texture* Refresh_CreateTextureCube(
	Refresh_Device *device,
	Refresh_ColorFormat format,
	uint32_t size,
	uint32_t levelCount,
	Refresh_TextureUsageFlags usageFlags
);

/* Creates a color target.
 *
 * multisampleCount:	The MSAA value for the color target.
 * textureSlice: 		The texture slice that the color target will resolve to.
 */
REFRESHAPI Refresh_ColorTarget* Refresh_CreateColorTarget(
	Refresh_Device *device,
	Refresh_SampleCount multisampleCount,
	Refresh_TextureSlice *textureSlice
);

/* Creates a depth/stencil target.
 *
 * width:	The width of the depth/stencil target.
 * height: 	The height of the depth/stencil target.
 * format:	The storage format of the depth/stencil target.
 */
REFRESHAPI Refresh_DepthStencilTarget* Refresh_CreateDepthStencilTarget(
	Refresh_Device *device,
	uint32_t width,
	uint32_t height,
	Refresh_DepthFormat format
);

/* Creates a buffer.
 *
 * usageFlags:	Specifies how the buffer will be used.
 * sizeInBytes:	The length of the buffer.
 */
REFRESHAPI Refresh_Buffer* Refresh_CreateBuffer(
	Refresh_Device *device,
	Refresh_BufferUsageFlags usageFlags,
	uint32_t sizeInBytes
);

/* Setters */

/* Uploads image data to a texture object.
 *
 * 	textureSlice:		The texture slice to be updated.
 * 	data:				A pointer to the image data.
 * 	dataLengthInBytes:	The size of the image data.
 */
REFRESHAPI void Refresh_SetTextureData(
	Refresh_Device *driverData,
	Refresh_TextureSlice *textureSlice,
	void *data,
	uint32_t dataLengthInBytes
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
REFRESHAPI void Refresh_SetTextureDataYUV(
	Refresh_Device *driverData,
	Refresh_Texture *y,
	Refresh_Texture *u,
	Refresh_Texture *v,
	uint32_t yWidth,
	uint32_t yHeight,
	uint32_t uvWidth,
	uint32_t uvHeight,
	void* data,
	uint32_t dataLength
);

/* Performs an asynchronous texture-to-texture copy.
 *
 * sourceTextureSlice:		The texture slice from which to copy.
 * destinationTextureSlice:	The texture slice to copy to.
 * filter:					The filter that will be used if the copy requires scaling.
 */
REFRESHAPI void Refresh_CopyTextureToTexture(
	Refresh_Device *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_TextureSlice *sourceTextureSlice,
	Refresh_TextureSlice *destinationTextureSlice,
	Refresh_Filter filter
);

/* Asynchronously copies image data from a texture slice into a buffer.
 *
 * NOTE:
 * 	The buffer will not contain correct data until the command buffer
 * 	is submitted and completed.
 *
 * textureSlice:	The texture object being copied.
 * buffer:			The buffer being filled with the image data.
 */
REFRESHAPI void Refresh_CopyTextureToBuffer(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_TextureSlice *textureSlice,
	Refresh_Buffer *buffer
);

/* Sets a region of the buffer with client data.
 *
 * NOTE:
 * 		Calling this function on a buffer after the buffer
 * 		has been bound without calling Submit first is an error.
 *
 * buffer:			The vertex buffer to be updated.
 * offsetInBytes:	The starting offset of the buffer to write into.
 * data:			The client data to write into the buffer.
 * dataLength:		The length of data from the client buffer to write.
 */
REFRESHAPI void Refresh_SetBufferData(
	Refresh_Device *device,
	Refresh_Buffer *buffer,
	uint32_t offsetInBytes,
	void* data,
	uint32_t dataLength
);

/* Pushes vertex shader params to the device.
 * Returns a starting offset value to be used with draw calls.
 *
 * NOTE:
 * 		A pipeline must be bound.
 * 		Will use the block size of the currently bound vertex shader.
 *
 * data: 				The client data to write into the buffer.
 * paramBlockCount: 	The number of param-sized blocks from the client buffer to write.
 */
REFRESHAPI uint32_t Refresh_PushVertexShaderParams(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	void *data,
	uint32_t paramBlockCount
);

/* Pushes fragment shader params to the device.
 * Returns a starting offset value to be used with draw calls.
 *
 * NOTE:
 * 		A graphics pipeline must be bound.
 * 		Will use the block size of the currently bound fragment shader.
 *
 * data: 				The client data to write into the buffer.
 * paramBlockCount: 	The number of param-sized blocks from the client buffer to write.
 */
REFRESHAPI uint32_t Refresh_PushFragmentShaderParams(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	void *data,
	uint32_t paramBlockCount
);

/* Pushes compute shader params to the device.
 * Returns a starting offset value to be used with draw calls.
 *
 * NOTE:
 * 	A compute pipeline must be bound.
 * 	Will use the block size of the currently bound compute shader.
 *
 * data:			The client data to write into the buffer.
 * paramBlockData:	The number of param-sized blocks from the client buffer to write.
 */
REFRESHAPI uint32_t Refresh_PushComputeShaderParams(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	void *data,
	uint32_t paramBlockCount
);

/* Getters */

/* Synchronously copies data from a buffer to a pointer.
 * You probably want to wait for a sync point to call this.
 *
 * buffer: 				The buffer to copy data from.
 * data:				The pointer to copy data to.
 * dataLengthInBytes:	The length of data to copy.
 */
REFRESHAPI void Refresh_GetBufferData(
	Refresh_Device *device,
	Refresh_Buffer *buffer,
	void *data,
	uint32_t dataLengthInBytes
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
 * "QueueDestroy" because it may not be immediately destroyed by the renderer if
 * this is not called from the main thread (for example, if a garbage collector
 * deletes the resource instead of the programmer).
 *
 * buffer: The Refresh_Buffer to be destroyed.
 */
REFRESHAPI void Refresh_QueueDestroyBuffer(
	Refresh_Device *device,
	Refresh_Buffer *buffer
);

/* Sends a color target to be destroyed by the renderer. Note that we call it
 * "QueueDestroy" because it may not be immediately destroyed by the renderer if
 * this is not called from the main thread (for example, if a garbage collector
 * deletes the resource instead of the programmer).
 *
 * colorTarget: The Refresh_ColorTarget to be destroyed.
 */
REFRESHAPI void Refresh_QueueDestroyColorTarget(
	Refresh_Device *device,
	Refresh_ColorTarget *colorTarget
);

/* Sends a depth/stencil target to be destroyed by the renderer. Note that we call it
 * "QueueDestroy" because it may not be immediately destroyed by the renderer if
 * this is not called from the main thread (for example, if a garbage collector
 * deletes the resource instead of the programmer).
 *
 * depthStencilTarget: The Refresh_DepthStencilTarget to be destroyed.
 */
REFRESHAPI void Refresh_QueueDestroyDepthStencilTarget(
	Refresh_Device *device,
	Refresh_DepthStencilTarget *depthStencilTarget
);

/* Sends a framebuffer to be destroyed by the renderer. Note that we call it
 * "QueueDestroy" because it may not be immediately destroyed by the renderer if
 * this is not called from the main thread (for example, if a garbage collector
 * deletes the resource instead of the programmer).
 *
 * framebuffer: The Refresh_Framebuffer to be destroyed.
 */
REFRESHAPI void Refresh_QueueDestroyFramebuffer(
	Refresh_Device *device,
	Refresh_Framebuffer *frameBuffer
);

/* Sends a shader module to be destroyed by the renderer. Note that we call it
 * "QueueDestroy" because it may not be immediately destroyed by the renderer if
 * this is not called from the main thread (for example, if a garbage collector
 * deletes the resource instead of the programmer).
 *
 * shaderModule: The Refresh_ShaderModule to be destroyed.
 */
REFRESHAPI void Refresh_QueueDestroyShaderModule(
	Refresh_Device *device,
	Refresh_ShaderModule *shaderModule
);

/* Sends a render pass to be destroyed by the renderer. Note that we call it
 * "QueueDestroy" because it may not be immediately destroyed by the renderer if
 * this is not called from the main thread (for example, if a garbage collector
 * deletes the resource instead of the programmer).
 *
 * renderPass: The Refresh_RenderPass to be destroyed.
 */
REFRESHAPI void Refresh_QueueDestroyRenderPass(
	Refresh_Device *device,
	Refresh_RenderPass *renderPass
);

/* Sends a compute pipeline to be destroyed by the renderer. Note that we call it
 * "QueueDestroy" because it may not be immediately destroyed by the renderer if
 * this is not called from the main thread (for example, if a garbage collector
 * deletes the resource instead of the programmer).
 *
 * computePipeline: The Refresh_ComputePipeline to be destroyed.
 */
REFRESHAPI void Refresh_QueueDestroyComputePipeline(
	Refresh_Device *device,
	Refresh_ComputePipeline *computePipeline
);

/* Sends a graphics pipeline to be destroyed by the renderer. Note that we call it
 * "QueueDestroy" because it may not be immediately destroyed by the renderer if
 * this is not called from the main thread (for example, if a garbage collector
 * deletes the resource instead of the programmer).
 *
 * graphicsPipeline: The Refresh_GraphicsPipeline to be destroyed.
 */
REFRESHAPI void Refresh_QueueDestroyGraphicsPipeline(
	Refresh_Device *device,
	Refresh_GraphicsPipeline *graphicsPipeline
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
 * 		A pointer to an array of Refresh_Color structures
 * 		that contains clear values for each color target in the
 * 		framebuffer. May be NULL.
 * clearCount: The amount of color structs in the above array.
 * depthStencilClearValue: The depth/stencil clear value. May be NULL.
 */
REFRESHAPI void Refresh_BeginRenderPass(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_RenderPass *renderPass,
	Refresh_Framebuffer *framebuffer,
	Refresh_Rect renderArea,
	Refresh_Color *pColorClearValues,
	uint32_t colorClearCount,
	Refresh_DepthStencilValue *depthStencilClearValue
);

/* Ends the current render pass. */
REFRESHAPI void Refresh_EndRenderPass(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer
);

/* Binds a graphics pipeline to the graphics bind point. */
REFRESHAPI void Refresh_BindGraphicsPipeline(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_GraphicsPipeline *graphicsPipeline
);

/* Binds vertex buffers for use with subsequent draw calls. */
REFRESHAPI void Refresh_BindVertexBuffers(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	uint32_t firstBinding,
	uint32_t bindingCount,
	Refresh_Buffer **pBuffers,
	uint64_t *pOffsets
);

/* Binds an index buffer for use with subsequent draw calls. */
REFRESHAPI void Refresh_BindIndexBuffer(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Buffer *buffer,
	uint64_t offset,
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
	Refresh_Buffer **pBuffers
);

/* Binds textures for use with the currently bound compute pipeline.
 *
 * pTextures: An array of textures to bind.
 * 	Length must be equal to the number of buffers
 * 	specified by the compute pipeline.
 */
REFRESHAPI void Refresh_BindComputeTextures(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Texture **pTextures
);

/* Submission/Presentation */

/* Returns an allocated Refresh_CommandBuffer* object.
 * This command buffer is managed by the implementation and
 * should NOT be freed by the user.
 *
 * NOTE:
 * 	A command buffer may only be used on the thread that
 * 	it was acquired on. Using it on any other thread is an error.
 *
 * fixed:
 * 	If a command buffer is designated as fixed, it can be
 * 	acquired once, have commands recorded into it, and
 * 	be re-submitted indefinitely.
 *
 */
REFRESHAPI Refresh_CommandBuffer* Refresh_AcquireCommandBuffer(
	Refresh_Device *device,
	uint8_t fixed
);

/* Queues an image to be presented to the screen.
 * The image will be presented upon the next Refresh_Submit call.
 *
 * NOTE:
 *		It is an error to call this function in headless mode.
 *
 * textureSlice:			The texture slice to present.
 * destinationRectangle:	The region of the window to update. Can be NULL.
 * filter:					The filter to use if scaling is required.
 */
REFRESHAPI void Refresh_QueuePresent(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_TextureSlice *textureSlice,
	Refresh_Rect *destinationRectangle,
	Refresh_Filter filter
);

/* Submits all of the enqueued commands. */
REFRESHAPI void Refresh_Submit(
	Refresh_Device* device,
	uint32_t commandBufferCount,
	Refresh_CommandBuffer **pCommandBuffers
);

/* Waits for the previous submission to complete. */
REFRESHAPI void Refresh_Wait(
	Refresh_Device *device
);

/* Export handles to be consumed by another API */
REFRESHAPI void Refresh_GetTextureHandles(
	Refresh_Device* device,
	Refresh_Texture* texture,
	Refresh_TextureHandles* handles
);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* REFRESH_H */

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
