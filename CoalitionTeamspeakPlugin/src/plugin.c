/* 
 * CRF TeamSpeak 3 Proximity VOIP Plugin (Windows-only, Arma Reforger)
 *
 * - DIRECT (VONType=0):
 *     Uses LeftGain/RightGain from VONData.json (already spatialized by game).
 *     No plugin-side attenuation. We only apply a mild "yell" boost if the
 *     average L/R gain exceeds ~1.0 (interpreted as the game indicating shouting).
 *     NEW: Applies a muffle (low-pass) driven by MuffledDecibels (negative dB):
 *          e.g., -12 dB ≈ inside vehicle (~4.5 kHz), -18 dB ≈ inside building (~2.7 kHz).
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
 *         (with yell boost + NEW muffle filter based on MuffledDecibels).
 *
 * - Mic control: IsTransmitting toggles CLIENT_INPUT_DEACTIVATED.
 * - Auto-move to VONChannelName (and optional VONChannelPassword) when InGame==true.
 * - I/O and channel moves run in a 500 ms worker thread (audio callbacks are pure DSP).
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

/* Requested reload cadences */
#define VONDATA_RELOAD_MS 50
#define RADIODATA_RELOAD_MS 300
#define SERVERDATA_RELOAD_MS 500

/* Worker + move handling */
#define WORKER_TICK_MS 200
#define SERVER_WRITE_SUPPRESS_MS 750
#define MOVE_BACKOFF_MIN_MS 1500
#define MOVE_BACKOFF_MAX_MS 30000

/* Raised global JSON attribute polling throttle (kept >=250 ms) */
#define JSON_RELOAD_THROTTLE_MS 300

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


static struct TS3Functions ts3Functions;
static uint64              g_prevChannelId     = 0; /* user’s last non-game channel (this conn) */
static uint64              g_gameChannelId     = 0; /* cached ID of the VONChannelName */
static DWORD               g_nextReturnTryTick = 0;
static DWORD               g_returnBackoffMs   = MOVE_BACKOFF_MIN_MS;
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
typedef enum { VON_DIRECT = 0, VON_RADIO = 1 } EVONType;

typedef struct {
    anyID    id;
    EVONType type;
    float    leftGain;
    float    rightGain;
    char     txFreq[64];
    int      txTimeDev;
    float    connQ;
    char     txFaction[64];
    float    muffledDb;
    float    behindIntensity;
    int      sameLanguage;
    DWORD    lastUpdateTick;
} VonEntry;

/* -------- Double-buffered snapshots -------- */
typedef struct {
    VonEntry entries[MAX_TRACKED];
    size_t   count;
    int      loaded;
    DWORD    buildTick;
} VonSnapshot;

typedef struct {
    char freq[64];
    int  timeDev;
    int  volStep;
    int  stereo;
    char faction[64];
} RadioLocal;

typedef struct {
    RadioLocal list[256];
    size_t     count;
    int        loaded;
    DWORD      buildTick;
} RadioSnapshot;

/* Two buffers each; audio reads ACTIVE, worker writes INACTIVE */
static VonSnapshot   g_VonSnap[2]   = {0};
static RadioSnapshot g_RadioSnap[2] = {0};

/* Active indices (0 or 1). Audio reads via atomic load; worker flips. */
static volatile LONG g_vonActiveIdx   = 0;
static volatile LONG g_radioActiveIdx = 0;

/* Paths (was g_Von.jsonPath earlier) */
static char g_VonPath[PATH_BUFSIZE] = {0};

/* ServerData cache */
static char  g_ServerPath[PATH_BUFSIZE] = {0};
static int   g_SD_have = 0, g_SD_inGame = 0;
static char  g_SD_chanName[512]         = {0};
static char  g_SD_chanPass[512]         = {0};
static DWORD g_serverWatchSuppressUntil = 0;

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
static volatile LONG g_inVonActiveFlag = 0; /* 0/1 set by worker, read by audio */

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

