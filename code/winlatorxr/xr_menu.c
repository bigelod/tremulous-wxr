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
	{ "Position tracking",  XR_MENU_TOGGLE, "vr_posTracking" },
	{ "Head roll",          XR_MENU_TOGGLE, "vr_rollEnable" },
	{ "Head pitch",         XR_MENU_TOGGLE, "vr_pitchEnable" },
	{ "Per-class scale",    XR_MENU_TOGGLE, "vr_classScale" },
	{ "Hand gun",           XR_MENU_TOGGLE, "vr_handGun" },
	{ "Hand aim on fire",   XR_MENU_TOGGLE, "vr_handAimOnFire" },
	{ "Left-handed",        XR_MENU_TOGGLE, "vr_dominantHand" },
	{ "Stick deadzone",     XR_MENU_SLIDER, "vr_stickDeadzone", 0.0f, 0.5f, 0.05f },
	{ "Turn speed (deg/s)", XR_MENU_SLIDER, "vr_turnSpeed",    60.0f, 300.0f, 10.0f },
	{ "Recentre",           XR_MENU_ACTION, NULL, 0.0f, 0.0f, 0.0f, "vr_recentre" },
	{ "Back",               XR_MENU_BACK }
};

static const xrMenuPage_t xr_pageDebug = {
	"DEBUG",
	xr_debugItems,
	(int)( sizeof( xr_debugItems ) / sizeof( xr_debugItems[0] ) )
};

static const xrMenuItem_t xr_qualityItems[] = {
	{ "Game FOV scale",       XR_MENU_SLIDER, "vr_fovScale",   0.5f,  1.0f,  0.05f },
	{ "XrAPI FOV H (fallback)", XR_MENU_SLIDER, "vr_fovX",    80.0f, 130.0f, 0.5f },
	{ "XrAPI FOV V (fallback)", XR_MENU_SLIDER, "vr_fovY",    80.0f, 130.0f, 0.5f },
	{ "World scale (human)",  XR_MENU_SLIDER, "vr_worldScale",20.0f,  50.0f,  1.0f },
	{ "HUD scale",            XR_MENU_SLIDER, "vr_hudScale",   0.3f,  1.0f,  0.05f },
	{ "Hide crosshair",       XR_MENU_TOGGLE, "vr_hideCrosshair" },
	{ "Block desktop input",  XR_MENU_TOGGLE, "vr_blockDesktopInput" },
	{ "Back",                 XR_MENU_BACK }
};

static const xrMenuPage_t xr_pageQuality = {
	"QUALITY",
	xr_qualityItems,
	(int)( sizeof( xr_qualityItems ) / sizeof( xr_qualityItems[0] ) )
};

