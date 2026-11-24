// ReCoF story co-op (B393): the main menu's "Co-op (Beta)" entry (B497: "Story Co-op" before; B541: "(Beta)") and its panel. The entry's
// picture (Cry of Fear's picture menu, the default) is ReCoF's own, made in the look of the menu's other labels (280 x 38, white
// text on a faint streak, red under the cursor) and kept here as an alpha mask, so no file is added to the game.
// B406: the panel is the group's lobby (a Steam lobby, rc_steamnet.cpp RcLobby_*), shown before anyone enters the
// game: the players in it, the difficulty and the game the host picks (a new game or the last co-op save), Invite
// friends, Leave and Start. When the host starts, the guests' games join by themselves.
// B498: the lobby hosts every co-op game: story co-op, and Cry of Fear's own co-op games (the co-op campaign,
// Manhunt, the survival maps and any multiplayer map of Host Server's list) with their own settings; Server list
// opens the server browser that the main menu's Join Server opened

#include "hud.h"
#include "cl_util.h"
#include "vgui_TeamFortressViewport.h"
#include "GameMenu_guess.h"
#include "..\game_shared\vgui_loadtga.h"
#include "rc_vgui1.h"
#include "rc_ui.h"	// B406
#include "rc_steamnet.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>

void IrrKlangStop_guess( float flFadeTime );

