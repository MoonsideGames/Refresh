﻿/* Refresh - XNA-inspired 3D Graphics Library with modern capabilities
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

#if REFRESH_DRIVER_D3D11

#define D3D11_NO_HELPERS
#define CINTERFACE
#define COBJMACROS

#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <d3dcompiler.h>

#include "Refresh_Driver.h"
#include "Refresh_Driver_D3D11_cdefines.h"

#include <SDL.h>
#include <SDL_syswm.h>

 /* Defines */

#define D3D11_DLL "d3d11.dll"
#define DXGI_DLL "dxgi.dll"
#define DXGIDEBUG_DLL "dxgidebug.dll"
#define D3D11_CREATE_DEVICE_FUNC "D3D11CreateDevice"
#define D3DCOMPILE_FUNC "D3DCompile"
#define CREATE_DXGI_FACTORY1_FUNC "CreateDXGIFactory1"
#define DXGI_GET_DEBUG_INTERFACE_FUNC "DXGIGetDebugInterface"
#define WINDOW_DATA "Refresh_D3D11WindowData"
#define UBO_BUFFER_SIZE 16000 /* 16KB */

#define NOT_IMPLEMENTED SDL_assert(0 && "Not implemented!");

/* Macros */

#define ERROR_CHECK(msg) \
	if (FAILED(res)) \
	{ \
		D3D11_INTERNAL_LogError(renderer->device, msg, res); \
	}

#define ERROR_CHECK_RETURN(msg, ret) \
	if (FAILED(res)) \
	{ \
		D3D11_INTERNAL_LogError(renderer->device, msg, res); \
		return ret; \
	}

#define EXPAND_ELEMENTS_IF_NEEDED(arr, initialValue, type)	\
	if (arr->count == arr->capacity)			\
	{							\
		if (arr->capacity == 0)				\
		{						\
			arr->capacity = initialValue;		\
		}						\
		else						\
		{						\
			arr->capacity *= 2;			\
		}						\
		arr->elements = (type*) SDL_realloc(		\
			arr->elements,				\
			arr->capacity * sizeof(type)		\
		);						\
	}

/* D3DCompile signature */

typedef HRESULT(WINAPI *PFN_D3DCOMPILE)(
	LPCVOID pSrcData,
	SIZE_T SrcDataSize,
	LPCSTR pSourceName,
	const D3D_SHADER_MACRO* pDefines,
	ID3DInclude* pInclude,
	LPCSTR pEntrypoint,
	LPCSTR pTarget,
	UINT Flags1,
	UINT Flags2,
	ID3DBlob **ppCode,
	ID3DBlob **ppErrorMsgs
);

/* Forward Declarations */

static void D3D11_Wait(Refresh_Renderer *driverData);
static void D3D11_UnclaimWindow(
	Refresh_Renderer * driverData,
	void *windowHandle
);

 /* Conversions */

static DXGI_FORMAT RefreshToD3D11_TextureFormat[] =
{
	DXGI_FORMAT_R8G8B8A8_UNORM,	/* R8G8B8A8 */
	DXGI_FORMAT_B8G8R8A8_UNORM,	/* B8G8R8A8 */
	DXGI_FORMAT_B5G6R5_UNORM,	/* R5G6B5 */ /* FIXME: Swizzle? */
	DXGI_FORMAT_B5G5R5A1_UNORM,	/* A1R5G5B5 */ /* FIXME: Swizzle? */
	DXGI_FORMAT_B4G4R4A4_UNORM,	/* B4G4R4A4 */
	DXGI_FORMAT_R10G10B10A2_UNORM,	/* A2R10G10B10 */
	DXGI_FORMAT_R16G16_UNORM,	/* R16G16 */
	DXGI_FORMAT_R16G16B16A16_UNORM,	/* R16G16B16A16 */
	DXGI_FORMAT_R8_UNORM,		/* R8 */
	DXGI_FORMAT_BC1_UNORM,		/* BC1 */
	DXGI_FORMAT_BC2_UNORM,		/* BC2 */
	DXGI_FORMAT_BC3_UNORM,		/* BC3 */
	DXGI_FORMAT_BC7_UNORM,		/* BC7 */
	DXGI_FORMAT_R8G8_SNORM,		/* R8G8_SNORM */
	DXGI_FORMAT_R8G8B8A8_SNORM,	/* R8G8B8A8_SNORM */
	DXGI_FORMAT_R16_FLOAT,		/* R16_SFLOAT */
	DXGI_FORMAT_R16G16_FLOAT,	/* R16G16_SFLOAT */
	DXGI_FORMAT_R16G16B16A16_FLOAT,	/* R16G16B16A16_SFLOAT */
	DXGI_FORMAT_R32_FLOAT,		/* R32_SFLOAT */
	DXGI_FORMAT_R32G32_FLOAT,	/* R32G32_SFLOAT */
	DXGI_FORMAT_R32G32B32A32_FLOAT,	/* R32G32B32A32_SFLOAT */
	DXGI_FORMAT_R8_UINT,		/* R8_UINT */
	DXGI_FORMAT_R8G8_UINT,		/* R8G8_UINT */
	DXGI_FORMAT_R8G8B8A8_UINT,	/* R8G8B8A8_UINT */
	DXGI_FORMAT_R16_UINT,		/* R16_UINT */
	DXGI_FORMAT_R16G16_UINT,	/* R16G16_UINT */
	DXGI_FORMAT_R16G16B16A16_UINT,	/* R16G16B16A16_UINT */
	DXGI_FORMAT_D16_UNORM,		/* D16_UNORM */
	DXGI_FORMAT_D32_FLOAT,		/* D32_SFLOAT */
	DXGI_FORMAT_D24_UNORM_S8_UINT,	/* D16_UNORM_S8_UINT */
	DXGI_FORMAT_D32_FLOAT_S8X24_UINT/* D32_SFLOAT_S8_UINT */
};

static DXGI_FORMAT RefreshToD3D11_VertexFormat[] =
{
	DXGI_FORMAT_R32_UINT,		/* UINT */
	DXGI_FORMAT_R32_FLOAT,		/* FLOAT */
	DXGI_FORMAT_R32G32_FLOAT,	/* VECTOR2 */
	DXGI_FORMAT_R32G32B32_FLOAT,	/* VECTOR3 */
	DXGI_FORMAT_R32G32B32A32_FLOAT,	/* VECTOR4 */
	DXGI_FORMAT_R8G8B8A8_UNORM,	/* COLOR */
	DXGI_FORMAT_R8G8B8A8_UINT,	/* BYTE4 */
	DXGI_FORMAT_R16G16_SINT,	/* SHORT2 */
	DXGI_FORMAT_R16G16B16A16_SINT,	/* SHORT4 */
	DXGI_FORMAT_R16G16_SNORM,	/* NORMALIZEDSHORT2 */
	DXGI_FORMAT_R16G16B16A16_SNORM,	/* NORMALIZEDSHORT4 */
	DXGI_FORMAT_R16G16_FLOAT,	/* HALFVECTOR2 */
	DXGI_FORMAT_R16G16B16A16_FLOAT	/* HALFVECTOR4 */
};

static uint32_t RefreshToD3D11_SampleCount[] =
{
	1,	/* REFRESH_SAMPLECOUNT_1 */
	2,	/* REFRESH_SAMPLECOUNT_2 */
	4,	/* REFRESH_SAMPLECOUNT_4 */
	8	/* REFRESH_SAMPLECOUNT_8 */
};

static DXGI_FORMAT RefreshToD3D11_IndexType[] =
{
	DXGI_FORMAT_R16_UINT,	/* 16BIT */
	DXGI_FORMAT_R32_UINT	/* 32BIT */
};

static D3D11_PRIMITIVE_TOPOLOGY RefreshToD3D11_PrimitiveType[] =
{
	D3D_PRIMITIVE_TOPOLOGY_POINTLIST,	/* POINTLIST */
	D3D_PRIMITIVE_TOPOLOGY_LINELIST,	/* LINELIST */
	D3D_PRIMITIVE_TOPOLOGY_LINESTRIP,	/* LINESTRIP */
	D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,	/* TRIANGLELIST */
	D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP	/* TRIANGLESTRIP */
};

static D3D11_FILL_MODE RefreshToD3D11_PolygonMode[] =
{
	D3D11_FILL_SOLID,	/* FILL */
	D3D11_FILL_WIREFRAME,	/* LINE */
};

static D3D11_CULL_MODE RefreshToD3D11_CullMode[] =
{
	D3D11_CULL_NONE,	/* NONE */
	D3D11_CULL_FRONT,	/* FRONT */
	D3D11_CULL_BACK		/* BACK */
};

static D3D11_BLEND RefreshToD3D11_BlendFactor[] =
{
	D3D11_BLEND_ZERO,		/* ZERO */
	D3D11_BLEND_ONE,		/* ONE */
	D3D11_BLEND_SRC_COLOR,		/* SRC_COLOR */
	D3D11_BLEND_INV_SRC_COLOR,	/* ONE_MINUS_SRC_COLOR */
	D3D11_BLEND_DEST_COLOR,		/* DST_COLOR */
	D3D11_BLEND_INV_DEST_COLOR,	/* ONE_MINUS_DST_COLOR */
	D3D11_BLEND_SRC_ALPHA,		/* SRC_ALPHA */
	D3D11_BLEND_INV_SRC_ALPHA,	/* ONE_MINUS_SRC_ALPHA */
	D3D11_BLEND_DEST_ALPHA,		/* DST_ALPHA */
	D3D11_BLEND_INV_DEST_ALPHA,	/* ONE_MINUS_DST_ALPHA */
	D3D11_BLEND_BLEND_FACTOR,	/* CONSTANT_COLOR */
	D3D11_BLEND_INV_BLEND_FACTOR,	/* ONE_MINUS_CONSTANT_COLOR */
	D3D11_BLEND_SRC_ALPHA_SAT,	/* SRC_ALPHA_SATURATE */
};

static D3D11_BLEND_OP RefreshToD3D11_BlendOp[] =
{
	D3D11_BLEND_OP_ADD,		/* ADD */
	D3D11_BLEND_OP_SUBTRACT,	/* SUBTRACT */
	D3D11_BLEND_OP_REV_SUBTRACT,	/* REVERSE_SUBTRACT */
	D3D11_BLEND_OP_MIN,		/* MIN */
	D3D11_BLEND_OP_MAX		/* MAX */
};

static D3D11_COMPARISON_FUNC RefreshToD3D11_CompareOp[] =
{
	D3D11_COMPARISON_NEVER,		/* NEVER */
	D3D11_COMPARISON_LESS,		/* LESS */
	D3D11_COMPARISON_EQUAL,		/* EQUAL */
	D3D11_COMPARISON_LESS_EQUAL,	/* LESS_OR_EQUAL */
	D3D11_COMPARISON_GREATER,	/* GREATER */
	D3D11_COMPARISON_NOT_EQUAL,	/* NOT_EQUAL */
	D3D11_COMPARISON_GREATER_EQUAL,	/* GREATER_OR_EQUAL */
	D3D11_COMPARISON_ALWAYS		/* ALWAYS */
};

static D3D11_STENCIL_OP RefreshToD3D11_StencilOp[] =
{
	D3D11_STENCIL_OP_KEEP,		/* KEEP */
	D3D11_STENCIL_OP_ZERO,		/* ZERO */
	D3D11_STENCIL_OP_REPLACE,	/* REPLACE */
	D3D11_STENCIL_OP_INCR_SAT,	/* INCREMENT_AND_CLAMP */
	D3D11_STENCIL_OP_DECR_SAT,	/* DECREMENT_AND_CLAMP */
	D3D11_STENCIL_OP_INVERT,	/* INVERT */
	D3D11_STENCIL_OP_INCR,		/* INCREMENT_AND_WRAP */
	D3D11_STENCIL_OP_DECR		/* DECREMENT_AND_WRAP */
};

static D3D11_INPUT_CLASSIFICATION RefreshToD3D11_VertexInputRate[] =
{
	D3D11_INPUT_PER_VERTEX_DATA,	/* VERTEX */
	D3D11_INPUT_PER_INSTANCE_DATA	/* INSTANCE */
};

static D3D11_TEXTURE_ADDRESS_MODE RefreshToD3D11_SamplerAddressMode[] =
{
	D3D11_TEXTURE_ADDRESS_WRAP,	/* REPEAT */
	D3D11_TEXTURE_ADDRESS_MIRROR,	/* MIRRORED_REPEAT */
	D3D11_TEXTURE_ADDRESS_CLAMP,	/* CLAMP_TO_EDGE */
	D3D11_TEXTURE_ADDRESS_BORDER	/* CLAMP_TO_BORDER */
};

static void RefreshToD3D11_BorderColor(
	Refresh_SamplerStateCreateInfo *createInfo,
	D3D11_SAMPLER_DESC *desc
) {
	switch (createInfo->borderColor)
	{
	case REFRESH_BORDERCOLOR_FLOAT_OPAQUE_BLACK:
	case REFRESH_BORDERCOLOR_INT_OPAQUE_BLACK:
		desc->BorderColor[0] = 0.0f;
		desc->BorderColor[1] = 0.0f;
		desc->BorderColor[2] = 0.0f;
		desc->BorderColor[3] = 1.0f;
		break;

	case REFRESH_BORDERCOLOR_FLOAT_OPAQUE_WHITE:
	case REFRESH_BORDERCOLOR_INT_OPAQUE_WHITE:
		desc->BorderColor[0] = 1.0f;
		desc->BorderColor[1] = 1.0f;
		desc->BorderColor[2] = 1.0f;
		desc->BorderColor[3] = 1.0f;
		break;

	case REFRESH_BORDERCOLOR_FLOAT_TRANSPARENT_BLACK:
	case REFRESH_BORDERCOLOR_INT_TRANSPARENT_BLACK:
		desc->BorderColor[0] = 0.0f;
		desc->BorderColor[1] = 0.0f;
		desc->BorderColor[2] = 0.0f;
		desc->BorderColor[3] = 0.0f;
		break;
	}
}

static D3D11_FILTER RefreshToD3D11_Filter(Refresh_SamplerStateCreateInfo *createInfo)
{
	if (createInfo->minFilter == REFRESH_FILTER_LINEAR)
	{
		if (createInfo->magFilter == REFRESH_FILTER_LINEAR)
		{
			if (createInfo->mipmapMode == REFRESH_SAMPLERMIPMAPMODE_LINEAR)
			{
				return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			}
			else
			{
				return D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
			}
		}
		else
		{
			if (createInfo->mipmapMode == REFRESH_SAMPLERMIPMAPMODE_LINEAR)
			{
				return D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
			}
			else
			{
				return D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT;
			}
		}
	}
	else
	{
		if (createInfo->magFilter == REFRESH_FILTER_LINEAR)
		{
			if (createInfo->mipmapMode == REFRESH_SAMPLERMIPMAPMODE_LINEAR)
			{
				return D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR;
			}
			else
			{
				return D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
			}
		}
		else
		{
			if (createInfo->mipmapMode == REFRESH_SAMPLERMIPMAPMODE_LINEAR)
			{
				return D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
			}
			else
			{
				return D3D11_FILTER_MIN_MAG_MIP_POINT;
			}
		}
	}
}

/* Structs */

typedef struct D3D11TargetView
{
	ID3D11View *view; /* ID3D11RenderTargetView* or ID3D11DepthStencilView* */
	uint32_t level;
	uint32_t depth;
	uint32_t layer;
} D3D11TargetView;

typedef struct D3D11Texture
{
	/* D3D Handles */
	ID3D11Resource *handle; /* ID3D11Texture2D* or ID3D11Texture3D* */
	ID3D11ShaderResourceView *shaderView;
	ID3D11UnorderedAccessView *unorderedAccessView; /* FIXME: This needs to be a dynamic array! */

	D3D11TargetView *targetViews;
	uint32_t targetViewCount;
	uint32_t targetViewCapacity;

	/* Basic Info */
	Refresh_TextureFormat format;
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	uint32_t levelCount;
	uint8_t isCube;
	uint8_t isRenderTarget;
} D3D11Texture;

typedef struct D3D11SwapchainData
{
	IDXGISwapChain *swapchain;
	D3D11Texture texture;
} D3D11SwapchainData;

typedef struct D3D11WindowData
{
	void* windowHandle;
	uint8_t allowTearing;
	D3D11SwapchainData *swapchainData;
} D3D11WindowData;

typedef struct D3D11ShaderModule
{
	ID3D11DeviceChild *shader; /* ID3D11VertexShader, ID3D11PixelShader, ID3D11ComputeShader */
	ID3D10Blob *blob;
	char *shaderSource;
	size_t shaderSourceLength;
} D3D11ShaderModule;

typedef struct D3D11GraphicsPipeline
{
	float blendConstants[4];
	int32_t numColorAttachments;
	DXGI_FORMAT colorAttachmentFormats[MAX_COLOR_TARGET_BINDINGS];
	ID3D11BlendState *colorAttachmentBlendState;

	Refresh_MultisampleState multisampleState;

	uint8_t hasDepthStencilAttachment;
	DXGI_FORMAT depthStencilAttachmentFormat;
	ID3D11DepthStencilState *depthStencilState;
	uint32_t stencilRef;

	Refresh_PrimitiveType primitiveType;
	ID3D11RasterizerState *rasterizerState;

	ID3D11VertexShader *vertexShader;
	ID3D11InputLayout *inputLayout;
	uint32_t *vertexStrides;
	uint32_t numVertexSamplers;
	uint32_t vertexUniformBlockSize;

	ID3D11PixelShader *fragmentShader;
	uint32_t numFragmentSamplers;
	uint32_t fragmentUniformBlockSize;
} D3D11GraphicsPipeline;

