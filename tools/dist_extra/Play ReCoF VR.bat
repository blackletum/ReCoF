@echo off
rem ReCoF VR test launch (the openvr branch): Cry of Fear in a headset through SteamVR. Start Steam and
rem SteamVR first (a Quest: Steam Link, Quest Link or Virtual Desktop into SteamVR, the headset at 72 or 90 Hz),
rem then double-click this file. The window on the desktop shows the left eye. The console goes to
rem qconsole.log next to cof.exe: send it if something goes wrong.

rem The desktop window's size (the HUD and the menus have this shape in the headset)
set WIDTH=1280
set HEIGHT=720

rem More launch options
set EXTRA=-novid

cd /d "%~dp0"
start "" cof.exe -basedir cryoffear -game cryoffear -hw -heapsize 1536000 -no3dmenu -condebug -windowed -w %WIDTH% -h %HEIGHT% -vr %EXTRA%
