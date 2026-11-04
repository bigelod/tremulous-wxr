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
// xr_input.c -- controller input (stage 7)
//
// WHY DIGITAL CONTROLS BECOME KEY EVENTS, which is the whole design
//
// The obvious implementation is to set usercmd button bits directly:
// BUTTON_ATTACK for the trigger, and so on. It is wrong here, and the reason
// is the same rule that keeps this port out of the QVMs.
//
// Half the controls the owner asked for are NOT usercmd bits at all. Reload,
// deconstruct and the score screen are *commands* (`reload`, `deconstruct`,
// `+scores`), registered by cgame via trap_AddCommand and dispatched by the
// QVM. The purchase/evolve menu is a UI menu. None of those can be expressed
// as a bit, and none can be reimplemented engine-side, because the code that
// implements them lives in QVMs a pure server replaces at connect time.
//
// So the digital controls are fed in as if they came from the keyboard, with
// Com_QueueEvent( SE_KEY ). That routes through CL_KeyEvent and the bind
// system, which means every one of these works for free and works exactly as
// it does on the desktop:
//
//   * `+attack` / `+button5` -- held buttons, with the engine's own
//     wasPressed handling so a press shorter than a frame is not lost.
//   * `reload`, `deconstruct` -- client commands relayed to the server.
//   * `+scores` / `-scores` -- a held cgame command.
//   * `+button7` (BUTTON_USE_EVOLVE) -- opens the armoury/evolve menu.
//   * `+speed`, `+moveup`, `+movedown` -- with cl_run and BUTTON_WALKING
//     handled by CL_KeyMove, which we then read back in XR_StickMove.
//
// It also means the player's own binds are respected: we send a KEY, and
// whatever they bound it to is what happens. Each mapping is therefore a
// cvar holding a KEY NAME, defaulting to Tremulous's stock bind, so the
// scheme can be retuned on the headset without a rebuild -- which matters a
// great deal when one round trip costs ten minutes.
//
// THE BUTTON TABLE
//
// Indices into the bool string, from docs/general_wxr/XRAPI_V0_4_DESC.md
// section 3, which reconciled them against a device log, and independently
// confirmed by Shogo-VR-WXR's XrApiUDP.h ("0 = L_GRIP ... 12 = R_GRIP
// ... 18 = R_TRIGGER"). Two sources that agree, neither of them a guess.
//
// PROJECT_PLAN.md still lists the table as an open item to be confirmed from
// a device log, and that has not been done yet. So `vr_verbose 1` logs the
// raw string together with the DECODED NAMES on every change -- one session
// of pressing each button settles it, and if an index is wrong the log says
// which one rather than leaving "the controls feel wrong" to guess at.
//
// WHAT IS DELIBERATELY NOT MAPPED
//
// The left menu button. WinlatorXR turns it into ESC for us, and the owner's
// instruction was to leave that alone; binding it here would either double it
// up or swallow it.

#include "xr_input.h"
#include "xr_api.h"
#include "xr_view.h"
#include "xr_weapon.h"
#include "../client/client.h"

// Bool-string indices. Named rather than inlined, because a bare 18 in the
// middle of a mapping table is exactly the sort of thing that gets silently
// mistranscribed.
#define XRB_L_GRIP      0
#define XRB_L_MENU      1       // ESC, handled by the runtime -- not mapped
#define XRB_L_STICK     2
#define XRB_L_TRIGGER   7
#define XRB_X           8
#define XRB_Y           9
#define XRB_A           10
#define XRB_B           11
#define XRB_R_GRIP      12
#define XRB_R_STICK     13
#define XRB_R_TRIGGER   18

static cvar_t   *vr_dominantHand;   // 0 = right (default), 1 = left
static cvar_t   *vr_stickDeadzone;
static cvar_t   *vr_turnSpeed;      // degrees per second at full deflection
static cvar_t   *vr_moveEnable;
static cvar_t   *vr_inputAlways;    // process input outside play too
static cvar_t   *vr_turnEnable;
static cvar_t   *vr_verbose;

