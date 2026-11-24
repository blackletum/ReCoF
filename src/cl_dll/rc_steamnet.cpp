// ReCoF co-op spike (not a feature yet): carries the engine's UDP traffic through Steam's relay network.
//
// client.dll asks the game's own 2012 steam_api.dll for today's networking interfaces (ISteamClient012 slot 12,
// GetISteamGenericInterface) and replaces three entries of hw.dll's import table in memory: sendto and recvfrom of
// WSOCK32. Datagrams to a Steam FakeIP (169.254.x.x) go through an ISteamNetworkingFakeUDPPort; everything else goes
// to the real socket, so no hw.dll file byte changes and single player never reaches this code.
//
// The host asks Steam for a FakeIP and shows it; a friend runs rc_coop_join <FakeIP:port>. With sv_lan 1 the host
// presents each friend to the engine as 127.77.0.<n> (a reserved address the LAN check accepts) and translates back.
//
// This file includes the Steamworks SDK headers only (src/external/steamworks_sdk), never the game's headers.
#include <winsock.h>
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "steamworks_sdk/steam/steamnetworkingtypes.h"
#include "steamworks_sdk/steam/isteamnetworkingsockets.h"
#include "steamworks_sdk/steam/isteamnetworkingutils.h"
#include "steamworks_sdk/steam/steamnetworkingfakeip.h"
#include "steamworks_sdk/steam/isteamfriends.h"
#include "steamworks_sdk/steam/isteammatchmaking.h"	// B406
#include "steamworks_sdk/steam/isteamuser.h"
#include "rc_steamnet.h"

typedef void *( __cdecl *RcSteamClient_t )( void );
typedef int ( __cdecl *RcSteamHandle_t )( void );
typedef void ( __cdecl *RcSteamVoid_t )( void );
typedef void ( __cdecl *RcSteamRegister_t )( class CCallbackBase *pCallback, int iCallback );
typedef void *( __thiscall *RcGetGenericInterface_t )( void *self, int hUser, int hPipe, const char *pchVersion );
typedef int ( PASCAL *RcSendto_t )( SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to, int tolen );
typedef int ( PASCAL *RcRecvfrom_t )( SOCKET s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen );

enum { RCSN_OFF, RCSN_HOST, RCSN_JOIN };

static RcSteamPrint_t s_pfnPrint;
static ISteamNetworkingUtils *s_pUtils;
static ISteamNetworkingSockets *s_pSockets;
static ISteamNetworkingFakeUDPPort *s_pPort;
static ISteamFriends *s_pFriends;
static bool s_bJoinRegistered;	// the join request callback is registered on client.dll's Steam connection
static char s_szConnect[64];	// the rich presence connect value, "+rc_coop_join a.b.c.d:port"
static RcSteamVoid_t s_pfnRunCallbacks;
static int s_iMode;
static bool s_bMap127;
static int s_iIPState;	// host: 1 waits for the relay network, 2 waits for the FakeIP, 3 has it
static int s_iIPTries;
static DWORD s_dwIPRetryAt, s_dwIPStart;
static SteamNetworkingIPAddr s_FakeIP;
static int s_iServerPort;
static SOCKET s_sockGame = INVALID_SOCKET;	// host: the server socket; joiner: the socket that sent to the FakeIP
static RcSendto_t s_pfnSendto;
static RcRecvfrom_t s_pfnRecvfrom;
static long s_nIn, s_nOut, s_nSendFail, s_nSendFull;	// B480: s_nSendFull counts sends refused for a full queue
static unsigned long s_ulBytesIn, s_ulBytesOut;
static DWORD s_dwNextStats;
static CRITICAL_SECTION s_csLog;
static FILE *s_pLog;

// host, sv_lan 1: friends shown to the engine as 127.77.0.<1 + index>
struct RcPeer_t
{
	uint32 ip;
	uint16 port;
};
static RcPeer_t s_Peers[32];
static int s_nPeers;

static void RcLog( const char *fmt, ... )
{
	char buf[1024];
	va_list ap;
	va_start( ap, fmt );
	_vsnprintf( buf, sizeof( buf ) - 1, fmt, ap );
	va_end( ap );
	buf[sizeof( buf ) - 1] = 0;
	EnterCriticalSection( &s_csLog );
	if ( !s_pLog )
	{
		char path[MAX_PATH];
		GetModuleFileNameA( NULL, path, sizeof( path ) );
		char *slash = strrchr( path, '\\' );
		if ( slash )
			strcpy( slash + 1, "rc_coop_steam.log" );
		s_pLog = fopen( path, "w" );
	}
	if ( s_pLog )
	{
		fprintf( s_pLog, "[%10lu] %s\n", GetTickCount(), buf );
		fflush( s_pLog );
	}
	LeaveCriticalSection( &s_csLog );
}

// Steam's own lines can carry Steam IDs and real addresses: keep only the others (Steam calls this from its threads)
static void RcSteamDebug( ESteamNetworkingSocketsDebugOutputType nType, const char *pszMsg )
{
	if ( strstr( pszMsg, "steamid:" ) || strstr( pszMsg, "ip:" ) || strstr( pszMsg, "Ping location" ) )
		return;
	RcLog( "steam(%d): %s", (int)nType, pszMsg );
}

static void RcFormatIP( uint32 ip, uint16 port, char *out, int size )
{
	_snprintf( out, size, "%u.%u.%u.%u:%u", ( ip >> 24 ) & 255, ( ip >> 16 ) & 255, ( ip >> 8 ) & 255, ip & 255, port );
	out[size - 1] = 0;
}

