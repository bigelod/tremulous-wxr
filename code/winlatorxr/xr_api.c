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
// xr_api.c -- WinlatorXR XrApi 0.5 client
//
// Reference: docs/general_wxr/XRAPI_V0_4_DESC.md
// Defects designed out rather than debugged: docs/ClaudeCode/BUG_FIXES.md
//
// Design notes, each of which is a deliberate choice and not an accident:
//
//  * No receive thread and no critical section. The socket is non-blocking
//    and drained from XR_Frame on the client thread. The inherited sketch
//    used a thread plus a CRITICAL_SECTION and three of its catalogued bugs
//    were consequences of that choice (BUG_FIXES part 1 item 10). A drain of
//    a handful of small datagrams costs nothing measurable.
//
//  * Floats are parsed with a locale-independent scanner. The engine does
//    not guarantee LC_NUMERIC is "C", and a comma decimal separator would
//    turn every pose into zero. BUG_FIXES part 3 records this as a real
//    concern whose claimed fix was never actually applied.
//
//  * The handshake files are rewritten while we are not receiving. The
//    runtime wipes its directory at startup, so a file written once, early,
//    may simply not be there any more.

#include "xr_api.h"
#include "../qcommon/qcommon.h"

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
   typedef int xr_socklen_t;
#  define XR_INVALID_SOCKET   INVALID_SOCKET
#  define XR_CLOSESOCKET(s)   closesocket(s)
   typedef SOCKET xr_socket_t;
#else
#  include <sys/types.h>
#  include <sys/stat.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <errno.h>
   typedef socklen_t xr_socklen_t;
   typedef int xr_socket_t;
#  define XR_INVALID_SOCKET   (-1)
#  define XR_CLOSESOCKET(s)   close(s)
#endif

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#define XR_PORT_IN          7872    // runtime -> game. 7873 is a duplicate
                                    // of this, not a fallback: bind one.
#define XR_PORT_OUT         7278    // game -> runtime

#define XR_PACKET_MAX       2048
#define XR_VERSION_STRING   "0.5"

// Rewrite the handshake this often while no packets are arriving.
#define XR_HANDSHAKE_RETRY_MS   2000
// Dump one raw packet this often under vr_verbose.
#define XR_RAWLOG_INTERVAL_MS   5000

static cvar_t   *vr_enable;
static cvar_t   *vr_verbose;
static cvar_t   *vr_modeVR;
static cvar_t   *vr_mode3D;
static cvar_t   *vr_fovX;
static cvar_t   *vr_fovY;

static xr_state_t   xr;

static xr_socket_t  xr_sock = XR_INVALID_SOCKET;
static qboolean     xr_initialised = qfalse;
static qboolean     xr_sockReady = qfalse;

static char         xr_dir[MAX_OSPATH];
static int          xr_lastHandshake;
static int          xr_lastRawLog;

// Length of the HMD quaternion as it arrived, before normalisation. Purely
// diagnostic, but it is the number that tells a tester capture apart from a
// device one -- real packets are unit to about 1e-4.
static float        xr_lastQuatLen;

static FILE        *xr_logFile;

// Headset information read from the runtime's `system` file.
static char         xr_hmdMake[64];
static char         xr_hmdModel[64];
static qboolean     xr_onDevice;

/*
==================
XR_LastSocketError
==================
*/
static int XR_LastSocketError( void )
{
#ifdef _WIN32
	return (int)WSAGetLastError();
#else
	return errno;
#endif
}

/*
==================
XR_TrimTrailing

Strip trailing whitespace in place. This tree has no Q_strstrip.
==================
*/
static void XR_TrimTrailing( char *s )
{
	size_t len = strlen( s );

	while ( len > 0 ) {
		char c = s[len - 1];

		if ( c != ' ' && c != '\t' && c != '\r' && c != '\n' ) {
			break;
		}
		s[--len] = '\0';
	}
}

