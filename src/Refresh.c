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

#include "Refresh_Driver.h"

#include <SDL.h>

#define NULL_RETURN(name) if (name == NULL) { return; }
#define NULL_RETURN_NULL(name) if (name == NULL) { return NULL; }

/* Drivers */

static const REFRESH_Driver *drivers[] = {
    &VulkanDriver,
    NULL
};

/* Logging */

static void REFRESH_Default_LogInfo(const char *msg)
{
	SDL_LogInfo(
		SDL_LOG_CATEGORY_APPLICATION,
		"%s",
		msg
	);
}

static void REFRESH_Default_LogWarn(const char *msg)
{
	SDL_LogWarn(
		SDL_LOG_CATEGORY_APPLICATION,
		"%s",
		msg
	);
}

static void REFRESH_Default_LogError(const char *msg)
{
	SDL_LogError(
		SDL_LOG_CATEGORY_APPLICATION,
		"%s",
		msg
	);
}

static REFRESH_LogFunc REFRESH_LogInfoFunc = REFRESH_Default_LogInfo;
static REFRESH_LogFunc REFRESH_LogWarnFunc = REFRESH_Default_LogWarn;
static REFRESH_LogFunc REFRESH_LogErrorFunc = REFRESH_Default_LogError;

#define MAX_MESSAGE_SIZE 1024

void REFRESH_LogInfo(const char *fmt, ...)
{
	char msg[MAX_MESSAGE_SIZE];
	va_list ap;
	va_start(ap, fmt);
	SDL_vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	REFRESH_LogInfoFunc(msg);
}

void REFRESH_LogWarn(const char *fmt, ...)
{
	char msg[MAX_MESSAGE_SIZE];
	va_list ap;
	va_start(ap, fmt);
	SDL_vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	REFRESH_LogWarnFunc(msg);
}

void REFRESH_LogError(const char *fmt, ...)
{
	char msg[MAX_MESSAGE_SIZE];
	va_list ap;
	va_start(ap, fmt);
	SDL_vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	REFRESH_LogErrorFunc(msg);
}

#undef MAX_MESSAGE_SIZE

void REFRESH_HookLogFunctions(
	REFRESH_LogFunc info,
	REFRESH_LogFunc warn,
	REFRESH_LogFunc error
) {
	REFRESH_LogInfoFunc = info;
	REFRESH_LogWarnFunc = warn;
	REFRESH_LogErrorFunc = error;
}

/* Version API */

uint32_t REFRESH_LinkedVersion(void)
{
	return REFRESH_COMPILED_VERSION;
}

/* Driver Functions */

static int32_t selectedDriver = 0;

REFRESH_Device* REFRESH_CreateDevice(
    REFRESH_PresentationParameters *presentationParameters,
    uint8_t debugMode
) {
    if (selectedDriver < 0)
    {
        return NULL;
    }

    return drivers[selectedDriver]->CreateDevice(
        presentationParameters,
        debugMode
    );
}

void REFRESH_DestroyDevice(REFRESH_Device *device)
{
    NULL_RETURN(device);
    device->DestroyDevice(device);
}

void REFRESH_Clear(
    REFRESH_Device *device,
	REFRESH_CommandBuffer *commandBuffer,
	REFRESH_Rect *clearRect,
	REFRESH_ClearOptions options,
	REFRESH_Color *colors,
	uint32_t colorCount,
	float depth,
	int32_t stencil
) {
    NULL_RETURN(device);
    device->Clear(
        device->driverData,
        commandBuffer,
        clearRect,
        options,
        colors,
        colorCount,
        depth,
        stencil
    );
}

void REFRESH_DrawIndexedPrimitives(
	REFRESH_Device *device,
    REFRESH_CommandBuffer *commandBuffer,
	uint32_t baseVertex,
	uint32_t minVertexIndex,
	uint32_t numVertices,
	uint32_t startIndex,
	uint32_t primitiveCount,
	REFRESH_Buffer *indices,
	REFRESH_IndexElementSize indexElementSize,
    uint32_t vertexParamOffset,
	uint32_t fragmentParamOffset
) {
    NULL_RETURN(device);
    device->DrawIndexedPrimitives(
        device->driverData,
        commandBuffer,
        baseVertex,
        minVertexIndex,
        numVertices,
        startIndex,
        primitiveCount,
        indices,
        indexElementSize,
        vertexParamOffset,
        fragmentParamOffset
    );
}

