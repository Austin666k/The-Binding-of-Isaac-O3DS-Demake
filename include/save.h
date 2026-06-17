/*
 * save.h - Binding of Isaac 3DS
 * Run save / continue system.
 *
 * A run is saved at the start of every floor (and on Save & Quit).
 * Continuing resumes at the start of the saved floor with the player's
 * full stats, items, consumables and run metadata intact; the floor
 * layout itself is re-rolled, roguelike-style.
 *
 * Save file: sdmc:/3ds/binding_of_isaac/run.sav (binary, versioned)
 */
#ifndef SAVE_H
#define SAVE_H

#include "game.h"

#define RUN_SAVE_PATH    "sdmc:/3ds/binding_of_isaac/run.sav"
#define RUN_SAVE_MAGIC   0x31494F42u   /* "BOI1" little-endian */
/* v3: active-item slot, trinket, familiars + reserved tails. From v3 on the
 * layout is append-only into the reserved tails, so future versions load
 * older saves non-destructively (see run_save_read). */
#define RUN_SAVE_VERSION 3
/* Oldest version whose on-disk layout is still binary-compatible with the
 * current struct. Pre-v3 saves had no reserved tails (different field
 * offsets) and are intentionally discarded. Keep this at 3 forever; only
 * bump it if a future change breaks the append-only contract. */
#define RUN_SAVE_MIN_COMPAT 3

/* Everything needed to resume a run at the start of a floor.
 * Player is plain-old-data (no pointers) so it is stored verbatim;
 * player_struct_size guards against layout changes between builds. */
typedef struct {
    unsigned int magic;
    unsigned int version;
    unsigned int player_struct_size;   /* sizeof(Player) sanity check */
    int game_mode;                     /* GameMode */
    int difficulty;                    /* Difficulty */
    int active_challenge;              /* ChallengeType or CHALLENGE_NONE */
    int current_floor;
    int infinite_loop;
    int best_floor;
    int score;
    int kills;
    int play_time_frames;
    int rooms_cleared;
    int characters_completed_run;
    int pill_color_map[PILL_EFFECT_COUNT];
    int pill_known[PILL_EFFECT_COUNT];
    /* Reserved headroom for future top-level run fields (zero-filled on write).
     * Append new fields by consuming this BEFORE `player` so player stays last. */
    unsigned int reserved[16];
    Player player;   /* must stay last: it carries its own reserved tail */
} RunSaveData;

/* Fast cached check - safe to call every frame (menu rendering). */
int  run_save_exists(void);

/* Write/read/delete the save file. Return 0 on success, -1 on failure. */
int  run_save_write(const RunSaveData *data);
int  run_save_read(RunSaveData *out);
void run_save_delete(void);

#endif /* SAVE_H */
