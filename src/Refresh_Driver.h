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

extern void Refresh_LogInfo(const char *fmt, ...);
extern void Refresh_LogWarn(const char *fmt, ...);
extern void Refresh_LogError(const char *fmt, ...);

/* Internal Helper Utilities */

static inline uint32_t Texture_GetFormatSize(
	Refresh_TextureFormat format
) {
	switch (format)
	{
		case REFRESH_TEXTUREFORMAT_BC1:
			return 8;
		case REFRESH_TEXTUREFORMAT_BC2:
		case REFRESH_TEXTUREFORMAT_BC3:
			return 16;
		case REFRESH_TEXTUREFORMAT_R8:
			return 1;
		case REFRESH_TEXTUREFORMAT_R5G6B5:
		case REFRESH_TEXTUREFORMAT_B4G4R4A4:
		case REFRESH_TEXTUREFORMAT_A1R5G5B5:
		case REFRESH_TEXTUREFORMAT_R16_SFLOAT:
		case REFRESH_TEXTUREFORMAT_R8G8_SNORM:
			return 2;
		case REFRESH_TEXTUREFORMAT_R8G8B8A8:
		case REFRESH_TEXTUREFORMAT_R32_SFLOAT:
		case REFRESH_TEXTUREFORMAT_R16G16_SFLOAT:
		case REFRESH_TEXTUREFORMAT_R8G8B8A8_SNORM:
		case REFRESH_TEXTUREFORMAT_A2R10G10B10:
			return 4;
		case REFRESH_TEXTUREFORMAT_R16G16B16A16_SFLOAT:
		case REFRESH_TEXTUREFORMAT_R16G16B16A16:
		case REFRESH_TEXTUREFORMAT_R32G32_SFLOAT:
			return 8;
		case REFRESH_TEXTUREFORMAT_R32G32B32A32_SFLOAT:
			return 16;
		default:
			Refresh_LogError(
				"Unrecognized SurfaceFormat!"
			);
			return 0;
	}
}

static inline uint32_t PrimitiveVerts(
	Refresh_PrimitiveType primitiveType,
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
			Refresh_LogError(
				"Unrecognized primitive type!"
			);
			return 0;
	}
}

static inline uint32_t IndexSize(Refresh_IndexElementSize size)
{
	return (size == REFRESH_INDEXELEMENTSIZE_16BIT) ? 2 : 4;
}

static inline uint32_t BytesPerRow(
	int32_t width,
	Refresh_TextureFormat format
) {
	uint32_t blocksPerRow = width;

	if (	format == REFRESH_TEXTUREFORMAT_BC1 ||
		format == REFRESH_TEXTUREFORMAT_BC2 ||
		format == REFRESH_TEXTUREFORMAT_BC3	)
	{
		blocksPerRow = (width + 3) / 4;
	}

	return blocksPerRow * Texture_GetFormatSize(format);
}