void REFRESH_DrawInstancedPrimitives(
	REFRESH_Device *device,
    REFRESH_CommandBuffer *commandBuffer,
	uint32_t baseVertex,
	uint32_t minVertexIndex,
	uint32_t numVertices,
	uint32_t startIndex,
	uint32_t primitiveCount,
	uint32_t instanceCount,
	REFRESH_Buffer *indices,
	REFRESH_IndexElementSize indexElementSize,
    uint32_t vertexParamOffset,
	uint32_t fragmentParamOffset
) {
    NULL_RETURN(device);
    device->DrawInstancedPrimitives(
        device->driverData,
        commandBuffer,
        baseVertex,
        minVertexIndex,
        numVertices,
        startIndex,
        primitiveCount,
        instanceCount,
        indices,
        indexElementSize,
        vertexParamOffset,
        fragmentParamOffset
    );
}

void REFRESH_DrawPrimitives(
	REFRESH_Device *device,
    REFRESH_CommandBuffer *commandBuffer,
	uint32_t vertexStart,
	uint32_t primitiveCount,
    uint32_t vertexParamOffset,
	uint32_t fragmentParamOffset
) {
    NULL_RETURN(device);
    device->DrawPrimitives(
        device->driverData,
        commandBuffer,
        vertexStart,
        primitiveCount,
        vertexParamOffset,
        fragmentParamOffset
    );
}

void REFRESH_DispatchCompute(
    REFRESH_Device *device,
    REFRESH_CommandBuffer *commandBuffer,
    uint32_t groupCountX,
    uint32_t groupCountY,
    uint32_t groupCountZ,
    uint32_t computeParamOffset
) {
    NULL_RETURN(device);
    device->DispatchCompute(
        device->driverData,
        commandBuffer,
        groupCountX,
        groupCountY,
        groupCountZ,
        computeParamOffset
    );
}

REFRESH_RenderPass* REFRESH_CreateRenderPass(
	REFRESH_Device *device,
	REFRESH_RenderPassCreateInfo *renderPassCreateInfo
) {
    NULL_RETURN_NULL(device);
    return device->CreateRenderPass(
        device->driverData,
        renderPassCreateInfo
    );
}

REFRESH_ComputePipeline* REFRESH_CreateComputePipeline(
    REFRESH_Device *device,
    REFRESH_ComputePipelineCreateInfo *pipelineCreateInfo
) {
    NULL_RETURN_NULL(device);
    return device->CreateComputePipeline(
        device->driverData,
        pipelineCreateInfo
    );
}

REFRESH_GraphicsPipeline* REFRESH_CreateGraphicsPipeline(
	REFRESH_Device *device,
	REFRESH_GraphicsPipelineCreateInfo *pipelineCreateInfo
) {
    NULL_RETURN_NULL(device);
    return device->CreateGraphicsPipeline(
        device->driverData,
        pipelineCreateInfo
    );
}

REFRESH_Sampler* REFRESH_CreateSampler(
	REFRESH_Device *device,
	REFRESH_SamplerStateCreateInfo *samplerStateCreateInfo
) {
    NULL_RETURN_NULL(device);
    return device->CreateSampler(
        device->driverData,
        samplerStateCreateInfo
    );
}

REFRESH_Framebuffer* REFRESH_CreateFramebuffer(
	REFRESH_Device *device,
	REFRESH_FramebufferCreateInfo *framebufferCreateInfo
) {
    NULL_RETURN_NULL(device);
    return device->CreateFramebuffer(
        device->driverData,
        framebufferCreateInfo
    );
}

REFRESH_ShaderModule* REFRESH_CreateShaderModule(
	REFRESH_Device *device,
	REFRESH_ShaderModuleCreateInfo *shaderModuleCreateInfo
) {
    NULL_RETURN_NULL(device);
    return device->CreateShaderModule(
        device->driverData,
        shaderModuleCreateInfo
    );
}

REFRESH_Texture* REFRESH_CreateTexture2D(
	REFRESH_Device *device,
	REFRESH_SurfaceFormat format,
	uint32_t width,
	uint32_t height,
	uint32_t levelCount,
    REFRESH_TextureUsageFlags usageFlags
) {
    NULL_RETURN_NULL(device);
    return device->CreateTexture2D(
        device->driverData,
        format,
        width,
        height,
        levelCount,
        usageFlags
    );
}

