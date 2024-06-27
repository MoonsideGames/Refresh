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

#ifndef REFRESH_H
#define REFRESH_H

#include "SDL.h"
#include <SDL_stdinc.h>

#ifdef _WIN32
#define REFRESHAPI  __declspec(dllexport)
#define REFRESHCALL __cdecl
#else
#define REFRESHAPI
#define REFRESHCALL
#endif

/* -Wpedantic nameless union/struct silencing */
#ifndef REFRESHNAMELESS
#ifdef __GNUC__
#define REFRESHNAMELESS __extension__
#else
#define REFRESHNAMELESS
#endif /* __GNUC__ */
#endif /* REFRESHNAMELESS */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Type Declarations */

typedef struct Refresh_Device Refresh_Device;
typedef struct Refresh_Buffer Refresh_Buffer;
typedef struct Refresh_TransferBuffer Refresh_TransferBuffer;
typedef struct Refresh_Texture Refresh_Texture;
typedef struct Refresh_Sampler Refresh_Sampler;
typedef struct Refresh_Shader Refresh_Shader;
typedef struct Refresh_ComputePipeline Refresh_ComputePipeline;
typedef struct Refresh_GraphicsPipeline Refresh_GraphicsPipeline;
typedef struct Refresh_CommandBuffer Refresh_CommandBuffer;
typedef struct Refresh_RenderPass Refresh_RenderPass;
typedef struct Refresh_ComputePass Refresh_ComputePass;
typedef struct Refresh_CopyPass Refresh_CopyPass;
typedef struct Refresh_Fence Refresh_Fence;

typedef enum Refresh_PrimitiveType
{
    REFRESH_PRIMITIVETYPE_POINTLIST,
    REFRESH_PRIMITIVETYPE_LINELIST,
    REFRESH_PRIMITIVETYPE_LINESTRIP,
    REFRESH_PRIMITIVETYPE_TRIANGLELIST,
    REFRESH_PRIMITIVETYPE_TRIANGLESTRIP
} Refresh_PrimitiveType;

typedef enum Refresh_LoadOp
{
    REFRESH_LOADOP_LOAD,
    REFRESH_LOADOP_CLEAR,
    REFRESH_LOADOP_DONT_CARE
} Refresh_LoadOp;

typedef enum Refresh_StoreOp
{
    REFRESH_STOREOP_STORE,
    REFRESH_STOREOP_DONT_CARE
} Refresh_StoreOp;

typedef enum Refresh_IndexElementSize
{
    REFRESH_INDEXELEMENTSIZE_16BIT,
    REFRESH_INDEXELEMENTSIZE_32BIT
} Refresh_IndexElementSize;

typedef enum Refresh_TextureFormat
{
    /* Unsigned Normalized Float Color Formats */
    REFRESH_TEXTUREFORMAT_R8G8B8A8,
    REFRESH_TEXTUREFORMAT_B8G8R8A8,
    REFRESH_TEXTUREFORMAT_R5G6B5,
    REFRESH_TEXTUREFORMAT_A1R5G5B5,
    REFRESH_TEXTUREFORMAT_B4G4R4A4,
    REFRESH_TEXTUREFORMAT_A2R10G10B10,
    REFRESH_TEXTUREFORMAT_A2B10G10R10,
    REFRESH_TEXTUREFORMAT_R16G16,
    REFRESH_TEXTUREFORMAT_R16G16B16A16,
    REFRESH_TEXTUREFORMAT_R8,
    REFRESH_TEXTUREFORMAT_A8,
    /* Compressed Unsigned Normalized Float Color Formats */
    REFRESH_TEXTUREFORMAT_BC1,
    REFRESH_TEXTUREFORMAT_BC2,
    REFRESH_TEXTUREFORMAT_BC3,
    REFRESH_TEXTUREFORMAT_BC7,
    /* Signed Normalized Float Color Formats  */
    REFRESH_TEXTUREFORMAT_R8G8_SNORM,
    REFRESH_TEXTUREFORMAT_R8G8B8A8_SNORM,
    /* Signed Float Color Formats */
    REFRESH_TEXTUREFORMAT_R16_SFLOAT,
    REFRESH_TEXTUREFORMAT_R16G16_SFLOAT,
    REFRESH_TEXTUREFORMAT_R16G16B16A16_SFLOAT,
    REFRESH_TEXTUREFORMAT_R32_SFLOAT,
    REFRESH_TEXTUREFORMAT_R32G32_SFLOAT,
    REFRESH_TEXTUREFORMAT_R32G32B32A32_SFLOAT,
    /* Unsigned Integer Color Formats */
    REFRESH_TEXTUREFORMAT_R8_UINT,
    REFRESH_TEXTUREFORMAT_R8G8_UINT,
    REFRESH_TEXTUREFORMAT_R8G8B8A8_UINT,
    REFRESH_TEXTUREFORMAT_R16_UINT,
    REFRESH_TEXTUREFORMAT_R16G16_UINT,
    REFRESH_TEXTUREFORMAT_R16G16B16A16_UINT,
    /* SRGB Color Formats */
    REFRESH_TEXTUREFORMAT_R8G8B8A8_SRGB,
    REFRESH_TEXTUREFORMAT_B8G8R8A8_SRGB,
    /* Compressed SRGB Color Formats */
    REFRESH_TEXTUREFORMAT_BC3_SRGB,
    REFRESH_TEXTUREFORMAT_BC7_SRGB,
    /* Depth Formats */
    REFRESH_TEXTUREFORMAT_D16_UNORM,
    REFRESH_TEXTUREFORMAT_D24_UNORM,
    REFRESH_TEXTUREFORMAT_D32_SFLOAT,
    REFRESH_TEXTUREFORMAT_D24_UNORM_S8_UINT,
    REFRESH_TEXTUREFORMAT_D32_SFLOAT_S8_UINT
} Refresh_TextureFormat;

typedef enum Refresh_TextureUsageFlagBits
{
    REFRESH_TEXTUREUSAGE_SAMPLER_BIT = 0x00000001,
    REFRESH_TEXTUREUSAGE_COLOR_TARGET_BIT = 0x00000002,
    REFRESH_TEXTUREUSAGE_DEPTH_STENCIL_TARGET_BIT = 0x00000004,
    REFRESH_TEXTUREUSAGE_GRAPHICS_STORAGE_READ_BIT = 0x00000008,
    REFRESH_TEXTUREUSAGE_COMPUTE_STORAGE_READ_BIT = 0x00000020,
    REFRESH_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE_BIT = 0x00000040
} Refresh_TextureUsageFlagBits;

typedef Uint32 Refresh_TextureUsageFlags;

typedef enum Refresh_TextureType
{
    REFRESH_TEXTURETYPE_2D,
    REFRESH_TEXTURETYPE_3D,
    REFRESH_TEXTURETYPE_CUBE,
} Refresh_TextureType;

typedef enum Refresh_SampleCount
{
    REFRESH_SAMPLECOUNT_1,
    REFRESH_SAMPLECOUNT_2,
    REFRESH_SAMPLECOUNT_4,
    REFRESH_SAMPLECOUNT_8
} Refresh_SampleCount;

typedef enum Refresh_CubeMapFace
{
    REFRESH_CUBEMAPFACE_POSITIVEX,
    REFRESH_CUBEMAPFACE_NEGATIVEX,
    REFRESH_CUBEMAPFACE_POSITIVEY,
    REFRESH_CUBEMAPFACE_NEGATIVEY,
    REFRESH_CUBEMAPFACE_POSITIVEZ,
    REFRESH_CUBEMAPFACE_NEGATIVEZ
} Refresh_CubeMapFace;

typedef enum Refresh_BufferUsageFlagBits
{
    REFRESH_BUFFERUSAGE_VERTEX_BIT = 0x00000001,
    REFRESH_BUFFERUSAGE_INDEX_BIT = 0x00000002,
    REFRESH_BUFFERUSAGE_INDIRECT_BIT = 0x00000004,
    REFRESH_BUFFERUSAGE_GRAPHICS_STORAGE_READ_BIT = 0x00000008,
    REFRESH_BUFFERUSAGE_COMPUTE_STORAGE_READ_BIT = 0x00000020,
    REFRESH_BUFFERUSAGE_COMPUTE_STORAGE_WRITE_BIT = 0x00000040
} Refresh_BufferUsageFlagBits;

typedef Uint32 Refresh_BufferUsageFlags;

typedef enum Refresh_TransferBufferUsage
{
    REFRESH_TRANSFERBUFFERUSAGE_UPLOAD,
    REFRESH_TRANSFERBUFFERUSAGE_DOWNLOAD
} Refresh_TransferBufferUsage;

typedef enum Refresh_ShaderStage
{
    REFRESH_SHADERSTAGE_VERTEX,
    REFRESH_SHADERSTAGE_FRAGMENT
} Refresh_ShaderStage;

typedef enum Refresh_ShaderFormat
{
    REFRESH_SHADERFORMAT_INVALID,
    REFRESH_SHADERFORMAT_SPIRV,    /* Vulkan */
    REFRESH_SHADERFORMAT_HLSL,     /* D3D11, D3D12 */
    REFRESH_SHADERFORMAT_DXBC,     /* D3D11, D3D12 */
    REFRESH_SHADERFORMAT_DXIL,     /* D3D12 */
    REFRESH_SHADERFORMAT_MSL,      /* Metal */
    REFRESH_SHADERFORMAT_METALLIB, /* Metal */
    REFRESH_SHADERFORMAT_SECRET    /* NDA'd platforms */
} Refresh_ShaderFormat;

