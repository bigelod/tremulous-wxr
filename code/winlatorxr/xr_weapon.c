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
// xr_weapon.c -- the gun, taken off the HMD and put in the hand (stage 8)
//
// THE SHAPE OF IT, which is the owner's design and worth stating plainly:
//
//   * cgame's view weapon is CAPTURED and not drawn.
//   * A visual duplicate is drawn at the dominant hand, in full 6DOF.
//   * The hidden real gun is AIMED where the visible one points.
//
// The last line is the whole trick. Tremulous fires along the player's view
// angles -- that is the only aim the engine can put in a usercmd, and a pure
// server will not take a second one. So rather than inventing a channel, we
// make the view angles BE the hand's direction. What is drawn is then
// exactly what is shot, which is also why this is honest against a server:
// there is one aim, the server owns it, and the client is not describing a
// different gun from the one it is firing.
//
// Three consequences follow, and all three have to be paid for or the thing
// is unplayable. They live in three different files on purpose, because each
// belongs to the pipeline it corrects:
//
//   1. The CAMERA would follow the hand, since cgame builds the refdef from
//      ps.viewangles. XR_AdjustRefdef composes (head - gun) back on, so the
//      view stays with the head. xr_view.c.
//
//   2. MOVEMENT would follow the hand, since the server resolves
//      forwardmove/rightmove against cmd->angles. XR_StickMove pre-rotates
//      the stick vector by the same delta, so the player still walks where
//      they are looking. xr_input.c.
//
//   3. The 2D crosshair is drawn at screen centre by cgame and now means the
//      HEAD's direction, not the shot. Known and unfixed: the gun model is
//      the aiming reference. A laser sight is the obvious follow-up and is
//      cheap from here, because we already add our own entities.
//
// ALIENS DO NOT GET THIS. They have no gun -- they attack with the body and
// the face -- so for aliens the aim stays on the head exactly as before, and
// cgame's first-person model is left alone. XR_HandGunActive is the single
// place that decides, so the aim, the camera and the movement can never
// disagree about which scheme is running.
//
// WHY CAPTURE RATHER THAN MOVE
//
// CG_AddViewWeapon builds the gun as several entities -- the hands model,
// the gun, the barrel and the muzzle flash -- each positioned on a tag of
// the one before, and all inheriting RF_FIRST_PERSON. Moving them one at a
// time would need every tag relationship rebuilt. Instead every
// RF_FIRST_PERSON entity is held back, and the whole set is re-expressed
// from cgame's camera frame into the hand's frame by one rigid transform.
// Relative geometry is preserved for free, and nothing needs to know which
// entity is the barrel.
//
// The source frame has to be cgame's ORIGINAL refdef, before XR_AdjustRefdef
// moves it: that is the frame cgame actually placed the entities in.

#include "xr_weapon.h"
#include "xr_input.h"
#include "xr_api.h"
#include "xr_view.h"
#include "../client/client.h"

// Tremulous playerState indices. These are game constants, from
// code/game/bg_public.h, and reading them here couples the engine to the
// game's protocol -- which is already true of everything in cl.snap.ps.
// Spelled out rather than included, because bg_public.h is QVM territory
// and the include would be the first step towards putting logic there.
//
// A server running a mod that renumbers these would read as "always alien",
// which fails SAFE: the gun stays on the head and nothing breaks.
#define XRPS_STAT_TEAM      6
#define XRPS_TEAM_HUMANS    2

// pmtype_t, same file. PM_Live( x ) is NORMAL, JETPACK or GRABBED; NOCLIP is
// added because it is how the map gets flown around while testing and the
// gun should not vanish for it.
#define XRPS_PM_NORMAL      0
#define XRPS_PM_NOCLIP      1
#define XRPS_PM_JETPACK     3
#define XRPS_PM_GRABBED     4

#define XR_MAX_FP_ENTS      16

