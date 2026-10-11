# Recommended display settings

Hotline Anywhere City makes GTA 2's window borderless and the size of your monitor by itself
(`[display] borderless=1` in `HotlineCity.ini`, the default). For a sharp picture, also make GTA 2
render at your desktop resolution:

- **GTA2.WidescreenFix.ini** (scripts folder): `ResX = 0` and `ResY = 0` (0 = desktop size).
  If your copy of the fix has a `WindowedMode` option, set `WindowedMode = 1` and delete `wndmode.ini`.
  If it has a `[CAMERA]` section, set `RotateWithPlayer = 0`. Mouse aiming expects north at the top of
  the screen, and a turned camera would send shots the wrong way.
- **wndmode.ini** (only if you still use it): `Border=0` under `[WINDOWMODE]`.

If the mod logs `display: the window keeps its frame`, the window wrapper is putting the frame back;
apply one of the two settings above.
