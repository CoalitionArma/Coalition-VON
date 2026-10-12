/* 
 * CRF TeamSpeak 3 Proximity VOIP Plugin (Windows-only, Arma Reforger)
 *
 * - DIRECT (VONType=0):
 *     Uses LeftGain/RightGain from VONData (the game works out distance, voice range tier, walls and
 *     direction, so loudness comes entirely from there; gains can exceed 1 for the loud tiers).
 *     Adds a muffle low-pass from MuffledDecibels (e.g. -12 dB building ~4.5 kHz, -18 dB vehicle
 *     ~2.7 kHz), a mild behind-the-head low-pass from BehindIntensity, a +5 dB make-up gain and a
 *     soft limiter so loud close talkers don't crackle.
 *
 * - RADIO (VONType=1):
 *     If a local radio in RadioData.json has the SAME string Frequency
 *     (exact match, e.g. "55500") and, if present on both sides, the same
 *     TimeDeviation: render radio with Volume (0..9 → 0..1) and Stereo
 *     (0=both, 1=right, 2=left) through an Acre-like chain:
 *       boost ×3 → pink+white noise (less with higher value) →
 *       ring modulation mix → foldback distortion → LP 4kHz → HP 750Hz.
 *     ConnectionQuality (0..1) injects more noise and softens voice as quality drops.
 *
 *     Behavior:
 *       - If there is a RADIO match → radio-only (consistent with previous).
 *       - If there is NO radio match → DIRECT fallback using Left/Right only
 *         (same direct-path processing as above).
 *
 * - INTERCOM (VONType=2): a talker on the listener's vehicle intercom. Clear and centred at the game's
 *     gain: telephone band (300 Hz - 3.4 kHz), light saturation, make-up gain, soft limiter. No muffle,
 *     no distance, no static.
 *
 * - SPEAKER (VONType=3): a radio transmission heard through a nearby vehicle's speaker. The radio chain
 *     above (driven by ConnectionQuality), then the direct path: positioned at the vehicle by the game's
 *     Left/Right gains, with its muffle and behind filters.
 *
 *     Plugins before 3.0 read types 2 and 3 as DIRECT: intercom is then a plain clear voice and the
 *     speaker an unfiltered voice from the vehicle.
 *
 * - Mic control: IsTransmitting toggles CLIENT_INPUT_DEACTIVATED.
 * - Auto-move to VONChannelName (and optional VONChannelPassword) when InGame==true.
 * - File reloads, mutes and channel moves run on a worker thread woken by profile-folder changes
 *   (audio callbacks are pure DSP).
 *
 * Build (VS, Release|x64):
 *   Compile as C or C++; Link with: Shell32.lib; Ole32.lib
 */

#define _CRT_SECURE_NO_WARNINGS

#include <Windows.h>
#include <knownfolders.h>
#include <limits.h>
#include <malloc.h>
#include <math.h>
#include <shlobj.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef uint64_t uint64;
typedef uint16_t anyID;

#include "plugin.h"
#include "cJSON.h"
#include "teamspeak/public_definitions.h"
#include "teamspeak/public_errors.h"
#include "teamspeak/public_errors_rare.h"
#include "teamspeak/public_rare_definitions.h"
#include "ts3_functions.h"

#define CLIENT_INPUT_DEACTIVATED 10

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846264338327950288
#endif
#ifndef M_PI_F
#define M_PI_F 3.14159265358979323846f
#endif
#ifndef TWO_PI_F
#define TWO_PI_F (2.0f * M_PI_F)
#endif

/* ---------------------------------------------------------------------------
   Config / Timings
--------------------------------------------------------------------------- */
#define PLUGIN_API_VERSION 26
#define PATH_BUFSIZE 1024
#define MAX_TRACKED 2048

/* Reload cadences. VON and radio data are reloaded as soon as the game writes them (the worker wakes on
   profile-folder change notifications); these are the slow-path cadences for everything else. */
#define SERVERDATA_RELOAD_MS 500
#define SLOW_TICK_MS 200        /* channel moves, auto-connect, mute enforcement fallback */
#define INFO_WRITE_MS 5000      /* VONPluginInfo.json refresh */
#define TALKING_WRITE_MIN_MS 50 /* VONTalking.json debounce */

/* Worker: wakes on a file change or after this long, whichever comes first. Was 200 ms, which made the
   old 50 ms VON reload cadence an effective 200 ms (push-to-talk and position lag). */
#define WORKER_TICK_MS 20
#define SERVER_WRITE_SUPPRESS_MS 750
#define MOVE_BACKOFF_MIN_MS 1500
#define MOVE_BACKOFF_MAX_MS 30000

/* Raised global JSON attribute polling throttle (kept >=250 ms) */
#define JSON_RELOAD_THROTTLE_MS 300

/* Game liveness (VONHeartbeat.json, rewritten by the game every second). A crash never runs the
   game's cleanup, so without this the plugin stays "in game" forever: mic held open, player stuck in
   the VON channel, stale audio. */
#define HEARTBEAT_MIC_STALE_MS 3000    /* stop forcing the mic on */
#define HEARTBEAT_GONE_MS 10000        /* treat the game as gone: leave VON, return to previous channel */

/* Radio filter (Acre-inspired) */
#define TS_SAMPLE_RATE 48000.0f
#define RADIO_RINGMOD_HZ 35.0f
#define RADIO_LP_HZ 4000.0f
#define RADIO_LP_Q 2.0f
#define RADIO_HP_HZ 750.0f
#define RADIO_HP_Q 0.97f
#define BASE_DISTORTION_LEVEL 0.43f
#define MUFFLE_STAGES 3 /* 2nd-order LP x3 ≈ much steeper rolloff */
/* Babbel effect (foreign language distortion) - IMPROVED for unintelligibility */
#define BABBEL_MOD_FREQ 2000.0f   /* Higher = more scrambling (was 5200) */
#define BABBEL_MOD_DEPTH 0.85f    /* Ring mod intensity 0-1 */
#define BABBEL_LP1_FREQ 900.0f   /* Narrower bandpass = more muffled (was 1560) */
#define BABBEL_LP2_FREQ 1800.0f   /* Narrower = removes clarity (was 2600) */
#define BABBEL_LP_Q 0.6f          /* Higher Q = more resonant/hollow */
#define BABBEL_VOLUME_BOOST 8.5f  /* Higher boost = more distortion (was 6.0) */
#define BABBEL_CHAOS_AMOUNT 0.3f /* Adds random pitch variation */

/* Logging (optional lightweight) */
#ifndef CRF_LOGGING
#define CRF_LOGGING 0
#endif

#if CRF_LOGGING
static void logf(const char* fmt, ...)
{
    char    buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
}
#else
#define logf(...) ((void)0)
#endif

#define PATH_SEP "\\"
/* Fixed-size processing chunk for radio path */
#define RADIO_PROCESS_CHUNK 1024

/* ----------------- Add near other Config constants ----------------- */
#define DIRECT_GAIN_DB 5.0f
#define DIRECT_GAIN_MUL 1.77827941f /* 10^(5/20) */
#define RADIO_GAIN_DB 3.0f
#define RADIO_GAIN_MUL 1.41253754f /* 10^(3/20) */

/* Vehicle intercom: headset/throat-mic sound */
#define INTERCOM_HP_HZ 300.0f
#define INTERCOM_LP_HZ 3400.0f
#define INTERCOM_DRIVE 2.0f   /* tanh saturation: unity gain for quiet speech, rounds off peaks */
#define INTERCOM_MAKEUP 2.0f  /* +6 dB: the band-limit takes away a lot of the voice's energy */

/* Vehicle speaker: level of the radio-coloured voice before the direct path positions it */
#define SPEAKER_PRE_GAIN 0.6f


static struct TS3Functions ts3Functions;
static uint64              g_prevChannelId     = 0; /* user’s last non-game channel (this conn) */
static uint64              g_gameChannelId     = 0; /* cached ID of the VONChannelName */
static unsigned long       g_nextReturnTryTick = 0;
static unsigned long       g_returnBackoffMs   = MOVE_BACKOFF_MIN_MS;
static char                g_SD_serverIp[512]       = {0};
static char                g_SD_serverPassword[512] = {0};
static char                g_SD_ingameName[512]     = {0};
static uint64              g_autoConnSch            = 0; // handler ID we auto-connected to
static char                g_lastAutoConnectIp[256] = {0};
static uint64              g_lastAutoConnectSch     = 0;

/* ---------------------------------------------------------------------------
   Helpers
--------------------------------------------------------------------------- */
/* Find channel by exact (or case-insensitive) name */
static int confirm_autoconnect(const char* ip, const char* nickname)
{
    char msg[512];
    _snprintf(msg, sizeof(msg), "Do you want to connect TeamSpeak to:\n\nServer: %s\nNickname: %s\n\nPress Yes to connect, No to cancel.", ip, nickname);
    int res = MessageBoxA(NULL, msg, "Arma Reforger Auto-Connect", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST);
    return (res == IDYES);
}

static uint64 find_channel_by_name(uint64 sch, const char* name)
{
    if (!name || !name[0])
        return 0;
    uint64* chList = NULL;
    if (ts3Functions.getChannelList(sch, &chList) != ERROR_ok || !chList)
        return 0;

    uint64 found = 0;
    for (size_t i = 0; chList[i]; ++i) {
        char* nm = NULL;
        if (ts3Functions.getChannelVariableAsString(sch, chList[i], CHANNEL_NAME, &nm) == ERROR_ok && nm) {
            int eq = (strcmp(nm, name) == 0) || (_stricmp ? _stricmp(nm, name) == 0 : 0);
            ts3Functions.freeMemory(nm);
            if (eq) {
                found = chList[i];
                break;
            }
        }
    }
    ts3Functions.freeMemory(chList);
    return found;
}

/* Verify a channel ID still exists */
static int channel_exists(uint64 sch, uint64 chId)
{
    if (!chId)
        return 0;
    char*        nm  = NULL;
    unsigned int err = ts3Functions.getChannelVariableAsString(sch, chId, CHANNEL_NAME, &nm);
    if (err == ERROR_ok && nm) {
        ts3Functions.freeMemory(nm);
        return 1;
    }
    return 0;
}
static inline float clampf(float x, float lo, float hi)
{
    return (x < lo) ? lo : (x > hi ? hi : x);
}
static inline short clamp_i16(float v)
{
    if (v > 32767.0f)
        return 32767;
    if (v < -32768.0f)
        return -32768;
    return (short)v;
}
static inline float gain_to_db(float gain)
{
    if (gain <= 0.000001f)
        return -100.0f;
    float db = 20.0f * (float)log10(gain);
    return clampf(db, -100.0f, 20.0f);
}
static void trim_inplace(char* s)
{
    if (!s)
        return;
    size_t len = strlen(s), i = 0;
    while (i < len && (unsigned char)s[i] <= ' ')
        i++;
    size_t j = len;
    while (j > i && (unsigned char)s[j - 1] <= ' ')
        j--;
    if (i > 0)
        memmove(s, s + i, j - i);
    s[j - i] = '\0';
}

/* ---------------------------------------------------------------------------
   Paths
--------------------------------------------------------------------------- */
static void getDocumentsPath(char* outBuf, size_t bufSize)
{
    PWSTR wpath = NULL;
    outBuf[0]   = '\0';
    if (SHGetKnownFolderPath(&FOLDERID_Documents, 0, NULL, &wpath) == S_OK && wpath) {
        size_t conv = wcstombs(outBuf, wpath, bufSize - 1);
        if (conv != (size_t)-1)
            outBuf[conv] = '\0';
        CoTaskMemFree(wpath);
    }
}
static void ensureParentDirExists(const char* filePath)
{
    char dir[PATH_BUFSIZE];
    _snprintf(dir, sizeof(dir), "%s", filePath);
    char* last = strrchr(dir, '\\');
    if (!last)
        last = strrchr(dir, '/');
    if (last)
        *last = '\0';
    if (dir[0])
        SHCreateDirectoryExA(NULL, dir, NULL);
}
static void buildVONDataPath(char* outBuf, size_t bufSize)
{
    char docs[PATH_BUFSIZE];
    getDocumentsPath(docs, sizeof(docs));
    if (!docs[0]) {
        outBuf[0] = '\0';
        return;
    }
    _snprintf(outBuf, (int)bufSize, "%s%sMy Games%sArmaReforger%sprofile%sVONData.json", docs, PATH_SEP, PATH_SEP, PATH_SEP, PATH_SEP);
}
static void buildServerJsonPath(char* outBuf, size_t bufSize)
{
    char docs[PATH_BUFSIZE];
    getDocumentsPath(docs, sizeof(docs));
    if (!docs[0]) {
        outBuf[0] = '\0';
        return;
    }
    _snprintf(outBuf, (int)bufSize, "%s%sMy Games%sArmaReforger%sprofile%sVONServerData.json", docs, PATH_SEP, PATH_SEP, PATH_SEP, PATH_SEP);
}
static void buildRadioJsonPath(char* outBuf, size_t bufSize)
{
    char docs[PATH_BUFSIZE];
    getDocumentsPath(docs, sizeof(docs));
    if (!docs[0]) {
        outBuf[0] = '\0';
        return;
    }
    _snprintf(outBuf, (int)bufSize, "%s%sMy Games%sArmaReforger%sprofile%sRadioData.json", docs, PATH_SEP, PATH_SEP, PATH_SEP, PATH_SEP);
}
static void buildProfileFilePath(const char* fileName, char* outBuf, size_t bufSize)
{
    char docs[PATH_BUFSIZE];
    getDocumentsPath(docs, sizeof(docs));
    if (!docs[0]) {
        outBuf[0] = '\0';
        return;
    }
    _snprintf(outBuf, (int)bufSize, "%s%sMy Games%sArmaReforger%sprofile%s%s", docs, PATH_SEP, PATH_SEP, PATH_SEP, PATH_SEP, fileName);
}
static void buildHeartbeatPath(char* outBuf, size_t bufSize)
{
    char docs[PATH_BUFSIZE];
    getDocumentsPath(docs, sizeof(docs));
    if (!docs[0]) {
        outBuf[0] = '\0';
        return;
    }
    _snprintf(outBuf, (int)bufSize, "%s%sMy Games%sArmaReforger%sprofile%sVONHeartbeat.json", docs, PATH_SEP, PATH_SEP, PATH_SEP, PATH_SEP);
}

/* ---------------------------------------------------------------------------
   File watcher (worker-driven cadence)
--------------------------------------------------------------------------- */
typedef struct {
    FILETIME lastWrite;
    int      loadedOnce;
} WatchedFileState;

static int filetime_equal(FILETIME a, FILETIME b)
{
    return (a.dwLowDateTime == b.dwLowDateTime) && (a.dwHighDateTime == b.dwHighDateTime);
}
static int file_modified_since_last(const char* path, WatchedFileState* st, FILETIME* outWrite)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad))
        return 0;
    *outWrite = fad.ftLastWriteTime;
    if (!st->loadedOnce || !filetime_equal(st->lastWrite, fad.ftLastWriteTime)) {
        st->lastWrite  = fad.ftLastWriteTime;
        st->loadedOnce = 1;
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
   Reusable buffers
--------------------------------------------------------------------------- */
static char*  g_vonBuf   = NULL;
static size_t g_vonCap   = 0;
static char*  g_radioBuf = NULL;
static size_t g_radioCap = 0;
static char*  g_srvBuf   = NULL;
static size_t g_srvCap   = 0;

static int read_file_reuse(const char* path, char** pBuf, size_t* pCap, long* outSz)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 4 * 1024 * 1024) {
        fclose(f);
        return 0;
    }
    size_t need = (size_t)sz + 1;
    if (*pCap < need) {
        size_t newCap = (*pCap == 0) ? 8192 : *pCap;
        while (newCap < need)
            newCap = (size_t)(newCap * 1.5) + 1024;
        char* nb = (char*)realloc(*pBuf, newCap);
        if (!nb) {
            fclose(f);
            return 0;
        }
        *pBuf = nb;
        *pCap = newCap;
    }
    fread(*pBuf, 1, (size_t)sz, f);
    (*pBuf)[sz] = '\0';
    fclose(f);
    if (outSz)
        *outSz = sz;
    return 1;
}

/* ---------------------------------------------------------------------------
   Data
--------------------------------------------------------------------------- */
typedef enum { VON_DIRECT = 0, VON_RADIO = 1, VON_INTERCOM = 2, VON_SPEAKER = 3 } EVONType;

/* Unknown types (a newer game) are played as direct speech */
static inline EVONType von_type_from_int(int value)
{
    return (value >= VON_DIRECT && value <= VON_SPEAKER) ? (EVONType)value : VON_DIRECT;
}

