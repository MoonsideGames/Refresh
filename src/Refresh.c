/* Refresh - a cross-platform hardware-accelerated graphics library with modern capabilities
 *
 * Copyright (c) 2020-2024 Evan Hemsley
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

#include "Refresh_driver.h"
#include "Refresh_spirv_c.h"

#define NULL_ASSERT(name) SDL_assert(name != NULL);

#define CHECK_COMMAND_BUFFER                                                             \
    if (commandBuffer == NULL) {                                                         \
        return;                                                                          \
    }                                                                                    \
    if (((CommandBufferCommonHeader *)commandBuffer)->submitted) {                       \
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Command buffer already submitted!"); \
        return;                                                                          \
    }

#define CHECK_COMMAND_BUFFER_RETURN_NULL                                                 \
    if (commandBuffer == NULL) {                                                         \
        return NULL;                                                                     \
    }                                                                                    \
    if (((CommandBufferCommonHeader *)commandBuffer)->submitted) {                       \
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Command buffer already submitted!"); \
        return NULL;                                                                     \
    }

#define CHECK_ANY_PASS_IN_PROGRESS                                               \
    if (                                                                         \
        ((CommandBufferCommonHeader *)commandBuffer)->renderPass.inProgress ||   \
        ((CommandBufferCommonHeader *)commandBuffer)->computePass.inProgress ||  \
        ((CommandBufferCommonHeader *)commandBuffer)->copyPass.inProgress) {     \
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Pass already in progress!"); \
        return NULL;                                                             \
    }

#define CHECK_RENDERPASS                                                            \
    if (!((Pass *)renderPass)->inProgress) {                                        \
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Render pass not in progress!"); \
        return;                                                                     \
    }

#define CHECK_GRAPHICS_PIPELINE_BOUND                                                       \
    if (!((CommandBufferCommonHeader *)RENDERPASS_COMMAND_BUFFER)->graphicsPipelineBound) { \
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Graphics pipeline not bound!");         \
        return;                                                                             \
    }

#define CHECK_COMPUTEPASS                                                            \
    if (!((Pass *)computePass)->inProgress) {                                        \
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Compute pass not in progress!"); \
        return;                                                                      \
    }

#define CHECK_COMPUTE_PIPELINE_BOUND                                                        \
    if (!((CommandBufferCommonHeader *)COMPUTEPASS_COMMAND_BUFFER)->computePipelineBound) { \
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Compute pipeline not bound!");          \
        return;                                                                             \
    }

#define CHECK_COPYPASS                                                            \
    if (!((Pass *)copyPass)->inProgress) {                                        \
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Copy pass not in progress!"); \
        return;                                                                   \
    }

#define COMMAND_BUFFER_DEVICE \
    ((CommandBufferCommonHeader *)commandBuffer)->device

#define RENDERPASS_COMMAND_BUFFER \
    ((Pass *)renderPass)->commandBuffer

#define RENDERPASS_DEVICE \
    ((CommandBufferCommonHeader *)RENDERPASS_COMMAND_BUFFER)->device

#define COMPUTEPASS_COMMAND_BUFFER \
    ((Pass *)computePass)->commandBuffer

#define COMPUTEPASS_DEVICE \
    ((CommandBufferCommonHeader *)COMPUTEPASS_COMMAND_BUFFER)->device

#define COPYPASS_COMMAND_BUFFER \
    ((Pass *)copyPass)->commandBuffer

#define COPYPASS_DEVICE \
    ((CommandBufferCommonHeader *)COPYPASS_COMMAND_BUFFER)->device

/* Drivers */

static const Refresh_Driver *backends[] = {
#if REFRESH_METAL
    &MetalDriver,
#endif
#if REFRESH_VULKAN
    &VulkanDriver,
#endif
#if REFRESH_D3D11
    &D3D11Driver,
#endif
    NULL
};

/* Driver Functions */

static Refresh_Backend Refresh_SelectBackend(Refresh_Backend preferredBackends)
{
    Uint32 i;

    /* Environment override... */
    const char *gpudriver = SDL_GetHint("REFRESH_HINT_BACKEND");
    if (gpudriver != NULL) {
        for (i = 0; backends[i]; i += 1) {
            if (SDL_strcasecmp(gpudriver, backends[i]->Name) == 0 && backends[i]->PrepareDriver()) {
                return backends[i]->backendflag;
            }
        }

        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "REFRESH_HINT_BACKEND %s unsupported!", gpudriver);
        return REFRESH_BACKEND_INVALID;
    }

    /* Preferred backends... */
    if (preferredBackends != REFRESH_BACKEND_INVALID) {
        for (i = 0; backends[i]; i += 1) {
            if ((preferredBackends & backends[i]->backendflag) && backends[i]->PrepareDriver()) {
                return backends[i]->backendflag;
            }
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No preferred Refresh_ backend found!");
    }

    /* ... Fallback backends */
    for (i = 0; backends[i]; i += 1) {
        if (backends[i]->PrepareDriver()) {
            return backends[i]->backendflag;
        }
    }

    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No supported Refresh_ backend found!");
    return REFRESH_BACKEND_INVALID;
}

Refresh_Device *Refresh_CreateDevice(
    Refresh_Backend preferredBackends,
    SDL_bool debugMode)
{
    int i;
    Refresh_Device *result = NULL;
    Refresh_Backend selectedBackend;

    selectedBackend = Refresh_SelectBackend(preferredBackends);
    if (selectedBackend != REFRESH_BACKEND_INVALID) {
        for (i = 0; backends[i]; i += 1) {
            if (backends[i]->backendflag == selectedBackend) {
                result = backends[i]->CreateDevice(debugMode);
                if (result != NULL) {
                    result->backend = backends[i]->backendflag;
                    break;
                }
            }
        }
    }
    return result;
}

