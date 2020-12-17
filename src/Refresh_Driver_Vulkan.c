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

/* Structures */

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

    #define VULKAN_INSTANCE_FUNCTION(ext, ret, func, params) \
		vkfntype_##func func;
	#define VULKAN_DEVICE_FUNCTION(ext, ret, func, params) \
		vkfntype_##func func;
	#include "Refresh_Driver_Vulkan_vkfuncs.h"
} VulkanRenderer;

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

/* Command Buffers */

/* Vulkan: Command Buffers */

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
	REFRESH_Renderer *renderer,
	REFRESH_ClearOptions options,
	REFRESH_Vec4 **colors,
    uint32_t colorCount,
	float depth,
	int32_t stencil
) {
    SDL_assert(0);
}

static void VULKAN_DrawIndexedPrimitives(
	REFRESH_Renderer *renderer,
	REFRESH_PrimitiveType primitiveType,
	uint32_t baseVertex,
	uint32_t minVertexIndex,
	uint32_t numVertices,
	uint32_t startIndex,
	uint32_t primitiveCount,
	REFRESH_Buffer *indices,
	REFRESH_IndexElementSize indexElementSize
) {
    SDL_assert(0);
}

static void VULKAN_DrawInstancedPrimitives(
	REFRESH_Renderer *renderer,
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
    SDL_assert(0);
}

static void VULKAN_DrawPrimitives(
	REFRESH_Renderer *renderer,
	REFRESH_PrimitiveType primitiveType,
	uint32_t vertexStart,
	uint32_t primitiveCount
) {
    SDL_assert(0);
}

static REFRESH_RenderPass* VULKAN_CreateRenderPass(
	REFRESH_Renderer *renderer,
	REFRESH_RenderPassCreateInfo *renderPassCreateInfo
) {
    SDL_assert(0);
}

static REFRESH_GraphicsPipeline* VULKAN_CreateGraphicsPipeline(
	REFRESH_Renderer *renderer,
	REFRESH_GraphicsPipelineCreateInfo *pipelineCreateInfo
) {
    SDL_assert(0);
}

static REFRESH_Sampler* VULKAN_CreateSampler(
	REFRESH_Renderer *renderer,
	REFRESH_SamplerStateCreateInfo *samplerStateCreateInfo
) {
    SDL_assert(0);
}

static REFRESH_Framebuffer* VULKAN_CreateFramebuffer(
	REFRESH_Renderer *renderer,
	REFRESH_FramebufferCreateInfo *framebufferCreateInfo
) {
    SDL_assert(0);
}

static REFRESH_ShaderModule* VULKAN_CreateShaderModule(
	REFRESH_Renderer *renderer,
	REFRESH_ShaderModuleCreateInfo *shaderModuleCreateInfo
) {
    SDL_assert(0);
}

static REFRESH_Texture* VULKAN_CreateTexture2D(
	REFRESH_Renderer *renderer,
	REFRESH_SurfaceFormat format,
	uint32_t width,
	uint32_t height,
	uint32_t levelCount
) {
    SDL_assert(0);
}

static REFRESH_Texture* VULKAN_CreateTexture3D(
	REFRESH_Renderer *renderer,
	REFRESH_SurfaceFormat format,
	uint32_t width,
	uint32_t height,
	uint32_t depth,
	uint32_t levelCount
) {
    SDL_assert(0);
}

static REFRESH_Texture* VULKAN_CreateTextureCube(
	REFRESH_Renderer *renderer,
	REFRESH_SurfaceFormat format,
	uint32_t size,
	uint32_t levelCount
) {
    SDL_assert(0);
}

static REFRESH_ColorTarget* VULKAN_GenColorTarget(
	REFRESH_Renderer *renderer,
	uint32_t width,
	uint32_t height,
	REFRESH_SurfaceFormat format,
	uint32_t multisampleCount,
	REFRESH_Texture *texture
) {
    SDL_assert(0);
}

static REFRESH_DepthStencilTarget* VULKAN_GenDepthStencilTarget(
	REFRESH_Renderer *renderer,
	uint32_t width,
	uint32_t height,
	REFRESH_DepthFormat format,
	REFRESH_Texture *texture
) {
    SDL_assert(0);
}

static REFRESH_Buffer* VULKAN_GenVertexBuffer(
	REFRESH_Renderer *renderer,
	uint32_t sizeInBytes
) {
    SDL_assert(0);
}

static REFRESH_Buffer* VULKAN_GenIndexBuffer(
	REFRESH_Renderer *renderer,
	uint32_t sizeInBytes
) {
    SDL_assert(0);
}

static REFRESH_Buffer* VULKAN_GenShaderParamBuffer(
	REFRESH_Renderer *renderer,
	uint32_t sizeInBytes
) {
    SDL_assert(0);
}

static void VULKAN_SetTextureData2D(
	REFRESH_Renderer *renderer,
	REFRESH_Texture *texture,
	uint32_t x,
	uint32_t y,
	uint32_t w,
	uint32_t h,
	uint32_t level,
	void *data,
	uint32_t dataLengthInBytes
) {
    SDL_assert(0);
}

static void VULKAN_SetTextureData3D(
	REFRESH_Renderer *renderer,
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
    SDL_assert(0);
}

