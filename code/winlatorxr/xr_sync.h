/*
===========================================================================
Copyright (C) 2026 Tremulous WXR contributors

This file is part of Tremulous.

Tremulous is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Tremulous is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
Public License for more details.

You should have received a copy of the GNU General Public License
along with Tremulous; if not, see <https://www.gnu.org/licenses/>
===========================================================================
*/
// xr_sync.h -- the red sync pixel
//
// The highest-value thing in the whole port, and the one piece that a move
// to a Vulkan renderer would force us to revisit. It is deliberately behind
// this two-function interface so that revisit stays contained.

#ifndef __XR_SYNC_H
#define __XR_SYNC_H

#include "../qcommon/q_shared.h"

void    XR_SyncInit( void );

// Renderer and resolution, logged once at startup, unconditionally.
void    XR_SyncLogEnvironment( void );

// Call LAST, after the world and after the HUD, immediately before
// re.EndFrame. Anything drawn afterwards overwrites the corner and the
// runtime reads that instead.
void    XR_DrawSyncPixel( void );

#endif // __XR_SYNC_H