/*
==================
XR_Log

vr.log, next to the executable, flushed every line. A crash that loses the
last line loses the one line that mattered.
==================
*/
void QDECL XR_Log( const char *fmt, ... )
{
	va_list argptr;

	if ( !xr_logFile ) {
		return;
	}

	va_start( argptr, fmt );
	vfprintf( xr_logFile, fmt, argptr );
	va_end( argptr );

	fputc( '\n', xr_logFile );
	fflush( xr_logFile );
}

/*
==================
XR_ParseFloat

Locale-independent. strtod() honours LC_NUMERIC, and if anything in the
process has set a locale whose decimal separator is a comma, every pose
silently becomes an integer truncation of itself.

Returns the number of characters consumed, 0 if the token did not scan.
==================
*/
static int XR_ParseFloat( const char *s, float *out )
{
	const char  *p = s;
	double      value = 0.0;
	double      sign = 1.0;
	int         digits = 0;

	if ( *p == '+' ) {
		p++;
	} else if ( *p == '-' ) {
		sign = -1.0;
		p++;
	}

	while ( *p >= '0' && *p <= '9' ) {
		value = value * 10.0 + (double)( *p - '0' );
		p++;
		digits++;
	}

	if ( *p == '.' ) {
		double scale = 0.1;
		p++;
		while ( *p >= '0' && *p <= '9' ) {
			value += (double)( *p - '0' ) * scale;
			scale *= 0.1;
			p++;
			digits++;
		}
	}

	if ( !digits ) {
		return 0;
	}

	if ( *p == 'e' || *p == 'E' ) {
		const char  *e = p + 1;
		int         expSign = 1;
		int         exp = 0;
		int         expDigits = 0;

		if ( *e == '+' ) {
			e++;
		} else if ( *e == '-' ) {
			expSign = -1;
			e++;
		}

		while ( *e >= '0' && *e <= '9' ) {
			exp = exp * 10 + ( *e - '0' );
			e++;
			expDigits++;
		}

		if ( expDigits ) {
			value *= pow( 10.0, (double)( expSign * exp ) );
			p = e;
		}
	}

	*out = (float)( sign * value );
	return (int)( p - s );
}

/*
==================
XR_QNormalise

Unconditionally, on ingest, on every quaternion.

The standard rotation shortcut is a rotation only when |q| == 1; for a
non-unit quaternion it also scales by |q|^2. Real headset packets are unit to
about 1e-4, but tester packets have been observed 1.4% to 10.8% long, which
stretches each hand's basis by a different amount and looks exactly like a
deep bug in the pose maths.
==================
*/
static void XR_QNormalise( vec4_t q )
{
	float n2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
	float inv;

	if ( n2 < 1e-12f ) {
		q[0] = q[1] = q[2] = 0.0f;
		q[3] = 1.0f;
		return;
	}

	inv = 1.0f / sqrtf( n2 );
	q[0] *= inv;
	q[1] *= inv;
	q[2] *= inv;
	q[3] *= inv;
}

/*
==================
XR_ResolveDir

Z: is the container's mapping of the Android side. If it is not there we are
on a desktop test box.
==================
*/
static void XR_ResolveDir( void )
{
#ifdef _WIN32
	if ( GetFileAttributesA( "Z:\\" ) != INVALID_FILE_ATTRIBUTES ) {
		Q_strncpyz( xr_dir, "Z:\\tmp\\xr", sizeof( xr_dir ) );
		xr_onDevice = qtrue;
		return;
	}
	Q_strncpyz( xr_dir, "D:\\xrtemp", sizeof( xr_dir ) );
#else
	Q_strncpyz( xr_dir, "/tmp/xr", sizeof( xr_dir ) );
#endif
	xr_onDevice = qfalse;
}

static void XR_MakeDir( const char *path )
{
#ifdef _WIN32
	CreateDirectoryA( path, NULL );
#else
	mkdir( path, 0777 );
#endif
}

