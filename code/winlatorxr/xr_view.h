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
// xr_view.h -- head pose to engine camera
//
// Split from xr_api.c on purpose: that file owns the wire, this one owns the
// geometry. The sync pixel (stage 5) is the one renderer-coupled piece and
// gets a third home, so a Vulkan renderer swap touches only that.

#ifndef __XR_VIEW_H
#define __XR_VIEW_H

#include "../qcommon/q_shared.h"
#include "../renderercommon/tr_types.h"

void        XR_ViewInit( void );

// Zero the yaw and the positional origin to wherever the head is now. Bound
// to a command because it gets used constantly.
void        XR_Recentre( void );

// Head orientation in the engine's frame, recentred, degrees.
// Returns qfalse when we have no pose, in which case angles is untouched.
qboolean    XR_HeadAngles( vec3_t angles );

// Hook 2. Head yaw and pitch are added to the angles that go into the
// usercmd, because the server has to agree about where the player is aiming.
// Yaw and pitch belong here and NOWHERE else -- applying them in the render
// hook as well is the classic double-rotation bug.
// The dominant hand's pose, in exactly the same convention as the head's:
// engine angles, recentred, body yaw NOT included; offset in engine units in
// the recentred room frame. hand is 0 = left, 1 = right.
qboolean    XR_HandBasis( int hand, vec3_t axis[3] );
qboolean    XR_HandAngles( int hand, vec3_t angles );
qboolean    XR_HandOffset( int hand, vec3_t offset );

void        XR_AdjustUsercmdAngles( vec3_t viewangles );

// Hook 1. Applies only what the server does not need to know: roll, composed
// onto the basis cgame already built, and the positional offset.
//
// bodyYaw is the player's own yaw (cl.viewangles[YAW]), passed in so this
// file does not need client.h. The head's lean is a displacement in the
// room, so it is rotated by the body's yaw rather than the view's -- the view
// already contains the head's yaw via the usercmd path.
void        XR_AdjustRefdef( refdef_t *rd, float bodyYaw );

// Logged on change. viewheight is per class in this game -- twelve classes,
// twelve bodies -- so it is the signal that D9's per-class world scale will
// key off, and the cheapest proxy for "the player changed class" that does
// not drag the game module's headers into the client.
void        XR_NoteViewheight( int viewheight );

#endif // __XR_VIEW_H
