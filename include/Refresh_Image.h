/* Refresh - XNA-inspired 3D Graphics Library with modern capabilities
 *
 * Copyright (c) 2020 Ethan Lee and Evan Hemsley
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
 * Ethan "flibitijibibo" Lee <flibitijibibo@flibitijibibo.com>
 * Evan "cosmonaut" Hemsley <evan@moonside.games>
 *
 * This source file is heavily borrowed from FNA3D_Image.h and was originally
 * written by Ethan Lee.
 */

#ifndef REFRESH_IMAGE_H
#define REFRESH_IMAGE_H

#ifdef _WIN32
#define REFRESHAPI __declspec(dllexport)
#define REFRESHCALL __cdecl
#else
#define REFRESHAPI
#define REFRESHCALL
#endif

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Decodes PNG data into raw RGBA8 texture data.
 *
 * w:		    Filled with the width of the image.
 * h:		    Filled with the height of the image.
 * numChannels: Filled with the number of channels in the image.
 *
 * Returns a block of memory suitable for use with Refresh_SetTextureData2D.
 * Be sure to free the memory with Refresh_Image_Free after use!
 */
REFRESHAPI uint8_t* Refresh_Image_Load(
	char const *filename,
	int32_t *w,
	int32_t *h,
	int32_t *numChannels
);

/* Frees memory returned by Refresh_Image_Load. (Do NOT free the memory yourself!)
 *
 * mem: A pointer previously returned by Refresh_Image_Load.
 */
REFRESHAPI void Refresh_Image_Free(uint8_t *mem);

/* Image Write API */

/* Encodes 32-bit color data into PNG data.
 *
 * filename:    The filename that the image will be written to.
 * w:	        The width of the PNG data.
 * h:	        The height of the PNG data.
 * bgra:		Whether the data is in BGRA8 format. Otherwise will assume RBGA8.
 * data:	    The raw color data.
 */
REFRESHAPI void Refresh_Image_SavePNG(
	char const *filename,
	int32_t w,
	int32_t h,
	uint8_t bgra,
	uint8_t *data
);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* REFRESH_IMAGE_H */

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