void Refresh_DestroyDevice(Refresh_Device *device)
{
    NULL_ASSERT(device);
    device->DestroyDevice(device);
}

Refresh_Backend Refresh_GetBackend(Refresh_Device *device)
{
    if (device == NULL) {
        return REFRESH_BACKEND_INVALID;
    }
    return device->backend;
}

Uint32 Refresh_TextureFormatTexelBlockSize(
    Refresh_TextureFormat textureFormat)
{
    switch (textureFormat) {
    case REFRESH_TEXTUREFORMAT_BC1:
        return 8;
    case REFRESH_TEXTUREFORMAT_BC2:
    case REFRESH_TEXTUREFORMAT_BC3:
    case REFRESH_TEXTUREFORMAT_BC7:
    case REFRESH_TEXTUREFORMAT_BC3_SRGB:
    case REFRESH_TEXTUREFORMAT_BC7_SRGB:
        return 16;
    case REFRESH_TEXTUREFORMAT_R8:
    case REFRESH_TEXTUREFORMAT_A8:
    case REFRESH_TEXTUREFORMAT_R8_UINT:
        return 1;
    case REFRESH_TEXTUREFORMAT_R5G6B5:
    case REFRESH_TEXTUREFORMAT_B4G4R4A4:
    case REFRESH_TEXTUREFORMAT_A1R5G5B5:
    case REFRESH_TEXTUREFORMAT_R16_SFLOAT:
    case REFRESH_TEXTUREFORMAT_R8G8_SNORM:
    case REFRESH_TEXTUREFORMAT_R8G8_UINT:
    case REFRESH_TEXTUREFORMAT_R16_UINT:
        return 2;
    case REFRESH_TEXTUREFORMAT_R8G8B8A8:
    case REFRESH_TEXTUREFORMAT_B8G8R8A8:
    case REFRESH_TEXTUREFORMAT_R8G8B8A8_SRGB:
    case REFRESH_TEXTUREFORMAT_B8G8R8A8_SRGB:
    case REFRESH_TEXTUREFORMAT_R32_SFLOAT:
    case REFRESH_TEXTUREFORMAT_R16G16_SFLOAT:
    case REFRESH_TEXTUREFORMAT_R8G8B8A8_SNORM:
    case REFRESH_TEXTUREFORMAT_A2R10G10B10:
    case REFRESH_TEXTUREFORMAT_R8G8B8A8_UINT:
    case REFRESH_TEXTUREFORMAT_R16G16_UINT:
        return 4;
    case REFRESH_TEXTUREFORMAT_R16G16B16A16_SFLOAT:
    case REFRESH_TEXTUREFORMAT_R16G16B16A16:
    case REFRESH_TEXTUREFORMAT_R32G32_SFLOAT:
    case REFRESH_TEXTUREFORMAT_R16G16B16A16_UINT:
        return 8;
    case REFRESH_TEXTUREFORMAT_R32G32B32A32_SFLOAT:
        return 16;
    default:
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Unrecognized TextureFormat!");
        return 0;
    }
}

SDL_bool Refresh_IsTextureFormatSupported(
    Refresh_Device *device,
    Refresh_TextureFormat format,
    Refresh_TextureType type,
    Refresh_TextureUsageFlags usage)
{
    if (device == NULL) {
        return SDL_FALSE;
    }
    return device->IsTextureFormatSupported(
        device->driverData,
        format,
        type,
        usage);
}

Refresh_SampleCount Refresh_GetBestSampleCount(
    Refresh_Device *device,
    Refresh_TextureFormat format,
    Refresh_SampleCount desiredSampleCount)
{
    if (device == NULL) {
        return 0;
    }
    return device->GetBestSampleCount(
        device->driverData,
        format,
        desiredSampleCount);
}

/* State Creation */

Refresh_ComputePipeline *Refresh_CreateComputePipeline(
    Refresh_Device *device,
    Refresh_ComputePipelineCreateInfo *computePipelineCreateInfo)
{
    NULL_ASSERT(device)
    if (computePipelineCreateInfo->threadCountX == 0 ||
        computePipelineCreateInfo->threadCountY == 0 ||
        computePipelineCreateInfo->threadCountZ == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "All ComputePipeline threadCount dimensions must be at least 1!");
        return NULL;
    }
    if (computePipelineCreateInfo->format == REFRESH_SHADERFORMAT_SPIRV &&
        device->backend != REFRESH_BACKEND_VULKAN) {
        return SDL_CompileFromSPIRV(device, computePipelineCreateInfo, SDL_TRUE);
    }
    return device->CreateComputePipeline(
        device->driverData,
        computePipelineCreateInfo);
}