static cvar_t   *vr_handGun;        // master switch for the whole scheme
static cvar_t   *vr_handAimOnFire;  // shots follow the hand while firing
static cvar_t   *vr_gunForward;     // hand-space trim, engine units
static cvar_t   *vr_gunRight;
static cvar_t   *vr_gunUp;
static cvar_t   *vr_gunPitch;       // "auto" = the measured grip offset
static cvar_t   *vr_gunRoll;        // "auto" = decide from the headset make
static cvar_t   *vr_gunYaw;         // "auto"; for hands reported backwards
static cvar_t   *vr_dominantHand;
static cvar_t   *vr_verbose;

static refEntity_t  xr_fpEnts[XR_MAX_FP_ENTS];
static int          xr_numFpEnts;
static int          xr_fpOverflow;

/*
==================
XR_GunHand
==================
*/
static int XR_GunHand( void )
{
	return vr_dominantHand->integer ? 0 : 1;
}

/*
==================
XR_HandGunActive

One decision, asked by four callers. If this ever disagreed between the aim
and the camera the result would be a view that rotates with the hand, which
is the single worst failure this port could ship.
==================
*/
qboolean XR_HandGunActive( void )
{
	vec3_t  dummy;

	if ( !XR_IsEnabled() || !vr_handGun->integer ) {
		return qfalse;
	}

	if ( !XR_InGame() ) {
		return qfalse;
	}

	// Humans only. Aliens attack with the body, so there is nothing to hold.
	if ( cl.snap.ps.stats[XRPS_STAT_TEAM] != XRPS_TEAM_HUMANS ) {
		return qfalse;
	}

	// Dead, spectating or in the intermission: cgame owns the camera
	// completely and there is no weapon to speak of.
	if ( cl.snap.ps.pm_type != XRPS_PM_NORMAL
		 && cl.snap.ps.pm_type != XRPS_PM_NOCLIP
		 && cl.snap.ps.pm_type != XRPS_PM_JETPACK
		 && cl.snap.ps.pm_type != XRPS_PM_GRABBED ) {
		return qfalse;
	}

	// No controller pose means no hand to hold it with.
	if ( !XR_HandAngles( XR_GunHand(), dummy ) ) {
		return qfalse;
	}

	return qtrue;
}

/*
==================
XR_GunTrim

The grip-to-barrel correction: how far to turn the drawn gun out of the
frame the controller reports and into the one a rifle is held in.

MEASURED, not guessed, from the XrApi UDP Tester capture in
SamplePoseData.txt -- a real Quest 3 packet with both controllers held
naturally, which SixDOFinator draws as two level, palm-down hands. Running
those two quaternions through our own conversion gives:

    left  hand   pitch -34.0   roll  176.7
    right hand   pitch -33.3   roll -165.4

THE TWO HANDS AGREE ON PITCH TO WITHIN 0.7 DEGREES. That is the whole
argument: a person does not hold both controllers at an identical 33.6
degrees nose-up by accident. It is a property of the reported frame, not of
the pose, so it is ours to cancel. Roll agrees far less tightly (18 degrees
apart), which is what you would expect -- wrist roll really does differ
between hands - but both sit near a half turn, which is the "hands upside
down" the 0.4 spec describes under HMD DETECTION FOR UPSIDE DOWN HANDS.

So: roll 180 and pitch +34, for META. The pitch figure is also about where
the Touch grip-to-aim offset is usually quoted, which is reassuring but is
not the evidence; the capture is.

Only the drawn model is affected. The aim is the head, so a rolled or
tilted controller could never have moved a shot -- which is also why none of
this could be caught by a log. Nothing numeric was wrong.

DETECTION IS A GUESS ABOUT HARDWARE WE DO NOT HAVE, so every one of these is
overridable, as the spec asks: by cvar, or by handsfix.txt for a player who
has no console. "auto" in either place hands the value back to detection.
==================
*/
static qboolean XR_TrimIsAuto( const cvar_t *cv )
{
	return (qboolean)( !cv->string[0] || !Q_stricmp( cv->string, "auto" ) );
}

static qboolean XR_IsMeta( void )
{
	return (qboolean)( !Q_stricmp( XR_HmdMake(), "META" ) );
}

