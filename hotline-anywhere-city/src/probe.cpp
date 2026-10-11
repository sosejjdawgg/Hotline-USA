// Probe build: verifies the loader chain, logs damage/death call sites, and runs a lab console
// (lab.cmd next to the .asi is polled every few ticks; replies go to hac-probe.log).
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "MinHook.h"

static FILE* g_log;
static char g_dir[MAX_PATH];
static void logf(const char* fmt, ...) {
    if (!g_log) return;
    va_list a; va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a);
    fputc('\n', g_log); fflush(g_log);
}

template <class T> static T rd(uintptr_t p) { return *(T*)p; }

static uintptr_t gamePtr() { return rd<uintptr_t>(0x5EB4FC); }
static uintptr_t playerPtr() { uintptr_t g = gamePtr(); return g ? rd<uintptr_t>(g + 0x38) : 0; }
static uintptr_t playerPed() { uintptr_t p = playerPtr(); return p ? rd<uintptr_t>(p + 0x2C4) : 0; }

typedef void*(__stdcall* SpawnPed_t)(int x, int y, int z, uint8_t remap, short p5);
typedef void(__fastcall* SetPedWeapon_t)(void* ped, void* edx, unsigned weapon);
typedef unsigned(__fastcall* AddWeapon_t)(void* ped, void* edx, unsigned weapon, unsigned ammo);
typedef void(__fastcall* SetObjective_t)(void* ped, void* edx, unsigned obj, unsigned short timer);
typedef void(__fastcall* Teleport_t)(void* ped, void* edx, int x, int y);

static void dump(uintptr_t a, int n) {
    char line[256]; int k = 0;
    for (int i = 0; i < n; i++) {
        if (i % 16 == 0) { if (i) logf("%s", line); k = sprintf(line, "%08x:", (unsigned)(a + i)); }
        k += sprintf(line + k, " %02x", rd<uint8_t>(a + i));
    }
    logf("%s", line);
}

static uintptr_t resolve(const char* s) {
    if (!strcmp(s, "ped")) return playerPed();
    if (!strcmp(s, "player")) return playerPtr();
    if (!strcmp(s, "game")) return gamePtr();
    if (!strcmp(s, "obj")) { uintptr_t p = playerPed(); return p ? rd<uintptr_t>(p + 0x168) : 0; }
    if (!strcmp(s, "spr")) { uintptr_t p = playerPed(); uintptr_t o = p ? rd<uintptr_t>(p + 0x168) : 0; return o ? rd<uintptr_t>(o + 0x80) : 0; }
    char* plus = (char*)strchr(s, '+');
    if (plus) { *plus = 0; return resolve(s) + strtoul(plus + 1, 0, 16); }
    return strtoul(s, 0, 16);
}

static void* g_lastSpawn;
static void runLab() {
    char path[MAX_PATH]; sprintf(path, "%s\\lab.cmd", g_dir);
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char c[32] = {}, a[64] = {}, b[64] = {}, d[64] = {}, e[64] = {};
        if (sscanf(line, "%31s %63s %63s %63s %63s", c, a, b, d, e) < 1) continue;
        logf("> %s", line);
        if (!strcmp(c, "r")) dump(resolve(a), b[0] ? strtol(b, 0, 0) : 64);
        else if (!strcmp(c, "w8")) *(uint8_t*)resolve(a) = (uint8_t)strtoul(b, 0, 0);
        else if (!strcmp(c, "w16")) *(uint16_t*)resolve(a) = (uint16_t)strtoul(b, 0, 0);
        else if (!strcmp(c, "w32")) *(uint32_t*)resolve(a) = (uint32_t)strtoul(b, 0, 0);
        else if (!strcmp(c, "spawn")) {
            uintptr_t p = playerPed();
            int x = rd<int>(p + 0x1AC) + strtol(a, 0, 0), y = rd<int>(p + 0x1B0) + strtol(b, 0, 0), z = rd<int>(p + 0x1B4);
            g_lastSpawn = ((SpawnPed_t)0x43DB40)(x, y, z, (uint8_t)strtoul(d, 0, 0), 0);
            logf("spawned %p", g_lastSpawn);
        } else if (!strcmp(c, "arm") && g_lastSpawn) {
            ((AddWeapon_t)0x43E4B0)(g_lastSpawn, 0, strtoul(a, 0, 0), 99);
            ((SetPedWeapon_t)0x43D830)(g_lastSpawn, 0, strtoul(a, 0, 0));
        } else if (!strcmp(c, "obj") && g_lastSpawn) {
            *(uintptr_t*)((uintptr_t)g_lastSpawn + 0x148) = playerPed();
            ((SetObjective_t)0x43BBC0)(g_lastSpawn, 0, strtoul(a, 0, 0), 9999);
        } else if (!strcmp(c, "give")) {
            ((AddWeapon_t)0x43E4B0)((void*)playerPed(), 0, strtoul(a, 0, 0), b[0] ? strtoul(b, 0, 0) : 99);
        }
    }
    fclose(f);
    DeleteFileA(path);
}