Refresh_GraphicsPipeline *Refresh_CreateGraphicsPipeline(
    Refresh_Device *device,
    Refresh_GraphicsPipelineCreateInfo *graphicsPipelineCreateInfo)
{
    Refresh_TextureFormat newFormat;

    NULL_ASSERT(device)

    /* Automatically swap out the depth format if it's unsupported.
     * See Refresh_CreateTexture.
     */
    if (
        graphicsPipelineCreateInfo->attachmentInfo.hasDepthStencilAttachment &&
        !device->IsTextureFormatSupported(
            device->driverData,
            graphicsPipelineCreateInfo->attachmentInfo.depthStencilFormat,
            REFRESH_TEXTURETYPE_2D,
            REFRESH_TEXTUREUSAGE_DEPTH_STENCIL_TARGET_BIT)) {
        switch (graphicsPipelineCreateInfo->attachmentInfo.depthStencilFormat) {
        case REFRESH_TEXTUREFORMAT_D24_UNORM:
            newFormat = REFRESH_TEXTUREFORMAT_D32_SFLOAT;
            break;
        case REFRESH_TEXTUREFORMAT_D32_SFLOAT:
            newFormat = REFRESH_TEXTUREFORMAT_D24_UNORM;
            break;
        case REFRESH_TEXTUREFORMAT_D24_UNORM_S8_UINT:
            newFormat = REFRESH_TEXTUREFORMAT_D32_SFLOAT_S8_UINT;
            break;
        case REFRESH_TEXTUREFORMAT_D32_SFLOAT_S8_UINT:
            newFormat = REFRESH_TEXTUREFORMAT_D24_UNORM_S8_UINT;
            break;
        default:
            /* This should never happen, but just in case... */
            newFormat = REFRESH_TEXTUREFORMAT_D16_UNORM;
            break;
        }

        SDL_LogWarn(
            SDL_LOG_CATEGORY_APPLICATION,
            "Requested unsupported depth format %d, falling back to format %d!",
            graphicsPipelineCreateInfo->attachmentInfo.depthStencilFormat,
            newFormat);
        graphicsPipelineCreateInfo->attachmentInfo.depthStencilFormat = newFormat;
    }

    return device->CreateGraphicsPipeline(
        device->driverData,
        graphicsPipelineCreateInfo);
}

Refresh_Sampler *Refresh_CreateSampler(
    Refresh_Device *device,
    Refresh_SamplerCreateInfo *samplerCreateInfo)
{
    NULL_ASSERT(device)
    return device->CreateSampler(
        device->driverData,
        samplerCreateInfo);
}

Refresh_Shader *Refresh_CreateShader(
    Refresh_Device *device,
    Refresh_ShaderCreateInfo *shaderCreateInfo)
{
    NULL_ASSERT(device)
    if (shaderCreateInfo->format == REFRESH_SHADERFORMAT_SPIRV &&
        device->backend != REFRESH_BACKEND_VULKAN) {
        return SDL_CompileFromSPIRV(device, shaderCreateInfo, SDL_FALSE);
    }
    return device->CreateShader(
        device->driverData,
        shaderCreateInfo);
}

Refresh_Texture *Refresh_CreateTexture(
    Refresh_Device *device,
    Refresh_TextureCreateInfo *textureCreateInfo)
{
    Refresh_TextureFormat newFormat;

    NULL_ASSERT(device)

    /* Automatically swap out the depth format if it's unsupported.
     * All backends have universal support for D16.
     * Vulkan always supports at least one of { D24, D32 } and one of { D24_S8, D32_S8 }.
     * D3D11 always supports all depth formats.
     * Metal always supports D32 and D32_S8.
     * So if D32/_S8 is not supported, we can safely fall back to D24/_S8, and vice versa.
     */
    if (IsDepthFormat(textureCreateInfo->format)) {
        if (!device->IsTextureFormatSupported(
                device->driverData,
                textureCreateInfo->format,
                REFRESH_TEXTURETYPE_2D, /* assuming that driver support for 2D implies support for Cube */
                textureCreateInfo->usageFlags)) {
            switch (textureCreateInfo->format) {
            case REFRESH_TEXTUREFORMAT_D24_UNORM:
                newFormat = REFRESH_TEXTUREFORMAT_D32_SFLOAT;
                break;
            case REFRESH_TEXTUREFORMAT_D32_SFLOAT:
                newFormat = REFRESH_TEXTUREFORMAT_D24_UNORM;
                break;
            case REFRESH_TEXTUREFORMAT_D24_UNORM_S8_UINT:
                newFormat = REFRESH_TEXTUREFORMAT_D32_SFLOAT_S8_UINT;
                break;
            case REFRESH_TEXTUREFORMAT_D32_SFLOAT_S8_UINT:
                newFormat = REFRESH_TEXTUREFORMAT_D24_UNORM_S8_UINT;
                break;
            default:
                /* This should never happen, but just in case... */
                newFormat = REFRESH_TEXTUREFORMAT_D16_UNORM;
                break;
            }

            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "Requested unsupported depth format %d, falling back to format %d!",
                textureCreateInfo->format,
                newFormat);
            textureCreateInfo->format = newFormat;
        }
    }

    return device->CreateTexture(
        device->driverData,
        textureCreateInfo);
}

Refresh_Buffer *Refresh_CreateBuffer(
    Refresh_Device *device,
    Refresh_BufferUsageFlags usageFlags,
    Uint32 sizeInBytes)
{
    NULL_ASSERT(device)
    return device->CreateBuffer(
        device->driverData,
        usageFlags,
        sizeInBytes);
}

Refresh_TransferBuffer *Refresh_CreateTransferBuffer(
    Refresh_Device *device,
    Refresh_TransferBufferUsage usage,
    Uint32 sizeInBytes)
{
    NULL_ASSERT(device)
    return device->CreateTransferBuffer(
        device->driverData,
        usage,
        sizeInBytes);
}

/* Debug Naming */

void Refresh_SetBufferName(
    Refresh_Device *device,
    Refresh_Buffer *buffer,
    const char *text)
{
    NULL_ASSERT(device)
    NULL_ASSERT(buffer)

    device->SetBufferName(
        device->driverData,
        buffer,
        text);
}

void Refresh_SetTextureName(
    Refresh_Device *device,
    Refresh_Texture *texture,
    const char *text)
{
    NULL_ASSERT(device)
    NULL_ASSERT(texture)

    device->SetTextureName(
        device->driverData,
        texture,
        text);
}

void Refresh_InsertDebugLabel(
    Refresh_CommandBuffer *commandBuffer,
    const char *text)
{
    CHECK_COMMAND_BUFFER
    COMMAND_BUFFER_DEVICE->InsertDebugLabel(
        commandBuffer,
        text);
}