typedef enum Refresh_VertexElementFormat
{
    REFRESH_VERTEXELEMENTFORMAT_UINT,
    REFRESH_VERTEXELEMENTFORMAT_FLOAT,
    REFRESH_VERTEXELEMENTFORMAT_VECTOR2,
    REFRESH_VERTEXELEMENTFORMAT_VECTOR3,
    REFRESH_VERTEXELEMENTFORMAT_VECTOR4,
    REFRESH_VERTEXELEMENTFORMAT_COLOR,
    REFRESH_VERTEXELEMENTFORMAT_BYTE4,
    REFRESH_VERTEXELEMENTFORMAT_SHORT2,
    REFRESH_VERTEXELEMENTFORMAT_SHORT4,
    REFRESH_VERTEXELEMENTFORMAT_NORMALIZEDSHORT2,
    REFRESH_VERTEXELEMENTFORMAT_NORMALIZEDSHORT4,
    REFRESH_VERTEXELEMENTFORMAT_HALFVECTOR2,
    REFRESH_VERTEXELEMENTFORMAT_HALFVECTOR4
} Refresh_VertexElementFormat;

typedef enum Refresh_VertexInputRate
{
    REFRESH_VERTEXINPUTRATE_VERTEX = 0,
    REFRESH_VERTEXINPUTRATE_INSTANCE = 1
} Refresh_VertexInputRate;

typedef enum Refresh_FillMode
{
    REFRESH_FILLMODE_FILL,
    REFRESH_FILLMODE_LINE
} Refresh_FillMode;

typedef enum Refresh_CullMode
{
    REFRESH_CULLMODE_NONE,
    REFRESH_CULLMODE_FRONT,
    REFRESH_CULLMODE_BACK
} Refresh_CullMode;

typedef enum Refresh_FrontFace
{
    REFRESH_FRONTFACE_COUNTER_CLOCKWISE,
    REFRESH_FRONTFACE_CLOCKWISE
} Refresh_FrontFace;

typedef enum Refresh_CompareOp
{
    REFRESH_COMPAREOP_NEVER,
    REFRESH_COMPAREOP_LESS,
    REFRESH_COMPAREOP_EQUAL,
    REFRESH_COMPAREOP_LESS_OR_EQUAL,
    REFRESH_COMPAREOP_GREATER,
    REFRESH_COMPAREOP_NOT_EQUAL,
    REFRESH_COMPAREOP_GREATER_OR_EQUAL,
    REFRESH_COMPAREOP_ALWAYS
} Refresh_CompareOp;

typedef enum Refresh_StencilOp
{
    REFRESH_STENCILOP_KEEP,
    REFRESH_STENCILOP_ZERO,
    REFRESH_STENCILOP_REPLACE,
    REFRESH_STENCILOP_INCREMENT_AND_CLAMP,
    REFRESH_STENCILOP_DECREMENT_AND_CLAMP,
    REFRESH_STENCILOP_INVERT,
    REFRESH_STENCILOP_INCREMENT_AND_WRAP,
    REFRESH_STENCILOP_DECREMENT_AND_WRAP
} Refresh_StencilOp;

typedef enum Refresh_BlendOp
{
    REFRESH_BLENDOP_ADD,
    REFRESH_BLENDOP_SUBTRACT,
    REFRESH_BLENDOP_REVERSE_SUBTRACT,
    REFRESH_BLENDOP_MIN,
    REFRESH_BLENDOP_MAX
} Refresh_BlendOp;

typedef enum Refresh_BlendFactor
{
    REFRESH_BLENDFACTOR_ZERO,
    REFRESH_BLENDFACTOR_ONE,
    REFRESH_BLENDFACTOR_SRC_COLOR,
    REFRESH_BLENDFACTOR_ONE_MINUS_SRC_COLOR,
    REFRESH_BLENDFACTOR_DST_COLOR,
    REFRESH_BLENDFACTOR_ONE_MINUS_DST_COLOR,
    REFRESH_BLENDFACTOR_SRC_ALPHA,
    REFRESH_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
    REFRESH_BLENDFACTOR_DST_ALPHA,
    REFRESH_BLENDFACTOR_ONE_MINUS_DST_ALPHA,
    REFRESH_BLENDFACTOR_CONSTANT_COLOR,
    REFRESH_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR,
    REFRESH_BLENDFACTOR_SRC_ALPHA_SATURATE
} Refresh_BlendFactor;

typedef enum Refresh_ColorComponentFlagBits
{
    REFRESH_COLORCOMPONENT_R_BIT = 0x00000001,
    REFRESH_COLORCOMPONENT_G_BIT = 0x00000002,
    REFRESH_COLORCOMPONENT_B_BIT = 0x00000004,
    REFRESH_COLORCOMPONENT_A_BIT = 0x00000008
} Refresh_ColorComponentFlagBits;

typedef Uint32 Refresh_ColorComponentFlags;

typedef enum Refresh_Filter
{
    REFRESH_FILTER_NEAREST,
    REFRESH_FILTER_LINEAR
} Refresh_Filter;

typedef enum Refresh_SamplerMipmapMode
{
    REFRESH_SAMPLERMIPMAPMODE_NEAREST,
    REFRESH_SAMPLERMIPMAPMODE_LINEAR
} Refresh_SamplerMipmapMode;

typedef enum Refresh_SamplerAddressMode
{
    REFRESH_SAMPLERADDRESSMODE_REPEAT,
    REFRESH_SAMPLERADDRESSMODE_MIRRORED_REPEAT,
    REFRESH_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
} Refresh_SamplerAddressMode;

/*
 * VSYNC:
 *   Waits for vblank before presenting.
 *   If there is a pending image to present, the new image is enqueued for presentation.
 *   Disallows tearing at the cost of visual latency.
 *   When using this present mode, AcquireSwapchainTexture will block if too many frames are in flight.
 * IMMEDIATE:
 *   Immediately presents.
 *   Lowest latency option, but tearing may occur.
 *   When using this mode, AcquireSwapchainTexture will return NULL if too many frames are in flight.
 * MAILBOX:
 *   Waits for vblank before presenting. No tearing is possible.
 *   If there is a pending image to present, the pending image is replaced by the new image.
 *   Similar to VSYNC, but with reduced visual latency.
 *   When using this mode, AcquireSwapchainTexture will return NULL if too many frames are in flight.
 */
typedef enum Refresh_PresentMode
{
    REFRESH_PRESENTMODE_VSYNC,
    REFRESH_PRESENTMODE_IMMEDIATE,
    REFRESH_PRESENTMODE_MAILBOX
} Refresh_PresentMode;

/*
 * SDR:
 *   B8G8R8A8 or R8G8B8A8 swapchain. Pixel values are in nonlinear sRGB encoding. Blends raw pixel values.
 * SDR_LINEAR:
 *   B8G8R8A8_SRGB or R8G8B8A8_SRGB swapchain. Pixel values are in nonlinear sRGB encoding. Blends in linear space.
 * HDR_EXTENDED_LINEAR:
 *   R16G16B16A16_SFLOAT swapchain. Pixel values are in extended linear encoding. Blends in linear space.
 * HDR10_ST2048:
 *   A2R10G10B10 or A2B10G10R10 swapchain. Pixel values are in PQ ST2048 encoding. Blends raw pixel values. (TODO: verify this)
 */
typedef enum Refresh_SwapchainComposition
{
    REFRESH_SWAPCHAINCOMPOSITION_SDR,
    REFRESH_SWAPCHAINCOMPOSITION_SDR_LINEAR,
    REFRESH_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR,
    REFRESH_SWAPCHAINCOMPOSITION_HDR10_ST2048
} Refresh_SwapchainComposition;

typedef enum Refresh_BackendBits
{
    REFRESH_BACKEND_INVALID = 0,
    REFRESH_BACKEND_VULKAN = 0x0000000000000001,
    REFRESH_BACKEND_D3D11 = 0x0000000000000002,
    REFRESH_BACKEND_METAL = 0x0000000000000004,
    REFRESH_BACKEND_ALL = (REFRESH_BACKEND_VULKAN | REFRESH_BACKEND_D3D11 | REFRESH_BACKEND_METAL)
} Refresh_BackendBits;

typedef Uint64 Refresh_Backend;

/* Structures */

typedef struct Refresh_DepthStencilValue
{
    float depth;
    Uint32 stencil;
} Refresh_DepthStencilValue;

typedef struct Refresh_Rect
{
    Sint32 x;
    Sint32 y;
    Sint32 w;
    Sint32 h;
} Refresh_Rect;

typedef struct Refresh_Color
{
    float r;
    float g;
    float b;
    float a;
} Refresh_Color;

typedef struct Refresh_Viewport
{
    float x;
    float y;
    float w;
    float h;
    float minDepth;
    float maxDepth;
} Refresh_Viewport;

typedef struct Refresh_TextureTransferInfo
{
    Refresh_TransferBuffer *transferBuffer;
    Uint32 offset;      /* starting location of the image data */
    Uint32 imagePitch;  /* number of pixels from one row to the next */
    Uint32 imageHeight; /* number of rows from one layer/depth-slice to the next */
} Refresh_TextureTransferInfo;

typedef struct Refresh_TransferBufferLocation
{
    Refresh_TransferBuffer *transferBuffer;
    Uint32 offset;
} Refresh_TransferBufferLocation;

typedef struct Refresh_TransferBufferRegion
{
    Refresh_TransferBuffer *transferBuffer;
    Uint32 offset;
    Uint32 size;
} Refresh_TransferBufferRegion;

typedef struct Refresh_TextureSlice
{
    Refresh_Texture *texture;
    Uint32 mipLevel;
    Uint32 layer;
} Refresh_TextureSlice;

typedef struct Refresh_TextureLocation
{
    Refresh_TextureSlice textureSlice;
    Uint32 x;
    Uint32 y;
    Uint32 z;
} Refresh_TextureLocation;

