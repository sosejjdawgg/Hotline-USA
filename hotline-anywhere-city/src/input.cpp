// Hotline Miami controls on foot (sheets/controls.json): WASD/left stick move in screen directions,
// mouse/right stick aim, click/RT attacks toward the aim, Shift/middle click/LT/R3 lock on to the
// enemy nearest the crosshair. In cars GTA 2's controls stay, plus a pad map.
// Input reaches GTA 2 through its own key decoder (DecodeKeys hook), never as fake OS input.
#include "core.h"
#include <xinput.h>

namespace input {

typedef DWORD(WINAPI* XInputGetState_t)(DWORD, XINPUT_STATE*);
static XInputGetState_t g_xget;
static XINPUT_STATE g_pad, g_padPrev;
static bool g_padOk;
static float g_moveX, g_moveY;      // -1..1 screen space (+y down)
static float g_aimX, g_aimY;        // aim vector, screen space
static bool g_aimActive, g_attack;
static bool g_kbPrev[256], g_kbNow[256];
static POINT g_lastMouse;
enum AimSource { AIM_NONE, AIM_MOUSE, AIM_PAD };
static AimSource g_source;          // the device that aimed last decides how the crosshair is drawn
static float g_mouseX, g_mouseY;    // cursor, client pixels
static float g_padX = 1, g_padY;    // last right stick direction (kept when the stick is released)
static uintptr_t g_lock; static int g_lockId; // locked-on ped, HM's Shift lock
static bool g_strafe;               // ManageWeapon hook is in: guns fire at the aim while walking elsewhere
static bool g_cursorHidden;
static int g_injected = -1;         // heading we forced this tick (-1 = none)
static int g_forceTicks, g_forceHeading = -1; static uint32_t g_forceKeys; // lab/test-mode scripted input

void force(uint32_t keys, int ticks, int heading) { g_forceKeys = keys; g_forceTicks = ticks; g_forceHeading = heading; }
void setStrafe(bool ok) { g_strafe = ok; }

void init() {
    HMODULE x = LoadLibraryA("xinput1_4.dll");
    if (!x) x = LoadLibraryA("xinput9_1_0.dll");
    if (x) g_xget = (XInputGetState_t)GetProcAddress(x, "XInputGetState");
    logf("input: xinput %s", g_xget ? "available" : "missing");
}

static HWND gameWindow() { return gta::window(); }
static bool focused() { return GetForegroundWindow() == gameWindow(); }
static bool key(int vk) { return g_kbNow[vk]; }
static bool keyHit(int vk) { return g_kbNow[vk] && !g_kbPrev[vk]; }
static bool padHit(WORD b) { return g_padOk && (g_pad.Gamepad.wButtons & b) && !(g_padPrev.Gamepad.wButtons & b); }
static float stick(SHORT v) { float f = v / 32767.0f; return fabsf(f) < 0.24f ? 0.0f : f; }

static bool lockable(uintptr_t p, uintptr_t pp) {
    if (!p || p == pp || at<uint16_t>(p + gta::PED_HEALTH) == 0 || at<int>(p + gta::PED_STATE) == gta::STATE_WASTED) return false;
    if (!at<uintptr_t>(p + gta::PED_GAMEOBJ) || at<uintptr_t>(p + gta::PED_CAR)) return false;
    float dx = gta::toBlocks(at<int>(p + gta::PED_X) - at<int>(pp + gta::PED_X)), dy = gta::toBlocks(at<int>(p + gta::PED_Y) - at<int>(pp + gta::PED_Y));
    return dx * dx + dy * dy < 12.0f * 12.0f;
}

// The ped nearest the crosshair, within reach of it.
static uintptr_t pickTarget(uintptr_t pp, float cx, float cy) {
    RECT r; GetClientRect(gameWindow(), &r);
    float reach = 140.0f * r.bottom / 480.0f, best = reach * reach;
    uintptr_t found = 0;
    gta::forEachPed([&](uintptr_t p) {
        float sx, sy;
        if (!lockable(p, pp) || !gta::screenOf(at<int>(p + gta::PED_X), at<int>(p + gta::PED_Y), at<int>(p + gta::PED_Z), &sx, &sy)) return;
        float d = (sx - cx) * (sx - cx) + (sy - cy) * (sy - cy);
        if (d < best) { best = d; found = p; }
    });
    return found;
}

static void updateLock(uintptr_t pp, bool onFoot, bool playerOnScreen, float px, float py) {
    if (g_lock && (!onFoot || at<int>(g_lock + gta::PED_ID) != g_lockId || !lockable(g_lock, pp))) g_lock = 0;
    bool press = keyHit(VK_SHIFT) || keyHit(VK_MBUTTON) || padHit(XINPUT_GAMEPAD_RIGHT_THUMB);
    static bool ltWas;
    bool lt = g_padOk && g_pad.Gamepad.bLeftTrigger > 80;
    if (lt && !ltWas && onFoot) press = true;
    ltWas = lt;
    if (!press || !onFoot || !playerOnScreen || masks::menuOpen()) return;
    float cx, cy; bool pad;
    if (!crosshair(&cx, &cy, &pad)) { cx = px; cy = py; }
    if (g_lock) { // HM: pressing again on another enemy switches to it, otherwise lets go
        float sx, sy;
        if (gta::screenOf(at<int>(g_lock + gta::PED_X), at<int>(g_lock + gta::PED_Y), at<int>(g_lock + gta::PED_Z), &sx, &sy)) { cx = sx; cy = sy; }
    }
    uintptr_t t = pickTarget(pp, cx, cy);
    if (t == g_lock && g_lock && g_source == AIM_MOUSE) t = pickTarget(pp, g_mouseX, g_mouseY);
    if (t && t != g_lock) { g_lock = t; g_lockId = at<int>(t + gta::PED_ID); logf("input: locked on ped %d", g_lockId); }
    else g_lock = 0;
}

// GTA 2 draws no cursor of its own; ours replaces the system arrow over the game.
static void updateCursor(bool hide) {
    if (hide && !g_cursorHidden) { while (ShowCursor(FALSE) >= 0) {} g_cursorHidden = true; }
    else if (!hide && g_cursorHidden) { while (ShowCursor(TRUE) < 0) {} g_cursorHidden = false; }
}

uintptr_t lockTarget() { return g_lock; }

// Where HM's crosshair goes, in client pixels: on the locked enemy, at the cursor, or out along the stick.
bool crosshair(float* x, float* y, bool* padAim) {
    uintptr_t pp = gta::playerPed();
    *padAim = g_source == AIM_PAD;
    if (!pp || at<uintptr_t>(pp + gta::PED_CAR)) return false;
    if (g_lock) return gta::screenOf(at<int>(g_lock + gta::PED_X), at<int>(g_lock + gta::PED_Y), at<int>(g_lock + gta::PED_Z), x, y);
    if (g_source == AIM_MOUSE) { *x = g_mouseX; *y = g_mouseY; return true; }
    if (g_source == AIM_PAD) {
        float px, py;
        if (!gta::playerScreen(&px, &py)) return false;
        RECT r; GetClientRect(gameWindow(), &r);
        float d = 110.0f * r.bottom / 480.0f;
        *x = px + g_padX * d; *y = py + g_padY * d;
        return true;
    }
    return false;
}

void tick() {
    memcpy(g_kbPrev, g_kbNow, sizeof g_kbNow);
    bool f = focused();
    for (int vk = 1; vk < 256; vk++) g_kbNow[vk] = f && (GetAsyncKeyState(vk) & 0x8000);
    g_padPrev = g_pad;
    g_padOk = g_xget && g_xget(0, &g_pad) == ERROR_SUCCESS && f;
    if (!g_padOk) memset(&g_pad, 0, sizeof g_pad);

    float mx = (key('D') || key(VK_RIGHT) ? 1.0f : 0.0f) - (key('A') || key(VK_LEFT) ? 1.0f : 0.0f);
    float my = (key('S') || key(VK_DOWN) ? 1.0f : 0.0f) - (key('W') || key(VK_UP) ? 1.0f : 0.0f);
    if (g_padOk) { mx += stick(g_pad.Gamepad.sThumbLX); my -= stick(g_pad.Gamepad.sThumbLY); }
    if (masks::has(Perk::reversed_controls)) { mx = -mx; my = -my; }
    g_moveX = mx; g_moveY = my;

    g_aimActive = false;
    uintptr_t pp = gta::playerPed();
    bool onFoot = pp && !at<uintptr_t>(pp + gta::PED_CAR);
    float px = 0, py = 0;
    bool playerOnScreen = onFoot && gta::playerScreen(&px, &py);
    float rx = g_padOk ? stick(g_pad.Gamepad.sThumbRX) : 0, ry = g_padOk ? -stick(g_pad.Gamepad.sThumbRY) : 0;
    POINT c; RECT r;
    GetCursorPos(&c);
    bool mouseMoved = c.x != g_lastMouse.x || c.y != g_lastMouse.y;
    g_lastMouse = c;
    ScreenToClient(gameWindow(), &c); GetClientRect(gameWindow(), &r);
    bool inside = c.x >= 0 && c.y >= 0 && c.x < r.right && c.y < r.bottom;
    if (f && inside) { g_mouseX = (float)c.x; g_mouseY = (float)c.y; }
    if (rx || ry) { g_source = AIM_PAD; float l = sqrtf(rx * rx + ry * ry); g_padX = rx / l; g_padY = ry / l; }
    else if (f && inside && mouseMoved) g_source = AIM_MOUSE;
    if (rx || ry) { g_aimX = rx; g_aimY = ry; g_aimActive = true; }
    else if (g_source == AIM_MOUSE && f && playerOnScreen) {
        // HM: Jacket always faces the cursor, measured from where he stands on screen
        float dx = g_mouseX - px, dy = g_mouseY - py;
        if (dx * dx + dy * dy > 16) { g_aimX = dx; g_aimY = dy; g_aimActive = true; }
    }
    g_attack = key(VK_LBUTTON) || key(VK_CONTROL) || (g_padOk && g_pad.Gamepad.bRightTrigger > 80);
    updateLock(pp, onFoot, playerOnScreen, px, py);
    updateCursor(f && inside && gta::isRunning());
}

bool aiming(int* heading) {
    if (g_forceTicks > 0) { if (heading && g_forceHeading >= 0) *heading = g_forceHeading; return g_forceHeading >= 0; } // scripted input wins
    uintptr_t pp = gta::playerPed();
    if (g_lock && pp) {
        if (heading) *heading = gta::headingFromVector((float)(at<int>(g_lock + gta::PED_X) - at<int>(pp + gta::PED_X)),
                                                       (float)(at<int>(g_lock + gta::PED_Y) - at<int>(pp + gta::PED_Y)));
        return true;
    }
    if (!g_aimActive) return false;
    if (heading) *heading = gta::headingFromVector(g_aimX, g_aimY);
    return true;
}

void menuInput(int* dir, bool* ok) {
    *dir = 0; *ok = false;
    if (keyHit(VK_LEFT) || keyHit('A') || padHit(XINPUT_GAMEPAD_DPAD_LEFT) || padHit(XINPUT_GAMEPAD_LEFT_SHOULDER)) *dir = -1;
    if (keyHit(VK_RIGHT) || keyHit('D') || padHit(XINPUT_GAMEPAD_DPAD_RIGHT) || padHit(XINPUT_GAMEPAD_RIGHT_SHOULDER)) *dir = 1;
    static DWORD stickAt;
    float sx = g_padOk ? stick(g_pad.Gamepad.sThumbLX) : 0;
    if (sx && GetTickCount() - stickAt > 250) { *dir = sx > 0 ? 1 : -1; stickAt = GetTickCount(); }
    *ok = keyHit(VK_RETURN) || keyHit(VK_SPACE) || keyHit(VK_LBUTTON) || padHit(XINPUT_GAMEPAD_A);
}

// Called with the player's raw GTA 2 key bitmask right before the game decodes it.
uint32_t filterKeys(uint32_t keys) {
    g_injected = -1;
    uintptr_t pp = gta::playerPed();
    if (!pp) return keys;
    bool inCar = at<uintptr_t>(pp + gta::PED_CAR) != 0;
    if (g_padOk) {
        WORD b = g_pad.Gamepad.wButtons;
        if (b & XINPUT_GAMEPAD_Y) keys |= gta::KEY_ENTER;
        if (b & XINPUT_GAMEPAD_A) keys |= gta::KEY_JUMP;
        if (b & XINPUT_GAMEPAD_LEFT_SHOULDER) keys |= gta::KEY_PREV;
        if (b & XINPUT_GAMEPAD_RIGHT_SHOULDER) keys |= gta::KEY_NEXT;
    }
    if (inCar) {
        if (g_padOk) {
            float sx = stick(g_pad.Gamepad.sThumbLX);
            if (sx < -0.3f) keys |= gta::KEY_LEFT;
            if (sx > 0.3f) keys |= gta::KEY_RIGHT;
            if (g_pad.Gamepad.bRightTrigger > 40) keys |= gta::KEY_UP;
            if (g_pad.Gamepad.bLeftTrigger > 40) keys |= gta::KEY_DOWN;
            if (g_pad.Gamepad.wButtons & XINPUT_GAMEPAD_X) keys |= gta::KEY_ATTACK;
        }
        return keys;
    }
    // On foot we own movement and attack.
    keys &= ~(gta::KEY_UP | gta::KEY_DOWN | gta::KEY_LEFT | gta::KEY_RIGHT | gta::KEY_ATTACK);
    float len = sqrtf(g_moveX * g_moveX + g_moveY * g_moveY);
    int aim;
    bool aimOn = aiming(&aim);
    if (len > 0.2f) {
        keys |= gta::KEY_UP;
        g_injected = gta::headingFromVector(g_moveX, g_moveY);
    }
    if (g_attack) {
        keys |= gta::KEY_ATTACK;
        // guns: the ManageWeapon hook turns the ped to the aim just for the shot, so Jacket can strafe;
        // fists and melee hit in front of the ped, so face the aim while swinging
        if (aimOn && (!at<uintptr_t>(pp + gta::PED_WEAPON) || !g_strafe || len <= 0.2f)) g_injected = aim;
    } else if (aimOn && len <= 0.2f) {
        g_injected = aim;            // standing still: turn to face the cursor
    }
    if (g_forceTicks > 0) { g_forceTicks--; keys = g_forceKeys; if (g_forceHeading >= 0) g_injected = g_forceHeading; }
    if (g_injected >= 0) gta::setHeading(pp, g_injected);
    return keys;
}

} // namespace input
