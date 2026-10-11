// Hotline Miami's mask pick (sheets/masks.json, perks.json). It opens when the player answers a
// payphone (every GTA 2 mission starts there) or steps on an HM job marker. Names and perk text come
// from the player's own Hotline Miami; abilities map onto GTA 2 systems.
#include "core.h"
#include <stdio.h>
#include <wchar.h>

namespace masks {

static int g_current = -1;     // mask being worn
static int g_cursor;           // menu selection
static bool g_open;
static int g_forJob = -1;      // job waiting for this mask, or -1 for a GTA 2 phone mission
static DWORD g_openedAt;
static uintptr_t g_lastPhone;
static int g_savedWalk = -1;
static uintptr_t g_walkObj;

bool menuOpen() { return g_open; }
int current() { return g_current; }
int cursor() { return g_cursor; }

bool has(Perk p, int* param) {
    if (g_current < 0) return false;
    const MaskRow& m = kMasks[g_current];
    if (m.perk != p) return false;
    if (param) *param = m.param;
    return true;
}

void open(int jobIndex) {
    if (g_open) return;
    g_open = true; g_forJob = jobIndex; g_openedAt = GetTickCount();
    g_cursor = g_current >= 0 ? g_current : 0;
    music::play(MusicCtx::mask_menu);
    logf("masks: menu opened (job %d)", jobIndex);
}

static void putOn(int idx) {
    g_current = idx;
    const MaskRow& m = kMasks[idx];
    uintptr_t pp = gta::playerPed();
    combat::setShield(m.perk == Perk::shield ? m.param : 0);
    if (m.perk == Perk::start_weapon && pp) { gta::giveWeapon(pp, m.param, 99); gta::holdWeapon(pp, m.param); }
    if (m.perk == Perk::electrofingers) gta::givePowerUp(9 /* ELECTROFINGERS */);
    hm::setFrench(m.perk == Perk::french);
    logf("masks: wearing %d perk %d", idx, (int)m.perk);
}

void choose(int idx) {
    if (!g_open) return;
    g_open = false;
    putOn(idx);
    if (g_forJob >= 0) jobs::start(g_forJob);
    else music::play(MusicCtx::phonecall);
}

// Keys GTA 2 sees while the menu is up: none. The menu reads input itself.
uint32_t filterKeys(uint32_t keys) { return g_open ? 0 : keys; }

static void applyContinuous() {
    uintptr_t pp = gta::playerPed();
    if (!pp) return;
    int p;
    // walking speed lives on the ped's sprite object (trainer research: sprite->obj+0x3C, 1024 = normal)
    uintptr_t obj = at<uintptr_t>(pp + gta::PED_GAMEOBJ), spr = obj ? at<uintptr_t>(obj + gta::OBJ_SPRITE) : 0;
    uintptr_t walk = spr ? at<uintptr_t>(spr + 8) : 0;
    if (walk != g_walkObj) { g_walkObj = walk; g_savedWalk = walk ? at<int>(walk + 0x3C) : -1; }
    if (walk && g_savedWalk > 0) at<int>(walk + 0x3C) = has(Perk::walk_fast, &p) ? g_savedWalk + p * 384 : g_savedWalk;
    short& cop = at<short>(pp + gta::PED_COPVALUE);
    static short lastCop;
    if (has(Perk::cops_ignore) && cop > 1599) cop = 1599;
    if (has(Perk::hard_to_spot, &p) && cop > lastCop) cop = (short)(lastCop + (cop - lastCop) * p / 100);
    lastCop = cop;
    if (has(Perk::fast_reload)) {
        uintptr_t w = at<uintptr_t>(pp + gta::PED_WEAPON);
        if (w) at<uint8_t>(w + gta::WEAPON_RELOAD) = 0;
    }
    static short lastAmmo; static uintptr_t lastW;
    uintptr_t w = at<uintptr_t>(pp + gta::PED_WEAPON);
    if (w && w == lastW && has(Perk::extra_ammo, &p) && at<short>(w) > lastAmmo + 1) at<short>(w) += (short)((at<short>(w) - lastAmmo) * (p - 1));
    lastW = w; lastAmmo = w ? at<short>(w) : 0;
}

void tick() {
    uintptr_t pp = gta::playerPed();
    if (!pp) return;
    if (g_open) {
        at<uint16_t>(pp + gta::PED_INVULN) = 30; // safe while choosing, like HM's mask room
        int dir = 0; bool ok = false;
        input::menuInput(&dir, &ok);
        if (dir) g_cursor = (g_cursor + dir + countof(kMasks)) % countof(kMasks);
        if (ok && GetTickCount() - g_openedAt > 400) choose(g_cursor);
        return;
    }
    // Answering a payphone: the player stands on a phone object while on foot.
    if (!at<uintptr_t>(pp + gta::PED_CAR)) {
        int px = at<int>(pp + gta::PED_X), py = at<int>(pp + gta::PED_Y), pz = at<int>(pp + gta::PED_Z);
        uintptr_t onPhone = 0;
        gta::forEachObject([&](uintptr_t o, int type, int x, int y, int z) {
            if (type != gta::OBJ_PHONE_RINGING) return;
            if (abs(x - px) < gta::kBlock / 2 && abs(y - py) < gta::kBlock / 2 && abs(z - pz) < gta::kBlock) onPhone = o;
        });
        if (onPhone && onPhone != g_lastPhone) open(-1);
        g_lastPhone = onPhone;
    }
    applyContinuous();
}

void drawHud() {
    wchar_t buf[96];
    if (g_current >= 0 && !g_open) {
        const wchar_t* n = hm::text(kMasks[g_current].nameId);
        if (n) gta::drawText(n, 50, 450, 0.6f, 3);
    }
    if (!g_open) return;
    const MaskRow& m = kMasks[g_cursor];
    const wchar_t* name = hm::text(m.nameId);
    const wchar_t* perk = hm::text(m.perkId);
    float cx = gta::uiWidth() * 0.5f;
    gta::drawText(L"<", cx - 120, 300, 1.4f, 5);
    gta::drawText(L">", cx + 110, 300, 1.4f, 5);
    swprintf(buf, 96, L"%ls", name ? name : L"?");
    gta::drawText(buf, cx - 70, 300, 1.4f, (GetTickCount() / 200) % 2 ? 5 : 2);
    if (perk && wcscmp(perk, L" ")) gta::drawText(perk, cx - 100, 340, 0.8f, 3);
    swprintf(buf, 96, L"%d / %d", g_cursor + 1, countof(kMasks));
    gta::drawText(buf, cx - 30, 370, 0.6f, 0);
}

} // namespace masks
