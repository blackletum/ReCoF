// ReCoF client additions
//
// The client features ReCoF adds. client.dll is built with a newer compiler than retail
// (docs/toolchains.md), so none of its addresses match retail; the LiveSplit autosplitter finds the
// values it reads through g_RcAutosplit below (docs/speedrun.md).

#include "hud.h"
#include "cl_util.h"
#include "kbutton.h"
#include "cvardef.h"
#include "usercmd.h"
#include "const.h"
#include "pm_defs.h"
#include "rc_client.h"
#include "rc_version.h"
#include "rc_prof.h"
#include "glmanager.h"
#include "vgui_TeamFortressViewport.h"
#include "Scope_guess.h"

#include <stdio.h>
#include <ctype.h>	// B498
#include <windows.h>
#include "rc_bones.h"	// ReCoF (B339)
#include "rc_steamnet.h"	// co-op spike
#include "net_api.h"
#include <stdarg.h>

#ifndef M_PI
#define M_PI		3.14159265358979323846	// matches value in gcc v2 math.h
#endif

extern "C" playermove_t *pmove;		// pm_shared.c: the move client prediction ran last

extern int g_iVisibleMouse;			// inputw32.cpp: a menu cursor is on screen

void KeyDown( kbutton_t *b );		// input.cpp
void KeyUp( kbutton_t *b );

static kbutton_t	in_rcducktap;
static cvar_t		*rc_autojump;
static cvar_t		*rc_ducktap_priority;
static cvar_t		*rc_jump_priority;
static int			s_bDucktapReleased;	// +rc_ducktap released duck on the frame before
static cvar_t		*rc_rawinput;
static cvar_t		*rc_vsync;	// B304
static cvar_t		*rc_omgbruh_sound;	// B318
static HWND			s_hRawWnd;			// raw input 1: the game window, once hooked
static WNDPROC		s_pfnRawOldProc;	// raw input 1: the engine's window procedure
static HANDLE		s_hRawThread;		// raw input 2: the input thread
static DWORD		s_dwRawThreadId;
static HWND volatile	s_hRawThreadWnd;	// raw input 2: the thread's window, once it has the raw mouse
static int			s_iRawMode;			// raw input: the mode whose window was given the raw mouse, 0 for none
static int			s_bRawFailed;		// raw input: a window, the thread or the registration failed
static int			s_bRawRInput;		// raw input: RInput.dll is loaded and has the raw mouse
static volatile LONG	s_lRawDX, s_lRawDY;	// raw input: mouse counts since the last mouse move
static cvar_t		*rc_viewmodel_fov;
static cvar_t		*rc_viewmodel_ofs_forward;
static cvar_t		*rc_viewmodel_ofs_right;
static cvar_t		*rc_viewmodel_ofs_up;
static float		s_flViewmodelOfsScale = 1;	// viewmodel offsets: eased to 0 in iron sights
static cvar_t		*rc_crosshair;
static cvar_t		*rc_cross;
static cvar_t		*rc_cross_color;
static cvar_t		*rc_cross_alpha;
static cvar_t		*rc_cross_thickness;
static cvar_t		*rc_cross_size;
static cvar_t		*rc_cross_gap;
static cvar_t		*rc_cross_outline;
static cvar_t		*rc_cross_circle_radius;
static cvar_t		*rc_cross_dot_color;
static cvar_t		*rc_cross_dot_size;
static cvar_t		*rc_cross_top_line;
static cvar_t		*rc_cross_bottom_line;
static cvar_t		*rc_cross_left_line;
static cvar_t		*rc_cross_right_line;

//-----------------------------------------------------------------------------
// Where this build keeps the values the LiveSplit autosplitter reads from client.dll, so a script can
// find them by the tag instead of by address (docs/speedrun.md, "ReCoF autosplitter block"). Each
// pointer gives the bytes retail has at the listed address; hw.dll keeps its retail addresses.
//-----------------------------------------------------------------------------
extern float lastDrawTime;				// glows.cpp
char *READ_STRING( void );				// parsemsg.cpp

RcAutosplit_t g_RcAutosplit;			// rc_client.h

static void IN_RcDucktapDown( void ) { KeyDown( &in_rcducktap ); }
static void IN_RcDucktapUp( void ) { KeyUp( &in_rcducktap ); }

//-----------------------------------------------------------------------------
// called from InitInput
//-----------------------------------------------------------------------------
// ReCoF (B307): rc_vidmode (hw.dll host_cmd.c) changed the video mode while the game runs, and calls this before
// it saves and loads the game again. What the client laid out for the old size is made again:
//  - the screen info behind ScreenWidth, ScreenHeight, XRES and YRES (CHud::VidInit read it once);
//  - the VGUI1 viewport, whose panels take their positions from the screen size when they are built. A new one
//    replaces it; the old one is taken off the root and hidden but not deleted, because VGUI1 keeps focus and
//    mouse pointers into its panels;
//  - the post process screen texture and the water's screen sized textures.
// CHud::VidInit is not run: it resets level state, and the load that follows runs it
void *VGui_GetPanel();					// vgui_int.cpp
void RcResizeScreenTexture( void );		// gl_postprocess.cpp
void RcWaterResize( void );				// hud_msg.cpp

static void RcVidRebuild_f( void )
{
	gHUD.m_scrinfo.iSize = sizeof( gHUD.m_scrinfo );
	GetScreenInfo( &gHUD.m_scrinfo );

	vgui::Panel *root = (vgui::Panel *)VGui_GetPanel();
	if ( gViewPort )
	{
		root->removeChild( gViewPort );
		gViewPort->setVisible( false );
	}

	TeamFortressViewport *viewport = new TeamFortressViewport( 0, 0, ScreenWidth, ScreenHeight );	// sets gViewPort
	viewport->setParent( root );
	viewport->Initialize();

	RcResizeScreenTexture();
	RcWaterResize();
}

// B363: the files the HUD reads during play are read once in the background when the game starts. The game reads
// them on the main thread in the frame that needs them: picking up an item reads its file in inventoryitems three
// times (CHud::CheckItem) and its picture once (vgui_LoadTGA), a hint reads txtfiles/hints.txt, a decal the engine
// let go of comes back from decals.wad. The first such read of a file since Windows started waits for the disk, and
// on some PCs for the virus scanner: a player's performance log (0.2.3-beta.7, Ryzen 9 5900X) showed frames of 83 and
// 92 ms on item pickups with the main thread running 6 ms, the rest waiting in ReadFile under vgui_LoadTGA and
// CheckItem, and 88 ms under the decal reload. This thread reads those files at low priority and drops what it read,
// so Windows holds them in memory when the game asks; the game still loads each file itself, as before
static HANDLE s_hPrefetch;
static volatile LONG s_lPrefetchStop;		// set by RcPrefetch_Shutdown
static char s_szPrefetchDir[MAX_PATH];		// the game folder, cryoffear\ next to cof.exe
static char s_PrefetchBuf[64 * 1024];
static char s_szPrefetchText[16 * 1024];	// an item file's start (the largest is under 8 KB)

// reads a file of the game folder to its end; with pszText, keeps its start there
static void RcPrefetchFile( const char *pszRelative, char *pszText = NULL, int iTextSize = 0 )
{
	if ( pszText )
		pszText[0] = 0;

	if ( s_lPrefetchStop )
		return;

	char szPath[MAX_PATH * 2];
	_snprintf( szPath, sizeof( szPath ) - 1, "%s%s", s_szPrefetchDir, pszRelative );
	szPath[sizeof( szPath ) - 1] = 0;

	HANDLE hFile = CreateFileA( szPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL );
	if ( hFile == INVALID_HANDLE_VALUE )
		return;

	// low disk priority: the game's own reads, a map loading, go first
	FILE_IO_PRIORITY_HINT_INFO hint;
	hint.PriorityHint = IoPriorityHintLow;
	SetFileInformationByHandle( hFile, FileIoPriorityHintInfo, &hint, sizeof( hint ) );

	int iKept = 0;
	DWORD dwRead;
	while ( !s_lPrefetchStop && ReadFile( hFile, s_PrefetchBuf, sizeof( s_PrefetchBuf ), &dwRead, NULL ) && dwRead )
	{
		if ( pszText && iKept < iTextSize - 1 )
		{
			int iCopy = iTextSize - 1 - iKept;
			if ( iCopy > (int)dwRead )
				iCopy = (int)dwRead;
			memcpy( pszText + iKept, s_PrefetchBuf, iCopy );
			iKept += iCopy;
			pszText[iKept] = 0;
		}
	}

	CloseHandle( hFile );
}

// the pictures an item file names: "inventory_picture", "quick_picture" and "check_picture", each a key, then
// { data "<file>" }
static void RcPrefetchItemPictures( const char *pszText )
{
	const char *p = pszText;

	while ( !s_lPrefetchStop && ( p = strstr( p, "picture\"" ) ) != NULL )
	{
		const char *pszData = strstr( p + 8, "data" );
		const char *pszStart = pszData ? strchr( pszData + 4, '"' ) : NULL;
		const char *pszEnd = pszStart ? strchr( pszStart + 1, '"' ) : NULL;
		if ( !pszEnd )
			return;

		char szFile[MAX_PATH];
		int iLength = pszEnd - pszStart - 1;
		if ( iLength > 0 && iLength < MAX_PATH )
		{
			memcpy( szFile, pszStart + 1, iLength );
			szFile[iLength] = 0;
			RcPrefetchFile( szFile );
		}

		p = pszEnd + 1;
	}
}

