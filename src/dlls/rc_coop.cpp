// ReCoF story co-op: Simon's story for 2 to 4 players on a listen server (rc_coop 1 with maxplayers above 1).
//
// CHalfLifeStoryCoop is the multiplayer rules without deathmatch: the story's level exits work (CChangeLevel returns
// early under deathmatch), the engine carries every player through the level transition (src/engine_hw/rc_coop.c),
// players are teammates, and a player who joins or respawns comes in next to a teammate instead of on a start spot
// that may already be taken or sit in a wall. Single player never makes these rules (InstallGameRules).

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "weapons.h"
#include "gamerules.h"
#include "game.h"
#include "shake.h"	// B430: FFADE_IN

extern void UTIL_ProMessage( CBaseEntity *pEntity, const char *pMessage, int colour, bool all );

// G1: how long an exit a player walks into waits for the rest of the group before it brings them (0: at once)
cvar_t	rc_coop_exit_wait = { "rc_coop_exit_wait", "60", FCVAR_SERVER };
// G12: seconds before a dead player comes back next to the group, with his things (0: Cry of Fear's own rule, jump
// after 60 s or wait sv_auto_respawn_time, without them)
cvar_t	rc_coop_respawn_time = { "rc_coop_respawn_time", "10", FCVAR_SERVER };
// G13: 1 saves the group's game as coop_auto once every player is in on a new level
cvar_t	rc_coop_autosave = { "rc_coop_autosave", "1", FCVAR_SERVER };
// G16: percent more monster health for each player beyond the first (at most double); 0 keeps single player's
cvar_t	rc_coop_monster_health = { "rc_coop_monster_health", "25", FCVAR_SERVER };
// G6: 1 while the host's scene skip runs: hw.dll then runs the server fast (rc_coop.c RcCoop_SkipPhysics)
cvar_t	rc_coop_skip = { "rc_coop_skip", "0" };
// G6 (B390): 1 lets the guests skip a scene too: once every player watching it has pressed Enter; 0 only the host
cvar_t	rc_coop_skip_vote = { "rc_coop_skip_vote", "1", FCVAR_SERVER };
// B411: the players are Simon (his voice, his sleeves and hands on the weapons) in the main campaign, Cry of Fear's
// police officers in a custom campaign; the lobby sets it with the campaign, a co-op save keeps it
cvar_t	rc_coop_simon = { "rc_coop_simon", "1", FCVAR_SERVER };
static bool s_bRcSkipVote[33];	// this player asked to skip the scene he is in
// B548: how many players the first level of a game waits for: the host's game sets it to the players in the group's
// lobby when it starts or continues a game (client.dll rc_client.cpp), and the arrival hold puts it back to 0
cvar_t	rc_coop_expect = { "rc_coop_expect", "0" };

extern bool m_bSpawningSimon;	// multiplay_gamerules.cpp: the police co-op's hunting Simon

static Vector RcCoopAnyFreeSpot( const Vector &vecBase, edict_t *pIgnore );

static CHalfLifeStoryCoop *RcCoopRules( void )
{
	return g_pGameRules ? dynamic_cast<CHalfLifeStoryCoop *>( g_pGameRules ) : NULL;
}

// a living, present teammate other than pSkip (B320: fuser4 1 is a player who left)
static CBasePlayer *RcCoopAnchor( CBasePlayer *pSkip )
{
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( !pEnt || pEnt == pSkip || !pEnt->IsPlayer() )
			continue;
		if ( pEnt->pev->fuser4 != 0 || !pEnt->IsAlive() || pEnt->pev->deadflag != DEAD_NO )
			continue;
		return (CBasePlayer *)pEnt;
	}
	return NULL;
}

// B463: a standing player's box at vecSpot meets a level exit, a teleport or a trigger_hurt: no place to put anyone
// (one put into the exit back sent the group back a map; charliee1w 35d8c95d, 4dda7409)
static bool RcCoopSpotInTrigger( const Vector &vecSpot )
{
	static const char *s_pszRcAvoid[3] = { "trigger_changelevel", "trigger_teleport", "trigger_hurt" };
	Vector vecMin = vecSpot + VEC_HULL_MIN, vecMax = vecSpot + VEC_HULL_MAX;
	for ( int k = 0; k < 3; k++ )
	{
		CBaseEntity *pTrig = NULL;
		while ( ( pTrig = UTIL_FindEntityByClassname( pTrig, s_pszRcAvoid[k] ) ) != NULL )
		{
			if ( pTrig->pev->solid == SOLID_NOT && k != 0 )
				continue;	// a teleport or hurt switched off
			if ( vecMin.x <= pTrig->pev->absmax.x && vecMax.x >= pTrig->pev->absmin.x && vecMin.y <= pTrig->pev->absmax.y &&
				vecMax.y >= pTrig->pev->absmin.y && vecMin.z <= pTrig->pev->absmax.z && vecMax.z >= pTrig->pev->absmin.z )
				return true;
		}
	}
	return false;
}

// a spot for a standing player at vecSpot: the hull fits, there is floor under it and nothing blocks the way from
// vecFrom (so nobody lands behind a wall), and no exit, teleport or hurting trigger is there (B463)
static bool RcCoopSpotFree( const Vector &vecFrom, const Vector &vecSpot, edict_t *pIgnore )
{
	TraceResult tr;

	if ( RcCoopSpotInTrigger( vecSpot ) )
		return false;

	UTIL_TraceHull( vecSpot, vecSpot, dont_ignore_monsters, human_hull, pIgnore, &tr );
	if ( tr.fStartSolid || tr.fAllSolid )
		return false;

	UTIL_TraceLine( vecFrom, vecSpot, ignore_monsters, pIgnore, &tr );
	if ( tr.flFraction < 1.0 )
		return false;

	UTIL_TraceHull( vecSpot, vecSpot - Vector( 0, 0, 72 ), ignore_monsters, human_hull, pIgnore, &tr );
	if ( tr.flFraction >= 1.0 || tr.fStartSolid )
		return false;

	return true;
}

// B403: move pEnt where his box is clear of pOver's: beside him, 36 to 104 units away, or, in a place too small for
// that (c_nightmare starts in a booth about 90 units wide), the two of them half a step apart from the spot they share.
// The engine links each box 1 unit larger on every side and CBaseEntity::Intersects counts touching boxes, so two
// players overlap until their origins are more than 34 units apart on an axis
static bool RcCoopStepAside( CBaseEntity *pEnt, CBaseEntity *pOver )
{
	static const float s_flStep[4] = { 36, 48, 72, 104 };
	Vector vecBase = pOver->pev->origin;
	for ( int r = 0; r < 4; r++ )
	{
		for ( int a = 0; a < 8; a++ )
		{
			float yaw = a * 45 * ( M_PI / 180.0 );
			Vector vecOfs( cos( yaw ) * s_flStep[r], sin( yaw ) * s_flStep[r], 0 );
			if ( fabs( vecOfs.x ) < 35 && fabs( vecOfs.y ) < 35 )
				continue;	// still inside his box
			if ( RcCoopSpotFree( vecBase, vecBase + vecOfs, pEnt->edict() ) )
			{
				UTIL_SetOrigin( pEnt, vecBase + vecOfs );
				pEnt->pev->velocity = g_vecZero;
				return true;
			}
		}
	}
	for ( int a = 0; a < 8; a++ )
	{
		float yaw = a * 45 * ( M_PI / 180.0 );
		float flHalf = ( a & 1 ) ? 26 : 18;	// 36 units apart on an axis, 36 on both on a diagonal
		Vector vecOfs( cos( yaw ) * flHalf, sin( yaw ) * flHalf, 0 );
		if ( RcCoopSpotFree( vecBase, vecBase + vecOfs, pEnt->edict() ) && RcCoopSpotFree( vecBase, vecBase - vecOfs, pOver->edict() ) )
		{
			UTIL_SetOrigin( pEnt, vecBase + vecOfs );
			UTIL_SetOrigin( pOver, vecBase - vecOfs );
			pEnt->pev->velocity = g_vecZero;
			pOver->pev->velocity = g_vecZero;
			return true;
		}
	}
	return false;
}

// B472: story co-op's arrival hold is on (cbase.cpp DispatchThink and DispatchTouch; world.cpp clears it with the rules)
bool g_bRcHolding = false;

CHalfLifeStoryCoop::CHalfLifeStoryCoop()
{
	CVAR_SET_FLOAT( "rc_coop_skip", 0 );	// G6: a new map or a loaded game does not go on fast forwarding
	m_bAutosaved = false;
	m_flLevelStart = -1;
	g_bRcHolding = false;
	m_bHoldDone = false;
	m_flHoldStart = -1;
	m_flHoldMsg = 0;
	m_iExitGone = 0;
	m_flExitGone = -1;
	for ( int i = 0; i < 33; i++ )
	{
		m_bHeld[i] = false;
		m_flHidden[i] = 0;
		m_flSeen[i] = 0;
		m_bProtected[i] = false;
		m_flOldTakeDamage[i] = DAMAGE_AIM;
		m_bArmed[i] = false;
		m_flOverlap[i] = 0;
		m_bWasAlive[i] = false;
		m_bRestore[i] = false;
		m_iSnapItems[i] = 0;
		m_flRcFollow[i] = 0;	// B503
	}
}

// B470: a player who left gives his slot back clean: a joiner who took it on the same level kept the left player's
// seen time (so the level's state was not sent to him: fog, physinfo, a door's leftovers), his death snapshot (his
// weapons instead of the late joiner's gifts, G18), his loading protection and his exits' arming (cof-coop 5399ae7)
void CHalfLifeStoryCoop::ClientDisconnected( edict_t *pClient )
{
	CHalfLifeMultiplay::ClientDisconnected( pClient );

	int i = pClient ? ENTINDEX( pClient ) : 0;
	if ( i < 1 || i > 32 )
		return;
	m_flSeen[i] = 0;
	m_bProtected[i] = false;
	m_flOldTakeDamage[i] = DAMAGE_AIM;
	m_bArmed[i] = false;
	m_flOverlap[i] = 0;
	m_bWasAlive[i] = false;
	m_bRestore[i] = false;
	m_iSnapItems[i] = 0;
}

extern int gmsgQSlotUpdate;	// B478 (player.cpp)

// B478: the class names in a player's 7 inventory places, each read once (GetDescriptionFromItem loads and parses
// the item's script file at every call and returns the parser's token buffer)
struct RcInv_s
{
	char sz[7][64];
};

static void RcCoopInvRead( CBasePlayer *pPlayer, RcInv_s &inv )
{
	for ( int q = 0; q < 7; q++ )
	{
		inv.sz[q][0] = 0;
		const char *pszScript = STRING( pPlayer->inventorySlot[q] );
		if ( !pszScript || strlen( pszScript ) <= 1 )
			continue;
		const char *pszHere = pPlayer->GetDescriptionFromItem( "classname", (char *)pszScript );
		if ( pszHere )
		{
			strncpy( inv.sz[q], pszHere, 63 );
			inv.sz[q][63] = 0;
		}
	}
}

// the inventory place (0 to 6) of a weapon, 99 when it has none
static int RcCoopInvIndex( const RcInv_s &inv, const char *pszClass )
{
	for ( int q = 0; q < 7; q++ )
	{
		if ( inv.sz[q][0] && !strcmp( inv.sz[q], pszClass ) )
			return q;
	}
	return 99;
}

static char *RcCoopQuickSel( CBasePlayer *pPlayer, int q )
{
	return q == 1 ? pPlayer->m_sQuickSel1 : q == 2 ? pPlayer->m_sQuickSel2 : pPlayer->m_sQuickSel3;
}

static CBasePlayerItem *RcCoopFindItem( CBasePlayer *pPlayer, const char *pszClass )
{
	for ( int s = 0; s < MAX_ITEM_TYPES; s++ )
	{
		for ( CBasePlayerItem *pItem = pPlayer->m_rgpPlayerItems[s]; pItem; pItem = pItem->m_pNext )
		{
			if ( FClassnameIs( pItem->pev, pszClass ) )
				return pItem;
		}
	}
	return NULL;
}

// B487: a player rides a wheelchair (c_ending4b): a save then loads the players frozen (charliee1w 23799f89)
static bool RcCoopAnyoneInChair( void )
{
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( pEnt && pEnt->IsPlayer() && ( (CBasePlayer *)pEnt )->m_pWheelchair )
			return true;
	}
	return false;
}

// the player's box overlaps the entity's box
static bool RcCoopInside( CBaseEntity *pBox, CBaseEntity *pPlayer )
{
	return pPlayer->pev->absmin.x <= pBox->pev->absmax.x && pPlayer->pev->absmax.x >= pBox->pev->absmin.x &&
		pPlayer->pev->absmin.y <= pBox->pev->absmax.y && pPlayer->pev->absmax.y >= pBox->pev->absmin.y &&
		pPlayer->pev->absmin.z <= pBox->pev->absmax.z && pPlayer->pev->absmax.z >= pBox->pev->absmin.z;
}

// B391: a player slot with no client behind it: a saved player whose player has not come back after a load (the
// engine's userinfo of the slot is empty until a client connects to it)
static bool RcCoopNoClient( CBaseEntity *pEnt )
{
	char *pszInfo = g_engfuncs.pfnGetInfoKeyBuffer( pEnt->edict() );
	return !pszInfo || !pszInfo[0];
}

// B495: a centred line to every player with a client. UTIL_HudMessageAll writes to every player slot, and after a
// load a saved player whose client has not come back has no network buffer: the engine stops on SZ_GetSpace (B451),
// as a text message is not one of the user messages the engine skips for such a slot
static void RcCoopHudMessage( const hudtextparms_t &tp, const char *pszText )
{
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( pEnt && pEnt->IsPlayer() && pEnt->pev->fuser4 == 0 && !RcCoopNoClient( pEnt ) )
			UTIL_HudMessage( pEnt, tp, pszText );
	}
}

// a player in the game: alive, not one who left (B320 fuser4), with a client (B391)
static bool RcCoopPresent( CBaseEntity *pEnt )
{
	return pEnt && pEnt->IsPlayer() && pEnt->pev->fuser4 == 0 && pEnt->IsAlive() && pEnt->pev->deadflag == DEAD_NO && !RcCoopNoClient( pEnt );
}

