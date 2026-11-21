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
// xr_menu.c -- the engine-side VR settings menu
//
// WHY NOT THE GAME'S OWN UI
//
// Tremulous's menus live in ui.qvm, and a pure server forces its own QVMs on
// the client at connect time. A menu added there would not exist on the
// servers where the game is played. This overlay is drawn by the client and
// only sets cvars, so it survives purity and works with any mod's UI.
//
// WHY AN OVERLAY RATHER THAN A PAUSE MENU
//
// Multiplayer cannot pause. The menu suppresses the usercmd instead (see
// CL_CreateCmd), so the player stands still and does not fire while it is
// open, but the game keeps running around them. That is the honest behaviour
// on a live server.
//
// CONTROLS
//
//   dominant stick click   open / close (vr_menuButton -1 = auto)
//   move stick up/down     move the selection
//   turn stick left/right  adjust a slider
//   dominant trigger       select / toggle
//   dominant B             back one page, then close
//   F1                     open / close on the desktop (`vr_menu`)
//
// The panel is drawn in 640x480 virtual space through SCR_DrawStringExt, so
// it is independent of the cgame 2D layer and of vr_hudScale. It is drawn
// BEFORE XR_DrawSyncPixel so the runtime's corner is still the last pixel
// written.

#include "xr_menu.h"
#include "xr_api.h"
#include "xr_view.h"
#include "xr_hud.h"
#include "../client/client.h"

// The two stick-click bool indices. Private in xr_input.c; repeated here
// because the meaning is fixed by the protocol, not by our action table.
#define XR_MENU_L_STICK     2
#define XR_MENU_R_STICK     13

typedef enum {
	XR_MENU_TOGGLE,
	XR_MENU_SLIDER,
	XR_MENU_ACTION,
	XR_MENU_SUBMENU,
	XR_MENU_BACK,
	XR_MENU_CLOSE
} xrMenuType_t;

typedef struct xrMenuPage_s xrMenuPage_t;

typedef struct {
	const char          *label;
	xrMenuType_t        type;
	const char          *cvar;      // toggle / slider
	float               min;
	float               max;
	float               step;
	const char          *action;    // action
	const xrMenuPage_t  *target;    // submenu
} xrMenuItem_t;

struct xrMenuPage_s {
	const char          *title;
	const xrMenuItem_t  *items;
	int                 count;
};

/*
===========================================================================
The pages

Every entry is a cvar the engine already owns, or an engine command. Nothing
here reaches into cgame or game code, so a pure server cannot take it away.
===========================================================================
*/

static const xrMenuItem_t xr_debugItems[] = {
	{ "Verbose log",        XR_MENU_TOGGLE, "vr_verbose" },
	{ "Sync pixel",         XR_MENU_TOGGLE, "vr_syncPixel" },
	{ "Position",           XR_MENU_TOGGLE, "vr_posTracking" },
	{ "Head roll",          XR_MENU_TOGGLE, "vr_rollEnable" },
	{ "Head pitch",         XR_MENU_TOGGLE, "vr_pitchEnable" },
	{ "Class scale",        XR_MENU_TOGGLE, "vr_classScale" },
	{ "Hand gun",           XR_MENU_TOGGLE, "vr_handGun" },
	{ "Aim on fire",        XR_MENU_TOGGLE, "vr_handAimOnFire" },
	{ "Left-handed",        XR_MENU_TOGGLE, "vr_dominantHand" },
	{ "Deadzone",           XR_MENU_SLIDER, "vr_stickDeadzone", 0.0f, 0.5f, 0.05f },
	{ "Turn speed",         XR_MENU_SLIDER, "vr_turnSpeed",    60.0f, 300.0f, 10.0f },
	{ "Invert menu",        XR_MENU_TOGGLE, "vr_menuInvert" },
	{ "Recentre",           XR_MENU_ACTION, NULL, 0.0f, 0.0f, 0.0f, "vr_recentre" },
	{ "Back",               XR_MENU_BACK }
};

