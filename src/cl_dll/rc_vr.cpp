// ReCoF VR: Cry of Fear in a headset through OpenVR (SteamVR), only with the -vr launch option (rc_vr.h)
//
// Spaces: OpenVR's tracking space is in metres, +X right, +Y up, -Z forward. The game's is in units, +X forward,
// +Y left, +Z up. RcVrToGame turns a tracking direction into a game one; the turn yaw (snap turns, the server's
// own turns of the player) and the world scale (rc_vr_scale units a metre) are applied on top. The head's
// offset from where it was at the last recentre moves the eyes away from the game's eye position (clamped), so
// the player can lean and look around a corner while the game keeps the body where the movement put it.
//
// A frame: CL_CreateMove reads the controllers (movement, snap turn, buttons, the aim the server shoots along);
// V_CalcRefdef runs the game's view once, then asks the engine for four passes (left eye, its head shield
// pass, right eye, its head shield pass), each into the eye's framebuffer with the wrapper (opengl32.dll)
// setting the eye's projection and viewport; HUD_Redraw puts the window back for the 2D; at the swap the 2D is
// copied into a texture and drawn on a screen in front of the player in both eyes, with the laser pointer, both
// eyes go to SteamVR, the left eye and the 2D go to the window, and WaitGetPoses waits for the next frame's
// poses.

// OpenVR first: its standard headers must come before the SDK's macros
#include "../external/openvr/headers/openvr.h"

#include "hud.h"
#include "cl_util.h"
#include "cvardef.h"
#include "const.h"
#include "pm_defs.h"
#include "pmtrace.h"
#include "ref_params.h"
#include "usercmd.h"
#include "com_model.h"
#include "studio.h"
#include "entity_types.h"
#include "r_studioint.h"
#include "screenfade.h"
#include "shake.h"
#include "rc_client.h"
#include "rc_glsl.h"
#include "log.h"
#include "rc_vr.h"

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>


// ------------------------------------------------------------------------------------------------------------
// GL pieces the client does not load (glmanager.cpp)

#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER					0x8D40
#endif
#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER					0x8D41
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0			0x8CE0
#endif
#ifndef GL_DEPTH_ATTACHMENT
#define GL_DEPTH_ATTACHMENT				0x8D00
#endif
#ifndef GL_STENCIL_ATTACHMENT
#define GL_STENCIL_ATTACHMENT			0x8D20
#endif
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8				0x88F0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE			0x8CD5
#endif
#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING			0x8CA6
#endif
#ifndef GL_CURRENT_PROGRAM
#define GL_CURRENT_PROGRAM				0x8B8D
#endif
#ifndef GL_ACTIVE_TEXTURE
#define GL_ACTIVE_TEXTURE				0x84E0
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0						0x84C0
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE				0x812F
#endif

typedef void	( APIENTRY *RcGlGenFramebuffers_t )( GLsizei, GLuint * );
typedef void	( APIENTRY *RcGlBindFramebuffer_t )( GLenum, GLuint );
typedef void	( APIENTRY *RcGlFramebufferTexture2D_t )( GLenum, GLenum, GLenum, GLuint, GLint );
typedef void	( APIENTRY *RcGlGenRenderbuffers_t )( GLsizei, GLuint * );
typedef void	( APIENTRY *RcGlBindRenderbuffer_t )( GLenum, GLuint );
typedef void	( APIENTRY *RcGlRenderbufferStorage_t )( GLenum, GLenum, GLsizei, GLsizei );
typedef void	( APIENTRY *RcGlFramebufferRenderbuffer_t )( GLenum, GLenum, GLenum, GLuint );
typedef GLenum	( APIENTRY *RcGlCheckFramebufferStatus_t )( GLenum );
typedef void	( APIENTRY *RcGlGenTextures_t )( GLsizei, GLuint * );
typedef void	( APIENTRY *RcGlLineWidth_t )( GLfloat );
typedef void	( APIENTRY *RcGlPointSize_t )( GLfloat );
typedef void	( APIENTRY *RcGlActiveTexture_t )( GLenum );
typedef BOOL	( WINAPI *RcWglSwapInterval_t )( int );

static struct
{
	RcGlGenFramebuffers_t			GenFramebuffers;
	RcGlBindFramebuffer_t			BindFramebuffer;
	RcGlFramebufferTexture2D_t		FramebufferTexture2D;
	RcGlGenRenderbuffers_t			GenRenderbuffers;
	RcGlBindRenderbuffer_t			BindRenderbuffer;
	RcGlRenderbufferStorage_t		RenderbufferStorage;
	RcGlFramebufferRenderbuffer_t	FramebufferRenderbuffer;
	RcGlCheckFramebufferStatus_t	CheckFramebufferStatus;
	RcGlGenTextures_t				GenTextures;
	RcGlLineWidth_t					LineWidth;
	RcGlPointSize_t					PointSize;
	RcGlActiveTexture_t				ActiveTexture;
} s_gl;

// opengl32.dll (src/opengl32/opengl32.cpp): the eye's projection and viewport, and the swap callback
typedef void ( __stdcall *RcVrSetEye_t )( int active, float l, float r, float b, float t, int eyeW, int eyeH, int winW, int winH );
typedef void ( __stdcall *RcVrSetSwapHook_t )( void ( __stdcall *pfn )( HDC ) );
static RcVrSetEye_t			s_pfnSetEye;

// ------------------------------------------------------------------------------------------------------------
// OpenVR

typedef uint32_t	( *RcVR_InitInternal2_t )( vr::EVRInitError *peError, vr::EVRApplicationType eType, const char *pStartupInfo );
typedef void		( *RcVR_ShutdownInternal_t )( void );
typedef void *		( *RcVR_GetGenericInterface_t )( const char *pchVersion, vr::EVRInitError *peError );
typedef const char *( *RcVR_GetErrorText_t )( vr::EVRInitError eError );

static HMODULE						s_hOpenVR;
static RcVR_ShutdownInternal_t		s_pfnShutdown;
static vr::IVRSystem				*s_pSystem;
static vr::IVRCompositor			*s_pComp;
static bool							s_bActive;

static vr::TrackedDevicePose_t		s_rgPoses[ vr::k_unMaxTrackedDeviceCount ];
static bool							s_bPoses;

static uint32_t		s_iEyeW, s_iEyeH;					// eye framebuffer size
static float		s_rgTan[ 2 ][ 4 ];					// each eye's left, right, bottom, top tangents (GL sense)
static vr::HmdMatrix34_t s_rgEyeToHead[ 2 ];

static GLuint		s_rgFbo[ 2 ], s_rgEyeTex[ 2 ], s_rgDepth[ 2 ];
static GLuint		s_texScreen;						// the window's 2D, copied at the swap
static int			s_iScreenW, s_iScreenH;
static GLuint		s_iProgram;							// the 2D's alpha from its brightness
static int			s_iProgTex, s_iProgOpaque, s_iProgFade;
static int			s_iGLState;							// 0 not made, 1 ready, -1 failed
static HWND			s_hWnd;

// the frame
static int			s_iPass;							// the pass V_CalcRefdef is in (0 to 3)
static bool			s_bShield;							// this frame has head shield passes
static bool			s_bSecondEye;
static bool			s_bEyesDrawn;						// the eyes were drawn this frame (a level is on)
static bool			s_bEyeBound;						// an eye framebuffer is bound

// tracking and the game
static float		s_flTurnYaw;						// game yaw of the tracking space's -Z
static float		s_rgRef[ 3 ];						// tracking position of the head at the last recentre
static bool			s_bRecentre = true;
static float		s_rgHeadGame[ 3 ];					// the head centre in the game this frame
static float		s_flBaseYaw;						// the yaw the tracking space is turned by this frame
static float		s_rgGameEye[ 3 ];					// where the game puts the eye (before the head offset)
static bool			s_bOwnView = true;
static float		s_flCamHmdYaw;						// the head's yaw when a camera began
static bool			s_bWasCamera;
static float		s_flSmoothZ;
static float		s_rgLastSetAngles[ 3 ];
static bool			s_bAnglesSet;

// the hands
struct RcVrHand_t
{
	vr::TrackedDeviceIndex_t	index;
	bool						valid;
	float						pos[ 3 ];				// game position
	float						fwd[ 3 ], right[ 3 ], up[ 3 ];	// game axes after the aim pitch
	float						angles[ 3 ];			// view style angles (pitch down positive)
	float						tpos[ 3 ];				// tracking position
	float						tfwd[ 3 ];				// tracking forward after the aim pitch
	vr::VRControllerState_t		state, old;
};
static RcVrHand_t	s_rgHand[ 2 ];						// 0 left, 1 right
static float		s_rgAimPoint[ 3 ];					// where the right hand points in the game
static bool			s_bAimPoint;

// the 2D screen and the pointer
static float		s_rgScreenPose[ 3 ][ 4 ];			// tracking transform of the screen (menus)
static bool			s_bScreenPlaced;
static bool			s_bPointerOnScreen;
static float		s_flPointerU, s_flPointerV;
static float		s_rgPointerHit[ 3 ];
static bool			s_bCursorMode;

// buttons sent as commands
enum
{
	RCVB_ATTACK, RCVB_ATTACK2, RCVB_USE, RCVB_RELOAD, RCVB_JUMP, RCVB_DUCK, RCVB_SPRINT, RCVB_DODGE, RCVB_INVENTORY,
	RCVB_COUNT
};
static const char *s_rgButtonCmd[ RCVB_COUNT ] = { "attack", "attack2", "use", "reload", "jump", "duck", "sprint", "dodge", "inventory" };
static bool			s_rgButtonDown[ RCVB_COUNT ];
static bool			s_bTurnLatch;
static bool			s_bMouseDown;
static int			s_iQuickSlot;
static float		s_flBothGrips;

static cvar_t *rc_vr_scale, *rc_vr_snapturn, *rc_vr_turnspeed, *rc_vr_aimpitch, *rc_vr_grip_fwd, *rc_vr_grip_right,
	*rc_vr_grip_up, *rc_vr_hidearms, *rc_vr_hud_dist, *rc_vr_hud_width, *rc_vr_menu_dist, *rc_vr_menu_width,
	*rc_vr_movehand, *rc_vr_lean, *rc_vr_renderscale, *rc_vr_mirror, *rc_vr_lefthand_item, *rc_vr_lefthand, *rc_vr_debug;

extern int g_DrawHeadShied;						// view.cpp
extern vec3_t v_origin, v_angles;				// view.cpp
extern int g_iHeadShieldType_guess;				// StudioModelRenderer.cpp
extern cl_entity_t g_HeadShieldEnt;
int RcIN_MouseFree( void );						// inputw32.cpp: the menus or a panel have the mouse

// ------------------------------------------------------------------------------------------------------------
// maths

#define RCVR_PI 3.14159265358979f

static void RcVrToGame( const float *t, float *g )
{
	float x = -t[ 2 ], y = -t[ 0 ], z = t[ 1 ];
	g[ 0 ] = x; g[ 1 ] = y; g[ 2 ] = z;
}

static void RcVrToTracking( const float *g, float *t )
{
	float x = -g[ 1 ], y = g[ 2 ], z = -g[ 0 ];
	t[ 0 ] = x; t[ 1 ] = y; t[ 2 ] = z;
}

static void RcVrRotZ( float *v, float yaw )
{
	float s = sinf( yaw * RCVR_PI / 180 ), c = cosf( yaw * RCVR_PI / 180 );
	float x = v[ 0 ] * c - v[ 1 ] * s, y = v[ 0 ] * s + v[ 1 ] * c;
	v[ 0 ] = x; v[ 1 ] = y;
}

// tracking direction to game direction, with the turn
static void RcVrDirToGame( const float *t, float *g, float yaw )
{
	RcVrToGame( t, g );
	RcVrRotZ( g, yaw );
}

static void RcVrDirToTracking( const float *g, float *t, float yaw )
{
	float v[ 3 ] = { g[ 0 ], g[ 1 ], g[ 2 ] };
	RcVrRotZ( v, -yaw );
	RcVrToTracking( v, t );
}