// B549: a player put where a teammate stands (two through one door, two up one ladder) steps aside at once, as
// B403 does after 1.5 s; the engine links boxes 1 unit larger, so 35 units apart on an axis is still inside
void RcCoop_StepOff( CBaseEntity *pPlayer )
{
	if ( !RcCoopRules() || !pPlayer )
		return;
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( !RcCoopPresent( pEnt ) || pEnt == pPlayer )
			continue;
		Vector d = pEnt->pev->origin - pPlayer->pev->origin;
		if ( fabs( d.x ) < 35 && fabs( d.y ) < 35 && fabs( d.z ) < 72 )
		{
			RcCoopStepAside( pPlayer, pEnt );
			return;
		}
	}
}

// G18: a player who joins the game (not a respawn, which gets his own things back) gets the weapons of the teammate
// he comes in next to, so a late joiner is not left with nothing; the group's keys come by themselves (G9)
void CHalfLifeStoryCoop::PlayerSpawn( CBasePlayer *pPlayer )
{
	CHalfLifeMultiplay::PlayerSpawn( pPlayer );

	int i = ENTINDEX( pPlayer->edict() );
	if ( i < 1 || i > 32 || m_bRestore[i] )
		return;
	CBasePlayer *pAnchor = RcCoopAnchor( pPlayer );
	if ( !pAnchor )
		return;
	int nGiven = 0;
	for ( int s = 0; s < MAX_ITEM_TYPES; s++ )
	{
		for ( CBasePlayerItem *pItem = pAnchor->m_rgpPlayerItems[s]; pItem; pItem = pItem->m_pNext )
		{
			const char *pszName = STRING( pItem->pev->classname );
			if ( !pPlayer->HasNamedPlayerItem( pszName ) )
			{
				pPlayer->GiveNamedItem( pszName, false, true );
				nGiven++;
			}
		}
	}
	if ( nGiven )
		g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %s got %d weapons like %s's\n", STRING( pPlayer->pev->netname ), nGiven, STRING( pAnchor->pev->netname ) ) );
}

// G16 (CBaseMonster::MonsterInit): story co-op monsters have more health with more players present, bounded
float RcCoop_MonsterHealthScale( void )
{
	if ( !RcCoopRules() || rc_coop_monster_health.value <= 0 )
		return 1;
	int n = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( pEnt && pEnt->IsPlayer() && pEnt->pev->fuser4 == 0 )
			n++;
	}
	if ( n <= 1 )
		return 1;
	float flScale = 1 + ( n - 1 ) * rc_coop_monster_health.value / 100.0;
	return flScale > 2 ? 2 : flScale;
}

// G16: brings a live monster's health to the group's scale; it keeps the scale it has (saved with it), so a level
// change, a loaded game or a player joining or leaving changes its health once, in proportion (a hurt monster stays as
// hurt). MonsterInit calls it for a new monster; the rules' Think, once a second, for all (a map's monsters spawn
// before any player is in).
void RcCoop_ScaleMonster( CBaseMonster *pMonster )
{
	if ( !RcCoopRules() || !pMonster || pMonster->IsPlayer() || pMonster->pev->deadflag != DEAD_NO || pMonster->pev->health <= 0 )
		return;
	float flOld = pMonster->m_flRcCoopScale > 0 ? pMonster->m_flRcCoopScale : 1;
	float flNew = RcCoop_MonsterHealthScale();
	if ( flNew == flOld )
	{
		pMonster->m_flRcCoopScale = flNew;
		return;
	}
	pMonster->pev->health = pMonster->pev->health * flNew / flOld;
	if ( pMonster->pev->max_health > 0 )
		pMonster->pev->max_health = pMonster->pev->max_health * flNew / flOld;
	pMonster->m_flRcCoopScale = flNew;
}

static void RcCoopScaleMonsters( void )
{
	for ( int i = gpGlobals->maxClients + 1; i < gpGlobals->maxEntities; i++ )
	{
		edict_t *pEdict = INDEXENT( i );
		if ( !pEdict || pEdict->free || !( pEdict->v.flags & FL_MONSTER ) )
			continue;
		CBaseEntity *pEnt = CBaseEntity::Instance( pEdict );
		if ( pEnt )
			RcCoop_ScaleMonster( pEnt->MyMonsterPointer() );
	}
}

void CHalfLifeStoryCoop::PlayerThink( CBasePlayer *pPlayer )
{
	CHalfLifeMultiplay::PlayerThink( pPlayer );
}

// B402: what the client knows of its player, as CBasePlayer::UpdateClientData and PostThink keep it, marked unknown so
// the next UpdateClientData sends all of it (CBasePlayer::ForceClientDllUpdate's list and the HUD flags, black bars,
// view entity, FOV, flashlight, blur, objective and ammo it leaves out)
void RcFogResend( CBasePlayer *pPlayer );	// B456, effects.cpp

static void RcCoopResendState( CBasePlayer *pPlayer )
{
	pPlayer->RcRestorePhysinfo();	// B424: the engine emptied the guest's physinfo when his game signed on
	RcFogResend( pPlayer );	// B456: the map's fog, which a loading or late client missed
	pPlayer->m_fInitHUD = TRUE;
	pPlayer->m_iClientHideHUD = -1;
	pPlayer->m_ClientDrawBars = -1;
	pPlayer->m_iClientViewEntIndex = -1;
	pPlayer->m_iClientFOV = -1;
	pPlayer->m_iClientHealth = -1;
	pPlayer->m_iClientBattery = -1;
	pPlayer->m_iClientUnlockedClothes = -1;
	pPlayer->m_iClientDualWeaponLeftAmmo = -2;
	pPlayer->m_iClientDualWeaponRightAmmo = -2;
	pPlayer->m_sClientDualWieldViewModel = MAKE_STRING( "update" );
	pPlayer->m_iClientPhoneDisabled = -1;
	pPlayer->m_iClientFlashlightFlags = -1;
	pPlayer->m_iClientFlashlight = -1;
	pPlayer->m_iClientBlurActive = -1;
	pPlayer->m_sClientObjectiveText = 0;
	pPlayer->m_bitsHUDDamage = -1;
	pPlayer->m_iTrain |= 0xc0;	// TRAIN_NEW (player.cpp)
	pPlayer->m_fWeapon = FALSE;
	pPlayer->m_fKnownItem = FALSE;
	pPlayer->m_pClientActiveItem = NULL;
	for ( int a = 0; a < MAX_AMMO_SLOTS; a++ )
		pPlayer->m_rgAmmoLast[a] = -1;
	// B430: a door cannot go on across a level change; a player carried mid-door arrived frozen, faded to black and
	// hidden. The door's own end, here: control back, the fade lifted, shown again (PostThink)
	if ( pPlayer->b_GoingThroughADoor )
	{
		pPlayer->b_GoingThroughADoor = false;
		pPlayer->EnableControl( TRUE );
		UTIL_ScreenFade( pPlayer, Vector( 0, 0, 0 ), 0.5, 0, 255, FFADE_IN );
		g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %s came in mid-door, the door is over\n", STRING( pPlayer->pev->netname ) ) );
	}
	// B506: nor can a scene: a teammate walked into an exit while this player watched one, and he kept its black bars
	// and hidden health on every level after (m_DrawBars, HIDEHUD_HEALTH), as the scene's end never came. Not when a
	// scene of this level already holds his view
	if ( ( pPlayer->m_DrawBars || ( pPlayer->m_iHideHUD & HIDEHUD_HEALTH ) ) && !pPlayer->IsInCutscene() && !pPlayer->m_bInPhoneCall &&
		pPlayer->m_iUsingComputerOrPhone != 3 )
	{
		pPlayer->m_DrawBars = 0;
		pPlayer->m_iHideHUD &= ~HIDEHUD_HEALTH;
		g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %s came in mid-scene, its bars and hidden health are over\n", STRING( pPlayer->pev->netname ) ) );
	}
	// B476: nor can a phone call (cof_phonecall stays on its level): one carried mid-call kept fuser3 5 (no walking
	// or jumping), the bars, the hidden health and the call lock (charliee1w 3f4ff32c). Not when a call of this level
	// goes on (a save loaded mid-call)
	if ( pPlayer->m_bInPhoneCall || pPlayer->pev->fuser3 == 5 || pPlayer->m_iUsingComputerOrPhone == 3 )
	{
		bool bCall = false;
		CBaseEntity *pCall = NULL;
		while ( !bCall && ( pCall = UTIL_FindEntityByClassname( pCall, "cof_phonecall" ) ) != NULL )
			bCall = pCall->pev->nextthink > 0;
		if ( !bCall )
		{
			pPlayer->m_bInPhoneCall = false;
			if ( pPlayer->pev->fuser3 == 5 )
				pPlayer->pev->fuser3 = 0;
			if ( pPlayer->m_iUsingComputerOrPhone == 3 )
				pPlayer->m_iUsingComputerOrPhone = 0;
			pPlayer->m_DrawBars = 0;
			pPlayer->m_iHideHUD &= ~HIDEHUD_HEALTH;
			g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %s came in mid-call, the call is over\n", STRING( pPlayer->pev->netname ) ) );
		}
	}
}

// CmdStart: this player's client sends commands, so it has loaded the level (PreThink returns early in some scenes)
void RcCoop_PlayerCmd( CBasePlayer *pPlayer )
{
	CHalfLifeStoryCoop *pRules = RcCoopRules();
	if ( !pRules )
		return;
	int i = ENTINDEX( pPlayer->edict() );
	if ( i >= 1 && i <= 32 )
	{
		// B402: the engine sends a carried player's client nothing while it loads the new level (hw.dll rc_coop.c
		// RcCoop_Inactivate), but the level runs as soon as the host is in, and what the game sent this player then
		// (the reset of the HUD, the end of a scene's hidden HUD and black bars, the weapons) is lost, though the
		// game counts it as sent: the first command on the level gets the client all of it again
		if ( pRules->m_flSeen[i] == 0 )
			RcCoopResendState( pPlayer );
		pRules->m_flSeen[i] = gpGlobals->time > 0 ? gpGlobals->time : 0.001;
	}
}

static void RcCoopSyncKeys( void );
static void RcCoopSyncCodes( void );
static void RcCoopSyncNightmare( void );	// B396
static void RcCoopDoorWaitFrame( void );	// B550
static float s_flRcNextKeySync;

