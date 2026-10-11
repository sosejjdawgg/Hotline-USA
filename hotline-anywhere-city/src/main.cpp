// Hotline Anywhere City: GTA 2 played the Hotline Miami way, with Hotline Miami's own masks, text,
// sprites and soundtrack read from the player's copy (Melty passes its folder in HAC_HM_DIR).
#include "core.h"
#include "MinHook.h"
#include <stdio.h>
#include <string.h>
#include <initializer_list>

typedef void(__fastcall* Tick_t)(uintptr_t, void*);
typedef void(__fastcall* Damage_t)(uintptr_t, void*, short);
typedef void(__fastcall* Die_t)(uintptr_t, void*);
typedef void(__fastcall* Decode_t)(uintptr_t, void*);
typedef void(__fastcall* Shoot_t)(uintptr_t, void*);
typedef void(__fastcall* DrawUI_t)(uintptr_t, void*);
static Tick_t oTick; static Damage_t oDamage; static Die_t oDie; static Decode_t oDecode; static Shoot_t oShoot; static DrawUI_t oDrawUI;
static bool g_hmOk;

// Writes the faulting address, registers and the return addresses on the stack to the log once.
static LONG CALLBACK crashLogger(EXCEPTION_POINTERS* e) {
    static bool done;
    if (done || e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    done = true;
    CONTEXT* c = e->ContextRecord;
    logf("CRASH at %08lx reading %08lx: eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx",
         c->Eip, (unsigned long)e->ExceptionRecord->ExceptionInformation[1], c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    uint32_t* sp = (uint32_t*)c->Esp;
    char line[512]; int k = 0;
    for (int i = 0; i < 96 && k < 480; i++) {
        uint32_t v = sp[i];
        if (v > 0x401000 && v < 0x56E000) k += snprintf(line + k, sizeof line - k, " %08x", v);
    }
    logf("CRASH stack:%s", line);
    return EXCEPTION_CONTINUE_SEARCH;
}

static void director() {
    // which Hotline Miami track fits what is happening in GTA 2 right now (sheets/music.json contexts)
    uintptr_t pp = gta::playerPed();
    if (!pp || masks::menuOpen()) return;
    static uintptr_t lastCar; static bool wasRampage;
    uintptr_t car = at<uintptr_t>(pp + gta::PED_CAR);
    if (jobs::active() >= 0) return;
    MusicCtx c = music::context();
    if (jobs::clearedRecently() && c == MusicCtx::stage_clear) return;
    if (c == MusicCtx::phonecall && music::msInContext() < 30000 && !car) { lastCar = car; return; } // the call plays out
    if (car) {
        if (car != lastCar || (c != MusicCtx::car_radio)) music::play(MusicCtx::car_radio); // new car, new station
    } else if (combo::msSinceKill() < 8000) {
        if (!wasRampage || c != MusicCtx::rampage) music::play(MusicCtx::rampage);
        wasRampage = true;
    } else if (wasRampage || c == MusicCtx::car_radio) {
        music::stop(); wasRampage = false;
    }
    lastCar = car;
}

static void __fastcall hTick(uintptr_t game, void* edx) {
    oTick(game, edx);
    if (!gta::isRunning()) { music::tick(); return; }
    input::tick();
    if (g_hmOk) {
        masks::tick();
        jobs::tick();
        npcs::tick();
        combo::tick();
        combat::tick();
        director();
        render::tick();
    }
    music::tick();
    lab::tick();
    test::tick();
}

static void __fastcall hDamage(uintptr_t ped, void* edx, short amount) {
    uint32_t site = (uint32_t)(uintptr_t)__builtin_return_address(0);
    int a = g_hmOk ? combat::onDamage(ped, amount, site) : amount;
    test::onDamage(ped, amount, a, site);
    oDamage(ped, edx, (short)a);
}

static void __fastcall hDie(uintptr_t ped, void* edx) {
    if (g_hmOk) combat::onDie(ped);
    oDie(ped, edx);
}

static void __fastcall hDecode(uintptr_t player, void* edx) {
    uint32_t& keys = at<uint32_t>(player + gta::PLAYER_KEYS);
    uint32_t real = keys;
    if (player == gta::player() && g_hmOk) keys = masks::filterKeys(input::filterKeys(real));
    oDecode(player, edx);
    keys = real; // GTA 2 keeps owning the real keyboard state
}

static void __fastcall hShoot(uintptr_t weapon, void* edx) {
    uintptr_t ped = at<uintptr_t>(weapon + gta::WEAPON_PED), pp = gta::playerPed();
    int aim;
    if (!g_hmOk || !ped || ped != pp || !input::aiming(&aim)) { oShoot(weapon, edx); return; }
    int saved = gta::heading(pp);
    short cop = at<short>(pp + gta::PED_COPVALUE);
    gta::setHeading(pp, aim);
    oShoot(weapon, edx);
    gta::setHeading(pp, saved);
    if (masks::has(Perk::quiet_gunshots)) at<short>(pp + gta::PED_COPVALUE) = cop;
}

static void __fastcall hDrawUI(uintptr_t s10, void* edx) {
    oDrawUI(s10, edx);
    if (!g_hmOk || !gta::isRunning()) return;
    combo::drawHud();
    jobs::drawHud();
    masks::drawHud();
}

static bool hookChecked(const char* id, void* detour, void** orig) {
    for (const HookRow& h : kHooks) {
        if (strcmp(h.id, id)) continue;
        // refuse to patch anything that does not look like the GTA 2 build these addresses came from
        const uint8_t* p = (const uint8_t*)(uintptr_t)h.addr;
        for (int i = 0; h.prologue[i * 2]; i++) {
            unsigned b; sscanf(h.prologue + i * 2, "%2x", &b);
            if (p[i] != b) { logf("hook %s: prologue mismatch at +%d (game version?)", id, i); return false; }
        }
        if (!detour) return true;
        int r = MH_CreateHook((void*)(uintptr_t)h.addr, detour, orig);
        if (r == MH_OK) r = MH_EnableHook((void*)(uintptr_t)h.addr);
        logf("hook %s @%08x: %s", id, h.addr, r == MH_OK ? "ok" : "failed");
        return r == MH_OK;
    }
    logf("hook %s: not in sheet", id);
    return false;
}

static bool flagFile(const char* name) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\%s", g_modDir, name);
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static void init(HMODULE self) {
    GetModuleFileNameA(self, g_modDir, MAX_PATH);
    char* s = strrchr(g_modDir, '\\'); if (s) *s = 0;
    char v[16] = {};
    g_testMode = (GetEnvironmentVariableA("HAC_TEST", v, sizeof v) && v[0] == '1') || flagFile("HotlineCity-test.flag");
    g_autostart = g_testMode || flagFile("HotlineCity-autostart.flag");
    if (!GetEnvironmentVariableA("HAC_HM_DIR", g_hmDir, MAX_PATH)) {
        // Melty passes the companion folder; for a manual install fall back to an ini next to the plugin
        char ini[MAX_PATH]; snprintf(ini, sizeof ini, "%s\\HotlineCity.ini", g_modDir);
        GetPrivateProfileStringA("paths", "hotline_miami", "", g_hmDir, MAX_PATH, ini);
    }
    logf("Hotline Anywhere City loaded; HM folder '%s'; test mode %d", g_hmDir, g_testMode);
    if (at<uint32_t>(0x400000 + at<uint32_t>(0x40003C) + 8) != 0x408EC2AC) logf("warning: gta2.exe is not the 9.6 Steam build; prologue checks decide what is enabled");
    g_hmOk = g_hmDir[0] && hm::load();
    if (!g_hmOk) logf("Hotline Miami data not found: the mashup layer stays off and GTA 2 plays normally");
    AddVectoredExceptionHandler(1, crashLogger);
    MH_Initialize();
    bool ok = hookChecked("GameTick", (void*)hTick, (void**)&oTick);
    ok &= hookChecked("PedDamage", (void*)hDamage, (void**)&oDamage);
    ok &= hookChecked("PedDie", (void*)hDie, (void**)&oDie);
    ok &= hookChecked("DecodeKeys", (void*)hDecode, (void**)&oDecode);
    ok &= hookChecked("ShootWeapon", (void*)hShoot, (void**)&oShoot);
    ok &= hookChecked("DrawUI", (void*)hDrawUI, (void**)&oDrawUI);
    for (const char* call : {"SpawnPed", "AddWeapon", "SetPedWeapon", "SetObjective", "SpawnObject", "WorldToScreen", "GivePowerUp", "DrawGTAText"})
        ok &= hookChecked(call, nullptr, nullptr);
    if (!ok) { g_hmOk = false; logf("some game code did not match; mashup layer disabled for safety"); }
    if (flagFile("HotlineCity-lab.flag")) {
        // lab only: report the window as windowed-capable so GTA 2 keeps running when another window has focus
        uint8_t ret1[] = {0xB0, 0x01, 0xC3};
        DWORD old; VirtualProtect((void*)0x4CB6F0, 3, PAGE_EXECUTE_READWRITE, &old);
        memcpy((void*)0x4CB6F0, ret1, 3); VirtualProtect((void*)0x4CB6F0, 3, old, &old);
        logf("lab: background running enabled");
    }
    input::init();
    music::init();
    test::init();
    test::check("plugin_loaded", true);
    test::check("hm_data_loaded", g_hmOk, g_hmDir);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); init(h); }
    return TRUE;
}
