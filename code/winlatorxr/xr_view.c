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
#include "xr_weapon.h"
#include "xr_api.h"
#include "xr_menu.h"
#include "../qcommon/qcommon.h"

static cvar_t   *vr_enable;
static cvar_t   *vr_verbose;
static cvar_t   *vr_worldScale;         // human reference, units per metre
static cvar_t   *vr_classScale;         // derive the other eleven from it
static cvar_t   *vr_fovScale;           // comfort: <= 1, never wider
static cvar_t   *vr_posTracking;
static cvar_t   *vr_rollEnable;
static cvar_t   *vr_pitchEnable;

/*
==================
Per-class body scale

D9 in PROJECT_PLAN.md: twelve classes, twelve bodies, and one units-per-metre
constant is wrong for eleven of them. "Preserve the apparent body size" is
the owner's decision (16 Nov 2026): the player's real eye height always maps
to the active class's eye height above its own feet, so a Tyrant's world is
smaller and a Dretch's is larger. The alternative -- one fixed world scale,
feeling 2.8 m tall as a Tyrant and knee-high as a Dretch -- was rejected: the
difference between the classes is the point, and comfort options can be
layered on top later rather than averaged away now.

eyeAboveFeet is `viewheight - mins[2]` from
assets/configs/classes/*.cfg, which is the class's standing eye height above
the bottom of its bounding box in engine units:

    human_base   viewheight 24, mins z -24  -> 48
    level0       viewheight  0, mins z -15  -> 15   (Dretch)
    level4       viewheight 64, mins z -22  -> 86   (Tyrant)

The human row is the anchor: 48 units at a 1.6 m eye height is where the
existing 30 u/m default came from, so `vr_worldScale 30` keeps meaning what
it always meant and the other classes are derived from it.

Why a table here rather than reading the configs: mapping a class INDEX to a
config file is game-module knowledge, and a pure server replaces the game
module. A mod that renumbers classes reads the wrong row; that changes how
big the world feels, not whether it renders, and the out-of-range fallback is
the human row. Spelled numerically with the PCL_* names in comments, exactly
as xr_weapon spells STAT_TEAM and TEAM_HUMANS.
==================
*/
static const struct {
	const char  *name;
	int         eyeAboveFeet;
} xr_classes[] = {
	{ "spectator",      48 },   // PCL_NONE -- no body to scale; keep human
	{ "granger",        20 },   // PCL_ALIEN_BUILDER0
	{ "granger upg",    20 },   // PCL_ALIEN_BUILDER0_UPG
	{ "dretch",         15 },   // PCL_ALIEN_LEVEL0
	{ "basilisk",       18 },   // PCL_ALIEN_LEVEL1
	{ "basilisk upg",   21 },   // PCL_ALIEN_LEVEL1_UPG
	{ "marauder",       30 },   // PCL_ALIEN_LEVEL2
	{ "marauder upg",   34 },   // PCL_ALIEN_LEVEL2_UPG
	{ "dragoon",        47 },   // PCL_ALIEN_LEVEL3
	{ "dragoon upg",    56 },   // PCL_ALIEN_LEVEL3_UPG
	{ "tyrant",         86 },   // PCL_ALIEN_LEVEL4
	{ "human",          48 },   // PCL_HUMAN
	{ "battlesuit",     67 }    // PCL_HUMAN_BSUIT
};

#define XR_NUM_CLASSES ((int)( sizeof( xr_classes ) / sizeof( xr_classes[0] ) ))
#define XR_HUMAN_EYE   48          // the anchor the 30 u/m default came from

// cgame computes fov_y as classFov * 0.75. The human classes are 90, so every
// class is held to that vertical angle; the horizontal follows the frame
// aspect. See the clamp in XR_AdjustRefdef.
#define XR_HUMAN_FOV     90.0f
#define XR_HUMAN_FOV_Y   ( XR_HUMAN_FOV * 0.75f )

static int      xr_classIndex = -1;
static int      xr_classEye = XR_HUMAN_EYE;

/*
==================
XR_WorldScale

Engine units per metre for the class being played. With vr_classScale 0 this
is the old single constant, which is still the right thing for desktop tests
that should not move when the class changes.
==================
*/
static float XR_WorldScale( void )
{
	if ( !vr_classScale->integer ) {
		return vr_worldScale->value;
	}

	return vr_worldScale->value * ( (float)xr_classEye / (float)XR_HUMAN_EYE );
}