// view style angles (pitch down positive, as cl_viewangles) from game axes; right points right
static void RcVrAngles( const float *f, const float *r, const float *u, float *angles )
{
	float sp = -f[ 2 ];
	if ( sp > 1 ) sp = 1;
	if ( sp < -1 ) sp = -1;
	angles[ 0 ] = asinf( sp ) * 180 / RCVR_PI;
	angles[ 1 ] = atan2f( f[ 1 ], f[ 0 ] ) * 180 / RCVR_PI;
	angles[ 2 ] = atan2f( -r[ 2 ], u[ 2 ] ) * 180 / RCVR_PI;
}

static void RcVrColumn( const vr::HmdMatrix34_t &m, int c, float *v )
{
	v[ 0 ] = m.m[ 0 ][ c ]; v[ 1 ] = m.m[ 1 ][ c ]; v[ 2 ] = m.m[ 2 ][ c ];
}

// a = b * c for 3x4 rigid transforms
static void RcVrMul34( const vr::HmdMatrix34_t &b, const vr::HmdMatrix34_t &c, vr::HmdMatrix34_t &a )
{
	vr::HmdMatrix34_t o;
	for ( int i = 0; i < 3; i++ )
	{
		for ( int j = 0; j < 4; j++ )
		{
			o.m[ i ][ j ] = b.m[ i ][ 0 ] * c.m[ 0 ][ j ] + b.m[ i ][ 1 ] * c.m[ 1 ][ j ] + b.m[ i ][ 2 ] * c.m[ 2 ][ j ];
			if ( j == 3 )
				o.m[ i ][ j ] += b.m[ i ][ 3 ];
		}
	}
	a = o;
}

// GL column-major view matrix (the inverse of a rigid tracking transform)
static void RcVrViewMatrix( const vr::HmdMatrix34_t &m, float *gl )
{
	// inverse rotation is the transpose; inverse translation is -R^T t
	for ( int c = 0; c < 3; c++ )
	{
		for ( int r = 0; r < 3; r++ )
			gl[ c * 4 + r ] = m.m[ c ][ r ];
		gl[ c * 4 + 3 ] = 0;
	}
	for ( int r = 0; r < 3; r++ )
		gl[ 12 + r ] = -( m.m[ 0 ][ r ] * m.m[ 0 ][ 3 ] + m.m[ 1 ][ r ] * m.m[ 1 ][ 3 ] + m.m[ 2 ][ r ] * m.m[ 2 ][ 3 ] );
	gl[ 15 ] = 1;
}

static float RcVrHmdYaw( void )
{
	// the yaw of the head's forward in the tracking space, game sense (counter-clockwise from -Z)
	const vr::HmdMatrix34_t &m = s_rgPoses[ vr::k_unTrackedDeviceIndex_Hmd ].mDeviceToAbsoluteTracking;
	float f[ 3 ] = { -m.m[ 0 ][ 2 ], -m.m[ 1 ][ 2 ], -m.m[ 2 ][ 2 ] };
	float g[ 3 ];
	RcVrToGame( f, g );
	return atan2f( g[ 1 ], g[ 0 ] ) * 180 / RCVR_PI;
}

static bool RcVrHmdValid( void )
{
	return s_bPoses && s_rgPoses[ vr::k_unTrackedDeviceIndex_Hmd ].bPoseIsValid;
}

// ------------------------------------------------------------------------------------------------------------
// start

static void RcVrRecentre_f( void )
{
	s_bRecentre = true;
}

static void RcVrInfo_f( void )
{
	gEngfuncs.Con_Printf( "ReCoF VR: %s, eye %u x %u, turn %.0f, hands %s %s, eyes drawn %d\n", s_bActive ? "on" : "off", s_iEyeW, s_iEyeH,
		s_flTurnYaw, s_rgHand[ 0 ].valid ? "left" : "-", s_rgHand[ 1 ].valid ? "right" : "-", s_bEyesDrawn ? 1 : 0 );
}

static void __stdcall RcVrSwap( HDC hDC );

bool RcVR_Init( void )
{
	if ( !gEngfuncs.CheckParm( (char *)"-vr", NULL ) && !gEngfuncs.CheckParm( (char *)"-openvr", NULL ) )
		return false;

	rc_vr_scale			= gEngfuncs.pfnRegisterVariable( (char *)"rc_vr_scale", "37.5", FCVAR_ARCHIVE );
	rc_vr_snapturn		= gEngfuncs.pfnRegisterVariable( (char *)"rc_vr_snapturn", "45", FCVAR_ARCHIVE );
	rc_vr_turnspeed		= gEngfuncs.pfnRegisterVariable( (char *)"rc_vr_turnspeed", "180", FCVAR_ARCHIVE );
	rc_vr_aimpitch		= gEngfuncs.pfnRegisterVariable( (char *)"rc_vr_aimpitch", "35", FCVAR_ARCHIVE );
	rc_vr_grip_fwd		= gEngfuncs.pfnRegisterVariable( (char *)"rc_vr_grip_fwd", "-4", FCVAR_ARCHIVE );
	rc_vr_grip_right	= gEngfuncs.pfnRegisterVariable( (char *)"rc_vr_grip_right", "0", FCVAR_ARCHIVE );
	rc_vr_grip_up		= gEngfuncs.pfnRegisterVariable( (char *)"rc_vr_grip_up", "-2", FCVAR_ARCHIVE );
	rc_vr_hidearms		= gEngfuncs.pfnRegisterVariable( (char *)"rc_vr_hidearms", "1", FCVAR_ARCHIVE );
	rc_vr_hud_dist		= gEngfuncs.pfnRegisterVariable( (char *)"rc_vr_hud_dist", "1.6", FCVAR_ARCHIVE );
	rc_vr_hud_width		= gEngfuncs.pfnRegisterVariable( (char *)"rc_vr_hud_width", "1.9", FCVAR_ARCHIVE );
	rc_vr_menu_dist		= gEngfuncs.pfnRegisterVariable( (char *)"rc_vr_menu_dist", "1.8", FCVAR_ARCHIVE );
	rc_vr_menu_width	= gEngfuncs.pfnRegisterVariable( (char *)"rc_vr_menu_width", "2.2", FCVAR_ARCHIVE );
	rc_vr_movehand		= gEngfuncs.pfnRegisterVariable( (char *)"rc_vr_movehand", "0", FCVAR_ARCHIVE );
	rc_vr_lean			= gEngfuncs.pfnRegisterVariable( (char *)"rc_vr_lean", "24", FCVAR_ARCHIVE );
	rc_vr_renderscale	= gEngfuncs.pfnRegisterVariable( (char *)"rc_vr_renderscale", "1", FCVAR_ARCHIVE );
	rc_vr_mirror		= gEngfuncs.pfnRegisterVariable( (char *)"rc_vr_mirror", "1", FCVAR_ARCHIVE );
	rc_vr_lefthand_item	= gEngfuncs.pfnRegisterVariable( (char *)"rc_vr_lefthand_item", "1", FCVAR_ARCHIVE );
	rc_vr_lefthand		= gEngfuncs.pfnRegisterVariable( (char *)"rc_vr_lefthand", "1", FCVAR_ARCHIVE );
	rc_vr_debug			= gEngfuncs.pfnRegisterVariable( (char *)"rc_vr_debug", "0", 0 );
	gEngfuncs.pfnAddCommand( (char *)"rc_vr_recenter", RcVrRecentre_f );
	gEngfuncs.pfnAddCommand( (char *)"rc_vr_info", RcVrInfo_f );

	s_hOpenVR = LoadLibraryA( "openvr_api.dll" );
	if ( !s_hOpenVR )
	{
		gEngfuncs.Con_Printf( "ReCoF VR: openvr_api.dll is missing next to cof.exe (error %lu); VR stays off\n", GetLastError() );
		return false;
	}

	RcVR_InitInternal2_t pfnInit = (RcVR_InitInternal2_t)GetProcAddress( s_hOpenVR, "VR_InitInternal2" );
	RcVR_GetGenericInterface_t pfnGet = (RcVR_GetGenericInterface_t)GetProcAddress( s_hOpenVR, "VR_GetGenericInterface" );
	RcVR_GetErrorText_t pfnText = (RcVR_GetErrorText_t)GetProcAddress( s_hOpenVR, "VR_GetVRInitErrorAsEnglishDescription" );
	s_pfnShutdown = (RcVR_ShutdownInternal_t)GetProcAddress( s_hOpenVR, "VR_ShutdownInternal" );
	if ( !pfnInit || !pfnGet || !s_pfnShutdown )
	{
		gEngfuncs.Con_Printf( "ReCoF VR: openvr_api.dll lacks VR_InitInternal2; VR stays off\n" );
		return false;
	}

	vr::EVRInitError err = vr::VRInitError_None;
	pfnInit( &err, vr::VRApplication_Scene, NULL );
	if ( err != vr::VRInitError_None )
	{
		gEngfuncs.Con_Printf( "ReCoF VR: SteamVR did not start: %s (%d); VR stays off\n", pfnText ? pfnText( err ) : "", (int)err );
		return false;
	}

	s_pSystem = (vr::IVRSystem *)pfnGet( vr::IVRSystem_Version, &err );
	if ( !s_pSystem )
	{
		gEngfuncs.Con_Printf( "ReCoF VR: no %s (%d); VR stays off\n", vr::IVRSystem_Version, (int)err );
		s_pfnShutdown();
		return false;
	}
	s_pComp = (vr::IVRCompositor *)pfnGet( vr::IVRCompositor_Version, &err );
	if ( !s_pComp )
	{
		gEngfuncs.Con_Printf( "ReCoF VR: no %s (%d); VR stays off\n", vr::IVRCompositor_Version, (int)err );
		s_pfnShutdown();
		return false;
	}

	HMODULE hGL = GetModuleHandleA( "opengl32.dll" );
	RcVrSetSwapHook_t pfnHook = hGL ? (RcVrSetSwapHook_t)GetProcAddress( hGL, "RcVrSetSwapHook" ) : NULL;
	s_pfnSetEye = hGL ? (RcVrSetEye_t)GetProcAddress( hGL, "RcVrSetEye" ) : NULL;
	if ( !pfnHook || !s_pfnSetEye )
	{
		gEngfuncs.Con_Printf( "ReCoF VR: this opengl32.dll has no VR exports (an old or a foreign wrapper); VR stays off\n" );
		s_pfnShutdown();
		return false;
	}

	char szSystem[ 128 ] = "", szModel[ 128 ] = "";
	s_pSystem->GetStringTrackedDeviceProperty( vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_TrackingSystemName_String, szSystem, sizeof( szSystem ) );
	s_pSystem->GetStringTrackedDeviceProperty( vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_ModelNumber_String, szModel, sizeof( szModel ) );

	s_pSystem->GetRecommendedRenderTargetSize( &s_iEyeW, &s_iEyeH );
	float flScale = rc_vr_renderscale->value;
	if ( flScale < 0.4f ) flScale = 0.4f;
	if ( flScale > 2.0f ) flScale = 2.0f;
	s_iEyeW = (uint32_t)( s_iEyeW * flScale ) & ~3u;
	s_iEyeH = (uint32_t)( s_iEyeH * flScale ) & ~3u;

	for ( int e = 0; e < 2; e++ )
	{
		float l, r, top, bottom;
		s_pSystem->GetProjectionRaw( e ? vr::Eye_Right : vr::Eye_Left, &l, &r, &top, &bottom );
		// OpenVR's top is the downward tangent and its bottom the upward one (ComposeProjection)
		s_rgTan[ e ][ 0 ] = l;
		s_rgTan[ e ][ 1 ] = r;
		s_rgTan[ e ][ 2 ] = top < bottom ? top : bottom;
		s_rgTan[ e ][ 3 ] = top < bottom ? bottom : top;
		s_rgEyeToHead[ e ] = s_pSystem->GetEyeToHeadTransform( e ? vr::Eye_Right : vr::Eye_Left );
	}

	s_pComp->SetTrackingSpace( vr::TrackingUniverseStanding );
	pfnHook( RcVrSwap );

	// the headset paces the frames: smooth frames and the weapon model's own field of view stand aside in code
	// (rc_smooth.cpp, rc_client.cpp), so the player's flat settings in config.cfg stay as they are

	s_bActive = true;
	gEngfuncs.Con_Printf( "ReCoF VR: on, %s %s, eyes %u x %u, left eye tangents %.2f %.2f %.2f %.2f, IPD %.1f mm\n", szSystem, szModel, s_iEyeW, s_iEyeH,
		s_rgTan[ 0 ][ 0 ], s_rgTan[ 0 ][ 1 ], s_rgTan[ 0 ][ 2 ], s_rgTan[ 0 ][ 3 ],
		( s_rgEyeToHead[ 1 ].m[ 0 ][ 3 ] - s_rgEyeToHead[ 0 ].m[ 0 ][ 3 ] ) * 1000 );
	gEngfuncs.Con_Printf( "ReCoF VR: left stick moves, right stick turns (up jumps, down crouches), right trigger fires, right grip uses, A reloads, B quick slots, "
		"X inventory, Y menu, left trigger secondary, left grip dodges, left stick click sprints; both grips held recentre\n" );

	// the first poses
	s_pComp->WaitGetPoses( s_rgPoses, vr::k_unMaxTrackedDeviceCount, NULL, 0 );
	s_bPoses = true;
	return true;
}

