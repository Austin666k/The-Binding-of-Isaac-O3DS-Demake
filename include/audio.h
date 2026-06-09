/*
 * audio.h - Binding of Isaac 3DS
 *
 * CSND-based audio system.  Uses the csnd:SND service which does NOT
 * require DSP firmware - works on every 3DS without any external setup.
 *
 *   - SFX (channels 9-15, round-robin): one-shot 16-bit PCM
 *   - Music (channel 8): looping 16-bit PCM, loaded into linear memory
 *   - Soft volume fade for music tracks via music_update()
 *
 * If csndInit() fails (extremely rare), the system gracefully falls back
 * to silent mode - all audio API calls become no-ops.
 */
#ifndef AUDIO_H
#define AUDIO_H

#include <3ds.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ---------- Audio System Status ---------- */
typedef enum {
    AUDIO_STATUS_NOT_INIT = 0,        /* audio_init() not called yet */
    AUDIO_STATUS_OK,                  /* CSND audio working */
    AUDIO_STATUS_DISABLED_USER,       /* user set audio_enabled=0 */
    AUDIO_STATUS_DISABLED_INIT_FAIL,  /* csndInit() returned error */
    AUDIO_STATUS_DISABLED_RUNTIME     /* runtime error */
} AudioStatus;

/* ---------- Sound Effect IDs ---------- */
typedef enum {
    SFX_SHOOT = 0,
    SFX_HIT,
    SFX_ENEMY_DEATH,
    SFX_HURT,
    SFX_PICKUP,
    SFX_DOOR,
    SFX_BOSS,
    SFX_ROOM_CLEAR,
    SFX_TEAR_FIRE1,
    SFX_TEAR_FIRE2,
    SFX_TEAR_BLOCK,
    SFX_HURT_GRUNT,
    SFX_PLAYER_DEATH,
    SFX_COUNT
} SfxId;

/* ---------- Music Track IDs ---------- */
typedef enum {
    MUS_NONE = -1,
    MUS_TITLE = 0,
    MUS_BASEMENT,
    MUS_CAVES,
    MUS_DEPTHS,
    MUS_BOSS,
    MUS_COUNT
} MusicId;

/* ---------- Core API ---------- */

/* Initialize the audio system.
 *   audio_enabled: from config (0 = user disabled, 1 = try to init)
 * Returns 0 on success (audio working), -1 on failure (silent mode). */
int  audio_init(int audio_enabled);

/* Shut down audio system */
void audio_exit(void);

/* Load all sound effects from romfs into linear memory */
int  audio_load_all(void);

/* Free all loaded SFX data (not normally needed - called by audio_exit) */
void audio_free_all(void);

/* Play a sound effect on the next available SFX channel */
void audio_play(SfxId id);

/* Set master volume for SFX (0.0 to 1.0) */
void audio_set_volume(float vol);

/* ---------- Music API ---------- */
void    music_play(MusicId id);
void    music_stop(void);
void    music_fade_out(int ticks);
void    music_crossfade(MusicId new_track, int ticks);
void    music_set_volume(float vol);
void    music_update(void);    /* call once per frame for volume fades */
MusicId music_current(void);

/* ---------- Status / Query API ---------- */
AudioStatus audio_get_status(void);
const char *audio_status_string(void);
int         audio_is_available(void);
void        audio_force_disable(void);

/* ---------- Diagnostic / Debug API ----------
 * These let a debug overlay show on-screen what the audio system is
 * actually doing - vital on real 3DS hardware where there's no stdio
 * console to inspect. */
typedef struct {
    int     init_called;
    int     init_result;            /* csndInit() return (0 = OK) */
    u32     csnd_channels;          /* bitmask from CSND_AcquireSoundChannels */

    int     sfx_load_count;         /* how many SFX successfully loaded */
    int     first_load_err;         /* first WAV_ERR_* (0 = none) */
    int     first_load_err_idx;     /* SfxId of first failed SFX */

    int     play_sfx_calls;         /* total audio_play() calls */
    int     last_play_result;       /* result of last csndPlaySound() (0 = OK, 1 = chn not allowed) */
    int     last_play_status;       /* audio_status at last call */
    int     last_play_chn;
    int     last_play_sfx;

    int     music_load_attempts;
    int     music_load_failures;
    int     last_music_load_err;
    int     last_music_play_result;

    int     test_tone_played;       /* incremented each test_tone() call */
    int     test_tone_result;       /* result of last test tone csndPlaySound */
} AudioDebug;

void audio_debug_get(AudioDebug *out);
void audio_test_tone(void);    /* synthesize+play 1s 440Hz tone on ch 8 */

#endif /* AUDIO_H */
