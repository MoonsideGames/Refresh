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

/* Drivers */

static const REFRESH_Driver *drivers[] = {
    &VulkanDriver,
    NULL
};

/* Version API */

uint32_t REFRESH_LinkedVersion(void)
{
	return REFRESH_COMPILED_VERSION;
}

/* Driver Functions */

static int32_t selectedDriver = -1;

REFRESH_Device* REFRESH_CreateDevice(
    FNA3D_Device *fnaDevice
) {
    if (selectedDriver < 0)
    {
        return NULL;
    }

    return drivers[selectedDriver]->CreateDevice(fnaDevice);
}

void REFRESH_DestroyDevice(REFRESH_Device *device)
{
    if (device == NULL)
    {
        return;
    }

    device->DestroyDevice(device);
}

void REFRESH_Clear(
	REFRESH_Device *device,
	REFRESH_ClearOptions options,
	REFRESH_Vec4 **colors,
    uint32_t colorCount,
	float depth,
	int32_t stencil
) {
    if (device == NULL)
    {
        return;
    }
    device->Clear(device->driverData, options, colors, colorCount, depth, stencil);
}
