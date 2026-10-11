// A more dangerous city (sheets/npcs.json, weapons.json): new pedestrians sometimes carry guns,
// and some of the armed ones go for the player when he comes close.
#include "core.h"

namespace npcs {

static int g_maxSeenId;
static uint32_t g_rng = 0x1234567;
static int rnd(int n) { g_rng = g_rng * 1103515245u + 12345u; return (int)((g_rng >> 16) % (unsigned)n); }

static const NpcArmRow* ruleFor(int occupation) {
    for (const NpcArmRow& r : kNpcArming)
        for (int o : r.occupations) if (o == occupation) return &r;
    return nullptr;
}

static int pickWeapon() {
    int total = 0;
    for (const WeaponRow& w : kWeapons) total += w.npcWeight;
    int r = rnd(total ? total : 1);
    for (const WeaponRow& w : kWeapons) { if (r < w.npcWeight) return w.id; r -= w.npcWeight; }
    return 0;
}

struct Armed { uintptr_t ped; int id; bool aggressive; bool hunting; };
static Armed g_armed[64];

void tick() {
    uintptr_t pp = gta::playerPed();
    if (!pp) return;
    int playerId = at<int>(pp + gta::PED_ID);
    int newest = g_maxSeenId;
    gta::forEachPed([&](uintptr_t p) {
        int id = at<int>(p + gta::PED_ID);
        if (id <= g_maxSeenId || p == pp) return;
        if (id > newest) newest = id;
        if (jobs::active() >= 0 && at<int>(p + gta::PED_REMAP) == 10) return; // job enemies are armed by jobs.cpp
        const NpcArmRow* rule = ruleFor(at<int>(p + gta::PED_OCCUPATION));
        if (!rule || at<uintptr_t>(p + gta::PED_CAR)) return;
        int chance = rule->chancePct, mult;
        if (masks::has(Perk::more_guns, &mult)) chance *= mult;
        if (rnd(100) >= chance) return;
        int w = pickWeapon(), ammo = 24;
        for (const WeaponRow& r : kWeapons) if (r.id == w) ammo = r.npcAmmo;
        gta::giveWeapon(p, w, ammo);
        gta::holdWeapon(p, w);
        bool aggressive = rnd(100) < rule->aggressivePct;
        for (Armed& a : g_armed) if (!a.ped || at<int>(a.ped + gta::PED_ID) != a.id) { a = {p, id, aggressive, false}; break; }
    });
    if (newest > g_maxSeenId) g_maxSeenId = newest;
    if (playerId < 0) return;
    // aggressive armed peds open fire once the player is within 5 blocks on foot
    int px = at<int>(pp + gta::PED_X), py = at<int>(pp + gta::PED_Y);
    for (Armed& a : g_armed) {
        if (!a.ped || at<int>(a.ped + gta::PED_ID) != a.id || at<uint16_t>(a.ped + gta::PED_HEALTH) == 0) { a.ped = 0; continue; }
        if (!a.aggressive || a.hunting) continue;
        float dx = gta::toBlocks(at<int>(a.ped + gta::PED_X) - px), dy = gta::toBlocks(at<int>(a.ped + gta::PED_Y) - py);
        if (dx * dx + dy * dy < 25.0f && !at<uintptr_t>(pp + gta::PED_CAR)) { gta::huntPlayer(a.ped); a.hunting = true; }
    }
}

} // namespace npcs
