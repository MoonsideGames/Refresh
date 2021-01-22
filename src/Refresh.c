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

static const Refresh_Driver *drivers[] = {
    &VulkanDriver,
    NULL
};

/* Logging */

static void Refresh_Default_LogInfo(const char *msg)
{
	SDL_LogInfo(
		SDL_LOG_CATEGORY_APPLICATION,
		"%s",
		msg
	);
}

static void Refresh_Default_LogWarn(const char *msg)
{
	SDL_LogWarn(
		SDL_LOG_CATEGORY_APPLICATION,
		"%s",
		msg
	);
}

static void Refresh_Default_LogError(const char *msg)
{
	SDL_LogError(
		SDL_LOG_CATEGORY_APPLICATION,
		"%s",
		msg
	);
}

static Refresh_LogFunc Refresh_LogInfoFunc = Refresh_Default_LogInfo;
static Refresh_LogFunc Refresh_LogWarnFunc = Refresh_Default_LogWarn;
static Refresh_LogFunc Refresh_LogErrorFunc = Refresh_Default_LogError;

#define MAX_MESSAGE_SIZE 1024

void Refresh_LogInfo(const char *fmt, ...)
{
	char msg[MAX_MESSAGE_SIZE];
	va_list ap;
	va_start(ap, fmt);
	SDL_vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	Refresh_LogInfoFunc(msg);
}

void Refresh_LogWarn(const char *fmt, ...)
{
	char msg[MAX_MESSAGE_SIZE];
	va_list ap;
	va_start(ap, fmt);
	SDL_vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	Refresh_LogWarnFunc(msg);
}

void Refresh_LogError(const char *fmt, ...)
{
	char msg[MAX_MESSAGE_SIZE];
	va_list ap;
	va_start(ap, fmt);
	SDL_vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	Refresh_LogErrorFunc(msg);
}

#undef MAX_MESSAGE_SIZE

void Refresh_HookLogFunctions(
	Refresh_LogFunc info,
	Refresh_LogFunc warn,
	Refresh_LogFunc error
) {
	Refresh_LogInfoFunc = info;
	Refresh_LogWarnFunc = warn;
	Refresh_LogErrorFunc = error;
}

/* Version API */

uint32_t Refresh_LinkedVersion(void)
{
	return REFRESH_COMPILED_VERSION;
}

/* Driver Functions */

static int32_t selectedDriver = 0;