typedef struct D3D11ComputePipeline
{
	ID3D11ComputeShader *computeShader;
	uint64_t computeUniformBlockSize;
	uint32_t numTextures;
	uint32_t numBuffers;
} D3D11ComputePipeline;

typedef struct D3D11Buffer
{
	ID3D11Buffer *handle;
	ID3D11UnorderedAccessView* uav;
	uint32_t size;
} D3D11Buffer;

typedef struct D3D11UniformBuffer
{
	D3D11Buffer *d3d11Buffer;
	uint32_t offset;
	uint8_t hasDiscarded;
} D3D11UniformBuffer;

typedef struct D3D11Fence
{
	ID3D11Query *handle;
} D3D11Fence;

typedef struct D3D11CommandBuffer
{
	/* D3D11 Object References */
	ID3D11DeviceContext1 *context;
	D3D11SwapchainData *swapchainData;

	/* Render Pass */
	ID3D11RenderTargetView *rtViews[MAX_COLOR_TARGET_BINDINGS];
	ID3D11DepthStencilView *dsView;
	D3D11GraphicsPipeline *graphicsPipeline;

	/* Compute Pass */
	D3D11ComputePipeline *computePipeline;

	/* State */
	SDL_threadID threadID;
	D3D11Fence *fence;
	uint8_t autoReleaseFence;

	/* Uniforms */
	D3D11UniformBuffer *vertexUniformBuffer;
	D3D11UniformBuffer *fragmentUniformBuffer;
	D3D11UniformBuffer *computeUniformBuffer;

	D3D11UniformBuffer **boundUniformBuffers;
	uint32_t boundUniformBufferCount;
	uint32_t boundUniformBufferCapacity;
} D3D11CommandBuffer;

typedef struct D3D11Sampler
{
	ID3D11SamplerState *handle;
} D3D11Sampler;

typedef struct D3D11Renderer
{
	ID3D11Device1 *device;
	ID3D11DeviceContext *immediateContext;
	IDXGIFactory1 *factory;
	IDXGIAdapter1 *adapter;
	IDXGIDebug *dxgiDebug;
	IDXGIInfoQueue *dxgiInfoQueue;
	void *d3d11_dll;
	void *dxgi_dll;
	void *dxgidebug_dll;
	void *d3dcompiler_dll;

	uint8_t debugMode;
	D3D_FEATURE_LEVEL featureLevel; /* FIXME: Do we need this? */
	PFN_D3DCOMPILE D3DCompileFunc;

	D3D11WindowData **claimedWindows;
	uint32_t claimedWindowCount;
	uint32_t claimedWindowCapacity;

	D3D11CommandBuffer **availableCommandBuffers;
	uint32_t availableCommandBufferCount;
	uint32_t availableCommandBufferCapacity;

	D3D11CommandBuffer **submittedCommandBuffers;
	uint32_t submittedCommandBufferCount;
	uint32_t submittedCommandBufferCapacity;

	D3D11UniformBuffer **availableUniformBuffers;
	uint32_t availableUniformBufferCount;
	uint32_t availableUniformBufferCapacity;

	D3D11Fence **availableFences;
	uint32_t availableFenceCount;
	uint32_t availableFenceCapacity;

	SDL_mutex *contextLock;
	SDL_mutex *acquireCommandBufferLock;
	SDL_mutex *uniformBufferLock;
	SDL_mutex *fenceLock;
} D3D11Renderer;

/* Logging */

static void D3D11_INTERNAL_LogError(
	ID3D11Device1 *device,
	const char *msg,
	HRESULT res
) {
	#define MAX_ERROR_LEN 1024 /* FIXME: Arbitrary! */

	/* Buffer for text, ensure space for \0 terminator after buffer */
	char wszMsgBuff[MAX_ERROR_LEN + 1];
	DWORD dwChars; /* Number of chars returned. */

	if (res == DXGI_ERROR_DEVICE_REMOVED)
	{
		res = ID3D11Device_GetDeviceRemovedReason(device);
	}

	/* Try to get the message from the system errors. */
	dwChars = FormatMessage(
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		res,
		0,
		wszMsgBuff,
		MAX_ERROR_LEN,
		NULL
	);

	/* No message? Screw it, just post the code. */
	if (dwChars == 0)
	{
		Refresh_LogError("%s! Error Code: 0x%08X", msg, res);
		return;
	}

	/* Ensure valid range */
	dwChars = SDL_min(dwChars, MAX_ERROR_LEN);

	/* Trim whitespace from tail of message */
	while (dwChars > 0)
	{
		if (wszMsgBuff[dwChars - 1] <= ' ')
		{
			dwChars--;
		}
		else
		{
			break;
		}
	}

	/* Ensure null-terminated string */
	wszMsgBuff[dwChars] = '\0';

	Refresh_LogError("%s! Error Code: %s (0x%08X)", msg, wszMsgBuff, res);
}

/* Helper Functions */

static inline uint32_t D3D11_INTERNAL_CalcSubresource(
	uint32_t mipLevel,
	uint32_t arraySlice,
	uint32_t numLevels
) {
	return mipLevel + (arraySlice * numLevels);
}

static inline uint32_t D3D11_INTERNAL_NextHighestAlignment(
	uint32_t n,
	uint32_t align
) {
	return align * ((n + align - 1) / align);
}

static DXGI_FORMAT D3D11_INTERNAL_GetTypelessFormat(
	DXGI_FORMAT typedFormat
) {
	switch (typedFormat)
	{
	case DXGI_FORMAT_D16_UNORM:
		return DXGI_FORMAT_R16_TYPELESS;
	case DXGI_FORMAT_D32_FLOAT:
		return DXGI_FORMAT_R32_TYPELESS;
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
		return DXGI_FORMAT_R24G8_TYPELESS;
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		return DXGI_FORMAT_R32G8X24_TYPELESS;
	default:
		Refresh_LogError("Cannot get typeless DXGI format of format %d", typedFormat);
		return 0;
	}
}

static DXGI_FORMAT D3D11_INTERNAL_GetSampleableFormat(
	DXGI_FORMAT format
) {
	switch (format)
	{
	case DXGI_FORMAT_R16_TYPELESS:
		return DXGI_FORMAT_R16_UNORM;
	case DXGI_FORMAT_R32_TYPELESS:
		return DXGI_FORMAT_R32_FLOAT;
	case DXGI_FORMAT_R24G8_TYPELESS:
		return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	case DXGI_FORMAT_R32G8X24_TYPELESS:
		return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
	default:
		return format;
	}
}

/* Quit */

static void D3D11_DestroyDevice(
	Refresh_Device *device
) {
	D3D11Renderer *renderer = (D3D11Renderer*) device->driverData;

	/* Flush any remaining GPU work... */
	D3D11_Wait(device->driverData);

	/* Release the window data */
	for (int32_t i = renderer->claimedWindowCount - 1; i >= 0; i -= 1)
	{
		D3D11_UnclaimWindow(device->driverData, renderer->claimedWindows[i]->windowHandle);
	}
	SDL_free(renderer->claimedWindows);

	/* FIXME: Copied this from Vulkan, is it actually necessary? */
	D3D11_Wait(device->driverData);

	/* Release command buffer infrastructure */
	for (uint32_t i = 0; i < renderer->availableCommandBufferCount; i += 1)
	{
		D3D11CommandBuffer *commandBuffer = renderer->availableCommandBuffers[i];
		ID3D11DeviceContext_Release(commandBuffer->context);
		SDL_free(commandBuffer);
	}
	SDL_free(renderer->availableCommandBuffers);
	SDL_free(renderer->submittedCommandBuffers);

	/* Release uniform buffer infrastructure */
	for (uint32_t i = 0; i < renderer->availableUniformBufferCount; i += 1)
	{
		D3D11UniformBuffer *uniformBuffer = renderer->availableUniformBuffers[i];
		ID3D11Buffer_Release(uniformBuffer->d3d11Buffer->handle);
		SDL_free(uniformBuffer->d3d11Buffer);
		SDL_free(uniformBuffer);
	}
	SDL_free(renderer->availableUniformBuffers);

	/* Release fence infrastructure */
	for (uint32_t i = 0; i < renderer->availableFenceCount; i += 1)
	{
		D3D11Fence *fence = renderer->availableFences[i];
		ID3D11Query_Release(fence->handle);
		SDL_free(fence);
	}
	SDL_free(renderer->availableFences);

	/* Release the mutexes */
	SDL_DestroyMutex(renderer->acquireCommandBufferLock);
	SDL_DestroyMutex(renderer->contextLock);
	SDL_DestroyMutex(renderer->uniformBufferLock);
	SDL_DestroyMutex(renderer->fenceLock);

	/* Release the device and associated objects */
	ID3D11DeviceContext_Release(renderer->immediateContext);
	ID3D11Device_Release(renderer->device);
	IDXGIAdapter_Release(renderer->adapter);
	IDXGIFactory_Release(renderer->factory);

	/* Report leaks and clean up debug objects */
	if (renderer->dxgiDebug)
	{
		IDXGIDebug_ReportLiveObjects(
			renderer->dxgiDebug,
			D3D_IID_DXGI_DEBUG_ALL,
			DXGI_DEBUG_RLO_SUMMARY | DXGI_DEBUG_RLO_DETAIL
		);
		IDXGIDebug_Release(renderer->dxgiDebug);
	}

	if (renderer->dxgiInfoQueue)
	{
		IDXGIInfoQueue_Release(renderer->dxgiInfoQueue);
	}

	/* Release the DLLs */
	SDL_UnloadObject(renderer->d3d11_dll);
	SDL_UnloadObject(renderer->dxgi_dll);
	if (renderer->dxgidebug_dll)
	{
		SDL_UnloadObject(renderer->dxgidebug_dll);
	}
	SDL_UnloadObject(renderer->d3dcompiler_dll);

	/* Free the primary Refresh structures */
	SDL_free(renderer);
	SDL_free(device);
}

/* Drawing */

static void D3D11_DrawInstancedPrimitives(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	uint32_t baseVertex,
	uint32_t startIndex,
	uint32_t primitiveCount,
	uint32_t instanceCount,
	uint32_t vertexParamOffset,
	uint32_t fragmentParamOffset
) {
	D3D11CommandBuffer *d3d11CommandBuffer = (D3D11CommandBuffer*) commandBuffer;
	uint32_t vertexOffsetInConstants = vertexParamOffset / 16;
	uint32_t fragmentOffsetInConstants = fragmentParamOffset / 16;
	uint32_t vertexBlockSizeInConstants = d3d11CommandBuffer->graphicsPipeline->vertexUniformBlockSize / 16;
	uint32_t fragmentBlockSizeInConstants = d3d11CommandBuffer->graphicsPipeline->fragmentUniformBlockSize / 16;

	if (d3d11CommandBuffer->vertexUniformBuffer != NULL)
	{
		ID3D11DeviceContext1_VSSetConstantBuffers1(
			d3d11CommandBuffer->context,
			0,
			1,
			&d3d11CommandBuffer->vertexUniformBuffer->d3d11Buffer->handle,
			&vertexOffsetInConstants,
			&vertexBlockSizeInConstants
		);
	}

	if (d3d11CommandBuffer->fragmentUniformBuffer != NULL)
	{
		ID3D11DeviceContext1_PSSetConstantBuffers1(
			d3d11CommandBuffer->context,
			0,
			1,
			&d3d11CommandBuffer->fragmentUniformBuffer->d3d11Buffer->handle,
			&fragmentOffsetInConstants,
			&fragmentBlockSizeInConstants
		);
	}

	ID3D11DeviceContext_DrawIndexedInstanced(
		d3d11CommandBuffer->context,
		PrimitiveVerts(d3d11CommandBuffer->graphicsPipeline->primitiveType, primitiveCount),
		instanceCount,
		startIndex,
		baseVertex,
		0
	);
}

static void D3D11_DrawIndexedPrimitives(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	uint32_t baseVertex,
	uint32_t startIndex,
	uint32_t primitiveCount,
	uint32_t vertexParamOffset,
	uint32_t fragmentParamOffset
) {
	D3D11_DrawInstancedPrimitives(
		driverData,
		commandBuffer,
		baseVertex,
		startIndex,
		primitiveCount,
		1,
		vertexParamOffset,
		fragmentParamOffset
	);
}

static void D3D11_DrawPrimitives(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	uint32_t vertexStart,
	uint32_t primitiveCount,
	uint32_t vertexParamOffset,
	uint32_t fragmentParamOffset
) {
	D3D11CommandBuffer *d3d11CommandBuffer = (D3D11CommandBuffer*) commandBuffer;
	uint32_t vertexOffsetInConstants = vertexParamOffset / 16;
	uint32_t fragmentOffsetInConstants = fragmentParamOffset / 16;
	uint32_t vertexBlockSizeInConstants = d3d11CommandBuffer->graphicsPipeline->vertexUniformBlockSize / 16;
	uint32_t fragmentBlockSizeInConstants = d3d11CommandBuffer->graphicsPipeline->fragmentUniformBlockSize / 16;

	if (d3d11CommandBuffer->vertexUniformBuffer != NULL)
	{
		ID3D11DeviceContext1_VSSetConstantBuffers1(
			d3d11CommandBuffer->context,
			0,
			1,
			&d3d11CommandBuffer->vertexUniformBuffer->d3d11Buffer->handle,
			&vertexOffsetInConstants,
			&vertexBlockSizeInConstants
		);
	}

	if (d3d11CommandBuffer->fragmentUniformBuffer != NULL)
	{
		ID3D11DeviceContext1_PSSetConstantBuffers1(
			d3d11CommandBuffer->context,
			0,
			1,
			&d3d11CommandBuffer->fragmentUniformBuffer->d3d11Buffer->handle,
			&fragmentOffsetInConstants,
			&fragmentBlockSizeInConstants
		);
	}

	ID3D11DeviceContext_Draw(
		d3d11CommandBuffer->context,
		PrimitiveVerts(d3d11CommandBuffer->graphicsPipeline->primitiveType, primitiveCount),
		vertexStart
	);
}

static void D3D11_DrawPrimitivesIndirect(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Buffer *buffer,
	uint32_t offsetInBytes,
	uint32_t drawCount,
	uint32_t stride,
	uint32_t vertexParamOffset,
	uint32_t fragmentParamOffset
) {
	D3D11CommandBuffer *d3d11CommandBuffer = (D3D11CommandBuffer*) commandBuffer;
	D3D11Buffer *d3d11Buffer = (D3D11Buffer*) buffer;
	uint32_t vertexOffsetInConstants = vertexParamOffset / 16;
	uint32_t fragmentOffsetInConstants = fragmentParamOffset / 16;
	uint32_t vertexBlockSizeInConstants = d3d11CommandBuffer->graphicsPipeline->vertexUniformBlockSize / 16;
	uint32_t fragmentBlockSizeInConstants = d3d11CommandBuffer->graphicsPipeline->fragmentUniformBlockSize / 16;

	if (d3d11CommandBuffer->vertexUniformBuffer != NULL)
	{
		ID3D11DeviceContext1_VSSetConstantBuffers1(
			d3d11CommandBuffer->context,
			0,
			1,
			&d3d11CommandBuffer->vertexUniformBuffer->d3d11Buffer->handle,
			&vertexOffsetInConstants,
			&vertexBlockSizeInConstants
		);
	}

	if (d3d11CommandBuffer->fragmentUniformBuffer != NULL)
	{
		ID3D11DeviceContext1_PSSetConstantBuffers1(
			d3d11CommandBuffer->context,
			0,
			1,
			&d3d11CommandBuffer->fragmentUniformBuffer->d3d11Buffer->handle,
			&fragmentOffsetInConstants,
			&fragmentBlockSizeInConstants
		);
	}

	/* D3D11: "We have multi-draw at home!"
	 * Multi-draw at home:
	 */
	for (uint32_t i = 0; i < drawCount; i += 1)
	{
		ID3D11DeviceContext_DrawInstancedIndirect(
			d3d11CommandBuffer->context,
			d3d11Buffer->handle,
			offsetInBytes + (stride * i)
		);
	}
}

static void D3D11_DispatchCompute(
	Refresh_Renderer *device,
	Refresh_CommandBuffer *commandBuffer,
	uint32_t groupCountX,
	uint32_t groupCountY,
	uint32_t groupCountZ,
	uint32_t computeParamOffset
) {
	D3D11CommandBuffer* d3d11CommandBuffer = (D3D11CommandBuffer*)commandBuffer;
	uint32_t computeOffsetInConstants = computeParamOffset / 16;
	uint32_t computeBlockSizeInConstants = (uint32_t) (d3d11CommandBuffer->computePipeline->computeUniformBlockSize / 16);

	if (d3d11CommandBuffer->computeUniformBuffer != NULL)
	{
		ID3D11DeviceContext1_CSSetConstantBuffers1(
			d3d11CommandBuffer->context,
			0,
			1,
			&d3d11CommandBuffer->computeUniformBuffer->d3d11Buffer->handle,
			&computeOffsetInConstants,
			&computeBlockSizeInConstants
		);
	}

	ID3D11DeviceContext_Dispatch(
		d3d11CommandBuffer->context,
		groupCountX,
		groupCountY,
		groupCountZ
	);
}

/* State Creation */

