// Hotline Miami "clear the building" jobs placed in GTA 2's districts (sheets/jobs.json).
// Each job sits next to one of the district's payphones (found in the running game), shows an HM marker,
// asks for a mask, then fills the block with armed Russian-mob peds. Kill them all: STAGE CLEAR.
#include "core.h"
#include <stdio.h>
#include <wchar.h>
#include <stdlib.h>

namespace jobs {

struct Site { int job; int x, y, z; bool done; };
static Site g_sites[16]; static int g_siteN;
static char g_map[8];
static int g_active = -1;
static uintptr_t g_enemy[16], g_enemyPtr[16]; static int g_enemyId[16]; static int g_enemyN; // g_enemy: still guarding
static DWORD g_startedAt, g_clearedAt, g_titleAt;
static int g_bestCombo;
static wchar_t g_grade[8];

int active() { return g_active; }
bool clearedRecently() { return g_clearedAt && GetTickCount() - g_clearedAt < 30000; }

static void progressPath(char* out) { snprintf(out, MAX_PATH, "%s\\HotlineCity-progress.ini", g_modDir); }
static bool loadDone(const char* id) { char p[MAX_PATH]; progressPath(p); return GetPrivateProfileIntA("cleared", id, 0, p) != 0; }
static void saveDone(const char* id) { char p[MAX_PATH]; progressPath(p); WritePrivateProfileStringA("cleared", id, "1", p); }

static int g_spawnX, g_spawnY;
static DWORD g_mapAt;

static void sitesKey(char* out) { snprintf(out, 32, "sites_%s", g_map); }

// Job sites are spots where GTA 2's own pedestrians walk (pavement), far enough from the start and
// from each other; the first good spot found for a job is saved so the job stays put afterwards.
static void loadSites() {
    char p[MAX_PATH], key[32], val[64];
    progressPath(p); sitesKey(key);
    g_siteN = 0;
    for (int j = 0; j < countof(kJobs); j++) {
        if (strcmp(kJobs[j].map, g_map)) continue;
        Site s = {j, 0, 0, 0, loadDone(kJobs[j].id)};
        GetPrivateProfileStringA(key, kJobs[j].id, "", val, sizeof val, p);
        if (sscanf(val, "%d,%d,%d", &s.x, &s.y, &s.z) != 3) s.x = s.y = s.z = 0;
        g_sites[g_siteN++] = s;
    }
}

static void discoverSites() {
    uintptr_t pp = gta::playerPed();
    char p[MAX_PATH], key[32], val[64];
    progressPath(p); sitesKey(key);
    gta::forEachPed([&](uintptr_t ped) {
        if (ped == pp || at<int>(ped + gta::PED_OCCUPATION) != gta::OCC_DUMMY || at<uintptr_t>(ped + gta::PED_CAR) || at<int>(ped + gta::PED_STATE) != 0) return;
        int x = at<int>(ped + gta::PED_X), y = at<int>(ped + gta::PED_Y), z = at<int>(ped + gta::PED_Z);
        for (int i = 0; i < g_siteN; i++) {
            Site& s = g_sites[i];
            if (s.x || s.done) continue;
            const JobRow& j = kJobs[s.job];
            float sx = gta::toBlocks(x - g_spawnX), sy = gta::toBlocks(y - g_spawnY);
            if (sx * sx + sy * sy < (float)j.minFromSpawn * j.minFromSpawn) continue;
            bool clear = true;
            for (int k = 0; k < g_siteN; k++) {
                if (!g_sites[k].x) continue;
                float dx = gta::toBlocks(x - g_sites[k].x), dy = gta::toBlocks(y - g_sites[k].y);
                if (dx * dx + dy * dy < (float)j.minSeparation * j.minSeparation) clear = false;
            }
            if (!clear) continue;
            s.x = x; s.y = y; s.z = z;
            snprintf(val, sizeof val, "%d,%d,%d", x, y, z);
            WritePrivateProfileStringA(key, j.id, val, p);
            logf("jobs: %s placed at %.1f,%.1f", j.id, gta::toBlocks(x), gta::toBlocks(y));
            return;
        }
    });
}

void start(int job) {
    Site* s = nullptr;
    for (int i = 0; i < g_siteN; i++) if (g_sites[i].job == job) s = &g_sites[i];
    if (!s) return;
    const JobRow& j = kJobs[job];
    g_active = job; g_enemyN = 0; g_startedAt = g_titleAt = GetTickCount(); g_bestCombo = 0;
    for (int k = 0; k < j.enemies && k < 16; k++) {
        float a = k * 6.2831853f / j.enemies;
        int r = gta::kBlock * (4 + k % 4);
        uintptr_t p = gta::spawnPed(s->x + (int)(cosf(a) * r), s->y + (int)(sinf(a) * r), s->z, j.enemyRemap);
        if (!p) continue;
        int w = j.weapons[k % 4] >= 0 ? j.weapons[k % 4] : j.weapons[0];
        gta::giveWeapon(p, w, 99);
        gta::holdWeapon(p, w);
        gta::guardSpot(p); // like HM: they hold their room until they see Jacket
        g_enemy[g_enemyN] = g_enemyPtr[g_enemyN] = p; g_enemyId[g_enemyN] = at<int>(p + gta::PED_ID); g_enemyN++;
    }
    at<uint16_t>(gta::playerPed() + gta::PED_INVULN) = 60; // a breath before the shooting starts
    music::play(MusicCtx::job, j.track);
    logf("jobs: started %s with %d enemies", j.id, g_enemyN);
    test::check("job_spawns_enemies", g_enemyN == (j.enemies < 16 ? j.enemies : 16));
}

static int alive() {
    int n = 0;
    for (int i = 0; i < g_enemyN; i++) {
        uintptr_t p = g_enemyPtr[i];
        if (at<int>(p + gta::PED_ID) == g_enemyId[i] && at<uint16_t>(p + gta::PED_HEALTH) > 0 && at<int>(p + gta::PED_STATE) != gta::STATE_WASTED) n++;
    }
    return n;
}

void tick() {
    const char* m = gta::mapName();
    uintptr_t pp = gta::playerPed();
    if (!pp) return;
    if (strcmp(m, g_map)) {
        strncpy(g_map, m, 7); g_active = -1; g_mapAt = GetTickCount();
        g_spawnX = at<int>(pp + gta::PED_X); g_spawnY = at<int>(pp + gta::PED_Y);
        loadSites();
    }
    static DWORD lastScan;
    if (g_map[0] && GetTickCount() - lastScan > 1000) { lastScan = GetTickCount(); discoverSites(); }
    if (g_active >= 0) {
        if (combo::count() > g_bestCombo) g_bestCombo = combo::count();
        if (at<uint16_t>(pp + gta::PED_HEALTH) == 0) { logf("jobs: failed %s", kJobs[g_active].id); g_active = -1; music::stop(); return; }
        // an enemy that sees or hears Jacket close by goes for him
        int px = at<int>(pp + gta::PED_X), py = at<int>(pp + gta::PED_Y);
        for (int i = 0; i < g_enemyN; i++) {
            uintptr_t e = g_enemy[i];
            if (!e || at<int>(e + gta::PED_ID) != g_enemyId[i] || at<uint16_t>(e + gta::PED_HEALTH) == 0) continue;
            float dx = gta::toBlocks(at<int>(e + gta::PED_X) - px), dy = gta::toBlocks(at<int>(e + gta::PED_Y) - py);
            if (dx * dx + dy * dy < 16.0f || combo::msSinceKill() < 1500) { gta::huntPlayer(e); g_enemy[i] = 0; }
        }
        if (alive() == 0) {
            DWORD secs = (GetTickCount() - g_startedAt) / 1000;
            const wchar_t* g = secs < 60 && g_bestCombo >= 4 ? L"A+" : secs < 90 ? L"A" : secs < 150 ? L"B" : secs < 240 ? L"C" : L"D";
            wcsncpy(g_grade, g, 7);
            for (int i = 0; i < g_siteN; i++) if (g_sites[i].job == g_active) g_sites[i].done = true;
            saveDone(kJobs[g_active].id);
            combo::award(ScoreEvent::stage_clear);
            logf("jobs: cleared %s in %lus grade %ls", kJobs[g_active].id, secs, g_grade);
            test::check("job_clears", true);
            g_active = -1; g_clearedAt = GetTickCount();
            music::play(MusicCtx::stage_clear);
        }
        return;
    }
    if (masks::menuOpen() || at<uintptr_t>(pp + gta::PED_CAR)) return;
    int px = at<int>(pp + gta::PED_X), py = at<int>(pp + gta::PED_Y);
    for (int i = 0; i < g_siteN; i++) {
        Site& s = g_sites[i];
        if (s.done || !s.x) continue;
        if (abs(s.x - px) < gta::kBlock * 7 / 10 && abs(s.y - py) < gta::kBlock * 7 / 10) { masks::open(s.job); return; }
    }
}

void drawHud() {
    wchar_t buf[96];
    DWORD now = GetTickCount();
    uintptr_t pp = gta::playerPed();
    if (!pp) return;
    if (g_active >= 0) {
        const wchar_t* name = hm::text(kJobs[g_active].nameId);
        if (now - g_titleAt < 3500 && name) gta::drawText(name, gta::uiWidth() * 0.5f - 100, 150, 2.0f, (now / 150) % 2 ? 5 : 2);
        swprintf(buf, 96, L"%d", alive());
        gta::drawText(buf, gta::uiWidth() * 0.5f - 20, 20, 1.0f, 5);
        return;
    }
    if (clearedRecently()) {
        const wchar_t* sc = hm::text(1513), *gr = hm::text(1606);
        if (now - g_clearedAt < 6000) {
            if (sc) gta::drawText(sc, gta::uiWidth() * 0.5f - 110, 150, 2.0f, (now / 150) % 2 ? 5 : 2);
            swprintf(buf, 96, L"%ls %ls", gr ? gr : L"", g_grade);
            gta::drawText(buf, gta::uiWidth() * 0.5f - 60, 200, 1.2f, 3);
        }
    }
    int range, px = at<int>(pp + gta::PED_X), py = at<int>(pp + gta::PED_Y);
    bool reveal = masks::has(Perk::reveal_jobs, &range);
    for (int i = 0; i < g_siteN; i++) {
        Site& s = g_sites[i];
        if (s.done || !s.x) continue;
        float dx = gta::toBlocks(s.x - px), dy = gta::toBlocks(s.y - py), d = sqrtf(dx * dx + dy * dy);
        if (d > 12 && !reveal) continue;
        const wchar_t* name = hm::text(kJobs[s.job].nameId);
        float sx, sy;
        if (d < 10 && gta::worldToScreen(s.x, s.y, s.z, &sx, &sy)) {
            gta::drawText(L"v", sx - 4, sy - 30 - (now / 60) % 8, 1.2f, (now / 150) % 2 ? 5 : 2);
            if (name) gta::drawText(name, sx - 40, sy - 50, 0.6f, 5);
        } else {
            // off screen: an HM-style pointer along the screen edge with the distance
            float k = 200.0f / (fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy));
            swprintf(buf, 96, L"%ls %d", name ? name : L"", (int)d);
            gta::drawText(buf, gta::uiWidth() * 0.5f - 20 + dx * k * 1.2f, 230 + dy * k, 0.6f, 5);
        }
    }
}

} // namespace jobs