/*
==================
The action table

One row per control the owner specified. `key` is a cvar holding a key NAME,
so the default reproduces the stock Tremulous bind and the owner can change
any of it from the console on the device.

`hand` says whether the physical button follows the dominant hand, so
vr_dominantHand 1 swaps them without a second table to keep in step.
==================
*/
typedef enum {
	HAND_NONE,          // index is fixed regardless of handedness
	HAND_DOMINANT,
	HAND_OFF
} xrHand_t;

typedef struct {
	const char  *name;          // for the log, and for the cvar
	const char  *cvarName;
	const char  *defaultKey;
	int         indexRight;     // bool index when the right hand is dominant
	int         indexLeft;      // bool index when the left hand is dominant
	const char  *what;          // what it does, for the log
} xrAction_t;

static const xrAction_t xr_actions[] = {
	// Triggers and grips: these swap with handedness.
	{ "fire",        "vr_key_fire",        "MOUSE1", XRB_R_TRIGGER, XRB_L_TRIGGER, "+attack" },
	{ "altFire",     "vr_key_altFire",     "MOUSE2", XRB_L_TRIGGER, XRB_R_TRIGGER, "+button5 (attack2)" },
	{ "jump",        "vr_key_jump",        "SPACE",  XRB_R_GRIP,    XRB_L_GRIP,    "+moveup" },
	{ "crouch",      "vr_key_crouch",      "c",      XRB_L_GRIP,    XRB_R_GRIP,    "+movedown" },
	// **[device - HUD text, 3 Nov 2026]** NOT "+speed". Tremulous's own
	// tutorial draws these as two different controls, and cg_tutorial.c's
	// table settles it:
	//
	//     { "+speed",   "Run/Walk" }
	//     { "+button8", "Sprint"   }
	//
	// +speed is the walk modifier; the real sprint is BUTTON_SPRINT, which
	// bg_pmove.c burns stamina for. This shipped as +speed in AlphaTest7 and
	// was simply the wrong control.
	//
	// Tremulous leaves Sprint UNBOUND -- the HUD said so in as many words,
	// 'Press "Sprint" (unbound)'. So scripts/autoexec_alphatest.cfg binds
	// HOME to +button8 and this points at it. HOME is free in the stock
	// default.cfg, which is why it was chosen.
	//
	// Set vr_key_sprint back to SHIFT if the Run/Walk modifier is wanted
	// instead; it is one cvar either way.
	{ "sprint",      "vr_key_sprint",      "HOME",   XRB_L_STICK,   XRB_R_STICK,   "+button8 (Sprint)" },

	// Face buttons. On a Quest, X/Y are the left controller and A/B the
	// right, so "the dominant hand's two buttons" swaps with handedness too.
	// A does two jobs, and sends two keys to do them. Tremulous expects ENTER
	// (+button2, BUTTON_USE_HOLDABLE) to ACTIVATE whatever the player cycled
	// to with weapnext/weapprev -- the cycle selects, it does not commit. That
	// split is deliberate in the game: it lets you line up a medkit while
	// still shooting, and then use it.
	//
	// Sharing the bool index is all it takes: two rows, two key cvars, two
	// edges detected from the same button. Keeping them as separate actions
	// rather than one row sending two keys is what makes them separable --
	// moving "useItem" to another button later is one cvar, not a code change.
	//
	// **The interaction to watch**, since the owner proposed this pairing
	// knowing A is otherwise idle: at an armoury, A now opens the buy menu
	// AND activates the selected upgrade in the same press. If that turns out
	// to spend a medkit when you meant to shop, set vr_key_useItem to another
	// key and bind it elsewhere -- or to "" to switch it off.
	{ "buy",         "vr_key_buy",         "q",      XRB_A,         XRB_X,         "+button7 (purchase/evolve menu)" },
	{ "useItem",     "vr_key_useItem",     "ENTER",  XRB_A,         XRB_X,         "+button2 (activate selected upgrade)" },
	{ "reload",      "vr_key_reload",      "r",      XRB_B,         XRB_Y,         "reload" },
	{ "scores",      "vr_key_scores",      "TAB",    XRB_X,         XRB_A,         "+scores" },
	{ "deconstruct", "vr_key_deconstruct", "e",      XRB_Y,         XRB_B,         "deconstruct" },
};

