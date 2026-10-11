// Hotline Miami sprites drawn into GTA 2's own Direct3D 6 frame (sheets/hm_assets.json): Jacket over the
// player, the worn mask, the big mask icon in the mask menu, and HM's crosshair and lock-on reticle. Frames are cut from the player's own
// HM atlas at runtime and uploaded as textures; nothing is stored on disk.
#define INITGUID
#include "core.h"
#include <ddraw.h>
#include <d3d.h>
#include <stdio.h>

namespace render {

struct Graphics { void* video; void* pad[8]; IDirect3D3* d3d; IDirect3DDevice3* device; IDirect3DViewport3* viewport; }; // plugin-sdk tGraphics
typedef double(__stdcall* EndScene_t)();
static EndScene_t g_origEndScene;
static bool g_fmtKnown, g_fmt8888;
static DDPIXELFORMAT g_pf;

struct Tex { int anim, frame; IDirectDrawSurface4* surf; IDirect3DTexture2* tex; int w, h, tw, th; };
static Tex g_tex[96]; static int g_texN;
static DWORD g_jacketAt;            // last frame Jacket was drawn (GTA 2's own player sprite stays hidden meanwhile)

bool jacketShown() { return g_jacketAt && GetTickCount() - g_jacketAt < 300; }

static Graphics* gfx() {
    HMODULE m = GetModuleHandleA("d3ddll.dll");
    return m ? *(Graphics**)((uintptr_t)m + 0x485E0) : nullptr;
}

static HRESULT CALLBACK enumFmt(DDPIXELFORMAT* pf, void*) {
    if (!(pf->dwFlags & DDPF_ALPHAPIXELS) || !(pf->dwFlags & DDPF_RGB)) return D3DENUMRET_OK;
    if (pf->dwRGBBitCount == 32 && pf->dwRGBAlphaBitMask == 0xFF000000) { g_pf = *pf; g_fmt8888 = true; return D3DENUMRET_CANCEL; }
    if (pf->dwRGBBitCount == 16 && pf->dwRGBAlphaBitMask == 0xF000 && !g_fmt8888) g_pf = *pf;
    return D3DENUMRET_OK;
}

static Tex* texture(IDirect3DDevice3* dev, int anim, int frame) {
    for (int i = 0; i < g_texN; i++) if (g_tex[i].anim == anim && g_tex[i].frame == frame) return g_tex[i].surf ? &g_tex[i] : nullptr;
    if (g_texN >= 96) return nullptr;
    Tex& t = g_tex[g_texN++];
    t = {anim, frame, nullptr, nullptr, 0, 0, 0, 0};
    hm::Frame f;
    if (!hm::frame(anim, frame, &f)) return nullptr;
    if (!g_fmtKnown) { dev->EnumTextureFormats(enumFmt, nullptr); g_fmtKnown = true; logf("render: texture format %s", g_fmt8888 ? "ARGB8888" : g_pf.dwRGBBitCount ? "ARGB4444" : "none"); }
    if (!g_pf.dwRGBBitCount) return nullptr;
    IDirectDrawSurface4* target = nullptr; IUnknown* ddu = nullptr; IDirectDraw4* dd = nullptr;
    if (FAILED(dev->GetRenderTarget(&target)) || !target) return nullptr;
    target->GetDDInterface((void**)&ddu);
    target->Release();
    if (!ddu || FAILED(ddu->QueryInterface(IID_IDirectDraw4, (void**)&dd))) { if (ddu) ddu->Release(); return nullptr; }
    ddu->Release();
    int tw = 8, th = 8;
    while (tw < f.w) tw *= 2;
    while (th < f.h) th *= 2;
    DDSURFACEDESC2 sd = {}; sd.dwSize = sizeof sd;
    sd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    sd.dwWidth = tw; sd.dwHeight = th; sd.ddpfPixelFormat = g_pf;
    sd.ddsCaps.dwCaps = DDSCAPS_TEXTURE; sd.ddsCaps.dwCaps2 = DDSCAPS2_TEXTUREMANAGE;
    IDirectDrawSurface4* s = nullptr;
    HRESULT hr = dd->CreateSurface(&sd, &s, nullptr);
    dd->Release();
    if (FAILED(hr) || !s) { logf("render: CreateSurface failed %08lx", hr); return nullptr; }
    DDSURFACEDESC2 ld = {}; ld.dwSize = sizeof ld;
    if (FAILED(s->Lock(nullptr, &ld, DDLOCK_WAIT | DDLOCK_WRITEONLY, nullptr))) { s->Release(); return nullptr; }
    const uint8_t* px = hm::atlasPixels();
    for (int y = 0; y < th; y++) {
        uint8_t* row = (uint8_t*)ld.lpSurface + y * ld.lPitch;
        for (int x = 0; x < tw; x++) {
            uint32_t r = 0, g = 0, b = 0, a = 0;
            if (x < f.w && y < f.h) { const uint8_t* p = px + ((f.y + y) * 2048 + f.x + x) * 4; r = p[0]; g = p[1]; b = p[2]; a = p[3]; }
            if (g_fmt8888) ((uint32_t*)row)[x] = (a << 24) | (r << 16) | (g << 8) | b;
            else ((uint16_t*)row)[x] = (uint16_t)(((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
        }
    }
    s->Unlock(nullptr);
    if (FAILED(s->QueryInterface(IID_IDirect3DTexture2, (void**)&t.tex))) { s->Release(); return nullptr; }
    t.surf = s; t.w = f.w; t.h = f.h; t.tw = tw; t.th = th;
    return &t;
}

// one textured quad centred on (cx, cy) in client pixels, rotated by `angle` radians, scaled
static void quad(IDirect3DDevice3* dev, Tex* t, float cx, float cy, float scale, float angle, uint32_t color = 0xFFFFFFFF) {
    float hw = t->w * scale * 0.5f, hh = t->h * scale * 0.5f, c = cosf(angle), s = sinf(angle);
    float u1 = (float)t->w / t->tw, v1 = (float)t->h / t->th;
    float corner[4][4] = {{-hw, -hh, 0, 0}, {hw, -hh, u1, 0}, {hw, hh, u1, v1}, {-hw, hh, 0, v1}};
    D3DTLVERTEX v[4];
    for (int i = 0; i < 4; i++) {
        float x = corner[i][0], y = corner[i][1];
        v[i].sx = cx + x * c - y * s; v[i].sy = cy + x * s + y * c; v[i].sz = 0.0f; v[i].rhw = 1.0f;
        v[i].color = color; v[i].specular = 0; v[i].tu = corner[i][2]; v[i].tv = corner[i][3];
    }
    dev->SetTexture(0, t->tex);
    dev->DrawPrimitive(D3DPT_TRIANGLEFAN, D3DFVF_TLVERTEX, v, 4, 0);
}

// one flat coloured bar centred on (cx, cy), rotated by `angle` radians
static void bar(IDirect3DDevice3* dev, float cx, float cy, float w, float h, float angle, uint32_t color) {
    float hw = w * 0.5f, hh = h * 0.5f, c = cosf(angle), s = sinf(angle);
    float corner[4][2] = {{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};
    D3DTLVERTEX v[4];
    for (int i = 0; i < 4; i++) {
        float x = corner[i][0], y = corner[i][1];
        v[i].sx = cx + x * c - y * s; v[i].sy = cy + x * s + y * c; v[i].sz = 0.0f; v[i].rhw = 1.0f;
        v[i].color = color; v[i].specular = 0; v[i].tu = 0; v[i].tv = 0;
    }
    dev->SetTexture(0, nullptr);
    dev->DrawPrimitive(D3DPT_TRIANGLEFAN, D3DFVF_TLVERTEX, v, 4, 0);
}

// HM colours: the cursor cycles hot pink -> violet -> cyan
static uint32_t hmColour(DWORD now, int phase) {
    static const uint32_t cols[] = {0xFFFF2E9A, 0xFFB43CFF, 0xFF2EE6FF, 0xFFB43CFF};
    return cols[((now / 90) + phase) % 4];
}

// HM's crosshair: four short bars round a gap, black outline, pulsing; a lock adds turning corner brackets.
static void drawCrosshair(IDirect3DDevice3* dev, float x, float y, float ui, bool locked, DWORD now) {
    float pulse = 1.0f + 0.12f * sinf(now / 110.0f);
    float gap = (locked ? 6.0f : 4.0f) * ui * pulse, len = 6.0f * ui, th = 2.0f * ui, o = 1.0f * ui;
    uint32_t col = hmColour(now, 0);
    for (int pass = 0; pass < 2; pass++) {
        uint32_t c = pass ? col : 0xC0000000;
        float e = pass ? 0 : o * 2;
        for (int k = 0; k < 4; k++) {
            float a = k * 1.5707963f, dx = cosf(a), dy = sinf(a), d = gap + len * 0.5f;
            bar(dev, x + dx * d, y + dy * d, (k % 2 ? th : len) + e, (k % 2 ? len : th) + e, 0, c);
        }
        bar(dev, x, y, th + e, th + e, 0, c);
    }
    if (!locked) return;
    float r = 15.0f * ui, arm = 6.0f * ui, spin = now / 400.0f;
    for (int pass = 0; pass < 2; pass++) {
        uint32_t c = pass ? hmColour(now, 2) : 0xC0000000;
        float e = pass ? 0 : o * 2;
        for (int k = 0; k < 4; k++) {
            float a = spin + k * 1.5707963f + 0.785398f, cx = x + cosf(a) * r, cy = y + sinf(a) * r;
            // an L at each corner of a turning square
            float a1 = a + 3.1415926f * 0.75f, a2 = a - 3.1415926f * 0.75f;
            bar(dev, cx + cosf(a1) * arm * 0.5f, cy + sinf(a1) * arm * 0.5f, arm + e, th + e, a1, c);
            bar(dev, cx + cosf(a2) * arm * 0.5f, cy + sinf(a2) * arm * 0.5f, arm + e, th + e, a2, c);
        }
    }
}

// The frame GTA 2 renders can differ from the window it is shown in (window wrappers scale it):
// draw in the render target's pixels.
static void targetSize(IDirect3DDevice3* dev, float* w, float* h) {
    RECT rc; GetClientRect(gta::window(), &rc);
    *w = (float)rc.right; *h = (float)rc.bottom;
    IDirectDrawSurface4* target = nullptr;
    if (FAILED(dev->GetRenderTarget(&target)) || !target) return;
    DDSURFACEDESC2 sd = {}; sd.dwSize = sizeof sd;
    if (SUCCEEDED(target->GetSurfaceDesc(&sd)) && sd.dwWidth && sd.dwHeight) { *w = (float)sd.dwWidth; *h = (float)sd.dwHeight; }
    target->Release();
}

static int assetAnim(Asset a) { for (const AssetRow& r : kAssets) if (r.use == a) return r.anim; return -1; }

static void draw() {
    Graphics* g = gfx();
    if (!g || !g->device || !hm::atlasPixels() || !gta::isRunning()) return;
    IDirect3DDevice3* dev = g->device;
    DWORD st[8];
    dev->GetRenderState(D3DRENDERSTATE_ZENABLE, &st[0]); dev->GetRenderState(D3DRENDERSTATE_ZWRITEENABLE, &st[1]);
    dev->GetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, &st[2]); dev->GetRenderState(D3DRENDERSTATE_SRCBLEND, &st[3]);
    dev->GetRenderState(D3DRENDERSTATE_DESTBLEND, &st[4]); dev->GetRenderState(D3DRENDERSTATE_CULLMODE, &st[5]);
    dev->GetTextureStageState(0, D3DTSS_MAGFILTER, &st[6]); dev->GetTextureStageState(0, D3DTSS_MINFILTER, &st[7]);
    dev->SetRenderState(D3DRENDERSTATE_ZENABLE, FALSE); dev->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, TRUE); dev->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA); dev->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
    dev->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTFG_POINT); dev->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTFN_POINT);