static const xrMenuPage_t xr_pageDebug = {
	"DEBUG",
	xr_debugItems,
	(int)( sizeof( xr_debugItems ) / sizeof( xr_debugItems[0] ) )
};

static const xrMenuItem_t xr_qualityItems[] = {
	{ "Game FOV",             XR_MENU_SLIDER, "vr_fovScale",   0.5f,  1.0f,  0.05f },
	{ "FOV H",                XR_MENU_SLIDER, "vr_fovX",      80.0f, 130.0f, 0.5f },
	{ "FOV V",                XR_MENU_SLIDER, "vr_fovY",      80.0f, 130.0f, 0.5f },
	{ "World scale",          XR_MENU_SLIDER, "vr_worldScale",20.0f,  50.0f,  1.0f },
	{ "HUD human",            XR_MENU_SLIDER, "vr_hudScale",   0.3f,  1.0f,  0.05f },
	{ "HUD alien",            XR_MENU_SLIDER, "vr_hudScaleAlien",0.2f, 1.0f,  0.05f },
	{ "Hide crosshair",       XR_MENU_TOGGLE, "vr_hideCrosshair" },
	{ "Gun aim align",        XR_MENU_TOGGLE, "vr_gunAimAlign" },
	{ "Desktop input",        XR_MENU_TOGGLE, "vr_blockDesktopInput" },
	{ "Back",                 XR_MENU_BACK }
};

static const xrMenuPage_t xr_pageQuality = {
	"QUALITY",
	xr_qualityItems,
	(int)( sizeof( xr_qualityItems ) / sizeof( xr_qualityItems[0] ) )
};

// Commands that the PC build binds to keys the controller does not have --
// deconstruct, the middle-click button, buy ammo, the medkit, votes. They are
// here rather than on the controller because there is no spare button left;
// the page is the discoverable, headset-friendly route to them.
static const xrMenuItem_t xr_actionItems[] = {
	{ "Deconstruct",    XR_MENU_ACTION, NULL, 0.0f, 0.0f, 0.0f, "deconstruct" },
	{ "Middle click",   XR_MENU_ACTION, NULL, 0.0f, 0.0f, 0.0f, "+button3\n-button3" },
	{ "Buy ammo",       XR_MENU_ACTION, NULL, 0.0f, 0.0f, 0.0f, "buy ammo" },
	{ "Use medkit",     XR_MENU_ACTION, NULL, 0.0f, 0.0f, 0.0f, "itemact medkit" },
	{ "Reload",         XR_MENU_ACTION, NULL, 0.0f, 0.0f, 0.0f, "reload" },
	{ "Vote yes",       XR_MENU_ACTION, NULL, 0.0f, 0.0f, 0.0f, "vote yes" },
	{ "Vote no",        XR_MENU_ACTION, NULL, 0.0f, 0.0f, 0.0f, "vote no" },
	{ "Back",           XR_MENU_BACK }
};

static const xrMenuPage_t xr_pageActions = {
	"ACTIONS",
	xr_actionItems,
	(int)( sizeof( xr_actionItems ) / sizeof( xr_actionItems[0] ) )
};

static const xrMenuItem_t xr_rootItems[] = {
	{ "DEBUG",   XR_MENU_SUBMENU, NULL, 0.0f, 0.0f, 0.0f, NULL, &xr_pageDebug },
	{ "QUALITY", XR_MENU_SUBMENU, NULL, 0.0f, 0.0f, 0.0f, NULL, &xr_pageQuality },
	{ "ACTIONS", XR_MENU_SUBMENU, NULL, 0.0f, 0.0f, 0.0f, NULL, &xr_pageActions },
	{ "Close",   XR_MENU_CLOSE }
};

static const xrMenuPage_t xr_pageRoot = {
	"VR",
	xr_rootItems,
	(int)( sizeof( xr_rootItems ) / sizeof( xr_rootItems[0] ) )
};