// the "Co-op (Beta)" label (B497, B541): 280 x 38 alpha values, top row first, run-length coded as (count, value) byte
// pairs. B541: the streak is hserver.tga's with its text taken out, the text Arial at 25.4 px, the size that matches
// the game's own "Host Server"
static const unsigned char s_ucRcCoopLabel[] =
{
	255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 221, 0, 1, 1, 1, 2, 10, 3, 1, 4,
	1, 5, 12, 6, 1, 21, 1, 114, 1, 190, 1, 234, 1, 251, 1, 241, 1, 203, 1, 129, 1, 27, 24, 6,
	1, 5, 2, 3, 1, 4, 1, 6, 1, 7, 1, 9, 1, 10, 4, 11, 5, 10, 27, 9, 1, 136, 1, 249,
	1, 46, 1, 9, 1, 51, 6, 255, 1, 253, 1, 237, 1, 207, 1, 135, 1, 26, 1, 9, 1, 8, 1, 7,
	1, 6, 1, 4, 1, 3, 1, 1, 13, 0, 1, 6, 1, 131, 1, 55, 17, 0, 1, 52, 1, 254, 1, 114,
	118, 0, 1, 2, 1, 5, 10, 6, 1, 8, 12, 11, 1, 88, 1, 239, 1, 255, 1, 254, 1, 232, 1, 202,
	1, 216, 1, 253, 1, 255, 1, 238, 1, 84, 4, 11, 4, 12, 15, 13, 1, 11, 1, 10, 1, 11, 1, 13,
	1, 16, 1, 18, 1, 20, 1, 22, 1, 24, 1, 26, 2, 27, 2, 25, 3, 24, 2, 23, 24, 22, 1, 75,
	1, 252, 1, 149, 2, 20, 1, 61, 10, 255, 1, 235, 1, 57, 1, 16, 1, 14, 1, 12, 1, 10, 1, 8,
	1, 6, 1, 3, 1, 1, 11, 0, 1, 204, 1, 255, 1, 60, 18, 0, 1, 158, 1, 249, 1, 45, 117, 0,
	1, 2, 1, 5, 10, 6, 1, 8, 11, 11, 1, 125, 2, 255, 1, 159, 1, 44, 3, 11, 1, 40, 1, 180,
	1, 255, 1, 251, 1, 78, 1, 11, 2, 12, 2, 13, 2, 14, 2, 15, 13, 16, 2, 15, 1, 16, 1, 18,
	2, 19, 1, 20, 1, 21, 1, 22, 1, 24, 1, 26, 1, 27, 2, 25, 3, 24, 2, 23, 16, 22, 7, 21,
	1, 25, 1, 214, 1, 239, 1, 34, 2, 18, 1, 59, 2, 255, 1, 66, 3, 18, 1, 22, 1, 38, 1, 94,
	1, 227, 1, 255, 1, 214, 1, 13, 1, 10, 2, 9, 1, 8, 1, 7, 1, 6, 1, 4, 1, 1, 10, 0,
	1, 244, 1, 255, 1, 60, 18, 0, 1, 26, 1, 246, 1, 197, 1, 1, 116, 0, 1, 2, 1, 5, 10, 6,
	1, 8, 10, 11, 1, 69, 1, 252, 1, 255, 1, 116, 6, 11, 1, 14, 1, 189, 1, 255, 1, 232, 1, 21,
	2, 12, 2, 13, 2, 14, 2, 15, 22, 16, 1, 18, 1, 20, 1, 21, 2, 19, 5, 18, 16, 17, 7, 16,
	1, 113, 1, 255, 1, 135, 1, 13, 2, 12, 1, 54, 2, 255, 1, 62, 6, 12, 1, 66, 2, 255, 1, 70,
	6, 6, 1, 5, 1, 3, 1, 1, 9, 0, 1, 244, 1, 255, 1, 60, 19, 0, 1, 145, 1, 255, 1, 86,
	116, 0, 1, 2, 1, 5, 10, 6, 1, 8, 9, 11, 1, 12, 1, 216, 1, 255, 1, 185, 8, 11, 1, 47,
	1, 253, 1, 255, 1, 114, 1, 11, 4, 12, 3, 13, 4, 14, 3, 15, 4, 16, 13, 17, 29, 16, 1, 15,
	1, 19, 1, 228, 1, 252, 1, 40, 2, 14, 1, 13, 1, 55, 2, 255, 1, 61, 1, 11, 2, 10, 2, 9,
	2, 8, 1, 237, 1, 255, 1, 114, 2, 5, 2, 4, 2, 3, 2, 2, 2, 1, 8, 0, 1, 244, 1, 255,
	1, 60, 19, 0, 1, 42, 1, 255, 1, 213, 116, 0, 1, 2, 1, 5, 10, 6, 1, 8, 7, 11, 1, 12,
	1, 14, 1, 73, 2, 255, 1, 80, 2, 22, 2, 23, 3, 24, 2, 25, 1, 189, 1, 199, 1, 117, 1, 32,
	4, 33, 1, 61, 1, 159, 1, 221, 1, 248, 1, 245, 1, 212, 1, 141, 1, 47, 2, 36, 3, 37, 4, 38,
	3, 39, 2, 40, 1, 66, 1, 162, 1, 222, 1, 248, 1, 245, 1, 213, 1, 144, 1, 53, 3, 43, 1, 44,
	1, 120, 1, 255, 1, 183, 1, 59, 1, 168, 1, 233, 1, 248, 1, 224, 1, 158, 1, 61, 6, 42, 6, 43,
	1, 44, 1, 114, 1, 255, 1, 198, 2, 44, 2, 45, 1, 81, 2, 255, 1, 88, 6, 46, 1, 47, 1, 237,
	1, 255, 1, 129, 2, 47, 3, 48, 1, 63, 1, 156, 1, 219, 1, 247, 1, 246, 1, 215, 1, 148, 1, 58,
	2, 47, 1, 46, 1, 164, 5, 255, 1, 138, 1, 39, 2, 38, 1, 93, 1, 178, 1, 226, 1, 245, 1, 250,
	1, 233, 1, 185, 1, 92, 2, 30, 1, 29, 1, 28, 1, 27, 1, 26, 1, 25, 1, 209, 1, 255, 1, 84,
	1, 22, 4, 21, 1, 20, 1, 19, 1, 17, 1, 15, 1, 13, 1, 11, 7, 10, 1, 8, 1, 7, 1, 6,
	1, 4, 1, 2, 1, 1, 91, 0, 1, 2, 1, 5, 10, 6, 1, 8, 6, 11, 1, 13, 1, 16, 1, 18,
	1, 138, 1, 255, 1, 245, 1, 30, 1, 27, 2, 28, 2, 29, 2, 30, 1, 31, 2, 32, 2, 33, 2, 34,
	2, 35, 1, 101, 1, 245, 1, 255, 1, 249, 1, 210, 1, 216, 1, 253, 1, 255, 1, 226, 1, 73, 2, 42,
	2, 43, 2, 44, 1, 45, 2, 46, 2, 47, 1, 48, 1, 110, 1, 245, 1, 255, 1, 249, 1, 213, 1, 218,
	1, 253, 1, 255, 1, 227, 1, 83, 1, 54, 2, 55, 1, 128, 1, 255, 1, 201, 1, 224, 1, 255, 1, 229,
	1, 213, 1, 250, 1, 255, 1, 235, 1, 92, 4, 56, 8, 55, 1, 189, 1, 255, 1, 143, 3, 54, 1, 53,
	1, 88, 2, 255, 1, 93, 1, 52, 2, 51, 3, 50, 1, 90, 1, 255, 1, 253, 1, 73, 2, 48, 2, 47,
	1, 89, 1, 236, 1, 255, 1, 252, 1, 221, 1, 214, 1, 251, 1, 255, 1, 223, 1, 70, 2, 43, 1, 132,
	1, 202, 1, 253, 1, 255, 1, 215, 1, 202, 1, 113, 2, 40, 1, 158, 2, 255, 1, 234, 1, 208, 1, 221,
	1, 250, 2, 255, 1, 126, 1, 36, 3, 35, 2, 34, 1, 148, 1, 255, 1, 168, 4, 31, 1, 30, 1, 28,
	1, 26, 1, 24, 1, 22, 1, 20, 1, 18, 1, 16, 5, 14, 1, 13, 1, 11, 1, 9, 1, 7, 1, 5,
	1, 3, 1, 1, 91, 0, 1, 2, 1, 5, 1, 6, 1, 7, 1, 8, 1, 9, 1, 10, 1, 12, 1, 13,
	1, 14, 1, 15, 1, 16, 1, 18, 1, 20, 4, 21, 1, 23, 1, 25, 2, 27, 1, 178, 1, 255, 1, 209,
	1, 29, 4, 31, 8, 32, 3, 33, 1, 67, 1, 248, 1, 255, 1, 178, 1, 48, 2, 34, 1, 63, 1, 208,
	1, 255, 1, 223, 1, 43, 1, 34, 8, 35, 1, 36, 1, 69, 1, 248, 1, 255, 1, 179, 1, 51, 2, 36,
	1, 66, 1, 209, 1, 255, 1, 223, 1, 46, 2, 37, 1, 116, 2, 255, 1, 247, 1, 108, 1, 40, 1, 42,
	1, 62, 1, 202, 1, 255, 1, 220, 1, 43, 3, 39, 4, 38, 3, 37, 1, 39, 1, 240, 1, 255, 1, 81,
	2, 36, 2, 35, 1, 73, 2, 255, 1, 79, 3, 34, 1, 36, 1, 52, 1, 107, 1, 230, 1, 255, 1, 149,
	3, 32, 1, 31, 1, 49, 1, 233, 1, 255, 1, 191, 1, 54, 2, 30, 1, 51, 1, 192, 1, 255, 1, 212,
	1, 32, 3, 28, 1, 245, 1, 255, 1, 81, 3, 27, 1, 87, 1, 255, 1, 251, 1, 105, 1, 26, 2, 25,
	1, 48, 1, 195, 1, 255, 1, 236, 1, 26, 1, 24, 4, 23, 1, 87, 1, 255, 1, 225, 3, 20, 1, 19,
	1, 17, 1, 15, 5, 14, 1, 12, 1, 10, 4, 8, 1, 7, 1, 5, 1, 3, 3, 1, 92, 0, 1, 2,
	1, 5, 1, 7, 1, 9, 1, 11, 1, 13, 1, 15, 1, 18, 1, 20, 1, 22, 1, 24, 1, 26, 1, 28,
	1, 30, 3, 31, 1, 32, 1, 34, 3, 36, 1, 197, 1, 255, 1, 193, 1, 36, 15, 38, 1, 178, 1, 255,
	1, 208, 1, 40, 4, 38, 1, 54, 1, 238, 1, 255, 1, 133, 10, 39, 1, 178, 1, 255, 1, 208, 1, 41,
	4, 39, 1, 55, 1, 238, 1, 255, 1, 134, 2, 39, 1, 117, 2, 255, 1, 123, 2, 39, 1, 40, 1, 43,
	1, 57, 1, 240, 1, 255, 1, 118, 2, 41, 3, 40, 3, 39, 2, 38, 1, 79, 1, 255, 1, 253, 1, 46,
	1, 37, 3, 36, 1, 74, 10, 255, 1, 140, 1, 33, 1, 32, 3, 31, 1, 145, 1, 255, 1, 221, 1, 39,
	3, 29, 1, 28, 1, 40, 1, 235, 1, 255, 1, 104, 2, 27, 1, 26, 1, 245, 1, 255, 1, 79, 3, 25,
	1, 130, 1, 226, 1, 170, 4, 23, 1, 22, 1, 70, 2, 255, 1, 43, 1, 21, 4, 20, 1, 44, 2, 255,
	1, 46, 2, 16, 1, 14, 1, 12, 7, 11, 1, 9, 4, 6, 1, 5, 1, 3, 1, 1, 95, 0, 1, 2,
	1, 5, 1, 6, 1, 7, 1, 8, 1, 9, 1, 10, 1, 12, 1, 13, 1, 14, 1, 15, 1, 16, 1, 18,
	1, 20, 3, 21, 1, 23, 1, 25, 3, 26, 1, 184, 1, 255, 1, 195, 1, 26, 3, 27, 5, 28, 6, 29,
	1, 32, 1, 240, 1, 255, 1, 111, 1, 30, 5, 31, 1, 155, 1, 255, 1, 199, 3, 32, 5, 33, 1, 34,
	1, 36, 1, 240, 1, 255, 1, 114, 6, 35, 1, 157, 1, 255, 1, 200, 2, 36, 1, 116, 1, 255, 1, 239,
	1, 40, 3, 37, 1, 40, 1, 41, 1, 166, 1, 255, 1, 184, 4, 39, 4, 40, 2, 41, 1, 110, 1, 255,
	1, 231, 4, 42, 1, 43, 1, 80, 10, 255, 1, 239, 1, 139, 2, 46, 2, 47, 1, 220, 1, 255, 1, 148,
	1, 45, 1, 44, 2, 43, 1, 42, 1, 41, 1, 172, 1, 255, 1, 176, 1, 38, 1, 37, 1, 36, 1, 246,
	1, 255, 1, 86, 1, 34, 1, 33, 1, 32, 1, 31, 2, 30, 1, 29, 2, 28, 1, 27, 1, 26, 1, 43,
	2, 255, 1, 54, 2, 22, 1, 21, 2, 20, 1, 19, 1, 247, 1, 255, 1, 76, 1, 15, 1, 14, 1, 12,
	8, 10, 1, 9, 1, 7, 3, 5, 1, 4, 1, 2, 96, 0, 1, 2, 1, 5, 10, 6, 1, 8, 4, 11,
	1, 13, 1, 15, 3, 16, 1, 158, 1, 255, 1, 211, 2, 16, 1, 18, 1, 19, 1, 20, 1, 22, 1, 24,
	1, 25, 1, 26, 1, 28, 1, 30, 1, 32, 2, 33, 1, 34, 1, 56, 2, 255, 1, 71, 2, 36, 2, 37,
	2, 38, 1, 114, 1, 255, 1, 235, 1, 39, 1, 83, 6, 255, 1, 158, 1, 62, 2, 255, 1, 75, 5, 42,
	1, 43, 1, 117, 1, 255, 1, 235, 2, 43, 1, 120, 1, 255, 1, 197, 1, 45, 1, 47, 1, 49, 1, 50,
	1, 52, 1, 55, 1, 135, 1, 255, 1, 218, 2, 51, 2, 52, 1, 53, 2, 54, 1, 55, 2, 56, 1, 137,
	1, 255, 1, 220, 2, 59, 2, 60, 1, 61, 1, 95, 2, 255, 1, 102, 1, 64, 2, 65, 1, 67, 1, 77,
	1, 105, 1, 177, 2, 255, 1, 161, 1, 70, 1, 71, 1, 72, 1, 254, 1, 255, 1, 225, 2, 209, 4, 208,
	1, 230, 1, 255, 1, 214, 1, 59, 1, 58, 1, 56, 1, 246, 1, 255, 1, 101, 1, 52, 1, 51, 1, 50,
	1, 49, 1, 48, 1, 46, 1, 52, 1, 79, 1, 107, 1, 137, 1, 176, 1, 228, 2, 255, 1, 67, 1, 35,
	1, 34, 1, 33, 2, 32, 1, 31, 1, 234, 1, 255, 1, 104, 1, 27, 1, 26, 1, 24, 1, 22, 1, 20,
	1, 19, 1, 17, 1, 16, 1, 14, 1, 12, 1, 11, 1, 9, 1, 7, 3, 5, 1, 4, 1, 2, 96, 0,
	1, 2, 1, 5, 10, 6, 1, 8, 4, 11, 1, 13, 1, 15, 3, 16, 1, 121, 1, 255, 1, 246, 1, 22,
	1, 16, 1, 17, 1, 18, 1, 19, 1, 20, 1, 21, 1, 22, 1, 23, 1, 24, 1, 26, 1, 28, 1, 31,
	2, 32, 1, 67, 2, 255, 1, 58, 2, 36, 2, 37, 1, 38, 1, 39, 1, 103, 1, 255, 1, 243, 1, 40,
	1, 84, 6, 255, 1, 159, 1, 75, 2, 255, 1, 65, 5, 43, 1, 44, 1, 107, 1, 255, 1, 243, 2, 44,
	1, 121, 1, 255, 1, 183, 1, 47, 1, 49, 1, 51, 1, 53, 2, 55, 1, 127, 1, 255, 1, 232, 4, 56,
	2, 57, 2, 58, 1, 59, 1, 60, 1, 152, 1, 255, 1, 213, 1, 62, 2, 63, 1, 64, 1, 65, 1, 98,
	2, 255, 1, 105, 2, 68, 2, 69, 1, 70, 2, 71, 1, 174, 1, 255, 1, 249, 1, 88, 1, 74, 1, 86,
	11, 255, 1, 228, 1, 65, 1, 64, 1, 63, 1, 247, 1, 255, 1, 105, 1, 58, 1, 56, 1, 55, 1, 54,
	1, 99, 1, 191, 1, 252, 4, 255, 1, 237, 2, 255, 1, 70, 1, 39, 1, 37, 3, 36, 1, 34, 1, 224,
	1, 255, 1, 122, 1, 32, 1, 30, 1, 28, 1, 26, 1, 24, 1, 22, 1, 20, 1, 18, 1, 15, 1, 13,
	1, 11, 1, 9, 1, 7, 3, 5, 1, 4, 1, 2, 96, 0, 1, 2, 1, 5, 10, 6, 1, 8, 4, 11,
	1, 13, 1, 15, 3, 16, 1, 49, 2, 255, 1, 76, 1, 16, 2, 17, 2, 18, 1, 19, 2, 20, 1, 21,
	1, 124, 1, 207, 1, 145, 2, 25, 1, 49, 2, 255, 1, 64, 1, 29, 1, 30, 1, 31, 2, 32, 1, 33,
	1, 114, 1, 255, 1, 226, 9, 35, 1, 57, 2, 255, 1, 70, 6, 34, 1, 114, 1, 255, 1, 226, 2, 34,
	1, 114, 1, 255, 1, 187, 6, 34, 1, 126, 1, 255, 1, 210, 6, 34, 4, 33, 1, 131, 1, 255, 1, 221,
	5, 32, 1, 70, 2, 255, 1, 77, 6, 32, 1, 31, 1, 71, 2, 255, 1, 91, 1, 31, 1, 35, 2, 255,
	1, 97, 1, 34, 2, 33, 1, 32, 2, 31, 2, 30, 2, 29, 1, 28, 1, 27, 1, 245, 1, 255, 1, 80,
	1, 25, 2, 24, 1, 102, 1, 253, 1, 255, 1, 238, 1, 184, 1, 146, 1, 108, 1, 60, 1, 41, 2, 255,
	1, 51, 2, 16, 2, 15, 1, 14, 1, 12, 1, 238, 1, 255, 1, 100, 1, 10, 1, 9, 1, 7, 13, 5,
	1, 4, 1, 2, 96, 0, 1, 2, 1, 5, 10, 6, 1, 8, 4, 11, 1, 13, 1, 15, 4, 16, 1, 208,
	1, 255, 1, 186, 1, 17, 2, 18, 1, 19, 2, 20, 2, 21, 1, 24, 1, 218, 1, 255, 1, 195, 2, 22,
	1, 25, 1, 240, 1, 255, 1, 108, 4, 24, 2, 25, 1, 155, 1, 255, 1, 196, 3, 26, 5, 27, 1, 28,
	1, 30, 1, 241, 1, 255, 1, 111, 3, 29, 3, 30, 1, 157, 1, 255, 1, 197, 2, 31, 1, 112, 1, 255,
	1, 226, 4, 32, 2, 33, 1, 168, 1, 255, 1, 172, 8, 32, 2, 33, 1, 110, 1, 255, 1, 239, 3, 34,
	2, 35, 1, 73, 2, 255, 1, 81, 2, 36, 4, 35, 1, 34, 1, 72, 2, 255, 1, 96, 2, 33, 1, 226,
	1, 255, 1, 144, 2, 35, 2, 34, 2, 33, 1, 32, 2, 31, 2, 30, 1, 29, 1, 245, 1, 255, 1, 81,
	1, 27, 2, 26, 1, 231, 1, 255, 1, 182, 1, 33, 2, 22, 1, 21, 1, 20, 1, 61, 2, 255, 1, 51,
	2, 17, 2, 16, 1, 14, 1, 15, 1, 253, 1, 255, 1, 79, 1, 11, 1, 10, 1, 8, 8, 6, 5, 5,
	1, 4, 1, 2, 96, 0, 1, 2, 1, 5, 10, 6, 1, 8, 4, 11, 1, 12, 1, 14, 4, 16, 1, 86,
	2, 255, 1, 115, 1, 19, 1, 20, 1, 21, 1, 22, 1, 23, 1, 24, 1, 25, 1, 143, 2, 255, 1, 85,
	1, 26, 2, 27, 1, 175, 1, 255, 1, 206, 1, 30, 2, 28, 2, 29, 1, 46, 1, 238, 1, 255, 1, 126,
	2, 30, 3, 31, 4, 32, 1, 33, 1, 177, 1, 255, 1, 207, 1, 36, 3, 34, 1, 35, 1, 51, 1, 239,
	1, 255, 1, 130, 2, 36, 1, 115, 2, 255, 1, 93, 2, 37, 1, 39, 1, 41, 1, 59, 1, 242, 1, 255,
	1, 104, 5, 41, 5, 42, 1, 83, 1, 255, 1, 253, 1, 50, 4, 42, 1, 79, 2, 255, 1, 85, 3, 42,
	4, 43, 1, 154, 1, 255, 1, 253, 1, 63, 2, 43, 1, 164, 1, 255, 1, 236, 1, 60, 2, 45, 1, 44,
	1, 43, 1, 42, 1, 204, 1, 232, 1, 146, 1, 39, 2, 38, 1, 244, 1, 255, 1, 88, 2, 34, 1, 47,
	2, 255, 1, 97, 1, 30, 1, 29, 1, 28, 2, 27, 1, 148, 2, 255, 1, 59, 1, 23, 1, 22, 1, 21,
	1, 20, 1, 18, 1, 43, 2, 255, 1, 45, 2, 16, 1, 14, 1, 12, 6, 11, 1, 10, 1, 8, 1, 6,
	3, 5, 1, 4, 1, 2, 96, 0, 1, 2, 1, 5, 10, 6, 1, 8, 5, 11, 1, 13, 1, 15, 4, 17,
	1, 154, 2, 255, 1, 161, 1, 54, 1, 24, 1, 26, 1, 27, 1, 53, 1, 166, 2, 255, 1, 156, 2, 30,
	2, 31, 1, 68, 1, 247, 1, 255, 1, 177, 1, 48, 2, 33, 1, 63, 1, 207, 1, 255, 1, 226, 1, 47,
	1, 35, 3, 36, 2, 37, 3, 38, 1, 39, 1, 75, 1, 247, 1, 255, 1, 180, 1, 54, 2, 41, 1, 69,
	1, 209, 1, 255, 1, 227, 1, 55, 2, 43, 1, 120, 2, 255, 1, 234, 1, 89, 1, 46, 1, 48, 1, 72,
	1, 209, 1, 255, 1, 205, 1, 50, 5, 49, 5, 48, 1, 50, 1, 244, 1, 255, 1, 102, 4, 48, 1, 84,
	2, 255, 1, 90, 3, 48, 1, 47, 1, 51, 1, 76, 1, 154, 1, 253, 1, 255, 1, 181, 3, 47, 1, 72,
	1, 242, 1, 255, 1, 202, 1, 72, 1, 48, 1, 47, 1, 57, 1, 171, 1, 255, 1, 253, 1, 83, 1, 42,
	1, 41, 1, 40, 1, 234, 1, 255, 1, 116, 1, 37, 1, 36, 1, 37, 1, 239, 1, 255, 1, 188, 1, 42,
	1, 31, 1, 30, 1, 42, 1, 142, 1, 253, 2, 255, 1, 70, 1, 25, 1, 24, 1, 23, 1, 21, 1, 19,
	1, 98, 1, 255, 1, 229, 3, 17, 1, 16, 1, 14, 1, 12, 5, 11, 1, 9, 1, 7, 4, 5, 1, 4,
	1, 2, 96, 0, 1, 2, 1, 5, 10, 6, 1, 8, 6, 11, 1, 13, 1, 16, 1, 18, 1, 19, 1, 21,
	1, 24, 1, 144, 1, 252, 1, 255, 1, 254, 1, 228, 1, 204, 1, 222, 1, 254, 1, 255, 1, 252, 1, 148,
	1, 39, 1, 40, 2, 41, 1, 42, 1, 43, 1, 108, 1, 246, 1, 255, 1, 248, 1, 212, 1, 217, 1, 253,
	1, 255, 1, 227, 1, 85, 1, 50, 1, 51, 2, 52, 1, 53, 2, 54, 1, 55, 2, 56, 1, 57, 1, 58,
	1, 118, 1, 247, 1, 255, 1, 249, 1, 215, 1, 219, 1, 253, 1, 255, 1, 229, 1, 98, 1, 65, 1, 66,
	1, 67, 1, 135, 1, 255, 1, 241, 1, 238, 1, 255, 1, 224, 1, 217, 1, 252, 1, 255, 1, 221, 1, 88,
	6, 66, 5, 65, 1, 64, 1, 194, 1, 255, 1, 159, 2, 64, 2, 63, 1, 96, 11, 255, 1, 215, 1, 75,
	4, 60, 1, 109, 1, 242, 1, 255, 1, 253, 1, 217, 1, 214, 1, 246, 1, 255, 1, 252, 1, 121, 1, 48,
	2, 47, 1, 46, 1, 193, 1, 255, 1, 248, 1, 207, 1, 125, 1, 41, 1, 130, 2, 255, 1, 243, 1, 209,
	1, 215, 1, 249, 1, 255, 1, 186, 1, 224, 1, 255, 1, 100, 1, 30, 1, 29, 1, 28, 1, 25, 1, 23,
	1, 158, 1, 255, 1, 162, 4, 23, 1, 21, 1, 19, 1, 17, 1, 15, 1, 13, 1, 12, 1, 10, 1, 7,
	1, 6, 4, 5, 1, 4, 1, 2, 96, 0, 1, 2, 1, 3, 9, 4, 1, 5, 1, 6, 7, 8, 1, 10,
	1, 11, 1, 13, 1, 16, 1, 18, 1, 19, 1, 55, 1, 149, 1, 207, 1, 240, 1, 252, 1, 244, 1, 211,
	1, 151, 1, 58, 1, 24, 1, 27, 2, 29, 2, 30, 2, 31, 1, 60, 1, 161, 1, 223, 1, 248, 1, 241,
	1, 209, 1, 135, 1, 46, 1, 35, 2, 36, 2, 37, 2, 38, 3, 39, 2, 40, 2, 41, 1, 69, 1, 165,
	1, 224, 1, 248, 1, 242, 1, 211, 1, 140, 1, 56, 1, 45, 2, 46, 1, 47, 1, 122, 1, 255, 1, 223,
	1, 72, 1, 178, 1, 238, 1, 247, 1, 216, 1, 141, 1, 54, 1, 45, 4, 44, 4, 43, 4, 42, 1, 108,
	1, 255, 1, 209, 4, 41, 1, 78, 7, 255, 1, 248, 1, 230, 1, 192, 1, 120, 1, 42, 4, 39, 1, 38,
	1, 37, 1, 57, 1, 153, 1, 218, 1, 246, 1, 245, 1, 219, 1, 165, 1, 68, 3, 33, 1, 31, 1, 30,
	1, 55, 1, 188, 1, 243, 1, 255, 1, 182, 1, 27, 1, 26, 1, 87, 1, 193, 1, 231, 1, 250, 1, 231,
	1, 178, 1, 84, 1, 22, 1, 158, 1, 255, 1, 165, 2, 19, 1, 18, 1, 17, 1, 15, 1, 220, 1, 255,
	1, 71, 6, 15, 1, 13, 1, 11, 1, 9, 1, 7, 1, 5, 6, 4, 1, 3, 1, 1, 173, 0, 1, 92,
	1, 255, 1, 216, 20, 0, 1, 2, 1, 216, 1, 255, 1, 44, 57, 0, 1, 64, 1, 255, 1, 198, 193, 0,
	1, 92, 1, 255, 1, 216, 21, 0, 1, 92, 1, 255, 1, 146, 57, 0, 1, 166, 1, 255, 1, 71, 193, 0,
	1, 92, 1, 255, 1, 216, 21, 0, 1, 3, 1, 212, 1, 241, 1, 21, 55, 0, 1, 33, 1, 249, 1, 196,
	194, 0, 1, 92, 1, 255, 1, 216, 22, 0, 1, 52, 1, 251, 1, 149, 55, 0, 1, 168, 1, 246, 1, 38,
	194, 0, 1, 92, 1, 255, 1, 216, 23, 0, 1, 128, 1, 251, 1, 40, 53, 0, 1, 54, 1, 254, 1, 111,
	255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 17, 0,
};