    RECT rc; GetClientRect(gta::window(), &rc);
    float W, H;
    targetSize(dev, &W, &H);
    float ui = H / 480.0f;
    float kx = rc.right ? W / rc.right : 1.0f, ky = rc.bottom ? H / rc.bottom : 1.0f; // client pixels -> target pixels
    static bool sizeLogged;
    if (!sizeLogged && rc.right) { sizeLogged = true; logf("render: frame %.0fx%.0f in a %ldx%ld window", W, H, rc.right, rc.bottom); }
    uintptr_t pp = gta::playerPed();
    DWORD now = GetTickCount();
    int icons = assetAnim(Asset::mask_icons);
    if (masks::menuOpen()) {
        Tex* t = texture(dev, icons, masks::cursor());
        if (t) quad(dev, t, W * 0.5f, H * 0.5f - 40 * ui, 5.0f * ui, sinf(now / 300.0f) * 0.12f);
    } else if (masks::current() >= 0) {
        Tex* t = texture(dev, icons, kMasks[masks::current()].iconFrame);
        if (t) quad(dev, t, 30 * ui, H - 50 * ui, 2.0f * ui, 0);
    }
    if (pp && !at<uintptr_t>(pp + gta::PED_CAR)) {
        float sx, sy;
        if (gta::playerScreen(&sx, &sy)) {
            sx *= kx; sy *= ky;
            int weapon = -1; uintptr_t w = at<uintptr_t>(pp + gta::PED_WEAPON);
            if (w) weapon = at<int>(w + gta::WEAPON_ID);
            Asset a = weapon < 0 ? Asset::jacket_unarmed : weapon == 0 || weapon == 10 ? Asset::jacket_pistol : weapon == 1 || weapon == 9 ? Asset::jacket_smg
                    : weapon == 6 ? Asset::jacket_shotgun : weapon == 7 ? Asset::jacket_melee : Asset::jacket_pistol;
            bool dead = at<uint16_t>(pp + gta::PED_HEALTH) == 0;
            if (dead) a = Asset::jacket_dead;
            int anim = assetAnim(a);
            int frames = hm::animFrames(anim);
            int st = at<int>(pp + gta::PED_STATE);
            int fr = dead ? frames - 1 : (st == 0 ? (now / 90) % (frames ? frames : 1) : 0);
            Tex* t = texture(dev, anim, fr);
            // HM sprites face east; GTA 2 heading 0 faces south (+y), 360 east
            float ang = (360 - gta::heading(pp)) * 3.14159265f / 720.0f;
            int aim;
            if (input::aiming(&aim) && !dead) ang = (360 - aim) * 3.14159265f / 720.0f;
            if (t) { quad(dev, t, sx, sy, gta::spriteScale() * ui, ang); g_jacketAt = now; }
        }
        float cx, cy; bool pad;
        if (!masks::menuOpen() && at<uint16_t>(pp + gta::PED_HEALTH) && input::crosshair(&cx, &cy, &pad))
            drawCrosshair(dev, cx * kx, cy * ky, ui, input::lockTarget() != 0, now);
    }
    dev->SetRenderState(D3DRENDERSTATE_ZENABLE, st[0]); dev->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, st[1]);
    dev->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, st[2]); dev->SetRenderState(D3DRENDERSTATE_SRCBLEND, st[3]);
    dev->SetRenderState(D3DRENDERSTATE_DESTBLEND, st[4]); dev->SetRenderState(D3DRENDERSTATE_CULLMODE, st[5]);
    dev->SetTextureStageState(0, D3DTSS_MAGFILTER, st[6]); dev->SetTextureStageState(0, D3DTSS_MINFILTER, st[7]);
    dev->SetTexture(0, nullptr);
}

static double __stdcall hEndScene() {
    draw();
    return g_origEndScene();
}

void endScene() {}

// GTA 2 fills its renderer function table (0x5952A8..) when d3ddll loads; swap gbh_EndScene once it is there.
void tick() {
    EndScene_t* slot = (EndScene_t*)0x595318;
    if (*slot && *slot != hEndScene) { g_origEndScene = *slot; *slot = hEndScene; logf("render: EndScene hooked"); }
}

} // namespace render