bool RcVR_Active( void )
{
	return s_bActive;
}

bool RcVR_SecondEye( void )
{
	return s_bSecondEye;
}

// ------------------------------------------------------------------------------------------------------------
// GL resources (made on the first frame, with the engine's context current)

static void *RcVrGL( const char *name, const char *ext )
{
	void *p = (void *)gl.wglGetProcAddress( name );
	if ( !p && ext )
		p = (void *)gl.wglGetProcAddress( ext );
	return p;
}

static bool RcVrMakeEye( int e )
{
	s_gl.GenTextures( 1, &s_rgEyeTex[ e ] );
	gl.glBindTexture( GL_TEXTURE_2D, s_rgEyeTex[ e ] );
	gl.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	gl.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	gl.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	gl.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	gl.glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, s_iEyeW, s_iEyeH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );

	s_gl.GenRenderbuffers( 1, &s_rgDepth[ e ] );
	s_gl.BindRenderbuffer( GL_RENDERBUFFER, s_rgDepth[ e ] );
	s_gl.RenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, s_iEyeW, s_iEyeH );

	s_gl.GenFramebuffers( 1, &s_rgFbo[ e ] );
	s_gl.BindFramebuffer( GL_FRAMEBUFFER, s_rgFbo[ e ] );
	s_gl.FramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_rgEyeTex[ e ], 0 );
	s_gl.FramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, s_rgDepth[ e ] );
	s_gl.FramebufferRenderbuffer( GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s_rgDepth[ e ] );
	GLenum status = s_gl.CheckFramebufferStatus( GL_FRAMEBUFFER );

	gl.glViewport( 0, 0, s_iEyeW, s_iEyeH );
	gl.glClearColor( 0, 0, 0, 1 );
	gl.glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
	s_gl.BindFramebuffer( GL_FRAMEBUFFER, 0 );
	s_gl.BindRenderbuffer( GL_RENDERBUFFER, 0 );

	if ( status != GL_FRAMEBUFFER_COMPLETE )
	{
		gEngfuncs.Con_Printf( "ReCoF VR: eye framebuffer %d is incomplete (0x%x)\n", e, status );
		return false;
	}
	return true;
}

static const char *s_pszVrVertex =
	"void main() { gl_Position = ftransform(); gl_TexCoord[0] = gl_MultiTexCoord0; gl_FrontColor = gl_Color; }\n";
static const char *s_pszVrFragment =
	"uniform sampler2D tex; uniform float opaque; uniform float fade;\n"
	"void main() {\n"
	"  vec4 c = texture2D( tex, gl_TexCoord[0].xy );\n"
	"  float a = max( c.r, max( c.g, c.b ) );\n"
	"  vec3 rgb = opaque > 0.5 ? c.rgb : c.rgb / max( a, 0.004 );\n"
	"  a = opaque > 0.5 ? 1.0 : clamp( a * 1.6, 0.0, 1.0 );\n"
	"  gl_FragColor = vec4( rgb, a * fade );\n"
	"}\n";

static bool RcVrMakeGL( void )
{
	if ( s_iGLState )
		return s_iGLState > 0;
	s_iGLState = -1;

	s_gl.GenFramebuffers = (RcGlGenFramebuffers_t)RcVrGL( "glGenFramebuffers", "glGenFramebuffersEXT" );
	s_gl.BindFramebuffer = (RcGlBindFramebuffer_t)RcVrGL( "glBindFramebuffer", "glBindFramebufferEXT" );
	s_gl.FramebufferTexture2D = (RcGlFramebufferTexture2D_t)RcVrGL( "glFramebufferTexture2D", "glFramebufferTexture2DEXT" );
	s_gl.GenRenderbuffers = (RcGlGenRenderbuffers_t)RcVrGL( "glGenRenderbuffers", "glGenRenderbuffersEXT" );
	s_gl.BindRenderbuffer = (RcGlBindRenderbuffer_t)RcVrGL( "glBindRenderbuffer", "glBindRenderbufferEXT" );
	s_gl.RenderbufferStorage = (RcGlRenderbufferStorage_t)RcVrGL( "glRenderbufferStorage", "glRenderbufferStorageEXT" );
	s_gl.FramebufferRenderbuffer = (RcGlFramebufferRenderbuffer_t)RcVrGL( "glFramebufferRenderbuffer", "glFramebufferRenderbufferEXT" );
	s_gl.CheckFramebufferStatus = (RcGlCheckFramebufferStatus_t)RcVrGL( "glCheckFramebufferStatus", "glCheckFramebufferStatusEXT" );
	s_gl.ActiveTexture = (RcGlActiveTexture_t)RcVrGL( "glActiveTexture", "glActiveTextureARB" );
	HMODULE hGL = GetModuleHandleA( "opengl32.dll" );
	s_gl.GenTextures = (RcGlGenTextures_t)GetProcAddress( hGL, "glGenTextures" );
	s_gl.LineWidth = (RcGlLineWidth_t)GetProcAddress( hGL, "glLineWidth" );
	s_gl.PointSize = (RcGlPointSize_t)GetProcAddress( hGL, "glPointSize" );

	if ( !s_gl.GenFramebuffers || !s_gl.BindFramebuffer || !s_gl.FramebufferTexture2D || !s_gl.GenRenderbuffers || !s_gl.BindRenderbuffer ||
		!s_gl.RenderbufferStorage || !s_gl.FramebufferRenderbuffer || !s_gl.CheckFramebufferStatus || !s_gl.GenTextures || !s_gl.LineWidth || !s_gl.PointSize )
	{
		gEngfuncs.Con_Printf( "ReCoF VR: the driver has no framebuffer objects (OpenGL %s)\n", (const char *)gl.glGetString( GL_VERSION ) );
		return false;
	}

	GLint iOldTex = 0;
	gl.glGetIntegerv( GL_TEXTURE_BINDING_2D, &iOldTex );
	bool ok = RcVrMakeEye( 0 ) && RcVrMakeEye( 1 );

	s_gl.GenTextures( 1, &s_texScreen );
	gl.glBindTexture( GL_TEXTURE_2D, iOldTex );
	if ( !ok )
		return false;

	if ( RcGlslInit() )
	{
		static const int rgIndex[] = { 0 };
		static const char *rgName[] = { NULL };
		s_iProgram = RcGlslProgram( "vr screen", s_pszVrVertex, s_pszVrFragment, rgIndex, rgName );
		if ( s_iProgram )
		{
			s_iProgTex = rcgl.glGetUniformLocation( s_iProgram, "tex" );
			s_iProgOpaque = rcgl.glGetUniformLocation( s_iProgram, "opaque" );
			s_iProgFade = rcgl.glGetUniformLocation( s_iProgram, "fade" );
		}
	}

	// the desktop window must not wait for the monitor: the headset paces the game
	RcWglSwapInterval_t pfnSwap = (RcWglSwapInterval_t)RcVrGL( "wglSwapIntervalEXT", NULL );
	if ( pfnSwap )
		pfnSwap( 0 );

	gEngfuncs.Con_Printf( "ReCoF VR: OpenGL %s, eye framebuffers ready, screen shader %s\n", (const char *)gl.glGetString( GL_VERSION ), s_iProgram ? "on" : "off (additive)" );
	s_iGLState = 1;
	return true;
}

// ------------------------------------------------------------------------------------------------------------
// poses, hands and the head in the game

static void RcVrUpdateHand( int h, const float *headGame, const float *headTrack, float yaw )
{
	RcVrHand_t &hand = s_rgHand[ h ];
	hand.index = s_pSystem->GetTrackedDeviceIndexForControllerRole( h ? vr::TrackedControllerRole_RightHand : vr::TrackedControllerRole_LeftHand );
	hand.valid = hand.index != vr::k_unTrackedDeviceIndexInvalid && hand.index < vr::k_unMaxTrackedDeviceCount && s_rgPoses[ hand.index ].bPoseIsValid;
	if ( !hand.valid )
		return;

	const vr::HmdMatrix34_t &m = s_rgPoses[ hand.index ].mDeviceToAbsoluteTracking;
	float scale = rc_vr_scale->value;

	// tracking axes, the forward pitched down by rc_vr_aimpitch round the controller's right axis
	float tr[ 3 ], tu[ 3 ], tb[ 3 ], tf[ 3 ];
	RcVrColumn( m, 0, tr );
	RcVrColumn( m, 1, tu );
	RcVrColumn( m, 2, tb );
	tf[ 0 ] = -tb[ 0 ]; tf[ 1 ] = -tb[ 1 ]; tf[ 2 ] = -tb[ 2 ];
	float a = rc_vr_aimpitch->value * RCVR_PI / 180, ca = cosf( a ), sa = sinf( a );
	float f2[ 3 ], u2[ 3 ];
	for ( int i = 0; i < 3; i++ )
	{
		f2[ i ] = tf[ i ] * ca - tu[ i ] * sa;
		u2[ i ] = tf[ i ] * sa + tu[ i ] * ca;
	}
	memcpy( hand.tfwd, f2, sizeof( f2 ) );

	// grip point in the controller's frame (cm)
	float gf = rc_vr_grip_fwd->value * 0.01f, gr = rc_vr_grip_right->value * 0.01f, gu = rc_vr_grip_up->value * 0.01f;
	if ( !h )
		gr = -gr;
	for ( int i = 0; i < 3; i++ )
		hand.tpos[ i ] = m.m[ i ][ 3 ] + f2[ i ] * gf + tr[ i ] * gr + u2[ i ] * gu;

	float off[ 3 ] = { hand.tpos[ 0 ] - headTrack[ 0 ], hand.tpos[ 1 ] - headTrack[ 1 ], hand.tpos[ 2 ] - headTrack[ 2 ] };
	float g[ 3 ];
	RcVrDirToGame( off, g, yaw );
	for ( int i = 0; i < 3; i++ )
		hand.pos[ i ] = headGame[ i ] + g[ i ] * scale;

	RcVrDirToGame( f2, hand.fwd, yaw );
	RcVrDirToGame( tr, hand.right, yaw );
	RcVrDirToGame( u2, hand.up, yaw );
	RcVrAngles( hand.fwd, hand.right, hand.up, hand.angles );
}

