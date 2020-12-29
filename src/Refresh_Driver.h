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

#ifndef REFRESH_DRIVER_H
#define REFRESH_DRIVER_H

#include "Refresh.h"

/* Windows/Visual Studio cruft */
#ifdef _WIN32
#define inline __inline
#endif

/* Logging */

extern void REFRESH_LogInfo(const char *fmt, ...);
extern void REFRESH_LogWarn(const char *fmt, ...);
extern void REFRESH_LogError(const char *fmt, ...);

/* Internal Helper Utilities */

static inline uint32_t Texture_GetFormatSize(
	REFRESH_SurfaceFormat format
) {
	switch (format)
	{
		case REFRESH_SURFACEFORMAT_BC1:
			return 8;
		case REFRESH_SURFACEFORMAT_BC2:
		case REFRESH_SURFACEFORMAT_BC3:
			return 16;
		case REFRESH_SURFACEFORMAT_R8:
			return 1;
		case REFRESH_SURFACEFORMAT_R5G6B5:
		case REFRESH_SURFACEFORMAT_B4G4R4A4:
		case REFRESH_SURFACEFORMAT_A1R5G5B5:
		case REFRESH_SURFACEFORMAT_R16_SFLOAT:
		case REFRESH_SURFACEFORMAT_R8G8_SNORM:
			return 2;
		case REFRESH_SURFACEFORMAT_R8G8B8A8:
		case REFRESH_SURFACEFORMAT_R32_SFLOAT:
		case REFRESH_SURFACEFORMAT_R16G16_SFLOAT:
		case REFRESH_SURFACEFORMAT_R8G8B8A8_SNORM:
		case REFRESH_SURFACEFORMAT_A2R10G10B10:
			return 4;
		case REFRESH_SURFACEFORMAT_R16G16B16A16_SFLOAT:
		case REFRESH_SURFACEFORMAT_R16G16B16A16:
		case REFRESH_SURFACEFORMAT_R32G32_SFLOAT:
			return 8;
		case REFRESH_SURFACEFORMAT_R32G32B32A32_SFLOAT:
			return 16;
		default:
			REFRESH_LogError(
				"Unrecognized SurfaceFormat!"
			);
			return 0;
	}
}

static inline uint32_t PrimitiveVerts(
	REFRESH_PrimitiveType primitiveType,
	uint32_t primitiveCount
) {
	switch (primitiveType)
	{
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
			REFRESH_LogError(
				"Unrecognized primitive type!"
			);
			return 0;
	}
}

static inline uint32_t IndexSize(REFRESH_IndexElementSize size)
{
	return (size == REFRESH_INDEXELEMENTSIZE_16BIT) ? 2 : 4;
}

static inline uint32_t BytesPerRow(
	int32_t width,
	REFRESH_SurfaceFormat format
) {
	uint32_t blocksPerRow = width;

	if (	format == REFRESH_SURFACEFORMAT_BC1 ||
		format == REFRESH_SURFACEFORMAT_BC2 ||
		format == REFRESH_SURFACEFORMAT_BC3	)
	{
		blocksPerRow = (width + 3) / 4;
	}

	return blocksPerRow * Texture_GetFormatSize(format);
}

static inline int32_t BytesPerImage(
	uint32_t width,
	uint32_t height,
	REFRESH_SurfaceFormat format
) {
	uint32_t blocksPerRow = width;
	uint32_t blocksPerColumn = height;

	if (	format == REFRESH_SURFACEFORMAT_BC1 ||
		format == REFRESH_SURFACEFORMAT_BC2 ||
		format == REFRESH_SURFACEFORMAT_BC3	)
	{
		blocksPerRow = (width + 3) / 4;
		blocksPerColumn = (height + 3) / 4;
	}

	return blocksPerRow * blocksPerColumn * Texture_GetFormatSize(format);
}

/* XNA GraphicsDevice Limits */
/* TODO: can these be adjusted for modern low-end? */

#define MAX_TEXTURE_SAMPLERS		16
#define MAX_VERTEXTEXTURE_SAMPLERS	4
#define MAX_TOTAL_SAMPLERS		(MAX_TEXTURE_SAMPLERS + MAX_VERTEXTEXTURE_SAMPLERS)

#define MAX_VERTEX_ATTRIBUTES		16
#define MAX_BOUND_VERTEX_BUFFERS	16

#define MAX_COLOR_TARGET_BINDINGS	4

/* REFRESH_Device Definition */

typedef struct REFRESH_Renderer REFRESH_Renderer;

struct REFRESH_Device
{
	/* Quit */