void CHalfLifeStoryCoop::Think( void )
{
	// the police co-op sometimes sends a monster Simon after the players; not in Simon's own story
	m_bSpawningSimon = false;

	CHalfLifeMultiplay::Think();

	RcCoopHold();	// B472
	RcDoor_Frame( false );	// B549: teammates going through a door alongside its user come out
	RcCoopDoorWaitFrame();	// B550

	// B503: a player a group teleport found in a door or on a ladder goes to the group once he is out
	for ( int f = 1; f <= gpGlobals->maxClients && f <= 32; f++ )
	{
		if ( !m_flRcFollow[f] )
			continue;
		CBaseEntity *pEnt = UTIL_PlayerByIndex( f );
		if ( !RcCoopPresent( pEnt ) || gpGlobals->time > m_flRcFollow[f] )
		{
			m_flRcFollow[f] = 0;
			continue;
		}
		CBasePlayer *pPlayer = (CBasePlayer *)pEnt;
		if ( pPlayer->b_GoingThroughADoor || pPlayer->IsOnLadder() )
			continue;
		m_flRcFollow[f] = 0;
		Vector vecSpot = RcCoopAnyFreeSpot( m_vecRcFollow[f], pPlayer->edict() );
		UTIL_SetOrigin( pPlayer, vecSpot );
		pPlayer->m_vPreviousOrigin = vecSpot;
		pPlayer->pev->velocity = pPlayer->pev->basevelocity = g_vecZero;
		pPlayer->pev->flags &= ~FL_ONGROUND;
		g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %s followed the group's teleport after his door or ladder\n", STRING( pPlayer->pev->netname ) ) );
	}

	// G6 (B390): a vote lasts while its player is in a scene (the cameras of one scene follow each other)
	for ( int v = 1; v <= gpGlobals->maxClients && v <= 32; v++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( v );
		if ( !pEnt || !pEnt->IsPlayer() || pEnt->pev->fuser4 != 0 || !( (CBasePlayer *)pEnt )->IsInCutscene() || CVAR_GET_FLOAT( "rc_coop_skip" ) != 0 )
			s_bRcSkipVote[v] = false;
	}

	// G6: the skip ends once no player is in a scene any more
	if ( CVAR_GET_FLOAT( "rc_coop_skip" ) != 0 )
	{
		bool bScene = false;
		for ( int i = 1; i <= gpGlobals->maxClients && !bScene; i++ )
		{
			CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
			if ( pEnt && pEnt->IsPlayer() && pEnt->pev->fuser4 == 0 && ( (CBasePlayer *)pEnt )->IsInCutscene() )
				bScene = true;
		}
		if ( !bScene )
		{
			CVAR_SET_FLOAT( "rc_coop_skip", 0 );
			g_engfuncs.pfnServerPrint( "ReCoF co-op: the scene is over, normal speed again\n" );
		}
	}

	if ( gpGlobals->time >= s_flRcNextKeySync || gpGlobals->time < s_flRcNextKeySync - 2 )
	{
		s_flRcNextKeySync = gpGlobals->time + 1;
		RcCoopScaleMonsters();
		RcCoopSyncKeys();
		RcCoopSyncCodes();
		RcCoopSyncNightmare();	// B396

		// B391: a saved player whose player has not come back once the load's 90 s are over (hw.dll rc_coop.c
		// RC_CARRY_SECONDS) is put away as one who left (client.cpp ClientDisconnect's marks): unseen, not solid
		if ( m_flLevelStart >= 0 && gpGlobals->time - m_flLevelStart > 95 )
		{
			for ( int g = 1; g <= gpGlobals->maxClients; g++ )
			{
				CBaseEntity *pEnt = UTIL_PlayerByIndex( g );
				if ( !pEnt || !pEnt->IsPlayer() || pEnt->pev->fuser4 != 0 || !RcCoopNoClient( pEnt ) )
					continue;
				pEnt->pev->fuser4 = 1;
				pEnt->pev->takedamage = DAMAGE_NO;
				pEnt->pev->solid = SOLID_NOT;
				pEnt->pev->flags |= FL_NOTARGET;
				pEnt->pev->effects |= EF_NODRAW;
				UTIL_SetOrigin( pEnt, pEnt->pev->origin );
				g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: saved player %d had nobody come back for him; put away\n", g ) );
			}
		}

		// G13: an autosave once every player is in on a new level
		if ( !m_bAutosaved )
		{
			if ( m_flLevelStart < 0 || m_flLevelStart > gpGlobals->time )
				m_flLevelStart = gpGlobals->time;
			bool bAllIn = true;
			int nIn = 0;
			for ( int i = 1; i <= gpGlobals->maxClients && i <= 32; i++ )
			{
				CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
				if ( !RcCoopPresent( pEnt ) )
					continue;
				if ( m_flSeen[i] == 0 || gpGlobals->time - m_flSeen[i] > 2.0 )
					bAllIn = false;
				nIn++;
			}
			if ( rc_coop_autosave.value && bAllIn && nIn && gpGlobals->time - m_flLevelStart > 3 && RcCoopAnyoneInChair() )
				m_bAutosaved = true;	// B487: not this level (its ride ends in the ending's scenes)
			else if ( rc_coop_autosave.value && bAllIn && nIn && gpGlobals->time - m_flLevelStart > 3 )
			{
				m_bAutosaved = true;
				SERVER_COMMAND( "save coop_auto\n" );
				g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: autosave on %s (load coop_auto)\n", STRING( gpGlobals->mapname ) ) );
			}
		}
	}

	for ( int i = 1; i <= gpGlobals->maxClients && i <= 32; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( !pEnt || !pEnt->IsPlayer() )
			continue;

		// G3: a player whose client has not sent a command on this level yet (loading after a level change) cannot
		// be hurt until it does
		bool bLoading = m_flSeen[i] == 0 || gpGlobals->time - m_flSeen[i] > 2.0;
		if ( bLoading && !m_bProtected[i] && pEnt->pev->takedamage != DAMAGE_NO && pEnt->IsAlive() )
		{
			m_bProtected[i] = true;
			m_flOldTakeDamage[i] = pEnt->pev->takedamage;
			pEnt->pev->takedamage = DAMAGE_NO;
		}
		else if ( !bLoading && m_bProtected[i] )
		{
			m_bProtected[i] = false;
			if ( pEnt->pev->takedamage == DAMAGE_NO )
				pEnt->pev->takedamage = m_flOldTakeDamage[i];
		}

		// client.dll shows the map's sky to story co-op players (gl_sky.cpp)
		if ( strcmp( g_engfuncs.pfnGetPhysicsKeyValue( pEnt->edict(), "rcc" ), "1" ) )
			g_engfuncs.pfnSetPhysicsKeyValue( pEnt->edict(), "rcc", "1" );
		// B414: the model follows rc_coop_simon (the lobby's campaign, a co-op save's setting)
		ClientUserInfoChanged( (CBasePlayer *)pEnt, g_engfuncs.pfnGetInfoKeyBuffer( pEnt->edict() ) );
		// B411: client.dll draws Simon's sleeves and hands on the weapons (gl_studiomodel.cpp)
		const char *pszSimon = rc_coop_simon.value ? "1" : "0";
		if ( strcmp( g_engfuncs.pfnGetPhysicsKeyValue( pEnt->edict(), "rcs" ), pszSimon ) )
			g_engfuncs.pfnSetPhysicsKeyValue( pEnt->edict(), "rcs", pszSimon );

		// G12: when a player dies, note his weapons and ammo and bring his respawn forward; when he is back (the
		// respawn takes everything away), give them back
		CBasePlayer *pPl = (CBasePlayer *)pEnt;
		bool bAlive = pEnt->IsAlive() && pEnt->pev->deadflag == DEAD_NO;
		if ( m_bWasAlive[i] && !bAlive && pEnt->pev->fuser4 == 0 && rc_coop_respawn_time.value > 0 )
		{
			m_iSnapItems[i] = 0;
			int rgRcOrder[24];
			RcInv_s rcInv;
			RcCoopInvRead( pPl, rcInv );
			for ( int s = 0; s < MAX_ITEM_TYPES; s++ )
			{
				for ( CBasePlayerItem *pItem = pPl->m_rgpPlayerItems[s]; pItem && m_iSnapItems[i] < 24; pItem = pItem->m_pNext )
				{
					// B478: with its clip and its inventory place, so they come back in that order (CrawlerD 102f355)
					int n = m_iSnapItems[i]++;
					m_iszSnapItem[i][n] = pItem->pev->classname;
					m_iSnapClip[i][n] = pItem->GetWeaponPtr() ? ( (CBasePlayerWeapon *)pItem )->m_iClip : -1;
					rgRcOrder[n] = RcCoopInvIndex( rcInv, STRING( pItem->pev->classname ) );
				}
			}
			for ( int a = 1; a < m_iSnapItems[i]; a++ )
			{
				for ( int b = a; b > 0 && rgRcOrder[b - 1] > rgRcOrder[b]; b-- )
				{
					int o = rgRcOrder[b]; rgRcOrder[b] = rgRcOrder[b - 1]; rgRcOrder[b - 1] = o;
					string_t z = m_iszSnapItem[i][b]; m_iszSnapItem[i][b] = m_iszSnapItem[i][b - 1]; m_iszSnapItem[i][b - 1] = z;
					int c = m_iSnapClip[i][b]; m_iSnapClip[i][b] = m_iSnapClip[i][b - 1]; m_iSnapClip[i][b - 1] = c;
				}
			}
			for ( int q = 1; q <= 3; q++ )
			{
				CBasePlayerItem *pQuick = RcCoopQuickSel( pPl, q )[0] ? RcCoopFindItem( pPl, RcCoopQuickSel( pPl, q ) ) : NULL;
				m_iszSnapQuick[i][q] = pQuick ? pQuick->pev->classname : 0;
			}
			memcpy( m_rgSnapAmmo[i], pPl->m_rgAmmo, sizeof( m_rgSnapAmmo[i] ) );
			m_bRestore[i] = true;
			float flBack = gpGlobals->time + rc_coop_respawn_time.value;
			if ( pPl->m_flDeadAutoReviveTime == 0 || pPl->m_flDeadAutoReviveTime > flBack )
				pPl->m_flDeadAutoReviveTime = flBack;
			if ( pPl->m_flCoopRespawnDelayTime > flBack )
				pPl->m_flCoopRespawnDelayTime = flBack;
		}
		else if ( !m_bWasAlive[i] && bAlive && m_bRestore[i] )
		{
			m_bRestore[i] = false;
			for ( int n = 0; n < m_iSnapItems[i]; n++ )
			{
				const char *pszName = STRING( m_iszSnapItem[i][n] );
				if ( !pPl->HasNamedPlayerItem( pszName ) )
					pPl->GiveNamedItem( pszName, false, true );
			}
			for ( int a = 0; a < MAX_AMMO_SLOTS; a++ )
			{
				if ( m_rgSnapAmmo[i][a] > pPl->m_rgAmmo[a] )
					pPl->m_rgAmmo[a] = m_rgSnapAmmo[i][a];
			}
			// B478: each clip as it was, and the quick slots on the weapons' new inventory places
			for ( int n = 0; n < m_iSnapItems[i]; n++ )
			{
				CBasePlayerItem *pItem = m_iSnapClip[i][n] >= 0 ? RcCoopFindItem( pPl, STRING( m_iszSnapItem[i][n] ) ) : NULL;
				if ( pItem && pItem->GetWeaponPtr() )
					( (CBasePlayerWeapon *)pItem )->m_iClip = m_iSnapClip[i][n];
			}
			RcInv_s rcInvNow;
			RcCoopInvRead( pPl, rcInvNow );
			for ( int q = 1; q <= 3; q++ )
			{
				if ( !m_iszSnapQuick[i][q] )
					continue;
				const char *pszQuick = STRING( m_iszSnapQuick[i][q] );
				int iInv = RcCoopInvIndex( rcInvNow, pszQuick );
				if ( iInv >= 7 || strlen( pszQuick ) >= 128 )
					continue;
				strcpy( RcCoopQuickSel( pPl, q ), pszQuick );
				pPl->m_iQuickSlotSlot[q] = iInv;
				MESSAGE_BEGIN( MSG_ONE, gmsgQSlotUpdate, NULL, pPl->pev );
					WRITE_BYTE( q );
					WRITE_BYTE( iInv );
				MESSAGE_END();
			}
			g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %s is back with %d weapons\n", STRING( pEnt->pev->netname ), m_iSnapItems[i] ) );
		}
		m_bWasAlive[i] = bAlive;

		// G2: a player who arrived inside an exit (the way back, often) arms its exits once he has stood outside all
		if ( !m_bArmed[i] && !bLoading )
		{
			bool bInside = false;
			CBaseEntity *pExit = NULL;
			while ( !bInside && ( pExit = UTIL_FindEntityByClassname( pExit, "trigger_changelevel" ) ) != NULL )
				bInside = RcCoopInside( pExit, pEnt );
			if ( !bInside )
				m_bArmed[i] = true;
		}

		// B477: a living present player hidden (EF_NODRAW) for 3 s outside a scene, a door, a ladder, a third-person
		// scene or a hold is shown again: the others could not see him and his proximity voice went silent with him
		// (AddToFullPack sends no entity whose effects are exactly EF_NODRAW; CrawlerD 67d7b6e). B430 ends the door's
		// own hide
		bool bRcHidden = ( pEnt->pev->effects & EF_NODRAW ) && !bLoading && RcCoopPresent( pEnt ) && !pPl->IsInCutscene() &&
			!pPl->pDeathScene && !pPl->IsOnLadder() && !pPl->b_GoingThroughADoor && pEnt->pev->movetype == MOVETYPE_WALK &&
			!( pEnt->pev->flags & FL_FROZEN ) && !pPl->m_pAttachedCamera && !pPl->m_hCurrentCamera;
		if ( !bRcHidden )
			m_flHidden[i] = 0;
		else if ( m_flHidden[i] == 0 || m_flHidden[i] > gpGlobals->time )
			m_flHidden[i] = gpGlobals->time;
		else if ( gpGlobals->time - m_flHidden[i] > 3.0 )
		{
			m_flHidden[i] = 0;
			pEnt->pev->effects &= ~EF_NODRAW;
			pPl->m_bRcDoorHid = false;
			g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %s was left hidden, shown again\n", STRING( pEnt->pev->netname ) ) );
		}

		// B466: the engine relinks a player after his move only while he is solid (sv_user.c SV_RunCmd), so a
		// non-solid one kept the box he had when he became non-solid, and the exit touch below, B419 and B403 read
		// that old box (charliee1w 2a44d5a6): linked again here, without touches
		if ( pEnt->pev->solid == SOLID_NOT && pEnt->pev->movetype == MOVETYPE_WALK && RcCoopPresent( pEnt ) )
			UTIL_SetOrigin( pEnt, pEnt->pev->origin );

		// Cry of Fear makes players who overlap SOLID_NOT until they part (CBasePlayer::PreThink), and triggers do not
		// touch a non-solid player: one standing in a level exit touches it here instead (the exit's own rules apply;
		// an exit fired by the map has no touch), once out of any scene, as a scene may carry a player through an exit
		if ( m_bArmed[i] && !bLoading && !g_bRcHolding && pEnt->pev->solid == SOLID_NOT && RcCoopPresent( pEnt ) && pEnt->pev->movetype != MOVETYPE_NOCLIP &&
			!( (CBasePlayer *)pEnt )->IsInCutscene() )
		{
			CBaseEntity *pExit = NULL;
			while ( ( pExit = UTIL_FindEntityByClassname( pExit, "trigger_changelevel" ) ) != NULL )
			{
				if ( RcCoopInside( pExit, pEnt ) )
				{
					pExit->Touch( pEnt );
					break;
				}
			}
		}

		// B419: the same rule kept such a player off every other trigger: after c_nightmare both players arrive on
		// one spot in c_start, inside the black box (func_wall_toggle "block") whose trigger_once (the same box)
		// starts the opening scene and opens it; neither touched it, the scene never started and B403 below found no
		// room in the box. A player non-solid only because he overlaps a teammate touches the triggers he stands in,
		// as the engine does for a solid one (exits keep their own rules above)
		if ( !bLoading && !g_bRcHolding && pEnt->pev->solid == SOLID_NOT && RcCoopPresent( pEnt ) && pEnt->pev->movetype == MOVETYPE_WALK &&
			!pPl->IsInCutscene() )
		{
			bool bOverlap = false;
			for ( int j = 1; j <= gpGlobals->maxClients && !bOverlap; j++ )
			{
				CBaseEntity *pOther = UTIL_PlayerByIndex( j );
				bOverlap = pOther != pEnt && RcCoopPresent( pOther ) && pOther->Intersects( pEnt );
			}
			CBaseEntity *pList[64];
			int nList = bOverlap ? UTIL_EntitiesInBox( pList, 64, pEnt->pev->absmin, pEnt->pev->absmax, 0 ) : 0;
			for ( int n = 0; n < nList; n++ )
			{
				CBaseEntity *pTrig = pList[n];
				if ( pTrig == pEnt || pTrig->pev->solid != SOLID_TRIGGER || ( pTrig->pev->flags & FL_KILLME ) ||
					FClassnameIs( pTrig->pev, "trigger_changelevel" ) )
					continue;
				pTrig->Touch( pEnt );
			}
		}

		// B403: the same rule leaves players a level change brings in on top of each other (a scene's end puts every
		// player on one spot) SOLID_NOT for good, and the new map's triggers where they arrive never go off (c_nightmare's
		// opening scene and camera): once they have stood together 1.5 s outside any scene, the later one steps aside
		CBaseEntity *pOver = NULL;
		if ( !bLoading && pEnt->pev->solid == SOLID_NOT && pEnt->pev->movetype == MOVETYPE_WALK && RcCoopPresent( pEnt ) &&
			!pPl->IsInCutscene() )
		{
			for ( int j = 1; j < i && !pOver; j++ )
			{
				CBaseEntity *pOther = UTIL_PlayerByIndex( j );
				if ( RcCoopPresent( pOther ) && pOther->Intersects( pEnt ) )
					pOver = pOther;
			}
		}
		if ( !pOver )
			m_flOverlap[i] = 0;
		else if ( m_flOverlap[i] == 0 )
			m_flOverlap[i] = gpGlobals->time;
		else if ( gpGlobals->time - m_flOverlap[i] > 1.5 )
		{
			m_flOverlap[i] = 0;
			if ( RcCoopStepAside( pEnt, pOver ) )
				g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: player %d stood inside player %d and stepped aside\n", i,
					ENTINDEX( pOver->edict() ) ) );
			else
				m_flOverlap[i] = gpGlobals->time + 5;	// no room here: try again in a while
		}
	}
}

