/*
 * Game audio capture for radio transmissions - see cvon_gameaudio.h.
 *
 * Uses WASAPI process loopback (ActivateAudioInterfaceAsync on VAD\Process_Loopback). It is looked up at
 * run time, so the plugin still loads on Windows versions without it; there the feature is just off.
 * The activation structures are declared here rather than taken from audioclientactivationparams.h so
 * the plugin builds with older Windows SDKs too.
 *
 * Must be compiled as C (COM is used through the C vtable macros).
 */
#define COBJMACROS
#define _CRT_SECURE_NO_WARNINGS

#include <Windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <tlhelp32.h>
#include <string.h>
#include <wchar.h>

#include "cvon_gameaudio.h"

/* ---------------------------------------------------------------------------
   Process loopback activation (audioclientactivationparams.h, Windows 10 2004+)
--------------------------------------------------------------------------- */
#define CVON_ACTIVATION_TYPE_PROCESS_LOOPBACK 1
#define CVON_LOOPBACK_INCLUDE_TARGET_PROCESS_TREE 0
#define CVON_VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK L"VAD\\Process_Loopback"

typedef struct {
    DWORD TargetProcessId;
    int   ProcessLoopbackMode;
} CVON_PROCESS_LOOPBACK_PARAMS;

typedef struct {
    int                          ActivationType;
    CVON_PROCESS_LOOPBACK_PARAMS ProcessLoopbackParams;
} CVON_ACTIVATION_PARAMS;

typedef HRESULT(WINAPI* PFN_ActivateAudioInterfaceAsync)(LPCWSTR, REFIID, PROPVARIANT*, IActivateAudioInterfaceCompletionHandler*, IActivateAudioInterfaceAsyncOperation**);

/* Own copies of the interface IDs: not every one of them is in uuid.lib */
static const IID CVON_IID_IUnknown     = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID CVON_IID_IAgileObject = {0x94EA2B94, 0xE9CC, 0x49E0, {0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90}};
static const IID CVON_IID_IActivateAudioInterfaceCompletionHandler = {0x41D949AB, 0x9862, 0x444A, {0x80, 0xF6, 0xC2, 0x61, 0x33, 0x4D, 0xA5, 0xEB}};
static const IID CVON_IID_IAudioClient        = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
static const IID CVON_IID_IAudioCaptureClient = {0xC8ADBD64, 0xE71E, 0x48A0, {0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17}};

static int iid_equal(REFIID a, const IID* b)
{
    return memcmp(a, b, sizeof(IID)) == 0;
}

/* ---------------------------------------------------------------------------
   Completion handler: signals an event when activation finishes. Agile (IAgileObject) so it may be
   called back on any thread. Heap-allocated and reference counted: the async operation can hold on to
   it after we have stopped waiting.
--------------------------------------------------------------------------- */
typedef struct {
    IActivateAudioInterfaceCompletionHandler iface;
    volatile LONG                            ref;
    HANDLE                                   done;
} ActivateHandler;