static float XR_GunTrim( const cvar_t *cv, float metaValue )
{
	if ( !XR_TrimIsAuto( cv ) ) {
		return cv->value;
	}
	return XR_IsMeta() ? metaValue : 0.0f;
}

static float XR_GunRoll( void )
{
	return XR_GunTrim( vr_gunRoll, 180.0f );
}

static float XR_GunPitchTrim( void )
{
	return XR_GunTrim( vr_gunPitch, 34.0f );
}

static float XR_GunYaw( void )
{
	// Nothing we have measured reports the hands BACKWARDS, so auto is zero
	// even on Meta. The spec says it happens on some headsets, so the knob
	// exists; inventing a value for it would be inventing a device.
	return XR_GunTrim( vr_gunYaw, 0.0f );
}

/*
==================
XR_LogGunTrims

Once, on the first frame that draws the gun -- the headset make is not known
until the system file has been read and the first pose has arrived.
==================
*/
static void XR_LogGunTrims( void )
{
	static qboolean logged;

	if ( logged ) {
		return;
	}
	logged = qtrue;

	XR_Log( "gun: %s / %s -- pitch=%.0f (%s) yaw=%.0f (%s) roll=%.0f (%s)",
			XR_HmdMake(), XR_HmdModel(),
			XR_GunPitchTrim(), XR_TrimIsAuto( vr_gunPitch ) ? "auto" : "set",
			XR_GunYaw(),       XR_TrimIsAuto( vr_gunYaw )   ? "auto" : "set",
			XR_GunRoll(),      XR_TrimIsAuto( vr_gunRoll )  ? "auto" : "set" );

	if ( !XR_IsMeta() ) {
		XR_Log( "gun: %s is not META, so NO grip correction is assumed. If "
				"the gun is upside down or points at the sky, put "
				"\"roll 180\" and \"pitch 34\" in handsfix.txt",
				XR_HmdMake() );
	}
}

/*
==================
XR_GunAimActive

THE SINGLE GATE for "the usercmd is carrying the hand's direction instead
of the head's". Three things hang off it and they must never disagree:

  * the aim sent to the server          xr_view.c, XR_AdjustUsercmdAngles
  * putting the camera back on the head xr_view.c, XR_AdjustRefdef
  * un-skewing the movement stick       xr_input.c, XR_StickMove

If any one of those ran while the others did not, the result is a view that
swings when you squeeze the trigger, or a player who walks sideways while
shooting. AlphaTest9 is the standing reminder of what that costs, which is
why this is one function and not three conditions.

ONLY WHILE THE TRIGGER IS DOWN. That is the whole design: the head aims and
the view is the head's, right up until you shoot, and for those frames the
shot follows the gun. The couplings below are real but they only exist
during a squeeze, rather than all the time as they did in AlphaTest9.
==================
*/
qboolean XR_GunAimActive( void )
{
	if ( !vr_handAimOnFire->integer ) {
		return qfalse;
	}

	if ( !XR_HandGunActive() ) {
		return qfalse;      // aliens, spectators, dead, VR off
	}

	return XR_FireHeld();
}
/*
==================
XR_MigrateTrims

A one-time repair of settings saved by an older build.

vr_gunRoll and vr_gunYaw shipped once with a default of "-1" meaning
"detect", and vr_gunPitch with "0". They are CVAR_ARCHIVE, so those values
are now written into every tester's q3config, and the sentinel has since
become the word "auto". A saved "-1" therefore reads as a literal minus one
degree and a saved "0" as a deliberate zero -- detection is silently off,
and the gun comes back upside down on exactly the machines that had already
run the previous build. Measured: roll 165.1 with the old config against
-13.9 with a clean one, same packet.

This is the archived-cvar trap the deploy script warns about, and changing a
default is not enough to escape it. The version stamp is: bump
XR_CFG_VERSION whenever a default changes in a way an old saved value would
defeat, and migrate here.

Only values EQUAL TO THE OLD DEFAULT are touched. Anything else is a number
the player chose, and a migration that discards deliberate tuning is a worse
bug than the one it fixes.
==================
*/
#define XR_CFG_VERSION  2