/*
===========================================================================
State
===========================================================================
*/

static qboolean                 xr_menuOpen;
static const xrMenuPage_t       *xr_menuPage;

static int                      xr_menuIndex;
static int                      xr_menuLastButton;
static int                      xr_menuLastSelect;
static int                      xr_menuLastBack;

static int                      xr_menuVerticalDir;
static int                      xr_menuVerticalNext;
static int                      xr_menuHorizontalDir;
static int                      xr_menuHorizontalNext;

#define XR_MENU_DEADZONE     0.55f
#define XR_MENU_FIRST_REPEAT 350
#define XR_MENU_REPEAT       120

/*
===========================================================================
Colours
===========================================================================
*/

static vec4_t xr_menuDim      = { 0.00f, 0.00f, 0.00f, 0.45f };
static vec4_t xr_menuBg       = { 0.03f, 0.05f, 0.08f, 0.92f };
static vec4_t xr_menuBorder   = { 0.25f, 0.55f, 0.90f, 0.95f };
static vec4_t xr_menuText     = { 0.88f, 0.93f, 1.00f, 1.00f };
static vec4_t xr_menuDimText  = { 0.55f, 0.62f, 0.72f, 1.00f };
static vec4_t xr_menuTitle    = { 0.45f, 0.85f, 1.00f, 1.00f };
static vec4_t xr_menuSelect   = { 0.10f, 0.28f, 0.55f, 0.95f };
static vec4_t xr_menuOn       = { 0.45f, 1.00f, 0.55f, 1.00f };
static vec4_t xr_menuOff      = { 0.65f, 0.65f, 0.70f, 1.00f };
static vec4_t xr_menuBarBg    = { 0.18f, 0.20f, 0.24f, 1.00f };
static vec4_t xr_menuBar      = { 0.35f, 0.75f, 1.00f, 1.00f };

/*
===========================================================================
Helpers
===========================================================================
*/

static void XR_MenuFill( int x, int y, int w, int h, const vec4_t c )
{
	re.SetColor( c );
	re.DrawStretchPic( (float)x, (float)y, (float)w, (float)h,
					   0.0f, 0.0f, 1.0f, 1.0f, cls.whiteShader );
}

static void XR_MenuChar( int x, int y, int size, int ch )
{
	int     row, col;
	float   frow, fcol;
	const float step = 0.0625f;

	ch &= 255;
	if ( ch == ' ' ) {
		return;
	}

	row = ch >> 4;
	col = ch & 15;
	frow = row * step;
	fcol = col * step;

	re.DrawStretchPic( (float)x, (float)y, (float)size, (float)size,
					   fcol, frow, fcol + step, frow + step,
					   cls.charSetShader );
}

static int XR_MenuText( int x, int y, int size, const char *text,
						const vec4_t c )
{
	int start = x;

	re.SetColor( c );

	for ( ; *text; text++, x += size ) {
		XR_MenuChar( x, y, size, (unsigned char)*text );
	}

	return x - start;
}

static const xrMenuPage_t *XR_MenuPage( void )
{
	return xr_menuPage ? xr_menuPage : &xr_pageRoot;
}

static void XR_MenuOpen( void )
{
	xr_menuOpen = qtrue;
	xr_menuPage = &xr_pageRoot;
	xr_menuIndex = 0;
	xr_menuVerticalDir = 0;
	xr_menuHorizontalDir = 0;

	// The trigger may still be held from firing. Require a release before it
	// can act as SELECT, or opening the menu would immediately activate the
	// first item.
	xr_menuLastSelect = 1;
	xr_menuLastBack = 1;

	XR_Log( "menu: open" );
}

static void XR_MenuClose( void )
{
	xr_menuOpen = qfalse;
	XR_Log( "menu: closed" );
}

