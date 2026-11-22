# VR (work in progress)

Cry of Fear in a headset through SteamVR, on the `openvr` branch. It only switches on when the game starts with `-vr` (or `-openvr`): without it client.dll never loads `openvr_api.dll` and every VR hook returns at once, so flat play, its settings in config.cfg and the speedrun tools are unchanged. The first headset tests are with a Meta Quest 3S (2026-11-21). The bug rows are B510 to B539 in [bugs.md](bugs.md); the design is in the research report (`I:\COFPP\reports\ReCoF VR version research.md`, local).

## Starting it

1. Start Steam, then SteamVR. A Quest connects through Steam Link (free, wireless), Quest Link (cable or Air Link) with SteamVR, or Virtual Desktop in its SteamVR mode. Set the headset to 72 or 90 Hz: the game runs at most 100 frames a second.
2. In the game folder, double-click `Play ReCoF VR.bat` (windowed 1280 x 720 with `-vr -novid`). `openvr_api.dll` must be next to `cof.exe`. If Steam runs as administrator (for example started from an administrator PowerShell), start the .bat from that same shell: a game started without administrator rights cannot reach that Steam and stops with "Failed to initialize authentication interface".
3. The window on the desktop shows the left eye with the HUD over it. The console goes to `qconsole.log` next to `cof.exe`; its `ReCoF VR:` lines say what the headset reported and what went wrong.

The HUD and the menus have the window's shape in the headset, so a 16:9 window gives a 16:9 screen.

## Controls (Quest Touch layout)

| Control | In the game | With a menu, the inventory or the console open |
|---|---|---|
| Left stick | Move, relative to the head (`rc_vr_movehand 1`: to the left hand) | |
| Left stick click | Sprint (hold) | |
| Right stick left or right | Snap turn (`rc_vr_snapturn` degrees; 0 for smooth turning at `rc_vr_turnspeed`) | |
| Right stick up or down | Jump or crouch | |
| Right trigger | Fire; a melee weapon also attacks when the right hand swings | Click where the laser points |
| Left trigger | Secondary attack (aim down the sights) | |
| Right grip | Use; at a holster, take its quick slot | |
| Left grip | Dodge; with the left hand in front of the gun, hold it with both hands | |
| A | Reload | |
| B | Next quick slot | Back (Escape) |
| X | Inventory | Close the inventory |
| Y | Game menu (Escape) | Back (Escape) |
| Both grips held 1.5 s | Recenter (also `rc_vr_recenter`) | |

Holsters: the right hand at the right hip, the left hip or over the right shoulder, then the right grip, takes quick slot 1, 2 or 3; the controller buzzes when the hand comes into one. Crouching for real crouches the player. On a ladder and under water the head gives the way up and down.

## What it does

- Each eye is drawn on its own with the headset's projection, through the engine's own view loop; 4x multisampling; the second eye repeats the first one's picture without running the frame's effects again.
- The head moves the eyes from where the game puts the player's eye (leaning a little is allowed, walking away from the body is not); a scene's camera turns the world while the head looks round in it.
- The right hand holds the weapon model: its wrist sits on the controller and the arms are folded away. The left controller has its own hand, drawn from the weapon model's left hand, or the flashlight or lantern. Weapon models without a left hand, and empty hands, use Simon's hands from `v_actions.mdl`.
- Shots leave from the gun in the hand and go where it points (the server takes the gun's position from each command and never fires through a wall between the eye and the hand): a red dot marks the spot. No 2D crosshair or sniper scope overlay in VR.
- The HUD hangs in front of the head with its dark parts see-through; menus, the inventory, keypads and the console are on a fixed panel, used with a laser from the right hand. With the game menu, its dialogs or the console open, the panel shows the game flat with the menu over it, in a dark room.
- The weapon models are scaled round the wrist to a real forearm (Cry of Fear's are drawn far bigger than life, for the eye).
- No screen noise or zoom blur in VR.
- A button still held when a menu or a panel opens or closes counts only after it is let go (X that closes the inventory does not open it again, the trigger that clicks a menu does not fire).
- The VR textures take their names from the client's own range, so a level change or a loaded save cannot overwrite them.
- The grayscale of the black and white scenes and the screen fades are drawn in both eyes.
- The near plane is 5 cm, so a gun raised to the eye is not cut.
- Closing SteamVR during a game switches VR off and the game goes on flat.

## Settings

| Setting | Default | What it does |
|---|---|---|
| `rc_vr_aimpitch` | 35 | Degrees the gun points under the controller's own forward; change it if shots go above or under the controller's line |
| `rc_vr_grip_fwd`, `rc_vr_grip_right`, `rc_vr_grip_up` | -4, 0, -2 | Where the hand sits on the controller, in cm |
| `rc_vr_scale` | 37.5 | Game units a metre (the world's size) |
| `rc_vr_lean` | 24 | How far the head may lean away from the body, in units |
| `rc_vr_snapturn`, `rc_vr_turnspeed` | 45, 180 | Snap turn degrees (0: smooth), smooth turn degrees a second |
| `rc_vr_movehand` | 0 | 1: move towards where the left hand points |
| `rc_vr_hidearms` | 1 | 0 shows the weapon models' arms |
| `rc_vr_handscale` | 1 | The hands' size against a real one (1: a 26 cm forearm) |
| `rc_vr_lefthand`, `rc_vr_lefthand_item` | 1, 1 | The left hand, and the flashlight or lantern in it |
| `rc_vr_twohand` | 1 | Both hands on the gun with the left grip |
| `rc_vr_handshots` | 1 | Shots leave from the gun; 0: from the eye, aimed at the point the gun points at |
| `rc_vr_swing` | 2.2 | Metres a second a swing needs to hit with a melee weapon (0: off) |
| `rc_vr_holsters`, `rc_vr_hip_drop` | 1, 0.6 | The holsters, and how far under the eyes the hips are, in metres |
| `rc_vr_physcrouch` | 1 | Crouching for real crouches the player |
| `rc_vr_hud_dist`, `rc_vr_hud_width` | 1.6, 1.5 | The HUD screen's distance and width, in metres |
| `rc_vr_menu_dist`, `rc_vr_menu_width` | 1.8, 2.2 | The menu panel's |
| `rc_vr_renderscale` | 1 | The eye size against SteamVR's recommended one (read at start) |
| `rc_vr_msaa` | 4 | Multisampling samples (read at start; 0 for none) |
| `rc_vr_mirror` | 1 | The left eye in the desktop window |
| `rc_vr_vignette` | 0 | Darkens the edges of the view while the sticks move or turn the player (up to 1) |
| `rc_vr_debug` | 0 | 1 prints a line a second (frame rate, hands, pointer or game, the right stick, jump and crouch) |

`rc_vr_info` prints the state.

## Not done yet

- The blur and the water's screen copies work on the window, not the eyes.
- No wrist HUD, no OpenXR backend (OpenVR only, so SteamVR is needed).
- The game window takes the focus back every 2 s when another window has it, because the engine slows down while it is inactive.