static qboolean XR_WriteFile( const char *name, const char *contents )
{
	char    path[MAX_OSPATH];
	FILE    *f;

	Com_sprintf( path, sizeof( path ), "%s%c%s", xr_dir, PATH_SEP, name );

	f = fopen( path, "wb" );
	if ( !f ) {
		return qfalse;
	}

	fputs( contents, f );
	fclose( f );
	return qtrue;
}

static void XR_DeleteFile( const char *name )
{
	char    path[MAX_OSPATH];

	Com_sprintf( path, sizeof( path ), "%s%c%s", xr_dir, PATH_SEP, name );
	remove( path );
}

/*
==================
XR_WriteHandshake

`version` decides everything, is read once, and an unrecognised value means
the runtime never starts a listener and never sends a packet. If nothing
arrives at all, suspect this file before suspecting the socket.

The runtime wipes this directory when it starts, so we rewrite until we are
actually receiving rather than assuming our first write survived.
==================
*/
static void XR_WriteHandshake( void )
{
	XR_MakeDir( xr_dir );

	if ( !XR_WriteFile( "version", XR_VERSION_STRING ) ) {
		XR_Log( "handshake: FAILED to write %s%cversion", xr_dir, PATH_SEP );
		return;
	}

	if ( !XR_WriteFile( "vr", "VR" ) ) {
		XR_Log( "handshake: FAILED to write %s%cvr", xr_dir, PATH_SEP );
	}
}

/*
==================
XR_ReadSystem

The runtime writes seven lines. Older clients read two and stop, which is why
this was once described as "make and model" -- line 2 is a product codename,
not a model name. Default rather than fail when it is missing.
==================
*/
static void XR_ReadSystem( void )
{
	char    path[MAX_OSPATH];
	char    line[7][128];
	FILE    *f;
	int     i, n = 0;

	Q_strncpyz( xr_hmdMake, "META", sizeof( xr_hmdMake ) );
	Q_strncpyz( xr_hmdModel, "QUEST 3", sizeof( xr_hmdModel ) );

	Com_sprintf( path, sizeof( path ), "%s%csystem", xr_dir, PATH_SEP );

	f = fopen( path, "rb" );
	if ( !f ) {
		XR_Log( "system: absent at %s -- assuming desktop, defaulting to %s / %s",
				path, xr_hmdMake, xr_hmdModel );
		return;
	}

	for ( i = 0; i < 7; i++ ) {
		if ( !fgets( line[i], sizeof( line[i] ), f ) ) {
			break;
		}
		XR_TrimTrailing( line[i] );
		n++;
	}
	fclose( f );

	XR_Log( "system: %d lines", n );
	for ( i = 0; i < n; i++ ) {
		XR_Log( "system[%d]: %s", i + 1, line[i] );
	}

	// Line 2 is Build.PRODUCT -- a product codename, not a model name. The
	// mapping below is HWXR's, quoted in XRAPI_V0_4_DESC.md section 2, and is
	// reproduced rather than invented: it is the behaviour of a port that has
	// actually shipped on this hardware.
	//
	// It is not only cosmetic. HWXR's comments on the fall-through cases
	// record that the hands are upside down on SEACLIFF, HOLLYWOOD and
	// MONTEREY, so this string is what a hand-orientation decision hangs off
	// in stage 8. Keep it accurate even while nothing reads it.

	if ( n >= 1 && line[0][0] ) {
		Q_strncpyz( xr_hmdMake, line[0], sizeof( xr_hmdMake ) );
	}

	if ( !Q_stricmp( xr_hmdMake, "OCULUS" ) ) {
		Q_strncpyz( xr_hmdMake, "META", sizeof( xr_hmdMake ) );
	}

	if ( n < 2 || !line[1][0] ) {
		// Absent: keep the "QUEST 3" default and do not fall through to the
		// catch-all below, which is what HWXR's if/else chain does.
		Q_strncpyz( xr_hmdModel, "QUEST 3", sizeof( xr_hmdModel ) );
	} else if ( !Q_stricmp( line[1], "EUREKA" )
				|| !Q_stricmp( line[1], "PANTHER" ) ) {
		Q_strncpyz( xr_hmdModel, "QUEST 3", sizeof( xr_hmdModel ) );
	} else if ( !Q_stricmp( xr_hmdMake, "META" ) ) {
		// Catch-all for every other Meta codename:
		//   SEACLIFF  - Quest Pro - untested, assume hands upside down
		//   HOLLYWOOD - Quest 2   - works, hands upside down
		//   MONTEREY  - Quest 1   - untested, unsupported
		// Note this also swallows any codename newer than HWXR knows about,
		// so a future headset reports as a Quest 2. That is HWXR's behaviour
		// and we keep it, but log the raw codename so the log says which
		// device it really was.
		XR_Log( "system: codename \"%s\" unrecognised, treating as QUEST 2 "
				"(HWXR fall-through)", line[1] );
		Q_strncpyz( xr_hmdModel, "QUEST 2", sizeof( xr_hmdModel ) );
	} else {
		// Not a Meta headset at all -- a Pico, for instance. Pass the
		// codename through rather than mislabelling it.
		Q_strncpyz( xr_hmdModel, line[1], sizeof( xr_hmdModel ) );
	}

	if ( n >= 7 && strstr( line[6], "BINARY_UDP" ) ) {
		XR_Log( "system: runtime advertises BINARY_UDP (it understands 0.7); "
				"we are speaking " XR_VERSION_STRING " deliberately" );
	}
}

