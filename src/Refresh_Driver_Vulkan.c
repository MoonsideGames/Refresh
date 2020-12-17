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

typedef struct VulkanRenderer
{
    FNA3D_Device *device;
} VulkanRenderer;

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

static REFRESH_Device* VULKAN_CreateDevice(
    FNA3D_Device *fnaDevice
) {
    REFRESH_Device *result;
    VulkanRenderer *renderer;

    result = (REFRESH_Device*) SDL_malloc(sizeof(REFRESH_Device));
    ASSIGN_DRIVER(VULKAN)

    renderer = (VulkanRenderer*) SDL_malloc(sizeof(VulkanRenderer));

    return result;
}

REFRESH_Driver VulkanDriver = {
    "Vulkan",
    VULKAN_CreateDevice
};

#endif //REFRESH_DRIVER_VULKAN
