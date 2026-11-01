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
// xr_view.c -- head pose to engine camera (stage 6)
//
// THE FRAME CONVERSION, because getting this wrong is a day lost:
//
// XrApi delivers OpenXR convention -- right-handed, +Y up, -Z forward.
// Quake 3 is ALSO right-handed, with +X forward, +Y left, +Z up
// (AnglesToAxis builds axis[0]=forward, axis[1]=-right, axis[2]=up).
//
// So the conversion is a pure axis relabel:
//
//     Q3 forward =  -Z_xr        (x, y, z)_xr  ->  (-z, -x, y)_q3
//     Q3 left    =  -X_xr
//     Q3 up      =  +Y_xr
//
// That matrix has determinant +1 and satisfies forward x left = up exactly,
// so there is NO handedness flip here.
//
// docs/general_wxr/XRAPI_V0_4_DESC.md section 6 gives the recipe as "rotate
// the canonical axes, then flip z for a left-handed engine". The flip is
// correct for a left-handed engine such as Unity and WRONG for this one --
// applying it would mirror the world, which presents as a world that looks
// right until you read any text in it. Rotating the axes is the part that
// generalises; the flip is per-engine and has to be derived.
//
// Other deliberate choices:
//
//  * Recentre is YAW ONLY, not the full inverse pose the generic snippet
//    suggests. A full inverse taken while the player happens to be looking
//    down makes "forward" mean "down" until the next recentre. Position is
//    fully recentred; orientation only in yaw.
//
//  * Head yaw and pitch reach the camera through the USERCMD, not through
//    the render hook. The server derives the player state from cmd->angles,
//    cgame builds viewaxis from the player state, and so the view follows
//    with no second application. The render hook therefore adds only roll
//    and the lean offset.

#include "xr_view.h"
#include "xr_api.h"
#include "../qcommon/qcommon.h"

static cvar_t   *vr_enable;
static cvar_t   *vr_verbose;
static cvar_t   *vr_worldScale;
static cvar_t   *vr_posTracking;
static cvar_t   *vr_rollEnable;
static cvar_t   *vr_pitchEnable;

static float    xr_centreYaw;           // degrees, Q3 frame
static vec3_t   xr_centrePos;           // metres, XR frame
static qboolean xr_centred;

// Logged on change rather than per frame; a log the owner has to scroll is a
// log that gets skimmed.
static vec3_t   xr_lastLoggedAngles;
static int      xr_lastViewLog;

#define XR_VIEWLOG_INTERVAL_MS  5000

/*
==================
XR_QRotate

v' = v + 2w(u x v) + 2u x (u x v)

A rotation only when |q| == 1, which is why xr_api.c normalises on ingest
unconditionally.
==================
*/
static void XR_QRotate( const vec4_t q, const vec3_t v, vec3_t out )
{
	vec3_t  u, uv, uuv;
	float   w = q[3];

	u[0] = q[0];
	u[1] = q[1];
	u[2] = q[2];

	CrossProduct( u, v, uv );
	CrossProduct( u, uv, uuv );

	out[0] = v[0] + 2.0f * ( w * uv[0] + uuv[0] );
	out[1] = v[1] + 2.0f * ( w * uv[1] + uuv[1] );
	out[2] = v[2] + 2.0f * ( w * uv[2] + uuv[2] );
}

/*
==================
XR_HeadBasis

The HMD orientation as an engine-frame basis matrix, before recentring.

Rotate the canonical axes by the quaternion and relabel. Never convert or
mirror the quaternion itself -- doing that by hand reliably produces a basis
that is subtly not orthonormal, and then the symptoms get debugged instead of
the cause.
==================
*/
static qboolean XR_HeadBasis( vec3_t axis[3] )
{
	const xr_state_t    *s = XR_GetState();
	const vec3_t        xrForward = { 0.0f, 0.0f, -1.0f };
	const vec3_t        xrUp      = { 0.0f, 1.0f,  0.0f };
	vec3_t              f, u;

	if ( !s->valid ) {
		return qfalse;
	}

	XR_QRotate( s->hmdQuat, xrForward, f );
	XR_QRotate( s->hmdQuat, xrUp, u );

	// (x, y, z)_xr -> (-z, -x, y)_q3
	axis[0][0] = -f[2];
	axis[0][1] = -f[0];
	axis[0][2] =  f[1];

	axis[2][0] = -u[2];
	axis[2][1] = -u[0];
	axis[2][2] =  u[1];

	// left = up x forward, which keeps the basis right-handed.
	CrossProduct( axis[2], axis[0], axis[1] );

	return qtrue;
}