// the head centre in the game from the game's eye, and the hands; called once a frame from the first pass and
// from CL_CreateMove (which runs before the view)
static void RcVrUpdateBody( const float *gameEye, float yaw, bool bOwnView )
{
	if ( !RcVrHmdValid() )
		return;

	const vr::HmdMatrix34_t &m = s_rgPoses[ vr::k_unTrackedDeviceIndex_Hmd ].mDeviceToAbsoluteTracking;
	float head[ 3 ] = { m.m[ 0 ][ 3 ], m.m[ 1 ][ 3 ], m.m[ 2 ][ 3 ] };

	if ( s_bRecentre )
	{
		s_bRecentre = false;
		memcpy( s_rgRef, head, sizeof( head ) );
		// the head's current direction becomes the game's current view direction
		float ang[ 3 ];
		gEngfuncs.GetViewAngles( ang );
		s_flTurnYaw = ang[ 1 ] - RcVrHmdYaw();
		yaw = bOwnView ? s_flTurnYaw : yaw;
		gEngfuncs.Con_Printf( "ReCoF VR: recentred (head %.2f m high)\n", head[ 1 ] );
	}

	float off[ 3 ] = { head[ 0 ] - s_rgRef[ 0 ], head[ 1 ] - s_rgRef[ 1 ], head[ 2 ] - s_rgRef[ 2 ] };
	float g[ 3 ];
	RcVrDirToGame( off, g, yaw );
	float scale = rc_vr_scale->value;
	for ( int i = 0; i < 3; i++ )
		g[ i ] *= scale;

	if ( bOwnView )
	{
		// lean, not walk: the body is where the movement put it
		float lean = rc_vr_lean->value;
		float len = sqrtf( g[ 0 ] * g[ 0 ] + g[ 1 ] * g[ 1 ] );
		if ( len > lean && len > 0 )
		{
			g[ 0 ] *= lean / len;
			g[ 1 ] *= lean / len;
		}
		if ( g[ 2 ] > 16 ) g[ 2 ] = 16;
		if ( g[ 2 ] < -40 ) g[ 2 ] = -40;
	}

	for ( int i = 0; i < 3; i++ )
		s_rgHeadGame[ i ] = gameEye[ i ] + g[ i ];
	s_flBaseYaw = yaw;

	RcVrUpdateHand( 0, s_rgHeadGame, head, yaw );
	RcVrUpdateHand( 1, s_rgHeadGame, head, yaw );
}

// an eye's game origin and angles
static void RcVrEye( int e, float *origin, float *angles )
{
	const vr::HmdMatrix34_t &hmd = s_rgPoses[ vr::k_unTrackedDeviceIndex_Hmd ].mDeviceToAbsoluteTracking;
	vr::HmdMatrix34_t eye;
	RcVrMul34( hmd, s_rgEyeToHead[ e ], eye );

	float scale = rc_vr_scale->value;
	float off[ 3 ] = { eye.m[ 0 ][ 3 ] - hmd.m[ 0 ][ 3 ], eye.m[ 1 ][ 3 ] - hmd.m[ 1 ][ 3 ], eye.m[ 2 ][ 3 ] - hmd.m[ 2 ][ 3 ] };
	float g[ 3 ];
	RcVrDirToGame( off, g, s_flBaseYaw );
	for ( int i = 0; i < 3; i++ )
		origin[ i ] = s_rgHeadGame[ i ] + g[ i ] * scale;

	float tr[ 3 ], tu[ 3 ], tb[ 3 ], tf[ 3 ], f[ 3 ], r[ 3 ], u[ 3 ];
	RcVrColumn( eye, 0, tr );
	RcVrColumn( eye, 1, tu );
	RcVrColumn( eye, 2, tb );
	tf[ 0 ] = -tb[ 0 ]; tf[ 1 ] = -tb[ 1 ]; tf[ 2 ] = -tb[ 2 ];
	RcVrDirToGame( tf, f, s_flBaseYaw );
	RcVrDirToGame( tr, r, s_flBaseYaw );
	RcVrDirToGame( tu, u, s_flBaseYaw );
	RcVrAngles( f, r, u, angles );
}

// ------------------------------------------------------------------------------------------------------------
// the passes (view.cpp V_CalcRefdef)

static void RcVrBindEye( int e )
{
	s_gl.BindFramebuffer( GL_FRAMEBUFFER, s_rgFbo[ e ] );
	s_bEyeBound = true;
	gl.glViewport( 0, 0, s_iEyeW, s_iEyeH );
	if ( s_pfnSetEye )
		s_pfnSetEye( 1, s_rgTan[ e ][ 0 ], s_rgTan[ e ][ 1 ], s_rgTan[ e ][ 2 ], s_rgTan[ e ][ 3 ], s_iEyeW, s_iEyeH, ScreenWidth, ScreenHeight );
}

static void RcVrUnbindEye( void )
{
	if ( s_pfnSetEye )
		s_pfnSetEye( 0, 0, 0, 0, 0, 0, 0, 0, 0 );
	if ( s_bEyeBound )
	{
		s_gl.BindFramebuffer( GL_FRAMEBUFFER, 0 );
		s_bEyeBound = false;
	}
	gl.glViewport( 0, 0, ScreenWidth, ScreenHeight );
}

static void ( *s_pfnPassEnd )( struct ref_params_s * );

static void RcVrPlaceViewModel( void )
{
	cl_entity_t *view = gEngfuncs.GetViewModel();
	if ( !view || !s_rgHand[ 1 ].valid )
		return;
	VectorCopy( s_rgHand[ 1 ].pos, view->origin );
	VectorCopy( s_rgHand[ 1 ].angles, view->angles );
	VectorCopy( s_rgHand[ 1 ].angles, view->curstate.angles );

	// the left hand item (flashlight, lantern) on the left controller
	if ( g_iHeadShieldType_guess == 3 && s_rgHand[ 0 ].valid && rc_vr_lefthand_item->value )
	{
		VectorCopy( s_rgHand[ 0 ].pos, g_HeadShieldEnt.origin );
		VectorCopy( s_rgHand[ 0 ].pos, g_HeadShieldEnt.curstate.origin );
		VectorCopy( s_rgHand[ 0 ].angles, g_HeadShieldEnt.angles );
		VectorCopy( s_rgHand[ 0 ].angles, g_HeadShieldEnt.curstate.angles );
	}
}

void RcVR_CalcRefdef( struct ref_params_s *pparams, void ( *pfnGame )( struct ref_params_s * ), int ( *pfnOwnView )( struct ref_params_s * ),
	void ( *pfnPassEnd )( struct ref_params_s * ) )
{
	s_pfnPassEnd = pfnPassEnd;
	if ( !pparams->nextView )
		s_iPass = 0;

	if ( !RcVrMakeGL() || !RcVrHmdValid() )
	{
		// no headset picture yet: the flat view, once
		s_bSecondEye = false;
		pparams->nextView = 0;
		pfnGame( pparams );
		pparams->nextView = 0;
		pparams->onlyClientDraw = 0;
		s_pfnPassEnd( pparams );
		return;
	}

	float origin[ 3 ], angles[ 3 ];

	switch ( s_iPass )
	{
	case 0:	// left eye: the game's view once a frame
	default:
	{
		s_bSecondEye = false;
		pparams->nextView = 0;
		pparams->onlyClientDraw = 0;
		pfnGame( pparams );	// V_CalcNormalRefdef and the rest; sets nextView 1 if Cry of Fear wants a head shield pass
		s_bShield = pparams->nextView != 0;
		s_bOwnView = pfnOwnView( pparams ) != 0;

		// the game's eye: own view without bob (the step smoothing kept), a camera as it is
		float eye[ 3 ];
		if ( s_bOwnView )
		{
			eye[ 0 ] = pparams->simorg[ 0 ] + pparams->viewheight[ 0 ];
			eye[ 1 ] = pparams->simorg[ 1 ] + pparams->viewheight[ 1 ];
			float z = pparams->simorg[ 2 ] + pparams->viewheight[ 2 ];
			float dz = z - s_flSmoothZ;
			if ( pparams->onground && dz > 0 && dz < 24 )
			{
				s_flSmoothZ += dz * ( pparams->frametime * 12 < 1 ? pparams->frametime * 12 : 1 );
				z = s_flSmoothZ;
			}
			else
				s_flSmoothZ = z;
			eye[ 2 ] = z;
		}
		else
			VectorCopy( pparams->vieworg, eye );
		VectorCopy( eye, s_rgGameEye );

		// a camera turns the world by its own yaw, from the head's direction when it began
		float yaw = s_flTurnYaw;
		if ( !s_bOwnView )
		{
			// the camera's yaw follows its turns (the wheelchair, moving cameras); the head looks round in it
			if ( !s_bWasCamera )
				s_flCamHmdYaw = RcVrHmdYaw();
			yaw = pparams->viewangles[ 1 ] - s_flCamHmdYaw;
		}
		else if ( s_bWasCamera )
		{
			// back to the player: the head looks where the player looks
			float ang[ 3 ];
			gEngfuncs.GetViewAngles( ang );
			s_flTurnYaw = ang[ 1 ] - RcVrHmdYaw();
			yaw = s_flTurnYaw;
		}
		s_bWasCamera = !s_bOwnView;

		RcVrUpdateBody( eye, yaw, s_bOwnView );
		if ( s_bOwnView )
			RcVrPlaceViewModel();

		s_bEyesDrawn = true;
		RcVrEye( 0, origin, angles );
		VectorCopy( origin, pparams->vieworg );
		VectorCopy( angles, pparams->viewangles );
		VectorCopy( origin, v_origin );
		VectorCopy( angles, v_angles );
		RcVrBindEye( 0 );
		pparams->viewport[ 0 ] = 0;
		pparams->viewport[ 1 ] = 0;
		pparams->viewport[ 2 ] = s_iEyeW;
		pparams->viewport[ 3 ] = s_iEyeH;
		gl.glClearColor( 0, 0, 0, 1 );
		gl.glClear( GL_COLOR_BUFFER_BIT );
		s_pfnPassEnd( pparams );

		pparams->nextView = 1;
		s_iPass = s_bShield ? 1 : 2;
		break;
	}

	case 1:	// left eye's head shield
		pparams->onlyClientDraw = 1;
		g_DrawHeadShied = 1;
		s_pfnPassEnd( pparams );
		pparams->nextView = 1;
		s_iPass = 2;
		break;

	case 2:	// right eye
		s_bSecondEye = true;
		g_DrawHeadShied = 0;
		pparams->onlyClientDraw = 0;
		RcVrEye( 1, origin, angles );
		VectorCopy( origin, pparams->vieworg );
		VectorCopy( angles, pparams->viewangles );
		VectorCopy( origin, v_origin );
		VectorCopy( angles, v_angles );
		RcVrBindEye( 1 );
		gl.glClearColor( 0, 0, 0, 1 );
		gl.glClear( GL_COLOR_BUFFER_BIT );
		s_pfnPassEnd( pparams );
		pparams->nextView = s_bShield ? 1 : 0;
		s_iPass = 3;
		break;

	case 3:	// right eye's head shield
		pparams->onlyClientDraw = 1;
		g_DrawHeadShied = 1;
		s_pfnPassEnd( pparams );
		pparams->nextView = 0;
		s_iPass = 4;
		break;
	}
}

