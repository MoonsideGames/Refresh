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

#include "Refresh_Image.h"

#include <SDL.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#ifndef __clang__
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif

#ifndef __STDC_WANT_SECURE_LIB__
#define __STDC_WANT_SECURE_LIB__ 1
#endif
#define sprintf_s SDL_snprintf

#define abs SDL_abs
#define ceilf SDL_ceilf
#define floorf SDL_floorf
#define ldexp SDL_scalbn
#define pow SDL_pow

#ifdef memcmp
#undef memcmp
#endif
#define memcmp SDL_memcmp
#ifdef memcpy
#undef memcpy
#endif
#define memcpy SDL_memcpy
#ifdef memmove
#undef memmove
#endif
#define memmove SDL_memmove
#ifdef memset
#undef memset
#endif
#define memset SDL_memset
#ifdef strcmp
#undef strcmp
#endif
#define strcmp SDL_strcmp
#ifdef strlen
#undef strlen
#endif
#define strlen SDL_strlen

/* These are per the Texture2D.FromStream spec */
#define STBI_ONLY_PNG

/* These are per the Texture2D.SaveAs* spec */
#define STBIW_ONLY_PNG

#if !SDL_VERSION_ATLEAST(2, 0, 13)
static void *
SDL_SIMDRealloc(void *mem, const size_t len)
{
	const size_t alignment = SDL_SIMDGetAlignment();
	const size_t padding = alignment - (len % alignment);
	const size_t padded = (padding != alignment) ? (len + padding) : len;
	Uint8 *retval = (Uint8*) mem;
	void *oldmem = mem;
	size_t memdiff, ptrdiff;
	Uint8 *ptr;

	if (mem) {
		void **realptr = (void **) mem;
		realptr--;
		mem = *(((void **) mem) - 1);

		/* Check the delta between the real pointer and user pointer */
		memdiff = ((size_t) oldmem) - ((size_t) mem);
	}

	ptr = (Uint8 *) SDL_realloc(mem, padded + alignment + sizeof (void *));

	if (ptr == mem) {
		return retval; /* Pointer didn't change, nothing to do */
	}
	if (ptr == NULL) {
		return NULL; /* Out of memory, bail! */
	}

	/* Store the actual malloc pointer right before our aligned pointer. */
	retval = ptr + sizeof (void *);
	retval += alignment - (((size_t) retval) % alignment);

	/* Make sure the delta is the same! */
	if (mem) {
		ptrdiff = ((size_t) retval) - ((size_t) ptr);
		if (memdiff != ptrdiff) { /* Delta has changed, copy to new offset! */
			oldmem = (void*) (((size_t) ptr) + memdiff);

			/* Even though the data past the old `len` is undefined, this is the
			 * only length value we have, and it guarantees that we copy all the
			 * previous memory anyhow.
			 */
			SDL_memmove(retval, oldmem, len);
		}
	}

	/* Actually store the malloc pointer, finally. */
	*(((void **) retval) - 1) = ptr;
	return retval;
}
#endif

#define STB_IMAGE_STATIC
#define STBI_NO_HDR
#define STBI_ASSERT SDL_assert
#define STBI_MALLOC SDL_SIMDAlloc
#define STBI_REALLOC SDL_SIMDRealloc
#define STBI_FREE SDL_SIMDFree
#define STB_IMAGE_IMPLEMENTATION
#ifdef __MINGW32__
#define STBI_NO_THREAD_LOCALS /* FIXME: Port to SDL_TLS -flibit */
#endif
#include "stb_image.h"

#define QOI_IMPLEMENTATION
#define QOI_MALLOC SDL_SIMDAlloc
#define QOI_FREE SDL_SIMDFree
#define QOI_ZEROARR SDL_zero
#include "qoi.h"

#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_SDL_MALLOC
#define MZ_ASSERT(x) SDL_assert(x)
#include "miniz.h"

/* Thanks Daniel Gibson! */
static unsigned char* dgibson_stbi_zlib_compress(
	unsigned char *data,
	int data_len,
	int *out_len,
	int quality
) {
	mz_ulong buflen = mz_compressBound(data_len);
	unsigned char *buf = SDL_malloc(buflen);

	if (	buf == NULL ||
		mz_compress2(buf, &buflen, data, data_len, quality) != 0	)
	{
		SDL_free(buf);
		return NULL;
	}
	*out_len = buflen;
	return buf;
}

#define STB_IMAGE_WRITE_STATIC
#define STBIW_ASSERT SDL_assert
#define STBIW_MALLOC SDL_malloc
#define STBIW_REALLOC SDL_realloc
#define STBIW_FREE SDL_free
#define STBIW_ZLIB_COMPRESS dgibson_stbi_zlib_compress
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#pragma GCC diagnostic pop

/* Image Read API */

uint8_t* Refresh_Image_LoadPNGFromFile(
	char const *filename,
	int32_t *w,
	int32_t *h,
	int32_t *numChannels
) {
	return stbi_load(filename, w, h, numChannels, STBI_rgb_alpha);
}

uint8_t* Refresh_Image_LoadPNGFromMemory(
	uint8_t *buffer,
	int32_t bufferLength,
	int32_t *w,
	int32_t *h,
	int32_t *numChannels
) {
	return stbi_load_from_memory(buffer, bufferLength, w, h, numChannels, STBI_rgb_alpha);
}

void Refresh_Image_FreePNG(uint8_t *mem)
{
	stbi_image_free(mem);
}

uint8_t *Refresh_Image_LoadQOIFromFile(
	char const *filename,
	int32_t *w,
	int32_t *h,
	int32_t *numChannels
) {
	qoi_desc desc;
	uint8_t *pixels = qoi_read(filename, &desc, 0);
	*w = desc.width;
	*h = desc.height;
	*numChannels = desc.channels;
	return pixels;
}

uint8_t* Refresh_Image_LoadQOIFromMemory(
	uint8_t *buffer,
	int32_t bufferLength,
	int32_t *w,
	int32_t *h,
	int32_t *numChannels
) {
	qoi_desc desc;
	uint8_t *pixels = qoi_decode(buffer, bufferLength, &desc, 0);
	*w = desc.width;
	*h = desc.height;
	*numChannels = desc.channels;
	return pixels;
}

void Refresh_Image_FreeQOI(uint8_t *mem)
{
	QOI_FREE(mem);
}

/* Image Write API */

void Refresh_Image_SavePNG(
	const char *filename,
	int32_t w,
	int32_t h,
	uint8_t bgra,
	uint8_t *data
) {
	uint32_t i;
	uint8_t *bgraData;

	if (bgra)
	{
		bgraData = SDL_malloc(w * h * 4);

		for (i = 0; i < w * h * 4; i += 4)
		{
			bgraData[i]     = data[i + 2];
			bgraData[i + 1] = data[i + 1];
			bgraData[i + 2] = data[i];
			bgraData[i + 3] = data[i + 3];
		}

		stbi_write_png(filename, w, h, 4, bgraData, w * 4);

		SDL_free(bgraData);
	}
	else
	{
		stbi_write_png(filename, w, h, 4, data, w * 4);
	}
}

void Refresh_Image_SaveQOI(
	char const *filename,
	int32_t w,
	int32_t h,
	uint8_t *data
) {
	qoi_desc desc;
	desc.width = w;
	desc.height = h;
	desc.channels = 4;
	desc.colorspace = QOI_LINEAR;
	qoi_write(filename, data, &desc);
}

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
