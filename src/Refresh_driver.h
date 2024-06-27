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

#ifndef REFRESH_DRIVER_H
#define REFRESH_DRIVER_H

#include "Refresh.h"

/* Common Struct */

typedef struct Pass
{
    Refresh_CommandBuffer *commandBuffer;
    SDL_bool inProgress;
} Pass;

typedef struct CommandBufferCommonHeader
{
    Refresh_Device *device;
    Pass renderPass;
    SDL_bool graphicsPipelineBound;
    Pass computePass;
    SDL_bool computePipelineBound;
    Pass copyPass;
    SDL_bool submitted;
} CommandBufferCommonHeader;

/* Internal Helper Utilities */

static inline Sint32 Texture_GetBlockSize(
    Refresh_TextureFormat format)
{
    switch (format) {
    case REFRESH_TEXTUREFORMAT_BC1:
    case REFRESH_TEXTUREFORMAT_BC2:
    case REFRESH_TEXTUREFORMAT_BC3:
    case REFRESH_TEXTUREFORMAT_BC7:
    case REFRESH_TEXTUREFORMAT_BC3_SRGB:
    case REFRESH_TEXTUREFORMAT_BC7_SRGB:
        return 4;
    case REFRESH_TEXTUREFORMAT_R8:
    case REFRESH_TEXTUREFORMAT_A8:
    case REFRESH_TEXTUREFORMAT_R8_UINT:
    case REFRESH_TEXTUREFORMAT_R5G6B5:
    case REFRESH_TEXTUREFORMAT_B4G4R4A4:
    case REFRESH_TEXTUREFORMAT_A1R5G5B5:
    case REFRESH_TEXTUREFORMAT_R16_SFLOAT:
    case REFRESH_TEXTUREFORMAT_R8G8_SNORM:
    case REFRESH_TEXTUREFORMAT_R8G8_UINT:
    case REFRESH_TEXTUREFORMAT_R16_UINT:
    case REFRESH_TEXTUREFORMAT_R8G8B8A8:
    case REFRESH_TEXTUREFORMAT_R32_SFLOAT:
    case REFRESH_TEXTUREFORMAT_R16G16_SFLOAT:
    case REFRESH_TEXTUREFORMAT_R8G8B8A8_SNORM:
    case REFRESH_TEXTUREFORMAT_R8G8B8A8_SRGB:
    case REFRESH_TEXTUREFORMAT_B8G8R8A8_SRGB:
    case REFRESH_TEXTUREFORMAT_A2R10G10B10:
    case REFRESH_TEXTUREFORMAT_R8G8B8A8_UINT:
    case REFRESH_TEXTUREFORMAT_R16G16_UINT:
    case REFRESH_TEXTUREFORMAT_R16G16B16A16_SFLOAT:
    case REFRESH_TEXTUREFORMAT_R16G16B16A16:
    case REFRESH_TEXTUREFORMAT_R32G32_SFLOAT:
    case REFRESH_TEXTUREFORMAT_R16G16B16A16_UINT:
    case REFRESH_TEXTUREFORMAT_R32G32B32A32_SFLOAT:
        return 1;
    default:
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Unrecognized TextureFormat!");
        return 0;
    }
}

static inline SDL_bool IsDepthFormat(
    Refresh_TextureFormat format)
{
    switch (format) {
    case REFRESH_TEXTUREFORMAT_D16_UNORM:
    case REFRESH_TEXTUREFORMAT_D24_UNORM:
    case REFRESH_TEXTUREFORMAT_D32_SFLOAT:
    case REFRESH_TEXTUREFORMAT_D24_UNORM_S8_UINT:
    case REFRESH_TEXTUREFORMAT_D32_SFLOAT_S8_UINT:
        return SDL_TRUE;

    default:
        return SDL_FALSE;
    }
}

