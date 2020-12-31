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
	uint32_t vertexStart,
	uint32_t primitiveCount,
    uint32_t vertexParamOffset,
	uint32_t fragmentParamOffset
) {
    NULL_RETURN(device);
    device->DrawPrimitives(
        device->driverData,
        vertexStart,
        primitiveCount,
        vertexParamOffset,
        fragmentParamOffset
    );
}

void REFRESH_DispatchCompute(
    REFRESH_Device *device,
    uint32_t groupCountX,
    uint32_t groupCountY,
    uint32_t groupCountZ,
    uint32_t computeParamOffset
) {
    NULL_RETURN(device);
    device->DispatchCompute(
        device->driverData,
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

void REFRESH_SetTextureData2D(
	REFRESH_Device *device,
	REFRESH_Texture *texture,
	uint32_t x,
	uint32_t y,
	uint32_t w,
	uint32_t h,
	uint32_t level,
	void *data,
	uint32_t dataLengthInBytes
) {
    NULL_RETURN(device);
    device->SetTextureData2D(
        device->driverData,
        texture,
        x,
        y,
        w,
        h,
        level,
        data,
        dataLengthInBytes
    );
}

void REFRESH_SetTextureData3D(
	REFRESH_Device *device,
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
    NULL_RETURN(device);
    device->SetTextureData3D(
        device->driverData,
        texture,
        x,
        y,
        z,
        w,
        h,
        d,
        level,
        data,
        dataLength
    );
}

void REFRESH_SetTextureDataCube(
	REFRESH_Device *device,
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
    NULL_RETURN(device);
    device->SetTextureDataCube(
        device->driverData,
        texture,
        x,
        y,
        w,
        h,
        cubeMapFace,
        level,
        data,
        dataLength
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
	void *data,
	uint32_t elementCount
) {
    if (device == NULL) { return 0; }
    return device->PushVertexShaderParams(
        device->driverData,
        data,
        elementCount
    );
}

uint32_t REFRESH_PushFragmentShaderParams(
	REFRESH_Device *device,
	void *data,
	uint32_t elementCount
) {
    if (device == NULL) { return 0; }
    return device->PushFragmentShaderParams(
        device->driverData,
        data,
        elementCount
    );
}

uint32_t REFRESH_PushComputeShaderParams(
    REFRESH_Device *device,
    void *data,
    uint32_t elementCount
) {
    if (device == NULL) { return 0; }
    return device->PushComputeShaderParams(
        device->driverData,
        data,
        elementCount
    );
}

void REFRESH_SetVertexSamplers(
	REFRESH_Device *device,
	REFRESH_Texture **pTextures,
	REFRESH_Sampler **pSamplers
) {
    NULL_RETURN(device);
    device->SetVertexSamplers(
        device->driverData,
        pTextures,
        pSamplers
    );
}

void REFRESH_SetFragmentSamplers(
	REFRESH_Device *device,
	REFRESH_Texture **pTextures,
	REFRESH_Sampler **pSamplers
) {
    NULL_RETURN(device);
    device->SetFragmentSamplers(
        device->driverData,
        pTextures,
        pSamplers
    );
}

void REFRESH_GetTextureData2D(
	REFRESH_Device *device,
	REFRESH_Texture *texture,
	uint32_t x,
	uint32_t y,
	uint32_t w,
	uint32_t h,
	uint32_t level,
	void* data
) {
    NULL_RETURN(device);
    device->GetTextureData2D(
        device->driverData,
        texture,
        x,
        y,
        w,
        h,
        level,
        data
    );
}

void REFRESH_GetTextureDataCube(
	REFRESH_Device *device,
	REFRESH_Texture *texture,
	uint32_t x,
	uint32_t y,
	uint32_t w,
	uint32_t h,
	REFRESH_CubeMapFace cubeMapFace,
	uint32_t level,
	void* data
) {
    NULL_RETURN(device);
    device->GetTextureDataCube(
        device->driverData,
        texture,
        x,
        y,
        w,
        h,
        cubeMapFace,
        level,
        data
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

void REFRESH_AddDisposeVertexBuffer(
	REFRESH_Device *device,
	REFRESH_Buffer *buffer
) {
    NULL_RETURN(device);
    device->AddDisposeVertexBuffer(
        device->driverData,
        buffer
    );
}

void REFRESH_AddDisposeIndexBuffer(
	REFRESH_Device *device,
	REFRESH_Buffer *buffer
) {
    NULL_RETURN(device);
    device->AddDisposeIndexBuffer(
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
        renderPass,
        framebuffer,
        renderArea,
        pColorClearValues,
        colorClearCount,
        depthStencilClearValue
    );
}

void REFRESH_EndRenderPass(
	REFRESH_Device *device
) {
    NULL_RETURN(device);
    device->EndRenderPass(device->driverData);
}

void REFRESH_BindGraphicsPipeline(
	REFRESH_Device *device,
	REFRESH_GraphicsPipeline *graphicsPipeline
) {
    NULL_RETURN(device);
    device->BindGraphicsPipeline(
        device->driverData,
        graphicsPipeline
    );
}

void REFRESH_BindVertexBuffers(
	REFRESH_Device *device,
	uint32_t firstBinding,
	uint32_t bindingCount,
	REFRESH_Buffer **pBuffers,
	uint64_t *pOffsets
) {
    NULL_RETURN(device);
    device->BindVertexBuffers(
        device->driverData,
        firstBinding,
        bindingCount,
        pBuffers,
        pOffsets
    );
}

void REFRESH_BindIndexBuffer(
    REFRESH_Device *device,
    REFRESH_Buffer *buffer,
	uint64_t offset,
	REFRESH_IndexElementSize indexElementSize
) {
    NULL_RETURN(device);
    device->BindIndexBuffer(
        device->driverData,
        buffer,
        offset,
        indexElementSize
    );
}

void REFRESH_BindComputePipeline(
    REFRESH_Device *device,
    REFRESH_ComputePipeline *computePipeline
) {
    NULL_RETURN(device);
    device->BindComputePipeline(
        device->driverData,
        computePipeline
    );
}

void REFRESH_BindComputeBuffers(
    REFRESH_Device *device,
    REFRESH_Buffer **pBuffers
) {
    NULL_RETURN(device);
    device->BindComputeBuffers(
        device->driverData,
        pBuffers
    );
}

void REFRESH_BindComputeTextures(
    REFRESH_Device *device,
    REFRESH_Texture **pTextures
) {
    NULL_RETURN(device);
    device->BindComputeTextures(
        device->driverData,
        pTextures
    );
}

void REFRESH_QueuePresent(
    REFRESH_Device *device,
    REFRESH_TextureSlice* textureSlice,
    REFRESH_Rect *sourceRectangle,
    REFRESH_Rect *destinationRectangle
) {
    NULL_RETURN(device);
    device->QueuePresent(
        device->driverData,
        textureSlice,
        sourceRectangle,
        destinationRectangle
    );
}

void REFRESH_Submit(
    REFRESH_Device *device
) {
    NULL_RETURN(device);
    device->Submit(
        device->driverData
    );
}

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
