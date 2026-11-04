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
// xr_hud.c -- the 2D layer, made usable inside a headset
//
// Two problems, both of them about a HUD that was laid out for a monitor
// being looked at through optics.
//
// 1. THE CROSSHAIR LIES. The aim is the head except during a trigger pull,
//    and cgame draws the crosshair at screen centre regardless. Owner:
//    "we can remove the crosshair in the center, it's not accurate."
//
// 2. THE CORNERS ARE NOT THERE. A headset shows a circular-ish region of
//    the frame sharply and falls off hard towards the edges, so health in
//    the bottom-left and ammo in the bottom-right sit exactly where a
//    person cannot read them. Shrinking the whole 2D layer towards the
//    centre puts it back in view.
//
// The second is done by intercepting the cgame syscall rather than by
// touching cgame, because cgame is a QVM the server forces on us. Every 2D
// element in Tremulous reaches the engine through CG_R_DRAWSTRETCHPIC --
// text included, since CG_Text_Paint draws glyph quads the same way -- so
// one transform there moves all of it, and the 3D view is untouched because
// that goes through RenderScene.
//
// cgame has already run CG_AdjustFrom640 by this point, so the coordinates
// arriving here are REAL SCREEN PIXELS, not 640x480 virtual ones. The
// centre is therefore the renderer's own width and height, halved.

#include "xr_hud.h"
#include "xr_api.h"
#include "../client/client.h"

static cvar_t   *vr_hudScale;
static cvar_t   *vr_hideCrosshair;

/*
==================
XR_HudInit
==================
*/
void XR_HudInit( void )
{
	// 0.5 is the owner's ask -- "half the screen space, centered". 1.0 is
	// off, and anything between is a straight interpolation, so this is
	// tunable on the device without a rebuild like every other vr_* value.
	vr_hudScale      = Cvar_Get( "vr_hudScale",      "0.5", CVAR_ARCHIVE );
	vr_hideCrosshair = Cvar_Get( "vr_hideCrosshair", "1",   CVAR_ARCHIVE );
}

/*
==================
XR_HideCrosshair

cg_drawCrosshair is cgame's own cvar, read out of the CLIENT, so setting it
here works even against a pure server forcing its own cgame. That is worth
preferring over filtering the draw call: cgame then also skips the crosshair
shader, the hit marker timing and the per-weapon crosshair sizes, instead of
us trying to recognise them in a stream of quads.

Done once, when VR comes up, rather than every frame -- it is the player's
cvar and they are allowed to turn it back on.
==================
*/
void XR_HideCrosshair( void )
{
	if ( !vr_hideCrosshair->integer ) {
		return;
	}

	Cvar_Set( "cg_drawCrosshair", "0" );
	XR_Log( "hud: cg_drawCrosshair 0 -- the crosshair is at screen centre, "
			"which is the head, and the shot follows the hand. Set "
			"vr_hideCrosshair 0 to keep it." );
}

/*
==================
XR_HudScale2D

Shrink one 2D quad towards the centre of the screen.

Returns with the rectangle rewritten in place. Harmless when the scale is 1.
==================
*/
void XR_HudScale2D( float *x, float *y, float *w, float *h )
{
	float   s = vr_hudScale->value;
	float   cx, cy;

	if ( !XR_IsEnabled() || s <= 0.0f || s > 1.0f || s == 1.0f ) {
		return;
	}

	// Menus are flat panels the runtime presents at MODE_VR 0, where the
	// whole frame is readable and shrinking it would only make it small.
	if ( !XR_InGame() ) {
		return;
	}

	cx = cls.glconfig.vidWidth * 0.5f;
	cy = cls.glconfig.vidHeight * 0.5f;

	// LEAVE FULL-SCREEN QUADS ALONE. Loading screens, fades to black and
	// the console background are drawn the same way as a health bar, and
	// shrinking one of those leaves the un-faded original showing round the
	// edge of it -- which looks far worse than an off-centre HUD.
	if ( *w >= cls.glconfig.vidWidth * 0.99f
		 && *h >= cls.glconfig.vidHeight * 0.99f ) {
		return;
	}

	*x = cx + ( *x - cx ) * s;
	*y = cy + ( *y - cy ) * s;
	*w *= s;
	*h *= s;
}