#define XR_NUM_ACTIONS  ARRAY_LEN( xr_actions )

static cvar_t   *xr_actionKey[XR_NUM_ACTIONS];
static qboolean xr_actionHeld[XR_NUM_ACTIONS];
static int      xr_actionKeynum[XR_NUM_ACTIONS];   // what we pressed, so we release the same key

// Weapon switching off the dominant stick's Y axis needs to re-arm: push and
// hold must change weapon ONCE, not every frame. Latched until the stick
// comes back inside the re-arm threshold.
static qboolean xr_weapLatched;

// Commands rather than key names, unlike the buttons above: weapon change is
// a one-shot off an ANALOG axis, so there is no press/release pair to feed
// the bind system and nothing to be gained by routing it through a key.
static cvar_t   *vr_cmd_weapNext;
static cvar_t   *vr_cmd_weapPrev;

/*
==================
The menu click

**[device - Quest 3, 3 Nov 2026]** The gate below switches the controller off
outside play, to stop 'c' or TAB typing into a 2D menu. Correct, but it left
the menus with no CLICK at all, and Tremulous's spawn menu has to be clicked
to get into the game. The owner could not spawn without reaching for
WinlatorXR's on-screen keyboard to send CTRL by hand -- which is Tremulous's
other +attack bind, and the only way they found through.

So while the gate is CLOSED the trigger becomes a mouse click, and nothing
else is sent. That keeps the gate doing its job -- no letter keys reaching a
text field -- while making the one control a menu actually needs work.

EITHER trigger clicks, tracked as a single logical button: the dominant one
because that is what was asked for, and the off hand as well because in a
menu it means nothing else, and a menu that ignores the trigger you happened
to pull is the problem we are fixing. Taking the OR of the two and sending
one key press is what keeps that safe -- two independent down/up pairs on the
same key would let the first release strand the second press down.
==================
*/
static cvar_t   *vr_key_menuClick;

static qboolean xr_menuClickHeld;
static int      xr_menuClickKeynum = -1;

/*
==================
XR_ResolveKey

Key name -> keynum, resolved per press rather than cached, so changing a
vr_key_* cvar on the device takes effect immediately.
==================
*/
static int XR_ResolveKey( cvar_t *cv )
{
	char    buf[64];

	if ( !cv || !cv->string[0] ) {
		return -1;
	}

	// Key_StringToKeynum takes a non-const char*, and we must not hand it
	// cvar storage to scribble on.
	Q_strncpyz( buf, cv->string, sizeof( buf ) );

	return Key_StringToKeynum( buf );
}

/*
==================
XR_SendKey
==================
*/
static void XR_SendKey( int keynum, qboolean down )
{
	if ( keynum < 0 ) {
		return;
	}

	// Queued rather than dispatched straight into CL_KeyEvent. Com_EventLoop
	// is the engine's single entry point for input, so going through it keeps
	// us on the same path as the keyboard -- journalling included. The cost
	// is that the event lands at the top of the next Com_Frame, under 14 ms
	// at 72 Hz, which is below the threshold of noticing on a trigger.
	Com_QueueEvent( 0, SE_KEY, keynum, down, 0, NULL );
}

/*
==================
XR_ActionIndex

Which bool index drives this action, given the current handedness.
==================
*/
static int XR_ActionIndex( int action )
{
	return vr_dominantHand->integer
			? xr_actions[action].indexLeft
			: xr_actions[action].indexRight;
}

