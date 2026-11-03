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
// xr_sync.c -- the red sync pixel (stage 5)
//
// WinlatorXR reads a shade of red from pixel (0,0) of the captured frame to
// learn which frame it is looking at. Without it the runtime cannot match an
// image to the pose that image was rendered for, and it reprojects against
// the wrong head pose: the world swims when you turn, by a varying amount.
// That is exactly the stimulus that causes motion sickness, and no amount of
// frame-rate work fixes it, because the defect is correlation, not latency.
//
// A port at 30 fps with correct sync feels better than one at 72 without it.
//
// WHERE THIS LIVES, AND WHY -- this is an architectural question, not an
// afterthought (BUG_FIXES part 1 item 6):
//
//   * NOT in a renderer DLL. USE_RENDERER_DLOPEN is ON, so
//     renderer_opengl1.dll and renderer_opengl2.dll are loaded at runtime and
//     cannot reach XR_GetState() in the client executable. sdl_glimp.c is in
//     renderer_common, so the SDL layer is inside that DLL too and is not
//     the escape hatch it first appears to be.
//
//   * In the CLIENT, immediately before re.EndFrame in SCR_UpdateScreen.
//     The client owns that call, both renderers converge there, and the
//     world and the HUD have both been drawn. Using re.DrawStretchPic means
//     one implementation covers gl1 and gl2 rather than two.
//
// THE CORNER -- the single most misattributed failure in this stack. The 2D
// pass sets qglOrtho(0, vidWidth, vidHeight, 0, 0, 1), so coordinates are
// REAL PIXELS with y = 0 at the TOP. Drawing at (0,0)-(10,10) is therefore
// the top-left, which is what the runtime samples.
//
// Do NOT run these through SCR_AdjustFrom640: that converts from the virtual
// 640x480 space, and the result would be offset by the widescreen
// correction. The prior attempt used the same top-left ortho and then drew
// from vidHeight-10 to vidHeight -- the BOTTOM -- and the symptom was head
// tracking that swam no matter how good the pose maths was.

#include "xr_sync.h"
#include "xr_api.h"
#include "../client/client.h"

static cvar_t   *vr_enable;
static cvar_t   *vr_verbose;
static cvar_t   *vr_syncPixel;
static cvar_t   *vr_mode3D;

static int      xr_lastSyncLogged = -1;
static int      xr_syncChanges;

#define XR_SYNC_SIZE    10

/*
==================
XR_SyncSquare

One square, at a given left edge. Colour is RGB(frameId, 0, eyeFlag):

  red   -- the frame id, in every mode
  green -- MUST be 0. The runtime's remapping is gated on g == 0, so a
           non-zero green silently disables the calibration.
  blue  -- the AER eye flag; 0 in every other mode.
==================
*/
static void XR_SyncSquare( float x, float sync, float eyeFlag )
{
	vec4_t  colour;

	// Echo HMD_SYNC exactly as received. Do not rescale, do not gamma
	// correct, do not invent ids. XrFramesync collects the distinct red
	// values it observes, sorts them and calibrates itself to whatever
	// colour space this renderer actually produces -- the correction belongs
	// on the runtime side and is already there. Pre-correcting here fights
	// a system that is trying to learn from us.
	colour[0] = sync / 255.0f;
	colour[1] = 0.0f;
	colour[2] = eyeFlag / 255.0f;
	colour[3] = 1.0f;

	re.SetColor( colour );
	re.DrawStretchPic( x, 0.0f, XR_SYNC_SIZE, XR_SYNC_SIZE,
					   0, 0, 0, 0, cls.whiteShader );
	re.SetColor( NULL );
}

