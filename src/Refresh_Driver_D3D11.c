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

#if REFRESH_DRIVER_D3D11

#define D3D11_NO_HELPERS
#define CINTERFACE
#define COBJMACROS
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>

#include "Refresh_Driver.h"
#include "Refresh_Driver_D3D11_cdefines.h"

#include <SDL.h>
#include <SDL_syswm.h>

 /* Defines */

#define D3D11_DLL "d3d11.dll"
#define DXGI_DLL "dxgi.dll"
#define D3D11_CREATE_DEVICE_FUNC "D3D11CreateDevice"
#define D3DCOMPILE_FUNC "D3DCompile"
#define CREATE_DXGI_FACTORY1_FUNC "CreateDXGIFactory1"
#define WINDOW_DATA "Refresh_D3D11WindowData"

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
	DXGI_FORMAT_B5G6R5_UNORM,	/* R5G6B5 */
	DXGI_FORMAT_B5G5R5A1_UNORM,	/* A1R5G5B5 */
	DXGI_FORMAT_B4G4R4A4_UNORM,	/* B4G4R4A4 */
	DXGI_FORMAT_BC1_UNORM,		/* BC1 */
	DXGI_FORMAT_BC3_UNORM,		/* BC3 */
	DXGI_FORMAT_BC5_UNORM,		/* BC5 */
	DXGI_FORMAT_R8G8_SNORM,		/* R8G8_SNORM */
	DXGI_FORMAT_R8G8B8A8_SNORM,	/* R8G8B8A8_SNORM */
	DXGI_FORMAT_R10G10B10A2_UNORM,	/* A2R10G10B10 */
	DXGI_FORMAT_R16G16_UNORM,	/* R16G16 */
	DXGI_FORMAT_R16G16B16A16_UNORM,	/* R16G16B16A16 */
	DXGI_FORMAT_R8_UNORM,		/* R8 */
	DXGI_FORMAT_R32_FLOAT,		/* R32_SFLOAT */
	DXGI_FORMAT_R32G32_FLOAT,	/* R32G32_SFLOAT */
	DXGI_FORMAT_R32G32B32A32_FLOAT,	/* R32G32B32A32_SFLOAT */
	DXGI_FORMAT_R16_FLOAT,		/* R16_SFLOAT */
	DXGI_FORMAT_R16G16_FLOAT,	/* R16G16_SFLOAT */
	DXGI_FORMAT_R16G16B16A16_FLOAT,	/* R16G16B16A16_SFLOAT */
	DXGI_FORMAT_D16_UNORM,		/* D16 */
	DXGI_FORMAT_D32_FLOAT,		/* D32 */
	DXGI_FORMAT_D24_UNORM_S8_UINT,	/* D16S8 */
	DXGI_FORMAT_D32_FLOAT_S8X24_UINT/* D32S8 */
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

typedef struct D3D11Texture
{
	/* D3D Handles */
	ID3D11Resource *handle; /* ID3D11Texture2D* or ID3D11Texture3D* */
	ID3D11ShaderResourceView *shaderView;

	/* Basic Info */
	uint32_t levelCount;
	uint8_t isRenderTarget;

	/* Dimensions*/
	#define REFRESH_D3D11_RENDERTARGET_2D 0
	#define REFRESH_D3D11_RENDERTARGET_3D 1
	#define REFRESH_D3D11_RENDERTARGET_CUBE 2
	uint8_t rtType;
	REFRESHNAMELESS union
	{
		struct
		{
			uint32_t width;
			uint32_t height;
			ID3D11View *targetView; /* ID3D11RenderTargetView* or ID3D11DepthStencilView* */
		} twod;
	};
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

	ID3D11PixelShader *fragmentShader;
} D3D11GraphicsPipeline;