/*
==================
XR_HeadAngles
==================
*/
qboolean XR_HeadAngles( vec3_t angles )
{
	vec3_t  axis[3];
	vec3_t  raw;

	if ( !XR_HeadBasis( axis ) ) {
		return qfalse;
	}

	AxisToAngles( axis, raw );

	// AxisToAngles returns 0..360 and then negates pitch, so a small upward
	// look comes back as -350 rather than +10. Normalise before anyone does
	// arithmetic on it.
	angles[PITCH] = AngleNormalize180( raw[PITCH] );
	angles[YAW]   = AngleNormalize180( raw[YAW] - xr_centreYaw );
	angles[ROLL]  = AngleNormalize180( raw[ROLL] );

	return qtrue;
}

/*
==================
XR_HeadOffset

The head's displacement from the recentre origin, in engine units, expressed
in the recentred room frame (not yet rotated by the body's yaw).
==================
*/
static qboolean XR_HeadOffset( vec3_t offset )
{
	const xr_state_t    *s = XR_GetState();
	vec3_t              d;
	float               scale;

	if ( !s->valid || !xr_centred ) {
		return qfalse;
	}

	d[0] = s->hmdPos[0] - xr_centrePos[0];
	d[1] = s->hmdPos[1] - xr_centrePos[1];
	d[2] = s->hmdPos[2] - xr_centrePos[2];

	scale = vr_worldScale->value;

	// (x, y, z)_xr -> (-z, -x, y)_q3, metres to engine units.
	offset[0] = -d[2] * scale;
	offset[1] = -d[0] * scale;
	offset[2] =  d[1] * scale;

	return qtrue;
}

/*
==================
XR_Recentre
==================
*/
void XR_Recentre( void )
{
	const xr_state_t    *s = XR_GetState();
	vec3_t              axis[3];
	vec3_t              raw;

	if ( !s->valid ) {
		Com_Printf( "XR: no head pose yet -- nothing to recentre\n" );
		XR_Log( "recentre: refused, no pose" );
		return;
	}

	if ( XR_HeadBasis( axis ) ) {
		AxisToAngles( axis, raw );
		xr_centreYaw = raw[YAW];
	}

	VectorCopy( s->hmdPos, xr_centrePos );
	xr_centred = qtrue;

	XR_Log( "recentre: yaw=%.1f pos=[%.3f %.3f %.3f]",
			xr_centreYaw, xr_centrePos[0], xr_centrePos[1],
			xr_centrePos[2] );
}

static void XR_Recentre_f( void )
{
	XR_Recentre();
}

/*
==================
XR_AdjustUsercmdAngles

Hook 2. Additive, so the engine's own mouse and stick accumulation into
cl.viewangles is left completely alone -- we never write back to it, which is
what stops the head yaw being integrated again on the following frame.
==================
*/
void XR_AdjustUsercmdAngles( vec3_t viewangles )
{
	vec3_t  head;

	if ( !XR_IsEnabled() ) {
		return;
	}

	if ( !XR_HeadAngles( head ) ) {
		return;     // no pose: leave the flat game's aim untouched
	}

	viewangles[YAW] = AngleNormalize180( viewangles[YAW] + head[YAW] );

	if ( vr_pitchEnable->integer ) {
		// Head pitch replaces mouse pitch rather than adding to it. Adding
		// would let the player aim further up than they can look, which is
		// both disorienting and an unfair advantage.
		viewangles[PITCH] = AngleNormalize180( head[PITCH] );
	}

	// ROLL is deliberately not touched here. The server does not need it and
	// cgame owns it for fall-bob, weapon kick and the death cam.
}