// G1: put the players who are not in the exit next to the one who reached it, with their weapons, so the level
// change carries them from where it carries him
// player i put beside pActivator: rings round him, facing away first (RcCoopSpotFree), else any free spot
static void RcCoopBringTo( CBasePlayer *pPlayer, CBaseEntity *pActivator, int i, const char *pszWhy )
{
	{
		Vector vecBase = pActivator->pev->origin;
		Vector vecSpot = vecBase;
		static const float s_flRing[3] = { 40, 72, 104 };
		bool bFound = false;
		for ( int r = 0; r < 3 && !bFound; r++ )
		{
			for ( int a = 0; a < 8 && !bFound; a++ )
			{
				float yaw = ( pActivator->pev->angles.y + 180 + a * 45 + i * 15 ) * ( M_PI / 180.0 );
				Vector vecTry = vecBase + Vector( cos( yaw ) * s_flRing[r], sin( yaw ) * s_flRing[r], 0 );
				if ( RcCoopSpotFree( vecBase, vecTry, pPlayer->edict() ) )
				{
					vecSpot = vecTry;
					bFound = true;
				}
			}
		}
		if ( !bFound )
			vecSpot = RcCoopAnyFreeSpot( vecBase, pPlayer->edict() );
		UTIL_SetOrigin( pPlayer, vecSpot );
		pPlayer->pev->velocity = g_vecZero;
		for ( int s = 0; s < MAX_ITEM_TYPES; s++ )
		{
			for ( CBasePlayerItem *pItem = pPlayer->m_rgpPlayerItems[s]; pItem; pItem = pItem->m_pNext )
				UTIL_SetOrigin( pItem, vecSpot );
		}
		g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %s brought %s\n", STRING( pPlayer->pev->netname ), pszWhy ) );
	}
}

static void RcCoopGather( CBaseEntity *pTrigger, CBaseEntity *pActivator )
{
	if ( !pActivator || !pActivator->IsPlayer() )
		return;
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( !RcCoopPresent( pEnt ) || pEnt == pActivator || ( pTrigger && RcCoopInside( pTrigger, pEnt ) ) )
			continue;
		RcCoopBringTo( (CBasePlayer *)pEnt, pActivator, i, "to the exit" );
	}
}

// B482: the group brought beside pAnchor where the map carries one player only (the c_lake boat, the c_bridge
// carriage): players in a scene, on a ladder, in a door or held by a third-person scene stay; the others fade in
// there and arm the exits only after standing outside them (B463)
static void RcCoopGatherTo( CBaseEntity *pAnchor, const char *pszWhy, bool bWatchers );

void RcCoop_GatherTo( CBaseEntity *pAnchor, const char *pszWhy )
{
	RcCoopGatherTo( pAnchor, pszWhy, false );
}

// B491: bWatchers also brings players held in a scene (their view stays on its cameras)
static void RcCoopGatherTo( CBaseEntity *pAnchor, const char *pszWhy, bool bWatchers )
{
	CHalfLifeStoryCoop *pRules = RcCoopRules();
	if ( !pRules || !pAnchor || !pAnchor->IsPlayer() )
		return;
	for ( int i = 1; i <= gpGlobals->maxClients && i <= 32; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( !RcCoopPresent( pEnt ) || pEnt == pAnchor )
			continue;
		CBasePlayer *pPlayer = (CBasePlayer *)pEnt;
		if ( ( pPlayer->IsInCutscene() && !bWatchers ) || pPlayer->IsOnLadder() || pPlayer->b_GoingThroughADoor || pPlayer->pDeathScene )
			continue;
		RcCoopBringTo( pPlayer, pAnchor, i, pszWhy );
		pPlayer->pev->angles = pAnchor->pev->angles;
		pPlayer->pev->angles.x = pPlayer->pev->angles.z = 0;
		pPlayer->pev->fixangle = TRUE;
		pPlayer->m_vPreviousOrigin = pPlayer->pev->origin;
		pRules->m_bArmed[i] = false;
		if ( !pPlayer->IsInCutscene() )	// a watcher's view is on the scene's camera
			UTIL_ScreenFade( pPlayer, Vector( 0, 0, 0 ), 1, 0, 255, FFADE_IN );
	}
}

// G9: the item_key pickups (keys, fuses, story objects) belong to the group: each living player carries as many of
// each as the group picked up and has not used, so any of them can open the lock; a lock that takes one takes it from
// everyone. Kept across level changes with the players' inventories.
#define RC_TEAM_KEYS	16
static string_t s_iszKeyName[RC_TEAM_KEYS], s_iszKeyModel[RC_TEAM_KEYS], s_iszKeyClass[RC_TEAM_KEYS];
static int s_iKeyCount[RC_TEAM_KEYS];

static int RcCoopKeySlot( const char *pszName )
{
	for ( int k = 0; k < RC_TEAM_KEYS; k++ )
	{
		if ( s_iszKeyName[k] && !strcmp( STRING( s_iszKeyName[k] ), pszName ) )
			return k;
	}
	return -1;
}

static int RcCoopCopies( CBasePlayer *pPlayer, const char *pszName )
{
	int n = 0, last = pPlayer->m_bThreeSlots ? 3 : 6;
	for ( int s = 1; s <= last; s++ )
	{
		if ( pPlayer->inventorySlot[s] && FStrEq( STRING( pPlayer->inventorySlot[s] ), pszName ) )
			n++;
	}
	return n;
}

// CKeyitemModel::Use, after the item went into the taker's inventory
void RcCoop_KeyTaken( CBaseEntity *pKey )
{
	if ( !RcCoopRules() || FStringNull( pKey->pev->message ) )
		return;
	int k = RcCoopKeySlot( STRING( pKey->pev->message ) );
	// B475: a key a player dropped (CBasePlayer::DropKeyItem marks it 666) is one the group already has: picked up
	// again it is the same key, not a second one for everyone (UNIk0z f589896)
	if ( k >= 0 && s_iKeyCount[k] > 0 && pKey->pev->armorvalue == 666 )
	{
		g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %s is back from the floor (%d)\n", STRING( s_iszKeyName[k] ), s_iKeyCount[k] ) );
		return;
	}
	if ( k < 0 )
	{
		for ( k = 0; k < RC_TEAM_KEYS && s_iszKeyName[k]; k++ )
			;
		if ( k >= RC_TEAM_KEYS )
			return;
		s_iszKeyName[k] = pKey->pev->message;
		s_iszKeyModel[k] = pKey->pev->model;
		s_iszKeyClass[k] = pKey->pev->classname;
		s_iKeyCount[k] = 0;
	}
	s_iKeyCount[k]++;
	g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %s is the group's now (%d)\n", STRING( s_iszKeyName[k] ), s_iKeyCount[k] ) );
}

// CCofYesNo took the item from the player who used it: one fewer for the group, and from everyone
void RcCoop_KeyUsed( const char *pszItem )
{
	if ( !RcCoopRules() )
		return;
	int k = RcCoopKeySlot( pszItem );
	if ( k < 0 )
		return;
	if ( s_iKeyCount[k] > 0 )
		s_iKeyCount[k]--;
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( !pEnt || !pEnt->IsPlayer() )
			continue;
		CBasePlayer *pPlayer = (CBasePlayer *)pEnt;
		while ( RcCoopCopies( pPlayer, pszItem ) > s_iKeyCount[k] )
			pPlayer->HasItemInSlot( (char *)pszItem, true );
	}
	g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %s used, the group has %d\n", pszItem, s_iKeyCount[k] ) );
	if ( s_iKeyCount[k] == 0 )
		s_iszKeyName[k] = 0;
}

// B475: the map took every inventory item (cof_clearitems flag 1, Chapter 6's bag loss): the group forgets its keys,
// or RcCoopSyncKeys gave them back within a second (charliee1w 3280f597, a3d8634f)
void RcCoop_KeysForget( void )
{
	if ( !RcCoopRules() )
		return;
	for ( int k = 0; k < RC_TEAM_KEYS; k++ )
	{
		s_iszKeyName[k] = 0;
		s_iKeyCount[k] = 0;
	}
	g_engfuncs.pfnServerPrint( "ReCoF co-op: the inventory was taken, the group's keys are gone\n" );
}

// every second from Think: each living player gets the group's items he lacks (a newcomer, a respawn, the others
// after a pickup), if his inventory has room
static void RcCoopSyncKeys( void )
{
	for ( int k = 0; k < RC_TEAM_KEYS; k++ )
	{
		if ( !s_iszKeyName[k] || s_iKeyCount[k] <= 0 )
			continue;
		const char *pszName = STRING( s_iszKeyName[k] );
		for ( int i = 1; i <= gpGlobals->maxClients; i++ )
		{
			CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
			if ( !RcCoopPresent( pEnt ) )
				continue;
			CBasePlayer *pPlayer = (CBasePlayer *)pEnt;
			int n = RcCoopCopies( pPlayer, pszName );
			while ( n < s_iKeyCount[k] && pPlayer->AddItemToInventory( s_iszKeyName[k], s_iszKeyModel[k], s_iszKeyClass[k] ) )
				n++;
		}
	}
}

// G10: Cry of Fear rolls a padlock combination for each player when he spawns and gives a keypad code to the player
// who reads it (a note, a screen, a board); a code one player reads would not open the lock for another. The group
// shares one: the padlock combination of the first player in the game, and the keypad code a player learned last.
static int s_iRcTeamPad[5];
static int s_iRcTeamKeypad = -1;
static int s_iRcKeypadSet[33];	// what this player's keypad code was set to by the group (to tell his own news)
static int s_iRcTeamKeypadType = -1;	// the keypad picture (m_iKeypadType: 0 off, 1 panel 2, 2 keypad 3), the same way
static int s_iRcKeypadTypeSet[33];

static void RcCoopSyncCodes( void )
{
	int i;
	for ( i = 1; i <= gpGlobals->maxClients && i <= 32; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( !pEnt || !pEnt->IsPlayer() || pEnt->pev->fuser4 != 0 )
			continue;
		CBasePlayer *pPlayer = (CBasePlayer *)pEnt;
		if ( !s_iRcTeamPad[1] && pPlayer->m_iPadlockNumber[1] && pPlayer->m_iPadlockNumber[2] && pPlayer->m_iPadlockNumber[3] && pPlayer->m_iPadlockNumber[4] )
		{
			for ( int d = 1; d <= 4; d++ )
				s_iRcTeamPad[d] = pPlayer->m_iPadlockNumber[d];
		}
		// a code this player learned himself (different from what the group gave him) is the group's now; the group's
		// code he opened its keypad with (KeypadNumberCheck makes it 9999) is used up for everyone
		int code = pPlayer->m_iKeypadNumber;
		bool bUsed = code == 9999 && s_iRcTeamKeypad > 0 && s_iRcTeamKeypad != 9999 && s_iRcKeypadSet[i] == s_iRcTeamKeypad;
		if ( code != s_iRcTeamKeypad && code != s_iRcKeypadSet[i] && ( ( code != 9999 && code != 0 ) || bUsed ) )
		{
			if ( bUsed )
				g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: keypad code %d used, for the group\n", s_iRcTeamKeypad ) );
			else
				g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: keypad code %d is the group's now\n", code ) );
			s_iRcTeamKeypad = code;
		}
		// the keypad a code is for: the screen shows its picture by the opener's own type (cof_keypad), and a type
		// a player was given himself (cof_keypad2, 3 and 4) is the group's
		if ( pPlayer->m_iKeypadType != s_iRcTeamKeypadType && pPlayer->m_iKeypadType != s_iRcKeypadTypeSet[i] )
			s_iRcTeamKeypadType = pPlayer->m_iKeypadType;
	}
	for ( i = 1; i <= gpGlobals->maxClients && i <= 32; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( !pEnt || !pEnt->IsPlayer() || pEnt->pev->fuser4 != 0 )
			continue;
		CBasePlayer *pPlayer = (CBasePlayer *)pEnt;
		if ( s_iRcTeamPad[1] )
		{
			for ( int d = 1; d <= 4; d++ )
				pPlayer->m_iPadlockNumber[d] = s_iRcTeamPad[d];
		}
		if ( s_iRcTeamKeypad >= 0 && pPlayer->m_iKeypadNumber != s_iRcTeamKeypad )
			pPlayer->m_iKeypadNumber = s_iRcTeamKeypad;
		s_iRcKeypadSet[i] = pPlayer->m_iKeypadNumber;
		if ( s_iRcTeamKeypadType >= 0 && pPlayer->m_iKeypadType != s_iRcTeamKeypadType )
			pPlayer->m_iKeypadType = s_iRcTeamKeypadType;
		s_iRcKeypadTypeSet[i] = pPlayer->m_iKeypadType;
	}
}

// G5: cof_teleport moved pMoved. Fired by a trigger he walked into (an arrival room), the other players standing in
// that trigger go with him (its wait would ignore their touch and leave them behind); fired by anything else (a scene's
// relays and managers), the whole group goes. They land on free spots around him.
// B503: walk-in triggers whose teleport takes the whole group: the c_ending5b arrival shaft's floor plate is its only
// way out and a trigger_once, so a teammate still falling or arriving when the first one stepped on it was sealed in
static const char *s_pszRcTeleportAll[][2] = { { "c_ending5b", "*2" } };

static bool RcCoopTeleportsAll( CBaseEntity *pCaller )
{
	if ( !pCaller || FStringNull( pCaller->pev->model ) )
		return false;
	for ( int i = 0; i < (int)ARRAYSIZE( s_pszRcTeleportAll ); i++ )
	{
		if ( !stricmp( STRING( gpGlobals->mapname ), s_pszRcTeleportAll[i][0] ) && !stricmp( STRING( pCaller->pev->model ), s_pszRcTeleportAll[i][1] ) )
			return true;
	}
	return false;
}