typedef struct {
    anyID         id;
    EVONType      type;
    float         leftGain;
    float         rightGain;
    char          txFreq[64];
    int           txTimeDev;
    float         connQ;
    char          txFaction[64];
    float         muffledDb;
    float         behindIntensity;
    int           sameLanguage;
    unsigned long lastUpdateTick;
} VonEntry;

/* -------- Double-buffered snapshots -------- */
typedef struct {
    VonEntry      entries[MAX_TRACKED];
    size_t        count;
    int           loaded;
    unsigned long buildTick;
} VonSnapshot;

typedef struct {
    char freq[64];
    int  timeDev;
    int  volStep;
    int  stereo;
    char faction[64];
} RadioLocal;

typedef struct {
    RadioLocal    list[256];
    size_t        count;
    int           loaded;
    unsigned long buildTick;
} RadioSnapshot;

/* Two buffers each; audio reads ACTIVE, worker writes INACTIVE */
static VonSnapshot   g_VonSnap[2]   = {0};
static RadioSnapshot g_RadioSnap[2] = {0};

/* Active indices (0 or 1). Audio reads via atomic load; worker flips. */
static volatile long g_vonActiveIdx   = 0;
static volatile long g_radioActiveIdx = 0;

/* Paths (was g_Von.jsonPath earlier) */
static char g_VonPath[PATH_BUFSIZE] = {0};

/* ServerData cache */
static char          g_ServerPath[PATH_BUFSIZE] = {0};
static int           g_SD_have = 0, g_SD_inGame = 0;
static char          g_SD_chanName[512]         = {0};
static char          g_SD_chanPass[512]         = {0};
static unsigned long g_serverWatchSuppressUntil = 0;

/* Last written snapshot to avoid unnecessary writes */
static struct {
    unsigned tsClientID;
    int      inGame;
    char     pluginVersionStr[64]; // now stored as string
    char     chanName[512];
    char     chanPass[512];
    int      valid;
} g_lastServerWritten = {0, 0, {0}, {0}, {0}, 0};

/* Paths + watchers */
static char             g_RadioPath[PATH_BUFSIZE] = {0};
static WatchedFileState g_vonDataWatch = {0}, g_serverWatch = {0}, g_radioWatch = {0};

/* Mic + state gate (worker sets; audio reads) */
static int           g_IsTransmitting = 0, g_LastMicActive = -1;
static volatile long g_inVonActiveFlag = 0; /* 0/1 set by worker, read by audio */

/* Per-client volume modifier state and muting control */
typedef struct {
    anyID id;
    int   have;
    int   lastMuted;
    float lastDb;
    int   isMutedByPlugin;    /* NEW: tracks if we muted this client */
    int   lastInRange;        /* NEW: tracks if client was in range last check */
} ClientApplyState;
static ClientApplyState g_Last[4096];
static size_t           g_LastCount = 0;

/* Threshold for considering a user "out of range" (very low gain) */
#define OUT_OF_RANGE_THRESHOLD 0.001f

/* ---------------------------------------------------------------------------
   Proximity-based muting functions
   
   These functions implement automatic muting of users who are not within
   audible range of the local player. When a speaker's gain values (indicating
   proximity/volume) fall below the threshold, they are muted via the TeamSpeak
   API. When they come back into range, they are automatically unmuted.
--------------------------------------------------------------------------- */

/* ---------------------------------------------------------------------------
   Filters / DSP for RADIO
--------------------------------------------------------------------------- */
typedef struct {
    float b0, b1, b2, a1, a2;
    float z1, z2;
} Biquad;
static inline void biquad_reset(Biquad* q)
{
    q->z1 = q->z2 = 0.0f;
}
static inline float biquad_tick(Biquad* q, float x)
{
    float y = q->b0 * x + q->z1;
    q->z1   = q->b1 * x - q->a1 * y + q->z2;
    q->z2   = q->b2 * x - q->a2 * y;
    return y;
}
static void biquad_calc_lowpass(Biquad* q, float fs, float fc, float Q)
{
    float w0 = 2.0f * (float)M_PI * fc / fs, cw = cosf(w0), sw = sinf(w0), alpha = sw / (2.0f * Q);
    float b0 = (1.0f - cw) * 0.5f, b1 = 1.0f - cw, b2 = (1.0f - cw) * 0.5f;
    float a0 = 1.0f + alpha, a1 = -2.0f * cw, a2 = 1.0f - alpha;
    q->b0 = b0 / a0;
    q->b1 = b1 / a0;
    q->b2 = b2 / a0;
    q->a1 = a1 / a0;
    q->a2 = a2 / a0;
    biquad_reset(q);
}
static void biquad_calc_highpass(Biquad* q, float fs, float fc, float Q)
{
    float w0 = 2.0f * (float)M_PI * fc / fs, cw = cosf(w0), sw = sinf(w0), alpha = sw / (2.0f * Q);
    float b0 = (1.0f + cw) * 0.5f, b1 = -(1.0f + cw), b2 = (1.0f + cw) * 0.5f;
    float a0 = 1.0f + alpha, a1 = -2.0f * cw, a2 = 1.0f - alpha;
    q->b0 = b0 / a0;
    q->b1 = b1 / a0;
    q->b2 = b2 / a0;
    q->a1 = a1 / a0;
    q->a2 = a2 / a0;
    biquad_reset(q);
}

/* Lowpass coefficient update WITHOUT resetting z1/z2 (for smooth cutoff changes) */
static void biquad_update_lowpass(Biquad* q, float fs, float fc, float Q)
{
    if (fc < 30.0f)
        fc = 30.0f;
    if (fc > fs * 0.45f)
        fc = fs * 0.45f;
    if (Q < 0.2f)
        Q = 0.2f;
    float w0 = 2.0f * (float)M_PI * fc / fs, cw = cosf(w0), sw = sinf(w0), alpha = sw / (2.0f * Q);
    float b0 = (1.0f - cw) * 0.5f, b1 = 1.0f - cw, b2 = (1.0f - cw) * 0.5f;
    float a0 = 1.0f + alpha, a1 = -2.0f * cw, a2 = 1.0f - alpha;
    q->b0 = b0 / a0;
    q->b1 = b1 / a0;
    q->b2 = b2 / a0;
    q->a1 = a1 / a0;
    q->a2 = a2 / a0;
}

/* Pink noise */
typedef struct {
    unsigned int rng;
    float        y;
} PinkNoise;
static inline float frand_u(unsigned int* s)
{
    *s = (*s) * 1664525u + 1013904223u;
    return ((float)((*s >> 8) & 0x00FFFFFF) / 16777215.0f);
}
static inline float white_signed(unsigned int* s)
{
    return frand_u(s) * 2.0f - 1.0f;
}
static inline void pink_init(PinkNoise* p, unsigned int seed)
{
    p->rng = seed ? seed : 1u;
    p->y   = 0.0f;
}
static inline float pink_tick(PinkNoise* p)
{
    float w = white_signed(&p->rng);
    p->y    = 0.997f * p->y + 0.03f * w;
    return p->y;
}

/* Ring modulator: a rotating phasor (one sin/cos per buffer instead of one per sample) */
typedef struct {
    float c, s; /* cos/sin of the current phase */
} RingMod;
static inline void ring_init(RingMod* r)
{
    r->c = 1.0f;
    r->s = 0.0f;
}
static inline void ring_mix(RingMod* r, float* x, int n, float fs, float hz, float mix)
{
    if (mix <= 0.0001f)
        return;
    const float dphi = 2.0f * (float)M_PI * hz / fs;
    const float cd = cosf(dphi), sd = sinf(dphi);
    float c = r->c, s = r->s;
    for (int i = 0; i < n; ++i) {
        x[i] = (1.0f - mix) * x[i] + mix * (x[i] * s);
        const float nc = c * cd - s * sd;
        s = s * cd + c * sd;
        c = nc;
    }
    /* renormalise so rounding never grows or shrinks the amplitude */
    const float mag = sqrtf(c * c + s * s);
    if (mag > 0.0f) {
        c /= mag;
        s /= mag;
    } else {
        c = 1.0f;
        s = 0.0f;
    }
    r->c = c;
    r->s = s;
}

/* Foldback distortion */
static inline float foldback_sample(float x, float th)
{
    float s = (x < 0.0f) ? -1.0f : 1.0f, ax = fabsf(x);
    if (th <= 0.0001f)
        return s * 0.0f;
    if (ax <= th)
        return x;
    float twoT = 2.0f * th;
    float y    = fmodf(ax - th, twoT);
    if (y > th)
        y = twoT - y;
    return s * (th - y);
}
static inline void foldback_buffer(float* x, int n, float th)
{
    for (int i = 0; i < n; ++i)
        x[i] = foldback_sample(x[i], th);
}

/* ---------------------------------------------------------------------------
   Per-talker state tables

   Open-addressing hash tables keyed by TeamSpeak client ID. Slots never move once claimed (the old
   sorted arrays were re-sorted with qsort on insert while other threads read them), so a lookup is
   always safe; inserts are serialised by a lock and publish the ID only after the state is fully
   initialised. All filter state is owned by the audio thread.

   A client leaving sets resetPending (from the TeamSpeak event thread); the audio thread then
   re-initialises the slot the next time that ID talks, so a reused client ID starts clean.
--------------------------------------------------------------------------- */
#define STATE_TABLE_SIZE 4096 /* power of two; well above the clients one server shows */
#define STATE_TABLE_MASK (STATE_TABLE_SIZE - 1)

static inline unsigned state_hash(anyID id)
{
    return ((unsigned)id * 2654435761u) & STATE_TABLE_MASK;
}

/* Radio state per talker (audio thread) */
typedef struct {
    volatile anyID id;           /* 0 = free slot */
    volatile long  resetPending;
    Biquad         lp, hp;       /* voice filters */
    Biquad         lp_n, hp_n;   /* static filters */
    PinkNoise      pink;
    RingMod        rm;
} RadioState;

static RadioState       g_radioStates[STATE_TABLE_SIZE];
static CRITICAL_SECTION g_radioLock;

static void radio_state_init(RadioState* s, anyID id)
{
    biquad_calc_lowpass(&s->lp, TS_SAMPLE_RATE, RADIO_LP_HZ, RADIO_LP_Q);
    biquad_calc_highpass(&s->hp, TS_SAMPLE_RATE, RADIO_HP_HZ, RADIO_HP_Q);
    biquad_calc_lowpass(&s->lp_n, TS_SAMPLE_RATE, RADIO_LP_HZ, RADIO_LP_Q);
    biquad_calc_highpass(&s->hp_n, TS_SAMPLE_RATE, RADIO_HP_HZ, RADIO_HP_Q);
    pink_init(&s->pink, (unsigned)id * 2654435761u);
    ring_init(&s->rm);
}

static RadioState* find_radio_slot(anyID id, int create)
{
    unsigned i = state_hash(id);
    for (int probe = 0; probe < STATE_TABLE_SIZE; ++probe, i = (i + 1) & STATE_TABLE_MASK) {
        anyID slotId = g_radioStates[i].id;
        if (slotId == id)
            return &g_radioStates[i];
        if (slotId == 0)
            break;
    }
    if (!create)
        return NULL;

    EnterCriticalSection(&g_radioLock);
    RadioState* found = NULL;
    i = state_hash(id);
    for (int probe = 0; probe < STATE_TABLE_SIZE; ++probe, i = (i + 1) & STATE_TABLE_MASK) {
        anyID slotId = g_radioStates[i].id;
        if (slotId == id) {
            found = &g_radioStates[i];
            break;
        }
        if (slotId == 0) {
            RadioState* s = &g_radioStates[i];
            memset((void*)s, 0, sizeof(*s));
            radio_state_init(s, id);
            MemoryBarrier();
            s->id = id; /* publish last */
            found = s;
            break;
        }
    }
    LeaveCriticalSection(&g_radioLock);
    return found;
}

static RadioState* get_radio_state(anyID id)
{
    RadioState* s = find_radio_slot(id, 1);
    if (!s)
        return NULL; /* table full: caller skips the effect */
    if (InterlockedExchange(&s->resetPending, 0))
        radio_state_init(s, id);
    return s;
}

/* ---------------------------------------------------------------------------
   Fast trig for the modulation effects (babble). Max error ~1e-3, inaudible in a distortion effect,
   several times cheaper than sinf/cosf per sample.
--------------------------------------------------------------------------- */
static inline float wrap_pi(float x)
{
    /* to [-pi, pi] */
    x = fmodf(x + M_PI_F, TWO_PI_F);
    if (x < 0.0f)
        x += TWO_PI_F;
    return x - M_PI_F;
}
static inline float fast_sinf(float x)
{
    x = wrap_pi(x);
    /* Bhaskara-style parabola + one correction step */
    const float B = 4.0f / M_PI_F, C = -4.0f / (M_PI_F * M_PI_F);
    float y = B * x + C * x * fabsf(x);
    return 0.225f * (y * fabsf(y) - y) + y;
}
static inline float fast_cosf(float x)
{
    return fast_sinf(x + 0.5f * M_PI_F);
}

static inline const VonSnapshot* get_active_von(void)
{
    long idx = InterlockedCompareExchange(&g_vonActiveIdx, 0, 0);
    return &g_VonSnap[(idx & 1)];
}
static inline const RadioSnapshot* get_active_radio(void)
{
    long idx = InterlockedCompareExchange(&g_radioActiveIdx, 0, 0);
    return &g_RadioSnap[(idx & 1)];
}

/* Static/hiss generator: driven by quality only; optional SNR scaling with volume */
static void radio_make_static(RadioState* st, float* out, int n, float volume01, float quality01)
{
    if (n <= 0)
        return;

    const float q = clampf(quality01, 0.0f, 1.0f);

    /* Intensity: more noise as quality drops; no dependence on volume */
    const float inv_eff = 0.90f * (1.0f - q);

    /* Spectral balance stays similar to before */
    const float pinkAmt  = 0.25f * inv_eff;
    const float whiteAmt = 0.0025f * inv_eff;
    const float bedWhite = 0.0045f * (1.0f - q);

    for (int i = 0; i < n; ++i) {
        float pn = pink_tick(&st->pink) * pinkAmt;
        float wn = white_signed(&st->pink.rng) * (whiteAmt + bedWhite);
        out[i]   = pn + wn;
    }

    for (int i = 0; i < n; ++i)
        out[i] = biquad_tick(&st->lp_n, out[i]);
    for (int i = 0; i < n; ++i)
        out[i] = biquad_tick(&st->hp_n, out[i]);

    /* Keep SNR reasonably consistent across volume steps (optional but nice) */
    const float snrComp = 0.45f + 0.55f * clampf(volume01, 0.0f, 1.0f);

    /* Final noise gain: largely quality-driven, then SNR-compensated by volume */
    const float noiseGain = (0.28f * (1.0f - q) + 0.015f) * snrComp;

    for (int i = 0; i < n; ++i) {
        float s = out[i] * noiseGain;
        if (s > 1.0f)
            s = 1.0f;
        if (s < -1.0f)
            s = -1.0f;
        out[i] = s;
    }
}

/* Voice-only Acre coloration */
/* Voice-only Acre coloration: FX driven by quality, not volume */
static void radio_color_voice_only(RadioState* st, float* buf, int n, float volume01, float quality01)
{
    if (n <= 0)
        return;

    /* Pre-FX boost like before to keep tone comparable */
    for (int i = 0; i < n; ++i)
        buf[i] *= 3.0f;

    /* Ring-mod amount grows as quality drops (0.18..0.50) */
    const float q    = clampf(quality01, 0.0f, 1.0f);
    const float rmix = clampf(0.18f + 0.32f * (1.0f - q), 0.0f, 0.6f);
    ring_mix(&st->rm, buf, n, TS_SAMPLE_RATE, RADIO_RINGMOD_HZ, rmix);

    /* Distortion threshold lowers slightly as quality drops */
    const float th = clampf(BASE_DISTORTION_LEVEL - 0.20f * (1.0f - q), 0.15f, 0.60f);
    foldback_buffer(buf, n, th);

    /* Radio bandpass as before */
    for (int i = 0; i < n; ++i)
        buf[i] = biquad_tick(&st->lp, buf[i]);
    for (int i = 0; i < n; ++i)
        buf[i] = biquad_tick(&st->hp, buf[i]);

    for (int i = 0; i < n; ++i) {
        if (buf[i] > 1.0f)
            buf[i] = 1.0f;
        if (buf[i] < -1.0f)
            buf[i] = -1.0f;
    }
}

