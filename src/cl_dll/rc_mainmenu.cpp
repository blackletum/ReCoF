// ReCoF (B405): the main menu in the modern look. Cry of Fear's picture menu (eleven picture rows under the logo) is
// hidden and a short text list under a wordmark takes its place, in the look of ReCoF's other screens (rc_ui.h):
// New Game, Load Game, Co-op, Custom Campaign, Extras (Unlockables, Bonus Content, Language), Options, Quit.
// B497: the Multiplayer page (Find Servers, Host Server) went into the Co-op lobby. Every entry runs what the picture row ran (CGameMenu::ActionSignal).
// B410: only with rc_menu_text 1; Cry of Fear's picture menu is the default (the user's choice).
#include "hud.h"
#include "cl_util.h"
#include "vgui_TeamFortressViewport.h"
#include "GameMenu_guess.h"
#include "..\game_shared\vgui_checkbutton2.h"
#include "rc_ui.h"

void RcCoopMenu_Open( void );
void RcCoopMenu_Show( bool bShow );

#define RC_CMD_COOP		-1
#define RC_CMD_EXTRAS	-3
#define RC_CMD_BACK		-4

struct RcMainItem_t
{
	const char	*text;
	int			cmd;	// above 0 the picture menu's command (CGameMenu::ActionSignal), below 0 ReCoF's
};

static const RcMainItem_t s_RcMainPage[] =
{
	{ "New Game", 1 }, { "Load Game", 2 }, { "Co-op (Beta)", RC_CMD_COOP }, { "Custom Campaign", 3 },	// B541: (Beta)
	{ "Extras", RC_CMD_EXTRAS }, { "Options", 5 }, { "Quit", 10 },
};
static const RcMainItem_t s_RcExtrasPage[] =
{
	{ "Unlockables", 7 }, { "Bonus Content", 8 }, { "Language", 6 }, { "Back", RC_CMD_BACK },
};

#define RC_MAIN_ITEMS	( sizeof( s_RcMainPage ) / sizeof( s_RcMainPage[0] ) )

static CGameMenu *s_pRcMain;
static CRcUiWordmark *s_pRcWord;
static CRcUiButton *s_pRcItem[RC_MAIN_ITEMS];
static const RcMainItem_t *s_pRcPage = s_RcMainPage;
static int s_nRcPage = RC_MAIN_ITEMS;
static int s_iRcModern = -1;	// the look the menu shows now, -1 before the first
static bool s_bRcItems = true;	// the menu shows its entries (the custom campaign list hides them)
static float s_flRcScale;
static int s_iRcArmed = -1;

static void RcMainMenu_Item( int i );

class CRcMainHandler : public ActionSignal
{
public:
	CRcMainHandler( int i ) { m_i = i; }
	virtual void actionPerformed( Panel *panel ) { RcMainMenu_Item( m_i ); }
private:
	int m_i;
};

// the picture menu's rows, their buttons and its logo
static void RcMainMenu_Pictures( bool bShow )
{
	CGameMenu *m = s_pRcMain;
	Panel *pPanels[] =
	{
		// B497: not Join Server and Host Server, which stay hidden
		m->m_pLogo_guess, m->m_pNewGame_guess, m->m_pLoadGame_guess, m->m_pCustom_guess, m->m_pOptions_guess,
		m->m_pLanguage_guess, m->m_pUnlockables_guess, m->m_pExtras_guess, m->m_pQuit_guess, m->m_pNewGameButton_guess,
		m->m_pLoadGameButton_guess, m->m_pCustomButton_guess, m->m_pOptionsButton_guess, m->m_pLanguageButton_guess,
		m->m_pUnlockablesButton_guess, m->m_pExtrasButton_guess, m->m_pQuitButton_guess,
	};
	for ( int i = 0; i < (int)( sizeof( pPanels ) / sizeof( pPanels[0] ) ); i++ )
	{
		if ( pPanels[i] )
			pPanels[i]->setVisible( bShow );
	}
	RcCoopMenu_Show( bShow );
}