typedef struct Refresh_TextureRegion
{
    Refresh_TextureSlice textureSlice;
    Uint32 x;
    Uint32 y;
    Uint32 z;
    Uint32 w;
    Uint32 h;
    Uint32 d;
} Refresh_TextureRegion;

typedef struct Refresh_BufferLocation
{
    Refresh_Buffer *buffer;
    Uint32 offset;
} Refresh_BufferLocation;

typedef struct Refresh_BufferRegion
{
    Refresh_Buffer *buffer;
    Uint32 offset;
    Uint32 size;
} Refresh_BufferRegion;

typedef struct Refresh_IndirectDrawCommand
{
    Uint32 vertexCount;   /* number of vertices to draw */
    Uint32 instanceCount; /* number of instances to draw */
    Uint32 firstVertex;   /* index of the first vertex to draw */
    Uint32 firstInstance; /* ID of the first instance to draw */
} Refresh_IndirectDrawCommand;

typedef struct Refresh_IndexedIndirectDrawCommand
{
    Uint32 indexCount;    /* number of vertices to draw */
    Uint32 instanceCount; /* number of instances to draw */
    Uint32 firstIndex;    /* base index within the index buffer */
    Uint32 vertexOffset;  /* value added to vertex index before indexing into the vertex buffer */
    Uint32 firstInstance; /* ID of the first instance to draw */
} Refresh_IndexedIndirectDrawCommand;

/* State structures */

typedef struct Refresh_SamplerCreateInfo
{
    Refresh_Filter minFilter;
    Refresh_Filter magFilter;
    Refresh_SamplerMipmapMode mipmapMode;
    Refresh_SamplerAddressMode addressModeU;
    Refresh_SamplerAddressMode addressModeV;
    Refresh_SamplerAddressMode addressModeW;
    float mipLodBias;
    SDL_bool anisotropyEnable;
    float maxAnisotropy;
    SDL_bool compareEnable;
    Refresh_CompareOp compareOp;
    float minLod;
    float maxLod;
} Refresh_SamplerCreateInfo;

typedef struct Refresh_VertexBinding
{
    Uint32 binding;
    Uint32 stride;
    Refresh_VertexInputRate inputRate;
    Uint32 stepRate;
} Refresh_VertexBinding;

typedef struct Refresh_VertexAttribute
{
    Uint32 location;
    Uint32 binding;
    Refresh_VertexElementFormat format;
    Uint32 offset;
} Refresh_VertexAttribute;

typedef struct Refresh_VertexInputState
{
    const Refresh_VertexBinding *vertexBindings;
    Uint32 vertexBindingCount;
    const Refresh_VertexAttribute *vertexAttributes;
    Uint32 vertexAttributeCount;
} Refresh_VertexInputState;

typedef struct Refresh_StencilOpState
{
    Refresh_StencilOp failOp;
    Refresh_StencilOp passOp;
    Refresh_StencilOp depthFailOp;
    Refresh_CompareOp compareOp;
} Refresh_StencilOpState;

typedef struct Refresh_ColorAttachmentBlendState
{
    SDL_bool blendEnable;
    Refresh_BlendFactor srcColorBlendFactor;
    Refresh_BlendFactor dstColorBlendFactor;
    Refresh_BlendOp colorBlendOp;
    Refresh_BlendFactor srcAlphaBlendFactor;
    Refresh_BlendFactor dstAlphaBlendFactor;
    Refresh_BlendOp alphaBlendOp;
    Refresh_ColorComponentFlags colorWriteMask;
} Refresh_ColorAttachmentBlendState;

typedef struct Refresh_ShaderCreateInfo
{
    size_t codeSize;
    const Uint8 *code;
    const char *entryPointName;
    Refresh_ShaderFormat format;
    Refresh_ShaderStage stage;
    Uint32 samplerCount;
    Uint32 storageTextureCount;
    Uint32 storageBufferCount;
    Uint32 uniformBufferCount;
} Refresh_ShaderCreateInfo;

typedef struct Refresh_TextureCreateInfo
{
    Uint32 width;
    Uint32 height;
    Uint32 depth;
    SDL_bool isCube;
    Uint32 layerCount;
    Uint32 levelCount;
    Refresh_SampleCount sampleCount;
    Refresh_TextureFormat format;
    Refresh_TextureUsageFlags usageFlags;
} Refresh_TextureCreateInfo;

/* Pipeline state structures */

typedef struct Refresh_RasterizerState
{
    Refresh_FillMode fillMode;
    Refresh_CullMode cullMode;
    Refresh_FrontFace frontFace;
    SDL_bool depthBiasEnable;
    float depthBiasConstantFactor;
    float depthBiasClamp;
    float depthBiasSlopeFactor;
} Refresh_RasterizerState;

typedef struct Refresh_MultisampleState
{
    Refresh_SampleCount multisampleCount;
    Uint32 sampleMask;
} Refresh_MultisampleState;

typedef struct Refresh_DepthStencilState
{
    SDL_bool depthTestEnable;
    SDL_bool depthWriteEnable;
    Refresh_CompareOp compareOp;
    SDL_bool stencilTestEnable;
    Refresh_StencilOpState backStencilState;
    Refresh_StencilOpState frontStencilState;
    Uint32 compareMask;
    Uint32 writeMask;
    Uint32 reference;
} Refresh_DepthStencilState;

typedef struct Refresh_ColorAttachmentDescription
{
    Refresh_TextureFormat format;
    Refresh_ColorAttachmentBlendState blendState;
} Refresh_ColorAttachmentDescription;

typedef struct Refresh_GraphicsPipelineAttachmentInfo
{
    Refresh_ColorAttachmentDescription *colorAttachmentDescriptions;
    Uint32 colorAttachmentCount;
    SDL_bool hasDepthStencilAttachment;
    Refresh_TextureFormat depthStencilFormat;
} Refresh_GraphicsPipelineAttachmentInfo;

typedef struct Refresh_GraphicsPipelineCreateInfo
{
    Refresh_Shader *vertexShader;
    Refresh_Shader *fragmentShader;
    Refresh_VertexInputState vertexInputState;
    Refresh_PrimitiveType primitiveType;
    Refresh_RasterizerState rasterizerState;
    Refresh_MultisampleState multisampleState;
    Refresh_DepthStencilState depthStencilState;
    Refresh_GraphicsPipelineAttachmentInfo attachmentInfo;
    float blendConstants[4];
} Refresh_GraphicsPipelineCreateInfo;

typedef struct Refresh_ComputePipelineCreateInfo
{
    size_t codeSize;
    const Uint8 *code;
    const char *entryPointName;
    Refresh_ShaderFormat format;
    Uint32 readOnlyStorageTextureCount;
    Uint32 readOnlyStorageBufferCount;
    Uint32 readWriteStorageTextureCount;
    Uint32 readWriteStorageBufferCount;
    Uint32 uniformBufferCount;
    Uint32 threadCountX;
    Uint32 threadCountY;
    Uint32 threadCountZ;
} Refresh_ComputePipelineCreateInfo;

typedef struct Refresh_ColorAttachmentInfo
{
    /* The texture slice that will be used as a color attachment by a render pass. */
    Refresh_TextureSlice textureSlice;

    /* Can be ignored by RenderPass if CLEAR is not used */
    Refresh_Color clearColor;

    /* Determines what is done with the texture slice at the beginning of the render pass.
     *
     *   LOAD:
     *     Loads the data currently in the texture slice.
     *
     *   CLEAR:
     *     Clears the texture slice to a single color.
     *
     *   DONT_CARE:
     *     The driver will do whatever it wants with the texture slice memory.
     *     This is a good option if you know that every single pixel will be touched in the render pass.
     */
    Refresh_LoadOp loadOp;

    /* Determines what is done with the texture slice at the end of the render pass.
     *
     *   STORE:
     *     Stores the results of the render pass in the texture slice.
     *
     *   DONT_CARE:
     *     The driver will do whatever it wants with the texture slice memory.
     *     This is often a good option for depth/stencil textures.
     */
    Refresh_StoreOp storeOp;

    /* if SDL_TRUE, cycles the texture if the texture slice is bound and loadOp is not LOAD */
    SDL_bool cycle;
} Refresh_ColorAttachmentInfo;

typedef struct Refresh_DepthStencilAttachmentInfo
{
    /* The texture slice that will be used as the depth stencil attachment by a render pass. */
    Refresh_TextureSlice textureSlice;

    /* Can be ignored by the render pass if CLEAR is not used */
    Refresh_DepthStencilValue depthStencilClearValue;

    /* Determines what is done with the depth values at the beginning of the render pass.
     *
     *   LOAD:
     *     Loads the depth values currently in the texture slice.
     *
     *   CLEAR:
     *     Clears the texture slice to a single depth.
     *
     *   DONT_CARE:
     *     The driver will do whatever it wants with the memory.
     *     This is a good option if you know that every single pixel will be touched in the render pass.
     */
    Refresh_LoadOp loadOp;

    /* Determines what is done with the depth values at the end of the render pass.
     *
     *   STORE:
     *     Stores the depth results in the texture slice.
     *
     *   DONT_CARE:
     *     The driver will do whatever it wants with the texture slice memory.
     *     This is often a good option for depth/stencil textures.
     */
    Refresh_StoreOp storeOp;

    /* Determines what is done with the stencil values at the beginning of the render pass.
     *
     *   LOAD:
     *     Loads the stencil values currently in the texture slice.
     *
     *   CLEAR:
     *     Clears the texture slice to a single stencil value.
     *
     *   DONT_CARE:
     *     The driver will do whatever it wants with the memory.
     *     This is a good option if you know that every single pixel will be touched in the render pass.
     */
    Refresh_LoadOp stencilLoadOp;

    /* Determines what is done with the stencil values at the end of the render pass.
     *
     *   STORE:
     *     Stores the stencil results in the texture slice.
     *
     *   DONT_CARE:
     *     The driver will do whatever it wants with the texture slice memory.
     *     This is often a good option for depth/stencil textures.
     */
    Refresh_StoreOp stencilStoreOp;

    /* if SDL_TRUE, cycles the texture if the texture slice is bound and any load ops are not LOAD */
    SDL_bool cycle;
} Refresh_DepthStencilAttachmentInfo;