/* ---------------------------------------------------------------------------
   DIRECT muffle state (per talker, audio thread)
--------------------------------------------------------------------------- */
typedef struct {
    volatile anyID id;           /* 0 = free slot */
    volatile long  resetPending;

    Biquad lpL[MUFFLE_STAGES];
    Biquad lpR[MUFFLE_STAGES];
    Biquad lpM[MUFFLE_STAGES];
    float  lastFc;

    // Behind effect filters
    Biquad behindLpL[2];
    Biquad behindLpR[2];
    Biquad behindLpM[2];
    float  lastBehindFc;

    // Babbel effect filters
    Biquad babbelLp1L;
    Biquad babbelLp1R;
    Biquad babbelLp1M;
    Biquad babbelLp2L;
    Biquad babbelLp2R;
    Biquad babbelLp2M;
    float  babbelPhase;

    /* Gains in effect at the end of the last buffer; each buffer ramps from these to the new target
       so a gain update never jumps mid-stream (zipper noise). */
    float  gainL, gainR;
    int    haveGain;

    /* Intercom band-pass (two stages each side for a steeper telephone band) */
    Biquad icHp[2];
    Biquad icLp[2];
} DirectState;

static DirectState      g_directStates[STATE_TABLE_SIZE];
static CRITICAL_SECTION g_directLock; /* serialises inserts; lookups are lock-free */

/* Map positive occlusion dB (e.g. 0..24) -> cutoff Hz. 0dB→8k, 24dB→1k.
   Tuned anchors: 12dB ≈ 4.5k, 18dB ≈ 2.7k. */
static inline float occlusion_db_to_fc(float occDbPos)
{
    float       t     = clampf(occDbPos / 24.0f, 0.0f, 1.0f);
    const float fc_hi = 8000.0f;
    const float fc_lo = 1000.0f;
    return fc_hi + (fc_lo - fc_hi) * t; /* linear lerp */
}

/* Map behind intensity (0..1) to cutoff Hz for the "behind head" effect: a voice behind you loses some
   brightness, as the outer ear shades it. 0 = in front (16 kHz, effectively transparent), 1 = directly
   behind (4.5 kHz). The filter always runs, so moving around someone never switches it in or out. */
#define BEHIND_FC_FRONT 16000.0f
static inline float behind_intensity_to_fc(float intensity)
{
    float       t     = clampf(intensity, 0.0f, 1.0f);
    const float fc_hi = BEHIND_FC_FRONT;
    const float fc_lo = 4500.0f;
    return fc_hi + (fc_lo - fc_hi) * t; /* linear lerp */
}

static void direct_state_init(DirectState* s)
{
    for (int i = 0; i < MUFFLE_STAGES; ++i) {
        biquad_calc_lowpass(&s->lpL[i], TS_SAMPLE_RATE, 8000.0f, 0.9f);
        biquad_calc_lowpass(&s->lpR[i], TS_SAMPLE_RATE, 8000.0f, 0.9f);
        biquad_calc_lowpass(&s->lpM[i], TS_SAMPLE_RATE, 8000.0f, 0.9f);
    }
    s->lastFc = 8000.0f;
    for (int i = 0; i < 2; ++i) {
        biquad_calc_lowpass(&s->behindLpL[i], TS_SAMPLE_RATE, BEHIND_FC_FRONT, 0.7f);
        biquad_calc_lowpass(&s->behindLpR[i], TS_SAMPLE_RATE, BEHIND_FC_FRONT, 0.7f);
        biquad_calc_lowpass(&s->behindLpM[i], TS_SAMPLE_RATE, BEHIND_FC_FRONT, 0.7f);
    }
    s->lastBehindFc = BEHIND_FC_FRONT;
    biquad_calc_lowpass(&s->babbelLp1L, TS_SAMPLE_RATE, BABBEL_LP1_FREQ, BABBEL_LP_Q);
    biquad_calc_lowpass(&s->babbelLp1R, TS_SAMPLE_RATE, BABBEL_LP1_FREQ, BABBEL_LP_Q);
    biquad_calc_lowpass(&s->babbelLp1M, TS_SAMPLE_RATE, BABBEL_LP1_FREQ, BABBEL_LP_Q);
    biquad_calc_lowpass(&s->babbelLp2L, TS_SAMPLE_RATE, BABBEL_LP2_FREQ, BABBEL_LP_Q);
    biquad_calc_lowpass(&s->babbelLp2R, TS_SAMPLE_RATE, BABBEL_LP2_FREQ, BABBEL_LP_Q);
    biquad_calc_lowpass(&s->babbelLp2M, TS_SAMPLE_RATE, BABBEL_LP2_FREQ, BABBEL_LP_Q);
    s->babbelPhase = 0.0f;
    s->gainL = s->gainR = 0.0f;
    s->haveGain = 0;
    for (int i = 0; i < 2; ++i) {
        biquad_calc_highpass(&s->icHp[i], TS_SAMPLE_RATE, INTERCOM_HP_HZ, 0.707f);
        biquad_calc_lowpass(&s->icLp[i], TS_SAMPLE_RATE, INTERCOM_LP_HZ, 0.707f);
    }
}

static DirectState* find_direct_slot(anyID id, int create)
{
    unsigned i = state_hash(id);
    for (int probe = 0; probe < STATE_TABLE_SIZE; ++probe, i = (i + 1) & STATE_TABLE_MASK) {
        anyID slotId = g_directStates[i].id;
        if (slotId == id)
            return &g_directStates[i];
        if (slotId == 0)
            break;
    }
    if (!create)
        return NULL;

    EnterCriticalSection(&g_directLock);
    DirectState* found = NULL;
    i = state_hash(id);
    for (int probe = 0; probe < STATE_TABLE_SIZE; ++probe, i = (i + 1) & STATE_TABLE_MASK) {
        anyID slotId = g_directStates[i].id;
        if (slotId == id) {
            found = &g_directStates[i];
            break;
        }
        if (slotId == 0) {
            DirectState* s = &g_directStates[i];
            memset((void*)s, 0, sizeof(*s));
            direct_state_init(s);
            MemoryBarrier();
            s->id = id; /* publish last */
            found = s;
            break;
        }
    }
    LeaveCriticalSection(&g_directLock);
    return found;
}

static DirectState* get_direct_state(anyID id)
{
    DirectState* s = find_direct_slot(id, 1);
    if (!s)
        return NULL; /* table full: caller leaves the audio untouched */
    if (InterlockedExchange(&s->resetPending, 0))
        direct_state_init(s);
    return s;
}

/* Client left: its ID may be reused by someone else, so start their effect state from scratch */
static void reset_client_states(anyID id)
{
    DirectState* d = find_direct_slot(id, 0);
    if (d)
        InterlockedExchange(&d->resetPending, 1);
    RadioState* r = find_radio_slot(id, 0);
    if (r)
        InterlockedExchange(&r->resetPending, 1);
}

/* Apply Babbel effect (foreign language distortion) - IMPROVED for maximum unintelligibility
   Multiple techniques combined:
   - Variable ring modulation (pitch scrambling with depth control)
   - Narrower bandpass (removes speech clarity)
   - Chaotic pitch variation (random warble)
   - Aggressive clipping (adds grit without sounding "robotic") */
static inline void apply_babbel_effect(DirectState* ds, float* samples, int count, int isMono)
{
    const float modFreq  = BABBEL_MOD_FREQ;
    const float phaseInc = TWO_PI_F * modFreq / TS_SAMPLE_RATE;
    const float modDepth = BABBEL_MOD_DEPTH;
    const float chaos    = BABBEL_CHAOS_AMOUNT;

    if (isMono) {
        /* Stage 1: Variable ring modulation with chaos - destroys pitch while keeping organic sound */
        for (int i = 0; i < count; ++i) {
            /* Add slight random variation to modulation frequency (wobble effect) */
            float chaosVar = 1.0f + (chaos * 0.5f * fast_sinf(ds->babbelPhase * 0.07f));
            float mod      = fast_cosf(ds->babbelPhase * 1.4f * chaosVar);

            /* Mix original with modulated - depth control prevents "metallic robot" sound */
            samples[i] = samples[i] * ((1.0f - modDepth) + modDepth * mod);

            ds->babbelPhase += phaseInc;
            if (ds->babbelPhase > TWO_PI_F)
                ds->babbelPhase -= TWO_PI_F;
        }

        /* Stage 2: Aggressive first lowpass - removes speech clarity */
        for (int i = 0; i < count; ++i)
            samples[i] = biquad_tick(&ds->babbelLp1M, samples[i]);

        /* Stage 3: Second lowpass - further muddles formants */
        for (int i = 0; i < count; ++i)
            samples[i] = biquad_tick(&ds->babbelLp2M, samples[i]);

        /* Stage 4: Soft clipping with asymmetry - adds grit organically */
        for (int i = 0; i < count; ++i) {
            float boosted = samples[i] * BABBEL_VOLUME_BOOST;

            /* Soft clipping curve - smoother than hard clip, less robotic */
            if (boosted > 0.7f)
                boosted = 0.7f + 0.3f * tanhf((boosted - 0.7f) * 3.0f);
            else if (boosted < -0.7f)
                boosted = -0.7f + 0.3f * tanhf((boosted + 0.7f) * 3.0f);

            /* Add slight asymmetric distortion (mimics vocal tract non-linearity) */
            boosted = boosted * (1.0f + 0.08f * boosted);

            /* Final hard limit */
            if (boosted > 1.0f)
                boosted = 1.0f;
            else if (boosted < -1.0f)
                boosted = -1.0f;

            samples[i] = boosted;
        }

    } else {
        /* Stereo: Same aggressive processing */

        /* Stage 1: Variable ring modulation with chaos */
        for (int i = 0; i < count; i += 2) {
            float chaosVar = 1.0f + (chaos * 0.5f * fast_sinf(ds->babbelPhase * 0.07f));
            float mod      = fast_cosf(ds->babbelPhase * 1.4f * chaosVar);

            samples[i + 0] = samples[i + 0] * ((1.0f - modDepth) + modDepth * mod);
            samples[i + 1] = samples[i + 1] * ((1.0f - modDepth) + modDepth * mod);

            ds->babbelPhase += phaseInc;
            if (ds->babbelPhase > TWO_PI_F)
                ds->babbelPhase -= TWO_PI_F;
        }

        /* Stage 2: First lowpass */
        for (int i = 0; i < count; i += 2) {
            samples[i + 0] = biquad_tick(&ds->babbelLp1L, samples[i + 0]);
            samples[i + 1] = biquad_tick(&ds->babbelLp1R, samples[i + 1]);
        }

        /* Stage 3: Second lowpass */
        for (int i = 0; i < count; i += 2) {
            samples[i + 0] = biquad_tick(&ds->babbelLp2L, samples[i + 0]);
            samples[i + 1] = biquad_tick(&ds->babbelLp2R, samples[i + 1]);
        }

        /* Stage 4: Soft clipping with asymmetry */
        for (int i = 0; i < count; i += 2) {
            float L = samples[i + 0] * BABBEL_VOLUME_BOOST;
            float R = samples[i + 1] * BABBEL_VOLUME_BOOST;

            /* Soft clipping curve */
            if (L > 0.7f)
                L = 0.7f + 0.3f * tanhf((L - 0.7f) * 3.0f);
            else if (L < -0.7f)
                L = -0.7f + 0.3f * tanhf((L + 0.7f) * 3.0f);

            if (R > 0.7f)
                R = 0.7f + 0.3f * tanhf((R - 0.7f) * 3.0f);
            else if (R < -0.7f)
                R = -0.7f + 0.3f * tanhf((R + 0.7f) * 3.0f);

            /* Asymmetric distortion */
            L = L * (1.0f + 0.08f * L);
            R = R * (1.0f + 0.08f * R);

            /* Final hard limit */
            if (L > 1.0f)
                L = 1.0f;
            else if (L < -1.0f)
                L = -1.0f;

            if (R > 1.0f)
                R = 1.0f;
            else if (R < -1.0f)
                R = -1.0f;

            samples[i + 0] = L;
            samples[i + 1] = R;
        }
    }
}

/* Soft limiter: untouched up to 0.8 of full scale, then eases toward 1.0 instead of clipping. The +5 dB
   make-up gain and the louder voice tiers can push close talkers past full scale. */
static inline float soft_limit(float x)
{
    const float knee = 0.8f;
    const float a    = fabsf(x);
    if (a <= knee)
        return x;
    const float y = knee + (1.0f - knee) * tanhf((a - knee) / (1.0f - knee));
    return x < 0.0f ? -y : y;
}

static inline int samples_all_zero(const short* samples, int total)
{
    for (int i = 0; i < total; ++i)
        if (samples[i])
            return 0;
    return 1;
}

/* Shared DIRECT-path DSP: gain, muffle LP, behind-head LP, babbel distortion.
   Called from onEditPostProcessVoiceDataEvent for both plain DIRECT talkers and
   radio talkers whose frequency does not match the local player's radio.
   All filter state is touched only here, on the audio thread. */
