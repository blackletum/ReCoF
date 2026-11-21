# OpenVR SDK 2.15.6 (ReCoF VR)

From https://github.com/ValveSoftware/openvr (tag v2.15.6), BSD-3-Clause, see LICENSE. ReCoF uses `headers/openvr.h` (client.dll, src/cl_dll/rc_vr.cpp) and ships `bin/win32/openvr_api.dll` next to cof.exe (tools/make_dist.py). The DLL is not in the repository (binaries are ignored): put it in `bin/win32/` from the tag above, SHA-256 ab696e4f218a95b3e396bc310f9fe6485df48c99c0969762083212b1e1f025a6.

client.dll loads it with LoadLibrary only when the game starts with `-vr` or `-openvr`, so flat play never touches it.