/* Binding structs */

typedef struct Refresh_BufferBinding
{
    Refresh_Buffer *buffer;
    Uint32 offset;
} Refresh_BufferBinding;

typedef struct Refresh_TextureSamplerBinding
{
    Refresh_Texture *texture;
    Refresh_Sampler *sampler;
} Refresh_TextureSamplerBinding;

typedef struct Refresh_StorageBufferReadWriteBinding
{
    Refresh_Buffer *buffer;

    /* if SDL_TRUE, cycles the buffer if it is bound. */
    SDL_bool cycle;
} Refresh_StorageBufferReadWriteBinding;

typedef struct Refresh_StorageTextureReadWriteBinding
{
    Refresh_TextureSlice textureSlice;

    /* if SDL_TRUE, cycles the texture if the texture slice is bound. */
    SDL_bool cycle;
} Refresh_StorageTextureReadWriteBinding;

/* Functions */

/* Device */

/**
 * Creates a GPU context.
 *
 * Backends will first be checked for availability in order of bitflags passed using preferredBackends. If none of the backends are available, the remaining backends are checked as fallback renderers.
 *
 * Think of "preferred" backends as those that have pre-built shaders readily available - for example, you would set the REFRESH_BACKEND_VULKAN bit if your game includes SPIR-V shaders. If you generate shaders at runtime (i.e. via SDL_shader) and the library does _not_ provide you with a preferredBackends value, you should pass REFRESH_BACKEND_ALL so that updated versions of SDL can be aware of which backends the application was aware of at compile time. REFRESH_BACKEND_INVALID is an accepted value but is not recommended.
 *
 * \param preferredBackends a bitflag containing the renderers most recognized by the application
 * \param debugMode enable debug mode properties and validations
 * \returns a GPU context on success or NULL on failure
 *
 * \since This function is available since Refresh 2.0.0
 *
 * \sa Refresh_SelectBackend
 * \sa Refresh_DestroyDevice
 */
REFRESHAPI Refresh_Device *Refresh_CreateDevice(
    Refresh_Backend preferredBackends,
    SDL_bool debugMode);

/**
 * Destroys a GPU context previously returned by Refresh_CreateDevice.
 *
 * \param device a GPU Context to destroy
 *
 * \since This function is available since Refresh 2.0.0
 *
 * \sa Refresh_CreateDevice
 */
REFRESHAPI void Refresh_DestroyDevice(Refresh_Device *device);

/**
 * Returns the backend used to create this GPU context.
 *
 * \param device a GPU context to query
 * \returns an Refresh_Backend value, or REFRESH_BACKEND_INVALID on error
 *
 * \since This function is available since Refresh 2.0.0
 *
 * \sa Refresh_SelectBackend
 */
REFRESHAPI Refresh_Backend Refresh_GetBackend(Refresh_Device *device);

/* State Creation */

/**
 * Creates a pipeline object to be used in a compute workflow.
 *
 * \param device a GPU Context
 * \param computePipelineCreateInfo a struct describing the state of the requested compute pipeline
 * \returns a compute pipeline object on success, or NULL on failure
 *
 * \since This function is available since Refresh 2.0.0
 *
 * \sa Refresh_BindComputePipeline
 * \sa Refresh_ReleaseComputePipeline
 */
REFRESHAPI Refresh_ComputePipeline *Refresh_CreateComputePipeline(
    Refresh_Device *device,
    Refresh_ComputePipelineCreateInfo *computePipelineCreateInfo);

/**
 * Creates a pipeline object to be used in a graphics workflow.
 *
 * \param device a GPU Context
 * \param pipelineCreateInfo a struct describing the state of the desired graphics pipeline
 * \returns a graphics pipeline object on success, or NULL on failure
 *
 * \since This function is available since Refresh 2.0.0
 *
 * \sa Refresh_CreateShader
 * \sa Refresh_BindGraphicsPipeline
 * \sa Refresh_ReleaseGraphicsPipeline
 */
REFRESHAPI Refresh_GraphicsPipeline *Refresh_CreateGraphicsPipeline(
    Refresh_Device *device,
    Refresh_GraphicsPipelineCreateInfo *pipelineCreateInfo);

/**
 * Creates a sampler object to be used when binding textures in a graphics workflow.
 *
 * \param device a GPU Context
 * \param samplerCreateInfo a struct describing the state of the desired sampler
 * \returns a sampler object on success, or NULL on failure
 *
 * \since This function is available since Refresh 2.0.0
 *
 * \sa Refresh_BindVertexSamplers
 * \sa Refresh_BindFragmentSamplers
 * \sa SDL_ReleaseSampler
 */
REFRESHAPI Refresh_Sampler *Refresh_CreateSampler(
    Refresh_Device *device,
    Refresh_SamplerCreateInfo *samplerCreateInfo);

/**
 * Creates a shader to be used when creating a graphics pipeline.
 *
 * \param device a GPU Context
 * \param shaderCreateInfo a struct describing the state of the desired shader
 * \returns a shader object on success, or NULL on failure
 *
 * \since This function is available since Refresh 2.0.0
 *
 * \sa Refresh_CreateGraphicsPipeline
 * \sa Refresh_ReleaseShader
 */
REFRESHAPI Refresh_Shader *Refresh_CreateShader(
    Refresh_Device *device,
    Refresh_ShaderCreateInfo *shaderCreateInfo);

/**
 * Creates a texture object to be used in graphics or compute workflows.
 * The contents of this texture are undefined until data is written to the texture.
 *
 * Note that certain combinations of usage flags are invalid.
 * For example, a texture cannot have both the SAMPLER and GRAPHICS_STORAGE_READ flags.
 *
 * If you request a sample count higher than the hardware supports,
 * the implementation will automatically fall back to the highest available sample count.
 *
 * \param device a GPU Context
 * \param textureCreateInfo a struct describing the state of the texture to create
 * \returns a texture object on success, or NULL on failure
 *
 * \since This function is available since Refresh 2.0.0
 *
 * \sa Refresh_UploadToTexture
 * \sa Refresh_DownloadFromTexture
 * \sa Refresh_BindVertexSamplers
 * \sa Refresh_BindVertexStorageTextures
 * \sa Refresh_BindFragmentSamplers
 * \sa Refresh_BindFragmentStorageTextures
 * \sa Refresh_BindComputeStorageTextures
 * \sa Refresh_Blit
 * \sa Refresh_ReleaseTexture
 */
REFRESHAPI Refresh_Texture *Refresh_CreateTexture(
    Refresh_Device *device,
    Refresh_TextureCreateInfo *textureCreateInfo);

/**
 * Creates a buffer object to be used in graphics or compute workflows.
 * The contents of this buffer are undefined until data is written to the buffer.
 *
 * Note that certain combinations of usage flags are invalid.
 * For example, a buffer cannot have both the VERTEX and INDEX flags.
 *
 * \param device a GPU Context
 * \param usageFlags bitflag mask hinting at how the buffer will be used
 * \param sizeInBytes the size of the buffer
 * \returns a buffer object on success, or NULL on failure
 *
 * \since This function is available since Refresh 2.0.0
 *
 * \sa Refresh_UploadToBuffer
 * \sa Refresh_BindVertexBuffers
 * \sa Refresh_BindIndexBuffer
 * \sa Refresh_BindVertexStorageBuffers
 * \sa Refresh_BindFragmentStorageBuffers
 * \sa Refresh_BindComputeStorageBuffers
 * \sa Refresh_ReleaseBuffer
 */
REFRESHAPI Refresh_Buffer *Refresh_CreateBuffer(
    Refresh_Device *device,
    Refresh_BufferUsageFlags usageFlags,
    Uint32 sizeInBytes);

/**
 * Creates a transfer buffer to be used when uploading to or downloading from graphics resources.
 *
 * \param device a GPU Context
 * \param usage whether the transfer buffer will be used for uploads or downloads
 * \param sizeInBytes the size of the transfer buffer
 * \returns a transfer buffer on success, or NULL on failure
 *
 * \since This function is available since Refresh 2.0.0
 *
 * \sa Refresh_UploadToBuffer
 * \sa Refresh_DownloadFromBuffer
 * \sa Refresh_UploadToTexture
 * \sa Refresh_DownloadFromTexture
 * \sa Refresh_ReleaseTransferBuffer
 */
REFRESHAPI Refresh_TransferBuffer *Refresh_CreateTransferBuffer(
    Refresh_Device *device,
    Refresh_TransferBufferUsage usage,
    Uint32 sizeInBytes);

/* Debug Naming */