// the engine's last fade went to the right eye only (hw.dll V_RenderView ends with R_PolyBlend): the left eye gets
// the same once more, as each picture of the flat game gets it twice
static void RcVrFadeLeftEye( void )
{
	screenfade_t sf;
	gEngfuncs.pfnGetScreenFade( &sf );
	float t = gEngfuncs.GetClientTime();
	int alpha;
	if ( t > sf.fadeReset && t > sf.fadeEnd && !( sf.fadeFlags & FFADE_STAYOUT ) )
		return;
	if ( sf.fadeFlags & FFADE_STAYOUT )
	{
		if ( ( sf.fadeFlags & FFADE_OUT ) && sf.fadeTotalEnd > t )
			alpha = (int)( sf.fadealpha + sf.fadeSpeed * ( sf.fadeTotalEnd - t ) );
		else
			alpha = sf.fadealpha;
	}
	else if ( sf.fadeFlags & FFADE_OUT )
		alpha = (int)( sf.fadealpha + sf.fadeSpeed * ( sf.fadeEnd - t ) );
	else
		alpha = (int)( sf.fadeSpeed * ( sf.fadeEnd - t ) );
	if ( alpha > sf.fadealpha )
		alpha = sf.fadealpha;
	if ( alpha <= 0 )
		return;

	s_gl.BindFramebuffer( GL_FRAMEBUFFER, s_rgFbo[ 0 ] );
	gl.glViewport( 0, 0, s_iEyeW, s_iEyeH );
	gl.glMatrixMode( GL_PROJECTION );
	gl.glPushMatrix();
	gl.glLoadIdentity();
	gl.glOrtho( 0, 1, 1, 0, -1, 1 );
	gl.glMatrixMode( GL_MODELVIEW );
	gl.glPushMatrix();
	gl.glLoadIdentity();
	gl.glPushAttrib( GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT );
	gl.glDisable( GL_TEXTURE_2D );
	gl.glDisable( GL_DEPTH_TEST );
	gl.glDisable( GL_ALPHA_TEST );
	gl.glDisable( GL_CULL_FACE );
	gl.glEnable( GL_BLEND );
	if ( sf.fadeFlags & FFADE_MODULATE )
	{
		gl.glBlendFunc( GL_ZERO, GL_SRC_COLOR );
		gl.glColor4f( ( sf.fader * alpha + ( 255 - alpha ) * 255 ) / 65280.0f, ( sf.fadeg * alpha + ( 255 - alpha ) * 255 ) / 65280.0f,
			( sf.fadeb * alpha + ( 255 - alpha ) * 255 ) / 65280.0f, 1 );
	}
	else
	{
		gl.glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		gl.glColor4f( sf.fader / 255.0f, sf.fadeg / 255.0f, sf.fadeb / 255.0f, alpha / 255.0f );
	}
	gl.glBegin( GL_QUADS );
	gl.glVertex3f( 0, 0, 0 );
	gl.glVertex3f( 0, 1, 0 );
	gl.glVertex3f( 1, 1, 0 );
	gl.glVertex3f( 1, 0, 0 );
	gl.glEnd();
	gl.glPopAttrib();
	gl.glMatrixMode( GL_PROJECTION );
	gl.glPopMatrix();
	gl.glMatrixMode( GL_MODELVIEW );
	gl.glPopMatrix();
}

void RcVR_HudRedrawStart( void )
{
	if ( !s_bActive )
		return;
	s_bSecondEye = false;
	g_DrawHeadShied = 0;
	if ( !s_bEyeBound )
		return;

	RcVrFadeLeftEye();
	RcVrUnbindEye();

	// the window holds only the 2D from here: black where nothing is drawn
	gl.glClearColor( 0, 0, 0, 0 );
	gl.glClear( GL_COLOR_BUFFER_BIT );
}

float RcVR_CullFov( void )
{
	// the engine culls with this horizontal field of view and the window's shape: wide enough for an eye
	float h = 0, v = 0;
	for ( int e = 0; e < 2; e++ )
	{
		h = max( h, max( fabsf( s_rgTan[ e ][ 0 ] ), fabsf( s_rgTan[ e ][ 1 ] ) ) );
		v = max( v, max( fabsf( s_rgTan[ e ][ 2 ] ), fabsf( s_rgTan[ e ][ 3 ] ) ) );
	}
	float aspect = ScreenHeight > 0 ? (float)ScreenWidth / ScreenHeight : 1.78f;
	float t = max( h, v * aspect ) * 1.1f;
	float fov = 2 * atanf( t ) * 180 / RCVR_PI;
	return fov > 170 ? 170 : fov;
}

float RcVR_CullViewsize( void )
{
	float h = 0, v = 0;
	for ( int e = 0; e < 2; e++ )
	{
		h = max( h, max( fabsf( s_rgTan[ e ][ 0 ] ), fabsf( s_rgTan[ e ][ 1 ] ) ) );
		v = max( v, max( fabsf( s_rgTan[ e ][ 2 ] ), fabsf( s_rgTan[ e ][ 3 ] ) ) );
	}
	float fh = 2 * atanf( h * 1.1f ) * 180 / RCVR_PI, fv = 2 * atanf( v * 1.1f ) * 180 / RCVR_PI;
	float size = max( fh, fv / 0.75f );
	return size > 170 ? 170 : size;
}

// ------------------------------------------------------------------------------------------------------------
// the weapon model in the hand (StudioModelRenderer.cpp)

static int RcVrBone( studiohdr_t *hdr, const char *a, const char *b )
{
	mstudiobone_t *bones = (mstudiobone_t *)( (byte *)hdr + hdr->boneindex );
	for ( int i = 0; i < hdr->numbones; i++ )
	{
		if ( !_stricmp( bones[ i ].name, a ) || ( b && !_stricmp( bones[ i ].name, b ) ) )
			return i;
	}
	return -1;
}

static bool RcVrArmBone( const char *name, bool &bLeft )
{
	static const char *rgRight[] = { "r_up", "r_upp", "r_low", "r_dum", "r_dummy", "r_slv", "upperarm_dummyR", "rhand_sleevedummy", NULL };
	static const char *rgLeft[] = { "l_up", "l_upp", "l_low", "l_dum", "l_dummy", "l_slv", "upperarm_dummyL", "lhand_sleevedummy", NULL };
	for ( int i = 0; rgRight[ i ]; i++ )
	{
		if ( !_stricmp( name, rgRight[ i ] ) )
		{
			bLeft = false;
			return true;
		}
	}
	for ( int i = 0; rgLeft[ i ]; i++ )
	{
		if ( !_stricmp( name, rgLeft[ i ] ) )
		{
			bLeft = true;
			return true;
		}
	}
	return false;
}

// the left hand: a copy of the weapon model whose left hand is on the left controller and everything else
// folded into it; the weapon model's own left hand is folded away then (one gun hand, one free hand)
extern engine_studio_api_t IEngineStudio;
static cl_entity_t	s_HandEnt;
static bool			s_bLeftHand;

void RcVR_CreateEntities( void )
{
	s_bLeftHand = false;
	if ( !s_bActive || !s_bOwnView || !rc_vr_lefthand->value || !s_rgHand[ 0 ].valid || !RcVrHmdValid() )
		return;
	if ( g_iHeadShieldType_guess == 3 && rc_vr_lefthand_item->value )
		return;	// the left hand holds its flashlight or lantern

	cl_entity_t *view = gEngfuncs.GetViewModel();
	if ( !view || !view->model || view->model->type != mod_studio )
		return;
	studiohdr_t *hdr = (studiohdr_t *)IEngineStudio.Mod_Extradata( view->model );
	if ( !hdr || RcVrBone( hdr, "l_wr", "l_wrist" ) < 0 || RcVrBone( hdr, "r_wr", "r_wrist" ) < 0 )
		return;

	s_HandEnt = *view;
	s_HandEnt.index = 0;
	s_HandEnt.player = 0;
	s_HandEnt.curstate.rendermode = kRenderNormal;
	s_HandEnt.curstate.renderfx = kRenderFxNone;
	s_HandEnt.curstate.renderamt = 255;
	s_HandEnt.curstate.movetype = MOVETYPE_NONE;
	VectorCopy( s_rgHand[ 0 ].pos, s_HandEnt.origin );
	VectorCopy( s_rgHand[ 0 ].pos, s_HandEnt.curstate.origin );
	VectorCopy( s_rgHand[ 0 ].angles, s_HandEnt.angles );
	VectorCopy( s_rgHand[ 0 ].angles, s_HandEnt.curstate.angles );
	if ( gEngfuncs.CL_CreateVisibleEntity( ET_NORMAL, &s_HandEnt ) )
		s_bLeftHand = true;
}

bool RcVR_NoEvents( struct cl_entity_s *ent )
{
	return s_bSecondEye || ( s_bActive && ent == &s_HandEnt );
}

// every bone whose name does (bLeft) or does not start with l_ folded into the point p
static void RcVrFold( studiohdr_t *hdr, float ( *bonetransform )[ 128 ][ 3 ][ 4 ], float ( *lighttransform )[ 128 ][ 3 ][ 4 ], bool bLeft, const float *p )
{
	mstudiobone_t *bones = (mstudiobone_t *)( (byte *)hdr + hdr->boneindex );
	for ( int b = 0; b < hdr->numbones; b++ )
	{
		bool bIsLeft = !_strnicmp( bones[ b ].name, "l_", 2 );
		if ( bIsLeft != bLeft )
			continue;
		for ( int i = 0; i < 3; i++ )
		{
			for ( int j = 0; j < 3; j++ )
			{
				( *bonetransform )[ b ][ i ][ j ] = 0;
				if ( lighttransform )
					( *lighttransform )[ b ][ i ][ j ] = 0;
			}
			( *bonetransform )[ b ][ i ][ 3 ] = p[ i ];
			if ( lighttransform )
				( *lighttransform )[ b ][ i ][ 3 ] = p[ i ];
		}
	}
}

void RcVR_AdjustBones( struct cl_entity_s *ent, void *pStudioHeader, float ( *bonetransform )[ 128 ][ 3 ][ 4 ], float ( *lighttransform )[ 128 ][ 3 ][ 4 ] )
{
	if ( !s_bActive || !s_bOwnView || !pStudioHeader || !bonetransform )
		return;

	int hand;
	bool bHandEnt = false;
	if ( ent == gEngfuncs.GetViewModel() )
		hand = 1;
	else if ( ent == &s_HandEnt )
	{
		hand = 0;
		bHandEnt = true;
	}
	else if ( ent == &g_HeadShieldEnt && g_iHeadShieldType_guess == 3 && rc_vr_lefthand_item->value )
		hand = 0;
	else
		return;
	if ( !s_rgHand[ hand ].valid )
		return;

	studiohdr_t *hdr = (studiohdr_t *)pStudioHeader;
	int iWrist = hand ? RcVrBone( hdr, "r_wr", "r_wrist" ) : RcVrBone( hdr, "l_wr", "l_wrist" );
	int iOther = hand ? RcVrBone( hdr, "l_wr", "l_wrist" ) : RcVrBone( hdr, "r_wr", "r_wrist" );

	// the model's wrist to the controller's grip point
	if ( iWrist >= 0 )
	{
		float d[ 3 ];
		for ( int i = 0; i < 3; i++ )
			d[ i ] = s_rgHand[ hand ].pos[ i ] - ( *bonetransform )[ iWrist ][ i ][ 3 ];
		for ( int b = 0; b < hdr->numbones; b++ )
		{
			for ( int i = 0; i < 3; i++ )
			{
				( *bonetransform )[ b ][ i ][ 3 ] += d[ i ];
				if ( lighttransform )
					( *lighttransform )[ b ][ i ][ 3 ] += d[ i ];
			}
		}
	}

	if ( bHandEnt )
	{
		// only the left hand: the weapon, the right hand and the arms folded into the left wrist
		if ( iWrist < 0 )
			return;
		float p[ 3 ] = { ( *bonetransform )[ iWrist ][ 0 ][ 3 ], ( *bonetransform )[ iWrist ][ 1 ][ 3 ], ( *bonetransform )[ iWrist ][ 2 ][ 3 ] };
		RcVrFold( hdr, bonetransform, lighttransform, false, p );
		static const char *rgArm[] = { "l_up", "l_upp", "l_low", "l_dum", "l_dummy", "l_slv", NULL };
		mstudiobone_t *bones = (mstudiobone_t *)( (byte *)hdr + hdr->boneindex );
		for ( int b = 0; b < hdr->numbones; b++ )
		{
			for ( int k = 0; rgArm[ k ]; k++ )
			{
				if ( _stricmp( bones[ b ].name, rgArm[ k ] ) )
					continue;
				for ( int i = 0; i < 3; i++ )
				{
					for ( int j = 0; j < 3; j++ )
					{
						( *bonetransform )[ b ][ i ][ j ] = 0;
						if ( lighttransform )
							( *lighttransform )[ b ][ i ][ j ] = 0;
					}
					( *bonetransform )[ b ][ i ][ 3 ] = p[ i ];
					if ( lighttransform )
						( *lighttransform )[ b ][ i ][ 3 ] = p[ i ];
				}
			}
		}
		return;
	}

	if ( hand == 1 && s_bLeftHand && iWrist >= 0 )
	{
		// the left controller has its own hand: the weapon model's left hand folded away
		float p[ 3 ] = { ( *bonetransform )[ iWrist ][ 0 ][ 3 ], ( *bonetransform )[ iWrist ][ 1 ][ 3 ], ( *bonetransform )[ iWrist ][ 2 ][ 3 ] };
		RcVrFold( hdr, bonetransform, lighttransform, true, p );
	}

	if ( !rc_vr_hidearms->value )
		return;

	// the arms folded into the wrists: floating hands
	mstudiobone_t *bones = (mstudiobone_t *)( (byte *)hdr + hdr->boneindex );
	for ( int b = 0; b < hdr->numbones; b++ )
	{
		bool bLeft;
		if ( !RcVrArmBone( bones[ b ].name, bLeft ) )
			continue;
		int w = ( bLeft == ( hand == 0 ) ) ? iWrist : iOther;
		if ( w < 0 )
			continue;
		float p[ 3 ] = { ( *bonetransform )[ w ][ 0 ][ 3 ], ( *bonetransform )[ w ][ 1 ][ 3 ], ( *bonetransform )[ w ][ 2 ][ 3 ] };
		for ( int i = 0; i < 3; i++ )
		{
			for ( int j = 0; j < 3; j++ )
			{
				( *bonetransform )[ b ][ i ][ j ] = 0;
				if ( lighttransform )
					( *lighttransform )[ b ][ i ][ j ] = 0;
			}
			( *bonetransform )[ b ][ i ][ 3 ] = p[ i ];
			if ( lighttransform )
				( *lighttransform )[ b ][ i ][ 3 ] = p[ i ];
		}
	}
}