/*
==================
XR_OpenSocket

With a protocol whose characteristic failure is total silence, an unchecked
bind is a diagnostic dead end. Check both, and say which one failed.
==================
*/
static qboolean XR_OpenSocket( void )
{
	struct sockaddr_in  addr;
#ifdef _WIN32
	u_long              nonblocking = 1;
#endif

	xr_sock = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if ( xr_sock == XR_INVALID_SOCKET ) {
		XR_Log( "socket: FAILED to create UDP socket (error %d)",
				XR_LastSocketError() );
		return qfalse;
	}

	memset( &addr, 0, sizeof( addr ) );
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
	addr.sin_port = htons( XR_PORT_IN );

	if ( bind( xr_sock, (struct sockaddr *)&addr, sizeof( addr ) ) != 0 ) {
		XR_Log( "socket: FAILED to bind 127.0.0.1:%d (error %d) -- "
				"another XrApi client is probably already running",
				XR_PORT_IN, XR_LastSocketError() );
		XR_CLOSESOCKET( xr_sock );
		xr_sock = XR_INVALID_SOCKET;
		return qfalse;
	}

#ifdef _WIN32
	ioctlsocket( xr_sock, FIONBIO, &nonblocking );
#else
	fcntl( xr_sock, F_SETFL, fcntl( xr_sock, F_GETFL, 0 ) | O_NONBLOCK );
#endif

	XR_Log( "socket: bound 127.0.0.1:%d, non-blocking", XR_PORT_IN );
	return qtrue;
}