// every file under a folder of the game folder, and for the item files the pictures they name
static void RcPrefetchFolder( const char *pszRelative, bool bItems )
{
	char szPattern[MAX_PATH * 2];
	_snprintf( szPattern, sizeof( szPattern ) - 1, "%s%s\\*", s_szPrefetchDir, pszRelative );
	szPattern[sizeof( szPattern ) - 1] = 0;

	WIN32_FIND_DATAA fd;
	HANDLE hFind = FindFirstFileA( szPattern, &fd );
	if ( hFind == INVALID_HANDLE_VALUE )
		return;

	do
	{
		if ( fd.cFileName[0] == '.' )
			continue;

		char szChild[MAX_PATH];
		_snprintf( szChild, sizeof( szChild ) - 1, "%s\\%s", pszRelative, fd.cFileName );
		szChild[sizeof( szChild ) - 1] = 0;

		if ( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
			RcPrefetchFolder( szChild, bItems );
		else if ( bItems )
		{
			RcPrefetchFile( szChild, s_szPrefetchText, sizeof( s_szPrefetchText ) );
			RcPrefetchItemPictures( s_szPrefetchText );
		}
		else
			RcPrefetchFile( szChild );
	} while ( !s_lPrefetchStop && FindNextFileA( hFind, &fd ) );

	FindClose( hFind );
}

// B365: the models the client loads by name the first time it draws them (StudioModelRenderer.cpp: the head
// shield's goggles, gas masks, death and telescope views, the book menu, muzzle flashes; the dual weapons and
// the head shield's own model come from the server and live in models/weapons). A model the server precaches is
// loaded with the map; these are not, and loading one waited for the disk in the frame (a 28 ms frame under
// Mod_LoadModel on c_avlopp in a player's performance log)
static const char *s_szRcLateModels[] =
{
	"models\\mask.mdl", "models\\zombie_eyes.mdl", "models\\flash1.mdl", "models\\flash2.mdl", "models\\flash3.mdl",
	"models\\nv_goggles.mdl", "models\\items\\tv_frame.mdl", "models\\props\\blandat\\v_telescope.mdl", "models\\bookmenu.mdl",
	"models\\null.mdl",
};

static HANDLE s_hPrefetchWake;			// a map is loading (RcPrefetch_Map)
static CRITICAL_SECTION s_csPrefetchMap;
static char s_szPrefetchMap[MAX_PATH];		// its maps/<name>.bsp, empty once taken

// B365: the music a map names: the mp3 files in its entities (ambient_fmodstream's message, inter_door's music),
// which the game finds in mp3/. irrKlang reads the whole file at each start of a track (mp3.cpp, B364)
static void RcPrefetchMapMusic( const char *pszMap )
{
	char szPath[MAX_PATH * 2];
	_snprintf( szPath, sizeof( szPath ) - 1, "%s%s", s_szPrefetchDir, pszMap );
	szPath[sizeof( szPath ) - 1] = 0;

	HANDLE hFile = CreateFileA( szPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL );
	if ( hFile == INVALID_HANDLE_VALUE )
		return;	// a map from another folder (downloads, an addon): nothing read ahead

	// the BSP header: version, then the entities lump's offset and length
	int header[3];
	DWORD dwRead = 0;
	char *pszEnts = NULL;
	if ( ReadFile( hFile, header, sizeof( header ), &dwRead, NULL ) && dwRead == sizeof( header ) && header[1] > 0 && header[2] > 0 && header[2] < 8 * 1024 * 1024 )
	{
		pszEnts = (char *)malloc( header[2] + 1 );
		if ( pszEnts && ( SetFilePointer( hFile, header[1], NULL, FILE_BEGIN ) == INVALID_SET_FILE_POINTER || !ReadFile( hFile, pszEnts, header[2], &dwRead, NULL ) || dwRead != (DWORD)header[2] ) )
		{
			free( pszEnts );
			pszEnts = NULL;
		}
	}
	CloseHandle( hFile );
	if ( !pszEnts )
		return;
	pszEnts[header[2]] = 0;

	// each quoted value that ends in .mp3
	for ( char *p = strstr( pszEnts, ".mp3\"" ); p && !s_lPrefetchStop; p = strstr( p + 5, ".mp3\"" ) )
	{
		char *pszStart = p;
		while ( pszStart > pszEnts && pszStart[-1] != '"' && pszStart[-1] != '\n' )
			pszStart--;
		int iLength = p + 4 - pszStart;
		if ( pszStart == pszEnts || pszStart[-1] != '"' || iLength < 5 || iLength > 200 )
			continue;

		char szFile[256];
		if ( memchr( pszStart, '/', iLength ) || memchr( pszStart, '\\', iLength ) )
			szFile[0] = 0;
		else
			strcpy( szFile, "mp3\\" );
		strncat( szFile, pszStart, iLength );
		RcPrefetchFile( szFile );
	}

	free( pszEnts );
}

static DWORD WINAPI RcPrefetchThread( LPVOID )
{
	SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_LOWEST );

	RcPrefetchFolder( "inventoryitems", true );
	RcPrefetchFile( "gfx\\vgui\\640_inventory.tga" );	// the inventory's backgrounds, which a pickup can change
	RcPrefetchFile( "gfx\\vgui\\640_inventory_three.tga" );
	RcPrefetchFile( "gfx\\vgui\\640_inventory_p.tga" );
	RcPrefetchFolder( "txtfiles", false );	// hints and the other texts
	RcPrefetchFile( "decals.wad" );

	// B365: the models the client loads late
	for ( int i = 0; i < ARRAYSIZE( s_szRcLateModels ); i++ )
		RcPrefetchFile( s_szRcLateModels[i] );
	RcPrefetchFolder( "models\\weapons", false );

	// B365: then the music of each map that loads
	while ( !s_lPrefetchStop )
	{
		WaitForSingleObject( s_hPrefetchWake, INFINITE );

		char szMap[MAX_PATH];
		EnterCriticalSection( &s_csPrefetchMap );
		strcpy( szMap, s_szPrefetchMap );
		s_szPrefetchMap[0] = 0;
		LeaveCriticalSection( &s_csPrefetchMap );

		if ( szMap[0] )
			RcPrefetchMapMusic( szMap );
	}

	return 0;
}

// B365: from CHud::VidInit, a map is loading; its name is maps/<name>.bsp
void RcPrefetch_Map( const char *pszLevel )
{
	if ( !s_hPrefetch || !s_hPrefetchWake || !pszLevel || !pszLevel[0] || strlen( pszLevel ) >= sizeof( s_szPrefetchMap ) )
		return;

	EnterCriticalSection( &s_csPrefetchMap );
	strcpy( s_szPrefetchMap, pszLevel );
	LeaveCriticalSection( &s_csPrefetchMap );
	SetEvent( s_hPrefetchWake );
}

static void RcPrefetch_Start( void )
{
	char szExe[MAX_PATH];
	DWORD dwLength = GetModuleFileNameA( NULL, szExe, sizeof( szExe ) );
	char *pszSlash = dwLength && dwLength < sizeof( szExe ) ? strrchr( szExe, '\\' ) : NULL;
	if ( !pszSlash || s_hPrefetch )
		return;

	pszSlash[1] = 0;
	_snprintf( s_szPrefetchDir, sizeof( s_szPrefetchDir ) - 1, "%s%s\\", szExe, gEngfuncs.pfnGetGameDirectory() );
	s_szPrefetchDir[sizeof( s_szPrefetchDir ) - 1] = 0;

	InitializeCriticalSection( &s_csPrefetchMap );
	s_hPrefetchWake = CreateEventA( NULL, FALSE, FALSE, NULL );
	s_hPrefetch = CreateThread( NULL, 64 * 1024, RcPrefetchThread, NULL, 0, NULL );
}

// B363: when the client shuts down (IN_Shutdown), before the engine unloads client.dll: the prefetch thread stops
// after its current read of up to 64 KB; if it does not end, client.dll stays loaded until the process ends (as B351)
void RcPrefetch_Shutdown( void )
{
	if ( !s_hPrefetch )
		return;

	InterlockedExchange( &s_lPrefetchStop, 1 );
	if ( s_hPrefetchWake )
		SetEvent( s_hPrefetchWake );
	if ( WaitForSingleObject( s_hPrefetch, 2000 ) != WAIT_OBJECT_0 )
	{
		HMODULE hSelf;
		GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCSTR)RcPrefetch_Shutdown, &hSelf );
	}

	CloseHandle( s_hPrefetch );
	s_hPrefetch = NULL;
}

// ReCoF co-op spike: the engine's UDP through Steam's relay network (rc_steamnet.cpp). rc_coop_host asks Steam for an
// address for this listen server, rc_coop_join <169.254.x.x:port> connects to a friend's. Every packet goes through
// Steam's relays, so the players' IP addresses stay hidden; "direct" lets Steam use direct routes too (B480: direct
// routes were on unless "relay" was given, and the lobby never gave it). Nothing runs until one of them is typed.
static int RcCoopHasArg( const char *pszArg );
static bool RcCoopRelayOnly( void )
{
	return !RcCoopHasArg( "direct" );
}
static void RcCoopPrint( const char *fmt, ... )
{
	char buf[512];
	va_list ap;
	va_start( ap, fmt );
	_vsnprintf( buf, sizeof( buf ) - 1, fmt, ap );
	va_end( ap );
	buf[sizeof( buf ) - 1] = 0;
	gEngfuncs.Con_Printf( "%s", buf );
}

static int RcCoopHasArg( const char *pszArg )
{
	for ( int i = 1; i < gEngfuncs.Cmd_Argc(); i++ )
	{
		if ( !stricmp( gEngfuncs.Cmd_Argv( i ), pszArg ) )
			return 1;
	}
	return 0;
}

// B391: a player who kept the default name gets his Steam name for co-op, so the players have names apart (the host
// gives each saved player back to the one with his name after a load, hw.dll RcCoop_PickSlot). Quotes, semicolons
// and control characters are left out; a name the player chose himself stays
static void RcCoopDefaultName( void )
{
	const char *pszName = gEngfuncs.pfnGetCvarString( "name" );
	if ( pszName && pszName[0] && stricmp( pszName, "Unknown" ) && stricmp( pszName, "Player" ) && stricmp( pszName, "unnamed" ) )
		return;
	const char *pszSteam = RcSteamNet_PersonaName();
	if ( !pszSteam )
		return;
	char szName[32];
	int n = 0;
	for ( const char *p = pszSteam; *p && n < (int)sizeof( szName ) - 1; p++ )
	{
		if ( (unsigned char)*p < 32 || *p == '"' || *p == ';' || *p == '\\' || *p == '%' )
			continue;
		szName[n++] = *p;
	}
	szName[n] = 0;
	if ( n > 0 )
	{
		char szCmd[64];
		_snprintf( szCmd, sizeof( szCmd ) - 1, "name \"%s\"\n", szName );
		szCmd[sizeof( szCmd ) - 1] = 0;
		gEngfuncs.pfnClientCmd( szCmd );
	}
}