void XR_MenuToggle( void )
{
	XR_Log( "menu: toggle requested (enabled=%d, open=%d)",
			XR_IsEnabled( ), xr_menuOpen );

	if ( !XR_IsEnabled() ) {
		Com_Printf( "XR: vr_menu needs vr_enable 1\n" );
		return;
	}

	if ( xr_menuOpen ) {
		XR_MenuClose();
	} else {
		XR_MenuOpen();
	}
}

qboolean XR_MenuIsOpen( void )
{
	return xr_menuOpen;
}

/*
==================
XR_MenuToggleIndex

The dominant stick click is the one stick click the action table does not
use: sprint is the OFF stick's click. So using the dominant one means the
menu button follows handedness and never steals sprint.

vr_menuButton -1 means auto; >= 0 is a literal bool index for a custom bind.
==================
*/
static int XR_MenuToggleIndex( void )
{
	int idx = Cvar_VariableIntegerValue( "vr_menuButton" );

	if ( idx >= 0 ) {
		return idx;
	}

	return Cvar_VariableIntegerValue( "vr_dominantHand" )
		   ? XR_MENU_L_STICK : XR_MENU_R_STICK;
}

static int XR_MenuSelectIndex( void )
{
	return Cvar_VariableIntegerValue( "vr_dominantHand" ) ? 7 : 18;
}

static int XR_MenuBackIndex( void )
{
	return Cvar_VariableIntegerValue( "vr_dominantHand" ) ? 9 : 11;
}

static void XR_MenuMove( int dir )
{
	const xrMenuPage_t *page = XR_MenuPage( );
	int n = page->count;

	if ( n <= 0 ) {
		return;
	}

	// Clamp rather than wrap. Wrapping made "up" from the first item select
	// the last one, which reads as an inverted stick even when the sign is
	// right. The owner explicitly does not want that surprise.
	xr_menuIndex += dir;
	if ( xr_menuIndex < 0 ) {
		xr_menuIndex = 0;
	}
	if ( xr_menuIndex >= n ) {
		xr_menuIndex = n - 1;
	}
}

static void XR_MenuAdjust( int dir )
{
	const xrMenuItem_t *it = &XR_MenuPage( )->items[xr_menuIndex];
	float v;

	if ( it->type != XR_MENU_SLIDER || !it->cvar ) {
		return;
	}

	v = Cvar_VariableValue( it->cvar ) + ( dir * it->step );
	v = Com_Clamp( it->min, it->max, v );

	Cvar_SetValue( it->cvar, v );
}

static void XR_MenuBack( void )
{
	if ( XR_MenuPage( ) != &xr_pageRoot ) {
		xr_menuPage = &xr_pageRoot;
		xr_menuIndex = 0;
	} else {
		XR_MenuClose( );
	}
}

static void XR_MenuActivate( void )
{
	const xrMenuItem_t *it = &XR_MenuPage( )->items[xr_menuIndex];

	switch ( it->type ) {
	case XR_MENU_TOGGLE:
		if ( !it->cvar ) {
			return;
		}
		Cvar_SetValue( it->cvar, !Cvar_VariableIntegerValue( it->cvar ) );
		XR_Log( "menu: %s %s", it->cvar,
				Cvar_VariableIntegerValue( it->cvar ) ? "on" : "off" );

		// cg_drawCrosshair is cgame's cvar, but it lives in the client;
		// re-apply so the change is visible without a restart. Turning the
		// option off restores the crosshair, which the init-only path never
		// needed to do.
		if ( !Q_stricmp( it->cvar, "vr_hideCrosshair" ) ) {
			if ( Cvar_VariableIntegerValue( "vr_hideCrosshair" ) ) {
				XR_HideCrosshair( );
			} else {
				Cvar_Set( "cg_drawCrosshair", "1" );
				XR_Log( "hud: cg_drawCrosshair 1 -- vr_hideCrosshair off" );
			}
		}
		break;

	case XR_MENU_SLIDER:
		XR_MenuAdjust( 1 );
		break;

	case XR_MENU_ACTION:
		if ( it->action ) {
			Cbuf_ExecuteText( EXEC_NOW, va( "%s\n", it->action ) );
		}
		break;

	case XR_MENU_SUBMENU:
		if ( it->target ) {
			xr_menuPage = it->target;
			xr_menuIndex = 0;
		}
		break;

	case XR_MENU_BACK:
		XR_MenuBack( );
		break;

	case XR_MENU_CLOSE:
		XR_MenuClose( );
		break;
	}
}