static bool RcIsFake( uint32 ip )
{
	return ( ip >> 16 ) == ( ( 169u << 8 ) | 254u );
}

static bool RcIsMapped( uint32 ip )
{
	return ( ip >> 8 ) == ( ( 127u << 16 ) | ( 77u << 8 ) );
}

static int RcPeerIndex( uint32 ip, uint16 port )
{
	for ( int i = 0; i < s_nPeers; i++ )
	{
		if ( s_Peers[i].ip == ip && s_Peers[i].port == port )
			return i;
	}
	if ( s_nPeers >= 32 )
		return -1;
	s_Peers[s_nPeers].ip = ip;
	s_Peers[s_nPeers].port = port;
	char sz[32];
	RcFormatIP( ip, port, sz, sizeof( sz ) );
	RcLog( "new friend %d at %s", s_nPeers, sz );
	return s_nPeers++;
}

static bool RcIsServerSocket( SOCKET s )
{
	if ( s_sockGame != INVALID_SOCKET )
		return s == s_sockGame;
	struct sockaddr_in a;
	int len = sizeof( a );
	if ( getsockname( s, (struct sockaddr *)&a, &len ) || a.sin_family != AF_INET )
		return false;
	if ( ntohs( a.sin_port ) != s_iServerPort )
		return false;
	s_sockGame = s;
	RcLog( "server socket found (port %d)", s_iServerPort );
	return true;
}

static int PASCAL RcSendto( SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to, int tolen )
{
	if ( s_iMode != RCSN_OFF && s_pPort && to && tolen >= (int)sizeof( struct sockaddr_in ) && to->sa_family == AF_INET )
	{
		const struct sockaddr_in *sin = (const struct sockaddr_in *)to;
		uint32 ip = ntohl( sin->sin_addr.s_addr );
		uint16 port = ntohs( sin->sin_port );
		SteamNetworkingIPAddr dest;
		bool bOurs = false;
		if ( s_iMode == RCSN_HOST && RcIsMapped( ip ) )
		{
			int idx = (int)( ip & 255 ) - 1;
			if ( idx >= 0 && idx < s_nPeers )
			{
				dest.Clear();
				dest.SetIPv4( s_Peers[idx].ip, s_Peers[idx].port );
				bOurs = true;
			}
		}
		else if ( RcIsFake( ip ) )
		{
			dest.Clear();
			dest.SetIPv4( ip, port );
			bOurs = true;
			if ( s_iMode == RCSN_JOIN && s_sockGame == INVALID_SOCKET )
			{
				s_sockGame = s;
				RcLog( "client socket found" );
			}
		}
		if ( bOurs )
		{
			EResult r = s_pPort->SendMessageToFakeIP( dest, buf, (uint32)len, k_nSteamNetworkingSend_UnreliableNoNagle );
			if ( r == k_EResultOK )
			{
				s_nOut++;
				s_ulBytesOut += len;
			}
			else
			{
				if ( r == k_EResultLimitExceeded )
					s_nSendFull++;	// B480: Steam's send queue is full
				if ( s_nSendFail++ < 20 || ( r == k_EResultLimitExceeded && ( s_nSendFull & 255 ) == 1 ) )
					RcLog( "send of %d bytes failed: %d (%ld for a full queue)", len, (int)r, s_nSendFull );
			}
			return len;
		}
	}
	return s_pfnSendto( s, buf, len, flags, to, tolen );
}

static int PASCAL RcRecvfrom( SOCKET s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen )
{
	if ( s_iMode != RCSN_OFF && s_pPort && ( s_iMode == RCSN_HOST ? RcIsServerSocket( s ) : s == s_sockGame ) )
	{
		SteamNetworkingMessage_t *m;
		if ( s_pPort->ReceiveMessages( &m, 1 ) == 1 )
		{
			int n = m->m_cbSize < len ? m->m_cbSize : len;
			memcpy( buf, m->m_pData, n );
			const SteamNetworkingIPAddr *pFrom = m->m_identityPeer.GetIPAddr();
			uint32 ip = pFrom ? pFrom->GetIPv4() : 0;
			uint16 port = pFrom ? pFrom->m_port : 0;
			if ( s_iMode == RCSN_HOST && s_bMap127 && pFrom )
			{
				int idx = RcPeerIndex( ip, port );
				if ( idx >= 0 )
					ip = ( 127u << 24 ) | ( 77u << 16 ) | (uint32)( idx + 1 );
			}
			if ( from && fromlen && *fromlen >= (int)sizeof( struct sockaddr_in ) )
			{
				struct sockaddr_in *sin = (struct sockaddr_in *)from;
				memset( sin, 0, sizeof( *sin ) );
				sin->sin_family = AF_INET;
				sin->sin_addr.s_addr = htonl( ip );
				sin->sin_port = htons( port );
				*fromlen = sizeof( struct sockaddr_in );
			}
			s_nIn++;
			s_ulBytesIn += n;
			m->Release();
			return n;
		}
	}
	return s_pfnRecvfrom( s, buf, len, flags, from, fromlen );
}

