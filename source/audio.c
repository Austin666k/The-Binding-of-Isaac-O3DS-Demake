/*
 * audio.c - Binding of Isaac 3DS
 *
 * CSND-based audio system with extensive runtime diagnostics.
 *
 * Why CSND instead of NDSP?
 *   NDSP requires the DSP firmware (dspfirm.cdc) to be present and valid.
 *   On many 3DS systems this firmware is missing or corrupt, and ndspInit()
 *   crashes (Data Abort) before any error code can be returned - no amount
 *   of pre-validation can prevent it.
 *
 *   CSND uses the legacy csnd:SND service directly.  It does NOT require
 *   DSP firmware at all and works on every 3DS console without any setup.
 *
 * Channel allocation:
 *   8       - Music (looping, 16-bit PCM)
 *   9-15    - SFX channels (round-robin assignment)
 *
 * Memory:
 *   - SFX are small (<100KB each, ~250KB total) and stay loaded in linear memory
 *   - Music tracks are large (5-11MB each), only one is loaded at a time;
 *     the previous track buffer is freed when a new track starts
 *
 * Diagnostics:
 *   - csndInit() return code, csndChannels mask, load counts and last play
 *     results are exposed via audio_debug_get() so a debug overlay can show
 *     them on real hardware (where there is no stdio console).
 */

#include "audio.h"

#define MUSIC_CHANNEL   8
#define SFX_CHN_START   9
#define SFX_CHN_END     16   /* exclusive - channels 9..15 inclusive */
#define SFX_CHN_COUNT   (SFX_CHN_END - SFX_CHN_START)

/* ================================================================
 *  Globals
 * ================================================================ */

static AudioStatus g_audio_status = AUDIO_STATUS_NOT_INIT;
static float       g_sfx_volume   = 0.8f;
static int         g_sfx_chn_next = SFX_CHN_START;

/* Per-SFX data */
typedef struct {
    u8 *data;          /* linearAlloc'd PCM data */
    u32 size;          /* size in bytes */
    u32 sample_rate;
    u16 channels;      /* 1 or 2 */
    u16 bits;          /* 8 or 16 */
    int loaded;
} SfxData;

static SfxData sfx_bank[SFX_COUNT];

/* Music state */
typedef struct {
    u8     *data;
    u32     size;
    u32     sample_rate;
    u16     channels;
    u16     bits;
    MusicId track;
    int     active;
    /* Volume fade */
    float   volume;
    float   target_volume;
    float   fade_speed;
    MusicId pending_track;  /* track to start after fade-out completes */
} MusicState;

static MusicState g_music;

/* Diagnostics - exposed via audio_debug_get() */
static AudioDebug g_debug;

/* File paths */
static const char *sfx_paths[SFX_COUNT] = {
    "romfs:/sfx/sfx_shoot.wav",
    "romfs:/sfx/sfx_hit.wav",
    "romfs:/sfx/sfx_enemy_death.wav",
    "romfs:/sfx/sfx_hurt.wav",
    "romfs:/sfx/sfx_pickup.wav",
    "romfs:/sfx/sfx_door.wav",
    "romfs:/sfx/sfx_boss.wav",
    "romfs:/sfx/sfx_room_clear.wav",
    "romfs:/sfx/sfx_tear_fire1.wav",
    "romfs:/sfx/sfx_tear_fire2.wav",
    "romfs:/sfx/sfx_tear_block.wav",
    "romfs:/sfx/sfx_hurt_grunt.wav",
    "romfs:/sfx/sfx_player_death.wav",
};

static const char *music_paths[MUS_COUNT] = {
    "romfs:/music/mus_title.wav",
    "romfs:/music/mus_basement.wav",
    "romfs:/music/mus_caves.wav",
    "romfs:/music/mus_depths.wav",
    "romfs:/music/mus_boss.wav",
};

/* ================================================================
 *  WAV loader - reads RIFF/WAVE PCM data into linear memory
 *
 *  Returns 0 on success and populates *out.  Allocated PCM data must
 *  be freed with linearFree().
 *
 *  Failure reasons are surfaced via *err_code (negative).
 * ================================================================ */
typedef struct {
    u8 *data;
    u32 size;
    u32 sample_rate;
    u16 channels;
    u16 bits;
} WavLoad;