	void (*DestroyDevice)(REFRESH_Device *device);

	/* Drawing */

	void (*Clear)(
        REFRESH_Renderer *driverData,
        REFRESH_Rect *clearRect,
        REFRESH_ClearOptions options,
        REFRESH_Color *colors,
        uint32_t colorCount,
        float depth,
        int32_t stencil
	);

	void (*DrawInstancedPrimitives)(
        REFRESH_Renderer *driverData,
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
	);

	void (*DrawIndexedPrimitives)(
        REFRESH_Renderer *driverData,
        uint32_t baseVertex,
        uint32_t minVertexIndex,
        uint32_t numVertices,
        uint32_t startIndex,
        uint32_t primitiveCount,
        REFRESH_Buffer *indices,
        REFRESH_IndexElementSize indexElementSize,
        uint32_t vertexParamOffset,
        uint32_t fragmentParamOffset
	);

	void (*DrawPrimitives)(
	    REFRESH_Renderer *driverData,
        uint32_t vertexStart,
        uint32_t primitiveCount,
        uint32_t vertexParamOffset,
        uint32_t fragmentParamOffset
	);

    /* State Creation */

    REFRESH_RenderPass* (*CreateRenderPass)(
        REFRESH_Renderer *driverData,
        REFRESH_RenderPassCreateInfo *renderPassCreateInfo
    );

    REFRESH_ComputePipeline* (*CreateComputePipeline)(
        REFRESH_Renderer *driverData,
        REFRESH_ComputePipelineCreateInfo *pipelineCreateInfo
    );

    REFRESH_GraphicsPipeline* (*CreateGraphicsPipeline)(
        REFRESH_Renderer *driverData,
        REFRESH_GraphicsPipelineCreateInfo *pipelineCreateInfo
    );

    REFRESH_Sampler* (*CreateSampler)(
        REFRESH_Renderer *driverData,
	    REFRESH_SamplerStateCreateInfo *samplerStateCreateInfo
    );

    REFRESH_Framebuffer* (*CreateFramebuffer)(
        REFRESH_Renderer *driverData,
        REFRESH_FramebufferCreateInfo *framebufferCreateInfo
    );

    REFRESH_ShaderModule* (*CreateShaderModule)(
        REFRESH_Renderer *driverData,
	    REFRESH_ShaderModuleCreateInfo *shaderModuleCreateInfo
    );

    REFRESH_Texture* (*CreateTexture2D)(
        REFRESH_Renderer *driverData,
        REFRESH_SurfaceFormat format,
        uint32_t width,
        uint32_t height,
        uint32_t levelCount,
        REFRESH_TextureUsageFlags usageFlags
    );

    REFRESH_Texture* (*CreateTexture3D)(
        REFRESH_Renderer *driverData,
        REFRESH_SurfaceFormat format,
        uint32_t width,
        uint32_t height,
        uint32_t depth,
        uint32_t levelCount,
        REFRESH_TextureUsageFlags usageFlags
    );

    REFRESH_Texture* (*CreateTextureCube)(
        REFRESH_Renderer *driverData,
        REFRESH_SurfaceFormat format,
        uint32_t size,
        uint32_t levelCount,
        REFRESH_TextureUsageFlags usageFlags
    );

    REFRESH_ColorTarget* (*CreateColorTarget)(
        REFRESH_Renderer *driverData,
        REFRESH_SampleCount multisampleCount,
        REFRESH_TextureSlice *textureSlice
    );

    REFRESH_DepthStencilTarget* (*CreateDepthStencilTarget)(
        REFRESH_Renderer *driverData,
        uint32_t width,
        uint32_t height,
        REFRESH_DepthFormat format
    );

    REFRESH_Buffer* (*CreateVertexBuffer)(
        REFRESH_Renderer *driverData,
        uint32_t sizeInBytes
    );

    REFRESH_Buffer* (*CreateIndexBuffer)(
        REFRESH_Renderer *driverData,
        uint32_t sizeInBytes
    );

    /* Setters */

    void(*SetTextureData2D)(
        REFRESH_Renderer *driverData,
        REFRESH_Texture *texture,
        uint32_t x,
        uint32_t y,
        uint32_t w,
        uint32_t h,
        uint32_t level,
        void *data,
        uint32_t dataLengthInBytes
    );

    void(*SetTextureData3D)(
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
    );

    void(*SetTextureDataCube)(
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
    );

    void(*SetTextureDataYUV)(
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
    );

    void(*SetVertexBufferData)(
        REFRESH_Renderer *driverData,
        REFRESH_Buffer *buffer,
        uint32_t offsetInBytes,
        void* data,
        uint32_t elementCount,
        uint32_t vertexStride
    );