/* Ring modulator */
typedef struct {
    float phase;
} RingMod;
static inline void ring_init(RingMod* r)
{
    r->phase = 0.0f;
}
static inline void ring_mix(RingMod* r, float* x, int n, float fs, float hz, float mix)
{
    if (mix <= 0.0001f)
        return;
    float dphi = 2.0f * (float)M_PI * hz / fs;
    for (int i = 0; i < n; ++i) {
        float m = sinf(r->phase);
        r->phase += dphi;
        if (r->phase > 2.0f * (float)M_PI)
            r->phase -= 2.0f * (float)M_PI;
        x[i] = (1.0f - mix) * x[i] + mix * (x[i] * m);
    }
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

/* Radio state per talker. Accessed ONLY from the audio callback thread;
   no lock is needed — allocations are also single-threaded (audio callback). */
typedef struct {
    anyID     id;
    int       have;
    Biquad    lp, hp;     /* voice filters */
    Biquad    lp_n, hp_n; /* static filters */
    PinkNoise pink;
    RingMod   rm;
    int       squelchLeft;
    float     squelchAmp;
    int       dropLeft;
    float     dropAtten;
} RadioState;

static RadioState g_radioStates[4096];
static size_t     g_radioCount = 0;

static int cmp_radio_state_by_id(const void* a, const void* b)
{
    return (int)((const RadioState*)a)->id - (int)((const RadioState*)b)->id;
}

/* Lock-free binary search; sort-on-insert keeps the array ordered. */
static RadioState* get_radio_state(anyID id)
{
    /* Binary search for existing entry (audio-thread only — no lock needed). */
    size_t lo = 0, hi = g_radioCount;
    while (lo < hi) {
        const size_t mid    = lo + (hi - lo) / 2;
        const anyID  mid_id = g_radioStates[mid].id;
        if (mid_id == id)
            return &g_radioStates[mid];
        if (mid_id < id)
            lo = mid + 1;
        else
            hi = mid;
    }
    /* Not found — allocate a new entry. */
    if (g_radioCount >= sizeof(g_radioStates) / sizeof(g_radioStates[0]))
        return &g_radioStates[0]; /* array full, reuse slot 0 as a dummy */
    RadioState* s = &g_radioStates[g_radioCount++];
    memset(s, 0, sizeof(*s));
    s->id   = id;
    s->have = 1;
    biquad_calc_lowpass(&s->lp, TS_SAMPLE_RATE, RADIO_LP_HZ, RADIO_LP_Q);
    biquad_calc_highpass(&s->hp, TS_SAMPLE_RATE, RADIO_HP_HZ, RADIO_HP_Q);
    biquad_calc_lowpass(&s->lp_n, TS_SAMPLE_RATE, RADIO_LP_HZ, RADIO_LP_Q);
    biquad_calc_highpass(&s->hp_n, TS_SAMPLE_RATE, RADIO_HP_HZ, RADIO_HP_Q);
    pink_init(&s->pink, (unsigned)id * 2654435761u);
    ring_init(&s->rm);
    s->dropAtten = 1.0f;
    /* Re-sort so future binary searches remain correct. */
    if (g_radioCount > 1)
        qsort(g_radioStates, g_radioCount, sizeof(RadioState), cmp_radio_state_by_id);
    /* After sort, find and return the newly inserted entry. */
    lo = 0; hi = g_radioCount;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (g_radioStates[mid].id == id) return &g_radioStates[mid];
        if (g_radioStates[mid].id < id)  lo = mid + 1;
        else                             hi = mid;
    }
    return &g_radioStates[0]; /* unreachable */
}