void Refresh_PushDebugGroup(
    Refresh_CommandBuffer *commandBuffer,
    const char *name)
{
    CHECK_COMMAND_BUFFER
    COMMAND_BUFFER_DEVICE->PushDebugGroup(
        commandBuffer,
        name);
}

void Refresh_PopDebugGroup(
    Refresh_CommandBuffer *commandBuffer)
{
    CHECK_COMMAND_BUFFER
    COMMAND_BUFFER_DEVICE->PopDebugGroup(
        commandBuffer);
}

/* Disposal */

void Refresh_ReleaseTexture(
    Refresh_Device *device,
    Refresh_Texture *texture)
{
    NULL_ASSERT(device);
    device->ReleaseTexture(
        device->driverData,
        texture);
}

void Refresh_ReleaseSampler(
    Refresh_Device *device,
    Refresh_Sampler *sampler)
{
    NULL_ASSERT(device);
    device->ReleaseSampler(
        device->driverData,
        sampler);
}

void Refresh_ReleaseBuffer(
    Refresh_Device *device,
    Refresh_Buffer *buffer)
{
    NULL_ASSERT(device);
    device->ReleaseBuffer(
        device->driverData,
        buffer);
}

void Refresh_ReleaseTransferBuffer(
    Refresh_Device *device,
    Refresh_TransferBuffer *transferBuffer)
{
    NULL_ASSERT(device);
    device->ReleaseTransferBuffer(
        device->driverData,
        transferBuffer);
}

void Refresh_ReleaseShader(
    Refresh_Device *device,
    Refresh_Shader *shader)
{
    NULL_ASSERT(device);
    device->ReleaseShader(
        device->driverData,
        shader);
}

void Refresh_ReleaseComputePipeline(
    Refresh_Device *device,
    Refresh_ComputePipeline *computePipeline)
{
    NULL_ASSERT(device);
    device->ReleaseComputePipeline(
        device->driverData,
        computePipeline);
}

void Refresh_ReleaseGraphicsPipeline(
    Refresh_Device *device,
    Refresh_GraphicsPipeline *graphicsPipeline)
{
    NULL_ASSERT(device);
    device->ReleaseGraphicsPipeline(
        device->driverData,
        graphicsPipeline);
}

/* Command Buffer */

Refresh_CommandBuffer *Refresh_AcquireCommandBuffer(
    Refresh_Device *device)
{
    Refresh_CommandBuffer *commandBuffer;
    CommandBufferCommonHeader *commandBufferHeader;
    NULL_ASSERT(device);

    commandBuffer = device->AcquireCommandBuffer(
        device->driverData);

    if (commandBuffer == NULL) {
        return NULL;
    }

    commandBufferHeader = (CommandBufferCommonHeader *)commandBuffer;
    commandBufferHeader->device = device;
    commandBufferHeader->renderPass.commandBuffer = commandBuffer;
    commandBufferHeader->renderPass.inProgress = SDL_FALSE;
    commandBufferHeader->graphicsPipelineBound = SDL_FALSE;
    commandBufferHeader->computePass.commandBuffer = commandBuffer;
    commandBufferHeader->computePass.inProgress = SDL_FALSE;
    commandBufferHeader->computePipelineBound = SDL_FALSE;
    commandBufferHeader->copyPass.commandBuffer = commandBuffer;
    commandBufferHeader->copyPass.inProgress = SDL_FALSE;
    commandBufferHeader->submitted = SDL_FALSE;

    return commandBuffer;
}

/* Uniforms */

void Refresh_PushVertexUniformData(
    Refresh_CommandBuffer *commandBuffer,
    Uint32 slotIndex,
    const void *data,
    Uint32 dataLengthInBytes)
{
    CHECK_COMMAND_BUFFER
    COMMAND_BUFFER_DEVICE->PushVertexUniformData(
        commandBuffer,
        slotIndex,
        data,
        dataLengthInBytes);
}

void Refresh_PushFragmentUniformData(
    Refresh_CommandBuffer *commandBuffer,
    Uint32 slotIndex,
    const void *data,
    Uint32 dataLengthInBytes)
{
    CHECK_COMMAND_BUFFER
    COMMAND_BUFFER_DEVICE->PushFragmentUniformData(
        commandBuffer,
        slotIndex,
        data,
        dataLengthInBytes);
}

void Refresh_PushComputeUniformData(
    Refresh_CommandBuffer *commandBuffer,
    Uint32 slotIndex,
    const void *data,
    Uint32 dataLengthInBytes)
{
    CHECK_COMMAND_BUFFER
    COMMAND_BUFFER_DEVICE->PushComputeUniformData(
        commandBuffer,
        slotIndex,
        data,
        dataLengthInBytes);
}

/* Render Pass */

Refresh_RenderPass *Refresh_BeginRenderPass(
    Refresh_CommandBuffer *commandBuffer,
    Refresh_ColorAttachmentInfo *colorAttachmentInfos,
    Uint32 colorAttachmentCount,
    Refresh_DepthStencilAttachmentInfo *depthStencilAttachmentInfo)
{
    CommandBufferCommonHeader *commandBufferHeader;

    CHECK_COMMAND_BUFFER_RETURN_NULL
    CHECK_ANY_PASS_IN_PROGRESS
    COMMAND_BUFFER_DEVICE->BeginRenderPass(
        commandBuffer,
        colorAttachmentInfos,
        colorAttachmentCount,
        depthStencilAttachmentInfo);

    commandBufferHeader = (CommandBufferCommonHeader *)commandBuffer;
    commandBufferHeader->renderPass.inProgress = SDL_TRUE;
    return (Refresh_RenderPass *)&(commandBufferHeader->renderPass);
}