static ID3D11BlendState* D3D11_INTERNAL_FetchBlendState(
	D3D11Renderer *renderer,
	uint32_t numColorAttachments,
	Refresh_ColorAttachmentDescription *colorAttachments
) {
	ID3D11BlendState *result;
	D3D11_BLEND_DESC blendDesc;
	HRESULT res;

	/* Create a new blend state.
	 * The spec says the driver will not create duplicate states, so there's no need to cache.
	 */
	SDL_zero(blendDesc); /* needed for any unused RT entries */

	blendDesc.AlphaToCoverageEnable = FALSE;
	blendDesc.IndependentBlendEnable = TRUE;

	for (uint32_t i = 0; i < numColorAttachments; i += 1)
	{
		blendDesc.RenderTarget[i].BlendEnable = colorAttachments[i].blendState.blendEnable;
		blendDesc.RenderTarget[i].BlendOp = RefreshToD3D11_BlendOp[
			colorAttachments[i].blendState.colorBlendOp
		];
		blendDesc.RenderTarget[i].BlendOpAlpha = RefreshToD3D11_BlendOp[
			colorAttachments[i].blendState.alphaBlendOp
		];
		blendDesc.RenderTarget[i].DestBlend = RefreshToD3D11_BlendFactor[
			colorAttachments[i].blendState.dstColorBlendFactor
		];
		blendDesc.RenderTarget[i].DestBlendAlpha = RefreshToD3D11_BlendFactor[
			colorAttachments[i].blendState.dstAlphaBlendFactor
		];
		blendDesc.RenderTarget[i].RenderTargetWriteMask = colorAttachments[i].blendState.colorWriteMask;
		blendDesc.RenderTarget[i].SrcBlend = RefreshToD3D11_BlendFactor[
			colorAttachments[i].blendState.srcColorBlendFactor
		];
		blendDesc.RenderTarget[i].SrcBlendAlpha = RefreshToD3D11_BlendFactor[
			colorAttachments[i].blendState.srcAlphaBlendFactor
		];
	}

	res = ID3D11Device_CreateBlendState(
		renderer->device,
		&blendDesc,
		&result
	);
	ERROR_CHECK_RETURN("Could not create blend state", NULL);

	return result;
}

static ID3D11DepthStencilState* D3D11_INTERNAL_FetchDepthStencilState(
	D3D11Renderer *renderer,
	Refresh_DepthStencilState depthStencilState
) {
	ID3D11DepthStencilState *result;
	D3D11_DEPTH_STENCIL_DESC dsDesc;
	HRESULT res;

	/* Create a new depth-stencil state.
	 * The spec says the driver will not create duplicate states, so there's no need to cache.
	 */
	dsDesc.DepthEnable = depthStencilState.depthTestEnable;
	dsDesc.StencilEnable = depthStencilState.stencilTestEnable;
	dsDesc.DepthFunc = RefreshToD3D11_CompareOp[depthStencilState.compareOp];
	dsDesc.DepthWriteMask = (
		depthStencilState.depthWriteEnable ?
			D3D11_DEPTH_WRITE_MASK_ALL :
			D3D11_DEPTH_WRITE_MASK_ZERO
	);

	dsDesc.BackFace.StencilFunc = RefreshToD3D11_CompareOp[depthStencilState.backStencilState.compareOp];
	dsDesc.BackFace.StencilDepthFailOp = RefreshToD3D11_CompareOp[depthStencilState.backStencilState.depthFailOp];
	dsDesc.BackFace.StencilFailOp = RefreshToD3D11_CompareOp[depthStencilState.backStencilState.failOp];
	dsDesc.BackFace.StencilPassOp = RefreshToD3D11_CompareOp[depthStencilState.backStencilState.passOp];

	dsDesc.FrontFace.StencilFunc = RefreshToD3D11_CompareOp[depthStencilState.frontStencilState.compareOp];
	dsDesc.FrontFace.StencilDepthFailOp = RefreshToD3D11_CompareOp[depthStencilState.frontStencilState.depthFailOp];
	dsDesc.FrontFace.StencilFailOp = RefreshToD3D11_CompareOp[depthStencilState.frontStencilState.failOp];
	dsDesc.FrontFace.StencilPassOp = RefreshToD3D11_CompareOp[depthStencilState.frontStencilState.passOp];

	/* FIXME: D3D11 doesn't have separate read/write masks for each stencil side. What should we do? */
	dsDesc.StencilReadMask = depthStencilState.frontStencilState.compareMask;
	dsDesc.StencilWriteMask = depthStencilState.frontStencilState.writeMask;

	/* FIXME: What do we do with these?
	 *	depthStencilState.depthBoundsTestEnable
	 *	depthStencilState.maxDepthBounds
	 *	depthStencilState.minDepthBounds
	 */

	res = ID3D11Device_CreateDepthStencilState(
		renderer->device,
		&dsDesc,
		&result
	);
	ERROR_CHECK_RETURN("Could not create depth-stencil state", NULL);

	return result;
}

static ID3D11RasterizerState* D3D11_INTERNAL_FetchRasterizerState(
	D3D11Renderer *renderer,
	Refresh_RasterizerState rasterizerState
) {
	ID3D11RasterizerState *result;
	D3D11_RASTERIZER_DESC rasterizerDesc;
	HRESULT res;

	/* Create a new rasterizer state.
	 * The spec says the driver will not create duplicate states, so there's no need to cache.
	 */
	rasterizerDesc.AntialiasedLineEnable = FALSE;
	rasterizerDesc.CullMode = RefreshToD3D11_CullMode[rasterizerState.cullMode];
	rasterizerDesc.DepthBias = (INT) rasterizerState.depthBiasConstantFactor; /* FIXME: Is this cast correct? */
	rasterizerDesc.DepthBiasClamp = rasterizerState.depthBiasClamp;
	rasterizerDesc.DepthClipEnable = TRUE; /* FIXME: Do we want this...? */
	rasterizerDesc.FillMode = (rasterizerState.fillMode == REFRESH_FILLMODE_FILL) ? D3D11_FILL_SOLID : D3D11_FILL_WIREFRAME;
	rasterizerDesc.FrontCounterClockwise = (rasterizerState.frontFace == REFRESH_FRONTFACE_COUNTER_CLOCKWISE);
	rasterizerDesc.MultisampleEnable = TRUE; /* only applies to MSAA render targets */
	rasterizerDesc.ScissorEnable = TRUE;
	rasterizerDesc.SlopeScaledDepthBias = rasterizerState.depthBiasSlopeFactor;

	res = ID3D11Device_CreateRasterizerState(
		renderer->device,
		&rasterizerDesc,
		&result
	);
	ERROR_CHECK_RETURN("Could not create rasterizer state", NULL);

	return result;
}

static uint32_t D3D11_INTERNAL_FindIndexOfVertexBinding(
	uint32_t targetBinding,
	const Refresh_VertexBinding *bindings,
	uint32_t numBindings
) {
	for (uint32_t i = 0; i < numBindings; i += 1)
	{
		if (bindings[i].binding == targetBinding)
		{
			return i;
		}
	}

	Refresh_LogError("Could not find vertex binding %d!", targetBinding);
	return 0;
}

static ID3D11InputLayout* D3D11_INTERNAL_FetchInputLayout(
	D3D11Renderer *renderer,
	Refresh_VertexInputState inputState,
	void *shaderBytes,
	size_t shaderByteLength
) {
	ID3D11InputLayout *result = NULL;
	D3D11_INPUT_ELEMENT_DESC *elementDescs;
	uint32_t bindingIndex;
	HRESULT res;

	/* Don't bother creating/fetching an input layout if there are no attributes. */
	if (inputState.vertexAttributeCount == 0)
	{
		return NULL;
	}

	/* Allocate an array of vertex elements */
	elementDescs = SDL_stack_alloc(
		D3D11_INPUT_ELEMENT_DESC,
		inputState.vertexAttributeCount
	);

	/* Create the array of input elements */
	for (uint32_t i = 0; i < inputState.vertexAttributeCount; i += 1)
	{
		elementDescs[i].AlignedByteOffset = inputState.vertexAttributes[i].offset;
		elementDescs[i].Format = RefreshToD3D11_VertexFormat[
			inputState.vertexAttributes[i].format
		];
		elementDescs[i].InputSlot = inputState.vertexAttributes[i].binding;

		bindingIndex = D3D11_INTERNAL_FindIndexOfVertexBinding(
			elementDescs[i].InputSlot,
			inputState.vertexBindings,
			inputState.vertexBindingCount
		);
		elementDescs[i].InputSlotClass = RefreshToD3D11_VertexInputRate[
			inputState.vertexBindings[bindingIndex].inputRate
		];
		/* The spec requires this to be 0 for per-vertex data */
		elementDescs[i].InstanceDataStepRate = (
			elementDescs[i].InputSlotClass == D3D11_INPUT_PER_INSTANCE_DATA ? 1 : 0
		);

		elementDescs[i].SemanticIndex = inputState.vertexAttributes[i].location;
		elementDescs[i].SemanticName = "TEXCOORD";
	}

	res = ID3D11Device_CreateInputLayout(
		renderer->device,
		elementDescs,
		inputState.vertexAttributeCount,
		shaderBytes,
		shaderByteLength,
		&result
	);
	if (FAILED(res))
	{
		Refresh_LogError("Could not create input layout! Error: %X", res);
		SDL_stack_free(elementDescs);
		return NULL;
	}

	/* FIXME:
	 * These are not cached by the driver! Should we cache them, or allow duplicates?
	 * If we have one input layout per graphics pipeline maybe that wouldn't be so bad...?
	 */

	SDL_stack_free(elementDescs);
	return result;
}

/* Pipeline Creation */

static Refresh_ComputePipeline* D3D11_CreateComputePipeline(
	Refresh_Renderer *driverData,
	Refresh_ComputeShaderInfo *computeShaderInfo
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11ComputePipeline* pipeline = (D3D11ComputePipeline*) SDL_malloc(sizeof(D3D11ComputePipeline));
	D3D11ShaderModule* shaderModule = (D3D11ShaderModule*) computeShaderInfo->shaderModule;
	ID3D10Blob *errorBlob;
	HRESULT res;

	pipeline->numTextures = computeShaderInfo->imageBindingCount;
	pipeline->numBuffers = computeShaderInfo->bufferBindingCount;

	if (shaderModule->shader == NULL)
	{
		res = renderer->D3DCompileFunc(
			shaderModule->shaderSource,
			shaderModule->shaderSourceLength,
			NULL,
			NULL,
			NULL,
			"main", /* FIXME: Is this correct or should this be computeShaderInfo.entryPoint? */
			"cs_5_0",
			0,
			0,
			&shaderModule->blob,
			&errorBlob
		);
		if (FAILED(res))
		{
			Refresh_LogError("Compute Shader Compile Error: %s", ID3D10Blob_GetBufferPointer(errorBlob));
			return NULL;
		}

		res = ID3D11Device_CreateComputeShader(
			renderer->device,
			ID3D10Blob_GetBufferPointer(shaderModule->blob),
			ID3D10Blob_GetBufferSize(shaderModule->blob),
			NULL,
			(ID3D11ComputeShader**) &shaderModule->shader
		);
		ERROR_CHECK_RETURN("Could not create compute shader", NULL); /* FIXME: This leaks the pipeline! */
	}
	pipeline->computeShader = (ID3D11ComputeShader*) shaderModule->shader;
	pipeline->computeUniformBlockSize = D3D11_INTERNAL_NextHighestAlignment(
		(uint32_t) computeShaderInfo->uniformBufferSize,
		256
	);

	return (Refresh_ComputePipeline*) pipeline;
}

static Refresh_GraphicsPipeline* D3D11_CreateGraphicsPipeline(
	Refresh_Renderer *driverData,
	Refresh_GraphicsPipelineCreateInfo *pipelineCreateInfo
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11GraphicsPipeline *pipeline = (D3D11GraphicsPipeline*) SDL_malloc(sizeof(D3D11GraphicsPipeline));
	D3D11ShaderModule *vertShaderModule = (D3D11ShaderModule*) pipelineCreateInfo->vertexShaderInfo.shaderModule;
	D3D11ShaderModule *fragShaderModule = (D3D11ShaderModule*) pipelineCreateInfo->fragmentShaderInfo.shaderModule;
	ID3D10Blob *errorBlob;
	HRESULT res;

	/* Blend */

	pipeline->colorAttachmentBlendState = D3D11_INTERNAL_FetchBlendState(
		renderer,
		pipelineCreateInfo->attachmentInfo.colorAttachmentCount,
		pipelineCreateInfo->attachmentInfo.colorAttachmentDescriptions
	);

	pipeline->numColorAttachments = pipelineCreateInfo->attachmentInfo.colorAttachmentCount;
	for (int32_t i = 0; i < pipeline->numColorAttachments; i += 1)
	{
		pipeline->colorAttachmentFormats[i] = RefreshToD3D11_TextureFormat[
			pipelineCreateInfo->attachmentInfo.colorAttachmentDescriptions[i].format
		];
	}

	pipeline->blendConstants[0] = pipelineCreateInfo->blendConstants[0];
	pipeline->blendConstants[1] = pipelineCreateInfo->blendConstants[1];
	pipeline->blendConstants[2] = pipelineCreateInfo->blendConstants[2];
	pipeline->blendConstants[3] = pipelineCreateInfo->blendConstants[3];

	/* Multisample */

	pipeline->multisampleState = pipelineCreateInfo->multisampleState;

	/* Depth-Stencil */

	pipeline->depthStencilState = D3D11_INTERNAL_FetchDepthStencilState(
		renderer,
		pipelineCreateInfo->depthStencilState
	);

	pipeline->hasDepthStencilAttachment = pipelineCreateInfo->attachmentInfo.hasDepthStencilAttachment;
	pipeline->depthStencilAttachmentFormat = RefreshToD3D11_TextureFormat[
		pipelineCreateInfo->attachmentInfo.depthStencilFormat
	];
	/* FIXME: D3D11 doesn't have separate references for each stencil side. What should we do? */
	pipeline->stencilRef = pipelineCreateInfo->depthStencilState.frontStencilState.reference;

	/* Rasterizer */

	pipeline->primitiveType = pipelineCreateInfo->primitiveType;
	pipeline->rasterizerState = D3D11_INTERNAL_FetchRasterizerState(
		renderer,
		pipelineCreateInfo->rasterizerState
	);

	/* Vertex Shader */

	if (vertShaderModule->shader == NULL)
	{
		res = renderer->D3DCompileFunc(
			vertShaderModule->shaderSource,
			vertShaderModule->shaderSourceLength,
			NULL,
			NULL,
			NULL,
			"main", /* FIXME: Is this correct or should this be vertexShaderInfo.entryPoint? */
			"vs_5_0",
			0,
			0,
			&vertShaderModule->blob,
			&errorBlob
		);
		if (FAILED(res))
		{
			Refresh_LogError("Vertex Shader Compile Error: %s", ID3D10Blob_GetBufferPointer(errorBlob));
			return NULL;
		}

		res = ID3D11Device_CreateVertexShader(
			renderer->device,
			ID3D10Blob_GetBufferPointer(vertShaderModule->blob),
			ID3D10Blob_GetBufferSize(vertShaderModule->blob),
			NULL,
			(ID3D11VertexShader**) &vertShaderModule->shader
		);
		ERROR_CHECK_RETURN("Could not create vertex shader", NULL); /* FIXME: This leaks the pipeline! */
	}
	pipeline->vertexShader = (ID3D11VertexShader*) vertShaderModule->shader;
	pipeline->numVertexSamplers = pipelineCreateInfo->vertexShaderInfo.samplerBindingCount;
	pipeline->vertexUniformBlockSize = D3D11_INTERNAL_NextHighestAlignment(
		(uint32_t) pipelineCreateInfo->vertexShaderInfo.uniformBufferSize,
		256
	);

	/* Input Layout */

	pipeline->inputLayout = D3D11_INTERNAL_FetchInputLayout(
		renderer,
		pipelineCreateInfo->vertexInputState,
		ID3D10Blob_GetBufferPointer(vertShaderModule->blob),
		ID3D10Blob_GetBufferSize(vertShaderModule->blob)
	);

	if (pipelineCreateInfo->vertexInputState.vertexBindingCount > 0)
	{
		pipeline->vertexStrides = SDL_malloc(
			sizeof(uint32_t) *
			pipelineCreateInfo->vertexInputState.vertexBindingCount
		);

		for (uint32_t i = 0; i < pipelineCreateInfo->vertexInputState.vertexBindingCount; i += 1)
		{
			pipeline->vertexStrides[i] = pipelineCreateInfo->vertexInputState.vertexBindings[i].stride;
		}
	}
	else
	{
		pipeline->vertexStrides = NULL;
	}

	/* Fragment Shader */

	if (fragShaderModule->shader == NULL)
	{
		res = renderer->D3DCompileFunc(
			fragShaderModule->shaderSource,
			fragShaderModule->shaderSourceLength,
			NULL,
			NULL,
			NULL,
			"main", /* FIXME: Is this correct or should this be fragmentShaderInfo.entryPoint? */
			"ps_5_0",
			0,
			0,
			&fragShaderModule->blob,
			&errorBlob
		);
		if (FAILED(res))
		{
			Refresh_LogError("Fragment Shader Compile Error: %s", ID3D10Blob_GetBufferPointer(errorBlob));
			return NULL;
		}

		res = ID3D11Device_CreatePixelShader(
			renderer->device,
			ID3D10Blob_GetBufferPointer(fragShaderModule->blob),
			ID3D10Blob_GetBufferSize(fragShaderModule->blob),
			NULL,
			(ID3D11PixelShader**) &fragShaderModule->shader
		);
		ERROR_CHECK_RETURN("Could not create pixel shader", NULL); /* FIXME: This leaks the pipeline! */
	}
	pipeline->fragmentShader = (ID3D11PixelShader*) fragShaderModule->shader;
	pipeline->numFragmentSamplers = pipelineCreateInfo->fragmentShaderInfo.samplerBindingCount;
	pipeline->fragmentUniformBlockSize = D3D11_INTERNAL_NextHighestAlignment(
		(uint32_t) pipelineCreateInfo->fragmentShaderInfo.uniformBufferSize,
		256
	);

	return (Refresh_GraphicsPipeline*) pipeline;
}