static inline const VonSnapshot* get_active_von(void)
{
    LONG idx = InterlockedCompareExchange(&g_vonActiveIdx, 0, 0);
    return &g_VonSnap[(idx & 1)];
}
static inline const RadioSnapshot* get_active_radio(void)
{
    LONG idx = InterlockedCompareExchange(&g_radioActiveIdx, 0, 0);
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
   DIRECT muffle state (per talker)
--------------------------------------------------------------------------- */
typedef struct {
    anyID  id;
    int    have;
    Biquad lpL[MUFFLE_STAGES];
    Biquad lpR[MUFFLE_STAGES];
    Biquad lpM[MUFFLE_STAGES];
    float  lastFc;

    // NEW for smoothing
    float smoothedL;
    float smoothedR;

    // Behind effect filters
    Biquad behindLpL[2];
    Biquad behindLpR[2];
    Biquad behindLpM[2];
    float  lastBehindFc;

    // Babbel effect filters (for foreign language)
    Biquad babbelLp1L;
    Biquad babbelLp1R;
    Biquad babbelLp1M;
    Biquad babbelLp2L;
    Biquad babbelLp2R;
    Biquad babbelLp2M;
    float  babbelPhase;
} DirectState;


static CRITICAL_SECTION g_directLock; /* guards insert-only; lookup is lock-free */
static DirectState      g_directStates[4096];
static size_t           g_directCount = 0;

static int cmp_direct_state_by_id(const void* a, const void* b)
{
    return (int)((const DirectState*)a)->id - (int)((const DirectState*)b)->id;
}

/* Map positive occlusion dB (e.g. 0..24) -> cutoff Hz. 0dB→8k, 24dB→1k.
   Tuned anchors: 12dB ≈ 4.5k, 18dB ≈ 2.7k. */
static inline float occlusion_db_to_fc(float occDbPos)
{
    float       t     = clampf(occDbPos / 24.0f, 0.0f, 1.0f);
    const float fc_hi = 8000.0f;
    const float fc_lo = 1000.0f;
    return fc_hi + (fc_lo - fc_hi) * t; /* linear lerp */
}

/* Map behind intensity (0..1) to cutoff Hz for "behind head" effect.
   0 = no effect (8kHz), 1 = fully behind (3kHz). */
static inline float behind_intensity_to_fc(float intensity)
{
    float       t     = clampf(intensity, 0.0f, 1.0f);
    const float fc_hi = 8000.0f;
    const float fc_lo = 3000.0f;
    return fc_hi + (fc_lo - fc_hi) * t; /* linear lerp */
}

static DirectState* get_direct_state(anyID id)
{
    /* Lock-free binary search for the common case (entry already exists).
       smoothedL/R written by worker, read by audio callback — aligned float
       access is naturally atomic on x86/x64, so no torn reads occur. */
    size_t lo = 0, hi = g_directCount;
    while (lo < hi) {
        const size_t mid    = lo + (hi - lo) / 2;
        const anyID  mid_id = g_directStates[mid].id;
        if (mid_id == id)
            return &g_directStates[mid];
        if (mid_id < id)
            lo = mid + 1;
        else
            hi = mid;
    }

    /* Entry not found — insert under lock (rare: once per new client). */
    EnterCriticalSection(&g_directLock);
    /* Re-check after acquiring lock to avoid a double-insert race. */
    lo = 0; hi = g_directCount;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (g_directStates[mid].id == id) {
            LeaveCriticalSection(&g_directLock);
            return &g_directStates[mid];
        }
        if (g_directStates[mid].id < id) lo = mid + 1;
        else                             hi = mid;
    }
    if (g_directCount >= sizeof(g_directStates) / sizeof(g_directStates[0])) {
        LeaveCriticalSection(&g_directLock);
        return &g_directStates[0]; /* array full */
    }
    DirectState* s = &g_directStates[g_directCount++];
    memset(s, 0, sizeof(*s));
    s->id        = id;
    s->have      = 1;
    s->smoothedL = 0.0f;
    s->smoothedR = 0.0f;
    for (int i = 0; i < MUFFLE_STAGES; ++i) {
        biquad_calc_lowpass(&s->lpL[i], TS_SAMPLE_RATE, 8000.0f, 0.9f);
        biquad_calc_lowpass(&s->lpR[i], TS_SAMPLE_RATE, 8000.0f, 0.9f);
        biquad_calc_lowpass(&s->lpM[i], TS_SAMPLE_RATE, 8000.0f, 0.9f);
    }
    s->lastFc = 8000.0f;
    for (int i = 0; i < 2; ++i) {
        biquad_calc_lowpass(&s->behindLpL[i], TS_SAMPLE_RATE, 8000.0f, 0.7f);
        biquad_calc_lowpass(&s->behindLpR[i], TS_SAMPLE_RATE, 8000.0f, 0.7f);
        biquad_calc_lowpass(&s->behindLpM[i], TS_SAMPLE_RATE, 8000.0f, 0.7f);
    }
    s->lastBehindFc = 8000.0f;
    biquad_calc_lowpass(&s->babbelLp1L, TS_SAMPLE_RATE, BABBEL_LP1_FREQ, BABBEL_LP_Q);
    biquad_calc_lowpass(&s->babbelLp1R, TS_SAMPLE_RATE, BABBEL_LP1_FREQ, BABBEL_LP_Q);
    biquad_calc_lowpass(&s->babbelLp1M, TS_SAMPLE_RATE, BABBEL_LP1_FREQ, BABBEL_LP_Q);
    biquad_calc_lowpass(&s->babbelLp2L, TS_SAMPLE_RATE, BABBEL_LP2_FREQ, BABBEL_LP_Q);
    biquad_calc_lowpass(&s->babbelLp2R, TS_SAMPLE_RATE, BABBEL_LP2_FREQ, BABBEL_LP_Q);
    biquad_calc_lowpass(&s->babbelLp2M, TS_SAMPLE_RATE, BABBEL_LP2_FREQ, BABBEL_LP_Q);
    s->babbelPhase = 0.0f;
    /* Re-sort so future searches remain correct. */
    if (g_directCount > 1)
        qsort(g_directStates, g_directCount, sizeof(DirectState), cmp_direct_state_by_id);
    LeaveCriticalSection(&g_directLock);
    /* Binary-search for the newly inserted entry (its position may have shifted). */
    lo = 0; hi = g_directCount;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (g_directStates[mid].id == id) return &g_directStates[mid];
        if (g_directStates[mid].id < id)  lo = mid + 1;
        else                              hi = mid;
    }
    return &g_directStates[0]; /* unreachable */
}