static void RcCoopHost_f( void )
{
	RcCoopDefaultName();
	int port = (int)gEngfuncs.pfnGetCvarFloat( "hostport" );
	if ( port <= 0 )
		port = (int)gEngfuncs.pfnGetCvarFloat( "port" );
	if ( port <= 0 )
		port = 27015;
	RcSteamNet_Host( port, RcCoopRelayOnly(), !RcCoopHasArg( "nomap" ), RcCoopPrint );
}

static char s_szCoopConnect[96];
static DWORD s_dwCoopConnectAt;
static int s_bCoopWasIn, s_iCoopRetries, s_bCoopLeft;	// the guest's reconnect (RcCoopReconnect)
static DWORD s_dwCoopRetryAt;

static void RcCoopJoin_f( void )
{
	if ( gEngfuncs.Cmd_Argc() < 2 )
	{
		RcCoopPrint( "usage: rc_coop_join <169.254.x.x:port> [direct]\n" );
		return;
	}
	// the engine's tokenizer splits "a.b.c.d:port" at the colon into three arguments: join them back
	char addr[64];
	addr[0] = 0;
	for ( int i = 1; i < gEngfuncs.Cmd_Argc(); i++ )
	{
		const char *arg = gEngfuncs.Cmd_Argv( i );
		if ( !stricmp( arg, "relay" ) || !stricmp( arg, "direct" ) || !stricmp( arg, "nomap" ) )
			continue;
		strncat( addr, arg, sizeof( addr ) - 1 - strlen( addr ) );
	}
	if ( RcSteamNet_Join( addr, RcCoopRelayOnly(), RcCoopPrint ) )
	{
		RcCoopDefaultName();
		// connect from HUD_Frame in 3 s: on the command line this runs before the menu map that InitInput queues,
		// which would cancel the connect
		_snprintf( s_szCoopConnect, sizeof( s_szCoopConnect ) - 1, "connect %s\n", addr );
		s_szCoopConnect[sizeof( s_szCoopConnect ) - 1] = 0;
		s_dwCoopConnectAt = GetTickCount() + 3000;
		s_bCoopLeft = 0;
		s_bCoopWasIn = 0;
		s_iCoopRetries = 0;
		gEngfuncs.Cvar_SetValue( "rc_coop_left", 0 );
	}
}

static void RcCoopStatus_f( void )
{
	RcSteamNet_Status( RcCoopPrint );
}

// rc_coop_start [map]: a story co-op game for up to 4 players that friends join through Steam (default: the story's
// first map)
static char s_szCoopStart[512];
static DWORD s_dwCoopStartAt;

static void RcCoopStart_f( void )
{
	const char *pszMap = gEngfuncs.Cmd_Argc() > 1 ? gEngfuncs.Cmd_Argv( 1 ) : "c_intro";
	// B392, B396: rc_coop_start <map> <easy | normal | hard | nightmare> sets the difficulty and its co-op rules first
	// (hl.dll rc_coop_preset)
	const char *pszPreset = gEngfuncs.Cmd_Argc() > 2 ? gEngfuncs.Cmd_Argv( 2 ) : "";
	if ( stricmp( pszPreset, "easy" ) && stricmp( pszPreset, "normal" ) && stricmp( pszPreset, "hard" ) && stricmp( pszPreset, "nightmare" ) )
		pszPreset = "";
	char szPreset[48];
	szPreset[0] = 0;
	if ( pszPreset[0] )
		_snprintf( szPreset, sizeof( szPreset ) - 1, "rc_coop_preset %s\n", pszPreset );
	szPreset[sizeof( szPreset ) - 1] = 0;
	// run from HUD_Frame a moment later, like rc_coop_join: on the command line (a launcher or a shortcut) this runs
	// before the menu map InitInput queues, which would replace the co-op map
	// B412: the lobby's rules after the preset: <exit wait> <back after dying> <autosave> <monster health> <skip vote> <simon>
	char szRules[200];
	szRules[0] = 0;
	if ( gEngfuncs.Cmd_Argc() > 8 )
		_snprintf( szRules, sizeof( szRules ) - 1, "rc_coop_exit_wait %d\nrc_coop_respawn_time %d\nrc_coop_autosave %d\nrc_coop_monster_health %d\n"
			"rc_coop_skip_vote %d\nrc_coop_simon %d\n", atoi( gEngfuncs.Cmd_Argv( 3 ) ), atoi( gEngfuncs.Cmd_Argv( 4 ) ), atoi( gEngfuncs.Cmd_Argv( 5 ) ),
			atoi( gEngfuncs.Cmd_Argv( 6 ) ), atoi( gEngfuncs.Cmd_Argv( 7 ) ), atoi( gEngfuncs.Cmd_Argv( 8 ) ) ? 1 : 0 );
	else if ( pszMap[0] )
		_snprintf( szRules, sizeof( szRules ) - 1, "rc_coop_simon %d\n", strcmp( pszMap, "c_intro" ) ? 0 : 1 );	// B411
	szRules[sizeof( szRules ) - 1] = 0;
	// B442: the rates for a story co-op host (sv_lan 1 capped each guest at sv_lan_rate, 20000 bytes a second)
	_snprintf( s_szCoopStart, sizeof( s_szCoopStart ) - 1, "disconnect\nmaxplayers 4\nsv_lan 1\nsv_lan_rate 100000\nsv_maxupdaterate 60\nrc_coop 1\n%s%src_coop_host\nmap %s\n", szPreset, szRules, pszMap );
	s_szCoopStart[sizeof( s_szCoopStart ) - 1] = 0;
	s_dwCoopStartAt = GetTickCount() + 1500;
}

// B498: rc_coop_mp <map> [<back after dying, s> <game starts after, s>]: one of Cry of Fear's own co-op games (the
// co-op campaign, Manhunt, the survival maps, any map of Host Server's list) for up to 4 players who join through
// Steam, set up as rc_coop_start sets up story co-op but with Cry of Fear's multiplayer rules (rc_coop 0) and two of
// Host Server's settings (settings.scr: sv_auto_respawn_time, and mp_footsteps for its lobby's wait)
static int s_bRcHostedClassic;	// read by RcCoopBackToOne: this game hosts one

static void RcCoopMp_f( void )
{
	const char *pszMap = gEngfuncs.Cmd_Argc() > 1 ? gEngfuncs.Cmd_Argv( 1 ) : "";
	bool bOk = pszMap[0] != 0 && strlen( pszMap ) < 64;
	for ( const char *p = pszMap; *p && bOk; p++ )
		bOk = isalnum( (unsigned char)*p ) || *p == '_' || *p == '-';	// a map name and nothing the console would run
	if ( !bOk )
	{
		RcCoopPrint( "usage: rc_coop_mp <map> [<back after dying, s> <game starts after, s>]\n" );
		return;
	}
	char szSet[96];
	szSet[0] = 0;
	if ( gEngfuncs.Cmd_Argc() > 3 )
		_snprintf( szSet, sizeof( szSet ) - 1, "sv_auto_respawn_time %d\nmp_footsteps %d\n", atoi( gEngfuncs.Cmd_Argv( 2 ) ),
			atoi( gEngfuncs.Cmd_Argv( 3 ) ) );
	szSet[sizeof( szSet ) - 1] = 0;
	// maxplayers 4 sets deathmatch 1 (net_ws.c MaxPlayers_f), and rc_coop 0 gives Cry of Fear's multiplayer rules
	_snprintf( s_szCoopStart, sizeof( s_szCoopStart ) - 1, "disconnect\nmaxplayers 4\nsv_lan 1\nsv_lan_rate 100000\n"
		"sv_maxupdaterate 60\nrc_coop 0\npausable 0\n%src_coop_host\nmap %s\n", szSet, pszMap );
	s_szCoopStart[sizeof( s_szCoopStart ) - 1] = 0;
	s_dwCoopStartAt = GetTickCount() + 1500;
	s_bRcHostedClassic = 1;
}

void RcCoopMenu_Open( void );	// B393, rc_coopmenu.cpp

// B397: rc_options opens the Options window from anywhere, as the main menu's Options does
static void RcOptions_f( void )
{
	gEngfuncs.pfnServerCmd( "optionshack 2" );
}
void RcCoopMenu_LaunchOptions( void );	// B395
void RcCoopMenu_Lobby( void );	// B408