#define WAV_ERR_NONE        0
#define WAV_ERR_OPEN       -1
#define WAV_ERR_HEADER     -2
#define WAV_ERR_NO_FMT     -3
#define WAV_ERR_NO_DATA    -4
#define WAV_ERR_COMPRESSED -5
#define WAV_ERR_ALLOC      -6
#define WAV_ERR_READ       -7

static int load_wav_to_linear(const char *path, WavLoad *out) {
    memset(out, 0, sizeof(*out));

    FILE *f = fopen(path, "rb");
    if (!f) return WAV_ERR_OPEN;

    char riff_id[4];
    u32  riff_size;
    char wave_id[4];

    if (fread(riff_id, 1, 4, f) != 4 ||
        fread(&riff_size, 4, 1, f) != 1 ||
        fread(wave_id, 1, 4, f) != 4 ||
        memcmp(riff_id, "RIFF", 4) != 0 ||
        memcmp(wave_id, "WAVE", 4) != 0) {
        fclose(f);
        return WAV_ERR_HEADER;
    }

    int got_fmt = 0, got_data = 0;
    u16 audio_fmt = 0;

    while (!got_data) {
        char chunk_id[4];
        u32  chunk_size;
        if (fread(chunk_id, 1, 4, f) != 4) break;
        if (fread(&chunk_size, 4, 1, f) != 1) break;

        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            u32 byte_rate;
            u16 block_align;
            fread(&audio_fmt, 2, 1, f);
            fread(&out->channels, 2, 1, f);
            fread(&out->sample_rate, 4, 1, f);
            fread(&byte_rate, 4, 1, f);
            fread(&block_align, 2, 1, f);
            fread(&out->bits, 2, 1, f);
            if (chunk_size > 16) fseek(f, chunk_size - 16, SEEK_CUR);
            got_fmt = 1;
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            out->size = chunk_size;
            out->data = (u8 *)linearAlloc(chunk_size);
            if (!out->data) { fclose(f); return WAV_ERR_ALLOC; }
            if (fread(out->data, 1, chunk_size, f) != chunk_size) {
                linearFree(out->data);
                out->data = NULL;
                fclose(f);
                return WAV_ERR_READ;
            }
            got_data = 1;
        } else {
            fseek(f, chunk_size, SEEK_CUR);
        }
    }

    fclose(f);

    if (!got_fmt)         { if (out->data) linearFree(out->data); memset(out, 0, sizeof(*out)); return WAV_ERR_NO_FMT; }
    if (!got_data)        { if (out->data) linearFree(out->data); memset(out, 0, sizeof(*out)); return WAV_ERR_NO_DATA; }
    if (audio_fmt != 1)   { if (out->data) linearFree(out->data); memset(out, 0, sizeof(*out)); return WAV_ERR_COMPRESSED; }

    /* Flush the CPU data cache so CSND's hardware DMA reads the actual
     * PCM samples from physical memory rather than stale cache lines.
     *
     * IMPORTANT: use GSPGPU_FlushDataCache here, NOT CSND_FlushDataCache.
     * The official devkitPro CSND mic example (3ds-examples/audio/mic)
     * uses GSPGPU_FlushDataCache and that's the known-working incantation
     * on real hardware.  CSND_FlushDataCache calls a different IPC path
     * that may or may not actually flush the L1 d-cache depending on
     * service handle state. */
    GSPGPU_FlushDataCache(out->data, out->size);

    return WAV_ERR_NONE;
}

/* ================================================================
 *  Helpers
 * ================================================================ */

static u32 sfx_flags_for(const SfxData *s) {
    u32 flags = SOUND_ONE_SHOT | SOUND_LINEAR_INTERP;
    if (s->bits == 16) flags |= SOUND_FORMAT_16BIT;
    else               flags |= SOUND_FORMAT_8BIT;
    return flags;
}

static u32 music_flags_for(const MusicState *m) {
    u32 flags = SOUND_REPEAT | SOUND_LINEAR_INTERP;
    if (m->bits == 16) flags |= SOUND_FORMAT_16BIT;
    else               flags |= SOUND_FORMAT_8BIT;
    return flags;
}

/* ================================================================
 *  Core API
 * ================================================================ */