static void XR_MigrateOneTrim( const char *name, const char *legacy )
{
	cvar_t  *cv = Cvar_Get( name, "auto", CVAR_ARCHIVE );

	if ( Q_stricmp( cv->string, legacy ) ) {
		return;                 // the player chose this; leave it alone
	}

	Cvar_Set( name, "auto" );
	XR_Log( "config: %s was \"%s\", an old build's way of saying \"detect\". "
			"Migrated to \"auto\".", name, legacy );
}

static void XR_MigrateTrims( void )
{
	cvar_t  *ver = Cvar_Get( "vr_cfgVersion", "0", CVAR_ARCHIVE );

	if ( ver->integer >= XR_CFG_VERSION ) {
		return;
	}

	XR_MigrateOneTrim( "vr_gunRoll",  "-1" );
	XR_MigrateOneTrim( "vr_gunYaw",   "-1" );
	XR_MigrateOneTrim( "vr_gunPitch", "0" );

	Cvar_SetValue( "vr_cfgVersion", XR_CFG_VERSION );
}

/*
==================
XR_ReadHandsFix

handsfix.txt, the player's override, read straight off the disk rather than
through the filesystem -- the player creates it by hand on the device, so it
is not in a pk3, and a pure server must not be able to hide it.

One "key value" per line, # for a comment:

    roll 180        degrees, or true/false for 180/0
    yaw 0
    pitch -15
    roll auto       hand that one back to detection

Looked for in base/ first and then beside the executable, so a mod can carry
one without displacing a game-wide file.
==================
*/
static const char *XR_HandsFixValue( const char *tok )
{
	// Resolved to the STRING the cvar should hold, not to a number, because
	// "auto" has to survive into the cvar -- that is how a line in this file
	// hands one trim back to detection while the others stay overridden.
	if ( !Q_stricmp( tok, "true" ) || !Q_stricmp( tok, "yes" )
		 || !Q_stricmp( tok, "on" ) ) {
		return "180";              // "upside down: yes" means half a turn
	}

	if ( !Q_stricmp( tok, "false" ) || !Q_stricmp( tok, "no" )
		 || !Q_stricmp( tok, "off" ) ) {
		return "0";
	}

	if ( !Q_stricmp( tok, "auto" ) ) {
		return "auto";
	}

	if ( *tok == '-' || *tok == '+' || ( *tok >= '0' && *tok <= '9' ) ) {
		return tok;
	}

	return NULL;
}

static void XR_ReadHandsFix( void )
{
	const char  *base = Cvar_VariableString( "fs_basepath" );
	const char  *mod  = Cvar_VariableString( "fs_game" );
	char        path[MAX_OSPATH];
	char        line[256];
	FILE        *f;
	int         applied = 0;

	if ( !base || !base[0] ) {
		return;
	}

	if ( !mod || !mod[0] ) {
		mod = "base";
	}

	Com_sprintf( path, sizeof( path ), "%s%c%s%chandsfix.txt",
				 base, PATH_SEP, mod, PATH_SEP );
	f = fopen( path, "r" );

	if ( !f ) {
		Com_sprintf( path, sizeof( path ), "%s%chandsfix.txt",
					 base, PATH_SEP );
		f = fopen( path, "r" );
	}

	if ( !f ) {
		return;          // the normal case. Detection stands; not an error.
	}

	XR_Log( "handsfix: reading %s -- it overrides headset detection", path );

	while ( fgets( line, sizeof( line ), f ) ) {
		char        key[64], val[64];
		const char  *set;

		if ( sscanf( line, "%63s %63s", key, val ) != 2 || key[0] == '#' ) {
			continue;
		}

		set = XR_HandsFixValue( val );
		if ( !set ) {
			XR_Log( "handsfix: \"%s %s\" -- not a number, true/false or "
					"auto, ignored", key, val );
			continue;
		}

		if ( !Q_stricmp( key, "roll" ) ) {
			Cvar_Set( "vr_gunRoll", set );
		} else if ( !Q_stricmp( key, "yaw" ) ) {
			Cvar_Set( "vr_gunYaw", set );
		} else if ( !Q_stricmp( key, "pitch" ) ) {
			Cvar_Set( "vr_gunPitch", set );
		} else {
			XR_Log( "handsfix: unknown key \"%s\", ignored -- roll, yaw and "
					"pitch are the keys", key );
			continue;
		}

		applied++;
		XR_Log( "handsfix: %s = %s", key, set );
	}

	fclose( f );

	if ( !applied ) {
		XR_Log( "handsfix: nothing usable in the file -- detection stands" );
	}
}