void Refresh_BindGraphicsPipeline(
    Refresh_RenderPass *renderPass,
    Refresh_GraphicsPipeline *graphicsPipeline)
{
    CommandBufferCommonHeader *commandBufferHeader;

    NULL_ASSERT(renderPass)
    RENDERPASS_DEVICE->BindGraphicsPipeline(
        RENDERPASS_COMMAND_BUFFER,
        graphicsPipeline);

    commandBufferHeader = (CommandBufferCommonHeader *)RENDERPASS_COMMAND_BUFFER;
    commandBufferHeader->graphicsPipelineBound = SDL_TRUE;
}

void Refresh_SetViewport(
    Refresh_RenderPass *renderPass,
    Refresh_Viewport *viewport)
{
    NULL_ASSERT(renderPass)
    CHECK_RENDERPASS
    RENDERPASS_DEVICE->SetViewport(
        RENDERPASS_COMMAND_BUFFER,
        viewport);
}

void Refresh_SetScissor(
    Refresh_RenderPass *renderPass,
    Refresh_Rect *scissor)
{
    NULL_ASSERT(renderPass)
    CHECK_RENDERPASS
    RENDERPASS_DEVICE->SetScissor(
        RENDERPASS_COMMAND_BUFFER,
        scissor);
}

void Refresh_BindVertexBuffers(
    Refresh_RenderPass *renderPass,
    Uint32 firstBinding,
    Refresh_BufferBinding *pBindings,
    Uint32 bindingCount)
{
    NULL_ASSERT(renderPass)
    CHECK_RENDERPASS
    CHECK_GRAPHICS_PIPELINE_BOUND
    RENDERPASS_DEVICE->BindVertexBuffers(
        RENDERPASS_COMMAND_BUFFER,
        firstBinding,
        pBindings,
        bindingCount);
}

void Refresh_BindIndexBuffer(
    Refresh_RenderPass *renderPass,
    Refresh_BufferBinding *pBinding,
    Refresh_IndexElementSize indexElementSize)
{
    NULL_ASSERT(renderPass)
    CHECK_RENDERPASS
    CHECK_GRAPHICS_PIPELINE_BOUND
    RENDERPASS_DEVICE->BindIndexBuffer(
        RENDERPASS_COMMAND_BUFFER,
        pBinding,
        indexElementSize);
}

void Refresh_BindVertexSamplers(
    Refresh_RenderPass *renderPass,
    Uint32 firstSlot,
    Refresh_TextureSamplerBinding *textureSamplerBindings,
    Uint32 bindingCount)
{
    NULL_ASSERT(renderPass)
    CHECK_RENDERPASS
    CHECK_GRAPHICS_PIPELINE_BOUND
    RENDERPASS_DEVICE->BindVertexSamplers(
        RENDERPASS_COMMAND_BUFFER,
        firstSlot,
        textureSamplerBindings,
        bindingCount);
}

void Refresh_BindVertexStorageTextures(
    Refresh_RenderPass *renderPass,
    Uint32 firstSlot,
    Refresh_TextureSlice *storageTextureSlices,
    Uint32 bindingCount)
{
    NULL_ASSERT(renderPass)
    CHECK_RENDERPASS
    CHECK_GRAPHICS_PIPELINE_BOUND
    RENDERPASS_DEVICE->BindVertexStorageTextures(
        RENDERPASS_COMMAND_BUFFER,
        firstSlot,
        storageTextureSlices,
        bindingCount);
}

void Refresh_BindVertexStorageBuffers(
    Refresh_RenderPass *renderPass,
    Uint32 firstSlot,
    Refresh_Buffer **storageBuffers,
    Uint32 bindingCount)
{
    NULL_ASSERT(renderPass)
    CHECK_RENDERPASS
    CHECK_GRAPHICS_PIPELINE_BOUND
    RENDERPASS_DEVICE->BindVertexStorageBuffers(
        RENDERPASS_COMMAND_BUFFER,
        firstSlot,
        storageBuffers,
        bindingCount);
}

void Refresh_BindFragmentSamplers(
    Refresh_RenderPass *renderPass,
    Uint32 firstSlot,
    Refresh_TextureSamplerBinding *textureSamplerBindings,
    Uint32 bindingCount)
{
    NULL_ASSERT(renderPass)
    CHECK_RENDERPASS
    CHECK_GRAPHICS_PIPELINE_BOUND
    RENDERPASS_DEVICE->BindFragmentSamplers(
        RENDERPASS_COMMAND_BUFFER,
        firstSlot,
        textureSamplerBindings,
        bindingCount);
}

void Refresh_BindFragmentStorageTextures(
    Refresh_RenderPass *renderPass,
    Uint32 firstSlot,
    Refresh_TextureSlice *storageTextureSlices,
    Uint32 bindingCount)
{
    NULL_ASSERT(renderPass)
    CHECK_RENDERPASS
    CHECK_GRAPHICS_PIPELINE_BOUND
    RENDERPASS_DEVICE->BindFragmentStorageTextures(
        RENDERPASS_COMMAND_BUFFER,
        firstSlot,
        storageTextureSlices,
        bindingCount);
}

void Refresh_BindFragmentStorageBuffers(
    Refresh_RenderPass *renderPass,
    Uint32 firstSlot,
    Refresh_Buffer **storageBuffers,
    Uint32 bindingCount)
{
    NULL_ASSERT(renderPass)
    CHECK_RENDERPASS
    CHECK_GRAPHICS_PIPELINE_BOUND
    RENDERPASS_DEVICE->BindFragmentStorageBuffers(
        RENDERPASS_COMMAND_BUFFER,
        firstSlot,
        storageBuffers,
        bindingCount);
}