/* Resource Creation */

static Refresh_Sampler* D3D11_CreateSampler(
	Refresh_Renderer *driverData,
	Refresh_SamplerStateCreateInfo *samplerStateCreateInfo
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11_SAMPLER_DESC samplerDesc;
	ID3D11SamplerState *samplerStateHandle;
	D3D11Sampler *d3d11Sampler;
	HRESULT res;

	samplerDesc.AddressU = RefreshToD3D11_SamplerAddressMode[samplerStateCreateInfo->addressModeU];
	samplerDesc.AddressV = RefreshToD3D11_SamplerAddressMode[samplerStateCreateInfo->addressModeV];
	samplerDesc.AddressW = RefreshToD3D11_SamplerAddressMode[samplerStateCreateInfo->addressModeW];

	RefreshToD3D11_BorderColor(
		samplerStateCreateInfo,
		&samplerDesc
	);

	samplerDesc.ComparisonFunc = (
		samplerStateCreateInfo->compareEnable ?
			RefreshToD3D11_CompareOp[samplerStateCreateInfo->compareOp] :
			RefreshToD3D11_CompareOp[REFRESH_COMPAREOP_ALWAYS]
	);
	samplerDesc.MaxAnisotropy = (
		samplerStateCreateInfo->anisotropyEnable ?
			(UINT) samplerStateCreateInfo->maxAnisotropy :
			0
	);
	samplerDesc.Filter = RefreshToD3D11_Filter(samplerStateCreateInfo);
	samplerDesc.MaxLOD = samplerStateCreateInfo->maxLod;
	samplerDesc.MinLOD = samplerStateCreateInfo->minLod;
	samplerDesc.MipLODBias = samplerStateCreateInfo->mipLodBias;

	res = ID3D11Device_CreateSamplerState(
		renderer->device,
		&samplerDesc,
		&samplerStateHandle
	);
	ERROR_CHECK_RETURN("Could not create sampler state", NULL);

	d3d11Sampler = (D3D11Sampler*) SDL_malloc(sizeof(D3D11Sampler));
	d3d11Sampler->handle = samplerStateHandle;

	return (Refresh_Sampler*) d3d11Sampler;
}

static Refresh_ShaderModule* D3D11_CreateShaderModule(
	Refresh_Renderer *driverData,
	Refresh_ShaderModuleCreateInfo *shaderModuleCreateInfo
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11ShaderModule *shaderModule = (D3D11ShaderModule*) SDL_malloc(sizeof(D3D11ShaderModule));

	/* We don't know whether this is a vertex, fragment, or compute shader,
	 * so wait to compile until we bind to a pipeline...
	 *
	 * FIXME: Could we store a flag in the shaderc output to mark if a shader is vertex/fragment/compute?
	 * Then we could compile on shader module creation instead of at bind time.
	 */
	shaderModule->shader = NULL;
	shaderModule->blob = NULL;

	shaderModule->shaderSourceLength = shaderModuleCreateInfo->codeSize;
	shaderModule->shaderSource = (char*) SDL_malloc(shaderModule->shaderSourceLength);
	SDL_memcpy(shaderModule->shaderSource, shaderModuleCreateInfo->byteCode, shaderModuleCreateInfo->codeSize);

	return (Refresh_ShaderModule*) shaderModule;
}

static Refresh_Texture* D3D11_CreateTexture(
	Refresh_Renderer *driverData,
	Refresh_TextureCreateInfo *textureCreateInfo
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	uint8_t isSampler, isCompute, isColorTarget, isDepthStencil;
	DXGI_FORMAT format;
	ID3D11Resource *textureHandle;
	ID3D11ShaderResourceView *srv = NULL;
	ID3D11UnorderedAccessView *uav = NULL;
	D3D11Texture *d3d11Texture;
	HRESULT res;

	isColorTarget = textureCreateInfo->usageFlags & REFRESH_TEXTUREUSAGE_COLOR_TARGET_BIT;
	isDepthStencil = textureCreateInfo->usageFlags & REFRESH_TEXTUREUSAGE_DEPTH_STENCIL_TARGET_BIT;
	isSampler = textureCreateInfo->usageFlags & REFRESH_TEXTUREUSAGE_SAMPLER_BIT;
	isCompute = textureCreateInfo->usageFlags & REFRESH_TEXTUREUSAGE_COMPUTE_BIT;

	format = RefreshToD3D11_TextureFormat[textureCreateInfo->format];
	if (isDepthStencil)
	{
		format = D3D11_INTERNAL_GetTypelessFormat(format);
	}

	if (textureCreateInfo->depth <= 1)
	{
		D3D11_TEXTURE2D_DESC desc2D;

		desc2D.Width = textureCreateInfo->width;
		desc2D.Height = textureCreateInfo->height;

		desc2D.BindFlags = 0;
		if (isSampler)
		{
			desc2D.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
		}
		if (isCompute)
		{
			desc2D.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
		}
		if (isColorTarget)
		{
			desc2D.BindFlags |= D3D11_BIND_RENDER_TARGET;
		}
		if (isDepthStencil)
		{
			desc2D.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
		}

		desc2D.ArraySize = textureCreateInfo->isCube ? 6 : 1;
		desc2D.CPUAccessFlags = 0;
		desc2D.Format = format;
		desc2D.MipLevels = textureCreateInfo->levelCount;
		desc2D.MiscFlags = textureCreateInfo->isCube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;
		desc2D.SampleDesc.Count = RefreshToD3D11_SampleCount[textureCreateInfo->sampleCount];
		desc2D.SampleDesc.Quality = 0;
		desc2D.Usage = D3D11_USAGE_DEFAULT;

		res = ID3D11Device_CreateTexture2D(
			renderer->device,
			&desc2D,
			NULL,
			(ID3D11Texture2D**) &textureHandle
		);
		ERROR_CHECK_RETURN("Could not create Texture2D", NULL);

		/* Create the SRV, if applicable */
		if (isSampler)
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
			srvDesc.Format = D3D11_INTERNAL_GetSampleableFormat(format);

			if (textureCreateInfo->isCube)
			{
				srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
				srvDesc.TextureCube.MipLevels = desc2D.MipLevels;
				srvDesc.TextureCube.MostDetailedMip = 0;
			}
			else
			{
				srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				srvDesc.Texture2D.MipLevels = desc2D.MipLevels;
				srvDesc.Texture2D.MostDetailedMip = 0;
			}

			res = ID3D11Device_CreateShaderResourceView(
				renderer->device,
				textureHandle,
				&srvDesc,
				&srv
			);
			if (FAILED(res))
			{
				ID3D11Resource_Release(textureHandle);
				D3D11_INTERNAL_LogError(renderer->device, "Could not create SRV for 2D texture", res);
				return NULL;
			}
		}

		/* Create the UAV, if applicable */
		if (isCompute)
		{
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;
			uavDesc.Format = format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0; /* FIXME */

			res = ID3D11Device_CreateUnorderedAccessView(
				renderer->device,
				textureHandle,
				&uavDesc,
				&uav
			);
			if (FAILED(res))
			{
				ID3D11Resource_Release(textureHandle);
				if (srv != NULL)
				{
					ID3D11ShaderResourceView_Release(srv);
				}
				D3D11_INTERNAL_LogError(renderer->device, "Could not create UAV for 2D texture", res);
				return NULL;
			}
		}
	}
	else
	{
		D3D11_TEXTURE3D_DESC desc3D;

		desc3D.Width = textureCreateInfo->width;
		desc3D.Height = textureCreateInfo->height;
		desc3D.Depth = textureCreateInfo->depth;

		desc3D.BindFlags = 0;
		if (isSampler)
		{
			desc3D.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
		}
		if (isCompute)
		{
			desc3D.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
		}
		if (isColorTarget)
		{
			desc3D.BindFlags |= D3D11_BIND_RENDER_TARGET;
		}

		desc3D.CPUAccessFlags = 0;
		desc3D.Format = format;
		desc3D.MipLevels = textureCreateInfo->levelCount;
		desc3D.MiscFlags = 0;
		desc3D.Usage = D3D11_USAGE_DEFAULT;

		res = ID3D11Device_CreateTexture3D(
			renderer->device,
			&desc3D,
			NULL,
			(ID3D11Texture3D**) &textureHandle
		);
		ERROR_CHECK_RETURN("Could not create Texture3D", NULL);

		/* Create the SRV, if applicable */
		if (isSampler)
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
			srvDesc.Format = format;
			srvDesc.ViewDimension = D3D10_SRV_DIMENSION_TEXTURE3D;
			srvDesc.Texture3D.MipLevels = desc3D.MipLevels;
			srvDesc.Texture3D.MostDetailedMip = 0;

			res = ID3D11Device_CreateShaderResourceView(
				renderer->device,
				textureHandle,
				&srvDesc,
				&srv
			);
			if (FAILED(res))
			{
				ID3D11Resource_Release(textureHandle);
				D3D11_INTERNAL_LogError(renderer->device, "Could not create SRV for 3D texture", res);
				return NULL;
			}
		}

		/* Create the UAV, if applicable */
		if (isCompute)
		{
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;
			uavDesc.Format = format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
			uavDesc.Texture3D.MipSlice = 0; /* FIXME */
			uavDesc.Texture3D.FirstWSlice = 0; /* FIXME */
			uavDesc.Texture3D.WSize = -1; /* FIXME */

			res = ID3D11Device_CreateUnorderedAccessView(
				renderer->device,
				textureHandle,
				&uavDesc,
				&uav
			);
			if (FAILED(res))
			{
				ID3D11Resource_Release(textureHandle);
				if (srv != NULL)
				{
					ID3D11ShaderResourceView_Release(srv);
				}
				D3D11_INTERNAL_LogError(renderer->device, "Could not create UAV for 3D texture", res);
				return NULL;
			}
		}
	}

	d3d11Texture = (D3D11Texture*) SDL_malloc(sizeof(D3D11Texture));
	d3d11Texture->handle = textureHandle;
	d3d11Texture->format = textureCreateInfo->format;
	d3d11Texture->width = textureCreateInfo->width;
	d3d11Texture->height = textureCreateInfo->height;
	d3d11Texture->depth = textureCreateInfo->depth;
	d3d11Texture->levelCount = textureCreateInfo->levelCount;
	d3d11Texture->isCube = textureCreateInfo->isCube;
	d3d11Texture->isRenderTarget = isColorTarget | isDepthStencil;
	d3d11Texture->shaderView = srv;
	d3d11Texture->unorderedAccessView = uav;
	d3d11Texture->targetViewCount = 0;

	if (d3d11Texture->isRenderTarget)
	{
		d3d11Texture->targetViewCapacity = d3d11Texture->isCube ? 6 : 1;
		d3d11Texture->targetViews = SDL_malloc(
			sizeof(D3D11TargetView) * d3d11Texture->targetViewCapacity
		);
	}
	else
	{
		d3d11Texture->targetViewCapacity = 0;
		d3d11Texture->targetViews = NULL;
	}

	return (Refresh_Texture*) d3d11Texture;
}

static Refresh_Buffer* D3D11_CreateBuffer(
	Refresh_Renderer *driverData,
	Refresh_BufferUsageFlags usageFlags,
	uint32_t sizeInBytes
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11_BUFFER_DESC bufferDesc;
	ID3D11Buffer *bufferHandle;
	ID3D11UnorderedAccessView *uav = NULL;
	D3D11Buffer *d3d11Buffer;
	HRESULT res;

	bufferDesc.BindFlags = 0;
	if (usageFlags & REFRESH_BUFFERUSAGE_VERTEX_BIT)
	{
		bufferDesc.BindFlags |= D3D11_BIND_VERTEX_BUFFER;
	}
	if (usageFlags & REFRESH_BUFFERUSAGE_INDEX_BIT)
	{
		bufferDesc.BindFlags |= D3D11_BIND_INDEX_BUFFER;
	}
	if ((usageFlags & REFRESH_BUFFERUSAGE_COMPUTE_BIT) || (usageFlags & REFRESH_BUFFERUSAGE_INDIRECT_BIT))
	{
		bufferDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
	}

	bufferDesc.ByteWidth = sizeInBytes;
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.CPUAccessFlags = 0;
	bufferDesc.MiscFlags = (usageFlags & REFRESH_BUFFERUSAGE_INDIRECT_BIT) ? D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS : 0;
	bufferDesc.StructureByteStride = 0;

	res = ID3D11Device_CreateBuffer(
		renderer->device,
		&bufferDesc,
		NULL,
		&bufferHandle
	);
	ERROR_CHECK_RETURN("Could not create buffer", NULL);

	/* Create a UAV for the buffer */
	if (usageFlags & REFRESH_BUFFERUSAGE_COMPUTE_BIT)
	{
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;
		uavDesc.Format = DXGI_FORMAT_R8_UINT;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.Flags = 0;
		uavDesc.Buffer.NumElements = sizeInBytes;

		res = ID3D11Device_CreateUnorderedAccessView(
			renderer->device,
			(ID3D11Resource*) bufferHandle,
			&uavDesc,
			&uav
		);
		if (FAILED(res))
		{
			ID3D11Buffer_Release(bufferHandle);
			ERROR_CHECK_RETURN("Could not create UAV for buffer!", NULL);
		}
	}

	d3d11Buffer = SDL_malloc(sizeof(D3D11Buffer));
	d3d11Buffer->handle = bufferHandle;
	d3d11Buffer->size = sizeInBytes;
	d3d11Buffer->uav = uav;

	return (Refresh_Buffer*) d3d11Buffer;
}

/* Texture Data */

static void D3D11_SetTextureData(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_TextureSlice *textureSlice,
	void *data,
	uint32_t dataLengthInBytes
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11CommandBuffer *d3d11CommandBuffer = (D3D11CommandBuffer*) commandBuffer;
	D3D11Texture *d3d11Texture = (D3D11Texture*) textureSlice->texture;

	int32_t w = textureSlice->rectangle.w;
	int32_t h = textureSlice->rectangle.h;

	int32_t blockSize = Texture_GetBlockSize(d3d11Texture->format);
	if (blockSize > 1)
	{
		w = (w + blockSize - 1) & ~(blockSize - 1);
		h = (h + blockSize - 1) & ~(blockSize - 1);
	}

	D3D11_BOX dstBox;
	dstBox.left = textureSlice->rectangle.x;
	dstBox.top = textureSlice->rectangle.y;
	dstBox.front = textureSlice->depth;
	dstBox.right = textureSlice->rectangle.x + w;
	dstBox.bottom = textureSlice->rectangle.y + h;
	dstBox.back = textureSlice->depth + 1;

	ID3D11DeviceContext1_UpdateSubresource1(
		d3d11CommandBuffer->context,
		d3d11Texture->handle,
		D3D11_INTERNAL_CalcSubresource(
			textureSlice->level,
			textureSlice->layer,
			d3d11Texture->levelCount
		),
		&dstBox,
		data,
		BytesPerRow(w, d3d11Texture->format),
		BytesPerImage(w, h, d3d11Texture->format),
		D3D11_COPY_DISCARD /* FIXME: Is this right? */
	);
}

static void D3D11_SetTextureDataYUV(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer* commandBuffer,
	Refresh_Texture *y,
	Refresh_Texture *u,
	Refresh_Texture *v,
	uint32_t yWidth,
	uint32_t yHeight,
	uint32_t uvWidth,
	uint32_t uvHeight,
	void *yDataPtr,
	void *uDataPtr,
	void *vDataPtr,
	uint32_t yDataLength,
	uint32_t uvDataLength,
	uint32_t yStride,
	uint32_t uvStride
) {
	NOT_IMPLEMENTED
}

static void D3D11_CopyTextureToTexture(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_TextureSlice *sourceTextureSlice,
	Refresh_TextureSlice *destinationTextureSlice,
	Refresh_Filter filter
) {
	NOT_IMPLEMENTED
}

static void D3D11_CopyTextureToBuffer(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_TextureSlice *textureSlice,
	Refresh_Buffer *buffer
) {
	NOT_IMPLEMENTED
}

/* Buffer Data */

static void D3D11_SetBufferData(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Buffer *buffer,
	uint32_t offsetInBytes,
	void* data,
	uint32_t dataLength
) {
	D3D11CommandBuffer *d3d11CommandBuffer = (D3D11CommandBuffer*) commandBuffer;
	D3D11Buffer *d3d11Buffer = (D3D11Buffer*) buffer;
	D3D11_BOX dstBox = { offsetInBytes, 0, 0, offsetInBytes + dataLength, 1, 1 };

	ID3D11DeviceContext1_UpdateSubresource1(
		d3d11CommandBuffer->context,
		(ID3D11Resource*) d3d11Buffer->handle,
		0,
		&dstBox,
		data,
		dataLength,
		1,
		dataLength == d3d11Buffer->size ? D3D11_COPY_DISCARD : 0
	);
}