int audio_init(int audio_enabled) {
    memset(&g_debug, 0, sizeof(g_debug));
    g_debug.init_called = 1;

    if (g_audio_status == AUDIO_STATUS_OK) return 0;

    if (!audio_enabled) {
        g_audio_status = AUDIO_STATUS_DISABLED_USER;
        return -1;
    }

    /* Initialize CSND.  No DSP firmware needed - this just opens the
     * csnd:SND service.  Should always succeed on real hardware.
     * Note: csndInit() internally calls CSND_AcquireSoundChannels() which
     * populates the global `csndChannels` bitmask.  Channels 0-7 are
     * usually reserved for the DSP service, so homebrew typically gets
     * channels 8-31 (mask 0xFFFFFF00). */
    Result rc = csndInit();
    g_debug.init_result = rc;
    if (R_FAILED(rc)) {
        g_audio_status = AUDIO_STATUS_DISABLED_INIT_FAIL;
        return -1;
    }

    /* Capture the channel allocation bitmask after init.  This is the
     * single most important piece of diagnostic info - if bit MUSIC_CHANNEL
     * (8) or any of SFX_CHN_START..SFX_CHN_END-1 (9..15) isn't set,
     * csndPlaySound() will silently return 1 (channel not allowed). */
    g_debug.csnd_channels = csndChannels;

    memset(sfx_bank, 0, sizeof(sfx_bank));
    memset(&g_music, 0, sizeof(g_music));
    g_music.track          = MUS_NONE;
    g_music.pending_track  = MUS_NONE;
    g_music.volume         = 0.6f;
    g_music.target_volume  = 0.6f;
    g_sfx_chn_next         = SFX_CHN_START;
    g_audio_status         = AUDIO_STATUS_OK;
    return 0;
}

void audio_exit(void) {
    if (g_audio_status != AUDIO_STATUS_OK) return;

    music_stop();
    audio_free_all();

    csndExit();
    g_audio_status = AUDIO_STATUS_NOT_INIT;
}

int audio_load_all(void) {
    if (g_audio_status != AUDIO_STATUS_OK) return 0;

    int loaded = 0;
    for (int i = 0; i < SFX_COUNT; i++) {
        WavLoad w;
        int rc = load_wav_to_linear(sfx_paths[i], &w);
        if (rc == 0) {
            sfx_bank[i].data        = w.data;
            sfx_bank[i].size        = w.size;
            sfx_bank[i].sample_rate = w.sample_rate;
            sfx_bank[i].channels    = w.channels;
            sfx_bank[i].bits        = w.bits;
            sfx_bank[i].loaded      = 1;
            loaded++;
        } else if (g_debug.first_load_err == 0) {
            /* Remember the first failure so we can show it on screen */
            g_debug.first_load_err      = rc;
            g_debug.first_load_err_idx  = i;
        }
    }
    g_debug.sfx_load_count = loaded;
    return loaded;
}

void audio_free_all(void) {
    for (int i = 0; i < SFX_COUNT; i++) {
        if (sfx_bank[i].data) {
            linearFree(sfx_bank[i].data);
            sfx_bank[i].data = NULL;
        }
        sfx_bank[i].loaded = 0;
    }
}

void audio_play(SfxId id) {
    g_debug.play_sfx_calls++;
    g_debug.last_play_status = g_audio_status;
    if (g_audio_status != AUDIO_STATUS_OK) {
        g_debug.last_play_result = -1;
        return;
    }
    if (id < 0 || id >= SFX_COUNT) { g_debug.last_play_result = -2; return; }
    if (!sfx_bank[id].loaded || !sfx_bank[id].data) {
        g_debug.last_play_result = -3;
        return;
    }

    /* Pick the next round-robin SFX channel */
    int chn = g_sfx_chn_next;
    g_sfx_chn_next++;
    if (g_sfx_chn_next >= SFX_CHN_END) g_sfx_chn_next = SFX_CHN_START;

    SfxData *s = &sfx_bank[id];

    /* Stop whatever is on this channel first.  csndPlaySound below will
     * call csndExecCmds(true) internally, so the SetPlayState command
     * queued here will be executed as part of that batch. */
    CSND_SetPlayState((u32)chn, 0);

    /* Belt-and-braces: ensure the sample data is visible to DMA in case
     * the cache wasn't flushed at load time.  Use GSPGPU_FlushDataCache
     * to match the official devkitPro CSND example. */
    GSPGPU_FlushDataCache(s->data, s->size);

    Result rc = csndPlaySound(
        chn,
        sfx_flags_for(s),
        (u32)s->sample_rate,
        g_sfx_volume,
        0.0f,           /* center pan */
        s->data,
        NULL,           /* one-shot: data1 must be NULL per devkitPro example */
        s->size
    );
    g_debug.last_play_result = rc;
    g_debug.last_play_chn    = chn;
    g_debug.last_play_sfx    = id;
}