static void apply_direct_dsp(anyID clientID, const VonEntry* e, short* samples, int sampleCount, int channels)
{
    DirectState* ds = get_direct_state(clientID);
    if (!ds)
        return;

    const float occDb = (e->muffledDb < 0.0f) ? -e->muffledDb : 0.0f;
    const int   doMuf = (occDb > 0.1f) ? 1 : 0;

    const float behindInt = clampf(e->behindIntensity, 0.0f, 1.0f);

    /* Behind-the-head filter on; no extra level drop here, the game's rear shadow already lowers it */
    const int   doBehind    = 1;
    const float behindAtten = 1.0f;

    /* Filter cutoffs follow the game's occlusion. Coefficients are recalculated here rather than on
       the worker thread (which raced the audio thread); filter memory is kept so the change is smooth. */
    if (doMuf) {
        const float fc = occlusion_db_to_fc(occDb);
        if (fabsf(fc - ds->lastFc) > 10.0f) {
            for (int k = 0; k < MUFFLE_STAGES; ++k) {
                biquad_update_lowpass(&ds->lpL[k], TS_SAMPLE_RATE, fc, 0.9f);
                biquad_update_lowpass(&ds->lpR[k], TS_SAMPLE_RATE, fc, 0.9f);
                biquad_update_lowpass(&ds->lpM[k], TS_SAMPLE_RATE, fc, 0.9f);
            }
            ds->lastFc = fc;
        }
    }
    if (doBehind) {
        /* Always tracked, so the cutoff returns to transparent when the talker comes back in front */
        const float behindFc = behind_intensity_to_fc(behindInt);
        if (fabsf(behindFc - ds->lastBehindFc) > 10.0f) {
            for (int k = 0; k < 2; ++k) {
                biquad_update_lowpass(&ds->behindLpL[k], TS_SAMPLE_RATE, behindFc, 0.7f);
                biquad_update_lowpass(&ds->behindLpR[k], TS_SAMPLE_RATE, behindFc, 0.7f);
                biquad_update_lowpass(&ds->behindLpM[k], TS_SAMPLE_RATE, behindFc, 0.7f);
            }
            ds->lastBehindFc = behindFc;
        }
    }

    /* Clamp gains so bad data cannot explode the signal */
    const float targetL = clampf(e->leftGain, 0.0f, 4.0f);
    const float targetR = clampf(e->rightGain, 0.0f, 4.0f);
    if (!ds->haveGain) {
        ds->gainL    = targetL;
        ds->gainR    = targetR;
        ds->haveGain = 1;
    }
    const float startL = ds->gainL, startR = ds->gainR;
    const float stepL  = (targetL - startL) / (float)sampleCount;
    const float stepR  = (targetR - startR) / (float)sampleCount;
    ds->gainL = targetL;
    ds->gainR = targetR;

    /* Nothing to shape in a silent buffer (TeamSpeak sends some around talk start/stop) */
    if (samples_all_zero(samples, sampleCount * channels))
        return;

    if (channels <= 1) {
        /* Mono input cannot preserve true L/R separation.
           Use average gain so volume still tracks distance. */
        if (!e->sameLanguage) {
            float* floatBuf = (float*)alloca(sampleCount * sizeof(float));
            for (int i = 0; i < sampleCount; ++i) {
                const float monoGain = 0.5f * ((startL + stepL * (i + 1)) + (startR + stepR * (i + 1)));
                floatBuf[i] = ((float)samples[i] / 32768.0f) * monoGain;
            }

            apply_babbel_effect(ds, floatBuf, sampleCount, 1);

            for (int i = 0; i < sampleCount; ++i) {
                float x = floatBuf[i];

                if (doMuf)
                    for (int s = 0; s < MUFFLE_STAGES; ++s)
                        x = biquad_tick(&ds->lpM[s], x);

                if (doBehind) {
                    for (int s = 0; s < 2; ++s)
                        x = biquad_tick(&ds->behindLpM[s], x);
                    x *= behindAtten;
                }

                x *= DIRECT_GAIN_MUL;
                samples[i] = clamp_i16(soft_limit(x) * 32767.0f);
            }
        } else {
            for (int i = 0; i < sampleCount; ++i) {
                const float monoGain = 0.5f * ((startL + stepL * (i + 1)) + (startR + stepR * (i + 1)));
                float x = ((float)samples[i] / 32768.0f) * monoGain;

                if (doMuf)
                    for (int s = 0; s < MUFFLE_STAGES; ++s)
                        x = biquad_tick(&ds->lpM[s], x);

                if (doBehind) {
                    for (int s = 0; s < 2; ++s)
                        x = biquad_tick(&ds->behindLpM[s], x);
                    x *= behindAtten;
                }

                x *= DIRECT_GAIN_MUL;
                samples[i] = clamp_i16(soft_limit(x) * 32767.0f);
            }
        }
    } else {
        /* Stereo: build new positional stereo from the source using LeftGain/RightGain. */
        if (!e->sameLanguage) {
            float* floatBuf = (float*)alloca(sampleCount * 2 * sizeof(float));

            for (int i = 0; i < sampleCount; ++i) {
                const int idx = i * channels;

                /* Collapse incoming to mono source, then re-pan with the (ramped) gains */
                const float inL  = (float)samples[idx + 0] / 32768.0f;
                const float inR  = (float)samples[idx + 1] / 32768.0f;
                const float mono = 0.5f * (inL + inR);

                floatBuf[i * 2 + 0] = mono * (startL + stepL * (i + 1));
                floatBuf[i * 2 + 1] = mono * (startR + stepR * (i + 1));
            }

            apply_babbel_effect(ds, floatBuf, sampleCount * 2, 0);

            for (int i = 0; i < sampleCount; ++i) {
                float L = floatBuf[i * 2 + 0];
                float R = floatBuf[i * 2 + 1];

                if (doMuf) {
                    for (int s = 0; s < MUFFLE_STAGES; ++s) {
                        L = biquad_tick(&ds->lpL[s], L);
                        R = biquad_tick(&ds->lpR[s], R);
                    }
                }

                if (doBehind) {
                    for (int s = 0; s < 2; ++s) {
                        L = biquad_tick(&ds->behindLpL[s], L);
                        R = biquad_tick(&ds->behindLpR[s], R);
                    }
                    L *= behindAtten;
                    R *= behindAtten;
                }

                L *= DIRECT_GAIN_MUL;
                R *= DIRECT_GAIN_MUL;

                const int idx    = i * channels;
                samples[idx + 0] = clamp_i16(soft_limit(L) * 32767.0f);
                samples[idx + 1] = clamp_i16(soft_limit(R) * 32767.0f);

                const short monoFill = (short)((samples[idx + 0] + samples[idx + 1]) / 2);
                for (int ch = 2; ch < channels; ++ch)
                    samples[idx + ch] = monoFill;
            }
        } else {
            for (int i = 0; i < sampleCount; ++i) {
                const int idx = i * channels;

                /* Collapse to mono source, then re-pan using the (ramped) gains */
                const float inL  = (float)samples[idx + 0] / 32768.0f;
                const float inR  = (float)samples[idx + 1] / 32768.0f;
                const float mono = 0.5f * (inL + inR);

                float L = mono * (startL + stepL * (i + 1));
                float R = mono * (startR + stepR * (i + 1));

                if (doMuf) {
                    for (int s = 0; s < MUFFLE_STAGES; ++s) {
                        L = biquad_tick(&ds->lpL[s], L);
                        R = biquad_tick(&ds->lpR[s], R);
                    }
                }

                if (doBehind) {
                    for (int s = 0; s < 2; ++s) {
                        L = biquad_tick(&ds->behindLpL[s], L);
                        R = biquad_tick(&ds->behindLpR[s], R);
                    }
                    L *= behindAtten;
                    R *= behindAtten;
                }

                L *= DIRECT_GAIN_MUL;
                R *= DIRECT_GAIN_MUL;

                samples[idx + 0] = clamp_i16(soft_limit(L) * 32767.0f);
                samples[idx + 1] = clamp_i16(soft_limit(R) * 32767.0f);

                const short monoFill = (short)((samples[idx + 0] + samples[idx + 1]) / 2);
                for (int ch = 2; ch < channels; ++ch)
                    samples[idx + ch] = monoFill;
            }
        }
    }
}

/* ---------------------------------------------------------------------------
   Vehicle intercom and speaker
--------------------------------------------------------------------------- */
/* Intercom: mono, telephone band, light saturation, the same in both ears */
static void apply_intercom_dsp(anyID clientID, const VonEntry* e, short* samples, int sampleCount, int channels)
{
    DirectState* ds = get_direct_state(clientID);
    if (!ds)
        return;

    /* One gain for both ears, ramped like the direct path so a change never clicks */
    const float target = clampf(0.5f * (e->leftGain + e->rightGain), 0.0f, 4.0f);
    if (!ds->haveGain) {
        ds->gainL = ds->gainR = target;
        ds->haveGain = 1;
    }
    const float start = 0.5f * (ds->gainL + ds->gainR);
    const float step  = (target - start) / (float)sampleCount;
    ds->gainL = ds->gainR = target;

    if (samples_all_zero(samples, sampleCount * channels))
        return;

    float* buf = (float*)alloca(sampleCount * sizeof(float));
    for (int i = 0; i < sampleCount; ++i) {
        float mono;
        if (channels <= 1) {
            mono = (float)samples[i] / 32768.0f;
        } else {
            const int idx = i * channels;
            mono = 0.5f * ((float)samples[idx + 0] + (float)samples[idx + 1]) / 32768.0f;
        }
        buf[i] = mono * (start + step * (i + 1));
    }

    if (!e->sameLanguage)
        apply_babbel_effect(ds, buf, sampleCount, 1);

    const float driveNorm = 1.0f / INTERCOM_DRIVE;
    for (int i = 0; i < sampleCount; ++i) {
        float x = buf[i];
        for (int k = 0; k < 2; ++k)
            x = biquad_tick(&ds->icHp[k], x);
        for (int k = 0; k < 2; ++k)
            x = biquad_tick(&ds->icLp[k], x);
        x = tanhf(x * INTERCOM_DRIVE) * driveNorm;
        x *= INTERCOM_MAKEUP;
        const short out = clamp_i16(soft_limit(x) * 32767.0f);

        if (channels <= 1) {
            samples[i] = out;
        } else {
            const int idx = i * channels;
            for (int ch = 0; ch < channels; ++ch)
                samples[idx + ch] = out;
        }
    }
}

/* Speaker: colour the voice like a radio (static and distortion follow the signal quality), then let
   the direct path place it at the vehicle */
static void apply_speaker_dsp(anyID clientID, const VonEntry* e, short* samples, int sampleCount, int channels)
{
    if (samples_all_zero(samples, sampleCount * channels))
        return;

    RadioState* st = get_radio_state(clientID);
    if (st) {
        const float q = clampf(e->connQ, 0.0f, 1.0f);
        int processed = 0;
        while (processed < sampleCount) {
            int n = sampleCount - processed;
            if (n > RADIO_PROCESS_CHUNK)
                n = RADIO_PROCESS_CHUNK;

            float voice[RADIO_PROCESS_CHUNK];
            float hiss[RADIO_PROCESS_CHUNK];
            for (int i = 0; i < n; ++i) {
                if (channels <= 1) {
                    voice[i] = (float)samples[processed + i] / 32768.0f;
                } else {
                    const int idx = (processed + i) * channels;
                    voice[i] = 0.5f * ((float)samples[idx + 0] + (float)samples[idx + 1]) / 32768.0f;
                }
            }

            radio_color_voice_only(st, voice, n, 1.0f, q);
            radio_make_static(st, hiss, n, q, q);

            for (int i = 0; i < n; ++i) {
                const float s     = clampf(voice[i] + hiss[i], -1.0f, 1.0f) * SPEAKER_PRE_GAIN;
                const short out   = clamp_i16(s * 32767.0f);
                if (channels <= 1) {
                    samples[processed + i] = out;
                } else {
                    const int idx = (processed + i) * channels;
                    for (int ch = 0; ch < channels; ++ch)
                        samples[idx + ch] = out;
                }
            }
            processed += n;
        }
    }

    apply_direct_dsp(clientID, e, samples, sampleCount, channels);
}

/* ---------------------------------------------------------------------------
   JSON loaders (using reusable buffers)
--------------------------------------------------------------------------- */
static int cmp_von_entry_by_id(const void* a, const void* b)
{
    return (int)((const VonEntry*)a)->id - (int)((const VonEntry*)b)->id;
}

static int load_von_into(VonSnapshot* dst)
{
    if (!g_VonPath[0])
        return 0;

    long sz = 0;
    if (!read_file_reuse(g_VonPath, &g_vonBuf, &g_vonCap, &sz))
        return 0;

    cJSON* root = cJSON_Parse(g_vonBuf);
    if (!root)
        return 0;

    size_t count = 0;

    cJSON* tx = cJSON_GetObjectItem(root, "IsTransmitting");
    if (cJSON_IsBool(tx))
        g_IsTransmitting = tx->valueint;

    cJSON* it;
    cJSON_ArrayForEach(it, root)
    {
        if (!it->string || strcmp(it->string, "IsTransmitting") == 0)
            continue;
        if (count >= MAX_TRACKED)
            break;

        VonEntry e;
        memset(&e, 0, sizeof(e));
        e.id        = (anyID)atoi(it->string);
        e.type      = VON_DIRECT;
        e.connQ     = 1.0f;
        e.txTimeDev = INT_MIN;

        cJSON* v;
        v = cJSON_GetObjectItem(it, "VONType");
        if (cJSON_IsNumber(v))
            e.type = von_type_from_int(v->valueint);
        v = cJSON_GetObjectItem(it, "LeftGain");
        if (cJSON_IsNumber(v))
            e.leftGain = (float)v->valuedouble;
        v = cJSON_GetObjectItem(it, "RightGain");
        if (cJSON_IsNumber(v))
            e.rightGain = (float)v->valuedouble;
        v = cJSON_GetObjectItem(it, "Frequency");
        if (cJSON_IsString(v)) {
            strncpy(e.txFreq, v->valuestring, sizeof(e.txFreq) - 1);
            trim_inplace(e.txFreq);
        }
        v = cJSON_GetObjectItem(it, "TimeDeviation");
        if (cJSON_IsNumber(v))
            e.txTimeDev = v->valueint;
        v = cJSON_GetObjectItem(it, "ConnectionQuality");
        if (cJSON_IsNumber(v))
            e.connQ = clampf((float)v->valuedouble, 0.0f, 1.0f);
        v = cJSON_GetObjectItem(it, "FactionKey");
        if (cJSON_IsString(v)) {
            strncpy(e.txFaction, v->valuestring, sizeof(e.txFaction) - 1);
            trim_inplace(e.txFaction);
        }
        v = cJSON_GetObjectItem(it, "MuffledDecibels");
        if (cJSON_IsNumber(v))
            e.muffledDb = (float)v->valuedouble;

        v = cJSON_GetObjectItem(it, "BehindIntensity");
        if (cJSON_IsNumber(v))
            e.behindIntensity = clampf((float)v->valuedouble, 0.0f, 1.0f);

        v = cJSON_GetObjectItem(it, "SameLanguage");
        if (cJSON_IsBool(v))
            e.sameLanguage = v->valueint;
        else
            e.sameLanguage = 1; /* default to same language if not specified */

        e.lastUpdateTick = GetTickCount();

        dst->entries[count++] = e;
    }

    cJSON_Delete(root);

    dst->count     = count;
    dst->loaded    = 1;
    dst->buildTick = GetTickCount();

    /* Sort by id so find_entry can use binary search instead of O(n) scan. */
    if (count > 1)
        qsort(dst->entries, count, sizeof(VonEntry), cmp_von_entry_by_id);

    return 1;
}

/* ---------------------------------------------------------------------------
   VONData.bin (format 1) - written by the game instead of VONData.json once this plugin has said it
   reads it (VONPluginInfo.json). All values little-endian, packed in 4-byte words:

     Header   w0 magic 'CVON' (0x4E4F5643)
              w1 version u16 | flags u16 (bit0 = IsTransmitting)
              w2 seq u32
              w3 entryCount u16 | entryWords u16
              w4 stringCount u16 | reserved u16
     Entry    e0 tsClientId u16 | VONType u8 | flags u8 (bit0 = SameLanguage)
              e1 leftGain u16 (x10000) | rightGain u16 (x10000)
              e2 frequency string index u16 | faction string index u16 (0xFFFF = none)
              e3 timeDeviation i32 (0x7FFFFFFF = not sent)
              e4 muffledDb i8 | connectionQuality u8 (x255) | behindIntensity u8 (x255) | 0
     Strings  per string: length (word), then that many bytes
     Trailer  seq u32 (equals the header's; anything else is a half-written file)
--------------------------------------------------------------------------- */
#define VONBIN_MAGIC 0x4E4F5643u
#define VONBIN_VERSION 1
#define VONBIN_HEADER_WORDS 5
#define VONBIN_MIN_ENTRY_WORDS 5
#define VONBIN_NO_STRING 0xFFFFu
#define VONBIN_NO_TIME_DEVIATION 0x7FFFFFFFu /* the JSON format never sent one either */
#define VONBIN_MAX_STRINGS 512

static char*  g_vonBinBuf = NULL;
static size_t g_vonBinCap = 0;