static void VULKAN_SetTextureDataCube(
	REFRESH_Renderer *renderer,
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

static void VULKAN_SetTextureDataYUV(
	REFRESH_Renderer *renderer,
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
    SDL_assert(0);
}

static void VULKAN_SetVertexBufferData(
	REFRESH_Renderer *renderer,
	REFRESH_Buffer *buffer,
	uint32_t offsetInBytes,
	void* data,
	uint32_t elementCount,
	uint32_t elementSizeInBytes
) {
    SDL_assert(0);
}

static void VULKAN_SetIndexBufferData(
	REFRESH_Renderer *renderer,
	REFRESH_Buffer *buffer,
	uint32_t offsetInBytes,
	void* data,
	uint32_t dataLength
) {
    SDL_assert(0);
}

static void VULKAN_SetShaderParamData(
	REFRESH_Renderer *renderer,
	REFRESH_Buffer *shaderParamBuffer,
	uint32_t offsetInBytes,
	void *data,
	uint32_t elementCount,
	uint32_t elementSizeInBytes
) {
    SDL_assert(0);
}

static void VULKAN_SetVertexSamplers(
	REFRESH_Renderer *renderer,
	uint32_t startIndex,
	REFRESH_Texture *pTextures,
	REFRESH_Sampler *pSamplers,
	uint32_t count
) {
    SDL_assert(0);
}

static void VULKAN_SetFragmentSamplers(
	REFRESH_Renderer *renderer,
	uint32_t startIndex,
	REFRESH_Texture *pTextures,
	REFRESH_Sampler *pSamplers,
	uint32_t count
) {
    SDL_assert(0);
}

static void VULKAN_GetTextureData2D(
	REFRESH_Renderer *renderer,
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
	REFRESH_Renderer *renderer,
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
	REFRESH_Renderer *renderer,
	REFRESH_Texture *texture
) {
    SDL_assert(0);
}

static void VULKAN_AddDisposeSampler(
	REFRESH_Renderer *renderer,
	REFRESH_Sampler *sampler
) {
    SDL_assert(0);
}

static void VULKAN_AddDisposeVertexBuffer(
	REFRESH_Renderer *renderer,
	REFRESH_Buffer *buffer
) {
    SDL_assert(0);
}

static void VULKAN_AddDisposeIndexBuffer(
	REFRESH_Renderer *renderer,
	REFRESH_Buffer *buffer
) {
    SDL_assert(0);
}

static void VULKAN_AddDisposeShaderParamBuffer(
	REFRESH_Renderer *renderer,
	REFRESH_Buffer *buffer
) {
    SDL_assert(0);
}

static void VULKAN_AddDisposeColorTarget(
	REFRESH_Renderer *renderer,
	REFRESH_ColorTarget *colorTarget
) {
    SDL_assert(0);
}

static void VULKAN_AddDisposeDepthStencilTarget(
	REFRESH_Renderer *renderer,
	REFRESH_DepthStencilTarget *depthStencilTarget
) {
    SDL_assert(0);
}

static void VULKAN_AddDisposeFramebuffer(
	REFRESH_Renderer *renderer,
	REFRESH_Framebuffer *frameBuffer
) {
    SDL_assert(0);
}

static void VULKAN_AddDisposeShaderModule(
	REFRESH_Renderer *renderer,
	REFRESH_ShaderModule *shaderModule
) {
    SDL_assert(0);
}

static void VULKAN_AddDisposeRenderPass(
	REFRESH_Renderer *renderer,
	REFRESH_RenderPass *renderPass
) {
    SDL_assert(0);
}

static void VULKAN_AddDisposeGraphicsPipeline(
	REFRESH_Renderer *renderer,
	REFRESH_GraphicsPipeline *graphicsPipeline
) {
    SDL_assert(0);
}

static void VULKAN_BeginRenderPass(
	REFRESH_Renderer *renderer,
	REFRESH_RenderPass *renderPass,
	REFRESH_Framebuffer *framebuffer,
	REFRESH_Rect renderArea,
	REFRESH_ClearValue *pClearValues,
	uint32_t clearCount
) {
    SDL_assert(0);
}

static void VULKAN_EndRenderPass(
	REFRESH_Renderer *renderer
) {
    SDL_assert(0);
}

static void VULKAN_BindGraphicsPipeline(
	REFRESH_Renderer *renderer,
	REFRESH_GraphicsPipeline *graphicsPipeline
) {
    SDL_assert(0);
}

static void VULKAN_Present(
    REFRESH_Renderer *renderer,
    REFRESH_Rect *sourceRectangle,
    REFRESH_Rect *destinationRectangle
) {
    SDL_assert(0);
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
	uint8_t layerFound;

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
    void *deviceWindowHandle,
    uint8_t debugMode
) {
    REFRESH_Device *result;
    VulkanRenderer *renderer;

    VkResult vulkanResult;

    /* Variables: Create fence and semaphores */
	VkFenceCreateInfo fenceInfo;
	VkSemaphoreCreateInfo semaphoreInfo;

	/* Variables: Create command pool and command buffer */
	VkCommandPoolCreateInfo commandPoolCreateInfo;
	VkCommandBufferAllocateInfo commandBufferAllocateInfo;

    result = (REFRESH_Device*) SDL_malloc(sizeof(REFRESH_Device));
    ASSIGN_DRIVER(VULKAN)

    renderer = (VulkanRenderer*) SDL_malloc(sizeof(VulkanRenderer));
    result->driverData = (REFRESH_Renderer*) renderer;
    renderer->debugMode = debugMode;
    renderer->headless = deviceWindowHandle == NULL;

    /* Create the VkInstance */
	if (!VULKAN_INTERNAL_CreateInstance(renderer, deviceWindowHandle))
	{
		REFRESH_LogError("Error creating vulkan instance");
		return NULL;
	}

    renderer->deviceWindowHandle = deviceWindowHandle;

	/*
	 * Create the WSI vkSurface
	 */

	if (!SDL_Vulkan_CreateSurface(
		(SDL_Window*) deviceWindowHandle,
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

    return result;
}

REFRESH_Driver VulkanDriver = {
    "Vulkan",
    VULKAN_CreateDevice
};

#endif //REFRESH_DRIVER_VULKAN
