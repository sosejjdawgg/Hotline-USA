// Hotline Miami combat rules on GTA 2's damage path (sheets/damage.json):
// everything kills in one hit, punches knock NPCs down, a stomp on a downed NPC is an execution,
// and the player dies to one hit unless a mask shield absorbs a bullet.
#include "core.h"

namespace combat {

struct Recent { uintptr_t ped; int kind; DWORD tick; };
static Recent g_recent[64];
static int g_shield;           // bullets the current mask can still absorb
static uintptr_t g_lastCar;

static const DamageRow& rowFor(uint32_t site) {
    for (const DamageRow& r : kDamage) if (r.site == site) return r;
    return kDamage[countof(kDamage) - 1]; // "other": lethal
}

static void remember(uintptr_t ped, DmgKind kind) {
    int slot = 0; DWORD oldest = ~0u;
    for (int i = 0; i < 64; i++) {
        if (g_recent[i].ped == ped) { slot = i; break; }
        if (g_recent[i].tick < oldest) { oldest = g_recent[i].tick; slot = i; }
    }
    g_recent[slot] = {ped, (int)kind, GetTickCount()};
}

static int lastKind(uintptr_t ped) {
    for (const Recent& r : g_recent) if (r.ped == ped && GetTickCount() - r.tick < 3000) return r.kind;
    return -1;
}

void setShield(int n) { g_shield = n; }

// Returns the amount to pass on to GTA 2's own damage code.
int onDamage(uintptr_t ped, int amount, uint32_t site) {
    const DamageRow& row = rowFor(site);
    int hp = at<uint16_t>(ped + gta::PED_HEALTH);
    uintptr_t pp = gta::playerPed();
    if (ped == pp) {
        if (row.player == PlayerRule::shieldable && g_shield > 0) {
            g_shield--;
            logf("combat: mask shield absorbed a bullet (%d left)", g_shield);
            return 0;
        }
        return hp > amount ? hp : amount; // one hit and Jacket is down
    }
    remember(ped, row.kind);
    if (row.npc == NpcRule::knockdown && !masks::has(Perk::lethal_fists))
        return amount >= hp ? hp - 1 : amount; // GTA 2 knocks the victim over; they stay alive for the stomp
    return hp > amount ? hp : amount;          // lethal (execution, bullets, fire, cars...)
}

void onDie(uintptr_t ped) {
    uintptr_t pp = gta::playerPed();
    if (!pp || ped == pp) return;
    if (at<int>(ped + gta::PED_ATTACKER) != at<int>(pp + gta::PED_ID)) return; // not the player's kill
    int k = lastKind(ped);
    ScoreEvent ev = ScoreEvent::kill_other;
    if (k == (int)DmgKind::stomp) ev = ScoreEvent::execution;
    else if (k == (int)DmgKind::punch) ev = ScoreEvent::kill_melee;
    else if (k == (int)DmgKind::bullet) {
        ev = ScoreEvent::kill_gun;
        uintptr_t w = at<uintptr_t>(pp + gta::PED_WEAPON);
        if (w) for (const WeaponRow& r : kWeapons) if (r.id == at<int>(w + gta::WEAPON_ID)) ev = r.killEvent;
    }
    combo::onKill(ev, ped);
}

void tick() {
    uintptr_t pp = gta::playerPed();
    if (!pp) return;
    // Don Juan: lethal doors - getting out of a car drops everyone next to the door.
    uintptr_t car = at<uintptr_t>(pp + gta::PED_CAR);
    int range;
    if (g_lastCar && !car && masks::has(Perk::lethal_doors, &range)) {
        int px = at<int>(pp + gta::PED_X), py = at<int>(pp + gta::PED_Y);
        gta::forEachPed([&](uintptr_t p) {
            if (p == pp || at<uint16_t>(p + gta::PED_HEALTH) == 0) return;
            float dx = gta::toBlocks(at<int>(p + gta::PED_X) - px), dy = gta::toBlocks(at<int>(p + gta::PED_Y) - py);
            if (dx * dx + dy * dy <= range * range && !at<uintptr_t>(p + gta::PED_CAR)) {
                at<int>(p + gta::PED_ATTACKER) = at<int>(pp + gta::PED_ID);
                remember(p, DmgKind::impact);
                gta::killPed(p);
            }
        });
    }
    g_lastCar = car;
}

} // namespace combat
