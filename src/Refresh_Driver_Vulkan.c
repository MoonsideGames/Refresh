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

/* Needed for VK_KHR_portability_subset */
#define VK_ENABLE_BETA_EXTENSIONS

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

typedef struct VulkanExtensions
{
	/* Globally supported */
	uint8_t KHR_swapchain;
	/* Core since 1.1 */
	uint8_t KHR_maintenance1;
	uint8_t KHR_get_memory_requirements2;

	/* Core since 1.2 */
	uint8_t KHR_driver_properties;
	/* EXT, probably not going to be Core */
	uint8_t EXT_vertex_attribute_divisor;
	/* Only required for special implementations (i.e. MoltenVK) */
	uint8_t KHR_portability_subset;
} VulkanExtensions;

/* Defines */

#define STARTING_ALLOCATION_SIZE 64000000 	    /* 64MB */
#define MAX_ALLOCATION_SIZE 256000000 		    /* 256MB */
#define ALLOCATION_INCREMENT 16000000           /* 16MB */
#define TRANSFER_BUFFER_STARTING_SIZE 8000000 	/* 8MB */
#define POOLED_TRANSFER_BUFFER_SIZE 16000000    /* 16MB */
#define UBO_BUFFER_SIZE 16777216 				/* 16MB */
#define UBO_SECTION_SIZE 4096 			        /* 4KB */
#define DESCRIPTOR_POOL_STARTING_SIZE 128
#define DEFRAG_TIME 200
#define WINDOW_DATA "Refresh_VulkanWindowData"

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
	{								\
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
	RESOURCE_ACCESS_INDIRECT_BUFFER,
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
	RESOURCE_ACCESS_COMPUTE_SHADER_STORAGE_IMAGE_READ_WRITE,
	RESOURCE_ACCESS_COMPUTE_SHADER_BUFFER_READ_WRITE,
	RESOURCE_ACCESS_TRANSFER_READ_WRITE,
	RESOURCE_ACCESS_GENERAL,

	/* Count */
	RESOURCE_ACCESS_TYPES_COUNT
} VulkanResourceAccessType;

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
	VK_FORMAT_R8G8B8A8_UNORM,			/* R8G8B8A8_UNORM */
	VK_FORMAT_B8G8R8A8_UNORM,			/* B8G8R8A8_UNORM */
	VK_FORMAT_R5G6B5_UNORM_PACK16,		/* R5G6B5_UNORM */
	VK_FORMAT_A1R5G5B5_UNORM_PACK16,	/* A1R5G5B5_UNORM */
	VK_FORMAT_B4G4R4A4_UNORM_PACK16,	/* B4G4R4A4_UNORM */
	VK_FORMAT_A2R10G10B10_UNORM_PACK32,	/* A2R10G10B10_UNORM */
	VK_FORMAT_R16G16_UNORM,				/* R16G16_UNORM */
	VK_FORMAT_R16G16B16A16_UNORM,		/* R16G16B16A16_UNORM */
	VK_FORMAT_R8_UNORM,					/* R8_UNORM */
	VK_FORMAT_BC1_RGBA_UNORM_BLOCK,		/* BC1_UNORM */
	VK_FORMAT_BC2_UNORM_BLOCK,			/* BC2_UNORM */
	VK_FORMAT_BC3_UNORM_BLOCK,			/* BC3_UNORM */
	VK_FORMAT_BC7_UNORM_BLOCK,			/* BC7_UNORM */
	VK_FORMAT_R8G8_SNORM,				/* R8G8_SNORM */
	VK_FORMAT_R8G8B8A8_SNORM,			/* R8G8B8A8_SNORM */
	VK_FORMAT_R16_SFLOAT,				/* R16_SFLOAT */
	VK_FORMAT_R16G16_SFLOAT,			/* R16G16_SFLOAT */
	VK_FORMAT_R16G16B16A16_SFLOAT,		/* R16G16B16A16_SFLOAT */
	VK_FORMAT_R32_SFLOAT,				/* R32_SFLOAT */
	VK_FORMAT_R32G32_SFLOAT,			/* R32G32_SFLOAT */
	VK_FORMAT_R32G32B32A32_SFLOAT,		/* R32G32B32A32_SFLOAT */
	VK_FORMAT_R8_UINT,					/* R8_UINT */
	VK_FORMAT_R8G8_UINT,				/* R8G8_UINT */
	VK_FORMAT_R8G8B8A8_UINT,			/* R8G8B8A8_UINT */
	VK_FORMAT_R16_UINT,					/* R16_UINT */
	VK_FORMAT_R16G16_UINT,				/* R16G16_UINT */
	VK_FORMAT_R16G16B16A16_UINT,		/* R16G16B16A16_UINT */
	VK_FORMAT_D16_UNORM,				/* D16_UNORM */
	VK_FORMAT_D32_SFLOAT,				/* D32_SFLOAT */
	VK_FORMAT_D16_UNORM_S8_UINT,		/* D16_UNORM_S8_UINT */
	VK_FORMAT_D32_SFLOAT_S8_UINT		/* D32_SFLOAT_S8_UINT */
};

static VkFormat RefreshToVK_VertexFormat[] =
{
	VK_FORMAT_R32_UINT,				/* UINT */
	VK_FORMAT_R32_SFLOAT,			/* FLOAT */
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
	VK_BLEND_FACTOR_SRC_ALPHA_SATURATE
};