    void(*SetIndexBufferData)(
        REFRESH_Renderer *driverData,
        REFRESH_Buffer *buffer,
        uint32_t offsetInBytes,
        void* data,
        uint32_t dataLength
    );

    uint32_t(*PushVertexShaderParams)(
        REFRESH_Renderer *driverData,
        void *data,
        uint32_t elementCount
    );

    uint32_t(*PushFragmentShaderParams)(
        REFRESH_Renderer *driverData,
        void *data,
        uint32_t elementCount
    );

    void(*SetVertexSamplers)(
        REFRESH_Renderer *driverData,
        REFRESH_Texture **pTextures,
        REFRESH_Sampler **pSamplers
    );

    void(*SetFragmentSamplers)(
        REFRESH_Renderer *driverData,
        REFRESH_Texture **pTextures,
        REFRESH_Sampler **pSamplers
    );

    /* Getters */

    void(*GetTextureData2D)(
        REFRESH_Renderer *driverData,
        REFRESH_Texture *texture,
        uint32_t x,
        uint32_t y,
        uint32_t w,
        uint32_t h,
        uint32_t level,
        void* data
    );

    void(*GetTextureDataCube)(
        REFRESH_Renderer *driverData,
        REFRESH_Texture *texture,
        uint32_t x,
        uint32_t y,
        uint32_t w,
        uint32_t h,
        REFRESH_CubeMapFace cubeMapFace,
        uint32_t level,
        void* data
    );

    /* Disposal */

    void(*AddDisposeTexture)(
        REFRESH_Renderer *driverData,
        REFRESH_Texture *texture
    );

    void(*AddDisposeSampler)(
        REFRESH_Renderer *driverData,
        REFRESH_Sampler *sampler
    );

    void(*AddDisposeVertexBuffer)(
        REFRESH_Renderer *driverData,
        REFRESH_Buffer *buffer
    );

    void(*AddDisposeIndexBuffer)(
        REFRESH_Renderer *driverData,
        REFRESH_Buffer *buffer
    );

    void(*AddDisposeColorTarget)(
        REFRESH_Renderer *driverData,
	    REFRESH_ColorTarget *colorTarget
    );

    void(*AddDisposeDepthStencilTarget)(
        REFRESH_Renderer *driverData,
	    REFRESH_DepthStencilTarget *depthStencilTarget
    );

    void(*AddDisposeFramebuffer)(
        REFRESH_Renderer *driverData,
        REFRESH_Framebuffer *frameBuffer
    );

    void(*AddDisposeShaderModule)(
        REFRESH_Renderer *driverData,
        REFRESH_ShaderModule *shaderModule
    );

    void(*AddDisposeRenderPass)(
        REFRESH_Renderer *driverData,
        REFRESH_RenderPass *renderPass
    );

    void(*AddDisposeComputePipeline)(
        REFRESH_Renderer *driverData,
        REFRESH_ComputePipeline *computePipeline
    );

    void(*AddDisposeGraphicsPipeline)(
        REFRESH_Renderer *driverData,
        REFRESH_GraphicsPipeline *graphicsPipeline
    );

    /* Graphics State */

    void(*BeginRenderPass)(
        REFRESH_Renderer *driverData,
        REFRESH_RenderPass *renderPass,
        REFRESH_Framebuffer *framebuffer,
        REFRESH_Rect renderArea,
        REFRESH_Color *pColorClearValues,
        uint32_t colorClearCount,
        REFRESH_DepthStencilValue *depthStencilClearValue
    );

    void(*EndRenderPass)(
        REFRESH_Renderer *driverData
    );

    void(*BindGraphicsPipeline)(
        REFRESH_Renderer *driverData,
        REFRESH_GraphicsPipeline *graphicsPipeline
    );

    void(*BindVertexBuffers)(
        REFRESH_Renderer *driverData,
        uint32_t firstBinding,
        uint32_t bindingCount,
        REFRESH_Buffer **pBuffers,
        uint64_t *pOffsets
    );

    void(*BindIndexBuffer)(
        REFRESH_Renderer *driverData,
        REFRESH_Buffer *buffer,
        uint64_t offset,
        REFRESH_IndexElementSize indexElementSize
    );

    void(*QueuePresent)(
        REFRESH_Renderer *driverData,
        REFRESH_TextureSlice *textureSlice,
        REFRESH_Rect *sourceRectangle,
        REFRESH_Rect *destinationRectangle
    );

    void(*Submit)(
        REFRESH_Renderer *driverData
    );

