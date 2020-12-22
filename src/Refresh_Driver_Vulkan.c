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
#define UBO_BUFFER_SIZE 8000000 				/* 8MB */
#define UBO_ACTUAL_SIZE (UBO_BUFFER_SIZE * 2)
#define SAMPLER_POOL_SIZE 100
#define SUB_BUFFER_COUNT 2

#define IDENTITY_SWIZZLE \
{ \
	VK_COMPONENT_SWIZZLE_IDENTITY, \
	VK_COMPONENT_SWIZZLE_IDENTITY, \
	VK_COMPONENT_SWIZZLE_IDENTITY, \
	VK_COMPONENT_SWIZZLE_IDENTITY \
}

#define NULL_RENDER_PASS (REFRESH_RenderPass*) 0

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
    VK_FORMAT_D24_UNORM_S8_UINT,
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

static VkFilter RefreshToVK_SamplerFilter[] =
{
	VK_FILTER_NEAREST,
	VK_FILTER_LINEAR
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
} QueueFamilyIndices;

typedef struct SwapChainSupportDetails
{
	VkSurfaceCapabilitiesKHR capabilities;
	VkSurfaceFormatKHR *formats;
	uint32_t formatsLength;
	VkPresentModeKHR *presentModes;
	uint32_t presentModesLength;
} SwapChainSupportDetails;

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

    REFRESH_PresentMode presentMode;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapChain;
    VkFormat swapChainFormat;
    VkComponentMapping swapChainSwizzle;
    VkImage *swapChainImages;
    VkImageView *swapChainImageViews;
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

	VkFence inFlightFence;
	VkSemaphore imageAvailableSemaphore;
	VkSemaphore renderFinishedSemaphore;

	VkCommandPool commandPool;
	VkCommandBuffer *inactiveCommandBuffers;
	VkCommandBuffer *activeCommandBuffers;
	VkCommandBuffer *submittedCommandBuffers;
	uint32_t inactiveCommandBufferCount;
	uint32_t activeCommandBufferCount;
	uint32_t submittedCommandBufferCount;
	uint32_t allocatedCommandBufferCount;
	uint32_t currentCommandCount;
	VkCommandBuffer currentCommandBuffer;
	uint32_t numActiveCommands;

	/*
	 * TODO: we can get rid of this reference when we
	 * come up with a clever descriptor set reuse system
	 */
	VkDescriptorPool *descriptorPools;
	uint32_t descriptorPoolCount;

	VkDescriptorPool UBODescriptorPool;
	VkDescriptorSetLayout vertexParamLayout;
	VkDescriptorSetLayout fragmentParamLayout;
	VkDescriptorSet vertexUBODescriptorSet;
	VkDescriptorSet fragmentUBODescriptorSet;

	VulkanBuffer *textureStagingBuffer;

	VulkanBuffer** buffersInUse;
	uint32_t buffersInUseCount;
	uint32_t buffersInUseCapacity;

	VulkanBuffer** submittedBuffers;
	uint32_t submittedBufferCount;
	uint32_t submittedBufferCapacity;

	VulkanBuffer *vertexUBO;
	VulkanBuffer *fragmentUBO;
	uint32_t minUBOAlignment;

	uint32_t vertexUBOOffset;
	uint32_t vertexUBOBlockSize;
	uint32_t vertexUBOBlockIncrement;

	uint32_t fragmentUBOOffset;
	uint32_t fragmentUBOBlockSize;
	uint32_t fragmentUBOBlockIncrement;

	uint32_t frameIndex;

	SDL_mutex *allocatorLock;
	SDL_mutex *commandLock;

    #define VULKAN_INSTANCE_FUNCTION(ext, ret, func, params) \
		vkfntype_##func func;
	#define VULKAN_DEVICE_FUNCTION(ext, ret, func, params) \
		vkfntype_##func func;
	#include "Refresh_Driver_Vulkan_vkfuncs.h"
} VulkanRenderer;

/* Image Data */

typedef struct VulkanTexture
{
	VulkanMemoryAllocation *allocation;
	VkDeviceSize offset;
	VkDeviceSize memorySize;

	VkImage image;
	VkImageView view;
	VkExtent2D dimensions;
	uint32_t depth;
	uint32_t layerCount;
	uint32_t levelCount;
	VkFormat format;
	VulkanResourceAccessType resourceAccessType;
} VulkanTexture;

typedef struct VulkanDepthStencilTexture
{
	VulkanMemoryAllocation *allocation;
	VkDeviceSize offset;
	VkDeviceSize memorySize;

	VkImage image;
	VkImageView view;
	VkExtent2D dimensions;
	VkFormat format;
	VulkanResourceAccessType resourceAccessType;
} VulkanDepthStencilTexture;

typedef struct VulkanColorTarget
{
	VulkanTexture *texture;
	VkImageView view;
	VulkanTexture *multisampleTexture;
	VkSampleCountFlags multisampleCount;
} VulkanColorTarget;

typedef struct VulkanDepthStencilTarget
{
	VulkanDepthStencilTexture *texture;
	VkImageView view;
} VulkanDepthStencilTarget;

/* Pipeline */

typedef struct VulkanGraphicsPipeline
{
	VkPipeline pipeline;
	VkPipelineLayout layout;
	VkDescriptorPool descriptorPool;
	VkDescriptorSetLayout vertexSamplerLayout;
	uint32_t vertexSamplerBindingCount;
	VkDescriptorSetLayout fragmentSamplerLayout;
	uint32_t fragmentSamplerBindingCount;
	VkDescriptorSet vertexSamplerDescriptorSet; /* updated by SetVertexSamplers */
	VkDescriptorSet fragmentSamplerDescriptorSet; /* updated by SetFragmentSamplers */
} VulkanGraphicsPipeline;

/* Forward declarations */

static void VULKAN_INTERNAL_BeginCommandBuffer(VulkanRenderer *renderer);

/* Macros */