static VkBlendOp RefreshToVK_BlendOp[] =
{
	VK_BLEND_OP_ADD,
	VK_BLEND_OP_SUBTRACT,
	VK_BLEND_OP_REVERSE_SUBTRACT,
	VK_BLEND_OP_MIN,
	VK_BLEND_OP_MAX
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
typedef struct VulkanBuffer VulkanBuffer;
typedef struct VulkanTexture VulkanTexture;

typedef struct VulkanMemoryFreeRegion
{
	VulkanMemoryAllocation *allocation;
	VkDeviceSize offset;
	VkDeviceSize size;
	uint32_t allocationIndex;
	uint32_t sortedIndex;
} VulkanMemoryFreeRegion;

typedef struct VulkanMemoryUsedRegion
{
	VulkanMemoryAllocation *allocation;
	VkDeviceSize offset;
	VkDeviceSize size;
	VkDeviceSize resourceOffset; /* differs from offset based on alignment*/
	VkDeviceSize resourceSize; /* differs from size based on alignment */
	VkDeviceSize alignment;
	uint8_t isBuffer;
	REFRESHNAMELESS union
	{
		VulkanBuffer *vulkanBuffer;
		VulkanTexture *vulkanTexture;
	};
} VulkanMemoryUsedRegion;

typedef struct VulkanMemorySubAllocator
{
	uint32_t memoryTypeIndex;
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
	VulkanMemoryUsedRegion **usedRegions;
	uint32_t usedRegionCount;
	uint32_t usedRegionCapacity;
	VulkanMemoryFreeRegion **freeRegions;
	uint32_t freeRegionCount;
	uint32_t freeRegionCapacity;
	uint8_t dedicated;
	uint8_t availableForAllocation;
	VkDeviceSize freeSpace;
	VkDeviceSize usedSpace;
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
		VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED
	},

	/* RESOURCE_ACCESS_INDIRECT_BUFFER */
	{
		VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
		VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
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
		VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
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

	/* RESOURCE_ACCESS_COMPUTE_SHADER_STORAGE_IMAGE_READ_WRITE */
	{
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
		VK_IMAGE_LAYOUT_GENERAL
	},

	/* RESOURCE_ACCESS_COMPUTE_SHADER_BUFFER_READ_WRITE */
	{
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED
	},

	/* RESOURCE_ACCESS_TRANSFER_READ_WRITE */
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

typedef struct VulkanBufferContainer /* cast from Refresh_Buffer */
{
	VulkanBuffer *vulkanBuffer;
} VulkanBufferContainer;

struct VulkanBuffer
{
	VkBuffer buffer;
	VkDeviceSize size;
	VulkanMemoryUsedRegion *usedRegion;
	VulkanResourceAccessType resourceAccessType;
	VkBufferUsageFlags usage;

	uint8_t preferDeviceLocal;

	SDL_atomic_t referenceCount; /* Tracks command buffer usage */

	VulkanBufferContainer *container;
};

typedef struct VulkanUniformBufferPool VulkanUniformBufferPool;

typedef struct VulkanUniformBuffer
{
	VulkanUniformBufferPool *pool;
	VkDeviceSize poolOffset; /* memory offset relative to the pool buffer */
	VkDeviceSize offset; /* based on uniform pushes */
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

/* This is actually just one buffer that we carve slices out of. */
struct VulkanUniformBufferPool
{
	VulkanUniformBufferType type;
	VulkanUniformDescriptorPool descriptorPool;
	VulkanBuffer *buffer;
	VkDeviceSize nextAvailableOffset;
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

typedef struct VulkanSampler
{
	VkSampler sampler;
	SDL_atomic_t referenceCount;
} VulkanSampler;

typedef struct VulkanShaderModule
{
	VkShaderModule shaderModule;
	SDL_atomic_t referenceCount;
} VulkanShaderModule;

typedef struct VulkanTextureContainer /* Cast from Refresh_Texture */
{
	VulkanTexture *vulkanTexture;
} VulkanTextureContainer;

struct VulkanTexture
{
	VulkanMemoryUsedRegion *usedRegion;

	VkImage image;
	VkImageView view;
	VkExtent2D dimensions;

	uint8_t is3D;
	uint8_t isCube;

	uint32_t depth;
	uint32_t layerCount;
	uint32_t levelCount;
	Refresh_SampleCount sampleCount;
	VkFormat format;
	VulkanResourceAccessType resourceAccessType;
	VkImageUsageFlags usageFlags;

	VkImageAspectFlags aspectFlags;

	struct VulkanTexture *msaaTex;

	SDL_atomic_t referenceCount;

	VulkanTextureContainer *container;
};

typedef struct VulkanRenderTarget
{
	VkImageView view;
} VulkanRenderTarget;

typedef struct VulkanFramebuffer
{
	VkFramebuffer framebuffer;
	SDL_atomic_t referenceCount;
} VulkanFramebuffer;

typedef struct VulkanSwapchainData
{
	/* Window surface */
	VkSurfaceKHR surface;
	VkSurfaceFormatKHR surfaceFormat;

	/* Swapchain for window surface */
	VkSwapchainKHR swapchain;
	VkFormat swapchainFormat;
	VkComponentMapping swapchainSwizzle;
	VkPresentModeKHR presentMode;

	/* Swapchain images */
	VkExtent2D extent;
	VulkanTextureContainer *textureContainers;
	uint32_t imageCount;

	/* Synchronization primitives */
	VkSemaphore imageAvailableSemaphore;
	VkSemaphore renderFinishedSemaphore;
} VulkanSwapchainData;

typedef struct WindowData
{
	void *windowHandle;
	VkPresentModeKHR preferredPresentMode;
	VulkanSwapchainData *swapchainData;
} WindowData;

typedef struct SwapChainSupportDetails
{
	VkSurfaceCapabilitiesKHR capabilities;
	VkSurfaceFormatKHR *formats;
	uint32_t formatsLength;
	VkPresentModeKHR *presentModes;
	uint32_t presentModesLength;
} SwapChainSupportDetails;

typedef struct VulkanPresentData
{
	WindowData *windowData;
	uint32_t swapchainImageIndex;
} VulkanPresentData;

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

	VulkanShaderModule *vertexShaderModule;
	VulkanShaderModule *fragmentShaderModule;

	SDL_atomic_t referenceCount;
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

	VulkanShaderModule *computeShaderModule;
	SDL_atomic_t referenceCount;
} VulkanComputePipeline;

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
	VkFormat format;
	Refresh_Vec4 clearColor;
	Refresh_LoadOp loadOp;
	Refresh_StoreOp storeOp;
} RenderPassColorTargetDescription;

typedef struct RenderPassDepthStencilTargetDescription
{
	VkFormat format;
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
	Refresh_SampleCount colorAttachmentSampleCount;
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

	if (a->colorAttachmentSampleCount != b->colorAttachmentSampleCount)
	{
		return 0;
	}

	for (i = 0; i < a->colorAttachmentCount; i += 1)
	{
		if (a->colorTargetDescriptions[i].format != b->colorTargetDescriptions[i].format)
		{
			return 0;
		}

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

	if (a->depthStencilTargetDescription.format != b->depthStencilTargetDescription.format)
	{
		return 0;
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
	VulkanFramebuffer *value;
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

static inline VulkanFramebuffer* FramebufferHashArray_Fetch(
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
	VulkanFramebuffer *value
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

typedef struct RenderTargetHash
{
	VulkanTexture *texture;
	uint32_t depth;
	uint32_t layer;
	uint32_t level;
} RenderTargetHash;

typedef struct RenderTargetHashMap
{
	RenderTargetHash key;
	VulkanRenderTarget *value;
} RenderTargetHashMap;

typedef struct RenderTargetHashArray
{
	RenderTargetHashMap *elements;
	int32_t count;
	int32_t capacity;
} RenderTargetHashArray;

static inline uint8_t RenderTargetHash_Compare(
	RenderTargetHash *a,
	RenderTargetHash *b
) {
	if (a->texture != b->texture)
	{
		return 0;
	}

	if (a->layer != b->layer)
	{
		return 0;
	}

	if (a->level != b->level)
	{
		return 0;
	}

	if (a->depth != b->depth)
	{
		return 0;
	}

	return 1;
}

static inline VulkanRenderTarget* RenderTargetHash_Fetch(
	RenderTargetHashArray *arr,
	RenderTargetHash *key
) {
	int32_t i;

	for (i = 0; i < arr->count; i += 1)
	{
		RenderTargetHash *e = &arr->elements[i].key;
		if (RenderTargetHash_Compare(e, key))
		{
			return arr->elements[i].value;
		}
	}

	return NULL;
}

static inline void RenderTargetHash_Insert(
	RenderTargetHashArray *arr,
	RenderTargetHash key,
	VulkanRenderTarget *value
) {
	RenderTargetHashMap map;
	map.key = key;
	map.value = value;

	EXPAND_ELEMENTS_IF_NEEDED(arr, 4, RenderTargetHashMap)

	arr->elements[arr->count] = map;
	arr->count += 1;
}

static inline void RenderTargetHash_Remove(
	RenderTargetHashArray *arr,
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
	uint8_t fromPool;
} VulkanTransferBuffer;

typedef struct VulkanTransferBufferPool
{
	SDL_mutex *lock;

	VulkanTransferBuffer **availableBuffers;
	uint32_t availableBufferCount;
	uint32_t availableBufferCapacity;
} VulkanTransferBufferPool;

typedef struct VulkanFencePool
{
	SDL_mutex *lock;

	VkFence *availableFences;
	uint32_t availableFenceCount;
	uint32_t availableFenceCapacity;
} VulkanFencePool;

typedef struct VulkanCommandPool VulkanCommandPool;

typedef struct VulkanCommandBuffer
{
	VkCommandBuffer commandBuffer;
	VulkanCommandPool *commandPool;

	VulkanPresentData *presentDatas;
	uint32_t presentDataCount;
	uint32_t presentDataCapacity;

	VkSemaphore *waitSemaphores;
	uint32_t waitSemaphoreCount;
	uint32_t waitSemaphoreCapacity;

	VkSemaphore *signalSemaphores;
	uint32_t signalSemaphoreCount;
	uint32_t signalSemaphoreCapacity;

	VulkanComputePipeline *currentComputePipeline;
	VulkanGraphicsPipeline *currentGraphicsPipeline;

	VulkanTexture *renderPassColorTargetTextures[MAX_COLOR_TARGET_BINDINGS];
	uint32_t renderPassColorTargetCount;
	VulkanTexture *renderPassDepthTexture; /* can be NULL */

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

	/* Keep track of compute resources for memory barriers */

	VulkanBuffer **boundComputeBuffers;
	uint32_t boundComputeBufferCount;
	uint32_t boundComputeBufferCapacity;

	VulkanTexture **boundComputeTextures;
	uint32_t boundComputeTextureCount;
	uint32_t boundComputeTextureCapacity;

	/* Viewport/scissor state */

	VkViewport currentViewport;
	VkRect2D currentScissor;

	/* Track used resources */

	VulkanBuffer **usedBuffers;
	uint32_t usedBufferCount;
	uint32_t usedBufferCapacity;

	VulkanTexture **usedTextures;
	uint32_t usedTextureCount;
	uint32_t usedTextureCapacity;

	VulkanSampler **usedSamplers;
	uint32_t usedSamplerCount;
	uint32_t usedSamplerCapacity;

	VulkanGraphicsPipeline **usedGraphicsPipelines;
	uint32_t usedGraphicsPipelineCount;
	uint32_t usedGraphicsPipelineCapacity;

	VulkanComputePipeline **usedComputePipelines;
	uint32_t usedComputePipelineCount;
	uint32_t usedComputePipelineCapacity;

	VulkanFramebuffer **usedFramebuffers;
	uint32_t usedFramebufferCount;
	uint32_t usedFramebufferCapacity;

	/* Shader modules have references tracked by pipelines */

	VkFence inFlightFence;
	uint8_t autoReleaseFence;
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
	uint8_t unifiedMemoryWarning;

	uint8_t supportsDebugUtils;
	uint8_t debugMode;
	VulkanExtensions supports;

	VulkanMemoryAllocator *memoryAllocator;
	VkPhysicalDeviceMemoryProperties memoryProperties;

	WindowData **claimedWindows;
	uint32_t claimedWindowCount;
	uint32_t claimedWindowCapacity;

	uint32_t queueFamilyIndex;
	VkQueue unifiedQueue;

	VulkanCommandBuffer **submittedCommandBuffers;
	uint32_t submittedCommandBufferCount;
	uint32_t submittedCommandBufferCapacity;

	VulkanTransferBufferPool transferBufferPool;
	VulkanFencePool fencePool;

	CommandPoolHashTable commandPoolHashTable;
	DescriptorSetLayoutHashTable descriptorSetLayoutHashTable;
	GraphicsPipelineLayoutHashTable graphicsPipelineLayoutHashTable;
	ComputePipelineLayoutHashTable computePipelineLayoutHashTable;
	RenderPassHashArray renderPassHashArray;
	FramebufferHashArray framebufferHashArray;
	RenderTargetHashArray renderTargetHashArray;

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

	VulkanBuffer *dummyBuffer;
	VulkanUniformBuffer *dummyVertexUniformBuffer;
	VulkanUniformBuffer *dummyFragmentUniformBuffer;
	VulkanUniformBuffer *dummyComputeUniformBuffer;

	VkDeviceSize minUBOAlignment;

	/* Some drivers don't support D16 for some reason. Fun! */
	VkFormat D16Format;
	VkFormat D16S8Format;

	VulkanTexture **texturesToDestroy;
	uint32_t texturesToDestroyCount;
	uint32_t texturesToDestroyCapacity;

	VulkanBuffer **buffersToDestroy;
	uint32_t buffersToDestroyCount;
	uint32_t buffersToDestroyCapacity;

	VulkanSampler **samplersToDestroy;
	uint32_t samplersToDestroyCount;
	uint32_t samplersToDestroyCapacity;

	VulkanGraphicsPipeline **graphicsPipelinesToDestroy;
	uint32_t graphicsPipelinesToDestroyCount;
	uint32_t graphicsPipelinesToDestroyCapacity;

	VulkanComputePipeline **computePipelinesToDestroy;
	uint32_t computePipelinesToDestroyCount;
	uint32_t computePipelinesToDestroyCapacity;

	VulkanShaderModule **shaderModulesToDestroy;
	uint32_t shaderModulesToDestroyCount;
	uint32_t shaderModulesToDestroyCapacity;

	VulkanFramebuffer **framebuffersToDestroy;
	uint32_t framebuffersToDestroyCount;
	uint32_t framebuffersToDestroyCapacity;

	SDL_mutex *allocatorLock;
	SDL_mutex *disposeLock;
	SDL_mutex *submitLock;
	SDL_mutex *acquireCommandBufferLock;
	SDL_mutex *renderPassFetchLock;
	SDL_mutex *framebufferFetchLock;
	SDL_mutex *renderTargetFetchLock;

	uint8_t needDefrag;
	uint64_t defragTimestamp;
	uint8_t defragInProgress;

#define VULKAN_INSTANCE_FUNCTION(ext, ret, func, params) \
		vkfntype_##func func;
	#define VULKAN_DEVICE_FUNCTION(ext, ret, func, params) \
		vkfntype_##func func;
	#include "Refresh_Driver_Vulkan_vkfuncs.h"
} VulkanRenderer;

/* Forward declarations */

static uint8_t VULKAN_INTERNAL_DefragmentMemory(VulkanRenderer *renderer);
static void VULKAN_INTERNAL_BeginCommandBuffer(VulkanRenderer *renderer, VulkanCommandBuffer *commandBuffer);
static void VULKAN_UnclaimWindow(Refresh_Renderer *driverData, void *windowHandle);
static void VULKAN_Wait(Refresh_Renderer *driverData);
static void VULKAN_Submit(Refresh_Renderer *driverData, Refresh_CommandBuffer *commandBuffer);
static void VULKAN_INTERNAL_DestroyRenderTarget(VulkanRenderer *renderer, VulkanRenderTarget *renderTarget);

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

#define VULKAN_ERROR_CHECK(res, fn, ret) \
	if (res != VK_SUCCESS) \
	{ \
		Refresh_LogError("%s %s", #fn, VkErrorMessages(res)); \
		return ret; \
	}

/* Utility */

static inline VkFormat RefreshToVK_DepthFormat(
	VulkanRenderer* renderer,
	Refresh_TextureFormat format
) {
	switch (format)
	{
		case REFRESH_TEXTUREFORMAT_D16_UNORM:
			return renderer->D16Format;
		case REFRESH_TEXTUREFORMAT_D16_UNORM_S8_UINT:
			return renderer->D16S8Format;
		case REFRESH_TEXTUREFORMAT_D32_SFLOAT:
			return VK_FORMAT_D32_SFLOAT;
		case REFRESH_TEXTUREFORMAT_D32_SFLOAT_S8_UINT:
			return VK_FORMAT_D32_SFLOAT_S8_UINT;
		default:
			return VK_FORMAT_UNDEFINED;
	}
}

static inline uint8_t IsRefreshDepthFormat(Refresh_TextureFormat format)
{
	switch (format)
	{
		case REFRESH_TEXTUREFORMAT_D16_UNORM:
		case REFRESH_TEXTUREFORMAT_D32_SFLOAT:
		case REFRESH_TEXTUREFORMAT_D16_UNORM_S8_UINT:
		case REFRESH_TEXTUREFORMAT_D32_SFLOAT_S8_UINT:
			return 1;

		default:
			return 0;
	}
}

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
		case VK_FORMAT_R8_UNORM:
		case VK_FORMAT_R8_UINT:
			return 1;
		case VK_FORMAT_R5G6B5_UNORM_PACK16:
		case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
		case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
		case VK_FORMAT_R16_SFLOAT:
		case VK_FORMAT_R8G8_SNORM:
		case VK_FORMAT_R8G8_UINT:
		case VK_FORMAT_R16_UINT:
		case VK_FORMAT_D16_UNORM:
			return 2;
		case VK_FORMAT_D16_UNORM_S8_UINT:
			return 3;
		case VK_FORMAT_R8G8B8A8_UNORM:
		case VK_FORMAT_B8G8R8A8_UNORM:
		case VK_FORMAT_R32_SFLOAT:
		case VK_FORMAT_R16G16_UNORM:
		case VK_FORMAT_R16G16_SFLOAT:
		case VK_FORMAT_R8G8B8A8_SNORM:
		case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
		case VK_FORMAT_R8G8B8A8_UINT:
		case VK_FORMAT_R16G16_UINT:
		case VK_FORMAT_D32_SFLOAT:
			return 4;
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
			return 5;
		case VK_FORMAT_R16G16B16A16_SFLOAT:
		case VK_FORMAT_R16G16B16A16_UNORM:
		case VK_FORMAT_R32G32_SFLOAT:
		case VK_FORMAT_R16G16B16A16_UINT:
		case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
			return 8;
		case VK_FORMAT_R32G32B32A32_SFLOAT:
		case VK_FORMAT_BC2_UNORM_BLOCK:
		case VK_FORMAT_BC3_UNORM_BLOCK:
		case VK_FORMAT_BC7_UNORM_BLOCK:
			return 16;
		default:
			Refresh_LogError("Texture format not recognized!");
			return 0;
	}
}

static inline uint32_t VULKAN_INTERNAL_TextureBlockSize(
	VkFormat format
) {
	switch (format)
	{
	case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
	case VK_FORMAT_BC2_UNORM_BLOCK:
	case VK_FORMAT_BC3_UNORM_BLOCK:
	case VK_FORMAT_BC7_UNORM_BLOCK:
		return 4;
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_UNORM:
	case VK_FORMAT_R5G6B5_UNORM_PACK16:
	case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
	case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
	case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
	case VK_FORMAT_R16G16_UNORM:
	case VK_FORMAT_R16G16B16A16_UNORM:
	case VK_FORMAT_R8_UNORM:
	case VK_FORMAT_R8G8_SNORM:
	case VK_FORMAT_R8G8B8A8_SNORM:
	case VK_FORMAT_R16_SFLOAT:
	case VK_FORMAT_R16G16_SFLOAT:
	case VK_FORMAT_R16G16B16A16_SFLOAT:
	case VK_FORMAT_R32_SFLOAT:
	case VK_FORMAT_R32G32_SFLOAT:
	case VK_FORMAT_R32G32B32A32_SFLOAT:
	case VK_FORMAT_R8_UINT:
	case VK_FORMAT_R8G8_UINT:
	case VK_FORMAT_R8G8B8A8_UINT:
	case VK_FORMAT_R16_UINT:
	case VK_FORMAT_R16G16_UINT:
	case VK_FORMAT_R16G16B16A16_UINT:
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_D32_SFLOAT:
	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return 1;
	default:
		Refresh_LogError("Unrecognized texture format!");
		return 0;
	}
}

static inline VkDeviceSize VULKAN_INTERNAL_BytesPerImage(
	uint32_t width,
	uint32_t height,
	VkFormat format
) {
	uint32_t blockSize = VULKAN_INTERNAL_TextureBlockSize(format);
	return (width * height * VULKAN_INTERNAL_BytesPerPixel(format)) / (blockSize * blockSize);
}

static inline Refresh_SampleCount VULKAN_INTERNAL_GetMaxMultiSampleCount(
	VulkanRenderer *renderer,
	Refresh_SampleCount multiSampleCount
) {
	VkSampleCountFlags flags = renderer->physicalDeviceProperties.properties.limits.framebufferColorSampleCounts;
	Refresh_SampleCount maxSupported = REFRESH_SAMPLECOUNT_1;

	if (flags & VK_SAMPLE_COUNT_8_BIT)
	{
		maxSupported = REFRESH_SAMPLECOUNT_8;
	}
	else if (flags & VK_SAMPLE_COUNT_4_BIT)
	{
		maxSupported = REFRESH_SAMPLECOUNT_4;
	}
	else if (flags & VK_SAMPLE_COUNT_2_BIT)
	{
		maxSupported = REFRESH_SAMPLECOUNT_2;
	}

	return SDL_min(multiSampleCount, maxSupported);
}

/* Memory Management */

/* Vulkan: Memory Allocation */

static inline VkDeviceSize VULKAN_INTERNAL_NextHighestAlignment(
	VkDeviceSize n,
	VkDeviceSize align
) {
	return align * ((n + align - 1) / align);
}

static void VULKAN_INTERNAL_MakeMemoryUnavailable(
	VulkanRenderer* renderer,
	VulkanMemoryAllocation *allocation
) {
	uint32_t i, j;
	VulkanMemoryFreeRegion *freeRegion;

	allocation->availableForAllocation = 0;

	for (i = 0; i < allocation->freeRegionCount; i += 1)
	{
		freeRegion = allocation->freeRegions[i];

		/* close the gap in the sorted list */
		if (allocation->allocator->sortedFreeRegionCount > 1)
		{
			for (j = freeRegion->sortedIndex; j < allocation->allocator->sortedFreeRegionCount - 1; j += 1)
			{
				allocation->allocator->sortedFreeRegions[j] =
					allocation->allocator->sortedFreeRegions[j + 1];

				allocation->allocator->sortedFreeRegions[j]->sortedIndex = j;
			}
		}

		allocation->allocator->sortedFreeRegionCount -= 1;
	}
}

static void VULKAN_INTERNAL_RemoveMemoryFreeRegion(
	VulkanRenderer *renderer,
	VulkanMemoryFreeRegion *freeRegion
) {
	uint32_t i;

	SDL_LockMutex(renderer->allocatorLock);

	if (freeRegion->allocation->availableForAllocation)
	{
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
	}

	/* close the gap in the buffer list */
	if (freeRegion->allocation->freeRegionCount > 1 && freeRegion->allocationIndex != freeRegion->allocation->freeRegionCount - 1)
	{
		freeRegion->allocation->freeRegions[freeRegion->allocationIndex] =
			freeRegion->allocation->freeRegions[freeRegion->allocation->freeRegionCount - 1];

		freeRegion->allocation->freeRegions[freeRegion->allocationIndex]->allocationIndex =
			freeRegion->allocationIndex;
	}

	freeRegion->allocation->freeRegionCount -= 1;

	freeRegion->allocation->freeSpace -= freeRegion->size;

	SDL_free(freeRegion);

	SDL_UnlockMutex(renderer->allocatorLock);
}

static void VULKAN_INTERNAL_NewMemoryFreeRegion(
	VulkanRenderer *renderer,
	VulkanMemoryAllocation *allocation,
	VkDeviceSize offset,
	VkDeviceSize size
) {
	VulkanMemoryFreeRegion *newFreeRegion;
	VkDeviceSize newOffset, newSize;
	int32_t insertionIndex = 0;
	int32_t i;

	SDL_LockMutex(renderer->allocatorLock);

	/* look for an adjacent region to merge */
	for (i = allocation->freeRegionCount - 1; i >= 0; i -= 1)
	{
		/* check left side */
		if (allocation->freeRegions[i]->offset + allocation->freeRegions[i]->size == offset)
		{
			newOffset = allocation->freeRegions[i]->offset;
			newSize = allocation->freeRegions[i]->size + size;

			VULKAN_INTERNAL_RemoveMemoryFreeRegion(renderer, allocation->freeRegions[i]);
			VULKAN_INTERNAL_NewMemoryFreeRegion(renderer, allocation, newOffset, newSize);

			SDL_UnlockMutex(renderer->allocatorLock);
			return;
		}

		/* check right side */
		if (allocation->freeRegions[i]->offset == offset + size)
		{
			newOffset = offset;
			newSize = allocation->freeRegions[i]->size + size;

			VULKAN_INTERNAL_RemoveMemoryFreeRegion(renderer, allocation->freeRegions[i]);
			VULKAN_INTERNAL_NewMemoryFreeRegion(renderer, allocation, newOffset, newSize);

			SDL_UnlockMutex(renderer->allocatorLock);
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

	allocation->freeSpace += size;

	allocation->freeRegions[allocation->freeRegionCount - 1] = newFreeRegion;
	newFreeRegion->allocationIndex = allocation->freeRegionCount - 1;

	if (allocation->availableForAllocation)
	{
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

	SDL_UnlockMutex(renderer->allocatorLock);
}

static VulkanMemoryUsedRegion* VULKAN_INTERNAL_NewMemoryUsedRegion(
	VulkanRenderer *renderer,
	VulkanMemoryAllocation *allocation,
	VkDeviceSize offset,
	VkDeviceSize size,
	VkDeviceSize resourceOffset,
	VkDeviceSize resourceSize,
	VkDeviceSize alignment
) {
	VulkanMemoryUsedRegion *memoryUsedRegion;

	SDL_LockMutex(renderer->allocatorLock);

	if (allocation->usedRegionCount == allocation->usedRegionCapacity)
	{
		allocation->usedRegionCapacity *= 2;
		allocation->usedRegions = SDL_realloc(
			allocation->usedRegions,
			allocation->usedRegionCapacity * sizeof(VulkanMemoryUsedRegion*)
		);
	}

	memoryUsedRegion = SDL_malloc(sizeof(VulkanMemoryUsedRegion));
	memoryUsedRegion->allocation = allocation;
	memoryUsedRegion->offset = offset;
	memoryUsedRegion->size = size;
	memoryUsedRegion->resourceOffset = resourceOffset;
	memoryUsedRegion->resourceSize = resourceSize;
	memoryUsedRegion->alignment = alignment;

	allocation->usedSpace += size;

	allocation->usedRegions[allocation->usedRegionCount] = memoryUsedRegion;
	allocation->usedRegionCount += 1;

	SDL_UnlockMutex(renderer->allocatorLock);

	return memoryUsedRegion;
}

static void VULKAN_INTERNAL_RemoveMemoryUsedRegion(
	VulkanRenderer *renderer,
	VulkanMemoryUsedRegion *usedRegion
) {
	uint32_t i;

	SDL_LockMutex(renderer->allocatorLock);

	for (i = 0; i < usedRegion->allocation->usedRegionCount; i += 1)
	{
		if (usedRegion->allocation->usedRegions[i] == usedRegion)
		{
			/* plug the hole */
			if (i != usedRegion->allocation->usedRegionCount - 1)
			{
				usedRegion->allocation->usedRegions[i] = usedRegion->allocation->usedRegions[usedRegion->allocation->usedRegionCount - 1];
			}

			break;
		}
	}

	usedRegion->allocation->usedSpace -= usedRegion->size;

	usedRegion->allocation->usedRegionCount -= 1;

	VULKAN_INTERNAL_NewMemoryFreeRegion(
		renderer,
		usedRegion->allocation,
		usedRegion->offset,
		usedRegion->size
	);

	if (!usedRegion->allocation->dedicated)
	{
		renderer->needDefrag = 1;
		renderer->defragTimestamp = SDL_GetTicks64() + DEFRAG_TIME; /* reset timer so we batch defrags */
	}

	SDL_free(usedRegion);

	SDL_UnlockMutex(renderer->allocatorLock);
}

static uint8_t VULKAN_INTERNAL_FindMemoryType(
	VulkanRenderer *renderer,
	uint32_t typeFilter,
	VkMemoryPropertyFlags requiredProperties,
	VkMemoryPropertyFlags ignoredProperties,
	uint32_t *memoryTypeIndex
) {
	uint32_t i;

	for (i = *memoryTypeIndex; i < renderer->memoryProperties.memoryTypeCount; i += 1)
	{
		if (	(typeFilter & (1 << i)) &&
			(renderer->memoryProperties.memoryTypes[i].propertyFlags & requiredProperties) == requiredProperties &&
			(renderer->memoryProperties.memoryTypes[i].propertyFlags & ignoredProperties) == 0	)
		{
			*memoryTypeIndex = i;
			return 1;
		}
	}

	return 0;
}

static uint8_t VULKAN_INTERNAL_FindBufferMemoryRequirements(
	VulkanRenderer *renderer,
	VkBuffer buffer,
	VkMemoryPropertyFlags requiredMemoryProperties,
	VkMemoryPropertyFlags ignoredMemoryProperties,
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

	return VULKAN_INTERNAL_FindMemoryType(
		renderer,
		pMemoryRequirements->memoryRequirements.memoryTypeBits,
		requiredMemoryProperties,
		ignoredMemoryProperties,
		pMemoryTypeIndex
	);
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

	return VULKAN_INTERNAL_FindMemoryType(
		renderer,
		pMemoryRequirements->memoryRequirements.memoryTypeBits,
		requiredMemoryPropertyFlags,
		ignoredMemoryPropertyFlags,
		pMemoryTypeIndex
	);
}

static void VULKAN_INTERNAL_DeallocateMemory(
	VulkanRenderer *renderer,
	VulkanMemorySubAllocator *allocator,
	uint32_t allocationIndex
) {
	uint32_t i;

	VulkanMemoryAllocation *allocation = allocator->allocations[allocationIndex];

	SDL_LockMutex(renderer->allocatorLock);

	for (i = 0; i < allocation->freeRegionCount; i += 1)
	{
		VULKAN_INTERNAL_RemoveMemoryFreeRegion(
			renderer,
			allocation->freeRegions[i]
		);
	}
	SDL_free(allocation->freeRegions);

	/* no need to iterate used regions because deallocate
	 * only happens when there are 0 used regions
	 */
	SDL_free(allocation->usedRegions);

	renderer->vkFreeMemory(
		renderer->logicalDevice,
		allocation->memory,
		NULL
	);

	SDL_DestroyMutex(allocation->memoryLock);
	SDL_free(allocation);

	if (allocationIndex != allocator->allocationCount - 1)
	{
		allocator->allocations[allocationIndex] = allocator->allocations[allocator->allocationCount - 1];
	}

	allocator->allocationCount -= 1;

	SDL_UnlockMutex(renderer->allocatorLock);
}

static uint8_t VULKAN_INTERNAL_AllocateMemory(
	VulkanRenderer *renderer,
	VkBuffer buffer,
	VkImage image,
	uint32_t memoryTypeIndex,
	VkDeviceSize allocationSize,
	uint8_t dedicated, /* indicates that one resource uses this memory and the memory shouldn't be moved */
	uint8_t isHostVisible,
	VulkanMemoryAllocation **pMemoryAllocation)
{
	VulkanMemoryAllocation *allocation;
	VulkanMemorySubAllocator *allocator = &renderer->memoryAllocator->subAllocators[memoryTypeIndex];
	VkMemoryAllocateInfo allocInfo;
	VkResult result;

	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.pNext = NULL;
	allocInfo.memoryTypeIndex = memoryTypeIndex;
	allocInfo.allocationSize = allocationSize;

	allocation = SDL_malloc(sizeof(VulkanMemoryAllocation));
	allocation->size = allocationSize;
	allocation->freeSpace = 0; /* added by FreeRegions */
	allocation->usedSpace = 0; /* added by UsedRegions */
	allocation->memoryLock = SDL_CreateMutex();

	allocator->allocationCount += 1;
	allocator->allocations = SDL_realloc(
		allocator->allocations,
		sizeof(VulkanMemoryAllocation*) * allocator->allocationCount
	);

	allocator->allocations[
		allocator->allocationCount - 1
	] = allocation;

	if (dedicated)
	{
		allocation->dedicated = 1;
		allocation->availableForAllocation = 0;
	}
	else
	{
		allocInfo.pNext = NULL;
		allocation->dedicated = 0;
		allocation->availableForAllocation = 1;
	}

	allocation->usedRegions = SDL_malloc(sizeof(VulkanMemoryUsedRegion*));
	allocation->usedRegionCount = 0;
	allocation->usedRegionCapacity = 1;

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

		return 0;
	}

	/* persistent mapping for host memory */
	if (isHostVisible)
	{
		result = renderer->vkMapMemory(
			renderer->logicalDevice,
			allocation->memory,
			0,
			VK_WHOLE_SIZE,
			0,
			(void**) &allocation->mapPointer
		);
		VULKAN_ERROR_CHECK(result, vkMapMemory, 0)
	}
	else
	{
		allocation->mapPointer = NULL;
	}

	VULKAN_INTERNAL_NewMemoryFreeRegion(
		renderer,
		allocation,
		0,
		allocation->size
	);

	*pMemoryAllocation = allocation;
	return 1;
}

static uint8_t VULKAN_INTERNAL_BindBufferMemory(
	VulkanRenderer *renderer,
	VulkanMemoryUsedRegion *usedRegion,
	VkDeviceSize alignedOffset,
	VkBuffer buffer
) {
	VkResult vulkanResult;

	SDL_LockMutex(usedRegion->allocation->memoryLock);

	vulkanResult = renderer->vkBindBufferMemory(
		renderer->logicalDevice,
		buffer,
		usedRegion->allocation->memory,
		alignedOffset
	);

	SDL_UnlockMutex(usedRegion->allocation->memoryLock);

	VULKAN_ERROR_CHECK(vulkanResult, vkBindBufferMemory, 0)

	return 1;
}

static uint8_t VULKAN_INTERNAL_BindImageMemory(
	VulkanRenderer *renderer,
	VulkanMemoryUsedRegion *usedRegion,
	VkDeviceSize alignedOffset,
	VkImage image
) {
	VkResult vulkanResult;

	SDL_LockMutex(usedRegion->allocation->memoryLock);

	vulkanResult = renderer->vkBindImageMemory(
		renderer->logicalDevice,
		image,
		usedRegion->allocation->memory,
		alignedOffset
	);

	SDL_UnlockMutex(usedRegion->allocation->memoryLock);

	VULKAN_ERROR_CHECK(vulkanResult, vkBindBufferMemory, 0)

	return 1;
}

static uint8_t VULKAN_INTERNAL_BindResourceMemory(
	VulkanRenderer* renderer,
	uint32_t memoryTypeIndex,
	VkMemoryRequirements2KHR* memoryRequirements,
	uint8_t forceDedicated,
	VkDeviceSize resourceSize, /* may be different from requirements size! */
	VkBuffer buffer, /* may be VK_NULL_HANDLE */
	VkImage image, /* may be VK_NULL_HANDLE */
	VulkanMemoryUsedRegion** pMemoryUsedRegion
) {
	VulkanMemoryAllocation *allocation;
	VulkanMemorySubAllocator *allocator;
	VulkanMemoryFreeRegion *region;
	VulkanMemoryUsedRegion *usedRegion;

	VkDeviceSize requiredSize, allocationSize;
	VkDeviceSize alignedOffset;
	uint32_t newRegionSize, newRegionOffset;
	uint8_t shouldAllocDedicated = forceDedicated;
	uint8_t isHostVisible, allocationResult;

	isHostVisible =
		(renderer->memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags &
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;

	allocator = &renderer->memoryAllocator->subAllocators[memoryTypeIndex];
	requiredSize = memoryRequirements->memoryRequirements.size;

	if (	(buffer == VK_NULL_HANDLE && image == VK_NULL_HANDLE) ||
		(buffer != VK_NULL_HANDLE && image != VK_NULL_HANDLE)	)
	{
		Refresh_LogError("BindResourceMemory must be given either a VulkanBuffer or a VulkanTexture");
		return 0;
	}

	SDL_LockMutex(renderer->allocatorLock);

	/* find the largest free region and use it */
	if (!shouldAllocDedicated && allocator->sortedFreeRegionCount > 0)
	{
		region = allocator->sortedFreeRegions[0];
		allocation = region->allocation;

		alignedOffset = VULKAN_INTERNAL_NextHighestAlignment(
			region->offset,
			memoryRequirements->memoryRequirements.alignment
		);

		if (alignedOffset + requiredSize <= region->offset + region->size)
		{
			usedRegion = VULKAN_INTERNAL_NewMemoryUsedRegion(
				renderer,
				allocation,
				region->offset,
				requiredSize + (alignedOffset - region->offset),
				alignedOffset,
				resourceSize,
				memoryRequirements->memoryRequirements.alignment
			);

			usedRegion->isBuffer = buffer != VK_NULL_HANDLE;

			newRegionSize = region->size - ((alignedOffset - region->offset) + requiredSize);
			newRegionOffset = alignedOffset + requiredSize;

			/* remove and add modified region to re-sort */
			VULKAN_INTERNAL_RemoveMemoryFreeRegion(renderer, region);

			/* if size is 0, no need to re-insert */
			if (newRegionSize != 0)
			{
				VULKAN_INTERNAL_NewMemoryFreeRegion(
					renderer,
					allocation,
					newRegionOffset,
					newRegionSize
				);
			}

			SDL_UnlockMutex(renderer->allocatorLock);

			if (buffer != VK_NULL_HANDLE)
			{
				if (!VULKAN_INTERNAL_BindBufferMemory(
					renderer,
					usedRegion,
					alignedOffset,
					buffer
				)) {
					VULKAN_INTERNAL_RemoveMemoryUsedRegion(
						renderer,
						usedRegion
					);

					return 0;
				}
			}
			else if (image != VK_NULL_HANDLE)
			{
				if (!VULKAN_INTERNAL_BindImageMemory(
					renderer,
					usedRegion,
					alignedOffset,
					image
				)) {
					VULKAN_INTERNAL_RemoveMemoryUsedRegion(
						renderer,
						usedRegion
					);

					return 0;
				}
			}

			*pMemoryUsedRegion = usedRegion;
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
		/* allocate a page of required size aligned to ALLOCATION_INCREMENT increments */
		allocationSize =
			VULKAN_INTERNAL_NextHighestAlignment(requiredSize, ALLOCATION_INCREMENT);
	}
	else
	{
		allocationSize = allocator->nextAllocationSize;
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
		return 2;
	}

	usedRegion = VULKAN_INTERNAL_NewMemoryUsedRegion(
		renderer,
		allocation,
		0,
		requiredSize,
		0,
		resourceSize,
		memoryRequirements->memoryRequirements.alignment
	);

	usedRegion->isBuffer = buffer != VK_NULL_HANDLE;

	region = allocation->freeRegions[0];

	newRegionOffset = region->offset + requiredSize;
	newRegionSize = region->size - requiredSize;

	VULKAN_INTERNAL_RemoveMemoryFreeRegion(renderer, region);

	if (newRegionSize != 0)
	{
		VULKAN_INTERNAL_NewMemoryFreeRegion(
			renderer,
			allocation,
			newRegionOffset,
			newRegionSize
		);
	}

	SDL_UnlockMutex(renderer->allocatorLock);

	if (buffer != VK_NULL_HANDLE)
	{
		if (!VULKAN_INTERNAL_BindBufferMemory(
			renderer,
			usedRegion,
			0,
			buffer
		)) {
			VULKAN_INTERNAL_RemoveMemoryUsedRegion(
				renderer,
				usedRegion
			);

			return 0;
		}
	}
	else if (image != VK_NULL_HANDLE)
	{
		if (!VULKAN_INTERNAL_BindImageMemory(
			renderer,
			usedRegion,
			0,
			image
		)) {
			VULKAN_INTERNAL_RemoveMemoryUsedRegion(
				renderer,
				usedRegion
			);

			return 0;
		}
	}

	*pMemoryUsedRegion = usedRegion;
	return 1;
}

static uint8_t VULKAN_INTERNAL_BindMemoryForImage(
	VulkanRenderer* renderer,
	VkImage image,
	uint8_t isRenderTarget,
	VulkanMemoryUsedRegion** usedRegion
) {
	uint8_t bindResult = 0;
	uint32_t memoryTypeIndex = 0;
	VkMemoryPropertyFlags requiredMemoryPropertyFlags;
	VkMemoryPropertyFlags ignoredMemoryPropertyFlags;
	VkMemoryRequirements2KHR memoryRequirements =
	{
		VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR,
		NULL
	};

	/* Prefer GPU allocation */
	requiredMemoryPropertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	ignoredMemoryPropertyFlags = 0;

	while (VULKAN_INTERNAL_FindImageMemoryRequirements(
		renderer,
		image,
		requiredMemoryPropertyFlags,
		ignoredMemoryPropertyFlags,
		&memoryRequirements,
		&memoryTypeIndex
	)) {
		bindResult = VULKAN_INTERNAL_BindResourceMemory(
			renderer,
			memoryTypeIndex,
			&memoryRequirements,
			isRenderTarget,
			memoryRequirements.memoryRequirements.size,
			VK_NULL_HANDLE,
			image,
			usedRegion
		);

		if (bindResult == 1)
		{
			break;
		}
		else /* Bind failed, try the next device-local heap */
		{
			memoryTypeIndex += 1;
		}
	}

	/* Bind _still_ failed, try again without device local */
	if (bindResult != 1)
	{
		memoryTypeIndex = 0;
		requiredMemoryPropertyFlags = 0;
		ignoredMemoryPropertyFlags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;

		if (isRenderTarget)
		{
			Refresh_LogWarn("RenderTarget is allocated in host memory, pre-allocate your targets!");
		}

		Refresh_LogWarn("Out of device local memory, falling back to host memory");

		while (VULKAN_INTERNAL_FindImageMemoryRequirements(
			renderer,
			image,
			requiredMemoryPropertyFlags,
			ignoredMemoryPropertyFlags,
			&memoryRequirements,
			&memoryTypeIndex
		)) {
			bindResult = VULKAN_INTERNAL_BindResourceMemory(
				renderer,
				memoryTypeIndex,
				&memoryRequirements,
				isRenderTarget,
				memoryRequirements.memoryRequirements.size,
				VK_NULL_HANDLE,
				image,
				usedRegion
			);

			if (bindResult == 1)
			{
				break;
			}
			else /* Bind failed, try the next heap */
			{
				memoryTypeIndex += 1;
			}
		}
	}

	return bindResult;
}

static uint8_t VULKAN_INTERNAL_BindMemoryForBuffer(
	VulkanRenderer* renderer,
	VkBuffer buffer,
	VkDeviceSize size,
	uint8_t preferDeviceLocal,
	uint8_t dedicatedAllocation,
	VulkanMemoryUsedRegion** usedRegion
) {
	uint8_t bindResult = 0;
	uint32_t memoryTypeIndex = 0;
	VkMemoryPropertyFlags requiredMemoryPropertyFlags;
	VkMemoryPropertyFlags ignoredMemoryPropertyFlags;
	VkMemoryRequirements2KHR memoryRequirements =
	{
		VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR,
		NULL
	};

	requiredMemoryPropertyFlags =
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
		VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	if (preferDeviceLocal)
	{
		requiredMemoryPropertyFlags |=
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	}
	ignoredMemoryPropertyFlags = 0;

	while (VULKAN_INTERNAL_FindBufferMemoryRequirements(
		renderer,
		buffer,
		requiredMemoryPropertyFlags,
		ignoredMemoryPropertyFlags,
		&memoryRequirements,
		&memoryTypeIndex
	)) {
		bindResult = VULKAN_INTERNAL_BindResourceMemory(
			renderer,
			memoryTypeIndex,
			&memoryRequirements,
			dedicatedAllocation,
			size,
			buffer,
			VK_NULL_HANDLE,
			usedRegion
		);

		if (bindResult == 1)
		{
			break;
		}
		else /* Bind failed, try the next device-local heap */
		{
			memoryTypeIndex += 1;
		}
	}

	/* Bind failed, try again if originally preferred device local */
	if (bindResult != 1 && preferDeviceLocal)
	{
		memoryTypeIndex = 0;
		requiredMemoryPropertyFlags =
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

		/* Follow-up for the warning logged by FindMemoryType */
		if (!renderer->unifiedMemoryWarning)
		{
			Refresh_LogWarn("No unified memory found, falling back to host memory");
			renderer->unifiedMemoryWarning = 1;
		}

		while (VULKAN_INTERNAL_FindBufferMemoryRequirements(
			renderer,
			buffer,
			requiredMemoryPropertyFlags,
			ignoredMemoryPropertyFlags,
			&memoryRequirements,
			&memoryTypeIndex
		)) {
			bindResult = VULKAN_INTERNAL_BindResourceMemory(
				renderer,
				memoryTypeIndex,
				&memoryRequirements,
				dedicatedAllocation,
				size,
				buffer,
				VK_NULL_HANDLE,
				usedRegion
			);

			if (bindResult == 1)
			{
				break;
			}
			else /* Bind failed, try the next heap */
			{
				memoryTypeIndex += 1;
			}
		}
	}

	return bindResult;
}

static uint8_t VULKAN_INTERNAL_FindAllocationToDefragment(
	VulkanRenderer *renderer,
	VulkanMemorySubAllocator *allocator,
	uint32_t *allocationIndexToDefrag
) {
	uint32_t i, j;

	for (i = 0; i < VK_MAX_MEMORY_TYPES; i += 1)
	{
		*allocator = renderer->memoryAllocator->subAllocators[i];

		for (j = 0; j < allocator->allocationCount; j += 1)
		{
			if (allocator->allocations[j]->availableForAllocation == 1 && allocator->allocations[j]->freeRegionCount > 1)
			{
				*allocationIndexToDefrag = j;
				return 1;
			}
		}
	}

	return 0;
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

/* Resource tracking */

#define TRACK_RESOURCE(resource, type, array, count, capacity) \
	uint32_t i; \
	\
	for (i = 0; i < commandBuffer->count; i += 1) \
	{ \
		if (commandBuffer->array[i] == resource) \
		{ \
			return; \
		} \
	} \
	\
	if (commandBuffer->count == commandBuffer->capacity) \
	{ \
		commandBuffer->capacity += 1; \
		commandBuffer->array = SDL_realloc( \
			commandBuffer->array, \
			commandBuffer->capacity * sizeof(type) \
		); \
	} \
	commandBuffer->array[commandBuffer->count] = resource; \
	commandBuffer->count += 1; \
	\
	SDL_AtomicIncRef(&resource->referenceCount);


static void VULKAN_INTERNAL_TrackBuffer(
	VulkanRenderer *renderer,
	VulkanCommandBuffer *commandBuffer,
	VulkanBuffer *buffer
) {
	TRACK_RESOURCE(
		buffer,
		VulkanBuffer*,
		usedBuffers,
		usedBufferCount,
		usedBufferCapacity
	)
}

static void VULKAN_INTERNAL_TrackTexture(
	VulkanRenderer *renderer,
	VulkanCommandBuffer *commandBuffer,
	VulkanTexture *texture
) {
	TRACK_RESOURCE(
		texture,
		VulkanTexture*,
		usedTextures,
		usedTextureCount,
		usedTextureCapacity
	)
}

static void VULKAN_INTERNAL_TrackSampler(
	VulkanRenderer *renderer,
	VulkanCommandBuffer *commandBuffer,
	VulkanSampler *sampler
) {
	TRACK_RESOURCE(
		sampler,
		VulkanSampler*,
		usedSamplers,
		usedSamplerCount,
		usedSamplerCapacity
	)
}

static void VULKAN_INTERNAL_TrackGraphicsPipeline(
	VulkanRenderer *renderer,
	VulkanCommandBuffer *commandBuffer,
	VulkanGraphicsPipeline *graphicsPipeline
) {
	TRACK_RESOURCE(
		graphicsPipeline,
		VulkanGraphicsPipeline*,
		usedGraphicsPipelines,
		usedGraphicsPipelineCount,
		usedGraphicsPipelineCapacity
	)
}

static void VULKAN_INTERNAL_TrackComputePipeline(
	VulkanRenderer *renderer,
	VulkanCommandBuffer *commandBuffer,
	VulkanComputePipeline *computePipeline
) {
	TRACK_RESOURCE(
		computePipeline,
		VulkanComputePipeline*,
		usedComputePipelines,
		usedComputePipelineCount,
		usedComputePipelineCapacity
	)
}

static void VULKAN_INTERNAL_TrackFramebuffer(
	VulkanRenderer *renderer,
	VulkanCommandBuffer *commandBuffer,
	VulkanFramebuffer *framebuffer
) {
	TRACK_RESOURCE(
		framebuffer,
		VulkanFramebuffer*,
		usedFramebuffers,
		usedFramebufferCount,
		usedFramebufferCapacity
	);
}

#undef TRACK_RESOURCE

/* Resource Disposal */

static void VULKAN_INTERNAL_QueueDestroyFramebuffer(
	VulkanRenderer *renderer,
	VulkanFramebuffer *framebuffer
) {
	SDL_LockMutex(renderer->disposeLock);

	EXPAND_ARRAY_IF_NEEDED(
		renderer->framebuffersToDestroy,
		VulkanFramebuffer*,
		renderer->framebuffersToDestroyCount + 1,
		renderer->framebuffersToDestroyCapacity,
		renderer->framebuffersToDestroyCapacity * 2
	)

	renderer->framebuffersToDestroy[renderer->framebuffersToDestroyCount] = framebuffer;
	renderer->framebuffersToDestroyCount += 1;

	SDL_UnlockMutex(renderer->disposeLock);
}

static void VULKAN_INTERNAL_DestroyFramebuffer(
	VulkanRenderer *renderer,
	VulkanFramebuffer *framebuffer
) {
	renderer->vkDestroyFramebuffer(
		renderer->logicalDevice,
		framebuffer->framebuffer,
		NULL
	);

	SDL_free(framebuffer);
}

static void VULKAN_INTERNAL_RemoveFramebuffersContainingView(
	VulkanRenderer *renderer,
	VkImageView view
) {
	FramebufferHash *hash;
	int32_t i, j;

	for (i = renderer->framebufferHashArray.count - 1; i >= 0; i -= 1)
	{
		hash = &renderer->framebufferHashArray.elements[i].key;

		for (j = 0; j < hash->colorAttachmentCount; j += 1)
		{
			if (hash->colorAttachmentViews[j] == view)
			{
				VULKAN_INTERNAL_QueueDestroyFramebuffer(
					renderer,
					renderer->framebufferHashArray.elements[i].value
				);

				FramebufferHashArray_Remove(
					&renderer->framebufferHashArray,
					i
				);

				break;
			}
		}
	}
}

static void VULKAN_INTERNAL_RemoveRenderTargetsContainingTexture(
	VulkanRenderer *renderer,
	VulkanTexture *texture
) {
	RenderTargetHash *hash;
	VkImageView *viewsToCheck;
	int32_t viewsToCheckCount;
	int32_t viewsToCheckCapacity;
	int32_t i;

	viewsToCheckCapacity = 16;
	viewsToCheckCount = 0;
	viewsToCheck = SDL_malloc(sizeof(VkImageView) * viewsToCheckCapacity);

	SDL_LockMutex(renderer->renderTargetFetchLock);

	for (i = renderer->renderTargetHashArray.count - 1; i >= 0; i -= 1)
	{
		hash = &renderer->renderTargetHashArray.elements[i].key;

		if (hash->texture == texture)
		{
			EXPAND_ARRAY_IF_NEEDED(
				viewsToCheck,
				VkImageView,
				viewsToCheckCount + 1,
				viewsToCheckCapacity,
				viewsToCheckCapacity * 2
			);

			viewsToCheck[viewsToCheckCount] = renderer->renderTargetHashArray.elements[i].value->view;
			viewsToCheckCount += 1;

			VULKAN_INTERNAL_DestroyRenderTarget(
				renderer,
				renderer->renderTargetHashArray.elements[i].value
			);

			RenderTargetHash_Remove(
				&renderer->renderTargetHashArray,
				i
			);
		}
	}

	SDL_UnlockMutex(renderer->renderTargetFetchLock);

	SDL_LockMutex(renderer->framebufferFetchLock);

	for (i = 0; i < viewsToCheckCount; i += 1)
	{
		VULKAN_INTERNAL_RemoveFramebuffersContainingView(
			renderer,
			viewsToCheck[i]
		);
	}

	SDL_UnlockMutex(renderer->framebufferFetchLock);

	SDL_free(viewsToCheck);
}

static void VULKAN_INTERNAL_DestroyTexture(
	VulkanRenderer* renderer,
	VulkanTexture* texture
) {
	VULKAN_INTERNAL_RemoveRenderTargetsContainingTexture(
		renderer,
		texture
	);

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

	VULKAN_INTERNAL_RemoveMemoryUsedRegion(
		renderer,
		texture->usedRegion
	);

	/* destroy the msaa texture, if there is one */
	if (texture->msaaTex != NULL)
	{
		VULKAN_INTERNAL_DestroyTexture(
			renderer,
			texture->msaaTex
		);
	}

	SDL_free(texture);
}

static void VULKAN_INTERNAL_DestroyRenderTarget(
	VulkanRenderer *renderer,
	VulkanRenderTarget *renderTarget
) {
	renderer->vkDestroyImageView(
		renderer->logicalDevice,
		renderTarget->view,
		NULL
	);

	SDL_free(renderTarget);
}

static void VULKAN_INTERNAL_DestroyBuffer(
	VulkanRenderer* renderer,
	VulkanBuffer* buffer
) {
	renderer->vkDestroyBuffer(
		renderer->logicalDevice,
		buffer->buffer,
		NULL
	);

	VULKAN_INTERNAL_RemoveMemoryUsedRegion(
		renderer,
		buffer->usedRegion
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

		SDL_free(commandBuffer->presentDatas);
		SDL_free(commandBuffer->waitSemaphores);
		SDL_free(commandBuffer->signalSemaphores);
		SDL_free(commandBuffer->transferBuffers);
		SDL_free(commandBuffer->boundUniformBuffers);
		SDL_free(commandBuffer->boundDescriptorSetDatas);
		SDL_free(commandBuffer->boundComputeBuffers);
		SDL_free(commandBuffer->boundComputeTextures);
		SDL_free(commandBuffer->usedBuffers);
		SDL_free(commandBuffer->usedTextures);
		SDL_free(commandBuffer->usedSamplers);
		SDL_free(commandBuffer->usedGraphicsPipelines);
		SDL_free(commandBuffer->usedComputePipelines);
		SDL_free(commandBuffer->usedFramebuffers);

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

	SDL_AtomicDecRef(&graphicsPipeline->vertexShaderModule->referenceCount);
	SDL_AtomicDecRef(&graphicsPipeline->fragmentShaderModule->referenceCount);

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

	SDL_AtomicDecRef(&computePipeline->computeShaderModule->referenceCount);

	SDL_free(computePipeline);
}

static void VULKAN_INTERNAL_DestroyShaderModule(
	VulkanRenderer *renderer,
	VulkanShaderModule *vulkanShaderModule
) {
	renderer->vkDestroyShaderModule(
		renderer->logicalDevice,
		vulkanShaderModule->shaderModule,
		NULL
	);

	SDL_free(vulkanShaderModule);
}

static void VULKAN_INTERNAL_DestroySampler(
	VulkanRenderer *renderer,
	VulkanSampler *vulkanSampler
) {
	renderer->vkDestroySampler(
		renderer->logicalDevice,
		vulkanSampler->sampler,
		NULL
	);

	SDL_free(vulkanSampler);
}

static void VULKAN_INTERNAL_DestroySwapchain(
	VulkanRenderer* renderer,
	WindowData *windowData
) {
	uint32_t i;
	VulkanSwapchainData *swapchainData;

	if (windowData == NULL)
	{
		return;
	}

	swapchainData = windowData->swapchainData;

	if (swapchainData == NULL)
	{
		return;
	}

	for (i = 0; i < swapchainData->imageCount; i += 1)
	{
		VULKAN_INTERNAL_RemoveRenderTargetsContainingTexture(
			renderer,
			swapchainData->textureContainers[i].vulkanTexture
		);

		renderer->vkDestroyImageView(
			renderer->logicalDevice,
			swapchainData->textureContainers[i].vulkanTexture->view,
			NULL
		);

		SDL_free(swapchainData->textureContainers[i].vulkanTexture);
	}

	SDL_free(swapchainData->textureContainers);

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

	windowData->swapchainData = NULL;
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
	VkBufferUsageFlags usage,
	uint8_t preferDeviceLocal,
	uint8_t dedicatedAllocation
) {
	VulkanBuffer* buffer;
	VkResult vulkanResult;
	VkBufferCreateInfo bufferCreateInfo;
	uint8_t bindResult;

	buffer = SDL_malloc(sizeof(VulkanBuffer));

	buffer->size = size;
	buffer->resourceAccessType = resourceAccessType;
	buffer->usage = usage;
	buffer->preferDeviceLocal = preferDeviceLocal;

	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.pNext = NULL;
	bufferCreateInfo.flags = 0;
	bufferCreateInfo.size = size;
	bufferCreateInfo.usage = usage;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	bufferCreateInfo.queueFamilyIndexCount = 1;
	bufferCreateInfo.pQueueFamilyIndices = &renderer->queueFamilyIndex;

	vulkanResult = renderer->vkCreateBuffer(
		renderer->logicalDevice,
		&bufferCreateInfo,
		NULL,
		&buffer->buffer
	);
	VULKAN_ERROR_CHECK(vulkanResult, vkCreateBuffer, 0)

	bindResult = VULKAN_INTERNAL_BindMemoryForBuffer(
		renderer,
		buffer->buffer,
		buffer->size,
		buffer->preferDeviceLocal,
		dedicatedAllocation,
		&buffer->usedRegion
	);

	if (bindResult != 1)
	{
		renderer->vkDestroyBuffer(
			renderer->logicalDevice,
			buffer->buffer,
			NULL);

		return NULL;
	}

	buffer->usedRegion->vulkanBuffer = buffer; /* lol */
	buffer->container = NULL;

	buffer->resourceAccessType = resourceAccessType;

	SDL_AtomicSet(&buffer->referenceCount, 0);

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
	VulkanResourceAccessType resourceAccessType;

	if (uniformBufferType == UNIFORM_BUFFER_VERTEX)
	{
		resourceAccessType = RESOURCE_ACCESS_VERTEX_SHADER_READ_UNIFORM_BUFFER;
	}
	else if (uniformBufferType == UNIFORM_BUFFER_FRAGMENT)
	{
		resourceAccessType = RESOURCE_ACCESS_FRAGMENT_SHADER_READ_UNIFORM_BUFFER;
	}
	else if (uniformBufferType == UNIFORM_BUFFER_COMPUTE)
	{
		resourceAccessType = RESOURCE_ACCESS_COMPUTE_SHADER_READ_UNIFORM_BUFFER;
	}
	else
	{
		Refresh_LogError("Unrecognized uniform buffer type!");
		return 0;
	}

	uniformBufferPool->buffer = VULKAN_INTERNAL_CreateBuffer(
		renderer,
		UBO_BUFFER_SIZE,
		resourceAccessType,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		0,
		1
	);

	uniformBufferPool->nextAvailableOffset = 0;

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
	VulkanRenderer *renderer,
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

/* Buffer indirection so we can cleanly defrag */
static VulkanBufferContainer* VULKAN_INTERNAL_CreateBufferContainer(
	VulkanRenderer *renderer,
	uint32_t sizeInBytes,
	VulkanResourceAccessType resourceAccessType,
	VkBufferUsageFlags usageFlags,
	uint8_t dedicated
) {
	VulkanBufferContainer* bufferContainer;
	VulkanBuffer* buffer;

	/* always set transfer bits so we can defrag */
	usageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	buffer = VULKAN_INTERNAL_CreateBuffer(
		renderer,
		sizeInBytes,
		resourceAccessType,
		usageFlags,
		1,
		dedicated
	);

	if (buffer == NULL)
	{
		Refresh_LogError("Failed to create buffer!");
		return NULL;
	}

	bufferContainer = SDL_malloc(sizeof(VulkanBufferContainer));
	bufferContainer->vulkanBuffer = buffer;
	buffer->container = bufferContainer;

	return (VulkanBufferContainer*) bufferContainer;
}

static uint8_t VULKAN_INTERNAL_CreateUniformBuffer(
	VulkanRenderer *renderer,
	VulkanUniformBufferPool *bufferPool
) {
	VkDescriptorSetLayout descriptorSetLayout;

	if (bufferPool->type == UNIFORM_BUFFER_VERTEX)
	{
		descriptorSetLayout = renderer->vertexUniformDescriptorSetLayout;
	}
	else if (bufferPool->type == UNIFORM_BUFFER_FRAGMENT)
	{
		descriptorSetLayout = renderer->fragmentUniformDescriptorSetLayout;
	}
	else if (bufferPool->type == UNIFORM_BUFFER_COMPUTE)
	{
		descriptorSetLayout = renderer->computeUniformDescriptorSetLayout;
	}
	else
	{
		Refresh_LogError("Unrecognized uniform buffer type!");
		return 0;
	}

	VulkanUniformBuffer *uniformBuffer = SDL_malloc(sizeof(VulkanUniformBuffer));
	uniformBuffer->pool = bufferPool;
	uniformBuffer->poolOffset = bufferPool->nextAvailableOffset;
	uniformBuffer->offset = 0;

	bufferPool->nextAvailableOffset += VULKAN_INTERNAL_NextHighestAlignment(UBO_SECTION_SIZE, renderer->minUBOAlignment);

	if (bufferPool->nextAvailableOffset >= UBO_BUFFER_SIZE)
	{
		Refresh_LogError("Uniform buffer overflow!");
		SDL_free(uniformBuffer);
		return 0;
	}

	/* Allocate a descriptor set for the uniform buffer */

	if (bufferPool->descriptorPool.availableDescriptorSetCount == 0)
	{
		if (!VULKAN_INTERNAL_AddUniformDescriptorPool(
			renderer,
			&bufferPool->descriptorPool
		)) {
			Refresh_LogError("Failed to add uniform descriptor pool!");
			SDL_free(uniformBuffer);
			return 0;
		}
	}

	if (!VULKAN_INTERNAL_AllocateDescriptorSets(
		renderer,
		bufferPool->descriptorPool.descriptorPools[bufferPool->descriptorPool.descriptorPoolCount - 1],
		descriptorSetLayout,
		1,
		&uniformBuffer->descriptorSet
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

	bufferPool->availableBuffers[bufferPool->availableBufferCount] = uniformBuffer;
	bufferPool->availableBufferCount += 1;

	return 1;
}

static VulkanUniformBuffer* VULKAN_INTERNAL_CreateDummyUniformBuffer(
	VulkanRenderer *renderer,
	VulkanUniformBufferType uniformBufferType
) {
	VkDescriptorSetLayout descriptorSetLayout;
	VkWriteDescriptorSet writeDescriptorSet;
	VkDescriptorBufferInfo descriptorBufferInfo;

	if (uniformBufferType == UNIFORM_BUFFER_VERTEX)
	{
		descriptorSetLayout = renderer->vertexUniformDescriptorSetLayout;
	}
	else if (uniformBufferType == UNIFORM_BUFFER_FRAGMENT)
	{
		descriptorSetLayout = renderer->fragmentUniformDescriptorSetLayout;
	}
	else if (uniformBufferType == UNIFORM_BUFFER_COMPUTE)
	{
		descriptorSetLayout = renderer->computeUniformDescriptorSetLayout;
	}
	else
	{
		Refresh_LogError("Unrecognized uniform buffer type!");
		return NULL;
	}

	VulkanUniformBuffer *uniformBuffer = SDL_malloc(sizeof(VulkanUniformBuffer));
	uniformBuffer->poolOffset = 0;
	uniformBuffer->offset = 0;

	/* Allocate a descriptor set for the uniform buffer */

	VULKAN_INTERNAL_AllocateDescriptorSets(
		renderer,
		renderer->defaultDescriptorPool,
		descriptorSetLayout,
		1,
		&uniformBuffer->descriptorSet
	);

	/* Update the descriptor set for the first and last time! */

	descriptorBufferInfo.buffer = renderer->dummyBuffer->buffer;
	descriptorBufferInfo.offset = 0;
	descriptorBufferInfo.range = VK_WHOLE_SIZE;

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

	uniformBuffer->pool = NULL; /* No pool because this is a dummy */

	return uniformBuffer;
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
		SDL_free(uniformBufferPool->availableBuffers[i]);
	}

	VULKAN_INTERNAL_DestroyBuffer(renderer, uniformBufferPool->buffer);

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
		if (!VULKAN_INTERNAL_CreateUniformBuffer(renderer, bufferPool))
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

	descriptorBufferInfo.buffer = uniformBuffer->pool->buffer->buffer;
	descriptorBufferInfo.offset = uniformBuffer->poolOffset;
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
	SwapChainSupportDetails *outputDetails
) {
	VkResult result;
	VkBool32 supportsPresent;

	renderer->vkGetPhysicalDeviceSurfaceSupportKHR(
		physicalDevice,
		renderer->queueFamilyIndex,
		surface,
		&supportsPresent
	);

	if (!supportsPresent)
	{
		Refresh_LogWarn("This surface does not support presenting!");
		return 0;
	}

	/* Initialize these in case anything fails */
	outputDetails->formatsLength = 0;
	outputDetails->presentModesLength = 0;

	/* Run the device surface queries */
	result = renderer->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
		physicalDevice,
		surface,
		&outputDetails->capabilities
	);
	VULKAN_ERROR_CHECK(result, vkGetPhysicalDeviceSurfaceCapabilitiesKHR, 0)

	if (!(outputDetails->capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR))
	{
		Refresh_LogWarn("Opaque presentation unsupported! Expect weird transparency bugs!");
	}

	result = renderer->vkGetPhysicalDeviceSurfaceFormatsKHR(
		physicalDevice,
		surface,
		&outputDetails->formatsLength,
		NULL
	);
	VULKAN_ERROR_CHECK(result, vkGetPhysicalDeviceSurfaceFormatsKHR, 0)
	result = renderer->vkGetPhysicalDeviceSurfacePresentModesKHR(
		physicalDevice,
		surface,
		&outputDetails->presentModesLength,
		NULL
	);
	VULKAN_ERROR_CHECK(result, vkGetPhysicalDeviceSurfacePresentModesKHR, 0)

	/* Generate the arrays, if applicable */
	if (outputDetails->formatsLength != 0)
	{
		outputDetails->formats = (VkSurfaceFormatKHR*) SDL_malloc(
			sizeof(VkSurfaceFormatKHR) * outputDetails->formatsLength
		);

		if (!outputDetails->formats)
		{
			SDL_OutOfMemory();
			return 0;
		}

		result = renderer->vkGetPhysicalDeviceSurfaceFormatsKHR(
			physicalDevice,
			surface,
			&outputDetails->formatsLength,
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
	if (outputDetails->presentModesLength != 0)
	{
		outputDetails->presentModes = (VkPresentModeKHR*) SDL_malloc(
			sizeof(VkPresentModeKHR) * outputDetails->presentModesLength
		);

		if (!outputDetails->presentModes)
		{
			SDL_OutOfMemory();
			return 0;
		}

		result = renderer->vkGetPhysicalDeviceSurfacePresentModesKHR(
			physicalDevice,
			surface,
			&outputDetails->presentModesLength,
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

	/* If we made it here, all the queries were successfull. This does NOT
	 * necessarily mean there are any supported formats or present modes!
	 */
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
				return 1; \
			} \
		} \

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

	*outputPresentMode = VK_PRESENT_MODE_FIFO_KHR;
	return 1;
}

static uint8_t VULKAN_INTERNAL_CreateSwapchain(
	VulkanRenderer *renderer,
	WindowData *windowData
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

	/* Each swapchain must have its own surface. */

	if (!SDL_Vulkan_CreateSurface(
		(SDL_Window*) windowData->windowHandle,
		renderer->instance,
		&swapchainData->surface
	)) {
		SDL_free(swapchainData);
		Refresh_LogError(
			"SDL_Vulkan_CreateSurface failed: %s",
			SDL_GetError()
		);
		return 0;
	}

	if (!VULKAN_INTERNAL_QuerySwapChainSupport(
		renderer,
		renderer->physicalDevice,
		swapchainData->surface,
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
		return 0;
	}

	if (	swapchainSupportDetails.capabilities.currentExtent.width == 0 ||
		swapchainSupportDetails.capabilities.currentExtent.height == 0)
	{
		/* Not an error, just minimize behavior! */
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
		return 0;
	}

	swapchainData->swapchainFormat = VK_FORMAT_R8G8B8A8_UNORM;
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
		swapchainData->swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
		swapchainData->swapchainSwizzle.r = VK_COMPONENT_SWIZZLE_B;
		swapchainData->swapchainSwizzle.g = VK_COMPONENT_SWIZZLE_G;
		swapchainData->swapchainSwizzle.b = VK_COMPONENT_SWIZZLE_R;
		swapchainData->swapchainSwizzle.a = VK_COMPONENT_SWIZZLE_A;

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
			return 0;
		}
	}

	if (!VULKAN_INTERNAL_ChooseSwapPresentMode(
		windowData->preferredPresentMode,
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
		return 0;
	}

	SDL_Vulkan_GetDrawableSize(
		(SDL_Window*) windowData->windowHandle,
		&drawableWidth,
		&drawableHeight
	);

	if (	drawableWidth < swapchainSupportDetails.capabilities.minImageExtent.width ||
		drawableWidth > swapchainSupportDetails.capabilities.maxImageExtent.width ||
		drawableHeight < swapchainSupportDetails.capabilities.minImageExtent.height ||
		drawableHeight > swapchainSupportDetails.capabilities.maxImageExtent.height	)
	{
		if (swapchainSupportDetails.capabilities.currentExtent.width != UINT32_MAX)
		{
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
			return 0;
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
	swapchainCreateInfo.imageUsage =
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT;
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
		return 0;
	}

	renderer->vkGetSwapchainImagesKHR(
		renderer->logicalDevice,
		swapchainData->swapchain,
		&swapchainData->imageCount,
		NULL
	);

	swapchainData->textureContainers = SDL_malloc(
		sizeof(VulkanTextureContainer) * swapchainData->imageCount
	);

	if (!swapchainData->textureContainers)
	{
		SDL_OutOfMemory();
		renderer->vkDestroySurfaceKHR(
			renderer->instance,
			swapchainData->surface,
			NULL
		);
		SDL_free(swapchainData);
		return 0;
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
		swapchainData->textureContainers[i].vulkanTexture = SDL_malloc(sizeof(VulkanTexture));

		swapchainData->textureContainers[i].vulkanTexture->image = swapchainImages[i];

		imageViewCreateInfo.image = swapchainImages[i];

		vulkanResult = renderer->vkCreateImageView(
			renderer->logicalDevice,
			&imageViewCreateInfo,
			NULL,
			&swapchainData->textureContainers[i].vulkanTexture->view
		);

		if (vulkanResult != VK_SUCCESS)
		{
			renderer->vkDestroySurfaceKHR(
				renderer->instance,
				swapchainData->surface,
				NULL
			);
			SDL_stack_free(swapchainImages);
			SDL_free(swapchainData->textureContainers);
			SDL_free(swapchainData);
			LogVulkanResultAsError("vkCreateImageView", vulkanResult);
			return 0;
		}

		swapchainData->textureContainers[i].vulkanTexture->resourceAccessType = RESOURCE_ACCESS_NONE;

		/* Swapchain memory is managed by the driver */
		swapchainData->textureContainers[i].vulkanTexture->usedRegion = NULL;

		swapchainData->textureContainers[i].vulkanTexture->dimensions = swapchainData->extent;
		swapchainData->textureContainers[i].vulkanTexture->format = swapchainData->swapchainFormat;
		swapchainData->textureContainers[i].vulkanTexture->is3D = 0;
		swapchainData->textureContainers[i].vulkanTexture->isCube = 0;
		swapchainData->textureContainers[i].vulkanTexture->layerCount = 1;
		swapchainData->textureContainers[i].vulkanTexture->levelCount = 1;
		swapchainData->textureContainers[i].vulkanTexture->sampleCount = REFRESH_SAMPLECOUNT_1;
		swapchainData->textureContainers[i].vulkanTexture->usageFlags =
			VK_IMAGE_USAGE_TRANSFER_DST_BIT |
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		swapchainData->textureContainers[i].vulkanTexture->aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
		swapchainData->textureContainers[i].vulkanTexture->resourceAccessType = RESOURCE_ACCESS_NONE;
		swapchainData->textureContainers[i].vulkanTexture->msaaTex = NULL;
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

	windowData->swapchainData = swapchainData;
	return 1;
}

static void VULKAN_INTERNAL_RecreateSwapchain(
	VulkanRenderer* renderer,
	WindowData *windowData
) {
	VULKAN_Wait((Refresh_Renderer*) renderer);
	VULKAN_INTERNAL_DestroySwapchain(renderer, windowData);
	VULKAN_INTERNAL_CreateSwapchain(renderer, windowData);
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
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

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
			renderer,
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

	for (i = renderer->claimedWindowCount - 1; i >= 0; i -= 1)
	{
		VULKAN_UnclaimWindow(device->driverData, renderer->claimedWindows[i]->windowHandle);
	}

	SDL_free(renderer->claimedWindows);

	VULKAN_Wait(device->driverData);

	SDL_free(renderer->submittedCommandBuffers);

	VULKAN_INTERNAL_DestroyBuffer(renderer, renderer->dummyBuffer);

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

	for (i = 0; i < renderer->fencePool.availableFenceCount; i += 1)
	{
		renderer->vkDestroyFence(renderer->logicalDevice, renderer->fencePool.availableFences[i], NULL);
	}

	SDL_free(renderer->fencePool.availableFences);
	SDL_DestroyMutex(renderer->fencePool.lock);

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

	VULKAN_INTERNAL_DestroyUniformBufferPool(renderer, renderer->vertexUniformBufferPool);
	VULKAN_INTERNAL_DestroyUniformBufferPool(renderer, renderer->fragmentUniformBufferPool);
	VULKAN_INTERNAL_DestroyUniformBufferPool(renderer, renderer->computeUniformBufferPool);

	for (i = 0; i < renderer->framebufferHashArray.count; i += 1)
	{
		VULKAN_INTERNAL_DestroyFramebuffer(
			renderer,
			renderer->framebufferHashArray.elements[i].value
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

	SDL_free(renderer->renderTargetHashArray.elements);

	for (i = 0; i < VK_MAX_MEMORY_TYPES; i += 1)
	{
		allocator = &renderer->memoryAllocator->subAllocators[i];

		for (j = allocator->allocationCount - 1; j >= 0; j -= 1)
		{
			for (k = allocator->allocations[j]->usedRegionCount - 1; k >= 0; k -= 1)
			{
				VULKAN_INTERNAL_RemoveMemoryUsedRegion(
					renderer,
					allocator->allocations[j]->usedRegions[k]
				);
			}

			VULKAN_INTERNAL_DeallocateMemory(
				renderer,
				allocator,
				j
			);
		}

		if (renderer->memoryAllocator->subAllocators[i].allocations != NULL)
		{
			SDL_free(renderer->memoryAllocator->subAllocators[i].allocations);
		}

		SDL_free(renderer->memoryAllocator->subAllocators[i].sortedFreeRegions);
	}

	SDL_free(renderer->memoryAllocator);

	SDL_free(renderer->texturesToDestroy);
	SDL_free(renderer->buffersToDestroy);
	SDL_free(renderer->graphicsPipelinesToDestroy);
	SDL_free(renderer->computePipelinesToDestroy);
	SDL_free(renderer->shaderModulesToDestroy);
	SDL_free(renderer->samplersToDestroy);
	SDL_free(renderer->framebuffersToDestroy);

	SDL_DestroyMutex(renderer->allocatorLock);
	SDL_DestroyMutex(renderer->disposeLock);
	SDL_DestroyMutex(renderer->submitLock);
	SDL_DestroyMutex(renderer->acquireCommandBufferLock);
	SDL_DestroyMutex(renderer->renderPassFetchLock);
	SDL_DestroyMutex(renderer->framebufferFetchLock);
	SDL_DestroyMutex(renderer->renderTargetFetchLock);

	renderer->vkDestroyDevice(renderer->logicalDevice, NULL);
	renderer->vkDestroyInstance(renderer->instance, NULL);

	SDL_free(renderer);
	SDL_free(device);
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

static void VULKAN_DrawPrimitivesIndirect(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Buffer *buffer,
	uint32_t offsetInBytes,
	uint32_t drawCount,
	uint32_t stride,
	uint32_t vertexParamOffset,
	uint32_t fragmentParamOffset
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	VulkanBuffer *vulkanBuffer = ((VulkanBufferContainer*) buffer)->vulkanBuffer;
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

	renderer->vkCmdDrawIndirect(
		vulkanCommandBuffer->commandBuffer,
		vulkanBuffer->buffer,
		offsetInBytes,
		drawCount,
		stride
	);

	VULKAN_INTERNAL_TrackBuffer(renderer, vulkanCommandBuffer, vulkanBuffer);
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
	VulkanResourceAccessType resourceAccessType = RESOURCE_ACCESS_NONE;
	VulkanBuffer *currentComputeBuffer;
	VulkanTexture *currentComputeTexture;
	uint32_t i;

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

	/* Re-transition buffers after dispatch */
	for (i = 0; i < vulkanCommandBuffer->boundComputeBufferCount; i += 1)
	{
		currentComputeBuffer = vulkanCommandBuffer->boundComputeBuffers[i];

		if (currentComputeBuffer->usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
		{
			resourceAccessType = RESOURCE_ACCESS_VERTEX_BUFFER;
		}
		else if (currentComputeBuffer->usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
		{
			resourceAccessType = RESOURCE_ACCESS_INDEX_BUFFER;
		}
		else if (currentComputeBuffer->usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
		{
			resourceAccessType = RESOURCE_ACCESS_INDIRECT_BUFFER;
		}

		if (resourceAccessType != RESOURCE_ACCESS_NONE)
		{
			VULKAN_INTERNAL_BufferMemoryBarrier(
				renderer,
				vulkanCommandBuffer->commandBuffer,
				resourceAccessType,
				currentComputeBuffer
			);
		}
	}

	vulkanCommandBuffer->boundComputeBufferCount = 0;

	/* Re-transition sampler images after dispatch */
	for (i = 0; i < vulkanCommandBuffer->boundComputeTextureCount; i += 1)
	{
		currentComputeTexture = vulkanCommandBuffer->boundComputeTextures[i];

		if (currentComputeTexture->usageFlags & VK_IMAGE_USAGE_SAMPLED_BIT)
		{
			resourceAccessType = RESOURCE_ACCESS_ANY_SHADER_READ_SAMPLED_IMAGE;

			VULKAN_INTERNAL_ImageMemoryBarrier(
				renderer,
				vulkanCommandBuffer->commandBuffer,
				resourceAccessType,
				currentComputeTexture->aspectFlags,
				0,
				currentComputeTexture->layerCount,
				0,
				currentComputeTexture->levelCount,
				0,
				currentComputeTexture->image,
				&currentComputeTexture->resourceAccessType
			);
		}
	}

	vulkanCommandBuffer->boundComputeTextureCount = 0;
}

static VulkanTexture* VULKAN_INTERNAL_CreateTexture(
	VulkanRenderer *renderer,
	uint32_t width,
	uint32_t height,
	uint32_t depth,
	uint32_t isCube,
	uint32_t levelCount,
	Refresh_SampleCount sampleCount,
	VkFormat format,
	VkImageAspectFlags aspectMask,
	VkImageUsageFlags imageUsageFlags
) {
	VkResult vulkanResult;
	VkImageCreateInfo imageCreateInfo;
	VkImageCreateFlags imageCreateFlags = 0;
	VkImageViewCreateInfo imageViewCreateInfo;
	uint8_t bindResult;
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
	imageCreateInfo.imageType = is3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
	imageCreateInfo.format = format;
	imageCreateInfo.extent.width = width;
	imageCreateInfo.extent.height = height;
	imageCreateInfo.extent.depth = depth;
	imageCreateInfo.mipLevels = levelCount;
	imageCreateInfo.arrayLayers = layerCount;
	imageCreateInfo.samples = RefreshToVK_SampleCount[sampleCount];
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
	VULKAN_ERROR_CHECK(vulkanResult, vkCreateImage, 0)

	bindResult = VULKAN_INTERNAL_BindMemoryForImage(
		renderer,
		texture->image,
		isRenderTarget,
		&texture->usedRegion
	);

	if (bindResult != 1)
	{
		renderer->vkDestroyImage(
			renderer->logicalDevice,
			texture->image,
			NULL);

		Refresh_LogError("Unable to bind memory for texture!");
		return NULL;
	}

	texture->usedRegion->vulkanTexture = texture; /* lol */

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
	else if (is3D)
	{
		imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
	}
	else
	{
		imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
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
		return NULL;
	}

	texture->dimensions.width = width;
	texture->dimensions.height = height;
	texture->depth = depth;
	texture->format = format;
	texture->levelCount = levelCount;
	texture->layerCount = layerCount;
	texture->sampleCount = sampleCount;
	texture->resourceAccessType = RESOURCE_ACCESS_NONE;
	texture->usageFlags = imageUsageFlags;
	texture->aspectFlags = aspectMask;
	texture->msaaTex = NULL;

	SDL_AtomicSet(&texture->referenceCount, 0);

	return texture;
}

static VulkanRenderTarget* VULKAN_INTERNAL_CreateRenderTarget(
	VulkanRenderer *renderer,
	VulkanTexture *texture,
	uint32_t depth,
	uint32_t layer,
	uint32_t level
) {
	VkResult vulkanResult;
	VulkanRenderTarget *renderTarget = (VulkanRenderTarget*) SDL_malloc(sizeof(VulkanRenderTarget));
	VkImageViewCreateInfo imageViewCreateInfo;
	VkComponentMapping swizzle = IDENTITY_SWIZZLE;
	VkImageAspectFlags aspectFlags = 0;

	if (IsDepthFormat(texture->format))
	{
		aspectFlags |= VK_IMAGE_ASPECT_DEPTH_BIT;

		if (IsStencilFormat(texture->format))
		{
			aspectFlags |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
	}
	else
	{
		aspectFlags |= VK_IMAGE_ASPECT_COLOR_BIT;
	}

	/* create framebuffer compatible views for RenderTarget */
	imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imageViewCreateInfo.pNext = NULL;
	imageViewCreateInfo.flags = 0;
	imageViewCreateInfo.image = texture->image;
	imageViewCreateInfo.format = texture->format;
	imageViewCreateInfo.components = swizzle;
	imageViewCreateInfo.subresourceRange.aspectMask = aspectFlags;
	imageViewCreateInfo.subresourceRange.baseMipLevel = level;
	imageViewCreateInfo.subresourceRange.levelCount = 1;
	imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
	if (texture->is3D)
	{
		imageViewCreateInfo.subresourceRange.baseArrayLayer = depth;
	}
	else if (texture->isCube)
	{
		imageViewCreateInfo.subresourceRange.baseArrayLayer = layer;
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

	return renderTarget;
}

static VulkanRenderTarget* VULKAN_INTERNAL_FetchRenderTarget(
	VulkanRenderer *renderer,
	VulkanTexture *texture,
	uint32_t depth,
	uint32_t layer,
	uint32_t level
) {
	RenderTargetHash hash;
	VulkanRenderTarget *renderTarget;

	hash.texture = texture;
	hash.depth = depth;
	hash.layer = layer;
	hash.level = level;

	SDL_LockMutex(renderer->renderTargetFetchLock);

	renderTarget = RenderTargetHash_Fetch(
		&renderer->renderTargetHashArray,
		&hash
	);

	if (renderTarget == NULL)
	{
		renderTarget = VULKAN_INTERNAL_CreateRenderTarget(
			renderer,
			texture,
			depth,
			layer,
			level
		);

		RenderTargetHash_Insert(
			&renderer->renderTargetHashArray,
			hash,
			renderTarget
		);
	}

	SDL_UnlockMutex(renderer->renderTargetFetchLock);

	return renderTarget;
}

static VkRenderPass VULKAN_INTERNAL_CreateRenderPass(
	VulkanRenderer *renderer,
	VulkanCommandBuffer *commandBuffer,
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

	uint32_t attachmentDescriptionCount = 0;
	uint32_t colorAttachmentReferenceCount = 0;
	uint32_t resolveReferenceCount = 0;

	VulkanTexture *texture;
	VulkanTexture *msaaTexture = NULL;

	for (i = 0; i < colorAttachmentCount; i += 1)
	{
		texture = ((VulkanTextureContainer*) colorAttachmentInfos[i].texture)->vulkanTexture;

		if (texture->msaaTex != NULL)
		{
			msaaTexture = texture->msaaTex;

			/* Transition the multisample attachment */

			VULKAN_INTERNAL_ImageMemoryBarrier(
				renderer,
				commandBuffer->commandBuffer,
				RESOURCE_ACCESS_COLOR_ATTACHMENT_WRITE,
				VK_IMAGE_ASPECT_COLOR_BIT,
				0,
				msaaTexture->layerCount,
				0,
				msaaTexture->levelCount,
				0,
				msaaTexture->image,
				&msaaTexture->resourceAccessType
			);

			/* Resolve attachment and multisample attachment */

			attachmentDescriptions[attachmentDescriptionCount].flags = 0;
			attachmentDescriptions[attachmentDescriptionCount].format = texture->format;
			attachmentDescriptions[attachmentDescriptionCount].samples =
				VK_SAMPLE_COUNT_1_BIT;
			attachmentDescriptions[attachmentDescriptionCount].loadOp = RefreshToVK_LoadOp[
				colorAttachmentInfos[i].loadOp
			];
			attachmentDescriptions[attachmentDescriptionCount].storeOp =
				VK_ATTACHMENT_STORE_OP_STORE; /* Always store the resolve texture */
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
			attachmentDescriptions[attachmentDescriptionCount].format = msaaTexture->format;
			attachmentDescriptions[attachmentDescriptionCount].samples = RefreshToVK_SampleCount[
				msaaTexture->sampleCount
			];
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
			attachmentDescriptions[attachmentDescriptionCount].format = texture->format;
			attachmentDescriptions[attachmentDescriptionCount].samples =
				VK_SAMPLE_COUNT_1_BIT;
			attachmentDescriptions[attachmentDescriptionCount].loadOp = RefreshToVK_LoadOp[
				colorAttachmentInfos[i].loadOp
			];
			attachmentDescriptions[attachmentDescriptionCount].storeOp =
				VK_ATTACHMENT_STORE_OP_STORE; /* Always store non-MSAA textures */
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
		texture = ((VulkanTextureContainer*) depthStencilAttachmentInfo->texture)->vulkanTexture;

		attachmentDescriptions[attachmentDescriptionCount].flags = 0;
		attachmentDescriptions[attachmentDescriptionCount].format = texture->format;
		attachmentDescriptions[attachmentDescriptionCount].samples = RefreshToVK_SampleCount[
			texture->sampleCount
		];
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

	if (msaaTexture != NULL)
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
	Refresh_GraphicsPipelineAttachmentInfo attachmentInfo,
	Refresh_SampleCount sampleCount
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

		if (sampleCount > REFRESH_SAMPLECOUNT_1)
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
			attachmentDescriptions[attachmentDescriptionCount].samples = RefreshToVK_SampleCount[
				sampleCount
			];
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
		attachmentDescriptions[attachmentDescriptionCount].format = RefreshToVK_DepthFormat(
			renderer,
			attachmentInfo.depthStencilFormat
		);
		attachmentDescriptions[attachmentDescriptionCount].samples = RefreshToVK_SampleCount[
			sampleCount
		];
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
	Refresh_SampleCount actualSampleCount;

	VulkanGraphicsPipeline *graphicsPipeline = (VulkanGraphicsPipeline*) SDL_malloc(sizeof(VulkanGraphicsPipeline));
	VkGraphicsPipelineCreateInfo vkPipelineCreateInfo;

	VkPipelineShaderStageCreateInfo shaderStageCreateInfos[2];

	VkPipelineVertexInputStateCreateInfo vertexInputStateCreateInfo;
	VkVertexInputBindingDescription *vertexInputBindingDescriptions = SDL_stack_alloc(VkVertexInputBindingDescription, pipelineCreateInfo->vertexInputState.vertexBindingCount);
	VkVertexInputAttributeDescription *vertexInputAttributeDescriptions = SDL_stack_alloc(VkVertexInputAttributeDescription, pipelineCreateInfo->vertexInputState.vertexAttributeCount);

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCreateInfo;

	VkPipelineViewportStateCreateInfo viewportStateCreateInfo;

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

	static const VkDynamicState dynamicStates[] =
	{
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};
	VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo;

	VulkanRenderer *renderer = (VulkanRenderer*) driverData;

	/* Find a compatible sample count to use */

	actualSampleCount = VULKAN_INTERNAL_GetMaxMultiSampleCount(
		renderer,
		pipelineCreateInfo->multisampleState.multisampleCount
	);

	/* Create a "compatible" render pass */

	VkRenderPass transientRenderPass = VULKAN_INTERNAL_CreateTransientRenderPass(
		renderer,
		pipelineCreateInfo->attachmentInfo,
		actualSampleCount
	);

	/* Dynamic state */

	dynamicStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicStateCreateInfo.pNext = NULL;
	dynamicStateCreateInfo.flags = 0;
	dynamicStateCreateInfo.dynamicStateCount = SDL_arraysize(dynamicStates);
	dynamicStateCreateInfo.pDynamicStates = dynamicStates;

	/* Shader stages */

	graphicsPipeline->vertexShaderModule = (VulkanShaderModule*) pipelineCreateInfo->vertexShaderInfo.shaderModule;
	SDL_AtomicIncRef(&graphicsPipeline->vertexShaderModule->referenceCount);

	shaderStageCreateInfos[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageCreateInfos[0].pNext = NULL;
	shaderStageCreateInfos[0].flags = 0;
	shaderStageCreateInfos[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStageCreateInfos[0].module = graphicsPipeline->vertexShaderModule->shaderModule;
	shaderStageCreateInfos[0].pName = pipelineCreateInfo->vertexShaderInfo.entryPointName;
	shaderStageCreateInfos[0].pSpecializationInfo = NULL;

	graphicsPipeline->vertexUniformBlockSize =
		VULKAN_INTERNAL_NextHighestAlignment(
			pipelineCreateInfo->vertexShaderInfo.uniformBufferSize,
			renderer->minUBOAlignment
		);

	graphicsPipeline->fragmentShaderModule = (VulkanShaderModule*) pipelineCreateInfo->fragmentShaderInfo.shaderModule;
	SDL_AtomicIncRef(&graphicsPipeline->fragmentShaderModule->referenceCount);

	shaderStageCreateInfos[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageCreateInfos[1].pNext = NULL;
	shaderStageCreateInfos[1].flags = 0;
	shaderStageCreateInfos[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStageCreateInfos[1].module = graphicsPipeline->fragmentShaderModule->shaderModule;
	shaderStageCreateInfos[1].pName = pipelineCreateInfo->fragmentShaderInfo.entryPointName;
	shaderStageCreateInfos[1].pSpecializationInfo = NULL;

	graphicsPipeline->fragmentUniformBlockSize =
		VULKAN_INTERNAL_NextHighestAlignment(
			pipelineCreateInfo->fragmentShaderInfo.uniformBufferSize,
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

	/* NOTE: viewport and scissor are dynamic, and must be set using the command buffer */

	viewportStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportStateCreateInfo.pNext = NULL;
	viewportStateCreateInfo.flags = 0;
	viewportStateCreateInfo.viewportCount = 1;
	viewportStateCreateInfo.pViewports = NULL;
	viewportStateCreateInfo.scissorCount = 1;
	viewportStateCreateInfo.pScissors = NULL;

	/* Rasterization */

	rasterizationStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizationStateCreateInfo.pNext = NULL;
	rasterizationStateCreateInfo.flags = 0;
	rasterizationStateCreateInfo.depthClampEnable = VK_FALSE;
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
	rasterizationStateCreateInfo.lineWidth = 1.0f;

	/* Multisample */

	multisampleStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampleStateCreateInfo.pNext = NULL;
	multisampleStateCreateInfo.flags = 0;
	multisampleStateCreateInfo.rasterizationSamples = RefreshToVK_SampleCount[actualSampleCount];
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
	colorBlendStateCreateInfo.attachmentCount =
		pipelineCreateInfo->attachmentInfo.colorAttachmentCount;
	colorBlendStateCreateInfo.pAttachments =
		colorBlendAttachmentStates;
	colorBlendStateCreateInfo.blendConstants[0] =
		pipelineCreateInfo->blendConstants[0];
	colorBlendStateCreateInfo.blendConstants[1] =
		pipelineCreateInfo->blendConstants[1];
	colorBlendStateCreateInfo.blendConstants[2] =
		pipelineCreateInfo->blendConstants[2];
	colorBlendStateCreateInfo.blendConstants[3] =
		pipelineCreateInfo->blendConstants[3];

	/* We don't support LogicOp, so this is easy. */
	colorBlendStateCreateInfo.logicOpEnable = VK_FALSE;
	colorBlendStateCreateInfo.logicOp = 0;

	/* Pipeline Layout */

	graphicsPipeline->pipelineLayout = VULKAN_INTERNAL_FetchGraphicsPipelineLayout(
		renderer,
		pipelineCreateInfo->vertexShaderInfo.samplerBindingCount,
		pipelineCreateInfo->fragmentShaderInfo.samplerBindingCount
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
	vkPipelineCreateInfo.pDynamicState = &dynamicStateCreateInfo;
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
	SDL_stack_free(colorBlendAttachmentStates);

	renderer->vkDestroyRenderPass(
		renderer->logicalDevice,
		transientRenderPass,
		NULL
	);

	if (vulkanResult != VK_SUCCESS)
	{
		SDL_free(graphicsPipeline);
		LogVulkanResultAsError("vkCreateGraphicsPipelines", vulkanResult);
		Refresh_LogError("Failed to create graphics pipeline!");
		return NULL;
	}

	SDL_AtomicSet(&graphicsPipeline->referenceCount, 0);

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
	Refresh_ComputeShaderInfo *computeShaderInfo
) {
	VkComputePipelineCreateInfo computePipelineCreateInfo;
	VkPipelineShaderStageCreateInfo pipelineShaderStageCreateInfo;

	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanComputePipeline *vulkanComputePipeline = SDL_malloc(sizeof(VulkanComputePipeline));

	vulkanComputePipeline->computeShaderModule = (VulkanShaderModule*) computeShaderInfo->shaderModule;
	SDL_AtomicIncRef(&vulkanComputePipeline->computeShaderModule->referenceCount);

	pipelineShaderStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	pipelineShaderStageCreateInfo.pNext = NULL;
	pipelineShaderStageCreateInfo.flags = 0;
	pipelineShaderStageCreateInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	pipelineShaderStageCreateInfo.module = vulkanComputePipeline->computeShaderModule->shaderModule;
	pipelineShaderStageCreateInfo.pName = computeShaderInfo->entryPointName;
	pipelineShaderStageCreateInfo.pSpecializationInfo = NULL;

	vulkanComputePipeline->pipelineLayout = VULKAN_INTERNAL_FetchComputePipelineLayout(
		renderer,
		computeShaderInfo->bufferBindingCount,
		computeShaderInfo->imageBindingCount
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
			computeShaderInfo->uniformBufferSize,
			renderer->minUBOAlignment
		);

	SDL_AtomicSet(&vulkanComputePipeline->referenceCount, 0);

	return (Refresh_ComputePipeline*) vulkanComputePipeline;
}

static Refresh_Sampler* VULKAN_CreateSampler(
	Refresh_Renderer *driverData,
	Refresh_SamplerStateCreateInfo *samplerStateCreateInfo
) {
	VulkanRenderer* renderer = (VulkanRenderer*)driverData;
	VulkanSampler *vulkanSampler = SDL_malloc(sizeof(VulkanSampler));
	VkResult vulkanResult;

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
		&vulkanSampler->sampler
	);

	if (vulkanResult != VK_SUCCESS)
	{
		SDL_free(vulkanSampler);
		LogVulkanResultAsError("vkCreateSampler", vulkanResult);
		return NULL;
	}

	SDL_AtomicSet(&vulkanSampler->referenceCount, 0);

	return (Refresh_Sampler*) vulkanSampler;
}

static Refresh_ShaderModule* VULKAN_CreateShaderModule(
	Refresh_Renderer *driverData,
	Refresh_ShaderModuleCreateInfo *shaderModuleCreateInfo
) {
	VulkanShaderModule *vulkanShaderModule = SDL_malloc(sizeof(VulkanShaderModule));
	VkResult vulkanResult;
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
		&vulkanShaderModule->shaderModule
	);

	if (vulkanResult != VK_SUCCESS)
	{
		SDL_free(vulkanShaderModule);
		LogVulkanResultAsError("vkCreateShaderModule", vulkanResult);
		Refresh_LogError("Failed to create shader module!");
		return NULL;
	}

	SDL_AtomicSet(&vulkanShaderModule->referenceCount, 0);

	return (Refresh_ShaderModule*) vulkanShaderModule;
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
	uint8_t isDepthFormat = IsRefreshDepthFormat(textureCreateInfo->format);
	VkFormat format;
	VulkanTextureContainer *container;
	VulkanTexture *vulkanTexture;

	if (isDepthFormat)
	{
		format = RefreshToVK_DepthFormat(renderer, textureCreateInfo->format);
	}
	else
	{
		format = RefreshToVK_SurfaceFormat[textureCreateInfo->format];
	}

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

	if (textureCreateInfo->usageFlags & REFRESH_TEXTUREUSAGE_COMPUTE_BIT)
	{
		imageUsageFlags |= VK_IMAGE_USAGE_STORAGE_BIT;
	}

	if (isDepthFormat)
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

	vulkanTexture = VULKAN_INTERNAL_CreateTexture(
		renderer,
		textureCreateInfo->width,
		textureCreateInfo->height,
		textureCreateInfo->depth,
		textureCreateInfo->isCube,
		textureCreateInfo->levelCount,
		isDepthFormat ?
			textureCreateInfo->sampleCount : /* depth textures do not have a separate msaaTex */
			REFRESH_SAMPLECOUNT_1,
		format,
		imageAspectFlags,
		imageUsageFlags
	);

	/* create the MSAA texture for color attachments, if needed */
	if (	vulkanTexture != NULL &&
		!isDepthFormat &&
		textureCreateInfo->sampleCount > REFRESH_SAMPLECOUNT_1	)
	{
		vulkanTexture->msaaTex = VULKAN_INTERNAL_CreateTexture(
			renderer,
			textureCreateInfo->width,
			textureCreateInfo->height,
			textureCreateInfo->depth,
			textureCreateInfo->isCube,
			textureCreateInfo->levelCount,
			textureCreateInfo->sampleCount,
			format,
			imageAspectFlags,
			imageUsageFlags
		);
	}

	container = SDL_malloc(sizeof(VulkanTextureContainer));
	container->vulkanTexture = vulkanTexture;
	vulkanTexture->container = container;

	return (Refresh_Texture*) container;
}

static Refresh_Buffer* VULKAN_CreateBuffer(
	Refresh_Renderer *driverData,
	Refresh_BufferUsageFlags usageFlags,
	uint32_t sizeInBytes
) {
	VulkanResourceAccessType resourceAccessType;
	VkBufferUsageFlags vulkanUsageFlags =
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	if (usageFlags == 0)
	{
		resourceAccessType = RESOURCE_ACCESS_TRANSFER_READ_WRITE;
	}

	if (usageFlags & REFRESH_BUFFERUSAGE_VERTEX_BIT)
	{
		vulkanUsageFlags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		resourceAccessType = RESOURCE_ACCESS_VERTEX_BUFFER;
	}

	if (usageFlags & REFRESH_BUFFERUSAGE_INDEX_BIT)
	{
		vulkanUsageFlags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		resourceAccessType = RESOURCE_ACCESS_INDEX_BUFFER;
	}

	if (usageFlags & REFRESH_BUFFERUSAGE_COMPUTE_BIT)
	{
		vulkanUsageFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		resourceAccessType = RESOURCE_ACCESS_COMPUTE_SHADER_BUFFER_READ_WRITE;
	}

	if (usageFlags & REFRESH_BUFFERUSAGE_INDIRECT_BIT)
	{
		vulkanUsageFlags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
		resourceAccessType = RESOURCE_ACCESS_INDIRECT_BUFFER;
	}

	return (Refresh_Buffer*) VULKAN_INTERNAL_CreateBufferContainer(
		(VulkanRenderer*) driverData,
		sizeInBytes,
		resourceAccessType,
		vulkanUsageFlags,
		0
	);
}

/* Setters */

static VulkanTransferBuffer* VULKAN_INTERNAL_AcquireTransferBuffer(
	VulkanRenderer *renderer,
	VulkanCommandBuffer *commandBuffer,
	VkDeviceSize requiredSize,
	VkDeviceSize alignment
) {
	VkDeviceSize size;
	VkDeviceSize offset;
	uint32_t i;
	VulkanTransferBuffer *transferBuffer;

	/* Search the command buffer's current transfer buffers */

	for (i = 0; i < commandBuffer->transferBufferCount; i += 1)
	{
		transferBuffer = commandBuffer->transferBuffers[i];
		offset = transferBuffer->offset + alignment - (transferBuffer->offset % alignment);

		if (offset + requiredSize <= transferBuffer->buffer->size)
		{
			transferBuffer->offset = offset;
			return transferBuffer;
		}
	}

	/* Nothing fits, can we get a transfer buffer from the pool? */

	SDL_LockMutex(renderer->transferBufferPool.lock);

	for (i = 0; i < renderer->transferBufferPool.availableBufferCount; i += 1)
	{
		transferBuffer = renderer->transferBufferPool.availableBuffers[i];
		offset = transferBuffer->offset + alignment - (transferBuffer->offset % alignment);

		if (offset + requiredSize <= transferBuffer->buffer->size)
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

			transferBuffer->offset = offset;
			return transferBuffer;
		}
	}

	SDL_UnlockMutex(renderer->transferBufferPool.lock);

	/* Nothing fits, so let's create a new transfer buffer */

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
		RESOURCE_ACCESS_TRANSFER_READ_WRITE,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		1,
		1
	);
	transferBuffer->fromPool = 0;

	if (transferBuffer->buffer == NULL)
	{
		Refresh_LogError("Failed to allocate transfer buffer!");
		SDL_free(transferBuffer);
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
	VulkanTexture *vulkanTexture = ((VulkanTextureContainer*) textureSlice->texture)->vulkanTexture;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	VulkanTransferBuffer *transferBuffer;
	VkBufferImageCopy imageCopy;
	uint8_t *stagingBufferPointer;
	uint32_t blockSize = VULKAN_INTERNAL_TextureBlockSize(vulkanTexture->format);
	uint32_t bufferRowLength;
	uint32_t bufferImageHeight;

	transferBuffer = VULKAN_INTERNAL_AcquireTransferBuffer(
		renderer,
		vulkanCommandBuffer,
		VULKAN_INTERNAL_BytesPerImage(
			textureSlice->rectangle.w,
			textureSlice->rectangle.h,
			vulkanTexture->format
		),
		VULKAN_INTERNAL_BytesPerPixel(vulkanTexture->format)
	);

	if (transferBuffer == NULL)
	{
		return;
	}

	stagingBufferPointer =
		transferBuffer->buffer->usedRegion->allocation->mapPointer +
		transferBuffer->buffer->usedRegion->resourceOffset +
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

	bufferRowLength = SDL_max(blockSize, textureSlice->rectangle.w);
	bufferImageHeight = SDL_max(blockSize, textureSlice->rectangle.h);

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
	imageCopy.bufferRowLength = bufferRowLength;
	imageCopy.bufferImageHeight = bufferImageHeight;

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

	VULKAN_INTERNAL_TrackTexture(renderer, vulkanCommandBuffer, vulkanTexture);
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
	void *yDataPtr,
	void *uDataPtr,
	void *vDataPtr,
	uint32_t yDataLength,
	uint32_t uvDataLength,
	uint32_t yStride,
	uint32_t uvStride
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTexture *tex = ((VulkanTextureContainer*) y)->vulkanTexture;

	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*)commandBuffer;
	VulkanTransferBuffer *transferBuffer;
	VkBufferImageCopy imageCopy;
	uint8_t * stagingBufferPointer;

	transferBuffer = VULKAN_INTERNAL_AcquireTransferBuffer(
		renderer,
		vulkanCommandBuffer,
		yDataLength + uvDataLength,
		VULKAN_INTERNAL_BytesPerPixel(tex->format)
	);

	if (transferBuffer == NULL)
	{
		return;
	}

	stagingBufferPointer =
		transferBuffer->buffer->usedRegion->allocation->mapPointer +
		transferBuffer->buffer->usedRegion->resourceOffset +
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

	SDL_memcpy(
		stagingBufferPointer,
		yDataPtr,
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
	imageCopy.bufferRowLength = yStride;
	imageCopy.bufferImageHeight = yHeight;

	renderer->vkCmdCopyBufferToImage(
		vulkanCommandBuffer->commandBuffer,
		transferBuffer->buffer->buffer,
		tex->image,
		AccessMap[tex->resourceAccessType].imageLayout,
		1,
		&imageCopy
	);

	if (tex->usageFlags & VK_IMAGE_USAGE_SAMPLED_BIT)
	{
		/* TODO: is it worth it to only transition the specific subresource? */
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

	VULKAN_INTERNAL_TrackTexture(renderer, vulkanCommandBuffer, tex);

	/* These apply to both U and V */

	imageCopy.imageExtent.width = uvWidth;
	imageCopy.imageExtent.height = uvHeight;
	imageCopy.bufferRowLength = uvStride;
	imageCopy.bufferImageHeight = uvHeight;

	/* U */

	imageCopy.bufferOffset = transferBuffer->offset + yDataLength;

	tex = ((VulkanTextureContainer*) u)->vulkanTexture;

	SDL_memcpy(
		stagingBufferPointer + yDataLength,
		uDataPtr,
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

	if (tex->usageFlags & VK_IMAGE_USAGE_SAMPLED_BIT)
	{
		/* TODO: is it worth it to only transition the specific subresource? */
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

	VULKAN_INTERNAL_TrackTexture(renderer, vulkanCommandBuffer, tex);

	/* V */

	imageCopy.bufferOffset = transferBuffer->offset + yDataLength + uvDataLength;

	tex = ((VulkanTextureContainer*) v)->vulkanTexture;

	SDL_memcpy(
		stagingBufferPointer + yDataLength + uvDataLength,
		vDataPtr,
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
		/* TODO: is it worth it to only transition the specific subresource? */
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

	VULKAN_INTERNAL_TrackTexture(renderer, vulkanCommandBuffer, tex);
}

static void VULKAN_INTERNAL_BlitImage(
	VulkanRenderer *renderer,
	VulkanCommandBuffer *commandBuffer,
	Refresh_TextureSlice *sourceTextureSlice,
	Refresh_TextureSlice *destinationTextureSlice,
	VkFilter filter
) {
	VkImageBlit blit;
	VulkanTexture *sourceTexture = ((VulkanTextureContainer*) sourceTextureSlice->texture)->vulkanTexture;
	VulkanTexture *destinationTexture = ((VulkanTextureContainer*) destinationTextureSlice->texture)->vulkanTexture;

	VulkanResourceAccessType originalSourceAccessType = sourceTexture->resourceAccessType;
	VulkanResourceAccessType originalDestinationAccessType = destinationTexture->resourceAccessType;

	if (originalDestinationAccessType == RESOURCE_ACCESS_NONE)
	{
		originalDestinationAccessType = RESOURCE_ACCESS_ANY_SHADER_READ_SAMPLED_IMAGE;
	}

	/* TODO: is it worth it to only transition the specific subresource? */
	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		commandBuffer->commandBuffer,
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
		commandBuffer->commandBuffer,
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

	blit.dstSubresource.mipLevel = destinationTextureSlice->level;
	blit.dstSubresource.baseArrayLayer = destinationTextureSlice->layer;
	blit.dstSubresource.layerCount = 1;
	blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	renderer->vkCmdBlitImage(
		commandBuffer->commandBuffer,
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
		commandBuffer->commandBuffer,
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
		commandBuffer->commandBuffer,
		originalDestinationAccessType,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		destinationTexture->layerCount,
		0,
		destinationTexture->levelCount,
		0,
		destinationTexture->image,
		&destinationTexture->resourceAccessType
	);

	VULKAN_INTERNAL_TrackTexture(renderer, commandBuffer, sourceTexture);
	VULKAN_INTERNAL_TrackTexture(renderer, commandBuffer, destinationTexture);
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

	VULKAN_INTERNAL_BlitImage(
		renderer,
		vulkanCommandBuffer,
		sourceTextureSlice,
		destinationTextureSlice,
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
		vulkanBuffer->usedRegion->allocation->mapPointer + vulkanBuffer->usedRegion->resourceOffset + offsetInBytes,
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
	VulkanBuffer* vulkanBuffer = ((VulkanBufferContainer*) buffer)->vulkanBuffer;
	VulkanTransferBuffer* transferBuffer;
	uint8_t* transferBufferPointer;
	VkBufferCopy bufferCopy;
	VulkanResourceAccessType accessType = vulkanBuffer->resourceAccessType;

	transferBuffer = VULKAN_INTERNAL_AcquireTransferBuffer(
		renderer,
		vulkanCommandBuffer,
		dataLength,
		renderer->physicalDeviceProperties.properties.limits.optimalBufferCopyOffsetAlignment
	);

	if (transferBuffer == NULL)
	{
		return;
	}

	transferBufferPointer =
		transferBuffer->buffer->usedRegion->allocation->mapPointer +
		transferBuffer->buffer->usedRegion->resourceOffset +
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

	VULKAN_INTERNAL_TrackBuffer(renderer, vulkanCommandBuffer, vulkanBuffer);
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
		UBO_SECTION_SIZE
	) {
		/* We're out of space in this buffer, bind the old one and acquire a new one */
		VULKAN_INTERNAL_BindUniformBuffer(
			renderer,
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
		vulkanCommandBuffer->vertexUniformBuffer->pool->buffer,
		vulkanCommandBuffer->vertexUniformBuffer->poolOffset + vulkanCommandBuffer->vertexUniformBuffer->offset,
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

	if (
		vulkanCommandBuffer->fragmentUniformBuffer->offset +
		graphicsPipeline->fragmentUniformBlockSize >=
		UBO_SECTION_SIZE
	) {
		/* We're out of space in this buffer, bind the old one and acquire a new one */
		VULKAN_INTERNAL_BindUniformBuffer(
			renderer,
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
		vulkanCommandBuffer->fragmentUniformBuffer->pool->buffer,
		vulkanCommandBuffer->fragmentUniformBuffer->poolOffset + vulkanCommandBuffer->fragmentUniformBuffer->offset,
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

	if (
		vulkanCommandBuffer->computeUniformBuffer->offset +
		computePipeline->uniformBlockSize >=
		UBO_SECTION_SIZE
	) {
		/* We're out of space in this buffer, bind the old one and acquire a new one */
		VULKAN_INTERNAL_BindUniformBuffer(
			renderer,
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
		vulkanCommandBuffer->computeUniformBuffer->pool->buffer,
		vulkanCommandBuffer->computeUniformBuffer->poolOffset + vulkanCommandBuffer->computeUniformBuffer->offset,
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
	VulkanSampler *currentSampler;
	uint32_t i, samplerCount;
	VkDescriptorImageInfo descriptorImageInfos[MAX_TEXTURE_SAMPLERS];

	if (graphicsPipeline->pipelineLayout->vertexSamplerDescriptorSetCache == NULL)
	{
		return;
	}

	samplerCount = graphicsPipeline->pipelineLayout->vertexSamplerDescriptorSetCache->bindingCount;

	for (i = 0; i < samplerCount; i += 1)
	{
		currentTexture = ((VulkanTextureContainer*) pTextures[i])->vulkanTexture;
		currentSampler = (VulkanSampler*) pSamplers[i];
		descriptorImageInfos[i].imageView = currentTexture->view;
		descriptorImageInfos[i].sampler = currentSampler->sampler;
		descriptorImageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VULKAN_INTERNAL_TrackTexture(renderer, vulkanCommandBuffer, currentTexture);
		VULKAN_INTERNAL_TrackSampler(renderer, vulkanCommandBuffer, currentSampler);
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
	VulkanSampler *currentSampler;
	uint32_t i, samplerCount;
	VkDescriptorImageInfo descriptorImageInfos[MAX_TEXTURE_SAMPLERS];

	if (graphicsPipeline->pipelineLayout->fragmentSamplerDescriptorSetCache == NULL)
	{
		return;
	}

	samplerCount = graphicsPipeline->pipelineLayout->fragmentSamplerDescriptorSetCache->bindingCount;

	for (i = 0; i < samplerCount; i += 1)
	{
		currentTexture = ((VulkanTextureContainer*) pTextures[i])->vulkanTexture;
		currentSampler = (VulkanSampler*) pSamplers[i];
		descriptorImageInfos[i].imageView = currentTexture->view;
		descriptorImageInfos[i].sampler = currentSampler->sampler;
		descriptorImageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VULKAN_INTERNAL_TrackTexture(renderer, vulkanCommandBuffer, currentTexture);
		VULKAN_INTERNAL_TrackSampler(renderer, vulkanCommandBuffer, currentSampler);
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
	VulkanBuffer* vulkanBuffer = ((VulkanBufferContainer*) buffer)->vulkanBuffer;
	uint8_t *dataPtr = (uint8_t*) data;
	uint8_t *mapPointer;

	mapPointer =
		vulkanBuffer->usedRegion->allocation->mapPointer +
		vulkanBuffer->usedRegion->resourceOffset;

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
	VulkanTexture *vulkanTexture = ((VulkanTextureContainer*) textureSlice->texture)->vulkanTexture;
	VulkanBuffer* vulkanBuffer = ((VulkanBufferContainer*) buffer)->vulkanBuffer;

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

	VULKAN_INTERNAL_TrackBuffer(renderer, vulkanCommandBuffer, vulkanBuffer);
	VULKAN_INTERNAL_TrackTexture(renderer, vulkanCommandBuffer, vulkanTexture);
}

static void VULKAN_INTERNAL_QueueDestroyTexture(
	VulkanRenderer *renderer,
	VulkanTexture *vulkanTexture
) {
	SDL_LockMutex(renderer->disposeLock);

	EXPAND_ARRAY_IF_NEEDED(
		renderer->texturesToDestroy,
		VulkanTexture*,
		renderer->texturesToDestroyCount + 1,
		renderer->texturesToDestroyCapacity,
		renderer->texturesToDestroyCapacity * 2
	)

	renderer->texturesToDestroy[
		renderer->texturesToDestroyCount
	] = vulkanTexture;
	renderer->texturesToDestroyCount += 1;

	SDL_UnlockMutex(renderer->disposeLock);
}

static void VULKAN_QueueDestroyTexture(
	Refresh_Renderer *driverData,
	Refresh_Texture *texture
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTextureContainer *vulkanTextureContainer = (VulkanTextureContainer *)texture;
	VulkanTexture *vulkanTexture = vulkanTextureContainer->vulkanTexture;

	SDL_LockMutex(renderer->disposeLock);

	VULKAN_INTERNAL_QueueDestroyTexture(renderer, vulkanTexture);

	/* Containers are just client handles, so we can destroy immediately */
	SDL_free(vulkanTextureContainer);

	SDL_UnlockMutex(renderer->disposeLock);
}

static void VULKAN_QueueDestroySampler(
	Refresh_Renderer *driverData,
	Refresh_Sampler *sampler
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanSampler* vulkanSampler = (VulkanSampler*) sampler;

	SDL_LockMutex(renderer->disposeLock);

	EXPAND_ARRAY_IF_NEEDED(
		renderer->samplersToDestroy,
		VulkanSampler*,
		renderer->samplersToDestroyCount + 1,
		renderer->samplersToDestroyCapacity,
		renderer->samplersToDestroyCapacity * 2
	)

	renderer->samplersToDestroy[renderer->samplersToDestroyCount] = vulkanSampler;
	renderer->samplersToDestroyCount += 1;

	SDL_UnlockMutex(renderer->disposeLock);
}

static void VULKAN_INTERNAL_QueueDestroyBuffer(
	VulkanRenderer *renderer,
	VulkanBuffer *vulkanBuffer
) {
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

static void VULKAN_QueueDestroyBuffer(
	Refresh_Renderer *driverData,
	Refresh_Buffer *buffer
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanBufferContainer *vulkanBufferContainer = (VulkanBufferContainer*) buffer;
	VulkanBuffer *vulkanBuffer = vulkanBufferContainer->vulkanBuffer;

	SDL_LockMutex(renderer->disposeLock);

	VULKAN_INTERNAL_QueueDestroyBuffer(renderer, vulkanBuffer);

	/* Containers are just client handles, so we can destroy immediately */
	SDL_free(vulkanBufferContainer);

	SDL_UnlockMutex(renderer->disposeLock);
}

static void VULKAN_QueueDestroyShaderModule(
	Refresh_Renderer *driverData,
	Refresh_ShaderModule *shaderModule
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanShaderModule *vulkanShaderModule = (VulkanShaderModule*) shaderModule;

	SDL_LockMutex(renderer->disposeLock);

	EXPAND_ARRAY_IF_NEEDED(
		renderer->shaderModulesToDestroy,
		VulkanShaderModule*,
		renderer->shaderModulesToDestroyCount + 1,
		renderer->shaderModulesToDestroyCapacity,
		renderer->shaderModulesToDestroyCapacity * 2
	)

	renderer->shaderModulesToDestroy[renderer->shaderModulesToDestroyCount] = vulkanShaderModule;
	renderer->shaderModulesToDestroyCount += 1;

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

/* Command Buffer render state */

static VkRenderPass VULKAN_INTERNAL_FetchRenderPass(
	VulkanRenderer *renderer,
	VulkanCommandBuffer *commandBuffer,
	Refresh_ColorAttachmentInfo *colorAttachmentInfos,
	uint32_t colorAttachmentCount,
	Refresh_DepthStencilAttachmentInfo *depthStencilAttachmentInfo
) {
	VkRenderPass renderPass;
	RenderPassHash hash;
	uint32_t i;
	VulkanTexture *texture;

	SDL_LockMutex(renderer->renderPassFetchLock);

	for (i = 0; i < colorAttachmentCount; i += 1)
	{
		hash.colorTargetDescriptions[i].format = ((VulkanTextureContainer*) colorAttachmentInfos[i].texture)->vulkanTexture->format;
		hash.colorTargetDescriptions[i].clearColor = colorAttachmentInfos[i].clearColor;
		hash.colorTargetDescriptions[i].loadOp = colorAttachmentInfos[i].loadOp;
		hash.colorTargetDescriptions[i].storeOp = colorAttachmentInfos[i].storeOp;
	}

	hash.colorAttachmentSampleCount = REFRESH_SAMPLECOUNT_1;
	if (colorAttachmentCount > 0)
	{
		texture = ((VulkanTextureContainer*) colorAttachmentInfos[0].texture)->vulkanTexture;
		if (texture->msaaTex != NULL)
		{
			hash.colorAttachmentSampleCount = texture->msaaTex->sampleCount;
		}
	}

	hash.colorAttachmentCount = colorAttachmentCount;

	if (depthStencilAttachmentInfo == NULL)
	{
		hash.depthStencilTargetDescription.format = 0;
		hash.depthStencilTargetDescription.loadOp = REFRESH_LOADOP_DONT_CARE;
		hash.depthStencilTargetDescription.storeOp = REFRESH_STOREOP_DONT_CARE;
		hash.depthStencilTargetDescription.stencilLoadOp = REFRESH_LOADOP_DONT_CARE;
		hash.depthStencilTargetDescription.stencilStoreOp = REFRESH_STOREOP_DONT_CARE;
	}
	else
	{
		hash.depthStencilTargetDescription.format = ((VulkanTextureContainer*) depthStencilAttachmentInfo->texture)->vulkanTexture->format;
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
		commandBuffer,
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

static VulkanFramebuffer* VULKAN_INTERNAL_FetchFramebuffer(
	VulkanRenderer *renderer,
	VkRenderPass renderPass,
	Refresh_ColorAttachmentInfo *colorAttachmentInfos,
	uint32_t colorAttachmentCount,
	Refresh_DepthStencilAttachmentInfo *depthStencilAttachmentInfo,
	uint32_t width,
	uint32_t height
) {
	VulkanFramebuffer *vulkanFramebuffer;
	VkFramebufferCreateInfo framebufferInfo;
	VkResult result;
	VkImageView imageViewAttachments[2 * MAX_COLOR_TARGET_BINDINGS + 1];
	FramebufferHash hash;
	VulkanTexture *texture;
	VulkanRenderTarget *renderTarget;
	uint32_t attachmentCount = 0;
	uint32_t i;

	for (i = 0; i < MAX_COLOR_TARGET_BINDINGS; i += 1)
	{
		hash.colorAttachmentViews[i] = VK_NULL_HANDLE;
		hash.colorMultiSampleAttachmentViews[i] = VK_NULL_HANDLE;
	}

	hash.colorAttachmentCount = colorAttachmentCount;

	for (i = 0; i < colorAttachmentCount; i += 1)
	{
		texture = ((VulkanTextureContainer*) colorAttachmentInfos[i].texture)->vulkanTexture;

		renderTarget = VULKAN_INTERNAL_FetchRenderTarget(
			renderer,
			texture,
			colorAttachmentInfos[i].depth,
			colorAttachmentInfos[i].layer,
			colorAttachmentInfos[i].level
		);

		hash.colorAttachmentViews[i] = (
			renderTarget->view
		);

		if (texture->msaaTex != NULL)
		{
			renderTarget = VULKAN_INTERNAL_FetchRenderTarget(
				renderer,
				texture->msaaTex,
				colorAttachmentInfos[i].depth,
				colorAttachmentInfos[i].layer,
				colorAttachmentInfos[i].level
			);

			hash.colorMultiSampleAttachmentViews[i] = (
				renderTarget->view
			);
		}
	}

	if (depthStencilAttachmentInfo == NULL)
	{
		hash.depthStencilAttachmentView = VK_NULL_HANDLE;
	}
	else
	{
		texture = ((VulkanTextureContainer*) depthStencilAttachmentInfo->texture)->vulkanTexture;
		renderTarget = VULKAN_INTERNAL_FetchRenderTarget(
			renderer,
			texture,
			depthStencilAttachmentInfo->depth,
			depthStencilAttachmentInfo->layer,
			depthStencilAttachmentInfo->level
		);
		hash.depthStencilAttachmentView = renderTarget->view;
	}

	hash.width = width;
	hash.height = height;

	SDL_LockMutex(renderer->framebufferFetchLock);

	vulkanFramebuffer = FramebufferHashArray_Fetch(
		&renderer->framebufferHashArray,
		&hash
	);

	SDL_UnlockMutex(renderer->framebufferFetchLock);

	if (vulkanFramebuffer != NULL)
	{
		return vulkanFramebuffer;
	}

	vulkanFramebuffer = SDL_malloc(sizeof(VulkanFramebuffer));

	SDL_AtomicSet(&vulkanFramebuffer->referenceCount, 0);

	/* Create a new framebuffer */

	for (i = 0; i < colorAttachmentCount; i += 1)
	{
		texture = ((VulkanTextureContainer*) colorAttachmentInfos[i].texture)->vulkanTexture;

		renderTarget = VULKAN_INTERNAL_FetchRenderTarget(
			renderer,
			texture,
			colorAttachmentInfos[i].depth,
			colorAttachmentInfos[i].layer,
			colorAttachmentInfos[i].level
		);

		imageViewAttachments[attachmentCount] =
			renderTarget->view;

		attachmentCount += 1;

		if (texture->msaaTex != NULL)
		{
			renderTarget = VULKAN_INTERNAL_FetchRenderTarget(
				renderer,
				texture->msaaTex,
				colorAttachmentInfos[i].depth,
				colorAttachmentInfos[i].layer,
				colorAttachmentInfos[i].level
			);

			imageViewAttachments[attachmentCount] =
				renderTarget->view;

			attachmentCount += 1;
		}
	}

	if (depthStencilAttachmentInfo != NULL)
	{
		texture = ((VulkanTextureContainer*) depthStencilAttachmentInfo->texture)->vulkanTexture;
		renderTarget = VULKAN_INTERNAL_FetchRenderTarget(
			renderer,
			texture,
			depthStencilAttachmentInfo->depth,
			depthStencilAttachmentInfo->layer,
			depthStencilAttachmentInfo->level
		);

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
		&vulkanFramebuffer->framebuffer
	);

	if (result == VK_SUCCESS)
	{
		SDL_LockMutex(renderer->framebufferFetchLock);

		FramebufferHashArray_Insert(
			&renderer->framebufferHashArray,
			hash,
			vulkanFramebuffer
		);

		SDL_UnlockMutex(renderer->framebufferFetchLock);
	}
	else
	{
		LogVulkanResultAsError("vkCreateFramebuffer", result);
		SDL_free(vulkanFramebuffer);
		vulkanFramebuffer = NULL;
	}

	return vulkanFramebuffer;
}

static void VULKAN_INTERNAL_SetCurrentViewport(
	VulkanCommandBuffer *commandBuffer,
	Refresh_Viewport *viewport
) {
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;

	vulkanCommandBuffer->currentViewport.x = viewport->x;
	vulkanCommandBuffer->currentViewport.y = viewport->y;
	vulkanCommandBuffer->currentViewport.width = viewport->w;
	vulkanCommandBuffer->currentViewport.height = viewport->h;
	vulkanCommandBuffer->currentViewport.minDepth = viewport->minDepth;
	vulkanCommandBuffer->currentViewport.maxDepth = viewport->maxDepth;
}

static void VULKAN_SetViewport(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Viewport *viewport
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;

	VULKAN_INTERNAL_SetCurrentViewport(
		vulkanCommandBuffer,
		viewport
	);

	renderer->vkCmdSetViewport(
		vulkanCommandBuffer->commandBuffer,
		0,
		1,
		&vulkanCommandBuffer->currentViewport
	);
}

static void VULKAN_INTERNAL_SetCurrentScissor(
	VulkanCommandBuffer *vulkanCommandBuffer,
	Refresh_Rect *scissor
) {
	vulkanCommandBuffer->currentScissor.offset.x = scissor->x;
	vulkanCommandBuffer->currentScissor.offset.y = scissor->y;
	vulkanCommandBuffer->currentScissor.extent.width = scissor->w;
	vulkanCommandBuffer->currentScissor.extent.height = scissor->h;
}

static void VULKAN_SetScissor(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Rect *scissor
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;

	VULKAN_INTERNAL_SetCurrentScissor(
		vulkanCommandBuffer,
		scissor
	);

	renderer->vkCmdSetScissor(
		vulkanCommandBuffer->commandBuffer,
		0,
		1,
		&vulkanCommandBuffer->currentScissor
	);
}

static void VULKAN_BeginRenderPass(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_ColorAttachmentInfo *colorAttachmentInfos,
	uint32_t colorAttachmentCount,
	Refresh_DepthStencilAttachmentInfo *depthStencilAttachmentInfo
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	VkRenderPass renderPass;
	VulkanFramebuffer *framebuffer;

	VulkanTexture *texture;
	uint32_t w, h;
	VkClearValue *clearValues;
	uint32_t clearCount = colorAttachmentCount;
	uint32_t multisampleAttachmentCount = 0;
	uint32_t totalColorAttachmentCount = 0;
	uint32_t i;
	VkImageAspectFlags depthAspectFlags;
	Refresh_Viewport defaultViewport;
	Refresh_Rect defaultScissor;
	uint32_t framebufferWidth = UINT32_MAX;
	uint32_t framebufferHeight = UINT32_MAX;

	/* The framebuffer cannot be larger than the smallest attachment. */

	for (i = 0; i < colorAttachmentCount; i += 1)
	{
		texture = ((VulkanTextureContainer*) colorAttachmentInfos[i].texture)->vulkanTexture;
		w = texture->dimensions.width >> colorAttachmentInfos[i].level;
		h = texture->dimensions.height >> colorAttachmentInfos[i].level;

		if (w < framebufferWidth)
		{
			framebufferWidth = w;
		}

		if (h < framebufferHeight)
		{
			framebufferHeight = h;
		}
	}

	if (depthStencilAttachmentInfo != NULL)
	{
		texture = ((VulkanTextureContainer*) depthStencilAttachmentInfo->texture)->vulkanTexture;
		w = texture->dimensions.width >> depthStencilAttachmentInfo->level;
		h = texture->dimensions.height >> depthStencilAttachmentInfo->level;

		if (w < framebufferWidth)
		{
			framebufferWidth = w;
		}

		if (h < framebufferHeight)
		{
			framebufferHeight = h;
		}
	}

	/* Fetch required render objects */

	renderPass = VULKAN_INTERNAL_FetchRenderPass(
		renderer,
		vulkanCommandBuffer,
		colorAttachmentInfos,
		colorAttachmentCount,
		depthStencilAttachmentInfo
	);

	framebuffer = VULKAN_INTERNAL_FetchFramebuffer(
		renderer,
		renderPass,
		colorAttachmentInfos,
		colorAttachmentCount,
		depthStencilAttachmentInfo,
		framebufferWidth,
		framebufferHeight
	);

	VULKAN_INTERNAL_TrackFramebuffer(renderer, vulkanCommandBuffer, framebuffer);

	/* Layout transitions */

	for (i = 0; i < colorAttachmentCount; i += 1)
	{
		texture = ((VulkanTextureContainer*) colorAttachmentInfos[i].texture)->vulkanTexture;

		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			vulkanCommandBuffer->commandBuffer,
			RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0,
			texture->layerCount,
			0,
			texture->levelCount,
			0,
			texture->image,
			&texture->resourceAccessType
		);

		if (texture->msaaTex != NULL)
		{
			clearCount += 1;
			multisampleAttachmentCount += 1;
		}

		VULKAN_INTERNAL_TrackTexture(renderer, vulkanCommandBuffer, texture);
	}

	if (depthStencilAttachmentInfo != NULL)
	{
		texture = ((VulkanTextureContainer*) depthStencilAttachmentInfo->texture)->vulkanTexture;
		depthAspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;

		if (IsStencilFormat(texture->format))
		{
			depthAspectFlags |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}

		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			vulkanCommandBuffer->commandBuffer,
			RESOURCE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_WRITE,
			depthAspectFlags,
			0,
			texture->layerCount,
			0,
			texture->levelCount,
			0,
			texture->image,
			&texture->resourceAccessType
		);

		clearCount += 1;

		VULKAN_INTERNAL_TrackTexture(renderer, vulkanCommandBuffer, texture);
	}

	/* Set clear values */

	clearValues = SDL_stack_alloc(VkClearValue, clearCount);

	totalColorAttachmentCount = colorAttachmentCount + multisampleAttachmentCount;

	for (i = 0; i < totalColorAttachmentCount; i += 1)
	{
		clearValues[i].color.float32[0] = colorAttachmentInfos[i].clearColor.x;
		clearValues[i].color.float32[1] = colorAttachmentInfos[i].clearColor.y;
		clearValues[i].color.float32[2] = colorAttachmentInfos[i].clearColor.z;
		clearValues[i].color.float32[3] = colorAttachmentInfos[i].clearColor.w;

		texture = ((VulkanTextureContainer*) colorAttachmentInfos[i].texture)->vulkanTexture;
		if (texture->msaaTex != NULL)
		{
			clearValues[i+1].color.float32[0] = colorAttachmentInfos[i].clearColor.x;
			clearValues[i+1].color.float32[1] = colorAttachmentInfos[i].clearColor.y;
			clearValues[i+1].color.float32[2] = colorAttachmentInfos[i].clearColor.z;
			clearValues[i+1].color.float32[3] = colorAttachmentInfos[i].clearColor.w;
			i += 1;
		}
	}

	if (depthStencilAttachmentInfo != NULL)
	{
		clearValues[totalColorAttachmentCount].depthStencil.depth =
			depthStencilAttachmentInfo->depthStencilClearValue.depth;
		clearValues[totalColorAttachmentCount].depthStencil.stencil =
			depthStencilAttachmentInfo->depthStencilClearValue.stencil;
	}

	VkRenderPassBeginInfo renderPassBeginInfo;
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.pNext = NULL;
	renderPassBeginInfo.renderPass = renderPass;
	renderPassBeginInfo.framebuffer = framebuffer->framebuffer;
	renderPassBeginInfo.pClearValues = clearValues;
	renderPassBeginInfo.clearValueCount = clearCount;
	renderPassBeginInfo.renderArea.extent.width = framebufferWidth;
	renderPassBeginInfo.renderArea.extent.height = framebufferHeight;
	renderPassBeginInfo.renderArea.offset.x = 0;
	renderPassBeginInfo.renderArea.offset.y = 0;

	renderer->vkCmdBeginRenderPass(
		vulkanCommandBuffer->commandBuffer,
		&renderPassBeginInfo,
		VK_SUBPASS_CONTENTS_INLINE
	);

	SDL_stack_free(clearValues);

	for (i = 0; i < colorAttachmentCount; i += 1)
	{
		vulkanCommandBuffer->renderPassColorTargetTextures[i] =
			((VulkanTextureContainer*) colorAttachmentInfos[i].texture)->vulkanTexture;
	}
	vulkanCommandBuffer->renderPassColorTargetCount = colorAttachmentCount;

	if (depthStencilAttachmentInfo != NULL)
	{
		vulkanCommandBuffer->renderPassDepthTexture = ((VulkanTextureContainer*) depthStencilAttachmentInfo->texture)->vulkanTexture;
	}

	/* Set sensible default viewport state */

	defaultViewport.x = 0;
	defaultViewport.y = 0;
	defaultViewport.w = framebufferWidth;
	defaultViewport.h = framebufferHeight;
	defaultViewport.minDepth = 0;
	defaultViewport.maxDepth = 1;

	VULKAN_INTERNAL_SetCurrentViewport(
		vulkanCommandBuffer,
		&defaultViewport
	);

	defaultScissor.x = 0;
	defaultScissor.y = 0;
	defaultScissor.w = framebufferWidth;
	defaultScissor.h = framebufferHeight;

	VULKAN_INTERNAL_SetCurrentScissor(
		vulkanCommandBuffer,
		&defaultScissor
	);
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
			renderer,
			vulkanCommandBuffer,
			vulkanCommandBuffer->vertexUniformBuffer
		);
	}
	vulkanCommandBuffer->vertexUniformBuffer = NULL;

	if (	vulkanCommandBuffer->fragmentUniformBuffer != renderer->dummyFragmentUniformBuffer &&
		vulkanCommandBuffer->fragmentUniformBuffer != NULL
	) {
		VULKAN_INTERNAL_BindUniformBuffer(
			renderer,
			vulkanCommandBuffer,
			vulkanCommandBuffer->fragmentUniformBuffer
		);
	}
	vulkanCommandBuffer->fragmentUniformBuffer = NULL;

	/* If the render targets can be sampled, transition them to sample layout */
	for (i = 0; i < vulkanCommandBuffer->renderPassColorTargetCount; i += 1)
	{
		currentTexture = vulkanCommandBuffer->renderPassColorTargetTextures[i];

		if (currentTexture->usageFlags & VK_IMAGE_USAGE_SAMPLED_BIT)
		{
			VULKAN_INTERNAL_ImageMemoryBarrier(
				renderer,
				vulkanCommandBuffer->commandBuffer,
				RESOURCE_ACCESS_ANY_SHADER_READ_SAMPLED_IMAGE,
				currentTexture->aspectFlags,
				0,
				currentTexture->layerCount,
				0,
				currentTexture->levelCount,
				0,
				currentTexture->image,
				&currentTexture->resourceAccessType
			);
		}
		else if (currentTexture->usageFlags & VK_IMAGE_USAGE_STORAGE_BIT)
		{
			VULKAN_INTERNAL_ImageMemoryBarrier(
				renderer,
				vulkanCommandBuffer->commandBuffer,
				RESOURCE_ACCESS_COMPUTE_SHADER_STORAGE_IMAGE_READ_WRITE,
				currentTexture->aspectFlags,
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

	if (vulkanCommandBuffer->renderPassDepthTexture != NULL)
	{
		currentTexture = vulkanCommandBuffer->renderPassDepthTexture;

		if (currentTexture->usageFlags & VK_IMAGE_USAGE_SAMPLED_BIT)
		{
			VULKAN_INTERNAL_ImageMemoryBarrier(
				renderer,
				vulkanCommandBuffer->commandBuffer,
				RESOURCE_ACCESS_ANY_SHADER_READ_SAMPLED_IMAGE,
				currentTexture->aspectFlags,
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
	vulkanCommandBuffer->renderPassDepthTexture = NULL;

	vulkanCommandBuffer->currentGraphicsPipeline = NULL;
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
			renderer,
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
			renderer,
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

	VULKAN_INTERNAL_TrackGraphicsPipeline(renderer, vulkanCommandBuffer, pipeline);

	renderer->vkCmdSetViewport(
		vulkanCommandBuffer->commandBuffer,
		0,
		1,
		&vulkanCommandBuffer->currentViewport
	);

	renderer->vkCmdSetScissor(
		vulkanCommandBuffer->commandBuffer,
		0,
		1,
		&vulkanCommandBuffer->currentScissor
	);
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
		currentVulkanBuffer = ((VulkanBufferContainer*) pBuffers[i])->vulkanBuffer;
		buffers[i] = currentVulkanBuffer->buffer;
		VULKAN_INTERNAL_TrackBuffer(renderer, vulkanCommandBuffer, currentVulkanBuffer);
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
	VulkanBuffer* vulkanBuffer = ((VulkanBufferContainer*) buffer)->vulkanBuffer;

	VULKAN_INTERNAL_TrackBuffer(renderer, vulkanCommandBuffer, vulkanBuffer);

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
			renderer,
			vulkanCommandBuffer,
			vulkanCommandBuffer->computeUniformBuffer
		);
	}
	renderer->vkCmdBindPipeline(
		vulkanCommandBuffer->commandBuffer,
		VK_PIPELINE_BIND_POINT_COMPUTE,
		vulkanComputePipeline->pipeline
	);

	vulkanCommandBuffer->currentComputePipeline = vulkanComputePipeline;

	if (vulkanComputePipeline->uniformBlockSize == 0)
	{
		vulkanCommandBuffer->computeUniformBuffer = renderer->dummyComputeUniformBuffer;
	}
	else
	{
		vulkanCommandBuffer->computeUniformBuffer = VULKAN_INTERNAL_AcquireUniformBufferFromPool(
			renderer,
			renderer->computeUniformBufferPool,
			vulkanComputePipeline->uniformBlockSize
		);
	}

	VULKAN_INTERNAL_TrackComputePipeline(renderer, vulkanCommandBuffer, vulkanComputePipeline);
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
		currentVulkanBuffer = ((VulkanBufferContainer*) pBuffers[i])->vulkanBuffer;

		descriptorBufferInfos[i].buffer = currentVulkanBuffer->buffer;
		descriptorBufferInfos[i].offset = 0;
		descriptorBufferInfos[i].range = currentVulkanBuffer->size;

		VULKAN_INTERNAL_BufferMemoryBarrier(
			renderer,
			vulkanCommandBuffer->commandBuffer,
			RESOURCE_ACCESS_COMPUTE_SHADER_BUFFER_READ_WRITE,
			currentVulkanBuffer
		);

		VULKAN_INTERNAL_TrackBuffer(renderer, vulkanCommandBuffer, currentVulkanBuffer);
	}

	vulkanCommandBuffer->bufferDescriptorSet =
		VULKAN_INTERNAL_FetchDescriptorSet(
			renderer,
			vulkanCommandBuffer,
			computePipeline->pipelineLayout->bufferDescriptorSetCache,
			NULL,
			descriptorBufferInfos
		);

	if (vulkanCommandBuffer->boundComputeBufferCount == vulkanCommandBuffer->boundComputeBufferCapacity)
	{
		vulkanCommandBuffer->boundComputeBufferCapacity *= 2;
		vulkanCommandBuffer->boundComputeBuffers = SDL_realloc(
			vulkanCommandBuffer->boundComputeBuffers,
			vulkanCommandBuffer->boundComputeBufferCapacity * sizeof(VulkanBuffer*)
		);
	}

	vulkanCommandBuffer->boundComputeBuffers[vulkanCommandBuffer->boundComputeBufferCount] = currentVulkanBuffer;
	vulkanCommandBuffer->boundComputeBufferCount += 1;
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
		currentTexture = ((VulkanTextureContainer*) pTextures[i])->vulkanTexture;
		descriptorImageInfos[i].imageView = currentTexture->view;
		descriptorImageInfos[i].sampler = VK_NULL_HANDLE;
		descriptorImageInfos[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			vulkanCommandBuffer->commandBuffer,
			RESOURCE_ACCESS_COMPUTE_SHADER_STORAGE_IMAGE_READ_WRITE,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0,
			currentTexture->layerCount,
			0,
			currentTexture->levelCount,
			0,
			currentTexture->image,
			&currentTexture->resourceAccessType
		);

		VULKAN_INTERNAL_TrackTexture(renderer, vulkanCommandBuffer, currentTexture);

		if (vulkanCommandBuffer->boundComputeTextureCount == vulkanCommandBuffer->boundComputeTextureCapacity)
		{
			vulkanCommandBuffer->boundComputeTextureCapacity *= 2;
			vulkanCommandBuffer->boundComputeTextures = SDL_realloc(
				vulkanCommandBuffer->boundComputeTextures,
				vulkanCommandBuffer->boundComputeTextureCapacity * sizeof(VulkanTexture *)
			);
		}

		vulkanCommandBuffer->boundComputeTextures[i] = currentTexture;
		vulkanCommandBuffer->boundComputeTextureCount += 1;
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

		commandBuffer->inFlightFence = VK_NULL_HANDLE;
		commandBuffer->renderPassDepthTexture = NULL;

		/* Presentation tracking */

		commandBuffer->presentDataCapacity = 1;
		commandBuffer->presentDataCount = 0;
		commandBuffer->presentDatas = SDL_malloc(
			commandBuffer->presentDataCapacity * sizeof(VkPresentInfoKHR)
		);

		commandBuffer->waitSemaphoreCapacity = 1;
		commandBuffer->waitSemaphoreCount = 0;
		commandBuffer->waitSemaphores = SDL_malloc(
			commandBuffer->waitSemaphoreCapacity * sizeof(VkSemaphore)
		);

		commandBuffer->signalSemaphoreCapacity = 1;
		commandBuffer->signalSemaphoreCount = 0;
		commandBuffer->signalSemaphores = SDL_malloc(
			commandBuffer->signalSemaphoreCapacity * sizeof(VkSemaphore)
		);

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

		/* Bound compute resource tracking */

		commandBuffer->boundComputeBufferCapacity = 16;
		commandBuffer->boundComputeBufferCount = 0;
		commandBuffer->boundComputeBuffers = SDL_malloc(
			commandBuffer->boundComputeBufferCapacity * sizeof(VulkanBuffer*)
		);

		commandBuffer->boundComputeTextureCapacity = 16;
		commandBuffer->boundComputeTextureCount = 0;
		commandBuffer->boundComputeTextures = SDL_malloc(
			commandBuffer->boundComputeTextureCapacity * sizeof(VulkanTexture*)
		);

		/* Resource tracking */

		commandBuffer->usedBufferCapacity = 4;
		commandBuffer->usedBufferCount = 0;
		commandBuffer->usedBuffers = SDL_malloc(
			commandBuffer->usedBufferCapacity * sizeof(VulkanBuffer*)
		);

		commandBuffer->usedTextureCapacity = 4;
		commandBuffer->usedTextureCount = 0;
		commandBuffer->usedTextures = SDL_malloc(
			commandBuffer->usedTextureCapacity * sizeof(VulkanTexture*)
		);

		commandBuffer->usedSamplerCapacity = 4;
		commandBuffer->usedSamplerCount = 0;
		commandBuffer->usedSamplers = SDL_malloc(
			commandBuffer->usedSamplerCapacity * sizeof(VulkanSampler*)
		);

		commandBuffer->usedGraphicsPipelineCapacity = 4;
		commandBuffer->usedGraphicsPipelineCount = 0;
		commandBuffer->usedGraphicsPipelines = SDL_malloc(
			commandBuffer->usedGraphicsPipelineCapacity * sizeof(VulkanGraphicsPipeline*)
		);

		commandBuffer->usedComputePipelineCapacity = 4;
		commandBuffer->usedComputePipelineCount = 0;
		commandBuffer->usedComputePipelines = SDL_malloc(
			commandBuffer->usedComputePipelineCapacity * sizeof(VulkanComputePipeline*)
		);

		commandBuffer->usedFramebufferCapacity = 4;
		commandBuffer->usedFramebufferCount = 0;
		commandBuffer->usedFramebuffers = SDL_malloc(
			commandBuffer->usedFramebufferCapacity * sizeof(VulkanFramebuffer*)
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
	commandPoolCreateInfo.queueFamilyIndex = renderer->queueFamilyIndex;

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
	Refresh_Renderer *driverData
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

	commandBuffer->renderPassColorTargetCount = 0;

	/* Reset the command buffer here to avoid resets being called
	 * from a separate thread than where the command buffer was acquired
	 */
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

static WindowData* VULKAN_INTERNAL_FetchWindowData(
	void *windowHandle
) {
	return (WindowData*) SDL_GetWindowData(windowHandle, WINDOW_DATA);
}

static uint8_t VULKAN_ClaimWindow(
	Refresh_Renderer *driverData,
	void *windowHandle,
	Refresh_PresentMode presentMode
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	WindowData *windowData = VULKAN_INTERNAL_FetchWindowData(windowHandle);

	if (windowData == NULL)
	{
		windowData = SDL_malloc(sizeof(WindowData));
		windowData->windowHandle = windowHandle;
		windowData->preferredPresentMode = presentMode;

		if (VULKAN_INTERNAL_CreateSwapchain(renderer, windowData))
		{
			SDL_SetWindowData((SDL_Window*) windowHandle, WINDOW_DATA, windowData);

			if (renderer->claimedWindowCount >= renderer->claimedWindowCapacity)
			{
				renderer->claimedWindowCapacity *= 2;
				renderer->claimedWindows = SDL_realloc(
					renderer->claimedWindows,
					renderer->claimedWindowCapacity * sizeof(WindowData*)
				);
			}

			renderer->claimedWindows[renderer->claimedWindowCount] = windowData;
			renderer->claimedWindowCount += 1;

			return 1;
		}
		else
		{
			Refresh_LogError("Could not create swapchain, failed to claim window!");
			SDL_free(windowData);
			return 0;
		}
	}
	else
	{
		Refresh_LogWarn("Window already claimed!");
		return 0;
	}
}

static void VULKAN_UnclaimWindow(
	Refresh_Renderer *driverData,
	void *windowHandle
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	WindowData *windowData = VULKAN_INTERNAL_FetchWindowData(windowHandle);
	uint32_t i;

	if (windowData == NULL)
	{
		return;
	}

	if (windowData->swapchainData != NULL)
	{
		VULKAN_Wait(driverData);
		VULKAN_INTERNAL_DestroySwapchain(
			(VulkanRenderer*) driverData,
			windowData
		);
	}

	for (i = 0; i < renderer->claimedWindowCount; i += 1)
	{
		if (renderer->claimedWindows[i]->windowHandle == windowHandle)
		{
			renderer->claimedWindows[i] = renderer->claimedWindows[renderer->claimedWindowCount - 1];
			renderer->claimedWindowCount -= 1;
			break;
		}
	}

	SDL_free(windowData);
	SDL_SetWindowData((SDL_Window*) windowHandle, WINDOW_DATA, NULL);
}

static Refresh_Texture* VULKAN_AcquireSwapchainTexture(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	void *windowHandle,
	uint32_t *pWidth,
	uint32_t *pHeight
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanCommandBuffer *vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	uint32_t swapchainImageIndex;
	WindowData *windowData;
	VulkanSwapchainData *swapchainData;
	VkResult acquireResult = VK_SUCCESS;
	VulkanTextureContainer *swapchainTextureContainer = NULL;
	VulkanPresentData *presentData;

	windowData = VULKAN_INTERNAL_FetchWindowData(windowHandle);
	if (windowData == NULL)
	{
		return NULL;
	}

	swapchainData = windowData->swapchainData;

	/* Window is claimed but swapchain is invalid! */
	if (swapchainData == NULL)
	{
		if (SDL_GetWindowFlags(windowHandle) & SDL_WINDOW_MINIMIZED)
		{
			/* Window is minimized, don't bother */
			return NULL;
		}

		/* Let's try to recreate */
		VULKAN_INTERNAL_RecreateSwapchain(renderer, windowData);
		swapchainData = windowData->swapchainData;

		if (swapchainData == NULL)
		{
			Refresh_LogWarn("Failed to recreate swapchain!");
			return NULL;
		}
	}

	acquireResult = renderer->vkAcquireNextImageKHR(
		renderer->logicalDevice,
		swapchainData->swapchain,
		UINT64_MAX,
		swapchainData->imageAvailableSemaphore,
		VK_NULL_HANDLE,
		&swapchainImageIndex
	);

	/* Acquisition is invalid, let's try to recreate */
	if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
	{
		VULKAN_INTERNAL_RecreateSwapchain(renderer, windowData);
		swapchainData = windowData->swapchainData;

		if (swapchainData == NULL)
		{
			Refresh_LogWarn("Failed to recreate swapchain!");
			return NULL;
		}

		acquireResult = renderer->vkAcquireNextImageKHR(
			renderer->logicalDevice,
			swapchainData->swapchain,
			UINT64_MAX,
			swapchainData->imageAvailableSemaphore,
			VK_NULL_HANDLE,
			&swapchainImageIndex
		);

		if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
		{
			Refresh_LogWarn("Failed to acquire swapchain texture!");
			return NULL;
		}
	}

	swapchainTextureContainer = &swapchainData->textureContainers[swapchainImageIndex];

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		vulkanCommandBuffer->commandBuffer,
		RESOURCE_ACCESS_COLOR_ATTACHMENT_WRITE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		1,
		0,
		1,
		0,
		swapchainTextureContainer->vulkanTexture->image,
		&swapchainTextureContainer->vulkanTexture->resourceAccessType
	);

	/* Set up present struct */

	if (vulkanCommandBuffer->presentDataCount == vulkanCommandBuffer->presentDataCapacity)
	{
		vulkanCommandBuffer->presentDataCapacity += 1;
		vulkanCommandBuffer->presentDatas = SDL_realloc(
			vulkanCommandBuffer->presentDatas,
			vulkanCommandBuffer->presentDataCapacity * sizeof(VkPresentInfoKHR)
		);
	}

	presentData = &vulkanCommandBuffer->presentDatas[vulkanCommandBuffer->presentDataCount];
	vulkanCommandBuffer->presentDataCount += 1;

	presentData->windowData = windowData;
	presentData->swapchainImageIndex = swapchainImageIndex;

	/* Set up present semaphores */

	if (vulkanCommandBuffer->waitSemaphoreCount == vulkanCommandBuffer->waitSemaphoreCapacity)
	{
		vulkanCommandBuffer->waitSemaphoreCapacity += 1;
		vulkanCommandBuffer->waitSemaphores = SDL_realloc(
			vulkanCommandBuffer->waitSemaphores,
			vulkanCommandBuffer->waitSemaphoreCapacity * sizeof(VkSemaphore)
		);
	}

	vulkanCommandBuffer->waitSemaphores[vulkanCommandBuffer->waitSemaphoreCount] = swapchainData->imageAvailableSemaphore;
	vulkanCommandBuffer->waitSemaphoreCount += 1;

	if (vulkanCommandBuffer->signalSemaphoreCount == vulkanCommandBuffer->signalSemaphoreCapacity)
	{
		vulkanCommandBuffer->signalSemaphoreCapacity += 1;
		vulkanCommandBuffer->signalSemaphores = SDL_realloc(
			vulkanCommandBuffer->signalSemaphores,
			vulkanCommandBuffer->signalSemaphoreCapacity * sizeof(VkSemaphore)
		);
	}

	vulkanCommandBuffer->signalSemaphores[vulkanCommandBuffer->signalSemaphoreCount] = swapchainData->renderFinishedSemaphore;
	vulkanCommandBuffer->signalSemaphoreCount += 1;

	*pWidth = swapchainData->extent.width;
	*pHeight = swapchainData->extent.height;

	return (Refresh_Texture*) swapchainTextureContainer;
}

static Refresh_TextureFormat VULKAN_GetSwapchainFormat(
	Refresh_Renderer *driverData,
	void *windowHandle
) {
	WindowData *windowData = VULKAN_INTERNAL_FetchWindowData(windowHandle);

	if (windowData == NULL)
	{
		Refresh_LogWarn("Cannot get swapchain format, window has not been claimed!");
		return 0;
	}

	if (windowData->swapchainData == NULL)
	{
		Refresh_LogWarn("Cannot get swapchain format, swapchain is currently invalid!");
		return 0;
	}

	if (windowData->swapchainData->swapchainFormat == VK_FORMAT_R8G8B8A8_UNORM)
	{
		return REFRESH_TEXTUREFORMAT_R8G8B8A8;
	}
	else if (windowData->swapchainData->swapchainFormat == VK_FORMAT_B8G8R8A8_UNORM)
	{
		return REFRESH_TEXTUREFORMAT_B8G8R8A8;
	}
	else
	{
		Refresh_LogWarn("Unrecognized swapchain format!");
		return 0;
	}
}

static void VULKAN_SetSwapchainPresentMode(
	Refresh_Renderer *driverData,
	void *windowHandle,
	Refresh_PresentMode presentMode
) {
	WindowData *windowData = VULKAN_INTERNAL_FetchWindowData(windowHandle);

	if (windowData == NULL)
	{
		Refresh_LogWarn("Cannot set present mode, window has not been claimed!");
		return;
	}

	VULKAN_INTERNAL_RecreateSwapchain(
		(VulkanRenderer *)driverData,
		windowData
	);
}

/* Submission structure */

static VkFence VULKAN_INTERNAL_AcquireFenceFromPool(
	VulkanRenderer *renderer
) {
	VkFenceCreateInfo fenceCreateInfo;
	VkFence fence;
	VkResult vulkanResult;

	if (renderer->fencePool.availableFenceCount == 0)
	{
		/* Create fence */
		fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceCreateInfo.pNext = NULL;
		fenceCreateInfo.flags = 0;

		vulkanResult = renderer->vkCreateFence(
			renderer->logicalDevice,
			&fenceCreateInfo,
			NULL,
			&fence
		);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResultAsError("vkCreateFence", vulkanResult);
			return NULL;
		}

		return fence;
	}

	SDL_LockMutex(renderer->fencePool.lock);

	fence = renderer->fencePool.availableFences[renderer->fencePool.availableFenceCount - 1];
	renderer->fencePool.availableFenceCount -= 1;

	vulkanResult = renderer->vkResetFences(
		renderer->logicalDevice,
		1,
		&fence
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResultAsError("vkResetFences", vulkanResult);
	}

	SDL_UnlockMutex(renderer->fencePool.lock);

	return fence;
}

static void VULKAN_INTERNAL_ReturnFenceToPool(
	VulkanRenderer *renderer,
	VkFence fence
) {
	SDL_LockMutex(renderer->fencePool.lock);

	EXPAND_ARRAY_IF_NEEDED(
		renderer->fencePool.availableFences,
		VkFence,
		renderer->fencePool.availableFenceCount + 1,
		renderer->fencePool.availableFenceCapacity,
		renderer->fencePool.availableFenceCapacity * 2
	);

	renderer->fencePool.availableFences[renderer->fencePool.availableFenceCount] = fence;
	renderer->fencePool.availableFenceCount += 1;

	SDL_UnlockMutex(renderer->fencePool.lock);
}

static void VULKAN_INTERNAL_PerformPendingDestroys(
	VulkanRenderer *renderer
) {
	int32_t i;

	SDL_LockMutex(renderer->disposeLock);

	for (i = renderer->texturesToDestroyCount - 1; i >= 0; i -= 1)
	{
		if (SDL_AtomicGet(&renderer->texturesToDestroy[i]->referenceCount) == 0)
		{
			VULKAN_INTERNAL_DestroyTexture(
				renderer,
				renderer->texturesToDestroy[i]
			);

			renderer->texturesToDestroy[i] = renderer->texturesToDestroy[renderer->texturesToDestroyCount - 1];
			renderer->texturesToDestroyCount -= 1;
		}
	}

	for (i = renderer->buffersToDestroyCount - 1; i >= 0; i -= 1)
	{
		if (SDL_AtomicGet(&renderer->buffersToDestroy[i]->referenceCount) == 0)
		{
			VULKAN_INTERNAL_DestroyBuffer(
				renderer,
				renderer->buffersToDestroy[i]);

			renderer->buffersToDestroy[i] = renderer->buffersToDestroy[renderer->buffersToDestroyCount - 1];
			renderer->buffersToDestroyCount -= 1;
		}
	}

	for (i = renderer->graphicsPipelinesToDestroyCount - 1; i >= 0; i -= 1)
	{
		if (SDL_AtomicGet(&renderer->graphicsPipelinesToDestroy[i]->referenceCount) == 0)
		{
			VULKAN_INTERNAL_DestroyGraphicsPipeline(
				renderer,
				renderer->graphicsPipelinesToDestroy[i]
			);

			renderer->graphicsPipelinesToDestroy[i] = renderer->graphicsPipelinesToDestroy[renderer->graphicsPipelinesToDestroyCount - 1];
			renderer->graphicsPipelinesToDestroyCount -= 1;
		}
	}

	for (i = renderer->computePipelinesToDestroyCount - 1; i >= 0; i -= 1)
	{
		if (SDL_AtomicGet(&renderer->computePipelinesToDestroy[i]->referenceCount) == 0)
		{
			VULKAN_INTERNAL_DestroyComputePipeline(
				renderer,
				renderer->computePipelinesToDestroy[i]
			);

			renderer->computePipelinesToDestroy[i] = renderer->computePipelinesToDestroy[renderer->computePipelinesToDestroyCount - 1];
			renderer->computePipelinesToDestroyCount -= 1 ;
		}
	}

	for (i = renderer->shaderModulesToDestroyCount - 1; i >= 0; i -= 1)
	{
		if (SDL_AtomicGet(&renderer->shaderModulesToDestroy[i]->referenceCount) == 0)
		{
			VULKAN_INTERNAL_DestroyShaderModule(
				renderer,
				renderer->shaderModulesToDestroy[i]
			);

			renderer->shaderModulesToDestroy[i] = renderer->shaderModulesToDestroy[renderer->shaderModulesToDestroyCount - 1];
			renderer->shaderModulesToDestroyCount -= 1;
		}
	}

	for (i = renderer->samplersToDestroyCount - 1; i >= 0; i -= 1)
	{
		if (SDL_AtomicGet(&renderer->samplersToDestroy[i]->referenceCount) == 0)
		{
			VULKAN_INTERNAL_DestroySampler(
				renderer,
				renderer->samplersToDestroy[i]
			);

			renderer->samplersToDestroy[i] = renderer->samplersToDestroy[renderer->samplersToDestroyCount - 1];
			renderer->samplersToDestroyCount -= 1;
		}
	}

	for (i = renderer->framebuffersToDestroyCount - 1; i >= 0; i -= 1)
	{
		if (SDL_AtomicGet(&renderer->framebuffersToDestroy[i]->referenceCount) == 0)
		{
			VULKAN_INTERNAL_DestroyFramebuffer(
				renderer,
				renderer->framebuffersToDestroy[i]
			);

			renderer->framebuffersToDestroy[i] = renderer->framebuffersToDestroy[renderer->framebuffersToDestroyCount - 1];
			renderer->framebuffersToDestroyCount -= 1;
		}
	}

	SDL_UnlockMutex(renderer->disposeLock);
}

static void VULKAN_INTERNAL_CleanCommandBuffer(
	VulkanRenderer *renderer,
	VulkanCommandBuffer *commandBuffer
) {
	uint32_t i;
	VulkanUniformBuffer *uniformBuffer;
	DescriptorSetData *descriptorSetData;

	if (commandBuffer->autoReleaseFence)
	{
		VULKAN_INTERNAL_ReturnFenceToPool(
			renderer,
			commandBuffer->inFlightFence
		);

		commandBuffer->inFlightFence = VK_NULL_HANDLE;
	}

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

	/* Clean up transfer buffers */

	for (i = 0; i < commandBuffer->transferBufferCount; i += 1)
	{
		if (commandBuffer->transferBuffers[i]->fromPool)
		{
			SDL_LockMutex(renderer->transferBufferPool.lock);

			commandBuffer->transferBuffers[i]->offset = 0;
			renderer->transferBufferPool.availableBuffers[renderer->transferBufferPool.availableBufferCount] = commandBuffer->transferBuffers[i];
			renderer->transferBufferPool.availableBufferCount += 1;

			SDL_UnlockMutex(renderer->transferBufferPool.lock);
		}
		else
		{
			VULKAN_INTERNAL_QueueDestroyBuffer(renderer, commandBuffer->transferBuffers[i]->buffer);
			SDL_free(commandBuffer->transferBuffers[i]);
			commandBuffer->transferBuffers[i] = NULL;
		}
	}

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

	/* Decrement reference counts */

	for (i = 0; i < commandBuffer->usedBufferCount; i += 1)
	{
		SDL_AtomicDecRef(&commandBuffer->usedBuffers[i]->referenceCount);
	}
	commandBuffer->usedBufferCount = 0;

	for (i = 0; i < commandBuffer->usedTextureCount; i += 1)
	{
		SDL_AtomicDecRef(&commandBuffer->usedTextures[i]->referenceCount);
	}
	commandBuffer->usedTextureCount = 0;

	for (i = 0; i < commandBuffer->usedSamplerCount; i += 1)
	{
		SDL_AtomicDecRef(&commandBuffer->usedSamplers[i]->referenceCount);
	}
	commandBuffer->usedSamplerCount = 0;

	for (i = 0; i < commandBuffer->usedGraphicsPipelineCount; i += 1)
	{
		SDL_AtomicDecRef(&commandBuffer->usedGraphicsPipelines[i]->referenceCount);
	}
	commandBuffer->usedGraphicsPipelineCount = 0;

	for (i = 0; i < commandBuffer->usedComputePipelineCount; i += 1)
	{
		SDL_AtomicDecRef(&commandBuffer->usedComputePipelines[i]->referenceCount);
	}
	commandBuffer->usedComputePipelineCount = 0;

	for (i = 0; i < commandBuffer->usedFramebufferCount; i += 1)
	{
		SDL_AtomicDecRef(&commandBuffer->usedFramebuffers[i]->referenceCount);
	}
	commandBuffer->usedFramebufferCount = 0;

	/* Reset presentation data */

	commandBuffer->presentDataCount = 0;
	commandBuffer->waitSemaphoreCount = 0;
	commandBuffer->signalSemaphoreCount = 0;

	/* Return command buffer to pool */

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

	result = renderer->vkDeviceWaitIdle(renderer->logicalDevice);

	if (result != VK_SUCCESS)
	{
		LogVulkanResultAsError("vkDeviceWaitIdle", result);
		return;
	}

	SDL_LockMutex(renderer->submitLock);

	for (i = renderer->submittedCommandBufferCount - 1; i >= 0; i -= 1)
	{
		commandBuffer = renderer->submittedCommandBuffers[i];
		VULKAN_INTERNAL_CleanCommandBuffer(renderer, commandBuffer);
	}

	VULKAN_INTERNAL_PerformPendingDestroys(renderer);

	SDL_UnlockMutex(renderer->submitLock);
}

static Refresh_Fence* VULKAN_SubmitAndAcquireFence(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer
) {
	VulkanCommandBuffer *vulkanCommandBuffer;

	VULKAN_Submit(driverData, commandBuffer);

	vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;
	vulkanCommandBuffer->autoReleaseFence = 0;

	return (Refresh_Fence*) vulkanCommandBuffer->inFlightFence;
}

static void VULKAN_Submit(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer
) {
	VulkanRenderer* renderer = (VulkanRenderer*)driverData;
	VkSubmitInfo submitInfo;
	VkPresentInfoKHR presentInfo;
	VulkanPresentData *presentData;
	VkResult vulkanResult, presentResult = VK_SUCCESS;
	VulkanCommandBuffer *vulkanCommandBuffer;
	VkPipelineStageFlags waitStages[MAX_PRESENT_COUNT];
	uint32_t swapchainImageIndex;
	uint8_t commandBufferCleaned = 0;
	VulkanMemorySubAllocator *allocator;
	int32_t i, j;

	SDL_LockMutex(renderer->submitLock);

	/* FIXME: Can this just be permanent? */
	for (i = 0; i < MAX_PRESENT_COUNT; i += 1)
	{
		waitStages[i] = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	}

	vulkanCommandBuffer = (VulkanCommandBuffer*) commandBuffer;

	for (j = 0; j < vulkanCommandBuffer->presentDataCount; j += 1)
	{
		swapchainImageIndex = vulkanCommandBuffer->presentDatas[j].swapchainImageIndex;

		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			vulkanCommandBuffer->commandBuffer,
			RESOURCE_ACCESS_PRESENT,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0,
			1,
			0,
			1,
			0,
			vulkanCommandBuffer->presentDatas[j].windowData->swapchainData->textureContainers[swapchainImageIndex].vulkanTexture->image,
			&vulkanCommandBuffer->presentDatas[j].windowData->swapchainData->textureContainers[swapchainImageIndex].vulkanTexture->resourceAccessType
		);
	}

	VULKAN_INTERNAL_EndCommandBuffer(renderer, vulkanCommandBuffer);

	vulkanCommandBuffer->autoReleaseFence = 1;
	vulkanCommandBuffer->inFlightFence = VULKAN_INTERNAL_AcquireFenceFromPool(renderer);

	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.pNext = NULL;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &vulkanCommandBuffer->commandBuffer;

	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.pWaitSemaphores = vulkanCommandBuffer->waitSemaphores;
	submitInfo.waitSemaphoreCount = vulkanCommandBuffer->waitSemaphoreCount;
	submitInfo.pSignalSemaphores = vulkanCommandBuffer->signalSemaphores;
	submitInfo.signalSemaphoreCount = vulkanCommandBuffer->signalSemaphoreCount;

	vulkanResult = renderer->vkQueueSubmit(
		renderer->unifiedQueue,
		1,
		&submitInfo,
		vulkanCommandBuffer->inFlightFence
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResultAsError("vkQueueSubmit", vulkanResult);
	}

	/* Mark command buffers as submitted */

	if (renderer->submittedCommandBufferCount + 1 >= renderer->submittedCommandBufferCapacity)
	{
		renderer->submittedCommandBufferCapacity = renderer->submittedCommandBufferCount + 1;

		renderer->submittedCommandBuffers = SDL_realloc(
			renderer->submittedCommandBuffers,
			sizeof(VulkanCommandBuffer*) * renderer->submittedCommandBufferCapacity
		);
	}

	renderer->submittedCommandBuffers[renderer->submittedCommandBufferCount] = vulkanCommandBuffer;
	renderer->submittedCommandBufferCount += 1;

	/* Present, if applicable */

	for (j = 0; j < vulkanCommandBuffer->presentDataCount; j += 1)
	{
		presentData = &vulkanCommandBuffer->presentDatas[j];

		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.pNext = NULL;
		presentInfo.pWaitSemaphores = &presentData->windowData->swapchainData->renderFinishedSemaphore;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pSwapchains = &presentData->windowData->swapchainData->swapchain;
		presentInfo.swapchainCount = 1;
		presentInfo.pImageIndices = &presentData->swapchainImageIndex;
		presentInfo.pResults = NULL;

		presentResult = renderer->vkQueuePresentKHR(
			renderer->unifiedQueue,
			&presentInfo
		);

		if (presentResult != VK_SUCCESS)
		{
			VULKAN_INTERNAL_RecreateSwapchain(
				renderer,
				presentData->windowData
			);
		}
	}

	/* Check if we can perform any cleanups */

	for (i = renderer->submittedCommandBufferCount - 1; i >= 0; i -= 1)
	{
		vulkanResult = renderer->vkGetFenceStatus(
			renderer->logicalDevice,
			renderer->submittedCommandBuffers[i]->inFlightFence
		);

		if (vulkanResult == VK_SUCCESS)
		{
			VULKAN_INTERNAL_CleanCommandBuffer(
				renderer,
				renderer->submittedCommandBuffers[i]
			);

			commandBufferCleaned = 1;
		}
	}

	if (commandBufferCleaned)
	{
		SDL_LockMutex(renderer->allocatorLock);

		for (i = 0; i < VK_MAX_MEMORY_TYPES; i += 1)
		{
			allocator = &renderer->memoryAllocator->subAllocators[i];

			for (j = allocator->allocationCount - 1; j >= 0; j -= 1)
			{
				if (allocator->allocations[j]->usedRegionCount == 0)
				{
					VULKAN_INTERNAL_DeallocateMemory(
						renderer,
						allocator,
						j
					);
				}
			}
		}

		SDL_UnlockMutex(renderer->allocatorLock);
	}

	/* Check pending destroys */

	VULKAN_INTERNAL_PerformPendingDestroys(renderer);

	/* Defrag! */
	if (renderer->needDefrag && !renderer->defragInProgress)
	{
		if (SDL_GetTicks64() >= renderer->defragTimestamp)
		{
			VULKAN_INTERNAL_DefragmentMemory(renderer);
		}
	}

	SDL_UnlockMutex(renderer->submitLock);
}

static uint8_t VULKAN_INTERNAL_DefragmentMemory(
	VulkanRenderer *renderer
) {
	VulkanMemorySubAllocator allocator;
	VulkanMemoryAllocation *allocation;
	uint32_t allocationIndexToDefrag;
	VulkanMemoryUsedRegion *currentRegion;
	VulkanBuffer* newBuffer;
	VulkanTexture* newTexture;
	VkBufferCopy bufferCopy;
	VkImageCopy *imageCopyRegions;
	VulkanCommandBuffer *commandBuffer;
	uint32_t i, level;
	VulkanResourceAccessType copyResourceAccessType = RESOURCE_ACCESS_NONE;
	VulkanResourceAccessType originalResourceAccessType;

	SDL_LockMutex(renderer->allocatorLock);

	renderer->needDefrag = 0;
	renderer->defragInProgress = 1;

	commandBuffer = (VulkanCommandBuffer*) VULKAN_AcquireCommandBuffer((Refresh_Renderer *) renderer);

	if (VULKAN_INTERNAL_FindAllocationToDefragment(
		renderer,
		&allocator,
		&allocationIndexToDefrag
	)) {
		allocation = allocator.allocations[allocationIndexToDefrag];

		VULKAN_INTERNAL_MakeMemoryUnavailable(
			renderer,
			allocation
		);

		/* For each used region in the allocation
		 * create a new resource, copy the data
		 * and re-point the resource containers
		 */
		for (i = 0; i < allocation->usedRegionCount; i += 1)
		{
			currentRegion = allocation->usedRegions[i];
			copyResourceAccessType = RESOURCE_ACCESS_NONE;

			if (currentRegion->isBuffer)
			{
				currentRegion->vulkanBuffer->usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

				newBuffer = VULKAN_INTERNAL_CreateBuffer(
					renderer,
					currentRegion->vulkanBuffer->size,
					RESOURCE_ACCESS_NONE,
					currentRegion->vulkanBuffer->usage,
					currentRegion->vulkanBuffer->preferDeviceLocal,
					0
				);

				if (newBuffer == NULL)
				{
					Refresh_LogError("Failed to create defrag buffer!");
					return 0;
				}

				originalResourceAccessType = currentRegion->vulkanBuffer->resourceAccessType;

				VULKAN_INTERNAL_BufferMemoryBarrier(
					renderer,
					commandBuffer->commandBuffer,
					RESOURCE_ACCESS_TRANSFER_READ,
					currentRegion->vulkanBuffer
				);

				VULKAN_INTERNAL_BufferMemoryBarrier(
					renderer,
					commandBuffer->commandBuffer,
					RESOURCE_ACCESS_TRANSFER_WRITE,
					newBuffer
				);

				bufferCopy.srcOffset = 0;
				bufferCopy.dstOffset = 0;
				bufferCopy.size = currentRegion->resourceSize;

				renderer->vkCmdCopyBuffer(
					commandBuffer->commandBuffer,
					currentRegion->vulkanBuffer->buffer,
					newBuffer->buffer,
					1,
					&bufferCopy
				);

				VULKAN_INTERNAL_BufferMemoryBarrier(
					renderer,
					commandBuffer->commandBuffer,
					originalResourceAccessType,
					newBuffer
				);

				VULKAN_INTERNAL_TrackBuffer(renderer, commandBuffer, currentRegion->vulkanBuffer);
				VULKAN_INTERNAL_TrackBuffer(renderer, commandBuffer, newBuffer);

				/* re-point original container to new buffer */
				if (currentRegion->vulkanBuffer->container != NULL)
				{
					newBuffer->container = currentRegion->vulkanBuffer->container;
					newBuffer->container->vulkanBuffer = newBuffer;
					currentRegion->vulkanBuffer->container = NULL;
				}

				VULKAN_INTERNAL_QueueDestroyBuffer(renderer, currentRegion->vulkanBuffer);

				renderer->needDefrag = 1;
			}
			else
			{
				newTexture = VULKAN_INTERNAL_CreateTexture(
					renderer,
					currentRegion->vulkanTexture->dimensions.width,
					currentRegion->vulkanTexture->dimensions.height,
					currentRegion->vulkanTexture->depth,
					currentRegion->vulkanTexture->isCube,
					currentRegion->vulkanTexture->levelCount,
					currentRegion->vulkanTexture->sampleCount,
					currentRegion->vulkanTexture->format,
					currentRegion->vulkanTexture->aspectFlags,
					currentRegion->vulkanTexture->usageFlags
				);

				if (newTexture == NULL)
				{
					Refresh_LogError("Failed to create defrag texture!");
					return 0;
				}

				originalResourceAccessType = currentRegion->vulkanTexture->resourceAccessType;

				VULKAN_INTERNAL_ImageMemoryBarrier(
					renderer,
					commandBuffer->commandBuffer,
					RESOURCE_ACCESS_TRANSFER_READ,
					currentRegion->vulkanTexture->aspectFlags,
					0,
					currentRegion->vulkanTexture->layerCount,
					0,
					currentRegion->vulkanTexture->levelCount,
					0,
					currentRegion->vulkanTexture->image,
					&currentRegion->vulkanTexture->resourceAccessType
				);

				VULKAN_INTERNAL_ImageMemoryBarrier(
					renderer,
					commandBuffer->commandBuffer,
					RESOURCE_ACCESS_TRANSFER_WRITE,
					currentRegion->vulkanTexture->aspectFlags,
					0,
					currentRegion->vulkanTexture->layerCount,
					0,
					currentRegion->vulkanTexture->levelCount,
					0,
					newTexture->image,
					&copyResourceAccessType
				);

				imageCopyRegions = SDL_stack_alloc(VkImageCopy, currentRegion->vulkanTexture->levelCount);

				for (level = 0; level < currentRegion->vulkanTexture->levelCount; level += 1)
				{
					imageCopyRegions[level].srcOffset.x = 0;
					imageCopyRegions[level].srcOffset.y = 0;
					imageCopyRegions[level].srcOffset.z = 0;
					imageCopyRegions[level].srcSubresource.aspectMask = currentRegion->vulkanTexture->aspectFlags;
					imageCopyRegions[level].srcSubresource.baseArrayLayer = 0;
					imageCopyRegions[level].srcSubresource.layerCount = currentRegion->vulkanTexture->layerCount;
					imageCopyRegions[level].srcSubresource.mipLevel = level;
					imageCopyRegions[level].extent.width = SDL_max(1, currentRegion->vulkanTexture->dimensions.width >> level);
					imageCopyRegions[level].extent.height = SDL_max(1, currentRegion->vulkanTexture->dimensions.height >> level);
					imageCopyRegions[level].extent.depth = currentRegion->vulkanTexture->depth;
					imageCopyRegions[level].dstOffset.x = 0;
					imageCopyRegions[level].dstOffset.y = 0;
					imageCopyRegions[level].dstOffset.z = 0;
					imageCopyRegions[level].dstSubresource.aspectMask = currentRegion->vulkanTexture->aspectFlags;
					imageCopyRegions[level].dstSubresource.baseArrayLayer = 0;
					imageCopyRegions[level].dstSubresource.layerCount = currentRegion->vulkanTexture->layerCount;
					imageCopyRegions[level].dstSubresource.mipLevel = level;
				}

				renderer->vkCmdCopyImage(
					commandBuffer->commandBuffer,
					currentRegion->vulkanTexture->image,
					AccessMap[currentRegion->vulkanTexture->resourceAccessType].imageLayout,
					newTexture->image,
					AccessMap[copyResourceAccessType].imageLayout,
					currentRegion->vulkanTexture->levelCount,
					imageCopyRegions
				);

				VULKAN_INTERNAL_ImageMemoryBarrier(
					renderer,
					commandBuffer->commandBuffer,
					originalResourceAccessType,
					currentRegion->vulkanTexture->aspectFlags,
					0,
					currentRegion->vulkanTexture->layerCount,
					0,
					currentRegion->vulkanTexture->levelCount,
					0,
					newTexture->image,
					&copyResourceAccessType
				);

				SDL_stack_free(imageCopyRegions);

				VULKAN_INTERNAL_TrackTexture(renderer, commandBuffer, currentRegion->vulkanTexture);
				VULKAN_INTERNAL_TrackTexture(renderer, commandBuffer, newTexture);

				/* re-point original container to new texture */
				newTexture->container = currentRegion->vulkanTexture->container;
				newTexture->container->vulkanTexture = newTexture;
				currentRegion->vulkanTexture->container = NULL;

				VULKAN_INTERNAL_QueueDestroyTexture(renderer, currentRegion->vulkanTexture);

				renderer->needDefrag = 1;
			}
		}
	}

	SDL_UnlockMutex(renderer->allocatorLock);

	renderer->defragTimestamp = SDL_GetTicks64() + DEFRAG_TIME;

	VULKAN_Submit(
		(Refresh_Renderer*) renderer,
		(Refresh_CommandBuffer*) commandBuffer
	);

	renderer->defragInProgress = 0;

	return 1;
}

static void VULKAN_WaitForFences(
	Refresh_Renderer *driverData,
	uint8_t waitAll,
	uint32_t fenceCount,
	Refresh_Fence **pFences
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VkResult result;

	result = renderer->vkWaitForFences(
		renderer->logicalDevice,
		fenceCount,
		(VkFence*) pFences,
		waitAll,
		UINT64_MAX
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResultAsError("vkWaitForFences", result);
	}
}

static int VULKAN_QueryFence(
	Refresh_Renderer *driverData,
	Refresh_Fence *fence
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VkResult result;

	result = renderer->vkGetFenceStatus(
		renderer->logicalDevice,
		(VkFence) fence
	);

	if (result == VK_SUCCESS)
	{
		return 1;
	}
	else if (result == VK_NOT_READY)
	{
		return 0;
	}
	else
	{
		LogVulkanResultAsError("vkGetFenceStatus", result);
		return -1;
	}
}

static void VULKAN_ReleaseFence(
	Refresh_Renderer *driverData,
	Refresh_Fence *fence
) {
	VULKAN_INTERNAL_ReturnFenceToPool((VulkanRenderer*) driverData, (VkFence) fence);
}

/* Device instantiation */

static inline uint8_t CheckDeviceExtensions(
	VkExtensionProperties *extensions,
	uint32_t numExtensions,
	VulkanExtensions *supports
) {
	uint32_t i;

	SDL_memset(supports, '\0', sizeof(VulkanExtensions));
	for (i = 0; i < numExtensions; i += 1)
	{
		const char *name = extensions[i].extensionName;
		#define CHECK(ext) \
			if (SDL_strcmp(name, "VK_" #ext) == 0) \
			{ \
				supports->ext = 1; \
			}
		CHECK(KHR_swapchain)
		else CHECK(KHR_maintenance1)
		else CHECK(KHR_get_memory_requirements2)
		else CHECK(KHR_driver_properties)
		else CHECK(EXT_vertex_attribute_divisor)
		else CHECK(KHR_portability_subset)
		#undef CHECK
	}

	return (	supports->KHR_swapchain &&
			supports->KHR_maintenance1 &&
			supports->KHR_get_memory_requirements2	);
}

static inline uint32_t GetDeviceExtensionCount(VulkanExtensions *supports)
{
	return (
		supports->KHR_swapchain +
		supports->KHR_maintenance1 +
		supports->KHR_get_memory_requirements2 +
		supports->KHR_driver_properties +
		supports->EXT_vertex_attribute_divisor +
		supports->KHR_portability_subset
	);
}

static inline void CreateDeviceExtensionArray(
	VulkanExtensions *supports,
	const char **extensions
) {
	uint8_t cur = 0;
	#define CHECK(ext) \
		if (supports->ext) \
		{ \
			extensions[cur++] = "VK_" #ext; \
		}
	CHECK(KHR_swapchain)
	CHECK(KHR_maintenance1)
	CHECK(KHR_get_memory_requirements2)
	CHECK(KHR_driver_properties)
	CHECK(EXT_vertex_attribute_divisor)
	CHECK(KHR_portability_subset)
	#undef CHECK
}

static inline uint8_t SupportsInstanceExtension(
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
	availableExtensions = SDL_malloc(
		extensionCount * sizeof(VkExtensionProperties)
	);
	vkEnumerateInstanceExtensionProperties(
		NULL,
		&extensionCount,
		availableExtensions
	);

	for (i = 0; i < requiredExtensionsLength; i += 1)
	{
		if (!SupportsInstanceExtension(
			requiredExtensions[i],
			availableExtensions,
			extensionCount
		)) {
			allExtensionsSupported = 0;
			break;
		}
	}

	/* This is optional, but nice to have! */
	*supportsDebugUtils = SupportsInstanceExtension(
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
		availableExtensions,
		extensionCount
	);

	SDL_free(availableExtensions);
	return allExtensionsSupported;
}

static uint8_t VULKAN_INTERNAL_CheckDeviceExtensions(
	VulkanRenderer *renderer,
	VkPhysicalDevice physicalDevice,
	VulkanExtensions *physicalDeviceExtensions
) {
	uint32_t extensionCount;
	VkExtensionProperties *availableExtensions;
	uint8_t allExtensionsSupported;

	renderer->vkEnumerateDeviceExtensionProperties(
		physicalDevice,
		NULL,
		&extensionCount,
		NULL
	);
	availableExtensions = (VkExtensionProperties*) SDL_malloc(
		extensionCount * sizeof(VkExtensionProperties)
	);
	renderer->vkEnumerateDeviceExtensionProperties(
		physicalDevice,
		NULL,
		&extensionCount,
		availableExtensions
	);

	allExtensionsSupported = CheckDeviceExtensions(
		availableExtensions,
		extensionCount,
		physicalDeviceExtensions
	);

	SDL_free(availableExtensions);
	return allExtensionsSupported;
}

static uint8_t VULKAN_INTERNAL_CheckValidationLayers(
	const char** validationLayers,
	uint32_t validationLayersLength
) {
	uint32_t layerCount;
	VkLayerProperties *availableLayers;
	uint32_t i, j;
	uint8_t layerFound;

	vkEnumerateInstanceLayerProperties(&layerCount, NULL);
	availableLayers = (VkLayerProperties*) SDL_malloc(
		layerCount * sizeof(VkLayerProperties)
	);
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

	SDL_free(availableLayers);
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

static uint8_t VULKAN_INTERNAL_IsDeviceSuitable(
	VulkanRenderer *renderer,
	VkPhysicalDevice physicalDevice,
	VulkanExtensions *physicalDeviceExtensions,
	VkSurfaceKHR surface,
	uint32_t *queueFamilyIndex,
	uint8_t *deviceRank
) {
	uint32_t queueFamilyCount, queueFamilyRank, queueFamilyBest;
	SwapChainSupportDetails swapchainSupportDetails;
	VkQueueFamilyProperties *queueProps;
	VkBool32 supportsPresent;
	uint8_t querySuccess;
	VkPhysicalDeviceProperties deviceProperties;
	uint32_t i;

	/* Get the device rank before doing any checks, in case one fails.
	 * Note: If no dedicated device exists, one that supports our features
	 * would be fine
	 */
	renderer->vkGetPhysicalDeviceProperties(
		physicalDevice,
		&deviceProperties
	);
	if (*deviceRank < DEVICE_PRIORITY[deviceProperties.deviceType])
	{
		/* This device outranks the best device we've found so far!
		 * This includes a dedicated GPU that has less features than an
		 * integrated GPU, because this is a freak case that is almost
		 * never intentionally desired by the end user
		 */
		*deviceRank = DEVICE_PRIORITY[deviceProperties.deviceType];
	}
	else if (*deviceRank > DEVICE_PRIORITY[deviceProperties.deviceType])
	{
		/* Device is outranked by a previous device, don't even try to
		 * run a query and reset the rank to avoid overwrites
		 */
		*deviceRank = 0;
		return 0;
	}

	if (!VULKAN_INTERNAL_CheckDeviceExtensions(
		renderer,
		physicalDevice,
		physicalDeviceExtensions
	)) {
		return 0;
	}

	renderer->vkGetPhysicalDeviceQueueFamilyProperties(
		physicalDevice,
		&queueFamilyCount,
		NULL
	);

	queueProps = (VkQueueFamilyProperties*) SDL_stack_alloc(
		VkQueueFamilyProperties,
		queueFamilyCount
	);
	renderer->vkGetPhysicalDeviceQueueFamilyProperties(
		physicalDevice,
		&queueFamilyCount,
		queueProps
	);

	queueFamilyBest = 0;
	*queueFamilyIndex = UINT32_MAX;
	for (i = 0; i < queueFamilyCount; i += 1)
	{
		renderer->vkGetPhysicalDeviceSurfaceSupportKHR(
			physicalDevice,
			i,
			surface,
			&supportsPresent
		);
		if (	!supportsPresent ||
			!(queueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)	)
		{
			/* Not a graphics family, ignore. */
			continue;
		}

		/* The queue family bitflags are kind of annoying.
		 *
		 * We of course need a graphics family, but we ideally want the
		 * _primary_ graphics family. The spec states that at least one
		 * graphics family must also be a compute family, so generally
		 * drivers make that the first one. But hey, maybe something
		 * genuinely can't do compute or something, and FNA doesn't
		 * need it, so we'll be open to a non-compute queue family.
		 *
		 * Additionally, it's common to see the primary queue family
		 * have the transfer bit set, which is great! But this is
		 * actually optional; it's impossible to NOT have transfers in
		 * graphics/compute but it _is_ possible for a graphics/compute
		 * family, even the primary one, to just decide not to set the
		 * bitflag. Admittedly, a driver may want to isolate transfer
		 * queues to a dedicated family so that queues made solely for
		 * transfers can have an optimized DMA queue.
		 *
		 * That, or the driver author got lazy and decided not to set
		 * the bit. Looking at you, Android.
		 *
		 * -flibit
		 */
		if (queueProps[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
		{
			if (queueProps[i].queueFlags & VK_QUEUE_TRANSFER_BIT)
			{
				/* Has all attribs! */
				queueFamilyRank = 3;
			}
			else
			{
				/* Probably has a DMA transfer queue family */
				queueFamilyRank = 2;
			}
		}
		else
		{
			/* Just a graphics family, probably has something better */
			queueFamilyRank = 1;
		}
		if (queueFamilyRank > queueFamilyBest)
		{
			*queueFamilyIndex = i;
			queueFamilyBest = queueFamilyRank;
		}
	}

	SDL_stack_free(queueProps);

	if (*queueFamilyIndex == UINT32_MAX)
	{
		/* Somehow no graphics queues existed. Compute-only device? */
		return 0;
	}

	/* FIXME: Need better structure for checking vs storing support details */
	querySuccess = VULKAN_INTERNAL_QuerySwapChainSupport(
		renderer,
		physicalDevice,
		surface,
		&swapchainSupportDetails
	);
	if (swapchainSupportDetails.formatsLength > 0)
	{
		SDL_free(swapchainSupportDetails.formats);
	}
	if (swapchainSupportDetails.presentModesLength > 0)
	{
		SDL_free(swapchainSupportDetails.presentModes);
	}

	return (	querySuccess &&
			swapchainSupportDetails.formatsLength > 0 &&
			swapchainSupportDetails.presentModesLength > 0	);
}

static uint8_t VULKAN_INTERNAL_DeterminePhysicalDevice(
	VulkanRenderer *renderer,
	VkSurfaceKHR surface
) {
	VkResult vulkanResult;
	VkPhysicalDevice *physicalDevices;
	VulkanExtensions *physicalDeviceExtensions;
	uint32_t physicalDeviceCount, i, suitableIndex;
	uint32_t queueFamilyIndex, suitableQueueFamilyIndex;
	uint8_t deviceRank, highestRank;

	vulkanResult = renderer->vkEnumeratePhysicalDevices(
		renderer->instance,
		&physicalDeviceCount,
		NULL
	);
	VULKAN_ERROR_CHECK(vulkanResult, vkEnumeratePhysicalDevices, 0)

	if (physicalDeviceCount == 0)
	{
		Refresh_LogWarn("Failed to find any GPUs with Vulkan support");
		return 0;
	}

	physicalDevices = SDL_stack_alloc(VkPhysicalDevice, physicalDeviceCount);
	physicalDeviceExtensions = SDL_stack_alloc(VulkanExtensions, physicalDeviceCount);

	vulkanResult = renderer->vkEnumeratePhysicalDevices(
		renderer->instance,
		&physicalDeviceCount,
		physicalDevices
	);

	/* This should be impossible to hit, but from what I can tell this can
	 * be triggered not because the array is too small, but because there
	 * were drivers that turned out to be bogus, so this is the loader's way
	 * of telling us that the list is now smaller than expected :shrug:
	 */
	if (vulkanResult == VK_INCOMPLETE)
	{
		Refresh_LogWarn("vkEnumeratePhysicalDevices returned VK_INCOMPLETE, will keep trying anyway...");
		vulkanResult = VK_SUCCESS;
	}

	if (vulkanResult != VK_SUCCESS)
	{
		Refresh_LogWarn(
			"vkEnumeratePhysicalDevices failed: %s",
			VkErrorMessages(vulkanResult)
		);
		SDL_stack_free(physicalDevices);
		SDL_stack_free(physicalDeviceExtensions);
		return 0;
	}

	/* Any suitable device will do, but we'd like the best */
	suitableIndex = -1;
	highestRank = 0;
	for (i = 0; i < physicalDeviceCount; i += 1)
	{
		deviceRank = highestRank;
		if (VULKAN_INTERNAL_IsDeviceSuitable(
			renderer,
			physicalDevices[i],
			&physicalDeviceExtensions[i],
			surface,
			&queueFamilyIndex,
			&deviceRank
		)) {
			/* Use this for rendering.
			 * Note that this may override a previous device that
			 * supports rendering, but shares the same device rank.
			 */
			suitableIndex = i;
			suitableQueueFamilyIndex = queueFamilyIndex;
			highestRank = deviceRank;
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
			highestRank = deviceRank;
		}
	}

	if (suitableIndex != -1)
	{
		renderer->supports = physicalDeviceExtensions[suitableIndex];
		renderer->physicalDevice = physicalDevices[suitableIndex];
		renderer->queueFamilyIndex = suitableQueueFamilyIndex;
	}
	else
	{
		SDL_stack_free(physicalDevices);
		SDL_stack_free(physicalDeviceExtensions);
		return 0;
	}

	renderer->physicalDeviceProperties.sType =
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	if (renderer->supports.KHR_driver_properties)
	{
		renderer->physicalDeviceDriverProperties.sType =
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES_KHR;
		renderer->physicalDeviceDriverProperties.pNext = NULL;

		renderer->physicalDeviceProperties.pNext =
			&renderer->physicalDeviceDriverProperties;
	}
	else
	{
		renderer->physicalDeviceProperties.pNext = NULL;
	}

	renderer->vkGetPhysicalDeviceProperties2KHR(
		renderer->physicalDevice,
		&renderer->physicalDeviceProperties
	);

	renderer->vkGetPhysicalDeviceMemoryProperties(
		renderer->physicalDevice,
		&renderer->memoryProperties
	);

	SDL_stack_free(physicalDevices);
	SDL_stack_free(physicalDeviceExtensions);
	return 1;
}

static uint8_t VULKAN_INTERNAL_CreateLogicalDevice(
	VulkanRenderer *renderer
) {
	VkResult vulkanResult;
	VkDeviceCreateInfo deviceCreateInfo;
	VkPhysicalDeviceFeatures deviceFeatures;
	VkPhysicalDevicePortabilitySubsetFeaturesKHR portabilityFeatures;
	const char **deviceExtensions;

	VkDeviceQueueCreateInfo queueCreateInfo;
	float queuePriority = 1.0f;

	queueCreateInfo.sType =
		VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueCreateInfo.pNext = NULL;
	queueCreateInfo.flags = 0;
	queueCreateInfo.queueFamilyIndex = renderer->queueFamilyIndex;
	queueCreateInfo.queueCount = 1;
	queueCreateInfo.pQueuePriorities = &queuePriority;

	/* specifying used device features */

	SDL_zero(deviceFeatures);
	deviceFeatures.fillModeNonSolid = VK_TRUE;
	deviceFeatures.samplerAnisotropy = VK_TRUE;
	deviceFeatures.multiDrawIndirect = VK_TRUE;
	deviceFeatures.independentBlend = VK_TRUE;

	/* creating the logical device */

	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	if (renderer->supports.KHR_portability_subset)
	{
		portabilityFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR;
		portabilityFeatures.pNext = NULL;
		portabilityFeatures.constantAlphaColorBlendFactors = VK_FALSE;
		portabilityFeatures.events = VK_FALSE;
		portabilityFeatures.imageViewFormatReinterpretation = VK_FALSE;
		portabilityFeatures.imageViewFormatSwizzle = VK_TRUE;
		portabilityFeatures.imageView2DOn3DImage = VK_FALSE;
		portabilityFeatures.multisampleArrayImage = VK_FALSE;
		portabilityFeatures.mutableComparisonSamplers = VK_FALSE;
		portabilityFeatures.pointPolygons = VK_FALSE;
		portabilityFeatures.samplerMipLodBias = VK_FALSE; /* Technically should be true, but eh */
		portabilityFeatures.separateStencilMaskRef = VK_FALSE;
		portabilityFeatures.shaderSampleRateInterpolationFunctions = VK_FALSE;
		portabilityFeatures.tessellationIsolines = VK_FALSE;
		portabilityFeatures.tessellationPointMode = VK_FALSE;
		portabilityFeatures.triangleFans = VK_FALSE;
		portabilityFeatures.vertexAttributeAccessBeyondStride = VK_FALSE;
		deviceCreateInfo.pNext = &portabilityFeatures;
	}
	else
	{
		deviceCreateInfo.pNext = NULL;
	}
	deviceCreateInfo.flags = 0;
	deviceCreateInfo.queueCreateInfoCount = 1;
	deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
	deviceCreateInfo.enabledLayerCount = 0;
	deviceCreateInfo.ppEnabledLayerNames = NULL;
	deviceCreateInfo.enabledExtensionCount = GetDeviceExtensionCount(
		&renderer->supports
	);
	deviceExtensions = SDL_stack_alloc(
		const char*,
		deviceCreateInfo.enabledExtensionCount
	);
	CreateDeviceExtensionArray(&renderer->supports, deviceExtensions);
	deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions;
	deviceCreateInfo.pEnabledFeatures = &deviceFeatures;

	vulkanResult = renderer->vkCreateDevice(
		renderer->physicalDevice,
		&deviceCreateInfo,
		NULL,
		&renderer->logicalDevice
	);
	SDL_stack_free(deviceExtensions);
	VULKAN_ERROR_CHECK(vulkanResult, vkCreateDevice, 0)

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
		renderer->queueFamilyIndex,
		0,
		&renderer->unifiedQueue
	);

	return 1;
}

static void VULKAN_INTERNAL_LoadEntryPoints(void)
{
	/* Required for MoltenVK support */
	SDL_setenv("MVK_CONFIG_FULL_IMAGE_VIEW_SWIZZLE", "1", 1);

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

static uint8_t VULKAN_INTERNAL_PrepareVulkan(
	VulkanRenderer *renderer
) {
	SDL_Window *dummyWindowHandle;
	VkSurfaceKHR surface;

	VULKAN_INTERNAL_LoadEntryPoints();

	dummyWindowHandle = SDL_CreateWindow(
		"Refresh Vulkan",
		0, 0,
		128, 128,
		SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN
	);

	if (dummyWindowHandle == NULL)
	{
		Refresh_LogWarn("Vulkan: Could not create dummy window");
		return 0;
	}

	if (!VULKAN_INTERNAL_CreateInstance(renderer, dummyWindowHandle))
	{
		SDL_DestroyWindow(dummyWindowHandle);
		SDL_free(renderer);
		Refresh_LogWarn("Vulkan: Could not create Vulkan instance");
		return 0;
	}

	if (!SDL_Vulkan_CreateSurface(
		(SDL_Window*) dummyWindowHandle,
		renderer->instance,
		&surface
	)) {
		SDL_DestroyWindow(dummyWindowHandle);
		SDL_free(renderer);
		Refresh_LogWarn(
			"SDL_Vulkan_CreateSurface failed: %s",
			SDL_GetError()
		);
		return 0;
	}

	#define VULKAN_INSTANCE_FUNCTION(ext, ret, func, params) \
		renderer->func = (vkfntype_##func) vkGetInstanceProcAddr(renderer->instance, #func);
	#include "Refresh_Driver_Vulkan_vkfuncs.h"

	if (!VULKAN_INTERNAL_DeterminePhysicalDevice(renderer, surface))
	{
		return 0;
	}

	renderer->vkDestroySurfaceKHR(
		renderer->instance,
		surface,
		NULL
	);
	SDL_DestroyWindow(dummyWindowHandle);

	return 1;
}

static uint8_t VULKAN_PrepareDriver(uint32_t *flags)
{
	/* Set up dummy VulkanRenderer */
	VulkanRenderer *renderer = (VulkanRenderer*) SDL_malloc(sizeof(VulkanRenderer));
	uint8_t result;

	SDL_memset(renderer, '\0', sizeof(VulkanRenderer));

	result = VULKAN_INTERNAL_PrepareVulkan(renderer);

	if (!result)
	{
		Refresh_LogWarn("Vulkan: Failed to determine a suitable physical device");
	}
	else
	{
		*flags = SDL_WINDOW_VULKAN;
	}

	renderer->vkDestroyInstance(renderer->instance, NULL);
	SDL_free(renderer);
	return result;
}

static Refresh_Device* VULKAN_CreateDevice(
	uint8_t debugMode
) {
	VulkanRenderer *renderer = (VulkanRenderer*) SDL_malloc(sizeof(VulkanRenderer));

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

	/* Variables: Image Format Detection */
	VkImageFormatProperties imageFormatProperties;

	/* Variables: Transfer buffer init */
	VulkanTransferBuffer *transferBuffer;

	SDL_memset(renderer, '\0', sizeof(VulkanRenderer));
	renderer->debugMode = debugMode;

	if (!VULKAN_INTERNAL_PrepareVulkan(renderer))
	{
		Refresh_LogError("Failed to initialize Vulkan!");
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

	if (!VULKAN_INTERNAL_CreateLogicalDevice(
		renderer
	)) {
		Refresh_LogError("Failed to create logical device");
		return NULL;
	}

	/* FIXME: just move this into this function */
	result = (Refresh_Device*) SDL_malloc(sizeof(Refresh_Device));
	ASSIGN_DRIVER(VULKAN)

	result->driverData = (Refresh_Renderer*) renderer;

	/*
	 * Create initial swapchain array
	 */

	renderer->claimedWindowCapacity = 1;
	renderer->claimedWindowCount = 0;
	renderer->claimedWindows = SDL_malloc(
		renderer->claimedWindowCapacity * sizeof(WindowData*)
	);

	/* Threading */

	renderer->allocatorLock = SDL_CreateMutex();
	renderer->disposeLock = SDL_CreateMutex();
	renderer->submitLock = SDL_CreateMutex();
	renderer->acquireCommandBufferLock = SDL_CreateMutex();
	renderer->renderPassFetchLock = SDL_CreateMutex();
	renderer->framebufferFetchLock = SDL_CreateMutex();
	renderer->renderTargetFetchLock = SDL_CreateMutex();

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
		renderer->memoryAllocator->subAllocators[i].memoryTypeIndex = i;
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

	renderer->dummyBuffer = VULKAN_INTERNAL_CreateBuffer(
		renderer,
		1,
		RESOURCE_ACCESS_GENERAL,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		0,
		1
	);

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

	renderer->renderTargetHashArray.elements = NULL;
	renderer->renderTargetHashArray.count = 0;
	renderer->renderTargetHashArray.capacity = 0;

	/* Initialize transfer buffer pool */

	renderer->transferBufferPool.lock = SDL_CreateMutex();

	renderer->transferBufferPool.availableBufferCapacity = 4;
	renderer->transferBufferPool.availableBufferCount = 0;
	renderer->transferBufferPool.availableBuffers = SDL_malloc(
		renderer->transferBufferPool.availableBufferCapacity * sizeof(VulkanTransferBuffer*)
	);

	for (i = 0; i < renderer->transferBufferPool.availableBufferCapacity; i += 1)
	{
		transferBuffer = SDL_malloc(sizeof(VulkanTransferBuffer));
		transferBuffer->offset = 0;
		transferBuffer->fromPool = 1;
		transferBuffer->buffer = VULKAN_INTERNAL_CreateBuffer(
			renderer,
			POOLED_TRANSFER_BUFFER_SIZE,
			RESOURCE_ACCESS_TRANSFER_READ_WRITE,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			1,
			1
		);

		if (transferBuffer->buffer == NULL)
		{
			Refresh_LogError("Failed to allocate transfer buffer!");
			SDL_free(transferBuffer);
		}

		renderer->transferBufferPool.availableBuffers[i] = transferBuffer;
		renderer->transferBufferPool.availableBufferCount += 1;
	}

	/* Initialize fence pool */

	renderer->fencePool.lock = SDL_CreateMutex();

	renderer->fencePool.availableFenceCapacity = 4;
	renderer->fencePool.availableFenceCount = 0;
	renderer->fencePool.availableFences = SDL_malloc(
		renderer->fencePool.availableFenceCapacity * sizeof(VkFence)
	);

	/* Some drivers don't support D16, so we have to fall back to D32. */

	vulkanResult = renderer->vkGetPhysicalDeviceImageFormatProperties(
		renderer->physicalDevice,
		VK_FORMAT_D16_UNORM,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_ASPECT_DEPTH_BIT,
		0,
		&imageFormatProperties
	);

	if (vulkanResult == VK_ERROR_FORMAT_NOT_SUPPORTED)
	{
		renderer->D16Format = VK_FORMAT_D32_SFLOAT;
	}
	else
	{
		renderer->D16Format = VK_FORMAT_D16_UNORM;
	}

	vulkanResult = renderer->vkGetPhysicalDeviceImageFormatProperties(
		renderer->physicalDevice,
		VK_FORMAT_D16_UNORM_S8_UINT,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
		0,
		&imageFormatProperties
	);

	if (vulkanResult == VK_ERROR_FORMAT_NOT_SUPPORTED)
	{
		renderer->D16S8Format = VK_FORMAT_D32_SFLOAT_S8_UINT;
	}
	else
	{
		renderer->D16S8Format = VK_FORMAT_D16_UNORM_S8_UINT;
	}

	/* Deferred destroy storage */

	renderer->texturesToDestroyCapacity = 16;
	renderer->texturesToDestroyCount = 0;

	renderer->texturesToDestroy = (VulkanTexture**)SDL_malloc(
		sizeof(VulkanTexture*) *
		renderer->texturesToDestroyCapacity
	);

	renderer->buffersToDestroyCapacity = 16;
	renderer->buffersToDestroyCount = 0;

	renderer->buffersToDestroy = SDL_malloc(
		sizeof(VulkanBuffer*) *
		renderer->buffersToDestroyCapacity
	);

	renderer->samplersToDestroyCapacity = 16;
	renderer->samplersToDestroyCount = 0;

	renderer->samplersToDestroy = SDL_malloc(
		sizeof(VulkanSampler*) *
		renderer->samplersToDestroyCapacity
	);

	renderer->graphicsPipelinesToDestroyCapacity = 16;
	renderer->graphicsPipelinesToDestroyCount = 0;

	renderer->graphicsPipelinesToDestroy = SDL_malloc(
		sizeof(VulkanGraphicsPipeline*) *
		renderer->graphicsPipelinesToDestroyCapacity
	);

	renderer->computePipelinesToDestroyCapacity = 16;
	renderer->computePipelinesToDestroyCount = 0;

	renderer->computePipelinesToDestroy = SDL_malloc(
		sizeof(VulkanComputePipeline*) *
		renderer->computePipelinesToDestroyCapacity
	);

	renderer->shaderModulesToDestroyCapacity = 16;
	renderer->shaderModulesToDestroyCount = 0;

	renderer->shaderModulesToDestroy = SDL_malloc(
		sizeof(VulkanShaderModule*) *
		renderer->shaderModulesToDestroyCapacity
	);

	renderer->framebuffersToDestroyCapacity = 16;
	renderer->framebuffersToDestroyCount = 0;
	renderer->framebuffersToDestroy = SDL_malloc(
		sizeof(VulkanFramebuffer*) *
		renderer->framebuffersToDestroyCapacity
	);

	renderer->needDefrag = 0;
	renderer->defragTimestamp = 0;
	renderer->defragInProgress = 0;

	return result;
}

Refresh_Driver VulkanDriver = {
	"Vulkan",
	VULKAN_PrepareDriver,
	VULKAN_CreateDevice
};

#endif //REFRESH_DRIVER_VULKAN