static HRESULT STDMETHODCALLTYPE handler_QueryInterface(IActivateAudioInterfaceCompletionHandler* This, REFIID riid, void** ppv)
{
    if (!ppv)
        return E_POINTER;
    if (iid_equal(riid, &CVON_IID_IUnknown) || iid_equal(riid, &CVON_IID_IAgileObject) || iid_equal(riid, &CVON_IID_IActivateAudioInterfaceCompletionHandler)) {
        *ppv = This;
        InterlockedIncrement(&((ActivateHandler*)This)->ref);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE handler_AddRef(IActivateAudioInterfaceCompletionHandler* This)
{
    return (ULONG)InterlockedIncrement(&((ActivateHandler*)This)->ref);
}

static ULONG STDMETHODCALLTYPE handler_Release(IActivateAudioInterfaceCompletionHandler* This)
{
    ActivateHandler* h   = (ActivateHandler*)This;
    LONG             ref = InterlockedDecrement(&h->ref);
    if (ref == 0) {
        if (h->done)
            CloseHandle(h->done);
        HeapFree(GetProcessHeap(), 0, h);
    }
    return (ULONG)ref;
}

static HRESULT STDMETHODCALLTYPE handler_ActivateCompleted(IActivateAudioInterfaceCompletionHandler* This, IActivateAudioInterfaceAsyncOperation* op)
{
    (void)op;
    SetEvent(((ActivateHandler*)This)->done);
    return S_OK;
}

static IActivateAudioInterfaceCompletionHandlerVtbl g_handlerVtbl = {
    handler_QueryInterface,
    handler_AddRef,
    handler_Release,
    handler_ActivateCompleted,
};

/* ---------------------------------------------------------------------------
   Ring buffer: 48 kHz mono float. One writer (capture thread), one reader (TeamSpeak's capture callback).
--------------------------------------------------------------------------- */
#define RING_LEN 32768u /* ~680 ms, power of two */
#define RING_MASK (RING_LEN - 1u)
/* The reader never lags more than this behind the newest audio. Beyond it (the reader paused, or the two
   clocks drifted) it skips ahead to RING_RESYNC so the game audio stays in step with the voice. */
#define RING_MAX_LAG 4800u  /* 100 ms */
#define RING_RESYNC 1920u   /* 40 ms */

static float            s_ring[RING_LEN];
static unsigned         s_ringW = 0, s_ringR = 0;
static CRITICAL_SECTION s_ringLock;
static int              s_lockReady = 0;

static void ring_push_pcm16_stereo(const short* data, UINT32 frames, int silent, int channels)
{
    EnterCriticalSection(&s_ringLock);
    for (UINT32 i = 0; i < frames; ++i) {
        float x = 0.0f;
        if (!silent) {
            int sum = 0;
            for (int c = 0; c < channels; ++c)
                sum += data[i * channels + c];
            x = (float)sum / (32768.0f * (float)channels);
        }
        s_ring[s_ringW & RING_MASK] = x;
        s_ringW++;
    }
    if (s_ringW - s_ringR > RING_LEN)
        s_ringR = s_ringW - RING_LEN;
    LeaveCriticalSection(&s_ringLock);
}

int gameaudio_read(float* out, int count)
{
    if (!out || count <= 0)
        return 0;
    if (!s_lockReady) {
        for (int i = 0; i < count; ++i)
            out[i] = 0.0f;
        return 0;
    }
    EnterCriticalSection(&s_ringLock);
    unsigned avail = s_ringW - s_ringR;
    if (avail > (unsigned)count + RING_MAX_LAG) {
        s_ringR = s_ringW - ((unsigned)count + RING_RESYNC);
        avail   = s_ringW - s_ringR;
    }
    const int take = (avail < (unsigned)count) ? (int)avail : count;
    for (int i = 0; i < take; ++i)
        out[i] = s_ring[(s_ringR + (unsigned)i) & RING_MASK];
    s_ringR += (unsigned)take;
    LeaveCriticalSection(&s_ringLock);
    for (int i = take; i < count; ++i)
        out[i] = 0.0f;
    return take;
}

void gameaudio_flush(void)
{
    if (!s_lockReady)
        return;
    EnterCriticalSection(&s_ringLock);
    s_ringR = s_ringW;
    LeaveCriticalSection(&s_ringLock);
}

/* ---------------------------------------------------------------------------
   Capture thread
--------------------------------------------------------------------------- */
static HANDLE                          s_thread     = NULL;
static HANDLE                          s_quit       = NULL;
static volatile LONG                   s_running    = 0;
static volatile LONG                   s_pid        = 0;
static volatile LONG                   s_lastError  = 0;
static volatile LONG                   s_supported  = 1;
static volatile LONG                   s_initDone   = 0;
static PFN_ActivateAudioInterfaceAsync s_activate   = NULL;
static HMODULE                         s_mmdevapi   = NULL;

#define CAPTURE_CHANNELS 2
#define CAPTURE_RATE 48000
#define CAPTURE_BUFFER_HNS 200000 /* 20 ms */

/* Activates a process loopback IAudioClient for pid. NULL on failure (s_lastError says why). */
static IAudioClient* activate_loopback(DWORD pid)
{
    CVON_ACTIVATION_PARAMS params;
    memset(&params, 0, sizeof(params));
    params.ActivationType                          = CVON_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId   = pid;
    params.ProcessLoopbackParams.ProcessLoopbackMode = CVON_LOOPBACK_INCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT pv;
    memset(&pv, 0, sizeof(pv));
    pv.vt             = VT_BLOB;
    pv.blob.cbSize    = sizeof(params);
    pv.blob.pBlobData = (BYTE*)&params;

    ActivateHandler* handler = (ActivateHandler*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ActivateHandler));
    if (!handler) {
        InterlockedExchange(&s_lastError, (LONG)E_OUTOFMEMORY);
        return NULL;
    }
    handler->iface.lpVtbl = &g_handlerVtbl;
    handler->ref          = 1;
    handler->done         = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!handler->done) {
        HeapFree(GetProcessHeap(), 0, handler);
        InterlockedExchange(&s_lastError, (LONG)HRESULT_FROM_WIN32(GetLastError()));
        return NULL;
    }

    IActivateAudioInterfaceAsyncOperation* op     = NULL;
    IAudioClient*                          client = NULL;
    HRESULT hr = s_activate(CVON_VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, &CVON_IID_IAudioClient, &pv, &handler->iface, &op);
    if (SUCCEEDED(hr)) {
        HANDLE waits[2] = {s_quit, handler->done};
        DWORD  w        = WaitForMultipleObjects(2, waits, FALSE, 5000);
        if (w == WAIT_OBJECT_0 + 1) {
            HRESULT   hrActivate = E_FAIL;
            IUnknown* unk        = NULL;
            hr = IActivateAudioInterfaceAsyncOperation_GetActivateResult(op, &hrActivate, &unk);
            if (SUCCEEDED(hr))
                hr = hrActivate;
            if (SUCCEEDED(hr) && unk)
                hr = IUnknown_QueryInterface(unk, &CVON_IID_IAudioClient, (void**)&client);
            if (unk)
                IUnknown_Release(unk);
        } else
            hr = (w == WAIT_OBJECT_0) ? E_ABORT : HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    if (op)
        IActivateAudioInterfaceAsyncOperation_Release(op);
    handler_Release(&handler->iface);

    if (FAILED(hr) || !client) {
        if (client)
            IAudioClient_Release(client);
        InterlockedExchange(&s_lastError, (LONG)(FAILED(hr) ? hr : E_FAIL));
        return NULL;
    }
    return client;
}

/* Activates and initialises the capture: 48 kHz 16-bit stereo, which the audio engine converts to from
   whatever the game plays. Retries once without the explicit conversion flags. */
static IAudioClient* open_capture(DWORD pid, HANDLE samplesReady)
{
    WAVEFORMATEX fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.wFormatTag      = WAVE_FORMAT_PCM;
    fmt.nChannels       = CAPTURE_CHANNELS;
    fmt.nSamplesPerSec  = CAPTURE_RATE;
    fmt.wBitsPerSample  = 16;
    fmt.nBlockAlign     = (WORD)(fmt.nChannels * fmt.wBitsPerSample / 8);
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    const DWORD baseFlags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    const DWORD attempts[2] = {baseFlags | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY, baseFlags};

    for (int i = 0; i < 2; ++i) {
        if (WaitForSingleObject(s_quit, 0) == WAIT_OBJECT_0)
            return NULL;
        IAudioClient* client = activate_loopback(pid);
        if (!client)
            return NULL; /* activation itself failed: retrying with other flags won't help */
        HRESULT hr = IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_SHARED, attempts[i], CAPTURE_BUFFER_HNS, 0, &fmt, NULL);
        if (SUCCEEDED(hr))
            hr = IAudioClient_SetEventHandle(client, samplesReady);
        if (SUCCEEDED(hr))
            return client;
        InterlockedExchange(&s_lastError, (LONG)hr);
        IAudioClient_Release(client); /* a failed Initialize can't be retried on the same client */
    }
    return NULL;
}