/*
==================
XR_DrawSyncPixel
==================
*/
void XR_DrawSyncPixel( void )
{
	const xr_state_t    *s;
	float               sync;
	int                 syncInt;

	if ( !XR_IsEnabled() ) {
		return;
	}

	// Fires once, the first frame the renderer has reported itself. Before
	// the early-out below, so the renderer and resolution reach the log even
	// if no pose ever arrives -- which is exactly the case where someone
	// most needs to know what the container actually gave us.
	XR_SyncLogEnvironment( );

	if ( !vr_syncPixel->integer ) {
		return;
	}

	s = XR_GetState();
	if ( !s->valid ) {
		// No pose yet. Drawing a constant would read to the runtime as a
		// frozen game -- it derives our frame rate by counting changes in
		// this value -- so draw nothing at all until the stream starts.
		return;
	}

	sync = s->sync;
	if ( sync < 0.0f ) {
		sync = 0.0f;
	} else if ( sync > 255.0f ) {
		sync = 255.0f;
	}

	XR_SyncSquare( 0.0f, sync, 0.0f );

	// Side-by-side: the runtime may sample either half, so carry the id in
	// both. Mono and the flat modes need only the one square.
	if ( vr_mode3D->integer == 1 ) {
		XR_SyncSquare( (float)( cls.glconfig.vidWidth / 2 ), sync, 0.0f );
	}

	// Log transitions, not state. What matters is that the value is MOVING:
	// a sync that never changes reads as a frozen game on the runtime side,
	// and that is a failure the log should be able to prove or disprove
	// without another trip to the headset.
	syncInt = (int)( sync + 0.5f );
	if ( syncInt != xr_lastSyncLogged ) {
		xr_lastSyncLogged = syncInt;
		xr_syncChanges++;

		if ( vr_verbose->integer && ( xr_syncChanges % 200 ) == 1 ) {
			XR_Log( "sync: value=%d changes=%d vid=%dx%d mode3D=%d",
					syncInt, xr_syncChanges,
					cls.glconfig.vidWidth, cls.glconfig.vidHeight,
					vr_mode3D->integer );
		}
	}
}

/*
==================
XR_SyncLogEnvironment

The renderer and the resolution, logged once, unconditionally. Both are named
in PROJECT_PLAN.md's logging contract and both answer questions that would
otherwise cost a whole manual round trip: which renderer the container
actually gave us, and what the captured frame's dimensions are -- the second
being the frame the sync pixel sits in the corner of.

Lives here because xr_sync.c is the one VR file that already includes
client.h, so it is the one that can see cls.glconfig.
==================
*/
void XR_SyncLogEnvironment( void )
{
	static qboolean logged;

	if ( logged ) {
		return;
	}

	// Deferred until the renderer is actually up. XR_Init runs at the end of
	// CL_Init, but the window and GL context are created later still, so
	// calling this from there logged an empty vendor string and "0x0" -- a
	// log line that looks like a renderer failure when nothing is wrong.
	// Call it from the frame path instead and let it fire the first time
	// glconfig has anything in it.
	if ( cls.glconfig.vidWidth == 0 ) {
		return;
	}

	logged = qtrue;

	XR_Log( "renderer: %s", cls.glconfig.renderer_string[0]
			? cls.glconfig.renderer_string : "(none reported)" );
	XR_Log( "renderer: vendor=%s version=%s",
			cls.glconfig.vendor_string, cls.glconfig.version_string );
	XR_Log( "renderer: module=%s",
			Cvar_VariableString( "cl_renderer" ) );
	// Which VM mode actually ran. A config file that did not take effect is
	// otherwise indistinguishable from one that did, and that ambiguity
	// already cost one device round trip.
	XR_Log( "vm: game=%s cgame=%s ui=%s  (0=native dll 1=bytecode 2=jit)",
			Cvar_VariableString( "vm_game" ),
			Cvar_VariableString( "vm_cgame" ),
			Cvar_VariableString( "vm_ui" ) );

	XR_Log( "video: %dx%d fullscreen=%d refresh=%d",
			cls.glconfig.vidWidth, cls.glconfig.vidHeight,
			cls.glconfig.isFullscreen ? 1 : 0,
			cls.glconfig.displayFrequency );
}

/*
==================
XR_SyncInit
==================
*/
void XR_SyncInit( void )
{
	vr_enable    = Cvar_Get( "vr_enable",    "0", CVAR_ARCHIVE | CVAR_LATCH );
	vr_verbose   = Cvar_Get( "vr_verbose",   "0", CVAR_ARCHIVE );
	vr_mode3D    = Cvar_Get( "vr_mode3D",    "0", CVAR_ARCHIVE );

	// An off switch, so "is the camera wrong or is sync wrong?" can be
	// answered on the device by toggling one cvar instead of shipping two
	// builds. Swimmy with this on and swimmy with it off are different
	// diagnoses.
	vr_syncPixel = Cvar_Get( "vr_syncPixel", "1", CVAR_ARCHIVE );
}