// ------------------------------------------------------------------------------------------------------------
// input (input.cpp CL_CreateMove)

static bool RcVrPressed( const RcVrHand_t &h, vr::EVRButtonId id )
{
	return ( h.state.ulButtonPressed & vr::ButtonMaskFromId( id ) ) != 0;
}

static bool RcVrWasPressed( const RcVrHand_t &h, vr::EVRButtonId id )
{
	return ( h.old.ulButtonPressed & vr::ButtonMaskFromId( id ) ) != 0;
}

static void RcVrButton( int b, bool down )
{
	if ( down == s_rgButtonDown[ b ] )
		return;
	s_rgButtonDown[ b ] = down;
	char cmd[ 32 ];
	_snprintf( cmd, sizeof( cmd ), "%c%s\n", down ? '+' : '-', s_rgButtonCmd[ b ] );
	cmd[ sizeof( cmd ) - 1 ] = 0;
	gEngfuncs.pfnClientCmd( cmd );
}

static void RcVrReleaseButtons( void )
{
	for ( int b = 0; b < RCVB_COUNT; b++ )
		RcVrButton( b, false );
}

static void RcVrKey( WPARAM vk )
{
	if ( !s_hWnd )
		return;
	// the engine reads the scan code (bits 16 to 23), not the virtual key
	LPARAM scan = (LPARAM)( MapVirtualKeyA( (UINT)vk, 0 ) & 0xFF ) << 16;
	PostMessageA( s_hWnd, WM_KEYDOWN, vk, scan | 1 );
	PostMessageA( s_hWnd, WM_KEYUP, vk, scan | 1 | ( 1u << 30 ) | ( 1u << 31 ) );
}

static void RcVrPollEvents( void )
{
	vr::VREvent_t ev;
	while ( s_pSystem->PollNextEvent( &ev, sizeof( ev ) ) )
	{
		if ( ev.eventType == vr::VREvent_Quit )
		{
			gEngfuncs.Con_Printf( "ReCoF VR: SteamVR is closing\n" );
			s_pSystem->AcknowledgeQuit_Exiting();
		}
	}
}

static bool s_bInputThisFrame;	// CL_CreateMove read the controllers this frame

static void RcVrReadControllers( void )
{
	RcVrPollEvents();
	for ( int h = 0; h < 2; h++ )
	{
		RcVrHand_t &hand = s_rgHand[ h ];
		hand.old = hand.state;
		vr::TrackedDeviceIndex_t idx = s_pSystem->GetTrackedDeviceIndexForControllerRole( h ? vr::TrackedControllerRole_RightHand : vr::TrackedControllerRole_LeftHand );
		if ( idx == vr::k_unTrackedDeviceIndexInvalid || !s_pSystem->GetControllerState( idx, &hand.state, sizeof( hand.state ) ) )
			memset( &hand.state, 0, sizeof( hand.state ) );
	}
	s_bInputThisFrame = true;
}

// the pointer's clicks on the screen: the right trigger is the left mouse button, B and Y are Escape
static void RcVrPointerInput( void )
{
	RcVrHand_t &L = s_rgHand[ 0 ], &R = s_rgHand[ 1 ];
	bool bTrigger = R.state.rAxis[ 1 ].x > 0.6f;
	if ( bTrigger != s_bMouseDown && s_hWnd && ( s_bPointerOnScreen || !bTrigger ) )
	{
		s_bMouseDown = bTrigger;
		RECT rc;
		GetClientRect( s_hWnd, &rc );
		int x = (int)( s_flPointerU * ( rc.right - rc.left ) ), y = (int)( ( 1 - s_flPointerV ) * ( rc.bottom - rc.top ) );
		PostMessageA( s_hWnd, bTrigger ? WM_LBUTTONDOWN : WM_LBUTTONUP, bTrigger ? MK_LBUTTON : 0, MAKELPARAM( x, y ) );
	}
	if ( RcVrPressed( R, vr::k_EButton_ApplicationMenu ) && !RcVrWasPressed( R, vr::k_EButton_ApplicationMenu ) )
		RcVrKey( VK_ESCAPE );
	if ( RcVrPressed( L, vr::k_EButton_ApplicationMenu ) && !RcVrWasPressed( L, vr::k_EButton_ApplicationMenu ) )
		RcVrKey( VK_ESCAPE );
}

void RcVR_CreateMove( float frametime, struct usercmd_s *cmd, int active, int bAim )
{
	if ( !s_bActive )
		return;

	RcVrReadControllers();

	// the hands and the head for this frame's aim (the view updates them again)
	float ang[ 3 ];
	gEngfuncs.GetViewAngles( ang );

	// a turn not made here (the server's: a spawn, a teleport, a scene; or the desktop mouse) turns the world
	if ( !bAim )
	{
		ang[ 0 ] = cmd->viewangles[ 0 ];
		ang[ 1 ] = cmd->viewangles[ 1 ];
	}
	else if ( s_bAnglesSet )
	{
		float d = ang[ 1 ] - s_rgLastSetAngles[ 1 ];
		while ( d > 180 ) d -= 360;
		while ( d < -180 ) d += 360;
		if ( fabsf( d ) > 0.5f )
			s_flTurnYaw += d;
	}

	RcVrHand_t &L = s_rgHand[ 0 ], &R = s_rgHand[ 1 ];
	s_bCursorMode = RcIN_MouseFree() != 0 || !active;

	// both grips held 1.5 s: recentre
	if ( RcVrPressed( L, vr::k_EButton_Grip ) && RcVrPressed( R, vr::k_EButton_Grip ) )
	{
		s_flBothGrips += frametime;
		if ( s_flBothGrips > 1.5f )
		{
			s_flBothGrips = -100;
			s_bRecentre = true;
		}
	}
	else
		s_flBothGrips = 0;

	if ( s_bCursorMode )
	{
		// a menu, the console or a panel (the inventory) has the mouse: the right hand points and clicks
		RcVrReleaseButtons();
		RcVrPointerInput();
		if ( RcVrPressed( L, vr::k_EButton_A ) && !RcVrWasPressed( L, vr::k_EButton_A ) )
			gEngfuncs.pfnClientCmd( (char *)"+inventory\n-inventory\n" );
		s_bAnglesSet = false;
		return;
	}

	// Y: the game menu
	if ( RcVrPressed( L, vr::k_EButton_ApplicationMenu ) && !RcVrWasPressed( L, vr::k_EButton_ApplicationMenu ) )
		RcVrKey( VK_ESCAPE );

	// buttons
	RcVrButton( RCVB_ATTACK, R.state.rAxis[ 1 ].x > 0.6f );
	RcVrButton( RCVB_ATTACK2, L.state.rAxis[ 1 ].x > 0.6f );
	RcVrButton( RCVB_USE, RcVrPressed( R, vr::k_EButton_Grip ) );
	RcVrButton( RCVB_RELOAD, RcVrPressed( R, vr::k_EButton_A ) );
	RcVrButton( RCVB_DODGE, RcVrPressed( L, vr::k_EButton_Grip ) && !RcVrPressed( R, vr::k_EButton_Grip ) );
	RcVrButton( RCVB_SPRINT, RcVrPressed( L, vr::k_EButton_SteamVR_Touchpad ) );
	RcVrButton( RCVB_INVENTORY, RcVrPressed( L, vr::k_EButton_A ) );
	RcVrButton( RCVB_JUMP, R.state.rAxis[ 0 ].y > 0.75f );
	RcVrButton( RCVB_DUCK, R.state.rAxis[ 0 ].y < -0.75f );

	// B: the next quick slot
	if ( RcVrPressed( R, vr::k_EButton_ApplicationMenu ) && !RcVrWasPressed( R, vr::k_EButton_ApplicationMenu ) )
	{
		s_iQuickSlot = s_iQuickSlot % 3 + 1;
		char sz[ 32 ];
		_snprintf( sz, sizeof( sz ), "quicksel %d\n", s_iQuickSlot );
		sz[ sizeof( sz ) - 1 ] = 0;
		gEngfuncs.pfnClientCmd( sz );
	}
	// right stick click: the weapon's other function
	if ( RcVrPressed( R, vr::k_EButton_SteamVR_Touchpad ) && !RcVrWasPressed( R, vr::k_EButton_SteamVR_Touchpad ) )
		gEngfuncs.pfnClientCmd( (char *)"weapontoggle\n" );

	// turning: snap (rc_vr_snapturn degrees) or smooth (rc_vr_snapturn 0, rc_vr_turnspeed degrees a second)
	float tx = R.state.rAxis[ 0 ].x;
	if ( rc_vr_snapturn->value > 0 )
	{
		if ( fabsf( tx ) > 0.7f && !s_bTurnLatch )
		{
			s_bTurnLatch = true;
			s_flTurnYaw -= tx > 0 ? rc_vr_snapturn->value : -rc_vr_snapturn->value;
		}
		else if ( fabsf( tx ) < 0.3f )
			s_bTurnLatch = false;
	}
	else if ( fabsf( tx ) > 0.15f )
		s_flTurnYaw -= tx * rc_vr_turnspeed->value * frametime;

	// the hands and the head for the aim, from the last view's game eye
	if ( s_bOwnView )
		RcVrUpdateBody( s_rgGameEye, s_flTurnYaw, true );

	// aim: the server shoots from the eye, so it is aimed at the point the right hand points at
	float aim[ 3 ] = { ang[ 0 ], ang[ 1 ], 0 };
	s_bAimPoint = false;
	if ( !bAim )
	{
		// the wheelchair or a dead player: the game's own angles
	}
	else if ( R.valid && s_bOwnView )
	{
		float end[ 3 ];
		for ( int i = 0; i < 3; i++ )
			end[ i ] = R.pos[ i ] + R.fwd[ i ] * 8192;
		pmtrace_t *tr = gEngfuncs.PM_TraceLine( R.pos, end, PM_TRACELINE_PHYSENTSONLY, 2, -1 );
		float hit[ 3 ];
		VectorCopy( tr->endpos, hit );
		VectorCopy( hit, s_rgAimPoint );
		s_bAimPoint = tr->fraction < 1;
		float d[ 3 ] = { hit[ 0 ] - s_rgGameEye[ 0 ], hit[ 1 ] - s_rgGameEye[ 1 ], hit[ 2 ] - s_rgGameEye[ 2 ] };
		float len = sqrtf( d[ 0 ] * d[ 0 ] + d[ 1 ] * d[ 1 ] + d[ 2 ] * d[ 2 ] );
		if ( len < 32 )
		{
			// too close to converge: the hand's own direction
			d[ 0 ] = R.fwd[ 0 ]; d[ 1 ] = R.fwd[ 1 ]; d[ 2 ] = R.fwd[ 2 ];
		}
		float xy = sqrtf( d[ 0 ] * d[ 0 ] + d[ 1 ] * d[ 1 ] );
		aim[ 0 ] = -atan2f( d[ 2 ], xy ) * 180 / RCVR_PI;
		aim[ 1 ] = atan2f( d[ 1 ], d[ 0 ] ) * 180 / RCVR_PI;
	}
	else if ( RcVrHmdValid() && s_bOwnView )
	{
		// no right hand: aim with the head
		aim[ 1 ] = s_flTurnYaw + RcVrHmdYaw();
	}

	// movement: the left stick, relative to the head (or the left hand), turned into the aim's frame
	float mx = L.state.rAxis[ 0 ].x, my = L.state.rAxis[ 0 ].y;
	float mag = sqrtf( mx * mx + my * my );
	if ( mag > 0.15f )
	{
		if ( mag > 1 )
		{
			mx /= mag; my /= mag; mag = 1;
		}
		float yaw = s_flTurnYaw + ( RcVrHmdValid() ? RcVrHmdYaw() : 0 );
		if ( rc_vr_movehand->value && L.valid )
			yaw = atan2f( L.fwd[ 1 ], L.fwd[ 0 ] ) * 180 / RCVR_PI;
		float wf[ 3 ] = { 1, 0, 0 }, wr[ 3 ] = { 0, -1, 0 };
		RcVrRotZ( wf, yaw );
		RcVrRotZ( wr, yaw );
		float w[ 2 ] = { wf[ 0 ] * my + wr[ 0 ] * mx, wf[ 1 ] * my + wr[ 1 ] * mx };
		float af[ 3 ] = { 1, 0, 0 }, ar[ 3 ] = { 0, -1, 0 };
		RcVrRotZ( af, aim[ 1 ] );
		RcVrRotZ( ar, aim[ 1 ] );
		float speed = 400;
		cvar_t *pFwd = gEngfuncs.pfnGetCvarPointer( "cl_forwardspeed" );
		if ( pFwd && pFwd->value > 0 )
			speed = pFwd->value;
		cmd->forwardmove += ( w[ 0 ] * af[ 0 ] + w[ 1 ] * af[ 1 ] ) * speed;
		cmd->sidemove += ( w[ 0 ] * ar[ 0 ] + w[ 1 ] * ar[ 1 ] ) * speed;
	}

	if ( !bAim )
	{
		s_bAnglesSet = false;
		return;
	}
	gEngfuncs.SetViewAngles( aim );
	VectorCopy( aim, cmd->viewangles );
	VectorCopy( aim, s_rgLastSetAngles );
	s_bAnglesSet = true;
}

