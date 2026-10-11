// Test mode (HAC_TEST=1, used by Melty's test runs): presses Enter through GTA 2's own keyboard reads
// until a game starts, then plays a scripted fight that exercises the mashup and writes
// PASS/FAIL lines to HotlineCity-test.log. Nothing here runs for players.
#include "core.h"
#include <stdio.h>
#include <string.h>

namespace test {

static FILE* g_out;
static int g_pass, g_fail;

void check(const char* name, bool ok, const char* detail) {
    if (!g_testMode) return;
    if (!g_out) { char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\HotlineCity-test.log", g_modDir); g_out = fopen(p, "w"); }
    if (!g_out) return;
    fprintf(g_out, "%s %s %s\n", ok ? "PASS" : "FAIL", name, detail ? detail : "");
    fflush(g_out);
    (ok ? g_pass : g_fail)++;
    logf("test: %s %s %s", ok ? "PASS" : "FAIL", name, detail ? detail : "");
}

// ---- keyboard events injected into IDirectInputDevice8::GetDeviceData (vtable slot 10) -----------
struct DIOD { DWORD ofs, data, time, seq; uintptr_t app; };
typedef HRESULT(__stdcall* GetData_t)(void*, DWORD, DIOD*, DWORD*, DWORD);
static GetData_t g_origGetData;
static volatile LONG g_pendingKey = 0, g_pendingUp = 0;

static HRESULT __stdcall hGetData(void* dev, DWORD sz, DIOD* buf, DWORD* n, DWORD flags) {
    HRESULT r = g_origGetData(dev, sz, buf, n, flags);
    if (buf && n && *n == 0 && sz == sizeof(DIOD)) {
        LONG k = InterlockedExchange(&g_pendingKey, 0);
        LONG u = k ? 0 : InterlockedExchange(&g_pendingUp, 0);
        if (k || u) { buf->ofs = k ? k : u; buf->data = k ? 0x80 : 0; buf->time = GetTickCount(); buf->seq = 0; buf->app = 0; *n = 1; if (k) g_pendingUp = k; return 0; }
    }
    return r;
}

static DWORD WINAPI frontendDriver(void*) {
    // wait for GTA 2 to create its keyboard device, then patch its GetDeviceData slot
    for (int i = 0; i < 3000 && !at<uintptr_t>(0x5E8F4C); i++) {
        // a game started behind another window can come up minimised; show it without stealing focus
        HWND w = at<HWND>(0x673D18);
        if (w && IsIconic(w)) ShowWindow(w, SW_SHOWNOACTIVATE);
        Sleep(100);
    }
    uintptr_t dev = at<uintptr_t>(0x5E8F4C);
    if (!dev) { check("frontend_keyboard", false, "no DirectInput keyboard"); return 0; }
    void** vt = *(void***)dev;
    DWORD old; VirtualProtect(&vt[10], sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
    g_origGetData = (GetData_t)vt[10]; vt[10] = (void*)hGetData;
    VirtualProtect(&vt[10], sizeof(void*), old, &old);
    for (int i = 0; i < 60 && !gta::isRunning(); i++) { Sleep(1500); g_pendingKey = 0x1C; /* DIK_RETURN */ }
    if (g_testMode) check("reached_gameplay", gta::isRunning());
    return 0;
}

static int g_step = -1;
static DWORD g_stepAt;
static uintptr_t g_target;
static long long g_scoreBefore;

static void next() { g_step++; g_stepAt = GetTickCount(); }
static DWORD since() { return GetTickCount() - g_stepAt; }

static uintptr_t spawnInFront(int dist100) {
    uintptr_t pp = gta::playerPed();
    float a = gta::heading(pp) * 3.14159265f / 720.0f;
    uintptr_t p = gta::spawnPed(at<int>(pp + gta::PED_X) + (int)(sinf(a) * dist100 * gta::kBlock / 100), at<int>(pp + gta::PED_Y) + (int)(cosf(a) * dist100 * gta::kBlock / 100),
                                at<int>(pp + gta::PED_Z), 20);
    if (p) gta::standStill(p);
    return p;
}
static bool down(uintptr_t p) { return p && at<uint16_t>(p + gta::PED_HEALTH) > 0 && at<int>(p + gta::PED_STATE) == gta::STATE_FALLEN; }
static bool dead(uintptr_t p) { return p && (at<uint16_t>(p + gta::PED_HEALTH) == 0 || at<int>(p + gta::PED_STATE) == gta::STATE_WASTED); }
static void face(uintptr_t pp, uintptr_t p) {
    gta::setHeading(pp, gta::headingFromVector((float)(at<int>(p + gta::PED_X) - at<int>(pp + gta::PED_X)), (float)(at<int>(p + gta::PED_Y) - at<int>(pp + gta::PED_Y))));
}

void init() { if (g_autostart) CreateThread(nullptr, 0, frontendDriver, nullptr, 0, nullptr); }

void onDamage(uintptr_t ped, int amount, int passed, uint32_t site) {
    if (g_testMode) logf("test: damage site %08x victim %d amount %d -> %d hp %d", site, at<int>(ped + gta::PED_ID), amount, passed, at<uint16_t>(ped + gta::PED_HEALTH));
}

void tick() {
    if (!g_testMode) return;
    uintptr_t pp = gta::playerPed();
    if (!pp) return;
    if (g_step < 0) { next(); return; }
    switch (g_step) {
    case 0: if (since() > 4000) { at<uint16_t>(pp + gta::PED_INVULN) = 9999; next(); } break; // let the level settle; keep the tester alive
    case 1: g_target = spawnInFront(70); check("spawn_ped", g_target != 0); next(); break;
    case 2: if (since() > 300) { face(pp, g_target); input::force(gta::KEY_ATTACK, 12, gta::heading(pp)); next(); } break; // one punch
    case 3: if (down(g_target) || since() > 1500) {
                char d[64]; snprintf(d, sizeof d, "hp %d state %d", at<uint16_t>(g_target + gta::PED_HEALTH), at<int>(g_target + gta::PED_STATE));
                check("punch_knocks_down", down(g_target), d);
                g_scoreBefore = combo::score(); face(pp, g_target); input::force(gta::KEY_ATTACK, 40, gta::heading(pp)); next(); } break; // stomp
    case 4: if (dead(g_target) || since() > 3000) {
                check("stomp_executes", dead(g_target), "kick on a downed ped");
                check("execution_scores", combo::score() > g_scoreBefore); next(); } break;
    case 5: if (since() > 1500) {
                gta::giveWeapon(pp, 0, 30); gta::holdWeapon(pp, 0);
                g_target = spawnInFront(250); next(); } break;
    case 6: if (since() > 400) { face(pp, g_target); input::force(gta::KEY_ATTACK, 8, gta::heading(pp)); next(); } break; // a single shot
    case 7: if (dead(g_target) || since() > 2500) {
                char d[64]; snprintf(d, sizeof d, "hp %d state %d", at<uint16_t>(g_target + gta::PED_HEALTH), at<int>(g_target + gta::PED_STATE));
                check("bullet_one_hit", dead(g_target), d); next(); } break;
    case 8: masks::open(-1); next(); break;
    case 9: if (since() > 600) {
                check("mask_menu_opens", masks::menuOpen());
                check("mask_names_from_hm", hm::text(1217) && hm::text(1245));
                check("mask_menu_music", music::context() == MusicCtx::mask_menu);
                masks::choose(2); next(); } break; // Tony
    case 10: if (since() > 600) {
                check("mask_worn", masks::current() == 2 && masks::has(Perk::lethal_fists));
                check("phonecall_music", music::context() == MusicCtx::phonecall);
                g_target = spawnInFront(70); next(); } break;
    case 11: if (since() > 300) { face(pp, g_target); input::force(gta::KEY_ATTACK, 12, gta::heading(pp)); next(); } break;
    case 12: if (dead(g_target) || since() > 2000) {
                check("tony_fists_kill", dead(g_target));
                char d[64]; snprintf(d, sizeof d, "%d passed, %d failed", g_pass, g_fail);
                check("test_done", g_fail == 0, d); next(); } break;
    default: break;
    }
}

} // namespace test