static float    xr_centreYaw;           // degrees, Q3 frame
static vec3_t   xr_centrePos;           // metres, XR frame
static qboolean xr_centred;

// WHAT THE LAST USERCMD ACTUALLY CARRIED. The render hook corrects against
// these, not against a fresh sample of the pose -- see XR_AdjustUsercmdAngles.
static qboolean xr_sentWasGun;
static vec3_t   xr_sentGun;         // recentred room angles, no body yaw
static vec3_t   xr_sentHead;

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
XR_PoseBasis

Any tracked orientation as an engine-frame basis matrix, before recentring.
Shared by the head and both hands -- one conversion, used three times.

Rotate the canonical axes by the quaternion and relabel. Never convert or
mirror the quaternion itself -- doing that by hand reliably produces a basis
that is subtly not orthonormal, and then the symptoms get debugged instead of
the cause.
==================
*/
static void XR_PoseBasis( const vec4_t q, vec3_t axis[3] )
{
	const vec3_t        xrForward = { 0.0f, 0.0f, -1.0f };
	const vec3_t        xrUp      = { 0.0f, 1.0f,  0.0f };
	vec3_t              f, u;

	XR_QRotate( q, xrForward, f );
	XR_QRotate( q, xrUp, u );

	// (x, y, z)_xr -> (-z, -x, y)_q3
	axis[0][0] = -f[2];
	axis[0][1] = -f[0];
	axis[0][2] =  f[1];

	axis[2][0] = -u[2];
	axis[2][1] = -u[0];
	axis[2][2] =  u[1];

	// left = up x forward, which keeps the basis right-handed.
	CrossProduct( axis[2], axis[0], axis[1] );
}

/*
==================
XR_HeadBasis
==================
*/
static qboolean XR_HeadBasis( vec3_t axis[3] )
{
	const xr_state_t    *s = XR_GetState();

	if ( !s->valid ) {
		return qfalse;
	}

	XR_PoseBasis( s->hmdQuat, axis );
	return qtrue;
}

/*
==================
XR_HandBasis / XR_HandAngles / XR_HandOffset

The same conversion as the head, reached through the same two functions.
Writing a second one for the hand is the obvious shortcut and the reliable
way to end up with two frames that disagree by a sign nobody can find.
==================
*/
qboolean XR_HandBasis( int hand, vec3_t axis[3] )
{
	const xr_state_t    *s = XR_GetState();

	if ( !s->valid || hand < 0 || hand > 1 ) {
		return qfalse;
	}

	XR_PoseBasis( s->handQuat[hand], axis );
	return qtrue;
}

qboolean XR_HandAngles( int hand, vec3_t angles )
{
	vec3_t  axis[3];
	vec3_t  raw;

	if ( !XR_HandBasis( hand, axis ) ) {
		return qfalse;
	}

	AxisToAngles( axis, raw );

	angles[PITCH] = AngleNormalize180( raw[PITCH] );
	angles[YAW]   = AngleNormalize180( raw[YAW] - xr_centreYaw );
	angles[ROLL]  = AngleNormalize180( raw[ROLL] );

	return qtrue;
}