// the WSOCK32 import of hw.dll for this ordinal (sendto 20, recvfrom 17), or the import now pointing at pCurrent
// (B422: rc_voice.cpp's waveInOpen)
void **RcFindImport( HMODULE hModule, const char *pszDll, int iOrdinal, void *pCurrent )
{
	BYTE *base = (BYTE *)hModule;
	IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
	IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)( base + dos->e_lfanew );
	IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if ( !dir.VirtualAddress )
		return NULL;
	for ( IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)( base + dir.VirtualAddress ); d->Name; d++ )
	{
		if ( _stricmp( (const char *)( base + d->Name ), pszDll ) )
			continue;
		IMAGE_THUNK_DATA *iat = (IMAGE_THUNK_DATA *)( base + d->FirstThunk );
		IMAGE_THUNK_DATA *names = d->OriginalFirstThunk ? (IMAGE_THUNK_DATA *)( base + d->OriginalFirstThunk ) : NULL;
		for ( int i = 0; iat[i].u1.Function; i++ )
		{
			if ( names && IMAGE_SNAP_BY_ORDINAL( names[i].u1.Ordinal ) && (int)IMAGE_ORDINAL( names[i].u1.Ordinal ) == iOrdinal )
				return (void **)&iat[i].u1.Function;
			if ( (void *)iat[i].u1.Function == pCurrent )
				return (void **)&iat[i].u1.Function;
		}
	}
	return NULL;
}

bool RcPatch( void **ppSlot, void *pNew, void **ppOld )
{
	DWORD old;
	if ( !VirtualProtect( ppSlot, sizeof( void * ), PAGE_READWRITE, &old ) )
		return false;
	*ppOld = *ppSlot;
	*ppSlot = pNew;
	VirtualProtect( ppSlot, sizeof( void * ), old, &old );
	return true;
}

static bool RcInstallHooks( void )
{
	if ( s_pfnSendto )
		return true;
	HMODULE hHw = GetModuleHandleA( "hw.dll" );
	HMODULE hWs = GetModuleHandleA( "wsock32.dll" );
	if ( !hHw || !hWs )
	{
		RcLog( "hooks: hw.dll %p, wsock32.dll %p", hHw, hWs );
		return false;
	}
	void **ppSend = RcFindImport( hHw, "WSOCK32.dll", 20, (void *)GetProcAddress( hWs, MAKEINTRESOURCEA( 20 ) ) );
	void **ppRecv = RcFindImport( hHw, "WSOCK32.dll", 17, (void *)GetProcAddress( hWs, MAKEINTRESOURCEA( 17 ) ) );
	if ( !ppSend || !ppRecv )
	{
		RcLog( "hooks: sendto %p, recvfrom %p not found in hw.dll's imports", ppSend, ppRecv );
		return false;
	}
	void *pOld;
	if ( !RcPatch( ppRecv, (void *)RcRecvfrom, &pOld ) )
		return false;
	s_pfnRecvfrom = (RcRecvfrom_t)pOld;
	if ( !RcPatch( ppSend, (void *)RcSendto, &pOld ) )
		return false;
	s_pfnSendto = (RcSendto_t)pOld;
	RcLog( "hooks installed" );
	return true;
}

// ISteamClient012 slot 12 for a version string, through the game's own steam_api.dll
static void *RcGetInterface( const char *pszVersion )
{
	HMODULE hApi = GetModuleHandleA( "steam_api.dll" );
	if ( !hApi )
		return NULL;
	RcSteamClient_t pfnClient = (RcSteamClient_t)GetProcAddress( hApi, "SteamClient" );
	RcSteamHandle_t pfnUser = (RcSteamHandle_t)GetProcAddress( hApi, "SteamAPI_GetHSteamUser" );
	RcSteamHandle_t pfnPipe = (RcSteamHandle_t)GetProcAddress( hApi, "SteamAPI_GetHSteamPipe" );
	void *pClient = pfnClient ? pfnClient() : NULL;
	int hUser = pfnUser ? pfnUser() : 0, hPipe = pfnPipe ? pfnPipe() : 0;
	if ( !pClient || !hUser || !hPipe )
		return NULL;
	RcGetGenericInterface_t pfnGeneric = ( *(RcGetGenericInterface_t **)pClient )[12];
	return pfnGeneric( pClient, hUser, hPipe, pszVersion );
}

static ISteamFriends *RcFriends( void )
{
	if ( !s_pFriends )
		s_pFriends = (ISteamFriends *)RcGetInterface( STEAMFRIENDS_INTERFACE_VERSION );
	return s_pFriends;
}

//-----------------------------------------------------------------------------
// B406: the story co-op lobby. The host makes a friends-only Steam lobby for 4 from the main menu; friends come in
// through Steam's invite (the overlay, or Join Game, which starts the game with +connect_lobby <id> when it is not
// running). The lobby's data carries the host's choices ("difficulty", "game") and, once the host's game has its
// Steam address, "connect" ("+rc_coop_join <FakeIP:port>"): the guests' games join it by themselves. The lobby stays
// for the whole game, so a friend invited later joins the running game the same way.
//-----------------------------------------------------------------------------
static ISteamMatchmaking *s_pMM;
static ISteamUser *s_pUser;
static uint64 s_ulLobby;			// the lobby this player is in, 0 for none
static int s_iLobbyState;			// RCLOBBY_*
static DWORD s_dwLobbyAsked;		// when the lobby was asked for (Steam answers in a second or two)
static char s_szLobbyJoined[64];	// the connect value this guest has followed
static char s_szLobbyNote[96];		// the last thing that went wrong, for the panel
static bool s_bLobbyWantPanel;		// Steam brought this player into a lobby: the menu shows it
static RcSteamJoin_t s_pfnLobbyJoin;

