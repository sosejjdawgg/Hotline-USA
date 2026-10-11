// Hotline Miami scoring on GTA 2 kills (sheets/scoring.json): combo chain with a timer window,
// multi-kills, floating points, HM's own labels, and a cash bonus paid into GTA 2's money.
#include "core.h"
#include <stdio.h>
#include <wchar.h>

namespace combo {

static int g_count;            // kills in the current chain
static DWORD g_lastKill;       // GetTickCount of the last kill
static int g_burst;            // kills inside the multi-kill window
static long long g_score;
struct Popup { wchar_t text[48]; int x, y, z; DWORD t; };
static Popup g_pop[12];
static int g_popNext;

static const ScoreRow& row(ScoreEvent ev) {
    for (const ScoreRow& r : kScoring) if (r.ev == ev) return r;
    return kScoring[0];
}

static DWORD windowMs() {
    int pct = 100;
    masks::has(Perk::longer_combo, &pct);
    return 3000u * (pct ? pct : 100) / 100;
}

static void popup(const wchar_t* label, int points, uintptr_t at_) {
    Popup& p = g_pop[g_popNext++ % 12];
    if (label) swprintf(p.text, 48, L"%ls +%d", label, points); else swprintf(p.text, 48, L"+%d", points);
    p.x = at_ ? at<int>(at_ + gta::PED_X) : 0; p.y = at_ ? at<int>(at_ + gta::PED_Y) : 0; p.z = at_ ? at<int>(at_ + gta::PED_Z) : 0;
    p.t = GetTickCount();
}

void award(ScoreEvent ev) {
    const ScoreRow& r = row(ev);
    int mult = g_count > 1 ? g_count : 1;
    g_score += (long long)r.points * mult;
    gta::addMoney(r.cash * mult);
    popup(hm::text(r.labelId), r.points * mult, 0);
}

void onKill(ScoreEvent ev, uintptr_t victim) {
    DWORD now = GetTickCount();
    g_count = (now - g_lastKill <= windowMs()) ? g_count + 1 : 1;
    g_burst = (now - g_lastKill <= 500) ? g_burst + 1 : 1;
    g_lastKill = now;
    const ScoreRow& r = row(ev);
    int mult = g_count > 1 ? g_count : 1;
    g_score += (long long)r.points * mult;
    gta::addMoney(r.cash * mult);
    const wchar_t* label = (ev == ScoreEvent::execution) ? hm::text(r.labelId) : nullptr;
    popup(label, r.points * mult, victim);
    if (g_burst >= 2) {
        ScoreEvent m = g_burst == 2 ? ScoreEvent::double_kill : g_burst == 3 ? ScoreEvent::triple_kill : ScoreEvent::multi_kill;
        const ScoreRow& mr = row(m);
        g_score += mr.points; gta::addMoney(mr.cash);
        popup(hm::text(mr.labelId), mr.points, victim);
    }
    int ammo;
    if (masks::has(Perk::rip_and_steal, &ammo)) {
        uintptr_t w = at<uintptr_t>(gta::playerPed() + gta::PED_WEAPON);
        if (w) at<short>(w + gta::WEAPON_AMMO) += (short)ammo;
    }
    int gore;
    if (masks::has(Perk::more_gore, &gore))
        for (int i = 0; i < gore; i++)
            gta::spawnObject(gta::OBJ_BLOOD, at<int>(victim + gta::PED_X) + (i - 1) * 3000, at<int>(victim + gta::PED_Y) + (i % 2) * 3000, at<int>(victim + gta::PED_Z));
    logf("combo: kill ev=%d chain=%d burst=%d score=%lld", (int)ev, g_count, g_burst, g_score);
}

int count() { return (GetTickCount() - g_lastKill <= windowMs()) ? g_count : 0; }
DWORD msSinceKill() { return g_lastKill ? GetTickCount() - g_lastKill : 0xFFFFFFFF; }
long long score() { return g_score; }

void tick() {
    if (g_count && GetTickCount() - g_lastKill > windowMs()) g_count = 0;
}

// GTA 2 UI coordinates are a virtual 640x480 space scaled by the camera's uiScale.
void drawHud() {
    wchar_t buf[64];
    const wchar_t* scoreLbl = hm::text(1649); // "SCORE:"
    swprintf(buf, 64, L"%ls %lld", scoreLbl ? scoreLbl : L"", g_score);
    gta::drawText(buf, gta::uiWidth() - 200, 64, 0.7f, 3);
    int c = count();
    if (c >= 2) {
        DWORD left = windowMs() - (GetTickCount() - g_lastKill);
        float pulse = 1.0f + 0.15f * (float)(left % 400) / 400.0f;
        const wchar_t* lbl = hm::text(1546); // "Combo"
        swprintf(buf, 64, L"%dx %ls", c, lbl ? lbl : L"");
        gta::drawText(buf, gta::uiWidth() - 200, 90, 1.0f * pulse, (GetTickCount() / 120) % 2 ? 5 : 2);
    }
    DWORD now = GetTickCount();
    for (Popup& p : g_pop) {
        DWORD age = now - p.t;
        if (!p.t || age > 1200) continue;
        float sx = gta::uiWidth() * 0.5f - 20, sy = 200;
        if (p.x) {
            float px, py;
            if (!gta::worldToScreen(p.x, p.y, p.z, &px, &py)) continue;
            sx = px; sy = py;
        }
        gta::drawText(p.text, sx, sy - age * 0.03f, 0.6f, (age / 100) % 2 ? 2 : 5);
    }
}

} // namespace combo