REFRESH_Texture* REFRESH_CreateTexture3D(
	REFRESH_Device *device,
	REFRESH_SurfaceFormat format,
	uint32_t width,
	uint32_t height,
	uint32_t depth,
	uint32_t levelCount,
    REFRESH_TextureUsageFlags usageFlags
) {
    NULL_RETURN_NULL(device);
    return device->CreateTexture3D(
        device->driverData,
        format,
        width,
        height,
        depth,
        levelCount,
        usageFlags
    );
}

REFRESH_Texture* REFRESH_CreateTextureCube(
	REFRESH_Device *device,
	REFRESH_SurfaceFormat format,
	uint32_t size,
	uint32_t levelCount,
    REFRESH_TextureUsageFlags usageFlags
) {
    NULL_RETURN_NULL(device);
    return device->CreateTextureCube(
        device->driverData,
        format,
        size,
        levelCount,
        usageFlags
    );
}

REFRESH_ColorTarget* REFRESH_CreateColorTarget(
	REFRESH_Device *device,
    REFRESH_SampleCount multisampleCount,
	REFRESH_TextureSlice *textureSlice
) {
    NULL_RETURN_NULL(device);
    return device->CreateColorTarget(
        device->driverData,
        multisampleCount,
        textureSlice
    );
}

REFRESH_DepthStencilTarget* REFRESH_CreateDepthStencilTarget(
	REFRESH_Device *device,
	uint32_t width,
	uint32_t height,
	REFRESH_DepthFormat format
) {
    NULL_RETURN_NULL(device);
    return device->CreateDepthStencilTarget(
        device->driverData,
        width,
        height,
        format
    );
}

REFRESH_Buffer* REFRESH_CreateBuffer(
	REFRESH_Device *device,
    REFRESH_BufferUsageFlags usageFlags,
	uint32_t sizeInBytes
) {
    NULL_RETURN_NULL(device);
    return device->CreateBuffer(
        device->driverData,
        usageFlags,
        sizeInBytes
    );
}

void REFRESH_SetTextureData(
	REFRESH_Device *device,
	REFRESH_TextureSlice *textureSlice,
	void *data,
	uint32_t dataLengthInBytes
) {
    NULL_RETURN(device);
    device->SetTextureData(
        device->driverData,
        textureSlice,
        data,
        dataLengthInBytes
    );
}

void REFRESH_SetTextureDataYUV(
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
) {
    NULL_RETURN(device);
    device->SetTextureDataYUV(
        device->driverData,
        y,
        u,
        v,
        yWidth,
        yHeight,
        uvWidth,
        uvHeight,
        data,
        dataLength
    );
}

void REFRESH_CopyTextureToTexture(
    REFRESH_Device *device,
	REFRESH_CommandBuffer *commandBuffer,
	REFRESH_TextureSlice *sourceTextureSlice,
	REFRESH_TextureSlice *destinationTextureSlice,
	REFRESH_Filter filter
) {
    NULL_RETURN(device);
    device->CopyTextureToTexture(
        device->driverData,
        commandBuffer,
        sourceTextureSlice,
        destinationTextureSlice,
        filter
    );
}

void REFRESH_CopyTextureToBuffer(
	REFRESH_Device *device,
	REFRESH_CommandBuffer *commandBuffer,
	REFRESH_TextureSlice *textureSlice,
	REFRESH_Buffer *buffer
) {
    NULL_RETURN(device);
    device->CopyTextureToBuffer(
        device->driverData,
        commandBuffer,
        textureSlice,
        buffer
    );
}

void REFRESH_SetBufferData(
	REFRESH_Device *device,
	REFRESH_Buffer *buffer,
	uint32_t offsetInBytes,
	void* data,
	uint32_t dataLength
) {
    NULL_RETURN(device);
    device->SetBufferData(
        device->driverData,
        buffer,
        offsetInBytes,
        data,
        dataLength
    );
}

uint32_t REFRESH_PushVertexShaderParams(
	REFRESH_Device *device,
    REFRESH_CommandBuffer *commandBuffer,
	void *data,
	uint32_t elementCount
) {
    if (device == NULL) { return 0; }
    return device->PushVertexShaderParams(
        device->driverData,
        commandBuffer,
        data,
        elementCount
    );
}