// the wordmark and the page's entries as one block in the middle of the screen; the block's height is the main
// page's, so the wordmark stays put between pages
static void RcMainMenu_Layout( void )
{
	if ( !s_pRcMain )
		return;
	bool bShow = s_iRcModern == 1 && s_bRcItems;
	s_flRcScale = RcUiScale();
	int pitch = RcUiS( 40 ), itemTall = RcUiS( 36 ), itemWide = RcUiS( 460 );
	int wordTall = s_pRcWord->blockTall();
	int gap = RcUiS( 56 );
	int top = ( ScreenHeight - ( wordTall + gap + (int)RC_MAIN_ITEMS * pitch ) ) / 2;
	if ( top < RcUiS( 16 ) )
		top = RcUiS( 16 );
	s_pRcWord->setBounds( 0, top, ScreenWidth, wordTall + RcUiS( 8 ) );
	s_pRcWord->setVisible( bShow );
	for ( int i = 0; i < (int)RC_MAIN_ITEMS; i++ )
	{
		CRcUiButton *p = s_pRcItem[i];
		if ( i < s_nRcPage )
		{
			p->setRcText( s_pRcPage[i].text );
			p->setBounds( ( ScreenWidth - itemWide ) / 2, top + wordTall + gap + i * pitch, itemWide, itemTall );
			p->setVisible( bShow );
		}
		else
			p->setVisible( false );
	}
}

static void RcMainMenu_SetPage( const RcMainItem_t *pPage, int n )
{
	s_pRcPage = pPage;
	s_nRcPage = n;
	s_iRcArmed = -1;
	RcMainMenu_Layout();
}

static void RcMainMenu_Item( int i )
{
	if ( !s_pRcMain || i < 0 || i >= s_nRcPage )
		return;
	int cmd = s_pRcPage[i].cmd;
	switch ( cmd )
	{
	case RC_CMD_COOP:
		RcCoopMenu_Open();
		break;
	case RC_CMD_EXTRAS:
		gEngfuncs.pfnPlaySoundByName( "ui/3d_click.wav", 0.5 );
		RcMainMenu_SetPage( s_RcExtrasPage, sizeof( s_RcExtrasPage ) / sizeof( s_RcExtrasPage[0] ) );
		break;
	case RC_CMD_BACK:
		gEngfuncs.pfnPlaySoundByName( "ui/3d_click.wav", 0.5 );
		RcMainMenu_SetPage( s_RcMainPage, RC_MAIN_ITEMS );
		break;
	default:
		// a screen of its own: the main page is there when the player comes back
		RcMainMenu_SetPage( s_RcMainPage, RC_MAIN_ITEMS );
		s_pRcMain->ActionSignal( cmd );
		break;
	}
}

// CGameMenu::CGameMenu, at its end
void RcMainMenu_Create( CGameMenu *pMenu )
{
	s_pRcMain = pMenu;
	s_pRcWord = new CRcUiWordmark( 0, 0, ScreenWidth, 10 );
	s_pRcWord->setParent( pMenu );
	s_pRcWord->setVisible( false );
	for ( int i = 0; i < (int)RC_MAIN_ITEMS; i++ )
	{
		s_pRcItem[i] = new CRcUiButton( "", 0, 0, 10, 10, CRcUiButton::STYLE_ITEM, RCF_ITEM );
		s_pRcItem[i]->setRcCentred( true );
		s_pRcItem[i]->setParent( pMenu );
		s_pRcItem[i]->addActionSignal( new CRcMainHandler( i ) );
		s_pRcItem[i]->setVisible( false );
	}
}