qboolean XR_HandOffset( int hand, vec3_t offset )
{
	const xr_state_t    *s = XR_GetState();
	vec3_t              d;
	float               scale;

	if ( !s->valid || !xr_centred || hand < 0 || hand > 1 ) {
		return qfalse;
	}

	d[0] = s->handPos[hand][0] - xr_centrePos[0];
	d[1] = s->handPos[hand][1] - xr_centrePos[1];
	d[2] = s->handPos[hand][2] - xr_centrePos[2];

	scale = XR_WorldScale();

	// (x, y, z)_xr -> (-z, -x, y)_q3, metres to engine units.
	offset[0] = -d[2] * scale;
	offset[1] = -d[0] * scale;
	offset[2] =  d[1] * scale;

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

	scale = XR_WorldScale();

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
	vec3_t  gun;

	if ( !XR_IsEnabled() ) {
		return;
	}

	// While the VR menu is up, the aim is frozen: no head yaw/pitch is sent,
	// so the view does not swing while the player reads a slider. The latch
	// is cleared too, so the render hook does not try to correct a gun aim
	// that was never sent.
	if ( XR_MenuIsOpen() ) {
		xr_sentWasGun = qfalse;
		return;
	}

	if ( !XR_HeadAngles( head ) ) {
		return;     // no pose: leave the flat game's aim untouched
	}

	// THE AIM IS THE HEAD, and the gun in the hand is scenery.
	//
	// AlphaTest9 tried it the other way round: the usercmd carried the
	// GUN's direction and the camera was rotated back onto the head
	// afterwards. The aim was right and the camera was wrong on hardware --
	// rotating the controller moved the view. That whole correction is gone
	// now, along with the matching one on the movement stick, because there
	// is nothing left to correct: the server is told what the head is
	// doing, cgame builds the view from that, and the view is simply right.
	//
	// What the hand owns the rest of the time is the VISIBLE gun, drawn at
	// the controller in full 6DOF by xr_weapon.c.

	// SHOTS FOLLOW THE DOMINANT HAND, BUT ONLY WHILE THE TRIGGER IS DOWN.
	//
	// Tremulous fires along the player's view angles; that is the only aim
	// a usercmd carries and a pure server will not take a second one. So
	// for the frames of a squeeze, the view angles ARE the hand's
	// direction, and the camera is put back on the head afterwards in
	// XR_AdjustRefdef.
	//
	// AlphaTest9 did this ALL THE TIME and the view swung with the
	// controller. The difference now is not that the correction got better
	// -- though it did, see XR_AdjustRefdef -- it is that the coupling only
	// exists during a trigger pull instead of permanently.
	//
	// WHAT IS SENT IS LATCHED HERE, and the render hook reads the latch
	// rather than sampling the pose again. Two samples a few milliseconds
	// apart would disagree, and the correction would be computed against an
	// aim that was never sent -- a shimmer on exactly the frames where the
	// player is shooting and looking hardest.
	if ( XR_GunAimActive() && XR_GunAngles( gun ) ) {
		viewangles[YAW]   = AngleNormalize180( viewangles[YAW] + gun[YAW] );
		viewangles[PITCH] = AngleNormalize180( gun[PITCH] );

		VectorCopy( gun, xr_sentGun );
		VectorCopy( head, xr_sentHead );
		xr_sentWasGun = qtrue;

		if ( vr_verbose->integer ) {
			static int  lastLog;
			int         now = Sys_Milliseconds();

			if ( now - lastLog >= 1000 ) {
				lastLog = now;
				XR_Log( "aim: GUN (firing) p=%.1f y=%.1f, head y=%.1f -> "
						"usercmd=[%.1f %.1f]",
						gun[PITCH], gun[YAW], head[YAW],
						viewangles[PITCH], viewangles[YAW] );
			}
		}
		return;
	}

	xr_sentWasGun = qfalse;
	viewangles[YAW] = AngleNormalize180( viewangles[YAW] + head[YAW] );

	if ( vr_verbose->integer ) {
		static int  lastHeadLog;
		int         now = Sys_Milliseconds();

		if ( now - lastHeadLog >= 1000 ) {
			lastHeadLog = now;
			XR_Log( "aim: HEAD y=%.1f usercmd=[%.1f %.1f]",
					head[YAW], head[PITCH], viewangles[YAW] );
		}
	}

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

	// ---- put the camera back on the head, if the shot took the aim ------
	//
	// Only on the frames where XR_AdjustUsercmdAngles sent the GUN's
	// direction. cgame has built this refdef pointing down the barrel; we
	// re-express the view basis from the gun's frame into the head's, and
	// the camera returns to the head while everything cgame layered on top
	// -- weapon kick, fall bob, the death cam, a wallwalker's whole
	// orientation -- rides along, because this composes onto viewaxis
	// rather than replacing it.
	//
	// BODY YAW GOES IN BOTH FRAMES. Leaving it out was the AlphaTest9
	// defect. The correction is V' = V . G^T . H -- read the camera's
	// orientation relative to the gun, then re-read it relative to the
	// head -- and that is only right when G and H are the frames V is
	// actually expressed against, which are WORLD frames. Drop body yaw b
	// from both and what comes out is b conjugated by the gun,
	// G^T.Rz(b).G, which equals Rz(b) only when b is 0. A rotation does not
	// cancel against itself across a change of basis the way an additive
	// term would, so the error was zero facing yaw 0 -- where the old
	// desktop test stood -- and grew with b everywhere else.
	//
	// The angles come from the LATCH, so this corrects for the aim that was
	// really sent rather than for a fresh sample that was not.
	//
	// Kick and bob are deliberately absent from G: they are in V, and
	// V.G^T keeps them. That is what "composes onto" means here.

	if ( xr_sentWasGun ) {
		vec3_t  gunAngles  = { 0.0f, 0.0f, 0.0f };
		vec3_t  headAngles = { 0.0f, 0.0f, 0.0f };
		vec3_t  gunAxis[3], headAxis[3], out[3];
		int     i;

		gunAngles[PITCH]  = xr_sentGun[PITCH];
		gunAngles[YAW]    = AngleNormalize180( xr_sentGun[YAW] + bodyYaw );
		headAngles[PITCH] = vr_pitchEnable->integer ? xr_sentHead[PITCH] : 0.0f;
		headAngles[YAW]   = AngleNormalize180( xr_sentHead[YAW] + bodyYaw );

		AnglesToAxis( gunAngles, gunAxis );
		AnglesToAxis( headAngles, headAxis );

		for ( i = 0; i < 3; i++ ) {
			float   l0 = DotProduct( rd->viewaxis[i], gunAxis[0] );
			float   l1 = DotProduct( rd->viewaxis[i], gunAxis[1] );
			float   l2 = DotProduct( rd->viewaxis[i], gunAxis[2] );

			out[i][0] = l0 * headAxis[0][0] + l1 * headAxis[1][0]
					  + l2 * headAxis[2][0];
			out[i][1] = l0 * headAxis[0][1] + l1 * headAxis[1][1]
					  + l2 * headAxis[2][1];
			out[i][2] = l0 * headAxis[0][2] + l1 * headAxis[1][2]
					  + l2 * headAxis[2][2];
		}

		AxisCopy( out, rd->viewaxis );
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

	// ---- one FOV for every class ----------------------------------------
	//
	// cgame takes the FOV from the class attribute, so a Dretch renders at
	// 140*0.75 and a Basilisk at 120*0.75 against the human 90*0.75. That
	// makes the world swell and shrink between classes and leaves the HUD
	// looking oversized on the wide ones. The owner's call, 20 Nov 2026:
	// keep the human FOV for every class and let the per-class scale carry
	// the body size instead.
	//
	// Clamp rather than assign, so a zoom or a narrower scripted FOV still
	// works; only a value WIDER than the human one is replaced. The
	// horizontal angle is recomputed from the vertical and the frame aspect
	// with cgame's own formula, so the projection stays correct.
	if ( XR_CurrentClassIsAlien() && rd->width > 0 && rd->height > 0
		 && rd->fov_y > XR_HUMAN_FOV_Y ) {
		float y = rd->height / tan( 0.5f * DEG2RAD( XR_HUMAN_FOV_Y ) );

		rd->fov_y = XR_HUMAN_FOV_Y;
		rd->fov_x = 2.0f * RAD2DEG( atan2( rd->width, y ) );
	}

	// ---- FOV scale, comfort only ----------------------------------------
	//
	// A narrower FOV is a comfort choice; a wider one would be a gameplay
	// advantage, which the no-advantage rule forbids. So the control is
	// clamped to 1.0 at the top, and the outbound FOV is noted AFTER this
	// (cl_cgame.c) so the runtime is still told what was really drawn.
	if ( vr_fovScale->value < 1.0f ) {
		float   s = Com_Clamp( 0.5f, 1.0f, vr_fovScale->value );

		rd->fov_x = Com_Clamp( 1.0f, 179.0f, rd->fov_x * s );
		rd->fov_y = Com_Clamp( 1.0f, 179.0f, rd->fov_y * s );
	}

	// THE number behind "why is rotating the controller rotating my head?".
	// camYaw is where the camera ended up pointing; expected is where the
	// head alone says it should be. If camYaw tracks gun y, the gun has got
	// into the view again and this log says so without a headset.
	if ( vr_verbose->integer ) {
		static int  lastCamLog;
		int         now = Sys_Milliseconds();
		vec3_t      gun;

		if ( now - lastCamLog >= 1000 ) {
			lastCamLog = now;
			XR_Log( "cam: camYaw=%.1f expected=%.1f (bodyYaw=%.1f + head y=%.1f)  gun y=%.1f",
					AngleNormalize180( RAD2DEG( atan2( rd->viewaxis[0][1],
													   rd->viewaxis[0][0] ) ) ),
					AngleNormalize180( bodyYaw + head[YAW] ),
					bodyYaw, head[YAW],
					XR_GunAngles( gun ) ? gun[YAW] : 0.0f );
		}
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
XR_NoteClass
==================
*/
void XR_NoteClass( int classIndex, int viewheight )
{
	static int  lastClass = -9999;

	// Class change, not viewheight change: viewheight moves on every crouch,
	// and a line per crouch would bury the log. The class is the thing the
	// scale hinges on.
	if ( classIndex == lastClass ) {
		return;
	}
	lastClass = classIndex;

	if ( classIndex >= 0 && classIndex < XR_NUM_CLASSES ) {
		xr_classIndex = classIndex;
		xr_classEye = xr_classes[classIndex].eyeAboveFeet;
	} else {
		xr_classIndex = -1;
		xr_classEye = XR_HUMAN_EYE;
	}

	if ( !XR_IsEnabled() ) {
		return;
	}

	XR_Log( "class: %s (idx %d) viewheight=%d eye=%d -> worldScale %.2f u/m "
			"(human %.1f, classScale %d)",
			xr_classIndex >= 0 ? xr_classes[classIndex].name : "unknown",
			classIndex, viewheight, xr_classEye, XR_WorldScale(),
			vr_worldScale->value, vr_classScale->integer );
}

/*
==================
XR_CurrentWorldScale / XR_CurrentClassName

Read-only accessors for the VR menu's status lines. They deliberately report
the DERIVED scale, not the cvar, because "what is the player's metre worth
right now" is the question the tester is actually asking.
==================
*/
float XR_CurrentWorldScale( void )
{
	return XR_WorldScale();
}

const char *XR_CurrentClassName( void )
{
	return ( xr_classIndex >= 0 ) ? xr_classes[xr_classIndex].name : "unknown";
}

qboolean XR_CurrentClassIsAlien( void )
{
	// 1-2 are the alien builders, 3-10 the alien levels. Spectator (0) and
	// the two human classes (11-12) are not.
	return ( xr_classIndex >= 1 && xr_classIndex <= 10 ) ? qtrue : qfalse;
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

	// Engine units per metre, for a HUMAN. DERIVED, not guessed:
	// human_base.cfg puts the eye at viewheight 24 with mins[2] -24, so 48
	// units above the feet. At a 1.6 m human eye height that is 30 u/m.
	//
	// This is the reference, not the whole answer. vr_classScale derives the
	// other classes from it through xr_classes above: a Tyrant gets
	// 30 * 86/48 = 53.75 u/m, a Dretch 30 * 15/48 = 9.375. Set
	// vr_classScale 0 to pin every class to this number, which is what a
	// desktop test that must not move when the class changes wants.
	vr_worldScale  = Cvar_Get( "vr_worldScale",  "30", CVAR_ARCHIVE );
	vr_classScale  = Cvar_Get( "vr_classScale",  "1",  CVAR_ARCHIVE );

	// Comfort only, and clamped <= 1 when applied. A wider FOV than the
	// class allows would be a gameplay advantage; narrower is a preference.
	vr_fovScale    = Cvar_Get( "vr_fovScale",    "1",  CVAR_ARCHIVE );

	// Orientation first, position second: FIRST_STEPS stage 6 is explicit
	// that getting looking-around right with fewer variables comes first. Run
	// CHECKPOINT 6 once with vr_posTracking 0, then again with it on.
	vr_posTracking = Cvar_Get( "vr_posTracking", "1", CVAR_ARCHIVE );

	vr_rollEnable  = Cvar_Get( "vr_rollEnable",  "1", CVAR_ARCHIVE );
	vr_pitchEnable = Cvar_Get( "vr_pitchEnable", "1", CVAR_ARCHIVE );

	Cmd_AddCommand( "vr_recentre", XR_Recentre_f );
	Cmd_AddCommand( "vr_recenter", XR_Recentre_f );   // both spellings

	if ( vr_enable->integer ) {
		XR_Log( "view: worldScale=%.1f u/m (human reference) classScale=%d "
				"fovScale=%.2f posTracking=%d roll=%d pitch=%d",
				vr_worldScale->value, vr_classScale->integer,
				vr_fovScale->value, vr_posTracking->integer,
				vr_rollEnable->integer, vr_pitchEnable->integer );
	}
}
