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
typedef void(__fastcall* ManageWeapon_t)(uintptr_t, void*);
typedef void(__fastcall* DrawUI_t)(uintptr_t, void*);
typedef void(__fastcall* AddSprite_t)(uintptr_t, void*, uintptr_t);
static Tick_t oTick; static Damage_t oDamage; static Die_t oDie; static Decode_t oDecode; static ManageWeapon_t oManageWeapon; static DrawUI_t oDrawUI;
static AddSprite_t oAddSprite;
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

// GTA 2's own sprite for the player stays out of the frame while Jacket is drawn in its place
// (if Jacket can't be drawn, it comes back within a few frames).
static void __fastcall hAddSprite(uintptr_t tree, void* edx, uintptr_t sprite) {
    if (g_hmOk && sprite && render::jacketShown()) {
        uintptr_t pp = gta::playerPed(), obj = pp ? at<uintptr_t>(pp + gta::PED_GAMEOBJ) : 0;
        if (obj && sprite == at<uintptr_t>(obj + gta::OBJ_SPRITE) && !at<uintptr_t>(pp + gta::PED_CAR)) return;
    }
    oAddSprite(tree, edx, sprite);
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

// GTA 2 fires a ped's weapon in its own step, before the ped walks. The player's walk follows the
// movement heading set in DecodeKeys; for this step alone the ped faces the aim, so every gun
// (pistol: char rotation, SMG/shotgun: facing + sprite angle) shoots exactly where Jacket looks.
static void __fastcall hManageWeapon(uintptr_t ped, void* edx) {
    uintptr_t pp = gta::playerPed();
    int aim;
    if (!g_hmOk || !ped || ped != pp || at<uintptr_t>(pp + gta::PED_CAR) || !input::aiming(&aim)) { oManageWeapon(ped, edx); return; }
    int saved = gta::heading(pp);
    short cop = at<short>(pp + gta::PED_COPVALUE);
    gta::setHeading(pp, aim);
    oManageWeapon(ped, edx);
    if (saved != aim) gta::setHeading(pp, saved);
    if (masks::has(Perk::quiet_gunshots)) at<short>(pp + gta::PED_COPVALUE) = cop;
}

static void __fastcall hDrawUI(uintptr_t s10, void* edx) {
    oDrawUI(s10, edx);
    if (!g_hmOk || !gta::isRunning()) return;
    combo::drawHud();
    jobs::drawHud();
    masks::drawHud();
}

// "~8378??09": those bytes (?? = any) somewhere in the function's first 64 bytes.
static bool scanBody(const uint8_t* p, const char* pat) {
    int n = (int)strlen(pat) / 2;
    for (int off = 0; off + n <= 64; off++) {
        bool ok = true;
        for (int i = 0; i < n && ok; i++) {
            if (pat[i * 2] == '?') continue;
            unsigned b; sscanf(pat + i * 2, "%2x", &b);
            ok = p[off + i] == b;
        }
        if (ok) return true;
    }
    return false;
}

// Addresses taken from the gta2_re 9.6f pairing (not yet seen in the lab) must sit on a function
// boundary and contain the expected instructions, or they are left alone.
static bool bodyMatches(const char* id, const uint8_t* p, const char* spec) {
    char hex[3 * 16 + 1]; int k = 0;
    for (int i = 0; i < 16; i++) k += snprintf(hex + k, sizeof hex - k, "%02x", p[i]);
    logf("hook %s: first bytes %s (before: %02x)", id, hex, p[-1]);
    bool boundary = ((uintptr_t)p & 15) == 0 && (p[-1] == 0x90 || p[-1] == 0xCC || p[-1] == 0xC3 || (p[-3] == 0xC2 && p[-1] == 0x00));
    if (!boundary) { logf("hook %s: not at a function start (game version?)", id); return false; }
    char one[64];
    for (const char* s = spec; *s;) {
        const char* bar = strchr(s, '|');
        size_t n = bar ? (size_t)(bar - s) : strlen(s);
        snprintf(one, sizeof one, "%.*s", (int)n - 1, s + 1);
        if (!scanBody(p, one)) { logf("hook %s: body does not contain %s (game version?)", id, one); return false; }
        s += n + (bar ? 1 : 0);
    }
    return true;
}

static bool hookChecked(const char* id, void* detour, void** orig) {
    for (const HookRow& h : kHooks) {
        if (strcmp(h.id, id)) continue;
        // refuse to patch anything that does not look like the GTA 2 build these addresses came from
        const uint8_t* p = (const uint8_t*)(uintptr_t)h.addr;
        if (h.prologue[0] == '~') { if (!bodyMatches(id, p, h.prologue)) return false; }
        else for (int i = 0; h.prologue[i * 2]; i++) {
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
    ok &= hookChecked("DrawUI", (void*)hDrawUI, (void**)&oDrawUI);
    // optional: without these Jacket faces the aim to shoot, and GTA 2's sprite shows under him
    bool strafe = g_hmOk && hookChecked("ManageWeapon", (void*)hManageWeapon, (void**)&oManageWeapon);
    input::setStrafe(strafe);
    if (g_hmOk) hookChecked("SpriteTreeAdd", (void*)hAddSprite, (void**)&oAddSprite);
    for (const char* call : {"SpawnPed", "AddWeapon", "SetPedWeapon", "SetObjective", "SpawnObject", "WorldToScreen", "GivePowerUp", "DrawGTAText"})
        ok &= hookChecked(call, nullptr, nullptr);
    if (!ok) { g_hmOk = false; logf("some game code did not match; mashup layer disabled for safety"); }
    if (flagFile("HotlineCity-lab.flag") || setting("display", "borderless", 1)) {
        // report the window as windowed-capable so GTA 2 keeps running (instead of minimising) when another window has focus
        uint8_t ret1[] = {0xB0, 0x01, 0xC3};
        DWORD old; VirtualProtect((void*)0x4CB6F0, 3, PAGE_EXECUTE_READWRITE, &old);
        memcpy((void*)0x4CB6F0, ret1, 3); VirtualProtect((void*)0x4CB6F0, 3, old, &old);
        logf("display: keeps running in the background");
    }
    display::init();
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
