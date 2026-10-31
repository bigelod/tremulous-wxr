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
// xr_api.h -- WinlatorXR XrApi 0.5 client
//
// Two UDP sockets on localhost carrying whitespace-separated ASCII, plus a
// handshake done through files. See docs/general_wxr/XRAPI_V0_4_DESC.md.
//
// This lives in the client executable, not in a renderer DLL and not in a
// QVM module. See docs/ClaudeCode/BUG_FIXES.md items 6 and 7 for why.

#ifndef __XR_API_H
#define __XR_API_H

#include "../qcommon/q_shared.h"

// Inbound float layout, indexed from 0. Index 28 is the frame id, so there
// are 29 numeric tokens on the wire even though the packet is documented as
// "28 floats plus a frame id".
#define XR_NUM_FLOATS       29

// GUEST_BUTTON_COUNT in the runtime is 19. From 0.3 two flag characters
// (immersive, SBS) follow as a separate token, so a parser that ignores
// whitespace sees 21. We accept either and bounds-check every index.
#define XR_MAX_BOOLS        32

#define XR_BOOL_IMMERSIVE   19
#define XR_BOOL_SBS         20

// MODE_VR
#define XR_MODE_VR_OFF      0   // runtime sends no UDP data at all
#define XR_MODE_VR_VR       1
#define XR_MODE_VR_SCREEN   2   // flat window, no automatic head tracking
#define XR_MODE_VR_SCREEN_T 3   // flat window, head tracking mapped to mouse

// MODE_3D
#define XR_MODE_3D_MONO     0
#define XR_MODE_3D_SBS      1
#define XR_MODE_3D_AER      2
#define XR_MODE_3D_USER     (-1)

typedef struct {
	// Orientation as delivered, normalised on ingest. OpenXR convention:
	// right-handed, +Y up, -Z forward, metres. Not yet converted to the
	// engine's frame -- that is stage 6's job, and doing it here would
	// scatter the conversion.
	vec4_t      hmdQuat;
	vec3_t      hmdPos;

	vec4_t      handQuat[2];        // 0 = left, 1 = right
	vec3_t      handPos[2];
	vec2_t      stick[2];

	float       ipd;
	float       fovX;               // tangent of half-angle, as received
	float       fovY;

	// HMD_SYNC. Echoed back as red in the top-left corner so the runtime can
	// correlate the captured image with the pose it was rendered for.
	float       sync;

	char        bools[XR_MAX_BOOLS + 1];
	int         numBools;

	char        hmdName[64];

	// Housekeeping
	qboolean    valid;              // we have received at least one packet
	int         lastPacketTime;      // Sys_Milliseconds of the newest packet
	int         packetsReceived;
} xr_state_t;

void            XR_Init( void );
void            XR_Frame( void );
void            XR_Shutdown( void );

// The newest pose we have. Never zeroed on a dropout -- callers get the last
// good value, because one zeroed frame is a violent camera jolt.
const xr_state_t *XR_GetState( void );

qboolean        XR_IsEnabled( void );

// Button helper. Bounds-checked, so an index past the end of a short bool
// string reads as not-pressed rather than running off the buffer.
qboolean        XR_Button( int index );

// vr.log. Build stamp is on line 1. On a standalone device with no debugger
// and a manual copy loop, this is the only channel back.
void   QDECL    XR_Log( const char *fmt, ... ) Q_PRINTF_FUNC(1, 2);

#endif // __XR_API_H