/*
==================
XR_MenuFrame
==================
*/
qboolean XR_MenuFrame( void )
{
	const xr_state_t    *s = XR_GetState( );
	int                 toggleIdx;
	int                 down;
	int                 moveHand, turnHand;
	int                 selectIdx, backIdx;
	int                 select, back;
	int                 now;
	int                 vdir, hdir;
	float               v, h;
	const xrMenuItem_t  *selected;

	toggleIdx = XR_MenuToggleIndex( );
	down = ( toggleIdx >= 0 ) ? XR_Button( toggleIdx ) : 0;

	if ( down && !xr_menuLastButton ) {
		if ( xr_menuOpen ) {
			XR_MenuClose( );
		} else if ( XR_InGame( )
					|| Cvar_VariableIntegerValue( "vr_inputAlways" ) ) {
			XR_MenuOpen( );
		}
	}
	xr_menuLastButton = down;

	if ( !xr_menuOpen ) {
		return qfalse;
	}

	// A console or a QVM menu has taken the screen. Close rather than draw
	// over something the player is trying to read or click.
	if ( ( Key_GetCatcher( ) & ( KEYCATCH_CONSOLE | KEYCATCH_UI
								 | KEYCATCH_CGAME ) )
		 || ( !XR_InGame( )
			  && !Cvar_VariableIntegerValue( "vr_inputAlways" ) ) ) {
		XR_MenuClose( );
		return qtrue;
	}

	moveHand = Cvar_VariableIntegerValue( "vr_dominantHand" ) ? 1 : 0;
	turnHand = Cvar_VariableIntegerValue( "vr_dominantHand" ) ? 0 : 1;

	v = s->stick[moveHand][1];
	h = s->stick[turnHand][0];

	// The packet's Y is forward-positive in the owner's capture, so positive
	// is "up" in a list. vr_menuInvert flips this for a headset or a hand
	// that reports the other way; it is a DEBUG toggle and a cvar, so a
	// tester never needs the console to correct it.
	vdir = ( v > XR_MENU_DEADZONE ) ? -1 : ( ( v < -XR_MENU_DEADZONE ) ? 1 : 0 );

	if ( Cvar_VariableIntegerValue( "vr_menuInvert" ) ) {
		vdir = -vdir;
	}
	hdir = ( h > XR_MENU_DEADZONE ) ? 1 : ( ( h < -XR_MENU_DEADZONE ) ? -1 : 0 );

	now = Sys_Milliseconds( );

	if ( vdir ) {
		if ( vdir != xr_menuVerticalDir || now >= xr_menuVerticalNext ) {
			XR_MenuMove( vdir );
			xr_menuVerticalNext = now
				+ ( ( vdir != xr_menuVerticalDir ) ? XR_MENU_FIRST_REPEAT
												   : XR_MENU_REPEAT );
			xr_menuVerticalDir = vdir;
		}
	} else {
		xr_menuVerticalDir = 0;
	}

	selected = &XR_MenuPage( )->items[xr_menuIndex];

	if ( hdir && selected->type == XR_MENU_SLIDER ) {
		if ( hdir != xr_menuHorizontalDir
			 || now >= xr_menuHorizontalNext ) {
			XR_MenuAdjust( hdir );
			xr_menuHorizontalNext = now
				+ ( ( hdir != xr_menuHorizontalDir ) ? XR_MENU_FIRST_REPEAT
													 : 80 );
			xr_menuHorizontalDir = hdir;
		}
	} else {
		xr_menuHorizontalDir = 0;
	}

	selectIdx = XR_MenuSelectIndex( );
	backIdx = XR_MenuBackIndex( );
	select = XR_Button( selectIdx );
	back = XR_Button( backIdx );

	if ( select && !xr_menuLastSelect ) {
		XR_MenuActivate( );
	}
	if ( back && !xr_menuLastBack ) {
		XR_MenuBack( );
	}

	xr_menuLastSelect = select;
	xr_menuLastBack = back;

	return qtrue;
}