static ISteamMatchmaking *RcMM( void )
{
	if ( !s_pMM )
		s_pMM = (ISteamMatchmaking *)RcGetInterface( STEAMMATCHMAKING_INTERFACE_VERSION );
	return s_pMM;
}

static uint64 RcSelf( void )
{
	if ( !s_pUser )
		s_pUser = (ISteamUser *)RcGetInterface( STEAMUSER_INTERFACE_VERSION );
	return s_pUser ? s_pUser->GetSteamID().ConvertToUint64() : 0;
}

static void RcLobbyNote( const char *pszNote )
{
	strncpy( s_szLobbyNote, pszNote, sizeof( s_szLobbyNote ) - 1 );
	s_szLobbyNote[sizeof( s_szLobbyNote ) - 1] = 0;
	RcLog( "lobby: %s", pszNote );
}

// Steam's Join Game for this player's friends: into the lobby
static void RcLobbyPresence( bool bIn )
{
	if ( !RcFriends() )
		return;
	char sz[64];
	sz[0] = 0;
	if ( bIn )
		_snprintf( sz, sizeof( sz ) - 1, "+connect_lobby %I64u", s_ulLobby );
	sz[sizeof( sz ) - 1] = 0;
	s_pFriends->SetRichPresence( "connect", sz );
	s_pFriends->SetRichPresence( "status", bIn ? "In a Cry of Fear story co-op lobby (ReCoF)" : "" );
}

bool RcLobby_Create( void )
{
	if ( !RcMM() )
	{
		RcLobbyNote( "Steam is not available" );
		return false;
	}
	if ( s_ulLobby )
		RcLobby_Leave();
	s_pMM->CreateLobby( k_ELobbyTypeFriendsOnly, 4 );
	s_iLobbyState = RCLOBBY_ASKED;
	s_dwLobbyAsked = GetTickCount();
	s_szLobbyNote[0] = 0;
	RcLog( "lobby: asked for one" );
	return true;
}

void RcCoopMenu_LeaveForLobby( unsigned long long ulLobby );	// B421, rc_coopmenu.cpp
void RcCoopMenu_BackToLobby( void );	// B546, rc_coopmenu.cpp

void RcLobby_Join( unsigned long long ulLobby )
{
	if ( !ulLobby || !RcMM() )
		return;
	RcCoopMenu_LeaveForLobby( ulLobby );	// B421: from a game, to the menu where the lobby opens
	if ( s_ulLobby == ulLobby )
	{
		s_bLobbyWantPanel = true;
		return;
	}
	if ( s_ulLobby )
		RcLobby_Leave();
	s_pMM->JoinLobby( CSteamID( (uint64)ulLobby ) );
	s_iLobbyState = RCLOBBY_ASKED;
	s_dwLobbyAsked = GetTickCount();
	s_szLobbyNote[0] = 0;
	s_bLobbyWantPanel = true;
	RcLog( "lobby: joining %I64u", (uint64)ulLobby );
}

void RcLobby_Leave( void )
{
	if ( s_ulLobby && RcMM() )
		s_pMM->LeaveLobby( CSteamID( s_ulLobby ) );
	if ( s_ulLobby )
		RcLog( "lobby: left %I64u", s_ulLobby );
	s_ulLobby = 0;
	s_iLobbyState = RCLOBBY_NONE;
	s_szLobbyJoined[0] = 0;
	RcLobbyPresence( false );
}

int RcLobby_State( void )
{
	if ( s_iLobbyState == RCLOBBY_ASKED && GetTickCount() - s_dwLobbyAsked > 15000 )
	{
		s_iLobbyState = RCLOBBY_NONE;
		RcLobbyNote( "Steam did not answer" );
	}
	return s_iLobbyState;
}

unsigned long long RcLobby_Id( void )
{
	return s_ulLobby;
}

const char *RcLobby_Note( void )
{
	return s_szLobbyNote;
}

// B421: this player is in the lobby's own game (he made the lobby, or followed its connect value)
bool RcLobby_InItsGame( void )
{
	return s_ulLobby && ( RcLobby_Owner() || s_szLobbyJoined[0] );
}

bool RcLobby_WantPanel( void )
{
	bool b = s_bLobbyWantPanel;
	s_bLobbyWantPanel = false;
	return b;
}

static uint64 RcLobbyOwner( void )
{
	return s_ulLobby && RcMM() ? s_pMM->GetLobbyOwner( CSteamID( s_ulLobby ) ).ConvertToUint64() : 0;
}

bool RcLobby_Owner( void )
{
	return s_ulLobby && RcLobbyOwner() == RcSelf();
}

int RcLobby_Members( char ( *pszNames )[48], bool *pbHost, bool *pbSelf, int max )
{
	if ( !s_ulLobby || !RcMM() )
		return 0;
	CSteamID lobby( s_ulLobby );
	uint64 owner = RcLobbyOwner(), self = RcSelf();
	int n = s_pMM->GetNumLobbyMembers( lobby );
	if ( n > max )
		n = max;
	for ( int i = 0; i < n; i++ )
	{
		CSteamID member = s_pMM->GetLobbyMemberByIndex( lobby, i );
		const char *pszName = RcFriends() ? s_pFriends->GetFriendPersonaName( member ) : NULL;
		strncpy( pszNames[i], pszName && pszName[0] ? pszName : "Player", 47 );
		pszNames[i][47] = 0;
		pbHost[i] = member.ConvertToUint64() == owner;
		pbSelf[i] = member.ConvertToUint64() == self;
	}
	return n;
}