// B393: rc_coop_continue <save>: the host's story co-op game from a co-op save (the main menu's Continue), as
// rc_coop_start sets it up; the save brings its own settings (B392)
static void RcCoopContinue_f( void )
{
	if ( gEngfuncs.Cmd_Argc() < 2 )
	{
		RcCoopPrint( "usage: rc_coop_continue <save, for example coop_auto>\n" );
		return;
	}
	// B412: the lobby's rules after the load (the save's own come back during it): <exit wait> <back after dying>
	// <autosave> <monster health> <skip vote> [<simon>, -1 or none for the save's]
	char szRules[200];
	szRules[0] = 0;
	if ( gEngfuncs.Cmd_Argc() > 6 )
	{
		_snprintf( szRules, sizeof( szRules ) - 1, "rc_coop_exit_wait %d\nrc_coop_respawn_time %d\nrc_coop_autosave %d\nrc_coop_monster_health %d\n"
			"rc_coop_skip_vote %d\n", atoi( gEngfuncs.Cmd_Argv( 2 ) ), atoi( gEngfuncs.Cmd_Argv( 3 ) ), atoi( gEngfuncs.Cmd_Argv( 4 ) ),
			atoi( gEngfuncs.Cmd_Argv( 5 ) ), atoi( gEngfuncs.Cmd_Argv( 6 ) ) );
		if ( gEngfuncs.Cmd_Argc() > 7 && atoi( gEngfuncs.Cmd_Argv( 7 ) ) >= 0 )
		{
			int len = strlen( szRules );
			_snprintf( szRules + len, sizeof( szRules ) - 1 - len, "rc_coop_simon %d\n", atoi( gEngfuncs.Cmd_Argv( 7 ) ) ? 1 : 0 );
		}
	}
	szRules[sizeof( szRules ) - 1] = 0;
	_snprintf( s_szCoopStart, sizeof( s_szCoopStart ) - 1, "disconnect\nmaxplayers 4\nsv_lan 1\nsv_lan_rate 100000\nsv_maxupdaterate 60\nrc_coop 1\nrc_coop_host\nload %s\n%s", gEngfuncs.Cmd_Argv( 1 ), szRules );	// B442
	s_szCoopStart[sizeof( s_szCoopStart ) - 1] = 0;
	s_dwCoopStartAt = GetTickCount() + 1500;
}

static void RcCoopInvite_f( void )
{
	RcSteamNet_Invite( RcCoopPrint );
}

// Steam's Join Game or an accepted invite: the host's connect value is "+rc_coop_join a.b.c.d:port"
static void RcCoopSteamJoin( const char *pszConnect )
{
	unsigned a, b, c, d, port;
	// B406: a friend in a story co-op lobby: into the lobby, which joins the host's game when it is up
	if ( !strncmp( pszConnect, "+connect_lobby ", 15 ) )
	{
		RcLobby_Join( _strtoui64( pszConnect + 15, NULL, 10 ) );
		return;
	}
	if ( sscanf( pszConnect, "+rc_coop_join %u.%u.%u.%u:%u", &a, &b, &c, &d, &port ) != 5 || a != 169 || b != 254 )
	{
		RcCoopPrint( "ReCoF co-op: Steam asked to join a game this version does not know how to reach\n" );
		return;
	}
	char cmd[96];
	_snprintf( cmd, sizeof( cmd ) - 1, "rc_coop_join %u.%u.%u.%u:%u\n", a, b, c, d, port );
	cmd[sizeof( cmd ) - 1] = 0;
	gEngfuncs.pfnClientCmd( cmd );
}

// B406: Steam starts the game with +connect_lobby <id> when a friend accepts an invite to a lobby while it is closed
static void RcConnectLobby_f( void )
{
	if ( gEngfuncs.Cmd_Argc() > 1 )
		RcLobby_Join( _strtoui64( gEngfuncs.Cmd_Argv( 1 ), NULL, 10 ) );
}

// rc_lobby: the lobby's state, players and data (rc_lobby create | leave | invite as the panel's buttons)
static void RcLobby_f( void )
{
	const char *pszArg = gEngfuncs.Cmd_Argc() > 1 ? gEngfuncs.Cmd_Argv( 1 ) : "";
	if ( !strcmp( pszArg, "create" ) )
		RcLobby_Create();
	else if ( !strcmp( pszArg, "leave" ) )
		RcLobby_Leave();
	else if ( !strcmp( pszArg, "invite" ) )
		RcLobby_Invite();
	static const char *s_pszState[3] = { "none", "asked", "in" };
	int iState = RcLobby_State();
	char szNames[4][48];
	bool bHost[4], bSelf[4];
	int n = RcLobby_Members( szNames, bHost, bSelf, 4 );
	RcCoopPrint( "ReCoF lobby: %s%s, id %I64u, %d players, difficulty %s, game %s, connect %s%s%s\n", s_pszState[iState >= 0 && iState <= 2 ? iState : 0],
		RcLobby_Owner() ? " (host)" : "", RcLobby_Id(), n, RcLobby_Data( "difficulty" ), RcLobby_Data( "game" ), RcLobby_Data( "connect" ),
		RcLobby_Note()[0] ? ", note: " : "", RcLobby_Note() );
	for ( int i = 0; i < n; i++ )
		RcCoopPrint( "ReCoF lobby: player %d%s%s\n", i + 1, bHost[i] ? " host" : "", bSelf[i] ? " (this game)" : "" );
}

// test runs: rc_coop_autotest <seconds> prints the counters and takes a screenshot halfway, then quits
static DWORD s_dwCoopShot, s_dwCoopQuit;

static void RcCoopAutotest_f( void )
{
	int sec = gEngfuncs.Cmd_Argc() > 1 ? atoi( gEngfuncs.Cmd_Argv( 1 ) ) : 0;
	if ( sec <= 0 )
	{
		s_dwCoopShot = s_dwCoopQuit = 0;
		return;
	}
	s_dwCoopShot = GetTickCount() + (DWORD)sec * 500 + 1;
	s_dwCoopQuit = GetTickCount() + (DWORD)sec * 1000 + 1;
}

// test runs only (cof.exe started with -rccmdfile): once a second, the console commands written to rc_cmd.txt next to
// cof.exe run and the file is deleted, so a test script can drive a game it cannot type into
static void RcCoopCmdFile( void )
{
	static int s_iOn = -1;
	static DWORD s_dwNext;
	if ( s_iOn < 0 )
		s_iOn = gEngfuncs.CheckParm( "-rccmdfile", NULL ) ? 1 : 0;
	if ( !s_iOn || (long)( GetTickCount() - s_dwNext ) < 0 )
		return;
	s_dwNext = GetTickCount() + 1000;

	char path[MAX_PATH];
	GetModuleFileNameA( NULL, path, sizeof( path ) );
	char *slash = strrchr( path, '\\' );
	if ( !slash )
		return;
	strcpy( slash + 1, "rc_cmd.txt" );
	FILE *f = fopen( path, "rb" );
	if ( !f )
		return;
	char buf[2048];
	int n = (int)fread( buf, 1, sizeof( buf ) - 2, f );
	fclose( f );
	DeleteFileA( path );
	if ( n <= 0 )
		return;
	if ( buf[n - 1] != '\n' )
		buf[n++] = '\n';
	buf[n] = 0;
	gEngfuncs.pfnClientCmd( buf );
}

// a story co-op guest the host dropped (a load of a co-op save, a map restart) connects again: up to 10 tries, 12 s
// apart, while this game is not connected to a 169.254 (Steam) address; rc_coop_leave, the disconnect command (the
// menu's too: hw.dll sets rc_coop_left) and a game elsewhere (not the menu's background map) stop it
static void RcCoopLeave_f( void )
{
	s_bCoopLeft = 1;
	// B546: the group's lobby stays (only its own Leave button leaves it): a player in one goes back to it on the
	// menu, where Join game takes him back in. B406 left the lobby too, and a host who left took it from everyone
	if ( RcLobby_State() == RCLOBBY_IN )
	{
		RcCoopPrint( "ReCoF co-op: left the game, back in the lobby\n" );
		RcCoopMenu_Lobby();
		return;
	}
	gEngfuncs.pfnClientCmd( "disconnect\n" );
	RcCoopPrint( "ReCoF co-op: left the game; rc_coop_join to come back\n" );
}

static void RcCoopReconnect( void )
{
	if ( !RcSteamNet_Joined() || s_bCoopLeft || !s_szCoopConnect[0] || s_dwCoopConnectAt )
		return;
	net_status_t st;
	memset( &st, 0, sizeof( st ) );
	gEngfuncs.pNetAPI->Status( &st );
	bool bIn = st.connected && st.remote_address.ip[0] == 169 && st.remote_address.ip[1] == 254;
	if ( bIn )
	{
		s_bCoopWasIn = 1;
		s_iCoopRetries = 0;
		s_dwCoopRetryAt = 0;
		if ( gEngfuncs.pfnGetCvarFloat( "rc_coop_left" ) != 0 )
			gEngfuncs.Cvar_SetValue( "rc_coop_left", 0 );	// a disconnect from before this game (a Steam invite join)
		return;
	}
	if ( gEngfuncs.pfnGetCvarFloat( "rc_coop_left" ) != 0 )
	{
		gEngfuncs.Cvar_SetValue( "rc_coop_left", 0 );
		if ( s_bCoopWasIn )
			RcCoopPrint( "ReCoF co-op: left the game; rc_coop_join to come back\n" );
		s_bCoopWasIn = 0;
		return;
	}
	const char *pszLevel = gEngfuncs.pfnGetLevelName();
	if ( st.connected && pszLevel && pszLevel[0] && !strstr( pszLevel, "game_menu" ) )
	{
		s_bCoopWasIn = 0;	// the user plays another game
		return;
	}
	if ( !s_bCoopWasIn || s_iCoopRetries >= 10 )
		return;
	if ( !s_dwCoopRetryAt )
	{
		s_dwCoopRetryAt = GetTickCount() + 3000;
		return;
	}
	if ( (long)( GetTickCount() - s_dwCoopRetryAt ) < 0 )
		return;
	s_iCoopRetries++;
	s_dwCoopRetryAt = GetTickCount() + 12000;
	RcCoopPrint( "ReCoF co-op: the host dropped this game, connecting again (%d)\n", s_iCoopRetries );
	gEngfuncs.pfnClientCmd( s_szCoopConnect );
}

void RcVoice_Init( void );	// B422, rc_voice.cpp
void RcVoice_Frame( void );

// B492: a story co-op host's server kept 4 player slots and rc_coop 1 after his game ended (Leave co-op and the
// menus' own disconnects set no "maxplayers 1"), and a map started after that (the load screen's c_loadgame, the menu
// map from the save screen or the credits) ran with 4 slots: a single-player save or level change then stopped the game
// with "ENTITY IS NOT A PLAYER: 2" (hw.dll host_cmd.c LoadGamestate, CreateEntityList) and "single player" ran under
// the story co-op rules. Once this game hosted story co-op and that game is over, single player's settings come back:
// after 1.5 s disconnected with no map "maxplayers 1" (a level change has no map for seconds while the client stays
// connected; story co-op needs more than one slot, so rc_coop is left alone there), and a menu map that started with
// 4 slots starts again with 1
static int s_bRcHostedCoop;
static DWORD s_dwRcNoMapSince;