/*
==================
XR_FireHeld

Is the fire control down right now?

Read straight off the action table rather than off a second copy of the
button index, so handedness cannot disagree between "which trigger fires"
and "which trigger aims". Action 0 is fire by construction; the assert-like
name check keeps that true if the table is ever reordered.
==================
*/
qboolean XR_FireHeld( void )
{
	if ( Q_stricmp( xr_actions[0].name, "fire" ) ) {
		return qfalse;          // table reordered; fail safe, head aim
	}

	return XR_Button( XR_ActionIndex( 0 ) );
}
/*
==================
XR_LogButtons

The decoded table, on every change, behind vr_verbose. This is the line that
turns "the controls feel wrong" into "index 12 is not the right grip".
==================
*/
static void XR_LogButtons( void )
{
	const xr_state_t    *s = XR_GetState();
	char                line[1024];
	size_t              i;

	Q_strncpyz( line, "", sizeof( line ) );

	for ( i = 0; i < XR_NUM_ACTIONS; i++ ) {
		if ( XR_Button( XR_ActionIndex( (int)i ) ) ) {
			Q_strcat( line, sizeof( line ), xr_actions[i].name );
			Q_strcat( line, sizeof( line ), " " );
		}
	}

	// The raw string goes out alongside the decode, always. If our table is
	// wrong the decode is wrong too, and then the raw string is the only
	// trustworthy thing in the line.
	XR_Log( "input: bools=\"%s\" (%d) down=[%s] hand=%s",
			s->bools, s->numBools,
			line[0] ? line : "-",
			vr_dominantHand->integer ? "left" : "right" );
}

/*
==================
XR_ReleaseActions

The nine mapped actions only. Separate from the menu click, because the two
are alive at opposite times and each has to be dropped as the other takes
over -- a trigger still held across the transition would otherwise stay down
in whichever scheme it left behind.
==================
*/
static void XR_ReleaseActions( void )
{
	size_t  i;

	for ( i = 0; i < XR_NUM_ACTIONS; i++ ) {
		if ( xr_actionHeld[i] ) {
			XR_SendKey( xr_actionKeynum[i], qfalse );
			xr_actionKeynum[i] = -1;
			xr_actionHeld[i] = qfalse;
		}
	}

	xr_weapLatched = qfalse;
}

/*
==================
XR_ReleaseMenuClick
==================
*/
static void XR_ReleaseMenuClick( void )
{
	if ( xr_menuClickHeld ) {
		XR_SendKey( xr_menuClickKeynum, qfalse );
		xr_menuClickKeynum = -1;
		xr_menuClickHeld = qfalse;
	}
}

/*
==================
XR_MenuClickFrame

Runs only while the gate is closed. Both triggers, OR'd into one key.
==================
*/
static void XR_MenuClickFrame( void )
{
	qboolean    down = ( XR_Button( XRB_R_TRIGGER ) || XR_Button( XRB_L_TRIGGER ) )
						? qtrue : qfalse;

	if ( down == xr_menuClickHeld ) {
		return;
	}

	if ( down ) {
		xr_menuClickKeynum = XR_ResolveKey( vr_key_menuClick );

		if ( xr_menuClickKeynum < 0 ) {
			Com_Printf( "WARNING: vr_key_menuClick is \"%s\", which is not a "
						"key name. Menus cannot be clicked.\n",
						vr_key_menuClick->string );
			xr_menuClickHeld = qtrue;    // latch, so this prints once
			return;
		}

		XR_SendKey( xr_menuClickKeynum, qtrue );

		if ( vr_verbose->integer ) {
			XR_Log( "input: menu click DOWN (key \"%s\")",
					vr_key_menuClick->string );
		}
	} else {
		XR_SendKey( xr_menuClickKeynum, qfalse );
		xr_menuClickKeynum = -1;
	}

	xr_menuClickHeld = down;
}

