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

/* Optional custom bitmap font. NULL = use citro2d's built-in system font
 * (the original, always-working behavior). Loaded (best-effort) in main();
 * if loading fails for any reason it simply stays NULL and every text call
 * silently keeps using the default font — text can never disappear. */
static C2D_Font g_font = NULL;

/* Route text through g_font when available, else fall back to the default
 * system font exactly like the original C2D_TextParse call did. Safe to
 * call even if g_font is NULL (that's the normal/original code path). */
static inline void gtext_parse(C2D_Text *t, C2D_TextBuf b, const char *s) {
    if (g_font) {
        C2D_TextFontParse(t, g_font, b, s);
    } else {
        C2D_TextParse(t, b, s);
    }
}

/* Curse of the Labyrinth: set by roll_curse() before dungeon_generate() runs
 * (call order was adjusted so the curse for the upcoming floor is known
 * ahead of generation) so dungeon_generate can enlarge the room target
 * without needing a signature change. */
static int g_curse_labyrinth_pending = 0;

/* Early forward declarations needed before definitions appear later */
static int is_boss_type(EnemyType t);
static void spawn_heart(Room *r, float x, float y, HeartType type);
static void spawn_tear_pop(Game *g, float x, float y, int is_blood);
/* Familiar system (Phase E6) */
static void familiars_reset_trail(Game *g);
static void familiar_pos(Game *g, int slot, float *fx, float *fy);
/* Warp path (E7 Teleport! uses it from the active-item switch) */
static void do_warp_cleanup(Game *g);
/* R10 (C4): devil-deal purchases feed the Azazel unlock counter */
static int apply_unlock_gates(void);
/* R8 (M3): golden-door Mega Satan room (defined after drain_black_burst) */
static void open_mega_satan_room(Game *g);
/* Parameterized explosion core (bombs, Epic Fetus, Ipecac tears) */
static void bomb_explode_ex(Game *g, float bx, float by, float blast,
                            float enemy_dmg, float boss_dmg, int player_dmg);
/* R8 (M5): red chest opens from the pickup switch inside player_update,
 * which sits before these definitions */
static Enemy *alloc_dynamic_enemy(Room *r);
static int  spawn_troll_bomb(Game *g, float x, float y);
static void drain_black_burst(Game *g);
/* R8 (M8): blue flies are summoned from the pickup/active paths too */
static void spawn_blue_fly(Game *g, float x, float y);

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

/* Luck-driven boolean roll: base_pct improved by luck_weight per point of
 * p->stats.luck, clamped to [0,95] so nothing is ever guaranteed. */
static int luck_roll(Game *g, int base_pct, int luck_weight) {
    int pct = base_pct + (int)(luck_weight * g->player.stats.luck);
    if (pct < 0) pct = 0;
    if (pct > 95) pct = 95;
    return randi(0, 99) < pct;
}

/* ================================================================
 * Item definitions
 * ================================================================ */

static ItemDef item_pool[ITEM_COUNT];

/* ================================================================
 * Trinkets — one held at a time, passive effect while held
 * ================================================================ */

const char *trinket_name(int t) {
    switch (t) {
    case TRINKET_SWALLOWED_PENNY: return "Swallowed Penny";
    case TRINKET_PETRIFIED_POOP:  return "Petrified Poop";
    case TRINKET_CHILDS_HEART:    return "Child's Heart";
    case TRINKET_RUSTED_KEY:      return "Rusted Key";
    case TRINKET_MATCH_STICK:     return "Match Stick";
    case TRINKET_LUCKY_TOE:       return "Lucky Toe";
    case TRINKET_CRACKED_CROWN:   return "Cracked Crown";
    case TRINKET_CANCER:          return "Cancer";
    case TRINKET_TICK:            return "Tick";
    case TRINKET_BROKEN_MAGNET:   return "Broken Magnet";
    case TRINKET_UMBILICAL_CORD:  return "Umbilical Cord";
    case TRINKET_CURVED_HORN:     return "Curved Horn";
    default:                      return "???";
    }
}

/* Spawn a trinket pickup on the floor */
static void spawn_trinket_pickup(Room *r, float x, float y, int trinket) {
    for (int i = 0; i < MAX_CONSUMABLE_PICKUPS; i++) {
        if (!r->consumables[i].active) {
            ConsumablePickup *c = &r->consumables[i];
            c->x = clampf(x, ROOM_LEFT + 12, ROOM_RIGHT - 12);
            c->y = clampf(y, ROOM_TOP + 12, ROOM_BOTTOM - 12);
            c->type = PICKUP_TRINKET;
            c->sub_type = trinket;
            c->active = 1;
            c->anim_timer = 0;
            r->consumable_count++;
            return;
        }
    }
}

/* Tiny procedural charm glyph per trinket (world pickup + HUD slot) */
static void draw_trinket_icon(float x, float y, int t, float sc) {
    switch (t) {
    case TRINKET_SWALLOWED_PENNY:
        C2D_DrawCircleSolid(x, y, 0, 5 * sc, C2D_Color32(210, 170, 60, 255));
        C2D_DrawRectSolid(x - 2 * sc, y - 1 * sc, 0, 4 * sc, 2 * sc,
                          C2D_Color32(120, 90, 25, 255));
        break;
    case TRINKET_PETRIFIED_POOP:
        C2D_DrawEllipseSolid(x - 5 * sc, y - 1 * sc, 0, 10 * sc, 5 * sc,
                             C2D_Color32(130, 130, 135, 255));
        C2D_DrawEllipseSolid(x - 3 * sc, y - 5 * sc, 0, 6 * sc, 4 * sc,
                             C2D_Color32(160, 160, 165, 255));
        break;
    case TRINKET_CHILDS_HEART:
        C2D_DrawCircleSolid(x - 2 * sc, y - 2 * sc, 0, 3 * sc,
                            C2D_Color32(230, 110, 150, 255));
        C2D_DrawCircleSolid(x + 2 * sc, y - 2 * sc, 0, 3 * sc,
                            C2D_Color32(230, 110, 150, 255));
        C2D_DrawRectSolid(x - 4 * sc, y - 1 * sc, 0, 8 * sc, 5 * sc,
                          C2D_Color32(230, 110, 150, 255));
        break;
    case TRINKET_RUSTED_KEY:
        C2D_DrawCircleSolid(x, y - 3 * sc, 0, 3 * sc, C2D_Color32(170, 110, 50, 255));
        C2D_DrawCircleSolid(x, y - 3 * sc, 0, 1.5f * sc, C2D_Color32(60, 40, 20, 255));
        C2D_DrawRectSolid(x - 1 * sc, y - 1 * sc, 0, 2 * sc, 7 * sc,
                          C2D_Color32(170, 110, 50, 255));
        C2D_DrawRectSolid(x, y + 4 * sc, 0, 3 * sc, 2 * sc,
                          C2D_Color32(170, 110, 50, 255));
        break;
    case TRINKET_MATCH_STICK:
        C2D_DrawRectSolid(x - 1 * sc, y - 4 * sc, 0, 2 * sc, 10 * sc,
                          C2D_Color32(200, 170, 120, 255));
        C2D_DrawCircleSolid(x, y - 5 * sc, 0, 2.5f * sc, C2D_Color32(220, 60, 40, 255));
        break;
    case TRINKET_LUCKY_TOE:
        C2D_DrawEllipseSolid(x - 4 * sc, y - 5 * sc, 0, 8 * sc, 11 * sc,
                             C2D_Color32(235, 195, 170, 255));
        C2D_DrawEllipseSolid(x - 2.5f * sc, y - 5 * sc, 0, 5 * sc, 3.5f * sc,
                             C2D_Color32(255, 240, 230, 255));
        break;
    case TRINKET_CRACKED_CROWN:
        C2D_DrawRectSolid(x - 5 * sc, y - 1 * sc, 0, 10 * sc, 4 * sc,
                          C2D_Color32(215, 180, 60, 255));
        C2D_DrawRectSolid(x - 5 * sc, y - 5 * sc, 0, 2 * sc, 4 * sc,
                          C2D_Color32(215, 180, 60, 255));
        C2D_DrawRectSolid(x - 1 * sc, y - 6 * sc, 0, 2 * sc, 5 * sc,
                          C2D_Color32(215, 180, 60, 255));
        C2D_DrawRectSolid(x + 3 * sc, y - 5 * sc, 0, 2 * sc, 4 * sc,
                          C2D_Color32(215, 180, 60, 255));
        /* the crack */
        C2D_DrawRectSolid(x, y - 1 * sc, 0, 1, 4 * sc, C2D_Color32(90, 70, 20, 255));
        break;
    case TRINKET_CANCER:
        C2D_DrawCircleSolid(x, y, 0, 4.5f * sc, C2D_Color32(225, 225, 230, 255));
        C2D_DrawCircleSolid(x - 4 * sc, y - 3 * sc, 0, 1.6f * sc,
                            C2D_Color32(225, 225, 230, 255));
        C2D_DrawCircleSolid(x + 4 * sc, y - 3 * sc, 0, 1.6f * sc,
                            C2D_Color32(225, 225, 230, 255));
        break;
    case TRINKET_TICK:
        C2D_DrawCircleSolid(x, y, 0, 4.5f * sc, C2D_Color32(120, 40, 50, 255));
        C2D_DrawCircleSolid(x, y - 4.5f * sc, 0, 1.3f * sc, C2D_Color32(60, 20, 25, 255));
        break;
    case TRINKET_BROKEN_MAGNET:
        C2D_DrawRectSolid(x - 4 * sc, y - 5 * sc, 0, 3 * sc, 9 * sc,
                          C2D_Color32(190, 30, 30, 255));
        C2D_DrawRectSolid(x + 1 * sc, y - 5 * sc, 0, 3 * sc, 9 * sc,
                          C2D_Color32(200, 200, 200, 255));
        break;
    case TRINKET_UMBILICAL_CORD:
        C2D_DrawEllipseSolid(x - 3 * sc, y, 0, 6 * sc, 3 * sc,
                             C2D_Color32(220, 180, 170, 255));
        C2D_DrawEllipseSolid(x + 3 * sc, y - 2 * sc, 0, 5 * sc, 2.5f * sc,
                             C2D_Color32(220, 180, 170, 255));
        break;
    case TRINKET_CURVED_HORN:
        C2D_DrawRectSolid(x - 1 * sc, y - 5 * sc, 0, 2 * sc, 10 * sc,
                          C2D_Color32(230, 220, 200, 255));
        C2D_DrawCircleSolid(x - 1 * sc, y - 5 * sc, 0, 2.2f * sc,
                            C2D_Color32(230, 220, 200, 255));
        break;
    default: break;
    }
}

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
    DEF(ITEM_EPIC_FETUS,      "Epic Fetus",      1.8f,  0.0f, -2.0f,  0.5f,  0, ITEM_FLAG_BOMB_TEAR,
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
    DEF(ITEM_YUM_HEART,       "Yum Heart",       0.0f,  0.0f,  0.0f,  0.0f,  0, ITEM_FLAG_ACTIVE,
        "Active: heals one heart");
    DEF(ITEM_LUCKY_FOOT,      "Lucky Foot",      0.0f,  0.1f,  0.0f,  0.0f,  0, 0,
        "Luck up, better drops");
    DEF(ITEM_BOOK_OF_BELIAL,  "Book of Belial",  0.0f,  0.0f,  0.0f,  0.0f,  0, ITEM_FLAG_ACTIVE,
        "Active: +damage this room");
    /* --- Phase 3 passive items --- */
    DEF(ITEM_CRICKETS_HEAD,   "Cricket's Head",  1.0f,  0.0f,  0.0f,  0.0f,  0, 0,
        "Damage way up");
    DEF(ITEM_ODD_MUSHROOM,    "Odd Mushroom",    0.3f,  0.1f,  0.3f,  0.0f,  0, 0,
        "Tears + damage");
    DEF(ITEM_ROID_RAGE,       "Roid Rage",       0.0f,  0.6f,  0.0f,  0.0f,  0, 0,
        "Big speed up");
    DEF(ITEM_MARKED,          "Marked",          0.1f,  0.0f,  0.5f,  0.0f,  0, 0,
        "Fire rate up");
    DEF(ITEM_ANEMIC,          "Anemic",          0.0f,  0.0f,  0.0f,  0.5f,  0, ITEM_FLAG_PIERCING,
        "Piercing tears");
    DEF(ITEM_CAT_O_NINE,      "Cat o' Nine Tails",0.5f, 0.0f,  0.0f,  0.0f,  0, 0,
        "Damage up");
    DEF(ITEM_LORD_OF_PIT,     "Lord of the Pit", 0.2f,  0.3f,  0.0f,  0.0f,  0, 0,
        "Speed + damage");
    DEF(ITEM_TOUGH_LOVE,      "Tough Love",      0.3f,  0.0f,  0.3f,  0.0f,  0, 0,
        "Damage + tears");
    DEF(ITEM_SYNTHOIL,        "Synthoil",        0.4f,  0.0f,  0.0f,  0.5f,  0, 0,
        "Damage + range");
    DEF(ITEM_DEAD_EYE,        "Dead Eye",        0.6f,  0.0f,  0.0f,  0.0f,  0, 0,
        "Damage up");
    DEF(ITEM_THE_POOP,        "The Poop",        0.0f,  0.0f,  0.0f,  0.0f,  0, ITEM_FLAG_ACTIVE,
        "Active: spawn a poop");
    /* --- Phase 5 passive items --- */
    DEF(ITEM_SACRED_ORB,      "Sacred Orb",      0.8f,  0.0f,  0.0f,  0.3f,  0, 0,
        "Damage + range");
    DEF(ITEM_DEATHS_TOUCH,    "Death's Touch",   1.3f,  0.0f,  0.0f,  0.0f,  0, ITEM_FLAG_PIERCING,
        "Piercing, big damage");
    DEF(ITEM_MUTANT_SPIDER,   "Mutant Spider",  -0.3f,  0.0f,  1.2f,  0.0f,  0, 0,
        "Huge fire rate, less damage");
    DEF(ITEM_TAMMYS_HEAD,     "Tammy's Head",    0.7f,  0.0f, -0.5f,  0.5f,  0, 0,
        "Damage + range, slower fire");
    DEF(ITEM_A_PONY,          "A Pony",          0.0f,  0.5f,  0.0f,  0.0f,  0, 0,
        "Speed Up");
    DEF(ITEM_CRICKETS_BODY,   "Cricket's Body",  0.5f,  0.0f,  0.0f,  1.0f,  0, 0,
        "Damage + range");
    DEF(ITEM_SACRIFICIAL_DAGGER, "Sacrificial Dagger", 0.8f, 0.0f, 0.0f, 0.0f, 0, ITEM_FLAG_SPECTRAL,
        "Spectral tears, damage up");
    DEF(ITEM_IPECAC_LITE,     "Ipecac Lite",     0.5f,  0.0f, -0.5f,  0.0f,  0, ITEM_FLAG_EXPLOSIVE,
        "Small explosive tears");
    DEF(ITEM_MAGIC_FINGERS,   "Magic Fingers",   0.4f,  0.0f,  0.3f,  0.0f,  0, 0,
        "Damage + tears");
    DEF(ITEM_STEVEN,          "Steven",          0.2f,  0.0f,  0.0f,  0.0f,  0, ITEM_FLAG_HOMING | ITEM_FLAG_PIERCING,
        "Homing, piercing tears");
    /* --- Round 6 (Phase E) items --- */
    DEF(ITEM_THE_PACT,        "The Pact",        0.5f,  0.0f,  0.7f,  0.0f,  0, 0,
        "Damage + tears up");
    DEF(ITEM_NECRONOMICON,    "Necronomicon",    0.0f,  0.0f,  0.0f,  0.0f,  0, ITEM_FLAG_ACTIVE,
        "Active: damage the whole room");
    DEF(ITEM_TELEPORT,        "Teleport!",       0.0f,  0.0f,  0.0f,  0.0f,  0, ITEM_FLAG_ACTIVE,
        "Active: warp to a random room");
    DEF(ITEM_DECK_OF_CARDS,   "Deck of Cards",   0.0f,  0.0f,  0.0f,  0.0f,  0, ITEM_FLAG_ACTIVE,
        "Active: draw a tarot card");
    DEF(ITEM_BIBLE,           "The Bible",       0.0f,  0.0f,  0.0f,  0.0f,  0, ITEM_FLAG_ACTIVE,
        "Active: smites Mom... usually");
    DEF(ITEM_20_20,           "20/20",           0.0f,  0.0f,  0.0f,  0.0f,  0, ITEM_FLAG_DOUBLE,
        "Double shot");
    DEF(ITEM_TORN_PHOTO,      "Torn Photo",      0.0f,  0.0f,  0.7f,  0.0f,  0, 0,
        "Tears up");
    DEF(ITEM_BLUE_CAP,        "Blue Cap",        0.0f,  0.0f,  0.7f,  0.0f,  2, 0,
        "Tears + health up");
    DEF(ITEM_SQUEEZY,         "Squeezy",         0.0f,  0.0f,  0.4f,  0.0f,  0, 0,
        "Tears up + soul hearts");
    DEF(ITEM_BROTHER_BOBBY,   "Brother Bobby",   0.0f,  0.0f,  0.0f,  0.0f,  0, 0,
        "A friend that fires with you");
    DEF(ITEM_GHOST_BABY,      "Ghost Baby",      0.0f,  0.0f,  0.0f,  0.0f,  0, 0,
        "Spectral familiar");
    DEF(ITEM_DEMON_BABY,      "Demon Baby",      0.0f,  0.0f,  0.0f,  0.0f,  0, 0,
        "Auto-targeting familiar");
    /* --- R8 (M7) Krampus-only drops. Devil-pool flavored, but they NEVER
       appear in pick_random_item / pick_random_active_item rolls (see the
       item_excluded_from_pool filter) — Krampus's pedestal is the only
       source. --- */
    DEF(ITEM_LUMP_OF_COAL,    "Lump of Coal",    0.0f,  0.0f,  0.0f,  0.5f,  0, ITEM_FLAG_COAL,
        "My X-mas present");
    DEF(ITEM_HEAD_OF_KRAMPUS, "Head of Krampus", 0.0f,  0.0f,  0.0f,  0.0f,  0, ITEM_FLAG_ACTIVE,
        "Active: 4-way brimstone");
    /* --- R8 (M8) Guppy set — normal pool (NOT excluded) --- */
    DEF(ITEM_GUPPYS_PAW,      "Guppy's Paw",     0.0f,  0.0f,  0.0f,  0.0f,  0, 0,
        "Soul hearts... and whiskers?");
    DEF(ITEM_GUPPYS_HEAD,     "Guppy's Head",    0.0f,  0.0f,  0.0f,  0.0f,  0, ITEM_FLAG_ACTIVE,
        "Active: summon blue flies");
    DEF(ITEM_GUPPYS_TAIL,     "Guppy's Tail",    0.0f,  0.0f,  0.0f,  0.0f,  0, 0,
        "Luck up... and a tail?");
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
    { "The Chest",     12,      105,      1.7f,      2.5f,       1.65f,   BOSS_TIER_SHEOL    },
};

/* R9 (C1) route system: floors 6/7 get route-dependent identity.
 * LIGHT (route 1): 6 = Cathedral (pale stone/gold), 7 = The Chest.
 * DARK  (route 2): 6 = Sheol (as before),           7 = Dark Room.
 * g_floor_route mirrors Game.route (get_floor_info's signature is used
 * from ~15 call sites that don't carry the Game pointer); every write to
 * Game.route goes through set_route below so the two can never diverge. */
static int g_floor_route = 0;

/* Cathedral mirrors Sheol's tuning (same chapter depth); Dark Room mirrors
 * The Chest's — balance stays consistent with the neighboring floors. */
static const FloorInfo floor_cathedral = {
    "Cathedral",   9,  85,  1.6f, 2.2f, 1.5f,  BOSS_TIER_SHEOL
};
static const FloorInfo floor_dark_room = {
    "Dark Room",   12, 105, 1.7f, 2.5f, 1.65f, BOSS_TIER_SHEOL
};

static void set_route(Game *g, int route) {
    g->route = route;
    g_floor_route = route;
}

const FloorInfo *get_floor_info(int floor_num) {
    if (floor_num < 0) floor_num = 0;
    if (floor_num >= MAX_FLOORS) floor_num = MAX_FLOORS - 1;
    if (floor_num == 6 && g_floor_route == 1) return &floor_cathedral;
    if (floor_num == 7 && g_floor_route == 2) return &floor_dark_room;
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

/* T5: shared difficulty + infinite-loop enemy HP multiplier — the same
 * scaling the normal room-spawn path applies. Boss-AI minion spawns (Mom's
 * door-hands, Satan's hoppers) multiply their flat base HP by this once. */
static float spawn_hp_mult(const Game *g) {
    float m = diff_enemy_hp_mult(g->difficulty);
    if (g->game_mode == MODE_INFINITE && g->infinite_loop > 0) {
        m *= 1.0f + g->infinite_loop * 0.25f;
    }
    return m;
}

/* Heart drop chance threshold out of 100 (higher = more hearts) */
float diff_heart_drop_rate(Difficulty d) {
    switch (d) {
        case DIFF_EASY:   return 24.0f;
        case DIFF_HARD:   return 10.0f;
        default:          return 16.0f;  /* Normal: full<5, half<16 */
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
     * Haunt and Widow added here for variety on early floors. Steven (twin-head
     * Gemini-lite) added for more Basement variety. */
    { { ENEMY_BOSS_DUKE, ENEMY_BOSS_MONSTRO, ENEMY_BOSS_LARRY, ENEMY_BOSS_GEMINI, ENEMY_BOSS_HAUNT, ENEMY_BOSS_PIN, ENEMY_BOSS_STEVEN }, 7 },

    /* BOSS_TIER_CAVES (Floors 2-3): Mid-tier bosses. Add Peep, Gurdy, Gish, Chub. */
    { { ENEMY_BOSS_PEEP, ENEMY_BOSS_GURDY, ENEMY_BOSS_GEMINI, ENEMY_BOSS_LARRY, ENEMY_BOSS_FAMINE, ENEMY_BOSS_WIDOW, ENEMY_BOSS_GISH, ENEMY_BOSS_CHUB }, 8 },

    /* BOSS_TIER_DEPTHS (Floor 4): Hardest standard bosses. Add Gish, Fistula. */
    { { ENEMY_BOSS_FAMINE, ENEMY_BOSS_GURDY, ENEMY_BOSS_PEEP, ENEMY_BOSS_GEMINI, ENEMY_BOSS_MONSTRO, ENEMY_BOSS_PIN, ENEMY_BOSS_GISH, ENEMY_BOSS_FISTULA }, 8 },

    /* BOSS_TIER_WOMB (Floor 5): Late-game bosses with high HP. Scolex (segmented
     * burrowing worm) added for more Womb variety. Loki lives here too so he
     * stays drawable (Boss Rush waves on floors 5+ use this pool) now that
     * Sheol has a fixed story boss. */
    { { ENEMY_BOSS_GURDY, ENEMY_BOSS_PEEP, ENEMY_BOSS_FAMINE, ENEMY_BOSS_HAUNT, ENEMY_BOSS_WIDOW, ENEMY_BOSS_PIN, ENEMY_BOSS_SCOLEX, ENEMY_BOSS_LOKI }, 8 },

    /* BOSS_TIER_SHEOL (Floors 6-7): INTENTIONALLY UNUSED since Phase E —
     * floors 6/7 return fixed story bosses (Satan / Mega Satan) from
     * select_boss_for_floor before any pool lookup, and Boss Rush clamps its
     * pool floor to 5 (Womb). Kept only so boss_pools[] indexing matches the
     * BOSS_TIER_* enum. */
    { { ENEMY_BOSS_MEGA_SATAN, ENEMY_BOSS_LOKI }, 2 },
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
    case ENEMY_BOSS_DUKE:       return 36;
    case ENEMY_BOSS_MONSTRO:    return 44;
    case ENEMY_BOSS_LARRY:      return 40;
    case ENEMY_BOSS_GEMINI:     return 40;
    case ENEMY_BOSS_FAMINE:     return 50;
    /* Phase 2 bosses */
    case ENEMY_BOSS_PEEP:       return 48;
    case ENEMY_BOSS_GURDY:      return 70;  /* tanky, stationary */
    case ENEMY_BOSS_PIN:        return 44;
    case ENEMY_BOSS_HAUNT:      return 56;  /* two phases */
    case ENEMY_BOSS_WIDOW:      return 46;
    case ENEMY_BOSS_GISH:       return 60;  /* jumping tar boss, leaves creep */
    case ENEMY_BOSS_LOKI:       return 64;  /* teleporting spread-shooter */
    case ENEMY_BOSS_STEVEN:     return 40;  /* twin-head, Gemini-lite: moderate */
    case ENEMY_BOSS_CHUB:       return 60;  /* segmented + fatter: tanky like Gurdy */
    case ENEMY_BOSS_FISTULA:    return 48;  /* splits into balls: moderate-high */
    case ENEMY_BOSS_SCOLEX:     return 64;  /* segmented + burrows: tanky like Chub */
    case ENEMY_BOSS_MEGA_SATAN: return 120; /* final boss */
    /* Phase E story-arc fixed bosses */
    case ENEMY_BOSS_MOM:        return 80;  /* Depths capstone */
    case ENEMY_BOSS_MOMS_HEART: return 100; /* Womb capstone, tanky + waves */
    case ENEMY_BOSS_SATAN:      return 120; /* Sheol capstone, 3 phases */
    /* R9 (C1) route-arc fixed bosses */
    case ENEMY_BOSS_ISAAC:      return 100; /* Cathedral: Mom's Heart tier */
    case ENEMY_BOSS_THE_LAMB:   return 110; /* Dark Room: Mega Satan minus a notch */
    case ENEMY_BOSS_IT_LIVES:   return 130; /* Mom's Heart +30% */
    /* R8 (M3/M7) — Uriel/Gabriel/Krampus are minibosses spawned with FLAT
       innate HP (their spawn paths bypass get_boss_base_hp on purpose;
       floor scaling would balloon a floor-5 angel to ~160 HP). */
    case ENEMY_BOSS_URIEL:      return 55;  /* first angel of the run */
    case ENEMY_BOSS_GABRIEL:    return 70;  /* second angel: faster + denser */
    case ENEMY_BOSS_KRAMPUS:    return 70;  /* devil-room ambush */
    case ENEMY_BOSS_BLUE_BABY:  return 105; /* ??? — The Chest floor-7 boss */
    default:                    return 40;
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
    if (scaled < 30) scaled = 30;
    if (scaled > 220) scaled = 220;
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

/* Select a random boss from the floor's RANDOM pool, bypassing the fixed
 * story-boss mapping. Used directly by Boss Rush waves (which want pool
 * bosses even on story floors) and by select_boss_for_floor below.
 * Avoids repeating recently fought bosses when possible. */
static EnemyType select_pool_boss_for_floor(Game *g, int floor_num) {
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

/* Select the boss for a floor's boss room.
 * Returns the selected EnemyType. */
EnemyType select_boss_for_floor(Game *g, int floor_num) {
    /* Phase E: the story-arc floors have FIXED bosses (Rebirth progression).
       Earlier floors keep their random pools. Mom / Mom's Heart / Satan are
       never in any random pool. Callers that want pool bosses on a late
       floor (Boss Rush waves) call select_pool_boss_for_floor directly. */
    switch (floor_num) {
    case 4: return ENEMY_BOSS_MOM;        /* Depths */
    case 5:
        /* R9 M2: after the first completed run the Womb capstone becomes
           IT LIVES (Mom's Heart skeleton, +30% HP, faster waves, radial
           rings in its last quarter). Its death offers the same
           beam/trapdoor route choice. */
        return (g_config.total_wins >= 1) ? ENEMY_BOSS_IT_LIVES
                                          : ENEMY_BOSS_MOMS_HEART;
    case 6:
        /* Route split: LIGHT = Cathedral (Isaac), DARK = Sheol (Satan).
           Route 0 can't normally reach floor 6; fall back to dark. */
        return (g->route == 1) ? ENEMY_BOSS_ISAAC : ENEMY_BOSS_SATAN;
    case 7:
        /* R8 (M3): golden doors landed — Mega Satan now lives ONLY behind
           the golden door (both routes; see open_mega_satan_room). The
           regular floor-7 boss is ??? (Blue Baby) in The Chest and The
           Lamb in the Dark Room. */
        return (g->route == 2) ? ENEMY_BOSS_THE_LAMB : ENEMY_BOSS_BLUE_BABY;
    default: break;
    }
    return select_pool_boss_for_floor(g, floor_num);
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

/* Isaac Rebirth paper palette (Round 7 UI restyle) */
static u32 PAPER;        /* cream/bone page                */
static u32 PAPER_DARK;   /* blotches/creases               */
static u32 PAPER_EDGE;   /* crease lines                   */
static u32 INK;          /* main text                      */
static u32 INK_FAINT;    /* hints/secondary text           */
static u32 INK_DISABLED; /* grayed options                 */
static u32 BORDER_BROWN; /* panel frames                   */
static u32 BLOOD;        /* selector/accents               */
static u32 BLOOD_DARK;   /* drip shading                   */
static u32 GOLD_CHARGE;  /* active-item ready              */
static u32 DIM_BLACK140; /* pause dim                      */

static void init_colours(void) {
    COL_BG            = C2D_Color32(30,  30,  30,  255);
    COL_WALL          = C2D_Color32(80,  60,  45,  255);
    COL_FLOOR         = C2D_Color32(140, 115, 90,  255);
    COL_PLAYER        = C2D_Color32(220, 200, 170, 255);
    COL_PLAYER_HIT    = C2D_Color32(255, 80,  80,  255);
    COL_TEAR          = C2D_Color32(100, 150, 255, 255);
    COL_HEART_FULL    = C2D_Color32(193, 18,  31,  255);  /* Rebirth heart red */
    COL_HEART_EMPTY   = C2D_Color32(90,  20,  25,  255);  /* dark maroon socket */
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

    PAPER        = C2D_Color32(234, 221, 200, 255);
    PAPER_DARK   = C2D_Color32(219, 204, 178, 255);
    PAPER_EDGE   = C2D_Color32(190, 173, 143, 255);
    INK          = C2D_Color32(55,  43,  33,  255);
    INK_FAINT    = C2D_Color32(122, 99,  75,  255);
    INK_DISABLED = C2D_Color32(168, 148, 126, 255);
    BORDER_BROWN = C2D_Color32(62,  47,  34,  255);
    BLOOD        = C2D_Color32(171, 22,  26,  255);
    BLOOD_DARK   = C2D_Color32(110, 12,  16,  255);
    GOLD_CHARGE  = C2D_Color32(247, 198, 74,  255);
    DIM_BLACK140 = C2D_Color32(0,   0,   0,   140);
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

static int player_has_item(const Player *p, ItemType type);

void recalc_player_stats(Player *p) {
    /* Start from base stats - adjusted per character */
    p->stats.damage    = 1.0f;
    p->stats.speed     = PLAYER_BASE_SPEED;
    p->stats.fire_rate = 0.0f;   /* modifier; see get_tear_cooldown() for the tears curve */
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
        case CHAR_EVE:
            p->stats.max_hp = 6;          /* 3 hearts */
            p->stats.damage *= 1.3f;
            p->stats.speed  *= 0.9f;
            break;
        case CHAR_SAMSON:
            p->stats.max_hp = PLAYER_BASE_HP;  /* normal HP */
            p->stats.damage *= 1.2f;
            p->stats.speed  *= 1.1f;
            break;
        case CHAR_BLUE_BABY:
            p->stats.max_hp = 4;          /* 2 red heart containers (safe option) */
            p->stats.damage *= 1.1f;
            break;
        /* --- R10 (C4) characters --- */
        case CHAR_AZAZEL:
            p->stats.max_hp = 4;          /* 2 red containers (+1 black heart at start) */
            p->stats.damage *= 1.3f;      /* +0.3 dmg */
            p->stats.speed  *= 1.05f;     /* +0.1 speed-ish */
            break;
        case CHAR_LAZARUS:
            /* Normal Isaac stats, 3 red containers; his edge is the extra
               life + the permanent rags bonus added below. */
            break;
        case CHAR_LOST:
            /* No health at all — max_hp forced to 0 after the clamps below.
               Flight/spectral/mantle granted as innate flags. */
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
        if (def->type == ITEM_GUPPYS_TAIL) p->stats.luck += 1.0f;  /* R8 (M8) */
        if (def->type == ITEM_DEAD_CAT)   p->stats.max_hp = 2;  /* Dead Cat: max HP = 1 heart */
    }

    /* Item synergies: hand-authored pairs that grant a bonus when BOTH are held. */
    if (player_has_item(p, ITEM_INNER_EYE) && player_has_item(p, ITEM_POLYPHEMUS)) {
        /* Triple shot + big single laser-like tear -> extra damage on the boosted spread */
        p->stats.damage += 0.5f;
    }
    if (player_has_item(p, ITEM_BRIMSTONE) &&
        (player_has_item(p, ITEM_SPOON_BENDER) || player_has_item(p, ITEM_SACRED_HEART))) {
        /* Homing item + Brimstone -> laser tracks targets */
        p->stats.flags |= ITEM_FLAG_HOMING;
        p->stats.damage += 0.3f;
    }
    if (player_has_item(p, ITEM_SAD_ONION) &&
        (player_has_item(p, ITEM_WIRE_COAT) || player_has_item(p, ITEM_WIRE_HANGER))) {
        /* Sad Onion + Wire Coat/Hanger -> extra fire rate */
        p->stats.fire_rate += 0.4f;
    }
    if (player_has_item(p, ITEM_SPOON_BENDER) && player_has_item(p, ITEM_CUPIDS_ARROW)) {
        /* Homing + piercing already granted individually; add a small damage bonus */
        p->stats.damage += 0.3f;
    }
    if (player_has_item(p, ITEM_MAGIC_MUSH) && player_has_item(p, ITEM_CRICKETS_HEAD)) {
        /* Magic Mushroom + Cricket's Head -> extra damage */
        p->stats.damage += 0.5f;
    }
    if (player_has_item(p, ITEM_SPEED_BALL) &&
        (player_has_item(p, ITEM_BELT) || player_has_item(p, ITEM_ROID_RAGE))) {
        /* Speed Ball + Belt/Roid Rage -> extra speed */
        p->stats.speed += 0.3f;
    }
    if (player_has_item(p, ITEM_SACRED_HEART) && player_has_item(p, ITEM_BLOOD_OF_MARTYR)) {
        /* Sacred Heart + Blood of Martyr -> extra damage */
        p->stats.damage += 0.5f;
    }
    if (player_has_item(p, ITEM_CUPIDS_ARROW) && player_has_item(p, ITEM_ANEMIC)) {
        /* Cupid's Arrow + Anemic -> extra range */
        p->stats.range += 20.0f;
    }

    /* Shot speed: flavor-fit items grant +1 each (+15% tear velocity in
     * shoot_tear). Speed Ball has +shot speed in Rebirth; Cupid's Arrow is
     * a swift arrow; Synthoil is slick oil — fast, greased shots. */
    if (player_has_item(p, ITEM_SPEED_BALL))   p->stats.shot_speed += 1;
    if (player_has_item(p, ITEM_CUPIDS_ARROW)) p->stats.shot_speed += 1;
    if (player_has_item(p, ITEM_SYNTHOIL))     p->stats.shot_speed += 1;

    /* Book of Belial active damage buff */
    if (p->book_belial_dmg_timer > 0) p->stats.damage += 1.5f;

    /* R8 (M6): tarot temp buffs (Empress/Strength/Devil dmg; Empress/Chariot
       speed). Bonus amounts are zeroed when their timer expires. */
    if (p->card_dmg_timer > 0) p->stats.damage += p->card_dmg_bonus;
    if (p->card_spd_timer > 0) p->stats.speed  += p->card_spd_bonus;

    /* R8 (M8) transformations */
    if (p->guppy_active)  p->stats.flags |= ITEM_FLAG_FLIGHT;
    if (p->funguy_active) p->stats.max_hp += 2;   /* FUN GUY: +1 container */

    /* --- R10 (C4) innate character abilities (flags can't be lost) --- */
    if (p->character == CHAR_AZAZEL) {
        /* Flight + short-range Brimstone (beam clamped in laser_update
           unless the real Brimstone item is also held). The item carries
           a -2.0 fire-rate penalty Azazel doesn't get; without any brake
           his beam cadence would be absurd — apply a milder -1.0 innate
           penalty (his beam is short, so he recharges faster than the
           real item but slower than tears). */
        p->stats.flags |= ITEM_FLAG_FLIGHT | ITEM_FLAG_BRIMSTONE;
        if (!player_has_item(p, ITEM_BRIMSTONE))
            p->stats.fire_rate -= 1.0f;
    }
    if (p->character == CHAR_LOST) {
        /* Flight, spectral tears, and an unlosable Holy Mantle (the
           per-room re-arm keys off ITEM_FLAG_MANTLE). */
        p->stats.flags |= ITEM_FLAG_FLIGHT | ITEM_FLAG_SPECTRAL
                        | ITEM_FLAG_MANTLE;
    }
    /* Eve — Whore of Babylon: at <= 1 full red heart (hp is in half-heart
       units) she turns on. Recalc is re-run on any HP-pool change by the
       STATE_PLAYING update (prev_hp_total watcher). */
    if (p->character == CHAR_EVE && p->hp <= 2) {
        p->stats.damage += 1.2f;
        p->stats.speed  += 0.2f;
    }
    /* Samson — Bloody Lust: +0.15 dmg per hit taken this room, cap +1.0.
       samson_hits resets on every room change. */
    if (p->character == CHAR_SAMSON && p->samson_hits > 0) {
        float bl = p->samson_hits * 0.15f;
        if (bl > 1.0f) bl = 1.0f;
        p->stats.damage += bl;
    }
    /* Lazarus' Rags: permanent per-run bonus from death-respawns */
    p->stats.damage += p->lazarus_dmg_bonus;

    /* Persistent pill stat bonuses (preserved across item pickups) */
    p->stats.speed     += p->pill_speed_bonus;
    p->stats.fire_rate += p->pill_fire_rate_bonus;
    p->stats.range     += p->pill_range_bonus;
    p->stats.luck      += p->pill_luck_bonus;
    p->stats.max_hp    += p->pill_max_hp_bonus;

    /* Trinket passives */
    if (p->trinket == TRINKET_LUCKY_TOE)     p->stats.luck      += 1.0f;
    if (p->trinket == TRINKET_CRACKED_CROWN) p->stats.damage    += 0.3f;
    if (p->trinket == TRINKET_CANCER)        p->stats.fire_rate += 0.5f;
    if (p->trinket == TRINKET_TICK)          p->stats.damage    += 0.4f;
    if (p->trinket == TRINKET_UMBILICAL_CORD) p->stats.max_hp   += 1;
    if (p->trinket == TRINKET_CURVED_HORN)   p->stats.damage    += 0.4f;

    /* Clamp stats */
    if (p->stats.damage < 0.3f) p->stats.damage = 0.3f;
    if (p->stats.speed < 1.0f) p->stats.speed = 1.0f;
    if (p->stats.speed > 4.0f) p->stats.speed = 4.0f;
    if (p->stats.range < 60.0f) p->stats.range = 60.0f;
    if (p->stats.range > 300.0f) p->stats.range = 300.0f;
    if (p->stats.fire_rate < -3.0f) p->stats.fire_rate = -3.0f;
    if (p->stats.max_hp > PLAYER_MAX_HP_CAP) p->stats.max_hp = PLAYER_MAX_HP_CAP;
    if (p->stats.max_hp < 2) p->stats.max_hp = 2;

    /* Challenge: Glass Cannon — locked to one heart, big damage */
    if (p->challenge == 1) {
        p->stats.max_hp = 2;
        p->stats.damage += 2.0f;
    }

    /* E9 Challenge: Speed! — the player moves 1.4x too (enemies get their
       1.4x via the FloorInfo speed multipliers in enemies_update) */
    if (p->challenge == 4) {
        p->stats.speed *= 1.4f;
        if (p->stats.speed > 4.5f) p->stats.speed = 4.5f;
    }

    /* R10 (C4) The Lost: NO health, ever — overrides the min-2 clamp,
       every hp_bonus item, pills and challenges. Death path is safe: the
       game only checks player_check_death after a damage event, and the
       Holy Mantle flag above absorbs the first hit of each room. */
    if (p->character == CHAR_LOST) p->stats.max_hp = 0;
}

/* E5: number of fully-depleted black hearts awaiting their room-wide 40dmg
 * burst. Set inside player_absorb_dmg (which has no Game*), processed once
 * per frame in the STATE_PLAYING update. */
static int g_black_burst_pending = 0;

/* Damage absorption: holy_mantle blocks all; black hearts absorb first
 * (Rebirth order: black BEFORE soul), then soul hearts.
 * Returns damage that actually reaches red HP. */
static int player_absorb_dmg(Player *p, int dmg) {
    if (p->holy_mantle_active) {
        p->holy_mantle_active = 0;
        return 0;
    }
    /* Black hearts (E5): each time a heart's 2 units hit an even boundary,
       one full black heart has been depleted -> queue a room burst. */
    while (dmg > 0 && p->black_hp > 0) {
        p->black_hp--;
        dmg--;
        if ((p->black_hp & 1) == 0) g_black_burst_pending++;
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
        /* R10 (C4) Lazarus: each death-respawn leaves him angrier —
           permanent +0.5 dmg for the rest of the run (Lazarus' Rags). */
        if (p->character == CHAR_LAZARUS) {
            p->lazarus_dmg_bonus += 0.5f;
            recalc_player_stats(p);
        }
        return 0;
    }
    return 1;
}

/* R8 (M8) transformations: called on every SUCCESSFUL item grant (both the
 * active-slot and passive paths). Counts set pieces and fires the banner +
 * stat recalc when a transformation completes. Per-run counters on Player.
 *  - GUPPY: 3+ of {Dead Cat, Guppy's Paw, Guppy's Head, Guppy's Tail} ->
 *    flight (ITEM_FLAG_FLIGHT via recalc) + tears spawn friendly blue flies.
 *  - FUN GUY: all 3 mushrooms {Magic Mushroom, Odd Mushroom, Blue Cap} ->
 *    +1 heart container (recalc adds it while funguy_active), healed full. */
static void check_transformations(Game *g, ItemType item) {
    Player *p = &g->player;
    if (item == ITEM_DEAD_CAT || item == ITEM_GUPPYS_PAW ||
        item == ITEM_GUPPYS_HEAD || item == ITEM_GUPPYS_TAIL) {
        p->guppy_count++;
        if (p->guppy_count >= 3 && !p->guppy_active) {
            p->guppy_active = 1;
            recalc_player_stats(p);
            snprintf(g->pickup_msg_text, sizeof(g->pickup_msg_text),
                     "GUPPY! You are what you eat");
            g->pickup_msg_timer = 240;
            audio_play(SFX_PICKUP);
        }
    }
    if (item == ITEM_MAGIC_MUSH || item == ITEM_ODD_MUSHROOM ||
        item == ITEM_BLUE_CAP) {
        p->funguy_count++;
        if (p->funguy_count >= 3 && !p->funguy_active) {
            p->funguy_active = 1;
            recalc_player_stats(p);   /* +2 max HP while funguy_active */
            p->hp += 2;
            if (p->hp > p->stats.max_hp) p->hp = p->stats.max_hp;
            snprintf(g->pickup_msg_text, sizeof(g->pickup_msg_text),
                     "FUN GUY! +1 heart container");
            g->pickup_msg_timer = 240;
            audio_play(SFX_PICKUP);
        }
    }
}

/* Returns 1 if the item was actually granted, 0 if refused (passive-item
 * cap reached). Purchases must gate their payment on this so the player
 * can never pay and get nothing. */
int collect_item(Game *g, ItemType item) {
    Player *p = &g->player;
    const ItemDef *def = get_item_def(item);

    /* Active items are held in the dedicated active slot with a charge bar
     * instead of the passive items[] list. */
    if (def && (def->flags & ITEM_FLAG_ACTIVE)) {
        p->active_item = item;
        switch (item) {
        case ITEM_YUM_HEART:      p->active_max_charge = 2; break;
        case ITEM_BOOK_OF_BELIAL: p->active_max_charge = 3; break;
        case ITEM_THE_POOP:       p->active_max_charge = 1; break;
        /* Phase E7 actives */
        case ITEM_NECRONOMICON:   p->active_max_charge = 4; break;
        case ITEM_TELEPORT:       p->active_max_charge = 2; break;
        case ITEM_DECK_OF_CARDS:  p->active_max_charge = 6; break;
        case ITEM_BIBLE:          p->active_max_charge = 6; break;
        /* R8 (M7) */
        case ITEM_HEAD_OF_KRAMPUS: p->active_max_charge = 4; break;
        /* R8 (M8) */
        case ITEM_GUPPYS_HEAD:    p->active_max_charge = 2; break;
        default:                  p->active_max_charge = 2; break;
        }
        p->active_charge = p->active_max_charge;  /* start fully charged */

        /* Show pickup name + description on top screen */
        if (def->name) {
            snprintf(g->pickup_msg_text, sizeof(g->pickup_msg_text),
                     "%s - %s", def->name,
                     def->description ? def->description : "");
            g->pickup_msg_timer = 180;
        }
        /* R8 (M8): actives count toward transformations too (Guppy's Head) */
        check_transformations(g, item);
        return 1;
    }

    if (p->item_count >= MAX_ITEMS_HELD) return 0;

    p->items[p->item_count++] = item;
    recalc_player_stats(p);

    /* Heal for HP bonus items */
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
    /* E8 Squeezy: +2 soul hearts granted on pickup (4 half-heart units) */
    if (item == ITEM_SQUEEZY) {
        p->soul_hp += 4;
        if (p->soul_hp > 12) p->soul_hp = 12;
    }
    /* R8 (M8) Guppy's Paw: +2 soul hearts on pickup (Squeezy-style) */
    if (item == ITEM_GUPPYS_PAW) {
        p->soul_hp += 4;
        if (p->soul_hp > 12) p->soul_hp = 12;
    }
    /* E6 familiars: occupy the first free follower slot. A 3rd familiar
       with both slots full converts into +2 soul hearts (like Squeezy)
       so the pickup is never a silent no-op; the item stays recorded. */
    if (item == ITEM_BROTHER_BOBBY || item == ITEM_GHOST_BABY ||
        item == ITEM_DEMON_BABY) {
        if (p->familiar[0] == ITEM_NONE)      p->familiar[0] = item;
        else if (p->familiar[1] == ITEM_NONE) p->familiar[1] = item;
        else {
            p->soul_hp += 4;
            if (p->soul_hp > 12) p->soul_hp = 12;
        }
    }
    /* R8 (M8): transformation progress (may overwrite the pickup message
       with the GUPPY!/FUN GUY! banner — intentional, the banner wins) */
    check_transformations(g, item);
    return 1;
}

static int get_tear_cooldown(Player *p) {
    /* Rebirth-style diminishing-returns tears curve.
     * fr >= 0: cd = 6 + 16/(1 + 0.35*fr) — base (fr 0) = 22 (~2.7 tears/s,
     *          close to Rebirth's ~22f), Soy Milk (+3.0) lands ~14,
     *          big stacks bottom out near 9-10.
     * fr <  0: continues UP from the fr=0 value at 6 frames per point so
     *          slow-fire items are genuinely slower than base
     *          (Polyphemus -1.0 -> 28). */
    float fr = p->stats.fire_rate;
    float cd;
    if (fr >= 0.0f) cd = 6.0f + 16.0f / (1.0f + 0.35f * fr);
    else            cd = 22.0f + (-fr) * 6.0f;
    if (cd < 5.0f) cd = 5.0f;
    if (cd > 40.0f) cd = 40.0f;
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

/* R8 (M7): fixed-drop items that must NEVER come up in random pedestal /
 * shop / pool rolls — Krampus's death pedestal is their only source. */
static int item_excluded_from_pool(ItemType t) {
    return t == ITEM_LUMP_OF_COAL || t == ITEM_HEAD_OF_KRAMPUS;
}

/* Pick a random item that the player doesn't already have */
static ItemType pick_random_item(Game *g) {
    /* Build pool of items not yet collected (the held active item counts
       as collected — don't re-offer it on pedestals) */
    ItemType available[ITEM_COUNT];
    int avail_count = 0;
    for (int t = 1; t < ITEM_COUNT; t++) {
        if (item_excluded_from_pool((ItemType)t)) continue;
        int has = (g->player.active_item == (ItemType)t);
        for (int j = 0; j < g->player.item_count; j++) {
            if (g->player.items[j] == (ItemType)t) { has = 1; break; }
        }
        if (!has) available[avail_count++] = (ItemType)t;
    }
    if (avail_count == 0) return ITEM_PENTAGRAM; /* fallback */
    return available[randi(0, avail_count - 1)];
}

/* Library room: bias toward active (book-like, usable-with-charge) items.
 * Falls back to any uncollected item if no active items remain available. */
static ItemType pick_random_active_item(Game *g) {
    ItemType available[ITEM_COUNT];
    int avail_count = 0;
    ItemType active_available[ITEM_COUNT];
    int active_count = 0;
    for (int t = 1; t < ITEM_COUNT; t++) {
        if (item_excluded_from_pool((ItemType)t)) continue;
        int has = (g->player.active_item == (ItemType)t);
        for (int j = 0; j < g->player.item_count; j++) {
            if (g->player.items[j] == (ItemType)t) { has = 1; break; }
        }
        if (has) continue;
        available[avail_count++] = (ItemType)t;
        if (item_pool[t].flags & ITEM_FLAG_ACTIVE) {
            active_available[active_count++] = (ItemType)t;
        }
    }
    if (active_count > 0) return active_available[randi(0, active_count - 1)];
    if (avail_count > 0) return available[randi(0, avail_count - 1)];
    return ITEM_PENTAGRAM; /* fallback */
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
        /* Curse of the Labyrinth: bigger floor layout. Bounded by MAX_ROOMS
         * and the DUNGEON_W*DUNGEON_H grid so this can never overflow the
         * fixed rooms[][] array or request more rooms than the grid can
         * physically hold. */
        if (g_curse_labyrinth_pending) {
            target += 5;
            if (target > MAX_ROOMS) target = MAX_ROOMS;
            if (target > DUNGEON_W * DUNGEON_H) target = DUNGEON_W * DUNGEON_H;
        }
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
            /* No safe (non-critical-path) spot: skip the curse room entirely
               rather than place it on the boss's only path and risk a soft-lock
               at <=1 HP (curse rooms are optional). */
            done_curse:;
        }

        /* ── Place sacrifice room (non-critical path only) ── */
        if (floor_num >= 1) {
            for (int ry = 0; ry < DUNGEON_H; ry++) {
                for (int rx = 0; rx < DUNGEON_W; rx++) {
                    if (d->rooms[ry][rx].type == ROOM_NORMAL) {
                        int dist_start = abs(rx - sx) + abs(ry - sy);
                        if (dist_start >= 2 && !is_on_critical_path(d, rx, ry)) {
                            d->rooms[ry][rx].type = ROOM_SACRIFICE;
                            goto done_sacrifice;
                        }
                    }
                }
            }
            /* No safe (non-critical-path) spot: skip the sacrifice room entirely
               rather than place it on the boss's only path (sacrifice rooms
               are optional). */
            done_sacrifice:;
        }

        /* ── Place Boss Rush room (non-critical path only, floors >= 2) ── */
        if (floor_num >= 2) {
            for (int ry = 0; ry < DUNGEON_H; ry++) {
                for (int rx = 0; rx < DUNGEON_W; rx++) {
                    if (d->rooms[ry][rx].type == ROOM_NORMAL) {
                        int dist_start = abs(rx - sx) + abs(ry - sy);
                        if (dist_start >= 2 && !is_on_critical_path(d, rx, ry)) {
                            d->rooms[ry][rx].type = ROOM_BOSSRUSH;
                            goto done_bossrush;
                        }
                    }
                }
            }
            /* No safe (non-critical-path) spot: skip the Boss Rush room entirely
               rather than place it on the boss's only path (optional room). */
            done_bossrush:;
        }

        /* ── Place Arcade room (non-critical path only, floors >= 1) ── */
        if (floor_num >= 1) {
            for (int ry = 0; ry < DUNGEON_H; ry++) {
                for (int rx = 0; rx < DUNGEON_W; rx++) {
                    if (d->rooms[ry][rx].type == ROOM_NORMAL) {
                        int dist_start = abs(rx - sx) + abs(ry - sy);
                        if (dist_start >= 2 && !is_on_critical_path(d, rx, ry)) {
                            d->rooms[ry][rx].type = ROOM_ARCADE;
                            goto done_arcade;
                        }
                    }
                }
            }
            /* No safe (non-critical-path) spot: skip the Arcade room entirely
               rather than place it on the boss's only path (optional room). */
            done_arcade:;
        }

        /* ── Place Library room (non-critical path only, floors >= 1) ── */
        if (floor_num >= 1) {
            for (int ry = 0; ry < DUNGEON_H; ry++) {
                for (int rx = 0; rx < DUNGEON_W; rx++) {
                    if (d->rooms[ry][rx].type == ROOM_NORMAL) {
                        int dist_start = abs(rx - sx) + abs(ry - sy);
                        if (dist_start >= 2 && !is_on_critical_path(d, rx, ry)) {
                            d->rooms[ry][rx].type = ROOM_LIBRARY;
                            goto done_library;
                        }
                    }
                }
            }
            /* No safe (non-critical-path) spot: skip the Library room entirely
               rather than place it on the boss's only path (optional room). */
            done_library:;
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

/* Authored room layouts (Rebirth-style interiors). Logical grid of 9x5
   cells inside the play area; the middle column (i=4) and middle row (j=2)
   are always left clear so every door keeps an open lane by construction. */
typedef struct { unsigned char i, j, t; } ObSpec;

static const ObSpec PAT_CORNERS[]   = {{1,1,0},{7,1,0},{1,3,0},{7,3,0}};
static const ObSpec PAT_RING[]      = {{2,1,0},{3,1,0},{5,1,0},{6,1,0},
                                       {2,3,0},{3,3,0},{5,3,0},{6,3,0}};
static const ObSpec PAT_PILLARS[]   = {{2,1,0},{2,3,0},{6,1,0},{6,3,0}};
static const ObSpec PAT_H_WALLS[]   = {{2,0,0},{2,1,0},{2,3,0},{2,4,0},
                                       {6,0,0},{6,1,0},{6,3,0},{6,4,0}};
static const ObSpec PAT_POOP_GRID[] = {{3,1,1},{5,1,1},{3,3,1},{5,3,1}};
static const ObSpec PAT_POOP_DIAG[] = {{1,0,1},{2,1,1},{6,3,1},{7,4,1}};
static const ObSpec PAT_MIXED[]     = {{1,1,0},{7,1,0},{1,3,0},{7,3,0},
                                       {3,1,1},{5,3,1}};
static const ObSpec PAT_SPIKE_ROW[] = {{2,1,2},{3,1,2},{5,1,2},{6,1,2}};
static const ObSpec PAT_SPIKE_COR[] = {{1,1,2},{7,1,2},{1,3,2},{7,3,2}};
static const ObSpec PAT_SPIKE_BOX[] = {{3,1,2},{5,1,2},{3,3,2},{5,3,2},
                                       {1,2,0},{7,2,0}};

static void place_obstacles(Room *r) {
    r->obstacle_count = 0;
    if (r->type != ROOM_NORMAL) return;   /* boss/special rooms stay open */

    static const struct { const ObSpec *p; int n; } pats[] = {
        {NULL, 0}, {NULL, 0}, {NULL, 0},           /* open rooms stay common */
        {PAT_CORNERS,   4}, {PAT_RING,      8},
        {PAT_PILLARS,   4}, {PAT_H_WALLS,   8},
        {PAT_POOP_GRID, 4}, {PAT_POOP_DIAG, 4},
        {PAT_MIXED,     6}, {PAT_SPIKE_ROW, 4},
        {PAT_SPIKE_COR, 4}, {PAT_SPIKE_BOX, 6},
    };
    int pick = randi(0, (int)(sizeof(pats) / sizeof(pats[0])) - 1);
    const ObSpec *pat = pats[pick].p;
    int n = pats[pick].n;

    for (int i = 0; i < n && i < MAX_OBSTACLES; i++) {
        Obstacle *o = &r->obstacles[r->obstacle_count++];
        o->x = ROOM_LEFT + (pat[i].i + 1) * (ROOM_RIGHT - ROOM_LEFT) / 10.0f;
        o->y = ROOM_TOP  + (pat[i].j + 1) * (ROOM_BOTTOM - ROOM_TOP) / 6.0f;
        o->type = pat[i].t;
        o->hp = (o->type == OBST_POOP) ? 3 : 1;
        o->active = 1;
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
            c->sub_type = 0;   /* R8 (M5): stale sub_type from a reused slot
                                  must not mark a fresh red chest "opened" */
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
    } else if (roll < 92) {
        spawn_consumable(r, x, y, PICKUP_COIN5);
    } else if (roll < 96) {
        spawn_consumable(r, x, y, PICKUP_BOMB2);
    } else if (roll < 99) {
        spawn_consumable(r, x, y, PICKUP_KEY5);
    } else {
        spawn_consumable(r, x, y, PICKUP_BATTERY);
    }
}

/* Initialize an Enemy slot as a boss: selects (or accepts) a boss type,
 * scales HP for the floor, and runs the per-boss-type init switch.
 * Reused by both normal boss rooms and the Boss Rush wave controller. */
static void init_boss_enemy(Game *g, const FloorInfo *fi, Enemy *e, EnemyType selected_boss, float cx) {
    int boss_hp = get_boss_base_hp(selected_boss, g->current_floor);

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
        e->hidden = 1;           /* burrowed = tear-immune + no contact */
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
    case ENEMY_BOSS_GISH:
        /* Copies Monstro's jump+shoot loop; adds creep on landing. */
        e->phase = 0;            /* 0=idle, 1=airborne, 2=landing recovery */
        e->jump_z = 0;
        e->jump_vz = 0;
        e->attack_pattern = 0;
        break;
    case ENEMY_BOSS_LOKI:
        /* Stationary teleporting spread-shooter. */
        e->phase = 0;            /* 0=idle aim, blinks on a timer */
        e->dx = 0; e->dy = 0;    /* stationary between blinks */
        e->timer = 90;           /* blink timer */
        e->shoot_timer = 60;     /* fire timer */
        e->attack_pattern = 0;   /* alternates 4-way / 8-way */
        break;
    case ENEMY_BOSS_STEVEN:
        /* Steven: twin-head Gemini-lite. Reuses Gemini's tether/split fields
           but never fully detaches -- the second head just wanders on a
           shorter tether and gets a bit more aggressive at low HP. */
        e->gemini_split = 0;
        e->gemini_cx = e->x + 28;
        e->gemini_cy = e->y;
        e->gemini_cdx = 0;
        e->gemini_cdy = 0;
        e->gemini_chp = boss_hp / 3;
        e->dx = 1.0f * fi->boss_speed_scale;
        e->dy = 0.8f * fi->boss_speed_scale;
        break;
    case ENEMY_BOSS_CHUB:
        /* Chub: segmented like Larry Jr but fatter/slower/tankier. */
        e->seg_count = MAX_LARRY_SEGMENTS;
        e->seg_speed = 1.3f * fi->boss_speed_scale;
        e->dx = 1.3f * fi->boss_speed_scale;
        e->dy = 0.4f;
        for (int s = 0; s < MAX_LARRY_SEGMENTS; s++) {
            e->segments[s].x = e->x - (s + 1) * LARRY_SEG_DIST;
            e->segments[s].y = e->y;
            e->segments[s].prev_x = e->segments[s].x;
            e->segments[s].prev_y = e->segments[s].y;
        }
        break;
    case ENEMY_BOSS_FISTULA:
        /* Fistula: splits into smaller balls on hit (handled in the damage/
           death paths). Slow drifting movement, no ranged attack. */
        e->phase = 0;
        e->split_done = 0;
        e->dx = randf(-0.6f, 0.6f) * fi->boss_speed_scale;
        e->dy = randf(-0.6f, 0.6f) * fi->boss_speed_scale;
        e->timer = randi(30, 60);
        break;
    case ENEMY_BOSS_SCOLEX:
        /* Scolex: segmented worm like Chub, but burrows (phase 1) to close
           distance safely before emerging (phase 0) to chase + collide. */
        e->seg_count = MAX_LARRY_SEGMENTS;
        e->seg_speed = 1.6f * fi->boss_speed_scale;
        e->phase = 1;            /* 0=emerged (vulnerable body collision), 1=burrowed */
        e->hidden = 1;           /* burrowed = tear-immune + no contact */
        e->timer = 90;           /* time burrowed */
        e->dx = 0; e->dy = 0;
        for (int s = 0; s < MAX_LARRY_SEGMENTS; s++) {
            e->segments[s].x = e->x - (s + 1) * LARRY_SEG_DIST;
            e->segments[s].y = e->y;
            e->segments[s].prev_x = e->segments[s].x;
            e->segments[s].prev_y = e->segments[s].y;
        }
        break;
    case ENEMY_BOSS_MOM:
        /* Mom: stomping foot (Monstro jump-target machinery) + periodic
           door-hand minion spawns on a Duke-style spawn timer. */
        e->phase = 0;            /* 0=idle, 1=airborne stomp, 2=recovery */
        e->jump_z = 0;
        e->jump_vz = 0;
        e->attack_pattern = 0;
        e->timer = 90;
        e->shoot_timer = DUKE_SPAWN_INTERVAL;  /* door-hand spawn timer */
        break;
    case ENEMY_BOSS_MOMS_HEART:
        /* Mom's Heart: stationary tank (Gurdy AI base) + enemy waves. */
        e->phase = 0;            /* 0=idle, 1=wave spawn, 2=shot spread */
        e->dx = 0; e->dy = 0;
        e->timer = 75;
        e->shoot_timer = 60;
        e->attack_pattern = 0;
        break;
    case ENEMY_BOSS_SATAN:
        /* Satan: 3 HP-driven phases (e->phase). Movement sub-state lives in
           e->attack_pattern (0=idle, 1=airborne stomp, 2=recovery) so the
           HP phase and the stomp state machine don't fight over e->phase. */
        e->phase = 0;
        e->attack_pattern = 0;
        e->dx = 0; e->dy = 0;
        e->timer = 90;
        e->shoot_timer = 60;
        break;
    case ENEMY_BOSS_ISAAC:
        /* Isaac (R9 C1): 3 HP-driven phases in e->phase. e->state is the
           pray/hop sub-state: 1 = praying (floats, NO contact damage),
           0 = active dodge-hop. Light columns run on g->vbeam_*. */
        e->phase = 0;
        e->state = 1;            /* opens praying */
        e->attack_pattern = 0;   /* deterministic ring-angle counter */
        e->dx = 0; e->dy = 0;
        e->timer = 70;
        e->shoot_timer = 90;
        break;
    case ENEMY_BOSS_THE_LAMB:
        /* The Lamb (R9 C1): demonic mirror of Isaac. P1 slow chase +
           brimstone crosses (g->ebeam_* with ebeam_cross set); P2 detaches
           the Lamb Body (Gemini companion fields: body chases, head goes
           stationary turret); P3 enrage below 25%. Body HP is granted at
           the detach, so gemini_chp starts 0 (no body target yet). */
        e->phase = 0;
        e->gemini_split = 0;
        e->gemini_chp = 0;
        e->gemini_cx = e->x;
        e->gemini_cy = e->y + 20;
        e->gemini_cdx = 0;
        e->gemini_cdy = 0;
        e->timer = 80;
        e->shoot_timer = 70;
        e->attack_pattern = 0;
        break;
    case ENEMY_BOSS_IT_LIVES:
        /* It Lives (R9 M2): Mom's Heart AI skeleton, hotter timers.
           famine_shoot_cd doubles as the last-quarter radial-ring timer. */
        e->phase = 0;
        e->dx = 0; e->dy = 0;
        e->timer = 60;
        e->shoot_timer = 50;
        e->attack_pattern = 0;
        e->famine_shoot_cd = 60;
        break;
    case ENEMY_BOSS_URIEL:
    case ENEMY_BOSS_GABRIEL:
        /* R8 (M3): angel minibosses. Shared AI, two tuning sets (Gabriel
           faster + denser; see the AI case). e->state 0 = hover-chase. */
        e->phase = 0;
        e->state = 0;
        e->dx = 0; e->dy = 0;
        e->timer = 90;                       /* light-column cooldown */
        e->shoot_timer = (selected_boss == ENEMY_BOSS_GABRIEL) ? 55 : 80;
        e->attack_pattern = 0;
        break;
    case ENEMY_BOSS_KRAMPUS:
        /* R8 (M7): chase + 4-way brimstone cross + coal-lob volleys. */
        e->phase = 0;
        e->dx = 0; e->dy = 0;
        e->timer = 100;                      /* cross cooldown */
        e->shoot_timer = 70;                 /* coal-lob cooldown */
        e->attack_pattern = 0;
        break;
    case ENEMY_BOSS_BLUE_BABY:
        /* R8 (M3): ??? — Isaac's hop/ring skeleton, dark mirror: no light
           columns, denser tear rings, extra aimed volleys at low HP. */
        e->phase = 0;
        e->state = 1;            /* opens hovering (contact ON — no prayer) */
        e->attack_pattern = 0;
        e->dx = 0; e->dy = 0;
        e->timer = 60;
        e->shoot_timer = 80;
        break;
    default: break;
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
        /* Place an item pedestal (E9 "The Purist": no pedestals all run) */
        r->pedestal.x = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
        r->pedestal.y = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
        r->pedestal.item = pick_random_item(g);
        r->pedestal.active = (g->challenge != 6);
        return;
    }

    if (r->type == ROOM_SHOP) {
        r->enemy_count = 0;
        r->cleared = 1;
        /* T3 "The Purist": no ITEMS anywhere — the shop stocks no item
           pedestals; consumables stay available (heart/key/bomb pickups
           below plus the usual pill/card sprinkles). */
        if (g->challenge == 6) {
            r->shop_count = 0;
            float pcx = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
            float pcy = (ROOM_TOP + ROOM_BOTTOM) / 2.0f - 10;
            spawn_heart(r, pcx - 40, pcy, HEART_RED_FULL);
            spawn_consumable(r, pcx, pcy, PICKUP_KEY);
            spawn_consumable(r, pcx + 40, pcy, PICKUP_BOMB);
            if (randi(0, 100) < 60) {
                spawn_pill_pickup(r, ROOM_LEFT + 40, ROOM_BOTTOM - 40,
                                  randi(0, PILL_EFFECT_COUNT - 1));
            }
            if (randi(0, 100) < 60) {
                spawn_card_pickup(r, ROOM_RIGHT - 40, ROOM_BOTTOM - 40,
                                  randi(0, TAROT_COUNT - 1));
            }
            return;
        }
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
        r->is_black_market = 0;

        /* Black market upgrade: low base chance, boosted by luck. Turns the
           secret room into a mini paid shop (2-3 pedestals at a premium
           price) instead of the usual free pickups.
           T3 "The Purist" (challenge 6): never upgrades — items are gated,
           so keep the consumable loot (hearts/keys/bombs) path below. */
        if (g->challenge != 6 && luck_roll(g, 16, 2)) {
            r->is_black_market = 1;
            r->shop_count = randi(2, MAX_SHOP_ITEMS);
            float shopX_start = ROOM_LEFT + 60;
            float shopSpacing = (ROOM_RIGHT - ROOM_LEFT - 120) /
                                 (float)(r->shop_count > 1 ? r->shop_count - 1 : 1);
            ItemType shop_used[MAX_SHOP_ITEMS];
            for (int i = 0; i < r->shop_count; i++) {
                ShopItem *si = &r->shop_items[i];
                si->x = shopX_start + i * shopSpacing;
                si->y = (ROOM_TOP + ROOM_BOTTOM) / 2.0f - 10;
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
                /* Black market premium: costlier than a normal shop */
                int base_cost = randi(SHOP_ITEM_COST_MIN + 3, SHOP_ITEM_COST_MAX + 5);
                si->cost = (int)(base_cost * diff_shop_price_mult(g->difficulty));
                if (si->cost < 1) si->cost = 1;
                si->active = 1;
            }
            return;
        }

        /* Secret rooms always have good loot; deeper floors and higher luck
           add more/better pickups on top of the guaranteed base trio. */
        float scx = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
        float scy = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
        spawn_consumable(r, scx - 15, scy, PICKUP_BOMB2);
        spawn_consumable(r, scx + 15, scy, PICKUP_COIN5);
        spawn_consumable(r, scx, scy - 15, PICKUP_KEY);

        /* Bonus coins scale with floor depth */
        if (g->current_floor >= 2) {
            spawn_consumable(r, scx + 30, scy + 20, PICKUP_COIN5);
        }
        if (luck_roll(g, 10 + g->current_floor * 6, 3)) {
            spawn_random_consumable(r, scx - 30, scy + 20);
        }
        /* Chance at a chest, better odds deeper in the run */
        if (luck_roll(g, g->current_floor * 8, 3)) {
            if (luck_roll(g, g->current_floor * 5, 2))
                spawn_consumable(r, scx, scy + 30, PICKUP_CHEST_GOLD);
            else
                spawn_consumable(r, scx, scy + 30, PICKUP_CHEST);
        }
        return;
    }

    if (r->type == ROOM_CURSE) {
        r->enemy_count = 0;
        r->cleared = 1;
        /* Curse rooms have a good item pedestal (Purist: none) */
        r->pedestal.x = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
        r->pedestal.y = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
        r->pedestal.item = pick_random_item(g);
        r->pedestal.active = (g->challenge != 6);
        /* R8 (M5): curse rooms stock 1-2 red chests (replacing the old
           coin nickel — risk room, risk loot) plus the key. */
        spawn_consumable(r, ROOM_LEFT + 60, ROOM_TOP + 60, PICKUP_CHEST_RED);
        if (randi(0, 1))
            spawn_consumable(r, ROOM_LEFT + 60, ROOM_BOTTOM - 50,
                             PICKUP_CHEST_RED);
        spawn_consumable(r, ROOM_RIGHT - 60, ROOM_TOP + 60, PICKUP_KEY);
        return;
    }

    if (r->type == ROOM_ANGEL) {
        r->enemy_count = 0;
        r->cleared = 1;
        /* Free item pedestal + 2 soul hearts, no enemies (Purist: none) */
        r->pedestal.x = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
        r->pedestal.y = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
        r->pedestal.item = pick_random_item(g);
        r->pedestal.active = (g->challenge != 6);
        spawn_heart(r, ROOM_LEFT + 60, ROOM_TOP + 60, HEART_SOUL);
        spawn_heart(r, ROOM_RIGHT - 60, ROOM_TOP + 60, HEART_SOUL);
        /* R8 (M3): central angel statue above the pedestal. Bombing it (or
           room-wide damage) awakens Uriel/Gabriel — see awaken_angel. */
        if (r->obstacle_count < MAX_OBSTACLES) {
            Obstacle *st = &r->obstacles[r->obstacle_count++];
            st->x = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
            st->y = ROOM_TOP + 44.0f;
            st->type = OBST_ANGEL_STATUE;
            st->hp = 1;
            st->active = 1;
        }
        return;
    }

    if (r->type == ROOM_SACRIFICE) {
        r->enemy_count = 0;
        r->cleared = 1;
        r->sacrifice_hits = 0;
        r->sacrifice_rewarded = 0;
        /* Central spikes obstacle; hitting them repeatedly grants a reward */
        r->obstacle_count = 0;
        Obstacle *o = &r->obstacles[r->obstacle_count++];
        o->x = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
        o->y = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
        o->type = OBST_SPIKES;
        o->hp = 1;
        o->active = 1;
        return;
    }

    if (r->type == ROOM_BOSSRUSH) {
        /* No enemies at spawn time -- the wave controller (in enemies_update)
           drives spawning wave-by-wave once the player enters. Room starts
           uncleared; it is guaranteed to clear once the wave machine finishes
           (see the anti-softlock safety net in enemies_update). */
        r->enemy_count = 0;
        r->cleared = 0;
        return;
    }

    if (r->type == ROOM_ARCADE) {
        r->enemy_count = 0;
        r->cleared = 1;
        r->arcade_slot_used = 0;
        /* Central "slot machine" obstacle: pay a coin, roll a reward */
        r->obstacle_count = 0;
        Obstacle *slot = &r->obstacles[r->obstacle_count++];
        slot->x = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
        slot->y = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
        slot->type = OBST_SLOT_MACHINE;
        slot->hp = 1;
        slot->active = 1;
        return;
    }

    if (r->type == ROOM_LIBRARY) {
        r->enemy_count = 0;
        r->cleared = 1;
        /* One free item pedestal biased toward active items -- no cost,
           no enemies. */
        r->pedestal.x = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
        r->pedestal.y = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
        r->pedestal.item = pick_random_active_item(g);
        r->pedestal.active = (g->challenge != 6);   /* Purist: none */
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
        int bad_spot;
        do {
            px = randf(ROOM_LEFT + 30, ROOM_RIGHT - 30);
            py = randf(ROOM_TOP + 30, ROOM_BOTTOM - 30);
            safety++;
            /* Reject spawns near the room centre (warp landing) and near
               all 4 door mouths (door landing) so entering never means
               materialising on top of an enemy. */
            bad_spot = (fabsf(px - cx) < 50 && fabsf(py - cy) < 50);
            if (!bad_spot) {
                float doorpt[4][2] = {
                    {cx, (float)ROOM_TOP},  {cx, (float)ROOM_BOTTOM},
                    {(float)ROOM_LEFT, cy}, {(float)ROOM_RIGHT, cy}
                };
                for (int dpi = 0; dpi < 4; dpi++) {
                    float ddx = px - doorpt[dpi][0];
                    float ddy = py - doorpt[dpi][1];
                    if (ddx * ddx + ddy * ddy < 60.0f * 60.0f) {
                        bad_spot = 1;
                        break;
                    }
                }
            }
        } while (bad_spot && safety < 20);

        e->x = px;
        e->y = py;

        if (r->type == ROOM_BOSS) {
            /* === Random Boss Selection from Pool === */
            EnemyType selected_boss = select_boss_for_floor(g, g->current_floor);

            /* Store the selected boss type and record in history */
            g->current_boss_type = selected_boss;
            push_boss_history(g, selected_boss);

            init_boss_enemy(g, fi, e, selected_boss, cx);
        } else {
            /* Floor-tiered enemy pool selection */
            int t;
            if (g->current_floor >= 3) {
                t = randi(0, 23);  /* all enemy types + Trite/Fatty/Charger (17-19) + Keeper/Sucker (20-21)
                                       + RoundWorm/Spitty (22-23) */
            } else if (g->current_floor >= 2) {
                /* Caves I: tiers 0-2 (0-12, incl. Maw/Mulligan) plus the
                   variants at 17-23 */
                t = randi(0, 19);
                if (t > 12) t += 4;  /* 13->17, 14->18, ..., 19->23 */
            } else if (g->current_floor >= 1) {
                t = randi(0, 8);   /* + tier 1 (Attack Fly, Pooter, Hopper, Baby) */
            } else {
                t = randi(0, 4);   /* base: Fly, Gaper, Pacer, Spider, Clotty */
            }

            /* R9: Cathedral (floor 6, light route) reads angelic — bias
               about half the spawns toward flies / hosts / babies. */
            if (g->current_floor == 6 && g->route == 1 && randi(0, 99) < 50) {
                const int holy[4] = { 0 /*Fly*/, 5 /*Attack Fly*/,
                                      8 /*Baby*/, 13 /*Host*/ };
                t = holy[randi(0, 3)];
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
            /* --- Tier 2: Caves+ --- */
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
            /* --- Tier 3: Caves II / Depths --- */
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
            /* --- New variant enemies (reachable floor 2+) --- */
            case 17:
                e->type = ENEMY_TRITE;
                e->hp = 5 + fi->enemy_hp_bonus;
                e->max_hp = e->hp;
                e->timer = randi(20, 45);
                e->jump_arc = 0.0f;
                break;
            case 18:
                e->type = ENEMY_FATTY;
                e->hp = 8 + fi->enemy_hp_bonus * 2;  /* tanky: ~2x a gaper */
                e->max_hp = e->hp;
                break;
            case 19:
                e->type = ENEMY_CHARGER;
                e->hp = 4 + fi->enemy_hp_bonus;
                e->max_hp = e->hp;
                e->dx = (randi(0, 1) == 0) ? 1.2f : -1.2f;
                e->dx *= fi->enemy_speed_mult;
                e->dy = 0;
                break;
            case 20:
                e->type = ENEMY_KEEPER;
                e->hp = 5 + fi->enemy_hp_bonus;
                e->max_hp = e->hp;
                e->timer = randi(15, 40);
                break;
            case 21:
                e->type = ENEMY_SUCKER;
                e->hp = 5 + fi->enemy_hp_bonus;
                e->max_hp = e->hp;
                e->timer = randi(40, 80);
                e->shoot_timer = randi(70, 110);
                break;
            /* --- New enemies (batch: more enemies + boss) --- */
            case 22:
                e->type = ENEMY_ROUND_WORM;
                e->hp = 5 + fi->enemy_hp_bonus;
                e->max_hp = e->hp;
                e->phase = 1;      /* 0=emerged (vulnerable, chasing), 1=burrowed */
                e->hidden = 1;
                e->timer = randi(50, 90);
                e->dx = 0; e->dy = 0;
                break;
            case 23:
                e->type = ENEMY_SPITTY;
                e->hp = 4 + fi->enemy_hp_bonus;
                e->max_hp = e->hp;
                e->shoot_timer = randi(50, 100);
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
            e->hp = e->hp * hp_mult;
            if (e->hp < 1) e->hp = 1;
            e->max_hp = e->hp;
            /* R8 #19: keep Gemini/Steven's second-head HP proportional so
               the healthbar's max_hp/3 head share stays accurate. */
            e->gemini_chp = (int)((float)e->gemini_chp * hp_mult);
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
            e->hp = e->max_hp = e->max_hp * 2.0f;
            break;
        case CHAMP_BLUE:
            e->hp = e->max_hp = e->max_hp * 1.5f;
            e->dx *= 1.3f;
            e->dy *= 1.3f;
            break;
        case CHAMP_YELLOW:
            e->hp = e->max_hp = e->max_hp * 1.5f;
            e->creep_drop_timer = 30;
            break;
        case CHAMP_BLACK:
            e->hp = e->max_hp = e->max_hp * 2.0f;
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
    g_floor_route = 0;   /* R9: keep the get_floor_info mirror in sync */
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
    int saved_challenge = g->challenge;

    memset(g, 0, sizeof(Game));

    g->game_mode = saved_mode;
    g->difficulty = saved_diff;
    g->selected_character = saved_char;
    g->challenge = saved_challenge;
    g->state = STATE_PLAYING;
    g->score = 0;
    g->current_floor = 0;
    set_route(g, 0);   /* R9: fresh run — route undecided until Mom's Heart */
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
    g->player.active_item = ITEM_NONE;
    g->player.active_charge = 0;
    g->player.active_max_charge = 0;
    g->player.soul_hp = 0;
    g->player.trinket = TRINKET_NONE;
    g->player.has_pill = 0;
    g->player.has_card = 0;

    /* Apply character starting bonuses (items + adjust HP to max) */
    apply_character_start(g);

    /* Challenge modifiers (mirrored onto the player so recalc sees them) */
    g->player.challenge = g->challenge;
    if (g->challenge) {
        recalc_player_stats(&g->player);
        if (g->player.hp > g->player.stats.max_hp)
            g->player.hp = g->player.stats.max_hp;
    }

    recalc_player_stats(&g->player);

    /* Set HP to max for character */
    g->player.hp = g->player.stats.max_hp;
    /* R10 (C4): The Lost never gains health — easy mode must not smuggle
       him 2 hp (that would break his whole identity). */
    if (g->difficulty == DIFF_EASY && g->player.character != CHAR_LOST) {
        g->player.hp += 2;
        if (g->player.hp > PLAYER_MAX_HP_CAP) g->player.hp = PLAYER_MAX_HP_CAP;
        if (g->player.hp > g->player.stats.max_hp) g->player.stats.max_hp = g->player.hp;
    }
    /* R10 (C4): seed the hp-conditional stat watcher (Eve/Samson) */
    g->prev_hp_total = g->player.hp + g->player.soul_hp + g->player.black_hp;

    /* Randomize pill color->effect mapping for this run */
    for (int i = 0; i < PILL_EFFECT_COUNT; i++) g->pill_color_map[i] = i;
    for (int i = PILL_EFFECT_COUNT - 1; i > 0; i--) {
        int j = randi(0, i);
        int tmp = g->pill_color_map[i];
        g->pill_color_map[i] = g->pill_color_map[j];
        g->pill_color_map[j] = tmp;
    }
    for (int i = 0; i < PILL_EFFECT_COUNT; i++) g->pill_known[i] = 0;

    /* Roll a curse for the starting floor (30% chance). Rolled before
     * dungeon_generate so Curse of the Labyrinth can influence layout size. */
    roll_curse(g);

    dungeon_generate(&g->dungeon, g->current_floor);

    g->player.x = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
    g->player.y = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;

    Room *r = current_room(g);
    room_spawn_enemies(g, r);

    /* E6: seed the familiar trail at the spawn position */
    familiars_reset_trail(g);
    g_black_burst_pending = 0;

    /* E9 challenge 5 "Cat Got Your Tongue": no tears — start with the
       Demon Baby + Brother Bobby familiars as the only weapons. */
    if (g->challenge == 5) {
        g->player.familiar[0] = ITEM_DEMON_BABY;
        g->player.familiar[1] = ITEM_BROTHER_BOBBY;
    }

    /* E9 challenge 6 "The Purist": the final floor's boss room becomes a
       Boss Rush gauntlet (its clear grants the winning trapdoor). Floor 0
       never qualifies, but keep the check parallel with advance_floor. */
    if (g->challenge == 6 && g->current_floor == MAX_FLOORS - 1) {
        Dungeon *pd = &g->dungeon;
        pd->rooms[pd->boss_y][pd->boss_x].type = ROOM_BOSSRUSH;
    }

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
            /* +1 heart container, Yum Heart starter (active item) */
            collect_item(g, ITEM_YUM_HEART);
            break;
        case CHAR_CAIN:
            /* Lucky Foot starter, missing eye visual (through collect_item
               so the luck stat is recalculated immediately) */
            collect_item(g, ITEM_LUCKY_FOOT);
            break;
        case CHAR_JUDAS:
            /* Book of Belial starter (active item) */
            collect_item(g, ITEM_BOOK_OF_BELIAL);
            break;
        case CHAR_EVE:
            /* Starts with 1 soul heart */
            p->soul_hp = 2;   /* soul_hp is in half-heart units */
            break;
        case CHAR_SAMSON:
            /* Flat stat start, no special starting item */
            break;
        case CHAR_BLUE_BABY:
            /* 2 red heart containers + 4 soul hearts (safe option, avoids
             * 0-red-heart edge cases in HUD/heal code) */
            p->soul_hp = 8;   /* soul_hp is in half-heart units: 4 hearts = 8 */
            break;
        /* --- R10 (C4) characters --- */
        case CHAR_AZAZEL:
            /* 2 red containers (recalc) + 1 black heart. Isaac-authentic is
               3 black / 0 red, but this demake's healing economy is red-
               heart-centric — 0 red containers would be Lost-tier brutal. */
            p->black_hp = 2;  /* black_hp is in half-heart units */
            break;
        case CHAR_LAZARUS:
            p->lives = 1;     /* one free resurrection (Lazarus rises) */
            break;
        case CHAR_LOST:
            /* Arm the innate Holy Mantle for the starting room (recalc's
               ITEM_FLAG_MANTLE handles every later room entry). */
            p->holy_mantle_active = 1;
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
            /* Infinite mode: loop back to floor 0 with increased scaling.
               R9: the route resets too so each loop re-chooses at Mom's
               Heart / It Lives. */
            g->current_floor = 0;
            g->infinite_loop++;
            set_route(g, 0);
        } else {
            /* Story mode: game won — the ending depends on the route.
               LIGHT (or legacy no-route): The Chest / Mega Satan escape
               (ending 0, unchanged). DARK: The Lamb was defeated in the
               Dark Room (ending 3, crowned in darkness). */
            g->win_ending = (g->route == 2) ? 3 : 0;
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

    /* R10 (C4) Samson: new floor = new room — Bloody Lust stacks reset */
    if (g->player.samson_hits) {
        g->player.samson_hits = 0;
        recalc_player_stats(&g->player);
    }

    /* Roll a possible curse for this floor (30% chance). Rolled before
     * dungeon_generate so Curse of the Labyrinth can influence layout size. */
    roll_curse(g);

    /* Generate new dungeon for new floor */
    dungeon_generate(&g->dungeon, g->current_floor);

    /* E9 challenge 6 "The Purist": final floor's boss room is a Boss Rush
       gauntlet instead (its clear grants the winning trapdoor). */
    if (g->challenge == 6 && g->current_floor == MAX_FLOORS - 1) {
        Dungeon *pd = &g->dungeon;
        pd->rooms[pd->boss_y][pd->boss_x].type = ROOM_BOSSRUSH;
    }

    /* Reset tears and enemy shots */
    for (int i = 0; i < MAX_TEARS; i++)
        g->tears[i].active = 0;
    for (int i = 0; i < MAX_ENEMY_SHOTS; i++)
        g->enemy_shots[i].active = 0;

    /* Clear blood particles */
    for (int i = 0; i < MAX_BLOOD_PARTICLES; i++)
        g->blood[i].active = 0;

    /* Reset bombs */
    for (int i = 0; i < MAX_BOMBS; i++)
        g->bombs[i].active = 0;

    /* Reset beam weapons + knife (Phase D) */
    g->laser_active = 0;
    g->laser_timer = 0;
    g->laser_charge = 0;
    g->knife_state = 0;
    g->knife_hit_cd = 0;

    /* Reset boss state */
    g->boss_active = 0;
    g->boss_death_anim = 0;
    g->boss_intro_timer = 0;

    /* E3: no enemy beam survives a floor change (R9: nor the Lamb's cross
       flag or Isaac's light columns) */
    g->ebeam_state = 0;
    g->ebeam_timer = 0;
    g->ebeam_cross = 0;
    g->vbeam_state = 0;
    g->vbeam_timer = 0;
    g->vbeam_count = 0;

    /* Place player in center */
    g->player.x = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
    g->player.y = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
    g->player.iframes = 30;

    /* E6: snap the familiar trail to the new floor's spawn */
    familiars_reset_trail(g);

    /* Safety: guarantee at least 1 key per floor so locked doors
     * are never a permanent softlock even in edge cases */
    if (g->player.keys < 1) g->player.keys = 1;

    Room *r = current_room(g);
    room_spawn_enemies(g, r);

    /* Play floor-appropriate music (crossfades if track changes) */
    music_play(music_for_floor(g->current_floor));
}

/* ================================================================
 * Player update
 * ================================================================ */

/* Check if a circle at (x,y) with given radius overlaps any obstacle in the room */
/* R8 (M8): flight (Guppy) skips rocks/poop — walls always block, and the
 * interactive props (slot machine, angel statue) stay solid so they can't
 * be flown through and forgotten. */
static int player_blocked_at(Room *r, float x, float y, int flight) {
    if (x < ROOM_LEFT + PLAYER_SIZE) return 1;
    if (x > ROOM_RIGHT - PLAYER_SIZE) return 1;
    if (y < ROOM_TOP + PLAYER_SIZE) return 1;
    if (y > ROOM_BOTTOM - PLAYER_SIZE) return 1;
    for (int i = 0; i < r->obstacle_count; i++) {
        Obstacle *o = &r->obstacles[i];
        if (!o->active) continue;
        if (o->type == OBST_SPIKES) continue;   /* spikes never block movement */
        if (flight && (o->type == OBST_ROCK || o->type == OBST_POOP))
            continue;                           /* Guppy flies over these */
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

    if (hasInput) {
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

    /* R8 (M8): Guppy flight passes over rocks/poop */
    int flight = (p->stats.flags & ITEM_FLAG_FLIGHT) != 0;

    /* Try X movement */
    float nx = p->x + p->vx;
    if (!player_blocked_at(r, nx, p->y, flight)) {
        p->x = nx;
    } else {
        /* Slide along wall: zero only the blocked axis */
        p->vx = 0.0f;
    }

    /* Try Y movement */
    float ny = p->y + p->vy;
    if (!player_blocked_at(r, p->x, ny, flight)) {
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

        /* Update facing only from intentional input */
        if (hasInput) {
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
            /* Only consume the pedestal if the item was actually granted
               (collect_item refuses passives at the 32-item cap). */
            if (collect_item(g, r->pedestal.item)) {
                g->last_pickup = r->pedestal.item;
                g->pickup_flash = PICKUP_FLASH_FRAMES;
                p->pickup_anim = 40;  /* show pickup pose for ~0.7s */
                r->pedestal.active = 0;
                g->score += 50;
                audio_play(SFX_PICKUP);
            }
        }
    }

    /* Heart pickup */
    for (int i = 0; i < MAX_HEART_PICKUPS; i++) {
        HeartPickup *h = &r->hearts[i];
        if (!h->active) continue;
        
        float hdx = p->x - h->x;
        float hdy = p->y - h->y;
        if (hdx * hdx + hdy * hdy < (PLAYER_SIZE + 8) * (PLAYER_SIZE + 8)) {
            if (h->type == HEART_SOUL) {
                /* Soul heart: bonus HP consumed before red hearts.
                   (Gain path was missing — soul hearts previously healed 0.)
                   R10 (C4): The Lost can hold NO health of any color. */
                if (p->character != CHAR_LOST && p->soul_hp < 12) {   /* cap at 6 blue hearts */
                    p->soul_hp += 2;
                    if (p->soul_hp > 12) p->soul_hp = 12;
                    h->active = 0;
                    r->heart_count--;
                    audio_play(SFX_PICKUP);
                }
            } else if (h->type == HEART_BLACK) {
                /* E5 black heart: absorbs like a soul heart but BEFORE soul
                   hearts; depleting one nukes the room. Refused at cap
                   (and by The Lost, who can hold no health — R10 C4). */
                if (p->character != CHAR_LOST && p->black_hp < 12) {  /* cap at 6 black hearts */
                    p->black_hp += 2;
                    if (p->black_hp > 12) p->black_hp = 12;
                    h->active = 0;
                    r->heart_count--;
                    audio_play(SFX_PICKUP);
                }
            } else {
                /* Red hearts heal, only if player is wounded */
                int heal_amount = (h->type == HEART_RED_FULL) ? 2 : 1;
                if (p->hp < p->stats.max_hp) {
                    p->hp += heal_amount;
                    if (p->hp > p->stats.max_hp) p->hp = p->stats.max_hp;
                    h->active = 0;
                    r->heart_count--;
                    audio_play(SFX_PICKUP);
                }
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
            int warped = 0;   /* R8 (M5): red chest devil warp mid-loop */
            switch (c->type) {
            case PICKUP_BOMB:   p->bombs += 1; break;
            case PICKUP_BOMB2:  p->bombs += 2; break;
            case PICKUP_KEY:    p->keys  += 1; break;
            case PICKUP_COIN:   p->coins += 1; break;
            case PICKUP_COIN5:  p->coins += 5; break;
            case PICKUP_KEY5:   p->keys  += 5; break;
            case PICKUP_BATTERY:
                if (p->active_item != ITEM_NONE) {
                    /* Refill active item charge to full */
                    p->active_charge = p->active_max_charge;
                } else picked_up = 0;  /* nothing to charge: leave it */
                break;
            case PICKUP_PILL:
                if (!p->has_pill) {
                    p->has_pill = 1;
                    p->held_pill = (PillEffect)c->sub_type;
                    /* Effect name if identified this run, else the color of
                       the capsule actually drawn (via pill_color_map) */
                    {
                        PillEffect pe = (PillEffect)c->sub_type;
                        const char *pn = g->pill_known[pe]
                            ? pill_name(pe, 1)
                            : pill_color_name(g->pill_color_map[pe]);
                        snprintf(g->pickup_msg_text, sizeof(g->pickup_msg_text),
                                 "Pill: %s", pn);
                    }
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
            case PICKUP_TRINKET: {
                int newT = c->sub_type;
                if (p->trinket != TRINKET_NONE) {
                    /* Swap: drop the held trinket just out of grab range */
                    spawn_trinket_pickup(r, c->x + 30, c->y, p->trinket);
                }
                p->trinket = newT;
                recalc_player_stats(p);
                snprintf(g->pickup_msg_text, sizeof(g->pickup_msg_text),
                         "Trinket: %s", trinket_name(newT));
                g->pickup_msg_timer = 180;
                break;
            }
            case PICKUP_CHEST: {
                /* Wooden chest: pops open into a small loot burst */
                int n = randi(2, 3);
                for (int ci2 = 0; ci2 < n; ci2++)
                    spawn_random_consumable(r, c->x + randf(-22, 22),
                                            c->y + randf(-16, 16));
                if (randi(0, 99) < 40)
                    spawn_heart(r, c->x + randf(-18, 18),
                                c->y + randf(-14, 14), HEART_RED_HALF);
                spawn_tear_pop(g, c->x, c->y, 0);
                break;
            }
            case PICKUP_CHEST_GOLD:
                if (p->keys > 0) {
                    /* Gold chest: costs a key, guaranteed good loot */
                    p->keys--;
                    if (randi(0, 1))
                        spawn_pill_pickup(r, c->x + randf(-22, 22),
                                          c->y + randf(-14, 14),
                                          randi(0, PILL_EFFECT_COUNT - 1));
                    else
                        spawn_card_pickup(r, c->x + randf(-22, 22),
                                          c->y + randf(-14, 14),
                                          randi(0, TAROT_COUNT - 1));
                    spawn_consumable(r, c->x + randf(-22, 22),
                                     c->y + randf(-16, 16), PICKUP_COIN5);
                    spawn_heart(r, c->x + randf(-18, 18), c->y + randf(-14, 14),
                                randi(0, 99) < 25 ? HEART_SOUL : HEART_RED_FULL);
                    /* 20% bonus trinket in gold chests */
                    if (randi(0, 99) < 20)
                        spawn_trinket_pickup(r, c->x + randf(-24, 24),
                                             c->y + randf(-16, 16),
                                             randi(1, TRINKET_COUNT - 1));
                    spawn_tear_pop(g, c->x, c->y, 0);
                } else {
                    picked_up = 0;
                    if (g->shop_deny_timer <= 0) {
                        audio_play(SFX_TEAR_BLOCK);   /* locked: need a key */
                        g->shop_deny_timer = 45;
                    }
                }
                break;
            case PICKUP_CHEST_RED: {
                /* R8 (M5): free to open (walk over). Weighted risk/reward
                   roll; the opened husk stays visible (sub_type = 1). */
                if (c->sub_type == 1) { picked_up = 0; break; }
                c->sub_type = 1;
                picked_up = 0;   /* husk persists — skip despawn bookkeeping */
                audio_play(SFX_PICKUP);
                spawn_tear_pop(g, c->x, c->y, 0);
                int rroll = randi(0, 99);
                if (rroll < 35) {
                    /* 35%: 1-3 spider ambush (dynamic spawn with grace) */
                    const FloorInfo *cfi = get_floor_info(g->current_floor);
                    int sn = randi(1, 3);
                    for (int si3 = 0; si3 < sn; si3++) {
                        Enemy *se = alloc_dynamic_enemy(r);
                        if (!se) break;
                        se->type = ENEMY_SPIDER;
                        se->x = c->x + randf(-16, 16);
                        se->y = c->y + randf(-12, 12);
                        se->hp = 3 + cfi->enemy_hp_bonus;
                        se->max_hp = se->hp;
                        se->timer = randi(10, 30);
                        se->dx = randf(-2.0f, 2.0f) * cfi->enemy_speed_mult;
                        se->dy = randf(-2.0f, 2.0f) * cfi->enemy_speed_mult;
                    }
                    r->cleared = 0;   /* a real ambush — fight it out */
                    trigger_shake(g, 2.0f, 10);
                } else if (rroll < 55) {
                    /* 20%: troll bomb */
                    spawn_troll_bomb(g, c->x + randf(-8, 8),
                                     c->y + randf(-6, 6));
                } else if (rroll < 70) {
                    /* 15%: black heart */
                    spawn_heart(r, c->x, c->y - 18, HEART_BLACK);
                } else if (rroll < 85) {
                    /* 15%: 2-4 coins */
                    int cn2 = randi(2, 4);
                    for (int ci3 = 0; ci3 < cn2; ci3++)
                        spawn_consumable(r, c->x + randf(-22, 22),
                                         c->y + randf(-16, 16), PICKUP_COIN);
                } else if (rroll < 95) {
                    /* 10%: devil-pool item pedestal (rooms own ONE pedestal
                       slot — if it's taken, pay out a nickel instead) */
                    if (!r->pedestal.active) {
                        r->pedestal.x = c->x;
                        r->pedestal.y = clampf(c->y - 26,
                                               ROOM_TOP + 24, ROOM_BOTTOM - 24);
                        r->pedestal.item =
                            (!player_has_item(p, ITEM_THE_PACT) &&
                             randi(0, 99) < 40)
                                ? ITEM_THE_PACT : pick_random_item(g);
                        r->pedestal.active = (g->challenge != 6); /* Purist */
                    } else {
                        spawn_consumable(r, c->x, c->y - 16, PICKUP_COIN5);
                    }
                } else {
                    /* 5%: teleport to this floor's devil room. Devil rooms
                       are created lazily post-boss, so one may not exist —
                       fallback is a black heart (flagged design choice;
                       creating a devil room from here would need a free
                       adjacent grid cell + door surgery). */
                    int dfound = 0;
                    for (int dy2 = 0; dy2 < DUNGEON_H && !dfound; dy2++) {
                        for (int dx2 = 0; dx2 < DUNGEON_W && !dfound; dx2++) {
                            if (g->dungeon.rooms[dy2][dx2].type == ROOM_DEVIL) {
                                drain_black_burst(g);
                                g->dungeon.cur_x = dx2;
                                g->dungeon.cur_y = dy2;
                                do_warp_cleanup(g);
                                dfound = 1;
                                warped = 1;
                            }
                        }
                    }
                    if (!dfound)
                        spawn_heart(r, c->x, c->y - 18, HEART_BLACK);
                }
                break;
            }
            default: break;
            }
            if (p->coins > 99) p->coins = 99;
            if (p->bombs > 99) p->bombs = 99;
            if (p->keys  > 99) p->keys  = 99;
            if (picked_up) {
                c->active = 0;
                r->consumable_count--;
                audio_play(SFX_PICKUP);
            }
            /* R8 (M5): the red chest warped us to another room — every
               pointer into the old room is stale; stop this frame's
               collision pass here. */
            if (warped) return;
        }
    }

    /* Shop / devil-deal / black-market item purchase (walk into item to buy).
       Black market is a secret room flagged via is_black_market rather than
       a distinct RoomType, so it reuses the ROOM_SHOP purchase path (paid in
       coins, same as a normal shop). */
    if (r->type == ROOM_SHOP || r->type == ROOM_DEVIL || r->is_black_market) {
        if (g->shop_deny_timer > 0) g->shop_deny_timer--;
        for (int i = 0; i < r->shop_count; i++) {
            ShopItem *si = &r->shop_items[i];
            if (!si->active) continue;

            float sdx = p->x - si->x;
            float sdy = p->y - (si->y + 5);  /* match pedestal visual center */
            if (sdx * sdx + sdy * sdy < (PLAYER_SIZE + 12) * (PLAYER_SIZE + 12)) {
                if (r->type == ROOM_DEVIL) {
                    /* Devil deal: pay with heart containers (permanent) */
                    int hpCost = si->cost * 2;
                    if (p->character == CHAR_LOST) {
                        /* R10 (C4) The Lost: devil deals are FREE — he has
                           no health to pay with. Covers both the heart-
                           container and Dead-Cat-lives payment branches. */
                        if (collect_item(g, si->item)) {
                            g->took_devil_deal = 1;
                            g_config.devil_deals_taken++;
                            apply_unlock_gates();
                            config_save(&g_config);
                            g->last_pickup = si->item;
                            g->pickup_flash = PICKUP_FLASH_FRAMES;
                            p->pickup_anim = 40;
                            si->active = 0;
                            g->score += 66;
                            audio_play(SFX_PICKUP);
                        } else if (g->shop_deny_timer <= 0) {
                            audio_play(SFX_HURT);
                            g->shop_deny_timer = 30;
                        }
                    } else if (player_has_item(p, ITEM_DEAD_CAT)) {
                        /* Dead Cat floors max_hp to 2, so heart-container payment
                           is impossible. Pay a spare life per heart of cost. */
                        int lifeCost = si->cost;
                        if (p->lives >= lifeCost && collect_item(g, si->item)) {
                            p->lives -= lifeCost;
                            g->took_devil_deal = 1;  /* R8: angels stop appearing */
                            g_config.devil_deals_taken++;   /* R10 (C4) Azazel gate */
                            apply_unlock_gates();
                            config_save(&g_config);
                            g->last_pickup = si->item;
                            g->pickup_flash = PICKUP_FLASH_FRAMES;
                            p->pickup_anim = 40;
                            si->active = 0;
                            g->score += 66;
                            g->hurt_flash_timer = HURT_FLASH_FRAMES;
                            audio_play(SFX_HURT_GRUNT);
                        } else if (g->shop_deny_timer <= 0) {
                            audio_play(SFX_HURT);
                            g->shop_deny_timer = 30;
                        }
                    } else if (p->stats.max_hp - hpCost >= 2 &&
                               collect_item(g, si->item)) {
                        /* Item granted (cap not hit) — now take the payment.
                           Burn over-cap pill HP first so the heart payment is
                           always real — recalc's cap clamp used to swallow the
                           whole debit for over-cap players. */
                        if (p->pill_max_hp_bonus > 0) {
                            int saved = p->pill_max_hp_bonus;
                            p->pill_max_hp_bonus = 0;
                            recalc_player_stats(p);
                            int head = PLAYER_MAX_HP_CAP - p->stats.max_hp;
                            p->pill_max_hp_bonus = (saved > head) ? head : saved;
                        }
                        p->pill_max_hp_bonus -= hpCost;  /* persistent debit */
                        recalc_player_stats(p);
                        if (p->hp > p->stats.max_hp) p->hp = p->stats.max_hp;
                        g->took_devil_deal = 1;  /* R8: angels stop appearing */
                        g_config.devil_deals_taken++;   /* R10 (C4) Azazel gate */
                        apply_unlock_gates();
                        config_save(&g_config);
                        g->last_pickup = si->item;
                        g->pickup_flash = PICKUP_FLASH_FRAMES;
                        p->pickup_anim = 40;
                        si->active = 0;
                        g->score += 66;
                        g->hurt_flash_timer = HURT_FLASH_FRAMES; /* it hurts */
                        audio_play(SFX_HURT_GRUNT);
                    } else if (g->shop_deny_timer <= 0) {
                        audio_play(SFX_HURT);
                        g->shop_deny_timer = 30;
                    }
                } else if (p->coins >= si->cost &&
                           collect_item(g, si->item)) {
                    p->coins -= si->cost;
                    g->last_pickup = si->item;
                    g->pickup_flash = PICKUP_FLASH_FRAMES;
                    p->pickup_anim = 40;
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

    /* Trapdoor check. R9: taking the trapdoor out of the Womb (floor 5,
       right after Mom's Heart / It Lives) locks in the DARK route —
       floor 6 becomes Sheol, floor 7 the Dark Room. */
    if (r->has_trapdoor) {
        float cx = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
        float cy = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
        float tdx = p->x - cx;
        float tdy = p->y - cy;
        if (tdx * tdx + tdy * tdy < 20.0f * 20.0f) {
            if (g->current_floor == 5 && g->route == 0)
                set_route(g, 2);
            advance_floor(g);
            return;
        }
    }

    /* R8 (M3): floor-7 golden door — top wall of the starting room (both
       routes). Opens ONLY with both key pieces (consumed); once opened it
       stays walkable so warping out never strands the fight. If the room
       already has a real top door, the golden door sits offset left. */
    if (g->current_floor == 7 && r->type == ROOM_START &&
        g->state == STATE_PLAYING) {
        /* deny-cooldown normally only ticks inside shop rooms */
        if (g->shop_deny_timer > 0) g->shop_deny_timer--;
        float gdx = (ROOM_LEFT + ROOM_RIGHT) / 2.0f -
                    (r->doors[0] ? 80.0f : 0.0f);
        if (p->y <= ROOM_TOP + DOOR_TRIGGER + 2.0f &&
            fabsf(p->x - gdx) < DOOR_WIDTH * 0.5f + 4.0f) {
            if (g->mega_created ||
                (p->has_key_piece_1 && p->has_key_piece_2)) {
                open_mega_satan_room(g);
                return;
            } else if (g->shop_deny_timer <= 0) {
                snprintf(g->pickup_msg_text, sizeof(g->pickup_msg_text),
                         "A golden door... it demands both key halves.");
                g->pickup_msg_timer = 90;
                g->shop_deny_timer = 45;
                audio_play(SFX_HURT);
            }
        }
    }

    /* R9 route choice: the "beam of light" beside the Womb trapdoor no
       longer ends the run — it locks in the LIGHT route and ascends to
       the Cathedral (floor 6). The SAME beam object, spawned again by
       Isaac's death in the Cathedral, IS the run-ending light exit
       (ending 2). R8 #24 still applies: only once the room is cleared. */
    if (r->has_ending_beam && r->cleared && g->state == STATE_PLAYING) {
        float bx = (ROOM_LEFT + ROOM_RIGHT) / 2.0f + 60.0f;
        float by = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
        float bdx = p->x - bx;
        float bdy = p->y - by;
        if (bdx * bdx + bdy * bdy < 18.0f * 18.0f) {
            if (g->current_floor == 5) {
                /* LIGHT route: ascend to the Cathedral */
                set_route(g, 1);
                r->has_ending_beam = 0;
                audio_play(SFX_ROOM_CLEAR);
                advance_floor(g);
                return;
            }
            /* Cathedral (floor 6, light): Isaac defeated — light ending */
            g->win_ending = 2;
            g->state = STATE_WIN;
            audio_play(SFX_ROOM_CLEAR);
            unlock_check_after_win(g);
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

/* Stamp a permanent blood stain onto the room floor (persists between
   visits — rooms get progressively gorier as you fight, like Rebirth). */
static void spawn_blood_decal(Room *r, float x, float y, int big) {
    BloodDecal *bd = &r->decals[r->decal_next];
    r->decal_next = (r->decal_next + 1) % MAX_BLOOD_DECALS;  /* wrap: no unbounded growth */
    bd->active = 1;
    /* Keep stains on the floor area, off the walls */
    bd->x = clampf(x, ROOM_LEFT + 8.0f, ROOM_RIGHT - 8.0f);
    bd->y = clampf(y, ROOM_TOP + 8.0f, ROOM_BOTTOM - 8.0f);
    bd->scale = big ? randf(1.2f, 1.9f) : randf(0.7f, 1.3f);
    bd->rotation = randf(0, 6.28f);
    int roll = randi(0, 2);
    if (roll == 0)
        bd->sprite_idx = bulletatlas_blood_splat_large_1_idx + randi(0, 2);
    else if (roll == 1)
        bd->sprite_idx = bulletatlas_blood_splat_huge_1_idx + randi(0, 1);
    else
        bd->sprite_idx = bulletatlas_blood_splat_med_1_idx;
}

/* Core tear spawn with a positional offset (E8 20/20 parallel shots pass
 * a perpendicular offset; everything else uses the spawn_tear wrapper). */
static void spawn_tear_off(Game *g, float offx, float offy, float dx, float dy) {
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
        t->x = g->player.x + fx + offx;
        /* Raise spawn so tears look like they come from the head, not
           the body. PLAYER_SIZE/3 ≈ Isaac's eye height. */
        t->y = g->player.y - (PLAYER_SIZE * 0.30f) + fy + offy;
    }

    /* Apply slight random spread by rotating the direction vector */
    float ang = randf(-TEAR_SPREAD, TEAR_SPREAD);
    float ca = cosf(ang), sa = sinf(ang);
    float ndx = dx * ca - dy * sa;
    float ndy = dx * sa + dy * ca;
    t->dx = ndx;
    t->dy = ndy;

    /* Momentum transfer: player velocity pushes tears (Rebirth feel) */
    t->dx += g->player.vx * 0.4f;
    t->dy += g->player.vy * 0.4f;

    t->dist = 0;
    t->speed = sqrtf(t->dx * t->dx + t->dy * t->dy);
    t->dmg = g->player.stats.damage;
    t->piercing = (g->player.stats.flags & ITEM_FLAG_PIERCING) ? 1 : 0;
    t->spectral = (g->player.stats.flags & ITEM_FLAG_SPECTRAL) ? 1 : 0;
    t->homing   = (g->player.stats.flags & ITEM_FLAG_HOMING) ? 1 : 0;
    /* Magician card: temporary homing tears */
    if (g->homing_timer > 0) t->homing = 1;

    /* D5 Ipecac (ITEM_FLAG_EXPLOSIVE): lobbed grenade-tears that detonate
       on any impact (enemy, obstacle, wall or landing) */
    t->explosive = (g->player.stats.flags & ITEM_FLAG_EXPLOSIVE) ? 1 : 0;

    /* Tear arc: launch slightly upward, gravity will bring it down.
       Explosive tears get a much higher lob (Ipecac mortar arc). */
    t->z = 0;
    t->vz = t->explosive ? TEAR_ARC_VEL * 2.2f : TEAR_ARC_VEL;

    /* Sprite animation fields */
    t->rotation = atan2f(t->dy, t->dx);  /* rotation based on travel direction */
    t->anim_frame = 0;
    t->is_enemy = 0;  /* player tear */

    /* Size scales with damage */
    float dscale = t->dmg / 2.0f;
    if (dscale < 0.7f) dscale = 0.7f;
    if (dscale > 2.0f) dscale = 2.0f;
    t->size = dscale;
}

static void spawn_tear(Game *g, float dx, float dy) {
    spawn_tear_off(g, 0.0f, 0.0f, dx, dy);
}

/* ---------------- Special weapon fire paths (Phase D) ---------------- */

/* Cardinal Direction -> unit vector */
static void dir_to_vec(Direction dir, float *dx, float *dy) {
    *dx = 0.0f; *dy = 0.0f;
    switch (dir) {
        case DIR_UP:    *dy = -1.0f; break;
        case DIR_DOWN:  *dy =  1.0f; break;
        case DIR_LEFT:  *dx = -1.0f; break;
        case DIR_RIGHT: *dx =  1.0f; break;
        default: break;
    }
}

/* Shared "player fired" presentation: face + crying-head anim */
static void weapon_face(Game *g, Direction dir) {
    g->player.face_dir = dir;
    g->player.shoot_dir = dir;
    g->player.shoot_anim = 18;
}

/* ---------------- Familiar system (Phase E6) ---------------- */

/* Fill the trail history with the player's current position (room entry,
 * warps, run start) so followers don't glide in from stale coordinates. */
static void familiars_reset_trail(Game *g) {
    for (int i = 0; i < FAM_TRAIL_LEN; i++) {
        g->fam_hist_x[i] = g->player.x;
        g->fam_hist_y[i] = g->player.y;
    }
    g->fam_hist_head = 0;
}

/* Follower position: trail the player's recent positions with a fixed
 * per-slot delay (slot 0 = 20 frames back, slot 1 = 40). */
static void familiar_pos(Game *g, int slot, float *fx, float *fy) {
    int delay = FAM_TRAIL_DELAY * (slot + 1);
    int idx = g->fam_hist_head - delay;
    while (idx < 0) idx += FAM_TRAIL_LEN;
    idx %= FAM_TRAIL_LEN;
    *fx = g->fam_hist_x[idx];
    *fy = g->fam_hist_y[idx];
}

/* Per-frame: record the player position + tick familiar fire cooldowns. */
static void familiars_update(Game *g) {
    g->fam_hist_head = (g->fam_hist_head + 1) % FAM_TRAIL_LEN;
    g->fam_hist_x[g->fam_hist_head] = g->player.x;
    g->fam_hist_y[g->fam_hist_head] = g->player.y;
    for (int i = 0; i < MAX_FAMILIARS; i++) {
        if (g->fam_cd[i] > 0) g->fam_cd[i]--;
    }
}

/* Familiar tears: plain 0.75-dmg tears fired from the familiar's position.
 * Ghost Baby's tears are spectral. T4: in Cat Got Your Tongue (challenge 5,
 * familiars are the ONLY weapon) they hit for 1.5 so bosses die in
 * reasonable time. */
static void spawn_familiar_tear(Game *g, float x, float y,
                                float dx, float dy, int spectral) {
    Tear *t = NULL;
    for (int i = 0; i < MAX_TEARS; i++) {
        if (!g->tears[i].active) { t = &g->tears[i]; break; }
    }
    if (!t) return;
    memset(t, 0, sizeof(Tear));
    t->active = 1;
    t->x = x;
    t->y = y;
    t->dx = dx;
    t->dy = dy;
    t->dist = 0;
    t->speed = sqrtf(dx * dx + dy * dy);
    t->dmg = (g->challenge == 5) ? 1.5f : 0.75f;
    t->spectral = spectral;
    t->z = 0;
    t->vz = TEAR_ARC_VEL;
    t->rotation = atan2f(dy, dx);
    t->size = 0.6f;
    t->is_enemy = 0;
}

/* Fire hook: called from shoot_tear on player fire INTENT — before the
 * weapon dispatch and before the Cat-Got-Your-Tongue no-tears gate, so
 * familiars remain that challenge's only weapon. Familiars shoot in the
 * player's aim direction at 2x the player's cooldown; Demon Baby instead
 * auto-aims the nearest enemy.
 * T6: familiar tears intentionally ignore the player's shot_speed stat —
 * they always fly at TEAR_BASE_SPEED. */
static void familiars_try_fire(Game *g, Direction dir) {
    Player *p = &g->player;
    Room *r = current_room(g);
    for (int i = 0; i < MAX_FAMILIARS; i++) {
        if (p->familiar[i] == ITEM_NONE) continue;
        if (g->fam_cd[i] > 0) continue;
        float fx, fy;
        familiar_pos(g, i, &fx, &fy);
        float dx = 0.0f, dy = 0.0f;
        if (p->familiar[i] == ITEM_DEMON_BABY) {
            /* Auto-aim the nearest enemy; hold fire in an empty room */
            float best = 1e9f;
            Enemy *bestE = NULL;
            for (int j = 0; j < r->enemy_count; j++) {
                Enemy *e = &r->enemies[j];
                if (!e->active || e->hidden) continue;
                float ddx = e->x - fx, ddy = e->y - fy;
                float d2 = ddx * ddx + ddy * ddy;
                if (d2 < best) { best = d2; bestE = e; }
            }
            if (!bestE) continue;
            float ddx = bestE->x - fx, ddy = bestE->y - fy;
            float dm = sqrtf(ddx * ddx + ddy * ddy);
            if (dm < 1.0f) continue;
            dx = (ddx / dm) * TEAR_BASE_SPEED;
            dy = (ddy / dm) * TEAR_BASE_SPEED;
        } else {
            float ux, uy;
            dir_to_vec(dir, &ux, &uy);
            if (ux == 0.0f && uy == 0.0f) continue;
            dx = ux * TEAR_BASE_SPEED;
            dy = uy * TEAR_BASE_SPEED;
        }
        spawn_familiar_tear(g, fx, fy, dx, dy,
                            p->familiar[i] == ITEM_GHOST_BABY);
        g->fam_cd[i] = get_tear_cooldown(p) * 2;
    }
}

/* ================================================================
 * R8 (M8): friendly blue flies (Guppy transformation / Guppy's Head)
 *
 * Implementation choice: a small dedicated pool on Game rather than
 * friendly-flagged entries in the room's Enemy array — the enemy array
 * is room-owned and every tear/contact/AI path assumes hostility, so a
 * separate 6-slot pool is the least invasive robust option. Flies seek
 * the nearest enemy, deal 2 contact damage and die on the hit (classic
 * Isaac blue-fly behaviour); with no target they orbit the player.
 * ================================================================ */
static int kill_enemy(Game *g, Room *r, Enemy *e);   /* defined below */

static void spawn_blue_fly(Game *g, float x, float y) {
    for (int i = 0; i < MAX_BLUE_FLIES; i++) {
        BlueFly *f = &g->blue_flies[i];
        if (f->active) continue;
        f->x = x;
        f->y = y;
        f->anim = i * 17;    /* deterministic phase offset per slot */
        f->active = 1;
        return;
    }
}

static void blue_flies_update(Game *g) {
    Room *r = current_room(g);
    if (!r) return;
    Player *p = &g->player;
    for (int i = 0; i < MAX_BLUE_FLIES; i++) {
        BlueFly *f = &g->blue_flies[i];
        if (!f->active) continue;
        f->anim++;

        /* Nearest live, targetable enemy in the current room */
        Enemy *best = NULL;
        float bestD2 = 1e12f;
        for (int j = 0; j < r->enemy_count; j++) {
            Enemy *e = &r->enemies[j];
            if (!e->active || e->hidden) continue;
            float dx = e->x - f->x, dy = e->y - f->y;
            float d2 = dx * dx + dy * dy;
            if (d2 < bestD2) { bestD2 = d2; best = e; }
        }

        if (best) {
            float dx = best->x - f->x, dy = best->y - f->y;
            float dm = sqrtf(dx * dx + dy * dy);
            float esz = is_boss_type(best->type) ? ENEMY_SIZE * 2 : ENEMY_SIZE;
            if (dm < esz + 4.0f) {
                /* Bite: 2 damage, then the fly is spent */
                best->hp -= 2.0f;
                best->flash = 14;
                audio_play(SFX_HIT);
                spawn_blood_splatter(g, f->x, f->y, dx, dy, 0);
                if (best->hp <= 0) kill_enemy(g, r, best);
                f->active = 0;
                continue;
            }
            /* Seek with a light deterministic wobble */
            float spd = 1.7f;
            f->x += (dx / dm) * spd + sinf((float)f->anim * 0.31f) * 0.4f;
            f->y += (dy / dm) * spd + cosf((float)f->anim * 0.27f) * 0.4f;
        } else {
            /* No target: lazy orbit around the player */
            float ang = (float)f->anim * 0.05f + (float)i * 1.047f;
            float tx = p->x + cosf(ang) * 26.0f;
            float ty = p->y + sinf(ang) * 18.0f;
            f->x += (tx - f->x) * 0.12f;
            f->y += (ty - f->y) * 0.12f;
        }
        /* Keep inside the room */
        f->x = clampf(f->x, ROOM_LEFT + 4, ROOM_RIGHT - 4);
        f->y = clampf(f->y, ROOM_TOP + 4, ROOM_BOTTOM - 4);
    }
}

/* Procedural blue fly: shadow, dark body with a blue sheen, wing flicker */
static void render_blue_flies(Game *g) {
    for (int i = 0; i < MAX_BLUE_FLIES; i++) {
        BlueFly *f = &g->blue_flies[i];
        if (!f->active) continue;
        float bobf = sinf((float)f->anim * 0.2f) * 1.5f;
        float fy = f->y + bobf;
        /* grounding shadow */
        C2D_DrawEllipseSolid(f->x - 3, f->y + 5, 0, 6, 2,
                             C2D_Color32(0, 0, 0, 60));
        /* wings (deterministic flicker from the anim counter) */
        int wing = (f->anim / 3) % 2;
        u32 wcol = C2D_Color32(220, 230, 245, 160);
        C2D_DrawEllipseSolid(f->x - 6, fy - 4 - wing, 0, 5, 3, wcol);
        C2D_DrawEllipseSolid(f->x + 1, fy - 4 + wing, 0, 5, 3, wcol);
        /* body: dark with a blue sheen */
        C2D_DrawCircleSolid(f->x, fy, 0, 3.5f, C2D_Color32(30, 34, 60, 255));
        C2D_DrawCircleSolid(f->x - 1, fy - 1, 0, 1.5f,
                            C2D_Color32(90, 130, 230, 255));
    }
}

/* D3 Mom's Knife: throw the held knife forward; it returns on its own */
static void knife_fire(Game *g, Direction dir) {
    Player *p = &g->player;
    if (dir == DIR_NONE) return;
    if (g->knife_state != 0) return;      /* knife already in flight */
    if (p->tear_cooldown > 0) return;
    float dx, dy;
    dir_to_vec(dir, &dx, &dy);
    g->knife_state = 1;                   /* thrown */
    g->knife_x = p->x + dx * PLAYER_SIZE;
    g->knife_y = p->y - PLAYER_SIZE * 0.3f + dy * PLAYER_SIZE;
    g->knife_dx = dx;
    g->knife_dy = dy;
    g->knife_dist = 0.0f;
    g->knife_hit_cd = 0;
    p->tear_cooldown = get_tear_cooldown(p);
    weapon_face(g, dir);
    audio_play(SFX_SHOOT);
}

/* D1 Brimstone: hold-to-charge (15f), then a 10f beam sweep. Normal firing
 * is fully replaced (precedence in shoot_tear); while a beam is live or the
 * post-beam cooldown runs, holding fire does nothing. */
static void brimstone_fire(Game *g, Direction dir) {
    Player *p = &g->player;
    if (dir == DIR_NONE) return;
    if (g->laser_active) return;          /* live beam blocks re-fire */
    if (p->tear_cooldown > 0) return;     /* recovering from last blast */
    float dx, dy;
    dir_to_vec(dir, &dx, &dy);
    g->laser_dx = dx;                     /* track aim while charging */
    g->laser_dy = dy;
    weapon_face(g, dir);
    g->laser_charge++;
    if (g->laser_charge >= 15) {
        g->laser_charge = 0;
        g->laser_active = 1;
        g->laser_is_tech = 0;
        g->laser_timer = 10;              /* beam sweep duration */
        p->tear_cooldown = get_tear_cooldown(p);  /* item's -2.0 fire rate */
        trigger_shake(g, 3.0f, 10);
        audio_play(SFX_BOSS);             /* deep roar sells the blast */
    }
}

/* D2 Technology: continuous thin beam while the fire button is held.
 * No charge, no cooldown; laser_update deals dmg*0.25 per frame. */
static void technology_fire(Game *g, Direction dir) {
    if (dir == DIR_NONE) return;
    float dx, dy;
    dir_to_vec(dir, &dx, &dy);
    if (!g->laser_active) audio_play(SFX_TEAR_FIRE1);
    g->laser_active = 1;
    g->laser_is_tech = 1;
    g->laser_dx = dx;
    g->laser_dy = dy;
    g->laser_timer = 2;   /* refreshed while held; dies 2f after release */
    weapon_face(g, dir);
}

/* D4 Dr./Epic Fetus: "tears" are pooled bombs launched with tear velocity
 * that slide, stop, then explode on a short 45f fuse. */
static void fetus_fire(Game *g, Direction dir) {
    Player *p = &g->player;
    if (dir == DIR_NONE) return;
    if (p->tear_cooldown > 0) return;
    int slot = -1;
    for (int i = 0; i < MAX_BOMBS; i++) {
        if (!g->bombs[i].active) { slot = i; break; }
    }
    if (slot < 0) return;                 /* pool full: wait for a blast */
    float dx, dy;
    dir_to_vec(dir, &dx, &dy);
    float sp = TEAR_BASE_SPEED * (1.0f + p->stats.shot_speed * 0.15f);
    ActiveBomb *b = &g->bombs[slot];
    b->x = p->x + dx * PLAYER_SIZE;
    b->y = p->y + dy * PLAYER_SIZE;
    b->vx = dx * sp + p->vx * 0.4f;       /* tear velocity + momentum */
    b->vy = dy * sp + p->vy * 0.4f;
    b->timer = 45;                        /* short Dr. Fetus fuse */
    b->flash = 0;
    b->is_epic = player_has_item(p, ITEM_EPIC_FETUS);
    b->is_fetus = 1;                      /* blast scales with damage stat */
    b->active = 1;
    p->tear_cooldown = get_tear_cooldown(p);
    weapon_face(g, dir);
    audio_play(SFX_SHOOT);
}

void shoot_tear(Game *g, Direction dir) {
    /* E6: familiars fire on player fire intent (their own 2x cooldown).
       Runs BEFORE the no-tears challenge gate so familiars remain the only
       weapon in "Cat Got Your Tongue". */
    if (dir != DIR_NONE) familiars_try_fire(g, dir);

    /* E9 challenge 5 "Cat Got Your Tongue": the player has no weapon */
    if (g->challenge == 5) {
        weapon_face(g, dir);
        return;
    }

    /* ---- Weapon precedence (Phase D) ----
     * KNIFE > BRIMSTONE > LASER (Technology) > BOMB_TEAR (Dr./Epic Fetus)
     * > normal tears. The highest-priority owned weapon fully replaces
     * tears; lower-priority flags are ignored while it is held. */
    int wflags = g->player.stats.flags;
    if (wflags & ITEM_FLAG_KNIFE)     { knife_fire(g, dir);      return; }
    if (wflags & ITEM_FLAG_BRIMSTONE) { brimstone_fire(g, dir);  return; }
    if (wflags & ITEM_FLAG_LASER)     { technology_fire(g, dir); return; }
    if (wflags & ITEM_FLAG_BOMB_TEAR) { fetus_fire(g, dir);      return; }

    if (g->player.tear_cooldown > 0) return;

    /* Live shot-speed stat: each point = +15% tear velocity */
    float sp = TEAR_BASE_SPEED * (1.0f + g->player.stats.shot_speed * 0.15f);

    float tdx = 0, tdy = 0;
    switch (dir) {
        case DIR_UP:    tdy = -sp; break;
        case DIR_DOWN:  tdy =  sp; break;
        case DIR_LEFT:  tdx = -sp; break;
        case DIR_RIGHT: tdx =  sp; break;
        default: return;
    }

    if (g->player.stats.flags & ITEM_FLAG_TRIPLE) {
        /* Triple shot: center + two angled */
        spawn_tear(g, tdx, tdy);

        float angle = 0.25f;
        if (tdx != 0) {
            spawn_tear(g, tdx, -sp * angle);
            spawn_tear(g, tdx,  sp * angle);
        } else {
            spawn_tear(g, -sp * angle, tdy);
            spawn_tear(g,  sp * angle, tdy);
        }
    } else if (g->player.stats.flags & ITEM_FLAG_DOUBLE) {
        /* E8 20/20: two parallel tears offset perpendicular to the aim */
        float pl = sqrtf(tdx * tdx + tdy * tdy);
        float pxn = (pl > 0.1f) ? -tdy / pl : 0.0f;
        float pyn = (pl > 0.1f) ?  tdx / pl : 0.0f;
        spawn_tear_off(g,  pxn * 5.0f,  pyn * 5.0f, tdx, tdy);
        spawn_tear_off(g, -pxn * 5.0f, -pyn * 5.0f, tdx, tdy);
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

/* D5 Ipecac detonation: small blast, tear dmg + 20 to everything in it,
 * and it CAN hurt the player standing too close (that's the Ipecac deal;
 * the player hit is Pyromaniac-gated inside bomb_explode_ex).
 * Ipecac Lite (without the real Ipecac) gets a smaller, weaker blast. */
static void tear_detonate(Game *g, Tear *t) {
    if (player_has_item(&g->player, ITEM_IPECAC_LITE) &&
        !player_has_item(&g->player, ITEM_IPECAC)) {
        bomb_explode_ex(g, t->x, t->y, 24.0f, t->dmg + 10.0f, t->dmg + 10.0f, 2);
        return;
    }
    bomb_explode_ex(g, t->x, t->y, 32.0f, t->dmg + 20.0f, t->dmg + 20.0f, 2);
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
                    /* Re-normalize speed back to this tear's own speed */
                    float sm = sqrtf(t->dx * t->dx + t->dy * t->dy);
                    if (sm > 0.1f) {
                        t->dx = (t->dx / sm) * t->speed;
                        t->dy = (t->dy / sm) * t->speed;
                    }
                }
            }
        }

        /* Move */
        t->x += t->dx;
        t->y += t->dy;
        /* Track distance using actual velocity */
        t->dist += sqrtf(t->dx * t->dx + t->dy * t->dy);

        /* Arc: launch hop, then glide slightly airborne; near the end of
           range the tear drops out of the air and pops where it lands. */
        t->z += t->vz;
        t->vz += TEAR_GRAVITY;
        if (t->dist > range - 30.0f) {
            /* Falling phase: gravity keeps pulling; landing = splash */
            if (t->z >= 0) {
                if (t->explosive) tear_detonate(g, t);
                spawn_tear_pop(g, t->x, t->y, 0);
                t->active = 0;
                continue;
            }
        } else if (t->vz > 0 && t->z > -4.0f) {
            /* Done with the launch arc: hold a small glide height */
            t->z = -4.0f;
            t->vz = 0;
        }

        /* Animate tear: increment frame counter. Homing tears re-aim their
           sprite each frame; everything else keeps its spawn aim rotation
           (Rebirth tears don't spin). */
        t->anim_frame++;
        if (t->homing) {
            t->rotation = atan2f(t->dy, t->dx);  /* always face travel direction */
        }

        /* Out of range -> splash + blood particles */
        if (t->dist >= range) {
            if (t->explosive) tear_detonate(g, t);
            spawn_tear_pop(g, t->x, t->y, 0);  /* wall splash */
            t->active = 0;
            continue;
        }

        /* Hit wall (spectral tears pass through) */
        if (!t->spectral) {
            if (t->x < ROOM_LEFT || t->x > ROOM_RIGHT ||
                t->y < ROOM_TOP  || t->y > ROOM_BOTTOM) {
                if (t->explosive) tear_detonate(g, t);
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

        /* Hit obstacle (spectral tears pass through; spikes never block) */
        if (!t->spectral) {
            for (int j = 0; j < r->obstacle_count; j++) {
                Obstacle *o = &r->obstacles[j];
                if (!o->active) continue;
                if (o->type == OBST_SPIKES) continue;
                float odx = t->x - o->x;
                float ody = t->y - o->y;
                float dist = odx * odx + ody * ody;
                float minDist = TEAR_RADIUS + OBSTACLE_SIZE * 0.5f;
                if (dist < minDist * minDist) {
                    if (o->type == OBST_POOP) {
                        /* Poop is destructible: 3 tear hits, then a chance
                           of a pickup underneath (classic Isaac) */
                        o->hp--;
                        if (o->hp <= 0) {
                            o->active = 0;
                            spawn_tear_pop(g, o->x, o->y, 0);
                            spawn_tear_pop(g, o->x, o->y, 0);
                            /* Petrified Poop: way better poop loot */
                            int poop_rate =
                                (g->player.trinket == TRINKET_PETRIFIED_POOP)
                                ? 60 : 25;
                            if (randi(0, 99) < poop_rate)
                                spawn_random_consumable(r, o->x, o->y);
                        }
                    }
                    if (t->explosive) tear_detonate(g, t);
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
    case ENEMY_BOSS_GISH:       return "GISH";
    case ENEMY_BOSS_LOKI:       return "LOKI";
    case ENEMY_BOSS_STEVEN:     return "STEVEN";
    case ENEMY_BOSS_CHUB:       return "CHUB";
    case ENEMY_BOSS_FISTULA:    return "FISTULA";
    case ENEMY_BOSS_SCOLEX:     return "SCOLEX";
    case ENEMY_BOSS_MEGA_SATAN: return "MEGA SATAN";
    case ENEMY_BOSS_MOM:        return "MOM";
    case ENEMY_BOSS_MOMS_HEART: return "MOM'S HEART";
    case ENEMY_BOSS_SATAN:      return "SATAN";
    case ENEMY_BOSS_ISAAC:      return "ISAAC";
    case ENEMY_BOSS_THE_LAMB:   return "THE LAMB";
    case ENEMY_BOSS_IT_LIVES:   return "IT LIVES";
    case ENEMY_BOSS_URIEL:      return "URIEL";
    case ENEMY_BOSS_GABRIEL:    return "GABRIEL";
    case ENEMY_BOSS_KRAMPUS:    return "KRAMPUS";
    case ENEMY_BOSS_BLUE_BABY:  return "???";
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

/* B6: boss minion spawners go through alloc_dynamic_enemy (append-first,
   reuse-dead-slots fallback) so long fights keep spawning even after
   enemy_count has ratcheted up to MAX_ENEMIES. */
static Enemy *alloc_dynamic_enemy(Room *r);

static void boss_spawn_fly(Game *g, Room *r, float bx, float by) {
    Enemy *e = alloc_dynamic_enemy(r);
    if (!e) return; /* active/spawn_grace set by alloc_dynamic_enemy */
    e->type = ENEMY_FLY;
    /* T5: same difficulty/infinite HP scaling as room spawns */
    e->hp = 2.0f * spawn_hp_mult(g);
    e->max_hp = e->hp;
    e->x = bx + randf(-30, 30);
    e->y = by + randf(-30, 30);
    e->dx = randf(-1.5f, 1.5f);
    e->dy = randf(-1.5f, 1.5f);
    e->timer = randi(30, 60);
}

/* Spawn boss tear in a specific direction */
static void boss_shoot_tear(Game *g, float x, float y, float dx, float dy) {
    spawn_enemy_shot(g, x, y, dx, dy, BOSS_SHOT_DMG);
}

/* E3 (Satan): clip the enemy Brimstone ray to the room walls. Uses the
 * dedicated ebeam_* state on Game — never the player's laser_* fields. */
static void ebeam_clip_endpoint(Game *g) {
    float t_max = 600.0f;   /* generous cap: longer than any room diagonal */
    if (g->ebeam_dx >  0.0001f) { float t = (ROOM_RIGHT  - g->ebeam_x) / g->ebeam_dx; if (t < t_max) t_max = t; }
    if (g->ebeam_dx < -0.0001f) { float t = (ROOM_LEFT   - g->ebeam_x) / g->ebeam_dx; if (t < t_max) t_max = t; }
    if (g->ebeam_dy >  0.0001f) { float t = (ROOM_BOTTOM - g->ebeam_y) / g->ebeam_dy; if (t < t_max) t_max = t; }
    if (g->ebeam_dy < -0.0001f) { float t = (ROOM_TOP    - g->ebeam_y) / g->ebeam_dy; if (t < t_max) t_max = t; }
    if (t_max < 0.0f) t_max = 0.0f;
    g->ebeam_ex = g->ebeam_x + g->ebeam_dx * t_max;
    g->ebeam_ey = g->ebeam_y + g->ebeam_dy * t_max;
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

/* Peep: spawn a small eye that pursues player.
   R8 #20: routed through alloc_dynamic_enemy (dead-slot reuse + spawn_grace)
   like every other mid-combat spawner. */
static void boss_spawn_eye(Room *r, float bx, float by) {
    Enemy *e = alloc_dynamic_enemy(r);
    if (!e) return; /* active/spawn_grace set by alloc_dynamic_enemy */
    e->type = ENEMY_EYE;
    e->hp = 3;
    e->max_hp = 3;
    e->x = bx + randf(-20, 20);
    e->y = by + randf(-20, 20);
    e->dx = randf(-1.0f, 1.0f);
    e->dy = randf(-1.0f, 1.0f);
    e->timer = randi(30, 60);
    e->shoot_timer = randi(60, 100);
}

/* Haunt: spawn a Lil Haunt minion (R8 #20: alloc_dynamic_enemy) */
static void boss_spawn_lil_haunt(Room *r, float bx, float by) {
    Enemy *e = alloc_dynamic_enemy(r);
    if (!e) return;
    e->type = ENEMY_LIL_HAUNT;
    e->hp = 4;
    e->max_hp = 4;
    e->x = bx + randf(-25, 25);
    e->y = by + randf(-25, 25);
    e->dx = randf(-0.8f, 0.8f);
    e->dy = randf(-0.8f, 0.8f);
    e->timer = randi(60, 90);
}

/* Widow / Gurdy: spawn a small spider (R8 #20: alloc_dynamic_enemy) */
static void boss_spawn_spider(Room *r, float bx, float by) {
    Enemy *e = alloc_dynamic_enemy(r);
    if (!e) return;
    e->type = ENEMY_SPIDER;
    e->hp = 2;
    e->max_hp = 2;
    e->x = bx + randf(-25, 25);
    e->y = by + randf(-25, 25);
    e->dx = randf(-2.0f, 2.0f);
    e->dy = randf(-2.0f, 2.0f);
    e->timer = randi(30, 60);
}

/* Gurdy: spawn pooter minion (R8 #20: alloc_dynamic_enemy) */
static void boss_spawn_pooter(Room *r, float bx, float by) {
    Enemy *e = alloc_dynamic_enemy(r);
    if (!e) return;
    e->type = ENEMY_POOTER;
    e->hp = 3;
    e->max_hp = 3;
    e->x = bx + randf(-30, 30);
    e->y = by + randf(-30, 30);
    e->dx = randf(-1.0f, 1.0f);
    e->dy = randf(-1.0f, 1.0f);
    e->timer = randi(40, 80);
    e->shoot_timer = randi(60, 100);
}

/* ----- R8 #18/#44: airborne-boss helpers ----- */

/* Is this boss mid-jump? Airborne bosses deal NO ground contact damage and
 * cast a growing landing-shadow telegraph at (target_x, target_y).
 * Sign conventions verified per boss: Monstro/Gish/Mom launch with
 * jump_vz < 0 (jump_z negative = up, lands at jump_z >= 0); Satan's stomp
 * lives in phase 2 / attack_pattern 1 with the same sign; Widow launches
 * with jump_vz = +7 (jump_z positive = up, lands at jump_z <= 0).
 * If `progress` is non-NULL it receives 0..1 landing progress (0 = just
 * launched, 1 = touchdown) derived from the jump's known frame budget. */
static int boss_airborne(const Enemy *e, float *progress) {
    int air = 0;
    float dur = 1.0f;
    switch (e->type) {
    case ENEMY_BOSS_MONSTRO:
    case ENEMY_BOSS_GISH:
        air = (e->phase == 1);
        dur = (e->attack_pattern == 2) ? 45.0f : 30.0f; /* big jump vs hop */
        break;
    case ENEMY_BOSS_MOM:
        air = (e->phase == 1);
        dur = 45.0f;
        break;
    case ENEMY_BOSS_SATAN:
        air = (e->phase == 2 && e->attack_pattern == 1);
        dur = 40.0f;
        break;
    case ENEMY_BOSS_WIDOW:
        air = (e->phase == 1);
        dur = 30.0f;
        break;
    default:
        break;
    }
    if (air && progress)
        *progress = clampf(1.0f - (float)e->timer / dur, 0.0f, 1.0f);
    return air;
}

/* R9 (C1 - Isaac): praying = floating in the light, dealing NO contact
 * damage (still fully damageable by tears — unlike e->hidden). */
static int boss_praying(const Enemy *e) {
    return e->type == ENEMY_BOSS_ISAAC && e->state == 1;
}

/* R8 #44: landing thump — standing at ground zero of a boss landing still
 * punishes (1 full heart in a small ~28px AoE), replacing the unfair
 * whole-body contact damage the boss used to deal while airborne. */
static void boss_landing_thump(Game *g, float lx, float ly) {
    Player *p = &g->player;
    if (p->iframes != 0) return;
    float dx = p->x - lx;
    float dy = p->y - ly;
    if (dx * dx + dy * dy >= 28.0f * 28.0f) return;
    p->hp -= player_absorb_dmg(p, 2);
    p->iframes = PLAYER_IFRAMES;
    g->hitstop = 3;
    float m = sqrtf(dx * dx + dy * dy);
    if (m > 0.1f) {
        p->vx = (dx / m) * PLAYER_KB_FORCE;
        p->vy = (dy / m) * PLAYER_KB_FORCE;
    } else {
        p->vx = randf(-1.0f, 1.0f) * PLAYER_KB_FORCE;
        p->vy = randf(-1.0f, 1.0f) * PLAYER_KB_FORCE;
    }
    audio_play(SFX_HURT_GRUNT);
    if (player_check_death(p)) {
        audio_play(SFX_PLAYER_DEATH);
        g->state = STATE_GAMEOVER;
    }
}

/* R8 #28/#43: shared boss phase-transition juice — hitstop freeze, a full
 * white flash on the boss, the roar SFX, and an enemy-projectile wipe so
 * the new phase always starts on a readable, fair slate. */
static void boss_phase_juice(Game *g, Enemy *e) {
    g->hitstop = 10;
    e->flash = 20;              /* long white flash (render tints on flash) */
    audio_play(SFX_BOSS);
    for (int i = 0; i < MAX_ENEMY_SHOTS; i++) g->enemy_shots[i].active = 0;
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

/* Allocate a slot for a mid-combat dynamic spawn. PREFERS appending at
 * r->enemy_count so death-spawns land at indices >= any sweep's captured
 * `initial` count (they must not be hit by the burst that killed their
 * parent — see damage_all_enemies). Falls back to the first inactive slot
 * only when the array is genuinely full. Returns NULL if no slot free.
 * The slot is zeroed and marked active; caller fills in the rest. */
static Enemy *alloc_dynamic_enemy(Room *r) {
    Enemy *e = NULL;
    if (r->enemy_count < MAX_ENEMIES) {
        e = &r->enemies[r->enemy_count++];
    } else {
        for (int i = 0; i < r->enemy_count; i++) {
            if (!r->enemies[i].active) { e = &r->enemies[i]; break; }
        }
    }
    if (e) {
        memset(e, 0, sizeof(Enemy));
        e->active = 1;
        e->spawn_grace = 10; /* F6: no contact damage at the corpse position */
    }
    return e;
}

/* Spawn a small gaper as part of split-on-death */
static void spawn_gaper_split(Room *r, float px, float py) {
    Enemy *e = alloc_dynamic_enemy(r);
    if (!e) return;
    e->type = ENEMY_GAPER_SMALL;
    e->hp = 1;
    e->max_hp = 1;
    e->x = px + randf(-10, 10);
    e->y = py + randf(-10, 10);
    e->dx = randf(-1.5f, 1.5f);
    e->dy = randf(-1.5f, 1.5f);
    e->timer = randi(20, 60);
    e->split_done = 1; /* don't split again */
}

/* Spawn a small Fistula ball as part of the boss's split-on-hit mechanic */
static void spawn_fistula_ball(Room *r, float px, float py, float hp) {
    Enemy *e = alloc_dynamic_enemy(r);
    if (!e) return;
    e->type = ENEMY_FISTULA_BALL;
    e->hp = hp;
    e->max_hp = hp;
    e->x = px + randf(-14, 14);
    e->y = py + randf(-14, 14);
    e->dx = randf(-1.8f, 1.8f);
    e->dy = randf(-1.8f, 1.8f);
    e->timer = randi(20, 60);
    e->split_done = (hp <= 1.0f) ? 1 : 0; /* stop splitting once tiny */
}

/* Spawn a fly from a dying Mulligan */
static void spawn_fly_from_death(Room *r, float px, float py, const FloorInfo *fi) {
    Enemy *e = alloc_dynamic_enemy(r);
    if (!e) return;
    e->type = ENEMY_FLY;
    e->hp = 2;
    e->max_hp = 2;
    e->x = px + randf(-12, 12);
    e->y = py + randf(-12, 12);
    e->dx = randf(-1.0f, 1.0f) * fi->enemy_speed_mult;
    e->dy = randf(-1.0f, 1.0f) * fi->enemy_speed_mult;
    e->timer = randi(30, 90);
}

/* Boom fly explosion: damage player if close, spawn blood particles */
static void boom_fly_explode(Game *g, float x, float y) {
    Player *p = &g->player;
    float dx = p->x - x;
    float dy = p->y - y;
    float dist = sqrtf(dx * dx + dy * dy);
    float blast_radius = 40.0f;
    if (dist < blast_radius && (p->stats.flags & ITEM_FLAG_FIRE_IMMUNE)) {
        /* D6 Pyromaniac: no explosion damage, heal half a heart instead */
        if (p->hp < p->stats.max_hp) p->hp += 1;
        audio_play(SFX_PICKUP);
    } else if (dist < blast_radius && p->iframes == 0) {
        p->hp -= player_absorb_dmg(p, 1);
        p->iframes = PLAYER_IFRAMES;
        g->hitstop = 3;
        if (dist > 0.1f) {
            p->vx = (dx / dist) * PLAYER_KB_FORCE * 1.5f;
            p->vy = (dy / dist) * PLAYER_KB_FORCE * 1.5f;
        }
        audio_play(SFX_HURT);
        /* F4: explosion damage can kill — consume a life (Dead Cat) or die,
           same as the bomb blast path. */
        if (player_check_death(p)) {
            audio_play(SFX_PLAYER_DEATH);
            g->state = STATE_GAMEOVER;
        }
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
        /* Boss drops one full heart */
        spawn_heart(r, e->x, e->y, HEART_RED_FULL);
    } else {
        /* Keeper: always drops a coin on top of the usual roll below */
        if (e->type == ENEMY_KEEPER) {
            spawn_consumable(r, e->x, e->y, PICKUP_COIN);
        }
        /* Rare trinket drop (luck improves it via luck_roll, ~1.2% base) */
        if (luck_roll(g, 1, 1)) {
            spawn_trinket_pickup(r, e->x, e->y, randi(1, TRINKET_COUNT - 1));
            return;
        }
        /* Pill/card chance on every regular kill, luck improves it
           (halved from 3% — kills shouldn't shower loot) */
        if (luck_roll(g, 2, 1)) {
            if (randi(0, 1))
                spawn_pill_pickup(r, e->x, e->y, randi(0, PILL_EFFECT_COUNT - 1));
            else
                spawn_card_pickup(r, e->x, e->y, randi(0, TAROT_COUNT - 1));
            return;
        }
        /* Difficulty-scaled heart drop rates:
         * Easy: full<8, half<24, consumable<44
         * Normal: full<5, half<16, consumable<36
         * Hard: full<3, half<10, consumable<30
         * Luck nudges the full/half thresholds up further. */
        float rate = diff_heart_drop_rate(g->difficulty);
        int heart_bonus = (g->player.trinket == TRINKET_CHILDS_HEART) ? 8 : 0;
        int luck_bonus  = (int)(g->player.stats.luck * 2);
        int full_thresh  = (int)(rate * 0.33f) + heart_bonus / 2 + luck_bonus / 2;
        int half_thresh  = (int)(rate) + heart_bonus + luck_bonus;
        /* T2b/T3 mercy: hearts drop a little more often at Womb depth
           (floor >= 5, where contact damage stiffens) and on The Purist
           (challenge 6, no items to heal with). Stacks, but capped. */
        {
            int widen = 0;
            if (g->current_floor >= 5) widen += 4;
            if (g->challenge == 6)     widen += 4;
            if (widen > 6) widen = 6;
            half_thresh += widen;
        }
        int consum_thresh = half_thresh + 10;          /* halved consumable window */
        int roll = randi(0, 100);
        if (roll < full_thresh) {
            spawn_heart(r, e->x, e->y, HEART_RED_FULL);
        } else if (roll < half_thresh) {
            spawn_heart(r, e->x, e->y, HEART_RED_HALF);
        } else if (roll < consum_thresh) {
            /* Rusted Key / Match Stick / Broken Magnet bias the consumable toward their type */
            if (g->player.trinket == TRINKET_RUSTED_KEY && randi(0, 1))
                spawn_consumable(r, e->x, e->y, PICKUP_KEY);
            else if (g->player.trinket == TRINKET_MATCH_STICK && randi(0, 1))
                spawn_consumable(r, e->x, e->y, PICKUP_BOMB);
            else if (g->player.trinket == TRINKET_BROKEN_MAGNET && randi(0, 1))
                spawn_consumable(r, e->x, e->y, PICKUP_COIN);
            else
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
    /* Reject non-finite velocities: a NaN shot (e.g. a zero-length aim vector
       normalised by 0) never satisfies the expiry checks in
       enemy_shots_update and would permanently occupy a pool slot. */
    if (!isfinite(dx) || !isfinite(dy)) return;
    /* Womb onward (floor >= 5): every shot hits for a full heart */
    if (g->current_floor >= 5 && dmg < 2) dmg = 2;
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
        case CURSE_DARKNESS:  return "CURSE OF DARKNESS";
        case CURSE_LOST:      return "CURSE OF THE LOST";
        case CURSE_BLIND:     return "CURSE OF THE BLIND";
        case CURSE_UNKNOWN:   return "CURSE OF THE UNKNOWN";
        case CURSE_MAZE:      return "CURSE OF THE MAZE";
        case CURSE_LABYRINTH: return "CURSE OF THE LABYRINTH";
        default:              return "";
    }
}

/* Roll a curse with 30% chance per floor.
 * Called on floor transition completion. */
void roll_curse(Game *g) {
    g->active_curse = CURSE_NONE;
    g_curse_labyrinth_pending = 0;
    g->floor_red_dmg = 0;   /* fresh floor: devil deal possible again */
    g->floor_intro_timer = 150;  /* Rebirth-style floor title banner */
    g->curse_display_timer = 0;

    /* Challenge: Eternal Darkness — every floor is cursed with darkness */
    if (g->challenge == 2) {
        g->active_curse = CURSE_DARKNESS;
        g->curse_display_timer = 180;
        return;
    }

    /* No curse on starting floor (floor 0) to give players a fair start */
    if (g->current_floor == 0) return;

    /* 30% chance per floor */
    if (randi(0, 9) < 3) {
        /* Weighted curse pool: the original 3 curses (DARKNESS, LOST, BLIND)
         * are always available. The 3 deeper curses (UNKNOWN, MAZE,
         * LABYRINTH) are rarer/absent on early floors and become
         * progressively more likely the deeper the run goes, so new
         * players aren't hit with the harsher curses immediately. */
        int pool[8];
        int pool_count = 0;
        pool[pool_count++] = CURSE_DARKNESS;
        pool[pool_count++] = CURSE_LOST;
        pool[pool_count++] = CURSE_BLIND;

        /* Deeper curses gated by floor: each becomes available a couple
         * floors in, then gets extra weighted entries the deeper we go
         * so they show up more often relative to the base three. */
        if (g->current_floor >= 2) pool[pool_count++] = CURSE_UNKNOWN;
        if (g->current_floor >= 3) pool[pool_count++] = CURSE_MAZE;
        if (g->current_floor >= 4) pool[pool_count++] = CURSE_LABYRINTH;
        if (g->current_floor >= 5) {
            /* Extra weight for the deep curses on late floors */
            pool[pool_count++] = CURSE_UNKNOWN;
            pool[pool_count++] = CURSE_MAZE;
        }

        g->active_curse = pool[randi(0, pool_count - 1)];
        g->curse_display_timer = 180;   /* 3 seconds at 60 FPS */

        if (g->active_curse == CURSE_LABYRINTH) g_curse_labyrinth_pending = 1;
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
        case 4: return "EVE";
        case 5: return "SAMSON";
        case 6: return "???";      /* Blue Baby */
        case 7: return "AZAZEL";
        case 8: return "LAZARUS";
        case 9: return "THE LOST";
        default: return "?";
    }
}

/* Apply the character-unlock progression gates from characters_completed.
 * Shared by unlock_check_after_win and the retroactive pass at boot (an old
 * save whose wins predate newly-added characters must still unlock them).
 * Returns 1 if any new character was unlocked. */
static int apply_unlock_gates(void) {
    int before = g_config.unlocked_chars;
    if (g_config.characters_completed & 0x01) g_config.unlocked_chars |= 0x02; /* Magdalene */
    if (g_config.characters_completed & 0x02) g_config.unlocked_chars |= 0x04; /* Cain */
    if (g_config.characters_completed & 0x04) g_config.unlocked_chars |= 0x08; /* Judas */
    if (g_config.characters_completed & 0x08) g_config.unlocked_chars |= 0x10; /* Eve */
    if (g_config.characters_completed & 0x10) g_config.unlocked_chars |= 0x20; /* Samson */
    if (g_config.characters_completed & 0x20) g_config.unlocked_chars |= 0x40; /* Blue Baby */
    /* R10 (C4) gates: Azazel = 3 lifetime devil deals; Lazarus = die 10
       times lifetime; The Lost = Mega Satan defeated once (bit 16 of
       bosses_defeated: ENEMY_BOSS_MEGA_SATAN - ENEMY_BOSS_DUKE == 16).
       All retroactive via the load-time call in main(). */
    if (g_config.devil_deals_taken >= 3)      g_config.unlocked_chars |= 0x80;  /* Azazel */
    if (g_config.total_deaths >= 10)          g_config.unlocked_chars |= 0x100; /* Lazarus */
    if (g_config.bosses_defeated & (1 << 16)) g_config.unlocked_chars |= 0x200; /* The Lost */
    return g_config.unlocked_chars != before;
}

/* Called on game win to update unlocks/achievements. */
void unlock_check_after_win(Game *g) {
    int char_bit = 1 << g->player.character;
    g_config.characters_completed |= char_bit;
    g_config.total_wins++;

    /* Unlock next character (progression: Isaac -> Magdalene -> Cain -> Judas
     * -> Eve -> Samson -> ??? (Blue Baby)) */
    apply_unlock_gates();

    /* Save progress */
    config_save(&g_config);
}

/* ================================================================
 * Bomb placement and explosion
 * ================================================================ */

/* Forward declaration for bomb death handling */
static int is_boss_type(EnemyType t);

/* Shared enemy death path — tear hits, bomb blasts and card damage all
 * funnel through here so drops, champion splits, boss-defeat tracking and
 * gore can't diverge. Room-clear detection stays in the per-frame sweep,
 * which sees e->active = 0 as before.
 * Returns 1 if the enemy actually died, 0 if it survived (Globin collapse). */
static int kill_enemy(Game *g, Room *r, Enemy *e) {
    /* F2: defensive re-entry guard — a second tear (TRIPLE/DOUBLE shot) or
       overlapping burst hitting the same corpse in one frame must not re-run
       the death path (double drops/score, quadruple splits, double saves). */
    if (!e->active) return 1;

    /* Globin: collapse into pile instead of dying (first time) */
    if (e->type == ENEMY_GLOBIN && e->state == 0 && !e->split_done) {
        e->state = 1;          /* collapsed pile */
        e->hp = 1;
        e->regen_timer = 120;   /* ticks until full regen */
        e->split_done = 1;      /* only collapse once */
        return 0;
    }

    /* Gaper split on death */
    if (e->type == ENEMY_GAPER && !e->split_done) {
        e->split_done = 1; /* F2: split exactly once */
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

    /* R8 #27: Fistula always bursts on death. The quarter-threshold split
       paths require SURVIVING the hit, so a one-shot from full HP (or the
       final killing blow) used to spawn zero balls. Spawning here (before
       the boss epilogue sweep below) keeps boss_active/music alive until
       the balls are dealt with. */
    if (e->type == ENEMY_BOSS_FISTULA) {
        float ball_hp = e->max_hp / 6.0f;
        if (ball_hp < 1.0f) ball_hp = 1.0f;
        spawn_fistula_ball(r, e->x, e->y, ball_hp);
        spawn_fistula_ball(r, e->x, e->y, ball_hp);
    }

    /* Drops (difficulty-scaled) */
    enemy_death_drops(g, r, e);

    /* Champion bonus reward + black champion split */
    if (e->champion != CHAMP_NONE) {
        enemy_drop_champion_reward(g, e);
        if (e->champion == CHAMP_BLACK && e->split_pending) {
            e->split_pending = 0; /* F2: split exactly once */
            /* Spawn 2 weaker copies of base enemy at same location.
               F3: alloc_dynamic_enemy appends at r->enemy_count so the
               splits land above any sweep's captured `initial` count. */
            float split_hp = (e->max_hp > 1.0f) ? (e->max_hp * 0.5f) : 1.0f;
            for (int ci = 0; ci < 2; ci++) {
                Enemy *ne = alloc_dynamic_enemy(r);
                if (!ne) break;
                ne->type = e->type;
                ne->x = e->x + randf(-12.0f, 12.0f);
                ne->y = e->y + randf(-12.0f, 12.0f);
                ne->hp = ne->max_hp = split_hp;
                ne->champion = CHAMP_NONE;
                ne->dx = randf(-1.0f, 1.0f);
                ne->dy = randf(-1.0f, 1.0f);
            }
        }
    }

    /* Death gore: big blood burst + permanent floor stain */
    spawn_blood_splatter(g, e->x, e->y, e->kb_dx, e->kb_dy, 1);
    spawn_blood_decal(r, e->x, e->y, is_boss_type(e->type));
    if (is_boss_type(e->type)) {
        /* Bosses go out messy: extra burst + extra stains */
        spawn_blood_splatter(g, e->x + randf(-10, 10),
                             e->y + randf(-10, 10), 0, 0, 1);
        spawn_blood_decal(r, e->x + randf(-18, 18),
                          e->y + randf(-14, 14), 1);
        spawn_blood_decal(r, e->x + randf(-18, 18),
                          e->y + randf(-14, 14), 1);
    }

    e->active = 0;
    g->score += is_boss_type(e->type) ? 100 : 10;
    g->kills++;
    audio_play(SFX_ENEMY_DEATH);
    if (is_boss_type(e->type)) {
        trigger_shake(g, 7.0f, 40);
        g->boss_death_anim = 60; /* boss death explosion effect */
        g->boss_death_x = e->x;  /* anchor the death anim on the corpse */
        g->boss_death_y = e->y;
        /* Track in persistent unlocks (bosses_defeated bitmask).
           27 bosses: Duke..Mega Satan (17) + Mom, Mom's Heart, Satan +
           R9: Isaac (20), The Lamb (21), It Lives (22) +
           R8: Uriel (23), Gabriel (24), Krampus (25), ??? (26).
           B4: only pay the blocking config_save on FIRST-time defeat. */
        {
            int boss_idx = (int)e->type - (int)ENEMY_BOSS_DUKE;
            if (boss_idx >= 0 && boss_idx < 27 &&
                !(g_config.bosses_defeated & (1u << boss_idx))) {
                g_config.bosses_defeated |= (1u << boss_idx);
                config_save(&g_config);
            }
        }
        /* E2/R9: Mom's Heart — and its post-win form It Lives — offer the
           route choice: the boss-room clear sweep drops the usual trapdoor
           (DARK -> Sheol); the "beam of light" beside it is the LIGHT
           ascent to the Cathedral. */
        if (e->type == ENEMY_BOSS_MOMS_HEART ||
            e->type == ENEMY_BOSS_IT_LIVES) {
            r->has_ending_beam = 1;
        }
        /* R9: Isaac's death spawns the same beam object in the Cathedral —
           there it IS the run-ending light exit (ending 2). The trapdoor
           from the clear sweep continues to The Chest (Mega Satan). Any
           light columns die with him. */
        if (e->type == ENEMY_BOSS_ISAAC) {
            r->has_ending_beam = 1;
            g->vbeam_state = 0;
            g->vbeam_timer = 0;
            g->vbeam_count = 0;
        }
        /* E5: Satan showers black hearts on defeat. Also kill any beam the
           corpse was charging — its state machine lives in his AI case.
           R9: The Lamb shares the beam cleanup (cross flag included). */
        if (e->type == ENEMY_BOSS_SATAN || e->type == ENEMY_BOSS_THE_LAMB) {
            spawn_heart(r, e->x - 20, e->y + 10, HEART_BLACK);
            spawn_heart(r, e->x + 20, e->y + 10, HEART_BLACK);
            g->ebeam_state = 0;
            g->ebeam_timer = 0;
            g->ebeam_cross = 0;
        }
        /* R8 (M3): angel defeats drop the Mega Satan key halves. Any light
           column the angel was charging dies with it (same shared-state
           cleanup discipline as Isaac). */
        if (e->type == ENEMY_BOSS_URIEL || e->type == ENEMY_BOSS_GABRIEL) {
            if (e->type == ENEMY_BOSS_URIEL) g->player.has_key_piece_1 = 1;
            else                             g->player.has_key_piece_2 = 1;
            snprintf(g->pickup_msg_text, sizeof(g->pickup_msg_text),
                     (e->type == ENEMY_BOSS_URIEL)
                         ? "Key Piece 1 - half of the golden key"
                         : "Key Piece 2 - half of the golden key");
            g->pickup_msg_timer = 180;
            g->vbeam_state = 0;
            g->vbeam_timer = 0;
            g->vbeam_count = 0;
        }
        /* R8 (M7): Krampus leaves his signature pedestal — 50/50 Lump of
           Coal / Head of Krampus (the ONLY source of both). His brimstone
           cross dies with him (shared ebeam machine). */
        if (e->type == ENEMY_BOSS_KRAMPUS) {
            r->pedestal.x = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
            r->pedestal.y = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
            r->pedestal.item = randi(0, 1) ? ITEM_HEAD_OF_KRAMPUS
                                           : ITEM_LUMP_OF_COAL;
            r->pedestal.active = (g->challenge != 6);
            g->ebeam_state = 0;
            g->ebeam_timer = 0;
            g->ebeam_cross = 0;
        }
        /* R8 (M3): Mega Satan is now ONLY fought behind the golden door,
           and his defeat ends the run directly with the classic escape
           ending (his room has no exits — no trapdoor epilogue). */
        if (e->type == ENEMY_BOSS_MEGA_SATAN) {
            g->win_ending = 0;
            g->state = STATE_WIN;
            audio_play(SFX_ROOM_CLEAR);
            unlock_check_after_win(g);
        }
        /* B3: the boss-defeat epilogue (clear boss_active + floor music)
           only fires when NO other boss-grade enemy is still alive — a
           Boss Rush wave partner or surviving Fistula pieces keep the
           fight (and its music) going. During the Boss Rush gauntlet the
           music is never switched here; the rush's own clear path in
           enemies_update owns the end-of-gauntlet state. */
        {
            int others_alive = 0;
            for (int oi = 0; oi < r->enemy_count; oi++) {
                Enemy *oe = &r->enemies[oi];
                if (!oe->active) continue;
                if (is_boss_type(oe->type) ||
                    oe->type == ENEMY_FISTULA_BALL) {
                    others_alive = 1;
                    break;
                }
            }
            if (!others_alive) {
                g->boss_active = 0;
                /* Return to floor music after boss defeat */
                if (!g->bossrush_active)
                    music_play(music_for_floor(g->current_floor));
            }
        }
    } else {
        trigger_shake(g, 1.5f, 6);
        /* R1: the main Fistula corpse skips the epilogue while its balls
           live (see B3 above) — so when the LAST ball dies, the release
           must fire from here or boss_active + boss music stay stuck
           until the floor changes. Balls only run the release: no drops
           bitmask / death-anim boss bookkeeping for a chunk. */
        if (e->type == ENEMY_FISTULA_BALL && g->boss_active) {
            int others_alive = 0;
            for (int oi = 0; oi < r->enemy_count; oi++) {
                Enemy *oe = &r->enemies[oi];
                if (!oe->active) continue;
                if (is_boss_type(oe->type) ||
                    oe->type == ENEMY_FISTULA_BALL) {
                    others_alive = 1;
                    break;
                }
            }
            if (!others_alive) {
                g->boss_active = 0;
                if (!g->bossrush_active)
                    music_play(music_for_floor(g->current_floor));
            }
        }
    }
    return 1;
}

/* R8 (M3): wake the angel statue in an angel room. Removes the statue and
 * every remaining freebie (pedestal / hearts / pickups), locks the room,
 * and spawns URIEL (first angel awakened this run) or GABRIEL (second+).
 * Triggered by bombing the statue or by room-wide damage (Necronomicon /
 * Death card / black-heart burst) landing in the room. Minibosses spawn
 * with FLAT innate HP — no floor scaling (see boss_innate_hp note).
 * alloc_dynamic_enemy's spawn_grace(10) shields the fresh angel from the
 * very blast that woke it. Angels never spawn in boss rooms, so the vbeam
 * machine is free here — their AI still guards vbeam_state anyway. */
static void awaken_angel(Game *g, Room *r) {
    if (!r || r->type != ROOM_ANGEL) return;
    Obstacle *st = NULL;
    for (int i = 0; i < r->obstacle_count; i++) {
        if (r->obstacles[i].active &&
            r->obstacles[i].type == OBST_ANGEL_STATUE) {
            st = &r->obstacles[i];
            break;
        }
    }
    if (!st) return;   /* no intact statue = nothing to awaken */
    st->active = 0;

    /* The fight replaces the room's remaining pickups */
    r->pedestal.active = 0;
    for (int i = 0; i < MAX_HEART_PICKUPS; i++)      r->hearts[i].active = 0;
    for (int i = 0; i < MAX_CONSUMABLE_PICKUPS; i++) r->consumables[i].active = 0;

    EnemyType at = (g->angels_fought == 0) ? ENEMY_BOSS_URIEL
                                           : ENEMY_BOSS_GABRIEL;
    g->angels_fought++;

    Enemy *e = alloc_dynamic_enemy(r);
    if (!e) return;    /* can't happen in practice: angel rooms are empty */
    const FloorInfo *afi = get_floor_info(g->current_floor);
    init_boss_enemy(g, afi, e, at, st->x);
    e->hp = e->max_hp = (float)boss_innate_hp(at);   /* flat miniboss HP */
    e->x = st->x;
    e->y = st->y + 14.0f;

    r->cleared = 0;    /* doors slam shut until the angel falls */
    g->boss_active = 1;
    g->boss_name = boss_name_str(at);
    music_play(MUS_BOSS);
    audio_play(SFX_BOSS);
    trigger_shake(g, 5.0f, 24);
    if (g->player.iframes < 45) g->player.iframes = 45;
}

/* Deal flat damage to every enemy in the room, routing deaths through the
 * shared kill_enemy path (drops, champion splits, boss tracking). Captures
 * the initial count so death-spawned splits aren't hit by the same burst.
 * Used by TAROT_DEATH, the Necronomicon (E7) and black-heart bursts (E5). */
static void damage_all_enemies(Game *g, float dmg) {
    Room *r = current_room(g);
    if (!r) return;
    /* R8 (M3): room-wide damage counts as desecrating the angel statue */
    awaken_angel(g, r);
    int initial = r->enemy_count;
    for (int i = 0; i < initial; i++) {
        Enemy *e = &r->enemies[i];
        if (!e->active) continue;
        /* B5: skip just-spawned enemies (death-spawns of this same sweep).
           When the array is full, alloc_dynamic_enemy reuses a dead slot
           BELOW the captured `initial`, so the capture alone isn't enough —
           spawn_grace marks them reliably (belt and suspenders). R4: only
           fresh spawns (same-frame or one-frame-old) are immune — a wave
           boss 10 frames young should not shrug off the whole blast. */
        if (e->spawn_grace >= 9) continue;
        e->hp -= dmg;
        e->flash = 14;
        if (e->hp <= 0) kill_enemy(g, r, e);
    }
}

/* B1: detonate any queued black-heart bursts NOW, in the room the player is
 * still standing in. Must run before any room switch (door transition or
 * warp initiation) so the nuke lands where the heart broke, never in the
 * room being entered. The per-frame drain in STATE_PLAYING still covers
 * normal combat. */
static void drain_black_burst(Game *g) {
    if (g_black_burst_pending <= 0) return;
    damage_all_enemies(g, 40.0f * (float)g_black_burst_pending);
    g_black_burst_pending = 0;
    trigger_shake(g, 6.0f, 24);
    audio_play(SFX_ENEMY_DEATH);
}

/* R8 (M3): golden-door capstone. Lazily creates a dedicated Mega Satan
 * boss room in a free grid cell — no doors in or out — and warps the
 * player in (do_warp_cleanup triggers the boss intro/music/name). Both
 * key pieces are consumed on the FIRST open; the door then stays open
 * (mega_created) so a Teleport! escape can never lock the fight away.
 * Mega Satan keeps his full floor-scaled HP, arena, AI and ending. */
static void open_mega_satan_room(Game *g) {
    Dungeon *d = &g->dungeon;
    if (!g->mega_created) {
        int fx = -1, fy = -1;
        for (int yy = 0; yy < DUNGEON_H && fx < 0; yy++) {
            for (int xx = 0; xx < DUNGEON_W; xx++) {
                if (d->rooms[yy][xx].type == ROOM_NONE) {
                    fx = xx; fy = yy;
                    break;
                }
            }
        }
        if (fx < 0) return;  /* no free cell — floors cap at 15 of 25 rooms,
                                so this is unreachable; door just refuses */
        Room *mr = &d->rooms[fy][fx];
        memset(mr, 0, sizeof(Room));
        mr->gx = fx;
        mr->gy = fy;
        mr->type = ROOM_BOSS;
        mr->cleared = 0;
        mr->enemies_spawned = 1;   /* hand-stocked below */
        mr->enemy_count = 1;
        Enemy *me = &mr->enemies[0];
        memset(me, 0, sizeof(Enemy));
        me->active = 1;
        init_boss_enemy(g, get_floor_info(g->current_floor), me,
                        ENEMY_BOSS_MEGA_SATAN,
                        (ROOM_LEFT + ROOM_RIGHT) / 2.0f);
        d->room_count++;
        g->mega_created = 1;
        g->mega_gx = fx;
        g->mega_gy = fy;
        /* The golden key is spent opening the seal */
        g->player.has_key_piece_1 = 0;
        g->player.has_key_piece_2 = 0;
    }
    g->current_boss_type = ENEMY_BOSS_MEGA_SATAN;
    drain_black_burst(g);       /* burst detonates in the room being left */
    d->cur_x = g->mega_gx;
    d->cur_y = g->mega_gy;
    audio_play(SFX_DOOR);
    do_warp_cleanup(g);
}

void place_bomb(Game *g) {
    if (g->player.bombs <= 0) return;
    int slot = -1;
    for (int i = 0; i < MAX_BOMBS; i++) {
        if (!g->bombs[i].active) { slot = i; break; }
    }
    if (slot < 0) return;                /* bomb pool full */
    g->player.bombs--;
    g->bombs[slot].x = g->player.x;
    g->bombs[slot].y = g->player.y;
    g->bombs[slot].vx = 0;
    g->bombs[slot].vy = 0;
    g->bombs[slot].timer = 90;   /* ~1.5 seconds at 60fps */
    g->bombs[slot].flash = 0;
    g->bombs[slot].is_epic = 0;
    g->bombs[slot].is_fetus = 0;
    g->bombs[slot].active = 1;
    audio_play(SFX_SHOOT);  /* reuse shoot sound for placement */
}

/* Arm a troll bomb (Tower card / Explosive Diarrhea): a live pool bomb that
 * explodes on the normal fuse and CAN hurt the player.
 * Returns 1 if a pool slot was free. */
static int spawn_troll_bomb(Game *g, float x, float y) {
    for (int i = 0; i < MAX_BOMBS; i++) {
        if (g->bombs[i].active) continue;
        g->bombs[i].x = x;
        g->bombs[i].y = y;
        g->bombs[i].vx = 0;
        g->bombs[i].vy = 0;
        g->bombs[i].timer = randi(60, 110);  /* staggered fuses */
        g->bombs[i].flash = 0;
        g->bombs[i].is_epic = 0;
        g->bombs[i].is_fetus = 0;
        g->bombs[i].active = 1;
        return 1;
    }
    return 0;
}

/* Parameterized explosion core. Every explosion that can touch the player
 * funnels through here so Pyromaniac (D6) gates them all in one place.
 *   blast      - radius in pixels
 *   enemy_dmg  - damage to non-boss enemies in the radius
 *   boss_dmg   - damage to bosses in the radius
 *   player_dmg - half-hearts dealt to the player if caught in the radius */
static void bomb_explode_ex(Game *g, float bx, float by, float blast,
                            float enemy_dmg, float boss_dmg, int player_dmg) {
    Room *r = current_room(g);

    trigger_shake(g, 6.0f, 20);
    audio_play(SFX_ENEMY_DEATH);  /* reuse for explosion sound */

    /* Destroy obstacles (rocks/poop) in blast radius. Slot machines and
       spikes are blast-proof. R8 (M3): the angel statue is not destroyed —
       a blast that reaches it AWAKENS the angel instead. */
    for (int i = 0; i < r->obstacle_count; i++) {
        Obstacle *o = &r->obstacles[i];
        if (!o->active) continue;
        if (o->type == OBST_SLOT_MACHINE || o->type == OBST_SPIKES) continue;
        if (o->type == OBST_ANGEL_STATUE) {
            float sdx = o->x - bx, sdy = o->y - by;
            if (sdx * sdx + sdy * sdy < (blast + 14.0f) * (blast + 14.0f))
                awaken_angel(g, r);
            continue;
        }
        float dx = o->x - bx, dy = o->y - by;
        if (dx * dx + dy * dy < blast * blast) {
            o->active = 0;
            /* Chance to drop a consumable from destroyed rock */
            if (randi(0, 100) < 35) {
                spawn_random_consumable(r, o->x, o->y);
            }
        }
    }

    /* Blasts hurt the player too (troll bombs and careless placement alike).
       D6 Pyromaniac (ITEM_FLAG_FIRE_IMMUNE): immune to ALL explosion damage;
       instead each blast that catches the player HEALS half a heart. */
    Player *p = &g->player;
    {
        float pdx = p->x - bx, pdy = p->y - by;
        int p_inside = (pdx * pdx + pdy * pdy < blast * blast);
        if (p->stats.flags & ITEM_FLAG_FIRE_IMMUNE) {
            if (p_inside) {
                if (p->hp < p->stats.max_hp) p->hp += 1;
                audio_play(SFX_PICKUP);
            }
        } else if (p_inside && p->iframes == 0) {
            p->hp -= player_absorb_dmg(p, player_dmg);
            p->iframes = PLAYER_IFRAMES;
            g->hitstop = 3;
            audio_play(SFX_HURT_GRUNT);
            trigger_shake(g, 5.0f, 18);
            if (player_check_death(p)) {
                audio_play(SFX_PLAYER_DEATH);
                g->state = STATE_GAMEOVER;
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
        /* B5: fresh death-spawns can land in a reused dead slot below the
           captured count when the array is full — spawn_grace marks them.
           R4: only fresh spawns (grace >= 9) are blast-immune. */
        if (e->spawn_grace >= 9) continue;
        /* R8 #26: hidden/burrowed enemies (Host in shell, Pin/Scolex/Round
           Worm underground) are safe from blasts, matching tear immunity. */
        if (e->hidden) continue;
        float dx = e->x - bx, dy = e->y - by;
        if (dx * dx + dy * dy < blast * blast) {
            float bomb_prev_hp = e->hp;
            /* Standard bombs kill any non-boss outright; bosses take a chunk */
            e->hp -= is_boss_type(e->type) ? boss_dmg : enemy_dmg;
            e->flash = 14;  /* punchier white-hit flash on bomb damage */
            /* Knockback away from bomb */
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > 0.1f) {
                e->x += (dx / dist) * 20.0f;
                e->y += (dy / dist) * 20.0f;
            }

            /* Fistula: splits into a couple of smaller balls every time it
               crosses a quarter-HP threshold, as long as it survives the hit. */
            if (e->type == ENEMY_BOSS_FISTULA && e->hp > 0) {
                float quarter = e->max_hp * 0.25f;
                if (quarter < 1.0f) quarter = 1.0f;
                int thresholds_crossed = (int)(bomb_prev_hp / quarter)
                                       - (int)(e->hp / quarter);
                if (thresholds_crossed > 0) {
                    float ball_hp = e->max_hp / 6.0f;
                    if (ball_hp < 1.0f) ball_hp = 1.0f;
                    spawn_fistula_ball(r, e->x, e->y, ball_hp);
                    spawn_fistula_ball(r, e->x, e->y, ball_hp);
                }
            }

            /* Handle enemy death from bomb (shared death path) */
            if (e->hp <= 0) kill_enemy(g, r, e);
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

/* Standard bomb: 48px blast, kills any non-boss (60), chunks bosses (15),
 * hits the player for a full heart (2 half-hearts). */
static void bomb_explode(Game *g, float bx, float by) {
    bomb_explode_ex(g, bx, by, 48.0f, 60.0f, 15.0f, 2);
}

void bomb_update(Game *g) {
    for (int i = 0; i < MAX_BOMBS; i++) {
        ActiveBomb *b = &g->bombs[i];
        if (!b->active) continue;
        /* D4 Dr. Fetus tear-bombs slide with friction, then sit on the fuse */
        if (b->vx != 0.0f || b->vy != 0.0f) {
            b->x += b->vx;
            b->y += b->vy;
            b->vx *= 0.90f;
            b->vy *= 0.90f;
            if (b->vx * b->vx + b->vy * b->vy < 0.05f) { b->vx = 0; b->vy = 0; }
            /* Stop dead at walls */
            if (b->x < ROOM_LEFT + 6)   { b->x = ROOM_LEFT + 6;   b->vx = 0; }
            if (b->x > ROOM_RIGHT - 6)  { b->x = ROOM_RIGHT - 6;  b->vx = 0; }
            if (b->y < ROOM_TOP + 6)    { b->y = ROOM_TOP + 6;    b->vy = 0; }
            if (b->y > ROOM_BOTTOM - 6) { b->y = ROOM_BOTTOM - 6; b->vy = 0; }
        }
        b->timer--;
        b->flash++;
        if (b->timer > 0) continue;
        b->active = 0;
        if (b->is_fetus) {
            /* D4 Dr./Epic Fetus tear-bombs scale with the damage stat.
               Baseline: the historical fixed numbers at Isaac's base 3.5
               damage, so balance is unchanged for an itemless run. */
            float ds = g->player.stats.damage / 3.5f;
            if (ds < 0.2f) ds = 0.2f;   /* keep blasts meaningful */
            if (b->is_epic) {
                /* Epic Fetus: x1.5 blast radius and x1.5 damage */
                bomb_explode_ex(g, b->x, b->y, 72.0f,
                                90.0f * ds, 22.5f * ds, 2);
            } else {
                bomb_explode_ex(g, b->x, b->y, 48.0f,
                                60.0f * ds, 15.0f * ds, 2);
            }
        } else {
            bomb_explode(g, b->x, b->y);
        }
    }
}

/* ================================================================
 * Beam weapons (D1 Brimstone / D2 Technology) + Mom's Knife (D3)
 * ================================================================ */

/* Advance the live beam one frame: recompute the player->wall segment
 * (bent toward the nearest on-line enemy for the Brimstone + Spoon Bender
 * synergy, D7) and damage every enemy the segment crosses.
 * Brimstone: 10-frame sweep totalling ~3x tear damage (dmg*0.30/frame).
 * Technology: continuous while firing at dmg*0.25/frame. */
static void laser_update(Game *g) {
    if (!g->laser_active) return;
    g->laser_timer--;
    if (g->laser_timer <= 0) { g->laser_active = 0; return; }

    Player *p = &g->player;
    Room *r = current_room(g);
    float ox = p->x;
    float oy = p->y - PLAYER_SIZE * 0.3f;   /* fires from eye height */

    /* Endpoint: straight to the wall in the (cardinal) aim direction */
    float ex = ox, ey = oy;
    if      (g->laser_dx > 0) ex = ROOM_RIGHT;
    else if (g->laser_dx < 0) ex = ROOM_LEFT;
    else if (g->laser_dy > 0) ey = ROOM_BOTTOM;
    else                      ey = ROOM_TOP;

    /* R10 (C4) Azazel: his innate Brimstone is SHORT-RANGE — clamp the
       beam to ~55% of the wall distance unless the real Brimstone item
       was picked up (which restores the full-length beam). */
    if (!g->laser_is_tech && p->character == CHAR_AZAZEL &&
        !player_has_item(p, ITEM_BRIMSTONE)) {
        ex = ox + (ex - ox) * 0.55f;
        ey = oy + (ey - oy) * 0.55f;
    }

    /* D7 Brimstone + Spoon Bender/Sacred Heart (synergy grants HOMING in
       recalc_player_stats): bend the far endpoint toward the closest enemy
       within ~60px of the beam line. */
    if (!g->laser_is_tech && (p->stats.flags & ITEM_FLAG_HOMING)) {
        float sx = ex - ox, sy = ey - oy;
        float slen2 = sx * sx + sy * sy;
        if (slen2 > 1.0f) {
            float best_d = 60.0f;
            Enemy *best = NULL;
            for (int i = 0; i < r->enemy_count; i++) {
                Enemy *e = &r->enemies[i];
                if (!e->active || e->hidden) continue;
                float t = ((e->x - ox) * sx + (e->y - oy) * sy) / slen2;
                if (t < 0.1f || t > 1.0f) continue;
                float px = ox + sx * t, py = oy + sy * t;
                float ddx = e->x - px, ddy = e->y - py;
                float dd = sqrtf(ddx * ddx + ddy * ddy);
                if (dd < best_d) { best_d = dd; best = e; }
            }
            if (best) {
                ex += (best->x - ex) * 0.65f;
                ey += (best->y - ey) * 0.65f;
            }
        }
    }
    g->laser_ex = ex;
    g->laser_ey = ey;

    float per_frame = g->laser_is_tech ? p->stats.damage * 0.25f
                                       : p->stats.damage * 0.30f;
    float half_w = g->laser_is_tech ? 2.0f : 6.0f;
    float sx = ex - ox, sy = ey - oy;
    float slen2 = sx * sx + sy * sy;
    if (slen2 < 1.0f) return;
    for (int i = 0; i < r->enemy_count; i++) {
        Enemy *e = &r->enemies[i];
        if (!e->active || e->hidden) continue;
        float esz = is_boss_type(e->type) ? ENEMY_SIZE * 2 : ENEMY_SIZE;
        /* Distance from enemy center to the beam segment */
        float t = ((e->x - ox) * sx + (e->y - oy) * sy) / slen2;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        float px = ox + sx * t, py = oy + sy * t;
        float ddx = e->x - px, ddy = e->y - py;
        float rr = esz + half_w;
        if (ddx * ddx + ddy * ddy >= rr * rr) continue;
        e->hp -= per_frame;
        e->flash = 4;
        /* Light gore while the beam burns (every 4th frame, deterministic) */
        if ((g->frame & 3) == 0)
            spawn_blood_splatter(g, e->x, e->y, sx * 0.002f, sy * 0.002f, 0);
        if (e->hp <= 0) kill_enemy(g, r, e);
    }
}

/* D3 Mom's Knife: single entity. Held in front of the player until thrown;
 * flies out to range, then homes back to the player's hand. Damages on
 * overlap in both flight states at 2x tear damage (a short re-hit cooldown
 * makes each pass-through count once per enemy pass). */
static void knife_update(Game *g) {
    Player *p = &g->player;
    if (!(p->stats.flags & ITEM_FLAG_KNIFE)) { g->knife_state = 0; return; }
    Room *r = current_room(g);

    if (g->knife_hit_cd > 0) g->knife_hit_cd--;

    if (g->knife_state == 0) {
        /* Held: hover just in front of the facing direction (no damage) */
        float dx, dy;
        dir_to_vec((p->face_dir == DIR_NONE) ? DIR_DOWN : p->face_dir,
                   &dx, &dy);
        g->knife_x = p->x + dx * (PLAYER_SIZE + 4.0f);
        g->knife_y = p->y - PLAYER_SIZE * 0.3f + dy * (PLAYER_SIZE + 4.0f);
        g->knife_dx = dx;
        g->knife_dy = dy;
        return;
    }

    float spd = TEAR_BASE_SPEED * (1.0f + p->stats.shot_speed * 0.15f) * 1.6f;
    if (g->knife_state == 1) {
        /* Thrown: fly straight out to range (walls also turn it around) */
        g->knife_x += g->knife_dx * spd;
        g->knife_y += g->knife_dy * spd;
        g->knife_dist += spd;
        if (g->knife_dist >= p->stats.range ||
            g->knife_x < ROOM_LEFT || g->knife_x > ROOM_RIGHT ||
            g->knife_y < ROOM_TOP  || g->knife_y > ROOM_BOTTOM) {
            g->knife_state = 2;   /* returning */
        }
    } else {
        /* Returning: home back to the player's hand */
        float hdx = p->x - g->knife_x;
        float hdy = p->y - g->knife_y;
        float hm = sqrtf(hdx * hdx + hdy * hdy);
        if (hm < spd + 4.0f) { g->knife_state = 0; return; }
        g->knife_dx = hdx / hm;
        g->knife_dy = hdy / hm;
        g->knife_x += g->knife_dx * spd;
        g->knife_y += g->knife_dy * spd;
    }

    /* Damage on overlap in both flight states */
    if (g->knife_hit_cd == 0) {
        for (int i = 0; i < r->enemy_count; i++) {
            Enemy *e = &r->enemies[i];
            if (!e->active || e->hidden) continue;
            float esz = is_boss_type(e->type) ? ENEMY_SIZE * 2 : ENEMY_SIZE;
            float kdx = g->knife_x - e->x, kdy = g->knife_y - e->y;
            float rr = esz + 6.0f;
            if (kdx * kdx + kdy * kdy >= rr * rr) continue;
            float dmg = p->stats.damage * 2.0f;
            e->hp -= dmg;
            e->flash = 14;
            audio_play(SFX_HIT);
            spawn_blood_splatter(g, g->knife_x, g->knife_y,
                                 g->knife_dx, g->knife_dy, 1);
            /* Knockback along the knife's travel direction */
            {
                float kbScale = is_boss_type(e->type) ? 0.25f : 1.0f;
                float kbMag = TEAR_KNOCKBACK * (0.5f + dmg * 0.4f) * kbScale;
                e->kb_dx += g->knife_dx * kbMag;
                e->kb_dy += g->knife_dy * kbMag;
            }
            if (e->hp <= 0) kill_enemy(g, r, e);
            g->knife_hit_cd = 8;
            break;
        }
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
                g->hitstop = 3;
                /* Knockback from projectile direction */
                float sv = sqrtf(s->dx * s->dx + s->dy * s->dy);
                if (sv > 0.1f) {
                    p->vx = (s->dx / sv) * PLAYER_KB_FORCE * 0.7f;
                    p->vy = (s->dy / sv) * PLAYER_KB_FORCE * 0.7f;
                }
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

    /* E9 Challenge "Speed!": every enemy AI reads its speed through the
       floor multipliers, so scale a local copy 1.4x for the whole update. */
    FloorInfo fi_speed;
    if (g->challenge == 4) {
        fi_speed = *fi;
        fi_speed.enemy_speed_mult *= 1.4f;
        fi_speed.boss_speed_scale *= 1.4f;
        fi = &fi_speed;
    }

    /* R8 (M7): cosmetic timers (Head of Krampus burst render, lights-dim
       pulse) + the Krampus ambush countdown for armed devil rooms. */
    if (g->pbeam_timer > 0) g->pbeam_timer--;
    if (g->krampus_dim > 0) g->krampus_dim--;
    if (r->type == ROOM_DEVIL && r->krampus_state == 1 &&
        g->krampus_timer > 0) {
        g->krampus_timer--;
        if (g->krampus_timer == 0) {
            Enemy *ke = alloc_dynamic_enemy(r);
            if (ke) {
                r->krampus_state = 2;   /* never re-arms */
                init_boss_enemy(g, fi, ke, ENEMY_BOSS_KRAMPUS,
                                (ROOM_LEFT + ROOM_RIGHT) / 2.0f);
                /* Minibosses use FLAT innate HP (no floor scaling) */
                ke->hp = ke->max_hp =
                    (float)boss_innate_hp(ENEMY_BOSS_KRAMPUS);
                ke->y = ROOM_TOP + 60.0f;
                r->cleared = 0;
                g->boss_active = 1;
                g->boss_name = boss_name_str(ENEMY_BOSS_KRAMPUS);
                music_play(MUS_BOSS);
                audio_play(SFX_BOSS);
                trigger_shake(g, 6.0f, 24);
                g->krampus_dim = 40;    /* brief lights-down pulse */
                if (g->player.iframes < 45) g->player.iframes = 45;
            }
        }
    }

    /* Pause AI during boss intro */
    if (g->boss_intro_timer > 0) {
        for (int i = 0; i < r->enemy_count; i++) {
            if (r->enemies[i].flash > 0) r->enemies[i].flash--;
        }
        return;
    }

    /* ── Boss Rush wave controller ──
       Drives the ROOM_BOSSRUSH gauntlet: 2 bosses per wave, 6 waves total,
       drawn from the same boss pools/init path as normal boss rooms. The
       room is guaranteed to become clearable -- see the safety net below. */
    if (r->type == ROOM_BOSSRUSH && !r->cleared) {
        if (!g->bossrush_active) {
            /* First frame in the room: start (or RESUME) the gauntlet.
               R8 #25: wave progress lives in r->bossrush_wave, so warping
               out and back in never refights waves already cleared. If an
               interrupted wave left bosses alive, re-arm the boss HUD. */
            g->bossrush_active = 1;
            g->bossrush_spawn_timer = 30; /* brief grace period */
            for (int i = 0; i < r->enemy_count; i++) {
                if (r->enemies[i].active && is_boss_type(r->enemies[i].type)) {
                    g->boss_active = 1;
                    g->boss_name = boss_name_str(r->enemies[i].type);
                    break;
                }
            }
        }

        if (g->bossrush_spawn_timer > 0) {
            g->bossrush_spawn_timer--;
        } else {
            int alive_bosses = 0;
            for (int i = 0; i < r->enemy_count; i++) {
                if (r->enemies[i].active) alive_bosses++;
            }

            if (alive_bosses == 0) {
                if (r->bossrush_wave >= BOSSRUSH_TOTAL_WAVES) {
                    /* Anti-softlock safety net: zero alive enemies and the wave
                       counter is at/after the final wave -- always clear here,
                       regardless of how we got to this state. */
                    r->cleared = 1;
                    g->bossrush_active = 0;
                    g->boss_active = 0;
                    audio_play(SFX_ROOM_CLEAR);

                    /* Reward: item pedestal + a full heal (guarantees the room
                       is worth clearing and never leaves the player stuck).
                       E9 "The Purist": no pedestals — hearts only. */
                    r->pedestal.x = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
                    r->pedestal.y = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
                    r->pedestal.item = pick_random_item(g);
                    r->pedestal.active = (g->challenge != 6);
                    spawn_heart(r, ROOM_LEFT + 60, ROOM_TOP + 60, HEART_RED_FULL);
                    spawn_heart(r, ROOM_RIGHT - 60, ROOM_TOP + 60, HEART_RED_FULL);

                    /* E9 "The Purist": the final floor's boss room was
                       replaced by this Boss Rush — its clear grants the
                       winning trapdoor (advance past the last floor). */
                    if (g->challenge == 6 &&
                        g->current_floor == MAX_FLOORS - 1) {
                        r->has_trapdoor = 1;
                    }
                } else {
                    /* Spawn the next wave: 2 bosses from the floor's boss pool.
                       B2: reset enemy_count BEFORE computing capacity — every
                       slot is dead here (alive check above found none), and a
                       minion-filled array must not clamp the wave to 0-1. */
                    float cx = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
                    r->enemy_count = 0; /* deactivate stale corpse slots */
                    int to_spawn = 2;
                    if (to_spawn > MAX_ENEMIES - r->enemy_count)
                        to_spawn = MAX_ENEMIES - r->enemy_count;
                    /* F5: wave spawn safety — room-entry-style iframes so a
                       boss materialising on top of the player can't land an
                       unavoidable hit. */
                    if (g->player.iframes < 40) g->player.iframes = 40;
                    for (int i = 0; i < to_spawn; i++) {
                        Enemy *e = &r->enemies[r->enemy_count++];
                        memset(e, 0, sizeof(Enemy));
                        e->active = 1;
                        e->spawn_grace = 10; /* F6: no same-frame contact */
                        /* Boss Rush waves draw from the RANDOM pools. Floors
                           4+ have fixed story bosses in select_boss_for_floor,
                           so map late floors onto pool floors — otherwise
                           every wave on a late floor would be the same fixed
                           boss (Mom x12...). Floor 4 -> Depths pool (Fistula),
                           5+ -> Womb pool (Scolex/Loki), keeping every
                           pool boss reachable for the 20-boss collection. */
                        int pool_floor = (g->current_floor > 5) ? 5 : g->current_floor;
                        EnemyType selected_boss = select_pool_boss_for_floor(g, pool_floor);
                        push_boss_history(g, selected_boss);
                        /* F5: proximity rejection — bosses spawn at
                           (cx±40, ROOM_TOP+60); if that lands within 70px of
                           the player, flip to the opposite side, and failing
                           that spawn at the far corner from the player. */
                        float sx = cx + (i == 0 ? -40.0f : 40.0f);
                        float sy = ROOM_TOP + 60.0f;
                        float pdx = sx - g->player.x;
                        float pdy = sy - g->player.y;
                        if (pdx * pdx + pdy * pdy < 70.0f * 70.0f) {
                            sx = cx + (i == 0 ? 40.0f : -40.0f);
                            pdx = sx - g->player.x;
                            if (pdx * pdx + pdy * pdy < 70.0f * 70.0f) {
                                sx = (g->player.x < cx) ? (ROOM_RIGHT - 60.0f)
                                                        : (ROOM_LEFT + 60.0f);
                            }
                        }
                        init_boss_enemy(g, fi, e, selected_boss, sx);
                        g->current_boss_type = selected_boss;
                        g->boss_name = boss_name_str(selected_boss);

                        /* Same difficulty + infinite-loop HP scaling the
                           normal boss-room spawn path applies */
                        float hp_mult = diff_enemy_hp_mult(g->difficulty);
                        if (g->game_mode == MODE_INFINITE && g->infinite_loop > 0) {
                            hp_mult *= 1.0f + g->infinite_loop * 0.25f;
                        }
                        if (hp_mult != 1.0f) {
                            e->hp = e->hp * hp_mult;
                            if (e->hp < 1) e->hp = 1;
                            e->max_hp = e->hp;
                            /* R8 #19: scale the twin head with the body */
                            e->gemini_chp = (int)((float)e->gemini_chp * hp_mult);
                        }
                    }
                    r->bossrush_wave++;
                    g->boss_active = 1;
                    g->bossrush_spawn_timer = 40; /* short breather before next check */
                    trigger_shake(g, 4.0f, 20);
                    audio_play(SFX_BOSS);
                }
            }
        }
    }

    for (int i = 0; i < r->enemy_count; i++) {
        Enemy *e = &r->enemies[i];
        if (!e->active) continue;

        if (e->flash > 0) e->flash--;
        if (e->spawn_grace > 0) e->spawn_grace--; /* F6: post-spawn contact grace */
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

        /* R8 #22: champion speed multiplier — the one-shot dx/dy scaling in
           enemy_make_champion was overwritten by the per-frame velocity
           recomputes below, making blue champions a speed no-op. Derived
           from e->champion (not a stored field) so the many memset-spawn
           paths can never zero it. Applied to the chase/hop/charge movers. */
        float champ_spd = (e->champion == CHAMP_BLUE) ? 1.3f : 1.0f;
        (void)champ_spd;

        switch (e->type) {
        case ENEMY_FLY: {
            /* Rebirth black fly: wanders aimlessly — the threat is only
               bumping into it, never a chase. Re-rolls heading periodically. */
            e->timer--;
            if (e->timer <= 0) {
                float ang = randf(0, 6.28f);
                float spd = 0.7f * fi->enemy_speed_mult * champ_spd;
                e->dx = cosf(ang) * spd;
                e->dy = sinf(ang) * spd;
                e->timer = randi(40, 90);
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
                          fi->enemy_speed_mult * champ_spd;
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

        case ENEMY_FATTY: {
            /* Slow, tanky gaper: same chase steering but sluggish. */
            float gdx = p->x - e->x;
            float gdy = p->y - e->y;
            float gm = sqrtf(gdx * gdx + gdy * gdy);
            float speed = 0.45f * fi->enemy_speed_mult * champ_spd;  /* slower than gaper's 0.8 */
            e->wobble += 0.10f;

            if (gm > 1.0f && !suppressAI) {
                float dirx = gdx / gm;
                float diry = gdy / gm;

                /* Avoid obstacles like a gaper */
                for (int j = 0; j < r->obstacle_count; j++) {
                    Obstacle *o = &r->obstacles[j];
                    if (!o->active) continue;
                    float odx = o->x - e->x;
                    float ody = o->y - e->y;
                    float od2 = odx * odx + ody * ody;
                    if (od2 > 60.0f * 60.0f) continue;
                    float ond = sqrtf(od2);
                    if (ond < 0.1f) continue;
                    float push = (60.0f - ond) / 60.0f;
                    dirx += (-ody / ond) * push * 0.5f;
                    diry += ( odx / ond) * push * 0.5f;
                }

                float dl = sqrtf(dirx * dirx + diry * diry);
                if (dl > 0.1f) { dirx /= dl; diry /= dl; }

                float w = sinf(e->wobble) * 0.3f;
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
                        e->dx = (pdx / pdmag) * 2.8f * fi->enemy_speed_mult * champ_spd;
                        e->dy = (pdy / pdmag) * 2.8f * fi->enemy_speed_mult * champ_spd;
                    }
                }

                if (e->state == 1) {
                    /* Charging */
                    e->x += e->dx;
                    e->y += e->dy;
                    e->timer--;
                    if (e->timer <= 0) {
                        e->state = 0;
                        e->dx = (randi(0, 1) == 0 ? 1 : -1) * 1.2f * fi->enemy_speed_mult * champ_spd;
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

        case ENEMY_CHARGER: {
            /* Pacer variant: paces horizontally, then charges STRAIGHT
             * (locked to an axis) the moment the player lines up with it. */
            float pdx = p->x - e->x;
            float pdy = p->y - e->y;

            if (!suppressAI) {
                if (e->state == 0 &&
                    (fabsf(pdy) < 14.0f || fabsf(pdx) < 14.0f)) {
                    /* Aligned on an axis -> begin a straight charge along it */
                    e->state = 1;
                    e->timer = 40;
                    float spd = 3.2f * fi->enemy_speed_mult * champ_spd;
                    if (fabsf(pdy) < 14.0f) {
                        /* horizontal charge */
                        e->dx = (pdx >= 0 ? 1.0f : -1.0f) * spd;
                        e->dy = 0;
                    } else {
                        /* vertical charge */
                        e->dx = 0;
                        e->dy = (pdy >= 0 ? 1.0f : -1.0f) * spd;
                    }
                }

                if (e->state == 1) {
                    /* Charging straight; bounce off walls, end after timer */
                    e->x += e->dx;
                    e->y += e->dy;
                    e->timer--;
                    if (e->x <= ROOM_LEFT + ENEMY_SIZE || e->x >= ROOM_RIGHT - ENEMY_SIZE ||
                        e->y <= ROOM_TOP + ENEMY_SIZE  || e->y >= ROOM_BOTTOM - ENEMY_SIZE ||
                        e->timer <= 0) {
                        e->state = 0;
                        e->dx = (randi(0, 1) == 0 ? 1 : -1) * 1.2f * fi->enemy_speed_mult * champ_spd;
                        e->dy = 0;
                    }
                } else {
                    /* Pacing horizontally */
                    e->x += e->dx;
                    if (e->x <= ROOM_LEFT + ENEMY_SIZE || e->x >= ROOM_RIGHT - ENEMY_SIZE)
                        e->dx = -e->dx;
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
                        e->dx = ((sdx / sm) + jx) * 3.4f * fi->enemy_speed_mult * champ_spd;
                        e->dy = ((sdy / sm) + jy) * 3.4f * fi->enemy_speed_mult * champ_spd;
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
                    e->dx = (-pdy / pmag) * 1.0f * dirSign * fi->enemy_speed_mult * champ_spd;
                    e->dy = ( pdx / pmag) * 1.0f * dirSign * fi->enemy_speed_mult * champ_spd;
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
            /* Rebirth attack fly: relentless straight chase — no hover or
               rest windows, just constant pursuit. */
            float pdx = p->x - e->x;
            float pdy = p->y - e->y;
            float pd  = sqrtf(pdx * pdx + pdy * pdy);
            if (pd > 1.0f && !suppressAI) {
                float spd = 1.4f * fi->enemy_speed_mult * champ_spd;
                e->dx = (pdx / pd) * spd;
                e->dy = (pdy / pd) * spd;
                e->x += e->dx;
                e->y += e->dy;
            }
            break;
        }

        case ENEMY_POOTER: {
            /* Flies slowly, stops to fire a 2-shot burst (8f apart) */
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
            } else if (e->state == 1) {
                /* Stopped, aiming, then first shot of the burst */
                e->dx *= 0.9f; e->dy *= 0.9f;
                if (e->timer <= 0) {
                    float pdx = p->x - e->x;
                    float pdy = p->y - e->y;
                    float pm  = sqrtf(pdx * pdx + pdy * pdy);
                    if (pm > 0.5f) {
                        spawn_enemy_shot(g, e->x, e->y,
                            (pdx / pm) * 2.5f, (pdy / pm) * 2.5f, 1);
                    }
                    e->state = 2;
                    e->timer = 8; /* gap to the second burst shot */
                }
            } else {
                /* Second burst shot, re-aimed, then back to floating */
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
                    e->dx = rx * 2.5f * fi->enemy_speed_mult * champ_spd;
                    e->dy = ry * 2.5f * fi->enemy_speed_mult * champ_spd;
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
            float spd = 0.4f * fi->enemy_speed_mult * champ_spd;
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
                float spd = 1.8f * fi->enemy_speed_mult * champ_spd;
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
                    e->hp = e->max_hp * 0.5f; /* regenerate to half HP */
                    if (e->hp < 1) e->hp = 1;
                }
            }
            break;
        }

        case ENEMY_BOOM_FLY: {
            /* Rebirth pattern: bounces diagonally around the room (it does
               NOT chase); the threat is the on-death explosion. */
            float spd = 1.4f * fi->enemy_speed_mult * champ_spd;
            if (e->dx == 0 && e->dy == 0) {
                /* start on a random diagonal */
                e->dx = (randi(0, 1) ? 1.0f : -1.0f) * spd * 0.7071f;
                e->dy = (randi(0, 1) ? 1.0f : -1.0f) * spd * 0.7071f;
            }
            if (!suppressAI) {
                e->x += e->dx;
                e->y += e->dy;
                /* bounce off the walls */
                if (e->x < ROOM_LEFT + ENEMY_SIZE)  { e->x = ROOM_LEFT + ENEMY_SIZE;  e->dx = -e->dx; }
                if (e->x > ROOM_RIGHT - ENEMY_SIZE) { e->x = ROOM_RIGHT - ENEMY_SIZE; e->dx = -e->dx; }
                if (e->y < ROOM_TOP + ENEMY_SIZE)   { e->y = ROOM_TOP + ENEMY_SIZE;   e->dy = -e->dy; }
                if (e->y > ROOM_BOTTOM - ENEMY_SIZE){ e->y = ROOM_BOTTOM - ENEMY_SIZE; e->dy = -e->dy; }
            }
            break;
        }

        case ENEMY_MAW: {
            /* Rebirth Maw: floats in an aimless sinusoidal drift (it never
               chases) and periodically fires an aimed shot. */
            e->wobble += 0.03f;
            float spd = 0.6f * fi->enemy_speed_mult;
            if (!suppressAI) {
                e->x += sinf(e->wobble) * spd;
                e->y += cosf(e->wobble * 0.8f) * spd;
            }
            e->shoot_timer--;
            if (e->shoot_timer <= 0) {
                e->shoot_timer = randi(60, 100);
                float pdx = p->x - e->x;
                float pdy = p->y - e->y;
                float pm  = sqrtf(pdx * pdx + pdy * pdy);
                if (pm > 0.5f) {
                    float shotSpd = 2.0f;
                    spawn_enemy_shot(g, e->x, e->y,
                        (pdx / pm) * shotSpd, (pdy / pm) * shotSpd, 1);
                }
            }
            break;
        }

        case ENEMY_MULLIGAN: {
            /* Rebirth pattern: a walking fly nest that AVOIDS the player.
               It never attacks — killing it is what's dangerous (fly burst). */
            float pdx = p->x - e->x;
            float pdy = p->y - e->y;
            float pm  = sqrtf(pdx * pdx + pdy * pdy);
            float spd = 0.8f * fi->enemy_speed_mult * champ_spd;
            e->wobble += 0.12f;
            if (pm > 1.0f && !suppressAI) {
                float w = sinf(e->wobble) * 0.35f;
                /* flee: move away, harder when the player is close */
                float urgency = (pm < 90.0f) ? 1.0f : 0.45f;
                e->x += (-(pdx / pm) + w) * spd * urgency;
                e->y += (-(pdy / pm)) * spd * urgency;
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
                        /* 3-shot fan: -0.25/0/+0.25 rad around the aim line */
                        float shotSpd = 2.5f;
                        float base = atan2f(pdy, pdx);
                        for (int fs = -1; fs <= 1; fs++) {
                            float a = base + (float)fs * 0.25f;
                            spawn_enemy_shot(g, e->x, e->y,
                                cosf(a) * shotSpd, sinf(a) * shotSpd, 1);
                        }
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
            /* Rebirth Red Maw: aggressive chaser that keeps up rapid fire
               while closing on the player. */
            float pdx = p->x - e->x;
            float pdy = p->y - e->y;
            float pm  = sqrtf(pdx * pdx + pdy * pdy);
            if (pm > 1.0f && !suppressAI) {
                float spd = 1.2f * fi->enemy_speed_mult * champ_spd;
                e->x += (pdx / pm) * spd;
                e->y += (pdy / pm) * spd;
            }
            e->shoot_timer--;
            if (e->shoot_timer <= 0) {
                e->shoot_timer = randi(25, 45); /* fast shooting */
                if (pm > 0.5f) {
                    float shotSpd = 3.0f;
                    spawn_enemy_shot(g, e->x, e->y,
                        (pdx / pm) * shotSpd, (pdy / pm) * shotSpd, 1);
                }
            }
            break;
        }

        case ENEMY_LEAPER: {
            /* Jumps toward player; every 3rd hop is a HIGH jump that fires
               the Rebirth-signature 4-way spread on landing. */
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
                        e->dx = (pdx / pm) * 3.0f * fi->enemy_speed_mult * champ_spd;
                        e->dy = (pdy / pm) * 3.0f * fi->enemy_speed_mult * champ_spd;
                    }
                    e->attack_pattern++;  /* hop counter */
                    e->state = 1;
                    e->timer = 24; /* jump duration */
                }
            } else {
                /* In air - arc (high on every 3rd hop, low otherwise) */
                int high_jump = (e->attack_pattern % 3 == 0);
                float t = (float)e->timer / 24.0f;
                e->jump_arc = -sinf(t * 3.14159f) * (high_jump ? 25.0f : 14.0f);
                if (e->timer <= 0) {
                    e->state = 0;
                    e->timer = randi(30, 60);
                    e->jump_arc = 0;
                    e->dx = 0; e->dy = 0;
                    /* 4-way spread only when landing from the high jump */
                    if (high_jump && !suppressAI) {
                        float ls = 2.0f;
                        spawn_enemy_shot(g, e->x, e->y,  ls,  0, 1);
                        spawn_enemy_shot(g, e->x, e->y, -ls,  0, 1);
                        spawn_enemy_shot(g, e->x, e->y,  0,  ls, 1);
                        spawn_enemy_shot(g, e->x, e->y,  0, -ls, 1);
                    }
                }
            }
            if (!suppressAI) { e->x += e->dx; e->y += e->dy; }
            break;
        }

        case ENEMY_TRITE: {
            /* Fast leaping spider: like a Leaper but quicker with a lower,
             * snappier arc and shorter recovery. No landing shot. */
            e->timer--;
            if (e->state == 0) {
                /* On ground, preparing */
                e->dx *= 0.8f; e->dy *= 0.8f;
                e->jump_arc = 0;
                if (e->timer <= 0) {
                    float pdx = p->x - e->x;
                    float pdy = p->y - e->y;
                    float pm  = sqrtf(pdx * pdx + pdy * pdy);
                    if (pm > 1.0f) {
                        e->dx = (pdx / pm) * 4.6f * fi->enemy_speed_mult * champ_spd;  /* faster */
                        e->dy = (pdy / pm) * 4.6f * fi->enemy_speed_mult * champ_spd;
                    }
                    e->state = 1;
                    e->timer = 16; /* shorter jump */
                }
            } else {
                /* In air - low fast arc */
                float t = (float)e->timer / 16.0f;
                e->jump_arc = -sinf(t * 3.14159f) * 14.0f; /* lower arc */
                if (e->timer <= 0) {
                    e->state = 0;
                    e->timer = randi(14, 30); /* quick recovery */
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

        case ENEMY_KEEPER: {
            /* Moves erratically around the room; drops extra coins on death
               (handled in enemy_death_drops / the coin-flagged death path). */
            e->timer--;
            if (e->timer <= 0) {
                float ang = randf(0, 6.28f);
                float spd = 1.6f * fi->enemy_speed_mult * champ_spd;
                e->dx = cosf(ang) * spd;
                e->dy = sinf(ang) * spd;
                e->timer = randi(15, 40);
            }
            if (!suppressAI) {
                e->x += e->dx;
                e->y += e->dy;
            }
            if (e->x <= ROOM_LEFT + ENEMY_SIZE) { e->dx = fabsf(e->dx); }
            if (e->x >= ROOM_RIGHT - ENEMY_SIZE) { e->dx = -fabsf(e->dx); }
            if (e->y <= ROOM_TOP + ENEMY_SIZE) { e->dy = fabsf(e->dy); }
            if (e->y >= ROOM_BOTTOM - ENEMY_SIZE) { e->dy = -fabsf(e->dy); }
            break;
        }

        case ENEMY_SUCKER: {
            /* Floats like a Vis but weaker/slower single-shot instead of a
               double shot, and fires less often. */
            e->wobble += 0.025f;
            float spd = 0.3f * fi->enemy_speed_mult;
            if (!suppressAI) {
                e->x += sinf(e->wobble) * spd;
                e->y += cosf(e->wobble * 0.8f) * spd;
            }
            e->shoot_timer--;
            if (e->shoot_timer <= 0) {
                e->shoot_timer = randi(70, 110);
                float pdx = p->x - e->x;
                float pdy = p->y - e->y;
                float pm  = sqrtf(pdx * pdx + pdy * pdy);
                if (pm > 0.5f) {
                    float shotSpd = 2.0f;
                    spawn_enemy_shot(g, e->x, e->y,
                        (pdx / pm) * shotSpd, (pdy / pm) * shotSpd, 1);
                }
            }
            break;
        }

        case ENEMY_ROUND_WORM: {
            /* Burrows underground (hidden/invulnerable via ENEMY_HOST-style
               hidden flag), tunnels toward the player, then emerges to chase
               briefly before diving again. */
            e->timer--;
            if (e->phase == 1) {
                /* Burrowed: hidden, invulnerable, move toward player */
                e->hidden = 1;
                float bdx = p->x - e->x;
                float bdy = p->y - e->y;
                float bm = sqrtf(bdx * bdx + bdy * bdy);
                if (bm > 1.0f) {
                    float spd = 1.6f * fi->enemy_speed_mult * champ_spd;
                    e->dx = (bdx / bm) * spd;
                    e->dy = (bdy / bm) * spd;
                }
                if (!suppressAI) {
                    e->x += e->dx;
                    e->y += e->dy;
                }
                if (e->timer <= 0) {
                    e->phase = 0;
                    e->hidden = 0;
                    e->timer = randi(50, 80);   /* time exposed */
                }
            } else {
                /* Emerged: vulnerable, chases briefly */
                float pdx = p->x - e->x;
                float pdy = p->y - e->y;
                float pm = sqrtf(pdx * pdx + pdy * pdy);
                if (pm > 1.0f) {
                    float spd = 1.0f * fi->enemy_speed_mult * champ_spd;
                    e->dx = (pdx / pm) * spd;
                    e->dy = (pdy / pm) * spd;
                }
                if (!suppressAI) {
                    e->x += e->dx;
                    e->y += e->dy;
                }
                if (e->timer <= 0) {
                    e->phase = 1;
                    e->timer = randi(60, 100);  /* time burrowed */
                }
            }
            break;
        }

        case ENEMY_SPITTY: {
            /* Stationary spitter: periodically spits a slow projectile at
               the player. Doesn't move, similar cadence to Red Maw but
               slower shots (spit, not a rapid turret). */
            e->shoot_timer--;
            if (e->shoot_timer <= 0) {
                e->shoot_timer = randi(60, 100);
                float pdx = p->x - e->x;
                float pdy = p->y - e->y;
                float pm  = sqrtf(pdx * pdx + pdy * pdy);
                if (pm > 0.5f) {
                    float shotSpd = 1.8f;
                    spawn_enemy_shot(g, e->x, e->y,
                        (pdx / pm) * shotSpd, (pdy / pm) * shotSpd, 1);
                }
            }
            /* Spitty doesn't move */
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
                    /* R8 #44: no contact damage while airborne — the small
                       landing AoE is the punish for standing at ground zero */
                    boss_landing_thump(g, e->x, e->y);
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

            /* R8 #19: dead second head (gemini_chp <= 0) = no movement, no
               contact, not drawn; the surviving body enrages mildly (+20%). */
            if (e->gemini_chp <= 0) speed_main *= 1.2f;

            if (!e->gemini_split && e->hp < e->max_hp / 2) {
                /* Split! */
                e->gemini_split = 1;
                trigger_shake(g, 4.0f, 15);
                boss_phase_juice(g, e);  /* R8 #28 */
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

                /* Companion follows main body with tether constraint
                   (R8 #19: skipped once the head is dead) */
                if (e->gemini_chp > 0) {
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
                }
            } else {
                /* Post-split: main body continues chasing, but faster */
                float gdx = p->x - e->x;
                float gdy = p->y - e->y;
                float gm = sqrtf(gdx * gdx + gdy * gdy);
                if (gm > 1.0f && !suppressAI) {
                    float post_spd = 1.6f * fi->boss_speed_scale;
                    if (e->gemini_chp <= 0) post_spd *= 1.2f; /* R8 #19 enrage */
                    e->x += (gdx / gm) * post_spd;
                    e->y += (gdy / gm) * post_spd;
                }

                /* Companion: aggressive charge behavior */
                e->timer--;
                if (e->gemini_chp <= 0) {
                    /* R8 #19: head dead — no more charges */
                } else if (e->state == 0) {
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

            /* Companion collision with player (R8 #19: dead head = harmless) */
            if (p->iframes == 0 && e->gemini_chp > 0) {
                float cdx = p->x - e->gemini_cx;
                float cdy = p->y - e->gemini_cy;
                float cdsq = cdx * cdx + cdy * cdy;
                float crad = PLAYER_SIZE + ENEMY_SIZE;
                if (cdsq < crad * crad) {
                    p->hp -= player_absorb_dmg(p, 2);
                    p->iframes = PLAYER_IFRAMES;
                    g->hitstop = 3;
                    float cm = sqrtf(cdsq);
                    if (cm > 0.1f) {
                        p->vx = (cdx / cm) * PLAYER_KB_FORCE;
                        p->vy = (cdy / cm) * PLAYER_KB_FORCE;
                    }
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
            /* R8 #23: the per-frame recompute must keep the floor's boss
               speed scale (init applied it, then this overwrote it). */
            e->seg_speed = (1.8f + (1.0f - hpRatio) * 2.0f) * fi->boss_speed_scale;

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
                        p->hp -= player_absorb_dmg(p, 2);
                        p->iframes = PLAYER_IFRAMES;
                        g->hitstop = 3;
                        float sm = sqrtf(sdx * sdx + sdy * sdy);
                        if (sm > 0.1f) {
                            p->vx = (sdx / sm) * PLAYER_KB_FORCE;
                            p->vy = (sdy / sm) * PLAYER_KB_FORCE;
                        }
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

            if (e->phase == 0 && e->hp < e->max_hp * 0.4f) {
                /* Transition to headless phase */
                e->phase = 1;
                e->timer = 10;
                trigger_shake(g, 5.0f, 20);
                boss_phase_juice(g, e);  /* R8 #28: wipe shots BEFORE burst */
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
                boss_phase_juice(g, e);  /* R8 #28 */
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
                    e->hidden = 0;
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
                    e->hidden = 1;
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
                boss_phase_juice(g, e);  /* R8 #28 */
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
                    boss_landing_thump(g, e->x, e->y);  /* R8 #44 */
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

        case ENEMY_BOSS_MOM: {
            /* Mom (E1): fixed Depths boss. Stomping foot reusing Monstro's
               jump-target machinery + "door hands" — minions spawned at the
               door midpoints on a Duke-style spawn timer. */
            e->timer--;

            /* Door-hand minion spawns (any phase), capped at 4 alive */
            e->shoot_timer--;
            if (e->shoot_timer <= 0) {
                e->shoot_timer = DUKE_SPAWN_INTERVAL;
                int minions = 0;
                for (int mi = 0; mi < r->enemy_count; mi++) {
                    if (r->enemies[mi].active &&
                        !is_boss_type(r->enemies[mi].type)) minions++;
                }
                if (minions < 4) {
                    float mmx = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
                    float mmy = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
                    float doorpos[4][2] = {
                        {mmx, ROOM_TOP + 14}, {mmx, ROOM_BOTTOM - 14},
                        {ROOM_LEFT + 14, mmy}, {ROOM_RIGHT - 14, mmy}
                    };
                    int dpick = randi(0, 3);
                    /* B6: dead-slot reuse so a ratcheted enemy_count can't
                       starve Mom's pressure (active/spawn_grace preset) */
                    Enemy *m = alloc_dynamic_enemy(r);
                    if (m) {
                        m->type = (randi(0, 1) == 0) ? ENEMY_GAPER : ENEMY_BABY;
                        /* T5: same difficulty/infinite HP scaling as room spawns */
                        m->hp = m->max_hp =
                            ((m->type == ENEMY_GAPER) ? 6.0f : 3.0f) * spawn_hp_mult(g);
                        m->x = doorpos[dpick][0];
                        m->y = doorpos[dpick][1];
                        m->timer = randi(30, 60);
                        spawn_tear_pop(g, m->x, m->y, 1);
                    }
                }
            }

            if (e->phase == 0) {
                /* Idle: track the player slowly */
                if (!suppressAI) {
                    float mdx = p->x - e->x;
                    float mdy = p->y - e->y;
                    float mm = sqrtf(mdx * mdx + mdy * mdy);
                    if (mm > 1.0f) {
                        float mspd = 0.35f * fi->boss_speed_scale;
                        e->x += (mdx / mm) * mspd;
                        e->y += (mdy / mm) * mspd;
                    }
                }
                if (e->timer <= 0) {
                    /* Stomp: Monstro's big-jump interpolation to the player */
                    e->phase = 1;
                    e->timer = 45;
                    e->target_x = p->x;
                    e->target_y = p->y;
                    e->jump_vz = -4.5f;
                    e->jump_z = 0;
                    e->dx = (e->target_x - e->x) / 45.0f;
                    e->dy = (e->target_y - e->y) / 45.0f;
                }
            } else if (e->phase == 1) {
                /* Airborne stomp */
                e->x += e->dx;
                e->y += e->dy;
                e->jump_z += e->jump_vz;
                e->jump_vz += 0.22f;
                if (e->timer <= 0 || e->jump_z >= 0) {
                    e->jump_z = 0;
                    e->jump_vz = 0;
                    e->phase = 2;
                    e->timer = 20;
                    trigger_shake(g, 7.0f, 24);
                    boss_spread_8way(g, e->x, e->y, BOSS_SHOT_SPEED);
                    spawn_blood_splatter(g, e->x, e->y, 0, 0, 0);
                    boss_landing_thump(g, e->x, e->y);  /* R8 #44 */
                }
            } else {
                /* Landing recovery */
                e->dx *= 0.85f;
                e->dy *= 0.85f;
                if (e->timer <= 0) {
                    e->phase = 0;
                    e->timer = randi(60, 110);
                    e->dx = 0;
                    e->dy = 0;
                }
            }
            break;
        }

        case ENEMY_BOSS_MOMS_HEART:
        case ENEMY_BOSS_IT_LIVES: {
            /* Mom's Heart (E2): fixed Womb boss. Stationary tank on the
               Gurdy AI base + periodic enemy waves.
               R9 M2: IT LIVES reuses the exact skeleton with hotter
               timers, bigger waves, and radial shot rings below 25% HP. */
            int is_il = (e->type == ENEMY_BOSS_IT_LIVES);
            e->timer--;

            float hxDiff = p->x - e->x;
            if (fabsf(hxDiff) > 4.0f) {
                e->dx = (hxDiff > 0 ? 0.25f : -0.25f) * fi->boss_speed_scale;
            } else {
                e->dx *= 0.9f;
            }
            if (!suppressAI) e->x += e->dx;

            if (e->timer <= 0) {
                e->phase = (e->phase + 1) % 3;
                if (e->phase == 0) {
                    e->timer = is_il ? 45 : 55;
                } else if (e->phase == 1) {
                    /* Wave spawn: 2-3 minions (It Lives: 3-4), capped at 6 alive */
                    int alive_minions = 0;
                    for (int mi = 0; mi < r->enemy_count; mi++) {
                        if (r->enemies[mi].active &&
                            !is_boss_type(r->enemies[mi].type)) alive_minions++;
                    }
                    if (alive_minions < 6) {
                        int wave = 2 + randi(0, 1) + (is_il ? 1 : 0);
                        for (int s = 0; s < wave; s++) {
                            int kind = randi(0, 2);
                            if (kind == 0) boss_spawn_fly(g, r, e->x, e->y);
                            else if (kind == 1) boss_spawn_spider(r, e->x, e->y);
                            else boss_spawn_pooter(r, e->x, e->y);
                        }
                    }
                    e->timer = is_il ? 70 : 95;
                } else {
                    /* Shot spread + aimed shot */
                    boss_spread_8way(g, e->x, e->y, BOSS_SHOT_SPEED * 0.85f);
                    float hfdx = p->x - e->x, hfdy = p->y - e->y;
                    float hfm = sqrtf(hfdx * hfdx + hfdy * hfdy);
                    if (hfm > 1.0f) {
                        float spd = BOSS_SHOT_SPEED * 1.2f;
                        boss_shoot_tear(g, e->x, e->y,
                                        (hfdx / hfm) * spd, (hfdy / hfm) * spd);
                    }
                    e->timer = is_il ? 60 : 70;
                }
            }

            /* It Lives final-25% desperation: rotating radial shot rings
               on their own cooldown (famine_shoot_cd is unused by this AI
               base). Deterministic ring angles from attack_pattern. */
            if (is_il && e->hp < e->max_hp * 0.25f) {
                e->famine_shoot_cd--;
                if (e->famine_shoot_cd <= 0) {
                    e->famine_shoot_cd = 85;
                    e->attack_pattern++;
                    float roff = (float)e->attack_pattern * 0.31f;
                    for (int ri = 0; ri < 10; ri++) {
                        float ra = roff + ri * (2.0f * (float)M_PI / 10.0f);
                        boss_shoot_tear(g, e->x, e->y,
                                        cosf(ra) * BOSS_SHOT_SPEED * 0.9f,
                                        sinf(ra) * BOSS_SHOT_SPEED * 0.9f);
                    }
                    trigger_shake(g, 3.0f, 10);
                }
            }
            break;
        }

        case ENEMY_BOSS_SATAN: {
            /* Satan (E3): fixed Sheol boss, 3 HP-driven phases.
               p0 (hp > 2/3): summons Hoppers + aimed shots
               p1 (1/3..2/3): enemy Brimstone beams — 30f thin-line telegraph
                              then a 10f thick beam that damages the player
                              on line overlap (state on g->ebeam_*, NOT the
                              player's laser_* fields)
               p2 (< 1/3):    giant stomps (Mom's foot logic scaled up).
               Movement sub-state lives in e->attack_pattern so it can't
               fight the HP phase stored in e->phase. */
            e->timer--;

            {
                int hp_phase = (e->hp > e->max_hp * 0.66f) ? 0
                             : (e->hp > e->max_hp * 0.33f) ? 1 : 2;
                if (hp_phase != e->phase) {
                    e->phase = hp_phase;
                    e->attack_pattern = 0;
                    e->jump_z = 0;
                    e->jump_vz = 0;
                    e->timer = 50;
                    trigger_shake(g, 5.0f, 20);
                    boss_phase_juice(g, e);  /* R8 #28 */
                }
            }

            if (e->phase == 0) {
                /* Drift toward player + aimed shots + Hopper summons */
                if (!suppressAI) {
                    float sdx = p->x - e->x, sdy = p->y - e->y;
                    float sm = sqrtf(sdx * sdx + sdy * sdy);
                    if (sm > 80.0f) {
                        e->x += (sdx / sm) * 0.35f * fi->boss_speed_scale;
                        e->y += (sdy / sm) * 0.35f * fi->boss_speed_scale;
                    }
                }
                e->shoot_timer--;
                if (e->shoot_timer <= 0) {
                    e->shoot_timer = 55;
                    float sfdx = p->x - e->x, sfdy = p->y - e->y;
                    float sfm = sqrtf(sfdx * sfdx + sfdy * sfdy);
                    if (sfm > 1.0f) {
                        float spd = BOSS_SHOT_SPEED * 1.1f;
                        boss_shoot_tear(g, e->x, e->y,
                                        (sfdx / sfm) * spd, (sfdy / sfm) * spd);
                    }
                }
                if (e->timer <= 0) {
                    e->timer = 150;
                    if (count_enemies_of_type(r, ENEMY_HOPPER) < 2) {
                        for (int s = 0; s < 2; s++) {
                            /* B6: dead-slot reuse — long fights keep their
                               pressure (active/spawn_grace preset) */
                            Enemy *m = alloc_dynamic_enemy(r);
                            if (!m) break;
                            m->type = ENEMY_HOPPER;
                            /* T5: same difficulty/infinite HP scaling as
                               room spawns */
                            m->hp = m->max_hp = 4.0f * spawn_hp_mult(g);
                            m->x = e->x + randf(-30, 30);
                            m->y = e->y + randf(-20, 20);
                            m->timer = randi(20, 50);
                        }
                        trigger_shake(g, 3.0f, 10);
                    }
                }
            } else if (e->phase == 1) {
                /* Enemy Brimstone beam cycle */
                if (!suppressAI) {
                    e->dx *= 0.85f;
                    e->dy *= 0.85f;
                    e->x += e->dx;
                    e->y += e->dy;
                }
                if (g->ebeam_state == 0) {
                    if (e->timer <= 0) {
                        float bdx = p->x - e->x, bdy = p->y - e->y;
                        float bm = sqrtf(bdx * bdx + bdy * bdy);
                        if (bm > 1.0f) {
                            g->ebeam_x = e->x;
                            g->ebeam_y = e->y;
                            g->ebeam_dx = bdx / bm;
                            g->ebeam_dy = bdy / bm;
                            ebeam_clip_endpoint(g);
                            g->ebeam_state = 1;   /* telegraph */
                            g->ebeam_timer = 30;
                            audio_play(SFX_BOSS);
                        }
                        e->timer = 130;
                    }
                } else if (g->ebeam_state == 1) {
                    g->ebeam_timer--;
                    if (g->ebeam_timer <= 0) {
                        g->ebeam_state = 2;       /* fire */
                        g->ebeam_timer = 10;
                        trigger_shake(g, 5.0f, 12);
                    }
                } else {
                    /* Firing: damage the player on line overlap each frame */
                    g->ebeam_timer--;
                    if (p->iframes == 0) {
                        float bsx = g->ebeam_ex - g->ebeam_x;
                        float bsy = g->ebeam_ey - g->ebeam_y;
                        float bsl = bsx * bsx + bsy * bsy;
                        if (bsl > 1.0f) {
                            float bt = ((p->x - g->ebeam_x) * bsx +
                                        (p->y - g->ebeam_y) * bsy) / bsl;
                            if (bt < 0.0f) bt = 0.0f;
                            if (bt > 1.0f) bt = 1.0f;
                            float qx = g->ebeam_x + bsx * bt;
                            float qy = g->ebeam_y + bsy * bt;
                            float qdx = p->x - qx, qdy = p->y - qy;
                            float rr = PLAYER_SIZE + 6.0f;
                            if (qdx * qdx + qdy * qdy < rr * rr) {
                                p->hp -= player_absorb_dmg(p, 2);
                                p->iframes = PLAYER_IFRAMES;
                                g->hitstop = 3;
                                audio_play(SFX_HURT_GRUNT);
                                trigger_shake(g, 5.0f, 18);
                                if (player_check_death(p)) {
                                    audio_play(SFX_PLAYER_DEATH);
                                    g->state = STATE_GAMEOVER;
                                }
                            }
                        }
                    }
                    if (g->ebeam_timer <= 0) g->ebeam_state = 0;
                }
            } else {
                /* Giant stomps (Mom's foot logic scaled up) */
                if (g->ebeam_state) g->ebeam_state = 0;  /* drop stale beam */
                if (e->attack_pattern == 0) {
                    if (e->timer <= 0) {
                        e->attack_pattern = 1;
                        e->timer = 40;
                        e->target_x = p->x;
                        e->target_y = p->y;
                        e->jump_vz = -5.5f;
                        e->jump_z = 0;
                        e->dx = (e->target_x - e->x) / 40.0f;
                        e->dy = (e->target_y - e->y) / 40.0f;
                    }
                } else if (e->attack_pattern == 1) {
                    e->x += e->dx;
                    e->y += e->dy;
                    e->jump_z += e->jump_vz;
                    e->jump_vz += 0.28f;
                    if (e->timer <= 0 || e->jump_z >= 0) {
                        e->jump_z = 0;
                        e->jump_vz = 0;
                        e->attack_pattern = 2;
                        e->timer = 18;
                        trigger_shake(g, 8.0f, 28);
                        boss_spread_8way(g, e->x, e->y, BOSS_SHOT_SPEED * 1.1f);
                        boss_spread_4way(g, e->x, e->y, BOSS_SHOT_SPEED * 0.7f);
                        spawn_blood_splatter(g, e->x, e->y, 0, 0, 0);
                        boss_landing_thump(g, e->x, e->y);  /* R8 #44 */
                    }
                } else {
                    e->dx *= 0.85f;
                    e->dy *= 0.85f;
                    if (e->timer <= 0) {
                        e->attack_pattern = 0;
                        e->timer = randi(45, 80);
                        e->dx = 0;
                        e->dy = 0;
                    }
                }
            }
            break;
        }

        case ENEMY_BOSS_ISAAC: {
            /* Isaac (R9 C1): Cathedral fixed boss, holy theme, 3 HP-driven
               phases in e->phase. e->state: 1 = praying (floats, NO contact
               damage — see boss_praying), 0 = dodge-hop dash.
               p0 (> 2/3): tear-ring bursts (8-11 radial) + dodge hops
               p1 (1/3..2/3): light-beam barrage — g->vbeam_* columns with a
                              ~40f ground-marker telegraph, ~20f of damage
               p2 (< 1/3):    desperation — faster rings + 3-beam volleys +
                              2 angelic flies kept alive. */
            e->timer--;

            {
                int hp_phase = (e->hp > e->max_hp * 0.66f) ? 0
                             : (e->hp > e->max_hp * 0.33f) ? 1 : 2;
                if (hp_phase != e->phase) {
                    e->phase = hp_phase;
                    e->state = 1;        /* re-enter prayer on phase shift */
                    e->timer = 45;
                    e->shoot_timer = 60;
                    e->dx = 0; e->dy = 0;
                    trigger_shake(g, 5.0f, 20);
                    boss_phase_juice(g, e);
                }
            }

            /* Praying float: drift gently toward the altar spot up top */
            if (!suppressAI && e->state == 1) {
                float icx = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
                float icy = ROOM_TOP + 70.0f;
                e->x += (icx - e->x) * 0.012f;
                e->y += (icy - e->y) * 0.012f;
            }

            if (e->phase == 0) {
                /* P1: pray -> quick dodge-hop -> radial tear ring -> pray */
                if (e->timer <= 0) {
                    if (e->state == 1) {
                        /* Hop: short dash toward the player (contact ON) */
                        e->state = 0;
                        e->timer = 24;
                        float hdx = p->x - e->x, hdy = p->y - e->y;
                        float hm = sqrtf(hdx * hdx + hdy * hdy);
                        if (hm < 1.0f) hm = 1.0f;
                        float hspd = 2.2f * fi->boss_speed_scale;
                        e->dx = (hdx / hm) * hspd;
                        e->dy = (hdy / hm) * hspd;
                    } else {
                        /* Land: ring burst, back to prayer */
                        e->state = 1;
                        e->timer = randi(55, 85);
                        e->attack_pattern++;
                        int ring = 8 + (e->attack_pattern & 3);   /* 8-11 */
                        float roff = (float)e->attack_pattern * 0.39f;
                        for (int ri = 0; ri < ring; ri++) {
                            float ra = roff + ri * (2.0f * (float)M_PI / ring);
                            boss_shoot_tear(g, e->x, e->y,
                                            cosf(ra) * BOSS_SHOT_SPEED,
                                            sinf(ra) * BOSS_SHOT_SPEED);
                        }
                        trigger_shake(g, 3.0f, 10);
                        e->dx = 0; e->dy = 0;
                    }
                }
                if (e->state == 0 && !suppressAI) {
                    e->x += e->dx; e->y += e->dy;
                    e->dx *= 0.94f; e->dy *= 0.94f;
                }
            } else if (e->phase == 1) {
                /* P2: stays praying; single light column on the player */
                e->state = 1;
                if (g->vbeam_state == 0 && e->timer <= 0) {
                    g->vbeam_count = 1;
                    g->vbeam_x[0] = clampf(p->x, ROOM_LEFT + 12.0f,
                                           ROOM_RIGHT - 12.0f);
                    g->vbeam_state = 1;      /* ground-marker telegraph */
                    g->vbeam_timer = 40;
                    audio_play(SFX_BOSS);
                    e->timer = 110;
                }
                /* Occasional small ring so P2 isn't beam-only */
                e->shoot_timer--;
                if (e->shoot_timer <= 0) {
                    e->shoot_timer = 120;
                    e->attack_pattern++;
                    float roff = (float)e->attack_pattern * 0.52f;
                    for (int ri = 0; ri < 6; ri++) {
                        float ra = roff + ri * (2.0f * (float)M_PI / 6.0f);
                        boss_shoot_tear(g, e->x, e->y,
                                        cosf(ra) * BOSS_SHOT_SPEED * 0.9f,
                                        sinf(ra) * BOSS_SHOT_SPEED * 0.9f);
                    }
                }
            } else {
                /* P3 desperation: faster rings + 3-column volleys + flies */
                e->state = 1;
                e->shoot_timer--;
                if (e->shoot_timer <= 0) {
                    e->shoot_timer = 55;
                    e->attack_pattern++;
                    int ring = 10 + (e->attack_pattern & 1);
                    float roff = (float)e->attack_pattern * 0.47f;
                    for (int ri = 0; ri < ring; ri++) {
                        float ra = roff + ri * (2.0f * (float)M_PI / ring);
                        boss_shoot_tear(g, e->x, e->y,
                                        cosf(ra) * BOSS_SHOT_SPEED,
                                        sinf(ra) * BOSS_SHOT_SPEED);
                    }
                }
                if (g->vbeam_state == 0 && e->timer <= 0) {
                    float bxc = clampf(p->x, ROOM_LEFT + 72.0f,
                                       ROOM_RIGHT - 72.0f);
                    g->vbeam_count = 3;
                    g->vbeam_x[0] = bxc;
                    g->vbeam_x[1] = bxc - 60.0f;
                    g->vbeam_x[2] = bxc + 60.0f;
                    g->vbeam_state = 1;
                    g->vbeam_timer = 40;
                    audio_play(SFX_BOSS);
                    e->timer = 140;
                }
                /* Keep 2 angelic flies in the fight */
                if (count_alive_flies(r) < 2 && (g->frame % 150) == 0) {
                    boss_spawn_fly(g, r, e->x, e->y);
                    boss_spawn_fly(g, r, e->x, e->y);
                }
            }

            /* Light-column state machine + damage (driven here, mirroring
               how Satan's case owns the ebeam machine) */
            if (g->vbeam_state == 1) {
                g->vbeam_timer--;
                if (g->vbeam_timer <= 0) {
                    g->vbeam_state = 2;
                    g->vbeam_timer = 20;
                    trigger_shake(g, 4.0f, 12);
                    audio_play(SFX_BOSS);
                }
            } else if (g->vbeam_state == 2) {
                g->vbeam_timer--;
                if (p->iframes == 0) {
                    for (int vi = 0; vi < g->vbeam_count; vi++) {
                        if (fabsf(p->x - g->vbeam_x[vi]) <
                            PLAYER_SIZE + 9.0f) {
                            p->hp -= player_absorb_dmg(p, 2);
                            p->iframes = PLAYER_IFRAMES;
                            g->hitstop = 3;
                            audio_play(SFX_HURT_GRUNT);
                            trigger_shake(g, 5.0f, 18);
                            if (player_check_death(p)) {
                                audio_play(SFX_PLAYER_DEATH);
                                g->state = STATE_GAMEOVER;
                            }
                            break;
                        }
                    }
                }
                if (g->vbeam_timer <= 0) {
                    g->vbeam_state = 0;
                    g->vbeam_count = 0;
                }
            }
            break;
        }

        case ENEMY_BOSS_THE_LAMB: {
            /* The Lamb (R9 C1): Dark Room fixed boss, demonic mirror of
               Isaac. HP phases in e->phase:
               p0 (> 60%): slow chase + 4-way brimstone crosses (the shared
                           g->ebeam_* machine with ebeam_cross set)
               p1 (25-60%): detaches the Lamb Body (Gemini companion
                            fields: body chases with contact damage and is
                            killable via gemini_chp; head goes stationary
                            turret with rotating radial bursts)
               p2 (< 25%):  enrage — every timer runs at ~60%. */
            e->timer--;

            {
                int lphase = (e->hp > e->max_hp * 0.60f) ? 0
                           : (e->hp > e->max_hp * 0.25f) ? 1 : 2;
                if (lphase != e->phase) {
                    e->phase = lphase;
                    e->timer = 45;
                    trigger_shake(g, 5.0f, 20);
                    boss_phase_juice(g, e);
                    if (lphase >= 1 && !e->gemini_split) {
                        /* Detach the Lamb Body: killable chaser */
                        e->gemini_split = 1;
                        e->gemini_chp = (int)(e->max_hp * 0.25f);
                        if (e->gemini_chp < 8) e->gemini_chp = 8;
                        e->gemini_cx = e->x;
                        e->gemini_cy = e->y + 22;
                        audio_play(SFX_BOSS);
                    }
                }
            }

            float enr = (e->phase == 2) ? 0.6f : 1.0f;   /* enrage scale */

            if (!e->gemini_split) {
                /* P1: slow chase while whole */
                if (!suppressAI) {
                    float ldx = p->x - e->x, ldy = p->y - e->y;
                    float lm = sqrtf(ldx * ldx + ldy * ldy);
                    if (lm > 60.0f) {
                        float lspd = 0.5f * fi->boss_speed_scale;
                        e->x += (ldx / lm) * lspd;
                        e->y += (ldy / lm) * lspd;
                    }
                }
                if (g->ebeam_state == 0 && e->timer <= 0) {
                    g->ebeam_x = e->x;
                    g->ebeam_y = e->y;
                    g->ebeam_dx = 1.0f; g->ebeam_dy = 0.0f; /* unused (cross) */
                    ebeam_clip_endpoint(g);
                    g->ebeam_cross = 1;
                    g->ebeam_state = 1;    /* telegraph */
                    g->ebeam_timer = 35;
                    audio_play(SFX_BOSS);
                    e->timer = (int)(150 * enr);
                }
            } else {
                /* Head: stationary turret with rotating bursts */
                e->dx *= 0.85f;
                e->dy *= 0.85f;
                e->shoot_timer--;
                if (e->shoot_timer <= 0) {
                    e->shoot_timer = (int)(80 * enr);
                    e->attack_pattern++;
                    float roff = (float)e->attack_pattern * 0.39f;
                    for (int ri = 0; ri < 8; ri++) {
                        float ra = roff + ri * (2.0f * (float)M_PI / 8.0f);
                        boss_shoot_tear(g, e->x, e->y,
                                        cosf(ra) * BOSS_SHOT_SPEED,
                                        sinf(ra) * BOSS_SHOT_SPEED);
                    }
                    /* plus one aimed shot */
                    float afdx = p->x - e->x, afdy = p->y - e->y;
                    float afm = sqrtf(afdx * afdx + afdy * afdy);
                    if (afm > 1.0f) {
                        float aspd = BOSS_SHOT_SPEED * 1.2f;
                        boss_shoot_tear(g, e->x, e->y,
                                        (afdx / afm) * aspd,
                                        (afdy / afm) * aspd);
                    }
                }
                /* Crosses keep coming, slower than P1's cadence */
                if (g->ebeam_state == 0 && e->timer <= 0) {
                    g->ebeam_x = e->x;
                    g->ebeam_y = e->y;
                    g->ebeam_dx = 1.0f; g->ebeam_dy = 0.0f;
                    ebeam_clip_endpoint(g);
                    g->ebeam_cross = 1;
                    g->ebeam_state = 1;
                    g->ebeam_timer = 35;
                    audio_play(SFX_BOSS);
                    e->timer = (int)(190 * enr);
                }
                /* Lamb Body: chases the player (killable, contact dmg) */
                if (e->gemini_chp > 0 && !suppressAI) {
                    float bdx2 = p->x - e->gemini_cx;
                    float bdy2 = p->y - e->gemini_cy;
                    float bm2 = sqrtf(bdx2 * bdx2 + bdy2 * bdy2);
                    if (bm2 > 1.0f) {
                        float bspd = 1.3f * fi->boss_speed_scale *
                                     (e->phase == 2 ? 1.35f : 1.0f);
                        e->gemini_cx += (bdx2 / bm2) * bspd;
                        e->gemini_cy += (bdy2 / bm2) * bspd;
                    }
                    e->gemini_cx = clampf(e->gemini_cx,
                                          ROOM_LEFT + ENEMY_SIZE,
                                          ROOM_RIGHT - ENEMY_SIZE);
                    e->gemini_cy = clampf(e->gemini_cy,
                                          ROOM_TOP + ENEMY_SIZE,
                                          ROOM_BOTTOM - ENEMY_SIZE);
                }
            }

            /* Cross-beam state machine + damage (this case owns the ebeam
               while The Lamb is alive; Satan never runs in this room) */
            if (g->ebeam_state == 1) {
                g->ebeam_timer--;
                if (g->ebeam_timer <= 0) {
                    g->ebeam_state = 2;
                    g->ebeam_timer = 12;
                    trigger_shake(g, 5.0f, 12);
                }
            } else if (g->ebeam_state == 2) {
                g->ebeam_timer--;
                if (p->iframes == 0) {
                    float rr = PLAYER_SIZE + 5.0f;
                    if (fabsf(p->y - g->ebeam_y) < rr ||
                        fabsf(p->x - g->ebeam_x) < rr) {
                        p->hp -= player_absorb_dmg(p, 2);
                        p->iframes = PLAYER_IFRAMES;
                        g->hitstop = 3;
                        audio_play(SFX_HURT_GRUNT);
                        trigger_shake(g, 5.0f, 18);
                        if (player_check_death(p)) {
                            audio_play(SFX_PLAYER_DEATH);
                            g->state = STATE_GAMEOVER;
                        }
                    }
                }
                if (g->ebeam_timer <= 0) {
                    g->ebeam_state = 0;
                    g->ebeam_cross = 0;
                }
            }

            /* Lamb Body contact damage (mirrors Gemini's companion hit) */
            if (e->gemini_split && e->gemini_chp > 0 && p->iframes == 0) {
                float cdx2 = p->x - e->gemini_cx;
                float cdy2 = p->y - e->gemini_cy;
                float cds2 = cdx2 * cdx2 + cdy2 * cdy2;
                float crad2 = PLAYER_SIZE + ENEMY_SIZE;
                if (cds2 < crad2 * crad2) {
                    p->hp -= player_absorb_dmg(p, 2);
                    p->iframes = PLAYER_IFRAMES;
                    g->hitstop = 3;
                    float cm2 = sqrtf(cds2);
                    if (cm2 > 0.1f) {
                        p->vx = (cdx2 / cm2) * PLAYER_KB_FORCE;
                        p->vy = (cdy2 / cm2) * PLAYER_KB_FORCE;
                    }
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

        case ENEMY_BOSS_URIEL:
        case ENEMY_BOSS_GABRIEL: {
            /* R8 (M3): angel minibosses, shared AI with two tuning sets.
               Uriel: slow hover-chase, 3-shot volleys, single light column.
               Gabriel: faster hover, 4-5 shot volleys, tighter cooldowns,
               twin columns. The g->vbeam_* machine is Game-level SHARED
               state: every use is gated on vbeam_state == 0, and this case
               drives the telegraph->fire->damage machine exactly like
               Isaac's (an angel and Isaac can never share a room — angels
               only wake in angel rooms — but the guard stands anyway). */
            int gab = (e->type == ENEMY_BOSS_GABRIEL);
            e->timer--;

            /* Hover-chase: approach to ~60px, deterministic bob */
            if (!suppressAI) {
                float adx = p->x - e->x, ady = p->y - e->y;
                float am = sqrtf(adx * adx + ady * ady);
                if (am > 1.0f) {
                    float aspd = (gab ? 0.85f : 0.55f) * fi->boss_speed_scale;
                    if (am < 60.0f) aspd = -aspd * 0.6f;  /* keep distance */
                    e->x += (adx / am) * aspd;
                    e->y += (ady / am) * aspd;
                }
                e->y += sinf((float)g->frame * 0.07f) * 0.35f;
            }

            /* Light-shot volleys: 3 (Uriel) / 4-5 (Gabriel) aimed shots
               fanned around the player direction */
            e->shoot_timer--;
            if (e->shoot_timer <= 0) {
                e->shoot_timer = gab ? randi(50, 70) : randi(75, 100);
                e->attack_pattern++;
                int vol = gab ? (4 + (e->attack_pattern & 1)) : 3;
                float vdx = p->x - e->x, vdy = p->y - e->y;
                float vm = sqrtf(vdx * vdx + vdy * vdy);
                if (vm > 1.0f) {
                    float vbase = atan2f(vdy, vdx);
                    float spread = gab ? 0.5f : 0.35f;
                    for (int vi = 0; vi < vol; vi++) {
                        float fr = (vol > 1)
                                 ? ((float)vi / (float)(vol - 1) - 0.5f)
                                 : 0.0f;
                        float va = vbase + fr * spread;
                        boss_shoot_tear(g, e->x, e->y,
                                        cosf(va) * BOSS_SHOT_SPEED,
                                        sinf(va) * BOSS_SHOT_SPEED);
                    }
                }
            }

            /* Occasional light column on the player — ONLY if the shared
               machine is idle. Gabriel drops a second offset column. */
            if (g->vbeam_state == 0 && e->timer <= 0) {
                float bxc = clampf(p->x,
                                   ROOM_LEFT + (gab ? 72.0f : 12.0f),
                                   ROOM_RIGHT - (gab ? 72.0f : 12.0f));
                g->vbeam_count = gab ? 2 : 1;
                g->vbeam_x[0] = bxc;
                if (gab)
                    g->vbeam_x[1] = bxc +
                        ((e->attack_pattern & 1) ? -55.0f : 55.0f);
                g->vbeam_state = 1;      /* ground-marker telegraph */
                g->vbeam_timer = 40;
                audio_play(SFX_BOSS);
                e->timer = gab ? 130 : 170;
            }

            /* Column state machine + damage (mirrors Isaac's case; only
               one angel is ever alive so this ticks once per frame) */
            if (g->vbeam_state == 1) {
                g->vbeam_timer--;
                if (g->vbeam_timer <= 0) {
                    g->vbeam_state = 2;
                    g->vbeam_timer = 20;
                    trigger_shake(g, 4.0f, 12);
                    audio_play(SFX_BOSS);
                }
            } else if (g->vbeam_state == 2) {
                g->vbeam_timer--;
                if (p->iframes == 0) {
                    for (int vi = 0; vi < g->vbeam_count; vi++) {
                        if (fabsf(p->x - g->vbeam_x[vi]) <
                            PLAYER_SIZE + 9.0f) {
                            p->hp -= player_absorb_dmg(p, 2);
                            p->iframes = PLAYER_IFRAMES;
                            g->hitstop = 3;
                            audio_play(SFX_HURT_GRUNT);
                            trigger_shake(g, 5.0f, 18);
                            if (player_check_death(p)) {
                                audio_play(SFX_PLAYER_DEATH);
                                g->state = STATE_GAMEOVER;
                            }
                            break;
                        }
                    }
                }
                if (g->vbeam_timer <= 0) {
                    g->vbeam_state = 0;
                    g->vbeam_count = 0;
                }
            }
            break;
        }

        case ENEMY_BOSS_KRAMPUS: {
            /* R8 (M7): devil-room ambush. Chase + 4-way brimstone cross
               (the SHARED g->ebeam_* machine with ebeam_cross set — every
               use gated on ebeam_state == 0; Satan / The Lamb can never
               share a devil room with him) + slow coal-lob volleys. */
            e->timer--;

            if (!suppressAI) {
                float kdx = p->x - e->x, kdy = p->y - e->y;
                float km = sqrtf(kdx * kdx + kdy * kdy);
                if (km > 40.0f) {
                    float kspd = 0.7f * fi->boss_speed_scale;
                    e->x += (kdx / km) * kspd;
                    e->y += (kdy / km) * kspd;
                }
            }

            /* Coal lobs: 2-3 slow heavy shots fanned at the player */
            e->shoot_timer--;
            if (e->shoot_timer <= 0) {
                e->shoot_timer = randi(70, 100);
                e->attack_pattern++;
                int lobs = 2 + (e->attack_pattern & 1);
                float ldx = p->x - e->x, ldy = p->y - e->y;
                float lm = sqrtf(ldx * ldx + ldy * ldy);
                if (lm > 1.0f) {
                    float lbase = atan2f(ldy, ldx);
                    for (int li = 0; li < lobs; li++) {
                        float fr = (lobs > 1)
                                 ? ((float)li / (float)(lobs - 1) - 0.5f)
                                 : 0.0f;
                        float la = lbase + fr * 0.6f;
                        boss_shoot_tear(g, e->x, e->y,
                                        cosf(la) * BOSS_SHOT_SPEED * 0.7f,
                                        sinf(la) * BOSS_SHOT_SPEED * 0.7f);
                    }
                }
            }

            /* 4-way brimstone cross when the shared machine is idle */
            if (g->ebeam_state == 0 && e->timer <= 0) {
                g->ebeam_x = e->x;
                g->ebeam_y = e->y;
                g->ebeam_dx = 1.0f; g->ebeam_dy = 0.0f; /* unused (cross) */
                ebeam_clip_endpoint(g);
                g->ebeam_cross = 1;
                g->ebeam_state = 1;      /* telegraph */
                g->ebeam_timer = 35;
                audio_play(SFX_BOSS);
                e->timer = 160;
            }

            /* Cross state machine + damage (this case owns the ebeam
               while Krampus is alive — same pattern as The Lamb) */
            if (g->ebeam_state == 1) {
                g->ebeam_timer--;
                if (g->ebeam_timer <= 0) {
                    g->ebeam_state = 2;
                    g->ebeam_timer = 12;
                    trigger_shake(g, 5.0f, 12);
                }
            } else if (g->ebeam_state == 2) {
                g->ebeam_timer--;
                if (p->iframes == 0) {
                    float krr = PLAYER_SIZE + 5.0f;
                    if (fabsf(p->y - g->ebeam_y) < krr ||
                        fabsf(p->x - g->ebeam_x) < krr) {
                        p->hp -= player_absorb_dmg(p, 2);
                        p->iframes = PLAYER_IFRAMES;
                        g->hitstop = 3;
                        audio_play(SFX_HURT_GRUNT);
                        trigger_shake(g, 5.0f, 18);
                        if (player_check_death(p)) {
                            audio_play(SFX_PLAYER_DEATH);
                            g->state = STATE_GAMEOVER;
                        }
                    }
                }
                if (g->ebeam_timer <= 0) {
                    g->ebeam_state = 0;
                    g->ebeam_cross = 0;
                }
            }
            break;
        }

        case ENEMY_BOSS_BLUE_BABY: {
            /* R8 (M3): ??? (Blue Baby) — Isaac's hop/ring skeleton, dark
               mirror: NO light columns (never touches g->vbeam_*), denser
               rings, aimed volleys below 2/3 HP, quickening cadence.
               Contact damage stays ON (no prayer float — boss_praying is
               Isaac-only). */
            e->timer--;

            {
                int bp = (e->hp > e->max_hp * 0.66f) ? 0
                       : (e->hp > e->max_hp * 0.33f) ? 1 : 2;
                if (bp != e->phase) {
                    e->phase = bp;
                    e->timer = 40;
                    trigger_shake(g, 5.0f, 20);
                    boss_phase_juice(g, e);
                }
            }

            if (e->timer <= 0) {
                if (e->state == 1) {
                    /* Hop: short dash toward the player */
                    e->state = 0;
                    e->timer = 22;
                    float hdx = p->x - e->x, hdy = p->y - e->y;
                    float hm = sqrtf(hdx * hdx + hdy * hdy);
                    if (hm < 1.0f) hm = 1.0f;
                    float hspd = (2.2f + 0.4f * (float)e->phase) *
                                 fi->boss_speed_scale;
                    e->dx = (hdx / hm) * hspd;
                    e->dy = (hdy / hm) * hspd;
                } else {
                    /* Land: dense radial ring (10-14), back to hover */
                    e->state = 1;
                    e->timer = randi(45, 70) - e->phase * 8;
                    e->attack_pattern++;
                    int ring = 10 + (e->attack_pattern % 3) + e->phase;
                    float roff = (float)e->attack_pattern * 0.43f;
                    for (int ri = 0; ri < ring; ri++) {
                        float ra = roff + ri * (2.0f * (float)M_PI / ring);
                        boss_shoot_tear(g, e->x, e->y,
                                        cosf(ra) * BOSS_SHOT_SPEED,
                                        sinf(ra) * BOSS_SHOT_SPEED);
                    }
                    trigger_shake(g, 3.0f, 10);
                    e->dx = 0; e->dy = 0;
                }
            }
            if (e->state == 0 && !suppressAI) {
                e->x += e->dx; e->y += e->dy;
                e->dx *= 0.94f; e->dy *= 0.94f;
            }

            /* Below 2/3 HP: extra aimed 3-volley on its own cooldown */
            if (e->phase >= 1) {
                e->shoot_timer--;
                if (e->shoot_timer <= 0) {
                    e->shoot_timer = (e->phase == 2) ? 55 : 75;
                    float bdx = p->x - e->x, bdy = p->y - e->y;
                    float bm = sqrtf(bdx * bdx + bdy * bdy);
                    if (bm > 1.0f) {
                        float bbase = atan2f(bdy, bdx);
                        for (int vi = 0; vi < 3; vi++) {
                            float va = bbase + (float)(vi - 1) * 0.28f;
                            boss_shoot_tear(g, e->x, e->y,
                                            cosf(va) * BOSS_SHOT_SPEED * 1.1f,
                                            sinf(va) * BOSS_SHOT_SPEED * 1.1f);
                        }
                    }
                }
            }

            /* Desperation: keep 2 flies in the fight */
            if (e->phase == 2 && count_alive_flies(r) < 2 &&
                (g->frame % 160) == 0) {
                boss_spawn_fly(g, r, e->x, e->y);
                boss_spawn_fly(g, r, e->x, e->y);
            }
            break;
        }

        case ENEMY_BOSS_GISH: {
            /* Gish: Monstro-style jump + shoot, but leaves damaging creep
             * where it lands. Same 3-phase loop as Monstro. */
            e->timer--;

            if (e->phase == 0) {
                /* Idle - hop toward player slowly, occasional shotgun */
                if (!suppressAI) {
                    float mdx = p->x - e->x;
                    float mdy = p->y - e->y;
                    float mm = sqrtf(mdx * mdx + mdy * mdy);
                    if (mm > 1.0f) {
                        float hop = fabsf(sinf(e->anim_timer * 0.08f)) * 1.2f;
                        float mspd = (0.4f + hop * 0.2f) * fi->boss_speed_scale;
                        e->x += (mdx / mm) * mspd;
                        e->y += (mdy / mm) * mspd;
                    }
                }

                e->shoot_timer--;
                if (e->shoot_timer <= 0) {
                    e->shoot_timer = randi(90, 140);
                    monstro_shotgun(g, e->x, e->y);
                    trigger_shake(g, 2.0f, 8);
                }

                if (e->timer <= 0) {
                    e->attack_pattern = (e->attack_pattern + 1) % 3;
                    if (e->attack_pattern == 2) {
                        /* Big jump to player position */
                        e->phase = 1;
                        e->timer = 45;
                        e->target_x = p->x;
                        e->target_y = p->y;
                        e->jump_vz = -4.0f;
                        e->jump_z = 0;
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
                /* Airborne */
                e->x += e->dx;
                e->y += e->dy;
                e->jump_z += e->jump_vz;
                e->jump_vz += 0.2f;

                if (e->timer <= 0 || e->jump_z >= 0) {
                    /* Landing! Tar creep + tear spread */
                    e->jump_z = 0;
                    e->jump_vz = 0;
                    e->phase = 2;
                    e->timer = 15;
                    trigger_shake(g, 5.0f, 20);
                    monstro_tear_spread(g, e->x, e->y);
                    spawn_blood_splatter(g, e->x, e->y, 0, 0, 0);
                    boss_landing_thump(g, e->x, e->y);  /* R8 #44 */
                    /* Gish signature: leave a pool of green creep on landing */
                    if (!suppressAI) {
                        spawn_creep(g, e->x, e->y, 1, 240);
                        spawn_creep(g, e->x + 16, e->y, 1, 220);
                        spawn_creep(g, e->x - 16, e->y, 1, 220);
                    }
                }
            } else {
                /* Landing recovery */
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

        case ENEMY_BOSS_LOKI: {
            /* Loki: stationary cross-shaped boss that blinks to a new spot and
             * fires a 4-way spread, then an 8-way spread on the next volley. */
            e->timer--;

            /* Blink: teleport to a fresh position on a timer. */
            if (e->timer <= 0) {
                if (!suppressAI) {
                    /* Pick a new spot away from the player and from edges */
                    float nx, ny;
                    int safety = 0;
                    do {
                        nx = randf(ROOM_LEFT + 60, ROOM_RIGHT - 60);
                        ny = randf(ROOM_TOP + 60, ROOM_BOTTOM - 60);
                        safety++;
                    } while (fabsf(nx - p->x) < 60 && fabsf(ny - p->y) < 60 && safety < 20);
                    e->x = nx;
                    e->y = ny;
                    trigger_shake(g, 3.0f, 10);
                    spawn_tear_pop(g, e->x, e->y, 0);
                }
                e->timer = randi(90, 150);
            }

            /* Fire alternating spreads on the shoot timer. */
            e->shoot_timer--;
            if (e->shoot_timer <= 0) {
                if (!suppressAI) {
                    if (e->attack_pattern == 0) {
                        boss_spread_4way(g, e->x, e->y, BOSS_SHOT_SPEED * 0.9f);
                    } else {
                        boss_spread_8way(g, e->x, e->y, BOSS_SHOT_SPEED * 0.8f);
                    }
                    e->attack_pattern ^= 1;  /* toggle 4-way / 8-way */
                }
                e->shoot_timer = randi(45, 70);
            }
            break;
        }

        case ENEMY_BOSS_STEVEN: {
            /* Steven: twin-head Gemini-lite. Main head chases the player;
             * the second head wobbles on a short tether and, once Steven
             * drops below half HP, starts making short aggressive lunges
             * (like Gemini's companion, but never fully detaches). */
            float speed_main = 1.0f * fi->boss_speed_scale;
            /* R8 #19: dead second head = mild enrage for the survivor */
            if (e->gemini_chp <= 0) speed_main *= 1.2f;

            if (!e->gemini_split && e->hp < e->max_hp / 2) {
                e->gemini_split = 1;  /* second head becomes aggressive */
                trigger_shake(g, 3.0f, 12);
                boss_phase_juice(g, e);  /* R8 #28 */
            }

            float gdx = p->x - e->x;
            float gdy = p->y - e->y;
            float gm = sqrtf(gdx * gdx + gdy * gdy);
            if (gm > 1.0f && !suppressAI) {
                e->x += (gdx / gm) * speed_main;
                e->y += (gdy / gm) * speed_main;
            }

            if (e->gemini_chp <= 0) {
                /* R8 #19: head dead — skip all second-head behavior */
            } else if (!e->gemini_split) {
                /* Second head trails on a short tether, gentle wobble */
                float tdx = e->x - e->gemini_cx;
                float tdy = e->y - e->gemini_cy;
                float td = sqrtf(tdx * tdx + tdy * tdy);
                if (td > GEMINI_TETHER_DIST * 0.6f) {
                    e->gemini_cx = e->x - (tdx / td) * (GEMINI_TETHER_DIST * 0.6f);
                    e->gemini_cy = e->y - (tdy / td) * (GEMINI_TETHER_DIST * 0.6f);
                }
                if (td > 8.0f) {
                    e->gemini_cx += (tdx / td) * 0.7f;
                    e->gemini_cy += (tdy / td) * 0.7f;
                }
                e->gemini_cx += sinf(g->frame * 0.08f) * 0.5f;
                e->gemini_cy += cosf(g->frame * 0.1f) * 0.5f;
            } else {
                /* Aggressive: short lunges toward the player */
                e->timer--;
                if (e->state == 0) {
                    e->gemini_cdx *= 0.9f;
                    e->gemini_cdy *= 0.9f;
                    e->gemini_cx += e->gemini_cdx;
                    e->gemini_cy += e->gemini_cdy;
                    if (e->timer <= 0) {
                        e->state = 1;
                        e->timer = 20;
                        float cdx = p->x - e->gemini_cx;
                        float cdy = p->y - e->gemini_cy;
                        float cm = sqrtf(cdx * cdx + cdy * cdy);
                        if (cm > 1.0f) {
                            float cchg = 3.0f * fi->boss_speed_scale;
                            e->gemini_cdx = (cdx / cm) * cchg;
                            e->gemini_cdy = (cdy / cm) * cchg;
                        }
                    }
                } else {
                    e->gemini_cx += e->gemini_cdx;
                    e->gemini_cy += e->gemini_cdy;
                    if (e->timer <= 0) {
                        e->state = 0;
                        e->timer = randi(25, 55);
                    }
                }
                e->gemini_cx = clampf(e->gemini_cx, ROOM_LEFT + ENEMY_SIZE,
                                      ROOM_RIGHT - ENEMY_SIZE);
                e->gemini_cy = clampf(e->gemini_cy, ROOM_TOP + ENEMY_SIZE,
                                      ROOM_BOTTOM - ENEMY_SIZE);
            }

            /* Second head collision with player (R8 #19: dead = harmless) */
            if (p->iframes == 0 && e->gemini_chp > 0) {
                float cdx = p->x - e->gemini_cx;
                float cdy = p->y - e->gemini_cy;
                float cdsq = cdx * cdx + cdy * cdy;
                float crad = PLAYER_SIZE + ENEMY_SIZE;
                if (cdsq < crad * crad) {
                    p->hp -= player_absorb_dmg(p, 2);
                    p->iframes = PLAYER_IFRAMES;
                    g->hitstop = 3;
                    float cm = sqrtf(cdsq);
                    if (cm > 0.1f) {
                        p->vx = (cdx / cm) * PLAYER_KB_FORCE;
                        p->vy = (cdy / cm) * PLAYER_KB_FORCE;
                    }
                    audio_play(SFX_HURT_GRUNT);
                    trigger_shake(g, 3.0f, 10);
                    if (player_check_death(p)) {
                        audio_play(SFX_PLAYER_DEATH);
                        g->state = STATE_GAMEOVER;
                    }
                }
            }
            break;
        }

        case ENEMY_BOSS_CHUB: {
            /* Chub: segmented like Larry Jr, but fatter and slower/tankier.
             * Reuses the same segment-follow logic with reduced speed. */
            float hpRatio = (float)e->hp / (float)e->max_hp;
            /* R8 #23: keep the floor's boss speed scale in the recompute */
            e->seg_speed = (1.3f + (1.0f - hpRatio) * 1.2f) * fi->boss_speed_scale;

            e->timer--;
            if (e->timer <= 0) {
                e->timer = randi(25, 70);
                float ldx = p->x - e->x;
                float ldy = p->y - e->y;
                float lm = sqrtf(ldx * ldx + ldy * ldy);
                if (lm > 1.0f) {
                    float turn = 0.5f + hpRatio * 0.3f;
                    e->dx = e->dx * (1.0f - turn) + (ldx / lm) * e->seg_speed * turn;
                    e->dy = e->dy * (1.0f - turn) + (ldy / lm) * e->seg_speed * turn;
                }
                float weave = sinf(g->frame * 0.05f) * 0.6f;
                float perp_x = -e->dy, perp_y = e->dx;
                float pm = sqrtf(perp_x * perp_x + perp_y * perp_y);
                if (pm > 0.1f) {
                    e->dx += (perp_x / pm) * weave;
                    e->dy += (perp_y / pm) * weave;
                }
            }

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

            if (e->x <= ROOM_LEFT + ENEMY_SIZE * 2) { e->dx = fabsf(e->dx); e->x = ROOM_LEFT + ENEMY_SIZE * 2; }
            if (e->x >= ROOM_RIGHT - ENEMY_SIZE * 2) { e->dx = -fabsf(e->dx); e->x = ROOM_RIGHT - ENEMY_SIZE * 2; }
            if (e->y <= ROOM_TOP + ENEMY_SIZE * 2) { e->dy = fabsf(e->dy); e->y = ROOM_TOP + ENEMY_SIZE * 2; }
            if (e->y >= ROOM_BOTTOM - ENEMY_SIZE * 2) { e->dy = -fabsf(e->dy); e->y = ROOM_BOTTOM - ENEMY_SIZE * 2; }

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
                    seg->x = ahead_x - (sdx / sd) * LARRY_SEG_DIST;
                    seg->y = ahead_y - (sdy / sd) * LARRY_SEG_DIST;
                }
            }

            if (p->iframes == 0) {
                for (int s = 0; s < e->seg_count; s++) {
                    LarrySegment *seg = &e->segments[s];
                    float sdx = p->x - seg->x;
                    float sdy = p->y - seg->y;
                    float sr = PLAYER_SIZE + ENEMY_SIZE; /* fatter body, slightly bigger hitbox than Larry */
                    if (sdx * sdx + sdy * sdy < sr * sr) {
                        p->hp -= player_absorb_dmg(p, 2);
                        p->iframes = PLAYER_IFRAMES;
                        g->hitstop = 3;
                        float sm = sqrtf(sdx * sdx + sdy * sdy);
                        if (sm > 0.1f) {
                            p->vx = (sdx / sm) * PLAYER_KB_FORCE;
                            p->vy = (sdy / sm) * PLAYER_KB_FORCE;
                        }
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

        case ENEMY_BOSS_FISTULA: {
            /* Fistula: slow drifting boss with no ranged attack. Its threat
             * is the split-on-hit mechanic (handled in the damage paths),
             * flooding the room with small balls as it's whittled down. */
            e->timer--;
            if (e->timer <= 0) {
                e->timer = randi(40, 80);
                float fdx = p->x - e->x;
                float fdy = p->y - e->y;
                float fm = sqrtf(fdx * fdx + fdy * fdy);
                float spd = 0.6f * fi->boss_speed_scale;
                if (fm > 1.0f) {
                    e->dx = (fdx / fm) * spd + randf(-0.3f, 0.3f);
                    e->dy = (fdy / fm) * spd + randf(-0.3f, 0.3f);
                }
            }
            if (!suppressAI) {
                e->x += e->dx;
                e->y += e->dy;
            }
            if (e->x <= ROOM_LEFT + ENEMY_SIZE * 2) { e->dx = fabsf(e->dx); }
            if (e->x >= ROOM_RIGHT - ENEMY_SIZE * 2) { e->dx = -fabsf(e->dx); }
            if (e->y <= ROOM_TOP + ENEMY_SIZE * 2) { e->dy = fabsf(e->dy); }
            if (e->y >= ROOM_BOTTOM - ENEMY_SIZE * 2) { e->dy = -fabsf(e->dy); }
            break;
        }

        case ENEMY_BOSS_SCOLEX: {
            /* Scolex: segmented worm (Chub-style body) that alternates
             * between burrowed (phase 1: fast, safe repositioning toward
             * the player) and emerged (phase 0: slower, vulnerable, body
             * segments collide with the player like Larry/Chub). */
            e->timer--;

            if (e->phase == 1) {
                /* Burrowed: fast tunnel toward player */
                float bdx = p->x - e->x;
                float bdy = p->y - e->y;
                float bm = sqrtf(bdx * bdx + bdy * bdy);
                if (bm > 1.0f) {
                    float spd = 2.2f * fi->boss_speed_scale;
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
                    e->hidden = 0;
                    e->timer = 100;      /* time vulnerable/emerged */
                    trigger_shake(g, 3.0f, 10);
                }
            } else {
                /* Emerged: slower segmented chase, vulnerable body collision */
                float hpRatio = (float)e->hp / (float)e->max_hp;
                /* R8 #23: keep the floor's boss speed scale in the recompute
                   (the burrowed branch above already scales) */
                e->seg_speed = (1.4f + (1.0f - hpRatio) * 1.0f) * fi->boss_speed_scale;
                float ldx = p->x - e->x;
                float ldy = p->y - e->y;
                float lm = sqrtf(ldx * ldx + ldy * ldy);
                if (lm > 1.0f) {
                    e->dx = (ldx / lm) * e->seg_speed;
                    e->dy = (ldy / lm) * e->seg_speed;
                }
                if (!suppressAI) {
                    e->x += e->dx;
                    e->y += e->dy;
                }
                if (e->timer <= 0) {
                    /* Burrow again */
                    e->phase = 1;
                    e->hidden = 1;
                    e->timer = randi(70, 110);
                }
            }

            if (e->x <= ROOM_LEFT + ENEMY_SIZE * 2) { e->dx = fabsf(e->dx); e->x = ROOM_LEFT + ENEMY_SIZE * 2; }
            if (e->x >= ROOM_RIGHT - ENEMY_SIZE * 2) { e->dx = -fabsf(e->dx); e->x = ROOM_RIGHT - ENEMY_SIZE * 2; }
            if (e->y <= ROOM_TOP + ENEMY_SIZE * 2) { e->dy = fabsf(e->dy); e->y = ROOM_TOP + ENEMY_SIZE * 2; }
            if (e->y >= ROOM_BOTTOM - ENEMY_SIZE * 2) { e->dy = -fabsf(e->dy); e->y = ROOM_BOTTOM - ENEMY_SIZE * 2; }

            /* Update body segments - follow the head like Larry/Chub */
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
                    seg->x = ahead_x - (sdx / sd) * LARRY_SEG_DIST;
                    seg->y = ahead_y - (sdy / sd) * LARRY_SEG_DIST;
                }
            }

            /* Segment collision with player -- only while emerged (phase 0);
             * burrowed segments trail underground and shouldn't hurt the
             * player, matching Pin's burrowed-is-safe feel. */
            if (e->phase == 0 && p->iframes == 0) {
                for (int s = 0; s < e->seg_count; s++) {
                    LarrySegment *seg = &e->segments[s];
                    float sdx = p->x - seg->x;
                    float sdy = p->y - seg->y;
                    float sr = PLAYER_SIZE + ENEMY_SIZE * 0.8f;
                    if (sdx * sdx + sdy * sdy < sr * sr) {
                        p->hp -= player_absorb_dmg(p, 2);
                        p->iframes = PLAYER_IFRAMES;
                        g->hitstop = 3;
                        float sm = sqrtf(sdx * sdx + sdy * sdy);
                        if (sm > 0.1f) {
                            p->vx = (sdx / sm) * PLAYER_KB_FORCE;
                            p->vy = (sdy / sm) * PLAYER_KB_FORCE;
                        }
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
                /* Only fire with a valid aim vector; fm can be 0 when the eye
                   sits exactly on the player (else fdx/fm -> NaN shot). */
                if (fm > 0.5f) {
                    float spd = BOSS_SHOT_SPEED * 0.8f;
                    boss_shoot_tear(g, e->x, e->y, (fdx / fm) * spd, (fdy / fm) * spd);
                }
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

        case ENEMY_FISTULA_BALL: {
            /* Fistula's split-off balls: simple bouncing drifter, no attack
               beyond contact damage (handled in the shared player collision). */
            if (!suppressAI) {
                e->x += e->dx;
                e->y += e->dy;
            }
            if (e->x <= ROOM_LEFT + ENEMY_SIZE) { e->dx = fabsf(e->dx); }
            if (e->x >= ROOM_RIGHT - ENEMY_SIZE) { e->dx = -fabsf(e->dx); }
            if (e->y <= ROOM_TOP + ENEMY_SIZE) { e->dy = fabsf(e->dy); }
            if (e->y >= ROOM_BOTTOM - ENEMY_SIZE) { e->dy = -fabsf(e->dy); }
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
        case ENEMY_BOSS_GISH:
        case ENEMY_BOSS_LOKI:
        case ENEMY_BOSS_STEVEN:
        case ENEMY_BOSS_CHUB:
        case ENEMY_BOSS_FISTULA:
        case ENEMY_BOSS_SCOLEX:
        case ENEMY_BOSS_MEGA_SATAN:
        case ENEMY_BOSS_MOM:
        case ENEMY_BOSS_MOMS_HEART:
        case ENEMY_BOSS_SATAN:
        case ENEMY_BOSS_ISAAC:
        case ENEMY_BOSS_THE_LAMB:
        case ENEMY_BOSS_IT_LIVES:
        case ENEMY_BOSS_URIEL:
        case ENEMY_BOSS_GABRIEL:
        case ENEMY_BOSS_KRAMPUS:
        case ENEMY_BOSS_BLUE_BABY:
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
        int aBoss = (a->type >= ENEMY_BOSS_DUKE && a->type <= ENEMY_BOSS_BLUE_BABY);
        for (int j = i + 1; j < r->enemy_count; j++) {
            Enemy *b = &r->enemies[j];
            if (!b->active) continue;
            int bBoss = (b->type >= ENEMY_BOSS_DUKE && b->type <= ENEMY_BOSS_BLUE_BABY);
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
    /* Boss block runs Duke..??? (Blue Baby). Mom/Mom's Heart/Satan, the R9
       trio Isaac/The Lamb/It Lives, and the R8 quartet Uriel/Gabriel/
       Krampus/Blue Baby all sit after Mega Satan in the enum, still before
       the minion types. */
    return t >= ENEMY_BOSS_DUKE && t <= ENEMY_BOSS_BLUE_BABY;
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
                /* Hidden enemies (Host in shell, Round Worm / Pin / Scolex
                   burrowed) are invulnerable to tears */
                if (e->hidden) {
                    if (!t->piercing) t->active = 0;
                    continue;
                }
                /* D5 Ipecac: explosive tears detonate on the target instead
                   of dealing normal contact damage (the blast covers it) */
                if (t->explosive) {
                    t->active = 0;
                    tear_detonate(g, t);
                    continue;
                }
                /* Float damage: fractional damage bonuses count for real.
                   Tiny floor so a cursed near-zero build still chips. */
                float dmg = t->dmg;
                /* R8 (M7) Lump of Coal: +0.03 damage per frame the tear
                   flew before impact, capped at +2.5 (tears only). */
                if ((p->stats.flags & ITEM_FLAG_COAL) && !t->is_enemy) {
                    float coal = (float)t->anim_frame * 0.03f;
                    if (coal > 2.5f) coal = 2.5f;
                    dmg += coal;
                }
                if (dmg < 0.1f) dmg = 0.1f;
                e->hp -= dmg;
                /* Snappier hit-flash (was 6) — about 14 frames feels more
                   responsive and reads clearly in motion. */
                e->flash = 14;
                audio_play(SFX_HIT);

                /* R8 (M8) GUPPY: 33% of player tear hits birth a friendly
                   blue fly at the impact point (spawn_blue_fly caps at 6) */
                if (!t->is_enemy && p->guppy_active && randi(0, 99) < 33)
                    spawn_blue_fly(g, t->x, t->y);

                /* Spawn blood splatter at impact point */
                spawn_blood_splatter(g, t->x, t->y, t->dx, t->dy, 1);
                /* Game-feel: also play the blue tear splash on impact */
                spawn_tear_pop(g, t->x, t->y, 0);

                /* Apply knockback in direction of tear velocity */
                float tv = sqrtf(t->dx * t->dx + t->dy * t->dy);
                if (tv > 0.1f) {
                    float kbScale = is_boss_type(e->type) ? 0.25f : 1.0f;
                    /* Knockback scales with tear damage: heavy hits shove */
                    float kbMag = TEAR_KNOCKBACK * (0.5f + t->dmg * 0.4f);
                    e->kb_dx += (t->dx / tv) * kbMag * kbScale;
                    e->kb_dy += (t->dy / tv) * kbMag * kbScale;
                }

                if (!t->piercing) {
                    t->active = 0;
                }

                /* Fistula: splits into a couple of smaller balls every time it
                   crosses a quarter-HP threshold, as long as it survives the hit. */
                if (e->type == ENEMY_BOSS_FISTULA && e->hp > 0) {
                    float quarter = e->max_hp * 0.25f;
                    if (quarter < 1.0f) quarter = 1.0f;
                    int thresholds_crossed = (int)((e->hp + dmg) / quarter)
                                           - (int)(e->hp / quarter);
                    if (thresholds_crossed > 0) {
                        float ball_hp = e->max_hp / 6.0f;
                        if (ball_hp < 1.0f) ball_hp = 1.0f;
                        spawn_fistula_ball(r, e->x, e->y, ball_hp);
                        spawn_fistula_ball(r, e->x, e->y, ball_hp);
                    }
                }

                /* Shared death path (drops, champion splits, boss tracking) */
                if (e->hp <= 0) {
                    kill_enemy(g, r, e);
                    /* F2: enemy died — stop testing the remaining tears of
                       this frame's volley against the corpse (a TRIPLE shot
                       at point-blank must not re-run the death path). */
                    if (!e->active) break;
                }
            } else if ((e->type == ENEMY_BOSS_GEMINI ||
                        e->type == ENEMY_BOSS_STEVEN ||
                        e->type == ENEMY_BOSS_THE_LAMB) &&
                       e->gemini_chp > 0 && !e->hidden &&
                       circle_overlap(t->x, t->y, TEAR_RADIUS,
                                      e->gemini_cx, e->gemini_cy, 10.0f)) {
                /* R8 #19: the second head is a real, killable target —
                   tears drain gemini_chp; at 0 the head goes down (no
                   contact damage, not drawn) and the survivor enrages.
                   R9: The Lamb's detached body uses the same fields. */
                int hd = (int)t->dmg;
                if (hd < 1) hd = 1;
                e->gemini_chp -= hd;
                e->flash = 14;
                audio_play(SFX_HIT);
                spawn_blood_splatter(g, t->x, t->y, t->dx, t->dy, 1);
                spawn_tear_pop(g, t->x, t->y, 0);
                if (!t->piercing) t->active = 0;
                if (e->gemini_chp <= 0) {
                    e->gemini_chp = 0;
                    /* Head-down gore + pop so the kill reads clearly */
                    spawn_blood_splatter(g, e->gemini_cx, e->gemini_cy,
                                         0, 0, 1);
                    spawn_blood_decal(r, e->gemini_cx, e->gemini_cy, 0);
                    audio_play(SFX_ENEMY_DEATH);
                    trigger_shake(g, 3.0f, 10);
                }
            }
        }

        /* Player vs Enemy (hidden/burrowed enemies deal no contact damage;
           freshly death-spawned enemies get a short grace so they can't hit
           the player on the very frame they appear at the corpse position).
           Bosses hit for a full heart everywhere; normal enemies hit for a
           full heart from Sheol on (floor >= 6); big bruisers (Fatty /
           Leaper / Vis) already hit full on the Womb (floor 5). */
        if (e->active && p->iframes == 0 && !e->hidden && e->spawn_grace == 0 &&
            !boss_airborne(e, NULL) /* R8 #18: no contact while mid-jump */ &&
            !boss_praying(e)   /* R9: Isaac deals no contact while praying */) {
            if (circle_overlap(p->x, p->y, PLAYER_SIZE, e->x, e->y, esz)) {
                int big_bruiser = (e->type == ENEMY_FATTY ||
                                   e->type == ENEMY_LEAPER ||
                                   e->type == ENEMY_VIS);
                int cdmg = (is_boss_type(e->type) ||
                            g->current_floor >= 6 ||
                            (big_bruiser && g->current_floor >= 5)) ? 2 : 1;
                p->hp -= player_absorb_dmg(p, cdmg);
                p->iframes = PLAYER_IFRAMES;
                g->hitstop = 3;
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
                audio_play(SFX_HURT_GRUNT);  /* New hurt grunt sound */
                trigger_shake(g, 5.0f, 18);
                if (player_check_death(p)) {
                    audio_play(SFX_PLAYER_DEATH);  /* Play death sound */
                    g->state = STATE_GAMEOVER;
                }
            }
        }
    }

    /* Spikes: damage the player on contact (1 full heart, like Rebirth) */
    if (p->iframes == 0 && g->state == STATE_PLAYING) {
        for (int i = 0; i < r->obstacle_count; i++) {
            Obstacle *o = &r->obstacles[i];
            if (!o->active || o->type != OBST_SPIKES) continue;
            if (circle_overlap(p->x, p->y, PLAYER_SIZE * 0.6f,
                               o->x, o->y, OBSTACLE_SIZE * 0.4f)) {
                p->hp -= player_absorb_dmg(p, 2);
                p->iframes = PLAYER_IFRAMES;
                g->hitstop = 3;
                audio_play(SFX_HURT_GRUNT);
                trigger_shake(g, 4.0f, 14);
                if (player_check_death(p)) {
                    audio_play(SFX_PLAYER_DEATH);
                    g->state = STATE_GAMEOVER;
                }
                /* Sacrifice room: repeated spike hits grant a reward */
                if (r->type == ROOM_SACRIFICE && !r->sacrifice_rewarded) {
                    r->sacrifice_hits++;
                    if (r->sacrifice_hits >= 3) {
                        r->sacrifice_rewarded = 1;
                        r->pedestal.x = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
                        r->pedestal.y = ROOM_TOP + 50;
                        r->pedestal.item = pick_random_item(g);
                        r->pedestal.active = (g->challenge != 6);  /* Purist */
                    }
                }
                break;
            }
        }
    }

    /* Arcade room: slot machine obstacle -- walk in with >=1 coin to pay and
       roll a reward (heart / consumable / small chance at a pedestal item).
       Bounded to a single use per room (arcade_slot_used persists across
       re-entries), mirroring the shop's shop_deny_timer cooldown for the
       "can't afford" feedback.
       R8 (M6): un-gated from ROOM_ARCADE — Wheel of Fortune can spawn a
       slot machine in ANY room, so the interaction keys off the obstacle
       itself (the loop below only touches OBST_SLOT_MACHINE). */
    {
        int room_has_slot = (r->type == ROOM_ARCADE);
        for (int i = 0; i < r->obstacle_count && !room_has_slot; i++)
            if (r->obstacles[i].active &&
                r->obstacles[i].type == OBST_SLOT_MACHINE)
                room_has_slot = 1;
    if (room_has_slot && g->state == STATE_PLAYING) {
        if (r->type == ROOM_ARCADE && g->shop_deny_timer > 0)
            g->shop_deny_timer--;
        for (int i = 0; i < r->obstacle_count; i++) {
            Obstacle *o = &r->obstacles[i];
            if (!o->active || o->type != OBST_SLOT_MACHINE) continue;
            if (!circle_overlap(p->x, p->y, PLAYER_SIZE * 0.6f,
                                o->x, o->y, OBSTACLE_SIZE * 0.5f)) continue;
            if (r->arcade_slot_used) break;
            if (p->coins >= 1) {
                p->coins -= 1;
                r->arcade_slot_used = 1;
                audio_play(SFX_PICKUP);
                if (luck_roll(g, 10, 2)) {
                    /* Small chance: free pedestal item */
                    r->pedestal.x = o->x;
                    r->pedestal.y = o->y - 40;
                    r->pedestal.item = pick_random_item(g);
                    r->pedestal.active = (g->challenge != 6);  /* Purist */
                } else if (randi(0, 1) == 0) {
                    spawn_heart(r, o->x, o->y - 40,
                                (randi(0, 3) == 0) ? HEART_SOUL : HEART_RED_HALF);
                } else {
                    int roll = randi(0, 2);
                    PickupType pt = (roll == 0) ? PICKUP_COIN :
                                    (roll == 1) ? PICKUP_BOMB : PICKUP_KEY;
                    spawn_consumable(r, o->x, o->y - 40, pt);
                }
            } else if (g->shop_deny_timer <= 0) {
                audio_play(SFX_HURT);
                g->shop_deny_timer = 30;
            }
            break;
        }
    }
    }   /* end R8 (M6) room_has_slot scope */

    /* Check room cleared. Boss Rush is excluded: its own wave controller
       (in enemies_update) owns clearing it, else it self-clears on frame 1
       before any wave spawns. The controller force-clears after the last wave. */
    if (g->state == STATE_PLAYING && !r->cleared && r->type != ROOM_BOSSRUSH) {
        int alive = 0;
        for (int i = 0; i < r->enemy_count; i++) {
            if (r->enemies[i].active) alive++;
        }
        if (alive == 0) {
            r->cleared = 1;
            g->rooms_cleared++;
            audio_play(SFX_ROOM_CLEAR);
            /* Recharge the active item by 1 on room clear */
            if (g->player.active_item != ITEM_NONE
                && g->player.active_charge < g->player.active_max_charge) {
                g->player.active_charge++;
            }
            /* Clear enemy projectiles when room is cleared */
            for (int k = 0; k < MAX_ENEMY_SHOTS; k++) g->enemy_shots[k].active = 0;

            /* Door-open poof at each doorway (Rebirth juice) */
            {
                float dmx = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
                float dmy = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
                float dpos[4][2] = {
                    {dmx, ROOM_TOP + 6}, {dmx, ROOM_BOTTOM - 6},
                    {ROOM_LEFT + 6, dmy}, {ROOM_RIGHT - 6, dmy}
                };
                for (int dd3 = 0; dd3 < 4; dd3++) {
                    if (!r->doors[dd3]) continue;
                    spawn_tear_pop(g, dpos[dd3][0], dpos[dd3][1], 0);
                    spawn_tear_pop(g, dpos[dd3][0], dpos[dd3][1], 0);
                }
            }

            /* Room-clear reward: weighted outcome table for normal rooms.
             * Higher luck and deeper floors shift weight from "nothing"
             * toward better rewards, ending in the two chest tiers. */
            if (r->type == ROOM_NORMAL) {
                float ccx = (ROOM_LEFT + ROOM_RIGHT) / 2.0f + randf(-30, 30);
                float ccy = (ROOM_TOP + ROOM_BOTTOM) / 2.0f + randf(-20, 20);
                int floor_bonus = g->current_floor * 2;       /* deeper = better */
                int luck_bonus  = (int)(g->player.stats.luck * 4);
                int bonus = floor_bonus + luck_bonus;
                if (bonus > 40) bonus = 40;                    /* keep it bounded */

                /* Rebirth-parity payout: most rooms give NOTHING. ~15% base
                 * payout, up to ~25% with deep floors + high luck; the paid
                 * weight splits roughly like the old table
                 * (consumable 35% / heart 25% / pill-card 15% / chest 15% /
                 *  gold chest = remainder). */
                int payout       = 15 + bonus / 4;             /* 15..25 */
                int w_nothing    = 100 - payout;
                int w_consumable = payout * 35 / 100;
                int w_heart      = payout * 25 / 100;
                int w_pillcard   = payout * 15 / 100;
                int w_chest      = payout * 15 / 100;
                /* remaining payout weight falls through to the gold chest */

                int roll = randi(0, 99);
                int t1 = w_nothing;
                int t2 = t1 + w_consumable;
                int t3 = t2 + w_heart;
                int t4 = t3 + w_pillcard;
                int t5 = t4 + w_chest;
                /* anything >= t5 falls through to the gold chest tier */

                if (roll < t1) {
                    /* nothing */
                } else if (roll < t2) {
                    spawn_random_consumable(r, ccx, ccy);
                } else if (roll < t3) {
                    spawn_heart(r, ccx, ccy,
                                luck_roll(g, 20, 3) ? HEART_SOUL : HEART_RED_HALF);
                } else if (roll < t4) {
                    if (randi(0, 1))
                        spawn_pill_pickup(r, ccx, ccy, randi(0, PILL_EFFECT_COUNT - 1));
                    else
                        spawn_card_pickup(r, ccx, ccy, randi(0, TAROT_COUNT - 1));
                } else if (roll < t5) {
                    spawn_consumable(r, ccx, ccy, PICKUP_CHEST);
                } else {
                    spawn_consumable(r, ccx, ccy, PICKUP_CHEST_GOLD);
                }
            }

            if (r->type == ROOM_BOSS) {
                /* Drop trapdoor to next floor */
                r->has_trapdoor = 1;

                /* Devil deal: 50% chance on a red-damage-free floor, but a
                   15% base chance survives red damage (Rebirth never hard-
                   zeroes the roll). 50/50 Devil vs Angel when it succeeds. */
                int deal_chance = 15 + (g->floor_red_dmg ? 0 : 35);
                if (randi(0, 99) < deal_chance) {
                    /* R8 (M3): once the player has bought ANY devil deal
                       this run, angels stop appearing — the flip always
                       chooses the devil room (Rebirth rule). */
                    int wantAngel = g->took_devil_deal ? 0
                                                       : (randi(0, 99) < 50);
                    Dungeon *dd2 = &g->dungeon;
                    int bx2 = dd2->cur_x, by2 = dd2->cur_y;
                    int ddx2[] = {0, 0, -1, 1}, ddy2[] = {-1, 1, 0, 0};
                    int opp2[] = {1, 0, 3, 2};
                    for (int di2 = 0; di2 < 4; di2++) {
                        int nx2 = bx2 + ddx2[di2], ny2 = by2 + ddy2[di2];
                        if (nx2 < 0 || nx2 >= DUNGEON_W ||
                            ny2 < 0 || ny2 >= DUNGEON_H) continue;
                        Room *dr = &dd2->rooms[ny2][nx2];
                        if (dr->type != ROOM_NONE) continue;

                        memset(dr, 0, sizeof(Room));
                        dr->gx = nx2; dr->gy = ny2;
                        dr->cleared = 1;
                        dr->enemies_spawned = 1;

                        if (wantAngel) {
                            dr->type = ROOM_ANGEL;
                            dr->doors[opp2[di2]] = 1;
                            dr->door_type[opp2[di2]] = 6;   /* angel door */
                            r->doors[di2] = 1;
                            r->door_type[di2] = 6;

                            /* Free item pedestal + 2 soul hearts (Purist:
                               soul hearts only) */
                            dr->pedestal.x = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
                            dr->pedestal.y = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
                            dr->pedestal.item = pick_random_item(g);
                            dr->pedestal.active = (g->challenge != 6);
                            spawn_heart(dr, ROOM_LEFT + 60, ROOM_TOP + 60, HEART_SOUL);
                            spawn_heart(dr, ROOM_RIGHT - 60, ROOM_TOP + 60, HEART_SOUL);
                            /* R8 (M3): this creation path skips
                               room_spawn_enemies, so place the angel
                               statue here too. */
                            if (dr->obstacle_count < MAX_OBSTACLES) {
                                Obstacle *st = &dr->obstacles[dr->obstacle_count++];
                                st->x = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
                                st->y = ROOM_TOP + 44.0f;
                                st->type = OBST_ANGEL_STATUE;
                                st->hp = 1;
                                st->active = 1;
                            }
                        } else {
                            dr->type = ROOM_DEVIL;
                            dr->doors[opp2[di2]] = 1;
                            dr->door_type[opp2[di2]] = 5;   /* devil door */
                            r->doors[di2] = 1;
                            r->door_type[di2] = 5;

                            /* R8 (M7): ~10% of devil rooms are a KRAMPUS
                               ambush instead — no stock, no free black
                               heart; he attacks ~45f after entry and his
                               death pedestal is the reward. */
                            if (randi(0, 99) < 10) {
                                dr->krampus_state = 1;
                                dr->shop_count = 0;
                                dd2->room_count++;
                                audio_play(SFX_DOOR);
                                break;
                            }

                            /* Two items priced in heart containers. E4: The
                               Pact is devil-pool biased — the first slot has
                               a 40% chance to stock it when not yet owned.
                               T3 "The Purist": no item stock — the free
                               black heart below is the whole deal. */
                            dr->shop_count = (g->challenge == 6) ? 0 : 2;
                            for (int si2 = 0; si2 < dr->shop_count; si2++) {
                                ShopItem *dsi = &dr->shop_items[si2];
                                dsi->item = pick_random_item(g); /* owned-item filtered */
                                if (si2 == 0 && randi(0, 99) < 40 &&
                                    !player_has_item(&g->player, ITEM_THE_PACT)) {
                                    dsi->item = ITEM_THE_PACT;
                                }
                                dsi->cost = 1 + randi(0, 1);   /* hearts, not coins */
                                dsi->x = (ROOM_LEFT + ROOM_RIGHT) / 2.0f - 45 + si2 * 90;
                                dsi->y = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
                                dsi->active = 1;
                            }
                            /* E5: devil rooms offer a free black heart */
                            spawn_heart(dr, (ROOM_LEFT + ROOM_RIGHT) / 2.0f,
                                        ROOM_BOTTOM - 40, HEART_BLACK);
                            /* R8 (M5): ~8% of devil rooms also hold a red
                               chest in the corner */
                            if (randi(0, 99) < 8)
                                spawn_consumable(dr, ROOM_LEFT + 50,
                                                 ROOM_BOTTOM - 44,
                                                 PICKUP_CHEST_RED);
                        }
                        dd2->room_count++;
                        audio_play(SFX_DOOR);   /* something opened... */
                        break;
                    }
                }
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

    /* Curse door - costs 1 HP to enter (cannot kill). F7: the toll goes
       through the normal absorb chain, so black/soul hearts pay first; the
       refusal guard only blocks when the toll would have to come out of a
       player's last half red heart. */
    if (r->door_type[dir] == 4) {
        Player *p = &g->player;
        /* B7: Holy Mantle absorbs the toll for free (Rebirth-correct), so
           refuse entry only when the toll would actually kill: no mantle,
           no soul/black hearts, and at the last half red heart. */
        if (!p->holy_mantle_active &&
            p->black_hp <= 0 && p->soul_hp <= 0 && p->hp <= 1) {
            audio_play(SFX_HURT); /* Feedback: can't afford HP cost */
            return 0;
        }
        p->hp -= player_absorb_dmg(p, 1);
        audio_play(SFX_HURT);
        if (player_check_death(p)) {
            /* Unreachable given the guard above, but keep the death path
               consistent (Dead Cat lives) if the guard ever changes. */
            audio_play(SFX_PLAYER_DEATH);
            g->state = STATE_GAMEOVER;
        }
    }

    return 1; /* allow transition */
}

/* A secret room entered by ANY means (bombed door, telepills, tarot warp,
 * Curse-of-Maze redirect) must never strand the player: its doors were
 * generated closed on both sides, so open them both ways to every adjacent
 * real room and mark it visited (Rebirth behavior). */
static void reveal_secret_room(Game *g, int rx, int ry) {
    Dungeon *d = &g->dungeon;
    Room *r = &d->rooms[ry][rx];
    if (r->type != ROOM_SECRET) return;
    int sdx[] = {0, 0, -1, 1};
    int sdy[] = {-1, 1, 0, 0};
    int opp[] = {1, 0, 3, 2};
    r->visited = 1;
    for (int i = 0; i < 4; i++) {
        int ax = rx + sdx[i], ay = ry + sdy[i];
        if (ax < 0 || ax >= DUNGEON_W || ay < 0 || ay >= DUNGEON_H) continue;
        if (d->rooms[ay][ax].type == ROOM_NONE) continue;
        r->doors[i] = 1;
        d->rooms[ay][ax].doors[opp[i]] = 1;
    }
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

        /* Rebirth rule: ALL doors stay shut until the room is cleared —
         * locked/special doors included (no fleeing a fight through a
         * treasure/curse door). */
        if (!r->cleared) return;

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

    /* B1: detonate any pending black-heart burst in the room being LEFT
       (e.g. the curse-door toll broke a black heart this frame) — never
       let it carry over and nuke the room being entered. */
    drain_black_burst(g);

    /* Curse of the Maze: small chance to redirect the player into a
     * different explored room adjacent to their CURRENT position instead
     * of the door they actually walked through. Only ever picks a room
     * that already exists and has been visited, so this can never send
     * the player out of bounds, into a ROOM_NONE cell, or soft-lock them
     * (the fallback is always the originally-intended, already-validated
     * room). */
    if (g->active_curse == CURSE_MAZE && randi(0, 99) < 12) {
        int mdxs[] = {0, 0, -1, 1};
        int mdys[] = {-1, 1, 0, 0};
        int candidates_x[4], candidates_y[4];
        int candidate_count = 0;
        for (int i = 0; i < 4; i++) {
            int mx = d->cur_x + mdxs[i];
            int my = d->cur_y + mdys[i];
            if (mx < 0 || mx >= DUNGEON_W || my < 0 || my >= DUNGEON_H) continue;
            if (d->rooms[my][mx].type == ROOM_NONE) continue;
            if (!d->rooms[my][mx].visited) continue;
            if (mx == nx && my == ny) continue; /* not the intended room */
            candidates_x[candidate_count] = mx;
            candidates_y[candidate_count] = my;
            candidate_count++;
        }
        if (candidate_count > 0) {
            int pick = randi(0, candidate_count - 1);
            nx = candidates_x[pick];
            ny = candidates_y[pick];
        }
        /* else: no valid alternate room, fall through to intended room */
    }

    /* Remember the room we're leaving for the sliding camera transition */
    g->slide_from_x = d->cur_x;
    g->slide_from_y = d->cur_y;

    d->cur_x = nx;
    d->cur_y = ny;

    Room *newRoom = &d->rooms[ny][nx];
    newRoom->visited = 1;
    /* Secret room landing (normal entry or maze redirect): open its doors */
    reveal_secret_room(g, nx, ny);

    /* NOTE: arcade_slot_used deliberately NOT reset on re-entry — the slot
       machine is once per room, or leaving/re-entering prints infinite items */

    /* Entering an uncleared Boss Rush room: reset the Game-global wave
       machine so the controller re-arms cleanly. R8 #25: the wave COUNTER
       itself lives in the Room now, so re-entry resumes where it left off. */
    if (newRoom->type == ROOM_BOSSRUSH && !newRoom->cleared) {
        g->bossrush_active = 0;
        g->bossrush_spawn_timer = 0;
    }

    /* R8 (M7): stepping into an armed Krampus devil room starts the
       ~45-frame ambush countdown (leaving before it fires re-arms it). */
    if (newRoom->type == ROOM_DEVIL && newRoom->krampus_state == 1)
        g->krampus_timer = 45;
    /* A player-burst render never carries across rooms */
    g->pbeam_timer = 0;

    /* E3: never carry a live enemy beam through a door (R9: nor the
       Lamb's cross flag or Isaac's light columns) */
    g->ebeam_state = 0;
    g->ebeam_timer = 0;
    g->ebeam_cross = 0;
    g->vbeam_state = 0;
    g->vbeam_timer = 0;
    g->vbeam_count = 0;
    /* E6: snap the familiar trail into the new room */
    familiars_reset_trail(g);

    for (int i = 0; i < MAX_TEARS; i++)
        g->tears[i].active = 0;

    audio_play(SFX_DOOR);

    if (!newRoom->enemies_spawned) {
        room_spawn_enemies(g, newRoom);
    }

    /* Reset enemy projectiles */
    for (int i = 0; i < MAX_ENEMY_SHOTS; i++) g->enemy_shots[i].active = 0;

    /* Rebirth-style sliding camera pan into the new room (replaces the old
       instant-teleport + fade). Warps (cards/pills/trapdoor) still fade. */
    g->slide_timer = SLIDE_FRAMES;
    g->slide_dir = dir;
    g->room_fade = 0;

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
    } else if (!newRoom->cleared) {
        /* Entry grace: half a second of iframes stepping into any
           uncleared room, so doorway ambushes can't cheap-shot. */
        if (g->player.iframes < 30) g->player.iframes = 30;
        /* R8: re-entering a non-boss room with a live miniboss (awakened
           angel / Krampus after a warp-out) re-arms the boss HUD + music */
        if (!g->boss_active && newRoom->type != ROOM_BOSSRUSH) {
            for (int bi = 0; bi < newRoom->enemy_count; bi++) {
                if (newRoom->enemies[bi].active &&
                    is_boss_type(newRoom->enemies[bi].type)) {
                    g->boss_active = 1;
                    g->boss_name =
                        boss_name_str(newRoom->enemies[bi].type);
                    music_play(MUS_BOSS);
                    break;
                }
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
    /* R10 (C4) Samson: Bloody Lust stacks reset on room change */
    if (g->player.samson_hits) {
        g->player.samson_hits = 0;
        recalc_player_stats(&g->player);
    }
    /* Clear any active screen creep when leaving the previous room */
    for (int i = 0; i < MAX_CREEP; i++) g->creep[i].active = 0;
    /* A live beam/knife/bomb must not persist across rooms (Phase D):
       bombs carry room-local coordinates, and the beam/knife reference the
       previous room's enemies. Knife snaps back to the held state. */
    g->laser_active = 0;
    g->laser_timer = 0;
    g->laser_charge = 0;
    g->knife_state = 0;
    g->knife_hit_cd = 0;
    for (int i = 0; i < MAX_BOMBS; i++) g->bombs[i].active = 0;
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
                    g->challenge = 0;       /* normal run, no challenge mods */
                    g->transition = 0;
                    break;
                case MENU_CONTINUE:
                    /* No save system yet: option is grayed out and does nothing.
                       When implemented, this would load g_config save slot. */
                    break;
                case MENU_CHALLENGES:
                    g->state = STATE_CHALLENGE_SELECT;
                    g->chal_sel = 0;
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
            g->challenge = 0;
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

    case STATE_CHALLENGE_SELECT:
        /* Pick one of the 6 challenge runs; A starts immediately as Isaac */
        if (kDown & KEY_DUP)   { g->chal_sel--; if (g->chal_sel < 0) g->chal_sel = 5; }
        if (kDown & KEY_DDOWN) { g->chal_sel++; if (g->chal_sel > 5) g->chal_sel = 0; }
        if (kDown & KEY_A) {
            g->challenge = g->chal_sel + 1;   /* 1..6 */
            g->game_mode = MODE_STORY;
            g->selected_character = CHAR_ISAAC;
            /* Eternal Darkness plays on Hard; the others on Normal */
            g->difficulty = (g->challenge == 2) ? DIFF_HARD : DIFF_NORMAL;
            start_new_game(g);
        }
        if (kDown & KEY_B) {
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
            g->challenge = 0;
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
                g->settings_changed = 0;  /* saved — no exit re-save needed */
            }
            g->state = STATE_MENU;
        }
        break;
    }

    case STATE_PLAYING:
        g->frame++;
        g->play_time_frames++;

        /* Game-feel HITSTOP: brief freeze on player damage. Skip the whole
           sim update (enemies/tears/shots/physics) for a few frames but keep
           rendering and stay in STATE_PLAYING. */
        if (g->hitstop > 0) {
            g->hitstop--;
            break;
        }

        /* R10 (C4): health-conditional character stats. Eve's Whore of
           Babylon and Samson's Bloody Lust live inside recalc_player_stats
           but depend on current HP — watch the total pool and recalc once
           on any change. A DECREASE is a hit taken: Samson gains a Bloody
           Lust stack (capped in recalc). The Lost's soul/black pools are
           also force-zeroed here so no pickup can ever grant him health. */
        {
            Player *pp = &g->player;
            if (pp->character == CHAR_LOST) {
                pp->soul_hp = 0;
                pp->black_hp = 0;
            }
            int hp_tot = pp->hp + pp->soul_hp + pp->black_hp;
            if (hp_tot != g->prev_hp_total) {
                if (hp_tot < g->prev_hp_total &&
                    pp->character == CHAR_SAMSON && pp->samson_hits < 7)
                    pp->samson_hits++;
                g->prev_hp_total = hp_tot;
                recalc_player_stats(pp);
            }
        }

        /* Challenge: Time Attack / Speed! — 20 minutes to win, or it's over.
           F13: route through player_check_death so Dead Cat lives aren't
           skipped — each remaining life buys a minute of overtime instead
           of the timeout re-killing the player every frame. */
        if ((g->challenge == 3 || g->challenge == 4)
            && g->play_time_frames >= 20 * 60 * 60) {
            g->player.hp = 0;
            g->player.soul_hp = 0;
            g->player.black_hp = 0;
            if (player_check_death(&g->player)) {
                audio_play(SFX_PLAYER_DEATH);
                g->state = STATE_GAMEOVER;
                break;
            }
            g->play_time_frames -= 60 * 60; /* overtime: 1 minute per life */
            audio_play(SFX_HURT);
        }

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

        /* Boss death animation countdown + gore bursts at the corpse
           (every 5 frames => ~12 bursts over the 60-frame anim) */
        if (g->boss_death_anim > 0) {
            g->boss_death_anim--;
            if ((g->boss_death_anim % 5) == 0) {
                float ga = (float)g->boss_death_anim * 0.7f;
                spawn_blood_splatter(g, g->boss_death_x + cosf(ga) * 18.0f,
                                     g->boss_death_y + sinf(ga) * 12.0f, 0, 0, 1);
            }
        }

        /* Decrement timers */
        if (g->pickup_msg_timer > 0) g->pickup_msg_timer--;
        if (g->player.book_belial_dmg_timer > 0) {
            g->player.book_belial_dmg_timer--;
            if (g->player.book_belial_dmg_timer == 0) recalc_player_stats(&g->player);
        }
        /* R8 (M6): tarot temp-buff expiry (zero the amount so the next
           card starts from a clean slate) */
        if (g->player.card_dmg_timer > 0) {
            g->player.card_dmg_timer--;
            if (g->player.card_dmg_timer == 0) {
                g->player.card_dmg_bonus = 0.0f;
                recalc_player_stats(&g->player);
            }
        }
        if (g->player.card_spd_timer > 0) {
            g->player.card_spd_timer--;
            if (g->player.card_spd_timer == 0) {
                g->player.card_spd_bonus = 0.0f;
                recalc_player_stats(&g->player);
            }
        }
        if (g->homing_timer > 0) g->homing_timer--;
        if (g->curse_display_timer > 0) g->curse_display_timer--;
        if (g->floor_intro_timer > 0) g->floor_intro_timer--;
        if (g->shop_deny_timer > 0) g->shop_deny_timer--;  /* also ticks outside shops (gold chests) */

        /* Yum Heart and Book of Belial are now active items: their effects are
         * triggered on demand via the active-item system (KEY_X) rather than
         * auto-firing here. See active-item use handling below. */

        /* Sliding room transition: freeze gameplay while the camera pans */
        if (g->slide_timer > 0) {
            g->slide_timer--;
            break;
        }

        /* Red hurt-pulse vignette countdown */
        if (g->hurt_flash_timer > 0) g->hurt_flash_timer--;

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

        /* Track HP across this frame's updates to trigger the hurt pulse.
           Red HP is tracked separately for devil-deal eligibility. */
        int hp_before = g->player.hp + g->player.soul_hp + g->player.black_hp;
        int red_before = g->player.hp;

        player_update(g, kHeld, circlePos);

        if (kHeld & KEY_X) shoot_tear(g, DIR_UP);
        if (kHeld & KEY_B) shoot_tear(g, DIR_DOWN);
        if (kHeld & KEY_Y) shoot_tear(g, DIR_LEFT);
        if (kHeld & KEY_A) shoot_tear(g, DIR_RIGHT);
        /* D1 Brimstone: the charge only holds while a fire button is held */
        if (!(kHeld & (KEY_X | KEY_B | KEY_Y | KEY_A))) g->laser_charge = 0;

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

        /* Active item use: all four face buttons (X/B/Y/A) are bound to shooting,
         * so the active item is triggered by tapping the bottom touch screen
         * (KEY_TOUCH), which is otherwise unused on Old & New 3DS. */
        if ((kDown & KEY_TOUCH)
            && g->player.active_item != ITEM_NONE
            && g->player.active_charge >= g->player.active_max_charge) {
            Player *p = &g->player;
            int used = 1;
            switch (p->active_item) {
            case ITEM_YUM_HEART:
                /* Heal 2 HP (clamped to max) */
                p->hp += 2;
                if (p->hp > p->stats.max_hp) p->hp = p->stats.max_hp;
                audio_play(SFX_PICKUP);
                break;
            case ITEM_BOOK_OF_BELIAL:
                /* +damage this room (reuse existing timer + recalc path) */
                p->book_belial_dmg_timer = 600;
                recalc_player_stats(p);
                audio_play(SFX_PICKUP);
                break;
            case ITEM_THE_POOP: {
                /* Spawn an OBST_POOP just ahead of the player's facing dir */
                Room *r = current_room(g);
                if (r && r->obstacle_count < MAX_OBSTACLES) {
                    float ox = p->x, oy = p->y;
                    switch (p->face_dir) {
                    case DIR_UP:    oy -= 28.0f; break;
                    case DIR_DOWN:  oy += 28.0f; break;
                    case DIR_LEFT:  ox -= 28.0f; break;
                    case DIR_RIGHT: ox += 28.0f; break;
                    default:        oy -= 28.0f; break;
                    }
                    if (ox < ROOM_LEFT)   ox = ROOM_LEFT;
                    if (ox > ROOM_RIGHT)  ox = ROOM_RIGHT;
                    if (oy < ROOM_TOP)    oy = ROOM_TOP;
                    if (oy > ROOM_BOTTOM) oy = ROOM_BOTTOM;
                    Obstacle *o = &r->obstacles[r->obstacle_count++];
                    o->x = ox; o->y = oy;
                    o->type = OBST_POOP;
                    o->hp = 3;
                    o->active = 1;
                    audio_play(SFX_PICKUP);
                } else {
                    used = 0;  /* no room for poop; keep charge */
                }
                break;
            }
            /* --- Phase E7 actives --- */
            case ITEM_NECRONOMICON:
                /* TAROT_DEATH's effect, dialed up: 40 to the whole room */
                damage_all_enemies(g, 40.0f);
                trigger_shake(g, 6.0f, 24);
                audio_play(SFX_BOSS);
                break;
            case ITEM_TELEPORT: {
                /* PILL_TELEPILLS path: random revealed-safe warp through
                   do_warp_cleanup (opens secret-room doors, resets state).
                   F12: if all 30 rolls miss a real room, the warp never
                   happened — keep the charge. */
                int ttries = 0;
                int warped = 0;
                /* B1: burst detonates in the room the heart broke in,
                   before cur_x/cur_y move (do_warp_cleanup runs after) */
                drain_black_burst(g);
                while (ttries < 30) {
                    int trx = randi(0, DUNGEON_W - 1);
                    int try2 = randi(0, DUNGEON_H - 1);
                    if (g->dungeon.rooms[try2][trx].type != ROOM_NONE) {
                        g->dungeon.cur_x = trx;
                        g->dungeon.cur_y = try2;
                        do_warp_cleanup(g);
                        warped = 1;
                        break;
                    }
                    ttries++;
                }
                if (!warped) used = 0;
                break;
            }
            case ITEM_DECK_OF_CARDS: {
                /* Draw a random tarot card as a pickup at the player */
                Room *cr = current_room(g);
                if (cr) {
                    spawn_card_pickup(cr, p->x + 16, p->y,
                                      randi(0, TAROT_COUNT - 1));
                    audio_play(SFX_PICKUP);
                } else {
                    used = 0;
                }
                break;
            }
            case ITEM_BIBLE: {
                /* Instantly kills Mom / Mom's Heart. Used on Satan it kills
                   the PLAYER (the classic). Elsewhere: flight flavor text. */
                Room *br2 = current_room(g);
                int did = 0;
                if (br2) {
                    for (int bi2 = 0; bi2 < br2->enemy_count; bi2++) {
                        Enemy *be = &br2->enemies[bi2];
                        if (!be->active) continue;
                        if (be->type == ENEMY_BOSS_MOM ||
                            be->type == ENEMY_BOSS_MOMS_HEART ||
                            be->type == ENEMY_BOSS_IT_LIVES) {
                            be->hp = 0;
                            kill_enemy(g, br2, be);
                            did = 1;
                        } else if (be->type == ENEMY_BOSS_SATAN) {
                            /* Divine judgment goes the other way */
                            p->hp = 0;
                            p->soul_hp = 0;
                            p->black_hp = 0;
                            p->lives = 0;
                            audio_play(SFX_PLAYER_DEATH);
                            g->state = STATE_GAMEOVER;
                            did = 1;
                            break;
                        }
                    }
                }
                if (!did) {
                    snprintf(g->pickup_msg_text, sizeof(g->pickup_msg_text),
                             "The Bible - You feel lighter for a moment.");
                    g->pickup_msg_timer = 120;
                    audio_play(SFX_PICKUP);
                }
                break;
            }
            /* --- R8 (M7) --- */
            case ITEM_HEAD_OF_KRAMPUS: {
                /* 4-way brimstone burst from the player: one deterministic
                   instant damage sweep along the four axis arms, plus a
                   ~10-frame beam render (g->pbeam_*). Deliberately does
                   NOT touch the enemy ebeam machine — it stays free for
                   whatever boss owns it. */
                Room *kr = current_room(g);
                if (kr) {
                    float arm = 14.0f;   /* arm half-width */
                    int initial = kr->enemy_count;
                    for (int ei = 0; ei < initial; ei++) {
                        Enemy *ke = &kr->enemies[ei];
                        if (!ke->active || ke->hidden) continue;
                        if (ke->spawn_grace >= 9) continue;
                        if (fabsf(ke->y - p->y) < arm + ENEMY_SIZE ||
                            fabsf(ke->x - p->x) < arm + ENEMY_SIZE) {
                            ke->hp -= 12.0f;
                            ke->flash = 14;
                            if (ke->hp <= 0) kill_enemy(g, kr, ke);
                        }
                    }
                    g->pbeam_timer = 10;
                    g->pbeam_x = p->x;
                    g->pbeam_y = p->y;
                    trigger_shake(g, 6.0f, 18);
                    audio_play(SFX_BOSS);
                } else {
                    used = 0;
                }
                break;
            }
            /* --- R8 (M8) --- */
            case ITEM_GUPPYS_HEAD:
                /* Summon 2 friendly blue flies (pool-capped at 6) */
                spawn_blue_fly(g, p->x - 12, p->y - 10);
                spawn_blue_fly(g, p->x + 12, p->y - 10);
                audio_play(SFX_PICKUP);
                break;
            default:
                used = 0;
                break;
            }
            if (used) g->player.active_charge = 0;
        }

        familiars_update(g);  /* E6: trail history + fire cooldowns */
        blue_flies_update(g); /* R8 (M8): friendly blue flies seek enemies */
        tears_update(g);
        enemies_update(g);
        enemy_shots_update(g);
        bomb_update(g);
        laser_update(g);    /* D1/D2 beam weapons */
        knife_update(g);    /* D3 Mom's Knife */
        creep_update(g);
        collisions_update(g);
        blood_particles_update(g);

        /* E5: each fully depleted black heart nukes the room for 40 (deaths
           routed through kill_enemy inside damage_all_enemies). B1: the same
           drain also runs before every room switch so it can't misfire in a
           freshly entered room. */
        drain_black_burst(g);

        check_door_transition(g);

        /* Any HP lost this frame -> red vignette pulse (black hearts absorb
           BEFORE soul hearts, so they must count or absorbed hits are silent) */
        if (g->player.hp + g->player.soul_hp + g->player.black_hp < hp_before) {
            g->hurt_flash_timer = HURT_FLASH_FRAMES;
            /* Swallowed Penny: pain pays — drop a coin on damage */
            if (g->player.trinket == TRINKET_SWALLOWED_PENNY)
                spawn_consumable(current_room(g),
                                 g->player.x, g->player.y - 18, PICKUP_COIN);
        }
        /* Red heart damage voids the devil deal for this floor */
        if (g->player.hp < red_before)
            g->floor_red_dmg = 1;
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
        /* R10 (C4): count the death exactly once (Lazarus unlock gate).
           death_counted is zeroed by start_new_game's memset. */
        if (!g->death_counted) {
            g->death_counted = 1;
            g_config.total_deaths++;
            apply_unlock_gates();
            config_save(&g_config);
        }
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

/* ================================================================
 * Round 7 shared paper-UI helpers
 * ================================================================ */

/* Aged-paper panel with a rough hand-drawn border. `a` scales all alphas
 * (255 = fully opaque). `plain` skips blotches/creases/nicks so unselected
 * list strips stay cheap and quiet. Deterministic — no rand(). */
static void draw_paper_panel_ex(float x, float y, float w, float h, u8 a, int plain) {
    /* 1. Drop shadow */
    C2D_DrawRectSolid(x + 3, y + 4, 0, w, h, C2D_Color32(0, 0, 0, (u8)(45 * a / 255)));

    /* 2. Rough border: 4 edge strips + 4 deterministic 1px "jogs" that break
     * the silhouette so it reads hand-drawn instead of a CSS box. */
    u32 border = C2D_Color32(62, 47, 34, a);       /* BORDER_BROWN @ a */
    C2D_DrawRectSolid(x - 3, y - 3, 0, w + 6, 3, border);   /* top    */
    C2D_DrawRectSolid(x - 3, y + h, 0, w + 6, 3, border);   /* bottom */
    C2D_DrawRectSolid(x - 3, y,     0, 3, h,     border);   /* left   */
    C2D_DrawRectSolid(x + w, y,     0, 3, h,     border);   /* right  */
    C2D_DrawRectSolid(x + w * 0.22f, y - 4,     0, 10, 2, border);
    C2D_DrawRectSolid(x + w * 0.68f, y + h + 1, 0, 12, 2, border);
    C2D_DrawRectSolid(x - 4,     y + h * 0.35f, 0, 2, 9,  border);
    C2D_DrawRectSolid(x + w + 1, y + h * 0.62f, 0, 2, 8,  border);

    /* 3. Paper fill */
    C2D_DrawRectSolid(x, y, 0, w, h, C2D_Color32(234, 221, 200, a)); /* PAPER @ a */

    if (plain) return;

    /* 4. Blotches (aged-paper stains) from a fixed table */
    for (int i = 0; i < 5; i++) {
        float ex = x + w * (0.18f + 0.31f * ((i * 7) % 3));
        float ey = y + h * (0.15f + 0.27f * ((i * 5) % 3));
        float ew = 14 + (i % 3) * 6;
        float eh = 9 + (i % 2) * 4;
        if (ex + ew > x + w) ex = x + w - ew;
        if (ey + eh > y + h) ey = y + h - eh;
        C2D_DrawEllipseSolid(ex, ey, 0, ew, eh,
                             C2D_Color32(219, 204, 178, (u8)(35 * a / 255)));
    }

    /* 5. Creases */
    u32 crease = C2D_Color32(190, 173, 143, a);    /* PAPER_EDGE @ a */
    C2D_DrawRectSolid(x + 2, y + h * 0.33f, 0, w - 4, 1, crease);
    C2D_DrawRectSolid(x + 2, y + h * 0.66f, 0, w - 4, 1, crease);

    /* 6. Corner nicks */
    u32 nick = C2D_Color32(219, 204, 178, a);      /* PAPER_DARK @ a */
    C2D_DrawRectSolid(x, y, 0, 2, 2, nick);
    C2D_DrawRectSolid(x + w - 2, y + h - 2, 0, 2, 2, nick);
    C2D_DrawRectSolid(x + w - 1, y - 1, 0, 3, 3, border);
}

static void draw_paper_panel(float x, float y, float w, float h, u8 a) {
    draw_paper_panel_ex(x, y, w, h, a, 0);
}

/* Blood-drip selection cursor. Uses the hand-drawn arrow sprite when the
 * menu-text sheet loaded; otherwise a procedural dripping splat. Bob phase
 * comes from the caller's timer — deterministic. */
static void draw_selector(float x, float y, int timer) {
    if (sheet_menu_text) {
        spr_draw(sheet_menu_text, menu_text_atlas_menu_arrow_idx,
                 x + sinf(timer * 0.12f) * 2.5f, y, 0.55f, 0.55f);
        return;
    }
    float bob = sinf(timer * 0.1f) * 1.5f;
    C2D_DrawCircleSolid(x, y, 0, 4.0f, BLOOD);
    C2D_DrawTriangle(x - 4, y, BLOOD,
                     x + 4, y, BLOOD,
                     x + bob, y + 7, BLOOD, 0);
    C2D_DrawCircleSolid(x - 1, y + 9, 0, 1.4f, BLOOD_DARK);
    C2D_DrawCircleSolid(x + 2, y + 12, 0, 1.0f, BLOOD_DARK);
}

/* Labelled pip row for stats (map page / character select). Filled pips are
 * solid INK; empty pips are a 1px PAPER_EDGE outline. */
static void draw_stat_pips(C2D_TextBuf textBuf, float x, float y,
                           const char *label, int filled, int max) {
    C2D_Text lt;
    gtext_parse(&lt, textBuf, label);
    C2D_TextOptimize(&lt);
    C2D_DrawText(&lt, C2D_WithColor, x, y, 0, 0.35f, 0.35f, INK_FAINT);

    if (max > 6) max = 6;
    if (filled > max) filled = max;
    if (filled < 0) filled = 0;
    for (int i = 0; i < max; i++) {
        float px = x + 34 + i * 9;
        float py = y + 2;
        if (i < filled) {
            C2D_DrawRectSolid(px, py, 0, 7, 5, INK);
        } else {
            C2D_DrawRectSolid(px,     py,     0, 7, 1, PAPER_EDGE);
            C2D_DrawRectSolid(px,     py + 4, 0, 7, 1, PAPER_EDGE);
            C2D_DrawRectSolid(px,     py,     0, 1, 5, PAPER_EDGE);
            C2D_DrawRectSolid(px + 6, py,     0, 1, 5, PAPER_EDGE);
        }
    }
}

/* Shared sub-menu title: INK text on a small paper strip with a BLOOD
 * scratch underline. */
static void draw_menu_title(C2D_TextBuf textBuf, const char *title) {
    C2D_Text t;
    gtext_parse(&t, textBuf, title);
    C2D_TextOptimize(&t);
    float tw = 0.0f, th = 0.0f;
    C2D_TextGetDimensions(&t, 0.55f, 0.55f, &tw, &th);
    float cx = TOP_SCREEN_WIDTH / 2.0f;
    draw_paper_panel(cx - tw / 2 - 10, 16, tw + 20, 24, 255);
    C2D_DrawText(&t, C2D_WithColor, cx - tw / 2, 16 + (24 - th) / 2, 0,
                 0.55f, 0.55f, INK);
    draw_doodle_scratch(cx - tw / 2, 44, cx + tw / 2, 46, BLOOD);
}

void render_menu(Game *g, C2D_TextBuf textBuf) {
    g->menu_timer++;

    /* ===== 1. PARCHMENT/CREAM BACKGROUND ===== */
    /* Warm beige base colour like an aged notebook page (shared palette) */
    u32 bg_col      = PAPER;
    u32 doodle_col  = C2D_Color32(160, 140, 120, 200);  /* sketchy gray-brown */
    u32 doodle_dim  = C2D_Color32(180, 165, 145, 180);
    u32 text_col    = INK;
    u32 text_disabled = INK_DISABLED;
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
        C2D_DrawRectSolid(gx, gy, 0, 1, 1, C2D_Color32(219, 204, 178, 120));
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
        gtext_parse(&titleA, textBuf, "BINDING");
        C2D_TextOptimize(&titleA);
        C2D_DrawText(&titleA, C2D_WithColor,
                     note_x + 40, note_y + 30, 0, 0.6f, 0.6f, text_col);
        gtext_parse(&titleB, textBuf, "of  ISAAC");
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
            gtext_parse(&optText, textBuf, opt_labels[i]);
            C2D_TextOptimize(&optText);
            C2D_DrawText(&optText, C2D_WithColor,
                         opt_x + xOffset, yPos, 0, 0.65f, 0.65f, col);
        }
    }

    /* ===== 6. BOTTOM HINT TEXT ===== */
    C2D_Text hint;
    gtext_parse(&hint, textBuf, "A: Select   D-Pad: Move   Y: Controls");
    C2D_TextOptimize(&hint);
    float hint_alpha = 0.6f + sinf(g->menu_timer * 0.08f) * 0.2f;
    C2D_DrawText(&hint, C2D_WithColor, 60, TOP_SCREEN_HEIGHT - 16, 0,
                 0.42f, 0.42f,
                 C2D_Color32(122, 99, 75, (int)(hint_alpha * 255)));

    /* Version text bottom-right */
    C2D_Text version;
    gtext_parse(&version, textBuf, "v2.0");
    C2D_TextOptimize(&version);
    C2D_DrawText(&version, C2D_WithColor,
                 TOP_SCREEN_WIDTH - 35, TOP_SCREEN_HEIGHT - 16, 0,
                 0.4f, 0.4f, C2D_Color32(122, 99, 75, 220));

    /* ==== AUDIO DEBUG OVERLAY (hold SELECT to show) ====
     * Kept for diagnosing audio failures on real hardware without a
     * debug console, but hidden during normal play.  Press X to play a
     * test tone, B to play a test SFX while it is visible. */
    if (hidKeysHeld() & KEY_SELECT) {
        AudioDebug ad;
        audio_debug_get(&ad);

        char dbg[160];
        snprintf(dbg, sizeof(dbg),
                 "AUDIO: %s  init=%d sfx_loaded=%d/%d",
                 audio_status_string(),
                 ad.init_result, ad.sfx_load_count, SFX_COUNT);

        C2D_Text line1;
        gtext_parse(&line1, textBuf, dbg);
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
        gtext_parse(&line2, textBuf, dbg2);
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
        gtext_parse(&line3, textBuf, dbg3);
        C2D_TextOptimize(&line3);
        C2D_DrawText(&line3, C2D_WithColor, 5, 26, 0,
                     0.34f, 0.34f, C2D_Color32(0, 80, 0, 255));
    }
}

/* ================================================================
 * Render: Game Mode Select screen
 * ================================================================ */

/* Shared helper: aged-paper background used across all sub-menu screens.
 * Full-screen page + grain + blotches + rough brown frame + corner doodles.
 * Deterministic — no rand(), no particles. */
static void draw_menu_background(Game *g) {
    g->menu_timer++;

    /* 1. Full-screen paper */
    C2D_DrawRectSolid(0, 0, 0, TOP_SCREEN_WIDTH, TOP_SCREEN_HEIGHT, PAPER);

    /* 2. Paper grain — faint specks at fixed spots */
    for (int i = 0; i < 24; i++) {
        float gx = (i * 53) % TOP_SCREEN_WIDTH;
        float gy = (i * 37) % TOP_SCREEN_HEIGHT;
        C2D_DrawRectSolid(gx, gy, 0, 1, 1, C2D_Color32(219, 204, 178, 120));
    }

    /* 3. Large blotches (aged stains) at fixed spots */
    C2D_DrawEllipseSolid(70 - 26, 190 - 16, 0, 52, 32, C2D_Color32(219, 204, 178, 30));
    C2D_DrawEllipseSolid(330 - 20, 40 - 14, 0, 40, 28, C2D_Color32(219, 204, 178, 30));
    C2D_DrawEllipseSolid(200 - 34, 120 - 20, 0, 68, 40, C2D_Color32(219, 204, 178, 30));

    /* 4. Rough hand-drawn frame (single, no glow) */
    {
        float fx = 6, fy = 6, fw = 388, fh = 228;
        C2D_DrawRectSolid(fx - 3, fy - 3, 0, fw + 6, 3, BORDER_BROWN);
        C2D_DrawRectSolid(fx - 3, fy + fh, 0, fw + 6, 3, BORDER_BROWN);
        C2D_DrawRectSolid(fx - 3, fy,     0, 3, fh,     BORDER_BROWN);
        C2D_DrawRectSolid(fx + fw, fy,    0, 3, fh,     BORDER_BROWN);
        C2D_DrawRectSolid(fx + fw * 0.22f, fy - 4,      0, 10, 2, BORDER_BROWN);
        C2D_DrawRectSolid(fx + fw * 0.68f, fy + fh + 1, 0, 12, 2, BORDER_BROWN);
        C2D_DrawRectSolid(fx - 4,      fy + fh * 0.35f, 0, 2, 9,  BORDER_BROWN);
        C2D_DrawRectSolid(fx + fw + 1, fy + fh * 0.62f, 0, 2, 8,  BORDER_BROWN);
    }

    /* 5. Corner doodles */
    {
        u32 doodle_col = C2D_Color32(160, 140, 120, 180);
        draw_doodle_scratch(15.0f, 40.0f, 45.0f, 30.0f, doodle_col);
        draw_doodle_scratch(TOP_SCREEN_WIDTH - 50.0f, 200.0f,
                            TOP_SCREEN_WIDTH - 20.0f, 210.0f, doodle_col);
        draw_doodle_bug(24.0f, 214.0f, doodle_col);
    }
}

/* Helper: draw a selection option row (used by mode/challenge/difficulty
   screens). Rebirth selection is quiet: every row sits on a stacked paper
   strip; the selected strip nudges left, gets the blood-drip selector and a
   BLOOD underline scribble under its label. No glow, no chevrons. */
static void draw_menu_option(C2D_TextBuf textBuf, const char *label, const char *desc,
                             float yPos, int selected, int timer) {
    float x = 50.0f, w = 300.0f;
    /* R6: 40 (was 44) — the panel strip's full extent is yPos-8 (top jog)
       to yPos+h (drop shadow); at the challenge screen's row pitch the
       44px strips bled into each other. Desc text ends ~yPos+33, so 40
       still clears it. */
    float h = desc ? 40.0f : 28.0f;
    if (selected) x -= 3.0f;

    if (selected)
        draw_paper_panel(x, yPos - 4, w, h, 255);
    else
        draw_paper_panel_ex(x, yPos - 4, w, h, 230, 1);

    /* Label */
    C2D_Text lbl;
    gtext_parse(&lbl, textBuf, label);
    C2D_TextOptimize(&lbl);
    float ts = selected ? 0.62f : 0.50f;
    float label_x = x + 26.0f;
    C2D_DrawText(&lbl, C2D_WithColor, label_x, yPos, 0, ts, ts,
                 selected ? INK : INK_FAINT);

    if (selected) {
        float label_w = 0.0f, label_h = 0.0f;
        C2D_TextGetDimensions(&lbl, ts, ts, &label_w, &label_h);
        draw_doodle_scratch(label_x, yPos + 16, label_x + label_w, yPos + 17, BLOOD);
        draw_selector(38, yPos + 8, timer);
    }

    /* Description text (smaller, below label) */
    if (desc) {
        C2D_Text dt;
        gtext_parse(&dt, textBuf, desc);
        C2D_TextOptimize(&dt);
        C2D_DrawText(&dt, C2D_WithColor, label_x, yPos + 21, 0,
                     0.38f, 0.38f, INK_FAINT);
    }
}

void render_mode_select(Game *g, C2D_TextBuf textBuf) {
    draw_menu_background(g);

    /* Title on a paper strip with blood scratch underline */
    draw_menu_title(textBuf, "SELECT GAME MODE");

    /* Mode options */
    draw_menu_option(textBuf, "Story Mode",
                     "Beat all floors to win. Classic Isaac!",
                     80, g->mode_sel == 0, g->menu_timer);
    draw_menu_option(textBuf, "Infinite Mode",
                     "Floors loop forever. How far can you go?",
                     145, g->mode_sel == 1, g->menu_timer);

    /* Hint */
    C2D_Text hint;
    gtext_parse(&hint, textBuf, "A: Select    B: Back");
    C2D_TextOptimize(&hint);
    C2D_DrawText(&hint, C2D_WithColor, 115, 218, 0, 0.48f, 0.48f, INK_FAINT);
}

/* ================================================================
 * Render: Challenge Select screen
 * ================================================================ */

void render_challenge_select(Game *g, C2D_TextBuf textBuf) {
    draw_menu_background(g);

    draw_menu_title(textBuf, "CHALLENGES");

    /* 6 challenges shown 3 at a time in a scrolling window centered on the
       selection (the draw_menu_option boxes are too tall for 6 rows). */
    {
        static const char *chal_names[6] = {
            "Glass Cannon", "Eternal Darkness", "Time Attack",
            "Speed!", "Cat Got Your Tongue", "The Purist"
        };
        static const char *chal_descs[6] = {
            "One heart. Massive damage. Don't get hit.",
            "Hard mode. Darkness curses every floor.",
            "Beat the game in 20 minutes. Clock's ticking.",
            "Everything moves 1.4x. 20 minute limit.",
            "No tears. Your familiars fight for you.",
            "No items anywhere. Final floor: Boss Rush."
        };
        int first = g->chal_sel - 1;
        if (first < 0) first = 0;
        if (first > 3) first = 3;
        for (int ci = 0; ci < 3; ci++) {
            int idx = first + ci;
            /* R6: pitch 52 + 40px panels = 4px clear gap between strips
               (extent yPos-8..yPos+40); row 3 bottoms out at 219, above
               the hint line at 220. */
            draw_menu_option(textBuf, chal_names[idx], chal_descs[idx],
                             75 + ci * 52, g->chal_sel == idx, g->menu_timer);
        }
        /* Position indicator */
        char posBuf[16];
        snprintf(posBuf, sizeof(posBuf), "%d / 6", g->chal_sel + 1);
        C2D_Text posT;
        gtext_parse(&posT, textBuf, posBuf);
        C2D_TextOptimize(&posT);
        C2D_DrawText(&posT, C2D_WithColor, TOP_SCREEN_WIDTH - 52, 30, 0,
                     0.45f, 0.45f, INK_FAINT);
    }

    C2D_Text hint;
    gtext_parse(&hint, textBuf, "A: Start    B: Back    Up/Down: More");
    C2D_TextOptimize(&hint);
    C2D_DrawText(&hint, C2D_WithColor, 92, 220, 0, 0.48f, 0.48f, INK_FAINT);
}

/* ================================================================
 * Render: Difficulty Select screen
 * ================================================================ */

void render_difficulty_select(Game *g, C2D_TextBuf textBuf) {
    draw_menu_background(g);

    /* Title with mode indicator */
    const char *mode_str = (g->game_mode == MODE_INFINITE) ? "INFINITE" : "STORY";
    char title_buf[48];
    snprintf(title_buf, sizeof(title_buf), "SELECT DIFFICULTY  [%s]", mode_str);
    draw_menu_title(textBuf, title_buf);

    /* Difficulty options with descriptions */
    const char *labels[] = { "Easy", "Normal", "Hard" };
    const char *descs[] = {
        "75% enemy HP, more hearts, extra starting HP",
        "Balanced gameplay. The classic experience.",
        "125% enemy HP, fewer hearts, pricier shops"
    };

    /* Difficulty color coding, kept as a small wax-seal dot per strip */
    u32 diff_colors[] = {
        C2D_Color32(80, 180, 80, 255),   /* green for easy */
        C2D_Color32(180, 180, 80, 255),  /* yellow for normal */
        C2D_Color32(200, 60, 60, 255)    /* red for hard */
    };

    for (int i = 0; i < DIFF_COUNT; i++) {
        float yPos = 60 + i * 55;
        draw_menu_option(textBuf, labels[i], descs[i], yPos, i == g->diff_sel,
                         g->menu_timer);
        /* Wax-seal dot at the strip's right edge */
        float sx = (i == g->diff_sel) ? 47.0f : 50.0f;
        C2D_DrawCircleSolid(sx + 300 - 14, yPos + 10, 0, 5, diff_colors[i]);
    }

    /* Hint */
    C2D_Text hint;
    gtext_parse(&hint, textBuf, "A: Start    B: Back");
    C2D_TextOptimize(&hint);
    C2D_DrawText(&hint, C2D_WithColor, 118, 218, 0, 0.48f, 0.48f, INK_FAINT);
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
        case CHAR_EVE:       return "Eve";
        case CHAR_SAMSON:    return "Samson";
        case CHAR_BLUE_BABY: return "???";
        case CHAR_AZAZEL:    return "Azazel";
        case CHAR_LAZARUS:   return "Lazarus";
        case CHAR_LOST:      return "The Lost";
        default:             return "?";
    }
}

static const char *character_blurb(CharacterType c) {
    switch (c) {
        case CHAR_ISAAC:     return "Balanced. 3 hearts. The default.";
        case CHAR_MAGDALENE: return "4 hearts, slower. Yum Heart heals.";
        case CHAR_CAIN:      return "2 hearts, +damage, +speed, +luck.";
        case CHAR_JUDAS:     return "1 heart, glass cannon. Book of Belial.";
        case CHAR_EVE:       return "3 hearts. Rages when down to 1 heart.";
        case CHAR_SAMSON:    return "3 hearts. Damage grows as he's hit.";
        case CHAR_BLUE_BABY: return "2 hearts, 4 soul hearts, +damage.";
        case CHAR_AZAZEL:    return "Flight. Short demon beam. 2+1 hearts.";
        case CHAR_LAZARUS:   return "3 hearts. Rises once, stronger.";
        case CHAR_LOST:      return "NO health. Flight, mantle, free deals.";
        default:             return "";
    }
}

static u32 character_tint(CharacterType c) {
    switch (c) {
        case CHAR_MAGDALENE: return C2D_Color32(255, 160, 200, 255); /* pink */
        case CHAR_CAIN:      return C2D_Color32(220, 200, 80,  255); /* gold */
        case CHAR_JUDAS:     return C2D_Color32(140, 40,  60,  255); /* dark red */
        case CHAR_EVE:       return C2D_Color32(180, 60,  120, 255); /* magenta */
        case CHAR_SAMSON:    return C2D_Color32(150, 90,  50,  255); /* brown */
        case CHAR_BLUE_BABY: return C2D_Color32(140, 200, 255, 255); /* pale blue */
        case CHAR_AZAZEL:    return C2D_Color32(75,  60,  95,  255); /* dark violet */
        case CHAR_LAZARUS:   return C2D_Color32(165, 205, 155, 255); /* pale green */
        case CHAR_LOST:      return C2D_Color32(238, 238, 238, 255); /* ghost white */
        case CHAR_ISAAC:
        default:             return C2D_Color32(255, 220, 180, 255); /* default */
    }
}

void render_character_select(Game *g, C2D_TextBuf textBuf) {
    draw_menu_background(g);

    /* Title */
    draw_menu_title(textBuf, "SELECT CHARACTER");

    /* R10 (C4): 10 characters no longer fit one row (10*52 + 9*4 = 556px
       > 400). Scrolling window instead: 7 cards visible, the selection
       kept roughly centered, chevrons hinting at off-screen cards. */
    const float card_w = 52.0f;
    const float card_h = 100.0f;
    const float spacing = 4.0f;
    const int   visible = (CHAR_COUNT < 7) ? CHAR_COUNT : 7;
    int first = g->char_sel - visible / 2;
    if (first < 0) first = 0;
    if (first > CHAR_COUNT - visible) first = CHAR_COUNT - visible;
    const float total_w = card_w * visible + spacing * (visible - 1);
    const float start_x = (TOP_SCREEN_WIDTH - total_w) / 2.0f;
    const float card_y = 55.0f;

    for (int vi = 0; vi < visible; vi++) {
        int i = first + vi;
        float cx = start_x + vi * (card_w + spacing);
        int selected = (i == g->char_sel);
        int unlocked_bit = (g_config.unlocked_chars & (1 << i)) ? 1 : 0;
        u32 tint = character_tint((CharacterType)i);

        /* Card = mini paper sheet (plain variant when not selected).
           Selected card keeps a gentle scale pulse. */
        float pulse = selected
                    ? (sinf(g->menu_timer * 0.15f) * 2.0f + 2.0f) : 0.0f;
        draw_paper_panel_ex(cx - pulse / 2, card_y - pulse / 2,
                            card_w + pulse, card_h + pulse, 255, !selected);

        if (selected) {
            /* Blood-drip arrow above the card + rough underline below it */
            draw_selector(cx + card_w / 2, card_y - 10, g->menu_timer);
            draw_doodle_scratch(cx + 4, card_y + card_h + 8,
                                cx + card_w - 4, card_y + card_h + 10, BLOOD);
        }

        if (unlocked_bit) {
            /* === Unlocked: portrait on a PAPER_DARK swatch === */
            /* Character tint reduced to a thin band at the card top */
            C2D_DrawRectSolid(cx, card_y, 0, card_w, 3, tint);
            C2D_DrawRectSolid(cx + 5, card_y + 8, 0, card_w - 10, 34,
                              PAPER_DARK);

            /* Procedural portrait: paper-palette face circle with small
               per-character identity touches (R10 C4), deterministic. */
            float fx = cx + card_w / 2, fy = card_y + 25;
            u32 faceCol = C2D_Color32(255, 240, 220, 255);
            if (i == CHAR_AZAZEL)       faceCol = C2D_Color32(70, 58, 85, 255);
            else if (i == CHAR_LOST)    faceCol = C2D_Color32(246, 246, 242, 255);
            else if (i == CHAR_LAZARUS) faceCol = C2D_Color32(228, 238, 216, 255);
            else if (i == CHAR_JUDAS)   faceCol = C2D_Color32(240, 220, 200, 255);
            /* Azazel: little demon horns behind the head */
            if (i == CHAR_AZAZEL) {
                u32 hornCol = C2D_Color32(45, 35, 55, 255);
                C2D_DrawTriangle(fx - 10, fy - 4, hornCol, fx - 4, fy - 8,
                                 hornCol, fx - 12, fy - 14, hornCol, 0);
                C2D_DrawTriangle(fx + 10, fy - 4, hornCol, fx + 4, fy - 8,
                                 hornCol, fx + 12, fy - 14, hornCol, 0);
            }
            C2D_DrawCircleSolid(fx, fy, 0, 11, faceCol);
            /* Judas: fez band */
            if (i == CHAR_JUDAS)
                C2D_DrawRectSolid(fx - 7, fy - 12, 0, 14, 4,
                                  C2D_Color32(140, 40, 60, 255));
            /* Lazarus: pale stitch across the brow (he's been dead) */
            if (i == CHAR_LAZARUS)
                C2D_DrawRectSolid(fx - 7, fy - 6, 0, 14, 1,
                                  C2D_Color32(120, 90, 90, 255));
            /* Eye dots (Azazel glows red; The Lost has hollow sockets) */
            u32 eyeCol = C2D_Color32(0, 0, 0, 255);
            float eyeR = 1.5f;
            if (i == CHAR_AZAZEL) eyeCol = C2D_Color32(215, 45, 45, 255);
            if (i == CHAR_LOST)   { eyeCol = C2D_Color32(30, 30, 30, 255); eyeR = 2.4f; }
            C2D_DrawCircleSolid(fx - 3, fy - 2, 0, eyeR, eyeCol);
            if (i != CHAR_CAIN) {  /* Cain has only one eye */
                C2D_DrawCircleSolid(fx + 3, fy - 2, 0, eyeR, eyeCol);
            } else {
                /* eye-patch */
                C2D_DrawRectSolid(fx, fy - 4, 0, 6, 3, C2D_Color32(40, 40, 40, 255));
            }
            /* The Lost: tiny open mouth — the classic ghost face */
            if (i == CHAR_LOST)
                C2D_DrawCircleSolid(fx, fy + 4, 0, 1.8f, C2D_Color32(30, 30, 30, 255));

            /* Character name */
            C2D_Text nm;
            gtext_parse(&nm, textBuf, character_name((CharacterType)i));
            C2D_TextOptimize(&nm);
            C2D_DrawText(&nm, C2D_WithColor, cx + 4, card_y + 52, 0,
                         0.38f, 0.38f, selected ? INK : INK_FAINT);

            /* HP as a heart row (Rebirth-style) instead of "HP:%d" */
            int hp_disp = 3;
            if (i == CHAR_MAGDALENE) hp_disp = 4;
            else if (i == CHAR_CAIN) hp_disp = 2;
            else if (i == CHAR_JUDAS) hp_disp = 1;
            else if (i == CHAR_EVE) hp_disp = 3;
            else if (i == CHAR_SAMSON) hp_disp = 3;
            else if (i == CHAR_BLUE_BABY) hp_disp = 2;
            else if (i == CHAR_AZAZEL) hp_disp = 2;   /* + black heart below */
            else if (i == CHAR_LAZARUS) hp_disp = 3;
            else if (i == CHAR_LOST) hp_disp = 0;     /* NO health */
            int hearts_shown = hp_disp > 4 ? 4 : hp_disp;  /* cap 4 per card */
            for (int h = 0; h < hearts_shown; h++) {
                float hhx = cx + 10 + h * 10;
                float hhy = card_y + 74;
                if (g_sprites_loaded && sheet_ui_items) {
                    spr_draw(sheet_ui_items,
                             i == CHAR_BLUE_BABY
                                 ? ui_items_atlas_heart_soul_full_idx
                                 : ui_items_atlas_heart_red_full_idx,
                             hhx, hhy, 0.45f, 0.45f);
                } else {
                    draw_heart(hhx, hhy, 8,
                               i == CHAR_BLUE_BABY
                                   ? C2D_Color32(120, 160, 220, 255)
                                   : BLOOD);
                }
            }
            /* Azazel: his 1 starting black heart, drawn as a dark heart */
            if (i == CHAR_AZAZEL)
                draw_heart(cx + 10 + hearts_shown * 10, card_y + 74, 8,
                           C2D_Color32(40, 35, 50, 255));
            /* The Lost: a faint dash where hearts would be — nothing to lose */
            if (i == CHAR_LOST)
                C2D_DrawRectSolid(cx + 12, card_y + 78, 0, card_w - 24, 2,
                                  INK_DISABLED);
        } else {
            /* === Locked: dimmed paper + big inked "?" === */
            C2D_DrawRectSolid(cx, card_y, 0, card_w, card_h,
                              C2D_Color32(60, 50, 40, 150));
            C2D_Text qm;
            gtext_parse(&qm, textBuf, "?");
            C2D_TextOptimize(&qm);
            float qw = 0.0f, qh = 0.0f;
            C2D_TextGetDimensions(&qm, 0.7f, 0.7f, &qw, &qh);
            C2D_DrawText(&qm, C2D_WithColor, cx + (card_w - qw) / 2,
                         card_y + (card_h - qh) / 2, 0, 0.7f, 0.7f, INK);
        }
    }

    /* R10 (C4): scroll chevrons when cards sit off-screen either side */
    {
        float ay = card_y + card_h / 2;
        if (first > 0) {
            C2D_DrawTriangle(start_x - 14, ay, INK,
                             start_x - 6,  ay - 6, INK,
                             start_x - 6,  ay + 6, INK, 0);
        }
        if (first + visible < CHAR_COUNT) {
            float rx = start_x + total_w;
            C2D_DrawTriangle(rx + 14, ay, INK,
                             rx + 6,  ay - 6, INK,
                             rx + 6,  ay + 6, INK, 0);
        }
    }

    /* Selected-character blurb on a small paper strip */
    draw_paper_panel(30, 180, 340, 26, 255);
    {
        int sel_unlocked = (g_config.unlocked_chars & (1 << g->char_sel)) ? 1 : 0;
        const char *blurb = sel_unlocked
            ? character_blurb((CharacterType)g->char_sel)
            : "Complete the game with another character to unlock.";
        C2D_Text desc;
        gtext_parse(&desc, textBuf, blurb);
        C2D_TextOptimize(&desc);
        float dw = 0.0f, dh = 0.0f;
        C2D_TextGetDimensions(&desc, 0.42f, 0.42f, &dw, &dh);
        C2D_DrawText(&desc, C2D_WithColor, 200 - dw / 2,
                     180 + (26 - dh) / 2, 0, 0.42f, 0.42f,
                     sel_unlocked ? INK : INK_FAINT);
    }

    /* Hint */
    C2D_Text hint;
    gtext_parse(&hint, textBuf, "D-Pad/Stick: Select   A: Confirm   B: Back");
    C2D_TextOptimize(&hint);
    C2D_DrawText(&hint, C2D_WithColor, 65, 220, 0, 0.45f, 0.45f, INK_FAINT);
}

/* ================================================================
 * Render: Stats overlay (bottom screen when paused)
 * ================================================================ */

void render_stats_overlay(Game *g, C2D_TextBuf textBuf) {
    C2D_TextBufClear(textBuf);
    Player *p = &g->player;

    /* Background: full-screen paper page + grain (Round 7) */
    C2D_DrawRectSolid(0, 0, 0, BOT_SCREEN_WIDTH, BOT_SCREEN_HEIGHT, PAPER);
    for (int i = 0; i < 30; i++) {
        float gx = (i * 47) % BOT_SCREEN_WIDTH;
        float gy = (i * 31) % BOT_SCREEN_HEIGHT;
        C2D_DrawRectSolid(gx, gy, 0, 1, 1, C2D_Color32(219, 204, 178, 120));
    }

    /* Title on a scratch underline */
    C2D_Text title;
    gtext_parse(&title, textBuf, "PLAYER STATS");
    C2D_TextOptimize(&title);
    C2D_DrawText(&title, C2D_WithColor, 90, 6, 0, 0.65f, 0.65f, INK);
    {
        float tw = 0.0f, th = 0.0f;
        C2D_TextGetDimensions(&title, 0.65f, 0.65f, &tw, &th);
        draw_doodle_scratch(90, 24, 90 + tw, 26, BLOOD);
    }

    /* Character name (tint kept as a small swatch, text inked) */
    char cb[48];
    snprintf(cb, sizeof(cb), "%s   Floor %d", character_name(p->character),
             g->current_floor + 1);
    C2D_Text ct;
    gtext_parse(&ct, textBuf, cb);
    C2D_TextOptimize(&ct);
    C2D_DrawRectSolid(66, 28, 0, 10, 10, character_tint(p->character));
    C2D_DrawText(&ct, C2D_WithColor, 80, 27, 0, 0.5f, 0.5f, INK);

    /* Stats list */
    char buf[64];
    float y = 50;
    float dy = 16;
    u32 lc = INK_FAINT;
    u32 vc = INK;

    #define STAT_LINE(label, fmt, val) do { \
        C2D_Text lt; \
        gtext_parse(&lt, textBuf, label); \
        C2D_TextOptimize(&lt); \
        C2D_DrawText(&lt, C2D_WithColor, 18, y, 0, 0.46f, 0.46f, lc); \
        snprintf(buf, sizeof(buf), fmt, val); \
        C2D_Text vt; \
        gtext_parse(&vt, textBuf, buf); \
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
    gtext_parse(&tl, textBuf, "Time:");
    C2D_TextOptimize(&tl);
    C2D_DrawText(&tl, C2D_WithColor, 18, y, 0, 0.46f, 0.46f, lc);
    gtext_parse(&tv, textBuf, buf);
    C2D_TextOptimize(&tv);
    C2D_DrawText(&tv, C2D_WithColor, 130, y, 0, 0.46f, 0.46f, vc);

    #undef STAT_LINE

    /* Bottom hint */
    C2D_Text hint;
    gtext_parse(&hint, textBuf, "START or B: Resume");
    C2D_TextOptimize(&hint);
    C2D_DrawText(&hint, C2D_WithColor, 88, 218, 0, 0.45f, 0.45f, INK_FAINT);
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

    /* Item pickups store "<name> - <desc>" in pickup_msg_text; pill/card/
       trinket pickups store just "Pill: <color>" / "Card: <name>" /
       "Trinket: <name>" with no description. Split on the first " - " so
       we can render the name (bigger) over the description (smaller); if
       there's no separator, the whole string is treated as the name. */
    char name_buf[96];
    char desc_buf[96];
    name_buf[0] = '\0';
    desc_buf[0] = '\0';
    snprintf(name_buf, sizeof(name_buf), "%s", g->pickup_msg_text);
    {
        char *sep = strstr(name_buf, " - ");
        if (sep) {
            *sep = '\0';
            snprintf(desc_buf, sizeof(desc_buf), "%s", sep + 3);
        }
    }

    u32 textcol = C2D_Color32(55, 43, 33, alpha);    /* INK, alpha-scaled */
    u32 desccol = C2D_Color32(122, 99, 75, alpha);   /* INK_FAINT, alpha-scaled */

    /* Paper strip, sized to fit the two lines of text */
    float bw = 320;
    float bh = desc_buf[0] ? 34 : 24;
    float bx = (TOP_SCREEN_WIDTH - bw) / 2;
    float by = ROOM_BOTTOM - bh - 4;
    u32 frame = C2D_Color32(62, 47, 34, alpha);      /* BORDER_BROWN, alpha-scaled */
    C2D_DrawRectSolid(bx + 3, by + 4, 0, bw, bh, C2D_Color32(0, 0, 0, (alpha * 45) / 255));
    C2D_DrawRectSolid(bx - 2, by - 2, 0, bw + 4, 2, frame);
    C2D_DrawRectSolid(bx - 2, by + bh, 0, bw + 4, 2, frame);
    C2D_DrawRectSolid(bx - 2, by, 0, 2, bh, frame);
    C2D_DrawRectSolid(bx + bw, by, 0, 2, bh, frame);
    C2D_DrawRectSolid(bx, by, 0, bw, bh, C2D_Color32(234, 221, 200, (alpha * 240) / 255));
    C2D_DrawEllipseSolid(bx + bw * 0.16f, by + bh * 0.3f, 0, 22, 10,
                         C2D_Color32(219, 204, 178, (alpha * 35) / 255));

    /* Name, larger, centered */
    C2D_Text nameText;
    gtext_parse(&nameText, textBuf, name_buf);
    C2D_TextOptimize(&nameText);
    float nw = 0.0f, nh = 0.0f;
    C2D_TextGetDimensions(&nameText, 0.52f, 0.52f, &nw, &nh);
    float nx = bx + (bw - nw) / 2.0f;
    float ny = by + (desc_buf[0] ? 4 : (bh - nh) / 2.0f);
    C2D_DrawText(&nameText, C2D_WithColor, nx, ny, 0, 0.52f, 0.52f, textcol);

    /* Description, smaller, centered underneath */
    if (desc_buf[0]) {
        C2D_Text descText;
        gtext_parse(&descText, textBuf, desc_buf);
        C2D_TextOptimize(&descText);
        float dw = 0.0f, dh = 0.0f;
        C2D_TextGetDimensions(&descText, 0.38f, 0.38f, &dw, &dh);
        float dx = bx + (bw - dw) / 2.0f;
        float dy = by + bh - dh - 4;
        C2D_DrawText(&descText, C2D_WithColor, dx, dy, 0, 0.38f, 0.38f, desccol);
    }
}

/* ================================================================
 * Render: Controls screen
 * ================================================================ */

/* ================================================================
 * Render: Settings screen
 * ================================================================ */
void render_settings(Game *g, C2D_TextBuf textBuf) {
    /* Round 7: shared paper background + title strip */
    draw_menu_background(g);
    draw_menu_title(textBuf, "SETTINGS");

    /* Audio status indicator */
    C2D_Text status_label, status_val;
    gtext_parse(&status_label, textBuf, "Audio Status:");
    C2D_TextOptimize(&status_label);
    C2D_DrawText(&status_label, C2D_WithColor, 30, 54, 0, 0.5f, 0.5f,
                 INK_FAINT);

    const char *status_str = audio_status_string();
    gtext_parse(&status_val, textBuf, status_str);
    C2D_TextOptimize(&status_val);
    u32 status_col;
    if (audio_is_available())
        status_col = C2D_Color32(80, 180, 80, 255);    /* green */
    else
        status_col = C2D_Color32(200, 140, 50, 255);   /* orange */
    C2D_DrawText(&status_val, C2D_WithColor, 175, 54, 0, 0.5f, 0.5f, status_col);

    /* Separator crease */
    C2D_DrawRectSolid(30, 72, 0, 340, 1, PAPER_EDGE);

    /* Settings items */
    const char *labels[SETTINGS_COUNT] = {
        "Audio Enabled:",
        "SFX Volume:",
        "Music Volume:"
    };

    for (int i = 0; i < SETTINGS_COUNT; i++) {
        float yPos = 84 + i * 34;
        int selected = (i == g->settings_sel);

        /* Paper strip per row; selected row nudges left + gets the
           blood-drip selector (no pulse highlight). */
        if (selected) {
            draw_paper_panel(37, yPos - 6, 326, 26, 255);
            draw_selector(26, yPos + 6, g->menu_timer);
        } else {
            draw_paper_panel_ex(40, yPos - 6, 320, 26, 230, 1);
        }

        /* Label */
        C2D_Text lbl;
        gtext_parse(&lbl, textBuf, labels[i]);
        C2D_TextOptimize(&lbl);
        C2D_DrawText(&lbl, C2D_WithColor, 48, yPos, 0, 0.5f, 0.5f,
                     selected ? INK : INK_FAINT);

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
        gtext_parse(&val_txt, textBuf, val_buf);
        C2D_TextOptimize(&val_txt);
        C2D_DrawText(&val_txt, C2D_WithColor, 252, yPos, 0, 0.5f, 0.5f,
                     selected ? BLOOD : BLOOD_DARK);
    }

    /* Restart notice if audio toggle changed */
    if (g->settings_changed) {
        C2D_Text notice;
        gtext_parse(&notice, textBuf, "* Audio toggle takes effect on next launch *");
        C2D_TextOptimize(&notice);
        float blink = sinf(g->menu_timer * 0.1f);
        int alpha = (int)(150 + blink * 80);
        C2D_DrawText(&notice, C2D_WithColor, 50, 196, 0, 0.42f, 0.42f,
                     C2D_Color32(110, 12, 16, alpha));
    }

    /* Hint at bottom (audio backend implementation detail no longer shown) */
    C2D_Text hint;
    gtext_parse(&hint, textBuf, "D-Pad: Navigate   A/L/R: Change   B: Save & Back");
    C2D_TextOptimize(&hint);
    C2D_DrawText(&hint, C2D_WithColor, 45, 226, 0, 0.38f, 0.38f, INK_FAINT);
}

void render_controls(Game *g, C2D_TextBuf textBuf) {
    /* Round 7: shared paper background + title strip */
    draw_menu_background(g);
    draw_menu_title(textBuf, "CONTROLS");

    C2D_Text lines[12];
    const char *text[] = {
        "",   /* title handled by draw_menu_title */
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
    float ypos[] = { 0, 0, 56, 74, 92, 110, 128, 0, 152, 168, 184, 206 };

    for (int i = 0; i < 12; i++) {
        if (strlen(text[i]) == 0) continue;
        gtext_parse(&lines[i], textBuf, text[i]);
        C2D_TextOptimize(&lines[i]);
        float scale = 0.42f;
        u32 col = (i == 11) ? INK_FAINT : INK;
        C2D_DrawText(&lines[i], C2D_WithColor, 40, ypos[i], 0, scale, scale, col);
    }
}

/* ================================================================
 * Render: Unlocks/Achievements Screen (Phase 2)
 * ================================================================ */

void render_unlocks_screen(Game *g, C2D_TextBuf textBuf) {
    /* Round 7: shared paper background + title strip */
    draw_menu_background(g);
    draw_menu_title(textBuf, "UNLOCKS & STATS");

    /* Build all the lines. Sized to hold the character list (10 as of R10
       C4) + stats + the full 27-boss roster (headers/stats 17 + 27 bosses
       = 44 < 48; sized with headroom on purpose — resize when either
       roster grows again). buf is static to keep the ~3KB scratch off the
       per-frame render stack. is_header/marker are presentation-only
       parallel flags — the line-count/window/scroll math is untouched. */
    static char buf[48][64];
    const char *lines_text[48];
    u8 is_header[48] = {0};
    u8 marker[48] = {0};   /* 0 none, 1 = defeated X, 2 = pending dash */
    int line_count = 0;

    /* Header section: characters */
    snprintf(buf[line_count], 64, "CHARACTERS");
    is_header[line_count] = 1;
    lines_text[line_count] = buf[line_count]; line_count++;

    /* Character unlocks */
    const char *chars[] = { "ISAAC", "MAGDALENE", "CAIN", "JUDAS", "EVE",
                            "SAMSON", "???", "AZAZEL", "LAZARUS", "THE LOST" };
    for (int i = 0; i < CHAR_COUNT; i++) {
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
    snprintf(buf[line_count], 64, "STATS");
    is_header[line_count] = 1;
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
    for (int b = 0; b < 27; b++) if (boss_mask & (1 << b)) boss_count++;
    snprintf(buf[line_count], 64, "Bosses Defeated: %d / 27", boss_count);
    lines_text[line_count] = buf[line_count]; line_count++;

    /* Boss list */
    snprintf(buf[line_count], 64, "BOSSES");
    is_header[line_count] = 1;
    lines_text[line_count] = buf[line_count]; line_count++;

    /* Must stay in exact EnemyType enum order (ENEMY_BOSS_DUKE..
       ENEMY_BOSS_BLUE_BABY) since boss_idx is computed as
       (e->type - ENEMY_BOSS_DUKE) -- keep in sync whenever a boss is
       added/reordered. */
    const char *boss_names[] = {
        "Duke of Flies", "Monstro", "Gemini", "Larry Jr.", "Famine",
        "Peep", "Gurdy", "Pin", "The Haunt", "Widow", "Gish",
        "Loki", "Steven", "Chub", "Fistula", "Scolex", "Mega Satan",
        "Mom", "Mom's Heart", "Satan", "Isaac", "The Lamb", "It Lives",
        "Uriel", "Gabriel", "Krampus", "Blue Baby"
    };
    /* Pack two columns - we'll show ones we have seen vs ???  */
    for (int b = 0; b < 27 && line_count < 48; b++) {
        int defeated = (g_config.bosses_defeated & (1 << b)) != 0;
        snprintf(buf[line_count], 64, "    %s",
                defeated ? boss_names[b] : "???");
        marker[line_count] = defeated ? 1 : 2;
        lines_text[line_count] = buf[line_count]; line_count++;
    }

    /* Scroll bounds */
    int max_scroll = line_count - 11;
    if (max_scroll < 0) max_scroll = 0;
    if (g->unlocks_scroll > max_scroll) g->unlocks_scroll = max_scroll;

    /* Boss defeat markers: parse once, stamp per visible boss line */
    C2D_Text mkX, mkDash;
    gtext_parse(&mkX, textBuf, "X");
    C2D_TextOptimize(&mkX);
    gtext_parse(&mkDash, textBuf, "-");
    C2D_TextOptimize(&mkDash);

    /* Render visible lines (window math unchanged: 11 lines from scroll) */
    C2D_Text txt[16];
    float y = 52.0f;
    const float pitch = 15.0f;
    int start = g->unlocks_scroll;
    int end = start + 11;
    if (end > line_count) end = line_count;
    for (int i = start; i < end; i++) {
        gtext_parse(&txt[i - start], textBuf, lines_text[i]);
        C2D_TextOptimize(&txt[i - start]);
        u32 col = is_header[i] ? BLOOD : INK;
        C2D_DrawText(&txt[i - start], C2D_WithColor, 30, y, 0, 0.42f, 0.42f, col);
        if (marker[i] == 1)
            C2D_DrawText(&mkX, C2D_WithColor, 32, y, 0, 0.42f, 0.42f, BLOOD);
        else if (marker[i] == 2)
            C2D_DrawText(&mkDash, C2D_WithColor, 32, y, 0, 0.42f, 0.42f,
                         INK_FAINT);
        y += pitch;
    }

    /* Thin paper scroll-track on the right edge: PAPER_EDGE rail + INK
       thumb sized/positioned from the existing scroll state. */
    {
        const float track_y = 52.0f;
        const float track_h = 11 * pitch;
        C2D_DrawRectSolid(384, track_y, 0, 2, track_h, PAPER_EDGE);
        if (line_count > 11) {
            float thumb_h = track_h * 11.0f / (float)line_count;
            float ty = track_y;
            if (max_scroll > 0)
                ty += (track_h - thumb_h) *
                      (float)g->unlocks_scroll / (float)max_scroll;
            C2D_DrawRectSolid(382, ty, 0, 6, thumb_h, INK);
        }
    }

    /* Footer hint */
    C2D_Text hint;
    gtext_parse(&hint, textBuf, "Up/Down: Scroll   B: Back");
    C2D_TextOptimize(&hint);
    C2D_DrawText(&hint, C2D_WithColor, 60, 224, 0, 0.38f, 0.38f, INK_FAINT);
}

/* ================================================================
 * Render: HUD
 * ================================================================ */

void render_hud(Game *g, C2D_TextBuf textBuf) {
    /* Round 7: opaque HUD strip deleted — hearts float over the wall art.
     * A soft two-band top gradient keeps the heart row readable against
     * bright wall tiles without boxing the HUD in. */
    C2D_DrawRectSolid(0, 0, 0, TOP_SCREEN_WIDTH, 10, C2D_Color32(0, 0, 0, 70));
    C2D_DrawRectSolid(0, 10, 0, TOP_SCREEN_WIDTH, 8, C2D_Color32(0, 0, 0, 30));

    /* Hearts – compact layout fitting within 16px tall HUD strip. Wraps to
       a second row (see wrap logic below) instead of hard-hiding extras,
       so raise the display cap enough for that to matter. */
    int maxHearts = g->player.stats.max_hp / 2;
    if (maxHearts > 12) maxHearts = 12;

    /* R8 #34: row math derives from the TOTAL displayed slots (red
       containers + soul + black), not just the red containers, so
       soul-heavy builds wrap exactly like red-heavy ones. All three
       heart loops below share this `wrapped` flag and 6-per-row grid.
       R8 #36: rows_used also drives the active-item box top offset.
       hudShift moves the rest of the left HUD stack only when a 3rd
       heart row is actually rendered. */
    int slotsSoul = 0, slotsBlack = 0;
    if (g->active_curse != CURSE_UNKNOWN) {
        slotsSoul = (g->player.soul_hp + 1) / 2;
        if (maxHearts + slotsSoul > 18) slotsSoul = 18 - maxHearts;
        if (slotsSoul < 0) slotsSoul = 0;
        slotsBlack = (g->player.black_hp + 1) / 2;
        if (maxHearts + slotsSoul + slotsBlack > 18)
            slotsBlack = 18 - maxHearts - slotsSoul;
        if (slotsBlack < 0) slotsBlack = 0;
    }
    int totalSlots = maxHearts + slotsSoul + slotsBlack;
    int wrapped = (totalSlots > 6);
    int rows_used = (totalSlots + 5) / 6;
    if (rows_used < 1 || g->active_curse == CURSE_UNKNOWN) rows_used = 1;
    float hudShift = (rows_used > 2) ? 14.0f : 0.0f;

    /* R8 #41: HUD heart damage jiggle — track previously displayed HP in
       statics; on a loss, the affected heart slot(s) get a ~12-frame
       eased scale pop (0.75 -> ~0.95 -> 0.75). Render-side only. */
    static int jig_prev_red = -1, jig_prev_soul = -1, jig_prev_black = -1;
    static int jig_timer = 0, jig_lo = 0, jig_hi = -1;
    {
        int curRed = g->player.hp;
        int curSoul = g->player.soul_hp;
        int curBlack = g->player.black_hp;
        if (jig_prev_red >= 0) {
            int lo = 9999, hi = -1;
            if (curRed < jig_prev_red) {
                int a = curRed / 2, b = (jig_prev_red - 1) / 2;
                if (a < lo) lo = a;
                if (b > hi) hi = b;
            }
            if (curSoul < jig_prev_soul) {
                int a = maxHearts + curSoul / 2;
                int b = maxHearts + (jig_prev_soul - 1) / 2;
                if (a < lo) lo = a;
                if (b > hi) hi = b;
            }
            if (curBlack < jig_prev_black) {
                int a = maxHearts + slotsSoul + curBlack / 2;
                int b = maxHearts + slotsSoul + (jig_prev_black - 1) / 2;
                if (a < lo) lo = a;
                if (b > hi) hi = b;
            }
            if (hi >= 0) { jig_timer = 12; jig_lo = lo; jig_hi = hi; }
        }
        jig_prev_red = curRed;
        jig_prev_soul = curSoul;
        jig_prev_black = curBlack;
    }
    float jigScale = 0.0f;
    if (jig_timer > 0) {
        float jt = 1.0f - (float)jig_timer / 12.0f;
        jigScale = 0.20f * sinf(jt * 3.14159265f);  /* eased 0->0.20->0 */
        jig_timer--;
    }

    /* Curse of the Unknown: hide the heart row entirely (render-only —
     * underlying HP state is untouched). Draw a single '?' where the
     * heart row would normally start so the player knows curse is active
     * but can't read their exact health. */
    if (g->active_curse == CURSE_UNKNOWN) {
        C2D_Text qt;
        gtext_parse(&qt, textBuf, "?");
        C2D_TextOptimize(&qt);
        C2D_DrawText(&qt, C2D_WithColor, 12, 6, 0, 0.55f, 0.55f,
                     C2D_Color32(220, 220, 220, 255));
    } else {
    for (int i = 0; i < maxHearts; i++) {
        /* Wrap to a second row instead of the old hard 8-heart cap, so
           high max-HP loadouts (Blood/Soul Hearts, Bag of Fish, etc.) all
           remain visible instead of silently vanishing off the HUD.
           (R8 #34: `wrapped` is total-slot based, computed above.) */
        int row = wrapped ? i / 6 : 0;
        int col = wrapped ? i % 6 : i;
        float hx = 12 + col * 15;
        float hy = 8 + row * 14;
        int hpForThisHeart = (g->player.hp - i * 2);

        if (g_sprites_loaded) {
            float hs = 0.75f; /* slightly smaller hearts to fit HUD */
            if (jigScale > 0.0f && i >= jig_lo && i <= jig_hi)
                hs += jigScale;   /* R8 #41 damage pop */
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
    }

    /* Soul hearts — drawn after the red heart containers, Rebirth-style.
       Continues the same row-wrap grid the red hearts use (6 per row) so
       they land next to the last red heart instead of overlapping it. */
    int soulShown = 0;
    if (g->active_curse != CURSE_UNKNOWN) {
        int soulUnits = g->player.soul_hp;
        int soulHearts = slotsSoul;   /* R8 #34: same math as row calc */
        for (int i = 0; i < soulHearts; i++) {
            int slot = maxHearts + i;
            int row = wrapped ? slot / 6 : 0;
            int col = wrapped ? slot % 6 : slot;
            float hx = 12 + col * 15;
            float hy = 8 + row * 14;
            int unitsForThis = soulUnits - i * 2;
            if (g_sprites_loaded) {
                float hs = 0.75f;
                if (jigScale > 0.0f && slot >= jig_lo && slot <= jig_hi)
                    hs += jigScale;   /* R8 #41 damage pop */
                spr_draw(sheet_ui_items,
                         (unitsForThis >= 2) ? ui_items_atlas_heart_soul_full_idx
                                             : ui_items_atlas_heart_soul_half_idx,
                         hx, hy, hs, hs);
            } else {
                draw_heart(hx, hy, (unitsForThis >= 2) ? 12 : 8,
                           C2D_Color32(120, 140, 255, 255));
            }
            soulShown++;
        }

        /* E5: black hearts continue the row after the soul hearts (they are
           consumed first, but Rebirth draws them in pickup order — after the
           red containers is the closest simple read). Half hearts draw the
           full sprite at reduced scale (no dedicated half-black art). */
        int blackUnits = g->player.black_hp;
        int blackHearts = slotsBlack;   /* R8 #34: same math as row calc */
        for (int i = 0; i < blackHearts; i++) {
            int slot = maxHearts + soulShown + i;
            int row = wrapped ? slot / 6 : 0;
            int col = wrapped ? slot % 6 : slot;
            float hx = 12 + col * 15;
            float hy = 8 + row * 14;
            int unitsForThis = blackUnits - i * 2;
            if (g_sprites_loaded) {
                float hs = (unitsForThis >= 2) ? 0.75f : 0.55f;
                if (jigScale > 0.0f && slot >= jig_lo && slot <= jig_hi)
                    hs += jigScale;   /* R8 #41 damage pop */
                spr_draw(sheet_ui_items, ui_items_atlas_heart_black_full_idx,
                         hx, hy, hs, hs);
            } else {
                draw_heart(hx, hy, (unitsForThis >= 2) ? 12 : 8,
                           C2D_Color32(40, 30, 50, 255));
            }
        }
    }

    /* Consumable counts (coin/bomb/key) — Rebirth layout: top-left column
       under the hearts / active-item box. Zero-padded bone-white text with
       a 1px black drop shadow so it reads over the room art. */
    {
        float cx = 12;
        float cy = WALL_THICKNESS + 34 + hudShift; /* R2: track heart rows */
        float iconSc = 0.45f;
        u32 boneWhite = C2D_Color32(232, 224, 208, 255);
        u32 shadowCol = C2D_Color32(0, 0, 0, 255);
        const int counts[3] = { g->player.coins, g->player.bombs, g->player.keys };

        for (int ci = 0; ci < 3; ci++) {
            float ly = cy + ci * 12;
            if (g_sprites_loaded) {
                int icon = (ci == 0) ? ui_items_atlas_item_coin_idx
                         : (ci == 1) ? ui_items_atlas_item_bomb_idx
                                     : ui_items_atlas_item_key_idx;
                spr_draw(sheet_ui_items, icon, cx, ly, iconSc, iconSc);
            } else if (ci == 0) {
                C2D_DrawCircleSolid(cx, ly, 0, 4, C2D_Color32(255, 215, 0, 255));
            } else if (ci == 1) {
                C2D_DrawCircleSolid(cx, ly, 0, 4, C2D_Color32(80, 80, 80, 255));
            } else {
                C2D_DrawRectSolid(cx - 3, ly - 5, 0, 6, 10, C2D_Color32(255, 215, 0, 255));
            }
            C2D_Text ct;
            char cb[8];
            snprintf(cb, sizeof(cb), "%02d", counts[ci]);
            gtext_parse(&ct, textBuf, cb);
            C2D_TextOptimize(&ct);
            C2D_DrawText(&ct, C2D_WithColor, cx + 10, ly - 4, 0, 0.42f, 0.42f,
                         shadowCol);
            C2D_DrawText(&ct, C2D_WithColor, cx + 9, ly - 5, 0, 0.42f, 0.42f,
                         boneWhite);
        }
    }

    /* ── Held pill / card slot plus trinket charm. Round 7: moved with the
       consumable counters into the Rebirth top-left column (they sit just
       below the coin/bomb/key stack); hint letters get a 1px black shadow
       for readability over the room. ── */
    {
        float px = 14, py = WALL_THICKNESS + 76 + hudShift; /* R2 */
        if (g->player.has_pill) {
            /* two-tone capsule */
            C2D_DrawCircleSolid(px - 3, py, 0, 3.5f, C2D_Color32(240, 240, 240, 255));
            C2D_DrawCircleSolid(px + 3, py, 0, 3.5f, C2D_Color32(220, 90, 160, 255));
            C2D_DrawRectSolid(px - 3, py - 3.5f, 0, 3, 7, C2D_Color32(240, 240, 240, 255));
            C2D_DrawRectSolid(px, py - 3.5f, 0, 3, 7, C2D_Color32(220, 90, 160, 255));
            C2D_Text lt;
            gtext_parse(&lt, textBuf, "L");
            C2D_TextOptimize(&lt);
            C2D_DrawText(&lt, C2D_WithColor, px + 10, py - 6, 0, 0.4f, 0.4f,
                         C2D_Color32(0, 0, 0, 220));
            C2D_DrawText(&lt, C2D_WithColor, px + 9, py - 7, 0, 0.4f, 0.4f,
                         C2D_Color32(232, 224, 208, 220));
        }
        if (g->player.has_card) {
            float ccx = px + 30;
            C2D_DrawRectSolid(ccx - 4, py - 6, 0, 8, 12, C2D_Color32(235, 225, 200, 255));
            C2D_DrawRectSolid(ccx - 3, py - 5, 0, 6, 10, C2D_Color32(150, 120, 60, 255));
            C2D_DrawRectSolid(ccx - 2, py - 4, 0, 4, 8, C2D_Color32(235, 225, 200, 255));
            C2D_Text rt;
            gtext_parse(&rt, textBuf, "R");
            C2D_TextOptimize(&rt);
            C2D_DrawText(&rt, C2D_WithColor, ccx + 8, py - 6, 0, 0.4f, 0.4f,
                         C2D_Color32(0, 0, 0, 220));
            C2D_DrawText(&rt, C2D_WithColor, ccx + 7, py - 7, 0, 0.4f, 0.4f,
                         C2D_Color32(232, 224, 208, 220));
        }
        /* Held trinket charm, right of the pill/card slots */
        if (g->player.trinket != TRINKET_NONE) {
            draw_trinket_icon(px + 48, py, g->player.trinket, 0.9f);
        }
        /* R8 (M3): golden key halves, right of the trinket slot — the two
           halves nest together when both are held. */
        if (g->player.has_key_piece_1 || g->player.has_key_piece_2) {
            int both = g->player.has_key_piece_1 &&
                       g->player.has_key_piece_2;
            u32 kc = C2D_Color32(232, 190, 80, 255);
            u32 kd = C2D_Color32(150, 112, 30, 255);
            for (int kp = 0; kp < 2; kp++) {
                int has = (kp == 0) ? g->player.has_key_piece_1
                                    : g->player.has_key_piece_2;
                if (!has) continue;
                float kx = px + 66.0f + (both ? kp * 5.0f : 0.0f);
                C2D_DrawCircleSolid(kx, py - 3, 0, 3.0f, kc);
                C2D_DrawCircleSolid(kx, py - 3, 0, 1.3f, kd);
                C2D_DrawRectSolid(kx - 1, py - 1, 0, 2, 8, kc);
                /* half-tooth faces the missing twin */
                C2D_DrawRectSolid((kp == 0) ? kx - 3 : kx + 1, py + 4,
                                  0, 2, 2, kc);
            }
        }

    }

    /* ── Active item box (charge-based, use with a touch-screen tap). A framed
       ~20x20 slot with the item icon inside and a segmented vertical charge bar
       down its left edge. Placed just below the heart strip, top-left. ── */
    if (g->player.active_item != ITEM_NONE) {
        Player *p = &g->player;
        float bx = 8;                    /* box left */
        /* R8 #36: box top derived from the heart rows actually rendered.
           Last row center y = 8 + (rows-1)*14, sprite half-height ~6, plus
           4px clearance and the 2px frame => 20 + (rows-1)*14. Never above
           the classic WALL_THICKNESS+4 resting spot. */
        float by = 20.0f + (float)(rows_used - 1) * 14.0f;
        if (by < WALL_THICKNESS + 4) by = WALL_THICKNESS + 4;
        float bs = 20;                   /* box size */
        int ready = (p->active_charge >= p->active_max_charge);

        /* Layered brown frame (Round 7); gold when ready with a gentle
           pulse ring behind the box (deterministic, gated on g->frame). */
        if (ready && ((g->frame >> 3) & 1)) {
            C2D_DrawCircleSolid(bx + bs / 2, by + bs / 2, 0, bs * 0.75f,
                                C2D_Color32(255, 220, 120, 40));
        }
        C2D_DrawRectSolid(bx - 2, by - 2, 0, bs + 4, bs + 4,
                          ready ? GOLD_CHARGE : BORDER_BROWN);
        C2D_DrawRectSolid(bx, by, 0, bs, bs, C2D_Color32(28, 24, 20, 200));

        /* Item icon inside */
        if (g_sprites_loaded) {
            spr_draw(sheet_ui_items, item_sprite_idx(p->active_item),
                     bx + bs * 0.5f, by + bs * 0.5f, 0.62f, 0.62f);
        }

        /* Segmented vertical charge bar down the left edge (Yum-Heart pip style) */
        int mx = p->active_max_charge;
        if (mx < 1) mx = 1;
        if (mx > 6) mx = 6;              /* clamp segment count for HUD sanity */
        float segGap = 1.0f;
        float segH = (bs - (mx - 1) * segGap) / (float)mx;
        for (int si = 0; si < mx; si++) {
            /* fill from bottom up */
            int filledFromBottom = p->active_charge;
            int segIndexFromBottom = mx - 1 - si;
            u32 sc = (segIndexFromBottom < filledFromBottom)
                     ? (ready ? GOLD_CHARGE : BLOOD)
                     : C2D_Color32(40, 34, 28, 220);
            C2D_DrawRectSolid(bx - 6, by + si * (segH + segGap), 0, 3, segH, sc);
        }
    }

    /* Challenge: Time Attack / Speed! countdown (top-right, red under 2:00) */
    if (g->challenge == 3 || g->challenge == 4) {
        int remain = 20 * 60 * 60 - g->play_time_frames;
        if (remain < 0) remain = 0;
        int rsec = remain / 60;
        char tbuf[16];
        snprintf(tbuf, sizeof(tbuf), "%d:%02d", rsec / 60, rsec % 60);
        C2D_Text tt;
        gtext_parse(&tt, textBuf, tbuf);
        C2D_TextOptimize(&tt);
        u32 tcol = (rsec < 120) ? C2D_Color32(255, 60, 60, 255)
                                : C2D_Color32(255, 230, 160, 255);
        C2D_DrawText(&tt, C2D_WithColor, TOP_SCREEN_WIDTH - 58, 2, 0,
                     0.45f, 0.45f, tcol);
    }

    /* Floor name and room type — Round 7: demoted to the bottom-left corner
       as a single faint line (the HUD strip is gone). */
    const FloorInfo *fi = get_floor_info(g->current_floor);
    Room *r = current_room(g);

    C2D_Text floorText;
    char floorBuf[48], roomBuf[32], hudLineBuf[96];
    const char *roomNames[] = { "???", "Start", "Room", "Treasure", "BOSS", "Exit",
                                "Shop", "Secret", "Curse", "DEVIL", "Angel", "Sacrifice",
                                "Boss Rush", "Arcade", "Library" };

    /* Floor display: show loop count in infinite mode */
    if (g->game_mode == MODE_INFINITE && g->infinite_loop > 0) {
        int display_floor = g->infinite_loop * MAX_FLOORS + g->current_floor + 1;
        snprintf(floorBuf, sizeof(floorBuf), "F%d %s", display_floor, fi->name);
    } else {
        snprintf(floorBuf, sizeof(floorBuf), "%s", fi->name);
    }
    snprintf(roomBuf, sizeof(roomBuf), "%s",
             r->type < 15 ? roomNames[r->type] : "???");
    snprintf(hudLineBuf, sizeof(hudLineBuf), "%s - %s", floorBuf, roomBuf);

    gtext_parse(&floorText, textBuf, hudLineBuf);
    C2D_TextOptimize(&floorText);
    C2D_DrawText(&floorText, C2D_WithColor, 10, TOP_SCREEN_HEIGHT - 12, 0,
                 0.34f, 0.34f, C2D_Color32(200, 190, 170, 160));

    /* Difficulty / Mode indicator — Round 7: shrunk and tucked under the
       top-right minimap frame. */
    {
        const char *diff_labels[] = { "EASY", "NORM", "HARD" };
        u32 diff_cols[] = {
            C2D_Color32(100, 220, 100, 140),  /* green */
            C2D_Color32(200, 200, 100, 140),  /* yellow */
            C2D_Color32(255, 80, 80, 140)     /* red */
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
        gtext_parse(&modeText, textBuf, mode_buf);
        C2D_TextOptimize(&modeText);
        u32 mc = diff_cols[g->difficulty < DIFF_COUNT ? g->difficulty : 1];
        /* Position derived from the minimap geometry below (5x5 grid,
           11x8 cells, 3px pad, 2px inset from the right edge). */
        float tagX = TOP_SCREEN_WIDTH - (DUNGEON_W * 11.0f + 6.0f) - 2 + 3;
        float tagY = WALL_THICKNESS + 2 + (DUNGEON_H * 8.0f + 6.0f) + 6;
        C2D_DrawText(&modeText, C2D_WithColor, tagX, tagY, 0,
                     0.28f, 0.28f, mc);
    }

    /* Active ability indicators – shifted left so they clear the minimap
       frame. R3: the Time Attack / Speed! countdown occupies x342-377 y2-13,
       so those challenges drop the dots to y16, below the timer. */
    float indX = 352;
    float indY = (g->challenge == 3 || g->challenge == 4) ? 16.0f : 8.0f;
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

        /* Parchment frame (Round 7): drop shadow, brown frame, aged-paper fill */
        C2D_DrawRectSolid(mX, mY, 0, mW + 4, mH + 4, C2D_Color32(0, 0, 0, 50));
        C2D_DrawRectSolid(mX - 2, mY - 2, 0, mW + 4, mH + 4, BORDER_BROWN);
        C2D_DrawRectSolid(mX, mY, 0, mW, mH, C2D_Color32(226, 213, 190, 215));

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
                        /* 1px faint-ink outline (Rebirth "unknown room") */
                        u32 oc = INK_FAINT;
                        C2D_DrawRectSolid(cx + 1, cy + 1, 0, mCellW - 2, 1, oc);
                        C2D_DrawRectSolid(cx + 1, cy + mCellH - 2, 0, mCellW - 2, 1, oc);
                        C2D_DrawRectSolid(cx + 1, cy + 1, 0, 1, mCellH - 2, oc);
                        C2D_DrawRectSolid(cx + mCellW - 2, cy + 1, 0, 1, mCellH - 2, oc);
                    }
                    continue;
                }

                /* Visited room — Round 7: uniform dark-ink cell on parchment;
                   the current room is bone-white. Type identity comes from
                   the micro-glyphs below. */
                u32 rmCol = isCurrent ? C2D_Color32(238, 228, 206, 255)
                                      : C2D_Color32(60, 48, 38, 230);

                C2D_DrawRectSolid(cx + 1, cy + 1, 0, mCellW - 2, mCellH - 2, rmCol);

                /* Door connections - thin lines between rooms */
                u32 doorCol = C2D_Color32(122, 99, 75, 160);   /* INK_FAINT @160 */
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
                    /* Skull icon: tiny blood dot */
                    C2D_DrawRectSolid(icx - 1, icy - 1, 0, 3, 2, BLOOD);
                } else if (rm->type == ROOM_TREASURE && !isCurrent) {
                    /* Star/sparkle */
                    C2D_DrawRectSolid(icx, icy - 1, 0, 1, 3, GOLD_CHARGE);
                    C2D_DrawRectSolid(icx - 1, icy, 0, 3, 1, GOLD_CHARGE);
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

                /* Current room marker - pulsing bone-white outline (keep the
                   pulse math; recolored for the parchment frame) */
                if (isCurrent) {
                    float pulse = sinf((float)g->frame * 0.15f) * 0.4f + 0.6f;
                    u8 a = (u8)(pulse * 255);
                    u32 markerCol = C2D_Color32(238, 228, 206, a);
                    /* Draw outline */
                    C2D_DrawRectSolid(cx, cy, 0, mCellW, 1, markerCol);
                    C2D_DrawRectSolid(cx, cy + mCellH - 1, 0, mCellW, 1, markerCol);
                    C2D_DrawRectSolid(cx, cy, 0, 1, mCellH, markerCol);
                    C2D_DrawRectSolid(cx + mCellW - 1, cy, 0, 1, mCellH, markerCol);
                    /* Dark center dot as player position (cell is bone-white now) */
                    C2D_DrawRectSolid(icx, icy, 0, 1, 1, C2D_Color32(60, 48, 38, 255));
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
            /* R8 #19: Gemini/Steven's killable second head counts toward
               the bar (head max is max_hp/3, mirroring init + hp scaling) */
            float bhp  = boss->hp;
            float bmax = boss->max_hp;
            if (boss->type == ENEMY_BOSS_GEMINI ||
                boss->type == ENEMY_BOSS_STEVEN) {
                bhp  += (float)boss->gemini_chp;
                bmax += boss->max_hp / 3.0f;
            }
            float hpPct = bhp / bmax;
            if (hpPct < 0) hpPct = 0;
            if (hpPct > 1) hpPct = 1;

            /* Pulsing effect when low */
            float pulse = 1.0f;
            if (hpPct < 0.25f) {
                pulse = 0.8f + sinf(g->frame * 0.2f) * 0.2f;
            }

            if (g_sprites_loaded && sheet_ui_items) {
                /* Rebirth-style framed bar sprite (128x16 art; the interior
                   trough spans source px x=32..103, rows 7-8). Drawn at
                   1.5x / 2.0y; fill rects are inset to that interior. */
                spr_draw(sheet_ui_items, ui_items_atlas_ui_boss_healthbar_idx,
                         barX, barY, 1.5f, 2.0f);
                float inX = barX - 48.0f;   /* (32-64)*1.5 */
                float inW = 108.0f;         /* 72*1.5 */
                float inY = barY - 2.0f;    /* (7-8)*2 */
                float inH = 4.0f;           /* 2px*2 */
                /* Cover the art's baked partial fill with the empty color */
                C2D_DrawRectSolid(inX, inY, 0, inW, inH,
                                  C2D_Color32(35, 8, 10, 255));
                C2D_DrawRectSolid(inX, inY, 0, inW * hpPct * pulse, inH,
                                  C2D_Color32(220, 30, 30, 255));
                C2D_DrawRectSolid(inX, inY, 0, inW * hpPct * pulse, 1,
                                  C2D_Color32(255, 120, 100, 200));
            } else {
                /* Procedural fallback bar — Round 7: brown frame, dark blood
                   trough, BLOOD fill with a 1px highlight (matches sprite path) */
                float barW = 160.0f;
                float barH = 8.0f;
                C2D_DrawRectSolid(barX - barW / 2 - 2, barY - barH / 2 - 2, 0,
                                  barW + 4, barH + 4, BORDER_BROWN);
                C2D_DrawRectSolid(barX - barW / 2, barY - barH / 2, 0,
                                  barW, barH, C2D_Color32(35, 8, 10, 255));
                C2D_DrawRectSolid(barX - barW / 2, barY - barH / 2, 0,
                                  barW * hpPct * pulse, barH, BLOOD);
                C2D_DrawRectSolid(barX - barW / 2, barY - barH / 2, 0,
                                  barW * hpPct * pulse, 1,
                                  C2D_Color32(255, 120, 100, 200));
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
            gtext_parse(&introText, textBuf, g->boss_name);
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

    /* Boss death explosion effect: blood-red bursts at the corpse */
    if (g->boss_death_anim > 0) {
        float t = (float)(60 - g->boss_death_anim) / 60.0f;
        int numExplosions = (int)(t * 8);
        for (int ex = 0; ex < numExplosions; ex++) {
            /* Deterministic pseudo-random positions based on frame */
            float ex_x = g->boss_death_x + sinf(ex * 2.7f + t * 5.0f) * 40.0f;
            float ex_y = g->boss_death_y + cosf(ex * 3.1f + t * 4.0f) * 28.0f;
            float radius = (1.0f - t) * 12.0f + 4.0f;
            u8 alpha = (u8)((1.0f - t) * 200);
            C2D_DrawCircleSolid(ex_x, ex_y, 0, radius,
                               C2D_Color32(190, 20, 20, alpha));
            C2D_DrawCircleSolid(ex_x, ex_y, 0, radius * 0.6f,
                               C2D_Color32(255, 80, 50, alpha));
        }
    }
}

/* ================================================================
 * Render: Room
 * ================================================================ */

/* Render one room of the dungeon grid. Parameterized by grid coords so the
   sliding room transition can draw the outgoing and incoming rooms in the
   same frame (each under its own C2D view translation). */
/* R8 #40: per-chapter wall palette — a subtle tint over the shared wall
 * art so each floor reads like its Rebirth chapter. Blends stay in the
 * 0.15-0.35 band so the stone detail survives; doors use half strength
 * so their identity colors stay readable. */
static const struct { u8 r, g, b; float blend; } wall_pal[10] = {
    { 150, 105,  60, 0.20f },  /* 0 Basement  — warm brown */
    { 160, 120,  80, 0.18f },  /* 1 Cellar    — dusty tan */
    { 110, 125, 145, 0.25f },  /* 2 Caves     — gray-blue */
    {  95, 105, 130, 0.28f },  /* 3 Catacombs — deeper gray-blue */
    {  55,  60,  75, 0.32f },  /* 4 Depths    — dark slate */
    { 175,  55,  60, 0.30f },  /* 5 Womb      — flesh red */
    {  25,  22,  40, 0.35f },  /* 6 Sheol     — near-black cold */
    { 215, 180,  95, 0.25f },  /* 7 Chest     — warm gold */
    /* R9 route alts (picked by env_wall_draw via g_floor_route): */
    { 236, 224, 186, 0.30f },  /* 8 Cathedral — pale stone / gold */
    {  38,  14,  18, 0.35f },  /* 9 Dark Room — near-black, red cast */
};

/* Top-left-anchored environment draw with the chapter tint applied.
 * blendScale < 1 softens the tint (doors). Also carries per-axis scale
 * so the last wall tile of a row can be clamped to the room edge
 * (R8 #38) instead of overhanging. */
static void env_wall_draw(int idx, float x, float y, float sx, float sy,
                          int fl, float blendScale) {
    if (!sheet_environment) return;
    C2D_Image img = C2D_SpriteSheetGetImage(sheet_environment, idx);
    if (!img.subtex) return;
    int pi = fl;
    if (pi < 0) pi = 0;
    if (pi > 7) pi = 7;
    /* R9 route alts: light floor 6 = Cathedral, dark floor 7 = Dark Room */
    if (pi == 6 && g_floor_route == 1) pi = 8;
    else if (pi == 7 && g_floor_route == 2) pi = 9;
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint,
                       C2D_Color32(wall_pal[pi].r, wall_pal[pi].g,
                                   wall_pal[pi].b, 255),
                       wall_pal[pi].blend * blendScale);
    C2D_DrawImageAt(img, x, y, 0, &tint, sx, sy);
}

/* R8 #42: warm pedestal spotlight — stacked translucent floor ellipses
 * plus a narrow upward light shaft built from two alpha-graded triangles
 * (alpha ~25 at the base fading to 0 at the top). Pure geometry, no art. */
static void render_pedestal_spotlight(float px, float py) {
    u32 baseCol = C2D_Color32(255, 225, 160, 25);
    u32 topCol  = C2D_Color32(255, 225, 160, 0);
    C2D_DrawTriangle(px - 9.0f, py + 2.0f, baseCol,
                     px + 9.0f, py + 2.0f, baseCol,
                     px + 15.0f, py - 62.0f, topCol, 0);
    C2D_DrawTriangle(px - 9.0f, py + 2.0f, baseCol,
                     px + 15.0f, py - 62.0f, topCol,
                     px - 15.0f, py - 62.0f, topCol, 0);
    /* Warm pool of light on the floor under the pedestal */
    C2D_DrawEllipseSolid(px - 24, py + 2, 0, 48, 14,
                         C2D_Color32(255, 215, 150, 18));
    C2D_DrawEllipseSolid(px - 17, py + 4, 0, 34, 10,
                         C2D_Color32(255, 220, 160, 24));
    C2D_DrawEllipseSolid(px - 10, py + 6, 0, 20, 6,
                         C2D_Color32(255, 230, 180, 30));
}

static void render_room_at(Game *g, int room_gx, int room_gy) {
    Room *r = &g->dungeon.rooms[room_gy][room_gx];
    int fl = g->current_floor;

    if (g_sprites_loaded) {
        /* === SPRITE-BASED ROOM RENDERING === */

        /* Floor tiles - pick variant based on room type / floor */
        int floorIdx = environment_atlas_env_floor_clean_idx;
        if (r->type == ROOM_BOSS) floorIdx = environment_atlas_env_floor_bloody_idx;
        else if (r->type == ROOM_SACRIFICE) floorIdx = environment_atlas_env_floor_bloody_idx;
        else if (r->type == ROOM_BOSSRUSH) floorIdx = environment_atlas_env_floor_bloody_idx;
        else if (fl >= 2) floorIdx = environment_atlas_env_floor_cracked_idx;

        /* Tile the floor with 32x32 sprites */
        for (float ty = ROOM_TOP; ty < ROOM_BOTTOM; ty += 32) {
            for (float tx = ROOM_LEFT; tx < ROOM_RIGHT; tx += 32) {
                spr_draw_at(sheet_environment, floorIdx, tx, ty, 1.0f, 1.0f);
            }
        }

        /* (b) Basement/Cellar warm-brown ambient grade (floors 0 & 1 only) */
        if (fl == 0 || fl == 1) {
            C2D_DrawRectSolid(ROOM_LEFT, ROOM_TOP, 0,
                              ROOM_RIGHT - ROOM_LEFT, ROOM_BOTTOM - ROOM_TOP,
                              C2D_Color32(110, 75, 40, 26));
        }

        /* Angel room: soft white/gold glow. Sacrifice room: dim red grade. */
        if (r->type == ROOM_ANGEL) {
            C2D_DrawRectSolid(ROOM_LEFT, ROOM_TOP, 0,
                              ROOM_RIGHT - ROOM_LEFT, ROOM_BOTTOM - ROOM_TOP,
                              C2D_Color32(255, 245, 200, 40));
        } else if (r->type == ROOM_SACRIFICE) {
            C2D_DrawRectSolid(ROOM_LEFT, ROOM_TOP, 0,
                              ROOM_RIGHT - ROOM_LEFT, ROOM_BOTTOM - ROOM_TOP,
                              C2D_Color32(120, 20, 20, 40));
        } else if (r->type == ROOM_BOSSRUSH) {
            C2D_DrawRectSolid(ROOM_LEFT, ROOM_TOP, 0,
                              ROOM_RIGHT - ROOM_LEFT, ROOM_BOTTOM - ROOM_TOP,
                              C2D_Color32(200, 90, 20, 40));
        } else if (r->type == ROOM_ARCADE) {
            C2D_DrawRectSolid(ROOM_LEFT, ROOM_TOP, 0,
                              ROOM_RIGHT - ROOM_LEFT, ROOM_BOTTOM - ROOM_TOP,
                              C2D_Color32(255, 100, 200, 35));
        } else if (r->type == ROOM_LIBRARY) {
            C2D_DrawRectSolid(ROOM_LEFT, ROOM_TOP, 0,
                              ROOM_RIGHT - ROOM_LEFT, ROOM_BOTTOM - ROOM_TOP,
                              C2D_Color32(140, 100, 220, 35));
        }

        /* (c) Grout grid: faint darker lines every 32px over the floor,
           clipped to the play rect so tiles don't read as one flat sheet. */
        {
            u32 groutCol = C2D_Color32(0, 0, 0, 40);
            for (float gx = ROOM_LEFT + 32; gx < ROOM_RIGHT; gx += 32)
                C2D_DrawRectSolid(gx, ROOM_TOP, 0, 1, ROOM_BOTTOM - ROOM_TOP, groutCol);
            for (float gy = ROOM_TOP + 32; gy < ROOM_BOTTOM; gy += 32)
                C2D_DrawRectSolid(ROOM_LEFT, gy, 0, ROOM_RIGHT - ROOM_LEFT, 1, groutCol);
        }

        /* Permanent blood stains — drawn right on top of the floor tiles,
           under everything else. Rooms stay gory between visits. */
        if (sheet_bullets) {
            for (int bdi = 0; bdi < MAX_BLOOD_DECALS; bdi++) {
                BloodDecal *bd = &r->decals[bdi];
                if (!bd->active) continue;
                spr_draw_rotated_alpha(sheet_bullets, bd->sprite_idx,
                                       bd->x, bd->y, bd->scale, bd->scale,
                                       bd->rotation, 0.75f);
            }
        }

        /* Damaging creep puddles: dark rim + brighter core, fading out over
           the last second. Hardcoded red for now (Gish tar / champion blood);
           parameterize with a CreepTile color field when green Pestilence
           creep lands. */
        for (int ci = 0; ci < MAX_CREEP; ci++) {
            CreepTile *cr = &g->creep[ci];
            if (!cr->active) continue;
            float ca = (float)cr->timer / 60.0f;
            if (ca > 1.0f) ca = 1.0f;
            float crr = cr->radius;
            C2D_DrawEllipseSolid(cr->x - crr, cr->y - crr * 0.6f, 0,
                                 crr * 2.0f, crr * 1.2f,
                                 C2D_Color32(110, 12, 12, (u8)(ca * 150)));
            C2D_DrawEllipseSolid(cr->x - crr * 0.65f, cr->y - crr * 0.4f, 0,
                                 crr * 1.3f, crr * 0.8f,
                                 C2D_Color32(190, 30, 25, (u8)(ca * 160)));
        }

        /* Walls - continuous directional wall tiles (64x24 horiz, 24x64 vert).
           R8 #38: the last tile of each row is clamped to the room edge via
           x/y scale instead of overhanging the screen / opposite wall.
           R8 #40: all wall art routes through the per-chapter tint. */
        {
            const float wallEndX = TOP_SCREEN_WIDTH - WALL_THICKNESS;
            for (float tx = WALL_THICKNESS; tx < wallEndX; tx += 64) {
                float sx = (tx + 64.0f > wallEndX) ? (wallEndX - tx) / 64.0f
                                                   : 1.0f;
                env_wall_draw(environment_atlas_env_wall_top_idx,
                              tx, ROOM_TOP - WALL_THICKNESS, sx, 1.0f, fl, 1.0f);
                env_wall_draw(environment_atlas_env_wall_bottom_idx,
                              tx, ROOM_BOTTOM, sx, 1.0f, fl, 1.0f);
            }
            for (float ty = ROOM_TOP; ty < ROOM_BOTTOM; ty += 64) {
                float sy = (ty + 64.0f > ROOM_BOTTOM)
                           ? ((float)ROOM_BOTTOM - ty) / 64.0f : 1.0f;
                env_wall_draw(environment_atlas_env_wall_left_idx,
                              0, ty, 1.0f, sy, fl, 1.0f);
                env_wall_draw(environment_atlas_env_wall_right_idx,
                              ROOM_RIGHT, ty, 1.0f, sy, fl, 1.0f);
            }
        }

        /* Corner tiles (24x24) */
        env_wall_draw(environment_atlas_env_corner_tl_idx,
                      0, ROOM_TOP - WALL_THICKNESS, 1.0f, 1.0f, fl, 1.0f);
        env_wall_draw(environment_atlas_env_corner_tr_idx,
                      ROOM_RIGHT, ROOM_TOP - WALL_THICKNESS, 1.0f, 1.0f, fl, 1.0f);
        env_wall_draw(environment_atlas_env_corner_bl_idx,
                      0, ROOM_BOTTOM, 1.0f, 1.0f, fl, 1.0f);
        env_wall_draw(environment_atlas_env_corner_br_idx,
                      ROOM_RIGHT, ROOM_BOTTOM, 1.0f, 1.0f, fl, 1.0f);

        /* R10 (C4) leftover #45: deterministic wall decor — 0-2 subtle
           sprites on the top wall, seeded from the room's grid coords so
           each room always dresses the same. Graffiti in Basement/Caves,
           blood smears in Depths/Womb/Sheol/Dark Room, carved stone in
           Cathedral/Chest. Drawn through the chapter tint at soft blend;
           the door gap in the wall center is kept clear. */
        {
            int decorIdx;
            if (fl <= 3)
                decorIdx = environment_atlas_env_wall_graffiti_idx;
            else if ((fl == 6 && g_floor_route == 1) ||
                     (fl == 7 && g_floor_route != 2))
                decorIdx = environment_atlas_env_stone_wall_idx; /* Cathedral/Chest */
            else
                decorIdx = environment_atlas_env_wall_blood_idx; /* Depths/Womb/Sheol/Dark */
            unsigned dseed = (unsigned)(room_gx * 73 + room_gy * 31 + fl * 7 + 5);
            int dcount = (int)(dseed % 3);   /* 0-2 per room */
            for (int di = 0; di < dcount; di++) {
                unsigned dh = (dseed + 17u * (unsigned)di) * 2654435761u;
                float span = 120.0f;
                float off  = (float)(dh % 997u) / 997.0f * span;
                float ddx  = (dh & 1u)
                           ? (WALL_THICKNESS + off)                       /* left half */
                           : (TOP_SCREEN_WIDTH / 2.0f + 22.0f + off);     /* right half */
                env_wall_draw(decorIdx, ddx, ROOM_TOP - WALL_THICKNESS,
                              0.75f, 0.75f, fl, 0.7f);
            }
        }

        /* (d) Wall bevel: 2px darker inner lip along the inside edge of the
           four walls to fake a recessed stone frame. */
        {
            u32 bevelCol = C2D_Color32(0, 0, 0, 120);
            C2D_DrawRectSolid(ROOM_LEFT, ROOM_TOP, 0,
                              ROOM_RIGHT - ROOM_LEFT, 2, bevelCol);          /* top */
            C2D_DrawRectSolid(ROOM_LEFT, ROOM_BOTTOM - 2, 0,
                              ROOM_RIGHT - ROOM_LEFT, 2, bevelCol);          /* bottom */
            C2D_DrawRectSolid(ROOM_LEFT, ROOM_TOP, 0,
                              2, ROOM_BOTTOM - ROOM_TOP, bevelCol);          /* left */
            C2D_DrawRectSolid(ROOM_RIGHT - 2, ROOM_TOP, 0,
                              2, ROOM_BOTTOM - ROOM_TOP, bevelCol);          /* right */
        }

        /* ── Doors ── sprite-based rendering with full type variants ── */
        float midX = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
        float midY = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
        Dungeon *dd = &g->dungeon;
        int rx = room_gx, ry = room_gy;

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
            } else if (neighbor_type == ROOM_BOSS ||
                       neighbor_type == ROOM_DEVIL || r->door_type[di] == 5) {
                spr_variant = 2; /* boss/devil (red with skull) */
            } else if (neighbor_type == ROOM_CURSE || r->door_type[di] == 4) {
                spr_variant = 4; /* curse (purple with spikes) */
            } else if (neighbor_type == ROOM_SHOP) {
                spr_variant = 5; /* shop (blue with coin) */
            } else if (r->door_locked[di] || neighbor_type == ROOM_TREASURE ||
                       neighbor_type == ROOM_ANGEL || r->door_type[di] == 6) {
                spr_variant = 1; /* treasure/locked (gold with lock) -- also angel (white/gold) */
            } else if (neighbor_type == ROOM_BOSSRUSH) {
                spr_variant = 2; /* boss rush door (reuse boss/devil red) */
            } else if (neighbor_type == ROOM_ARCADE || neighbor_type == ROOM_LIBRARY) {
                spr_variant = 5; /* arcade/library door (reuse shop blue) */
            } else {
                spr_variant = 0; /* normal/open */
            }

            /* R8 #40: half-strength chapter tint keeps doors readable */
            env_wall_draw(door_spr_arr[di][spr_variant],
                          door_pos[di][0], door_pos[di][1], 1.0f, 1.0f,
                          fl, 0.5f);

            /* Door center + covered wall rect (for identity overlays) */
            float dcx = door_pos[di][0] + DOOR_WIDTH / 2.0f;
            float dcy = door_pos[di][1] + WALL_THICKNESS / 2.0f;
            float drw = DOOR_WIDTH, drh = WALL_THICKNESS;
            if (di >= 2) { /* left/right doors: center on vertical span */
                dcx = door_pos[di][0] + WALL_THICKNESS / 2.0f;
                dcy = door_pos[di][1] + DOOR_WIDTH / 2.0f;
                drw = WALL_THICKNESS; drh = DOOR_WIDTH;
            }
            float dpulse = sinf((float)g->frame * 0.08f) * 0.35f + 0.65f;

            if (neighbor_type == ROOM_DEVIL) {
                /* Devil door: near-black wash + deep-red sigil */
                C2D_DrawRectSolid(door_pos[di][0], door_pos[di][1], 0, drw, drh,
                                  C2D_Color32(10, 4, 8, 180));
                C2D_DrawCircleSolid(dcx, dcy, 0, 9.0f,
                                    C2D_Color32(140, 5, 20, (int)(dpulse * 130)));
                C2D_DrawCircleSolid(dcx, dcy, 0, 5.0f,
                                    C2D_Color32(200, 20, 30, (int)(dpulse * 170)));
            } else if (neighbor_type == ROOM_ANGEL) {
                /* Angel door: white wash + soft yellow glow */
                C2D_DrawRectSolid(door_pos[di][0], door_pos[di][1], 0, drw, drh,
                                  C2D_Color32(255, 255, 255, 128));
                C2D_DrawCircleSolid(dcx, dcy, 0, 12.0f,
                                    C2D_Color32(255, 240, 150, (int)(dpulse * 60)));
                C2D_DrawCircleSolid(dcx, dcy, 0, 6.0f,
                                    C2D_Color32(255, 250, 200, (int)(dpulse * 100)));
            } else if (spr_variant == 2) {
                /* (e) Boss-door sigil: pulsing red glow at door center */
                C2D_DrawCircleSolid(dcx, dcy, 0, 9.0f,
                                    C2D_Color32(220, 30, 30, (int)(dpulse * 70)));
                C2D_DrawCircleSolid(dcx, dcy, 0, 5.0f,
                                    C2D_Color32(255, 80, 60, (int)(dpulse * 110)));
            }
        }

        /* R8 (M3): floor-7 golden door on the start room's top wall.
           Distinct gold frame; twin key-half sigils light up per collected
           piece (deterministic pulse from g->frame); reads as an open dark
           passage once used. Offset left if a real top door exists. */
        if (g->current_floor == 7 && r->type == ROOM_START) {
            float gdx = (ROOM_LEFT + ROOM_RIGHT) / 2.0f -
                        (r->doors[0] ? 80.0f : 0.0f);
            float dw = (float)DOOR_WIDTH;
            float dy = ROOM_TOP - 18.0f;
            u32 goldD = C2D_Color32(150, 112, 30, 255);
            u32 goldL = C2D_Color32(232, 190, 80, 255);
            float gpulse = 0.6f + 0.4f * sinf((float)g->frame * 0.06f);
            C2D_DrawRectSolid(gdx - dw / 2 - 3, dy - 3, 0, dw + 6, 24, goldD);
            C2D_DrawRectSolid(gdx - dw / 2, dy, 0, dw, 18, goldL);
            if (g->mega_created) {
                /* opened: dark passage down to Mega Satan */
                C2D_DrawRectSolid(gdx - dw / 2 + 4, dy + 3, 0, dw - 8, 15,
                                  C2D_Color32(20, 10, 10, 255));
            } else {
                /* sealed slab: center seam + twin key-half sigils */
                C2D_DrawRectSolid(gdx - 1, dy + 2, 0, 2, 16, goldD);
                for (int kp = 0; kp < 2; kp++) {
                    int has = kp == 0 ? g->player.has_key_piece_1
                                      : g->player.has_key_piece_2;
                    float kx = gdx + (kp == 0 ? -9.0f : 9.0f);
                    u32 sig = has
                        ? C2D_Color32(255, 240, 150,
                                      (int)(160 + 90 * gpulse))
                        : C2D_Color32(96, 70, 22, 255);
                    C2D_DrawCircleSolid(kx, dy + 7, 0, 3.2f, sig);
                    C2D_DrawRectSolid(kx - 1, dy + 7, 0, 2, 8, sig);
                    /* half-key notch faces the seam */
                    C2D_DrawRectSolid((kp == 0) ? kx : kx - 2, dy + 12,
                                      0, 2, 2, sig);
                }
            }
        }

        /* Obstacles - use rock sprite */
        for (int i = 0; i < r->obstacle_count; i++) {
            Obstacle *o = &r->obstacles[i];
            if (!o->active) continue;
            /* R8 (M3): angel statue — fully procedural pale stone figure
               with folded wings and gold accents (no atlas art). Slow
               deterministic halo shimmer from g->frame. */
            if (o->type == OBST_ANGEL_STATUE) {
                float ax = o->x, ay = o->y;
                float shimmer = 0.7f + 0.3f * sinf((float)g->frame * 0.05f);
                u32 stone     = C2D_Color32(226, 226, 234, 255);
                u32 stoneDark = C2D_Color32(178, 178, 192, 255);
                u32 gold      = C2D_Color32(214, 178, 84, 255);
                /* grounding shadow + plinth */
                C2D_DrawEllipseSolid(ax - 16, ay + 12, 0, 32, 10,
                                     C2D_Color32(0, 0, 0, 70));
                C2D_DrawRectSolid(ax - 14, ay + 6, 0, 28, 8, stoneDark);
                C2D_DrawRectSolid(ax - 11, ay + 4, 0, 22, 4, stone);
                /* folded wings (two mirrored arcs behind the body) */
                C2D_DrawEllipseSolid(ax - 22, ay - 22, 0, 16, 30, stoneDark);
                C2D_DrawEllipseSolid(ax + 6,  ay - 22, 0, 16, 30, stoneDark);
                C2D_DrawEllipseSolid(ax - 19, ay - 19, 0, 11, 24, stone);
                C2D_DrawEllipseSolid(ax + 8,  ay - 19, 0, 11, 24, stone);
                /* robed body */
                C2D_DrawEllipseSolid(ax - 8, ay - 14, 0, 16, 22, stone);
                C2D_DrawRectSolid(ax - 8, ay - 2, 0, 16, 8, stoneDark);
                /* head + gold halo */
                C2D_DrawCircleSolid(ax, ay - 18, 0, 6.0f, stone);
                C2D_DrawEllipseSolid(ax - 8, ay - 28, 0, 16, 5,
                                     C2D_Color32(214, 178, 84,
                                                 (int)(200 * shimmer)));
                /* gold sash accent */
                C2D_DrawRectSolid(ax - 7, ay - 8, 0, 14, 2, gold);
                continue;
            }
            /* Grounding drop shadow (rocks/poop sit on the floor; spikes are flush) */
            if (o->type != OBST_SPIKES)
                C2D_DrawEllipseSolid(o->x - OBSTACLE_SIZE * 0.55f, o->y + OBSTACLE_SIZE * 0.35f, 0,
                                     OBSTACLE_SIZE * 1.1f, OBSTACLE_SIZE * 0.4f,
                                     C2D_Color32(0, 0, 0, 70));
            int oidx = environment_atlas_env_rock_idx;
            float osc = OBSTACLE_SIZE / 32.0f;
            if (o->type == OBST_POOP) {
                oidx = (o->hp >= 3) ? environment_atlas_env_poop_1_idx
                     : (o->hp == 2) ? environment_atlas_env_poop_2_idx
                                    : environment_atlas_env_poop_3_idx;
                osc = OBSTACLE_SIZE / 16.0f;   /* poop/spike art is 16x16 */
            } else if (o->type == OBST_SPIKES) {
                oidx = environment_atlas_env_spikes_idx;
                osc = OBSTACLE_SIZE / 16.0f;
            } else if (o->type == OBST_SLOT_MACHINE) {
                /* No dedicated slot-machine sprite yet: reuse the gold chest
                   art so it reads as a distinct paid interactable. */
                oidx = environment_atlas_env_chest_gold_idx;
                osc = OBSTACLE_SIZE / 16.0f;
            }
            spr_draw(sheet_environment, oidx, o->x, o->y, osc, osc);
        }

        /* Pedestal */
        if (r->pedestal.active) {
            float px = r->pedestal.x;
            float py = r->pedestal.y;
            float t = (float)g->frame;

            /* R8 #42: warm spotlight shaft + floor glow under the item */
            render_pedestal_spotlight(px, py);

            /* Outer glow pulse */
            float pulse = sinf(t * 0.05f) * 0.3f + 0.7f;
            u32 glowOuter = C2D_Color32(255, 255, 180, (int)(pulse * 40));
            C2D_DrawCircleSolid(px, py, 0, 26, glowOuter);

            /* Inner glow */
            u32 glowInner = C2D_Color32(255, 255, 200, (int)(pulse * 80));
            C2D_DrawCircleSolid(px, py, 0, 18, glowInner);

            /* Sparkle glints: 1-2 white crosses that blink for 2 frames
               every ~40, offset phases (Rebirth pedestal shimmer) */
            for (int si = 0; si < 2; si++) {
                int phase = ((int)t + si * 23) % 40;
                if (phase < 2) {
                    float sxp = px + (si ? -10.0f : 8.0f);
                    float syp = py + (si ? -2.0f : -14.0f);
                    u32 spc = C2D_Color32(255, 255, 255, 230);
                    C2D_DrawRectSolid(sxp - 3.0f, syp - 0.5f, 0, 6, 1, spc);
                    C2D_DrawRectSolid(sxp - 0.5f, syp - 3.0f, 0, 1, 6, spc);
                }
            }

            /* Pedestal base - use ui_item_pickup sprite */
            spr_draw(sheet_ui_items, ui_items_atlas_ui_item_pickup_idx,
                     px, py + 5, 1.0f, 1.0f);

            /* R8 #42: slow bob in the spotlight + subtle scale pulse */
            float scPulse = 1.0f + sinf(t * 0.08f) * 0.05f;
            float bobP = sinf(t * 0.05f) * 2.0f;
            if (g->active_curse == CURSE_BLIND) {
                /* Draw a question mark instead */
                float qx = px;
                float qy = py - 6;
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
                spr_draw(sheet_ui_items, itemIdx, px, py - 6 + bobP,
                         scPulse, scPulse);
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
            } else if (h->type == HEART_BLACK) {
                heart_idx = ui_items_atlas_heart_black_full_idx;   /* E5 */
            } else {
                heart_idx = ui_items_atlas_heart_soul_full_idx;
            }
            
            /* Grounding shadow so the bobbing heart doesn't float */
            C2D_DrawEllipseSolid(h->x - 5, h->y + 6, 0, 10, 3,
                                 C2D_Color32(0, 0, 0, 70));
            /* Heartbeat pulse: sharp cubed-sine thump instead of a bob */
            float hb = sinf((float)h->anim_timer * 0.1f);
            float hbs = 1.0f + 0.10f * ((hb > 0.0f) ? hb * hb * hb : 0.0f);
            spr_draw(sheet_ui_items, heart_idx, h->x, h->y + bob_offset * 0.3f,
                     hbs, hbs);
        }

        /* Consumable pickups (bombs, keys, coins, pills, cards) */
        for (int i = 0; i < MAX_CONSUMABLE_PICKUPS; i++) {
            ConsumablePickup *c = &r->consumables[i];
            if (!c->active) continue;
            c->anim_timer++;
            float bob_off = sinf((float)c->anim_timer * CONSUMABLE_BOB_SPEED) * CONSUMABLE_BOB_AMP;
            /* Grounding shadow under every pickup */
            C2D_DrawEllipseSolid(c->x - 5, c->y + 5, 0, 10, 3,
                                 C2D_Color32(0, 0, 0, 70));
            /* Chests: grounded (no bob), drawn from the environment atlas */
            if (c->type == PICKUP_CHEST || c->type == PICKUP_CHEST_GOLD) {
                int chIdx = (c->type == PICKUP_CHEST_GOLD)
                          ? environment_atlas_env_chest_gold_idx
                          : environment_atlas_env_chest_wood_idx;
                spr_draw(sheet_environment, chIdx, c->x, c->y, 1.2f, 1.2f);
                continue;
            }
            /* R8 (M5): red chest — procedural, dark red with gold trim.
               Deterministic per-slot jitter; closed vs opened-husk states. */
            if (c->type == PICKUP_CHEST_RED) {
                float jx = (float)((i * 7) % 3) - 1.0f;   /* -1..1 wiggle */
                float rx = c->x + jx, ry = c->y;
                int opened = (c->sub_type == 1);
                u32 body  = C2D_Color32(122, 24, 28, 255);
                u32 shade = C2D_Color32(84, 14, 20, 255);
                u32 trim  = C2D_Color32(212, 168, 64, 255);
                if (opened) {
                    /* lid flipped up behind the box, dark open interior */
                    C2D_DrawRectSolid(rx - 9, ry - 14, 0, 18, 5, shade);
                    C2D_DrawRectSolid(rx - 8, ry - 13, 0, 16, 3,
                                      C2D_Color32(150, 40, 40, 255));
                    C2D_DrawRectSolid(rx - 9, ry - 6, 0, 18, 11, body);
                    C2D_DrawRectSolid(rx - 7, ry - 6, 0, 14, 4,
                                      C2D_Color32(20, 8, 10, 255));
                    /* gold trim band */
                    C2D_DrawRectSolid(rx - 9, ry + 1, 0, 18, 2, trim);
                } else {
                    /* closed: domed lid + body + trim + lock */
                    C2D_DrawRectSolid(rx - 9, ry - 9, 0, 18, 5, shade);
                    C2D_DrawRectSolid(rx - 8, ry - 10, 0, 16, 2,
                                      C2D_Color32(150, 40, 40, 255));
                    C2D_DrawRectSolid(rx - 9, ry - 4, 0, 18, 9, body);
                    C2D_DrawRectSolid(rx - 9, ry - 5, 0, 18, 2, trim);
                    C2D_DrawRectSolid(rx - 2, ry - 6, 0, 4, 5, trim);
                    C2D_DrawRectSolid(rx - 1, ry - 4, 0, 2, 2, shade);
                    /* faint pulsing evil glow */
                    float rg = 0.5f + 0.5f * sinf((float)c->anim_timer * 0.07f);
                    C2D_DrawCircleSolid(rx, ry, 0, 13,
                                        C2D_Color32(200, 30, 30,
                                                    (int)(22 + 18 * rg)));
                }
                continue;
            }
            if (c->type == PICKUP_TRINKET) {
                /* grounding shadow + bobbing charm */
                C2D_DrawEllipseSolid(c->x - 5, c->y + 6, 0, 10, 3,
                                     C2D_Color32(0, 0, 0, 70));
                draw_trinket_icon(c->x, c->y + bob_off, c->sub_type, 1.0f);
                continue;
            }
            /* Pills and cards drawn as colored shapes (no atlas yet) */
            if (c->type == PICKUP_PILL) {
                /* Pill: two-tone capsule. Color from scrambled map per run.
                   Order matches pill_color_names[] exactly so the pickup
                   message always names the color actually drawn. */
                static const u32 pcols[PILL_EFFECT_COUNT] = {
                    0xFF5050D0, /* red */     0xFFE07050, /* blue */
                    0xFF60E0E0, /* yellow */  0xFF70C850, /* green */
                    0xFF2080E0, /* orange */  0xFFC0A0F0, /* pink */
                    0xFFFFFFFF, /* white */   0xFF303030, /* black */
                    0xFFE05080, /* purple */  0xFFD0D040, /* cyan */
                    0xFF3060A0, /* brown */   0xFFA0A0A0, /* grey */
                    0xFFE040E0  /* magenta */
                };
                int colidx = g->pill_color_map[c->sub_type % PILL_EFFECT_COUNT] % PILL_EFFECT_COUNT;
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
            if (c->type == PICKUP_BATTERY) {
                /* Battery: small green rect */
                float by = c->y + bob_off;
                C2D_DrawRectSolid(c->x - 4, by - 6, 0, 8, 12, 0xFF208020);
                C2D_DrawRectSolid(c->x - 2, by - 8, 0, 4, 2, 0xFF60E060);
                continue;
            }
            int cidx;
            switch (c->type) {
                case PICKUP_BOMB:  case PICKUP_BOMB2: cidx = ui_items_atlas_item_bomb_idx; break;
                case PICKUP_KEY:   case PICKUP_KEY5:  cidx = ui_items_atlas_item_key_idx; break;
                case PICKUP_COIN:  case PICKUP_COIN5: cidx = ui_items_atlas_item_coin_idx; break;
                default: cidx = ui_items_atlas_item_coin_idx; break;
            }
            float csc = (c->type == PICKUP_COIN5 || c->type == PICKUP_BOMB2 ||
                         c->type == PICKUP_KEY5) ? 1.2f : 0.9f;
            if (c->type == PICKUP_COIN || c->type == PICKUP_COIN5) {
                /* Fake Y-axis spin: width follows cos, mirrored on the
                   far side of the turn; keys/bombs keep the plain bob */
                float spin = cosf((float)c->anim_timer * 0.15f);
                float sw = csc * fabsf(spin);
                if (sw < 0.15f) sw = 0.15f;   /* never vanish edge-on */
                if (spin < 0)
                    spr_draw_fliph(sheet_ui_items, cidx, c->x, c->y + bob_off, sw, csc);
                else
                    spr_draw(sheet_ui_items, cidx, c->x, c->y + bob_off, sw, csc);
            } else {
                spr_draw(sheet_ui_items, cidx, c->x, c->y + bob_off, csc, csc);
            }
        }

        /* Shop items (item on pedestal + price, or sold-out indicator) */
        for (int i = 0; i < r->shop_count; i++) {
            ShopItem *si = &r->shop_items[i];
            if (si->active) {
                float bob_s = sinf((float)g->frame * 0.06f + (float)i) * 2.0f;
                int sIdx = item_sprite_idx(si->item);
                /* R8 #42: spotlight on shop/devil pedestals too */
                render_pedestal_spotlight(si->x, si->y);
                /* Pedestal */
                spr_draw(sheet_ui_items, ui_items_atlas_ui_item_pickup_idx,
                         si->x, si->y + 5, 0.8f, 0.8f);
                /* Item bobbing on pedestal */
                spr_draw(sheet_ui_items, sIdx, si->x, si->y - 4 + bob_s, 0.9f, 0.9f);
                /* Price tag: coin icon + number drawn via render_shop_prices
                   (devil rooms show heart prices instead of coins) */
                if (r->type != ROOM_DEVIL) {
                    spr_draw(sheet_ui_items, ui_items_atlas_item_coin_idx,
                             si->x - 8, si->y + 16, 0.5f, 0.5f);
                }
            } else {
                /* Sold-out: dim empty pedestal */
                spr_draw(sheet_ui_items, ui_items_atlas_ui_item_pickup_idx,
                         si->x, si->y + 5, 0.7f, 0.7f);
                /* Dark overlay to show sold */
                C2D_DrawRectSolid(si->x - 10, si->y, 0, 20, 12,
                                  C2D_Color32(0, 0, 0, 100));
            }
        }

        /* Active bombs (flashing before explosion) */
        for (int bi = 0; bi < MAX_BOMBS; bi++) {
            ActiveBomb *b = &g->bombs[bi];
            if (!b->active) continue;
            int flash = (b->flash / 4) % 2;
            float bsc = 1.0f + (flash ? 0.15f : 0.0f);
            spr_draw(sheet_ui_items, ui_items_atlas_item_bomb_idx,
                     b->x, b->y, bsc, bsc);
            /* Warning circle when close to exploding */
            if (b->timer < 30) {
                u32 warnCol = C2D_Color32(255, 100, 50, (int)(80 + flash * 60));
                C2D_DrawCircleSolid(b->x, b->y, 0, 48.0f * (1.0f - (float)b->timer / 30.0f), warnCol);
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

        /* E2: end-run "beam of light" (Ending 1) beside the trapdoor.
           R8 #24: hidden until the room is cleared (matches the trigger). */
        if (r->has_ending_beam && r->cleared) {
            float ebx = (ROOM_LEFT + ROOM_RIGHT) / 2.0f + 60.0f;
            float eby = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
            float bp = 0.75f + 0.25f * sinf((float)g->frame * 0.08f);
            C2D_DrawRectSolid(ebx - 10, ROOM_TOP, 0, 20, eby - ROOM_TOP,
                              C2D_Color32(255, 250, 210, (int)(60 * bp)));
            C2D_DrawCircleSolid(ebx, eby, 0, 16,
                                C2D_Color32(255, 250, 210, (int)(120 * bp)));
            C2D_DrawCircleSolid(ebx, eby, 0, 9,
                                C2D_Color32(255, 255, 240, 230));
        }

    } else {
        /* === FALLBACK: PROCEDURAL RENDERING (original code) === */
        u32 floorCol = COL_FLOOR;
        if (fl == 0) floorCol = C2D_Color32(140, 115, 90, 255);
        else if (fl == 1) floorCol = C2D_Color32(130, 110, 85, 255);
        else if (fl == 2) floorCol = C2D_Color32(100, 100, 110, 255);
        else if (fl == 3) floorCol = C2D_Color32(90, 90, 105, 255);
        else if (fl == 6 && g_floor_route == 1)
            floorCol = C2D_Color32(205, 195, 165, 255);  /* Cathedral stone */
        else if (fl == 7 && g_floor_route == 2)
            floorCol = C2D_Color32(45, 32, 36, 255);     /* Dark Room */
        else floorCol = C2D_Color32(80, 70, 80, 255);

        if (r->type == ROOM_BOSS) floorCol = C2D_Color32(120, 70, 70, 255);
        else if (r->type == ROOM_TREASURE) floorCol = C2D_Color32(140, 130, 90, 255);
        else if (r->type == ROOM_SHOP) floorCol = C2D_Color32(90, 120, 140, 255);
        else if (r->type == ROOM_SECRET) floorCol = C2D_Color32(100, 100, 100, 255);
        else if (r->type == ROOM_CURSE) floorCol = C2D_Color32(110, 60, 90, 255);
        else if (r->type == ROOM_DEVIL) floorCol = C2D_Color32(90, 30, 30, 255);
        else if (r->type == ROOM_ANGEL) floorCol = C2D_Color32(200, 190, 150, 255);
        else if (r->type == ROOM_SACRIFICE) floorCol = C2D_Color32(100, 40, 40, 255);
        else if (r->type == ROOM_BOSSRUSH) floorCol = C2D_Color32(150, 70, 20, 255);
        else if (r->type == ROOM_ARCADE) floorCol = C2D_Color32(150, 70, 120, 255);
        else if (r->type == ROOM_LIBRARY) floorCol = C2D_Color32(90, 70, 130, 255);

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
        int rx_fb = room_gx, ry_fb = room_gy;
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
                else if (ntype == ROOM_BOSS || ntype == ROOM_DEVIL)
                    doorCol = C2D_Color32(200, 60, 60, 255);   /* red */
                else if (ntype == ROOM_CURSE)
                    doorCol = C2D_Color32(160, 50, 140, 255);  /* purple */
                else if (ntype == ROOM_SHOP)
                    doorCol = C2D_Color32(60, 180, 60, 255);   /* green */
                else if (ntype == ROOM_ANGEL)
                    doorCol = C2D_Color32(255, 245, 200, 255); /* white/gold */
                else if (ntype == ROOM_SACRIFICE)
                    doorCol = C2D_Color32(180, 40, 40, 255);   /* dark red */
                else if (ntype == ROOM_BOSSRUSH)
                    doorCol = C2D_Color32(230, 100, 30, 255);  /* orange */
                else if (ntype == ROOM_ARCADE)
                    doorCol = C2D_Color32(255, 100, 200, 255); /* pink */
                else if (ntype == ROOM_LIBRARY)
                    doorCol = C2D_Color32(140, 100, 220, 255); /* purple */
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
            /* R8 #42: spotlight on the fallback pedestal too */
            render_pedestal_spotlight(px, py);
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
            if (c->type == PICKUP_BATTERY) {
                /* Battery: small green rect */
                C2D_DrawRectSolid(c->x - 4, c->y + bob_off - 6, 0, 8, 12, 0xFF208020);
                C2D_DrawRectSolid(c->x - 2, c->y + bob_off - 8, 0, 4, 2, 0xFF60E060);
                continue;
            }
            if (c->type == PICKUP_CHEST_RED) {
                /* R8 (M5) fallback: compact red chest w/ gold band */
                C2D_DrawRectSolid(c->x - 7, c->y - 6, 0, 14, 11,
                                  C2D_Color32(122, 24, 28, 255));
                C2D_DrawRectSolid(c->x - 7, c->y - 2, 0, 14, 2,
                                  C2D_Color32(212, 168, 64, 255));
                continue;
            }
            u32 ccol;
            switch (c->type) {
                case PICKUP_BOMB:  case PICKUP_BOMB2: ccol = C2D_Color32(80, 80, 80, 255); break;
                case PICKUP_KEY:   case PICKUP_KEY5:  ccol = C2D_Color32(255, 215, 0, 255); break;
                default:           ccol = C2D_Color32(255, 200, 50, 255); break;
            }
            float csz = (c->type == PICKUP_COIN5 || c->type == PICKUP_BOMB2 ||
                         c->type == PICKUP_KEY5) ? 7 : 5;
            C2D_DrawCircleSolid(c->x, c->y + bob_off, 0, csz, ccol);
        }

        /* Shop items (fallback) */
        for (int i = 0; i < r->shop_count; i++) {
            ShopItem *si = &r->shop_items[i];
            if (si->active) {
                float bob_s = sinf((float)g->frame * 0.06f + (float)i) * 2.0f;
                /* R8 #42: spotlight on fallback shop pedestals */
                render_pedestal_spotlight(si->x, si->y);
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

        /* Active bombs (fallback) */
        for (int bi = 0; bi < MAX_BOMBS; bi++) {
            ActiveBomb *b = &g->bombs[bi];
            if (!b->active) continue;
            int flash = (b->flash / 4) % 2;
            u32 bc = flash ? C2D_Color32(255, 100, 50, 255) : C2D_Color32(80, 80, 80, 255);
            C2D_DrawCircleSolid(b->x, b->y, 0, 8, bc);
        }

        /* Trapdoor */
        if (r->has_trapdoor) {
            float cx = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
            float cy = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
            C2D_DrawCircleSolid(cx, cy, 0, 16, COL_TRAPDOOR);
            C2D_DrawCircleSolid(cx, cy, 0, 12, C2D_Color32(20, 15, 10, 255));
            C2D_DrawCircleSolid(cx, cy, 0, 6, C2D_Color32(0, 0, 0, 255));
        }

        /* E2: end-run "beam of light" (Ending 1), procedural fallback.
           R8 #24: hidden until the room is cleared (matches the trigger). */
        if (r->has_ending_beam && r->cleared) {
            float ebx = (ROOM_LEFT + ROOM_RIGHT) / 2.0f + 60.0f;
            float eby = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
            float bp = 0.75f + 0.25f * sinf((float)g->frame * 0.08f);
            C2D_DrawRectSolid(ebx - 10, ROOM_TOP, 0, 20, eby - ROOM_TOP,
                              C2D_Color32(255, 250, 210, (int)(60 * bp)));
            C2D_DrawCircleSolid(ebx, eby, 0, 16,
                                C2D_Color32(255, 250, 210, (int)(120 * bp)));
            C2D_DrawCircleSolid(ebx, eby, 0, 9,
                                C2D_Color32(255, 255, 240, 230));
        }
    }
}

/* ================================================================
 * Render: Game objects
 * ================================================================ */

/* Render the room the player is currently in */
static void render_room(Game *g) {
    render_room_at(g, g->dungeon.cur_x, g->dungeon.cur_y);
}

/* R8 #37: per-floor ambient tint + standing vignette, shared by the
   normal PLAYING render and the PAUSED underlay so pausing doesn't
   cause a visible color pop. */
static void render_ambient_and_vignette(Game *g) {
    /* Per-floor ambient tint (Rebirth's chapters each have a palette).
       Clipped to the play rect (ROOM_TOP..ROOM_BOTTOM) so it dyes the
       room, not the HUD. Floors 0/1 (Basement/Cellar) get a subtle
       warm-brown grade of their own. */
    {
        float pty = ROOM_TOP, pth = ROOM_BOTTOM - ROOM_TOP;
        if (g->current_floor == 0 || g->current_floor == 1) {
            /* Basement/Cellar: subtle warm brown */
            C2D_DrawRectSolid(0, pty, 0, TOP_SCREEN_WIDTH, pth,
                              C2D_Color32(120, 85, 45, 24));
        } else if (g->current_floor == 2 || g->current_floor == 3) {
            /* Caves: warm earthy brown */
            C2D_DrawRectSolid(0, pty, 0, TOP_SCREEN_WIDTH, pth,
                              C2D_Color32(120, 85, 40, 28));
        } else if (g->current_floor == 4) {
            /* Depths: cold near-black gray */
            C2D_DrawRectSolid(0, pty, 0, TOP_SCREEN_WIDTH, pth,
                              C2D_Color32(25, 25, 35, 55));
        } else if (g->current_floor == 5) {
            /* Womb: red flesh tint */
            C2D_DrawRectSolid(0, pty, 0, TOP_SCREEN_WIDTH, pth,
                              C2D_Color32(140, 30, 40, 50));
        } else if (g->current_floor == 6) {
            if (g->route == 1) {
                /* Cathedral (light route): pale holy gold wash */
                C2D_DrawRectSolid(0, pty, 0, TOP_SCREEN_WIDTH, pth,
                                  C2D_Color32(240, 225, 175, 42));
            } else {
                /* Sheol: dark hellish tint */
                C2D_DrawRectSolid(0, pty, 0, TOP_SCREEN_WIDTH, pth,
                                  C2D_Color32(20, 0, 0, 90));
            }
        } else if (g->current_floor == 7) {
            if (g->route == 2) {
                /* Dark Room (dark route): near-black with a red accent */
                C2D_DrawRectSolid(0, pty, 0, TOP_SCREEN_WIDTH, pth,
                                  C2D_Color32(35, 0, 6, 100));
            } else {
                /* The Chest: deep gold/white tint */
                C2D_DrawRectSolid(0, pty, 0, TOP_SCREEN_WIDTH, pth,
                                  C2D_Color32(90, 80, 40, 70));
            }
        }
    }

    /* Rebirth-style vignette: soft dark edges all the time, heavier on
       deeper floors. Three nested edge bands fake a radial falloff. */
    {
        int va = 26 + g->current_floor * 5;
        if (va > 60) va = 60;
        for (int vi = 0; vi < 3; vi++) {
            int band = 10 + vi * 12;
            int a = va / (vi + 1);
            u32 vc = C2D_Color32(0, 0, 0, a);
            C2D_DrawRectSolid(0, 0, 0, TOP_SCREEN_WIDTH, band, vc);
            C2D_DrawRectSolid(0, TOP_SCREEN_HEIGHT - band, 0,
                              TOP_SCREEN_WIDTH, band, vc);
            C2D_DrawRectSolid(0, band, 0, band,
                              TOP_SCREEN_HEIGHT - band * 2, vc);
            C2D_DrawRectSolid(TOP_SCREEN_WIDTH - band, band, 0, band,
                              TOP_SCREEN_HEIGHT - band * 2, vc);
        }

        /* (f) Radial corner vignette: stamp four darker ellipses at the
           room corners so corners read darkest (more radial falloff). */
        u32 corner_vc = C2D_Color32(0, 0, 0, va);
        float ew = 70.0f, eh = 55.0f;
        C2D_DrawEllipseSolid(0 - ew * 0.4f, 0 - eh * 0.4f, 0,
                             ew, eh, corner_vc);
        C2D_DrawEllipseSolid(TOP_SCREEN_WIDTH - ew * 0.6f, 0 - eh * 0.4f, 0,
                             ew, eh, corner_vc);
        C2D_DrawEllipseSolid(0 - ew * 0.4f, TOP_SCREEN_HEIGHT - eh * 0.6f, 0,
                             ew, eh, corner_vc);
        C2D_DrawEllipseSolid(TOP_SCREEN_WIDTH - ew * 0.6f,
                             TOP_SCREEN_HEIGHT - eh * 0.6f, 0,
                             ew, eh, corner_vc);
    }
}

static void render_player(Game *g) {
    Player *p = &g->player;

    /* Shadow under player */
    C2D_DrawEllipseSolid(p->x - PLAYER_SIZE, p->y + PLAYER_SIZE * 0.4f, 0,
                         PLAYER_SIZE * 2, PLAYER_SIZE * 0.6f, COL_SHADOW);

    /* D1 Brimstone charge-up: dark red glow that tightens and deepens as the
       charge builds. Smooth ease, no strobing (photosensitivity-safe). */
    if (g->laser_charge > 0) {
        float cf = (float)g->laser_charge / 15.0f;
        if (cf > 1.0f) cf = 1.0f;
        C2D_DrawCircleSolid(p->x, p->y, 0, PLAYER_SIZE + 7.0f - cf * 4.0f,
                            C2D_Color32(200, 30, 30, (int)(30 + cf * 70)));
    }

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

        /* Game-feel walk-bob: gentle vertical bounce while moving (idle: none) */
        float bob = p->moving ? fabsf(sinf(p->anim_timer * 0.35f)) * 2.0f : 0.0f;
        float draw_y = p->y - bob;

        if (p->iframes > PLAYER_IFRAMES - 8) {
            /* Red hurt tint only for the first 8 frames of the iframe
               window; after that the blink alone reads as invulnerable */
            float intensity = (float)(p->iframes - (PLAYER_IFRAMES - 8)) / 8.0f;
            spr_draw_tinted(sheet_sprites, idx, p->x, draw_y, scale, scale,
                           C2D_Color32(255, 60, 60, 255), 0.4f + intensity * 0.4f);
        } else if (p->character != CHAR_ISAAC) {
            /* Character identity tint (???/The Lost read paler, stronger) */
            float ti = (p->character == CHAR_BLUE_BABY ||
                        p->character == CHAR_LOST) ? 0.45f : 0.30f;
            u32 ctint = character_tint(p->character);
            /* R10 (C4) Samson Bloody Lust: subtle red shift as stacks grow */
            if (p->character == CHAR_SAMSON && p->samson_hits > 0) {
                float bl = (float)p->samson_hits / 7.0f;
                if (bl > 1.0f) bl = 1.0f;
                ctint = C2D_Color32(150 + (int)(60.0f * bl),
                                    90  - (int)(45.0f * bl),
                                    50  - (int)(25.0f * bl), 255);
                ti = 0.30f + 0.12f * bl;
            }
            spr_draw_tinted(sheet_sprites, idx, p->x, draw_y, scale, scale,
                           ctint, ti);
        } else {
            spr_draw(sheet_sprites, idx, p->x, draw_y, scale, scale);
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

/* D1/D2: layered beam from the player to the (possibly bent) endpoint.
 * Photosensitivity-safe: constant intensity with a smooth ease-out fade on
 * the final frames — never a strobe. */
static void render_laser(Game *g) {
    if (!g->laser_active) return;
    Player *p = &g->player;
    float ox = p->x;
    float oy = p->y - PLAYER_SIZE * 0.3f;
    float ex = g->laser_ex, ey = g->laser_ey;
    float fade = 1.0f;
    if (!g->laser_is_tech && g->laser_timer < 4)
        fade = (float)g->laser_timer / 4.0f;   /* Brimstone tail ease-out */
    int a = (int)(255.0f * fade);

    if (g->laser_is_tech) {
        /* Technology: thin tech beam — red sheath, 1px white core */
        u32 outer = C2D_Color32(220, 40, 40, (int)(200.0f * fade));
        u32 core  = C2D_Color32(255, 255, 255, a);
        C2D_DrawLine(ox, oy, outer, ex, ey, outer, 3.0f, 0);
        C2D_DrawLine(ox, oy, core,  ex, ey, core,  1.0f, 0);
    } else {
        /* Brimstone: dark red 8px + red 4px + white 2px core */
        u32 dark = C2D_Color32(120, 10, 10, a);
        u32 red  = C2D_Color32(220, 30, 30, a);
        u32 core = C2D_Color32(255, 240, 240, a);
        C2D_DrawLine(ox, oy, dark, ex, ey, dark, 8.0f, 0);
        C2D_DrawLine(ox, oy, red,  ex, ey, red,  4.0f, 0);
        C2D_DrawLine(ox, oy, core, ex, ey, core, 2.0f, 0);
        /* Impact splash where the beam terminates */
        C2D_DrawCircleSolid(ex, ey, 0, 6.0f * fade, red);
        C2D_DrawCircleSolid(ex, ey, 0, 3.0f * fade, core);
    }
}

/* E3 (Satan): enemy Brimstone beam. Telegraph = deterministic thin red
 * line; fire = layered thick beam (same layered-line approach as the
 * player's Brimstone renderer, separate state). Photosensitivity-safe:
 * smooth alpha, no strobing. */
static void render_enemy_beam(Game *g) {
    if (!g->ebeam_state) return;
    float ox = g->ebeam_x, oy = g->ebeam_y;

    /* R9 (The Lamb): 4-way brimstone CROSS — the same telegraph/fire
       styling applied to four axis-aligned arms running to the walls. */
    if (g->ebeam_cross) {
        float axs[4] = { (float)ROOM_LEFT, (float)ROOM_RIGHT, ox, ox };
        float ays[4] = { oy, oy, (float)ROOM_TOP, (float)ROOM_BOTTOM };
        if (g->ebeam_state == 1) {
            float tfrac = 1.0f - (float)g->ebeam_timer / 35.0f;
            if (tfrac < 0.0f) tfrac = 0.0f;
            int a = (int)(90 + 100 * tfrac);
            u32 warn = C2D_Color32(255, 40, 40, a);
            for (int bi = 0; bi < 4; bi++)
                C2D_DrawLine(ox, oy, warn, axs[bi], ays[bi], warn, 1.5f, 0);
        } else {
            float fade = (g->ebeam_timer < 3) ? (float)g->ebeam_timer / 3.0f
                                              : 1.0f;
            int a = (int)(255.0f * fade);
            u32 dark = C2D_Color32(100, 5, 5, a);
            u32 red  = C2D_Color32(210, 25, 25, a);
            u32 core = C2D_Color32(255, 230, 230, a);
            for (int bi = 0; bi < 4; bi++) {
                C2D_DrawLine(ox, oy, dark, axs[bi], ays[bi], dark, 10.0f, 0);
                C2D_DrawLine(ox, oy, red,  axs[bi], ays[bi], red,  5.0f, 0);
                C2D_DrawLine(ox, oy, core, axs[bi], ays[bi], core, 2.0f, 0);
            }
            C2D_DrawCircleSolid(ox, oy, 0, 9.0f * fade, red);
        }
        return;
    }

    float ex = g->ebeam_ex, ey = g->ebeam_ey;
    if (g->ebeam_state == 1) {
        /* Telegraph: thin warning line, eases in over the 30 frames */
        float tfrac = 1.0f - (float)g->ebeam_timer / 30.0f;
        int a = (int)(90 + 100 * tfrac);
        u32 warn = C2D_Color32(255, 40, 40, a);
        C2D_DrawLine(ox, oy, warn, ex, ey, warn, 1.5f, 0);
    } else {
        /* Fire: dark red 10px + red 5px + pale core 2px */
        float fade = (g->ebeam_timer < 3) ? (float)g->ebeam_timer / 3.0f : 1.0f;
        int a = (int)(255.0f * fade);
        u32 dark = C2D_Color32(100, 5, 5, a);
        u32 red  = C2D_Color32(210, 25, 25, a);
        u32 core = C2D_Color32(255, 230, 230, a);
        C2D_DrawLine(ox, oy, dark, ex, ey, dark, 10.0f, 0);
        C2D_DrawLine(ox, oy, red,  ex, ey, red,  5.0f, 0);
        C2D_DrawLine(ox, oy, core, ex, ey, core, 2.0f, 0);
        C2D_DrawCircleSolid(ex, ey, 0, 7.0f * fade, red);
    }
}

/* R9 (Isaac): Cathedral light columns. Telegraph = warm ground-marker
 * ellipse pulsing up over the 40-frame warning; fire = full-height light
 * column for ~20 frames. All alphas ease smoothly (no strobing); pulse
 * phase is deterministic from the telegraph timer. */
static void render_vbeams(Game *g) {
    if (!g->vbeam_state) return;
    float gy = ROOM_BOTTOM - 8.0f;
    for (int i = 0; i < g->vbeam_count; i++) {
        float bx = g->vbeam_x[i];
        if (g->vbeam_state == 1) {
            float tfrac = 1.0f - (float)g->vbeam_timer / 40.0f;
            if (tfrac < 0.0f) tfrac = 0.0f;
            if (tfrac > 1.0f) tfrac = 1.0f;
            int a = (int)(70 + 110 * tfrac);
            float w = 10.0f + 8.0f * tfrac;
            C2D_DrawEllipseSolid(bx - w, gy - w * 0.35f, 0,
                                 w * 2.0f, w * 0.7f,
                                 C2D_Color32(255, 235, 150, a));
            C2D_DrawEllipseSolid(bx - w * 0.5f, gy - w * 0.18f, 0,
                                 w, w * 0.35f,
                                 C2D_Color32(255, 255, 220, a));
        } else {
            float fade = (g->vbeam_timer < 4) ? (float)g->vbeam_timer / 4.0f
                                              : 1.0f;
            int a = (int)(200.0f * fade);
            C2D_DrawRectSolid(bx - 9, ROOM_TOP, 0, 18,
                              ROOM_BOTTOM - ROOM_TOP,
                              C2D_Color32(255, 245, 200, (int)(a * 0.55f)));
            C2D_DrawRectSolid(bx - 4, ROOM_TOP, 0, 8,
                              ROOM_BOTTOM - ROOM_TOP,
                              C2D_Color32(255, 255, 235, a));
            C2D_DrawEllipseSolid(bx - 14, gy - 5, 0, 28, 10,
                                 C2D_Color32(255, 245, 200,
                                             (int)(90.0f * fade)));
        }
    }
}

/* R8 (M7): Head of Krampus player burst — 4-way brimstone arms from the
 * fire position, fading out over the ~10-frame pbeam_timer. Presentation
 * only (damage was applied instantly at use); photosensitivity-safe fade. */
static void render_pbeam(Game *g) {
    if (g->pbeam_timer <= 0) return;
    float fade = (float)g->pbeam_timer / 10.0f;
    if (fade > 1.0f) fade = 1.0f;
    int a = (int)(255.0f * fade);
    float ox = g->pbeam_x, oy = g->pbeam_y;
    float axs[4] = { (float)ROOM_LEFT, (float)ROOM_RIGHT, ox, ox };
    float ays[4] = { oy, oy, (float)ROOM_TOP, (float)ROOM_BOTTOM };
    u32 dark = C2D_Color32(100, 5, 5, a);
    u32 red  = C2D_Color32(210, 25, 25, a);
    u32 core = C2D_Color32(255, 230, 230, a);
    for (int bi = 0; bi < 4; bi++) {
        C2D_DrawLine(ox, oy, dark, axs[bi], ays[bi], dark, 10.0f, 0);
        C2D_DrawLine(ox, oy, red,  axs[bi], ays[bi], red,  5.0f, 0);
        C2D_DrawLine(ox, oy, core, axs[bi], ays[bi], core, 2.0f, 0);
    }
    C2D_DrawCircleSolid(ox, oy, 0, 9.0f * fade, red);
}

/* E6: familiars drawn as small tinted baby sprites at their trail slots */
static void render_familiars(Game *g) {
    Player *p = &g->player;
    for (int i = 0; i < MAX_FAMILIARS; i++) {
        if (p->familiar[i] == ITEM_NONE) continue;
        float fx, fy;
        familiar_pos(g, i, &fx, &fy);
        float bob = sinf((float)(g->frame + i * 17) * 0.12f) * 2.0f;
        /* Shadow */
        C2D_DrawEllipseSolid(fx - 6.0f, fy + 6.0f, 0, 12.0f, 4.0f, COL_SHADOW);
        if (g_sprites_loaded) {
            float sc = 0.55f;
            switch (p->familiar[i]) {
            case ITEM_GHOST_BABY:
                /* Pale blue, translucent (spectral) */
                spr_draw_rotated_tinted(sheet_sprites, sprites_atlas_enemy_baby_idx,
                                        fx, fy - bob, sc, sc, 0.0f,
                                        C2D_Color32(150, 200, 255, 255), 0.45f);
                break;
            case ITEM_DEMON_BABY:
                /* Dark red demon tint */
                spr_draw_rotated_tinted(sheet_sprites, sprites_atlas_enemy_baby_idx,
                                        fx, fy - bob, sc, sc, 0.0f,
                                        C2D_Color32(150, 30, 30, 255), 0.5f);
                break;
            default: /* Brother Bobby: plain */
                spr_draw(sheet_sprites, sprites_atlas_enemy_baby_idx,
                         fx, fy - bob, sc, sc);
                break;
            }
        } else {
            u32 fc = (p->familiar[i] == ITEM_DEMON_BABY)
                       ? C2D_Color32(150, 30, 30, 255)
                   : (p->familiar[i] == ITEM_GHOST_BABY)
                       ? C2D_Color32(150, 200, 255, 200)
                       : C2D_Color32(220, 200, 170, 255);
            C2D_DrawCircleSolid(fx, fy - bob, 0, 6.0f, fc);
        }
    }
}

/* D3 Mom's Knife: drawn from the actual knife item sprite rotated to the
 * travel direction (falls back to a stretched large tear, then to a plain
 * line, when atlases are missing). Visible while held and in flight. */
static void render_knife(Game *g) {
    if (!(g->player.stats.flags & ITEM_FLAG_KNIFE)) return;
    float ang = atan2f(g->knife_dy, g->knife_dx);
    /* Small shadow under the blade */
    C2D_DrawEllipseSolid(g->knife_x - 6.0f, g->knife_y + 4.0f, 0,
                         12.0f, 4.0f, C2D_Color32(0, 0, 0, 40));
    if (sheet_ui_items) {
        /* Item icon's blade points up: +90 deg leads with the blade */
        spr_draw_rotated(sheet_ui_items, ui_items_atlas_item_moms_knife_idx,
                         g->knife_x, g->knife_y, 0.9f, 0.9f,
                         ang + 1.5707963f);
    } else if (sheet_bullets) {
        /* Stretched largest tear reads as a blade */
        spr_draw_rotated(sheet_bullets, bulletatlas_tear_blue_1_idx + 5,
                         g->knife_x, g->knife_y, 2.2f, 0.6f, ang);
    } else {
        u32 kc = C2D_Color32(220, 220, 230, 255);
        C2D_DrawLine(g->knife_x - cosf(ang) * 10.0f,
                     g->knife_y - sinf(ang) * 10.0f, kc,
                     g->knife_x + cosf(ang) * 10.0f,
                     g->knife_y + sinf(ang) * 10.0f, kc, 4.0f, 0);
    }
}

static void render_tears(Game *g) {
    /* R8 #45: count active tears once — skip the trail draws when the
       screen is saturated so the C2D budget stays healthy. */
    int tearCount = 0;
    for (int i = 0; i < MAX_TEARS; i++)
        if (g->tears[i].active) tearCount++;
    int drawTrails = (tearCount <= 20);

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

            /* R8 #45: short fading trail behind player tears using the
               previously unused bulletatlas tear_trail_1/2/3 sprites.
               Afterimages are offset against velocity — no history
               arrays. Piercing tears keep their own distinct trail. */
            if (drawTrails && !t->is_enemy && !t->piercing) {
                static const int trail_idx[3] = {
                    bulletatlas_tear_trail_1_idx,
                    bulletatlas_tear_trail_2_idx,
                    bulletatlas_tear_trail_3_idx
                };
                static const float trail_alpha[3] = { 0.42f, 0.28f, 0.16f };
                for (int tr = 0; tr < 3; tr++) {
                    float trx = t->x - t->dx * 1.6f * (float)(tr + 1);
                    float tryy = render_y - t->dy * 1.6f * (float)(tr + 1);
                    spr_draw_rotated_alpha(sheet_bullets, trail_idx[tr],
                                           trx, tryy,
                                           base_scale, base_scale,
                                           t->rotation, trail_alpha[tr]);
                }
            }

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

                    /* R8 #44: the other jumpers get the same airborne draw
                       offset. Gish/Mom/Satan share Monstro's convention
                       (jump_z negative = up); Widow's arc is positive-up. */
                    if (e->type == ENEMY_BOSS_GISH ||
                        e->type == ENEMY_BOSS_MOM ||
                        e->type == ENEMY_BOSS_SATAN) {
                        draw_y += e->jump_z;
                    } else if (e->type == ENEMY_BOSS_WIDOW) {
                        draw_y -= e->jump_z;
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
                    } else if (e->type == ENEMY_BOSS_ISAAC) {
                        /* R9: holy pale-gold grade; deeper while praying */
                        float hb = (e->state == 1) ? 0.45f : 0.25f;
                        spr_draw_tinted(sheet_sprites, idx, draw_x, draw_y,
                                       draw_scaleX, draw_scaleY,
                                       C2D_Color32(255, 240, 190, 255), hb);
                    } else if (e->type == ENEMY_BOSS_THE_LAMB) {
                        /* R9: demonic near-black violet grade */
                        spr_draw_tinted(sheet_sprites, idx, draw_x, draw_y,
                                       draw_scaleX, draw_scaleY,
                                       C2D_Color32(60, 45, 70, 255), 0.55f);
                    } else if (e->type == ENEMY_BOSS_IT_LIVES) {
                        /* R9: deep pulsing flesh-red grade */
                        spr_draw_tinted(sheet_sprites, idx, draw_x, draw_y,
                                       draw_scaleX, draw_scaleY,
                                       C2D_Color32(190, 25, 35, 255), 0.5f);
                    } else if (e->type == ENEMY_BOSS_URIEL ||
                               e->type == ENEMY_BOSS_GABRIEL) {
                        /* R8: pale-stone angel grade (Gabriel warmer/gold) */
                        u32 acol = (e->type == ENEMY_BOSS_GABRIEL)
                                 ? C2D_Color32(250, 226, 160, 255)
                                 : C2D_Color32(232, 232, 240, 255);
                        spr_draw_tinted(sheet_sprites, idx, draw_x, draw_y,
                                       draw_scaleX, draw_scaleY, acol, 0.5f);
                    } else if (e->type == ENEMY_BOSS_KRAMPUS) {
                        /* R8: sooty near-black red grade */
                        spr_draw_tinted(sheet_sprites, idx, draw_x, draw_y,
                                       draw_scaleX, draw_scaleY,
                                       C2D_Color32(55, 30, 30, 255), 0.55f);
                    } else if (e->type == ENEMY_BOSS_BLUE_BABY) {
                        /* R8: cold blue-grey corpse grade */
                        spr_draw_tinted(sheet_sprites, idx, draw_x, draw_y,
                                       draw_scaleX, draw_scaleY,
                                       C2D_Color32(150, 175, 210, 255), 0.5f);
                    } else {
                        spr_draw(sheet_sprites, idx, draw_x, draw_y,
                                draw_scaleX, draw_scaleY);
                    }

                    /* R9 Isaac: halo + soft prayer glow (deterministic
                       pulse from g->frame — safe for stereoscopic 3D) */
                    if (e->type == ENEMY_BOSS_ISAAC) {
                        float hp2 = 0.75f + 0.25f * sinf((float)g->frame * 0.08f);
                        C2D_DrawEllipseSolid(draw_x - 14.0f,
                                             draw_y - sz - 12.0f, 0,
                                             28.0f, 8.0f,
                                             C2D_Color32(255, 235, 160,
                                                         (int)(120 * hp2)));
                        if (e->state == 1)
                            C2D_DrawCircleSolid(draw_x, draw_y, 0,
                                                sz * 1.15f,
                                                C2D_Color32(255, 245, 200, 28));
                    }

                    /* Larry Jr / Chub / Scolex: body segments as flat
                       circles (skin-tone fill + darker rim) so only the
                       head wears the face. Skipped while burrowed. */
                    if ((e->type == ENEMY_BOSS_LARRY ||
                         e->type == ENEMY_BOSS_CHUB ||
                         e->type == ENEMY_BOSS_SCOLEX) && !e->hidden) {
                        u32 segFill, segRim;
                        if (e->type == ENEMY_BOSS_CHUB) {
                            segFill = C2D_Color32(224, 178, 186, 255);
                            segRim  = C2D_Color32(160, 106, 116, 255);
                        } else if (e->type == ENEMY_BOSS_SCOLEX) {
                            segFill = C2D_Color32(206, 186, 128, 255);
                            segRim  = C2D_Color32(146, 126, 76, 255);
                        } else { /* Larry Jr */
                            segFill = C2D_Color32(198, 166, 134, 255);
                            segRim  = C2D_Color32(138, 108, 82, 255);
                        }
                        for (int s = 0; s < e->seg_count; s++) {
                            LarrySegment *seg = &e->segments[s];
                            /* Shadow for each segment */
                            float segSz = ENEMY_SIZE * 1.5f;
                            C2D_DrawEllipseSolid(seg->x - segSz * 0.5f, seg->y + segSz * 0.15f, 0,
                                                segSz, segSz * 0.3f, COL_SHADOW);
                            /* Shrinking flat body circles */
                            float segR = ENEMY_SIZE * (1.3f - s * 0.12f);
                            u32 fill = (e->flash > 0)
                                     ? C2D_Color32(255, 255, 255, 255) : segFill;
                            C2D_DrawCircleSolid(seg->x, seg->y, 0, segR, segRim);
                            C2D_DrawCircleSolid(seg->x, seg->y - 1.0f, 0,
                                                segR * 0.72f, fill);
                        }
                    }

                    /* Gemini/Steven: render companion entity + tether.
                       R8 #19: Steven's second head now draws too (it's a
                       killable target), and a dead head (gemini_chp <= 0)
                       is not drawn at all.
                       R9: The Lamb's detached body reuses this draw
                       (gemini_chp only becomes > 0 at the detach, and
                       gemini_split is set there, so no tether shows). */
                    if ((e->type == ENEMY_BOSS_GEMINI ||
                         e->type == ENEMY_BOSS_STEVEN ||
                         e->type == ENEMY_BOSS_THE_LAMB) && e->gemini_chp > 0) {
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

                    /* R8 #44: shared landing telegraph for ALL jump bosses
                       (Monstro/Gish/Mom/Satan/Widow) — a dark ellipse at
                       the landing target that grows as touchdown nears,
                       replacing the old Monstro-only under-body shadow. */
                    {
                        float jprog = 0.0f;
                        if (boss_airborne(e, &jprog)) {
                            float shR = sz * (0.4f + 0.6f * jprog);
                            u8 shA = (u8)(70.0f + 110.0f * jprog);
                            C2D_DrawEllipseSolid(e->target_x - shR,
                                                 e->target_y - shR * 0.4f, 0,
                                                 shR * 2.0f, shR * 0.8f,
                                                 C2D_Color32(0, 0, 0, shA));
                        }
                    }

                } else {
                    /* Regular enemy sprite */
                    float draw_y = e->y;
                    /* Hopper/Leaper/Trite: offset Y by jump_arc for airborne effect */
                    if (e->type == ENEMY_HOPPER || e->type == ENEMY_LEAPER ||
                        e->type == ENEMY_TRITE) {
                        draw_y -= e->jump_arc;
                    }
                    /* Game-feel hit-squash: briefly widen + flatten on hit
                       (mirrors the Clotty squash). Render only — no hitbox change. */
                    float sx = scale;
                    float sy = scale;
                    if (e->flash > 0) {
                        sx *= 1.0f + 0.15f * (e->flash / 14.0f);
                        sy *= 1.0f - 0.12f * (e->flash / 14.0f);
                    }

                    /* ── Procedural animation (all driven by anim_timer) ──
                       Flyers: sine bob + mirrored wing-beat every 8f.
                       Walkers: squash-stretch scaled by how fast they move.
                       Shooters: 6f scale-up + warm tint telegraph before
                       shoot_timer fires. Clotty/Pacer keep their real
                       frame animations (handled above, never reach here). */
                    int flyer = (e->type == ENEMY_FLY || e->type == ENEMY_ATTACK_FLY ||
                                 e->type == ENEMY_POOTER || e->type == ENEMY_BOOM_FLY ||
                                 e->type == ENEMY_SUCKER || e->type == ENEMY_VIS ||
                                 e->type == ENEMY_EYE || e->type == ENEMY_LIL_HAUNT);
                    int shooter = (e->type == ENEMY_POOTER || e->type == ENEMY_MAW ||
                                   e->type == ENEMY_RED_MAW || e->type == ENEMY_VIS ||
                                   e->type == ENEMY_SUCKER || e->type == ENEMY_SPITTY ||
                                   e->type == ENEMY_MULLIGAN || e->type == ENEMY_EYE ||
                                   (e->type == ENEMY_HOST && !e->hidden));
                    int flip = 0;
                    if (flyer) {
                        draw_y += sinf((float)e->anim_timer * 0.12f) * 2.0f;
                        flip = (e->anim_timer / 8) & 1;   /* wing-beat mirror */
                    } else if (e->type != ENEMY_HOPPER && e->type != ENEMY_LEAPER &&
                               e->type != ENEMY_TRITE) {
                        float spd = sqrtf(e->dx * e->dx + e->dy * e->dy);
                        if (spd > 1.0f) spd = 1.0f;
                        float sq = 1.0f + 0.06f * spd *
                                   sinf((float)e->anim_timer * 0.25f);
                        sx *= 1.0f / sq;
                        sy *= sq;
                    }
                    float tele = 0.0f;
                    if (shooter && e->shoot_timer > 0 && e->shoot_timer <= 6) {
                        tele = (float)(7 - e->shoot_timer) / 6.0f;
                        if (tele > 1.0f) tele = 1.0f;
                        sx *= 1.0f + 0.18f * tele;
                        sy *= 1.0f + 0.18f * tele;
                    }

                    /* Host / Round Worm: draw at half alpha when hidden */
                    if ((e->type == ENEMY_HOST || e->type == ENEMY_ROUND_WORM) && e->hidden) {
                        spr_draw_tinted(sheet_sprites, idx, e->x, draw_y, sx, sy,
                                       C2D_Color32(128, 128, 128, 180), 0.6f);
                    } else if (e->type == ENEMY_GLOBIN && e->state == 1) {
                        /* Globin collapsed: draw squished */
                        spr_draw_tinted(sheet_sprites, idx, e->x, draw_y + 4.0f,
                                       scale * 1.3f, scale * 0.5f,
                                       C2D_Color32(200, 80, 80, 200), 0.5f);
                    } else if (e->flash > 0) {
                        spr_draw_tinted(sheet_sprites, idx, e->x, draw_y, sx, sy,
                                       C2D_Color32(255, 255, 255, 255), 0.7f);
                    } else if (tele > 0.0f) {
                        spr_draw_tinted(sheet_sprites, idx, e->x, draw_y, sx, sy,
                                       C2D_Color32(255, 140, 80, 255), 0.3f * tele);
                    } else if (flip) {
                        spr_draw_fliph(sheet_sprites, idx, e->x, draw_y, sx, sy);
                    } else {
                        spr_draw(sheet_sprites, idx, e->x, draw_y, sx, sy);
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
                    case ENEMY_TRITE:        col = C2D_Color32(60, 80, 100, 255); break;
                    case ENEMY_FATTY:        col = C2D_Color32(120, 160, 90, 255); break;
                    case ENEMY_CHARGER:      col = C2D_Color32(150, 120, 70, 255); break;
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
                    case ENEMY_BOSS_GISH:       col = C2D_Color32(40, 80, 50, 255); break;
                    case ENEMY_BOSS_LOKI:       col = C2D_Color32(90, 40, 110, 255); break;
                    case ENEMY_BOSS_STEVEN:     col = C2D_Color32(190, 90, 60, 255); break;
                    case ENEMY_BOSS_CHUB:       col = C2D_Color32(170, 130, 50, 255); break;
                    case ENEMY_BOSS_FISTULA:    col = C2D_Color32(150, 70, 90, 255); break;
                    case ENEMY_BOSS_SCOLEX:     col = C2D_Color32(160, 150, 90, 255); break;
                    case ENEMY_BOSS_MEGA_SATAN: col = C2D_Color32(40, 0, 0, 255); break;
                    case ENEMY_BOSS_MOM:        col = C2D_Color32(200, 150, 130, 255); break;
                    case ENEMY_BOSS_MOMS_HEART: col = C2D_Color32(170, 30, 40, 255); break;
                    case ENEMY_BOSS_SATAN:      col = C2D_Color32(60, 10, 10, 255); break;
                    case ENEMY_BOSS_ISAAC:      col = C2D_Color32(240, 230, 200, 255); break;
                    case ENEMY_BOSS_THE_LAMB:   col = C2D_Color32(70, 50, 80, 255); break;
                    case ENEMY_BOSS_IT_LIVES:   col = C2D_Color32(150, 20, 30, 255); break;
                    case ENEMY_BOSS_URIEL:      col = C2D_Color32(225, 225, 235, 255); break;
                    case ENEMY_BOSS_GABRIEL:    col = C2D_Color32(235, 210, 140, 255); break;
                    case ENEMY_BOSS_KRAMPUS:    col = C2D_Color32(50, 28, 28, 255); break;
                    case ENEMY_BOSS_BLUE_BABY:  col = C2D_Color32(140, 165, 200, 255); break;
                    case ENEMY_EYE:             col = C2D_Color32(230, 220, 220, 255); break;
                    case ENEMY_LIL_HAUNT:       col = C2D_Color32(120, 100, 130, 255); break;
                    case ENEMY_KEEPER:          col = C2D_Color32(200, 170, 40, 255); break;
                    case ENEMY_SUCKER:          col = C2D_Color32(110, 70, 120, 255); break;
                    case ENEMY_FISTULA_BALL:    col = C2D_Color32(150, 70, 90, 255); break;
                    case ENEMY_ROUND_WORM:      col = C2D_Color32(130, 110, 70, 255); break;
                    case ENEMY_SPITTY:          col = C2D_Color32(90, 130, 100, 255); break;
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

    /* Fade in effect */
    float alpha = 1.0f;
    if (g->floor_transition_timer > 100) {
        alpha = (120.0f - g->floor_transition_timer) / 20.0f;
    } else if (g->floor_transition_timer < 20) {
        alpha = g->floor_transition_timer / 20.0f;
    }
    if (alpha < 0) alpha = 0;
    if (alpha > 1) alpha = 1;
    u8 a8 = (u8)(alpha * 255);

    /* ── Parchment note (shared paper panel, alpha follows the fade) ── */
    float ppx = 108, ppy = 26, ppw = 184, pph = 176;
    draw_paper_panel(ppx, ppy, ppw, pph, a8);

    /* Floor name, inked at the top of the note */
    C2D_Text t1;
    gtext_parse(&t1, textBuf, fi->name);
    C2D_TextOptimize(&t1);
    float nw = t1.width * 0.7f;
    C2D_DrawText(&t1, C2D_WithColor, TOP_SCREEN_WIDTH / 2.0f - nw / 2,
                 ppy + 10, 0, 0.7f, 0.7f, C2D_Color32(55, 43, 33, a8));

    /* Isaac descending, centered on the note */
    if (g_sprites_loaded && alpha > 0.5f) {
        spr_draw(sheet_sprites, player_sprite_idx(DIR_DOWN, 0),
                 TOP_SCREEN_WIDTH / 2.0f, ppy + 78, 2.0f, 2.0f);
    }

    /* Floor number (show total in infinite mode) */
    C2D_Text t2;
    char floorBuf[48];
    if (g->game_mode == MODE_INFINITE && g->infinite_loop > 0) {
        int total = g->infinite_loop * MAX_FLOORS + g->current_floor + 1;
        snprintf(floorBuf, sizeof(floorBuf), "Floor %d (Loop %d)", total, g->infinite_loop + 1);
    } else {
        snprintf(floorBuf, sizeof(floorBuf), "Floor %d", g->current_floor + 1);
    }
    gtext_parse(&t2, textBuf, floorBuf);
    C2D_TextOptimize(&t2);
    float fw = t2.width * 0.55f;
    C2D_DrawText(&t2, C2D_WithColor, TOP_SCREEN_WIDTH / 2.0f - fw / 2,
                 ppy + 116, 0, 0.55f, 0.55f, C2D_Color32(122, 99, 75, a8));

    /* Row of pickup counters carried down the ladder */
    if (g_sprites_loaded && sheet_ui_items) {
        const int icons[3] = { ui_items_atlas_item_coin_idx,
                               ui_items_atlas_item_bomb_idx,
                               ui_items_atlas_item_key_idx };
        const int counts[3] = { g->player.coins, g->player.bombs,
                                g->player.keys };
        for (int pi = 0; pi < 3; pi++) {
            float ix = TOP_SCREEN_WIDTH / 2.0f - 58.0f + pi * 46.0f;
            float iy = ppy + 148;
            if (alpha > 0.5f)
                spr_draw(sheet_ui_items, icons[pi], ix, iy, 0.8f, 0.8f);
            char cb[8];
            snprintf(cb, sizeof(cb), "%d", counts[pi]);
            C2D_Text ct;
            gtext_parse(&ct, textBuf, cb);
            C2D_TextOptimize(&ct);
            C2D_DrawText(&ct, C2D_WithColor, ix + 8, iy - 6, 0, 0.5f, 0.5f,
                         C2D_Color32(55, 43, 33, a8));
        }
    }
}

/* ================================================================
 * Render: Game Over / Win screens
 * ================================================================ */

void render_gameover(Game *g, C2D_TextBuf textBuf) {
    /* ── Rebirth-style "last will" death note on parchment ── */
    C2D_Text t1, t2, t3;

    /* Deterministic pulse phase for the hint (render-side counter) */
    g->menu_timer++;

    /* Parchment panel (shared paper helper) */
    float ppx = 64, ppy = 14, ppw = 272, pph = 196;
    draw_paper_panel(ppx, ppy, ppw, pph, 255);

    gtext_parse(&t1, textBuf, "YOU DIED");
    C2D_TextOptimize(&t1);
    C2D_DrawText(&t1, C2D_WithColor, 148, ppy + 6, 0, 0.85f, 0.85f, BLOOD);

    /* Dead Isaac drawing, left side of the note */
    if (g_sprites_loaded) {
        spr_draw(sheet_sprites, player_death_sprite_idx(),
                 ppx + 52, ppy + 92, 2.2f, 2.2f);
    }
    /* Blood scribbles near the corpse */
    draw_doodle_scratch(ppx + 28, ppy + 112, ppx + 72, ppy + 118, BLOOD_DARK);
    draw_doodle_scratch(ppx + 36, ppy + 124, ppx + 78, ppy + 118, BLOOD_DARK);

    /* Last item held, Rebirth-style, right side of the note */
    if (g->player.item_count > 0 && g_sprites_loaded) {
        ItemType last = g->player.items[g->player.item_count - 1];
        const ItemDef *ldef = get_item_def(last);
        C2D_Text liText;
        gtext_parse(&liText, textBuf, "Last item:");
        C2D_TextOptimize(&liText);
        C2D_DrawText(&liText, C2D_WithColor, ppx + 150, ppy + 44, 0, 0.45f, 0.45f,
                     INK_FAINT);
        spr_draw(sheet_ui_items, item_sprite_idx(last),
                 ppx + 170, ppy + 74, 1.4f, 1.4f);
        if (ldef && ldef->name) {
            C2D_Text lnText;
            gtext_parse(&lnText, textBuf, ldef->name);
            C2D_TextOptimize(&lnText);
            C2D_DrawText(&lnText, C2D_WithColor, ppx + 132, ppy + 92, 0,
                         0.4f, 0.4f, INK_FAINT);
        }
    }

    /* Run stats, written like a note */
    const FloorInfo *fi = get_floor_info(g->current_floor);
    char line[64];
    int secs = g->play_time_frames / 60;
    if (g->game_mode == MODE_INFINITE) {
        snprintf(line, sizeof(line), "Deepest: F%d %s   Kills: %d   %d:%02d",
                 g->best_floor + 1, fi->name, g->kills, secs / 60, secs % 60);
    } else {
        snprintf(line, sizeof(line), "%s   Kills: %d   %d:%02d",
                 fi->name, g->kills, secs / 60, secs % 60);
    }
    C2D_Text stText;
    gtext_parse(&stText, textBuf, line);
    C2D_TextOptimize(&stText);
    C2D_DrawText(&stText, C2D_WithColor, ppx + 18, ppy + 142, 0, 0.45f, 0.45f,
                 INK_FAINT);

    char scoreBuf[48];
    snprintf(scoreBuf, sizeof(scoreBuf), "Score: %d   Items: %d",
             g->score, g->player.item_count);
    gtext_parse(&t2, textBuf, scoreBuf);
    C2D_TextOptimize(&t2);
    C2D_DrawText(&t2, C2D_WithColor, ppx + 18, ppy + 162, 0, 0.5f, 0.5f,
                 BLOOD_DARK);

    gtext_parse(&t3, textBuf, "Press START for menu");
    C2D_TextOptimize(&t3);
    {
        float ga = 0.6f + sinf(g->menu_timer * 0.1f) * 0.3f;
        if (ga < 0.0f) ga = 0.0f;
        if (ga > 1.0f) ga = 1.0f;
        C2D_DrawText(&t3, C2D_WithColor, 118, 218, 0, 0.55f, 0.55f,
                     C2D_Color32(122, 99, 75, (u8)(ga * 255)));
    }
}

void render_win(Game *g, C2D_TextBuf textBuf) {
    /* Round 7: spotlight-in-the-dark ending tableau. Deterministic pulse
       phase comes from menu_timer (render-side counter, like render_menu). */
    g->menu_timer++;

    u32 bone = C2D_Color32(238, 228, 206, 255);
    int dark_end = (g->win_ending == 3);

    /* 1. Void */
    C2D_DrawRectSolid(0, 0, 0, TOP_SCREEN_WIDTH, TOP_SCREEN_HEIGHT,
                      C2D_Color32(0, 0, 0, 255));

    /* 2. Spotlight: stacked soft ellipses centered on (200,130).
       R9: the dark ending swaps the warm beam for a blood-red gloom;
       the light ending (2) brightens the shaft. */
    {
        u8 sr = dark_end ? 120 : 255;
        u8 sg2 = dark_end ? 12  : 244;
        u8 sb = dark_end ? 18  : 214;
        int boost = (g->win_ending == 2) ? 10 : 0;
        C2D_DrawEllipseSolid(200 - 120, 130 - 90, 0, 240, 180,
                             C2D_Color32(sr, sg2, sb, 25 + boost));
        C2D_DrawEllipseSolid(200 - 95, 130 - 72, 0, 190, 144,
                             C2D_Color32(sr, sg2, sb, 35 + boost));
        C2D_DrawEllipseSolid(200 - 70, 130 - 54, 0, 140, 108,
                             C2D_Color32(sr, sg2, sb, 45 + boost));
        C2D_DrawEllipseSolid(200 - 48, 130 - 38, 0, 96, 76,
                             C2D_Color32(sr, sg2, sb, 60 + boost));
    }

    /* 3+4. Center tableau per ending. */
    if (dark_end) {
        /* THE LAMB CROWNED (procedural — no atlas art): dark silhouette,
           curved horns, burning eyes, and a crown floating above. */
        float lp = 0.75f + 0.25f * sinf(g->menu_timer * 0.06f);
        u32 body = C2D_Color32(20, 14, 24, 255);
        u32 horn = C2D_Color32(46, 34, 52, 255);
        u32 eye  = C2D_Color32(220, 30, 30, (u8)(200 * lp));
        u32 gold = C2D_Color32(212, 168, 60, 255);
        /* horns */
        C2D_DrawTriangle(178, 112, horn, 190, 118, horn, 172, 88, horn, 0);
        C2D_DrawTriangle(222, 112, horn, 210, 118, horn, 228, 88, horn, 0);
        /* head + body */
        C2D_DrawCircleSolid(200, 154, 0, 22, body);
        C2D_DrawCircleSolid(200, 124, 0, 24, body);
        /* eyes */
        C2D_DrawCircleSolid(191, 120, 0, 4, eye);
        C2D_DrawCircleSolid(209, 120, 0, 4, eye);
        /* crown: three gold spikes on a band, floating above the horns */
        C2D_DrawRectSolid(184, 82, 0, 32, 6, gold);
        C2D_DrawTriangle(184, 82, gold, 192, 82, gold, 188, 68, gold, 0);
        C2D_DrawTriangle(196, 82, gold, 204, 82, gold, 200, 64, gold, 0);
        C2D_DrawTriangle(208, 82, gold, 216, 82, gold, 212, 68, gold, 0);
    } else if (g->win_ending == 2) {
        /* ISAAC ASCENDS: he floats up inside a light column; the wooden
           chest below has closed behind him. */
        float rise = sinf(g->menu_timer * 0.05f) * 3.0f;
        C2D_DrawRectSolid(200 - 16, 20, 0, 32, 150,
                          C2D_Color32(255, 250, 220, 46));
        C2D_DrawRectSolid(200 - 7, 20, 0, 14, 150,
                          C2D_Color32(255, 255, 240, 70));
        if (g_sprites_loaded) {
            spr_draw(sheet_sprites, player_sprite_idx(DIR_DOWN, 0),
                     200, 104 + rise, 2.0f, 2.0f);
            if (sheet_environment)
                spr_draw(sheet_environment,
                         environment_atlas_env_chest_wood_idx,
                         200, 168, 1.4f, 1.4f);
        } else {
            C2D_DrawCircleSolid(200, 104 + rise, 0, 14, bone);
        }
    } else if (g_sprites_loaded) {
        spr_draw(sheet_sprites, player_sprite_idx(DIR_DOWN, 0),
                 200, 128, 2.2f, 2.2f);
        if (sheet_environment)
            spr_draw(sheet_environment,
                     g->win_ending == 1 ? environment_atlas_env_chest_wood_idx
                                        : environment_atlas_env_chest_gold_idx,
                     200, 158, 1.4f, 1.4f);
    }

    /* 5. Title, bone-white with a black offset shadow — each ending
       keeps its distinct line. */
    C2D_Text t1;
    gtext_parse(&t1, textBuf,
                g->win_ending == 3 ? "THE LAMB IS CROWNED." :
                g->win_ending == 2 ? "ISAAC ASCENDS."       :
                g->win_ending == 1 ? "ENDING 1"             :
                                     "ISAAC ESCAPED.");
    C2D_TextOptimize(&t1);
    {
        float tw = 0.0f, th = 0.0f;
        C2D_TextGetDimensions(&t1, 0.7f, 0.7f, &tw, &th);
        C2D_DrawText(&t1, C2D_WithColor, 200 - tw / 2 + 2, 36, 0, 0.7f, 0.7f,
                     C2D_Color32(0, 0, 0, 255));
        C2D_DrawText(&t1, C2D_WithColor, 200 - tw / 2, 34, 0, 0.7f, 0.7f,
                     dark_end ? C2D_Color32(220, 60, 60, 255) : bone);
    }

    /* 6. Stats lines (content kept from the old screen) */
    {
        u32 dim = C2D_Color32(190, 180, 160, 220);
        char lineBuf[64];
        if (g->win_ending == 3)
            snprintf(lineBuf, sizeof(lineBuf), "Darkness has a new king.");
        else if (g->win_ending == 2)
            snprintf(lineBuf, sizeof(lineBuf), "The light takes him home.");
        else if (g->win_ending == 1)
            snprintf(lineBuf, sizeof(lineBuf), "Isaac chose the light.");
        else
            snprintf(lineBuf, sizeof(lineBuf), "All %d floors conquered!",
                     MAX_FLOORS);
        C2D_Text t4;
        gtext_parse(&t4, textBuf, lineBuf);
        C2D_TextOptimize(&t4);
        float lw = 0.0f, lh = 0.0f;
        C2D_TextGetDimensions(&t4, 0.45f, 0.45f, &lw, &lh);
        C2D_DrawText(&t4, C2D_WithColor, 200 - lw / 2, 196, 0,
                     0.45f, 0.45f, dim);

        snprintf(lineBuf, sizeof(lineBuf), "Final Score: %d   Items: %d",
                 g->score, g->player.item_count);
        C2D_Text t2;
        gtext_parse(&t2, textBuf, lineBuf);
        C2D_TextOptimize(&t2);
        C2D_TextGetDimensions(&t2, 0.45f, 0.45f, &lw, &lh);
        C2D_DrawText(&t2, C2D_WithColor, 200 - lw / 2, 210, 0,
                     0.45f, 0.45f, dim);
    }

    /* 7. "Press START" pulsing */
    {
        float pa = 0.6f + sinf(g->menu_timer * 0.1f) * 0.4f;
        if (pa < 0.0f) pa = 0.0f;
        if (pa > 1.0f) pa = 1.0f;
        C2D_Text t3;
        gtext_parse(&t3, textBuf, "Press START");
        C2D_TextOptimize(&t3);
        float pw = 0.0f, ph = 0.0f;
        C2D_TextGetDimensions(&t3, 0.45f, 0.45f, &pw, &ph);
        C2D_DrawText(&t3, C2D_WithColor, 200 - pw / 2, 224, 0, 0.45f, 0.45f,
                     C2D_Color32(238, 228, 206, (u8)(pa * 255)));
    }
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
    if (r->type == ROOM_DEVIL) {
        /* Devil deal: prices shown as heart containers */
        for (int i = 0; i < r->shop_count; i++) {
            ShopItem *si = &r->shop_items[i];
            if (!si->active) continue;
            for (int h = 0; h < si->cost; h++) {
                draw_heart(si->x - 6 + h * 13, si->y + 22, 11,
                           C2D_Color32(220, 30, 30, 255));
            }
        }
        return;
    }
    if (r->type != ROOM_SHOP && !r->is_black_market) return;

    for (int i = 0; i < r->shop_count; i++) {
        ShopItem *si = &r->shop_items[i];
        if (si->active) {
            /* Coin icon + price number; unaffordable = 50% alpha on both */
            int affordable = (g->player.coins >= si->cost);
            float af = affordable ? 1.0f : 0.5f;
            if (g_sprites_loaded && sheet_ui_items) {
                spr_draw_alpha(sheet_ui_items, ui_items_atlas_item_coin_idx,
                               si->x - 8, si->y + 18, 0.4f, 0.4f, af);
            }
            char price_str[8];
            snprintf(price_str, sizeof(price_str), "%d", si->cost);
            C2D_Text priceText;
            gtext_parse(&priceText, textBuf, price_str);
            C2D_TextOptimize(&priceText);
            C2D_DrawText(&priceText, C2D_WithColor,
                         si->x + 2, si->y + 14, 0, 0.4f, 0.4f,
                         C2D_Color32(238, 228, 206, (u8)(255 * af)));
        } else {
            /* "SOLD" text on empty pedestal: faint ink on a dark tag */
            C2D_DrawRectSolid(si->x - 12, si->y, 0, 28, 10,
                              C2D_Color32(20, 16, 14, 160));
            C2D_Text soldText;
            gtext_parse(&soldText, textBuf, "SOLD");
            C2D_TextOptimize(&soldText);
            C2D_DrawText(&soldText, C2D_WithColor,
                         si->x - 10, si->y + 2, 0, 0.35f, 0.35f,
                         INK_FAINT);
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

    case STATE_CHALLENGE_SELECT:
        render_challenge_select(g, textBuf);
        break;

    case STATE_CHARACTER_SELECT:
        render_character_select(g, textBuf);
        break;

    case STATE_DIFFICULTY_SELECT:
        render_difficulty_select(g, textBuf);
        break;

    case STATE_CONTROLS:
        render_controls(g, textBuf);
        break;

    case STATE_UNLOCKS:
        render_unlocks_screen(g, textBuf);
        break;

    case STATE_SETTINGS:
        render_settings(g, textBuf);
        break;

    case STATE_PLAYING: {
        /* Compute shake offset — R8 #39: frame-seeded deterministic jitter
           (never rand() in the render path; keeps stereo/replay safe and
           doesn't burn the RNG stream on cosmetics). Two incommensurate
           prime-ish frequencies per axis read as noise. */
        float sox = 0, soy = 0;
        if (g->shake_timer > 0 && g->shake_intensity > 0.1f) {
            float fs = (float)g->frame;
            sox = sinf(fs * 1.3f) * cosf(fs * 2.9f) * g->shake_intensity;
            soy = sinf(fs * 1.7f + 1.1f) * cosf(fs * 2.3f) * g->shake_intensity;
            C2D_ViewTranslate(sox, soy);
        }

        if (g->slide_timer > 0) {
            /* ── Rebirth-style sliding camera pan between rooms ──
               Old room slides out opposite the travel direction while the
               new room (player already positioned at its entrance) slides
               in. Entities/tears are skipped during the ~1/3s pan. */
            float st = 1.0f - (float)g->slide_timer / (float)SLIDE_FRAMES;
            st = st * st * (3.0f - 2.0f * st);   /* smoothstep ease */
            float sdx = (g->slide_dir == DIR_RIGHT) ? 1.0f :
                        (g->slide_dir == DIR_LEFT)  ? -1.0f : 0.0f;
            float sdy = (g->slide_dir == DIR_DOWN)  ? 1.0f :
                        (g->slide_dir == DIR_UP)    ? -1.0f : 0.0f;
            float ox_old = -sdx * st * TOP_SCREEN_WIDTH;
            float oy_old = -sdy * st * TOP_SCREEN_HEIGHT;

            C2D_ViewReset();
            C2D_ViewTranslate(sox + ox_old, soy + oy_old);
            render_room_at(g, g->slide_from_x, g->slide_from_y);

            C2D_ViewReset();
            C2D_ViewTranslate(sox + ox_old + sdx * TOP_SCREEN_WIDTH,
                              soy + oy_old + sdy * TOP_SCREEN_HEIGHT);
            render_room_at(g, g->dungeon.cur_x, g->dungeon.cur_y);
            render_player(g);

            /* Restore plain shake view for the overlays below */
            C2D_ViewReset();
            if (sox != 0 || soy != 0) C2D_ViewTranslate(sox, soy);
        } else {
            render_room(g);
            render_shop_prices(g, textBuf);  /* price text overlay on shop items */
            render_tears(g);
            render_enemy_shots(g);
            render_blood_particles(g);
            render_enemies(g);
            render_familiars(g);  /* E6: followers under the player */
            render_blue_flies(g); /* R8 (M8): friendly blue flies */
            render_player(g);
            render_knife(g);   /* D3: knife rides above the player sprite */
            render_laser(g);   /* D1/D2: beam over everything in the room */
            render_enemy_beam(g); /* E3: Satan/Lamb/Krampus Brimstone beam */
            render_vbeams(g);     /* R9: Isaac/angel light columns */
            render_pbeam(g);      /* R8: Head of Krampus player burst */
            /* R8 (M7): brief lights-down pulse as Krampus reveals himself
               (smooth fade-out, photosensitivity-safe — never strobes) */
            if (g->krampus_dim > 0) {
                int da = (int)(140.0f * ((float)g->krampus_dim / 40.0f));
                C2D_DrawRectSolid(0, 0, 0, TOP_SCREEN_WIDTH,
                                  TOP_SCREEN_HEIGHT,
                                  C2D_Color32(0, 0, 0, da));
            }
        }

        /* R8 #37: per-floor ambient tint + vignette (shared helper, also
           used by the STATE_PAUSED underlay for frame parity). */
        render_ambient_and_vignette(g);

        /* Red hurt pulse: screen edges flash red briefly when damaged */
        if (g->hurt_flash_timer > 0) {
            float hf = (float)g->hurt_flash_timer / (float)HURT_FLASH_FRAMES;
            int ha = (int)(90 * hf);
            u32 hc = C2D_Color32(180, 10, 10, ha);
            C2D_DrawRectSolid(0, 0, 0, TOP_SCREEN_WIDTH, 14, hc);
            C2D_DrawRectSolid(0, TOP_SCREEN_HEIGHT - 14, 0, TOP_SCREEN_WIDTH, 14, hc);
            C2D_DrawRectSolid(0, 14, 0, 14, TOP_SCREEN_HEIGHT - 28, hc);
            C2D_DrawRectSolid(TOP_SCREEN_WIDTH - 14, 14, 0, 14,
                              TOP_SCREEN_HEIGHT - 28, hc);
        }

        /* Curse: Darkness — R8 #33 rebuild. The old approach stacked
         * semi-transparent circles ON TOP of a full-screen black sheet;
         * with citro2d's src-alpha blending an alpha-0 "punch-out" circle
         * is a no-op, so the player area ended up ~97% black. Instead the
         * darkness is now built only from geometry OUTSIDE the player's
         * light: 4 edge rects beyond dim_r plus 4 concentric square ring
         * bands that fake the radial falloff from clear_r (nearly clear)
         * out to dim_r (heavily dark). Max darkness is capped at 200
         * alpha so the room stays barely readable (accessibility), and a
         * warm candle glow flickers near Isaac (Rebirth candlelit look). */
        if (g->active_curse == CURSE_DARKNESS) {
            float cx = g->player.x;
            float cy = g->player.y;
            const float clear_r = 55.0f;   /* nearly clear around Isaac */
            const float dim_r   = 110.0f;  /* heavy dark from here out */
            const int   MAX_A   = 200;     /* accessibility cap */
            u32 dark = C2D_Color32(0, 0, 0, MAX_A);
            const float W = TOP_SCREEN_WIDTH, H = TOP_SCREEN_HEIGHT;

            /* 4 edge rects covering everything outside the dim_r box */
            float x0 = cx - dim_r, x1 = cx + dim_r;
            float y0 = cy - dim_r, y1 = cy + dim_r;
            float bx0 = (x0 > 0) ? x0 : 0;
            float bx1 = (x1 < W) ? x1 : W;
            if (x0 > 0) C2D_DrawRectSolid(0, 0, 0, x0, H, dark);
            if (x1 < W) C2D_DrawRectSolid(x1, 0, 0, W - x1, H, dark);
            if (y0 > 0) C2D_DrawRectSolid(bx0, 0, 0, bx1 - bx0, y0, dark);
            if (y1 < H) C2D_DrawRectSolid(bx0, y1, 0, bx1 - bx0, H - y1, dark);

            /* 4 concentric square ring bands, darkest outermost, grading
               down toward the clear radius (rect approximation of rings) */
            static const int bandA[4] = { 170, 120, 72, 34 };
            const float bw = (dim_r - clear_r) / 4.0f;
            for (int bi = 0; bi < 4; bi++) {
                float outer = dim_r - bw * (float)bi;
                float inner = outer - bw;
                u32 bc = C2D_Color32(0, 0, 0, bandA[bi]);
                C2D_DrawRectSolid(cx - outer, cy - outer, 0,
                                  outer * 2.0f, bw, bc);            /* top */
                C2D_DrawRectSolid(cx - outer, cy + inner, 0,
                                  outer * 2.0f, bw, bc);            /* bottom */
                C2D_DrawRectSolid(cx - outer, cy - inner, 0,
                                  bw, inner * 2.0f, bc);            /* left */
                C2D_DrawRectSolid(cx + inner, cy - inner, 0,
                                  bw, inner * 2.0f, bc);            /* right */
            }

            /* Warm candlelight near the player: two soft glows with a
               gentle two-frequency flicker (deterministic, frame-seeded) */
            float flicker = sinf((float)g->frame * 0.21f) * 2.5f
                          + sinf((float)g->frame * 0.53f) * 1.5f;
            C2D_DrawCircleSolid(cx, cy, 0, clear_r * 0.8f + flicker,
                                C2D_Color32(255, 190, 110, 16));
            C2D_DrawCircleSolid(cx, cy, 0, clear_r * 0.45f + flicker * 0.6f,
                                C2D_Color32(255, 210, 140, 12));
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
            gtext_parse(&ctext, textBuf, banner);
            C2D_TextOptimize(&ctext);
            /* Paper strip with a BLOOD frame + BLOOD text.
               R8 #35: strip now ends at x=330 so it clears the top-right
               minimap (x335-398, y24-74) and the difficulty tag (~x340,
               y78) that it used to overlap for its ~3s of display. */
            u32 cframe = C2D_Color32(171, 22, 26, alpha);  /* BLOOD, alpha-scaled */
            C2D_DrawRectSolid(53, 64, 0, 278, 30, C2D_Color32(0, 0, 0, (alpha * 45) / 255));
            C2D_DrawRectSolid(48, 58, 0, 282, 2, cframe);
            C2D_DrawRectSolid(48, 90, 0, 282, 2, cframe);
            C2D_DrawRectSolid(48, 60, 0, 2, 30, cframe);
            C2D_DrawRectSolid(328, 60, 0, 2, 30, cframe);
            C2D_DrawRectSolid(50, 60, 0, 278, 30,
                              C2D_Color32(234, 221, 200, (alpha * 240) / 255));
            C2D_DrawText(&ctext, C2D_WithColor, 70, 68, 0, 0.6f, 0.6f, cframe);
        }

        /* Floor-intro nameplate: Rebirth-style floor title on entry */
        if (g->floor_intro_timer > 0 && g->boss_intro_timer <= 0) {
            int t = g->floor_intro_timer;
            float af = 1.0f;
            if (t > 130) af = (150 - t) / 20.0f;
            else if (t < 30) af = t / 30.0f;
            if (af < 0) af = 0;
            if (af > 1) af = 1;
            int fa = (int)(af * 235);

            const FloorInfo *fint = get_floor_info(g->current_floor);
            char fname[48];
            if (g->game_mode == MODE_INFINITE && g->infinite_loop > 0)
                snprintf(fname, sizeof(fname), "%s +%d", fint->name, g->infinite_loop);
            else
                snprintf(fname, sizeof(fname), "%s", fint->name);

            C2D_Text fiText;
            gtext_parse(&fiText, textBuf, fname);
            C2D_TextOptimize(&fiText);
            float fw = 0.0f, fh = 0.0f;
            C2D_TextGetDimensions(&fiText, 0.95f, 0.95f, &fw, &fh);
            float fx = (TOP_SCREEN_WIDTH - fw) / 2.0f;
            /* soft dark banner behind the title */
            C2D_DrawRectSolid(fx - 18, 96, 0, fw + 36, fh + 10,
                              C2D_Color32(0, 0, 0, fa / 2));
            C2D_DrawText(&fiText, C2D_WithColor, fx, 100, 0, 0.95f, 0.95f,
                         C2D_Color32(235, 225, 205, fa));
        }

        /* Room fade-in overlay */
        if (g->room_fade > 0) {
            int alpha = (g->room_fade * 16);
            if (alpha > 200) alpha = 200;
            C2D_DrawRectSolid(0, 0, 0, TOP_SCREEN_WIDTH, TOP_SCREEN_HEIGHT,
                              C2D_Color32(0, 0, 0, alpha));
        }

        /* Boss intro: Rebirth-style VS splash (portrait + name art) */
        if (g->boss_intro_timer > 0) {
            int alpha = 160;
            if (g->boss_intro_timer > BOSS_INTRO_FRAMES - 15) {
                alpha = (BOSS_INTRO_FRAMES - g->boss_intro_timer) * 11;
            } else if (g->boss_intro_timer < 15) {
                alpha = g->boss_intro_timer * 11;
            }
            if (alpha < 0) alpha = 0;
            if (alpha > 175) alpha = 175;
            float fade = (float)alpha / 175.0f;

            C2D_DrawRectSolid(0, 0, 0, TOP_SCREEN_WIDTH, TOP_SCREEN_HEIGHT,
                              C2D_Color32(30, 0, 0, alpha));

            /* Sides slide in during the first quarter second */
            float sp = (float)(BOSS_INTRO_FRAMES - g->boss_intro_timer) / 15.0f;
            if (sp > 1.0f) sp = 1.0f;
            float slide_off = (1.0f - sp) * 90.0f;

            /* Official portrait + name art where available */
            int pidx = -1, nidx = -1;
            switch (g->current_boss_type) {
            case ENEMY_BOSS_GEMINI:
                pidx = boss_splash_atlas_portrait_gemini_idx;
                nidx = boss_splash_atlas_name_gemini_idx; break;
            case ENEMY_BOSS_LARRY:
                pidx = boss_splash_atlas_portrait_larry_idx;
                nidx = boss_splash_atlas_name_larry_idx; break;
            case ENEMY_BOSS_FAMINE:
                pidx = boss_splash_atlas_portrait_famine_idx;
                nidx = boss_splash_atlas_name_famine_idx; break;
            case ENEMY_BOSS_PEEP:
                pidx = boss_splash_atlas_portrait_peep_idx;
                nidx = boss_splash_atlas_name_peep_idx; break;
            case ENEMY_BOSS_GURDY:
                pidx = boss_splash_atlas_portrait_gurdy_idx;
                nidx = boss_splash_atlas_name_gurdy_idx; break;
            default: break;
            }

            if (g_sprites_loaded) {
                /* Player on the left, sliding in */
                spr_draw_alpha(sheet_sprites, player_sprite_idx(DIR_RIGHT, 0),
                               95.0f - slide_off, 115.0f, 2.4f, 2.4f, fade);

                /* Boss on the right: official portrait, else in-game sprite */
                if (pidx >= 0 && sheet_boss_splash) {
                    spr_draw_alpha(sheet_boss_splash, pidx,
                                   305.0f + slide_off, 115.0f, 1.0f, 1.0f, fade);
                } else {
                    /* Fallback: in-game sprite on a dark blood pool circle */
                    C2D_DrawCircleSolid(305.0f + slide_off, 115.0f, 0, 34.0f,
                                        C2D_Color32(110, 12, 16, (int)(120 * fade)));
                    spr_draw_alpha(sheet_sprites,
                                   boss_sprite_idx(g->current_boss_type),
                                   305.0f + slide_off, 115.0f, 1.6f, 1.6f, fade);
                }
            }

            /* "VS" center text, pulsing, bone-white with black drop shadow */
            C2D_Text vsText;
            gtext_parse(&vsText, textBuf, "VS");
            C2D_TextOptimize(&vsText);
            float pulseScale = 1.1f + sinf((float)g->frame * 0.3f) * 0.12f;
            C2D_DrawText(&vsText, C2D_WithColor, 187, 101, 0,
                         pulseScale, pulseScale,
                         C2D_Color32(0, 0, 0, (int)(200 * fade)));
            C2D_DrawText(&vsText, C2D_WithColor, 186, 100, 0,
                         pulseScale, pulseScale,
                         C2D_Color32(238, 228, 206, (int)(240 * fade)));

            /* Boss name: official art, else INK text on a torn paper banner */
            if (nidx >= 0 && sheet_boss_splash && g_sprites_loaded) {
                spr_draw_alpha(sheet_boss_splash, nidx,
                               TOP_SCREEN_WIDTH / 2.0f, 195.0f, 1.0f, 1.0f, fade);
            } else if (g->boss_name) {
                C2D_Text bnText;
                gtext_parse(&bnText, textBuf, g->boss_name);
                C2D_TextOptimize(&bnText);
                float tw = 0.0f, th = 0.0f;
                C2D_TextGetDimensions(&bnText, 0.6f, 0.6f, &tw, &th);
                float cx = TOP_SCREEN_WIDTH / 2.0f;
                draw_paper_panel(cx - tw / 2 - 14, 182, tw + 28, 24,
                                 (u8)(fade * 255));
                C2D_DrawText(&bnText, C2D_WithColor, cx - tw / 2,
                             182 + (24 - th) / 2, 0, 0.6f, 0.6f,
                             C2D_Color32(55, 43, 33, (int)(255 * fade)));
            }
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
                gtext_parse(&pickedText, textBuf, pbuf);
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
                gtext_parse(&cleared, textBuf, "TRAPDOOR OPEN!");
                C2D_TextOptimize(&cleared);
                C2D_DrawText(&cleared, C2D_WithColor, 140, ROOM_BOTTOM - 18, 0,
                             0.4f, 0.4f, C2D_Color32(255, 200, 50, 200));
            } else {
                gtext_parse(&cleared, textBuf, "CLEARED");
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
                gtext_parse(&itemName, textBuf, def->name);
                C2D_TextOptimize(&itemName);
                C2D_DrawText(&itemName, C2D_WithColor,
                            current_room(g)->pedestal.x - 30,
                            current_room(g)->pedestal.y + 16, 0,
                            0.35f, 0.35f, C2D_Color32(255, 255, 200, 200));
            }
        } else if (current_room(g)->pedestal.active && g->active_curse == CURSE_BLIND) {
            C2D_Text qm;
            gtext_parse(&qm, textBuf, "???");
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
        /* Render game underneath dimmed — R8 #37: mirror the PLAYING
           frame (shop prices + ambient tint + vignette) so pausing
           doesn't visibly pop the room's color grade. */
        render_room(g);
        render_shop_prices(g, textBuf);
        render_tears(g);
        render_enemy_shots(g);
        render_blood_particles(g);
        render_enemies(g);
        render_familiars(g);
        render_blue_flies(g);   /* R8 (M8) */
        render_player(g);
        render_knife(g);
        render_laser(g);
        render_enemy_beam(g);
        render_vbeams(g);   /* R9: Isaac's light columns under the pause dim */
        render_pbeam(g);    /* R8: player burst under the pause dim */
        render_ambient_and_vignette(g);
        render_hud(g, textBuf);
        /* Dim overlay */
        C2D_DrawRectSolid(0, 0, 0, TOP_SCREEN_WIDTH, TOP_SCREEN_HEIGHT,
                          DIM_BLACK140);

        /* Round 7: pause note — a small paper card over the dimmed game */
        draw_paper_panel(120, 52, 160, 130, 255);

        /* "PAUSED" centered with a blood scratch under it */
        C2D_Text pausedText;
        gtext_parse(&pausedText, textBuf, "PAUSED");
        C2D_TextOptimize(&pausedText);
        {
            float pw = 0.0f, ph = 0.0f;
            C2D_TextGetDimensions(&pausedText, 0.8f, 0.8f, &pw, &ph);
            C2D_DrawText(&pausedText, C2D_WithColor, 200 - pw / 2, 62, 0,
                         0.8f, 0.8f, INK);
            draw_doodle_scratch(200 - pw / 2, 84, 200 + pw / 2, 86, BLOOD);
        }

        /* Run stats as note lines */
        {
            const FloorInfo *fi = get_floor_info(g->current_floor);
            int psecs = g->play_time_frames / 60;
            char pline[48];
            float py = 94.0f;
            const float ppitch = 14.0f;

            snprintf(pline, sizeof(pline), "%s", fi->name);
            for (int li = 0; li < 4; li++) {
                if (li == 1)
                    snprintf(pline, sizeof(pline), "Kills: %d", g->kills);
                else if (li == 2)
                    snprintf(pline, sizeof(pline), "Items: %d",
                             g->player.item_count);
                else if (li == 3)
                    snprintf(pline, sizeof(pline), "Time: %02d:%02d",
                             psecs / 60, psecs % 60);
                C2D_Text plt;
                gtext_parse(&plt, textBuf, pline);
                C2D_TextOptimize(&plt);
                C2D_DrawText(&plt, C2D_WithColor, 134, py, 0, 0.4f, 0.4f,
                             INK_FAINT);
                py += ppitch;
            }
        }

        /* Resume hint at the panel bottom */
        C2D_Text resumeText;
        gtext_parse(&resumeText, textBuf, "START or B: Resume");
        C2D_TextOptimize(&resumeText);
        C2D_DrawText(&resumeText, C2D_WithColor, 132, 164, 0, 0.4f, 0.4f,
                     INK_FAINT);

        /* Tiny dead-eye doodle face, bottom-right of the card */
        {
            float fx = 258.0f, fy = 160.0f;
            C2D_DrawCircleSolid(fx, fy, 0, 5.0f, PAPER_DARK);
            C2D_DrawRectSolid(fx - 1.8f, fy - 1.0f, 0, 1.2f, 1.5f, INK);
            C2D_DrawRectSolid(fx + 0.6f, fy - 1.0f, 0, 1.2f, 1.5f, INK);
        }
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
        /* Parchment background to match top screen (shared palette) */
        u32 bg_col      = PAPER;
        u32 text_col    = INK;
        u32 text_dim    = INK_FAINT;
        u32 doodle_col  = C2D_Color32(160, 140, 120, 200);
        C2D_DrawRectSolid(0, 0, 0, BOT_SCREEN_WIDTH, BOT_SCREEN_HEIGHT, bg_col);

        /* Subtle paper grain */
        for (int i = 0; i < 30; i++) {
            float gx = (i * 47) % BOT_SCREEN_WIDTH;
            float gy = (i * 31) % BOT_SCREEN_HEIGHT;
            C2D_DrawRectSolid(gx, gy, 0, 1, 1, C2D_Color32(219, 204, 178, 120));
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
        gtext_parse(&t1, textBuf, "The Binding of Isaac");
        C2D_TextOptimize(&t1);
        C2D_DrawText(&t1, C2D_WithColor, 60, 30, 0, 0.7f, 0.7f, text_col);

        C2D_Text sub;
        gtext_parse(&sub, textBuf, "REBIRTH  -  3DS Edition");
        C2D_TextOptimize(&sub);
        C2D_DrawText(&sub, C2D_WithColor, 75, 60, 0, 0.5f, 0.5f, text_dim);

        /* Decorative divider line under subtitle */
        C2D_DrawRectSolid(60, 82, 0, BOT_SCREEN_WIDTH - 120, 1, text_dim);

        char floorIntro[48];
        snprintf(floorIntro, sizeof(floorIntro), "%d floors of darkness await...", MAX_FLOORS);
        gtext_parse(&t2, textBuf, floorIntro);
        C2D_TextOptimize(&t2);
        C2D_DrawText(&t2, C2D_WithColor, 50, 100, 0, 0.45f, 0.45f, text_col);

        gtext_parse(&t3, textBuf, "Collect items to grow stronger.");
        C2D_TextOptimize(&t3);
        C2D_DrawText(&t3, C2D_WithColor, 50, 122, 0, 0.45f, 0.45f, text_col);

        gtext_parse(&t4, textBuf, "Defeat bosses to descend.");
        C2D_TextOptimize(&t4);
        C2D_DrawText(&t4, C2D_WithColor, 50, 144, 0, 0.45f, 0.45f, text_col);

        gtext_parse(&t5, textBuf, "Defeat Mega Satan to win!");
        C2D_TextOptimize(&t5);
        C2D_DrawText(&t5, C2D_WithColor, 50, 166, 0, 0.45f, 0.45f, text_col);

        /* Decorative divider near bottom */
        C2D_DrawRectSolid(80, 188, 0, BOT_SCREEN_WIDTH - 160, 1, text_dim);
        return;
    }

    /* In-game / paused etc: dark background for the minimap. R5: skipped
       during the floor transition — that branch repaints the whole screen
       with PAPER anyway, so the dark rect would be a dead draw. */
    if (g->state != STATE_FLOOR_TRANSITION) {
        u32 bgCol = C2D_Color32(25, 25, 25, 255);
        C2D_DrawRectSolid(0, 0, 0, BOT_SCREEN_WIDTH, BOT_SCREEN_HEIGHT, bgCol);
    }

    if (g->state == STATE_FLOOR_TRANSITION) {
        /* Round 7: paper page instead of the dark panel */
        C2D_DrawRectSolid(0, 0, 0, BOT_SCREEN_WIDTH, BOT_SCREEN_HEIGHT, PAPER);
        for (int i = 0; i < 30; i++) {
            float gx = (i * 47) % BOT_SCREEN_WIDTH;
            float gy = (i * 31) % BOT_SCREEN_HEIGHT;
            C2D_DrawRectSolid(gx, gy, 0, 1, 1,
                              C2D_Color32(219, 204, 178, 120));
        }
        C2D_Text info;
        gtext_parse(&info, textBuf, "Descending...");
        C2D_TextOptimize(&info);
        C2D_DrawText(&info, C2D_WithColor, 100, 110, 0, 0.6f, 0.6f, INK);
        return;
    }

    if (g->state != STATE_PLAYING) {
        C2D_Text info;
        gtext_parse(&info, textBuf, "Press START to continue");
        C2D_TextOptimize(&info);
        C2D_DrawText(&info, C2D_WithColor, 60, 110, 0, 0.55f, 0.55f,
                     C2D_Color32(180, 180, 180, 255));
        return;
    }

    /* Curse of the Lost: hide the minimap entirely.
       Show only a faint, atmospheric notice instead of a placeholder. */
    if (g->active_curse == CURSE_LOST) {
        C2D_Text lost1, lost2;
        /* Round 7: paper page with a faint empty map frame, INK notice and
           a blood scribble where the map should be */
        C2D_DrawRectSolid(0, 0, 0, BOT_SCREEN_WIDTH, BOT_SCREEN_HEIGHT, PAPER);
        for (int i = 0; i < 30; i++) {
            float gx = (i * 47) % BOT_SCREEN_WIDTH;
            float gy = (i * 31) % BOT_SCREEN_HEIGHT;
            C2D_DrawRectSolid(gx, gy, 0, 1, 1, C2D_Color32(219, 204, 178, 120));
        }
        {
            float fx = (BOT_SCREEN_WIDTH - DUNGEON_W * 18.0f) / 2 - 8;
            float fy = 22;
            float fw = DUNGEON_W * 18.0f + 16;
            float fh = DUNGEON_H * 18.0f + 12;
            C2D_DrawRectSolid(fx, fy, 0, fw, 1, PAPER_EDGE);
            C2D_DrawRectSolid(fx, fy + fh - 1, 0, fw, 1, PAPER_EDGE);
            C2D_DrawRectSolid(fx, fy, 0, 1, fh, PAPER_EDGE);
            C2D_DrawRectSolid(fx + fw - 1, fy, 0, 1, fh, PAPER_EDGE);
        }
        gtext_parse(&lost1, textBuf, "Map Hidden");
        C2D_TextOptimize(&lost1);
        C2D_DrawText(&lost1, C2D_WithColor, 100, 90, 0, 0.7f, 0.7f, INK);
        draw_doodle_scratch(80, 45, 245, 115, BLOOD);
        draw_doodle_scratch(240, 40, 90, 120, BLOOD_DARK);
        gtext_parse(&lost2, textBuf, "(Curse of the Lost)");
        C2D_TextOptimize(&lost2);
        C2D_DrawText(&lost2, C2D_WithColor, 80, 130, 0, 0.5f, 0.5f, INK_FAINT);
        return;
    }

    /* === Minimap === */
    Dungeon *d = &g->dungeon;

    /* Round 7: full paper page — grain + two blotches */
    C2D_DrawRectSolid(0, 0, 0, BOT_SCREEN_WIDTH, BOT_SCREEN_HEIGHT, PAPER);
    for (int i = 0; i < 30; i++) {
        float gx = (i * 47) % BOT_SCREEN_WIDTH;
        float gy = (i * 31) % BOT_SCREEN_HEIGHT;
        C2D_DrawRectSolid(gx, gy, 0, 1, 1, C2D_Color32(219, 204, 178, 120));
    }
    C2D_DrawEllipseSolid(24, 168, 0, 44, 26, C2D_Color32(219, 204, 178, 30));
    C2D_DrawEllipseSolid(236, 18, 0, 36, 22, C2D_Color32(219, 204, 178, 30));

    float cellW = 18;
    float cellH = 18;
    float mapOffX = (BOT_SCREEN_WIDTH - DUNGEON_W * cellW) / 2;
    float mapOffY = 28;

    /* Inner paper panel around the map area */
    draw_paper_panel(mapOffX - 8, mapOffY - 6, DUNGEON_W * cellW + 16,
                     DUNGEON_H * cellH + 12, 255);

    /* Floor title: INK with a blood scratch underline (no more '=' signs) */
    C2D_Text mapTitle;
    const FloorInfo *fi = get_floor_info(g->current_floor);
    char titleBuf[48];
    snprintf(titleBuf, sizeof(titleBuf), "%s", fi->name);
    gtext_parse(&mapTitle, textBuf, titleBuf);
    C2D_TextOptimize(&mapTitle);
    float titleW = 0.0f, titleH = 0.0f;
    C2D_TextGetDimensions(&mapTitle, 0.5f, 0.5f, &titleW, &titleH);
    float titleX = (BOT_SCREEN_WIDTH - titleW) / 2;
    C2D_DrawText(&mapTitle, C2D_WithColor, titleX, 1, 0, 0.5f, 0.5f, INK);
    draw_doodle_scratch(titleX, 16, titleX + titleW, 17, BLOOD);

    for (int ry = 0; ry < DUNGEON_H; ry++) {
        for (int rx = 0; rx < DUNGEON_W; rx++) {
            Room *rm = &d->rooms[ry][rx];
            if (rm->type == ROOM_NONE) continue;

            float x = mapOffX + rx * cellW;
            float y = mapOffY + ry * cellH;
            int isCurrent = (rx == d->cur_x && ry == d->cur_y);

            if (!rm->visited) {
                /* Show unvisited rooms as a dim 1px outline only (no fill, no "?")
                   if adjacent to a visited room */
                int adjVis = 0;
                if (ry > 0 && d->rooms[ry-1][rx].visited && d->rooms[ry-1][rx].doors[1]) adjVis = 1;
                if (ry < DUNGEON_H-1 && d->rooms[ry+1][rx].visited && d->rooms[ry+1][rx].doors[0]) adjVis = 1;
                if (rx > 0 && d->rooms[ry][rx-1].visited && d->rooms[ry][rx-1].doors[3]) adjVis = 1;
                if (rx < DUNGEON_W-1 && d->rooms[ry][rx+1].visited && d->rooms[ry][rx+1].doors[2]) adjVis = 1;

                /* Secret rooms only show if revealed */
                if (rm->type == ROOM_SECRET && !rm->secret_revealed) adjVis = 0;

                if (adjVis) {
                    u32 dimOutline = INK_FAINT;
                    C2D_DrawRectSolid(x + 2, y + 2, 0, cellW - 4, 1, dimOutline);
                    C2D_DrawRectSolid(x + 2, y + cellH - 3, 0, cellW - 4, 1, dimOutline);
                    C2D_DrawRectSolid(x + 2, y + 2, 0, 1, cellH - 4, dimOutline);
                    C2D_DrawRectSolid(x + cellW - 3, y + 2, 0, 1, cellH - 4, dimOutline);
                }
                continue;
            }

            u32 roomCol;
            if (isCurrent) {
                /* Pulsing bright fill for current room (kept math; biased
                   warm so it reads bone-white on the paper page) */
                float pulse = sinf((float)g->frame * 0.12f) * 30.0f;
                int v = 225 + (int)pulse;
                roomCol = C2D_Color32(v, v - 10, v - 30, 255);
            } else {
                /* Round 7: uniform dark-ink cell — type identity comes from
                   the marker glyphs below */
                roomCol = C2D_Color32(64, 52, 40, 235);
            }

            /* Square-ish cell: dark outline rect behind, fill inset inside it —
               gives a slightly rounded, tightened-up look versus a flat rect. */
            u32 outlineCol = isCurrent ? C2D_Color32(238, 228, 206, 255)
                                        : C2D_Color32(15, 15, 15, 255);
            C2D_DrawRectSolid(x + 1, y + 1, 0, cellW - 2, cellH - 2, outlineCol);
            C2D_DrawRectSolid(x + 2, y + 2, 0, cellW - 4, cellH - 4, roomCol);

            /* Special-room markers: small distinct shape/color centered in the cell */
            float icx = x + cellW / 2;
            float icy = y + cellH / 2;

            if (rm->type == ROOM_BOSS && !isCurrent) {
                /* Blood skull-ish marker: multi-rect */
                C2D_DrawRectSolid(icx - 2, icy - 2, 0, 4, 3, BLOOD);
                C2D_DrawRectSolid(icx - 1, icy + 1, 0, 1, 1, BLOOD);
                C2D_DrawRectSolid(icx,     icy + 1, 0, 1, 1, BLOOD);
            } else if (rm->type == ROOM_TREASURE && !isCurrent) {
                /* Gold dot */
                C2D_DrawCircleSolid(icx, icy, 0, 2.5f, GOLD_CHARGE);
            } else if (rm->type == ROOM_SHOP && !isCurrent) {
                /* Blue dot */
                C2D_DrawCircleSolid(icx, icy, 0, 2.5f, C2D_Color32(100, 180, 255, 255));
            } else if (rm->type == ROOM_CURSE && !isCurrent) {
                /* Purple dot */
                C2D_DrawCircleSolid(icx, icy, 0, 2.5f, C2D_Color32(220, 100, 220, 255));
            } else if (rm->type == ROOM_DEVIL && !isCurrent) {
                /* Dark blood dot */
                C2D_DrawCircleSolid(icx, icy, 0, 2.5f, BLOOD_DARK);
            } else if (rm->type == ROOM_ANGEL && !isCurrent) {
                /* Bone-white dot */
                C2D_DrawCircleSolid(icx, icy, 0, 2.5f, C2D_Color32(238, 228, 206, 255));
            } else if (rm->type == ROOM_SACRIFICE && !isCurrent) {
                /* Small red cross marker */
                C2D_DrawRectSolid(icx - 2, icy, 0, 4, 1, C2D_Color32(230, 60, 60, 255));
                C2D_DrawRectSolid(icx, icy - 2, 0, 1, 4, C2D_Color32(230, 60, 60, 255));
            } else if (rm->type == ROOM_BOSSRUSH && !isCurrent) {
                /* Orange skull-ish marker (same shape as boss, different color) */
                C2D_DrawRectSolid(icx - 2, icy - 2, 0, 4, 3, C2D_Color32(255, 150, 40, 255));
                C2D_DrawRectSolid(icx - 1, icy + 1, 0, 1, 1, C2D_Color32(255, 150, 40, 255));
                C2D_DrawRectSolid(icx,     icy + 1, 0, 1, 1, C2D_Color32(255, 150, 40, 255));
            } else if (rm->type == ROOM_ARCADE && !isCurrent) {
                /* Pink dot */
                C2D_DrawCircleSolid(icx, icy, 0, 2.5f, C2D_Color32(255, 100, 200, 255));
            } else if (rm->type == ROOM_LIBRARY && !isCurrent) {
                /* Purple dot */
                C2D_DrawCircleSolid(icx, icy, 0, 2.5f, C2D_Color32(150, 110, 230, 255));
            } else if (rm->type == ROOM_SECRET && !isCurrent) {
                /* Faint dot */
                C2D_DrawCircleSolid(icx, icy, 0, 1.5f, C2D_Color32(150, 150, 150, 160));
            } else if (rm->cleared && rm->type != ROOM_START && !isCurrent) {
                C2D_DrawCircleSolid(icx, icy, 0, 1.5f, C2D_Color32(80, 220, 80, 200));
            }

            /* Current room: pulsing bone-white outline on top of the cell */
            if (isCurrent) {
                float pulse = sinf((float)g->frame * 0.12f) * 0.4f + 0.6f;
                u8 a = (u8)(pulse * 255);
                u32 markerCol = C2D_Color32(238, 228, 206, a);
                C2D_DrawRectSolid(x, y, 0, cellW, 1, markerCol);
                C2D_DrawRectSolid(x, y + cellH - 1, 0, cellW, 1, markerCol);
                C2D_DrawRectSolid(x, y, 0, 1, cellH, markerCol);
                C2D_DrawRectSolid(x + cellW - 1, y, 0, 1, cellH, markerCol);
            }

            /* Door connections */
            u32 connCol = INK_FAINT;
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

    /* Player stats section — Round 7: 2x2 pip-row grid with tiny doodle
       icons instead of colored bars */
    float statsY = mapOffY + DUNGEON_H * cellH + 16;
    C2D_Text statsTitle;
    gtext_parse(&statsTitle, textBuf, "STATS");
    C2D_TextOptimize(&statsTitle);
    C2D_DrawText(&statsTitle, C2D_WithColor, 10, statsY, 0, 0.4f, 0.4f, INK);

    Player *p = &g->player;
    statsY += 14;
    float statX = 10;
    float col2X = 150;

    /* DMG: tiny blood heart icon + pips */
    draw_heart(statX + 3, statsY + 3, 7, BLOOD);
    draw_stat_pips(textBuf, statX + 10, statsY, "DMG",
                   (int)(p->stats.damage / 5.0f * 6), 6);

    /* SPD: tiny ink triangle + pips */
    C2D_DrawTriangle(col2X,     statsY + 7, INK,
                     col2X + 6, statsY + 7, INK,
                     col2X + 3, statsY + 1, INK, 0);
    draw_stat_pips(textBuf, col2X + 10, statsY, "SPD",
                   (int)(p->stats.speed / 4.0f * 6), 6);

    statsY += 14;

    /* RATE: tiny tear circle + pips */
    C2D_DrawCircleSolid(statX + 3, statsY + 4, 0, 3.0f,
                        C2D_Color32(90, 140, 190, 255));
    draw_stat_pips(textBuf, statX + 10, statsY, "RATE",
                   (int)((p->stats.fire_rate + 2.0f) / 6.0f * 6), 6);

    /* RNG: tiny ink arrow (shaft + tip) + pips */
    C2D_DrawRectSolid(col2X - 1, statsY + 3, 0, 7, 2, INK);
    C2D_DrawTriangle(col2X + 6, statsY + 1, INK,
                     col2X + 6, statsY + 7, INK,
                     col2X + 9, statsY + 4, INK, 0);
    draw_stat_pips(textBuf, col2X + 10, statsY, "RNG",
                   (int)(p->stats.range / 300.0f * 6), 6);

    statsY += 14;

    /* Last item collected */
    if (p->item_count > 0) {
        const ItemDef *last = get_item_def(p->items[p->item_count - 1]);
        if (last) {
            C2D_Text itemLbl;
            char ibuf[48];
            snprintf(ibuf, sizeof(ibuf), "Last: %s", last->name);
            gtext_parse(&itemLbl, textBuf, ibuf);
            C2D_TextOptimize(&itemLbl);
            C2D_DrawText(&itemLbl, C2D_WithColor, statX, statsY, 0, 0.35f, 0.35f,
                        INK);
        }
    }

    /* Controls reminder */
    statsY += 16;
    C2D_Text ctrl;
    gtext_parse(&ctrl, textBuf, "ABXY: Shoot | D-Pad: Move | SELECT: Bomb");
    C2D_TextOptimize(&ctrl);
    C2D_DrawText(&ctrl, C2D_WithColor, 15, statsY, 0, 0.35f, 0.35f,
                 C2D_Color32(122, 99, 75, 160));
}

/* ================================================================
 * Pill / Tarot / Champion / Creep helpers
 * ================================================================ */

static const char *pill_color_names[PILL_EFFECT_COUNT] = {
    "Red Pill", "Blue Pill", "Yellow Pill", "Green Pill",
    "Orange Pill", "Pink Pill", "White Pill", "Black Pill",
    "Purple Pill", "Cyan Pill", "Brown Pill", "Grey Pill",
    "Magenta Pill"
};

const char *pill_color_name(int color_idx) {
    if (color_idx < 0 || color_idx >= PILL_EFFECT_COUNT) return "?";
    return pill_color_names[color_idx];
}

static const char *pill_effect_text[PILL_EFFECT_COUNT] = {
    "Health Up", "Health Down", "Speed Up", "Speed Down",
    "Tears Up", "Tears Down", "Range Up", "Luck Up",
    "Full Health", "Telepills", "Bad Trip", "Bombs Are Key",
    "Explosive Diarrhea"
};

const char *pill_name(PillEffect e, int known) {
    if (e < 0 || e >= PILL_EFFECT_COUNT) return "?";
    if (known) return pill_effect_text[e];
    /* Lookup color */
    return "Unknown Pill";
}

static const char *tarot_names[TAROT_COUNT] = {
    "The Fool", "The Magician", "The High Priestess", "The Emperor",
    "The Hierophant", "The Lovers", "The Tower", "The World",
    "Death", "The Star", "The Sun", "The Hanged Man",
    /* R8 (M6): the 10 missing major arcana — order MUST match the enum */
    "The Empress", "The Chariot", "Justice", "The Hermit",
    "Wheel of Fortune", "Strength", "The Devil", "Temperance",
    "The Moon", "Judgement"
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
                /* Route through player_absorb_dmg so soul hearts + Holy Mantle
                   are honoured (they were previously ignored by creep). */
                p->hp -= player_absorb_dmg(p, g->creep[i].dmg);
                p->iframes = PLAYER_IFRAMES;
                g->hitstop = 3;
                audio_play(SFX_HURT);
                trigger_shake(g, 2.0f, 8);
                if (player_check_death(p)) {
                    g->state = STATE_GAMEOVER;
                    audio_play(SFX_PLAYER_DEATH);
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
    /* Warping into a secret room must open its doors (see reveal_secret_room) */
    reveal_secret_room(g, g->dungeon.cur_x, g->dungeon.cur_y);
    /* NOTE: arcade_slot_used deliberately NOT reset — once per room, or
       warping in and out prints infinite items */
    if (!nr->enemies_spawned) room_spawn_enemies(g, nr);

    /* Clear projectiles, creep and reset Holy Mantle */
    for (int i = 0; i < MAX_TEARS; i++)        g->tears[i].active = 0;
    for (int i = 0; i < MAX_ENEMY_SHOTS; i++)  g->enemy_shots[i].active = 0;
    for (int i = 0; i < MAX_CREEP; i++)        g->creep[i].active = 0;
    /* A live beam/knife/bomb must not persist across a warp (Phase D) */
    for (int i = 0; i < MAX_BOMBS; i++)        g->bombs[i].active = 0;
    g->laser_active = 0;
    g->laser_timer = 0;
    g->laser_charge = 0;
    g->knife_state = 0;
    g->knife_hit_cd = 0;

    if (g->player.stats.flags & ITEM_FLAG_MANTLE) g->player.holy_mantle_active = 1;
    /* R10 (C4) Samson: Bloody Lust stacks reset on room change (warps too) */
    if (g->player.samson_hits) {
        g->player.samson_hits = 0;
        recalc_player_stats(&g->player);
    }

    /* Warping out mid-Boss-Rush (or mid-boss-fight) must not leave stale
       boss/wave state behind — same reset as the door re-entry path.
       R8 #25: wave progress itself stays in the Room struct. */
    g->boss_active = 0;
    g->bossrush_active = 0;
    g->bossrush_spawn_timer = 0;
    /* E3: a live enemy beam must not persist across a warp (R9: nor the
       Lamb's cross flag or Isaac's light columns) */
    g->ebeam_state = 0;
    g->ebeam_timer = 0;
    g->ebeam_cross = 0;
    g->vbeam_state = 0;
    g->vbeam_timer = 0;
    g->vbeam_count = 0;
    g->pbeam_timer = 0;
    /* R8 (M7): warping into an armed Krampus devil room also starts the
       ambush countdown (teleport / telepills entry path). */
    if (nr->type == ROOM_DEVIL && nr->krampus_state == 1)
        g->krampus_timer = 45;

    /* Center player and brief fade */
    g->player.x = (ROOM_LEFT + ROOM_RIGHT) / 2.0f;
    g->player.y = (ROOM_TOP + ROOM_BOTTOM) / 2.0f;
    g->player.iframes = 30;
    g->room_fade = 16;

    /* E6: snap the familiar trail to the warped-in position */
    familiars_reset_trail(g);

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
    } else {
        /* R8 #21: warping OUT of a boss fight (Fool/Emperor/Telepills) left
           MUS_BOSS playing forever. Any non-boss destination gets the floor
           track back; music_play no-ops if it's already the current track. */
        music_play(music_for_floor(g->current_floor));
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
            /* Teleport to random room. B1: detonate any pending black-heart
               burst here, before cur_x/cur_y move (do_warp_cleanup is only
               called after the switch). */
            drain_black_burst(g);
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
        case PILL_BAD_TRIP:
            /* Bad Trip: lose 1 heart (can kill, like any other damage) */
            p->hp -= player_absorb_dmg(p, 1);
            audio_play(SFX_HURT);
            if (player_check_death(p)) {
                audio_play(SFX_PLAYER_DEATH);
                g->state = STATE_GAMEOVER;
            }
            break;
        case PILL_BOMBS_ARE_KEY: {
            /* Swap bomb and key counts */
            int tmp = p->bombs;
            p->bombs = p->keys;
            p->keys = tmp;
            break;
        }
        case PILL_EXPLOSIVE_DIARRHEA: {
            /* Drop a spread of LIVE troll bombs around the player — they
               explode on the normal fuse and can hurt you */
            Room *r = current_room(g);
            if (r) {
                for (int i = 0; i < 5; i++) {
                    float ang = randf(0.0f, 6.28f);
                    float dst = randf(20.0f, 44.0f);
                    if (!spawn_troll_bomb(g, p->x + cosf(ang) * dst,
                                          p->y + sinf(ang) * dst)) break;
                }
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
            /* Teleport back to starting room. B1: pending burst fires in
               the room being left, before the switch. */
            drain_black_burst(g);
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
                /* B1: pending burst fires in the room being left */
                drain_black_burst(g);
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
            /* Spawn live troll bombs around the player (damages player too) */
            for (int i = 0; i < 6; i++) {
                float ang = randf(0.0f, 6.28f);
                float dst = randf(24.0f, 52.0f);
                if (!spawn_troll_bomb(g, p->x + cosf(ang) * dst,
                                      p->y + sinf(ang) * dst)) break;
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
        case TAROT_DEATH:
            /* Damage all enemies in the room heavily (shared helper routes
               deaths through kill_enemy; splits spawned by the deaths are
               not re-hit by the same card). */
            if (r) {
                damage_all_enemies(g, 6.0f);
                trigger_shake(g, 4.0f, 16);
            }
            break;
        case TAROT_STAR:
            /* Full map reveal + spawn a heart */
            g->map_revealed = 1;
            for (int yy = 0; yy < DUNGEON_H; yy++) {
                for (int xx = 0; xx < DUNGEON_W; xx++) {
                    if (g->dungeon.rooms[yy][xx].type != ROOM_NONE) {
                        g->dungeon.rooms[yy][xx].visited = 1;
                    }
                }
            }
            if (r) spawn_heart(r, p->x + 12, p->y, HEART_RED_FULL);
            break;
        case TAROT_SUN:
            /* Full heal + full map reveal */
            p->hp = p->stats.max_hp;
            g->map_revealed = 1;
            for (int yy = 0; yy < DUNGEON_H; yy++) {
                for (int xx = 0; xx < DUNGEON_W; xx++) {
                    if (g->dungeon.rooms[yy][xx].type != ROOM_NONE) {
                        g->dungeon.rooms[yy][xx].visited = 1;
                    }
                }
            }
            break;
        case TAROT_HANGED_MAN:
            /* Spawn two soul hearts */
            if (r) {
                spawn_heart(r, p->x + 12, p->y, HEART_SOUL);
                spawn_heart(r, p->x - 12, p->y, HEART_SOUL);
            }
            break;
        /* --- R8 (M6): the 10 new arcana --- */
        case TAROT_EMPRESS:
            /* Mother's blessing: +0.3 dmg +0.2 spd for ~10s (Belial-style
               timed buff via the card_* fields recalc reads). */
            p->card_dmg_bonus += 0.3f;
            p->card_dmg_timer = 600;
            p->card_spd_bonus += 0.2f;
            p->card_spd_timer = 600;
            recalc_player_stats(p);
            break;
        case TAROT_CHARIOT:
            /* 6 seconds of invincibility (the iframes machinery already
               gates contact, shots, creep and blasts) + a speed burst. */
            if (p->iframes < 360) p->iframes = 360;
            p->card_spd_bonus += 0.3f;
            if (p->card_spd_timer < 360) p->card_spd_timer = 360;
            recalc_player_stats(p);
            break;
        case TAROT_JUSTICE:
            /* One of each: coin + bomb + key + half heart */
            if (r) {
                spawn_consumable(r, p->x - 18, p->y - 12, PICKUP_COIN);
                spawn_consumable(r, p->x + 18, p->y - 12, PICKUP_BOMB);
                spawn_consumable(r, p->x - 18, p->y + 14, PICKUP_KEY);
                spawn_heart(r, p->x + 18, p->y + 14, HEART_RED_HALF);
            }
            break;
        case TAROT_HERMIT: {
            /* Warp to the shop. Floors can generate without one — then the
               substitute payout is 3 coins (flagged design choice). */
            int done = 0;
            for (int hy = 0; hy < DUNGEON_H && !done; hy++) {
                for (int hx = 0; hx < DUNGEON_W && !done; hx++) {
                    if (g->dungeon.rooms[hy][hx].type == ROOM_SHOP) {
                        drain_black_burst(g);
                        g->dungeon.cur_x = hx;
                        g->dungeon.cur_y = hy;
                        do_warp_cleanup(g);
                        done = 1;
                    }
                }
            }
            if (!done && r) {
                for (int hc = 0; hc < 3; hc++)
                    spawn_consumable(r, p->x - 20 + hc * 20, p->y + 16,
                                     PICKUP_COIN);
            }
            break;
        }
        case TAROT_WHEEL_OF_FORTUNE:
            /* Spawn a usable slot machine near the room centre (the slot
               interaction was un-gated from ROOM_ARCADE for this). Fresh
               spin allowance; falls back to 2 coins if the obstacle pool
               is full. */
            if (r && r->obstacle_count < MAX_OBSTACLES) {
                Obstacle *ws = &r->obstacles[r->obstacle_count++];
                float wx = p->x + ((p->x < (ROOM_LEFT + ROOM_RIGHT) / 2.0f)
                                   ? 40.0f : -40.0f);
                ws->x = clampf(wx, ROOM_LEFT + 24, ROOM_RIGHT - 24);
                ws->y = clampf(p->y, ROOM_TOP + 24, ROOM_BOTTOM - 24);
                ws->type = OBST_SLOT_MACHINE;
                ws->hp = 1;
                ws->active = 1;
                r->arcade_slot_used = 0;
                audio_play(SFX_DOOR);
            } else if (r) {
                spawn_consumable(r, p->x - 12, p->y + 16, PICKUP_COIN);
                spawn_consumable(r, p->x + 12, p->y + 16, PICKUP_COIN);
            }
            break;
        case TAROT_STRENGTH:
            /* Isaac's +1 container-for-the-room is out of reach of this
               stat system — substituted with a permanent half-heart heal
               + 0.3 dmg for ~10s (flagged design choice). */
            if (p->hp < p->stats.max_hp) p->hp += 1;
            p->card_dmg_bonus += 0.3f;
            p->card_dmg_timer = 600;
            recalc_player_stats(p);
            break;
        case TAROT_DEVIL:
            /* +2.0 damage for ~10s */
            p->card_dmg_bonus += 2.0f;
            p->card_dmg_timer = 600;
            recalc_player_stats(p);
            break;
        case TAROT_TEMPERANCE:
            /* Heal one full heart */
            p->hp += 2;
            if (p->hp > p->stats.max_hp) p->hp = p->stats.max_hp;
            break;
        case TAROT_MOON: {
            /* Warp to the secret room (do_warp_cleanup reveals its doors).
               No secret room: 2 coins consolation (flagged design choice). */
            int mdone = 0;
            for (int my = 0; my < DUNGEON_H && !mdone; my++) {
                for (int mx = 0; mx < DUNGEON_W && !mdone; mx++) {
                    if (g->dungeon.rooms[my][mx].type == ROOM_SECRET) {
                        drain_black_burst(g);
                        g->dungeon.cur_x = mx;
                        g->dungeon.cur_y = my;
                        do_warp_cleanup(g);
                        mdone = 1;
                    }
                }
            }
            if (!mdone && r) {
                spawn_consumable(r, p->x - 12, p->y + 16, PICKUP_COIN);
                spawn_consumable(r, p->x + 12, p->y + 16, PICKUP_COIN);
            }
            break;
        }
        case TAROT_JUDGEMENT: {
            /* Beggars don't exist in this demake — substituted with a
               pickup shower: 3-5 mixed random drops around the player. */
            if (r) {
                int jn = randi(3, 5);
                for (int ji = 0; ji < jn; ji++) {
                    float ja = randf(0.0f, 6.28f);
                    float jd = randf(18.0f, 40.0f);
                    spawn_random_consumable(r, p->x + cosf(ja) * jd,
                                            p->y + sinf(ja) * jd);
                }
                if (randi(0, 99) < 40)
                    spawn_heart(r, p->x, p->y + 20, HEART_RED_HALF);
            }
            break;
        }
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
    /* F8: 4096 default silently drops draws — budget is SHARED across both
       screens per frame, so raise it (platform rule). */
    C2D_Init(16384);
    C2D_Prepare();

    /* Initialize romfs for loading sprite assets */
    romfsInit();

    /* Best-effort load of custom bitmap font. On any failure this stays
     * NULL and gtext_parse() transparently falls back to the default
     * citro2d system font — identical to pre-existing behavior. */
    g_font = C2D_FontLoad("romfs:/gamefont.bcfnt");

    /* Load user config from SD card (or use defaults) */
    config_init(&g_config);
    config_load(&g_config);  /* -1 = no file, defaults kept */

    /* Retroactive unlocks: an old save whose completed characters predate
       newly-added roster entries earns those unlocks immediately at boot,
       not only after the next win. Persist if anything changed. */
    if (apply_unlock_gates()) config_save(&g_config);

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

    /* HOME-menu / SELECT exit: persist any settings changed in the settings
       menu that were never saved (the user backed out via HOME, not B). */
    if (game.settings_changed) config_save(&g_config);

    /* Cleanup */
    audio_exit();
    sprites_free();
    if (g_font) { C2D_FontFree(g_font); g_font = NULL; }
    C2D_TextBufDelete(textBuf);
    C2D_Fini();
    C3D_Fini();
    romfsExit();
    gfxExit();

    return 0;
}