static inline uint32_t rd_u32(const unsigned char* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static char g_VonBinPath[PATH_BUFSIZE] = {0};

/* Returns 1 on success, 0 if the file is missing/invalid, -1 if it looks half-written (retry soon) */
static int load_von_bin_into(VonSnapshot* dst)
{
    if (!g_VonBinPath[0])
        return 0;

    long sz = 0;
    if (!read_file_reuse(g_VonBinPath, &g_vonBinBuf, &g_vonBinCap, &sz))
        return 0;

    const unsigned char* buf = (const unsigned char*)g_vonBinBuf;
    const size_t         len = (size_t)sz;
    if (len < (VONBIN_HEADER_WORDS + 1) * 4 || rd_u32(buf) != VONBIN_MAGIC)
        return 0;

    const uint32_t w1 = rd_u32(buf + 4);
    if ((w1 & 0xFFFF) != VONBIN_VERSION)
        return 0;
    const int      transmitting = (w1 >> 16) & 1;
    const uint32_t seq          = rd_u32(buf + 8);
    const uint32_t w3           = rd_u32(buf + 12);
    const size_t   entryCount   = w3 & 0xFFFF;
    const size_t   entryWords   = (w3 >> 16) & 0xFFFF;
    const size_t   stringCount  = rd_u32(buf + 16) & 0xFFFF;
    if (entryWords < VONBIN_MIN_ENTRY_WORDS || entryCount > MAX_TRACKED || stringCount > VONBIN_MAX_STRINGS)
        return -1;

    size_t pos = VONBIN_HEADER_WORDS * 4;
    const size_t entriesAt = pos;
    pos += entryCount * entryWords * 4;
    if (pos > len)
        return -1;

    /* String table: remember where each string starts */
    static uint32_t strOff[VONBIN_MAX_STRINGS], strLen[VONBIN_MAX_STRINGS];
    for (size_t s = 0; s < stringCount; ++s) {
        if (pos + 4 > len)
            return -1;
        uint32_t n = rd_u32(buf + pos);
        pos += 4;
        if (n > 4096 || pos + n > len)
            return -1;
        strOff[s] = (uint32_t)pos;
        strLen[s] = n;
        pos += n;
    }
    if (pos + 4 > len || rd_u32(buf + pos) != seq)
        return -1; /* trailer doesn't match: the game was mid-write */

    size_t count = 0;
    const unsigned long now = GetTickCount();
    for (size_t i = 0; i < entryCount; ++i) {
        const unsigned char* e = buf + entriesAt + i * entryWords * 4;
        const uint32_t e0 = rd_u32(e), e1 = rd_u32(e + 4), e2 = rd_u32(e + 8), e4 = rd_u32(e + 16);

        VonEntry v;
        memset(&v, 0, sizeof(v));
        v.id = (anyID)(e0 & 0xFFFF);
        if (!v.id)
            continue;
        v.type         = von_type_from_int((int)((e0 >> 16) & 0xFF));
        v.sameLanguage = (e0 >> 24) & 1;
        v.leftGain     = (float)(e1 & 0xFFFF) / 10000.0f;
        v.rightGain    = (float)((e1 >> 16) & 0xFFFF) / 10000.0f;
        const uint32_t td = rd_u32(e + 12);
        v.txTimeDev    = (td == VONBIN_NO_TIME_DEVIATION) ? INT_MIN : (int)td; /* INT_MIN = don't compare */
        v.muffledDb    = (float)(signed char)(e4 & 0xFF);
        v.connQ        = (float)((e4 >> 8) & 0xFF) / 255.0f;
        v.behindIntensity = (float)((e4 >> 16) & 0xFF) / 255.0f;

        const uint32_t freqIdx = e2 & 0xFFFF, facIdx = (e2 >> 16) & 0xFFFF;
        if (freqIdx != VONBIN_NO_STRING && freqIdx < stringCount) {
            size_t n = strLen[freqIdx] < sizeof(v.txFreq) - 1 ? strLen[freqIdx] : sizeof(v.txFreq) - 1;
            memcpy(v.txFreq, buf + strOff[freqIdx], n);
            v.txFreq[n] = '\0';
        }
        if (facIdx != VONBIN_NO_STRING && facIdx < stringCount) {
            size_t n = strLen[facIdx] < sizeof(v.txFaction) - 1 ? strLen[facIdx] : sizeof(v.txFaction) - 1;
            memcpy(v.txFaction, buf + strOff[facIdx], n);
            v.txFaction[n] = '\0';
        }
        v.lastUpdateTick = now;
        dst->entries[count++] = v;
    }

    g_IsTransmitting = transmitting;
    dst->count     = count;
    dst->loaded    = 1;
    dst->buildTick = now;
    if (count > 1)
        qsort(dst->entries, count, sizeof(VonEntry), cmp_von_entry_by_id);
    return 1;
}

static int load_radio_into(RadioSnapshot* dst)
{
    if (!g_RadioPath[0])
        return 0;

    long sz = 0;
    if (!read_file_reuse(g_RadioPath, &g_radioBuf, &g_radioCap, &sz))
        return 0;

    cJSON* root = cJSON_Parse(g_radioBuf);
    if (!root)
        return 0;

    size_t count = 0;
    cJSON* it;
    cJSON_ArrayForEach(it, root)
    {
        if (count >= (sizeof(dst->list) / sizeof(dst->list[0])))
            break;

        RadioLocal r;
        memset(&r, 0, sizeof(r));
        r.timeDev = INT_MIN;

        cJSON* v;
        v = cJSON_GetObjectItem(it, "Freq");
        if (cJSON_IsString(v)) {
            strncpy(r.freq, v->valuestring, sizeof(r.freq) - 1);
            trim_inplace(r.freq);
        }
        v = cJSON_GetObjectItem(it, "TimeDeviation");
        if (cJSON_IsNumber(v))
            r.timeDev = v->valueint;
        v = cJSON_GetObjectItem(it, "Volume");
        if (cJSON_IsNumber(v))
            r.volStep = v->valueint;
        v = cJSON_GetObjectItem(it, "Stereo");
        if (cJSON_IsNumber(v))
            r.stereo = v->valueint;
        v = cJSON_GetObjectItem(it, "FactionKey");
        if (cJSON_IsString(v)) {
            strncpy(r.faction, v->valuestring, sizeof(r.faction) - 1);
            trim_inplace(r.faction);
        }

        if (r.volStep < 0)
            r.volStep = 0;
        if (r.volStep > 9)
            r.volStep = 9;
        if (r.stereo < 0 || r.stereo > 2)
            r.stereo = 0;

        if (r.freq[0])
            dst->list[count++] = r;
    }

    cJSON_Delete(root);

    dst->count     = count;
    dst->loaded    = 1;
    dst->buildTick = GetTickCount();
    return 1;
}

/* ---------------------------------------------------------------------------
   ServerData read/compare/write (write only on change + suppress watcher)
--------------------------------------------------------------------------- */
static void update_von_active_flag(uint64 sch)
{
    int active = 0;
    if (g_SD_have && g_SD_inGame && g_SD_chanName[0] && sch) {
        anyID  me      = 0;
        uint64 myCh    = 0;
        char*  curName = NULL;
        if (ts3Functions.getClientID(sch, &me) == ERROR_ok && me != 0 && ts3Functions.getChannelOfClient(sch, me, &myCh) == ERROR_ok && myCh && ts3Functions.getChannelVariableAsString(sch, myCh, CHANNEL_NAME, &curName) == ERROR_ok && curName) {
            active = (strcmp(curName, g_SD_chanName) == 0 || (_stricmp ? _stricmp(curName, g_SD_chanName) == 0 : 0));
            ts3Functions.freeMemory(curName);
        }
    }
    InterlockedExchange(&g_inVonActiveFlag, active ? 1 : 0);
}

/* Did we activate the mic (0/1)? */
static int g_pluginActivatedMic = 0;

/* allowed = in game with a live heartbeat. IsTransmitting alone isn't trusted: after a crash it can be
   stuck at true in VONData.json. */
static void apply_mic_state(uint64 sch, int allowed)
{
    if (!sch)
        return;

    int wantActive = (g_IsTransmitting && allowed) ? 1 : 0;

    // Query current mic state from TS3
    int currentDeact = -1;
    if (ts3Functions.getClientSelfVariableAsInt(sch, CLIENT_INPUT_DEACTIVATED, &currentDeact) != ERROR_ok)
        return;

    if (wantActive) {
        // If plugin wants mic ON but TS has it OFF, turn it on
        if (currentDeact == INPUT_DEACTIVATED) {
            ts3Functions.setClientSelfVariableAsInt(sch, CLIENT_INPUT_DEACTIVATED, INPUT_ACTIVE);
            ts3Functions.flushClientSelfUpdates(sch, NULL);
            g_pluginActivatedMic = 1; // remember WE forced it on
        }
    } else {
        // Only deactivate if WE were the ones who activated it
        if (g_pluginActivatedMic) {
            ts3Functions.setClientSelfVariableAsInt(sch, CLIENT_INPUT_DEACTIVATED, INPUT_DEACTIVATED);
            ts3Functions.flushClientSelfUpdates(sch, NULL);
            g_pluginActivatedMic = 0;
        }
    }
}




/* Reset mic state at init/shutdown so user is not left muted */
static void reset_mic_state(uint64 sch)
{
    if (!sch)
        return;

    // Clear our tracking
    g_LastMicActive = -1;

    // Force TS to mark mic as not deactivated
    ts3Functions.setClientSelfVariableAsInt(sch, CLIENT_INPUT_DEACTIVATED, 0);
    ts3Functions.flushClientSelfUpdates(sch, NULL);
}


static void read_serverdata_from_disk(void)
{
    if (!g_ServerPath[0])
        return;
    if (GetTickCount() < g_serverWatchSuppressUntil)
        return; /* ignore own recent writes */

    long sz = 0;
    if (!read_file_reuse(g_ServerPath, &g_srvBuf, &g_srvCap, &sz))
        return;

    cJSON* serverDataRootJSON = cJSON_Parse(g_srvBuf);
    if (serverDataRootJSON == NULL) {
        const char* error = cJSON_GetErrorPtr();
        if (error != NULL) {
            logf("Error reading VONServerData: %s\n", error);
        }
        return;
    }

    int inGame = g_SD_inGame;
    char* chanName = NULL;
    char* chanPass = NULL;

    cJSON* serverDataJSON = cJSON_GetObjectItem(serverDataRootJSON, "ServerData");
    if (cJSON_IsObject(serverDataJSON)) {
        cJSON* inGameJSON = cJSON_GetObjectItem(serverDataJSON, "InGame");
        if (cJSON_IsBool(inGameJSON)) {
            inGame = inGameJSON->valueint;
        }

        cJSON* chanNameJSON = cJSON_GetObjectItem(serverDataJSON, "VONChannelName");
        if (cJSON_IsString(chanNameJSON)) {
            chanName = chanNameJSON->valuestring;
            trim_inplace(chanName);
        }

        cJSON* chanPassJSON = cJSON_GetObjectItem(serverDataJSON, "VONChannelPassword");
        if (cJSON_IsString(chanPassJSON)) {
            chanPass = chanPassJSON->valuestring;
            trim_inplace(chanPass);
        }

        g_SD_have   = 1;
        g_SD_inGame = inGame;
        if (chanName) {
            strncpy(g_SD_chanName, chanName, sizeof(g_SD_chanName) - 1);
            g_SD_chanName[sizeof(g_SD_chanName) - 1] = '\0';
        }
        if (chanPass) {
            strncpy(g_SD_chanPass, chanPass, sizeof(g_SD_chanPass) - 1);
            g_SD_chanPass[sizeof(g_SD_chanPass) - 1] = '\0';
        }
        cJSON* ipJSON = cJSON_GetObjectItem(serverDataJSON, "TSServerIp");
        if (cJSON_IsString(ipJSON)) {
            strncpy(g_SD_serverIp, ipJSON->valuestring, sizeof(g_SD_serverIp) - 1);
            trim_inplace(g_SD_serverIp);
        }

        cJSON* pwJSON = cJSON_GetObjectItem(serverDataJSON, "TSServerPassword");
        if (cJSON_IsString(pwJSON)) {
            strncpy(g_SD_serverPassword, pwJSON->valuestring, sizeof(g_SD_serverPassword) - 1);
            trim_inplace(g_SD_serverPassword);
        }

        cJSON* nickJSON = cJSON_GetObjectItem(serverDataJSON, "InGameName");
        if (cJSON_IsString(nickJSON)) {
            strncpy(g_SD_ingameName, nickJSON->valuestring, sizeof(g_SD_ingameName) - 1);
            trim_inplace(g_SD_ingameName);
        }

    }

    cJSON_Delete(serverDataRootJSON);
}

static void write_serverdata_if_changed(uint64 sch)
{
    if (!g_ServerPath[0])
        return;

    anyID myID = 0;
    if (ts3Functions.getClientID(sch, &myID) != ERROR_ok)
        myID = 0;

    const char* pvStr = ts3plugin_version();

    int needWrite = 0;
    if (!g_lastServerWritten.valid)
        needWrite = 1;
    else if (g_lastServerWritten.tsClientID != (unsigned)myID)
        needWrite = 1;
    else if (g_lastServerWritten.inGame != g_SD_inGame)
        needWrite = 1;
    else if (strcmp(g_lastServerWritten.pluginVersionStr, pvStr) != 0)
        needWrite = 1;
    else if (strcmp(g_lastServerWritten.chanName, g_SD_chanName) != 0)
        needWrite = 1;
    else if (strcmp(g_lastServerWritten.chanPass, g_SD_chanPass) != 0)
        needWrite = 1;

    if (!needWrite)
        return;

    ensureParentDirExists(g_ServerPath);

    // Build new JSON object but preserve what Arma set
    cJSON* serverDataJSON = cJSON_CreateObject();

    // Fields the game cares about
    cJSON_AddStringToObject(serverDataJSON, "TSServerIp", g_SD_serverIp);
    cJSON_AddStringToObject(serverDataJSON, "TSServerPassword", g_SD_serverPassword);
    cJSON_AddStringToObject(serverDataJSON, "InGameName", g_SD_ingameName);

    // Fields the plugin manages
    cJSON_AddBoolToObject(serverDataJSON, "InGame", g_SD_inGame);
    cJSON_AddNumberToObject(serverDataJSON, "TSClientID", myID);
    cJSON_AddStringToObject(serverDataJSON, "TSPluginVersion", pvStr);
    cJSON_AddStringToObject(serverDataJSON, "VONChannelName", g_SD_chanName);
    cJSON_AddStringToObject(serverDataJSON, "VONChannelPassword", g_SD_chanPass);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "ServerData", serverDataJSON);

    char* out = cJSON_Print(root);
    if (out) {
        FILE* wf = fopen(g_ServerPath, "wb");
        if (wf) {
            fprintf(wf, "%s", out);
            fclose(wf);
        }
        free(out);
    }
    cJSON_Delete(root);

    g_lastServerWritten.tsClientID = (unsigned)myID;
    g_lastServerWritten.inGame     = g_SD_inGame;
    strncpy(g_lastServerWritten.pluginVersionStr, pvStr, sizeof(g_lastServerWritten.pluginVersionStr) - 1);
    strncpy(g_lastServerWritten.chanName, g_SD_chanName, sizeof(g_lastServerWritten.chanName) - 1);
    strncpy(g_lastServerWritten.chanPass, g_SD_chanPass, sizeof(g_lastServerWritten.chanPass) - 1);
    g_lastServerWritten.valid = 1;

    g_serverWatchSuppressUntil = GetTickCount() + SERVER_WRITE_SUPPRESS_MS;
}


/* ---------------------------------------------------------------------------
   Game liveness
--------------------------------------------------------------------------- */
static char g_HeartbeatPath[PATH_BUFSIZE] = {0};
/* Set once this game session has shown a live heartbeat. The timeout is only enforced after that, so
   an older addon (never writes the file) or a leftover file from a newer one can't kick players out. */
static int  g_heartbeatArmed  = 0;
/* The first check after TeamSpeak starts is always enforced: a stale InGame=true at that point can
   only be left over from a crash. */
static int  g_livenessChecked = 0;

/* Milliseconds since the game last wrote VONHeartbeat.json. Returns 0 ("alive") when the file doesn't
   exist: an addon from before the heartbeat never writes it, and those keep the old behaviour. */
static unsigned long heartbeat_age_ms(void)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!g_HeartbeatPath[0] || !GetFileAttributesExA(g_HeartbeatPath, GetFileExInfoStandard, &fad))
        return 0;

    FILETIME nowFt;
    GetSystemTimeAsFileTime(&nowFt);
    ULARGE_INTEGER now, written;
    now.LowPart      = nowFt.dwLowDateTime;
    now.HighPart     = nowFt.dwHighDateTime;
    written.LowPart  = fad.ftLastWriteTime.dwLowDateTime;
    written.HighPart = fad.ftLastWriteTime.dwHighDateTime;
    if (now.QuadPart <= written.QuadPart)
        return 0;

    ULONGLONG ms = (now.QuadPart - written.QuadPart) / 10000ULL; /* FILETIME is 100 ns */
    return (ms > 0xFFFFFFF0ULL) ? 0xFFFFFFF0UL : (unsigned long)ms;
}

/* The game stopped beating while it said we were in game (crash, killed process, hang). Drop out of
   VON the same way a clean exit does: the out-of-game path in the worker then unmutes, returns the
   player to their previous channel and writes InGame=false, so the stale state isn't reloaded on the
   next TeamSpeak start. If the game was only frozen, it rewrites InGame=true within a second of
   resuming and the player is moved back in. */
static void handle_game_gone(unsigned long heartbeatAgeMs)
{
    (void)heartbeatAgeMs;
    logf("[CRF] Game heartbeat stale (%lu ms) - leaving VON\n", heartbeatAgeMs);
    g_SD_inGame      = 0;
    g_IsTransmitting = 0;
    g_heartbeatArmed = 0;
    InterlockedExchange(&g_inVonActiveFlag, 0);
}

/* Channel move with backoff handled by worker */
static unsigned long g_nextMoveTryTick = 0;
static unsigned long g_moveBackoffMs   = MOVE_BACKOFF_MIN_MS;

