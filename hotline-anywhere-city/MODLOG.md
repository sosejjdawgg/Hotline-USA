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

## Session 2 (cloud, no game access): fixes for the user's bug report
Source recovered from the user's zip and committed. GTA 2 internals read from the gta2_re decompilation
(CriminalRETeam/gta2_re, 10.5 source + Scripts/ida/functions_data.json 9.6f address pairs; every
lab-verified hook address here matches its pairing, so 9.6f = this Steam build).
- 8-direction shooting: the old ShootWeapon hook (0x4D0530) is not a function start in the 9.6f function
  list, so shots used the movement heading (8-way on WASD). Replaced by a hook on Ped::ManageWeapon
  (9.6f 0x445230). In 10.5 that runs in Ped's update before UpdateCharB4 (the walk), and for the player it
  copies sprite rotation into ped+0x12E. Pistol aims with char+0x40, SMG/shotgun with ComputeAimAngle
  (facing +0x12C, auto-aim cone). The hook faces the aim just for that step, so Jacket strafes.
- Old player model under Jacket: hook SpriteTree_4::AddSprite (9.6f 0x4C4BF0) and drop the player's
  sprite (shadow included) while Jacket was drawn in the last 300 ms.
- Both new hooks are checked at runtime: they must sit on a function boundary and contain the expected
  bytes (sheet prologue "~..."). The first 16 bytes are logged. If a check fails, strafing falls back
  to "face the aim while firing" and the sprite stays visible.
- Crosshair + lock-on: drawn in EndScene (render.cpp). The crosshair follows the mouse, or sits 110 px out
  along the right stick. Lock: Shift / middle click / LT / R3 locks the enemy nearest the crosshair
  (within 12 blocks); pressing again switches or releases. While locked, aim follows the target.
  The mouse aim is now measured from Jacket's screen position (it was the window centre).
- Overlay is drawn in render-target pixels (client coords scaled), so it stays in place if the window
  and GTA 2's resolution differ.
- Borderless: display.cpp keeps the window frameless and monitor-sized (HotlineCity.ini [display]
  borderless=1, default). It gives up after 8 tries if a wrapper keeps restyling. The keep-running-in-
  background patch (0x4CB6F0) now applies whenever borderless is on. Recommended fix/wrapper settings:
  config/README.md.
- Built here with zig 0.13.0 (linux). NOT run in game yet. Next lab session: check the log lines
  "hook ManageWeapon/SpriteTreeAdd: first bytes ..." and "render: frame WxH in a WxH window", then test
  strafing with each gun, lock-on and Alt+Tab.
