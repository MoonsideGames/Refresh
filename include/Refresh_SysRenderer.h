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

#ifndef REFRESH_SYSRENDERER_H
#define REFRESH_SYSRENDERER_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* This header exposes an in-progress extension to
 * push texture objects to the backend.
 *
 * In general you do NOT want to use anything in here, in fact this whole
 * extension is skipped unless you explicitly include the header in your
 * application.
 */

#define REFRESH_SYSRENDERER_VERSION_EXT 0

typedef enum Refresh_SysRendererTypeEXT
{
	REFRESH_RENDERER_TYPE_VULKAN_EXT
} Refresh_SysRendererTypeEXT;

typedef struct Refresh_TextureHandlesEXT
{
	uint32_t version;
	Refresh_SysRendererTypeEXT rendererType;

	union
	{
#if REFRESH_DRIVER_VULKAN

#if defined(__LP64__) || defined(_WIN64) || defined(__x86_64__) || defined(_M_X64) || defined(__ia64) || defined (_M_IA64) || defined(__aarch64__) || defined(__powerpc64__)
#define REFRESH_VULKAN_HANDLE_TYPE void*
#else
#define REFRESH_VULKAN_HANDLE_TYPE uint64_t
#endif

		struct
		{
			REFRESH_VULKAN_HANDLE_TYPE image;	/* VkImage */
			REFRESH_VULKAN_HANDLE_TYPE view;	/* VkImageView */
		} vulkan;
#endif /* REFRESH_DRIVER_VULKAN */
		uint8_t filler[64];
	} texture;
} Refresh_TextureHandlesEXT;

/* Export handles to be consumed by another API */
REFRESHAPI void Refresh_GetTextureHandlesEXT(
	Refresh_Device *device,
	Refresh_Texture *texture,
	Refresh_TextureHandlesEXT *handles
);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* REFRESH_SYSRENDERER_H */

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