/**
 * Sets an arbitrary string constant to label a buffer. Useful for debugging.
 *
 * \param device a GPU Context
 * \param buffer a buffer to attach the name to
 * \param text a UTF-8 string constant to mark as the name of the buffer
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_SetBufferName(
    Refresh_Device *device,
    Refresh_Buffer *buffer,
    const char *text);

/**
 * Sets an arbitrary string constant to label a texture. Useful for debugging.
 *
 * \param device a GPU Context
 * \param texture a texture to attach the name to
 * \param text a UTF-8 string constant to mark as the name of the texture
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_SetTextureName(
    Refresh_Device *device,
    Refresh_Texture *texture,
    const char *text);

/**
 * Inserts an arbitrary string label into the command buffer callstream.
 * Useful for debugging.
 *
 * \param commandBuffer a command buffer
 * \param text a UTF-8 string constant to insert as the label
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_InsertDebugLabel(
    Refresh_CommandBuffer *commandBuffer,
    const char *text);

/**
 * Begins a debug group with an arbitary name.
 * Used for denoting groups of calls when viewing the command buffer callstream
 * in a graphics debugging tool.
 *
 * Each call to Refresh_PushDebugGroup must have a corresponding call to Refresh_PopDebugGroup.
 *
 * On some backends (e.g. Metal), pushing a debug group during a render/blit/compute pass
 * will create a group that is scoped to the native pass rather than the command buffer.
 * For best results, if you push a debug group during a pass, always pop it in the same pass.
 *
 * \param commandBuffer a command buffer
 * \param name a UTF-8 string constant that names the group
 *
 * \since This function is available since Refresh 2.0.0
 *
 * \sa Refresh_PopDebugGroup
 */
REFRESHAPI void Refresh_PushDebugGroup(
    Refresh_CommandBuffer *commandBuffer,
    const char *name);

/**
 * Ends the most-recently pushed debug group.
 *
 * \param commandBuffer a command buffer
 *
 * \since This function is available since Refresh 2.0.0
 *
 * \sa Refresh_PushDebugGroup
 */
REFRESHAPI void Refresh_PopDebugGroup(
    Refresh_CommandBuffer *commandBuffer);

/* Disposal */

/**
 * Frees the given texture as soon as it is safe to do so.
 * You must not reference the texture after calling this function.
 *
 * \param device a GPU context
 * \param texture a texture to be destroyed
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_ReleaseTexture(
    Refresh_Device *device,
    Refresh_Texture *texture);

/**
 * Frees the given sampler as soon as it is safe to do so.
 * You must not reference the texture after calling this function.
 *
 * \param device a GPU context
 * \param sampler a sampler to be destroyed
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_ReleaseSampler(
    Refresh_Device *device,
    Refresh_Sampler *sampler);

/**
 * Frees the given buffer as soon as it is safe to do so.
 * You must not reference the buffer after calling this function.
 *
 * \param device a GPU context
 * \param buffer a buffer to be destroyed
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_ReleaseBuffer(
    Refresh_Device *device,
    Refresh_Buffer *buffer);

/**
 * Frees the given transfer buffer as soon as it is safe to do so.
 * You must not reference the transfer buffer after calling this function.
 *
 * \param device a GPU context
 * \param transferBuffer a transfer buffer to be destroyed
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_ReleaseTransferBuffer(
    Refresh_Device *device,
    Refresh_TransferBuffer *transferBuffer);

/**
 * Frees the given compute pipeline as soon as it is safe to do so.
 * You must not reference the compute pipeline after calling this function.
 *
 * \param device a GPU context
 * \param computePipeline a compute pipeline to be destroyed
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_ReleaseComputePipeline(
    Refresh_Device *device,
    Refresh_ComputePipeline *computePipeline);

/**
 * Frees the given shader as soon as it is safe to do so.
 * You must not reference the shader after calling this function.
 *
 * \param device a GPU context
 * \param shader a shader to be destroyed
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_ReleaseShader(
    Refresh_Device *device,
    Refresh_Shader *shader);

/**
 * Frees the given graphics pipeline as soon as it is safe to do so.
 * You must not reference the graphics pipeline after calling this function.
 *
 * \param device a GPU context
 * \param graphicsPipeline a graphics pipeline to be destroyed
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_ReleaseGraphicsPipeline(
    Refresh_Device *device,
    Refresh_GraphicsPipeline *graphicsPipeline);

/*
 * COMMAND BUFFERS
 *
 * Render state is managed via command buffers.
 * When setting render state, that state is always local to the command buffer.
 *
 * Commands only begin execution on the GPU once Submit is called.
 * Once the command buffer is submitted, it is no longer valid to use it.
 *
 * In multi-threading scenarios, you should acquire and submit a command buffer on the same thread.
 * As long as you satisfy this requirement, all functionality related to command buffers is thread-safe.
 */

/**
 * Acquire a command buffer.
 * This command buffer is managed by the implementation and should not be freed by the user.
 * The command buffer may only be used on the thread it was acquired on.
 * The command buffer should be submitted on the thread it was acquired on.
 *
 * \param device a GPU context
 * \returns a command buffer
 *
 * \since This function is available since Refresh 2.0.0
 *
 * \sa Refresh_Submit
 * \sa Refresh_SubmitAndAcquireFence
 */
REFRESHAPI Refresh_CommandBuffer *Refresh_AcquireCommandBuffer(
    Refresh_Device *device);

/*
 * UNIFORM DATA
 *
 * Uniforms are for passing data to shaders.
 * The uniform data will be constant across all executions of the shader.
 *
 * There are 4 available uniform slots per shader stage (vertex, fragment, compute).
 * Uniform data pushed to a slot on a stage keeps its value throughout the command buffer
 * until you call the relevant Push function on that slot again.
 *
 * For example, you could write your vertex shaders to read a camera matrix from uniform binding slot 0,
 * push the camera matrix at the start of the command buffer, and that data will be used for every
 * subsequent draw call.
 *
 * It is valid to push uniform data during a render or compute pass.
 *
 * Uniforms are best for pushing small amounts of data.
 * If you are pushing more than a matrix or two per call you should consider using a storage buffer instead.
 */

/**
 * Pushes data to a vertex uniform slot on the command buffer.
 * Subsequent draw calls will use this uniform data.
 *
 * \param commandBuffer a command buffer
 * \param slotIndex the vertex uniform slot to push data to
 * \param data client data to write
 * \param dataLengthInBytes the length of the data to write
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_PushVertexUniformData(
    Refresh_CommandBuffer *commandBuffer,
    Uint32 slotIndex,
    const void *data,
    Uint32 dataLengthInBytes);

/**
 * Pushes data to a fragment uniform slot on the command buffer.
 * Subsequent draw calls will use this uniform data.
 *
 * \param commandBuffer a command buffer
 * \param slotIndex the fragment uniform slot to push data to
 * \param data client data to write
 * \param dataLengthInBytes the length of the data to write
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_PushFragmentUniformData(
    Refresh_CommandBuffer *commandBuffer,
    Uint32 slotIndex,
    const void *data,
    Uint32 dataLengthInBytes);

/**
 * Pushes data to a uniform slot on the command buffer.
 * Subsequent draw calls will use this uniform data.
 *
 * \param commandBuffer a command buffer
 * \param slotIndex the uniform slot to push data to
 * \param data client data to write
 * \param dataLengthInBytes the length of the data to write
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_PushComputeUniformData(
    Refresh_CommandBuffer *commandBuffer,
    Uint32 slotIndex,
    const void *data,
    Uint32 dataLengthInBytes);

/*
 * A NOTE ON CYCLING
 *
 * When using a command buffer, operations do not occur immediately -
 * they occur some time after the command buffer is submitted.
 *
 * When a resource is used in a pending or active command buffer, it is considered to be "bound".
 * When a resource is no longer used in any pending or active command buffers, it is considered to be "unbound".
 *
 * If data resources are bound, it is unspecified when that data will be unbound
 * unless you acquire a fence when submitting the command buffer and wait on it.
 * However, this doesn't mean you need to track resource usage manually.
 *
 * All of the functions and structs that involve writing to a resource have a "cycle" bool.
 * GpuTransferBuffer, GpuBuffer, and GpuTexture all effectively function as ring buffers on internal resources.
 * When cycle is SDL_TRUE, if the resource is bound, the cycle rotates to the next unbound internal resource,
 * or if none are available, a new one is created.
 * This means you don't have to worry about complex state tracking and synchronization as long as cycling is correctly employed.
 *
 * For example: you can call SetTransferData and then UploadToTexture. The next time you call SetTransferData,
 * if you set the cycle param to SDL_TRUE, you don't have to worry about overwriting any data that is not yet uploaded.
 *
 * Another example: If you are using a texture in a render pass every frame, this can cause a data dependency between frames.
 * If you set cycle to SDL_TRUE in the ColorAttachmentInfo struct, you can prevent this data dependency.
 *
 * Note that all functions which write to a texture specifically write to a GpuTextureSlice,
 * and these slices themselves are tracked for binding.
 * The GpuTexture will only cycle if the specific GpuTextureSlice being written to is bound.
 *
 * Cycling will never undefine already bound data.
 * When cycling, all data in the resource is considered to be undefined for subsequent commands until that data is written again.
 * You must take care not to read undefined data.
 *
 * You must also take care not to overwrite a section of data that has been referenced in a command without cycling first.
 * It is OK to overwrite unreferenced data in a bound resource without cycling,
 * but overwriting a section of data that has already been referenced will produce unexpected results.
 */

/* Graphics State */

/**
 * Begins a render pass on a command buffer.
 * A render pass consists of a set of texture slices, clear values, and load/store operations
 * which will be rendered to during the render pass.
 * All operations related to graphics pipelines must take place inside of a render pass.
 * A default viewport and scissor state are automatically set when this is called.
 * You cannot begin another render pass, or begin a compute pass or copy pass
 * until you have ended the render pass.
 *
 * \param commandBuffer a command buffer
 * \param colorAttachmentInfos an array of Refresh_ColorAttachmentInfo structs
 * \param colorAttachmentCount the number of color attachments in the colorAttachmentInfos array
 * \param depthStencilAttachmentInfo the depth-stencil target and clear value, may be NULL
 * \returns a render pass handle
 *
 * \since This function is available since Refresh 2.0.0
 *
 * \sa Refresh_EndRenderPass
 */