static inline SDL_bool IsStencilFormat(
    Refresh_TextureFormat format)
{
    switch (format) {
    case REFRESH_TEXTUREFORMAT_D24_UNORM_S8_UINT:
    case REFRESH_TEXTUREFORMAT_D32_SFLOAT_S8_UINT:
        return SDL_TRUE;

    default:
        return SDL_FALSE;
    }
}

static inline Uint32 PrimitiveVerts(
    Refresh_PrimitiveType primitiveType,
    Uint32 primitiveCount)
{
    switch (primitiveType) {
    case REFRESH_PRIMITIVETYPE_TRIANGLELIST:
        return primitiveCount * 3;
    case REFRESH_PRIMITIVETYPE_TRIANGLESTRIP:
        return primitiveCount + 2;
    case REFRESH_PRIMITIVETYPE_LINELIST:
        return primitiveCount * 2;
    case REFRESH_PRIMITIVETYPE_LINESTRIP:
        return primitiveCount + 1;
    case REFRESH_PRIMITIVETYPE_POINTLIST:
        return primitiveCount;
    default:
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Unrecognized primitive type!");
        return 0;
    }
}

static inline Uint32 IndexSize(Refresh_IndexElementSize size)
{
    return (size == REFRESH_INDEXELEMENTSIZE_16BIT) ? 2 : 4;
}

static inline Uint32 BytesPerRow(
    Sint32 width,
    Refresh_TextureFormat format)
{
    Uint32 blocksPerRow = width;

    if (format == REFRESH_TEXTUREFORMAT_BC1 ||
        format == REFRESH_TEXTUREFORMAT_BC2 ||
        format == REFRESH_TEXTUREFORMAT_BC3 ||
        format == REFRESH_TEXTUREFORMAT_BC7) {
        blocksPerRow = (width + 3) / 4;
    }

    return blocksPerRow * Refresh_TextureFormatTexelBlockSize(format);
}

static inline Sint32 BytesPerImage(
    Uint32 width,
    Uint32 height,
    Refresh_TextureFormat format)
{
    Uint32 blocksPerRow = width;
    Uint32 blocksPerColumn = height;

    if (format == REFRESH_TEXTUREFORMAT_BC1 ||
        format == REFRESH_TEXTUREFORMAT_BC2 ||
        format == REFRESH_TEXTUREFORMAT_BC3 ||
        format == REFRESH_TEXTUREFORMAT_BC7) {
        blocksPerRow = (width + 3) / 4;
        blocksPerColumn = (height + 3) / 4;
    }

    return blocksPerRow * blocksPerColumn * Refresh_TextureFormatTexelBlockSize(format);
}

/* GraphicsDevice Limits */

#define MAX_TEXTURE_SAMPLERS_PER_STAGE 16
#define MAX_STORAGE_TEXTURES_PER_STAGE 8
#define MAX_STORAGE_BUFFERS_PER_STAGE  8
#define MAX_UNIFORM_BUFFERS_PER_STAGE  4
#define UNIFORM_BUFFER_SIZE            32768
#define MAX_BUFFER_BINDINGS            16
#define MAX_COLOR_TARGET_BINDINGS      4
#define MAX_PRESENT_COUNT              16
#define MAX_FRAMES_IN_FLIGHT           3

/* Refresh_Device Definition */

typedef struct Refresh_Renderer Refresh_Renderer;

struct Refresh_Device
{
    /* Quit */

    void (*DestroyDevice)(Refresh_Device *device);

    /* State Creation */

    Refresh_ComputePipeline *(*CreateComputePipeline)(
        Refresh_Renderer *driverData,
        Refresh_ComputePipelineCreateInfo *pipelineCreateInfo);

    Refresh_GraphicsPipeline *(*CreateGraphicsPipeline)(
        Refresh_Renderer *driverData,
        Refresh_GraphicsPipelineCreateInfo *pipelineCreateInfo);

    Refresh_Sampler *(*CreateSampler)(
        Refresh_Renderer *driverData,
        Refresh_SamplerCreateInfo *samplerCreateInfo);

    Refresh_Shader *(*CreateShader)(
        Refresh_Renderer *driverData,
        Refresh_ShaderCreateInfo *shaderCreateInfo);