#define RC_LABEL_W	280
#define RC_LABEL_H	38

// the label as a 32-bit TGA in memory (bottom row first, as the menu's own files), in one colour
static BitmapTGA *RcCoopLabel( int r, int g, int b )
{
	const int size = 18 + RC_LABEL_W * RC_LABEL_H * 4;
	unsigned char *buf = new unsigned char[size];
	memset( buf, 0, size );
	buf[2] = 2;	// uncompressed true colour
	buf[12] = RC_LABEL_W & 255;
	buf[13] = RC_LABEL_W >> 8;
	buf[14] = RC_LABEL_H & 255;
	buf[15] = RC_LABEL_H >> 8;
	buf[16] = 32;
	buf[17] = 8;	// 8 alpha bits
	int p = 0;
	for ( int k = 0; k + 1 < (int)sizeof( s_ucRcCoopLabel ); k += 2 )
	{
		for ( int n = 0; n < s_ucRcCoopLabel[k] && p < RC_LABEL_W * RC_LABEL_H; n++, p++ )
		{
			int x = p % RC_LABEL_W, y = p / RC_LABEL_W;
			unsigned char *px = buf + 18 + ( ( RC_LABEL_H - 1 - y ) * RC_LABEL_W + x ) * 4;
			px[0] = b;
			px[1] = g;
			px[2] = r;
			px[3] = s_ucRcCoopLabel[k + 1];
		}
	}
	BitmapTGA *pImage = vgui_LoadTGAFromMemory( buf, size );
	delete [] buf;
	if ( pImage )
		pImage->setColor( Color( 255, 255, 255, 0 ) );
	return pImage;
}

class CRcCoopPanel;

class CRcCoopHandler : public ActionSignal
{
public:
	CRcCoopHandler( int cmd ) { m_cmd = cmd; }
	virtual void actionPerformed( Panel *panel );
private:
	int m_cmd;
};

