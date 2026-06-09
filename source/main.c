/*
 * main.c - Binding of Isaac 3DS
 *
 * Enhanced homebrew port with:
 *   - Item system with stat boosts and special abilities
 *   - Player stats (damage, speed, fire rate, range, max HP)
 *   - Multiple floors (Basement I/II, Caves I/II, Depths)
 *   - Unique boss per floor with different attack patterns
 *   - Treasure rooms with collectible items on pedestals
 *   - Floor transitions via trapdoor after boss defeat
 *
 * Controls:
 *   D-Pad / Circle Pad  - Move Isaac / Navigate menu
 *   A  - Select / Shoot right
 *   B  - Shoot down / Back
 *   X  - Shoot up
 *   Y  - Shoot left
 *   START - Restart after game over
 */

#include "game.h"
#include "sprite.h"
#include "audio.h"
#include "config.h"
#include "menu_logo_atlas.h"
#include "bulletatlas.h"
#include "enemies_atlas.h"

/* Global flag: 1 if sprites loaded successfully, 0 = fallback to procedural */
static int g_sprites_loaded = 0;

/* Early forward declarations needed before definitions appear later */
static int is_boss_type(EnemyType t);
static void spawn_heart(Room *r, float x, float y, HeartType type);

/* Global config (loaded from SD card on startup) */
static GameConfig g_config;

/* ================================================================
 * Utility helpers
 * ================================================================ */

float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int randi(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + (rand() % (hi - lo + 1));
}

float randf(float lo, float hi) {
    return lo + ((float)rand() / (float)RAND_MAX) * (hi - lo);
}

/* ================================================================
 * Item definitions
 * ================================================================ */

static ItemDef item_pool[ITEM_COUNT];

void init_item_pool(void) {
    memset(item_pool, 0, sizeof(item_pool));

    /* ITEM_NONE = 0, skip */
    #define DEF(t, n, d, s, f, r, h, fl, desc) do { \
        item_pool[t].type = t; item_pool[t].name = n; \
        item_pool[t].damage_bonus = d; item_pool[t].speed_bonus = s; \
        item_pool[t].fire_rate_bonus = f; item_pool[t].range_bonus = r; \
        item_pool[t].hp_bonus = h; item_pool[t].flags = fl; \
        item_pool[t].description = desc; \
    } while(0)

    DEF(ITEM_PENTAGRAM,       "Pentagram",       0.5f,  0.0f,  0.0f,  0.0f,  0, 0,
        "Damage Up");
    DEF(ITEM_BELT,            "The Belt",        0.0f,  0.3f,  0.0f,  0.0f,  0, 0,
        "Speed Up");
    DEF(ITEM_WIRE_COAT,       "Wire Coat",       0.0f,  0.0f,  0.7f,  0.0f,  0, 0,
        "Tears Up");
    DEF(ITEM_LUNCH,           "Lunch",           0.0f,  0.0f,  0.0f,  0.0f,  2, 0,
        "Health Up");
    DEF(ITEM_CUPIDS_ARROW,    "Cupid's Arrow",   0.0f,  0.0f,  0.0f,  0.0f,  0, ITEM_FLAG_PIERCING,
        "Piercing tears");
    DEF(ITEM_SPOON_BENDER,    "Spoon Bender",    0.0f,  0.0f,  0.0f,  0.0f,  0, ITEM_FLAG_HOMING,
        "Homing tears");
    DEF(ITEM_POLYPHEMUS,      "Polyphemus",      1.5f,  0.0f, -1.0f,  0.0f,  0, 0,
        "Bigger tears, slower fire");
    DEF(ITEM_INNER_EYE,       "Inner Eye",       0.0f,  0.0f, -1.5f,  0.0f,  0, ITEM_FLAG_TRIPLE,
        "Triple shot");
    DEF(ITEM_SPELUNKER_HAT,   "Spelunker Hat",   0.0f,  0.0f,  0.0f,  1.5f,  0, 0,
        "Range Up");
    DEF(ITEM_SPEED_BALL,      "Speed Ball",      0.0f,  0.3f,  0.5f,  0.0f,  0, 0,
        "Speed + Tears");
    DEF(ITEM_MAGIC_MUSH,      "Magic Mushroom",  0.5f,  0.0f,  0.0f,  1.0f,  2, 0,
        "All stats up");
    DEF(ITEM_SACRED_HEART,    "Sacred Heart",    1.0f, -0.3f,  0.0f,  0.0f,  0, ITEM_FLAG_HOMING,
        "Big damage, homing");
    DEF(ITEM_SPIRIT_SWORD,    "Spirit Sword",    0.0f,  0.0f,  0.0f,  0.0f,  0, ITEM_FLAG_SPECTRAL,
        "Spectral tears");
    DEF(ITEM_BLOOD_OF_MARTYR, "Blood Martyr",    0.5f,  0.0f,  0.0f,  0.0f,  0, 0,
        "Damage Up");
    DEF(ITEM_STIGMATA,        "Stigmata",        0.3f,  0.0f,  0.0f,  0.0f,  2, 0,
        "Damage + Health");
    DEF(ITEM_SAD_ONION,       "Sad Onion",       0.0f,  0.0f,  1.0f,  0.0f,  0, 0,
        "Tears Up");
    DEF(ITEM_WIRE_HANGER,     "Wire Hanger",     0.0f,  0.0f,  0.5f,  0.0f,  0, 0,
        "Tears Up");
    DEF(ITEM_GROWTH_HORMONES, "Growth Hormones", 0.4f,  0.2f,  0.0f,  0.0f,  0, 0,
        "Damage + Speed");
    DEF(ITEM_JESUS_JUICE,     "Jesus Juice",     0.5f,  0.0f,  0.0f,  0.5f,  0, 0,
        "Damage + Range");
    DEF(ITEM_HALO,            "The Halo",        0.3f,  0.2f,  0.2f,  0.5f,  2, 0,
        "All stats up");
    /* --- New expansion items --- */
    DEF(ITEM_MOMS_KNIFE,      "Mom's Knife",     1.0f,  0.0f, -1.5f,  0.5f,  0, ITEM_FLAG_KNIFE | ITEM_FLAG_PIERCING,
        "Throws a piercing knife");
    DEF(ITEM_BRIMSTONE,       "Brimstone",       1.5f,  0.0f, -2.0f,  0.0f,  0, ITEM_FLAG_BRIMSTONE,
        "Charged blood laser");
    DEF(ITEM_TECHNOLOGY,      "Technology",      0.4f,  0.0f,  0.0f,  0.5f,  0, ITEM_FLAG_LASER,
        "Continuous laser eye");
    DEF(ITEM_NUMBER_ONE,      "Number One",      0.0f,  0.0f,  2.0f, -0.5f,  0, 0,
        "Huge fire rate, short range");
    DEF(ITEM_DR_FETUS,        "Dr. Fetus",       1.2f,  0.0f, -1.5f,  0.0f,  0, ITEM_FLAG_BOMB_TEAR,
        "Shoots bombs");
    DEF(ITEM_EPIC_FETUS,      "Epic Fetus",      1.8f,  0.0f, -2.0f,  0.5f,  0, ITEM_FLAG_BOMB_TEAR | ITEM_FLAG_EXPLOSIVE,
        "Targeted bomb strikes");
    DEF(ITEM_DEAD_CAT,        "Dead Cat",        0.0f,  0.0f,  0.0f,  0.0f,  0, 0,
        "9 lives. Max HP set to 1");
    DEF(ITEM_HOLY_MANTLE,     "Holy Mantle",     0.0f,  0.0f,  0.0f,  0.0f,  0, ITEM_FLAG_MANTLE,
        "Absorb one hit per room");
    DEF(ITEM_PYROMANIAC,      "Pyromaniac",      0.0f,  0.0f,  0.0f,  0.0f,  0, ITEM_FLAG_FIRE_IMMUNE,
        "Explosions heal you");
    DEF(ITEM_IPECAC,          "Ipecac",          1.0f,  0.0f, -1.0f,  0.0f,  0, ITEM_FLAG_EXPLOSIVE,
        "Explosive tears");
    DEF(ITEM_SOY_MILK,        "Soy Milk",       -0.5f,  0.0f,  3.0f,  0.0f,  0, 0,
        "Tiny tears, huge fire rate");
    DEF(ITEM_YUM_HEART,       "Yum Heart",       0.0f,  0.0f,  0.0f,  0.0f,  0, 0,
        "Active: heal half a heart");
    DEF(ITEM_LUCKY_FOOT,      "Lucky Foot",      0.0f,  0.1f,  0.0f,  0.0f,  0, 0,
        "Luck up, better drops");
    DEF(ITEM_BOOK_OF_BELIAL,  "Book of Belial",  0.0f,  0.0f,  0.0f,  0.0f,  0, 0,
        "Active: +damage this room");
    #undef DEF
}

const ItemDef *get_item_def(ItemType type) {
    if (type <= ITEM_NONE || type >= ITEM_COUNT) return NULL;
    return &item_pool[type];
}

/* ================================================================
 * Floor definitions
 * ================================================================ */

/*                name        hp_bonus  base_boss_hp  spd_mult  boss_hp_sc  boss_spd_sc  tier */
static const FloorInfo floors[MAX_FLOORS] = {
    { "Basement I",    0,       20,       1.0f,      1.0f,       1.0f,    BOSS_TIER_BASEMENT },
    { "Basement II",   1,       25,       1.1f,      1.2f,       1.05f,   BOSS_TIER_BASEMENT },
    { "Caves I",       2,       32,       1.2f,      1.35f,      1.1f,    BOSS_TIER_CAVES    },
    { "Caves II",      3,       40,       1.3f,      1.5f,       1.15f,   BOSS_TIER_CAVES    },
    { "Depths",        4,       50,       1.4f,      1.7f,       1.25f,   BOSS_TIER_DEPTHS   },
    { "The Womb",      6,       65,       1.5f,      1.9f,       1.35f,   BOSS_TIER_WOMB     },
    { "Sheol",         9,       85,       1.6f,      2.2f,       1.5f,    BOSS_TIER_SHEOL    },
};

const FloorInfo *get_floor_info(int floor_num) {
    if (floor_num < 0) floor_num = 0;
    if (floor_num >= MAX_FLOORS) floor_num = MAX_FLOORS - 1;
    return &floors[floor_num];
}

/* ================================================================
 * Difficulty Modifiers
 * ================================================================ */

float diff_enemy_hp_mult(Difficulty d) {
    switch (d) {
        case DIFF_EASY:   return 0.75f;
        case DIFF_HARD:   return 1.25f;
        default:          return 1.0f;
    }
}

/* Heart drop chance threshold out of 100 (higher = more hearts) */
float diff_heart_drop_rate(Difficulty d) {
    switch (d) {
        case DIFF_EASY:   return 40.0f;
        case DIFF_HARD:   return 20.0f;
        default:          return 30.0f;  /* Normal: full=10, half=30 */
    }
}

float diff_shop_price_mult(Difficulty d) {
    switch (d) {
        case DIFF_EASY:   return 0.85f;
        case DIFF_HARD:   return 1.4f;
        default:          return 1.0f;
    }
}

/* ================================================================
 * Boss Pool System - Random boss selection matching original Isaac
 * ================================================================ */

/* Boss pools by difficulty tier.
 * Original Isaac design: earlier floors have easier bosses,
 * later floors draw from harder pools. Some bosses span tiers. */

typedef struct {
    EnemyType bosses[MAX_BOSS_POOL];
    int       count;
} BossPool;

static const BossPool boss_pools[BOSS_POOL_TIERS] = {
    /* BOSS_TIER_BASEMENT (Floors 0-1): Classic Basement bosses + Haunt/Widow/Pin.
     * Haunt and Widow added here for variety on early floors. */
    { { ENEMY_BOSS_DUKE, ENEMY_BOSS_MONSTRO, ENEMY_BOSS_LARRY, ENEMY_BOSS_GEMINI, ENEMY_BOSS_HAUNT, ENEMY_BOSS_PIN }, 6 },

    /* BOSS_TIER_CAVES (Floors 2-3): Mid-tier bosses. Add Peep, Gurdy. */
    { { ENEMY_BOSS_PEEP, ENEMY_BOSS_GURDY, ENEMY_BOSS_GEMINI, ENEMY_BOSS_LARRY, ENEMY_BOSS_FAMINE, ENEMY_BOSS_WIDOW }, 6 },

    /* BOSS_TIER_DEPTHS (Floor 4): Hardest standard bosses. */
    { { ENEMY_BOSS_FAMINE, ENEMY_BOSS_GURDY, ENEMY_BOSS_PEEP, ENEMY_BOSS_GEMINI, ENEMY_BOSS_MONSTRO, ENEMY_BOSS_PIN }, 6 },

    /* BOSS_TIER_WOMB (Floor 5): Late-game bosses with high HP. */
    { { ENEMY_BOSS_GURDY, ENEMY_BOSS_PEEP, ENEMY_BOSS_FAMINE, ENEMY_BOSS_HAUNT, ENEMY_BOSS_WIDOW, ENEMY_BOSS_PIN }, 6 },

    /* BOSS_TIER_SHEOL (Floor 6): Mega Satan only - final boss. */
    { { ENEMY_BOSS_MEGA_SATAN }, 1 },
};

/* Per-boss base HP values (before floor scaling).
 * Reflects each boss's innate difficulty:
 *   Duke: moderate (swarm boss, flies do the work)
 *   Monstro: tanky (big, slow, high damage)
 *   Larry Jr: moderate (segmented, hard to dodge)
 *   Gemini: moderate-high (two targets)
 *   Famine: high (fast charges, projectiles) */
static int boss_innate_hp(EnemyType boss) {
    switch (boss) {
    case ENEMY_BOSS_DUKE:       return 18;
    case ENEMY_BOSS_MONSTRO:    return 22;
    case ENEMY_BOSS_LARRY:      return 20;
    case ENEMY_BOSS_GEMINI:     return 20;
    case ENEMY_BOSS_FAMINE:     return 25;
    /* Phase 2 bosses */
    case ENEMY_BOSS_PEEP:       return 24;
    case ENEMY_BOSS_GURDY:      return 35;  /* tanky, stationary */
    case ENEMY_BOSS_PIN:        return 22;
    case ENEMY_BOSS_HAUNT:      return 28;  /* two phases */
    case ENEMY_BOSS_WIDOW:      return 23;
    case ENEMY_BOSS_MEGA_SATAN: return 60;  /* final boss */
    default:                    return 20;
    }
}

/* Calculate actual boss HP for a given floor.
 * Formula: innate_hp * floor_hp_scale + floor_base_bonus
 * This means the same boss gets harder on deeper floors. */
int get_boss_base_hp(EnemyType boss, int floor_num) {
    const FloorInfo *fi = get_floor_info(floor_num);
    int base = boss_innate_hp(boss);
    int scaled = (int)(base * fi->boss_hp_scale) + fi->enemy_hp_bonus * 2;
    /* Clamp to reasonable range */
    if (scaled < 15) scaled = 15;
    if (scaled > 80) scaled = 80;
    return scaled;
}

/* Push a boss into the recent history ring buffer */
void push_boss_history(Game *g, EnemyType boss) {
    /* Shift history back if full */
    if (g->boss_history_count >= BOSS_HISTORY_SIZE) {
        for (int i = 0; i < BOSS_HISTORY_SIZE - 1; i++) {
            g->boss_history[i] = g->boss_history[i + 1];
        }
        g->boss_history[BOSS_HISTORY_SIZE - 1] = boss;
    } else {
        g->boss_history[g->boss_history_count] = boss;
        g->boss_history_count++;
    }
}

/* Check if a boss was recently fought */
static int boss_in_history(Game *g, EnemyType boss) {
    for (int i = 0; i < g->boss_history_count; i++) {
        if (g->boss_history[i] == boss) return 1;
    }
    return 0;
}

/* Select a random boss from the appropriate pool for this floor.
 * Avoids repeating recently fought bosses when possible.
 * Returns the selected EnemyType. */
EnemyType select_boss_for_floor(Game *g, int floor_num) {
    const FloorInfo *fi = get_floor_info(floor_num);
    const BossPool *pool = &boss_pools[fi->boss_tier];

    /* Build a candidate list excluding recent history */
    EnemyType candidates[MAX_BOSS_POOL];
    int cand_count = 0;

    for (int i = 0; i < pool->count; i++) {
        if (!boss_in_history(g, pool->bosses[i])) {
            candidates[cand_count++] = pool->bosses[i];
        }
    }

    /* If all bosses were recently fought, allow any (shouldn't happen with
     * BOSS_HISTORY_SIZE=2 and pool_size=5, but safety fallback) */
    if (cand_count == 0) {
        cand_count = pool->count;
        for (int i = 0; i < pool->count; i++) {
            candidates[i] = pool->bosses[i];
        }
    }

    /* Weighted selection: bosses listed earlier in the pool for each tier
     * have slightly higher weight on that tier's floors.
     * Weight: first boss = 3, second = 3, third = 2, rest = 1 */
    int weights[MAX_BOSS_POOL];
    int total_weight = 0;
    for (int i = 0; i < cand_count; i++) {
        /* Find this candidate's position in the original pool for weighting */
        int pool_pos = -1;
        for (int j = 0; j < pool->count; j++) {
            if (pool->bosses[j] == candidates[i]) { pool_pos = j; break; }
        }
        if (pool_pos <= 1) weights[i] = 3;
        else if (pool_pos == 2) weights[i] = 2;
        else weights[i] = 1;
        total_weight += weights[i];
    }

    /* Weighted random pick */
    int roll = randi(0, total_weight - 1);
    int accum = 0;
    for (int i = 0; i < cand_count; i++) {
        accum += weights[i];
        if (roll < accum) {
            return candidates[i];
        }
    }

    /* Fallback (should never reach) */
    return candidates[0];
}

/* ================================================================
 * Colour palette
 * ================================================================ */
static u32 COL_BG, COL_WALL, COL_FLOOR, COL_PLAYER, COL_PLAYER_HIT;
static u32 COL_TEAR, COL_HEART_FULL, COL_HEART_EMPTY;
static u32 COL_ENEMY_FLY, COL_ENEMY_GAPER, COL_ENEMY_PACER;
static u32 COL_ENEMY_BOSS, COL_ENEMY_HIT, COL_TEXT, COL_HUD_BG;
static u32 COL_SHADOW, COL_DOOR, COL_DOOR_LOCKED, COL_TREASURE;
static u32 COL_MENU_SEL, COL_MENU_BG, COL_OBSTACLE;
static u32 COL_PEDESTAL, COL_ITEM_GLOW, COL_TRAPDOOR;
static u32 COL_HOMING_TEAR, COL_SPECTRAL_TEAR;

static void init_colours(void) {
    COL_BG            = C2D_Color32(30,  30,  30,  255);
    COL_WALL          = C2D_Color32(80,  60,  45,  255);
    COL_FLOOR         = C2D_Color32(140, 115, 90,  255);
    COL_PLAYER        = C2D_Color32(220, 200, 170, 255);
    COL_PLAYER_HIT    = C2D_Color32(255, 80,  80,  255);
    COL_TEAR          = C2D_Color32(100, 150, 255, 255);
    COL_HEART_FULL    = C2D_Color32(255, 50,  50,  255);
    COL_HEART_EMPTY   = C2D_Color32(80,  80,  80,  255);
    COL_ENEMY_FLY     = C2D_Color32(60,  60,  60,  255);
    COL_ENEMY_GAPER   = C2D_Color32(180, 80,  80,  255);
    COL_ENEMY_PACER   = C2D_Color32(100, 180, 80,  255);
    COL_ENEMY_BOSS    = C2D_Color32(200, 50,  50,  255);
    COL_ENEMY_HIT     = C2D_Color32(255, 255, 255, 255);
    COL_TEXT          = C2D_Color32(255, 255, 255, 255);
    COL_HUD_BG       = C2D_Color32(20,  20,  20,  255);
    COL_SHADOW        = C2D_Color32(0,   0,   0,   80);
    COL_DOOR          = C2D_Color32(160, 130, 60,  255);
    COL_DOOR_LOCKED   = C2D_Color32(100, 80,  40,  255);
    COL_TREASURE      = C2D_Color32(255, 215, 0,   255);
    COL_MENU_SEL      = C2D_Color32(255, 220, 100, 255);
    COL_MENU_BG       = C2D_Color32(40,  20,  20,  255);
    COL_OBSTACLE      = C2D_Color32(110, 95,  70,  255);
    COL_PEDESTAL      = C2D_Color32(200, 180, 140, 255);
    COL_ITEM_GLOW     = C2D_Color32(255, 255, 200, 120);
    COL_TRAPDOOR      = C2D_Color32(40,  30,  20,  255);
    COL_HOMING_TEAR   = C2D_Color32(200, 100, 255, 255);
    COL_SPECTRAL_TEAR = C2D_Color32(255, 255, 255, 160);
}

/* ================================================================
 * Current room helper
 * ================================================================ */

Room *current_room(Game *g) {
    return &g->dungeon.rooms[g->dungeon.cur_y][g->dungeon.cur_x];
}

/* ================================================================
 * Player stats calculation
 * ================================================================ */

void recalc_player_stats(Player *p) {
    /* Start from base stats - adjusted per character */
    p->stats.damage    = 1.0f;
    p->stats.speed     = PLAYER_BASE_SPEED;
    p->stats.fire_rate = 0.0f;   /* modifier; actual cooldown = BASE_TEAR_COOLDOWN - fire_rate*2 */
    p->stats.range     = TEAR_BASE_RANGE;
    p->stats.max_hp    = PLAYER_BASE_HP;
    p->stats.flags     = 0;
    p->stats.luck      = 0.0f;
    p->stats.shot_speed = 0;

    /* Character base modifiers */
    switch (p->character) {
        case CHAR_MAGDALENE:
            p->stats.max_hp = 8;          /* 4 hearts */
            p->stats.speed *= 0.85f;
            break;
        case CHAR_CAIN:
            p->stats.max_hp = 4;          /* 2 hearts */
            p->stats.damage *= 1.2f;
            p->stats.speed  *= 1.1f;
            p->stats.luck   += 1.0f;
            break;
        case CHAR_JUDAS:
            p->stats.max_hp = 2;          /* 1 heart - glass cannon */
            p->stats.damage *= 1.35f;
            break;
        case CHAR_ISAAC:
        default:
            break;
    }

    /* Apply all held items */
    for (int i = 0; i < p->item_count; i++) {
        const ItemDef *def = get_item_def(p->items[i]);
        if (!def) continue;
        p->stats.damage    += def->damage_bonus;
        p->stats.speed     += def->speed_bonus;
        p->stats.fire_rate += def->fire_rate_bonus;
        p->stats.range     += def->range_bonus * 30.0f;  /* scale range bonus */
        p->stats.max_hp    += def->hp_bonus;
        p->stats.flags     |= def->flags;
        if (def->type == ITEM_LUCKY_FOOT) p->stats.luck += 1.0f;
        if (def->type == ITEM_DEAD_CAT)   p->stats.max_hp = 2;  /* Dead Cat: max HP = 1 heart */
    }

    /* Book of Belial active damage buff */
    if (p->book_belial_dmg_timer > 0) p->stats.damage += 1.5f;

    /* Persistent pill stat bonuses (preserved across item pickups) */
    p->stats.speed     += p->pill_speed_bonus;
    p->stats.fire_rate += p->pill_fire_rate_bonus;
    p->stats.range     += p->pill_range_bonus;
    p->stats.luck      += p->pill_luck_bonus;
    p->stats.max_hp    += p->pill_max_hp_bonus;

    /* Clamp stats */
    if (p->stats.damage < 0.3f) p->stats.damage = 0.3f;
    if (p->stats.speed < 1.0f) p->stats.speed = 1.0f;
    if (p->stats.speed > 4.0f) p->stats.speed = 4.0f;
    if (p->stats.range < 60.0f) p->stats.range = 60.0f;
    if (p->stats.range > 300.0f) p->stats.range = 300.0f;
    if (p->stats.fire_rate < -3.0f) p->stats.fire_rate = -3.0f;
    if (p->stats.max_hp > PLAYER_MAX_HP_CAP) p->stats.max_hp = PLAYER_MAX_HP_CAP;
    if (p->stats.max_hp < 2) p->stats.max_hp = 2;
}

/* Damage absorption: holy_mantle blocks all, soul hearts absorb first.
 * Returns damage that actually reaches HP. */
static int player_absorb_dmg(Player *p, int dmg) {
    if (p->holy_mantle_active) {
        p->holy_mantle_active = 0;
        return 0;
    }
    while (dmg > 0 && p->soul_hp > 0) {
        p->soul_hp--;
        dmg--;
    }
    return dmg;
}

/* Check if player owns a particular item */
static int player_has_item(const Player *p, ItemType type) {
    for (int i = 0; i < p->item_count; i++) {
        if (p->items[i] == type) return 1;
    }
    return 0;
}

/* Check if player is dead. If lives > 0, respawn at full HP. Returns 1 if truly dead. */
static int player_check_death(Player *p) {
    if (p->hp > 0) return 0;
    if (p->lives > 0) {
        p->lives--;
        p->hp = p->stats.max_hp;
        return 0;
    }
    return 1;
}

void collect_item(Game *g, ItemType item) {
    Player *p = &g->player;
    if (p->item_count >= MAX_ITEMS_HELD) return;

    p->items[p->item_count++] = item;
    recalc_player_stats(p);

    /* Heal for HP bonus items */
    const ItemDef *def = get_item_def(item);
    if (def && def->hp_bonus > 0) {
        p->hp += def->hp_bonus;
        if (p->hp > p->stats.max_hp) p->hp = p->stats.max_hp;
    }

    /* Show pickup name + description on top screen */
    if (def && def->name) {
        snprintf(g->pickup_msg_text, sizeof(g->pickup_msg_text),
                 "%s - %s", def->name,
                 def->description ? def->description : "");
        g->pickup_msg_timer = 180;  /* 3 seconds at 60fps */
    }

    /* Dead Cat: grant 9 lives, max HP set to 1 (handled in recalc) */
    if (item == ITEM_DEAD_CAT) {
        p->lives = 8;          /* total lives = current + 8 = 9 */
        if (p->hp > 2) p->hp = 2;
    }
    /* Holy Mantle: shield is active per-room */
    if (item == ITEM_HOLY_MANTLE) {
        p->holy_mantle_active = 1;
    }
}

static int get_tear_cooldown(Player *p) {
    float cd = (float)BASE_TEAR_COOLDOWN - p->stats.fire_rate * 2.0f;
    if (cd < 3.0f) cd = 3.0f;
    if (cd > 30.0f) cd = 30.0f;
    return (int)cd;
}

/* ================================================================
 * Dungeon generation
 * ================================================================ */

static int dungeon_connected(Dungeon *d) {
    int visited[DUNGEON_H][DUNGEON_W];
    memset(visited, 0, sizeof(visited));

    int qx[MAX_ROOMS], qy[MAX_ROOMS];
    int head = 0, tail = 0;
    qx[tail] = d->start_x;
    qy[tail] = d->start_y;
    tail++;
    visited[d->start_y][d->start_x] = 1;
    int count = 1;

    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};

    while (head < tail) {
        int cx = qx[head], cy = qy[head];
        head++;
        for (int i = 0; i < 4; i++) {
            int nx = cx + dx[i], ny = cy + dy[i];
            if (nx >= 0 && nx < DUNGEON_W && ny >= 0 && ny < DUNGEON_H &&
                !visited[ny][nx] && d->rooms[ny][nx].type != ROOM_NONE) {
                visited[ny][nx] = 1;
                qx[tail] = nx;
                qy[tail] = ny;
                tail++;
                count++;
            }
        }
    }
    return count == d->room_count;
}

/* ── Path validation helpers for dungeon generation ── */

/* BFS from start to boss, traversing only rooms that do NOT require a key
 * (i.e., skipping ROOM_TREASURE rooms).  Returns 1 if boss is reachable. */
static int boss_reachable_without_keys(Dungeon *d) {
    int visited[DUNGEON_H][DUNGEON_W];
    memset(visited, 0, sizeof(visited));
    int qx[DUNGEON_W * DUNGEON_H], qy[DUNGEON_W * DUNGEON_H];
    int head = 0, tail = 0;

    qx[tail] = d->start_x;
    qy[tail] = d->start_y;
    tail++;
    visited[d->start_y][d->start_x] = 1;

    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};

    while (head < tail) {
        int cx = qx[head], cy = qy[head];
        head++;

        if (cx == d->boss_x && cy == d->boss_y) return 1;

        for (int i = 0; i < 4; i++) {
            int nx = cx + dx[i], ny = cy + dy[i];
            if (nx < 0 || nx >= DUNGEON_W || ny < 0 || ny >= DUNGEON_H) continue;
            if (visited[ny][nx]) continue;
            RoomType t = d->rooms[ny][nx].type;
            if (t == ROOM_NONE) continue;
            /* Skip locked room types — they block the free path */
            if (t == ROOM_TREASURE) continue;
            /* Secret rooms have hidden doors — skip */
            if (t == ROOM_SECRET) continue;
            visited[ny][nx] = 1;
            qx[tail] = nx;
            qy[tail] = ny;
            tail++;
        }
    }
    return 0; /* boss not reachable without keys */
}

/* Check if a room is a dead-end (has only 1 adjacent non-NONE room).
 * Dead-ends are safe spots for treasure rooms since they're optional branches. */
static int is_dead_end(Dungeon *d, int rx, int ry) {
    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};
    int adj = 0;
    for (int i = 0; i < 4; i++) {
        int nx = rx + dx[i], ny = ry + dy[i];
        if (nx >= 0 && nx < DUNGEON_W && ny >= 0 && ny < DUNGEON_H &&
            d->rooms[ny][nx].type != ROOM_NONE)
            adj++;
    }
    return adj <= 1;
}

/* Check if removing a room would disconnect the dungeon (i.e., it's
 * an articulation point / bridge node on the critical path).
 * We temporarily mark it as ROOM_NONE and check connectivity. */
static int is_on_critical_path(Dungeon *d, int rx, int ry) {
    /* Never block start or boss */
    if (rx == d->start_x && ry == d->start_y) return 1;
    if (rx == d->boss_x && ry == d->boss_y) return 1;

    RoomType saved = d->rooms[ry][rx].type;
    d->rooms[ry][rx].type = ROOM_NONE;
    d->room_count--;

    int connected = dungeon_connected(d);

    d->rooms[ry][rx].type = saved;
    d->room_count++;

    return !connected; /* if removing it breaks connectivity, it's critical */
}

/* Pick a random item that the player doesn't already have */
static ItemType pick_random_item(Game *g) {
    /* Build pool of items not yet collected */
    ItemType available[ITEM_COUNT];
    int avail_count = 0;
    for (int t = 1; t < ITEM_COUNT; t++) {
        int has = 0;
        for (int j = 0; j < g->player.item_count; j++) {
            if (g->player.items[j] == (ItemType)t) { has = 1; break; }
        }
        if (!has) available[avail_count++] = (ItemType)t;
    }
    if (avail_count == 0) return ITEM_PENTAGRAM; /* fallback */
    return available[randi(0, avail_count - 1)];
}

void dungeon_generate(Dungeon *d, int floor_num) {
    int attempts = 0;

    do {
        memset(d, 0, sizeof(Dungeon));
        d->room_count = 0;

        int sx = DUNGEON_W / 2, sy = DUNGEON_H / 2;
        d->start_x = sx;
        d->start_y = sy;
        d->rooms[sy][sx].type = ROOM_START;
        d->rooms[sy][sx].gx = sx;
        d->rooms[sy][sx].gy = sy;
        d->rooms[sy][sx].cleared = 1;
        d->room_count = 1;

        int cx = sx, cy = sy;
        int target = randi(MIN_ROOMS, MIN_ROOMS + 3);
        int steps = 0;
        int dxs[] = {0, 0, -1, 1};
        int dys[] = {-1, 1, 0, 0};

        while (d->room_count < target && steps < 200) {
            int dir = randi(0, 3);
            int nx = cx + dxs[dir], ny = cy + dys[dir];
            steps++;
            if (nx < 0 || nx >= DUNGEON_W || ny < 0 || ny >= DUNGEON_H) continue;

            if (d->rooms[ny][nx].type == ROOM_NONE) {
                d->rooms[ny][nx].type = ROOM_NORMAL;
                d->rooms[ny][nx].gx = nx;
                d->rooms[ny][nx].gy = ny;
                d->room_count++;
            }
            cx = nx;
            cy = ny;
        }

        /* Place boss room at furthest point */
        int best_dist = 0;
        int bx = sx, by = sy;
        for (int ry = 0; ry < DUNGEON_H; ry++) {
            for (int rx = 0; rx < DUNGEON_W; rx++) {
                if (d->rooms[ry][rx].type == ROOM_NORMAL) {
                    int dist = abs(rx - sx) + abs(ry - sy);
                    if (dist > best_dist) {
                        best_dist = dist;
                        bx = rx;
                        by = ry;
                    }
                }
            }
        }
        d->rooms[by][bx].type = ROOM_BOSS;
        d->boss_x = bx;
        d->boss_y = by;

        /* ── Place treasure room as an OPTIONAL side branch ──
         * Priority: 1) dead-end neighbor of start (safest — never blocks path)
         *           2) non-critical-path neighbor of start
         *           3) any non-critical ROOM_NORMAL on the map
         *           4) create a new room in a NONE neighbor of start (guaranteed) */
        int treasure_placed = 0;

        /* Pass 1: dead-end ROOM_NORMAL neighbor of start */
        for (int i = 0; i < 4 && !treasure_placed; i++) {
            int tx = sx + dxs[i], ty = sy + dys[i];
            if (tx >= 0 && tx < DUNGEON_W && ty >= 0 && ty < DUNGEON_H &&
                d->rooms[ty][tx].type == ROOM_NORMAL &&
                is_dead_end(d, tx, ty)) {
                d->rooms[ty][tx].type = ROOM_TREASURE;
                treasure_placed = 1;
            }
        }
        /* Pass 2: non-critical ROOM_NORMAL neighbor of start */
        for (int i = 0; i < 4 && !treasure_placed; i++) {
            int tx = sx + dxs[i], ty = sy + dys[i];
            if (tx >= 0 && tx < DUNGEON_W && ty >= 0 && ty < DUNGEON_H &&
                d->rooms[ty][tx].type == ROOM_NORMAL &&
                !is_on_critical_path(d, tx, ty)) {
                d->rooms[ty][tx].type = ROOM_TREASURE;
                treasure_placed = 1;
            }
        }
        /* Pass 3: any non-critical ROOM_NORMAL on the map (preferring dead-ends) */
        if (!treasure_placed) {
            /* First try dead-ends anywhere */
            for (int ry = 0; ry < DUNGEON_H && !treasure_placed; ry++) {
                for (int rx = 0; rx < DUNGEON_W && !treasure_placed; rx++) {
                    if (d->rooms[ry][rx].type == ROOM_NORMAL &&
                        is_dead_end(d, rx, ry)) {
                        d->rooms[ry][rx].type = ROOM_TREASURE;
                        treasure_placed = 1;
                    }
                }
            }
            /* Then try any non-critical room */
            for (int ry = 0; ry < DUNGEON_H && !treasure_placed; ry++) {
                for (int rx = 0; rx < DUNGEON_W && !treasure_placed; rx++) {
                    if (d->rooms[ry][rx].type == ROOM_NORMAL &&
                        !is_on_critical_path(d, rx, ry)) {
                        d->rooms[ry][rx].type = ROOM_TREASURE;
                        treasure_placed = 1;
                    }
                }
            }
        }
        /* Pass 4: create a new room in a NONE neighbor of start */
        if (!treasure_placed) {
            for (int i = 0; i < 4 && !treasure_placed; i++) {
                int tx = sx + dxs[i], ty = sy + dys[i];
                if (tx >= 0 && tx < DUNGEON_W && ty >= 0 && ty < DUNGEON_H &&
                    d->rooms[ty][tx].type == ROOM_NONE) {
                    d->rooms[ty][tx].type = ROOM_TREASURE;
                    d->rooms[ty][tx].gx = tx;
                    d->rooms[ty][tx].gy = ty;
                    d->room_count++;
                    treasure_placed = 1;
                }
            }
        }

        /* ── Second treasure room on later floors (also non-critical) ── */
        if (floor_num >= 2) {
            for (int ry = 0; ry < DUNGEON_H; ry++) {
                for (int rx = 0; rx < DUNGEON_W; rx++) {
                    if (d->rooms[ry][rx].type == ROOM_NORMAL) {
                        int dist_to_boss = abs(rx - bx) + abs(ry - by);
                        if (dist_to_boss > 1 && !is_on_critical_path(d, rx, ry)) {
                            d->rooms[ry][rx].type = ROOM_TREASURE;
                            goto done_second_treasure;
                        }
                    }
                }
            }
            done_second_treasure:;
        }

        /* ── Place shop room (non-critical path only) ── */
        for (int ry = 0; ry < DUNGEON_H; ry++) {
            for (int rx = 0; rx < DUNGEON_W; rx++) {
                if (d->rooms[ry][rx].type == ROOM_NORMAL) {
                    int dist_start = abs(rx - sx) + abs(ry - sy);
                    int dist_boss  = abs(rx - bx) + abs(ry - by);
                    if (dist_start >= 2 && dist_boss > 1) {
                        d->rooms[ry][rx].type = ROOM_SHOP;
                        goto done_shop;
                    }
                }
            }
        }
        done_shop:;

        /* ── Place secret room adjacent to 2+ rooms, doors initially hidden ── */
        for (int ry = 1; ry < DUNGEON_H - 1; ry++) {
            for (int rx = 1; rx < DUNGEON_W - 1; rx++) {
                if (d->rooms[ry][rx].type != ROOM_NONE) continue;
                int adj = 0;
                if (d->rooms[ry - 1][rx].type != ROOM_NONE) adj++;
                if (d->rooms[ry + 1][rx].type != ROOM_NONE) adj++;
                if (d->rooms[ry][rx - 1].type != ROOM_NONE) adj++;
                if (d->rooms[ry][rx + 1].type != ROOM_NONE) adj++;
                if (adj >= 2) {
                    d->rooms[ry][rx].type = ROOM_SECRET;
                    d->rooms[ry][rx].gx = rx;
                    d->rooms[ry][rx].gy = ry;
                    d->rooms[ry][rx].cleared = 1;
                    d->room_count++;
                    goto done_secret;
                }
            }
        }
        done_secret:;

        /* ── Place curse room (non-critical path only) ── */
        if (floor_num >= 1) {
            for (int ry = 0; ry < DUNGEON_H; ry++) {
                for (int rx = 0; rx < DUNGEON_W; rx++) {
                    if (d->rooms[ry][rx].type == ROOM_NORMAL) {
                        int dist_start = abs(rx - sx) + abs(ry - sy);
                        if (dist_start >= 2 && !is_on_critical_path(d, rx, ry)) {
                            d->rooms[ry][rx].type = ROOM_CURSE;
                            goto done_curse;
                        }
                    }
                }
            }
            /* Fallback: allow curse on critical path if no other option */
            for (int ry = 0; ry < DUNGEON_H; ry++) {
                for (int rx = 0; rx < DUNGEON_W; rx++) {
                    if (d->rooms[ry][rx].type == ROOM_NORMAL) {
                        int dist_start = abs(rx - sx) + abs(ry - sy);
                        if (dist_start >= 2) {
                            d->rooms[ry][rx].type = ROOM_CURSE;
                            goto done_curse;
                        }
                    }
                }
            }
            done_curse:;
        }

        /* ── VALIDATE: boss must be reachable without keys ── */
        if (!boss_reachable_without_keys(d)) {
            /* Layout is softlocked — retry generation */
            attempts++;
            continue;
        }

        attempts++;
    } while ((!dungeon_connected(d) || d->room_count < MIN_ROOMS ||
              !boss_reachable_without_keys(d)) && attempts < 50);

    /* Set up doors */
    int dxs2[] = {0, 0, -1, 1};
    int dys2[] = {-1, 1, 0, 0};
    for (int ry = 0; ry < DUNGEON_H; ry++) {
        for (int rx = 0; rx < DUNGEON_W; rx++) {
            Room *r = &d->rooms[ry][rx];
            if (r->type == ROOM_NONE) continue;
            for (int i = 0; i < 4; i++) {
                int nx = rx + dxs2[i], ny = ry + dys2[i];
                if (nx >= 0 && nx < DUNGEON_W && ny >= 0 && ny < DUNGEON_H &&
                    d->rooms[ny][nx].type != ROOM_NONE) {
                    /* Secret rooms: doors are hidden until bombed */
                    if (r->type == ROOM_SECRET || d->rooms[ny][nx].type == ROOM_SECRET) {
                        r->doors[i] = 0;  /* hidden */
                    } else {
                        r->doors[i] = 1;
                    }
                    /* Mark doors to treasure rooms as key-locked */
                    if (d->rooms[ny][nx].type == ROOM_TREASURE) {
                        r->door_locked[i] = 1;
                        r->door_type[i] = 1; /* key door */
                    }
                    /* Mark doors to curse rooms */
                    if (d->rooms[ny][nx].type == ROOM_CURSE) {
                        r->door_type[i] = 4; /* curse door */
                    }
                }
            }
        }
    }

    d->cur_x = d->start_x;
    d->cur_y = d->start_y;
    d->rooms[d->start_y][d->start_x].visited = 1;
}

/* ================================================================
 * Room enemy spawning
 * ================================================================ */

static void place_obstacles(Room *r) {
    if (r->type == ROOM_START || r->type == ROOM_TREASURE ||
        r->type == ROOM_SHOP || r->type == ROOM_SECRET) return;

    int count = randi(0, 4);
    r->obstacle_count = 0;
    for (int i = 0; i < count && i < MAX_OBSTACLES; i++) {
        Obstacle *o = &r->obstacles[i];
        o->x = randf(ROOM_LEFT + 50, ROOM_RIGHT - 50);
        o->y = randf(ROOM_TOP + 50, ROOM_BOTTOM - 50);
        o->active = 1;
        r->obstacle_count++;
    }
}

/* Spawn a consumable pickup at given position */
void spawn_consumable(Room *r, float x, float y, PickupType type) {
    for (int i = 0; i < MAX_CONSUMABLE_PICKUPS; i++) {
        if (!r->consumables[i].active) {
            ConsumablePickup *c = &r->consumables[i];
            c->x = x + randf(-6, 6);
            c->y = y + randf(-6, 6);
            c->type = type;
            c->active = 1;
            c->anim_timer = randi(0, 60);
            r->consumable_count++;
            return;
        }
    }
}

/* Spawn random consumable drop (weighted) */
static void spawn_random_consumable(Room *r, float x, float y) {
    int roll = randi(0, 100);
    if (roll < 40) {
        spawn_consumable(r, x, y, PICKUP_COIN);
    } else if (roll < 65) {
        spawn_consumable(r, x, y, PICKUP_BOMB);
    } else if (roll < 85) {
        spawn_consumable(r, x, y, PICKUP_KEY);
    } else if (roll < 95) {
        spawn_consumable(r, x, y, PICKUP_COIN5);
    } else {
        spawn_consumable(r, x, y, PICKUP_BOMB2);
    }
}

void room_spawn_enemies(Game *g, Room *r) {
    if (r->enemies_spawned) return;
    r->enemies_spawned = 1;

    /* Initialize heart pickups */
    r->heart_count = 0;
    for (int i = 0; i < MAX_HEART_PICKUPS; i++) {
        r->hearts[i].active = 0;
    }
    /* Initialize consumable pickups */
    r->consumable_count = 0;
    for (int i = 0; i < MAX_CONSUMABLE_PICKUPS; i++) {
        r->consumables[i].active = 0;
    }

    place_obstacles(r);

    const FloorInfo *fi = get_floor_info(g->current_floor);

    if (r->type == ROOM_START) {
        r->enemy_count = 0;
        r->cleared = 1;
        return;
    }

    if (r->type == ROOM_TREASURE) {
        r->enemy_count = 0;
        r->cleared = 1;
        /* Place an item pedestal */
        r->pedestal.x = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
        r->pedestal.y = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
        r->pedestal.item = pick_random_item(g);
        r->pedestal.active = 1;
        return;
    }

    if (r->type == ROOM_SHOP) {
        r->enemy_count = 0;
        r->cleared = 1;
        /* Populate shop items (no duplicates within same shop) */
        r->shop_count = randi(2, MAX_SHOP_ITEMS);
        float shopX_start = ROOM_LEFT + 60;
        float shopSpacing = (ROOM_RIGHT - ROOM_LEFT - 120) / (float)(r->shop_count > 1 ? r->shop_count - 1 : 1);
        ItemType shop_used[MAX_SHOP_ITEMS];
        for (int i = 0; i < r->shop_count; i++) {
            ShopItem *si = &r->shop_items[i];
            si->x = shopX_start + i * shopSpacing;
            si->y = (ROOM_TOP + ROOM_BOTTOM) / 2.0f - 10;
            /* Pick item not already in this shop */
            int tries = 0;
            do {
                si->item = pick_random_item(g);
                int dup = 0;
                for (int j = 0; j < i; j++) {
                    if (shop_used[j] == si->item) { dup = 1; break; }
                }
                if (!dup) break;
                tries++;
            } while (tries < 20);
            shop_used[i] = si->item;
            int base_cost = randi(SHOP_ITEM_COST_MIN, SHOP_ITEM_COST_MAX);
            si->cost = (int)(base_cost * diff_shop_price_mult(g->difficulty));
            if (si->cost < 1) si->cost = 1;
            si->active = 1;
        }
        /* Sprinkle a free pill/card in the shop as additional loot */
        if (randi(0, 100) < 60) {
            spawn_pill_pickup(r, ROOM_LEFT + 40, ROOM_BOTTOM - 40, randi(0, PILL_EFFECT_COUNT - 1));
        }
        if (randi(0, 100) < 60) {
            spawn_card_pickup(r, ROOM_RIGHT - 40, ROOM_BOTTOM - 40, randi(0, TAROT_COUNT - 1));
        }
        return;
    }

    if (r->type == ROOM_SECRET) {
        r->enemy_count = 0;
        r->cleared = 1;
        /* Secret rooms always have good loot */
        spawn_consumable(r, (ROOM_LEFT + ROOM_RIGHT) / 2.0f - 15,
                         (ROOM_TOP + ROOM_BOTTOM) / 2.0f, PICKUP_BOMB2);
        spawn_consumable(r, (ROOM_LEFT + ROOM_RIGHT) / 2.0f + 15,
                         (ROOM_TOP + ROOM_BOTTOM) / 2.0f, PICKUP_COIN5);
        spawn_consumable(r, (ROOM_LEFT + ROOM_RIGHT) / 2.0f,
                         (ROOM_TOP + ROOM_BOTTOM) / 2.0f - 15, PICKUP_KEY);
        return;
    }

    if (r->type == ROOM_CURSE) {
        r->enemy_count = 0;
        r->cleared = 1;
        /* Curse rooms have a good item pedestal */
        r->pedestal.x = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
        r->pedestal.y = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
        r->pedestal.item = pick_random_item(g);
        r->pedestal.active = 1;
        /* Also spawn some pickups */
        spawn_consumable(r, ROOM_LEFT + 60, ROOM_TOP + 60, PICKUP_COIN5);
        spawn_consumable(r, ROOM_RIGHT - 60, ROOM_TOP + 60, PICKUP_KEY);
        return;
    }

    int count;
    if (r->type == ROOM_BOSS) {
        count = 1;
    } else {
        count = randi(2, 4 + g->current_floor);
        if (count > MAX_ENEMIES) count = MAX_ENEMIES;
    }

    r->enemy_count = 0;
    float cx = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
    float cy = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;

    for (int i = 0; i < count; i++) {
        Enemy *e = &r->enemies[i];
        memset(e, 0, sizeof(Enemy));
        e->active = 1;

        float px, py;
        int safety = 0;
        do {
            px = randf(ROOM_LEFT + 30, ROOM_RIGHT - 30);
            py = randf(ROOM_TOP + 30, ROOM_BOTTOM - 30);
            safety++;
        } while (fabsf(px - cx) < 50 && fabsf(py - cy) < 50 && safety < 20);

        e->x = px;
        e->y = py;

        if (r->type == ROOM_BOSS) {
            /* === Random Boss Selection from Pool === */
            EnemyType selected_boss = select_boss_for_floor(g, g->current_floor);
            int boss_hp = get_boss_base_hp(selected_boss, g->current_floor);

            /* Store the selected boss type and record in history */
            g->current_boss_type = selected_boss;
            push_boss_history(g, selected_boss);

            e->type = selected_boss;
            e->hp = boss_hp;
            e->max_hp = boss_hp;
            e->x = cx;
            e->y = ROOM_TOP + 60;
            e->timer = randi(60, 120);
            e->shoot_timer = randi(40, 80);

            /* Boss-specific initialization */
            switch (selected_boss) {
            case ENEMY_BOSS_MONSTRO:
                e->phase = 0;       /* 0=idle, 1=jumping, 2=landing_tear_spread */
                e->jump_z = 0;
                e->jump_vz = 0;
                e->attack_pattern = 0;
                break;
            case ENEMY_BOSS_LARRY:
                e->seg_count = MAX_LARRY_SEGMENTS;
                e->seg_speed = 1.8f * fi->boss_speed_scale;
                e->dx = 1.8f * fi->boss_speed_scale;
                e->dy = 0.5f;
                for (int s = 0; s < MAX_LARRY_SEGMENTS; s++) {
                    e->segments[s].x = e->x - (s + 1) * LARRY_SEG_DIST;
                    e->segments[s].y = e->y;
                    e->segments[s].prev_x = e->segments[s].x;
                    e->segments[s].prev_y = e->segments[s].y;
                }
                break;
            case ENEMY_BOSS_DUKE:
                e->orbit_phase = 0;
                e->timer = DUKE_SPAWN_INTERVAL;
                break;
            case ENEMY_BOSS_GEMINI:
                e->gemini_split = 0;
                e->gemini_cx = e->x + 35;
                e->gemini_cy = e->y;
                e->gemini_cdx = 0;
                e->gemini_cdy = 0;
                e->gemini_chp = boss_hp / 3;
                break;
            case ENEMY_BOSS_FAMINE:
                e->phase = 0;       /* 0=galloping, 1=headless */
                e->famine_shoot_cd = randi(40, 80);
                e->dx = FAMINE_CHARGE_SPEED * fi->boss_speed_scale;
                break;
            /* --- Phase 2 bosses --- */
            case ENEMY_BOSS_PEEP:
                e->phase = 0;            /* 0=full, 1=detach phase (below 50% HP) */
                e->dx = 1.8f * fi->boss_speed_scale;
                e->dy = 1.5f * fi->boss_speed_scale;
                e->timer = 120;
                e->attack_pattern = 0;   /* eye spawn counter */
                break;
            case ENEMY_BOSS_GURDY:
                e->phase = 0;            /* 0=idle, 1=spawn wave, 2=shoot spread */
                e->dx = 0; e->dy = 0;    /* stationary */
                e->timer = 60;
                e->shoot_timer = 60;
                e->attack_pattern = 0;   /* alternates between attacks */
                break;
            case ENEMY_BOSS_PIN:
                e->phase = 1;            /* 0=above ground (vulnerable) 1=burrowed */
                e->timer = 90;           /* time burrowed */
                e->target_x = e->x;
                e->target_y = e->y;
                e->dx = 0; e->dy = 0;
                e->shoot_timer = 30;
                break;
            case ENEMY_BOSS_HAUNT:
                e->phase = 0;            /* 0=Lil Haunts phase, 1=brimstone phase */
                e->dx = 1.0f * fi->boss_speed_scale;
                e->dy = 0.8f * fi->boss_speed_scale;
                e->timer = 60;
                e->shoot_timer = 120;
                e->attack_pattern = 0;
                break;
            case ENEMY_BOSS_WIDOW:
                e->phase = 0;            /* 0=idle, 1=jumping */
                e->jump_z = 0;
                e->jump_vz = 0;
                e->target_x = e->x;
                e->target_y = e->y;
                e->timer = 60;
                e->shoot_timer = 90;
                break;
            case ENEMY_BOSS_MEGA_SATAN:
                e->phase = 0;            /* 0=stomp, 1=brimstone, 2=fireball spread */
                e->dx = 0; e->dy = 0;    /* mostly stationary */
                e->timer = 90;
                e->shoot_timer = 60;
                e->attack_pattern = 0;
                break;
            default: break;
            }
        } else {
            /* Floor-tiered enemy pool selection */
            int t;
            if (g->current_floor >= 3) {
                t = randi(0, 16);  /* all enemy types available */
            } else if (g->current_floor >= 2) {
                t = randi(0, 10);  /* + tier 2 (Globin, Boom Fly, Maw, Mulligan) */
            } else if (g->current_floor >= 1) {
                t = randi(0, 6);   /* + tier 1 (Attack Fly, Pooter, Hopper, Baby) */
            } else {
                t = randi(0, 4);   /* base: Fly, Gaper, Pacer, Spider, Clotty */
            }

            switch (t) {
            /* --- Base enemies (Basement I+) --- */
            case 0:
                e->type = ENEMY_FLY;
                e->hp = 3 + fi->enemy_hp_bonus;
                e->max_hp = e->hp;
                e->timer = randi(30, 90);
                e->dx = randf(-1.0f, 1.0f) * fi->enemy_speed_mult;
                e->dy = randf(-1.0f, 1.0f) * fi->enemy_speed_mult;
                break;
            case 1:
                e->type = ENEMY_GAPER;
                e->hp = 4 + fi->enemy_hp_bonus;
                e->max_hp = e->hp;
                break;
            case 2:
                e->type = ENEMY_PACER;
                e->hp = 3 + fi->enemy_hp_bonus;
                e->max_hp = e->hp;
                e->dx = (randi(0, 1) == 0) ? 1.2f : -1.2f;
                e->dx *= fi->enemy_speed_mult;
                e->dy = 0;
                break;
            case 3:
                e->type = ENEMY_SPIDER;
                e->hp = 3 + fi->enemy_hp_bonus;
                e->max_hp = e->hp;
                e->timer = randi(10, 30);
                e->dx = randf(-2.0f, 2.0f) * fi->enemy_speed_mult;
                e->dy = randf(-2.0f, 2.0f) * fi->enemy_speed_mult;
                break;
            case 4:
                e->type = ENEMY_CLOTTY;
                e->hp = 5 + fi->enemy_hp_bonus;
                e->max_hp = e->hp;
                e->timer = randi(60, 120);
                e->shoot_timer = randi(30, 70);
                break;
            /* --- Tier 1: Basement II+ --- */
            case 5:
                e->type = ENEMY_ATTACK_FLY;
                e->hp = 4 + fi->enemy_hp_bonus;
                e->max_hp = e->hp;
                e->timer = randi(30, 60);
                e->dx = randf(-1.0f, 1.0f) * fi->enemy_speed_mult;
                e->dy = randf(-1.0f, 1.0f) * fi->enemy_speed_mult;
                break;
            case 6:
                e->type = ENEMY_POOTER;
                e->hp = 5 + fi->enemy_hp_bonus;
                e->max_hp = e->hp;
                e->timer = randi(60, 120);
                e->shoot_timer = randi(40, 80);
                break;
            /* --- Tier 2: Caves+ --- */
            case 7:
                e->type = ENEMY_HOPPER;
                e->hp = 4 + fi->enemy_hp_bonus;
                e->max_hp = e->hp;
                e->timer = randi(20, 50);
                e->jump_arc = 0.0f;
                break;
            case 8:
                e->type = ENEMY_BABY;
                e->hp = 3 + fi->enemy_hp_bonus;
                e->max_hp = e->hp;
                break;
            case 9:
                e->type = ENEMY_GLOBIN;
                e->hp = 8 + fi->enemy_hp_bonus;
                e->max_hp = e->hp;
                e->state = 0;
                e->regen_timer = 0;
                break;
            case 10:
                e->type = ENEMY_BOOM_FLY;
                e->hp = 4 + fi->enemy_hp_bonus;
                e->max_hp = e->hp;
                e->dx = randf(-0.5f, 0.5f) * fi->enemy_speed_mult;
                e->dy = randf(-0.5f, 0.5f) * fi->enemy_speed_mult;
                break;
            /* --- Tier 3: Caves II / Depths --- */
            case 11:
                e->type = ENEMY_MAW;
                e->hp = 6 + fi->enemy_hp_bonus;
                e->max_hp = e->hp;
                e->shoot_timer = randi(40, 80);
                break;
            case 12:
                e->type = ENEMY_MULLIGAN;
                e->hp = 5 + fi->enemy_hp_bonus;
                e->max_hp = e->hp;
                e->shoot_timer = randi(50, 100);
                break;
            case 13:
                e->type = ENEMY_HOST;
                e->hp = 4 + fi->enemy_hp_bonus;
                e->max_hp = e->hp;
                e->hidden = 1;
                e->timer = randi(60, 120);
                break;
            case 14:
                e->type = ENEMY_RED_MAW;
                e->hp = 5 + fi->enemy_hp_bonus;
                e->max_hp = e->hp;
                e->shoot_timer = randi(20, 50);
                break;
            case 15:
                e->type = ENEMY_LEAPER;
                e->hp = 6 + fi->enemy_hp_bonus;
                e->max_hp = e->hp;
                e->timer = randi(30, 60);
                e->jump_arc = 0.0f;
                break;
            case 16:
                e->type = ENEMY_VIS;
                e->hp = 7 + fi->enemy_hp_bonus;
                e->max_hp = e->hp;
                e->timer = randi(40, 80);
                e->shoot_timer = randi(60, 120);
                break;
            }
        }

        /* Apply difficulty + infinite loop scaling to HP */
        float hp_mult = diff_enemy_hp_mult(g->difficulty);
        /* Infinite mode: each loop adds 25% enemy HP */
        if (g->game_mode == MODE_INFINITE && g->infinite_loop > 0) {
            hp_mult *= 1.0f + g->infinite_loop * 0.25f;
        }
        if (hp_mult != 1.0f) {
            e->hp = (int)(e->hp * hp_mult);
            if (e->hp < 1) e->hp = 1;
            e->max_hp = e->hp;
        }

        /* Champion / Elite roll: 15% chance, non-bosses only */
        if (!is_boss_type(e->type)) {
            int roll = randi(0, 99);
            if (roll < 15) {
                /* Pick one of 4 champion types */
                ChampionType ct = (ChampionType)(CHAMP_RED + randi(0, 3));
                enemy_make_champion(e, ct);
            }
        }

        r->enemy_count++;
    }
}

/* Apply champion stats to an enemy */
void enemy_make_champion(Enemy *e, ChampionType c) {
    e->champion = c;
    switch (c) {
        case CHAMP_RED:
            e->hp = e->max_hp = (int)(e->max_hp * 2.0f);
            break;
        case CHAMP_BLUE:
            e->hp = e->max_hp = (int)(e->max_hp * 1.5f);
            e->dx *= 1.3f;
            e->dy *= 1.3f;
            break;
        case CHAMP_YELLOW:
            e->hp = e->max_hp = (int)(e->max_hp * 1.5f);
            e->creep_drop_timer = 30;
            break;
        case CHAMP_BLACK:
            e->hp = e->max_hp = (int)(e->max_hp * 2.0f);
            e->split_pending = 1;  /* marks split-on-death */
            break;
        case CHAMP_NONE:
        default:
            break;
    }
}

/* ================================================================
 * Game initialisation
 * ================================================================ */

void game_init(Game *g) {
    memset(g, 0, sizeof(Game));
    g->state = STATE_MENU;
    g->menu_sel = 0;
    g->player.hp = PLAYER_BASE_HP;
    g->player.item_count = 0;
    recalc_player_stats(&g->player);
    g->player.x = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
    g->player.y = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;

    /* Start title music */
    music_play(MUS_TITLE);
}

/* Map floor number to the appropriate background music track */
static MusicId music_for_floor(int floor_num) {
    if (floor_num <= 1) return MUS_BASEMENT;  /* Floors 0-1: Basement */
    if (floor_num <= 3) return MUS_CAVES;     /* Floors 2-3: Caves */
    if (floor_num == 4) return MUS_DEPTHS;    /* Floor 4:   Depths */
    /* Floors 5 (Womb) and 6 (Sheol): reuse Depths music for now */
    return MUS_DEPTHS;
}

void start_new_game(Game *g) {
    /* Preserve settings selected in menus */
    GameMode  saved_mode = g->game_mode;
    Difficulty saved_diff = g->difficulty;
    CharacterType saved_char = g->selected_character;

    memset(g, 0, sizeof(Game));

    g->game_mode = saved_mode;
    g->difficulty = saved_diff;
    g->selected_character = saved_char;
    g->state = STATE_PLAYING;
    g->score = 0;
    g->current_floor = 0;
    g->infinite_loop = 0;
    g->best_floor = 0;
    g->rooms_cleared = 0;
    g->kills = 0;
    g->play_time_frames = 0;

    /* Initialize player character */
    g->player.character = g->selected_character;

    /* Base HP + easy mode bonus */
    g->player.hp = PLAYER_BASE_HP;
    if (g->difficulty == DIFF_EASY) {
        g->player.hp += 2;  /* extra heart for easy mode */
    }
    g->player.item_count = 0;
    /* Starting consumables */
    g->player.bombs = 1;
    g->player.keys = 1;
    g->player.coins = 0;
    g->player.lives = 0;
    g->player.holy_mantle_active = 0;
    g->player.book_belial_dmg_timer = 0;
    g->player.yum_heart_cd = 0;
    g->player.soul_hp = 0;
    g->player.has_pill = 0;
    g->player.has_card = 0;

    /* Apply character starting bonuses (items + adjust HP to max) */
    apply_character_start(g);

    recalc_player_stats(&g->player);

    /* Set HP to max for character */
    g->player.hp = g->player.stats.max_hp;
    if (g->difficulty == DIFF_EASY) {
        g->player.hp += 2;
        if (g->player.hp > PLAYER_MAX_HP_CAP) g->player.hp = PLAYER_MAX_HP_CAP;
        if (g->player.hp > g->player.stats.max_hp) g->player.stats.max_hp = g->player.hp;
    }

    /* Randomize pill color->effect mapping for this run */
    for (int i = 0; i < PILL_EFFECT_COUNT; i++) g->pill_color_map[i] = i;
    for (int i = PILL_EFFECT_COUNT - 1; i > 0; i--) {
        int j = randi(0, i);
        int tmp = g->pill_color_map[i];
        g->pill_color_map[i] = g->pill_color_map[j];
        g->pill_color_map[j] = tmp;
    }
    for (int i = 0; i < PILL_EFFECT_COUNT; i++) g->pill_known[i] = 0;

    dungeon_generate(&g->dungeon, g->current_floor);

    g->player.x = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
    g->player.y = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;

    Room *r = current_room(g);
    room_spawn_enemies(g, r);

    /* Roll a curse for the starting floor (30% chance) */
    roll_curse(g);

    /* Track total runs started in persistent config */
    g_config.total_runs_started++;
    config_save(&g_config);

    /* Switch to floor-appropriate music */
    music_play(music_for_floor(g->current_floor));
}

/* Apply per-character starting items */
void apply_character_start(Game *g) {
    Player *p = &g->player;
    switch (p->character) {
        case CHAR_MAGDALENE:
            /* +1 heart container, Yum Heart starter */
            p->items[p->item_count++] = ITEM_YUM_HEART;
            break;
        case CHAR_CAIN:
            /* Lucky Foot starter, missing eye visual */
            p->items[p->item_count++] = ITEM_LUCKY_FOOT;
            break;
        case CHAR_JUDAS:
            /* Book of Belial starter */
            p->items[p->item_count++] = ITEM_BOOK_OF_BELIAL;
            break;
        case CHAR_ISAAC:
        default:
            break;
    }
}

/* Advance to next floor */
void advance_floor(Game *g) {
    g->current_floor++;

    /* Track best floor for infinite mode high score */
    if (g->current_floor > g->best_floor) {
        g->best_floor = g->current_floor;
    }

    if (g->current_floor >= MAX_FLOORS) {
        if (g->game_mode == MODE_INFINITE) {
            /* Infinite mode: loop back to floor 0 with increased scaling */
            g->current_floor = 0;
            g->infinite_loop++;
        } else {
            /* Story mode: game won - beat all floors! */
            g->state = STATE_WIN;
            unlock_check_after_win(g);
            return;
        }
    }

    /* Track deepest floor reached - persist to save file */
    if (g->current_floor > g_config.floors_reached) {
        g_config.floors_reached = g->current_floor;
        config_save(&g_config);
    }

    g->state = STATE_FLOOR_TRANSITION;
    g->floor_transition_timer = 120;  /* 2 seconds */
}

static void finish_floor_transition(Game *g) {
    g->state = STATE_PLAYING;

    /* Generate new dungeon for new floor */
    dungeon_generate(&g->dungeon, g->current_floor);

    /* Reset tears and enemy shots */
    for (int i = 0; i < MAX_TEARS; i++)
        g->tears[i].active = 0;
    for (int i = 0; i < MAX_ENEMY_SHOTS; i++)
        g->enemy_shots[i].active = 0;

    /* Clear blood particles */
    for (int i = 0; i < MAX_BLOOD_PARTICLES; i++)
        g->blood[i].active = 0;

    /* Reset bomb */
    g->bomb_timer = 0;

    /* Reset boss state */
    g->boss_active = 0;
    g->boss_death_anim = 0;
    g->boss_intro_timer = 0;

    /* Place player in center */
    g->player.x = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
    g->player.y = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
    g->player.iframes = 30;

    /* Safety: guarantee at least 1 key per floor so locked doors
     * are never a permanent softlock even in edge cases */
    if (g->player.keys < 1) g->player.keys = 1;

    Room *r = current_room(g);
    room_spawn_enemies(g, r);

    /* Roll a possible curse for this floor (30% chance) */
    roll_curse(g);

    /* Play floor-appropriate music (crossfades if track changes) */
    music_play(music_for_floor(g->current_floor));
}

/* ================================================================
 * Player update
 * ================================================================ */

/* Check if a circle at (x,y) with given radius overlaps any obstacle in the room */
static int player_blocked_at(Room *r, float x, float y) {
    if (x < ROOM_LEFT + PLAYER_SIZE) return 1;
    if (x > ROOM_RIGHT - PLAYER_SIZE) return 1;
    if (y < ROOM_TOP + PLAYER_SIZE) return 1;
    if (y > ROOM_BOTTOM - PLAYER_SIZE) return 1;
    for (int i = 0; i < r->obstacle_count; i++) {
        Obstacle *o = &r->obstacles[i];
        if (!o->active) continue;
        float ox = x - o->x, oy = y - o->y;
        float md = PLAYER_SIZE + OBSTACLE_SIZE * 0.5f;
        if (ox * ox + oy * oy < md * md) return 1;
    }
    return 0;
}

void player_update(Game *g, u32 kHeld, circlePosition circlePos) {
    Player *p = &g->player;
    float spd = p->stats.speed;  /* max speed from stats (item-modified) */

    /* ── Timers ── */
    if (p->iframes > 0) p->iframes--;
    if (p->tear_cooldown > 0) p->tear_cooldown--;
    if (p->kb_timer > 0) p->kb_timer--;
    if (p->shoot_anim > 0) p->shoot_anim--;
    if (p->pickup_anim > 0) p->pickup_anim--;
    if (g->pickup_flash > 0) g->pickup_flash--;

    /* ══════════════════════════════════════════════════════════
     * 1. READ INPUT — 8-directional D-pad + analog Circle Pad
     * ══════════════════════════════════════════════════════════ */
    float ix = 0.0f, iy = 0.0f;   /* desired direction, magnitude 0-1 */
    int   has_dpad = 0;

    /* D-pad: digital, each direction adds full ±1 (supports 8-way diags) */
    if (kHeld & KEY_DUP)    { iy -= 1.0f; has_dpad = 1; }
    if (kHeld & KEY_DDOWN)  { iy += 1.0f; has_dpad = 1; }
    if (kHeld & KEY_DLEFT)  { ix -= 1.0f; has_dpad = 1; }
    if (kHeld & KEY_DRIGHT) { ix += 1.0f; has_dpad = 1; }

    /* Circle Pad: analog — overrides D-pad when outside deadzone */
    int cpActive = (abs(circlePos.dx) > CIRCLE_PAD_DEADZONE ||
                    abs(circlePos.dy) > CIRCLE_PAD_DEADZONE);
    if (cpActive) {
        /* Convert raw analog to -1..1 range */
        float cpx = (float)circlePos.dx / CIRCLE_PAD_MAX;
        float cpy = -(float)circlePos.dy / CIRCLE_PAD_MAX; /* Y inverted on 3DS */

        if (has_dpad) {
            /* Blend: circle pad takes priority when active */
            ix = cpx;
            iy = cpy;
        } else {
            ix = cpx;
            iy = cpy;
        }
    }

    /* Compute input magnitude (how far the stick is tilted / digital input) */
    float imag = sqrtf(ix * ix + iy * iy);

    /* Normalize direction so diagonal speed == cardinal speed,
     * but preserve the analog magnitude for variable walk speed */
    float inputStrength = 1.0f;  /* 0-1 strength for speed scaling */
    if (imag > 0.01f) {
        if (has_dpad && !cpActive) {
            /* Digital input: always full speed, just normalize direction */
            ix /= imag;
            iy /= imag;
            inputStrength = 1.0f;
        } else {
            /* Analog: allow variable speed based on tilt magnitude */
            inputStrength = imag;
            if (inputStrength > 1.0f) inputStrength = 1.0f;
            /* Normalize direction only */
            ix /= imag;
            iy /= imag;
        }
    } else {
        ix = 0.0f;
        iy = 0.0f;
        inputStrength = 0.0f;
    }

    int hasInput = (inputStrength > 0.05f);

    /* ══════════════════════════════════════════════════════════
     * 2. MOMENTUM PHYSICS — Isaac's "slippery" feel
     * ══════════════════════════════════════════════════════════ */

    /* During knockback stun, player has no movement control */
    if (p->kb_timer > 0) {
        /* Knockback decays via friction every frame */
        p->vx *= PLAYER_FRICTION;
        p->vy *= PLAYER_FRICTION;
    } else if (hasInput) {
        /* Target velocity = direction × magnitude × max speed */
        float tvx = ix * inputStrength * spd;
        float tvy = iy * inputStrength * spd;

        /* Lerp current velocity toward target (smooth acceleration) */
        p->vx += (tvx - p->vx) * PLAYER_ACCEL;
        p->vy += (tvy - p->vy) * PLAYER_ACCEL;
    } else {
        /* No input: apply friction (the Isaac slide-to-stop feel) */
        p->vx *= PLAYER_FRICTION;
        p->vy *= PLAYER_FRICTION;

        /* Snap to zero below threshold to prevent infinite micro-drift */
        if (fabsf(p->vx) < PLAYER_STOP_THRESH) p->vx = 0.0f;
        if (fabsf(p->vy) < PLAYER_STOP_THRESH) p->vy = 0.0f;
    }

    /* Clamp velocity magnitude (allow brief overshoot from knockback) */
    float vmag = sqrtf(p->vx * p->vx + p->vy * p->vy);
    float maxV = spd * 1.6f;   /* knockback can briefly exceed normal speed */
    if (vmag > maxV) {
        float scale = maxV / vmag;
        p->vx *= scale;
        p->vy *= scale;
    }

    /* ══════════════════════════════════════════════════════════
     * 3. COLLISION — axis-separated for smooth wall sliding
     * ══════════════════════════════════════════════════════════ */
    Room *r = current_room(g);

    /* Try X movement */
    float nx = p->x + p->vx;
    if (!player_blocked_at(r, nx, p->y)) {
        p->x = nx;
    } else {
        /* Slide along wall: zero only the blocked axis */
        p->vx = 0.0f;
    }

    /* Try Y movement */
    float ny = p->y + p->vy;
    if (!player_blocked_at(r, p->x, ny)) {
        p->y = ny;
    } else {
        p->vy = 0.0f;
    }

    /* Final boundary clamp (safety net) */
    p->x = clampf(p->x, ROOM_LEFT + PLAYER_SIZE, ROOM_RIGHT - PLAYER_SIZE);
    p->y = clampf(p->y, ROOM_TOP + PLAYER_SIZE, ROOM_BOTTOM - PLAYER_SIZE);

    /* ══════════════════════════════════════════════════════════
     * 4. FACING DIRECTION + ANIMATION
     * ══════════════════════════════════════════════════════════ */
    float actualSpeed = sqrtf(p->vx * p->vx + p->vy * p->vy);
    if (actualSpeed > 0.2f) {
        p->moving = 1;
        p->anim_timer++;

        /* Update facing only from intentional input (not knockback drift) */
        if (hasInput && p->kb_timer <= 0) {
            if (fabsf(ix) > fabsf(iy)) {
                p->face_dir = (ix > 0) ? DIR_RIGHT : DIR_LEFT;
            } else {
                p->face_dir = (iy > 0) ? DIR_DOWN : DIR_UP;
            }
        }
    } else {
        p->moving = 0;
    }

    /* ══════════════════════════════════════════════════════════
     * 5. INTERACTIONS — pickups and trapdoor
     * ══════════════════════════════════════════════════════════ */

    /* Pedestal pickup */
    if (r->pedestal.active) {
        float pdx = p->x - r->pedestal.x;
        float pdy = p->y - r->pedestal.y;
        if (pdx * pdx + pdy * pdy < (PLAYER_SIZE + 10) * (PLAYER_SIZE + 10)) {
            g->last_pickup = r->pedestal.item;
            g->pickup_flash = PICKUP_FLASH_FRAMES;
            p->pickup_anim = 40;  /* show pickup pose for ~0.7s */
            collect_item(g, r->pedestal.item);
            r->pedestal.active = 0;
            g->score += 50;
            audio_play(SFX_PICKUP);
        }
    }

    /* Heart pickup */
    for (int i = 0; i < MAX_HEART_PICKUPS; i++) {
        HeartPickup *h = &r->hearts[i];
        if (!h->active) continue;
        
        float hdx = p->x - h->x;
        float hdy = p->y - h->y;
        if (hdx * hdx + hdy * hdy < (PLAYER_SIZE + 8) * (PLAYER_SIZE + 8)) {
            /* Heal player based on heart type */
            int heal_amount = 0;
            if (h->type == HEART_RED_FULL) {
                heal_amount = 2;
            } else if (h->type == HEART_RED_HALF) {
                heal_amount = 1;
            }
            
            /* Only pick up if player can be healed */
            if (p->hp < p->stats.max_hp) {
                p->hp += heal_amount;
                if (p->hp > p->stats.max_hp) p->hp = p->stats.max_hp;
                h->active = 0;
                r->heart_count--;
                audio_play(SFX_PICKUP);
            }
        }
    }

    /* Consumable pickup */
    for (int i = 0; i < MAX_CONSUMABLE_PICKUPS; i++) {
        ConsumablePickup *c = &r->consumables[i];
        if (!c->active) continue;

        float cdx = p->x - c->x;
        float cdy = p->y - c->y;
        if (cdx * cdx + cdy * cdy < CONSUMABLE_PICKUP_DIST * CONSUMABLE_PICKUP_DIST) {
            int picked_up = 1;
            switch (c->type) {
            case PICKUP_BOMB:   p->bombs += 1; break;
            case PICKUP_BOMB2:  p->bombs += 2; break;
            case PICKUP_KEY:    p->keys  += 1; break;
            case PICKUP_COIN:   p->coins += 1; break;
            case PICKUP_COIN5:  p->coins += 5; break;
            case PICKUP_PILL:
                if (!p->has_pill) {
                    p->has_pill = 1;
                    p->held_pill = (PillEffect)c->sub_type;
                    /* Show pill color name as pickup message */
                    snprintf(g->pickup_msg_text, sizeof(g->pickup_msg_text),
                             "Pill: %s", pill_color_name(c->sub_type));
                    g->pickup_msg_timer = 180;
                } else picked_up = 0;
                break;
            case PICKUP_CARD:
                if (!p->has_card) {
                    p->has_card = 1;
                    p->held_card = (TarotCard)c->sub_type;
                    snprintf(g->pickup_msg_text, sizeof(g->pickup_msg_text),
                             "Card: %s", tarot_name(c->sub_type));
                    g->pickup_msg_timer = 180;
                } else picked_up = 0;
                break;
            default: break;
            }
            if (p->coins > 99) p->coins = 99;
            if (picked_up) {
                c->active = 0;
                r->consumable_count--;
                audio_play(SFX_PICKUP);
            }
        }
    }

    /* Shop item purchase (walk into shop item to buy) */
    if (r->type == ROOM_SHOP) {
        if (g->shop_deny_timer > 0) g->shop_deny_timer--;
        for (int i = 0; i < r->shop_count; i++) {
            ShopItem *si = &r->shop_items[i];
            if (!si->active) continue;

            float sdx = p->x - si->x;
            float sdy = p->y - (si->y + 5);  /* match pedestal visual center */
            if (sdx * sdx + sdy * sdy < (PLAYER_SIZE + 12) * (PLAYER_SIZE + 12)) {
                /* Player walked into shop item - buy if affordable */
                if (p->coins >= si->cost) {
                    p->coins -= si->cost;
                    g->last_pickup = si->item;
                    g->pickup_flash = PICKUP_FLASH_FRAMES;
                    p->pickup_anim = 40;
                    collect_item(g, si->item);
                    si->active = 0;
                    g->score += 50;
                    audio_play(SFX_PICKUP);
                } else if (g->shop_deny_timer <= 0) {
                    /* Can't afford - audio/visual feedback with cooldown */
                    audio_play(SFX_HURT);
                    g->shop_deny_timer = 30;  /* ~0.5s cooldown */
                }
            }
        }
    }

    /* Trapdoor check */
    if (r->has_trapdoor) {
        float cx = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
        float cy = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
        float tdx = p->x - cx;
        float tdy = p->y - cy;
        if (tdx * tdx + tdy * tdy < 20.0f * 20.0f) {
            advance_floor(g);
        }
    }
}

/* ================================================================
 * Shooting
 * ================================================================ */

/* ================================================================
 * Blood Splatter Particle System
 * ================================================================ */

/* Spawn a tear pop/splash effect (when tear expires or hits wall) */
static void spawn_tear_pop(Game *g, float x, float y, int is_blood) {
    /* Spawn 2-3 small particles for the pop */
    int count = 2 + (randi(0, 2));
    for (int i = 0; i < count; i++) {
        BloodParticle *bp = NULL;
        for (int j = 0; j < MAX_BLOOD_PARTICLES; j++) {
            if (!g->blood[j].active) { bp = &g->blood[j]; break; }
        }
        if (!bp) return;

        bp->active = 1;
        bp->x = x + randf(-3, 3);
        bp->y = y + randf(-3, 3);
        bp->vx = randf(-1.5f, 1.5f);
        bp->vy = randf(-1.5f, 1.5f);
        bp->alpha = 0.9f;
        bp->scale = randf(0.4f, 0.8f);
        bp->rotation = randf(0, 6.28f);
        bp->timer = randi(12, 25);
        bp->max_timer = bp->timer;

        /* Use tear pop sprites — there are 4 of them (idx 21..24).
           randi is inclusive so use randi(0,3) to avoid spilling into the
           neighbouring blood_splat_small atlas slot. */
        bp->sprite_idx = bulletatlas_tear_pop_1_idx + randi(0, 3);
    }
}

/* Spawn blood splatter particles at enemy hit location */
static void spawn_blood_splatter(Game *g, float x, float y, float dx, float dy, int is_big) {
    /* Boost particle counts for a beefier hit feel (was 3-6 / 2-4) */
    int count = is_big ? randi(5, 9) : randi(3, 6);
    float tv = sqrtf(dx * dx + dy * dy);
    float ndx = (tv > 0.1f) ? dx / tv : 0;
    float ndy = (tv > 0.1f) ? dy / tv : 0;

    for (int i = 0; i < count; i++) {
        BloodParticle *bp = NULL;
        for (int j = 0; j < MAX_BLOOD_PARTICLES; j++) {
            if (!g->blood[j].active) { bp = &g->blood[j]; break; }
        }
        if (!bp) return;

        bp->active = 1;
        bp->x = x + randf(-2, 2);
        bp->y = y + randf(-2, 2);
        /* Spread in direction of tear travel + random scatter (livelier kick) */
        bp->vx = ndx * randf(0.8f, 3.2f) + randf(-1.4f, 1.4f);
        bp->vy = ndy * randf(0.8f, 3.2f) + randf(-1.4f, 1.4f);
        bp->alpha = 1.0f;
        /* Wider scale range so individual droplets vary more in size */
        bp->scale = randf(0.35f, is_big ? 1.3f : 0.8f);
        bp->rotation = randf(0, 6.28f);
        bp->timer = randi(20, 45);
        bp->max_timer = bp->timer;

        /* Pick from blood splatter sprites.
           randi is inclusive on both ends, so:
             small  (idx 25..27) → randi(0,2)  (3 sprites)
             medium (idx 28)     → 0           (1 sprite)
             large  (idx 29..31) → randi(0,2)  (3 sprites)
             huge   (idx 32..33) → randi(0,1)  (2 sprites)
           The previous code used randi(0,3)/randi(0,2) here and would
           occasionally spill into the next sprite's atlas slot, producing
           wrong-looking droplets. */
        int roll = randi(0, 9);
        if (roll < 3)
            bp->sprite_idx = bulletatlas_blood_splat_small_1_idx + randi(0, 2);
        else if (roll < 6)
            bp->sprite_idx = bulletatlas_blood_splat_med_1_idx;
        else if (roll < 8)
            bp->sprite_idx = bulletatlas_blood_splat_large_1_idx + randi(0, 2);
        else
            bp->sprite_idx = bulletatlas_blood_splat_huge_1_idx + randi(0, 1);
    }
}

/* Update blood particles: move, fade, deactivate */
static void blood_particles_update(Game *g) {
    for (int i = 0; i < MAX_BLOOD_PARTICLES; i++) {
        BloodParticle *bp = &g->blood[i];
        if (!bp->active) continue;

        bp->x += bp->vx;
        bp->y += bp->vy;
        /* Friction/deceleration */
        bp->vx *= 0.92f;
        bp->vy *= 0.92f;
        bp->rotation += 0.02f;

        bp->timer--;
        /* Fade out in last 40% of lifetime */
        float life_ratio = (float)bp->timer / (float)bp->max_timer;
        if (life_ratio < 0.4f) {
            bp->alpha = life_ratio / 0.4f;
        }
        bp->scale *= 0.99f; /* subtle shrink */

        if (bp->timer <= 0) {
            bp->active = 0;
        }
    }
}

/* Render blood particles */
static void render_blood_particles(Game *g) {
    if (!sheet_bullets) return;
    for (int i = 0; i < MAX_BLOOD_PARTICLES; i++) {
        BloodParticle *bp = &g->blood[i];
        if (!bp->active) continue;

        spr_draw_rotated_alpha(sheet_bullets, bp->sprite_idx,
                               bp->x, bp->y,
                               bp->scale, bp->scale,
                               bp->rotation, bp->alpha);
    }
}

static void spawn_tear(Game *g, float dx, float dy) {
    Tear *t = NULL;
    for (int i = 0; i < MAX_TEARS; i++) {
        if (!g->tears[i].active) { t = &g->tears[i]; break; }
    }
    if (!t) return;

    memset(t, 0, sizeof(Tear));
    t->active = 1;
    /* Spawn the tear slightly out in front of Isaac (along the shoot dir)
       and biased a bit above the centre of mass, so it visually leaves
       his crying head instead of overlapping his torso/feet.
       Also nudges it forward enough that the tear is clearly separate
       from the player sprite on its very first frame. */
    {
        float tlen = sqrtf(dx * dx + dy * dy);
        float fwd = PLAYER_SIZE * 0.55f;  /* forward push (in pixels) */
        float fx = (tlen > 0.1f) ? (dx / tlen) * fwd : 0.0f;
        float fy = (tlen > 0.1f) ? (dy / tlen) * fwd : 0.0f;
        t->x = g->player.x + fx;
        /* Raise spawn so tears look like they come from the head, not
           the body. PLAYER_SIZE/3 ≈ Isaac's eye height. */
        t->y = g->player.y - (PLAYER_SIZE * 0.30f) + fy;
    }

    /* Apply slight random spread by rotating the direction vector */
    float ang = randf(-TEAR_SPREAD, TEAR_SPREAD);
    float ca = cosf(ang), sa = sinf(ang);
    float ndx = dx * ca - dy * sa;
    float ndy = dx * sa + dy * ca;
    t->dx = ndx;
    t->dy = ndy;

    t->dist = 0;
    t->speed = sqrtf(ndx * ndx + ndy * ndy);
    t->dmg = g->player.stats.damage;
    t->piercing = (g->player.stats.flags & ITEM_FLAG_PIERCING) ? 1 : 0;
    t->spectral = (g->player.stats.flags & ITEM_FLAG_SPECTRAL) ? 1 : 0;
    t->homing   = (g->player.stats.flags & ITEM_FLAG_HOMING) ? 1 : 0;

    /* Tear arc: launch slightly upward, gravity will bring it down */
    t->z = 0;
    t->vz = TEAR_ARC_VEL;

    /* Sprite animation fields */
    t->rotation = atan2f(ndy, ndx);  /* rotation based on travel direction */
    t->anim_frame = 0;
    t->is_enemy = 0;  /* player tear */

    /* Size scales with damage */
    float dscale = t->dmg / 2.0f;
    if (dscale < 0.7f) dscale = 0.7f;
    if (dscale > 2.0f) dscale = 2.0f;
    t->size = dscale;
}

void shoot_tear(Game *g, Direction dir) {
    if (g->player.tear_cooldown > 0) return;

    float tdx = 0, tdy = 0;
    switch (dir) {
        case DIR_UP:    tdy = -TEAR_BASE_SPEED; break;
        case DIR_DOWN:  tdy =  TEAR_BASE_SPEED; break;
        case DIR_LEFT:  tdx = -TEAR_BASE_SPEED; break;
        case DIR_RIGHT: tdx =  TEAR_BASE_SPEED; break;
        default: return;
    }

    if (g->player.stats.flags & ITEM_FLAG_TRIPLE) {
        /* Triple shot: center + two angled */
        spawn_tear(g, tdx, tdy);

        float angle = 0.25f;
        if (tdx != 0) {
            spawn_tear(g, tdx, -TEAR_BASE_SPEED * angle);
            spawn_tear(g, tdx,  TEAR_BASE_SPEED * angle);
        } else {
            spawn_tear(g, -TEAR_BASE_SPEED * angle, tdy);
            spawn_tear(g,  TEAR_BASE_SPEED * angle, tdy);
        }
    } else {
        spawn_tear(g, tdx, tdy);
    }

    /* Face the player in the shoot direction + trigger crying head animation */
    if (dir != DIR_NONE) {
        g->player.face_dir = dir;
        g->player.shoot_dir = dir;
        g->player.shoot_anim = 18;  /* show crying face for 18 frames (~0.3s) */
    }

    g->player.tear_cooldown = get_tear_cooldown(&g->player);
    /* Alternate between two tear fire sounds for variety */
    static int tear_sound_toggle = 0;
    audio_play(tear_sound_toggle ? SFX_TEAR_FIRE2 : SFX_TEAR_FIRE1);
    tear_sound_toggle = !tear_sound_toggle;
}

void tears_update(Game *g) {
    Room *r = current_room(g);
    float range = g->player.stats.range;

    for (int i = 0; i < MAX_TEARS; i++) {
        Tear *t = &g->tears[i];
        if (!t->active) continue;

        /* Homing: steer toward nearest enemy */
        if (t->homing) {
            float best_dist = 99999.0f;
            Enemy *best = NULL;
            for (int j = 0; j < r->enemy_count; j++) {
                Enemy *e = &r->enemies[j];
                if (!e->active) continue;
                float edx = e->x - t->x;
                float edy = e->y - t->y;
                float d = edx * edx + edy * edy;
                if (d < best_dist) { best_dist = d; best = e; }
            }
            if (best) {
                float edx = best->x - t->x;
                float edy = best->y - t->y;
                float em = sqrtf(edx * edx + edy * edy);
                if (em > 1.0f) {
                    float steer = 0.18f;
                    t->dx += (edx / em) * steer;
                    t->dy += (edy / em) * steer;
                    /* Re-normalize speed back to original */
                    float sm = sqrtf(t->dx * t->dx + t->dy * t->dy);
                    if (sm > 0.1f) {
                        t->dx = (t->dx / sm) * TEAR_BASE_SPEED;
                        t->dy = (t->dy / sm) * TEAR_BASE_SPEED;
                    }
                }
            }
        }

        /* Move */
        t->x += t->dx;
        t->y += t->dy;
        /* Track distance using actual velocity */
        t->dist += sqrtf(t->dx * t->dx + t->dy * t->dy);

        /* Arc: gravity pulls vz back down to ground */
        t->z += t->vz;
        t->vz += TEAR_GRAVITY;
        if (t->z > 0) t->z = 0; /* hit ground (z = 0 ground plane) */

        /* Animate tear: spin slowly and increment frame counter */
        t->anim_frame++;
        if (t->homing) {
            t->rotation = atan2f(t->dy, t->dx);  /* always face travel direction */
        } else {
            t->rotation += 0.04f;  /* gentle wobble/spin */
        }

        /* Out of range -> splash + blood particles */
        if (t->dist >= range) {
            spawn_tear_pop(g, t->x, t->y, 0);  /* wall splash */
            t->active = 0;
            continue;
        }

        /* Hit wall (spectral tears pass through) */
        if (!t->spectral) {
            if (t->x < ROOM_LEFT || t->x > ROOM_RIGHT ||
                t->y < ROOM_TOP  || t->y > ROOM_BOTTOM) {
                audio_play(SFX_TEAR_BLOCK);  /* Play wall hit sound */
                spawn_tear_pop(g, t->x, t->y, 0);  /* wall splash */
                t->active = 0;
                continue;
            }
        } else {
            /* Even spectral tears die if way out of bounds */
            if (t->x < ROOM_LEFT - 20 || t->x > ROOM_RIGHT + 20 ||
                t->y < ROOM_TOP - 20  || t->y > ROOM_BOTTOM + 20) {
                t->active = 0;
                continue;
            }
        }

        /* Hit obstacle (spectral tears pass through) */
        if (!t->spectral) {
            for (int j = 0; j < r->obstacle_count; j++) {
                Obstacle *o = &r->obstacles[j];
                if (!o->active) continue;
                float odx = t->x - o->x;
                float ody = t->y - o->y;
                float dist = odx * odx + ody * ody;
                float minDist = TEAR_RADIUS + OBSTACLE_SIZE * 0.5f;
                if (dist < minDist * minDist) {
                    audio_play(SFX_TEAR_BLOCK);  /* Play obstacle hit sound */
                    spawn_tear_pop(g, t->x, t->y, 0);  /* obstacle splash */
                    t->active = 0;
                    break;
                }
            }
        }
    }
}

/* ================================================================
 * Enemy AI
 * ================================================================ */

/* Forward declarations for boss helpers */
static void spawn_enemy_shot(Game *g, float x, float y, float dx, float dy, int dmg);

/* Boss: spawn a fly enemy if there's room */
static const char *boss_name_str(EnemyType t) {
    switch (t) {
    case ENEMY_BOSS_MONSTRO:    return "MONSTRO";
    case ENEMY_BOSS_LARRY:      return "LARRY JR.";
    case ENEMY_BOSS_DUKE:       return "DUKE OF FLIES";
    case ENEMY_BOSS_GEMINI:     return "GEMINI";
    case ENEMY_BOSS_FAMINE:     return "FAMINE";
    case ENEMY_BOSS_PEEP:       return "PEEP";
    case ENEMY_BOSS_GURDY:      return "GURDY";
    case ENEMY_BOSS_PIN:        return "PIN";
    case ENEMY_BOSS_HAUNT:      return "THE HAUNT";
    case ENEMY_BOSS_WIDOW:      return "WIDOW";
    case ENEMY_BOSS_MEGA_SATAN: return "MEGA SATAN";
    default: return "BOSS";
    }
}

static int count_alive_flies(Room *r) {
    int c = 0;
    for (int i = 0; i < r->enemy_count; i++) {
        if (r->enemies[i].active && r->enemies[i].type == ENEMY_FLY) c++;
    }
    return c;
}

static void boss_spawn_fly(Game *g, Room *r, float bx, float by) {
    (void)g;
    if (r->enemy_count >= MAX_ENEMIES) return;
    Enemy *e = &r->enemies[r->enemy_count];
    memset(e, 0, sizeof(Enemy));
    e->active = 1;
    e->type = ENEMY_FLY;
    e->hp = 2;
    e->max_hp = 2;
    e->x = bx + randf(-30, 30);
    e->y = by + randf(-30, 30);
    e->dx = randf(-1.5f, 1.5f);
    e->dy = randf(-1.5f, 1.5f);
    e->timer = randi(30, 60);
    r->enemy_count++;
}

/* Spawn boss tear in a specific direction */
static void boss_shoot_tear(Game *g, float x, float y, float dx, float dy) {
    spawn_enemy_shot(g, x, y, dx, dy, BOSS_SHOT_DMG);
}

/* Monstro: 8-way tear spread on landing */
static void monstro_tear_spread(Game *g, float x, float y) {
    for (int i = 0; i < MONSTRO_TEAR_COUNT; i++) {
        float angle = (float)i * (2.0f * M_PI / MONSTRO_TEAR_COUNT) + randf(-0.15f, 0.15f);
        float spd = BOSS_SHOT_SPEED + randf(-0.3f, 0.3f);
        boss_shoot_tear(g, x, y, cosf(angle) * spd, sinf(angle) * spd);
    }
}

/* Monstro: shotgun blast toward player */
static void monstro_shotgun(Game *g, float bx, float by) {
    float pdx = g->player.x - bx;
    float pdy = g->player.y - by;
    float pm = sqrtf(pdx * pdx + pdy * pdy);
    if (pm < 1.0f) pm = 1.0f;
    float base_angle = atan2f(pdy, pdx);
    for (int i = 0; i < 5; i++) {
        float a = base_angle + randf(-0.4f, 0.4f);
        float spd = BOSS_SHOT_SPEED + randf(-0.5f, 0.8f);
        boss_shoot_tear(g, bx, by, cosf(a) * spd, sinf(a) * spd);
    }
}

/* ----- Phase 2 boss minion spawn helpers ----- */

/* Peep: spawn a small eye that pursues player */
static void boss_spawn_eye(Room *r, float bx, float by) {
    if (r->enemy_count >= MAX_ENEMIES) return;
    Enemy *e = &r->enemies[r->enemy_count];
    memset(e, 0, sizeof(Enemy));
    e->active = 1;
    e->type = ENEMY_EYE;
    e->hp = 3;
    e->max_hp = 3;
    e->x = bx + randf(-20, 20);
    e->y = by + randf(-20, 20);
    e->dx = randf(-1.0f, 1.0f);
    e->dy = randf(-1.0f, 1.0f);
    e->timer = randi(30, 60);
    e->shoot_timer = randi(60, 100);
    r->enemy_count++;
}

/* Haunt: spawn a Lil Haunt minion */
static void boss_spawn_lil_haunt(Room *r, float bx, float by) {
    if (r->enemy_count >= MAX_ENEMIES) return;
    Enemy *e = &r->enemies[r->enemy_count];
    memset(e, 0, sizeof(Enemy));
    e->active = 1;
    e->type = ENEMY_LIL_HAUNT;
    e->hp = 4;
    e->max_hp = 4;
    e->x = bx + randf(-25, 25);
    e->y = by + randf(-25, 25);
    e->dx = randf(-0.8f, 0.8f);
    e->dy = randf(-0.8f, 0.8f);
    e->timer = randi(60, 90);
    r->enemy_count++;
}

/* Widow / Gurdy: spawn a small spider */
static void boss_spawn_spider(Room *r, float bx, float by) {
    if (r->enemy_count >= MAX_ENEMIES) return;
    Enemy *e = &r->enemies[r->enemy_count];
    memset(e, 0, sizeof(Enemy));
    e->active = 1;
    e->type = ENEMY_SPIDER;
    e->hp = 2;
    e->max_hp = 2;
    e->x = bx + randf(-25, 25);
    e->y = by + randf(-25, 25);
    e->dx = randf(-2.0f, 2.0f);
    e->dy = randf(-2.0f, 2.0f);
    e->timer = randi(30, 60);
    r->enemy_count++;
}

/* Gurdy: spawn pooter minion */
static void boss_spawn_pooter(Room *r, float bx, float by) {
    if (r->enemy_count >= MAX_ENEMIES) return;
    Enemy *e = &r->enemies[r->enemy_count];
    memset(e, 0, sizeof(Enemy));
    e->active = 1;
    e->type = ENEMY_POOTER;
    e->hp = 3;
    e->max_hp = 3;
    e->x = bx + randf(-30, 30);
    e->y = by + randf(-30, 30);
    e->dx = randf(-1.0f, 1.0f);
    e->dy = randf(-1.0f, 1.0f);
    e->timer = randi(40, 80);
    e->shoot_timer = randi(60, 100);
    r->enemy_count++;
}

/* Phase 2 boss: shoot a 4-way spread of tears */
static void boss_spread_4way(Game *g, float x, float y, float spd) {
    boss_shoot_tear(g, x, y,  spd, 0);
    boss_shoot_tear(g, x, y, -spd, 0);
    boss_shoot_tear(g, x, y, 0,  spd);
    boss_shoot_tear(g, x, y, 0, -spd);
}

/* Phase 2 boss: shoot an 8-way spread of tears */
static void boss_spread_8way(Game *g, float x, float y, float spd) {
    for (int i = 0; i < 8; i++) {
        float angle = (float)i * (M_PI / 4.0f);
        boss_shoot_tear(g, x, y, cosf(angle) * spd, sinf(angle) * spd);
    }
}

/* Count enemies of given type (used for boss minion caps) */
static int count_enemies_of_type(Room *r, EnemyType t) {
    int c = 0;
    for (int i = 0; i < r->enemy_count; i++) {
        if (r->enemies[i].active && r->enemies[i].type == t) c++;
    }
    return c;
}

/* Spawn a small gaper as part of split-on-death */
static void spawn_gaper_split(Room *r, float px, float py) {
    if (r->enemy_count >= MAX_ENEMIES) return;
    Enemy *e = &r->enemies[r->enemy_count];
    memset(e, 0, sizeof(Enemy));
    e->active = 1;
    e->type = ENEMY_GAPER_SMALL;
    e->hp = 1;
    e->max_hp = 1;
    e->x = px + randf(-10, 10);
    e->y = py + randf(-10, 10);
    e->dx = randf(-1.5f, 1.5f);
    e->dy = randf(-1.5f, 1.5f);
    e->timer = randi(20, 60);
    e->split_done = 1; /* don't split again */
    r->enemy_count++;
}

/* Spawn a fly from a dying Mulligan */
static void spawn_fly_from_death(Room *r, float px, float py, const FloorInfo *fi) {
    if (r->enemy_count >= MAX_ENEMIES) return;
    Enemy *e = &r->enemies[r->enemy_count];
    memset(e, 0, sizeof(Enemy));
    e->active = 1;
    e->type = ENEMY_FLY;
    e->hp = 2;
    e->max_hp = 2;
    e->x = px + randf(-12, 12);
    e->y = py + randf(-12, 12);
    e->dx = randf(-1.0f, 1.0f) * fi->enemy_speed_mult;
    e->dy = randf(-1.0f, 1.0f) * fi->enemy_speed_mult;
    e->timer = randi(30, 90);
    r->enemy_count++;
}

/* Boom fly explosion: damage player if close, spawn blood particles */
static void boom_fly_explode(Game *g, float x, float y) {
    Player *p = &g->player;
    float dx = p->x - x;
    float dy = p->y - y;
    float dist = sqrtf(dx * dx + dy * dy);
    float blast_radius = 40.0f;
    if (dist < blast_radius && p->iframes == 0) {
        p->hp -= player_absorb_dmg(p, 1);
        p->iframes = PLAYER_IFRAMES;
        if (dist > 0.1f) {
            p->vx = (dx / dist) * PLAYER_KB_FORCE * 1.5f;
            p->vy = (dy / dist) * PLAYER_KB_FORCE * 1.5f;
        }
        p->kb_timer = PLAYER_KB_FRAMES;
        audio_play(SFX_HURT);
    }
    /* Screen shake */
    trigger_shake(g, 5.0f, 20);
    /* Spawn blood particles for visual explosion */
    for (int i = 0; i < 6; i++) {
        spawn_blood_splatter(g, x + randf(-10, 10), y + randf(-10, 10),
                            randf(-2, 2), randf(-2, 2), 1);
    }
}

/* Forward declarations for drop system */
static void spawn_heart(Room *r, float x, float y, HeartType type);
static void spawn_random_consumable(Room *r, float x, float y);
static int is_boss_type(EnemyType t);

/* Roll heart/consumable drops from killed enemy, respecting difficulty */
static void enemy_death_drops(Game *g, Room *r, Enemy *e) {
    if (is_boss_type(e->type)) {
        /* Boss always drops 2 full hearts */
        spawn_heart(r, e->x - 12, e->y, HEART_RED_FULL);
        spawn_heart(r, e->x + 12, e->y, HEART_RED_FULL);
    } else {
        /* 5% chance for pill or card on every regular kill */
        int special = randi(0, 100);
        if (special < 3) {
            spawn_pill_pickup(r, e->x, e->y, randi(0, PILL_EFFECT_COUNT - 1));
            return;
        } else if (special < 5) {
            spawn_card_pickup(r, e->x, e->y, randi(0, TAROT_COUNT - 1));
            return;
        }
        /* Difficulty-scaled heart drop rates:
         * Easy: full<13, half<40, consumable<60
         * Normal: full<10, half<30, consumable<50
         * Hard: full<7, half<20, consumable<40 */
        float rate = diff_heart_drop_rate(g->difficulty);
        int full_thresh  = (int)(rate * 0.33f);       /* ~10 normal */
        int half_thresh  = (int)(rate);                /* ~30 normal */
        int consum_thresh = half_thresh + 20;          /* ~50 normal */
        int roll = randi(0, 100);
        if (roll < full_thresh) {
            spawn_heart(r, e->x, e->y, HEART_RED_FULL);
        } else if (roll < half_thresh) {
            spawn_heart(r, e->x, e->y, HEART_RED_HALF);
        } else if (roll < consum_thresh) {
            spawn_random_consumable(r, e->x, e->y);
        }
    }
}

static void spawn_heart(Room *r, float x, float y, HeartType type) {
    for (int i = 0; i < MAX_HEART_PICKUPS; i++) {
        if (!r->hearts[i].active) {
            HeartPickup *h = &r->hearts[i];
            h->x = x;
            h->y = y;
            h->type = type;
            h->active = 1;
            h->anim_timer = randi(0, 60);  /* random phase for bobbing */
            r->heart_count++;
            return;
        }
    }
}

/* Spawn an enemy projectile (clotty blood shots) */
static void spawn_enemy_shot(Game *g, float x, float y, float dx, float dy, int dmg) {
    for (int i = 0; i < MAX_ENEMY_SHOTS; i++) {
        if (!g->enemy_shots[i].active) {
            EnemyShot *s = &g->enemy_shots[i];
            s->x = x;
            s->y = y;
            s->dx = dx;
            s->dy = dy;
            s->dist = 0;
            s->active = 1;
            s->dmg = dmg;
            return;
        }
    }
}

/* Trigger screen shake (intensity capped, takes max of current and new) */
void trigger_shake(Game *g, float intensity, int frames) {
    if (intensity > g->shake_intensity) g->shake_intensity = intensity;
    if (frames > g->shake_timer) g->shake_timer = frames;
    if (g->shake_intensity > SCREEN_SHAKE_MAX) g->shake_intensity = SCREEN_SHAKE_MAX;
}

/* ================================================================
 * Curse System (Phase 2)
 * ================================================================ */

const char *curse_name(int curse) {
    switch (curse) {
        case CURSE_DARKNESS: return "CURSE OF DARKNESS";
        case CURSE_LOST:     return "CURSE OF THE LOST";
        case CURSE_BLIND:    return "CURSE OF THE BLIND";
        default:             return "";
    }
}

/* Roll a curse with 30% chance per floor.
 * Called on floor transition completion. */
void roll_curse(Game *g) {
    g->active_curse = CURSE_NONE;
    g->curse_display_timer = 0;

    /* No curse on starting floor (floor 0) to give players a fair start */
    if (g->current_floor == 0) return;

    /* 30% chance per floor */
    if (randi(0, 9) < 3) {
        /* Pick one of the 3 curses at random */
        g->active_curse = randi(1, 3);  /* CURSE_DARKNESS..CURSE_BLIND */
        g->curse_display_timer = 180;   /* 3 seconds at 60 FPS */
    }
}

/* ================================================================
 * Unlock System (Phase 2)
 * ================================================================ */

const char *character_unlock_name(int char_idx) {
    switch (char_idx) {
        case 0: return "ISAAC";
        case 1: return "MAGDALENE";
        case 2: return "CAIN";
        case 3: return "JUDAS";
        default: return "?";
    }
}

/* Called on game win to update unlocks/achievements. */
void unlock_check_after_win(Game *g) {
    int char_bit = 1 << g->player.character;
    g_config.characters_completed |= char_bit;
    g_config.total_wins++;

    /* Unlock next character (progression: Isaac -> Magdalene -> Cain -> Judas) */
    int prev_unlocked = g_config.unlocked_chars;
    if ((g_config.characters_completed & 0x01) && !(g_config.unlocked_chars & 0x02)) {
        g_config.unlocked_chars |= 0x02;  /* unlock Magdalene */
    }
    if ((g_config.characters_completed & 0x02) && !(g_config.unlocked_chars & 0x04)) {
        g_config.unlocked_chars |= 0x04;  /* unlock Cain */
    }
    if ((g_config.characters_completed & 0x04) && !(g_config.unlocked_chars & 0x08)) {
        g_config.unlocked_chars |= 0x08;  /* unlock Judas */
    }

    /* Track current boss in defeated mask (current_boss_type may not always
     * be valid; bosses_defeated mainly accrued via collisions_update on kill) */
    (void)prev_unlocked;

    /* Save progress */
    config_save(&g_config);
}

/* ================================================================
 * Bomb placement and explosion
 * ================================================================ */

/* Forward declaration for bomb death handling */
static int is_boss_type(EnemyType t);

void place_bomb(Game *g) {
    if (g->bomb_timer > 0) return;       /* already a bomb active */
    if (g->player.bombs <= 0) return;
    g->player.bombs--;
    g->bomb_x = g->player.x;
    g->bomb_y = g->player.y;
    g->bomb_timer = 90;   /* ~1.5 seconds at 60fps */
    g->bomb_flash = 0;
    audio_play(SFX_SHOOT);  /* reuse shoot sound for placement */
}

void bomb_update(Game *g) {
    if (g->bomb_timer <= 0) return;
    g->bomb_timer--;
    g->bomb_flash++;

    if (g->bomb_timer > 0) return;

    /* === EXPLOSION === */
    Room *r = current_room(g);
    float bx = g->bomb_x, by = g->bomb_y;
    float blast = 48.0f;

    trigger_shake(g, 6.0f, 20);
    audio_play(SFX_ENEMY_DEATH);  /* reuse for explosion sound */

    /* Destroy obstacles (rocks) in blast radius */
    for (int i = 0; i < r->obstacle_count; i++) {
        Obstacle *o = &r->obstacles[i];
        if (!o->active) continue;
        float dx = o->x - bx, dy = o->y - by;
        if (dx * dx + dy * dy < blast * blast) {
            o->active = 0;
            /* Chance to drop a consumable from destroyed rock */
            if (randi(0, 100) < 35) {
                spawn_random_consumable(r, o->x, o->y);
            }
        }
    }

    /* Damage enemies in blast radius.
     * Capture initial count: enemies spawned by this bomb's chain reactions
     * (mulligan flies, gaper splits, black champion splits) should NOT be
     * damaged by the same explosion. */
    int initial_enemy_count = r->enemy_count;
    for (int i = 0; i < initial_enemy_count; i++) {
        Enemy *e = &r->enemies[i];
        if (!e->active) continue;
        float dx = e->x - bx, dy = e->y - by;
        if (dx * dx + dy * dy < blast * blast) {
            e->hp -= 3;  /* bombs do heavy damage */
            e->flash = 14;  /* punchier white-hit flash on bomb damage */
            /* Knockback away from bomb */
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > 0.1f) {
                e->x += (dx / dist) * 20.0f;
                e->y += (dy / dist) * 20.0f;
            }

            /* Handle enemy death from bomb */
            if (e->hp <= 0) {
                /* Globin: collapse instead of dying (first time) */
                if (e->type == ENEMY_GLOBIN && e->state == 0 && !e->split_done) {
                    e->state = 1;
                    e->hp = 1;
                    e->regen_timer = 120;
                    e->split_done = 1;
                    continue;
                }

                /* Gaper split on death */
                if (e->type == ENEMY_GAPER && !e->split_done) {
                    spawn_gaper_split(r, e->x, e->y);
                    spawn_gaper_split(r, e->x, e->y);
                }

                /* Boom Fly: explode on death */
                if (e->type == ENEMY_BOOM_FLY) {
                    boom_fly_explode(g, e->x, e->y);
                }

                /* Mulligan: spawn 2-3 flies on death */
                if (e->type == ENEMY_MULLIGAN) {
                    const FloorInfo *mfi = get_floor_info(g->current_floor);
                    int nflies = randi(2, 3);
                    for (int f = 0; f < nflies; f++) {
                        spawn_fly_from_death(r, e->x, e->y, mfi);
                    }
                }

                /* Drops (difficulty-scaled) */
                enemy_death_drops(g, r, e);

                /* Champion bonus reward + black champion split */
                if (e->champion != CHAMP_NONE) {
                    enemy_drop_champion_reward(g, e);
                    if (e->champion == CHAMP_BLACK && e->split_pending) {
                        int split_hp = (e->max_hp > 1) ? (e->max_hp / 2) : 1;
                        for (int ci = 0; ci < 2; ci++) {
                            for (int ei = 0; ei < MAX_ENEMIES; ei++) {
                                if (!r->enemies[ei].active) {
                                    Enemy *ne = &r->enemies[ei];
                                    memset(ne, 0, sizeof(Enemy));
                                    ne->type = e->type;
                                    ne->x = e->x + randf(-12.0f, 12.0f);
                                    ne->y = e->y + randf(-12.0f, 12.0f);
                                    ne->hp = ne->max_hp = split_hp;
                                    ne->active = 1;
                                    ne->champion = CHAMP_NONE;
                                    ne->dx = randf(-1.0f, 1.0f);
                                    ne->dy = randf(-1.0f, 1.0f);
                                    if (ei >= r->enemy_count) r->enemy_count = ei + 1;
                                    break;
                                }
                            }
                        }
                    }
                }

                e->active = 0;
                g->score += is_boss_type(e->type) ? 100 : 10;
                g->kills++;
                audio_play(SFX_ENEMY_DEATH);
                if (is_boss_type(e->type)) {
                    trigger_shake(g, 7.0f, 40);
                    g->boss_active = 0;
                    g->boss_death_anim = 60;
                    /* Track in persistent unlocks (bosses_defeated bitmask) */
                    {
                        int boss_idx = (int)e->type - (int)ENEMY_BOSS_DUKE;
                        if (boss_idx >= 0 && boss_idx < 16) {
                            g_config.bosses_defeated |= (1 << boss_idx);
                            config_save(&g_config);
                        }
                    }
                    /* Return to floor music after boss defeat */
                    music_play(music_for_floor(g->current_floor));
                }
            }
        }
    }

    /* Reveal secret room doors: check all 4 directions */
    Dungeon *d = &g->dungeon;
    int rx = d->cur_x, ry = d->cur_y;
    int dxs[] = {0, 0, -1, 1};
    int dys[] = {-1, 1, 0, 0};
    float midX = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
    float midY = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;

    /* Check if bomb is near a wall that leads to a secret room */
    for (int dir = 0; dir < 4; dir++) {
        int nx = rx + dxs[dir], ny = ry + dys[dir];
        if (nx < 0 || nx >= DUNGEON_W || ny < 0 || ny >= DUNGEON_H) continue;
        if (d->rooms[ny][nx].type != ROOM_SECRET) continue;
        if (r->doors[dir]) continue; /* door already open */

        /* Check if bomb is near the wall in this direction */
        int near_wall = 0;
        if (dir == 0 && by < ROOM_TOP + 40 && fabsf(bx - midX) < 50) near_wall = 1;
        if (dir == 1 && by > ROOM_BOTTOM - 40 && fabsf(bx - midX) < 50) near_wall = 1;
        if (dir == 2 && bx < ROOM_LEFT + 40 && fabsf(by - midY) < 50) near_wall = 1;
        if (dir == 3 && bx > ROOM_RIGHT - 40 && fabsf(by - midY) < 50) near_wall = 1;

        if (near_wall) {
            r->doors[dir] = 1;
            r->secret_revealed = 1;
            /* Also open the door on the secret room side */
            int opp = (dir == 0) ? 1 : (dir == 1) ? 0 : (dir == 2) ? 3 : 2;
            d->rooms[ny][nx].doors[opp] = 1;
        }
    }

    /* Spawn explosion particles using blood splatter system */
    for (int i = 0; i < 4; i++) {
        float angle = randf(0, 6.28f);
        float spd = randf(1.5f, 3.0f);
        spawn_blood_splatter(g, bx, by, cosf(angle) * spd, sinf(angle) * spd, 1);
    }
}

void enemy_shots_update(Game *g) {
    Room *r = current_room(g);
    Player *p = &g->player;

    for (int i = 0; i < MAX_ENEMY_SHOTS; i++) {
        EnemyShot *s = &g->enemy_shots[i];
        if (!s->active) continue;

        s->x += s->dx;
        s->y += s->dy;
        s->dist += sqrtf(s->dx * s->dx + s->dy * s->dy);

        /* Out of range */
        if (s->dist > 220.0f) { s->active = 0; continue; }

        /* Wall collide */
        if (s->x < ROOM_LEFT || s->x > ROOM_RIGHT ||
            s->y < ROOM_TOP  || s->y > ROOM_BOTTOM) {
            s->active = 0; continue;
        }

        /* Obstacle collide */
        for (int j = 0; j < r->obstacle_count; j++) {
            Obstacle *o = &r->obstacles[j];
            if (!o->active) continue;
            float dx = s->x - o->x, dy = s->y - o->y;
            float md = 3.0f + OBSTACLE_SIZE * 0.5f;
            if (dx * dx + dy * dy < md * md) { s->active = 0; break; }
        }
        if (!s->active) continue;

        /* Player collide */
        if (p->iframes == 0) {
            float pdx = s->x - p->x, pdy = s->y - p->y;
            float md = 3.0f + PLAYER_SIZE * 0.7f;
            if (pdx * pdx + pdy * pdy < md * md) {
                p->hp -= player_absorb_dmg(p, s->dmg);
                p->iframes = PLAYER_IFRAMES;
                /* Knockback from projectile direction */
                float sv = sqrtf(s->dx * s->dx + s->dy * s->dy);
                if (sv > 0.1f) {
                    p->vx = (s->dx / sv) * PLAYER_KB_FORCE * 0.7f;
                    p->vy = (s->dy / sv) * PLAYER_KB_FORCE * 0.7f;
                }
                p->kb_timer = PLAYER_KB_FRAMES;
                s->active = 0;
                audio_play(SFX_HURT_GRUNT);  /* New hurt grunt sound */
                trigger_shake(g, 4.0f, 14);
                if (player_check_death(p)) {
                    audio_play(SFX_PLAYER_DEATH);  /* Play death sound */
                    g->state = STATE_GAMEOVER;
                }
            }
        }
    }
}

void enemies_update(Game *g) {
    Player *p = &g->player;
    Room *r = current_room(g);
    const FloorInfo *fi = get_floor_info(g->current_floor);

    /* Pause AI during boss intro */
    if (g->boss_intro_timer > 0) {
        for (int i = 0; i < r->enemy_count; i++) {
            if (r->enemies[i].flash > 0) r->enemies[i].flash--;
        }
        return;
    }

    for (int i = 0; i < r->enemy_count; i++) {
        Enemy *e = &r->enemies[i];
        if (!e->active) continue;

        if (e->flash > 0) e->flash--;
        e->anim_timer++;  /* increment sprite animation counter */

        /* Yellow champion: drop creep trail periodically */
        if (e->champion == CHAMP_YELLOW) {
            if (e->creep_drop_timer > 0) e->creep_drop_timer--;
            if (e->creep_drop_timer == 0) {
                spawn_creep(g, e->x, e->y, 1, 180);
                e->creep_drop_timer = 30;
            }
        }

        /* Apply knockback velocity (decays) */
        e->x += e->kb_dx;
        e->y += e->kb_dy;
        e->kb_dx *= ENEMY_KB_FRICTION;
        e->kb_dy *= ENEMY_KB_FRICTION;
        if (fabsf(e->kb_dx) < 0.05f) e->kb_dx = 0;
        if (fabsf(e->kb_dy) < 0.05f) e->kb_dy = 0;

        /* If still knocked back significantly, skip AI movement this frame */
        float kbMag2 = e->kb_dx * e->kb_dx + e->kb_dy * e->kb_dy;
        int suppressAI = (kbMag2 > 1.0f);

        switch (e->type) {
        case ENEMY_FLY: {
            /* Smarter fly: orbits the player at some radius, occasionally dives */
            float pdx = p->x - e->x;
            float pdy = p->y - e->y;
            float pd  = sqrtf(pdx * pdx + pdy * pdy);

            e->timer--;
            if (e->state == 0) {
                /* Orbit: move perpendicular to player vector + slight inward bias */
                e->orbit_phase += 0.04f;
                float target_r = 50.0f;
                /* tangent direction (counter-clockwise) */
                if (pd > 0.5f) {
                    float tx = -pdy / pd;
                    float ty =  pdx / pd;
                    float inward = (pd - target_r) * 0.02f;
                    float spd = 1.1f * fi->enemy_speed_mult;
                    if (!suppressAI) {
                        e->dx = tx * spd + (pdx / pd) * inward;
                        e->dy = ty * spd + (pdy / pd) * inward;
                    }
                }
                if (e->timer <= 0) {
                    /* Switch to dive */
                    e->state = 1;
                    e->timer = 28;
                    if (pd > 0.5f) {
                        e->dx = (pdx / pd) * 2.6f * fi->enemy_speed_mult;
                        e->dy = (pdy / pd) * 2.6f * fi->enemy_speed_mult;
                    }
                }
            } else {
                /* Dive: keep velocity, then return to orbit */
                if (e->timer <= 0) {
                    e->state = 0;
                    e->timer = randi(60, 140);
                }
            }
            if (!suppressAI) {
                e->x += e->dx;
                e->y += e->dy;
            }
            break;
        }

        case ENEMY_GAPER:
        case ENEMY_GAPER_SMALL: {
            /* Pathfind around obstacles with simple steering + wobble */
            float gdx = p->x - e->x;
            float gdy = p->y - e->y;
            float gm = sqrtf(gdx * gdx + gdy * gdy);
            float speed = (e->type == ENEMY_GAPER_SMALL ? 1.0f : 0.8f) *
                          fi->enemy_speed_mult;
            e->wobble += 0.16f;

            if (gm > 1.0f && !suppressAI) {
                float dirx = gdx / gm;
                float diry = gdy / gm;

                /* Avoid obstacles: push perpendicular if obstacle in front */
                for (int j = 0; j < r->obstacle_count; j++) {
                    Obstacle *o = &r->obstacles[j];
                    if (!o->active) continue;
                    float odx = o->x - e->x;
                    float ody = o->y - e->y;
                    float od2 = odx * odx + ody * ody;
                    if (od2 > 60.0f * 60.0f) continue;
                    /* perpendicular avoidance */
                    float ond = sqrtf(od2);
                    if (ond < 0.1f) continue;
                    float push = (60.0f - ond) / 60.0f;
                    /* perpendicular dir to obstacle direction */
                    dirx += (-ody / ond) * push * 0.5f;
                    diry += ( odx / ond) * push * 0.5f;
                }

                /* Renormalize */
                float dl = sqrtf(dirx * dirx + diry * diry);
                if (dl > 0.1f) {
                    dirx /= dl;
                    diry /= dl;
                }

                /* Wobble for organic motion */
                float w = sinf(e->wobble) * 0.4f;
                float wx = -diry * w;
                float wy =  dirx * w;

                e->dx = (dirx + wx) * speed;
                e->dy = (diry + wy) * speed;
                e->x += e->dx;
                e->y += e->dy;
            }
            break;
        }

        case ENEMY_PACER: {
            /* Pace horizontally; charge when player is close */
            float pdx = p->x - e->x;
            float pdy = p->y - e->y;
            float pd2 = pdx * pdx + pdy * pdy;

            if (!suppressAI) {
                if (pd2 < 70.0f * 70.0f && e->state == 0) {
                    /* Begin charge */
                    e->state = 1;
                    e->timer = 24;
                    float pdmag = sqrtf(pd2);
                    if (pdmag > 0.1f) {
                        e->dx = (pdx / pdmag) * 2.8f * fi->enemy_speed_mult;
                        e->dy = (pdy / pdmag) * 2.8f * fi->enemy_speed_mult;
                    }
                }

                if (e->state == 1) {
                    /* Charging */
                    e->x += e->dx;
                    e->y += e->dy;
                    e->timer--;
                    if (e->timer <= 0) {
                        e->state = 0;
                        e->dx = (randi(0, 1) == 0 ? 1 : -1) * 1.2f * fi->enemy_speed_mult;
                        e->dy = 0;
                    }
                } else {
                    /* Pacing */
                    e->x += e->dx;
                    if (e->x <= ROOM_LEFT + ENEMY_SIZE || e->x >= ROOM_RIGHT - ENEMY_SIZE)
                        e->dx = -e->dx;
                    if (p->y > e->y + 2) e->y += 0.35f * fi->enemy_speed_mult;
                    else if (p->y < e->y - 2) e->y -= 0.35f * fi->enemy_speed_mult;
                }
            }
            break;
        }

        case ENEMY_SPIDER: {
            /* Burst movement: short hops/leaps toward player */
            e->timer--;
            if (e->state == 0) {
                /* Resting / hopping */
                e->dx *= 0.9f;
                e->dy *= 0.9f;
                if (e->timer <= 0) {
                    /* Begin leap */
                    float sdx = p->x - e->x;
                    float sdy = p->y - e->y;
                    float sm  = sqrtf(sdx * sdx + sdy * sdy);
                    if (sm > 1.0f) {
                        /* Random scatter so multiple spiders don't all run the same line */
                        float jx = randf(-0.3f, 0.3f);
                        float jy = randf(-0.3f, 0.3f);
                        e->dx = ((sdx / sm) + jx) * 3.4f * fi->enemy_speed_mult;
                        e->dy = ((sdy / sm) + jy) * 3.4f * fi->enemy_speed_mult;
                    }
                    e->state = 1;
                    e->timer = 12;
                }
            } else {
                /* Mid-leap */
                if (e->timer <= 0) {
                    e->state = 0;
                    e->timer = randi(15, 45);
                }
            }
            if (!suppressAI) {
                e->x += e->dx;
                e->y += e->dy;
            }
            break;
        }

        case ENEMY_CLOTTY: {
            /* Strafe + shoot blood in 4 directions */
            e->timer--;
            if (e->timer <= 0) {
                /* Pick new strafe direction (perpendicular to player) */
                float pdx = p->x - e->x;
                float pdy = p->y - e->y;
                float pmag = sqrtf(pdx * pdx + pdy * pdy);
                if (pmag > 0.5f) {
                    /* perpendicular */
                    float dirSign = (randi(0, 1) == 0) ? 1.0f : -1.0f;
                    e->dx = (-pdy / pmag) * 1.0f * dirSign * fi->enemy_speed_mult;
                    e->dy = ( pdx / pmag) * 1.0f * dirSign * fi->enemy_speed_mult;
                } else {
                    e->dx = randf(-1.0f, 1.0f) * fi->enemy_speed_mult;
                    e->dy = randf(-1.0f, 1.0f) * fi->enemy_speed_mult;
                }
                e->timer = randi(60, 100);
            }
            if (!suppressAI) {
                e->x += e->dx * 0.6f;
                e->y += e->dy * 0.6f;
            }

            /* Shoot */
            e->shoot_timer--;
            if (e->shoot_timer <= 0) {
                e->shoot_timer = 90 + randi(0, 40);
                float spd = 2.0f;
                spawn_enemy_shot(g, e->x, e->y,  spd,  0, 1);
                spawn_enemy_shot(g, e->x, e->y, -spd,  0, 1);
                spawn_enemy_shot(g, e->x, e->y,  0,  spd, 1);
                spawn_enemy_shot(g, e->x, e->y,  0, -spd, 1);
            }
            break;
        }

        /* === NEW ENEMY AIs === */
        case ENEMY_ATTACK_FLY: {
            /* Aggressive fly: dives at player repeatedly, faster than normal fly */
            float pdx = p->x - e->x;
            float pdy = p->y - e->y;
            float pd  = sqrtf(pdx * pdx + pdy * pdy);
            e->timer--;

            if (e->state == 0) {
                /* Brief hover before diving */
                e->dx *= 0.92f;
                e->dy *= 0.92f;
                if (e->timer <= 0) {
                    e->state = 1;
                    e->timer = 20;
                    if (pd > 0.5f) {
                        e->dx = (pdx / pd) * 3.2f * fi->enemy_speed_mult;
                        e->dy = (pdy / pd) * 3.2f * fi->enemy_speed_mult;
                    }
                }
            } else {
                /* Diving - hold course */
                if (e->timer <= 0) {
                    e->state = 0;
                    e->timer = randi(20, 50); /* shorter rest than normal fly */
                }
            }
            if (!suppressAI) { e->x += e->dx; e->y += e->dy; }
            break;
        }

        case ENEMY_POOTER: {
            /* Flies slowly, stops to shoot at player */
            e->timer--;
            e->shoot_timer--;

            if (e->state == 0) {
                /* Floating around slowly */
                float spd = 0.5f * fi->enemy_speed_mult;
                e->wobble += 0.03f;
                float wx = sinf(e->wobble) * spd;
                float wy = cosf(e->wobble * 0.7f) * spd;
                if (!suppressAI) { e->x += wx; e->y += wy; }

                if (e->shoot_timer <= 0) {
                    e->state = 1; /* stop to aim */
                    e->timer = 20;
                }
            } else {
                /* Stopped, aiming, then shoot */
                e->dx *= 0.9f; e->dy *= 0.9f;
                if (e->timer <= 0) {
                    float pdx = p->x - e->x;
                    float pdy = p->y - e->y;
                    float pm  = sqrtf(pdx * pdx + pdy * pdy);
                    if (pm > 0.5f) {
                        spawn_enemy_shot(g, e->x, e->y,
                            (pdx / pm) * 2.5f, (pdy / pm) * 2.5f, 1);
                    }
                    e->shoot_timer = randi(70, 120);
                    e->state = 0;
                }
            }
            break;
        }

        case ENEMY_HOPPER: {
            /* Hops randomly around room */
            e->timer--;
            if (e->state == 0) {
                /* On ground, waiting */
                e->dx *= 0.85f; e->dy *= 0.85f;
                e->jump_arc = 0;
                if (e->timer <= 0) {
                    /* Hop in random direction (biased toward player) */
                    float pdx = p->x - e->x;
                    float pdy = p->y - e->y;
                    float pm  = sqrtf(pdx * pdx + pdy * pdy);
                    float bias = 0.3f;
                    float rx = randf(-1, 1) + (pm > 1 ? (pdx / pm) * bias : 0);
                    float ry = randf(-1, 1) + (pm > 1 ? (pdy / pm) * bias : 0);
                    float rm = sqrtf(rx * rx + ry * ry);
                    if (rm > 0.1f) { rx /= rm; ry /= rm; }
                    e->dx = rx * 2.5f * fi->enemy_speed_mult;
                    e->dy = ry * 2.5f * fi->enemy_speed_mult;
                    e->state = 1;
                    e->timer = 18;
                }
            } else {
                /* In air */
                float t = (float)e->timer / 18.0f;
                e->jump_arc = -sinf(t * 3.14159f) * 12.0f; /* arc up then down */
                if (e->timer <= 0) {
                    e->state = 0;
                    e->timer = randi(20, 50);
                    e->jump_arc = 0;
                }
            }
            if (!suppressAI) { e->x += e->dx; e->y += e->dy; }
            break;
        }

        case ENEMY_BABY: {
            /* Slow crawl toward player */
            float pdx = p->x - e->x;
            float pdy = p->y - e->y;
            float pm  = sqrtf(pdx * pdx + pdy * pdy);
            float spd = 0.4f * fi->enemy_speed_mult;
            if (pm > 1.0f && !suppressAI) {
                e->dx = (pdx / pm) * spd;
                e->dy = (pdy / pm) * spd;
                e->x += e->dx;
                e->y += e->dy;
            }
            break;
        }

        case ENEMY_GLOBIN: {
            /* Charges at player; when killed, turns into a pile that regenerates */
            if (e->state == 0) {
                /* Normal: charge toward player */
                float pdx = p->x - e->x;
                float pdy = p->y - e->y;
                float pm  = sqrtf(pdx * pdx + pdy * pdy);
                float spd = 1.8f * fi->enemy_speed_mult;
                if (pm > 1.0f && !suppressAI) {
                    e->dx = (pdx / pm) * spd;
                    e->dy = (pdy / pm) * spd;
                    e->x += e->dx;
                    e->y += e->dy;
                }
            } else if (e->state == 1) {
                /* Collapsed pile - regenerating */
                e->regen_timer--;
                e->dx *= 0.9f; e->dy *= 0.9f;
                if (e->regen_timer <= 0) {
                    e->state = 0;
                    e->hp = e->max_hp / 2; /* regenerate to half HP */
                    if (e->hp < 1) e->hp = 1;
                }
            }
            break;
        }

        case ENEMY_BOOM_FLY: {
            /* Flies toward player, explodes on death (handled in death code) */
            float pdx = p->x - e->x;
            float pdy = p->y - e->y;
            float pd  = sqrtf(pdx * pdx + pdy * pdy);
            float spd = 1.0f * fi->enemy_speed_mult;
            if (pd > 1.0f && !suppressAI) {
                e->dx = (pdx / pd) * spd;
                e->dy = (pdy / pd) * spd;
                e->x += e->dx;
                e->y += e->dy;
            }
            break;
        }

        case ENEMY_MAW: {
            /* Walks slowly, periodically shoots aimed projectiles */
            float pdx = p->x - e->x;
            float pdy = p->y - e->y;
            float pm  = sqrtf(pdx * pdx + pdy * pdy);
            float spd = 0.6f * fi->enemy_speed_mult;
            if (pm > 1.0f && !suppressAI) {
                e->x += (pdx / pm) * spd;
                e->y += (pdy / pm) * spd;
            }
            e->shoot_timer--;
            if (e->shoot_timer <= 0) {
                e->shoot_timer = randi(60, 100);
                if (pm > 0.5f) {
                    float shotSpd = 2.0f;
                    spawn_enemy_shot(g, e->x, e->y,
                        (pdx / pm) * shotSpd, (pdy / pm) * shotSpd, 1);
                }
            }
            break;
        }

        case ENEMY_MULLIGAN: {
            /* Walks toward player, shoots occasionally; spawns flies on death */
            float pdx = p->x - e->x;
            float pdy = p->y - e->y;
            float pm  = sqrtf(pdx * pdx + pdy * pdy);
            float spd = 0.7f * fi->enemy_speed_mult;
            e->wobble += 0.12f;
            if (pm > 1.0f && !suppressAI) {
                float w = sinf(e->wobble) * 0.3f;
                e->x += (pdx / pm + w) * spd;
                e->y += (pdy / pm) * spd;
            }
            e->shoot_timer--;
            if (e->shoot_timer <= 0) {
                e->shoot_timer = randi(80, 140);
                if (pm > 0.5f) {
                    spawn_enemy_shot(g, e->x, e->y,
                        (pdx / pm) * 1.8f, (pdy / pm) * 1.8f, 1);
                }
            }
            break;
        }

        case ENEMY_HOST: {
            /* Stationary. Hides in shell (invulnerable), pops up to shoot, then hides. */
            e->timer--;
            if (e->state == 0) {
                /* Hidden - invulnerable */
                e->hidden = 1;
                if (e->timer <= 0) {
                    e->state = 1;
                    e->timer = 40; /* exposed time */
                    e->hidden = 0;
                }
            } else {
                /* Exposed - vulnerable, shoot at player */
                e->hidden = 0;
                if (e->timer == 20) { /* shoot midway through exposure */
                    float pdx = p->x - e->x;
                    float pdy = p->y - e->y;
                    float pm  = sqrtf(pdx * pdx + pdy * pdy);
                    if (pm > 0.5f) {
                        float shotSpd = 2.5f;
                        spawn_enemy_shot(g, e->x, e->y,
                            (pdx / pm) * shotSpd, (pdy / pm) * shotSpd, 1);
                        /* Second shot slightly spread */
                        float spread = 0.3f;
                        spawn_enemy_shot(g, e->x, e->y,
                            (pdx / pm + spread) * shotSpd,
                            (pdy / pm - spread) * shotSpd, 1);
                    }
                }
                if (e->timer <= 0) {
                    e->state = 0;
                    e->timer = randi(60, 120); /* hide duration */
                    e->hidden = 1;
                }
            }
            /* Host doesn't move */
            break;
        }

        case ENEMY_RED_MAW: {
            /* Stationary turret, rapidly shoots toward player */
            e->shoot_timer--;
            if (e->shoot_timer <= 0) {
                e->shoot_timer = randi(25, 45); /* fast shooting */
                float pdx = p->x - e->x;
                float pdy = p->y - e->y;
                float pm  = sqrtf(pdx * pdx + pdy * pdy);
                if (pm > 0.5f) {
                    float shotSpd = 3.0f;
                    spawn_enemy_shot(g, e->x, e->y,
                        (pdx / pm) * shotSpd, (pdy / pm) * shotSpd, 1);
                }
            }
            /* Red Maw doesn't move */
            break;
        }

        case ENEMY_LEAPER: {
            /* Jumps toward player in high arcs */
            e->timer--;
            if (e->state == 0) {
                /* On ground, preparing */
                e->dx *= 0.85f; e->dy *= 0.85f;
                e->jump_arc = 0;
                if (e->timer <= 0) {
                    float pdx = p->x - e->x;
                    float pdy = p->y - e->y;
                    float pm  = sqrtf(pdx * pdx + pdy * pdy);
                    if (pm > 1.0f) {
                        e->dx = (pdx / pm) * 3.0f * fi->enemy_speed_mult;
                        e->dy = (pdy / pm) * 3.0f * fi->enemy_speed_mult;
                    }
                    e->state = 1;
                    e->timer = 24; /* jump duration */
                }
            } else {
                /* In air - arc */
                float t = (float)e->timer / 24.0f;
                e->jump_arc = -sinf(t * 3.14159f) * 25.0f; /* high arc */
                if (e->timer <= 0) {
                    e->state = 0;
                    e->timer = randi(30, 60);
                    e->jump_arc = 0;
                    e->dx = 0; e->dy = 0;
                }
            }
            if (!suppressAI) { e->x += e->dx; e->y += e->dy; }
            break;
        }

        case ENEMY_VIS: {
            /* Floats, shoots double projectiles */
            e->wobble += 0.03f;
            float spd = 0.4f * fi->enemy_speed_mult;
            if (!suppressAI) {
                e->x += sinf(e->wobble) * spd;
                e->y += cosf(e->wobble * 0.8f) * spd;
            }
            e->shoot_timer--;
            if (e->shoot_timer <= 0) {
                e->shoot_timer = randi(50, 80);
                float pdx = p->x - e->x;
                float pdy = p->y - e->y;
                float pm  = sqrtf(pdx * pdx + pdy * pdy);
                if (pm > 0.5f) {
                    float shotSpd = 2.5f;
                    float nx = pdx / pm, ny = pdy / pm;
                    /* Double shot - slightly spread */
                    float spread = 0.15f;
                    spawn_enemy_shot(g, e->x, e->y,
                        (nx - ny * spread) * shotSpd,
                        (ny + nx * spread) * shotSpd, 1);
                    spawn_enemy_shot(g, e->x, e->y,
                        (nx + ny * spread) * shotSpd,
                        (ny - nx * spread) * shotSpd, 1);
                }
            }
            break;
        }

        /* === ENHANCED BOSS AIs === */
        case ENEMY_BOSS_DUKE: {
            /* Duke of Flies: orbits room center, spawns flies, shoots when low HP */
            e->timer--;
            e->orbit_phase += 0.015f;
            float rcx = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
            float rcy = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;

            /* Phase 1: gentle orbit + spawn flies */
            /* Phase 2 (below 50% HP): faster orbit, more frequent spawns, shoots */
            int lowHP = (e->hp < e->max_hp / 2);
            (void)0; /* orbit_speed handled inline */
            float orbit_radius = lowHP ? 40.0f : 55.0f;

            if (!suppressAI) {
                /* Figure-8 style orbit for more interesting movement */
                float a = e->orbit_phase * (lowHP ? 1.5f : 1.0f);
                float tx = rcx + cosf(a) * orbit_radius;
                float ty = rcy + sinf(a * 2.0f) * (orbit_radius * 0.6f);
                e->x += (tx - e->x) * 0.04f;
                e->y += (ty - e->y) * 0.04f;
            }

            /* Spawn flies periodically (only if under cap) */
            if (e->timer <= 0) {
                int fly_count = count_alive_flies(r);
                int interval = lowHP ? 80 : DUKE_SPAWN_INTERVAL;
                e->timer = interval;
                if (fly_count < DUKE_MAX_FLIES) {
                    boss_spawn_fly(g, r, e->x, e->y);
                    if (lowHP) boss_spawn_fly(g, r, e->x, e->y);
                }
            }

            /* Phase 2: occasionally shoot 4-way at player */
            if (lowHP) {
                e->shoot_timer--;
                if (e->shoot_timer <= 0) {
                    e->shoot_timer = randi(80, 130);
                    float spd = 1.8f;
                    boss_shoot_tear(g, e->x, e->y,  spd,  0);
                    boss_shoot_tear(g, e->x, e->y, -spd,  0);
                    boss_shoot_tear(g, e->x, e->y,  0,  spd);
                    boss_shoot_tear(g, e->x, e->y,  0, -spd);
                }
            }

            /* Bobbing animation */
            e->wobble = sinf(g->frame * 0.05f) * 3.0f;
            break;
        }

        case ENEMY_BOSS_MONSTRO: {
            /* Monstro: hop around, 8-way tear spread on land, shotgun blasts, jump attacks */
            e->timer--;

            if (e->phase == 0) {
                /* Phase 0: Idle - hop toward player slowly, occasionally shoot */
                if (!suppressAI) {
                    float mdx = p->x - e->x;
                    float mdy = p->y - e->y;
                    float mm = sqrtf(mdx * mdx + mdy * mdy);
                    if (mm > 1.0f) {
                        /* Small hops - sinusoidal bob, scaled by floor */
                        float hop = fabsf(sinf(e->anim_timer * 0.08f)) * 1.2f;
                        float mspd = (0.4f + hop * 0.2f) * fi->boss_speed_scale;
                        e->x += (mdx / mm) * mspd;
                        e->y += (mdy / mm) * mspd;
                    }
                }

                /* Shoot timer - occasional shotgun blast while idle */
                e->shoot_timer--;
                if (e->shoot_timer <= 0) {
                    e->shoot_timer = randi(90, 140);
                    monstro_shotgun(g, e->x, e->y);
                    trigger_shake(g, 2.0f, 8);
                }

                if (e->timer <= 0) {
                    /* Pick next attack: alternate jump vs big jump */
                    e->attack_pattern = (e->attack_pattern + 1) % 3;
                    if (e->attack_pattern == 2) {
                        /* Big jump to player position */
                        e->phase = 1;
                        e->timer = 45;
                        e->target_x = p->x;
                        e->target_y = p->y;
                        e->jump_vz = -4.0f; /* launch upward */
                        e->jump_z = 0;
                        /* Store start position for interpolation */
                        e->dx = (e->target_x - e->x) / 45.0f;
                        e->dy = (e->target_y - e->y) / 45.0f;
                    } else {
                        /* Small hop attack */
                        e->phase = 1;
                        e->timer = 30;
                        float jdx = p->x - e->x;
                        float jdy = p->y - e->y;
                        float jm = sqrtf(jdx * jdx + jdy * jdy);
                        if (jm > 1.0f) {
                            float jspd = 2.5f * fi->boss_speed_scale;
                            e->dx = (jdx / jm) * jspd;
                            e->dy = (jdy / jm) * jspd;
                        }
                        e->target_x = e->x + e->dx * 30;
                        e->target_y = e->y + e->dy * 30;
                        e->jump_vz = -2.5f;
                        e->jump_z = 0;
                    }
                }
            } else if (e->phase == 1) {
                /* Phase 1: In the air - moving toward target */
                e->x += e->dx;
                e->y += e->dy;
                /* Parabolic arc */
                e->jump_z += e->jump_vz;
                e->jump_vz += 0.2f; /* gravity */

                if (e->timer <= 0 || e->jump_z >= 0) {
                    /* Landing! */
                    e->jump_z = 0;
                    e->jump_vz = 0;
                    e->phase = 2;
                    e->timer = 15; /* brief landing stun */
                    trigger_shake(g, 5.0f, 20);
                    /* 8-way tear spread on landing */
                    monstro_tear_spread(g, e->x, e->y);
                    /* Spawn blood splatter at landing */
                    spawn_blood_splatter(g, e->x, e->y, 0, 0, 0);
                }
            } else {
                /* Phase 2: Landing recovery - brief stun */
                e->dx *= 0.85f;
                e->dy *= 0.85f;
                if (e->timer <= 0) {
                    e->phase = 0;
                    e->timer = randi(50, 100);
                    e->dx = 0;
                    e->dy = 0;
                }
            }
            break;
        }

        case ENEMY_BOSS_GEMINI: {
            /* Gemini: two connected entities (Contusion + Suture)
             * Before split: big body drags small body on tether
             * After split (below 50% HP): both move independently, small one charges */
            float speed_main = 0.9f * fi->boss_speed_scale;

            if (!e->gemini_split && e->hp < e->max_hp / 2) {
                /* Split! */
                e->gemini_split = 1;
                trigger_shake(g, 4.0f, 15);
            }

            if (!e->gemini_split) {
                /* Pre-split: main body slowly chases player, companion trails on tether */
                float gdx = p->x - e->x;
                float gdy = p->y - e->y;
                float gm = sqrtf(gdx * gdx + gdy * gdy);
                if (gm > 1.0f && !suppressAI) {
                    e->x += (gdx / gm) * speed_main;
                    e->y += (gdy / gm) * speed_main;
                }

                /* Companion follows main body with tether constraint */
                float tdx = e->x - e->gemini_cx;
                float tdy = e->y - e->gemini_cy;
                float td = sqrtf(tdx * tdx + tdy * tdy);
                if (td > GEMINI_TETHER_DIST) {
                    /* Snap to tether distance */
                    e->gemini_cx = e->x - (tdx / td) * GEMINI_TETHER_DIST;
                    e->gemini_cy = e->y - (tdy / td) * GEMINI_TETHER_DIST;
                }
                /* Companion gently follows */
                if (td > 10.0f) {
                    e->gemini_cx += (tdx / td) * 0.8f;
                    e->gemini_cy += (tdy / td) * 0.8f;
                }
                /* Companion wobbles */
                e->gemini_cx += sinf(g->frame * 0.07f) * 0.5f;
                e->gemini_cy += cosf(g->frame * 0.09f) * 0.5f;
            } else {
                /* Post-split: main body continues chasing, but faster */
                float gdx = p->x - e->x;
                float gdy = p->y - e->y;
                float gm = sqrtf(gdx * gdx + gdy * gdy);
                if (gm > 1.0f && !suppressAI) {
                    float post_spd = 1.6f * fi->boss_speed_scale;
                    e->x += (gdx / gm) * post_spd;
                    e->y += (gdy / gm) * post_spd;
                }

                /* Companion: aggressive charge behavior */
                e->timer--;
                if (e->state == 0) {
                    /* Waiting to charge */
                    e->gemini_cdx *= 0.92f;
                    e->gemini_cdy *= 0.92f;
                    e->gemini_cx += e->gemini_cdx;
                    e->gemini_cy += e->gemini_cdy;

                    if (e->timer <= 0) {
                        e->state = 1;
                        e->timer = 25;
                        float cdx = p->x - e->gemini_cx;
                        float cdy = p->y - e->gemini_cy;
                        float cm = sqrtf(cdx * cdx + cdy * cdy);
                        if (cm > 1.0f) {
                            float cchg = 3.5f * fi->boss_speed_scale;
                            e->gemini_cdx = (cdx / cm) * cchg;
                            e->gemini_cdy = (cdy / cm) * cchg;
                        }
                    }
                } else {
                    /* Charging */
                    e->gemini_cx += e->gemini_cdx;
                    e->gemini_cy += e->gemini_cdy;
                    if (e->timer <= 0) {
                        e->state = 0;
                        e->timer = randi(30, 60);
                    }
                }
                /* Keep companion in bounds */
                e->gemini_cx = clampf(e->gemini_cx, ROOM_LEFT + ENEMY_SIZE,
                                      ROOM_RIGHT - ENEMY_SIZE);
                e->gemini_cy = clampf(e->gemini_cy, ROOM_TOP + ENEMY_SIZE,
                                      ROOM_BOTTOM - ENEMY_SIZE);
            }

            /* Companion collision with player */
            if (p->iframes == 0) {
                float cdx = p->x - e->gemini_cx;
                float cdy = p->y - e->gemini_cy;
                float cdsq = cdx * cdx + cdy * cdy;
                float crad = PLAYER_SIZE + ENEMY_SIZE;
                if (cdsq < crad * crad) {
                    p->hp -= player_absorb_dmg(p, 1);
                    p->iframes = PLAYER_IFRAMES;
                    float cm = sqrtf(cdsq);
                    if (cm > 0.1f) {
                        p->vx = (cdx / cm) * PLAYER_KB_FORCE;
                        p->vy = (cdy / cm) * PLAYER_KB_FORCE;
                    }
                    p->kb_timer = PLAYER_KB_FRAMES;
                    audio_play(SFX_HURT_GRUNT);
                    trigger_shake(g, 4.0f, 12);
                    if (player_check_death(p)) {
                        audio_play(SFX_PLAYER_DEATH);
                        g->state = STATE_GAMEOVER;
                    }
                }
            }
            break;
        }

        case ENEMY_BOSS_LARRY: {
            /* Larry Jr: segmented worm - head leads, body follows in snake pattern
             * Speeds up when damaged, bounces off walls */
            float hpRatio = (float)e->hp / (float)e->max_hp;
            e->seg_speed = 1.8f + (1.0f - hpRatio) * 2.0f; /* faster when hurt */

            e->timer--;
            if (e->timer <= 0) {
                e->timer = randi(20, 60);
                /* Steer toward player with randomness */
                float ldx = p->x - e->x;
                float ldy = p->y - e->y;
                float lm = sqrtf(ldx * ldx + ldy * ldy);
                if (lm > 1.0f) {
                    float turn = 0.6f + hpRatio * 0.4f; /* tighter turns when more damaged */
                    e->dx = e->dx * (1.0f - turn) + (ldx / lm) * e->seg_speed * turn;
                    e->dy = e->dy * (1.0f - turn) + (ldy / lm) * e->seg_speed * turn;
                }
                /* Add sinusoidal weave */
                float weave = sinf(g->frame * 0.06f) * 0.8f;
                float perp_x = -e->dy, perp_y = e->dx;
                float pm = sqrtf(perp_x * perp_x + perp_y * perp_y);
                if (pm > 0.1f) {
                    e->dx += (perp_x / pm) * weave;
                    e->dy += (perp_y / pm) * weave;
                }
            }

            /* Normalize speed */
            {
                float sm = sqrtf(e->dx * e->dx + e->dy * e->dy);
                if (sm > 0.1f) {
                    e->dx = (e->dx / sm) * e->seg_speed;
                    e->dy = (e->dy / sm) * e->seg_speed;
                }
            }

            if (!suppressAI) {
                e->x += e->dx;
                e->y += e->dy;
            }

            /* Bounce off walls */
            if (e->x <= ROOM_LEFT + ENEMY_SIZE * 2) { e->dx = fabsf(e->dx); e->x = ROOM_LEFT + ENEMY_SIZE * 2; }
            if (e->x >= ROOM_RIGHT - ENEMY_SIZE * 2) { e->dx = -fabsf(e->dx); e->x = ROOM_RIGHT - ENEMY_SIZE * 2; }
            if (e->y <= ROOM_TOP + ENEMY_SIZE * 2) { e->dy = fabsf(e->dy); e->y = ROOM_TOP + ENEMY_SIZE * 2; }
            if (e->y >= ROOM_BOTTOM - ENEMY_SIZE * 2) { e->dy = -fabsf(e->dy); e->y = ROOM_BOTTOM - ENEMY_SIZE * 2; }

            /* Update body segments - each follows the one ahead.
               We snap segments to exactly LARRY_SEG_DIST behind the head/prev
               segment along the connecting vector. This keeps the worm body
               coherent regardless of head speed (no rubber-banding) and
               prevents segments from clipping into each other when Larry
               accelerates after taking damage. */
            for (int s = 0; s < e->seg_count; s++) {
                LarrySegment *seg = &e->segments[s];
                seg->prev_x = seg->x;
                seg->prev_y = seg->y;
                float ahead_x = (s == 0) ? e->x : e->segments[s - 1].x;
                float ahead_y = (s == 0) ? e->y : e->segments[s - 1].y;
                float sdx = ahead_x - seg->x;
                float sdy = ahead_y - seg->y;
                float sd = sqrtf(sdx * sdx + sdy * sdy);
                if (sd > 0.1f) {
                    /* Position this segment exactly LARRY_SEG_DIST behind the
                       leader, along the vector from segment → leader. */
                    seg->x = ahead_x - (sdx / sd) * LARRY_SEG_DIST;
                    seg->y = ahead_y - (sdy / sd) * LARRY_SEG_DIST;
                }
            }

            /* Segment collision with player */
            if (p->iframes == 0) {
                for (int s = 0; s < e->seg_count; s++) {
                    LarrySegment *seg = &e->segments[s];
                    float sdx = p->x - seg->x;
                    float sdy = p->y - seg->y;
                    float sr = PLAYER_SIZE + ENEMY_SIZE * 0.8f;
                    if (sdx * sdx + sdy * sdy < sr * sr) {
                        p->hp -= player_absorb_dmg(p, 1);
                        p->iframes = PLAYER_IFRAMES;
                        float sm = sqrtf(sdx * sdx + sdy * sdy);
                        if (sm > 0.1f) {
                            p->vx = (sdx / sm) * PLAYER_KB_FORCE;
                            p->vy = (sdy / sm) * PLAYER_KB_FORCE;
                        }
                        p->kb_timer = PLAYER_KB_FRAMES;
                        audio_play(SFX_HURT_GRUNT);
                        trigger_shake(g, 3.0f, 10);
                        if (player_check_death(p)) {
                            audio_play(SFX_PLAYER_DEATH);
                            g->state = STATE_GAMEOVER;
                        }
                        break;
                    }
                }
            }
            break;
        }

        case ENEMY_BOSS_FAMINE: {
            /* Famine: Phase 0 - gallops across room shooting tears, spawns pooters
             *         Phase 1 - headless body (below 40% HP), faster charges, more erratic */
            e->timer--;

            if (e->phase == 0 && e->hp < (int)(e->max_hp * 0.4f)) {
                /* Transition to headless phase */
                e->phase = 1;
                e->timer = 10;
                trigger_shake(g, 5.0f, 20);
                /* Shoot burst on phase transition */
                monstro_tear_spread(g, e->x, e->y);
            }

            /* Shoot timer (both phases) */
            e->famine_shoot_cd--;
            if (e->famine_shoot_cd <= 0) {
                int interval = (e->phase == 1) ? randi(30, 50) : randi(50, 90);
                e->famine_shoot_cd = interval;
                /* Shoot aimed tears at player */
                float fdx = p->x - e->x;
                float fdy = p->y - e->y;
                float fm = sqrtf(fdx * fdx + fdy * fdy);
                if (fm > 1.0f) {
                    float spd = (e->phase == 1) ? 3.0f : 2.2f;
                    boss_shoot_tear(g, e->x, e->y, (fdx / fm) * spd, (fdy / fm) * spd);
                    if (e->phase == 1) {
                        /* Double shot in headless phase */
                        boss_shoot_tear(g, e->x, e->y,
                                       (fdx / fm) * spd + randf(-0.5f, 0.5f),
                                       (fdy / fm) * spd + randf(-0.5f, 0.5f));
                    }
                }
            }

            if (e->state == 0) {
                /* Pause / drift phase */
                e->dx *= 0.92f;
                e->dy *= 0.92f;
                /* Drift toward player Y */
                float yDiff = p->y - e->y;
                if (fabsf(yDiff) > 3.0f) {
                    e->dy += (yDiff > 0 ? 0.1f : -0.1f);
                }
                if (!suppressAI) {
                    e->x += e->dx;
                    e->y += e->dy;
                }

                if (e->timer <= 0) {
                    e->state = 1;
                    e->timer = (e->phase == 1) ? 35 : 28;
                    /* Charge toward player */
                    float chg_speed = ((e->phase == 1) ? FAMINE_CHARGE_SPEED * 1.3f : FAMINE_CHARGE_SPEED) * fi->boss_speed_scale;
                    e->dx = (p->x > e->x) ? chg_speed : -chg_speed;
                    e->dy = (p->y - e->y) * 0.02f; /* slight vertical aim */
                    /* Spawn a fly on charge start */
                    if (count_alive_flies(r) < 4) {
                        boss_spawn_fly(g, r, e->x, e->y);
                    }
                }
            } else {
                /* Charging across room */
                if (!suppressAI) {
                    e->x += e->dx;
                    e->y += e->dy;
                }

                /* Bounce off left/right walls */
                if (e->x <= ROOM_LEFT + ENEMY_SIZE * 2 || e->x >= ROOM_RIGHT - ENEMY_SIZE * 2) {
                    e->dx = -e->dx * 0.8f;
                }

                if (e->timer <= 0) {
                    e->state = 0;
                    e->timer = randi(30, 65);
                }
            }
            break;
        }

        /* ============================
         * Phase 2 Bosses
         * ============================ */

        case ENEMY_BOSS_PEEP: {
            /* Peep: bouncing eyeball boss.
             * Phase 0: bounces around the room, spawns small eyes periodically.
             * Phase 1: (below 50% HP) detaches eyes that pursue player. */
            e->timer--;

            if (e->phase == 0 && e->hp < e->max_hp / 2) {
                e->phase = 1;
                trigger_shake(g, 4.0f, 18);
                /* On detach, spawn 2 eyes immediately */
                boss_spawn_eye(r, e->x, e->y);
                boss_spawn_eye(r, e->x, e->y);
            }

            if (!suppressAI) {
                e->x += e->dx;
                e->y += e->dy;
            }

            /* Bounce off walls */
            if (e->x <= ROOM_LEFT + ENEMY_SIZE * 2) e->dx = fabsf(e->dx);
            if (e->x >= ROOM_RIGHT - ENEMY_SIZE * 2) e->dx = -fabsf(e->dx);
            if (e->y <= ROOM_TOP + ENEMY_SIZE * 2) e->dy = fabsf(e->dy);
            if (e->y >= ROOM_BOTTOM - ENEMY_SIZE * 2) e->dy = -fabsf(e->dy);

            /* Periodic eye spawning (phase 1 faster) */
            if (e->timer <= 0) {
                int max_eyes = (e->phase == 1) ? 5 : 3;
                int interval = (e->phase == 1) ? 90 : 150;
                if (count_enemies_of_type(r, ENEMY_EYE) < max_eyes) {
                    boss_spawn_eye(r, e->x, e->y);
                }
                e->timer = interval;
            }

            /* Occasional cross-shot of tears */
            e->shoot_timer--;
            if (e->shoot_timer <= 0) {
                boss_spread_4way(g, e->x, e->y, BOSS_SHOT_SPEED * 0.9f);
                e->shoot_timer = (e->phase == 1) ? 80 : 130;
            }
            break;
        }

        case ENEMY_BOSS_GURDY: {
            /* Gurdy: stationary fat boss.
             * Cycles between:
             *   phase 0: idle pause
             *   phase 1: spawn minion wave (flies, pooters, spiders)
             *   phase 2: shoot 8-way tear spread */
            e->timer--;

            /* Mostly stationary - slight drift to follow player horizontally */
            float xDiff = p->x - e->x;
            if (fabsf(xDiff) > 4.0f) {
                e->dx = (xDiff > 0 ? 0.3f : -0.3f) * fi->boss_speed_scale;
            } else {
                e->dx *= 0.9f;
            }
            if (!suppressAI) {
                e->x += e->dx;
            }

            if (e->timer <= 0) {
                e->phase = (e->phase + 1) % 3;

                if (e->phase == 0) {
                    e->timer = 60;
                } else if (e->phase == 1) {
                    /* Spawn 3-5 minions in waves */
                    int wave_size = 3 + randi(0, 2);
                    int total_minions = count_alive_flies(r) +
                                        count_enemies_of_type(r, ENEMY_POOTER) +
                                        count_enemies_of_type(r, ENEMY_SPIDER);
                    if (total_minions < 8) {
                        for (int s = 0; s < wave_size; s++) {
                            int kind = randi(0, 2);
                            if (kind == 0) boss_spawn_fly(g, r, e->x, e->y);
                            else if (kind == 1) boss_spawn_pooter(r, e->x, e->y);
                            else boss_spawn_spider(r, e->x, e->y);
                        }
                    }
                    e->timer = 90;
                } else {
                    /* Phase 2: 8-way spread, multiple bursts */
                    boss_spread_8way(g, e->x, e->y, BOSS_SHOT_SPEED * 0.8f);
                    /* Also direct aimed shot at player */
                    float fdx = p->x - e->x;
                    float fdy = p->y - e->y;
                    float fm = sqrtf(fdx * fdx + fdy * fdy);
                    if (fm > 1.0f) {
                        float spd = BOSS_SHOT_SPEED * 1.2f;
                        boss_shoot_tear(g, e->x, e->y, (fdx / fm) * spd, (fdy / fm) * spd);
                    }
                    e->timer = 75;
                }
            }
            break;
        }

        case ENEMY_BOSS_PIN: {
            /* Pin: worm boss.
             * Phase 1 (burrowed): invulnerable, moves underground toward player position.
             * Phase 0 (emerged):  vulnerable, shoots explosive tears.
             * Cycle between the two states. */
            e->timer--;

            if (e->phase == 1) {
                /* Burrowed: invisible-ish, move toward player */
                float bdx = p->x - e->x;
                float bdy = p->y - e->y;
                float bm = sqrtf(bdx * bdx + bdy * bdy);
                if (bm > 1.0f) {
                    float spd = 2.0f * fi->boss_speed_scale;
                    e->dx = (bdx / bm) * spd;
                    e->dy = (bdy / bm) * spd;
                }
                if (!suppressAI) {
                    e->x += e->dx;
                    e->y += e->dy;
                }

                if (e->timer <= 0) {
                    /* Emerge */
                    e->phase = 0;
                    e->timer = 75;       /* time vulnerable */
                    e->shoot_timer = 15;
                    trigger_shake(g, 3.0f, 10);
                    /* Telegraph emergence burst */
                    boss_spread_8way(g, e->x, e->y, BOSS_SHOT_SPEED * 0.8f);
                }
            } else {
                /* Emerged: vulnerable, shoot at player periodically */
                e->dx *= 0.85f;
                e->dy *= 0.85f;
                if (!suppressAI) {
                    e->x += e->dx;
                    e->y += e->dy;
                }

                e->shoot_timer--;
                if (e->shoot_timer <= 0) {
                    /* Aimed tear at player */
                    float fdx = p->x - e->x;
                    float fdy = p->y - e->y;
                    float fm = sqrtf(fdx * fdx + fdy * fdy);
                    if (fm > 1.0f) {
                        float spd = BOSS_SHOT_SPEED * 1.1f;
                        boss_shoot_tear(g, e->x, e->y, (fdx / fm) * spd, (fdy / fm) * spd);
                        /* Side shots */
                        float angle = atan2f(fdy, fdx);
                        boss_shoot_tear(g, e->x, e->y,
                                       cosf(angle + 0.3f) * spd, sinf(angle + 0.3f) * spd);
                        boss_shoot_tear(g, e->x, e->y,
                                       cosf(angle - 0.3f) * spd, sinf(angle - 0.3f) * spd);
                    }
                    e->shoot_timer = 22;
                }

                if (e->timer <= 0) {
                    /* Burrow again */
                    e->phase = 1;
                    e->timer = 80;
                }
            }
            break;
        }

        case ENEMY_BOSS_HAUNT: {
            /* The Haunt: ghost boss with 2 phases.
             * Phase 0: Lil Haunts phase - spawn 2-3 Lil Haunts, drift slowly
             * Phase 1: Brimstone phase (below 50% HP) - fires beam cross + chasing
             */
            e->timer--;

            /* Phase transition */
            if (e->phase == 0 && e->hp < e->max_hp / 2) {
                e->phase = 1;
                trigger_shake(g, 5.0f, 25);
                e->shoot_timer = 60;
            }

            if (e->phase == 0) {
                /* Drift around */
                float fdx = p->x - e->x;
                float fdy = p->y - e->y;
                float fm = sqrtf(fdx * fdx + fdy * fdy);
                if (fm > 1.0f) {
                    e->dx = (fdx / fm) * 0.6f * fi->boss_speed_scale;
                    e->dy = (fdy / fm) * 0.6f * fi->boss_speed_scale;
                }
                if (!suppressAI) {
                    e->x += e->dx;
                    e->y += e->dy;
                }

                /* Spawn Lil Haunts in waves */
                if (e->timer <= 0) {
                    int alive = count_enemies_of_type(r, ENEMY_LIL_HAUNT);
                    if (alive < 3) {
                        boss_spawn_lil_haunt(r, e->x, e->y);
                        boss_spawn_lil_haunt(r, e->x, e->y);
                    }
                    e->timer = 120;
                }

                /* Slow tear shot */
                e->shoot_timer--;
                if (e->shoot_timer <= 0) {
                    float fdx2 = p->x - e->x;
                    float fdy2 = p->y - e->y;
                    float fm2 = sqrtf(fdx2 * fdx2 + fdy2 * fdy2);
                    if (fm2 > 1.0f) {
                        float spd = BOSS_SHOT_SPEED * 0.9f;
                        boss_shoot_tear(g, e->x, e->y, (fdx2 / fm2) * spd, (fdy2 / fm2) * spd);
                    }
                    e->shoot_timer = 90;
                }
            } else {
                /* Brimstone phase: chase player faster, fire cross spreads */
                float fdx = p->x - e->x;
                float fdy = p->y - e->y;
                float fm = sqrtf(fdx * fdx + fdy * fdy);
                if (fm > 1.0f) {
                    e->dx = (fdx / fm) * 1.4f * fi->boss_speed_scale;
                    e->dy = (fdy / fm) * 1.4f * fi->boss_speed_scale;
                }
                if (!suppressAI) {
                    e->x += e->dx;
                    e->y += e->dy;
                }

                e->shoot_timer--;
                if (e->shoot_timer <= 0) {
                    /* 8-way brimstone burst */
                    boss_spread_8way(g, e->x, e->y, BOSS_SHOT_SPEED);
                    e->shoot_timer = 80;
                }
            }
            break;
        }

        case ENEMY_BOSS_WIDOW: {
            /* Widow: spider boss.
             * Phase 0: idle, then jumps toward player.
             * On landing: leaves creep, sometimes spawns spiders. */
            e->timer--;

            if (e->phase == 0) {
                /* Idle: drift slowly */
                e->dx *= 0.9f;
                e->dy *= 0.9f;
                if (!suppressAI) {
                    e->x += e->dx;
                    e->y += e->dy;
                }

                if (e->timer <= 0) {
                    /* Start a jump toward player */
                    e->phase = 1;
                    e->target_x = p->x;
                    e->target_y = p->y;
                    float jdx = e->target_x - e->x;
                    float jdy = e->target_y - e->y;
                    float jm = sqrtf(jdx * jdx + jdy * jdy);
                    if (jm < 1.0f) jm = 1.0f;
                    float jspd = 4.5f * fi->boss_speed_scale;
                    e->dx = (jdx / jm) * jspd;
                    e->dy = (jdy / jm) * jspd;
                    e->jump_z = 0;
                    e->jump_vz = 7.0f;
                    e->timer = 30;
                }
            } else {
                /* Jumping */
                if (!suppressAI) {
                    e->x += e->dx;
                    e->y += e->dy;
                    e->jump_z += e->jump_vz;
                    e->jump_vz -= 0.5f;
                }

                /* Land */
                if (e->jump_z <= 0) {
                    e->jump_z = 0;
                    e->phase = 0;
                    /* Drop creep puddle */
                    spawn_creep(g, e->x, e->y, 1, 240);
                    /* Sometimes spawn 1-2 spiders on landing */
                    if (randi(0, 1) == 0) {
                        if (count_enemies_of_type(r, ENEMY_SPIDER) < 4) {
                            boss_spawn_spider(r, e->x, e->y);
                            boss_spawn_spider(r, e->x, e->y);
                        }
                    }
                    /* Shoot 4-way burst on landing */
                    boss_spread_4way(g, e->x, e->y, BOSS_SHOT_SPEED * 0.9f);
                    trigger_shake(g, 3.5f, 12);
                    e->timer = randi(40, 70);
                }
            }

            /* Periodic shot during idle */
            e->shoot_timer--;
            if (e->shoot_timer <= 0 && e->phase == 0) {
                float fdx = p->x - e->x;
                float fdy = p->y - e->y;
                float fm = sqrtf(fdx * fdx + fdy * fdy);
                if (fm > 1.0f) {
                    float spd = BOSS_SHOT_SPEED;
                    boss_shoot_tear(g, e->x, e->y, (fdx / fm) * spd, (fdy / fm) * spd);
                }
                e->shoot_timer = 90;
            }
            break;
        }

        case ENEMY_BOSS_MEGA_SATAN: {
            /* Mega Satan: Sheol final boss, multi-attack rotation.
             * Phase 0: stomp (screen shake + 8-way burst)
             * Phase 1: brimstone aimed beam (rapid aimed shots)
             * Phase 2: fireball spread (large 12-way burst) */
            e->timer--;

            /* Stay roughly centered, slow drift toward player */
            float fdx = p->x - e->x;
            float fdy = p->y - e->y;
            float fm = sqrtf(fdx * fdx + fdy * fdy);
            if (fm > 100.0f && fm > 1.0f) {
                e->dx = (fdx / fm) * 0.4f * fi->boss_speed_scale;
                e->dy = (fdy / fm) * 0.4f * fi->boss_speed_scale;
            } else {
                e->dx *= 0.85f;
                e->dy *= 0.85f;
            }
            if (!suppressAI) {
                e->x += e->dx;
                e->y += e->dy;
            }

            if (e->timer <= 0) {
                e->phase = (e->phase + 1) % 3;

                if (e->phase == 0) {
                    /* Stomp - shake and 8-way */
                    trigger_shake(g, 7.0f, 28);
                    boss_spread_8way(g, e->x, e->y, BOSS_SHOT_SPEED);
                    /* Spawn 2 spiders to keep pressure */
                    boss_spawn_spider(r, e->x, e->y);
                    e->timer = 100;
                } else if (e->phase == 1) {
                    /* Brimstone: rapid aimed bursts */
                    e->shoot_timer = 6;
                    e->attack_pattern = 5;  /* burst count */
                    e->timer = 80;
                } else {
                    /* Fireball spread: 12-way */
                    for (int i = 0; i < 12; i++) {
                        float angle = (float)i * (M_PI / 6.0f);
                        boss_shoot_tear(g, e->x, e->y,
                                       cosf(angle) * BOSS_SHOT_SPEED * 1.1f,
                                       sinf(angle) * BOSS_SHOT_SPEED * 1.1f);
                    }
                    e->timer = 100;
                }
            }

            /* Mid-phase 1 sub-shots */
            if (e->phase == 1 && e->attack_pattern > 0) {
                e->shoot_timer--;
                if (e->shoot_timer <= 0) {
                    float fdx2 = p->x - e->x;
                    float fdy2 = p->y - e->y;
                    float fm2 = sqrtf(fdx2 * fdx2 + fdy2 * fdy2);
                    if (fm2 > 1.0f) {
                        float spd = BOSS_SHOT_SPEED * 1.2f;
                        boss_shoot_tear(g, e->x, e->y, (fdx2 / fm2) * spd, (fdy2 / fm2) * spd);
                    }
                    e->shoot_timer = 12;
                    e->attack_pattern--;
                }
            }
            break;
        }

        /* ============================
         * Phase 2 Minor enemies (boss minions)
         * ============================ */

        case ENEMY_EYE: {
            /* Pursue player slowly, occasional tear shots */
            float fdx = p->x - e->x;
            float fdy = p->y - e->y;
            float fm = sqrtf(fdx * fdx + fdy * fdy);
            if (fm > 1.0f) {
                e->dx = (fdx / fm) * 1.5f;
                e->dy = (fdy / fm) * 1.5f;
            }
            if (!suppressAI) {
                e->x += e->dx;
                e->y += e->dy;
            }
            e->shoot_timer--;
            if (e->shoot_timer <= 0) {
                float spd = BOSS_SHOT_SPEED * 0.8f;
                boss_shoot_tear(g, e->x, e->y, (fdx / fm) * spd, (fdy / fm) * spd);
                e->shoot_timer = randi(80, 130);
            }
            break;
        }

        case ENEMY_LIL_HAUNT: {
            /* Charge at player erratically */
            e->timer--;
            if (e->timer <= 0) {
                float fdx = p->x - e->x;
                float fdy = p->y - e->y;
                float fm = sqrtf(fdx * fdx + fdy * fdy);
                if (fm > 1.0f) {
                    float spd = 1.8f;
                    e->dx = (fdx / fm) * spd + randf(-0.3f, 0.3f);
                    e->dy = (fdy / fm) * spd + randf(-0.3f, 0.3f);
                }
                e->timer = randi(50, 90);
            }
            if (!suppressAI) {
                e->x += e->dx;
                e->y += e->dy;
            }
            break;
        }

        } /* end switch */

        /* Keep in bounds */
        float sz = ENEMY_SIZE;
        switch (e->type) {
        case ENEMY_BOSS_DUKE:
        case ENEMY_BOSS_MONSTRO:
        case ENEMY_BOSS_GEMINI:
        case ENEMY_BOSS_LARRY:
        case ENEMY_BOSS_FAMINE:
        case ENEMY_BOSS_PEEP:
        case ENEMY_BOSS_GURDY:
        case ENEMY_BOSS_PIN:
        case ENEMY_BOSS_HAUNT:
        case ENEMY_BOSS_WIDOW:
        case ENEMY_BOSS_MEGA_SATAN:
            sz = ENEMY_SIZE * 2;
            break;
        default: break;
        }
        e->x = clampf(e->x, ROOM_LEFT + sz, ROOM_RIGHT - sz);
        e->y = clampf(e->y, ROOM_TOP + sz, ROOM_BOTTOM - sz);
    }

    /* === Enemy-vs-enemy separation pass ===
     * Push apart any overlapping enemies (skip bosses; they're large and immovable). */
    for (int i = 0; i < r->enemy_count; i++) {
        Enemy *a = &r->enemies[i];
        if (!a->active) continue;
        int aBoss = (a->type >= ENEMY_BOSS_DUKE && a->type <= ENEMY_BOSS_MEGA_SATAN);
        for (int j = i + 1; j < r->enemy_count; j++) {
            Enemy *b = &r->enemies[j];
            if (!b->active) continue;
            int bBoss = (b->type >= ENEMY_BOSS_DUKE && b->type <= ENEMY_BOSS_MEGA_SATAN);
            float dx = b->x - a->x;
            float dy = b->y - a->y;
            float minD = ENEMY_SIZE * 1.6f;
            float d2 = dx * dx + dy * dy;
            if (d2 < minD * minD && d2 > 0.01f) {
                float d = sqrtf(d2);
                float overlap = (minD - d) * 0.5f * ENEMY_SEPARATION;
                float nx = dx / d, ny = dy / d;
                if (!aBoss) { a->x -= nx * overlap; a->y -= ny * overlap; }
                if (!bBoss) { b->x += nx * overlap; b->y += ny * overlap; }
            }
        }
    }
}

/* ================================================================
 * Collisions
 * ================================================================ */

static int circle_overlap(float x1, float y1, float r1,
                           float x2, float y2, float r2) {
    float dx = x1 - x2;
    float dy = y1 - y2;
    float dr = r1 + r2;
    return (dx * dx + dy * dy) < (dr * dr);
}

static int is_boss_type(EnemyType t) {
    return t >= ENEMY_BOSS_DUKE && t <= ENEMY_BOSS_MEGA_SATAN;
}

void collisions_update(Game *g) {
    Player *p = &g->player;
    Room *r = current_room(g);

    for (int i = 0; i < r->enemy_count; i++) {
        Enemy *e = &r->enemies[i];
        if (!e->active) continue;

        float esz = is_boss_type(e->type) ? ENEMY_SIZE * 2 : ENEMY_SIZE;

        /* Tear vs Enemy */
        for (int j = 0; j < MAX_TEARS; j++) {
            Tear *t = &g->tears[j];
            if (!t->active) continue;

            if (circle_overlap(t->x, t->y, TEAR_RADIUS, e->x, e->y, esz)) {
                /* Host is invulnerable when hidden */
                if (e->type == ENEMY_HOST && e->hidden) {
                    if (!t->piercing) t->active = 0;
                    continue;
                }
                int dmg = (int)t->dmg;
                if (dmg < 1) dmg = 1;
                e->hp -= dmg;
                /* Snappier hit-flash (was 6) — about 14 frames feels more
                   responsive and reads clearly in motion. */
                e->flash = 14;
                audio_play(SFX_HIT);

                /* Spawn blood splatter at impact point */
                spawn_blood_splatter(g, t->x, t->y, t->dx, t->dy, 1);

                /* Apply knockback in direction of tear velocity */
                float tv = sqrtf(t->dx * t->dx + t->dy * t->dy);
                if (tv > 0.1f) {
                    float kbScale = is_boss_type(e->type) ? 0.25f : 1.0f;
                    e->kb_dx += (t->dx / tv) * TEAR_KNOCKBACK * kbScale;
                    e->kb_dy += (t->dy / tv) * TEAR_KNOCKBACK * kbScale;
                }

                if (!t->piercing) {
                    t->active = 0;
                }

                if (e->hp <= 0) {
                    /* Globin: collapse into pile instead of dying (first time) */
                    if (e->type == ENEMY_GLOBIN && e->state == 0 && !e->split_done) {
                        e->state = 1;          /* collapsed pile */
                        e->hp = 1;
                        e->regen_timer = 120;   /* ticks until full regen */
                        e->split_done = 1;      /* only collapse once */
                        goto skip_death;
                    }

                    /* Gaper split on death */
                    if (e->type == ENEMY_GAPER && !e->split_done) {
                        spawn_gaper_split(r, e->x, e->y);
                        spawn_gaper_split(r, e->x, e->y);
                    }

                    /* Boom Fly: explode on death */
                    if (e->type == ENEMY_BOOM_FLY) {
                        boom_fly_explode(g, e->x, e->y);
                    }

                    /* Mulligan: spawn 2-3 flies on death */
                    if (e->type == ENEMY_MULLIGAN) {
                        const FloorInfo *fi = get_floor_info(g->current_floor);
                        int nflies = randi(2, 3);
                        for (int f = 0; f < nflies; f++) {
                            spawn_fly_from_death(r, e->x, e->y, fi);
                        }
                    }
                    
                    /* Drops (difficulty-scaled) */
                    enemy_death_drops(g, r, e);

                    /* Champion bonus reward + black champion split */
                    if (e->champion != CHAMP_NONE) {
                        enemy_drop_champion_reward(g, e);
                        if (e->champion == CHAMP_BLACK && e->split_pending) {
                            /* Spawn 2 weaker copies of base enemy at same location */
                            for (int ci = 0; ci < 2; ci++) {
                                for (int ei = 0; ei < MAX_ENEMIES; ei++) {
                                    if (!r->enemies[ei].active) {
                                        Enemy *ne = &r->enemies[ei];
                                        memset(ne, 0, sizeof(Enemy));
                                        ne->type = e->type;
                                        ne->x = e->x + randf(-12.0f, 12.0f);
                                        ne->y = e->y + randf(-12.0f, 12.0f);
                                        ne->hp = ne->max_hp = 3;
                                        ne->active = 1;
                                        ne->champion = CHAMP_NONE;
                                        ne->dx = randf(-1.0f, 1.0f);
                                        ne->dy = randf(-1.0f, 1.0f);
                                        r->enemy_count++;
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    e->active = 0;
                    g->score += is_boss_type(e->type) ? 100 : 10;
                    g->kills++;
                    audio_play(SFX_ENEMY_DEATH);
                    if (is_boss_type(e->type)) {
                        trigger_shake(g, 7.0f, 40);
                        g->boss_active = 0;
                        g->boss_death_anim = 60; /* boss death explosion effect */
                        /* Track in persistent unlocks (bosses_defeated bitmask) */
                        {
                            int boss_idx = (int)e->type - (int)ENEMY_BOSS_DUKE;
                            if (boss_idx >= 0 && boss_idx < 16) {
                                g_config.bosses_defeated |= (1 << boss_idx);
                                config_save(&g_config);
                            }
                        }
                        /* Return to floor music after boss defeat */
                        music_play(music_for_floor(g->current_floor));
                    } else {
                        trigger_shake(g, 1.5f, 6);
                    }
                }
                skip_death: ; /* Globin collapse jumps here */
            }
        }

        /* Player vs Enemy */
        if (e->active && p->iframes == 0) {
            if (circle_overlap(p->x, p->y, PLAYER_SIZE, e->x, e->y, esz)) {
                p->hp -= player_absorb_dmg(p, 1);
                p->iframes = PLAYER_IFRAMES;
                /* Knockback: push player away from enemy with stun frames */
                float kdx = p->x - e->x;
                float kdy = p->y - e->y;
                float km = sqrtf(kdx * kdx + kdy * kdy);
                if (km > 0.1f) {
                    p->vx = (kdx / km) * PLAYER_KB_FORCE;
                    p->vy = (kdy / km) * PLAYER_KB_FORCE;
                } else {
                    /* Overlapping perfectly: push in random direction */
                    p->vx = randf(-1.0f, 1.0f) * PLAYER_KB_FORCE;
                    p->vy = randf(-1.0f, 1.0f) * PLAYER_KB_FORCE;
                }
                p->kb_timer = PLAYER_KB_FRAMES;
                audio_play(SFX_HURT_GRUNT);  /* New hurt grunt sound */
                trigger_shake(g, 5.0f, 18);
                if (player_check_death(p)) {
                    audio_play(SFX_PLAYER_DEATH);  /* Play death sound */
                    g->state = STATE_GAMEOVER;
                }
            }
        }
    }

    /* Check room cleared */
    if (g->state == STATE_PLAYING && !r->cleared) {
        int alive = 0;
        for (int i = 0; i < r->enemy_count; i++) {
            if (r->enemies[i].active) alive++;
        }
        if (alive == 0) {
            r->cleared = 1;
            g->rooms_cleared++;
            audio_play(SFX_ROOM_CLEAR);
            /* Clear enemy projectiles when room is cleared */
            for (int k = 0; k < MAX_ENEMY_SHOTS; k++) g->enemy_shots[k].active = 0;

            if (r->type == ROOM_BOSS) {
                /* Drop trapdoor to next floor */
                r->has_trapdoor = 1;
            }
        }
    }
}

/* ================================================================
 * Door transition system
 * ================================================================ */

/* Try to pass through a door, handling locked/curse doors */
static int try_door_unlock(Game *g, Room *r, int dir) {
    Dungeon *d = &g->dungeon;
    int dxs[] = {0, 0, -1, 1};
    int dys[] = {-1, 1, 0, 0};
    int nx = d->cur_x + dxs[dir], ny = d->cur_y + dys[dir];
    if (nx < 0 || nx >= DUNGEON_W || ny < 0 || ny >= DUNGEON_H) return 0;

    /* Key-locked door (treasure room) */
    if (r->door_locked[dir]) {
        if (g->player.keys <= 0) {
            audio_play(SFX_HURT); /* Feedback: no key available */
            return 0; /* blocked */
        }
        g->player.keys--;
        r->door_locked[dir] = 0;

        /* Unlock ALL doors leading to the target room (nx, ny) so that
         * unlocking a treasure room from one entrance also unlocks every
         * other entrance — prevents wasting multiple keys on the same room. */
        Room *target = &d->rooms[ny][nx];
        int adj_dx[] = {0, 0, -1, 1};  /* neighbour offsets per direction */
        int adj_dy[] = {-1, 1, 0, 0};
        int opp_dir[] = {1, 0, 3, 2};  /* opposite direction mapping */
        for (int i = 0; i < 4; i++) {
            /* Unlock the target room's own door in direction i */
            target->door_locked[i] = 0;
            /* Unlock the matching door in the adjacent room facing back */
            int ax = nx + adj_dx[i], ay = ny + adj_dy[i];
            if (ax >= 0 && ax < DUNGEON_W && ay >= 0 && ay < DUNGEON_H) {
                d->rooms[ay][ax].door_locked[opp_dir[i]] = 0;
            }
        }
        audio_play(SFX_DOOR);
    }

    /* Curse door - costs 1 HP to enter (cannot kill) */
    if (r->door_type[dir] == 4) {
        if (g->player.hp <= 1) {
            audio_play(SFX_HURT); /* Feedback: can't afford HP cost */
            return 0;
        }
        g->player.hp -= 1;
        audio_play(SFX_HURT);
    }

    return 1; /* allow transition */
}

void check_door_transition(Game *g) {
    Room *r = current_room(g);
    Player *p = &g->player;

    if (g->transition > 0) return;

    float midX = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
    float midY = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;

    /* Check each door direction */
    int in_range[4] = {0, 0, 0, 0};
    in_range[0] = r->doors[0] && p->y <= ROOM_TOP + DOOR_TRIGGER &&
                  fabsf(p->x - midX) < DOOR_WIDTH;
    in_range[1] = r->doors[1] && p->y >= ROOM_BOTTOM - DOOR_TRIGGER &&
                  fabsf(p->x - midX) < DOOR_WIDTH;
    in_range[2] = r->doors[2] && p->x <= ROOM_LEFT + DOOR_TRIGGER &&
                  fabsf(p->y - midY) < DOOR_WIDTH;
    in_range[3] = r->doors[3] && p->x >= ROOM_RIGHT - DOOR_TRIGGER &&
                  fabsf(p->y - midY) < DOOR_WIDTH;

    Direction dirs[] = {DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT};

    for (int d = 0; d < 4; d++) {
        if (!in_range[d]) continue;

        /* Normal (unlocked) doors require room to be cleared first.
         * Locked/special doors (treasure, curse) can always be interacted
         * with — the lock/cost check happens inside try_door_unlock. */
        int is_special = r->door_locked[d] || r->door_type[d] != 0;
        if (!r->cleared && !is_special) return;

        if (try_door_unlock(g, r, d)) do_room_transition(g, dirs[d]);
        return;
    }
}

void do_room_transition(Game *g, Direction dir) {
    Dungeon *d = &g->dungeon;
    int nx = d->cur_x, ny = d->cur_y;

    switch (dir) {
        case DIR_UP:    ny--; break;
        case DIR_DOWN:  ny++; break;
        case DIR_LEFT:  nx--; break;
        case DIR_RIGHT: nx++; break;
        default: return;
    }

    if (nx < 0 || nx >= DUNGEON_W || ny < 0 || ny >= DUNGEON_H) return;
    if (d->rooms[ny][nx].type == ROOM_NONE) return;

    d->cur_x = nx;
    d->cur_y = ny;

    Room *newRoom = &d->rooms[ny][nx];
    newRoom->visited = 1;

    for (int i = 0; i < MAX_TEARS; i++)
        g->tears[i].active = 0;

    audio_play(SFX_DOOR);

    if (!newRoom->enemies_spawned) {
        room_spawn_enemies(g, newRoom);
    }

    /* Reset enemy projectiles */
    for (int i = 0; i < MAX_ENEMY_SHOTS; i++) g->enemy_shots[i].active = 0;

    /* Brief room fade-in for polish */
    g->room_fade = 16;

    /* Play boss roar and trigger intro pause when entering boss room first time.
       Grant invulnerability frames that outlast the intro so the player gets a
       fair grace window even after the dramatic pause ends. */
    if (newRoom->type == ROOM_BOSS && !newRoom->cleared) {
        audio_play(SFX_BOSS);
        g->boss_intro_timer = BOSS_INTRO_FRAMES;
        trigger_shake(g, 4.0f, 30);
        g->boss_active = 1;
        music_play(MUS_BOSS);  /* Switch to boss fight music */
        /* Grace period: intro frames + ~1s extra invulnerability after the
           pause. iframes only tick down while gameplay is running (after the
           intro), so this becomes a clean ~1s of post-intro immunity. */
        g->player.iframes = BOSS_INTRO_FRAMES + 60;
        /* Set boss name for HUD display */
        for (int bi = 0; bi < newRoom->enemy_count; bi++) {
            if (newRoom->enemies[bi].active && is_boss_type(newRoom->enemies[bi].type)) {
                g->boss_name = boss_name_str(newRoom->enemies[bi].type);
                break;
            }
        }
    }

    float midX = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
    float midY = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
    (void)midY;

    switch (dir) {
        case DIR_UP:
            g->player.x = midX;
            g->player.y = ROOM_BOTTOM - PLAYER_SIZE - DOOR_TRIGGER - 5;
            break;
        case DIR_DOWN:
            g->player.x = midX;
            g->player.y = ROOM_TOP + PLAYER_SIZE + DOOR_TRIGGER + 5;
            break;
        case DIR_LEFT:
            g->player.x = ROOM_RIGHT - PLAYER_SIZE - DOOR_TRIGGER - 5;
            g->player.y = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
            break;
        case DIR_RIGHT:
            g->player.x = ROOM_LEFT + PLAYER_SIZE + DOOR_TRIGGER + 5;
            g->player.y = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
            break;
        default: break;
    }

    g->transition = 10;
    g->trans_dir = dir;

    /* Re-activate Holy Mantle shield on every new room entry */
    if (g->player.stats.flags & ITEM_FLAG_MANTLE) g->player.holy_mantle_active = 1;
    /* Clear any active screen creep when leaving the previous room */
    for (int i = 0; i < MAX_CREEP; i++) g->creep[i].active = 0;
}

/* ================================================================
 * Main game update
 * ================================================================ */

void game_update(Game *g, u32 kDown, u32 kHeld, circlePosition circlePos) {
    /* Track previous state to detect transitions for music changes */
    static GameState prev_state = STATE_MENU;
    if (g->state != prev_state) {
        switch (g->state) {
        case STATE_MENU:
            music_play(MUS_TITLE);
            break;
        case STATE_GAMEOVER:
            music_fade_out(60);  /* ~1s fade out on death */
            break;
        case STATE_WIN:
            music_fade_out(90);  /* ~1.5s fade out on victory */
            break;
        default:
            break;
        }
        prev_state = g->state;
    }

    switch (g->state) {
    case STATE_MENU:
        if (kDown & KEY_DUP) {
            g->menu_sel--;
            if (g->menu_sel < 0) g->menu_sel = MENU_COUNT - 1;
        }
        if (kDown & KEY_DDOWN) {
            g->menu_sel++;
            if (g->menu_sel >= MENU_COUNT) g->menu_sel = 0;
        }
        if (abs(circlePos.dy) > 100) {
            if (g->transition <= 0) {
                if (circlePos.dy > 100) {
                    g->menu_sel--;
                    if (g->menu_sel < 0) g->menu_sel = MENU_COUNT - 1;
                } else {
                    g->menu_sel++;
                    if (g->menu_sel >= MENU_COUNT) g->menu_sel = 0;
                }
                g->transition = 10;
            }
        }
        if (g->transition > 0) g->transition--;

        if (kDown & KEY_A) {
            switch (g->menu_sel) {
                case MENU_NEW_RUN:
                    /* Start a new run: jump to mode select (Story/Infinite) */
                    g->state = STATE_MODE_SELECT;
                    g->mode_sel = 0;
                    g->transition = 0;
                    break;
                case MENU_CONTINUE:
                    /* No save system yet: option is grayed out and does nothing.
                       When implemented, this would load g_config save slot. */
                    break;
                case MENU_CHALLENGES:
                    /* Map Challenges to mode select for now (Story/Infinite).
                       Future: dedicated challenge list screen. */
                    g->state = STATE_MODE_SELECT;
                    g->mode_sel = 1;  /* default to Infinite as the challenge mode */
                    g->transition = 0;
                    break;
                case MENU_STATS:
                    /* Stats and unlocks share the same screen */
                    g->state = STATE_UNLOCKS;
                    g->unlocks_scroll = 0;
                    g->transition = 0;
                    break;
                case MENU_OPTIONS:
                    g->state = STATE_SETTINGS;
                    g->settings_sel = 0;
                    g->settings_changed = 0;
                    g->transition = 0;
                    break;
            }
        }
        /* Y opens controls reference (since we removed CONTROLS from menu) */
        if (kDown & KEY_Y) {
            g->state = STATE_CONTROLS;
        }
        if (kDown & KEY_START) {
            /* Quick-start: Normal Story mode */
            g->game_mode = MODE_STORY;
            g->difficulty = DIFF_NORMAL;
            start_new_game(g);
        }
        /* Audio debug: X plays a synthesized test tone, B plays a SFX */
        if (kDown & KEY_X) {
            audio_test_tone();
        }
        if (kDown & KEY_B) {
            audio_play(SFX_SHOOT);
        }
        break;

    case STATE_MODE_SELECT:
        /* Navigate mode selection (Story / Infinite) */
        if (kDown & KEY_DUP) { g->mode_sel = 0; }
        if (kDown & KEY_DDOWN) { g->mode_sel = 1; }
        if (abs(circlePos.dy) > 100) {
            if (g->transition <= 0) {
                g->mode_sel = (circlePos.dy > 100) ? 0 : 1;
                g->transition = 10;
            }
        }
        if (g->transition > 0) g->transition--;

        if (kDown & KEY_A) {
            g->game_mode = (g->mode_sel == 0) ? MODE_STORY : MODE_INFINITE;
            g->state = STATE_CHARACTER_SELECT;
            g->char_sel = 0;
            g->transition = 0;
        }
        if (kDown & (KEY_B)) {
            g->state = STATE_MENU;
        }
        break;

    case STATE_CHARACTER_SELECT:
        /* Navigate character selection */
        if (kDown & KEY_DLEFT) {
            g->char_sel--;
            if (g->char_sel < 0) g->char_sel = CHAR_COUNT - 1;
        }
        if (kDown & KEY_DRIGHT) {
            g->char_sel++;
            if (g->char_sel >= CHAR_COUNT) g->char_sel = 0;
        }
        if (kDown & KEY_DUP) {
            g->char_sel--;
            if (g->char_sel < 0) g->char_sel = CHAR_COUNT - 1;
        }
        if (kDown & KEY_DDOWN) {
            g->char_sel++;
            if (g->char_sel >= CHAR_COUNT) g->char_sel = 0;
        }
        if (abs(circlePos.dx) > 100) {
            if (g->transition <= 0) {
                if (circlePos.dx > 100) {
                    g->char_sel++;
                    if (g->char_sel >= CHAR_COUNT) g->char_sel = 0;
                } else {
                    g->char_sel--;
                    if (g->char_sel < 0) g->char_sel = CHAR_COUNT - 1;
                }
                g->transition = 10;
            }
        }
        if (g->transition > 0) g->transition--;

        if (kDown & KEY_A) {
            /* Only allow selecting an unlocked character */
            if (g_config.unlocked_chars & (1 << g->char_sel)) {
                g->selected_character = (CharacterType)g->char_sel;
                g->state = STATE_DIFFICULTY_SELECT;
                g->diff_sel = 1;
                g->transition = 0;
            }
        }
        if (kDown & KEY_B) {
            g->state = STATE_MODE_SELECT;
        }
        break;

    case STATE_DIFFICULTY_SELECT:
        /* Navigate difficulty selection (Easy / Normal / Hard) */
        if (kDown & KEY_DUP) {
            g->diff_sel--;
            if (g->diff_sel < 0) g->diff_sel = DIFF_COUNT - 1;
        }
        if (kDown & KEY_DDOWN) {
            g->diff_sel++;
            if (g->diff_sel >= DIFF_COUNT) g->diff_sel = 0;
        }
        if (abs(circlePos.dy) > 100) {
            if (g->transition <= 0) {
                if (circlePos.dy > 100) {
                    g->diff_sel--;
                    if (g->diff_sel < 0) g->diff_sel = DIFF_COUNT - 1;
                } else {
                    g->diff_sel++;
                    if (g->diff_sel >= DIFF_COUNT) g->diff_sel = 0;
                }
                g->transition = 10;
            }
        }
        if (g->transition > 0) g->transition--;

        if (kDown & KEY_A) {
            g->difficulty = (Difficulty)g->diff_sel;
            start_new_game(g);
        }
        if (kDown & (KEY_B)) {
            g->state = STATE_CHARACTER_SELECT;
        }
        break;

    case STATE_CONTROLS:
        if (kDown & (KEY_A | KEY_B | KEY_START)) {
            g->state = STATE_MENU;
        }
        break;

    case STATE_UNLOCKS:
        if (kDown & (KEY_A | KEY_B | KEY_START)) {
            g->state = STATE_MENU;
        }
        if (kDown & KEY_DUP)   { if (g->unlocks_scroll > 0) g->unlocks_scroll--; }
        if (kDown & KEY_DDOWN) { g->unlocks_scroll++; }
        break;

    case STATE_SETTINGS: {
        /* Settings menu with 3 items: Audio toggle, SFX Vol, Music Vol */
        #define SETTINGS_COUNT 3
        if (kDown & KEY_DUP) {
            g->settings_sel--;
            if (g->settings_sel < 0) g->settings_sel = SETTINGS_COUNT - 1;
        }
        if (kDown & KEY_DDOWN) {
            g->settings_sel++;
            if (g->settings_sel >= SETTINGS_COUNT) g->settings_sel = 0;
        }
        /* Left/Right to change values */
        if (kDown & (KEY_DLEFT | KEY_DRIGHT | KEY_A)) {
            if (g->settings_sel == 0) {
                /* Toggle audio enabled */
                g_config.audio_enabled = g_config.audio_enabled ? 0 : 1;
                g->settings_changed = 1;
            }
        }
        if (kDown & KEY_DLEFT) {
            if (g->settings_sel == 1 && g_config.sfx_volume > 0.05f) {
                g_config.sfx_volume -= 0.1f;
                if (g_config.sfx_volume < 0.0f) g_config.sfx_volume = 0.0f;
                audio_set_volume(g_config.sfx_volume);
                g->settings_changed = 1;
            }
            if (g->settings_sel == 2 && g_config.music_volume > 0.05f) {
                g_config.music_volume -= 0.1f;
                if (g_config.music_volume < 0.0f) g_config.music_volume = 0.0f;
                music_set_volume(g_config.music_volume);
                g->settings_changed = 1;
            }
        }
        if (kDown & KEY_DRIGHT) {
            if (g->settings_sel == 1 && g_config.sfx_volume < 0.95f) {
                g_config.sfx_volume += 0.1f;
                if (g_config.sfx_volume > 1.0f) g_config.sfx_volume = 1.0f;
                audio_set_volume(g_config.sfx_volume);
                g->settings_changed = 1;
            }
            if (g->settings_sel == 2 && g_config.music_volume < 0.95f) {
                g_config.music_volume += 0.1f;
                if (g_config.music_volume > 1.0f) g_config.music_volume = 1.0f;
                music_set_volume(g_config.music_volume);
                g->settings_changed = 1;
            }
        }
        /* B = back (save if changed) */
        if (kDown & (KEY_B | KEY_START)) {
            if (g->settings_changed) {
                config_save(&g_config);
            }
            g->state = STATE_MENU;
        }
        break;
    }

    case STATE_PLAYING:
        g->frame++;
        g->play_time_frames++;

        /* Pause toggle: START opens stats screen */
        if (kDown & KEY_START) {
            g->state = STATE_PAUSED;
            break;
        }

        /* Update shake timer */
        if (g->shake_timer > 0) {
            g->shake_timer--;
            if (g->shake_timer <= 0) {
                g->shake_intensity = 0;
            } else {
                /* Decay intensity */
                g->shake_intensity *= 0.92f;
            }
        }

        /* Room fade-in counter */
        if (g->room_fade > 0) g->room_fade--;

        /* Boss death animation countdown */
        if (g->boss_death_anim > 0) g->boss_death_anim--;

        /* Decrement timers */
        if (g->pickup_msg_timer > 0) g->pickup_msg_timer--;
        if (g->player.book_belial_dmg_timer > 0) {
            g->player.book_belial_dmg_timer--;
            if (g->player.book_belial_dmg_timer == 0) recalc_player_stats(&g->player);
        }
        if (g->player.yum_heart_cd > 0) g->player.yum_heart_cd--;
        if (g->homing_timer > 0) g->homing_timer--;
        if (g->curse_display_timer > 0) g->curse_display_timer--;

        /* Yum Heart auto-trigger: heal 1 HP every 10s if cooldown expired & wounded */
        if (player_has_item(&g->player, ITEM_YUM_HEART)
            && g->player.yum_heart_cd == 0
            && g->player.hp < g->player.stats.max_hp) {
            g->player.hp += 1;
            if (g->player.hp > g->player.stats.max_hp) g->player.hp = g->player.stats.max_hp;
            g->player.yum_heart_cd = 600;          /* 10 second cooldown */
            audio_play(SFX_PICKUP);
        }

        /* Book of Belial: auto-activate damage buff when entering boss room */
        if (player_has_item(&g->player, ITEM_BOOK_OF_BELIAL)
            && g->boss_intro_timer > 0
            && g->player.book_belial_dmg_timer == 0) {
            g->player.book_belial_dmg_timer = 600;  /* 10 seconds of bonus damage */
            recalc_player_stats(&g->player);
        }

        /* Boss intro pause */
        if (g->boss_intro_timer > 0) {
            g->boss_intro_timer--;
            /* Still process player movement but skip enemies/collisions */
            player_update(g, kHeld, circlePos);
            break;
        }

        if (g->transition > 0) {
            g->transition--;
            return;
        }

        player_update(g, kHeld, circlePos);

        if (kHeld & KEY_X) shoot_tear(g, DIR_UP);
        if (kHeld & KEY_B) shoot_tear(g, DIR_DOWN);
        if (kHeld & KEY_Y) shoot_tear(g, DIR_LEFT);
        if (kHeld & KEY_A) shoot_tear(g, DIR_RIGHT);

        /* Bomb placement (SELECT) */
        if (kDown & KEY_SELECT) place_bomb(g);

        /* Use held pill (L) */
        if ((kDown & KEY_L) && g->player.has_pill) {
            apply_pill_effect(g, g->player.held_pill);
            g->player.has_pill = 0;
            audio_play(SFX_PICKUP);
        }
        /* Use held card (R) */
        if ((kDown & KEY_R) && g->player.has_card) {
            apply_tarot_card(g, g->player.held_card);
            g->player.has_card = 0;
            audio_play(SFX_PICKUP);
        }

        /* Active item use (Yum Heart / Book of Belial / etc) on Y? Use ZL via select? */
        /* Active items handled via button: ZL (which 3DS lacks); use SELECT+Y combo, or auto when items exist */

        tears_update(g);
        enemies_update(g);
        enemy_shots_update(g);
        bomb_update(g);
        creep_update(g);
        collisions_update(g);
        blood_particles_update(g);
        check_door_transition(g);
        break;

    case STATE_PAUSED:
        /* Stats overlay shown - resume on START */
        if (kDown & (KEY_START | KEY_B)) {
            g->state = STATE_PLAYING;
        }
        break;

    case STATE_FLOOR_TRANSITION:
        g->floor_transition_timer--;
        if (g->floor_transition_timer <= 0) {
            finish_floor_transition(g);
        }
        break;

    case STATE_GAMEOVER:
        if (g->player.hp == -999) return;
        if (kDown & KEY_START) {
            g->state = STATE_MENU;
            g->menu_sel = 0;
            g->player.hp = 0;
        }
        break;

    case STATE_WIN:
        if (kDown & KEY_START) {
            g->state = STATE_MENU;
            g->menu_sel = 0;
        }
        break;
    }
}

/* ================================================================
 * Rendering helpers
 * ================================================================ */

static void draw_heart(float cx, float cy, float size, u32 col) {
    float r = size * 0.35f;
    C2D_DrawCircleSolid(cx - r * 0.55f, cy - r * 0.2f, 0, r, col);
    C2D_DrawCircleSolid(cx + r * 0.55f, cy - r * 0.2f, 0, r, col);
    C2D_DrawTriangle(cx - size * 0.45f, cy,          col,
                     cx + size * 0.45f, cy,          col,
                     cx,                cy + size * 0.55f, col, 0);
}

/* ================================================================
 * Render: Rebirth-Style Main Menu
 * Beige parchment background, doodle decorations, pinned note for logo,
 * hand-drawn-feel option list on the left.
 * ================================================================ */

/* Helper: draw a thin "scratch" line with a slight wobble */
static void draw_doodle_scratch(float x1, float y1, float x2, float y2, u32 col) {
    /* Simple stepwise rect lines so it looks hand-drawn (slight wobble) */
    int steps = 6;
    float prev_x = x1, prev_y = y1;
    for (int s = 1; s <= steps; s++) {
        float t = s / (float)steps;
        float nx = x1 + (x2 - x1) * t;
        float ny = y1 + (y2 - y1) * t;
        /* wobble */
        float wob = sinf(t * 6.0f) * 1.2f;
        float ox = -(y2 - y1);
        float oy = (x2 - x1);
        float len = sqrtf(ox * ox + oy * oy);
        if (len > 0.001f) { ox /= len; oy /= len; }
        nx += ox * wob;
        ny += oy * wob;
        /* draw a short segment as a small rect */
        float midx = (prev_x + nx) / 2.0f;
        float midy = (prev_y + ny) / 2.0f;
        C2D_DrawRectSolid(midx - 0.8f, midy - 0.8f, 0, 1.6f, 1.6f, col);
        C2D_DrawRectSolid(prev_x - 0.8f, prev_y - 0.8f, 0, 1.6f, 1.6f, col);
        prev_x = nx;
        prev_y = ny;
    }
    C2D_DrawRectSolid(x2 - 0.8f, y2 - 0.8f, 0, 1.6f, 1.6f, col);
}

/* Helper: tiny stick-figure doodle (head + body + arms + legs) */
static void draw_doodle_stickfig(float x, float y, u32 col) {
    /* head */
    C2D_DrawCircleSolid(x, y, 0, 4.0f, col);
    C2D_DrawCircleSolid(x, y, 0, 3.2f, C2D_Color32(235, 220, 200, 255));
    /* tiny eyes */
    C2D_DrawRectSolid(x - 1.5f, y - 0.5f, 0, 1.0f, 1.0f, col);
    C2D_DrawRectSolid(x + 0.5f, y - 0.5f, 0, 1.0f, 1.0f, col);
    /* body */
    C2D_DrawRectSolid(x - 0.5f, y + 4, 0, 1.0f, 7.0f, col);
    /* arms */
    C2D_DrawRectSolid(x - 4, y + 6, 0, 8.0f, 1.0f, col);
    /* legs */
    C2D_DrawRectSolid(x - 3, y + 11, 0, 1.0f, 4.0f, col);
    C2D_DrawRectSolid(x + 2, y + 11, 0, 1.0f, 4.0f, col);
}

/* Helper: tiny bug/fly doodle */
static void draw_doodle_bug(float x, float y, u32 col) {
    C2D_DrawCircleSolid(x, y, 0, 2.5f, col);
    /* wings - thin lines */
    C2D_DrawRectSolid(x - 4, y - 1, 0, 3.0f, 1.0f, col);
    C2D_DrawRectSolid(x + 1, y - 1, 0, 3.0f, 1.0f, col);
    /* legs */
    C2D_DrawRectSolid(x - 1, y + 3, 0, 0.8f, 2.0f, col);
    C2D_DrawRectSolid(x + 0.5f, y + 3, 0, 0.8f, 2.0f, col);
}

/* Helper: tiny chest doodle outline */
static void draw_doodle_chest(float x, float y, u32 col) {
    /* base box */
    C2D_DrawRectSolid(x, y + 4, 0, 14, 9, col);
    C2D_DrawRectSolid(x + 1, y + 5, 0, 12, 7,
                      C2D_Color32(235, 220, 200, 255));
    /* lid */
    C2D_DrawRectSolid(x, y, 0, 14, 4, col);
    C2D_DrawRectSolid(x + 1, y + 1, 0, 12, 2,
                      C2D_Color32(235, 220, 200, 255));
    /* lock */
    C2D_DrawRectSolid(x + 6, y + 3, 0, 2, 3, col);
}

void render_menu(Game *g, C2D_TextBuf textBuf) {
    g->menu_timer++;

    /* ===== 1. PARCHMENT/CREAM BACKGROUND ===== */
    /* Warm beige base colour like an aged notebook page */
    u32 bg_col      = C2D_Color32(235, 220, 200, 255);
    u32 doodle_col  = C2D_Color32(160, 140, 120, 200);  /* sketchy gray-brown */
    u32 doodle_dim  = C2D_Color32(180, 165, 145, 180);
    u32 text_col    = C2D_Color32(50,  35,  25, 255);   /* dark brown/black */
    u32 text_disabled = C2D_Color32(170, 150, 130, 255);
    u32 note_col    = C2D_Color32(225, 210, 215, 255);  /* light pink/lavender pinned note */
    u32 note_edge   = C2D_Color32(180, 165, 170, 255);  /* darker note edge */
    u32 note_corner = C2D_Color32(225, 200, 175, 255);  /* folded corner */
    u32 pin_col     = C2D_Color32(70,  60,  55,  255);  /* dark pins */
    u32 pin_high    = C2D_Color32(140, 130, 120, 255);  /* pin highlight */

    /* Solid background */
    C2D_DrawRectSolid(0, 0, 0, TOP_SCREEN_WIDTH, TOP_SCREEN_HEIGHT, bg_col);

    /* Subtle paper grain - very faint specks scattered across */
    for (int i = 0; i < 24; i++) {
        float gx = (i * 53) % TOP_SCREEN_WIDTH;
        float gy = (i * 37) % TOP_SCREEN_HEIGHT;
        C2D_DrawRectSolid(gx, gy, 0, 1, 1, C2D_Color32(210, 195, 175, 120));
    }

    /* ===== 2. DOODLE DECORATIONS ===== */
    /* Top-left: stick figure (Isaac-like) */
    draw_doodle_stickfig(40.0f, 20.0f, doodle_col);

    /* Bottom-left under note: small bug */
    draw_doodle_bug(35.0f, 75.0f, doodle_dim);

    /* Left side: scratches */
    draw_doodle_scratch(30.0f, 105.0f, 60.0f, 95.0f, doodle_dim);
    draw_doodle_scratch(20.0f, 130.0f, 40.0f, 145.0f, doodle_dim);

    /* Bottom-left tiny stick figure */
    draw_doodle_stickfig(28.0f, 170.0f, doodle_col);

    /* Top-right: ink splatter (cluster of dots) */
    for (int s = 0; s < 9; s++) {
        float ang = s * 0.7f;
        float dist = 4 + s * 1.5f;
        float dx = TOP_SCREEN_WIDTH - 60.0f + cosf(ang) * dist;
        float dy = 25.0f + sinf(ang) * dist;
        float sz = (s % 3 == 0) ? 2.0f : 1.2f;
        C2D_DrawCircleSolid(dx, dy, 0, sz, doodle_dim);
    }

    /* Right side: chest doodle */
    draw_doodle_chest(TOP_SCREEN_WIDTH - 40.0f, 60.0f, doodle_col);

    /* Right side: scratches */
    draw_doodle_scratch(TOP_SCREEN_WIDTH - 50.0f, 105.0f,
                        TOP_SCREEN_WIDTH - 25.0f, 100.0f, doodle_dim);
    draw_doodle_scratch(TOP_SCREEN_WIDTH - 30.0f, 130.0f,
                        TOP_SCREEN_WIDTH - 50.0f, 145.0f, doodle_dim);

    /* Bottom-right small bug */
    draw_doodle_bug(TOP_SCREEN_WIDTH - 35.0f, 175.0f, doodle_dim);

    /* ===== 3. PINNED NOTE / PAPER (Title card) ===== */
    float note_w = 165.0f;
    float note_h = 95.0f;
    float note_cx = TOP_SCREEN_WIDTH / 2.0f;
    /* Subtle bob animation */
    float note_bob = sinf(g->menu_timer * 0.025f) * 1.5f;
    float note_x = note_cx - note_w / 2.0f;
    float note_y = 15.0f + note_bob;

    /* Soft shadow behind note */
    C2D_DrawRectSolid(note_x + 4, note_y + 6, 0, note_w, note_h,
                      C2D_Color32(0, 0, 0, 30));

    /* Outer/edge layer */
    C2D_DrawRectSolid(note_x - 2, note_y - 2, 0, note_w + 4, note_h + 4, note_edge);

    /* Main note body */
    C2D_DrawRectSolid(note_x, note_y, 0, note_w, note_h, note_col);

    /* Slight highlight on the upper-left to look creased */
    C2D_DrawRectSolid(note_x + 6, note_y + 4, 0, note_w - 12, 2,
                      C2D_Color32(240, 230, 230, 180));

    /* Folded corner at bottom-right */
    float fc_size = 22.0f;
    /* The triangular fold - simulated by two rects of slightly differing tone */
    C2D_DrawRectSolid(note_x + note_w - fc_size + 4,
                      note_y + note_h - fc_size + 4,
                      0, fc_size, fc_size, note_corner);
    /* Edge shadow on the fold seam */
    C2D_DrawRectSolid(note_x + note_w - fc_size + 4,
                      note_y + note_h - fc_size + 4,
                      0, fc_size, 1, note_edge);
    C2D_DrawRectSolid(note_x + note_w - fc_size + 4,
                      note_y + note_h - fc_size + 4,
                      0, 1, fc_size, note_edge);

    /* Two dark pins at the top of the note */
    float pin_y = note_y + 3;
    float pin_lx = note_x + note_w * 0.30f;
    float pin_rx = note_x + note_w * 0.70f;
    /* Pin shadow */
    C2D_DrawCircleSolid(pin_lx + 1, pin_y + 1, 0, 4.5f, C2D_Color32(0, 0, 0, 80));
    C2D_DrawCircleSolid(pin_rx + 1, pin_y + 1, 0, 4.5f, C2D_Color32(0, 0, 0, 80));
    /* Pin head */
    C2D_DrawCircleSolid(pin_lx, pin_y, 0, 4.5f, pin_col);
    C2D_DrawCircleSolid(pin_rx, pin_y, 0, 4.5f, pin_col);
    /* Pin highlight (small dot for shine) */
    C2D_DrawCircleSolid(pin_lx - 1.2f, pin_y - 1.2f, 0, 1.5f, pin_high);
    C2D_DrawCircleSolid(pin_rx - 1.2f, pin_y - 1.2f, 0, 1.5f, pin_high);

    /* ===== 4. LOGO INSIDE THE NOTE ===== */
    if (sheet_menu_logo) {
        /* Fit logo inside the note (note is ~165x95) */
        float logo_pulse = sinf(g->menu_timer * 0.04f);
        float logo_scale = 0.40f + logo_pulse * 0.01f;
        float logo_cx = note_cx;
        float logo_cy = note_y + note_h / 2.0f + 4.0f;
        spr_draw(sheet_menu_logo, menu_logo_atlas_menu_logo_idx,
                 logo_cx, logo_cy, logo_scale, logo_scale);
    } else {
        /* Fallback: hand-drawn title text inside note */
        C2D_Text titleA, titleB;
        C2D_TextParse(&titleA, textBuf, "BINDING");
        C2D_TextOptimize(&titleA);
        C2D_DrawText(&titleA, C2D_WithColor,
                     note_x + 40, note_y + 30, 0, 0.6f, 0.6f, text_col);
        C2D_TextParse(&titleB, textBuf, "of  ISAAC");
        C2D_TextOptimize(&titleB);
        C2D_DrawText(&titleB, C2D_WithColor,
                     note_x + 30, note_y + 55, 0, 0.55f, 0.55f, text_col);
    }

    /* Tiny doodle face inside the note (lower-right area) */
    {
        float fx = note_x + note_w * 0.78f;
        float fy = note_y + note_h * 0.55f;
        C2D_DrawCircleSolid(fx, fy, 0, 5.0f, note_corner);
        /* eyes */
        C2D_DrawRectSolid(fx - 1.8f, fy - 1.0f, 0, 1.2f, 1.5f, text_col);
        C2D_DrawRectSolid(fx + 0.6f, fy - 1.0f, 0, 1.2f, 1.5f, text_col);
    }

    /* ===== 5. MENU OPTIONS (hand-drawn sprite text, Isaac Rebirth style) ===== */
    /* Sprite indices for each menu option (from menu_text_atlas.h) */
    int opt_sprites[MENU_COUNT] = {
        menu_text_atlas_menu_newrun_idx,
        menu_text_atlas_menu_continue_idx,   /* swapped to gray version if disabled */
        menu_text_atlas_menu_challenges_idx,
        menu_text_atlas_menu_stats_idx,
        menu_text_atlas_menu_options_idx
    };

    float opt_x = 60.0f;
    float opt_y0 = 122.0f;
    float opt_gap = 22.0f;

    for (int i = 0; i < MENU_COUNT; i++) {
        int selected = (i == g->menu_sel);
        int disabled = (i == MENU_CONTINUE);   /* CONTINUE grayed out (no save) */
        float yPos = opt_y0 + i * opt_gap;

        /* Selected option: subtle bounce + slight scale up */
        float scale = selected ? 0.58f : 0.52f;
        float bounce = selected ? (sinf(g->menu_timer * 0.18f) * 1.6f) : 0.0f;
        float xOffset = selected ? (2.0f + bounce * 0.5f) : 0.0f;

        /* Selection arrow cursor (hand-drawn sprite) */
        if (selected && sheet_menu_text) {
            float ax = opt_x - 18.0f + bounce;
            float ay = yPos + 8.0f;
            spr_draw(sheet_menu_text, menu_text_atlas_menu_arrow_idx,
                     ax, ay, 0.55f, 0.55f);
        }

        /* Draw hand-drawn text sprite */
        if (sheet_menu_text) {
            int spr_idx = opt_sprites[i];
            /* Use grayed-out version for disabled CONTINUE */
            if (disabled) spr_idx = menu_text_atlas_menu_continue_gray_idx;

            if (selected) {
                /* Pulse alpha for selected option */
                float pulse_alpha = 0.85f + sinf(g->menu_timer * 0.18f) * 0.15f;
                spr_draw_at(sheet_menu_text, spr_idx,
                            opt_x + xOffset, yPos, scale, scale);
                (void)pulse_alpha;  /* sprite already looks good; alpha optional */
            } else {
                spr_draw_at(sheet_menu_text, spr_idx,
                            opt_x + xOffset, yPos, scale, scale);
            }
        } else {
            /* Fallback: text rendering if sprite sheet missing */
            const char *opt_labels[MENU_COUNT] = {
                "NEW RUN", "CONTINUE", "CHALLENGES", "STATS", "OPTIONS"
            };
            u32 col = disabled ? text_disabled : text_col;
            C2D_Text optText;
            C2D_TextParse(&optText, textBuf, opt_labels[i]);
            C2D_TextOptimize(&optText);
            C2D_DrawText(&optText, C2D_WithColor,
                         opt_x + xOffset, yPos, 0, 0.65f, 0.65f, col);
        }
    }

    /* ===== 6. BOTTOM HINT TEXT ===== */
    C2D_Text hint;
    C2D_TextParse(&hint, textBuf, "A: Select   D-Pad: Move   Y: Controls");
    C2D_TextOptimize(&hint);
    float hint_alpha = 0.6f + sinf(g->menu_timer * 0.08f) * 0.2f;
    C2D_DrawText(&hint, C2D_WithColor, 60, TOP_SCREEN_HEIGHT - 16, 0,
                 0.42f, 0.42f,
                 C2D_Color32(100, 80, 60, (int)(hint_alpha * 255)));

    /* Version text bottom-right */
    C2D_Text version;
    C2D_TextParse(&version, textBuf, "v2.0");
    C2D_TextOptimize(&version);
    C2D_DrawText(&version, C2D_WithColor,
                 TOP_SCREEN_WIDTH - 35, TOP_SCREEN_HEIGHT - 16, 0,
                 0.4f, 0.4f, C2D_Color32(120, 100, 80, 220));

    /* ==== AUDIO DEBUG OVERLAY ====
     * Shown on the title screen so audio failures are diagnosable on
     * real hardware without a debug console.  Press X to play a test
     * tone, B to play a test SFX. */
    {
        AudioDebug ad;
        audio_debug_get(&ad);

        char dbg[160];
        snprintf(dbg, sizeof(dbg),
                 "AUDIO: %s  init=%d sfx_loaded=%d/%d",
                 audio_status_string(),
                 ad.init_result, ad.sfx_load_count, SFX_COUNT);

        C2D_Text line1;
        C2D_TextParse(&line1, textBuf, dbg);
        C2D_TextOptimize(&line1);
        C2D_DrawText(&line1, C2D_WithColor, 5, 2, 0,
                     0.36f, 0.36f, C2D_Color32(0, 100, 0, 255));

        char dbg2[160];
        snprintf(dbg2, sizeof(dbg2),
                 "csndChan=0x%08lX last_play=%d chn=%d sfx=%d tone=%d",
                 (unsigned long)ad.csnd_channels,
                 ad.last_play_result, ad.last_play_chn, ad.last_play_sfx,
                 ad.test_tone_result);

        C2D_Text line2;
        C2D_TextParse(&line2, textBuf, dbg2);
        C2D_TextOptimize(&line2);
        C2D_DrawText(&line2, C2D_WithColor, 5, 14, 0,
                     0.34f, 0.34f, C2D_Color32(0, 80, 0, 255));

        char dbg3[160];
        snprintf(dbg3, sizeof(dbg3),
                 "mus_load=%d/%d last_mus_play=%d  [X]=tone [B]=sfx",
                 ad.music_load_attempts - ad.music_load_failures,
                 ad.music_load_attempts,
                 ad.last_music_play_result);

        C2D_Text line3;
        C2D_TextParse(&line3, textBuf, dbg3);
        C2D_TextOptimize(&line3);
        C2D_DrawText(&line3, C2D_WithColor, 5, 26, 0,
                     0.34f, 0.34f, C2D_Color32(0, 80, 0, 255));
    }
}

/* ================================================================
 * Render: Game Mode Select screen
 * ================================================================ */

/* Shared helper: draw the animated gradient background used across menu screens */
static void draw_menu_background(Game *g) {
    g->menu_timer++;

    /* Initialize particles on first frame */
    if (!g->particles_init) {
        for (int i = 0; i < 20; i++) {
            g->particle_x[i] = randf(0, TOP_SCREEN_WIDTH);
            g->particle_y[i] = randf(0, TOP_SCREEN_HEIGHT);
            g->particle_vx[i] = randf(-0.3f, 0.3f);
            g->particle_vy[i] = randf(-0.5f, -0.1f);
        }
        g->particles_init = 1;
    }

    /* Animated gradient background */
    float grad_shift = sinf(g->menu_timer * 0.02f) * 15.0f;
    for (int y = 0; y < TOP_SCREEN_HEIGHT; y += 4) {
        float ratio = y / (float)TOP_SCREEN_HEIGHT;
        int rv = (int)(20 + ratio * 15 + grad_shift);
        int gv = (int)(10 + ratio * 8 + grad_shift * 0.5f);
        int bv = (int)(5 + ratio * 5);
        C2D_DrawRectSolid(0, y, 0, TOP_SCREEN_WIDTH, 4,
                          C2D_Color32(rv, gv, bv, 255));
    }

    /* Floating particles */
    for (int i = 0; i < 20; i++) {
        g->particle_x[i] += g->particle_vx[i];
        g->particle_y[i] += g->particle_vy[i];
        if (g->particle_y[i] < -10) {
            g->particle_y[i] = TOP_SCREEN_HEIGHT + 10;
            g->particle_x[i] = randf(0, TOP_SCREEN_WIDTH);
        }
        if (g->particle_x[i] < -10) g->particle_x[i] = TOP_SCREEN_WIDTH + 10;
        if (g->particle_x[i] > TOP_SCREEN_WIDTH + 10) g->particle_x[i] = -10;
        float pulse = sinf(g->menu_timer * 0.05f + i * 0.3f);
        int alpha = (int)(30 + pulse * 20);
        int size = (i % 3) + 2;
        C2D_DrawRectSolid(g->particle_x[i], g->particle_y[i], 0, size, size,
                          C2D_Color32(200, 150, 100, alpha));
    }

    /* Decorative border */
    u32 borderGlow = C2D_Color32(150, 100, 50, 180);
    u32 borderMain = C2D_Color32(200, 140, 80, 255);
    C2D_DrawRectSolid(5, 5, 0, TOP_SCREEN_WIDTH - 10, 2, borderGlow);
    C2D_DrawRectSolid(5, TOP_SCREEN_HEIGHT - 7, 0, TOP_SCREEN_WIDTH - 10, 2, borderGlow);
    C2D_DrawRectSolid(5, 5, 0, 2, TOP_SCREEN_HEIGHT - 10, borderGlow);
    C2D_DrawRectSolid(TOP_SCREEN_WIDTH - 7, 5, 0, 2, TOP_SCREEN_HEIGHT - 10, borderGlow);
    C2D_DrawRectSolid(8, 8, 0, TOP_SCREEN_WIDTH - 16, 3, borderMain);
    C2D_DrawRectSolid(8, TOP_SCREEN_HEIGHT - 11, 0, TOP_SCREEN_WIDTH - 16, 3, borderMain);
    C2D_DrawRectSolid(8, 8, 0, 3, TOP_SCREEN_HEIGHT - 16, borderMain);
    C2D_DrawRectSolid(TOP_SCREEN_WIDTH - 11, 8, 0, 3, TOP_SCREEN_HEIGHT - 16, borderMain);
}

/* Helper: draw a selection option row (used by mode and difficulty screens).
   The "selected" state is intentionally loud — a bright accent rail, a glowing
   outline, and oversized animated chevrons — so it is always obvious from
   across the room which option will be picked. Unselected rows are dimmed so
   the eye is drawn straight to the active one. */
static void draw_menu_option(C2D_TextBuf textBuf, const char *label, const char *desc,
                             float yPos, int selected, int timer) {
    if (selected) {
        float sel_pulse = sinf(timer * 0.15f);
        float box_width = 300 + sel_pulse * 10;
        float box_x = (TOP_SCREEN_WIDTH - box_width) / 2;

        /* Outer glow (large soft halo) */
        C2D_DrawRectSolid(box_x - 10, yPos - 12, 0, box_width + 20, 58,
                          C2D_Color32(255, 180, 80, 60));
        C2D_DrawRectSolid(box_x - 6,  yPos - 9,  0, box_width + 12, 52,
                          C2D_Color32(255, 200, 100, 120));

        /* Bright accent border (thick, pulsing) */
        u8 borderA = (u8)(220 + sinf(timer * 0.18f) * 35);
        C2D_DrawRectSolid(box_x - 3, yPos - 6, 0, box_width + 6, 46,
                          C2D_Color32(255, 215, 130, borderA));

        /* Inner fill */
        C2D_DrawRectSolid(box_x, yPos - 3, 0, box_width, 40,
                          C2D_Color32(110, 60, 30, 255));

        /* Left vertical accent rail (bright) */
        C2D_DrawRectSolid(box_x, yPos - 3, 0, 5, 40,
                          C2D_Color32(255, 230, 150, 255));

        /* Animated chevron arrows (larger and brighter than before) */
        C2D_Text al, ar;
        float ao = sinf(timer * 0.22f) * 4.5f;
        C2D_TextParse(&al, textBuf, ">>");
        C2D_TextOptimize(&al);
        C2D_DrawText(&al, C2D_WithColor, box_x + 14 + ao, yPos - 1, 0,
                     0.85f, 0.85f, C2D_Color32(255, 240, 170, 255));
        C2D_TextParse(&ar, textBuf, "<<");
        C2D_TextOptimize(&ar);
        C2D_DrawText(&ar, C2D_WithColor, box_x + box_width - 38 - ao, yPos - 1, 0,
                     0.85f, 0.85f, C2D_Color32(255, 240, 170, 255));
    }

    /* Label text — bigger, brighter when selected; dimmer when not */
    C2D_Text lbl;
    C2D_TextParse(&lbl, textBuf, label);
    C2D_TextOptimize(&lbl);
    float ts = selected ? 0.75f : 0.50f;
    u32 tc = selected ? C2D_Color32(255, 250, 220, 255)
                      : C2D_Color32(130, 105, 80,  200);
    C2D_DrawText(&lbl, C2D_WithColor, TOP_SCREEN_WIDTH / 2.0f - 55, yPos, 0, ts, ts, tc);

    /* Description text (smaller, below label) */
    if (desc) {
        C2D_Text dt;
        C2D_TextParse(&dt, textBuf, desc);
        C2D_TextOptimize(&dt);
        u32 dc = selected ? C2D_Color32(220, 200, 170, 240)
                          : C2D_Color32(110, 90,  75,  150);
        C2D_DrawText(&dt, C2D_WithColor, TOP_SCREEN_WIDTH / 2.0f - 80, yPos + 21, 0,
                     0.38f, 0.38f, dc);
    }
}

void render_mode_select(Game *g, C2D_TextBuf textBuf) {
    draw_menu_background(g);

    /* Title */
    C2D_Text title;
    C2D_TextParse(&title, textBuf, "SELECT GAME MODE");
    C2D_TextOptimize(&title);
    float tp = sinf(g->menu_timer * 0.04f) * 0.03f;
    C2D_DrawText(&title, C2D_WithColor, 95, 28, 0, 0.8f + tp, 0.8f + tp,
                 C2D_Color32(255, 230, 180, 255));

    /* Decorative line under title */
    float lw = 200 + sinf(g->menu_timer * 0.06f) * 10;
    C2D_DrawRectSolid((TOP_SCREEN_WIDTH - lw) / 2, 55, 0, lw, 2,
                      C2D_Color32(200, 140, 80, 200));

    /* Mode options */
    draw_menu_option(textBuf, "Story Mode",
                     "Beat all floors to win. Classic Isaac!",
                     80, g->mode_sel == 0, g->menu_timer);
    draw_menu_option(textBuf, "Infinite Mode",
                     "Floors loop forever. How far can you go?",
                     145, g->mode_sel == 1, g->menu_timer);

    /* Hint */
    C2D_Text hint;
    C2D_TextParse(&hint, textBuf, "A: Select    B: Back");
    C2D_TextOptimize(&hint);
    float ha = 0.5f + sinf(g->menu_timer * 0.1f) * 0.3f;
    C2D_DrawText(&hint, C2D_WithColor, 115, 218, 0, 0.48f, 0.48f,
                 C2D_Color32(150, 130, 110, (int)(ha * 255)));
}

/* ================================================================
 * Render: Difficulty Select screen
 * ================================================================ */

void render_difficulty_select(Game *g, C2D_TextBuf textBuf) {
    draw_menu_background(g);

    /* Title with mode indicator */
    C2D_Text title;
    const char *mode_str = (g->game_mode == MODE_INFINITE) ? "INFINITE" : "STORY";
    char title_buf[48];
    snprintf(title_buf, sizeof(title_buf), "SELECT DIFFICULTY  [%s]", mode_str);
    C2D_TextParse(&title, textBuf, title_buf);
    C2D_TextOptimize(&title);
    float tp = sinf(g->menu_timer * 0.04f) * 0.02f;
    C2D_DrawText(&title, C2D_WithColor, 42, 20, 0, 0.65f + tp, 0.65f + tp,
                 C2D_Color32(255, 230, 180, 255));

    /* Decorative line */
    float lw = 220 + sinf(g->menu_timer * 0.06f) * 10;
    C2D_DrawRectSolid((TOP_SCREEN_WIDTH - lw) / 2, 45, 0, lw, 2,
                      C2D_Color32(200, 140, 80, 200));

    /* Difficulty options with descriptions */
    const char *labels[] = { "Easy", "Normal", "Hard" };
    const char *descs[] = {
        "75% enemy HP, more hearts, extra starting HP",
        "Balanced gameplay. The classic experience.",
        "125% enemy HP, fewer hearts, pricier shops"
    };

    /* Color-coded difficulty backgrounds */
    u32 diff_colors[] = {
        C2D_Color32(80, 180, 80, 60),   /* green tint for easy */
        C2D_Color32(180, 180, 80, 60),  /* yellow tint for normal */
        C2D_Color32(200, 60, 60, 60)    /* red tint for hard */
    };

    for (int i = 0; i < DIFF_COUNT; i++) {
        float yPos = 60 + i * 55;

        /* Subtle colored background strip for each difficulty */
        if (i == g->diff_sel) {
            float pw = sinf(g->menu_timer * 0.1f) * 4;
            C2D_DrawRectSolid(30, yPos - 8, 0, TOP_SCREEN_WIDTH - 60 + pw, 48,
                              diff_colors[i]);
        }

        draw_menu_option(textBuf, labels[i], descs[i], yPos, i == g->diff_sel,
                         g->menu_timer);
    }

    /* Hint */
    C2D_Text hint;
    C2D_TextParse(&hint, textBuf, "A: Start    B: Back");
    C2D_TextOptimize(&hint);
    float ha = 0.5f + sinf(g->menu_timer * 0.1f) * 0.3f;
    C2D_DrawText(&hint, C2D_WithColor, 118, 218, 0, 0.48f, 0.48f,
                 C2D_Color32(150, 130, 110, (int)(ha * 255)));
}

/* ================================================================
 * Character helpers and names
 * ================================================================ */

const char *character_name(CharacterType c) {
    switch (c) {
        case CHAR_ISAAC:     return "Isaac";
        case CHAR_MAGDALENE: return "Magdalene";
        case CHAR_CAIN:      return "Cain";
        case CHAR_JUDAS:     return "Judas";
        default:             return "?";
    }
}

static const char *character_blurb(CharacterType c) {
    switch (c) {
        case CHAR_ISAAC:     return "Balanced. 3 hearts. The default.";
        case CHAR_MAGDALENE: return "4 hearts, slower. Yum Heart heals.";
        case CHAR_CAIN:      return "2 hearts, +damage, +speed, +luck.";
        case CHAR_JUDAS:     return "1 heart, glass cannon. Book of Belial.";
        default:             return "";
    }
}

static u32 character_tint(CharacterType c) {
    switch (c) {
        case CHAR_MAGDALENE: return C2D_Color32(255, 160, 200, 255); /* pink */
        case CHAR_CAIN:      return C2D_Color32(220, 200, 80,  255); /* gold */
        case CHAR_JUDAS:     return C2D_Color32(140, 40,  60,  255); /* dark red */
        case CHAR_ISAAC:
        default:             return C2D_Color32(255, 220, 180, 255); /* default */
    }
}

void render_character_select(Game *g, C2D_TextBuf textBuf) {
    draw_menu_background(g);

    /* Title */
    C2D_Text title;
    C2D_TextParse(&title, textBuf, "SELECT CHARACTER");
    C2D_TextOptimize(&title);
    float tp = sinf(g->menu_timer * 0.04f) * 0.03f;
    C2D_DrawText(&title, C2D_WithColor, 95, 18, 0, 0.8f + tp, 0.8f + tp,
                 C2D_Color32(255, 230, 180, 255));

    /* Decorative line */
    float lw = 240 + sinf(g->menu_timer * 0.06f) * 10;
    C2D_DrawRectSolid((TOP_SCREEN_WIDTH - lw) / 2, 45, 0, lw, 2,
                      C2D_Color32(200, 140, 80, 200));

    /* 4 character cards side-by-side */
    const float card_w = 80.0f;
    const float card_h = 110.0f;
    const float spacing = 8.0f;
    const float total_w = card_w * 4 + spacing * 3;
    const float start_x = (TOP_SCREEN_WIDTH - total_w) / 2.0f;
    const float card_y = 60.0f;

    for (int i = 0; i < CHAR_COUNT; i++) {
        float cx = start_x + i * (card_w + spacing);
        int selected = (i == g->char_sel);
        int unlocked_bit = (g_config.unlocked_chars & (1 << i)) ? 1 : 0;
        u32 tint = character_tint((CharacterType)i);

        /* Card background */
        u32 card_bg = selected ? C2D_Color32(80, 50, 30, 240)
                               : C2D_Color32(40, 30, 20, 200);
        float pulse = selected ? sinf(g->menu_timer * 0.15f) * 4.0f : 0.0f;
        C2D_DrawRectSolid(cx - 2 - pulse / 2, card_y - 2 - pulse / 2, 0,
                          card_w + 4 + pulse, card_h + 4 + pulse,
                          C2D_Color32(120, 80, 40, selected ? 220 : 100));
        C2D_DrawRectSolid(cx, card_y, 0, card_w, card_h, card_bg);

        if (unlocked_bit) {
            /* === Unlocked: show character preview, name, and HP === */
            /* Color swatch (character tint preview) */
            C2D_DrawRectSolid(cx + 8, card_y + 8, 0, card_w - 16, 40, tint);

            /* Character icon - placeholder circle */
            C2D_DrawCircleSolid(cx + card_w / 2, card_y + 28, 0, 14, C2D_Color32(255, 240, 220, 255));
            /* Eye dots */
            C2D_DrawCircleSolid(cx + card_w / 2 - 4, card_y + 26, 0, 2, C2D_Color32(0, 0, 0, 255));
            if (i != CHAR_CAIN) {  /* Cain has only one eye */
                C2D_DrawCircleSolid(cx + card_w / 2 + 4, card_y + 26, 0, 2, C2D_Color32(0, 0, 0, 255));
            } else {
                /* eye-patch */
                C2D_DrawRectSolid(cx + card_w / 2 + 1, card_y + 24, 0, 8, 4, C2D_Color32(40, 40, 40, 255));
            }

            /* Character name */
            C2D_Text nm;
            C2D_TextParse(&nm, textBuf, character_name((CharacterType)i));
            C2D_TextOptimize(&nm);
            u32 tc = selected ? C2D_Color32(255, 240, 200, 255)
                              : C2D_Color32(180, 160, 130, 255);
            C2D_DrawText(&nm, C2D_WithColor, cx + 10, card_y + 60, 0, 0.5f, 0.5f, tc);

            /* HP indicator */
            int hp_disp = 3;
            if (i == CHAR_MAGDALENE) hp_disp = 4;
            else if (i == CHAR_CAIN) hp_disp = 2;
            else if (i == CHAR_JUDAS) hp_disp = 1;
            char hpb[16];
            snprintf(hpb, sizeof(hpb), "HP: %d", hp_disp);
            C2D_Text hpt;
            C2D_TextParse(&hpt, textBuf, hpb);
            C2D_TextOptimize(&hpt);
            C2D_DrawText(&hpt, C2D_WithColor, cx + 10, card_y + 80, 0, 0.4f, 0.4f,
                         C2D_Color32(220, 100, 100, 255));
        } else {
            /* === Locked: hide all character details === */
            /* Greyed-out silhouette area (no tint reveal) */
            C2D_DrawRectSolid(cx + 8, card_y + 8, 0, card_w - 16, 40,
                              C2D_Color32(25, 20, 18, 255));
            /* Anonymous silhouette */
            C2D_DrawCircleSolid(cx + card_w / 2, card_y + 28, 0, 14,
                                C2D_Color32(60, 50, 45, 255));
            /* No eyes, no character preview */

            /* Name placeholder */
            C2D_Text nm;
            C2D_TextParse(&nm, textBuf, "??????");
            C2D_TextOptimize(&nm);
            C2D_DrawText(&nm, C2D_WithColor, cx + 10, card_y + 60, 0, 0.5f, 0.5f,
                         C2D_Color32(120, 100, 90, 255));
            /* HP hidden as well */
            C2D_Text hpt;
            C2D_TextParse(&hpt, textBuf, "HP: ?");
            C2D_TextOptimize(&hpt);
            C2D_DrawText(&hpt, C2D_WithColor, cx + 10, card_y + 80, 0, 0.4f, 0.4f,
                         C2D_Color32(120, 90, 90, 255));

            /* Soft dim overlay so the card reads as "disabled" */
            C2D_DrawRectSolid(cx, card_y, 0, card_w, card_h,
                              C2D_Color32(0, 0, 0, 140));
            /* Lock icon: padlock body */
            float lx = cx + card_w / 2 - 8;
            float ly = card_y + card_h / 2 - 4;
            C2D_DrawRectSolid(lx, ly, 0, 16, 14,
                              C2D_Color32(220, 200, 80, 255));
            /* Shackle */
            C2D_DrawRectSolid(lx + 3, ly - 6, 0, 10, 4,
                              C2D_Color32(220, 200, 80, 255));
            C2D_DrawRectSolid(lx + 3, ly - 6, 0, 2, 8,
                              C2D_Color32(220, 200, 80, 255));
            C2D_DrawRectSolid(lx + 11, ly - 6, 0, 2, 8,
                              C2D_Color32(220, 200, 80, 255));
            /* LOCKED text */
            C2D_Text lk;
            C2D_TextParse(&lk, textBuf, "LOCKED");
            C2D_TextOptimize(&lk);
            C2D_DrawText(&lk, C2D_WithColor, cx + 16, card_y + card_h - 22,
                         0, 0.45f, 0.45f, C2D_Color32(255, 180, 60, 255));
        }
    }

    /* Description of selected character — hide for locked */
    int sel_unlocked = (g_config.unlocked_chars & (1 << g->char_sel)) ? 1 : 0;
    if (sel_unlocked) {
        C2D_Text desc;
        C2D_TextParse(&desc, textBuf, character_blurb((CharacterType)g->char_sel));
        C2D_TextOptimize(&desc);
        C2D_DrawText(&desc, C2D_WithColor, 35, 185, 0, 0.45f, 0.45f,
                     C2D_Color32(220, 200, 160, 255));
    } else {
        C2D_Text desc;
        C2D_TextParse(&desc, textBuf, "Complete the game with another character to unlock.");
        C2D_TextOptimize(&desc);
        C2D_DrawText(&desc, C2D_WithColor, 20, 185, 0, 0.45f, 0.45f,
                     C2D_Color32(160, 130, 90, 255));
    }

    /* Hint */
    C2D_Text hint;
    C2D_TextParse(&hint, textBuf, "D-Pad/Stick: Select   A: Confirm   B: Back");
    C2D_TextOptimize(&hint);
    float ha = 0.5f + sinf(g->menu_timer * 0.1f) * 0.3f;
    C2D_DrawText(&hint, C2D_WithColor, 65, 218, 0, 0.45f, 0.45f,
                 C2D_Color32(150, 130, 110, (int)(ha * 255)));
}

/* ================================================================
 * Render: Stats overlay (bottom screen when paused)
 * ================================================================ */

void render_stats_overlay(Game *g, C2D_TextBuf textBuf) {
    C2D_TextBufClear(textBuf);
    Player *p = &g->player;

    /* Background */
    C2D_DrawRectSolid(0, 0, 0, BOT_SCREEN_WIDTH, BOT_SCREEN_HEIGHT,
                      C2D_Color32(30, 25, 20, 255));

    /* Title */
    C2D_Text title;
    C2D_TextParse(&title, textBuf, "PLAYER STATS");
    C2D_TextOptimize(&title);
    C2D_DrawText(&title, C2D_WithColor, 90, 6, 0, 0.65f, 0.65f,
                 C2D_Color32(255, 230, 180, 255));

    /* Character name */
    char cb[48];
    snprintf(cb, sizeof(cb), "%s   Floor %d", character_name(p->character),
             g->current_floor + 1);
    C2D_Text ct;
    C2D_TextParse(&ct, textBuf, cb);
    C2D_TextOptimize(&ct);
    C2D_DrawText(&ct, C2D_WithColor, 80, 25, 0, 0.5f, 0.5f,
                 character_tint(p->character));

    /* Stats list */
    char buf[64];
    float y = 50;
    float dy = 16;
    u32 lc = C2D_Color32(200, 200, 200, 255);
    u32 vc = C2D_Color32(255, 240, 180, 255);

    #define STAT_LINE(label, fmt, val) do { \
        C2D_Text lt; \
        C2D_TextParse(&lt, textBuf, label); \
        C2D_TextOptimize(&lt); \
        C2D_DrawText(&lt, C2D_WithColor, 18, y, 0, 0.46f, 0.46f, lc); \
        snprintf(buf, sizeof(buf), fmt, val); \
        C2D_Text vt; \
        C2D_TextParse(&vt, textBuf, buf); \
        C2D_TextOptimize(&vt); \
        C2D_DrawText(&vt, C2D_WithColor, 130, y, 0, 0.46f, 0.46f, vc); \
        y += dy; \
    } while (0)

    STAT_LINE("Damage:",    "%.2f", p->stats.damage);
    STAT_LINE("Tears:",     "%.2f", p->stats.fire_rate);
    STAT_LINE("Speed:",     "%.2f", p->stats.speed);
    STAT_LINE("Range:",     "%.0f", p->stats.range);
    STAT_LINE("Max HP:",    "%d",   p->stats.max_hp);
    STAT_LINE("Luck:",      "%.1f", p->stats.luck);
    STAT_LINE("Items:",     "%d",   p->item_count);
    STAT_LINE("Kills:",     "%d",   g->kills);

    /* Play time */
    int seconds = g->play_time_frames / 60;
    int minutes = seconds / 60;
    seconds %= 60;
    snprintf(buf, sizeof(buf), "%02d:%02d", minutes, seconds);
    C2D_Text tl, tv;
    C2D_TextParse(&tl, textBuf, "Time:");
    C2D_TextOptimize(&tl);
    C2D_DrawText(&tl, C2D_WithColor, 18, y, 0, 0.46f, 0.46f, lc);
    C2D_TextParse(&tv, textBuf, buf);
    C2D_TextOptimize(&tv);
    C2D_DrawText(&tv, C2D_WithColor, 130, y, 0, 0.46f, 0.46f, vc);

    #undef STAT_LINE

    /* Bottom hint */
    C2D_Text hint;
    C2D_TextParse(&hint, textBuf, "START or B: Resume");
    C2D_TextOptimize(&hint);
    C2D_DrawText(&hint, C2D_WithColor, 88, 218, 0, 0.45f, 0.45f,
                 C2D_Color32(150, 130, 110, 255));
}

/* ================================================================
 * Render: Pickup message (item name + short description)
 * ================================================================ */

void render_pickup_message(Game *g, C2D_TextBuf textBuf) {
    if (g->pickup_msg_timer <= 0) return;
    /* Fade in then out */
    int t = g->pickup_msg_timer;
    int alpha = 255;
    if (t < 30) alpha = (t * 255) / 30;
    if (t > 150) alpha = ((180 - t) * 255) / 30;
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;

    /* Color-code the banner by pickup type. The collect_item / pickup code
       prefixes pill/card pickups with "Pill:" / "Card:", and item pickups
       use "<name> - <desc>". We use that to pick an accent color and a
       short icon-style tag so the player can tell at a glance what kind
       of pickup it was — this avoids the "everything looks the same"
       feedback issue in the original UI. */
    const char *msg_text = g->pickup_msg_text;
    u32 accent  = C2D_Color32(255, 200, 100, alpha);  /* default: warm gold (item) */
    u32 textcol = C2D_Color32(255, 240, 200, alpha);
    const char *tag = "ITEM";
    u32 tagcol = C2D_Color32(255, 230, 150, alpha);

    if (strncmp(msg_text, "Pill:", 5) == 0) {
        accent  = C2D_Color32(220, 130, 200, alpha);  /* pink / pill capsule */
        textcol = C2D_Color32(255, 220, 240, alpha);
        tag = "PILL";
        tagcol = C2D_Color32(255, 180, 220, alpha);
    } else if (strncmp(msg_text, "Card:", 5) == 0) {
        accent  = C2D_Color32(140, 180, 255, alpha);  /* cool blue / tarot */
        textcol = C2D_Color32(220, 235, 255, alpha);
        tag = "CARD";
        tagcol = C2D_Color32(190, 215, 255, alpha);
    }

    /* Background banner near bottom-center of top screen */
    float bw = 320;
    float bh = 28;
    float bx = (TOP_SCREEN_WIDTH - bw) / 2;
    float by = ROOM_BOTTOM - bh - 4;
    C2D_DrawRectSolid(bx, by, 0, bw, bh, C2D_Color32(40, 30, 20, (alpha * 220) / 255));
    C2D_DrawRectSolid(bx, by, 0, bw, 2, accent);
    C2D_DrawRectSolid(bx, by + bh - 2, 0, bw, 2, accent);
    /* Coloured vertical accent rail on the left — instant visual tell of
       what kind of pickup this is, even before reading the text */
    C2D_DrawRectSolid(bx, by, 0, 4, bh, accent);

    /* Small tag (ITEM / PILL / CARD) */
    C2D_Text tagText;
    C2D_TextParse(&tagText, textBuf, tag);
    C2D_TextOptimize(&tagText);
    C2D_DrawText(&tagText, C2D_WithColor, bx + 10, by + 3, 0, 0.38f, 0.38f, tagcol);

    /* Main message text, shifted right to leave room for the tag */
    C2D_Text msg;
    C2D_TextParse(&msg, textBuf, msg_text);
    C2D_TextOptimize(&msg);
    C2D_DrawText(&msg, C2D_WithColor, bx + 52, by + 6, 0, 0.48f, 0.48f, textcol);
}

/* ================================================================
 * Render: Controls screen
 * ================================================================ */

/* ================================================================
 * Render: Settings screen
 * ================================================================ */
void render_settings(Game *g, C2D_TextBuf textBuf) {
    /* Background */
    C2D_DrawRectSolid(0, 0, 0, TOP_SCREEN_WIDTH, TOP_SCREEN_HEIGHT, COL_MENU_BG);

    /* Title */
    C2D_Text title;
    C2D_TextParse(&title, textBuf, "=== SETTINGS ===");
    C2D_TextOptimize(&title);
    C2D_DrawText(&title, C2D_WithColor, 120, 12, 0, 0.7f, 0.7f, COL_MENU_SEL);

    /* Audio status indicator */
    C2D_Text status_label, status_val;
    C2D_TextParse(&status_label, textBuf, "Audio Status:");
    C2D_TextOptimize(&status_label);
    C2D_DrawText(&status_label, C2D_WithColor, 30, 42, 0, 0.5f, 0.5f,
                 C2D_Color32(180, 180, 180, 255));

    const char *status_str = audio_status_string();
    C2D_TextParse(&status_val, textBuf, status_str);
    C2D_TextOptimize(&status_val);
    u32 status_col;
    if (audio_is_available())
        status_col = C2D_Color32(80, 220, 80, 255);   /* green */
    else
        status_col = C2D_Color32(220, 160, 60, 255);   /* orange */
    C2D_DrawText(&status_val, C2D_WithColor, 175, 42, 0, 0.5f, 0.5f, status_col);

    /* Separator line */
    C2D_DrawRectSolid(30, 62, 0, 340, 1, C2D_Color32(100, 80, 60, 180));

    /* Settings items */
    const char *labels[SETTINGS_COUNT] = {
        "Audio Enabled:",
        "SFX Volume:",
        "Music Volume:"
    };

    for (int i = 0; i < SETTINGS_COUNT; i++) {
        float yPos = 75 + i * 32;
        int selected = (i == g->settings_sel);

        /* Selection highlight */
        if (selected) {
            float sel_pulse = sinf(g->menu_timer * 0.15f);
            int alpha = (int)(80 + sel_pulse * 30);
            C2D_DrawRectSolid(25, yPos - 3, 0, 350, 26, C2D_Color32(100, 70, 40, alpha));
        }

        /* Label */
        C2D_Text lbl;
        C2D_TextParse(&lbl, textBuf, labels[i]);
        C2D_TextOptimize(&lbl);
        u32 lbl_col = selected ? C2D_Color32(255, 240, 200, 255)
                               : C2D_Color32(180, 160, 130, 255);
        C2D_DrawText(&lbl, C2D_WithColor, 40, yPos, 0, 0.5f, 0.5f, lbl_col);

        /* Value */
        char val_buf[32];
        if (i == 0) {
            snprintf(val_buf, sizeof(val_buf), "< %s >",
                     g_config.audio_enabled ? "ON" : "OFF");
        } else if (i == 1) {
            snprintf(val_buf, sizeof(val_buf), "< %d%% >",
                     (int)(g_config.sfx_volume * 100.0f + 0.5f));
        } else {
            snprintf(val_buf, sizeof(val_buf), "< %d%% >",
                     (int)(g_config.music_volume * 100.0f + 0.5f));
        }
        C2D_Text val_txt;
        C2D_TextParse(&val_txt, textBuf, val_buf);
        C2D_TextOptimize(&val_txt);
        u32 val_col = selected ? C2D_Color32(255, 220, 150, 255)
                               : C2D_Color32(200, 180, 140, 255);
        C2D_DrawText(&val_txt, C2D_WithColor, 250, yPos, 0, 0.5f, 0.5f, val_col);
    }

    /* Restart notice if audio toggle changed */
    if (g->settings_changed) {
        C2D_Text notice;
        C2D_TextParse(&notice, textBuf, "* Audio toggle takes effect on next launch *");
        C2D_TextOptimize(&notice);
        float blink = sinf(g->menu_timer * 0.1f);
        int alpha = (int)(150 + blink * 80);
        C2D_DrawText(&notice, C2D_WithColor, 50, 175, 0, 0.42f, 0.42f,
                     C2D_Color32(255, 200, 100, alpha));
    }

    /* Hint at bottom (audio backend implementation detail no longer shown) */
    C2D_Text hint;
    C2D_TextParse(&hint, textBuf, "D-Pad: Navigate   A/L/R: Change   B: Save & Back");
    C2D_TextOptimize(&hint);
    C2D_DrawText(&hint, C2D_WithColor, 45, 228, 0, 0.38f, 0.38f,
                 C2D_Color32(120, 100, 80, 200));
}

void render_controls(C2D_TextBuf textBuf) {
    C2D_DrawRectSolid(0, 0, 0, TOP_SCREEN_WIDTH, TOP_SCREEN_HEIGHT, COL_MENU_BG);

    C2D_Text lines[12];
    const char *text[] = {
        "=== CONTROLS ===",
        "",
        "D-Pad / Circle Pad : Move",
        "X / B / Y / A : Shoot U / D / L / R",
        "SELECT : Place Bomb",
        "L : Use Pill   R : Use Card",
        "START : Pause / Stats",
        "",
        "Clear rooms to open doors!",
        "Champions glow with auras!",
        "Use trapdoor after boss to descend!",
        "Press A/B to go back"
    };
    float ypos[] = { 12, 0, 38, 58, 78, 98, 118, 0, 145, 162, 179, 200 };

    for (int i = 0; i < 12; i++) {
        if (strlen(text[i]) == 0) continue;
        C2D_TextParse(&lines[i], textBuf, text[i]);
        C2D_TextOptimize(&lines[i]);
        float scale = (i == 0) ? 0.7f : 0.42f;
        u32 col = (i == 0) ? COL_MENU_SEL : (i == 11 ? C2D_Color32(120, 120, 120, 255) : COL_TEXT);
        float xoff = (i == 0) ? 100 : 40;
        C2D_DrawText(&lines[i], C2D_WithColor, xoff, ypos[i], 0, scale, scale, col);
    }
}

/* ================================================================
 * Render: Unlocks/Achievements Screen (Phase 2)
 * ================================================================ */

void render_unlocks_screen(Game *g, C2D_TextBuf textBuf) {
    C2D_DrawRectSolid(0, 0, 0, TOP_SCREEN_WIDTH, TOP_SCREEN_HEIGHT, COL_MENU_BG);

    /* Title */
    C2D_Text title;
    C2D_TextParse(&title, textBuf, "=== UNLOCKS & STATS ===");
    C2D_TextOptimize(&title);
    C2D_DrawText(&title, C2D_WithColor, 80, 8, 0, 0.62f, 0.62f, COL_MENU_SEL);

    /* Build all the lines */
    char buf[16][64];
    const char *lines_text[16];
    int line_count = 0;

    /* Header section: characters */
    snprintf(buf[line_count], 64, "-- CHARACTERS --");
    lines_text[line_count] = buf[line_count]; line_count++;

    /* Character unlocks */
    const char *chars[] = { "ISAAC", "MAGDALENE", "CAIN", "JUDAS" };
    for (int i = 0; i < 4; i++) {
        int unlocked = (g_config.unlocked_chars & (1 << i)) != 0;
        int completed = (g_config.characters_completed & (1 << i)) != 0;
        if (unlocked) {
            snprintf(buf[line_count], 64, "%s: %s",
                    chars[i],
                    completed ? "COMPLETED" : "UNLOCKED");
        } else {
            snprintf(buf[line_count], 64, "???: LOCKED");
        }
        lines_text[line_count] = buf[line_count]; line_count++;
    }

    /* Stats */
    snprintf(buf[line_count], 64, "-- STATS --");
    lines_text[line_count] = buf[line_count]; line_count++;

    snprintf(buf[line_count], 64, "Total Wins: %d", g_config.total_wins);
    lines_text[line_count] = buf[line_count]; line_count++;
    snprintf(buf[line_count], 64, "Runs Started: %d", g_config.total_runs_started);
    lines_text[line_count] = buf[line_count]; line_count++;
    snprintf(buf[line_count], 64, "Deepest Floor: %d", g_config.floors_reached + 1);
    lines_text[line_count] = buf[line_count]; line_count++;

    /* Boss progress: count bits */
    int boss_count = 0;
    int boss_mask = g_config.bosses_defeated;
    for (int b = 0; b < 16; b++) if (boss_mask & (1 << b)) boss_count++;
    snprintf(buf[line_count], 64, "Bosses Defeated: %d / 11", boss_count);
    lines_text[line_count] = buf[line_count]; line_count++;

    /* Boss list */
    snprintf(buf[line_count], 64, "-- BOSSES --");
    lines_text[line_count] = buf[line_count]; line_count++;

    const char *boss_names[] = {
        "Duke of Flies", "Monstro", "Gemini", "Larry Jr.", "Famine",
        "Peep", "Gurdy", "Pin", "The Haunt", "Widow", "Mega Satan"
    };
    /* Pack two columns - we'll show ones we have seen vs ???  */
    for (int b = 0; b < 11 && line_count < 16; b++) {
        int defeated = (g_config.bosses_defeated & (1 << b)) != 0;
        snprintf(buf[line_count], 64, "  %c %s",
                defeated ? 'X' : '-',
                defeated ? boss_names[b] : "???");
        lines_text[line_count] = buf[line_count]; line_count++;
    }

    /* Scroll bounds */
    int max_scroll = line_count - 11;
    if (max_scroll < 0) max_scroll = 0;
    if (g->unlocks_scroll > max_scroll) g->unlocks_scroll = max_scroll;

    /* Render visible lines */
    C2D_Text txt[16];
    float y = 32.0f;
    int start = g->unlocks_scroll;
    int end = start + 11;
    if (end > line_count) end = line_count;
    for (int i = start; i < end; i++) {
        C2D_TextParse(&txt[i], textBuf, lines_text[i]);
        C2D_TextOptimize(&txt[i]);
        u32 col = COL_TEXT;
        if (lines_text[i][0] == '-' && lines_text[i][1] == '-') col = COL_MENU_SEL;
        C2D_DrawText(&txt[i], C2D_WithColor, 30, y, 0, 0.42f, 0.42f, col);
        y += 16;
    }

    /* Footer hint */
    C2D_Text hint;
    C2D_TextParse(&hint, textBuf, "Up/Down: Scroll   B: Back");
    C2D_TextOptimize(&hint);
    C2D_DrawText(&hint, C2D_WithColor, 60, 220, 0, 0.38f, 0.38f,
                C2D_Color32(160, 160, 160, 255));
}

/* ================================================================
 * Render: HUD
 * ================================================================ */

void render_hud(Game *g, C2D_TextBuf textBuf) {
    /* HUD background only covers the area above the wall (y=0 to WALL_THICKNESS)
     * so it does not obscure the top door which sits at y=WALL_THICKNESS */
    C2D_DrawRectSolid(0, 0, 0, TOP_SCREEN_WIDTH, WALL_THICKNESS, COL_HUD_BG);

    /* Hearts – compact layout fitting within 16px tall HUD strip */
    int maxHearts = g->player.stats.max_hp / 2;
    if (maxHearts > 8) maxHearts = 8;
    for (int i = 0; i < maxHearts; i++) {
        float hx = 12 + i * 15;
        float hy = 8;
        int hpForThisHeart = (g->player.hp - i * 2);

        if (g_sprites_loaded) {
            float hs = 0.75f; /* slightly smaller hearts to fit HUD */
            if (hpForThisHeart >= 2) {
                spr_draw(sheet_ui_items, ui_items_atlas_heart_red_full_idx, hx, hy, hs, hs);
            } else if (hpForThisHeart == 1) {
                spr_draw(sheet_ui_items, ui_items_atlas_heart_red_half_idx, hx, hy, hs, hs);
            } else {
                spr_draw(sheet_ui_items, ui_items_atlas_heart_red_empty_idx, hx, hy, hs, hs);
            }
        } else {
            if (hpForThisHeart >= 2) {
                draw_heart(hx, hy, 12, COL_HEART_FULL);
            } else if (hpForThisHeart == 1) {
                draw_heart(hx, hy, 12, COL_HEART_EMPTY);
                draw_heart(hx, hy, 8, COL_HEART_FULL);
            } else {
                draw_heart(hx, hy, 12, COL_HEART_EMPTY);
            }
        }
    }

    /* Consumable counts (bomb/key/coin) in HUD - right of hearts */
    {
        float cx = 12 + maxHearts * 15 + 8;
        float cy = 8;
        float iconSc = 0.55f;
        if (g_sprites_loaded) {
            spr_draw(sheet_ui_items, ui_items_atlas_item_bomb_idx, cx, cy, iconSc, iconSc);
        } else {
            C2D_DrawCircleSolid(cx, cy, 0, 4, C2D_Color32(80, 80, 80, 255));
        }
        C2D_Text bt;
        char bb[8];
        snprintf(bb, sizeof(bb), "x%d", g->player.bombs);
        C2D_TextParse(&bt, textBuf, bb);
        C2D_TextOptimize(&bt);
        C2D_DrawText(&bt, C2D_WithColor, cx + 7, cy - 4, 0, 0.35f, 0.35f,
                     C2D_Color32(200, 200, 200, 255));

        cx += 30;
        if (g_sprites_loaded) {
            spr_draw(sheet_ui_items, ui_items_atlas_item_key_idx, cx, cy, iconSc, iconSc);
        } else {
            C2D_DrawRectSolid(cx - 3, cy - 5, 0, 6, 10, C2D_Color32(255, 215, 0, 255));
        }
        C2D_Text kt;
        char kb[8];
        snprintf(kb, sizeof(kb), "x%d", g->player.keys);
        C2D_TextParse(&kt, textBuf, kb);
        C2D_TextOptimize(&kt);
        C2D_DrawText(&kt, C2D_WithColor, cx + 7, cy - 4, 0, 0.35f, 0.35f,
                     C2D_Color32(200, 200, 200, 255));

        cx += 30;
        if (g_sprites_loaded) {
            spr_draw(sheet_ui_items, ui_items_atlas_item_coin_idx, cx, cy, iconSc, iconSc);
        } else {
            C2D_DrawCircleSolid(cx, cy, 0, 4, C2D_Color32(255, 215, 0, 255));
        }
        C2D_Text ct;
        char cb[8];
        snprintf(cb, sizeof(cb), "x%d", g->player.coins);
        C2D_TextParse(&ct, textBuf, cb);
        C2D_TextOptimize(&ct);
        C2D_DrawText(&ct, C2D_WithColor, cx + 7, cy - 4, 0, 0.35f, 0.35f,
                     C2D_Color32(200, 200, 200, 255));
    }

    /* Floor name and room type */
    const FloorInfo *fi = get_floor_info(g->current_floor);
    Room *r = current_room(g);

    C2D_Text floorText, roomText;
    char floorBuf[48], roomBuf[32];
    const char *roomNames[] = { "???", "Start", "Room", "Treasure", "BOSS", "Exit",
                                "Shop", "Secret", "Curse" };

    /* Floor display: show loop count in infinite mode */
    if (g->game_mode == MODE_INFINITE && g->infinite_loop > 0) {
        int display_floor = g->infinite_loop * MAX_FLOORS + g->current_floor + 1;
        snprintf(floorBuf, sizeof(floorBuf), "F%d %s", display_floor, fi->name);
    } else {
        snprintf(floorBuf, sizeof(floorBuf), "%s", fi->name);
    }
    snprintf(roomBuf, sizeof(roomBuf), "%s",
             r->type < 9 ? roomNames[r->type] : "???");

    C2D_TextParse(&floorText, textBuf, floorBuf);
    C2D_TextOptimize(&floorText);
    C2D_DrawText(&floorText, C2D_WithColor, 155, 3, 0, 0.4f, 0.4f,
                 C2D_Color32(200, 200, 200, 255));

    u32 roomCol = (r->type == ROOM_BOSS) ? COL_HEART_FULL :
                  (r->type == ROOM_TREASURE) ? COL_TREASURE :
                  (r->type == ROOM_SHOP) ? C2D_Color32(100, 200, 255, 255) :
                  (r->type == ROOM_SECRET) ? C2D_Color32(180, 180, 180, 255) :
                  (r->type == ROOM_CURSE) ? C2D_Color32(180, 60, 180, 255) : COL_TEXT;
    C2D_TextParse(&roomText, textBuf, roomBuf);
    C2D_TextOptimize(&roomText);
    C2D_DrawText(&roomText, C2D_WithColor, 230, 3, 0, 0.4f, 0.4f, roomCol);

    /* Difficulty / Mode indicator (top-right corner) */
    {
        const char *diff_labels[] = { "EASY", "NORM", "HARD" };
        u32 diff_cols[] = {
            C2D_Color32(100, 220, 100, 200),  /* green */
            C2D_Color32(200, 200, 100, 200),  /* yellow */
            C2D_Color32(255, 80, 80, 200)     /* red */
        };
        char mode_buf[16];
        if (g->game_mode == MODE_INFINITE) {
            snprintf(mode_buf, sizeof(mode_buf), "%s INF",
                     diff_labels[g->difficulty < DIFF_COUNT ? g->difficulty : 1]);
        } else {
            snprintf(mode_buf, sizeof(mode_buf), "%s",
                     diff_labels[g->difficulty < DIFF_COUNT ? g->difficulty : 1]);
        }
        C2D_Text modeText;
        C2D_TextParse(&modeText, textBuf, mode_buf);
        C2D_TextOptimize(&modeText);
        u32 mc = diff_cols[g->difficulty < DIFF_COUNT ? g->difficulty : 1];
        C2D_DrawText(&modeText, C2D_WithColor, TOP_SCREEN_WIDTH - 55, 14, 0,
                     0.3f, 0.3f, mc);
    }

    /* Item count */
    if (g->player.item_count > 0) {
        C2D_Text itemText;
        char itemBuf[24];
        snprintf(itemBuf, sizeof(itemBuf), "Items:%d", g->player.item_count);
        C2D_TextParse(&itemText, textBuf, itemBuf);
        C2D_TextOptimize(&itemText);
        C2D_DrawText(&itemText, C2D_WithColor, 300, 3, 0, 0.35f, 0.35f,
                     C2D_Color32(180, 180, 255, 255));
    }

    /* Active ability indicators – fit within 16px HUD */
    float indX = 360;
    float indY = 8;
    if (g->player.stats.flags & ITEM_FLAG_HOMING) {
        C2D_DrawCircleSolid(indX, indY, 0, 3, COL_HOMING_TEAR);
        indX += 8;
    }
    if (g->player.stats.flags & ITEM_FLAG_PIERCING) {
        C2D_DrawRectSolid(indX - 3, indY - 1, 0, 6, 2, COL_TEAR);
        indX += 8;
    }
    if (g->player.stats.flags & ITEM_FLAG_SPECTRAL) {
        C2D_DrawCircleSolid(indX, indY, 0, 3, COL_SPECTRAL_TEAR);
        indX += 8;
    }
    if (g->player.stats.flags & ITEM_FLAG_TRIPLE) {
        C2D_DrawCircleSolid(indX - 2, indY, 0, 2, COL_TEAR);
        C2D_DrawCircleSolid(indX, indY - 2, 0, 2, COL_TEAR);
        C2D_DrawCircleSolid(indX + 2, indY, 0, 2, COL_TEAR);
    }

    /* ============================================
     * Top-screen Minimap (upper-right overlay)
     * ============================================ */
    {
        Dungeon *d = &g->dungeon;

        /* Minimap dimensions - compact to fit in upper-right corner */
        float mCellW = 11.0f;
        float mCellH = 8.0f;
        float mPad   = 3.0f;
        float mW = DUNGEON_W * mCellW + mPad * 2;
        float mH = DUNGEON_H * mCellH + mPad * 2;
        float mX = TOP_SCREEN_WIDTH - mW - 2;  /* top-right corner */
        float mY = WALL_THICKNESS + 2;          /* just below HUD strip */

        /* Semi-transparent background panel */
        C2D_DrawRectSolid(mX, mY, 0, mW, mH, C2D_Color32(0, 0, 0, 140));
        /* Subtle border */
        C2D_DrawRectSolid(mX, mY, 0, mW, 1, C2D_Color32(80, 80, 80, 180));
        C2D_DrawRectSolid(mX, mY + mH - 1, 0, mW, 1, C2D_Color32(80, 80, 80, 180));
        C2D_DrawRectSolid(mX, mY, 0, 1, mH, C2D_Color32(80, 80, 80, 180));
        C2D_DrawRectSolid(mX + mW - 1, mY, 0, 1, mH, C2D_Color32(80, 80, 80, 180));

        for (int ry = 0; ry < DUNGEON_H; ry++) {
            for (int rx = 0; rx < DUNGEON_W; rx++) {
                Room *rm = &d->rooms[ry][rx];
                if (rm->type == ROOM_NONE) continue;

                float cx = mX + mPad + rx * mCellW;
                float cy = mY + mPad + ry * mCellH;
                int isCurrent = (rx == d->cur_x && ry == d->cur_y);

                if (!rm->visited) {
                    /* Show unvisited rooms as dim outlines only if adjacent to a
                     * visited room (so player knows doors lead somewhere) */
                    int adjVisible = 0;
                    if (ry > 0 && d->rooms[ry-1][rx].visited && d->rooms[ry-1][rx].doors[1]) adjVisible = 1;
                    if (ry < DUNGEON_H-1 && d->rooms[ry+1][rx].visited && d->rooms[ry+1][rx].doors[0]) adjVisible = 1;
                    if (rx > 0 && d->rooms[ry][rx-1].visited && d->rooms[ry][rx-1].doors[3]) adjVisible = 1;
                    if (rx < DUNGEON_W-1 && d->rooms[ry][rx+1].visited && d->rooms[ry][rx+1].doors[2]) adjVisible = 1;

                    /* Secret rooms only show if revealed */
                    if (rm->type == ROOM_SECRET && !rm->secret_revealed) adjVisible = 0;

                    if (adjVisible) {
                        /* Dim question-mark style outline */
                        C2D_DrawRectSolid(cx + 1, cy + 1, 0, mCellW - 2, mCellH - 2,
                                         C2D_Color32(50, 50, 50, 120));
                    }
                    continue;
                }

                /* Visited room - color by type */
                u32 rmCol;
                switch (rm->type) {
                    case ROOM_START:    rmCol = C2D_Color32(80, 160, 80, 200); break;
                    case ROOM_BOSS:     rmCol = C2D_Color32(200, 50, 50, 220); break;
                    case ROOM_TREASURE: rmCol = C2D_Color32(220, 180, 30, 220); break;
                    case ROOM_SHOP:     rmCol = C2D_Color32(80, 150, 220, 220); break;
                    case ROOM_SECRET:   rmCol = C2D_Color32(140, 140, 140, 200); break;
                    case ROOM_CURSE:    rmCol = C2D_Color32(160, 50, 160, 220); break;
                    default:
                        rmCol = rm->cleared
                              ? C2D_Color32(90, 90, 90, 180)
                              : C2D_Color32(110, 100, 90, 200);
                        break;
                }

                C2D_DrawRectSolid(cx + 1, cy + 1, 0, mCellW - 2, mCellH - 2, rmCol);

                /* Door connections - thin lines between rooms */
                u32 doorCol = C2D_Color32(120, 120, 120, 160);
                if (rm->doors[0] && ry > 0 && d->rooms[ry-1][rx].visited)
                    C2D_DrawRectSolid(cx + mCellW/2 - 1, cy, 0, 2, 1, doorCol);
                if (rm->doors[1] && ry < DUNGEON_H-1 && d->rooms[ry+1][rx].visited)
                    C2D_DrawRectSolid(cx + mCellW/2 - 1, cy + mCellH - 1, 0, 2, 1, doorCol);
                if (rm->doors[2] && rx > 0 && d->rooms[ry][rx-1].visited)
                    C2D_DrawRectSolid(cx, cy + mCellH/2 - 1, 0, 1, 2, doorCol);
                if (rm->doors[3] && rx < DUNGEON_W-1 && d->rooms[ry][rx+1].visited)
                    C2D_DrawRectSolid(cx + mCellW - 1, cy + mCellH/2 - 1, 0, 1, 2, doorCol);

                /* Room type icons (tiny 1-3px symbols inside cells) */
                float icx = cx + mCellW / 2;
                float icy = cy + mCellH / 2;

                if (rm->type == ROOM_BOSS && !isCurrent) {
                    /* Skull icon: tiny red dot */
                    C2D_DrawRectSolid(icx - 1, icy - 1, 0, 3, 2, C2D_Color32(255, 200, 200, 255));
                } else if (rm->type == ROOM_TREASURE && !isCurrent) {
                    /* Star/sparkle */
                    C2D_DrawRectSolid(icx, icy - 1, 0, 1, 3, C2D_Color32(255, 255, 180, 255));
                    C2D_DrawRectSolid(icx - 1, icy, 0, 3, 1, C2D_Color32(255, 255, 180, 255));
                } else if (rm->type == ROOM_SHOP && !isCurrent) {
                    /* $ coin icon */
                    C2D_DrawRectSolid(icx, icy - 1, 0, 1, 3, C2D_Color32(255, 255, 200, 255));
                } else if (rm->type == ROOM_SECRET && !isCurrent) {
                    /* ? mark */
                    C2D_DrawRectSolid(icx, icy - 1, 0, 1, 2, C2D_Color32(220, 220, 220, 255));
                    C2D_DrawRectSolid(icx, icy + 2, 0, 1, 1, C2D_Color32(220, 220, 220, 255));
                } else if (rm->type == ROOM_CURSE && !isCurrent) {
                    /* X mark */
                    C2D_DrawRectSolid(icx - 1, icy - 1, 0, 1, 1, C2D_Color32(255, 200, 255, 255));
                    C2D_DrawRectSolid(icx + 1, icy - 1, 0, 1, 1, C2D_Color32(255, 200, 255, 255));
                    C2D_DrawRectSolid(icx, icy, 0, 1, 1, C2D_Color32(255, 200, 255, 255));
                    C2D_DrawRectSolid(icx - 1, icy + 1, 0, 1, 1, C2D_Color32(255, 200, 255, 255));
                    C2D_DrawRectSolid(icx + 1, icy + 1, 0, 1, 1, C2D_Color32(255, 200, 255, 255));
                } else if (rm->cleared && rm->type != ROOM_START && !isCurrent) {
                    /* Small green checkmark dot for cleared normal rooms */
                    C2D_DrawRectSolid(icx, icy, 0, 1, 1, C2D_Color32(100, 255, 100, 200));
                }

                /* Current room marker - pulsing white outline */
                if (isCurrent) {
                    float pulse = sinf((float)g->frame * 0.15f) * 0.4f + 0.6f;
                    u8 a = (u8)(pulse * 255);
                    u32 markerCol = C2D_Color32(255, 255, 255, a);
                    /* Draw outline */
                    C2D_DrawRectSolid(cx, cy, 0, mCellW, 1, markerCol);
                    C2D_DrawRectSolid(cx, cy + mCellH - 1, 0, mCellW, 1, markerCol);
                    C2D_DrawRectSolid(cx, cy, 0, 1, mCellH, markerCol);
                    C2D_DrawRectSolid(cx + mCellW - 1, cy, 0, 1, mCellH, markerCol);
                    /* Bright center dot as player position */
                    C2D_DrawRectSolid(icx, icy, 0, 1, 1, C2D_Color32(255, 255, 255, 255));
                }
            }
        }
    }

    /* ============================================
     * Top-screen Boss Health Bar (prominent, centered)
     * ============================================ */
    if (g->boss_active || g->boss_intro_timer > 0) {
        Room *br = current_room(g);
        Enemy *boss = NULL;
        for (int bi = 0; bi < br->enemy_count; bi++) {
            if (br->enemies[bi].active && is_boss_type(br->enemies[bi].type)) {
                boss = &br->enemies[bi];
                break;
            }
        }
        if (boss) {
            float barX = TOP_SCREEN_WIDTH / 2.0f;
            float barY = TOP_SCREEN_HEIGHT - 14;
            float barW = 160.0f;
            float barH = 8.0f;
            float hpPct = (float)boss->hp / (float)boss->max_hp;
            if (hpPct < 0) hpPct = 0;
            if (hpPct > 1) hpPct = 1;

            /* Bar background (dark) */
            C2D_DrawRectSolid(barX - barW / 2 - 1, barY - barH / 2 - 1, 0,
                              barW + 2, barH + 2, C2D_Color32(20, 20, 20, 220));

            /* Bar border */
            C2D_DrawRectSolid(barX - barW / 2, barY - barH / 2, 0,
                              barW, barH, C2D_Color32(60, 60, 60, 200));

            /* HP fill with color gradient */
            u32 hpCol;
            if (hpPct > 0.6f)      hpCol = C2D_Color32(220, 40, 40, 255);
            else if (hpPct > 0.3f) hpCol = C2D_Color32(255, 140, 40, 255);
            else                    hpCol = C2D_Color32(255, 50, 50, 255);

            /* Pulsing effect when low */
            float pulse = 1.0f;
            if (hpPct < 0.25f) {
                pulse = 0.8f + sinf(g->frame * 0.2f) * 0.2f;
            }

            C2D_DrawRectSolid(barX - barW / 2, barY - barH / 2, 0,
                              barW * hpPct * pulse, barH, hpCol);

            /* Highlight on top of bar */
            C2D_DrawRectSolid(barX - barW / 2, barY - barH / 2, 0,
                              barW * hpPct * pulse, 2,
                              C2D_Color32(255, 255, 255, 60));

            /* Boss name text above bar */
            if (g->boss_name) {
                C2D_Text bossText;
                C2D_TextParse(&bossText, textBuf, g->boss_name);
                C2D_TextOptimize(&bossText);
                float tw = bossText.width * 0.4f;
                C2D_DrawText(&bossText, C2D_WithColor,
                            barX - tw / 2, barY - barH / 2 - 12, 0, 0.4f, 0.4f,
                            C2D_Color32(255, 220, 220, 255));
            }
        }
    }

    /* Boss intro overlay - dramatic name reveal */
    if (g->boss_intro_timer > 0) {
        float alpha = (float)g->boss_intro_timer / BOSS_INTRO_FRAMES;
        if (alpha > 1.0f) alpha = 1.0f;

        /* Darken screen edges */
        u32 overlayCol = C2D_Color32(0, 0, 0, (u8)(alpha * 120));
        C2D_DrawRectSolid(0, 0, 0, TOP_SCREEN_WIDTH, 40, overlayCol);
        C2D_DrawRectSolid(0, TOP_SCREEN_HEIGHT - 40, 0, TOP_SCREEN_WIDTH, 40, overlayCol);

        /* Boss name - large centered text with dramatic fade */
        if (g->boss_name && g->boss_intro_timer > BOSS_INTRO_FRAMES / 3) {
            float nameAlpha = (float)(g->boss_intro_timer - BOSS_INTRO_FRAMES / 3) /
                              (BOSS_INTRO_FRAMES * 2.0f / 3.0f);
            if (nameAlpha > 1.0f) nameAlpha = 1.0f;
            u8 a = (u8)(nameAlpha * 255);

            C2D_Text introText;
            C2D_TextParse(&introText, textBuf, g->boss_name);
            C2D_TextOptimize(&introText);
            float tw = introText.width * 0.7f;

            /* Shadow */
            C2D_DrawText(&introText, C2D_WithColor,
                        TOP_SCREEN_WIDTH / 2.0f - tw / 2 + 2, 98, 0, 0.7f, 0.7f,
                        C2D_Color32(0, 0, 0, a));
            /* Main text */
            C2D_DrawText(&introText, C2D_WithColor,
                        TOP_SCREEN_WIDTH / 2.0f - tw / 2, 96, 0, 0.7f, 0.7f,
                        C2D_Color32(255, 60, 60, a));
        }
    }

    /* Boss death explosion effect */
    if (g->boss_death_anim > 0) {
        float t = (float)(60 - g->boss_death_anim) / 60.0f;
        int numExplosions = (int)(t * 8);
        for (int ex = 0; ex < numExplosions; ex++) {
            /* Deterministic pseudo-random positions based on frame */
            float ex_x = TOP_SCREEN_WIDTH / 2.0f + sinf(ex * 2.7f + t * 5.0f) * 80.0f;
            float ex_y = TOP_SCREEN_HEIGHT / 2.0f + cosf(ex * 3.1f + t * 4.0f) * 50.0f;
            float radius = (1.0f - t) * 12.0f + 4.0f;
            u8 alpha = (u8)((1.0f - t) * 200);
            C2D_DrawCircleSolid(ex_x, ex_y, 0, radius,
                               C2D_Color32(255, 200, 50, alpha));
            C2D_DrawCircleSolid(ex_x, ex_y, 0, radius * 0.6f,
                               C2D_Color32(255, 255, 200, alpha));
        }
    }
}

/* ================================================================
 * Render: Room
 * ================================================================ */

static void render_room(Game *g) {
    Room *r = current_room(g);
    int fl = g->current_floor;

    if (g_sprites_loaded) {
        /* === SPRITE-BASED ROOM RENDERING === */

        /* Floor tiles - pick variant based on room type / floor */
        int floorIdx = environment_atlas_env_floor_clean_idx;
        if (r->type == ROOM_BOSS) floorIdx = environment_atlas_env_floor_bloody_idx;
        else if (fl >= 2) floorIdx = environment_atlas_env_floor_cracked_idx;

        /* Tile the floor with 32x32 sprites */
        for (float ty = ROOM_TOP; ty < ROOM_BOTTOM; ty += 32) {
            for (float tx = ROOM_LEFT; tx < ROOM_RIGHT; tx += 32) {
                spr_draw_at(sheet_environment, floorIdx, tx, ty, 1.0f, 1.0f);
            }
        }

        /* Walls - continuous directional wall tiles (64x24 horiz, 24x64 vert) */
        for (float tx = WALL_THICKNESS; tx < TOP_SCREEN_WIDTH - WALL_THICKNESS; tx += 64) {
            spr_draw_at(sheet_environment, environment_atlas_env_wall_top_idx,
                        tx, ROOM_TOP - WALL_THICKNESS, 1.0f, 1.0f);
        }
        for (float tx = WALL_THICKNESS; tx < TOP_SCREEN_WIDTH - WALL_THICKNESS; tx += 64) {
            spr_draw_at(sheet_environment, environment_atlas_env_wall_bottom_idx,
                        tx, ROOM_BOTTOM, 1.0f, 1.0f);
        }
        for (float ty = ROOM_TOP; ty < ROOM_BOTTOM; ty += 64) {
            spr_draw_at(sheet_environment, environment_atlas_env_wall_left_idx,
                        0, ty, 1.0f, 1.0f);
        }
        for (float ty = ROOM_TOP; ty < ROOM_BOTTOM; ty += 64) {
            spr_draw_at(sheet_environment, environment_atlas_env_wall_right_idx,
                        ROOM_RIGHT, ty, 1.0f, 1.0f);
        }

        /* Corner tiles (24x24) */
        spr_draw_at(sheet_environment, environment_atlas_env_corner_tl_idx,
                    0, ROOM_TOP - WALL_THICKNESS, 1.0f, 1.0f);
        spr_draw_at(sheet_environment, environment_atlas_env_corner_tr_idx,
                    ROOM_RIGHT, ROOM_TOP - WALL_THICKNESS, 1.0f, 1.0f);
        spr_draw_at(sheet_environment, environment_atlas_env_corner_bl_idx,
                    0, ROOM_BOTTOM, 1.0f, 1.0f);
        spr_draw_at(sheet_environment, environment_atlas_env_corner_br_idx,
                    ROOM_RIGHT, ROOM_BOTTOM, 1.0f, 1.0f);

        /* ── Doors ── sprite-based rendering with full type variants ── */
        float midX = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
        float midY = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
        Dungeon *dd = &g->dungeon;
        int rx = dd->cur_x, ry = dd->cur_y;

        /* Door sprite indices per direction:
         * [0]=normal/open, [1]=treasure/locked, [2]=boss, [3]=closed,
         * [4]=curse, [5]=shop */
        int door_top_spr[] = {
            environment_atlas_env_door_top_idx,
            environment_atlas_env_door_treasure_top_idx,
            environment_atlas_env_door_boss_top_idx,
            environment_atlas_env_door_closed_top_idx,
            environment_atlas_env_door_curse_top_idx,
            environment_atlas_env_door_shop_top_idx
        };
        int door_bot_spr[] = {
            environment_atlas_env_door_bottom_idx,
            environment_atlas_env_door_treasure_bottom_idx,
            environment_atlas_env_door_boss_bottom_idx,
            environment_atlas_env_door_closed_bottom_idx,
            environment_atlas_env_door_curse_bottom_idx,
            environment_atlas_env_door_shop_bottom_idx
        };
        int door_left_spr[] = {
            environment_atlas_env_door_left_idx,
            environment_atlas_env_door_treasure_left_idx,
            environment_atlas_env_door_boss_left_idx,
            environment_atlas_env_door_closed_left_idx,
            environment_atlas_env_door_curse_left_idx,
            environment_atlas_env_door_shop_left_idx
        };
        int door_right_spr[] = {
            environment_atlas_env_door_right_idx,
            environment_atlas_env_door_treasure_right_idx,
            environment_atlas_env_door_boss_right_idx,
            environment_atlas_env_door_closed_right_idx,
            environment_atlas_env_door_curse_right_idx,
            environment_atlas_env_door_shop_right_idx
        };
        int *door_spr_arr[] = {door_top_spr, door_bot_spr, door_left_spr, door_right_spr};

        /* Door draw positions per direction */
        float door_pos[4][2] = {
            {midX - DOOR_WIDTH / 2, ROOM_TOP - WALL_THICKNESS},
            {midX - DOOR_WIDTH / 2, ROOM_BOTTOM},
            {0, midY - DOOR_WIDTH / 2},
            {ROOM_RIGHT, midY - DOOR_WIDTH / 2}
        };

        int door_nb_dx[] = {0, 0, -1, 1};
        int door_nb_dy[] = {-1, 1, 0, 0};

        for (int di = 0; di < 4; di++) {
            if (!r->doors[di]) continue;

            /* Determine which sprite variant to use */
            int nnx = rx + door_nb_dx[di], nny = ry + door_nb_dy[di];
            RoomType neighbor_type = ROOM_NONE;
            if (nnx >= 0 && nnx < DUNGEON_W && nny >= 0 && nny < DUNGEON_H)
                neighbor_type = dd->rooms[nny][nnx].type;

            int spr_variant;
            if (!r->cleared && !r->door_locked[di] && r->door_type[di] == 0) {
                /* Normal door, room not cleared => closed/barred */
                spr_variant = 3; /* closed */
            } else if (r->door_locked[di] || neighbor_type == ROOM_TREASURE) {
                spr_variant = 1; /* treasure/locked (gold with lock) */
            } else if (neighbor_type == ROOM_BOSS) {
                spr_variant = 2; /* boss (red with skull) */
            } else if (neighbor_type == ROOM_CURSE || r->door_type[di] == 4) {
                spr_variant = 4; /* curse (purple with spikes) */
            } else if (neighbor_type == ROOM_SHOP) {
                spr_variant = 5; /* shop (blue with coin) */
            } else {
                spr_variant = 0; /* normal/open */
            }

            spr_draw_at(sheet_environment, door_spr_arr[di][spr_variant],
                       door_pos[di][0], door_pos[di][1], 1.0f, 1.0f);
        }

        /* Obstacles - use rock sprite */
        for (int i = 0; i < r->obstacle_count; i++) {
            Obstacle *o = &r->obstacles[i];
            if (!o->active) continue;
            spr_draw(sheet_environment, environment_atlas_env_rock_idx,
                     o->x, o->y, OBSTACLE_SIZE / 32.0f, OBSTACLE_SIZE / 32.0f);
        }

        /* Pedestal */
        if (r->pedestal.active) {
            float px = r->pedestal.x;
            float py = r->pedestal.y;
            float t = (float)g->frame;

            /* Outer glow pulse */
            float pulse = sinf(t * 0.05f) * 0.3f + 0.7f;
            u32 glowOuter = C2D_Color32(255, 255, 180, (int)(pulse * 40));
            C2D_DrawCircleSolid(px, py, 0, 26, glowOuter);

            /* Inner glow */
            u32 glowInner = C2D_Color32(255, 255, 200, (int)(pulse * 80));
            C2D_DrawCircleSolid(px, py, 0, 18, glowInner);

            /* Light rays (4 rotating lines as thin rects) */
            float rayAng = t * 0.03f;
            u32 rayCol = C2D_Color32(255, 255, 200, (int)(pulse * 35));
            for (int ri = 0; ri < 4; ri++) {
                float a = rayAng + ri * (3.14159f / 4.0f);
                float dx = cosf(a) * 22.0f;
                float dy = sinf(a) * 22.0f;
                C2D_DrawLine(px - dx, py - dy, rayCol, px + dx, py + dy, rayCol, 1.5f, 0);
            }

            /* Pedestal base - use ui_item_pickup sprite */
            spr_draw(sheet_ui_items, ui_items_atlas_ui_item_pickup_idx,
                     px, py + 5, 1.0f, 1.0f);

            /* Bobbing item sprite (or "?" if Curse of the Blind) */
            float bob = sinf(t * 0.06f) * 3.0f;
            float scPulse = 1.0f + sinf(t * 0.08f) * 0.05f;
            if (g->active_curse == CURSE_BLIND) {
                /* Draw a question mark instead */
                float qx = px;
                float qy = py - 6 + bob;
                C2D_DrawCircleSolid(qx, qy, 0, 8,
                                    C2D_Color32(255, 255, 200, 230));
                C2D_DrawCircleSolid(qx, qy, 0, 6,
                                    C2D_Color32(60, 40, 20, 255));
                /* Render "?" via tiny rects shaping the glyph */
                C2D_DrawRectSolid(qx - 2, qy - 3, 0, 4, 1,
                                  C2D_Color32(255, 255, 200, 255));
                C2D_DrawRectSolid(qx + 1, qy - 3, 0, 1, 3,
                                  C2D_Color32(255, 255, 200, 255));
                C2D_DrawRectSolid(qx - 1, qy, 0, 3, 1,
                                  C2D_Color32(255, 255, 200, 255));
                C2D_DrawRectSolid(qx - 1, qy + 1, 0, 1, 2,
                                  C2D_Color32(255, 255, 200, 255));
                C2D_DrawRectSolid(qx - 1, qy + 4, 0, 2, 2,
                                  C2D_Color32(255, 255, 200, 255));
            } else {
                int itemIdx = item_sprite_idx(r->pedestal.item);
                spr_draw(sheet_ui_items, itemIdx, px, py - 6 + bob, scPulse, scPulse);
            }
        }

        /* Heart pickups */
        for (int i = 0; i < MAX_HEART_PICKUPS; i++) {
            HeartPickup *h = &r->hearts[i];
            if (!h->active) continue;
            
            /* Bobbing animation */
            h->anim_timer++;
            float bob_offset = sinf((float)h->anim_timer * 0.1f) * 3.0f;
            
            /* Pick heart sprite based on type */
            int heart_idx;
            if (h->type == HEART_RED_FULL) {
                heart_idx = ui_items_atlas_heart_red_full_idx;
            } else if (h->type == HEART_RED_HALF) {
                heart_idx = ui_items_atlas_heart_red_half_idx;
            } else {
                heart_idx = ui_items_atlas_heart_soul_full_idx;
            }
            
            spr_draw(sheet_ui_items, heart_idx, h->x, h->y + bob_offset, 1.0f, 1.0f);
        }

        /* Consumable pickups (bombs, keys, coins, pills, cards) */
        for (int i = 0; i < MAX_CONSUMABLE_PICKUPS; i++) {
            ConsumablePickup *c = &r->consumables[i];
            if (!c->active) continue;
            c->anim_timer++;
            float bob_off = sinf((float)c->anim_timer * CONSUMABLE_BOB_SPEED) * CONSUMABLE_BOB_AMP;
            /* Pills and cards drawn as colored shapes (no atlas yet) */
            if (c->type == PICKUP_PILL) {
                /* Pill: two-tone capsule. Color from scrambled map per run */
                static const u32 pcols[12] = {
                    0xFFFFFFFF, 0xFFD05050, 0xFF5070E0, 0xFF50C870, 0xFFE0E060,
                    0xFFE060C0, 0xFFE08020, 0xFF8050E0, 0xFF40D0D0, 0xFFA0A0A0,
                    0xFFB05858, 0xFFE0E0E0
                };
                int colidx = g->pill_color_map[c->sub_type % PILL_EFFECT_COUNT] % 12;
                u32 col = pcols[colidx];
                float cy = c->y + bob_off;
                C2D_DrawRectSolid(c->x - 6, cy - 3, 0, 6, 6, col);
                C2D_DrawRectSolid(c->x, cy - 3, 0, 6, 6, 0xFFFFFFFF);
                continue;
            }
            if (c->type == PICKUP_CARD) {
                /* Card: white rectangle outlined gold with letter T */
                float cy = c->y + bob_off;
                C2D_DrawRectSolid(c->x - 5, cy - 7, 0, 10, 14, 0xFFFFD060);
                C2D_DrawRectSolid(c->x - 4, cy - 6, 0, 8, 12, 0xFFFFFFFF);
                C2D_DrawRectSolid(c->x - 1, cy - 4, 0, 2, 6, 0xFF606060);
                continue;
            }
            int cidx;
            switch (c->type) {
                case PICKUP_BOMB:  case PICKUP_BOMB2: cidx = ui_items_atlas_item_bomb_idx; break;
                case PICKUP_KEY:   cidx = ui_items_atlas_item_key_idx; break;
                case PICKUP_COIN:  case PICKUP_COIN5: cidx = ui_items_atlas_item_coin_idx; break;
                default: cidx = ui_items_atlas_item_coin_idx; break;
            }
            float csc = (c->type == PICKUP_COIN5 || c->type == PICKUP_BOMB2) ? 1.2f : 0.9f;
            spr_draw(sheet_ui_items, cidx, c->x, c->y + bob_off, csc, csc);
        }

        /* Shop items (item on pedestal + price, or sold-out indicator) */
        for (int i = 0; i < r->shop_count; i++) {
            ShopItem *si = &r->shop_items[i];
            if (si->active) {
                float bob_s = sinf((float)g->frame * 0.06f + (float)i) * 2.0f;
                int sIdx = item_sprite_idx(si->item);
                /* Pedestal */
                spr_draw(sheet_ui_items, ui_items_atlas_ui_item_pickup_idx,
                         si->x, si->y + 5, 0.8f, 0.8f);
                /* Item bobbing on pedestal */
                spr_draw(sheet_ui_items, sIdx, si->x, si->y - 4 + bob_s, 0.9f, 0.9f);
                /* Price tag: coin icon + number drawn via render_shop_prices */
                spr_draw(sheet_ui_items, ui_items_atlas_item_coin_idx,
                         si->x - 8, si->y + 16, 0.5f, 0.5f);
            } else {
                /* Sold-out: dim empty pedestal */
                spr_draw(sheet_ui_items, ui_items_atlas_ui_item_pickup_idx,
                         si->x, si->y + 5, 0.7f, 0.7f);
                /* Dark overlay to show sold */
                C2D_DrawRectSolid(si->x - 10, si->y, 0, 20, 12,
                                  C2D_Color32(0, 0, 0, 100));
            }
        }

        /* Active bomb (flashing before explosion) */
        if (g->bomb_timer > 0) {
            int flash = (g->bomb_flash / 4) % 2;
            float bsc = 1.0f + (flash ? 0.15f : 0.0f);
            spr_draw(sheet_ui_items, ui_items_atlas_item_bomb_idx,
                     g->bomb_x, g->bomb_y, bsc, bsc);
            /* Warning circle when close to exploding */
            if (g->bomb_timer < 30) {
                u32 warnCol = C2D_Color32(255, 100, 50, (int)(80 + flash * 60));
                C2D_DrawCircleSolid(g->bomb_x, g->bomb_y, 0, 48.0f * (1.0f - (float)g->bomb_timer / 30.0f), warnCol);
            }
        }

        /* Trapdoor */
        if (r->has_trapdoor) {
            float cx = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
            float cy = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
            float pulse = sinf((float)g->frame * 0.08f) * 0.15f + 1.0f;
            spr_draw(sheet_environment, environment_atlas_env_trapdoor_idx,
                     cx, cy, pulse, pulse);
        }

    } else {
        /* === FALLBACK: PROCEDURAL RENDERING (original code) === */
        u32 floorCol = COL_FLOOR;
        if (fl == 0) floorCol = C2D_Color32(140, 115, 90, 255);
        else if (fl == 1) floorCol = C2D_Color32(130, 110, 85, 255);
        else if (fl == 2) floorCol = C2D_Color32(100, 100, 110, 255);
        else if (fl == 3) floorCol = C2D_Color32(90, 90, 105, 255);
        else floorCol = C2D_Color32(80, 70, 80, 255);

        if (r->type == ROOM_BOSS) floorCol = C2D_Color32(120, 70, 70, 255);
        else if (r->type == ROOM_TREASURE) floorCol = C2D_Color32(140, 130, 90, 255);
        else if (r->type == ROOM_SHOP) floorCol = C2D_Color32(90, 120, 140, 255);
        else if (r->type == ROOM_SECRET) floorCol = C2D_Color32(100, 100, 100, 255);
        else if (r->type == ROOM_CURSE) floorCol = C2D_Color32(110, 60, 90, 255);

        u32 wallCol = COL_WALL;
        if (fl >= 2 && fl <= 3) wallCol = C2D_Color32(70, 70, 80, 255);
        else if (fl >= 4) wallCol = C2D_Color32(60, 50, 60, 255);

        C2D_DrawRectSolid(ROOM_LEFT, ROOM_TOP, 0,
                          ROOM_RIGHT - ROOM_LEFT, ROOM_BOTTOM - ROOM_TOP, floorCol);

        /* Floor grid */
        u32 gridCol = C2D_Color32(130, 105, 80, 255);
        for (float x = ROOM_LEFT; x < ROOM_RIGHT; x += 32)
            C2D_DrawRectSolid(x, ROOM_TOP, 0, 1, ROOM_BOTTOM - ROOM_TOP, gridCol);
        for (float y = ROOM_TOP; y < ROOM_BOTTOM; y += 32)
            C2D_DrawRectSolid(ROOM_LEFT, y, 0, ROOM_RIGHT - ROOM_LEFT, 1, gridCol);

        /* Walls - continuous solid rectangles */
        C2D_DrawRectSolid(0, ROOM_TOP - WALL_THICKNESS, 0,
                          TOP_SCREEN_WIDTH, WALL_THICKNESS, wallCol);
        C2D_DrawRectSolid(0, ROOM_BOTTOM, 0,
                          TOP_SCREEN_WIDTH, WALL_THICKNESS, wallCol);
        C2D_DrawRectSolid(0, ROOM_TOP, 0,
                          WALL_THICKNESS, ROOM_BOTTOM - ROOM_TOP, wallCol);
        C2D_DrawRectSolid(ROOM_RIGHT, ROOM_TOP, 0,
                          WALL_THICKNESS, ROOM_BOTTOM - ROOM_TOP, wallCol);

        /* Doors - colored overlays on walls with per-type colors */
        float midX = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
        float midY = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
        Dungeon *dd_fb = &g->dungeon;
        int rx_fb = dd_fb->cur_x, ry_fb = dd_fb->cur_y;
        int fb_dxs[] = {0, 0, -1, 1};
        int fb_dys[] = {-1, 1, 0, 0};
        for (int di = 0; di < 4; di++) {
            if (!r->doors[di]) continue;
            /* Choose color based on neighbor room type */
            u32 doorCol;
            if (!r->cleared && !r->door_locked[di] && r->door_type[di] == 0) {
                doorCol = COL_DOOR_LOCKED; /* normal door, room not cleared */
            } else {
                int nnx = rx_fb + fb_dxs[di], nny = ry_fb + fb_dys[di];
                RoomType ntype = ROOM_NONE;
                if (nnx >= 0 && nnx < DUNGEON_W && nny >= 0 && nny < DUNGEON_H)
                    ntype = dd_fb->rooms[nny][nnx].type;
                if (ntype == ROOM_TREASURE)
                    doorCol = C2D_Color32(220, 180, 50, 255);  /* gold/yellow */
                else if (ntype == ROOM_BOSS)
                    doorCol = C2D_Color32(200, 60, 60, 255);   /* red */
                else if (ntype == ROOM_CURSE)
                    doorCol = C2D_Color32(160, 50, 140, 255);  /* purple */
                else if (ntype == ROOM_SHOP)
                    doorCol = C2D_Color32(60, 180, 60, 255);   /* green */
                else
                    doorCol = COL_DOOR;
            }
            /* Draw a lock indicator on locked doors */
            int locked = r->door_locked[di];
            switch (di) {
                case 0: /* top */
                    C2D_DrawRectSolid(midX - DOOR_WIDTH / 2, ROOM_TOP - WALL_THICKNESS, 0,
                                      DOOR_WIDTH, WALL_THICKNESS, doorCol);
                    if (locked)
                        C2D_DrawRectSolid(midX - 4, ROOM_TOP - WALL_THICKNESS / 2 - 3, 0,
                                          8, 6, C2D_Color32(40, 40, 40, 255));
                    break;
                case 1: /* bottom */
                    C2D_DrawRectSolid(midX - DOOR_WIDTH / 2, ROOM_BOTTOM, 0,
                                      DOOR_WIDTH, WALL_THICKNESS, doorCol);
                    if (locked)
                        C2D_DrawRectSolid(midX - 4, ROOM_BOTTOM + WALL_THICKNESS / 2 - 3, 0,
                                          8, 6, C2D_Color32(40, 40, 40, 255));
                    break;
                case 2: /* left */
                    C2D_DrawRectSolid(0, midY - DOOR_WIDTH / 2, 0,
                                      WALL_THICKNESS, DOOR_WIDTH, doorCol);
                    if (locked)
                        C2D_DrawRectSolid(WALL_THICKNESS / 2 - 4, midY - 3, 0,
                                          8, 6, C2D_Color32(40, 40, 40, 255));
                    break;
                case 3: /* right */
                    C2D_DrawRectSolid(ROOM_RIGHT, midY - DOOR_WIDTH / 2, 0,
                                      WALL_THICKNESS, DOOR_WIDTH, doorCol);
                    if (locked)
                        C2D_DrawRectSolid(ROOM_RIGHT + WALL_THICKNESS / 2 - 4, midY - 3, 0,
                                          8, 6, C2D_Color32(40, 40, 40, 255));
                    break;
            }
        }

        /* Obstacles */
        for (int i = 0; i < r->obstacle_count; i++) {
            Obstacle *o = &r->obstacles[i];
            if (!o->active) continue;
            C2D_DrawRectSolid(o->x - OBSTACLE_SIZE / 2, o->y - OBSTACLE_SIZE / 2, 0,
                              OBSTACLE_SIZE, OBSTACLE_SIZE, COL_OBSTACLE);
        }

        /* Pedestal */
        if (r->pedestal.active) {
            float px = r->pedestal.x;
            float py = r->pedestal.y;
            float pulse = sinf((float)g->frame * 0.05f) * 0.3f + 0.7f;
            u32 glowCol = C2D_Color32(255, 255, 200, (int)(pulse * 80));
            C2D_DrawCircleSolid(px, py, 0, 20, glowCol);
            C2D_DrawRectSolid(px - 10, py + 4, 0, 20, 6, COL_PEDESTAL);
            if (g->active_curse == CURSE_BLIND) {
                /* Blind: render as "?" */
                C2D_DrawCircleSolid(px, py - 6, 0, 7,
                                    C2D_Color32(60, 40, 20, 255));
                C2D_DrawRectSolid(px - 1, py - 9, 0, 2, 4,
                                  C2D_Color32(255, 255, 200, 255));
                C2D_DrawRectSolid(px - 1, py - 3, 0, 2, 2,
                                  C2D_Color32(255, 255, 200, 255));
            } else {
                C2D_DrawCircleSolid(px, py - 6, 0, 7, C2D_Color32(255, 200, 50, 255));
            }
        }

        /* Consumable pickups (fallback) */
        for (int i = 0; i < MAX_CONSUMABLE_PICKUPS; i++) {
            ConsumablePickup *c = &r->consumables[i];
            if (!c->active) continue;
            c->anim_timer++;
            float bob_off = sinf((float)c->anim_timer * CONSUMABLE_BOB_SPEED) * CONSUMABLE_BOB_AMP;
            if (c->type == PICKUP_PILL) {
                C2D_DrawRectSolid(c->x - 5, c->y + bob_off - 3, 0, 5, 6, C2D_Color32(220, 80, 80, 255));
                C2D_DrawRectSolid(c->x, c->y + bob_off - 3, 0, 5, 6, 0xFFFFFFFF);
                continue;
            }
            if (c->type == PICKUP_CARD) {
                C2D_DrawRectSolid(c->x - 5, c->y + bob_off - 7, 0, 10, 14, 0xFFFFFFFF);
                continue;
            }
            u32 ccol;
            switch (c->type) {
                case PICKUP_BOMB:  case PICKUP_BOMB2: ccol = C2D_Color32(80, 80, 80, 255); break;
                case PICKUP_KEY:   ccol = C2D_Color32(255, 215, 0, 255); break;
                default:           ccol = C2D_Color32(255, 200, 50, 255); break;
            }
            float csz = (c->type == PICKUP_COIN5 || c->type == PICKUP_BOMB2) ? 7 : 5;
            C2D_DrawCircleSolid(c->x, c->y + bob_off, 0, csz, ccol);
        }

        /* Shop items (fallback) */
        for (int i = 0; i < r->shop_count; i++) {
            ShopItem *si = &r->shop_items[i];
            if (si->active) {
                float bob_s = sinf((float)g->frame * 0.06f + (float)i) * 2.0f;
                /* Pedestal base */
                C2D_DrawRectSolid(si->x - 10, si->y + 4, 0, 20, 6,
                                  COL_PEDESTAL);
                /* Item circle on pedestal */
                C2D_DrawCircleSolid(si->x, si->y - 4 + bob_s, 0, 7,
                                    C2D_Color32(255, 200, 50, 255));
                /* Price: coin dot + cost indicator */
                C2D_DrawCircleSolid(si->x - 6, si->y + 18, 0, 3,
                                    C2D_Color32(255, 215, 0, 255));
                /* Cost bars (simple visual for price) */
                for (int c = 0; c < si->cost && c < 7; c++) {
                    C2D_DrawRectSolid(si->x - 1 + c * 3, si->y + 16, 0,
                                      2, 4, C2D_Color32(255, 215, 0, 255));
                }
            } else {
                /* Sold-out: dim empty pedestal */
                C2D_DrawRectSolid(si->x - 10, si->y + 4, 0, 20, 6,
                                  C2D_Color32(60, 50, 50, 180));
            }
        }

        /* Active bomb (fallback) */
        if (g->bomb_timer > 0) {
            int flash = (g->bomb_flash / 4) % 2;
            u32 bc = flash ? C2D_Color32(255, 100, 50, 255) : C2D_Color32(80, 80, 80, 255);
            C2D_DrawCircleSolid(g->bomb_x, g->bomb_y, 0, 8, bc);
        }

        /* Trapdoor */
        if (r->has_trapdoor) {
            float cx = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
            float cy = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
            C2D_DrawCircleSolid(cx, cy, 0, 16, COL_TRAPDOOR);
            C2D_DrawCircleSolid(cx, cy, 0, 12, C2D_Color32(20, 15, 10, 255));
            C2D_DrawCircleSolid(cx, cy, 0, 6, C2D_Color32(0, 0, 0, 255));
        }
    }
}

/* ================================================================
 * Render: Game objects
 * ================================================================ */

static void render_player(Game *g) {
    Player *p = &g->player;

    /* Shadow under player */
    C2D_DrawEllipseSolid(p->x - PLAYER_SIZE, p->y + PLAYER_SIZE * 0.4f, 0,
                         PLAYER_SIZE * 2, PLAYER_SIZE * 0.6f, COL_SHADOW);

    /* Blink during iframes - faster blink for better feedback */
    if (p->iframes > 0 && (p->iframes / 3) % 2 == 0) return;

    if (g_sprites_loaded) {
        /* ═══ Sprite-based player rendering with full animation ═══
         *
         * Animation priority (highest first):
         *   1. Item pickup pose (arms raised, holding item above head)
         *   2. Hurt/death pose (cringing, only during first few iframes)
         *   3. Shooting face (crying head, tear direction) 
         *   4. Normal walk/idle (direction-based head + walk body cycle)
         */
        int idx;
        int dir = p->face_dir;
        if (dir == DIR_NONE) dir = DIR_DOWN;

        /* Sprites are 32x32 composed; scale to fit PLAYER_SIZE*2 display */
        float scale = (PLAYER_SIZE * 2.0f) / 32.0f;

        if (p->pickup_anim > 0) {
            /* Item pickup pose */
            idx = player_pickup_sprite_idx();
        } else if (p->iframes > 0 && p->iframes > PLAYER_IFRAMES - 12) {
            /* Hurt flinch (only first 12 frames of iframes) */
            idx = player_hurt_sprite_idx();
        } else if (p->shoot_anim > 0) {
            /* Shooting: use crying-face sprite in the shoot direction */
            idx = player_shoot_sprite_idx(p->shoot_dir);
        } else {
            /* Normal walk/idle */
            int frame = p->moving ? (p->anim_timer / 8) : 0;
            idx = player_sprite_idx(dir, frame);
        }

        if (p->iframes > 0) {
            /* Tint red when hurt - intensity scales with how recent the hit was */
            float intensity = (float)p->iframes / (float)PLAYER_IFRAMES;
            spr_draw_tinted(sheet_sprites, idx, p->x, p->y, scale, scale,
                           C2D_Color32(255, 60, 60, 255), 0.3f + intensity * 0.5f);
        } else {
            spr_draw(sheet_sprites, idx, p->x, p->y, scale, scale);
        }

        /* Item aura effects (procedural overlays) */
        if (p->stats.flags & ITEM_FLAG_HOMING) {
            float pulse = sinf((float)g->frame * 0.1f) * 2.0f;
            C2D_DrawCircleSolid(p->x, p->y, 0, PLAYER_SIZE + 3 + pulse,
                               C2D_Color32(200, 100, 255, 40));
        }
        if (p->stats.flags & ITEM_FLAG_SPECTRAL) {
            C2D_DrawCircleSolid(p->x, p->y, 0, PLAYER_SIZE + 2,
                               C2D_Color32(255, 255, 255, 30));
        }
    } else {
        /* Procedural fallback (no sprites loaded) */
        u32 col = (p->iframes > 0) ? COL_PLAYER_HIT : COL_PLAYER;
        C2D_DrawCircleSolid(p->x, p->y, 0, PLAYER_SIZE, col);
        u32 eyeCol = C2D_Color32(30, 30, 30, 255);
        C2D_DrawCircleSolid(p->x - 4, p->y - 2, 0, 2.5f, eyeCol);
        C2D_DrawCircleSolid(p->x + 4, p->y - 2, 0, 2.5f, eyeCol);
        C2D_DrawCircleSolid(p->x, p->y + 4, 0, 2.0f, eyeCol);

        if (p->stats.flags & ITEM_FLAG_HOMING) {
            float pulse = sinf((float)g->frame * 0.1f) * 2.0f;
            C2D_DrawCircleSolid(p->x, p->y, 0, PLAYER_SIZE + 3 + pulse,
                               C2D_Color32(200, 100, 255, 40));
        }
        if (p->stats.flags & ITEM_FLAG_SPECTRAL) {
            C2D_DrawCircleSolid(p->x, p->y, 0, PLAYER_SIZE + 2,
                               C2D_Color32(255, 255, 255, 30));
        }
    }
}

static void render_tears(Game *g) {
    for (int i = 0; i < MAX_TEARS; i++) {
        Tear *t = &g->tears[i];
        if (!t->active) continue;

        /* Render y position adjusted by arc (z offset) */
        float render_y = t->y + t->z;

        if (sheet_bullets) {
            /* Pick tear sprite size based on damage */
            int base_idx;
            if (t->is_enemy) {
                base_idx = bulletatlas_tear_red_1_idx;
            } else if (t->homing) {
                base_idx = bulletatlas_tear_dark_1_idx;
            } else {
                base_idx = bulletatlas_tear_blue_1_idx;
            }

            /* Select size variant (0-6) based on damage */
            int size_variant;
            if (t->dmg < 1.5f)      size_variant = 1;  /* small */
            else if (t->dmg < 2.5f) size_variant = 2;  /* medium-small */
            else if (t->dmg < 3.5f) size_variant = 3;  /* medium */
            else if (t->dmg < 5.0f) size_variant = 4;  /* large */
            else                     size_variant = 5;  /* huge */

            int tear_idx = base_idx + size_variant;

            /* Scale to match game TEAR_RADIUS */
            float base_scale = (TEAR_RADIUS * 2.0f) / (8.0f + size_variant * 2.0f);
            if (t->dmg > 2.0f) base_scale *= 1.2f;

            /* Subtle pulsing scale */
            float pulse = 1.0f + sinf(t->anim_frame * 0.2f) * 0.08f;
            float scale = base_scale * pulse;

            /* Shadow underneath */
            C2D_DrawCircleSolid(t->x, t->y + 2.0f, 0,
                               TEAR_RADIUS * 0.6f * base_scale,
                               C2D_Color32(0, 0, 0, 40));

            if (t->spectral) {
                /* Spectral: semi-transparent with ghostly tint */
                spr_draw_rotated_tinted(sheet_bullets, tear_idx,
                                        t->x, render_y, scale, scale,
                                        t->rotation,
                                        C2D_Color32(180, 220, 255, 180), 0.4f);
            } else if (t->homing) {
                /* Homing: purple tint */
                spr_draw_rotated_tinted(sheet_bullets, tear_idx,
                                        t->x, render_y, scale, scale,
                                        t->rotation,
                                        C2D_Color32(200, 100, 255, 255), 0.5f);
            } else {
                /* Normal tear */
                spr_draw_rotated(sheet_bullets, tear_idx,
                                t->x, render_y, scale, scale,
                                t->rotation);
            }

            /* Piercing trail effect: smaller copies behind */
            if (t->piercing) {
                float trail_alpha = 0.35f;
                for (int trail = 1; trail <= 2; trail++) {
                    float tx = t->x - t->dx * 0.4f * trail;
                    float ty = render_y - t->dy * 0.4f * trail;
                    float ts = scale * (0.7f - trail * 0.15f);
                    spr_draw_rotated_alpha(sheet_bullets, tear_idx,
                                           tx, ty, ts, ts,
                                           t->rotation, trail_alpha);
                    trail_alpha *= 0.6f;
                }
            }
        } else {
            /* Procedural fallback */
            u32 tearCol = COL_TEAR;
            float radius = TEAR_RADIUS;
            if (t->homing) tearCol = COL_HOMING_TEAR;
            if (t->spectral) tearCol = COL_SPECTRAL_TEAR;
            if (t->dmg > 2.0f) radius = TEAR_RADIUS * 1.3f;

            C2D_DrawCircleSolid(t->x, render_y, 0, radius, tearCol);
            u32 hl = C2D_Color32(220, 230, 255, 200);
            C2D_DrawCircleSolid(t->x - 1, render_y - 1, 0, radius * 0.4f, hl);

            if (t->piercing) {
                C2D_DrawCircleSolid(t->x - t->dx * 0.5f, render_y - t->dy * 0.5f, 0,
                                   radius * 0.5f, C2D_Color32(100, 150, 255, 100));
            }
        }
    }
}

static void render_enemies(Game *g) {
    Room *r = current_room(g);

    for (int i = 0; i < r->enemy_count; i++) {
        Enemy *e = &r->enemies[i];
        if (!e->active) continue;

        int boss = is_boss_type(e->type);
        float sz = boss ? ENEMY_SIZE * 2 : ENEMY_SIZE;

        /* Shadow */
        C2D_DrawEllipseSolid(e->x - sz, e->y + sz * 0.3f, 0,
                             sz * 2, sz * 0.5f, COL_SHADOW);

        /* Champion aura: pulsing colored circle behind enemy */
        if (e->champion != CHAMP_NONE) {
            u32 acol = 0xFF606060;
            switch (e->champion) {
                case CHAMP_RED:    acol = C2D_Color32(255,  80,  80, 140); break;
                case CHAMP_BLUE:   acol = C2D_Color32(100, 140, 255, 140); break;
                case CHAMP_YELLOW: acol = C2D_Color32(240, 240,  80, 140); break;
                case CHAMP_BLACK:  acol = C2D_Color32( 30,  30,  30, 180); break;
                default: break;
            }
            float pulse = 1.0f + sinf((float)g->frame * 0.12f) * 0.15f;
            C2D_DrawCircleSolid(e->x, e->y, 0, sz * 1.2f * pulse, acol);
        }

        if (g_sprites_loaded) {
            /* ===== Animated enemy sprites for Clotty & Pacer ===== */
            int use_enemy_atlas = 0;

            if (e->type == ENEMY_CLOTTY && sheet_enemies) {
                /* Clotty: 4 idle frames, 4 shoot frames, 3 angry frames
                 * Pick state based on shoot_timer proximity */
                int idx;
                int anim_speed = 8; /* frames per sprite frame */
                if (e->shoot_timer < 20) {
                    /* About to shoot or just shot - squash/shoot animation */
                    int frame = (e->anim_timer / anim_speed) % 4;
                    idx = enemies_atlas_clotty_shoot_0_idx + frame;
                } else if (e->flash > 0) {
                    /* Hit flash - use angry sprites */
                    int frame = (e->anim_timer / anim_speed) % 3;
                    idx = enemies_atlas_clotty_angry_0_idx + frame;
                } else {
                    /* Idle bobbing animation */
                    int frame = (e->anim_timer / anim_speed) % 4;
                    idx = enemies_atlas_clotty_idle_0_idx + frame;
                }

                float sprSrcSize = 24.0f; /* clotty sprites are 24x24 */
                float scale = (sz * 2.0f) / sprSrcSize;
                /* Subtle squash-stretch based on movement */
                float squash = 1.0f + sinf(e->anim_timer * 0.15f) * 0.06f;
                float scaleX = scale * (1.0f / squash);
                float scaleY = scale * squash;

                if (e->flash > 0) {
                    spr_draw_tinted(sheet_enemies, idx, e->x, e->y, scaleX, scaleY,
                                   C2D_Color32(255, 255, 255, 255), 0.7f);
                } else {
                    spr_draw(sheet_enemies, idx, e->x, e->y, scaleX, scaleY);
                }
                use_enemy_atlas = 1;

            } else if (e->type == ENEMY_PACER && sheet_enemies) {
                /* Pacer: 22 walk frames, use 8 for smooth walk cycle
                 * Rows 0-1 (frames 0-7) are a good walk cycle */
                int anim_speed = 5; /* faster animation for walking */
                int frame = (e->anim_timer / anim_speed) % 8;
                int idx = enemies_atlas_pacer_walk_00_idx + frame;

                float sprSrcSize = 20.0f; /* pacer sprites are 20x20 */
                float scale = (sz * 2.0f) / sprSrcSize;

                /* Flip horizontally when moving left */
                if (e->dx < 0) {
                    if (e->flash > 0) {
                        /* Can't easily flip+tint with current API, draw tinted normally */
                        spr_draw_tinted(sheet_enemies, idx, e->x, e->y, scale, scale,
                                       C2D_Color32(255, 255, 255, 255), 0.7f);
                    } else {
                        spr_draw_fliph(sheet_enemies, idx, e->x, e->y, scale, scale);
                    }
                } else {
                    if (e->flash > 0) {
                        spr_draw_tinted(sheet_enemies, idx, e->x, e->y, scale, scale,
                                       C2D_Color32(255, 255, 255, 255), 0.7f);
                    } else {
                        spr_draw(sheet_enemies, idx, e->x, e->y, scale, scale);
                    }
                }

                /* Speed up animation during charge */
                if (e->state == 1) {
                    /* Use faster frames from row 2+ during charge */
                    /* Already handled by anim_timer increment speed */
                }
                use_enemy_atlas = 1;
            }

            if (!use_enemy_atlas) {
                /* Other enemies: use original sprites atlas */
                int idx;
                float sprSrcSize;
                if (boss) {
                    idx = boss_sprite_idx(e->type);
                    sprSrcSize = 80.0f;
                } else {
                    idx = enemy_sprite_idx(e->type);
                    sprSrcSize = 20.0f;
                }
                float scale = (sz * 2.0f) / sprSrcSize;

                /* Boss-specific rendering enhancements */
                if (boss) {
                    float draw_x = e->x;
                    float draw_y = e->y;
                    float draw_scaleX = scale;
                    float draw_scaleY = scale;

                    /* Monstro: jump arc offset + squash/stretch */
                    if (e->type == ENEMY_BOSS_MONSTRO) {
                        draw_y += e->jump_z; /* negative = up */
                        if (e->phase == 1) {
                            /* In air: stretch vertically */
                            draw_scaleX = scale * 0.85f;
                            draw_scaleY = scale * 1.15f;
                        } else if (e->phase == 2) {
                            /* Landing: squash */
                            float landT = e->timer / 15.0f;
                            draw_scaleX = scale * (1.0f + landT * 0.2f);
                            draw_scaleY = scale * (1.0f - landT * 0.15f);
                        } else {
                            /* Idle bob */
                            float bob = sinf(e->anim_timer * 0.08f) * 2.0f;
                            draw_y += bob;
                        }
                    }

                    /* Duke: gentle bobbing */
                    if (e->type == ENEMY_BOSS_DUKE) {
                        draw_y += e->wobble;
                        /* Breathing scale */
                        float breath = 1.0f + sinf(g->frame * 0.04f) * 0.03f;
                        draw_scaleX *= breath;
                        draw_scaleY *= breath;
                    }

                    /* Famine: tint change in phase 2 (headless) */
                    if (e->type == ENEMY_BOSS_FAMINE && e->phase == 1) {
                        /* Leaning in charge direction */
                        float lean = e->dx * 0.02f;
                        if (e->flash > 0) {
                            spr_draw_rotated_tinted(sheet_sprites, idx, draw_x, draw_y,
                                                   draw_scaleX, draw_scaleY, lean,
                                                   C2D_Color32(255, 100, 100, 255), 0.5f);
                        } else {
                            spr_draw_rotated_tinted(sheet_sprites, idx, draw_x, draw_y,
                                                   draw_scaleX, draw_scaleY, lean,
                                                   C2D_Color32(200, 60, 60, 255), 0.25f);
                        }
                    } else if (e->flash > 0) {
                        spr_draw_tinted(sheet_sprites, idx, draw_x, draw_y,
                                       draw_scaleX, draw_scaleY,
                                       C2D_Color32(255, 255, 255, 255), 0.7f);
                    } else {
                        spr_draw(sheet_sprites, idx, draw_x, draw_y,
                                draw_scaleX, draw_scaleY);
                    }

                    /* Larry Jr: render body segments */
                    if (e->type == ENEMY_BOSS_LARRY) {
                        for (int s = 0; s < e->seg_count; s++) {
                            LarrySegment *seg = &e->segments[s];
                            /* Shadow for each segment */
                            float segSz = ENEMY_SIZE * 1.5f;
                            C2D_DrawEllipseSolid(seg->x - segSz * 0.5f, seg->y + segSz * 0.15f, 0,
                                                segSz, segSz * 0.3f, COL_SHADOW);
                            /* Segment body - use boss sprite scaled down, or procedural circle */
                            float segScale = scale * (0.7f - s * 0.06f); /* segments get smaller */
                            if (e->flash > 0) {
                                spr_draw_tinted(sheet_sprites, idx, seg->x, seg->y,
                                               segScale, segScale,
                                               C2D_Color32(255, 255, 255, 255), 0.7f);
                            } else {
                                spr_draw(sheet_sprites, idx, seg->x, seg->y,
                                        segScale, segScale);
                            }
                        }
                    }

                    /* Gemini: render companion entity + tether */
                    if (e->type == ENEMY_BOSS_GEMINI) {
                        /* Draw tether line (if not split) */
                        if (!e->gemini_split) {
                            u32 tetherCol = C2D_Color32(180, 60, 60, 180);
                            C2D_DrawLine(e->x, e->y, tetherCol,
                                        e->gemini_cx, e->gemini_cy, tetherCol, 2.0f, 0);
                        }

                        /* Companion shadow */
                        float csz = ENEMY_SIZE * 1.2f;
                        C2D_DrawEllipseSolid(e->gemini_cx - csz, e->gemini_cy + csz * 0.3f, 0,
                                            csz * 2, csz * 0.5f, COL_SHADOW);

                        /* Companion body - smaller version, possibly different tint */
                        float cScale = scale * 0.6f;
                        float cBob = sinf(g->frame * 0.1f) * 1.5f;
                        if (e->flash > 0 || (e->gemini_split && e->state == 1)) {
                            spr_draw_tinted(sheet_sprites, idx, e->gemini_cx, e->gemini_cy + cBob,
                                           cScale, cScale,
                                           C2D_Color32(255, 150, 150, 255), 0.5f);
                        } else {
                            spr_draw(sheet_sprites, idx, e->gemini_cx, e->gemini_cy + cBob,
                                    cScale, cScale);
                        }
                    }

                    /* Monstro: landing shadow when in air */
                    if (e->type == ENEMY_BOSS_MONSTRO && e->phase == 1) {
                        float shadowAlpha = clampf(-e->jump_z / 40.0f, 0.2f, 0.6f);
                        float shadowSz = sz + (-e->jump_z * 0.3f);
                        C2D_DrawEllipseSolid(e->x - shadowSz, e->y + sz * 0.3f, 0,
                                            shadowSz * 2, shadowSz * 0.4f,
                                            C2D_Color32(0, 0, 0, (u8)(shadowAlpha * 180)));
                    }

                } else {
                    /* Regular enemy sprite */
                    float draw_y = e->y;
                    /* Hopper/Leaper: offset Y by jump_arc for airborne effect */
                    if (e->type == ENEMY_HOPPER || e->type == ENEMY_LEAPER) {
                        draw_y -= e->jump_arc;
                    }
                    /* Host: draw at half alpha when hidden */
                    if (e->type == ENEMY_HOST && e->hidden) {
                        spr_draw_tinted(sheet_sprites, idx, e->x, draw_y, scale, scale,
                                       C2D_Color32(128, 128, 128, 180), 0.6f);
                    } else if (e->type == ENEMY_GLOBIN && e->state == 1) {
                        /* Globin collapsed: draw squished */
                        spr_draw_tinted(sheet_sprites, idx, e->x, draw_y + 4.0f,
                                       scale * 1.3f, scale * 0.5f,
                                       C2D_Color32(200, 80, 80, 200), 0.5f);
                    } else if (e->flash > 0) {
                        spr_draw_tinted(sheet_sprites, idx, e->x, draw_y, scale, scale,
                                       C2D_Color32(255, 255, 255, 255), 0.7f);
                    } else {
                        spr_draw(sheet_sprites, idx, e->x, draw_y, scale, scale);
                    }
                }
            }
        } else {
            /* Procedural fallback */
            u32 col;
            if (e->flash > 0) {
                col = COL_ENEMY_HIT;
            } else {
                switch (e->type) {
                    case ENEMY_FLY:          col = COL_ENEMY_FLY;   break;
                    case ENEMY_GAPER:        col = COL_ENEMY_GAPER; break;
                    case ENEMY_PACER:        col = COL_ENEMY_PACER; break;
                    case ENEMY_SPIDER:       col = C2D_Color32(80, 60, 40, 255); break;
                    case ENEMY_CLOTTY:       col = C2D_Color32(150, 40, 40, 255); break;
                    case ENEMY_ATTACK_FLY:   col = C2D_Color32(180, 50, 50, 255); break;
                    case ENEMY_POOTER:       col = C2D_Color32(100, 80, 140, 255); break;
                    case ENEMY_HOPPER:       col = C2D_Color32(140, 100, 60, 255); break;
                    case ENEMY_BABY:         col = C2D_Color32(220, 180, 160, 255); break;
                    case ENEMY_GLOBIN:       col = C2D_Color32(180, 40, 40, 255); break;
                    case ENEMY_BOOM_FLY:     col = C2D_Color32(200, 120, 40, 255); break;
                    case ENEMY_MAW:          col = C2D_Color32(120, 60, 80, 255); break;
                    case ENEMY_MULLIGAN:     col = C2D_Color32(160, 140, 100, 255); break;
                    case ENEMY_HOST:         col = C2D_Color32(180, 180, 180, 255); break;
                    case ENEMY_RED_MAW:      col = C2D_Color32(200, 40, 60, 255); break;
                    case ENEMY_LEAPER:       col = C2D_Color32(100, 140, 80, 255); break;
                    case ENEMY_VIS:          col = C2D_Color32(140, 60, 140, 255); break;
                    case ENEMY_BOSS_DUKE:       col = C2D_Color32(80, 120, 80, 255); break;
                    case ENEMY_BOSS_MONSTRO:    col = C2D_Color32(160, 100, 80, 255); break;
                    case ENEMY_BOSS_GEMINI:     col = C2D_Color32(200, 80, 80, 255); break;
                    case ENEMY_BOSS_LARRY:      col = C2D_Color32(180, 140, 60, 255); break;
                    case ENEMY_BOSS_FAMINE:     col = C2D_Color32(140, 140, 140, 255); break;
                    case ENEMY_BOSS_PEEP:       col = C2D_Color32(220, 200, 180, 255); break;
                    case ENEMY_BOSS_GURDY:      col = C2D_Color32(140, 100, 80, 255); break;
                    case ENEMY_BOSS_PIN:        col = C2D_Color32(190, 220, 180, 255); break;
                    case ENEMY_BOSS_HAUNT:      col = C2D_Color32(70, 70, 90, 255); break;
                    case ENEMY_BOSS_WIDOW:      col = C2D_Color32(120, 80, 120, 255); break;
                    case ENEMY_BOSS_MEGA_SATAN: col = C2D_Color32(40, 0, 0, 255); break;
                    case ENEMY_EYE:             col = C2D_Color32(230, 220, 220, 255); break;
                    case ENEMY_LIL_HAUNT:       col = C2D_Color32(120, 100, 130, 255); break;
                    default:                    col = COL_ENEMY_FLY; break;
                }
            }

            C2D_DrawCircleSolid(e->x, e->y, 0, sz, col);

            if (boss) {
                u32 eyeW = C2D_Color32(255, 255, 255, 255);
                u32 eyeB = C2D_Color32(255, 0, 0, 255);
                C2D_DrawCircleSolid(e->x - 7, e->y - 4, 0, 5.0f, eyeW);
                C2D_DrawCircleSolid(e->x + 7, e->y - 4, 0, 5.0f, eyeW);
                C2D_DrawCircleSolid(e->x - 7, e->y - 3, 0, 3.0f, eyeB);
                C2D_DrawCircleSolid(e->x + 7, e->y - 3, 0, 3.0f, eyeB);
                C2D_DrawRectSolid(e->x - 8, e->y + 6, 0, 16, 4, C2D_Color32(50, 0, 0, 255));

                float barW = 40.0f;
                float hpPct = (float)e->hp / (float)e->max_hp;
                if (hpPct < 0) hpPct = 0;
                C2D_DrawRectSolid(e->x - barW / 2, e->y - sz - 8, 0,
                                  barW, 4, C2D_Color32(60, 60, 60, 255));
                u32 hpBarCol = (hpPct > 0.5f) ? COL_HEART_FULL :
                               (hpPct > 0.25f) ? C2D_Color32(255, 165, 0, 255) :
                               C2D_Color32(255, 50, 50, 255);
                C2D_DrawRectSolid(e->x - barW / 2, e->y - sz - 8, 0,
                                  barW * hpPct, 4, hpBarCol);
            } else if (e->type == ENEMY_FLY) {
                u32 wingCol = C2D_Color32(120, 120, 120, 180);
                float wOff = (g->frame % 10 < 5) ? 2.0f : -1.0f;
                C2D_DrawCircleSolid(e->x - 8, e->y - 4 + wOff, 0, 5.0f, wingCol);
                C2D_DrawCircleSolid(e->x + 8, e->y - 4 + wOff, 0, 5.0f, wingCol);
            } else {
                u32 eyeW = C2D_Color32(255, 255, 255, 255);
                u32 eyeB2 = C2D_Color32(10, 10, 10, 255);
                C2D_DrawCircleSolid(e->x - 3, e->y - 2, 0, 3.0f, eyeW);
                C2D_DrawCircleSolid(e->x + 3, e->y - 2, 0, 3.0f, eyeW);
                C2D_DrawCircleSolid(e->x - 3, e->y - 1, 0, 1.5f, eyeB2);
                C2D_DrawCircleSolid(e->x + 3, e->y - 1, 0, 1.5f, eyeB2);
            }
        }
    }
}

/* ================================================================
 * Render: Floor transition screen
 * ================================================================ */

void render_floor_transition(Game *g, C2D_TextBuf textBuf) {
    C2D_DrawRectSolid(0, 0, 0, TOP_SCREEN_WIDTH, TOP_SCREEN_HEIGHT,
                     C2D_Color32(0, 0, 0, 255));

    const FloorInfo *fi = get_floor_info(g->current_floor);

    C2D_Text t1;
    C2D_TextParse(&t1, textBuf, fi->name);
    C2D_TextOptimize(&t1);

    /* Fade in effect */
    float alpha = 1.0f;
    if (g->floor_transition_timer > 100) {
        alpha = (120.0f - g->floor_transition_timer) / 20.0f;
    } else if (g->floor_transition_timer < 20) {
        alpha = g->floor_transition_timer / 20.0f;
    }
    if (alpha < 0) alpha = 0;
    if (alpha > 1) alpha = 1;

    u32 textCol = C2D_Color32(255, 220, 150, (int)(alpha * 255));
    C2D_DrawText(&t1, C2D_WithColor, 130, 90, 0, 0.9f, 0.9f, textCol);

    /* Floor number (show total in infinite mode) */
    C2D_Text t2;
    char floorBuf[48];
    if (g->game_mode == MODE_INFINITE && g->infinite_loop > 0) {
        int total = g->infinite_loop * MAX_FLOORS + g->current_floor + 1;
        snprintf(floorBuf, sizeof(floorBuf), "Floor %d (Loop %d)", total, g->infinite_loop + 1);
    } else {
        snprintf(floorBuf, sizeof(floorBuf), "Floor %d", g->current_floor + 1);
    }
    C2D_TextParse(&t2, textBuf, floorBuf);
    C2D_TextOptimize(&t2);
    C2D_DrawText(&t2, C2D_WithColor, 140, 130, 0, 0.6f, 0.6f,
                C2D_Color32(180, 180, 180, (int)(alpha * 255)));
}

/* ================================================================
 * Render: Game Over / Win screens
 * ================================================================ */

void render_gameover(Game *g, C2D_TextBuf textBuf) {
    C2D_Text t1, t2, t3;
    char scoreBuf[32];
    snprintf(scoreBuf, sizeof(scoreBuf), "Score: %d", g->score);

    /* Draw death sprite */
    if (g_sprites_loaded) {
        float dscale = 2.0f;
        spr_draw(sheet_sprites, player_death_sprite_idx(),
                 TOP_SCREEN_WIDTH / 2.0f, 50, dscale, dscale);
    }

    C2D_TextParse(&t1, textBuf, "YOU DIED");
    C2D_TextOptimize(&t1);
    C2D_DrawText(&t1, C2D_WithColor, 135, 70, 0, 0.9f, 0.9f, COL_HEART_FULL);

    /* Show floor reached */
    const FloorInfo *fi = get_floor_info(g->current_floor);
    C2D_Text flText;
    char flBuf[64];
    if (g->game_mode == MODE_INFINITE) {
        int total = g->best_floor + 1;
        snprintf(flBuf, sizeof(flBuf), "Deepest Floor: %d (%s)", total, fi->name);
    } else {
        snprintf(flBuf, sizeof(flBuf), "Reached: %s", fi->name);
    }
    C2D_TextParse(&flText, textBuf, flBuf);
    C2D_TextOptimize(&flText);
    C2D_DrawText(&flText, C2D_WithColor, 90, 100, 0, 0.55f, 0.55f,
                C2D_Color32(200, 200, 200, 255));

    C2D_TextParse(&t2, textBuf, scoreBuf);
    C2D_TextOptimize(&t2);
    C2D_DrawText(&t2, C2D_WithColor, 155, 130, 0, 0.6f, 0.6f, COL_TEXT);

    /* Items collected */
    if (g->player.item_count > 0) {
        C2D_Text itemsText;
        char ibuf[32];
        snprintf(ibuf, sizeof(ibuf), "Items: %d", g->player.item_count);
        C2D_TextParse(&itemsText, textBuf, ibuf);
        C2D_TextOptimize(&itemsText);
        C2D_DrawText(&itemsText, C2D_WithColor, 160, 155, 0, 0.5f, 0.5f,
                    C2D_Color32(180, 180, 255, 255));
    }

    C2D_TextParse(&t3, textBuf, "Press START for menu");
    C2D_TextOptimize(&t3);
    C2D_DrawText(&t3, C2D_WithColor, 110, 200, 0, 0.55f, 0.55f,
                 C2D_Color32(255, 220, 100, 255));
}

void render_win(Game *g, C2D_TextBuf textBuf) {
    C2D_Text t1, t2, t3;
    char scoreBuf[32];
    snprintf(scoreBuf, sizeof(scoreBuf), "Final Score: %d", g->score);

    C2D_TextParse(&t1, textBuf, "YOU ESCAPED!");
    C2D_TextOptimize(&t1);
    C2D_DrawText(&t1, C2D_WithColor, 110, 40, 0, 0.9f, 0.9f,
                 C2D_Color32(100, 255, 100, 255));

    C2D_TextParse(&t2, textBuf, scoreBuf);
    C2D_TextOptimize(&t2);
    C2D_DrawText(&t2, C2D_WithColor, 135, 85, 0, 0.6f, 0.6f, COL_TEXT);

    C2D_Text t4;
    char floorBuf[48];
    snprintf(floorBuf, sizeof(floorBuf), "All %d floors conquered!", MAX_FLOORS);
    C2D_TextParse(&t4, textBuf, floorBuf);
    C2D_TextOptimize(&t4);
    C2D_DrawText(&t4, C2D_WithColor, 100, 115, 0, 0.55f, 0.55f,
                 C2D_Color32(200, 200, 200, 255));

    /* Show items collected */
    C2D_Text itemsText;
    char ibuf[48];
    snprintf(ibuf, sizeof(ibuf), "Items Collected: %d", g->player.item_count);
    C2D_TextParse(&itemsText, textBuf, ibuf);
    C2D_TextOptimize(&itemsText);
    C2D_DrawText(&itemsText, C2D_WithColor, 120, 145, 0, 0.5f, 0.5f,
                C2D_Color32(180, 180, 255, 255));

    C2D_TextParse(&t3, textBuf, "Press START for menu");
    C2D_TextOptimize(&t3);
    C2D_DrawText(&t3, C2D_WithColor, 108, 190, 0, 0.55f, 0.55f,
                 C2D_Color32(255, 220, 100, 255));
}

/* ================================================================
 * Top-screen composite render
 * ================================================================ */

/* Render enemy projectiles (clotty blood shots) */
static void render_enemy_shots(Game *g) {
    for (int i = 0; i < MAX_ENEMY_SHOTS; i++) {
        EnemyShot *s = &g->enemy_shots[i];
        if (!s->active) continue;

        if (sheet_bullets) {
            /* Use red tear sprites from bullet atlas */
            int tear_idx = bulletatlas_tear_red_2_idx;  /* small-medium variant */
            float base_scale = 0.6f;  /* enemy shots are smaller than player tears */

            /* Subtle pulsing */
            float pulse = 1.0f + sinf(s->dist * 0.15f) * 0.1f;
            float scale = base_scale * pulse;

            /* Compute rotation from movement direction */
            float angle = atan2f(s->dy, s->dx);

            /* Shadow underneath */
            C2D_DrawCircleSolid(s->x, s->y + 1.5f, 0, 3.0f * base_scale,
                               C2D_Color32(0, 0, 0, 40));

            /* Draw red tear sprite with slight darkening tint for menace */
            spr_draw_rotated_tinted(sheet_bullets, tear_idx,
                                    s->x, s->y, scale, scale, angle,
                                    C2D_Color32(180, 40, 40, 255), 0.3f);
        } else {
            /* Procedural fallback: dark red blood drop */
            C2D_DrawCircleSolid(s->x, s->y, 0, 4.0f, C2D_Color32(140, 30, 30, 255));
            C2D_DrawCircleSolid(s->x - 1, s->y - 1, 0, 1.5f, C2D_Color32(255, 100, 100, 200));
        }
    }
}

/* Draw shop price numbers and "SOLD" text (needs textBuf, called from game_render_top) */
static void render_shop_prices(Game *g, C2D_TextBuf textBuf) {
    Room *r = current_room(g);
    if (r->type != ROOM_SHOP) return;

    for (int i = 0; i < r->shop_count; i++) {
        ShopItem *si = &r->shop_items[i];
        if (si->active) {
            /* Draw price number next to coin icon */
            char price_str[8];
            snprintf(price_str, sizeof(price_str), "%d", si->cost);
            C2D_Text priceText;
            C2D_TextParse(&priceText, textBuf, price_str);
            C2D_TextOptimize(&priceText);
            /* Color: green if affordable, red if not */
            u32 pcol = (g->player.coins >= si->cost)
                ? C2D_Color32(100, 255, 100, 255)
                : C2D_Color32(255, 80, 80, 255);
            C2D_DrawText(&priceText, C2D_WithColor,
                         si->x - 2, si->y + 14, 0, 0.4f, 0.4f, pcol);
        } else {
            /* "SOLD" text on empty pedestal */
            C2D_Text soldText;
            C2D_TextParse(&soldText, textBuf, "SOLD");
            C2D_TextOptimize(&soldText);
            C2D_DrawText(&soldText, C2D_WithColor,
                         si->x - 10, si->y + 2, 0, 0.35f, 0.35f,
                         C2D_Color32(180, 80, 80, 200));
        }
    }
}

void game_render_top(Game *g, C2D_TextBuf textBuf) {
    C2D_TextBufClear(textBuf);

    /* Reset view, then apply screen shake offset for gameplay rendering */
    C2D_ViewReset();

    switch (g->state) {
    case STATE_MENU:
        render_menu(g, textBuf);
        break;

    case STATE_MODE_SELECT:
        render_mode_select(g, textBuf);
        break;

    case STATE_CHARACTER_SELECT:
        render_character_select(g, textBuf);
        break;

    case STATE_DIFFICULTY_SELECT:
        render_difficulty_select(g, textBuf);
        break;

    case STATE_CONTROLS:
        render_controls(textBuf);
        break;

    case STATE_UNLOCKS:
        render_unlocks_screen(g, textBuf);
        break;

    case STATE_SETTINGS:
        render_settings(g, textBuf);
        break;

    case STATE_PLAYING: {
        /* Compute shake offset */
        float sox = 0, soy = 0;
        if (g->shake_timer > 0 && g->shake_intensity > 0.1f) {
            sox = randf(-g->shake_intensity, g->shake_intensity);
            soy = randf(-g->shake_intensity, g->shake_intensity);
            C2D_ViewTranslate(sox, soy);
        }

        render_room(g);
        render_shop_prices(g, textBuf);  /* price text overlay on shop items */
        render_tears(g);
        render_enemy_shots(g);
        render_blood_particles(g);
        render_enemies(g);
        render_player(g);

        /* Floor tint overlay for Womb (red) and Sheol (dark) */
        if (g->current_floor == 5) {
            /* Womb: red flesh tint */
            C2D_DrawRectSolid(0, 0, 0, TOP_SCREEN_WIDTH, TOP_SCREEN_HEIGHT,
                              C2D_Color32(140, 30, 40, 50));
        } else if (g->current_floor == 6) {
            /* Sheol: dark hellish tint */
            C2D_DrawRectSolid(0, 0, 0, TOP_SCREEN_WIDTH, TOP_SCREEN_HEIGHT,
                              C2D_Color32(20, 0, 0, 90));
        }

        /* Curse: Darkness - real circular vignette centered on the player.
         * Only a small radius around Isaac is visible; the rest of the
         * room is heavily darkened so the curse is actually a challenge. */
        if (g->active_curse == CURSE_DARKNESS) {
            float cx = g->player.x;
            float cy = g->player.y;
            /* Vision radius: ~80px clear, dim from there outward */
            const float clear_r = 70.0f;
            const float dim_r   = 130.0f;
            /* Solid black corners (outside dim_r) - cover screen with 4 wide
             * rectangles that don't include the visible center area */
            const int OUTER_A = 220;          /* near-black outside */
            const int MID_A   = 180;          /* heavy dim in middle band */

            /* Full-screen black underlay, then "punch out" the visible area
             * using stacked semi-transparent circles to fake a radial gradient. */
            C2D_DrawRectSolid(0, 0, 0, TOP_SCREEN_WIDTH, TOP_SCREEN_HEIGHT,
                              C2D_Color32(0, 0, 0, OUTER_A));

            /* Larger faint circle to soften the dim->dark transition */
            C2D_DrawCircleSolid(cx, cy, 0, dim_r + 18.0f,
                                C2D_Color32(0, 0, 0, OUTER_A - MID_A));
            /* Medium circle: heavy dim zone */
            C2D_DrawCircleSolid(cx, cy, 0, dim_r,
                                C2D_Color32(0, 0, 0, MID_A - 80));
            /* Smaller circle: lighter dim closer to player */
            C2D_DrawCircleSolid(cx, cy, 0, dim_r - 30.0f,
                                C2D_Color32(0, 0, 0, 60));
            /* Innermost circle: fully clear around Isaac */
            C2D_DrawCircleSolid(cx, cy, 0, clear_r,
                                C2D_Color32(0, 0, 0, 0));

            /* Subtle warm flicker so the player feels like they're holding
             * a candle - tiny radius pulse from the player */
            float flicker = sinf((float)g->frame * 0.25f) * 3.0f;
            C2D_DrawCircleSolid(cx, cy, 0, clear_r * 0.75f + flicker,
                                C2D_Color32(255, 200, 100, 12));
        }

        /* Reset view for HUD (no shake on HUD) */
        if (sox != 0 || soy != 0) {
            C2D_ViewReset();
        }
        render_hud(g, textBuf);

        /* Curse banner display for first ~3 seconds of floor */
        if (g->curse_display_timer > 0 && g->active_curse != CURSE_NONE) {
            int t = g->curse_display_timer;
            float alpha_f = 1.0f;
            if (t > 150) alpha_f = (180 - t) / 30.0f;
            else if (t < 30) alpha_f = t / 30.0f;
            if (alpha_f < 0) alpha_f = 0;
            if (alpha_f > 1) alpha_f = 1;
            int alpha = (int)(alpha_f * 230);
            char banner[64];
            /* curse_name() already returns the full string "CURSE OF ..." so
             * just copy it directly to avoid the "CURSE OF THE CURSE OF" bug. */
            snprintf(banner, sizeof(banner), "%s",
                     curse_name(g->active_curse));
            C2D_Text ctext;
            C2D_TextParse(&ctext, textBuf, banner);
            C2D_TextOptimize(&ctext);
            /* Background panel */
            C2D_DrawRectSolid(50, 60, 0, 300, 30,
                              C2D_Color32(40, 0, 0, alpha));
            C2D_DrawRectSolid(50, 60, 0, 300, 2,
                              C2D_Color32(200, 40, 40, alpha));
            C2D_DrawRectSolid(50, 88, 0, 300, 2,
                              C2D_Color32(200, 40, 40, alpha));
            C2D_DrawText(&ctext, C2D_WithColor, 80, 68, 0, 0.6f, 0.6f,
                         C2D_Color32(255, 200, 200, alpha));
        }

        /* Room fade-in overlay */
        if (g->room_fade > 0) {
            int alpha = (g->room_fade * 16);
            if (alpha > 200) alpha = 200;
            C2D_DrawRectSolid(0, 0, 0, TOP_SCREEN_WIDTH, TOP_SCREEN_HEIGHT,
                              C2D_Color32(0, 0, 0, alpha));
        }

        /* Boss intro overlay: dimmed screen + "BOSS" text */
        if (g->boss_intro_timer > 0) {
            int alpha = 100;
            if (g->boss_intro_timer > BOSS_INTRO_FRAMES - 15) {
                alpha = (BOSS_INTRO_FRAMES - g->boss_intro_timer) * 8;
            } else if (g->boss_intro_timer < 15) {
                alpha = g->boss_intro_timer * 7;
            }
            if (alpha < 0) alpha = 0;
            if (alpha > 140) alpha = 140;
            C2D_DrawRectSolid(0, 0, 0, TOP_SCREEN_WIDTH, TOP_SCREEN_HEIGHT,
                              C2D_Color32(80, 0, 0, alpha));

            /* BOSS text */
            C2D_Text bossText;
            C2D_TextParse(&bossText, textBuf, "! BOSS !");
            C2D_TextOptimize(&bossText);
            float pulseScale = 1.0f + sinf((float)g->frame * 0.3f) * 0.1f;
            C2D_DrawText(&bossText, C2D_WithColor, 130, 90, 0,
                         pulseScale, pulseScale,
                         C2D_Color32(255, 60, 60, 240));
        }

        /* Pickup flash overlay */
        if (g->pickup_flash > 0) {
            const ItemDef *itemDef = get_item_def(g->last_pickup);
            if (itemDef) {
                int alpha = g->pickup_flash * 6;
                if (alpha > 255) alpha = 255;
                float progress = (float)(PICKUP_FLASH_FRAMES - g->pickup_flash) / (float)PICKUP_FLASH_FRAMES;

                /* Item sprite rising and fading */
                if (g_sprites_loaded) {
                    int pidx = item_sprite_idx(g->last_pickup);
                    float sprX = TOP_SCREEN_WIDTH / 2.0f;
                    float sprY = 50.0f - progress * 12.0f;
                    float sprScale = 1.8f - progress * 0.4f;
                    /* Glow behind sprite */
                    u32 flashGlow = C2D_Color32(255, 255, 200, alpha / 2);
                    C2D_DrawCircleSolid(sprX, sprY, 0, 20 * sprScale, flashGlow);
                    spr_draw_alpha(sheet_ui_items, pidx, sprX, sprY,
                                   sprScale, sprScale, (float)alpha / 255.0f);
                }

                /* Item name text */
                C2D_Text pickedText;
                char pbuf[64];
                snprintf(pbuf, sizeof(pbuf), "+ %s", itemDef->name);
                C2D_TextParse(&pickedText, textBuf, pbuf);
                C2D_TextOptimize(&pickedText);
                float yPos = 70.0f - progress * 8.0f;
                C2D_DrawText(&pickedText, C2D_WithColor, 110, yPos, 0,
                             0.55f, 0.55f,
                             C2D_Color32(255, 230, 100, alpha));
            }
        }

        /* Room cleared indicator */
        if (current_room(g)->cleared && current_room(g)->type != ROOM_START) {
            C2D_Text cleared;
            if (current_room(g)->has_trapdoor) {
                C2D_TextParse(&cleared, textBuf, "TRAPDOOR OPEN!");
                C2D_TextOptimize(&cleared);
                C2D_DrawText(&cleared, C2D_WithColor, 140, ROOM_BOTTOM - 18, 0,
                             0.4f, 0.4f, C2D_Color32(255, 200, 50, 200));
            } else {
                C2D_TextParse(&cleared, textBuf, "CLEARED");
                C2D_TextOptimize(&cleared);
                C2D_DrawText(&cleared, C2D_WithColor, 165, ROOM_BOTTOM - 18, 0,
                             0.4f, 0.4f, C2D_Color32(100, 255, 100, 180));
            }
        }

        /* Show item name briefly when pedestal exists (hidden if Blind) */
        if (current_room(g)->pedestal.active && g->active_curse != CURSE_BLIND) {
            const ItemDef *def = get_item_def(current_room(g)->pedestal.item);
            if (def) {
                C2D_Text itemName;
                C2D_TextParse(&itemName, textBuf, def->name);
                C2D_TextOptimize(&itemName);
                C2D_DrawText(&itemName, C2D_WithColor,
                            current_room(g)->pedestal.x - 30,
                            current_room(g)->pedestal.y + 16, 0,
                            0.35f, 0.35f, C2D_Color32(255, 255, 200, 200));
            }
        } else if (current_room(g)->pedestal.active && g->active_curse == CURSE_BLIND) {
            C2D_Text qm;
            C2D_TextParse(&qm, textBuf, "???");
            C2D_TextOptimize(&qm);
            C2D_DrawText(&qm, C2D_WithColor,
                        current_room(g)->pedestal.x - 8,
                        current_room(g)->pedestal.y + 16, 0,
                        0.4f, 0.4f, C2D_Color32(200, 100, 100, 200));
        }

        /* Pickup description overlay */
        render_pickup_message(g, textBuf);
        break;
    }

    case STATE_PAUSED: {
        /* Render game underneath dimmed */
        render_room(g);
        render_tears(g);
        render_enemy_shots(g);
        render_blood_particles(g);
        render_enemies(g);
        render_player(g);
        render_hud(g, textBuf);
        /* Dim overlay */
        C2D_DrawRectSolid(0, 0, 0, TOP_SCREEN_WIDTH, TOP_SCREEN_HEIGHT,
                          C2D_Color32(0, 0, 0, 140));
        /* Paused label */
        C2D_Text pausedText;
        C2D_TextParse(&pausedText, textBuf, "PAUSED");
        C2D_TextOptimize(&pausedText);
        C2D_DrawText(&pausedText, C2D_WithColor, 145, 95, 0, 1.1f, 1.1f,
                     C2D_Color32(255, 230, 180, 255));
        C2D_Text resumeText;
        C2D_TextParse(&resumeText, textBuf, "Press START or B to resume");
        C2D_TextOptimize(&resumeText);
        C2D_DrawText(&resumeText, C2D_WithColor, 95, 130, 0, 0.5f, 0.5f,
                     C2D_Color32(200, 180, 150, 255));
        break;
    }

    case STATE_FLOOR_TRANSITION:
        render_floor_transition(g, textBuf);
        break;

    case STATE_GAMEOVER:
        render_gameover(g, textBuf);
        break;

    case STATE_WIN:
        render_win(g, textBuf);
        break;
    }
}

/* ================================================================
 * Bottom screen - Minimap + stats
 * ================================================================ */

void render_minimap(Game *g, C2D_TextBuf textBuf) {
    C2D_TextBufClear(textBuf);

    /* For the main menu / settings / controls screens we draw a matching
       beige parchment bottom screen with subtitle text. */
    if (g->state == STATE_MENU || g->state == STATE_CONTROLS || g->state == STATE_SETTINGS) {
        /* Parchment background to match top screen */
        u32 bg_col      = C2D_Color32(228, 213, 192, 255);
        u32 text_col    = C2D_Color32(50,  35,  25, 255);
        u32 text_dim    = C2D_Color32(120, 95,  70, 255);
        u32 doodle_col  = C2D_Color32(160, 140, 120, 200);
        C2D_DrawRectSolid(0, 0, 0, BOT_SCREEN_WIDTH, BOT_SCREEN_HEIGHT, bg_col);

        /* Subtle paper grain */
        for (int i = 0; i < 30; i++) {
            float gx = (i * 47) % BOT_SCREEN_WIDTH;
            float gy = (i * 31) % BOT_SCREEN_HEIGHT;
            C2D_DrawRectSolid(gx, gy, 0, 1, 1, C2D_Color32(205, 188, 165, 120));
        }

        /* Decorative scratches at edges */
        draw_doodle_scratch(15.0f,  40.0f, 45.0f, 30.0f, doodle_col);
        draw_doodle_scratch(BOT_SCREEN_WIDTH - 50.0f, 35.0f,
                            BOT_SCREEN_WIDTH - 20.0f, 50.0f, doodle_col);
        draw_doodle_scratch(20.0f, 195.0f, 50.0f, 205.0f, doodle_col);
        draw_doodle_scratch(BOT_SCREEN_WIDTH - 55.0f, 200.0f,
                            BOT_SCREEN_WIDTH - 25.0f, 195.0f, doodle_col);

        /* Tiny bug doodles in corners */
        draw_doodle_bug(20.0f, 215.0f, doodle_col);
        draw_doodle_bug(BOT_SCREEN_WIDTH - 20.0f, 220.0f, doodle_col);

        C2D_Text t1, t2, t3, t4, t5;
        C2D_TextParse(&t1, textBuf, "The Binding of Isaac");
        C2D_TextOptimize(&t1);
        C2D_DrawText(&t1, C2D_WithColor, 60, 30, 0, 0.7f, 0.7f, text_col);

        C2D_Text sub;
        C2D_TextParse(&sub, textBuf, "REBIRTH  -  3DS Edition");
        C2D_TextOptimize(&sub);
        C2D_DrawText(&sub, C2D_WithColor, 75, 60, 0, 0.5f, 0.5f, text_dim);

        /* Decorative divider line under subtitle */
        C2D_DrawRectSolid(60, 82, 0, BOT_SCREEN_WIDTH - 120, 1, text_dim);

        char floorIntro[48];
        snprintf(floorIntro, sizeof(floorIntro), "%d floors of darkness await...", MAX_FLOORS);
        C2D_TextParse(&t2, textBuf, floorIntro);
        C2D_TextOptimize(&t2);
        C2D_DrawText(&t2, C2D_WithColor, 50, 100, 0, 0.45f, 0.45f, text_col);

        C2D_TextParse(&t3, textBuf, "Collect items to grow stronger.");
        C2D_TextOptimize(&t3);
        C2D_DrawText(&t3, C2D_WithColor, 50, 122, 0, 0.45f, 0.45f, text_col);

        C2D_TextParse(&t4, textBuf, "Defeat bosses to descend.");
        C2D_TextOptimize(&t4);
        C2D_DrawText(&t4, C2D_WithColor, 50, 144, 0, 0.45f, 0.45f, text_col);

        C2D_TextParse(&t5, textBuf, "Defeat Mega Satan to win!");
        C2D_TextOptimize(&t5);
        C2D_DrawText(&t5, C2D_WithColor, 50, 166, 0, 0.45f, 0.45f, text_col);

        /* Decorative divider near bottom */
        C2D_DrawRectSolid(80, 188, 0, BOT_SCREEN_WIDTH - 160, 1, text_dim);
        return;
    }

    /* In-game / paused etc: dark background for the minimap */
    u32 bgCol = C2D_Color32(25, 25, 25, 255);
    C2D_DrawRectSolid(0, 0, 0, BOT_SCREEN_WIDTH, BOT_SCREEN_HEIGHT, bgCol);

    if (g->state == STATE_FLOOR_TRANSITION) {
        C2D_Text info;
        C2D_TextParse(&info, textBuf, "Descending...");
        C2D_TextOptimize(&info);
        C2D_DrawText(&info, C2D_WithColor, 100, 110, 0, 0.6f, 0.6f,
                     C2D_Color32(200, 200, 200, 255));
        return;
    }

    if (g->state != STATE_PLAYING) {
        C2D_Text info;
        C2D_TextParse(&info, textBuf, "Press START to continue");
        C2D_TextOptimize(&info);
        C2D_DrawText(&info, C2D_WithColor, 60, 110, 0, 0.55f, 0.55f,
                     C2D_Color32(180, 180, 180, 255));
        return;
    }

    /* Curse of the Lost: hide the minimap entirely.
       Show only a faint, atmospheric notice instead of a placeholder. */
    if (g->active_curse == CURSE_LOST) {
        C2D_Text lost1, lost2;
        /* faint scrambled overlay */
        C2D_DrawRectSolid(0, 18, 0, BOT_SCREEN_WIDTH, 222,
                          C2D_Color32(8, 0, 8, 255));
        C2D_TextParse(&lost1, textBuf, "Map Hidden");
        C2D_TextOptimize(&lost1);
        C2D_DrawText(&lost1, C2D_WithColor, 100, 90, 0, 0.7f, 0.7f,
                     C2D_Color32(120, 30, 30, 220));
        C2D_TextParse(&lost2, textBuf, "(Curse of the Lost)");
        C2D_TextOptimize(&lost2);
        C2D_DrawText(&lost2, C2D_WithColor, 80, 130, 0, 0.5f, 0.5f,
                     C2D_Color32(140, 100, 100, 200));
        return;
    }

    /* === Minimap === */
    Dungeon *d = &g->dungeon;

    C2D_Text mapTitle;
    const FloorInfo *fi = get_floor_info(g->current_floor);
    char titleBuf[48];
    snprintf(titleBuf, sizeof(titleBuf), "= %s =", fi->name);
    C2D_TextParse(&mapTitle, textBuf, titleBuf);
    C2D_TextOptimize(&mapTitle);
    C2D_DrawText(&mapTitle, C2D_WithColor, 105, 3, 0, 0.5f, 0.5f,
                 C2D_Color32(200, 200, 200, 255));

    float cellW = 26;
    float cellH = 18;
    float mapOffX = (BOT_SCREEN_WIDTH - DUNGEON_W * cellW) / 2;
    float mapOffY = 20;

    for (int ry = 0; ry < DUNGEON_H; ry++) {
        for (int rx = 0; rx < DUNGEON_W; rx++) {
            Room *rm = &d->rooms[ry][rx];
            if (rm->type == ROOM_NONE) continue;

            float x = mapOffX + rx * cellW;
            float y = mapOffY + ry * cellH;
            int isCurrent = (rx == d->cur_x && ry == d->cur_y);

            if (!rm->visited) {
                /* Show unvisited rooms as dim outlines if adjacent to a visited room */
                int adjVis = 0;
                if (ry > 0 && d->rooms[ry-1][rx].visited && d->rooms[ry-1][rx].doors[1]) adjVis = 1;
                if (ry < DUNGEON_H-1 && d->rooms[ry+1][rx].visited && d->rooms[ry+1][rx].doors[0]) adjVis = 1;
                if (rx > 0 && d->rooms[ry][rx-1].visited && d->rooms[ry][rx-1].doors[3]) adjVis = 1;
                if (rx < DUNGEON_W-1 && d->rooms[ry][rx+1].visited && d->rooms[ry][rx+1].doors[2]) adjVis = 1;

                /* Secret rooms only show if revealed */
                if (rm->type == ROOM_SECRET && !rm->secret_revealed) adjVis = 0;

                if (adjVis) {
                    C2D_DrawRectSolid(x + 1, y + 1, 0, cellW - 2, cellH - 2,
                                     C2D_Color32(45, 45, 45, 200));
                    /* Question mark for unknown rooms */
                    C2D_DrawRectSolid(x + cellW/2, y + cellH/2 - 3, 0, 2, 4,
                                     C2D_Color32(80, 80, 80, 200));
                    C2D_DrawRectSolid(x + cellW/2, y + cellH/2 + 3, 0, 2, 2,
                                     C2D_Color32(80, 80, 80, 200));
                }
                continue;
            }

            u32 roomCol;
            if (isCurrent) {
                /* Pulsing white for current room */
                float pulse = sinf((float)g->frame * 0.12f) * 30.0f;
                int v = 225 + (int)pulse;
                roomCol = C2D_Color32(v, v, v, 255);
            } else {
                switch (rm->type) {
                    case ROOM_START:    roomCol = C2D_Color32(100, 200, 100, 255); break;
                    case ROOM_BOSS:     roomCol = C2D_Color32(255, 60, 60, 255); break;
                    case ROOM_TREASURE: roomCol = C2D_Color32(255, 215, 0, 255); break;
                    case ROOM_SHOP:     roomCol = C2D_Color32(100, 180, 255, 255); break;
                    case ROOM_SECRET:   roomCol = C2D_Color32(160, 160, 160, 255); break;
                    case ROOM_CURSE:    roomCol = C2D_Color32(180, 60, 180, 255); break;
                    default:
                        roomCol = rm->cleared
                                ? C2D_Color32(100, 100, 100, 255)
                                : C2D_Color32(120, 120, 120, 255);
                        break;
                }
            }

            C2D_DrawRectSolid(x + 1, y + 1, 0, cellW - 2, cellH - 2, roomCol);

            /* Room type icons (larger than top-screen minimap) */
            float icx = x + cellW / 2;
            float icy = y + cellH / 2;

            if (rm->type == ROOM_BOSS && !isCurrent) {
                /* Skull: small rectangle */
                C2D_DrawRectSolid(icx - 2, icy - 2, 0, 5, 4, C2D_Color32(255, 200, 200, 255));
            } else if (rm->type == ROOM_TREASURE && !isCurrent) {
                /* Cross/star sparkle */
                C2D_DrawRectSolid(icx, icy - 2, 0, 1, 5, C2D_Color32(255, 255, 150, 255));
                C2D_DrawRectSolid(icx - 2, icy, 0, 5, 1, C2D_Color32(255, 255, 150, 255));
            } else if (rm->type == ROOM_SHOP && !isCurrent) {
                /* $ line */
                C2D_DrawRectSolid(icx, icy - 2, 0, 1, 5, C2D_Color32(255, 255, 200, 255));
                C2D_DrawRectSolid(icx - 1, icy - 1, 0, 3, 1, C2D_Color32(255, 255, 200, 255));
                C2D_DrawRectSolid(icx - 1, icy + 1, 0, 3, 1, C2D_Color32(255, 255, 200, 255));
            } else if (rm->type == ROOM_SECRET && !isCurrent) {
                /* ? mark */
                C2D_DrawRectSolid(icx - 1, icy - 2, 0, 3, 3, C2D_Color32(220, 220, 220, 255));
                C2D_DrawRectSolid(icx, icy + 2, 0, 1, 2, C2D_Color32(220, 220, 220, 255));
            } else if (rm->type == ROOM_CURSE && !isCurrent) {
                /* X mark */
                C2D_DrawRectSolid(icx - 2, icy - 2, 0, 2, 2, C2D_Color32(255, 200, 255, 255));
                C2D_DrawRectSolid(icx + 1, icy - 2, 0, 2, 2, C2D_Color32(255, 200, 255, 255));
                C2D_DrawRectSolid(icx - 1, icy - 1, 0, 3, 2, C2D_Color32(255, 200, 255, 255));
                C2D_DrawRectSolid(icx - 2, icy + 1, 0, 2, 2, C2D_Color32(255, 200, 255, 255));
                C2D_DrawRectSolid(icx + 1, icy + 1, 0, 2, 2, C2D_Color32(255, 200, 255, 255));
            } else if (rm->cleared && rm->type != ROOM_START && !isCurrent) {
                C2D_DrawRectSolid(icx - 2, icy - 2, 0, 4, 4, C2D_Color32(0, 255, 0, 200));
            }

            /* Current room pulsing outline */
            if (isCurrent) {
                float pulse = sinf((float)g->frame * 0.12f) * 0.4f + 0.6f;
                u8 a = (u8)(pulse * 255);
                u32 markerCol = C2D_Color32(255, 255, 255, a);
                C2D_DrawRectSolid(x, y, 0, cellW, 1, markerCol);
                C2D_DrawRectSolid(x, y + cellH - 1, 0, cellW, 1, markerCol);
                C2D_DrawRectSolid(x, y, 0, 1, cellH, markerCol);
                C2D_DrawRectSolid(x + cellW - 1, y, 0, 1, cellH, markerCol);
            }

            /* Door connections */
            u32 connCol = C2D_Color32(80, 80, 80, 255);
            if (rm->doors[0] && ry > 0 && d->rooms[ry - 1][rx].type != ROOM_NONE)
                C2D_DrawRectSolid(x + cellW / 2 - 2, y - 2, 0, 4, 3, connCol);
            if (rm->doors[1] && ry < DUNGEON_H - 1 && d->rooms[ry + 1][rx].type != ROOM_NONE)
                C2D_DrawRectSolid(x + cellW / 2 - 2, y + cellH - 1, 0, 4, 3, connCol);
            if (rm->doors[2] && rx > 0 && d->rooms[ry][rx - 1].type != ROOM_NONE)
                C2D_DrawRectSolid(x - 2, y + cellH / 2 - 2, 0, 3, 4, connCol);
            if (rm->doors[3] && rx < DUNGEON_W - 1 && d->rooms[ry][rx + 1].type != ROOM_NONE)
                C2D_DrawRectSolid(x + cellW - 1, y + cellH / 2 - 2, 0, 3, 4, connCol);
        }
    }

    /* Room legend */
    {
        float lx = mapOffX;
        float ly = mapOffY + DUNGEON_H * cellH + 2;
        float ls = 6.0f;
        float lspacing = 50.0f;

        struct { u32 col; const char *label; } legend[] = {
            { C2D_Color32(100, 200, 100, 255), "Start" },
            { C2D_Color32(255, 60, 60, 255),   "Boss" },
            { C2D_Color32(255, 215, 0, 255),   "Item" },
            { C2D_Color32(100, 180, 255, 255), "Shop" },
            { C2D_Color32(180, 60, 180, 255),  "Curse" },
            { C2D_Color32(160, 160, 160, 255), "Secret" },
        };
        int legendCount = 6;

        for (int li = 0; li < legendCount; li++) {
            float llx = lx + li * lspacing;
            if (llx + lspacing > BOT_SCREEN_WIDTH) break;
            C2D_DrawRectSolid(llx, ly + 1, 0, ls, ls, legend[li].col);
            C2D_Text lt;
            C2D_TextParse(&lt, textBuf, legend[li].label);
            C2D_TextOptimize(&lt);
            C2D_DrawText(&lt, C2D_WithColor, llx + ls + 2, ly, 0, 0.3f, 0.3f,
                         C2D_Color32(160, 160, 160, 255));
        }
    }

    /* Player stats section */
    float statsY = mapOffY + DUNGEON_H * cellH + 16;
    C2D_Text statsTitle;
    C2D_TextParse(&statsTitle, textBuf, "STATS");
    C2D_TextOptimize(&statsTitle);
    C2D_DrawText(&statsTitle, C2D_WithColor, 10, statsY, 0, 0.4f, 0.4f,
                C2D_Color32(200, 200, 200, 255));

    /* Stat bars */
    Player *p = &g->player;
    statsY += 14;
    float barMaxW = 50.0f;
    float statX = 10;

    /* DMG */
    {
        C2D_Text lbl;
        C2D_TextParse(&lbl, textBuf, "DMG");
        C2D_TextOptimize(&lbl);
        C2D_DrawText(&lbl, C2D_WithColor, statX, statsY, 0, 0.35f, 0.35f,
                    C2D_Color32(255, 100, 100, 255));
        float pct = p->stats.damage / 5.0f;
        if (pct > 1) pct = 1;
        C2D_DrawRectSolid(statX + 28, statsY + 2, 0, barMaxW, 6,
                         C2D_Color32(60, 60, 60, 255));
        C2D_DrawRectSolid(statX + 28, statsY + 2, 0, barMaxW * pct, 6,
                         C2D_Color32(255, 80, 80, 255));
    }

    /* SPD */
    {
        C2D_Text lbl;
        C2D_TextParse(&lbl, textBuf, "SPD");
        C2D_TextOptimize(&lbl);
        C2D_DrawText(&lbl, C2D_WithColor, statX + 90, statsY, 0, 0.35f, 0.35f,
                    C2D_Color32(100, 255, 100, 255));
        float pct = p->stats.speed / 4.0f;
        if (pct > 1) pct = 1;
        C2D_DrawRectSolid(statX + 118, statsY + 2, 0, barMaxW, 6,
                         C2D_Color32(60, 60, 60, 255));
        C2D_DrawRectSolid(statX + 118, statsY + 2, 0, barMaxW * pct, 6,
                         C2D_Color32(80, 255, 80, 255));
    }

    /* RATE */
    {
        C2D_Text lbl;
        C2D_TextParse(&lbl, textBuf, "RATE");
        C2D_TextOptimize(&lbl);
        C2D_DrawText(&lbl, C2D_WithColor, statX + 180, statsY, 0, 0.35f, 0.35f,
                    C2D_Color32(100, 100, 255, 255));
        float pct = (p->stats.fire_rate + 2.0f) / 6.0f;  /* normalize */
        if (pct > 1) pct = 1;
        if (pct < 0) pct = 0;
        C2D_DrawRectSolid(statX + 212, statsY + 2, 0, barMaxW, 6,
                         C2D_Color32(60, 60, 60, 255));
        C2D_DrawRectSolid(statX + 212, statsY + 2, 0, barMaxW * pct, 6,
                         C2D_Color32(80, 80, 255, 255));
    }

    statsY += 14;

    /* RANGE */
    {
        C2D_Text lbl;
        C2D_TextParse(&lbl, textBuf, "RNG");
        C2D_TextOptimize(&lbl);
        C2D_DrawText(&lbl, C2D_WithColor, statX, statsY, 0, 0.35f, 0.35f,
                    C2D_Color32(255, 200, 100, 255));
        float pct = p->stats.range / 300.0f;
        if (pct > 1) pct = 1;
        C2D_DrawRectSolid(statX + 28, statsY + 2, 0, barMaxW, 6,
                         C2D_Color32(60, 60, 60, 255));
        C2D_DrawRectSolid(statX + 28, statsY + 2, 0, barMaxW * pct, 6,
                         C2D_Color32(255, 200, 80, 255));
    }

    /* Last item collected */
    if (p->item_count > 0) {
        const ItemDef *last = get_item_def(p->items[p->item_count - 1]);
        if (last) {
            C2D_Text itemLbl;
            char ibuf[48];
            snprintf(ibuf, sizeof(ibuf), "Last: %s", last->name);
            C2D_TextParse(&itemLbl, textBuf, ibuf);
            C2D_TextOptimize(&itemLbl);
            C2D_DrawText(&itemLbl, C2D_WithColor, statX + 90, statsY, 0, 0.35f, 0.35f,
                        C2D_Color32(255, 215, 0, 255));
        }
    }

    /* Controls reminder */
    statsY += 16;
    C2D_Text ctrl;
    C2D_TextParse(&ctrl, textBuf, "ABXY: Shoot | D-Pad: Move | SELECT: Bomb");
    C2D_TextOptimize(&ctrl);
    C2D_DrawText(&ctrl, C2D_WithColor, 15, statsY, 0, 0.35f, 0.35f,
                 C2D_Color32(100, 100, 100, 255));
}

/* ================================================================
 * Pill / Tarot / Champion / Creep helpers
 * ================================================================ */

static const char *pill_color_names[PILL_EFFECT_COUNT] = {
    "Red Pill", "Blue Pill", "Yellow Pill", "Green Pill",
    "Orange Pill", "Pink Pill", "White Pill", "Black Pill",
    "Purple Pill", "Cyan Pill"
};

const char *pill_color_name(int color_idx) {
    if (color_idx < 0 || color_idx >= PILL_EFFECT_COUNT) return "?";
    return pill_color_names[color_idx];
}

static const char *pill_effect_text[PILL_EFFECT_COUNT] = {
    "Health Up", "Health Down", "Speed Up", "Speed Down",
    "Tears Up", "Tears Down", "Range Up", "Luck Up",
    "Full Health", "Telepills"
};

const char *pill_name(PillEffect e, int known) {
    if (e < 0 || e >= PILL_EFFECT_COUNT) return "?";
    if (known) return pill_effect_text[e];
    /* Lookup color */
    return "Unknown Pill";
}

static const char *tarot_names[TAROT_COUNT] = {
    "The Fool", "The Magician", "The High Priestess", "The Emperor",
    "The Hierophant", "The Lovers", "The Tower", "The World"
};
const char *tarot_name(TarotCard c) {
    if (c < 0 || c >= TAROT_COUNT) return "?";
    return tarot_names[c];
}

const char *champion_name(ChampionType c) {
    switch (c) {
        case CHAMP_RED:    return "Red";
        case CHAMP_BLUE:   return "Blue";
        case CHAMP_YELLOW: return "Yellow";
        case CHAMP_BLACK:  return "Black";
        case CHAMP_NONE:
        default:           return "";
    }
}

/* Drop champion reward (heart based on color) */
void enemy_drop_champion_reward(Game *g, Enemy *e) {
    Room *r = current_room(g);
    if (!r) return;
    switch (e->champion) {
        case CHAMP_RED:
            spawn_heart(r, e->x, e->y, HEART_RED_FULL);
            break;
        case CHAMP_BLUE:
            spawn_heart(r, e->x, e->y, HEART_SOUL);
            break;
        case CHAMP_YELLOW:
            spawn_consumable(r, e->x, e->y, PICKUP_COIN5);
            break;
        case CHAMP_BLACK:
            spawn_consumable(r, e->x, e->y, PICKUP_BOMB2);
            break;
        case CHAMP_NONE:
        default:
            break;
    }
}

/* Spawn a creep tile at position */
void spawn_creep(Game *g, float x, float y, int dmg, int frames) {
    for (int i = 0; i < MAX_CREEP; i++) {
        if (!g->creep[i].active) {
            g->creep[i].x = x;
            g->creep[i].y = y;
            g->creep[i].radius = 14.0f;
            g->creep[i].timer = frames;
            g->creep[i].dmg = dmg;
            g->creep[i].active = 1;
            return;
        }
    }
}

/* Update creep: decrement timers, damage player if on creep */
void creep_update(Game *g) {
    Player *p = &g->player;
    for (int i = 0; i < MAX_CREEP; i++) {
        if (!g->creep[i].active) continue;
        g->creep[i].timer--;
        if (g->creep[i].timer <= 0) {
            g->creep[i].active = 0;
            continue;
        }
        /* Damage player if standing on creep (and not invul) */
        if (p->iframes == 0) {
            float dx = p->x - g->creep[i].x;
            float dy = p->y - g->creep[i].y;
            float r2 = dx * dx + dy * dy;
            float lim = g->creep[i].radius + PLAYER_SIZE / 2.0f;
            if (r2 < lim * lim) {
                if (p->holy_mantle_active) {
                    p->holy_mantle_active = 0;
                } else {
                    p->hp -= g->creep[i].dmg;
                    p->iframes = PLAYER_IFRAMES;
                    audio_play(SFX_HURT);
                    trigger_shake(g, 2.0f, 8);
                    if (p->hp <= 0) {
                        if (p->lives > 0) {
                            p->lives--;
                            p->hp = p->stats.max_hp;
                        } else {
                            g->state = STATE_GAMEOVER;
                            audio_play(SFX_PLAYER_DEATH);
                        }
                    }
                }
            }
        }
    }
}

/* Helper: handle the per-room state cleanup performed when warping the player
 * to a different room without using a normal door transition (used by
 * PILL_TELEPILLS, TAROT_FOOL, TAROT_EMPEROR).  Must be called AFTER cur_x/cur_y
 * have been updated to the destination room. */
static void do_warp_cleanup(Game *g) {
    /* Mark room as visited and spawn enemies if first visit */
    Room *nr = current_room(g);
    if (!nr) return;
    nr->visited = 1;
    if (!nr->enemies_spawned) room_spawn_enemies(g, nr);

    /* Clear projectiles, creep and reset Holy Mantle */
    for (int i = 0; i < MAX_TEARS; i++)        g->tears[i].active = 0;
    for (int i = 0; i < MAX_ENEMY_SHOTS; i++)  g->enemy_shots[i].active = 0;
    for (int i = 0; i < MAX_CREEP; i++)        g->creep[i].active = 0;

    if (g->player.stats.flags & ITEM_FLAG_MANTLE) g->player.holy_mantle_active = 1;

    /* Center player and brief fade */
    g->player.x = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
    g->player.y = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
    g->player.iframes = 30;
    g->room_fade = 16;

    /* Trigger boss intro/music if we landed on an uncleared boss room */
    if (nr->type == ROOM_BOSS && !nr->cleared) {
        audio_play(SFX_BOSS);
        g->boss_intro_timer = BOSS_INTRO_FRAMES;
        trigger_shake(g, 4.0f, 30);
        g->boss_active = 1;
        music_play(MUS_BOSS);
        /* Grant a generous grace window: intro frames + ~1s post-intro
           invulnerability (iframes only tick down during gameplay). */
        g->player.iframes = BOSS_INTRO_FRAMES + 60;
        for (int bi = 0; bi < nr->enemy_count; bi++) {
            if (nr->enemies[bi].active && is_boss_type(nr->enemies[bi].type)) {
                g->boss_name = boss_name_str(nr->enemies[bi].type);
                break;
            }
        }
    }
    audio_play(SFX_DOOR);
}

/* Apply pill effect to player */
void apply_pill_effect(Game *g, PillEffect e) {
    Player *p = &g->player;
    /* Identify the pill */
    if (e >= 0 && e < PILL_EFFECT_COUNT) g->pill_known[e] = 1;

    switch (e) {
        case PILL_HEALTH_UP:
            p->pill_max_hp_bonus += 2;
            recalc_player_stats(p);
            p->hp += 2;
            if (p->hp > p->stats.max_hp) p->hp = p->stats.max_hp;
            break;
        case PILL_HEALTH_DOWN:
            p->pill_max_hp_bonus -= 2;
            recalc_player_stats(p);
            if (p->hp > p->stats.max_hp) p->hp = p->stats.max_hp;
            break;
        case PILL_SPEED_UP:
            p->pill_speed_bonus += 0.3f;
            recalc_player_stats(p);
            break;
        case PILL_SPEED_DOWN:
            p->pill_speed_bonus -= 0.3f;
            recalc_player_stats(p);
            break;
        case PILL_TEARS_UP:
            p->pill_fire_rate_bonus += 0.5f;
            recalc_player_stats(p);
            break;
        case PILL_TEARS_DOWN:
            p->pill_fire_rate_bonus -= 0.5f;
            recalc_player_stats(p);
            break;
        case PILL_RANGE_UP:
            p->pill_range_bonus += 30.0f;
            recalc_player_stats(p);
            break;
        case PILL_LUCK_UP:
            p->pill_luck_bonus += 1.0f;
            recalc_player_stats(p);
            break;
        case PILL_FULL_HEALTH:
            p->hp = p->stats.max_hp;
            break;
        case PILL_TELEPILLS: {
            /* Teleport to random room */
            int tries = 0;
            while (tries < 30) {
                int rx = randi(0, DUNGEON_W - 1);
                int ry = randi(0, DUNGEON_H - 1);
                if (g->dungeon.rooms[ry][rx].type != ROOM_NONE) {
                    g->dungeon.cur_x = rx;
                    g->dungeon.cur_y = ry;
                    do_warp_cleanup(g);
                    break;
                }
                tries++;
            }
            break;
        }
        default: break;
    }
}

/* Apply tarot card effect */
void apply_tarot_card(Game *g, TarotCard c) {
    Player *p = &g->player;
    Room *r = current_room(g);
    switch (c) {
        case TAROT_FOOL:
            /* Teleport back to starting room */
            g->dungeon.cur_x = g->dungeon.start_x;
            g->dungeon.cur_y = g->dungeon.start_y;
            do_warp_cleanup(g);
            break;
        case TAROT_MAGICIAN:
            /* Homing tears for ~10 seconds */
            g->homing_timer = 600;
            break;
        case TAROT_PRIESTESS:
            if (r) spawn_heart(r, p->x + 12, p->y, HEART_RED_FULL);
            break;
        case TAROT_EMPEROR: {
            /* Warp to boss room */
            int bx = g->dungeon.boss_x;
            int by = g->dungeon.boss_y;
            if (bx >= 0 && bx < DUNGEON_W && by >= 0 && by < DUNGEON_H &&
                g->dungeon.rooms[by][bx].type != ROOM_NONE) {
                g->dungeon.cur_x = bx;
                g->dungeon.cur_y = by;
                do_warp_cleanup(g);
            }
            break;
        }
        case TAROT_HIEROPHANT:
            if (r) {
                spawn_heart(r, p->x + 12, p->y, HEART_SOUL);
                spawn_heart(r, p->x - 12, p->y, HEART_SOUL);
            }
            break;
        case TAROT_LOVERS:
            if (r) {
                spawn_heart(r, p->x + 12, p->y, HEART_RED_FULL);
                spawn_heart(r, p->x - 12, p->y, HEART_RED_FULL);
            }
            break;
        case TAROT_TOWER:
            /* Spawn 6 troll bombs around player (damages player too) */
            for (int i = 0; i < 6; i++) {
                float ang = (i / 6.0f) * 2.0f * 3.14159f;
                float bx = p->x + cosf(ang) * 40.0f;
                float by = p->y + sinf(ang) * 40.0f;
                if (r) spawn_consumable(r, bx, by, PICKUP_BOMB);
            }
            break;
        case TAROT_WORLD:
            g->map_revealed = 1;
            /* Mark all rooms as visible on minimap */
            for (int yy = 0; yy < DUNGEON_H; yy++) {
                for (int xx = 0; xx < DUNGEON_W; xx++) {
                    if (g->dungeon.rooms[yy][xx].type != ROOM_NONE) {
                        g->dungeon.rooms[yy][xx].visited = 1;
                    }
                }
            }
            break;
        default: break;
    }
}

void spawn_pill_pickup(Room *r, float x, float y, int effect) {
    if (effect < 0) effect = 0;
    if (effect >= PILL_EFFECT_COUNT) effect = PILL_EFFECT_COUNT - 1;
    for (int i = 0; i < MAX_CONSUMABLE_PICKUPS; i++) {
        if (!r->consumables[i].active) {
            ConsumablePickup *c = &r->consumables[i];
            c->x = x + randf(-6, 6);
            c->y = y + randf(-6, 6);
            c->type = PICKUP_PILL;
            c->sub_type = effect;
            c->active = 1;
            c->anim_timer = randi(0, 60);
            r->consumable_count++;
            return;
        }
    }
}

void spawn_card_pickup(Room *r, float x, float y, int card) {
    if (card < 0) card = 0;
    if (card >= TAROT_COUNT) card = TAROT_COUNT - 1;
    for (int i = 0; i < MAX_CONSUMABLE_PICKUPS; i++) {
        if (!r->consumables[i].active) {
            ConsumablePickup *c = &r->consumables[i];
            c->x = x + randf(-6, 6);
            c->y = y + randf(-6, 6);
            c->type = PICKUP_CARD;
            c->sub_type = card;
            c->active = 1;
            c->anim_timer = randi(0, 60);
            r->consumable_count++;
            return;
        }
    }
}

/* ================================================================
 * MAIN
 * ================================================================ */

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    /* Initialize romfs for loading sprite assets */
    romfsInit();

    /* Load user config from SD card (or use defaults) */
    config_init(&g_config);
    config_load(&g_config);  /* -1 = no file, defaults kept */

    /* Initialize audio system with multi-layer safety checks */
    audio_init(g_config.audio_enabled);
    audio_load_all();
    audio_set_volume(g_config.sfx_volume);
    music_set_volume(g_config.music_volume);

    C3D_RenderTarget *topTarget = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget *botTarget = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    C2D_TextBuf textBuf = C2D_TextBufNew(1024);

    srand((unsigned)time(NULL));

    init_colours();
    init_item_pool();

    /* Load sprite sheets from romfs */
    g_sprites_loaded = (sprites_init() == 0) ? 1 : 0;

    static Game game;  /* Must be static — struct too large for 3DS stack (~38KB vs 32KB limit) */
    game_init(&game);

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();
        circlePosition circlePos;
        hidCircleRead(&circlePos);

        /* SELECT places bombs during gameplay; quits only from menus / game-over screens.
         * Without this guard, the in-game "place_bomb" binding (KEY_SELECT) is dead code
         * because the application exits before game_update sees the press. */
        if ((kDown & KEY_SELECT)
            && game.state != STATE_PLAYING
            && game.state != STATE_PAUSED) break;
        if (game.player.hp == -999) break;

        game_update(&game, kDown, kHeld, circlePos);
        music_update();  /* refill streaming buffers & handle fades */

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        C2D_TargetClear(topTarget, COL_BG);
        C2D_SceneBegin(topTarget);
        game_render_top(&game, textBuf);

        C2D_TargetClear(botTarget, C2D_Color32(25, 25, 25, 255));
        C2D_SceneBegin(botTarget);
        if (game.state == STATE_PAUSED) {
            render_stats_overlay(&game, textBuf);
        } else {
            render_minimap(&game, textBuf);
        }

        C3D_FrameEnd(0);
    }

    /* Cleanup */
    audio_exit();
    sprites_free();
    C2D_TextBufDelete(textBuf);
    C2D_Fini();
    C3D_Fini();
    romfsExit();
    gfxExit();

    return 0;
}
