// Hotline Miami's soundtrack, streamed from the player's HM folder (sheets/music.json) on its own thread.
// GTA 2's own music (the car radio) is muted while the game is being played.
#include "core.h"
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>

#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_HEADER_ONLY
#include "../third_party/stb/stb_vorbis.c"

namespace music {

static CRITICAL_SECTION g_cs;
static HANDLE g_thread;
static volatile int g_want = -1;      // music row to play (-1 = silence)
static volatile int g_playing = -1;
static volatile float g_volume = 1.0f;
static MusicCtx g_ctx = MusicCtx::car_radio;
static int g_savedGtaVolume = -1;
static unsigned g_radioIndex;
static DWORD g_startedAt;

static DWORD WINAPI worker(void*) {
    const int kBuf = 4, kSamples = 4096;
    HWAVEOUT wo = nullptr;
    WAVEHDR hdr[kBuf] = {};
    short* pcm[kBuf];
    for (int i = 0; i < kBuf; i++) pcm[i] = (short*)calloc(kSamples * 2, sizeof(short));
    stb_vorbis* v = nullptr;
    int channels = 2, rate = 44100, cur = -1;
    for (;;) {
        int want = g_want;
        if (want != cur) {
            if (wo) { waveOutReset(wo); for (int i = 0; i < kBuf; i++) if (hdr[i].dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(wo, &hdr[i], sizeof(WAVEHDR)); waveOutClose(wo); wo = nullptr; }
            if (v) { stb_vorbis_close(v); v = nullptr; }
            cur = want; g_playing = -1;
            if (cur >= 0) {
                char path[MAX_PATH];
                snprintf(path, sizeof path, "%s\\%s", g_hmDir, kMusic[cur].file);
                int err = 0;
                v = stb_vorbis_open_filename(path, &err, nullptr);
                if (!v) { logf("music: cannot open %s (%d)", path, err); }
                else {
                    stb_vorbis_info info = stb_vorbis_get_info(v);
                    channels = info.channels; rate = info.sample_rate;
                    WAVEFORMATEX wf = {WAVE_FORMAT_PCM, 2, (DWORD)rate, (DWORD)rate * 4, 4, 16, 0};
                    if (waveOutOpen(&wo, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) wo = nullptr;
                    for (int i = 0; i < kBuf; i++) { hdr[i] = {}; hdr[i].dwFlags = WHDR_DONE; }
                    g_playing = cur;
                    logf("music: %s (%d Hz, %d ch)", kMusic[cur].file, rate, channels);
                }
            }
        }
        if (!v || !wo) { Sleep(30); continue; }
        bool fed = false;
        for (int i = 0; i < kBuf; i++) {
            if (!(hdr[i].dwFlags & WHDR_DONE)) continue;
            if (hdr[i].dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(wo, &hdr[i], sizeof(WAVEHDR));
            int got = stb_vorbis_get_samples_short_interleaved(v, 2, pcm[i], kSamples * 2);
            if (got == 0) {
                if (kMusic[cur].loop) { stb_vorbis_seek_start(v); got = stb_vorbis_get_samples_short_interleaved(v, 2, pcm[i], kSamples * 2); }
                else { g_want = -2; break; } // track ended: tick() picks the next radio song
            }
            float vol = g_volume;
            for (int s = 0; s < got * 2; s++) pcm[i][s] = (short)(pcm[i][s] * vol);
            hdr[i] = {}; hdr[i].lpData = (LPSTR)pcm[i]; hdr[i].dwBufferLength = got * 4;
            waveOutPrepareHeader(wo, &hdr[i], sizeof(WAVEHDR));
            waveOutWrite(wo, &hdr[i], sizeof(WAVEHDR));
            fed = true;
        }
        if (!fed) Sleep(10);
    }
}

void init() {
    InitializeCriticalSection(&g_cs);
    g_thread = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
}

static int pick(MusicCtx ctx, int n) {
    int idx[64], k = 0;
    for (int i = 0; i < countof(kMusic) && k < 64; i++) if (kMusic[i].ctx == ctx) idx[k++] = i;
    return k ? idx[(unsigned)n % k] : -1;
}

void play(MusicCtx ctx, int track) {
    if (ctx != g_ctx) g_startedAt = GetTickCount();
    g_ctx = ctx;
    int row = track >= 0 ? track : pick(ctx, ctx == MusicCtx::car_radio ? g_radioIndex++ : GetTickCount() / 1000);
    if (row != g_want) g_want = row;
}
void stop() { g_want = -1; }
MusicCtx context() { return g_ctx; }
DWORD msInContext() { return GetTickCount() - g_startedAt; }

void tick() {
    // Mute GTA 2's radio only while a game is being played; give its volume back in menus.
    bool running = gta::isRunning();
    if (running && g_savedGtaVolume < 0) { g_savedGtaVolume = gta::musicVolume(); gta::setMusicVolume(0); }
    if (!running && g_savedGtaVolume >= 0) { gta::setMusicVolume(g_savedGtaVolume); g_savedGtaVolume = -1; }
    if (g_savedGtaVolume >= 0) g_volume = (g_savedGtaVolume > 0 ? g_savedGtaVolume : 127) / 127.0f;
    if (!running) { if (g_want >= 0) g_want = -1; return; }
    if (g_want == -2) play(g_ctx); // a non-looping radio song finished
}

} // namespace music