static CGameMenu *s_pRcMenu;
static CRcImagePanel *s_pRcItem;	// B401: drawn at the menus' scale
static ColorButton *s_pRcItemButton;
static BitmapTGA *s_pRcLabel, *s_pRcLabelArmed;
static bool s_bRcArmed;
static int s_iRcArmedX, s_iRcArmedY;
static CRcCoopPanel *s_pRcPanel;
// B396: the game's own difficulties are the co-op presets (hl.dll rc_coop_preset): each brings its co-op rules
static const char *s_pszRcPreset[5] = { "", "easy", "normal", "hard", "nightmare" };
static const char *s_pszRcSkillName[5] = { "", "Easy", "Normal", "Hard", "Nightmare" };

// the newest co-op save (coop_auto, coop_tape) in either save folder next to cof.exe, its name in pszName; false for none
static bool RcCoopLastSave( char *pszName, int len )
{
	char dir[MAX_PATH];
	GetModuleFileNameA( NULL, dir, sizeof( dir ) );
	char *slash = strrchr( dir, '\\' );
	if ( !slash )
		return false;
	slash[1] = 0;
	static const char *s_pszDirs[2] = { "SAVE\\", "cryoffear\\SAVE\\" };
	static const char *s_pszSaves[2] = { "coop_auto", "coop_tape" };
	FILETIME best;
	bool bFound = false;
	for ( int d = 0; d < 2; d++ )
	{
		for ( int s = 0; s < 2; s++ )
		{
			char path[MAX_PATH];
			_snprintf( path, sizeof( path ) - 1, "%s%s%s.sav", dir, s_pszDirs[d], s_pszSaves[s] );
			path[sizeof( path ) - 1] = 0;
			WIN32_FILE_ATTRIBUTE_DATA fad;
			if ( !GetFileAttributesExA( path, GetFileExInfoStandard, &fad ) )
				continue;
			if ( !bFound || CompareFileTime( &fad.ftLastWriteTime, &best ) > 0 )
			{
				best = fad.ftLastWriteTime;
				strncpy( pszName, s_pszSaves[s], len - 1 );
				pszName[len - 1] = 0;
				bFound = true;
			}
		}
	}
	return bFound;
}

// B395: Steam's launch options for Cry of Fear that start ReCoF instead ("<this cof.exe>" %command%): with them a
// friend's invite or Join Game starts ReCoF while the game is closed, as Steam then runs this cof.exe with its own
// command and the host's connect value (+rc_coop_join) after it. Copied to the clipboard; false if it could not be
static bool RcCoopLaunchOptions( char *text, int len )
{
	char exe[MAX_PATH];
	if ( !GetModuleFileNameA( NULL, exe, sizeof( exe ) ) )
		return false;
	_snprintf( text, len - 1, "\"%s\" %%command%%", exe );
	text[len - 1] = 0;
	return true;
}

static bool RcCoopCopyLaunchOptions( void )
{
	char text[MAX_PATH + 32];
	if ( !RcCoopLaunchOptions( text, sizeof( text ) ) )
		return false;
	if ( !OpenClipboard( NULL ) )
		return false;
	EmptyClipboard();
	size_t n = strlen( text ) + 1;
	HGLOBAL h = GlobalAlloc( GMEM_MOVEABLE, n );
	bool bOk = false;
	if ( h )
	{
		void *p = GlobalLock( h );
		if ( p )
		{
			memcpy( p, text, n );
			GlobalUnlock( h );
			bOk = SetClipboardData( CF_TEXT, h ) != NULL;
		}
		if ( !bOk )
			GlobalFree( h );
	}
	CloseClipboard();
	return bOk;
}

// the panel's commands
enum
{
	RC_LOBBY_OPEN = 1,	// the menu's entry
	RC_LOBBY_CLOSE = 10,
	RC_LOBBY_LEAVE,
	RC_LOBBY_INVITE,
	RC_LOBBY_START,
	RC_LOBBY_NEW,
	RC_LOBBY_CONTINUE,
	RC_LOBBY_LAUNCH,
	RC_LOBBY_TAB_GAME,
	RC_LOBBY_TAB_SET,
	RC_LOBBY_CAMP_PREV,
	RC_LOBBY_CAMP_NEXT,
	RC_LOBBY_SERVERS,	// B498
	RC_LOBBY_SKILL = 30,	// + 0 to 3
	RC_LOBBY_SET = 40,		// + 2 * the setting, + 1 for the next value
	RC_LOBBY_SETMP = 60,	// B498: + 2 * the setting, + 1 for the next value
};

// B412: the co-op settings: the host's rules (hl.dll rc_coop.cpp; the lobby passes them to rc_coop_start after the
// difficulty's preset) and this player's own voice (client.dll's cvars, set at once)
struct RcLobbySetting_t
{
	const char	*label;
	const char	*cvar;
	bool		host;
	int			n;
	int			values[8];
	const char	*names[8];
};

#define RC_SET_HOST	7
#define RC_SET_ALL	11
#define RC_SET_SIMON	5
#define RC_SET_COLLIDE	6	// B500: for every game, story co-op or not

static const RcLobbySetting_t s_RcSet[RC_SET_ALL] =
{
	{ "Exit waits for the group", "rc_coop_exit_wait", true, 5, { 0, 15, 30, 60, 120 }, { "Off", "15 s", "30 s", "60 s", "2 min" } },
	{ "Back after dying", "rc_coop_respawn_time", true, 6, { 0, 5, 10, 20, 30, 60 }, { "Original", "5 s", "10 s", "20 s", "30 s", "60 s" } },
	{ "Monster health per extra player", "rc_coop_monster_health", true, 7, { 0, 10, 25, 40, 50, 75, 100 }, { "+0%", "+10%", "+25%", "+40%", "+50%", "+75%", "+100%" } },
	{ "Autosave on each level", "rc_coop_autosave", true, 2, { 0, 1 }, { "Off", "On" } },
	{ "Vote to skip scenes", "rc_coop_skip_vote", true, 2, { 0, 1 }, { "Off", "On" } },
	{ "Players are", "rc_coop_simon", true, 2, { 1, 0 }, { "Simon", "Police officers" } },
	{ "Players collide", "rc_player_collide", true, 2, { 1, 0 }, { "On, solid", "Off, pass through" } },	// B500
	{ "Voices from where friends stand", "rc_voice_3d", false, 2, { 0, 1 }, { "Off", "On" } },
	{ "Voice range", "rc_voice_range", false, 4, { 800, 1400, 2000, 3000 }, { "Short", "Normal", "Long", "Very long" } },
	{ "Voices behind walls", "rc_voice_wall", false, 5, { 0, 10, 25, 50, 100 }, { "Silent", "10%", "25%", "50%", "Full" } },
	{ "Talk", "rc_voice_mode", false, 2, { 0, 1 }, { "Push to talk", "Open mic" } },	// B422
};

// the difficulties' rules (hl.dll rc_coop.cpp s_flRcPreset, B396): exit wait, back after dying, monster health,
// autosave, skip vote, in this panel's order
static const int s_iRcPresetRules[5][5] =
{
	{ 60, 10, 25, 1, 1 }, { 60, 5, 0, 1, 1 }, { 60, 10, 25, 1, 1 }, { 60, 20, 40, 1, 1 }, { 60, 0, 50, 0, 1 },
};

// the index of the setting's value nearest to v
static int RcSetIndex( int s, int v )
{
	int best = 0;
	for ( int i = 1; i < s_RcSet[s].n; i++ )
	{
		if ( abs( s_RcSet[s].values[i] - v ) < abs( s_RcSet[s].values[best] - v ) )
			best = i;
	}
	return best;
}

// the games the host picks from (B498): story co-op, then Cry of Fear's own co-op games
struct RcLobbyCampaign_t
{
	char name[64];
	char firstmap[64];
	bool classic;	// B498: Cry of Fear's own multiplayer rules (rc_coop_mp), not story co-op
};
#define RC_MAX_CAMPAIGNS	64

// B498: Cry of Fear's own co-op games, the maps Host Server lists, in this order and under these names; any other
// map of that list comes after them under its file name
static const char *s_pszRcClassic[][2] =
{
	{ "cof_campaign_01", "Co-op campaign" },
	{ "cof_campaign_01_p2", "Co-op campaign, part 2" },
	{ "cof_campaign_01_p3", "Co-op campaign, part 3" },
	{ "cof_campaign_01_p4", "Co-op campaign, part 4" },
	{ "cof_manhunt_campaign", "Manhunt" },
	{ "cof_suicide1", "Survival 1" },
	{ "cof_suicide2", "Survival 2" },
	{ "cof_suicide3", "Survival 3" },
	{ "cof_suicide4", "Survival 4" },
};
#define RC_CLASSIC_KNOWN	( sizeof( s_pszRcClassic ) / sizeof( s_pszRcClassic[0] ) )

// B498: their settings, two of Host Server's (the game's settings.scr): the time before a dead player comes back by
// himself, and how long the game's own lobby waits for the players before it starts (mp_footsteps, which Cry of Fear
// uses for it)
#define RC_SETMP	2
static const RcLobbySetting_t s_RcSetMp[RC_SETMP] =
{
	{ "Back after dying", "sv_auto_respawn_time", true, 5, { 30, 60, 120, 240, 384 }, { "30 s", "1 min", "2 min", "4 min", "Original (384 s)" } },
	{ "Game starts after", "mp_footsteps", true, 4, { 30, 60, 120, 300 }, { "30 s", "1 min", "Original (2 min)", "5 min" } },
};
static const int s_iRcSetMpDefault[RC_SETMP] = { 4, 2 };

static int RcCampaignSort( const void *a, const void *b )
{
	return stricmp( ( (const RcLobbyCampaign_t *)a )->name, ( (const RcLobbyCampaign_t *)b )->name );
}

