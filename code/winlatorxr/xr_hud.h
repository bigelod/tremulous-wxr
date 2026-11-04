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
#ifndef __XR_HUD_H
#define __XR_HUD_H

#include "../qcommon/q_shared.h"

void        XR_HudInit( void );

// Turns off cgame's centre crosshair, once, when VR comes up.
void        XR_HideCrosshair( void );

// Shrinks one 2D quad towards the centre of the screen, so a HUD laid out
// for a monitor lands where a headset can actually resolve it. Full-screen
// quads are left alone. No-op with VR off, in menus, or at vr_hudScale 1.
void        XR_HudScale2D( float *x, float *y, float *w, float *h );

#endif
