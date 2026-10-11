// Developer console: only active when HotlineCity-lab.flag sits next to the plugin. It polls lab.cmd
// (one command per line) and logs replies, so the running game can be driven and inspected.
#include "core.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

namespace lab {

static uintptr_t g_last;
static int g_counter;

static uintptr_t resolve(char* s) {
    char* plus = strchr(s, '+');
    if (plus) { *plus = 0; return resolve(s) + strtoul(plus + 1, 0, 16); }
    if (!strcmp(s, "ped")) return gta::playerPed();
    if (!strcmp(s, "player")) return gta::player();
    if (!strcmp(s, "game")) return gta::game();
    if (!strcmp(s, "cam")) return gta::mainCamera();
    if (!strcmp(s, "last")) return g_last;
    if (!strcmp(s, "obj")) { uintptr_t p = gta::playerPed(); return p ? at<uintptr_t>(p + gta::PED_GAMEOBJ) : 0; }
    if (!strcmp(s, "spr")) { uintptr_t o = resolve((char*)"obj"); return o ? at<uintptr_t>(o + gta::OBJ_SPRITE) : 0; }
    return strtoul(s, 0, 16);
}

static void dump(uintptr_t a, int n) {
    char line[128]; int k = 0;
    for (int i = 0; i < n; i++) {
        if (i % 16 == 0) { if (i) logf("%s", line); k = sprintf(line, "%08x:", (unsigned)(a + i)); }
        k += sprintf(line + k, " %02x", at<uint8_t>(a + i));
    }
    logf("%s", line);
}

static void run(char* line) {
    char c[32] = {}, a[64] = {}, b[64] = {}, d[64] = {};
    if (sscanf(line, "%31s %63s %63s %63s", c, a, b, d) < 1) return;
    logf("lab> %s", line);
    uintptr_t pp = gta::playerPed();
    if (!strcmp(c, "r")) dump(resolve(a), b[0] ? strtol(b, 0, 0) : 64);
    else if (!strcmp(c, "w8")) at<uint8_t>(resolve(a)) = (uint8_t)strtoul(b, 0, 0);
    else if (!strcmp(c, "w16")) at<uint16_t>(resolve(a)) = (uint16_t)strtoul(b, 0, 0);
    else if (!strcmp(c, "w32")) at<uint32_t>(resolve(a)) = (uint32_t)strtoul(b, 0, 0);
    else if (!strcmp(c, "spawn") && pp) {   // spawn dx dy remap   (blocks*100 relative to the player)
        g_last = gta::spawnPed(at<int>(pp + gta::PED_X) + strtol(a, 0, 0) * gta::kBlock / 100, at<int>(pp + gta::PED_Y) + strtol(b, 0, 0) * gta::kBlock / 100,
                               at<int>(pp + gta::PED_Z), d[0] ? strtol(d, 0, 0) : 255);
        logf("lab: spawned %08x id %d hp %d", (unsigned)g_last, g_last ? at<int>(g_last + gta::PED_ID) : 0, g_last ? at<uint16_t>(g_last + gta::PED_HEALTH) : 0);
    } else if (!strcmp(c, "arm") && g_last) { gta::giveWeapon(g_last, strtol(a, 0, 0), 99); gta::holdWeapon(g_last, strtol(a, 0, 0)); }
    else if (!strcmp(c, "hunt") && g_last) gta::huntPlayer(g_last);
    else if (!strcmp(c, "give") && pp) { gta::giveWeapon(pp, strtol(a, 0, 0), b[0] ? strtol(b, 0, 0) : 99); gta::holdWeapon(pp, strtol(a, 0, 0)); }
    else if (!strcmp(c, "keys")) input::force(strtoul(a, 0, 0), b[0] ? strtol(b, 0, 0) : 30, d[0] ? strtol(d, 0, 0) : -1); // keys mask ticks heading
    else if (!strcmp(c, "heading") && pp) gta::setHeading(pp, strtol(a, 0, 0));
    else if (!strcmp(c, "mask")) masks::open(-1);
    else if (!strcmp(c, "job")) jobs::start(strtol(a, 0, 0));
    else if (!strcmp(c, "music")) music::play((MusicCtx)strtol(a, 0, 0));
    else if (!strcmp(c, "w2s") && pp) {
        float sx, sy; gta::worldToScreen(at<int>(pp + gta::PED_X), at<int>(pp + gta::PED_Y), at<int>(pp + gta::PED_Z), &sx, &sy);
        logf("lab: player screen %.1f %.1f", sx, sy);
    } else if (!strcmp(c, "phones")) {
        gta::forEachObject([](uintptr_t o, int type, int x, int y, int z) {
            if (type == gta::OBJ_PHONE || type == gta::OBJ_PHONE_RINGING) logf("lab: phone type %d at %.2f %.2f %.2f", type, gta::toBlocks(x), gta::toBlocks(y), gta::toBlocks(z));
        });
    } else if (!strcmp(c, "peds")) {
        gta::forEachPed([](uintptr_t p) {
            logf("lab: ped %08x id %d occ %d remap %d hp %d state %d at %.1f %.1f", (unsigned)p, at<int>(p + gta::PED_ID), at<int>(p + gta::PED_OCCUPATION),
                 at<int>(p + gta::PED_REMAP), at<uint16_t>(p + gta::PED_HEALTH), at<int>(p + gta::PED_STATE), gta::toBlocks(at<int>(p + gta::PED_X)), gta::toBlocks(at<int>(p + gta::PED_Y)));
        });
    } else if (!strcmp(c, "state") && pp) {
        logf("lab: map %s hp %d pos %.2f %.2f %.2f heading %d car %08x weapon %08x cop %d score %lld combo %d mask %d job %d",
             gta::mapName(), at<uint16_t>(pp + gta::PED_HEALTH), gta::toBlocks(at<int>(pp + gta::PED_X)), gta::toBlocks(at<int>(pp + gta::PED_Y)),
             gta::toBlocks(at<int>(pp + gta::PED_Z)), gta::heading(pp), at<unsigned>(pp + gta::PED_CAR), at<unsigned>(pp + gta::PED_WEAPON),
             at<short>(pp + gta::PED_COPVALUE), combo::score(), combo::count(), masks::current(), jobs::active());
    }
}

void tick() {
    if (++g_counter % 10) return;
    static int enabled = -1;
    char path[MAX_PATH];
    if (enabled < 0) {
        snprintf(path, sizeof path, "%s\\HotlineCity-lab.flag", g_modDir);
        enabled = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    }
    if (!enabled) return;
    snprintf(path, sizeof path, "%s\\lab.cmd", g_modDir);
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) { line[strcspn(line, "\r\n")] = 0; run(line); }
    fclose(f);
    DeleteFileA(path);
}

} // namespace lab