static bool RcCoopMenuMap( const char *pszLevel )
{
	static const char *s_pszMenuMaps[] = { "/c_game_menu", "/c_loadgame.", "/c_difficulty_settings.", "/c_unlockables." };
	for ( int i = 0; i < 4; i++ )
	{
		if ( strstr( pszLevel, s_pszMenuMaps[i] ) )
			return true;
	}
	return false;
}

static void RcCoopBackToOne( void )
{
	if ( s_dwCoopStartAt || s_dwCoopConnectAt )
		return;	// a co-op start or join is on its way
	const char *pszLevel = gEngfuncs.pfnGetLevelName();
	net_status_t rcNet;
	memset( &rcNet, 0, sizeof( rcNet ) );
	gEngfuncs.pNetAPI->Status( &rcNet );
	bool bNoMap = ( !pszLevel || !pszLevel[0] ) && !rcNet.connected;	// out of every game, not between two levels
	if ( !pszLevel )
		pszLevel = "";
	int nSlots = gEngfuncs.GetMaxClients();
	bool bMenu = pszLevel[0] && RcCoopMenuMap( pszLevel );
	// this game hosts story co-op: rc_coop is the host's own hl.dll cvar (0 on a guest's machine); B498: or one of
	// Cry of Fear's own co-op games the lobby started (rc_coop_mp)
	if ( pszLevel[0] && !bMenu && nSlots > 1 && ( gEngfuncs.pfnGetCvarFloat( "rc_coop" ) != 0 || s_bRcHostedClassic ) )
	{
		s_bRcHostedCoop = 1;
		return;
	}
	if ( !bNoMap )
		s_dwRcNoMapSince = 0;
	else if ( !s_dwRcNoMapSince )
		s_dwRcNoMapSince = GetTickCount() | 1;
	if ( !s_bRcHostedCoop )
		return;
	if ( bNoMap )
	{
		if ( (long)( GetTickCount() - s_dwRcNoMapSince ) < 1500 )
			return;
		s_bRcHostedCoop = 0;
		s_bRcHostedClassic = 0;
		gEngfuncs.pfnClientCmd( "maxplayers 1\n" );
		RcCoopPrint( "ReCoF co-op: the co-op game is over, back to one player\n" );
	}
	else if ( bMenu && nSlots > 1 )
	{
		s_bRcHostedCoop = 0;
		s_bRcHostedClassic = 0;
		char szMap[64];
		const char *pszBase = strrchr( pszLevel, '/' );
		_snprintf( szMap, sizeof( szMap ) - 1, "%s", pszBase ? pszBase + 1 : pszLevel );
		szMap[sizeof( szMap ) - 1] = 0;
		char *pszDot = strrchr( szMap, '.' );
		if ( pszDot )
			*pszDot = 0;
		char szCmd[160];
		_snprintf( szCmd, sizeof( szCmd ) - 1, "disconnect\nmaxplayers 1\ndeathmatch 0\nrc_coop 0\nmap %s\n", szMap );
		szCmd[sizeof( szCmd ) - 1] = 0;
		gEngfuncs.pfnClientCmd( szCmd );
		RcCoopPrint( "ReCoF co-op: the co-op game is over, %s starts again for one player\n", szMap );
	}
	else if ( pszLevel[0] && nSlots <= 1 )
		s_bRcHostedCoop = 0;
}

void RcCoop_Frame( void )
{
	RcVoice_Frame();	// B422
	RcCoopCmdFile();
	RcSteamNet_Frame();
	RcSteamNet_Pump();
	RcCoopReconnect();
	RcCoopBackToOne();	// B492
	if ( s_dwCoopStartAt && (long)( GetTickCount() - s_dwCoopStartAt ) >= 0 )
	{
		s_dwCoopStartAt = 0;
		gEngfuncs.pfnClientCmd( s_szCoopStart );
	}
	if ( s_dwCoopConnectAt && (long)( GetTickCount() - s_dwCoopConnectAt ) >= 0 )
	{
		s_dwCoopConnectAt = 0;
		// B442: a guest at the engine's 1998 rates (20 updates and 30 commands a second, 25000 bytes) saw his
		// teammates move in steps; raised for co-op, never lowered (they are the player's own, kept in config.cfg).
		// Here, not in rc_coop_join: on a command line that runs before config.cfg puts the saved values back
		static const struct { const char *cvar; float value; } s_Rates[] = { { "cl_updaterate", 60 }, { "cl_cmdrate", 60 }, { "rate", 100000 } };
		for ( int r = 0; r < 3; r++ )
		{
			if ( gEngfuncs.pfnGetCvarFloat( (char *)s_Rates[r].cvar ) < s_Rates[r].value )
				gEngfuncs.Cvar_SetValue( (char *)s_Rates[r].cvar, s_Rates[r].value );
		}
		gEngfuncs.pfnClientCmd( s_szCoopConnect );
	}
	if ( s_dwCoopShot && (long)( GetTickCount() - s_dwCoopShot ) >= 0 )
	{
		s_dwCoopShot = 0;
		gEngfuncs.pfnClientCmd( "rc_coop_status;status;screenshot\n" );
	}
	if ( s_dwCoopQuit && (long)( GetTickCount() - s_dwCoopQuit ) >= 0 )
	{
		s_dwCoopQuit = 0;
		gEngfuncs.pfnClientCmd( "rc_coop_status;quit\n" );
	}
}