// B406, B412: the lobby, a frame in the middle of the screen over a dark veil, with a Game tab and a Co-op settings tab
class CRcCoopPanel : public Panel
{
public:
	CRcCoopPanel() : Panel( 0, 0, ScreenWidth, ScreenHeight )
	{
		setBgColor( 0, 0, 0, 255 );
		m_pFrame = new CRcUiFrame( "Co-op (Beta)", 0, 0, 10, 10 );	// B497, B541
		m_pFrame->setParent( this );
		m_pClose = AddButton( m_pFrame, "X", CRcUiButton::STYLE_ROW, RCF_TITLE, RC_LOBBY_CLOSE );
		m_pClose->setRcCentred( true );
		m_pTabGame = AddButton( m_pFrame, "Game", CRcUiButton::STYLE_ROW, RCF_BUTTON, RC_LOBBY_TAB_GAME );
		m_pTabGame->setRcCentred( true );
		m_pTabSet = AddButton( m_pFrame, "Co-op settings", CRcUiButton::STYLE_ROW, RCF_BUTTON, RC_LOBBY_TAB_SET );
		m_pTabSet->setRcCentred( true );

		// the Game tab
		m_pGame = AddPage();
		m_pPlayersCap = AddText( m_pGame, "PLAYERS", RCF_LABEL, RCC_DIM );
		for ( int i = 0; i < 4; i++ )
		{
			m_pName[i] = AddText( m_pGame, "", RCF_BODY, RCC_TEXT );
			m_pTag[i] = AddText( m_pGame, "", RCF_LABEL, RCC_DIM );
			m_pTag[i]->setRcAlign( 2 );
		}
		m_pCampCap = AddText( m_pGame, "CAMPAIGN", RCF_LABEL, RCC_DIM );
		m_pCampPrev = AddButton( m_pGame, "<", CRcUiButton::STYLE_BOX, RCF_BUTTON, RC_LOBBY_CAMP_PREV );
		m_pCampNext = AddButton( m_pGame, ">", CRcUiButton::STYLE_BOX, RCF_BUTTON, RC_LOBBY_CAMP_NEXT );
		m_pCampName = AddText( m_pGame, "", RCF_BODY, RCC_TEXT );
		m_pCampName->setRcAlign( 1 );
		m_pSkillCap = AddText( m_pGame, "DIFFICULTY", RCF_LABEL, RCC_DIM );
		for ( int i = 0; i < 4; i++ )
		{
			m_pSkill[i] = AddButton( m_pGame, s_pszRcSkillName[i + 1], CRcUiButton::STYLE_ROW, RCF_BODY, RC_LOBBY_SKILL + i );
			m_pSkill[i]->setRcCentred( true );
		}
		m_pGameCap = AddText( m_pGame, "START FROM", RCF_LABEL, RCC_DIM );
		m_pNew = AddButton( m_pGame, "New game", CRcUiButton::STYLE_ROW, RCF_BODY, RC_LOBBY_NEW );
		m_pNew->setRcCentred( true );
		m_pContinue = AddButton( m_pGame, "Continue", CRcUiButton::STYLE_ROW, RCF_BODY, RC_LOBBY_CONTINUE );
		m_pContinue->setRcCentred( true );

		// the Co-op settings tab
		m_pSet = AddPage();
		m_pHostCap = AddText( m_pSet, "HOST'S RULES", RCF_LABEL, RCC_DIM );
		m_pOwnCap = AddText( m_pSet, "YOUR GAME", RCF_LABEL, RCC_DIM );
		for ( int s = 0; s < RC_SET_ALL; s++ )
		{
			m_pSetLabel[s] = AddText( m_pSet, s_RcSet[s].label, RCF_BODY, RCC_TEXT );
			m_pSetPrev[s] = AddButton( m_pSet, "<", CRcUiButton::STYLE_BOX, RCF_BUTTON, RC_LOBBY_SET + 2 * s );
			m_pSetNext[s] = AddButton( m_pSet, ">", CRcUiButton::STYLE_BOX, RCF_BUTTON, RC_LOBBY_SET + 2 * s + 1 );
			m_pSetValue[s] = AddText( m_pSet, "", RCF_BODY, RCC_TEXT );
			m_pSetValue[s]->setRcAlign( 1 );
		}
		for ( int s = 0; s < RC_SETMP; s++ )	// B498
		{
			m_pMpLabel[s] = AddText( m_pSet, s_RcSetMp[s].label, RCF_BODY, RCC_TEXT );
			m_pMpPrev[s] = AddButton( m_pSet, "<", CRcUiButton::STYLE_BOX, RCF_BUTTON, RC_LOBBY_SETMP + 2 * s );
			m_pMpNext[s] = AddButton( m_pSet, ">", CRcUiButton::STYLE_BOX, RCF_BUTTON, RC_LOBBY_SETMP + 2 * s + 1 );
			m_pMpValue[s] = AddText( m_pSet, "", RCF_BODY, RCC_TEXT );
			m_pMpValue[s]->setRcAlign( 1 );
			m_iMp[s] = s_iRcSetMpDefault[s];
		}

		m_pStatus = AddText( m_pFrame, "", RCF_LABEL, RCC_DIM );
		m_pStatus->setRcAlign( 1 );
		m_pInvite = AddButton( m_pFrame, "Invite friends", CRcUiButton::STYLE_BOX, RCF_BUTTON, RC_LOBBY_INVITE );
		m_pLeave = AddButton( m_pFrame, "Leave", CRcUiButton::STYLE_BOX, RCF_BUTTON, RC_LOBBY_LEAVE );
		m_pServers = AddButton( m_pFrame, "Server list", CRcUiButton::STYLE_BOX, RCF_BUTTON, RC_LOBBY_SERVERS );	// B498
		m_pStart = AddButton( m_pFrame, "Start", CRcUiButton::STYLE_PRIMARY, RCF_BUTTON, RC_LOBBY_START );
		// under the frame: the launch options a friend's Steam needs to start ReCoF from an invite (B395)
		m_pLaunch = new CRcUiButton( "Copy Steam launch options", 0, 0, 10, 10, CRcUiButton::STYLE_ITEM, RCF_LABEL );
		m_pLaunch->setRcCentred( true );
		m_pLaunch->setParent( this );
		m_pLaunch->addActionSignal( new CRcCoopHandler( RC_LOBBY_LAUNCH ) );

		m_iGame = 0;
		m_bSave = false;
		m_flScale = 0;
		m_iTab = 0;
		m_iCampaign = 0;
		m_nCampaigns = 0;
		m_bLookSet = false;
		m_bLaidClassic = false;
		m_iW = 10;
		m_iServersY = 0;
		m_iServersX[0] = m_iServersX[1] = 0;
		RulesFromSkill( RcSkill() );
		setVisible( false );
	}

	// from the menu (bHost: a lobby of this player's own when there is none), or Steam brought the player into one
	void Open( bool bHost )
	{
		m_bSave = RcCoopLastSave( m_szSave, sizeof( m_szSave ) );
		if ( !m_bSave )
			m_iGame = 0;
		if ( bHost && RcLobby_State() == RCLOBBY_NONE )
			RcLobby_Create();
		ReadCampaigns();
		for ( int s = RC_SET_HOST; s < RC_SET_ALL; s++ )
			m_iSet[s] = RcSetIndex( s, (int)gEngfuncs.pfnGetCvarFloat( (char *)s_RcSet[s].cvar ) );
		m_iSet[RC_SET_COLLIDE] = RcSetIndex( RC_SET_COLLIDE, (int)gEngfuncs.pfnGetCvarFloat( "rc_player_collide" ) );	// B500
		m_pLaunch->setRcText( "Copy Steam launch options" );
		Layout();
		Refresh();
		setVisible( true );
		if ( s_pRcMenu )
			s_pRcMenu->Panel::setVisible( false );	// Panel's: CGameMenu's own would start its music again
		gViewPort->UpdateCursorState();
	}

	void Close( void )
	{
		setVisible( false );
		if ( s_pRcMenu && strstr( gEngfuncs.pfnGetLevelName(), "game_menu" ) )
			s_pRcMenu->Panel::setVisible( true );
		gViewPort->UpdateCursorState();
	}

	int RcSkill( void )
	{
		int iSkill = (int)gEngfuncs.pfnGetCvarFloat( "difficulty" );
		if ( iSkill < 1 || iSkill > 4 || ( iSkill == 4 && !( gHUD.m_iUnlocks_guess & 32 ) ) )
			iSkill = 2;
		return iSkill;
	}

	// a difficulty brings its rules (B396); the host changes them after
	void RulesFromSkill( int iSkill )
	{
		for ( int s = 0; s < 5; s++ )
			m_iSet[s] = RcSetIndex( s, s_iRcPresetRules[iSkill][s] );
		if ( !m_bLookSet )
			m_iSet[RC_SET_SIMON] = m_iCampaign == 0 ? 0 : 1;
	}

	// B498: story co-op (the main campaign), then Cry of Fear's own co-op games: the maps Host Server lists (the
	// game's maps/*.bsp without liblist.gam's mpfilter, "c_"), the known ones first under their names, then any
	// other (a multiplayer map a player added) under its file name. The custom campaigns (the main menu's Custom
	// Campaign list: The Stairway and the others) are single player campaigns and are no longer offered here
	void ReadCampaigns( void )
	{
		m_nCampaigns = 1;
		strcpy( m_Campaign[0].name, "Story co-op" );
		strcpy( m_Campaign[0].firstmap, "c_intro" );
		m_Campaign[0].classic = false;

		char dir[MAX_PATH];
		GetModuleFileNameA( NULL, dir, sizeof( dir ) );
		char *slash = strrchr( dir, '\\' );
		if ( slash )
		{
			_snprintf( slash + 1, sizeof( dir ) - 1 - ( slash + 1 - dir ), "%s\\maps\\", gEngfuncs.pfnGetGameDirectory() );
			dir[sizeof( dir ) - 1] = 0;
			for ( int i = 0; i < (int)RC_CLASSIC_KNOWN && m_nCampaigns < RC_MAX_CAMPAIGNS; i++ )
			{
				char path[MAX_PATH];
				_snprintf( path, sizeof( path ) - 1, "%s%s.bsp", dir, s_pszRcClassic[i][0] );
				path[sizeof( path ) - 1] = 0;
				if ( GetFileAttributesA( path ) == INVALID_FILE_ATTRIBUTES )
					continue;
				AddCampaign( s_pszRcClassic[i][0], s_pszRcClassic[i][1] );
			}
			int nFirstOther = m_nCampaigns;
			char pattern[MAX_PATH];
			_snprintf( pattern, sizeof( pattern ) - 1, "%s*.bsp", dir );
			pattern[sizeof( pattern ) - 1] = 0;
			WIN32_FIND_DATAA fd;
			HANDLE hFind = FindFirstFileA( pattern, &fd );
			if ( hFind != INVALID_HANDLE_VALUE )
			{
				do
				{
					char map[64];
					strncpy( map, fd.cFileName, 63 );
					map[63] = 0;
					char *dot = strrchr( map, '.' );
					if ( dot )
						*dot = 0;
					if ( strstr( map, "c_" ) || IsKnownClassic( map ) )
						continue;
					AddCampaign( map, map );
				} while ( m_nCampaigns < RC_MAX_CAMPAIGNS && FindNextFileA( hFind, &fd ) );
				FindClose( hFind );
			}
			if ( m_nCampaigns - nFirstOther > 1 )
				qsort( &m_Campaign[nFirstOther], m_nCampaigns - nFirstOther, sizeof( RcLobbyCampaign_t ), RcCampaignSort );
		}
		if ( m_iCampaign >= m_nCampaigns )
			m_iCampaign = 0;
	}

	void AddCampaign( const char *pszMap, const char *pszName )
	{
		RcLobbyCampaign_t &d = m_Campaign[m_nCampaigns++];
		strncpy( d.firstmap, pszMap, 63 );
		d.firstmap[63] = 0;
		strncpy( d.name, pszName, 63 );
		d.name[63] = 0;
		d.classic = true;
	}