/*
==================
XR_MenuKeyEvent
==================
*/
qboolean XR_MenuKeyEvent( int key, qboolean down )
{
	if ( !xr_menuOpen ) {
		return qfalse;
	}

	switch ( key ) {
	case K_UPARROW:
	case K_KP_UPARROW:
		if ( down ) {
			XR_MenuMove( -1 );
		}
		return qtrue;

	case K_DOWNARROW:
	case K_KP_DOWNARROW:
		if ( down ) {
			XR_MenuMove( 1 );
		}
		return qtrue;

	case K_LEFTARROW:
	case K_KP_LEFTARROW:
		if ( down ) {
			XR_MenuAdjust( -1 );
		}
		return qtrue;

	case K_RIGHTARROW:
	case K_KP_RIGHTARROW:
		if ( down ) {
			XR_MenuAdjust( 1 );
		}
		return qtrue;

	case K_ENTER:
	case K_KP_ENTER:
		if ( down ) {
			XR_MenuActivate( );
		}
		return qtrue;

	case K_ESCAPE:
	case K_BACKSPACE:
		if ( down ) {
			XR_MenuBack( );
		}
		return qtrue;

	default:
		return qfalse;
	}
}

/*
===========================================================================
Drawing
===========================================================================
*/

static void XR_MenuValueText( const xrMenuItem_t *it, char *buf, int size )
{
	switch ( it->type ) {
	case XR_MENU_TOGGLE:
		Q_strncpyz( buf,
					Cvar_VariableIntegerValue( it->cvar ) ? "ON"
														  : "OFF",
					size );
		break;
	case XR_MENU_SLIDER:
		Com_sprintf( buf, size, "%.2f", Cvar_VariableValue( it->cvar ) );
		break;
	case XR_MENU_SUBMENU:
		Q_strncpyz( buf, ">", size );
		break;
	case XR_MENU_ACTION:
		Q_strncpyz( buf, "RUN", size );
		break;
	case XR_MENU_BACK:
		Q_strncpyz( buf, "<", size );
		break;
	case XR_MENU_CLOSE:
		Q_strncpyz( buf, "X", size );
		break;
	}
}