typedef void(__fastcall* GameTick_t)(void* game, void* edx);
static GameTick_t oGameTick;
static unsigned g_tick;
static void __fastcall hGameTick(void* game, void* edx) {
    oGameTick(game, edx);
    ++g_tick;
    if (g_tick % 10 == 0) runLab();
    if (g_tick % 300 == 0) {
        uintptr_t p = playerPed();
        if (p) logf("tick %u ped=%08x id=%d hp=%u state=%u car=%08x pos=%d,%d,%d rot=%d", g_tick, (unsigned)p, rd<int>(p + 0x200),
                    rd<uint16_t>(p + 0x216), rd<uint32_t>(p + 0x278), rd<uint32_t>(p + 0x16C), rd<int>(p + 0x1AC), rd<int>(p + 0x1B0),
                    rd<int>(p + 0x1B4), rd<uintptr_t>(p + 0x168) ? rd<short>(rd<uintptr_t>(p + 0x168) + 0x40) : -1);
    }
}

typedef void(__fastcall* Damage_t)(void* ped, void* edx, short amount);
static Damage_t oDamage;
static void __fastcall hDamage(void* ped, void* edx, short amount) {
    uintptr_t p = (uintptr_t)ped;
    logf("damage site=%p victim=%d occ=%u hp=%u amount=%d lastAttacker=%d", __builtin_return_address(0), rd<int>(p + 0x200),
         rd<uint32_t>(p + 0x240), rd<uint16_t>(p + 0x216), amount, rd<int>(p + 0x204));
    oDamage(ped, edx, amount);
}

typedef void(__fastcall* Die_t)(void* ped, void* edx);
static Die_t oDie;
static void __fastcall hDie(void* ped, void* edx) {
    uintptr_t p = (uintptr_t)ped;
    logf("die site=%p victim=%d occ=%u lastAttacker=%d", __builtin_return_address(0), rd<int>(p + 0x200), rd<uint32_t>(p + 0x240), rd<int>(p + 0x204));
    oDie(ped, edx);
}

typedef void(__fastcall* ReadDebug_t)(void* self, void* edx);
static ReadDebug_t oReadDebug;
static void __fastcall hReadDebug(void* self, void* edx) {
    oReadDebug(self, edx);
    char fl[MAX_PATH]; sprintf(fl, "%s\\lab-skipfrontend.flag", g_dir);
    if (GetFileAttributesA(fl) != INVALID_FILE_ATTRIBUTES) { *(uint8_t*)0x5EAD7C = 1; logf("skip frontend on"); }
}

static void init(HMODULE self) {
    GetModuleFileNameA(self, g_dir, MAX_PATH);
    char* s = strrchr(g_dir, '\\'); if (s) *s = 0;
    char path[MAX_PATH]; sprintf(path, "%s\\hac-probe.log", g_dir);
    g_log = fopen(path, "w");
    logf("probe loaded; exe stamp %08x", rd<uint32_t>(0x400000 + rd<uint32_t>(0x40003C) + 8));
    *(uint8_t*)0x595014 = 0; // windowed for the lab
    MH_Initialize();
    MH_CreateHook((void*)0x45C1F0, (void*)hGameTick, (void**)&oGameTick);
    MH_CreateHook((void*)0x4419E0, (void*)hDamage, (void**)&oDamage);
    MH_CreateHook((void*)0x4411B0, (void*)hDie, (void**)&oDie);
    MH_CreateHook((void*)0x451930, (void*)hReadDebug, (void**)&oReadDebug);
    logf("hooks: %d", MH_EnableHook(MH_ALL_HOOKS));
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); init(h); }
    return TRUE;
}
