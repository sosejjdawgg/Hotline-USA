// Hotline Anywhere City: shared declarations. GTA 2 9.6 (Steam) / 11.44 share these addresses.
#pragma once
#include <windows.h>
#include <stdint.h>
#include <math.h>
#include "sheets.h"

// ---- logging / config ------------------------------------------------------------------------
void logf(const char* fmt, ...);
extern char g_modDir[MAX_PATH];   // folder holding HotlineCity.asi (GTA 2\scripts)
extern char g_hmDir[MAX_PATH];    // Hotline Miami install (from Melty's HAC_HM_DIR)
extern bool g_testMode;           // HAC_TEST=1 (or HotlineCity-test.flag): scripted checks write HotlineCity-test.log
extern bool g_autostart;          // press Enter through the frontend until a game starts (test mode, lab)

// ---- raw memory ------------------------------------------------------------------------------
template <class T> static inline T& at(uintptr_t p) { return *(T*)p; }

namespace gta {
static const int kBlock = 16384;            // one map block in SCR_f units
static inline float toBlocks(int v) { return v / 16384.0f; }

// Ped field offsets (sheets/hooks.json evidence; seuko + lab)
enum : uint32_t {
    PED_HEADING_A = 0x12C, PED_HEADING_B = 0x12E, PED_TARGET_PED = 0x148, PED_GAMEOBJ = 0x168, PED_CAR = 0x16C,
    PED_WEAPON = 0x170, PED_X = 0x1AC, PED_Y = 0x1B0, PED_Z = 0x1B4, PED_ID = 0x200, PED_ATTACKER = 0x204,
    PED_INVULN = 0x208, PED_COPVALUE = 0x20A, PED_HEALTH = 0x216, PED_OCCUPATION = 0x240, PED_REMAP = 0x244,
    PED_STATE = 0x278, PED_THREAT_SEARCH = 0x288, PED_THREAT_REACT = 0x28C, PED_SIZE = 0x294,
    OBJ_ROTATION = 0x40, OBJ_SPRITE = 0x80, SPRITE_ROTATION = 0x0,
    PLAYER_KEYS = 0x4, PLAYER_PED = 0x2C4, PLAYER_MONEY = 0x2D4,
    WEAPON_AMMO = 0x0, WEAPON_RELOAD = 0x2, WEAPON_ID = 0x1C, WEAPON_PED = 0x24,
};
enum : uint32_t { KEY_UP = 1, KEY_DOWN = 2, KEY_LEFT = 4, KEY_RIGHT = 8, KEY_ATTACK = 0x10, KEY_ENTER = 0x20, KEY_JUMP = 0x40, KEY_PREV = 0x80, KEY_NEXT = 0x100 };
enum { OCC_PLAYER = 0, OCC_DUMMY = 3, STATE_FALLEN = 8, STATE_WASTED = 9, OBJ_PHONE = 163, OBJ_PHONE_RINGING = 164, OBJ_BLOOD = 111 };

static inline uintptr_t game() { return at<uintptr_t>(0x5EB4FC); }
static inline uintptr_t player() { uintptr_t g = game(); return g ? at<uintptr_t>(g + 0x38) : 0; }
static inline uintptr_t playerPed() { uintptr_t p = player(); return p ? at<uintptr_t>(p + PLAYER_PED) : 0; }
static inline uintptr_t pedManager() { return at<uintptr_t>(0x5E5BBC); }
static inline uintptr_t objectManager() { return at<uintptr_t>(0x665784); }
static inline uintptr_t mainCamera() { return at<uintptr_t>(0x5E3CC4); }
static inline bool isRunning() { uintptr_t g = game(); return g && at<int>(g) == 1; } // Game.status: 1 = playing (UnpauseGame sets it)
const char* mapName();                       // "wil" / "ste" / "bil" / "" from the loaded .gmp
int heading(uintptr_t ped);                  // 0..1439, 0 = +y (south), 360 = +x (east)
void setHeading(uintptr_t ped, int h);
static inline int headingFromVector(float dx, float dy) {
    int h = (int)lroundf(atan2f(dx, dy) * 720.0f / 3.14159265f);
    return (h % 1440 + 1440) % 1440;
}

// game functions (sheets/hooks.json, kind=call)
uintptr_t spawnPed(int x, int y, int z, int remap);
void giveWeapon(uintptr_t ped, int weapon, int ammo);
void holdWeapon(uintptr_t ped, int weapon);
void huntPlayer(uintptr_t ped);
void guardSpot(uintptr_t ped);
void standStill(uintptr_t ped);
void killPed(uintptr_t ped);
uintptr_t spawnObject(int type, int x, int y, int z);
void givePowerUp(int type);
void drawText(const wchar_t* s, float x, float y, float scale, int remap); // virtual 640x480 UI coords
bool worldToScreen(int x, int y, int z, float* sx, float* sy);              // GTA 2 UI units (640x480 virtual)
bool playerScreen(float* px, float* py);                                     // client pixels
float spriteScale();
float uiWidth();                                                            // UI units across the window (640 at 4:3)                                                        // HM pixels -> client pixels at the current zoom
void addMoney(int amount);
void setMusicVolume(int v); int musicVolume();

template <class F> static void forEachPed(F f) {
    uintptr_t m = pedManager();
    if (!m) return;
    for (int i = 0; i < 200; i++) {
        uintptr_t p = m + 8 + i * PED_SIZE;
        if (at<int>(p + PED_ID) > 0 && at<uintptr_t>(p + PED_GAMEOBJ)) f(p);
    }
}
template <class F> static void forEachObject(F f) {
    uintptr_t m = objectManager();
    if (!m) return;
    for (int i = 0; i < 3825; i++) {
        uintptr_t o = m + 8 + i * 0x2C;
        uintptr_t spr = at<uintptr_t>(o + 4);
        if (spr) f(o, at<int>(o + 0x18), at<int>(spr + 0x14), at<int>(spr + 0x18), at<int>(spr + 0x1C));
    }
}
} // namespace gta

