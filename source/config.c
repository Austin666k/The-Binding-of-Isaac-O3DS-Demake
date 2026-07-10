/*
 * config.c - Binding of Isaac 3DS
 * Simple INI-style config file read/write.
 *
 * Format:
 *   # comment
 *   key=value
 *
 * Supported keys:
 *   audio_enabled  (0 or 1)
 *   sfx_volume     (0.0 - 1.0)
 *   music_volume   (0.0 - 1.0)
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

/* ---------- Helpers ---------- */

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Trim leading whitespace in place (returns pointer into same buffer) */
static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    /* Trim trailing whitespace/newline */
    char *end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r'))
        *end-- = '\0';
    return s;
}

/* ---------- API ---------- */

void config_init(GameConfig *cfg) {
    cfg->audio_enabled = CONFIG_DEFAULT_AUDIO_ENABLED;
    cfg->sfx_volume    = CONFIG_DEFAULT_SFX_VOLUME;
    cfg->music_volume  = CONFIG_DEFAULT_MUSIC_VOLUME;
    /* Unlocks/achievements defaults */
    cfg->unlocked_chars       = CONFIG_DEFAULT_UNLOCKED_CHARS;
    cfg->total_wins           = 0;
    cfg->bosses_defeated      = 0;
    cfg->characters_completed = 0;
    cfg->floors_reached       = 0;
    cfg->total_runs_started   = 0;
    cfg->devil_deals_taken    = 0;
    cfg->total_deaths         = 0;
}

int config_load(GameConfig *cfg) {
    config_init(cfg);  /* start with defaults */

    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f) return -1;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);
        if (*s == '#' || *s == '\0') continue;  /* skip comments/blanks */

        char *eq = strchr(s, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = trim(s);
        char *val = trim(eq + 1);

        if (strcmp(key, "audio_enabled") == 0) {
            cfg->audio_enabled = (atoi(val) != 0) ? 1 : 0;
        } else if (strcmp(key, "sfx_volume") == 0) {
            cfg->sfx_volume = clampf((float)atof(val), 0.0f, 1.0f);
        } else if (strcmp(key, "music_volume") == 0) {
            cfg->music_volume = clampf((float)atof(val), 0.0f, 1.0f);
        } else if (strcmp(key, "unlocked_chars") == 0) {
            cfg->unlocked_chars = atoi(val);
            if (cfg->unlocked_chars < 0) cfg->unlocked_chars = 0;
            /* Always keep Isaac unlocked */
            cfg->unlocked_chars |= 0x01;
        } else if (strcmp(key, "total_wins") == 0) {
            cfg->total_wins = atoi(val);
        } else if (strcmp(key, "bosses_defeated") == 0) {
            cfg->bosses_defeated = atoi(val);
        } else if (strcmp(key, "characters_completed") == 0) {
            cfg->characters_completed = atoi(val);
        } else if (strcmp(key, "floors_reached") == 0) {
            cfg->floors_reached = atoi(val);
        } else if (strcmp(key, "total_runs_started") == 0) {
            cfg->total_runs_started = atoi(val);
        } else if (strcmp(key, "devil_deals_taken") == 0) {
            cfg->devil_deals_taken = atoi(val);
        } else if (strcmp(key, "total_deaths") == 0) {
            cfg->total_deaths = atoi(val);
        }
        /* unknown keys are silently ignored for forward-compatibility */
    }

    fclose(f);

    /* Sanitize: a corrupt/hand-edited file must never load negative stats
       (negative bitmasks would unlock everything; negative counters break
       display + progression math). floors_reached is also bounded above
       (CONFIG_FLOORS_MAX_INDEX mirrors MAX_FLOORS - 1 from game.h). */
    if (cfg->total_wins           < 0) cfg->total_wins           = 0;
    if (cfg->bosses_defeated      < 0) cfg->bosses_defeated      = 0;
    if (cfg->characters_completed < 0) cfg->characters_completed = 0;
    if (cfg->floors_reached       < 0) cfg->floors_reached       = 0;
    if (cfg->total_runs_started   < 0) cfg->total_runs_started   = 0;
    if (cfg->devil_deals_taken    < 0) cfg->devil_deals_taken    = 0;
    if (cfg->total_deaths         < 0) cfg->total_deaths         = 0;
    if (cfg->floors_reached > CONFIG_FLOORS_MAX_INDEX)
        cfg->floors_reached = CONFIG_FLOORS_MAX_INDEX;

    return 0;
}

int config_save(const GameConfig *cfg) {
    /* Ensure directory exists */
    mkdir(CONFIG_DIR, 0755);  /* ignore error if already exists */

    /* Write to a temp file first, then swap it in — a power-off or HOME
       mid-write must never destroy the existing config/unlock data. */
    FILE *f = fopen(CONFIG_TMP_PATH, "w");
    if (!f) return -1;

    fprintf(f, "# Binding of Isaac 3DS - Configuration\n");
    fprintf(f, "# Edit this file or change settings in-game (SELECT on main menu).\n");
    fprintf(f, "#\n");
    fprintf(f, "# audio_enabled: 1 = enable CSND audio, 0 = force silent mode\n");
    fprintf(f, "# CSND does not require any external firmware - works on every 3DS\n");
    fprintf(f, "audio_enabled=%d\n", cfg->audio_enabled ? 1 : 0);
    fprintf(f, "\n");
    fprintf(f, "# Volume levels (0.0 to 1.0)\n");
    fprintf(f, "sfx_volume=%.2f\n", cfg->sfx_volume);
    fprintf(f, "music_volume=%.2f\n", cfg->music_volume);
    fprintf(f, "\n");
    fprintf(f, "# --- Unlocks / Achievements ---\n");
    fprintf(f, "# Bitmasks: 0x01=Isaac, 0x02=Magdalene, 0x04=Cain, 0x08=Judas\n");
    fprintf(f, "unlocked_chars=%d\n", cfg->unlocked_chars);
    fprintf(f, "characters_completed=%d\n", cfg->characters_completed);
    fprintf(f, "bosses_defeated=%d\n", cfg->bosses_defeated);
    fprintf(f, "total_wins=%d\n", cfg->total_wins);
    fprintf(f, "total_runs_started=%d\n", cfg->total_runs_started);
    fprintf(f, "floors_reached=%d\n", cfg->floors_reached);
    fprintf(f, "devil_deals_taken=%d\n", cfg->devil_deals_taken);
    fprintf(f, "total_deaths=%d\n", cfg->total_deaths);

    if (fclose(f) != 0) {
        remove(CONFIG_TMP_PATH);
        return -1;
    }

    /* Swap into place. FAT/sdmc rename() fails if the target exists, so
       remove the old file first (the fully-written temp is the fallback). */
    remove(CONFIG_PATH);  /* may not exist yet — ignore result */
    if (rename(CONFIG_TMP_PATH, CONFIG_PATH) != 0) return -1;

    return 0;
}
