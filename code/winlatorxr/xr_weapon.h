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
// xr_weapon.h -- the hand-held gun (stage 8)

#ifndef __XR_WEAPON_H
#define __XR_WEAPON_H

#include "../qcommon/q_shared.h"
#include "../renderercommon/tr_types.h"

void        XR_WeaponInit( void );

// Is the hand-held gun active THIS frame? Humans only, alive, pose present.
// The aim, the camera compensation and the movement compensation must all
// agree about this, so they all ask the same function.
qboolean    XR_HandGunActive( void );

// Called from CG_R_ADDREFENTITYTOSCENE. Returns qtrue if the entity was
// taken over by us, in which case the caller must NOT pass it to the
// renderer -- we re-add our own copy at the hand.
qboolean    XR_CaptureViewWeapon( const refEntity_t *ent );

// Called from CG_R_RENDERSCENE with cgame's ORIGINAL refdef, before
// XR_AdjustRefdef touches it and before re.RenderScene.
void        XR_EmitHandWeapon( const refdef_t *cgameRefdef, float bodyYaw );

// The aim the server is given, and what the camera has to undo. Both in
// engine angles, body yaw NOT included.
qboolean    XR_GunAngles( vec3_t angles );

#endif // __XR_WEAPON_H

// Is the usercmd carrying the HAND's direction rather than the head's?
// True only while the fire control is held, and only for a living human
// with the hand gun on. The aim, the camera correction and the movement
// stick all ask this one function, so they cannot disagree.
qboolean    XR_GunAimActive( void );