static void D3D11_GetBufferData(
	Refresh_Renderer *driverData,
	Refresh_Buffer *buffer,
	void* data,
	uint32_t dataLengthInBytes
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11Buffer *d3d11Buffer = (D3D11Buffer*) buffer;
	D3D11_BUFFER_DESC stagingBufferDesc;
	ID3D11Resource *stagingBuffer;
	D3D11_BOX srcBox = { 0, 0, 0, dataLengthInBytes, 1, 1 };
	D3D11_MAPPED_SUBRESOURCE mappedSubresource;
	HRESULT res;

	/* Create staging buffer */
	stagingBufferDesc.ByteWidth = dataLengthInBytes;
	stagingBufferDesc.Usage = D3D11_USAGE_STAGING;
	stagingBufferDesc.BindFlags = 0;
	stagingBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	stagingBufferDesc.MiscFlags = 0;
	stagingBufferDesc.StructureByteStride = 0;

	res = ID3D11Device_CreateBuffer(
		renderer->device,
		&stagingBufferDesc,
		NULL,
		(ID3D11Buffer**) &stagingBuffer
	);
	ERROR_CHECK_RETURN("Could not create staging buffer for readback", );

	/* Copy data into staging buffer */
	ID3D11DeviceContext_CopySubresourceRegion(
		renderer->immediateContext,
		stagingBuffer,
		0,
		0,
		0,
		0,
		(ID3D11Resource*) d3d11Buffer->handle,
		0,
		&srcBox
	);

	/* Read from the staging buffer */
	res = ID3D11DeviceContext_Map(
		renderer->immediateContext,
		stagingBuffer,
		0,
		D3D11_MAP_READ,
		0,
		&mappedSubresource
	);
	if (FAILED(res))
	{
		D3D11_INTERNAL_LogError(
			renderer->device,
			"Failed to map staging buffer for read!",
			res
		);
		ID3D11Buffer_Release(stagingBuffer);
		return;
	}

	SDL_memcpy(data, mappedSubresource.pData, dataLengthInBytes);

	ID3D11DeviceContext_Unmap(
		renderer->immediateContext,
		stagingBuffer,
		0
	);

	/* Clean up the staging buffer */
	ID3D11Resource_Release(stagingBuffer);
}

/* Uniforms */

static uint8_t D3D11_INTERNAL_CreateUniformBuffer(
	D3D11Renderer *renderer
) {
	D3D11_BUFFER_DESC bufferDesc;
	ID3D11Buffer *bufferHandle;
	D3D11UniformBuffer *uniformBuffer;
	HRESULT res;

	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bufferDesc.ByteWidth = UBO_BUFFER_SIZE;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bufferDesc.MiscFlags = 0;
	bufferDesc.StructureByteStride = 0;
	bufferDesc.Usage = D3D11_USAGE_DYNAMIC;

	res = ID3D11Device_CreateBuffer(
		renderer->device,
		&bufferDesc,
		NULL,
		&bufferHandle
	);
	ERROR_CHECK_RETURN("Failed to create uniform buffer", 0);

	uniformBuffer = SDL_malloc(sizeof(D3D11UniformBuffer));
	uniformBuffer->offset = 0;
	uniformBuffer->hasDiscarded = 0;
	uniformBuffer->d3d11Buffer = SDL_malloc(sizeof(D3D11Buffer));
	uniformBuffer->d3d11Buffer->handle = bufferHandle;
	uniformBuffer->d3d11Buffer->size = UBO_BUFFER_SIZE;
	uniformBuffer->d3d11Buffer->uav = NULL;

	/* Add it to the available pool */
	if (renderer->availableUniformBufferCount >= renderer->availableUniformBufferCapacity)
	{
		renderer->availableUniformBufferCapacity *= 2;

		renderer->availableUniformBuffers = SDL_realloc(
			renderer->availableUniformBuffers,
			sizeof(D3D11UniformBuffer*) * renderer->availableUniformBufferCapacity
		);
	}

	renderer->availableUniformBuffers[renderer->availableUniformBufferCount] = uniformBuffer;
	renderer->availableUniformBufferCount += 1;

	return 1;
}

static uint8_t D3D11_INTERNAL_AcquireUniformBuffer(
	D3D11Renderer *renderer,
	D3D11CommandBuffer *commandBuffer,
	D3D11UniformBuffer **uniformBufferToBind,
	uint64_t blockSize
) {
	D3D11UniformBuffer *uniformBuffer;

	/* Acquire a uniform buffer from the pool */
	SDL_LockMutex(renderer->uniformBufferLock);

	if (renderer->availableUniformBufferCount == 0)
	{
		if (!D3D11_INTERNAL_CreateUniformBuffer(renderer))
		{
			SDL_UnlockMutex(renderer->uniformBufferLock);
			Refresh_LogError("Failed to create uniform buffer!");
			return 0;
		}
	}

	uniformBuffer = renderer->availableUniformBuffers[renderer->availableUniformBufferCount - 1];
	renderer->availableUniformBufferCount -= 1;

	SDL_UnlockMutex(renderer->uniformBufferLock);

	/* Reset the uniform buffer */
	uniformBuffer->hasDiscarded = 0;
	uniformBuffer->offset = 0;

	/* Bind the uniform buffer to the command buffer */
	if (commandBuffer->boundUniformBufferCount >= commandBuffer->boundUniformBufferCapacity)
	{
		commandBuffer->boundUniformBufferCapacity *= 2;
		commandBuffer->boundUniformBuffers = SDL_realloc(
			commandBuffer->boundUniformBuffers,
			sizeof(D3D11UniformBuffer*) * commandBuffer->boundUniformBufferCapacity
		);
	}
	commandBuffer->boundUniformBuffers[commandBuffer->boundUniformBufferCount] = uniformBuffer;
	commandBuffer->boundUniformBufferCount += 1;

	*uniformBufferToBind = uniformBuffer;

	return 1;
}

static void D3D11_INTERNAL_SetUniformBufferData(
	D3D11Renderer *renderer,
	D3D11CommandBuffer *commandBuffer,
	D3D11UniformBuffer *uniformBuffer,
	void* data,
	uint32_t dataLength
) {
	D3D11_MAPPED_SUBRESOURCE subres;

	HRESULT res = ID3D11DeviceContext_Map(
		commandBuffer->context,
		(ID3D11Resource*) uniformBuffer->d3d11Buffer->handle,
		0,
		uniformBuffer->hasDiscarded ? D3D11_MAP_WRITE_NO_OVERWRITE : D3D11_MAP_WRITE_DISCARD,
		0,
		&subres
	);
	ERROR_CHECK_RETURN("Could not map buffer for writing!", );

	SDL_memcpy(
		(uint8_t*) subres.pData + uniformBuffer->offset,
		data,
		dataLength
	);

	ID3D11DeviceContext_Unmap(
		commandBuffer->context,
		(ID3D11Resource*) uniformBuffer->d3d11Buffer->handle,
		0
	);

	uniformBuffer->hasDiscarded = 1;
}

static uint32_t D3D11_PushVertexShaderUniforms(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	void *data,
	uint32_t dataLengthInBytes
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11CommandBuffer *d3d11CommandBuffer = (D3D11CommandBuffer*) commandBuffer;
	D3D11GraphicsPipeline *graphicsPipeline = d3d11CommandBuffer->graphicsPipeline;
	uint32_t offset;

	if (d3d11CommandBuffer->vertexUniformBuffer->offset + graphicsPipeline->vertexUniformBlockSize >= UBO_BUFFER_SIZE)
	{
		/* Out of space! Get a new uniform buffer. */
		D3D11_INTERNAL_AcquireUniformBuffer(
			renderer,
			d3d11CommandBuffer,
			&d3d11CommandBuffer->vertexUniformBuffer,
			graphicsPipeline->vertexUniformBlockSize
		);
	}

	offset = d3d11CommandBuffer->vertexUniformBuffer->offset;

	D3D11_INTERNAL_SetUniformBufferData(
		renderer,
		d3d11CommandBuffer,
		d3d11CommandBuffer->vertexUniformBuffer,
		data,
		dataLengthInBytes
	);

	d3d11CommandBuffer->vertexUniformBuffer->offset += graphicsPipeline->vertexUniformBlockSize;

	return offset;
}

static uint32_t D3D11_PushFragmentShaderUniforms(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	void *data,
	uint32_t dataLengthInBytes
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11CommandBuffer *d3d11CommandBuffer = (D3D11CommandBuffer*) commandBuffer;
	D3D11GraphicsPipeline *graphicsPipeline = d3d11CommandBuffer->graphicsPipeline;
	uint32_t offset;

	if (d3d11CommandBuffer->fragmentUniformBuffer->offset + graphicsPipeline->fragmentUniformBlockSize >= UBO_BUFFER_SIZE)
	{
		/* Out of space! Get a new uniform buffer. */
		D3D11_INTERNAL_AcquireUniformBuffer(
			renderer,
			d3d11CommandBuffer,
			&d3d11CommandBuffer->fragmentUniformBuffer,
			graphicsPipeline->fragmentUniformBlockSize
		);
	}

	offset = d3d11CommandBuffer->fragmentUniformBuffer->offset;

	D3D11_INTERNAL_SetUniformBufferData(
		renderer,
		d3d11CommandBuffer,
		d3d11CommandBuffer->fragmentUniformBuffer,
		data,
		dataLengthInBytes
	);

	d3d11CommandBuffer->fragmentUniformBuffer->offset += graphicsPipeline->fragmentUniformBlockSize;

	return offset;
}

static uint32_t D3D11_PushComputeShaderUniforms(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	void *data,
	uint32_t dataLengthInBytes
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11CommandBuffer *d3d11CommandBuffer = (D3D11CommandBuffer*) commandBuffer;
	D3D11ComputePipeline *computePipeline = d3d11CommandBuffer->computePipeline;
	uint32_t offset;

	if (d3d11CommandBuffer->computeUniformBuffer->offset + computePipeline->computeUniformBlockSize >= UBO_BUFFER_SIZE)
	{
		/* Out of space! Get a new uniform buffer. */
		D3D11_INTERNAL_AcquireUniformBuffer(
			renderer,
			d3d11CommandBuffer,
			&d3d11CommandBuffer->computeUniformBuffer,
			computePipeline->computeUniformBlockSize
		);
	}

	offset = d3d11CommandBuffer->computeUniformBuffer->offset;

	D3D11_INTERNAL_SetUniformBufferData(
		renderer,
		d3d11CommandBuffer,
		d3d11CommandBuffer->computeUniformBuffer,
		data,
		dataLengthInBytes
	);

	d3d11CommandBuffer->computeUniformBuffer->offset += (uint32_t) computePipeline->computeUniformBlockSize; /* FIXME: Is this cast safe? */

	return offset;
}

/* Samplers */

static void D3D11_BindVertexSamplers(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Texture **pTextures,
	Refresh_Sampler **pSamplers
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11CommandBuffer *d3d11CommandBuffer = (D3D11CommandBuffer*) commandBuffer;
	ID3D11ShaderResourceView* srvs[MAX_VERTEXTEXTURE_SAMPLERS];
	ID3D11SamplerState* d3d11Samplers[MAX_VERTEXTEXTURE_SAMPLERS];

	int32_t numVertexSamplers = d3d11CommandBuffer->graphicsPipeline->numVertexSamplers;

	for (int32_t i = 0; i < numVertexSamplers; i += 1)
	{
		srvs[i] = ((D3D11Texture*) pTextures[i])->shaderView;
		d3d11Samplers[i] = ((D3D11Sampler*) pSamplers[i])->handle;
	}

	ID3D11DeviceContext_VSSetShaderResources(
		d3d11CommandBuffer->context,
		0,
		numVertexSamplers,
		srvs
	);

	ID3D11DeviceContext_VSSetSamplers(
		d3d11CommandBuffer->context,
		0,
		numVertexSamplers,
		d3d11Samplers
	);
}

static void D3D11_BindFragmentSamplers(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Texture **pTextures,
	Refresh_Sampler **pSamplers
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11CommandBuffer *d3d11CommandBuffer = (D3D11CommandBuffer*) commandBuffer;
	ID3D11ShaderResourceView* srvs[MAX_TEXTURE_SAMPLERS];
	ID3D11SamplerState* d3d11Samplers[MAX_TEXTURE_SAMPLERS];

	int32_t numFragmentSamplers = d3d11CommandBuffer->graphicsPipeline->numFragmentSamplers;

	for (int32_t i = 0; i < numFragmentSamplers; i += 1)
	{
		srvs[i] = ((D3D11Texture*) pTextures[i])->shaderView;
		d3d11Samplers[i] = ((D3D11Sampler*) pSamplers[i])->handle;
	}

	ID3D11DeviceContext_PSSetShaderResources(
		d3d11CommandBuffer->context,
		0,
		numFragmentSamplers,
		srvs
	);

	ID3D11DeviceContext_PSSetSamplers(
		d3d11CommandBuffer->context,
		0,
		numFragmentSamplers,
		d3d11Samplers
	);
}

/* Disposal */

static void D3D11_QueueDestroyTexture(
	Refresh_Renderer *driverData,
	Refresh_Texture *texture
) {
	D3D11Texture *d3d11Texture = (D3D11Texture*) texture;

	if (d3d11Texture->shaderView)
	{
		ID3D11ShaderResourceView_Release(d3d11Texture->shaderView);
	}
	if (d3d11Texture->unorderedAccessView)
	{
		ID3D11UnorderedAccessView_Release(d3d11Texture->unorderedAccessView);
	}
	if (d3d11Texture->targetViewCount > 0)
	{
		for (uint32_t i = 0; i < d3d11Texture->targetViewCount; i += 1)
		{
			ID3D11View_Release(d3d11Texture->targetViews[i].view);
		}
		SDL_free(d3d11Texture->targetViews);
	}

	ID3D11Resource_Release(d3d11Texture->handle);

	SDL_free(d3d11Texture);
}

static void D3D11_QueueDestroySampler(
	Refresh_Renderer *driverData,
	Refresh_Sampler *sampler
) {
	D3D11Sampler *d3d11Sampler = (D3D11Sampler*) sampler;
	ID3D11SamplerState_Release(d3d11Sampler->handle);
	SDL_free(d3d11Sampler);
}

static void D3D11_QueueDestroyBuffer(
	Refresh_Renderer *driverData,
	Refresh_Buffer *buffer
) {
	D3D11Buffer *d3d11Buffer = (D3D11Buffer*) buffer;

	if (d3d11Buffer->uav)
	{
		ID3D11UnorderedAccessView_Release(d3d11Buffer->uav);
	}

	ID3D11Buffer_Release(d3d11Buffer->handle);

	SDL_free(d3d11Buffer);
}

static void D3D11_QueueDestroyShaderModule(
	Refresh_Renderer *driverData,
	Refresh_ShaderModule *shaderModule
) {
	D3D11ShaderModule *d3dShaderModule = (D3D11ShaderModule*) shaderModule;

	if (d3dShaderModule->shader)
	{
		ID3D11DeviceChild_Release(d3dShaderModule->shader);
	}
	if (d3dShaderModule->blob)
	{
		ID3D10Blob_Release(d3dShaderModule->blob);
	}

	SDL_free(d3dShaderModule->shaderSource);
	SDL_free(d3dShaderModule);
}

static void D3D11_QueueDestroyComputePipeline(
	Refresh_Renderer *driverData,
	Refresh_ComputePipeline *computePipeline
) {
	D3D11ComputePipeline *d3d11ComputePipeline = (D3D11ComputePipeline*) computePipeline;
	SDL_free(d3d11ComputePipeline);
}

static void D3D11_QueueDestroyGraphicsPipeline(
	Refresh_Renderer *driverData,
	Refresh_GraphicsPipeline *graphicsPipeline
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11GraphicsPipeline *d3d11GraphicsPipeline = (D3D11GraphicsPipeline*) graphicsPipeline;

	ID3D11BlendState_Release(d3d11GraphicsPipeline->colorAttachmentBlendState);
	ID3D11DepthStencilState_Release(d3d11GraphicsPipeline->depthStencilState);
	ID3D11RasterizerState_Release(d3d11GraphicsPipeline->rasterizerState);

	if (d3d11GraphicsPipeline->inputLayout)
	{
		ID3D11InputLayout_Release(d3d11GraphicsPipeline->inputLayout);
	}
	if (d3d11GraphicsPipeline->vertexStrides)
	{
		SDL_free(d3d11GraphicsPipeline->vertexStrides);
	}

	SDL_free(d3d11GraphicsPipeline);
}

/* Graphics State */

static void D3D11_INTERNAL_AllocateCommandBuffers(
	D3D11Renderer *renderer,
	uint32_t allocateCount
) {
	D3D11CommandBuffer *commandBuffer;
	HRESULT res;

	renderer->availableCommandBufferCapacity += allocateCount;

	renderer->availableCommandBuffers = SDL_realloc(
		renderer->availableCommandBuffers,
		sizeof(D3D11CommandBuffer*) * renderer->availableCommandBufferCapacity
	);

	for (uint32_t i = 0; i < allocateCount; i += 1)
	{
		commandBuffer = SDL_malloc(sizeof(D3D11CommandBuffer));

		/* Deferred Device Context */
		res = ID3D11Device1_CreateDeferredContext1(
			renderer->device,
			0,
			&commandBuffer->context
		);
		ERROR_CHECK("Could not create deferred context");

		/* Bound Uniform Buffers */
		commandBuffer->boundUniformBufferCapacity = 16;
		commandBuffer->boundUniformBufferCount = 0;
		commandBuffer->boundUniformBuffers = SDL_malloc(
			commandBuffer->boundUniformBufferCapacity * sizeof(D3D11UniformBuffer*)
		);

		renderer->availableCommandBuffers[renderer->availableCommandBufferCount] = commandBuffer;
		renderer->availableCommandBufferCount += 1;
	}
}

static D3D11CommandBuffer* D3D11_INTERNAL_GetInactiveCommandBufferFromPool(
	D3D11Renderer *renderer
) {
	D3D11CommandBuffer *commandBuffer;

	if (renderer->availableCommandBufferCount == 0)
	{
		D3D11_INTERNAL_AllocateCommandBuffers(
			renderer,
			renderer->availableCommandBufferCapacity
		);
	}

	commandBuffer = renderer->availableCommandBuffers[renderer->availableCommandBufferCount - 1];
	renderer->availableCommandBufferCount -= 1;

	return commandBuffer;
}