static DWORD WINAPI capture_main(LPVOID param)
{
    const DWORD pid = (DWORD)(ULONG_PTR)param;

    const HRESULT hrCo   = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    HANDLE        ready  = CreateEventW(NULL, FALSE, FALSE, NULL);
    IAudioClient* client = ready ? open_capture(pid, ready) : NULL;
    IAudioCaptureClient* capture = NULL;

    HRESULT hr = client ? IAudioClient_GetService(client, &CVON_IID_IAudioCaptureClient, (void**)&capture) : E_FAIL;
    if (client && SUCCEEDED(hr))
        hr = IAudioClient_Start(client);

    if (client && capture && SUCCEEDED(hr)) {
        InterlockedExchange(&s_lastError, 0);
        HANDLE waits[2] = {s_quit, ready};
        for (;;) {
            DWORD w = WaitForMultipleObjects(2, waits, FALSE, 200);
            if (w == WAIT_OBJECT_0 || w == WAIT_FAILED)
                break;

            UINT32 packet = 0;
            hr = IAudioCaptureClient_GetNextPacketSize(capture, &packet);
            while (SUCCEEDED(hr) && packet > 0) {
                BYTE*  data   = NULL;
                UINT32 frames = 0;
                DWORD  flags  = 0;
                hr = IAudioCaptureClient_GetBuffer(capture, &data, &frames, &flags, NULL, NULL);
                if (FAILED(hr))
                    break;
                if (frames > 0)
                    ring_push_pcm16_stereo((const short*)data, frames, (flags & AUDCLNT_BUFFERFLAGS_SILENT) || !data, CAPTURE_CHANNELS);
                IAudioCaptureClient_ReleaseBuffer(capture, frames);
                hr = IAudioCaptureClient_GetNextPacketSize(capture, &packet);
            }
            if (FAILED(hr)) {
                InterlockedExchange(&s_lastError, (LONG)hr); /* device gone, process gone, ... */
                break;
            }
        }
        IAudioClient_Stop(client);
    } else if (client && FAILED(hr))
        InterlockedExchange(&s_lastError, (LONG)hr);

    if (capture)
        IAudioCaptureClient_Release(capture);
    if (client)
        IAudioClient_Release(client);
    if (ready)
        CloseHandle(ready);
    if (SUCCEEDED(hrCo))
        CoUninitialize();

    InterlockedExchange(&s_running, 0);
    return 0;
}

