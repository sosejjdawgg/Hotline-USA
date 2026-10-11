// Logging and thin wrappers around GTA 2's own functions (addresses: sheets/hooks.json).
#include "core.h"
#include <stdio.h>
#include <string.h>

char g_modDir[MAX_PATH];
char g_hmDir[MAX_PATH];
bool g_testMode;
bool g_autostart;
static FILE* g_log;

void logf(const char* fmt, ...) {
    if (!g_log) {
        char path[MAX_PATH];
        snprintf(path, sizeof path, "%s\\HotlineCity.log", g_modDir);
        g_log = fopen(path, "w");
        if (!g_log) return;
    }
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a);
    fputc('\n', g_log); fflush(g_log);
}

int setting(const char* section, const char* key, int def) {
    char ini[MAX_PATH];
    snprintf(ini, sizeof ini, "%s\\HotlineCity.ini", g_modDir);
    return GetPrivateProfileIntA(section, key, def, ini);
}

namespace gta {

const char* mapName() {
    // Menu struct at 0x5EC070: the .gmp file name of the level being played (e.g. "data\wil.gmp")
    static char buf[64];
    const char* s = (const char*)0x5EC075;
    size_t n = strnlen(s, 60);
    for (size_t i = 0; i < n; i++) buf[i] = (char)tolower((unsigned char)s[i]);
    buf[n] = 0;
    if (strstr(buf, "wil")) return "wil";
    if (strstr(buf, "ste")) return "ste";
    if (strstr(buf, "bil")) return "bil";
    return "";
}

int heading(uintptr_t ped) { return at<uint16_t>(ped + PED_HEADING_A) % 1440; }

void setHeading(uintptr_t ped, int h) {
    h = (h % 1440 + 1440) % 1440;
    at<uint16_t>(ped + PED_HEADING_A) = (uint16_t)h;
    at<uint16_t>(ped + PED_HEADING_B) = (uint16_t)h;
    uintptr_t obj = at<uintptr_t>(ped + PED_GAMEOBJ);
    if (obj) {
        at<uint16_t>(obj + OBJ_ROTATION) = (uint16_t)h;
        uintptr_t spr = at<uintptr_t>(obj + OBJ_SPRITE);
        if (spr) at<uint16_t>(spr + SPRITE_ROTATION) = (uint16_t)h;
    }
}

typedef uintptr_t(__stdcall* SpawnPed_t)(int, int, int, uint8_t, short);
typedef unsigned(__fastcall* AddWeapon_t)(uintptr_t, void*, unsigned, unsigned);
typedef void(__fastcall* SetPedWeapon_t)(uintptr_t, void*, unsigned);
typedef void(__fastcall* SetObjective_t)(uintptr_t, void*, unsigned, unsigned short);
typedef void(__fastcall* Die_t)(uintptr_t, void*);
typedef uintptr_t(__fastcall* SpawnObject_t)(uintptr_t, void*, int, int, int, int, short);
typedef unsigned(__fastcall* GivePowerUp_t)(uintptr_t, void*, int);
typedef void(__stdcall* DrawText_t)(const wchar_t*, int, int, short, int, int*, int, int, int);
typedef void(__fastcall* WorldToScreen_t)(uintptr_t, void*, int, int, int, int*, int*);
typedef void(__fastcall* SetMusicVol_t)(uintptr_t, void*, uint8_t);
typedef uint8_t(__fastcall* GetMusicVol_t)(uintptr_t, void*);

uintptr_t spawnPed(int x, int y, int z, int remap) { return ((SpawnPed_t)0x43DB40)(x, y, z, (uint8_t)remap, 0); }
void giveWeapon(uintptr_t ped, int w, int ammo) { if (ped) ((AddWeapon_t)0x43E4B0)(ped, 0, w, ammo); }
void holdWeapon(uintptr_t ped, int w) { if (ped) ((SetPedWeapon_t)0x43D830)(ped, 0, w); }
void huntPlayer(uintptr_t ped) {
    uintptr_t pp = playerPed();
    if (!ped || !pp) return;
    // objective first, target second: SetObjective clears the target (crash at 0x433B40 otherwise)
    ((SetObjective_t)0x43BBC0)(ped, 0, 20 /* KILL_CHAR_ON_FOOT */, 9999);
    at<uintptr_t>(ped + PED_TARGET_PED) = pp;
    at<int>(ped + PED_THREAT_REACT) = 2; // react as normal: fight back
}
void standStill(uintptr_t ped) { if (ped) ((SetObjective_t)0x43BBC0)(ped, 0, 26 /* WAIT_ON_FOOT */, 9999); }
void guardSpot(uintptr_t ped) {
    if (!ped) return;
    ((SetObjective_t)0x43BBC0)(ped, 0, 24 /* GUARD_SPOT */, 9999);
    at<int>(ped + PED_THREAT_SEARCH) = 6; // line of sight, player only
    at<int>(ped + PED_THREAT_REACT) = 2;  // react as normal: shoot
}
void killPed(uintptr_t ped) { if (ped) ((Die_t)0x4411B0)(ped, 0); } // through our PedDie hook, so kills still count
uintptr_t spawnObject(int type, int x, int y, int z) {
    uintptr_t s33 = at<uintptr_t>(0x665788);
    return s33 ? ((SpawnObject_t)0x4852E0)(s33, 0, type, x, y, z, 0) : 0;
}
void givePowerUp(int type) { uintptr_t p = player(); if (p) ((GivePowerUp_t)0x4A5780)(p, 0, type); }

void drawText(const wchar_t* s, float x, float y, float scale, int remap) {
    uintptr_t cam = mainCamera();
    short font = at<short>(0x672F24);
    if (!cam || !font || !s) return;
    int ui = at<int>(cam + 0xA8); // camera uiScale (SCR_f)
    auto mul = [&](float v) { return (int)(((int64_t)(int)(v * 16384.0f) * ui) >> 14); };
    int pal = remap < 0 ? 2 /* SPRITE */ : 8 /* FONT_REMAP */;
    ((DrawText_t)0x4CC100)(s, mul(x), mul(y), font, mul(scale), &pal, remap < 0 ? 0 : remap, 0, 1);
}

bool worldToScreen(int x, int y, int z, float* sx, float* sy) {
    uintptr_t cam = mainCamera();
    if (!cam) return false;
    int ox = 0, oy = 0;
    ((WorldToScreen_t)0x40CFC0)(cam, 0, x, y, z, &ox, &oy);
    *sx = ox / 16384.0f * uiWidth() / 640.0f; *sy = oy / 16384.0f; // game answers in a 640x480 space
    return true;
}

float uiWidth() {
    RECT rc; GetClientRect(at<HWND>(0x673D18), &rc);
    return rc.bottom > 0 ? 480.0f * rc.right / rc.bottom : 640.0f; // GTA 2 UI is 480 tall; width follows the aspect (widescreen fix)
}

// Calibrated in the lab: see sheets/hooks.json WorldToScreen evidence.
bool screenOf(int x, int y, int z, float* px, float* py) {
    float ux, uy;
    if (!worldToScreen(x, y, z, &ux, &uy)) return false;
    RECT rc; GetClientRect(window(), &rc);
    *px = ux * rc.right / uiWidth(); *py = uy * rc.bottom / 480.0f;
    return true;
}
bool playerScreen(float* px, float* py) {
    uintptr_t pp = playerPed();
    return pp && screenOf(at<int>(pp + PED_X), at<int>(pp + PED_Y), at<int>(pp + PED_Z), px, py);
}
float spriteScale() { return 1.0f; }

void addMoney(int amount) {
    uintptr_t p = player();
    if (p) at<uint32_t>(p + PLAYER_MONEY) += amount; // SaveSlotAnimatedValue.value; the HUD animates to it
}

void setMusicVolume(int v) { ((SetMusicVol_t)0x410600)(0x5D85A0, 0, (uint8_t)v); }
int musicVolume() { return ((GetMusicVol_t)0x410630)(0x5D85A0, 0); }

} // namespace gta