/*
==================
XR_ParsePacket

    client <28 floats> <frame id> <bool string> <HMD name>

The token is a prefix ("client0"), so match it loosely. Ignore trailing
tokens we do not recognise -- the format gains fields at the end between
versions, and a strict parser rejects a packet it could have used.

Nothing past index 28 is a float. The inherited sketch invented an altitude
and two grip quaternions at indices 29-37; no such fields exist before 0.6,
which is a different packet shape entirely. See BUG_FIXES part 1 item 1.
==================
*/
static qboolean XR_ParsePacket( const char *buf )
{
	float       f[XR_NUM_FLOATS];
	const char  *p = buf;
	char        bools[XR_MAX_BOOLS + 1];
	int         numBools = 0;
	int         i;

	while ( *p == ' ' || *p == '\t' ) {
		p++;
	}

	if ( Q_strncmp( p, "client", 6 ) != 0 ) {
		return qfalse;
	}

	while ( *p && *p != ' ' && *p != '\t' ) {
		p++;
	}

	for ( i = 0; i < XR_NUM_FLOATS; i++ ) {
		int consumed;

		while ( *p == ' ' || *p == '\t' ) {
			p++;
		}

		consumed = XR_ParseFloat( p, &f[i] );
		if ( !consumed ) {
			return qfalse;
		}
		p += consumed;
	}

	// Collect the run of T/F characters, ignoring whitespace between tokens.
	// On the wire this is a 19-character button string, a space, then the two
	// flag characters -- so indices 19 and 20 are IS_IMMERSIVE and IS_SBS.
	for ( ;; ) {
		while ( *p == ' ' || *p == '\t' ) {
			p++;
		}

		if ( *p != 'T' && *p != 'F' ) {
			break;
		}

		// Only absorb this token if it is entirely T/F; otherwise it is an
		// HMD name that happens to start with one of those letters.
		{
			const char *q = p;
			while ( *q == 'T' || *q == 'F' ) {
				q++;
			}
			if ( *q && *q != ' ' && *q != '\t' && *q != '\r' && *q != '\n' ) {
				break;
			}
			while ( p < q ) {
				if ( numBools < XR_MAX_BOOLS ) {
					bools[numBools++] = *p;
				}
				p++;
			}
		}
	}
	bools[numBools] = '\0';

	// Whatever is left is the HMD name, if anything.
	while ( *p == ' ' || *p == '\t' ) {
		p++;
	}

	// Commit. Up to here we have touched nothing the rest of the engine can
	// see, so a malformed packet leaves the last good pose intact.
	for ( i = 0; i < 2; i++ ) {
		int base = i * 9;

		xr.handQuat[i][0] = f[base + 0];
		xr.handQuat[i][1] = f[base + 1];
		xr.handQuat[i][2] = f[base + 2];
		xr.handQuat[i][3] = f[base + 3];
		XR_QNormalise( xr.handQuat[i] );

		xr.stick[i][0] = f[base + 4];
		xr.stick[i][1] = f[base + 5];

		xr.handPos[i][0] = f[base + 6];
		xr.handPos[i][1] = f[base + 7];
		xr.handPos[i][2] = f[base + 8];
	}

	xr.hmdQuat[0] = f[18];
	xr.hmdQuat[1] = f[19];
	xr.hmdQuat[2] = f[20];
	xr.hmdQuat[3] = f[21];
	xr_lastQuatLen = sqrtf( f[18] * f[18] + f[19] * f[19]
							+ f[20] * f[20] + f[21] * f[21] );
	XR_QNormalise( xr.hmdQuat );

	xr.hmdPos[0] = f[22];
	xr.hmdPos[1] = f[23];
	xr.hmdPos[2] = f[24];

	xr.ipd  = f[25];
	xr.fovX = f[26];
	xr.fovY = f[27];
	xr.sync = f[28];

	Q_strncpyz( xr.bools, bools, sizeof( xr.bools ) );
	xr.numBools = numBools;

	if ( *p ) {
		Q_strncpyz( xr.hmdName, p, sizeof( xr.hmdName ) );
		XR_TrimTrailing( xr.hmdName );
	}

	xr.valid = qtrue;
	xr.packetsReceived++;
	xr.lastPacketTime = Sys_Milliseconds();

	return qtrue;
}