REFRESHAPI Refresh_RenderPass *Refresh_BeginRenderPass(
    Refresh_CommandBuffer *commandBuffer,
    Refresh_ColorAttachmentInfo *colorAttachmentInfos,
    Uint32 colorAttachmentCount,
    Refresh_DepthStencilAttachmentInfo *depthStencilAttachmentInfo);

/**
 * Binds a graphics pipeline on a render pass to be used in rendering.
 * A graphics pipeline must be bound before making any draw calls.
 *
 * \param renderPass a render pass handle
 * \param graphicsPipeline the graphics pipeline to bind
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_BindGraphicsPipeline(
    Refresh_RenderPass *renderPass,
    Refresh_GraphicsPipeline *graphicsPipeline);

/**
 * Sets the current viewport state on a command buffer.
 *
 * \param renderPass a render pass handle
 * \param viewport the viewport to set
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_SetViewport(
    Refresh_RenderPass *renderPass,
    Refresh_Viewport *viewport);

/**
 * Sets the current scissor state on a command buffer.
 *
 * \param renderPass a render pass handle
 * \param scissor the scissor area to set
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_SetScissor(
    Refresh_RenderPass *renderPass,
    Refresh_Rect *scissor);

/**
 * Binds vertex buffers on a command buffer for use with subsequent draw calls.
 *
 * \param renderPass a render pass handle
 * \param firstBinding the starting bind point for the vertex buffers
 * \param pBindings an array of Refresh_BufferBinding structs containing vertex buffers and offset values
 * \param bindingCount the number of bindings in the pBindings array
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_BindVertexBuffers(
    Refresh_RenderPass *renderPass,
    Uint32 firstBinding,
    Refresh_BufferBinding *pBindings,
    Uint32 bindingCount);

/**
 * Binds an index buffer on a command buffer for use with subsequent draw calls.
 *
 * \param renderPass a render pass handle
 * \param pBinding a pointer to a struct containing an index buffer and offset
 * \param indexElementSize whether the index values in the buffer are 16- or 32-bit
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_BindIndexBuffer(
    Refresh_RenderPass *renderPass,
    Refresh_BufferBinding *pBinding,
    Refresh_IndexElementSize indexElementSize);

/**
 * Binds texture-sampler pairs for use on the vertex shader.
 * The textures must have been created with REFRESH_TEXTUREUSAGE_SAMPLER_BIT.
 *
 * \param renderPass a render pass handle
 * \param firstSlot the vertex sampler slot to begin binding from
 * \param textureSamplerBindings an array of texture-sampler binding structs
 * \param bindingCount the number of texture-sampler pairs to bind from the array
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_BindVertexSamplers(
    Refresh_RenderPass *renderPass,
    Uint32 firstSlot,
    Refresh_TextureSamplerBinding *textureSamplerBindings,
    Uint32 bindingCount);

/**
 * Binds storage textures for use on the vertex shader.
 * These textures must have been created with REFRESH_TEXTUREUSAGE_GRAPHICS_STORAGE_READ_BIT.
 *
 * \param renderPass a render pass handle
 * \param firstSlot the vertex storage texture slot to begin binding from
 * \param storageTextureSlices an array of storage texture slices
 * \param bindingCount the number of storage texture slices to bind from the array
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_BindVertexStorageTextures(
    Refresh_RenderPass *renderPass,
    Uint32 firstSlot,
    Refresh_TextureSlice *storageTextureSlices,
    Uint32 bindingCount);

/**
 * Binds storage buffers for use on the vertex shader.
 * These buffers must have been created with REFRESH_BUFFERUSAGE_GRAPHICS_STORAGE_READ_BIT.
 *
 * \param renderPass a render pass handle
 * \param firstSlot the vertex storage buffer slot to begin binding from
 * \param storageBuffers an array of buffers
 * \param bindingCount the number of buffers to bind from the array
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_BindVertexStorageBuffers(
    Refresh_RenderPass *renderPass,
    Uint32 firstSlot,
    Refresh_Buffer **storageBuffers,
    Uint32 bindingCount);

/**
 * Binds texture-sampler pairs for use on the fragment shader.
 * The textures must have been created with REFRESH_TEXTUREUSAGE_SAMPLER_BIT.
 *
 * \param renderPass a render pass handle
 * \param firstSlot the fragment sampler slot to begin binding from
 * \param textureSamplerBindings an array of texture-sampler binding structs
 * \param bindingCount the number of texture-sampler pairs to bind from the array
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_BindFragmentSamplers(
    Refresh_RenderPass *renderPass,
    Uint32 firstSlot,
    Refresh_TextureSamplerBinding *textureSamplerBindings,
    Uint32 bindingCount);

/**
 * Binds storage textures for use on the fragment shader.
 * These textures must have been created with REFRESH_TEXTUREUSAGE_GRAPHICS_STORAGE_READ_BIT.
 *
 * \param renderPass a render pass handle
 * \param firstSlot the fragment storage texture slot to begin binding from
 * \param storageTextureSlices an array of storage texture slices
 * \param bindingCount the number of storage texture slices to bind from the array
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_BindFragmentStorageTextures(
    Refresh_RenderPass *renderPass,
    Uint32 firstSlot,
    Refresh_TextureSlice *storageTextureSlices,
    Uint32 bindingCount);

/**
 * Binds storage buffers for use on the fragment shader.
 * These buffers must have been created with REFRESH_BUFFERUSAGE_GRAPHICS_STORAGE_READ_BIT.
 *
 * \param renderPass a render pass handle
 * \param firstSlot the fragment storage buffer slot to begin binding from
 * \param storageBuffers an array of storage buffers
 * \param bindingCount the number of storage buffers to bind from the array
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_BindFragmentStorageBuffers(
    Refresh_RenderPass *renderPass,
    Uint32 firstSlot,
    Refresh_Buffer **storageBuffers,
    Uint32 bindingCount);

/* Drawing */

/**
 * Draws data using bound graphics state with an index buffer and instancing enabled.
 * You must not call this function before binding a graphics pipeline.
 *
 * \param renderPass a render pass handle
 * \param baseVertex the starting offset to read from the vertex buffer
 * \param startIndex the starting offset to read from the index buffer
 * \param primitiveCount the number of primitives to draw
 * \param instanceCount the number of instances that will be drawn
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_DrawIndexedPrimitives(
    Refresh_RenderPass *renderPass,
    Uint32 baseVertex,
    Uint32 startIndex,
    Uint32 primitiveCount,
    Uint32 instanceCount);

/**
 * Draws data using bound graphics state.
 * You must not call this function before binding a graphics pipeline.
 *
 * \param renderPass a render pass handle
 * \param vertexStart The starting offset to read from the vertex buffer
 * \param primitiveCount The number of primitives to draw
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_DrawPrimitives(
    Refresh_RenderPass *renderPass,
    Uint32 vertexStart,
    Uint32 primitiveCount);

/**
 * Draws data using bound graphics state and with draw parameters set from a buffer.
 * The buffer layout should match the layout of Refresh_IndirectDrawCommand.
 * You must not call this function before binding a graphics pipeline.
 *
 * \param renderPass a render pass handle
 * \param buffer a buffer containing draw parameters
 * \param offsetInBytes the offset to start reading from the draw buffer
 * \param drawCount the number of draw parameter sets that should be read from the draw buffer
 * \param stride the byte stride between sets of draw parameters
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_DrawPrimitivesIndirect(
    Refresh_RenderPass *renderPass,
    Refresh_Buffer *buffer,
    Uint32 offsetInBytes,
    Uint32 drawCount,
    Uint32 stride);

/**
 * Draws data using bound graphics state with an index buffer enabled
 * and with draw parameters set from a buffer.
 * The buffer layout should match the layout of Refresh_IndexedIndirectDrawCommand.
 * You must not call this function before binding a graphics pipeline.
 *
 * \param renderPass a render pass handle
 * \param buffer a buffer containing draw parameters
 * \param offsetInBytes the offset to start reading from the draw buffer
 * \param drawCount the number of draw parameter sets that should be read from the draw buffer
 * \param stride the byte stride between sets of draw parameters
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_DrawIndexedPrimitivesIndirect(
    Refresh_RenderPass *renderPass,
    Refresh_Buffer *buffer,
    Uint32 offsetInBytes,
    Uint32 drawCount,
    Uint32 stride);

/**
 * Ends the given render pass.
 * All bound graphics state on the render pass command buffer is unset.
 * The render pass handle is now invalid.
 *
 * \param renderPass a render pass handle
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_EndRenderPass(
    Refresh_RenderPass *renderPass);

/* Compute Pass */

/**
 * Begins a compute pass on a command buffer.
 * A compute pass is defined by a set of texture slices and buffers that
 * will be written to by compute pipelines.
 * These textures and buffers must have been created with the COMPUTE_STORAGE_WRITE bit.
 * If these resources will also be read during the pass, they must be created with the COMPUTE_STORAGE_READ bit.
 * All operations related to compute pipelines must take place inside of a compute pass.
 * You must not begin another compute pass, or a render pass or copy pass
 * before ending the compute pass.
 *
 * \param commandBuffer a command buffer
 * \param storageTextureBindings an array of writeable storage texture binding structs
 * \param storageTextureBindingCount the number of storage textures to bind from the array
 * \param storageBufferBindings an array of writeable storage buffer binding structs
 * \param storageBufferBindingCount an array of read-write storage buffer binding structs
 *
 * \returns a compute pass handle
 *
 * \since This function is available since Refresh 2.0.0
 *
 * \sa Refresh_EndComputePass
 */
