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

#ifdef REFRESH_DRIVER_VULKAN
	#define VULKAN_DRIVER &VulkanDriver
#else
	#define VULKAN_DRIVER NULL
#endif

#ifdef REFRESH_DRIVER_D3D11
	#define D3D11_DRIVER &D3D11Driver
#else
	#define D3D11_DRIVER NULL
#endif

#ifdef REFRESH_DRIVER_PS5
	#define PS5_DRIVER &PS5Driver
#else
	#define PS5_DRIVER NULL
#endif

static const Refresh_Driver *backends[] = {
	NULL,
	VULKAN_DRIVER,
	D3D11_DRIVER,
	PS5_DRIVER
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

static Refresh_Backend selectedBackend = REFRESH_BACKEND_INVALID;

Refresh_Backend Refresh_SelectBackend(Refresh_Backend preferredBackend, uint32_t *flags)
{
	uint32_t i;

	if (preferredBackend != REFRESH_BACKEND_DONTCARE)
	{
		if (backends[preferredBackend] == NULL)
		{
			Refresh_LogWarn("Preferred backend was not compiled into this binary! Attempting to fall back!");
		}
		else if (backends[preferredBackend]->PrepareDriver(flags))
		{
			selectedBackend = preferredBackend;
			return selectedBackend;
		}
	}

	/* Iterate until we find an appropriate backend. */

	for (i = 1; i < SDL_arraysize(backends); i += 1)
	{
		if (i != preferredBackend && backends[i] != NULL && backends[i]->PrepareDriver(flags))
		{
			selectedBackend = i;
			return i;
		}
	}

	if (backends[i] == NULL)
	{
		Refresh_LogError("No supported Refresh backend found!");
	}

	selectedBackend = REFRESH_BACKEND_INVALID;
	return REFRESH_BACKEND_INVALID;
}

Refresh_Device* Refresh_CreateDevice(
	uint8_t debugMode
) {
	if (selectedBackend == REFRESH_BACKEND_INVALID)
	{
		Refresh_LogError("Invalid backend selection. Did you call Refresh_SelectBackend?");
		return NULL;
	}

	return backends[selectedBackend]->CreateDevice(
		debugMode
	);
}

void Refresh_DestroyDevice(Refresh_Device *device)
{
	NULL_RETURN(device);
	device->DestroyDevice(device);
}

/* State Creation */

Refresh_ComputePipeline* Refresh_CreateComputePipeline(
	Refresh_Device *device,
	Refresh_ComputeShaderInfo *computeShaderInfo
) {
	NULL_RETURN_NULL(device);
	return device->CreateComputePipeline(
		device->driverData,
		computeShaderInfo
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

Refresh_ShaderModule* Refresh_CreateShaderModule(
	Refresh_Device *device,
	Refresh_ShaderModuleCreateInfo *shaderModuleCreateInfo
) {
	Refresh_Driver_ShaderModuleCreateInfo driverSpecificCreateInfo = { 0, NULL, 0 };
	uint8_t *bytes;
	uint32_t i, size;

	NULL_RETURN_NULL(device);

	/* verify the magic number in the shader blob header */
	bytes = (uint8_t*) shaderModuleCreateInfo->byteCode;
	if (bytes[0] != 'R' || bytes[1] != 'F' || bytes[2] != 'S' || bytes[3] != 'H')
	{
		Refresh_LogError("Cannot parse malformed Refresh shader blob: Incorrect magic number");
		return NULL;
	}

	/* get the type of shader */
	driverSpecificCreateInfo.type = (Refresh_Driver_ShaderType) *((uint32_t*) &bytes[4]);
	if (	driverSpecificCreateInfo.type < 0 ||
		driverSpecificCreateInfo.type > REFRESH_DRIVER_SHADERTYPE_COMPUTE	)
	{
		Refresh_LogError(
			"Cannot parse malformed Refresh shader blob: Unknown shader type (%d)",
			driverSpecificCreateInfo.type
		);
		return NULL;
	}

	/* find the code for the selected backend */
	i = 8;
	while (i < shaderModuleCreateInfo->codeSize)
	{
		size = *((uint32_t*) &bytes[i + 1]);

		if (bytes[i] == (uint8_t) selectedBackend)
		{
			driverSpecificCreateInfo.codeSize = size;
			driverSpecificCreateInfo.byteCode = (uint32_t*) &bytes[i + 1 + sizeof(uint32_t)];
			break;
		}
		else
		{
			/* skip over the backend byte, the blob size, and the blob */
			i += 1 + sizeof(uint32_t) + size;
		}
	}

	/* verify the shader blob supports the selected backend */
	if (driverSpecificCreateInfo.byteCode == NULL)
	{
		Refresh_LogError(
			"Cannot create shader module that does not contain shader code for the selected backend! "
			"Recompile your shader and enable this backend."
		);
		return NULL;
	}

	return device->CreateShaderModule(
		device->driverData,
		&driverSpecificCreateInfo
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

Refresh_GpuBuffer* Refresh_CreateGpuBuffer(
	Refresh_Device *device,
	Refresh_BufferUsageFlags usageFlags,
	uint32_t sizeInBytes
) {
	NULL_RETURN_NULL(device);
	return device->CreateGpuBuffer(
		device->driverData,
		usageFlags,
		sizeInBytes
	);
}

Refresh_TransferBuffer* Refresh_CreateTransferBuffer(
	Refresh_Device *device,
	uint32_t sizeInBytes
) {
	NULL_RETURN_NULL(device);
	return device->CreateTransferBuffer(
		device->driverData,
		sizeInBytes
	);
}

/* Disposal */

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

void Refresh_QueueDestroyGpuBuffer(
	Refresh_Device *device,
	Refresh_GpuBuffer *gpuBuffer
) {
	NULL_RETURN(device);
	device->QueueDestroyGpuBuffer(
		device->driverData,
		gpuBuffer
	);
}

void Refresh_QueueDestroyTransferBuffer(
	Refresh_Device *device,
	Refresh_TransferBuffer *transferBuffer
) {
	NULL_RETURN(device);
	device->QueueDestroyTransferBuffer(
		device->driverData,
		transferBuffer
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

/* Render Pass */

void Refresh_BeginRenderPass(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_ColorAttachmentInfo *colorAttachmentInfos,
	uint32_t colorAttachmentCount,
	Refresh_DepthStencilAttachmentInfo *depthStencilAttachmentInfo
) {
	NULL_RETURN(device);
	device->BeginRenderPass(
		device->driverData,
		commandBuffer,
		colorAttachmentInfos,
		colorAttachmentCount,
		depthStencilAttachmentInfo
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

void Refresh_SetViewport(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Viewport *viewport
) {
	NULL_RETURN(device)
	device->SetViewport(
		device->driverData,
		commandBuffer,
		viewport
	);
}

void Refresh_SetScissor(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Rect *scissor
) {
	NULL_RETURN(device)
	device->SetScissor(
		device->driverData,
		commandBuffer,
		scissor
	);
}

void Refresh_BindVertexBuffers(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	uint32_t firstBinding,
	uint32_t bindingCount,
	Refresh_BufferBinding *pBindings
) {
	NULL_RETURN(device);
	device->BindVertexBuffers(
		device->driverData,
		commandBuffer,
		firstBinding,
		bindingCount,
		pBindings
	);
}

void Refresh_BindIndexBuffer(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_BufferBinding *pBinding,
	Refresh_IndexElementSize indexElementSize
) {
	NULL_RETURN(device);
	device->BindIndexBuffer(
		device->driverData,
		commandBuffer,
		pBinding,
		indexElementSize
	);
}

void Refresh_BindVertexSamplers(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_TextureSamplerBinding *pBindings
) {
	NULL_RETURN(device);
	device->BindVertexSamplers(
		device->driverData,
		commandBuffer,
		pBindings
	);
}

void Refresh_BindFragmentSamplers(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_TextureSamplerBinding *pBindings
) {
	NULL_RETURN(device);
	device->BindFragmentSamplers(
		device->driverData,
		commandBuffer,
		pBindings
	);
}

void Refresh_PushVertexShaderUniforms(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	void *data,
	uint32_t dataLengthInBytes
) {
	NULL_RETURN(device);
	device->PushVertexShaderUniforms(
		device->driverData,
		commandBuffer,
		data,
		dataLengthInBytes
	);
}

void Refresh_PushFragmentShaderUniforms(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	void *data,
	uint32_t dataLengthInBytes
) {
	NULL_RETURN(device);
	device->PushFragmentShaderUniforms(
		device->driverData,
		commandBuffer,
		data,
		dataLengthInBytes
	);
}

void Refresh_DrawInstancedPrimitives(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	uint32_t baseVertex,
	uint32_t startIndex,
	uint32_t primitiveCount,
	uint32_t instanceCount
) {
	NULL_RETURN(device);
	device->DrawInstancedPrimitives(
		device->driverData,
		commandBuffer,
		baseVertex,
		startIndex,
		primitiveCount,
		instanceCount
	);
}

void Refresh_DrawIndexedPrimitives(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	uint32_t baseVertex,
	uint32_t startIndex,
	uint32_t primitiveCount
) {
	NULL_RETURN(device);
	device->DrawIndexedPrimitives(
		device->driverData,
		commandBuffer,
		baseVertex,
		startIndex,
		primitiveCount
	);
}

void Refresh_DrawPrimitives(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	uint32_t vertexStart,
	uint32_t primitiveCount
) {
	NULL_RETURN(device);
	device->DrawPrimitives(
		device->driverData,
		commandBuffer,
		vertexStart,
		primitiveCount
	);
}

void Refresh_DrawPrimitivesIndirect(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_GpuBuffer *gpuBuffer,
	uint32_t offsetInBytes,
	uint32_t drawCount,
	uint32_t stride
) {
	NULL_RETURN(device);
	device->DrawPrimitivesIndirect(
		device->driverData,
		commandBuffer,
		gpuBuffer,
		offsetInBytes,
		drawCount,
		stride
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

/* Compute Pass */

void Refresh_BeginComputePass(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer
) {
	NULL_RETURN(device);
	device->BeginComputePass(
		device->driverData,
		commandBuffer
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
	Refresh_ComputeBufferBinding *pBindings
) {
	NULL_RETURN(device);
	device->BindComputeBuffers(
		device->driverData,
		commandBuffer,
		pBindings
	);
}

void Refresh_BindComputeTextures(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_ComputeTextureBinding *pBindings
) {
	NULL_RETURN(device);
	device->BindComputeTextures(
		device->driverData,
		commandBuffer,
		pBindings
	);
}

void Refresh_PushComputeShaderUniforms(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	void *data,
	uint32_t dataLengthInBytes
) {
	NULL_RETURN(device);
	device->PushComputeShaderUniforms(
		device->driverData,
		commandBuffer,
		data,
		dataLengthInBytes
	);
}

void Refresh_DispatchCompute(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	uint32_t groupCountX,
	uint32_t groupCountY,
	uint32_t groupCountZ
) {
	NULL_RETURN(device);
	device->DispatchCompute(
		device->driverData,
		commandBuffer,
		groupCountX,
		groupCountY,
		groupCountZ
	);
}

void Refresh_EndComputePass(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer
) {
	NULL_RETURN(device);
	device->EndComputePass(
		device->driverData,
		commandBuffer
	);
}

/* TransferBuffer Set/Get */

void Refresh_SetTransferData(
	Refresh_Device *device,
	void* data,
	Refresh_TransferBuffer *transferBuffer,
	Refresh_BufferCopy *copyParams,
	Refresh_TransferOptions transferOption
) {
	NULL_RETURN(device);
	device->SetTransferData(
		device->driverData,
		data,
		transferBuffer,
		copyParams,
		transferOption
	);
}

void Refresh_GetTransferData(
	Refresh_Device *device,
	Refresh_TransferBuffer *transferBuffer,
	void* data,
	Refresh_BufferCopy *copyParams
) {
	NULL_RETURN(device);
	device->GetTransferData(
		device->driverData,
		transferBuffer,
		data,
		copyParams
	);
}

/* Copy Pass */

void Refresh_BeginCopyPass(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer
) {
	NULL_RETURN(device);
	device->BeginCopyPass(
		device->driverData,
		commandBuffer
	);
}

void Refresh_UploadToTexture(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_TransferBuffer *transferBuffer,
	Refresh_TextureRegion *textureRegion,
	Refresh_BufferImageCopy *copyParams,
	Refresh_WriteOptions writeOption
) {
	NULL_RETURN(device);
	device->UploadToTexture(
		device->driverData,
		commandBuffer,
		transferBuffer,
		textureRegion,
		copyParams,
		writeOption
	);
}

void Refresh_UploadToBuffer(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_TransferBuffer *transferBuffer,
	Refresh_GpuBuffer *gpuBuffer,
	Refresh_BufferCopy *copyParams,
	Refresh_WriteOptions writeOption
) {
	NULL_RETURN(device);
	device->UploadToBuffer(
		device->driverData,
		commandBuffer,
		transferBuffer,
		gpuBuffer,
		copyParams,
		writeOption
	);
}

void Refresh_DownloadFromTexture(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_TextureRegion *textureRegion,
	Refresh_TransferBuffer *transferBuffer,
	Refresh_BufferImageCopy *copyParams,
	Refresh_TransferOptions transferOption
) {
	NULL_RETURN(device);
	device->DownloadFromTexture(
		device->driverData,
		commandBuffer,
		textureRegion,
		transferBuffer,
		copyParams,
		transferOption
	);
}

void Refresh_DownloadFromBuffer(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_GpuBuffer *gpuBuffer,
	Refresh_TransferBuffer *transferBuffer,
	Refresh_BufferCopy *copyParams,
	Refresh_TransferOptions transferOption
) {
	NULL_RETURN(device);
	device->DownloadFromBuffer(
		device->driverData,
		commandBuffer,
		gpuBuffer,
		transferBuffer,
		copyParams,
		transferOption
	);
}

void Refresh_CopyTextureToTexture(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_TextureRegion *source,
	Refresh_TextureRegion *destination,
	Refresh_WriteOptions writeOption
) {
	NULL_RETURN(device);
	device->CopyTextureToTexture(
		device->driverData,
		commandBuffer,
		source,
		destination,
		writeOption
	);
}

void Refresh_CopyTextureToBuffer(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_TextureRegion *textureRegion,
	Refresh_GpuBuffer *gpuBuffer,
	Refresh_BufferImageCopy *copyParameters,
	Refresh_WriteOptions writeOption
) {
	NULL_RETURN(device);
	device->CopyTextureToBuffer(
		device->driverData,
		commandBuffer,
		textureRegion,
		gpuBuffer,
		copyParameters,
		writeOption
	);
}

void Refresh_CopyBufferToTexture(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_GpuBuffer *gpuBuffer,
	Refresh_TextureRegion *textureRegion,
	Refresh_BufferImageCopy *copyParams,
	Refresh_WriteOptions writeOption
) {
	NULL_RETURN(device);
	device->CopyBufferToTexture(
		device->driverData,
		commandBuffer,
		gpuBuffer,
		textureRegion,
		copyParams,
		writeOption
	);
}

void Refresh_CopyBufferToBuffer(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_GpuBuffer *source,
	Refresh_GpuBuffer *destination,
	Refresh_BufferCopy *copyParams,
	Refresh_WriteOptions writeOption
) {
	NULL_RETURN(device);
	device->CopyBufferToBuffer(
		device->driverData,
		commandBuffer,
		source,
		destination,
		copyParams,
		writeOption
	);
}

void Refresh_GenerateMipmaps(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Texture *texture
) {
	NULL_RETURN(device);
	device->GenerateMipmaps(
		device->driverData,
		commandBuffer,
		texture
	);
}

void Refresh_EndCopyPass(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer
) {
	NULL_RETURN(device);
	device->EndCopyPass(
		device->driverData,
		commandBuffer
	);
}

/* Submission/Presentation */

uint8_t Refresh_ClaimWindow(
	Refresh_Device *device,
	void *windowHandle,
	Refresh_PresentMode presentMode
) {
	if (device == NULL) { return 0; }
	return device->ClaimWindow(
		device->driverData,
		windowHandle,
		presentMode
	);
}

void Refresh_UnclaimWindow(
	Refresh_Device *device,
	void *windowHandle
) {
	NULL_RETURN(device);
	device->UnclaimWindow(
		device->driverData,
		windowHandle
	);
}

void Refresh_SetSwapchainPresentMode(
	Refresh_Device *device,
	void *windowHandle,
	Refresh_PresentMode presentMode
) {
	NULL_RETURN(device);
	device->SetSwapchainPresentMode(
		device->driverData,
		windowHandle,
		presentMode
	);
}

Refresh_TextureFormat Refresh_GetSwapchainFormat(
	Refresh_Device *device,
	void *windowHandle
) {
	if (device == NULL) { return 0; }
	return device->GetSwapchainFormat(
		device->driverData,
		windowHandle
	);
}

Refresh_CommandBuffer* Refresh_AcquireCommandBuffer(
	Refresh_Device *device
) {
	NULL_RETURN_NULL(device);
	return device->AcquireCommandBuffer(
		device->driverData
	);
}

Refresh_Texture* Refresh_AcquireSwapchainTexture(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer,
	void *windowHandle,
	uint32_t *pWidth,
	uint32_t *pHeight
) {
	NULL_RETURN_NULL(device);
	return device->AcquireSwapchainTexture(
		device->driverData,
		commandBuffer,
		windowHandle,
		pWidth,
		pHeight
	);
}

void Refresh_Submit(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer
) {
	NULL_RETURN(device);
	device->Submit(
		device->driverData,
		commandBuffer
	);
}

Refresh_Fence* Refresh_SubmitAndAcquireFence(
	Refresh_Device *device,
	Refresh_CommandBuffer *commandBuffer
) {
	NULL_RETURN_NULL(device);
	return device->SubmitAndAcquireFence(
		device->driverData,
		commandBuffer
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

void Refresh_WaitForFences(
	Refresh_Device *device,
	uint8_t waitAll,
	uint32_t fenceCount,
	Refresh_Fence **pFences
) {
	NULL_RETURN(device);
	device->WaitForFences(
		device->driverData,
		waitAll,
		fenceCount,
		pFences
	);
}

int Refresh_QueryFence(
	Refresh_Device *device,
	Refresh_Fence *fence
) {
	if (device == NULL) { return 0; }

	return device->QueryFence(
		device->driverData,
		fence
	);
}

void Refresh_ReleaseFence(
	Refresh_Device *device,
	Refresh_Fence *fence
) {
	NULL_RETURN(device);
	device->ReleaseFence(
		device->driverData,
		fence
	);
}

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