/*
==================
XR_GunAngles

Where the gun points, in the same convention XR_HeadAngles uses: engine
angles, recentred, body yaw NOT included.
==================
*/
qboolean XR_GunAngles( vec3_t angles )
{
	if ( !XR_HandGunActive() ) {
		return qfalse;
	}

	if ( !XR_HandAngles( XR_GunHand(), angles ) ) {
		return qfalse;
	}

	// Controllers are held with the nose tipped forward, so a comfortable
	// grip points the model at the floor. Both trims live here, in the one
	// function every caller goes through, so the drawn gun cannot end up
	// trimmed differently from anything else derived from the same pose.
	angles[PITCH] = AngleNormalize180( angles[PITCH] + XR_GunPitchTrim() );
	angles[YAW]   = AngleNormalize180( angles[YAW]   + XR_GunYaw() );
	angles[ROLL]  = AngleNormalize180( angles[ROLL]  + XR_GunRoll() );

	XR_LogGunTrims();

	return qtrue;
}

/*
==================
XR_CaptureViewWeapon

RF_FIRST_PERSON marks the view weapon and everything hung off it, and
nothing else: it means "draw only when the owner is looking out of their own
eyes". That makes it the exact filter we want, with no model-name matching.
==================
*/
qboolean XR_CaptureViewWeapon( const refEntity_t *ent )
{
	if ( !ent || !( ent->renderfx & RF_FIRST_PERSON ) ) {
		return qfalse;
	}

	if ( !XR_HandGunActive() ) {
		return qfalse;       // aliens, spectators, VR off -- cgame's as usual
	}

	if ( xr_numFpEnts >= XR_MAX_FP_ENTS ) {
		// Drop it rather than draw it in the wrong place. Logged once per
		// frame-run so a weapon that genuinely needs more parts shows up as
		// a number instead of as a missing barrel.
		xr_fpOverflow++;
		return qtrue;
	}

	xr_fpEnts[xr_numFpEnts++] = *ent;
	return qtrue;
}

/*
==================
XR_ReFrame

Re-express a world direction from one orthonormal basis into another:
decompose in the source axes, rebuild in the destination axes.
==================
*/
static void XR_ReFrame( vec3_t src[3], vec3_t dst[3],
						const vec3_t in, vec3_t out )
{
	float   l0 = DotProduct( in, src[0] );
	float   l1 = DotProduct( in, src[1] );
	float   l2 = DotProduct( in, src[2] );

	out[0] = l0 * dst[0][0] + l1 * dst[1][0] + l2 * dst[2][0];
	out[1] = l0 * dst[0][1] + l1 * dst[1][1] + l2 * dst[2][1];
	out[2] = l0 * dst[0][2] + l1 * dst[1][2] + l2 * dst[2][2];
}