// ------------------------------------------------------------------------------------------------------------
// the swap: the 2D on a screen, the pointer, the submit, the window

static void RcVrScreenTransform( float m[ 3 ][ 4 ], bool bMenu )
{
	// the HUD hangs before the head; a menu stays where it was put
	const vr::HmdMatrix34_t &hmd = s_rgPoses[ vr::k_unTrackedDeviceIndex_Hmd ].mDeviceToAbsoluteTracking;
	if ( bMenu )
	{
		if ( !s_bScreenPlaced )
		{
			// before the head, level, at the head's height
			float f[ 3 ] = { -hmd.m[ 0 ][ 2 ], 0, -hmd.m[ 2 ][ 2 ] };
			float len = sqrtf( f[ 0 ] * f[ 0 ] + f[ 2 ] * f[ 2 ] );
			if ( len < 0.01f )
			{
				f[ 0 ] = 0; f[ 2 ] = -1; len = 1;
			}
			f[ 0 ] /= len; f[ 2 ] /= len;
			float d = rc_vr_menu_dist->value;
			// columns: right, up, back (the screen faces the head)
			s_rgScreenPose[ 0 ][ 0 ] = -f[ 2 ]; s_rgScreenPose[ 1 ][ 0 ] = 0; s_rgScreenPose[ 2 ][ 0 ] = f[ 0 ];
			s_rgScreenPose[ 0 ][ 1 ] = 0; s_rgScreenPose[ 1 ][ 1 ] = 1; s_rgScreenPose[ 2 ][ 1 ] = 0;
			s_rgScreenPose[ 0 ][ 2 ] = -f[ 0 ]; s_rgScreenPose[ 1 ][ 2 ] = 0; s_rgScreenPose[ 2 ][ 2 ] = -f[ 2 ];
			s_rgScreenPose[ 0 ][ 3 ] = hmd.m[ 0 ][ 3 ] + f[ 0 ] * d;
			s_rgScreenPose[ 1 ][ 3 ] = hmd.m[ 1 ][ 3 ];
			s_rgScreenPose[ 2 ][ 3 ] = hmd.m[ 2 ][ 3 ] + f[ 2 ] * d;
			s_bScreenPlaced = true;
		}
		memcpy( m, s_rgScreenPose, sizeof( s_rgScreenPose ) );
		return;
	}
	s_bScreenPlaced = false;
	float d = rc_vr_hud_dist->value;
	for ( int i = 0; i < 3; i++ )
	{
		for ( int j = 0; j < 3; j++ )
			m[ i ][ j ] = hmd.m[ i ][ j ];
		m[ i ][ 3 ] = hmd.m[ i ][ 3 ] - hmd.m[ i ][ 2 ] * d;
	}
}

// the right hand's ray against the screen: u, v from its bottom left corner
static bool RcVrPointer( float m[ 3 ][ 4 ], float w, float h )
{
	RcVrHand_t &R = s_rgHand[ 1 ];
	if ( !R.valid )
		return false;
	float n[ 3 ] = { m[ 0 ][ 2 ], m[ 1 ][ 2 ], m[ 2 ][ 2 ] };
	float c[ 3 ] = { m[ 0 ][ 3 ], m[ 1 ][ 3 ], m[ 2 ][ 3 ] };
	float denom = n[ 0 ] * R.tfwd[ 0 ] + n[ 1 ] * R.tfwd[ 1 ] + n[ 2 ] * R.tfwd[ 2 ];
	if ( fabsf( denom ) < 1e-4f )
		return false;
	float t = ( ( c[ 0 ] - R.tpos[ 0 ] ) * n[ 0 ] + ( c[ 1 ] - R.tpos[ 1 ] ) * n[ 1 ] + ( c[ 2 ] - R.tpos[ 2 ] ) * n[ 2 ] ) / denom;
	if ( t < 0 )
		return false;
	float p[ 3 ], q[ 3 ];
	for ( int i = 0; i < 3; i++ )
	{
		p[ i ] = R.tpos[ i ] + R.tfwd[ i ] * t;
		q[ i ] = p[ i ] - c[ i ];
	}
	float x = q[ 0 ] * m[ 0 ][ 0 ] + q[ 1 ] * m[ 1 ][ 0 ] + q[ 2 ] * m[ 2 ][ 0 ];
	float y = q[ 0 ] * m[ 0 ][ 1 ] + q[ 1 ] * m[ 1 ][ 1 ] + q[ 2 ] * m[ 2 ][ 1 ];
	float u = x / w + 0.5f, v = y / h + 0.5f;
	if ( u < 0 || u > 1 || v < 0 || v > 1 )
		return false;
	s_flPointerU = u;
	s_flPointerV = v;
	memcpy( s_rgPointerHit, p, sizeof( p ) );
	return true;
}

static void RcVrDrawScreen( float m[ 3 ][ 4 ], float w, float h, bool bOpaque )
{
	float hw = w * 0.5f, hh = h * 0.5f;
	float corner[ 4 ][ 3 ];
	static const float s_rgSign[ 4 ][ 2 ] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
	for ( int k = 0; k < 4; k++ )
		for ( int i = 0; i < 3; i++ )
			corner[ k ][ i ] = m[ i ][ 3 ] + m[ i ][ 0 ] * hw * s_rgSign[ k ][ 0 ] + m[ i ][ 1 ] * hh * s_rgSign[ k ][ 1 ];

	gl.glEnable( GL_TEXTURE_2D );
	gl.glBindTexture( GL_TEXTURE_2D, s_texScreen );
	gl.glEnable( GL_BLEND );
	if ( s_iProgram )
	{
		rcgl.glUseProgram( s_iProgram );
		rcgl.glUniform1i( s_iProgTex, 0 );
		rcgl.glUniform1f( s_iProgOpaque, bOpaque ? 1.0f : 0.0f );
		rcgl.glUniform1f( s_iProgFade, 1.0f );
		gl.glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	}
	else if ( bOpaque )
		gl.glDisable( GL_BLEND );
	else
		gl.glBlendFunc( GL_ONE, GL_ONE );
	gl.glColor4f( 1, 1, 1, 1 );
	gl.glBegin( GL_QUADS );
	gl.glTexCoord2f( 0, 0 ); gl.glVertex3f( corner[ 0 ][ 0 ], corner[ 0 ][ 1 ], corner[ 0 ][ 2 ] );
	gl.glTexCoord2f( 1, 0 ); gl.glVertex3f( corner[ 1 ][ 0 ], corner[ 1 ][ 1 ], corner[ 1 ][ 2 ] );
	gl.glTexCoord2f( 1, 1 ); gl.glVertex3f( corner[ 2 ][ 0 ], corner[ 2 ][ 1 ], corner[ 2 ][ 2 ] );
	gl.glTexCoord2f( 0, 1 ); gl.glVertex3f( corner[ 3 ][ 0 ], corner[ 3 ][ 1 ], corner[ 3 ][ 2 ] );
	gl.glEnd();
	if ( s_iProgram )
		rcgl.glUseProgram( 0 );
	gl.glDisable( GL_TEXTURE_2D );
}

static void RcVrDrawHandMarker( const RcVrHand_t &hand, float len )
{
	gl.glBegin( GL_LINES );
	gl.glVertex3f( hand.tpos[ 0 ], hand.tpos[ 1 ], hand.tpos[ 2 ] );
	gl.glVertex3f( hand.tpos[ 0 ] + hand.tfwd[ 0 ] * len, hand.tpos[ 1 ] + hand.tfwd[ 1 ] * len, hand.tpos[ 2 ] + hand.tfwd[ 2 ] * len );
	gl.glEnd();
}