void RcLobby_SetData( const char *pszKey, const char *pszValue )
{
	if ( RcLobby_Owner() )
		s_pMM->SetLobbyData( CSteamID( s_ulLobby ), pszKey, pszValue );
}

const char *RcLobby_Data( const char *pszKey )
{
	const char *psz = s_ulLobby && RcMM() ? s_pMM->GetLobbyData( CSteamID( s_ulLobby ), pszKey ) : NULL;
	return psz ? psz : "";
}

bool RcLobby_Invite( void )
{
	if ( !s_ulLobby || !RcFriends() )
		return false;
	s_pFriends->ActivateGameOverlayInviteDialog( CSteamID( s_ulLobby ) );
	RcLog( "lobby: invite dialog opened" );
	return true;
}

// the host's game is up: the guests follow its connect value (once each; again after the guest's own game was left
// only through the panel's Join game)
static void RcLobbyFollow( void )
{
	if ( !s_ulLobby || RcLobby_Owner() )
		return;
	// the host who made the lobby has left it: Steam gave it to a guest, who has no game to join
	const char *pszHost = RcLobby_Data( "host" );
	if ( pszHost[0] && _strtoui64( pszHost, NULL, 10 ) != RcLobbyOwner() )
	{
		RcLobby_Leave();
		RcLobbyNote( "The host left the lobby" );
		s_bLobbyWantPanel = true;
		return;
	}
	const char *pszConnect = RcLobby_Data( "connect" );
	// B546: the host's game is over (back in the lobby, his panel clears connect): a guest who followed it goes
	// back to the lobby too, instead of trying to reconnect to a game that is gone
	if ( !pszConnect[0] && s_szLobbyJoined[0] )
	{
		s_szLobbyJoined[0] = 0;
		RcLog( "lobby: the host's game is over, back to the lobby" );
		RcCoopMenu_BackToLobby();
		return;
	}
	if ( pszConnect[0] && strcmp( pszConnect, s_szLobbyJoined ) )
	{
		strncpy( s_szLobbyJoined, pszConnect, sizeof( s_szLobbyJoined ) - 1 );
		s_szLobbyJoined[sizeof( s_szLobbyJoined ) - 1] = 0;
		RcLog( "lobby: the host's game is up, joining (%s)", pszConnect );
		if ( s_pfnLobbyJoin )
			s_pfnLobbyJoin( pszConnect );
	}
}

void RcLobby_JoinGame( void )
{
	s_szLobbyJoined[0] = 0;
	RcLobbyFollow();
}

class RcLobbyEnterCallback : public CCallbackBase
{
public:
	virtual void Run( void *pvParam )
	{
		LobbyEnter_t *p = (LobbyEnter_t *)pvParam;
		if ( p->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess )
		{
			s_iLobbyState = RCLOBBY_NONE;
			RcLobbyNote( p->m_EChatRoomEnterResponse == k_EChatRoomEnterResponseFull ? "The lobby is full" : "Could not enter the lobby" );
			return;
		}
		s_ulLobby = p->m_ulSteamIDLobby;
		s_iLobbyState = RCLOBBY_IN;
		s_szLobbyJoined[0] = 0;
		RcLog( "lobby: in %I64u, %s", s_ulLobby, RcLobby_Owner() ? "host" : "guest" );
		if ( RcLobby_Owner() )
		{
			char sz[32];
			_snprintf( sz, sizeof( sz ) - 1, "%I64u", RcSelf() );
			sz[sizeof( sz ) - 1] = 0;
			RcLobby_SetData( "host", sz );
			RcLobby_SetData( "rc", "1" );
		}
		RcLobbyPresence( true );
		RcLobbyFollow();
	}
	virtual void Run( void *pvParam, bool bIOFailure, SteamAPICall_t hSteamAPICall ) { Run( pvParam ); }
	virtual int GetCallbackSizeBytes() { return sizeof( LobbyEnter_t ); }
};

class RcLobbyDataCallback : public CCallbackBase
{
public:
	virtual void Run( void *pvParam )
	{
		LobbyDataUpdate_t *p = (LobbyDataUpdate_t *)pvParam;
		if ( p->m_ulSteamIDLobby == s_ulLobby && p->m_ulSteamIDMember == s_ulLobby )
			RcLobbyFollow();
	}
	virtual void Run( void *pvParam, bool bIOFailure, SteamAPICall_t hSteamAPICall ) { Run( pvParam ); }
	virtual int GetCallbackSizeBytes() { return sizeof( LobbyDataUpdate_t ); }
};

// a member came or went: a lobby whose host has gone is left (RcLobbyFollow)
class RcLobbyChatCallback : public CCallbackBase
{
public:
	virtual void Run( void *pvParam )
	{
		LobbyChatUpdate_t *p = (LobbyChatUpdate_t *)pvParam;
		if ( p->m_ulSteamIDLobby == s_ulLobby )
			RcLobbyFollow();
	}
	virtual void Run( void *pvParam, bool bIOFailure, SteamAPICall_t hSteamAPICall ) { Run( pvParam ); }
	virtual int GetCallbackSizeBytes() { return sizeof( LobbyChatUpdate_t ); }
};