static void try_ensure_move(uint64 sch)
{
    if (!g_SD_have || !g_SD_inGame || !g_SD_chanName[0] || !sch)
        return;

    unsigned long now = GetTickCount();
    if (now < g_nextMoveTryTick)
        return;

    anyID myID = 0;
    if (ts3Functions.getClientID(sch, &myID) != ERROR_ok || myID == 0)
        return;

    uint64 myCh = 0;
    if (ts3Functions.getChannelOfClient(sch, myID, &myCh) == ERROR_ok && myCh) {
        char* curName = NULL;
        if (ts3Functions.getChannelVariableAsString(sch, myCh, CHANNEL_NAME, &curName) == ERROR_ok && curName) {
            int already = (strcmp(curName, g_SD_chanName) == 0 || (_stricmp ? _stricmp(curName, g_SD_chanName) == 0 : 0));
            ts3Functions.freeMemory(curName);
            if (already) {
                g_moveBackoffMs   = MOVE_BACKOFF_MIN_MS;
                g_nextMoveTryTick = now + g_moveBackoffMs;
                return;
            }
        }
    }

    uint64* chList = NULL;
    if (ts3Functions.getChannelList(sch, &chList) != ERROR_ok || !chList)
        return;

    uint64 target = 0;
    for (size_t i = 0; chList[i]; ++i) {
        char* name = NULL;
        if (ts3Functions.getChannelVariableAsString(sch, chList[i], CHANNEL_NAME, &name) == ERROR_ok && name) {
            if (strcmp(name, g_SD_chanName) == 0 || (_stricmp ? _stricmp(name, g_SD_chanName) == 0 : 0))
                target = chList[i];
            ts3Functions.freeMemory(name);
            if (target)
                break;
        }
    }

    if (target) {
        g_gameChannelId = target; /* cache actual game channel ID */

        int hasPw = 0;
        ts3Functions.getChannelVariableAsInt(sch, target, CHANNEL_FLAG_PASSWORD, &hasPw);

        /* Remember where the user was (if not already the game channel) */
        if (myCh && myCh != target) {
            g_prevChannelId = myCh;
        }

        const char*  pw  = (hasPw && g_SD_chanPass[0]) ? g_SD_chanPass : "";
        unsigned int err = ts3Functions.requestClientMove(sch, myID, target, pw, NULL);
        if (err == ERROR_ok) {
            logf("[CRF] requestClientMove OK\n");
            g_moveBackoffMs = MOVE_BACKOFF_MIN_MS;
        } else {
            g_moveBackoffMs = (g_moveBackoffMs < MOVE_BACKOFF_MAX_MS) ? (g_moveBackoffMs * 2) : MOVE_BACKOFF_MAX_MS;
            logf("[CRF] requestClientMove failed err=%u, backoff=%u ms\n", err, (unsigned)g_moveBackoffMs);
        }
    } else {
        g_moveBackoffMs = (g_moveBackoffMs < MOVE_BACKOFF_MAX_MS) ? (g_moveBackoffMs * 2) : MOVE_BACKOFF_MAX_MS;
        logf("[CRF] Channel '%s' not found, backoff=%u ms\n", g_SD_chanName, (unsigned)g_moveBackoffMs);
    }

    ts3Functions.freeMemory(chList);
    g_nextMoveTryTick = now + g_moveBackoffMs;
}

static void try_return_to_previous(uint64 sch)
{
    if (!sch)
        return;
    if (!g_prevChannelId)
        return;
    if (g_SD_inGame)
        return; /* only when OUT of game */

    unsigned long now = GetTickCount();
    if (now < g_nextReturnTryTick)
        return;

    anyID myID = 0;
    if (ts3Functions.getClientID(sch, &myID) != ERROR_ok || myID == 0)
        return;

    uint64 curCh = 0;
    if (ts3Functions.getChannelOfClient(sch, myID, &curCh) != ERROR_ok || !curCh)
        return;

    /* Are we still in the game channel? */
    int inGameChannel = 0;
    if (g_gameChannelId) {
        inGameChannel = (curCh == g_gameChannelId);
    } else if (g_SD_chanName[0]) {
        char* nm = NULL;
        if (ts3Functions.getChannelVariableAsString(sch, curCh, CHANNEL_NAME, &nm) == ERROR_ok && nm) {
            inGameChannel = (strcmp(nm, g_SD_chanName) == 0) || (_stricmp ? _stricmp(nm, g_SD_chanName) == 0 : 0);
            ts3Functions.freeMemory(nm);
        }
    }

    if (!inGameChannel) {
        /* User already moved elsewhere; forget stored channel */
        g_prevChannelId     = 0;
        g_returnBackoffMs   = MOVE_BACKOFF_MIN_MS;
        g_nextReturnTryTick = now + g_returnBackoffMs;
        return;
    }

    if (!channel_exists(sch, g_prevChannelId)) {
        g_prevChannelId     = 0;
        g_returnBackoffMs   = MOVE_BACKOFF_MIN_MS;
        g_nextReturnTryTick = now + g_returnBackoffMs;
        return;
    }

    unsigned int err = ts3Functions.requestClientMove(sch, myID, g_prevChannelId, "", NULL);
    if (err == ERROR_ok) {
        logf("[CRF] Returned to previous channel\n");
        g_prevChannelId     = 0;
        g_returnBackoffMs   = MOVE_BACKOFF_MIN_MS;
        g_nextReturnTryTick = now + g_returnBackoffMs;
    } else {
        g_returnBackoffMs   = (g_returnBackoffMs < MOVE_BACKOFF_MAX_MS) ? (g_returnBackoffMs * 2) : MOVE_BACKOFF_MAX_MS;
        g_nextReturnTryTick = now + g_returnBackoffMs;
        logf("[CRF] Return move failed err=%u, backoff=%u ms\n", err, (unsigned)g_returnBackoffMs);
    }
}


/* ---------------------------------------------------------------------------
   Worker thread: file reloads, mute enforcement, channel moves, mic state
--------------------------------------------------------------------------- */
static HANDLE        g_workerThread = NULL;
static HANDLE        g_quitEvent    = NULL; /* wakes the worker immediately on shutdown */
static volatile long g_workerQuit   = 0;

static unsigned long g_nextServerReloadTick = 0;
static unsigned long g_nextSlowTick         = 0;
static unsigned long g_nextInfoWriteTick    = 0;
static int           g_vonChangedSinceMute  = 0;
static int           g_mutingActive         = -1; /* -1 = not decided yet (first pass after start) */

static WatchedFileState g_vonBinWatch = {0};
static int              g_vonRetries  = 0;

/* Find or create client state for muting tracking */
static ClientApplyState* get_client_state(anyID clientID)
{
    for (size_t i = 0; i < g_LastCount; ++i) {
        if (g_Last[i].id == clientID) {
            return &g_Last[i];
        }
    }

    if (g_LastCount < sizeof(g_Last) / sizeof(g_Last[0])) {
        ClientApplyState* state = &g_Last[g_LastCount++];
        memset(state, 0, sizeof(*state));
        state->id = clientID;
        state->have = 1;
        state->isMutedByPlugin = 0;
        state->lastInRange = 1; /* assume in range initially */
        return state;
    }

    return NULL; /* array full */
}

static const VonEntry* find_entry(anyID clientID)
{
    const VonSnapshot* snap = get_active_von();
    if (!snap || !snap->loaded || snap->count == 0)
        return NULL;

    /* Binary search — entries are sorted by id when loaded. */
    size_t lo = 0, hi = snap->count;
    while (lo < hi) {
        const size_t  mid = lo + (hi - lo) / 2;
        const anyID   mid_id = snap->entries[mid].id;
        if (mid_id == clientID)
            return &snap->entries[mid];
        if (mid_id < clientID)
            lo = mid + 1;
        else
            hi = mid;
    }
    return NULL;
}

/* Mute players with no VON entry (out of range), unmute the ones that have one again.
   One TeamSpeak request for each direction instead of one per client. */
static void apply_proximity_muting(uint64 sch)
{
    if (!sch)
        return;

    anyID myID;
    if (ts3Functions.getClientID(sch, &myID) != ERROR_ok)
        return;

    uint64 myChannel;
    if (ts3Functions.getChannelOfClient(sch, myID, &myChannel) != ERROR_ok)
        return;

    anyID* clients = NULL;
    if (ts3Functions.getChannelClientList(sch, myChannel, &clients) != ERROR_ok || !clients)
        return;

    int n = 0;
    while (clients[n])
        ++n;

    anyID* toMute   = (anyID*)alloca((n + 1) * sizeof(anyID));
    anyID* toUnmute = (anyID*)alloca((n + 1) * sizeof(anyID));
    int    muteCount = 0, unmuteCount = 0;

    for (int i = 0; i < n; ++i) {
        anyID cid = clients[i];
        if (cid == myID)
            continue;
        ClientApplyState* st = get_client_state(cid);
        if (!st)
            continue;

        const int shouldMute = (find_entry(cid) == NULL);
        if (shouldMute && !st->isMutedByPlugin)
            toMute[muteCount++] = cid;
        else if (!shouldMute && st->isMutedByPlugin)
            toUnmute[unmuteCount++] = cid;
    }
    toMute[muteCount]     = 0;
    toUnmute[unmuteCount] = 0;

    if (muteCount && ts3Functions.requestMuteClients(sch, toMute, NULL) == ERROR_ok) {
        for (int i = 0; i < muteCount; ++i) {
            ClientApplyState* st = get_client_state(toMute[i]);
            if (st)
                st->isMutedByPlugin = 1;
        }
    }
    if (unmuteCount && ts3Functions.requestUnmuteClients(sch, toUnmute, NULL) == ERROR_ok) {
        for (int i = 0; i < unmuteCount; ++i) {
            ClientApplyState* st = get_client_state(toUnmute[i]);
            if (st)
                st->isMutedByPlugin = 0;
        }
    }

    ts3Functions.freeMemory(clients);
}

/* Unmute everyone the plugin muted. A player's own manual mutes are left alone. */
static void release_plugin_mutes(uint64 sch)
{
    if (!sch || g_LastCount == 0)
        return;

    anyID* ids   = (anyID*)alloca((g_LastCount + 1) * sizeof(anyID));
    int    count = 0;
    for (size_t i = 0; i < g_LastCount; ++i)
        if (g_Last[i].isMutedByPlugin)
            ids[count++] = g_Last[i].id;
    ids[count] = 0;

    if (count && ts3Functions.requestUnmuteClients(sch, ids, NULL) == ERROR_ok) {
        for (size_t i = 0; i < g_LastCount; ++i)
            g_Last[i].isMutedByPlugin = 0;
    }
}

/* One pass over the channel for mutes an earlier plugin instance made (its state is lost when the
   plugin reloads). Runs once when leaving the game or starting out of game; it used to run every
   200 ms while out of game, which spammed the API and undid the player's own mutes. */
static void unmute_channel_once(uint64 sch)
{
    if (!sch)
        return;

    anyID myID;
    if (ts3Functions.getClientID(sch, &myID) != ERROR_ok)
        return;

    uint64 myChannel;
    if (ts3Functions.getChannelOfClient(sch, myID, &myChannel) != ERROR_ok)
        return;

    anyID* clients = NULL;
    if (ts3Functions.getChannelClientList(sch, myChannel, &clients) != ERROR_ok || !clients)
        return;

    int n = 0;
    while (clients[n])
        ++n;
    anyID* ids   = (anyID*)alloca((n + 1) * sizeof(anyID));
    int    count = 0;
    for (int i = 0; i < n; ++i)
        if (clients[i] != myID)
            ids[count++] = clients[i];
    ids[count] = 0;

    if (count)
        ts3Functions.requestUnmuteClients(sch, ids, NULL);
    ts3Functions.freeMemory(clients);
}

/* ---------------------------------------------------------------------------
   VONData reload (JSON or binary, whichever the game wrote last)
--------------------------------------------------------------------------- */
static void reload_von_if_changed(void)
{
    FILETIME wt;
    const int jsonChanged = g_VonPath[0] && file_modified_since_last(g_VonPath, &g_vonDataWatch, &wt);
    const int binChanged  = g_VonBinPath[0] && file_modified_since_last(g_VonBinPath, &g_vonBinWatch, &wt);
    if (!jsonChanged && !binChanged)
        return;

    /* The game writes one format or the other; the most recently written file is current. (A clean
       game exit writes VONData.json, so it wins over a stale VONData.bin.) */
    const int useBin = g_vonBinWatch.loadedOnce &&
                       (!g_vonDataWatch.loadedOnce || CompareFileTime(&g_vonBinWatch.lastWrite, &g_vonDataWatch.lastWrite) >= 0);

    const long readIdx  = InterlockedCompareExchange(&g_vonActiveIdx, 0, 0);
    const long writeIdx = (readIdx ^ 1) & 1; /* inactive buffer */
    const int  result   = useBin ? load_von_bin_into(&g_VonSnap[writeIdx]) : load_von_into(&g_VonSnap[writeIdx]);

    if (result == 1) {
        InterlockedExchange(&g_vonActiveIdx, writeIdx); /* publish */
        g_vonChangedSinceMute = 1;
        g_vonRetries          = 0;
        return;
    }

    /* Probably caught mid-write: re-read on the next tick (a few times, then wait for the next change) */
    if (++g_vonRetries <= 5) {
        if (useBin)
            g_vonBinWatch.loadedOnce = 0;
        else
            g_vonDataWatch.loadedOnce = 0;
    } else {
        g_vonRetries = 0;
    }
}

static void reload_radio_if_changed(void)
{
    FILETIME wt;
    if (!g_RadioPath[0] || !file_modified_since_last(g_RadioPath, &g_radioWatch, &wt))
        return;
    const long readIdx  = InterlockedCompareExchange(&g_radioActiveIdx, 0, 0);
    const long writeIdx = (readIdx ^ 1) & 1;
    if (load_radio_into(&g_RadioSnap[writeIdx]))
        InterlockedExchange(&g_radioActiveIdx, writeIdx);
    else
        g_radioWatch.loadedOnce = 0; /* retry next tick */
}

/* ---------------------------------------------------------------------------
   Files the plugin writes for the game

   VONPluginInfo.json  what this plugin supports, refreshed every few seconds so the game can tell a
                       running plugin from a leftover file. The game only writes VONData.bin while
                       "bin1" is listed here.
   VONTalking.json     TeamSpeak client IDs currently talking; the game updates silent players less often.
--------------------------------------------------------------------------- */
#define PLUGIN_FEATURES "bin1|talking1|heartbeat1|comms1"
#define MAX_TALKING 256

static char             g_InfoPath[PATH_BUFSIZE]    = {0};
static char             g_TalkingPath[PATH_BUFSIZE] = {0};
static CRITICAL_SECTION g_talkLock;
static anyID            g_talking[MAX_TALKING];
static int              g_talkingCount     = 0;
static volatile long    g_talkingDirty     = 0;
static unsigned long    g_lastTalkingWrite = 0;
static int              g_talkingWrittenInGame = 0;

static void write_text_file(const char* path, const char* text)
{
    if (!path[0])
        return;
    FILE* f = fopen(path, "wb");
    if (!f)
        return;
    fputs(text, f);
    fclose(f);
}

static void write_plugin_info(void)
{
    char buf[256];
    _snprintf(buf, sizeof(buf), "{\"Version\":\"%s\",\"Features\":\"%s\",\"Time\":%lld}", ts3plugin_version(), PLUGIN_FEATURES, (long long)_time64(NULL));
    buf[sizeof(buf) - 1] = '\0';
    write_text_file(g_InfoPath, buf);
}

static void set_talking(anyID clientID, int talking)
{
    if (!clientID)
        return;
    EnterCriticalSection(&g_talkLock);
    int idx = -1;
    for (int i = 0; i < g_talkingCount; ++i)
        if (g_talking[i] == clientID) {
            idx = i;
            break;
        }
    if (talking && idx < 0 && g_talkingCount < MAX_TALKING) {
        g_talking[g_talkingCount++] = clientID;
        InterlockedExchange(&g_talkingDirty, 1);
    } else if (!talking && idx >= 0) {
        g_talking[idx] = g_talking[--g_talkingCount];
        InterlockedExchange(&g_talkingDirty, 1);
    }
    LeaveCriticalSection(&g_talkLock);
}

static void clear_talking(void)
{
    EnterCriticalSection(&g_talkLock);
    if (g_talkingCount) {
        g_talkingCount = 0;
        InterlockedExchange(&g_talkingDirty, 1);
    }
    LeaveCriticalSection(&g_talkLock);
}

/* Only while in game: outside it nothing reads the file, and a busy channel would mean constant writes */
static void write_talking_if_dirty(unsigned long now)
{
    if (!g_SD_inGame) {
        if (g_talkingWrittenInGame) {
            write_text_file(g_TalkingPath, "{\"Talking\":[]}");
            g_talkingWrittenInGame = 0;
        }
        return;
    }
    if (!InterlockedCompareExchange(&g_talkingDirty, 0, 0) && g_talkingWrittenInGame)
        return;
    if (now - g_lastTalkingWrite < TALKING_WRITE_MIN_MS)
        return;

    char buf[MAX_TALKING * 7 + 32];
    int  pos = _snprintf(buf, sizeof(buf), "{\"Talking\":[");
    EnterCriticalSection(&g_talkLock);
    InterlockedExchange(&g_talkingDirty, 0);
    for (int i = 0; i < g_talkingCount && pos < (int)sizeof(buf) - 16; ++i)
        pos += _snprintf(buf + pos, sizeof(buf) - pos, i ? ",%u" : "%u", (unsigned)g_talking[i]);
    LeaveCriticalSection(&g_talkLock);
    _snprintf(buf + pos, sizeof(buf) - pos, "]}");
    buf[sizeof(buf) - 1] = '\0';

    write_text_file(g_TalkingPath, buf);
    g_lastTalkingWrite     = now;
    g_talkingWrittenInGame = 1;
}