void RcCoop_TeleportFollow( CBasePlayer *pMoved, CBaseEntity *pCaller )
{
	CHalfLifeStoryCoop *pRules = RcCoopRules();
	if ( !pRules || !pMoved )
		return;
	bool bTrigger = pCaller && pCaller->pev->solid == SOLID_TRIGGER && !strncmp( STRING( pCaller->pev->classname ), "trigger_", 8 ) && !RcCoopTeleportsAll( pCaller );
	int nMoved = 0;

	// B464: a teleport into a deadly drop (a working trigger_hurt of 100 or more where he lands: the c_roofs fall, the
	// c_city flower trap) is the end of that player only; the group stayed with him and died (charliee1w 907d5013)
	// B503: a switched off one too: c_city's flower trap turns its hurt (kallamig, 1000) on 5.5 s after the teleport, and
	// c_bridge's carriage timeout (telemia) turns tjenahaha on 3 s after, so the group went along and died with him
	CBaseEntity *pRcHurt = NULL;
	while ( ( pRcHurt = UTIL_FindEntityByClassname( pRcHurt, "trigger_hurt" ) ) != NULL )
	{
		if ( pRcHurt->pev->dmg >= 100 && RcCoopInside( pRcHurt, pMoved ) )
		{
			g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %s went into a deadly place, the group stays\n", STRING( pMoved->pev->netname ) ) );
			return;
		}
	}

	// the destination is taken by a teammate who came the same way before: step beside him
	for ( int j = 1; j <= gpGlobals->maxClients; j++ )
	{
		CBaseEntity *pOther = UTIL_PlayerByIndex( j );
		if ( !RcCoopPresent( pOther ) || pOther == pMoved )
			continue;
		Vector d = pOther->pev->origin - pMoved->pev->origin;
		if ( fabs( d.x ) >= 32 || fabs( d.y ) >= 32 || fabs( d.z ) >= 72 )
			continue;
		Vector vecBase = pOther->pev->origin;
		static const float s_flStep[3] = { 40, 72, 104 };
		for ( int r = 0; r < 3; r++ )
		{
			bool bDone = false;
			for ( int a = 0; a < 8 && !bDone; a++ )
			{
				float yaw = ( pMoved->pev->angles.y + 90 + a * 45 ) * ( M_PI / 180.0 );
				Vector vecTry = vecBase + Vector( cos( yaw ) * s_flStep[r], sin( yaw ) * s_flStep[r], 0 );
				if ( RcCoopSpotFree( vecBase, vecTry, pMoved->edict() ) )
				{
					UTIL_SetOrigin( pMoved, vecTry );
					pMoved->m_vPreviousOrigin = vecTry;
					bDone = true;
				}
			}
			if ( bDone )
				break;
		}
		g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %s stepped beside a teammate at the teleport's end\n", STRING( pMoved->pev->netname ) ) );
		break;
	}
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( !RcCoopPresent( pEnt ) || pEnt == pMoved )
			continue;
		if ( bTrigger && !RcCoopInside( pCaller, pEnt ) )
			continue;
		CBasePlayer *pPlayer = (CBasePlayer *)pEnt;
		// B503: in a door's sequence the door puts him at its exit up to 2 s later, and a ladder holds him: he goes
		// once he is out (CHalfLifeStoryCoop::Think)
		if ( pPlayer->b_GoingThroughADoor || pPlayer->IsOnLadder() )
		{
			pRules->m_vecRcFollow[i] = pMoved->pev->origin;
			pRules->m_flRcFollow[i] = gpGlobals->time + 10;
			continue;
		}
		Vector vecBase = pMoved->pev->origin;
		Vector vecSpot = vecBase;
		static const float s_flRing[3] = { 40, 72, 104 };
		bool bFound = false;
		for ( int r = 0; r < 3 && !bFound; r++ )
		{
			for ( int a = 0; a < 8 && !bFound; a++ )
			{
				float yaw = ( pMoved->pev->angles.y + 180 + a * 45 + i * 15 ) * ( M_PI / 180.0 );
				Vector vecTry = vecBase + Vector( cos( yaw ) * s_flRing[r], sin( yaw ) * s_flRing[r], 0 );
				if ( RcCoopSpotFree( vecBase, vecTry, pPlayer->edict() ) )
				{
					vecSpot = vecTry;
					bFound = true;
				}
			}
		}
		if ( !bFound )
			vecSpot = RcCoopAnyFreeSpot( vecBase, pPlayer->edict() );
		UTIL_SetOrigin( pPlayer, vecSpot );
		pPlayer->m_vPreviousOrigin = vecSpot;
		pPlayer->pev->angles = pMoved->pev->angles;
		pPlayer->pev->v_angle.y = pMoved->pev->angles.y;
		pPlayer->pev->fixangle = TRUE;
		pPlayer->pev->velocity = pPlayer->pev->basevelocity = g_vecZero;
		pPlayer->pev->flags &= ~FL_ONGROUND;
		pPlayer->b_GoingThroughADoor = false;
		nMoved++;
	}
	if ( nMoved )
		g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %d teammates went along with %s (%s)\n", nMoved, STRING( pMoved->pev->netname ),
			bTrigger ? "same trigger" : "scene" ) );
}

// G13: a tape recorder in story co-op: the host's game saves the group (coop_tape); a guest's own game cannot, and the
// save screen would show only to the one who used it. On the hardest difficulty the user spends a tape as in single
// player. False outside story co-op.
bool RcCoop_TapeSave( CBasePlayer *pPlayer )
{
	if ( !RcCoopRules() )
		return false;
	// B452: the engine refuses a save while its first client (the host) is dead (host_cmd.c Host_ValidSave), and the
	// group was told its progress was recorded, and on the hardest difficulty the tape was spent (cofmp 7d6b434)
	CBaseEntity *pRcHost = UTIL_PlayerByIndex( 1 );
	if ( pRcHost && pRcHost->IsPlayer() && !pRcHost->IsAlive() )
	{
		UTIL_ProMessage( pPlayer, "The host is down: the game cannot be recorded now.", 3, false );
		return true;
	}
	if ( RcCoopAnyoneInChair() )
	{
		UTIL_ProMessage( pPlayer, "Not during the wheelchair ride.", 3, false );	// B487
		return true;
	}
	if ( CVAR_GET_FLOAT( "difficulty" ) == 4 )
	{
		pPlayer->m_iTapes--;
		if ( pPlayer->m_iTapes <= 0 )
			pPlayer->HasItemInSlot( "inventoryitems/cassettetape.txt", true );
	}
	SERVER_COMMAND( "save coop_tape\n" );
	EMIT_SOUND( ENT( pPlayer->pev ), CHAN_VOICE, "save/save_sequence.wav", 1, ATTN_NORM );
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( pEnt && pEnt->IsPlayer() && pEnt->pev->fuser4 == 0 )
			UTIL_ProMessage( (CBasePlayer *)pEnt, "The group's progress was recorded.", 3, false );
	}
	g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %s saved the group's game at a tape recorder (load coop_tape)\n", STRING( pPlayer->pev->netname ) ) );
	return true;
}

// B465: rc_unstuck (a client command): the player is put on a free spot next to a living teammate, for a stuck spot or
// a place a one-shot door or teleport cut him off from the group (charliee1w d184fb51, 300a0d0a, 5c60c624). Not while
// he is down, in a scene, on a ladder, going through a door or held, and once every 10 s
void RcCoop_Unstuck( CBasePlayer *pPlayer )
{
	static float s_flRcUnstuck[33];
	int me = ENTINDEX( pPlayer->edict() );
	if ( !RcCoopRules() || me < 1 || me > 32 )
		return;
	if ( !pPlayer->IsAlive() || pPlayer->IsInCutscene() || pPlayer->pDeathScene || pPlayer->IsOnLadder() ||
		pPlayer->b_GoingThroughADoor || pPlayer->pev->fuser3 != 0 || ( pPlayer->pev->flags & FL_FROZEN ) )
	{
		UTIL_ProMessage( pPlayer, "Not now.", 2, false );
		return;
	}
	if ( gpGlobals->time < s_flRcUnstuck[me] && gpGlobals->time > s_flRcUnstuck[me] - 11 )
	{
		UTIL_ProMessage( pPlayer, "Wait a moment.", 2, false );
		return;
	}
	CBasePlayer *pAnchor = RcCoopAnchor( pPlayer );
	if ( !pAnchor )
	{
		UTIL_ProMessage( pPlayer, "No teammate to go to.", 2, false );
		return;
	}
	s_flRcUnstuck[me] = gpGlobals->time + 10;
	Vector vecSpot = RcCoopAnyFreeSpot( pAnchor->pev->origin, pPlayer->edict() );
	UTIL_SetOrigin( pPlayer, vecSpot + Vector( 0, 0, 1 ) );
	pPlayer->m_vPreviousOrigin = pPlayer->pev->origin;
	pPlayer->pev->velocity = pPlayer->pev->basevelocity = g_vecZero;
	pPlayer->pev->angles = pAnchor->pev->angles;
	pPlayer->pev->angles.x = pPlayer->pev->angles.z = 0;
	pPlayer->pev->fixangle = TRUE;
	pPlayer->pev->flags &= ~FL_ONGROUND;
	int iRc = ENTINDEX( pPlayer->edict() );
	CHalfLifeStoryCoop *pRules = RcCoopRules();
	if ( pRules && iRc >= 1 && iRc <= 32 )
		pRules->m_bArmed[iRc] = false;	// B463: not straight into an exit
	g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %s used rc_unstuck, now next to %s\n", STRING( pPlayer->pev->netname ), STRING( pAnchor->pev->netname ) ) );
}

static EHANDLE s_hRcWaitExit;
static float s_flRcWaitStart, s_flRcWaitTouch, s_flRcWaitMsg;
static bool s_bRcGoNow;
static float s_flRcDoorGo;	// B550: until then the exit a door's group came through goes at once, 0 for none

// CChangeLevel::ChangeLevelNow: true to change level now. Outside story co-op always true.
bool RcCoop_ExitReady( CBaseEntity *pTrigger, CBaseEntity *pActivator, bool bTouch )
{
	CHalfLifeStoryCoop *pRules = RcCoopRules();
	if ( !pRules )
		return true;

	// G2: a player who arrived inside this exit does not send the group back until he has left every exit once
	if ( bTouch && pActivator && pActivator->IsPlayer() )
	{
		int a = ENTINDEX( pActivator->edict() );
		if ( a >= 1 && a <= 32 && !pRules->m_bArmed[a] )
			return false;
	}

	// B474: an exit went a moment ago (its change runs at the end of the frame): another one does not go as well,
	// or its gather and change replace the first's (UNIk0z f227563)
	int iMe = ENTINDEX( pTrigger->edict() );
	if ( pRules->m_flExitGone >= 0 && gpGlobals->time >= pRules->m_flExitGone && gpGlobals->time - pRules->m_flExitGone < 2.0 &&
		pRules->m_iExitGone != iMe )
		return false;

	int nPresent = 0, nInside = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( !RcCoopPresent( pEnt ) )
			continue;
		nPresent++;
		if ( RcCoopInside( pTrigger, pEnt ) )
			nInside++;
	}

	float flWait = rc_coop_exit_wait.value;
	// B550: the group came to this exit through a door together (or the door's wait is over): it goes at once, and
	// brings anyone who did not come
	bool bDoorGo = s_flRcDoorGo > 0 && gpGlobals->time < s_flRcDoorGo && s_flRcDoorGo - gpGlobals->time <= 10;
	bool bGo = !bTouch || flWait <= 0 || nInside >= nPresent || s_bRcGoNow || bDoorGo;
	if ( !bGo )
	{
		// B474: first come, first served: while someone stands in the exit that is waiting, another exit does not
		// start its own wait (two players in two exits restarted each other's every frame; charliee1w 125f7728)
		CBaseEntity *pWaiting = s_hRcWaitExit;
		if ( pWaiting && pWaiting != pTrigger && gpGlobals->time >= s_flRcWaitTouch && gpGlobals->time - s_flRcWaitTouch <= 2.0 )
			return false;

		// a new wait: another exit, or nobody stood in this one for 2 s
		if ( pWaiting != pTrigger || gpGlobals->time - s_flRcWaitTouch > 2.0 )
		{
			s_hRcWaitExit = pTrigger;
			s_flRcWaitStart = gpGlobals->time;
			s_flRcWaitMsg = 0;
		}
		s_flRcWaitTouch = gpGlobals->time;
		float flLeft = s_flRcWaitStart + flWait - gpGlobals->time;
		if ( flLeft <= 0 )
			bGo = true;
		else if ( gpGlobals->time >= s_flRcWaitMsg )
		{
			s_flRcWaitMsg = gpGlobals->time + 1.0;
			hudtextparms_t tp;
			memset( &tp, 0, sizeof( tp ) );
			tp.x = -1;
			tp.y = 0.8;
			tp.r1 = tp.g1 = tp.b1 = 220;
			tp.a1 = 255;
			tp.fadeoutTime = 0.2;
			tp.holdTime = 1.2;
			tp.channel = 4;
			RcCoopHudMessage( tp, UTIL_VarArgs( "Waiting at the exit: %d of %d players (%d s)", nInside, nPresent, (int)( flLeft + 0.99 ) ) );
			if ( (int)( flLeft + 0.99 ) % 5 == 0 )
				g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: waiting at the exit, %d of %d players, %d s\n", nInside, nPresent, (int)( flLeft + 0.99 ) ) );
		}
	}
	if ( !bGo )
		return false;

	RcDoor_Frame( true );	// B549: everyone in a door's sequence comes out first, or the change takes him along frozen
	RcCoopGather( pTrigger, pActivator );
	s_hRcWaitExit = NULL;
	s_bRcGoNow = false;
	s_flRcDoorGo = 0;
	pRules->m_iExitGone = iMe;	// B474
	pRules->m_flExitGone = gpGlobals->time;
	return true;
}

// rc_coop_go (server command, so the host's console or rcon only): the exit someone waits at fires now
static void RcCoopGo( void )
{
	s_bRcGoNow = true;
	g_engfuncs.pfnServerPrint( "ReCoF co-op: the next exit touch goes now\n" );
}

// B550: most of the story's ways into the next map are an inter_door whose exit is a closed black room around a
// trigger_changelevel (c_start's door to c_apartment1 and over a hundred more): the first player through waited in
// that room, in the dark, until the exit's wait brought the others. Such a door waits for the group in front of it
// instead: each player who uses it is ready while he stays by it, and when everyone is (or rc_coop_exit_wait is
// over) the ready ones go through together and the exit goes as soon as one of them is in it, bringing the rest
static EHANDLE s_hRcDoorWait;	// the door the group gathers at
static float s_flRcDoorWaitStart, s_flRcDoorWaitMsg;
static bool s_bRcDoorReady[33];	// this player used it and stays by it

