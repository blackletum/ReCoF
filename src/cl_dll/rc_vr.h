// ReCoF VR (rc_vr.cpp): Cry of Fear in a headset through OpenVR (SteamVR), only with the -vr launch option.
//
// Without -vr nothing here runs and openvr_api.dll is never loaded, so flat play is unchanged. With it, the engine's
// own pass loop (hw.dll V_RenderView repeats V_CalcRefdef and R_RenderView while ref_params.nextView is set, the
// loop Cry of Fear's head shield already uses) draws each eye into its own framebuffer; opengl32.dll (the wrapper)
// gives each eye the headset's projection and viewport; at the buffer swap the HUD and the menus go on a screen in
// front of the player, both eyes go to the compositor and the left eye goes to the desktop window.

#ifndef RC_VR_H
#define RC_VR_H

struct ref_params_s;
struct usercmd_s;
struct cl_entity_s;

// at client start (rc_client.cpp): -vr or -openvr starts OpenVR; false if not asked for or if it failed
bool	RcVR_Init( void );

// OpenVR is running: the VR paths are on
bool	RcVR_Active( void );

// the second eye's passes draw what the first eye drew: once-a-frame work is skipped (RcSmooth_RenderOnly)
bool	RcVR_SecondEye( void );

// view.cpp V_CalcRefdef in VR: the four passes (left eye, its head shield, right eye, its head shield); pfnGame
// computes the game's view once a frame (V_CalcNormalRefdef and the rest), bOwnView says whether it is the
// player's own first person view; pfnPassEnd ends each pass (the render origin, the client renderer's setup)
void	RcVR_CalcRefdef( struct ref_params_s *pparams, void ( *pfnGame )( struct ref_params_s * ), int ( *pfnOwnView )( struct ref_params_s * ),
	void ( *pfnPassEnd )( struct ref_params_s * ) );

// hud_redraw.cpp, first thing in HUD_Redraw: the eyes are drawn, the 2D goes to the window
void	RcVR_HudRedrawStart( void );

// input.cpp CL_CreateMove, at the end: the controllers' movement, buttons and aim
void	RcVR_CreateMove( float frametime, struct usercmd_s *cmd, int active, int bAim );

// hud_update.cpp: the field of view the engine culls with
float	RcVR_CullFov( void );

// gl_renderer.cpp PrepareRenderer: the client renderer's frustum (viewsize argument)
float	RcVR_CullViewsize( void );

// StudioModelRenderer.cpp, after a model's bones are set up: the weapon model's wrist goes to the controller
void	RcVR_AdjustBones( struct cl_entity_s *ent, void *pStudioHeader, float ( *bonetransform )[ 128 ][ 3 ][ 4 ], float ( *lighttransform )[ 128 ][ 3 ][ 4 ] );

#endif // RC_VR_H
