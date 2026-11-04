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
// xr_input.h -- controller input (stage 7)
//
// Two mechanisms, because the controls divide cleanly in two:
//
//   * Digital controls become SYNTHESISED KEY EVENTS, so Tremulous's own
//     binds, commands and menus do the work. See xr_input.c for why that
//     beats writing usercmd bits directly.
//
//   * The sticks are ANALOG and cannot be a key event, so they go straight
//     into the usercmd and into cl.viewangles.

#ifndef __XR_INPUT_H
#define __XR_INPUT_H

#include "../qcommon/q_shared.h"

void    XR_InputInit( void );

// Edge-detects every mapped button and queues key events. Call once per
// client frame, after the pose has been ingested.
void    XR_InputFrame( void );

// Off-hand stick -> forwardmove / rightmove. Call from CL_CreateCmd AFTER
// the keyboard, mouse and joystick contributions, because it reads back the
// BUTTON_WALKING the engine has already decided.
void    XR_StickMove( usercmd_t *cmd );

// Dominant stick: X turns the body, Y changes weapon. Yaw accumulates into
// cl.viewangles exactly as mouse turn does, so it is body yaw, not head yaw.
void    XR_StickLook( int msec );

// Drop every held synthetic key. Called on leaving play, so a trigger held
// as a menu opens cannot stick down.
void    XR_InputReleaseAll( void );

#endif // __XR_INPUT_H

// Is the fire control held? The gun-aim scheme hangs off this: shots go
// where the dominant hand points, and only while the trigger is down.
qboolean    XR_FireHeld( void );