	/* Opaque pointer for the Driver */
	REFRESH_Renderer *driverData;
};

#define ASSIGN_DRIVER_FUNC(func, name) \
	result->func = name##_##func;
#define ASSIGN_DRIVER(name) \
	ASSIGN_DRIVER_FUNC(DestroyDevice, name) \
	ASSIGN_DRIVER_FUNC(Clear, name) \
	ASSIGN_DRIVER_FUNC(DrawIndexedPrimitives, name) \
	ASSIGN_DRIVER_FUNC(DrawInstancedPrimitives, name) \
	ASSIGN_DRIVER_FUNC(DrawPrimitives, name) \
    ASSIGN_DRIVER_FUNC(CreateRenderPass, name) \
    ASSIGN_DRIVER_FUNC(CreateComputePipeline, name) \
    ASSIGN_DRIVER_FUNC(CreateGraphicsPipeline, name) \
    ASSIGN_DRIVER_FUNC(CreateSampler, name) \
    ASSIGN_DRIVER_FUNC(CreateFramebuffer, name) \
    ASSIGN_DRIVER_FUNC(CreateShaderModule, name) \
    ASSIGN_DRIVER_FUNC(CreateTexture2D, name) \
    ASSIGN_DRIVER_FUNC(CreateTexture3D, name) \
    ASSIGN_DRIVER_FUNC(CreateTextureCube, name) \
    ASSIGN_DRIVER_FUNC(CreateColorTarget, name) \
    ASSIGN_DRIVER_FUNC(CreateDepthStencilTarget, name) \
    ASSIGN_DRIVER_FUNC(CreateVertexBuffer, name) \
    ASSIGN_DRIVER_FUNC(CreateIndexBuffer, name) \
    ASSIGN_DRIVER_FUNC(SetTextureData2D, name) \
    ASSIGN_DRIVER_FUNC(SetTextureData3D, name) \
    ASSIGN_DRIVER_FUNC(SetTextureDataCube, name) \
    ASSIGN_DRIVER_FUNC(SetTextureDataYUV, name) \
    ASSIGN_DRIVER_FUNC(SetVertexBufferData, name) \
    ASSIGN_DRIVER_FUNC(SetIndexBufferData, name) \
    ASSIGN_DRIVER_FUNC(PushVertexShaderParams, name) \
    ASSIGN_DRIVER_FUNC(PushFragmentShaderParams, name) \
    ASSIGN_DRIVER_FUNC(SetVertexSamplers, name) \
    ASSIGN_DRIVER_FUNC(SetFragmentSamplers, name) \
    ASSIGN_DRIVER_FUNC(GetTextureData2D, name) \
    ASSIGN_DRIVER_FUNC(GetTextureDataCube, name) \
    ASSIGN_DRIVER_FUNC(AddDisposeTexture, name) \
    ASSIGN_DRIVER_FUNC(AddDisposeSampler, name) \
    ASSIGN_DRIVER_FUNC(AddDisposeVertexBuffer, name) \
    ASSIGN_DRIVER_FUNC(AddDisposeIndexBuffer, name) \
    ASSIGN_DRIVER_FUNC(AddDisposeColorTarget, name) \
    ASSIGN_DRIVER_FUNC(AddDisposeDepthStencilTarget, name) \
    ASSIGN_DRIVER_FUNC(AddDisposeFramebuffer, name) \
    ASSIGN_DRIVER_FUNC(AddDisposeShaderModule, name) \
    ASSIGN_DRIVER_FUNC(AddDisposeRenderPass, name) \
    ASSIGN_DRIVER_FUNC(AddDisposeComputePipeline, name) \
    ASSIGN_DRIVER_FUNC(AddDisposeGraphicsPipeline, name) \
    ASSIGN_DRIVER_FUNC(BeginRenderPass, name) \
    ASSIGN_DRIVER_FUNC(EndRenderPass, name) \
    ASSIGN_DRIVER_FUNC(BindGraphicsPipeline, name) \
    ASSIGN_DRIVER_FUNC(BindVertexBuffers, name) \
    ASSIGN_DRIVER_FUNC(BindIndexBuffer, name) \
    ASSIGN_DRIVER_FUNC(QueuePresent, name) \
    ASSIGN_DRIVER_FUNC(Submit, name)

typedef struct REFRESH_Driver
{
	const char *Name;
	REFRESH_Device* (*CreateDevice)(
		REFRESH_PresentationParameters *presentationParameters,
        uint8_t debugMode
	);
} REFRESH_Driver;

extern REFRESH_Driver VulkanDriver;

#endif /* REFRESH_DRIVER_H */

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