static inline int32_t BytesPerImage(
	uint32_t width,
	uint32_t height,
	Refresh_TextureFormat format
) {
	uint32_t blocksPerRow = width;
	uint32_t blocksPerColumn = height;

	if (	format == REFRESH_TEXTUREFORMAT_BC1 ||
		format == REFRESH_TEXTUREFORMAT_BC2 ||
		format == REFRESH_TEXTUREFORMAT_BC3	)
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

#define MAX_BUFFER_BINDINGS			16

#define MAX_COLOR_TARGET_BINDINGS	4
#define MAX_PRESENT_COUNT			16

/* Refresh_Device Definition */

typedef struct Refresh_Renderer Refresh_Renderer;

struct Refresh_Device
{
	/* Quit */

	void (*DestroyDevice)(Refresh_Device *device);

	/* Drawing */

	void (*Clear)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		Refresh_Rect *clearRect,
		Refresh_ClearOptions options,
		Refresh_Vec4 *colors,
		uint32_t colorCount,
		Refresh_DepthStencilValue depthStencil
	);

	void (*DrawInstancedPrimitives)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		uint32_t baseVertex,
		uint32_t startIndex,
		uint32_t primitiveCount,
		uint32_t instanceCount,
		uint32_t vertexParamOffset,
		uint32_t fragmentParamOffset
	);

	void (*DrawIndexedPrimitives)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		uint32_t baseVertex,
		uint32_t startIndex,
		uint32_t primitiveCount,
		uint32_t vertexParamOffset,
		uint32_t fragmentParamOffset
	);

	void (*DrawPrimitives)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		uint32_t vertexStart,
		uint32_t primitiveCount,
		uint32_t vertexParamOffset,
		uint32_t fragmentParamOffset
	);

	void (*DispatchCompute)(
		Refresh_Renderer *device,
		Refresh_CommandBuffer *commandBuffer,
		uint32_t groupCountX,
		uint32_t groupCountY,
		uint32_t groupCountZ,
		uint32_t computeParamOffset
	);

	/* State Creation */

	Refresh_ComputePipeline* (*CreateComputePipeline)(
		Refresh_Renderer *driverData,
		Refresh_ComputePipelineCreateInfo *pipelineCreateInfo
	);

	Refresh_GraphicsPipeline* (*CreateGraphicsPipeline)(
		Refresh_Renderer *driverData,
		Refresh_GraphicsPipelineCreateInfo *pipelineCreateInfo
	);

	Refresh_Sampler* (*CreateSampler)(
		Refresh_Renderer *driverData,
		Refresh_SamplerStateCreateInfo *samplerStateCreateInfo
	);

	Refresh_ShaderModule* (*CreateShaderModule)(
		Refresh_Renderer *driverData,
		Refresh_ShaderModuleCreateInfo *shaderModuleCreateInfo
	);

	Refresh_Texture* (*CreateTexture)(
		Refresh_Renderer *driverData,
		Refresh_TextureCreateInfo *textureCreateInfo
	);

	Refresh_Buffer* (*CreateBuffer)(
		Refresh_Renderer *driverData,
		Refresh_BufferUsageFlags usageFlags,
		uint32_t sizeInBytes
	);

	/* Setters */

	void(*SetTextureData)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		Refresh_TextureSlice *textureSlice,
		void *data,
		uint32_t dataLengthInBytes
	);

	void(*SetTextureDataYUV)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer* commandBuffer,
		Refresh_Texture *y,
		Refresh_Texture *u,
		Refresh_Texture *v,
		uint32_t yWidth,
		uint32_t yHeight,
		uint32_t uvWidth,
		uint32_t uvHeight,
		void* data,
		uint32_t dataLength
	);

	void(*CopyTextureToTexture)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		Refresh_TextureSlice *sourceTextureSlice,
		Refresh_TextureSlice *destinationTextureSlice,
		Refresh_Filter filter
	);

	void(*CopyTextureToBuffer)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		Refresh_TextureSlice *textureSlice,
		Refresh_Buffer *buffer
	);

	void(*SetBufferData)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		Refresh_Buffer *buffer,
		uint32_t offsetInBytes,
		void* data,
		uint32_t dataLength
	);

	uint32_t(*PushVertexShaderUniforms)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		void *data,
		uint32_t dataLengthInBytes
	);

	uint32_t(*PushFragmentShaderUniforms)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		void *data,
		uint32_t dataLengthInBytes
	);

	uint32_t (*PushComputeShaderUniforms)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		void *data,
		uint32_t dataLengthInBytes
	);

	void(*BindVertexSamplers)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		Refresh_Texture **pTextures,
		Refresh_Sampler **pSamplers
	);

	void(*BindFragmentSamplers)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		Refresh_Texture **pTextures,
		Refresh_Sampler **pSamplers
	);

	/* Getters */

	void(*GetBufferData)(
		Refresh_Renderer *driverData,
		Refresh_Buffer *buffer,
		void *data,
		uint32_t dataLengthInBytes
	);

	/* Disposal */

	void(*QueueDestroyTexture)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		Refresh_Texture *texture
	);

	void(*QueueDestroySampler)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		Refresh_Sampler *sampler
	);

	void(*QueueDestroyBuffer)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		Refresh_Buffer *buffer
	);

	void(*QueueDestroyShaderModule)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		Refresh_ShaderModule *shaderModule
	);

	void(*QueueDestroyComputePipeline)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		Refresh_ComputePipeline *computePipeline
	);

	void(*QueueDestroyGraphicsPipeline)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		Refresh_GraphicsPipeline *graphicsPipeline
	);

	/* Graphics State */

	void(*BeginRenderPass)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		Refresh_Rect *renderArea,
		Refresh_ColorAttachmentInfo *colorAttachmentInfos,
		uint32_t colorAttachmentCount,
		Refresh_DepthStencilAttachmentInfo *depthStencilAttachmentInfo
	);

	void(*EndRenderPass)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer
	);

	void(*BindGraphicsPipeline)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		Refresh_GraphicsPipeline *graphicsPipeline
	);

	void(*BindVertexBuffers)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		uint32_t firstBinding,
		uint32_t bindingCount,
		Refresh_Buffer **pBuffers,
		uint64_t *pOffsets
	);

	void(*BindIndexBuffer)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		Refresh_Buffer *buffer,
		uint64_t offset,
		Refresh_IndexElementSize indexElementSize
	);

	void(*BindComputePipeline)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		Refresh_ComputePipeline *computePipeline
	);

	void(*BindComputeBuffers)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		Refresh_Buffer **pBuffers
	);

	void(*BindComputeTextures)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		Refresh_Texture **pTextures
	);

	Refresh_CommandBuffer* (*AcquireCommandBuffer)(
		Refresh_Renderer *driverData,
		uint8_t fixed
	);

	Refresh_Texture* (*AcquireSwapchainTexture)(
		Refresh_Renderer *driverData,
		Refresh_CommandBuffer *commandBuffer,
		void *windowHandle
	);

	Refresh_TextureFormat (*GetSwapchainFormat)(
		Refresh_Renderer *driverData,
		void *windowHandle
	);

	void(*Submit)(
		Refresh_Renderer *driverData,
		uint32_t commandBufferCount,
		Refresh_CommandBuffer **pCommandBuffers
	);

	void(*Wait)(
		Refresh_Renderer *driverData
	);

	/* Opaque pointer for the Driver */
	Refresh_Renderer *driverData;
};