	static bool IsKnownClassic( const char *pszMap )
	{
		for ( int i = 0; i < (int)RC_CLASSIC_KNOWN; i++ )
		{
			if ( !stricmp( pszMap, s_pszRcClassic[i][0] ) )
				return true;
		}
		return false;
	}

	// this player decides: the lobby's owner, or a player alone (no lobby: Steam is not there)
	bool Hosting( void )
	{
		return RcLobby_State() != RCLOBBY_IN || RcLobby_Owner();
	}

	void Command( int cmd )
	{
		char szCmd[256];
		bool bHost = Hosting();
		if ( cmd >= RC_LOBBY_SKILL && cmd < RC_LOBBY_SKILL + 4 )
		{
			int iSkill = cmd - RC_LOBBY_SKILL + 1;
			if ( !bHost || ( iSkill == 4 && !( gHUD.m_iUnlocks_guess & 32 ) ) )
				return;
			_snprintf( szCmd, sizeof( szCmd ) - 1, "difficulty %d\n", iSkill );
			szCmd[sizeof( szCmd ) - 1] = 0;
			gEngfuncs.pfnClientCmd( szCmd );
			gEngfuncs.Cvar_SetValue( "difficulty", iSkill );	// shown at once, before the command runs
			RulesFromSkill( iSkill );
			gEngfuncs.pfnPlaySoundByName( "ui/3d_click.wav", 0.5 );
			Refresh();
			return;
		}
		if ( cmd >= RC_LOBBY_SETMP && cmd < RC_LOBBY_SETMP + 2 * RC_SETMP )	// B498
		{
			int s = ( cmd - RC_LOBBY_SETMP ) / 2, dir = ( cmd - RC_LOBBY_SETMP ) % 2 ? 1 : -1;
			if ( !bHost )
				return;
			m_iMp[s] = ( m_iMp[s] + dir + s_RcSetMp[s].n ) % s_RcSetMp[s].n;
			gEngfuncs.pfnPlaySoundByName( "ui/3d_click.wav", 0.5 );
			Refresh();
			return;
		}
		if ( cmd >= RC_LOBBY_SET && cmd < RC_LOBBY_SET + 2 * RC_SET_ALL )
		{
			int s = ( cmd - RC_LOBBY_SET ) / 2, dir = ( cmd - RC_LOBBY_SET ) % 2 ? 1 : -1;
			if ( s_RcSet[s].host && !bHost )
				return;
			m_iSet[s] = ( m_iSet[s] + dir + s_RcSet[s].n ) % s_RcSet[s].n;
			if ( s == RC_SET_SIMON )
				m_bLookSet = true;
			if ( !s_RcSet[s].host )
			{
				_snprintf( szCmd, sizeof( szCmd ) - 1, "%s %d\n", s_RcSet[s].cvar, s_RcSet[s].values[m_iSet[s]] );
				szCmd[sizeof( szCmd ) - 1] = 0;
				gEngfuncs.pfnClientCmd( szCmd );
			}
			gEngfuncs.pfnPlaySoundByName( "ui/3d_click.wav", 0.5 );
			Refresh();
			return;
		}
		switch ( cmd )
		{
		case RC_LOBBY_TAB_GAME:
		case RC_LOBBY_TAB_SET:
			m_iTab = cmd == RC_LOBBY_TAB_SET ? 1 : 0;
			gEngfuncs.pfnPlaySoundByName( "ui/3d_click.wav", 0.5 );
			Refresh();
			break;
		case RC_LOBBY_CAMP_PREV:
		case RC_LOBBY_CAMP_NEXT:
			if ( !bHost || m_iGame == 1 || m_nCampaigns < 2 )
				return;
			m_iCampaign = ( m_iCampaign + ( cmd == RC_LOBBY_CAMP_NEXT ? 1 : -1 ) + m_nCampaigns ) % m_nCampaigns;
			if ( !m_bLookSet )
				m_iSet[RC_SET_SIMON] = m_iCampaign == 0 ? 0 : 1;	// Simon in the main campaign, police officers in the others
			gEngfuncs.pfnPlaySoundByName( "ui/3d_click.wav", 0.5 );
			Refresh();
			break;
		case RC_LOBBY_CLOSE:
			gEngfuncs.pfnPlaySoundByName( "ui/3d_click.wav", 0.5 );
			Close();
			break;
		case RC_LOBBY_SERVERS:	// B498: the server browser, as the main menu's Join Server opened it
			gEngfuncs.pfnPlaySoundByName( "ui/3d_click.wav", 0.5 );
			Close();
			gEngfuncs.pfnServerCmd( "optionshack 0" );
			break;
		case RC_LOBBY_LEAVE:
			gEngfuncs.pfnPlaySoundByName( "ui/3d_click.wav", 0.5 );
			RcLobby_Leave();
			Close();
			break;
		case RC_LOBBY_INVITE:
			gEngfuncs.pfnPlaySoundByName( "ui/3d_click.wav", 0.5 );
			if ( !RcLobby_Invite() )
				m_pStatus->setRcText( "Steam's overlay is not available" );
			break;
		case RC_LOBBY_NEW:
		case RC_LOBBY_CONTINUE:
			if ( !bHost || ( cmd == RC_LOBBY_CONTINUE && ( !m_bSave || m_Campaign[m_iCampaign].classic ) ) )
				return;	// B498: Cry of Fear's multiplayer has no saves
			m_iGame = cmd == RC_LOBBY_CONTINUE ? 1 : 0;
			gEngfuncs.pfnPlaySoundByName( "ui/3d_click.wav", 0.5 );
			Refresh();
			break;
		case RC_LOBBY_LAUNCH:
			gEngfuncs.pfnPlaySoundByName( "ui/3d_click.wav", 0.5 );
			m_pLaunch->setRcText( RcCoopCopyLaunchOptions() ? "Copied: paste it in Cry of Fear's launch options on Steam" : "Could not copy them" );
			break;
		case RC_LOBBY_START:
		{
			gEngfuncs.pfnPlaySoundByName( "ui/3d_click.wav", 0.5 );
			if ( !bHost )
			{
				RcLobby_JoinGame();	// the host's game is up: join it
				return;
			}
			if ( m_iGame == 1 && !m_bSave )
				return;
			int v[RC_SET_HOST];
			for ( int s = 0; s < RC_SET_HOST; s++ )
				v[s] = s_RcSet[s].values[m_iSet[s]];
			Close();
			if ( s_pRcMenu )
				s_pRcMenu->setVisible( false );
			gViewPort->UpdateCursorState();
			if ( strstr( gEngfuncs.pfnGetLevelName(), "game_menu" ) )
				IrrKlangStop_guess( 1 );
			// rc_coop_start <map> <difficulty> <exit wait> <back after dying> <autosave> <monster health> <skip vote> <simon>
			_snprintf( szCmd, sizeof( szCmd ) - 1, "rc_player_collide %d\n", v[RC_SET_COLLIDE] );	// B500: the host's own cvar
			szCmd[sizeof( szCmd ) - 1] = 0;
			gEngfuncs.pfnClientCmd( szCmd );
			if ( m_Campaign[m_iCampaign].classic )	// B498: rc_coop_mp <map> <back after dying> <game starts after>
				_snprintf( szCmd, sizeof( szCmd ) - 1, "rc_coop_mp %s %d %d\n", m_Campaign[m_iCampaign].firstmap,
					s_RcSetMp[0].values[m_iMp[0]], s_RcSetMp[1].values[m_iMp[1]] );
			else if ( m_iGame == 0 )
				_snprintf( szCmd, sizeof( szCmd ) - 1, "rc_coop_start %s %s %d %d %d %d %d %d\n", m_Campaign[m_iCampaign].firstmap,
					s_pszRcPreset[RcSkill()], v[0], v[1], v[3], v[2], v[4], v[5] );
			else	// a save keeps its campaign's look unless the host chose one
				_snprintf( szCmd, sizeof( szCmd ) - 1, "rc_coop_continue %s %d %d %d %d %d %d\n", m_szSave, v[0], v[1], v[3], v[2], v[4],
					m_bLookSet ? v[5] : -1 );
			szCmd[sizeof( szCmd ) - 1] = 0;
			gEngfuncs.pfnClientCmd( szCmd );
			break;
		}
		}
	}