typedef struct D3D11CommandBuffer
{
	/* D3D11 Object References */
	ID3D11DeviceContext *context;
	D3D11SwapchainData *swapchainData;

	/* Render Pass */
	ID3D11RenderTargetView *rtViews[MAX_COLOR_TARGET_BINDINGS];
	ID3D11DepthStencilView *dsView;
	D3D11GraphicsPipeline *graphicsPipeline;

	/* State */
	SDL_threadID threadID;
	ID3D11Query *completionQuery;
} D3D11CommandBuffer;

typedef struct D3D11CommandBufferPool
{
	D3D11CommandBuffer **elements;
	uint32_t count;
	uint32_t capacity;
} D3D11CommandBufferPool;

typedef struct D3D11Buffer
{
	ID3D11Buffer *handle;
	uint32_t size;
} D3D11Buffer;

typedef struct D3D11Sampler
{
	ID3D11SamplerState *handle;
} D3D11Sampler;

typedef struct D3D11Renderer
{
	ID3D11Device *device;
	ID3D11DeviceContext *immediateContext;
	IDXGIFactory1 *factory;
	IDXGIAdapter1 *adapter;
	void *d3d11_dll;
	void *dxgi_dll;
	void *d3dcompiler_dll;

	uint8_t debugMode;
	D3D_FEATURE_LEVEL featureLevel; /* FIXME: Do we need this? */
	PFN_D3DCOMPILE D3DCompileFunc;

	D3D11WindowData **claimedWindows;
	uint32_t claimedWindowCount;
	uint32_t claimedWindowCapacity;

	D3D11CommandBufferPool commandBufferPool;

	SDL_mutex *contextLock;
	SDL_mutex *acquireCommandBufferLock;

	D3D11CommandBuffer **submittedCommandBuffers;
	uint32_t submittedCommandBufferCount;
	uint32_t submittedCommandBufferCapacity;
} D3D11Renderer;

/* Logging */