/*
==================
XR_InputFrame
==================
*/
void XR_InputFrame( void )
{
	static char     lastBools[XR_MAX_BOOLS + 1];
	const xr_state_t *s;
	size_t          i;

	if ( !XR_IsEnabled() ) {
		return;
	}

	s = XR_GetState();

	// No pose yet means no controllers either. Nothing to release, because
	// nothing can have been pressed.
	if ( !s->valid ) {
		return;
	}

	// Input only while actually playing. The menus are flat 2D screens driven
	// by the mouse (vr_autoMode drops MODE_VR for exactly that reason), and
	// synthesising 'c', 'r' or TAB into a menu would type into text fields
	// and move selections. The runtime's own ESC still reaches the menu,
	// which is the one control that has to work there.
	// Whether the gate is open, and WHY, logged on every transition. "The
	// controller does nothing" has three quite different causes -- not
	// connected, a menu holding the key catcher, or input disabled -- and
	// they are indistinguishable from inside the headset. Without this line
	// each one costs a round trip to tell apart.
	{
		static int  lastGate = -1;
		int         gate = ( XR_InGame() || vr_inputAlways->integer ) ? 1 : 0;

		if ( gate != lastGate ) {
			lastGate = gate;
			XR_Log( "input: gate=%s  clc.state=%d keycatcher=0x%x "
					"inGame=%d vr_inputAlways=%d",
					gate ? "OPEN" : "closed",
					(int)clc.state, Key_GetCatcher( ),
					XR_InGame() ? 1 : 0, vr_inputAlways->integer );
		}
	}

	if ( !XR_InGame() && !vr_inputAlways->integer ) {
		// Gate closed: drop the play controls, and leave the trigger working
		// as a mouse click so menus -- the spawn menu above all -- can be
		// clicked at all. See "The menu click" above.
		XR_ReleaseActions( );
		XR_MenuClickFrame( );
		return;
	}

	// Gate open. The trigger belongs to +attack from here, so let go of the
	// click first or it stays down for the rest of the round.
	XR_ReleaseMenuClick( );

	if ( vr_verbose->integer && strcmp( s->bools, lastBools ) != 0 ) {
		XR_LogButtons( );
		Q_strncpyz( lastBools, s->bools, sizeof( lastBools ) );
	}

	for ( i = 0; i < XR_NUM_ACTIONS; i++ ) {
		qboolean    down = XR_Button( XR_ActionIndex( (int)i ) );

		if ( down == xr_actionHeld[i] ) {
			continue;               // no edge
		}

		if ( down ) {
			// Resolve on the press and remember it, so a cvar changed while
			// the button is held still releases the key we actually pressed.
			// Otherwise the old key sticks down forever.
			xr_actionKeynum[i] = XR_ResolveKey( xr_actionKey[i] );

			// An EMPTY cvar is "switch this control off", not a mistake, so it
			// passes without complaint. That is the documented way to unpair
			// the two actions that share the A button.
			if ( !xr_actionKey[i]->string[0] ) {
				xr_actionHeld[i] = qtrue;
				continue;
			}

			if ( xr_actionKeynum[i] < 0 ) {
				Com_Printf( "WARNING: %s is \"%s\", which is not a key name. "
							"%s will do nothing.\n",
							xr_actions[i].cvarName,
							xr_actionKey[i]->string,
							xr_actions[i].name );
				// Still latch, so the warning does not repeat every frame.
				xr_actionHeld[i] = qtrue;
				continue;
			}

			XR_SendKey( xr_actionKeynum[i], qtrue );
		} else {
			XR_SendKey( xr_actionKeynum[i], qfalse );
			xr_actionKeynum[i] = -1;
		}

		xr_actionHeld[i] = down;
	}
}

/*
==================
XR_InputReleaseAll
==================
*/
void XR_InputReleaseAll( void )
{
	XR_ReleaseActions( );
	XR_ReleaseMenuClick( );
}

/*
==================
XR_StickAxis

One axis, with the deadzone rescaled rather than clipped: without the
rescale the stick jumps from nothing to 15% the instant it leaves the
deadzone, which reads as a twitch.
==================
*/
static float XR_StickAxis( float v )
{
	float   dz = vr_stickDeadzone->value;
	float   mag;

	if ( dz < 0.0f )  dz = 0.0f;
	if ( dz > 0.9f )  dz = 0.9f;

	mag = fabs( v );
	if ( mag <= dz ) {
		return 0.0f;
	}

	mag = ( mag - dz ) / ( 1.0f - dz );
	if ( mag > 1.0f ) {
		mag = 1.0f;
	}

	return ( v < 0.0f ) ? -mag : mag;
}