// an invite accepted, or Join Game on a friend in a lobby, while this game runs
class RcLobbyJoinRequestCallback : public CCallbackBase
{
public:
	virtual void Run( void *pvParam )
	{
		GameLobbyJoinRequested_t *p = (GameLobbyJoinRequested_t *)pvParam;
		RcLog( "lobby: Steam asks to join %I64u", p->m_steamIDLobby.ConvertToUint64() );
		RcLobby_Join( p->m_steamIDLobby.ConvertToUint64() );
	}
	virtual void Run( void *pvParam, bool bIOFailure, SteamAPICall_t hSteamAPICall ) { Run( pvParam ); }
	virtual int GetCallbackSizeBytes() { return sizeof( GameLobbyJoinRequested_t ); }
};

static RcLobbyEnterCallback s_LobbyEnter;
static RcLobbyDataCallback s_LobbyData;
static RcLobbyChatCallback s_LobbyChat;
static RcLobbyJoinRequestCallback s_LobbyJoinRequest;

static bool RcStart( bool bRelayOnly )
{
	static bool s_bInit;
	if ( !s_bInit && !s_bJoinRegistered )
		InitializeCriticalSection( &s_csLog );
	s_bInit = true;
	if ( s_pSockets )
		return true;
	HMODULE hApi = GetModuleHandleA( "steam_api.dll" );
	if ( !hApi )
	{
		s_pfnPrint( "ReCoF co-op: steam_api.dll is not loaded\n" );
		return false;
	}
	RcSteamClient_t pfnClient = (RcSteamClient_t)GetProcAddress( hApi, "SteamClient" );
	RcSteamHandle_t pfnUser = (RcSteamHandle_t)GetProcAddress( hApi, "SteamAPI_GetHSteamUser" );
	RcSteamHandle_t pfnPipe = (RcSteamHandle_t)GetProcAddress( hApi, "SteamAPI_GetHSteamPipe" );
	s_pfnRunCallbacks = (RcSteamVoid_t)GetProcAddress( hApi, "SteamAPI_RunCallbacks" );
	void *pClient = pfnClient ? pfnClient() : NULL;
	int hUser = pfnUser ? pfnUser() : 0, hPipe = pfnPipe ? pfnPipe() : 0;
	if ( !pClient || !hUser || !hPipe )
	{
		s_pfnPrint( "ReCoF co-op: Steam is not running or not signed in\n" );
		RcLog( "no Steam: client %p user %d pipe %d", pClient, hUser, hPipe );
		return false;
	}
	RcGetGenericInterface_t pfnGeneric = ( *(RcGetGenericInterface_t **)pClient )[12];
	s_pUtils = (ISteamNetworkingUtils *)pfnGeneric( pClient, hUser, hPipe, STEAMNETWORKINGUTILS_INTERFACE_VERSION );
	s_pSockets = (ISteamNetworkingSockets *)pfnGeneric( pClient, hUser, hPipe, STEAMNETWORKINGSOCKETS_INTERFACE_VERSION );
	RcLog( "%s %s, %s %s", STEAMNETWORKINGUTILS_INTERFACE_VERSION, s_pUtils ? "ok" : "NULL", STEAMNETWORKINGSOCKETS_INTERFACE_VERSION, s_pSockets ? "ok" : "NULL" );
	if ( !s_pUtils || !s_pSockets )
	{
		s_pfnPrint( "ReCoF co-op: Steam's networking is not available\n" );
		s_pSockets = NULL;
		return false;
	}
	s_pUtils->SetDebugOutputFunction( k_ESteamNetworkingSocketsDebugOutputType_Msg, RcSteamDebug );
	if ( bRelayOnly )
	{
		s_pUtils->SetGlobalConfigValueInt32( k_ESteamNetworkingConfig_P2P_Transport_ICE_Enable, k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_Disable );
		RcLog( "direct routes off (relay only)" );
	}
	// B480: room for a level change's burst: Steam's default queue (512 KB) and rate (256 KB/s) could fill once the
	// netchan sends up to 100000 bytes a second to each guest (B442), and a full queue drops the datagrams
	// (Mhdshehri 2cefc69)
	s_pUtils->SetGlobalConfigValueInt32( k_ESteamNetworkingConfig_SendBufferSize, 4 * 1024 * 1024 );
	s_pUtils->SetGlobalConfigValueInt32( k_ESteamNetworkingConfig_SendRateMin, 256 * 1024 );
	s_pUtils->SetGlobalConfigValueInt32( k_ESteamNetworkingConfig_SendRateMax, 2 * 1024 * 1024 );
	s_pUtils->InitRelayNetworkAccess();
	return true;
}

bool RcSteamNet_Host( int iServerPort, bool bRelayOnly, bool bMap127, RcSteamPrint_t pfnPrint )
{
	s_pfnPrint = pfnPrint;
	if ( s_iMode != RCSN_OFF )
	{
		s_pfnPrint( "ReCoF co-op: already started\n" );
		return false;
	}
	if ( !RcStart( bRelayOnly ) )
		return false;
	// the server port (index 0) is made once the FakeIP request has begun (RcSteamNet_Frame)
	if ( !RcInstallHooks() )
	{
		s_pfnPrint( "ReCoF co-op: could not set up the Steam port\n" );
		return false;
	}
	s_iServerPort = iServerPort;
	s_bMap127 = bMap127;
	s_iIPState = 1;
	s_iIPTries = 0;
	s_dwIPRetryAt = s_dwIPStart = GetTickCount();
	s_iMode = RCSN_HOST;
	s_pfnPrint( "ReCoF co-op: asking Steam for an address (server port %d%s%s)...\n", iServerPort, bRelayOnly ? ", relay only" : "", bMap127 ? ", friends as 127.77.0.x" : "" );
	RcLog( "host on port %d", iServerPort );
	return true;
}