/*
==================
XR_EmitHandWeapon
==================
*/
void XR_EmitHandWeapon( const refdef_t *cgameRefdef, float bodyYaw )
{
	const vec3_t    worldUp = { 0.0f, 0.0f, 1.0f };
	vec3_t          gunAngles;
	vec3_t          gunAxis[3];
	vec3_t          srcAxis[3];
	vec3_t          handOffset, rotated, gunOrigin;
	vec3_t          rootRel;
	int             i, j;

	if ( !xr_numFpEnts ) {
		if ( xr_fpOverflow ) {
			XR_Log( "weapon: %d first-person entities dropped (limit %d)",
					xr_fpOverflow, XR_MAX_FP_ENTS );
			xr_fpOverflow = 0;
		}
		return;
	}

	if ( !XR_GunAngles( gunAngles ) ) {
		// Captured but cannot place: put them back where cgame wanted them
		// rather than losing the player's gun entirely.
		for ( i = 0; i < xr_numFpEnts; i++ ) {
			re.AddRefEntityToScene( &xr_fpEnts[i] );
		}
		xr_numFpEnts = 0;
		return;
	}

	// The gun's own basis. Body yaw is added here because gunAngles is in
	// the recentred room frame, exactly as the head's is.
	gunAngles[YAW] = AngleNormalize180( gunAngles[YAW] + bodyYaw );
	gunAngles[ROLL] = AngleNormalize180( gunAngles[ROLL] );
	AnglesToAxis( gunAngles, gunAxis );

	// Where the hand is. Same construction as the head's positional offset
	// in XR_AdjustRefdef -- a room displacement, rotated by the BODY's yaw,
	// added to cgame's unmoved view origin. Using the same vieworg for both
	// is what keeps the gun and the eye in one room rather than two.
	VectorCopy( cgameRefdef->vieworg, gunOrigin );

	if ( XR_HandOffset( XR_GunHand(), handOffset ) ) {
		RotatePointAroundVector( rotated, worldUp, handOffset, bodyYaw );
		VectorAdd( gunOrigin, rotated, gunOrigin );
	}

	// Trim, in the gun's own frame, so it sits in the fist rather than at
	// the tracking origin of the controller. Tunable on the device because
	// the right numbers are a matter of how it feels, not of arithmetic.
	VectorMA( gunOrigin, vr_gunForward->value, gunAxis[0], gunOrigin );
	VectorMA( gunOrigin, vr_gunRight->value,   gunAxis[1], gunOrigin );
	VectorMA( gunOrigin, vr_gunUp->value,      gunAxis[2], gunOrigin );

	// cgame placed every one of these relative to ITS camera, so that is the
	// frame we are moving them out of. Copied vector by vector rather than
	// with AxisCopy, which takes a non-const argument and would need the
	// const casting away.
	for ( i = 0; i < 3; i++ ) {
		VectorCopy( cgameRefdef->viewaxis[i], srcAxis[i] );
	}

	// THE ROOT ENTITY'S OWN OFFSET FROM THE EYE HAS TO COME OFF FIRST, and
	// not doing that is why the rifle filled the screen.
	//
	// cgame holds the gun out in front of the face -- that offset is how a
	// flat first-person weapon is posed at all. Re-anchoring every entity at
	// the hand WITHOUT removing it adds the two together: the model lands at
	// the hand PLUS wherever cgame wanted it relative to the eye, which is
	// roughly double the intended distance and in a direction that no longer
	// means anything, because the frame it was measured in has been replaced.
	//
	// Subtracting the FIRST captured entity's offset, rather than each
	// entity's own, is the point: the set is a rigid assembly of hands, gun,
	// barrel and flash hung off each other's tags. Remove one shared
	// displacement and the root lands exactly on the hand while every
	// relative position inside the assembly survives untouched.
	VectorSubtract( xr_fpEnts[0].origin, cgameRefdef->vieworg, rootRel );

	for ( i = 0; i < xr_numFpEnts; i++ ) {
		refEntity_t *e = &xr_fpEnts[i];
		vec3_t      rel, newRel;

		// Position, relative to cgame's camera and then to the root,
		// re-expressed about the gun.
		VectorSubtract( e->origin, cgameRefdef->vieworg, rel );
		VectorSubtract( rel, rootRel, rel );
		XR_ReFrame( srcAxis, gunAxis, rel, newRel );
		VectorAdd( gunOrigin, newRel, e->origin );

		// Orientation, the same way, axis vector by axis vector.
		for ( j = 0; j < 3; j++ ) {
			vec3_t  a;

			VectorCopy( e->axis[j], a );
			XR_ReFrame( srcAxis, gunAxis, a, e->axis[j] );
		}

		// oldorigin carries the lerp origin for some entities; leaving it in
		// the old frame makes a moving gun smear between two places.
		VectorSubtract( e->oldorigin, cgameRefdef->vieworg, rel );
		VectorSubtract( rel, rootRel, rel );
		XR_ReFrame( srcAxis, gunAxis, rel, newRel );
		VectorAdd( gunOrigin, newRel, e->oldorigin );

		// RF_DEPTHHACK squashes the gun into a narrow depth range so it
		// cannot poke through walls. That is right for a gun welded to the
		// camera and wrong for one held at arm's length in the world -- it
		// makes the model shrink oddly as the hand moves. Drop it and let
		// the gun live in the scene like any other object.
		e->renderfx &= ~RF_DEPTHHACK;

		re.AddRefEntityToScene( e );
	}

	if ( vr_verbose->integer ) {
		static int  lastLog;
		int         now = Sys_Milliseconds();

		if ( now - lastLog >= 1000 ) {
			lastLog = now;
			// The origin is logged as a DISPLACEMENT FROM THE EYE, not as a
			// world position. "Is the gun in my hand?" is a question about
			// where it sits relative to the player, and a world coordinate
			// cannot answer it without knowing where the player was standing.
			XR_Log( "weapon: %d ents  eye+[%.0f %.0f %.0f] (hand %.0f u out, "
					"model %.0f u) angles=[%.1f %.1f %.1f] hand=%s",
					xr_numFpEnts,
					gunOrigin[0] - cgameRefdef->vieworg[0],
					gunOrigin[1] - cgameRefdef->vieworg[1],
					gunOrigin[2] - cgameRefdef->vieworg[2],
					Distance( gunOrigin, cgameRefdef->vieworg ),
					Distance( xr_fpEnts[0].origin, cgameRefdef->vieworg ),
					gunAngles[PITCH], gunAngles[YAW], gunAngles[ROLL],
					XR_GunHand() ? "right" : "left" );
		}
	}

	xr_numFpEnts = 0;
	xr_fpOverflow = 0;
}