/*
==================
XR_StickMove

The off hand's stick drives forwardmove / rightmove.

Additive and clamped, so the keyboard keeps working alongside -- which is
what lets the thing be tested at a desk with WASD while the spoofer supplies
a pose.

Movement is relative to where the BODY faces, not the head: these are
usercmd axes and the server resolves them against cmd->angles. Head-relative
strafing would need the yaw difference folded in here, and that is a separate
decision about how the port should feel, not a detail to settle silently.
==================
*/
void XR_StickMove( usercmd_t *cmd )
{
	const xr_state_t    *s;
	int                 moveHand;
	float               side, fwd;
	int                 speed;

	if ( !XR_IsEnabled() || !vr_moveEnable->integer ) {
		return;
	}

	s = XR_GetState();
	if ( !s->valid || ( !XR_InGame() && !vr_inputAlways->integer ) ) {
		return;
	}

	// The off hand moves, the dominant hand turns.
	moveHand = vr_dominantHand->integer ? 1 : 0;

	side = XR_StickAxis( s->stick[moveHand][0] );
	fwd  = XR_StickAxis( s->stick[moveHand][1] );

	if ( side == 0.0f && fwd == 0.0f ) {
		return;
	}

	// Read back the engine's own walk/run decision rather than making a
	// second one. CL_KeyMove has already weighed in_speed against cl_run,
	// and in_speed is driven by the synthetic SHIFT from the sprint button,
	// so the stick inherits all of it for free and cannot disagree with the
	// keyboard about how fast the player is going.
	speed = ( cmd->buttons & BUTTON_WALKING ) ? 64 : 127;

	// While the trigger is down the usercmd carries the GUN's direction, so
	// the server resolves forwardmove/rightmove against the GUN's yaw too --
	// point the gun sideways, hold the trigger, and "forward" would walk you
	// sideways. Pre-rotate the stick by (head - gun) and the two cancel, so
	// the player keeps walking where they are LOOKING even mid-burst.
	//
	// Same gate as the aim and the camera, by construction. Done on the
	// input rather than on the angles because the angles are the server's
	// business and the stick is ours.
	if ( XR_GunAimActive() ) {
		vec3_t  gun, head;

		if ( XR_GunAngles( gun ) && XR_HeadAngles( head ) ) {
			float   d = DEG2RAD( head[YAW] - gun[YAW] );
			float   c = cos( d ), sn = sin( d );
			// Q3 movement is fwd*forward + side*right, and right sits at
			// (yaw - 90), so the vector that rotates with yaw is (fwd, -side).
			float   a = fwd, b = -side;
			float   a2 = a * c - b * sn;
			float   b2 = a * sn + b * c;

			fwd  = a2;
			side = -b2;
		}
	}
	cmd->forwardmove = ClampChar( cmd->forwardmove + (int)( fwd  * speed ) );
	cmd->rightmove   = ClampChar( cmd->rightmove   + (int)( side * speed ) );

	// Rate-limited, because this runs at the usercmd rate. It separates the
	// two failures that look identical from inside a headset: "the stick is
	// not reaching the usercmd" (no line, or zeroes here) and "the usercmd is
	// right but the player is not moving" (a line with real numbers, and then
	// the cause is the game -- dead, spectating, or in a spawn queue).
	if ( vr_verbose->integer ) {
		static int  lastLog;
		int         now = Sys_Milliseconds();

		if ( now - lastLog >= 1000 ) {
			lastLog = now;
			XR_Log( "input: stick move=[%.2f %.2f] -> fwd=%d side=%d "
					"speed=%d walking=%d",
					side, fwd, cmd->forwardmove, cmd->rightmove,
					speed, ( cmd->buttons & BUTTON_WALKING ) ? 1 : 0 );
		}
	}
}

