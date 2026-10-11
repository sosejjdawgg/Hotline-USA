// Hotline Miami controls on foot (sheets/controls.json): WASD/left stick move in screen directions,
// mouse/right stick aim, click/RT attacks toward the aim. In cars GTA 2's controls stay, plus a pad map.
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
static DWORD g_mouseMovedAt;
static POINT g_lastMouse;
static int g_injected = -1;         // heading we forced this tick (-1 = none)
static int g_forceTicks, g_forceHeading = -1; static uint32_t g_forceKeys; // lab/test-mode scripted input

void force(uint32_t keys, int ticks, int heading) { g_forceKeys = keys; g_forceTicks = ticks; g_forceHeading = heading; }

void init() {
    HMODULE x = LoadLibraryA("xinput1_4.dll");
    if (!x) x = LoadLibraryA("xinput9_1_0.dll");
    if (x) g_xget = (XInputGetState_t)GetProcAddress(x, "XInputGetState");
    logf("input: xinput %s", g_xget ? "available" : "missing");
}

static HWND gameWindow() { return at<HWND>(0x673D18); }
static bool focused() { return GetForegroundWindow() == gameWindow(); }
static bool key(int vk) { return g_kbNow[vk]; }
static bool keyHit(int vk) { return g_kbNow[vk] && !g_kbPrev[vk]; }
static bool padHit(WORD b) { return g_padOk && (g_pad.Gamepad.wButtons & b) && !(g_padPrev.Gamepad.wButtons & b); }
static float stick(SHORT v) { float f = v / 32767.0f; return fabsf(f) < 0.24f ? 0.0f : f; }

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
    float rx = g_padOk ? stick(g_pad.Gamepad.sThumbRX) : 0, ry = g_padOk ? -stick(g_pad.Gamepad.sThumbRY) : 0;
    if (rx || ry) { g_aimX = rx; g_aimY = ry; g_aimActive = true; }
    else if (f) {
        POINT c; RECT r;
        GetCursorPos(&c);
        if (c.x != g_lastMouse.x || c.y != g_lastMouse.y) g_mouseMovedAt = GetTickCount();
        g_lastMouse = c;
        ScreenToClient(gameWindow(), &c); GetClientRect(gameWindow(), &r);
        float dx = c.x - r.right * 0.5f, dy = c.y - r.bottom * 0.5f; // the camera keeps the player near the centre
        if (dx * dx + dy * dy > 64 && GetTickCount() - g_mouseMovedAt < 4000) { g_aimX = dx; g_aimY = dy; g_aimActive = true; }
    }
    g_attack = key(VK_LBUTTON) || key(VK_CONTROL) || (g_padOk && g_pad.Gamepad.bRightTrigger > 80);
}

bool aiming(int* heading) {
    if (g_forceTicks > 0) { if (heading && g_forceHeading >= 0) *heading = g_forceHeading; return g_forceHeading >= 0; } // scripted input wins
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
        // guns: the ShootWeapon hook turns each shot toward the aim, so Jacket can strafe;
        // fists and melee hit in front of the ped, so face the aim while swinging
        if (aimOn && (!at<uintptr_t>(pp + gta::PED_WEAPON) || len <= 0.2f)) g_injected = aim;
    } else if (aimOn && len <= 0.2f) {
        g_injected = aim;            // standing still: turn to face the cursor
    }
    if (g_forceTicks > 0) { g_forceTicks--; keys = g_forceKeys; if (g_forceHeading >= 0) g_injected = g_forceHeading; }
    if (g_injected >= 0) gta::setHeading(pp, g_injected);
    return keys;
}

} // namespace input