bool RcSteamNet_Join( const char *pszAddr, bool bRelayOnly, RcSteamPrint_t pfnPrint )
{
	s_pfnPrint = pfnPrint;
	unsigned a, b, c, d, port;
	if ( sscanf( pszAddr, "%u.%u.%u.%u:%u", &a, &b, &c, &d, &port ) != 5 || a != 169 || b != 254 )
	{
		s_pfnPrint( "ReCoF co-op: rc_coop_join takes the host's Steam address, 169.254.x.x:port\n" );
		return false;
	}
	// a guest who left joins again (the same host or another) through the port he has: the engine's packets to any
	// 169.254 address go out through it
	if ( s_iMode == RCSN_JOIN )
	{
		RcLog( "join again %s", pszAddr );
		return true;
	}
	if ( s_iMode != RCSN_OFF )
	{
		s_pfnPrint( "ReCoF co-op: this game hosts; it cannot join another\n" );
		return false;
	}
	if ( !RcStart( bRelayOnly ) )
		return false;
	s_pPort = s_pSockets->CreateFakeUDPPort( -1 );
	if ( !s_pPort || !RcInstallHooks() )
	{
		s_pfnPrint( "ReCoF co-op: could not set up the Steam port\n" );
		return false;
	}
	s_iMode = RCSN_JOIN;
	RcLog( "join %s", pszAddr );
	return true;
}

void RcSteamNet_Frame( void )
{
	if ( s_iMode == RCSN_OFF )
		return;
	if ( s_pfnRunCallbacks )
		s_pfnRunCallbacks();
	// the FakeIP is asked for once the relay network is up (asked earlier it can fail while Steam still fetches the
	// relay list), and asked again a few times if Steam refuses it
	if ( s_iIPState == 1 && (long)( GetTickCount() - s_dwIPRetryAt ) >= 0 )
	{
		SteamRelayNetworkStatus_t st;
		memset( &st, 0, sizeof( st ) );
		if ( s_pUtils->GetRelayNetworkStatus( &st ) == k_ESteamNetworkingAvailability_Current || GetTickCount() - s_dwIPStart > 30000 )
		{
			s_iIPTries++;
			RcLog( "asking for a FakeIP (try %d)", s_iIPTries );
			if ( s_pSockets->BeginAsyncRequestFakeIP( 1 ) )
			{
				s_iIPState = 2;
				if ( s_pPort )
					s_pPort->DestroyFakeUDPPort();
				s_pPort = s_pSockets->CreateFakeUDPPort( 0 );
				RcLog( "server port %s", s_pPort ? "made" : "NOT made" );
			}
			else
				s_dwIPRetryAt = GetTickCount() + 2000;
		}
	}
	else if ( s_iIPState == 2 )
	{
		SteamNetworkingFakeIPResult_t r;
		memset( &r, 0, sizeof( r ) );
		s_pSockets->GetFakeIP( 0, &r );
		if ( r.m_eResult != k_EResultBusy && r.m_eResult != 0 )
		{
			if ( r.m_eResult == k_EResultOK )
			{
				s_iIPState = 3;
				char sz[32];
				RcFormatIP( r.m_unIP, r.m_unPorts[0], sz, sizeof( sz ) );
				s_FakeIP.Clear();
				s_FakeIP.SetIPv4( r.m_unIP, r.m_unPorts[0] );
				s_pfnPrint( "ReCoF co-op: your friends join with: rc_coop_join %s\n", sz );
				RcLog( "FAKEIP %s", sz );
				// Steam's Join Game and the overlay's Invite to Game hand this to the friend's game
				_snprintf( s_szConnect, sizeof( s_szConnect ) - 1, "+rc_coop_join %s", sz );
				s_szConnect[sizeof( s_szConnect ) - 1] = 0;
				if ( RcLobby_Owner() )
				{
					RcLobby_SetData( "connect", s_szConnect );	// B406: the lobby's guests join by themselves
					s_pfnPrint( "ReCoF co-op: the lobby's friends are joining\n" );
				}
				else if ( RcFriends() )
				{
					bool bOk = s_pFriends->SetRichPresence( "connect", s_szConnect );
					s_pFriends->SetRichPresence( "status", "Playing Cry of Fear co-op (ReCoF)" );
					RcLog( "rich presence connect %s", bOk ? "set" : "refused" );
					s_pfnPrint( "ReCoF co-op: friends can join from Steam now; rc_coop_invite opens Steam's invite list\n" );
				}
				char path[MAX_PATH];
				GetModuleFileNameA( NULL, path, sizeof( path ) );
				char *slash = strrchr( path, '\\' );
				if ( slash )
				{
					strcpy( slash + 1, "rc_coop_fakeip.txt" );
					FILE *f = fopen( path, "w" );
					if ( f )
					{
						fprintf( f, "%s\n", sz );
						fclose( f );
					}
				}
			}
			else if ( s_iIPTries < 5 )
			{
				RcLog( "FakeIP try %d failed: %d", s_iIPTries, (int)r.m_eResult );
				s_iIPState = 1;
				s_dwIPRetryAt = GetTickCount() + 2000 * s_iIPTries;
			}
			else
			{
				s_iIPState = 0;
				s_pfnPrint( "ReCoF co-op: Steam gave no address (result %d)\n", (int)r.m_eResult );
				RcLog( "FakeIP failed: %d", (int)r.m_eResult );
			}
		}
	}
	DWORD now = GetTickCount();
	if ( (long)( now - s_dwNextStats ) >= 0 )
	{
		s_dwNextStats = now + 10000;
		RcLog( "stats: %ld in (%lu bytes), %ld out (%lu bytes), %ld send failures (%ld for a full queue), %d friends", s_nIn, s_ulBytesIn, s_nOut, s_ulBytesOut, s_nSendFail, s_nSendFull, s_nPeers );
	}
}