/*
==================
XR_StickLook

Dominant stick. X turns the body; Y changes weapon.

Yaw goes into cl.viewangles, which is the BODY yaw -- the same place
CL_MouseMove puts mouse turn. The head's own yaw is added on top of this
later, in XR_AdjustUsercmdAngles. Putting stick turn anywhere else would
either fight the head pose or double-apply it.

Pitch is deliberately absent. The head owns pitch (vr_pitchEnable), and a
stick that also pitched would let the player aim somewhere they are not
looking.
==================
*/
void XR_StickLook( int msec )
{
	const xr_state_t    *s;
	int                 turnHand;
	float               x, y;

	if ( !XR_IsEnabled() || !vr_turnEnable->integer ) {
		return;
	}

	s = XR_GetState();
	if ( !s->valid || ( !XR_InGame() && !vr_inputAlways->integer ) ) {
		return;
	}

	if ( msec <= 0 ) {
		return;
	}

	turnHand = vr_dominantHand->integer ? 0 : 1;

	x = XR_StickAxis( s->stick[turnHand][0] );
	y = XR_StickAxis( s->stick[turnHand][1] );

	// Right is a yaw DECREASE: Quake yaw runs counter-clockwise, and
	// CL_MouseMove subtracts for the same reason. Scaled by frame time so
	// the turn rate is degrees per second rather than per frame, which would
	// make it a function of how busy the scene is.
	if ( x != 0.0f ) {
		cl.viewangles[YAW] = AngleNormalize180(
				cl.viewangles[YAW] - x * vr_turnSpeed->value * ( msec / 1000.0f ) );
	}

	// Weapon change, latched. Hold the stick up and the weapon changes once;
	// it must come back towards centre to arm the next change. Without this
	// a held stick cycles the whole inventory in a fraction of a second.
	if ( !xr_weapLatched ) {
		if ( y > 0.7f ) {
			Cbuf_ExecuteText( EXEC_APPEND,
							  va( "%s\n", vr_cmd_weapNext->string ) );
			xr_weapLatched = qtrue;
		} else if ( y < -0.7f ) {
			Cbuf_ExecuteText( EXEC_APPEND,
							  va( "%s\n", vr_cmd_weapPrev->string ) );
			xr_weapLatched = qtrue;
		}
	} else if ( fabs( y ) < 0.4f ) {
		xr_weapLatched = qfalse;
	}
}

/*
==================
XR_InputInit
==================
*/
void XR_InputInit( void )
{
	size_t  i;

	// Right-handed by default, because that is what the control scheme was
	// specified against. One cvar swaps triggers, grips, sticks and the face
	// buttons together.
	vr_dominantHand  = Cvar_Get( "vr_dominantHand",  "0",   CVAR_ARCHIVE );

	vr_stickDeadzone = Cvar_Get( "vr_stickDeadzone", "0.15", CVAR_ARCHIVE );
	vr_turnSpeed     = Cvar_Get( "vr_turnSpeed",     "140",  CVAR_ARCHIVE );

	// Off switches, so "is it the camera or the input?" is one cvar on the
	// device rather than another build.
	vr_moveEnable    = Cvar_Get( "vr_moveEnable",    "1",    CVAR_ARCHIVE );

	// Desk testing, and the only way to exercise the input path without
	// being spawned into a map. Default 0, so shipped behaviour is unchanged
	// and nothing can type into a menu by accident.
	vr_inputAlways   = Cvar_Get( "vr_inputAlways",   "0",    CVAR_ARCHIVE );
	vr_turnEnable    = Cvar_Get( "vr_turnEnable",    "1",    CVAR_ARCHIVE );

	vr_verbose       = Cvar_Get( "vr_verbose",       "0",    CVAR_ARCHIVE );

	// MOUSE1 because that is what a 2D menu expects, and because it is also
	// Tremulous's +attack -- which is what the player needs to press to spawn
	// once the menu has closed.
	vr_key_menuClick = Cvar_Get( "vr_key_menuClick", "MOUSE1", CVAR_ARCHIVE );

	vr_cmd_weapNext  = Cvar_Get( "vr_cmd_weapNext",  "weapnext", CVAR_ARCHIVE );
	vr_cmd_weapPrev  = Cvar_Get( "vr_cmd_weapPrev",  "weapprev", CVAR_ARCHIVE );

	for ( i = 0; i < XR_NUM_ACTIONS; i++ ) {
		xr_actionKey[i] = Cvar_Get( xr_actions[i].cvarName,
									xr_actions[i].defaultKey,
									CVAR_ARCHIVE );
		xr_actionHeld[i]   = qfalse;
		xr_actionKeynum[i] = -1;
	}
}