/* ---------------------------------------------------------------------------
   Public API
--------------------------------------------------------------------------- */
void gameaudio_init(void)
{
    if (InterlockedCompareExchange(&s_initDone, 1, 0) != 0)
        return;
    /* The lock lives as long as the DLL (never deleted): TeamSpeak's audio thread may still be inside
       gameaudio_read while the plugin shuts down */
    if (!s_lockReady) {
        InitializeCriticalSection(&s_ringLock);
        s_lockReady = 1;
    }
    gameaudio_flush();
    InterlockedExchange(&s_supported, 1);
    s_quit  = CreateEventW(NULL, TRUE, FALSE, NULL);

    s_mmdevapi = LoadLibraryW(L"Mmdevapi.dll");
    if (s_mmdevapi)
        s_activate = (PFN_ActivateAudioInterfaceAsync)GetProcAddress(s_mmdevapi, "ActivateAudioInterfaceAsync");
    if (!s_activate || !s_quit)
        InterlockedExchange(&s_supported, 0);
}

void gameaudio_shutdown(void)
{
    if (!s_initDone)
        return;
    gameaudio_stop();
    if (s_quit) {
        CloseHandle(s_quit);
        s_quit = NULL;
    }
    s_activate = NULL;
    if (s_mmdevapi) {
        FreeLibrary(s_mmdevapi);
        s_mmdevapi = NULL;
    }
    InterlockedExchange(&s_initDone, 0);
}