// the door puts its user inside a level exit a player can walk into (EndSequence: its target, 37 units up)
static bool RcCoopDoorToExit( CBaseEntity *pDoor )
{
	if ( !pDoor->pev->target )
		return false;
	const char *pszTarget = STRING( pDoor->pev->target );
	if ( !strcmp( pszTarget, "endingcheck" ) )
		pszTarget = "ending_exit1";	// c_village3's door to the endings: each of its exits is such a room
	CBaseEntity *pTarget = UTIL_FindEntityByTargetname( NULL, pszTarget );
	if ( !pTarget )
		return false;
	Vector vecSpot = pTarget->pev->origin + Vector( 0, 0, 37 );
	Vector vecMin = vecSpot + VEC_HULL_MIN, vecMax = vecSpot + VEC_HULL_MAX;
	CBaseEntity *pTrig = NULL;
	while ( ( pTrig = UTIL_FindEntityByClassname( pTrig, "trigger_changelevel" ) ) != NULL )
	{
		if ( pTrig->pev->spawnflags & 2 )
			continue;	// SF_CHANGELEVEL_USEONLY: fired, never touched
		if ( vecMin.x <= pTrig->pev->absmax.x && vecMax.x >= pTrig->pev->absmin.x && vecMin.y <= pTrig->pev->absmax.y &&
			vecMax.y >= pTrig->pev->absmin.y && vecMin.z <= pTrig->pev->absmax.z && vecMax.z >= pTrig->pev->absmin.z )
			return true;
	}
	return false;
}

bool RcCoop_DoorWaiting( CBaseEntity *pDoor )
{
	return pDoor && RcCoopRules() && (CBaseEntity *)s_hRcDoorWait == pDoor;
}

// the players ready by the waiting door (one who walked off is not) and the players in the game
static void RcCoopDoorCount( CBaseEntity *pDoor, int &nReady, int &nPresent )
{
	Vector vecDoor = ( pDoor->pev->absmin + pDoor->pev->absmax ) * 0.5;
	nReady = nPresent = 0;
	for ( int i = 1; i <= gpGlobals->maxClients && i <= 32; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( !RcCoopPresent( pEnt ) )
		{
			s_bRcDoorReady[i] = false;
			continue;
		}
		nPresent++;
		if ( s_bRcDoorReady[i] && ( pEnt->pev->origin - vecDoor ).Length() > 192 )
			s_bRcDoorReady[i] = false;
		if ( s_bRcDoorReady[i] )
			nReady++;
	}
}

static void RcCoopDoorGo( CBaseEntity *pDoor, bool bEveryone )
{
	s_hRcDoorWait = NULL;
	s_flRcDoorGo = gpGlobals->time + 10;
	g_engfuncs.pfnServerPrint( bEveryone ? "ReCoF co-op: everyone is at the door, through it together\n" : "ReCoF co-op: the door's wait is over, through it\n" );
	for ( int i = 1; i <= gpGlobals->maxClients && i <= 32; i++ )
	{
		if ( !s_bRcDoorReady[i] )
			continue;
		s_bRcDoorReady[i] = false;
		RcDoor_Pass( pDoor, UTIL_PlayerByIndex( i ) );
	}
}

bool RcCoop_DoorWait( CBaseEntity *pDoor, CBaseEntity *pActivator )
{
	if ( !RcCoopRules() || rc_coop_exit_wait.value <= 0 || !pDoor || !pActivator || !pActivator->IsPlayer() )
		return false;
	int a = ENTINDEX( pActivator->edict() );
	if ( a < 1 || a > 32 || !RcCoopDoorToExit( pDoor ) )
		return false;
	if ( (CBaseEntity *)s_hRcDoorWait != pDoor )
	{
		// a new wait (one at another door ends: its players can come here)
		memset( s_bRcDoorReady, 0, sizeof( s_bRcDoorReady ) );
		s_hRcDoorWait = pDoor;
		s_flRcDoorWaitStart = gpGlobals->time;
	}
	s_bRcDoorReady[a] = true;
	s_flRcDoorWaitMsg = 0;
	int nReady, nPresent;
	RcCoopDoorCount( pDoor, nReady, nPresent );
	if ( nReady >= nPresent )
		RcCoopDoorGo( pDoor, true );
	else
		g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %s waits at the door to the exit, %d of %d players\n", STRING( pActivator->pev->netname ), nReady, nPresent ) );
	return true;
}

// CHalfLifeStoryCoop::Think: the door's wait goes once everyone is by it, the host's rc_coop_go or the wait is over
static void RcCoopDoorWaitFrame( void )
{
	CBaseEntity *pDoor = s_hRcDoorWait;
	if ( !pDoor )
		return;
	int nReady, nPresent;
	RcCoopDoorCount( pDoor, nReady, nPresent );
	float flWait = rc_coop_exit_wait.value;
	float flLeft = s_flRcDoorWaitStart + flWait - gpGlobals->time;
	if ( !nReady || flWait <= 0 || flLeft > flWait + 1 )
	{
		s_hRcDoorWait = NULL;	// nobody waits by it any more (or the wait began on another level)
		return;
	}
	if ( nReady >= nPresent || flLeft <= 0 || s_bRcGoNow )
	{
		RcCoopDoorGo( pDoor, nReady >= nPresent );
		return;
	}
	if ( gpGlobals->time >= s_flRcDoorWaitMsg || gpGlobals->time < s_flRcDoorWaitMsg - 2 )
	{
		s_flRcDoorWaitMsg = gpGlobals->time + 1.0;
		hudtextparms_t tp;
		memset( &tp, 0, sizeof( tp ) );
		tp.x = -1;
		tp.y = 0.8;
		tp.r1 = tp.g1 = tp.b1 = 220;
		tp.a1 = 255;
		tp.fadeoutTime = 0.2;
		tp.holdTime = 1.2;
		tp.channel = 4;
		RcCoopHudMessage( tp, UTIL_VarArgs( "Waiting at the door: %d of %d players (%d s)", nReady, nPresent, (int)( flLeft + 0.99 ) ) );
	}
}

// a story co-op game (the effects that reach only the first player in single player reach everyone)
bool RcCoop_Story( void )
{
	return RcCoopRules() != NULL;
}

// G7 (B387): "the player" of story code written for one player is, in story co-op, the one who set it off (its
// activator, when that is a player in the game); otherwise, and outside story co-op, the first player as before
CBaseEntity *RcCoop_User( CBaseEntity *pActivator )
{
	if ( RcCoopRules() && pActivator && pActivator->IsPlayer() && pActivator->pev->fuser4 == 0 )
		return pActivator;
	return UTIL_FirstPlayer_rc();
}

// G7 (B389): the living player in the game nearest to pFrom; with bVisible the nearest one pFrom can see, when it
// sees any. Outside story co-op the first player as before
CBaseEntity *RcCoop_NearestPlayer( CBaseEntity *pFrom, bool bVisible )
{
	if ( !RcCoopRules() || !pFrom )
		return UTIL_FirstPlayer_rc();
	CBaseEntity *pBest = NULL, *pBestSeen = NULL;
	float flBest = 1e9, flBestSeen = 1e9;
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( !RcCoopPresent( pEnt ) )
			continue;
		float fl = ( pEnt->pev->origin - pFrom->pev->origin ).Length();
		if ( fl < flBest )
		{
			flBest = fl;
			pBest = pEnt;
		}
		if ( bVisible && fl < flBestSeen && pFrom->FVisible( pEnt ) )
		{
			flBestSeen = fl;
			pBestSeen = pEnt;
		}
	}
	if ( pBestSeen )
		return pBestSeen;
	return pBest ? pBest : UTIL_FirstPlayer_rc();
}

bool RcPoliceMP( void )
{
	return g_pGameRules && g_pGameRules->IsMultiplayer() && !RcCoopRules();
}

// B414: the players' model: "simon" (client.dll rc_simonmodel.cpp makes models/player/simon/simon.mdl from the game's
// own files) in the main campaign, the map's or Cry of Fear's police officers (police1 to police4) in the others
extern int gmsgMPModel;

void CHalfLifeStoryCoop::ClientUserInfoChanged( CBasePlayer *pPlayer, char *infobuffer )
{
	if ( !rc_coop_simon.value )
	{
		CHalfLifeMultiplay::ClientUserInfoChanged( pPlayer, infobuffer );
		return;
	}
	if ( !FStrEq( g_engfuncs.pfnInfoKeyValue( infobuffer, "model" ), "simon" ) )
	{
		g_engfuncs.pfnSetClientKeyValue( pPlayer->entindex(), g_engfuncs.pfnGetInfoKeyBuffer( pPlayer->edict() ), "model", "simon" );
		MESSAGE_BEGIN( MSG_ONE, gmsgMPModel, NULL, pPlayer->pev );
		MESSAGE_END();
	}
}

// B411: a police officer's voice and arms: Cry of Fear's own multiplayer, and story co-op outside the main campaign
bool RcCoopPolice( void )
{
	return RcPoliceMP() || ( RcCoopRules() && rc_coop_simon.value == 0 );
}

// a spot near vecBase where a standing player's hull fits, rings of 8 at 36 to 96 units, then up a step; vecBase
// itself when there is none (B403 and B419 then part the two). B463: a spot RcCoopSpotFree takes (floor, line of
// sight, no trigger) first; retail-like hull-only spots put gathered teammates behind a wall or over a drop
static Vector RcCoopAnyFreeSpot( const Vector &vecBase, edict_t *pIgnore )
{
	static const float s_flRing[4] = { 36, 52, 72, 96 };
	for ( int r = 0; r < 4; r++ )
	{
		for ( int a = 0; a < 8; a++ )
		{
			float yaw = a * 45 * ( M_PI / 180.0 );
			Vector vecTry = vecBase + Vector( cos( yaw ) * s_flRing[r], sin( yaw ) * s_flRing[r], 0 );
			if ( RcCoopSpotFree( vecBase, vecTry, pIgnore ) )
				return vecTry;
		}
	}
	for ( int up = 0; up < 2; up++ )
	{
		for ( int r = 0; r < 4; r++ )
		{
			for ( int a = 0; a < 8; a++ )
			{
				float yaw = a * 45 * ( M_PI / 180.0 );
				Vector vecTry = vecBase + Vector( cos( yaw ) * s_flRing[r], sin( yaw ) * s_flRing[r], up * 18 );
				TraceResult tr;
				UTIL_TraceHull( vecTry, vecTry, dont_ignore_monsters, human_hull, pIgnore, &tr );
				if ( tr.fStartSolid || tr.fAllSolid || RcCoopSpotInTrigger( vecTry ) )
					continue;
				UTIL_TraceLine( vecBase, vecTry, ignore_monsters, pIgnore, &tr );
				if ( tr.flFraction >= 1.0 )
					return vecTry;	// B463: in sight, even with no floor (a step down)
			}
		}
	}
	return vecBase;
}

// B483: story co-op's per-map rules (the research's map census, I:/COFPP/research_notes): an entity of one map, matched
// by its brush model, name or target, and what happens when it goes off (a trigger fires, a wall goes up)
enum { RC_MATCH_MODEL, RC_MATCH_NAME, RC_MATCH_TARGET, RC_MATCH_TARGET_PREFIX };
enum { RC_RULE_GATHER, RC_RULE_GATHER_WATCHERS };
static const struct RcMapRule_s
{
	const char *pszMap;
	int iMatch;
	const char *pszValue;
	int iRule;
	const char *pszWhy;
} s_RcMapRules[] =
{
	// the carriage falls 0.5 s after *109 (destroyall) and its hurt kills whoever is still inside (UNIk0z 3f05917
	// and ddb5c2d, charliee1w d690a5d2)
	{ "c_bridge", RC_MATCH_MODEL, "*109", RC_RULE_GATHER, "out of the falling carriage" },
	// the trigger past fudoor1 fires multisawyou, which locks fudoor1 behind the first player (lofu1; cof-coop)
	{ "c_hamn", RC_MATCH_TARGET, "multisawyou", RC_RULE_GATHER, "through the door before it locks" },
	// walls that go up between the players (cof-coop's invisible walls)
	{ "c_down", RC_MATCH_NAME, "spawnm2s", RC_RULE_GATHER, "before the wall goes up" },
	{ "c_fsewer1", RC_MATCH_NAME, "blockstairs", RC_RULE_GATHER, "before the stairs close" },
	// B490: a correct photo at a camera_spot_trigger (multispot2 to 11, multispott1) moves the dream on; the wrong
	// ones (multiwrong, killwrong) do not (CoF_Camera.cpp; cof-coop gathered the far partner)
	{ "c_nightmare", RC_MATCH_TARGET_PREFIX, "multispot", RC_RULE_GATHER, "to the photographer" },
	// B491: the elevator (trigger_cofteleport *127, during the hisskam cameras) takes the one who touches it to the
	// next floor; the others, watching, stayed (cof-coop's elevator follow)
	{ "c_apartment1", RC_MATCH_MODEL, "*127", RC_RULE_GATHER_WATCHERS, "up with the elevator" },
	// B502: c_apartmentsick has the same elevator (trigger_cofteleport *9 to hiss1_land1)
	{ "c_apartmentsick", RC_MATCH_MODEL, "*9", RC_RULE_GATHER_WATCHERS, "up with the elevator" },
	// B502: trigger_once *81 puts the fence fenceapp (*82) up for good; it seals where the rusty-key door lands, so a
	// teammate who used that door later could only go back up
	{ "c_sewer3", RC_MATCH_MODEL, "*81", RC_RULE_GATHER, "before the fence goes up" },
	// B502: triggers *30, *38 and *123 put the doorway walls wato3, wato10 and wato11 back up behind the first one past
	// them, after the photo that opened them (B490 gathers at the photo itself)
	{ "c_nightmare", RC_MATCH_MODEL, "*30", RC_RULE_GATHER, "before the doorway closes" },
	{ "c_nightmare", RC_MATCH_MODEL, "*38", RC_RULE_GATHER, "before the doorway closes" },
	{ "c_nightmare", RC_MATCH_MODEL, "*123", RC_RULE_GATHER, "before the doorway closes" },
	// B502: trigger_once *20 starts sob_mm: sob_door slams, sob_block goes up and both change back 27 s later, so the
	// group goes through that scene on one side of the door
	{ "c_asylum1", RC_MATCH_MODEL, "*20", RC_RULE_GATHER, "before the door slams" },
};

void RcCoop_MapEvent( CBaseEntity *pEnt, CBaseEntity *pActivator )
{
	if ( !RcCoopRules() || !pEnt || !pActivator || !pActivator->IsPlayer() )
		return;
	const char *pszMap = STRING( gpGlobals->mapname );
	for ( int r = 0; r < (int)ARRAYSIZE( s_RcMapRules ); r++ )
	{
		const RcMapRule_s &rule = s_RcMapRules[r];
		if ( stricmp( rule.pszMap, pszMap ) )
			continue;
		string_t isz = rule.iMatch == RC_MATCH_MODEL ? pEnt->pev->model : rule.iMatch == RC_MATCH_NAME ? pEnt->pev->targetname : pEnt->pev->target;
		if ( FStringNull( isz ) )
			continue;
		if ( rule.iMatch == RC_MATCH_TARGET_PREFIX ? strnicmp( STRING( isz ), rule.pszValue, strlen( rule.pszValue ) ) : stricmp( STRING( isz ), rule.pszValue ) )
			continue;
		g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: map rule %s %s\n", rule.pszMap, rule.pszValue ) );
		if ( rule.iRule == RC_RULE_GATHER )
			RcCoop_GatherTo( pActivator, rule.pszWhy );
		else if ( rule.iRule == RC_RULE_GATHER_WATCHERS )
			RcCoopGatherTo( pActivator, rule.pszWhy, true );
	}
}

