// Reads Hotline Miami's own files from the player's install: HotlineMiami_GL.wad (text, player atlas).
// Nothing is copied out of the game folder; everything lives in memory while GTA 2 runs.
#include "core.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

namespace hm {

static const int kAtlasSize = 2048;
static uint8_t* g_atlasFile;      // whole player.ags.phyre
static uint8_t* g_pixels;         // points into g_atlasFile
static int g_animStart[400], g_animCount[400], g_anims;
static const float* g_rects;      // 6 floats per frame: ?, ?, u, v, w, h
static int g_frames;

struct Str { int id; wchar_t* s; };
static Str* g_en; static int g_enN;
static Str* g_fr; static int g_frN;
static bool g_french;

static bool readWadEntry(FILE* f, uint32_t base, const char* want, uint8_t** data, uint32_t* size, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        uint32_t len; char name[512];
        if (fread(&len, 4, 1, f) != 1 || len >= sizeof name) return false;
        if (fread(name, 1, len, f) != len) return false;
        name[len] = 0;
        uint32_t sz, off;
        if (fread(&sz, 4, 1, f) != 1 || fread(&off, 4, 1, f) != 1) return false;
        if (!strcmp(name, want)) {
            long pos = ftell(f);
            uint8_t* buf = (uint8_t*)malloc(sz);
            if (!buf) return false;
            fseek(f, base + off, SEEK_SET);
            bool ok = fread(buf, 1, sz, f) == sz;
            fseek(f, pos, SEEK_SET);
            if (!ok) { free(buf); return false; }
            *data = buf; *size = sz;
            return true;
        }
    }
    return false;
}

static bool wadRead(const char* want, uint8_t** data, uint32_t* size) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\HotlineMiami_GL.wad", g_hmDir);
    FILE* f = fopen(path, "rb");
    if (!f) { logf("hm: cannot open %s", path); return false; }
    uint32_t hdr, count;
    bool ok = fread(&hdr, 4, 1, f) == 1 && fread(&count, 4, 1, f) == 1 && count < 100000 &&
              readWadEntry(f, 8 + hdr, want, data, size, count);
    fclose(f);
    if (!ok) logf("hm: %s not found in wad", want);
    return ok;
}

// localisation: [u32 len][utf16 name][u32 offset] language table, then blocks of [u32 id][u32 bytes][utf16]
static bool parseLanguage(const uint8_t* d, uint32_t n, const wchar_t* lang, Str** out, int* outN) {
    uint32_t p = 0, start = 0, end = n;
    bool found = false;
    while (p + 4 < n) {
        uint32_t len = *(uint32_t*)(d + p);
        if (len == 0 || len > 64 || p + 8 + len > n) break;
        uint32_t off = *(uint32_t*)(d + p + 4 + len);
        if (found && !end) end = off - 4;
        if (!found && len == wcslen(lang) * 2 && !memcmp(d + p + 4, lang, len)) { found = true; start = off - 4; end = 0; }
        p += 8 + len;
    }
    if (!found) return false;
    if (!end) end = n;
    int cap = 4096, cnt = 0;
    Str* arr = (Str*)calloc(cap, sizeof(Str));
    for (uint32_t q = start; q + 8 <= end && cnt < cap;) {
        uint32_t id = *(uint32_t*)(d + q), bytes = *(uint32_t*)(d + q + 4);
        if (bytes > 20000 || q + 8 + bytes > end) break;
        wchar_t* s = (wchar_t*)calloc(bytes / 2 + 1, 2);
        memcpy(s, d + q + 8, bytes);
        for (wchar_t* c = s; *c; c++) if (*c == L'#') *c = L' '; // HM uses '#' as a line break
        arr[cnt++] = {(int)id, s};
        q += 8 + bytes;
    }
    *out = arr; *outN = cnt;
    return cnt > 0;
}

bool load() {
    uint8_t* loc; uint32_t locN;
    if (!wadRead("GL/hotline_localization.bin", &loc, &locN)) return false;
    bool en = parseLanguage(loc, locN, L"English", &g_en, &g_enN);
    parseLanguage(loc, locN, L"French", &g_fr, &g_frN);
    free(loc);
    logf("hm: text en=%d fr=%d", g_enN, g_frN);
    if (!en) return false;

    uint32_t sz;
    if (!wadRead("GL/AssetScripts/p/player.ags.phyre", &g_atlasFile, &sz)) return false;
    const uint32_t pixelBytes = kAtlasSize * kAtlasSize * 4;
    // Layout checks for this file (HM GL Steam build): frame-count table and rect table offsets.
    const uint32_t kCounts = 3232, kRects = 27124, kAnims = 281, kFrames = 2319;
    bool ok = sz > pixelBytes + kRects + kFrames * 24 && !memcmp(g_atlasFile + 4, "LGCP", 4);
    int total = 0;
    for (uint32_t a = 0; ok && a < kAnims; a++) {
        int c = *(uint32_t*)(g_atlasFile + kCounts + 20 * a);
        if (c <= 0 || c > 200) ok = false;
        g_animStart[a] = total; g_animCount[a] = c; total += c;
    }
    if (!ok || total != (int)kFrames) {
        logf("hm: player atlas layout not recognised (total=%d); Jacket sprites disabled", total);
        free(g_atlasFile); g_atlasFile = nullptr;
        return true; // text and music still work
    }
    g_anims = kAnims; g_frames = kFrames;
    g_rects = (const float*)(g_atlasFile + kRects);
    g_pixels = g_atlasFile + sz - pixelBytes;
    logf("hm: player atlas ok (%d animations, %d frames)", g_anims, g_frames);
    return true;
}

static const wchar_t* find(Str* arr, int n, int id) {
    for (int i = 0; i < n; i++) if (arr[i].id == id) return arr[i].s;
    return nullptr;
}
const wchar_t* text(int id) {
    const wchar_t* s = g_french ? find(g_fr, g_frN, id) : nullptr;
    return s ? s : find(g_en, g_enN, id);
}
void setFrench(bool on) { g_french = on; }

const uint8_t* atlasPixels() { return g_pixels; }
int animFrames(int anim) { return (anim >= 0 && anim < g_anims) ? g_animCount[anim] : 0; }
bool frame(int anim, int index, Frame* out) {
    if (!g_pixels || anim < 0 || anim >= g_anims || g_animCount[anim] == 0) return false;
    int i = g_animStart[anim] + index % g_animCount[anim];
    const float* r = g_rects + i * 6;
    out->x = (int)lroundf(r[2] * kAtlasSize); out->y = (int)lroundf(r[3] * kAtlasSize);
    out->w = (int)lroundf(r[4] * kAtlasSize); out->h = (int)lroundf(r[5] * kAtlasSize);
    return out->w > 0 && out->h > 0 && out->x + out->w <= kAtlasSize && out->y + out->h <= kAtlasSize;
}

} // namespace hm