int gameaudio_supported(void)
{
    return s_initDone && InterlockedCompareExchange(&s_supported, 0, 0) != 0;
}

int gameaudio_start(DWORD pid)
{
    if (!gameaudio_supported() || !pid)
        return 0;
    if (gameaudio_running())
        return 1;

    /* A previous capture that ended on its own */
    if (s_thread) {
        WaitForSingleObject(s_thread, 2000);
        CloseHandle(s_thread);
        s_thread = NULL;
    }

    ResetEvent(s_quit);
    gameaudio_flush();
    InterlockedExchange(&s_pid, (LONG)pid);
    InterlockedExchange(&s_running, 1);
    s_thread = CreateThread(NULL, 0, capture_main, (LPVOID)(ULONG_PTR)pid, 0, NULL);
    if (!s_thread) {
        InterlockedExchange(&s_running, 0);
        InterlockedExchange(&s_lastError, (LONG)HRESULT_FROM_WIN32(GetLastError()));
        return 0;
    }
    return 1;
}

void gameaudio_stop(void)
{
    if (!s_thread)
        return;
    if (s_quit)
        SetEvent(s_quit);
    WaitForSingleObject(s_thread, 3000);
    CloseHandle(s_thread);
    s_thread = NULL;
    InterlockedExchange(&s_running, 0);
    InterlockedExchange(&s_pid, 0);
    gameaudio_flush();
}

int gameaudio_running(void)
{
    return InterlockedCompareExchange(&s_running, 0, 0) != 0;
}

DWORD gameaudio_pid(void)
{
    return (DWORD)InterlockedCompareExchange(&s_pid, 0, 0);
}

long gameaudio_last_error(void)
{
    return (long)InterlockedCompareExchange(&s_lastError, 0, 0);
}

/* ---------------------------------------------------------------------------
   Finding the game
--------------------------------------------------------------------------- */
static int starts_with_ci(const wchar_t* s, const wchar_t* prefix)
{
    return _wcsnicmp(s, prefix, wcslen(prefix)) == 0;
}

static int contains_ci(const wchar_t* s, const wchar_t* needle)
{
    const size_t n = wcslen(needle);
    for (; *s; ++s)
        if (_wcsnicmp(s, needle, n) == 0)
            return 1;
    return 0;
}

DWORD gameaudio_find_game_process(void)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;

    /* Rank 3: the game (ArmaReforgerSteam.exe, ArmaReforgerSteamDiag.exe, ...). 2: Workbench, whose play
       mode runs inside its own process. 1: a launcher such as ArmaReforger_BE.exe (BattlEye), only if
       nothing better is running; its process tree includes the game it started. */
    DWORD best = 0;
    int   bestRank = 0;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (!starts_with_ci(pe.szExeFile, L"ArmaReforger") || contains_ci(pe.szExeFile, L"Server"))
                continue;
            int rank = 3;
            if (contains_ci(pe.szExeFile, L"_BE") || contains_ci(pe.szExeFile, L"Launcher"))
                rank = 1;
            else if (contains_ci(pe.szExeFile, L"Workbench"))
                rank = 2;
            if (rank > bestRank) {
                best     = pe.th32ProcessID;
                bestRank = rank;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return best;
}

int gameaudio_process_alive(DWORD pid)
{
    if (!pid)
        return 0;
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!h)
        return 0;
    const int alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
}