// the menu has just shown (bItems) or hidden (the custom campaign list) its entries: in the modern look the
// pictures stay hidden and the text list follows
void RcMainMenu_Apply( bool bItems )
{
	if ( !s_pRcMain )
		return;
	s_bRcItems = bItems;
	int iModern = RcUiModern() ? 1 : 0;
	if ( iModern )
		RcMainMenu_Pictures( false );
	else if ( s_iRcModern == 1 && bItems )
		RcMainMenu_Pictures( true );	// rc_menu_text was just cleared
	s_iRcModern = iModern;
	if ( bItems )
	{
		s_pRcPage = s_RcMainPage;
		s_nRcPage = RC_MAIN_ITEMS;
	}
	RcMainMenu_Layout();
}

// CGameMenu::solve, each frame: a changed look or scale, and the rollover sound
void RcMainMenu_Solve( void )
{
	if ( !s_pRcMain )
		return;
	int iModern = RcUiModern() ? 1 : 0;
	if ( iModern != s_iRcModern )
		RcMainMenu_Apply( s_bRcItems );
	else if ( RcUiScale() != s_flRcScale )
		RcMainMenu_Layout();
	if ( s_iRcModern != 1 )
		return;
	int iArmed = -1;
	for ( int i = 0; i < s_nRcPage; i++ )
	{
		if ( s_pRcItem[i]->isVisible() && s_pRcItem[i]->isArmed() )
			iArmed = i;
	}
	if ( iArmed >= 0 && iArmed != s_iRcArmed )
		gEngfuncs.pfnPlaySoundByName( "ui/3d_rollover.wav", 0.5 );
	s_iRcArmed = iArmed;
}

// the modern look hides the picture menu's Story Co-op entry too (rc_coopmenu.cpp)
bool RcMainMenu_Modern( void )
{
	return s_iRcModern == 1;
}

//-----------------------------------------------------------------------------
// B409: the New Game screen (CDifficulty, a 640 x 480 picture with four difficulty buttons, Back and a developer
// commentary check) in the same look: a panel with the four difficulties as rows (Nightmare greyed until it is
// unlocked), the commentary as a row that turns on and off (once unlocked), and Back. Each row runs what its picture
// button ran (CDifficulty::ActionSignal 1 to 5), so the custom campaigns and the c_difficulty_settings map work as
// before. Only with rc_menu_text 1 (B410); the picture is the default.
//-----------------------------------------------------------------------------
#include "Difficulty_guess.h"

static CDifficulty *s_pRcNew;
static CRcUiFrame *s_pRcNewFrame;
static CRcUiButton *s_pRcNewSkill[4], *s_pRcNewCommentary, *s_pRcNewBack;
static float s_flRcNewScale;

class CRcNewHandler : public ActionSignal
{
public:
	CRcNewHandler( int cmd ) { m_cmd = cmd; }
	virtual void actionPerformed( Panel *panel )
	{
		if ( !s_pRcNew )
			return;
		if ( m_cmd == 4 && !( gHUD.m_iUnlocks_guess & 32 ) )
			return;
		gEngfuncs.pfnPlaySoundByName( "ui/3d_click.wav", 0.5 );
		if ( m_cmd == 6 )
		{
			// the developer commentary
			bool bOn = gEngfuncs.pfnGetCvarFloat( "dev_commentary" ) == 0;
			gEngfuncs.pfnClientCmd( bOn ? "dev_commentary 1\n" : "dev_commentary 0\n" );
			gEngfuncs.Cvar_SetValue( "dev_commentary", bOn ? 1 : 0 );	// shown at once
			s_pRcNewCommentary->setRcSelected( bOn );
			s_pRcNewCommentary->setRcText( bOn ? "Developer commentary: on" : "Developer commentary: off" );
			return;
		}
		s_pRcNew->ActionSignal( m_cmd );
	}
private:
	int m_cmd;
};