static inline float smooth_gain(float current, float target)
{
    /* alpha = 0.35 at 200ms worker-tick cadence gives ~80% convergence within 600ms.
       The old value (0.05) meant a player entering range was barely audible for >2s. */
    const float alpha = 0.35f;
    return current + alpha * (target - current);
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
            float chaosVar = 1.0f + (chaos * 0.5f * sinf(ds->babbelPhase * 0.07f));
            float mod      = cosf(ds->babbelPhase * 1.4f * chaosVar);

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
            float chaosVar = 1.0f + (chaos * 0.5f * sinf(ds->babbelPhase * 0.07f));
            float mod      = cosf(ds->babbelPhase * 1.4f * chaosVar);

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

/* Shared DIRECT-path DSP: gain, muffle LP, behind-head LP, babbel distortion.
   Called from onEditPostProcessVoiceDataEvent for both plain DIRECT talkers and
   radio talkers whose frequency does not match the local player's radio. */
static void apply_direct_dsp(anyID clientID, const VonEntry* e, short* samples, int sampleCount, int channels)
{
    DirectState* ds = get_direct_state(clientID);

    const float lGain = ds->smoothedL;
    const float rGain = ds->smoothedR;

    const float occDb = (e->muffledDb < 0.0f) ? -e->muffledDb : 0.0f;
    const int   doMuf = (occDb > 0.1f) ? 1 : 0;

    const float behindInt   = clampf(e->behindIntensity, 0.0f, 1.0f);
    const int   doBehind    = (behindInt > 0.05f) ? 1 : 0;
    float       behindAtten = 1.0f;
    if (doBehind)
        behindAtten = 1.0f - (behindInt * 0.29f);

    if (channels <= 1) {
        const float mGain = 0.5f * (lGain + rGain);

        if (!e->sameLanguage) {
            float* floatBuf = (float*)alloca(sampleCount * sizeof(float));
            for (int i = 0; i < sampleCount; ++i)
                floatBuf[i] = ((float)samples[i] / 32768.0f) * mGain;
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
                samples[i] = clamp_i16(x * 32767.0f);
            }
        } else {
            for (int i = 0; i < sampleCount; ++i) {
                float x = ((float)samples[i] / 32768.0f) * mGain;
                if (doMuf)
                    for (int s = 0; s < MUFFLE_STAGES; ++s)
                        x = biquad_tick(&ds->lpM[s], x);
                if (doBehind) {
                    for (int s = 0; s < 2; ++s)
                        x = biquad_tick(&ds->behindLpM[s], x);
                    x *= behindAtten;
                }
                samples[i] = clamp_i16(x * 32767.0f);
            }
        }
    } else {
        if (!e->sameLanguage) {
            /* Only L+R are processed; allocate exactly 2 floats per sample. */
            float* floatBuf = (float*)alloca(sampleCount * 2 * sizeof(float));
            for (int i = 0; i < sampleCount; ++i) {
                const int idx       = i * channels;
                floatBuf[i * 2 + 0] = ((float)samples[idx + 0] / 32768.0f) * lGain;
                floatBuf[i * 2 + 1] = ((float)samples[idx + 1] / 32768.0f) * rGain;
            }
            apply_babbel_effect(ds, floatBuf, sampleCount * 2, 0);
            for (int i = 0; i < sampleCount; ++i) {
                float     L          = floatBuf[i * 2 + 0];
                float     R          = floatBuf[i * 2 + 1];
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
                const int   idx      = i * channels;
                samples[idx + 0]     = clamp_i16(L * 32767.0f);
                samples[idx + 1]     = clamp_i16(R * 32767.0f);
                const short monoFill = (short)((samples[idx + 0] + samples[idx + 1]) / 2);
                for (int ch = 2; ch < channels; ++ch)
                    samples[idx + ch] = monoFill;
            }
        } else {
            for (int i = 0; i < sampleCount; ++i) {
                const int idx = i * channels;
                float     L   = ((float)samples[idx + 0] / 32768.0f) * lGain;
                float     R   = ((float)samples[idx + 1] / 32768.0f) * rGain;
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
                samples[idx + 0]     = clamp_i16(L * 32767.0f);
                samples[idx + 1]     = clamp_i16(R * 32767.0f);
                const short monoFill = (short)((samples[idx + 0] + samples[idx + 1]) / 2);
                for (int ch = 2; ch < channels; ++ch)
                    samples[idx + ch] = monoFill;
            }
        }
    }
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
            e.type = (v->valueint == 1) ? VON_RADIO : VON_DIRECT;
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

static void apply_mic_state(uint64 sch)
{
    if (!sch)
        return;

    int wantActive = g_IsTransmitting ? 1 : 0;

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
    if ((DWORD)GetTickCount() < g_serverWatchSuppressUntil)
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


/* Channel move with backoff handled by worker */
static DWORD g_nextMoveTryTick = 0;
static DWORD g_moveBackoffMs   = MOVE_BACKOFF_MIN_MS;

static void try_ensure_move(uint64 sch)
{
    if (!g_SD_have || !g_SD_inGame || !g_SD_chanName[0] || !sch)
        return;

    DWORD now = GetTickCount();
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

    DWORD now = GetTickCount();
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
   Worker thread: file reloads + moves + mic state
--------------------------------------------------------------------------- */
static HANDLE        g_workerThread = NULL;
static volatile LONG g_workerQuit   = 0;

static DWORD g_nextVonReloadTick    = 0;
static DWORD g_nextRadioReloadTick  = 0;
static DWORD g_nextServerReloadTick = 0;

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

/* Mute a client via TeamSpeak API */
static void mute_client(uint64 sch, anyID clientID)
{
    if (!sch || clientID == 0)
        return;
        
    ClientApplyState* state = get_client_state(clientID);
    if (!state || state->isMutedByPlugin)
        return; /* already muted by us */
    
    anyID clientArray[2] = {clientID, 0}; /* null-terminated array */
    unsigned int err = ts3Functions.requestMuteClients(sch, clientArray, NULL);
    
    if (err == ERROR_ok) {
        state->isMutedByPlugin = 1;
        logf("[CRF] Muted client %u (out of range)\n", (unsigned)clientID);
    } else {
        logf("[CRF] Failed to mute client %u, error %u\n", (unsigned)clientID, err);
    }
}

/* Unmute a client via TeamSpeak API */
static void unmute_client(uint64 sch, anyID clientID)
{
    if (!sch || clientID == 0)
        return;
        
    ClientApplyState* state = get_client_state(clientID);
    if (!state || !state->isMutedByPlugin)
        return; /* not muted by us */
    
    anyID clientArray[2] = {clientID, 0}; /* null-terminated array */
    unsigned int err = ts3Functions.requestUnmuteClients(sch, clientArray, NULL);
    
    if (err == ERROR_ok) {
        state->isMutedByPlugin = 0;
        logf("[CRF] Unmuted client %u (back in range)\n", (unsigned)clientID);
    } else {
        logf("[CRF] Failed to unmute client %u, error %u\n", (unsigned)clientID, err);
    }
}

static const VonEntry* find_entry(anyID clientID)
{
    const VonSnapshot* snap = get_active_von();
    if (!snap || !snap->loaded || snap->count == 0)
        return NULL;

    /* Binary search — entries are sorted by id in load_von_into(). */
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

    for (int i = 0; clients[i]; ++i)
    {
        anyID cid = clients[i];
        if (cid == myID)
            continue;

        const VonEntry* e = find_entry(cid);
        ClientApplyState* st = get_client_state(cid);
        if (!st)
            continue;

        /* Client has no VON entry → OUT OF RANGE → should be muted */
        int shouldMute = (!e);

        /* ---- MUTE ---- */
        if (shouldMute && !st->isMutedByPlugin) 
        {
            anyID arr[2] = { cid, 0 };
            unsigned int err = ts3Functions.requestMuteClients(sch, arr, NULL);
            if (err == ERROR_ok) {
                st->isMutedByPlugin = 1;
                logf("[CRF] Muted client %u (no VON data)\n", (unsigned)cid);
            }
        }

        /* ---- UNMUTE ---- */
        else if (!shouldMute && st->isMutedByPlugin)
        {
            anyID arr[2] = { cid, 0 };
            unsigned int err = ts3Functions.requestUnmuteClients(sch, arr, NULL);
            if (err == ERROR_ok) {
                st->isMutedByPlugin = 0;
                logf("[CRF] Unmuted client %u (VON active)\n", (unsigned)cid);
            }
        }
    }

    ts3Functions.freeMemory(clients);
}

static void unmute_all_clients(uint64 sch)
{
    if (!sch)
        return;

    anyID myID;
    if (ts3Functions.getClientID(sch, &myID) != ERROR_ok)
        return;

    /* Use channel-scoped list to avoid unmuting the entire server. */
    uint64 myChannel;
    if (ts3Functions.getChannelOfClient(sch, myID, &myChannel) != ERROR_ok)
        return;

    anyID* clients = NULL;
    if (ts3Functions.getChannelClientList(sch, myChannel, &clients) != ERROR_ok || !clients)
        return;

    for (int i = 0; clients[i]; ++i) {
        anyID cid = clients[i];
        if (cid == myID)
            continue; // don't touch ourselves

        anyID        arr[2] = {cid, 0}; // null-terminated array
        unsigned int err    = ts3Functions.requestUnmuteClients(sch, arr, NULL);
        if (err == ERROR_ok) {
            logf("[CRF] Unmuted client %u\n", (unsigned)cid);
        } else {
            logf("[CRF] Failed to unmute client %u, error %u\n", (unsigned)cid, err);
        }
    }

    ts3Functions.freeMemory(clients);
}

static void update_direct_states_in_worker(void)
{
    const VonSnapshot* snap = get_active_von();
    if (!snap || !snap->loaded)
        return;

    for (size_t i = 0; i < snap->count; ++i) {
        const VonEntry* e = &snap->entries[i];

        /* Skip radio-only entries */
        if (e->type == VON_RADIO) {
            /* Check if this should fallback to direct */
            const RadioSnapshot* rSnap   = get_active_radio();
            int                  matched = 0;

            if (rSnap && rSnap->loaded) {
                for (size_t j = 0; j < rSnap->count; ++j) {
                    const RadioLocal* r = &rSnap->list[j];
                    if (strcmp(r->freq, e->txFreq) != 0)
                        continue;
                    if (!(r->timeDev == INT_MIN || e->txTimeDev == INT_MIN || r->timeDev == e->txTimeDev))
                        continue;
                    int rf = (r->faction[0] && strcmp(r->faction, "0") != 0);
                    int vf = (e->txFaction[0] && strcmp(e->txFaction, "0") != 0);
                    if (rf && vf && strcmp(r->faction, e->txFaction) != 0)
                        continue;
                    matched = 1;
                    break;
                }
            }

            /* If matched, skip (pure radio, no direct processing needed) */
            if (matched)
                continue;
        }

        /* Get or create DirectState for this client */
        DirectState* ds = get_direct_state(e->id);
        if (!ds)
            continue;

        /* Calculate target gains */
        const float lG        = clampf(e->leftGain, 0.0f, 2.0f);
        const float rG        = clampf(e->rightGain, 0.0f, 2.0f);
        float       avg       = 0.5f * (lG + rG);
        float       yellBoost = (avg > 1.0f) ? clampf(1.0f + 0.15f * (avg - 1.0f), 1.0f, 1.5f) : 1.0f;

        const float lTarget = clampf(lG * yellBoost * DIRECT_GAIN_MUL, 0.0f, 2.0f);
        const float rTarget = clampf(rG * yellBoost * DIRECT_GAIN_MUL, 0.0f, 2.0f);

        /* Smooth gain updates (happens every worker tick, not just when audio flows) */
        ds->smoothedL = smooth_gain(ds->smoothedL, lTarget);
        ds->smoothedR = smooth_gain(ds->smoothedR, rTarget);

        /* Update muffle filters if needed */
        const float occDb = (e->muffledDb < 0.0f) ? -e->muffledDb : 0.0f;
        if (occDb > 0.1f) {
            float fc = occlusion_db_to_fc(occDb);
            if (fabsf(fc - ds->lastFc) > 10.0f) {
                for (int k = 0; k < MUFFLE_STAGES; ++k) {
                    biquad_update_lowpass(&ds->lpL[k], TS_SAMPLE_RATE, fc, 0.9f);
                    biquad_update_lowpass(&ds->lpR[k], TS_SAMPLE_RATE, fc, 0.9f);
                    biquad_update_lowpass(&ds->lpM[k], TS_SAMPLE_RATE, fc, 0.9f);
                }
                ds->lastFc = fc;
            }
        }

        /* Update behind filters if needed */
        const float behindInt = clampf(e->behindIntensity, 0.0f, 1.0f);
        if (behindInt > 0.05f) {
            float behindFc = behind_intensity_to_fc(behindInt);
            if (fabsf(behindFc - ds->lastBehindFc) > 10.0f) {
                for (int k = 0; k < 2; ++k) {
                    biquad_update_lowpass(&ds->behindLpL[k], TS_SAMPLE_RATE, behindFc, 0.7f);
                    biquad_update_lowpass(&ds->behindLpR[k], TS_SAMPLE_RATE, behindFc, 0.7f);
                    biquad_update_lowpass(&ds->behindLpM[k], TS_SAMPLE_RATE, behindFc, 0.7f);
                }
                ds->lastBehindFc = behindFc;
            }
        }
    }
}

static DWORD WINAPI worker_main(LPVOID param)
{
    (void)param;

    while (InterlockedCompareExchange(&g_workerQuit, 0, 0) == 0) {
        DWORD  now = GetTickCount();
        uint64 sch = ts3Functions.getCurrentServerConnectionHandlerID();

        /* ---------------- ServerData reload/write ---------------- */
        if (now >= g_nextServerReloadTick) {
            g_nextServerReloadTick = now + SERVERDATA_RELOAD_MS;
            if ((DWORD)now >= g_serverWatchSuppressUntil) {
                FILETIME wt;
                if (g_ServerPath[0] && file_modified_since_last(g_ServerPath, &g_serverWatch, &wt))
                    read_serverdata_from_disk();
            }
            write_serverdata_if_changed(sch);
        }

        /* ---------------- Auto-connect if IP changed ---------------- */
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



        /* ---------------- Channel moves ---------------- */
        try_ensure_move(sch);
        try_return_to_previous(sch);

        /* ---------------- Mic toggle ---------------- */
        apply_mic_state(sch);

        /* ---------------- VONData + mute enforcement ---------------- */
        if (now >= g_nextVonReloadTick) {
            g_nextVonReloadTick = now + VONDATA_RELOAD_MS;

            if (g_VonPath[0]) {
                FILETIME wt;
                if (file_modified_since_last(g_VonPath, &g_vonDataWatch, &wt)) {
                    LONG readIdx  = InterlockedCompareExchange(&g_vonActiveIdx, 0, 0);
                    LONG writeIdx = (readIdx ^ 1) & 1; /* inactive buffer */
                    if (load_von_into(&g_VonSnap[writeIdx])) {
                        InterlockedExchange(&g_vonActiveIdx, writeIdx); /* publish */
                    }
                }
            }

            /* NEW: Update all DirectStates continuously */
            update_direct_states_in_worker();

            if (sch) {
                if (g_SD_inGame)
                    apply_proximity_muting(sch);
                else
                    unmute_all_clients(sch);
            }
        }

        /* ---------------- RadioData reload ---------------- */
        if (now >= g_nextRadioReloadTick) {
            g_nextRadioReloadTick = now + RADIODATA_RELOAD_MS;
            if (g_RadioPath[0]) {
                FILETIME wt;
                if (file_modified_since_last(g_RadioPath, &g_radioWatch, &wt)) {
                    LONG readIdx  = InterlockedCompareExchange(&g_radioActiveIdx, 0, 0);
                    LONG writeIdx = (readIdx ^ 1) & 1;
                    if (load_radio_into(&g_RadioSnap[writeIdx])) {
                        InterlockedExchange(&g_radioActiveIdx, writeIdx);
                    }
                }
            }
        }

        /* ---------------- Sleep with slices ---------------- */
        for (int i = 0; i < WORKER_TICK_MS; i += 50) {
            if (InterlockedCompareExchange(&g_workerQuit, 0, 0) != 0)
                break;
            Sleep(50);
        }
    }
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
    return "2.0.1";
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

    buildVONDataPath(g_VonPath, sizeof(g_VonPath));
    buildServerJsonPath(g_ServerPath, sizeof(g_ServerPath));
    buildRadioJsonPath(g_RadioPath, sizeof(g_RadioPath));

    if (g_VonPath[0])
        ensureParentDirExists(g_VonPath);
    if (g_ServerPath[0])
        ensureParentDirExists(g_ServerPath);
    if (g_RadioPath[0])
        ensureParentDirExists(g_RadioPath);

    g_LastCount                = 0;
    memset(g_Last, 0, sizeof(g_Last));  /* Clear client state tracking */
    g_vonDataWatch.loadedOnce  = 0;
    g_serverWatch.loadedOnce   = 0;
    g_radioWatch.loadedOnce    = 0;
    g_SD_have                  = 0;
    g_SD_inGame                = 0;
    g_SD_chanName[0]           = '\0';
    g_SD_chanPass[0]           = '\0';
    g_serverWatchSuppressUntil = 0;
    g_lastServerWritten.valid  = 0;
    InterlockedExchange(&g_inVonActiveFlag, 0);

       g_directCount = 0;

    /* Build initial snapshots so audio has something valid to read */
    {
        /* VON */
        LONG w = 0;
        memset(&g_VonSnap[w], 0, sizeof(g_VonSnap[w]));
        load_von_into(&g_VonSnap[w]); /* best effort */
        InterlockedExchange(&g_vonActiveIdx, w);

        /* Radio */
        w = 0;
        memset(&g_RadioSnap[w], 0, sizeof(g_RadioSnap[w]));
        load_radio_into(&g_RadioSnap[w]); /* best effort */
        InterlockedExchange(&g_radioActiveIdx, w);
    }

    if (ts3Functions.getCurrentServerConnectionHandlerID()) {
        apply_proximity_muting(ts3Functions.getCurrentServerConnectionHandlerID());
    }
    read_serverdata_from_disk();
    write_serverdata_if_changed(0);

    InterlockedExchange(&g_workerQuit, 0);
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
        
        /* Unmute all clients that were muted by the plugin */
        for (size_t i = 0; i < g_LastCount; ++i) {
            if (g_Last[i].isMutedByPlugin) {
                unmute_client(ts3Functions.getCurrentServerConnectionHandlerID(), g_Last[i].id);
            }
        }
    }

    InterlockedExchange(&g_workerQuit, 1);
    if (g_workerThread) {
        WaitForSingleObject(g_workerThread, 3000);
        CloseHandle(g_workerThread);
        g_workerThread = NULL;
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
}

/* ---------------------------------------------------------------------------
   Events
--------------------------------------------------------------------------- */
PLUGINS_EXPORTDLL void ts3plugin_onConnectStatusChangeEvent(uint64 sch, int newStatus, unsigned int errorNumber)
{
    (void)errorNumber;
    if (newStatus == STATUS_CONNECTION_ESTABLISHED) {
        apply_proximity_muting(sch);
        g_nextVonReloadTick    = 0;
        g_nextRadioReloadTick  = 0;
        g_nextServerReloadTick = 0;
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
    g_LastCount  = 0;
}
PLUGINS_EXPORTDLL void ts3plugin_onClientMoveEvent(uint64 sch, anyID clientID, uint64 oldCh, uint64 newCh, int visibility, const char* moveMessage)
{
    (void)oldCh;
    (void)newCh;
    (void)moveMessage;
    
    /* Clean up muting state for clients who leave visibility */
    if (visibility == LEAVE_VISIBILITY) {
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

    DWORD age = GetTickCount() - e->lastUpdateTick;
    if (age > 250) { /* give a bit more grace than 250ms */
        silence_samples(samples, sampleCount, channels);
        return;
    }

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