Refresh_Device* Refresh_CreateDevice(
    Refresh_PresentationParameters *presentationParameters,
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

Refresh_Device* Refresh_CreateDeviceUsingExternal(
    Refresh_SysRenderer *sysRenderer,
    uint8_t debugMode
) {
    if (selectedDriver < 0)
    {
        return NULL;
    }

    if (sysRenderer == NULL)
    {
        return NULL;
    }

    return drivers[selectedDriver]->CreateDeviceUsingExternal(
        sysRenderer,
        debugMode
    );
}

void Refresh_DestroyDevice(Refresh_Device *device)
{
    NULL_RETURN(device);
    device->DestroyDevice(device);
}

void Refresh_Clear(
    Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Rect *clearRect,
	Refresh_ClearOptions options,
	Refresh_Color *colors,
	uint32_t colorCount,
    Refresh_DepthStencilValue depthStencil
) {
    NULL_RETURN(device);
    device->Clear(
        device->driverData,
        commandBuffer,
        clearRect,
        options,
        colors,
        colorCount,
        depthStencil
    );
}

void Refresh_DrawIndexedPrimitives(
	Refresh_Device *device,
    Refresh_CommandBuffer *commandBuffer,
	uint32_t baseVertex,
	uint32_t startIndex,
	uint32_t primitiveCount,
    uint32_t vertexParamOffset,
	uint32_t fragmentParamOffset
) {
    NULL_RETURN(device);
    device->DrawIndexedPrimitives(
        device->driverData,
        commandBuffer,
        baseVertex,
        startIndex,
        primitiveCount,
        vertexParamOffset,
        fragmentParamOffset
    );
}

void Refresh_DrawInstancedPrimitives(
	Refresh_Device *device,
    Refresh_CommandBuffer *commandBuffer,
	uint32_t baseVertex,
	uint32_t startIndex,
	uint32_t primitiveCount,
	uint32_t instanceCount,
    uint32_t vertexParamOffset,
	uint32_t fragmentParamOffset
) {
    NULL_RETURN(device);
    device->DrawInstancedPrimitives(
        device->driverData,
        commandBuffer,
        baseVertex,
        startIndex,
        primitiveCount,
        instanceCount,
        vertexParamOffset,
        fragmentParamOffset
    );
}

void Refresh_DrawPrimitives(
	Refresh_Device *device,
    Refresh_CommandBuffer *commandBuffer,
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

void Refresh_DispatchCompute(
    Refresh_Device *device,
    Refresh_CommandBuffer *commandBuffer,
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

Refresh_RenderPass* Refresh_CreateRenderPass(
	Refresh_Device *device,
	Refresh_RenderPassCreateInfo *renderPassCreateInfo
) {
    NULL_RETURN_NULL(device);
    return device->CreateRenderPass(
        device->driverData,
        renderPassCreateInfo
    );
}

Refresh_ComputePipeline* Refresh_CreateComputePipeline(
    Refresh_Device *device,
    Refresh_ComputePipelineCreateInfo *pipelineCreateInfo
) {
    NULL_RETURN_NULL(device);
    return device->CreateComputePipeline(
        device->driverData,
        pipelineCreateInfo
    );
}

Refresh_GraphicsPipeline* Refresh_CreateGraphicsPipeline(
	Refresh_Device *device,
	Refresh_GraphicsPipelineCreateInfo *pipelineCreateInfo
) {
    NULL_RETURN_NULL(device);
    return device->CreateGraphicsPipeline(
        device->driverData,
        pipelineCreateInfo
    );
}

Refresh_Sampler* Refresh_CreateSampler(
	Refresh_Device *device,
	Refresh_SamplerStateCreateInfo *samplerStateCreateInfo
) {
    NULL_RETURN_NULL(device);
    return device->CreateSampler(
        device->driverData,
        samplerStateCreateInfo
    );
}

Refresh_Framebuffer* Refresh_CreateFramebuffer(
	Refresh_Device *device,
	Refresh_FramebufferCreateInfo *framebufferCreateInfo
) {
    NULL_RETURN_NULL(device);
    return device->CreateFramebuffer(
        device->driverData,
        framebufferCreateInfo
    );
}

Refresh_ShaderModule* Refresh_CreateShaderModule(
	Refresh_Device *device,
	Refresh_ShaderModuleCreateInfo *shaderModuleCreateInfo
) {
    NULL_RETURN_NULL(device);
    return device->CreateShaderModule(
        device->driverData,
        shaderModuleCreateInfo
    );
}

Refresh_Texture* Refresh_CreateTexture(
	Refresh_Device *device,
    Refresh_TextureCreateInfo *textureCreateInfo
) {
    NULL_RETURN_NULL(device);
    return device->CreateTexture(
        device->driverData,
        textureCreateInfo
    );
}

Refresh_ColorTarget* Refresh_CreateColorTarget(
	Refresh_Device *device,
    Refresh_SampleCount multisampleCount,
	Refresh_TextureSlice *textureSlice
) {
    NULL_RETURN_NULL(device);
    return device->CreateColorTarget(
        device->driverData,
        multisampleCount,
        textureSlice
    );
}

Refresh_DepthStencilTarget* Refresh_CreateDepthStencilTarget(
	Refresh_Device *device,
	uint32_t width,
	uint32_t height,
	Refresh_DepthFormat format
) {
    NULL_RETURN_NULL(device);
    return device->CreateDepthStencilTarget(
        device->driverData,
        width,
        height,
        format
    );
}

Refresh_Buffer* Refresh_CreateBuffer(
	Refresh_Device *device,
    Refresh_BufferUsageFlags usageFlags,
	uint32_t sizeInBytes
) {
    NULL_RETURN_NULL(device);
    return device->CreateBuffer(
        device->driverData,
        usageFlags,
        sizeInBytes
    );
}

void Refresh_SetTextureData(
	Refresh_Device *device,
	Refresh_TextureSlice *textureSlice,
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

void Refresh_SetTextureDataYUV(
	Refresh_Device *device,
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

void Refresh_CopyTextureToTexture(
    Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_TextureSlice *sourceTextureSlice,
	Refresh_TextureSlice *destinationTextureSlice,
	Refresh_Filter filter
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

void Refresh_CopyTextureToBuffer(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_TextureSlice *textureSlice,
	Refresh_Buffer *buffer
) {
    NULL_RETURN(device);
    device->CopyTextureToBuffer(
        device->driverData,
        commandBuffer,
        textureSlice,
        buffer
    );
}

void Refresh_SetBufferData(
	Refresh_Device *device,
	Refresh_Buffer *buffer,
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

uint32_t Refresh_PushVertexShaderParams(
	Refresh_Device *device,
    Refresh_CommandBuffer *commandBuffer,
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

uint32_t Refresh_PushFragmentShaderParams(
	Refresh_Device *device,
    Refresh_CommandBuffer *commandBuffer,
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

uint32_t Refresh_PushComputeShaderParams(
    Refresh_Device *device,
    Refresh_CommandBuffer *commandBuffer,
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

void Refresh_BindVertexSamplers(
	Refresh_Device *device,
    Refresh_CommandBuffer *commandBuffer,
	Refresh_Texture **pTextures,
	Refresh_Sampler **pSamplers
) {
    NULL_RETURN(device);
    device->BindVertexSamplers(
        device->driverData,
        commandBuffer,
        pTextures,
        pSamplers
    );
}

void Refresh_BindFragmentSamplers(
	Refresh_Device *device,
    Refresh_CommandBuffer *commandBuffer,
	Refresh_Texture **pTextures,
	Refresh_Sampler **pSamplers
) {
    NULL_RETURN(device);
    device->BindFragmentSamplers(
        device->driverData,
        commandBuffer,
        pTextures,
        pSamplers
    );
}

void Refresh_GetBufferData(
    Refresh_Device *device,
    Refresh_Buffer *buffer,
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

void Refresh_QueueDestroyTexture(
	Refresh_Device *device,
	Refresh_Texture *texture
) {
    NULL_RETURN(device);
    device->QueueDestroyTexture(
        device->driverData,
        texture
    );
}

void Refresh_QueueDestroySampler(
	Refresh_Device *device,
	Refresh_Sampler *sampler
) {
    NULL_RETURN(device);
    device->QueueDestroySampler(
        device->driverData,
        sampler
    );
}

void Refresh_QueueDestroyBuffer(
	Refresh_Device *device,
	Refresh_Buffer *buffer
) {
    NULL_RETURN(device);
    device->QueueDestroyBuffer(
        device->driverData,
        buffer
    );
}

void Refresh_QueueDestroyColorTarget(
	Refresh_Device *device,
	Refresh_ColorTarget *colorTarget
) {
    NULL_RETURN(device);
    device->QueueDestroyColorTarget(
        device->driverData,
        colorTarget
    );
}

void Refresh_QueueDestroyDepthStencilTarget(
	Refresh_Device *device,
	Refresh_DepthStencilTarget *depthStencilTarget
) {
    NULL_RETURN(device);
    device->QueueDestroyDepthStencilTarget(
        device->driverData,
        depthStencilTarget
    );
}

void Refresh_QueueDestroyFramebuffer(
	Refresh_Device *device,
	Refresh_Framebuffer *frameBuffer
) {
    NULL_RETURN(device);
    device->QueueDestroyFramebuffer(
        device->driverData,
        frameBuffer
    );
}

void Refresh_QueueDestroyShaderModule(
	Refresh_Device *device,
	Refresh_ShaderModule *shaderModule
) {
    NULL_RETURN(device);
    device->QueueDestroyShaderModule(
        device->driverData,
        shaderModule
    );
}

void Refresh_QueueDestroyRenderPass(
	Refresh_Device *device,
	Refresh_RenderPass *renderPass
) {
    NULL_RETURN(device);
    device->QueueDestroyRenderPass(
        device->driverData,
        renderPass
    );
}

void Refresh_QueueDestroyComputePipeline(
	Refresh_Device *device,
	Refresh_ComputePipeline *computePipeline
) {
    NULL_RETURN(device);
    device->QueueDestroyComputePipeline(
        device->driverData,
        computePipeline
    );
}

void Refresh_QueueDestroyGraphicsPipeline(
	Refresh_Device *device,
	Refresh_GraphicsPipeline *graphicsPipeline
) {
    NULL_RETURN(device);
    device->QueueDestroyGraphicsPipeline(
        device->driverData,
        graphicsPipeline
    );
}

void Refresh_BeginRenderPass(
	Refresh_Device *device,
    Refresh_CommandBuffer *commandBuffer,
	Refresh_RenderPass *renderPass,
	Refresh_Framebuffer *framebuffer,
	Refresh_Rect renderArea,
	Refresh_Color *pColorClearValues,
	uint32_t colorClearCount,
	Refresh_DepthStencilValue *depthStencilClearValue
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

void Refresh_EndRenderPass(
	Refresh_Device *device,
    Refresh_CommandBuffer *commandBuffer
) {
    NULL_RETURN(device);
    device->EndRenderPass(
        device->driverData,
        commandBuffer
    );
}

void Refresh_BindGraphicsPipeline(
	Refresh_Device *device,
    Refresh_CommandBuffer *commandBuffer,
	Refresh_GraphicsPipeline *graphicsPipeline
) {
    NULL_RETURN(device);
    device->BindGraphicsPipeline(
        device->driverData,
        commandBuffer,
        graphicsPipeline
    );
}

void Refresh_BindVertexBuffers(
	Refresh_Device *device,
    Refresh_CommandBuffer *commandBuffer,
	uint32_t firstBinding,
	uint32_t bindingCount,
	Refresh_Buffer **pBuffers,
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

void Refresh_BindIndexBuffer(
    Refresh_Device *device,
    Refresh_CommandBuffer *commandBuffer,
    Refresh_Buffer *buffer,
	uint64_t offset,
	Refresh_IndexElementSize indexElementSize
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

void Refresh_BindComputePipeline(
    Refresh_Device *device,
    Refresh_CommandBuffer *commandBuffer,
    Refresh_ComputePipeline *computePipeline
) {
    NULL_RETURN(device);
    device->BindComputePipeline(
        device->driverData,
        commandBuffer,
        computePipeline
    );
}

void Refresh_BindComputeBuffers(
    Refresh_Device *device,
    Refresh_CommandBuffer *commandBuffer,
    Refresh_Buffer **pBuffers
) {
    NULL_RETURN(device);
    device->BindComputeBuffers(
        device->driverData,
        commandBuffer,
        pBuffers
    );
}

void Refresh_BindComputeTextures(
    Refresh_Device *device,
    Refresh_CommandBuffer *commandBuffer,
    Refresh_Texture **pTextures
) {
    NULL_RETURN(device);
    device->BindComputeTextures(
        device->driverData,
        commandBuffer,
        pTextures
    );
}

Refresh_CommandBuffer* Refresh_AcquireCommandBuffer(
    Refresh_Device *device,
    uint8_t fixed
) {
    NULL_RETURN_NULL(device);
    return device->AcquireCommandBuffer(
        device->driverData,
        fixed
    );
}

void Refresh_QueuePresent(
    Refresh_Device *device,
    Refresh_CommandBuffer *commandBuffer,
    Refresh_TextureSlice* textureSlice,
    Refresh_Rect *destinationRectangle,
    Refresh_Filter filter
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

void Refresh_Submit(
    Refresh_Device *device,
	uint32_t commandBufferCount,
	Refresh_CommandBuffer **pCommandBuffers
) {
    NULL_RETURN(device);
    device->Submit(
        device->driverData,
        commandBufferCount,
        pCommandBuffers
    );
}

void Refresh_Wait(
    Refresh_Device *device
) {
    NULL_RETURN(device);
    device->Wait(
        device->driverData
    );
}

void Refresh_GetTextureHandles(
    Refresh_Device* device,
    Refresh_Texture* texture,
    Refresh_TextureHandles *handles
) {
    NULL_RETURN(device);
    device->GetTextureHandles(
        device->driverData,
        texture,
        handles
    );
}

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