void Refresh_DrawIndexedPrimitives(
    Refresh_RenderPass *renderPass,
    Uint32 baseVertex,
    Uint32 startIndex,
    Uint32 primitiveCount,
    Uint32 instanceCount)
{
    NULL_ASSERT(renderPass)
    CHECK_RENDERPASS
    CHECK_GRAPHICS_PIPELINE_BOUND
    RENDERPASS_DEVICE->DrawIndexedPrimitives(
        RENDERPASS_COMMAND_BUFFER,
        baseVertex,
        startIndex,
        primitiveCount,
        instanceCount);
}

void Refresh_DrawPrimitives(
    Refresh_RenderPass *renderPass,
    Uint32 vertexStart,
    Uint32 primitiveCount)
{
    NULL_ASSERT(renderPass)
    CHECK_RENDERPASS
    CHECK_GRAPHICS_PIPELINE_BOUND
    RENDERPASS_DEVICE->DrawPrimitives(
        RENDERPASS_COMMAND_BUFFER,
        vertexStart,
        primitiveCount);
}

void Refresh_DrawPrimitivesIndirect(
    Refresh_RenderPass *renderPass,
    Refresh_Buffer *buffer,
    Uint32 offsetInBytes,
    Uint32 drawCount,
    Uint32 stride)
{
    NULL_ASSERT(renderPass)
    CHECK_RENDERPASS
    CHECK_GRAPHICS_PIPELINE_BOUND
    RENDERPASS_DEVICE->DrawPrimitivesIndirect(
        RENDERPASS_COMMAND_BUFFER,
        buffer,
        offsetInBytes,
        drawCount,
        stride);
}

void Refresh_DrawIndexedPrimitivesIndirect(
    Refresh_RenderPass *renderPass,
    Refresh_Buffer *buffer,
    Uint32 offsetInBytes,
    Uint32 drawCount,
    Uint32 stride)
{
    NULL_ASSERT(renderPass)
    CHECK_RENDERPASS
    CHECK_GRAPHICS_PIPELINE_BOUND
    RENDERPASS_DEVICE->DrawIndexedPrimitivesIndirect(
        RENDERPASS_COMMAND_BUFFER,
        buffer,
        offsetInBytes,
        drawCount,
        stride);
}

void Refresh_EndRenderPass(
    Refresh_RenderPass *renderPass)
{
    CommandBufferCommonHeader *commandBufferCommonHeader;

    NULL_ASSERT(renderPass)
    CHECK_RENDERPASS
    RENDERPASS_DEVICE->EndRenderPass(
        RENDERPASS_COMMAND_BUFFER);

    commandBufferCommonHeader = (CommandBufferCommonHeader *)RENDERPASS_COMMAND_BUFFER;
    commandBufferCommonHeader->renderPass.inProgress = SDL_FALSE;
    commandBufferCommonHeader->graphicsPipelineBound = SDL_FALSE;
}

/* Compute Pass */

Refresh_ComputePass *Refresh_BeginComputePass(
    Refresh_CommandBuffer *commandBuffer,
    Refresh_StorageTextureReadWriteBinding *storageTextureBindings,
    Uint32 storageTextureBindingCount,
    Refresh_StorageBufferReadWriteBinding *storageBufferBindings,
    Uint32 storageBufferBindingCount)
{
    CommandBufferCommonHeader *commandBufferHeader;

    CHECK_COMMAND_BUFFER_RETURN_NULL
    CHECK_ANY_PASS_IN_PROGRESS
    COMMAND_BUFFER_DEVICE->BeginComputePass(
        commandBuffer,
        storageTextureBindings,
        storageTextureBindingCount,
        storageBufferBindings,
        storageBufferBindingCount);

    commandBufferHeader = (CommandBufferCommonHeader *)commandBuffer;
    commandBufferHeader->computePass.inProgress = SDL_TRUE;
    return (Refresh_ComputePass *)&(commandBufferHeader->computePass);
}

void Refresh_BindComputePipeline(
    Refresh_ComputePass *computePass,
    Refresh_ComputePipeline *computePipeline)
{
    CommandBufferCommonHeader *commandBufferHeader;

    NULL_ASSERT(computePass)
    CHECK_COMPUTEPASS
    COMPUTEPASS_DEVICE->BindComputePipeline(
        COMPUTEPASS_COMMAND_BUFFER,
        computePipeline);

    commandBufferHeader = (CommandBufferCommonHeader *)COMPUTEPASS_COMMAND_BUFFER;
    commandBufferHeader->computePipelineBound = SDL_TRUE;
}

void Refresh_BindComputeStorageTextures(
    Refresh_ComputePass *computePass,
    Uint32 firstSlot,
    Refresh_TextureSlice *storageTextureSlices,
    Uint32 bindingCount)
{
    NULL_ASSERT(computePass)
    CHECK_COMPUTEPASS
    CHECK_COMPUTE_PIPELINE_BOUND
    COMPUTEPASS_DEVICE->BindComputeStorageTextures(
        COMPUTEPASS_COMMAND_BUFFER,
        firstSlot,
        storageTextureSlices,
        bindingCount);
}

void Refresh_BindComputeStorageBuffers(
    Refresh_ComputePass *computePass,
    Uint32 firstSlot,
    Refresh_Buffer **storageBuffers,
    Uint32 bindingCount)
{
    NULL_ASSERT(computePass)
    CHECK_COMPUTEPASS
    CHECK_COMPUTE_PIPELINE_BOUND
    COMPUTEPASS_DEVICE->BindComputeStorageBuffers(
        COMPUTEPASS_COMMAND_BUFFER,
        firstSlot,
        storageBuffers,
        bindingCount);
}