// B505: a func_wall_toggle went up where a teammate stood (the single player map put it where the one player could
// not be: c_subway2st3's trainblock, 167 x 2406 units around its own trigger): a player inside its brush is moved
// to the nearest free spot instead of staying stuck in it. Not the one who set it off: the map put it on him (the
// trainblock holds him on the tracks for the train)
void RcCoop_WallUp( CBaseEntity *pWall, CBaseEntity *pActivator )
{
	if ( !RcCoopRules() || !pWall || FStringNull( pWall->pev->model ) )
		return;
	for ( int i = 1; i <= gpGlobals->maxClients && i <= 32; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( !RcCoopPresent( pEnt ) || !pEnt->IsAlive() || pEnt == pActivator )
			continue;
		if ( pEnt->pev->absmax.x < pWall->pev->absmin.x || pEnt->pev->absmin.x > pWall->pev->absmax.x ||
			pEnt->pev->absmax.y < pWall->pev->absmin.y || pEnt->pev->absmin.y > pWall->pev->absmax.y ||
			pEnt->pev->absmax.z < pWall->pev->absmin.z || pEnt->pev->absmin.z > pWall->pev->absmax.z )
			continue;
		TraceResult tr;
		int hull = ( pEnt->pev->flags & FL_DUCKING ) ? 3 : 1;
		g_engfuncs.pfnTraceModel( pEnt->pev->origin, pEnt->pev->origin, hull, pWall->edict(), &tr );
		if ( !tr.fStartSolid && !tr.fAllSolid )
			continue;
		Vector vecSpot = RcCoopAnyFreeSpot( pEnt->pev->origin, pEnt->edict() );
		if ( vecSpot == pEnt->pev->origin )
			continue;
		UTIL_SetOrigin( pEnt, vecSpot );
		( (CBasePlayer *)pEnt )->m_vPreviousOrigin = vecSpot;
		g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %s was inside %s when it went up, moved to a free spot\n",
			STRING( pEnt->pev->netname ), FStringNull( pWall->pev->targetname ) ? STRING( pWall->pev->model ) : STRING( pWall->pev->targetname ) ) );
	}
}

// B479: a revived player stood up where he lay: in a corner, a vent or against a wall his standing box was stuck in
// the world (Lambda 9347dc7). When his box does not fit there he stands on the nearest free spot (multiplayer)
void RcCoop_ReviveSpot( CBasePlayer *pPlayer )
{
	TraceResult tr;
	bool bDuck = ( pPlayer->pev->flags & FL_DUCKING ) != 0;
	UTIL_TraceHull( pPlayer->pev->origin, pPlayer->pev->origin, ignore_monsters, bDuck ? head_hull : human_hull, pPlayer->edict(), &tr );
	if ( !tr.fStartSolid && !tr.fAllSolid )
		return;
	Vector vecSpot = RcCoopAnyFreeSpot( pPlayer->pev->origin, pPlayer->edict() );
	if ( vecSpot == pPlayer->pev->origin )
		return;
	UTIL_SetOrigin( pPlayer, vecSpot );
	pPlayer->m_vPreviousOrigin = vecSpot;
	g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %s stood up stuck, moved to a free spot\n", STRING( pPlayer->pev->netname ) ) );
}

// B494: a weapon given or picked up goes into the hand as in single player (CHalfLifeRules::FShouldSwitchWeapon):
// the multiplayer rule switched only to a heavier one, so c_start's scene (the phone at 2 s, the knife at 4 s) left
// story co-op players with the phone in hand where Simon holds the knife
BOOL CHalfLifeStoryCoop::FShouldSwitchWeapon( CBasePlayer *pPlayer, CBasePlayerItem *pWeapon )
{
	if ( !pPlayer->m_pActiveItem )
		return TRUE;
	if ( !pPlayer->m_pActiveItem->CanHolster() )
		return FALSE;
	if ( pWeapon->iFlags() & ITEM_FLAG_NOAUTOSWITCHTO_guess )
		return FALSE;
	return TRUE;
}

BOOL CHalfLifeStoryCoop::IsDeathmatch( void )
{
	return FALSE;
}

BOOL CHalfLifeStoryCoop::IsCoOp( void )
{
	return TRUE;
}

int CHalfLifeStoryCoop::PlayerRelationship( CBaseEntity *pPlayer, CBaseEntity *pTarget )
{
	return GR_TEAMMATE;
}

BOOL CHalfLifeStoryCoop::FPlayerCanTakeDamage( CBasePlayer *pPlayer, CBaseEntity *pAttacker )
{
	// no friendly fire between players
	if ( pAttacker && pAttacker != pPlayer && pAttacker->IsPlayer() )
		return FALSE;

	return CHalfLifeMultiplay::FPlayerCanTakeDamage( pPlayer, pAttacker );
}

edict_t *CHalfLifeStoryCoop::GetPlayerSpawnSpot( CBasePlayer *pPlayer )
{
	// B463: a respawned or joining player arms the exits only once he has stood outside them all (G2), as an arrival
	int iRcSlot = ENTINDEX( pPlayer->edict() );
	if ( iRcSlot >= 1 && iRcSlot <= 32 )
		m_bArmed[iRcSlot] = false;

	CBasePlayer *pAnchor = RcCoopAnchor( pPlayer );
	edict_t *pStart = FIND_ENTITY_BY_CLASSNAME( NULL, "info_player_start" );

	if ( !pAnchor )
	{
		// the first player: the start single player uses
		if ( FNullEnt( pStart ) )
			return CHalfLifeMultiplay::GetPlayerSpawnSpot( pPlayer );

		pPlayer->pev->origin = pStart->v.origin + Vector( 0, 0, 1 );
		pPlayer->pev->v_angle = g_vecZero;
		pPlayer->pev->velocity = g_vecZero;
		pPlayer->pev->angles = pStart->v.angles;
		pPlayer->pev->punchangle = g_vecZero;
		pPlayer->pev->fixangle = TRUE;
		pPlayer->m_vPreviousOrigin = pPlayer->pev->origin;
		if ( pStart->v.target )
			FireTargets( STRING( pStart->v.target ), pPlayer, pPlayer, USE_TOGGLE, 0 );
		return pStart;
	}

	// next to a teammate: rings of 8 spots at 48, 80 and 112 units, the first that is free
	Vector vecBase = pAnchor->pev->origin;
	Vector vecSpot = vecBase;
	bool bFound = false;
	static const float s_flRing[3] = { 48, 80, 112 };
	for ( int r = 0; r < 3 && !bFound; r++ )
	{
		for ( int a = 0; a < 8 && !bFound; a++ )
		{
			float yaw = ( pAnchor->pev->angles.y + 180 + a * 45 ) * ( M_PI / 180.0 );
			Vector vecTry = vecBase + Vector( cos( yaw ) * s_flRing[r], sin( yaw ) * s_flRing[r], 0 );
			if ( RcCoopSpotFree( vecBase, vecTry, pPlayer->edict() ) )
			{
				vecSpot = vecTry;
				bFound = true;
			}
		}
	}

	pPlayer->pev->origin = vecSpot + Vector( 0, 0, 1 );
	pPlayer->pev->v_angle = g_vecZero;
	pPlayer->pev->velocity = g_vecZero;
	pPlayer->pev->angles = pAnchor->pev->angles;
	pPlayer->pev->angles.x = 0;
	pPlayer->pev->angles.z = 0;
	pPlayer->pev->punchangle = g_vecZero;
	pPlayer->pev->fixangle = TRUE;
	pPlayer->m_vPreviousOrigin = pPlayer->pev->origin;
	g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %s comes in next to %s%s\n", STRING( pPlayer->pev->netname ), STRING( pAnchor->pev->netname ), bFound ? "" : " (no free spot, on him)" ));

	return pAnchor->edict();
}

// B472: the level and its scenes ran while carried guests still loaded: arrival triggers, scene starts and messages
// were missed, and the host could walk off before anyone else was in (cof-coop 3f05917 and 08f1d69, charliee1w
// 2a44d5a6). Once per level, until every player carried in (or saved, after a load) has sent a command, at most 20 s:
// entity thinks wait (players and moving brushes go on), players touch no trigger and those already in are frozen
void CHalfLifeStoryCoop::RcCoopHold( void )
{
	if ( m_bHoldDone )
		return;
	if ( m_flHoldStart < 0 || m_flHoldStart > gpGlobals->time )
		m_flHoldStart = gpGlobals->time;

	int nWait = 0, nIn = 0;
	for ( int i = 1; i <= gpGlobals->maxClients && i <= 32; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( !pEnt || !pEnt->IsPlayer() || pEnt->pev->fuser4 != 0 )
			continue;
		if ( m_flSeen[i] == 0 )
			nWait++;
		else
			nIn++;
	}

	// B548: a game started from the lobby also waits for the lobby's players who have no slot yet (still connecting
	// through Steam), so everyone starts together; 60 s at most for them
	int nExpect = (int)rc_coop_expect.value;
	if ( nExpect > nIn + nWait )
		nWait = nExpect - nIn;
	int iMax = nExpect > 1 ? 60 : 20;

	bool bHold = nWait > 0 && gpGlobals->time - m_flHoldStart <= iMax;
	if ( bHold && !g_bRcHolding && nIn + nWait > 1 )
	{
		g_bRcHolding = true;
		g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: the level waits for %d player(s) still loading (%d s at most)\n", nWait, iMax ) );
	}
	if ( !g_bRcHolding )
	{
		if ( !bHold )
		{
			m_bHoldDone = true;	// nobody to wait for (or a lone player): no hold on this level
			if ( nExpect )
				CVAR_SET_FLOAT( "rc_coop_expect", 0 );
		}
		return;
	}

	if ( bHold )
	{
		for ( int i = 1; i <= gpGlobals->maxClients && i <= 32; i++ )
		{
			CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
			if ( !pEnt || !pEnt->IsPlayer() || m_bHeld[i] || m_flSeen[i] == 0 || !RcCoopPresent( pEnt ) )
				continue;
			if ( pEnt->pev->flags & FL_FROZEN )
				continue;	// held by something else: not ours to let go
			m_bHeld[i] = true;
			pEnt->pev->flags |= FL_FROZEN;
			pEnt->pev->velocity = g_vecZero;
		}
		// B495: a centred line every second, over a scene's black screen too (the message strip is hidden there)
		if ( gpGlobals->time >= m_flHoldMsg || gpGlobals->time < m_flHoldMsg - 2 )
		{
			m_flHoldMsg = gpGlobals->time + 1.0;
			hudtextparms_t tp;
			memset( &tp, 0, sizeof( tp ) );
			tp.x = -1;
			tp.y = 0.8;
			tp.r1 = tp.g1 = tp.b1 = 220;
			tp.a1 = 255;
			tp.fadeoutTime = 0.2;
			tp.holdTime = 1.2;
			tp.channel = 4;
			int nLeft = (int)( m_flHoldStart + iMax - gpGlobals->time + 0.99 );
			RcCoopHudMessage( tp, UTIL_VarArgs( "Waiting for the others to load: %d of %d players in (%d s)", nIn, nIn + nWait, nLeft > 0 ? nLeft : 0 ) );
		}
		return;
	}

	// everyone is in, or 20 s (60 for a lobby's start) are over: the level goes on
	g_bRcHolding = false;
	m_bHoldDone = true;
	if ( nExpect )
		CVAR_SET_FLOAT( "rc_coop_expect", 0 );	// B548: later levels wait only for the players carried in
	for ( int i = 1; i <= gpGlobals->maxClients && i <= 32; i++ )
	{
		if ( !m_bHeld[i] )
			continue;
		m_bHeld[i] = false;
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( !pEnt || !pEnt->IsPlayer() )
			continue;
		pEnt->pev->flags &= ~FL_FROZEN;
	}
	g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: the level goes on (%s)\n", nWait ? "the wait is over" : "everyone is in" ) );
}

// rc_coop_dump (server command): every player's slot, health, place and weapons, for tests
static void RcCoopAutosaveState( void );

