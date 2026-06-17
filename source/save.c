/*
 * save.c - Binding of Isaac 3DS
 * Run save / continue system (see save.h).
 */

#include "save.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* Cached existence flag so the main menu can poll every frame without
 * hitting the SD card: -1 = unknown (stat once), 0 = no, 1 = yes. */
static int s_save_exists = -1;

int run_save_exists(void) {
    if (s_save_exists < 0) {
        struct stat st;
        s_save_exists = (stat(RUN_SAVE_PATH, &st) == 0) ? 1 : 0;
    }
    return s_save_exists;
}

int run_save_write(const RunSaveData *data) {
    mkdir(CONFIG_DIR, 0755);  /* ignore error if already exists */

    FILE *f = fopen(RUN_SAVE_PATH, "wb");
    if (!f) return -1;

    size_t written = fwrite(data, 1, sizeof(*data), f);
    fclose(f);

    if (written != sizeof(*data)) {
        /* Partial write (SD full/removed): don't leave a corrupt save. */
        remove(RUN_SAVE_PATH);
        s_save_exists = 0;
        return -1;
    }
    s_save_exists = 1;
    return 0;
}

int run_save_read(RunSaveData *out) {
    FILE *f = fopen(RUN_SAVE_PATH, "rb");
    if (!f) {
        s_save_exists = 0;
        return -1;
    }

    /* Zero first so any field a shorter/older save lacks reads back as 0
     * rather than uninitialized garbage. The layout is append-only into the
     * reserved tails (from v3 on), so an older save's bytes land at the same
     * offsets and only the newer trailing fields default to zero. */
    memset(out, 0, sizeof(*out));

    /* Validate the header before trusting any payload: magic, a supported
     * version, and the Player struct size. The append-only contract keeps
     * sizeof(Player) constant within a version (new fields consume the
     * reserved tail), so a mismatch means an incompatible build slipped
     * through without bumping the version - reject rather than load a
     * mis-aligned blob. */
    unsigned int header[3];
    if (fread(header, sizeof(unsigned int), 3, f) != 3 ||
        header[0] != RUN_SAVE_MAGIC ||
        header[1] < RUN_SAVE_MIN_COMPAT ||
        header[1] > RUN_SAVE_VERSION ||
        header[2] != (unsigned int)sizeof(Player)) {
        /* Missing/garbage magic, a pre-reserved-tail layout, a save from a
         * NEWER build we can't read, or a Player-layout mismatch: discard. */
        fclose(f);
        run_save_delete();
        return -1;
    }

    /* Re-read the full record from the top. While MIN_COMPAT == VERSION every
     * valid save is exactly our size, so a short read means truncation/
     * corruption - discard it (a zero-filled tail would otherwise restore a
     * dead, hp=0 player). NOTE: when a future build supports an older, smaller
     * version, gate this on the expected size for header[1] instead of a flat
     * sizeof(*out). */
    rewind(f);
    size_t got = fread(out, 1, sizeof(*out), f);
    fclose(f);
    if (got < sizeof(*out)) {
        run_save_delete();
        return -1;
    }

    s_save_exists = 1;
    return 0;
}

void run_save_delete(void) {
    remove(RUN_SAVE_PATH);
    s_save_exists = 0;
}