void Refresh_DispatchCompute(
    Refresh_ComputePass *computePass,
    Uint32 groupCountX,
    Uint32 groupCountY,
    Uint32 groupCountZ)
{
    NULL_ASSERT(computePass)
    CHECK_COMPUTEPASS
    CHECK_COMPUTE_PIPELINE_BOUND
    COMPUTEPASS_DEVICE->DispatchCompute(
        COMPUTEPASS_COMMAND_BUFFER,
        groupCountX,
        groupCountY,
        groupCountZ);
}

void Refresh_EndComputePass(
    Refresh_ComputePass *computePass)
{
    CommandBufferCommonHeader *commandBufferCommonHeader;

    NULL_ASSERT(computePass)
    CHECK_COMPUTEPASS
    COMPUTEPASS_DEVICE->EndComputePass(
        COMPUTEPASS_COMMAND_BUFFER);

    commandBufferCommonHeader = (CommandBufferCommonHeader *)COMPUTEPASS_COMMAND_BUFFER;
    commandBufferCommonHeader->computePass.inProgress = SDL_FALSE;
    commandBufferCommonHeader->computePipelineBound = SDL_FALSE;
}

/* TransferBuffer Data */

void Refresh_MapTransferBuffer(
    Refresh_Device *device,
    Refresh_TransferBuffer *transferBuffer,
    SDL_bool cycle,
    void **ppData)
{
    NULL_ASSERT(device)
    NULL_ASSERT(transferBuffer)
    device->MapTransferBuffer(
        device->driverData,
        transferBuffer,
        cycle,
        ppData);
}

void Refresh_UnmapTransferBuffer(
    Refresh_Device *device,
    Refresh_TransferBuffer *transferBuffer)
{
    NULL_ASSERT(device)
    NULL_ASSERT(transferBuffer)
    device->UnmapTransferBuffer(
        device->driverData,
        transferBuffer);
}

void Refresh_SetTransferData(
    Refresh_Device *device,
    const void *source,
    Refresh_TransferBufferRegion *destination,
    SDL_bool cycle)
{
    NULL_ASSERT(device)
    device->SetTransferData(
        device->driverData,
        source,
        destination,
        cycle);
}

void Refresh_GetTransferData(
    Refresh_Device *device,
    Refresh_TransferBufferRegion *source,
    void *destination)
{
    NULL_ASSERT(device)
    device->GetTransferData(
        device->driverData,
        source,
        destination);
}

/* Copy Pass */

Refresh_CopyPass *Refresh_BeginCopyPass(
    Refresh_CommandBuffer *commandBuffer)
{
    CommandBufferCommonHeader *commandBufferHeader;

    CHECK_COMMAND_BUFFER_RETURN_NULL
    CHECK_ANY_PASS_IN_PROGRESS
    COMMAND_BUFFER_DEVICE->BeginCopyPass(
        commandBuffer);

    commandBufferHeader = (CommandBufferCommonHeader *)commandBuffer;
    commandBufferHeader->copyPass.inProgress = SDL_TRUE;
    return (Refresh_CopyPass *)&(commandBufferHeader->copyPass);
}

void Refresh_UploadToTexture(
    Refresh_CopyPass *copyPass,
    Refresh_TextureTransferInfo *source,
    Refresh_TextureRegion *destination,
    SDL_bool cycle)
{
    NULL_ASSERT(copyPass)
    CHECK_COPYPASS
    COPYPASS_DEVICE->UploadToTexture(
        COPYPASS_COMMAND_BUFFER,
        source,
        destination,
        cycle);
}

void Refresh_UploadToBuffer(
    Refresh_CopyPass *copyPass,
    Refresh_TransferBufferLocation *source,
    Refresh_BufferRegion *destination,
    SDL_bool cycle)
{
    NULL_ASSERT(copyPass)
    COPYPASS_DEVICE->UploadToBuffer(
        COPYPASS_COMMAND_BUFFER,
        source,
        destination,
        cycle);
}

void Refresh_CopyTextureToTexture(
    Refresh_CopyPass *copyPass,
    Refresh_TextureLocation *source,
    Refresh_TextureLocation *destination,
    Uint32 w,
    Uint32 h,
    Uint32 d,
    SDL_bool cycle)
{
    NULL_ASSERT(copyPass)
    COPYPASS_DEVICE->CopyTextureToTexture(
        COPYPASS_COMMAND_BUFFER,
        source,
        destination,
        w,
        h,
        d,
        cycle);
}

void Refresh_CopyBufferToBuffer(
    Refresh_CopyPass *copyPass,
    Refresh_BufferLocation *source,
    Refresh_BufferLocation *destination,
    Uint32 size,
    SDL_bool cycle)
{
    NULL_ASSERT(copyPass)
    COPYPASS_DEVICE->CopyBufferToBuffer(
        COPYPASS_COMMAND_BUFFER,
        source,
        destination,
        size,
        cycle);
}

void Refresh_GenerateMipmaps(
    Refresh_CopyPass *copyPass,
    Refresh_Texture *texture)
{
    NULL_ASSERT(copyPass)
    COPYPASS_DEVICE->GenerateMipmaps(
        COPYPASS_COMMAND_BUFFER,
        texture);
}

void Refresh_DownloadFromTexture(
    Refresh_CopyPass *copyPass,
    Refresh_TextureRegion *source,
    Refresh_TextureTransferInfo *destination)
{
    NULL_ASSERT(copyPass);
    COPYPASS_DEVICE->DownloadFromTexture(
        COPYPASS_COMMAND_BUFFER,
        source,
        destination);
}