    Refresh_Texture *(*CreateTexture)(
        Refresh_Renderer *driverData,
        Refresh_TextureCreateInfo *textureCreateInfo);

    Refresh_Buffer *(*CreateBuffer)(
        Refresh_Renderer *driverData,
        Refresh_BufferUsageFlags usageFlags,
        Uint32 sizeInBytes);

    Refresh_TransferBuffer *(*CreateTransferBuffer)(
        Refresh_Renderer *driverData,
        Refresh_TransferBufferUsage usage,
        Uint32 sizeInBytes);

    /* Debug Naming */

    void (*SetBufferName)(
        Refresh_Renderer *driverData,
        Refresh_Buffer *buffer,
        const char *text);

    void (*SetTextureName)(
        Refresh_Renderer *driverData,
        Refresh_Texture *texture,
        const char *text);

    void (*InsertDebugLabel)(
        Refresh_CommandBuffer *commandBuffer,
        const char *text);

    void (*PushDebugGroup)(
        Refresh_CommandBuffer *commandBuffer,
        const char *name);

    void (*PopDebugGroup)(
        Refresh_CommandBuffer *commandBuffer);

    /* Disposal */

    void (*ReleaseTexture)(
        Refresh_Renderer *driverData,
        Refresh_Texture *texture);

    void (*ReleaseSampler)(
        Refresh_Renderer *driverData,
        Refresh_Sampler *sampler);

    void (*ReleaseBuffer)(
        Refresh_Renderer *driverData,
        Refresh_Buffer *buffer);

    void (*ReleaseTransferBuffer)(
        Refresh_Renderer *driverData,
        Refresh_TransferBuffer *transferBuffer);

    void (*ReleaseShader)(
        Refresh_Renderer *driverData,
        Refresh_Shader *shader);

    void (*ReleaseComputePipeline)(
        Refresh_Renderer *driverData,
        Refresh_ComputePipeline *computePipeline);

    void (*ReleaseGraphicsPipeline)(
        Refresh_Renderer *driverData,
        Refresh_GraphicsPipeline *graphicsPipeline);

    /* Render Pass */

    void (*BeginRenderPass)(
        Refresh_CommandBuffer *commandBuffer,
        Refresh_ColorAttachmentInfo *colorAttachmentInfos,
        Uint32 colorAttachmentCount,
        Refresh_DepthStencilAttachmentInfo *depthStencilAttachmentInfo);

    void (*BindGraphicsPipeline)(
        Refresh_CommandBuffer *commandBuffer,
        Refresh_GraphicsPipeline *graphicsPipeline);

    void (*SetViewport)(
        Refresh_CommandBuffer *commandBuffer,
        Refresh_Viewport *viewport);

    void (*SetScissor)(
        Refresh_CommandBuffer *commandBuffer,
        Refresh_Rect *scissor);

    void (*BindVertexBuffers)(
        Refresh_CommandBuffer *commandBuffer,
        Uint32 firstBinding,
        Refresh_BufferBinding *pBindings,
        Uint32 bindingCount);

    void (*BindIndexBuffer)(
        Refresh_CommandBuffer *commandBuffer,
        Refresh_BufferBinding *pBinding,
        Refresh_IndexElementSize indexElementSize);

    void (*BindVertexSamplers)(
        Refresh_CommandBuffer *commandBuffer,
        Uint32 firstSlot,
        Refresh_TextureSamplerBinding *textureSamplerBindings,
        Uint32 bindingCount);

    void (*BindVertexStorageTextures)(
        Refresh_CommandBuffer *commandBuffer,
        Uint32 firstSlot,
        Refresh_TextureSlice *storageTextureSlices,
        Uint32 bindingCount);

    void (*BindVertexStorageBuffers)(
        Refresh_CommandBuffer *commandBuffer,
        Uint32 firstSlot,
        Refresh_Buffer **storageBuffers,
        Uint32 bindingCount);