static void RcVrDrawOverlays( int e, float screen[ 3 ][ 4 ], float sw, float sh, bool bMenu, bool bWorld )
{
	s_gl.BindFramebuffer( GL_FRAMEBUFFER, s_rgFbo[ e ] );
	gl.glViewport( 0, 0, s_iEyeW, s_iEyeH );

	if ( !bWorld )
	{
		// no level: a dark room
		gl.glClearColor( 0.02f, 0.02f, 0.025f, 1 );
		gl.glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	}

	const float n = 0.05f, f = 100.0f;
	gl.glMatrixMode( GL_PROJECTION );
	gl.glLoadIdentity();
	gl.glFrustum( s_rgTan[ e ][ 0 ] * n, s_rgTan[ e ][ 1 ] * n, s_rgTan[ e ][ 2 ] * n, s_rgTan[ e ][ 3 ] * n, n, f );
	gl.glMatrixMode( GL_MODELVIEW );
	vr::HmdMatrix34_t eye;
	RcVrMul34( s_rgPoses[ vr::k_unTrackedDeviceIndex_Hmd ].mDeviceToAbsoluteTracking, s_rgEyeToHead[ e ], eye );
	float view[ 16 ];
	RcVrViewMatrix( eye, view );
	gl.glLoadMatrixf( view );

	gl.glDisable( GL_DEPTH_TEST );
	gl.glDisable( GL_CULL_FACE );
	gl.glDisable( GL_ALPHA_TEST );
	gl.glDisable( GL_FOG );

	RcVrDrawScreen( screen, sw, sh, bMenu );

	gl.glDisable( GL_TEXTURE_2D );
	gl.glEnable( GL_BLEND );
	gl.glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

	// the pointer on the screen, else the hands in a menu
	if ( s_bPointerOnScreen && s_rgHand[ 1 ].valid )
	{
		s_gl.LineWidth( 3 );
		gl.glColor4f( 0.9f, 0.15f, 0.1f, 0.8f );
		gl.glBegin( GL_LINES );
		gl.glVertex3f( s_rgHand[ 1 ].tpos[ 0 ], s_rgHand[ 1 ].tpos[ 1 ], s_rgHand[ 1 ].tpos[ 2 ] );
		gl.glVertex3f( s_rgPointerHit[ 0 ], s_rgPointerHit[ 1 ], s_rgPointerHit[ 2 ] );
		gl.glEnd();
		s_gl.PointSize( 9 );
		gl.glColor4f( 1, 0.3f, 0.2f, 1 );
		gl.glBegin( GL_POINTS );
		gl.glVertex3f( s_rgPointerHit[ 0 ], s_rgPointerHit[ 1 ], s_rgPointerHit[ 2 ] );
		gl.glEnd();
	}
	else if ( !bWorld )
	{
		s_gl.LineWidth( 3 );
		gl.glColor4f( 0.8f, 0.8f, 0.8f, 0.7f );
		for ( int h = 0; h < 2; h++ )
		{
			if ( s_rgHand[ h ].valid )
				RcVrDrawHandMarker( s_rgHand[ h ], 0.12f );
		}
	}

	// where the gun points, a small dot in the world
	if ( bWorld && !bMenu && s_bAimPoint && s_bOwnView && !s_bCursorMode )
	{
		float t[ 3 ], g[ 3 ] = { s_rgAimPoint[ 0 ] - s_rgHeadGame[ 0 ], s_rgAimPoint[ 1 ] - s_rgHeadGame[ 1 ], s_rgAimPoint[ 2 ] - s_rgHeadGame[ 2 ] };
		RcVrDirToTracking( g, t, s_flBaseYaw );
		const vr::HmdMatrix34_t &hmd = s_rgPoses[ vr::k_unTrackedDeviceIndex_Hmd ].mDeviceToAbsoluteTracking;
		float scale = rc_vr_scale->value;
		float dist = sqrtf( t[ 0 ] * t[ 0 ] + t[ 1 ] * t[ 1 ] + t[ 2 ] * t[ 2 ] ) / scale;
		s_gl.PointSize( dist > 6 ? 3.0f : 5.0f );
		gl.glColor4f( 0.95f, 0.1f, 0.05f, 0.85f );
		gl.glBegin( GL_POINTS );
		gl.glVertex3f( hmd.m[ 0 ][ 3 ] + t[ 0 ] / scale, hmd.m[ 1 ][ 3 ] + t[ 1 ] / scale, hmd.m[ 2 ][ 3 ] + t[ 2 ] / scale );
		gl.glEnd();
	}
}

static void RcVrMirror( void )
{
	// the left eye to the window, cropped to the window's shape, and the 2D over it
	int w = ScreenWidth, h = ScreenHeight;
	gl.glViewport( 0, 0, w, h );
	gl.glMatrixMode( GL_PROJECTION );
	gl.glLoadIdentity();
	gl.glOrtho( 0, 1, 0, 1, -1, 1 );
	gl.glMatrixMode( GL_MODELVIEW );
	gl.glLoadIdentity();
	gl.glDisable( GL_BLEND );
	gl.glEnable( GL_TEXTURE_2D );
	gl.glBindTexture( GL_TEXTURE_2D, s_rgEyeTex[ 0 ] );
	float aspW = (float)w / h, aspE = (float)s_iEyeW / s_iEyeH;
	float u0 = 0, u1 = 1, v0 = 0, v1 = 1;
	if ( aspW > aspE )
	{
		float k = aspE / aspW;
		v0 = 0.5f - k * 0.5f; v1 = 0.5f + k * 0.5f;
	}
	else
	{
		float k = aspW / aspE;
		u0 = 0.5f - k * 0.5f; u1 = 0.5f + k * 0.5f;
	}
	gl.glColor4f( 1, 1, 1, 1 );
	gl.glBegin( GL_QUADS );
	gl.glTexCoord2f( u0, v0 ); gl.glVertex3f( 0, 0, 0 );
	gl.glTexCoord2f( u1, v0 ); gl.glVertex3f( 1, 0, 0 );
	gl.glTexCoord2f( u1, v1 ); gl.glVertex3f( 1, 1, 0 );
	gl.glTexCoord2f( u0, v1 ); gl.glVertex3f( 0, 1, 0 );
	gl.glEnd();
	gl.glDisable( GL_TEXTURE_2D );
}

static void __stdcall RcVrSwap( HDC hDC )
{
	if ( !s_bActive )
		return;
	if ( !s_hWnd )
		s_hWnd = WindowFromDC( hDC );
	if ( !RcVrMakeGL() )
		return;

	RcVrUnbindEye();

	// GL state the engine and the client expect back
	GLint iTex = 0, iActive = GL_TEXTURE0, iProgram = 0;
	gl.glGetIntegerv( GL_TEXTURE_BINDING_2D, &iTex );
	gl.glGetIntegerv( GL_ACTIVE_TEXTURE, &iActive );
	if ( s_iProgram )
		gl.glGetIntegerv( GL_CURRENT_PROGRAM, &iProgram );
	gl.glPushAttrib( GL_ALL_ATTRIB_BITS );
	if ( s_gl.ActiveTexture )
	{
		// the other texture units off for the overlays (glPopAttrib puts their enables back)
		for ( int u = 3; u >= 1; u-- )
		{
			s_gl.ActiveTexture( GL_TEXTURE0 + u );
			gl.glDisable( GL_TEXTURE_2D );
		}
		s_gl.ActiveTexture( GL_TEXTURE0 );
	}
	gl.glMatrixMode( GL_TEXTURE );
	gl.glPushMatrix();
	gl.glLoadIdentity();
	gl.glMatrixMode( GL_PROJECTION );
	gl.glPushMatrix();
	gl.glMatrixMode( GL_MODELVIEW );
	gl.glPushMatrix();
	if ( s_iProgram )
		rcgl.glUseProgram( 0 );
	gl.glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	gl.glDisable( GL_SCISSOR_TEST );
	gl.glDisable( GL_STENCIL_TEST );
	gl.glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	gl.glDepthMask( GL_TRUE );

	// the window's 2D into the screen texture
	int w = ScreenWidth, h = ScreenHeight;
	gl.glBindTexture( GL_TEXTURE_2D, s_texScreen );
	if ( w != s_iScreenW || h != s_iScreenH )
	{
		gl.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		gl.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		gl.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		gl.glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		gl.glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
		s_iScreenW = w;
		s_iScreenH = h;
	}
	gl.glReadBuffer( GL_BACK );
	gl.glCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h );

	bool bWorld = s_bEyesDrawn;
	bool bMenu = !bWorld || s_bCursorMode;
	if ( !bWorld )
		s_bCursorMode = true;

	if ( RcVrHmdValid() )
	{
		float screen[ 3 ][ 4 ];
		float sw = bMenu ? rc_vr_menu_width->value : rc_vr_hud_width->value;
		float sh = sw * h / ( w > 0 ? w : 1 );
		RcVrScreenTransform( screen, bMenu );

		// the hands for the menu pointer (no view ran without a level)
		if ( !bWorld )
		{
			const vr::HmdMatrix34_t &hmd = s_rgPoses[ vr::k_unTrackedDeviceIndex_Hmd ].mDeviceToAbsoluteTracking;
			float head[ 3 ] = { hmd.m[ 0 ][ 3 ], hmd.m[ 1 ][ 3 ], hmd.m[ 2 ][ 3 ] };
			float zero[ 3 ] = { 0, 0, 0 };
			RcVrUpdateHand( 0, zero, head, 0 );
			RcVrUpdateHand( 1, zero, head, 0 );
		}

		s_bPointerOnScreen = s_bCursorMode && RcVrPointer( screen, sw, sh );
		if ( s_bPointerOnScreen && s_hWnd && GetForegroundWindow() == s_hWnd )
		{
			RECT rc;
			GetClientRect( s_hWnd, &rc );
			POINT pt = { (LONG)( s_flPointerU * ( rc.right - rc.left ) ), (LONG)( ( 1 - s_flPointerV ) * ( rc.bottom - rc.top ) ) };
			ClientToScreen( s_hWnd, &pt );
			SetCursorPos( pt.x, pt.y );
		}

		// no level (the main menu, a load): CL_CreateMove does not run, the pointer clicks from here
		if ( !s_bInputThisFrame )
		{
			RcVrReadControllers();
			RcVrPointerInput();
		}

		for ( int e = 0; e < 2; e++ )
			RcVrDrawOverlays( e, screen, sw, sh, bMenu, bWorld );
		s_gl.BindFramebuffer( GL_FRAMEBUFFER, 0 );

		for ( int e = 0; e < 2; e++ )
		{
			vr::Texture_t tex = { (void *)(uintptr_t)s_rgEyeTex[ e ], vr::TextureType_OpenGL, vr::ColorSpace_Gamma };
			vr::EVRCompositorError err = s_pComp->Submit( e ? vr::Eye_Right : vr::Eye_Left, &tex );
			if ( err != vr::VRCompositorError_None && rc_vr_debug->value )
				gEngfuncs.Con_Printf( "ReCoF VR: submit %d failed (%d)\n", e, (int)err );
		}
	}

	// the window: the left eye with the 2D over it
	if ( rc_vr_mirror->value )
	{
		RcVrMirror();
		gl.glEnable( GL_TEXTURE_2D );
		float m[ 3 ][ 4 ] = { { 1, 0, 0, 0.5f }, { 0, 1, 0, 0.5f }, { 0, 0, 1, 0 } };
		RcVrDrawScreen( m, 1, 1, !bWorld );
	}

	gl.glMatrixMode( GL_TEXTURE );
	gl.glPopMatrix();
	gl.glMatrixMode( GL_PROJECTION );
	gl.glPopMatrix();
	gl.glMatrixMode( GL_MODELVIEW );
	gl.glPopMatrix();
	gl.glPopAttrib();
	if ( s_iProgram )
		rcgl.glUseProgram( iProgram );
	gl.glBindTexture( GL_TEXTURE_2D, iTex );
	if ( s_gl.ActiveTexture )
		s_gl.ActiveTexture( iActive );

	s_bEyesDrawn = false;
	s_bInputThisFrame = false;

	// the next frame's poses (this waits for the headset)
	s_pComp->PostPresentHandoff();
	s_bPoses = s_pComp->WaitGetPoses( s_rgPoses, vr::k_unMaxTrackedDeviceCount, NULL, 0 ) == vr::VRCompositorError_None;

	// a headset game cannot wait 20 ms a frame because the desktop window lost the focus (hw.dll CEngine::Frame)
	static float s_flLastFocus;
	if ( s_hWnd && GetForegroundWindow() != s_hWnd )
	{
		float t = gEngfuncs.GetClientTime();
		if ( t - s_flLastFocus > 2 || t < s_flLastFocus )
		{
			s_flLastFocus = t;
			keybd_event( VK_MENU, 0, 0, 0 );
			keybd_event( VK_MENU, 0, KEYEVENTF_KEYUP, 0 );
			SetForegroundWindow( s_hWnd );
		}
	}
}