/*
==================
XR_Receive

Drain every queued datagram and keep the newest. Reading one per frame builds
a queue and acts on steadily staler poses.
==================
*/
static void XR_Receive( void )
{
	char    buf[XR_PACKET_MAX];
	char    newest[XR_PACKET_MAX];
	int     drained = 0;
	int     ret;

	newest[0] = '\0';

	for ( ;; ) {
		ret = recv( xr_sock, buf, sizeof( buf ) - 1, 0 );
		if ( ret <= 0 ) {
			break;
		}
		buf[ret] = '\0';
		memcpy( newest, buf, ret + 1 );
		drained++;
	}

	if ( !drained ) {
		return;     // keep the last good pose; never zero
	}

	if ( !XR_ParsePacket( newest ) ) {
		XR_Log( "parse: rejected packet: %s", newest );
		return;
	}

	if ( vr_verbose->integer ) {
		int now = Sys_Milliseconds();

		if ( now - xr_lastRawLog >= XR_RAWLOG_INTERVAL_MS ) {
			xr_lastRawLog = now;
			// Verbatim, so a device capture can be diffed against a tester
			// capture by eye and pasted straight back into the tester.
			XR_Log( "raw: %s", newest );
			if ( drained > 1 ) {
				XR_Log( "raw: (drained %d queued datagrams this frame)",
						drained );
			}

			// The raw line above says what arrived; this one says what we
			// actually hold after ingest. Logging |q| before normalising is
			// the only way to see that normalisation is doing anything --
			// otherwise a client that silently skipped it looks identical
			// here, and the symptom only shows up as asymmetric hand bases
			// much later.
			XR_Log( "ingest: hmd q=[%.4f %.4f %.4f %.4f] |q|in=%.4f "
					"pos=[%.3f %.3f %.3f] ipd=%.4f sync=%.0f",
					xr.hmdQuat[0], xr.hmdQuat[1], xr.hmdQuat[2], xr.hmdQuat[3],
					xr_lastQuatLen,
					xr.hmdPos[0], xr.hmdPos[1], xr.hmdPos[2],
					xr.ipd, xr.sync );
			XR_Log( "ingest: bools=\"%s\" (%d) immersive=%d sbs=%d hmd=\"%s\"",
					xr.bools, xr.numBools,
					XR_Button( XR_BOOL_IMMERSIVE ),
					XR_Button( XR_BOOL_SBS ),
					xr.hmdName );
		}
	}
}

/*
==================
XR_SendOut

    <L haptic> <R haptic> <MODE_VR> <MODE_3D> <FOV x> <FOV y>

The FOV pair is in degrees and applied only if greater than 1. Sending zero,
or a tangent of a half-angle, means the runtime silently keeps its own
default and the port appears to work for the wrong reason. 104.5 is what both
opentrackWXR and HWXR ship. See BUG_FIXES part 1 item 3.

Both mode fields are parameters rather than constants: MODE_VR 0 is how menus
are made usable, and a hardcoded 1 makes that impossible. Keep sending every
frame even while the mode is 0, or the stream can never be turned back on.
==================
*/
static void XR_SendOut( void )
{
	struct sockaddr_in  addr;
	char                msg[128];
	xr_socket_t         s;

	s = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if ( s == XR_INVALID_SOCKET ) {
		return;
	}

	memset( &addr, 0, sizeof( addr ) );
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
	addr.sin_port = htons( XR_PORT_OUT );

	// Haptics stay at 0 for now. Note that the runtime stores field 0 and 1
	// only when greater than zero, so a 0 here leaves the previous haptic
	// level in place rather than clearing it -- that is the documented
	// behaviour, not a bug to work around.
	Com_sprintf( msg, sizeof( msg ), "0 0 %d %d %.1f %.1f",
				 vr_modeVR->integer,
				 vr_mode3D->integer,
				 vr_fovX->value,
				 vr_fovY->value );

	sendto( s, msg, (int)strlen( msg ), 0,
			(struct sockaddr *)&addr, sizeof( addr ) );

	XR_CLOSESOCKET( s );
}