    void (*BindFragmentSamplers)(
        Refresh_CommandBuffer *commandBuffer,
        Uint32 firstSlot,
        Refresh_TextureSamplerBinding *textureSamplerBindings,
        Uint32 bindingCount);

    void (*BindFragmentStorageTextures)(
        Refresh_CommandBuffer *commandBuffer,
        Uint32 firstSlot,
        Refresh_TextureSlice *storageTextureSlices,
        Uint32 bindingCount);

    void (*BindFragmentStorageBuffers)(
        Refresh_CommandBuffer *commandBuffer,
        Uint32 firstSlot,
        Refresh_Buffer **storageBuffers,
        Uint32 bindingCount);

    void (*PushVertexUniformData)(
        Refresh_CommandBuffer *commandBuffer,
        Uint32 slotIndex,
        const void *data,
        Uint32 dataLengthInBytes);

    void (*PushFragmentUniformData)(
        Refresh_CommandBuffer *commandBuffer,
        Uint32 slotIndex,
        const void *data,
        Uint32 dataLengthInBytes);

    void (*DrawIndexedPrimitives)(
        Refresh_CommandBuffer *commandBuffer,
        Uint32 baseVertex,
        Uint32 startIndex,
        Uint32 primitiveCount,
        Uint32 instanceCount);

    void (*DrawPrimitives)(
        Refresh_CommandBuffer *commandBuffer,
        Uint32 vertexStart,
        Uint32 primitiveCount);

    void (*DrawPrimitivesIndirect)(
        Refresh_CommandBuffer *commandBuffer,
        Refresh_Buffer *buffer,
        Uint32 offsetInBytes,
        Uint32 drawCount,
        Uint32 stride);

    void (*DrawIndexedPrimitivesIndirect)(
        Refresh_CommandBuffer *commandBuffer,
        Refresh_Buffer *buffer,
        Uint32 offsetInBytes,
        Uint32 drawCount,
        Uint32 stride);

    void (*EndRenderPass)(
        Refresh_CommandBuffer *commandBuffer);

    /* Compute Pass */

    void (*BeginComputePass)(
        Refresh_CommandBuffer *commandBuffer,
        Refresh_StorageTextureReadWriteBinding *storageTextureBindings,
        Uint32 storageTextureBindingCount,
        Refresh_StorageBufferReadWriteBinding *storageBufferBindings,
        Uint32 storageBufferBindingCount);

    void (*BindComputePipeline)(
        Refresh_CommandBuffer *commandBuffer,
        Refresh_ComputePipeline *computePipeline);

    void (*BindComputeStorageTextures)(
        Refresh_CommandBuffer *commandBuffer,
        Uint32 firstSlot,
        Refresh_TextureSlice *storageTextureSlices,
        Uint32 bindingCount);

    void (*BindComputeStorageBuffers)(
        Refresh_CommandBuffer *commandBuffer,
        Uint32 firstSlot,
        Refresh_Buffer **storageBuffers,
        Uint32 bindingCount);

    void (*PushComputeUniformData)(
        Refresh_CommandBuffer *commandBuffer,
        Uint32 slotIndex,
        const void *data,
        Uint32 dataLengthInBytes);

    void (*DispatchCompute)(
        Refresh_CommandBuffer *commandBuffer,
        Uint32 groupCountX,
        Uint32 groupCountY,
        Uint32 groupCountZ);

    void (*EndComputePass)(
        Refresh_CommandBuffer *commandBuffer);

    /* TransferBuffer Data */

    void (*MapTransferBuffer)(
        Refresh_Renderer *device,
        Refresh_TransferBuffer *transferBuffer,
        SDL_bool cycle,
        void **ppData);

    void (*UnmapTransferBuffer)(
        Refresh_Renderer *device,
        Refresh_TransferBuffer *transferBuffer);

    void (*SetTransferData)(
        Refresh_Renderer *driverData,
        const void *source,
        Refresh_TransferBufferRegion *destination,
        SDL_bool cycle);