uint32_t REFRESH_PushFragmentShaderParams(
	REFRESH_Device *device,
    REFRESH_CommandBuffer *commandBuffer,
	void *data,
	uint32_t elementCount
) {
    if (device == NULL) { return 0; }
    return device->PushFragmentShaderParams(
        device->driverData,
        commandBuffer,
        data,
        elementCount
    );
}

uint32_t REFRESH_PushComputeShaderParams(
    REFRESH_Device *device,
    REFRESH_CommandBuffer *commandBuffer,
    void *data,
    uint32_t elementCount
) {
    if (device == NULL) { return 0; }
    return device->PushComputeShaderParams(
        device->driverData,
        commandBuffer,
        data,
        elementCount
    );
}

void REFRESH_SetVertexSamplers(
	REFRESH_Device *device,
    REFRESH_CommandBuffer *commandBuffer,
	REFRESH_Texture **pTextures,
	REFRESH_Sampler **pSamplers
) {
    NULL_RETURN(device);
    device->SetVertexSamplers(
        device->driverData,
        commandBuffer,
        pTextures,
        pSamplers
    );
}

void REFRESH_SetFragmentSamplers(
	REFRESH_Device *device,
    REFRESH_CommandBuffer *commandBuffer,
	REFRESH_Texture **pTextures,
	REFRESH_Sampler **pSamplers
) {
    NULL_RETURN(device);
    device->SetFragmentSamplers(
        device->driverData,
        commandBuffer,
        pTextures,
        pSamplers
    );
}

void REFRESH_GetBufferData(
    REFRESH_Device *device,
    REFRESH_Buffer *buffer,
    void *data,
    uint32_t dataLengthInBytes
) {
    NULL_RETURN(device);
    device->GetBufferData(
        device->driverData,
        buffer,
        data,
        dataLengthInBytes
    );
}

void REFRESH_AddDisposeTexture(
	REFRESH_Device *device,
	REFRESH_Texture *texture
) {
    NULL_RETURN(device);
    device->AddDisposeTexture(
        device->driverData,
        texture
    );
}

void REFRESH_AddDisposeSampler(
	REFRESH_Device *device,
	REFRESH_Sampler *sampler
) {
    NULL_RETURN(device);
    device->AddDisposeSampler(
        device->driverData,
        sampler
    );
}

void REFRESH_AddDisposeBuffer(
	REFRESH_Device *device,
	REFRESH_Buffer *buffer
) {
    NULL_RETURN(device);
    device->AddDisposeBuffer(
        device->driverData,
        buffer
    );
}

void REFRESH_AddDisposeColorTarget(
	REFRESH_Device *device,
	REFRESH_ColorTarget *colorTarget
) {
    NULL_RETURN(device);
    device->AddDisposeColorTarget(
        device->driverData,
        colorTarget
    );
}

void REFRESH_AddDisposeDepthStencilTarget(
	REFRESH_Device *device,
	REFRESH_DepthStencilTarget *depthStencilTarget
) {
    NULL_RETURN(device);
    device->AddDisposeDepthStencilTarget(
        device->driverData,
        depthStencilTarget
    );
}

void REFRESH_AddDisposeFramebuffer(
	REFRESH_Device *device,
	REFRESH_Framebuffer *frameBuffer
) {
    NULL_RETURN(device);
    device->AddDisposeFramebuffer(
        device->driverData,
        frameBuffer
    );
}

void REFRESH_AddDisposeShaderModule(
	REFRESH_Device *device,
	REFRESH_ShaderModule *shaderModule
) {
    NULL_RETURN(device);
    device->AddDisposeShaderModule(
        device->driverData,
        shaderModule
    );
}

void REFRESH_AddDisposeRenderPass(
	REFRESH_Device *device,
	REFRESH_RenderPass *renderPass
) {
    NULL_RETURN(device);
    device->AddDisposeRenderPass(
        device->driverData,
        renderPass
    );
}

void REFRESH_AddDisposeComputePipeline(
	REFRESH_Device *device,
	REFRESH_ComputePipeline *computePipeline
) {
    NULL_RETURN(device);
    device->AddDisposeComputePipeline(
        device->driverData,
        computePipeline
    );
}

void REFRESH_AddDisposeGraphicsPipeline(
	REFRESH_Device *device,
	REFRESH_GraphicsPipeline *graphicsPipeline
) {
    NULL_RETURN(device);
    device->AddDisposeGraphicsPipeline(
        device->driverData,
        graphicsPipeline
    );
}