	// every frame while shown: the lobby's players, the host's choices, what happens next
	void Refresh( void )
	{
		int iState = RcLobby_State();
		bool bHost = Hosting();
		char szNames[4][48], sz[96];
		bool bIsHost[4], bIsSelf[4];
		int n = iState == RCLOBBY_IN ? RcLobby_Members( szNames, bIsHost, bIsSelf, 4 ) : 0;
		if ( n == 0 )
		{
			const char *pszMe = RcSteamNet_PersonaName();
			strncpy( szNames[0], pszMe && pszMe[0] ? pszMe : "You", 47 );
			szNames[0][47] = 0;
			bIsHost[0] = bIsSelf[0] = true;
			n = 1;
		}
		for ( int i = 0; i < 4; i++ )
		{
			if ( i < n )
			{
				m_pName[i]->setRcText( szNames[i] );
				m_pName[i]->setRcColor( RCC_TEXT );
				m_pTag[i]->setRcText( bIsHost[i] && bIsSelf[i] ? "Host, you" : bIsHost[i] ? "Host" : bIsSelf[i] ? "You" : "" );
			}
			else
			{
				m_pName[i]->setRcText( "Open" );
				m_pName[i]->setRcColor( RCC_DISABLED );
				m_pTag[i]->setRcText( "" );
			}
		}

		// the host's choices: his own (and into the lobby for the guests), or the lobby's for a guest
		int iSkill;
		bool bContinue;
		const char *pszCampaign;
		int iRule[RC_SET_HOST];
		bool bClassic;		// B498
		int iMp[RC_SETMP];
		if ( bHost )
		{
			iSkill = RcSkill();
			bContinue = m_iGame == 1;
			pszCampaign = m_Campaign[m_iCampaign].name;
			for ( int s = 0; s < RC_SET_HOST; s++ )
				iRule[s] = m_iSet[s];
			_snprintf( sz, sizeof( sz ) - 1, "%d", iSkill );
			sz[sizeof( sz ) - 1] = 0;
			SetData( "difficulty", sz );
			SetData( "game", bContinue ? "continue" : "new" );
			SetData( "campaign", pszCampaign );
			_snprintf( sz, sizeof( sz ) - 1, "%d %d %d %d %d %d %d", iRule[0], iRule[1], iRule[2], iRule[3], iRule[4], iRule[5], iRule[6] );
			sz[sizeof( sz ) - 1] = 0;
			SetData( "rules", sz );
			bClassic = m_Campaign[m_iCampaign].classic && !bContinue;
			SetData( "mode", bClassic ? "classic" : "story" );
			for ( int s = 0; s < RC_SETMP; s++ )
				iMp[s] = m_iMp[s];
			_snprintf( sz, sizeof( sz ) - 1, "%d %d", iMp[0], iMp[1] );
			sz[sizeof( sz ) - 1] = 0;
			SetData( "mprules", sz );
		}
		else
		{
			iSkill = atoi( RcLobby_Data( "difficulty" ) );
			bContinue = !strcmp( RcLobby_Data( "game" ), "continue" );
			pszCampaign = RcLobby_Data( "campaign" );
			memset( iRule, 0, sizeof( iRule ) );
			sscanf( RcLobby_Data( "rules" ), "%d %d %d %d %d %d %d", &iRule[0], &iRule[1], &iRule[2], &iRule[3], &iRule[4], &iRule[5], &iRule[6] );
			bClassic = !strcmp( RcLobby_Data( "mode" ), "classic" );
			iMp[0] = iMp[1] = 0;
			sscanf( RcLobby_Data( "mprules" ), "%d %d", &iMp[0], &iMp[1] );
		}
		if ( bClassic != m_bLaidClassic )
			LayoutSettings( bClassic );

		m_pTabGame->setRcSelected( m_iTab == 0 );
		m_pTabSet->setRcSelected( m_iTab == 1 );
		m_pGame->setVisible( m_iTab == 0 );
		m_pSet->setVisible( m_iTab == 1 );

		m_pCampName->setRcText( bContinue ? "From the save" : pszCampaign[0] ? pszCampaign : "Main campaign" );
		m_pCampName->setRcColor( bContinue ? RCC_DISABLED : RCC_TEXT );
		m_pCampPrev->setVisible( bHost && !bContinue && m_nCampaigns > 1 );
		m_pCampNext->setVisible( bHost && !bContinue && m_nCampaigns > 1 );
		for ( int i = 0; i < 4; i++ )
		{
			m_pSkill[i]->setRcSelected( i + 1 == iSkill );
			m_pSkill[i]->setRcDisabled( !bHost || ( i == 3 && !( gHUD.m_iUnlocks_guess & 32 ) ) );
		}
		m_pGameCap->setVisible( !bClassic );	// B498: no saves in Cry of Fear's multiplayer
		m_pNew->setVisible( !bClassic );
		m_pContinue->setVisible( !bClassic );
		m_pNew->setRcSelected( !bContinue );
		m_pNew->setRcDisabled( !bHost );
		m_pContinue->setRcSelected( bContinue );
		m_pContinue->setRcDisabled( !bHost || !m_bSave );
		if ( bHost && m_bSave )
			_snprintf( sz, sizeof( sz ) - 1, "Continue (%s)", m_szSave );
		else
			strcpy( sz, "Continue" );
		sz[sizeof( sz ) - 1] = 0;
		m_pContinue->setRcText( sz );

		for ( int s = 0; s < RC_SET_ALL; s++ )
		{
			int i = s < RC_SET_HOST ? iRule[s] : m_iSet[s];
			if ( i < 0 || i >= s_RcSet[s].n )
				i = 0;
			m_pSetValue[s]->setRcText( s_RcSet[s].names[i] );
			bool bCan = !s_RcSet[s].host || bHost;
			bool bShown = s >= RC_SET_HOST || !bClassic || s == RC_SET_COLLIDE;	// B498: story co-op's rules only for story co-op
			m_pSetLabel[s]->setVisible( bShown );
			m_pSetValue[s]->setVisible( bShown );
			m_pSetPrev[s]->setVisible( bCan && bShown );
			m_pSetNext[s]->setVisible( bCan && bShown );
		}
		for ( int s = 0; s < RC_SETMP; s++ )	// B498
		{
			int i = iMp[s] >= 0 && iMp[s] < s_RcSetMp[s].n ? iMp[s] : 0;
			m_pMpValue[s]->setRcText( s_RcSetMp[s].names[i] );
			m_pMpLabel[s]->setVisible( bClassic );
			m_pMpValue[s]->setVisible( bClassic );
			m_pMpPrev[s]->setVisible( bClassic && bHost );
			m_pMpNext[s]->setVisible( bClassic && bHost );
		}

		// a host back on the menu after a game: the lobby's guests no longer have a game to join
		const char *pszConnect = RcLobby_Data( "connect" );
		if ( iState == RCLOBBY_IN && RcLobby_Owner() && pszConnect[0] && strstr( gEngfuncs.pfnGetLevelName(), "game_menu" ) )
		{
			RcLobby_SetData( "connect", "" );
			pszConnect = "";
		}

		const char *pszStatus = "";
		if ( iState == RCLOBBY_ASKED )
			pszStatus = "Opening the lobby...";
		else if ( iState == RCLOBBY_NONE )
			pszStatus = RcLobby_Note()[0] ? RcLobby_Note() : "";
		else if ( bHost )
			pszStatus = n < 2 ? "Invite your friends, then start" : "";
		else
			pszStatus = pszConnect[0] ? "The host's game is on" : "Waiting for the host to start";
		m_pStatus->setRcText( pszStatus );
		m_pInvite->setVisible( iState == RCLOBBY_IN );
		m_pServers->setPos( m_iServersX[iState == RCLOBBY_IN ? 1 : 0], m_iServersY );	// B498: next to Invite friends
		m_pStart->setRcText( bHost ? "Start" : "Join game" );
		m_pStart->setVisible( bHost || pszConnect[0] );
	}

	void Layout( void )
	{
		m_flScale = RcUiScale();
		int W = RcUiS( 660 ), H = RcUiS( 570 );
		if ( W > ScreenWidth - 32 )
			W = ScreenWidth - 32;
		int x0 = ( ScreenWidth - W ) / 2, y0 = ( ScreenHeight - H ) / 2 - RcUiS( 16 );
		if ( y0 < 0 )
			y0 = 0;
		m_pFrame->setBounds( x0, y0, W, H );
		int band = m_pFrame->bandTall(), pad = RcUiS( 24 ), gap = RcUiS( 8 ), cw = W - 2 * pad;
		m_pClose->setBounds( W - band, 0, band, band );
		int tabY = band + RcUiS( 10 );
		m_pTabGame->setBounds( pad, tabY, RcUiS( 130 ), RcUiS( 30 ) );
		m_pTabSet->setBounds( pad + RcUiS( 130 ) + gap, tabY, RcUiS( 170 ), RcUiS( 30 ) );
		int top = tabY + RcUiS( 30 ) + RcUiS( 14 );
		int by = H - RcUiS( 20 ) - RcUiS( 34 );
		m_pGame->setBounds( 0, top, W, by - RcUiS( 34 ) - top );
		m_pSet->setBounds( 0, top, W, by - RcUiS( 34 ) - top );

		// the Game tab
		int y = 0;
		m_pPlayersCap->setBounds( pad, y, cw, RcUiS( 20 ) );
		y += RcUiS( 22 );
		for ( int i = 0; i < 4; i++ )
		{
			m_pName[i]->setBounds( pad, y + i * RcUiS( 28 ), cw, RcUiS( 26 ) );
			m_pTag[i]->setBounds( pad, y + i * RcUiS( 28 ), cw, RcUiS( 26 ) );
		}
		y += 4 * RcUiS( 28 ) + RcUiS( 12 );
		m_pCampCap->setBounds( pad, y, cw, RcUiS( 20 ) );
		y += RcUiS( 22 );
		m_pCampPrev->setBounds( pad, y, RcUiS( 34 ), RcUiS( 32 ) );
		m_pCampNext->setBounds( W - pad - RcUiS( 34 ), y, RcUiS( 34 ), RcUiS( 32 ) );
		m_pCampName->setBounds( pad + RcUiS( 40 ), y, cw - RcUiS( 80 ), RcUiS( 32 ) );
		y += RcUiS( 32 ) + RcUiS( 14 );
		m_pSkillCap->setBounds( pad, y, cw, RcUiS( 20 ) );
		y += RcUiS( 22 );
		int bw = ( cw - 3 * gap ) / 4;
		for ( int i = 0; i < 4; i++ )
			m_pSkill[i]->setBounds( pad + i * ( bw + gap ), y, bw, RcUiS( 32 ) );
		y += RcUiS( 32 ) + RcUiS( 14 );
		m_pGameCap->setBounds( pad, y, cw, RcUiS( 20 ) );
		y += RcUiS( 22 );
		int gw = ( cw - gap ) / 2;
		m_pNew->setBounds( pad, y, gw, RcUiS( 32 ) );
		m_pContinue->setBounds( pad + gw + gap, y, gw, RcUiS( 32 ) );

		// the Co-op settings tab (B498: its host's rows by the game's kind)
		m_iW = W;
		LayoutSettings( m_bLaidClassic );

		m_pStatus->setBounds( pad, by - RcUiS( 30 ), cw, RcUiS( 22 ) );
		m_pInvite->setBounds( pad, by, RcUiS( 170 ), RcUiS( 34 ) );
		m_iServersY = by;	// B498: Server list, next to Invite friends when it shows
		m_iServersX[0] = pad;
		m_iServersX[1] = pad + RcUiS( 170 ) + RcUiS( 12 );
		m_pServers->setBounds( m_iServersX[1], by, RcUiS( 140 ), RcUiS( 34 ) );
		m_pStart->setBounds( W - pad - RcUiS( 150 ), by, RcUiS( 150 ), RcUiS( 34 ) );
		m_pLeave->setBounds( W - pad - RcUiS( 150 ) - RcUiS( 12 ) - RcUiS( 120 ), by, RcUiS( 120 ), RcUiS( 34 ) );
		m_pLaunch->setBounds( x0, y0 + H + RcUiS( 10 ), W, RcUiS( 28 ) );
	}

	// the Co-op settings tab: the host's rules, story co-op's or Cry of Fear's own (B498), then the player's own; a
	// label, then < value > on the right
	void LayoutSettings( bool bClassic )
	{
		m_bLaidClassic = bClassic;
		int pad = RcUiS( 24 ), cw = m_iW - 2 * pad, pitch = RcUiS( 30 ), y = 0;
		m_pHostCap->setBounds( pad, y, cw, RcUiS( 20 ) );
		y += RcUiS( 22 );
		if ( bClassic )
		{
			for ( int s = 0; s < RC_SETMP; s++, y += pitch )
				RowBounds( m_pMpLabel[s], m_pMpPrev[s], m_pMpValue[s], m_pMpNext[s], y );
			RowBounds( m_pSetLabel[RC_SET_COLLIDE], m_pSetPrev[RC_SET_COLLIDE], m_pSetValue[RC_SET_COLLIDE], m_pSetNext[RC_SET_COLLIDE], y );	// B500
			y += pitch;
		}
		else
		{
			for ( int s = 0; s < RC_SET_HOST; s++, y += pitch )
				RowBounds( m_pSetLabel[s], m_pSetPrev[s], m_pSetValue[s], m_pSetNext[s], y );
		}
		y += RcUiS( 10 );
		m_pOwnCap->setBounds( pad, y, cw, RcUiS( 20 ) );
		y += RcUiS( 22 );
		for ( int s = RC_SET_HOST; s < RC_SET_ALL; s++, y += pitch )
			RowBounds( m_pSetLabel[s], m_pSetPrev[s], m_pSetValue[s], m_pSetNext[s], y );
	}