int RcSteamNet_Joined( void )
{
	return s_iMode == RCSN_JOIN;
}

void RcSteamNet_Status( RcSteamPrint_t pfnPrint )
{
	static const char *s_szMode[3] = { "off", "host", "join" };
	pfnPrint( "ReCoF co-op: %s, %ld datagrams in (%lu bytes), %ld out (%lu bytes), %ld send failures (%ld for a full queue), %d friends\n", s_szMode[s_iMode], s_nIn, s_ulBytesIn, s_nOut, s_ulBytesOut, s_nSendFail, s_nSendFull, s_nPeers );
	if ( s_pUtils )
	{
		SteamRelayNetworkStatus_t st;
		memset( &st, 0, sizeof( st ) );
		s_pUtils->GetRelayNetworkStatus( &st );
		pfnPrint( "ReCoF co-op: relay network %d: %s\n", (int)st.m_eAvail, st.m_debugMsg );
	}
}

// rc_coop_invite: Steam's overlay picker that sends the connect value to the friends chosen (no lobby needed)
const char *RcSteamNet_PersonaName( void )
{
	return RcFriends() ? s_pFriends->GetPersonaName() : NULL;
}

void RcSteamNet_Invite( RcSteamPrint_t pfnPrint )
{
	if ( RcLobby_Invite() )
	{
		pfnPrint( "ReCoF co-op: Steam's invite list is open (Shift+Tab if the overlay does not show)\n" );
		return;
	}
	if ( !s_szConnect[0] )
	{
		pfnPrint( "ReCoF co-op: host a co-op game first (rc_coop_start); invites are possible once Steam gave it an address\n" );
		return;
	}
	if ( !RcFriends() )
	{
		pfnPrint( "ReCoF co-op: Steam's friends interface is not available\n" );
		return;
	}
	s_pFriends->ActivateGameOverlayInviteDialogConnectString( s_szConnect );
	pfnPrint( "ReCoF co-op: Steam's invite list is open (Shift+Tab if the overlay does not show); friends can also type: rc_coop_join %s\n", s_szConnect + 14 );
	RcLog( "invite dialog opened" );
}

// a friend accepted an invite or clicked Join Game while this game runs: Steam sends the host's connect value here
class RcJoinCallback : public CCallbackBase
{
public:
	RcSteamJoin_t m_pfnJoin;

	virtual void Run( void *pvParam )
	{
		GameRichPresenceJoinRequested_t *p = (GameRichPresenceJoinRequested_t *)pvParam;
		char sz[k_cchMaxRichPresenceValueLength + 1];
		memcpy( sz, p->m_rgchConnect, k_cchMaxRichPresenceValueLength );
		sz[k_cchMaxRichPresenceValueLength] = 0;
		RcLog( "join request: %s", sz );
		if ( m_pfnJoin )
			m_pfnJoin( sz );
	}
	virtual void Run( void *pvParam, bool bIOFailure, SteamAPICall_t hSteamAPICall )
	{
		Run( pvParam );
	}
	virtual int GetCallbackSizeBytes()
	{
		return sizeof( GameRichPresenceJoinRequested_t );
	}
};

static RcJoinCallback s_JoinCallback;

void RcSteamNet_InitCallbacks( RcSteamJoin_t pfnJoin )
{
	static bool s_bInit;
	if ( !s_bInit )
	{
		InitializeCriticalSection( &s_csLog );
		s_bInit = true;
	}
	HMODULE hApi = GetModuleHandleA( "steam_api.dll" );
	RcSteamRegister_t pfnRegister = hApi ? (RcSteamRegister_t)GetProcAddress( hApi, "SteamAPI_RegisterCallback" ) : NULL;
	if ( !s_pfnRunCallbacks && hApi )
		s_pfnRunCallbacks = (RcSteamVoid_t)GetProcAddress( hApi, "SteamAPI_RunCallbacks" );
	if ( !pfnRegister || !s_pfnRunCallbacks || s_bJoinRegistered )
		return;
	s_JoinCallback.m_pfnJoin = pfnJoin;
	pfnRegister( &s_JoinCallback, GameRichPresenceJoinRequested_t::k_iCallback );
	// B406: the lobby's
	s_pfnLobbyJoin = pfnJoin;
	pfnRegister( &s_LobbyEnter, LobbyEnter_t::k_iCallback );
	pfnRegister( &s_LobbyData, LobbyDataUpdate_t::k_iCallback );
	pfnRegister( &s_LobbyChat, LobbyChatUpdate_t::k_iCallback );
	pfnRegister( &s_LobbyJoinRequest, GameLobbyJoinRequested_t::k_iCallback );
	s_bJoinRegistered = true;
}

// every frame: Steam's callbacks of client.dll's own connection (the engine reads only its own)
void RcSteamNet_Pump( void )
{
	if ( s_bJoinRegistered && s_iMode == RCSN_OFF && s_pfnRunCallbacks )
		s_pfnRunCallbacks();
}