void XR_MenuDraw( void )
{
	const xrMenuPage_t  *page;
	const xrMenuItem_t  *it;
	const vec4_t        *valueColor;
	char                value[64];
	char                line[256];
	float               fovX, fovY;
	int                 vw, vh;
	int                 size, small, rowH, pad, border;
	int                 pw, ph, header, footer;
	int                 x, y, rowY0, rowY, labelX, valueRight, valueX;
	int                 barX, barW, barH;
	int                 bottom, footer1, footer2, status1, status2;
	int                 titleSize;
	int                 i;

	if ( !xr_menuOpen || !XR_IsEnabled( ) ) {
		return;
	}

	vw = cls.glconfig.vidWidth;
	vh = cls.glconfig.vidHeight;

	// Real pixels, not the 640x480 virtual space the game's HUD uses. That
	// space is stretched by the window's aspect, and on a square headset
	// frame it makes the glyphs 33% taller than they are wide -- which is
	// exactly what "hard to read" looks like. Drawing at a size derived from
	// the frame height keeps the panel the same physical size at 1024x768,
	// 1400x1400 and 2800x1400.
	if ( vw < 320 || vh < 240 ) {
		return;
	}

	page = XR_MenuPage( );

	size = vh / 44;
	if ( size < 12 ) {
		size = 12;
	}

	small = size * 2 / 3;
	if ( small < 12 ) {
		small = 12;
	}

	rowH = size * 3 / 2;
	if ( rowH < 20 ) {
		rowH = 20;
	}

	pad = size;

	// Width follows the frame height, so the panel subtends a similar angle
	// whatever the container resolution. Capped on very wide frames so it
	// does not become a thin column.
	pw = vh * 62 / 100;
	if ( pw > vw * 50 / 100 ) {
		pw = vw * 50 / 100;
	}
	if ( pw < 360 ) {
		pw = 360;
	}
	if ( pw > vw - 20 ) {
		pw = vw - 20;
	}

	header = rowH + size;
	footer = rowH * 4;              // two status lines + two help lines
	ph = header + ( page->count * rowH ) + footer + pad;

	if ( ph > vh - 16 ) {
		int avail = ( vh - 16 ) - header - footer - pad;

		if ( avail < page->count * 14 ) {
			// Nothing sensible left to trim; let the list clip rather than
			// draw a panel taller than the frame.
			ph = vh - 16;
		} else {
			rowH = avail / page->count;
			ph = header + ( page->count * rowH ) + footer + pad;
		}
	}

	x = ( vw - pw ) / 2;
	y = ( vh - ph ) / 2;

	border = size / 12;
	if ( border < 2 ) {
		border = 2;
	}

	// Dim the world behind the panel so text reads against any map.
	XR_MenuFill( 0, 0, vw, vh, xr_menuDim );

	XR_MenuFill( x, y, pw, ph, xr_menuBg );
	XR_MenuFill( x, y, pw, border, xr_menuBorder );
	XR_MenuFill( x, y + ph - border, pw, border, xr_menuBorder );
	XR_MenuFill( x, y, border, ph, xr_menuBorder );
	XR_MenuFill( x + pw - border, y, border, ph, xr_menuBorder );

	titleSize = size * 3 / 2;
	XR_MenuText( x + pad, y + pad / 2, titleSize, "VR", xr_menuTitle );
	XR_MenuText( x + pad + ( titleSize * 2 ) + size,
				 y + pad / 2 + ( titleSize - size ) / 2, size,
				 ( page == &xr_pageRoot ) ? "SETTINGS" : page->title,
				 xr_menuText );

	XR_MenuFill( x + pad, y + header - size / 2, pw - ( pad * 2 ), 1,
				 xr_menuBorder );

	rowY0 = y + header;
	labelX = x + pad;
	valueRight = x + pw - pad;
	valueX = valueRight - ( size * 5 );
	barW = size * 3;
	barH = size / 4;
	if ( barH < 6 ) {
		barH = 6;
	}
	barX = valueX - barW - size;

	for ( i = 0; i < page->count; i++ ) {
		it = &page->items[i];
		rowY = rowY0 + ( i * rowH );

		if ( i == xr_menuIndex ) {
			XR_MenuFill( x + ( pad / 2 ), rowY, pw - pad, rowH,
						 xr_menuSelect );
		}

		XR_MenuText( labelX, rowY + ( ( rowH - size ) / 2 ), size,
					 it->label, xr_menuText );

		if ( it->type == XR_MENU_SLIDER ) {
			float v = Cvar_VariableValue( it->cvar );
			float f = ( it->max > it->min )
					  ? ( v - it->min ) / ( it->max - it->min ) : 0.0f;
			int   barY = rowY + ( ( rowH - barH ) / 2 );

			f = Com_Clamp( 0.0f, 1.0f, f );

			XR_MenuFill( barX, barY, barW, barH, xr_menuBarBg );
			XR_MenuFill( barX, barY, (int)( barW * f ), barH, xr_menuBar );

			Com_sprintf( value, sizeof( value ), "%.2f", v );
			XR_MenuText( valueX, rowY + ( ( rowH - size ) / 2 ), size,
						 value, xr_menuText );
		} else {
			XR_MenuValueText( it, value, sizeof( value ) );
			valueColor = &xr_menuText;

			if ( it->type == XR_MENU_TOGGLE ) {
				valueColor = Cvar_VariableIntegerValue( it->cvar )
							 ? &xr_menuOn : &xr_menuOff;
			}

			XR_MenuText( valueX, rowY + ( ( rowH - size ) / 2 ), size,
						 value, *valueColor );
		}
	}

	// Status and help, laid out from the bottom so they cannot collide with
	// the last row. Read-only numbers the tester would otherwise pull a log
	// for.
	bottom = y + ph - pad;
	footer2 = bottom - small;
	footer1 = footer2 - rowH;
	status2 = footer1 - rowH;
	status1 = status2 - rowH;

	XR_GetRenderedFov( &fovX, &fovY );

	if ( page == &xr_pageDebug ) {
		Com_sprintf( line, sizeof( line ),
					 "class %s  %.2f u/m  pkt %d",
					 XR_CurrentClassName( ), XR_CurrentWorldScale( ),
					 XR_GetState( )->packetsReceived );
		XR_MenuText( labelX, status1, small, line, xr_menuDimText );

		Com_sprintf( line, sizeof( line ),
					 "FOV %.1f x %.1f  sync %.0f",
					 fovX, fovY, XR_GetState( )->sync );
		XR_MenuText( labelX, status2, small, line, xr_menuDimText );
	} else if ( page == &xr_pageQuality ) {
		Com_sprintf( line, sizeof( line ),
					 "FOV %.1f x %.1f  frame %d x %d",
					 fovX, fovY, vw, vh );
		XR_MenuText( labelX, status1, small, line, xr_menuDimText );

		Com_sprintf( line, sizeof( line ),
					 "class %s  %.2f u/m",
					 XR_CurrentClassName( ), XR_CurrentWorldScale( ) );
		XR_MenuText( labelX, status2, small, line, xr_menuDimText );
	} else {
		Com_sprintf( line, sizeof( line ),
					 "class %s  %.2f u/m  pkt %d",
					 XR_CurrentClassName( ), XR_CurrentWorldScale( ),
					 XR_GetState( )->packetsReceived );
		XR_MenuText( labelX, status1, small, line, xr_menuDimText );

		XR_MenuText( labelX, status2, small,
					 "TRIGGER selects. Console-free.",
					 xr_menuDimText );
	}

	XR_MenuText( labelX, footer1, small,
				 "STICK MOVE / TRIGGER SELECT / B BACK",
				 xr_menuDimText );
	XR_MenuText( labelX, footer2, small,
				 "F1 or stick click: close",
				 xr_menuDimText );

	re.SetColor( NULL );
}

/*
==================
XR_MenuToggle_f
==================
*/
static void XR_MenuToggle_f( void )
{
	XR_MenuToggle( );
}

/*
==================
XR_MenuInit
==================
*/
void XR_MenuInit( void )
{
	// -1 = the dominant stick click, which is the one stick click the action
	// table leaves free. >= 0 pins a literal bool index.
	Cvar_Get( "vr_menuButton", "-1", CVAR_ARCHIVE );

	// 0 = packet Y positive moves the selection up; 1 flips it. A DEBUG
	// toggle because the stick sign is the one thing a screenshot cannot
	// settle.
	Cvar_Get( "vr_menuInvert", "0", CVAR_ARCHIVE );

	Cmd_AddCommand( "vr_menu", XR_MenuToggle_f );

	xr_menuOpen = qfalse;
	xr_menuPage = &xr_pageRoot;
	xr_menuIndex = 0;

	XR_Log( "menu: ready (vr_menu command, vr_menuButton %d)",
			Cvar_VariableIntegerValue( "vr_menuButton" ) );
}