	void RowBounds( CRcUiText *pLabel, CRcUiButton *pPrev, CRcUiText *pValue, CRcUiButton *pNext, int y )
	{
		int pad = RcUiS( 24 ), gap = RcUiS( 8 ), vw = RcUiS( 170 ), aw = RcUiS( 30 );
		int x = m_iW - pad - aw;
		pNext->setBounds( x, y, aw, RcUiS( 28 ) );
		x -= vw;
		pValue->setBounds( x, y, vw, RcUiS( 28 ) );
		x -= aw;
		pPrev->setBounds( x, y, aw, RcUiS( 28 ) );
		pLabel->setBounds( pad, y, x - pad - gap, RcUiS( 28 ) );
	}

	virtual void solve()
	{
		Panel::solve();
		// the guest's game joined the host's: the lobby goes with the menu
		const char *pszLevel = gEngfuncs.pfnGetLevelName();
		if ( pszLevel && pszLevel[0] && !strstr( pszLevel, "game_menu" ) )
		{
			setVisible( false );
			gViewPort->UpdateCursorState();
			return;
		}
		if ( RcUiScale() != m_flScale )
			Layout();
		Refresh();
	}

	// a dark veil over the menu's scene
	virtual void paintBackground()
	{
		int wide, tall;
		getPaintSize( wide, tall );
		drawSetColor( RCC_SCRIM.r, RCC_SCRIM.g, RCC_SCRIM.b, 255 - RCC_SCRIM.a );
		drawFilledRect( 0, 0, wide, tall );
	}

private:
	// the lobby's data changes only when the value does
	void SetData( const char *pszKey, const char *pszValue )
	{
		if ( strcmp( RcLobby_Data( pszKey ), pszValue ) )
			RcLobby_SetData( pszKey, pszValue );
	}

	Panel *AddPage( void )
	{
		Panel *p = new Panel( 0, 0, 10, 10 );
		p->setParent( m_pFrame );
		p->setBgColor( 0, 0, 0, 255 );
		return p;
	}

	CRcUiText *AddText( Panel *pParent, const char *pszText, int font, const RcUiColor &color )
	{
		CRcUiText *p = new CRcUiText( pszText, 0, 0, 10, 10, font, color );
		p->setParent( pParent );
		return p;
	}

	CRcUiButton *AddButton( Panel *pParent, const char *pszText, int style, int font, int cmd )
	{
		CRcUiButton *p = new CRcUiButton( pszText, 0, 0, 10, 10, style, font );
		p->setParent( pParent );
		p->addActionSignal( new CRcCoopHandler( cmd ) );
		return p;
	}

	CRcUiFrame *m_pFrame;
	Panel *m_pGame, *m_pSet;
	CRcUiButton *m_pClose, *m_pTabGame, *m_pTabSet, *m_pSkill[4], *m_pNew, *m_pContinue, *m_pInvite, *m_pLeave, *m_pStart,
		*m_pLaunch, *m_pCampPrev, *m_pCampNext, *m_pSetPrev[RC_SET_ALL], *m_pSetNext[RC_SET_ALL];
	CRcUiText *m_pPlayersCap, *m_pName[4], *m_pTag[4], *m_pCampCap, *m_pCampName, *m_pSkillCap, *m_pGameCap, *m_pStatus,
		*m_pHostCap, *m_pOwnCap, *m_pSetLabel[RC_SET_ALL], *m_pSetValue[RC_SET_ALL];
	int m_iGame;	// 0 a new game, 1 the last co-op save
	bool m_bSave;
	char m_szSave[32];
	float m_flScale;
	int m_iTab;		// 0 Game, 1 Co-op settings
	int m_iSet[RC_SET_ALL];	// each setting's value, as an index into its list
	bool m_bLookSet;	// the host chose Simon or the police officers himself
	RcLobbyCampaign_t m_Campaign[RC_MAX_CAMPAIGNS];
	int m_nCampaigns, m_iCampaign;
	// B498: Cry of Fear's own co-op games' settings, Server list
	CRcUiButton *m_pServers, *m_pMpPrev[RC_SETMP], *m_pMpNext[RC_SETMP];
	CRcUiText *m_pMpLabel[RC_SETMP], *m_pMpValue[RC_SETMP];
	int m_iMp[RC_SETMP];	// each setting's value, as an index into its list
	bool m_bLaidClassic;	// the settings tab shows the rows of Cry of Fear's own games
	int m_iW, m_iServersY, m_iServersX[2];
};

void CRcCoopHandler::actionPerformed( Panel *panel )
{
	if ( !s_pRcPanel )
	{
		s_pRcPanel = new CRcCoopPanel();
		s_pRcPanel->setParent( gViewPort );
	}
	if ( m_cmd == RC_LOBBY_OPEN )
	{
		// the menu's entry: the lobby in place of the menu's entries (the viewport keeps the cursor for it,
		// RcCoopMenu_Visible)
		gEngfuncs.pfnPlaySoundByName( "ui/3d_click.wav", 0.5 );
		s_pRcPanel->Open( true );
		return;
	}
	s_pRcPanel->Command( m_cmd );
}

// TeamFortressViewport::UpdateCursorState: the panel needs the mouse cursor
bool RcCoopMenu_Visible( void )
{
	return s_pRcPanel && s_pRcPanel->isVisible();
}

// rc_coop_launchoptions: prints the Steam launch options the panel copies (B395)
void RcCoopMenu_LaunchOptions( void )
{
	char text[MAX_PATH + 32];
	if ( RcCoopLaunchOptions( text, sizeof( text ) ) )
		gEngfuncs.Con_Printf( "ReCoF co-op: Steam, Cry of Fear, Properties, Launch options: %s\n", text );
}

// rc_coop_menu: the Co-op panel, as the menu's entry opens it
void RcCoopMenu_Open( void )
{
	CRcCoopHandler open( 1 );
	open.actionPerformed( NULL );
}

// CGameMenu::CGameMenu: the entry (picture and button) at x, y
void RcCoopMenu_Create( CGameMenu *pMenu, int x, int y, int wide, int tall )
{
	s_pRcMenu = pMenu;
	s_pRcLabel = RcCoopLabel( 255, 255, 255 );
	s_pRcLabelArmed = RcCoopLabel( 217, 0, 0 );

	s_pRcItem = new CRcImagePanel( s_pRcLabel );
	s_pRcItem->setParent( pMenu );
	s_pRcItem->setPos( x, y );
	s_pRcItem->setVisible( true );

	s_pRcItemButton = new ColorButton( "", x, y, wide, tall, false, true );
	s_pRcItemButton->setParent( pMenu );
	s_pRcItemButton->addActionSignal( new CRcCoopHandler( 1 ) );
}

// CGameMenu::ResetButtonPositions_guess
void RcCoopMenu_SetPos( int x, int y, int wide, int tall )
{
	if ( s_pRcItem )
		s_pRcItem->setPos( x, y );
	if ( s_pRcItemButton )
		s_pRcItemButton->setBounds( x, y, wide, tall );	// B429: the click area follows the picture
}

// the menu shows or hides its entries (the custom campaign list hides them)
void RcCoopMenu_Show( bool bShow )
{
	if ( s_pRcItem )
		s_pRcItem->setVisible( bShow );
	if ( s_pRcItemButton )
		s_pRcItemButton->setVisible( bShow );
}

// B408: rc_coop_lobby, the game menu's Story co-op: the lobby, on the main menu (its map c_game_menu1, which the
// game menu's 3D menu loads; from a game or the game menu's own screen it is loaded first)
static bool s_bRcLobbyOnMenu;

void RcCoopMenu_Lobby( void )
{
	const char *pszLevel = gEngfuncs.pfnGetLevelName();
	if ( s_pRcMenu && pszLevel && strstr( pszLevel, "game_menu" ) )
	{
		RcCoopMenu_Open();
		return;
	}
	s_bRcLobbyOnMenu = true;
	gEngfuncs.pfnClientCmd( "disconnect;maxplayers 1;deathmatch 0;map c_game_menu1\n" );
}

// B421: Steam brought this player into a lobby (an invite accepted, Join Game in the friends list, connect_lobby)
// while he plays: the lobby opened only once he went back to the menu by himself. He leaves that game for the menu,
// which opens the lobby (RcCoopMenu_Solve below, the map's own menus closed by the map change); in the lobby's own
// game already, he stays
void RcCoopMenu_LeaveForLobby( unsigned long long ulLobby )
{
	const char *pszLevel = gEngfuncs.pfnGetLevelName();
	if ( !pszLevel || !pszLevel[0] || strstr( pszLevel, "game_menu" ) )
		return;	// on the menu, or no game: the menu opens the lobby itself
	if ( ulLobby == RcLobby_Id() && RcLobby_InItsGame() )
		return;
	gEngfuncs.pfnClientCmd( "disconnect;maxplayers 1;deathmatch 0;map c_game_menu1\n" );
}

// CGameMenu::solve, each frame: the red picture and the shake under the cursor, as the menu's other entries
void RcCoopMenu_Solve( void )
{
	if ( s_bRcLobbyOnMenu )
	{
		s_bRcLobbyOnMenu = false;
		RcCoopMenu_Open();
	}
	// B406: Steam brought this player into a lobby (an invite, Join Game, +connect_lobby): the menu shows it
	if ( RcLobby_WantPanel() )
	{
		if ( !s_pRcPanel )
		{
			s_pRcPanel = new CRcCoopPanel();
			s_pRcPanel->setParent( gViewPort );
		}
		s_pRcPanel->Open( false );
	}
	if ( !s_pRcItemButton || !s_pRcItem )
		return;
	if ( s_pRcItemButton->isArmed() )
	{
		if ( !s_bRcArmed )
		{
			s_pRcItem->setImage( s_pRcLabelArmed );
			gEngfuncs.pfnPlaySoundByName( "ui/3d_rollover.wav", 0.5 );
			s_bRcArmed = true;
			s_pRcItem->getPos( s_iRcArmedX, s_iRcArmedY );
		}
		else
			s_pRcItem->setPos( s_iRcArmedX + gEngfuncs.pfnRandomLong( -3, 3 ), s_iRcArmedY );
	}
	else if ( s_bRcArmed )
	{
		s_pRcItem->setImage( s_pRcLabel );
		s_bRcArmed = false;
		if ( s_pRcMenu )
			s_pRcMenu->ResetButtonPositions_guess();
	}
}

// MsgGameMenu_guess: the menu comes back with every picture unlit
void RcCoopMenu_Reset( void )
{
	if ( s_pRcItem && s_pRcLabel )
		s_pRcItem->setImage( s_pRcLabel );
	s_bRcArmed = false;
}