#define ASSIGN_DRIVER_FUNC(func, name) \
	result->func = name##_##func;
#define ASSIGN_DRIVER(name) \
	ASSIGN_DRIVER_FUNC(DestroyDevice, name) \
	ASSIGN_DRIVER_FUNC(Clear, name) \
	ASSIGN_DRIVER_FUNC(DrawIndexedPrimitives, name) \
	ASSIGN_DRIVER_FUNC(DrawInstancedPrimitives, name) \
	ASSIGN_DRIVER_FUNC(DrawPrimitives, name) \
	ASSIGN_DRIVER_FUNC(DispatchCompute, name) \
	ASSIGN_DRIVER_FUNC(CreateComputePipeline, name) \
	ASSIGN_DRIVER_FUNC(CreateGraphicsPipeline, name) \
	ASSIGN_DRIVER_FUNC(CreateSampler, name) \
	ASSIGN_DRIVER_FUNC(CreateShaderModule, name) \
	ASSIGN_DRIVER_FUNC(CreateTexture, name) \
	ASSIGN_DRIVER_FUNC(CreateBuffer, name) \
	ASSIGN_DRIVER_FUNC(SetTextureData, name) \
	ASSIGN_DRIVER_FUNC(SetTextureDataYUV, name) \
	ASSIGN_DRIVER_FUNC(CopyTextureToTexture, name) \
	ASSIGN_DRIVER_FUNC(CopyTextureToBuffer, name) \
	ASSIGN_DRIVER_FUNC(SetBufferData, name) \
	ASSIGN_DRIVER_FUNC(PushVertexShaderUniforms, name) \
	ASSIGN_DRIVER_FUNC(PushFragmentShaderUniforms, name) \
	ASSIGN_DRIVER_FUNC(PushComputeShaderUniforms, name) \
	ASSIGN_DRIVER_FUNC(BindVertexSamplers, name) \
	ASSIGN_DRIVER_FUNC(BindFragmentSamplers, name) \
	ASSIGN_DRIVER_FUNC(GetBufferData, name) \
	ASSIGN_DRIVER_FUNC(QueueDestroyTexture, name) \
	ASSIGN_DRIVER_FUNC(QueueDestroySampler, name) \
	ASSIGN_DRIVER_FUNC(QueueDestroyBuffer, name) \
	ASSIGN_DRIVER_FUNC(QueueDestroyShaderModule, name) \
	ASSIGN_DRIVER_FUNC(QueueDestroyComputePipeline, name) \
	ASSIGN_DRIVER_FUNC(QueueDestroyGraphicsPipeline, name) \
	ASSIGN_DRIVER_FUNC(BeginRenderPass, name) \
	ASSIGN_DRIVER_FUNC(EndRenderPass, name) \
	ASSIGN_DRIVER_FUNC(BindGraphicsPipeline, name) \
	ASSIGN_DRIVER_FUNC(BindVertexBuffers, name) \
	ASSIGN_DRIVER_FUNC(BindIndexBuffer, name) \
	ASSIGN_DRIVER_FUNC(BindComputePipeline, name) \
	ASSIGN_DRIVER_FUNC(BindComputeBuffers, name) \
	ASSIGN_DRIVER_FUNC(BindComputeTextures, name) \
	ASSIGN_DRIVER_FUNC(AcquireCommandBuffer, name) \
	ASSIGN_DRIVER_FUNC(AcquireSwapchainTexture, name) \
	ASSIGN_DRIVER_FUNC(GetSwapchainFormat, name) \
	ASSIGN_DRIVER_FUNC(Submit, name) \
	ASSIGN_DRIVER_FUNC(Wait, name)

typedef struct Refresh_Driver
{
	const char *Name;
	Refresh_Device* (*CreateDevice)(
		Refresh_PresentationParameters *presentationParameters,
		uint8_t debugMode
	);
} Refresh_Driver;

extern Refresh_Driver VulkanDriver;

#endif /* REFRESH_DRIVER_H */

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