void RcClient_Init( void )
{
	g_RcAutosplit.version = 1;
	g_RcAutosplit.cutscene = &gHUD.m_BlackBar_guess.m_iOn_guess;
	// B312: retail's code at these addresses (read in client.dll 1.6a) is lastDrawTime and the start of READ_STRING's
	// buffer; ReCoF's first block pointed at decaltextures[0] + 8 and at the buffer + 0x58, from wrong names in the
	// old retail address map, and the values never changed
	g_RcAutosplit.igt = &lastDrawTime;
	g_RcAutosplit.music = READ_STRING();	// no message is being read here, so this only returns the buffer
	g_RcAutosplit.savestate = &gHUD.m_flSaveHackTime_guess;
	strcpy( g_RcAutosplit.magic, "ReCoF_AUTO" );
	strcat( g_RcAutosplit.magic, "SPLIT" );

	gEngfuncs.pfnAddCommand( "+rc_ducktap", IN_RcDucktapDown );
	gEngfuncs.pfnAddCommand( "-rc_ducktap", IN_RcDucktapUp );
	gEngfuncs.pfnAddCommand( "rc_vidrebuild", RcVidRebuild_f );	// B307

	// not archived: rc_hlmovement turns it on, and a saved 1 would keep it on in the next game without it
	rc_autojump = gEngfuncs.pfnRegisterVariable( "rc_autojump", "0", FCVAR_ARCHIVE );	// B332: saved, as the options set it
	rc_ducktap_priority = gEngfuncs.pfnRegisterVariable( "rc_ducktap_priority", "1", FCVAR_ARCHIVE );
	rc_jump_priority = gEngfuncs.pfnRegisterVariable( "rc_jump_priority", "0", FCVAR_ARCHIVE );

	RcSpeedrun_Init();	// rc_speedrun.cpp: the speedometer, the HUD numbers, the information panel and the run timer
	RcSmooth_Init();	// rc_smooth.cpp (B315): smooth frames
	RcDiscord_Init();	// rc_discord.cpp (B316): Discord Rich Presence
	RcPractice_Init();	// rc_practice.cpp (B317): wallhack, novis, disable_vgui
	RcPrefetch_Start();	// B363: the files the HUD reads during play, once in the background
	RcMusic_Init();		// mp3.cpp (B364): rc_music_thread
	// story co-op on the next map with more than one slot (hl.dll reads it by name; registered here too so that
	// +rc_coop 1 on the command line works before the first map loads hl.dll)
	gEngfuncs.pfnRegisterVariable( "rc_coop", "0", 0 );
	gEngfuncs.pfnRegisterVariable( "rc_player_collide", "1", FCVAR_ARCHIVE );	// B500: the host's, hl.dll reads it by name
	gEngfuncs.pfnRegisterVariable( "rc_coop_left", "0", 0 );
	// B394: story co-op proximity voice (hw.dll rc_coop.c RcVoice_*): 1 a teammate's voice comes from where he stands;
	// it falls silent at rc_voice_range units, and behind a wall it is muffled at rc_voice_wall percent of its volume
	// B399: the menus' size in percent on top of the size for the screen (GameUI.dll RcTheme.cpp RcUiScale)
	gEngfuncs.pfnRegisterVariable( "rc_menu_scale", "100", FCVAR_ARCHIVE );
	gEngfuncs.pfnRegisterVariable( "rc_menu_text", "0", FCVAR_ARCHIVE );	// B405, B410: 1 for the text main menu
	gEngfuncs.pfnRegisterVariable( "rc_voice_3d", "1", FCVAR_ARCHIVE );
	gEngfuncs.pfnRegisterVariable( "rc_voice_range", "1400", FCVAR_ARCHIVE );
	gEngfuncs.pfnRegisterVariable( "rc_voice_wall", "10", FCVAR_ARCHIVE );	// hw.dll sets it when the user disconnects (RcCoopReconnect)
	RcVoice_Init();	// B422: push to talk or open mic, the microphone
	gEngfuncs.pfnAddCommand( "rc_coop_host", RcCoopHost_f );	// co-op spike: Steam relay networking
	gEngfuncs.pfnAddCommand( "rc_coop_join", RcCoopJoin_f );
	gEngfuncs.pfnAddCommand( "rc_coop_status", RcCoopStatus_f );
	gEngfuncs.pfnAddCommand( "rc_coop_autotest", RcCoopAutotest_f );
	gEngfuncs.pfnAddCommand( "rc_coop_start", RcCoopStart_f );
	gEngfuncs.pfnAddCommand( "rc_coop_continue", RcCoopContinue_f );	// B393
	gEngfuncs.pfnAddCommand( "rc_coop_mp", RcCoopMp_f );	// B498
	gEngfuncs.pfnAddCommand( "rc_coop_menu", RcCoopMenu_Open );	// B393: the main menu's Story Co-op panel
	gEngfuncs.pfnAddCommand( "rc_options", RcOptions_f );	// B397
	gEngfuncs.pfnAddCommand( "rc_coop_launchoptions", RcCoopMenu_LaunchOptions );	// B395
	gEngfuncs.pfnAddCommand( "rc_coop_invite", RcCoopInvite_f );
	gEngfuncs.pfnAddCommand( "rc_coop_leave", RcCoopLeave_f );
	gEngfuncs.pfnAddCommand( "connect_lobby", RcConnectLobby_f );	// B406
	gEngfuncs.pfnAddCommand( "rc_coop_lobby", RcCoopMenu_Lobby );	// B408
	gEngfuncs.pfnAddCommand( "rc_lobby", RcLobby_f );
	RcSteamNet_InitCallbacks( RcCoopSteamJoin );	// Join Game and invites from Steam

	// B319: tells a multiplayer server this is ReCoF, through the private userinfo key _recof (sent with the connect
	// request, and again when it changes); servers take ReCoF players only (hl.dll ClientConnect, rc_require_recof)
	gEngfuncs.PlayerInfo_SetValueForKey( "_recof", RECOF_VERSION );

	rc_rawinput = gEngfuncs.pfnRegisterVariable( "rc_rawinput", "2", FCVAR_ARCHIVE );
	rc_vsync = gEngfuncs.pfnRegisterVariable( "rc_vsync", "0", FCVAR_ARCHIVE );	// B304
	rc_omgbruh_sound = gEngfuncs.pfnRegisterVariable( "rc_omgbruh_sound", "0", FCVAR_ARCHIVE );	// B318

	rc_viewmodel_fov = gEngfuncs.pfnRegisterVariable( "rc_viewmodel_fov", "0", FCVAR_ARCHIVE );
	rc_viewmodel_ofs_forward = gEngfuncs.pfnRegisterVariable( "rc_viewmodel_ofs_forward", "0", FCVAR_ARCHIVE );
	rc_viewmodel_ofs_right = gEngfuncs.pfnRegisterVariable( "rc_viewmodel_ofs_right", "0", FCVAR_ARCHIVE );
	rc_viewmodel_ofs_up = gEngfuncs.pfnRegisterVariable( "rc_viewmodel_ofs_up", "0", FCVAR_ARCHIVE );

	rc_crosshair = gEngfuncs.pfnRegisterVariable( "rc_crosshair", "1", FCVAR_ARCHIVE );
	rc_cross = gEngfuncs.pfnRegisterVariable( "rc_cross", "0", FCVAR_ARCHIVE );
	rc_cross_color = gEngfuncs.pfnRegisterVariable( "rc_cross_color", "0 255 0", FCVAR_ARCHIVE );
	rc_cross_alpha = gEngfuncs.pfnRegisterVariable( "rc_cross_alpha", "255", FCVAR_ARCHIVE );
	rc_cross_thickness = gEngfuncs.pfnRegisterVariable( "rc_cross_thickness", "2", FCVAR_ARCHIVE );
	rc_cross_size = gEngfuncs.pfnRegisterVariable( "rc_cross_size", "10", FCVAR_ARCHIVE );
	rc_cross_gap = gEngfuncs.pfnRegisterVariable( "rc_cross_gap", "3", FCVAR_ARCHIVE );
	rc_cross_outline = gEngfuncs.pfnRegisterVariable( "rc_cross_outline", "0", FCVAR_ARCHIVE );
	rc_cross_circle_radius = gEngfuncs.pfnRegisterVariable( "rc_cross_circle_radius", "0", FCVAR_ARCHIVE );
	rc_cross_dot_color = gEngfuncs.pfnRegisterVariable( "rc_cross_dot_color", "", FCVAR_ARCHIVE );
	rc_cross_dot_size = gEngfuncs.pfnRegisterVariable( "rc_cross_dot_size", "0", FCVAR_ARCHIVE );
	rc_cross_top_line = gEngfuncs.pfnRegisterVariable( "rc_cross_top_line", "1", FCVAR_ARCHIVE );
	rc_cross_bottom_line = gEngfuncs.pfnRegisterVariable( "rc_cross_bottom_line", "1", FCVAR_ARCHIVE );
	rc_cross_left_line = gEngfuncs.pfnRegisterVariable( "rc_cross_left_line", "1", FCVAR_ARCHIVE );
	rc_cross_right_line = gEngfuncs.pfnRegisterVariable( "rc_cross_right_line", "1", FCVAR_ARCHIVE );

	RcProf_Init();
	RcBones_Init();	// ReCoF (B339)
}

//-----------------------------------------------------------------------------
// rc_hlmovement: the server's movement mode, as client prediction sees it
//-----------------------------------------------------------------------------
int RcHLMovement( void )
{
	const char *value = gEngfuncs.PhysInfo_ValueForKey( "hlm" );

	return value && atoi( value ) != 0;
}

//-----------------------------------------------------------------------------
// called at the end of CL_CreateMove: +rc_ducktap and rc_autojump work on the buttons of the command, as
// RebuiltSimon's +rs_ducktap and rs_autojump do. With rc_jump_priority, holding jump pauses +rc_ducktap;
// with rc_ducktap_priority, holding +rc_ducktap pauses rc_autojump
//-----------------------------------------------------------------------------
void RcClient_CreateMove( usercmd_t *cmd )
{
	// rc_hlmovement: the sprint key does nothing in Half-Life movement (the server strips it too)
	if ( RcHLMovement() )
		cmd->buttons &= ~IN_RUN;

	if ( !pmove || !rc_autojump )
		return;

	int bOnGround = pmove->onground != -1;
	int bDucktap = ( in_rcducktap.state & 1 ) && !( rc_jump_priority->value && ( cmd->buttons & IN_JUMP ) );

	if ( bDucktap )
	{
		// press duck for one frame on the ground, release it on the next, so each landing ducktaps
		if ( pmove->movetype == MOVETYPE_WALK && bOnGround && s_bDucktapReleased )
		{
			cmd->buttons |= IN_DUCK;
			s_bDucktapReleased = 0;
		}
		else
		{
			cmd->buttons &= ~IN_DUCK;
			s_bDucktapReleased = 1;
		}
	}

	// release jump in the air, so holding it jumps again on landing
	if ( rc_autojump->value && pmove->movetype == MOVETYPE_WALK && !( rc_ducktap_priority->value && bDucktap ) )
	{
		if ( !bOnGround && pmove->waterlevel <= 1 )
			cmd->buttons &= ~IN_JUMP;
	}
}

//-----------------------------------------------------------------------------
// rc_cross: a crosshair set up by console variables, as BunnymodXT's bxt_cross: four lines around a gap, a dot
// and a circle, each with an optional black outline. It is drawn with alpha blending in OpenGL, since FillRGBA
// adds its color and cannot draw dark ones. It shows with or without a weapon and hides where the Cry of Fear
// crosshair does: with the HUD, in cutscenes, in iron sights and in a scope
//-----------------------------------------------------------------------------
static void RcCrossColor( const char *text, int *color )
{
	int r, g, b;

	if ( sscanf( text, "%d %d %d", &r, &g, &b ) == 3 )
	{
		color[0] = r;
		color[1] = g;
		color[2] = b;
	}
}

static void RcCrossRect( float x0, float y0, float x1, float y1, float grow )
{
	gl.glVertex3f( x0 - grow, y0 - grow, 0 );
	gl.glVertex3f( x1 + grow, y0 - grow, 0 );
	gl.glVertex3f( x1 + grow, y1 + grow, 0 );
	gl.glVertex3f( x0 - grow, y1 + grow, 0 );
}

static void RcCrossRing( float cx, float cy, float inner, float outer )
{
	int segments = (int)( outer * 1.5 );

	if ( segments < 16 )
		segments = 16;
	else if ( segments > 180 )
		segments = 180;

	if ( inner < 0 )
		inner = 0;

	gl.glBegin( GL_TRIANGLE_STRIP );

	for ( int i = 0; i <= segments; i++ )
	{
		float a = i * 2 * M_PI / segments;

		gl.glVertex3f( cx + cos( a ) * outer, cy + sin( a ) * outer, 0 );
		gl.glVertex3f( cx + cos( a ) * inner, cy + sin( a ) * inner, 0 );
	}

	gl.glEnd();
}