static uint8_t D3D11_INTERNAL_CreateFence(
	D3D11Renderer *renderer
) {
	D3D11_QUERY_DESC queryDesc;
	ID3D11Query *queryHandle;
	D3D11Fence* fence;
	HRESULT res;

	queryDesc.Query = D3D11_QUERY_EVENT;
	queryDesc.MiscFlags = 0;
	res = ID3D11Device_CreateQuery(
		renderer->device,
		&queryDesc,
		&queryHandle
	);
	ERROR_CHECK_RETURN("Could not create query", 0);

	fence = SDL_malloc(sizeof(D3D11Fence));
	fence->handle = queryHandle;

	/* Add it to the available pool */
	if (renderer->availableFenceCount >= renderer->availableFenceCapacity)
	{
		renderer->availableFenceCapacity *= 2;

		renderer->availableFences = SDL_realloc(
			renderer->availableFences,
			sizeof(D3D11Fence*) * renderer->availableFenceCapacity
		);
	}

	renderer->availableFences[renderer->availableFenceCount] = fence;
	renderer->availableFenceCount += 1;

	return 1;
}

static uint8_t D3D11_INTERNAL_AcquireFence(
	D3D11Renderer *renderer,
	D3D11CommandBuffer *commandBuffer
) {
	D3D11Fence *fence;

	/* Acquire a fence from the pool */
	SDL_LockMutex(renderer->fenceLock);

	if (renderer->availableFenceCount == 0)
	{
		if (!D3D11_INTERNAL_CreateFence(renderer))
		{
			SDL_UnlockMutex(renderer->fenceLock);
			Refresh_LogError("Failed to create fence!");
			return 0;
		}
	}

	fence = renderer->availableFences[renderer->availableFenceCount - 1];
	renderer->availableFenceCount -= 1;

	SDL_UnlockMutex(renderer->fenceLock);

	/* Associate the fence with the command buffer */
	commandBuffer->fence = fence;

	return 1;
}

static Refresh_CommandBuffer* D3D11_AcquireCommandBuffer(
	Refresh_Renderer *driverData
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11CommandBuffer *commandBuffer;

	SDL_LockMutex(renderer->acquireCommandBufferLock);

	commandBuffer = D3D11_INTERNAL_GetInactiveCommandBufferFromPool(renderer);

	commandBuffer->threadID = SDL_ThreadID();
	commandBuffer->swapchainData = NULL;
	commandBuffer->graphicsPipeline = NULL;
	commandBuffer->computePipeline = NULL;
	commandBuffer->vertexUniformBuffer = NULL;
	commandBuffer->fragmentUniformBuffer = NULL;
	commandBuffer->computeUniformBuffer = NULL;
	commandBuffer->dsView = NULL;

	for (uint32_t i = 0; i < MAX_COLOR_TARGET_BINDINGS; i += 1)
	{
		commandBuffer->rtViews[i] = NULL;
	}

	D3D11_INTERNAL_AcquireFence(renderer, commandBuffer);
	commandBuffer->autoReleaseFence = 1;

	SDL_UnlockMutex(renderer->acquireCommandBufferLock);

	return (Refresh_CommandBuffer*) commandBuffer;
}

static ID3D11RenderTargetView* D3D11_INTERNAL_FetchRTV(
	D3D11Renderer *renderer,
	Refresh_ColorAttachmentInfo *info
) {
	D3D11Texture *texture = (D3D11Texture*) info->texture;
	D3D11TargetView *targetView;
	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc;
	ID3D11RenderTargetView *rtv;
	HRESULT res;

	/* Does this target already exist? */
	for (uint32_t i = 0; i < texture->targetViewCount; i += 1)
	{
		targetView = &texture->targetViews[i];

		if (	targetView->depth == info->depth &&
			targetView->layer == info->layer &&
			targetView->level == info->level	)
		{
			return (ID3D11RenderTargetView*) targetView->view;
		}
	}

	/* Let's create a new RTV! */
	rtvDesc.Format = RefreshToD3D11_TextureFormat[texture->format];
	if (texture->isCube)
	{
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY; /* FIXME: MSAA? */
		rtvDesc.Texture2DArray.ArraySize = 1;
		rtvDesc.Texture2DArray.FirstArraySlice = info->layer;
		rtvDesc.Texture2DArray.MipSlice = info->level;
	}
	else if (texture->depth > 1)
	{
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
		rtvDesc.Texture3D.MipSlice = info->level;
		rtvDesc.Texture3D.FirstWSlice = info->depth;
		rtvDesc.Texture3D.WSize = 1;
	}
	else
	{
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = info->level;
	}

	res = ID3D11Device_CreateRenderTargetView(
		renderer->device,
		texture->handle,
		&rtvDesc,
		&rtv
	);
	ERROR_CHECK_RETURN("Could not create RTV!", NULL);

	/* Create the D3D11TargetView to wrap our new RTV */
	if (texture->targetViewCount >= texture->targetViewCapacity)
	{
		texture->targetViewCapacity *= 2;
		texture->targetViews = SDL_realloc(
			texture->targetViews,
			sizeof(D3D11TargetView) * texture->targetViewCapacity
		);
	}

	targetView = &texture->targetViews[texture->targetViewCount];
	targetView->depth = info->depth;
	targetView->layer = info->layer;
	targetView->level = info->level;
	targetView->view = (ID3D11View*) rtv;

	texture->targetViewCount += 1;

	return rtv;
}

static ID3D11DepthStencilView* D3D11_INTERNAL_FetchDSV(
	D3D11Renderer* renderer,
	Refresh_DepthStencilAttachmentInfo* info
) {
	D3D11Texture *texture = (D3D11Texture*) info->texture;
	D3D11TargetView *targetView;
	D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	ID3D11DepthStencilView *dsv;
	HRESULT res;

	/* Does this target already exist? */
	for (uint32_t i = 0; i < texture->targetViewCount; i += 1)
	{
		targetView = &texture->targetViews[i];

		if (	targetView->depth == info->depth &&
			targetView->layer == info->layer &&
			targetView->level == info->level	)
		{
			return (ID3D11DepthStencilView*) targetView->view;
		}
	}

	/* Let's create a new DSV! */
	dsvDesc.Format = RefreshToD3D11_TextureFormat[texture->format];
	dsvDesc.Flags = 0;
	if (texture->isCube)
	{
		dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY; /* FIXME: MSAA? */
		dsvDesc.Texture2DArray.ArraySize = 6;
		dsvDesc.Texture2DArray.FirstArraySlice = 0;
		dsvDesc.Texture2DArray.MipSlice = info->level;
	}
	else
	{
		dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		dsvDesc.Texture2D.MipSlice = info->level;
	}

	res = ID3D11Device_CreateDepthStencilView(
		renderer->device,
		texture->handle,
		&dsvDesc,
		&dsv
	);
	ERROR_CHECK_RETURN("Could not create DSV!", NULL);

	/* Create the D3D11TargetView to wrap our new DSV */
	if (texture->targetViewCount == texture->targetViewCapacity)
	{
		texture->targetViewCapacity *= 2;
		texture->targetViews = SDL_realloc(
			texture->targetViews,
			sizeof(D3D11TargetView) * texture->targetViewCapacity
		);
	}

	targetView = &texture->targetViews[texture->targetViewCount];
	targetView->depth = info->depth;
	targetView->layer = info->layer;
	targetView->level = info->level;
	targetView->view = (ID3D11View*) dsv;

	texture->targetViewCount += 1;

	return dsv;
}

static void D3D11_BeginRenderPass(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_ColorAttachmentInfo *colorAttachmentInfos,
	uint32_t colorAttachmentCount,
	Refresh_DepthStencilAttachmentInfo *depthStencilAttachmentInfo
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11CommandBuffer *d3d11CommandBuffer = (D3D11CommandBuffer*) commandBuffer;
	float clearColors[4];
	D3D11_CLEAR_FLAG dsClearFlags;
	uint32_t vpWidth = UINT_MAX;
	uint32_t vpHeight = UINT_MAX;
	D3D11_VIEWPORT viewport;
	D3D11_RECT scissorRect;

	/* Clear the bound RTs for the current command buffer */
	for (uint32_t i = 0; i < MAX_COLOR_TARGET_BINDINGS; i += 1)
	{
		d3d11CommandBuffer->rtViews[i] = NULL;
	}
	d3d11CommandBuffer->dsView = NULL;

	/* Get RTVs for the color attachments */
	for (uint32_t i = 0; i < colorAttachmentCount; i += 1)
	{
		d3d11CommandBuffer->rtViews[i] = D3D11_INTERNAL_FetchRTV(
			renderer,
			&colorAttachmentInfos[i]
		);
	}

	/* Get the DSV for the depth stencil attachment, if applicable */
	if (depthStencilAttachmentInfo != NULL)
	{
		d3d11CommandBuffer->dsView = D3D11_INTERNAL_FetchDSV(
			renderer,
			depthStencilAttachmentInfo
		);
	}

	/* Actually set the RTs */
	ID3D11DeviceContext_OMSetRenderTargets(
		d3d11CommandBuffer->context,
		colorAttachmentCount,
		d3d11CommandBuffer->rtViews,
		d3d11CommandBuffer->dsView
	);

	/* Perform load ops on the RTs */
	for (uint32_t i = 0; i < colorAttachmentCount; i += 1)
	{
		if (colorAttachmentInfos[i].loadOp == REFRESH_LOADOP_CLEAR)
		{
			clearColors[0] = colorAttachmentInfos[i].clearColor.x;
			clearColors[1] = colorAttachmentInfos[i].clearColor.y;
			clearColors[2] = colorAttachmentInfos[i].clearColor.z;
			clearColors[3] = colorAttachmentInfos[i].clearColor.w;

			ID3D11DeviceContext_ClearRenderTargetView(
				d3d11CommandBuffer->context,
				d3d11CommandBuffer->rtViews[i],
				clearColors
			);
		}
	}

	if (d3d11CommandBuffer->dsView != NULL)
	{
		dsClearFlags = 0;
		if (depthStencilAttachmentInfo->loadOp == REFRESH_LOADOP_CLEAR)
		{
			dsClearFlags |= D3D11_CLEAR_DEPTH;
		}
		if (depthStencilAttachmentInfo->stencilLoadOp == REFRESH_LOADOP_CLEAR)
		{
			dsClearFlags |= D3D11_CLEAR_STENCIL;
		}

		if (dsClearFlags != 0)
		{
			ID3D11DeviceContext_ClearDepthStencilView(
				d3d11CommandBuffer->context,
				d3d11CommandBuffer->dsView,
				dsClearFlags,
				depthStencilAttachmentInfo->depthStencilClearValue.depth,
				(uint8_t) depthStencilAttachmentInfo->depthStencilClearValue.stencil
			);
		}
	}

	/* The viewport cannot be larger than the smallest attachment. */
	for (uint32_t i = 0; i < colorAttachmentCount; i += 1)
	{
		D3D11Texture *texture = (D3D11Texture*) colorAttachmentInfos[i].texture;
		uint32_t w = texture->width >> colorAttachmentInfos[i].level;
		uint32_t h = texture->height >> colorAttachmentInfos[i].level;

		if (w < vpWidth)
		{
			vpWidth = w;
		}

		if (h < vpHeight)
		{
			vpHeight = h;
		}
	}

	if (depthStencilAttachmentInfo != NULL)
	{
		D3D11Texture *texture = (D3D11Texture*) depthStencilAttachmentInfo->texture;
		uint32_t w = texture->width >> depthStencilAttachmentInfo->level;
		uint32_t h = texture->height >> depthStencilAttachmentInfo->level;

		if (w < vpWidth)
		{
			vpWidth = w;
		}

		if (h < vpHeight)
		{
			vpHeight = h;
		}
	}

	/* Set default viewport and scissor state */
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.Width = (FLOAT) vpWidth;
	viewport.Height = (FLOAT) vpHeight;
	viewport.MinDepth = 0;
	viewport.MaxDepth = 1;

	ID3D11DeviceContext_RSSetViewports(
		d3d11CommandBuffer->context,
		1,
		&viewport
	);

	scissorRect.left = 0;
	scissorRect.right = (LONG) viewport.Width;
	scissorRect.top = 0;
	scissorRect.bottom = (LONG) viewport.Height;

	ID3D11DeviceContext_RSSetScissorRects(
		d3d11CommandBuffer->context,
		1,
		&scissorRect
	);
}

static void D3D11_EndRenderPass(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer
) {
	D3D11CommandBuffer *d3d11CommandBuffer = (D3D11CommandBuffer*) commandBuffer;

	d3d11CommandBuffer->vertexUniformBuffer = NULL;
	d3d11CommandBuffer->fragmentUniformBuffer = NULL;
	d3d11CommandBuffer->computeUniformBuffer = NULL;

	/* FIXME: Resolve MSAA here! */
	/* FIXME: Anything else we need to do...? */
}

static void D3D11_BindGraphicsPipeline(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_GraphicsPipeline *graphicsPipeline
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11CommandBuffer *d3d11CommandBuffer = (D3D11CommandBuffer*) commandBuffer;
	D3D11GraphicsPipeline *pipeline = (D3D11GraphicsPipeline*) graphicsPipeline;

	d3d11CommandBuffer->graphicsPipeline = pipeline;

	if (pipeline->vertexUniformBlockSize == 0)
	{
		d3d11CommandBuffer->vertexUniformBuffer = NULL;
	}
	else
	{
		D3D11_INTERNAL_AcquireUniformBuffer(
			renderer,
			d3d11CommandBuffer,
			&d3d11CommandBuffer->vertexUniformBuffer,
			pipeline->vertexUniformBlockSize
		);
	}

	if (pipeline->fragmentUniformBlockSize == 0)
	{
		d3d11CommandBuffer->fragmentUniformBuffer = NULL;
	}
	else
	{
		D3D11_INTERNAL_AcquireUniformBuffer(
			renderer,
			d3d11CommandBuffer,
			&d3d11CommandBuffer->fragmentUniformBuffer,
			pipeline->fragmentUniformBlockSize
		);
	}

	ID3D11DeviceContext_OMSetBlendState(
		d3d11CommandBuffer->context,
		pipeline->colorAttachmentBlendState,
		pipeline->blendConstants,
		pipeline->multisampleState.sampleMask
	);

	ID3D11DeviceContext_OMSetDepthStencilState(
		d3d11CommandBuffer->context,
		pipeline->depthStencilState,
		pipeline->stencilRef
	);

	ID3D11DeviceContext_IASetPrimitiveTopology(
		d3d11CommandBuffer->context,
		RefreshToD3D11_PrimitiveType[pipeline->primitiveType]
	);

	ID3D11DeviceContext_IASetInputLayout(
		d3d11CommandBuffer->context,
		pipeline->inputLayout
	);

	ID3D11DeviceContext_RSSetState(
		d3d11CommandBuffer->context,
		pipeline->rasterizerState
	);

	ID3D11DeviceContext_VSSetShader(
		d3d11CommandBuffer->context,
		pipeline->vertexShader,
		NULL,
		0
	);

	ID3D11DeviceContext_PSSetShader(
		d3d11CommandBuffer->context,
		pipeline->fragmentShader,
		NULL,
		0
	);
}

static void D3D11_SetViewport(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Viewport *viewport
) {
	D3D11CommandBuffer *d3d11CommandBuffer = (D3D11CommandBuffer*) commandBuffer;
	D3D11_VIEWPORT vp =
	{
		viewport->x,
		viewport->y,
		viewport->w,
		viewport->h,
		viewport->minDepth,
		viewport->maxDepth
	};

	ID3D11DeviceContext_RSSetViewports(
		d3d11CommandBuffer->context,
		1,
		&vp
	);
}

static void D3D11_SetScissor(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Rect *scissor
) {
	D3D11CommandBuffer *d3d11CommandBuffer = (D3D11CommandBuffer*) commandBuffer;
	D3D11_RECT rect =
	{
		scissor->x,
		scissor->y,
		scissor->x + scissor->w,
		scissor->y + scissor->h
	};

	ID3D11DeviceContext_RSSetScissorRects(
		d3d11CommandBuffer->context,
		1,
		&rect
	);
}

static void D3D11_BindVertexBuffers(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	uint32_t firstBinding,
	uint32_t bindingCount,
	Refresh_Buffer **pBuffers,
	uint64_t *pOffsets
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11CommandBuffer *d3d11CommandBuffer = (D3D11CommandBuffer*) commandBuffer;
	ID3D11Buffer *bufferHandles[MAX_BUFFER_BINDINGS];

	for (uint32_t i = 0; i < bindingCount; i += 1)
	{
		bufferHandles[i] = ((D3D11Buffer*) pBuffers[i])->handle;
	}

	ID3D11DeviceContext_IASetVertexBuffers(
		d3d11CommandBuffer->context,
		firstBinding,
		bindingCount,
		bufferHandles,
		&d3d11CommandBuffer->graphicsPipeline->vertexStrides[firstBinding],
		(UINT*) pOffsets
	);
}

static void D3D11_BindIndexBuffer(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Buffer *buffer,
	uint64_t offset,
	Refresh_IndexElementSize indexElementSize
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11CommandBuffer *d3d11CommandBuffer = (D3D11CommandBuffer*) commandBuffer;
	D3D11Buffer *d3d11Buffer = (D3D11Buffer*) buffer;

	ID3D11DeviceContext_IASetIndexBuffer(
		d3d11CommandBuffer->context,
		d3d11Buffer->handle,
		RefreshToD3D11_IndexType[indexElementSize],
		(UINT) offset
	);
}

/* Compute State */