void Refresh_DownloadFromBuffer(
    Refresh_CopyPass *copyPass,
    Refresh_BufferRegion *source,
    Refresh_TransferBufferLocation *destination)
{
    NULL_ASSERT(copyPass);
    COPYPASS_DEVICE->DownloadFromBuffer(
        COPYPASS_COMMAND_BUFFER,
        source,
        destination);
}

void Refresh_EndCopyPass(
    Refresh_CopyPass *copyPass)
{
    NULL_ASSERT(copyPass)
    CHECK_COPYPASS
    COPYPASS_DEVICE->EndCopyPass(
        COPYPASS_COMMAND_BUFFER);

    ((CommandBufferCommonHeader *)COPYPASS_COMMAND_BUFFER)->copyPass.inProgress = SDL_FALSE;
}

void Refresh_Blit(
    Refresh_CommandBuffer *commandBuffer,
    Refresh_TextureRegion *source,
    Refresh_TextureRegion *destination,
    Refresh_Filter filterMode,
    SDL_bool cycle)
{
    CHECK_COMMAND_BUFFER
    COMMAND_BUFFER_DEVICE->Blit(
        commandBuffer,
        source,
        destination,
        filterMode,
        cycle);
}

/* Submission/Presentation */

SDL_bool Refresh_SupportsSwapchainComposition(
    Refresh_Device *device,
    SDL_Window *window,
    Refresh_SwapchainComposition swapchainFormat)
{
    if (device == NULL) {
        return 0;
    }
    return device->SupportsSwapchainComposition(
        device->driverData,
        window,
        swapchainFormat);
}

SDL_bool Refresh_SupportsPresentMode(
    Refresh_Device *device,
    SDL_Window *window,
    Refresh_PresentMode presentMode)
{
    if (device == NULL) {
        return 0;
    }
    return device->SupportsPresentMode(
        device->driverData,
        window,
        presentMode);
}

SDL_bool Refresh_ClaimWindow(
    Refresh_Device *device,
    SDL_Window *window,
    Refresh_SwapchainComposition swapchainFormat,
    Refresh_PresentMode presentMode)
{
    if (device == NULL) {
        return 0;
    }
    return device->ClaimWindow(
        device->driverData,
        window,
        swapchainFormat,
        presentMode);
}

void Refresh_UnclaimWindow(
    Refresh_Device *device,
    SDL_Window *window)
{
    NULL_ASSERT(device);
    device->UnclaimWindow(
        device->driverData,
        window);
}

SDL_bool Refresh_SetSwapchainParameters(
    Refresh_Device *device,
    SDL_Window *window,
    Refresh_SwapchainComposition swapchainFormat,
    Refresh_PresentMode presentMode)
{
    NULL_ASSERT(device);
    return device->SetSwapchainParameters(
        device->driverData,
        window,
        swapchainFormat,
        presentMode);
}

Refresh_TextureFormat Refresh_GetSwapchainTextureFormat(
    Refresh_Device *device,
    SDL_Window *window)
{
    if (device == NULL) {
        return 0;
    }
    return device->GetSwapchainTextureFormat(
        device->driverData,
        window);
}

Refresh_Texture *Refresh_AcquireSwapchainTexture(
    Refresh_CommandBuffer *commandBuffer,
    SDL_Window *window,
    Uint32 *pWidth,
    Uint32 *pHeight)
{
    CHECK_COMMAND_BUFFER_RETURN_NULL
    return COMMAND_BUFFER_DEVICE->AcquireSwapchainTexture(
        commandBuffer,
        window,
        pWidth,
        pHeight);
}

void Refresh_Submit(
    Refresh_CommandBuffer *commandBuffer)
{
    CHECK_COMMAND_BUFFER
    CommandBufferCommonHeader *commandBufferHeader = (CommandBufferCommonHeader *)commandBuffer;

    if (
        commandBufferHeader->renderPass.inProgress ||
        commandBufferHeader->computePass.inProgress ||
        commandBufferHeader->copyPass.inProgress) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Cannot submit command buffer while a pass is in progress!");
        return;
    }

    commandBufferHeader->submitted = SDL_TRUE;

    COMMAND_BUFFER_DEVICE->Submit(
        commandBuffer);
}

Refresh_Fence *Refresh_SubmitAndAcquireFence(
    Refresh_CommandBuffer *commandBuffer)
{
    CHECK_COMMAND_BUFFER_RETURN_NULL
    CommandBufferCommonHeader *commandBufferHeader = (CommandBufferCommonHeader *)commandBuffer;

    if (
        commandBufferHeader->renderPass.inProgress ||
        commandBufferHeader->computePass.inProgress ||
        commandBufferHeader->copyPass.inProgress) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Cannot submit command buffer while a pass is in progress!");
        return NULL;
    }

    commandBufferHeader->submitted = SDL_TRUE;

    return COMMAND_BUFFER_DEVICE->SubmitAndAcquireFence(
        commandBuffer);
}

void Refresh_Wait(
    Refresh_Device *device)
{
    NULL_ASSERT(device);
    device->Wait(
        device->driverData);
}

void Refresh_WaitForFences(
    Refresh_Device *device,
    SDL_bool waitAll,
    Refresh_Fence **pFences,
    Uint32 fenceCount)
{
    NULL_ASSERT(device);
    device->WaitForFences(
        device->driverData,
        waitAll,
        pFences,
        fenceCount);
}

SDL_bool Refresh_QueryFence(
    Refresh_Device *device,
    Refresh_Fence *fence)
{
    if (device == NULL) {
        return 0;
    }

    return device->QueryFence(
        device->driverData,
        fence);
}

void Refresh_ReleaseFence(
    Refresh_Device *device,
    Refresh_Fence *fence)
{
    NULL_ASSERT(device);
    device->ReleaseFence(
        device->driverData,
        fence);
}
