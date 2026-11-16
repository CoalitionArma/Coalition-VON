/*
 * Game audio capture for radio transmissions.
 *
 * Records what Arma Reforger itself is playing (Windows per-process loopback capture, Windows 10 2004 or
 * later) into a short buffer at 48 kHz mono. While the player talks on a radio the plugin mixes it into
 * the microphone stream (ts3plugin_onEditCapturedVoiceDataEvent), so gunfire, explosions and vehicles
 * around the sender go out with their voice. TeamSpeak's own audio is a different process and is never
 * captured.
 *
 * All functions are safe to call from any thread. gameaudio_read / gameaudio_flush are cheap enough for
 * TeamSpeak's audio callback; start/stop/find are for the worker thread.
 */
#ifndef CVON_GAMEAUDIO_H
#define CVON_GAMEAUDIO_H

#include <Windows.h>

#ifdef __cplusplus
extern "C" {
#endif

void gameaudio_init(void);
void gameaudio_shutdown(void);

/* 0 when this Windows can't activate audio interfaces asynchronously at all (before Windows 8). Windows
   8 to 10 1909 report 1 but every capture fails to activate, which gameaudio_running then shows. */
int gameaudio_supported(void);

/* Starts capturing the given process (and its child processes) on a background thread. Returns 0 when
   capture can't start at all; the capture can still fail afterwards, which gameaudio_running reports. */
int  gameaudio_start(DWORD pid);
void gameaudio_stop(void);

/* 1 while the capture thread is activating or capturing */
int   gameaudio_running(void);
DWORD gameaudio_pid(void);

/* HRESULT of the last capture failure (0 = none), for logging */
long gameaudio_last_error(void);

/* The running Arma Reforger game, 0 if none. Prefers the game over Workbench (play mode runs inside the
   Workbench process) and never picks the dedicated server. */
DWORD gameaudio_find_game_process(void);
int   gameaudio_process_alive(DWORD pid);

/* Reads up to `count` mono samples (-1..1) of game audio, oldest first; the rest of `out` is zero-filled.
   Returns how many samples were real audio. */
int  gameaudio_read(float* out, int count);
/* Discards everything buffered: the next read starts with audio captured from now on */
void gameaudio_flush(void);

#ifdef __cplusplus
}
#endif

#endif