static void RcNewGame_Layout( void )
{
	s_flRcNewScale = RcUiScale();
	int W = RcUiS( 440 ), pitch = RcUiS( 44 ), rowTall = RcUiS( 38 );
	int band = s_pRcNewFrame->bandTall(), pad = RcUiS( 24 );
	int comment = s_pRcNewCommentary->isVisible() ? RcUiS( 10 ) + pitch : 0;	// the commentary's row, once unlocked
	int H = band + RcUiS( 18 ) + 4 * pitch + comment + RcUiS( 18 ) + RcUiS( 34 ) + RcUiS( 20 );
	s_pRcNewFrame->setBounds( ( ScreenWidth - W ) / 2, ( ScreenHeight - H ) / 2, W, H );
	int y = band + RcUiS( 18 );
	for ( int i = 0; i < 4; i++ )
		s_pRcNewSkill[i]->setBounds( pad, y + i * pitch, W - 2 * pad, rowTall );
	y += 4 * pitch + RcUiS( 10 );
	s_pRcNewCommentary->setBounds( pad, y, W - 2 * pad, rowTall );
	s_pRcNewBack->setBounds( W - pad - RcUiS( 120 ), H - RcUiS( 20 ) - RcUiS( 34 ), RcUiS( 120 ), RcUiS( 34 ) );
}

// CDifficulty::CDifficulty, at its end
void RcNewGame_Create( CDifficulty *pPanel )
{
	static const char *s_pszSkill[4] = { "Easy", "Normal", "Hard", "Nightmare" };
	s_pRcNew = pPanel;
	s_pRcNewFrame = new CRcUiFrame( "New Game", 0, 0, 10, 10 );
	s_pRcNewFrame->setParent( pPanel );
	for ( int i = 0; i < 4; i++ )
	{
		s_pRcNewSkill[i] = new CRcUiButton( s_pszSkill[i], 0, 0, 10, 10, CRcUiButton::STYLE_ROW, RCF_BODY );
		s_pRcNewSkill[i]->setParent( s_pRcNewFrame );
		s_pRcNewSkill[i]->addActionSignal( new CRcNewHandler( i + 1 ) );
	}
	s_pRcNewCommentary = new CRcUiButton( "", 0, 0, 10, 10, CRcUiButton::STYLE_ROW, RCF_BODY );
	s_pRcNewCommentary->setParent( s_pRcNewFrame );
	s_pRcNewCommentary->addActionSignal( new CRcNewHandler( 6 ) );
	s_pRcNewBack = new CRcUiButton( "Back", 0, 0, 10, 10, CRcUiButton::STYLE_BOX, RCF_BUTTON );
	s_pRcNewBack->setParent( s_pRcNewFrame );
	s_pRcNewBack->addActionSignal( new CRcNewHandler( 5 ) );
	s_pRcNewFrame->setVisible( false );
}

// CDifficulty::setVisible( true ), after it chose its picture: the panel in the modern look, or the picture
void RcNewGame_Show( void )
{
	if ( !s_pRcNew )
		return;
	bool bModern = RcUiModern();
	s_pRcNew->m_pImagePanel_guess->setVisible( !bModern );
	if ( bModern )
		s_pRcNew->m_CommentaryButton_guess.setVisible( false );
	s_pRcNewFrame->setVisible( bModern );
	if ( !bModern )
		return;
	bool bCampaign = gViewPort && gViewPort->m_pGameMenu_guess && gViewPort->m_pGameMenu_guess->m_pszCampaign_guess;
	s_pRcNewFrame->setRcTitle( bCampaign ? "Custom Campaign" : "New Game" );
	s_pRcNewSkill[3]->setRcDisabled( !( gHUD.m_iUnlocks_guess & 32 ) );
	s_pRcNewSkill[3]->setRcText( ( gHUD.m_iUnlocks_guess & 32 ) ? "Nightmare" : "Nightmare (locked)" );
	bool bOn = gEngfuncs.pfnGetCvarFloat( "dev_commentary" ) != 0;
	s_pRcNewCommentary->setRcSelected( bOn );
	s_pRcNewCommentary->setRcText( bOn ? "Developer commentary: on" : "Developer commentary: off" );
	s_pRcNewCommentary->setVisible( ( gHUD.m_iUnlocks_guess & 0x400000 ) != 0 );
	RcNewGame_Layout();
}