void audio_set_volume(float vol) {
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;
    g_sfx_volume = vol;
}

/* ================================================================
 *  Status / Query API
 * ================================================================ */

AudioStatus audio_get_status(void) {
    return g_audio_status;
}

int audio_is_available(void) {
    return (g_audio_status == AUDIO_STATUS_OK) ? 1 : 0;
}

const char *audio_status_string(void) {
    switch (g_audio_status) {
    case AUDIO_STATUS_NOT_INIT:           return "Not initialized";
    case AUDIO_STATUS_OK:                 return "Working";
    case AUDIO_STATUS_DISABLED_USER:      return "Disabled (user config)";
    case AUDIO_STATUS_DISABLED_INIT_FAIL: return "CSND init failed";
    case AUDIO_STATUS_DISABLED_RUNTIME:   return "Disabled (error)";
    default:                              return "Unknown";
    }
}

void audio_force_disable(void) {
    if (g_audio_status == AUDIO_STATUS_OK) {
        music_stop();
        audio_free_all();
        csndExit();
    }
    g_audio_status = AUDIO_STATUS_DISABLED_RUNTIME;
}

void audio_debug_get(AudioDebug *out) {
    if (out) *out = g_debug;
}

/* Synthesize a 1-second 440 Hz square wave and play it on channel 8.
 * Used by the debug overlay to verify CSND output is actually producing
 * sound, independent of the WAV loader or romfs. */
void audio_test_tone(void) {
    if (g_audio_status != AUDIO_STATUS_OK) {
        g_debug.test_tone_result = -1;
        return;
    }
    const u32 sample_rate = 22050;
    const u32 freq_hz     = 440;
    const u32 num_samples = sample_rate;          /* 1 second */
    const u32 byte_size   = num_samples * 2;      /* 16-bit mono */
    const u32 period      = sample_rate / freq_hz;

    s16 *buf = (s16 *)linearAlloc(byte_size);
    if (!buf) { g_debug.test_tone_result = -2; return; }

    for (u32 i = 0; i < num_samples; i++) {
        buf[i] = ((i / (period / 2)) & 1) ? 20000 : -20000;
    }

    /* Stop existing music on channel 8 (test tone will use it). */
    CSND_SetPlayState(MUSIC_CHANNEL, 0);
    GSPGPU_FlushDataCache(buf, byte_size);

    Result rc = csndPlaySound(
        MUSIC_CHANNEL,
        SOUND_ONE_SHOT | SOUND_FORMAT_16BIT,
        sample_rate,
        0.8f,
        0.0f,
        buf,
        NULL,           /* one-shot: data1 must be NULL */
        byte_size
    );
    g_debug.test_tone_result = rc;
    g_debug.test_tone_played++;

    /* We leak this 44KB buffer on purpose: linearFree would happen before
     * the hardware finishes playing it.  It only happens when the user
     * presses the debug-overlay test button, so the leak is bounded. */
    (void)buf;
}

/* ================================================================
 *  Music API
 * ================================================================ */

static void music_apply_volume(void) {
    if (g_audio_status != AUDIO_STATUS_OK) return;
    if (!g_music.active) return;

    u32 vol = CSND_VOL(g_music.volume, 0.0f);
    CSND_SetVol(MUSIC_CHANNEL, vol, 0);
    csndExecCmds(false);
}

static void music_free_buffer(void) {
    if (g_music.data) {
        linearFree(g_music.data);
        g_music.data = NULL;
    }
    g_music.size = 0;
}