REFRESHAPI Refresh_ComputePass *Refresh_BeginComputePass(
    Refresh_CommandBuffer *commandBuffer,
    Refresh_StorageTextureReadWriteBinding *storageTextureBindings,
    Uint32 storageTextureBindingCount,
    Refresh_StorageBufferReadWriteBinding *storageBufferBindings,
    Uint32 storageBufferBindingCount);

/**
 * Binds a compute pipeline on a command buffer for use in compute dispatch.
 *
 * \param computePass a compute pass handle
 * \param computePipeline a compute pipeline to bind
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_BindComputePipeline(
    Refresh_ComputePass *computePass,
    Refresh_ComputePipeline *computePipeline);

/**
 * Binds storage textures as readonly for use on the compute pipeline.
 * These textures must have been created with REFRESH_TEXTUREUSAGE_COMPUTE_STORAGE_READ_BIT.
 *
 * \param computePass a compute pass handle
 * \param firstSlot the compute storage texture slot to begin binding from
 * \param storageTextureSlices an array of storage texture binding structs
 * \param bindingCount the number of storage textures to bind from the array
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_BindComputeStorageTextures(
    Refresh_ComputePass *computePass,
    Uint32 firstSlot,
    Refresh_TextureSlice *storageTextureSlices,
    Uint32 bindingCount);

/**
 * Binds storage buffers as readonly for use on the compute pipeline.
 * These buffers must have been created with REFRESH_BUFFERUSAGE_COMPUTE_STORAGE_READ_BIT.
 *
 * \param computePass a compute pass handle
 * \param firstSlot the compute storage buffer slot to begin binding from
 * \param storageBuffers an array of storage buffer binding structs
 * \param bindingCount the number of storage buffers to bind from the array
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_BindComputeStorageBuffers(
    Refresh_ComputePass *computePass,
    Uint32 firstSlot,
    Refresh_Buffer **storageBuffers,
    Uint32 bindingCount);

/**
 * Dispatches compute work.
 * You must not call this function before binding a compute pipeline.
 *
 * A VERY IMPORTANT NOTE
 * If you dispatch multiple times in a compute pass,
 * and the dispatches write to the same resource region as each other,
 * there is no guarantee of which order the writes will occur.
 * If the write order matters, you MUST end the compute pass and begin another one.
 *
 * \param computePass a compute pass handle
 * \param groupCountX number of local workgroups to dispatch in the X dimension
 * \param groupCountY number of local workgroups to dispatch in the Y dimension
 * \param groupCountZ number of local workgroups to dispatch in the Z dimension
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_DispatchCompute(
    Refresh_ComputePass *computePass,
    Uint32 groupCountX,
    Uint32 groupCountY,
    Uint32 groupCountZ);

/**
 * Ends the current compute pass.
 * All bound compute state on the command buffer is unset.
 * The compute pass handle is now invalid.
 *
 * \param computePass a compute pass handle
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_EndComputePass(
    Refresh_ComputePass *computePass);

/* TransferBuffer Data */

/**
 * Maps a transfer buffer into application address space.
 * You must unmap the transfer buffer before encoding upload commands.
 *
 * \param device a GPU context
 * \param transferBuffer a transfer buffer
 * \param cycle if SDL_TRUE, cycles the transfer buffer if it is bound
 * \param ppData where to store the address of the mapped transfer buffer memory
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_MapTransferBuffer(
    Refresh_Device *device,
    Refresh_TransferBuffer *transferBuffer,
    SDL_bool cycle,
    void **ppData);

/**
 * Unmaps a previously mapped transfer buffer.
 *
 * \param device a GPU context
 * \param transferBuffer a previously mapped transfer buffer
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_UnmapTransferBuffer(
    Refresh_Device *device,
    Refresh_TransferBuffer *transferBuffer);

/**
 * Immediately copies data from a pointer to a transfer buffer.
 *
 * \param device a GPU context
 * \param source a pointer to data to copy into the transfer buffer
 * \param destination a transfer buffer with offset and size
 * \param cycle if SDL_TRUE, cycles the transfer buffer if it is bound, otherwise overwrites the data.
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_SetTransferData(
    Refresh_Device *device,
    const void *source,
    Refresh_TransferBufferRegion *destination,
    SDL_bool cycle);

/**
 * Immediately copies data from a transfer buffer to a pointer.
 *
 * \param device a GPU context
 * \param source a transfer buffer with offset and size
 * \param destination a data pointer
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_GetTransferData(
    Refresh_Device *device,
    Refresh_TransferBufferRegion *source,
    void *destination);

/* Copy Pass */

/**
 * Begins a copy pass on a command buffer.
 * All operations related to copying to or from buffers or textures take place inside a copy pass.
 * You must not begin another copy pass, or a render pass or compute pass
 * before ending the copy pass.
 *
 * \param commandBuffer a command buffer
 * \returns a copy pass handle
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI Refresh_CopyPass *Refresh_BeginCopyPass(
    Refresh_CommandBuffer *commandBuffer);

/**
 * Uploads data from a transfer buffer to a texture.
 * The upload occurs on the GPU timeline.
 * You may assume that the upload has finished in subsequent commands.
 *
 * You must align the data in the transfer buffer to a multiple of
 * the texel size of the texture format.
 *
 * \param copyPass a copy pass handle
 * \param source the source transfer buffer with image layout information
 * \param destination the destination texture region
 * \param cycle if SDL_TRUE, cycles the texture if the texture slice is bound, otherwise overwrites the data.
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_UploadToTexture(
    Refresh_CopyPass *copyPass,
    Refresh_TextureTransferInfo *source,
    Refresh_TextureRegion *destination,
    SDL_bool cycle);

/* Uploads data from a TransferBuffer to a Buffer. */

/**
 * Uploads data from a transfer buffer to a buffer.
 * The upload occurs on the GPU timeline.
 * You may assume that the upload has finished in subsequent commands.
 *
 * \param copyPass a copy pass handle
 * \param source the source transfer buffer with offset
 * \param destination the destination buffer with offset and size
 * \param cycle if SDL_TRUE, cycles the buffer if it is bound, otherwise overwrites the data.
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_UploadToBuffer(
    Refresh_CopyPass *copyPass,
    Refresh_TransferBufferLocation *source,
    Refresh_BufferRegion *destination,
    SDL_bool cycle);

/**
 * Performs a texture-to-texture copy.
 * This copy occurs on the GPU timeline.
 * You may assume the copy has finished in subsequent commands.
 *
 * \param copyPass a copy pass handle
 * \param source a source texture region
 * \param destination a destination texture region
 * \param w the width of the region to copy
 * \param h the height of the region to copy
 * \param d the depth of the region to copy
 * \param cycle if SDL_TRUE, cycles the destination texture if the destination texture slice is bound, otherwise overwrites the data.
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_CopyTextureToTexture(
    Refresh_CopyPass *copyPass,
    Refresh_TextureLocation *source,
    Refresh_TextureLocation *destination,
    Uint32 w,
    Uint32 h,
    Uint32 d,
    SDL_bool cycle);

/* Copies data from a buffer to a buffer. */

/**
 * Performs a buffer-to-buffer copy.
 * This copy occurs on the GPU timeline.
 * You may assume the copy has finished in subsequent commands.
 *
 * \param copyPass a copy pass handle
 * \param source the buffer and offset to copy from
 * \param destination the buffer and offset to copy to
 * \param size the length of the buffer to copy
 * \param cycle if SDL_TRUE, cycles the destination buffer if it is bound, otherwise overwrites the data.
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_CopyBufferToBuffer(
    Refresh_CopyPass *copyPass,
    Refresh_BufferLocation *source,
    Refresh_BufferLocation *destination,
    Uint32 size,
    SDL_bool cycle);

/**
 * Generates mipmaps for the given texture.
 *
 * \param copyPass a copy pass handle
 * \param texture a texture with more than 1 mip level
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_GenerateMipmaps(
    Refresh_CopyPass *copyPass,
    Refresh_Texture *texture);

/**
 * Copies data from a texture to a transfer buffer on the GPU timeline.
 * This data is not guaranteed to be copied until the command buffer fence is signaled.
 *
 * \param copyPass a copy pass handle
 * \param source the source texture region
 * \param destination the destination transfer buffer with image layout information
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_DownloadFromTexture(
    Refresh_CopyPass *copyPass,
    Refresh_TextureRegion *source,
    Refresh_TextureTransferInfo *destination);

/**
 * Copies data from a buffer to a transfer buffer on the GPU timeline.
 * This data is not guaranteed to be copied until the command buffer fence is signaled.
 *
 * \param copyPass a copy pass handle
 * \param source the source buffer with offset and size
 * \param destination the destination transfer buffer with offset
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_DownloadFromBuffer(
    Refresh_CopyPass *copyPass,
    Refresh_BufferRegion *source,
    Refresh_TransferBufferLocation *destination);

/**
 * Ends the current copy pass.
 *
 * \param copyPass a copy pass handle
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_EndCopyPass(
    Refresh_CopyPass *copyPass);

/**
 * Blits from a source texture region to a destination texture region.
 * This function must not be called inside of any render, compute, or copy pass.
 *
 * \param commandBuffer a command buffer
 * \param source the texture region to copy from
 * \param destination the texture region to copy to
 * \param filterMode the filter mode that will be used when blitting
 * \param cycle if SDL_TRUE, cycles the destination texture if the destination texture slice is bound, otherwise overwrites the data.
 *
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI void Refresh_Blit(
    Refresh_CommandBuffer *commandBuffer,
    Refresh_TextureRegion *source,
    Refresh_TextureRegion *destination,
    Refresh_Filter filterMode,
    SDL_bool cycle);

/* Submission/Presentation */