// ---- Hotline Miami data (read from the player's own copy) ------------------------------------
namespace hm {
bool load();                                  // WAD index, text table, player atlas
const wchar_t* text(int id);                  // HM localisation (English or French), nullptr if missing
void setFrench(bool on);
struct Frame { int x, y, w, h; };
const uint8_t* atlasPixels();                 // 2048x2048 RGBA of the player atlas
bool frame(int anim, int index, Frame* out);  // rectangle of one frame of an atlas animation
int animFrames(int anim);
}

// ---- systems ---------------------------------------------------------------------------------
namespace combat { int onDamage(uintptr_t ped, int amount, uint32_t site); void onDie(uintptr_t ped); void tick(); void setShield(int n); }
namespace combo  { void onKill(ScoreEvent ev, uintptr_t victim); void award(ScoreEvent ev); void tick(); void drawHud(); int count(); DWORD msSinceKill(); long long score(); }
namespace masks  { void tick(); void drawHud(); bool menuOpen(); int current(); int cursor(); bool has(Perk p, int* param = nullptr); void open(int jobIndex); void choose(int idx); uint32_t filterKeys(uint32_t keys); }
namespace jobs   { void tick(); void drawHud(); int active(); void start(int job); bool clearedRecently(); }
namespace npcs   { void tick(); }
namespace music  { void init(); void tick(); void play(MusicCtx ctx, int track = -1); void stop(); MusicCtx context(); DWORD msInContext(); }
namespace input  { void init(); uint32_t filterKeys(uint32_t keys); void tick(); bool aiming(int* heading); void menuInput(int* dir, bool* ok); void force(uint32_t keys, int ticks, int heading); }
namespace render { void endScene(); void tick(); }
namespace lab    { void tick(); }
namespace test   { void init(); void tick(); void check(const char* name, bool ok, const char* detail = ""); void onDamage(uintptr_t ped, int amount, int passed, uint32_t site); }