/*
==================
XR_Init
==================
*/
void XR_Init( void )
{
	vr_enable  = Cvar_Get( "vr_enable",  "0", CVAR_ARCHIVE | CVAR_LATCH );
	vr_verbose = Cvar_Get( "vr_verbose", "0", CVAR_ARCHIVE );

	vr_modeVR  = Cvar_Get( "vr_modeVR",  "1", CVAR_ARCHIVE );
	vr_mode3D  = Cvar_Get( "vr_mode3D",  "0", CVAR_ARCHIVE );

	// Degrees. Tuning values, not a unit conversion to get right -- send a
	// pair, look through the headset, adjust. Archived, so they can be
	// changed on the device through q3config.cfg or the console without a
	// rebuild, which is what HWXR's flag file exists to achieve.
	vr_fovX    = Cvar_Get( "vr_fovX",    "104.5", CVAR_ARCHIVE );
	vr_fovY    = Cvar_Get( "vr_fovY",    "104.5", CVAR_ARCHIVE );

	if ( !vr_enable->integer ) {
		return;
	}

	xr_logFile = fopen( "vr.log", "w" );
	if ( !xr_logFile ) {
		Com_Printf( "WARNING: XR: could not open vr.log -- "
					"there is no other channel back, so this is serious\n" );
	}

	// Line 1, always. "Am I running the binary I just built?" is answered
	// permanently, from the start.
	XR_Log( "Tremulous WXR -- built " __DATE__ " " __TIME__ " -- XrApi "
			XR_VERSION_STRING );
	XR_Log( "engine: %s", Q3_VERSION );

	memset( &xr, 0, sizeof( xr ) );
	xr.hmdQuat[3] = 1.0f;
	xr.handQuat[0][3] = 1.0f;
	xr.handQuat[1][3] = 1.0f;

	XR_ResolveDir();
	XR_Log( "handshake dir: %s (%s)", xr_dir,
			xr_onDevice ? "device" : "desktop fallback" );

	XR_WriteHandshake();
	XR_ReadSystem();

	XR_Log( "hmd: %s / %s", xr_hmdMake, xr_hmdModel );

	xr_sockReady = XR_OpenSocket();
	xr_lastHandshake = Sys_Milliseconds();

	xr_initialised = qtrue;

	Com_Printf( "XR: XrApi " XR_VERSION_STRING " client started, logging to vr.log\n" );
}

/*
==================
XR_Frame
==================
*/
void XR_Frame( void )
{
	int now;

	if ( !xr_initialised ) {
		return;
	}

	if ( xr_sockReady ) {
		XR_Receive();
	}

	XR_SendOut();

	// The runtime wipes its directory when it starts. If we have never heard
	// from it, our handshake may simply have been deleted -- rewrite it
	// rather than waiting forever for a reply to a file that is gone.
	now = Sys_Milliseconds();
	if ( !xr.valid && now - xr_lastHandshake >= XR_HANDSHAKE_RETRY_MS ) {
		xr_lastHandshake = now;
		XR_WriteHandshake();

		if ( vr_verbose->integer ) {
			XR_Log( "handshake: rewritten (no packet received yet)" );
		}
	}
}

/*
==================
XR_Shutdown

Delete `vr`. Leaving it behind can convince the runtime that a dead process
still wants VR.
==================
*/
void XR_Shutdown( void )
{
	if ( !xr_initialised ) {
		return;
	}

	if ( xr_sock != XR_INVALID_SOCKET ) {
		XR_CLOSESOCKET( xr_sock );
		xr_sock = XR_INVALID_SOCKET;
	}
	xr_sockReady = qfalse;

	XR_DeleteFile( "vr" );
	XR_DeleteFile( "version" );

	XR_Log( "shutdown: %d packets received", xr.packetsReceived );

	if ( xr_logFile ) {
		fclose( xr_logFile );
		xr_logFile = NULL;
	}

	xr_initialised = qfalse;
}

const xr_state_t *XR_GetState( void )
{
	return &xr;
}

qboolean XR_IsEnabled( void )
{
	return ( xr_initialised && vr_enable->integer ) ? qtrue : qfalse;
}

qboolean XR_Button( int index )
{
	if ( index < 0 || index >= xr.numBools ) {
		return qfalse;
	}
	return ( xr.bools[index] == 'T' ) ? qtrue : qfalse;
}
