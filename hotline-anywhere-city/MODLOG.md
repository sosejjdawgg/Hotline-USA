# MODLOG — Hotline Anywhere City (GTA 2 × Hotline Miami)

## Paths
- GTA 2 (host): D:\STEAM INSTALL DIRECTORY\steamapps\common\Grand Theft Auto 2 — gta2.exe 9.6.0.0 (Steam), base 0x400000
- Hotline Miami (companion): D:\STEAM INSTALL DIRECTORY\steamapps\common\hotline_miami — GameMaker GL port, HotlineMiami_GL.wad + loose .ogg music

## Lab install (files ADDED to GTA 2 folder; nothing overwritten). Restore = delete these:
- dinput8.dll (Ultimate ASI Loader v9.7.4, MIT)
- scripts/GTA2.WidescreenFix.asi + .ini (ThirteenAG, MIT)
- scripts/probe.asi / scripts/HotlineCity.asi (ours)

## Route
Native hooks: UAL dinput8 proxy loads our .asi; MinHook detours on known 9.6 addresses (same layout as 11.44,
verified by disassembly). HM content read at runtime from the player's HM folder (HAC_HM_DIR env from Melty).

## Status at pause (15:05)
- Working in game: hooks, HM text/atlas/music from player's copy, one-hit lethality (bullets), executions via stomp (seen live), combo/score HUD, mask menu at payphones + perks, armed NPCs, HM jobs (placed on pavement, cleared live by user), Jacket + mask icons rendered via D3D EndScene, widescreen (UAL dinput8 + ThirteenAG WidescreenFix + wndmode.ini, 1280x720 tested).
- Open: scripted test mode never fires the player's attack (input::force). Caller 0x4A7683 decodes keys then reads/zeroes player+4 itself — investigate what fire code reads (likely key flags at +0x7C edge vs held) before trusting tests.
- Not done: Melty connect steps (inspect_package, create_mod, upload, recipe with companion HAC_HM_DIR={game:custom-hotline-miami}, test_release, screenshot, publish). Lab files in GTA2 folder: dinput8.dll, wndmode.ini, scripts/* (remove before Melty install test).
