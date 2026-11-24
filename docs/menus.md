# Menus

ReCoF keeps Cry of Fear's picture main menu and gives its other windows one modern look: flat dark panels, plain text, one red accent and the Inter font, after cof-enhanced's (the user's choices of 2026-11-17). Bug rows B397 to B401 and B405 to B410 in [bugs.md](bugs.md).

## Look

- **The main menu** is Cry of Fear's pictures (white labels, red under the cursor), with a Co-op (Beta) entry made in the same look that opens the co-op lobby ([coop.md](coop.md)). A text version of it and of the New Game screen is in the code behind `rc_menu_text 1` (console only).
- **Every GameUI window** (Options, the console, the server list, the Esc menu) uses one dark theme in Cry of Fear's red: dark panels, light grey text in three steps, red for the selected tab, the hovered entry and the checks. The windows are flat as cof-enhanced's: the title in capitals on a band across the window, text tabs with a dark red fill and a red line under the chosen one, and a hairline instead of the 3D frames round buttons, boxes and lists (B413). It is set in GameUI.dll's code (src/gameui/RcTheme.cpp), so a mod that rewrites resource/trackerscheme.res no longer changes ReCoF's menus.
- **The font** of those windows is Inter (SIL Open Font License, src/gameui/fonts), carried inside GameUI.dll and registered for the game's process only; nothing is installed or added to the game folder.

## Size

Everything grows with the screen's height from 720 lines up: a window, its text and its controls take their 720p size times the screen's height over 720, so 720p and smaller screens look as before and 1080p, 1440p and 4K keep the same proportions. Game, General, **Menu size** (`rc_menu_scale`, 100 to 200 percent) makes them larger still, as far as they fit on the screen. The main menu's text is a fraction of the screen's height as well, and Cry of Fear's picture menu grows the same way (its pictures are drawn larger).

## Options

Each option says what it does in one line at the bottom of the window while the mouse is over it; nothing else is written on the pages. The Options window holds the settings that were spread over other screens: subtitle language and developer commentary (Game, General), the story co-op host's rules and the voice (Multiplayer, Co-op), and ReCoF's own key actions on the Keyboard page (duck-tap, skip a scene, the run timer, Options). `rc_options` opens it from the console or a key.

## Game menu

The menu Esc opens in a game (and the list after leaving one) keeps Cry of Fear's entries with short names: Resume game, New game, Load game, Main menu (the main menu's map), Leave game, Unlockables, Find servers, Host server, Story co-op (the lobby on the main menu), Options and Quit. They are renamed in code by their command, so the game's GameMenu.res stays as it is.

## Not done yet

- Cry of Fear's other picture screens (difficulty, save and load, language, unlockables, extras) and the in-game panels (inventory, notes) still have their fixed sizes.
- A pause menu over the live game, gamepad navigation and translations of ReCoF's own texts are not done.
- The launcher writes its own values into config.cfg at every start, so an option set there wins over the same option changed in the game.
