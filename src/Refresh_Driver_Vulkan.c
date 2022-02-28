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

#define VULKAN_INTERNAL_clamp(val, min, max) SDL_max(min, SDL_min(val, max))

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

#define STARTING_ALLOCATION_SIZE 64000000 	/* 64MB */
#define MAX_ALLOCATION_SIZE 256000000 		/* 256MB */
#define TRANSFER_BUFFER_STARTING_SIZE 8000000 	/* 8MB */
#define UBO_BUFFER_SIZE 16000 			/* 16KB */
#define DESCRIPTOR_POOL_STARTING_SIZE 128
#define DESCRIPTOR_SET_DEACTIVATE_FRAMES 10
#define WINDOW_SWAPCHAIN_DATA "Refresh_VulkanSwapchain"

#define IDENTITY_SWIZZLE 		\
{					\
	VK_COMPONENT_SWIZZLE_IDENTITY, 	\
	VK_COMPONENT_SWIZZLE_IDENTITY, 	\
	VK_COMPONENT_SWIZZLE_IDENTITY, 	\
	VK_COMPONENT_SWIZZLE_IDENTITY 	\
}

#define NULL_DESC_LAYOUT (VkDescriptorSetLayout) 0
#define NULL_PIPELINE_LAYOUT (VkPipelineLayout) 0
#define NULL_RENDER_PASS (Refresh_RenderPass*) 0

#define EXPAND_ELEMENTS_IF_NEEDED(arr, initialValue, type)	\
	if (arr->count == arr->capacity)			\
	{							\
		if (arr->capacity == 0)				\
		{						\
			arr->capacity = initialValue;		\
		}						\
		else						\
		{						\
			arr->capacity *= 2;			\
		}						\
		arr->elements = (type*) SDL_realloc(		\
			arr->elements,				\
			arr->capacity * sizeof(type)		\
		);						\
	}

#define EXPAND_ARRAY_IF_NEEDED(arr, elementType, newCount, capacity, newCapacity)	\
	if (newCount >= capacity)							\
	{										\
		capacity = newCapacity;							\
		arr = (elementType*) SDL_realloc(					\
			arr,								\
			sizeof(elementType) * capacity					\
		);									\
	}

#define MOVE_ARRAY_CONTENTS_AND_RESET(i, dstArr, dstCount, srcArr, srcCount)	\
	for (i = 0; i < srcCount; i += 1)					\
	{									\
		dstArr[i] = srcArr[i];						\
	}									\
	dstCount = srcCount;							\
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
	RESOURCE_ACCESS_COMPUTE_SHADER_READ_SAMPLED_IMAGE_OR_UNIFORM_TEXEL_BUFFER,
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

static const uint8_t DEVICE_PRIORITY[] =
{
	0,	/* VK_PHYSICAL_DEVICE_TYPE_OTHER */
	3,	/* VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU */
	4,	/* VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU */
	2,	/* VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU */
	1	/* VK_PHYSICAL_DEVICE_TYPE_CPU */
};

static VkFormat RefreshToVK_SurfaceFormat[] =
{
	VK_FORMAT_R8G8B8A8_UNORM,		/* R8G8B8A8 */
	VK_FORMAT_R5G6B5_UNORM_PACK16,		/* R5G6B5 */
	VK_FORMAT_A1R5G5B5_UNORM_PACK16,	/* A1R5G5B5 */
	VK_FORMAT_B4G4R4A4_UNORM_PACK16,	/* B4G4R4A4 */
	VK_FORMAT_BC1_RGBA_UNORM_BLOCK,		/* BC1 */
	VK_FORMAT_BC2_UNORM_BLOCK,		/* BC3 */
	VK_FORMAT_BC3_UNORM_BLOCK,		/* BC5 */
	VK_FORMAT_R8G8_SNORM,			/* R8G8_SNORM */
	VK_FORMAT_R8G8B8A8_SNORM,		/* R8G8B8A8_SNORM */
	VK_FORMAT_A2R10G10B10_UNORM_PACK32,	/* A2R10G10B10 */
	VK_FORMAT_R16G16_UNORM,			/* R16G16 */
	VK_FORMAT_R16G16B16A16_UNORM,		/* R16G16B16A16 */
	VK_FORMAT_R8_UNORM,			/* R8 */
	VK_FORMAT_R32_SFLOAT,			/* R32_SFLOAT */
	VK_FORMAT_R32G32_SFLOAT,		/* R32G32_SFLOAT */
	VK_FORMAT_R32G32B32A32_SFLOAT,		/* R32G32B32A32_SFLOAT */
	VK_FORMAT_R16_SFLOAT,			/* R16_SFLOAT */
	VK_FORMAT_R16G16_SFLOAT,		/* R16G16_SFLOAT */
	VK_FORMAT_R16G16B16A16_SFLOAT,		/* R16G16B16A16_SFLOAT */
	VK_FORMAT_D16_UNORM,			/* D16 */
	VK_FORMAT_D32_SFLOAT,			/* D32 */
	VK_FORMAT_D16_UNORM_S8_UINT,		/* D16S8 */
	VK_FORMAT_D32_SFLOAT_S8_UINT		/* D32S8 */
};

