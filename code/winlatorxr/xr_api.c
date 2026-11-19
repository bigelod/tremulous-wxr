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
#include "xr_view.h"
#include "xr_sync.h"
#include "xr_input.h"
#include "xr_weapon.h"
#include "xr_hud.h"
#include "xr_menu.h"
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
// Treat the stream as stopped after this long with nothing arriving.
#define XR_STREAM_TIMEOUT_MS    1000
// Retry a failed bind this often rather than dying silently for the session.
#define XR_SOCKET_RETRY_MS      2000

static cvar_t   *vr_enable;
static cvar_t   *vr_verbose;
static cvar_t   *vr_modeVR;
static cvar_t   *vr_mode3D;
static cvar_t   *vr_autoMode;
static cvar_t   *vr_fovX;
static cvar_t   *vr_fovY;

static xr_state_t   xr;

static xr_socket_t  xr_sock = XR_INVALID_SOCKET;
static qboolean     xr_initialised = qfalse;
static qboolean     xr_sockReady;
static int          xr_sockRetryAt;
#ifdef _WIN32
static qboolean     xr_wsaStarted;
#endif

static char         xr_dir[MAX_OSPATH];
static int          xr_lastHandshake;
static int          xr_lastRawLog;

// Length of the HMD quaternion as it arrived, before normalisation. Purely
// diagnostic, but it is the number that tells a tester capture apart from a
// device one -- real packets are unit to about 1e-4.
static float        xr_lastQuatLen;

// Transition tracking. The plan is explicit that the log should carry state
// CHANGES rather than per-frame state -- a log the owner has to scroll is a
// log that gets skimmed.
static qboolean     xr_streaming;           // are packets currently arriving?
static int          xr_streamStoppedAt;
static char         xr_lastBools[XR_MAX_BOOLS + 1];
static char         xr_lastOutbound[128];

// Is the player actually playing, rather than in a menu / connecting /
// loading? Fed from the client each frame.
static qboolean     xr_inGame;

static FILE        *xr_logFile;

#define XR_MAX_PENDING_LOG  64
static char         xr_pending[XR_MAX_PENDING_LOG][1024];
static int          xr_pendingLines;

// Headset information read from the runtime's `system` file.
static char         xr_hmdMake[64];
static float        xr_renderFovX;    // degrees, from the live refdef
static float        xr_renderFovY;
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