static void D3D11_BindComputePipeline(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_ComputePipeline *computePipeline
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11CommandBuffer *d3d11CommandBuffer = (D3D11CommandBuffer*) commandBuffer;
	D3D11ComputePipeline *pipeline = (D3D11ComputePipeline*) computePipeline;

	d3d11CommandBuffer->computePipeline = pipeline;

	if (pipeline->computeUniformBlockSize == 0)
	{
		d3d11CommandBuffer->computeUniformBuffer = NULL;
	}
	else
	{
		D3D11_INTERNAL_AcquireUniformBuffer(
			renderer,
			d3d11CommandBuffer,
			&d3d11CommandBuffer->computeUniformBuffer,
			pipeline->computeUniformBlockSize
		);
	}

	ID3D11DeviceContext_CSSetShader(
		d3d11CommandBuffer->context,
		pipeline->computeShader,
		NULL,
		0
	);
}

static void D3D11_BindComputeBuffers(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Buffer **pBuffers
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11CommandBuffer *d3d11CommandBuffer = (D3D11CommandBuffer*) commandBuffer;
	ID3D11UnorderedAccessView* uavs[MAX_BUFFER_BINDINGS]; /* FIXME: Is this limit right? */

	int32_t numBuffers = d3d11CommandBuffer->computePipeline->numBuffers;

	for (int32_t i = 0; i < numBuffers; i += 1)
	{
		uavs[i] = ((D3D11Buffer*) pBuffers[i])->uav;
	}

	ID3D11DeviceContext_CSSetUnorderedAccessViews(
		d3d11CommandBuffer->context,
		0,
		numBuffers,
		uavs,
		NULL
	);
}

static void D3D11_BindComputeTextures(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Texture **pTextures
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11CommandBuffer *d3d11CommandBuffer = (D3D11CommandBuffer*) commandBuffer;
	ID3D11UnorderedAccessView *uavs[MAX_TEXTURE_SAMPLERS]; /* FIXME: Is this limit right? */

	int32_t numTextures = d3d11CommandBuffer->computePipeline->numTextures;

	for (int32_t i = 0; i < numTextures; i += 1)
	{
		uavs[i] = ((D3D11Texture*) pTextures[i])->unorderedAccessView;
	}

	ID3D11DeviceContext_CSSetUnorderedAccessViews(
		d3d11CommandBuffer->context,
		0,
		numTextures,
		uavs,
		NULL
	);
}

/* Window and Swapchain Management */

static D3D11WindowData* D3D11_INTERNAL_FetchWindowData(
	void *windowHandle
) {
	return (D3D11WindowData*) SDL_GetWindowData(windowHandle, WINDOW_DATA);
}

static uint8_t D3D11_INTERNAL_InitializeSwapchainTexture(
	D3D11Renderer *renderer,
	IDXGISwapChain *swapchain,
	D3D11Texture *pTexture
) {
	ID3D11Texture2D *swapchainTexture;
	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc;
	D3D11_TEXTURE2D_DESC textureDesc;
	ID3D11RenderTargetView *rtv;
	HRESULT res;

	/* Clear all the texture data */
	SDL_zerop(pTexture);

	/* Grab the buffer from the swapchain */
	res = IDXGISwapChain_GetBuffer(
		swapchain,
		0,
		&D3D_IID_ID3D11Texture2D,
		(void**) &swapchainTexture
	);
	ERROR_CHECK_RETURN("Could not get buffer from swapchain!", 0);

	/* Create the RTV for the swapchain */
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;

	res = ID3D11Device_CreateRenderTargetView(
		renderer->device,
		(ID3D11Resource*) swapchainTexture,
		&rtvDesc,
		&rtv
	);
	if (FAILED(res))
	{
		ID3D11Texture2D_Release(swapchainTexture);
		D3D11_INTERNAL_LogError(renderer->device, "Swapchain RTV creation failed", res);
		return 0;
	}

	/* Fill out the texture struct */
	pTexture->handle = NULL; /* The texture does not "own" the swapchain texture, and it can change dynamically. */
	pTexture->shaderView = NULL;
	pTexture->unorderedAccessView = NULL;
	pTexture->targetViewCapacity = 1;
	pTexture->targetViewCount = 1;
	pTexture->targetViews = SDL_calloc(1, sizeof(D3D11TargetView));
	pTexture->targetViews[0].view = (ID3D11View*) rtv;

	ID3D11Texture2D_GetDesc(swapchainTexture, &textureDesc);
	pTexture->levelCount = textureDesc.MipLevels;
	pTexture->width = textureDesc.Width;
	pTexture->height = textureDesc.Height;
	pTexture->depth = 1;
	pTexture->isCube = 0;
	pTexture->isRenderTarget = 1;

	/* Cleanup */
	ID3D11Texture2D_Release(swapchainTexture);

	return 1;
}

static uint8_t D3D11_INTERNAL_CreateSwapchain(
	D3D11Renderer *renderer,
	D3D11WindowData *windowData
) {
	SDL_SysWMinfo info;
	HWND dxgiHandle;
	int width, height;
	DXGI_SWAP_CHAIN_DESC swapchainDesc;
	IDXGIFactory1 *pParent;
	IDXGISwapChain *swapchain;
	D3D11SwapchainData *swapchainData;
	HRESULT res;

	/* Get the DXGI handle */
	SDL_VERSION(&info.version);
	SDL_GetWindowWMInfo((SDL_Window*) windowData->windowHandle, &info);
	dxgiHandle = info.info.win.window;

	/* Get the window size */
	SDL_GetWindowSize((SDL_Window*) windowData->windowHandle, &width, &height);

	/* Initialize the swapchain buffer descriptor */
	swapchainDesc.BufferDesc.Width = 0;
	swapchainDesc.BufferDesc.Height = 0;
	swapchainDesc.BufferDesc.RefreshRate.Numerator = 0;
	swapchainDesc.BufferDesc.RefreshRate.Denominator = 0;
	swapchainDesc.BufferDesc.Format = RefreshToD3D11_TextureFormat[REFRESH_TEXTUREFORMAT_R8G8B8A8];
	swapchainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapchainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

	/* Initialize the rest of the swapchain descriptor */
	swapchainDesc.SampleDesc.Count = 1;
	swapchainDesc.SampleDesc.Quality = 0;
	swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapchainDesc.BufferCount = 3;
	swapchainDesc.OutputWindow = dxgiHandle;
	swapchainDesc.Windowed = 1;
	swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	swapchainDesc.Flags = 0;

	/* Create the swapchain! */
	res = IDXGIFactory1_CreateSwapChain(
		(IDXGIFactory1*) renderer->factory,
		(IUnknown*) renderer->device,
		&swapchainDesc,
		&swapchain
	);
	ERROR_CHECK("Could not create swapchain");

	/*
	 * The swapchain's parent is a separate factory from the factory that
	 * we used to create the swapchain, and only that parent can be used to
	 * set the window association. Trying to set an association on our factory
	 * will silently fail and doesn't even verify arguments or return errors.
	 * See https://gamedev.net/forums/topic/634235-dxgidisabling-altenter/4999955/
	 */
	res = IDXGISwapChain_GetParent(
		swapchain,
		&D3D_IID_IDXGIFactory1,
		(void**) &pParent
	);
	if (FAILED(res))
	{
		Refresh_LogWarn(
			"Could not get swapchain parent! Error Code: %08X",
			res
		);
	}
	else
	{
		/* Disable DXGI window crap */
		res = IDXGIFactory1_MakeWindowAssociation(
			pParent,
			dxgiHandle,
			DXGI_MWA_NO_WINDOW_CHANGES
		);
		if (FAILED(res))
		{
			Refresh_LogWarn(
				"MakeWindowAssociation failed! Error Code: %08X",
				res
			);
		}

		/* We're done with the parent now */
		IDXGIFactory1_Release(pParent);
	}

	/* Create the swapchain data */
	swapchainData = (D3D11SwapchainData*) SDL_malloc(sizeof(D3D11SwapchainData));
	swapchainData->swapchain = swapchain;

	if (!D3D11_INTERNAL_InitializeSwapchainTexture(
		renderer,
		swapchain,
		&swapchainData->texture
	)) {
		SDL_free(swapchainData);
		IDXGISwapChain_Release(swapchain);
		return 0;
	}

	windowData->swapchainData = swapchainData;
	return 1;
}

static uint8_t D3D11_INTERNAL_ResizeSwapchain(
	D3D11Renderer *renderer,
	D3D11SwapchainData *swapchainData,
	int32_t width,
	int32_t height
) {
	HRESULT res;

	/* Release the old RTV */
	ID3D11RenderTargetView_Release(swapchainData->texture.targetViews[0].view);
	SDL_free(swapchainData->texture.targetViews);

	/* Resize the swapchain */
	res = IDXGISwapChain_ResizeBuffers(
		swapchainData->swapchain,
		0,			/* Keep buffer count the same */
		width,
		height,
		DXGI_FORMAT_UNKNOWN,	/* Keep the old format */
		0
	);
	ERROR_CHECK_RETURN("Could not resize swapchain buffers", 0);

	/* Create the Refresh-side texture for the swapchain */
	return D3D11_INTERNAL_InitializeSwapchainTexture(
		renderer,
		swapchainData->swapchain,
		&swapchainData->texture
	);
}

static void D3D11_INTERNAL_DestroySwapchain(
	D3D11Renderer *renderer,
	D3D11WindowData *windowData
) {
	D3D11SwapchainData *swapchainData;

	if (windowData == NULL)
	{
		return;
	}

	swapchainData = windowData->swapchainData;

	if (swapchainData == NULL)
	{
		return;
	}

	ID3D11RenderTargetView_Release(swapchainData->texture.targetViews[0].view);
	SDL_free(swapchainData->texture.targetViews);

	IDXGISwapChain_Release(swapchainData->swapchain);

	windowData->swapchainData = NULL;
	SDL_free(swapchainData);
}

static uint8_t D3D11_ClaimWindow(
	Refresh_Renderer *driverData,
	void *windowHandle,
	Refresh_PresentMode presentMode
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11WindowData *windowData = D3D11_INTERNAL_FetchWindowData(windowHandle);

	if (windowData == NULL)
	{
		windowData = (D3D11WindowData*) SDL_malloc(sizeof(D3D11WindowData));
		windowData->windowHandle = windowHandle;
		windowData->allowTearing = presentMode == REFRESH_PRESENTMODE_IMMEDIATE;

		if (D3D11_INTERNAL_CreateSwapchain(renderer, windowData))
		{
			SDL_SetWindowData((SDL_Window*) windowHandle, WINDOW_DATA, windowData);

			if (renderer->claimedWindowCount >= renderer->claimedWindowCapacity)
			{
				renderer->claimedWindowCapacity *= 2;
				renderer->claimedWindows = SDL_realloc(
					renderer->claimedWindows,
					renderer->claimedWindowCapacity * sizeof(D3D11WindowData*)
				);
			}

			renderer->claimedWindows[renderer->claimedWindowCount] = windowData;
			renderer->claimedWindowCount += 1;

			return 1;
		}
		else
		{
			Refresh_LogError("Could not create swapchain, failed to claim window!");
			SDL_free(windowData);
			return 0;
		}
	}
	else
	{
		Refresh_LogWarn("Window already claimed!");
		return 0;
	}
}

static void D3D11_UnclaimWindow(
	Refresh_Renderer *driverData,
	void *windowHandle
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11WindowData *windowData = D3D11_INTERNAL_FetchWindowData(windowHandle);

	if (windowData == NULL)
	{
		return;
	}

	if (windowData->swapchainData != NULL)
	{
		D3D11_Wait(driverData);
		D3D11_INTERNAL_DestroySwapchain(
			(D3D11Renderer*) driverData,
			windowData
		);
	}

	for (uint32_t i = 0; i < renderer->claimedWindowCount; i += 1)
	{
		if (renderer->claimedWindows[i]->windowHandle == windowHandle)
		{
			renderer->claimedWindows[i] = renderer->claimedWindows[renderer->claimedWindowCount - 1];
			renderer->claimedWindowCount -= 1;
			break;
		}
	}

	SDL_free(windowData);
	SDL_SetWindowData((SDL_Window*) windowHandle, WINDOW_DATA, NULL);
}

static Refresh_Texture* D3D11_AcquireSwapchainTexture(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	void *windowHandle,
	uint32_t *pWidth,
	uint32_t *pHeight
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11CommandBuffer *d3d11CommandBuffer = (D3D11CommandBuffer*) commandBuffer;
	D3D11WindowData *windowData;
	D3D11SwapchainData *swapchainData;
	DXGI_SWAP_CHAIN_DESC swapchainDesc;
	int w, h;
	HRESULT res;

	windowData = D3D11_INTERNAL_FetchWindowData(windowHandle);
	if (windowData == NULL)
	{
		return NULL;
	}

	swapchainData = windowData->swapchainData;
	if (swapchainData == NULL)
	{
		return NULL;
	}

	/* Check for window size changes and resize the swapchain if needed. */
	IDXGISwapChain_GetDesc(swapchainData->swapchain, &swapchainDesc);
	SDL_GetWindowSize((SDL_Window*) windowHandle, &w, &h);

	if (w != swapchainDesc.BufferDesc.Width || h != swapchainDesc.BufferDesc.Height)
	{
		res = D3D11_INTERNAL_ResizeSwapchain(
			renderer,
			swapchainData,
			w,
			h
		);
		ERROR_CHECK_RETURN("Could not resize swapchain", NULL);
	}

	/* Let the command buffer know it's associated with this swapchain. */
	d3d11CommandBuffer->swapchainData = swapchainData;

	/* Send the dimensions to the out parameters. */
	*pWidth = swapchainData->texture.width;
	*pHeight = swapchainData->texture.height;

	/* Return the swapchain texture */
	return (Refresh_Texture*) &swapchainData->texture;
}

static Refresh_TextureFormat D3D11_GetSwapchainFormat(
	Refresh_Renderer *driverData,
	void *windowHandle
) {
	return REFRESH_TEXTUREFORMAT_R8G8B8A8;
}

static void D3D11_SetSwapchainPresentMode(
	Refresh_Renderer *driverData,
	void *windowHandle,
	Refresh_PresentMode presentMode
) {
	NOT_IMPLEMENTED
}

/* Submission and Fences */

static void D3D11_INTERNAL_ReleaseFenceToPool(
	D3D11Renderer *renderer,
	D3D11Fence *fence
) {
	SDL_LockMutex(renderer->fenceLock);

	if (renderer->availableFenceCount == renderer->availableFenceCapacity)
	{
		renderer->availableFenceCapacity *= 2;
		renderer->availableFences = SDL_realloc(
			renderer->availableFences,
			renderer->availableFenceCapacity * sizeof(D3D11Fence*)
		);
	}
	renderer->availableFences[renderer->availableFenceCount] = fence;
	renderer->availableFenceCount += 1;

	SDL_UnlockMutex(renderer->fenceLock);
}

static void D3D11_INTERNAL_CleanCommandBuffer(
	D3D11Renderer *renderer,
	D3D11CommandBuffer *commandBuffer
) {
	/* Bound uniform buffers are now available */
	SDL_LockMutex(renderer->uniformBufferLock);
	for (uint32_t i = 0; i < commandBuffer->boundUniformBufferCount; i += 1)
	{
		if (renderer->availableUniformBufferCount == renderer->availableUniformBufferCapacity)
		{
			renderer->availableUniformBufferCapacity *= 2;
			renderer->availableUniformBuffers = SDL_realloc(
				renderer->availableUniformBuffers,
				renderer->availableUniformBufferCapacity * sizeof(D3D11UniformBuffer*)
			);
		}

		renderer->availableUniformBuffers[renderer->availableUniformBufferCount] = commandBuffer->boundUniformBuffers[i];
		renderer->availableUniformBufferCount += 1;
	}
	SDL_UnlockMutex(renderer->uniformBufferLock);

	commandBuffer->boundUniformBufferCount = 0;

	/* The fence is now available (unless SubmitAndAcquireFence was called) */
	if (commandBuffer->autoReleaseFence)
	{
		D3D11_INTERNAL_ReleaseFenceToPool(renderer, commandBuffer->fence);
	}

	/* Return command buffer to pool */
	SDL_LockMutex(renderer->acquireCommandBufferLock);
	if (renderer->availableCommandBufferCount == renderer->availableCommandBufferCapacity)
	{
		renderer->availableCommandBufferCapacity += 1;
		renderer->availableCommandBuffers = SDL_realloc(
			renderer->availableCommandBuffers,
			renderer->availableCommandBufferCapacity * sizeof(D3D11CommandBuffer*)
		);
	}
	renderer->availableCommandBuffers[renderer->availableCommandBufferCount] = commandBuffer;
	renderer->availableCommandBufferCount += 1;
	SDL_UnlockMutex(renderer->acquireCommandBufferLock);

	/* Remove this command buffer from the submitted list */
	for (uint32_t i = 0; i < renderer->submittedCommandBufferCount; i += 1)
	{
		if (renderer->submittedCommandBuffers[i] == commandBuffer)
		{
			renderer->submittedCommandBuffers[i] = renderer->submittedCommandBuffers[renderer->submittedCommandBufferCount - 1];
			renderer->submittedCommandBufferCount -= 1;
		}
	}
}

static void D3D11_INTERNAL_WaitForFence(
	D3D11Renderer *renderer,
	D3D11Fence *fence
) {
	BOOL queryData;

	while (S_OK != ID3D11DeviceContext_GetData(
		renderer->immediateContext,
		(ID3D11Asynchronous*) fence->handle,
		&queryData,
		sizeof(queryData),
		0
	)) {
		/* Spin until we get a result back... */
	}
}