static const xrMenuItem_t xr_rootItems[] = {
	{ "DEBUG",   XR_MENU_SUBMENU, NULL, 0.0f, 0.0f, 0.0f, NULL, &xr_pageDebug },
	{ "QUALITY", XR_MENU_SUBMENU, NULL, 0.0f, 0.0f, 0.0f, NULL, &xr_pageQuality },
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

static void XR_MenuFill( float x, float y, float w, float h, const vec4_t c )
{
	re.SetColor( c );
	SCR_DrawPic( x, y, w, h, cls.whiteShader );
}

static void XR_MenuText( float x, float y, float size, const char *text,
						 const vec4_t c )
{
	vec4_t  col;

	col[0] = c[0];
	col[1] = c[1];
	col[2] = c[2];
	col[3] = c[3];

	SCR_DrawStringExt( (int)x, (int)y, size, text, col, qtrue, qtrue );
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

	xr_menuIndex = ( xr_menuIndex + dir + n ) % n;
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

	vdir = ( v > XR_MENU_DEADZONE ) ? -1 : ( ( v < -XR_MENU_DEADZONE ) ? 1 : 0 );
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
					Cvar_VariableIntegerValue( it->cvar ) ? "[ ON  ]"
														  : "[ OFF ]",
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
	float               x = 90.0f, w = 460.0f;
	float               y, h;
	float               ry, bar;
	char                value[64];
	char                line[256];
	float               fovX, fovY;
	int                 i;

	if ( !xr_menuOpen || !XR_IsEnabled( ) ) {
		return;
	}

	page = XR_MenuPage( );

	// A page-sized panel, centred. Three items and thirteen should not have
	// the same amount of empty space below them.
	h = 150.0f + ( page->count * 22.0f );
	if ( h > 400.0f ) {
		h = 400.0f;
	}
	y = ( 480.0f - h ) * 0.5f;

	// Dim the world behind the panel so text reads against any map.
	XR_MenuFill( 0.0f, 0.0f, 640.0f, 480.0f, xr_menuDim );

	XR_MenuFill( x, y, w, h, xr_menuBg );
	XR_MenuFill( x, y, w, 2.0f, xr_menuBorder );
	XR_MenuFill( x, y + h - 2.0f, w, 2.0f, xr_menuBorder );
	XR_MenuFill( x, y, 2.0f, h, xr_menuBorder );
	XR_MenuFill( x + w - 2.0f, y, 2.0f, h, xr_menuBorder );

	XR_MenuText( x + 16.0f, y + 10.0f, 16.0f, "VR", xr_menuTitle );
	XR_MenuText( x + 62.0f, y + 15.0f, 10.0f,
				 ( page == &xr_pageRoot ) ? "SETTINGS" : page->title,
				 xr_menuText );
	XR_MenuFill( x + 12.0f, y + 34.0f, w - 24.0f, 1.0f, xr_menuBorder );

	for ( i = 0; i < page->count; i++ ) {
		it = &page->items[i];
		ry = y + 50.0f + ( i * 22.0f );

		if ( i == xr_menuIndex ) {
			XR_MenuFill( x + 10.0f, ry - 3.0f, w - 20.0f, 20.0f,
						 xr_menuSelect );
		}

		XR_MenuText( x + 20.0f, ry, 10.0f, it->label, xr_menuText );

		if ( it->type == XR_MENU_SLIDER ) {
			float v = Cvar_VariableValue( it->cvar );
			float f = ( it->max > it->min )
					  ? ( v - it->min ) / ( it->max - it->min ) : 0.0f;

			f = Com_Clamp( 0.0f, 1.0f, f );
			bar = 90.0f;

			XR_MenuFill( x + w - 200.0f, ry + 7.0f, bar, 6.0f,
						 xr_menuBarBg );
			XR_MenuFill( x + w - 200.0f, ry + 7.0f, bar * f, 6.0f,
						 xr_menuBar );

			Com_sprintf( value, sizeof( value ), "%.2f", v );
			XR_MenuText( x + w - 100.0f, ry, 10.0f, value, xr_menuText );
		} else {
			XR_MenuValueText( it, value, sizeof( value ) );
			valueColor = &xr_menuText;

			if ( it->type == XR_MENU_TOGGLE ) {
				valueColor = Cvar_VariableIntegerValue( it->cvar )
							 ? &xr_menuOn : &xr_menuOff;
			}

			XR_MenuText( x + w - 88.0f, ry, 10.0f, value, *valueColor );
		}
	}

	// Status. Read-only, and the numbers the tester would otherwise have to
	// pull a log for.
	XR_GetRenderedFov( &fovX, &fovY );

	if ( page == &xr_pageDebug ) {
		Com_sprintf( line, sizeof( line ),
					 "class %s  scale %.2f u/m  packets %d",
					 XR_CurrentClassName( ), XR_CurrentWorldScale( ),
					 XR_GetState( )->packetsReceived );
		XR_MenuText( x + 20.0f, y + h - 66.0f, 8.0f, line, xr_menuDimText );

		Com_sprintf( line, sizeof( line ),
					 "rendered FOV %.1f x %.1f   sync %.0f",
					 fovX, fovY, XR_GetState( )->sync );
		XR_MenuText( x + 20.0f, y + h - 54.0f, 8.0f, line, xr_menuDimText );
	} else if ( page == &xr_pageQuality ) {
		Com_sprintf( line, sizeof( line ),
					 "live FOV %.1f x %.1f   frame %d x %d",
					 fovX, fovY, cls.glconfig.vidWidth,
					 cls.glconfig.vidHeight );
		XR_MenuText( x + 20.0f, y + h - 66.0f, 8.0f, line, xr_menuDimText );

		Com_sprintf( line, sizeof( line ),
					 "class %s  derived scale %.2f u/m",
					 XR_CurrentClassName( ), XR_CurrentWorldScale( ) );
		XR_MenuText( x + 20.0f, y + h - 54.0f, 8.0f, line, xr_menuDimText );
	} else {
		Com_sprintf( line, sizeof( line ),
					 "class %s  scale %.2f u/m  packets %d",
					 XR_CurrentClassName( ), XR_CurrentWorldScale( ),
					 XR_GetState( )->packetsReceived );
		XR_MenuText( x + 20.0f, y + h - 66.0f, 8.0f, line, xr_menuDimText );

		XR_MenuText( x + 20.0f, y + h - 54.0f, 8.0f,
					 "Console-free settings. TRIGGER selects.",
					 xr_menuDimText );
	}

	XR_MenuText( x + 20.0f, y + h - 30.0f, 8.0f,
				 "Stick: move / adjust   TRIGGER: select   B: back",
				 xr_menuDimText );
	XR_MenuText( x + 20.0f, y + h - 18.0f, 8.0f,
				 "F1 or dominant stick click: close", xr_menuDimText );

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

	Cmd_AddCommand( "vr_menu", XR_MenuToggle_f );

	xr_menuOpen = qfalse;
	xr_menuPage = &xr_pageRoot;
	xr_menuIndex = 0;

	XR_Log( "menu: ready (vr_menu command, vr_menuButton %d)",
			Cvar_VariableIntegerValue( "vr_menuButton" ) );
}