static void RcDrawCross( void )
{
	if ( !rc_cross->value || !gl.IsGLAllowed() || !gHUD.m_pCvarDraw->value )
		return;

	if ( gHUD.m_fPlayerDead || ( gHUD.m_iHideHUDDisplay & ( HIDEHUD_WEAPONS | HIDEHUD_ALL | HIDEHUD_HEALTH ) ) || g_iVisibleMouse )
		return;

	// a cutscene, the view on another entity, iron sights or a scope
	if ( gHUD.m_BlackBar_guess.m_iOn_guess || IsViewEntityActive_guess() || gHUD.m_bIronSights_guess ||
		( gViewPort && gViewPort->m_pScope_guess && gViewPort->m_pScope_guess->m_iScope_guess ) )
		return;

	int color[3] = { 0, 255, 0 };
	RcCrossColor( rc_cross_color->string, color );

	int dot[3] = { color[0], color[1], color[2] };
	RcCrossColor( rc_cross_dot_color->string, dot );

	int alpha = (int)rc_cross_alpha->value;
	alpha = alpha < 0 ? 0 : ( alpha > 255 ? 255 : alpha );

	float cx = ScreenWidth / 2;
	float cy = ScreenHeight / 2;
	float half = rc_cross_thickness->value / 2;
	float size = rc_cross_size->value;
	float gap = rc_cross_gap->value;
	float outline = rc_cross_outline->value > 0 ? rc_cross_outline->value : 0;
	float dothalf = rc_cross_dot_size->value / 2;
	float radius = rc_cross_circle_radius->value;

	// the lines, top, bottom, left and right
	float lines[4][4];
	int count = 0;

	if ( half > 0 && size > 0 )
	{
		if ( rc_cross_top_line->value )
		{
			lines[count][0] = cx - half; lines[count][1] = cy - gap - size; lines[count][2] = cx + half; lines[count][3] = cy - gap;
			count++;
		}

		if ( rc_cross_bottom_line->value )
		{
			lines[count][0] = cx - half; lines[count][1] = cy + gap; lines[count][2] = cx + half; lines[count][3] = cy + gap + size;
			count++;
		}

		if ( rc_cross_left_line->value )
		{
			lines[count][0] = cx - gap - size; lines[count][1] = cy - half; lines[count][2] = cx - gap; lines[count][3] = cy + half;
			count++;
		}

		if ( rc_cross_right_line->value )
		{
			lines[count][0] = cx + gap; lines[count][1] = cy - half; lines[count][2] = cx + gap + size; lines[count][3] = cy + half;
			count++;
		}
	}

	GLboolean bAlphaTest = gl.glIsEnabled( GL_ALPHA_TEST );

	gl.glDisable( GL_TEXTURE_2D );
	gl.glDisable( GL_ALPHA_TEST );
	gl.glEnable( GL_BLEND );
	gl.glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	gl.glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

	// the outlines first, under everything
	if ( outline > 0 )
	{
		gl.glColor4ub( 0, 0, 0, alpha );

		if ( radius > 0 && half > 0 )
			RcCrossRing( cx, cy, radius - half - outline, radius + half + outline );

		gl.glBegin( GL_QUADS );

		for ( int i = 0; i < count; i++ )
			RcCrossRect( lines[i][0], lines[i][1], lines[i][2], lines[i][3], outline );

		if ( dothalf > 0 )
			RcCrossRect( cx - dothalf, cy - dothalf, cx + dothalf, cy + dothalf, outline );

		gl.glEnd();
	}

	gl.glColor4ub( color[0], color[1], color[2], alpha );

	if ( radius > 0 && half > 0 )
		RcCrossRing( cx, cy, radius - half, radius + half );

	gl.glBegin( GL_QUADS );

	for ( int i = 0; i < count; i++ )
		RcCrossRect( lines[i][0], lines[i][1], lines[i][2], lines[i][3], 0 );

	if ( dothalf > 0 )
	{
		gl.glColor4ub( dot[0], dot[1], dot[2], alpha );
		RcCrossRect( cx - dothalf, cy - dothalf, cx + dothalf, cy + dothalf, 0 );
	}

	gl.glEnd();

	// what FillRGBA leaves behind
	gl.glColor4ub( 255, 255, 255, 255 );
	gl.glEnable( GL_TEXTURE_2D );
	gl.glDisable( GL_BLEND );

	if ( bAlphaTest )
		gl.glEnable( GL_ALPHA_TEST );
}

void RcScoreFrame( void );		// Credits_guess.cpp (B309)

//-----------------------------------------------------------------------------
// called from CHud::Redraw: the end of game score check, the custom crosshair, the movement mode tag, and the
// speedometer, information panel and run timer of rc_speedrun.cpp
//-----------------------------------------------------------------------------
void RcClient_Redraw( float flTime, int intermission )
{
	RcScoreFrame();

	if ( intermission || !rc_cross )
		return;

	RcDrawCross();

	// the tag keeps runs in Half-Life movement visible
	if ( RcHLMovement() )
	{
		gEngfuncs.pfnDrawSetTextColor( 1.0, 0.7, 0.2 );
		gEngfuncs.pfnDrawConsoleString( 8, 8, (char *)"HL movement" );
	}

	RcSpeedrun_Redraw();
}

//-----------------------------------------------------------------------------
// rc_crosshair 0 hides the red Cry of Fear crosshair and keeps the ammo display; cl_crosshair 0 hides both
//-----------------------------------------------------------------------------
int RcCrosshairOn( void )
{
	return !rc_crosshair || rc_crosshair->value != 0;
}

//-----------------------------------------------------------------------------
// rc_viewmodel_ofs_forward, _right and _up move the weapon model along the view, in units, as BunnymodXT's
// bxt_viewmodel_ofs_* do; the left hand item follows the weapon model. Iron sights ease them out over a
// quarter of a second, so the sights stay on the middle of the screen
//-----------------------------------------------------------------------------
void RcViewmodelOffset( float *origin, const float *forward, const float *right, const float *up, float frametime )
{
	if ( !rc_viewmodel_ofs_forward )
		return;

	float target = gHUD.m_bIronSights_guess ? 0 : 1;
	float step = frametime * 4;

	if ( s_flViewmodelOfsScale < target )
	{
		s_flViewmodelOfsScale += step;
		if ( s_flViewmodelOfsScale > target )
			s_flViewmodelOfsScale = target;
	}
	else if ( s_flViewmodelOfsScale > target )
	{
		s_flViewmodelOfsScale -= step;
		if ( s_flViewmodelOfsScale < target )
			s_flViewmodelOfsScale = target;
	}

	float f = rc_viewmodel_ofs_forward->value * s_flViewmodelOfsScale;
	float r = rc_viewmodel_ofs_right->value * s_flViewmodelOfsScale;
	float u = rc_viewmodel_ofs_up->value * s_flViewmodelOfsScale;

	for ( int i = 0; i < 3; i++ )
		origin[i] += forward[i] * f + right[i] * r + up[i] * u;
}

//-----------------------------------------------------------------------------
// rc_viewmodel_fov draws the weapon model and the left hand item with this horizontal field of view instead of
// the view's, as BunnymodXT's bxt_viewmodel_fov does (0 keeps the view's). The engine's projection is a
// symmetric frustum, so only its x and y scales change; the depth stays as the engine set it
//-----------------------------------------------------------------------------
int RcViewmodelFovBegin( void )
{
	if ( !rc_viewmodel_fov || rc_viewmodel_fov->value <= 0 || !gl.IsGLAllowed() )
		return 0;

	float fov = rc_viewmodel_fov->value;

	if ( fov > 170 )
		fov = 170;

	GLfloat m[16];
	GLint mode;

	gl.glGetFloatv( GL_PROJECTION_MATRIX, m );

	if ( m[0] <= 0 || m[11] != -1 )	// not a perspective projection
		return 0;

	float scale = 1.0 / ( tan( fov * M_PI / 360.0 ) * m[0] );
	m[0] *= scale;
	m[5] *= scale;

	gl.glGetIntegerv( GL_MATRIX_MODE, &mode );
	gl.glMatrixMode( GL_PROJECTION );
	gl.glPushMatrix();
	gl.glLoadMatrixf( m );
	gl.glMatrixMode( mode );

	return 1;
}

void RcViewmodelFovEnd( int on )
{
	if ( !on )
		return;

	GLint mode;

	gl.glGetIntegerv( GL_MATRIX_MODE, &mode );
	gl.glMatrixMode( GL_PROJECTION );
	gl.glPopMatrix();
	gl.glMatrixMode( mode );
}

//-----------------------------------------------------------------------------
// B184 rc_rawinput: raw mouse counts (WM_INPUT) instead of the cursor movement, which Windows accelerates
// and which the game forced to "enhance pointer precision".
// 1 reads them on the game window. Its procedure belongs to the engine, so the client chains its own in
// front of it the first time the mouse moves in game, and the counts come in when the engine reads its
// window messages, once a frame.
// 2 reads them the way RInput does: a thread of its own, with a window of its own, adds up each count as
// it arrives, and the mouse move takes everything that came in up to that moment.
//-----------------------------------------------------------------------------
#define RC_RAW_TAKE		( WM_APP + 1 )	// rc_rawinput 2: the input thread takes the raw mouse

extern "C" IMAGE_DOS_HEADER __ImageBase;	// client.dll's module handle, for the window class

static const char	s_szRawClass[] = "ReCoF raw input";

// adds up the counts of one WM_INPUT
static void RcRawAdd( LPARAM lParam )
{
	RAWINPUT ri;
	UINT size = sizeof( ri );

	if ( g_iVisibleMouse )
		return;	// a menu has the cursor; these counts would turn the view once it closes

	if ( GetRawInputData( (HRAWINPUT)lParam, RID_INPUT, &ri, &size, sizeof( RAWINPUTHEADER ) ) != (UINT)-1 &&
		 ri.header.dwType == RIM_TYPEMOUSE && !( ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE ) )
	{
		InterlockedExchangeAdd( &s_lRawDX, ri.data.mouse.lLastX );
		InterlockedExchangeAdd( &s_lRawDY, ri.data.mouse.lLastY );
	}
}

// rc_rawinput 1: in front of the engine's procedure of the game window
static LRESULT CALLBACK RcRawWndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
	if ( msg == WM_INPUT )
		RcRawAdd( lParam );

	return CallWindowProcA( s_pfnRawOldProc, hWnd, msg, wParam, lParam );
}

// gives the raw mouse of the process to hWnd; NULL with RIDEV_REMOVE gives it up
static BOOL RcRawRegister( HWND hWnd, DWORD dwFlags )
{
	RAWINPUTDEVICE rid;

	rid.usUsagePage = 1;	// generic desktop
	rid.usUsage = 2;		// mouse
	rid.dwFlags = dwFlags;
	rid.hwndTarget = hWnd;

	return RegisterRawInputDevices( &rid, 1, sizeof( rid ) );
}

