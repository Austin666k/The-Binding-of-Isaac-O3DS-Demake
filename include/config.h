/*
 * config.h - Binding of Isaac 3DS
 * Simple INI-style config file system for user preferences.
 *
 * Config file location: sdmc:/3ds/binding_of_isaac/config.ini
 * The directory is created automatically on first write.
 */
#ifndef CONFIG_H
#define CONFIG_H

/* ---------- Config Data ---------- */
typedef struct {
    int audio_enabled;      /* 1 = attempt audio init, 0 = force silent mode */
    float sfx_volume;       /* 0.0 - 1.0 */
    float music_volume;     /* 0.0 - 1.0 */
    /* Unlocks / Achievements (Phase 2) */
    int unlocked_chars;          /* bitmask: bit 0=Isaac, 1=Magdalene, 2=Cain, 3=Judas,
                                     4=Eve, 5=Samson, 6=Blue Baby (???) */
    int total_wins;              /* total runs completed successfully */
    int bosses_defeated;         /* bitmask of bosses defeated (by enum index) */
    int characters_completed;    /* bitmask of characters who beat a run */
    int floors_reached;          /* deepest floor index ever reached (0-7) */
    int total_runs_started;      /* total number of runs ever started */
} GameConfig;

/* Default values */
#define CONFIG_DEFAULT_AUDIO_ENABLED  1
#define CONFIG_DEFAULT_SFX_VOLUME     0.8f
#define CONFIG_DEFAULT_MUSIC_VOLUME   0.6f
/* Default unlocked characters: only Isaac (bit 0) */
#define CONFIG_DEFAULT_UNLOCKED_CHARS 0x01

/* Config file path (saves go through a temp file + rename so a power-off
 * mid-write can never destroy the existing config/unlock data) */
#define CONFIG_DIR      "sdmc:/3ds/binding_of_isaac"
#define CONFIG_PATH     "sdmc:/3ds/binding_of_isaac/config.ini"
#define CONFIG_TMP_PATH "sdmc:/3ds/binding_of_isaac/config.ini.tmp"

/* Highest legal floors_reached value. config.c cannot see game.h;
 * keep in sync with MAX_FLOORS - 1 (game.h). */
#define CONFIG_FLOORS_MAX_INDEX 7

/* ---------- API ---------- */

/* Initialize config with defaults */
void config_init(GameConfig *cfg);

/* Load config from SD card. Returns 0 on success, -1 if file not found
 * (defaults are kept in that case). */
int  config_load(GameConfig *cfg);

/* Save config to SD card. Creates directory if needed.
 * Returns 0 on success, -1 on failure. */
int  config_save(const GameConfig *cfg);

#endif /* CONFIG_H */