/* ---------------------------------------------------------------------------
   Status shown to other players (TeamSpeak's client info panel), like ACRE did

   The plugin publishes "<CVON>v=<version>;s=<state></CVON>" in this client's metadata, which the
   TeamSpeak server shares with everyone who can see the client. Selecting a player then shows their
   plugin version and whether their game is connected; selecting a channel shows how many people in it
   have the plugin and are in game. Only our own tag is touched, so other plugins' metadata survives.
--------------------------------------------------------------------------- */
enum { CVON_STATE_NOT_IN_GAME = 0, CVON_STATE_IN_GAME = 1, CVON_STATE_IN_VON = 2 };
#define CVON_META_OPEN "<CVON>"
#define CVON_META_CLOSE "</CVON>"
#define CVON_META_MAX 4096

static char          g_lastStatusTag[128] = {0};
static uint64        g_lastStatusSch      = 0;
static volatile long g_statusRepublish    = 0; /* set on (re)connect: publish even if unchanged */

/* Copy metadata without our own <CVON>...</CVON> segment */
static void strip_status_tag(const char* meta, char* out, size_t outSize)
{
    out[0] = '\0';
    if (!meta)
        return;
    const char* open  = strstr(meta, CVON_META_OPEN);
    const char* close = open ? strstr(open, CVON_META_CLOSE) : NULL;
    if (!open || !close) {
        _snprintf(out, outSize, "%s", meta);
        out[outSize - 1] = '\0';
        return;
    }
    _snprintf(out, outSize, "%.*s%s", (int)(open - meta), meta, close + strlen(CVON_META_CLOSE));
    out[outSize - 1] = '\0';
}

/* Reads a player's <CVON> tag. Returns 0 when they don't have it (no plugin, or an older one). */
static int parse_status_tag(const char* meta, char* version, size_t versionSize, int* state)
{
    version[0] = '\0';
    *state     = -1;
    if (!meta)
        return 0;
    const char* open  = strstr(meta, CVON_META_OPEN);
    const char* close = open ? strstr(open, CVON_META_CLOSE) : NULL;
    if (!open || !close)
        return 0;

    char body[128];
    const size_t len = (size_t)(close - open - strlen(CVON_META_OPEN));
    if (len >= sizeof(body))
        return 0;
    memcpy(body, open + strlen(CVON_META_OPEN), len);
    body[len] = '\0';

    char* ctx = NULL;
    for (char* part = strtok_s(body, ";", &ctx); part; part = strtok_s(NULL, ";", &ctx)) {
        if (strncmp(part, "v=", 2) == 0) {
            _snprintf(version, versionSize, "%s", part + 2);
            version[versionSize - 1] = '\0';
        } else if (strncmp(part, "s=", 2) == 0) {
            *state = atoi(part + 2);
        }
    }
    return 1;
}

/* Writes metadata = (whatever other plugins stored) + our tag, or without our tag when tag is NULL */
static int set_status_metadata(uint64 sch, const char* tag)
{
    char* cur    = NULL;
    char* merged = (char*)malloc(CVON_META_MAX);
    if (!merged)
        return 0;
    if (ts3Functions.getClientSelfVariableAsString(sch, CLIENT_META_DATA, &cur) == ERROR_ok && cur) {
        strip_status_tag(cur, merged, CVON_META_MAX);
        ts3Functions.freeMemory(cur);
    } else {
        merged[0] = '\0';
    }
    if (tag && strlen(merged) + strlen(tag) < CVON_META_MAX)
        strcat(merged, tag);

    const int ok = ts3Functions.setClientSelfVariableAsString(sch, CLIENT_META_DATA, merged) == ERROR_ok &&
                   ts3Functions.flushClientSelfUpdates(sch, NULL) == ERROR_ok;
    free(merged);
    return ok;
}

static void clear_status(uint64 sch);

/* Slow path: publish our version and state whenever they change */
static void publish_status(uint64 sch, int state)
{
    if (!sch)
        return;
    /* Switched server tab: the tab we left must not keep showing us as connected */
    if (g_lastStatusSch && g_lastStatusSch != sch)
        clear_status(g_lastStatusSch);
    if (InterlockedExchange(&g_statusRepublish, 0))
        g_lastStatusTag[0] = '\0';

    char tag[128];
    _snprintf(tag, sizeof(tag), CVON_META_OPEN "v=%s;s=%d" CVON_META_CLOSE, ts3plugin_version(), state);
    tag[sizeof(tag) - 1] = '\0';
    if (sch == g_lastStatusSch && strcmp(tag, g_lastStatusTag) == 0)
        return;
    if (set_status_metadata(sch, tag)) {
        g_lastStatusSch = sch;
        strncpy(g_lastStatusTag, tag, sizeof(g_lastStatusTag) - 1);
    }
}

/* Plugin unloading: nobody should see us as connected any more */
static void clear_status(uint64 sch)
{
    if (sch && g_lastStatusSch == sch)
        set_status_metadata(sch, NULL);
    g_lastStatusSch    = 0;
    g_lastStatusTag[0] = '\0';
}

static const char* status_text(int state)
{
    switch (state) {
    case CVON_STATE_IN_VON: return "[color=#3f9b5c]In game, connected to VON[/color]";
    case CVON_STATE_IN_GAME: return "[color=#d08a1c]In game, not in the VON channel[/color]";
    default: return "Not in game";
    }
}

/* ---------------------------------------------------------------------------
   Auto-connect (slow path)
--------------------------------------------------------------------------- */
static void try_auto_connect(uint64 sch)
{
/* Auto-connect if IP changed */
    if (g_SD_have && g_SD_inGame && g_SD_serverIp[0]) {
        if (strcmp(g_lastAutoConnectIp, g_SD_serverIp) != 0) {
                int    alreadyConnected = 0;
                if (sch) {

                anyID myId = 0;
                if (ts3Functions.getClientID(sch, &myId) == ERROR_ok && myId != 0) {

                    char* curIp = NULL;

                    if (ts3Functions.getConnectionVariableAsString(sch, myId, CONNECTION_SERVER_IP, &curIp) == ERROR_ok && curIp) {
                        alreadyConnected = (strcmp(curIp, g_SD_serverIp) == 0);
                        ts3Functions.freeMemory(curIp);
                    }
                }

                if (alreadyConnected) {
                    /* Mark this IP as handled so we do NOT ask again */
                    strncpy(g_lastAutoConnectIp, g_SD_serverIp, sizeof(g_lastAutoConnectIp) - 1);
                    g_lastAutoConnectIp[sizeof(g_lastAutoConnectIp) - 1] = '\0';
                    /* Skip popup & connection attempt */
                    goto skip_autoconnect;
                }

                /* ---- Original logic follows only if not already connected ---- */
                if (strcmp(g_lastAutoConnectIp, g_SD_serverIp) != 0) {

                    const char* nickname = g_SD_ingameName[0] ? g_SD_ingameName : "ReforgerUser";

                    if (!confirm_autoconnect(g_SD_serverIp, nickname)) {
                        strncpy(g_lastAutoConnectIp, g_SD_serverIp, sizeof(g_lastAutoConnectIp) - 1);
                        g_lastAutoConnectIp[sizeof(g_lastAutoConnectIp) - 1] = '\0';
                        goto skip_autoconnect; /* do NOT try again until IP changes */
                    }

                    /* Close old auto-connection if needed */
                    if (g_lastAutoConnectSch) {
                        unsigned int derr = ts3Functions.stopConnection(g_lastAutoConnectSch, "Switching game server");
                        if (derr == ERROR_ok) {
                            logf("[CRF] Closed previous connection (%s)\n", g_lastAutoConnectIp);
                        } else {
                            logf("[CRF] Failed to close previous connection, err=%u\n", derr);
                        }
                        g_lastAutoConnectSch   = 0;
                        g_lastAutoConnectIp[0] = '\0';
                    }

                    /* Connect */
                    uint64       newSch = 0;
                    unsigned int err    = ts3Functions.guiConnect(PLUGIN_CONNECT_TAB_CURRENT, "Arma Reforger AutoConnect", g_SD_serverIp, g_SD_serverPassword, nickname, "", "", NULL, NULL, NULL, NULL, NULL, NULL, NULL, &newSch);

                    if (err == ERROR_ok) {
                        logf("[CRF] Auto-connected to %s as %s\n", g_SD_serverIp, nickname);
                        strncpy(g_lastAutoConnectIp, g_SD_serverIp, sizeof(g_lastAutoConnectIp) - 1);
                        g_lastAutoConnectIp[sizeof(g_lastAutoConnectIp) - 1] = '\0';
                        g_lastAutoConnectSch                                 = newSch;
                    } else {
                        logf("[CRF] Auto-connect failed (%u)\n", err);
                    }
                }
                skip_autoconnect:;
            }
        }
    }
}

static void profile_dir_of(const char* filePath, char* out, size_t outSize)
{
    _snprintf(out, outSize, "%s", filePath);
    out[outSize - 1] = '\0';
    char* last = strrchr(out, '\\');
    if (last)
        *last = '\0';
    else
        out[0] = '\0';
}

static unsigned long WINAPI worker_main(LPVOID param)
{
    (void)param;

    /* Wake as soon as the game writes a file in its profile folder instead of polling on a timer */
    char   profileDir[PATH_BUFSIZE];
    HANDLE change = INVALID_HANDLE_VALUE;
    profile_dir_of(g_VonPath, profileDir, sizeof(profileDir));
    if (profileDir[0])
        change = FindFirstChangeNotificationA(profileDir, FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE);

    while (InterlockedCompareExchange(&g_workerQuit, 0, 0) == 0) {
        const unsigned long now = GetTickCount();
        const uint64        sch = ts3Functions.getCurrentServerConnectionHandlerID();

        /* ---------------- ServerData reload/write ---------------- */
        if (now >= g_nextServerReloadTick) {
            g_nextServerReloadTick = now + SERVERDATA_RELOAD_MS;
            if (now >= g_serverWatchSuppressUntil) {
                FILETIME wt;
                if (g_ServerPath[0] && file_modified_since_last(g_ServerPath, &g_serverWatch, &wt))
                    read_serverdata_from_disk();
            }
            write_serverdata_if_changed(sch);
        }

        /* ---------------- Game liveness ---------------- */
        /* Before auto-connect and channel moves, so a stale InGame=true left by a crash (also on a fresh
           TeamSpeak start) never triggers the connect prompt or a move into the VON channel. */
        const unsigned long heartbeatAge = heartbeat_age_ms();
        if (!g_SD_inGame)
            g_heartbeatArmed = 0;
        else if (heartbeatAge < HEARTBEAT_MIC_STALE_MS)
            g_heartbeatArmed = 1;

        if (g_SD_inGame && heartbeatAge >= HEARTBEAT_GONE_MS && (g_heartbeatArmed || !g_livenessChecked)) {
            handle_game_gone(heartbeatAge);
            write_serverdata_if_changed(sch);
        }
        g_livenessChecked = 1;
        const int heartbeatOk = !g_heartbeatArmed || heartbeatAge < HEARTBEAT_MIC_STALE_MS;

        /* ---------------- Fast path: every wake ---------------- */
        reload_von_if_changed();
        reload_radio_if_changed();
        apply_mic_state(sch, g_SD_inGame && heartbeatOk);
        write_talking_if_dirty(now);

        if (now >= g_nextInfoWriteTick) {
            g_nextInfoWriteTick = now + INFO_WRITE_MS;
            write_plugin_info();
        }

        /* ---------------- Slow path ---------------- */
        int slowTick = 0;
        if (now >= g_nextSlowTick) {
            g_nextSlowTick = now + SLOW_TICK_MS;
            slowTick       = 1;
            try_auto_connect(sch);
            try_ensure_move(sch);
            try_return_to_previous(sch);
            update_von_active_flag(sch);

            /* What other players see in TeamSpeak's info panel when they select us */
            int state = CVON_STATE_NOT_IN_GAME;
            if (g_SD_inGame && heartbeatOk)
                state = InterlockedCompareExchange(&g_inVonActiveFlag, 0, 0) ? CVON_STATE_IN_VON : CVON_STATE_IN_GAME;
            publish_status(sch, state);
        }

        /* ---------------- Mute enforcement ---------------- */
        /* Only inside the VON channel: muting players in a normal channel because they have no VON
           entry would silence the player's friends if the move into VON failed. Runs whenever new VON
           data arrives, plus on the slow tick. */
        if (sch) {
            const int wantMuting = g_SD_inGame && InterlockedCompareExchange(&g_inVonActiveFlag, 0, 0);
            if (wantMuting) {
                if (g_vonChangedSinceMute || slowTick) {
                    apply_proximity_muting(sch);
                    g_vonChangedSinceMute = 0;
                }
            } else if (g_mutingActive != 0) {
                release_plugin_mutes(sch);
                unmute_channel_once(sch);
            }
            g_mutingActive = wantMuting;
        }

        /* ---------------- Wait for a file change, shutdown, or the tick ---------------- */
        HANDLE handles[2] = {g_quitEvent, change};
        const DWORD n = (change != INVALID_HANDLE_VALUE) ? 2 : 1;
        const DWORD r = WaitForMultipleObjects(n, handles, FALSE, WORKER_TICK_MS);
        if (r == WAIT_OBJECT_0 + 1)
            FindNextChangeNotification(change);
    }

    if (change != INVALID_HANDLE_VALUE)
        FindCloseChangeNotification(change);
    return 0;
}
/* ---------------------------------------------------------------------------
   TS3 exports (metadata)
--------------------------------------------------------------------------- */
static int wcharToUtf8(const wchar_t* str, char** result)
{
    int outlen = WideCharToMultiByte(CP_UTF8, 0, str, -1, 0, 0, 0, 0);
    *result    = (char*)malloc(outlen);
    if (!*result)
        return -1;
    if (WideCharToMultiByte(CP_UTF8, 0, str, -1, *result, outlen, 0, 0) == 0) {
        free(*result);
        *result = NULL;
        return -1;
    }
    return 0;
}
PLUGINS_EXPORTDLL const char* ts3plugin_name()
{
    static char* result = NULL;
    if (!result) {
        const wchar_t* name = L"Coalition TeamSpeak Plugin";
        if (wcharToUtf8(name, &result) == -1)
            result = "Coalition TeamSpeak Plugin";
    }
    return result;
}
PLUGINS_EXPORTDLL const char* ts3plugin_version()
{
    return "3.0.0";
}
PLUGINS_EXPORTDLL int ts3plugin_apiVersion()
{
    return PLUGIN_API_VERSION;
}
PLUGINS_EXPORTDLL const char* ts3plugin_author()
{
    return "Salami";
}
PLUGINS_EXPORTDLL const char* ts3plugin_description()
{
    return "A tool for Arma Reforger to communicate with Teamspeak - includes proximity-based automatic muting";
}
PLUGINS_EXPORTDLL void ts3plugin_setFunctionPointers(const struct TS3Functions funcs)
{
    ts3Functions = funcs;
}