    void (*GetTransferData)(
        Refresh_Renderer *driverData,
        Refresh_TransferBufferRegion *source,
        void *destination);

    /* Copy Pass */

    void (*BeginCopyPass)(
        Refresh_CommandBuffer *commandBuffer);

    void (*UploadToTexture)(
        Refresh_CommandBuffer *commandBuffer,
        Refresh_TextureTransferInfo *source,
        Refresh_TextureRegion *destination,
        SDL_bool cycle);

    void (*UploadToBuffer)(
        Refresh_CommandBuffer *commandBuffer,
        Refresh_TransferBufferLocation *source,
        Refresh_BufferRegion *destination,
        SDL_bool cycle);

    void (*CopyTextureToTexture)(
        Refresh_CommandBuffer *commandBuffer,
        Refresh_TextureLocation *source,
        Refresh_TextureLocation *destination,
        Uint32 w,
        Uint32 h,
        Uint32 d,
        SDL_bool cycle);

    void (*CopyBufferToBuffer)(
        Refresh_CommandBuffer *commandBuffer,
        Refresh_BufferLocation *source,
        Refresh_BufferLocation *destination,
        Uint32 size,
        SDL_bool cycle);

    void (*GenerateMipmaps)(
        Refresh_CommandBuffer *commandBuffer,
        Refresh_Texture *texture);

    void (*DownloadFromTexture)(
        Refresh_CommandBuffer *commandBuffer,
        Refresh_TextureRegion *source,
        Refresh_TextureTransferInfo *destination);

    void (*DownloadFromBuffer)(
        Refresh_CommandBuffer *commandBuffer,
        Refresh_BufferRegion *source,
        Refresh_TransferBufferLocation *destination);

    void (*EndCopyPass)(
        Refresh_CommandBuffer *commandBuffer);

    void (*Blit)(
        Refresh_CommandBuffer *commandBuffer,
        Refresh_TextureRegion *source,
        Refresh_TextureRegion *destination,
        Refresh_Filter filterMode,
        SDL_bool cycle);

    /* Submission/Presentation */

    SDL_bool (*SupportsSwapchainComposition)(
        Refresh_Renderer *driverData,
        SDL_Window *window,
        Refresh_SwapchainComposition swapchainComposition);

    SDL_bool (*SupportsPresentMode)(
        Refresh_Renderer *driverData,
        SDL_Window *window,
        Refresh_PresentMode presentMode);

    SDL_bool (*ClaimWindow)(
        Refresh_Renderer *driverData,
        SDL_Window *window,
        Refresh_SwapchainComposition swapchainComposition,
        Refresh_PresentMode presentMode);

    void (*UnclaimWindow)(
        Refresh_Renderer *driverData,
        SDL_Window *window);

    SDL_bool (*SetSwapchainParameters)(
        Refresh_Renderer *driverData,
        SDL_Window *window,
        Refresh_SwapchainComposition swapchainComposition,
        Refresh_PresentMode presentMode);

    Refresh_TextureFormat (*GetSwapchainTextureFormat)(
        Refresh_Renderer *driverData,
        SDL_Window *window);

    Refresh_CommandBuffer *(*AcquireCommandBuffer)(
        Refresh_Renderer *driverData);

    Refresh_Texture *(*AcquireSwapchainTexture)(
        Refresh_CommandBuffer *commandBuffer,
        SDL_Window *window,
        Uint32 *pWidth,
        Uint32 *pHeight);

    void (*Submit)(
        Refresh_CommandBuffer *commandBuffer);

    Refresh_Fence *(*SubmitAndAcquireFence)(
        Refresh_CommandBuffer *commandBuffer);

    void (*Wait)(
        Refresh_Renderer *driverData);

    void (*WaitForFences)(
        Refresh_Renderer *driverData,
        SDL_bool waitAll,
        Refresh_Fence **pFences,
        Uint32 fenceCount);

    SDL_bool (*QueryFence)(
        Refresh_Renderer *driverData,
        Refresh_Fence *fence);