static void RcCoopDump( void )
{
	RcCoopAutosaveState();
	g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: map %s, %d slots, rules %s\n", STRING( gpGlobals->mapname ), gpGlobals->maxClients,
		g_pGameRules && g_pGameRules->IsCoOp() && !g_pGameRules->IsDeathmatch() ? "story co-op" : "other" ));
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( !pEnt || !pEnt->IsPlayer() )
			continue;
		CBasePlayer *pPlayer = (CBasePlayer *)pEnt;
		char szItems[512];
		szItems[0] = 0;
		int nItems = 0;
		for ( int s = 0; s < MAX_ITEM_TYPES; s++ )
		{
			for ( CBasePlayerItem *pItem = pPlayer->m_rgpPlayerItems[s]; pItem; pItem = pItem->m_pNext )
			{
				if ( strlen( szItems ) + strlen( STRING( pItem->pev->classname ) ) + 2 < sizeof( szItems ) )
				{
					strcat( szItems, " " );
					strcat( szItems, STRING( pItem->pev->classname ) );
				}
				nItems++;
			}
		}
		char szSlots[256];
		szSlots[0] = 0;
		for ( int s = 1; s <= 6; s++ )
		{
			const char *pszSlot = pPlayer->inventorySlot[s] ? STRING( pPlayer->inventorySlot[s] ) : "";
			const char *pszShort = strrchr( pszSlot, '/' );
			pszShort = pszShort ? pszShort + 1 : pszSlot;
			if ( strlen( szSlots ) + strlen( pszShort ) + 2 < sizeof( szSlots ) )
			{
				strcat( szSlots, " " );
				strcat( szSlots, pszShort[0] ? pszShort : "-" );
			}
		}
		CHalfLifeStoryCoop *pRules = RcCoopRules();
		if ( pRules && i <= 32 )
			g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: player %d last command at %.1f (time %.1f), protected %d, armed %d, movetype %d, solid %d, flags %x, scene %d, at %.0f %.0f %.0f\n", i,
				pRules->m_flSeen[i], gpGlobals->time, pRules->m_bProtected[i], pRules->m_bArmed[i], pPlayer->pev->movetype, pPlayer->pev->solid,
				pPlayer->pev->flags, pPlayer->IsInCutscene(), pPlayer->pev->origin.x, pPlayer->pev->origin.y, pPlayer->pev->origin.z ) );
		g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: player %d use: phone or computer %d, call %d, ladder %d, reviving %d, padlock open %d, next use in %.1f\n", i,
			pPlayer->m_iUsingComputerOrPhone, pPlayer->m_bInPhoneCall ? 1 : 0, pPlayer->IsOnLadder() ? 1 : 0, pPlayer->pPlayerToRevive ? 1 : 0,
			pPlayer->m_pPadlock != NULL ? 1 : 0, pPlayer->m_fUseTime - gpGlobals->time ) );
		g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: player %d slots:%s, padlock %d%d%d%d, keypad %d type %d, render %.0f, hidden hud %x, bars %d, view %d, camera %d\n", i, szSlots,
			pPlayer->m_iPadlockNumber[1], pPlayer->m_iPadlockNumber[2], pPlayer->m_iPadlockNumber[3], pPlayer->m_iPadlockNumber[4], pPlayer->m_iKeypadNumber,
			pPlayer->m_iKeypadType, pPlayer->pev->renderamt, pPlayer->m_iHideHUD, pPlayer->m_DrawBars, pPlayer->m_iViewEntIndex,
			pPlayer->m_hCurrentCamera != NULL ? 1 : 0 ) );
		g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: player %d health %.0f %s at %.0f %.0f %.0f, left %d, %d items:%s, in hand %s\n", i, pPlayer->pev->health,
			pPlayer->IsAlive() ? "alive" : "dead", pPlayer->pev->origin.x, pPlayer->pev->origin.y, pPlayer->pev->origin.z,
			pPlayer->pev->fuser4 != 0, nItems, szItems, pPlayer->m_pActiveItem ? STRING( pPlayer->m_pActiveItem->pev->classname ) : "nothing" ));
		g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: player %d move: fuser3 %g, iuser3 %d, maxspeed %.0f, ducking %d, in duck %d, "
			"view %.0f, on ground %d, phone %d, fall freeze %d, effects %x, door %d hid %d, physinfo %s\n", i, pPlayer->pev->fuser3, pPlayer->pev->iuser3, pPlayer->pev->maxspeed,
			( pPlayer->pev->flags & FL_DUCKING ) != 0, pPlayer->pev->bInDuck, pPlayer->pev->view_ofs.z, ( pPlayer->pev->flags & FL_ONGROUND ) != 0,
			pPlayer->m_bInPhoneCall, pPlayer->m_bShouldFallFreeze, pPlayer->pev->effects, pPlayer->b_GoingThroughADoor, pPlayer->m_bRcDoorHid,
			g_engfuncs.pfnGetPhysicsInfoString( pPlayer->edict() ) ) );
	}

	// G16: the live monsters, their health and the scale they have
	int nMonsters = 0;
	for ( int e = gpGlobals->maxClients + 1; e < gpGlobals->maxEntities; e++ )
	{
		edict_t *pEdict = INDEXENT( e );
		if ( !pEdict || pEdict->free || !( pEdict->v.flags & FL_MONSTER ) || pEdict->v.deadflag != DEAD_NO )
			continue;
		CBaseEntity *pEnt = CBaseEntity::Instance( pEdict );
		CBaseMonster *pMonster = pEnt ? pEnt->MyMonsterPointer() : NULL;
		if ( !pMonster )
			continue;
		if ( nMonsters++ < 8 )
			g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: monster %d %s health %.0f of %.0f, scale %.2f\n", e, STRING( pEdict->v.classname ),
				pEdict->v.health, pEdict->v.max_health, pMonster->m_flRcCoopScale ) );
	}
	g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %d live monsters, group scale %.2f\n", nMonsters, RcCoop_MonsterHealthScale() ) );

	// B386: the padlocks, open or not, and who has one open
	CBaseEntity *pPad = NULL;
	while ( ( pPad = UTIL_FindEntityByClassname( pPad, "item_padlock" ) ) != NULL )
		g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: padlock %s open %d, used by player %d\n", STRING( pPad->pev->targetname ), pPad->pev->body,
			pPad->pev->euser1 ? ENTINDEX( pPad->pev->euser1 ) : 0 ) );
}

static void RcCoopAutosaveState( void )
{
	CHalfLifeStoryCoop *pRules = RcCoopRules();
	if ( pRules )
		g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: autosaved %d, level start %.1f, time %.1f\n",
			pRules->m_bAutosaved, pRules->m_flLevelStart, gpGlobals->time ) );
}

// G6: the host skips the scene he is in for the group (rc_cheats.cpp RcCutsceneSkip, sent by his client for Enter in
// game). The game time of a scene is the world's, so it hurries for everyone: hw.dll runs extra 0.1 s server steps
// while rc_coop_skip is 1, about two seconds of game time a frame as single player's skip, while realtime and the
// connections go on as normal. A scene Enter may skip blacks out the screens of the players in it as single player's
// skip does (SkipScene: their clients set host_framerate 2, which the engine ignores in multiplayer, and the host's
// game drops the sounds the scene toggles, as single player's); one the map keeps from being skipped (sv_cheats 1)
// is shown fast. A guest's skip is refused: it would hurry the host's world.
extern int gmsgSkipScene;
void RcCoop_SkipScene( bool bSkippable )
{
	if ( !RcCoopRules() || CVAR_GET_FLOAT( "rc_coop_skip" ) != 0 )
		return;
	CVAR_SET_FLOAT( "rc_coop_skip", 1 );
	if ( bSkippable )
	{
		MESSAGE_BEGIN( MSG_ALL, gmsgSkipScene );
		MESSAGE_END();
	}
	g_engfuncs.pfnServerPrint( "ReCoF co-op: the group skips the scene\n" );
}

// G6 (B390): a guest pressed Enter in a scene (rc_cheats.cpp RcCutsceneSkip): his vote; once every player in a scene
// has voted, the group skips it, and until then the others see who asked
void RcCoop_SkipVote( CBasePlayer *pPlayer, bool bSkippable )
{
	if ( !RcCoopRules() || CVAR_GET_FLOAT( "rc_coop_skip" ) != 0 )
		return;
	int me = ENTINDEX( pPlayer->edict() );
	if ( me < 1 || me > 32 )
		return;
	bool bNew = !s_bRcSkipVote[me];
	s_bRcSkipVote[me] = true;

	int nWatch = 0, nVotes = 0;
	for ( int i = 1; i <= gpGlobals->maxClients && i <= 32; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( !pEnt || !pEnt->IsPlayer() || pEnt->pev->fuser4 != 0 || !( (CBasePlayer *)pEnt )->IsInCutscene() )
			continue;
		nWatch++;
		if ( s_bRcSkipVote[i] )
			nVotes++;
	}
	if ( nVotes >= nWatch )
	{
		RcCoop_SkipScene( bSkippable );
		return;
	}
	if ( bNew )
	{
		UTIL_ClientPrintAll( HUD_PRINTNOTIFY, UTIL_VarArgs( "%s wants to skip the scene (%d of %d): Enter to agree\n", STRING( pPlayer->pev->netname ), nVotes, nWatch ) );
		g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op: %s votes to skip the scene, %d of %d\n", STRING( pPlayer->pev->netname ), nVotes, nWatch ) );
	}
}

// B392, B396: the host's co-op settings. The presets are the game's own difficulties: each sets the difficulty cvar
// (1 to 4) and the co-op rules that go with it. The order of the values in a save: exit wait, respawn time,
// autosave, monster health, skip vote, then the difficulty
static cvar_t *s_pRcSet[] = { &rc_coop_exit_wait, &rc_coop_respawn_time, &rc_coop_autosave, &rc_coop_monster_health, &rc_coop_skip_vote };
static const char *s_pszRcPreset[4] = { "easy", "normal", "hard", "nightmare" };
static const char *s_pszRcPresetName[4] = { "Easy", "Normal", "Hard", "Nightmare" };
static const float s_flRcPreset[4][5] = {
	{ 60, 5, 1, 0, 1 },	// easy: quick respawn, monsters as for one player
	{ 60, 10, 1, 25, 1 },	// normal: the defaults
	{ 60, 20, 1, 40, 1 },	// hard: a longer wait to come back, monsters +40% a player
	{ 60, 0, 0, 50, 1 },	// nightmare: Cry of Fear's own death rule, tape recorders only (a tape each), monsters +50% a player
};

static int RcCoopDifficulty( void )
{
	int iSkill = (int)CVAR_GET_FLOAT( "difficulty" );
	return iSkill < 1 ? 1 : ( iSkill > 4 ? 4 : iSkill );
}

static void RcCoopShowSettings( const char *pszWhy )
{
	int iSkill = RcCoopDifficulty();
	bool bRules = true;
	for ( int k = 0; k < 5; k++ )
		bRules = bRules && s_pRcSet[k]->value == s_flRcPreset[iSkill - 1][k];
	g_engfuncs.pfnServerPrint( UTIL_VarArgs( "ReCoF co-op %s: %s%s; exit wait %g s, respawn %g s%s, autosave %g, monster health +%g%% a player, guests' skip vote %g\n",
		pszWhy, s_pszRcPresetName[iSkill - 1], bRules ? "" : " (rules changed)", rc_coop_exit_wait.value, rc_coop_respawn_time.value,
		rc_coop_respawn_time.value == 0 ? " (Cry of Fear's own rule)" : "", rc_coop_autosave.value, rc_coop_monster_health.value, rc_coop_skip_vote.value ) );
}

// sets the difficulty; the game's skill data follows at once, as a new game's would
static void RcCoopSetDifficulty( int iSkill )
{
	if ( (int)CVAR_GET_FLOAT( "difficulty" ) == iSkill )
		return;
	CVAR_SET_FLOAT( "difficulty", iSkill );
	if ( g_pGameRules )
		g_pGameRules->RefreshSkillData();
}

// rc_coop_preset [easy | normal | hard | nightmare]: sets the difficulty and its co-op rules, or shows them
static void RcCoopPreset( void )
{
	if ( CMD_ARGC() < 2 )
	{
		RcCoopShowSettings( "settings" );
		g_engfuncs.pfnServerPrint( "usage: rc_coop_preset easy | normal | hard | nightmare\n" );
		return;
	}
	for ( int p = 0; p < 4; p++ )
	{
		if ( stricmp( CMD_ARGV( 1 ), s_pszRcPreset[p] ) )
			continue;
		RcCoopSetDifficulty( p + 1 );
		for ( int k = 0; k < 5; k++ )
			CVAR_SET_FLOAT( s_pRcSet[k]->name, s_flRcPreset[p][k] );
		RcCoopShowSettings( "difficulty" );
		return;
	}
	g_engfuncs.pfnServerPrint( "usage: rc_coop_preset easy | normal | hard | nightmare\n" );
}

// B396: nightmare's stamina (a quicker drain, a slower refill: CBasePlayer m_bNightmare, which single player's
// difficulty choice sets) follows the difficulty for every player in story co-op, players who join included
static void RcCoopSyncNightmare( void )
{
	bool bNightmare = RcCoopDifficulty() == 4;
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *pEnt = UTIL_PlayerByIndex( i );
		if ( pEnt && pEnt->IsPlayer() && pEnt->pev->fuser4 == 0 && ( (CBasePlayer *)pEnt )->m_bNightmare != bNightmare )
			( (CBasePlayer *)pEnt )->m_bNightmare = bNightmare;
	}
}

// CBasePlayer::Save: in story co-op every saved player keeps the host's co-op settings
void RcCoop_SettingsSave( CBasePlayer *pPlayer )
{
	if ( !RcCoopRules() )
		return;
	_snprintf( pPlayer->m_szRcCoopSet, sizeof( pPlayer->m_szRcCoopSet ) - 1, "%g %g %g %g %g %d %d", rc_coop_exit_wait.value,
		rc_coop_respawn_time.value, rc_coop_autosave.value, rc_coop_monster_health.value, rc_coop_skip_vote.value, RcCoopDifficulty(),
		rc_coop_simon.value ? 1 : 0 );
	pPlayer->m_szRcCoopSet[sizeof( pPlayer->m_szRcCoopSet ) - 1] = 0;
}

// CBasePlayer::Restore: a story co-op save (or the carry of a level change) brings its settings back; a save from
// before (no settings) and single player keep the host's
void RcCoop_SettingsLoad( CBasePlayer *pPlayer )
{
	if ( !RcCoopRules() || !pPlayer->m_szRcCoopSet[0] )
		return;
	float v[5];
	int iSkill = 0, iSimon = 1;	// B411: a save from before the campaigns is the main campaign's
	if ( sscanf( pPlayer->m_szRcCoopSet, "%f %f %f %f %f %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &iSkill, &iSimon ) < 5 )
		return;
	bool bChanged = false;
	if ( ( iSimon ? 1 : 0 ) != ( rc_coop_simon.value ? 1 : 0 ) )
	{
		CVAR_SET_FLOAT( "rc_coop_simon", iSimon ? 1 : 0 );
		bChanged = true;
	}
	if ( iSkill >= 1 && iSkill <= 4 && iSkill != RcCoopDifficulty() )
	{
		RcCoopSetDifficulty( iSkill );
		bChanged = true;
	}
	for ( int k = 0; k < 5; k++ )
	{
		if ( s_pRcSet[k]->value != v[k] )
		{
			CVAR_SET_FLOAT( s_pRcSet[k]->name, v[k] );
			bChanged = true;
		}
	}
	if ( bChanged )
		RcCoopShowSettings( "settings from the save" );
}

void RcCoop_Init( void )
{
	g_engfuncs.pfnAddServerCommand( "rc_coop_dump", RcCoopDump );
	g_engfuncs.pfnAddServerCommand( "rc_coop_go", RcCoopGo );
	g_engfuncs.pfnAddServerCommand( "rc_coop_preset", RcCoopPreset );	// B392
	CVAR_REGISTER( &rc_coop_exit_wait );
	CVAR_REGISTER( &rc_coop_respawn_time );
	CVAR_REGISTER( &rc_coop_autosave );
	CVAR_REGISTER( &rc_coop_monster_health );
	CVAR_REGISTER( &rc_coop_skip );
	CVAR_REGISTER( &rc_coop_skip_vote );
	CVAR_REGISTER( &rc_coop_simon );
	CVAR_REGISTER( &rc_coop_expect );	// B548
}