/*
==================
XR_AdjustRefdef

Hook 1. Composes onto what cgame built rather than replacing it, which is
what keeps wallwalking, weapon kick, fall-bob and the death cam working --
cgame has already encoded all of those in viewaxis.
==================
*/
void XR_AdjustRefdef( refdef_t *rd, float bodyYaw )
{
	vec3_t  head;
	vec3_t  offset;
	vec3_t  rotated;
	vec3_t  newAxis[3];

	if ( !XR_IsEnabled() ) {
		return;
	}

	if ( !XR_HeadAngles( head ) ) {
		return;
	}

	// ---- roll, composed about the view's own forward axis ----------------
	//
	// Rotating about viewaxis[0] rather than about world Z is the whole point
	// of composing: on a ceiling, cgame's forward axis is already the
	// wallwalked one, so the roll stays relative to the player's own head
	// rather than to the map.

	if ( vr_rollEnable->integer && head[ROLL] != 0.0f ) {
		RotatePointAroundVector( newAxis[1], rd->viewaxis[0],
								 rd->viewaxis[1], head[ROLL] );
		RotatePointAroundVector( newAxis[2], rd->viewaxis[0],
								 rd->viewaxis[2], head[ROLL] );

		VectorCopy( rd->viewaxis[0], newAxis[0] );
		AxisCopy( newAxis, rd->viewaxis );
	}

	// ---- positional offset ----------------------------------------------
	//
	// The lean is a displacement in the room, so it is rotated by the BODY's
	// yaw. The view basis already contains the head's yaw (it arrived via the
	// usercmd), so rotating by the view would apply that yaw a second time
	// and the world would swing as the head turned.
	//
	// Known limitation, and a specific question for CHECKPOINT 6: this uses
	// world Z as the rotation axis, which is right on the ground and only
	// approximate once a Granger is on a wall. Doing it properly needs the
	// player state's ground normal, which is stage 10's territory.

	if ( vr_posTracking->integer && XR_HeadOffset( offset ) ) {
		const vec3_t worldUp = { 0.0f, 0.0f, 1.0f };

		RotatePointAroundVector( rotated, worldUp, offset, bodyYaw );
		VectorAdd( rd->vieworg, rotated, rd->vieworg );
	} else {
		VectorClear( rotated );
	}

	// ---- diagnostics -----------------------------------------------------
	//
	// The composed basis and the head offset, which the plan names explicitly
	// as things the log must already carry before a build ships.

	if ( vr_verbose->integer ) {
		int     now = Sys_Milliseconds();
		float   moved = fabs( head[PITCH] - xr_lastLoggedAngles[PITCH] )
					  + fabs( head[YAW]   - xr_lastLoggedAngles[YAW] )
					  + fabs( head[ROLL]  - xr_lastLoggedAngles[ROLL] );

		if ( now - xr_lastViewLog >= XR_VIEWLOG_INTERVAL_MS && moved > 1.0f ) {
			xr_lastViewLog = now;
			VectorCopy( head, xr_lastLoggedAngles );

			XR_Log( "view: head p=%.1f y=%.1f r=%.1f bodyYaw=%.1f",
					head[PITCH], head[YAW], head[ROLL], bodyYaw );
			XR_Log( "view: offset=[%.1f %.1f %.1f] units  vieworg=[%.1f %.1f %.1f]",
					rotated[0], rotated[1], rotated[2],
					rd->vieworg[0], rd->vieworg[1], rd->vieworg[2] );
			XR_Log( "view: axis fwd=[%.3f %.3f %.3f] left=[%.3f %.3f %.3f] up=[%.3f %.3f %.3f]",
					rd->viewaxis[0][0], rd->viewaxis[0][1], rd->viewaxis[0][2],
					rd->viewaxis[1][0], rd->viewaxis[1][1], rd->viewaxis[1][2],
					rd->viewaxis[2][0], rd->viewaxis[2][1], rd->viewaxis[2][2] );
		}
	}
}

/*
==================
XR_ViewInit
==================
*/
void XR_ViewInit( void )
{
	vr_enable  = Cvar_Get( "vr_enable",  "0", CVAR_ARCHIVE | CVAR_LATCH );
	vr_verbose = Cvar_Get( "vr_verbose", "0", CVAR_ARCHIVE );

	// Engine units per metre. DERIVED, not guessed: human_base.cfg puts the
	// eye at viewheight 24 with mins[2] -24, so 48 units above the feet. At a
	// 1.6 m human eye height that is 30 units/metre.
	//
	// This is a placeholder for ONE class. D9 in PROJECT_PLAN.md is explicit
	// that twelve classes have twelve bodies and a single constant is wrong
	// for eleven of them -- a Dretch and a Tyrant do not share a scale. The
	// per-class derivation is stage 10; this cvar exists so stage 6 can be
	// tested as a human without pretending the problem is solved.
	vr_worldScale  = Cvar_Get( "vr_worldScale",  "30", CVAR_ARCHIVE );

	// Orientation first, position second: FIRST_STEPS stage 6 is explicit
	// that getting looking-around right with fewer variables comes first. Run
	// CHECKPOINT 6 once with vr_posTracking 0, then again with it on.
	vr_posTracking = Cvar_Get( "vr_posTracking", "1", CVAR_ARCHIVE );

	vr_rollEnable  = Cvar_Get( "vr_rollEnable",  "1", CVAR_ARCHIVE );
	vr_pitchEnable = Cvar_Get( "vr_pitchEnable", "1", CVAR_ARCHIVE );

	Cmd_AddCommand( "vr_recentre", XR_Recentre_f );
	Cmd_AddCommand( "vr_recenter", XR_Recentre_f );   // both spellings

	if ( vr_enable->integer ) {
		XR_Log( "view: worldScale=%.1f units/m posTracking=%d roll=%d pitch=%d",
				vr_worldScale->value, vr_posTracking->integer,
				vr_rollEnable->integer, vr_pitchEnable->integer );
	}
}