// rc_rawinput 2: the window of the input thread, a message-only window that is never shown
static LRESULT CALLBACK RcRawThreadWndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
	if ( msg == WM_INPUT )
	{
		RcRawAdd( lParam );	// DefWindowProc below frees the input
	}
	else if ( msg == RC_RAW_TAKE )
	{
		// RIDEV_INPUTSINK, as a message-only window is never in the foreground. What comes in while the
		// game is in the background is dropped when the game gets the mouse back (RcRawMouseReset)
		if ( !RcRawRegister( hWnd, RIDEV_INPUTSINK ) )
			s_bRawFailed = 1;	// keep the cursor movement

		return 0;
	}

	return DefWindowProcA( hWnd, msg, wParam, lParam );
}

// rc_rawinput 2: the input thread
static DWORD WINAPI RcRawThread( LPVOID pParam )
{
	HWND hWnd;
	MSG msg;

	hWnd = CreateWindowExA( 0, s_szRawClass, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, (HINSTANCE)&__ImageBase, NULL );
	if ( !hWnd )
	{
		s_bRawFailed = 1;	// keep the cursor movement
		return 0;
	}

	// a count taken late turns the view one frame later, so this thread goes before the game's
	SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_HIGHEST );

	RcRawThreadWndProc( hWnd, RC_RAW_TAKE, 0, 0 );
	s_hRawThreadWnd = hWnd;

	while ( GetMessageA( &msg, NULL, 0, 0 ) > 0 )
		DispatchMessageA( &msg );

	s_hRawThreadWnd = NULL;
	DestroyWindow( hWnd );
	return 0;
}

// gives the raw mouse to the window of rc_rawinput 1 or 2; the counts start over
static void RcRawTake( int mode )
{
	InterlockedExchange( &s_lRawDX, 0 );
	InterlockedExchange( &s_lRawDY, 0 );
	s_iRawMode = mode;

	if ( mode == 2 )
	{
		if ( s_hRawThread )
		{
			// back from the game window or from RInput; a thread that has no window yet takes it itself
			if ( s_hRawThreadWnd )
				PostMessageA( s_hRawThreadWnd, RC_RAW_TAKE, 0, 0 );
			return;
		}

		WNDCLASSA wc;
		memset( &wc, 0, sizeof( wc ) );
		wc.lpfnWndProc = RcRawThreadWndProc;
		wc.hInstance = (HINSTANCE)&__ImageBase;
		wc.lpszClassName = s_szRawClass;

		if ( !RegisterClassA( &wc ) )
		{
			UnregisterClassA( s_szRawClass, wc.hInstance );	// left by a client.dll whose thread did not stop
			if ( !RegisterClassA( &wc ) )
			{
				s_bRawFailed = 1;	// keep the cursor movement
				return;
			}
		}

		s_hRawThread = CreateThread( NULL, 0, RcRawThread, NULL, 0, &s_dwRawThreadId );
		if ( !s_hRawThread )
			s_bRawFailed = 1;
		return;
	}

	if ( !s_hRawWnd )
	{
		// the engine's window class (sys_sdlwind.cpp); skip another game's window
		HWND hWnd = NULL;
		DWORD pid;

		while ( ( hWnd = FindWindowExA( NULL, hWnd, "Valve001", NULL ) ) != NULL )
		{
			if ( GetWindowThreadProcessId( hWnd, &pid ) && pid == GetCurrentProcessId() )
				break;
		}

		if ( !hWnd )
		{
			s_bRawFailed = 1;	// keep the cursor movement
			return;
		}

		s_pfnRawOldProc = (WNDPROC)SetWindowLongPtrA( hWnd, GWLP_WNDPROC, (LONG_PTR)RcRawWndProc );
		s_hRawWnd = hWnd;
	}

	if ( !RcRawRegister( s_hRawWnd, 0 ) )
		s_bRawFailed = 1;	// keep the cursor movement
}

int RcRawInputOn( void )
{
	return rc_rawinput && rc_rawinput->value && !s_bRawFailed;
}

// called from IN_ActivateMouse: drop what came in while the console or a menu had the mouse
void RcRawMouseReset( void )
{
	InterlockedExchange( &s_lRawDX, 0 );
	InterlockedExchange( &s_lRawDY, 0 );
}

// called from IN_MouseMove: replaces *mx and *my with the raw counts since the last call
void RcRawMouse( int *mx, int *my )
{
	int mode;

	if ( !RcRawInputOn() )
		return;

	// B300: Windows gives the raw mouse of a process to one window, and RInput takes it for its own.
	// RInput hands its raw counts out through GetCursorPos, so *mx and *my already hold them: leave the
	// mouse to it, and take it back if it goes
	if ( GetModuleHandleA( "RInput.dll" ) )
	{
		if ( !s_bRawRInput )
			gEngfuncs.Con_Printf( "RInput is loaded: rc_rawinput leaves the mouse to it\n" );

		s_bRawRInput = 1;
		s_iRawMode = 0;
		return;
	}

	s_bRawRInput = 0;
	mode = ( rc_rawinput->value >= 2 ) ? 2 : 1;

	if ( s_iRawMode != mode )
	{
		RcRawTake( mode );
		return;	// the cursor movement this once
	}

	if ( mode == 2 && !s_hRawThreadWnd )
		return;	// the thread does not have the raw mouse yet

	*mx = InterlockedExchange( &s_lRawDX, 0 );
	*my = InterlockedExchange( &s_lRawDY, 0 );
}

// called from IN_Shutdown (B299). The engine unloads client.dll right after, so nothing of it may run on
// the input thread or stay in the game window's procedure: the engine destroys the window after the
// unload, and destroying it still runs its procedure. Left in front of the engine's, RcRawWndProc crashed
// the game on quit (Windows logs the fault in client.dll_unloaded), and a video mode restart goes the
// same way
void RcRawMouseShutdown( void )
{
	int i;

	if ( s_hRawThread )
	{
		// WM_QUIT needs the thread's message queue, which it has once it made its window
		for ( i = 0; i < 40 && WaitForSingleObject( s_hRawThread, 0 ) == WAIT_TIMEOUT; i++ )
		{
			PostThreadMessageA( s_dwRawThreadId, WM_QUIT, 0, 0 );
			WaitForSingleObject( s_hRawThread, 50 );
		}

		CloseHandle( s_hRawThread );
		s_hRawThread = NULL;
		UnregisterClassA( s_szRawClass, (HINSTANCE)&__ImageBase );
	}

	// another program that chained its procedure in front of this one would be cut off: leave it then
	if ( s_hRawWnd && GetWindowLongPtrA( s_hRawWnd, GWLP_WNDPROC ) == (LONG_PTR)RcRawWndProc )
		SetWindowLongPtrA( s_hRawWnd, GWLP_WNDPROC, (LONG_PTR)s_pfnRawOldProc );

	if ( s_iRawMode )
		RcRawRegister( NULL, RIDEV_REMOVE );	// not while RInput has the raw mouse (B300)

	s_hRawWnd = NULL;
	s_iRawMode = 0;
}

//-----------------------------------------------------------------------------
// B318 rc_omgbruh_sound: the weapon sounds that irrKlang plays (mp3.cpp CMP3::PlaySFX_guess) get a random playback
// speed between 0.9 and 1.1, which also moves their pitch at every shot. 1 plays every shot as recorded, which
// replacement sounds such as Cry of Fear 1.5's gun sounds need; 0 keeps retail's variation
float RcWeaponSoundSpeed( void )
{
	if ( rc_omgbruh_sound && rc_omgbruh_sound->value )
		return 1.0f;
	return gEngfuncs.pfnRandomFloat( -0.1, 0.1 ) + 1.0;
}

// B328: the player's own iron sights toggle and subtitle language, for multiplayer servers (hl.dll
// UTIL_PlayerSetting_rc): mirrored into the private userinfo keys _rcis and _rcsl whenever they change. Both cvars
// belong to the game DLL, which the client loads at start; a missing one is not sent.
static void RcMirrorSetting( const char *cvar, const char *key, float *last )
{
	cvar_t *p = gEngfuncs.pfnGetCvarPointer( (char *)cvar );
	if ( !p || p->value == *last )
		return;

	*last = p->value;
	char value[16];
	_snprintf( value, sizeof( value ), "%g", p->value );
	value[sizeof( value ) - 1] = 0;
	gEngfuncs.PlayerInfo_SetValueForKey( key, value );
}

void RcUserinfoFrame( void )
{
	static float s_flIronsights = -12345.0f;
	static float s_flLanguage = -12345.0f;

	RcMirrorSetting( "ironsights_toggle", "_rcis", &s_flIronsights );
	RcMirrorSetting( "cof_subtitlelanguage", "_rcsl", &s_flLanguage );
}

// B304 rc_vsync: the engine never sets the swap interval, so the driver's default applied, which is vsync
// on, and only the driver's control panel could turn it off. 0 turns it off, 1 on. It is set when the value
// or the GL context changes; a setting forced in the driver's control panel still wins
//-----------------------------------------------------------------------------
typedef BOOL (WINAPI *RcSwapInterval_t)( int interval );

void RcVsyncFrame( void )
{
	static HGLRC s_hContext;
	static int s_iApplied = -1;
	static RcSwapInterval_t s_pfnSwapInterval;

	if ( !rc_vsync )
		return;

	HGLRC hContext = wglGetCurrentContext();
	if ( !hContext )
		return;

	int iWant = rc_vsync->value ? 1 : 0;
	if ( hContext == s_hContext && iWant == s_iApplied )
		return;

	if ( hContext != s_hContext )
	{
		s_hContext = hContext;
		s_pfnSwapInterval = (RcSwapInterval_t)wglGetProcAddress( "wglSwapIntervalEXT" );
		if ( !s_pfnSwapInterval )
			gEngfuncs.Con_Printf( "rc_vsync: the driver has no wglSwapIntervalEXT, so vsync stays as the driver sets it\n" );
	}

	s_iApplied = iWant;
	if ( s_pfnSwapInterval )
		s_pfnSwapInterval( iWant );
}