#define RECORD_CMD(cmdCall)					\
	SDL_LockMutex(renderer->commandLock);			\
	if (renderer->currentCommandBuffer == NULL)		\
	{							\
		VULKAN_INTERNAL_BeginCommandBuffer(renderer);	\
	}							\
	cmdCall;						\
	renderer->numActiveCommands += 1;			\
	SDL_UnlockMutex(renderer->commandLock);

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
		REFRESH_LogError(
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

static inline uint32_t VULKAN_INTERNAL_NextHighestAlignment32(
	uint32_t n,
	uint32_t align
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

	REFRESH_LogError("Failed to find memory properties %X, filter %X", properties, typeFilter);
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
		REFRESH_LogError(
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
		REFRESH_LogError(
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
		REFRESH_LogError("Calling FindAvailableMemory with both a buffer and image handle is invalid!");
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
			REFRESH_LogError("Failed to acquire buffer memory requirements!");
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
			REFRESH_LogError("Failed to acquire image memory requirements!");
			return 0;
		}
	}
	else
	{
		REFRESH_LogError("Calling FindAvailableMemory with neither buffer nor image handle is invalid!");
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
		REFRESH_LogWarn("Failed to allocate memory!");
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

	RECORD_CMD(renderer->vkCmdPipelineBarrier(
		renderer->currentCommandBuffer,
		srcStages,
		dstStages,
		0,
		0,
		NULL,
		1,
		&memoryBarrier,
		0,
		NULL
	));

	buffer->resourceAccessType = nextResourceAccessType;
}

static void VULKAN_INTERNAL_ImageMemoryBarrier(
	VulkanRenderer *renderer,
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

	RECORD_CMD(renderer->vkCmdPipelineBarrier(
		renderer->currentCommandBuffer,
		srcStages,
		dstStages,
		0,
		0,
		NULL,
		0,
		NULL,
		1,
		&memoryBarrier
	));

	*resourceAccessType = nextAccess;
}

/* Resource Disposal */

static void VULKAN_INTERNAL_DestroyBuffer(
	VulkanRenderer *renderer,
	VulkanBuffer *buffer
) {
	uint32_t i;

	if (buffer->bound || buffer->boundSubmitted)
	{
		REFRESH_LogError("Cannot destroy a bound buffer!");
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

	renderer->vkDestroySwapchainKHR(
		renderer->logicalDevice,
		renderer->swapChain,
		NULL
	);
}

static void VULKAN_INTERNAL_PerformDeferredDestroys(VulkanRenderer* renderer)
{
	/* TODO */
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
		REFRESH_LogError(
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
			REFRESH_LogError(
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
			REFRESH_LogError(
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

	REFRESH_LogError("Desired surface format is unavailable.");
	return 0;
}

static uint8_t VULKAN_INTERNAL_ChooseSwapPresentMode(
	REFRESH_PresentMode desiredPresentInterval,
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
				REFRESH_LogInfo("Using " #m "!"); \
				return 1; \
			} \
		} \
		REFRESH_LogInfo(#m " unsupported.");

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
		REFRESH_LogError(
			"Unrecognized PresentInterval: %d",
			desiredPresentInterval
		);
		return 0;
	}

	#undef CHECK_MODE

	REFRESH_LogInfo("Fall back to VK_PRESENT_MODE_FIFO_KHR.");
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
		REFRESH_LogError("Device does not support swap chain creation");
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
		REFRESH_LogError("Device does not support swap chain format");
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
		REFRESH_LogError("Device does not support swap chain present mode");
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
		REFRESH_LogError("Failed to recreate swapchain");
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
	buffer->bound = 0;
	buffer->boundSubmitted = 0;
	buffer->resourceAccessType = resourceAccessType;
	buffer->usage = usage;
	buffer->subBufferCount = subBufferCount;
	buffer->subBuffers = SDL_malloc(
		sizeof(VulkanSubBuffer) * buffer->subBufferCount
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
		vulkanResult = renderer->vkCreateBuffer(
			renderer->logicalDevice,
			&bufferCreateInfo,
			NULL,
			&buffer->subBuffers[i]->buffer
		);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkCreateBuffer", vulkanResult);
			REFRESH_LogError("Failed to create VkBuffer");
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
			REFRESH_LogWarn("Out of buffer memory!");
			return 2;
		}
		else if (findMemoryResult == 0)
		{
			REFRESH_LogError("Failed to find buffer memory!");
			return 0;
		}

		vulkanResult = renderer->vkBindBufferMemory(
			renderer->logicalDevice,
			buffer->subBuffers[i]->buffer,
			buffer->subBuffers[i]->allocation->memory,
			buffer->subBuffers[i]->offset
		);

		if (vulkanResult != VK_SUCCESS)
		{
			REFRESH_LogError("Failed to bind buffer memory!");
			return 0;
		}

		buffer->subBuffers[i]->resourceAccessType = resourceAccessType;
		buffer->subBuffers[i]->bound = -1;

		VULKAN_INTERNAL_BufferMemoryBarrier(
			renderer,
			buffer->resourceAccessType,
			buffer,
			buffer->subBuffers[i]
		);
	}

	return 1;
}

/* Command Buffers */

static void VULKAN_INTERNAL_BeginCommandBuffer(VulkanRenderer *renderer)
{
	VkCommandBufferAllocateInfo allocateInfo;
	VkCommandBufferBeginInfo beginInfo;
	VkResult result;

	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.pNext = NULL;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	beginInfo.pInheritanceInfo = NULL;

	/* If we are out of unused command buffers, allocate some more */
	if (renderer->inactiveCommandBufferCount == 0)
	{
		renderer->activeCommandBuffers = SDL_realloc(
			renderer->activeCommandBuffers,
			sizeof(VkCommandBuffer) * renderer->allocatedCommandBufferCount * 2
		);

		renderer->inactiveCommandBuffers = SDL_realloc(
			renderer->inactiveCommandBuffers,
			sizeof(VkCommandBuffer) * renderer->allocatedCommandBufferCount * 2
		);

		renderer->submittedCommandBuffers = SDL_realloc(
			renderer->submittedCommandBuffers,
			sizeof(VkCommandBuffer) * renderer->allocatedCommandBufferCount * 2
		);

		allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocateInfo.pNext = NULL;
		allocateInfo.commandPool = renderer->commandPool;
		allocateInfo.commandBufferCount = renderer->allocatedCommandBufferCount;
		allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

		result = renderer->vkAllocateCommandBuffers(
			renderer->logicalDevice,
			&allocateInfo,
			renderer->inactiveCommandBuffers
		);

		if (result != VK_SUCCESS)
		{
			LogVulkanResult("vkAllocateCommandBuffers", result);
			return;
		}

		renderer->inactiveCommandBufferCount = renderer->allocatedCommandBufferCount;
		renderer->allocatedCommandBufferCount *= 2;
	}

	renderer->currentCommandBuffer =
		renderer->inactiveCommandBuffers[renderer->inactiveCommandBufferCount - 1];

	renderer->activeCommandBuffers[renderer->activeCommandBufferCount] = renderer->currentCommandBuffer;

	renderer->activeCommandBufferCount += 1;
	renderer->inactiveCommandBufferCount -= 1;

	result = renderer->vkBeginCommandBuffer(
		renderer->currentCommandBuffer,
		&beginInfo
	);

	if (result != VK_SUCCESS)
	{
		LogVulkanResult("vkBeginCommandBuffer", result);
	}
}

/* Public API */

static void VULKAN_DestroyDevice(
    REFRESH_Device *device
) {
    SDL_assert(0);
}

static void VULKAN_Clear(
	REFRESH_Renderer *driverData,
	REFRESH_ClearOptions options,
	REFRESH_Vec4 **colors,
    uint32_t colorCount,
	float depth,
	int32_t stencil
) {
    SDL_assert(0);
}

static void VULKAN_DrawInstancedPrimitives(
	REFRESH_Renderer *driverData,
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
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanGraphicsPipeline *pipeline = (VulkanGraphicsPipeline*) graphicsPipeline;
	VkDescriptorSet descriptorSets[4];
	uint32_t dynamicOffsets[2];

	descriptorSets[0] = pipeline->vertexSamplerDescriptorSet;
	descriptorSets[1] = pipeline->fragmentSamplerDescriptorSet;
	descriptorSets[2] = renderer->vertexUBODescriptorSet;
	descriptorSets[3] = renderer->fragmentUBODescriptorSet;

	dynamicOffsets[0] = renderer->vertexUBOOffset;
	dynamicOffsets[1] = renderer->fragmentUBOOffset;

	RECORD_CMD(renderer->vkCmdBindDescriptorSets(
		renderer->currentCommandBuffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		pipeline->layout,
		0,
		4,
		descriptorSets,
		2,
		dynamicOffsets
	));

	RECORD_CMD(renderer->vkCmdDrawIndexed(
		renderer->currentCommandBuffer,
		PrimitiveVerts(primitiveType, primitiveCount),
		instanceCount,
		startIndex,
		baseVertex,
		0
	));
}

static void VULKAN_DrawIndexedPrimitives(
	REFRESH_Renderer *driverData,
	REFRESH_GraphicsPipeline *graphicsPipeline,
	REFRESH_PrimitiveType primitiveType,
	uint32_t baseVertex,
	uint32_t minVertexIndex,
	uint32_t numVertices,
	uint32_t startIndex,
	uint32_t primitiveCount,
	REFRESH_Buffer *indices,
	REFRESH_IndexElementSize indexElementSize
) {
	VULKAN_DrawInstancedPrimitives(
		driverData,
		graphicsPipeline,
		primitiveType,
		baseVertex,
		minVertexIndex,
		numVertices,
		startIndex,
		primitiveCount,
		1,
		indices,
		indexElementSize
	);
}

static void VULKAN_DrawPrimitives(
	REFRESH_Renderer *driverData,
	REFRESH_GraphicsPipeline *graphicsPipeline,
	REFRESH_PrimitiveType primitiveType,
	uint32_t vertexStart,
	uint32_t primitiveCount
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanGraphicsPipeline *pipeline = (VulkanGraphicsPipeline*) graphicsPipeline;
	VkDescriptorSet descriptorSets[4];
	uint32_t dynamicOffsets[2];

	descriptorSets[0] = pipeline->vertexSamplerDescriptorSet;
	descriptorSets[1] = pipeline->fragmentSamplerDescriptorSet;
	descriptorSets[2] = renderer->vertexUBODescriptorSet;
	descriptorSets[3] = renderer->fragmentUBODescriptorSet;

	dynamicOffsets[0] = renderer->vertexUBOOffset;
	dynamicOffsets[1] = renderer->fragmentUBOOffset;

	RECORD_CMD(renderer->vkCmdBindDescriptorSets(
		renderer->currentCommandBuffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		pipeline->layout,
		0,
		4,
		descriptorSets,
		2,
		dynamicOffsets
	));

	RECORD_CMD(renderer->vkCmdDraw(
		renderer->currentCommandBuffer,
		PrimitiveVerts(
			primitiveType,
			primitiveCount
		),
		1,
		vertexStart,
		0
	));
}

static REFRESH_RenderPass* VULKAN_CreateRenderPass(
	REFRESH_Renderer *driverData,
	REFRESH_RenderPassCreateInfo *renderPassCreateInfo
) {
    VulkanRenderer *renderer = (VulkanRenderer*) driverData;

    VkResult vulkanResult;
    VkAttachmentDescription attachmentDescriptions[2 * MAX_RENDERTARGET_BINDINGS + 1];
    VkAttachmentReference colorAttachmentReferences[MAX_RENDERTARGET_BINDINGS];
    VkAttachmentReference resolveReferences[MAX_RENDERTARGET_BINDINGS + 1];
    VkAttachmentReference depthStencilAttachmentReference;
	VkRenderPassCreateInfo vkRenderPassCreateInfo;
    VkSubpassDescription subpass;
    VkRenderPass renderPass;
    uint32_t i;

    uint32_t attachmentDescriptionCount = 0;
    uint32_t colorAttachmentReferenceCount = 0;
    uint32_t resolveReferenceCount = 0;

    for (i = 0; i < renderPassCreateInfo->colorTargetCount; i += 1)
    {
        if (renderPassCreateInfo->colorTargetDescriptions[attachmentDescriptionCount].multisampleCount > REFRESH_SAMPLECOUNT_1)
        {
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

    return (REFRESH_RenderPass*) renderPass;
}

static uint8_t VULKAN_INTERNAL_CreateSamplerDescriptorPool(
	VulkanRenderer *renderer,
	REFRESH_PipelineLayoutCreateInfo *pipelineLayoutCreateInfo,
	VkDescriptorPool *pDescriptorPool
) {
	VkResult vulkanResult;

	VkDescriptorPoolSize poolSizes[2];
	VkDescriptorPoolCreateInfo descriptorPoolInfo;

	poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[0].descriptorCount = SAMPLER_POOL_SIZE;

	poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[1].descriptorCount = SAMPLER_POOL_SIZE;

	descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolInfo.pNext = NULL;
	descriptorPoolInfo.flags = 0;
	descriptorPoolInfo.maxSets = 2 * SAMPLER_POOL_SIZE;
	descriptorPoolInfo.poolSizeCount = 2;
	descriptorPoolInfo.pPoolSizes = poolSizes;

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

static REFRESH_GraphicsPipeline* VULKAN_CreateGraphicsPipeline(
	REFRESH_Renderer *driverData,
	REFRESH_GraphicsPipelineCreateInfo *pipelineCreateInfo
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

	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo;
	VkPipelineLayout pipelineLayout;
	VkDescriptorSetLayout setLayouts[4];
	VkDescriptorSetLayoutCreateInfo setLayoutCreateInfo;

	VkDescriptorSetLayoutBinding *vertexSamplerLayoutBindings = SDL_stack_alloc(
		VkDescriptorSetLayoutBinding,
		pipelineCreateInfo->pipelineLayoutCreateInfo.vertexSamplerBindingCount
	);
	VkDescriptorSetLayoutBinding *fragmentSamplerLayoutBindings = SDL_stack_alloc(
		VkDescriptorSetLayoutBinding,
		pipelineCreateInfo->pipelineLayoutCreateInfo.fragmentSamplerBindingCount
	);

	VulkanRenderer *renderer = (VulkanRenderer*) driverData;

	/* Shader stages */

	shaderStageCreateInfos[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageCreateInfos[0].pNext = NULL;
	shaderStageCreateInfos[0].flags = 0;
	shaderStageCreateInfos[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStageCreateInfos[0].module = (VkShaderModule) pipelineCreateInfo->vertexShaderState.shaderModule;
	shaderStageCreateInfos[0].pName = pipelineCreateInfo->vertexShaderState.entryPointName;
	shaderStageCreateInfos[0].pSpecializationInfo = NULL;

	shaderStageCreateInfos[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageCreateInfos[1].pNext = NULL;
	shaderStageCreateInfos[1].flags = 0;
	shaderStageCreateInfos[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStageCreateInfos[1].module = (VkShaderModule) pipelineCreateInfo->fragmentShaderState.shaderModule;
	shaderStageCreateInfos[1].pName = pipelineCreateInfo->fragmentShaderState.entryPointName;
	shaderStageCreateInfos[1].pSpecializationInfo = NULL;

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
		pipelineCreateInfo->topologyState.topology
	];

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
		pipelineCreateInfo->multisampleState.sampleMask;
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
		pipelineCreateInfo->colorBlendState.blendOpEnable;
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
	/* TODO: should we hash these? */

	/* Vertex sampler layout */
	/* TODO: should we let the user split up images and samplers? */
	for (i = 0; i < pipelineCreateInfo->pipelineLayoutCreateInfo.vertexSamplerBindingCount; i += 1)
	{
		vertexSamplerLayoutBindings[i].binding =
			pipelineCreateInfo->pipelineLayoutCreateInfo.vertexSamplerBindings[i];
		vertexSamplerLayoutBindings[i].descriptorCount = 1;
		vertexSamplerLayoutBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		vertexSamplerLayoutBindings[i].stageFlags = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
		vertexSamplerLayoutBindings[i].pImmutableSamplers = NULL;
	}

	setLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setLayoutCreateInfo.pNext = NULL;
	setLayoutCreateInfo.flags = 0;
	setLayoutCreateInfo.bindingCount = pipelineCreateInfo->pipelineLayoutCreateInfo.vertexSamplerBindingCount;
	setLayoutCreateInfo.pBindings = vertexSamplerLayoutBindings;

	vulkanResult = renderer->vkCreateDescriptorSetLayout(
		renderer->logicalDevice,
		&setLayoutCreateInfo,
		NULL,
		&setLayouts[0]
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateDescriptorSetLayout", vulkanResult);
		REFRESH_LogError("Failed to create vertex sampler layout!");

		SDL_stack_free(vertexInputBindingDescriptions);
		SDL_stack_free(vertexInputAttributeDescriptions);
		SDL_stack_free(viewports);
		SDL_stack_free(scissors);
		SDL_stack_free(colorBlendAttachmentStates);
		SDL_stack_free(vertexSamplerLayoutBindings);
		SDL_stack_free(fragmentSamplerLayoutBindings);
		return NULL;
	}

	/* Frag sampler layout */

	for (i = 0; i < pipelineCreateInfo->pipelineLayoutCreateInfo.fragmentSamplerBindingCount; i += 1)
	{
		fragmentSamplerLayoutBindings[i].binding =
			pipelineCreateInfo->pipelineLayoutCreateInfo.fragmentSamplerBindings[i];
		fragmentSamplerLayoutBindings[i].descriptorCount = 1;
		fragmentSamplerLayoutBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		fragmentSamplerLayoutBindings[i].stageFlags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		fragmentSamplerLayoutBindings[i].pImmutableSamplers = NULL;
	}

	setLayoutCreateInfo.bindingCount = pipelineCreateInfo->pipelineLayoutCreateInfo.fragmentSamplerBindingCount;
	setLayoutCreateInfo.pBindings = fragmentSamplerLayoutBindings;

	vulkanResult = renderer->vkCreateDescriptorSetLayout(
		renderer->logicalDevice,
		&setLayoutCreateInfo,
		NULL,
		&setLayouts[1]
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateDescriptorSetLayout", vulkanResult);
		REFRESH_LogError("Failed to create fragment sampler layout!");

		SDL_stack_free(vertexInputBindingDescriptions);
		SDL_stack_free(vertexInputAttributeDescriptions);
		SDL_stack_free(viewports);
		SDL_stack_free(scissors);
		SDL_stack_free(colorBlendAttachmentStates);
		SDL_stack_free(vertexSamplerLayoutBindings);
		SDL_stack_free(fragmentSamplerLayoutBindings);
		return NULL;
	}

	setLayouts[2] = renderer->vertexParamLayout;
	setLayouts[3] = renderer->fragmentParamLayout;

	pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCreateInfo.pNext = NULL;
	pipelineLayoutCreateInfo.flags = 0;
	pipelineLayoutCreateInfo.setLayoutCount = 4;
	pipelineLayoutCreateInfo.pSetLayouts = setLayouts;

	vulkanResult = renderer->vkCreatePipelineLayout(
		renderer->logicalDevice,
		&pipelineLayoutCreateInfo,
		NULL,
		&pipelineLayout
	);

	graphicsPipeline->vertexSamplerLayout = setLayouts[0];
	graphicsPipeline->fragmentSamplerLayout = setLayouts[1];
	graphicsPipeline->vertexSamplerBindingCount = pipelineCreateInfo->pipelineLayoutCreateInfo.vertexSamplerBindingCount;
	graphicsPipeline->fragmentSamplerBindingCount = pipelineCreateInfo->pipelineLayoutCreateInfo.fragmentSamplerBindingCount;
	graphicsPipeline->layout = pipelineLayout;

	/* Pipeline */

	vkPipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
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
	vkPipelineCreateInfo.layout = pipelineLayout;
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
		REFRESH_LogError("Failed to create graphics pipeline!");

		SDL_stack_free(vertexInputBindingDescriptions);
		SDL_stack_free(vertexInputAttributeDescriptions);
		SDL_stack_free(viewports);
		SDL_stack_free(scissors);
		SDL_stack_free(colorBlendAttachmentStates);
		SDL_stack_free(vertexSamplerLayoutBindings);
		SDL_stack_free(fragmentSamplerLayoutBindings);
		return NULL;
	}

	SDL_stack_free(vertexInputBindingDescriptions);
	SDL_stack_free(vertexInputAttributeDescriptions);
	SDL_stack_free(viewports);
	SDL_stack_free(scissors);
	SDL_stack_free(colorBlendAttachmentStates);
	SDL_stack_free(vertexSamplerLayoutBindings);
	SDL_stack_free(fragmentSamplerLayoutBindings);

	if (!VULKAN_INTERNAL_CreateSamplerDescriptorPool(
		renderer,
		&pipelineCreateInfo->pipelineLayoutCreateInfo,
		&graphicsPipeline->descriptorPool
	)) {
		REFRESH_LogError("Failed to create descriptor pool!");
		return NULL;
	}

	renderer->descriptorPools = SDL_realloc(
		renderer->descriptorPools,
		renderer->descriptorPoolCount + 1
	);
	renderer->descriptorPools[renderer->descriptorPoolCount + 1] =
		graphicsPipeline->descriptorPool;

	return (REFRESH_GraphicsPipeline*) graphicsPipeline;
}

static REFRESH_Sampler* VULKAN_CreateSampler(
	REFRESH_Renderer *driverData,
	REFRESH_SamplerStateCreateInfo *samplerStateCreateInfo
) {
	VkResult vulkanResult;
	VkSampler sampler;

	VulkanRenderer* renderer = (VulkanRenderer*)driverData;

	VkSamplerCreateInfo vkSamplerCreateInfo;
	vkSamplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	vkSamplerCreateInfo.pNext = NULL;
	vkSamplerCreateInfo.flags = 0;
	vkSamplerCreateInfo.magFilter = RefreshToVK_SamplerFilter[
		samplerStateCreateInfo->magFilter
	];
	vkSamplerCreateInfo.minFilter = RefreshToVK_SamplerFilter[
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

	return (REFRESH_Sampler*) sampler;
}

static REFRESH_Framebuffer* VULKAN_CreateFramebuffer(
	REFRESH_Renderer *driverData,
	REFRESH_FramebufferCreateInfo *framebufferCreateInfo
) {
	VkResult vulkanResult;
	VkFramebuffer framebuffer;
	VkFramebufferCreateInfo vkFramebufferCreateInfo;

	VkImageView *imageViews;
	uint32_t colorAttachmentCount = framebufferCreateInfo->colorTargetCount;
	uint32_t attachmentCount = colorAttachmentCount;
	uint32_t i;

	VulkanRenderer *renderer = (VulkanRenderer*) driverData;

	if (framebufferCreateInfo->pDepthTarget != NULL)
	{
		attachmentCount += 1;
	}

	imageViews = SDL_stack_alloc(VkImageView, attachmentCount);

	for (i = 0; i < colorAttachmentCount; i += 1)
	{
		imageViews[i] = ((VulkanColorTarget*)framebufferCreateInfo->pColorTargets[i])->view;
	}

	if (framebufferCreateInfo->pDepthTarget != NULL)
	{
		imageViews[colorAttachmentCount] = ((VulkanDepthStencilTarget*)framebufferCreateInfo->pDepthTarget)->view;
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
		&framebuffer
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateFramebuffer", vulkanResult);
		SDL_stack_free(imageViews);
		return NULL;
	}

	SDL_stack_free(imageViews);
	return (REFRESH_Framebuffer*) framebuffer;
}

static REFRESH_ShaderModule* VULKAN_CreateShaderModule(
	REFRESH_Renderer *driverData,
	REFRESH_ShaderModuleCreateInfo *shaderModuleCreateInfo
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
		REFRESH_LogError("Failed to create shader module!");
		return NULL;
	}

	return (REFRESH_ShaderModule*) shaderModule;
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
	VkImageUsageFlags usage,
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

	if (isCube)
	{
		imageCreateFlags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	}
	else if (is3D)
	{
		imageCreateFlags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
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
	imageCreateInfo.usage = usage;
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
		REFRESH_LogError("Failed to create texture!");
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
		REFRESH_LogError("Failed to find texture memory!");
		return 0;
	}

	vulkanResult = renderer->vkBindImageMemory(
		renderer->logicalDevice,
		texture->image,
		texture->allocation->memory,
		texture->offset
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkBindImageMemory", vulkanResult);
		REFRESH_LogError("Failed to bind texture memory!");
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
		REFRESH_LogError("invalid image type: %u", imageType);
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
		REFRESH_LogError("Failed to create texture image view");
		return 0;
	}

	texture->dimensions.width = width;
	texture->dimensions.height = height;
	texture->depth = depth;
	texture->format = format;
	texture->levelCount = levelCount;
	texture->layerCount = layerCount;
	texture->resourceAccessType = RESOURCE_ACCESS_NONE;

	return 1;
}

static uint8_t VULKAN_INTERNAL_CreateTextureDepthStencil(
	VulkanRenderer *renderer,
	uint32_t width,
	uint32_t height,
	VkFormat format,
	VulkanDepthStencilTexture *texture
) {
	VkResult vulkanResult;
	VkImageCreateInfo imageCreateInfo;
	VkImageViewCreateInfo imageViewCreateInfo;
	uint8_t findMemoryResult;
	uint8_t layerCount = 1;
	VkComponentMapping swizzle = IDENTITY_SWIZZLE;
	uint32_t usageFlags = (
		VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
	);
	VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;

	imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageCreateInfo.pNext = NULL;
	imageCreateInfo.flags = 0;
	imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	imageCreateInfo.format = format;
	imageCreateInfo.extent.width = width;
	imageCreateInfo.extent.height = height;
	imageCreateInfo.extent.depth = 1;
	imageCreateInfo.mipLevels = 1;
	imageCreateInfo.arrayLayers = layerCount;
	imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCreateInfo.usage = usageFlags;
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
		REFRESH_LogError("Failed to create texture!");
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
		REFRESH_LogError("Failed to find texture memory!");
		return 0;
	}

	vulkanResult = renderer->vkBindImageMemory(
		renderer->logicalDevice,
		texture->image,
		texture->allocation->memory,
		texture->offset
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkBindImageMemory", vulkanResult);
		REFRESH_LogError("Failed to bind texture memory!");
		return 0;
	}

	if (DepthFormatContainsStencil(format))
	{
		aspectFlags |= VK_IMAGE_ASPECT_STENCIL_BIT;
	}

	imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imageViewCreateInfo.pNext = NULL;
	imageViewCreateInfo.flags = 0;
	imageViewCreateInfo.image = texture->image;
	imageViewCreateInfo.format = format;
	imageViewCreateInfo.components = swizzle;
	imageViewCreateInfo.subresourceRange.aspectMask = aspectFlags;
	imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
	imageViewCreateInfo.subresourceRange.levelCount = 1;
	imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
	imageViewCreateInfo.subresourceRange.layerCount = layerCount;
	imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;

	vulkanResult = renderer->vkCreateImageView(
		renderer->logicalDevice,
		&imageViewCreateInfo,
		NULL,
		&texture->view
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateImageView", vulkanResult);
		REFRESH_LogError("Failed to create texture image view");
		return 0;
	}

	texture->dimensions.width = width;
	texture->dimensions.height = height;
	texture->format = format;
	texture->resourceAccessType = RESOURCE_ACCESS_NONE;

	return 1;
}

static REFRESH_Texture* VULKAN_CreateTexture2D(
	REFRESH_Renderer *driverData,
	REFRESH_SurfaceFormat format,
	uint32_t width,
	uint32_t height,
	uint32_t levelCount,
	uint8_t canBeRenderTarget
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTexture *result;
	uint32_t usageFlags = (
		VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT
	);

	if (canBeRenderTarget)
	{
		usageFlags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}

	result = (VulkanTexture*) SDL_malloc(sizeof(VulkanTexture));

	VULKAN_INTERNAL_CreateTexture(
		renderer,
		width,
		height,
		1,
		0,
		VK_SAMPLE_COUNT_1_BIT,
		levelCount,
		RefreshToVK_SurfaceFormat[format],
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_TYPE_2D,
		usageFlags,
		result
	);

	return (REFRESH_Texture*) result;
}

static REFRESH_Texture* VULKAN_CreateTexture3D(
	REFRESH_Renderer *driverData,
	REFRESH_SurfaceFormat format,
	uint32_t width,
	uint32_t height,
	uint32_t depth,
	uint32_t levelCount,
	uint8_t canBeRenderTarget
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTexture *result;
	uint32_t usageFlags = (
		VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT
	);

	if (canBeRenderTarget)
	{
		usageFlags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}

	result = (VulkanTexture*) SDL_malloc(sizeof(VulkanTexture));

	VULKAN_INTERNAL_CreateTexture(
		renderer,
		width,
		height,
		depth,
		0,
		VK_SAMPLE_COUNT_1_BIT,
		levelCount,
		RefreshToVK_SurfaceFormat[format],
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_TYPE_3D,
		usageFlags,
		result
	);

	return (REFRESH_Texture*) result;
}

static REFRESH_Texture* VULKAN_CreateTextureCube(
	REFRESH_Renderer *driverData,
	REFRESH_SurfaceFormat format,
	uint32_t size,
	uint32_t levelCount,
	uint8_t canBeRenderTarget
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTexture *result;
	uint32_t usageFlags = (
		VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT
	);

	if (canBeRenderTarget)
	{
		usageFlags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}

	result = (VulkanTexture*) SDL_malloc(sizeof(VulkanTexture));

	VULKAN_INTERNAL_CreateTexture(
		renderer,
		size,
		size,
		1,
		1,
		VK_SAMPLE_COUNT_1_BIT,
		levelCount,
		RefreshToVK_SurfaceFormat[format],
		VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_TYPE_2D,
		usageFlags,
		result
	);

	return (REFRESH_Texture*) result;
}

static REFRESH_ColorTarget* VULKAN_CreateColorTarget(
	REFRESH_Renderer *driverData,
	REFRESH_SampleCount multisampleCount,
	REFRESH_TextureSlice textureSlice
) {
	VkResult vulkanResult;
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanColorTarget *colorTarget = (VulkanColorTarget*) SDL_malloc(sizeof(VulkanColorTarget));
	VkImageViewCreateInfo imageViewCreateInfo;
	VkComponentMapping swizzle = IDENTITY_SWIZZLE;

	colorTarget->texture = (VulkanTexture*) textureSlice.texture;
	colorTarget->multisampleTexture = NULL;
	colorTarget->multisampleCount = 1;

	/* create resolve target for multisample */
	if (multisampleCount > 1)
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
			RefreshToVK_SurfaceFormat[colorTarget->texture->format],
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_TYPE_2D,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			colorTarget->multisampleTexture
		);
		colorTarget->multisampleCount = multisampleCount;

		VULKAN_INTERNAL_ImageMemoryBarrier(
			renderer,
			RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE,
			VK_IMAGE_ASPECT_COLOR_BIT,
			0,
			colorTarget->multisampleTexture->layerCount,
			0,
			colorTarget->multisampleTexture->levelCount,
			0,
			colorTarget->multisampleTexture->image,
			&colorTarget->multisampleTexture->resourceAccessType
		);
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
	imageViewCreateInfo.subresourceRange.baseArrayLayer = textureSlice.layer;
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
		REFRESH_LogError("Failed to create color attachment image view");
		return NULL;
	}

	return (REFRESH_ColorTarget*) colorTarget;
}

static REFRESH_DepthStencilTarget* VULKAN_CreateDepthStencilTarget(
	REFRESH_Renderer *driverData,
	uint32_t width,
	uint32_t height,
	REFRESH_DepthFormat format
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanDepthStencilTarget *depthStencilTarget =
		(VulkanDepthStencilTarget*) SDL_malloc(
			sizeof(VulkanDepthStencilTarget)
		);

	VulkanDepthStencilTexture *texture =
		(VulkanDepthStencilTexture*) SDL_malloc(
			sizeof(VulkanDepthStencilTexture)
		);

	VULKAN_INTERNAL_CreateTextureDepthStencil(
		renderer,
		width,
		height,
		RefreshToVK_DepthFormat[format],
		texture
	);

	depthStencilTarget->texture = texture;
	depthStencilTarget->view = texture->view;

    return (REFRESH_DepthStencilTarget*) depthStencilTarget;
}

static REFRESH_Buffer* VULKAN_CreateVertexBuffer(
	REFRESH_Renderer *driverData,
	uint32_t sizeInBytes
) {
	VulkanBuffer *buffer = (VulkanBuffer*) SDL_malloc(sizeof(VulkanBuffer));

	if(!VULKAN_INTERNAL_CreateBuffer(
		(VulkanRenderer*) driverData,
		sizeInBytes,
		RESOURCE_ACCESS_VERTEX_BUFFER,
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		SUB_BUFFER_COUNT,
		buffer
	)) {
		REFRESH_LogError("Failed to create vertex buffer!");
		return NULL;
	}

	return (REFRESH_Buffer*) buffer;
}

static REFRESH_Buffer* VULKAN_CreateIndexBuffer(
	REFRESH_Renderer *driverData,
	uint32_t sizeInBytes
) {
	VulkanBuffer *buffer = (VulkanBuffer*) SDL_malloc(sizeof(VulkanBuffer));

	if (!VULKAN_INTERNAL_CreateBuffer(
		(VulkanRenderer*) driverData,
		sizeInBytes,
		RESOURCE_ACCESS_INDEX_BUFFER,
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		SUB_BUFFER_COUNT,
		buffer
	)) {
		REFRESH_LogError("Failed to create index buffer!");
		return NULL;
	}

	return (REFRESH_Buffer*) buffer;
}

/* Setters */

static void VULKAN_INTERNAL_DestroyTextureStagingBuffer(
	VulkanRenderer *renderer
) {
	VULKAN_INTERNAL_DestroyBuffer(
		renderer,
		renderer->textureStagingBuffer
	);
}

static void VULKAN_INTERNAL_MaybeExpandStagingBuffer(
	VulkanRenderer *renderer,
	VkDeviceSize size
) {
	if (size <= renderer->textureStagingBuffer->size)
	{
		return;
	}

	VULKAN_INTERNAL_DestroyTextureStagingBuffer(renderer);

	renderer->textureStagingBuffer = (VulkanBuffer*) SDL_malloc(sizeof(VulkanBuffer));

	if (!VULKAN_INTERNAL_CreateBuffer(
		renderer,
		size,
		RESOURCE_ACCESS_MEMORY_TRANSFER_READ_WRITE,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		1,
		renderer->textureStagingBuffer
	)) {
		REFRESH_LogError("Failed to expand texture staging buffer!");
		return;
	}
}

static void VULKAN_SetTextureData2D(
	REFRESH_Renderer *driverData,
	REFRESH_Texture *texture,
	uint32_t x,
	uint32_t y,
	uint32_t w,
	uint32_t h,
	uint32_t level,
	void *data,
	uint32_t dataLengthInBytes
) {
	VkResult vulkanResult;
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTexture *vulkanTexture = (VulkanTexture*) texture;
	VkBufferImageCopy imageCopy;
	uint8_t *mapPointer;

	VULKAN_INTERNAL_MaybeExpandStagingBuffer(renderer, dataLengthInBytes);

	vulkanResult = renderer->vkMapMemory(
		renderer->logicalDevice,
		renderer->textureStagingBuffer->subBuffers[0]->allocation->memory,
		renderer->textureStagingBuffer->subBuffers[0]->offset,
		renderer->textureStagingBuffer->subBuffers[0]->size,
		0,
		(void**) &mapPointer
	);

	if (vulkanResult != VK_SUCCESS)
	{
		REFRESH_LogError("Failed to map buffer memory!");
		return;
	}

	SDL_memcpy(mapPointer, data, dataLengthInBytes);

	renderer->vkUnmapMemory(
		renderer->logicalDevice,
		renderer->textureStagingBuffer->subBuffers[0]->allocation->memory
	);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
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

	imageCopy.imageExtent.width = w;
	imageCopy.imageExtent.height = h;
	imageCopy.imageExtent.depth = 1;
	imageCopy.imageOffset.x = x;
	imageCopy.imageOffset.y = y;
	imageCopy.imageOffset.z = 0;
	imageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageCopy.imageSubresource.baseArrayLayer = 0;
	imageCopy.imageSubresource.layerCount = 1;
	imageCopy.imageSubresource.mipLevel = level;
	imageCopy.bufferOffset = 0;
	imageCopy.bufferRowLength = 0;
	imageCopy.bufferImageHeight = 0;

	RECORD_CMD(renderer->vkCmdCopyBufferToImage(
		renderer->currentCommandBuffer,
		renderer->textureStagingBuffer->subBuffers[0]->buffer,
		vulkanTexture->image,
		AccessMap[vulkanTexture->resourceAccessType].imageLayout,
		1,
		&imageCopy
	));
}

static void VULKAN_SetTextureData3D(
	REFRESH_Renderer *driverData,
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
) {
	VkResult vulkanResult;
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTexture *vulkanTexture = (VulkanTexture*) texture;
	VkBufferImageCopy imageCopy;
	uint8_t *mapPointer;

	VULKAN_INTERNAL_MaybeExpandStagingBuffer(renderer, dataLength);

	vulkanResult = renderer->vkMapMemory(
		renderer->logicalDevice,
		renderer->textureStagingBuffer->subBuffers[0]->allocation->memory,
		renderer->textureStagingBuffer->subBuffers[0]->offset,
		renderer->textureStagingBuffer->subBuffers[0]->size,
		0,
		(void**) &mapPointer
	);

	if (vulkanResult != VK_SUCCESS)
	{
		REFRESH_LogError("Failed to map buffer memory!");
		return;
	}

	SDL_memcpy(mapPointer, data, dataLength);

	renderer->vkUnmapMemory(
		renderer->logicalDevice,
		renderer->textureStagingBuffer->subBuffers[0]->allocation->memory
	);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
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

	imageCopy.imageExtent.width = w;
	imageCopy.imageExtent.height = h;
	imageCopy.imageExtent.depth = d;
	imageCopy.imageOffset.x = x;
	imageCopy.imageOffset.y = y;
	imageCopy.imageOffset.z = z;
	imageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageCopy.imageSubresource.baseArrayLayer = 0;
	imageCopy.imageSubresource.layerCount = 1;
	imageCopy.imageSubresource.mipLevel = level;
	imageCopy.bufferOffset = 0;
	imageCopy.bufferRowLength = 0;
	imageCopy.bufferImageHeight = 0;

	RECORD_CMD(renderer->vkCmdCopyBufferToImage(
		renderer->currentCommandBuffer,
		renderer->textureStagingBuffer->subBuffers[0]->buffer,
		vulkanTexture->image,
		AccessMap[vulkanTexture->resourceAccessType].imageLayout,
		1,
		&imageCopy
	));
}

static void VULKAN_SetTextureDataCube(
	REFRESH_Renderer *driverData,
	REFRESH_Texture *texture,
	uint32_t x,
	uint32_t y,
	uint32_t w,
	uint32_t h,
	REFRESH_CubeMapFace cubeMapFace,
	uint32_t level,
	void* data,
	uint32_t dataLength
) {
	VkResult vulkanResult;
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTexture *vulkanTexture = (VulkanTexture*) texture;
	VkBufferImageCopy imageCopy;
	uint8_t *mapPointer;

	VULKAN_INTERNAL_MaybeExpandStagingBuffer(renderer, dataLength);

	vulkanResult = renderer->vkMapMemory(
		renderer->logicalDevice,
		renderer->textureStagingBuffer->subBuffers[0]->allocation->memory,
		renderer->textureStagingBuffer->subBuffers[0]->offset,
		renderer->textureStagingBuffer->subBuffers[0]->size,
		0,
		(void**) &mapPointer
	);

	if (vulkanResult != VK_SUCCESS)
	{
		REFRESH_LogError("Failed to map buffer memory!");
		return;
	}

	SDL_memcpy(mapPointer, data, dataLength);

	renderer->vkUnmapMemory(
		renderer->logicalDevice,
		renderer->textureStagingBuffer->subBuffers[0]->allocation->memory
	);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
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

	imageCopy.imageExtent.width = w;
	imageCopy.imageExtent.height = h;
	imageCopy.imageExtent.depth = 1;
	imageCopy.imageOffset.x = x;
	imageCopy.imageOffset.y = y;
	imageCopy.imageOffset.z = 0;
	imageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageCopy.imageSubresource.baseArrayLayer = cubeMapFace;
	imageCopy.imageSubresource.layerCount = 1;
	imageCopy.imageSubresource.mipLevel = level;
	imageCopy.bufferOffset = 0;
	imageCopy.bufferRowLength = 0; /* assumes tightly packed data */
	imageCopy.bufferImageHeight = 0; /* assumes tightly packed data */

	RECORD_CMD(renderer->vkCmdCopyBufferToImage(
		renderer->currentCommandBuffer,
		renderer->textureStagingBuffer->subBuffers[0]->buffer,
		vulkanTexture->image,
		AccessMap[vulkanTexture->resourceAccessType].imageLayout,
		1,
		&imageCopy
	));
}

static void VULKAN_SetTextureDataYUV(
	REFRESH_Renderer *driverData,
	REFRESH_Texture *y,
	REFRESH_Texture *u,
	REFRESH_Texture *v,
	uint32_t yWidth,
	uint32_t yHeight,
	uint32_t uvWidth,
	uint32_t uvHeight,
	void* data,
	uint32_t dataLength
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VulkanTexture *tex;
	uint8_t *dataPtr = (uint8_t*) data;
	int32_t yDataLength = BytesPerImage(yWidth, yHeight, REFRESH_SURFACEFORMAT_R8);
	int32_t uvDataLength = BytesPerImage(uvWidth, uvHeight, REFRESH_SURFACEFORMAT_R8);
	VkBufferImageCopy imageCopy;
	uint8_t *mapPointer;
	VkResult vulkanResult;

	VULKAN_INTERNAL_MaybeExpandStagingBuffer(renderer, dataLength);

	/* Initialize values that are the same for Y, U, and V */

	imageCopy.imageExtent.depth = 1;
	imageCopy.imageOffset.x = 0;
	imageCopy.imageOffset.y = 0;
	imageCopy.imageOffset.z = 0;
	imageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageCopy.imageSubresource.baseArrayLayer = 0;
	imageCopy.imageSubresource.layerCount = 1;
	imageCopy.imageSubresource.mipLevel = 0;
	imageCopy.bufferOffset = 0;

	/* Y */

	tex = (VulkanTexture*) y;

	vulkanResult = renderer->vkMapMemory(
		renderer->logicalDevice,
		renderer->textureStagingBuffer->subBuffers[0]->allocation->memory,
		renderer->textureStagingBuffer->subBuffers[0]->offset,
		renderer->textureStagingBuffer->subBuffers[0]->size,
		0,
		(void**) &mapPointer
	);

	if (vulkanResult != VK_SUCCESS)
	{
		REFRESH_LogError("Failed to map buffer memory!");
		return;
	}

	SDL_memcpy(
		mapPointer,
		dataPtr,
		yDataLength
	);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
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
	imageCopy.bufferRowLength = yWidth;
	imageCopy.bufferImageHeight = yHeight;

	RECORD_CMD(renderer->vkCmdCopyBufferToImage(
		renderer->currentCommandBuffer,
		renderer->textureStagingBuffer->subBuffers[0]->buffer,
		tex->image,
		AccessMap[tex->resourceAccessType].imageLayout,
		1,
		&imageCopy
	));

	/* These apply to both U and V */

	imageCopy.imageExtent.width = uvWidth;
	imageCopy.imageExtent.height = uvHeight;
	imageCopy.bufferRowLength = uvWidth;
	imageCopy.bufferImageHeight = uvHeight;

	/* U */

	imageCopy.bufferOffset = yDataLength;

	tex = (VulkanTexture*) u;

	SDL_memcpy(
		mapPointer + yDataLength,
		dataPtr + yDataLength,
		uvDataLength
	);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
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

	RECORD_CMD(renderer->vkCmdCopyBufferToImage(
		renderer->currentCommandBuffer,
		renderer->textureStagingBuffer->subBuffers[0]->buffer,
		tex->image,
		AccessMap[tex->resourceAccessType].imageLayout,
		1,
		&imageCopy
	));

	/* V */

	imageCopy.bufferOffset = yDataLength + uvDataLength;

	tex = (VulkanTexture*) v;

	SDL_memcpy(
		mapPointer + yDataLength + uvDataLength,
		dataPtr + yDataLength + uvDataLength,
		uvDataLength
	);

	renderer->vkUnmapMemory(
		renderer->logicalDevice,
		renderer->textureStagingBuffer->subBuffers[0]->allocation->memory
	);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
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

	RECORD_CMD(renderer->vkCmdCopyBufferToImage(
		renderer->currentCommandBuffer,
		renderer->textureStagingBuffer->subBuffers[0]->buffer,
		tex->image,
		AccessMap[tex->resourceAccessType].imageLayout,
		1,
		&imageCopy
	));
}

static void VULKAN_INTERNAL_SetBufferData(
	REFRESH_Renderer* driverData,
	REFRESH_Buffer* buffer,
	uint32_t offsetInBytes,
	void* data,
	uint32_t dataLength
) {
	VulkanRenderer* renderer = (VulkanRenderer*)driverData;
	VulkanBuffer* vulkanBuffer = (VulkanBuffer*)buffer;
	uint8_t* mapPointer;
	VkResult vulkanResult;

	#define SUBBUF vulkanBuffer->subBuffers[renderer->frameIndex]

	/* Buffer already bound, time to die */
	if (vulkanBuffer->subBuffers[renderer->frameIndex]->bound)
	{
		REFRESH_LogError("Buffer already bound. It is an error to write data to a buffer after binding before calling Present.");
		return;
	}

	/* Map the memory and perform the copy */
	vulkanResult = renderer->vkMapMemory(
		renderer->logicalDevice,
		SUBBUF->allocation->memory,
		SUBBUF->offset,
		SUBBUF->size,
		0,
		(void**)&mapPointer
	);

	if (vulkanResult != VK_SUCCESS)
	{
		REFRESH_LogError("Failed to map buffer memory!");
		return;
	}

	SDL_memcpy(
		mapPointer + offsetInBytes,
		data,
		dataLength
	);

	renderer->vkUnmapMemory(
		renderer->logicalDevice,
		SUBBUF->allocation->memory
	);

	#undef SUBBUF
}

static void VULKAN_SetVertexBufferData(
	REFRESH_Renderer *driverData,
	REFRESH_Buffer *buffer,
	uint32_t offsetInBytes,
	void* data,
	uint32_t elementCount,
	uint32_t vertexStride
) {
	VULKAN_INTERNAL_SetBufferData(
		driverData,
		buffer,
		offsetInBytes,
		data,
		elementCount * vertexStride
	);
}

static void VULKAN_SetIndexBufferData(
	REFRESH_Renderer *driverData,
	REFRESH_Buffer *buffer,
	uint32_t offsetInBytes,
	void* data,
	uint32_t dataLength
) {
	VULKAN_INTERNAL_SetBufferData(
		driverData,
		buffer,
		offsetInBytes,
		data,
		dataLength
	);
}

static void VULKAN_PushVertexShaderParams(
	REFRESH_Renderer *driverData,
    REFRESH_GraphicsPipeline *pipeline,
	void *data,
	uint32_t elementCount,
	uint32_t elementSizeInBytes
) {
	VulkanRenderer* renderer = (VulkanRenderer*)driverData;
	VulkanGraphicsPipeline* vulkanPipeline = (VulkanGraphicsPipeline*)pipeline;

	renderer->vertexUBOOffset += renderer->vertexUBOBlockIncrement;
	renderer->vertexUBOBlockSize = VULKAN_INTERNAL_NextHighestAlignment32(
		renderer->vertexUBOOffset,
		renderer->minUBOAlignment
	);
	renderer->vertexUBOBlockIncrement = renderer->vertexUBOBlockSize;

	if (renderer->vertexUBOOffset + renderer->vertexUBOBlockSize >= UBO_BUFFER_SIZE * renderer->frameIndex)
	{
		REFRESH_LogError("Vertex UBO overflow!");
		return;
	}

	VULKAN_INTERNAL_SetBufferData(
		driverData,
		(REFRESH_Buffer*) renderer->vertexUBO,
		renderer->vertexUBOOffset,
		data,
		elementCount * elementSizeInBytes
	);
}

static void VULKAN_PushFragmentShaderParams(
	REFRESH_Renderer *driverData,
    REFRESH_GraphicsPipeline *pipeline,
	void *data,
	uint32_t elementCount,
	uint32_t elementSizeInBytes
) {
	VulkanRenderer* renderer = (VulkanRenderer*)driverData;
	VulkanGraphicsPipeline* vulkanPipeline = (VulkanGraphicsPipeline*)pipeline;

	renderer->fragmentUBOOffset += renderer->fragmentUBOBlockIncrement;
	renderer->fragmentUBOBlockSize = VULKAN_INTERNAL_NextHighestAlignment32(
		renderer->fragmentUBOOffset,
		renderer->minUBOAlignment
	);
	renderer->fragmentUBOBlockIncrement = renderer->fragmentUBOBlockSize;

	if (renderer->fragmentUBOOffset + renderer->fragmentUBOBlockSize >= UBO_BUFFER_SIZE * renderer->frameIndex)
	{
		REFRESH_LogError("Fragment UBO overflow!");
		return;
	}

	VULKAN_INTERNAL_SetBufferData(
		driverData,
		(REFRESH_Buffer*) renderer->fragmentUBO,
		renderer->fragmentUBOOffset,
		data,
		elementCount * elementSizeInBytes
	);
}

static void VULKAN_SetVertexSamplers(
	REFRESH_Renderer *driverData,
	REFRESH_GraphicsPipeline *pipeline,
	REFRESH_Texture **pTextures,
	REFRESH_Sampler **pSamplers
) {
	/* TODO: we can defer and batch these */

	VkDescriptorSetAllocateInfo descriptorSetAllocateInfo;
	VkDescriptorSet descriptorSet;
	VkWriteDescriptorSet *writeDescriptorSets;
	VkDescriptorImageInfo *descriptorImageInfos;
	VulkanTexture *currentTexture;
	VkSampler currentSampler;
	uint32_t i;

	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanGraphicsPipeline *graphicsPipeline = (VulkanGraphicsPipeline*) pipeline;

	writeDescriptorSets = SDL_stack_alloc(VkWriteDescriptorSet, graphicsPipeline->vertexSamplerBindingCount);
	descriptorImageInfos = SDL_stack_alloc(VkDescriptorImageInfo, graphicsPipeline->vertexSamplerBindingCount);

	descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSetAllocateInfo.pNext = NULL;
	descriptorSetAllocateInfo.descriptorSetCount = 1;
	descriptorSetAllocateInfo.descriptorPool = graphicsPipeline->descriptorPool;
	descriptorSetAllocateInfo.pSetLayouts = &graphicsPipeline->vertexSamplerLayout;

	renderer->vkAllocateDescriptorSets(
		renderer->logicalDevice,
		&descriptorSetAllocateInfo,
		&descriptorSet
	);

	for (i = 0; i < graphicsPipeline->vertexSamplerBindingCount; i += 1)
	{
		currentTexture = (VulkanTexture*) pTextures[i];
		currentSampler = (VkSampler) pSamplers[i];

		descriptorImageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		descriptorImageInfos[i].imageView = currentTexture->view;
		descriptorImageInfos[i].sampler = currentSampler;

		writeDescriptorSets[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSets[i].pNext = NULL;
		writeDescriptorSets[i].dstSet = descriptorSet;
		writeDescriptorSets[i].dstBinding = i;
		writeDescriptorSets[i].dstArrayElement = 0;
		writeDescriptorSets[i].descriptorCount = 1;
		writeDescriptorSets[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writeDescriptorSets[i].pImageInfo = &descriptorImageInfos[i];
	}

	renderer->vkUpdateDescriptorSets(
		renderer->logicalDevice,
		graphicsPipeline->vertexSamplerBindingCount,
		writeDescriptorSets,
		0,
		NULL
	);

	graphicsPipeline->vertexSamplerDescriptorSet = descriptorSet;

	SDL_stack_free(writeDescriptorSets);
	SDL_stack_free(descriptorImageInfos);
}

static void VULKAN_SetFragmentSamplers(
	REFRESH_Renderer *driverData,
	REFRESH_GraphicsPipeline *pipeline,
	REFRESH_Texture **pTextures,
	REFRESH_Sampler **pSamplers
) {
	/* TODO: we can defer and batch these */

	VkDescriptorSetAllocateInfo descriptorSetAllocateInfo;
	VkDescriptorSet descriptorSet;
	VkWriteDescriptorSet *writeDescriptorSets;
	VkDescriptorImageInfo *descriptorImageInfos;
	VulkanTexture *currentTexture;
	VkSampler currentSampler;
	uint32_t i;

	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanGraphicsPipeline *graphicsPipeline = (VulkanGraphicsPipeline*) pipeline;

	writeDescriptorSets = SDL_stack_alloc(VkWriteDescriptorSet, graphicsPipeline->fragmentSamplerBindingCount);
	descriptorImageInfos = SDL_stack_alloc(VkDescriptorImageInfo, graphicsPipeline->fragmentSamplerBindingCount);

	descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSetAllocateInfo.pNext = NULL;
	descriptorSetAllocateInfo.descriptorSetCount = 1;
	descriptorSetAllocateInfo.descriptorPool = graphicsPipeline->descriptorPool;
	descriptorSetAllocateInfo.pSetLayouts = &graphicsPipeline->fragmentSamplerLayout;

	renderer->vkAllocateDescriptorSets(
		renderer->logicalDevice,
		&descriptorSetAllocateInfo,
		&descriptorSet
	);

	for (i = 0; i < graphicsPipeline->fragmentSamplerBindingCount; i += 1)
	{
		currentTexture = (VulkanTexture*) pTextures[i];
		currentSampler = (VkSampler) pSamplers[i];

		descriptorImageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		descriptorImageInfos[i].imageView = currentTexture->view;
		descriptorImageInfos[i].sampler = currentSampler;

		writeDescriptorSets[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSets[i].pNext = NULL;
		writeDescriptorSets[i].dstSet = descriptorSet;
		writeDescriptorSets[i].dstBinding = i;
		writeDescriptorSets[i].dstArrayElement = 0;
		writeDescriptorSets[i].descriptorCount = 1;
		writeDescriptorSets[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writeDescriptorSets[i].pImageInfo = &descriptorImageInfos[i];
	}

	renderer->vkUpdateDescriptorSets(
		renderer->logicalDevice,
		graphicsPipeline->fragmentSamplerBindingCount,
		writeDescriptorSets,
		0,
		NULL
	);

	graphicsPipeline->fragmentSamplerDescriptorSet = descriptorSet;

	SDL_stack_free(writeDescriptorSets);
	SDL_stack_free(descriptorImageInfos);
}

static void VULKAN_GetTextureData2D(
	REFRESH_Renderer *driverData,
	REFRESH_Texture *texture,
	uint32_t x,
	uint32_t y,
	uint32_t w,
	uint32_t h,
	uint32_t level,
	void* data,
	uint32_t dataLength
) {
    SDL_assert(0);
}

static void VULKAN_GetTextureDataCube(
	REFRESH_Renderer *driverData,
	REFRESH_Texture *texture,
	uint32_t x,
	uint32_t y,
	uint32_t w,
	uint32_t h,
	REFRESH_CubeMapFace cubeMapFace,
	uint32_t level,
	void* data,
	uint32_t dataLength
) {
    SDL_assert(0);
}

static void VULKAN_AddDisposeTexture(
	REFRESH_Renderer *driverData,
	REFRESH_Texture *texture
) {
    SDL_assert(0);
}

static void VULKAN_AddDisposeSampler(
	REFRESH_Renderer *driverData,
	REFRESH_Sampler *sampler
) {
    SDL_assert(0);
}

static void VULKAN_AddDisposeVertexBuffer(
	REFRESH_Renderer *driverData,
	REFRESH_Buffer *buffer
) {
    SDL_assert(0);
}

static void VULKAN_AddDisposeIndexBuffer(
	REFRESH_Renderer *driverData,
	REFRESH_Buffer *buffer
) {
    SDL_assert(0);
}

static void VULKAN_AddDisposeColorTarget(
	REFRESH_Renderer *driverData,
	REFRESH_ColorTarget *colorTarget
) {
    SDL_assert(0);
}

static void VULKAN_AddDisposeDepthStencilTarget(
	REFRESH_Renderer *driverData,
	REFRESH_DepthStencilTarget *depthStencilTarget
) {
    SDL_assert(0);
}

static void VULKAN_AddDisposeFramebuffer(
	REFRESH_Renderer *driverData,
	REFRESH_Framebuffer *frameBuffer
) {
    SDL_assert(0);
}

static void VULKAN_AddDisposeShaderModule(
	REFRESH_Renderer *driverData,
	REFRESH_ShaderModule *shaderModule
) {
    SDL_assert(0);
}

static void VULKAN_AddDisposeRenderPass(
	REFRESH_Renderer *driverData,
	REFRESH_RenderPass *renderPass
) {
    SDL_assert(0);
}

static void VULKAN_AddDisposeGraphicsPipeline(
	REFRESH_Renderer *driverData,
	REFRESH_GraphicsPipeline *graphicsPipeline
) {
    SDL_assert(0);
}

static void VULKAN_BeginRenderPass(
	REFRESH_Renderer *driverData,
	REFRESH_RenderPass *renderPass,
	REFRESH_Framebuffer *framebuffer,
	REFRESH_Rect renderArea,
	REFRESH_Color *pColorClearValues,
	uint32_t colorClearCount,
	REFRESH_DepthStencilValue *depthStencilClearValue
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;
	VkClearValue *clearValues;
	uint32_t i;
	uint32_t colorCount = colorClearCount;

	if (depthStencilClearValue != NULL)
	{
		colorCount += 1;
	}

	clearValues = SDL_stack_alloc(VkClearValue, colorCount);

	for (i = 0; i < colorClearCount; i += 1)
	{
		clearValues[i].color.uint32[0] = pColorClearValues[i].r;
		clearValues[i].color.uint32[1] = pColorClearValues[i].g;
		clearValues[i].color.uint32[2] = pColorClearValues[i].b;
		clearValues[i].color.uint32[3] = pColorClearValues[i].a;
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
	renderPassBeginInfo.framebuffer = (VkFramebuffer) framebuffer;
	renderPassBeginInfo.renderArea.extent.width = renderArea.w;
	renderPassBeginInfo.renderArea.extent.height = renderArea.h;
	renderPassBeginInfo.renderArea.offset.x = renderArea.x;
	renderPassBeginInfo.renderArea.offset.y = renderArea.y;
	renderPassBeginInfo.pClearValues = clearValues;
	renderPassBeginInfo.clearValueCount = colorCount;

	RECORD_CMD(renderer->vkCmdBeginRenderPass(
		renderer->currentCommandBuffer,
		&renderPassBeginInfo,
		VK_SUBPASS_CONTENTS_INLINE
	));

	SDL_stack_free(clearValues);
}

static void VULKAN_EndRenderPass(
	REFRESH_Renderer *driverData
) {
	VulkanRenderer *renderer = (VulkanRenderer*) driverData;

	RECORD_CMD(renderer->vkCmdEndRenderPass(
		renderer->currentCommandBuffer
	));
}

static void VULKAN_BindGraphicsPipeline(
	REFRESH_Renderer *driverData,
	REFRESH_GraphicsPipeline *graphicsPipeline
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanGraphicsPipeline* pipeline = (VulkanGraphicsPipeline*) graphicsPipeline;

	RECORD_CMD(renderer->vkCmdBindPipeline(
		renderer->currentCommandBuffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		pipeline->pipeline
	));
    SDL_assert(0);
}

static void VULKAN_INTERNAL_MarkAsBound(
	VulkanRenderer* renderer,
	VulkanBuffer* buf
) {
	VulkanSubBuffer* subbuf = buf->subBuffers[renderer->frameIndex];
	subbuf->bound = 1;

	/* Don't rebind a bound buffer */
	if (buf->bound) return;

	buf->bound = 1;

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
}

static void VULKAN_BindVertexBuffers(
	REFRESH_Renderer *driverData,
	uint32_t firstBinding,
	uint32_t bindingCount,
	REFRESH_Buffer **pBuffers,
	uint64_t *pOffsets
) {
	VkBuffer *buffers = SDL_stack_alloc(VkBuffer, bindingCount);
	VulkanBuffer* currentBuffer;
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	uint32_t i;

	for (i = 0; i < bindingCount; i += 1)
	{
		currentBuffer = (VulkanBuffer*) pBuffers[i];
		buffers[i] = currentBuffer->subBuffers[renderer->frameIndex]->buffer;
		VULKAN_INTERNAL_MarkAsBound(renderer, currentBuffer);
	}

	RECORD_CMD(renderer->vkCmdBindVertexBuffers(
		renderer->currentCommandBuffer,
		firstBinding,
		bindingCount,
		buffers,
		pOffsets
	));

	SDL_stack_free(buffers);
}

static void VULKAN_BindIndexBuffer(
	REFRESH_Renderer *driverData,
	REFRESH_Buffer *buffer,
	uint64_t offset,
	REFRESH_IndexElementSize indexElementSize
) {
	VulkanRenderer* renderer = (VulkanRenderer*) driverData;
	VulkanBuffer* vulkanBuffer = (VulkanBuffer*) buffer;

	VULKAN_INTERNAL_MarkAsBound(renderer, vulkanBuffer);

	RECORD_CMD(renderer->vkCmdBindIndexBuffer(
		renderer->currentCommandBuffer,
		vulkanBuffer->subBuffers[renderer->frameIndex]->buffer,
		offset,
		RefreshToVK_IndexType[indexElementSize]
	));
}

static void VULKAN_PreparePresent(
	REFRESH_Renderer* driverData,
	REFRESH_Texture* texture,
	REFRESH_Rect* sourceRectangle,
	REFRESH_Rect* destinationRectangle
) {
	VkResult acquireResult;
	uint32_t swapChainImageIndex;

	REFRESH_Rect srcRect;
	REFRESH_Rect dstRect;
	VkImageBlit blit;

	VulkanRenderer* renderer = (VulkanRenderer*)driverData;
	VulkanTexture* vulkanTexture = (VulkanTexture*)texture;

	if (renderer->headless)
	{
		REFRESH_LogError("Cannot call PreparePresent in headless mode!");
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

	if (sourceRectangle != NULL)
	{
		srcRect = *sourceRectangle;
	}
	else
	{
		srcRect.x = 0;
		srcRect.y = 0;
		srcRect.w = vulkanTexture->dimensions.width;
		srcRect.h = vulkanTexture->dimensions.height;
	}

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

	/* Blit the framebuffer! */

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_TRANSFER_READ,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		1,
		0,
		1,
		0,
		vulkanTexture->image,
		&vulkanTexture->resourceAccessType
	);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_TRANSFER_WRITE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		1,
		0,
		1,
		0,
		renderer->swapChainImages[swapChainImageIndex],
		&renderer->swapChainResourceAccessTypes[swapChainImageIndex]
	);

	blit.srcOffsets[0].x = srcRect.x;
	blit.srcOffsets[0].y = srcRect.y;
	blit.srcOffsets[0].z = 0;
	blit.srcOffsets[1].x = srcRect.x + srcRect.w;
	blit.srcOffsets[1].y = srcRect.y + srcRect.h;
	blit.srcOffsets[1].z = 1;

	blit.srcSubresource.mipLevel = 0;
	blit.srcSubresource.baseArrayLayer = 0;
	blit.srcSubresource.layerCount = 1;
	blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	blit.dstOffsets[0].x = dstRect.x;
	blit.dstOffsets[0].y = dstRect.y;
	blit.dstOffsets[0].z = 0;
	blit.dstOffsets[1].x = dstRect.x + dstRect.w;
	blit.dstOffsets[1].y = dstRect.y + dstRect.h;
	blit.dstOffsets[1].z = 1;

	blit.dstSubresource.mipLevel = 0;
	blit.dstSubresource.baseArrayLayer = 0;
	blit.dstSubresource.layerCount = 1;
	blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	RECORD_CMD(renderer->vkCmdBlitImage(
		renderer->currentCommandBuffer,
		vulkanTexture->image,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		renderer->swapChainImages[swapChainImageIndex],
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&blit,
		VK_FILTER_LINEAR
	));

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_PRESENT,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		1,
		0,
		1,
		0,
		renderer->swapChainImages[swapChainImageIndex],
		&renderer->swapChainResourceAccessTypes[swapChainImageIndex]
	);

	VULKAN_INTERNAL_ImageMemoryBarrier(
		renderer,
		RESOURCE_ACCESS_COLOR_ATTACHMENT_READ_WRITE,
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		1,
		0,
		1,
		0,
		vulkanTexture->image,
		&vulkanTexture->resourceAccessType
	);
}

static void VULKAN_Submit(
    REFRESH_Renderer *driverData
) {
	VulkanRenderer* renderer = (VulkanRenderer*)driverData;
	VkSubmitInfo submitInfo;
	VkResult vulkanResult, presentResult = VK_SUCCESS;
	uint32_t i;
	uint8_t present;

	VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkPresentInfoKHR presentInfo;

	present = !renderer->headless && renderer->shouldPresent;

	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.pNext = NULL;
	submitInfo.commandBufferCount = renderer->activeCommandBufferCount;
	submitInfo.pCommandBuffers = renderer->activeCommandBuffers;

	if (present)
	{
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &renderer->imageAvailableSemaphore;
		submitInfo.pWaitDstStageMask = &waitStages;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &renderer->renderFinishedSemaphore;
	}
	else
	{
		submitInfo.waitSemaphoreCount = 0;
		submitInfo.pWaitSemaphores = NULL;
		submitInfo.pWaitDstStageMask = NULL;
		submitInfo.signalSemaphoreCount = 0;
		submitInfo.pSignalSemaphores = NULL;
	}

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

	VULKAN_INTERNAL_PerformDeferredDestroys(renderer);

	renderer->frameIndex = (renderer->frameIndex + 1) % 2;

	/* Mark sub buffers of previously submitted buffers as unbound */
	for (i = 0; i < renderer->submittedBufferCount; i += 1)
	{
		if (renderer->submittedBuffers[i] != NULL)
		{
			renderer->submittedBuffers[i]->subBuffers[renderer->frameIndex]->bound = 0;
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

	/* Reset the previously submitted command buffers */
	for (i = 0; i < renderer->submittedCommandBufferCount; i += 1)
	{
		vulkanResult = renderer->vkResetCommandBuffer(
			renderer->submittedCommandBuffers[i],
			VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT
		);

		if (vulkanResult != VK_SUCCESS)
		{
			LogVulkanResult("vkResetCommandBuffer", vulkanResult);
		}
	}

	/* Mark the previously submitted command buffers as inactive */
	for (i = 0; i < renderer->submittedCommandBufferCount; i += 1)
	{
		renderer->inactiveCommandBuffers[renderer->inactiveCommandBufferCount] = renderer->submittedCommandBuffers[i];
		renderer->inactiveCommandBufferCount += 1;
	}

	renderer->submittedCommandBufferCount = 0;

	/* Prepare the command buffer fence for submission */
	renderer->vkResetFences(
		renderer->logicalDevice,
		1,
		&renderer->inFlightFence
	);

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

	/* Mark active command buffers as submitted */
	for (i = 0; i < renderer->activeCommandBufferCount; i += 1)
	{
		renderer->submittedCommandBuffers[renderer->submittedCommandBufferCount] = renderer->activeCommandBuffers[i];
		renderer->submittedCommandBufferCount += 1;
	}

	renderer->activeCommandBufferCount = 0;

	/* Reset UBOs */

	renderer->vertexUBOOffset = UBO_BUFFER_SIZE * renderer->frameIndex;
	renderer->vertexUBOBlockIncrement = 0;
	renderer->fragmentUBOOffset = UBO_BUFFER_SIZE * renderer->frameIndex;
	renderer->fragmentUBOBlockIncrement = 0;

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

		if (renderer->needNewSwapChain)
		{
			VULKAN_INTERNAL_RecreateSwapchain(renderer);
		}
	}

	renderer->swapChainImageAcquired = 0;
	renderer->shouldPresent = 0;

	VULKAN_INTERNAL_BeginCommandBuffer(renderer);
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
        REFRESH_LogError(
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
		REFRESH_LogError(
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
		REFRESH_LogError(
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
		REFRESH_LogWarn(
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
			REFRESH_LogWarn("Validation layers not found, continuing without validation");
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
		REFRESH_LogError(
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
	uint8_t querySuccess, foundSuitableDevice = 0;
	VkPhysicalDeviceProperties deviceProperties;

	queueFamilyIndices->graphicsFamily = UINT32_MAX;
	queueFamilyIndices->presentFamily = UINT32_MAX;
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
		if (	supportsPresent &&
			(queueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0	)
		{
			queueFamilyIndices->graphicsFamily = i;
			queueFamilyIndices->presentFamily = i;
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
		REFRESH_LogError(
			"vkEnumeratePhysicalDevices failed: %s",
			VkErrorMessages(vulkanResult)
		);
		return 0;
	}

	if (physicalDeviceCount == 0)
	{
		REFRESH_LogError("Failed to find any GPUs with Vulkan support");
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
		REFRESH_LogError(
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
		REFRESH_LogError("No suitable physical devices found");
		SDL_stack_free(physicalDevices);
		return 0;
	}

	renderer->physicalDevice = physicalDevice;
	renderer->queueFamilyIndices = queueFamilyIndices;

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

	VkDeviceQueueCreateInfo *queueCreateInfos = SDL_stack_alloc(
		VkDeviceQueueCreateInfo,
		2
	);
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

		queueCreateInfos[1] = queueCreateInfoPresent;
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
		REFRESH_LogError(
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

	SDL_stack_free(queueCreateInfos);
	return 1;
}

static REFRESH_Device* VULKAN_CreateDevice(
	REFRESH_PresentationParameters *presentationParameters,
    uint8_t debugMode
) {
    REFRESH_Device *result;
    VulkanRenderer *renderer;

    VkResult vulkanResult;
	uint32_t i;

    /* Variables: Create fence and semaphores */
	VkFenceCreateInfo fenceInfo;
	VkSemaphoreCreateInfo semaphoreInfo;

	/* Variables: Create command pool and command buffer */
	VkCommandPoolCreateInfo commandPoolCreateInfo;
	VkCommandBufferAllocateInfo commandBufferAllocateInfo;

	/* Variables: Shader param layouts */
	VkDescriptorSetLayoutCreateInfo setLayoutCreateInfo;
	VkDescriptorSetLayoutBinding vertexParamLayoutBinding;
	VkDescriptorSetLayoutBinding fragmentParamLayoutBinding;

	/* Variables: UBO Creation */
	VkDescriptorPoolCreateInfo uboDescriptorPoolInfo;
	VkDescriptorPoolSize uboPoolSize;
	VkDescriptorSetAllocateInfo vertexUBODescriptorAllocateInfo;
	VkDescriptorSetAllocateInfo fragmentUBODescriptorAllocateInfo;

    result = (REFRESH_Device*) SDL_malloc(sizeof(REFRESH_Device));
    ASSIGN_DRIVER(VULKAN)

    renderer = (VulkanRenderer*) SDL_malloc(sizeof(VulkanRenderer));
    result->driverData = (REFRESH_Renderer*) renderer;
    renderer->debugMode = debugMode;
    renderer->headless = presentationParameters->deviceWindowHandle == NULL;

    /* Create the VkInstance */
	if (!VULKAN_INTERNAL_CreateInstance(renderer, presentationParameters->deviceWindowHandle))
	{
		REFRESH_LogError("Error creating vulkan instance");
		return NULL;
	}

    renderer->deviceWindowHandle = presentationParameters->deviceWindowHandle;
	renderer->presentMode = presentationParameters->presentMode;

	/*
	 * Create the WSI vkSurface
	 */

	if (!SDL_Vulkan_CreateSurface(
		(SDL_Window*) renderer->deviceWindowHandle,
		renderer->instance,
		&renderer->surface
	)) {
		REFRESH_LogError(
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
		REFRESH_LogError("Failed to determine a suitable physical device");
		return NULL;
	}

	REFRESH_LogInfo("Refresh Driver: Vulkan");
	REFRESH_LogInfo(
		"Vulkan Device: %s",
		renderer->physicalDeviceProperties.properties.deviceName
	);
	REFRESH_LogInfo(
		"Vulkan Driver: %s %s",
		renderer->physicalDeviceDriverProperties.driverName,
		renderer->physicalDeviceDriverProperties.driverInfo
	);
	REFRESH_LogInfo(
		"Vulkan Conformance: %u.%u.%u",
		renderer->physicalDeviceDriverProperties.conformanceVersion.major,
		renderer->physicalDeviceDriverProperties.conformanceVersion.minor,
		renderer->physicalDeviceDriverProperties.conformanceVersion.patch
	);
	REFRESH_LogWarn(
		"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"
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
		REFRESH_LogError("Failed to create logical device");
		return NULL;
	}

	/*
	 * Create initial swapchain
	 */

    if (!renderer->headless)
    {
        if (VULKAN_INTERNAL_CreateSwapchain(renderer) != CREATE_SWAPCHAIN_SUCCESS)
        {
            REFRESH_LogError("Failed to create swap chain");
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
		&renderer->imageAvailableSemaphore
	);

	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateFence", vulkanResult);
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
		LogVulkanResult("vkCreateSemaphore", vulkanResult);
		return NULL;
	}

	/*
	 * Create command pool and buffers
	 */

	commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolCreateInfo.pNext = NULL;
	commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	commandPoolCreateInfo.queueFamilyIndex = renderer->queueFamilyIndices.graphicsFamily;
	vulkanResult = renderer->vkCreateCommandPool(
		renderer->logicalDevice,
		&commandPoolCreateInfo,
		NULL,
		&renderer->commandPool
	);
	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkCreateCommandPool", vulkanResult);
	}

	renderer->allocatedCommandBufferCount = 4;
	renderer->inactiveCommandBuffers = SDL_malloc(sizeof(VkCommandBuffer) * renderer->allocatedCommandBufferCount);
	renderer->activeCommandBuffers = SDL_malloc(sizeof(VkCommandBuffer) * renderer->allocatedCommandBufferCount);
	renderer->submittedCommandBuffers = SDL_malloc(sizeof(VkCommandBuffer) * renderer->allocatedCommandBufferCount);
	renderer->inactiveCommandBufferCount = renderer->allocatedCommandBufferCount;
	renderer->activeCommandBufferCount = 0;
	renderer->submittedCommandBufferCount = 0;

	commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commandBufferAllocateInfo.pNext = NULL;
	commandBufferAllocateInfo.commandPool = renderer->commandPool;
	commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandBufferAllocateInfo.commandBufferCount = renderer->allocatedCommandBufferCount;
	vulkanResult = renderer->vkAllocateCommandBuffers(
		renderer->logicalDevice,
		&commandBufferAllocateInfo,
		renderer->inactiveCommandBuffers
	);
	if (vulkanResult != VK_SUCCESS)
	{
		LogVulkanResult("vkAllocateCommandBuffers", vulkanResult);
	}

	renderer->currentCommandCount = 0;

	VULKAN_INTERNAL_BeginCommandBuffer(renderer);

	/* Set up UBO layouts */

	vertexParamLayoutBinding.binding = 0;
	vertexParamLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	vertexParamLayoutBinding.descriptorCount = 1;
	vertexParamLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	vertexParamLayoutBinding.pImmutableSamplers = NULL;

	setLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setLayoutCreateInfo.pNext = NULL;
	setLayoutCreateInfo.flags = 0;
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
		REFRESH_LogError("Failed to create vertex UBO layout!");
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
		REFRESH_LogError("Failed to create fragment UBO layout!");
		return NULL;
	}

	/* UBO Descriptors */

	uboPoolSize.descriptorCount = 2;
	uboPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;

	uboDescriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	uboDescriptorPoolInfo.pNext = NULL;
	uboDescriptorPoolInfo.flags = 0;
	uboDescriptorPoolInfo.maxSets = 2;
	uboDescriptorPoolInfo.poolSizeCount = 1;
	uboDescriptorPoolInfo.pPoolSizes = &uboPoolSize;

	renderer->vkCreateDescriptorPool(
		renderer->logicalDevice,
		&uboDescriptorPoolInfo,
		NULL,
		&renderer->UBODescriptorPool
	);

	vertexUBODescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	vertexUBODescriptorAllocateInfo.pNext = NULL;
	vertexUBODescriptorAllocateInfo.descriptorPool = renderer->UBODescriptorPool;
	vertexUBODescriptorAllocateInfo.descriptorSetCount = 1;
	vertexUBODescriptorAllocateInfo.pSetLayouts = &renderer->vertexParamLayout;

	renderer->vkAllocateDescriptorSets(
		renderer->logicalDevice,
		&vertexUBODescriptorAllocateInfo,
		&renderer->vertexUBODescriptorSet
	);

	fragmentUBODescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	fragmentUBODescriptorAllocateInfo.pNext = NULL;
	fragmentUBODescriptorAllocateInfo.descriptorPool = renderer->UBODescriptorPool;
	fragmentUBODescriptorAllocateInfo.descriptorSetCount = 1;
	fragmentUBODescriptorAllocateInfo.pSetLayouts = &renderer->fragmentParamLayout;

	renderer->vkAllocateDescriptorSets(
		renderer->logicalDevice,
		&fragmentUBODescriptorAllocateInfo,
		&renderer->fragmentUBODescriptorSet
	);

	/* UBO Data */

	renderer->vertexUBO = (VulkanBuffer*) SDL_malloc(sizeof(VulkanBuffer));

	if (!VULKAN_INTERNAL_CreateBuffer(
		renderer,
		UBO_ACTUAL_SIZE,
		RESOURCE_ACCESS_VERTEX_SHADER_READ_UNIFORM_BUFFER,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		SUB_BUFFER_COUNT,
		renderer->vertexUBO
	)) {
		REFRESH_LogError("Failed to create vertex UBO!");
		return NULL;
	}

	renderer->fragmentUBO = (VulkanBuffer*) SDL_malloc(sizeof(VulkanBuffer));

	if (!VULKAN_INTERNAL_CreateBuffer(
		renderer,
		UBO_ACTUAL_SIZE,
		RESOURCE_ACCESS_FRAGMENT_SHADER_READ_UNIFORM_BUFFER,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		SUB_BUFFER_COUNT,
		renderer->fragmentUBO
	)) {
		REFRESH_LogError("Failed to create fragment UBO!");
		return NULL;
	}

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

	/* Initialize buffer space */

	renderer->buffersInUseCapacity = 32;
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
		REFRESH_LogError("Failed to create texture staging buffer!");
		return NULL;
	}

	/* Descriptor Pools */

	renderer->descriptorPools = NULL;
	renderer->descriptorPoolCount = 0;

	/* Threading */

	renderer->allocatorLock = SDL_CreateMutex();
	renderer->commandLock = SDL_CreateMutex();

    return result;
}

REFRESH_Driver VulkanDriver = {
    "Vulkan",
    VULKAN_CreateDevice
};

#endif //REFRESH_DRIVER_VULKAN