/**
 * Obtains whether or not a swapchain composition is supported by the GPU backend.
 *
 * \param device a GPU context
 * \param window an SDL_Window
 * \param swapchainComposition the swapchain composition to check
 *
 * \returns SDL_TRUE if supported, SDL_FALSE if unsupported (or on error)
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI SDL_bool Refresh_SupportsSwapchainComposition(
    Refresh_Device *device,
    SDL_Window *window,
    Refresh_SwapchainComposition swapchainComposition);

/**
 * Obtains whether or not a presentation mode is supported by the GPU backend.
 *
 * \param device a GPU context
 * \param window an SDL_Window
 * \param presentMode the presentation mode to check
 *
 * \returns SDL_TRUE if supported, SDL_FALSE if unsupported (or on error)
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI SDL_bool Refresh_SupportsPresentMode(
    Refresh_Device *device,
    SDL_Window *window,
    Refresh_PresentMode presentMode);

/**
 * Claims a window, creating a swapchain structure for it.
 * This must be called before Refresh_AcquireSwapchainTexture is called using the window.
 *
 * This function will fail if the requested present mode or swapchain composition
 * are unsupported by the device. Check if the parameters are supported via
 * Refresh_SupportsPresentMode / Refresh_SupportsSwapchainComposition prior to
 * calling this function.
 *
 * REFRESH_PRESENTMODE_VSYNC and REFRESH_SWAPCHAINCOMPOSITION_SDR are
 * always supported.
 *
 * \param device a GPU context
 * \param window an SDL_Window
 * \param swapchainComposition the desired composition of the swapchain
 * \param presentMode the desired present mode for the swapchain
 *
 * \returns SDL_TRUE on success, otherwise SDL_FALSE.
 *
 * \since This function is available since Refresh 2.0.0
 *
 * \sa Refresh_AcquireSwapchainTexture
 * \sa Refresh_UnclaimWindow
 * \sa Refresh_SupportsPresentMode
 * \sa Refresh_SupportsSwapchainComposition
 */
REFRESHAPI SDL_bool Refresh_ClaimWindow(
    Refresh_Device *device,
    SDL_Window *window,
    Refresh_SwapchainComposition swapchainComposition,
    Refresh_PresentMode presentMode);

/**
 * Unclaims a window, destroying its swapchain structure.
 *
 * \param device a GPU context
 * \param window an SDL_Window that has been claimed
 *
 * \since This function is available since Refresh 2.0.0
 *
 * \sa Refresh_ClaimWindow
 */
REFRESHAPI void Refresh_UnclaimWindow(
    Refresh_Device *device,
    SDL_Window *window);

/**
 * Changes the swapchain parameters for the given claimed window.
 *
 * This function will fail if the requested present mode or swapchain composition
 * are unsupported by the device. Check if the parameters are supported via
 * Refresh_SupportsPresentMode / Refresh_SupportsSwapchainComposition prior to
 * calling this function.
 *
 * REFRESH_PRESENTMODE_VSYNC and REFRESH_SWAPCHAINCOMPOSITION_SDR are
 * always supported.
 *
 * \param device a GPU context
 * \param window an SDL_Window that has been claimed
 * \param swapchainComposition the desired composition of the swapchain
 * \param presentMode the desired present mode for the swapchain
 * \returns SDL_TRUE if successful, SDL_FALSE on error
 *
 * \since This function is available since Refresh 2.0.0
 *
 * \sa Refresh_SupportsPresentMode
 * \sa Refresh_SupportsSwapchainComposition
 */
REFRESHAPI SDL_bool Refresh_SetSwapchainParameters(
    Refresh_Device *device,
    SDL_Window *window,
    Refresh_SwapchainComposition swapchainComposition,
    Refresh_PresentMode presentMode);

/**
 * Obtains the texture format of the swapchain for the given window.
 *
 * \param device a GPU context
 * \param window an SDL_Window that has been claimed
 *
 * \returns the texture format of the swapchain
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI Refresh_TextureFormat Refresh_GetSwapchainTextureFormat(
    Refresh_Device *device,
    SDL_Window *window);

/**
 * Acquire a texture to use in presentation.
 * When a swapchain texture is acquired on a command buffer,
 * it will automatically be submitted for presentation when the command buffer is submitted.
 * The swapchain texture should only be referenced by the command buffer used to acquire it.
 * May return NULL under certain conditions. This is not necessarily an error.
 * This texture is managed by the implementation and must not be freed by the user.
 * You MUST NOT call this function from any thread other than the one that created the window.
 *
 * \param commandBuffer a command buffer
 * \param window a window that has been claimed
 * \param pWidth a pointer filled in with the swapchain width
 * \param pHeight a pointer filled in with the swapchain height
 * \returns a swapchain texture
 *
 * \since This function is available since Refresh 2.0.0
 *
 * \sa Refresh_ClaimWindow
 * \sa Refresh_Submit
 * \sa Refresh_SubmitAndAcquireFence
 */
REFRESHAPI Refresh_Texture *Refresh_AcquireSwapchainTexture(
    Refresh_CommandBuffer *commandBuffer,
    SDL_Window *window,
    Uint32 *pWidth,
    Uint32 *pHeight);

/**
 * Submits a command buffer so its commands can be processed on the GPU.
 * It is invalid to use the command buffer after this is called.
 *
 * \param commandBuffer a command buffer
 *
 * \since This function is available since Refresh 2.0.0
 *
 * \sa Refresh_AcquireCommandBuffer
 * \sa Refresh_AcquireSwapchainTexture
 * \sa Refresh_SubmitAndAcquireFence
 */
REFRESHAPI void Refresh_Submit(
    Refresh_CommandBuffer *commandBuffer);

/**
 * Submits a command buffer so its commands can be processed on the GPU,
 * and acquires a fence associated with the command buffer.
 * You must release this fence when it is no longer needed or it will cause a leak.
 * It is invalid to use the command buffer after this is called.
 *
 * \param commandBuffer a command buffer
 * \returns a fence associated with the command buffer
 *
 * \since This function is available since Refresh 2.0.0
 *
 * \sa SDL_AcquireCommandBuffer
 * \sa Refresh_AcquireSwapchainTexture
 * \sa Refresh_Submit
 * \sa Refresh_ReleaseFence
 */
REFRESHAPI Refresh_Fence *Refresh_SubmitAndAcquireFence(
    Refresh_CommandBuffer *commandBuffer);

/**
 * Blocks the thread until the GPU is completely idle.
 *
 * \param device a GPU context
 *
 * \since This function is available since Refresh 2.0.0
 *
 * \sa Refresh_WaitForFences
 */
REFRESHAPI void Refresh_Wait(
    Refresh_Device *device);

/**
 * Blocks the thread until the given fences are signaled.
 *
 * \param device a GPU context
 * \param waitAll if 0, wait for any fence to be signaled, if 1, wait for all fences to be signaled
 * \param pFences an array of fences to wait on
 * \param fenceCount the number of fences in the pFences array
 *
 * \since This function is available since Refresh 2.0.0
 *
 * \sa Refresh_SubmitAndAcquireFence
 * \sa Refresh_Wait
 */
REFRESHAPI void Refresh_WaitForFences(
    Refresh_Device *device,
    SDL_bool waitAll,
    Refresh_Fence **pFences,
    Uint32 fenceCount);

/**
 * Checks the status of a fence.
 *
 * \param device a GPU context
 * \param fence a fence
 * \returns SDL_TRUE if the fence is signaled, SDL_FALSE if it is not
 *
 * \since This function is available since Refresh 2.0.0
 *
 * \sa Refresh_SubmitAndAcquireFence
 */
REFRESHAPI SDL_bool Refresh_QueryFence(
    Refresh_Device *device,
    Refresh_Fence *fence);

/**
 * Releases a fence obtained from Refresh_SubmitAndAcquireFence.
 *
 * \param device a GPU context
 * \param fence a fence
 *
 * \since This function is available since Refresh 2.0.0
 *
 * \sa Refresh_SubmitAndAcquireFence
 */
REFRESHAPI void Refresh_ReleaseFence(
    Refresh_Device *device,
    Refresh_Fence *fence);

/* Format Info */

/**
 * Obtains the texel block size for a texture format.
 *
 * \param textureFormat the texture format you want to know the texel size of
 * \returns the texel block size of the texture format
 *
 * \since This function is available since Refresh 2.0.0
 *
 * \sa Refresh_SetTransferData
 * \sa Refresh_UploadToTexture
 */
REFRESHAPI Uint32 Refresh_TextureFormatTexelBlockSize(
    Refresh_TextureFormat textureFormat);

/**
 * Determines whether a texture format is supported for a given type and usage.
 *
 * \param device a GPU context
 * \param format the texture format to check
 * \param type the type of texture (2D, 3D, Cube)
 * \param usage a bitmask of all usage scenarios to check
 * \returns whether the texture format is supported for this type and usage
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI SDL_bool Refresh_IsTextureFormatSupported(
    Refresh_Device *device,
    Refresh_TextureFormat format,
    Refresh_TextureType type,
    Refresh_TextureUsageFlags usage);

/**
 * Determines the "best" sample count for a texture format, i.e.
 * the highest supported sample count that is <= the desired sample count.
 *
 * \param device a GPU context
 * \param format the texture format to check
 * \param desiredSampleCount the sample count you want
 * \returns a hardware-specific version of min(preferred, possible)
 *
 * \since This function is available since Refresh 2.0.0
 */
REFRESHAPI Refresh_SampleCount Refresh_GetBestSampleCount(
    Refresh_Device *device,
    Refresh_TextureFormat format,
    Refresh_SampleCount desiredSampleCount);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* REFRESH_H */