static void D3D11_Submit(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11CommandBuffer *d3d11CommandBuffer = (D3D11CommandBuffer*) commandBuffer;
	ID3D11CommandList *commandList;
	HRESULT res;

	/* FIXME: Should add sanity check that current thread ID matches the command buffer's threadID. */

	SDL_LockMutex(renderer->contextLock);

	/* Notify the command buffer completion query that we have completed recording */
	ID3D11DeviceContext_End(
		renderer->immediateContext,
		(ID3D11Asynchronous*) d3d11CommandBuffer->fence->handle
	);

	/* Serialize the commands into the command list */
	res = ID3D11DeviceContext_FinishCommandList(
		d3d11CommandBuffer->context,
		0,
		&commandList
	);
	ERROR_CHECK("Could not finish command list recording!");

	/* Submit the command list to the immediate context */
	ID3D11DeviceContext_ExecuteCommandList(
		renderer->immediateContext,
		commandList,
		0
	);
	ID3D11CommandList_Release(commandList);

	/* Mark the command buffer as submitted */
	if (renderer->submittedCommandBufferCount + 1 >= renderer->submittedCommandBufferCapacity)
	{
		renderer->submittedCommandBufferCapacity = renderer->submittedCommandBufferCount + 1;

		renderer->submittedCommandBuffers = SDL_realloc(
			renderer->submittedCommandBuffers,
			sizeof(D3D11CommandBuffer*) * renderer->submittedCommandBufferCapacity
		);
	}

	renderer->submittedCommandBuffers[renderer->submittedCommandBufferCount] = d3d11CommandBuffer;
	renderer->submittedCommandBufferCount += 1;

	/* Present, if applicable */
	if (d3d11CommandBuffer->swapchainData)
	{
		IDXGISwapChain_Present(
			d3d11CommandBuffer->swapchainData->swapchain,
			1, /* FIXME: Assumes vsync! */
			0
		);
	}

	/* Check if we can perform any cleanups */
	for (int32_t i = renderer->submittedCommandBufferCount - 1; i >= 0; i -= 1)
	{
		BOOL queryData;

		res = ID3D11DeviceContext_GetData(
			renderer->immediateContext,
			(ID3D11Asynchronous*) renderer->submittedCommandBuffers[i]->fence->handle,
			&queryData,
			sizeof(queryData),
			0
		);
		if (res == S_OK)
		{
			D3D11_INTERNAL_CleanCommandBuffer(
				renderer,
				renderer->submittedCommandBuffers[i]
			);
		}
	}

	SDL_UnlockMutex(renderer->contextLock);
}

static Refresh_Fence* D3D11_SubmitAndAcquireFence(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer
) {
	D3D11CommandBuffer *d3d11CommandBuffer = (D3D11CommandBuffer*) commandBuffer;
	D3D11Fence *fence = d3d11CommandBuffer->fence;

	d3d11CommandBuffer->autoReleaseFence = 0;
	D3D11_Submit(driverData, commandBuffer);

	return (Refresh_Fence*) fence;
}

static void D3D11_Wait(
	Refresh_Renderer *driverData
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11CommandBuffer *commandBuffer;

	/*
	 * Wait for all submitted command buffers to complete.
	 * Sort of equivalent to vkDeviceWaitIdle.
	 */
	for (uint32_t i = 0; i < renderer->submittedCommandBufferCount; i += 1)
	{
		D3D11_INTERNAL_WaitForFence(
			renderer,
			renderer->submittedCommandBuffers[i]->fence
		);
	}

	SDL_LockMutex(renderer->contextLock);

	for (int32_t i = renderer->submittedCommandBufferCount - 1; i >= 0; i -= 1)
	{
		commandBuffer = renderer->submittedCommandBuffers[i];
		D3D11_INTERNAL_CleanCommandBuffer(renderer, commandBuffer);
	}

	SDL_UnlockMutex(renderer->contextLock);
}

static void D3D11_WaitForFences(
	Refresh_Renderer *driverData,
	uint8_t waitAll,
	uint32_t fenceCount,
	Refresh_Fence **pFences
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11Fence *fence;
	BOOL queryData;
	HRESULT res = S_FALSE;

	if (waitAll)
	{
		for (uint32_t i = 0; i < fenceCount; i += 1)
		{
			fence = (D3D11Fence*) pFences[i];
			D3D11_INTERNAL_WaitForFence(renderer, fence);
		}
	}
	else
	{
		while (res != S_OK)
		{
			for (uint32_t i = 0; i < fenceCount; i += 1)
			{
				fence = (D3D11Fence*) pFences[i];
				res = ID3D11DeviceContext_GetData(
					renderer->immediateContext,
					(ID3D11Asynchronous*) fence->handle,
					&queryData,
					sizeof(queryData),
					0
				);
				if (res == S_OK)
				{
					break;
				}
			}
		}
	}
}

static int D3D11_QueryFence(
	Refresh_Renderer *driverData,
	Refresh_Fence *fence
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11Fence *d3d11Fence = (D3D11Fence*) fence;
	BOOL queryData;

	HRESULT res = ID3D11DeviceContext_GetData(
		renderer->immediateContext,
		(ID3D11Asynchronous*) d3d11Fence->handle,
		&queryData,
		sizeof(queryData),
		0
	);

	return res == S_OK;
}

static void D3D11_ReleaseFence(
	Refresh_Renderer *driverData,
	Refresh_Fence *fence
) {
	D3D11_INTERNAL_ReleaseFenceToPool(
		(D3D11Renderer*) driverData,
		(D3D11Fence*) fence
	);
}

/* Device Creation */

static uint8_t D3D11_PrepareDriver(
	uint32_t *flags
) {
	void *d3d11_dll, *d3dcompiler_dll, *dxgi_dll;
	PFN_D3D11_CREATE_DEVICE D3D11CreateDeviceFunc;
	D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1 };
	PFN_D3DCOMPILE D3DCompileFunc;
	PFN_CREATE_DXGI_FACTORY1 CreateDXGIFactoryFunc;
	HRESULT res;

	/* Can we load D3D11? */

	d3d11_dll = SDL_LoadObject(D3D11_DLL);
	if (d3d11_dll == NULL)
	{
		Refresh_LogWarn("D3D11: Could not find " D3D11_DLL);
		return 0;
	}

	D3D11CreateDeviceFunc = (PFN_D3D11_CREATE_DEVICE) SDL_LoadFunction(
		d3d11_dll,
		D3D11_CREATE_DEVICE_FUNC
	);
	if (D3D11CreateDeviceFunc == NULL)
	{
		Refresh_LogWarn("D3D11: Could not find function " D3D11_CREATE_DEVICE_FUNC " in " D3D11_DLL);
		SDL_UnloadObject(d3d11_dll);
		return 0;
	}

	/* Can we create a device? */

	res = D3D11CreateDeviceFunc(
		NULL,
		D3D_DRIVER_TYPE_HARDWARE,
		NULL,
		D3D11_CREATE_DEVICE_BGRA_SUPPORT,
		levels,
		SDL_arraysize(levels),
		D3D11_SDK_VERSION,
		NULL,
		NULL,
		NULL
	);

	SDL_UnloadObject(d3d11_dll);

	if (FAILED(res))
	{
		Refresh_LogWarn("D3D11: Could not create D3D11Device with feature level 11_0");
		return 0;
	}

	/* Can we load D3DCompiler? */

	d3dcompiler_dll = SDL_LoadObject(D3DCOMPILER_DLL);
	if (d3dcompiler_dll == NULL)
	{
		Refresh_LogWarn("D3D11: Could not find " D3DCOMPILER_DLL);
		return 0;
	}

	D3DCompileFunc = (PFN_D3DCOMPILE) SDL_LoadFunction(
		d3dcompiler_dll,
		D3DCOMPILE_FUNC
	);
	SDL_UnloadObject(d3dcompiler_dll); /* We're not going to call this function, so we can just unload now. */
	if (D3DCompileFunc == NULL)
	{
		Refresh_LogWarn("D3D11: Could not find function " D3DCOMPILE_FUNC " in " D3DCOMPILER_DLL);
		return 0;
	}

	/* Can we load DXGI? */

	dxgi_dll = SDL_LoadObject(DXGI_DLL);
	if (dxgi_dll == NULL)
	{
		Refresh_LogWarn("D3D11: Could not find " DXGI_DLL);
		return 0;
	}

	CreateDXGIFactoryFunc = (PFN_CREATE_DXGI_FACTORY1) SDL_LoadFunction(
		dxgi_dll,
		CREATE_DXGI_FACTORY1_FUNC
	);
	SDL_UnloadObject(dxgi_dll); /* We're not going to call this function, so we can just unload now. */
	if (CreateDXGIFactoryFunc == NULL)
	{
		Refresh_LogWarn("D3D11: Could not find function " CREATE_DXGI_FACTORY1_FUNC " in " DXGI_DLL);
		return 0;
	}

	/* No window flags required */
	SDL_SetHint(SDL_HINT_VIDEO_EXTERNAL_CONTEXT, "1");

	return 1;
}

static void D3D11_INTERNAL_TryInitializeDXGIDebug(D3D11Renderer *renderer)
{
	PFN_DXGI_GET_DEBUG_INTERFACE DXGIGetDebugInterfaceFunc;
	HRESULT res;

	renderer->dxgidebug_dll = SDL_LoadObject(DXGIDEBUG_DLL);
	if (renderer->dxgidebug_dll == NULL)
	{
		Refresh_LogWarn("Could not find " DXGIDEBUG_DLL);
		return;
	}

	DXGIGetDebugInterfaceFunc = SDL_LoadFunction(
		renderer->dxgidebug_dll,
		DXGI_GET_DEBUG_INTERFACE_FUNC
	);
	if (DXGIGetDebugInterfaceFunc == NULL)
	{
		Refresh_LogWarn("Could not load function: " DXGI_GET_DEBUG_INTERFACE_FUNC);
		return;
	}

	res = DXGIGetDebugInterfaceFunc(&D3D_IID_IDXGIDebug, &renderer->dxgiDebug);
	if (FAILED(res))
	{
		Refresh_LogWarn("Could not get IDXGIDebug interface");
	}

	res = DXGIGetDebugInterfaceFunc(&D3D_IID_IDXGIInfoQueue, &renderer->dxgiInfoQueue);
	if (FAILED(res))
	{
		Refresh_LogWarn("Could not get IDXGIInfoQueue interface");
	}
}

static Refresh_Device* D3D11_CreateDevice(
	uint8_t debugMode
) {
	D3D11Renderer *renderer;
	PFN_CREATE_DXGI_FACTORY1 CreateDXGIFactoryFunc;
	PFN_D3D11_CREATE_DEVICE D3D11CreateDeviceFunc;
	D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1 };
	IDXGIFactory6 *factory6;
	uint32_t flags;
	DXGI_ADAPTER_DESC1 adapterDesc;
	HRESULT res;
	Refresh_Device* result;

	/* Allocate and zero out the renderer */
	renderer = (D3D11Renderer*) SDL_calloc(1, sizeof(D3D11Renderer));

	/* Load the D3DCompiler library */
	renderer->d3dcompiler_dll = SDL_LoadObject(D3DCOMPILER_DLL);
	if (renderer->d3dcompiler_dll == NULL)
	{
		Refresh_LogError("Could not find " D3DCOMPILER_DLL);
		return NULL;
	}

	/* Load the D3DCompile function pointer */
	renderer->D3DCompileFunc = (PFN_D3DCOMPILE) SDL_LoadFunction(
		renderer->d3dcompiler_dll,
		D3DCOMPILE_FUNC
	);
	if (renderer->D3DCompileFunc == NULL)
	{
		Refresh_LogError("Could not load function: " D3DCOMPILE_FUNC);
		return NULL;
	}

	/* Load the DXGI library */
	renderer->dxgi_dll = SDL_LoadObject(DXGI_DLL);
	if (renderer->dxgi_dll == NULL)
	{
		Refresh_LogError("Could not find " DXGI_DLL);
		return NULL;
	}

	/* Load the CreateDXGIFactory1 function */
	CreateDXGIFactoryFunc = (PFN_CREATE_DXGI_FACTORY1) SDL_LoadFunction(
		renderer->dxgi_dll,
		CREATE_DXGI_FACTORY1_FUNC
	);
	if (CreateDXGIFactoryFunc == NULL)
	{
		Refresh_LogError("Could not load function: " CREATE_DXGI_FACTORY1_FUNC);
		return NULL;
	}

	/* Create the DXGI factory */
	res = CreateDXGIFactoryFunc(
		&D3D_IID_IDXGIFactory1,
		&renderer->factory
	);
	ERROR_CHECK_RETURN("Could not create DXGIFactory", NULL);

	/* Get the default adapter */
	res = IDXGIAdapter1_QueryInterface(
		renderer->factory,
		&D3D_IID_IDXGIFactory6,
		(void**) &factory6
	);
	if (SUCCEEDED(res))
	{
		IDXGIFactory6_EnumAdapterByGpuPreference(
			factory6,
			0,
			DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
			&D3D_IID_IDXGIAdapter1,
			&renderer->adapter
		);
	}
	else
	{
		IDXGIFactory1_EnumAdapters1(
			renderer->factory,
			0,
			&renderer->adapter
		);
	}

	/* Get information about the selected adapter. Used for logging info. */
	IDXGIAdapter1_GetDesc1(renderer->adapter, &adapterDesc);

	/* Initialize the DXGI debug layer, if applicable */
	if (debugMode)
	{
		D3D11_INTERNAL_TryInitializeDXGIDebug(renderer);
	}

	/* Load the D3D library */
	renderer->d3d11_dll = SDL_LoadObject(D3D11_DLL);
	if (renderer->d3d11_dll == NULL)
	{
		Refresh_LogError("Could not find " D3D11_DLL);
		return NULL;
	}

	/* Load the CreateDevice function */
	D3D11CreateDeviceFunc = (PFN_D3D11_CREATE_DEVICE) SDL_LoadFunction(
		renderer->d3d11_dll,
		D3D11_CREATE_DEVICE_FUNC
	);
	if (D3D11CreateDeviceFunc == NULL)
	{
		Refresh_LogError("Could not load function: " D3D11_CREATE_DEVICE_FUNC);
		return NULL;
	}

	/* Set up device flags */
	flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	if (debugMode)
	{
		flags |= D3D11_CREATE_DEVICE_DEBUG;
	}

	/* Create the device */
	ID3D11Device *d3d11Device;
tryCreateDevice:
	res = D3D11CreateDeviceFunc(
		(IDXGIAdapter*) renderer->adapter,
		D3D_DRIVER_TYPE_UNKNOWN, /* Must be UNKNOWN if adapter is non-null according to spec */
		NULL,
		flags,
		levels,
		SDL_arraysize(levels),
		D3D11_SDK_VERSION,
		&d3d11Device,
		&renderer->featureLevel,
		&renderer->immediateContext
	);
	if (FAILED(res) && debugMode)
	{
		/* If device creation failed, and we're in debug mode, remove the debug flag and try again. */
		Refresh_LogWarn("Creating device in debug mode failed with error %08X. Trying non-debug.", res);
		flags &= ~D3D11_CREATE_DEVICE_DEBUG;
		debugMode = 0;
		goto tryCreateDevice;
	}

	ERROR_CHECK_RETURN("Could not create D3D11 device", NULL);

	/* The actual device we want is the ID3D11Device1 interface... */
	res = ID3D11Device_QueryInterface(
		d3d11Device,
		&D3D_IID_ID3D11Device1,
		&renderer->device
	);
	ERROR_CHECK_RETURN("Could not get ID3D11Device1 interface", NULL);

	/* Release the old device interface, we don't need it anymore */
	ID3D11Device_Release(d3d11Device);

	/* Set up the info queue */
	if (renderer->dxgiInfoQueue)
	{
		DXGI_INFO_QUEUE_MESSAGE_SEVERITY sevList[] =
		{
			DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION,
			DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR,
			DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING,
			// DXGI_INFO_QUEUE_MESSAGE_SEVERITY_INFO, /* This can be a bit much, so toggle as needed for debugging. */
			DXGI_INFO_QUEUE_MESSAGE_SEVERITY_MESSAGE
		};
		DXGI_INFO_QUEUE_FILTER filter = { 0 };
		filter.AllowList.NumSeverities = SDL_arraysize(sevList);
		filter.AllowList.pSeverityList = sevList;

		IDXGIInfoQueue_PushStorageFilter(
			renderer->dxgiInfoQueue,
			D3D_IID_DXGI_DEBUG_ALL,
			&filter
		);
	}

	/* Print driver info */
	Refresh_LogInfo("Refresh Driver: D3D11");
	Refresh_LogInfo("D3D11 Adapter: %S", adapterDesc.Description);

	/* Create mutexes */
	renderer->contextLock = SDL_CreateMutex(); /* FIXME: We should be using this *everywhere* the immediate context is accessed! */
	renderer->acquireCommandBufferLock = SDL_CreateMutex();
	renderer->uniformBufferLock = SDL_CreateMutex();
	renderer->fenceLock = SDL_CreateMutex();

	/* Initialize miscellaneous renderer members */
	renderer->debugMode = (flags & D3D11_CREATE_DEVICE_DEBUG);

	/* Create command buffer pool */
	D3D11_INTERNAL_AllocateCommandBuffers(renderer, 2);

	/* Create uniform buffer pool */
	renderer->availableUniformBufferCapacity = 2;
	renderer->availableUniformBuffers = SDL_malloc(sizeof(D3D11UniformBuffer*) * renderer->availableUniformBufferCapacity);

	/* Create fence pool */
	renderer->availableFenceCapacity = 2;
	renderer->availableFences = SDL_malloc(sizeof(D3D11Fence*) * renderer->availableFenceCapacity);

	/* Create the Refresh Device */
	result = (Refresh_Device*) SDL_malloc(sizeof(Refresh_Device));
	ASSIGN_DRIVER(D3D11)
	result->driverData = (Refresh_Renderer*) renderer;

	return result;
}

Refresh_Driver D3D11Driver = {
	"D3D11",
	D3D11_PrepareDriver,
	D3D11_CreateDevice
};

#endif //REFRESH_DRIVER_D3D11
