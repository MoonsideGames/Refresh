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

#if REFRESH_DRIVER_VULKAN

#define VK_NO_PROTOTYPES
#include "vulkan/vulkan.h"

#include "Refresh_Driver.h"

#include <SDL.h>
#include <SDL_syswm.h>
#include <SDL_vulkan.h>

/* Global Vulkan Loader Entry Points */

static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = NULL;

#define VULKAN_GLOBAL_FUNCTION(name) \
	static PFN_##name name = NULL;
#include "Refresh_Driver_Vulkan_vkfuncs.h"

/* vkInstance/vkDevice function typedefs */

#define VULKAN_INSTANCE_FUNCTION(ext, ret, func, params) \
	typedef ret (VKAPI_CALL *vkfntype_##func) params;
#define VULKAN_DEVICE_FUNCTION(ext, ret, func, params) \
	typedef ret (VKAPI_CALL *vkfntype_##func) params;
#include "Refresh_Driver_Vulkan_vkfuncs.h"

/* Required extensions */
static const char* deviceExtensionNames[] =
{
	/* Globally supported */
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
	/* Core since 1.1 */
	VK_KHR_MAINTENANCE1_EXTENSION_NAME,
	VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
	VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
	/* Core since 1.2 */
	VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME,
	/* EXT, probably not going to be Core */
	VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME,
};
static uint32_t deviceExtensionCount = SDL_arraysize(deviceExtensionNames);

/* Defines */

#define STARTING_ALLOCATION_SIZE 64000000 		/* 64MB */
#define MAX_ALLOCATION_SIZE 256000000 			/* 256MB */
#define TEXTURE_STAGING_SIZE 8000000 			/* 8MB */
#define MAX_TEXTURE_STAGING_SIZE 128000000		/* 128MB */
#define UBO_BUFFER_SIZE 8000000 				/* 8MB */
#define UBO_ACTUAL_SIZE (UBO_BUFFER_SIZE * 2)
#define DESCRIPTOR_POOL_STARTING_SIZE 128
#define UBO_POOL_SIZE 1000
#define SUB_BUFFER_COUNT 2
#define DESCRIPTOR_SET_DEACTIVATE_FRAMES 10

#define IDENTITY_SWIZZLE \
{ \
	VK_COMPONENT_SWIZZLE_IDENTITY, \
	VK_COMPONENT_SWIZZLE_IDENTITY, \
	VK_COMPONENT_SWIZZLE_IDENTITY, \
	VK_COMPONENT_SWIZZLE_IDENTITY \
}

#define NULL_DESC_LAYOUT (VkDescriptorSetLayout) 0
#define NULL_PIPELINE_LAYOUT (VkPipelineLayout) 0
#define NULL_RENDER_PASS (Refresh_RenderPass*) 0

#define EXPAND_ELEMENTS_IF_NEEDED(arr, initialValue, type)	\
	if (arr->count == arr->capacity)		\
	{						\
		if (arr->capacity == 0)			\
		{					\
			arr->capacity = initialValue;	\
		}					\
		else					\
		{					\
			arr->capacity *= 2;		\
		}					\
		arr->elements = (type*) SDL_realloc(	\
			arr->elements,			\
			arr->capacity * sizeof(type)	\
		);					\
	}

#define EXPAND_ARRAY_IF_NEEDED(arr, elementType, newCount, capacity, newCapacity)	\
	if (newCount >= capacity)														\
	{																				\
		capacity = newCapacity;														\
		arr = (elementType*) SDL_realloc(													\
			arr,																	\
			sizeof(elementType) * capacity													\
		);																			\
	}

#define MOVE_ARRAY_CONTENTS_AND_RESET(i, dstArr, dstCount, srcArr, srcCount)	\
	for (i = 0; i < srcCount; i += 1)											\
	{																			\
		dstArr[i] = srcArr[i];													\
	}																			\
	dstCount = srcCount;														\
	srcCount = 0;

/* Enums */

typedef enum VulkanResourceAccessType
{
	/* Reads */
	RESOURCE_ACCESS_NONE, /* For initialization */
	RESOURCE_ACCESS_INDEX_BUFFER,
	RESOURCE_ACCESS_VERTEX_BUFFER,
	RESOURCE_ACCESS_VERTEX_SHADER_READ_UNIFORM_BUFFER,
	RESOURCE_ACCESS_VERTEX_SHADER_READ_SAMPLED_IMAGE,
	RESOURCE_ACCESS_FRAGMENT_SHADER_READ_UNIFORM_BUFFER,
	RESOURCE_ACCESS_FRAGMENT_SHADER_READ_SAMPLED_IMAGE,
	RESOURCE_ACCESS_FRAGMENT_SHADER_READ_COLOR_ATTACHMENT,
	RESOURCE_ACCESS_FRAGMENT_SHADER_READ_DEPTH_STENCIL_ATTACHMENT,
	RESOURCE_ACCESS_COMPUTE_SHADER_READ_UNIFORM_BUFFER,
	RESOURCE_ACCESS_COMPUTE_SHADER_READ_OTHER,
	RESOURCE_ACCESS_ANY_SHADER_READ_SAMPLED_IMAGE,
	RESOURCE_ACCESS_COLOR_ATTACHMENT_READ,
	RESOURCE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ,
	RESOURCE_ACCESS_TRANSFER_READ,
	RESOURCE_ACCESS_HOST_READ,
	RESOURCE_ACCESS_PRESENT,
	RESOURCE_ACCESS_END_OF_READ,

	/* Writes */
	RESOURCE_ACCESS_VERTEX_SHADER_WRITE,
	RESOURCE_ACCESS_FRAGMENT_SHADER_WRITE,
	RESOURCE_ACCESS_COLOR_ATTACHMENT_WRITE,
	RESOURCE_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE,
	RESOURCE_ACCESS_TRANSFER_WRITE,
	RESOURCE_ACCESS_HOST_WRITE,

	/* Read-Writes */
	RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE,
	RESOURCE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_WRITE,
	RESOURCE_ACCESS_MEMORY_TRANSFER_READ_WRITE,
	RESOURCE_ACCESS_GENERAL,

	/* Count */
	RESOURCE_ACCESS_TYPES_COUNT
} VulkanResourceAccessType;

typedef enum CreateSwapchainResult
{
	CREATE_SWAPCHAIN_FAIL,
	CREATE_SWAPCHAIN_SUCCESS,
	CREATE_SWAPCHAIN_SURFACE_ZERO,
} CreateSwapchainResult;

/* Conversions */

static VkFormat RefreshToVK_SurfaceFormat[] =
{
	VK_FORMAT_R8G8B8A8_UNORM,		    /* R8G8B8A8 */
	VK_FORMAT_R5G6B5_UNORM_PACK16,		/* R5G6B5 */
	VK_FORMAT_A1R5G5B5_UNORM_PACK16,	/* A1R5G5B5 */
	VK_FORMAT_B4G4R4A4_UNORM_PACK16,	/* B4G4R4A4 */
	VK_FORMAT_BC1_RGBA_UNORM_BLOCK,		/* BC1 */
	VK_FORMAT_BC2_UNORM_BLOCK,		    /* BC3 */
	VK_FORMAT_BC3_UNORM_BLOCK,		    /* BC5 */
	VK_FORMAT_R8G8_SNORM,			    /* R8G8_SNORM */
	VK_FORMAT_R8G8B8A8_SNORM,		    /* R8G8B8A8_SNORM */
	VK_FORMAT_A2R10G10B10_UNORM_PACK32,	/* A2R10G10B10 */
	VK_FORMAT_R16G16_UNORM,			    /* R16G16 */
	VK_FORMAT_R16G16B16A16_UNORM,		/* R16G16B16A16 */
	VK_FORMAT_R8_UNORM,			        /* R8 */
	VK_FORMAT_R32_SFLOAT,			    /* R32_SFLOAT */
	VK_FORMAT_R32G32_SFLOAT,		    /* R32G32_SFLOAT */
	VK_FORMAT_R32G32B32A32_SFLOAT,		/* R32G32B32A32_SFLOAT */
	VK_FORMAT_R16_SFLOAT,			    /* R16_SFLOAT */
	VK_FORMAT_R16G16_SFLOAT,		    /* R16G16_SFLOAT */
	VK_FORMAT_R16G16B16A16_SFLOAT		/* R16G16B16A16_SFLOAT */
};

static VkFormat RefreshToVK_DepthFormat[] =
{
    VK_FORMAT_D16_UNORM,
    VK_FORMAT_D32_SFLOAT,
    VK_FORMAT_D16_UNORM_S8_UINT,
    VK_FORMAT_D32_SFLOAT_S8_UINT
};

static VkFormat RefreshToVK_VertexFormat[] =
{
	VK_FORMAT_R32_SFLOAT,			/* SINGLE */
	VK_FORMAT_R32G32_SFLOAT,		/* VECTOR2 */
	VK_FORMAT_R32G32B32_SFLOAT,		/* VECTOR3 */
	VK_FORMAT_R32G32B32A32_SFLOAT,	/* VECTOR4 */
	VK_FORMAT_R8G8B8A8_UNORM,		/* COLOR */
	VK_FORMAT_R8G8B8A8_USCALED,		/* BYTE4 */
	VK_FORMAT_R16G16_SSCALED,		/* SHORT2 */
	VK_FORMAT_R16G16B16A16_SSCALED,	/* SHORT4 */
	VK_FORMAT_R16G16_SNORM,			/* NORMALIZEDSHORT2 */
	VK_FORMAT_R16G16B16A16_SNORM,	/* NORMALIZEDSHORT4 */
	VK_FORMAT_R16G16_SFLOAT,		/* HALFVECTOR2 */
	VK_FORMAT_R16G16B16A16_SFLOAT	/* HALFVECTOR4 */
};

static VkIndexType RefreshToVK_IndexType[] =
{
	VK_INDEX_TYPE_UINT16,
	VK_INDEX_TYPE_UINT32
};

static VkPrimitiveTopology RefreshToVK_PrimitiveType[] =
{
	VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
	VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
	VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
};

static VkPolygonMode RefreshToVK_PolygonMode[] =
{
	VK_POLYGON_MODE_FILL,
	VK_POLYGON_MODE_LINE,
	VK_POLYGON_MODE_POINT
};

static VkCullModeFlags RefreshToVK_CullMode[] =
{
	VK_CULL_MODE_NONE,
	VK_CULL_MODE_FRONT_BIT,
	VK_CULL_MODE_BACK_BIT,
	VK_CULL_MODE_FRONT_AND_BACK
};

static VkFrontFace RefreshToVK_FrontFace[] =
{
	VK_FRONT_FACE_COUNTER_CLOCKWISE,
	VK_FRONT_FACE_CLOCKWISE
};

static VkBlendFactor RefreshToVK_BlendFactor[] =
{
	VK_BLEND_FACTOR_ZERO,
	VK_BLEND_FACTOR_ONE,
	VK_BLEND_FACTOR_SRC_COLOR,
	VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
	VK_BLEND_FACTOR_DST_COLOR,
	VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
	VK_BLEND_FACTOR_SRC_ALPHA,
	VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
	VK_BLEND_FACTOR_DST_ALPHA,
	VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
	VK_BLEND_FACTOR_CONSTANT_COLOR,
	VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
	VK_BLEND_FACTOR_CONSTANT_ALPHA,
	VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA,
	VK_BLEND_FACTOR_SRC_ALPHA_SATURATE,
	VK_BLEND_FACTOR_SRC1_COLOR,
	VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR,
	VK_BLEND_FACTOR_SRC1_ALPHA,
	VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA
};

static VkBlendOp RefreshToVK_BlendOp[] =
{
	VK_BLEND_OP_ADD,
	VK_BLEND_OP_SUBTRACT,
	VK_BLEND_OP_REVERSE_SUBTRACT,
	VK_BLEND_OP_MIN,
	VK_BLEND_OP_MAX
};

static VkLogicOp RefreshToVK_LogicOp[] =
{
	VK_LOGIC_OP_CLEAR,
	VK_LOGIC_OP_AND,
	VK_LOGIC_OP_AND_REVERSE,
	VK_LOGIC_OP_COPY,
	VK_LOGIC_OP_AND_INVERTED,
	VK_LOGIC_OP_NO_OP,
	VK_LOGIC_OP_XOR,
	VK_LOGIC_OP_OR,
	VK_LOGIC_OP_NOR,
	VK_LOGIC_OP_EQUIVALENT,
	VK_LOGIC_OP_INVERT,
	VK_LOGIC_OP_OR_REVERSE,
	VK_LOGIC_OP_COPY_INVERTED,
	VK_LOGIC_OP_OR_INVERTED,
	VK_LOGIC_OP_NAND,
	VK_LOGIC_OP_SET
};

static VkCompareOp RefreshToVK_CompareOp[] =
{
	VK_COMPARE_OP_NEVER,
	VK_COMPARE_OP_LESS,
	VK_COMPARE_OP_EQUAL,
	VK_COMPARE_OP_LESS_OR_EQUAL,
	VK_COMPARE_OP_GREATER,
	VK_COMPARE_OP_NOT_EQUAL,
	VK_COMPARE_OP_GREATER_OR_EQUAL,
	VK_COMPARE_OP_ALWAYS
};

static VkStencilOp RefreshToVK_StencilOp[] =
{
	VK_STENCIL_OP_KEEP,
	VK_STENCIL_OP_ZERO,
	VK_STENCIL_OP_REPLACE,
	VK_STENCIL_OP_INCREMENT_AND_CLAMP,
	VK_STENCIL_OP_DECREMENT_AND_CLAMP,
	VK_STENCIL_OP_INVERT,
	VK_STENCIL_OP_INCREMENT_AND_WRAP,
	VK_STENCIL_OP_DECREMENT_AND_WRAP
};

static VkAttachmentLoadOp RefreshToVK_LoadOp[] =
{
    VK_ATTACHMENT_LOAD_OP_LOAD,
    VK_ATTACHMENT_LOAD_OP_CLEAR,
    VK_ATTACHMENT_LOAD_OP_DONT_CARE
};

static VkAttachmentStoreOp RefreshToVK_StoreOp[] =
{
    VK_ATTACHMENT_STORE_OP_STORE,
    VK_ATTACHMENT_STORE_OP_DONT_CARE
};

static VkSampleCountFlagBits RefreshToVK_SampleCount[] =
{
    VK_SAMPLE_COUNT_1_BIT,
    VK_SAMPLE_COUNT_2_BIT,
    VK_SAMPLE_COUNT_4_BIT,
    VK_SAMPLE_COUNT_8_BIT,
    VK_SAMPLE_COUNT_16_BIT,
    VK_SAMPLE_COUNT_32_BIT,
    VK_SAMPLE_COUNT_64_BIT
};

static VkVertexInputRate RefreshToVK_VertexInputRate[] =
{
	VK_VERTEX_INPUT_RATE_VERTEX,
	VK_VERTEX_INPUT_RATE_INSTANCE
};

static VkFilter RefreshToVK_Filter[] =
{
	VK_FILTER_NEAREST,
	VK_FILTER_LINEAR,
	VK_FILTER_CUBIC_EXT
};

static VkSamplerMipmapMode RefreshToVK_SamplerMipmapMode[] =
{
	VK_SAMPLER_MIPMAP_MODE_NEAREST,
	VK_SAMPLER_MIPMAP_MODE_LINEAR
};

static VkSamplerAddressMode RefreshToVK_SamplerAddressMode[] =
{
	VK_SAMPLER_ADDRESS_MODE_REPEAT,
	VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
	VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
	VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER
};

static VkBorderColor RefreshToVK_BorderColor[] =
{
	VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
	VK_BORDER_COLOR_INT_TRANSPARENT_BLACK,
	VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
	VK_BORDER_COLOR_INT_OPAQUE_BLACK,
	VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
	VK_BORDER_COLOR_INT_OPAQUE_WHITE
};

/* Structures */

/* Memory Allocation */

typedef struct VulkanMemoryAllocation VulkanMemoryAllocation;

typedef struct VulkanMemoryFreeRegion
{
	VulkanMemoryAllocation *allocation;
	VkDeviceSize offset;
	VkDeviceSize size;
	uint32_t allocationIndex;
	uint32_t sortedIndex;
} VulkanMemoryFreeRegion;

typedef struct VulkanMemorySubAllocator
{
	VkDeviceSize nextAllocationSize;
	VulkanMemoryAllocation **allocations;
	uint32_t allocationCount;
	VulkanMemoryFreeRegion **sortedFreeRegions;
	uint32_t sortedFreeRegionCount;
	uint32_t sortedFreeRegionCapacity;
} VulkanMemorySubAllocator;

struct VulkanMemoryAllocation
{
	VulkanMemorySubAllocator *allocator;
	VkDeviceMemory memory;
	VkDeviceSize size;
	VulkanMemoryFreeRegion **freeRegions;
	uint32_t freeRegionCount;
	uint32_t freeRegionCapacity;
	uint8_t dedicated;
	uint8_t *mapPointer;
	SDL_mutex *memoryLock;
};

typedef struct VulkanMemoryAllocator
{
	VulkanMemorySubAllocator subAllocators[VK_MAX_MEMORY_TYPES];
} VulkanMemoryAllocator;

/* Memory Barriers */

typedef struct VulkanResourceAccessInfo
{
	VkPipelineStageFlags stageMask;
	VkAccessFlags accessMask;
	VkImageLayout imageLayout;
} VulkanResourceAccessInfo;

static const VulkanResourceAccessInfo AccessMap[RESOURCE_ACCESS_TYPES_COUNT] =
{
	/* RESOURCE_ACCESS_NONE */
	{
		0,
		0,
		VK_IMAGE_LAYOUT_UNDEFINED
	},

	/* RESOURCE_ACCESS_INDEX_BUFFER */
	{
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
		VK_ACCESS_INDEX_READ_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED
	},

	/* RESOURCE_ACCESS_VERTEX_BUFFER */
	{
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
		VK_ACCESS_INDEX_READ_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED
	},

	/* RESOURCE_ACCESS_VERTEX_SHADER_READ_UNIFORM_BUFFER */
	{
		VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
		VK_ACCESS_SHADER_READ_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED
	},

	/* RESOURCE_ACCESS_VERTEX_SHADER_READ_SAMPLED_IMAGE */
	{
		VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
		VK_ACCESS_SHADER_READ_BIT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	},

	/* RESOURCE_ACCESS_FRAGMENT_SHADER_READ_UNIFORM_BUFFER */
	{
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_UNIFORM_READ_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED
	},

	/* RESOURCE_ACCESS_FRAGMENT_SHADER_READ_SAMPLED_IMAGE */
	{
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_SHADER_READ_BIT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	},

	/* RESOURCE_ACCESS_FRAGMENT_SHADER_READ_COLOR_ATTACHMENT */
	{
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	},

	/* RESOURCE_ACCESS_FRAGMENT_SHADER_READ_DEPTH_STENCIL_ATTACHMENT */
	{
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
	},

	/* RESOURCE_ACCESS_COMPUTE_SHADER_READ_UNIFORM_BUFFER */
	{
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_ACCESS_UNIFORM_READ_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED
	},

	/* RESOURCE_ACCESS_COMPUTE_SHADER_READ_OTHER */
	{
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_ACCESS_SHADER_READ_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED
	},

	/* RESOURCE_ACCESS_ANY_SHADER_READ_SAMPLED_IMAGE */
	{
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_ACCESS_SHADER_READ_BIT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	},

	/* RESOURCE_ACCESS_COLOR_ATTACHMENT_READ */
	{
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	},

	/* RESOURCE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ */
	{
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
	},

	/* RESOURCE_ACCESS_TRANSFER_READ */
	{
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_ACCESS_TRANSFER_READ_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
	},

	/* RESOURCE_ACCESS_HOST_READ */
	{
		VK_PIPELINE_STAGE_HOST_BIT,
		VK_ACCESS_HOST_READ_BIT,
		VK_IMAGE_LAYOUT_GENERAL
	},

	/* RESOURCE_ACCESS_PRESENT */
	{
		0,
		0,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
	},

	/* RESOURCE_ACCESS_END_OF_READ */
	{
		0,
		0,
		VK_IMAGE_LAYOUT_UNDEFINED
	},

	/* RESOURCE_ACCESS_VERTEX_SHADER_WRITE */
	{
		VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
		VK_ACCESS_SHADER_WRITE_BIT,
		VK_IMAGE_LAYOUT_GENERAL
	},

	/* RESOURCE_ACCESS_FRAGMENT_SHADER_WRITE */
	{
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_SHADER_WRITE_BIT,
		VK_IMAGE_LAYOUT_GENERAL
	},

	/* RESOURCE_ACCESS_COLOR_ATTACHMENT_WRITE */
	{
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	},

	/* RESOURCE_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE */
	{
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	},

	/* RESOURCE_ACCESS_TRANSFER_WRITE */
	{
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
	},

	/* RESOURCE_ACCESS_HOST_WRITE */
	{
		VK_PIPELINE_STAGE_HOST_BIT,
		VK_ACCESS_HOST_WRITE_BIT,
		VK_IMAGE_LAYOUT_GENERAL
	},

	/* RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE */
	{
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	},

	/* RESOURCE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_WRITE */
	{
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	},

	/* RESOURCE_ACCESS_MEMORY_TRANSFER_READ_WRITE */
	{
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED
	},

	/* RESOURCE_ACCESS_GENERAL */
	{
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
		VK_IMAGE_LAYOUT_GENERAL
	}
};

/* Memory structures */

typedef struct VulkanBuffer VulkanBuffer;

typedef struct VulkanSubBuffer
{
	VulkanMemoryAllocation *allocation;
	VkBuffer buffer;
	VkDeviceSize offset;
	VkDeviceSize size;
	VulkanResourceAccessType resourceAccessType;
	int8_t bound;
} VulkanSubBuffer;

/*
 * Our VulkanBuffer is actually a series of sub-buffers
 * so we can properly support updates while a frame is in flight
 * without needing a sync point
 */
struct VulkanBuffer /* cast from FNA3D_Buffer */
{
	VkDeviceSize size;
	VulkanSubBuffer **subBuffers;
	uint32_t subBufferCount;
	uint32_t currentSubBufferIndex;
	VulkanResourceAccessType resourceAccessType;
	VkBufferUsageFlags usage;
	uint8_t bound;
	uint8_t boundSubmitted;
};

/* Renderer Structure */

typedef struct QueueFamilyIndices
{
	uint32_t graphicsFamily;
	uint32_t presentFamily;
	uint32_t computeFamily;
	uint32_t transferFamily;
} QueueFamilyIndices;

typedef struct SwapChainSupportDetails
{
	VkSurfaceCapabilitiesKHR capabilities;
	VkSurfaceFormatKHR *formats;
	uint32_t formatsLength;
	VkPresentModeKHR *presentModes;
	uint32_t presentModesLength;
} SwapChainSupportDetails;

typedef struct BufferDescriptorSetCache BufferDescriptorSetCache;
typedef struct ImageDescriptorSetCache ImageDescriptorSetCache;

typedef struct VulkanGraphicsPipelineLayout
{
	VkPipelineLayout pipelineLayout;
	ImageDescriptorSetCache *vertexSamplerDescriptorSetCache;
	ImageDescriptorSetCache *fragmentSamplerDescriptorSetCache;
} VulkanGraphicsPipelineLayout;

typedef struct VulkanGraphicsPipeline
{
	VkPipeline pipeline;
	VulkanGraphicsPipelineLayout *pipelineLayout;
	Refresh_PrimitiveType primitiveType;
	VkDescriptorSet vertexSamplerDescriptorSet; /* updated by BindVertexSamplers */
	VkDescriptorSet fragmentSamplerDescriptorSet; /* updated by BindFragmentSamplers */

	VkDescriptorSet vertexUBODescriptorSet; /* permanently set in Create function */
	VkDescriptorSet fragmentUBODescriptorSet; /* permanently set in Create function */
	VkDeviceSize vertexUBOBlockSize; /* permanently set in Create function */
	VkDeviceSize fragmentUBOBlockSize; /* permantenly set in Create function */
} VulkanGraphicsPipeline;

typedef struct VulkanComputePipelineLayout
{
	VkPipelineLayout pipelineLayout;
	BufferDescriptorSetCache *bufferDescriptorSetCache;
	ImageDescriptorSetCache *imageDescriptorSetCache;
} VulkanComputePipelineLayout;

typedef struct VulkanComputePipeline
{
	VkPipeline pipeline;
	VulkanComputePipelineLayout *pipelineLayout;
	VkDescriptorSet bufferDescriptorSet; /* updated by BindComputeBuffers */
	VkDescriptorSet imageDescriptorSet; /* updated by BindComputeTextures */

	VkDescriptorSet computeUBODescriptorSet; /* permanently set in Create function */
	VkDeviceSize computeUBOBlockSize; /* permanently set in Create function */
} VulkanComputePipeline;

typedef struct VulkanTexture
{
	VulkanMemoryAllocation *allocation;
	VkDeviceSize offset;
	VkDeviceSize memorySize;

	VkImage image;
	VkImageView view;
	VkExtent2D dimensions;

	uint8_t is3D;
	uint8_t isCube;

	uint32_t depth;
	uint32_t layerCount;
	uint32_t levelCount;
	VkFormat format;
	Refresh_ColorFormat refreshFormat;
	VulkanResourceAccessType resourceAccessType;
	uint32_t queueFamilyIndex;
	Refresh_TextureUsageFlags usageFlags;
	REFRESHNAMELESS union
	{
		Refresh_ColorFormat colorFormat;
		Refresh_DepthFormat depthStencilFormat;
	};
} VulkanTexture;

typedef struct VulkanColorTarget
{
	VulkanTexture *texture;
	uint32_t layer;
	VkImageView view;
	VulkanTexture *multisampleTexture;
	VkSampleCountFlags multisampleCount;
} VulkanColorTarget;

typedef struct VulkanDepthStencilTarget
{
	VulkanTexture *texture;
	VkImageView view;
} VulkanDepthStencilTarget;

typedef struct VulkanFramebuffer
{
	VkFramebuffer framebuffer;
	VulkanColorTarget *colorTargets[MAX_COLOR_TARGET_BINDINGS];
	uint32_t colorTargetCount;
	VulkanDepthStencilTarget *depthStencilTarget;
	uint32_t width;
	uint32_t height;
} VulkanFramebuffer;

/* Cache structures */

/* Descriptor Set Layout Caches*/

#define NUM_DESCRIPTOR_SET_LAYOUT_BUCKETS 1031

typedef struct DescriptorSetLayoutHash
{
	VkDescriptorType descriptorType;
	uint32_t bindingCount;
	VkShaderStageFlagBits stageFlag;
} DescriptorSetLayoutHash;

typedef struct DescriptorSetLayoutHashMap
{
	DescriptorSetLayoutHash key;
	VkDescriptorSetLayout value;
} DescriptorSetLayoutHashMap;

typedef struct DescriptorSetLayoutHashArray
{
	DescriptorSetLayoutHashMap *elements;
	int32_t count;
	int32_t capacity;
} DescriptorSetLayoutHashArray;

typedef struct DescriptorSetLayoutHashTable
{
	DescriptorSetLayoutHashArray buckets[NUM_DESCRIPTOR_SET_LAYOUT_BUCKETS];
} DescriptorSetLayoutHashTable;

static inline uint64_t DescriptorSetLayoutHashTable_GetHashCode(DescriptorSetLayoutHash key)
{
	const uint64_t HASH_FACTOR = 97;
	uint64_t result = 1;
	result = result * HASH_FACTOR + key.descriptorType;
	result = result * HASH_FACTOR + key.bindingCount;
	result = result * HASH_FACTOR + key.stageFlag;
	return result;
}

static inline VkDescriptorSetLayout DescriptorSetLayoutHashTable_Fetch(
	DescriptorSetLayoutHashTable *table,
	DescriptorSetLayoutHash key
) {
	int32_t i;
	uint64_t hashcode = DescriptorSetLayoutHashTable_GetHashCode(key);
	DescriptorSetLayoutHashArray *arr = &table->buckets[hashcode % NUM_DESCRIPTOR_SET_LAYOUT_BUCKETS];

	for (i = 0; i < arr->count; i += 1)
	{
		const DescriptorSetLayoutHash *e = &arr->elements[i].key;
		if (    key.descriptorType == e->descriptorType &&
			key.bindingCount == e->bindingCount &&
			key.stageFlag == e->stageFlag   )
		{
			return arr->elements[i].value;
		}
	}

	return VK_NULL_HANDLE;
}

static inline void DescriptorSetLayoutHashTable_Insert(
	DescriptorSetLayoutHashTable *table,
	DescriptorSetLayoutHash key,
	VkDescriptorSetLayout value
) {
	uint64_t hashcode = DescriptorSetLayoutHashTable_GetHashCode(key);
	DescriptorSetLayoutHashArray *arr = &table->buckets[hashcode % NUM_DESCRIPTOR_SET_LAYOUT_BUCKETS];

	DescriptorSetLayoutHashMap map;
	map.key = key;
	map.value = value;

	EXPAND_ELEMENTS_IF_NEEDED(arr, 4, DescriptorSetLayoutHashMap);

	arr->elements[arr->count] = map;
	arr->count += 1;
}

/* Descriptor Set Caches */

#define NUM_DESCRIPTOR_SET_HASH_BUCKETS 1031

typedef struct ImageDescriptorSetData
{
	VkDescriptorImageInfo descriptorImageInfo[MAX_TEXTURE_SAMPLERS]; /* used for vertex samplers as well */
} ImageDescriptorSetData;

typedef struct ImageDescriptorSetHashMap
{
	uint64_t key;
	ImageDescriptorSetData descriptorSetData;
	VkDescriptorSet descriptorSet;
	uint8_t inactiveFrameCount;
} ImageDescriptorSetHashMap;

typedef struct ImageDescriptorSetHashArray
{
	uint32_t *elements;
	int32_t count;
	int32_t capacity;
} ImageDescriptorSetHashArray;

static inline uint64_t ImageDescriptorSetHashTable_GetHashCode(
	ImageDescriptorSetData *descriptorSetData,
	uint32_t samplerCount
) {
	const uint64_t HASH_FACTOR = 97;
	uint32_t i;
	uint64_t result = 1;

	for (i = 0; i < samplerCount; i += 1)
	{
		result = result * HASH_FACTOR + (uint64_t) descriptorSetData->descriptorImageInfo[i].imageView;
		result = result * HASH_FACTOR + (uint64_t) descriptorSetData->descriptorImageInfo[i].sampler;
	}

	return result;
}

struct ImageDescriptorSetCache
{
	VkDescriptorSetLayout descriptorSetLayout;
	uint32_t bindingCount;
	VkDescriptorType descriptorType;

	ImageDescriptorSetHashArray buckets[NUM_DESCRIPTOR_SET_HASH_BUCKETS]; /* these buckets store indices */
	ImageDescriptorSetHashMap *elements; /* where the hash map elements are stored */
	uint32_t count;
	uint32_t capacity;

	VkDescriptorPool *imageDescriptorPools;
	uint32_t imageDescriptorPoolCount;
	uint32_t nextPoolSize;

	VkDescriptorSet *inactiveDescriptorSets;
	uint32_t inactiveDescriptorSetCount;
	uint32_t inactiveDescriptorSetCapacity;
};

typedef struct BufferDescriptorSetData
{
	VkDescriptorBufferInfo descriptorBufferInfo[MAX_BUFFER_BINDINGS];
} BufferDescriptorSetData;

typedef struct BufferDescriptorSetHashMap
{
	uint64_t key;
	BufferDescriptorSetData descriptorSetData;
	VkDescriptorSet descriptorSet;
	uint8_t inactiveFrameCount;
} BufferDescriptorSetHashMap;

typedef struct BufferDescriptorSetHashArray
{
	uint32_t *elements;
	int32_t count;
	int32_t capacity;
} BufferDescriptorSetHashArray;

static inline uint64_t BufferDescriptorSetHashTable_GetHashCode(
	BufferDescriptorSetData *descriptorSetData,
	uint32_t bindingCount
) {
	const uint64_t HASH_FACTOR = 97;
	uint32_t i;
	uint64_t result = 1;

	for (i = 0; i < bindingCount; i += 1)
	{
		result = result * HASH_FACTOR + (uint64_t) descriptorSetData->descriptorBufferInfo[i].buffer;
		result = result * HASH_FACTOR + (uint64_t) descriptorSetData->descriptorBufferInfo[i].offset;
		result = result * HASH_FACTOR + (uint64_t) descriptorSetData->descriptorBufferInfo[i].range;
	}

	return result;
}

struct BufferDescriptorSetCache
{
	VkDescriptorSetLayout descriptorSetLayout;
	uint32_t bindingCount;
	VkDescriptorType descriptorType;

	BufferDescriptorSetHashArray buckets[NUM_DESCRIPTOR_SET_HASH_BUCKETS];
	BufferDescriptorSetHashMap *elements;
	uint32_t count;
	uint32_t capacity;

	VkDescriptorPool *bufferDescriptorPools;
	uint32_t bufferDescriptorPoolCount;
	uint32_t nextPoolSize;

	VkDescriptorSet *inactiveDescriptorSets;
	uint32_t inactiveDescriptorSetCount;
	uint32_t inactiveDescriptorSetCapacity;
};

/* Pipeline Caches */

#define NUM_PIPELINE_LAYOUT_BUCKETS 1031

typedef struct GraphicsPipelineLayoutHash
{
	VkDescriptorSetLayout vertexSamplerLayout;
	VkDescriptorSetLayout fragmentSamplerLayout;
	VkDescriptorSetLayout vertexUniformLayout;
	VkDescriptorSetLayout fragmentUniformLayout;
} GraphicsPipelineLayoutHash;

typedef struct GraphicsPipelineLayoutHashMap
{
	GraphicsPipelineLayoutHash key;
	VulkanGraphicsPipelineLayout *value;
} GraphicsPipelineLayoutHashMap;

typedef struct GraphicsPipelineLayoutHashArray
{
	GraphicsPipelineLayoutHashMap *elements;
	int32_t count;
	int32_t capacity;
} GraphicsPipelineLayoutHashArray;

typedef struct GraphicsPipelineLayoutHashTable
{
	GraphicsPipelineLayoutHashArray buckets[NUM_PIPELINE_LAYOUT_BUCKETS];
} GraphicsPipelineLayoutHashTable;

static inline uint64_t GraphicsPipelineLayoutHashTable_GetHashCode(GraphicsPipelineLayoutHash key)
{
	const uint64_t HASH_FACTOR = 97;
	uint64_t result = 1;
	result = result * HASH_FACTOR + (uint64_t) key.vertexSamplerLayout;
	result = result * HASH_FACTOR + (uint64_t) key.fragmentSamplerLayout;
	result = result * HASH_FACTOR + (uint64_t) key.vertexUniformLayout;
	result = result * HASH_FACTOR + (uint64_t) key.fragmentUniformLayout;
	return result;
}

static inline VulkanGraphicsPipelineLayout* GraphicsPipelineLayoutHashArray_Fetch(
	GraphicsPipelineLayoutHashTable *table,
	GraphicsPipelineLayoutHash key
) {
	int32_t i;
	uint64_t hashcode = GraphicsPipelineLayoutHashTable_GetHashCode(key);
	GraphicsPipelineLayoutHashArray *arr = &table->buckets[hashcode % NUM_PIPELINE_LAYOUT_BUCKETS];

	for (i = 0; i < arr->count; i += 1)
	{
		const GraphicsPipelineLayoutHash *e = &arr->elements[i].key;
		if (	key.vertexSamplerLayout == e->vertexSamplerLayout &&
			key.fragmentSamplerLayout == e->fragmentSamplerLayout &&
			key.vertexUniformLayout == e->vertexUniformLayout &&
			key.fragmentUniformLayout == e->fragmentUniformLayout	)
		{
			return arr->elements[i].value;
		}
	}

	return NULL;
}

static inline void GraphicsPipelineLayoutHashArray_Insert(
	GraphicsPipelineLayoutHashTable *table,
	GraphicsPipelineLayoutHash key,
	VulkanGraphicsPipelineLayout *value
) {
	uint64_t hashcode = GraphicsPipelineLayoutHashTable_GetHashCode(key);
	GraphicsPipelineLayoutHashArray *arr = &table->buckets[hashcode % NUM_PIPELINE_LAYOUT_BUCKETS];

	GraphicsPipelineLayoutHashMap map;
	map.key = key;
	map.value = value;

	EXPAND_ELEMENTS_IF_NEEDED(arr, 4, GraphicsPipelineLayoutHashMap)

	arr->elements[arr->count] = map;
	arr->count += 1;
}

typedef struct ComputePipelineLayoutHash
{
	VkDescriptorSetLayout bufferLayout;
	VkDescriptorSetLayout imageLayout;
	VkDescriptorSetLayout uniformLayout;
} ComputePipelineLayoutHash;

typedef struct ComputePipelineLayoutHashMap
{
	ComputePipelineLayoutHash key;
	VulkanComputePipelineLayout *value;
} ComputePipelineLayoutHashMap;

typedef struct ComputePipelineLayoutHashArray
{
	ComputePipelineLayoutHashMap *elements;
	int32_t count;
	int32_t capacity;
} ComputePipelineLayoutHashArray;

typedef struct ComputePipelineLayoutHashTable
{
	ComputePipelineLayoutHashArray buckets[NUM_PIPELINE_LAYOUT_BUCKETS];
} ComputePipelineLayoutHashTable;

static inline uint64_t ComputePipelineLayoutHashTable_GetHashCode(ComputePipelineLayoutHash key)
{
	const uint64_t HASH_FACTOR = 97;
	uint64_t result = 1;
	result = result * HASH_FACTOR + (uint64_t) key.bufferLayout;
	result = result * HASH_FACTOR + (uint64_t) key.imageLayout;
	result = result * HASH_FACTOR + (uint64_t) key.uniformLayout;
	return result;
}

static inline VulkanComputePipelineLayout* ComputePipelineLayoutHashArray_Fetch(
	ComputePipelineLayoutHashTable *table,
	ComputePipelineLayoutHash key
) {
	int32_t i;
	uint64_t hashcode = ComputePipelineLayoutHashTable_GetHashCode(key);
	ComputePipelineLayoutHashArray *arr = &table->buckets[hashcode % NUM_PIPELINE_LAYOUT_BUCKETS];

	for (i = 0; i < arr->count; i += 1)
	{
		const ComputePipelineLayoutHash *e = &arr->elements[i].key;
		if (	key.bufferLayout == e->bufferLayout &&
			key.imageLayout == e->imageLayout &&
			key.uniformLayout == e->uniformLayout	)
		{
			return arr->elements[i].value;
		}
	}

	return NULL;
}

static inline void ComputePipelineLayoutHashArray_Insert(
	ComputePipelineLayoutHashTable *table,
	ComputePipelineLayoutHash key,
	VulkanComputePipelineLayout *value
) {
	uint64_t hashcode = ComputePipelineLayoutHashTable_GetHashCode(key);
	ComputePipelineLayoutHashArray *arr = &table->buckets[hashcode % NUM_PIPELINE_LAYOUT_BUCKETS];

	ComputePipelineLayoutHashMap map;
	map.key = key;
	map.value = value;

	EXPAND_ELEMENTS_IF_NEEDED(arr, 4, ComputePipelineLayoutHashMap)

	arr->elements[arr->count] = map;
	arr->count += 1;
}

/* Command structures */

typedef struct VulkanCommandPool VulkanCommandPool;

typedef struct VulkanCommandBuffer
{
	VkCommandBuffer commandBuffer;
	uint8_t fixed;
	uint8_t submitted;

	VulkanCommandPool *commandPool;

	VulkanComputePipeline *currentComputePipeline;
	VulkanGraphicsPipeline *currentGraphicsPipeline;
	VulkanFramebuffer *currentFramebuffer;

	VulkanBuffer *boundComputeBuffers[MAX_BUFFER_BINDINGS];
	uint32_t boundComputeBufferCount;
} VulkanCommandBuffer;

struct VulkanCommandPool
{
	SDL_threadID threadID;
	VkCommandPool commandPool;

	VulkanCommandBuffer **inactiveCommandBuffers;
	uint32_t inactiveCommandBufferCapacity;
	uint32_t inactiveCommandBufferCount;
};

#define NUM_COMMAND_POOL_BUCKETS 1031

typedef struct CommandPoolHash
{
	SDL_threadID threadID;
} CommandPoolHash;

typedef struct CommandPoolHashMap
{
	CommandPoolHash key;
	VulkanCommandPool *value;
} CommandPoolHashMap;

typedef struct CommandPoolHashArray
{
	CommandPoolHashMap *elements;
	uint32_t count;
	uint32_t capacity;
} CommandPoolHashArray;

typedef struct CommandPoolHashTable
{
	CommandPoolHashArray buckets[NUM_COMMAND_POOL_BUCKETS];
} CommandPoolHashTable;

static inline uint64_t CommandPoolHashTable_GetHashCode(CommandPoolHash key)
{
	const uint64_t HASH_FACTOR = 97;
	uint64_t result = 1;
	result = result * HASH_FACTOR + (uint64_t) key.threadID;
	return result;
}

static inline VulkanCommandPool* CommandPoolHashTable_Fetch(
	CommandPoolHashTable *table,
	CommandPoolHash key
) {
	uint32_t i;
	uint64_t hashcode = CommandPoolHashTable_GetHashCode(key);
	CommandPoolHashArray *arr = &table->buckets[hashcode % NUM_COMMAND_POOL_BUCKETS];

	for (i = 0; i < arr->count; i += 1)
	{
		const CommandPoolHash *e = &arr->elements[i].key;
		if (key.threadID == e->threadID)
		{
			return arr->elements[i].value;
		}
	}

	return NULL;
}

static inline void CommandPoolHashTable_Insert(
	CommandPoolHashTable *table,
	CommandPoolHash key,
	VulkanCommandPool *value
) {
	uint64_t hashcode = CommandPoolHashTable_GetHashCode(key);
	CommandPoolHashArray *arr = &table->buckets[hashcode % NUM_COMMAND_POOL_BUCKETS];

	CommandPoolHashMap map;
	map.key = key;
	map.value = value;

	EXPAND_ELEMENTS_IF_NEEDED(arr, 4, CommandPoolHashMap)

	arr->elements[arr->count] = map;
	arr->count += 1;
}

/* Context */

typedef struct VulkanRenderer
{
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkPhysicalDeviceProperties2 physicalDeviceProperties;
    VkPhysicalDeviceDriverPropertiesKHR physicalDeviceDriverProperties;
    VkDevice logicalDevice;

    void* deviceWindowHandle;

    uint8_t supportsDebugUtils;
    uint8_t debugMode;
    uint8_t headless;

	VulkanMemoryAllocator *memoryAllocator;

    Refresh_PresentMode presentMode;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapChain;
    VkFormat swapChainFormat;
    VkComponentMapping swapChainSwizzle;
    VkImage *swapChainImages;
    VkImageView *swapChainImageViews;
	uint32_t *swapChainQueueFamilyIndices;
    VulkanResourceAccessType *swapChainResourceAccessTypes;
    uint32_t swapChainImageCount;
    VkExtent2D swapChainExtent;

	uint8_t needNewSwapChain;
	uint8_t shouldPresent;
	uint8_t swapChainImageAcquired;
	uint32_t currentSwapChainIndex;

    QueueFamilyIndices queueFamilyIndices;
	VkQueue graphicsQueue;
	VkQueue presentQueue;
	VkQueue computeQueue;
	VkQueue transferQueue;

	VkFence inFlightFence;
	VkSemaphore transferFinishedSemaphore;
	VkSemaphore imageAvailableSemaphore;
	VkSemaphore renderFinishedSemaphore;

	VkCommandPool transferCommandPool;
	VkCommandBuffer transferCommandBuffers[2]; /* frame count */
	uint8_t pendingTransfer;

	VulkanCommandBuffer **submittedCommandBuffers;
	uint32_t submittedCommandBufferCount;
	uint32_t submittedCommandBufferCapacity;

	CommandPoolHashTable commandPoolHashTable;
	DescriptorSetLayoutHashTable descriptorSetLayoutHashTable;
	GraphicsPipelineLayoutHashTable graphicsPipelineLayoutHashTable;
	ComputePipelineLayoutHashTable computePipelineLayoutHashTable;

	/* initialize baseline descriptor info */
	VkDescriptorPool defaultDescriptorPool;

	VkDescriptorSetLayout emptyVertexSamplerLayout;
	VkDescriptorSetLayout emptyFragmentSamplerLayout;
	VkDescriptorSetLayout emptyComputeBufferDescriptorSetLayout;
	VkDescriptorSetLayout emptyComputeImageDescriptorSetLayout;

	VkDescriptorSet emptyVertexSamplerDescriptorSet;
	VkDescriptorSet emptyFragmentSamplerDescriptorSet;
	VkDescriptorSet emptyComputeBufferDescriptorSet;
	VkDescriptorSet emptyComputeImageDescriptorSet;

	VkDescriptorSetLayout vertexParamLayout;
	VkDescriptorSetLayout fragmentParamLayout;
	VkDescriptorSetLayout computeParamLayout;
	VulkanBuffer *dummyVertexUniformBuffer;
	VulkanBuffer *dummyFragmentUniformBuffer;
	VulkanBuffer *dummyComputeUniformBuffer;

	VulkanBuffer *textureStagingBuffer;
	uint32_t textureStagingBufferOffset;

	VulkanBuffer** buffersInUse;
	uint32_t buffersInUseCount;
	uint32_t buffersInUseCapacity;

	VulkanBuffer** submittedBuffers;
	uint32_t submittedBufferCount;
	uint32_t submittedBufferCapacity;

	VulkanBuffer *vertexUBO;
	VulkanBuffer *fragmentUBO;
	VulkanBuffer *computeUBO;
	uint32_t minUBOAlignment;

	uint32_t vertexUBOOffset;
	VkDeviceSize vertexUBOBlockIncrement;

	uint32_t fragmentUBOOffset;
	VkDeviceSize fragmentUBOBlockIncrement;

	uint32_t computeUBOOffset;
	VkDeviceSize computeUBOBlockIncrement;

	uint32_t frameIndex;

	SDL_mutex *allocatorLock;
	SDL_mutex *disposeLock;
	SDL_mutex *uniformBufferLock;
	SDL_mutex *descriptorSetLock;
	SDL_mutex *boundBufferLock;
	SDL_mutex *stagingLock;

	/* Deferred destroy storage */

	VulkanColorTarget **colorTargetsToDestroy;
	uint32_t colorTargetsToDestroyCount;
	uint32_t colorTargetsToDestroyCapacity;

	VulkanColorTarget **submittedColorTargetsToDestroy;
	uint32_t submittedColorTargetsToDestroyCount;
	uint32_t submittedColorTargetsToDestroyCapacity;

	VulkanDepthStencilTarget **depthStencilTargetsToDestroy;
	uint32_t depthStencilTargetsToDestroyCount;
	uint32_t depthStencilTargetsToDestroyCapacity;

	VulkanDepthStencilTarget **submittedDepthStencilTargetsToDestroy;
	uint32_t submittedDepthStencilTargetsToDestroyCount;
	uint32_t submittedDepthStencilTargetsToDestroyCapacity;

	VulkanTexture **texturesToDestroy;
	uint32_t texturesToDestroyCount;
	uint32_t texturesToDestroyCapacity;

	VulkanTexture **submittedTexturesToDestroy;
	uint32_t submittedTexturesToDestroyCount;
	uint32_t submittedTexturesToDestroyCapacity;

	VulkanBuffer **buffersToDestroy;
	uint32_t buffersToDestroyCount;
	uint32_t buffersToDestroyCapacity;

	VulkanBuffer **submittedBuffersToDestroy;
	uint32_t submittedBuffersToDestroyCount;
	uint32_t submittedBuffersToDestroyCapacity;

	VulkanGraphicsPipeline **graphicsPipelinesToDestroy;
	uint32_t graphicsPipelinesToDestroyCount;
	uint32_t graphicsPipelinesToDestroyCapacity;

	VulkanGraphicsPipeline **submittedGraphicsPipelinesToDestroy;
	uint32_t submittedGraphicsPipelinesToDestroyCount;
	uint32_t submittedGraphicsPipelinesToDestroyCapacity;

	VulkanComputePipeline **computePipelinesToDestroy;
	uint32_t computePipelinesToDestroyCount;
	uint32_t computePipelinesToDestroyCapacity;

	VulkanComputePipeline **submittedComputePipelinesToDestroy;
	uint32_t submittedComputePipelinesToDestroyCount;
	uint32_t submittedComputePipelinesToDestroyCapacity;

	VkShaderModule *shaderModulesToDestroy;
	uint32_t shaderModulesToDestroyCount;
	uint32_t shaderModulesToDestroyCapacity;

	VkShaderModule *submittedShaderModulesToDestroy;
	uint32_t submittedShaderModulesToDestroyCount;
	uint32_t submittedShaderModulesToDestroyCapacity;

	VkSampler *samplersToDestroy;
	uint32_t samplersToDestroyCount;
	uint32_t samplersToDestroyCapacity;

	VkSampler *submittedSamplersToDestroy;
	uint32_t submittedSamplersToDestroyCount;
	uint32_t submittedSamplersToDestroyCapacity;

	VulkanFramebuffer **framebuffersToDestroy;
	uint32_t framebuffersToDestroyCount;
	uint32_t framebuffersToDestroyCapacity;

	VulkanFramebuffer **submittedFramebuffersToDestroy;
	uint32_t submittedFramebuffersToDestroyCount;
	uint32_t submittedFramebuffersToDestroyCapacity;

	VkRenderPass *renderPassesToDestroy;
	uint32_t renderPassesToDestroyCount;
	uint32_t renderPassesToDestroyCapacity;

	VkRenderPass *submittedRenderPassesToDestroy;
	uint32_t submittedRenderPassesToDestroyCount;
	uint32_t submittedRenderPassesToDestroyCapacity;

	/* External Interop */

	uint8_t usesExternalDevice;

    #define VULKAN_INSTANCE_FUNCTION(ext, ret, func, params) \
		vkfntype_##func func;
	#define VULKAN_DEVICE_FUNCTION(ext, ret, func, params) \
		vkfntype_##func func;
	#include "Refresh_Driver_Vulkan_vkfuncs.h"
} VulkanRenderer;

/* Forward declarations */

static void VULKAN_INTERNAL_BeginCommandBuffer(VulkanRenderer *renderer, VulkanCommandBuffer *commandBuffer);
static void VULKAN_Submit(Refresh_Renderer *driverData, uint32_t commandBufferCount, Refresh_CommandBuffer **pCommandBuffers);
static void VULKAN_INTERNAL_FlushTransfers(VulkanRenderer *renderer);

/* Error Handling */

static inline const char* VkErrorMessages(VkResult code)
{
	#define ERR_TO_STR(e) \
		case e: return #e;
	switch (code)
	{
		ERR_TO_STR(VK_ERROR_OUT_OF_HOST_MEMORY)
		ERR_TO_STR(VK_ERROR_OUT_OF_DEVICE_MEMORY)
		ERR_TO_STR(VK_ERROR_FRAGMENTED_POOL)
		ERR_TO_STR(VK_ERROR_OUT_OF_POOL_MEMORY)
		ERR_TO_STR(VK_ERROR_INITIALIZATION_FAILED)
		ERR_TO_STR(VK_ERROR_LAYER_NOT_PRESENT)
		ERR_TO_STR(VK_ERROR_EXTENSION_NOT_PRESENT)
		ERR_TO_STR(VK_ERROR_FEATURE_NOT_PRESENT)
		ERR_TO_STR(VK_ERROR_TOO_MANY_OBJECTS)
		ERR_TO_STR(VK_ERROR_DEVICE_LOST)
		ERR_TO_STR(VK_ERROR_INCOMPATIBLE_DRIVER)
		ERR_TO_STR(VK_ERROR_OUT_OF_DATE_KHR)
		ERR_TO_STR(VK_ERROR_SURFACE_LOST_KHR)
		ERR_TO_STR(VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT)
		ERR_TO_STR(VK_SUBOPTIMAL_KHR)
		default: return "Unhandled VkResult!";
	}
	#undef ERR_TO_STR
}

static inline void LogVulkanResult(
	const char* vulkanFunctionName,
	VkResult result
) {
	if (result != VK_SUCCESS)
	{
		Refresh_LogError(
			"%s: %s",
			vulkanFunctionName,
			VkErrorMessages(result)
		);
	}
}

/* Utility */

static inline uint8_t DepthFormatContainsStencil(VkFormat format)
{
	switch(format)
	{
		case VK_FORMAT_D16_UNORM:
		case VK_FORMAT_D32_SFLOAT:
			return 0;
		case VK_FORMAT_D16_UNORM_S8_UINT:
		case VK_FORMAT_D24_UNORM_S8_UINT:
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
			return 1;
		default:
			SDL_assert(0 && "Invalid depth format");
			return 0;
	}
}

/* Memory Management */

static inline VkDeviceSize VULKAN_INTERNAL_NextHighestAlignment(
	VkDeviceSize n,
	VkDeviceSize align
) {
	return align * ((n + align - 1) / align);
}

static VulkanMemoryFreeRegion* VULKAN_INTERNAL_NewMemoryFreeRegion(
	VulkanMemoryAllocation *allocation,
	VkDeviceSize offset,
	VkDeviceSize size
) {
	VulkanMemoryFreeRegion *newFreeRegion;
	uint32_t insertionIndex = 0;
	uint32_t i;

	/* TODO: an improvement here could be to merge contiguous free regions */
	allocation->freeRegionCount += 1;
	if (allocation->freeRegionCount > allocation->freeRegionCapacity)
	{
		allocation->freeRegionCapacity *= 2;
		allocation->freeRegions = SDL_realloc(
			allocation->freeRegions,
			sizeof(VulkanMemoryFreeRegion*) * allocation->freeRegionCapacity
		);
	}

	newFreeRegion = SDL_malloc(sizeof(VulkanMemoryFreeRegion));
	newFreeRegion->offset = offset;
	newFreeRegion->size = size;
	newFreeRegion->allocation = allocation;

	allocation->freeRegions[allocation->freeRegionCount - 1] = newFreeRegion;
	newFreeRegion->allocationIndex = allocation->freeRegionCount - 1;

	for (i = 0; i < allocation->allocator->sortedFreeRegionCount; i += 1)
	{
		if (allocation->allocator->sortedFreeRegions[i]->size < size)
		{
			/* this is where the new region should go */
			break;
		}

		insertionIndex += 1;
	}

	if (allocation->allocator->sortedFreeRegionCount + 1 > allocation->allocator->sortedFreeRegionCapacity)
	{
		allocation->allocator->sortedFreeRegionCapacity *= 2;
		allocation->allocator->sortedFreeRegions = SDL_realloc(
			allocation->allocator->sortedFreeRegions,
			sizeof(VulkanMemoryFreeRegion*) * allocation->allocator->sortedFreeRegionCapacity
		);
	}

	/* perform insertion sort */
	if (allocation->allocator->sortedFreeRegionCount > 0 && insertionIndex != allocation->allocator->sortedFreeRegionCount)
	{
		for (i = allocation->allocator->sortedFreeRegionCount; i > insertionIndex && i > 0; i -= 1)
		{
			allocation->allocator->sortedFreeRegions[i] = allocation->allocator->sortedFreeRegions[i - 1];
			allocation->allocator->sortedFreeRegions[i]->sortedIndex = i;
		}
	}

	allocation->allocator->sortedFreeRegionCount += 1;
	allocation->allocator->sortedFreeRegions[insertionIndex] = newFreeRegion;
	newFreeRegion->sortedIndex = insertionIndex;

	return newFreeRegion;
}

static void VULKAN_INTERNAL_RemoveMemoryFreeRegion(
	VulkanMemoryFreeRegion *freeRegion
) {
	uint32_t i;

	/* close the gap in the sorted list */
	if (freeRegion->allocation->allocator->sortedFreeRegionCount > 1)
	{
		for (i = freeRegion->sortedIndex; i < freeRegion->allocation->allocator->sortedFreeRegionCount - 1; i += 1)
		{
			freeRegion->allocation->allocator->sortedFreeRegions[i] =
				freeRegion->allocation->allocator->sortedFreeRegions[i + 1];

			freeRegion->allocation->allocator->sortedFreeRegions[i]->sortedIndex = i;
		}
	}

	freeRegion->allocation->allocator->sortedFreeRegionCount -= 1;

	/* close the gap in the buffer list */
	if (freeRegion->allocation->freeRegionCount > 1 && freeRegion->allocationIndex != freeRegion->allocation->freeRegionCount - 1)
	{
		freeRegion->allocation->freeRegions[freeRegion->allocationIndex] =
			freeRegion->allocation->freeRegions[freeRegion->allocation->freeRegionCount - 1];

		freeRegion->allocation->freeRegions[freeRegion->allocationIndex]->allocationIndex =
			freeRegion->allocationIndex;
	}

	freeRegion->allocation->freeRegionCount -= 1;

	SDL_free(freeRegion);
}

static uint8_t VULKAN_INTERNAL_FindMemoryType(
	VulkanRenderer *renderer,
	uint32_t typeFilter,
	VkMemoryPropertyFlags properties,
	uint32_t *result
) {
	VkPhysicalDeviceMemoryProperties memoryProperties;
	uint32_t i;

	renderer->vkGetPhysicalDeviceMemoryProperties(
		renderer->physicalDevice,
		&memoryProperties
	);

	for (i = 0; i < memoryProperties.memoryTypeCount; i += 1)
	{
		if (	(typeFilter & (1 << i)) &&
			(memoryProperties.memoryTypes[i].propertyFlags & properties) == properties	)
		{
			*result = i;
			return 1;
		}
	}

	Refresh_LogError("Failed to find memory properties %X, filter %X", properties, typeFilter);
	return 0;
}

static uint8_t VULKAN_INTERNAL_FindBufferMemoryRequirements(
	VulkanRenderer *renderer,
	VkBuffer buffer,
	VkMemoryRequirements2KHR *pMemoryRequirements,
	uint32_t *pMemoryTypeIndex
) {
	VkBufferMemoryRequirementsInfo2KHR bufferRequirementsInfo;
	bufferRequirementsInfo.sType =
		VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2_KHR;
	bufferRequirementsInfo.pNext = NULL;
	bufferRequirementsInfo.buffer = buffer;

	renderer->vkGetBufferMemoryRequirements2KHR(
		renderer->logicalDevice,
		&bufferRequirementsInfo,
		pMemoryRequirements
	);

	if (!VULKAN_INTERNAL_FindMemoryType(
		renderer,
		pMemoryRequirements->memoryRequirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		pMemoryTypeIndex
	)) {
		Refresh_LogError(
			"Could not find valid memory type for buffer creation"
		);
		return 0;
	}

	return 1;
}

static uint8_t VULKAN_INTERNAL_FindImageMemoryRequirements(
	VulkanRenderer *renderer,
	VkImage image,
	VkMemoryRequirements2KHR *pMemoryRequirements,
	uint32_t *pMemoryTypeIndex
) {
	VkImageMemoryRequirementsInfo2KHR imageRequirementsInfo;
	imageRequirementsInfo.sType =
		VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2_KHR;
	imageRequirementsInfo.pNext = NULL;
	imageRequirementsInfo.image = image;

	renderer->vkGetImageMemoryRequirements2KHR(
		renderer->logicalDevice,
		&imageRequirementsInfo,
		pMemoryRequirements
	);

	if (!VULKAN_INTERNAL_FindMemoryType(
		renderer,
		pMemoryRequirements->memoryRequirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		pMemoryTypeIndex
	)) {
		Refresh_LogError(
			"Could not find valid memory type for image creation"
		);
		return 0;
	}

	return 1;
}

static uint8_t VULKAN_INTERNAL_AllocateMemory(
	VulkanRenderer *renderer,
	VkBuffer buffer,
	VkImage image,
	uint32_t memoryTypeIndex,
	VkDeviceSize allocationSize,
	uint8_t dedicated,
	VulkanMemoryAllocation **pMemoryAllocation
) {
	VulkanMemoryAllocation *allocation;
	VulkanMemorySubAllocator *allocator = &renderer->memoryAllocator->subAllocators[memoryTypeIndex];
	VkMemoryAllocateInfo allocInfo;
	VkMemoryDedicatedAllocateInfoKHR dedicatedInfo;
	VkResult result;

	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.memoryTypeIndex = memoryTypeIndex;
	allocInfo.allocationSize = allocationSize;

	allocation = SDL_malloc(sizeof(VulkanMemoryAllocation));
	allocation->size = allocationSize;
	allocation->memoryLock = SDL_CreateMutex();

	if (dedicated)
	{
		dedicatedInfo.sType =
			VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR;
		dedicatedInfo.pNext = NULL;
		dedicatedInfo.buffer = buffer;
		dedicatedInfo.image = image;

		allocInfo.pNext = &dedicatedInfo;

		allocation->dedicated = 1;
	}
	else
	{
		allocInfo.pNext = NULL;

		/* allocate a non-dedicated texture buffer */
		allocator->allocationCount += 1;
		allocator->allocations = SDL_realloc(
			allocator->allocations,
			sizeof(VulkanMemoryAllocation*) * allocator->allocationCount
		);

		allocator->allocations[
			allocator->allocationCount - 1
		] = allocation;

		allocation->dedicated = 0;
	}

	allocation->freeRegions = SDL_malloc(sizeof(VulkanMemoryFreeRegion*));
	allocation->freeRegionCount = 0;
	allocation->freeRegionCapacity = 1;
	allocation->allocator = allocator;

	result = renderer->vkAllocateMemory(
		renderer->logicalDevice,
		&allocInfo,
		NULL,
		&allocation->memory
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkAllocateMemory", result);
		return 0;
	}

	if (buffer != NULL)
	{
		result = renderer->vkMapMemory(
			renderer->logicalDevice,
			allocation->memory,
			0,
			allocation->size,
			0,
			(void**) &allocation->mapPointer
		);

		if (result != VK_SUCCESS)
		{
			LogVulkanResult("vkMapMemory", result);
			return 0;
		}
	}
	else
	{
		allocation->mapPointer = NULL;
	}

	VULKAN_INTERNAL_NewMemoryFreeRegion(
		allocation,
		0,
		allocation->size
	);

	*pMemoryAllocation = allocation;
	return 1;
}

static uint8_t VULKAN_INTERNAL_FindAvailableMemory(
	VulkanRenderer *renderer,
	VkBuffer buffer,
	VkImage image,
	VulkanMemoryAllocation **pMemoryAllocation,
	VkDeviceSize *pOffset,
	VkDeviceSize *pSize
) {
	VkMemoryDedicatedRequirementsKHR dedicatedRequirements =
	{
		VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR,
		NULL
	};
	VkMemoryRequirements2KHR memoryRequirements =
	{
		VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR,
		&dedicatedRequirements
	};
	uint32_t memoryTypeIndex;

	VulkanMemoryAllocation *allocation;
	VulkanMemorySubAllocator *allocator;
	VulkanMemoryFreeRegion *region;

	VkDeviceSize requiredSize, allocationSize;
	VkDeviceSize alignedOffset;
	VkDeviceSize newRegionSize, newRegionOffset;
	uint8_t allocationResult;

	if (buffer != VK_NULL_HANDLE && image != VK_NULL_HANDLE)
	{
		Refresh_LogError("Calling FindAvailableMemory with both a buffer and image handle is invalid!");
		return 0;
	}
	else if (buffer != VK_NULL_HANDLE)
	{
		if (!VULKAN_INTERNAL_FindBufferMemoryRequirements(
			renderer,
			buffer,
			&memoryRequirements,
			&memoryTypeIndex
		)) {
			Refresh_LogError("Failed to acquire buffer memory requirements!");
			return 0;
		}
	}
	else if (image != VK_NULL_HANDLE)
	{
		if (!VULKAN_INTERNAL_FindImageMemoryRequirements(
			renderer,
			image,
			&memoryRequirements,
			&memoryTypeIndex
		)) {
			Refresh_LogError("Failed to acquire image memory requirements!");
			return 0;
		}
	}
	else
	{
		Refresh_LogError("Calling FindAvailableMemory with neither buffer nor image handle is invalid!");
		return 0;
	}

	allocator = &renderer->memoryAllocator->subAllocators[memoryTypeIndex];
	requiredSize = memoryRequirements.memoryRequirements.size;

	SDL_LockMutex(renderer->allocatorLock);

	/* find the largest free region and use it */
	if (allocator->sortedFreeRegionCount > 0)
	{
		region = allocator->sortedFreeRegions[0];
		allocation = region->allocation;

		alignedOffset = VULKAN_INTERNAL_NextHighestAlignment(
			region->offset,
			memoryRequirements.memoryRequirements.alignment
		);

		if (alignedOffset + requiredSize <= region->offset + region->size)
		{
			*pMemoryAllocation = allocation;

			/* not aligned - create a new free region */
			if (region->offset != alignedOffset)
			{
				VULKAN_INTERNAL_NewMemoryFreeRegion(
					allocation,
					region->offset,
					alignedOffset - region->offset
				);
			}

			*pOffset = alignedOffset;
			*pSize = requiredSize;

			newRegionSize = region->size - ((alignedOffset - region->offset) + requiredSize);
			newRegionOffset = alignedOffset + requiredSize;

			/* remove and add modified region to re-sort */
			VULKAN_INTERNAL_RemoveMemoryFreeRegion(region);

			/* if size is 0, no need to re-insert */
			if (newRegionSize != 0)
			{
				VULKAN_INTERNAL_NewMemoryFreeRegion(
					allocation,
					newRegionOffset,
					newRegionSize
				);
			}

			SDL_UnlockMutex(renderer->allocatorLock);

			return 1;
		}
	}

	/* No suitable free regions exist, allocate a new memory region */

	if (dedicatedRequirements.prefersDedicatedAllocation || dedicatedRequirements.requiresDedicatedAllocation)
	{
		allocationSize = requiredSize;
	}
	else if (requiredSize > allocator->nextAllocationSize)
	{
		/* allocate a page of required size aligned to STARTING_ALLOCATION_SIZE increments */
		allocationSize =
			VULKAN_INTERNAL_NextHighestAlignment(requiredSize, STARTING_ALLOCATION_SIZE);
	}
	else
	{
		allocationSize = allocator->nextAllocationSize;
		allocator->nextAllocationSize = SDL_min(allocator->nextAllocationSize * 2, MAX_ALLOCATION_SIZE);
	}

	allocationResult = VULKAN_INTERNAL_AllocateMemory(
		renderer,
		buffer,
		image,
		memoryTypeIndex,
		allocationSize,
		dedicatedRequirements.prefersDedicatedAllocation || dedicatedRequirements.requiresDedicatedAllocation,
		&allocation
	);

	/* Uh oh, we're out of memory */
	if (allocationResult == 0)
	{
		/* Responsibility of the caller to handle being out of memory */
		Refresh_LogWarn("Failed to allocate memory!");
		SDL_UnlockMutex(renderer->allocatorLock);

		return 2;
	}

	*pMemoryAllocation = allocation;
	*pOffset = 0;
	*pSize = requiredSize;

	region = allocation->freeRegions[0];

	newRegionOffset = region->offset + requiredSize;
	newRegionSize = region->size - requiredSize;

	VULKAN_INTERNAL_RemoveMemoryFreeRegion(region);

	if (newRegionSize != 0)
	{
		VULKAN_INTERNAL_NewMemoryFreeRegion(
			allocation,
			newRegionOffset,
			newRegionSize
		);
	}

	SDL_UnlockMutex(renderer->allocatorLock);

	return 1;
}

/* Memory Barriers */

static void VULKAN_INTERNAL_BufferMemoryBarrier(
	VulkanRenderer *renderer,
	VkCommandBuffer commandBuffer,
	VulkanResourceAccessType nextResourceAccessType,
	VulkanBuffer *buffer,
	VulkanSubBuffer *subBuffer
) {
	VkPipelineStageFlags srcStages = 0;
	VkPipelineStageFlags dstStages = 0;
	VkBufferMemoryBarrier memoryBarrier;
	VulkanResourceAccessType prevAccess, nextAccess;
	const VulkanResourceAccessInfo *prevAccessInfo, *nextAccessInfo;

	if (buffer->resourceAccessType == nextResourceAccessType)
	{
		return;
	}

	memoryBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	memoryBarrier.pNext = NULL;
	memoryBarrier.srcAccessMask = 0;
	memoryBarrier.dstAccessMask = 0;
	memoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	memoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	memoryBarrier.buffer = subBuffer->buffer;
	memoryBarrier.offset = 0;
	memoryBarrier.size = buffer->size;

	prevAccess = buffer->resourceAccessType;
	prevAccessInfo = &AccessMap[prevAccess];

	srcStages |= prevAccessInfo->stageMask;

	if (prevAccess > RESOURCE_ACCESS_END_OF_READ)
	{
		memoryBarrier.srcAccessMask |= prevAccessInfo->accessMask;
	}

	nextAccess = nextResourceAccessType;
	nextAccessInfo = &AccessMap[nextAccess];

	dstStages |= nextAccessInfo->stageMask;

	if (memoryBarrier.srcAccessMask != 0)
	{
		memoryBarrier.dstAccessMask |= nextAccessInfo->accessMask;
	}

	if (srcStages == 0)
	{
		srcStages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	}
	if (dstStages == 0)
	{
		dstStages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	}

	renderer->vkCmdPipelineBarrier(
		commandBuffer,
		srcStages,
		dstStages,
		0,
		0,
		NULL,
		1,
		&memoryBarrier,
		0,
		NULL
	);

	buffer->resourceAccessType = nextResourceAccessType;
}

static void VULKAN_INTERNAL_ImageMemoryBarrier(
	VulkanRenderer *renderer,
	VkCommandBuffer commandBuffer,
	VulkanResourceAccessType nextAccess,
	VkImageAspectFlags aspectMask,
	uint32_t baseLayer,
	uint32_t layerCount,
	uint32_t baseLevel,
	uint32_t levelCount,
	uint8_t discardContents,
	VkImage image,
	VulkanResourceAccessType *resourceAccessType
) {
	VkPipelineStageFlags srcStages = 0;
	VkPipelineStageFlags dstStages = 0;
	VkImageMemoryBarrier memoryBarrier;
	VulkanResourceAccessType prevAccess;
	const VulkanResourceAccessInfo *pPrevAccessInfo, *pNextAccessInfo;

	if (*resourceAccessType == nextAccess)
	{
		return;
	}

	memoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	memoryBarrier.pNext = NULL;
	memoryBarrier.srcAccessMask = 0;
	memoryBarrier.dstAccessMask = 0;
	memoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	memoryBarrier.newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	memoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	memoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	memoryBarrier.image = image;
	memoryBarrier.subresourceRange.aspectMask = aspectMask;
	memoryBarrier.subresourceRange.baseArrayLayer = baseLayer;
	memoryBarrier.subresourceRange.layerCount = layerCount;
	memoryBarrier.subresourceRange.baseMipLevel = baseLevel;
	memoryBarrier.subresourceRange.levelCount = levelCount;

	prevAccess = *resourceAccessType;
	pPrevAccessInfo = &AccessMap[prevAccess];

	srcStages |= pPrevAccessInfo->stageMask;

	if (prevAccess > RESOURCE_ACCESS_END_OF_READ)
	{
		memoryBarrier.srcAccessMask |= pPrevAccessInfo->accessMask;
	}

	if (discardContents)
	{
		memoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	}
	else
	{
		memoryBarrier.oldLayout = pPrevAccessInfo->imageLayout;
	}

	pNextAccessInfo = &AccessMap[nextAccess];

	dstStages |= pNextAccessInfo->stageMask;

	memoryBarrier.dstAccessMask |= pNextAccessInfo->accessMask;
	memoryBarrier.newLayout = pNextAccessInfo->imageLayout;

	if (srcStages == 0)
	{
		srcStages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	}
	if (dstStages == 0)
	{
		dstStages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	}

	renderer->vkCmdPipelineBarrier(
		commandBuffer,
		srcStages,
		dstStages,
		0,
		0,
		NULL,
		0,
		NULL,
		1,
		&memoryBarrier
	);

	*resourceAccessType = nextAccess;
}

/* Resource Disposal */

static void VULKAN_INTERNAL_DestroyTexture(
	VulkanRenderer* renderer,
	VulkanTexture* texture
) {
	if (texture->allocation->dedicated)
	{
		renderer->vkFreeMemory(
			renderer->logicalDevice,
			texture->allocation->memory,
			NULL
		);

		SDL_DestroyMutex(texture->allocation->memoryLock);
		SDL_free(texture->allocation->freeRegions);
		SDL_free(texture->allocation);
	}
	else
	{
		SDL_LockMutex(renderer->allocatorLock);

		VULKAN_INTERNAL_NewMemoryFreeRegion(
			texture->allocation,
			texture->offset,
			texture->memorySize
		);

		SDL_UnlockMutex(renderer->allocatorLock);
	}

	renderer->vkDestroyImageView(
		renderer->logicalDevice,
		texture->view,
		NULL
	);

	renderer->vkDestroyImage(
		renderer->logicalDevice,
		texture->image,
		NULL
	);

	SDL_free(texture);
}

static void VULKAN_INTERNAL_DestroyColorTarget(
	VulkanRenderer *renderer,
	VulkanColorTarget *colorTarget
) {
	renderer->vkDestroyImageView(
		renderer->logicalDevice,
		colorTarget->view,
		NULL
	);

	/* The texture is not owned by the ColorTarget
	 * so we don't free it here
	 * But the multisampleTexture is!
	 */
	if (colorTarget->multisampleTexture != NULL)
	{
		VULKAN_INTERNAL_DestroyTexture(
			renderer,
			colorTarget->multisampleTexture
		);
	}

	SDL_free(colorTarget);
}

static void VULKAN_INTERNAL_DestroyDepthStencilTarget(
	VulkanRenderer *renderer,
	VulkanDepthStencilTarget *depthStencilTarget
) {
	VULKAN_INTERNAL_DestroyTexture(renderer, depthStencilTarget->texture);
	SDL_free(depthStencilTarget);
}

static void VULKAN_INTERNAL_DestroyBuffer(
	VulkanRenderer* renderer,
	VulkanBuffer* buffer
) {
	uint32_t i;

	if (buffer->bound || buffer->boundSubmitted)
	{
		Refresh_LogError("Cannot destroy a bound buffer!");
		return;
	}

	for (i = 0; i < buffer->subBufferCount; i += 1)
	{
		if (buffer->subBuffers[i]->allocation->dedicated)
		{
			renderer->vkFreeMemory(
				renderer->logicalDevice,
				buffer->subBuffers[i]->allocation->memory,
				NULL
			);

			SDL_DestroyMutex(buffer->subBuffers[i]->allocation->memoryLock);
			SDL_free(buffer->subBuffers[i]->allocation->freeRegions);
			SDL_free(buffer->subBuffers[i]->allocation);
		}
		else
		{
			SDL_LockMutex(renderer->allocatorLock);

			VULKAN_INTERNAL_NewMemoryFreeRegion(
				buffer->subBuffers[i]->allocation,
				buffer->subBuffers[i]->offset,
				buffer->subBuffers[i]->size
			);

			SDL_UnlockMutex(renderer->allocatorLock);
		}

		renderer->vkDestroyBuffer(
			renderer->logicalDevice,
			buffer->subBuffers[i]->buffer,
			NULL
		);

		SDL_free(buffer->subBuffers[i]);
	}

	SDL_free(buffer->subBuffers);
	buffer->subBuffers = NULL;

	SDL_free(buffer);
}

static void VULKAN_INTERNAL_DestroyCommandPool(
	VulkanRenderer *renderer,
	VulkanCommandPool *commandPool
) {
	renderer->vkDestroyCommandPool(
		renderer->logicalDevice,
		commandPool->commandPool,
		NULL
	);

	SDL_free(commandPool->inactiveCommandBuffers);
	SDL_free(commandPool);
}

static void VULKAN_INTERNAL_DestroyGraphicsPipeline(
	VulkanRenderer *renderer,
	VulkanGraphicsPipeline *graphicsPipeline
) {
	VkDescriptorSet descriptorSets[2];
	descriptorSets[0] = graphicsPipeline->vertexUBODescriptorSet;
	descriptorSets[1] = graphicsPipeline->fragmentUBODescriptorSet;

	renderer->vkFreeDescriptorSets(
		renderer->logicalDevice,
		renderer->defaultDescriptorPool,
		2,
		descriptorSets
	);

	renderer->vkDestroyPipeline(
		renderer->logicalDevice,
		graphicsPipeline->pipeline,
		NULL
	);

	SDL_free(graphicsPipeline);
}

static void VULKAN_INTERNAL_DestroyComputePipeline(
	VulkanRenderer *renderer,
	VulkanComputePipeline *computePipeline
) {
	renderer->vkFreeDescriptorSets(
		renderer->logicalDevice,
		renderer->defaultDescriptorPool,
		1,
		&computePipeline->computeUBODescriptorSet
	);

	renderer->vkDestroyPipeline(
		renderer->logicalDevice,
		computePipeline->pipeline,
		NULL
	);

	SDL_free(computePipeline);
}

static void VULKAN_INTERNAL_DestroyShaderModule(
	VulkanRenderer *renderer,
	VkShaderModule shaderModule
) {
	renderer->vkDestroyShaderModule(
		renderer->logicalDevice,
		shaderModule,
		NULL
	);
}

static void VULKAN_INTERNAL_DestroySampler(
	VulkanRenderer *renderer,
	VkSampler sampler
) {
	renderer->vkDestroySampler(
		renderer->logicalDevice,
		sampler,
		NULL
	);
}

/* The framebuffer doesn't own any targets so we don't have to do much. */
static void VULKAN_INTERNAL_DestroyFramebuffer(
	VulkanRenderer *renderer,
	VulkanFramebuffer *framebuffer
) {
	renderer->vkDestroyFramebuffer(
		renderer->logicalDevice,
		framebuffer->framebuffer,
		NULL
	);
}

static void VULKAN_INTERNAL_DestroyRenderPass(
	VulkanRenderer *renderer,
	VkRenderPass renderPass
) {
	renderer->vkDestroyRenderPass(
		renderer->logicalDevice,
		renderPass,
		NULL
	);
}

static void VULKAN_INTERNAL_DestroySwapchain(VulkanRenderer* renderer)
{
	uint32_t i;

	for (i = 0; i < renderer->swapChainImageCount; i += 1)
	{
		renderer->vkDestroyImageView(
			renderer->logicalDevice,
			renderer->swapChainImageViews[i],
			NULL
		);
	}

	SDL_free(renderer->swapChainImages);
	renderer->swapChainImages = NULL;
	SDL_free(renderer->swapChainImageViews);
	renderer->swapChainImageViews = NULL;
	SDL_free(renderer->swapChainResourceAccessTypes);
	renderer->swapChainResourceAccessTypes = NULL;
	SDL_free(renderer->swapChainQueueFamilyIndices);
	renderer->swapChainQueueFamilyIndices = NULL;

	renderer->vkDestroySwapchainKHR(
		renderer->logicalDevice,
		renderer->swapChain,
		NULL
	);
}

static void VULKAN_INTERNAL_DestroyTextureStagingBuffer(
	VulkanRenderer* renderer
) {
	VULKAN_INTERNAL_DestroyBuffer(
		renderer,
		renderer->textureStagingBuffer
	);
}

static void VULKAN_INTERNAL_DestroyBufferDescriptorSetCache(
	VulkanRenderer *renderer,
	BufferDescriptorSetCache *cache
) {
	uint32_t i;

	if (cache == NULL)
	{
		return;
	}

	for (i = 0; i < cache->bufferDescriptorPoolCount; i += 1)
	{
		renderer->vkDestroyDescriptorPool(
			renderer->logicalDevice,
			cache->bufferDescriptorPools[i],
			NULL
		);
	}

	SDL_free(cache->bufferDescriptorPools);
	SDL_free(cache->inactiveDescriptorSets);
	SDL_free(cache->elements);

	for (i = 0; i < NUM_DESCRIPTOR_SET_HASH_BUCKETS; i += 1)
	{
		SDL_free(cache->buckets[i].elements);
	}

	SDL_free(cache);
}

static void VULKAN_INTERNAL_DestroyImageDescriptorSetCache(
	VulkanRenderer *renderer,
	ImageDescriptorSetCache *cache
) {
	uint32_t i;

	if (cache == NULL)
	{
		return;
	}

	for (i = 0; i < cache->imageDescriptorPoolCount; i += 1)
	{
		renderer->vkDestroyDescriptorPool(
			renderer->logicalDevice,
			cache->imageDescriptorPools[i],
			NULL
		);
	}

	SDL_free(cache->imageDescriptorPools);
	SDL_free(cache->inactiveDescriptorSets);
	SDL_free(cache->elements);

	for (i = 0; i < NUM_DESCRIPTOR_SET_HASH_BUCKETS; i += 1)
	{
		SDL_free(cache->buckets[i].elements);
	}

	SDL_free(cache);
}

static void VULKAN_INTERNAL_PostWorkCleanup(VulkanRenderer* renderer)
{
	uint32_t i, j;

	/* Destroy submitted resources */

	SDL_LockMutex(renderer->disposeLock);

	for (i = 0; i < renderer->submittedColorTargetsToDestroyCount; i += 1)
	{
		VULKAN_INTERNAL_DestroyColorTarget(
			renderer,
			renderer->submittedColorTargetsToDestroy[i]
		);
	}
	renderer->submittedColorTargetsToDestroyCount = 0;

	for (i = 0; i < renderer->submittedDepthStencilTargetsToDestroyCount; i += 1)
	{
		VULKAN_INTERNAL_DestroyDepthStencilTarget(
			renderer,
			renderer->submittedDepthStencilTargetsToDestroy[i]
		);
	}
	renderer->submittedDepthStencilTargetsToDestroyCount = 0;

	for (i = 0; i < renderer->submittedTexturesToDestroyCount; i += 1)
	{
		VULKAN_INTERNAL_DestroyTexture(
			renderer,
			renderer->submittedTexturesToDestroy[i]
		);
	}
	renderer->submittedTexturesToDestroyCount = 0;

	for (i = 0; i < renderer->submittedBuffersToDestroyCount; i += 1)
	{
		VULKAN_INTERNAL_DestroyBuffer(
			renderer,
			renderer->submittedBuffersToDestroy[i]
		);
	}
	renderer->submittedBuffersToDestroyCount = 0;

	for (i = 0; i < renderer->submittedGraphicsPipelinesToDestroyCount; i += 1)
	{
		VULKAN_INTERNAL_DestroyGraphicsPipeline(
			renderer,
			renderer->submittedGraphicsPipelinesToDestroy[i]
		);
	}
	renderer->submittedGraphicsPipelinesToDestroyCount = 0;

	for (i = 0; i < renderer->submittedComputePipelinesToDestroyCount; i += 1)
	{
		VULKAN_INTERNAL_DestroyComputePipeline(
			renderer,
			renderer->submittedComputePipelinesToDestroy[i]
		);
	}
	renderer->submittedComputePipelinesToDestroyCount = 0;

	for (i = 0; i < renderer->submittedShaderModulesToDestroyCount; i += 1)
	{
		VULKAN_INTERNAL_DestroyShaderModule(
			renderer,
			renderer->submittedShaderModulesToDestroy[i]
		);
	}
	renderer->submittedShaderModulesToDestroyCount = 0;

	for (i = 0; i < renderer->submittedSamplersToDestroyCount; i += 1)
	{
		VULKAN_INTERNAL_DestroySampler(
			renderer,
			renderer->submittedSamplersToDestroy[i]
		);
	}
	renderer->submittedSamplersToDestroyCount = 0;

	for (i = 0; i < renderer->submittedFramebuffersToDestroyCount; i += 1)
	{
		VULKAN_INTERNAL_DestroyFramebuffer(
			renderer,
			renderer->submittedFramebuffersToDestroy[i]
		);
	}
	renderer->submittedFramebuffersToDestroyCount = 0;

	for (i = 0; i < renderer->submittedRenderPassesToDestroyCount; i += 1)
	{
		VULKAN_INTERNAL_DestroyRenderPass(
			renderer,
			renderer->submittedRenderPassesToDestroy[i]
		);
	}
	renderer->submittedRenderPassesToDestroyCount = 0;

	/* Re-size submitted destroy lists */

	EXPAND_ARRAY_IF_NEEDED(
		renderer->submittedColorTargetsToDestroy,
		VulkanColorTarget*,
		renderer->colorTargetsToDestroyCount,
		renderer->submittedColorTargetsToDestroyCapacity,
		renderer->colorTargetsToDestroyCount
	)

	EXPAND_ARRAY_IF_NEEDED(
		renderer->submittedDepthStencilTargetsToDestroy,
		VulkanDepthStencilTarget*,
		renderer->depthStencilTargetsToDestroyCount,
		renderer->submittedDepthStencilTargetsToDestroyCapacity,
		renderer->depthStencilTargetsToDestroyCount
	)

	EXPAND_ARRAY_IF_NEEDED(
		renderer->submittedTexturesToDestroy,
		VulkanTexture*,
		renderer->texturesToDestroyCount,
		renderer->submittedTexturesToDestroyCapacity,
		renderer->texturesToDestroyCount
	)

	EXPAND_ARRAY_IF_NEEDED(
		renderer->submittedBuffersToDestroy,
		VulkanBuffer*,
		renderer->buffersToDestroyCount,
		renderer->submittedBuffersToDestroyCapacity,
		renderer->buffersToDestroyCount
	)

	EXPAND_ARRAY_IF_NEEDED(
		renderer->submittedGraphicsPipelinesToDestroy,
		VulkanGraphicsPipeline*,
		renderer->graphicsPipelinesToDestroyCount,
		renderer->submittedGraphicsPipelinesToDestroyCapacity,
		renderer->graphicsPipelinesToDestroyCount
	)

	EXPAND_ARRAY_IF_NEEDED(
		renderer->submittedComputePipelinesToDestroy,
		VulkanComputePipeline*,
		renderer->computePipelinesToDestroyCount,
		renderer->submittedComputePipelinesToDestroyCapacity,
		renderer->computePipelinesToDestroyCount
	)

	EXPAND_ARRAY_IF_NEEDED(
		renderer->submittedShaderModulesToDestroy,
		VkShaderModule,
		renderer->shaderModulesToDestroyCount,
		renderer->submittedShaderModulesToDestroyCapacity,
		renderer->shaderModulesToDestroyCount
	)

	EXPAND_ARRAY_IF_NEEDED(
		renderer->submittedSamplersToDestroy,
		VkSampler,
		renderer->samplersToDestroyCount,
		renderer->submittedSamplersToDestroyCapacity,
		renderer->samplersToDestroyCount
	)

	EXPAND_ARRAY_IF_NEEDED(
		renderer->submittedFramebuffersToDestroy,
		VulkanFramebuffer*,
		renderer->framebuffersToDestroyCount,
		renderer->submittedFramebuffersToDestroyCapacity,
		renderer->framebuffersToDestroyCount
	)

	EXPAND_ARRAY_IF_NEEDED(
		renderer->submittedRenderPassesToDestroy,
		VkRenderPass,
		renderer->renderPassesToDestroyCount,
		renderer->submittedRenderPassesToDestroyCapacity,
		renderer->renderPassesToDestroyCount
	)

	/* Rotate destroy lists */

	MOVE_ARRAY_CONTENTS_AND_RESET(
		i,
		renderer->submittedColorTargetsToDestroy,
		renderer->submittedColorTargetsToDestroyCount,
		renderer->colorTargetsToDestroy,
		renderer->colorTargetsToDestroyCount
	)

	MOVE_ARRAY_CONTENTS_AND_RESET(
		i,
		renderer->submittedDepthStencilTargetsToDestroy,
		renderer->submittedDepthStencilTargetsToDestroyCount,
		renderer->depthStencilTargetsToDestroy,
		renderer->depthStencilTargetsToDestroyCount
	)

	MOVE_ARRAY_CONTENTS_AND_RESET(
		i,
		renderer->submittedTexturesToDestroy,
		renderer->submittedTexturesToDestroyCount,
		renderer->texturesToDestroy,
		renderer->texturesToDestroyCount
	)

	MOVE_ARRAY_CONTENTS_AND_RESET(
		i,
		renderer->submittedBuffersToDestroy,
		renderer->submittedBuffersToDestroyCount,
		renderer->buffersToDestroy,
		renderer->buffersToDestroyCount
	)

	MOVE_ARRAY_CONTENTS_AND_RESET(
		i,
		renderer->submittedGraphicsPipelinesToDestroy,
		renderer->submittedGraphicsPipelinesToDestroyCount,
		renderer->graphicsPipelinesToDestroy,
		renderer->graphicsPipelinesToDestroyCount
	)

	MOVE_ARRAY_CONTENTS_AND_RESET(
		i,
		renderer->submittedComputePipelinesToDestroy,
		renderer->submittedComputePipelinesToDestroyCount,
		renderer->computePipelinesToDestroy,
		renderer->computePipelinesToDestroyCount
	)

	MOVE_ARRAY_CONTENTS_AND_RESET(
		i,
		renderer->submittedShaderModulesToDestroy,
		renderer->submittedShaderModulesToDestroyCount,
		renderer->shaderModulesToDestroy,
		renderer->shaderModulesToDestroyCount
	)

	MOVE_ARRAY_CONTENTS_AND_RESET(
		i,
		renderer->submittedSamplersToDestroy,
		renderer->submittedSamplersToDestroyCount,
		renderer->samplersToDestroy,
		renderer->samplersToDestroyCount
	)

	MOVE_ARRAY_CONTENTS_AND_RESET(
		i,
		renderer->submittedFramebuffersToDestroy,
		renderer->submittedFramebuffersToDestroyCount,
		renderer->framebuffersToDestroy,
		renderer->framebuffersToDestroyCount
	)

	MOVE_ARRAY_CONTENTS_AND_RESET(
		i,
		renderer->submittedRenderPassesToDestroy,
		renderer->submittedRenderPassesToDestroyCount,
		renderer->renderPassesToDestroy,
		renderer->renderPassesToDestroyCount
	)

	SDL_UnlockMutex(renderer->disposeLock);

	/* Increment the frame index */
	/* FIXME: need a better name, and to get rid of the magic value % 2 */
	renderer->frameIndex = (renderer->frameIndex + 1) % 2;

	/* Mark sub buffers of previously submitted buffers as unbound */
	for (i = 0; i < renderer->submittedBufferCount; i += 1)
	{
		if (renderer->submittedBuffers[i] != NULL)
		{
			renderer->submittedBuffers[i]->boundSubmitted = 0;

			for (j = 0; j < renderer->submittedBuffers[i]->subBufferCount; j += 1)
			{
				if (renderer->submittedBuffers[i]->subBuffers[j]->bound == renderer->frameIndex)
				{
					renderer->submittedBuffers[i]->subBuffers[j]->bound = -1;
				}
			}

			renderer->submittedBuffers[i] = NULL;
		}
	}

	renderer->submittedBufferCount = 0;

	/* Mark currently bound buffers as submitted buffers */
	if (renderer->buffersInUseCount > renderer->submittedBufferCapacity)
	{
		renderer->submittedBuffers = SDL_realloc(
			renderer->submittedBuffers,
			sizeof(VulkanBuffer*) * renderer->buffersInUseCount
		);

		renderer->submittedBufferCapacity = renderer->buffersInUseCount;
	}

	for (i = 0; i < renderer->buffersInUseCount; i += 1)
	{
		if (renderer->buffersInUse[i] != NULL)
		{
			renderer->buffersInUse[i]->bound = 0;
			renderer->buffersInUse[i]->boundSubmitted = 1;

			renderer->submittedBuffers[i] = renderer->buffersInUse[i];
			renderer->buffersInUse[i] = NULL;
		}
	}

	renderer->submittedBufferCount = renderer->buffersInUseCount;
	renderer->buffersInUseCount = 0;
}

/* Swapchain */

static inline VkExtent2D VULKAN_INTERNAL_ChooseSwapExtent(
	void* windowHandle,
	const VkSurfaceCapabilitiesKHR capabilities
) {
	VkExtent2D actualExtent;
	int32_t drawableWidth, drawableHeight;

	if (capabilities.currentExtent.width != UINT32_MAX)
	{
		return capabilities.currentExtent;
	}
	else
	{
		SDL_Vulkan_GetDrawableSize(
			(SDL_Window*) windowHandle,
			&drawableWidth,
			&drawableHeight
		);

		actualExtent.width = drawableWidth;
		actualExtent.height = drawableHeight;

		return actualExtent;
	}
}

static uint8_t VULKAN_INTERNAL_QuerySwapChainSupport(
	VulkanRenderer *renderer,
	VkPhysicalDevice physicalDevice,
	VkSurfaceKHR surface,
	SwapChainSupportDetails *outputDetails
) {
	VkResult result;
	uint32_t formatCount;
	uint32_t presentModeCount;

	result = renderer->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
		physicalDevice,
		surface,
		&outputDetails->capabilities
	);
	if (result != VK_SUCCESS)
	{
		Refresh_LogError(
			"vkGetPhysicalDeviceSurfaceCapabilitiesKHR: %s",
			VkErrorMessages(result)
		);

		return 0;
	}

	renderer->vkGetPhysicalDeviceSurfaceFormatsKHR(
		physicalDevice,
		surface,
		&formatCount,
		NULL
	);

	if (formatCount != 0)
	{
		outputDetails->formats = (VkSurfaceFormatKHR*) SDL_malloc(
			sizeof(VkSurfaceFormatKHR) * formatCount
		);
		outputDetails->formatsLength = formatCount;

		if (!outputDetails->formats)
		{
			SDL_OutOfMemory();
			return 0;
		}

		result = renderer->vkGetPhysicalDeviceSurfaceFormatsKHR(
			physicalDevice,
			surface,
			&formatCount,
			outputDetails->formats
		);
		if (result != VK_SUCCESS)
		{
			Refresh_LogError(
				"vkGetPhysicalDeviceSurfaceFormatsKHR: %s",
				VkErrorMessages(result)
			);

			SDL_free(outputDetails->formats);
			return 0;
		}
	}

	renderer->vkGetPhysicalDeviceSurfacePresentModesKHR(
		physicalDevice,
		surface,
		&presentModeCount,
		NULL
	);

	if (presentModeCount != 0)
	{
		outputDetails->presentModes = (VkPresentModeKHR*) SDL_malloc(
			sizeof(VkPresentModeKHR) * presentModeCount
		);
		outputDetails->presentModesLength = presentModeCount;

		if (!outputDetails->presentModes)
		{
			SDL_OutOfMemory();
			return 0;
		}

		result = renderer->vkGetPhysicalDeviceSurfacePresentModesKHR(
			physicalDevice,
			surface,
			&presentModeCount,
			outputDetails->presentModes
		);
		if (result != VK_SUCCESS)
		{
			Refresh_LogError(
				"vkGetPhysicalDeviceSurfacePresentModesKHR: %s",
				VkErrorMessages(result)
			);

			SDL_free(outputDetails->formats);
			SDL_free(outputDetails->presentModes);
			return 0;
		}
	}

	return 1;
}

static uint8_t VULKAN_INTERNAL_ChooseSwapSurfaceFormat(
	VkFormat desiredFormat,
	VkSurfaceFormatKHR *availableFormats,
	uint32_t availableFormatsLength,
	VkSurfaceFormatKHR *outputFormat
) {
	uint32_t i;
	for (i = 0; i < availableFormatsLength; i += 1)
	{
		if (	availableFormats[i].format == desiredFormat &&
			availableFormats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR	)
		{
			*outputFormat = availableFormats[i];
			return 1;
		}
	}

	Refresh_LogError("Desired surface format is unavailable.");
	return 0;
}

static uint8_t VULKAN_INTERNAL_ChooseSwapPresentMode(
	Refresh_PresentMode desiredPresentInterval,
	VkPresentModeKHR *availablePresentModes,
	uint32_t availablePresentModesLength,
	VkPresentModeKHR *outputPresentMode
) {
	#define CHECK_MODE(m) \
		for (i = 0; i < availablePresentModesLength; i += 1) \
		{ \
			if (availablePresentModes[i] == m) \
			{ \
				*outputPresentMode = m; \
				Refresh_LogInfo("Using " #m "!"); \
				return 1; \
			} \
		} \
		Refresh_LogInfo(#m " unsupported.");

	uint32_t i;
    if (desiredPresentInterval == REFRESH_PRESENTMODE_IMMEDIATE)
	{
		CHECK_MODE(VK_PRESENT_MODE_IMMEDIATE_KHR)
	}
    else if (desiredPresentInterval == REFRESH_PRESENTMODE_MAILBOX)
    {
        CHECK_MODE(VK_PRESENT_MODE_MAILBOX_KHR)
    }
    else if (desiredPresentInterval == REFRESH_PRESENTMODE_FIFO)
    {
        CHECK_MODE(VK_PRESENT_MODE_FIFO_KHR)
    }
    else if (desiredPresentInterval == REFRESH_PRESENTMODE_FIFO_RELAXED)
    {
        CHECK_MODE(VK_PRESENT_MODE_FIFO_RELAXED_KHR)
    }
	else
	{
		Refresh_LogError(
			"Unrecognized PresentInterval: %d",
			desiredPresentInterval
		);
		return 0;
	}

	#undef CHECK_MODE

	Refresh_LogInfo("Fall back to VK_PRESENT_MODE_FIFO_KHR.");
	*outputPresentMode = VK_PRESENT_MODE_FIFO_KHR;
	return 1;
}

static CreateSwapchainResult VULKAN_INTERNAL_CreateSwapchain(
	VulkanRenderer *renderer
) {
	VkResult vulkanResult;
	SwapChainSupportDetails swapChainSupportDetails;
	VkSurfaceFormatKHR surfaceFormat;
	VkPresentModeKHR presentMode;
	VkExtent2D extent;
	uint32_t imageCount, swapChainImageCount, i;
	VkSwapchainCreateInfoKHR swapChainCreateInfo;
	VkImage *swapChainImages;
	VkImageViewCreateInfo createInfo;
	VkImageView swapChainImageView;

	if (!VULKAN_INTERNAL_QuerySwapChainSupport(
		renderer,
		renderer->physicalDevice,
		renderer->surface,
		&swapChainSupportDetails
	)) {
		Refresh_LogError("Device does not support swap chain creation");
		return CREATE_SWAPCHAIN_FAIL;
	}

	renderer->swapChainFormat = VK_FORMAT_B8G8R8A8_UNORM;
	renderer->swapChainSwizzle.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	renderer->swapChainSwizzle.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	renderer->swapChainSwizzle.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	renderer->swapChainSwizzle.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	if (!VULKAN_INTERNAL_ChooseSwapSurfaceFormat(
		renderer->swapChainFormat,
		swapChainSupportDetails.formats,
		swapChainSupportDetails.formatsLength,
		&surfaceFormat
	)) {
		SDL_free(swapChainSupportDetails.formats);
		SDL_free(swapChainSupportDetails.presentModes);
		Refresh_LogError("Device does not support swap chain format");
		return CREATE_SWAPCHAIN_FAIL;
	}

	if (!VULKAN_INTERNAL_ChooseSwapPresentMode(
		renderer->presentMode,
		swapChainSupportDetails.presentModes,
		swapChainSupportDetails.presentModesLength,
		&presentMode
	)) {
		SDL_free(swapChainSupportDetails.formats);
		SDL_free(swapChainSupportDetails.presentModes);
		Refresh_LogError("Device does not support swap chain present mode");
		return CREATE_SWAPCHAIN_FAIL;
	}

	extent = VULKAN_INTERNAL_ChooseSwapExtent(
		renderer->deviceWindowHandle,
		swapChainSupportDetails.capabilities
	);

	if (extent.width == 0 || extent.height == 0)
	{
		return CREATE_SWAPCHAIN_SURFACE_ZERO;
	}

	imageCount = swapChainSupportDetails.capabilities.minImageCount + 1;

	if (	swapChainSupportDetails.capabilities.maxImageCount > 0 &&
		imageCount > swapChainSupportDetails.capabilities.maxImageCount	)
	{
		imageCount = swapChainSupportDetails.capabilities.maxImageCount;
	}

	if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR)
	{
		/* Required for proper triple-buffering.
		 * Note that this is below the above maxImageCount check!
		 * If the driver advertises MAILBOX but does not support 3 swap
		 * images, it's not real mailbox support, so let it fail hard.
		 * -flibit
		 */
		imageCount = SDL_max(imageCount, 3);
	}

	swapChainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapChainCreateInfo.pNext = NULL;
	swapChainCreateInfo.flags = 0;
	swapChainCreateInfo.surface = renderer->surface;
	swapChainCreateInfo.minImageCount = imageCount;
	swapChainCreateInfo.imageFormat = surfaceFormat.format;
	swapChainCreateInfo.imageColorSpace = surfaceFormat.colorSpace;
	swapChainCreateInfo.imageExtent = extent;
	swapChainCreateInfo.imageArrayLayers = 1;
	swapChainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapChainCreateInfo.queueFamilyIndexCount = 0;
	swapChainCreateInfo.pQueueFamilyIndices = NULL;
	swapChainCreateInfo.preTransform = swapChainSupportDetails.capabilities.currentTransform;
	swapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapChainCreateInfo.presentMode = presentMode;
	swapChainCreateInfo.clipped = VK_TRUE;
	swapChainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

	vulkanResult = renderer->vkCreateSwapchainKHR(
		renderer->logicalDevice,
		&swapChainCreateInfo,
		NULL,
		&renderer->swapChain
	);

	SDL_free(swapChainSupportDetails.formats);
	SDL_free(swapChainSupportDetails.presentModes);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateSwapchainKHR", vulkanResult);

		return CREATE_SWAPCHAIN_FAIL;
	}

	renderer->vkGetSwapchainImagesKHR(
		renderer->logicalDevice,
		renderer->swapChain,
		&swapChainImageCount,
		NULL
	);

	renderer->swapChainImages = (VkImage*) SDL_malloc(
		sizeof(VkImage) * swapChainImageCount
	);
	if (!renderer->swapChainImages)
	{
		SDL_OutOfMemory();
		return CREATE_SWAPCHAIN_FAIL;
	}

	renderer->swapChainImageViews = (VkImageView*) SDL_malloc(
		sizeof(VkImageView) * swapChainImageCount
	);
	if (!renderer->swapChainImageViews)
	{
		SDL_OutOfMemory();
		return CREATE_SWAPCHAIN_FAIL;
	}

	renderer->swapChainResourceAccessTypes = (VulkanResourceAccessType*) SDL_malloc(
		sizeof(VulkanResourceAccessType) * swapChainImageCount
	);
	if (!renderer->swapChainResourceAccessTypes)
	{
		SDL_OutOfMemory();
		return CREATE_SWAPCHAIN_FAIL;
	}

	renderer->swapChainQueueFamilyIndices = (uint32_t*) SDL_malloc(
		sizeof(uint32_t) * swapChainImageCount
	);
	if (!renderer->swapChainQueueFamilyIndices)
	{
		SDL_OutOfMemory();
		return CREATE_SWAPCHAIN_FAIL;
	}

	swapChainImages = SDL_stack_alloc(VkImage, swapChainImageCount);
	renderer->vkGetSwapchainImagesKHR(
		renderer->logicalDevice,
		renderer->swapChain,
		&swapChainImageCount,
		swapChainImages
	);
	renderer->swapChainImageCount = swapChainImageCount;
	renderer->swapChainExtent = extent;

	createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	createInfo.pNext = NULL;
	createInfo.flags = 0;
	createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	createInfo.format = surfaceFormat.format;
	createInfo.components = renderer->swapChainSwizzle;
	createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	createInfo.subresourceRange.baseMipLevel = 0;
	createInfo.subresourceRange.levelCount = 1;
	createInfo.subresourceRange.baseArrayLayer = 0;
	createInfo.subresourceRange.layerCount = 1;
	for (i = 0; i < swapChainImageCount; i += 1)
	{
		createInfo.image = swapChainImages[i];

		vulkanResult = renderer->vkCreateImageView(
			renderer->logicalDevice,
			&createInfo,
			NULL,
			&swapChainImageView
		);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkCreateImageView", vulkanResult);
			SDL_stack_free(swapChainImages);
			return CREATE_SWAPCHAIN_FAIL;
		}

		renderer->swapChainImages[i] = swapChainImages[i];
		renderer->swapChainImageViews[i] = swapChainImageView;
		renderer->swapChainResourceAccessTypes[i] = RESOURCE_ACCESS_NONE;
		renderer->swapChainQueueFamilyIndices[i] = renderer->queueFamilyIndices.graphicsFamily;
	}

	SDL_stack_free(swapChainImages);
	return CREATE_SWAPCHAIN_SUCCESS;
}

static void VULKAN_INTERNAL_RecreateSwapchain(VulkanRenderer* renderer)
{
	CreateSwapchainResult createSwapchainResult;
	SwapChainSupportDetails swapChainSupportDetails;
	VkExtent2D extent;

	renderer->vkDeviceWaitIdle(renderer->logicalDevice);

	VULKAN_INTERNAL_QuerySwapChainSupport(
		renderer,
		renderer->physicalDevice,
		renderer->surface,
		&swapChainSupportDetails
	);

	extent = VULKAN_INTERNAL_ChooseSwapExtent(
		renderer->deviceWindowHandle,
		swapChainSupportDetails.capabilities
	);

	if (extent.width == 0 || extent.height == 0)
	{
		return;
	}

	VULKAN_INTERNAL_DestroySwapchain(renderer);
	createSwapchainResult = VULKAN_INTERNAL_CreateSwapchain(renderer);

	if (createSwapchainResult == CREATE_SWAPCHAIN_FAIL)
	{
		Refresh_LogError("Failed to recreate swapchain");
		return;
	}

	renderer->vkDeviceWaitIdle(renderer->logicalDevice);
}

/* Data Buffer */

/* buffer should be an alloc'd but uninitialized VulkanTexture */
static uint8_t VULKAN_INTERNAL_CreateBuffer(
	VulkanRenderer *renderer,
	VkDeviceSize size,
	VulkanResourceAccessType resourceAccessType,
	VkBufferUsageFlags usage,
	uint32_t subBufferCount,
	VulkanBuffer *buffer
) {
	VkResult vulkanResult;
	VkBufferCreateInfo bufferCreateInfo;
	uint8_t findMemoryResult;
	uint32_t i;

	buffer->size = size;
	buffer->currentSubBufferIndex = 0;
	buffer->bound = 0;
	buffer->boundSubmitted = 0;
	buffer->resourceAccessType = resourceAccessType;
	buffer->usage = usage;
	buffer->subBufferCount = subBufferCount;
	buffer->subBuffers = SDL_malloc(
		sizeof(VulkanSubBuffer*) * buffer->subBufferCount
	);

	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.pNext = NULL;
	bufferCreateInfo.flags = 0;
	bufferCreateInfo.size = size;
	bufferCreateInfo.usage = usage;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	bufferCreateInfo.queueFamilyIndexCount = 1;
	bufferCreateInfo.pQueueFamilyIndices = &renderer->queueFamilyIndices.graphicsFamily;

	for (i = 0; i < subBufferCount; i += 1)
	{
		buffer->subBuffers[i] = SDL_malloc(
			sizeof(VulkanSubBuffer) * buffer->subBufferCount
		);

		vulkanResult = renderer->vkCreateBuffer(
			renderer->logicalDevice,
			&bufferCreateInfo,
			NULL,
			&buffer->subBuffers[i]->buffer
		);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkCreateBuffer", vulkanResult);
			Refresh_LogError("Failed to create VkBuffer");
			return 0;
		}

		findMemoryResult = VULKAN_INTERNAL_FindAvailableMemory(
			renderer,
			buffer->subBuffers[i]->buffer,
			VK_NULL_HANDLE,
			&buffer->subBuffers[i]->allocation,
			&buffer->subBuffers[i]->offset,
			&buffer->subBuffers[i]->size
		);

		/* We're out of available memory */
		if (findMemoryResult == 2)
		{
			Refresh_LogWarn("Out of buffer memory!");
			return 2;
		}
		else if (findMemoryResult == 0)
		{
			Refresh_LogError("Failed to find buffer memory!");
			return 0;
		}

		SDL_LockMutex(buffer->subBuffers[i]->allocation->memoryLock);

		vulkanResult = renderer->vkBindBufferMemory(
			renderer->logicalDevice,
			buffer->subBuffers[i]->buffer,
			buffer->subBuffers[i]->allocation->memory,
			buffer->subBuffers[i]->offset
		);

		SDL_UnlockMutex(buffer->subBuffers[i]->allocation->memoryLock);

		if (vulkanResult != VK_SUCCESS)
		{
			Refresh_LogError("Failed to bind buffer memory!");
			return 0;
		}

		buffer->subBuffers[i]->resourceAccessType = resourceAccessType;
		buffer->subBuffers[i]->bound = -1;
	}

	return 1;
}

/* Command Buffers */

static void VULKAN_INTERNAL_BeginCommandBuffer(
	VulkanRenderer *renderer,
	VulkanCommandBuffer *commandBuffer
) {
	VkCommandBufferBeginInfo beginInfo;
	VkResult result;

	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.pNext = NULL;
	beginInfo.flags = 0;
	beginInfo.pInheritanceInfo = NULL;

	if (!commandBuffer->fixed)
	{
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	}

	result = renderer->vkBeginCommandBuffer(
		commandBuffer->commandBuffer,
		&beginInfo
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkBeginCommandBuffer", result);
	}
}

static void VULKAN_INTERNAL_EndCommandBuffer(
	VulkanRenderer* renderer,
	VulkanCommandBuffer *commandBuffer
) {
	VkResult result;

	result = renderer->vkEndCommandBuffer(
		commandBuffer->commandBuffer
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkEndCommandBuffer", result);
	}

	commandBuffer->currentComputePipeline = NULL;
	commandBuffer->boundComputeBufferCount = 0;
}

/* Public API */

static void VULKAN_DestroyDevice(
    Refresh_Device *device
) {
	VulkanRenderer* renderer = (VulkanRenderer*) device->driverData;
	VkResult waitResult;
	CommandPoolHashArray commandPoolHashArray;
	GraphicsPipelineLayoutHashArray graphicsPipelineLayoutHashArray;
	ComputePipelineLayoutHashArray computePipelineLayoutHashArray;
	VulkanMemorySubAllocator *allocator;
	uint32_t i, j, k;

	waitResult = renderer->vkDeviceWaitIdle(renderer->logicalDevice);

	if (waitResult != VK_SUCCESS)
	{
		LogVulkanResult("vkDeviceWaitIdle", waitResult);
	}

	VULKAN_INTERNAL_DestroyBuffer(renderer, renderer->dummyVertexUniformBuffer);
	VULKAN_INTERNAL_DestroyBuffer(renderer, renderer->dummyFragmentUniformBuffer);
	VULKAN_INTERNAL_DestroyBuffer(renderer, renderer->dummyComputeUniformBuffer);
	VULKAN_INTERNAL_DestroyBuffer(renderer, renderer->vertexUBO);
	VULKAN_INTERNAL_DestroyBuffer(renderer, renderer->fragmentUBO);
	VULKAN_INTERNAL_DestroyBuffer(renderer, renderer->computeUBO);

	/* We have to do this twice so the rotation happens correctly */
	VULKAN_INTERNAL_PostWorkCleanup(renderer);
	VULKAN_INTERNAL_PostWorkCleanup(renderer);

	VULKAN_INTERNAL_DestroyTextureStagingBuffer(renderer);

	renderer->vkDestroySemaphore(
		renderer->logicalDevice,
		renderer->transferFinishedSemaphore,
		NULL
	);

	renderer->vkDestroySemaphore(
		renderer->logicalDevice,
		renderer->imageAvailableSemaphore,
		NULL
	);

	renderer->vkDestroySemaphore(
		renderer->logicalDevice,
		renderer->renderFinishedSemaphore,
		NULL
	);

	renderer->vkDestroyFence(
		renderer->logicalDevice,
		renderer->inFlightFence,
		NULL
	);

	for (i = 0; i < NUM_COMMAND_POOL_BUCKETS; i += 1)
	{
		commandPoolHashArray = renderer->commandPoolHashTable.buckets[i];
		for (j = 0; j < commandPoolHashArray.count; j += 1)
		{
			VULKAN_INTERNAL_DestroyCommandPool(
				renderer,
				commandPoolHashArray.elements[j].value
			);
		}

		if (commandPoolHashArray.elements != NULL)
		{
			SDL_free(commandPoolHashArray.elements);
		}
	}

	renderer->vkDestroyCommandPool(
		renderer->logicalDevice,
		renderer->transferCommandPool,
		NULL
	);

	for (i = 0; i < NUM_PIPELINE_LAYOUT_BUCKETS; i += 1)
	{
		graphicsPipelineLayoutHashArray = renderer->graphicsPipelineLayoutHashTable.buckets[i];
		for (j = 0; j < graphicsPipelineLayoutHashArray.count; j += 1)
		{
			VULKAN_INTERNAL_DestroyImageDescriptorSetCache(
				renderer,
				graphicsPipelineLayoutHashArray.elements[j].value->vertexSamplerDescriptorSetCache
			);

			VULKAN_INTERNAL_DestroyImageDescriptorSetCache(
				renderer,
				graphicsPipelineLayoutHashArray.elements[j].value->fragmentSamplerDescriptorSetCache
			);

			renderer->vkDestroyPipelineLayout(
				renderer->logicalDevice,
				graphicsPipelineLayoutHashArray.elements[j].value->pipelineLayout,
				NULL
			);
		}

		if (graphicsPipelineLayoutHashArray.elements != NULL)
		{
			SDL_free(graphicsPipelineLayoutHashArray.elements);
		}

		computePipelineLayoutHashArray = renderer->computePipelineLayoutHashTable.buckets[i];
		for (j = 0; j < computePipelineLayoutHashArray.count; j += 1)
		{
			VULKAN_INTERNAL_DestroyBufferDescriptorSetCache(
				renderer,
				computePipelineLayoutHashArray.elements[j].value->bufferDescriptorSetCache
			);

			VULKAN_INTERNAL_DestroyImageDescriptorSetCache(
				renderer,
				computePipelineLayoutHashArray.elements[j].value->imageDescriptorSetCache
			);

			renderer->vkDestroyPipelineLayout(
				renderer->logicalDevice,
				computePipelineLayoutHashArray.elements[j].value->pipelineLayout,
				NULL
			);
		}

		if (computePipelineLayoutHashArray.elements != NULL)
		{
			SDL_free(computePipelineLayoutHashArray.elements);
		}
	}

	renderer->vkDestroyDescriptorPool(
		renderer->logicalDevice,
		renderer->defaultDescriptorPool,
		NULL
	);

	for (i = 0; i < NUM_DESCRIPTOR_SET_LAYOUT_BUCKETS; i += 1)
	{
		for (j = 0; j < renderer->descriptorSetLayoutHashTable.buckets[i].count; j += 1)
		{
			renderer->vkDestroyDescriptorSetLayout(
				renderer->logicalDevice,
				renderer->descriptorSetLayoutHashTable.buckets[i].elements[j].value,
				NULL
			);
		}

		SDL_free(renderer->descriptorSetLayoutHashTable.buckets[i].elements);
	}

	renderer->vkDestroyDescriptorSetLayout(
		renderer->logicalDevice,
		renderer->emptyVertexSamplerLayout,
		NULL
	);

	renderer->vkDestroyDescriptorSetLayout(
		renderer->logicalDevice,
		renderer->emptyFragmentSamplerLayout,
		NULL
	);

	renderer->vkDestroyDescriptorSetLayout(
		renderer->logicalDevice,
		renderer->emptyComputeBufferDescriptorSetLayout,
		NULL
	);

	renderer->vkDestroyDescriptorSetLayout(
		renderer->logicalDevice,
		renderer->emptyComputeImageDescriptorSetLayout,
		NULL
	);

	renderer->vkDestroyDescriptorSetLayout(
		renderer->logicalDevice,
		renderer->vertexParamLayout,
		NULL
	);

	renderer->vkDestroyDescriptorSetLayout(
		renderer->logicalDevice,
		renderer->fragmentParamLayout,
		NULL
	);

	renderer->vkDestroyDescriptorSetLayout(
		renderer->logicalDevice,
		renderer->computeParamLayout,
		NULL
	);

	VULKAN_INTERNAL_DestroySwapchain(renderer);

	if (!renderer->headless)
	{
		renderer->vkDestroySurfaceKHR(
			renderer->instance,
			renderer->surface,
			NULL
		);
	}

	for (i = 0; i < VK_MAX_MEMORY_TYPES; i += 1)
	{
		allocator = &renderer->memoryAllocator->subAllocators[i];

		for (j = 0; j < allocator->allocationCount; j += 1)
		{
			for (k = 0; k < allocator->allocations[j]->freeRegionCount; k += 1)
			{
				SDL_free(allocator->allocations[j]->freeRegions[k]);
			}

			SDL_free(allocator->allocations[j]->freeRegions);

			renderer->vkFreeMemory(
				renderer->logicalDevice,
				allocator->allocations[j]->memory,
				NULL
			);

			SDL_DestroyMutex(allocator->allocations[j]->memoryLock);
			SDL_free(allocator->allocations[j]);
		}

		SDL_free(allocator->allocations);
		SDL_free(allocator->sortedFreeRegions);
	}

	SDL_free(renderer->memoryAllocator);

	SDL_DestroyMutex(renderer->allocatorLock);
	SDL_DestroyMutex(renderer->disposeLock);
	SDL_DestroyMutex(renderer->uniformBufferLock);
	SDL_DestroyMutex(renderer->descriptorSetLock);
	SDL_DestroyMutex(renderer->boundBufferLock);
	SDL_DestroyMutex(renderer->stagingLock);

	SDL_free(renderer->buffersInUse);

	if (!renderer->usesExternalDevice)
	{
		renderer->vkDestroyDevice(renderer->logicalDevice, NULL);
		renderer->vkDestroyInstance(renderer->instance, NULL);
	}

	SDL_free(renderer);
	SDL_free(device);
}

static void VULKAN_Clear(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Rect *clearRect,
	Refresh_ClearOptions options,
	Refresh_Color *colors,
	uint32_t colorCount,
	float depth,
	int32_t stencil
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;

	uint32_t attachmentCount, i;
	VkClearAttachment clearAttachments[MAX_COLOR_TARGET_BINDINGS + 1];
	VkClearRect vulkanClearRect;
	VkClearValue clearValues[4];

	uint8_t shouldClearColor = options & REFRESH_CLEAROPTIONS_COLOR;
	uint8_t shouldClearDepth = options & REFRESH_CLEAROPTIONS_DEPTH;
	uint8_t shouldClearStencil = options & REFRESH_CLEAROPTIONS_STENCIL;

	uint8_t shouldClearDepthStencil = (
		(shouldClearDepth || shouldClearStencil) &&
		vulkanCommandBuffer->currentFramebuffer->depthStencilTarget != NULL
	);

	if (!shouldClearColor && !shouldClearDepthStencil)
	{
		return;
	}

	vulkanClearRect.baseArrayLayer = 0;
	vulkanClearRect.layerCount = 1;
	vulkanClearRect.rect.offset.x = clearRect->x;
	vulkanClearRect.rect.offset.y = clearRect->y;
	vulkanClearRect.rect.extent.width = clearRect->w;
	vulkanClearRect.rect.extent.height = clearRect->h;

	attachmentCount = 0;

	if (shouldClearColor)
	{
		for (i = 0; i < colorCount; i += 1)
		{
			clearValues[i].color.float32[0] = colors[i].r / 255.0f;
			clearValues[i].color.float32[1] = colors[i].g / 255.0f;
			clearValues[i].color.float32[2] = colors[i].b / 255.0f;
			clearValues[i].color.float32[3] = colors[i].a / 255.0f;
		}

		for (i = 0; i < colorCount; i += 1)
		{
			clearAttachments[attachmentCount].aspectMask =
				VK_IMAGE_ASPECT_COLOR_BIT;
			clearAttachments[attachmentCount].colorAttachment =
				attachmentCount;
			clearAttachments[attachmentCount].clearValue =
				clearValues[attachmentCount];
			attachmentCount += 1;

			/* Do NOT clear the multisample image here!
			 * Vulkan treats them both as the same color attachment.
			 * Vulkan is a very good and not confusing at all API.
			 */
		}
	}

	if (shouldClearDepthStencil)
	{
		clearAttachments[attachmentCount].aspectMask = 0;
		clearAttachments[attachmentCount].colorAttachment = 0;

		if (shouldClearDepth)
		{
			if (depth < 0.0f)
			{
				depth = 0.0f;
			}
			else if (depth > 1.0f)
			{
				depth = 1.0f;
			}
			clearAttachments[attachmentCount].aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
			clearAttachments[attachmentCount].clearValue.depthStencil.depth = depth;
		}
		else
		{
			clearAttachments[attachmentCount].clearValue.depthStencil.depth = 0.0f;
		}

		if (shouldClearStencil)
		{
			clearAttachments[attachmentCount].aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			clearAttachments[attachmentCount].clearValue.depthStencil.stencil = stencil;
		}
		else
		{
			clearAttachments[attachmentCount].clearValue.depthStencil.stencil = 0;
		}

		attachmentCount += 1;
	}

	renderer->vkCmdClearAttachments(
		vulkanCommandBuffer->commandBuffer,
		attachmentCount,
		clearAttachments,
		1,
		&vulkanClearRect
	);
}

static void VULKAN_DrawInstancedPrimitives(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	uint32_t baseVertex,
	uint32_t startIndex,
	uint32_t primitiveCount,
	uint32_t instanceCount,
	uint32_t vertexParamOffset,
	uint32_t fragmentParamOffset
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;

	VkDescriptorSet descriptorSets[4];
	uint32_t dynamicOffsets[2];

	descriptorSets[0] = vulkanCommandBuffer->currentGraphicsPipeline->vertexSamplerDescriptorSet;
	descriptorSets[1] = vulkanCommandBuffer->currentGraphicsPipeline->fragmentSamplerDescriptorSet;
	descriptorSets[2] = vulkanCommandBuffer->currentGraphicsPipeline->vertexUBODescriptorSet;
	descriptorSets[3] = vulkanCommandBuffer->currentGraphicsPipeline->fragmentUBODescriptorSet;

	dynamicOffsets[0] = vertexParamOffset;
	dynamicOffsets[1] = fragmentParamOffset;

	renderer->vkCmdBindDescriptorSets(
		vulkanCommandBuffer->commandBuffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		vulkanCommandBuffer->currentGraphicsPipeline->pipelineLayout->pipelineLayout,
		0,
		4,
		descriptorSets,
		2,
		dynamicOffsets
	);

	renderer->vkCmdDrawIndexed(
		vulkanCommandBuffer->commandBuffer,
		PrimitiveVerts(
			vulkanCommandBuffer->currentGraphicsPipeline->primitiveType,
			primitiveCount
		),
		instanceCount,
		startIndex,
		baseVertex,
		0
	);
}

static void VULKAN_DrawIndexedPrimitives(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	uint32_t baseVertex,
	uint32_t startIndex,
	uint32_t primitiveCount,
	uint32_t vertexParamOffset,
	uint32_t fragmentParamOffset
) {
	VULKAN_DrawInstancedPrimitives(
		driverData,
		commandBuffer,
		baseVertex,
		startIndex,
		primitiveCount,
		1,
		vertexParamOffset,
		fragmentParamOffset
	);
}

static void VULKAN_DrawPrimitives(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	uint32_t vertexStart,
	uint32_t primitiveCount,
	uint32_t vertexParamOffset,
	uint32_t fragmentParamOffset
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;

	VkDescriptorSet descriptorSets[4];
	uint32_t dynamicOffsets[2];

	descriptorSets[0] = vulkanCommandBuffer->currentGraphicsPipeline->vertexSamplerDescriptorSet;
	descriptorSets[1] = vulkanCommandBuffer->currentGraphicsPipeline->fragmentSamplerDescriptorSet;
	descriptorSets[2] = vulkanCommandBuffer->currentGraphicsPipeline->vertexUBODescriptorSet;
	descriptorSets[3] = vulkanCommandBuffer->currentGraphicsPipeline->fragmentUBODescriptorSet;

	dynamicOffsets[0] = vertexParamOffset;
	dynamicOffsets[1] = fragmentParamOffset;

	renderer->vkCmdBindDescriptorSets(
		vulkanCommandBuffer->commandBuffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		vulkanCommandBuffer->currentGraphicsPipeline->pipelineLayout->pipelineLayout,
		0,
		4,
		descriptorSets,
		2,
		dynamicOffsets
	);

	renderer->vkCmdDraw(
		vulkanCommandBuffer->commandBuffer,
		PrimitiveVerts(
			vulkanCommandBuffer->currentGraphicsPipeline->primitiveType,
			primitiveCount
		),
		1,
		vertexStart,
		0
	);
}

static void VULKAN_DispatchCompute(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	uint32_t groupCountX,
	uint32_t groupCountY,
	uint32_t groupCountZ,
	uint32_t computeParamOffset
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	VulkanComputePipeline *computePipeline = vulkanCommandBuffer->currentComputePipeline;

	VulkanBuffer *currentBuffer;
	VkDescriptorSet descriptorSets[3];
	uint32_t i;

	for (i = 0; i < vulkanCommandBuffer->boundComputeBufferCount; i += 1)
	{
		currentBuffer = vulkanCommandBuffer->boundComputeBuffers[i];
		VULKAN_INTERNAL_BufferMemoryBarrier(
			renderer,
			vulkanCommandBuffer->commandBuffer,
			RESOURCE_ACCESS_COMPUTE_SHADER_READ_OTHER,
			currentBuffer,
			currentBuffer->subBuffers[currentBuffer->currentSubBufferIndex]
		);
	}

	descriptorSets[0] = computePipeline->bufferDescriptorSet;
	descriptorSets[1] = computePipeline->imageDescriptorSet;
	descriptorSets[2] = computePipeline->computeUBODescriptorSet;

	renderer->vkCmdBindDescriptorSets(
		vulkanCommandBuffer->commandBuffer,
		VK_PIPELINE_BIND_POINT_COMPUTE,
		computePipeline->pipelineLayout->pipelineLayout,
		0,
		3,
		descriptorSets,
		1,
		&computeParamOffset
	);

	renderer->vkCmdDispatch(
		vulkanCommandBuffer->commandBuffer,
		groupCountX,
		groupCountY,
		groupCountZ
	);

	for (i = 0; i < vulkanCommandBuffer->boundComputeBufferCount; i += 1)
	{
		currentBuffer = vulkanCommandBuffer->boundComputeBuffers[i];
		if (currentBuffer->usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
		{
			VULKAN_INTERNAL_BufferMemoryBarrier(
				renderer,
				vulkanCommandBuffer->commandBuffer,
				RESOURCE_ACCESS_VERTEX_BUFFER,
				currentBuffer,
				currentBuffer->subBuffers[currentBuffer->currentSubBufferIndex]
			);
		}
		else if (currentBuffer->usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
		{
			VULKAN_INTERNAL_BufferMemoryBarrier(
				renderer,
				vulkanCommandBuffer->commandBuffer,
				RESOURCE_ACCESS_INDEX_BUFFER,
				currentBuffer,
				currentBuffer->subBuffers[currentBuffer->currentSubBufferIndex]
			);
		}
	}
}

static Refresh_RenderPass* VULKAN_CreateRenderPass(
	Refresh_Renderer *driverData,
	Refresh_RenderPassCreateInfo *renderPassCreateInfo
) {
    VulkanRenderer *renderer = (VulkanRenderer*) driverData;

    VkResult vulkanResult;
    VkAttachmentDescription attachmentDescriptions[2 * MAX_COLOR_TARGET_BINDINGS + 1];
    VkAttachmentReference colorAttachmentReferences[MAX_COLOR_TARGET_BINDINGS];
    VkAttachmentReference resolveReferences[MAX_COLOR_TARGET_BINDINGS + 1];
    VkAttachmentReference depthStencilAttachmentReference;
	VkRenderPassCreateInfo vkRenderPassCreateInfo;
    VkSubpassDescription subpass;
    VkRenderPass renderPass;
    uint32_t i;
	uint8_t multisampling = 0;

    uint32_t attachmentDescriptionCount = 0;
    uint32_t colorAttachmentReferenceCount = 0;
    uint32_t resolveReferenceCount = 0;

    for (i = 0; i < renderPassCreateInfo->colorTargetCount; i += 1)
    {
        if (renderPassCreateInfo->colorTargetDescriptions[attachmentDescriptionCount].multisampleCount > REFRESH_SAMPLECOUNT_1)
        {
			multisampling = 1;

            /* Resolve attachment and multisample attachment */

            attachmentDescriptions[attachmentDescriptionCount].flags = 0;
            attachmentDescriptions[attachmentDescriptionCount].format = RefreshToVK_SurfaceFormat[
                renderPassCreateInfo->colorTargetDescriptions[i].format
            ];
            attachmentDescriptions[attachmentDescriptionCount].samples =
                VK_SAMPLE_COUNT_1_BIT;
            attachmentDescriptions[attachmentDescriptionCount].loadOp = RefreshToVK_LoadOp[
                renderPassCreateInfo->colorTargetDescriptions[i].loadOp
            ];
            attachmentDescriptions[attachmentDescriptionCount].storeOp = RefreshToVK_StoreOp[
                renderPassCreateInfo->colorTargetDescriptions[i].storeOp
            ];
            attachmentDescriptions[attachmentDescriptionCount].stencilLoadOp =
                VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachmentDescriptions[attachmentDescriptionCount].stencilStoreOp =
                VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachmentDescriptions[attachmentDescriptionCount].initialLayout =
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachmentDescriptions[attachmentDescriptionCount].finalLayout =
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            resolveReferences[resolveReferenceCount].attachment =
                attachmentDescriptionCount;
            resolveReferences[resolveReferenceCount].layout =
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            attachmentDescriptionCount += 1;
            resolveReferenceCount += 1;

            attachmentDescriptions[attachmentDescriptionCount].flags = 0;
            attachmentDescriptions[attachmentDescriptionCount].format = RefreshToVK_SurfaceFormat[
                renderPassCreateInfo->colorTargetDescriptions[i].format
            ];
            attachmentDescriptions[attachmentDescriptionCount].samples = RefreshToVK_SampleCount[
                renderPassCreateInfo->colorTargetDescriptions[i].multisampleCount
            ];
            attachmentDescriptions[attachmentDescriptionCount].loadOp = RefreshToVK_LoadOp[
                renderPassCreateInfo->colorTargetDescriptions[i].loadOp
            ];
            attachmentDescriptions[attachmentDescriptionCount].storeOp = RefreshToVK_StoreOp[
                renderPassCreateInfo->colorTargetDescriptions[i].storeOp
            ];
            attachmentDescriptions[attachmentDescriptionCount].stencilLoadOp =
                VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachmentDescriptions[attachmentDescriptionCount].stencilStoreOp =
                VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachmentDescriptions[attachmentDescriptionCount].initialLayout =
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachmentDescriptions[attachmentDescriptionCount].finalLayout =
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            colorAttachmentReferences[colorAttachmentReferenceCount].attachment =
                attachmentDescriptionCount;
            colorAttachmentReferences[colorAttachmentReferenceCount].layout =
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            attachmentDescriptionCount += 1;
            colorAttachmentReferenceCount += 1;
        }
        else
        {
            attachmentDescriptions[attachmentDescriptionCount].flags = 0;
            attachmentDescriptions[attachmentDescriptionCount].format = RefreshToVK_SurfaceFormat[
                renderPassCreateInfo->colorTargetDescriptions[i].format
            ];
            attachmentDescriptions[attachmentDescriptionCount].samples =
                VK_SAMPLE_COUNT_1_BIT;
            attachmentDescriptions[attachmentDescriptionCount].loadOp = RefreshToVK_LoadOp[
                renderPassCreateInfo->colorTargetDescriptions[i].loadOp
            ];
            attachmentDescriptions[attachmentDescriptionCount].storeOp = RefreshToVK_StoreOp[
                renderPassCreateInfo->colorTargetDescriptions[i].storeOp
            ];
            attachmentDescriptions[attachmentDescriptionCount].stencilLoadOp =
                VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachmentDescriptions[attachmentDescriptionCount].stencilStoreOp =
                VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachmentDescriptions[attachmentDescriptionCount].initialLayout =
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachmentDescriptions[attachmentDescriptionCount].finalLayout =
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            attachmentDescriptionCount += 1;

            colorAttachmentReferences[colorAttachmentReferenceCount].attachment = i;
            colorAttachmentReferences[colorAttachmentReferenceCount].layout =
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            colorAttachmentReferenceCount += 1;
        }
    }

    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.flags = 0;
    subpass.inputAttachmentCount = 0;
    subpass.pInputAttachments = NULL;
    subpass.colorAttachmentCount = renderPassCreateInfo->colorTargetCount;
    subpass.pColorAttachments = colorAttachmentReferences;
    subpass.preserveAttachmentCount = 0;
    subpass.pPreserveAttachments = NULL;

    if (renderPassCreateInfo->depthTargetDescription == NULL)
    {
        subpass.pDepthStencilAttachment = NULL;
    }
    else
    {
        attachmentDescriptions[attachmentDescriptionCount].flags = 0;
        attachmentDescriptions[attachmentDescriptionCount].format = RefreshToVK_DepthFormat[
            renderPassCreateInfo->depthTargetDescription->depthFormat
        ];
        attachmentDescriptions[attachmentDescriptionCount].samples =
            VK_SAMPLE_COUNT_1_BIT; /* FIXME: do these take multisamples? */
        attachmentDescriptions[attachmentDescriptionCount].loadOp = RefreshToVK_LoadOp[
            renderPassCreateInfo->depthTargetDescription->loadOp
        ];
        attachmentDescriptions[attachmentDescriptionCount].storeOp = RefreshToVK_StoreOp[
            renderPassCreateInfo->depthTargetDescription->storeOp
        ];
        attachmentDescriptions[attachmentDescriptionCount].stencilLoadOp = RefreshToVK_LoadOp[
            renderPassCreateInfo->depthTargetDescription->stencilLoadOp
        ];
        attachmentDescriptions[attachmentDescriptionCount].stencilStoreOp = RefreshToVK_StoreOp[
            renderPassCreateInfo->depthTargetDescription->stencilStoreOp
        ];
        attachmentDescriptions[attachmentDescriptionCount].initialLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachmentDescriptions[attachmentDescriptionCount].finalLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        depthStencilAttachmentReference.attachment =
            attachmentDescriptionCount;
        depthStencilAttachmentReference.layout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        subpass.pDepthStencilAttachment =
            &depthStencilAttachmentReference;

        attachmentDescriptionCount += 1;
    }

	if (multisampling)
	{
		subpass.pResolveAttachments = resolveReferences;
	}
	else
	{
		subpass.pResolveAttachments = NULL;
	}

    vkRenderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    vkRenderPassCreateInfo.pNext = NULL;
    vkRenderPassCreateInfo.flags = 0;
    vkRenderPassCreateInfo.pAttachments = attachmentDescriptions;
    vkRenderPassCreateInfo.attachmentCount = attachmentDescriptionCount;
    vkRenderPassCreateInfo.subpassCount = 1;
    vkRenderPassCreateInfo.pSubpasses = &subpass;
    vkRenderPassCreateInfo.dependencyCount = 0;
    vkRenderPassCreateInfo.pDependencies = NULL;

    vulkanResult = renderer->vkCreateRenderPass(
        renderer->logicalDevice,
        &vkRenderPassCreateInfo,
        NULL,
        &renderPass
    );

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateRenderPass", vulkanResult);
		return NULL_RENDER_PASS;
	}

    return (Refresh_RenderPass*) renderPass;
}

static uint8_t VULKAN_INTERNAL_CreateDescriptorPool(
	VulkanRenderer *renderer,
	VkDescriptorType descriptorType,
	uint32_t descriptorSetCount,
	uint32_t descriptorCount,
	VkDescriptorPool *pDescriptorPool
) {
	VkResult vulkanResult;

	VkDescriptorPoolSize descriptorPoolSize;
	VkDescriptorPoolCreateInfo descriptorPoolInfo;

	descriptorPoolSize.type = descriptorType;
	descriptorPoolSize.descriptorCount = descriptorCount;

	descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolInfo.pNext = NULL;
	descriptorPoolInfo.flags = 0;
	descriptorPoolInfo.maxSets = descriptorSetCount;
	descriptorPoolInfo.poolSizeCount = 1;
	descriptorPoolInfo.pPoolSizes = &descriptorPoolSize;

	vulkanResult = renderer->vkCreateDescriptorPool(
		renderer->logicalDevice,
		&descriptorPoolInfo,
		NULL,
		pDescriptorPool
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateDescriptorPool", vulkanResult);
		return 0;
	}

	return 1;
}

static uint8_t VULKAN_INTERNAL_AllocateDescriptorSets(
	VulkanRenderer *renderer,
	VkDescriptorPool descriptorPool,
	VkDescriptorSetLayout descriptorSetLayout,
	uint32_t descriptorSetCount,
	VkDescriptorSet *descriptorSetArray
) {
	VkResult vulkanResult;
	uint32_t i;
	VkDescriptorSetAllocateInfo descriptorSetAllocateInfo;
	VkDescriptorSetLayout *descriptorSetLayouts = SDL_stack_alloc(VkDescriptorSetLayout, descriptorSetCount);

	SDL_LockMutex(renderer->descriptorSetLock);

	for (i = 0; i < descriptorSetCount; i += 1)
	{
		descriptorSetLayouts[i] = descriptorSetLayout;
	}

	descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSetAllocateInfo.pNext = NULL;
	descriptorSetAllocateInfo.descriptorPool = descriptorPool;
	descriptorSetAllocateInfo.descriptorSetCount = descriptorSetCount;
	descriptorSetAllocateInfo.pSetLayouts = descriptorSetLayouts;

	vulkanResult = renderer->vkAllocateDescriptorSets(
		renderer->logicalDevice,
		&descriptorSetAllocateInfo,
		descriptorSetArray
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkAllocateDescriptorSets", vulkanResult);
		SDL_stack_free(descriptorSetLayouts);
		return 0;
	}

	SDL_UnlockMutex(renderer->descriptorSetLock);

	SDL_stack_free(descriptorSetLayouts);
	return 1;
}

static ImageDescriptorSetCache* VULKAN_INTERNAL_CreateImageDescriptorSetCache(
	VulkanRenderer *renderer,
	VkDescriptorType descriptorType,
	VkDescriptorSetLayout descriptorSetLayout,
	uint32_t bindingCount
) {
	uint32_t i;
	ImageDescriptorSetCache *imageDescriptorSetCache = SDL_malloc(sizeof(ImageDescriptorSetCache));

	imageDescriptorSetCache->elements = SDL_malloc(sizeof(ImageDescriptorSetHashMap) * 16);
	imageDescriptorSetCache->count = 0;
	imageDescriptorSetCache->capacity = 16;

	for (i = 0; i < NUM_DESCRIPTOR_SET_HASH_BUCKETS; i += 1)
	{
		imageDescriptorSetCache->buckets[i].elements = NULL;
		imageDescriptorSetCache->buckets[i].count = 0;
		imageDescriptorSetCache->buckets[i].capacity = 0;
	}

	imageDescriptorSetCache->descriptorSetLayout = descriptorSetLayout;
	imageDescriptorSetCache->bindingCount = bindingCount;
	imageDescriptorSetCache->descriptorType = descriptorType;

	imageDescriptorSetCache->imageDescriptorPools = SDL_malloc(sizeof(VkDescriptorPool));
	imageDescriptorSetCache->imageDescriptorPoolCount = 1;
	imageDescriptorSetCache->nextPoolSize = DESCRIPTOR_POOL_STARTING_SIZE * 2;

	VULKAN_INTERNAL_CreateDescriptorPool(
		renderer,
		descriptorType,
		DESCRIPTOR_POOL_STARTING_SIZE,
		DESCRIPTOR_POOL_STARTING_SIZE * bindingCount,
		&imageDescriptorSetCache->imageDescriptorPools[0]
	);

	imageDescriptorSetCache->inactiveDescriptorSetCapacity = DESCRIPTOR_POOL_STARTING_SIZE;
	imageDescriptorSetCache->inactiveDescriptorSetCount = DESCRIPTOR_POOL_STARTING_SIZE;
	imageDescriptorSetCache->inactiveDescriptorSets = SDL_malloc(
		sizeof(VkDescriptorSet) * DESCRIPTOR_POOL_STARTING_SIZE
	);

	VULKAN_INTERNAL_AllocateDescriptorSets(
		renderer,
		imageDescriptorSetCache->imageDescriptorPools[0],
		imageDescriptorSetCache->descriptorSetLayout,
		DESCRIPTOR_POOL_STARTING_SIZE,
		imageDescriptorSetCache->inactiveDescriptorSets
	);

	return imageDescriptorSetCache;
}

static BufferDescriptorSetCache* VULKAN_INTERNAL_CreateBufferDescriptorSetCache(
	VulkanRenderer *renderer,
	VkDescriptorType descriptorType,
	VkDescriptorSetLayout descriptorSetLayout,
	uint32_t bindingCount
) {
	uint32_t i;
	BufferDescriptorSetCache *bufferDescriptorSetCache = SDL_malloc(sizeof(BufferDescriptorSetCache));

	bufferDescriptorSetCache->elements = SDL_malloc(sizeof(BufferDescriptorSetHashMap) * 16);
	bufferDescriptorSetCache->count = 0;
	bufferDescriptorSetCache->capacity = 16;

	for (i = 0; i < NUM_DESCRIPTOR_SET_HASH_BUCKETS; i += 1)
	{
		bufferDescriptorSetCache->buckets[i].elements = NULL;
		bufferDescriptorSetCache->buckets[i].count = 0;
		bufferDescriptorSetCache->buckets[i].capacity = 0;
	}

	bufferDescriptorSetCache->descriptorSetLayout = descriptorSetLayout;
	bufferDescriptorSetCache->bindingCount = bindingCount;
	bufferDescriptorSetCache->descriptorType = descriptorType;

	bufferDescriptorSetCache->bufferDescriptorPools = SDL_malloc(sizeof(VkDescriptorPool));
	bufferDescriptorSetCache->bufferDescriptorPoolCount = 1;
	bufferDescriptorSetCache->nextPoolSize = DESCRIPTOR_POOL_STARTING_SIZE * 2;

	VULKAN_INTERNAL_CreateDescriptorPool(
		renderer,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		DESCRIPTOR_POOL_STARTING_SIZE,
		DESCRIPTOR_POOL_STARTING_SIZE * bindingCount,
		&bufferDescriptorSetCache->bufferDescriptorPools[0]
	);

	bufferDescriptorSetCache->inactiveDescriptorSetCapacity = DESCRIPTOR_POOL_STARTING_SIZE;
	bufferDescriptorSetCache->inactiveDescriptorSetCount = DESCRIPTOR_POOL_STARTING_SIZE;
	bufferDescriptorSetCache->inactiveDescriptorSets = SDL_malloc(
		sizeof(VkDescriptorSet) * DESCRIPTOR_POOL_STARTING_SIZE
	);

	VULKAN_INTERNAL_AllocateDescriptorSets(
		renderer,
		bufferDescriptorSetCache->bufferDescriptorPools[0],
		bufferDescriptorSetCache->descriptorSetLayout,
		DESCRIPTOR_POOL_STARTING_SIZE,
		bufferDescriptorSetCache->inactiveDescriptorSets
	);

	return bufferDescriptorSetCache;
}

static VkDescriptorSetLayout VULKAN_INTERNAL_FetchDescriptorSetLayout(
	VulkanRenderer *renderer,
	VkDescriptorType descriptorType,
	uint32_t bindingCount,
	VkShaderStageFlagBits shaderStageFlagBit
) {
	DescriptorSetLayoutHash descriptorSetLayoutHash;
	VkDescriptorSetLayout descriptorSetLayout;

	VkDescriptorSetLayoutBinding setLayoutBindings[MAX_TEXTURE_SAMPLERS];
	VkDescriptorSetLayoutCreateInfo setLayoutCreateInfo;

	VkResult vulkanResult;
	uint32_t i;

	if (bindingCount == 0)
	{
		if (shaderStageFlagBit == VK_SHADER_STAGE_VERTEX_BIT)
		{
			return renderer->emptyVertexSamplerLayout;
		}
		else if (shaderStageFlagBit == VK_SHADER_STAGE_FRAGMENT_BIT)
		{
			return renderer->emptyFragmentSamplerLayout;
		}
		else if (shaderStageFlagBit == VK_SHADER_STAGE_COMPUTE_BIT)
		{
			if (descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
			{
				return renderer->emptyComputeBufferDescriptorSetLayout;
			}
			else if (descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
			{
				return renderer->emptyComputeImageDescriptorSetLayout;
			}
			else
			{
				Refresh_LogError("Invalid descriptor type for compute shader: ", descriptorType);
				return NULL_DESC_LAYOUT;
			}
		}
		else
		{
			Refresh_LogError("Invalid shader stage flag bit: ", shaderStageFlagBit);
			return NULL_DESC_LAYOUT;
		}
	}

	descriptorSetLayoutHash.descriptorType = descriptorType;
	descriptorSetLayoutHash.bindingCount = bindingCount;
	descriptorSetLayoutHash.stageFlag = shaderStageFlagBit;

	descriptorSetLayout = DescriptorSetLayoutHashTable_Fetch(
		&renderer->descriptorSetLayoutHashTable,
		descriptorSetLayoutHash
	);

	if (descriptorSetLayout != VK_NULL_HANDLE)
	{
		return descriptorSetLayout;
	}

	for (i = 0; i < bindingCount; i += 1)
	{
		setLayoutBindings[i].binding = i;
		setLayoutBindings[i].descriptorCount = 1;
		setLayoutBindings[i].descriptorType = descriptorType;
		setLayoutBindings[i].stageFlags = shaderStageFlagBit;
		setLayoutBindings[i].pImmutableSamplers = NULL;
	}

	setLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setLayoutCreateInfo.pNext = NULL;
	setLayoutCreateInfo.flags = 0;
	setLayoutCreateInfo.bindingCount = bindingCount;
	setLayoutCreateInfo.pBindings = setLayoutBindings;

	vulkanResult = renderer->vkCreateDescriptorSetLayout(
		renderer->logicalDevice,
		&setLayoutCreateInfo,
		NULL,
		&descriptorSetLayout
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateDescriptorSetLayout", vulkanResult);
		return NULL_DESC_LAYOUT;
	}

	DescriptorSetLayoutHashTable_Insert(
		&renderer->descriptorSetLayoutHashTable,
		descriptorSetLayoutHash,
		descriptorSetLayout
	);

	return descriptorSetLayout;
}

static VulkanGraphicsPipelineLayout* VULKAN_INTERNAL_FetchGraphicsPipelineLayout(
	VulkanRenderer *renderer,
	uint32_t vertexSamplerBindingCount,
	uint32_t fragmentSamplerBindingCount
) {
	VkDescriptorSetLayout setLayouts[4];

	GraphicsPipelineLayoutHash pipelineLayoutHash;
	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo;
	VkResult vulkanResult;

	VulkanGraphicsPipelineLayout *vulkanGraphicsPipelineLayout;

	pipelineLayoutHash.vertexSamplerLayout = VULKAN_INTERNAL_FetchDescriptorSetLayout(
		renderer,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		vertexSamplerBindingCount,
		VK_SHADER_STAGE_VERTEX_BIT
	);

	pipelineLayoutHash.fragmentSamplerLayout = VULKAN_INTERNAL_FetchDescriptorSetLayout(
		renderer,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		fragmentSamplerBindingCount,
		VK_SHADER_STAGE_FRAGMENT_BIT
	);

	pipelineLayoutHash.vertexUniformLayout = renderer->vertexParamLayout;
	pipelineLayoutHash.fragmentUniformLayout = renderer->fragmentParamLayout;

	vulkanGraphicsPipelineLayout = GraphicsPipelineLayoutHashArray_Fetch(
		&renderer->graphicsPipelineLayoutHashTable,
		pipelineLayoutHash
	);

	if (vulkanGraphicsPipelineLayout != NULL)
	{
		return vulkanGraphicsPipelineLayout;
	}

	vulkanGraphicsPipelineLayout = SDL_malloc(sizeof(VulkanGraphicsPipelineLayout));

	setLayouts[0] = pipelineLayoutHash.vertexSamplerLayout;
	setLayouts[1] = pipelineLayoutHash.fragmentSamplerLayout;
	setLayouts[2] = renderer->vertexParamLayout;
	setLayouts[3] = renderer->fragmentParamLayout;

	pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCreateInfo.pNext = NULL;
	pipelineLayoutCreateInfo.flags = 0;
	pipelineLayoutCreateInfo.setLayoutCount = 4;
	pipelineLayoutCreateInfo.pSetLayouts = setLayouts;
	pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
	pipelineLayoutCreateInfo.pPushConstantRanges = NULL;

	vulkanResult = renderer->vkCreatePipelineLayout(
		renderer->logicalDevice,
		&pipelineLayoutCreateInfo,
		NULL,
		&vulkanGraphicsPipelineLayout->pipelineLayout
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreatePipelineLayout", vulkanResult);
		return NULL;
	}

	GraphicsPipelineLayoutHashArray_Insert(
		&renderer->graphicsPipelineLayoutHashTable,
		pipelineLayoutHash,
		vulkanGraphicsPipelineLayout
	);

	/* If the binding count is 0
	 * we can just bind the same descriptor set
	 * so no cache is needed
	 */

	if (vertexSamplerBindingCount == 0)
	{
		vulkanGraphicsPipelineLayout->vertexSamplerDescriptorSetCache = NULL;
	}
	else
	{
		vulkanGraphicsPipelineLayout->vertexSamplerDescriptorSetCache =
			VULKAN_INTERNAL_CreateImageDescriptorSetCache(
				renderer,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				pipelineLayoutHash.vertexSamplerLayout,
				vertexSamplerBindingCount
			);
	}

	if (fragmentSamplerBindingCount == 0)
	{
		vulkanGraphicsPipelineLayout->fragmentSamplerDescriptorSetCache = NULL;
	}
	else
	{
		vulkanGraphicsPipelineLayout->fragmentSamplerDescriptorSetCache =
			VULKAN_INTERNAL_CreateImageDescriptorSetCache(
				renderer,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				pipelineLayoutHash.fragmentSamplerLayout,
				fragmentSamplerBindingCount
			);
	}

	return vulkanGraphicsPipelineLayout;
}

static Refresh_GraphicsPipeline* VULKAN_CreateGraphicsPipeline(
	Refresh_Renderer *driverData,
	Refresh_GraphicsPipelineCreateInfo *pipelineCreateInfo
) {
	VkResult vulkanResult;
	uint32_t i;

	VulkanGraphicsPipeline *graphicsPipeline = (VulkanGraphicsPipeline*) SDL_malloc(sizeof(VulkanGraphicsPipeline));
	VkGraphicsPipelineCreateInfo vkPipelineCreateInfo;

	VkPipelineShaderStageCreateInfo shaderStageCreateInfos[2];

	VkPipelineVertexInputStateCreateInfo vertexInputStateCreateInfo;
	VkVertexInputBindingDescription *vertexInputBindingDescriptions = SDL_stack_alloc(VkVertexInputBindingDescription, pipelineCreateInfo->vertexInputState.vertexBindingCount);
	VkVertexInputAttributeDescription *vertexInputAttributeDescriptions = SDL_stack_alloc(VkVertexInputAttributeDescription, pipelineCreateInfo->vertexInputState.vertexAttributeCount);

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCreateInfo;

	VkPipelineViewportStateCreateInfo viewportStateCreateInfo;
	VkViewport *viewports = SDL_stack_alloc(VkViewport, pipelineCreateInfo->viewportState.viewportCount);
	VkRect2D *scissors = SDL_stack_alloc(VkRect2D, pipelineCreateInfo->viewportState.scissorCount);

	VkPipelineRasterizationStateCreateInfo rasterizationStateCreateInfo;

	VkPipelineMultisampleStateCreateInfo multisampleStateCreateInfo;

	VkPipelineDepthStencilStateCreateInfo depthStencilStateCreateInfo;
	VkStencilOpState frontStencilState;
	VkStencilOpState backStencilState;

	VkPipelineColorBlendStateCreateInfo colorBlendStateCreateInfo;
	VkPipelineColorBlendAttachmentState *colorBlendAttachmentStates = SDL_stack_alloc(
		VkPipelineColorBlendAttachmentState,
		pipelineCreateInfo->colorBlendState.blendStateCount
	);

	VkDescriptorSetAllocateInfo vertexUBODescriptorAllocateInfo;
	VkDescriptorSetAllocateInfo fragmentUBODescriptorAllocateInfo;

	VkWriteDescriptorSet uboWriteDescriptorSets[2];
	VkDescriptorBufferInfo vertexUniformBufferInfo;
	VkDescriptorBufferInfo fragmentUniformBufferInfo;

	VulkanRenderer *renderer = (VulkanRenderer*) driverData;

	/* Shader stages */

	shaderStageCreateInfos[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageCreateInfos[0].pNext = NULL;
	shaderStageCreateInfos[0].flags = 0;
	shaderStageCreateInfos[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStageCreateInfos[0].module = (VkShaderModule) pipelineCreateInfo->vertexShaderState.shaderModule;
	shaderStageCreateInfos[0].pName = pipelineCreateInfo->vertexShaderState.entryPointName;
	shaderStageCreateInfos[0].pSpecializationInfo = NULL;

	graphicsPipeline->vertexUBOBlockSize =
		VULKAN_INTERNAL_NextHighestAlignment(
			pipelineCreateInfo->vertexShaderState.uniformBufferSize,
			renderer->minUBOAlignment
		);

	shaderStageCreateInfos[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageCreateInfos[1].pNext = NULL;
	shaderStageCreateInfos[1].flags = 0;
	shaderStageCreateInfos[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStageCreateInfos[1].module = (VkShaderModule) pipelineCreateInfo->fragmentShaderState.shaderModule;
	shaderStageCreateInfos[1].pName = pipelineCreateInfo->fragmentShaderState.entryPointName;
	shaderStageCreateInfos[1].pSpecializationInfo = NULL;

	graphicsPipeline->fragmentUBOBlockSize =
		VULKAN_INTERNAL_NextHighestAlignment(
			pipelineCreateInfo->fragmentShaderState.uniformBufferSize,
			renderer->minUBOAlignment
		);

	/* Vertex input */

	for (i = 0; i < pipelineCreateInfo->vertexInputState.vertexBindingCount; i += 1)
	{
		vertexInputBindingDescriptions[i].binding = pipelineCreateInfo->vertexInputState.vertexBindings[i].binding;
		vertexInputBindingDescriptions[i].inputRate = RefreshToVK_VertexInputRate[
			pipelineCreateInfo->vertexInputState.vertexBindings[i].inputRate
		];
		vertexInputBindingDescriptions[i].stride = pipelineCreateInfo->vertexInputState.vertexBindings[i].stride;
	}

	for (i = 0; i < pipelineCreateInfo->vertexInputState.vertexAttributeCount; i += 1)
	{
		vertexInputAttributeDescriptions[i].binding = pipelineCreateInfo->vertexInputState.vertexAttributes[i].binding;
		vertexInputAttributeDescriptions[i].format = RefreshToVK_VertexFormat[
			pipelineCreateInfo->vertexInputState.vertexAttributes[i].format
		];
		vertexInputAttributeDescriptions[i].location = pipelineCreateInfo->vertexInputState.vertexAttributes[i].location;
		vertexInputAttributeDescriptions[i].offset = pipelineCreateInfo->vertexInputState.vertexAttributes[i].offset;
	}

	vertexInputStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputStateCreateInfo.pNext = NULL;
	vertexInputStateCreateInfo.flags = 0;
	vertexInputStateCreateInfo.vertexBindingDescriptionCount = pipelineCreateInfo->vertexInputState.vertexBindingCount;
	vertexInputStateCreateInfo.pVertexBindingDescriptions = vertexInputBindingDescriptions;
	vertexInputStateCreateInfo.vertexAttributeDescriptionCount = pipelineCreateInfo->vertexInputState.vertexAttributeCount;
	vertexInputStateCreateInfo.pVertexAttributeDescriptions = vertexInputAttributeDescriptions;

	/* Topology */

	inputAssemblyStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssemblyStateCreateInfo.pNext = NULL;
	inputAssemblyStateCreateInfo.flags = 0;
	inputAssemblyStateCreateInfo.primitiveRestartEnable = VK_FALSE;
	inputAssemblyStateCreateInfo.topology = RefreshToVK_PrimitiveType[
		pipelineCreateInfo->primitiveType
	];

	graphicsPipeline->primitiveType = pipelineCreateInfo->primitiveType;

	/* Viewport */

	for (i = 0; i < pipelineCreateInfo->viewportState.viewportCount; i += 1)
	{
		viewports[i].x = pipelineCreateInfo->viewportState.viewports[i].x;
		viewports[i].y = pipelineCreateInfo->viewportState.viewports[i].y;
		viewports[i].width = pipelineCreateInfo->viewportState.viewports[i].w;
		viewports[i].height = pipelineCreateInfo->viewportState.viewports[i].h;
		viewports[i].minDepth = pipelineCreateInfo->viewportState.viewports[i].minDepth;
		viewports[i].maxDepth = pipelineCreateInfo->viewportState.viewports[i].maxDepth;
	}

	for (i = 0; i < pipelineCreateInfo->viewportState.scissorCount; i += 1)
	{
		scissors[i].offset.x = pipelineCreateInfo->viewportState.scissors[i].x;
		scissors[i].offset.y = pipelineCreateInfo->viewportState.scissors[i].y;
		scissors[i].extent.width = pipelineCreateInfo->viewportState.scissors[i].w;
		scissors[i].extent.height = pipelineCreateInfo->viewportState.scissors[i].h;
	}

	viewportStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportStateCreateInfo.pNext = NULL;
	viewportStateCreateInfo.flags = 0;
	viewportStateCreateInfo.viewportCount = pipelineCreateInfo->viewportState.viewportCount;
	viewportStateCreateInfo.pViewports = viewports;
	viewportStateCreateInfo.scissorCount = pipelineCreateInfo->viewportState.scissorCount;
	viewportStateCreateInfo.pScissors = scissors;

	/* Rasterization */

	rasterizationStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizationStateCreateInfo.pNext = NULL;
	rasterizationStateCreateInfo.flags = 0;
	rasterizationStateCreateInfo.depthClampEnable = pipelineCreateInfo->rasterizerState.depthClampEnable;
	rasterizationStateCreateInfo.rasterizerDiscardEnable = VK_FALSE;
	rasterizationStateCreateInfo.polygonMode = RefreshToVK_PolygonMode[
		pipelineCreateInfo->rasterizerState.fillMode
	];
	rasterizationStateCreateInfo.cullMode = RefreshToVK_CullMode[
		pipelineCreateInfo->rasterizerState.cullMode
	];
	rasterizationStateCreateInfo.frontFace = RefreshToVK_FrontFace[
		pipelineCreateInfo->rasterizerState.frontFace
	];
	rasterizationStateCreateInfo.depthBiasEnable =
		pipelineCreateInfo->rasterizerState.depthBiasEnable;
	rasterizationStateCreateInfo.depthBiasConstantFactor =
		pipelineCreateInfo->rasterizerState.depthBiasConstantFactor;
	rasterizationStateCreateInfo.depthBiasClamp =
		pipelineCreateInfo->rasterizerState.depthBiasClamp;
	rasterizationStateCreateInfo.depthBiasSlopeFactor =
		pipelineCreateInfo->rasterizerState.depthBiasSlopeFactor;
	rasterizationStateCreateInfo.lineWidth =
		pipelineCreateInfo->rasterizerState.lineWidth;

	/* Multisample */

	multisampleStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampleStateCreateInfo.pNext = NULL;
	multisampleStateCreateInfo.flags = 0;
	multisampleStateCreateInfo.rasterizationSamples = RefreshToVK_SampleCount[
		pipelineCreateInfo->multisampleState.multisampleCount
	];
	multisampleStateCreateInfo.sampleShadingEnable = VK_FALSE;
	multisampleStateCreateInfo.minSampleShading = 1.0f;
	multisampleStateCreateInfo.pSampleMask =
		&pipelineCreateInfo->multisampleState.sampleMask;
	multisampleStateCreateInfo.alphaToCoverageEnable = VK_FALSE;
	multisampleStateCreateInfo.alphaToOneEnable = VK_FALSE;

	/* Depth Stencil State */

	frontStencilState.failOp = RefreshToVK_StencilOp[
		pipelineCreateInfo->depthStencilState.frontStencilState.failOp
	];
	frontStencilState.passOp = RefreshToVK_StencilOp[
		pipelineCreateInfo->depthStencilState.frontStencilState.passOp
	];
	frontStencilState.depthFailOp = RefreshToVK_StencilOp[
		pipelineCreateInfo->depthStencilState.frontStencilState.depthFailOp
	];
	frontStencilState.compareOp = RefreshToVK_CompareOp[
		pipelineCreateInfo->depthStencilState.frontStencilState.compareOp
	];
	frontStencilState.compareMask =
		pipelineCreateInfo->depthStencilState.frontStencilState.compareMask;
	frontStencilState.writeMask =
		pipelineCreateInfo->depthStencilState.frontStencilState.writeMask;
	frontStencilState.reference =
		pipelineCreateInfo->depthStencilState.frontStencilState.reference;

	backStencilState.failOp = RefreshToVK_StencilOp[
		pipelineCreateInfo->depthStencilState.backStencilState.failOp
	];
	backStencilState.passOp = RefreshToVK_StencilOp[
		pipelineCreateInfo->depthStencilState.backStencilState.passOp
	];
	backStencilState.depthFailOp = RefreshToVK_StencilOp[
		pipelineCreateInfo->depthStencilState.backStencilState.depthFailOp
	];
	backStencilState.compareOp = RefreshToVK_CompareOp[
		pipelineCreateInfo->depthStencilState.backStencilState.compareOp
	];
	backStencilState.compareMask =
		pipelineCreateInfo->depthStencilState.backStencilState.compareMask;
	backStencilState.writeMask =
		pipelineCreateInfo->depthStencilState.backStencilState.writeMask;
	backStencilState.reference =
		pipelineCreateInfo->depthStencilState.backStencilState.reference;


	depthStencilStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencilStateCreateInfo.pNext = NULL;
	depthStencilStateCreateInfo.flags = 0;
	depthStencilStateCreateInfo.depthTestEnable =
		pipelineCreateInfo->depthStencilState.depthTestEnable;
	depthStencilStateCreateInfo.depthWriteEnable =
		pipelineCreateInfo->depthStencilState.depthWriteEnable;
	depthStencilStateCreateInfo.depthCompareOp = RefreshToVK_CompareOp[
		pipelineCreateInfo->depthStencilState.compareOp
	];
	depthStencilStateCreateInfo.depthBoundsTestEnable =
		pipelineCreateInfo->depthStencilState.depthBoundsTestEnable;
	depthStencilStateCreateInfo.stencilTestEnable =
		pipelineCreateInfo->depthStencilState.stencilTestEnable;
	depthStencilStateCreateInfo.front = frontStencilState;
	depthStencilStateCreateInfo.back = backStencilState;
	depthStencilStateCreateInfo.minDepthBounds =
		pipelineCreateInfo->depthStencilState.minDepthBounds;
	depthStencilStateCreateInfo.maxDepthBounds =
		pipelineCreateInfo->depthStencilState.maxDepthBounds;

	/* Color Blend */

	for (i = 0; i < pipelineCreateInfo->colorBlendState.blendStateCount; i += 1)
	{
		colorBlendAttachmentStates[i].blendEnable =
			pipelineCreateInfo->colorBlendState.blendStates[i].blendEnable;
		colorBlendAttachmentStates[i].srcColorBlendFactor = RefreshToVK_BlendFactor[
			pipelineCreateInfo->colorBlendState.blendStates[i].srcColorBlendFactor
		];
		colorBlendAttachmentStates[i].dstColorBlendFactor = RefreshToVK_BlendFactor[
			pipelineCreateInfo->colorBlendState.blendStates[i].dstColorBlendFactor
		];
		colorBlendAttachmentStates[i].colorBlendOp = RefreshToVK_BlendOp[
			pipelineCreateInfo->colorBlendState.blendStates[i].colorBlendOp
		];
		colorBlendAttachmentStates[i].srcAlphaBlendFactor = RefreshToVK_BlendFactor[
			pipelineCreateInfo->colorBlendState.blendStates[i].srcAlphaBlendFactor
		];
		colorBlendAttachmentStates[i].dstAlphaBlendFactor = RefreshToVK_BlendFactor[
			pipelineCreateInfo->colorBlendState.blendStates[i].dstAlphaBlendFactor
		];
		colorBlendAttachmentStates[i].alphaBlendOp = RefreshToVK_BlendOp[
			pipelineCreateInfo->colorBlendState.blendStates[i].alphaBlendOp
		];
		colorBlendAttachmentStates[i].colorWriteMask =
			pipelineCreateInfo->colorBlendState.blendStates[i].colorWriteMask;
	}

	colorBlendStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendStateCreateInfo.pNext = NULL;
	colorBlendStateCreateInfo.flags = 0;
	colorBlendStateCreateInfo.logicOpEnable =
		pipelineCreateInfo->colorBlendState.logicOpEnable;
	colorBlendStateCreateInfo.logicOp = RefreshToVK_LogicOp[
		pipelineCreateInfo->colorBlendState.logicOp
	];
	colorBlendStateCreateInfo.attachmentCount =
		pipelineCreateInfo->colorBlendState.blendStateCount;
	colorBlendStateCreateInfo.pAttachments =
		colorBlendAttachmentStates;
	colorBlendStateCreateInfo.blendConstants[0] =
		pipelineCreateInfo->colorBlendState.blendConstants[0];
	colorBlendStateCreateInfo.blendConstants[1] =
		pipelineCreateInfo->colorBlendState.blendConstants[1];
	colorBlendStateCreateInfo.blendConstants[2] =
		pipelineCreateInfo->colorBlendState.blendConstants[2];
	colorBlendStateCreateInfo.blendConstants[3] =
		pipelineCreateInfo->colorBlendState.blendConstants[3];

	/* Pipeline Layout */

	graphicsPipeline->pipelineLayout = VULKAN_INTERNAL_FetchGraphicsPipelineLayout(
		renderer,
		pipelineCreateInfo->pipelineLayoutCreateInfo.vertexSamplerBindingCount,
		pipelineCreateInfo->pipelineLayoutCreateInfo.fragmentSamplerBindingCount
	);

	/* Pipeline */

	vkPipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	vkPipelineCreateInfo.pNext = NULL;
	vkPipelineCreateInfo.flags = 0;
	vkPipelineCreateInfo.stageCount = 2;
	vkPipelineCreateInfo.pStages = shaderStageCreateInfos;
	vkPipelineCreateInfo.pVertexInputState = &vertexInputStateCreateInfo;
	vkPipelineCreateInfo.pInputAssemblyState = &inputAssemblyStateCreateInfo;
	vkPipelineCreateInfo.pTessellationState = VK_NULL_HANDLE;
	vkPipelineCreateInfo.pViewportState = &viewportStateCreateInfo;
	vkPipelineCreateInfo.pRasterizationState = &rasterizationStateCreateInfo;
	vkPipelineCreateInfo.pMultisampleState = &multisampleStateCreateInfo;
	vkPipelineCreateInfo.pDepthStencilState = &depthStencilStateCreateInfo;
	vkPipelineCreateInfo.pColorBlendState = &colorBlendStateCreateInfo;
	vkPipelineCreateInfo.pDynamicState = VK_NULL_HANDLE;
	vkPipelineCreateInfo.layout = graphicsPipeline->pipelineLayout->pipelineLayout;
	vkPipelineCreateInfo.renderPass = (VkRenderPass) pipelineCreateInfo->renderPass;
	vkPipelineCreateInfo.subpass = 0;
	vkPipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
	vkPipelineCreateInfo.basePipelineIndex = 0;

	/* TODO: enable pipeline caching */
	vulkanResult = renderer->vkCreateGraphicsPipelines(
		renderer->logicalDevice,
		VK_NULL_HANDLE,
		1,
		&vkPipelineCreateInfo,
		NULL,
		&graphicsPipeline->pipeline
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateGraphicsPipelines", vulkanResult);
		Refresh_LogError("Failed to create graphics pipeline!");

		SDL_stack_free(vertexInputBindingDescriptions);
		SDL_stack_free(vertexInputAttributeDescriptions);
		SDL_stack_free(viewports);
		SDL_stack_free(scissors);
		SDL_stack_free(colorBlendAttachmentStates);
		return NULL;
	}

	SDL_stack_free(vertexInputBindingDescriptions);
	SDL_stack_free(vertexInputAttributeDescriptions);
	SDL_stack_free(viewports);
	SDL_stack_free(scissors);
	SDL_stack_free(colorBlendAttachmentStates);

	/* Allocate uniform buffer descriptors */

	vertexUBODescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	vertexUBODescriptorAllocateInfo.pNext = NULL;
	vertexUBODescriptorAllocateInfo.descriptorPool = renderer->defaultDescriptorPool;
	vertexUBODescriptorAllocateInfo.descriptorSetCount = 1;
	vertexUBODescriptorAllocateInfo.pSetLayouts = &renderer->vertexParamLayout;

	renderer->vkAllocateDescriptorSets(
		renderer->logicalDevice,
		&vertexUBODescriptorAllocateInfo,
		&graphicsPipeline->vertexUBODescriptorSet
	);

	fragmentUBODescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	fragmentUBODescriptorAllocateInfo.pNext = NULL;
	fragmentUBODescriptorAllocateInfo.descriptorPool = renderer->defaultDescriptorPool;
	fragmentUBODescriptorAllocateInfo.descriptorSetCount = 1;
	fragmentUBODescriptorAllocateInfo.pSetLayouts = &renderer->fragmentParamLayout;

	renderer->vkAllocateDescriptorSets(
		renderer->logicalDevice,
		&fragmentUBODescriptorAllocateInfo,
		&graphicsPipeline->fragmentUBODescriptorSet
	);

	if (graphicsPipeline->vertexUBOBlockSize == 0)
	{
		vertexUniformBufferInfo.buffer = renderer->dummyVertexUniformBuffer->subBuffers[0]->buffer;
		vertexUniformBufferInfo.offset = 0;
		vertexUniformBufferInfo.range = renderer->dummyVertexUniformBuffer->subBuffers[0]->size;
	}
	else
	{
		vertexUniformBufferInfo.buffer = renderer->vertexUBO->subBuffers[0]->buffer;
		vertexUniformBufferInfo.offset = 0;
		vertexUniformBufferInfo.range = graphicsPipeline->vertexUBOBlockSize;
	}

	if (graphicsPipeline->fragmentUBOBlockSize == 0)
	{
		fragmentUniformBufferInfo.buffer = renderer->dummyFragmentUniformBuffer->subBuffers[0]->buffer;
		fragmentUniformBufferInfo.offset = 0;
		fragmentUniformBufferInfo.range = renderer->dummyFragmentUniformBuffer->subBuffers[0]->size;
	}
	else
	{
		fragmentUniformBufferInfo.buffer = renderer->fragmentUBO->subBuffers[0]->buffer;
		fragmentUniformBufferInfo.offset = 0;
		fragmentUniformBufferInfo.range = graphicsPipeline->fragmentUBOBlockSize;
	}

	uboWriteDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	uboWriteDescriptorSets[0].pNext = NULL;
	uboWriteDescriptorSets[0].descriptorCount = 1;
	uboWriteDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	uboWriteDescriptorSets[0].dstArrayElement = 0;
	uboWriteDescriptorSets[0].dstBinding = 0;
	uboWriteDescriptorSets[0].dstSet = graphicsPipeline->vertexUBODescriptorSet;
	uboWriteDescriptorSets[0].pBufferInfo = &vertexUniformBufferInfo;
	uboWriteDescriptorSets[0].pImageInfo = NULL;
	uboWriteDescriptorSets[0].pTexelBufferView = NULL;

	uboWriteDescriptorSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	uboWriteDescriptorSets[1].pNext = NULL;
	uboWriteDescriptorSets[1].descriptorCount = 1;
	uboWriteDescriptorSets[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	uboWriteDescriptorSets[1].dstArrayElement = 0;
	uboWriteDescriptorSets[1].dstBinding = 0;
	uboWriteDescriptorSets[1].dstSet = graphicsPipeline->fragmentUBODescriptorSet;
	uboWriteDescriptorSets[1].pBufferInfo = &fragmentUniformBufferInfo;
	uboWriteDescriptorSets[1].pImageInfo = NULL;
	uboWriteDescriptorSets[1].pTexelBufferView = NULL;

	renderer->vkUpdateDescriptorSets(
		renderer->logicalDevice,
		2,
		uboWriteDescriptorSets,
		0,
		NULL
	);

	return (Refresh_GraphicsPipeline*) graphicsPipeline;
}

static VulkanComputePipelineLayout* VULKAN_INTERNAL_FetchComputePipelineLayout(
	VulkanRenderer *renderer,
	uint32_t bufferBindingCount,
	uint32_t imageBindingCount
) {
	VkResult vulkanResult;
	VkDescriptorSetLayout setLayouts[3];
	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo;
	ComputePipelineLayoutHash pipelineLayoutHash;
	VulkanComputePipelineLayout *vulkanComputePipelineLayout;

	pipelineLayoutHash.bufferLayout = VULKAN_INTERNAL_FetchDescriptorSetLayout(
		renderer,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		bufferBindingCount,
		VK_SHADER_STAGE_COMPUTE_BIT
	);

	pipelineLayoutHash.imageLayout = VULKAN_INTERNAL_FetchDescriptorSetLayout(
		renderer,
		VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		imageBindingCount,
		VK_SHADER_STAGE_COMPUTE_BIT
	);

	pipelineLayoutHash.uniformLayout = renderer->computeParamLayout;

	vulkanComputePipelineLayout = ComputePipelineLayoutHashArray_Fetch(
		&renderer->computePipelineLayoutHashTable,
		pipelineLayoutHash
	);

	if (vulkanComputePipelineLayout != NULL)
	{
		return vulkanComputePipelineLayout;
	}

	vulkanComputePipelineLayout = SDL_malloc(sizeof(VulkanComputePipelineLayout));

	setLayouts[0] = pipelineLayoutHash.bufferLayout;
	setLayouts[1] = pipelineLayoutHash.imageLayout;
	setLayouts[2] = pipelineLayoutHash.uniformLayout;

	pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCreateInfo.pNext = NULL;
	pipelineLayoutCreateInfo.flags = 0;
	pipelineLayoutCreateInfo.setLayoutCount = 3;
	pipelineLayoutCreateInfo.pSetLayouts = setLayouts;
	pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
	pipelineLayoutCreateInfo.pPushConstantRanges = NULL;

	vulkanResult = renderer->vkCreatePipelineLayout(
		renderer->logicalDevice,
		&pipelineLayoutCreateInfo,
		NULL,
		&vulkanComputePipelineLayout->pipelineLayout
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreatePipelineLayout", vulkanResult);
		return NULL;
	}

	ComputePipelineLayoutHashArray_Insert(
		&renderer->computePipelineLayoutHashTable,
		pipelineLayoutHash,
		vulkanComputePipelineLayout
	);

	/* If the binding count is 0
	 * we can just bind the same descriptor set
	 * so no cache is needed
	 */

	if (bufferBindingCount == 0)
	{
		vulkanComputePipelineLayout->bufferDescriptorSetCache = NULL;
	}
	else
	{
		vulkanComputePipelineLayout->bufferDescriptorSetCache =
			VULKAN_INTERNAL_CreateBufferDescriptorSetCache(
				renderer,
				VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				pipelineLayoutHash.bufferLayout,
				bufferBindingCount
			);
	}

	if (imageBindingCount == 0)
	{
		vulkanComputePipelineLayout->imageDescriptorSetCache = NULL;
	}
	else
	{
		vulkanComputePipelineLayout->imageDescriptorSetCache =
			VULKAN_INTERNAL_CreateImageDescriptorSetCache(
				renderer,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				pipelineLayoutHash.imageLayout,
				imageBindingCount
			);
	}

	return vulkanComputePipelineLayout;
}

static Refresh_ComputePipeline* VULKAN_CreateComputePipeline(
	Refresh_Renderer *driverData,
	Refresh_ComputePipelineCreateInfo *pipelineCreateInfo
) {
	VkComputePipelineCreateInfo computePipelineCreateInfo;
	VkPipelineShaderStageCreateInfo pipelineShaderStageCreateInfo;

	VkDescriptorSetAllocateInfo descriptorSetAllocateInfo;
	VkDescriptorBufferInfo uniformBufferInfo;
	VkWriteDescriptorSet writeDescriptorSet;

	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanComputePipeline *vulkanComputePipeline = SDL_malloc(sizeof(VulkanComputePipeline));

	pipelineShaderStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	pipelineShaderStageCreateInfo.pNext = NULL;
	pipelineShaderStageCreateInfo.flags = 0;
	pipelineShaderStageCreateInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	pipelineShaderStageCreateInfo.module = (VkShaderModule) pipelineCreateInfo->computeShaderState.shaderModule;
	pipelineShaderStageCreateInfo.pName = pipelineCreateInfo->computeShaderState.entryPointName;
	pipelineShaderStageCreateInfo.pSpecializationInfo = NULL;

	vulkanComputePipeline->pipelineLayout = VULKAN_INTERNAL_FetchComputePipelineLayout(
		renderer,
		pipelineCreateInfo->pipelineLayoutCreateInfo.bufferBindingCount,
		pipelineCreateInfo->pipelineLayoutCreateInfo.imageBindingCount
	);

	computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	computePipelineCreateInfo.pNext = NULL;
	computePipelineCreateInfo.flags = 0;
	computePipelineCreateInfo.stage = pipelineShaderStageCreateInfo;
	computePipelineCreateInfo.layout =
		vulkanComputePipeline->pipelineLayout->pipelineLayout;
	computePipelineCreateInfo.basePipelineHandle = NULL;
	computePipelineCreateInfo.basePipelineIndex = 0;

	renderer->vkCreateComputePipelines(
		renderer->logicalDevice,
		NULL,
		1,
		&computePipelineCreateInfo,
		NULL,
		&vulkanComputePipeline->pipeline
	);

	vulkanComputePipeline->computeUBOBlockSize =
		VULKAN_INTERNAL_NextHighestAlignment(
			pipelineCreateInfo->computeShaderState.uniformBufferSize,
			renderer->minUBOAlignment
		);

	descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSetAllocateInfo.pNext = NULL;
	descriptorSetAllocateInfo.descriptorPool = renderer->defaultDescriptorPool;
	descriptorSetAllocateInfo.descriptorSetCount = 1;
	descriptorSetAllocateInfo.pSetLayouts = &renderer->computeParamLayout;

	renderer->vkAllocateDescriptorSets(
		renderer->logicalDevice,
		&descriptorSetAllocateInfo,
		&vulkanComputePipeline->computeUBODescriptorSet
	);

	if (vulkanComputePipeline->computeUBOBlockSize == 0)
	{
		uniformBufferInfo.buffer = renderer->dummyComputeUniformBuffer->subBuffers[0]->buffer;
		uniformBufferInfo.offset = 0;
		uniformBufferInfo.range = renderer->dummyComputeUniformBuffer->subBuffers[0]->size;
	}
	else
	{
		uniformBufferInfo.buffer = renderer->computeUBO->subBuffers[0]->buffer;
		uniformBufferInfo.offset = 0;
		uniformBufferInfo.range = vulkanComputePipeline->computeUBOBlockSize;
	}

	writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writeDescriptorSet.pNext = NULL;
	writeDescriptorSet.descriptorCount = 1;
	writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	writeDescriptorSet.dstArrayElement = 0;
	writeDescriptorSet.dstBinding = 0;
	writeDescriptorSet.dstSet = vulkanComputePipeline->computeUBODescriptorSet;
	writeDescriptorSet.pBufferInfo = &uniformBufferInfo;
	writeDescriptorSet.pImageInfo = NULL;
	writeDescriptorSet.pTexelBufferView = NULL;

	renderer->vkUpdateDescriptorSets(
		renderer->logicalDevice,
		1,
		&writeDescriptorSet,
		0,
		NULL
	);

	return (Refresh_ComputePipeline*) vulkanComputePipeline;
}

static Refresh_Sampler* VULKAN_CreateSampler(
	Refresh_Renderer *driverData,
	Refresh_SamplerStateCreateInfo *samplerStateCreateInfo
) {
	VkResult vulkanResult;
	VkSampler sampler;

	VulkanRenderer* renderer = (VulkanRenderer*)driverData;

	VkSamplerCreateInfo vkSamplerCreateInfo;
	vkSamplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	vkSamplerCreateInfo.pNext = NULL;
	vkSamplerCreateInfo.flags = 0;
	vkSamplerCreateInfo.magFilter = RefreshToVK_Filter[
		samplerStateCreateInfo->magFilter
	];
	vkSamplerCreateInfo.minFilter = RefreshToVK_Filter[
		samplerStateCreateInfo->minFilter
	];
	vkSamplerCreateInfo.mipmapMode = RefreshToVK_SamplerMipmapMode[
		samplerStateCreateInfo->mipmapMode
	];
	vkSamplerCreateInfo.addressModeU = RefreshToVK_SamplerAddressMode[
		samplerStateCreateInfo->addressModeU
	];
	vkSamplerCreateInfo.addressModeV = RefreshToVK_SamplerAddressMode[
		samplerStateCreateInfo->addressModeV
	];
	vkSamplerCreateInfo.addressModeW = RefreshToVK_SamplerAddressMode[
		samplerStateCreateInfo->addressModeW
	];
	vkSamplerCreateInfo.mipLodBias = samplerStateCreateInfo->mipLodBias;
	vkSamplerCreateInfo.anisotropyEnable = samplerStateCreateInfo->anisotropyEnable;
	vkSamplerCreateInfo.maxAnisotropy = samplerStateCreateInfo->maxAnisotropy;
	vkSamplerCreateInfo.compareEnable = samplerStateCreateInfo->compareEnable;
	vkSamplerCreateInfo.compareOp = RefreshToVK_CompareOp[
		samplerStateCreateInfo->compareOp
	];
	vkSamplerCreateInfo.minLod = samplerStateCreateInfo->minLod;
	vkSamplerCreateInfo.maxLod = samplerStateCreateInfo->maxLod;
	vkSamplerCreateInfo.borderColor = RefreshToVK_BorderColor[
		samplerStateCreateInfo->borderColor
	];
	vkSamplerCreateInfo.unnormalizedCoordinates = VK_FALSE;

	vulkanResult = renderer->vkCreateSampler(
		renderer->logicalDevice,
		&vkSamplerCreateInfo,
		NULL,
		&sampler
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateSampler", vulkanResult);
		return NULL;
	}

	return (Refresh_Sampler*) sampler;
}

static Refresh_Framebuffer* VULKAN_CreateFramebuffer(
	Refresh_Renderer *driverData,
	Refresh_FramebufferCreateInfo *framebufferCreateInfo
) {
	VkResult vulkanResult;
	VkFramebufferCreateInfo vkFramebufferCreateInfo;

	VkImageView *imageViews;
	uint32_t colorAttachmentCount = framebufferCreateInfo->colorTargetCount;
	uint32_t attachmentCount = colorAttachmentCount;
	uint32_t i;

	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanFramebuffer *vulkanFramebuffer = (VulkanFramebuffer*) SDL_malloc(sizeof(VulkanFramebuffer));

	if (framebufferCreateInfo->pDepthStencilTarget != NULL)
	{
		attachmentCount += 1;
	}

	imageViews = SDL_stack_alloc(VkImageView, attachmentCount);

	for (i = 0; i < colorAttachmentCount; i += 1)
	{
		imageViews[i] = ((VulkanColorTarget*)framebufferCreateInfo->pColorTargets[i])->view;
	}

	if (framebufferCreateInfo->pDepthStencilTarget != NULL)
	{
		imageViews[colorAttachmentCount] = ((VulkanDepthStencilTarget*)framebufferCreateInfo->pDepthStencilTarget)->view;
	}

	vkFramebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	vkFramebufferCreateInfo.pNext = NULL;
	vkFramebufferCreateInfo.flags = 0;
	vkFramebufferCreateInfo.renderPass = (VkRenderPass) framebufferCreateInfo->renderPass;
	vkFramebufferCreateInfo.attachmentCount = attachmentCount;
	vkFramebufferCreateInfo.pAttachments = imageViews;
	vkFramebufferCreateInfo.width = framebufferCreateInfo->width;
	vkFramebufferCreateInfo.height = framebufferCreateInfo->height;
	vkFramebufferCreateInfo.layers = 1;

	vulkanResult = renderer->vkCreateFramebuffer(
		renderer->logicalDevice,
		&vkFramebufferCreateInfo,
		NULL,
		&vulkanFramebuffer->framebuffer
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateFramebuffer", vulkanResult);
		SDL_stack_free(imageViews);
		return NULL;
	}

	for (i = 0; i < colorAttachmentCount; i += 1)
	{
		vulkanFramebuffer->colorTargets[i] =
			(VulkanColorTarget*) framebufferCreateInfo->pColorTargets[i];
	}

	vulkanFramebuffer->colorTargetCount = colorAttachmentCount;
	vulkanFramebuffer->depthStencilTarget =
		(VulkanDepthStencilTarget*) framebufferCreateInfo->pDepthStencilTarget;

	vulkanFramebuffer->width = framebufferCreateInfo->width;
	vulkanFramebuffer->height = framebufferCreateInfo->height;

	SDL_stack_free(imageViews);
	return (Refresh_Framebuffer*) vulkanFramebuffer;
}

static Refresh_ShaderModule* VULKAN_CreateShaderModule(
	Refresh_Renderer *driverData,
	Refresh_ShaderModuleCreateInfo *shaderModuleCreateInfo
) {
	VkResult vulkanResult;
	VkShaderModule shaderModule;
	VkShaderModuleCreateInfo vkShaderModuleCreateInfo;
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;

	vkShaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	vkShaderModuleCreateInfo.pNext = NULL;
	vkShaderModuleCreateInfo.flags = 0;
	vkShaderModuleCreateInfo.codeSize = shaderModuleCreateInfo->codeSize;
	vkShaderModuleCreateInfo.pCode = (uint32_t*) shaderModuleCreateInfo->byteCode;

	vulkanResult = renderer->vkCreateShaderModule(
		renderer->logicalDevice,
		&vkShaderModuleCreateInfo,
		NULL,
		&shaderModule
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateShaderModule", vulkanResult);
		Refresh_LogError("Failed to create shader module!");
		return NULL;
	}

	return (Refresh_ShaderModule*) shaderModule;
}

/* texture should be an alloc'd but uninitialized VulkanTexture */
static uint8_t VULKAN_INTERNAL_CreateTexture(
	VulkanRenderer *renderer,
	uint32_t width,
	uint32_t height,
	uint32_t depth,
	uint32_t isCube,
	VkSampleCountFlagBits samples,
	uint32_t levelCount,
	VkFormat format,
	VkImageAspectFlags aspectMask,
	VkImageTiling tiling,
	VkImageType imageType,
	VkImageUsageFlags imageUsageFlags,
	Refresh_TextureUsageFlags textureUsageFlags,
	VulkanTexture *texture
) {
	VkResult vulkanResult;
	VkImageCreateInfo imageCreateInfo;
	VkImageCreateFlags imageCreateFlags = 0;
	VkImageViewCreateInfo imageViewCreateInfo;
	uint8_t findMemoryResult;
	uint8_t is3D = depth > 1 ? 1 : 0;
	uint8_t layerCount = isCube ? 6 : 1;
	VkComponentMapping swizzle = IDENTITY_SWIZZLE;

	texture->isCube = 0;
	texture->is3D = 0;

	if (isCube)
	{
		imageCreateFlags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		texture->isCube = 1;
	}
	else if (is3D)
	{
		imageCreateFlags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
		texture->is3D = 1;
	}

	imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageCreateInfo.pNext = NULL;
	imageCreateInfo.flags = imageCreateFlags;
	imageCreateInfo.imageType = imageType;
	imageCreateInfo.format = format;
	imageCreateInfo.extent.width = width;
	imageCreateInfo.extent.height = height;
	imageCreateInfo.extent.depth = depth;
	imageCreateInfo.mipLevels = levelCount;
	imageCreateInfo.arrayLayers = layerCount;
	imageCreateInfo.samples = samples;
	imageCreateInfo.tiling = tiling;
	imageCreateInfo.usage = imageUsageFlags;
	// FIXME: would this interfere with pixel data sharing?
	imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageCreateInfo.queueFamilyIndexCount = 0;
	imageCreateInfo.pQueueFamilyIndices = NULL;
	imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	vulkanResult = renderer->vkCreateImage(
		renderer->logicalDevice,
		&imageCreateInfo,
		NULL,
		&texture->image
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateImage", vulkanResult);
		Refresh_LogError("Failed to create texture!");
	}

	findMemoryResult = VULKAN_INTERNAL_FindAvailableMemory(
		renderer,
		VK_NULL_HANDLE,
		texture->image,
		&texture->allocation,
		&texture->offset,
		&texture->memorySize
	);

	/* No device memory available, time to die */
	if (findMemoryResult == 0 || findMemoryResult == 2)
	{
		Refresh_LogError("Failed to find texture memory!");
		return 0;
	}

	SDL_LockMutex(texture->allocation->memoryLock);

	vulkanResult = renderer->vkBindImageMemory(
		renderer->logicalDevice,
		texture->image,
		texture->allocation->memory,
		texture->offset
	);

	SDL_UnlockMutex(texture->allocation->memoryLock);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkBindImageMemory", vulkanResult);
		Refresh_LogError("Failed to bind texture memory!");
		return 0;
	}

	imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imageViewCreateInfo.pNext = NULL;
	imageViewCreateInfo.flags = 0;
	imageViewCreateInfo.image = texture->image;
	imageViewCreateInfo.format = format;
	imageViewCreateInfo.components = swizzle;
	imageViewCreateInfo.subresourceRange.aspectMask = aspectMask;
	imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
	imageViewCreateInfo.subresourceRange.levelCount = levelCount;
	imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
	imageViewCreateInfo.subresourceRange.layerCount = layerCount;

	if (isCube)
	{
		imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
	}
	else if (imageType == VK_IMAGE_TYPE_2D)
	{
		imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	}
	else if (imageType == VK_IMAGE_TYPE_3D)
	{
		imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
	}
	else
	{
		Refresh_LogError("invalid image type: %u", imageType);
	}

	vulkanResult = renderer->vkCreateImageView(
		renderer->logicalDevice,
		&imageViewCreateInfo,
		NULL,
		&texture->view
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateImageView", vulkanResult);
		Refresh_LogError("Failed to create texture image view");
		return 0;
	}

	texture->dimensions.width = width;
	texture->dimensions.height = height;
	texture->depth = depth;
	texture->format = format;
	texture->levelCount = levelCount;
	texture->layerCount = layerCount;
	texture->resourceAccessType = RESOURCE_ACCESS_NONE;
	texture->queueFamilyIndex = renderer->queueFamilyIndices.graphicsFamily;
	texture->usageFlags = textureUsageFlags;

	return 1;
}

static Refresh_Texture* VULKAN_CreateTexture(
	Refresh_Renderer *driverData,
	Refresh_TextureCreateInfo *textureCreateInfo
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTexture *result;
	VkImageUsageFlags imageUsageFlags = (
		VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT
	);

	if (textureCreateInfo->usageFlags & REFRESH_TEXTUREUSAGE_COLOR_TARGET_BIT)
	{
		imageUsageFlags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}

	result = (VulkanTexture*) SDL_malloc(sizeof(VulkanTexture));

	VULKAN_INTERNAL_CreateTexture(
		renderer,
		textureCreateInfo->width,
		textureCreateInfo->height,
		textureCreateInfo->depth,
		textureCreateInfo->isCube,
		VK_SAMPLE_COUNT_1_BIT,
		textureCreateInfo->levelCount,
		RefreshToVK_SurfaceFormat[textureCreateInfo->format],
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_TYPE_2D,
		imageUsageFlags,
		textureCreateInfo->usageFlags,
		result
	);
	result->colorFormat = textureCreateInfo->format;

	return (Refresh_Texture*) result;
}

static Refresh_ColorTarget* VULKAN_CreateColorTarget(
	Refresh_Renderer *driverData,
	Refresh_SampleCount multisampleCount,
	Refresh_TextureSlice *textureSlice
) {
	VkResult vulkanResult;
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanColorTarget *colorTarget = (VulkanColorTarget*) SDL_malloc(sizeof(VulkanColorTarget));
	VkImageViewCreateInfo imageViewCreateInfo;
	VkComponentMapping swizzle = IDENTITY_SWIZZLE;

	colorTarget->texture = (VulkanTexture*) textureSlice->texture;
	colorTarget->layer = textureSlice->layer;
	colorTarget->multisampleTexture = NULL;
	colorTarget->multisampleCount = 1;

	/* create resolve target for multisample */
	if (multisampleCount > REFRESH_SAMPLECOUNT_1)
	{
		colorTarget->multisampleTexture =
			(VulkanTexture*) SDL_malloc(sizeof(VulkanTexture));

		VULKAN_INTERNAL_CreateTexture(
			renderer,
			colorTarget->texture->dimensions.width,
			colorTarget->texture->dimensions.height,
			1,
			0,
			RefreshToVK_SampleCount[multisampleCount],
			1,
			colorTarget->texture->format,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_TYPE_2D,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			REFRESH_TEXTUREUSAGE_COLOR_TARGET_BIT,
			colorTarget->multisampleTexture
		);
		colorTarget->multisampleTexture->colorFormat = colorTarget->texture->colorFormat;
		colorTarget->multisampleCount = multisampleCount;
	}

	/* create framebuffer compatible views for RenderTarget */
	imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imageViewCreateInfo.pNext = NULL;
	imageViewCreateInfo.flags = 0;
	imageViewCreateInfo.image = colorTarget->texture->image;
	imageViewCreateInfo.format = colorTarget->texture->format;
	imageViewCreateInfo.components = swizzle;
	imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
	imageViewCreateInfo.subresourceRange.levelCount = 1;
	imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
	if (colorTarget->texture->is3D)
	{
		imageViewCreateInfo.subresourceRange.baseArrayLayer = textureSlice->depth;
	}
	else if (colorTarget->texture->isCube)
	{
		imageViewCreateInfo.subresourceRange.baseArrayLayer = textureSlice->layer;
	}
	imageViewCreateInfo.subresourceRange.layerCount = 1;
	imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;

	vulkanResult = renderer->vkCreateImageView(
		renderer->logicalDevice,
		&imageViewCreateInfo,
		NULL,
		&colorTarget->view
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult(
			"vkCreateImageView",
			vulkanResult
		);
		Refresh_LogError("Failed to create color attachment image view");
		return NULL;
	}

	return (Refresh_ColorTarget*) colorTarget;
}

static Refresh_DepthStencilTarget* VULKAN_CreateDepthStencilTarget(
	Refresh_Renderer *driverData,
	uint32_t width,
	uint32_t height,
	Refresh_DepthFormat format
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanDepthStencilTarget *depthStencilTarget =
		(VulkanDepthStencilTarget*) SDL_malloc(
			sizeof(VulkanDepthStencilTarget)
		);

	VulkanTexture *texture =
		(VulkanTexture*) SDL_malloc(sizeof(VulkanTexture));

	VkImageAspectFlags imageAspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;

	VkImageUsageFlags imageUsageFlags =
		VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	if (DepthFormatContainsStencil(RefreshToVK_DepthFormat[format]))
	{
		imageAspectFlags |= VK_IMAGE_ASPECT_STENCIL_BIT;
	}

	VULKAN_INTERNAL_CreateTexture(
		renderer,
		width,
		height,
		1,
		0,
		VK_SAMPLE_COUNT_1_BIT,
		1,
		RefreshToVK_DepthFormat[format],
		imageAspectFlags,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_TYPE_2D,
		imageUsageFlags,
		0,
		texture
	);
	texture->depthStencilFormat = format;

	depthStencilTarget->texture = texture;
	depthStencilTarget->view = texture->view;

    return (Refresh_DepthStencilTarget*) depthStencilTarget;
}

static Refresh_Buffer* VULKAN_CreateBuffer(
	Refresh_Renderer *driverData,
	Refresh_BufferUsageFlags usageFlags,
	uint32_t sizeInBytes
) {
	VulkanBuffer *buffer = (VulkanBuffer*) SDL_malloc(sizeof(VulkanBuffer));

	VkBufferUsageFlags vulkanUsageFlags =
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	if (usageFlags & REFRESH_BUFFERUSAGE_VERTEX_BIT)
	{
		vulkanUsageFlags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	}

	if (usageFlags & REFRESH_BUFFERUSAGE_INDEX_BIT)
	{
		vulkanUsageFlags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	}

	if (usageFlags & REFRESH_BUFFERUSAGE_COMPUTE_BIT)
	{
		vulkanUsageFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	}

	if(!VULKAN_INTERNAL_CreateBuffer(
		(VulkanRenderer*) driverData,
		sizeInBytes,
		RESOURCE_ACCESS_VERTEX_BUFFER,
		vulkanUsageFlags,
		SUB_BUFFER_COUNT,
		buffer
	)) {
		Refresh_LogError("Failed to create vertex buffer!");
		return NULL;
	}

	return (Refresh_Buffer*) buffer;
}

/* Setters */

static void VULKAN_INTERNAL_MaybeExpandStagingBuffer(
	VulkanRenderer *renderer,
	uint32_t textureSize
) {
	VkDeviceSize currentStagingSize = renderer->textureStagingBuffer->size;

	if (renderer->textureStagingBufferOffset + textureSize <= renderer->textureStagingBuffer->size)
	{
		return;
	}

	/* not enough room in the staging buffer, time to flush */
	VULKAN_INTERNAL_FlushTransfers(renderer);

	/* double staging buffer size up to max */
	if (currentStagingSize * 2 <= MAX_TEXTURE_STAGING_SIZE)
	{
		VULKAN_INTERNAL_DestroyTextureStagingBuffer(renderer);

		renderer->textureStagingBuffer = (VulkanBuffer*) SDL_malloc(sizeof(VulkanBuffer));

		if (!VULKAN_INTERNAL_CreateBuffer(
			renderer,
			currentStagingSize * 2,
			RESOURCE_ACCESS_MEMORY_TRANSFER_READ_WRITE,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			1,
			renderer->textureStagingBuffer
		)) {
			Refresh_LogError("Failed to expand texture staging buffer!");
			return;
		}
	}
}

static void VULKAN_INTERNAL_MaybeBeginTransferCommandBuffer(
	VulkanRenderer *renderer
) {
	VkCommandBufferBeginInfo transferCommandBufferBeginInfo;

	if (!renderer->pendingTransfer)
	{
		transferCommandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		transferCommandBufferBeginInfo.pNext = NULL;
		transferCommandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		transferCommandBufferBeginInfo.pInheritanceInfo = NULL;

		renderer->vkBeginCommandBuffer(
			renderer->transferCommandBuffers[renderer->frameIndex],
			&transferCommandBufferBeginInfo
		);

		renderer->pendingTransfer = 1;
	}
}

static void VULKAN_INTERNAL_EndTransferCommandBuffer(
	VulkanRenderer *renderer
) {
	if (renderer->pendingTransfer)
	{
		renderer->vkEndCommandBuffer(
			renderer->transferCommandBuffers[renderer->frameIndex]
		);
	}
}

/* Hard sync point! */
static void VULKAN_INTERNAL_FlushTransfers(
	VulkanRenderer *renderer
) {
	VkSubmitInfo transferSubmitInfo;
	VkResult vulkanResult;

	if (renderer->pendingTransfer)
	{
		VULKAN_INTERNAL_EndTransferCommandBuffer(renderer);

		transferSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		transferSubmitInfo.pNext = NULL;
		transferSubmitInfo.commandBufferCount = 1;
		transferSubmitInfo.pCommandBuffers = &renderer->transferCommandBuffers[renderer->frameIndex];
		transferSubmitInfo.pWaitDstStageMask = NULL;
		transferSubmitInfo.pWaitSemaphores = NULL;
		transferSubmitInfo.waitSemaphoreCount = 0;
		transferSubmitInfo.pSignalSemaphores = NULL;
		transferSubmitInfo.signalSemaphoreCount = 0;

		/* Wait for the previous submission to complete */
		vulkanResult = renderer->vkWaitForFences(
			renderer->logicalDevice,
			1,
			&renderer->inFlightFence,
			VK_TRUE,
			UINT64_MAX
		);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkWaitForFences", vulkanResult);
			return;
		}

		renderer->vkResetFences(
			renderer->logicalDevice,
			1,
			&renderer->inFlightFence
		);

		/* Submit transfers */
		vulkanResult = renderer->vkQueueSubmit(
			renderer->transferQueue,
			1,
			&transferSubmitInfo,
			renderer->inFlightFence
		);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkQueueSubmit", vulkanResult);
			return;
		}

		/* Wait again */
		vulkanResult = renderer->vkWaitForFences(
			renderer->logicalDevice,
			1,
			&renderer->inFlightFence,
			VK_TRUE,
			UINT64_MAX
		);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkWaitForFences", vulkanResult);
			return;
		}

		renderer->pendingTransfer = 0;
		renderer->textureStagingBufferOffset = 0;
	}
}

static void VULKAN_SetTextureData(
	Refresh_Renderer *driverData,
	Refresh_TextureSlice *textureSlice,
	void *data,
	uint32_t dataLengthInBytes
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTexture *vulkanTexture = (VulkanTexture*) textureSlice->texture;

	VkCommandBuffer commandBuffer = renderer->transferCommandBuffers[renderer->frameIndex];
	VkBufferImageCopy imageCopy;
	uint8_t *stagingBufferPointer;

	SDL_LockMutex(renderer->stagingLock);

	VULKAN_INTERNAL_MaybeExpandStagingBuffer(renderer, dataLengthInBytes);
	VULKAN_INTERNAL_MaybeBeginTransferCommandBuffer(renderer);

	stagingBufferPointer =
		renderer->textureStagingBuffer->subBuffers[0]->allocation->mapPointer +
		renderer->textureStagingBuffer->subBuffers[0]->offset +
		renderer->textureStagingBufferOffset;

	SDL_memcpy(
		stagingBufferPointer,
		data,
		dataLengthInBytes
	);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		commandBuffer,
		RESOURCE_ACCESS_TRANSFER_WRITE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		textureSlice->layer,
		1,
		textureSlice->level,
		1,
		0,
		vulkanTexture->image,
		&vulkanTexture->resourceAccessType
	);

	imageCopy.imageExtent.width = textureSlice->rectangle.w;
	imageCopy.imageExtent.height = textureSlice->rectangle.h;
	imageCopy.imageExtent.depth = 1;
	imageCopy.imageOffset.x = textureSlice->rectangle.x;
	imageCopy.imageOffset.y = textureSlice->rectangle.y;
	imageCopy.imageOffset.z = textureSlice->depth;
	imageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageCopy.imageSubresource.baseArrayLayer = textureSlice->layer;
	imageCopy.imageSubresource.layerCount = 1;
	imageCopy.imageSubresource.mipLevel = textureSlice->level;
	imageCopy.bufferOffset = renderer->textureStagingBufferOffset;
	imageCopy.bufferRowLength = 0;
	imageCopy.bufferImageHeight = 0;

	renderer->vkCmdCopyBufferToImage(
		commandBuffer,
		renderer->textureStagingBuffer->subBuffers[0]->buffer,
		vulkanTexture->image,
		AccessMap[vulkanTexture->resourceAccessType].imageLayout,
		1,
		&imageCopy
	);

	renderer->textureStagingBufferOffset += dataLengthInBytes;

	if (vulkanTexture->usageFlags & REFRESH_TEXTUREUSAGE_SAMPLER_BIT)
	{
		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			commandBuffer,
			RESOURCE_ACCESS_ANY_SHADER_READ_SAMPLED_IMAGE,
			VK_IMAGE_ASPECT_COLOR_BIT,
			textureSlice->layer,
			1,
			textureSlice->level,
			1,
			0,
			vulkanTexture->image,
			&vulkanTexture->resourceAccessType
		);
	}

	SDL_UnlockMutex(renderer->stagingLock);
}

static void VULKAN_SetTextureDataYUV(
	Refresh_Renderer *driverData,
	Refresh_Texture *y,
	Refresh_Texture *u,
	Refresh_Texture *v,
	uint32_t yWidth,
	uint32_t yHeight,
	uint32_t uvWidth,
	uint32_t uvHeight,
	void* data,
	uint32_t dataLength
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTexture *tex;

	VkCommandBuffer commandBuffer = renderer->transferCommandBuffers[renderer->frameIndex];
	uint8_t *dataPtr = (uint8_t*) data;
	int32_t yDataLength = BytesPerImage(yWidth, yHeight, REFRESH_COLORFORMAT_R8);
	int32_t uvDataLength = BytesPerImage(uvWidth, uvHeight, REFRESH_COLORFORMAT_R8);
	VkBufferImageCopy imageCopy;
	uint8_t * stagingBufferPointer;

	SDL_LockMutex(renderer->stagingLock);

	VULKAN_INTERNAL_MaybeExpandStagingBuffer(renderer, dataLength);
	VULKAN_INTERNAL_MaybeBeginTransferCommandBuffer(renderer);

	stagingBufferPointer =
		renderer->textureStagingBuffer->subBuffers[0]->allocation->mapPointer +
		renderer->textureStagingBuffer->subBuffers[0]->offset +
		renderer->textureStagingBufferOffset;

	/* Initialize values that are the same for Y, U, and V */

	imageCopy.imageExtent.depth = 1;
	imageCopy.imageOffset.x = 0;
	imageCopy.imageOffset.y = 0;
	imageCopy.imageOffset.z = 0;
	imageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageCopy.imageSubresource.baseArrayLayer = 0;
	imageCopy.imageSubresource.layerCount = 1;
	imageCopy.imageSubresource.mipLevel = 0;

	/* Y */

	tex = (VulkanTexture*) y;

	SDL_memcpy(
		stagingBufferPointer,
		dataPtr,
		yDataLength
	);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		commandBuffer,
		RESOURCE_ACCESS_TRANSFER_WRITE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		tex->layerCount,
		0,
		tex->levelCount,
		0,
		tex->image,
		&tex->resourceAccessType
	);


	imageCopy.imageExtent.width = yWidth;
	imageCopy.imageExtent.height = yHeight;
	imageCopy.bufferOffset = renderer->textureStagingBufferOffset;
	imageCopy.bufferRowLength = yWidth;
	imageCopy.bufferImageHeight = yHeight;

	renderer->vkCmdCopyBufferToImage(
		commandBuffer,
		renderer->textureStagingBuffer->subBuffers[0]->buffer,
		tex->image,
		AccessMap[tex->resourceAccessType].imageLayout,
		1,
		&imageCopy
	);

	/* These apply to both U and V */

	imageCopy.imageExtent.width = uvWidth;
	imageCopy.imageExtent.height = uvHeight;
	imageCopy.bufferRowLength = uvWidth;
	imageCopy.bufferImageHeight = uvHeight;

	/* U */

	imageCopy.bufferOffset = renderer->textureStagingBufferOffset + yDataLength;

	tex = (VulkanTexture*) u;

	SDL_memcpy(
		stagingBufferPointer + yDataLength,
		dataPtr + yDataLength,
		uvDataLength
	);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		commandBuffer,
		RESOURCE_ACCESS_TRANSFER_WRITE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		tex->layerCount,
		0,
		tex->levelCount,
		0,
		tex->image,
		&tex->resourceAccessType
	);

	renderer->vkCmdCopyBufferToImage(
		commandBuffer,
		renderer->textureStagingBuffer->subBuffers[0]->buffer,
		tex->image,
		AccessMap[tex->resourceAccessType].imageLayout,
		1,
		&imageCopy
	);

	/* V */

	imageCopy.bufferOffset = renderer->textureStagingBufferOffset + uvDataLength;

	tex = (VulkanTexture*) v;

	SDL_memcpy(
		stagingBufferPointer + yDataLength + uvDataLength,
		dataPtr + yDataLength + uvDataLength,
		uvDataLength
	);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		commandBuffer,
		RESOURCE_ACCESS_TRANSFER_WRITE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		tex->layerCount,
		0,
		tex->levelCount,
		0,
		tex->image,
		&tex->resourceAccessType
	);

	renderer->vkCmdCopyBufferToImage(
		commandBuffer,
		renderer->textureStagingBuffer->subBuffers[0]->buffer,
		tex->image,
		AccessMap[tex->resourceAccessType].imageLayout,
		1,
		&imageCopy
	);

	renderer->textureStagingBufferOffset += dataLength;

	if (tex->usageFlags & REFRESH_TEXTUREUSAGE_SAMPLER_BIT)
	{
		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			commandBuffer,
			RESOURCE_ACCESS_ANY_SHADER_READ_SAMPLED_IMAGE,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0,
			tex->layerCount,
			0,
			tex->levelCount,
			0,
			tex->image,
			&tex->resourceAccessType
		);
	}

	SDL_UnlockMutex(renderer->stagingLock);
}

static void VULKAN_INTERNAL_BlitImage(
	VulkanRenderer *renderer,
	VkCommandBuffer commandBuffer,
	Refresh_Rect *sourceRectangle,
	uint32_t sourceDepth,
	uint32_t sourceLayer,
	uint32_t sourceLevel,
	VkImage sourceImage,
	VulkanResourceAccessType *currentSourceAccessType,
	VulkanResourceAccessType nextSourceAccessType,
	Refresh_Rect *destinationRectangle,
	uint32_t destinationDepth,
	uint32_t destinationLayer,
	uint32_t destinationLevel,
	VkImage destinationImage,
	VulkanResourceAccessType *currentDestinationAccessType,
	VulkanResourceAccessType nextDestinationAccessType,
	VkFilter filter
) {
	VkImageBlit blit;

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		commandBuffer,
		RESOURCE_ACCESS_TRANSFER_READ,
		VK_IMAGE_ASPECT_COLOR_BIT,
		sourceLayer,
		1,
		sourceLevel,
		1,
		0,
		sourceImage,
		currentSourceAccessType
	);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		commandBuffer,
		RESOURCE_ACCESS_TRANSFER_WRITE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		destinationLayer,
		1,
		destinationLevel,
		1,
		0,
		destinationImage,
		currentDestinationAccessType
	);

	blit.srcOffsets[0].x = sourceRectangle->x;
	blit.srcOffsets[0].y = sourceRectangle->y;
	blit.srcOffsets[0].z = sourceDepth;
	blit.srcOffsets[1].x = sourceRectangle->x + sourceRectangle->w;
	blit.srcOffsets[1].y = sourceRectangle->y + sourceRectangle->h;
	blit.srcOffsets[1].z = 1;

	blit.srcSubresource.mipLevel = sourceLevel;
	blit.srcSubresource.baseArrayLayer = sourceLayer;
	blit.srcSubresource.layerCount = 1;
	blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	blit.dstOffsets[0].x = destinationRectangle->x;
	blit.dstOffsets[0].y = destinationRectangle->y;
	blit.dstOffsets[0].z = destinationDepth;
	blit.dstOffsets[1].x = destinationRectangle->x + destinationRectangle->w;
	blit.dstOffsets[1].y = destinationRectangle->y + destinationRectangle->h;
	blit.dstOffsets[1].z = 1;

	blit.dstSubresource.mipLevel = destinationLevel;
	blit.dstSubresource.baseArrayLayer = destinationLayer;
	blit.dstSubresource.layerCount = 1;
	blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	renderer->vkCmdBlitImage(
		commandBuffer,
		sourceImage,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		destinationImage,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&blit,
		filter
	);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		commandBuffer,
		nextSourceAccessType,
		VK_IMAGE_ASPECT_COLOR_BIT,
		sourceLayer,
		1,
		sourceLevel,
		1,
		0,
		sourceImage,
		currentSourceAccessType
	);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		commandBuffer,
		nextDestinationAccessType,
		VK_IMAGE_ASPECT_COLOR_BIT,
		destinationLayer,
		1,
		destinationLevel,
		1,
		0,
		destinationImage,
		currentDestinationAccessType
	);
}

REFRESHAPI void VULKAN_CopyTextureToTexture(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_TextureSlice *sourceTextureSlice,
	Refresh_TextureSlice *destinationTextureSlice,
	Refresh_Filter filter
) {
	VulkanRenderer *renderer = (VulkanRenderer*)driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	VulkanTexture *sourceTexture = (VulkanTexture*) sourceTextureSlice->texture;
	VulkanTexture *destinationTexture = (VulkanTexture*) destinationTextureSlice->texture;

	VULKAN_INTERNAL_BlitImage(
		renderer,
		vulkanCommandBuffer->commandBuffer,
		&sourceTextureSlice->rectangle,
		sourceTextureSlice->depth,
		sourceTextureSlice->layer,
		sourceTextureSlice->level,
		sourceTexture->image,
		&sourceTexture->resourceAccessType,
		sourceTexture->resourceAccessType,
		&destinationTextureSlice->rectangle,
		destinationTextureSlice->depth,
		destinationTextureSlice->layer,
		destinationTextureSlice->level,
		destinationTexture->image,
		&destinationTexture->resourceAccessType,
		destinationTexture->resourceAccessType,
		RefreshToVK_Filter[filter]
	);
}

static void VULKAN_SetBufferData(
	Refresh_Renderer *driverData,
	Refresh_Buffer *buffer,
	uint32_t offsetInBytes,
	void* data,
	uint32_t dataLength
) {
	VulkanRenderer* renderer = (VulkanRenderer*)driverData;
	VulkanBuffer* vulkanBuffer = (VulkanBuffer*)buffer;
	uint32_t i;

	#define CURIDX vulkanBuffer->currentSubBufferIndex
	#define SUBBUF vulkanBuffer->subBuffers[CURIDX]

	/* If buffer has not been bound this frame, set the first unbound index */
	if (!vulkanBuffer->bound)
	{
		for (i = 0; i < vulkanBuffer->subBufferCount; i += 1)
		{
			if (vulkanBuffer->subBuffers[i]->bound == -1)
			{
				break;
			}
		}
		CURIDX = i;
	}
	else
	{
		Refresh_LogError("Buffer already bound. It is an error to set vertex data after binding but before submitting.");
		return;
	}

	SDL_memcpy(
		SUBBUF->allocation->mapPointer + SUBBUF->offset + offsetInBytes,
		data,
		dataLength
	);

	#undef CURIDX
	#undef SUBBUF
}

static uint32_t VULKAN_PushVertexShaderParams(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	void *data,
	uint32_t elementCount
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;

	SDL_LockMutex(renderer->uniformBufferLock);

	renderer->vertexUBOOffset += renderer->vertexUBOBlockIncrement;
	renderer->vertexUBOBlockIncrement = vulkanCommandBuffer->currentGraphicsPipeline->vertexUBOBlockSize;

	if (
		renderer->vertexUBOOffset +
		vulkanCommandBuffer->currentGraphicsPipeline->vertexUBOBlockSize >=
		UBO_BUFFER_SIZE * (renderer->frameIndex + 1)
	) {
		Refresh_LogError("Vertex UBO overflow!");
		return 0;
	}

	VULKAN_SetBufferData(
		(Refresh_Renderer*) renderer,
		(Refresh_Buffer*) renderer->vertexUBO,
		renderer->vertexUBOOffset,
		data,
		elementCount * vulkanCommandBuffer->currentGraphicsPipeline->vertexUBOBlockSize
	);

	SDL_UnlockMutex(renderer->uniformBufferLock);

	return renderer->vertexUBOOffset;
}

static uint32_t VULKAN_PushFragmentShaderParams(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	void *data,
	uint32_t elementCount
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;

	SDL_LockMutex(renderer->uniformBufferLock);

	renderer->fragmentUBOOffset += renderer->fragmentUBOBlockIncrement;
	renderer->fragmentUBOBlockIncrement = vulkanCommandBuffer->currentGraphicsPipeline->fragmentUBOBlockSize;

	if (
		renderer->fragmentUBOOffset +
		vulkanCommandBuffer->currentGraphicsPipeline->fragmentUBOBlockSize >=
		UBO_BUFFER_SIZE * (renderer->frameIndex + 1)
	) {
		Refresh_LogError("Fragment UBO overflow!");
		return 0;
	}

	VULKAN_SetBufferData(
		(Refresh_Renderer*) renderer,
		(Refresh_Buffer*) renderer->fragmentUBO,
		renderer->fragmentUBOOffset,
		data,
		elementCount * vulkanCommandBuffer->currentGraphicsPipeline->fragmentUBOBlockSize
	);

	SDL_UnlockMutex(renderer->uniformBufferLock);

	return renderer->fragmentUBOOffset;
}

static uint32_t VULKAN_PushComputeShaderParams(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	void *data,
	uint32_t elementCount
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;

	SDL_LockMutex(renderer->uniformBufferLock);

	renderer->computeUBOOffset += renderer->computeUBOBlockIncrement;
	renderer->computeUBOBlockIncrement = vulkanCommandBuffer->currentComputePipeline->computeUBOBlockSize;

	if (
		renderer->computeUBOOffset +
		vulkanCommandBuffer->currentComputePipeline->computeUBOBlockSize >=
		UBO_BUFFER_SIZE * (renderer->frameIndex + 1)
	) {
		Refresh_LogError("Compute UBO overflow!");
		return 0;
	}

	VULKAN_SetBufferData(
		(Refresh_Renderer*) renderer,
		(Refresh_Buffer*) renderer->computeUBO,
		renderer->computeUBOOffset,
		data,
		elementCount * vulkanCommandBuffer->currentComputePipeline->computeUBOBlockSize
	);

	SDL_UnlockMutex(renderer->uniformBufferLock);

	return renderer->computeUBOOffset;
}

static inline uint8_t BufferDescriptorSetDataEqual(
	BufferDescriptorSetData *a,
	BufferDescriptorSetData *b,
	uint8_t bindingCount
) {
	uint32_t i;

	for (i = 0; i < bindingCount; i += 1)
	{
		if (	a->descriptorBufferInfo[i].buffer != b->descriptorBufferInfo[i].buffer ||
			a->descriptorBufferInfo[i].offset != b->descriptorBufferInfo[i].offset ||
			a->descriptorBufferInfo[i].range != b->descriptorBufferInfo[i].range	)
		{
			return 0;
		}
	}

	return 1;
}

/* FIXME: this can probably be cleverly folded into the same cache structure as image descriptors */
static VkDescriptorSet VULKAN_INTERNAL_FetchBufferDescriptorSet(
	VulkanRenderer *renderer,
	BufferDescriptorSetCache *bufferDescriptorSetCache,
	BufferDescriptorSetData *bufferDescriptorSetData
) {
	uint32_t i;
	uint64_t hashcode;
	BufferDescriptorSetHashArray *arr;
	VkDescriptorSet newDescriptorSet;
	VkWriteDescriptorSet writeDescriptorSets[MAX_BUFFER_BINDINGS];
	BufferDescriptorSetHashMap *map;

	hashcode = BufferDescriptorSetHashTable_GetHashCode(
		bufferDescriptorSetData,
		bufferDescriptorSetCache->bindingCount
	);
	arr = &bufferDescriptorSetCache->buckets[hashcode % NUM_DESCRIPTOR_SET_HASH_BUCKETS];

	for (i = 0; i < arr->count; i += 1)
	{
		BufferDescriptorSetHashMap *e = &bufferDescriptorSetCache->elements[arr->elements[i]];
		if (BufferDescriptorSetDataEqual(
			bufferDescriptorSetData,
			&e->descriptorSetData,
			bufferDescriptorSetCache->bindingCount
		)) {
			e->inactiveFrameCount = 0;
			return e->descriptorSet;
		}
	}

	/* If no match exists, assign a new descriptor set and prepare it for update */
	/* If no inactive descriptor sets remain, create a new pool and allocate new inactive sets */

	if (bufferDescriptorSetCache->inactiveDescriptorSetCount == 0)
	{
		bufferDescriptorSetCache->bufferDescriptorPoolCount += 1;
		bufferDescriptorSetCache->bufferDescriptorPools = SDL_realloc(
			bufferDescriptorSetCache->bufferDescriptorPools,
			sizeof(VkDescriptorPool) * bufferDescriptorSetCache->bufferDescriptorPoolCount
		);

		VULKAN_INTERNAL_CreateDescriptorPool(
			renderer,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			bufferDescriptorSetCache->nextPoolSize,
			bufferDescriptorSetCache->nextPoolSize * bufferDescriptorSetCache->bindingCount,
			&bufferDescriptorSetCache->bufferDescriptorPools[bufferDescriptorSetCache->bufferDescriptorPoolCount - 1]
		);

		bufferDescriptorSetCache->inactiveDescriptorSetCapacity += bufferDescriptorSetCache->nextPoolSize;

		bufferDescriptorSetCache->inactiveDescriptorSets = SDL_realloc(
			bufferDescriptorSetCache->inactiveDescriptorSets,
			sizeof(VkDescriptorSet) * bufferDescriptorSetCache->inactiveDescriptorSetCapacity
		);

		VULKAN_INTERNAL_AllocateDescriptorSets(
			renderer,
			bufferDescriptorSetCache->bufferDescriptorPools[bufferDescriptorSetCache->bufferDescriptorPoolCount - 1],
			bufferDescriptorSetCache->descriptorSetLayout,
			bufferDescriptorSetCache->nextPoolSize,
			bufferDescriptorSetCache->inactiveDescriptorSets
		);

		bufferDescriptorSetCache->inactiveDescriptorSetCount = bufferDescriptorSetCache->nextPoolSize;

		bufferDescriptorSetCache->nextPoolSize *= 2;
	}

	newDescriptorSet = bufferDescriptorSetCache->inactiveDescriptorSets[bufferDescriptorSetCache->inactiveDescriptorSetCount - 1];
	bufferDescriptorSetCache->inactiveDescriptorSetCount -= 1;

	for (i = 0; i < bufferDescriptorSetCache->bindingCount; i += 1)
	{
		writeDescriptorSets[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSets[i].pNext = NULL;
		writeDescriptorSets[i].descriptorCount = 1;
		writeDescriptorSets[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writeDescriptorSets[i].dstArrayElement = 0;
		writeDescriptorSets[i].dstBinding = i;
		writeDescriptorSets[i].dstSet = newDescriptorSet;
		writeDescriptorSets[i].pBufferInfo = &bufferDescriptorSetData->descriptorBufferInfo[i];
		writeDescriptorSets[i].pImageInfo = NULL;
		writeDescriptorSets[i].pTexelBufferView = NULL;
	}

	renderer->vkUpdateDescriptorSets(
		renderer->logicalDevice,
		bufferDescriptorSetCache->bindingCount,
		writeDescriptorSets,
		0,
		NULL
	);

	EXPAND_ELEMENTS_IF_NEEDED(arr, 2, uint32_t)
	arr->elements[arr->count] = bufferDescriptorSetCache->count;
	arr->count += 1;

	if (bufferDescriptorSetCache->count == bufferDescriptorSetCache->capacity)
	{
		bufferDescriptorSetCache->capacity *= 2;

		bufferDescriptorSetCache->elements = SDL_realloc(
			bufferDescriptorSetCache->elements,
			sizeof(BufferDescriptorSetHashMap) * bufferDescriptorSetCache->capacity
		);
	}

	map = &bufferDescriptorSetCache->elements[bufferDescriptorSetCache->count];
	map->key = hashcode;

	for (i = 0; i < bufferDescriptorSetCache->bindingCount; i += 1)
	{
		map->descriptorSetData.descriptorBufferInfo[i].buffer =
			bufferDescriptorSetData->descriptorBufferInfo[i].buffer;
		map->descriptorSetData.descriptorBufferInfo[i].offset =
			bufferDescriptorSetData->descriptorBufferInfo[i].offset;
		map->descriptorSetData.descriptorBufferInfo[i].range =
			bufferDescriptorSetData->descriptorBufferInfo[i].range;
	}

	map->descriptorSet = newDescriptorSet;
	map->inactiveFrameCount = 0;
	bufferDescriptorSetCache->count += 1;

	return newDescriptorSet;
}

static inline uint8_t ImageDescriptorSetDataEqual(
	ImageDescriptorSetData *a,
	ImageDescriptorSetData *b,
	uint8_t bindingCount
) {
	uint32_t i;

	for (i = 0; i < bindingCount; i += 1)
	{
		if (	a->descriptorImageInfo[i].imageLayout != b->descriptorImageInfo[i].imageLayout ||
			a->descriptorImageInfo[i].imageView != b->descriptorImageInfo[i].imageView ||
			a->descriptorImageInfo[i].sampler != b->descriptorImageInfo[i].sampler	)
		{
			return 0;
		}
	}

	return 1;
}

static VkDescriptorSet VULKAN_INTERNAL_FetchImageDescriptorSet(
	VulkanRenderer *renderer,
	ImageDescriptorSetCache *imageDescriptorSetCache,
	ImageDescriptorSetData *imageDescriptorSetData
) {
	uint32_t i;
	uint64_t hashcode;
	ImageDescriptorSetHashArray *arr;
	VkDescriptorSet newDescriptorSet;
	VkWriteDescriptorSet writeDescriptorSets[MAX_TEXTURE_SAMPLERS];
	ImageDescriptorSetHashMap *map;

	hashcode = ImageDescriptorSetHashTable_GetHashCode(
		imageDescriptorSetData,
		imageDescriptorSetCache->bindingCount
	);
	arr = &imageDescriptorSetCache->buckets[hashcode % NUM_DESCRIPTOR_SET_HASH_BUCKETS];

	for (i = 0; i < arr->count; i += 1)
	{
		ImageDescriptorSetHashMap *e = &imageDescriptorSetCache->elements[arr->elements[i]];
		if (ImageDescriptorSetDataEqual(
			imageDescriptorSetData,
			&e->descriptorSetData,
			imageDescriptorSetCache->bindingCount
		)) {
			e->inactiveFrameCount = 0;
			return e->descriptorSet;
		}
	}

	/* If no match exists, assign a new descriptor set and prepare it for update */
	/* If no inactive descriptor sets remain, create a new pool and allocate new inactive sets */

	if (imageDescriptorSetCache->inactiveDescriptorSetCount == 0)
	{
		imageDescriptorSetCache->imageDescriptorPoolCount += 1;
		imageDescriptorSetCache->imageDescriptorPools = SDL_realloc(
			imageDescriptorSetCache->imageDescriptorPools,
			sizeof(VkDescriptorPool) * imageDescriptorSetCache->imageDescriptorPoolCount
		);

		VULKAN_INTERNAL_CreateDescriptorPool(
			renderer,
			imageDescriptorSetCache->descriptorType,
			imageDescriptorSetCache->nextPoolSize,
			imageDescriptorSetCache->nextPoolSize * imageDescriptorSetCache->bindingCount,
			&imageDescriptorSetCache->imageDescriptorPools[imageDescriptorSetCache->imageDescriptorPoolCount - 1]
		);

		imageDescriptorSetCache->inactiveDescriptorSetCapacity += imageDescriptorSetCache->nextPoolSize;

		imageDescriptorSetCache->inactiveDescriptorSets = SDL_realloc(
			imageDescriptorSetCache->inactiveDescriptorSets,
			sizeof(VkDescriptorSet) * imageDescriptorSetCache->inactiveDescriptorSetCapacity
		);

		VULKAN_INTERNAL_AllocateDescriptorSets(
			renderer,
			imageDescriptorSetCache->imageDescriptorPools[imageDescriptorSetCache->imageDescriptorPoolCount - 1],
			imageDescriptorSetCache->descriptorSetLayout,
			imageDescriptorSetCache->nextPoolSize,
			imageDescriptorSetCache->inactiveDescriptorSets
		);

		imageDescriptorSetCache->inactiveDescriptorSetCount = imageDescriptorSetCache->nextPoolSize;

		imageDescriptorSetCache->nextPoolSize *= 2;
	}

	newDescriptorSet = imageDescriptorSetCache->inactiveDescriptorSets[imageDescriptorSetCache->inactiveDescriptorSetCount - 1];
	imageDescriptorSetCache->inactiveDescriptorSetCount -= 1;

	for (i = 0; i < imageDescriptorSetCache->bindingCount; i += 1)
	{
		writeDescriptorSets[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSets[i].pNext = NULL;
		writeDescriptorSets[i].descriptorCount = 1;
		writeDescriptorSets[i].descriptorType = imageDescriptorSetCache->descriptorType;
		writeDescriptorSets[i].dstArrayElement = 0;
		writeDescriptorSets[i].dstBinding = i;
		writeDescriptorSets[i].dstSet = newDescriptorSet;
		writeDescriptorSets[i].pBufferInfo = NULL;
		writeDescriptorSets[i].pImageInfo = &imageDescriptorSetData->descriptorImageInfo[i];
		writeDescriptorSets[i].pTexelBufferView = NULL;
	}

	renderer->vkUpdateDescriptorSets(
		renderer->logicalDevice,
		imageDescriptorSetCache->bindingCount,
		writeDescriptorSets,
		0,
		NULL
	);

	EXPAND_ELEMENTS_IF_NEEDED(arr, 2, uint32_t)
	arr->elements[arr->count] = imageDescriptorSetCache->count;
	arr->count += 1;

	if (imageDescriptorSetCache->count == imageDescriptorSetCache->capacity)
	{
		imageDescriptorSetCache->capacity *= 2;

		imageDescriptorSetCache->elements = SDL_realloc(
			imageDescriptorSetCache->elements,
			sizeof(ImageDescriptorSetHashMap) * imageDescriptorSetCache->capacity
		);
	}

	map = &imageDescriptorSetCache->elements[imageDescriptorSetCache->count];
	map->key = hashcode;

	for (i = 0; i < imageDescriptorSetCache->bindingCount; i += 1)
	{
		map->descriptorSetData.descriptorImageInfo[i].imageLayout =
			imageDescriptorSetData->descriptorImageInfo[i].imageLayout;
		map->descriptorSetData.descriptorImageInfo[i].imageView =
			imageDescriptorSetData->descriptorImageInfo[i].imageView;
		map->descriptorSetData.descriptorImageInfo[i].sampler =
			imageDescriptorSetData->descriptorImageInfo[i].sampler;
	}

	map->descriptorSet = newDescriptorSet;
	map->inactiveFrameCount = 0;
	imageDescriptorSetCache->count += 1;

	return newDescriptorSet;
}

static void VULKAN_BindVertexSamplers(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Texture **pTextures,
	Refresh_Sampler **pSamplers
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	VulkanGraphicsPipeline *graphicsPipeline = vulkanCommandBuffer->currentGraphicsPipeline;

	VulkanTexture *currentTexture;
	uint32_t i, samplerCount;
	ImageDescriptorSetData vertexSamplerDescriptorSetData;

	if (graphicsPipeline->pipelineLayout->vertexSamplerDescriptorSetCache == NULL)
	{
		return;
	}

	samplerCount = graphicsPipeline->pipelineLayout->vertexSamplerDescriptorSetCache->bindingCount;

	for (i = 0; i < samplerCount; i += 1)
	{
		currentTexture = (VulkanTexture*) pTextures[i];
		vertexSamplerDescriptorSetData.descriptorImageInfo[i].imageView = currentTexture->view;
		vertexSamplerDescriptorSetData.descriptorImageInfo[i].sampler = (VkSampler) pSamplers[i];
		vertexSamplerDescriptorSetData.descriptorImageInfo[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	graphicsPipeline->vertexSamplerDescriptorSet = VULKAN_INTERNAL_FetchImageDescriptorSet(
		renderer,
		graphicsPipeline->pipelineLayout->vertexSamplerDescriptorSetCache,
		&vertexSamplerDescriptorSetData
	);
}

static void VULKAN_BindFragmentSamplers(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Texture **pTextures,
	Refresh_Sampler **pSamplers
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	VulkanGraphicsPipeline *graphicsPipeline = vulkanCommandBuffer->currentGraphicsPipeline;
	VulkanTexture *currentTexture;

	uint32_t i, samplerCount;
	ImageDescriptorSetData fragmentSamplerDescriptorSetData;

	if (graphicsPipeline->pipelineLayout->fragmentSamplerDescriptorSetCache == NULL)
	{
		return;
	}

	samplerCount = graphicsPipeline->pipelineLayout->fragmentSamplerDescriptorSetCache->bindingCount;

	for (i = 0; i < samplerCount; i += 1)
	{
		currentTexture = (VulkanTexture*) pTextures[i];
		fragmentSamplerDescriptorSetData.descriptorImageInfo[i].imageView = currentTexture->view;
		fragmentSamplerDescriptorSetData.descriptorImageInfo[i].sampler = (VkSampler) pSamplers[i];
		fragmentSamplerDescriptorSetData.descriptorImageInfo[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	graphicsPipeline->fragmentSamplerDescriptorSet = VULKAN_INTERNAL_FetchImageDescriptorSet(
		renderer,
		graphicsPipeline->pipelineLayout->fragmentSamplerDescriptorSetCache,
		&fragmentSamplerDescriptorSetData
	);
}

static void VULKAN_GetBufferData(
	Refresh_Renderer *driverData,
	Refresh_Buffer *buffer,
	void *data,
	uint32_t dataLengthInBytes
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanBuffer *vulkanBuffer = (VulkanBuffer*) buffer;
	uint8_t *dataPtr = (uint8_t*) data;
	uint8_t *mapPointer;

	mapPointer =
		vulkanBuffer->subBuffers[vulkanBuffer->currentSubBufferIndex]->allocation->mapPointer +
		vulkanBuffer->subBuffers[vulkanBuffer->currentSubBufferIndex]->offset;

	SDL_memcpy(
		dataPtr,
		mapPointer,
		dataLengthInBytes
	);
}

static void VULKAN_CopyTextureToBuffer(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_TextureSlice *textureSlice,
	Refresh_Buffer *buffer
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	VulkanTexture *vulkanTexture = (VulkanTexture*) textureSlice->texture;
	VulkanBuffer *vulkanBuffer = (VulkanBuffer*) buffer;

	VulkanResourceAccessType prevResourceAccess;
	VkBufferImageCopy imageCopy;

	/* Cache this so we can restore it later */
	prevResourceAccess = vulkanTexture->resourceAccessType;

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		vulkanCommandBuffer->commandBuffer,
		RESOURCE_ACCESS_TRANSFER_READ,
		VK_IMAGE_ASPECT_COLOR_BIT,
		textureSlice->layer,
		1,
		textureSlice->level,
		1,
		0,
		vulkanTexture->image,
		&vulkanTexture->resourceAccessType
	);

	/* Save texture data to buffer */

	imageCopy.imageExtent.width = textureSlice->rectangle.w;
	imageCopy.imageExtent.height = textureSlice->rectangle.h;
	imageCopy.imageExtent.depth = 1;
	imageCopy.bufferRowLength = textureSlice->rectangle.w;
	imageCopy.bufferImageHeight = textureSlice->rectangle.h;
	imageCopy.imageOffset.x = textureSlice->rectangle.x;
	imageCopy.imageOffset.y = textureSlice->rectangle.y;
	imageCopy.imageOffset.z = textureSlice->depth;
	imageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageCopy.imageSubresource.baseArrayLayer = textureSlice->layer;
	imageCopy.imageSubresource.layerCount = 1;
	imageCopy.imageSubresource.mipLevel = textureSlice->level;
	imageCopy.bufferOffset = 0;

	renderer->vkCmdCopyImageToBuffer(
		vulkanCommandBuffer->commandBuffer,
		vulkanTexture->image,
		AccessMap[vulkanTexture->resourceAccessType].imageLayout,
		vulkanBuffer->subBuffers[vulkanBuffer->currentSubBufferIndex]->buffer,
		1,
		&imageCopy
	);

	/* Restore the image layout */

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		vulkanCommandBuffer->commandBuffer,
		prevResourceAccess,
		VK_IMAGE_ASPECT_COLOR_BIT,
		textureSlice->layer,
		1,
		textureSlice->level,
		1,
		0,
		vulkanTexture->image,
		&vulkanTexture->resourceAccessType
	);
}

static void VULKAN_QueueDestroyTexture(
	Refresh_Renderer *driverData,
	Refresh_Texture *texture
) {
	VulkanRenderer* renderer = (VulkanRenderer*)driverData;
	VulkanTexture* vulkanTexture = (VulkanTexture*)texture;

	SDL_LockMutex(renderer->disposeLock);

	EXPAND_ARRAY_IF_NEEDED(
		renderer->texturesToDestroy,
		VulkanTexture*,
		renderer->texturesToDestroyCount + 1,
		renderer->texturesToDestroyCapacity,
		renderer->texturesToDestroyCapacity * 2
	)

	renderer->texturesToDestroy[renderer->texturesToDestroyCount] = vulkanTexture;
	renderer->texturesToDestroyCount += 1;

	SDL_UnlockMutex(renderer->disposeLock);
}

static void VULKAN_QueueDestroySampler(
	Refresh_Renderer *driverData,
	Refresh_Sampler *sampler
) {
	VulkanRenderer* renderer = (VulkanRenderer*)driverData;
	VkSampler vulkanSampler = (VkSampler) sampler;

	SDL_LockMutex(renderer->disposeLock);

	EXPAND_ARRAY_IF_NEEDED(
		renderer->samplersToDestroy,
		VkSampler,
		renderer->samplersToDestroyCount + 1,
		renderer->samplersToDestroyCapacity,
		renderer->samplersToDestroyCapacity * 2
	)

	renderer->samplersToDestroy[renderer->samplersToDestroyCount] = vulkanSampler;
	renderer->samplersToDestroyCount += 1;

	SDL_UnlockMutex(renderer->disposeLock);
}

static void VULKAN_QueueDestroyBuffer(
	Refresh_Renderer *driverData,
	Refresh_Buffer *buffer
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanBuffer *vulkanBuffer = (VulkanBuffer*) buffer;

	SDL_LockMutex(renderer->disposeLock);

	EXPAND_ARRAY_IF_NEEDED(
		renderer->buffersToDestroy,
		VulkanBuffer*,
		renderer->buffersToDestroyCount + 1,
		renderer->buffersToDestroyCapacity,
		renderer->buffersToDestroyCapacity * 2
	)

	renderer->buffersToDestroy[
		renderer->buffersToDestroyCount
	] = vulkanBuffer;
	renderer->buffersToDestroyCount += 1;

	SDL_UnlockMutex(renderer->disposeLock);
}

static void VULKAN_QueueDestroyColorTarget(
	Refresh_Renderer *driverData,
	Refresh_ColorTarget *colorTarget
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanColorTarget *vulkanColorTarget = (VulkanColorTarget*) colorTarget;

	SDL_LockMutex(renderer->disposeLock);

	EXPAND_ARRAY_IF_NEEDED(
		renderer->colorTargetsToDestroy,
		VulkanColorTarget*,
		renderer->colorTargetsToDestroyCount + 1,
		renderer->colorTargetsToDestroyCapacity,
		renderer->colorTargetsToDestroyCapacity * 2
	)

	renderer->colorTargetsToDestroy[renderer->colorTargetsToDestroyCount] = vulkanColorTarget;
	renderer->colorTargetsToDestroyCount += 1;

	SDL_UnlockMutex(renderer->disposeLock);
}

static void VULKAN_QueueDestroyDepthStencilTarget(
	Refresh_Renderer *driverData,
	Refresh_DepthStencilTarget *depthStencilTarget
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanDepthStencilTarget *vulkanDepthStencilTarget = (VulkanDepthStencilTarget*) depthStencilTarget;

	SDL_LockMutex(renderer->disposeLock);

	EXPAND_ARRAY_IF_NEEDED(
		renderer->depthStencilTargetsToDestroy,
		VulkanDepthStencilTarget*,
		renderer->depthStencilTargetsToDestroyCount + 1,
		renderer->depthStencilTargetsToDestroyCapacity,
		renderer->depthStencilTargetsToDestroyCapacity * 2
	)

	renderer->depthStencilTargetsToDestroy[renderer->depthStencilTargetsToDestroyCount] = vulkanDepthStencilTarget;
	renderer->depthStencilTargetsToDestroyCount += 1;

	SDL_UnlockMutex(renderer->disposeLock);
}

static void VULKAN_QueueDestroyFramebuffer(
	Refresh_Renderer *driverData,
	Refresh_Framebuffer *framebuffer
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanFramebuffer *vulkanFramebuffer = (VulkanFramebuffer*) framebuffer;

	SDL_LockMutex(renderer->disposeLock);

	EXPAND_ARRAY_IF_NEEDED(
		renderer->framebuffersToDestroy,
		VulkanFramebuffer*,
		renderer->framebuffersToDestroyCount + 1,
		renderer->framebuffersToDestroyCapacity,
		renderer->framebuffersToDestroyCapacity * 2
	)

	renderer->framebuffersToDestroy[renderer->framebuffersToDestroyCount] = vulkanFramebuffer;
	renderer->framebuffersToDestroyCount += 1;

	SDL_UnlockMutex(renderer->disposeLock);
}

static void VULKAN_QueueDestroyShaderModule(
	Refresh_Renderer *driverData,
	Refresh_ShaderModule *shaderModule
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VkShaderModule vulkanShaderModule = (VkShaderModule) shaderModule;

	SDL_LockMutex(renderer->disposeLock);

	EXPAND_ARRAY_IF_NEEDED(
		renderer->shaderModulesToDestroy,
		VkShaderModule,
		renderer->shaderModulesToDestroyCount + 1,
		renderer->shaderModulesToDestroyCapacity,
		renderer->shaderModulesToDestroyCapacity * 2
	)

	renderer->shaderModulesToDestroy[renderer->shaderModulesToDestroyCount] = vulkanShaderModule;
	renderer->shaderModulesToDestroyCount += 1;

	SDL_UnlockMutex(renderer->disposeLock);
}

static void VULKAN_QueueDestroyRenderPass(
	Refresh_Renderer *driverData,
	Refresh_RenderPass *renderPass
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VkRenderPass vulkanRenderPass = (VkRenderPass) renderPass;

	SDL_LockMutex(renderer->disposeLock);

	EXPAND_ARRAY_IF_NEEDED(
		renderer->renderPassesToDestroy,
		VkRenderPass,
		renderer->renderPassesToDestroyCount + 1,
		renderer->renderPassesToDestroyCapacity,
		renderer->renderPassesToDestroyCapacity * 2
	)

	renderer->renderPassesToDestroy[renderer->renderPassesToDestroyCount] = vulkanRenderPass;
	renderer->renderPassesToDestroyCount += 1;

	SDL_UnlockMutex(renderer->disposeLock);
}

static void VULKAN_QueueDestroyComputePipeline(
	Refresh_Renderer *driverData,
	Refresh_ComputePipeline *computePipeline
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanComputePipeline *vulkanComputePipeline = (VulkanComputePipeline*) computePipeline;

	SDL_LockMutex(renderer->disposeLock);

	EXPAND_ARRAY_IF_NEEDED(
		renderer->computePipelinesToDestroy,
		VulkanComputePipeline*,
		renderer->computePipelinesToDestroyCount + 1,
		renderer->computePipelinesToDestroyCapacity,
		renderer->computePipelinesToDestroyCapacity * 2
	)

	renderer->computePipelinesToDestroy[renderer->computePipelinesToDestroyCount] = vulkanComputePipeline;
	renderer->computePipelinesToDestroyCount += 1;

	SDL_UnlockMutex(renderer->disposeLock);
}

static void VULKAN_QueueDestroyGraphicsPipeline(
	Refresh_Renderer *driverData,
	Refresh_GraphicsPipeline *graphicsPipeline
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanGraphicsPipeline *vulkanGraphicsPipeline = (VulkanGraphicsPipeline*) graphicsPipeline;

	SDL_LockMutex(renderer->disposeLock);

	EXPAND_ARRAY_IF_NEEDED(
		renderer->graphicsPipelinesToDestroy,
		VulkanGraphicsPipeline*,
		renderer->graphicsPipelinesToDestroyCount + 1,
		renderer->graphicsPipelinesToDestroyCapacity,
		renderer->graphicsPipelinesToDestroyCapacity * 2
	)

	renderer->graphicsPipelinesToDestroy[renderer->graphicsPipelinesToDestroyCount] = vulkanGraphicsPipeline;
	renderer->graphicsPipelinesToDestroyCount += 1;

	SDL_UnlockMutex(renderer->disposeLock);
}

static void VULKAN_BeginRenderPass(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_RenderPass *renderPass,
	Refresh_Framebuffer *framebuffer,
	Refresh_Rect renderArea,
	Refresh_Color *pColorClearValues,
	uint32_t colorClearCount,
	Refresh_DepthStencilValue *depthStencilClearValue
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	VulkanFramebuffer *vulkanFramebuffer = (VulkanFramebuffer*) framebuffer;

	VkClearValue *clearValues;
	uint32_t i;
	uint32_t clearCount = colorClearCount;
	VkImageAspectFlags depthAspectFlags;

	/* Layout transitions */

	for (i = 0; i < vulkanFramebuffer->colorTargetCount; i += 1)
	{
		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			vulkanCommandBuffer->commandBuffer,
			RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE,
			VK_IMAGE_ASPECT_COLOR_BIT,
			vulkanFramebuffer->colorTargets[i]->layer,
			1,
			0,
			1,
			0,
			vulkanFramebuffer->colorTargets[i]->texture->image,
			&vulkanFramebuffer->colorTargets[i]->texture->resourceAccessType
		);
	}

	if (depthStencilClearValue != NULL)
	{
		depthAspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (DepthFormatContainsStencil(
			vulkanFramebuffer->depthStencilTarget->texture->format
		)) {
			depthAspectFlags |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}

		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			vulkanCommandBuffer->commandBuffer,
			RESOURCE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_WRITE,
			depthAspectFlags,
			0,
			1,
			0,
			1,
			0,
			vulkanFramebuffer->depthStencilTarget->texture->image,
			&vulkanFramebuffer->depthStencilTarget->texture->resourceAccessType
		);

		clearCount += 1;
	}

	/* Set clear values */

	clearValues = SDL_stack_alloc(VkClearValue, clearCount);

	for (i = 0; i < colorClearCount; i += 1)
	{
		clearValues[i].color.float32[0] = pColorClearValues[i].r / 255.0f;
		clearValues[i].color.float32[1] = pColorClearValues[i].g / 255.0f;
		clearValues[i].color.float32[2] = pColorClearValues[i].b / 255.0f;
		clearValues[i].color.float32[3] = pColorClearValues[i].a / 255.0f;
	}

	if (depthStencilClearValue != NULL)
	{
		clearValues[colorClearCount].depthStencil.depth =
			depthStencilClearValue->depth;
		clearValues[colorClearCount].depthStencil.stencil =
			depthStencilClearValue->stencil;
	}

	VkRenderPassBeginInfo renderPassBeginInfo;
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.pNext = NULL;
	renderPassBeginInfo.renderPass = (VkRenderPass) renderPass;
	renderPassBeginInfo.framebuffer = vulkanFramebuffer->framebuffer;
	renderPassBeginInfo.renderArea.extent.width = renderArea.w;
	renderPassBeginInfo.renderArea.extent.height = renderArea.h;
	renderPassBeginInfo.renderArea.offset.x = renderArea.x;
	renderPassBeginInfo.renderArea.offset.y = renderArea.y;
	renderPassBeginInfo.pClearValues = clearValues;
	renderPassBeginInfo.clearValueCount = clearCount;

	renderer->vkCmdBeginRenderPass(
		vulkanCommandBuffer->commandBuffer,
		&renderPassBeginInfo,
		VK_SUBPASS_CONTENTS_INLINE
	);

	vulkanCommandBuffer->currentFramebuffer = vulkanFramebuffer;

	SDL_stack_free(clearValues);
}

static void VULKAN_EndRenderPass(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	VulkanTexture *currentTexture;
	uint32_t i;

	renderer->vkCmdEndRenderPass(
		vulkanCommandBuffer->commandBuffer
	);

	for (i = 0; i < vulkanCommandBuffer->currentFramebuffer->colorTargetCount; i += 1)
	{
		currentTexture = vulkanCommandBuffer->currentFramebuffer->colorTargets[i]->texture;
		if (currentTexture->usageFlags & REFRESH_TEXTUREUSAGE_SAMPLER_BIT)
		{
			VULKAN_INTERNAL_ImageMemoryBarrier(
				renderer,
				vulkanCommandBuffer->commandBuffer,
				RESOURCE_ACCESS_ANY_SHADER_READ_SAMPLED_IMAGE,
				VK_IMAGE_ASPECT_COLOR_BIT,
				0,
				currentTexture->layerCount,
				0,
				currentTexture->levelCount,
				0,
				currentTexture->image,
				&currentTexture->resourceAccessType
			);
		}
	}

	vulkanCommandBuffer->currentGraphicsPipeline = NULL;
	vulkanCommandBuffer->currentFramebuffer = NULL;
}

static void VULKAN_BindGraphicsPipeline(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_GraphicsPipeline *graphicsPipeline
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	VulkanGraphicsPipeline* pipeline = (VulkanGraphicsPipeline*) graphicsPipeline;

	/* bind dummy sets */
	if (pipeline->pipelineLayout->vertexSamplerDescriptorSetCache == NULL)
	{
		pipeline->vertexSamplerDescriptorSet = renderer->emptyVertexSamplerDescriptorSet;
	}

	if (pipeline->pipelineLayout->fragmentSamplerDescriptorSetCache == NULL)
	{
		pipeline->fragmentSamplerDescriptorSet = renderer->emptyFragmentSamplerDescriptorSet;
	}

	renderer->vkCmdBindPipeline(
		vulkanCommandBuffer->commandBuffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		pipeline->pipeline
	);

	vulkanCommandBuffer->currentGraphicsPipeline = pipeline;
}

static void VULKAN_INTERNAL_MarkAsBound(
	VulkanRenderer* renderer,
	VulkanBuffer* buf
) {
	VulkanSubBuffer *subbuf = buf->subBuffers[buf->currentSubBufferIndex];
	subbuf->bound = renderer->frameIndex;

	/* Don't rebind a bound buffer */
	if (buf->bound) return;

	buf->bound = 1;

	SDL_LockMutex(renderer->boundBufferLock);

	if (renderer->buffersInUseCount == renderer->buffersInUseCapacity)
	{
		renderer->buffersInUseCapacity *= 2;
		renderer->buffersInUse = SDL_realloc(
			renderer->buffersInUse,
			sizeof(VulkanBuffer*) * renderer->buffersInUseCapacity
		);
	}

	renderer->buffersInUse[renderer->buffersInUseCount] = buf;
	renderer->buffersInUseCount += 1;

	SDL_UnlockMutex(renderer->boundBufferLock);
}

static void VULKAN_BindVertexBuffers(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	uint32_t firstBinding,
	uint32_t bindingCount,
	Refresh_Buffer **pBuffers,
	uint64_t *pOffsets
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;

	VkBuffer *buffers = SDL_stack_alloc(VkBuffer, bindingCount);
	VulkanBuffer* currentBuffer;
	uint32_t i;

	for (i = 0; i < bindingCount; i += 1)
	{
		currentBuffer = (VulkanBuffer*) pBuffers[i];
		buffers[i] = currentBuffer->subBuffers[currentBuffer->currentSubBufferIndex]->buffer;
		VULKAN_INTERNAL_MarkAsBound(renderer, currentBuffer);
	}

	renderer->vkCmdBindVertexBuffers(
		vulkanCommandBuffer->commandBuffer,
		firstBinding,
		bindingCount,
		buffers,
		pOffsets
	);

	SDL_stack_free(buffers);
}

static void VULKAN_BindIndexBuffer(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Buffer *buffer,
	uint64_t offset,
	Refresh_IndexElementSize indexElementSize
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	VulkanBuffer* vulkanBuffer = (VulkanBuffer*) buffer;

	VULKAN_INTERNAL_MarkAsBound(renderer, vulkanBuffer);

	renderer->vkCmdBindIndexBuffer(
		vulkanCommandBuffer->commandBuffer,
		vulkanBuffer->subBuffers[vulkanBuffer->currentSubBufferIndex]->buffer,
		offset,
		RefreshToVK_IndexType[indexElementSize]
	);
}

static void VULKAN_BindComputePipeline(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_ComputePipeline *computePipeline
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	VulkanComputePipeline *vulkanComputePipeline = (VulkanComputePipeline*) computePipeline;

	/* bind dummy sets */
	if (vulkanComputePipeline->pipelineLayout->bufferDescriptorSetCache == NULL)
	{
		vulkanComputePipeline->bufferDescriptorSet = renderer->emptyComputeBufferDescriptorSet;
	}

	if (vulkanComputePipeline->pipelineLayout->imageDescriptorSetCache == NULL)
	{
		vulkanComputePipeline->imageDescriptorSet = renderer->emptyComputeImageDescriptorSet;
	}

	renderer->vkCmdBindPipeline(
		vulkanCommandBuffer->commandBuffer,
		VK_PIPELINE_BIND_POINT_COMPUTE,
		vulkanComputePipeline->pipeline
	);

	vulkanCommandBuffer->currentComputePipeline = vulkanComputePipeline;
}

static void VULKAN_BindComputeBuffers(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Buffer **pBuffers
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	VulkanComputePipeline *computePipeline = vulkanCommandBuffer->currentComputePipeline;

	VulkanBuffer *currentBuffer;
	BufferDescriptorSetData bufferDescriptorSetData;
	uint32_t i;

	if (computePipeline->pipelineLayout->bufferDescriptorSetCache == NULL)
	{
		return;
	}

	for (i = 0; i < computePipeline->pipelineLayout->bufferDescriptorSetCache->bindingCount; i += 1)
	{
		currentBuffer = (VulkanBuffer*) pBuffers[i];

		bufferDescriptorSetData.descriptorBufferInfo[i].buffer = currentBuffer->subBuffers[currentBuffer->currentSubBufferIndex]->buffer;
		bufferDescriptorSetData.descriptorBufferInfo[i].offset = 0;
		bufferDescriptorSetData.descriptorBufferInfo[i].range = currentBuffer->subBuffers[currentBuffer->currentSubBufferIndex]->size;

		VULKAN_INTERNAL_MarkAsBound(renderer, currentBuffer);
		vulkanCommandBuffer->boundComputeBuffers[i] = currentBuffer;
	}

	vulkanCommandBuffer->boundComputeBufferCount = computePipeline->pipelineLayout->bufferDescriptorSetCache->bindingCount;

	computePipeline->bufferDescriptorSet =
		VULKAN_INTERNAL_FetchBufferDescriptorSet(
			renderer,
			computePipeline->pipelineLayout->bufferDescriptorSetCache,
			&bufferDescriptorSetData
		);
}

static void VULKAN_BindComputeTextures(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Texture **pTextures
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	VulkanComputePipeline *computePipeline = vulkanCommandBuffer->currentComputePipeline;

	VulkanTexture *currentTexture;
	ImageDescriptorSetData imageDescriptorSetData;
	uint32_t i;

	if (computePipeline->pipelineLayout->imageDescriptorSetCache == NULL)
	{
		return;
	}

	for (i = 0; i < computePipeline->pipelineLayout->imageDescriptorSetCache->bindingCount; i += 1)
	{
		currentTexture = (VulkanTexture*) pTextures[i];
		imageDescriptorSetData.descriptorImageInfo[i].imageView = currentTexture->view;
		imageDescriptorSetData.descriptorImageInfo[i].sampler = VK_NULL_HANDLE;
		imageDescriptorSetData.descriptorImageInfo[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	computePipeline->imageDescriptorSet =
		VULKAN_INTERNAL_FetchImageDescriptorSet(
			renderer,
			computePipeline->pipelineLayout->imageDescriptorSetCache,
			&imageDescriptorSetData
		);
}

static void VULKAN_INTERNAL_AllocateCommandBuffers(
	VulkanRenderer *renderer,
	VulkanCommandPool *vulkanCommandPool,
	uint32_t allocateCount
) {
	VkCommandBufferAllocateInfo allocateInfo;
	VkResult vulkanResult;
	uint32_t i;
	VkCommandBuffer *commandBuffers = SDL_stack_alloc(VkCommandBuffer, allocateCount);
	VulkanCommandBuffer *currentVulkanCommandBuffer;

	vulkanCommandPool->inactiveCommandBufferCapacity += allocateCount;

	vulkanCommandPool->inactiveCommandBuffers = SDL_realloc(
		vulkanCommandPool->inactiveCommandBuffers,
		sizeof(VulkanCommandBuffer*) *
		vulkanCommandPool->inactiveCommandBufferCapacity
	);

	allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocateInfo.pNext = NULL;
	allocateInfo.commandPool = vulkanCommandPool->commandPool;
	allocateInfo.commandBufferCount = allocateCount;
	allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

	vulkanResult = renderer->vkAllocateCommandBuffers(
		renderer->logicalDevice,
		&allocateInfo,
		commandBuffers
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkAllocateCommandBuffers", vulkanResult);
		SDL_stack_free(commandBuffers);
		return;
	}

	for (i = 0; i < allocateCount; i += 1)
	{
		currentVulkanCommandBuffer = SDL_malloc(sizeof(VulkanCommandBuffer));
		currentVulkanCommandBuffer->commandPool = vulkanCommandPool;
		currentVulkanCommandBuffer->commandBuffer = commandBuffers[i];
		vulkanCommandPool->inactiveCommandBuffers[
			vulkanCommandPool->inactiveCommandBufferCount
		] = currentVulkanCommandBuffer;
		vulkanCommandPool->inactiveCommandBufferCount += 1;
	}

	SDL_stack_free(commandBuffers);
}

static VulkanCommandPool* VULKAN_INTERNAL_FetchCommandPool(
	VulkanRenderer *renderer,
	SDL_threadID threadID
) {
	VulkanCommandPool *vulkanCommandPool;
	VkCommandPoolCreateInfo commandPoolCreateInfo;
	VkResult vulkanResult;
	CommandPoolHash commandPoolHash;

	commandPoolHash.threadID = threadID;

	vulkanCommandPool = CommandPoolHashTable_Fetch(
		&renderer->commandPoolHashTable,
		commandPoolHash
	);

	if (vulkanCommandPool != NULL)
	{
		return vulkanCommandPool;
	}

	vulkanCommandPool = (VulkanCommandPool*) SDL_malloc(sizeof(VulkanCommandPool));

	commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolCreateInfo.pNext = NULL;
	commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	commandPoolCreateInfo.queueFamilyIndex = renderer->queueFamilyIndices.graphicsFamily;

	vulkanResult = renderer->vkCreateCommandPool(
		renderer->logicalDevice,
		&commandPoolCreateInfo,
		NULL,
		&vulkanCommandPool->commandPool
	);

	if (vulkanResult != VK_SUCCESS)
	{
		Refresh_LogError("Failed to create command pool!");
		LogVulkanResult("vkCreateCommandPool", vulkanResult);
		return NULL;
	}

	vulkanCommandPool->threadID = threadID;

	vulkanCommandPool->inactiveCommandBufferCapacity = 0;
	vulkanCommandPool->inactiveCommandBufferCount = 0;
	vulkanCommandPool->inactiveCommandBuffers = NULL;

	VULKAN_INTERNAL_AllocateCommandBuffers(
		renderer,
		vulkanCommandPool,
		2
	);

	CommandPoolHashTable_Insert(
		&renderer->commandPoolHashTable,
		commandPoolHash,
		vulkanCommandPool
	);

	return vulkanCommandPool;
}

static VulkanCommandBuffer* VULKAN_INTERNAL_GetInactiveCommandBufferFromPool(
	VulkanRenderer *renderer,
	SDL_threadID threadID
) {
	VulkanCommandPool *commandPool =
		VULKAN_INTERNAL_FetchCommandPool(renderer, threadID);
	VulkanCommandBuffer *commandBuffer;

	if (commandPool->inactiveCommandBufferCount == 0)
	{
		VULKAN_INTERNAL_AllocateCommandBuffers(
			renderer,
			commandPool,
			commandPool->inactiveCommandBufferCapacity
		);
	}

	commandBuffer = commandPool->inactiveCommandBuffers[commandPool->inactiveCommandBufferCount - 1];
	commandPool->inactiveCommandBufferCount -= 1;

	return commandBuffer;
}

static Refresh_CommandBuffer* VULKAN_AcquireCommandBuffer(
	Refresh_Renderer *driverData,
	uint8_t fixed
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	uint32_t i;

	SDL_threadID threadID = SDL_ThreadID();

	VulkanCommandBuffer *commandBuffer =
		VULKAN_INTERNAL_GetInactiveCommandBufferFromPool(renderer, threadID);

	/* State tracking */

	commandBuffer->currentComputePipeline = NULL;
	commandBuffer->currentGraphicsPipeline = NULL;
	commandBuffer->currentFramebuffer = NULL;

	/* init bound compute buffer array */

	for (i = 0; i < MAX_BUFFER_BINDINGS; i += 1)
	{
		commandBuffer->boundComputeBuffers[i] = NULL;
	}
	commandBuffer->boundComputeBufferCount = 0;

	commandBuffer->fixed = fixed;
	commandBuffer->submitted = 0;

	VULKAN_INTERNAL_BeginCommandBuffer(renderer, commandBuffer);

	return (Refresh_CommandBuffer*) commandBuffer;
}

static void VULKAN_QueuePresent(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_TextureSlice *textureSlice,
	Refresh_Rect *destinationRectangle,
	Refresh_Filter filter
) {
	VkResult acquireResult;
	uint32_t swapChainImageIndex;

	Refresh_Rect dstRect;

	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	VulkanTexture* vulkanTexture = (VulkanTexture*) textureSlice->texture;

	if (renderer->headless)
	{
		Refresh_LogError("Cannot call QueuePresent in headless mode!");
		return;
	}

	acquireResult = renderer->vkAcquireNextImageKHR(
		renderer->logicalDevice,
		renderer->swapChain,
		UINT64_MAX,
		renderer->imageAvailableSemaphore,
		VK_NULL_HANDLE,
		&swapChainImageIndex
	);

	if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
	{
		/* Failed to acquire swapchain image, mark that we need a new one */
		renderer->needNewSwapChain = 1;
		return;
	}

	renderer->shouldPresent = 1;
	renderer->swapChainImageAcquired = 1;
	renderer->currentSwapChainIndex = swapChainImageIndex;

	if (destinationRectangle != NULL)
	{
		dstRect = *destinationRectangle;
	}
	else
	{
		dstRect.x = 0;
		dstRect.y = 0;
		dstRect.w = renderer->swapChainExtent.width;
		dstRect.h = renderer->swapChainExtent.height;
	}

	/* Blit! */

	VULKAN_INTERNAL_BlitImage(
		renderer,
		vulkanCommandBuffer->commandBuffer,
		&textureSlice->rectangle,
		textureSlice->depth,
		textureSlice->layer,
		textureSlice->level,
		vulkanTexture->image,
		&vulkanTexture->resourceAccessType,
		vulkanTexture->resourceAccessType,
		&dstRect,
		0,
		0,
		0,
		renderer->swapChainImages[swapChainImageIndex],
		&renderer->swapChainResourceAccessTypes[swapChainImageIndex],
		RESOURCE_ACCESS_PRESENT,
		RefreshToVK_Filter[filter]
	);
}

static void VULKAN_INTERNAL_DeactivateUnusedBufferDescriptorSets(
	BufferDescriptorSetCache *bufferDescriptorSetCache
) {
	int32_t i, j;
	BufferDescriptorSetHashArray *arr;

	for (i = bufferDescriptorSetCache->count - 1; i >= 0; i -= 1)
	{
		bufferDescriptorSetCache->elements[i].inactiveFrameCount += 1;

		if (bufferDescriptorSetCache->elements[i].inactiveFrameCount + 1 > DESCRIPTOR_SET_DEACTIVATE_FRAMES)
		{
			arr = &bufferDescriptorSetCache->buckets[bufferDescriptorSetCache->elements[i].key % NUM_DESCRIPTOR_SET_HASH_BUCKETS];

			/* remove index from bucket */
			for (j = 0; j < arr->count; j += 1)
			{
				if (arr->elements[j] == i)
				{
					if (j < arr->count - 1)
					{
						arr->elements[j] = arr->elements[arr->count - 1];
					}

					arr->count -= 1;
					break;
				}
			}

			/* remove element from table and place in inactive sets */

			bufferDescriptorSetCache->inactiveDescriptorSets[bufferDescriptorSetCache->inactiveDescriptorSetCount] = bufferDescriptorSetCache->elements[i].descriptorSet;
			bufferDescriptorSetCache->inactiveDescriptorSetCount += 1;

			/* move another descriptor set to fill the hole */
			if (i < bufferDescriptorSetCache->count - 1)
			{
				bufferDescriptorSetCache->elements[i] = bufferDescriptorSetCache->elements[bufferDescriptorSetCache->count - 1];

				/* update index in bucket */
				arr = &bufferDescriptorSetCache->buckets[bufferDescriptorSetCache->elements[i].key % NUM_DESCRIPTOR_SET_HASH_BUCKETS];

				for (j = 0; j < arr->count; j += 1)
				{
					if (arr->elements[j] == bufferDescriptorSetCache->count - 1)
					{
						arr->elements[j] = i;
						break;
					}
				}
			}

			bufferDescriptorSetCache->count -= 1;
		}
	}
}

static void VULKAN_INTERNAL_DeactivateUnusedImageDescriptorSets(
	ImageDescriptorSetCache *imageDescriptorSetCache
) {
	int32_t i, j;
	ImageDescriptorSetHashArray *arr;

	for (i = imageDescriptorSetCache->count - 1; i >= 0; i -= 1)
	{
		imageDescriptorSetCache->elements[i].inactiveFrameCount += 1;

		if (imageDescriptorSetCache->elements[i].inactiveFrameCount + 1 > DESCRIPTOR_SET_DEACTIVATE_FRAMES)
		{
			arr = &imageDescriptorSetCache->buckets[imageDescriptorSetCache->elements[i].key % NUM_DESCRIPTOR_SET_HASH_BUCKETS];

			/* remove index from bucket */
			for (j = 0; j < arr->count; j += 1)
			{
				if (arr->elements[j] == i)
				{
					if (j < arr->count - 1)
					{
						arr->elements[j] = arr->elements[arr->count - 1];
					}

					arr->count -= 1;
					break;
				}
			}

			/* remove element from table and place in inactive sets */

			imageDescriptorSetCache->inactiveDescriptorSets[imageDescriptorSetCache->inactiveDescriptorSetCount] = imageDescriptorSetCache->elements[i].descriptorSet;
			imageDescriptorSetCache->inactiveDescriptorSetCount += 1;

			/* move another descriptor set to fill the hole */
			if (i < imageDescriptorSetCache->count - 1)
			{
				imageDescriptorSetCache->elements[i] = imageDescriptorSetCache->elements[imageDescriptorSetCache->count - 1];

				/* update index in bucket */
				arr = &imageDescriptorSetCache->buckets[imageDescriptorSetCache->elements[i].key % NUM_DESCRIPTOR_SET_HASH_BUCKETS];

				for (j = 0; j < arr->count; j += 1)
				{
					if (arr->elements[j] == imageDescriptorSetCache->count - 1)
					{
						arr->elements[j] = i;
						break;
					}
				}
			}

			imageDescriptorSetCache->count -= 1;
		}
	}
}

static void VULKAN_INTERNAL_ResetDescriptorSetData(VulkanRenderer *renderer)
{
	uint32_t i, j;
	VulkanGraphicsPipelineLayout *graphicsPipelineLayout;
	VulkanComputePipelineLayout *computePipelineLayout;

	for (i = 0; i < NUM_PIPELINE_LAYOUT_BUCKETS; i += 1)
	{
		for (j = 0; j < renderer->graphicsPipelineLayoutHashTable.buckets[i].count; j += 1)
		{
			graphicsPipelineLayout = renderer->graphicsPipelineLayoutHashTable.buckets[i].elements[j].value;

			if (graphicsPipelineLayout->vertexSamplerDescriptorSetCache != NULL)
			{
				VULKAN_INTERNAL_DeactivateUnusedImageDescriptorSets(
					graphicsPipelineLayout->vertexSamplerDescriptorSetCache
				);
			}

			if (graphicsPipelineLayout->fragmentSamplerDescriptorSetCache != NULL)
			{
				VULKAN_INTERNAL_DeactivateUnusedImageDescriptorSets(
					graphicsPipelineLayout->fragmentSamplerDescriptorSetCache
				);
			}
		}

		for (j = 0; j < renderer->computePipelineLayoutHashTable.buckets[i].count; j += 1)
		{
			computePipelineLayout = renderer->computePipelineLayoutHashTable.buckets[i].elements[j].value;

			if (computePipelineLayout->bufferDescriptorSetCache != NULL)
			{
				VULKAN_INTERNAL_DeactivateUnusedBufferDescriptorSets(
					computePipelineLayout->bufferDescriptorSetCache
				);
			}

			if (computePipelineLayout->imageDescriptorSetCache != NULL)
			{
				VULKAN_INTERNAL_DeactivateUnusedImageDescriptorSets(
					computePipelineLayout->imageDescriptorSetCache
				);
			}
		}
	}
}

static void VULKAN_INTERNAL_ResetCommandBuffer(
	VulkanRenderer *renderer,
	VulkanCommandBuffer *commandBuffer
) {
	VkResult vulkanResult;
	VulkanCommandPool *commandPool = commandBuffer->commandPool;

	vulkanResult = renderer->vkResetCommandBuffer(
		commandBuffer->commandBuffer,
		VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkResetCommandBuffer", vulkanResult);
	}

	commandBuffer->submitted = 0;

	commandPool->inactiveCommandBuffers[
		commandPool->inactiveCommandBufferCount
	] = commandBuffer;
	commandPool->inactiveCommandBufferCount += 1;
}

static void VULKAN_Submit(
    Refresh_Renderer *driverData,
	uint32_t commandBufferCount,
	Refresh_CommandBuffer **pCommandBuffers
) {
	VulkanRenderer* renderer = (VulkanRenderer*)driverData;
	VkSubmitInfo transferSubmitInfo, submitInfo;
	VkResult vulkanResult, presentResult = VK_SUCCESS;
	VulkanCommandBuffer *currentCommandBuffer;
	VkCommandBuffer *commandBuffers;
	uint32_t i;
	uint8_t present;

	VkPipelineStageFlags waitStages[2];
	VkSemaphore waitSemaphores[2];
	uint32_t waitSemaphoreCount = 0;
	VkPresentInfoKHR presentInfo;

	if (renderer->pendingTransfer)
	{
		VULKAN_INTERNAL_EndTransferCommandBuffer(renderer);

		transferSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		transferSubmitInfo.pNext = NULL;
		transferSubmitInfo.commandBufferCount = 1;
		transferSubmitInfo.pCommandBuffers = &renderer->transferCommandBuffers[renderer->frameIndex];
		transferSubmitInfo.pWaitDstStageMask = NULL;
		transferSubmitInfo.pWaitSemaphores = NULL;
		transferSubmitInfo.waitSemaphoreCount = 0;
		transferSubmitInfo.pSignalSemaphores = &renderer->transferFinishedSemaphore;
		transferSubmitInfo.signalSemaphoreCount = 1;

		waitSemaphores[waitSemaphoreCount] = renderer->transferFinishedSemaphore;
		waitStages[waitSemaphoreCount] = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		waitSemaphoreCount += 1;
	}

	present = !renderer->headless && renderer->shouldPresent;

	commandBuffers = SDL_stack_alloc(VkCommandBuffer, commandBufferCount);

	for (i = 0; i < commandBufferCount; i += 1)
	{
		currentCommandBuffer = (VulkanCommandBuffer*)pCommandBuffers[i];
		VULKAN_INTERNAL_EndCommandBuffer(renderer, currentCommandBuffer);
		commandBuffers[i] = currentCommandBuffer->commandBuffer;
	}

	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.pNext = NULL;
	submitInfo.commandBufferCount = commandBufferCount;
	submitInfo.pCommandBuffers = commandBuffers;

	if (present)
	{
		waitSemaphores[waitSemaphoreCount] = renderer->imageAvailableSemaphore;
		waitStages[waitSemaphoreCount] = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		waitSemaphoreCount += 1;

		submitInfo.pWaitDstStageMask = waitStages;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &renderer->renderFinishedSemaphore;
	}
	else
	{
		submitInfo.pWaitDstStageMask = waitStages;
		submitInfo.signalSemaphoreCount = 0;
		submitInfo.pSignalSemaphores = NULL;
	}

	submitInfo.waitSemaphoreCount = waitSemaphoreCount;
	submitInfo.pWaitSemaphores = waitSemaphores;

	/* Wait for the previous submission to complete */
	vulkanResult = renderer->vkWaitForFences(
		renderer->logicalDevice,
		1,
		&renderer->inFlightFence,
		VK_TRUE,
		UINT64_MAX
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkWaitForFences", vulkanResult);
		return;
	}

	VULKAN_INTERNAL_PostWorkCleanup(renderer);

	/* Reset the previously submitted command buffers */
	for (i = 0; i < renderer->submittedCommandBufferCount; i += 1)
	{
		if (!renderer->submittedCommandBuffers[i]->fixed)
		{
			VULKAN_INTERNAL_ResetCommandBuffer(
				renderer,
				renderer->submittedCommandBuffers[i]
			);
		}
	}
	renderer->submittedCommandBufferCount = 0;

	/* Prepare the command buffer fence for submission */
	renderer->vkResetFences(
		renderer->logicalDevice,
		1,
		&renderer->inFlightFence
	);

	if (renderer->pendingTransfer)
	{
		/* Submit any pending transfers */
		vulkanResult = renderer->vkQueueSubmit(
			renderer->transferQueue,
			1,
			&transferSubmitInfo,
			NULL
		);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkQueueSubmit", vulkanResult);
			return;
		}
	}

	/* Submit the commands, finally. */
	vulkanResult = renderer->vkQueueSubmit(
		renderer->graphicsQueue,
		1,
		&submitInfo,
		renderer->inFlightFence
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkQueueSubmit", vulkanResult);
		return;
	}

	if (renderer->submittedCommandBufferCount >= renderer->submittedCommandBufferCapacity)
	{
		renderer->submittedCommandBufferCapacity *= 2;

		renderer->submittedCommandBuffers = SDL_realloc(
			renderer->submittedCommandBuffers,
			sizeof(VulkanCommandBuffer*) * renderer->submittedCommandBufferCapacity
		);
	}

	/* Mark command buffers as submitted */
	for (i = 0; i < commandBufferCount; i += 1)
	{
		((VulkanCommandBuffer*)pCommandBuffers[i])->submitted = 1;
		renderer->submittedCommandBuffers[renderer->submittedCommandBufferCount] = (VulkanCommandBuffer*) pCommandBuffers[i];
	}
	renderer->submittedCommandBufferCount = commandBufferCount;

	/* Reset UBOs */

	SDL_LockMutex(renderer->uniformBufferLock);
	renderer->vertexUBOOffset = UBO_BUFFER_SIZE * renderer->frameIndex;
	renderer->vertexUBOBlockIncrement = 0;
	renderer->fragmentUBOOffset = UBO_BUFFER_SIZE * renderer->frameIndex;
	renderer->fragmentUBOBlockIncrement = 0;
	renderer->computeUBOOffset = UBO_BUFFER_SIZE * renderer->frameIndex;
	renderer->computeUBOBlockIncrement = 0;
	SDL_UnlockMutex(renderer->uniformBufferLock);

	/* Reset descriptor set data */
	VULKAN_INTERNAL_ResetDescriptorSetData(renderer);

	/* Present, if applicable */

	if (present)
	{
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.pNext = NULL;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &renderer->renderFinishedSemaphore;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &renderer->swapChain;
		presentInfo.pImageIndices = &renderer->currentSwapChainIndex;
		presentInfo.pResults = NULL;

		presentResult = renderer->vkQueuePresentKHR(
			renderer->presentQueue,
			&presentInfo
		);

		if (presentResult != VK_SUCCESS || renderer->needNewSwapChain)
		{
			VULKAN_INTERNAL_RecreateSwapchain(renderer);
		}
	}

	renderer->swapChainImageAcquired = 0;
	renderer->shouldPresent = 0;
	renderer->pendingTransfer = 0;
	renderer->textureStagingBufferOffset = 0;

	SDL_stack_free(commandBuffers);
}

static void VULKAN_Wait(
    Refresh_Renderer *driverData
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;

	renderer->vkWaitForFences(
		renderer->logicalDevice,
		1,
		&renderer->inFlightFence,
		VK_TRUE,
		UINT64_MAX
	);
}

/* External interop */

static void VULKAN_GetTextureHandles(
	Refresh_Renderer* driverData,
	Refresh_Texture* texture,
	Refresh_TextureHandles *handles
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTexture *vulkanTexture = (VulkanTexture*) texture;

	handles->rendererType = REFRESH_RENDERER_TYPE_VULKAN;
	handles->texture.vulkan.image = vulkanTexture->image;
	handles->texture.vulkan.view = vulkanTexture->view;
}

/* Device instantiation */

static inline uint8_t VULKAN_INTERNAL_SupportsExtension(
	const char *ext,
	VkExtensionProperties *availableExtensions,
	uint32_t numAvailableExtensions
) {
	uint32_t i;
	for (i = 0; i < numAvailableExtensions; i += 1)
	{
		if (SDL_strcmp(ext, availableExtensions[i].extensionName) == 0)
		{
			return 1;
		}
	}
	return 0;
}

static uint8_t VULKAN_INTERNAL_CheckInstanceExtensions(
	const char **requiredExtensions,
	uint32_t requiredExtensionsLength,
	uint8_t *supportsDebugUtils
) {
	uint32_t extensionCount, i;
	VkExtensionProperties *availableExtensions;
	uint8_t allExtensionsSupported = 1;

	vkEnumerateInstanceExtensionProperties(
		NULL,
		&extensionCount,
		NULL
	);
	availableExtensions = SDL_stack_alloc(
		VkExtensionProperties,
		extensionCount
	);
	vkEnumerateInstanceExtensionProperties(
		NULL,
		&extensionCount,
		availableExtensions
	);

	for (i = 0; i < requiredExtensionsLength; i += 1)
	{
		if (!VULKAN_INTERNAL_SupportsExtension(
			requiredExtensions[i],
			availableExtensions,
			extensionCount
		)) {
			allExtensionsSupported = 0;
			break;
		}
	}

	/* This is optional, but nice to have! */
	*supportsDebugUtils = VULKAN_INTERNAL_SupportsExtension(
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
		availableExtensions,
		extensionCount
	);

	SDL_stack_free(availableExtensions);
	return allExtensionsSupported;
}

static uint8_t VULKAN_INTERNAL_CheckValidationLayers(
	const char** validationLayers,
	uint32_t validationLayersLength
) {
	uint32_t layerCount;
	VkLayerProperties *availableLayers;
	uint32_t i, j;
	uint8_t layerFound = 0;

	vkEnumerateInstanceLayerProperties(&layerCount, NULL);
	availableLayers = SDL_stack_alloc(VkLayerProperties, layerCount);
	vkEnumerateInstanceLayerProperties(&layerCount, availableLayers);

	for (i = 0; i < validationLayersLength; i += 1)
	{
		layerFound = 0;

		for (j = 0; j < layerCount; j += 1)
		{
			if (SDL_strcmp(validationLayers[i], availableLayers[j].layerName) == 0)
			{
				layerFound = 1;
				break;
			}
		}

		if (!layerFound)
		{
			break;
		}
	}

	SDL_stack_free(availableLayers);
	return layerFound;
}

static uint8_t VULKAN_INTERNAL_CreateInstance(
    VulkanRenderer *renderer,
    void *deviceWindowHandle
) {
	VkResult vulkanResult;
	VkApplicationInfo appInfo;
	const char **instanceExtensionNames;
	uint32_t instanceExtensionCount;
	VkInstanceCreateInfo createInfo;
	static const char *layerNames[] = { "VK_LAYER_KHRONOS_validation" };

	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pNext = NULL;
	appInfo.pApplicationName = NULL;
	appInfo.applicationVersion = 0;
	appInfo.pEngineName = "REFRESH";
	appInfo.engineVersion = REFRESH_COMPILED_VERSION;
	appInfo.apiVersion = VK_MAKE_VERSION(1, 0, 0);

    if (!SDL_Vulkan_GetInstanceExtensions(
        (SDL_Window*) deviceWindowHandle,
        &instanceExtensionCount,
        NULL
    )) {
        Refresh_LogError(
            "SDL_Vulkan_GetInstanceExtensions(): getExtensionCount: %s",
            SDL_GetError()
        );

        return 0;
    }

	/* Extra space for the following extensions:
	 * VK_KHR_get_physical_device_properties2
	 * VK_EXT_debug_utils
	 */
	instanceExtensionNames = SDL_stack_alloc(
		const char*,
		instanceExtensionCount + 2
	);

	if (!SDL_Vulkan_GetInstanceExtensions(
		(SDL_Window*) deviceWindowHandle,
		&instanceExtensionCount,
		instanceExtensionNames
	)) {
		Refresh_LogError(
			"SDL_Vulkan_GetInstanceExtensions(): %s",
			SDL_GetError()
		);

        SDL_stack_free((char*) instanceExtensionNames);
        return 0;
	}

	/* Core since 1.1 */
	instanceExtensionNames[instanceExtensionCount++] =
		VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;

	if (!VULKAN_INTERNAL_CheckInstanceExtensions(
		instanceExtensionNames,
		instanceExtensionCount,
		&renderer->supportsDebugUtils
	)) {
		Refresh_LogError(
			"Required Vulkan instance extensions not supported"
		);

        SDL_stack_free((char*) instanceExtensionNames);
        return 0;
	}

	if (renderer->supportsDebugUtils)
	{
		/* Append the debug extension to the end */
		instanceExtensionNames[instanceExtensionCount++] =
			VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	}
	else
	{
		Refresh_LogWarn(
			"%s is not supported!",
			VK_EXT_DEBUG_UTILS_EXTENSION_NAME
		);
	}

    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pNext = NULL;
	createInfo.flags = 0;
	createInfo.pApplicationInfo = &appInfo;
	createInfo.ppEnabledLayerNames = layerNames;
	createInfo.enabledExtensionCount = instanceExtensionCount;
	createInfo.ppEnabledExtensionNames = instanceExtensionNames;
	if (renderer->debugMode)
	{
		createInfo.enabledLayerCount = SDL_arraysize(layerNames);
		if (!VULKAN_INTERNAL_CheckValidationLayers(
			layerNames,
			createInfo.enabledLayerCount
		)) {
			Refresh_LogWarn("Validation layers not found, continuing without validation");
			createInfo.enabledLayerCount = 0;
		}
	}
	else
	{
		createInfo.enabledLayerCount = 0;
	}

    vulkanResult = vkCreateInstance(&createInfo, NULL, &renderer->instance);
	if (vulkanResult != VK_SUCCESS)
	{
		Refresh_LogError(
			"vkCreateInstance failed: %s",
			VkErrorMessages(vulkanResult)
		);

        SDL_stack_free((char*) instanceExtensionNames);
        return 0;
	}

	SDL_stack_free((char*) instanceExtensionNames);
	return 1;
}

static uint8_t VULKAN_INTERNAL_CheckDeviceExtensions(
	VulkanRenderer *renderer,
	VkPhysicalDevice physicalDevice,
	const char** requiredExtensions,
	uint32_t requiredExtensionsLength
) {
	uint32_t extensionCount, i;
	VkExtensionProperties *availableExtensions;
	uint8_t allExtensionsSupported = 1;

	renderer->vkEnumerateDeviceExtensionProperties(
		physicalDevice,
		NULL,
		&extensionCount,
		NULL
	);
	availableExtensions = SDL_stack_alloc(
		VkExtensionProperties,
		extensionCount
	);
	renderer->vkEnumerateDeviceExtensionProperties(
		physicalDevice,
		NULL,
		&extensionCount,
		availableExtensions
	);

	for (i = 0; i < requiredExtensionsLength; i += 1)
	{
		if (!VULKAN_INTERNAL_SupportsExtension(
			requiredExtensions[i],
			availableExtensions,
			extensionCount
		)) {
			allExtensionsSupported = 0;
			break;
		}
	}

	SDL_stack_free(availableExtensions);
	return allExtensionsSupported;
}

static uint8_t VULKAN_INTERNAL_IsDeviceSuitable(
	VulkanRenderer *renderer,
	VkPhysicalDevice physicalDevice,
	const char** requiredExtensionNames,
	uint32_t requiredExtensionNamesLength,
	VkSurfaceKHR surface,
	QueueFamilyIndices *queueFamilyIndices,
	uint8_t *isIdeal
) {
	uint32_t queueFamilyCount, i;
	SwapChainSupportDetails swapChainSupportDetails;
	VkQueueFamilyProperties *queueProps;
	VkBool32 supportsPresent;
	uint8_t querySuccess = 0;
	uint8_t foundFamily = 0;
	uint8_t foundSuitableDevice = 0;
	VkPhysicalDeviceProperties deviceProperties;

	queueFamilyIndices->graphicsFamily = UINT32_MAX;
	queueFamilyIndices->presentFamily = UINT32_MAX;
	queueFamilyIndices->computeFamily = UINT32_MAX;
	queueFamilyIndices->transferFamily = UINT32_MAX;
	*isIdeal = 0;

	/* Note: If no dedicated device exists,
	 * one that supports our features would be fine
	 */

	if (!VULKAN_INTERNAL_CheckDeviceExtensions(
		renderer,
		physicalDevice,
		requiredExtensionNames,
		requiredExtensionNamesLength
	)) {
		return 0;
	}

	renderer->vkGetPhysicalDeviceQueueFamilyProperties(
		physicalDevice,
		&queueFamilyCount,
		NULL
	);

	/* FIXME: Need better structure for checking vs storing support details */
	querySuccess = VULKAN_INTERNAL_QuerySwapChainSupport(
		renderer,
		physicalDevice,
		surface,
		&swapChainSupportDetails
	);
	SDL_free(swapChainSupportDetails.formats);
	SDL_free(swapChainSupportDetails.presentModes);
	if (	querySuccess == 0 ||
		swapChainSupportDetails.formatsLength == 0 ||
		swapChainSupportDetails.presentModesLength == 0	)
	{
		return 0;
	}

	queueProps = (VkQueueFamilyProperties*) SDL_stack_alloc(
		VkQueueFamilyProperties,
		queueFamilyCount
	);
	renderer->vkGetPhysicalDeviceQueueFamilyProperties(
		physicalDevice,
		&queueFamilyCount,
		queueProps
	);

	for (i = 0; i < queueFamilyCount; i += 1)
	{
		renderer->vkGetPhysicalDeviceSurfaceSupportKHR(
			physicalDevice,
			i,
			surface,
			&supportsPresent
		);
		if (!foundFamily)
		{
			if (	supportsPresent &&
				(queueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
				(queueProps[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
				(queueProps[i].queueFlags & VK_QUEUE_TRANSFER_BIT)	)
			{
				queueFamilyIndices->graphicsFamily = i;
				queueFamilyIndices->presentFamily = i;
				queueFamilyIndices->computeFamily = i;
				queueFamilyIndices->transferFamily = i;
				foundFamily = 1;
			}
		}

		if (foundFamily)
		{
			foundSuitableDevice = 1;
			break;
		}
	}

	SDL_stack_free(queueProps);

	if (foundSuitableDevice)
	{
		/* We'd really like a discrete GPU, but it's OK either way! */
		renderer->vkGetPhysicalDeviceProperties(
			physicalDevice,
			&deviceProperties
		);
		if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			*isIdeal = 1;
		}
		return 1;
	}

	/* This device is useless for us, next! */
	return 0;
}

static VULKAN_INTERNAL_GetPhysicalDeviceProperties(
	VulkanRenderer *renderer
) {
	renderer->physicalDeviceDriverProperties.sType =
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES_KHR;
	renderer->physicalDeviceDriverProperties.pNext = NULL;

	renderer->physicalDeviceProperties.sType =
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	renderer->physicalDeviceProperties.pNext =
		&renderer->physicalDeviceDriverProperties;

	renderer->vkGetPhysicalDeviceProperties2KHR(
		renderer->physicalDevice,
		&renderer->physicalDeviceProperties
	);
}

static uint8_t VULKAN_INTERNAL_DeterminePhysicalDevice(
	VulkanRenderer *renderer,
	const char **deviceExtensionNames,
	uint32_t deviceExtensionCount
) {
	VkResult vulkanResult;
	VkPhysicalDevice *physicalDevices;
	uint32_t physicalDeviceCount, i, suitableIndex;
	VkPhysicalDevice physicalDevice;
	QueueFamilyIndices queueFamilyIndices;
	uint8_t isIdeal;

	vulkanResult = renderer->vkEnumeratePhysicalDevices(
		renderer->instance,
		&physicalDeviceCount,
		NULL
	);

	if (vulkanResult != VK_SUCCESS)
	{
		Refresh_LogError(
			"vkEnumeratePhysicalDevices failed: %s",
			VkErrorMessages(vulkanResult)
		);
		return 0;
	}

	if (physicalDeviceCount == 0)
	{
		Refresh_LogError("Failed to find any GPUs with Vulkan support");
		return 0;
	}

	physicalDevices = SDL_stack_alloc(VkPhysicalDevice, physicalDeviceCount);

	vulkanResult = renderer->vkEnumeratePhysicalDevices(
		renderer->instance,
		&physicalDeviceCount,
		physicalDevices
	);

	if (vulkanResult != VK_SUCCESS)
	{
		Refresh_LogError(
			"vkEnumeratePhysicalDevices failed: %s",
			VkErrorMessages(vulkanResult)
		);
		SDL_stack_free(physicalDevices);
		return 0;
	}

	/* Any suitable device will do, but we'd like the best */
	suitableIndex = -1;
	for (i = 0; i < physicalDeviceCount; i += 1)
	{
		if (VULKAN_INTERNAL_IsDeviceSuitable(
			renderer,
			physicalDevices[i],
			deviceExtensionNames,
			deviceExtensionCount,
			renderer->surface,
			&queueFamilyIndices,
			&isIdeal
		)) {
			suitableIndex = i;
			if (isIdeal)
			{
				/* This is the one we want! */
				break;
			}
		}
	}

	if (suitableIndex != -1)
	{
		physicalDevice = physicalDevices[suitableIndex];
	}
	else
	{
		Refresh_LogError("No suitable physical devices found");
		SDL_stack_free(physicalDevices);
		return 0;
	}

	renderer->physicalDevice = physicalDevice;
	renderer->queueFamilyIndices = queueFamilyIndices;

	VULKAN_INTERNAL_GetPhysicalDeviceProperties(renderer);

	SDL_stack_free(physicalDevices);
	return 1;
}

static uint8_t VULKAN_INTERNAL_CreateLogicalDevice(
	VulkanRenderer *renderer,
	const char **deviceExtensionNames,
	uint32_t deviceExtensionCount
) {
	VkResult vulkanResult;

	VkDeviceCreateInfo deviceCreateInfo;
	VkPhysicalDeviceFeatures deviceFeatures;

	VkDeviceQueueCreateInfo queueCreateInfos[2];
	VkDeviceQueueCreateInfo queueCreateInfoGraphics;
	VkDeviceQueueCreateInfo queueCreateInfoPresent;

	int32_t queueInfoCount = 1;
	float queuePriority = 1.0f;

	queueCreateInfoGraphics.sType =
		VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueCreateInfoGraphics.pNext = NULL;
	queueCreateInfoGraphics.flags = 0;
	queueCreateInfoGraphics.queueFamilyIndex =
		renderer->queueFamilyIndices.graphicsFamily;
	queueCreateInfoGraphics.queueCount = 1;
	queueCreateInfoGraphics.pQueuePriorities = &queuePriority;

	queueCreateInfos[0] = queueCreateInfoGraphics;

	if (renderer->queueFamilyIndices.presentFamily != renderer->queueFamilyIndices.graphicsFamily)
	{
		queueCreateInfoPresent.sType =
			VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfoPresent.pNext = NULL;
		queueCreateInfoPresent.flags = 0;
		queueCreateInfoPresent.queueFamilyIndex =
			renderer->queueFamilyIndices.presentFamily;
		queueCreateInfoPresent.queueCount = 1;
		queueCreateInfoPresent.pQueuePriorities = &queuePriority;

		queueCreateInfos[queueInfoCount] = queueCreateInfoPresent;
		queueInfoCount += 1;
	}

	/* specifying used device features */

	SDL_zero(deviceFeatures);
	deviceFeatures.occlusionQueryPrecise = VK_TRUE;
	deviceFeatures.fillModeNonSolid = VK_TRUE;

	/* creating the logical device */

	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceCreateInfo.pNext = NULL;
	deviceCreateInfo.flags = 0;
	deviceCreateInfo.queueCreateInfoCount = queueInfoCount;
	deviceCreateInfo.pQueueCreateInfos = queueCreateInfos;
	deviceCreateInfo.enabledLayerCount = 0;
	deviceCreateInfo.ppEnabledLayerNames = NULL;
	deviceCreateInfo.enabledExtensionCount = deviceExtensionCount;
	deviceCreateInfo.ppEnabledExtensionNames = deviceExtensionNames;
	deviceCreateInfo.pEnabledFeatures = &deviceFeatures;

	vulkanResult = renderer->vkCreateDevice(
		renderer->physicalDevice,
		&deviceCreateInfo,
		NULL,
		&renderer->logicalDevice
	);
	if (vulkanResult != VK_SUCCESS)
	{
		Refresh_LogError(
			"vkCreateDevice failed: %s",
			VkErrorMessages(vulkanResult)
		);
		return 0;
	}

	/* Load vkDevice entry points */

	#define VULKAN_DEVICE_FUNCTION(ext, ret, func, params) \
		renderer->func = (vkfntype_##func) \
			renderer->vkGetDeviceProcAddr( \
				renderer->logicalDevice, \
				#func \
			);
	#include "Refresh_Driver_Vulkan_vkfuncs.h"

	renderer->vkGetDeviceQueue(
		renderer->logicalDevice,
		renderer->queueFamilyIndices.graphicsFamily,
		0,
		&renderer->graphicsQueue
	);

	renderer->vkGetDeviceQueue(
		renderer->logicalDevice,
		renderer->queueFamilyIndices.presentFamily,
		0,
		&renderer->presentQueue
	);

	renderer->vkGetDeviceQueue(
		renderer->logicalDevice,
		renderer->queueFamilyIndices.computeFamily,
		0,
		&renderer->computeQueue
	);

	renderer->vkGetDeviceQueue(
		renderer->logicalDevice,
		renderer->queueFamilyIndices.transferFamily,
		0,
		&renderer->transferQueue
	);

	return 1;
}

static void VULKAN_INTERNAL_LoadEntryPoints(
	VulkanRenderer *renderer
) {
	/* Load Vulkan entry points */
	if (SDL_Vulkan_LoadLibrary(NULL) < 0)
	{
		Refresh_LogWarn("Vulkan: SDL_Vulkan_LoadLibrary failed!");
		return;
	}

	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wpedantic"
		vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
	#pragma GCC diagnostic pop
		if (vkGetInstanceProcAddr == NULL)
		{
			Refresh_LogWarn(
				"SDL_Vulkan_GetVkGetInstanceProcAddr(): %s",
				SDL_GetError()
			);
			return;
		}

	#define VULKAN_GLOBAL_FUNCTION(name)								\
			name = (PFN_##name) vkGetInstanceProcAddr(VK_NULL_HANDLE, #name);			\
			if (name == NULL)									\
			{											\
				Refresh_LogWarn("vkGetInstanceProcAddr(VK_NULL_HANDLE, \"" #name "\") failed");	\
				return;									\
			}
	#include "Refresh_Driver_Vulkan_vkfuncs.h"
}

/* Expects a partially initialized VulkanRenderer */
static Refresh_Device* VULKAN_INTERNAL_CreateDevice(
	VulkanRenderer *renderer
) {
    Refresh_Device *result;

    VkResult vulkanResult;
	uint32_t i;

    /* Variables: Create fence and semaphores */
	VkFenceCreateInfo fenceInfo;
	VkSemaphoreCreateInfo semaphoreInfo;

	/* Variables: Transfer command buffer */
	VkCommandPoolCreateInfo transferCommandPoolCreateInfo;
	VkCommandBufferAllocateInfo transferCommandBufferAllocateInfo;

	/* Variables: Descriptor set layouts */
	VkDescriptorSetLayoutCreateInfo setLayoutCreateInfo;
	VkDescriptorSetLayoutBinding vertexParamLayoutBinding;
	VkDescriptorSetLayoutBinding fragmentParamLayoutBinding;
	VkDescriptorSetLayoutBinding computeParamLayoutBinding;

	VkDescriptorSetLayoutBinding emptyVertexSamplerLayoutBinding;
	VkDescriptorSetLayoutBinding emptyFragmentSamplerLayoutBinding;
	VkDescriptorSetLayoutBinding emptyComputeBufferDescriptorSetLayoutBinding;
	VkDescriptorSetLayoutBinding emptyComputeImageDescriptorSetLayoutBinding;

	/* Variables: UBO Creation */
	VkDescriptorPoolCreateInfo defaultDescriptorPoolInfo;
	VkDescriptorPoolSize poolSizes[4];
	VkDescriptorSetAllocateInfo descriptorAllocateInfo;

    result = (Refresh_Device*) SDL_malloc(sizeof(Refresh_Device));
    ASSIGN_DRIVER(VULKAN)

    result->driverData = (Refresh_Renderer*) renderer;

	/*
	 * Create initial swapchain
	 */

    if (!renderer->headless)
    {
        if (VULKAN_INTERNAL_CreateSwapchain(renderer) != CREATE_SWAPCHAIN_SUCCESS)
        {
            Refresh_LogError("Failed to create swap chain");
            return NULL;
        }
    }

	renderer->needNewSwapChain = 0;
	renderer->shouldPresent = 0;
	renderer->swapChainImageAcquired = 0;

	/*
	 * Create fence and semaphores
	 */

	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.pNext = NULL;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphoreInfo.pNext = NULL;
	semaphoreInfo.flags = 0;

	vulkanResult = renderer->vkCreateSemaphore(
		renderer->logicalDevice,
		&semaphoreInfo,
		NULL,
		&renderer->transferFinishedSemaphore
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateSemaphore", vulkanResult);
		return NULL;
	}

	vulkanResult = renderer->vkCreateSemaphore(
		renderer->logicalDevice,
		&semaphoreInfo,
		NULL,
		&renderer->imageAvailableSemaphore
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateSemaphore", vulkanResult);
		return NULL;
	}

	vulkanResult = renderer->vkCreateSemaphore(
		renderer->logicalDevice,
		&semaphoreInfo,
		NULL,
		&renderer->renderFinishedSemaphore
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateSemaphore", vulkanResult);
		return NULL;
	}

	vulkanResult = renderer->vkCreateFence(
		renderer->logicalDevice,
		&fenceInfo,
		NULL,
		&renderer->inFlightFence
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateFence", vulkanResult);
		return NULL;
	}

	/* Threading */

	renderer->allocatorLock = SDL_CreateMutex();
	renderer->disposeLock = SDL_CreateMutex();
	renderer->uniformBufferLock = SDL_CreateMutex();
	renderer->descriptorSetLock = SDL_CreateMutex();
	renderer->boundBufferLock = SDL_CreateMutex();
	renderer->stagingLock = SDL_CreateMutex();

	/* Transfer buffer */

	transferCommandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	transferCommandPoolCreateInfo.pNext = NULL;
	transferCommandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	transferCommandPoolCreateInfo.queueFamilyIndex = renderer->queueFamilyIndices.transferFamily;

	vulkanResult = renderer->vkCreateCommandPool(
		renderer->logicalDevice,
		&transferCommandPoolCreateInfo,
		NULL,
		&renderer->transferCommandPool
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateCommandPool", vulkanResult);
		return NULL;
	}

	transferCommandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	transferCommandBufferAllocateInfo.pNext = NULL;
	transferCommandBufferAllocateInfo.commandBufferCount = 2;
	transferCommandBufferAllocateInfo.commandPool = renderer->transferCommandPool;
	transferCommandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

	vulkanResult = renderer->vkAllocateCommandBuffers(
		renderer->logicalDevice,
		&transferCommandBufferAllocateInfo,
		renderer->transferCommandBuffers
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkAllocateCommandBuffers", vulkanResult);
		return NULL;
	}

	renderer->pendingTransfer = 0;

	/*
	 * Create submitted command buffer list
	 */

	renderer->submittedCommandBufferCapacity = 16;
	renderer->submittedCommandBufferCount = 0;
	renderer->submittedCommandBuffers = SDL_malloc(sizeof(VulkanCommandBuffer*) * renderer->submittedCommandBufferCapacity);

	/* Memory Allocator */

		renderer->memoryAllocator = (VulkanMemoryAllocator*) SDL_malloc(
		sizeof(VulkanMemoryAllocator)
	);

	for (i = 0; i < VK_MAX_MEMORY_TYPES; i += 1)
	{
		renderer->memoryAllocator->subAllocators[i].nextAllocationSize = STARTING_ALLOCATION_SIZE;
		renderer->memoryAllocator->subAllocators[i].allocations = NULL;
		renderer->memoryAllocator->subAllocators[i].allocationCount = 0;
		renderer->memoryAllocator->subAllocators[i].sortedFreeRegions = SDL_malloc(
			sizeof(VulkanMemoryFreeRegion*) * 4
		);
		renderer->memoryAllocator->subAllocators[i].sortedFreeRegionCount = 0;
		renderer->memoryAllocator->subAllocators[i].sortedFreeRegionCapacity = 4;
	}

	/* UBO Data */

	renderer->vertexUBO = (VulkanBuffer*) SDL_malloc(sizeof(VulkanBuffer));

	if (!VULKAN_INTERNAL_CreateBuffer(
		renderer,
		UBO_ACTUAL_SIZE,
		RESOURCE_ACCESS_VERTEX_SHADER_READ_UNIFORM_BUFFER,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		1,
		renderer->vertexUBO
	)) {
		Refresh_LogError("Failed to create vertex UBO!");
		return NULL;
	}

	renderer->fragmentUBO = (VulkanBuffer*) SDL_malloc(sizeof(VulkanBuffer));

	if (!VULKAN_INTERNAL_CreateBuffer(
		renderer,
		UBO_ACTUAL_SIZE,
		RESOURCE_ACCESS_FRAGMENT_SHADER_READ_UNIFORM_BUFFER,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		1,
		renderer->fragmentUBO
	)) {
		Refresh_LogError("Failed to create fragment UBO!");
		return NULL;
	}

	renderer->computeUBO = (VulkanBuffer*) SDL_malloc(sizeof(VulkanBuffer));

	if (!VULKAN_INTERNAL_CreateBuffer(
		renderer,
		UBO_ACTUAL_SIZE,
		RESOURCE_ACCESS_COMPUTE_SHADER_READ_UNIFORM_BUFFER,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		1,
		renderer->computeUBO
	)) {
		Refresh_LogError("Failed to create compute UBO!");
		return NULL;
	}

	renderer->minUBOAlignment = renderer->physicalDeviceProperties.properties.limits.minUniformBufferOffsetAlignment;
	renderer->vertexUBOOffset = 0;
	renderer->vertexUBOBlockIncrement = 0;
	renderer->fragmentUBOOffset = 0;
	renderer->fragmentUBOBlockIncrement = 0;
	renderer->computeUBOOffset = 0;
	renderer->computeUBOBlockIncrement = 0;

	/* Set up UBO layouts */

	emptyVertexSamplerLayoutBinding.binding = 0;
	emptyVertexSamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	emptyVertexSamplerLayoutBinding.descriptorCount = 0;
	emptyVertexSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	emptyVertexSamplerLayoutBinding.pImmutableSamplers = NULL;

	setLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setLayoutCreateInfo.pNext = NULL;
	setLayoutCreateInfo.flags = 0;
	setLayoutCreateInfo.bindingCount = 1;
	setLayoutCreateInfo.pBindings = &emptyVertexSamplerLayoutBinding;

	vulkanResult = renderer->vkCreateDescriptorSetLayout(
		renderer->logicalDevice,
		&setLayoutCreateInfo,
		NULL,
		&renderer->emptyVertexSamplerLayout
	);

	emptyFragmentSamplerLayoutBinding.binding = 0;
	emptyFragmentSamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	emptyFragmentSamplerLayoutBinding.descriptorCount = 0;
	emptyFragmentSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	emptyFragmentSamplerLayoutBinding.pImmutableSamplers = NULL;

	setLayoutCreateInfo.pBindings = &emptyFragmentSamplerLayoutBinding;

	vulkanResult = renderer->vkCreateDescriptorSetLayout(
		renderer->logicalDevice,
		&setLayoutCreateInfo,
		NULL,
		&renderer->emptyFragmentSamplerLayout
	);

	emptyComputeBufferDescriptorSetLayoutBinding.binding = 0;
	emptyComputeBufferDescriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	emptyComputeBufferDescriptorSetLayoutBinding.descriptorCount = 0;
	emptyComputeBufferDescriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	emptyComputeBufferDescriptorSetLayoutBinding.pImmutableSamplers = NULL;

	setLayoutCreateInfo.pBindings = &emptyComputeBufferDescriptorSetLayoutBinding;

	vulkanResult = renderer->vkCreateDescriptorSetLayout(
		renderer->logicalDevice,
		&setLayoutCreateInfo,
		NULL,
		&renderer->emptyComputeBufferDescriptorSetLayout
	);

	emptyComputeImageDescriptorSetLayoutBinding.binding = 0;
	emptyComputeImageDescriptorSetLayoutBinding.descriptorCount = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	emptyComputeImageDescriptorSetLayoutBinding.descriptorCount = 0;
	emptyComputeImageDescriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	emptyComputeImageDescriptorSetLayoutBinding.pImmutableSamplers = NULL;

	setLayoutCreateInfo.pBindings = &emptyComputeImageDescriptorSetLayoutBinding;

	vulkanResult = renderer->vkCreateDescriptorSetLayout(
		renderer->logicalDevice,
		&setLayoutCreateInfo,
		NULL,
		&renderer->emptyComputeImageDescriptorSetLayout
	);

	vertexParamLayoutBinding.binding = 0;
	vertexParamLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	vertexParamLayoutBinding.descriptorCount = 1;
	vertexParamLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	vertexParamLayoutBinding.pImmutableSamplers = NULL;

	setLayoutCreateInfo.bindingCount = 1;
	setLayoutCreateInfo.pBindings = &vertexParamLayoutBinding;

	vulkanResult = renderer->vkCreateDescriptorSetLayout(
		renderer->logicalDevice,
		&setLayoutCreateInfo,
		NULL,
		&renderer->vertexParamLayout
	);

	if (vulkanResult != VK_SUCCESS)
	{
		Refresh_LogError("Failed to create vertex UBO layout!");
		return NULL;
	}

	fragmentParamLayoutBinding.binding = 0;
	fragmentParamLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	fragmentParamLayoutBinding.descriptorCount = 1;
	fragmentParamLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragmentParamLayoutBinding.pImmutableSamplers = NULL;

	setLayoutCreateInfo.bindingCount = 1;
	setLayoutCreateInfo.pBindings = &fragmentParamLayoutBinding;

	vulkanResult = renderer->vkCreateDescriptorSetLayout(
		renderer->logicalDevice,
		&setLayoutCreateInfo,
		NULL,
		&renderer->fragmentParamLayout
	);

	if (vulkanResult != VK_SUCCESS)
	{
		Refresh_LogError("Failed to create fragment UBO layout!");
		return NULL;
	}

	computeParamLayoutBinding.binding = 0;
	computeParamLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	computeParamLayoutBinding.descriptorCount = 1;
	computeParamLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	computeParamLayoutBinding.pImmutableSamplers = NULL;

	setLayoutCreateInfo.bindingCount = 1;
	setLayoutCreateInfo.pBindings = &computeParamLayoutBinding;

	vulkanResult = renderer->vkCreateDescriptorSetLayout(
		renderer->logicalDevice,
		&setLayoutCreateInfo,
		NULL,
		&renderer->computeParamLayout
	);

	/* Default Descriptors */

	/* default empty sampler descriptor sets */
	poolSizes[0].descriptorCount = 2;
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

	/* UBO descriptor sets */
	poolSizes[1].descriptorCount = UBO_POOL_SIZE;
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;

	poolSizes[2].descriptorCount = 1;
	poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

	poolSizes[3].descriptorCount = 1;
	poolSizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

	defaultDescriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	defaultDescriptorPoolInfo.pNext = NULL;
	defaultDescriptorPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	defaultDescriptorPoolInfo.maxSets = UBO_POOL_SIZE + 2 + 1 + 1;
	defaultDescriptorPoolInfo.poolSizeCount = 4;
	defaultDescriptorPoolInfo.pPoolSizes = poolSizes;

	renderer->vkCreateDescriptorPool(
		renderer->logicalDevice,
		&defaultDescriptorPoolInfo,
		NULL,
		&renderer->defaultDescriptorPool
	);

	descriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorAllocateInfo.pNext = NULL;
	descriptorAllocateInfo.descriptorPool = renderer->defaultDescriptorPool;
	descriptorAllocateInfo.descriptorSetCount = 1;
	descriptorAllocateInfo.pSetLayouts = &renderer->emptyVertexSamplerLayout;

	renderer->vkAllocateDescriptorSets(
		renderer->logicalDevice,
		&descriptorAllocateInfo,
		&renderer->emptyVertexSamplerDescriptorSet
	);

	descriptorAllocateInfo.pSetLayouts = &renderer->emptyFragmentSamplerLayout;

	renderer->vkAllocateDescriptorSets(
		renderer->logicalDevice,
		&descriptorAllocateInfo,
		&renderer->emptyFragmentSamplerDescriptorSet
	);

	descriptorAllocateInfo.pSetLayouts = &renderer->emptyComputeBufferDescriptorSetLayout;

	renderer->vkAllocateDescriptorSets(
		renderer->logicalDevice,
		&descriptorAllocateInfo,
		&renderer->emptyComputeBufferDescriptorSet
	);

	descriptorAllocateInfo.pSetLayouts = &renderer->emptyComputeImageDescriptorSetLayout;

	renderer->vkAllocateDescriptorSets(
		renderer->logicalDevice,
		&descriptorAllocateInfo,
		&renderer->emptyComputeImageDescriptorSet
	);

	/* Initialize buffer space */

	renderer->buffersInUseCapacity = 32;
	renderer->buffersInUseCount = 0;
	renderer->buffersInUse = (VulkanBuffer**)SDL_malloc(
		sizeof(VulkanBuffer*) * renderer->buffersInUseCapacity
	);

	renderer->submittedBufferCapacity = 32;
	renderer->submittedBufferCount = 0;
	renderer->submittedBuffers = (VulkanBuffer**)SDL_malloc(
		sizeof(VulkanBuffer*) * renderer->submittedBufferCapacity
	);

	/* Staging Buffer */

	renderer->textureStagingBuffer = (VulkanBuffer*) SDL_malloc(sizeof(VulkanBuffer));

	if (!VULKAN_INTERNAL_CreateBuffer(
		renderer,
		TEXTURE_STAGING_SIZE,
		RESOURCE_ACCESS_MEMORY_TRANSFER_READ_WRITE,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		1,
		renderer->textureStagingBuffer
	)) {
		Refresh_LogError("Failed to create texture staging buffer!");
		return NULL;
	}

	renderer->textureStagingBufferOffset = 0;

	/* Dummy Uniform Buffers */

	renderer->dummyVertexUniformBuffer = (VulkanBuffer*) SDL_malloc(sizeof(VulkanBuffer));

	if (!VULKAN_INTERNAL_CreateBuffer(
		renderer,
		16,
		RESOURCE_ACCESS_VERTEX_SHADER_READ_UNIFORM_BUFFER,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		1,
		renderer->dummyVertexUniformBuffer
	)) {
		Refresh_LogError("Failed to create dummy vertex uniform buffer!");
		return NULL;
	}

	renderer->dummyFragmentUniformBuffer = (VulkanBuffer*) SDL_malloc(sizeof(VulkanBuffer));

	if (!VULKAN_INTERNAL_CreateBuffer(
		renderer,
		16,
		RESOURCE_ACCESS_FRAGMENT_SHADER_READ_UNIFORM_BUFFER,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		1,
		renderer->dummyFragmentUniformBuffer
	)) {
		Refresh_LogError("Failed to create dummy fragment uniform buffer!");
		return NULL;
	}

	renderer->dummyComputeUniformBuffer = (VulkanBuffer*) SDL_malloc(sizeof(VulkanBuffer));

	if (!VULKAN_INTERNAL_CreateBuffer(
		renderer,
		16,
		RESOURCE_ACCESS_COMPUTE_SHADER_READ_UNIFORM_BUFFER,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		1,
		renderer->dummyComputeUniformBuffer
	)) {
		Refresh_LogError("Fialed to create dummy compute uniform buffer!");
		return NULL;
	}

	/* Initialize caches */

	for (i = 0; i < NUM_COMMAND_POOL_BUCKETS; i += 1)
	{
		renderer->commandPoolHashTable.buckets[i].elements = NULL;
		renderer->commandPoolHashTable.buckets[i].count = 0;
		renderer->commandPoolHashTable.buckets[i].capacity = 0;
	}

	for (i = 0; i < NUM_PIPELINE_LAYOUT_BUCKETS; i += 1)
	{
		renderer->graphicsPipelineLayoutHashTable.buckets[i].elements = NULL;
		renderer->graphicsPipelineLayoutHashTable.buckets[i].count = 0;
		renderer->graphicsPipelineLayoutHashTable.buckets[i].capacity = 0;
	}

	for (i = 0; i < NUM_PIPELINE_LAYOUT_BUCKETS; i += 1)
	{
		renderer->computePipelineLayoutHashTable.buckets[i].elements = NULL;
		renderer->computePipelineLayoutHashTable.buckets[i].count = 0;
		renderer->computePipelineLayoutHashTable.buckets[i].capacity = 0;
	}

	for (i = 0; i < NUM_DESCRIPTOR_SET_LAYOUT_BUCKETS; i += 1)
	{
		renderer->descriptorSetLayoutHashTable.buckets[i].elements = NULL;
		renderer->descriptorSetLayoutHashTable.buckets[i].count = 0;
		renderer->descriptorSetLayoutHashTable.buckets[i].capacity = 0;
	}

	/* Deferred destroy storage */

	renderer->colorTargetsToDestroyCapacity = 16;
	renderer->colorTargetsToDestroyCount = 0;

	renderer->colorTargetsToDestroy = (VulkanColorTarget**) SDL_malloc(
		sizeof(VulkanColorTarget*) *
		renderer->colorTargetsToDestroyCapacity
	);

	renderer->submittedColorTargetsToDestroyCapacity = 16;
	renderer->submittedColorTargetsToDestroyCount = 0;

	renderer->submittedColorTargetsToDestroy = (VulkanColorTarget**) SDL_malloc(
		sizeof(VulkanColorTarget*) *
		renderer->submittedColorTargetsToDestroyCapacity
	);

	renderer->depthStencilTargetsToDestroyCapacity = 16;
	renderer->depthStencilTargetsToDestroyCount = 0;

	renderer->depthStencilTargetsToDestroy = (VulkanDepthStencilTarget**) SDL_malloc(
		sizeof(VulkanDepthStencilTarget*) *
		renderer->depthStencilTargetsToDestroyCapacity
	);

	renderer->submittedDepthStencilTargetsToDestroyCapacity = 16;
	renderer->submittedDepthStencilTargetsToDestroyCount = 0;

	renderer->submittedDepthStencilTargetsToDestroy = (VulkanDepthStencilTarget**) SDL_malloc(
		sizeof(VulkanDepthStencilTarget*) *
		renderer->submittedDepthStencilTargetsToDestroyCapacity
	);

	renderer->texturesToDestroyCapacity = 16;
	renderer->texturesToDestroyCount = 0;

	renderer->texturesToDestroy = (VulkanTexture**)SDL_malloc(
		sizeof(VulkanTexture*) *
		renderer->texturesToDestroyCapacity
	);

	renderer->submittedTexturesToDestroyCapacity = 16;
	renderer->submittedTexturesToDestroyCount = 0;

	renderer->submittedTexturesToDestroy = (VulkanTexture**)SDL_malloc(
		sizeof(VulkanTexture*) *
		renderer->submittedTexturesToDestroyCapacity
	);

	renderer->buffersToDestroyCapacity = 16;
	renderer->buffersToDestroyCount = 0;

	renderer->buffersToDestroy = (VulkanBuffer**) SDL_malloc(
		sizeof(VulkanBuffer*) *
		renderer->buffersToDestroyCapacity
	);

	renderer->submittedBuffersToDestroyCapacity = 16;
	renderer->submittedBuffersToDestroyCount = 0;

	renderer->submittedBuffersToDestroy = (VulkanBuffer**) SDL_malloc(
		sizeof(VulkanBuffer*) *
		renderer->submittedBuffersToDestroyCapacity
	);

	renderer->graphicsPipelinesToDestroyCapacity = 16;
	renderer->graphicsPipelinesToDestroyCount = 0;

	renderer->graphicsPipelinesToDestroy = (VulkanGraphicsPipeline**) SDL_malloc(
		sizeof(VulkanGraphicsPipeline*) *
		renderer->graphicsPipelinesToDestroyCapacity
	);

	renderer->submittedGraphicsPipelinesToDestroyCapacity = 16;
	renderer->submittedGraphicsPipelinesToDestroyCount = 0;

	renderer->submittedGraphicsPipelinesToDestroy = (VulkanGraphicsPipeline**) SDL_malloc(
		sizeof(VulkanGraphicsPipeline*) *
		renderer->submittedGraphicsPipelinesToDestroyCapacity
	);

	renderer->computePipelinesToDestroyCapacity = 16;
	renderer->computePipelinesToDestroyCount = 0;

	renderer->computePipelinesToDestroy = (VulkanComputePipeline**) SDL_malloc(
		sizeof(VulkanComputePipeline*) *
		renderer->computePipelinesToDestroyCapacity
	);

	renderer->submittedComputePipelinesToDestroyCapacity = 16;
	renderer->submittedComputePipelinesToDestroyCount = 0;

	renderer->submittedComputePipelinesToDestroy = (VulkanComputePipeline**) SDL_malloc(
		sizeof(VulkanComputePipeline*) *
		renderer->submittedComputePipelinesToDestroyCapacity
	);

	renderer->shaderModulesToDestroyCapacity = 16;
	renderer->shaderModulesToDestroyCount = 0;

	renderer->shaderModulesToDestroy = (VkShaderModule*) SDL_malloc(
		sizeof(VkShaderModule) *
		renderer->shaderModulesToDestroyCapacity
	);

	renderer->submittedShaderModulesToDestroyCapacity = 16;
	renderer->submittedShaderModulesToDestroyCount = 0;

	renderer->submittedShaderModulesToDestroy = (VkShaderModule*) SDL_malloc(
		sizeof(VkShaderModule) *
		renderer->submittedShaderModulesToDestroyCapacity
	);

	renderer->samplersToDestroyCapacity = 16;
	renderer->samplersToDestroyCount = 0;

	renderer->samplersToDestroy = (VkSampler*) SDL_malloc(
		sizeof(VkSampler) *
		renderer->samplersToDestroyCapacity
	);

	renderer->submittedSamplersToDestroyCapacity = 16;
	renderer->submittedSamplersToDestroyCount = 0;

	renderer->submittedSamplersToDestroy = (VkSampler*) SDL_malloc(
		sizeof(VkSampler) *
		renderer->submittedSamplersToDestroyCapacity
	);

	renderer->framebuffersToDestroyCapacity = 16;
	renderer->framebuffersToDestroyCount = 0;

	renderer->framebuffersToDestroy = (VulkanFramebuffer**) SDL_malloc(
		sizeof(VulkanFramebuffer*) *
		renderer->framebuffersToDestroyCapacity
	);

	renderer->submittedFramebuffersToDestroyCapacity = 16;
	renderer->submittedFramebuffersToDestroyCount = 0;

	renderer->submittedFramebuffersToDestroy = (VulkanFramebuffer**) SDL_malloc(
		sizeof(VulkanFramebuffer*) *
		renderer->submittedFramebuffersToDestroyCapacity
	);

	renderer->renderPassesToDestroyCapacity = 16;
	renderer->renderPassesToDestroyCount = 0;

	renderer->renderPassesToDestroy = (VkRenderPass*) SDL_malloc(
		sizeof(VkRenderPass) *
		renderer->renderPassesToDestroyCapacity
	);

	renderer->submittedRenderPassesToDestroyCapacity = 16;
	renderer->submittedRenderPassesToDestroyCount = 0;

	renderer->submittedRenderPassesToDestroy = (VkRenderPass*) SDL_malloc(
		sizeof(VkRenderPass) *
		renderer->submittedRenderPassesToDestroyCapacity
	);

    return result;
}

static Refresh_Device* VULKAN_CreateDevice(
	Refresh_PresentationParameters *presentationParameters,
	uint8_t debugMode
) {
	VulkanRenderer *renderer = (VulkanRenderer*) SDL_malloc(sizeof(VulkanRenderer));

	VULKAN_INTERNAL_LoadEntryPoints(renderer);

	/* Create the VkInstance */
	if (!VULKAN_INTERNAL_CreateInstance(renderer, presentationParameters->deviceWindowHandle))
	{
		Refresh_LogError("Error creating vulkan instance");
		return NULL;
	}

	renderer->deviceWindowHandle = presentationParameters->deviceWindowHandle;
	renderer->presentMode = presentationParameters->presentMode;
	renderer->debugMode = debugMode;
	renderer->headless = presentationParameters->deviceWindowHandle == NULL;
	renderer->usesExternalDevice = 0;

	/*
	 * Create the WSI vkSurface
	 */

	if (!SDL_Vulkan_CreateSurface(
		(SDL_Window*)renderer->deviceWindowHandle,
		renderer->instance,
		&renderer->surface
	)) {
		Refresh_LogError(
			"SDL_Vulkan_CreateSurface failed: %s",
			SDL_GetError()
		);
		return NULL;
	}

	/*
	 * Get vkInstance entry points
	 */

	#define VULKAN_INSTANCE_FUNCTION(ext, ret, func, params) \
			renderer->func = (vkfntype_##func) vkGetInstanceProcAddr(renderer->instance, #func);
	#include "Refresh_Driver_Vulkan_vkfuncs.h"

	/*
	* Choose/Create vkDevice
	*/

	if (SDL_strcmp(SDL_GetPlatform(), "Stadia") != 0)
	{
		deviceExtensionCount -= 1;
	}
	if (!VULKAN_INTERNAL_DeterminePhysicalDevice(
		renderer,
		deviceExtensionNames,
		deviceExtensionCount
	)) {
		Refresh_LogError("Failed to determine a suitable physical device");
		return NULL;
	}

	Refresh_LogInfo("Refresh Driver: Vulkan");
	Refresh_LogInfo(
		"Vulkan Device: %s",
		renderer->physicalDeviceProperties.properties.deviceName
	);
	Refresh_LogInfo(
		"Vulkan Driver: %s %s",
		renderer->physicalDeviceDriverProperties.driverName,
		renderer->physicalDeviceDriverProperties.driverInfo
	);
	Refresh_LogInfo(
		"Vulkan Conformance: %u.%u.%u",
		renderer->physicalDeviceDriverProperties.conformanceVersion.major,
		renderer->physicalDeviceDriverProperties.conformanceVersion.minor,
		renderer->physicalDeviceDriverProperties.conformanceVersion.patch
	);
	Refresh_LogWarn(
		"\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"
		"! Refresh Vulkan is still in development!    !\n"
		"! The API is unstable and subject to change! !\n"
		"! You have been warned!                      !\n"
		"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
	);

	if (!VULKAN_INTERNAL_CreateLogicalDevice(
		renderer,
		deviceExtensionNames,
		deviceExtensionCount
	)) {
		Refresh_LogError("Failed to create logical device");
		return NULL;
	}

	return VULKAN_INTERNAL_CreateDevice(renderer);
}

static Refresh_Device* VULKAN_CreateDeviceUsingExternal(
	Refresh_SysRenderer *sysRenderer,
	uint8_t debugMode
) {
	VulkanRenderer* renderer = (VulkanRenderer*)SDL_malloc(sizeof(VulkanRenderer));

	renderer->instance = sysRenderer->renderer.vulkan.instance;
	renderer->physicalDevice = sysRenderer->renderer.vulkan.physicalDevice;
	renderer->logicalDevice = sysRenderer->renderer.vulkan.logicalDevice;
	renderer->queueFamilyIndices.computeFamily = sysRenderer->renderer.vulkan.queueFamilyIndex;
	renderer->queueFamilyIndices.graphicsFamily = sysRenderer->renderer.vulkan.queueFamilyIndex;
	renderer->queueFamilyIndices.presentFamily = sysRenderer->renderer.vulkan.queueFamilyIndex;
	renderer->queueFamilyIndices.transferFamily = sysRenderer->renderer.vulkan.queueFamilyIndex;
	renderer->deviceWindowHandle = NULL;
	renderer->presentMode = 0;
	renderer->debugMode = debugMode;
	renderer->headless = 1;
	renderer->usesExternalDevice = 1;

	VULKAN_INTERNAL_LoadEntryPoints(renderer);

	/*
	 * Get vkInstance entry points
	 */

	#define VULKAN_INSTANCE_FUNCTION(ext, ret, func, params) \
			renderer->func = (vkfntype_##func) vkGetInstanceProcAddr(renderer->instance, #func);
	#include "Refresh_Driver_Vulkan_vkfuncs.h"

	 /* Load vkDevice entry points */

	#define VULKAN_DEVICE_FUNCTION(ext, ret, func, params) \
			renderer->func = (vkfntype_##func) \
				renderer->vkGetDeviceProcAddr( \
					renderer->logicalDevice, \
					#func \
				);
	#include "Refresh_Driver_Vulkan_vkfuncs.h"

	renderer->vkGetDeviceQueue(
		renderer->logicalDevice,
		renderer->queueFamilyIndices.graphicsFamily,
		0,
		&renderer->graphicsQueue
	);

	renderer->vkGetDeviceQueue(
		renderer->logicalDevice,
		renderer->queueFamilyIndices.presentFamily,
		0,
		&renderer->presentQueue
	);

	renderer->vkGetDeviceQueue(
		renderer->logicalDevice,
		renderer->queueFamilyIndices.computeFamily,
		0,
		&renderer->computeQueue
	);

	renderer->vkGetDeviceQueue(
		renderer->logicalDevice,
		renderer->queueFamilyIndices.transferFamily,
		0,
		&renderer->transferQueue
	);

	VULKAN_INTERNAL_GetPhysicalDeviceProperties(renderer);

	return VULKAN_INTERNAL_CreateDevice(renderer);
}


Refresh_Driver VulkanDriver = {
    "Vulkan",
    VULKAN_CreateDevice,
	VULKAN_CreateDeviceUsingExternal
};

#endif //REFRESH_DRIVER_VULKAN
