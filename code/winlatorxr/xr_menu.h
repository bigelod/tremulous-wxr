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
// xr_menu.h -- the engine-side VR settings menu
//
// NOT a Tremulous UI menu. ui.qvm is replaced by a pure server at connect
// time, so anything added there does not exist on the servers the game is
// played on. This is drawn and driven by the client, which survives purity,
// and it only sets cvars -- the engine's own settings mechanism.

#ifndef __XR_MENU_H
#define __XR_MENU_H

#include "../qcommon/q_shared.h"

void        XR_MenuInit( void );

// True while the overlay is up. Input and the usercmd are gated on this so a
// menu cannot fire the weapon or move the player.
qboolean    XR_MenuIsOpen( void );

// Controller navigation. Returns qtrue if this frame's input belonged to the
// menu, including the frame it closes on, so a held button cannot fall
// through into the game on the same frame.
qboolean    XR_MenuFrame( void );

// 2D overlay. Drawn after the world and HUD, immediately before the sync
// pixel, which must remain the last thing drawn.
void        XR_MenuDraw( void );

// Desktop keys. Returns qtrue when the key was consumed by the menu. Called
// from CL_KeyEvent so arrows/enter/escape do not reach the game or the QVM UI.
qboolean    XR_MenuKeyEvent( int key, qboolean down );

// Console command `vr_menu`, and the controller chord-free toggle.
void        XR_MenuToggle( void );

#endif // __XR_MENU_H