    void (*ReleaseFence)(
        Refresh_Renderer *driverData,
        Refresh_Fence *fence);

    /* Feature Queries */

    SDL_bool (*IsTextureFormatSupported)(
        Refresh_Renderer *driverData,
        Refresh_TextureFormat format,
        Refresh_TextureType type,
        Refresh_TextureUsageFlags usage);

    Refresh_SampleCount (*GetBestSampleCount)(
        Refresh_Renderer *driverData,
        Refresh_TextureFormat format,
        Refresh_SampleCount desiredSampleCount);

    /* Opaque pointer for the Driver */
    Refresh_Renderer *driverData;

    /* Store this for Refresh_GetBackend() */
    Refresh_Backend backend;
};

#define ASSIGN_DRIVER_FUNC(func, name) \
    result->func = name##_##func;
#define ASSIGN_DRIVER(name)                                 \
    ASSIGN_DRIVER_FUNC(DestroyDevice, name)                 \
    ASSIGN_DRIVER_FUNC(CreateComputePipeline, name)         \
    ASSIGN_DRIVER_FUNC(CreateGraphicsPipeline, name)        \
    ASSIGN_DRIVER_FUNC(CreateSampler, name)                 \
    ASSIGN_DRIVER_FUNC(CreateShader, name)                  \
    ASSIGN_DRIVER_FUNC(CreateTexture, name)                 \
    ASSIGN_DRIVER_FUNC(CreateBuffer, name)                  \
    ASSIGN_DRIVER_FUNC(CreateTransferBuffer, name)          \
    ASSIGN_DRIVER_FUNC(SetBufferName, name)                 \
    ASSIGN_DRIVER_FUNC(SetTextureName, name)                \
    ASSIGN_DRIVER_FUNC(InsertDebugLabel, name)              \
    ASSIGN_DRIVER_FUNC(PushDebugGroup, name)                \
    ASSIGN_DRIVER_FUNC(PopDebugGroup, name)                 \
    ASSIGN_DRIVER_FUNC(ReleaseTexture, name)                \
    ASSIGN_DRIVER_FUNC(ReleaseSampler, name)                \
    ASSIGN_DRIVER_FUNC(ReleaseBuffer, name)                 \
    ASSIGN_DRIVER_FUNC(ReleaseTransferBuffer, name)         \
    ASSIGN_DRIVER_FUNC(ReleaseShader, name)                 \
    ASSIGN_DRIVER_FUNC(ReleaseComputePipeline, name)        \
    ASSIGN_DRIVER_FUNC(ReleaseGraphicsPipeline, name)       \
    ASSIGN_DRIVER_FUNC(BeginRenderPass, name)               \
    ASSIGN_DRIVER_FUNC(BindGraphicsPipeline, name)          \
    ASSIGN_DRIVER_FUNC(SetViewport, name)                   \
    ASSIGN_DRIVER_FUNC(SetScissor, name)                    \
    ASSIGN_DRIVER_FUNC(BindVertexBuffers, name)             \
    ASSIGN_DRIVER_FUNC(BindIndexBuffer, name)               \
    ASSIGN_DRIVER_FUNC(BindVertexSamplers, name)            \
    ASSIGN_DRIVER_FUNC(BindVertexStorageTextures, name)     \
    ASSIGN_DRIVER_FUNC(BindVertexStorageBuffers, name)      \
    ASSIGN_DRIVER_FUNC(BindFragmentSamplers, name)          \
    ASSIGN_DRIVER_FUNC(BindFragmentStorageTextures, name)   \
    ASSIGN_DRIVER_FUNC(BindFragmentStorageBuffers, name)    \
    ASSIGN_DRIVER_FUNC(PushVertexUniformData, name)         \
    ASSIGN_DRIVER_FUNC(PushFragmentUniformData, name)       \
    ASSIGN_DRIVER_FUNC(DrawIndexedPrimitives, name)         \
    ASSIGN_DRIVER_FUNC(DrawPrimitives, name)                \
    ASSIGN_DRIVER_FUNC(DrawPrimitivesIndirect, name)        \
    ASSIGN_DRIVER_FUNC(DrawIndexedPrimitivesIndirect, name) \
    ASSIGN_DRIVER_FUNC(EndRenderPass, name)                 \
    ASSIGN_DRIVER_FUNC(BeginComputePass, name)              \
    ASSIGN_DRIVER_FUNC(BindComputePipeline, name)           \
    ASSIGN_DRIVER_FUNC(BindComputeStorageTextures, name)    \
    ASSIGN_DRIVER_FUNC(BindComputeStorageBuffers, name)     \
    ASSIGN_DRIVER_FUNC(PushComputeUniformData, name)        \
    ASSIGN_DRIVER_FUNC(DispatchCompute, name)               \
    ASSIGN_DRIVER_FUNC(EndComputePass, name)                \
    ASSIGN_DRIVER_FUNC(MapTransferBuffer, name)             \
    ASSIGN_DRIVER_FUNC(UnmapTransferBuffer, name)           \
    ASSIGN_DRIVER_FUNC(SetTransferData, name)               \
    ASSIGN_DRIVER_FUNC(GetTransferData, name)               \
    ASSIGN_DRIVER_FUNC(BeginCopyPass, name)                 \
    ASSIGN_DRIVER_FUNC(UploadToTexture, name)               \
    ASSIGN_DRIVER_FUNC(UploadToBuffer, name)                \
    ASSIGN_DRIVER_FUNC(DownloadFromTexture, name)           \
    ASSIGN_DRIVER_FUNC(DownloadFromBuffer, name)            \
    ASSIGN_DRIVER_FUNC(CopyTextureToTexture, name)          \
    ASSIGN_DRIVER_FUNC(CopyBufferToBuffer, name)            \
    ASSIGN_DRIVER_FUNC(GenerateMipmaps, name)               \
    ASSIGN_DRIVER_FUNC(EndCopyPass, name)                   \
    ASSIGN_DRIVER_FUNC(Blit, name)                          \
    ASSIGN_DRIVER_FUNC(SupportsSwapchainComposition, name)  \
    ASSIGN_DRIVER_FUNC(SupportsPresentMode, name)           \
    ASSIGN_DRIVER_FUNC(ClaimWindow, name)                   \
    ASSIGN_DRIVER_FUNC(UnclaimWindow, name)                 \
    ASSIGN_DRIVER_FUNC(SetSwapchainParameters, name)        \
    ASSIGN_DRIVER_FUNC(GetSwapchainTextureFormat, name)     \
    ASSIGN_DRIVER_FUNC(AcquireCommandBuffer, name)          \
    ASSIGN_DRIVER_FUNC(AcquireSwapchainTexture, name)       \
    ASSIGN_DRIVER_FUNC(Submit, name)                        \
    ASSIGN_DRIVER_FUNC(SubmitAndAcquireFence, name)         \
    ASSIGN_DRIVER_FUNC(Wait, name)                          \
    ASSIGN_DRIVER_FUNC(WaitForFences, name)                 \
    ASSIGN_DRIVER_FUNC(QueryFence, name)                    \
    ASSIGN_DRIVER_FUNC(ReleaseFence, name)                  \
    ASSIGN_DRIVER_FUNC(IsTextureFormatSupported, name)      \
    ASSIGN_DRIVER_FUNC(GetBestSampleCount, name)

typedef struct Refresh_Driver
{
    const char *Name;
    const Refresh_Backend backendflag;
    SDL_bool (*PrepareDriver)();
    Refresh_Device *(*CreateDevice)(SDL_bool debugMode);
} Refresh_Driver;

extern Refresh_Driver VulkanDriver;
extern Refresh_Driver D3D11Driver;
extern Refresh_Driver MetalDriver;
extern Refresh_Driver PS5Driver;

#endif /* REFRESH_DRIVER_H */