/*
==================
XR_WeaponInit
==================
*/
void XR_WeaponInit( void )
{
	vr_handGun      = Cvar_Get( "vr_handGun",     "1",   CVAR_ARCHIVE );

	// Owner's call: the shot goes where the dominant hand points. 0 keeps
	// the aim on the head always, which is AlphaTest10 behaviour and the
	// fallback if squeezing the trigger feels wrong.
	vr_handAimOnFire = Cvar_Get( "vr_handAimOnFire", "1", CVAR_ARCHIVE );

	vr_gunForward   = Cvar_Get( "vr_gunForward",  "0",   CVAR_ARCHIVE );
	vr_gunRight     = Cvar_Get( "vr_gunRight",    "0",   CVAR_ARCHIVE );
	vr_gunUp        = Cvar_Get( "vr_gunUp",       "0",   CVAR_ARCHIVE );
	vr_gunPitch     = Cvar_Get( "vr_gunPitch",    "auto", CVAR_ARCHIVE );
	vr_gunRoll      = Cvar_Get( "vr_gunRoll",     "auto", CVAR_ARCHIVE );
	vr_gunYaw       = Cvar_Get( "vr_gunYaw",      "auto", CVAR_ARCHIVE );

	// Repair an older build's saved sentinels first, so a value the player
	// never chose cannot shadow the detection they are entitled to.
	XR_MigrateTrims();

	// Then handsfix.txt, last, because the player put it there to be
	// obeyed -- it overrides both the detection and the migration.
	XR_ReadHandsFix();

	vr_dominantHand = Cvar_Get( "vr_dominantHand", "0",  CVAR_ARCHIVE );
	vr_verbose      = Cvar_Get( "vr_verbose",      "0",  CVAR_ARCHIVE );
}