static void D3D11_INTERNAL_LogError(
	ID3D11Device *device,
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

/* Quit */

static void D3D11_DestroyDevice(
	Refresh_Device *device
) {
	D3D11Renderer *renderer = (D3D11Renderer*) device->driverData;
	D3D11CommandBuffer *commandBuffer;

	D3D11_Wait(device->driverData);

	/* Release the window data */

	for (int32_t i = renderer->claimedWindowCount - 1; i >= 0; i -= 1)
	{
		D3D11_UnclaimWindow(device->driverData, renderer->claimedWindows[i]->windowHandle);
	}

	SDL_free(renderer->claimedWindows);

	D3D11_Wait(device->driverData); /* FIXME: Copied this from Vulkan, is it actually necessary? */

	/* Release command buffer infrastructure */

	SDL_free(renderer->submittedCommandBuffers);

	for (uint32_t i = 0; i < renderer->commandBufferPool.count; i += 1)
	{
		commandBuffer = renderer->commandBufferPool.elements[i];

		ID3D11Query_Release(commandBuffer->completionQuery);
		ID3D11DeviceContext_Release(commandBuffer->context);

		SDL_free(commandBuffer);
	}

	/* Release the mutexes */

	SDL_DestroyMutex(renderer->acquireCommandBufferLock);
	SDL_DestroyMutex(renderer->contextLock);

	/* Release the DLLs and D3D11 device infrastructure */

	SDL_UnloadObject(renderer->d3d11_dll);
	SDL_UnloadObject(renderer->d3dcompiler_dll);

	ID3D11DeviceContext_Release(renderer->immediateContext);
	ID3D11Device_Release(renderer->device);
	IDXGIAdapter_Release(renderer->adapter);
	IDXGIFactory_Release(renderer->factory);

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
	NOT_IMPLEMENTED
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
	NOT_IMPLEMENTED
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

	ID3D11DeviceContext_Draw(
		d3d11CommandBuffer->context,
		PrimitiveVerts(d3d11CommandBuffer->graphicsPipeline->primitiveType, primitiveCount),
		vertexStart
	);

	/* FIXME: vertex/fragment param offsets */
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
	NOT_IMPLEMENTED
}

static void D3D11_DispatchCompute(
	Refresh_Renderer *device,
	Refresh_CommandBuffer *commandBuffer,
	uint32_t groupCountX,
	uint32_t groupCountY,
	uint32_t groupCountZ,
	uint32_t computeParamOffset
) {
	NOT_IMPLEMENTED
}

/* State Creation */

static ID3D11BlendState* D3D11_INTERNAL_FetchBlendState(
	D3D11Renderer *renderer,
	uint32_t numColorAttachments,
	Refresh_ColorAttachmentDescription *colorAttachments
) {
	ID3D11BlendState *result;
	D3D11_BLEND_DESC blendDesc;
	uint32_t i;
	HRESULT res;

	/* Create a new blend state.
	 * The spec says the driver will not create duplicate states, so there's no need to cache.
	 */
	SDL_zero(blendDesc); /* needed for any unused RT entries */

	blendDesc.AlphaToCoverageEnable = FALSE;
	blendDesc.IndependentBlendEnable = TRUE;

	for (i = 0; i < numColorAttachments; i += 1)
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
	dsDesc.DepthFunc = depthStencilState.compareOp;
	dsDesc.DepthWriteMask = (
		depthStencilState.depthWriteEnable ?
			D3D11_DEPTH_WRITE_MASK_ALL :
			D3D11_DEPTH_WRITE_MASK_ZERO
	);

	dsDesc.BackFace.StencilFunc = depthStencilState.backStencilState.compareOp;
	dsDesc.BackFace.StencilDepthFailOp = depthStencilState.backStencilState.depthFailOp;
	dsDesc.BackFace.StencilFailOp = depthStencilState.backStencilState.failOp;
	dsDesc.BackFace.StencilPassOp = depthStencilState.backStencilState.passOp;

	dsDesc.FrontFace.StencilFunc = depthStencilState.frontStencilState.compareOp;
	dsDesc.FrontFace.StencilDepthFailOp = depthStencilState.frontStencilState.depthFailOp;
	dsDesc.FrontFace.StencilFailOp = depthStencilState.frontStencilState.failOp;
	dsDesc.FrontFace.StencilPassOp = depthStencilState.frontStencilState.passOp;

	/* FIXME: D3D11 doesn't have separate read/write masks for each stencil side. What should we do? */
	dsDesc.StencilReadMask = depthStencilState.backStencilState.compareMask;
	dsDesc.StencilWriteMask = depthStencilState.backStencilState.writeMask;

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
	uint32_t i;
	for (i = 0; i < numBindings; i += 1)
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
	uint32_t i, bindingIndex;
	HRESULT res;

	/* Allocate an array of vertex elements */
	elementDescs = SDL_stack_alloc(
		D3D11_INPUT_ELEMENT_DESC,
		inputState.vertexAttributeCount
	);

	/* Create the array of input elements */
	for (i = 0; i < inputState.vertexAttributeCount; i += 1)
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

static Refresh_ComputePipeline* D3D11_CreateComputePipeline(
	Refresh_Renderer *driverData,
	Refresh_ComputeShaderInfo *computeShaderInfo
) {
	NOT_IMPLEMENTED
	return NULL;
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

	/* Color */

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

	pipeline->multisampleState = pipelineCreateInfo->multisampleState;

	/* Depth stencil */

	pipeline->depthStencilState = D3D11_INTERNAL_FetchDepthStencilState(
		renderer,
		pipelineCreateInfo->depthStencilState
	);

	pipeline->hasDepthStencilAttachment = pipelineCreateInfo->attachmentInfo.hasDepthStencilAttachment;
	pipeline->depthStencilAttachmentFormat = RefreshToD3D11_TextureFormat[
		pipelineCreateInfo->attachmentInfo.depthStencilFormat
	];
	pipeline->stencilRef = pipelineCreateInfo->depthStencilState.backStencilState.reference; /* FIXME: Should we use front or back? */

	/* Rasterizer */

	pipeline->primitiveType = pipelineCreateInfo->primitiveType;
	pipeline->rasterizerState = D3D11_INTERNAL_FetchRasterizerState(
		renderer,
		pipelineCreateInfo->rasterizerState
	);

	/* Vertex shader */

	if (vertShaderModule->shader == NULL)
	{
		/* FIXME:
		 * Could we store a flag in the shaderc output to mark if a shader is vertex/fragment?
		 * Then we could compile on shader module creation instead of at bind time.
		 */
		res = renderer->D3DCompileFunc(
			vertShaderModule->shaderSource,
			vertShaderModule->shaderSourceLength,
			NULL,
			NULL,
			NULL,
			"main",
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
		ERROR_CHECK_RETURN("Could not create vertex shader", NULL);
	}
	pipeline->vertexShader = (ID3D11VertexShader*) vertShaderModule->shader;

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
		/* Not sure if this is even possible, but juuust in case... */
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
			"main",
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
		ERROR_CHECK_RETURN("Could not create pixel shader", NULL);
	}
	pipeline->fragmentShader = (ID3D11PixelShader*) fragShaderModule->shader;

	/* FIXME: Need to create uniform buffers for the shaders */

	return (Refresh_GraphicsPipeline*) pipeline;
}

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

	/* We don't know whether this is a vertex or fragment shader,
	 * so wait to compile until we bind to a pipeline...
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
	NOT_IMPLEMENTED
	return NULL;
}

static Refresh_Buffer* D3D11_CreateBuffer(
	Refresh_Renderer *driverData,
	Refresh_BufferUsageFlags usageFlags,
	uint32_t sizeInBytes
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11_BUFFER_DESC bufferDesc;
	ID3D11Buffer *bufferHandle;
	D3D11Buffer *d3d11Buffer;
	HRESULT res;

	uint32_t bindFlags = 0;
	if (usageFlags & REFRESH_BUFFERUSAGE_VERTEX_BIT)
	{
		bindFlags |= D3D11_BIND_VERTEX_BUFFER;
	}
	if (usageFlags & REFRESH_BUFFERUSAGE_INDEX_BIT)
	{
		bindFlags |= D3D11_BIND_INDEX_BUFFER;
	}
	if (usageFlags & REFRESH_BUFFERUSAGE_COMPUTE_BIT)
	{
		bindFlags |= D3D11_BIND_UNORDERED_ACCESS;
	}
	bufferDesc.BindFlags = bindFlags;

	bufferDesc.ByteWidth = sizeInBytes;
	bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bufferDesc.MiscFlags = 0;
	bufferDesc.StructureByteStride = 0;

	res = ID3D11Device_CreateBuffer(
		renderer->device,
		&bufferDesc,
		NULL,
		&bufferHandle
	);
	ERROR_CHECK_RETURN("Could not create buffer", NULL);

	d3d11Buffer = SDL_malloc(sizeof(D3D11Buffer));
	d3d11Buffer->handle = bufferHandle;
	d3d11Buffer->size = sizeInBytes;

	return (Refresh_Buffer*) d3d11Buffer;
}

/* Setters */

static void D3D11_SetTextureData(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_TextureSlice *textureSlice,
	void *data,
	uint32_t dataLengthInBytes
) {
	NOT_IMPLEMENTED
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

static void D3D11_SetBufferData(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Buffer *buffer,
	uint32_t offsetInBytes,
	void* data,
	uint32_t dataLength
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11CommandBuffer *d3d11CommandBuffer = (D3D11CommandBuffer*) commandBuffer;
	D3D11Buffer *d3d11Buffer = (D3D11Buffer*) buffer;
	D3D11_MAPPED_SUBRESOURCE subres = { 0 };
	HRESULT res;

	res = ID3D11DeviceContext_Map(
		d3d11CommandBuffer->context,
		(ID3D11Resource*) d3d11Buffer->handle,
		0,
		D3D11_MAP_WRITE_DISCARD,
		0,
		&subres
	);
	ERROR_CHECK_RETURN("Could not map buffer for writing!", );

	SDL_memcpy(
		(uint8_t*) subres.pData + offsetInBytes,
		data,
		dataLength
	);

	ID3D11DeviceContext_Unmap(
		d3d11CommandBuffer->context,
		(ID3D11Resource*) d3d11Buffer->handle,
		0
	);
}

static uint32_t D3D11_PushVertexShaderUniforms(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	void *data,
	uint32_t dataLengthInBytes
) {
	NOT_IMPLEMENTED
	return 0;
}

static uint32_t D3D11_PushFragmentShaderUniforms(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	void *data,
	uint32_t dataLengthInBytes
) {
	NOT_IMPLEMENTED
	return 0;
}

static uint32_t D3D11_PushComputeShaderUniforms(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	void *data,
	uint32_t dataLengthInBytes
) {
	NOT_IMPLEMENTED
	return 0;
}

static void D3D11_BindVertexSamplers(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Texture **pTextures,
	Refresh_Sampler **pSamplers
) {
	NOT_IMPLEMENTED
}

static void D3D11_BindFragmentSamplers(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Texture **pTextures,
	Refresh_Sampler **pSamplers
) {
	NOT_IMPLEMENTED
}

/* Getters */

static void D3D11_GetBufferData(
	Refresh_Renderer *driverData,
	Refresh_Buffer *buffer,
	void *data,
	uint32_t dataLengthInBytes
) {
	NOT_IMPLEMENTED
}

/* Disposal */

static void D3D11_QueueDestroyTexture(
	Refresh_Renderer *driverData,
	Refresh_Texture *texture
) {
	NOT_IMPLEMENTED
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
	NOT_IMPLEMENTED
}

static void D3D11_QueueDestroyGraphicsPipeline(
	Refresh_Renderer *driverData,
	Refresh_GraphicsPipeline *graphicsPipeline
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11GraphicsPipeline *d3dGraphicsPipeline = (D3D11GraphicsPipeline*) graphicsPipeline;

	ID3D11BlendState_Release(d3dGraphicsPipeline->colorAttachmentBlendState);
	ID3D11DepthStencilState_Release(d3dGraphicsPipeline->depthStencilState);
	ID3D11RasterizerState_Release(d3dGraphicsPipeline->rasterizerState);
	ID3D11InputLayout_Release(d3dGraphicsPipeline->inputLayout);

	/* FIXME: Release uniform buffers, once that's written in */

	if (d3dGraphicsPipeline->vertexStrides)
	{
		SDL_free(d3dGraphicsPipeline->vertexStrides);
	}

	SDL_free(d3dGraphicsPipeline);
}

/* Graphics State */

static void D3D11_INTERNAL_AllocateCommandBuffers(
	D3D11Renderer *renderer,
	uint32_t allocateCount
) {
	D3D11CommandBufferPool *pool = &renderer->commandBufferPool;
	D3D11CommandBuffer *commandBuffer;
	D3D11_QUERY_DESC queryDesc;
	HRESULT res;

	pool->capacity += allocateCount;

	pool->elements = SDL_realloc(
		pool->elements,
		sizeof(D3D11CommandBuffer*) * pool->capacity
	);

	for (uint32_t i = 0; i < allocateCount; i += 1)
	{
		commandBuffer = SDL_malloc(sizeof(D3D11CommandBuffer));

		res = ID3D11Device_CreateDeferredContext(
			renderer->device,
			0,
			&commandBuffer->context
		);
		ERROR_CHECK("Could not create deferred context");

		queryDesc.Query = D3D11_QUERY_EVENT;
		queryDesc.MiscFlags = 0;
		res = ID3D11Device_CreateQuery(
			renderer->device,
			&queryDesc,
			&commandBuffer->completionQuery
		);
		ERROR_CHECK("Could not create query");

		/* FIXME: Resource tracking? */

		pool->elements[pool->count] = commandBuffer;
		pool->count += 1;
	}
}

static D3D11CommandBuffer* D3D11_INTERNAL_GetInactiveCommandBufferFromPool(
	D3D11Renderer *renderer
) {
	D3D11CommandBufferPool *pool = &renderer->commandBufferPool;
	D3D11CommandBuffer *commandBuffer;

	if (pool->count == 0)
	{
		D3D11_INTERNAL_AllocateCommandBuffers(
			renderer,
			pool->capacity
		);
	}

	commandBuffer = pool->elements[pool->count - 1];
	pool->count -= 1;

	return commandBuffer;
}

static Refresh_CommandBuffer* D3D11_AcquireCommandBuffer(
	Refresh_Renderer *driverData
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11CommandBuffer *commandBuffer;
	uint32_t i;

	SDL_LockMutex(renderer->acquireCommandBufferLock);

	/* Set up the command buffer */
	commandBuffer = D3D11_INTERNAL_GetInactiveCommandBufferFromPool(renderer);
	commandBuffer->threadID = SDL_ThreadID();
	commandBuffer->swapchainData = NULL;
	commandBuffer->dsView = NULL;
	commandBuffer->graphicsPipeline = NULL;
	for (i = 0; i < MAX_COLOR_TARGET_BINDINGS; i += 1)
	{
		commandBuffer->rtViews[i] = NULL;
	}

	SDL_UnlockMutex(renderer->acquireCommandBufferLock);

	return (Refresh_CommandBuffer*) commandBuffer;
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
	D3D11_VIEWPORT viewport;
	D3D11_RECT scissorRect;
	uint32_t i;

	/* FIXME:
	 * We need to unbind the RT textures on the Refresh side
	 * if they're bound for sampling on the command buffer!
	 */

	/* Clear the bound RTs for the current command buffer */
	for (i = 0; i < MAX_COLOR_TARGET_BINDINGS; i += 1)
	{
		d3d11CommandBuffer->rtViews[i] = NULL;
	}
	d3d11CommandBuffer->dsView = NULL;

	/* Get RTVs for the color attachments */
	for (i = 0; i < colorAttachmentCount; i += 1)
	{
		/* FIXME: Cube RTs */
		d3d11CommandBuffer->rtViews[i] = (ID3D11RenderTargetView*) ((D3D11Texture*) colorAttachmentInfos[i].texture)->twod.targetView;
	}

	/* Get the DSV for the depth stencil attachment, if applicable */
	if (depthStencilAttachmentInfo != NULL)
	{
		d3d11CommandBuffer->dsView = (ID3D11DepthStencilView*) ((D3D11Texture*) depthStencilAttachmentInfo->texture)->twod.targetView;
	}

	/* Actually set the RTs */
	ID3D11DeviceContext_OMSetRenderTargets(
		d3d11CommandBuffer->context,
		colorAttachmentCount,
		d3d11CommandBuffer->rtViews,
		d3d11CommandBuffer->dsView
	);

	/* Perform load ops on the RTs */
	for (i = 0; i < colorAttachmentCount; i += 1)
	{
		if (colorAttachmentInfos[i].loadOp == REFRESH_LOADOP_CLEAR)
		{
			clearColors[0] = colorAttachmentInfos[i].clearColor.x;
			clearColors[1] = colorAttachmentInfos[i].clearColor.y;
			clearColors[2] = colorAttachmentInfos[i].clearColor.z;
			clearColors[3] = colorAttachmentInfos[i].clearColor.w;

			ID3D11DeviceContext_ClearRenderTargetView(
				d3d11CommandBuffer->context,
				(ID3D11RenderTargetView*) ((D3D11Texture*) colorAttachmentInfos[i].texture)->twod.targetView,
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
				(ID3D11DepthStencilView*) ((D3D11Texture*) depthStencilAttachmentInfo->texture)->twod.targetView,
				dsClearFlags,
				depthStencilAttachmentInfo->depthStencilClearValue.depth,
				(uint8_t) depthStencilAttachmentInfo->depthStencilClearValue.stencil
			);
		}
	}

	/* Set default viewport and scissor state */
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.Width = (float) ((D3D11Texture*) colorAttachmentInfos[0].texture)->twod.width;
	viewport.Height = (float) ((D3D11Texture*) colorAttachmentInfos[0].texture)->twod.height;
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
	NOT_IMPLEMENTED
}

/* Compute State */

static void D3D11_BindComputePipeline(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_ComputePipeline *computePipeline
) {
	NOT_IMPLEMENTED
}

static void D3D11_BindComputeBuffers(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Buffer **pBuffers
) {
	NOT_IMPLEMENTED
}

static void D3D11_BindComputeTextures(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer,
	Refresh_Texture **pTextures
) {
	NOT_IMPLEMENTED
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
		(ID3D11RenderTargetView**) &pTexture->twod.targetView
	);
	if (FAILED(res))
	{
		ID3D11Texture2D_Release(swapchainTexture);
		D3D11_INTERNAL_LogError(renderer->device, "Swapchain RTV creation failed", res);
		return 0;
	}

	/* Fill out the rest of the texture struct */
	pTexture->handle = NULL;
	pTexture->shaderView = NULL;
	pTexture->isRenderTarget = 1;
	pTexture->rtType = REFRESH_D3D11_RENDERTARGET_2D;

	ID3D11Texture2D_GetDesc(swapchainTexture, &textureDesc);
	pTexture->levelCount = textureDesc.MipLevels;
	pTexture->twod.width = textureDesc.Width;
	pTexture->twod.height = textureDesc.Height;

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
		(void**) &pParent /* FIXME: Does pParent need to get released? (Same for FNA3D) */
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
	ID3D11RenderTargetView_Release(swapchainData->texture.twod.targetView);

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

	ID3D11RenderTargetView_Release(swapchainData->texture.twod.targetView);
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
	uint32_t i;

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

	for (i = 0; i < renderer->claimedWindowCount; i += 1)
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
	*pWidth = swapchainData->texture.twod.width;
	*pHeight = swapchainData->texture.twod.height;

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

static void D3D11_INTERNAL_CleanCommandBuffer(
	D3D11Renderer *renderer,
	D3D11CommandBuffer *commandBuffer
) {
	D3D11CommandBufferPool *commandBufferPool = &renderer->commandBufferPool;
	uint32_t i;

	/* FIXME: All kinds of stuff should go here... */

	SDL_LockMutex(renderer->acquireCommandBufferLock);

	if (commandBufferPool->count == commandBufferPool->capacity)
	{
		commandBufferPool->capacity += 1;
		commandBufferPool->elements = SDL_realloc(
			commandBufferPool->elements,
			commandBufferPool->capacity * sizeof(D3D11CommandBuffer*)
		);
	}

	commandBufferPool->elements[commandBufferPool->count] = commandBuffer;
	commandBufferPool->count += 1;

	SDL_UnlockMutex(renderer->acquireCommandBufferLock);

	/* Remove this command buffer from the submitted list */
	for (i = 0; i < renderer->submittedCommandBufferCount; i += 1)
	{
		if (renderer->submittedCommandBuffers[i] == commandBuffer)
		{
			renderer->submittedCommandBuffers[i] = renderer->submittedCommandBuffers[renderer->submittedCommandBufferCount - 1];
			renderer->submittedCommandBufferCount -= 1;
		}
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
		(ID3D11Asynchronous*) d3d11CommandBuffer->completionQuery
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

	SDL_UnlockMutex(renderer->contextLock);

	/* Present, if applicable */
	if (d3d11CommandBuffer->swapchainData)
	{
		IDXGISwapChain_Present(
			d3d11CommandBuffer->swapchainData->swapchain,
			1, /* FIXME: Assumes vsync! */
			0
		);
	}

	SDL_UnlockMutex(renderer->contextLock);
}

static Refresh_Fence* D3D11_SubmitAndAcquireFence(
	Refresh_Renderer *driverData,
	Refresh_CommandBuffer *commandBuffer
) {
	NOT_IMPLEMENTED
	return NULL;
}

static void D3D11_Wait(
	Refresh_Renderer *driverData
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11CommandBuffer *commandBuffer;
	BOOL queryData;

	/*
	 * Wait for all submitted command buffers to complete.
	 * Sort of equivalent to vkDeviceWaitIdle.
	 */
	for (uint32_t i = 0; i < renderer->submittedCommandBufferCount; i += 1)
	{
		while (S_OK != ID3D11DeviceContext_GetData(
			renderer->immediateContext,
			(ID3D11Asynchronous*) renderer->submittedCommandBuffers[i]->completionQuery,
			&queryData,
			sizeof(queryData),
			0
		)) {
			/* Spin until we get a result back... */
		}
	}

	SDL_LockMutex(renderer->contextLock);

	for (int32_t i = renderer->submittedCommandBufferCount - 1; i >= 0; i -= 1)
	{
		commandBuffer = renderer->submittedCommandBuffers[i];
		D3D11_INTERNAL_CleanCommandBuffer(renderer, commandBuffer);
	}

	/* FIXME: D3D11_INTERNAL_PerformPendingDestroys(renderer); */

	SDL_UnlockMutex(renderer->contextLock);
}

static void D3D11_WaitForFences(
	Refresh_Renderer *driverData,
	uint8_t waitAll,
	uint32_t fenceCount,
	Refresh_Fence **pFences
) {
	NOT_IMPLEMENTED
}

static int D3D11_QueryFence(
	Refresh_Renderer *driverData,
	Refresh_Fence *fence
) {
	NOT_IMPLEMENTED
	return 0;
}

static void D3D11_ReleaseFence(
	Refresh_Renderer *driverData,
	Refresh_Fence *fence
) {
	NOT_IMPLEMENTED
}

/* Device Creation */

static uint8_t D3D11_PrepareDriver(
	uint32_t *flags
) {
	void *d3d11_dll, *d3dcompiler_dll, *dxgi_dll;
	PFN_D3D11_CREATE_DEVICE D3D11CreateDeviceFunc;
	D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
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

static Refresh_Device* D3D11_CreateDevice(
	uint8_t debugMode
) {
	D3D11Renderer *renderer;
	PFN_CREATE_DXGI_FACTORY1 CreateDXGIFactoryFunc;
	PFN_D3D11_CREATE_DEVICE D3D11CreateDeviceFunc;
	D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
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
tryCreateDevice:
	res = D3D11CreateDeviceFunc(
		(IDXGIAdapter*) renderer->adapter,
		D3D_DRIVER_TYPE_UNKNOWN, /* Must be UNKNOWN if adapter is non-null according to spec */
		NULL,
		flags,
		levels,
		SDL_arraysize(levels),
		D3D11_SDK_VERSION,
		&renderer->device,
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

	/* Print driver info */
	Refresh_LogInfo("Refresh Driver: D3D11");
	Refresh_LogInfo("D3D11 Adapter: %S", adapterDesc.Description);

	/* Initialize the command buffer pool */
	renderer->commandBufferPool = (D3D11CommandBufferPool) { 0 };

	/* Create mutexes */
	renderer->contextLock = SDL_CreateMutex();
	renderer->acquireCommandBufferLock = SDL_CreateMutex();

	/* Initialize miscellaneous renderer members */
	renderer->debugMode = (flags & D3D11_CREATE_DEVICE_DEBUG);

	/* Create command buffers to initialize the pool */
	D3D11_INTERNAL_AllocateCommandBuffers(renderer, 2);

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