void REFRESH_BeginRenderPass(
	REFRESH_Device *device,
    REFRESH_CommandBuffer *commandBuffer,
	REFRESH_RenderPass *renderPass,
	REFRESH_Framebuffer *framebuffer,
	REFRESH_Rect renderArea,
	REFRESH_Color *pColorClearValues,
	uint32_t colorClearCount,
	REFRESH_DepthStencilValue *depthStencilClearValue
) {
    NULL_RETURN(device);
    device->BeginRenderPass(
        device->driverData,
        commandBuffer,
        renderPass,
        framebuffer,
        renderArea,
        pColorClearValues,
        colorClearCount,
        depthStencilClearValue
    );
}

void REFRESH_EndRenderPass(
	REFRESH_Device *device,
    REFRESH_CommandBuffer *commandBuffer
) {
    NULL_RETURN(device);
    device->EndRenderPass(
        device->driverData,
        commandBuffer
    );
}

void REFRESH_BindGraphicsPipeline(
	REFRESH_Device *device,
    REFRESH_CommandBuffer *commandBuffer,
	REFRESH_GraphicsPipeline *graphicsPipeline
) {
    NULL_RETURN(device);
    device->BindGraphicsPipeline(
        device->driverData,
        commandBuffer,
        graphicsPipeline
    );
}

void REFRESH_BindVertexBuffers(
	REFRESH_Device *device,
    REFRESH_CommandBuffer *commandBuffer,
	uint32_t firstBinding,
	uint32_t bindingCount,
	REFRESH_Buffer **pBuffers,
	uint64_t *pOffsets
) {
    NULL_RETURN(device);
    device->BindVertexBuffers(
        device->driverData,
        commandBuffer,
        firstBinding,
        bindingCount,
        pBuffers,
        pOffsets
    );
}

void REFRESH_BindIndexBuffer(
    REFRESH_Device *device,
    REFRESH_CommandBuffer *commandBuffer,
    REFRESH_Buffer *buffer,
	uint64_t offset,
	REFRESH_IndexElementSize indexElementSize
) {
    NULL_RETURN(device);
    device->BindIndexBuffer(
        device->driverData,
        commandBuffer,
        buffer,
        offset,
        indexElementSize
    );
}

void REFRESH_BindComputePipeline(
    REFRESH_Device *device,
    REFRESH_CommandBuffer *commandBuffer,
    REFRESH_ComputePipeline *computePipeline
) {
    NULL_RETURN(device);
    device->BindComputePipeline(
        device->driverData,
        commandBuffer,
        computePipeline
    );
}

void REFRESH_BindComputeBuffers(
    REFRESH_Device *device,
    REFRESH_CommandBuffer *commandBuffer,
    REFRESH_Buffer **pBuffers
) {
    NULL_RETURN(device);
    device->BindComputeBuffers(
        device->driverData,
        commandBuffer,
        pBuffers
    );
}

void REFRESH_BindComputeTextures(
    REFRESH_Device *device,
    REFRESH_CommandBuffer *commandBuffer,
    REFRESH_Texture **pTextures
) {
    NULL_RETURN(device);
    device->BindComputeTextures(
        device->driverData,
        commandBuffer,
        pTextures
    );
}

REFRESH_CommandBuffer* REFRESH_AcquireCommandBuffer(
    REFRESH_Device *device,
    uint8_t fixed
) {
    NULL_RETURN_NULL(device);
    return device->AcquireCommandBuffer(
        device->driverData,
        fixed
    );
}

void REFRESH_QueuePresent(
    REFRESH_Device *device,
    REFRESH_CommandBuffer *commandBuffer,
    REFRESH_TextureSlice* textureSlice,
    REFRESH_Rect *destinationRectangle,
    REFRESH_Filter filter
) {
    NULL_RETURN(device);
    device->QueuePresent(
        device->driverData,
        commandBuffer,
        textureSlice,
        destinationRectangle,
        filter
    );
}

void REFRESH_Submit(
    REFRESH_Device *device,
    REFRESH_CommandBuffer **pCommandBuffers,
    uint32_t commandBufferCount
) {
    NULL_RETURN(device);
    device->Submit(
        device->driverData,
        pCommandBuffers,
        commandBufferCount
    );
}

void REFRESH_Wait(
    REFRESH_Device *device
) {
    NULL_RETURN(device);
    device->Wait(
        device->driverData
    );
}

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