vr.log, in fs_basepath (the executable's own directory unless overridden),
flushed every line. A crash that loses the last line loses the one line that
mattered. XR_Init prints the resolved path to the engine log.
==================
*/
void QDECL XR_Log( const char *fmt, ... )
{
	va_list argptr;
	char    line[1024];

	va_start( argptr, fmt );
	Q_vsnprintf( line, sizeof( line ), fmt, argptr );
	va_end( argptr );

	// BEFORE THE FILE EXISTS, HOLD THE LINE. Do not drop it.
	//
	// The cvars, the trim migration and handsfix.txt are all set up in
	// XR_Init BEFORE the log is opened, deliberately -- they have to exist
	// even with VR off. Every line they logged was therefore thrown away,
	// so handsfix.txt announced reading a file into a log that never got
	// the message, and a one-time settings repair happened invisibly. A
	// diagnostic that only starts working after the interesting part is
	// over is not a diagnostic.
	if ( !xr_logFile ) {
		if ( xr_pendingLines < XR_MAX_PENDING_LOG ) {
			Q_strncpyz( xr_pending[xr_pendingLines], line,
						sizeof( xr_pending[0] ) );
		}
		xr_pendingLines++;      // keeps counting, so the flush can say so
		return;
	}

	fprintf( xr_logFile, "%s\n", line );
	fflush( xr_logFile );
}

/*
==================
XR_FlushPendingLog

Emptied the moment the file is open, so startup reads in order.
==================
*/
static void XR_FlushPendingLog( void )
{
	int     i, n = xr_pendingLines;

	xr_pendingLines = 0;        // before logging anything, or this recurses

	if ( n > XR_MAX_PENDING_LOG ) {
		XR_Log( "log: %d lines were held before the file opened, %d kept",
				n, XR_MAX_PENDING_LOG );
		n = XR_MAX_PENDING_LOG;
	}

	for ( i = 0; i < n; i++ ) {
		XR_Log( "%s", xr_pending[i] );
	}
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
XR_NoteFov

The field of view the engine is actually rendering with, taken from cgame's
refdef every frame. See the outbound packet for why this is not a setting.
==================
*/
void XR_NoteFov( float fovX, float fovY )
{
	xr_renderFovX = fovX;
	xr_renderFovY = fovY;
}

/*
==================
XR_GetRenderedFov

Read-only, for the VR menu's status line. Zero means "no frame yet", which
is when the outbound packet falls back to vr_fovX / vr_fovY.
==================
*/
void XR_GetRenderedFov( float *fovX, float *fovY )
{
	if ( fovX ) {
		*fovX = xr_renderFovX;
	}
	if ( fovY ) {
		*fovY = xr_renderFovY;
	}
}

/*
==================
XR_HmdMake / XR_HmdModel
==================
*/
const char *XR_HmdMake( void )
{
	return xr_hmdMake;
}

const char *XR_HmdModel( void )
{
	return xr_hmdModel;
}

/*
==================
XR_OpenSocket

With a protocol whose characteristic failure is total silence, an unchecked
bind is a diagnostic dead end. Check both, and say which one failed.
==================
*/
/*
==================
XR_InitWinsock

**[device - Quest 3, 1 Nov 2026]** Without this, socket() fails with 10093,
WSANOTINITIALISED, and the whole inbound stream never exists.

The engine does call WSAStartup -- in NET_Init -- but sys_main.c calls
NET_Init AFTER Com_Init, and Com_Init is what calls CL_Init and therefore
XR_Init. So our socket is always created before the engine initialises
Winsock. On a desktop this happened to work because something else in the
process (SDL or wininet) had already started Winsock; inside the container
nothing had, and the failure was total and silent apart from this one logged
line.

WSAStartup is reference counted per process, so calling it in addition to the
engine's call is safe provided we WSACleanup exactly once.
==================
*/
static void XR_InitWinsock( void )
{
#ifdef _WIN32
	WSADATA wsaData;
	int     err;

	if ( xr_wsaStarted ) {
		return;
	}

	err = WSAStartup( MAKEWORD( 2, 2 ), &wsaData );
	if ( err != 0 ) {
		XR_Log( "winsock: WSAStartup FAILED (%d) -- no UDP is possible", err );
		return;
	}

	xr_wsaStarted = qtrue;
	XR_Log( "winsock: WSAStartup ok (we call this ourselves because "
			"NET_Init runs after CL_Init)" );
#endif
}

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

	// The bool string ON EVERY CHANGE, not on a timer. This is what makes
	// the button table buildable: the owner presses each control in turn and
	// reads off which index moved. Sampling it every few seconds instead
	// would miss most presses and make the exercise worthless.
	if ( vr_verbose->integer && strcmp( bools, xr_lastBools ) != 0 ) {
		Q_strncpyz( xr_lastBools, bools, sizeof( xr_lastBools ) );
		XR_Log( "bools: %s", bools );
	}

	if ( !xr_streaming ) {
		xr_streaming = qtrue;
		if ( xr.packetsReceived > 0 ) {
			XR_Log( "packets: RESUMED after %d ms",
					Sys_Milliseconds() - xr_streamStoppedAt );
		} else {
			XR_Log( "packets: started (first pose from \"%s\")", xr.hmdName );
		}
	}

	xr.valid = qtrue;
	xr.packetsReceived++;
	xr.lastPacketTime = Sys_Milliseconds();

	// Recentre on the very first pose, so the player starts facing their own
	// forward instead of wherever the runtime's origin happens to point.
	// `vr_recentre` is still bound for every time after that.
	//
	// AlphaTest10 briefly held this back until the head was half a metre
	// off the floor, on the theory that a first packet reporting a head at
	// y = 0.006 could not be a real measurement. IT IS A REAL MEASUREMENT.
	// XrApi positions are HEAD-CENTRED, not floor-relative: the tester
	// capture in SamplePoseData.txt has the head at y = 0.006 and both
	// hands at y = -0.23, a palm's drop below it. There is no floor in this
	// coordinate system to be half a metre above.
	//
	// The gate therefore never passed on hardware, xr_centred stayed false,
	// XR_HandOffset refused, and the gun was drawn with no hand offset at
	// all -- "stuck to my forehead", with the dominant-hand setting
	// apparently doing nothing because both hands resolved to the same
	// place. One wrong assumption about a coordinate system, two symptoms
	// that look unrelated.
	if ( xr.packetsReceived == 1 ) {
		XR_Recentre( );
	}

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
	float       fovX, fovY;
	int                 modeVR;

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
	// MODE_VR follows the game state.
	//
	// **[device - Quest 3, 1 Nov 2026]** Holding MODE_VR 1 from startup makes
	// the game unusable before you are in it: the runtime takes the mouse and
	// presents immersively, so the menus cannot be clicked and do not fit in
	// view. Tremulous's menus are a 2D layout built for a monitor, and the
	// alternative to this is rebuilding all of them for VR.
	//
	// MODE_VR 0 hands the mouse back and drops the frame to a flat panel,
	// which is exactly what a menu wants. The cost is that the runtime stops
	// sending poses entirely -- hence holding the last good pose across the
	// gap, and hence still sending this packet every frame, because the mode
	// is only ever re-read from a packet we send.
	modeVR = vr_modeVR->integer;

	if ( vr_autoMode->integer && !xr_inGame ) {
		modeVR = XR_MODE_VR_OFF;
	}

	// THE FOV WE SEND HAS TO BE THE FOV WE DREW WITH.
	//
	// This field tells the runtime how wide the image it is about to
	// composite actually is. Claim 104.5 while the engine renders Tremulous'
	// 90 and the runtime maps the frame onto the display at the wrong scale:
	// the world then moves at a different rate from the head, which does not
	// read as "wrong FOV" from inside a headset. It reads as the view
	// sliding, as rotations that do not come back to where they started --
	// as drift.
	//
	// So the live value from the refdef wins, and the cvars are the fallback
	// for before the first frame is drawn. XR_NoteFov supplies it.
	fovX = ( xr_renderFovX > 0.0f ) ? xr_renderFovX : vr_fovX->value;
	fovY = ( xr_renderFovY > 0.0f ) ? xr_renderFovY : vr_fovY->value;

	if ( vr_verbose->integer && xr_renderFovX > 0.0f ) {
		static float    loggedX, loggedY;

		if ( fabs( fovX - loggedX ) > 0.5f || fabs( fovY - loggedY ) > 0.5f ) {
			loggedX = fovX;
			loggedY = fovY;
			XR_Log( "fov: sending %.1f x %.1f (what we RENDER) -- runtime "
					"offered %.1f x %.1f, cvars hold %.1f x %.1f",
					fovX, fovY, xr.fovX, xr.fovY,
					vr_fovX->value, vr_fovY->value );
		}
	}

	Com_sprintf( msg, sizeof( msg ), "0 0 %d %d %.1f %.1f",
				 modeVR,
				 vr_mode3D->integer,
				 fovX,
				 fovY );

	// Logged when it CHANGES, which is exactly when a MODE_VR or MODE_3D
	// switch or an FOV tweak happened. Logging it every frame would bury
	// everything else; never logging it at all leaves "what did we actually
	// ask the runtime for?" unanswerable from a device report.
	if ( strcmp( msg, xr_lastOutbound ) != 0 ) {
		Q_strncpyz( xr_lastOutbound, msg, sizeof( xr_lastOutbound ) );
		XR_Log( "out: %s   (haptL haptR MODE_VR MODE_3D fovX fovY)", msg );
	}

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

	// Enter VR only once actually playing. Set 0 to force MODE_VR at all
	// times, which is useful for testing and unusable for playing.
	vr_autoMode = Cvar_Get( "vr_autoMode", "1", CVAR_ARCHIVE );

	// Degrees. Tuning values, not a unit conversion to get right -- send a
	// pair, look through the headset, adjust. Archived, so they can be
	// changed on the device through q3config.cfg or the console without a
	// rebuild, which is what HWXR's flag file exists to achieve.
	vr_fovX    = Cvar_Get( "vr_fovX",    "104.5", CVAR_ARCHIVE );
	vr_fovY    = Cvar_Get( "vr_fovY",    "104.5", CVAR_ARCHIVE );

	// Before the early return, so every vr_* cvar and the recentre command
	// exist even with VR off. A setting the player cannot see until they have
	// already enabled the thing is a setting they cannot find, and vr_enable
	// is CVAR_LATCH -- it needs setting and then a restart.
	XR_ViewInit( );
	XR_SyncInit( );
	XR_InputInit( );
	XR_WeaponInit( );
	XR_HudInit( );
	XR_MenuInit( );

	if ( !vr_enable->integer ) {
		return;
	}

	// Anchored to fs_basepath, which defaults to the executable's own
	// directory (DEFAULT_BASEDIR -> Sys_BinaryPath). A bare fopen( "vr.log" )
	// is relative to the WORKING directory, so launching through a shortcut
	// with a "start in" folder set -- or from a shell sitting anywhere else --
	// silently wrote the log somewhere nobody would look. For the one channel
	// back off the device, "probably in the right place" is not good enough.
	{
		const char  *base = Cvar_VariableString( "fs_basepath" );
		char        path[MAX_OSPATH];

		if ( base && base[0] ) {
			Com_sprintf( path, sizeof( path ), "%s%c%s",
						 base, PATH_SEP, "vr.log" );
		} else {
			Q_strncpyz( path, "vr.log", sizeof( path ) );
		}

		xr_logFile = fopen( path, "w" );
		if ( !xr_logFile ) {
			Com_Printf( "WARNING: XR: could not open %s -- "
						"there is no other channel back, so this is "
						"serious\n", path );
		} else {
			// Where it actually went, in the engine log. The two logs are
			// requested as a pair and this is what pairs them up.
			Com_Printf( "XR: vr.log -> %s\n", path );
		}
	}

	// Line 1, always. "Am I running the binary I just built?" is answered
	// permanently, from the start.
	XR_Log( "Tremulous WXR -- built " __DATE__ " " __TIME__ " -- XrApi "
			XR_VERSION_STRING );
	XR_Log( "engine: %s", Q3_VERSION );

	// Anything the cvar and config setup above wanted to say, now that
	// there is somewhere to say it. After the build stamp, so line 1 is
	// still the build stamp.
	XR_FlushPendingLog( );

	// After the log exists, so the reason is on the record.
	XR_HideCrosshair( );

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

	XR_InitWinsock( );
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

	// Retry the bind if it has not come up. Transport failures here are
	// fatal to everything downstream and the characteristic symptom is total
	// silence, so it is worth re-attempting rather than giving up in Init
	// and leaving the port dead for the whole session.
	if ( !xr_sockReady ) {
		int retryNow = Sys_Milliseconds();

		if ( retryNow - xr_sockRetryAt >= XR_SOCKET_RETRY_MS ) {
			xr_sockRetryAt = retryNow;
			XR_InitWinsock( );
			xr_sockReady = XR_OpenSocket();
		}
	}

	if ( xr_sockReady ) {
		XR_Receive();
	}

	// Buttons, immediately after ingest and before anything else can look at
	// the state, so a press and its key event land in the same frame.
	XR_InputFrame( );

	XR_SendOut();

	// The runtime wipes its directory when it starts. If we have never heard
	// from it, our handshake may simply have been deleted -- rewrite it
	// rather than waiting forever for a reply to a file that is gone.
	now = Sys_Milliseconds();

	// Packets stopping is the characteristic failure of this protocol, and
	// it is invisible otherwise: we hold the last good pose, so the view
	// simply freezes and looks like a hang rather than a dropout. Say so.
	if ( xr_streaming && xr.valid
		 && now - xr.lastPacketTime >= XR_STREAM_TIMEOUT_MS ) {
		xr_streaming = qfalse;
		xr_streamStoppedAt = now;
		XR_Log( "packets: STOPPED after %d received "
				"(holding last pose; MODE_VR 0 does this deliberately)",
				xr.packetsReceived );
	}

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

#ifdef _WIN32
	if ( xr_wsaStarted ) {
		WSACleanup( );
		xr_wsaStarted = qfalse;
	}
#endif

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

void XR_NoteGameState( qboolean inGame )
{
	xr_inGame = inGame;
}

/*
==================
XR_InGame
==================
*/
qboolean XR_InGame( void )
{
	return xr_inGame;
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