PLUGINS_EXPORTDLL int ts3plugin_init()
{
    InitializeCriticalSection(&g_directLock);
    InitializeCriticalSection(&g_radioLock);
    InitializeCriticalSection(&g_talkLock);
    memset((void*)g_directStates, 0, sizeof(g_directStates));
    memset((void*)g_radioStates, 0, sizeof(g_radioStates));
    g_talkingCount = 0;

    buildVONDataPath(g_VonPath, sizeof(g_VonPath));
    buildServerJsonPath(g_ServerPath, sizeof(g_ServerPath));
    buildRadioJsonPath(g_RadioPath, sizeof(g_RadioPath));
    buildHeartbeatPath(g_HeartbeatPath, sizeof(g_HeartbeatPath));
    buildProfileFilePath("VONData.bin", g_VonBinPath, sizeof(g_VonBinPath));
    buildProfileFilePath("VONPluginInfo.json", g_InfoPath, sizeof(g_InfoPath));
    buildProfileFilePath("VONTalking.json", g_TalkingPath, sizeof(g_TalkingPath));

    if (g_VonPath[0])
        ensureParentDirExists(g_VonPath);
    if (g_ServerPath[0])
        ensureParentDirExists(g_ServerPath);
    if (g_RadioPath[0])
        ensureParentDirExists(g_RadioPath);

    g_LastCount                = 0;
    memset(g_Last, 0, sizeof(g_Last));  /* Clear client state tracking */
    g_vonDataWatch.loadedOnce  = 0;
    g_vonBinWatch.loadedOnce   = 0;
    g_serverWatch.loadedOnce   = 0;
    g_radioWatch.loadedOnce    = 0;
    g_SD_have                  = 0;
    g_SD_inGame                = 0;
    g_SD_chanName[0]           = '\0';
    g_SD_chanPass[0]           = '\0';
    g_serverWatchSuppressUntil = 0;
    g_lastServerWritten.valid  = 0;
    g_heartbeatArmed           = 0;
    g_livenessChecked          = 0;
    InterlockedExchange(&g_inVonActiveFlag, 0);
    g_mutingActive             = -1;
    g_nextSlowTick             = 0;
    g_nextInfoWriteTick        = 0;

    /* Build initial snapshots so audio has something valid to read */
    {
        /* VON */
        long w = 0;
        memset(&g_VonSnap[w], 0, sizeof(g_VonSnap[w]));
        load_von_into(&g_VonSnap[w]); /* best effort */
        InterlockedExchange(&g_vonActiveIdx, w);

        /* Radio */
        w = 0;
        memset(&g_RadioSnap[w], 0, sizeof(g_RadioSnap[w]));
        load_radio_into(&g_RadioSnap[w]); /* best effort */
        InterlockedExchange(&g_radioActiveIdx, w);
    }

    read_serverdata_from_disk();
    write_serverdata_if_changed(0);
    write_plugin_info();

    InterlockedExchange(&g_workerQuit, 0);
    g_quitEvent    = CreateEventA(NULL, TRUE, FALSE, NULL);
    g_workerThread = CreateThread(NULL, 0, worker_main, NULL, 0, NULL);


    // Safety: ensure mic is not left closed
    if (ts3Functions.getCurrentServerConnectionHandlerID()) {
        reset_mic_state(ts3Functions.getCurrentServerConnectionHandlerID());
    }

    logf("[CRF] Init complete\n");
    return 0;
}
PLUGINS_EXPORTDLL void ts3plugin_shutdown()
{
    if (ts3Functions.getCurrentServerConnectionHandlerID()) {
        reset_mic_state(ts3Functions.getCurrentServerConnectionHandlerID());
    }

    InterlockedExchange(&g_workerQuit, 1);
    if (g_quitEvent)
        SetEvent(g_quitEvent);
    if (g_workerThread) {
        WaitForSingleObject(g_workerThread, 3000);
        CloseHandle(g_workerThread);
        g_workerThread = NULL;
    }
    if (g_quitEvent) {
        CloseHandle(g_quitEvent);
        g_quitEvent = NULL;
    }

    /* After the worker has stopped, so the two never touch the mute state at once */
    if (ts3Functions.getCurrentServerConnectionHandlerID())
        release_plugin_mutes(ts3Functions.getCurrentServerConnectionHandlerID());
    clear_status(g_lastStatusSch);

    /* The game must not keep writing VONData.bin for a plugin that is no longer running */
    if (g_InfoPath[0])
        DeleteFileA(g_InfoPath);
    if (g_TalkingPath[0])
        DeleteFileA(g_TalkingPath);
    if (g_vonBinBuf) {
        free(g_vonBinBuf);
        g_vonBinBuf = NULL;
        g_vonBinCap = 0;
    }
    if (g_vonBuf) {
        free(g_vonBuf);
        g_vonBuf = NULL;
        g_vonCap = 0;
    }
    if (g_radioBuf) {
        free(g_radioBuf);
        g_radioBuf = NULL;
        g_radioCap = 0;
    }
    if (g_srvBuf) {
        free(g_srvBuf);
        g_srvBuf = NULL;
        g_srvCap = 0;
    }
    DeleteCriticalSection(&g_directLock);
    DeleteCriticalSection(&g_radioLock);
    DeleteCriticalSection(&g_talkLock);
}

/* ---------------------------------------------------------------------------
   Events
--------------------------------------------------------------------------- */
PLUGINS_EXPORTDLL void ts3plugin_onConnectStatusChangeEvent(uint64 sch, int newStatus, unsigned int errorNumber)
{
    (void)errorNumber;
    if (newStatus == STATUS_DISCONNECTED)
        clear_talking();
    if (newStatus == STATUS_CONNECTION_ESTABLISHED) {
        g_mutingActive         = -1;
        g_nextSlowTick         = 0;
        g_nextServerReloadTick = 0;
        InterlockedExchange(&g_statusRepublish, 1);
        g_nextMoveTryTick      = 0;
        g_moveBackoffMs        = MOVE_BACKOFF_MIN_MS;

        /* reset return-to-previous state */
        g_prevChannelId     = 0;
        g_gameChannelId     = 0;
        g_nextReturnTryTick = 0;
        g_returnBackoffMs   = MOVE_BACKOFF_MIN_MS;

        logf("[CRF] Connection established\n");
    }
}
PLUGINS_EXPORTDLL void ts3plugin_currentServerConnectionChanged(uint64 sch)
{
    (void)sch;
    g_LastCount  = 0;
    clear_talking();
}

/* ---------------------------------------------------------------------------
   Info panel: selecting a player or channel in TeamSpeak shows this under "Coalition VON"
--------------------------------------------------------------------------- */
PLUGINS_EXPORTDLL const char* ts3plugin_infoTitle()
{
    return "Coalition VON";
}

static char* dup_text(const char* text)
{
    size_t n   = strlen(text) + 1;
    char*  out = (char*)malloc(n);
    if (out)
        memcpy(out, text, n);
    return out;
}

PLUGINS_EXPORTDLL void ts3plugin_infoData(uint64 sch, uint64 id, enum PluginItemType type, char** data)
{
    *data = NULL;
    char text[512];

    if (type == PLUGIN_CLIENT) {
        char* meta = NULL;
        char  version[32];
        int   state = -1;
        int   has   = 0;
        if (ts3Functions.getClientVariableAsString(sch, (anyID)id, CLIENT_META_DATA, &meta) == ERROR_ok && meta) {
            has = parse_status_tag(meta, version, sizeof(version), &state);
            ts3Functions.freeMemory(meta);
        }
        if (!has) {
            _snprintf(text, sizeof(text), "Plugin: [color=#c93636]not detected[/color]");
        } else {
            const int sameVersion = strcmp(version, ts3plugin_version()) == 0;
            _snprintf(text, sizeof(text), "Plugin: v%s%s\nArma Reforger: %s",
                      version[0] ? version : "?",
                      sameVersion ? "" : " [color=#c93636](different from yours)[/color]",
                      status_text(state));
        }
    } else if (type == PLUGIN_CHANNEL) {
        anyID* clients = NULL;
        if (ts3Functions.getChannelClientList(sch, id, &clients) != ERROR_ok || !clients)
            return;
        int total = 0, withPlugin = 0, inGame = 0;
        for (int i = 0; clients[i]; ++i) {
            ++total;
            char* meta = NULL;
            char  version[32];
            int   state = -1;
            if (ts3Functions.getClientVariableAsString(sch, clients[i], CLIENT_META_DATA, &meta) == ERROR_ok && meta) {
                if (parse_status_tag(meta, version, sizeof(version), &state)) {
                    ++withPlugin;
                    if (state >= CVON_STATE_IN_GAME)
                        ++inGame;
                }
                ts3Functions.freeMemory(meta);
            }
        }
        ts3Functions.freeMemory(clients);
        if (total == 0)
            return;
        _snprintf(text, sizeof(text), "%d of %d have the plugin\n%d in game", withPlugin, total, inGame);
    } else {
        return;
    }

    text[sizeof(text) - 1] = '\0';
    *data = dup_text(text);
}

/* Frees what ts3plugin_infoData allocated */
PLUGINS_EXPORTDLL void ts3plugin_freeMemory(void* data)
{
    free(data);
}

/* Someone's metadata (and so their status) may have changed: refresh the panel if it shows them */
PLUGINS_EXPORTDLL void ts3plugin_onUpdateClientEvent(uint64 sch, anyID clientID, anyID invokerID, const char* invokerName, const char* invokerUniqueIdentifier)
{
    (void)invokerID;
    (void)invokerName;
    (void)invokerUniqueIdentifier;
    ts3Functions.requestInfoUpdate(sch, PLUGIN_CLIENT, clientID);
}

/* Who is talking, for VONTalking.json (the game updates silent players less often) */
PLUGINS_EXPORTDLL void ts3plugin_onTalkStatusChangeEvent(uint64 sch, int status, int isReceivedWhisper, anyID clientID)
{
    (void)isReceivedWhisper;
    if (sch != ts3Functions.getCurrentServerConnectionHandlerID())
        return;
    set_talking(clientID, status == STATUS_TALKING);
}
PLUGINS_EXPORTDLL void ts3plugin_onClientMoveEvent(uint64 sch, anyID clientID, uint64 oldCh, uint64 newCh, int visibility, const char* moveMessage)
{
    (void)oldCh;
    (void)newCh;
    (void)moveMessage;
    
    /* Clean up muting state for clients who leave visibility */
    if (visibility == LEAVE_VISIBILITY) {
        /* Their client ID can be reused by someone else: start any effect state from scratch */
        reset_client_states(clientID);
        set_talking(clientID, 0);
        ClientApplyState* state = get_client_state(clientID);
        if (state && state->isMutedByPlugin) {
            /* Client is leaving - clear our muting state */
            state->isMutedByPlugin = 0;
            state->lastInRange = 1; /* reset for next time */
            logf("[CRF] Client %u left, cleared muting state\n", (unsigned)clientID);
        }
    }
    
    anyID me = 0;
    if (ts3Functions.getClientID(sch, &me) != ERROR_ok || me == 0)
        return;
    if (clientID != me)
        return;
    update_von_active_flag(sch);
}

static inline void silence_samples(short* samples, int sampleCount, int channels)
{
    if (channels <= 1) {
        memset(samples, 0, sizeof(short) * sampleCount);
    } else {
        for (int i = 0; i < sampleCount; ++i) {
            int idx          = i * channels;
            samples[idx + 0] = 0;
            samples[idx + 1] = 0;
            for (int ch = 2; ch < channels; ++ch)
                samples[idx + ch] = 0;
        }
    }
}

/* ---------------------------------------------------------------------------
   Audio (pure DSP)
--------------------------------------------------------------------------- */
PLUGINS_EXPORTDLL void ts3plugin_onEditPostProcessVoiceDataEvent(uint64 sch, anyID clientID, short* samples, int sampleCount, int channels, const unsigned int* channelSpeakerArray, unsigned int* channelFillMask)
{
    (void)channelSpeakerArray;
    (void)channelFillMask;

    if (InterlockedCompareExchange(&g_inVonActiveFlag, 0, 0) == 0)
        return; /* leave TS audio untouched */

    if (!samples || sampleCount <= 0 || channels <= 0)
        return;

    const VonEntry* e = find_entry(clientID);
    if (!e) {
        silence_samples(samples, sampleCount, channels);
        return;
    }

    unsigned long age = GetTickCount() - e->lastUpdateTick;
    /* TEMP: do not hard-silence talkers just because VON JSON did not refresh.
   When gains remain unchanged (for example 1.0 / 1.0 centered), the game may
   stop touching the file even though the talker is still valid. */
    /*
    if (age > 250) {
        silence_samples(samples, sampleCount, channels);
        return;
    }
    */
    (void)age;

    /* ----------------------------- RADIO talkers ----------------------------- */
    if (e->type == VON_RADIO) {
        float                vol01    = 0.0f;
        int                  stereo   = 0;
        const RadioSnapshot* rSnap    = get_active_radio();
        const int            hasRadio = (rSnap && rSnap->loaded) ? 1 : 0;
        int                  matched  = 0;

        if (hasRadio) {
            for (size_t i = 0; i < rSnap->count; ++i) {
                RadioLocal* r = (RadioLocal*)&rSnap->list[i];
                if (strcmp(r->freq, e->txFreq) != 0)
                    continue;
                if (!(r->timeDev == INT_MIN || e->txTimeDev == INT_MIN || r->timeDev == e->txTimeDev))
                    continue;
                int rf = (r->faction[0] && strcmp(r->faction, "0") != 0);
                int vf = (e->txFaction[0] && strcmp(e->txFaction, "0") != 0);
                if (rf && vf && strcmp(r->faction, e->txFaction) != 0)
                    continue;
                vol01   = (float)r->volStep / 9.0f;
                stereo  = r->stereo;
                matched = 1;
                break;
            }
        }

        if (matched) {
            const float q  = clampf(e->connQ, 0.0f, 1.0f);
            RadioState* st = get_radio_state(clientID);
            if (!st || samples_all_zero(samples, sampleCount * channels))
                return; /* state table full, or a silent buffer: nothing to colour */

            int processed = 0;
            while (processed < sampleCount) {
                int n = sampleCount - processed;
                if (n > RADIO_PROCESS_CHUNK)
                    n = RADIO_PROCESS_CHUNK;

                float mono[RADIO_PROCESS_CHUNK];
                float voice[RADIO_PROCESS_CHUNK];
                float hiss[RADIO_PROCESS_CHUNK];
                float outL[RADIO_PROCESS_CHUNK];
                float outR[RADIO_PROCESS_CHUNK];

                if (channels <= 1) {
                    for (int i = 0; i < n; ++i)
                        mono[i] = (float)samples[processed + i] / 32768.0f;
                } else {
                    for (int i = 0; i < n; ++i) {
                        const int   idx = (processed + i) * channels;
                        const float L   = (float)samples[idx + 0] / 32768.0f;
                        const float R   = (float)samples[idx + 1] / 32768.0f;
                        mono[i]         = 0.5f * (L + R);
                    }
                }

                for (int i = 0; i < n; ++i) {
                    voice[i] = mono[i];
                    outL[i] = outR[i] = 0.0f;
                }

                radio_color_voice_only(st, voice, n, vol01, q);

                const float voiceGain = clampf(vol01, 0.0f, 1.0f); // quality affects FX/noise, not loudness
                for (int i = 0; i < n; ++i)
                    voice[i] *= voiceGain;

                radio_make_static(st, hiss, n, q, q);

                if (channels <= 1) {
                    for (int i = 0; i < n; ++i) {
                        float s = clampf(voice[i] + hiss[i], -1.0f, 1.0f);
                        s *= RADIO_GAIN_MUL;

                        outL[i] = s;
                        outR[i] = s;
                    }
                } else {
                    for (int i = 0; i < n; ++i) {
                        float s = clampf(voice[i] + hiss[i], -1.0f, 1.0f);
                        s *= RADIO_GAIN_MUL;

                        float Ls = s, Rs = s;
                        if (stereo == 1) {
                            Ls = 0.0f;
                            Rs = s;
                        } else if (stereo == 2) {
                            Ls = s;
                            Rs = 0.0f;
                        }
                        outL[i] = Ls;
                        outR[i] = Rs;
                    }
                }

                const float headroom = 0.85f;
                if (channels <= 1) {
                    for (int i = 0; i < n; ++i) {
                        float m                = 0.5f * (outL[i] + outR[i]);
                        m                      = clampf(m * headroom, -1.0f, 1.0f);
                        samples[processed + i] = clamp_i16(m * 32767.0f);
                    }
                } else {
                    for (int i = 0; i < n; ++i) {
                        const int   idx      = (processed + i) * channels;
                        const float L        = clampf(outL[i] * headroom, -1.0f, 1.0f);
                        const float R        = clampf(outR[i] * headroom, -1.0f, 1.0f);
                        const short Ls       = clamp_i16(L * 32767.0f);
                        const short Rs       = clamp_i16(R * 32767.0f);
                        samples[idx + 0]     = Ls;
                        samples[idx + 1]     = Rs;
                        const short monoFill = (short)((Ls + Rs) / 2);
                        for (int ch = 2; ch < channels; ++ch)
                            samples[idx + ch] = monoFill;
                    }
                }
                processed += n;
            }
            return;
        } else {
            /* No local radio matches this talker's frequency — fall back to direct-path DSP. */
            apply_direct_dsp(clientID, e, samples, sampleCount, channels);
            return;
        }
    }

    /* ------------------------- Vehicle intercom / speaker ------------------------- */
    if (e->type == VON_INTERCOM) {
        apply_intercom_dsp(clientID, e, samples, sampleCount, channels);
        return;
    }
    if (e->type == VON_SPEAKER) {
        apply_speaker_dsp(clientID, e, samples, sampleCount, channels);
        return;
    }

    /* ----------------------------- DIRECT talkers ----------------------------- */
    apply_direct_dsp(clientID, e, samples, sampleCount, channels);
}

/* ---------------------------------------------------------------------------
   Mixed playback (we no longer touch per-client volume here)
--------------------------------------------------------------------------- */
PLUGINS_EXPORTDLL void ts3plugin_onEditMixedPlaybackVoiceDataEvent(uint64 sch, short* samples, int sampleCount, int channels, const unsigned int* channelSpeakerArray, unsigned int* channelFillMask)
{
    (void)sch;
    (void)samples;
    (void)sampleCount;
    (void)channels;
    (void)channelSpeakerArray;
    (void)channelFillMask;
    /* Intentionally no-op to avoid "messing with volume". All shaping is in per-client callback. */
}