static void music_start_track(MusicId id) {
    if (g_audio_status != AUDIO_STATUS_OK) return;
    if (id < 0 || id >= MUS_COUNT) return;

    g_debug.music_load_attempts++;

    /* Stop whatever is currently playing on the music channel. */
    CSND_SetPlayState(MUSIC_CHANNEL, 0);
    csndExecCmds(true);  /* wait for the stop to take effect */
    music_free_buffer();

    /* Load the new track.  load_wav_to_linear() already flushes the data
     * cache so the PCM data is visible to hardware DMA. */
    WavLoad w;
    int rc_load = load_wav_to_linear(music_paths[id], &w);
    if (rc_load != 0) {
        g_debug.music_load_failures++;
        g_debug.last_music_load_err = rc_load;
        g_music.active = 0;
        g_music.track  = MUS_NONE;
        return;
    }

    g_music.data         = w.data;
    g_music.size         = w.size;
    g_music.sample_rate  = w.sample_rate;
    g_music.channels     = w.channels;
    g_music.bits         = w.bits;
    g_music.track        = id;
    g_music.active       = 1;
    g_music.pending_track = MUS_NONE;

    Result rc = csndPlaySound(
        MUSIC_CHANNEL,
        music_flags_for(&g_music),
        g_music.sample_rate,
        g_music.volume,
        0.0f,
        g_music.data,
        g_music.data,
        g_music.size
    );
    g_debug.last_music_play_result = rc;
}

void music_play(MusicId id) {
    if (g_audio_status != AUDIO_STATUS_OK) return;
    if (id < 0 || id >= MUS_COUNT) return;

    /* Already playing this track? */
    if (g_music.active && g_music.track == id && g_music.fade_speed == 0.0f) {
        return;
    }

    /* If something is already playing, crossfade rather than abrupt cut */
    if (g_music.active) {
        music_crossfade(id, 30);
        return;
    }

    if (g_music.target_volume < 0.05f) g_music.target_volume = 0.6f;
    g_music.volume     = g_music.target_volume;
    g_music.fade_speed = 0.0f;
    music_start_track(id);
}

void music_stop(void) {
    if (g_audio_status != AUDIO_STATUS_OK) return;
    if (!g_music.active) return;

    CSND_SetPlayState(MUSIC_CHANNEL, 0);
    csndExecCmds(true);  /* wait for stop to actually take effect */
    music_free_buffer();

    g_music.active        = 0;
    g_music.track         = MUS_NONE;
    g_music.pending_track = MUS_NONE;
    g_music.fade_speed    = 0.0f;
}

void music_fade_out(int ticks) {
    if (g_audio_status != AUDIO_STATUS_OK) return;
    if (!g_music.active) return;
    if (ticks <= 0) { music_stop(); return; }

    g_music.target_volume  = 0.0f;
    g_music.fade_speed     = -g_music.volume / (float)ticks;
    g_music.pending_track  = MUS_NONE;
}

void music_crossfade(MusicId new_track, int ticks) {
    if (g_audio_status != AUDIO_STATUS_OK) return;
    if (new_track < 0 || new_track >= MUS_COUNT) return;

    if (!g_music.active) {
        if (g_music.target_volume < 0.05f) g_music.target_volume = 0.6f;
        g_music.volume = g_music.target_volume;
        music_start_track(new_track);
        return;
    }
    if (g_music.track == new_track) return;
    if (ticks <= 0) ticks = 1;

    g_music.target_volume  = 0.0f;
    g_music.fade_speed     = -g_music.volume / (float)ticks;
    g_music.pending_track  = new_track;
}

void music_set_volume(float vol) {
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;

    g_music.target_volume = vol;
    if (g_audio_status != AUDIO_STATUS_OK) return;
    if (!g_music.active || g_music.fade_speed != 0.0f) return;

    g_music.volume = vol;
    music_apply_volume();
}

MusicId music_current(void) {
    return g_music.active ? g_music.track : MUS_NONE;
}

void music_update(void) {
    if (g_audio_status != AUDIO_STATUS_OK) return;
    if (!g_music.active) return;

    /* Handle volume fade-in / fade-out */
    if (g_music.fade_speed != 0.0f) {
        g_music.volume += g_music.fade_speed;

        if (g_music.fade_speed < 0.0f && g_music.volume <= 0.0f) {
            /* Fade-out complete */
            g_music.volume = 0.0f;
            g_music.fade_speed = 0.0f;

            MusicId next = g_music.pending_track;
            music_stop();

            if (next != MUS_NONE) {
                /* Fade-in the new track */
                g_music.volume        = 0.01f;
                g_music.target_volume = 0.6f;
                g_music.fade_speed    = 0.6f / 30.0f;
                music_start_track(next);
            }
            return;
        }

        if (g_music.fade_speed > 0.0f && g_music.volume >= g_music.target_volume) {
            g_music.volume     = g_music.target_volume;
            g_music.fade_speed = 0.0f;
        }

        music_apply_volume();
    }
}
