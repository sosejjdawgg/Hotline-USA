// Borderless window (HotlineCity.ini [display] borderless=1, the default): GTA 2's window loses its
// frame and covers the monitor it is on, so Alt+Tab and other windows come and go instantly.
// Works with GTA 2's own windowed mode, the ASI loader's wndmode wrapper and the widescreen fix.
#include "core.h"

namespace display {

static DWORD WINAPI keepBorderless(void*) {
    int fixes = 0;
    bool gaveUp = false;
    for (;;) {
        Sleep(500);
        HWND w = gta::window();
        if (!w || !IsWindow(w) || !IsWindowVisible(w) || IsIconic(w)) continue;
        MONITORINFO mi = {sizeof mi};
        if (!GetMonitorInfoA(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &mi)) continue;
        LONG style = GetWindowLongA(w, GWL_STYLE), ex = GetWindowLongA(w, GWL_EXSTYLE);
        RECT wr; GetWindowRect(w, &wr);
        const LONG frame = WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME;
        if (!(style & frame) && EqualRect(&wr, &mi.rcMonitor)) { fixes = 0; continue; }
        if (gaveUp) continue;
        if (++fixes > 8) {
            // something else keeps putting the frame back; stop fighting it
            gaveUp = true;
            logf("display: the window keeps its frame; set Border=0 in wndmode.ini or WindowedMode=1 in GTA2.WidescreenFix.ini");
            continue;
        }
        SetWindowLongA(w, GWL_STYLE, (style & ~(frame | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) | WS_POPUP | WS_VISIBLE);
        SetWindowLongA(w, GWL_EXSTYLE, ex & ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE));
        const RECT& m = mi.rcMonitor;
        SetWindowPos(w, HWND_NOTOPMOST, m.left, m.top, m.right - m.left, m.bottom - m.top,
                     SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_ASYNCWINDOWPOS);
        logf("display: borderless %ldx%ld at %ld,%ld", m.right - m.left, m.bottom - m.top, m.left, m.top);
    }
    return 0;
}

void init() {
    if (!setting("display", "borderless", 1)) { logf("display: borderless off (HotlineCity.ini)"); return; }
    CreateThread(nullptr, 0, keepBorderless, nullptr, 0, nullptr);
}

} // namespace display