static VkFormat RefreshToVK_VertexFormat[] =
{
	VK_FORMAT_R32_SFLOAT,		/* SINGLE */
	VK_FORMAT_R32G32_SFLOAT,	/* VECTOR2 */
	VK_FORMAT_R32G32B32_SFLOAT,	/* VECTOR3 */
	VK_FORMAT_R32G32B32A32_SFLOAT,	/* VECTOR4 */
	VK_FORMAT_R8G8B8A8_UNORM,	/* COLOR */
	VK_FORMAT_R8G8B8A8_USCALED,	/* BYTE4 */
	VK_FORMAT_R16G16_SSCALED,	/* SHORT2 */
	VK_FORMAT_R16G16B16A16_SSCALED,	/* SHORT4 */
	VK_FORMAT_R16G16_SNORM,		/* NORMALIZEDSHORT2 */
	VK_FORMAT_R16G16B16A16_SNORM,	/* NORMALIZEDSHORT4 */
	VK_FORMAT_R16G16_SFLOAT,	/* HALFVECTOR2 */
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

	/* RESOURCE_ACCESS_COMPUTE_SHADER_READ_SAMPLED_IMAGE_OR_UNIFORM_TEXEL_BUFFER */
	{   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_ACCESS_SHADER_READ_BIT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
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

typedef struct VulkanBuffer /* cast from Refresh_Buffer */
{
	VkBuffer buffer;
	VkDeviceSize size;
	VkDeviceSize offset; /* move this to UsedMemoryRegion system */
	VkDeviceSize memorySize; /* move this to UsedMemoryRegion system */
	VulkanMemoryAllocation *allocation; /* see above */
	VulkanResourceAccessType resourceAccessType;
	VkBufferUsageFlags usage;
} VulkanBuffer;

typedef struct VulkanUniformBufferPool VulkanUniformBufferPool;

typedef struct VulkanUniformBuffer
{
	VulkanUniformBufferPool *pool;
	VulkanBuffer *vulkanBuffer;
	VkDeviceSize offset;
	VkDescriptorSet descriptorSet;
} VulkanUniformBuffer;

typedef enum VulkanUniformBufferType
{
	UNIFORM_BUFFER_VERTEX,
	UNIFORM_BUFFER_FRAGMENT,
	UNIFORM_BUFFER_COMPUTE
} VulkanUniformBufferType;

/* Yes, the pool is made of multiple pools.
 * For some reason it was considered a good idea to make VkDescriptorPool fixed-size.
 */
typedef struct VulkanUniformDescriptorPool
{
	VkDescriptorPool* descriptorPools;
	uint32_t descriptorPoolCount;

	/* Decremented whenever a descriptor set is allocated and
	 * incremented whenever a descriptor pool is allocated.
	 * This lets us keep track of when we need a new pool.
	 */
	uint32_t availableDescriptorSetCount;
} VulkanUniformDescriptorPool;

struct VulkanUniformBufferPool
{
	VulkanUniformBufferType type;
	VulkanUniformDescriptorPool descriptorPool;
	SDL_mutex *lock;

	VulkanUniformBuffer **availableBuffers;
	uint32_t availableBufferCount;
	uint32_t availableBufferCapacity;
};

/* Renderer Structure */

typedef struct QueueFamilyIndices
{
	uint32_t graphicsFamily;
	uint32_t presentFamily;
	uint32_t computeFamily;
	uint32_t transferFamily;
} QueueFamilyIndices;

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
	VulkanResourceAccessType resourceAccessType;
	VkImageUsageFlags usageFlags;
} VulkanTexture;

typedef struct VulkanSwapchainData
{
	/* Window surface */
	VkSurfaceKHR surface;
	VkSurfaceFormatKHR surfaceFormat;
	void *windowHandle;

	/* Swapchain for window surface */
	VkSwapchainKHR swapchain;
	VkFormat swapchainFormat;
	VkComponentMapping swapchainSwizzle;
	VkPresentModeKHR presentMode;

	/* Swapchain images */
	VkExtent2D extent;
	VulkanTexture *textures;
	uint32_t imageCount;

	/* Synchronization primitives */
	VkSemaphore imageAvailableSemaphore;
	VkSemaphore renderFinishedSemaphore;
	VkFence inFlightFence; /* borrowed from VulkanRenderer */
} VulkanSwapchainData;

typedef struct SwapChainSupportDetails
{
	VkSurfaceCapabilitiesKHR capabilities;
	VkSurfaceFormatKHR *formats;
	uint32_t formatsLength;
	VkPresentModeKHR *presentModes;
	uint32_t presentModesLength;
} SwapChainSupportDetails;

typedef struct DescriptorSetCache DescriptorSetCache;

typedef struct VulkanGraphicsPipelineLayout
{
	VkPipelineLayout pipelineLayout;
	DescriptorSetCache *vertexSamplerDescriptorSetCache;
	DescriptorSetCache *fragmentSamplerDescriptorSetCache;
} VulkanGraphicsPipelineLayout;

typedef struct VulkanGraphicsPipeline
{
	VkPipeline pipeline;
	VulkanGraphicsPipelineLayout *pipelineLayout;
	Refresh_PrimitiveType primitiveType;
	VkDeviceSize vertexUniformBlockSize;
	VkDeviceSize fragmentUniformBlockSize;
} VulkanGraphicsPipeline;

typedef struct VulkanComputePipelineLayout
{
	VkPipelineLayout pipelineLayout;
	DescriptorSetCache *bufferDescriptorSetCache;
	DescriptorSetCache *imageDescriptorSetCache;
} VulkanComputePipelineLayout;

typedef struct VulkanComputePipeline
{
	VkPipeline pipeline;
	VulkanComputePipelineLayout *pipelineLayout;
	VkDeviceSize uniformBlockSize; /* permanently set in Create function */
} VulkanComputePipeline;

typedef struct VulkanRenderTarget
{
	VulkanTexture *texture;
	uint32_t layer;
	VkImageView view;
	VulkanTexture *multisampleTexture;
	VkSampleCountFlags multisampleCount;
} VulkanRenderTarget;

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
		if (	key.descriptorType == e->descriptorType &&
			key.bindingCount == e->bindingCount &&
			key.stageFlag == e->stageFlag	)
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

typedef struct RenderPassColorTargetDescription
{
	Refresh_Vec4 clearColor;
	Refresh_LoadOp loadOp;
	Refresh_StoreOp storeOp;
} RenderPassColorTargetDescription;

typedef struct RenderPassDepthStencilTargetDescription
{
	Refresh_LoadOp loadOp;
	Refresh_StoreOp storeOp;
	Refresh_LoadOp stencilLoadOp;
	Refresh_StoreOp stencilStoreOp;
} RenderPassDepthStencilTargetDescription;

typedef struct RenderPassHash
{
	RenderPassColorTargetDescription colorTargetDescriptions[MAX_COLOR_TARGET_BINDINGS];
	uint32_t colorAttachmentCount;
	RenderPassDepthStencilTargetDescription depthStencilTargetDescription;
} RenderPassHash;

typedef struct RenderPassHashMap
{
	RenderPassHash key;
	VkRenderPass value;
} RenderPassHashMap;

typedef struct RenderPassHashArray
{
	RenderPassHashMap *elements;
	int32_t count;
	int32_t capacity;
} RenderPassHashArray;

static inline uint8_t RenderPassHash_Compare(
	RenderPassHash *a,
	RenderPassHash *b
) {
	uint32_t i;

	if (a->colorAttachmentCount != b->colorAttachmentCount)
	{
		return 0;
	}

	for (i = 0; i < a->colorAttachmentCount; i += 1)
	{
		if (	a->colorTargetDescriptions[i].clearColor.x != b->colorTargetDescriptions[i].clearColor.x ||
			a->colorTargetDescriptions[i].clearColor.y != b->colorTargetDescriptions[i].clearColor.y ||
			a->colorTargetDescriptions[i].clearColor.z != b->colorTargetDescriptions[i].clearColor.z ||
			a->colorTargetDescriptions[i].clearColor.w != b->colorTargetDescriptions[i].clearColor.w	)
		{
			return 0;
		}

		if (a->colorTargetDescriptions[i].loadOp != b->colorTargetDescriptions[i].loadOp)
		{
			return 0;
		}

		if (a->colorTargetDescriptions[i].storeOp != b->colorTargetDescriptions[i].storeOp)
		{
			return 0;
		}
	}

	if (a->depthStencilTargetDescription.loadOp != b->depthStencilTargetDescription.loadOp)
	{
		return 0;
	}

	if (a->depthStencilTargetDescription.storeOp != b->depthStencilTargetDescription.storeOp)
	{
		return 0;
	}

	if (a->depthStencilTargetDescription.stencilLoadOp != b->depthStencilTargetDescription.stencilLoadOp)
	{
		return 0;
	}

	if (a->depthStencilTargetDescription.stencilStoreOp != b->depthStencilTargetDescription.stencilStoreOp)
	{
		return 0;
	}

	return 1;
}

static inline VkRenderPass RenderPassHashArray_Fetch(
	RenderPassHashArray *arr,
	RenderPassHash *key
) {
	int32_t i;

	for (i = 0; i < arr->count; i += 1)
	{
		RenderPassHash *e = &arr->elements[i].key;

		if (RenderPassHash_Compare(e, key))
		{
			return arr->elements[i].value;
		}
	}

	return VK_NULL_HANDLE;
}

static inline void RenderPassHashArray_Insert(
	RenderPassHashArray *arr,
	RenderPassHash key,
	VkRenderPass value
) {
	RenderPassHashMap map;

	map.key = key;
	map.value = value;

	EXPAND_ELEMENTS_IF_NEEDED(arr, 4, RenderPassHashMap)

	arr->elements[arr->count] = map;
	arr->count += 1;
}

typedef struct FramebufferHash
{
	VkImageView colorAttachmentViews[MAX_COLOR_TARGET_BINDINGS];
	VkImageView colorMultiSampleAttachmentViews[MAX_COLOR_TARGET_BINDINGS];
	uint32_t colorAttachmentCount;
	VkImageView depthStencilAttachmentView;
	uint32_t width;
	uint32_t height;
} FramebufferHash;

typedef struct FramebufferHashMap
{
	FramebufferHash key;
	VkFramebuffer value;
} FramebufferHashMap;

typedef struct FramebufferHashArray
{
	FramebufferHashMap *elements;
	int32_t count;
	int32_t capacity;
} FramebufferHashArray;

static inline uint8_t FramebufferHash_Compare(
	FramebufferHash *a,
	FramebufferHash *b
) {
	uint32_t i;

	if (a->colorAttachmentCount != b->colorAttachmentCount)
	{
		return 0;
	}

	for (i = 0; i < a->colorAttachmentCount; i += 1)
	{
		if (a->colorAttachmentViews[i] != b->colorAttachmentViews[i])
		{
			return 0;
		}

		if (a->colorMultiSampleAttachmentViews[i] != b->colorMultiSampleAttachmentViews[i])
		{
			return 0;
		}
	}

	if (a->depthStencilAttachmentView != b->depthStencilAttachmentView)
	{
		return 0;
	}

	if (a->width != b->width)
	{
		return 0;
	}

	if (a->height != b->height)
	{
		return 0;
	}

	return 1;
}

static inline VkFramebuffer FramebufferHashArray_Fetch(
	FramebufferHashArray *arr,
	FramebufferHash *key
) {
	int32_t i;

	for (i = 0; i < arr->count; i += 1)
	{
		FramebufferHash *e = &arr->elements[i].key;
		if (FramebufferHash_Compare(e, key))
		{
			return arr->elements[i].value;
		}
	}

	return VK_NULL_HANDLE;
}

static inline void FramebufferHashArray_Insert(
	FramebufferHashArray *arr,
	FramebufferHash key,
	VkFramebuffer value
) {
	FramebufferHashMap map;
	map.key = key;
	map.value = value;

	EXPAND_ELEMENTS_IF_NEEDED(arr, 4, FramebufferHashMap)

	arr->elements[arr->count] = map;
	arr->count += 1;
}

static inline void FramebufferHashArray_Remove(
	FramebufferHashArray *arr,
	uint32_t index
) {
	if (index != arr->count - 1)
	{
		arr->elements[index] = arr->elements[arr->count - 1];
	}

	arr->count -= 1;
}

/* Descriptor Set Caches */

struct DescriptorSetCache
{
	SDL_mutex *lock;
	VkDescriptorSetLayout descriptorSetLayout;
	uint32_t bindingCount;
	VkDescriptorType descriptorType;

	VkDescriptorPool *descriptorPools;
	uint32_t descriptorPoolCount;
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

typedef struct DescriptorSetData
{
	DescriptorSetCache *descriptorSetCache;
	VkDescriptorSet descriptorSet;
} DescriptorSetData;

typedef struct VulkanTransferBuffer
{
	VulkanBuffer* buffer;
	VkDeviceSize offset;
} VulkanTransferBuffer;

typedef struct VulkanTransferBufferPool
{
	SDL_mutex *lock;

	VulkanTransferBuffer **availableBuffers;
	uint32_t availableBufferCount;
	uint32_t availableBufferCapacity;
} VulkanTransferBufferPool;

typedef struct VulkanCommandPool VulkanCommandPool;

typedef struct VulkanCommandBuffer
{
	VkCommandBuffer commandBuffer;
	uint8_t fixed;
	uint8_t submitted;
	uint8_t renderPassInProgress;

	uint8_t present;
	void *presentWindowHandle;
	uint32_t presentSwapchainImageIndex;
	uint8_t needNewSwapchain;

	VulkanCommandPool *commandPool;

	VulkanComputePipeline *currentComputePipeline;
	VulkanGraphicsPipeline *currentGraphicsPipeline;

	VulkanRenderTarget *renderPassColorTargets[MAX_COLOR_TARGET_BINDINGS];
	uint32_t renderPassColorTargetCount;

	VulkanUniformBuffer *vertexUniformBuffer;
	VulkanUniformBuffer *fragmentUniformBuffer;
	VulkanUniformBuffer *computeUniformBuffer;

	VkDescriptorSet vertexSamplerDescriptorSet; /* updated by BindVertexSamplers */
	VkDescriptorSet fragmentSamplerDescriptorSet; /* updated by BindFragmentSamplers */
	VkDescriptorSet bufferDescriptorSet; /* updated by BindComputeBuffers */
	VkDescriptorSet imageDescriptorSet; /* updated by BindComputeTextures */

	VulkanTransferBuffer** transferBuffers;
	uint32_t transferBufferCount;
	uint32_t transferBufferCapacity;

	VulkanUniformBuffer **boundUniformBuffers;
	uint32_t boundUniformBufferCount;
	uint32_t boundUniformBufferCapacity;

	DescriptorSetData *boundDescriptorSetDatas;
	uint32_t boundDescriptorSetDataCount;
	uint32_t boundDescriptorSetDataCapacity;

	/* Deferred destroy storage */

	VulkanRenderTarget **renderTargetsToDestroy;
	uint32_t renderTargetsToDestroyCount;
	uint32_t renderTargetsToDestroyCapacity;

	VulkanTexture **texturesToDestroy;
	uint32_t texturesToDestroyCount;
	uint32_t texturesToDestroyCapacity;

	VulkanBuffer **buffersToDestroy;
	uint32_t buffersToDestroyCount;
	uint32_t buffersToDestroyCapacity;

	VulkanGraphicsPipeline **graphicsPipelinesToDestroy;
	uint32_t graphicsPipelinesToDestroyCount;
	uint32_t graphicsPipelinesToDestroyCapacity;

	VulkanComputePipeline **computePipelinesToDestroy;
	uint32_t computePipelinesToDestroyCount;
	uint32_t computePipelinesToDestroyCapacity;

	VkShaderModule *shaderModulesToDestroy;
	uint32_t shaderModulesToDestroyCount;
	uint32_t shaderModulesToDestroyCapacity;

	VkSampler *samplersToDestroy;
	uint32_t samplersToDestroyCount;
	uint32_t samplersToDestroyCapacity;

	VkFence inFlightFence; /* borrowed from VulkanRenderer */
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

	uint8_t supportsDebugUtils;
	uint8_t debugMode;

	VulkanMemoryAllocator *memoryAllocator;
	VkPhysicalDeviceMemoryProperties memoryProperties;

	VulkanSwapchainData **swapchainDatas;
	uint32_t swapchainDataCount;
	uint32_t swapchainDataCapacity;

	QueueFamilyIndices queueFamilyIndices;
	VkQueue graphicsQueue;
	VkQueue presentQueue;
	VkQueue computeQueue;
	VkQueue transferQueue;

	Refresh_PresentMode presentMode;

	VkFence *availableFences;
	uint32_t availableFenceCount;
	uint32_t availableFenceCapacity;

	VulkanCommandBuffer **submittedCommandBuffers;
	uint32_t submittedCommandBufferCount;
	uint32_t submittedCommandBufferCapacity;

	VulkanTransferBufferPool transferBufferPool;

	CommandPoolHashTable commandPoolHashTable;
	DescriptorSetLayoutHashTable descriptorSetLayoutHashTable;
	GraphicsPipelineLayoutHashTable graphicsPipelineLayoutHashTable;
	ComputePipelineLayoutHashTable computePipelineLayoutHashTable;
	RenderPassHashArray renderPassHashArray;
	FramebufferHashArray framebufferHashArray;

	VkDescriptorPool defaultDescriptorPool;

	VkDescriptorSetLayout emptyVertexSamplerLayout;
	VkDescriptorSetLayout emptyFragmentSamplerLayout;
	VkDescriptorSetLayout emptyComputeBufferDescriptorSetLayout;
	VkDescriptorSetLayout emptyComputeImageDescriptorSetLayout;

	VkDescriptorSet emptyVertexSamplerDescriptorSet;
	VkDescriptorSet emptyFragmentSamplerDescriptorSet;
	VkDescriptorSet emptyComputeBufferDescriptorSet;
	VkDescriptorSet emptyComputeImageDescriptorSet;

	VulkanUniformBufferPool *vertexUniformBufferPool;
	VulkanUniformBufferPool *fragmentUniformBufferPool;
	VulkanUniformBufferPool *computeUniformBufferPool;

	VkDescriptorSetLayout vertexUniformDescriptorSetLayout;
	VkDescriptorSetLayout fragmentUniformDescriptorSetLayout;
	VkDescriptorSetLayout computeUniformDescriptorSetLayout;
	VulkanUniformBuffer *dummyVertexUniformBuffer;
	VulkanUniformBuffer *dummyFragmentUniformBuffer;
	VulkanUniformBuffer *dummyComputeUniformBuffer;

	VkDeviceSize minUBOAlignment;

	SDL_mutex *allocatorLock;
	SDL_mutex *submitLock;
	SDL_mutex *acquireFenceLock;
	SDL_mutex *acquireCommandBufferLock;
	SDL_mutex *renderPassFetchLock;
	SDL_mutex *framebufferFetchLock;

	#define VULKAN_INSTANCE_FUNCTION(ext, ret, func, params) \
		vkfntype_##func func;
	#define VULKAN_DEVICE_FUNCTION(ext, ret, func, params) \
		vkfntype_##func func;
	#include "Refresh_Driver_Vulkan_vkfuncs.h"
} VulkanRenderer;

/* Forward declarations */

static void VULKAN_INTERNAL_BeginCommandBuffer(VulkanRenderer *renderer, VulkanCommandBuffer *commandBuffer);
static void VULKAN_Wait(Refresh_Renderer *driverData);
static void VULKAN_Submit(Refresh_Renderer *driverData, uint32_t commandBufferCount, Refresh_CommandBuffer **pCommandBuffers);

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

static inline void LogVulkanResultAsError(
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

static inline void LogVulkanResultAsWarn(
	const char* vulkanFunctionName,
	VkResult result
) {
	if (result != VK_SUCCESS)
	{
		Refresh_LogWarn(
			"%s: %s",
			vulkanFunctionName,
			VkErrorMessages(result)
		);
	}
}

/* Utility */

static inline uint8_t IsDepthFormat(VkFormat format)
{
	switch(format)
	{
		case VK_FORMAT_D16_UNORM:
		case VK_FORMAT_D32_SFLOAT:
		case VK_FORMAT_D16_UNORM_S8_UINT:
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
			return 1;

		default:
			return 0;
	}
}

static inline uint8_t IsStencilFormat(VkFormat format)
{
	switch(format)
	{
		case VK_FORMAT_D16_UNORM_S8_UINT:
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
			return 1;

		default:
			return 0;
	}
}

static inline uint32_t VULKAN_INTERNAL_BytesPerPixel(VkFormat format)
{
	switch (format)
	{
		case VK_FORMAT_R32G32B32A32_SFLOAT:
		case VK_FORMAT_BC2_UNORM_BLOCK:
		case VK_FORMAT_BC3_UNORM_BLOCK:
			return 16;

		case VK_FORMAT_R8G8B8A8_UNORM:
		case VK_FORMAT_R8G8B8A8_SNORM:
		case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
		case VK_FORMAT_R16G16_UNORM:
		case VK_FORMAT_R16G16_SFLOAT:
		case VK_FORMAT_R32_SFLOAT:
		case VK_FORMAT_D32_SFLOAT:
			return 4;

		case VK_FORMAT_R5G6B5_UNORM_PACK16:
		case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
		case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
		case VK_FORMAT_R8G8_SNORM:
		case VK_FORMAT_R16_SFLOAT:
		case VK_FORMAT_D16_UNORM:
			return 2;

		case VK_FORMAT_R16G16B16A16_UNORM:
		case VK_FORMAT_R32G32_SFLOAT:
		case VK_FORMAT_R16G16B16A16_SFLOAT:
		case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
			return 8;

		case VK_FORMAT_R8_UNORM:
			return 1;

		case VK_FORMAT_D16_UNORM_S8_UINT:
			return 3;

		case VK_FORMAT_D32_SFLOAT_S8_UINT:
			return 5;

		default:
			Refresh_LogError("Invalid texture format!");
			return 0;
	}
}

static inline VkDeviceSize VULKAN_INTERNAL_BytesPerImage(
	uint32_t width,
	uint32_t height,
	VkFormat format
) {
	uint32_t blocksPerRow = width;
	uint32_t blocksPerColumn = height;

	if (format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK ||
		format == VK_FORMAT_BC3_UNORM_BLOCK ||
		format == VK_FORMAT_BC5_UNORM_BLOCK)
	{
		blocksPerRow = (width + 3) / 4;
	}

	return blocksPerRow * blocksPerColumn * VULKAN_INTERNAL_BytesPerPixel(format);
}

/* Memory Management */

static inline VkDeviceSize VULKAN_INTERNAL_NextHighestAlignment(
	VkDeviceSize n,
	VkDeviceSize align
) {
	return align * ((n + align - 1) / align);
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

static void VULKAN_INTERNAL_NewMemoryFreeRegion(
	VulkanMemoryAllocation *allocation,
	VkDeviceSize offset,
	VkDeviceSize size
) {
	VulkanMemoryFreeRegion *newFreeRegion;
	VkDeviceSize newOffset, newSize;
	int32_t insertionIndex = 0;
	int32_t i;

	/* look for an adjacent region to merge */
	for (i = allocation->freeRegionCount - 1; i >= 0; i -= 1)
	{
		/* check left side */
		if (allocation->freeRegions[i]->offset + allocation->freeRegions[i]->size == offset)
		{
			newOffset = allocation->freeRegions[i]->offset;
			newSize = allocation->freeRegions[i]->size + size;

			VULKAN_INTERNAL_RemoveMemoryFreeRegion(allocation->freeRegions[i]);
			VULKAN_INTERNAL_NewMemoryFreeRegion(allocation, newOffset, newSize);
			return;
		}

		/* check right side */
		if (allocation->freeRegions[i]->offset == offset + size)
		{
			newOffset = offset;
			newSize = allocation->freeRegions[i]->size + size;

			VULKAN_INTERNAL_RemoveMemoryFreeRegion(allocation->freeRegions[i]);
			VULKAN_INTERNAL_NewMemoryFreeRegion(allocation, newOffset, newSize);
			return;
		}
	}

	/* region is not contiguous with another free region, make a new one */
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
}

static uint8_t VULKAN_INTERNAL_FindMemoryType(
	VulkanRenderer *renderer,
	uint32_t typeFilter,
	VkMemoryPropertyFlags requiredProperties,
	VkMemoryPropertyFlags ignoredProperties,
	uint32_t *result
) {
	uint32_t i;

	for (i = 0; i < renderer->memoryProperties.memoryTypeCount; i += 1)
	{
		if (	(typeFilter & (1 << i)) &&
			(renderer->memoryProperties.memoryTypes[i].propertyFlags & requiredProperties) == requiredProperties &&
			(renderer->memoryProperties.memoryTypes[i].propertyFlags & ignoredProperties) == 0	)
		{
			*result = i;
			return 1;
		}
	}

	Refresh_LogError("Failed to find memory properties %X, required %X, ignored %X", requiredProperties, ignoredProperties, typeFilter);
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
		0,
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
	VkMemoryPropertyFlags requiredMemoryPropertyFlags,
	VkMemoryPropertyFlags ignoredMemoryPropertyFlags,
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
		requiredMemoryPropertyFlags,
		ignoredMemoryPropertyFlags,
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
	uint8_t isHostVisible,
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
		/* Uh oh, we couldn't allocate, time to clean up */
		SDL_free(allocation->freeRegions);

		allocator->allocationCount -= 1;
		allocator->allocations = SDL_realloc(
			allocator->allocations,
			sizeof(VulkanMemoryAllocation*) * allocator->allocationCount
		);

		SDL_free(allocation);

		LogVulkanResultAsWarn("vkAllocateMemory", result);
		return 0;
	}

	/* persistent mapping for host memory */
	if (isHostVisible)
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
			LogVulkanResultAsError("vkMapMemory", result);
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
	uint32_t memoryTypeIndex,
	VkMemoryRequirements2KHR *memoryRequirements,
	VkMemoryDedicatedRequirementsKHR *dedicatedRequirements,
	VkBuffer buffer, /* may be VK_NULL_HANDLE */
	VkImage image, /* may be VK_NULL_HANDLE */
	VulkanMemoryAllocation **pMemoryAllocation,
	VkDeviceSize *pOffset,
	VkDeviceSize *pSize
) {
	VulkanMemoryAllocation *allocation;
	VulkanMemorySubAllocator *allocator;
	VulkanMemoryFreeRegion *region;

	VkDeviceSize requiredSize, allocationSize;
	VkDeviceSize alignedOffset;
	uint32_t newRegionSize, newRegionOffset;
	uint8_t shouldAllocDedicated =
		dedicatedRequirements->prefersDedicatedAllocation ||
		dedicatedRequirements->requiresDedicatedAllocation;
	uint8_t isHostVisible, allocationResult;

	isHostVisible =
		(renderer->memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags &
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;

	allocator = &renderer->memoryAllocator->subAllocators[memoryTypeIndex];
	requiredSize = memoryRequirements->memoryRequirements.size;

	SDL_LockMutex(renderer->allocatorLock);

	/* find the largest free region and use it */
	if (allocator->sortedFreeRegionCount > 0)
	{
		region = allocator->sortedFreeRegions[0];
		allocation = region->allocation;

		alignedOffset = VULKAN_INTERNAL_NextHighestAlignment(
			region->offset,
			memoryRequirements->memoryRequirements.alignment
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

	if (shouldAllocDedicated)
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
		shouldAllocDedicated,
		isHostVisible,
		&allocation
	);

	/* Uh oh, we're out of memory */
	if (allocationResult == 0)
	{
		SDL_UnlockMutex(renderer->allocatorLock);

		/* Responsibility of the caller to handle being out of memory */
		Refresh_LogWarn("Failed to allocate memory!");
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

static uint8_t VULKAN_INTERNAL_FindAvailableBufferMemory(
	VulkanRenderer *renderer,
	VkBuffer buffer,
	VulkanMemoryAllocation **pMemoryAllocation,
	VkDeviceSize *pOffset,
	VkDeviceSize *pSize
) {
	uint32_t memoryTypeIndex;
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

	if (!VULKAN_INTERNAL_FindBufferMemoryRequirements(
		renderer,
		buffer,
		&memoryRequirements,
		&memoryTypeIndex
	)) {
		Refresh_LogError("Failed to acquire buffer memory requirements!");
		return 0;
	}

	return VULKAN_INTERNAL_FindAvailableMemory(
		renderer,
		memoryTypeIndex,
		&memoryRequirements,
		&dedicatedRequirements,
		buffer,
		VK_NULL_HANDLE,
		pMemoryAllocation,
		pOffset,
		pSize
	);
}

static uint8_t VULKAN_INTERNAL_FindAvailableTextureMemory(
	VulkanRenderer *renderer,
	VkImage image,
	uint8_t cpuAllocation,
	VulkanMemoryAllocation **pMemoryAllocation,
	VkDeviceSize *pOffset,
	VkDeviceSize *pSize
) {
	uint32_t memoryTypeIndex;
	VkMemoryPropertyFlags requiredMemoryPropertyFlags;
	VkMemoryPropertyFlags ignoredMemoryPropertyFlags;
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

	if (cpuAllocation)
	{
		requiredMemoryPropertyFlags = 0;
		ignoredMemoryPropertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	}
	else
	{
		requiredMemoryPropertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		ignoredMemoryPropertyFlags = 0;
	}

	if (!VULKAN_INTERNAL_FindImageMemoryRequirements(
		renderer,
		image,
		requiredMemoryPropertyFlags,
		ignoredMemoryPropertyFlags,
		&memoryRequirements,
		&memoryTypeIndex
	)) {
		Refresh_LogError("Failed to acquire image memory requirements!");
		return 0;
	}

	return VULKAN_INTERNAL_FindAvailableMemory(
		renderer,
		memoryTypeIndex,
		&memoryRequirements,
		&dedicatedRequirements,
		VK_NULL_HANDLE,
		image,
		pMemoryAllocation,
		pOffset,
		pSize
	);
}

/* Memory Barriers */

static void VULKAN_INTERNAL_BufferMemoryBarrier(
	VulkanRenderer *renderer,
	VkCommandBuffer commandBuffer,
	VulkanResourceAccessType nextResourceAccessType,
	VulkanBuffer *buffer
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
	memoryBarrier.buffer = buffer->buffer;
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

static void VULKAN_INTERNAL_DestroyRenderTarget(
	VulkanRenderer *renderer,
	VulkanRenderTarget *renderTarget
) {
	int32_t i, j;
	FramebufferHash *hash;

	SDL_LockMutex(renderer->framebufferFetchLock);

	/* Remove all associated framebuffers */
	for (i = renderer->framebufferHashArray.count - 1; i >= 0; i -= 1)
	{
		hash = &renderer->framebufferHashArray.elements[i].key;

		for (j = 0; j < hash->colorAttachmentCount; j += 1)
		{
			if (hash->colorAttachmentViews[i] == renderTarget->view)
			{
				renderer->vkDestroyFramebuffer(
					renderer->logicalDevice,
					renderer->framebufferHashArray.elements[i].value,
					NULL
				);

				FramebufferHashArray_Remove(
					&renderer->framebufferHashArray,
					i
				);

				break;
			}
		}
	}

	SDL_UnlockMutex(renderer->framebufferFetchLock);

	renderer->vkDestroyImageView(
		renderer->logicalDevice,
		renderTarget->view,
		NULL
	);

	/* The texture is not owned by the RenderTarget
	 * so we don't free it here
	 * But the multisampleTexture is!
	 */
	if (renderTarget->multisampleTexture != NULL)
	{
		VULKAN_INTERNAL_DestroyTexture(
			renderer,
			renderTarget->multisampleTexture
		);
	}

	SDL_free(renderTarget);
}

static void VULKAN_INTERNAL_DestroyBuffer(
	VulkanRenderer* renderer,
	VulkanBuffer* buffer
) {
	if (buffer->allocation->dedicated)
	{
		renderer->vkFreeMemory(
			renderer->logicalDevice,
			buffer->allocation->memory,
			NULL
		);

		SDL_DestroyMutex(buffer->allocation->memoryLock);
		SDL_free(buffer->allocation->freeRegions);
		SDL_free(buffer->allocation);
	}
	else
	{
		SDL_LockMutex(renderer->allocatorLock);

		VULKAN_INTERNAL_NewMemoryFreeRegion(
			buffer->allocation,
			buffer->offset,
			buffer->memorySize
		);

		SDL_UnlockMutex(renderer->allocatorLock);
	}

	renderer->vkDestroyBuffer(
		renderer->logicalDevice,
		buffer->buffer,
		NULL
	);

	SDL_free(buffer);
}

static void VULKAN_INTERNAL_DestroyCommandPool(
	VulkanRenderer *renderer,
	VulkanCommandPool *commandPool
) {
	uint32_t i;
	VulkanCommandBuffer* commandBuffer;

	renderer->vkDestroyCommandPool(
		renderer->logicalDevice,
		commandPool->commandPool,
		NULL
	);

	for (i = 0; i < commandPool->inactiveCommandBufferCount; i += 1)
	{
		commandBuffer = commandPool->inactiveCommandBuffers[i];

		SDL_free(commandBuffer->transferBuffers);
		SDL_free(commandBuffer->boundUniformBuffers);
		SDL_free(commandBuffer->boundDescriptorSetDatas);
		SDL_free(commandBuffer->renderTargetsToDestroy);
		SDL_free(commandBuffer->texturesToDestroy);
		SDL_free(commandBuffer->buffersToDestroy);
		SDL_free(commandBuffer->graphicsPipelinesToDestroy);
		SDL_free(commandBuffer->computePipelinesToDestroy);
		SDL_free(commandBuffer->shaderModulesToDestroy);
		SDL_free(commandBuffer->samplersToDestroy);

		SDL_free(commandBuffer);
	}

	SDL_free(commandPool->inactiveCommandBuffers);
	SDL_free(commandPool);
}

static void VULKAN_INTERNAL_DestroyGraphicsPipeline(
	VulkanRenderer *renderer,
	VulkanGraphicsPipeline *graphicsPipeline
) {
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

static void VULKAN_INTERNAL_DestroySwapchain(
	VulkanRenderer* renderer,
	void *windowHandle
) {
	uint32_t i;
	VulkanSwapchainData *swapchainData;

	swapchainData = (VulkanSwapchainData*) SDL_GetWindowData(windowHandle, WINDOW_SWAPCHAIN_DATA);

	if (swapchainData == NULL)
	{
		return;
	}

	for (i = 0; i < swapchainData->imageCount; i += 1)
	{
		renderer->vkDestroyImageView(
			renderer->logicalDevice,
			swapchainData->textures[i].view,
			NULL
		);
	}

	SDL_free(swapchainData->textures);

	renderer->vkDestroySwapchainKHR(
		renderer->logicalDevice,
		swapchainData->swapchain,
		NULL
	);

	renderer->vkDestroySurfaceKHR(
		renderer->instance,
		swapchainData->surface,
		NULL
	);

	renderer->vkDestroySemaphore(
		renderer->logicalDevice,
		swapchainData->imageAvailableSemaphore,
		NULL
	);

	renderer->vkDestroySemaphore(
		renderer->logicalDevice,
		swapchainData->renderFinishedSemaphore,
		NULL
	);

	for (i = 0; i < renderer->swapchainDataCount; i += 1)
	{
		if (windowHandle == renderer->swapchainDatas[i]->windowHandle)
		{
			renderer->swapchainDatas[i] = renderer->swapchainDatas[renderer->swapchainDataCount - 1];
			renderer->swapchainDataCount -= 1;
			break;
		}
	}

	SDL_SetWindowData(windowHandle, WINDOW_SWAPCHAIN_DATA, NULL);
	SDL_free(swapchainData);
}

static void VULKAN_INTERNAL_DestroyDescriptorSetCache(
	VulkanRenderer *renderer,
	DescriptorSetCache *cache
) {
	uint32_t i;

	if (cache == NULL)
	{
		return;
	}

	for (i = 0; i < cache->descriptorPoolCount; i += 1)
	{
		renderer->vkDestroyDescriptorPool(
			renderer->logicalDevice,
			cache->descriptorPools[i],
			NULL
		);
	}

	SDL_free(cache->descriptorPools);
	SDL_free(cache->inactiveDescriptorSets);
	SDL_DestroyMutex(cache->lock);
	SDL_free(cache);
}

/* Descriptor cache stuff */

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
		LogVulkanResultAsError("vkCreateDescriptorPool", vulkanResult);
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
		LogVulkanResultAsError("vkAllocateDescriptorSets", vulkanResult);
		SDL_stack_free(descriptorSetLayouts);
		return 0;
	}

	SDL_stack_free(descriptorSetLayouts);
	return 1;
}

static DescriptorSetCache* VULKAN_INTERNAL_CreateDescriptorSetCache(
	VulkanRenderer *renderer,
	VkDescriptorType descriptorType,
	VkDescriptorSetLayout descriptorSetLayout,
	uint32_t bindingCount
) {
	DescriptorSetCache *descriptorSetCache = SDL_malloc(sizeof(DescriptorSetCache));

	descriptorSetCache->lock = SDL_CreateMutex();

	descriptorSetCache->descriptorSetLayout = descriptorSetLayout;
	descriptorSetCache->bindingCount = bindingCount;
	descriptorSetCache->descriptorType = descriptorType;

	descriptorSetCache->descriptorPools = SDL_malloc(sizeof(VkDescriptorPool));
	descriptorSetCache->descriptorPoolCount = 1;
	descriptorSetCache->nextPoolSize = DESCRIPTOR_POOL_STARTING_SIZE * 2;

	VULKAN_INTERNAL_CreateDescriptorPool(
		renderer,
		descriptorType,
		DESCRIPTOR_POOL_STARTING_SIZE,
		DESCRIPTOR_POOL_STARTING_SIZE * bindingCount,
		&descriptorSetCache->descriptorPools[0]
	);

	descriptorSetCache->inactiveDescriptorSetCapacity = DESCRIPTOR_POOL_STARTING_SIZE;
	descriptorSetCache->inactiveDescriptorSetCount = DESCRIPTOR_POOL_STARTING_SIZE;
	descriptorSetCache->inactiveDescriptorSets = SDL_malloc(
		sizeof(VkDescriptorSet) * DESCRIPTOR_POOL_STARTING_SIZE
	);

	VULKAN_INTERNAL_AllocateDescriptorSets(
		renderer,
		descriptorSetCache->descriptorPools[0],
		descriptorSetCache->descriptorSetLayout,
		DESCRIPTOR_POOL_STARTING_SIZE,
		descriptorSetCache->inactiveDescriptorSets
	);

	return descriptorSetCache;
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
		LogVulkanResultAsError("vkCreateDescriptorSetLayout", vulkanResult);
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

	pipelineLayoutHash.vertexUniformLayout = renderer->vertexUniformDescriptorSetLayout;
	pipelineLayoutHash.fragmentUniformLayout = renderer->fragmentUniformDescriptorSetLayout;

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
	setLayouts[2] = renderer->vertexUniformDescriptorSetLayout;
	setLayouts[3] = renderer->fragmentUniformDescriptorSetLayout;

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
		LogVulkanResultAsError("vkCreatePipelineLayout", vulkanResult);
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
			VULKAN_INTERNAL_CreateDescriptorSetCache(
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
			VULKAN_INTERNAL_CreateDescriptorSetCache(
				renderer,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				pipelineLayoutHash.fragmentSamplerLayout,
				fragmentSamplerBindingCount
			);
	}

	return vulkanGraphicsPipelineLayout;
}

/* Data Buffer */

static VulkanBuffer* VULKAN_INTERNAL_CreateBuffer(
	VulkanRenderer *renderer,
	VkDeviceSize size,
	VulkanResourceAccessType resourceAccessType,
	VkBufferUsageFlags usage
) {
	VulkanBuffer* buffer;
	VkResult vulkanResult;
	VkBufferCreateInfo bufferCreateInfo;
	uint8_t findMemoryResult;

	buffer = SDL_malloc(sizeof(VulkanBuffer));

	buffer->size = size;
	buffer->resourceAccessType = resourceAccessType;
	buffer->usage = usage;

	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.pNext = NULL;
	bufferCreateInfo.flags = 0;
	bufferCreateInfo.size = size;
	bufferCreateInfo.usage = usage;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	bufferCreateInfo.queueFamilyIndexCount = 1;
	bufferCreateInfo.pQueueFamilyIndices = &renderer->queueFamilyIndices.graphicsFamily;

	vulkanResult = renderer->vkCreateBuffer(
		renderer->logicalDevice,
		&bufferCreateInfo,
		NULL,
		&buffer->buffer
	);

	if (vulkanResult != VK_SUCCESS)
	{
		SDL_free(buffer);
		LogVulkanResultAsError("vkCreateBuffer", vulkanResult);
		Refresh_LogError("Failed to create VkBuffer");
		return NULL;
	}

	findMemoryResult = VULKAN_INTERNAL_FindAvailableBufferMemory(
		renderer,
		buffer->buffer,
		&buffer->allocation,
		&buffer->offset,
		&buffer->memorySize
	);

	/* We're out of available memory */
	if (findMemoryResult == 2)
	{
		Refresh_LogWarn("Out of buffer memory!");
		return NULL;
	}
	else if (findMemoryResult == 0)
	{
		Refresh_LogError("Failed to find buffer memory!");
		return NULL;
	}

	SDL_LockMutex(buffer->allocation->memoryLock);

	vulkanResult = renderer->vkBindBufferMemory(
		renderer->logicalDevice,
		buffer->buffer,
		buffer->allocation->memory,
		buffer->offset
	);

	SDL_UnlockMutex(buffer->allocation->memoryLock);

	if (vulkanResult != VK_SUCCESS)
	{
		Refresh_LogError("Failed to bind buffer memory!");
		return NULL;
	}

	buffer->resourceAccessType = resourceAccessType;

	return buffer;
}

/* Uniform buffer functions */

static uint8_t VULKAN_INTERNAL_AddUniformDescriptorPool(
	VulkanRenderer *renderer,
	VulkanUniformDescriptorPool *vulkanUniformDescriptorPool
) {
	vulkanUniformDescriptorPool->descriptorPools = SDL_realloc(
		vulkanUniformDescriptorPool->descriptorPools,
		sizeof(VkDescriptorPool) * (vulkanUniformDescriptorPool->descriptorPoolCount + 1)
	);

	if (!VULKAN_INTERNAL_CreateDescriptorPool(
		renderer,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
		DESCRIPTOR_POOL_STARTING_SIZE,
		DESCRIPTOR_POOL_STARTING_SIZE,
		&vulkanUniformDescriptorPool->descriptorPools[vulkanUniformDescriptorPool->descriptorPoolCount]
	)) {
		Refresh_LogError("Failed to create descriptor pool!");
		return 0;
	}

	vulkanUniformDescriptorPool->descriptorPoolCount += 1;
	vulkanUniformDescriptorPool->availableDescriptorSetCount += DESCRIPTOR_POOL_STARTING_SIZE;

	return 1;
}

static VulkanUniformBufferPool* VULKAN_INTERNAL_CreateUniformBufferPool(
	VulkanRenderer *renderer,
	VulkanUniformBufferType uniformBufferType
) {
	VulkanUniformBufferPool* uniformBufferPool = SDL_malloc(sizeof(VulkanUniformBufferPool));

	uniformBufferPool->type = uniformBufferType;
	uniformBufferPool->lock = SDL_CreateMutex();

	uniformBufferPool->availableBufferCapacity = 16;
	uniformBufferPool->availableBufferCount = 0;
	uniformBufferPool->availableBuffers = SDL_malloc(uniformBufferPool->availableBufferCapacity * sizeof(VulkanUniformBuffer*));

	uniformBufferPool->descriptorPool.availableDescriptorSetCount = 0;
	uniformBufferPool->descriptorPool.descriptorPoolCount = 0;
	uniformBufferPool->descriptorPool.descriptorPools = NULL;

	VULKAN_INTERNAL_AddUniformDescriptorPool(renderer, &uniformBufferPool->descriptorPool);

	return uniformBufferPool;
}

static void VULKAN_INTERNAL_BindUniformBuffer(
	VulkanCommandBuffer *commandBuffer,
	VulkanUniformBuffer *uniformBuffer
) {
	if (commandBuffer->boundUniformBufferCount >= commandBuffer->boundUniformBufferCapacity)
	{
		commandBuffer->boundUniformBufferCapacity *= 2;
		commandBuffer->boundUniformBuffers = SDL_realloc(
			commandBuffer->boundUniformBuffers,
			sizeof(VulkanUniformBuffer*) * commandBuffer->boundUniformBufferCapacity
		);
	}

	commandBuffer->boundUniformBuffers[commandBuffer->boundUniformBufferCount] = uniformBuffer;
	commandBuffer->boundUniformBufferCount += 1;
}

static uint8_t VULKAN_INTERNAL_CreateUniformBuffer(
	VulkanRenderer *renderer,
	VulkanUniformBufferPool *bufferPool,
	VkDeviceSize blockSize
) {
	VulkanResourceAccessType resourceAccessType;
	VkDescriptorSetLayout descriptorSetLayout;

	if (bufferPool->type == UNIFORM_BUFFER_VERTEX)
	{
		resourceAccessType = RESOURCE_ACCESS_VERTEX_SHADER_READ_UNIFORM_BUFFER;
		descriptorSetLayout = renderer->vertexUniformDescriptorSetLayout;
	}
	else if (bufferPool->type == UNIFORM_BUFFER_FRAGMENT)
	{
		resourceAccessType = RESOURCE_ACCESS_FRAGMENT_SHADER_READ_UNIFORM_BUFFER;
		descriptorSetLayout = renderer->fragmentUniformDescriptorSetLayout;
	}
	else if (bufferPool->type == UNIFORM_BUFFER_COMPUTE)
	{
		resourceAccessType = RESOURCE_ACCESS_COMPUTE_SHADER_READ_UNIFORM_BUFFER;
		descriptorSetLayout = renderer->computeUniformDescriptorSetLayout;
	}
	else
	{
		Refresh_LogError("Unrecognized uniform buffer type!");
		return 0;
	}

	VulkanUniformBuffer *buffer = SDL_malloc(sizeof(VulkanUniformBuffer));
	buffer->pool = bufferPool;

	buffer->vulkanBuffer = VULKAN_INTERNAL_CreateBuffer(
		renderer,
		UBO_BUFFER_SIZE,
		resourceAccessType,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
	);

	if (buffer->vulkanBuffer == NULL)
	{
		Refresh_LogError("Failed to create buffer for uniform buffer!");
		return 0;
	}

	buffer->offset = 0;

	/* Allocate a descriptor set for the uniform buffer */

	if (bufferPool->descriptorPool.availableDescriptorSetCount == 0)
	{
		if (!VULKAN_INTERNAL_AddUniformDescriptorPool(
			renderer,
			&bufferPool->descriptorPool
		)) {
			Refresh_LogError("Failed to add uniform descriptor pool!");
			return 0;
		}
	}

	if (!VULKAN_INTERNAL_AllocateDescriptorSets(
		renderer,
		bufferPool->descriptorPool.descriptorPools[bufferPool->descriptorPool.descriptorPoolCount - 1],
		descriptorSetLayout,
		1,
		&buffer->descriptorSet
	)) {
		Refresh_LogError("Failed to allocate uniform descriptor set!");
		return 0;
	}

	bufferPool->descriptorPool.availableDescriptorSetCount -= 1;

	if (bufferPool->availableBufferCount >= bufferPool->availableBufferCapacity)
	{
		bufferPool->availableBufferCapacity *= 2;

		bufferPool->availableBuffers = SDL_realloc(
			bufferPool->availableBuffers,
			sizeof(VulkanUniformBuffer*) * bufferPool->availableBufferCapacity
		);
	}

	bufferPool->availableBuffers[bufferPool->availableBufferCount] = buffer;
	bufferPool->availableBufferCount += 1;

	return 1;
}

static VulkanUniformBuffer* VULKAN_INTERNAL_CreateDummyUniformBuffer(
	VulkanRenderer *renderer,
	VulkanUniformBufferType uniformBufferType
) {
	VulkanResourceAccessType resourceAccessType;
	VkDescriptorSetLayout descriptorSetLayout;
	VkWriteDescriptorSet writeDescriptorSet;
	VkDescriptorBufferInfo descriptorBufferInfo;

	if (uniformBufferType == UNIFORM_BUFFER_VERTEX)
	{
		resourceAccessType = RESOURCE_ACCESS_VERTEX_SHADER_READ_UNIFORM_BUFFER;
		descriptorSetLayout = renderer->vertexUniformDescriptorSetLayout;
	}
	else if (uniformBufferType == UNIFORM_BUFFER_FRAGMENT)
	{
		resourceAccessType = RESOURCE_ACCESS_FRAGMENT_SHADER_READ_UNIFORM_BUFFER;
		descriptorSetLayout = renderer->fragmentUniformDescriptorSetLayout;
	}
	else if (uniformBufferType == UNIFORM_BUFFER_COMPUTE)
	{
		resourceAccessType = RESOURCE_ACCESS_COMPUTE_SHADER_READ_UNIFORM_BUFFER;
		descriptorSetLayout = renderer->computeUniformDescriptorSetLayout;
	}
	else
	{
		Refresh_LogError("Unrecognized uniform buffer type!");
		return NULL;
	}

	VulkanUniformBuffer *buffer = SDL_malloc(sizeof(VulkanUniformBuffer));

	buffer->vulkanBuffer = VULKAN_INTERNAL_CreateBuffer(
		renderer,
		UBO_BUFFER_SIZE,
		resourceAccessType,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
	);
	buffer->offset = 0;

	/* Allocate a descriptor set for the uniform buffer */

	VULKAN_INTERNAL_AllocateDescriptorSets(
		renderer,
		renderer->defaultDescriptorPool,
		descriptorSetLayout,
		1,
		&buffer->descriptorSet
	);

	/* Update the descriptor set for the first and last time! */

	descriptorBufferInfo.buffer = buffer->vulkanBuffer->buffer;
	descriptorBufferInfo.offset = 0;
	descriptorBufferInfo.range = VK_WHOLE_SIZE;

	writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writeDescriptorSet.pNext = NULL;
	writeDescriptorSet.descriptorCount = 1;
	writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	writeDescriptorSet.dstArrayElement = 0;
	writeDescriptorSet.dstBinding = 0;
	writeDescriptorSet.dstSet = buffer->descriptorSet;
	writeDescriptorSet.pBufferInfo = &descriptorBufferInfo;
	writeDescriptorSet.pImageInfo = NULL;
	writeDescriptorSet.pTexelBufferView = NULL;

	renderer->vkUpdateDescriptorSets(
		renderer->logicalDevice,
		1,
		&writeDescriptorSet,
		0,
		NULL
	);

	buffer->pool = NULL; /* No pool because this is a dummy */

	return buffer;
}

static void VULKAN_INTERNAL_DestroyUniformBufferPool(
	VulkanRenderer *renderer,
	VulkanUniformBufferPool *uniformBufferPool
) {
	uint32_t i;

	for (i = 0; i < uniformBufferPool->descriptorPool.descriptorPoolCount; i += 1)
	{
		renderer->vkDestroyDescriptorPool(
			renderer->logicalDevice,
			uniformBufferPool->descriptorPool.descriptorPools[i],
			NULL
		);
	}
	SDL_free(uniformBufferPool->descriptorPool.descriptorPools);

	/* This is always destroyed after submissions, so all buffers are available */
	for (i = 0; i < uniformBufferPool->availableBufferCount; i += 1)
	{
		VULKAN_INTERNAL_DestroyBuffer(renderer, uniformBufferPool->availableBuffers[i]->vulkanBuffer);
		SDL_free(uniformBufferPool->availableBuffers[i]);
	}

	SDL_DestroyMutex(uniformBufferPool->lock);
	SDL_free(uniformBufferPool->availableBuffers);
	SDL_free(uniformBufferPool);
}

static VulkanUniformBuffer* VULKAN_INTERNAL_AcquireUniformBufferFromPool(
	VulkanRenderer *renderer,
	VulkanUniformBufferPool *bufferPool,
	VkDeviceSize blockSize
) {
	VkWriteDescriptorSet writeDescriptorSet;
	VkDescriptorBufferInfo descriptorBufferInfo;

	SDL_LockMutex(bufferPool->lock);

	if (bufferPool->availableBufferCount == 0)
	{
		if (!VULKAN_INTERNAL_CreateUniformBuffer(renderer, bufferPool, blockSize))
		{
			SDL_UnlockMutex(bufferPool->lock);
			Refresh_LogError("Failed to create uniform buffer!");
			return NULL;
		}
	}

	VulkanUniformBuffer *uniformBuffer = bufferPool->availableBuffers[bufferPool->availableBufferCount - 1];
	bufferPool->availableBufferCount -= 1;

	SDL_UnlockMutex(bufferPool->lock);

	uniformBuffer->offset = 0;

	/* Update the descriptor set with the correct range */

	descriptorBufferInfo.buffer = uniformBuffer->vulkanBuffer->buffer;
	descriptorBufferInfo.offset = 0;
	descriptorBufferInfo.range = blockSize;

	writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writeDescriptorSet.pNext = NULL;
	writeDescriptorSet.descriptorCount = 1;
	writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	writeDescriptorSet.dstArrayElement = 0;
	writeDescriptorSet.dstBinding = 0;
	writeDescriptorSet.dstSet = uniformBuffer->descriptorSet;
	writeDescriptorSet.pBufferInfo = &descriptorBufferInfo;
	writeDescriptorSet.pImageInfo = NULL;
	writeDescriptorSet.pTexelBufferView = NULL;

	renderer->vkUpdateDescriptorSets(
		renderer->logicalDevice,
		1,
		&writeDescriptorSet,
		0,
		NULL
	);

	return uniformBuffer;
}

/* Swapchain */

static uint8_t VULKAN_INTERNAL_QuerySwapChainSupport(
	VulkanRenderer *renderer,
	VkPhysicalDevice physicalDevice,
	VkSurfaceKHR surface,
	uint32_t graphicsFamilyIndex,
	SwapChainSupportDetails *outputDetails
) {
	VkResult result;
	uint32_t formatCount;
	uint32_t presentModeCount;
	VkBool32 supportsPresent;

	if (graphicsFamilyIndex != UINT32_MAX)
	{
		renderer->vkGetPhysicalDeviceSurfaceSupportKHR(
			physicalDevice,
			graphicsFamilyIndex,
			surface,
			&supportsPresent
		);

		if (!supportsPresent)
		{
			Refresh_LogWarn("This surface does not support presenting!");
			return 0;
		}
	}

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
	VulkanRenderer *renderer,
	void *windowHandle
) {
	VkResult vulkanResult;
	VulkanSwapchainData *swapchainData;
	VkSwapchainCreateInfoKHR swapchainCreateInfo;
	VkImage *swapchainImages;
	VkImageViewCreateInfo imageViewCreateInfo;
	VkSemaphoreCreateInfo semaphoreCreateInfo;
	SwapChainSupportDetails swapchainSupportDetails;
	int32_t drawableWidth, drawableHeight;
	uint32_t i;

	swapchainData = SDL_malloc(sizeof(VulkanSwapchainData));
	swapchainData->windowHandle = windowHandle;

	/* Each swapchain must have its own surface. */

	if (!SDL_Vulkan_CreateSurface(
		(SDL_Window*) windowHandle,
		renderer->instance,
		&swapchainData->surface
	)) {
		SDL_free(swapchainData);
		Refresh_LogError(
			"SDL_Vulkan_CreateSurface failed: %s",
			SDL_GetError()
		);
		return CREATE_SWAPCHAIN_FAIL;
	}

	if (!VULKAN_INTERNAL_QuerySwapChainSupport(
		renderer,
		renderer->physicalDevice,
		swapchainData->surface,
		renderer->queueFamilyIndices.graphicsFamily,
		&swapchainSupportDetails
	)) {
		renderer->vkDestroySurfaceKHR(
			renderer->instance,
			swapchainData->surface,
			NULL
		);
		if (swapchainSupportDetails.formatsLength > 0)
		{
			SDL_free(swapchainSupportDetails.formats);
		}
		if (swapchainSupportDetails.presentModesLength > 0)
		{
			SDL_free(swapchainSupportDetails.presentModes);
		}
		SDL_free(swapchainData);
		Refresh_LogError("Device does not support swap chain creation");
		return CREATE_SWAPCHAIN_FAIL;
	}

	swapchainData->swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
	swapchainData->swapchainSwizzle.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	swapchainData->swapchainSwizzle.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	swapchainData->swapchainSwizzle.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	swapchainData->swapchainSwizzle.a = VK_COMPONENT_SWIZZLE_IDENTITY;

	if (!VULKAN_INTERNAL_ChooseSwapSurfaceFormat(
		swapchainData->swapchainFormat,
		swapchainSupportDetails.formats,
		swapchainSupportDetails.formatsLength,
		&swapchainData->surfaceFormat
	)) {
		renderer->vkDestroySurfaceKHR(
			renderer->instance,
			swapchainData->surface,
			NULL
		);
		if (swapchainSupportDetails.formatsLength > 0)
		{
			SDL_free(swapchainSupportDetails.formats);
		}
		if (swapchainSupportDetails.presentModesLength > 0)
		{
			SDL_free(swapchainSupportDetails.presentModes);
		}
		SDL_free(swapchainData);
		Refresh_LogError("Device does not support swap chain format");
		return CREATE_SWAPCHAIN_FAIL;
	}

	if (!VULKAN_INTERNAL_ChooseSwapPresentMode(
		renderer->presentMode,
		swapchainSupportDetails.presentModes,
		swapchainSupportDetails.presentModesLength,
		&swapchainData->presentMode
	)) {
		renderer->vkDestroySurfaceKHR(
			renderer->instance,
			swapchainData->surface,
			NULL
		);
		if (swapchainSupportDetails.formatsLength > 0)
		{
			SDL_free(swapchainSupportDetails.formats);
		}
		if (swapchainSupportDetails.presentModesLength > 0)
		{
			SDL_free(swapchainSupportDetails.presentModes);
		}
		SDL_free(swapchainData);
		Refresh_LogError("Device does not support swap chain present mode");
		return CREATE_SWAPCHAIN_FAIL;
	}

	SDL_Vulkan_GetDrawableSize(
		(SDL_Window*) windowHandle,
		&drawableWidth,
		&drawableHeight
	);

	if (	drawableWidth < swapchainSupportDetails.capabilities.minImageExtent.width ||
		drawableWidth > swapchainSupportDetails.capabilities.maxImageExtent.width ||
		drawableHeight < swapchainSupportDetails.capabilities.minImageExtent.height ||
		drawableHeight > swapchainSupportDetails.capabilities.maxImageExtent.height	)
	{
		Refresh_LogWarn("Drawable size not possible for this VkSurface!");

		if (	swapchainSupportDetails.capabilities.currentExtent.width == 0 ||
			swapchainSupportDetails.capabilities.currentExtent.height == 0)
		{
			renderer->vkDestroySurfaceKHR(
				renderer->instance,
				swapchainData->surface,
				NULL
			);
			if (swapchainSupportDetails.formatsLength > 0)
			{
				SDL_free(swapchainSupportDetails.formats);
			}
			if (swapchainSupportDetails.presentModesLength > 0)
			{
				SDL_free(swapchainSupportDetails.presentModes);
			}
			SDL_free(swapchainData);
			return CREATE_SWAPCHAIN_SURFACE_ZERO;
		}

		if (swapchainSupportDetails.capabilities.currentExtent.width != UINT32_MAX)
		{
			Refresh_LogWarn("Falling back to an acceptable swapchain extent.");
			drawableWidth = VULKAN_INTERNAL_clamp(
				drawableWidth,
				swapchainSupportDetails.capabilities.minImageExtent.width,
				swapchainSupportDetails.capabilities.maxImageExtent.width
			);
			drawableHeight = VULKAN_INTERNAL_clamp(
				drawableHeight,
				swapchainSupportDetails.capabilities.minImageExtent.height,
				swapchainSupportDetails.capabilities.maxImageExtent.height
			);
		}
		else
		{
			renderer->vkDestroySurfaceKHR(
				renderer->instance,
				swapchainData->surface,
				NULL
			);
			if (swapchainSupportDetails.formatsLength > 0)
			{
				SDL_free(swapchainSupportDetails.formats);
			}
			if (swapchainSupportDetails.presentModesLength > 0)
			{
				SDL_free(swapchainSupportDetails.presentModes);
			}
			SDL_free(swapchainData);
			Refresh_LogError("No fallback swapchain size available!");
			return CREATE_SWAPCHAIN_FAIL;
		}
	}

	swapchainData->extent.width = drawableWidth;
	swapchainData->extent.height = drawableHeight;

	swapchainData->imageCount = swapchainSupportDetails.capabilities.minImageCount + 1;

	if (	swapchainSupportDetails.capabilities.maxImageCount > 0 &&
		swapchainData->imageCount > swapchainSupportDetails.capabilities.maxImageCount	)
	{
		swapchainData->imageCount = swapchainSupportDetails.capabilities.maxImageCount;
	}

	if (swapchainData->presentMode == VK_PRESENT_MODE_MAILBOX_KHR)
	{
		/* Required for proper triple-buffering.
		 * Note that this is below the above maxImageCount check!
		 * If the driver advertises MAILBOX but does not support 3 swap
		 * images, it's not real mailbox support, so let it fail hard.
		 * -flibit
		 */
		swapchainData->imageCount = SDL_max(swapchainData->imageCount, 3);
	}

	swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchainCreateInfo.pNext = NULL;
	swapchainCreateInfo.flags = 0;
	swapchainCreateInfo.surface = swapchainData->surface;
	swapchainCreateInfo.minImageCount = swapchainData->imageCount;
	swapchainCreateInfo.imageFormat = swapchainData->surfaceFormat.format;
	swapchainCreateInfo.imageColorSpace = swapchainData->surfaceFormat.colorSpace;
	swapchainCreateInfo.imageExtent = swapchainData->extent;
	swapchainCreateInfo.imageArrayLayers = 1;
	swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchainCreateInfo.queueFamilyIndexCount = 0;
	swapchainCreateInfo.pQueueFamilyIndices = NULL;
	swapchainCreateInfo.preTransform = swapchainSupportDetails.capabilities.currentTransform;
	swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchainCreateInfo.presentMode = swapchainData->presentMode;
	swapchainCreateInfo.clipped = VK_TRUE;
	swapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

	vulkanResult = renderer->vkCreateSwapchainKHR(
		renderer->logicalDevice,
		&swapchainCreateInfo,
		NULL,
		&swapchainData->swapchain
	);

	if (swapchainSupportDetails.formatsLength > 0)
	{
		SDL_free(swapchainSupportDetails.formats);
	}
	if (swapchainSupportDetails.presentModesLength > 0)
	{
		SDL_free(swapchainSupportDetails.presentModes);
	}

	if (vulkanResult != VK_SUCCESS)
	{
		renderer->vkDestroySurfaceKHR(
			renderer->instance,
			swapchainData->surface,
			NULL
		);
		SDL_free(swapchainData);
		LogVulkanResultAsError("vkCreateSwapchainKHR", vulkanResult);
		return CREATE_SWAPCHAIN_FAIL;
	}

	renderer->vkGetSwapchainImagesKHR(
		renderer->logicalDevice,
		swapchainData->swapchain,
		&swapchainData->imageCount,
		NULL
	);

	swapchainData->textures = SDL_malloc(
		sizeof(VulkanTexture) * swapchainData->imageCount
	);

	if (!swapchainData->textures)
	{
		SDL_OutOfMemory();
		renderer->vkDestroySurfaceKHR(
			renderer->instance,
			swapchainData->surface,
			NULL
		);
		SDL_free(swapchainData);
		return CREATE_SWAPCHAIN_FAIL;
	}

	swapchainImages = SDL_stack_alloc(VkImage, swapchainData->imageCount);

	renderer->vkGetSwapchainImagesKHR(
		renderer->logicalDevice,
		swapchainData->swapchain,
		&swapchainData->imageCount,
		swapchainImages
	);

	imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imageViewCreateInfo.pNext = NULL;
	imageViewCreateInfo.flags = 0;
	imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	imageViewCreateInfo.format = swapchainData->surfaceFormat.format;
	imageViewCreateInfo.components = swapchainData->swapchainSwizzle;
	imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
	imageViewCreateInfo.subresourceRange.levelCount = 1;
	imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
	imageViewCreateInfo.subresourceRange.layerCount = 1;

	for (i = 0; i < swapchainData->imageCount; i += 1)
	{
		swapchainData->textures[i].image = swapchainImages[i];

		imageViewCreateInfo.image = swapchainImages[i];

		vulkanResult = renderer->vkCreateImageView(
			renderer->logicalDevice,
			&imageViewCreateInfo,
			NULL,
			&swapchainData->textures[i].view
		);

		if (vulkanResult != VK_SUCCESS)
		{
			renderer->vkDestroySurfaceKHR(
				renderer->instance,
				swapchainData->surface,
				NULL
			);
			SDL_stack_free(swapchainImages);
			SDL_free(swapchainData->textures);
			SDL_free(swapchainData);
			LogVulkanResultAsError("vkCreateImageView", vulkanResult);
			return CREATE_SWAPCHAIN_FAIL;
		}

		swapchainData->textures[i].resourceAccessType = RESOURCE_ACCESS_NONE;

		/* Swapchain memory is managed by the driver */
		swapchainData->textures[i].allocation = NULL;
		swapchainData->textures[i].offset = 0;
		swapchainData->textures[i].memorySize = 0;

		swapchainData->textures[i].dimensions = swapchainData->extent;
		swapchainData->textures[i].format = swapchainData->swapchainFormat;
		swapchainData->textures[i].is3D = 0;
		swapchainData->textures[i].isCube = 0;
		swapchainData->textures[i].layerCount = 1;
		swapchainData->textures[i].levelCount = 1;
		swapchainData->textures[i].usageFlags =
			VK_IMAGE_USAGE_TRANSFER_DST_BIT |
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}

	SDL_stack_free(swapchainImages);

	semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphoreCreateInfo.pNext = NULL;
	semaphoreCreateInfo.flags = 0;

	renderer->vkCreateSemaphore(
		renderer->logicalDevice,
		&semaphoreCreateInfo,
		NULL,
		&swapchainData->imageAvailableSemaphore
	);

	renderer->vkCreateSemaphore(
		renderer->logicalDevice,
		&semaphoreCreateInfo,
		NULL,
		&swapchainData->renderFinishedSemaphore
	);

	swapchainData->inFlightFence = VK_NULL_HANDLE;

	SDL_SetWindowData(windowHandle, WINDOW_SWAPCHAIN_DATA, swapchainData);

	if (renderer->swapchainDataCount >= renderer->swapchainDataCapacity)
	{
		renderer->swapchainDataCapacity *= 2;
		renderer->swapchainDatas = SDL_realloc(
			renderer->swapchainDatas,
			renderer->swapchainDataCapacity * sizeof(VulkanSwapchainData*)
		);
	}

	renderer->swapchainDatas[renderer->swapchainDataCount] = swapchainData;
	renderer->swapchainDataCount += 1;

	return CREATE_SWAPCHAIN_SUCCESS;
}

static void VULKAN_INTERNAL_RecreateSwapchain(
	VulkanRenderer* renderer,
	void *windowHandle
) {
	CreateSwapchainResult createSwapchainResult;

	VULKAN_Wait((Refresh_Renderer*)renderer);

	VULKAN_INTERNAL_DestroySwapchain(renderer, windowHandle);
	createSwapchainResult = VULKAN_INTERNAL_CreateSwapchain(renderer, windowHandle);

	if (createSwapchainResult == CREATE_SWAPCHAIN_FAIL)
	{
		return;
	}

	VULKAN_Wait((Refresh_Renderer*)renderer);
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
		LogVulkanResultAsError("vkBeginCommandBuffer", result);
	}
}

static void VULKAN_INTERNAL_EndCommandBuffer(
	VulkanRenderer* renderer,
	VulkanCommandBuffer *commandBuffer
) {
	VkResult result;

	/* Compute pipelines are not explicitly unbound so we have to clean up here */
	if (	commandBuffer->computeUniformBuffer != renderer->dummyComputeUniformBuffer &&
		commandBuffer->computeUniformBuffer != NULL
	) {
		VULKAN_INTERNAL_BindUniformBuffer(
			commandBuffer,
			commandBuffer->computeUniformBuffer
		);
	}
	commandBuffer->computeUniformBuffer = NULL;
	commandBuffer->currentComputePipeline = NULL;

	result = renderer->vkEndCommandBuffer(
		commandBuffer->commandBuffer
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResultAsError("vkEndCommandBuffer", result);
	}
}

static void VULKAN_DestroyDevice(
	Refresh_Device *device
) {
	VulkanRenderer* renderer = (VulkanRenderer*) device->driverData;
	CommandPoolHashArray commandPoolHashArray;
	GraphicsPipelineLayoutHashArray graphicsPipelineLayoutHashArray;
	ComputePipelineLayoutHashArray computePipelineLayoutHashArray;
	VulkanMemorySubAllocator *allocator;
	int32_t i, j, k;

	VULKAN_Wait(device->driverData);

	SDL_free(renderer->submittedCommandBuffers);

	VULKAN_INTERNAL_DestroyBuffer(renderer, renderer->dummyVertexUniformBuffer->vulkanBuffer);
	VULKAN_INTERNAL_DestroyBuffer(renderer, renderer->dummyFragmentUniformBuffer->vulkanBuffer);
	VULKAN_INTERNAL_DestroyBuffer(renderer, renderer->dummyComputeUniformBuffer->vulkanBuffer);

	SDL_free(renderer->dummyVertexUniformBuffer);
	SDL_free(renderer->dummyFragmentUniformBuffer);
	SDL_free(renderer->dummyComputeUniformBuffer);

	for (i = 0; i < renderer->transferBufferPool.availableBufferCount; i += 1)
	{
		VULKAN_INTERNAL_DestroyBuffer(renderer, renderer->transferBufferPool.availableBuffers[i]->buffer);
		SDL_free(renderer->transferBufferPool.availableBuffers[i]);
	}

	SDL_free(renderer->transferBufferPool.availableBuffers);
	SDL_DestroyMutex(renderer->transferBufferPool.lock);

	for (i = 0; i < renderer->availableFenceCount; i += 1)
	{
		renderer->vkDestroyFence(
			renderer->logicalDevice,
			renderer->availableFences[i],
			NULL
		);
	}

	SDL_free(renderer->availableFences);

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

	for (i = 0; i < NUM_PIPELINE_LAYOUT_BUCKETS; i += 1)
	{
		graphicsPipelineLayoutHashArray = renderer->graphicsPipelineLayoutHashTable.buckets[i];
		for (j = 0; j < graphicsPipelineLayoutHashArray.count; j += 1)
		{
			VULKAN_INTERNAL_DestroyDescriptorSetCache(
				renderer,
				graphicsPipelineLayoutHashArray.elements[j].value->vertexSamplerDescriptorSetCache
			);

			VULKAN_INTERNAL_DestroyDescriptorSetCache(
				renderer,
				graphicsPipelineLayoutHashArray.elements[j].value->fragmentSamplerDescriptorSetCache
			);

			renderer->vkDestroyPipelineLayout(
				renderer->logicalDevice,
				graphicsPipelineLayoutHashArray.elements[j].value->pipelineLayout,
				NULL
			);

			SDL_free(graphicsPipelineLayoutHashArray.elements[j].value);
		}

		if (graphicsPipelineLayoutHashArray.elements != NULL)
		{
			SDL_free(graphicsPipelineLayoutHashArray.elements);
		}

		computePipelineLayoutHashArray = renderer->computePipelineLayoutHashTable.buckets[i];
		for (j = 0; j < computePipelineLayoutHashArray.count; j += 1)
		{
			VULKAN_INTERNAL_DestroyDescriptorSetCache(
				renderer,
				computePipelineLayoutHashArray.elements[j].value->bufferDescriptorSetCache
			);

			VULKAN_INTERNAL_DestroyDescriptorSetCache(
				renderer,
				computePipelineLayoutHashArray.elements[j].value->imageDescriptorSetCache
			);

			renderer->vkDestroyPipelineLayout(
				renderer->logicalDevice,
				computePipelineLayoutHashArray.elements[j].value->pipelineLayout,
				NULL
			);

			SDL_free(computePipelineLayoutHashArray.elements[j].value);
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
		renderer->vertexUniformDescriptorSetLayout,
		NULL
	);

	renderer->vkDestroyDescriptorSetLayout(
		renderer->logicalDevice,
		renderer->fragmentUniformDescriptorSetLayout,
		NULL
	);

	renderer->vkDestroyDescriptorSetLayout(
		renderer->logicalDevice,
		renderer->computeUniformDescriptorSetLayout,
		NULL
	);

	for (i = 0; i < renderer->framebufferHashArray.count; i += 1)
	{
		renderer->vkDestroyFramebuffer(
			renderer->logicalDevice,
			renderer->framebufferHashArray.elements[i].value,
			NULL
		);
	}

	SDL_free(renderer->framebufferHashArray.elements);

	for (i = 0; i < renderer->renderPassHashArray.count; i += 1)
	{
		renderer->vkDestroyRenderPass(
			renderer->logicalDevice,
			renderer->renderPassHashArray.elements[i].value,
			NULL
		);
	}

	SDL_free(renderer->renderPassHashArray.elements);

	VULKAN_INTERNAL_DestroyUniformBufferPool(renderer, renderer->vertexUniformBufferPool);
	VULKAN_INTERNAL_DestroyUniformBufferPool(renderer, renderer->fragmentUniformBufferPool);
	VULKAN_INTERNAL_DestroyUniformBufferPool(renderer, renderer->computeUniformBufferPool);

	for (i = renderer->swapchainDataCount - 1; i >= 0; i -= 1)
	{
		VULKAN_INTERNAL_DestroySwapchain(renderer, renderer->swapchainDatas[i]->windowHandle);
	}

	SDL_free(renderer->swapchainDatas);

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
	SDL_DestroyMutex(renderer->submitLock);
	SDL_DestroyMutex(renderer->acquireFenceLock);
	SDL_DestroyMutex(renderer->acquireCommandBufferLock);
	SDL_DestroyMutex(renderer->renderPassFetchLock);
	SDL_DestroyMutex(renderer->framebufferFetchLock);

	renderer->vkDestroyDevice(renderer->logicalDevice, NULL);
	renderer->vkDestroyInstance(renderer->instance, NULL);

	SDL_free(renderer);
	SDL_free(device);
}

static void VULKAN_Clear(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Rect *clearRect,
	Refresh_ClearOptions options,
	Refresh_Vec4 *colors,
	uint32_t colorCount,
	Refresh_DepthStencilValue depthStencil
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
		(shouldClearDepth || shouldClearStencil)
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
			clearValues[i].color.float32[0] = colors[i].x;
			clearValues[i].color.float32[1] = colors[i].y;
			clearValues[i].color.float32[2] = colors[i].z;
			clearValues[i].color.float32[3] = colors[i].w;
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
			if (depthStencil.depth < 0.0f)
			{
				depthStencil.depth = 0.0f;
			}
			else if (depthStencil.depth > 1.0f)
			{
				depthStencil.depth = 1.0f;
			}
			clearAttachments[attachmentCount].aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
			clearAttachments[attachmentCount].clearValue.depthStencil.depth = depthStencil.depth;
		}
		else
		{
			clearAttachments[attachmentCount].clearValue.depthStencil.depth = 0.0f;
		}

		if (shouldClearStencil)
		{
			clearAttachments[attachmentCount].aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			clearAttachments[attachmentCount].clearValue.depthStencil.stencil = depthStencil.stencil;
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

	descriptorSets[0] = vulkanCommandBuffer->vertexSamplerDescriptorSet;
	descriptorSets[1] = vulkanCommandBuffer->fragmentSamplerDescriptorSet;
	descriptorSets[2] = vulkanCommandBuffer->vertexUniformBuffer->descriptorSet;
	descriptorSets[3] = vulkanCommandBuffer->fragmentUniformBuffer->descriptorSet;

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

	descriptorSets[0] = vulkanCommandBuffer->vertexSamplerDescriptorSet;
	descriptorSets[1] = vulkanCommandBuffer->fragmentSamplerDescriptorSet;
	descriptorSets[2] = vulkanCommandBuffer->vertexUniformBuffer->descriptorSet;
	descriptorSets[3] = vulkanCommandBuffer->fragmentUniformBuffer->descriptorSet;

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

	VkDescriptorSet descriptorSets[3];

	descriptorSets[0] = vulkanCommandBuffer->bufferDescriptorSet;
	descriptorSets[1] = vulkanCommandBuffer->imageDescriptorSet;
	descriptorSets[2] = vulkanCommandBuffer->computeUniformBuffer->descriptorSet;

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
}

static VkRenderPass VULKAN_INTERNAL_CreateRenderPass(
	VulkanRenderer *renderer,
	Refresh_ColorAttachmentInfo *colorAttachmentInfos,
	uint32_t colorAttachmentCount,
	Refresh_DepthStencilAttachmentInfo *depthStencilAttachmentInfo
) {
	VkResult vulkanResult;
	VkAttachmentDescription attachmentDescriptions[2 * MAX_COLOR_TARGET_BINDINGS + 1];
	VkAttachmentReference colorAttachmentReferences[MAX_COLOR_TARGET_BINDINGS];
	VkAttachmentReference resolveReferences[MAX_COLOR_TARGET_BINDINGS + 1];
	VkAttachmentReference depthStencilAttachmentReference;
	VkRenderPassCreateInfo renderPassCreateInfo;
	VkSubpassDescription subpass;
	VkRenderPass renderPass;
	uint32_t i;
	uint8_t multisampling = 0;

	uint32_t attachmentDescriptionCount = 0;
	uint32_t colorAttachmentReferenceCount = 0;
	uint32_t resolveReferenceCount = 0;

	VulkanRenderTarget *colorTarget;
	VulkanRenderTarget *depthStencilTarget;

	for (i = 0; i < colorAttachmentCount; i += 1)
	{
		colorTarget = (VulkanRenderTarget*) colorAttachmentInfos[attachmentDescriptionCount].pRenderTarget;

		if (colorTarget->multisampleCount > VK_SAMPLE_COUNT_1_BIT)
		{
			multisampling = 1;

			/* Resolve attachment and multisample attachment */

			attachmentDescriptions[attachmentDescriptionCount].flags = 0;
			attachmentDescriptions[attachmentDescriptionCount].format = colorTarget->texture->format;
			attachmentDescriptions[attachmentDescriptionCount].samples =
				VK_SAMPLE_COUNT_1_BIT;
			attachmentDescriptions[attachmentDescriptionCount].loadOp = RefreshToVK_LoadOp[
				colorAttachmentInfos[i].loadOp
			];
			attachmentDescriptions[attachmentDescriptionCount].storeOp = RefreshToVK_StoreOp[
				colorAttachmentInfos[i].storeOp
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
			attachmentDescriptions[attachmentDescriptionCount].format = colorTarget->texture->format;
			attachmentDescriptions[attachmentDescriptionCount].samples = colorTarget->multisampleCount;
			attachmentDescriptions[attachmentDescriptionCount].loadOp = RefreshToVK_LoadOp[
				colorAttachmentInfos[i].loadOp
			];
			attachmentDescriptions[attachmentDescriptionCount].storeOp = RefreshToVK_StoreOp[
				colorAttachmentInfos[i].storeOp
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
			attachmentDescriptions[attachmentDescriptionCount].format = colorTarget->texture->format;
			attachmentDescriptions[attachmentDescriptionCount].samples =
				VK_SAMPLE_COUNT_1_BIT;
			attachmentDescriptions[attachmentDescriptionCount].loadOp = RefreshToVK_LoadOp[
				colorAttachmentInfos[i].loadOp
			];
			attachmentDescriptions[attachmentDescriptionCount].storeOp = RefreshToVK_StoreOp[
				colorAttachmentInfos[i].storeOp
			];
			attachmentDescriptions[attachmentDescriptionCount].stencilLoadOp =
				VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescriptions[attachmentDescriptionCount].stencilStoreOp =
				VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachmentDescriptions[attachmentDescriptionCount].initialLayout =
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachmentDescriptions[attachmentDescriptionCount].finalLayout =
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;


			colorAttachmentReferences[colorAttachmentReferenceCount].attachment = attachmentDescriptionCount;
			colorAttachmentReferences[colorAttachmentReferenceCount].layout =
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			attachmentDescriptionCount += 1;
			colorAttachmentReferenceCount += 1;
		}
	}

	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.flags = 0;
	subpass.inputAttachmentCount = 0;
	subpass.pInputAttachments = NULL;
	subpass.colorAttachmentCount = colorAttachmentCount;
	subpass.pColorAttachments = colorAttachmentReferences;
	subpass.preserveAttachmentCount = 0;
	subpass.pPreserveAttachments = NULL;

	if (depthStencilAttachmentInfo == NULL)
	{
		subpass.pDepthStencilAttachment = NULL;
	}
	else
	{
		depthStencilTarget = (VulkanRenderTarget*) depthStencilAttachmentInfo->pDepthStencilTarget;
		attachmentDescriptions[attachmentDescriptionCount].flags = 0;
		attachmentDescriptions[attachmentDescriptionCount].format = depthStencilTarget->texture->format;
		attachmentDescriptions[attachmentDescriptionCount].samples =
			VK_SAMPLE_COUNT_1_BIT; /* FIXME: do these take multisamples? */
		attachmentDescriptions[attachmentDescriptionCount].loadOp = RefreshToVK_LoadOp[
			depthStencilAttachmentInfo->loadOp
		];
		attachmentDescriptions[attachmentDescriptionCount].storeOp = RefreshToVK_StoreOp[
			depthStencilAttachmentInfo->storeOp
		];
		attachmentDescriptions[attachmentDescriptionCount].stencilLoadOp = RefreshToVK_LoadOp[
			depthStencilAttachmentInfo->stencilLoadOp
		];
		attachmentDescriptions[attachmentDescriptionCount].stencilStoreOp = RefreshToVK_StoreOp[
			depthStencilAttachmentInfo->stencilStoreOp
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

	renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassCreateInfo.pNext = NULL;
	renderPassCreateInfo.flags = 0;
	renderPassCreateInfo.pAttachments = attachmentDescriptions;
	renderPassCreateInfo.attachmentCount = attachmentDescriptionCount;
	renderPassCreateInfo.subpassCount = 1;
	renderPassCreateInfo.pSubpasses = &subpass;
	renderPassCreateInfo.dependencyCount = 0;
	renderPassCreateInfo.pDependencies = NULL;

	vulkanResult = renderer->vkCreateRenderPass(
		renderer->logicalDevice,
		&renderPassCreateInfo,
		NULL,
		&renderPass
	);

	if (vulkanResult != VK_SUCCESS)
	{
		renderPass = VK_NULL_HANDLE;
		LogVulkanResultAsError("vkCreateRenderPass", vulkanResult);
	}

	return renderPass;
}

static VkRenderPass VULKAN_INTERNAL_CreateTransientRenderPass(
	VulkanRenderer *renderer,
	Refresh_GraphicsPipelineAttachmentInfo attachmentInfo
) {
	VkAttachmentDescription attachmentDescriptions[2 * MAX_COLOR_TARGET_BINDINGS + 1];
	VkAttachmentReference colorAttachmentReferences[MAX_COLOR_TARGET_BINDINGS];
	VkAttachmentReference resolveReferences[MAX_COLOR_TARGET_BINDINGS + 1];
	VkAttachmentReference depthStencilAttachmentReference;
	Refresh_ColorAttachmentDescription attachmentDescription;
	VkSubpassDescription subpass;
	VkRenderPassCreateInfo renderPassCreateInfo;
	VkRenderPass renderPass;
	VkResult result;

	uint32_t multisampling = 0;
	uint32_t attachmentDescriptionCount = 0;
	uint32_t colorAttachmentReferenceCount = 0;
	uint32_t resolveReferenceCount = 0;
	uint32_t i;

	for (i = 0; i < attachmentInfo.colorAttachmentCount; i += 1)
	{
		attachmentDescription = attachmentInfo.colorAttachmentDescriptions[i];

		if (attachmentDescription.sampleCount > REFRESH_SAMPLECOUNT_1)
		{
			multisampling = 1;

			/* Resolve attachment and multisample attachment */

			attachmentDescriptions[attachmentDescriptionCount].flags = 0;
			attachmentDescriptions[attachmentDescriptionCount].format = RefreshToVK_SurfaceFormat[
				attachmentDescription.format
			];
			attachmentDescriptions[attachmentDescriptionCount].samples = VK_SAMPLE_COUNT_1_BIT;
			attachmentDescriptions[attachmentDescriptionCount].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescriptions[attachmentDescriptionCount].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachmentDescriptions[attachmentDescriptionCount].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescriptions[attachmentDescriptionCount].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachmentDescriptions[attachmentDescriptionCount].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachmentDescriptions[attachmentDescriptionCount].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			resolveReferences[resolveReferenceCount].attachment = attachmentDescriptionCount;
			resolveReferences[resolveReferenceCount].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			attachmentDescriptionCount += 1;
			resolveReferenceCount += 1;

			attachmentDescriptions[attachmentDescriptionCount].flags = 0;
			attachmentDescriptions[attachmentDescriptionCount].format = RefreshToVK_SurfaceFormat[
				attachmentDescription.format
			];
			attachmentDescriptions[attachmentDescriptionCount].samples = VK_SAMPLE_COUNT_1_BIT;
			attachmentDescriptions[attachmentDescriptionCount].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescriptions[attachmentDescriptionCount].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachmentDescriptions[attachmentDescriptionCount].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescriptions[attachmentDescriptionCount].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachmentDescriptions[attachmentDescriptionCount].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachmentDescriptions[attachmentDescriptionCount].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

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
				attachmentDescription.format
			];
			attachmentDescriptions[attachmentDescriptionCount].samples =
				VK_SAMPLE_COUNT_1_BIT;
			attachmentDescriptions[attachmentDescriptionCount].loadOp =
				VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescriptions[attachmentDescriptionCount].storeOp =
				VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachmentDescriptions[attachmentDescriptionCount].stencilLoadOp =
				VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescriptions[attachmentDescriptionCount].stencilStoreOp =
				VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachmentDescriptions[attachmentDescriptionCount].initialLayout =
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachmentDescriptions[attachmentDescriptionCount].finalLayout =
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;


			colorAttachmentReferences[colorAttachmentReferenceCount].attachment = attachmentDescriptionCount;
			colorAttachmentReferences[colorAttachmentReferenceCount].layout =
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			attachmentDescriptionCount += 1;
			colorAttachmentReferenceCount += 1;
		}
	}

	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.flags = 0;
	subpass.inputAttachmentCount = 0;
	subpass.pInputAttachments = NULL;
	subpass.colorAttachmentCount = attachmentInfo.colorAttachmentCount;
	subpass.pColorAttachments = colorAttachmentReferences;
	subpass.preserveAttachmentCount = 0;
	subpass.pPreserveAttachments = NULL;

	if (attachmentInfo.hasDepthStencilAttachment)
	{
		attachmentDescriptions[attachmentDescriptionCount].flags = 0;
		attachmentDescriptions[attachmentDescriptionCount].format = RefreshToVK_SurfaceFormat[
			attachmentInfo.depthStencilFormat
		];
		attachmentDescriptions[attachmentDescriptionCount].samples =
			VK_SAMPLE_COUNT_1_BIT; /* FIXME: do these take multisamples? */
		attachmentDescriptions[attachmentDescriptionCount].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescriptions[attachmentDescriptionCount].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescriptions[attachmentDescriptionCount].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescriptions[attachmentDescriptionCount].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
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
	else
	{
		subpass.pDepthStencilAttachment = NULL;
	}

	if (multisampling)
	{
		subpass.pResolveAttachments = resolveReferences;
	}
	else
	{
		subpass.pResolveAttachments = NULL;
	}

	renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassCreateInfo.pNext = NULL;
	renderPassCreateInfo.flags = 0;
	renderPassCreateInfo.pAttachments = attachmentDescriptions;
	renderPassCreateInfo.attachmentCount = attachmentDescriptionCount;
	renderPassCreateInfo.subpassCount = 1;
	renderPassCreateInfo.pSubpasses = &subpass;
	renderPassCreateInfo.dependencyCount = 0;
	renderPassCreateInfo.pDependencies = NULL;

	result = renderer->vkCreateRenderPass(
		renderer->logicalDevice,
		&renderPassCreateInfo,
		NULL,
		&renderPass
	);

	if (result != VK_SUCCESS)
	{
		renderPass = VK_NULL_HANDLE;
		LogVulkanResultAsError("vkCreateRenderPass", result);
	}

	return renderPass;
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
		pipelineCreateInfo->attachmentInfo.colorAttachmentCount
	);

	VulkanRenderer *renderer = (VulkanRenderer*) driverData;

	/* Create a "compatible" render pass */

	VkRenderPass transientRenderPass = VULKAN_INTERNAL_CreateTransientRenderPass(
		renderer,
		pipelineCreateInfo->attachmentInfo
	);

	/* Shader stages */

	shaderStageCreateInfos[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageCreateInfos[0].pNext = NULL;
	shaderStageCreateInfos[0].flags = 0;
	shaderStageCreateInfos[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStageCreateInfos[0].module = (VkShaderModule) pipelineCreateInfo->vertexShaderState.shaderModule;
	shaderStageCreateInfos[0].pName = pipelineCreateInfo->vertexShaderState.entryPointName;
	shaderStageCreateInfos[0].pSpecializationInfo = NULL;

	graphicsPipeline->vertexUniformBlockSize =
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

	graphicsPipeline->fragmentUniformBlockSize =
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

	for (i = 0; i < pipelineCreateInfo->attachmentInfo.colorAttachmentCount; i += 1)
	{
		Refresh_ColorAttachmentBlendState blendState = pipelineCreateInfo->attachmentInfo.colorAttachmentDescriptions[i].blendState;

		colorBlendAttachmentStates[i].blendEnable =
			blendState.blendEnable;
		colorBlendAttachmentStates[i].srcColorBlendFactor = RefreshToVK_BlendFactor[
			blendState.srcColorBlendFactor
		];
		colorBlendAttachmentStates[i].dstColorBlendFactor = RefreshToVK_BlendFactor[
			blendState.dstColorBlendFactor
		];
		colorBlendAttachmentStates[i].colorBlendOp = RefreshToVK_BlendOp[
			blendState.colorBlendOp
		];
		colorBlendAttachmentStates[i].srcAlphaBlendFactor = RefreshToVK_BlendFactor[
			blendState.srcAlphaBlendFactor
		];
		colorBlendAttachmentStates[i].dstAlphaBlendFactor = RefreshToVK_BlendFactor[
			blendState.dstAlphaBlendFactor
		];
		colorBlendAttachmentStates[i].alphaBlendOp = RefreshToVK_BlendOp[
			blendState.alphaBlendOp
		];
		colorBlendAttachmentStates[i].colorWriteMask =
			blendState.colorWriteMask;
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
		pipelineCreateInfo->attachmentInfo.colorAttachmentCount;
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
	vkPipelineCreateInfo.renderPass = transientRenderPass;
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

	SDL_stack_free(vertexInputBindingDescriptions);
	SDL_stack_free(vertexInputAttributeDescriptions);
	SDL_stack_free(viewports);
	SDL_stack_free(scissors);
	SDL_stack_free(colorBlendAttachmentStates);

	renderer->vkDestroyRenderPass(
		renderer->logicalDevice,
		transientRenderPass,
		NULL
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResultAsError("vkCreateGraphicsPipelines", vulkanResult);
		Refresh_LogError("Failed to create graphics pipeline!");
		return NULL;
	}

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

	pipelineLayoutHash.uniformLayout = renderer->computeUniformDescriptorSetLayout;

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
		LogVulkanResultAsError("vkCreatePipelineLayout", vulkanResult);
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
			VULKAN_INTERNAL_CreateDescriptorSetCache(
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
			VULKAN_INTERNAL_CreateDescriptorSetCache(
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

	vulkanComputePipeline->uniformBlockSize =
		VULKAN_INTERNAL_NextHighestAlignment(
			pipelineCreateInfo->computeShaderState.uniformBufferSize,
			renderer->minUBOAlignment
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
		LogVulkanResultAsError("vkCreateSampler", vulkanResult);
		return NULL;
	}

	return (Refresh_Sampler*) sampler;
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
		LogVulkanResultAsError("vkCreateShaderModule", vulkanResult);
		Refresh_LogError("Failed to create shader module!");
		return NULL;
	}

	return (Refresh_ShaderModule*) shaderModule;
}

static VulkanTexture* VULKAN_INTERNAL_CreateTexture(
	VulkanRenderer *renderer,
	uint32_t width,
	uint32_t height,
	uint32_t depth,
	uint32_t isCube,
	VkSampleCountFlagBits samples,
	uint32_t levelCount,
	VkFormat format,
	VkImageAspectFlags aspectMask,
	VkImageType imageType,
	VkImageUsageFlags imageUsageFlags
) {
	VkResult vulkanResult;
	VkImageCreateInfo imageCreateInfo;
	VkImageCreateFlags imageCreateFlags = 0;
	VkImageViewCreateInfo imageViewCreateInfo;
	uint8_t findMemoryResult;
	uint8_t is3D = depth > 1 ? 1 : 0;
	uint8_t layerCount = isCube ? 6 : 1;
	uint8_t isRenderTarget =
		((imageUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0) ||
		((imageUsageFlags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0);
	VkComponentMapping swizzle = IDENTITY_SWIZZLE;

	VulkanTexture *texture = SDL_malloc(sizeof(VulkanTexture));

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
	imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCreateInfo.usage = imageUsageFlags;
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
		LogVulkanResultAsError("vkCreateImage", vulkanResult);
		Refresh_LogError("Failed to create texture!");
	}

	/* Prefer GPU allocation */
	findMemoryResult = VULKAN_INTERNAL_FindAvailableTextureMemory(
		renderer,
		texture->image,
		0,
		&texture->allocation,
		&texture->offset,
		&texture->memorySize
	);

	/* No device local memory available */
	if (findMemoryResult == 2)
	{
		if (isRenderTarget)
		{
			Refresh_LogWarn("RenderTarget is allocated in host memory, pre-allocate your targets!");
		}

		Refresh_LogWarn("Out of device local memory, falling back to host memory");

		/* Attempt CPU allocation */
		findMemoryResult = VULKAN_INTERNAL_FindAvailableTextureMemory(
			renderer,
			texture->image,
			1,
			&texture->allocation,
			&texture->offset,
			&texture->memorySize
		);

		/* Memory alloc completely failed, time to die */
		if (findMemoryResult == 0)
		{
			Refresh_LogError("Something went very wrong allocating memory!");
			return 0;
		}
		else if (findMemoryResult == 2)
		{
			Refresh_LogError("Out of memory!");
			return 0;
		}
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
		LogVulkanResultAsError("vkBindImageMemory", vulkanResult);
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
		LogVulkanResultAsError("vkCreateImageView", vulkanResult);
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
	texture->usageFlags = imageUsageFlags;

	return texture;
}

static Refresh_Texture* VULKAN_CreateTexture(
	Refresh_Renderer *driverData,
	Refresh_TextureCreateInfo *textureCreateInfo
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VkImageUsageFlags imageUsageFlags = (
		VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT
	);
	VkImageAspectFlags imageAspectFlags;
	VkFormat format = RefreshToVK_SurfaceFormat[textureCreateInfo->format];

	if (textureCreateInfo->usageFlags & REFRESH_TEXTUREUSAGE_SAMPLER_BIT)
	{
		imageUsageFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;
	}

	if (textureCreateInfo->usageFlags & REFRESH_TEXTUREUSAGE_COLOR_TARGET_BIT)
	{
		imageUsageFlags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}

	if (textureCreateInfo->usageFlags & REFRESH_TEXTUREUSAGE_DEPTH_STENCIL_TARGET_BIT)
	{
		imageUsageFlags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}

	if (IsDepthFormat(format))
	{
		imageAspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;

		if (IsStencilFormat(format))
		{
			imageAspectFlags |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
	}
	else
	{
		imageAspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
	}

	return (Refresh_Texture*) VULKAN_INTERNAL_CreateTexture(
		renderer,
		textureCreateInfo->width,
		textureCreateInfo->height,
		textureCreateInfo->depth,
		textureCreateInfo->isCube,
		VK_SAMPLE_COUNT_1_BIT,
		textureCreateInfo->levelCount,
		format,
		imageAspectFlags,
		VK_IMAGE_TYPE_2D,
		imageUsageFlags
	);
}

static Refresh_RenderTarget* VULKAN_CreateRenderTarget(
	Refresh_Renderer *driverData,
	Refresh_TextureSlice *textureSlice,
	Refresh_SampleCount multisampleCount
) {
	VkResult vulkanResult;
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanRenderTarget *renderTarget = (VulkanRenderTarget*) SDL_malloc(sizeof(VulkanRenderTarget));
	VkImageViewCreateInfo imageViewCreateInfo;
	VkComponentMapping swizzle = IDENTITY_SWIZZLE;
	VkImageAspectFlags aspectFlags = 0;

	renderTarget->texture = (VulkanTexture*) textureSlice->texture;
	renderTarget->layer = textureSlice->layer;
	renderTarget->multisampleTexture = NULL;
	renderTarget->multisampleCount = 1;

	if (IsDepthFormat(renderTarget->texture->format))
	{
		aspectFlags |= VK_IMAGE_ASPECT_DEPTH_BIT;

		if (IsStencilFormat(renderTarget->texture->format))
		{
			aspectFlags |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
	}
	else
	{
		aspectFlags |= VK_IMAGE_ASPECT_COLOR_BIT;
	}


	/* create resolve target for multisample */
	if (multisampleCount > REFRESH_SAMPLECOUNT_1)
	{
		renderTarget->multisampleTexture =
			VULKAN_INTERNAL_CreateTexture(
				renderer,
				renderTarget->texture->dimensions.width,
				renderTarget->texture->dimensions.height,
				1,
				0,
				RefreshToVK_SampleCount[multisampleCount],
				1,
				renderTarget->texture->format,
				aspectFlags,
				VK_IMAGE_TYPE_2D,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
			);

		renderTarget->multisampleCount = multisampleCount;
	}

	/* create framebuffer compatible views for RenderTarget */
	imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imageViewCreateInfo.pNext = NULL;
	imageViewCreateInfo.flags = 0;
	imageViewCreateInfo.image = renderTarget->texture->image;
	imageViewCreateInfo.format = renderTarget->texture->format;
	imageViewCreateInfo.components = swizzle;
	imageViewCreateInfo.subresourceRange.aspectMask = aspectFlags;
	imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
	imageViewCreateInfo.subresourceRange.levelCount = 1;
	imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
	if (renderTarget->texture->is3D)
	{
		imageViewCreateInfo.subresourceRange.baseArrayLayer = textureSlice->depth;
	}
	else if (renderTarget->texture->isCube)
	{
		imageViewCreateInfo.subresourceRange.baseArrayLayer = textureSlice->layer;
	}
	imageViewCreateInfo.subresourceRange.layerCount = 1;
	imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;

	vulkanResult = renderer->vkCreateImageView(
		renderer->logicalDevice,
		&imageViewCreateInfo,
		NULL,
		&renderTarget->view
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResultAsError(
			"vkCreateImageView",
			vulkanResult
		);
		Refresh_LogError("Failed to create color attachment image view");
		return NULL;
	}

	return (Refresh_RenderTarget*) renderTarget;
}

static Refresh_Buffer* VULKAN_CreateBuffer(
	Refresh_Renderer *driverData,
	Refresh_BufferUsageFlags usageFlags,
	uint32_t sizeInBytes
) {
	VulkanBuffer* buffer;

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

	buffer = VULKAN_INTERNAL_CreateBuffer(
		(VulkanRenderer*)driverData,
		sizeInBytes,
		RESOURCE_ACCESS_MEMORY_TRANSFER_READ_WRITE,
		vulkanUsageFlags
	);

	if (buffer == NULL)
	{
		Refresh_LogError("Failed to create buffer!");
		return NULL;
	}

	return (Refresh_Buffer*) buffer;
}

/* Setters */

static VulkanTransferBuffer* VULKAN_INTERNAL_AcquireTransferBuffer(
	VulkanRenderer *renderer,
	VulkanCommandBuffer *commandBuffer,
	VkDeviceSize requiredSize
) {
	VkDeviceSize size;
	uint32_t i;
	VulkanTransferBuffer *transferBuffer;

	/* Search the command buffer's current transfer buffers */

	for (i = 0; i < commandBuffer->transferBufferCount; i += 1)
	{
		transferBuffer = commandBuffer->transferBuffers[i];

		if (transferBuffer->offset + requiredSize <= transferBuffer->buffer->size)
		{
			return transferBuffer;
		}
	}

	/* Nothing fits, so let's get a transfer buffer from the pool */

	SDL_LockMutex(renderer->transferBufferPool.lock);

	for (i = 0; i < renderer->transferBufferPool.availableBufferCount; i += 1)
	{
		transferBuffer = renderer->transferBufferPool.availableBuffers[i];

		if (transferBuffer->offset + requiredSize <= transferBuffer->buffer->size)
		{
			if (commandBuffer->transferBufferCount == commandBuffer->transferBufferCapacity)
			{
				commandBuffer->transferBufferCapacity *= 2;
				commandBuffer->transferBuffers = SDL_realloc(
					commandBuffer->transferBuffers,
					commandBuffer->transferBufferCapacity * sizeof(VulkanTransferBuffer*)
				);
			}

			commandBuffer->transferBuffers[commandBuffer->transferBufferCount] = transferBuffer;
			commandBuffer->transferBufferCount += 1;

			renderer->transferBufferPool.availableBuffers[i] = renderer->transferBufferPool.availableBuffers[renderer->transferBufferPool.availableBufferCount - 1];
			renderer->transferBufferPool.availableBufferCount -= 1;
			SDL_UnlockMutex(renderer->transferBufferPool.lock);

			return transferBuffer;
		}
	}

	SDL_UnlockMutex(renderer->transferBufferPool.lock);

	/* Nothing fits still, so let's create a new transfer buffer */

	size = TRANSFER_BUFFER_STARTING_SIZE;

	while (size < requiredSize)
	{
		size *= 2;
	}

	transferBuffer = SDL_malloc(sizeof(VulkanTransferBuffer));
	transferBuffer->offset = 0;
	transferBuffer->buffer = VULKAN_INTERNAL_CreateBuffer(
		renderer,
		size,
		RESOURCE_ACCESS_MEMORY_TRANSFER_READ_WRITE,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
	);

	if (transferBuffer == NULL)
	{
		Refresh_LogError("Failed to allocate transfer buffer!");
		return NULL;
	}

	if (commandBuffer->transferBufferCount == commandBuffer->transferBufferCapacity)
	{
		commandBuffer->transferBufferCapacity *= 2;
		commandBuffer->transferBuffers = SDL_realloc(
			commandBuffer->transferBuffers,
			commandBuffer->transferBufferCapacity * sizeof(VulkanTransferBuffer*)
		);
	}

	commandBuffer->transferBuffers[commandBuffer->transferBufferCount] = transferBuffer;
	commandBuffer->transferBufferCount += 1;

	return transferBuffer;
}

static void VULKAN_SetTextureData(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_TextureSlice *textureSlice,
	void *data,
	uint32_t dataLengthInBytes
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTexture *vulkanTexture = (VulkanTexture*) textureSlice->texture;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	VulkanTransferBuffer *transferBuffer;
	VkBufferImageCopy imageCopy;
	uint8_t *stagingBufferPointer;

	if (vulkanCommandBuffer->renderPassInProgress)
	{
		Refresh_LogError("Cannot perform buffer updates mid-render pass!");
		return;
	}

	transferBuffer = VULKAN_INTERNAL_AcquireTransferBuffer(
		renderer,
		vulkanCommandBuffer,
		VULKAN_INTERNAL_BytesPerImage(
			textureSlice->rectangle.w,
			textureSlice->rectangle.h,
			vulkanTexture->format
		)
	);

	if (transferBuffer == NULL)
	{
		return;
	}

	stagingBufferPointer =
		transferBuffer->buffer->allocation->mapPointer +
		transferBuffer->buffer->offset +
		transferBuffer->offset;

	SDL_memcpy(
		stagingBufferPointer,
		data,
		dataLengthInBytes
	);

	/* TODO: is it worth it to only transition the specific subresource? */
	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		vulkanCommandBuffer->commandBuffer,
		RESOURCE_ACCESS_TRANSFER_WRITE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		vulkanTexture->layerCount,
		0,
		vulkanTexture->levelCount,
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
	imageCopy.bufferOffset = transferBuffer->offset;
	imageCopy.bufferRowLength = 0;
	imageCopy.bufferImageHeight = 0;

	renderer->vkCmdCopyBufferToImage(
		vulkanCommandBuffer->commandBuffer,
		transferBuffer->buffer->buffer,
		vulkanTexture->image,
		AccessMap[vulkanTexture->resourceAccessType].imageLayout,
		1,
		&imageCopy
	);

	transferBuffer->offset += dataLengthInBytes;

	if (vulkanTexture->usageFlags & VK_IMAGE_USAGE_SAMPLED_BIT)
	{
		/* TODO: is it worth it to only transition the specific subresource? */
		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			vulkanCommandBuffer->commandBuffer,
			RESOURCE_ACCESS_ANY_SHADER_READ_SAMPLED_IMAGE,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0,
			vulkanTexture->layerCount,
			0,
			vulkanTexture->levelCount,
			0,
			vulkanTexture->image,
			&vulkanTexture->resourceAccessType
		);
	}
}

static void VULKAN_SetTextureDataYUV(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer* commandBuffer,
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

	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*)commandBuffer;
	VulkanTransferBuffer *transferBuffer;
	uint8_t *dataPtr = (uint8_t*) data;
	int32_t yDataLength = BytesPerImage(yWidth, yHeight, REFRESH_TEXTUREFORMAT_R8);
	int32_t uvDataLength = BytesPerImage(uvWidth, uvHeight, REFRESH_TEXTUREFORMAT_R8);
	VkBufferImageCopy imageCopy;
	uint8_t * stagingBufferPointer;

	transferBuffer = VULKAN_INTERNAL_AcquireTransferBuffer(
		renderer,
		vulkanCommandBuffer,
		yDataLength + uvDataLength
	);

	if (transferBuffer == NULL)
	{
		return;
	}

	stagingBufferPointer =
		transferBuffer->buffer->allocation->mapPointer +
		transferBuffer->buffer->offset +
		transferBuffer->offset;

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
		vulkanCommandBuffer->commandBuffer,
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
	imageCopy.bufferOffset = transferBuffer->offset;
	imageCopy.bufferRowLength = yWidth;
	imageCopy.bufferImageHeight = yHeight;

	renderer->vkCmdCopyBufferToImage(
		vulkanCommandBuffer->commandBuffer,
		transferBuffer->buffer->buffer,
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

	imageCopy.bufferOffset = transferBuffer->offset + yDataLength;

	tex = (VulkanTexture*) u;

	SDL_memcpy(
		stagingBufferPointer + yDataLength,
		dataPtr + yDataLength,
		uvDataLength
	);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		vulkanCommandBuffer->commandBuffer,
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
		vulkanCommandBuffer->commandBuffer,
		transferBuffer->buffer->buffer,
		tex->image,
		AccessMap[tex->resourceAccessType].imageLayout,
		1,
		&imageCopy
	);

	/* V */

	imageCopy.bufferOffset = transferBuffer->offset + yDataLength + uvDataLength;

	tex = (VulkanTexture*) v;

	SDL_memcpy(
		stagingBufferPointer + yDataLength + uvDataLength,
		dataPtr + yDataLength + uvDataLength,
		uvDataLength
	);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		vulkanCommandBuffer->commandBuffer,
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
		vulkanCommandBuffer->commandBuffer,
		transferBuffer->buffer->buffer,
		tex->image,
		AccessMap[tex->resourceAccessType].imageLayout,
		1,
		&imageCopy
	);

	transferBuffer->offset += yDataLength + uvDataLength;

	if (tex->usageFlags & VK_IMAGE_USAGE_SAMPLED_BIT)
	{
		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			vulkanCommandBuffer->commandBuffer,
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
}

static void VULKAN_INTERNAL_BlitImage(
	VulkanRenderer *renderer,
	VkCommandBuffer commandBuffer,
	Refresh_TextureSlice *sourceTextureSlice,
	Refresh_TextureSlice *destinationTextureSlice,
	VulkanResourceAccessType newDestinationAccessType,
	VkFilter filter
) {
	VkImageBlit blit;
	VulkanTexture *sourceTexture = (VulkanTexture*) sourceTextureSlice->texture;
	VulkanTexture *destinationTexture = (VulkanTexture*) destinationTextureSlice->texture;

	VulkanResourceAccessType originalSourceAccessType = sourceTexture->resourceAccessType;

	/* TODO: is it worth it to only transition the specific subresource? */
	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		commandBuffer,
		RESOURCE_ACCESS_TRANSFER_READ,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		sourceTexture->layerCount,
		0,
		sourceTexture->levelCount,
		0,
		sourceTexture->image,
		&sourceTexture->resourceAccessType
	);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		commandBuffer,
		RESOURCE_ACCESS_TRANSFER_WRITE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		destinationTexture->layerCount,
		0,
		destinationTexture->levelCount,
		0,
		destinationTexture->image,
		&destinationTexture->resourceAccessType
	);

	blit.srcOffsets[0].x = sourceTextureSlice->rectangle.x;
	blit.srcOffsets[0].y = sourceTextureSlice->rectangle.y;
	blit.srcOffsets[0].z = sourceTextureSlice->depth;
	blit.srcOffsets[1].x = sourceTextureSlice->rectangle.x + sourceTextureSlice->rectangle.w;
	blit.srcOffsets[1].y = sourceTextureSlice->rectangle.y + sourceTextureSlice->rectangle.h;
	blit.srcOffsets[1].z = 1;

	blit.srcSubresource.mipLevel = sourceTextureSlice->level;
	blit.srcSubresource.baseArrayLayer = sourceTextureSlice->layer;
	blit.srcSubresource.layerCount = 1;
	blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	blit.dstOffsets[0].x = destinationTextureSlice->rectangle.x;
	blit.dstOffsets[0].y = destinationTextureSlice->rectangle.y;
	blit.dstOffsets[0].z = destinationTextureSlice->depth;
	blit.dstOffsets[1].x = destinationTextureSlice->rectangle.x + destinationTextureSlice->rectangle.w;
	blit.dstOffsets[1].y = destinationTextureSlice->rectangle.y + destinationTextureSlice->rectangle.h;
	blit.dstOffsets[1].z = 1;

	blit.dstSubresource.mipLevel = sourceTextureSlice->level;
	blit.dstSubresource.baseArrayLayer = sourceTextureSlice->layer;
	blit.dstSubresource.layerCount = 1;
	blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	renderer->vkCmdBlitImage(
		commandBuffer,
		sourceTexture->image,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		destinationTexture->image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&blit,
		filter
	);

	/* TODO: is it worth it to only transition the specific subresource? */
	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		commandBuffer,
		originalSourceAccessType,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		sourceTexture->layerCount,
		0,
		sourceTexture->levelCount,
		0,
		sourceTexture->image,
		&sourceTexture->resourceAccessType
	);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		commandBuffer,
		newDestinationAccessType,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		destinationTexture->layerCount,
		0,
		destinationTexture->levelCount,
		0,
		destinationTexture->image,
		&destinationTexture->resourceAccessType
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
	VulkanTexture *destinationTexture = (VulkanTexture*) destinationTextureSlice->texture;

	VULKAN_INTERNAL_BlitImage(
		renderer,
		vulkanCommandBuffer->commandBuffer,
		sourceTextureSlice,
		destinationTextureSlice,
		destinationTexture->resourceAccessType,
		RefreshToVK_Filter[filter]
	);
}

static void VULKAN_INTERNAL_SetBufferData(
	VulkanBuffer* vulkanBuffer,
	VkDeviceSize offsetInBytes,
	void* data,
	uint32_t dataLength
) {
	SDL_memcpy(
		vulkanBuffer->allocation->mapPointer + vulkanBuffer->offset + offsetInBytes,
		data,
		dataLength
	);
}

static void VULKAN_SetBufferData(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Buffer *buffer,
	uint32_t offsetInBytes,
	void* data,
	uint32_t dataLength
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer* vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	VulkanBuffer* vulkanBuffer = (VulkanBuffer*) buffer;
	VulkanTransferBuffer* transferBuffer;
	uint8_t* transferBufferPointer;
	VkBufferCopy bufferCopy;
	VulkanResourceAccessType accessType = vulkanBuffer->resourceAccessType;

	if (vulkanCommandBuffer->renderPassInProgress)
	{
		Refresh_LogError("Cannot perform buffer updates mid-render pass!");
		return;
	}

	transferBuffer = VULKAN_INTERNAL_AcquireTransferBuffer(
		renderer,
		vulkanCommandBuffer,
		dataLength
	);

	if (transferBuffer == NULL)
	{
		return;
	}

	transferBufferPointer =
		transferBuffer->buffer->allocation->mapPointer +
		transferBuffer->buffer->offset +
		transferBuffer->offset;

	SDL_memcpy(
		transferBufferPointer,
		data,
		dataLength
	);

	VULKAN_INTERNAL_BufferMemoryBarrier(
		renderer,
		vulkanCommandBuffer->commandBuffer,
		RESOURCE_ACCESS_TRANSFER_READ,
		transferBuffer->buffer
	);

	VULKAN_INTERNAL_BufferMemoryBarrier(
		renderer,
		vulkanCommandBuffer->commandBuffer,
		RESOURCE_ACCESS_TRANSFER_WRITE,
		vulkanBuffer
	);

	bufferCopy.srcOffset = transferBuffer->offset;
	bufferCopy.dstOffset = offsetInBytes;
	bufferCopy.size = (VkDeviceSize) dataLength;

	renderer->vkCmdCopyBuffer(
		vulkanCommandBuffer->commandBuffer,
		transferBuffer->buffer->buffer,
		vulkanBuffer->buffer,
		1,
		&bufferCopy
	);

	VULKAN_INTERNAL_BufferMemoryBarrier(
		renderer,
		vulkanCommandBuffer->commandBuffer,
		accessType,
		vulkanBuffer
	);

	transferBuffer->offset += dataLength;
}

/* FIXME: this should return uint64_t */
static uint32_t VULKAN_PushVertexShaderUniforms(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	void *data,
	uint32_t dataLengthInBytes
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer* vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	VulkanGraphicsPipeline* graphicsPipeline = vulkanCommandBuffer->currentGraphicsPipeline;
	uint32_t offset;

	if (graphicsPipeline == NULL)
	{
		Refresh_LogError("Cannot push uniforms if a pipeline is not bound!");
		return 0;
	}

	if (graphicsPipeline->vertexUniformBlockSize == 0)
	{
		Refresh_LogError("Bound pipeline's vertex stage does not declare uniforms!");
		return 0;
	}

	if (
		vulkanCommandBuffer->vertexUniformBuffer->offset +
		graphicsPipeline->vertexUniformBlockSize >=
		UBO_BUFFER_SIZE
	) {
		/* We're out of space in this buffer, bind the old one and acquire a new one */
		VULKAN_INTERNAL_BindUniformBuffer(
			vulkanCommandBuffer,
			vulkanCommandBuffer->vertexUniformBuffer
		);
		vulkanCommandBuffer->vertexUniformBuffer = VULKAN_INTERNAL_AcquireUniformBufferFromPool(
			renderer,
			renderer->vertexUniformBufferPool,
			graphicsPipeline->vertexUniformBlockSize
		);
	}

	offset = vulkanCommandBuffer->vertexUniformBuffer->offset;

	VULKAN_INTERNAL_SetBufferData(
		vulkanCommandBuffer->vertexUniformBuffer->vulkanBuffer,
		vulkanCommandBuffer->vertexUniformBuffer->offset,
		data,
		dataLengthInBytes
	);

	vulkanCommandBuffer->vertexUniformBuffer->offset += graphicsPipeline->vertexUniformBlockSize;

	return offset;
}

/* FIXME: this should return uint64_t */
static uint32_t VULKAN_PushFragmentShaderUniforms(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	void *data,
	uint32_t dataLengthInBytes
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer* vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	VulkanGraphicsPipeline* graphicsPipeline = vulkanCommandBuffer->currentGraphicsPipeline;
	uint32_t offset;

	if (graphicsPipeline == NULL)
	{
		Refresh_LogError("Cannot push uniforms if a pipeline is not bound!");
		return 0;
	}

	if (graphicsPipeline->fragmentUniformBlockSize == 0)
	{
		Refresh_LogError("Bound pipeline's fragment stage does not declare uniforms!");
		return 0;
	}

	if (
		vulkanCommandBuffer->fragmentUniformBuffer->offset +
		graphicsPipeline->fragmentUniformBlockSize >=
		UBO_BUFFER_SIZE
	) {
		/* We're out of space in this buffer, bind the old one and acquire a new one */
		VULKAN_INTERNAL_BindUniformBuffer(
			vulkanCommandBuffer,
			vulkanCommandBuffer->fragmentUniformBuffer
		);
		vulkanCommandBuffer->fragmentUniformBuffer = VULKAN_INTERNAL_AcquireUniformBufferFromPool(
			renderer,
			renderer->fragmentUniformBufferPool,
			graphicsPipeline->fragmentUniformBlockSize
		);
	}

	offset = vulkanCommandBuffer->fragmentUniformBuffer->offset;

	VULKAN_INTERNAL_SetBufferData(
		vulkanCommandBuffer->fragmentUniformBuffer->vulkanBuffer,
		vulkanCommandBuffer->fragmentUniformBuffer->offset,
		data,
		dataLengthInBytes
	);

	vulkanCommandBuffer->fragmentUniformBuffer->offset += graphicsPipeline->fragmentUniformBlockSize;

	return offset;
}

static uint32_t VULKAN_PushComputeShaderUniforms(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	void *data,
	uint32_t dataLengthInBytes
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer* vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	VulkanComputePipeline* computePipeline = vulkanCommandBuffer->currentComputePipeline;
	uint32_t offset;

	if (computePipeline == NULL)
	{
		Refresh_LogError("Cannot push uniforms if a pipeline is not bound!");
		return 0;
	}

	if (computePipeline->uniformBlockSize == 0)
	{
		Refresh_LogError("Bound compute pipeline does not declare uniforms!");
		return 0;
	}

	if (
		vulkanCommandBuffer->computeUniformBuffer->offset +
		computePipeline->uniformBlockSize >=
		UBO_BUFFER_SIZE
	) {
		/* We're out of space in this buffer, bind the old one and acquire a new one */
		VULKAN_INTERNAL_BindUniformBuffer(
			vulkanCommandBuffer,
			vulkanCommandBuffer->computeUniformBuffer
		);
		vulkanCommandBuffer->computeUniformBuffer = VULKAN_INTERNAL_AcquireUniformBufferFromPool(
			renderer,
			renderer->computeUniformBufferPool,
			computePipeline->uniformBlockSize
		);
	}

	offset = vulkanCommandBuffer->computeUniformBuffer->offset;

	VULKAN_INTERNAL_SetBufferData(
		vulkanCommandBuffer->computeUniformBuffer->vulkanBuffer,
		vulkanCommandBuffer->computeUniformBuffer->offset,
		data,
		dataLengthInBytes
	);

	vulkanCommandBuffer->computeUniformBuffer->offset += computePipeline->uniformBlockSize;

	return offset;
}

/* If fetching an image descriptor, descriptorImageInfos must not be NULL.
 * If fetching a buffer descriptor, descriptorBufferInfos must not be NULL.
 */
static VkDescriptorSet VULKAN_INTERNAL_FetchDescriptorSet(
	VulkanRenderer *renderer,
	VulkanCommandBuffer *vulkanCommandBuffer,
	DescriptorSetCache *descriptorSetCache,
	VkDescriptorImageInfo *descriptorImageInfos, /* Can be NULL */
	VkDescriptorBufferInfo *descriptorBufferInfos /* Can be NULL */
) {
	uint32_t i;
	VkDescriptorSet descriptorSet;
	VkWriteDescriptorSet writeDescriptorSets[MAX_TEXTURE_SAMPLERS];
	uint8_t isImage;

	if (descriptorImageInfos == NULL && descriptorBufferInfos == NULL)
	{
		Refresh_LogError("descriptorImageInfos and descriptorBufferInfos cannot both be NULL!");
		return VK_NULL_HANDLE;
	}
	else if (descriptorImageInfos != NULL && descriptorBufferInfos != NULL)
	{
		Refresh_LogError("descriptorImageInfos and descriptorBufferInfos cannot both be set!");
		return VK_NULL_HANDLE;
	}

	isImage = descriptorImageInfos != NULL;

	SDL_LockMutex(descriptorSetCache->lock);

	/* If no inactive descriptor sets remain, create a new pool and allocate new inactive sets */

	if (descriptorSetCache->inactiveDescriptorSetCount == 0)
	{
		descriptorSetCache->descriptorPoolCount += 1;
		descriptorSetCache->descriptorPools = SDL_realloc(
			descriptorSetCache->descriptorPools,
			sizeof(VkDescriptorPool) * descriptorSetCache->descriptorPoolCount
		);

		if (!VULKAN_INTERNAL_CreateDescriptorPool(
			renderer,
			descriptorSetCache->descriptorType,
			descriptorSetCache->nextPoolSize,
			descriptorSetCache->nextPoolSize * descriptorSetCache->bindingCount,
			&descriptorSetCache->descriptorPools[descriptorSetCache->descriptorPoolCount - 1]
		)) {
			SDL_UnlockMutex(descriptorSetCache->lock);
			Refresh_LogError("Failed to create descriptor pool!");
			return VK_NULL_HANDLE;
		}

		descriptorSetCache->inactiveDescriptorSetCapacity += descriptorSetCache->nextPoolSize;

		descriptorSetCache->inactiveDescriptorSets = SDL_realloc(
			descriptorSetCache->inactiveDescriptorSets,
			sizeof(VkDescriptorSet) * descriptorSetCache->inactiveDescriptorSetCapacity
		);

		if (!VULKAN_INTERNAL_AllocateDescriptorSets(
			renderer,
			descriptorSetCache->descriptorPools[descriptorSetCache->descriptorPoolCount - 1],
			descriptorSetCache->descriptorSetLayout,
			descriptorSetCache->nextPoolSize,
			descriptorSetCache->inactiveDescriptorSets
		)) {
			SDL_UnlockMutex(descriptorSetCache->lock);
			Refresh_LogError("Failed to allocate descriptor sets!");
			return VK_NULL_HANDLE;
		}

		descriptorSetCache->inactiveDescriptorSetCount = descriptorSetCache->nextPoolSize;

		descriptorSetCache->nextPoolSize *= 2;
	}

	descriptorSet = descriptorSetCache->inactiveDescriptorSets[descriptorSetCache->inactiveDescriptorSetCount - 1];
	descriptorSetCache->inactiveDescriptorSetCount -= 1;

	for (i = 0; i < descriptorSetCache->bindingCount; i += 1)
	{
		writeDescriptorSets[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSets[i].pNext = NULL;
		writeDescriptorSets[i].descriptorCount = 1;
		writeDescriptorSets[i].descriptorType = descriptorSetCache->descriptorType;
		writeDescriptorSets[i].dstArrayElement = 0;
		writeDescriptorSets[i].dstBinding = i;
		writeDescriptorSets[i].dstSet = descriptorSet;
		writeDescriptorSets[i].pTexelBufferView = NULL;

		if (isImage)
		{
			writeDescriptorSets[i].pImageInfo = &descriptorImageInfos[i];
			writeDescriptorSets[i].pBufferInfo = NULL;

		}
		else
		{
			writeDescriptorSets[i].pBufferInfo = &descriptorBufferInfos[i];
			writeDescriptorSets[i].pImageInfo = NULL;
		}
	}

	renderer->vkUpdateDescriptorSets(
		renderer->logicalDevice,
		descriptorSetCache->bindingCount,
		writeDescriptorSets,
		0,
		NULL
	);

	SDL_UnlockMutex(descriptorSetCache->lock);

	if (vulkanCommandBuffer->boundDescriptorSetDataCount == vulkanCommandBuffer->boundDescriptorSetDataCapacity)
	{
		vulkanCommandBuffer->boundDescriptorSetDataCapacity *= 2;
		vulkanCommandBuffer->boundDescriptorSetDatas = SDL_realloc(
			vulkanCommandBuffer->boundDescriptorSetDatas,
			vulkanCommandBuffer->boundDescriptorSetDataCapacity * sizeof(DescriptorSetData)
		);
	}

	vulkanCommandBuffer->boundDescriptorSetDatas[vulkanCommandBuffer->boundDescriptorSetDataCount].descriptorSet = descriptorSet;
	vulkanCommandBuffer->boundDescriptorSetDatas[vulkanCommandBuffer->boundDescriptorSetDataCount].descriptorSetCache = descriptorSetCache;
	vulkanCommandBuffer->boundDescriptorSetDataCount += 1;

	return descriptorSet;
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
	VkDescriptorImageInfo descriptorImageInfos[MAX_TEXTURE_SAMPLERS];

	if (graphicsPipeline->pipelineLayout->vertexSamplerDescriptorSetCache == NULL)
	{
		return;
	}

	samplerCount = graphicsPipeline->pipelineLayout->vertexSamplerDescriptorSetCache->bindingCount;

	for (i = 0; i < samplerCount; i += 1)
	{
		currentTexture = (VulkanTexture*) pTextures[i];
		descriptorImageInfos[i].imageView = currentTexture->view;
		descriptorImageInfos[i].sampler = (VkSampler) pSamplers[i];
		descriptorImageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	vulkanCommandBuffer->vertexSamplerDescriptorSet = VULKAN_INTERNAL_FetchDescriptorSet(
		renderer,
		vulkanCommandBuffer,
		graphicsPipeline->pipelineLayout->vertexSamplerDescriptorSetCache,
		descriptorImageInfos,
		NULL
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
	VkDescriptorImageInfo descriptorImageInfos[MAX_TEXTURE_SAMPLERS];

	if (graphicsPipeline->pipelineLayout->fragmentSamplerDescriptorSetCache == NULL)
	{
		return;
	}

	samplerCount = graphicsPipeline->pipelineLayout->fragmentSamplerDescriptorSetCache->bindingCount;

	for (i = 0; i < samplerCount; i += 1)
	{
		currentTexture = (VulkanTexture*) pTextures[i];
		descriptorImageInfos[i].imageView = currentTexture->view;
		descriptorImageInfos[i].sampler = (VkSampler) pSamplers[i];
		descriptorImageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	vulkanCommandBuffer->fragmentSamplerDescriptorSet = VULKAN_INTERNAL_FetchDescriptorSet(
		renderer,
		vulkanCommandBuffer,
		graphicsPipeline->pipelineLayout->fragmentSamplerDescriptorSetCache,
		descriptorImageInfos,
		NULL
	);
}

static void VULKAN_GetBufferData(
	Refresh_Renderer *driverData,
	Refresh_Buffer *buffer,
	void *data,
	uint32_t dataLengthInBytes
) {
	VulkanBuffer* vulkanBuffer = (VulkanBuffer*) buffer;
	uint8_t *dataPtr = (uint8_t*) data;
	uint8_t *mapPointer;

	mapPointer =
		vulkanBuffer->allocation->mapPointer +
		vulkanBuffer->offset;

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
	VulkanBuffer* vulkanBuffer = (VulkanBuffer*) buffer;

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
		vulkanBuffer->buffer,
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
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Texture *texture
) {
	VulkanCommandBuffer* vulkanCommandBuffer = (VulkanCommandBuffer*)commandBuffer;
	VulkanTexture* vulkanTexture = (VulkanTexture*)texture;

	EXPAND_ARRAY_IF_NEEDED(
		vulkanCommandBuffer->texturesToDestroy,
		VulkanTexture*,
		vulkanCommandBuffer->texturesToDestroyCount + 1,
		vulkanCommandBuffer->texturesToDestroyCapacity,
		vulkanCommandBuffer->texturesToDestroyCapacity * 2
	)

	vulkanCommandBuffer->texturesToDestroy[vulkanCommandBuffer->texturesToDestroyCount] = vulkanTexture;
	vulkanCommandBuffer->texturesToDestroyCount += 1;
}

static void VULKAN_QueueDestroySampler(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Sampler *sampler
) {
	VulkanCommandBuffer* vulkanCommandBuffer = (VulkanCommandBuffer*)commandBuffer;
	VkSampler vulkanSampler = (VkSampler) sampler;

	EXPAND_ARRAY_IF_NEEDED(
		vulkanCommandBuffer->samplersToDestroy,
		VkSampler,
		vulkanCommandBuffer->samplersToDestroyCount + 1,
		vulkanCommandBuffer->samplersToDestroyCapacity,
		vulkanCommandBuffer->samplersToDestroyCapacity * 2
	)

	vulkanCommandBuffer->samplersToDestroy[vulkanCommandBuffer->samplersToDestroyCount] = vulkanSampler;
	vulkanCommandBuffer->samplersToDestroyCount += 1;
}

static void VULKAN_QueueDestroyBuffer(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Buffer *buffer
) {
	VulkanCommandBuffer* vulkanCommandBuffer = (VulkanCommandBuffer*)commandBuffer;
	VulkanBuffer *vulkanBuffer = (VulkanBuffer*) buffer;

	EXPAND_ARRAY_IF_NEEDED(
		vulkanCommandBuffer->buffersToDestroy,
		VulkanBuffer*,
		vulkanCommandBuffer->buffersToDestroyCount + 1,
		vulkanCommandBuffer->buffersToDestroyCapacity,
		vulkanCommandBuffer->buffersToDestroyCapacity * 2
	)

	vulkanCommandBuffer->buffersToDestroy[
		vulkanCommandBuffer->buffersToDestroyCount
	] = vulkanBuffer;
	vulkanCommandBuffer->buffersToDestroyCount += 1;
}

static void VULKAN_QueueDestroyRenderTarget(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_RenderTarget *renderTarget
) {
	VulkanCommandBuffer* vulkanCommandBuffer = (VulkanCommandBuffer*)commandBuffer;
	VulkanRenderTarget *vulkanRenderTarget = (VulkanRenderTarget*) renderTarget;

	EXPAND_ARRAY_IF_NEEDED(
		vulkanCommandBuffer->renderTargetsToDestroy,
		VulkanRenderTarget*,
		vulkanCommandBuffer->renderTargetsToDestroyCount + 1,
		vulkanCommandBuffer->renderTargetsToDestroyCapacity,
		vulkanCommandBuffer->renderTargetsToDestroyCapacity * 2
	)

	vulkanCommandBuffer->renderTargetsToDestroy[vulkanCommandBuffer->renderTargetsToDestroyCount] = vulkanRenderTarget;
	vulkanCommandBuffer->renderTargetsToDestroyCount += 1;
}

static void VULKAN_QueueDestroyShaderModule(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_ShaderModule *shaderModule
) {
	VulkanCommandBuffer* vulkanCommandBuffer = (VulkanCommandBuffer*)commandBuffer;
	VkShaderModule vulkanShaderModule = (VkShaderModule) shaderModule;

	EXPAND_ARRAY_IF_NEEDED(
		vulkanCommandBuffer->shaderModulesToDestroy,
		VkShaderModule,
		vulkanCommandBuffer->shaderModulesToDestroyCount + 1,
		vulkanCommandBuffer->shaderModulesToDestroyCapacity,
		vulkanCommandBuffer->shaderModulesToDestroyCapacity * 2
	)

	vulkanCommandBuffer->shaderModulesToDestroy[vulkanCommandBuffer->shaderModulesToDestroyCount] = vulkanShaderModule;
	vulkanCommandBuffer->shaderModulesToDestroyCount += 1;
}

static void VULKAN_QueueDestroyComputePipeline(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_ComputePipeline *computePipeline
) {
	VulkanCommandBuffer* vulkanCommandBuffer = (VulkanCommandBuffer*)commandBuffer;
	VulkanComputePipeline *vulkanComputePipeline = (VulkanComputePipeline*) computePipeline;

	EXPAND_ARRAY_IF_NEEDED(
		vulkanCommandBuffer->computePipelinesToDestroy,
		VulkanComputePipeline*,
		vulkanCommandBuffer->computePipelinesToDestroyCount + 1,
		vulkanCommandBuffer->computePipelinesToDestroyCapacity,
		vulkanCommandBuffer->computePipelinesToDestroyCapacity * 2
	)

	vulkanCommandBuffer->computePipelinesToDestroy[vulkanCommandBuffer->computePipelinesToDestroyCount] = vulkanComputePipeline;
	vulkanCommandBuffer->computePipelinesToDestroyCount += 1;
}

static void VULKAN_QueueDestroyGraphicsPipeline(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_GraphicsPipeline *graphicsPipeline
) {
	VulkanCommandBuffer* vulkanCommandBuffer = (VulkanCommandBuffer*)commandBuffer;
	VulkanGraphicsPipeline *vulkanGraphicsPipeline = (VulkanGraphicsPipeline*) graphicsPipeline;

	EXPAND_ARRAY_IF_NEEDED(
		vulkanCommandBuffer->graphicsPipelinesToDestroy,
		VulkanGraphicsPipeline*,
		vulkanCommandBuffer->graphicsPipelinesToDestroyCount + 1,
		vulkanCommandBuffer->graphicsPipelinesToDestroyCapacity,
		vulkanCommandBuffer->graphicsPipelinesToDestroyCapacity * 2
	)

	vulkanCommandBuffer->graphicsPipelinesToDestroy[vulkanCommandBuffer->graphicsPipelinesToDestroyCount] = vulkanGraphicsPipeline;
	vulkanCommandBuffer->graphicsPipelinesToDestroyCount += 1;
}

/* Command Buffer render state */

static VkRenderPass VULKAN_INTERNAL_FetchRenderPass(
	VulkanRenderer *renderer,
	Refresh_ColorAttachmentInfo *colorAttachmentInfos,
	uint32_t colorAttachmentCount,
	Refresh_DepthStencilAttachmentInfo *depthStencilAttachmentInfo
) {
	VkRenderPass renderPass;
	RenderPassHash hash;
	uint32_t i;

	SDL_LockMutex(renderer->renderPassFetchLock);

	for (i = 0; i < colorAttachmentCount; i += 1)
	{
		hash.colorTargetDescriptions[i].clearColor = colorAttachmentInfos[i].clearColor;
		hash.colorTargetDescriptions[i].loadOp = colorAttachmentInfos[i].loadOp;
		hash.colorTargetDescriptions[i].storeOp = colorAttachmentInfos[i].storeOp;
	}

	hash.colorAttachmentCount = colorAttachmentCount;

	if (depthStencilAttachmentInfo == NULL)
	{
		hash.depthStencilTargetDescription.loadOp = REFRESH_LOADOP_DONT_CARE;
		hash.depthStencilTargetDescription.storeOp = REFRESH_STOREOP_DONT_CARE;
		hash.depthStencilTargetDescription.stencilLoadOp = REFRESH_LOADOP_DONT_CARE;
		hash.depthStencilTargetDescription.stencilStoreOp = REFRESH_STOREOP_DONT_CARE;
	}
	else
	{
		hash.depthStencilTargetDescription.loadOp = depthStencilAttachmentInfo->loadOp;
		hash.depthStencilTargetDescription.storeOp = depthStencilAttachmentInfo->storeOp;
		hash.depthStencilTargetDescription.stencilLoadOp = depthStencilAttachmentInfo->stencilLoadOp;
		hash.depthStencilTargetDescription.stencilStoreOp = depthStencilAttachmentInfo->stencilStoreOp;
	}

	renderPass = RenderPassHashArray_Fetch(
		&renderer->renderPassHashArray,
		&hash
	);

	if (renderPass != VK_NULL_HANDLE)
	{
		SDL_UnlockMutex(renderer->renderPassFetchLock);
		return renderPass;
	}

	renderPass = VULKAN_INTERNAL_CreateRenderPass(
		renderer,
		colorAttachmentInfos,
		colorAttachmentCount,
		depthStencilAttachmentInfo
	);

	if (renderPass != VK_NULL_HANDLE)
	{
		RenderPassHashArray_Insert(
			&renderer->renderPassHashArray,
			hash,
			renderPass
		);
	}

	SDL_UnlockMutex(renderer->renderPassFetchLock);
	return renderPass;
}

static VkFramebuffer VULKAN_INTERNAL_FetchFramebuffer(
	VulkanRenderer *renderer,
	VkRenderPass renderPass,
	Refresh_ColorAttachmentInfo *colorAttachmentInfos,
	uint32_t colorAttachmentCount,
	Refresh_DepthStencilAttachmentInfo *depthStencilAttachmentInfo
) {
	VkFramebuffer framebuffer;
	VkFramebufferCreateInfo framebufferInfo;
	VkResult result;
	VkImageView imageViewAttachments[2 * MAX_COLOR_TARGET_BINDINGS + 1];
	FramebufferHash hash;
	VulkanRenderTarget *renderTarget;
	uint32_t attachmentCount = 0;
	uint32_t i;

	SDL_LockMutex(renderer->framebufferFetchLock);

	for (i = 0; i < MAX_COLOR_TARGET_BINDINGS; i += 1)
	{
		hash.colorAttachmentViews[i] = VK_NULL_HANDLE;
		hash.colorMultiSampleAttachmentViews[i] = VK_NULL_HANDLE;
	}

	hash.colorAttachmentCount = colorAttachmentCount;

	for (i = 0; i < colorAttachmentCount; i += 1)
	{
		renderTarget = (VulkanRenderTarget*) colorAttachmentInfos[i].pRenderTarget;

		hash.colorAttachmentViews[i] = (
			renderTarget->view
		);

		if (renderTarget->multisampleTexture != NULL)
		{
			hash.colorMultiSampleAttachmentViews[i] = (
				renderTarget->multisampleTexture->view
			);
		}
	}

	if (depthStencilAttachmentInfo == NULL)
	{
		hash.depthStencilAttachmentView = VK_NULL_HANDLE;
	}
	else
	{
		hash.depthStencilAttachmentView = ((VulkanRenderTarget*)depthStencilAttachmentInfo->pDepthStencilTarget)->view;
	}

	if (colorAttachmentCount > 0)
	{
		renderTarget = (VulkanRenderTarget*) colorAttachmentInfos[0].pRenderTarget;
	}
	else
	{
		renderTarget = (VulkanRenderTarget*) depthStencilAttachmentInfo->pDepthStencilTarget;
	}

	hash.width = renderTarget->texture->dimensions.width;
	hash.height = renderTarget->texture->dimensions.height;

	framebuffer = FramebufferHashArray_Fetch(
		&renderer->framebufferHashArray,
		&hash
	);

	if (framebuffer != VK_NULL_HANDLE)
	{
		SDL_UnlockMutex(renderer->framebufferFetchLock);
		return framebuffer;
	}

	/* Create a new framebuffer */

	for (i = 0; i < colorAttachmentCount; i += 1)
	{
		renderTarget = (VulkanRenderTarget*) colorAttachmentInfos[i].pRenderTarget;

		imageViewAttachments[attachmentCount] =
			renderTarget->view;

		attachmentCount += 1;

		if (renderTarget->multisampleTexture != NULL)
		{
			imageViewAttachments[attachmentCount] =
				renderTarget->multisampleTexture->view;

			attachmentCount += 1;
		}
	}

	if (depthStencilAttachmentInfo != NULL)
	{
		renderTarget = (VulkanRenderTarget*) depthStencilAttachmentInfo->pDepthStencilTarget;

		imageViewAttachments[attachmentCount] = renderTarget->view;

		attachmentCount += 1;
	}

	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.pNext = NULL;
	framebufferInfo.flags = 0;
	framebufferInfo.renderPass = renderPass;
	framebufferInfo.attachmentCount = attachmentCount;
	framebufferInfo.pAttachments = imageViewAttachments;
	framebufferInfo.width = hash.width;
	framebufferInfo.height = hash.height;
	framebufferInfo.layers = 1;

	result = renderer->vkCreateFramebuffer(
		renderer->logicalDevice,
		&framebufferInfo,
		NULL,
		&framebuffer
	);

	if (result == VK_SUCCESS)
	{
		FramebufferHashArray_Insert(
			&renderer->framebufferHashArray,
			hash,
			framebuffer
		);
	}
	else
	{
		LogVulkanResultAsError("vkCreateFramebuffer", result);
		framebuffer = VK_NULL_HANDLE;
	}

	SDL_UnlockMutex(renderer->framebufferFetchLock);
	return framebuffer;
}

static void VULKAN_BeginRenderPass(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Rect *renderArea,
	Refresh_ColorAttachmentInfo *colorAttachmentInfos,
	uint32_t colorAttachmentCount,
	Refresh_DepthStencilAttachmentInfo *depthStencilAttachmentInfo
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	VkRenderPass renderPass;
	VkFramebuffer framebuffer;

	VulkanRenderTarget *depthStencilTarget;
	VkClearValue *clearValues;
	uint32_t clearCount = colorAttachmentCount;
	uint32_t i;
	VkImageAspectFlags depthAspectFlags;

	if (colorAttachmentCount == 0 && depthStencilAttachmentInfo == NULL)
	{
		Refresh_LogError("Render pass must have at least one render target!");
		return;
	}

	renderPass = VULKAN_INTERNAL_FetchRenderPass(
		renderer,
		colorAttachmentInfos,
		colorAttachmentCount,
		depthStencilAttachmentInfo
	);

	framebuffer = VULKAN_INTERNAL_FetchFramebuffer(
		renderer,
		renderPass,
		colorAttachmentInfos,
		colorAttachmentCount,
		depthStencilAttachmentInfo
	);

	/* Layout transitions */

	for (i = 0; i < colorAttachmentCount; i += 1)
	{
		VulkanRenderTarget *colorTarget = (VulkanRenderTarget*) colorAttachmentInfos->pRenderTarget;

		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			vulkanCommandBuffer->commandBuffer,
			RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0,
			colorTarget->texture->layerCount,
			0,
			colorTarget->texture->levelCount,
			0,
			colorTarget->texture->image,
			&colorTarget->texture->resourceAccessType
		);
	}

	if (depthStencilAttachmentInfo != NULL)
	{
		depthStencilTarget = (VulkanRenderTarget*) depthStencilAttachmentInfo->pDepthStencilTarget;
		depthAspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;

		if (IsStencilFormat(
			depthStencilTarget->texture->format
		)) {
			depthAspectFlags |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}

		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			vulkanCommandBuffer->commandBuffer,
			RESOURCE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_WRITE,
			depthAspectFlags,
			0,
			depthStencilTarget->texture->layerCount,
			0,
			depthStencilTarget->texture->levelCount,
			0,
			depthStencilTarget->texture->image,
			&depthStencilTarget->texture->resourceAccessType
		);

		clearCount += 1;
	}

	/* Set clear values */

	clearValues = SDL_stack_alloc(VkClearValue, clearCount);

	for (i = 0; i < colorAttachmentCount; i += 1)
	{
		clearValues[i].color.float32[0] = colorAttachmentInfos[i].clearColor.x;
		clearValues[i].color.float32[1] = colorAttachmentInfos[i].clearColor.y;
		clearValues[i].color.float32[2] = colorAttachmentInfos[i].clearColor.z;
		clearValues[i].color.float32[3] = colorAttachmentInfos[i].clearColor.w;
	}

	if (depthStencilAttachmentInfo != NULL)
	{
		clearValues[colorAttachmentCount].depthStencil.depth =
			depthStencilAttachmentInfo->depthStencilValue.depth;
		clearValues[colorAttachmentCount].depthStencil.stencil =
			depthStencilAttachmentInfo->depthStencilValue.stencil;
	}

	VkRenderPassBeginInfo renderPassBeginInfo;
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.pNext = NULL;
	renderPassBeginInfo.renderPass = (VkRenderPass) renderPass;
	renderPassBeginInfo.framebuffer = framebuffer;
	renderPassBeginInfo.renderArea.extent.width = renderArea->w;
	renderPassBeginInfo.renderArea.extent.height = renderArea->h;
	renderPassBeginInfo.renderArea.offset.x = renderArea->x;
	renderPassBeginInfo.renderArea.offset.y = renderArea->y;
	renderPassBeginInfo.pClearValues = clearValues;
	renderPassBeginInfo.clearValueCount = clearCount;

	renderer->vkCmdBeginRenderPass(
		vulkanCommandBuffer->commandBuffer,
		&renderPassBeginInfo,
		VK_SUBPASS_CONTENTS_INLINE
	);

	vulkanCommandBuffer->renderPassInProgress = 1;

	SDL_stack_free(clearValues);

	for (i = 0; i < colorAttachmentCount; i += 1)
	{
		vulkanCommandBuffer->renderPassColorTargets[i] =
			(VulkanRenderTarget*) colorAttachmentInfos[i].pRenderTarget;
	}
	vulkanCommandBuffer->renderPassColorTargetCount = colorAttachmentCount;
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

	if (	vulkanCommandBuffer->vertexUniformBuffer != renderer->dummyVertexUniformBuffer &&
		vulkanCommandBuffer->vertexUniformBuffer != NULL
	) {
		VULKAN_INTERNAL_BindUniformBuffer(
			vulkanCommandBuffer,
			vulkanCommandBuffer->vertexUniformBuffer
		);
	}
	vulkanCommandBuffer->vertexUniformBuffer = NULL;

	if (	vulkanCommandBuffer->fragmentUniformBuffer != renderer->dummyFragmentUniformBuffer &&
		vulkanCommandBuffer->fragmentUniformBuffer != NULL
	) {
		VULKAN_INTERNAL_BindUniformBuffer(
			vulkanCommandBuffer,
			vulkanCommandBuffer->fragmentUniformBuffer
		);
	}
	vulkanCommandBuffer->fragmentUniformBuffer = NULL;

	/* If the render targets can be sampled, transition them to sample layout */
	for (i = 0; i < vulkanCommandBuffer->renderPassColorTargetCount; i += 1)
	{
		currentTexture = vulkanCommandBuffer->renderPassColorTargets[i]->texture;

		if (currentTexture->usageFlags & VK_IMAGE_USAGE_SAMPLED_BIT)
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
	vulkanCommandBuffer->renderPassColorTargetCount = 0;

	vulkanCommandBuffer->currentGraphicsPipeline = NULL;
	vulkanCommandBuffer->renderPassInProgress = 0;
}

static void VULKAN_BindGraphicsPipeline(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_GraphicsPipeline *graphicsPipeline
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	VulkanGraphicsPipeline* pipeline = (VulkanGraphicsPipeline*) graphicsPipeline;

	if (	vulkanCommandBuffer->vertexUniformBuffer != renderer->dummyVertexUniformBuffer &&
		vulkanCommandBuffer->vertexUniformBuffer != NULL
	) {
		VULKAN_INTERNAL_BindUniformBuffer(
			vulkanCommandBuffer,
			vulkanCommandBuffer->vertexUniformBuffer
		);
	}

	if (pipeline->vertexUniformBlockSize == 0)
	{
		vulkanCommandBuffer->vertexUniformBuffer = renderer->dummyVertexUniformBuffer;
	}
	else
	{
		vulkanCommandBuffer->vertexUniformBuffer = VULKAN_INTERNAL_AcquireUniformBufferFromPool(
			renderer,
			renderer->vertexUniformBufferPool,
			pipeline->vertexUniformBlockSize
		);
	}

	if (	vulkanCommandBuffer->fragmentUniformBuffer != renderer->dummyFragmentUniformBuffer &&
		vulkanCommandBuffer->fragmentUniformBuffer != NULL
	) {
		VULKAN_INTERNAL_BindUniformBuffer(
			vulkanCommandBuffer,
			vulkanCommandBuffer->fragmentUniformBuffer
		);
	}

	if (pipeline->fragmentUniformBlockSize == 0)
	{
		vulkanCommandBuffer->fragmentUniformBuffer = renderer->dummyFragmentUniformBuffer;
	}
	else
	{
		vulkanCommandBuffer->fragmentUniformBuffer = VULKAN_INTERNAL_AcquireUniformBufferFromPool(
			renderer,
			renderer->fragmentUniformBufferPool,
			pipeline->fragmentUniformBlockSize
		);
	}

	/* bind dummy sets if necessary */
	if (pipeline->pipelineLayout->vertexSamplerDescriptorSetCache == NULL)
	{
		vulkanCommandBuffer->vertexSamplerDescriptorSet = renderer->emptyVertexSamplerDescriptorSet;
	}

	if (pipeline->pipelineLayout->fragmentSamplerDescriptorSetCache == NULL)
	{
		vulkanCommandBuffer->fragmentSamplerDescriptorSet = renderer->emptyFragmentSamplerDescriptorSet;
	}

	renderer->vkCmdBindPipeline(
		vulkanCommandBuffer->commandBuffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		pipeline->pipeline
	);

	vulkanCommandBuffer->currentGraphicsPipeline = pipeline;
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
	VulkanBuffer *currentVulkanBuffer;
	VkBuffer *buffers = SDL_stack_alloc(VkBuffer, bindingCount);
	uint32_t i;

	for (i = 0; i < bindingCount; i += 1)
	{
		currentVulkanBuffer = (VulkanBuffer*) pBuffers[i];
		buffers[i] = currentVulkanBuffer->buffer;
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

	renderer->vkCmdBindIndexBuffer(
		vulkanCommandBuffer->commandBuffer,
		vulkanBuffer->buffer,
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
		vulkanCommandBuffer->bufferDescriptorSet = renderer->emptyComputeBufferDescriptorSet;
	}

	if (vulkanComputePipeline->pipelineLayout->imageDescriptorSetCache == NULL)
	{
		vulkanCommandBuffer->imageDescriptorSet = renderer->emptyComputeImageDescriptorSet;
	}

	if (	vulkanCommandBuffer->computeUniformBuffer != renderer->dummyComputeUniformBuffer &&
		vulkanCommandBuffer->computeUniformBuffer != NULL
	) {
		VULKAN_INTERNAL_BindUniformBuffer(
			vulkanCommandBuffer,
			vulkanCommandBuffer->computeUniformBuffer
		);
	}
	vulkanCommandBuffer->computeUniformBuffer = NULL;

	renderer->vkCmdBindPipeline(
		vulkanCommandBuffer->commandBuffer,
		VK_PIPELINE_BIND_POINT_COMPUTE,
		vulkanComputePipeline->pipeline
	);

	vulkanCommandBuffer->currentComputePipeline = vulkanComputePipeline;

	if (vulkanComputePipeline->uniformBlockSize != 0)
	{
		vulkanCommandBuffer->computeUniformBuffer = VULKAN_INTERNAL_AcquireUniformBufferFromPool(
			renderer,
			renderer->computeUniformBufferPool,
			vulkanComputePipeline->uniformBlockSize
		);
	}
}

static void VULKAN_BindComputeBuffers(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Buffer **pBuffers
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	VulkanComputePipeline *computePipeline = vulkanCommandBuffer->currentComputePipeline;

	VulkanBuffer *currentVulkanBuffer;
	VkDescriptorBufferInfo descriptorBufferInfos[MAX_BUFFER_BINDINGS];
	uint32_t i;

	if (computePipeline->pipelineLayout->bufferDescriptorSetCache == NULL)
	{
		return;
	}

	for (i = 0; i < computePipeline->pipelineLayout->bufferDescriptorSetCache->bindingCount; i += 1)
	{
		currentVulkanBuffer = (VulkanBuffer*) pBuffers[i];

		descriptorBufferInfos[i].buffer = currentVulkanBuffer->buffer;
		descriptorBufferInfos[i].offset = 0;
		descriptorBufferInfos[i].range = currentVulkanBuffer->size;

		VULKAN_INTERNAL_BufferMemoryBarrier(
			renderer,
			vulkanCommandBuffer->commandBuffer,
			RESOURCE_ACCESS_COMPUTE_SHADER_READ_OTHER,
			currentVulkanBuffer
		);
	}

	vulkanCommandBuffer->bufferDescriptorSet =
		VULKAN_INTERNAL_FetchDescriptorSet(
			renderer,
			vulkanCommandBuffer,
			computePipeline->pipelineLayout->bufferDescriptorSetCache,
			NULL,
			descriptorBufferInfos
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
	VkDescriptorImageInfo descriptorImageInfos[MAX_TEXTURE_SAMPLERS];
	uint32_t i;

	if (computePipeline->pipelineLayout->imageDescriptorSetCache == NULL)
	{
		return;
	}

	for (i = 0; i < computePipeline->pipelineLayout->imageDescriptorSetCache->bindingCount; i += 1)
	{
		currentTexture = (VulkanTexture*) pTextures[i];
		descriptorImageInfos[i].imageView = currentTexture->view;
		descriptorImageInfos[i].sampler = VK_NULL_HANDLE;
		descriptorImageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			vulkanCommandBuffer->commandBuffer,
			RESOURCE_ACCESS_COMPUTE_SHADER_READ_SAMPLED_IMAGE_OR_UNIFORM_TEXEL_BUFFER,
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

	vulkanCommandBuffer->imageDescriptorSet =
		VULKAN_INTERNAL_FetchDescriptorSet(
			renderer,
			vulkanCommandBuffer,
			computePipeline->pipelineLayout->imageDescriptorSetCache,
			descriptorImageInfos,
			NULL
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
	VulkanCommandBuffer *commandBuffer;

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
		LogVulkanResultAsError("vkAllocateCommandBuffers", vulkanResult);
		SDL_stack_free(commandBuffers);
		return;
	}

	for (i = 0; i < allocateCount; i += 1)
	{
		commandBuffer = SDL_malloc(sizeof(VulkanCommandBuffer));
		commandBuffer->commandPool = vulkanCommandPool;
		commandBuffer->commandBuffer = commandBuffers[i];

		commandBuffer->present = 0;
		commandBuffer->presentWindowHandle = NULL;
		commandBuffer->presentSwapchainImageIndex = 0;
		commandBuffer->needNewSwapchain = 0;

		/* Transfer buffer tracking */

		commandBuffer->transferBufferCapacity = 4;
		commandBuffer->transferBufferCount = 0;
		commandBuffer->transferBuffers = SDL_malloc(
			commandBuffer->transferBufferCapacity * sizeof(VulkanTransferBuffer*)
		);

		/* Bound buffer tracking */

		commandBuffer->boundUniformBufferCapacity = 16;
		commandBuffer->boundUniformBufferCount = 0;
		commandBuffer->boundUniformBuffers = SDL_malloc(
			commandBuffer->boundUniformBufferCapacity * sizeof(VulkanUniformBuffer*)
		);

		/* Descriptor set tracking */

		commandBuffer->boundDescriptorSetDataCapacity = 16;
		commandBuffer->boundDescriptorSetDataCount = 0;
		commandBuffer->boundDescriptorSetDatas = SDL_malloc(
			commandBuffer->boundDescriptorSetDataCapacity * sizeof(DescriptorSetData)
		);

		/* Deferred destroy storage */

		commandBuffer->renderTargetsToDestroyCapacity = 16;
		commandBuffer->renderTargetsToDestroyCount = 0;

		commandBuffer->renderTargetsToDestroy = (VulkanRenderTarget**) SDL_malloc(
			sizeof(VulkanRenderTarget*) *
			commandBuffer->renderTargetsToDestroyCapacity
		);

		commandBuffer->texturesToDestroyCapacity = 16;
		commandBuffer->texturesToDestroyCount = 0;

		commandBuffer->texturesToDestroy = (VulkanTexture**)SDL_malloc(
			sizeof(VulkanTexture*) *
			commandBuffer->texturesToDestroyCapacity
		);

		commandBuffer->buffersToDestroyCapacity = 16;
		commandBuffer->buffersToDestroyCount = 0;

		commandBuffer->buffersToDestroy = (VulkanBuffer**) SDL_malloc(
			sizeof(VulkanBuffer*) *
			commandBuffer->buffersToDestroyCapacity
		);

		commandBuffer->graphicsPipelinesToDestroyCapacity = 16;
		commandBuffer->graphicsPipelinesToDestroyCount = 0;

		commandBuffer->graphicsPipelinesToDestroy = (VulkanGraphicsPipeline**) SDL_malloc(
			sizeof(VulkanGraphicsPipeline*) *
			commandBuffer->graphicsPipelinesToDestroyCapacity
		);

		commandBuffer->computePipelinesToDestroyCapacity = 16;
		commandBuffer->computePipelinesToDestroyCount = 0;

		commandBuffer->computePipelinesToDestroy = (VulkanComputePipeline**) SDL_malloc(
			sizeof(VulkanComputePipeline*) *
			commandBuffer->computePipelinesToDestroyCapacity
		);

		commandBuffer->shaderModulesToDestroyCapacity = 16;
		commandBuffer->shaderModulesToDestroyCount = 0;

		commandBuffer->shaderModulesToDestroy = (VkShaderModule*) SDL_malloc(
			sizeof(VkShaderModule) *
			commandBuffer->shaderModulesToDestroyCapacity
		);

		commandBuffer->samplersToDestroyCapacity = 16;
		commandBuffer->samplersToDestroyCount = 0;

		commandBuffer->samplersToDestroy = (VkSampler*) SDL_malloc(
			sizeof(VkSampler) *
			commandBuffer->samplersToDestroyCapacity
		);

		vulkanCommandPool->inactiveCommandBuffers[
			vulkanCommandPool->inactiveCommandBufferCount
		] = commandBuffer;
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
		LogVulkanResultAsError("vkCreateCommandPool", vulkanResult);
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
	VkResult result;

	SDL_threadID threadID = SDL_ThreadID();

	SDL_LockMutex(renderer->acquireCommandBufferLock);

	VulkanCommandBuffer *commandBuffer =
		VULKAN_INTERNAL_GetInactiveCommandBufferFromPool(renderer, threadID);

	SDL_UnlockMutex(renderer->acquireCommandBufferLock);

	/* Reset state */

	commandBuffer->currentComputePipeline = NULL;
	commandBuffer->currentGraphicsPipeline = NULL;

	commandBuffer->vertexUniformBuffer = NULL;
	commandBuffer->fragmentUniformBuffer = NULL;
	commandBuffer->computeUniformBuffer = NULL;

	commandBuffer->fixed = fixed;
	commandBuffer->submitted = 0;
	commandBuffer->present = 0;

	commandBuffer->renderPassInProgress = 0;
	commandBuffer->renderPassColorTargetCount = 0;

	result = renderer->vkResetCommandBuffer(
		commandBuffer->commandBuffer,
		VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResultAsError("vkResetCommandBuffer", result);
	}

	VULKAN_INTERNAL_BeginCommandBuffer(renderer, commandBuffer);

	return (Refresh_CommandBuffer*) commandBuffer;
}

static void VULKAN_QueuePresent(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_TextureSlice *textureSlice,
	Refresh_Rect *destinationRectangle,
	Refresh_Filter filter,
	void *windowHandle
) {
	VkResult acquireResult;
	Refresh_Rect dstRect;

	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	Refresh_TextureSlice destinationTextureSlice;
	VulkanSwapchainData *swapchainData = NULL;
	CreateSwapchainResult createSwapchainResult = 0;
	uint8_t validSwapchainExists = 1;
	uint8_t acquireSuccess = 0;
	uint32_t swapchainImageIndex;

	if (vulkanCommandBuffer->present)
	{
		Refresh_LogError("This command buffer already has a present queued!");
		return;
	}

	vulkanCommandBuffer->presentWindowHandle = windowHandle;

	swapchainData = (VulkanSwapchainData*) SDL_GetWindowData(windowHandle, WINDOW_SWAPCHAIN_DATA);

	if (swapchainData == NULL)
	{
		createSwapchainResult = VULKAN_INTERNAL_CreateSwapchain(renderer, windowHandle);

		if (createSwapchainResult == CREATE_SWAPCHAIN_FAIL)
		{
			Refresh_LogError("Failed to create swapchain for window handle: %p", windowHandle);
			validSwapchainExists = 0;
		}
		else if (createSwapchainResult == CREATE_SWAPCHAIN_SURFACE_ZERO)
		{
			Refresh_LogInfo("Surface for window handle: %p is size zero, cancelling present", windowHandle);
			validSwapchainExists = 0;
		}
		else
		{
			swapchainData = (VulkanSwapchainData*) SDL_GetWindowData(windowHandle, WINDOW_SWAPCHAIN_DATA);
		}
	}

	if (validSwapchainExists)
	{
		acquireResult = renderer->vkAcquireNextImageKHR(
			renderer->logicalDevice,
			swapchainData->swapchain,
			UINT64_MAX,
			swapchainData->imageAvailableSemaphore,
			VK_NULL_HANDLE,
			&swapchainImageIndex
		);

		if (acquireResult == VK_SUCCESS || acquireResult == VK_SUBOPTIMAL_KHR)
		{
			if (destinationRectangle != NULL)
			{
				dstRect = *destinationRectangle;
			}
			else
			{
				dstRect.x = 0;
				dstRect.y = 0;
				dstRect.w = swapchainData->extent.width;
				dstRect.h = swapchainData->extent.height;
			}

			/* Blit! */

			destinationTextureSlice.depth = 0;
			destinationTextureSlice.layer = 0;
			destinationTextureSlice.level = 0;
			destinationTextureSlice.rectangle = dstRect;
			destinationTextureSlice.texture = (Refresh_Texture*) &swapchainData->textures[swapchainImageIndex];

			VULKAN_INTERNAL_BlitImage(
				renderer,
				vulkanCommandBuffer->commandBuffer,
				textureSlice,
				&destinationTextureSlice,
				RESOURCE_ACCESS_PRESENT,
				RefreshToVK_Filter[filter]
			);

			acquireSuccess = 1;
		}
	}

	if (acquireSuccess)
	{
		vulkanCommandBuffer->present = 1;
		vulkanCommandBuffer->presentSwapchainImageIndex = swapchainImageIndex;
	}

	if (!validSwapchainExists || acquireResult == VK_SUBOPTIMAL_KHR)
	{
		vulkanCommandBuffer->needNewSwapchain = 1;
	}
}

/* Synchronization management */

static VkFence VULKAN_INTERNAL_AcquireFence(
	VulkanRenderer *renderer
) {
	VkFenceCreateInfo fenceCreateInfo;
	VkFence fence;
	VkResult fenceCreateResult;

	SDL_LockMutex(renderer->acquireFenceLock);

	if (renderer->availableFenceCount == 0)
	{
		fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceCreateInfo.pNext = NULL;
		fenceCreateInfo.flags = 0;

		fenceCreateResult = renderer->vkCreateFence(
			renderer->logicalDevice,
			&fenceCreateInfo,
			NULL,
			&renderer->availableFences[0]
		);

		if (fenceCreateResult != VK_SUCCESS)
		{
			SDL_UnlockMutex(renderer->acquireFenceLock);
			LogVulkanResultAsError("vkCreateFence", fenceCreateResult);
			return VK_NULL_HANDLE;
		}

		renderer->availableFenceCount += 1;
	}

	fence = renderer->availableFences[renderer->availableFenceCount - 1];
	renderer->availableFenceCount -= 1;

	renderer->vkResetFences(
		renderer->logicalDevice,
		1,
		&fence
	);

	SDL_UnlockMutex(renderer->acquireFenceLock);

	return fence;
}

/* Submission structure */

static void VULKAN_INTERNAL_CleanCommandBuffer(
	VulkanRenderer *renderer,
	VulkanCommandBuffer *commandBuffer
) {
	uint32_t i;
	VulkanUniformBuffer *uniformBuffer;
	DescriptorSetData *descriptorSetData;

	/* Bound uniform buffers are now available */

	for (i = 0; i < commandBuffer->boundUniformBufferCount; i += 1)
	{
		uniformBuffer = commandBuffer->boundUniformBuffers[i];

		SDL_LockMutex(uniformBuffer->pool->lock);
		if (uniformBuffer->pool->availableBufferCount == uniformBuffer->pool->availableBufferCapacity)
		{
			uniformBuffer->pool->availableBufferCapacity *= 2;
			uniformBuffer->pool->availableBuffers = SDL_realloc(
				uniformBuffer->pool->availableBuffers,
				uniformBuffer->pool->availableBufferCapacity * sizeof(VulkanUniformBuffer*)
			);
		}

		uniformBuffer->pool->availableBuffers[uniformBuffer->pool->availableBufferCount] = uniformBuffer;
		uniformBuffer->pool->availableBufferCount += 1;
		SDL_UnlockMutex(uniformBuffer->pool->lock);
	}

	commandBuffer->boundUniformBufferCount = 0;

	SDL_LockMutex(renderer->transferBufferPool.lock);

	if (renderer->transferBufferPool.availableBufferCount + commandBuffer->transferBufferCount >= renderer->transferBufferPool.availableBufferCapacity)
	{
		renderer->transferBufferPool.availableBufferCapacity = renderer->transferBufferPool.availableBufferCount + commandBuffer->transferBufferCount;
		renderer->transferBufferPool.availableBuffers = SDL_realloc(
			renderer->transferBufferPool.availableBuffers,
			renderer->transferBufferPool.availableBufferCapacity * sizeof(VulkanTransferBuffer*)
		);
	}

	for (i = 0; i < commandBuffer->transferBufferCount; i += 1)
	{
		commandBuffer->transferBuffers[i]->offset = 0;
		renderer->transferBufferPool.availableBuffers[renderer->transferBufferPool.availableBufferCount] = commandBuffer->transferBuffers[i];
		renderer->transferBufferPool.availableBufferCount += 1;
	}

	SDL_UnlockMutex(renderer->transferBufferPool.lock);

	commandBuffer->transferBufferCount = 0;

	/* Bound descriptor sets are now available */

	for (i = 0; i < commandBuffer->boundDescriptorSetDataCount; i += 1)
	{
		descriptorSetData = &commandBuffer->boundDescriptorSetDatas[i];

		SDL_LockMutex(descriptorSetData->descriptorSetCache->lock);

		if (descriptorSetData->descriptorSetCache->inactiveDescriptorSetCount == descriptorSetData->descriptorSetCache->inactiveDescriptorSetCapacity)
		{
			descriptorSetData->descriptorSetCache->inactiveDescriptorSetCapacity *= 2;
			descriptorSetData->descriptorSetCache->inactiveDescriptorSets = SDL_realloc(
				descriptorSetData->descriptorSetCache->inactiveDescriptorSets,
				descriptorSetData->descriptorSetCache->inactiveDescriptorSetCapacity * sizeof(VkDescriptorSet)
			);
		}

		descriptorSetData->descriptorSetCache->inactiveDescriptorSets[descriptorSetData->descriptorSetCache->inactiveDescriptorSetCount] = descriptorSetData->descriptorSet;
		descriptorSetData->descriptorSetCache->inactiveDescriptorSetCount += 1;

		SDL_UnlockMutex(descriptorSetData->descriptorSetCache->lock);
	}

	commandBuffer->boundDescriptorSetDataCount = 0;

	/* Perform pending destroys */

	for (i = 0; i < commandBuffer->renderTargetsToDestroyCount; i += 1)
	{
		VULKAN_INTERNAL_DestroyRenderTarget(
			renderer,
			commandBuffer->renderTargetsToDestroy[i]
		);
	}
	commandBuffer->renderTargetsToDestroyCount = 0;

	for (i = 0; i < commandBuffer->texturesToDestroyCount; i += 1)
	{
		VULKAN_INTERNAL_DestroyTexture(
			renderer,
			commandBuffer->texturesToDestroy[i]
		);
	}
	commandBuffer->texturesToDestroyCount = 0;

	for (i = 0; i < commandBuffer->buffersToDestroyCount; i += 1)
	{
		VULKAN_INTERNAL_DestroyBuffer(
			renderer,
			commandBuffer->buffersToDestroy[i]
		);
	}
	commandBuffer->buffersToDestroyCount = 0;

	for (i = 0; i < commandBuffer->graphicsPipelinesToDestroyCount; i += 1)
	{
		VULKAN_INTERNAL_DestroyGraphicsPipeline(
			renderer,
			commandBuffer->graphicsPipelinesToDestroy[i]
		);
	}
	commandBuffer->graphicsPipelinesToDestroyCount = 0;

	for (i = 0; i < commandBuffer->computePipelinesToDestroyCount; i += 1)
	{
		VULKAN_INTERNAL_DestroyComputePipeline(
			renderer,
			commandBuffer->computePipelinesToDestroy[i]
		);
	}
	commandBuffer->computePipelinesToDestroyCount = 0;

	for (i = 0; i < commandBuffer->shaderModulesToDestroyCount; i += 1)
	{
		VULKAN_INTERNAL_DestroyShaderModule(
			renderer,
			commandBuffer->shaderModulesToDestroy[i]
		);
	}
	commandBuffer->shaderModulesToDestroyCount = 0;

	for (i = 0; i < commandBuffer->samplersToDestroyCount; i += 1)
	{
		VULKAN_INTERNAL_DestroySampler(
			renderer,
			commandBuffer->samplersToDestroy[i]
		);
	}
	commandBuffer->samplersToDestroyCount = 0;

	SDL_LockMutex(renderer->acquireCommandBufferLock);

	if (commandBuffer->commandPool->inactiveCommandBufferCount == commandBuffer->commandPool->inactiveCommandBufferCapacity)
	{
		commandBuffer->commandPool->inactiveCommandBufferCapacity += 1;
		commandBuffer->commandPool->inactiveCommandBuffers = SDL_realloc(
			commandBuffer->commandPool->inactiveCommandBuffers,
			commandBuffer->commandPool->inactiveCommandBufferCapacity * sizeof(VulkanCommandBuffer*)
		);
	}

	commandBuffer->commandPool->inactiveCommandBuffers[
		commandBuffer->commandPool->inactiveCommandBufferCount
	] = commandBuffer;
	commandBuffer->commandPool->inactiveCommandBufferCount += 1;

	SDL_UnlockMutex(renderer->acquireCommandBufferLock);

	SDL_LockMutex(renderer->acquireFenceLock);

	if (renderer->availableFenceCount == renderer->availableFenceCapacity)
	{
		renderer->availableFenceCapacity *= 2;
		renderer->availableFences = SDL_realloc(
			renderer->availableFences,
			renderer->availableFenceCapacity * sizeof(VkFence)
		);
	}

	renderer->availableFences[renderer->availableFenceCount] = commandBuffer->inFlightFence;
	renderer->availableFenceCount += 1;

	SDL_UnlockMutex(renderer->acquireFenceLock);

	commandBuffer->inFlightFence = VK_NULL_HANDLE;

	/* Remove this command buffer from the submitted list */
	for (i = 0; i < renderer->submittedCommandBufferCount; i += 1)
	{
		if (renderer->submittedCommandBuffers[i] == commandBuffer)
		{
			renderer->submittedCommandBuffers[i] = renderer->submittedCommandBuffers[renderer->submittedCommandBufferCount - 1];
			renderer->submittedCommandBufferCount -= 1;
		}
	}
}

static void VULKAN_Wait(
	Refresh_Renderer *driverData
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *commandBuffer;
	VkResult result;
	int32_t i;

	for (i = renderer->submittedCommandBufferCount - 1; i >= 0; i -= 1)
	{
		commandBuffer = renderer->submittedCommandBuffers[i];

		result = renderer->vkWaitForFences(
			renderer->logicalDevice,
			1,
			&commandBuffer->inFlightFence,
			VK_TRUE,
			UINT64_MAX
		);

		if (result != VK_SUCCESS)
		{
			LogVulkanResultAsError("vkWaitForFences", result);
		}

		VULKAN_INTERNAL_CleanCommandBuffer(renderer, commandBuffer);
	}
}

static void VULKAN_Submit(
	Refresh_Renderer *driverData,
	uint32_t commandBufferCount,
	Refresh_CommandBuffer **pCommandBuffers
) {
	VulkanRenderer* renderer = (VulkanRenderer*)driverData;
	VkSubmitInfo *submitInfos;
	VkPresentInfoKHR *presentInfos;
	void **presentWindowHandles;
	uint8_t *needsNewSwapchain;
	uint32_t presentCount = 0;
	VkResult vulkanResult, presentResult = VK_SUCCESS;
	VulkanCommandBuffer *currentCommandBuffer;
	VkCommandBuffer *commandBuffers;
	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VulkanSwapchainData *swapchainData;
	VkFence fence;
	int32_t i;

	SDL_LockMutex(renderer->submitLock);

	commandBuffers = SDL_stack_alloc(VkCommandBuffer, commandBufferCount);
	submitInfos = SDL_stack_alloc(VkSubmitInfo, commandBufferCount);

	/* In the worst case we will need 1 semaphore per CB so let's allocate appropriately. */
	presentInfos = SDL_stack_alloc(VkPresentInfoKHR, commandBufferCount);
	presentWindowHandles = SDL_stack_alloc(void*, commandBufferCount);
	needsNewSwapchain = SDL_stack_alloc(uint8_t, commandBufferCount);

	for (i = 0; i < commandBufferCount; i += 1)
	{
		currentCommandBuffer = (VulkanCommandBuffer*)pCommandBuffers[i];
		VULKAN_INTERNAL_EndCommandBuffer(renderer, currentCommandBuffer);
		commandBuffers[i] = currentCommandBuffer->commandBuffer;

		submitInfos[i].sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfos[i].pNext = NULL;
		submitInfos[i].commandBufferCount = 1;
		submitInfos[i].pCommandBuffers = &commandBuffers[i];

		if (currentCommandBuffer->present)
		{
			swapchainData = (VulkanSwapchainData*) SDL_GetWindowData(currentCommandBuffer->presentWindowHandle, WINDOW_SWAPCHAIN_DATA);

			submitInfos[i].pWaitDstStageMask = &waitStage;
			submitInfos[i].pWaitSemaphores = &swapchainData->imageAvailableSemaphore;
			submitInfos[i].waitSemaphoreCount = 1;
			submitInfos[i].pSignalSemaphores = &swapchainData->renderFinishedSemaphore;
			submitInfos[i].signalSemaphoreCount = 1;

			presentInfos[presentCount].sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
			presentInfos[presentCount].pNext = NULL;
			presentInfos[presentCount].waitSemaphoreCount = 1;
			presentInfos[presentCount].pWaitSemaphores = &swapchainData->renderFinishedSemaphore;

			presentInfos[presentCount].swapchainCount = 1;
			presentInfos[presentCount].pSwapchains = &swapchainData->swapchain;
			presentInfos[presentCount].pImageIndices = &currentCommandBuffer->presentSwapchainImageIndex;
			presentInfos[presentCount].pResults = NULL;

			presentWindowHandles[presentCount] = currentCommandBuffer->presentWindowHandle;
			needsNewSwapchain[presentCount] = currentCommandBuffer->needNewSwapchain;

			presentCount += 1;
		}
		else
		{
			submitInfos[i].pWaitDstStageMask = NULL;
			submitInfos[i].pWaitSemaphores = NULL;
			submitInfos[i].waitSemaphoreCount = 0;
			submitInfos[i].pSignalSemaphores = NULL;
			submitInfos[i].signalSemaphoreCount = 0;
		}
	}

	/* Wait for any previous submissions on swapchains */
	for (i = 0; i < commandBufferCount; i += 1)
	{
		currentCommandBuffer = (VulkanCommandBuffer*)pCommandBuffers[i];

		if (currentCommandBuffer->present)
		{
			swapchainData = (VulkanSwapchainData*) SDL_GetWindowData(currentCommandBuffer->presentWindowHandle, WINDOW_SWAPCHAIN_DATA);

			if (swapchainData->inFlightFence != VK_NULL_HANDLE)
			{
				vulkanResult = renderer->vkWaitForFences(
					renderer->logicalDevice,
					1,
					&swapchainData->inFlightFence,
					VK_TRUE,
					UINT64_MAX
				);

				if (vulkanResult != VK_SUCCESS)
				{
					SDL_UnlockMutex(renderer->submitLock);
					LogVulkanResultAsError("vkWaitForFences", vulkanResult);
					return;
				}

				swapchainData->inFlightFence = VK_NULL_HANDLE;
			}
		}
	}

	/* Check if we can perform any cleanups */
	for (i = renderer->submittedCommandBufferCount - 1; i >= 0; i -= 1)
	{
		/* If we set a timeout of 0, we can query the command buffer state */
		vulkanResult = renderer->vkWaitForFences(
			renderer->logicalDevice,
			1,
			&renderer->submittedCommandBuffers[i]->inFlightFence,
			VK_TRUE,
			0
		);

		if (vulkanResult == VK_SUCCESS)
		{
			VULKAN_INTERNAL_CleanCommandBuffer(
				renderer,
				renderer->submittedCommandBuffers[i]
			);
		}
	}

	/* Acquire a fence */
	fence = VULKAN_INTERNAL_AcquireFence(renderer);

	if (fence == VK_NULL_HANDLE)
	{
		SDL_UnlockMutex(renderer->submitLock);
		Refresh_LogError("Failed to acquire fence!");
		return;
	}

	/* Submit the commands, finally. */
	vulkanResult = renderer->vkQueueSubmit(
		renderer->graphicsQueue,
		commandBufferCount,
		submitInfos,
		fence
	);

	if (vulkanResult != VK_SUCCESS)
	{
		SDL_UnlockMutex(renderer->submitLock);
		LogVulkanResultAsError("vkQueueSubmit", vulkanResult);
		return;
	}

	for (i = 0; i < commandBufferCount; i += 1)
	{
		currentCommandBuffer = (VulkanCommandBuffer*)pCommandBuffers[i];
		currentCommandBuffer->inFlightFence = fence;

		if (currentCommandBuffer->present)
		{
			swapchainData = (VulkanSwapchainData*) SDL_GetWindowData(currentCommandBuffer->presentWindowHandle, WINDOW_SWAPCHAIN_DATA);
			swapchainData->inFlightFence = fence;
		}
	}

	if (renderer->submittedCommandBufferCount + commandBufferCount >= renderer->submittedCommandBufferCapacity)
	{
		renderer->submittedCommandBufferCapacity = renderer->submittedCommandBufferCount + commandBufferCount;

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
		renderer->submittedCommandBufferCount += 1;
	}

	/* Present, if applicable */

	for (i = 0; i < presentCount; i += 1)
	{
		presentResult = renderer->vkQueuePresentKHR(
			renderer->presentQueue,
			&presentInfos[i]
		);

		if (presentResult != VK_SUCCESS || needsNewSwapchain[i])
		{
			VULKAN_INTERNAL_RecreateSwapchain(renderer, presentWindowHandles[i]);
		}
	}

	SDL_stack_free(commandBuffers);
	SDL_stack_free(submitInfos);
	SDL_stack_free(presentInfos);
	SDL_stack_free(presentWindowHandles);
	SDL_stack_free(needsNewSwapchain);

	SDL_UnlockMutex(renderer->submitLock);
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
		else
		{
			Refresh_LogInfo("Validation layers enabled, expect debug level performance!");
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
	uint8_t *deviceRank
) {
	uint32_t queueFamilyCount, i;
	SwapChainSupportDetails swapChainSupportDetails;
	VkQueueFamilyProperties *queueProps;
	VkBool32 supportsPresent;
	uint8_t querySuccess = 0;
	uint8_t foundSuitableDevice = 0;
	VkPhysicalDeviceProperties deviceProperties;

	queueFamilyIndices->graphicsFamily = UINT32_MAX;
	queueFamilyIndices->presentFamily = UINT32_MAX;
	queueFamilyIndices->computeFamily = UINT32_MAX;
	queueFamilyIndices->transferFamily = UINT32_MAX;
	*deviceRank = 0;

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
		UINT32_MAX,
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

		if (	supportsPresent &&
			(queueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
			(queueProps[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
			(queueProps[i].queueFlags & VK_QUEUE_TRANSFER_BIT)	)
		{
			queueFamilyIndices->graphicsFamily = i;
			queueFamilyIndices->presentFamily = i;
			queueFamilyIndices->computeFamily = i;
			queueFamilyIndices->transferFamily = i;
			foundSuitableDevice = 1;
			break;
		}
	}

	SDL_stack_free(queueProps);

	if (foundSuitableDevice)
	{
		/* Try to make sure we pick the best device available */
		renderer->vkGetPhysicalDeviceProperties(
			physicalDevice,
			&deviceProperties
		);
		*deviceRank = DEVICE_PRIORITY[deviceProperties.deviceType];
		return 1;
	}

	/* This device is useless for us, next! */
	return 0;
}

static void VULKAN_INTERNAL_GetPhysicalDeviceProperties(
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

	renderer->vkGetPhysicalDeviceMemoryProperties(
		renderer->physicalDevice,
		&renderer->memoryProperties
	);
}

static uint8_t VULKAN_INTERNAL_DeterminePhysicalDevice(
	VulkanRenderer *renderer,
	VkSurfaceKHR surface,
	const char **deviceExtensionNames,
	uint32_t deviceExtensionCount
) {
	VkResult vulkanResult;
	VkPhysicalDevice *physicalDevices;
	uint32_t physicalDeviceCount, i, suitableIndex;
	QueueFamilyIndices queueFamilyIndices, suitableQueueFamilyIndices;
	uint8_t deviceRank, highestRank;

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
	deviceRank = 0;
	highestRank = 0;
	for (i = 0; i < physicalDeviceCount; i += 1)
	{
		const uint8_t suitable = VULKAN_INTERNAL_IsDeviceSuitable(
			renderer,
			physicalDevices[i],
			deviceExtensionNames,
			deviceExtensionCount,
			surface,
			&queueFamilyIndices,
			&deviceRank
		);

		if (deviceRank >= highestRank)
		{
			if (suitable)
			{
				suitableIndex = i;
				suitableQueueFamilyIndices.computeFamily = queueFamilyIndices.computeFamily;
				suitableQueueFamilyIndices.graphicsFamily = queueFamilyIndices.graphicsFamily;
				suitableQueueFamilyIndices.presentFamily = queueFamilyIndices.presentFamily;
				suitableQueueFamilyIndices.transferFamily = queueFamilyIndices.transferFamily;
			}
			else if (deviceRank > highestRank)
			{
				/* In this case, we found a... "realer?" GPU,
				 * but it doesn't actually support our Vulkan.
				 * We should disqualify all devices below as a
				 * result, because if we don't we end up
				 * ignoring real hardware and risk using
				 * something like LLVMpipe instead!
				 * -flibit
				 */
				suitableIndex = -1;
			}
			highestRank = deviceRank;
		}
	}

	if (suitableIndex != -1)
	{
		renderer->physicalDevice = physicalDevices[suitableIndex];
		renderer->queueFamilyIndices = suitableQueueFamilyIndices;
	}
	else
	{
		Refresh_LogError("No suitable physical devices found");
		SDL_stack_free(physicalDevices);
		return 0;
	}

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

static Refresh_Device* VULKAN_CreateDevice(
	Refresh_PresentationParameters *presentationParameters,
	uint8_t debugMode
) {
	VulkanRenderer *renderer = (VulkanRenderer*) SDL_malloc(sizeof(VulkanRenderer));
	VkSurfaceKHR surface;

	Refresh_Device *result;
	VkResult vulkanResult;
	uint32_t i;

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

	VULKAN_INTERNAL_LoadEntryPoints(renderer);

	renderer->presentMode = presentationParameters->presentMode;
	renderer->debugMode = debugMode;

	/* Create the VkInstance */
	if (!VULKAN_INTERNAL_CreateInstance(renderer, presentationParameters->deviceWindowHandle))
	{
		Refresh_LogError("Error creating vulkan instance");
		return NULL;
	}

	/*
	 * Create the WSI vkSurface
	 */

	if (!SDL_Vulkan_CreateSurface(
		(SDL_Window*)presentationParameters->deviceWindowHandle,
		renderer->instance,
		&surface
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
		surface,
		deviceExtensionNames,
		deviceExtensionCount
	)) {
		Refresh_LogError("Failed to determine a suitable physical device");
		return NULL;
	}

	renderer->vkDestroySurfaceKHR(
		renderer->instance,
		surface,
		NULL
	);

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
		"\n"
		"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"
		"! Refresh Vulkan is still in development!	!\n"
		"! The API is unstable and subject to change	!\n"
		"! You have been warned!			!\n"
		"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
	);

	if (!VULKAN_INTERNAL_CreateLogicalDevice(
		renderer,
		deviceExtensionNames,
		deviceExtensionCount
	)) {
		Refresh_LogError("Failed to create logical device");
		return NULL;
	}

	/* FIXME: just move this into this function */
	result = (Refresh_Device*) SDL_malloc(sizeof(Refresh_Device));
	ASSIGN_DRIVER(VULKAN)

	result->driverData = (Refresh_Renderer*) renderer;

	/*
	 * Create initial swapchain
	 */

	renderer->swapchainDataCapacity = 1;
	renderer->swapchainDataCount = 0;
	renderer->swapchainDatas = SDL_malloc(
		renderer->swapchainDataCapacity * sizeof(VulkanSwapchainData*)
	);

	if (VULKAN_INTERNAL_CreateSwapchain(renderer, presentationParameters->deviceWindowHandle) != CREATE_SWAPCHAIN_SUCCESS)
	{
		Refresh_LogError("Failed to create swap chain");
		return NULL;
	}

	/* Threading */

	renderer->allocatorLock = SDL_CreateMutex();
	renderer->submitLock = SDL_CreateMutex();
	renderer->acquireFenceLock = SDL_CreateMutex();
	renderer->acquireCommandBufferLock = SDL_CreateMutex();
	renderer->renderPassFetchLock = SDL_CreateMutex();
	renderer->framebufferFetchLock = SDL_CreateMutex();

	/* Create fence lists */

	renderer->availableFenceCount = 0;
	renderer->availableFenceCapacity = 4;
	renderer->availableFences = SDL_malloc(renderer->availableFenceCapacity * sizeof(VkFence));

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

	/* Set up UBO layouts */

	renderer->minUBOAlignment = renderer->physicalDeviceProperties.properties.limits.minUniformBufferOffsetAlignment;

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
	emptyComputeImageDescriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
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
		&renderer->vertexUniformDescriptorSetLayout
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
		&renderer->fragmentUniformDescriptorSetLayout
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
		&renderer->computeUniformDescriptorSetLayout
	);

	/* Default Descriptors */

	poolSizes[0].descriptorCount = 2;
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

	poolSizes[1].descriptorCount = 1;
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

	poolSizes[2].descriptorCount = 1;
	poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

	poolSizes[3].descriptorCount = 3;
	poolSizes[3].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;

	defaultDescriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	defaultDescriptorPoolInfo.pNext = NULL;
	defaultDescriptorPoolInfo.flags = 0;
	defaultDescriptorPoolInfo.maxSets = 2 + 1 + 1 + 3;
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

	/* Dummy Uniform Buffers */

	renderer->dummyVertexUniformBuffer = VULKAN_INTERNAL_CreateDummyUniformBuffer(
		renderer,
		UNIFORM_BUFFER_VERTEX
	);

	if (renderer->dummyVertexUniformBuffer == NULL)
	{
		Refresh_LogError("Failed to create dummy vertex uniform buffer!");
		return NULL;
	}

	renderer->dummyFragmentUniformBuffer = VULKAN_INTERNAL_CreateDummyUniformBuffer(
		renderer,
		UNIFORM_BUFFER_FRAGMENT
	);

	if (renderer->dummyFragmentUniformBuffer == NULL)
	{
		Refresh_LogError("Failed to create dummy fragment uniform buffer!");
		return NULL;
	}

	renderer->dummyComputeUniformBuffer = VULKAN_INTERNAL_CreateDummyUniformBuffer(
		renderer,
		UNIFORM_BUFFER_COMPUTE
	);

	if (renderer->dummyComputeUniformBuffer == NULL)
	{
		Refresh_LogError("Failed to create dummy compute uniform buffer!");
		return NULL;
	}

	/* Initialize uniform buffer pools */

	renderer->vertexUniformBufferPool = VULKAN_INTERNAL_CreateUniformBufferPool(
		renderer,
		UNIFORM_BUFFER_VERTEX
	);

	renderer->fragmentUniformBufferPool = VULKAN_INTERNAL_CreateUniformBufferPool(
		renderer,
		UNIFORM_BUFFER_FRAGMENT
	);

	renderer->computeUniformBufferPool = VULKAN_INTERNAL_CreateUniformBufferPool(
		renderer,
		UNIFORM_BUFFER_COMPUTE
	);

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

	renderer->renderPassHashArray.elements = NULL;
	renderer->renderPassHashArray.count = 0;
	renderer->renderPassHashArray.capacity = 0;

	renderer->framebufferHashArray.elements = NULL;
	renderer->framebufferHashArray.count = 0;
	renderer->framebufferHashArray.capacity = 0;

	/* Initialize transfer buffer pool */
	renderer->transferBufferPool.lock = SDL_CreateMutex();

	renderer->transferBufferPool.availableBufferCapacity = 4;
	renderer->transferBufferPool.availableBufferCount = 0;
	renderer->transferBufferPool.availableBuffers = SDL_malloc(renderer->transferBufferPool.availableBufferCapacity * sizeof(VulkanTransferBuffer*));

	return result;
}

Refresh_Driver VulkanDriver = {
	"Vulkan",
	VULKAN_CreateDevice
};

#endif //REFRESH_DRIVER_VULKAN
