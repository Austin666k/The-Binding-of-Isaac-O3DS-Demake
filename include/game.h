/*
 * game.h - Binding of Isaac 3DS
 * Core game types, constants and declarations
 * Enhanced with items, stats, multi-floor system, and boss battles
 */
#ifndef GAME_H
#define GAME_H

#include <3ds.h>
#include <citro2d.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

/* ---------- Game Mode / Difficulty ---------- */
typedef enum {
    MODE_STORY = 0,      /* Normal progression: beat final boss to win */
    MODE_INFINITE        /* Endless: floors loop with increasing difficulty */
} GameMode;

typedef enum {
    DIFF_EASY = 0,
    DIFF_NORMAL,
    DIFF_HARD,
    DIFF_COUNT
} Difficulty;

/* ---------- Screen / Room Constants ---------- */
#define TOP_SCREEN_WIDTH   400
#define TOP_SCREEN_HEIGHT  240
#define BOT_SCREEN_WIDTH   320
#define BOT_SCREEN_HEIGHT  240

#define WALL_THICKNESS     24
#define ROOM_LEFT          (WALL_THICKNESS)
#define ROOM_TOP           (WALL_THICKNESS + 16)  /* extra 16 for HUD */
#define ROOM_RIGHT         (TOP_SCREEN_WIDTH  - WALL_THICKNESS)
#define ROOM_BOTTOM        (TOP_SCREEN_HEIGHT - WALL_THICKNESS)

/* ---------- Door Constants ---------- */
#define DOOR_WIDTH         32
#define DOOR_DEPTH         16
#define DOOR_TRIGGER       20.0f

/* ---------- Dungeon Constants ---------- */
#define DUNGEON_W          5
#define DUNGEON_H          5
#define MAX_ROOMS          25
#define MIN_ROOMS          7
#define MAX_ROOM_ENEMIES   8

/* ---------- Player ---------- */
#define PLAYER_SIZE        12.0f
#define PLAYER_BASE_SPEED  2.2f     /* slightly higher base; Isaac feels quick */
#define PLAYER_BASE_HP     6        /* 3 full hearts (each heart = 2 hp) */
#define PLAYER_IFRAMES     60
#define PLAYER_MAX_HP_CAP  16       /* max 8 hearts */

/* Isaac-style momentum physics tuning */
#define PLAYER_ACCEL       0.38f    /* how fast velocity approaches target (lower = more slide) */
#define PLAYER_FRICTION    0.82f    /* velocity decay when no input (higher = more slide) */
#define PLAYER_STOP_THRESH 0.08f   /* below this speed, snap to zero */
#define PLAYER_KB_FORCE    1.5f    /* flinch impulse magnitude when hit (no input lockout) */
#define PLAYER_KB_FRAMES   12      /* legacy knockback stun length (no longer applied on hits) */
#define CIRCLE_PAD_DEADZONE 20     /* center deadzone for analog stick */
#define CIRCLE_PAD_MAX     155.0f  /* max analog value for normalization */

/* ---------- Tears ---------- */
#define MAX_TEARS          30
#define TEAR_RADIUS        4.0f
#define TEAR_BASE_SPEED    4.0f
#define TEAR_BASE_RANGE    120.0f
#define TEAR_GRAVITY       0.15f    /* gravity applied to tear vz per frame */
#define TEAR_ARC_VEL      -2.0f     /* initial upward velocity (negative = up) */
#define TEAR_SPREAD        0.0f     /* random spread angle in radians (Rebirth tears
                                       are dead accurate; a future item can re-enable) */
#define TEAR_KNOCKBACK     1.4f     /* knockback impulse magnitude (scaled by tear dmg) */

/* ---------- Enemies ---------- */
#define MAX_ENEMIES        20       /* increased for death-spawns */
#define ENEMY_SIZE         10.0f
#define ENEMY_KB_FRICTION  0.85f    /* knockback decay per frame */
#define ENEMY_SEPARATION   0.4f     /* separation push strength */
#define BOSSRUSH_TOTAL_WAVES 6      /* number of boss waves in a Boss Rush room */

/* ---------- Polish ---------- */
#define SCREEN_SHAKE_MAX   8.0f
#define BOSS_INTRO_FRAMES  90       /* frames to pause on boss room entry */
#define PICKUP_FLASH_FRAMES 40
#define SLIDE_FRAMES       22       /* Rebirth-style room slide transition length */
#define HURT_FLASH_FRAMES  24       /* red vignette pulse after taking damage */

/* ---------- Boss System ---------- */
#define MAX_LARRY_SEGMENTS 5        /* Larry Jr. segmented body */
#define LARRY_SEG_DIST     18.0f    /* spacing between segments */
#define MONSTRO_TEAR_COUNT 8        /* 8-way tear spread */
#define MONSTRO_JUMP_HEIGHT 60.0f   /* visual jump arc height */
#define DUKE_SPAWN_INTERVAL 120     /* frames between fly spawns */
#define DUKE_MAX_FLIES     6        /* max flies Duke can have alive */
#define GEMINI_TETHER_DIST 50.0f    /* max distance before tether breaks */
#define FAMINE_CHARGE_SPEED 5.0f    /* horizontal charge velocity */
#define BOSS_SHOT_SPEED    2.5f     /* boss projectile speed */
#define BOSS_SHOT_DMG      2        /* boss projectile damage (bosses hit for a full heart) */

/* ---------- Obstacles ---------- */
#define MAX_OBSTACLES      12
#define OBSTACLE_SIZE      16.0f

/* ---------- Consumable Pickups ---------- */
#define MAX_CONSUMABLE_PICKUPS 16
#define CONSUMABLE_BOB_SPEED   0.08f
#define CONSUMABLE_BOB_AMP     2.5f
#define CONSUMABLE_PICKUP_DIST 14.0f

/* ---------- Shop ---------- */
#define MAX_SHOP_ITEMS     3
#define SHOP_ITEM_COST_MIN 3
#define SHOP_ITEM_COST_MAX 7

/* ---------- Items ---------- */
#define MAX_ITEMS_HELD     32       /* max items player can collect */

/* ---------- Familiars ---------- */
#define MAX_FAMILIARS      2        /* follower slots (Phase E6) */
#define FAM_TRAIL_LEN      64       /* player position history ring buffer */
#define FAM_TRAIL_DELAY    20       /* frames each follower trails behind */

/* ---------- Characters ---------- */
typedef enum {
    CHAR_ISAAC = 0,
    CHAR_MAGDALENE,
    CHAR_CAIN,
    CHAR_JUDAS,
    CHAR_EVE,
    CHAR_SAMSON,
    CHAR_BLUE_BABY,
    /* --- R10 (C4) characters. PARALLEL TABLES: every new entry needs
       character_name / character_blurb / character_tint cases, recalc +
       apply_character_start blocks, select-card portrait + heart row,
       unlocks-screen chars[] entry, character_unlock_name case,
       apply_unlock_gates bit and a config unlock bit (7-9). --- */
    CHAR_AZAZEL,     /* flight + innate short-range Brimstone */
    CHAR_LAZARUS,    /* 1 extra life; respawns stronger (Lazarus' Rags) */
    CHAR_LOST,       /* no health at all; flight + mantle + free devil deals */
    CHAR_COUNT
} CharacterType;

/* ---------- Pills ---------- */
typedef enum {
    PILL_HEALTH_UP = 0,
    PILL_HEALTH_DOWN,
    PILL_SPEED_UP,
    PILL_SPEED_DOWN,
    PILL_TEARS_UP,
    PILL_TEARS_DOWN,
    PILL_RANGE_UP,
    PILL_LUCK_UP,
    PILL_FULL_HEALTH,
    PILL_TELEPILLS,
    PILL_BAD_TRIP,            /* lose 1 heart (damage self) */
    PILL_BOMBS_ARE_KEY,      /* swap bomb and key counts */
    PILL_EXPLOSIVE_DIARRHEA, /* drop a spread of bombs */
    PILL_EFFECT_COUNT
} PillEffect;

/* ---------- Tarot Cards ---------- */
typedef enum {
    TAROT_FOOL = 0,         /* teleport back to starting room */
    TAROT_MAGICIAN,         /* homing tears for the rest of the room */
    TAROT_PRIESTESS,        /* spawn a heart */
    TAROT_EMPEROR,          /* warp directly to the boss room */
    TAROT_HIEROPHANT,       /* spawn two soul hearts */
    TAROT_LOVERS,           /* spawn two full hearts */
    TAROT_TOWER,            /* spawn 6 troll bombs (damages player too) */
    TAROT_WORLD,            /* full map reveal */
    TAROT_DEATH,            /* damage all enemies in room heavily */
    TAROT_STAR,             /* full map reveal + spawn a heart */
    TAROT_SUN,              /* full heal + full map reveal */
    TAROT_HANGED_MAN,       /* spawn two soul hearts */
    /* --- R8 (M6): the 10 missing major arcana (deck complete at 22).
       Every entry here needs BOTH a tarot_names[] string (main.c) and an
       apply_tarot_card case — the table is sized by TAROT_COUNT. --- */
    TAROT_EMPRESS,          /* +0.3 dmg +0.2 spd temp buff (~10s) */
    TAROT_CHARIOT,          /* 6s invincibility (big iframes) + speed */
    TAROT_JUSTICE,          /* spawn 1 coin + bomb + key + half heart */
    TAROT_HERMIT,           /* warp to the shop (no shop: 3 coins) */
    TAROT_WHEEL_OF_FORTUNE, /* spawn a usable slot machine in-room */
    TAROT_STRENGTH,         /* heal half heart + 0.3 dmg temp buff */
    TAROT_DEVIL,            /* +2.0 dmg temp buff (~10s) */
    TAROT_TEMPERANCE,       /* heal 1 full heart */
    TAROT_MOON,             /* warp to the secret room */
    TAROT_JUDGEMENT,        /* pickup shower: 3-5 mixed drops */
    TAROT_COUNT
} TarotCard;

/* ---------- Champion Enemies ---------- */
typedef enum {
    CHAMP_NONE = 0,
    CHAMP_RED,       /* 2x HP, drops red heart */
    CHAMP_BLUE,      /* 1.5x HP, 1.3x speed, drops soul heart */
    CHAMP_YELLOW,    /* 1.5x HP, leaves creep trail that damages player */
    CHAMP_BLACK      /* 2x HP, splits into 2 enemies on death */
} ChampionType;

/* ---------- Active bombs ---------- */
#define MAX_BOMBS 4
typedef struct {
    float x, y;      /* placed bomb position */
    float vx, vy;    /* slide velocity (Dr./Epic Fetus tear-bombs) */
    int   timer;     /* frames until explosion */
    int   flash;     /* visual flash counter */
    int   active;
    int   is_epic;   /* Epic Fetus bomb: x1.5 blast radius and damage */
    int   is_fetus;  /* Dr./Epic Fetus tear-bomb: blast scales with damage stat */
} ActiveBomb;

/* ---------- Creep / hazard tile ---------- */
#define MAX_CREEP 32
typedef struct {
    float x, y;
    float radius;
    int   timer;      /* frames remaining */
    int   active;
    int   dmg;
} CreepTile;

/* ---------- Floors ---------- */
#define MAX_FLOORS         8

/* ---------- Game States ---------- */
typedef enum {
    STATE_MENU,
    STATE_MODE_SELECT,         /* choose Story vs Infinite */
    STATE_CHALLENGE_SELECT,    /* choose a challenge run */
    STATE_CHARACTER_SELECT,    /* choose playable character */
    STATE_DIFFICULTY_SELECT,   /* choose Easy / Normal / Hard */
    STATE_CONTROLS,
    STATE_SETTINGS,            /* in-game settings (audio toggle, volumes) */
    STATE_UNLOCKS,             /* unlocks / collection screen */
    STATE_PLAYING,
    STATE_PAUSED,              /* paused: stats overlay shown */
    STATE_GAMEOVER,
    STATE_WIN,
    STATE_FLOOR_TRANSITION
} GameState;

/* ---------- Menu Options (Rebirth-style) ---------- */
typedef enum {
    MENU_NEW_RUN = 0,
    MENU_CONTINUE,
    MENU_CHALLENGES,
    MENU_STATS,
    MENU_OPTIONS,
    MENU_COUNT,
    /* Aliases for backwards compatibility */
    MENU_NEW_GAME = MENU_NEW_RUN,
    MENU_UNLOCKS  = MENU_STATS,
    MENU_SETTINGS = MENU_OPTIONS
} MenuOption;

/* ---------- Direction ---------- */
typedef enum {
    DIR_NONE = 0,
    DIR_UP,
    DIR_DOWN,
    DIR_LEFT,
    DIR_RIGHT
} Direction;

/* ---------- Enemy Types ---------- */
typedef enum {
    /* --- Original enemies (Tier 0: Basement) --- */
    ENEMY_FLY,            /* orbits player, occasionally dives */
    ENEMY_GAPER,          /* walks toward player, splits on death */
    ENEMY_PACER,          /* paces horizontally, charges when close */
    ENEMY_SPIDER,         /* burst movement, short hops toward player */
    ENEMY_CLOTTY,         /* strafes + shoots blood in 4 directions */
    ENEMY_GAPER_SMALL,    /* split from a dying gaper */
    /* --- New enemies (Tier 1: Basement II+) --- */
    ENEMY_ATTACK_FLY,     /* aggressive fly, dives at player repeatedly */
    ENEMY_POOTER,         /* flies around slowly, shoots at player */
    ENEMY_HOPPER,         /* hops randomly around room */
    ENEMY_BABY,           /* slow crawling enemy, low HP */
    /* --- New enemies (Tier 2: Caves+) --- */
    ENEMY_GLOBIN,         /* charges at player, regenerates briefly */
    ENEMY_BOOM_FLY,       /* flies toward player, explodes on death */
    ENEMY_MAW,            /* walks slowly, shoots homing-ish projectiles */
    ENEMY_MULLIGAN,       /* walks, shoots, spawns flies on death */
    /* --- New enemies (Tier 3: Caves II / Depths) --- */
    ENEMY_HOST,           /* stationary, hides in shell, pops up to shoot */
    ENEMY_RED_MAW,        /* stationary turret, shoots rapidly in player dir */
    ENEMY_LEAPER,         /* jumps toward player in high arcs */
    ENEMY_VIS,            /* floats, shoots double projectiles */
    /* --- New enemies (batch: extra variety) --- */
    ENEMY_TRITE,          /* fast leaping spider - quick, low arc */
    ENEMY_FATTY,          /* slow tanky gaper - ~2x HP, sluggish */
    ENEMY_CHARGER,        /* pacer variant that charges straight when aligned */
    /* --- New enemies (batch: more variety) --- */
    ENEMY_KEEPER,         /* moves erratically, drops coins on death */
    ENEMY_SUCKER,         /* floats and shoots like a Vis but weaker */
    /* --- New enemies (batch: more enemies + boss) --- */
    ENEMY_ROUND_WORM,     /* burrows underground, emerges near player to attack */
    ENEMY_SPITTY,         /* stationary, periodically spits projectiles */
    /* --- Bosses --- */
    ENEMY_BOSS_DUKE,      /* Duke of Flies - spawns flies */
    ENEMY_BOSS_MONSTRO,   /* Monstro - jumps and shoots */
    ENEMY_BOSS_GEMINI,    /* Gemini - charges aggressively */
    ENEMY_BOSS_LARRY,     /* Larry Jr - snakes around */
    ENEMY_BOSS_FAMINE,    /* Famine - horseman, fast charges */
    /* --- Phase 2 Bosses --- */
    ENEMY_BOSS_PEEP,      /* Peep - bounces around, eyes detach at low HP */
    ENEMY_BOSS_GURDY,     /* Gurdy - stationary fat boss, spawns minions */
    ENEMY_BOSS_PIN,       /* Pin - worm boss, burrows and emerges */
    ENEMY_BOSS_HAUNT,     /* The Haunt - ghost boss with 2 phases */
    ENEMY_BOSS_WIDOW,     /* Widow - spider boss, jumps and spawns spiders */
    ENEMY_BOSS_GISH,      /* Gish - jumps + shoots like Monstro, leaves creep on landing */
    ENEMY_BOSS_LOKI,      /* Loki - blinks/teleports and fires 4-way then 8-way spreads */
    /* --- Batch: more bosses for under-served tiers --- */
    ENEMY_BOSS_STEVEN,    /* Steven - Basement twin-head, Gemini-lite */
    ENEMY_BOSS_CHUB,      /* Chub - Caves segmented boss, fatter Larry */
    ENEMY_BOSS_FISTULA,   /* Fistula - Depths boss, splits into smaller balls on hit */
    ENEMY_BOSS_SCOLEX,    /* Scolex - Womb segmented worm boss, burrows and emerges */
    ENEMY_BOSS_MEGA_SATAN,/* Mega Satan - The Chest fixed final boss */
    /* --- Round 6 (Phase E) story-arc fixed bosses --- */
    ENEMY_BOSS_MOM,       /* Mom - fixed Depths boss (stomping foot + door hands) */
    ENEMY_BOSS_MOMS_HEART,/* Mom's Heart - fixed Womb boss (stationary + waves) */
    ENEMY_BOSS_SATAN,     /* Satan - fixed Sheol boss, 3 phases */
    /* --- Round 9 (Phase C1) route-arc fixed bosses ---
       MUST stay inside the boss block (is_boss_type / bosses_defeated bit
       index / boss_names[] on the unlocks screen are all enum-order based:
       Isaac=20, The Lamb=21, It Lives=22). */
    ENEMY_BOSS_ISAAC,     /* Isaac - Cathedral (light route), holy 3 phases */
    ENEMY_BOSS_THE_LAMB,  /* The Lamb - Dark Room (dark route), demonic mirror */
    ENEMY_BOSS_IT_LIVES,  /* It Lives - Womb capstone after 1+ total wins */
    /* --- Round 8 (M3/M7) bosses ---
       MUST stay inside the boss block (is_boss_type / bosses_defeated bit
       index / boss_names[] on the unlocks screen are all enum-order based:
       Uriel=23, Gabriel=24, Krampus=25, ??? (Blue Baby)=26). */
    ENEMY_BOSS_URIEL,     /* Uriel - first angel-statue miniboss of a run */
    ENEMY_BOSS_GABRIEL,   /* Gabriel - second angel miniboss, faster/denser */
    ENEMY_BOSS_KRAMPUS,   /* Krampus - devil-room ambush miniboss */
    ENEMY_BOSS_BLUE_BABY, /* ??? (Blue Baby) - The Chest floor-7 boss */
    /* --- Phase 2 minor enemies (boss minions) --- */
    ENEMY_EYE,            /* Peep's detached eyes */
    ENEMY_LIL_HAUNT,      /* Haunt's minions */
    ENEMY_FISTULA_BALL,   /* Fistula's split-off balls */
} EnemyType;

/* ---------- Enemy Projectile (for clotty blood shots) ---------- */
#define MAX_ENEMY_SHOTS 40
typedef struct {
    float x, y;
    float dx, dy;
    float dist;
    int   active;
    int   dmg;
} EnemyShot;

/* ---------- Room Types ---------- */
typedef enum {
    ROOM_NONE = 0,
    ROOM_START,
    ROOM_NORMAL,
    ROOM_TREASURE,    /* requires a key to enter */
    ROOM_BOSS,
    ROOM_TRAPDOOR,    /* appears after boss is defeated */
    ROOM_SHOP,        /* buy items with coins */
    ROOM_SECRET,      /* hidden room revealed by bombing adjacent wall */
    ROOM_CURSE,       /* costs 1 heart to enter, contains good loot */
    ROOM_DEVIL,       /* devil deal: items bought with heart containers */
    ROOM_ANGEL,       /* free item pedestal + soul hearts, no enemies */
    ROOM_SACRIFICE,   /* central spikes; hitting them repeatedly grants a reward */
    ROOM_BOSSRUSH,    /* gauntlet of boss waves; clears + rewards after final wave */
    ROOM_ARCADE,      /* slot machine: pay a coin, roll a reward */
    ROOM_LIBRARY      /* 1-2 free item pedestals biased toward active items */
} RoomType;

/* ---------- Consumable Types ---------- */
typedef enum {
    PICKUP_BOMB = 0,
    PICKUP_KEY,
    PICKUP_COIN,
    PICKUP_COIN5,     /* 5-coin pickup (nickel) */
    PICKUP_BOMB2,     /* double bomb pickup */
    PICKUP_PILL,      /* pill (single-use, scrambled color) */
    PICKUP_CARD,      /* tarot card (single-use) */
    PICKUP_CHEST,     /* wooden chest: opens into a small loot burst */
    PICKUP_CHEST_GOLD,/* gold chest: costs a key, better loot */
    PICKUP_TRINKET,   /* trinket (sub_type = TrinketType); swaps with held */
    PICKUP_KEY5,      /* 5-key pickup (charged key ring) */
    PICKUP_BATTERY,   /* refills active item charge to full */
    PICKUP_CHEST_RED, /* R8 (M5): free to open, weighted risk/reward roll;
                         sub_type 1 = already opened (husk stays visible) */
    PICKUP_TYPE_COUNT
} PickupType;

/* ---------- Trinkets (one held at a time, passive while held) ---------- */
typedef enum {
    TRINKET_NONE = 0,
    TRINKET_SWALLOWED_PENNY,  /* drop a coin when you take damage */
    TRINKET_PETRIFIED_POOP,   /* poop drops pickups far more often */
    TRINKET_CHILDS_HEART,     /* higher heart drop chance from kills */
    TRINKET_RUSTED_KEY,       /* higher key drop chance from kills */
    TRINKET_MATCH_STICK,      /* higher bomb drop chance from kills */
    TRINKET_LUCKY_TOE,        /* +1 luck */
    TRINKET_CRACKED_CROWN,    /* +0.3 damage */
    TRINKET_CANCER,           /* +0.5 tears (fire rate) */
    TRINKET_TICK,             /* +0.4 damage */
    TRINKET_BROKEN_MAGNET,    /* higher coin drop chance from kills */
    TRINKET_UMBILICAL_CORD,   /* +1 max HP */
    TRINKET_CURVED_HORN,      /* +0.4 damage */
    TRINKET_COUNT
} TrinketType;

const char *trinket_name(int t);

/* ---------- Consumable Pickup ---------- */
typedef struct {
    float x, y;
    PickupType type;
    int   active;
    int   anim_timer;  /* for bobbing animation */
    int   sub_type;    /* for PICKUP_PILL: pill effect; PICKUP_CARD: card id */
} ConsumablePickup;

/* ---------- Item Types ---------- */
typedef enum {
    ITEM_NONE = 0,
    /* Stat boost items */
    ITEM_PENTAGRAM,       /* +0.5 damage mult */
    ITEM_BELT,            /* +0.3 speed */
    ITEM_WIRE_COAT,       /* +0.7 fire rate */
    ITEM_LUNCH,           /* +2 max HP */
    ITEM_CUPIDS_ARROW,    /* piercing tears */
    ITEM_SPOON_BENDER,    /* homing tears */
    ITEM_POLYPHEMUS,      /* +1.5 damage, -1 fire rate, bigger tears */
    ITEM_INNER_EYE,       /* triple shot, slower fire rate */
    ITEM_SPELUNKER_HAT,   /* +1.5 range */
    ITEM_SPEED_BALL,      /* +0.3 speed, +0.5 fire rate */
    ITEM_MAGIC_MUSH,      /* +0.5 dmg, +1 range, +2 HP */
    ITEM_SACRED_HEART,    /* +1.0 dmg, homing, -0.3 speed */
    ITEM_SPIRIT_SWORD,    /* spectral tears */
    ITEM_BLOOD_OF_MARTYR, /* +0.5 damage mult */
    ITEM_STIGMATA,        /* +0.3 dmg, +2 HP */
    ITEM_SAD_ONION,       /* +1.0 fire rate */
    ITEM_WIRE_HANGER,     /* +0.5 fire rate */
    ITEM_GROWTH_HORMONES, /* +0.4 dmg, +0.2 speed */
    ITEM_JESUS_JUICE,     /* +0.5 dmg, +0.5 range */
    ITEM_HALO,            /* +0.3 dmg, +0.2 spd, +0.2 fire, +0.5 range, +2 HP */
    /* New expansion items (Phase 1) */
    ITEM_MOMS_KNIFE,      /* big knife tear, +1.0 dmg, very slow fire */
    ITEM_BRIMSTONE,       /* charged laser beam, +1.5 dmg, slow fire */
    ITEM_TECHNOLOGY,      /* persistent laser instead of tears, +0.4 dmg */
    ITEM_NUMBER_ONE,      /* huge fire rate, -0.5 range */
    ITEM_DR_FETUS,        /* fires bombs instead of tears */
    ITEM_EPIC_FETUS,      /* targeted air-strike bombs */
    ITEM_DEAD_CAT,        /* extra life (max HP -> 1, on death respawn until lives=0) */
    ITEM_HOLY_MANTLE,     /* absorb one hit per room */
    ITEM_PYROMANIAC,      /* immune to explosions, heal when in blast */
    ITEM_IPECAC,          /* explosive tears (also damages self) */
    ITEM_SOY_MILK,        /* huge fire rate, very low dmg per tear */
    ITEM_YUM_HEART,       /* Magdalene starter: spawn heart on use */
    ITEM_LUCKY_FOOT,      /* Cain starter: +1 luck */
    ITEM_BOOK_OF_BELIAL,  /* Judas starter: +1.5 dmg this room on use */
    /* New expansion items (Phase 3) — passive stat/flag items */
    ITEM_CRICKETS_HEAD,   /* +1.0 damage */
    ITEM_ODD_MUSHROOM,    /* +0.3 dmg, +0.1 spd, +0.3 fire */
    ITEM_ROID_RAGE,       /* +0.6 speed */
    ITEM_MARKED,          /* +0.5 fire, +0.1 dmg */
    ITEM_ANEMIC,          /* piercing + range */
    ITEM_CAT_O_NINE,      /* +0.5 damage */
    ITEM_LORD_OF_PIT,     /* +0.3 spd, +0.2 dmg */
    ITEM_TOUGH_LOVE,      /* +0.3 dmg, +0.3 fire */
    ITEM_SYNTHOIL,        /* +0.4 dmg, +0.5 range */
    ITEM_DEAD_EYE,        /* +0.6 damage */
    ITEM_THE_POOP,        /* active: spawn a poop obstacle in front of player */
    /* New expansion items (Phase 5) — passive stat/flag items */
    ITEM_SACRED_ORB,      /* +0.8 dmg, +0.3 range (Sacred Orb-ish) */
    ITEM_DEATHS_TOUCH,    /* piercing + big damage (Death's Touch) */
    ITEM_MUTANT_SPIDER,   /* +1.2 fire rate, -0.3 dmg (Mutant Spider-ish) */
    ITEM_TAMMYS_HEAD,     /* +0.7 dmg, +0.5 range, -0.5 fire (Tammy's Head-ish) */
    ITEM_A_PONY,          /* +0.5 speed (A Pony) */
    ITEM_CRICKETS_BODY,   /* +0.5 dmg, +1.0 range (Cricket's Body-ish) */
    ITEM_SACRIFICIAL_DAGGER, /* +0.8 dmg, spectral (Sacrificial Dagger-ish) */
    ITEM_IPECAC_LITE,     /* explosive tears, smaller than Ipecac (Ipecac-lite) */
    ITEM_MAGIC_FINGERS,   /* +0.4 dmg, +0.3 fire (Magic Fingers-ish) */
    ITEM_STEVEN,          /* homing + piercing, +0.2 dmg (Steven-ish) */
    /* --- Round 6 (Phase E) items --- */
    ITEM_THE_PACT,        /* +0.5 dmg, +0.7 fire rate (devil-pool biased) */
    ITEM_NECRONOMICON,    /* active: 40 dmg to whole room, 4-room charge */
    ITEM_TELEPORT,        /* active: random warp, 2-room charge */
    ITEM_DECK_OF_CARDS,   /* active: spawn a tarot card, 6-room charge */
    ITEM_BIBLE,           /* active: kills Mom/Mom's Heart; Satan kills YOU */
    ITEM_20_20,           /* double shot */
    ITEM_TORN_PHOTO,      /* +0.7 fire rate */
    ITEM_BLUE_CAP,        /* +0.7 fire rate, +2 max HP */
    ITEM_SQUEEZY,         /* +0.4 fire rate, +2 soul hearts on pickup */
    ITEM_BROTHER_BOBBY,   /* familiar: plain tears in aim direction */
    ITEM_GHOST_BABY,      /* familiar: spectral tears in aim direction */
    ITEM_DEMON_BABY,      /* familiar: auto-aims the nearest enemy */
    /* --- Round 8 (M7) Krampus-only drops (EXCLUDED from random pools) --- */
    ITEM_LUMP_OF_COAL,    /* passive: tear damage grows with tear flight time */
    ITEM_HEAD_OF_KRAMPUS, /* active (4): 4-way brimstone burst from the player */
    /* --- R8 (M8) Guppy set (normal pool, NOT excluded). Each pickup of a
       Guppy piece (these 3 + Dead Cat) advances the Guppy transformation. */
    ITEM_GUPPYS_PAW,      /* passive: +2 soul hearts, Guppy piece */
    ITEM_GUPPYS_HEAD,     /* active (2): summons 2 friendly blue flies */
    ITEM_GUPPYS_TAIL,     /* passive: +1 luck, Guppy piece */
    ITEM_COUNT
} ItemType;

/* ---------- Shop Item ---------- */
typedef struct {
    float    x, y;
    ItemType item;
    int      cost;     /* in coins */
    int      active;   /* 1 = still for sale */
} ShopItem;

/* ---------- Item Flags (special abilities) ---------- */
#define ITEM_FLAG_HOMING       (1 << 0)
#define ITEM_FLAG_PIERCING     (1 << 1)
#define ITEM_FLAG_SPECTRAL     (1 << 2)
#define ITEM_FLAG_TRIPLE       (1 << 3)
#define ITEM_FLAG_KNIFE        (1 << 4)   /* Mom's Knife: melee */
#define ITEM_FLAG_BRIMSTONE    (1 << 5)   /* Brimstone: charged laser */
#define ITEM_FLAG_LASER        (1 << 6)   /* Technology: persistent laser */
#define ITEM_FLAG_BOMB_TEAR    (1 << 7)   /* Dr Fetus: tears are bombs */
#define ITEM_FLAG_EXPLOSIVE    (1 << 8)   /* Ipecac: explosive tears */
#define ITEM_FLAG_MANTLE       (1 << 9)   /* Holy Mantle: absorb one hit */
#define ITEM_FLAG_FIRE_IMMUNE  (1 << 10)  /* Pyromaniac: immune+heal from blasts */
#define ITEM_FLAG_ACTIVE       (1 << 11)  /* Active item: usable with charge bar (KEY_X) */
#define ITEM_FLAG_DOUBLE       (1 << 12)  /* 20/20: two parallel tears */
#define ITEM_FLAG_COAL         (1 << 13)  /* Lump of Coal: damage grows with tear age */
#define ITEM_FLAG_FLIGHT       (1 << 14)  /* R8 (M8) Guppy: fly over rocks/poop */

/* ---------- Item Definition ---------- */
typedef struct {
    ItemType type;
    const char *name;
    float damage_bonus;
    float speed_bonus;
    float fire_rate_bonus;   /* negative = faster */
    float range_bonus;
    int   hp_bonus;
    int   flags;             /* ITEM_FLAG_* */
    const char *description; /* short description shown on pickup */
} ItemDef;

/* ---------- Pedestal (item on ground) ---------- */
typedef struct {
    float    x, y;
    ItemType item;
    int      active;     /* 1 = item still on pedestal */
} Pedestal;

/* ---------- Tear ---------- */
typedef struct {
    float x, y;
    float dx, dy;
    float dist;
    int   active;
    int   piercing;
    int   spectral;
    int   homing;
    int   explosive;     /* Ipecac: detonate on any impact (enemy/wall/landing) */
    float dmg;           /* damage this tear deals */
    float z;             /* vertical offset (for arc rendering) */
    float vz;            /* vertical velocity */
    float size;          /* render scale (based on damage) */
    float speed;         /* projectile speed magnitude (for proper range tracking) */
    float rotation;      /* current rotation angle in radians */
    int   anim_frame;    /* animation counter for wobble/spin */
    int   is_enemy;      /* 1 = red/enemy tear, 0 = player blue tear */
} Tear;

/* ---------- Blood Splatter Particle ---------- */
#define MAX_BLOOD_PARTICLES 48

typedef struct {
    float x, y;
    float vx, vy;
    float alpha;         /* 0.0-1.0 fade */
    float scale;
    float rotation;
    int   sprite_idx;    /* index into bullet atlas */
    int   active;
    int   timer;         /* frames remaining */
    int   max_timer;     /* total lifetime */
} BloodParticle;

/* ---------- Larry Jr. Body Segment ---------- */
typedef struct {
    float x, y;
    float prev_x, prev_y;   /* position history for trailing */
} LarrySegment;

/* ---------- Enemy ---------- */
typedef struct {
    float     x, y;
    float     dx, dy;
    float     hp;        /* float so fractional tear damage counts */
    float     max_hp;    /* for boss HP bar */
    int       active;
    int       timer;
    int       flash;
    int       phase;     /* boss phase */
    EnemyType type;
    /* New AI state */
    float     kb_dx, kb_dy;     /* knockback velocity */
    int       state;            /* sub-state for AI (e.g. fly orbit/dive, spider charge) */
    int       shoot_timer;      /* clotty shoot cooldown */
    int       split_done;       /* gaper has already split */
    float     orbit_phase;      /* fly orbit angle */
    float     wobble;           /* gaper wobble offset */
    int       anim_timer;       /* sprite animation frame counter */
    /* New enemy AI fields */
    float     jump_arc;         /* leaper/hopper: vertical arc offset for rendering */
    int       hidden;           /* host: 1 = hiding in shell (invulnerable) */
    int       regen_timer;      /* globin: regeneration countdown */
    /* Boss-specific state */
    float     jump_z;           /* Monstro: vertical offset during jump */
    float     jump_vz;          /* Monstro: vertical velocity for jump arc */
    float     target_x, target_y; /* target position for jump landing */
    int       attack_pattern;   /* which attack pattern to use next */
    LarrySegment segments[MAX_LARRY_SEGMENTS]; /* Larry Jr: body segments */
    int       seg_count;        /* Larry Jr: number of active segments */
    float     seg_speed;        /* Larry Jr: current speed (increases when hurt) */
    float     gemini_cx, gemini_cy; /* Gemini: companion entity position */
    float     gemini_cdx, gemini_cdy; /* Gemini: companion velocity */
    int       gemini_split;     /* Gemini: 1 if tether broken */
    int       gemini_chp;       /* Gemini: companion HP */
    int       famine_shoot_cd;  /* Famine: shoot cooldown */
    /* Champion / elite enemy state */
    ChampionType champion;      /* CHAMP_NONE if not a champion */
    int       creep_drop_timer; /* yellow champion: drops creep periodically */
    int       split_pending;    /* black champion: deferred split on death */
    int       spawn_grace;      /* frames of no contact damage after a
                                   mid-combat dynamic spawn (splits, waves);
                                   zero-init covers normal room spawns */
} Enemy;

/* ---------- Obstacle (rocks / poop / spikes) ---------- */
typedef enum {
    OBST_ROCK = 0,
    OBST_POOP,        /* destructible by tears; may drop a pickup */
    OBST_SPIKES,      /* blocks nothing; damages the player on contact */
    OBST_SLOT_MACHINE,/* Arcade room: pay a coin, roll a reward */
    OBST_ANGEL_STATUE /* Angel room: bombing it awakens Uriel/Gabriel */
} ObstacleType;

typedef struct {
    float x, y;
    int   active;
    int   type;       /* ObstacleType */
    int   hp;         /* poop: tear hits remaining (3 -> gone) */
} Obstacle;

/* ---------- Player Stats ---------- */
typedef struct {
    float damage;        /* base damage multiplier */
    float speed;         /* movement speed */
    float fire_rate;     /* lower = faster shooting (cooldown modifier) */
    float range;         /* tear range in pixels */
    int   max_hp;        /* maximum HP */
    int   flags;         /* ITEM_FLAG_* abilities */
    float luck;          /* +luck improves drop chances */
    int   shot_speed;    /* tear speed bonus (integer for display) */
} PlayerStats;

/* ---------- Player ---------- */
typedef struct {
    float x, y;
    float vx, vy;          /* velocity for momentum */
    int   hp;
    int   iframes;
    int   tear_cooldown;
    int   shoot_anim;      /* >0 = frames remaining of shooting face (crying head) */
    Direction shoot_dir;   /* direction player last shot (for crying head) */
    int   pickup_anim;     /* >0 = frames of item pickup pose */
    PlayerStats stats;
    ItemType items[MAX_ITEMS_HELD];
    int      item_count;
    Direction face_dir;    /* direction player is facing (for sprite anim) */
    int       anim_timer;  /* animation frame counter */
    int       moving;      /* 1 if player moved this frame */
    /* Consumables */
    int   bombs;            /* current bomb count */
    int   keys;             /* current key count */
    int   coins;            /* current coin count */
    /* New: held single-use pill/card */
    int       has_pill;
    PillEffect held_pill;
    int       has_card;
    TarotCard held_card;
    /* Held trinket (TRINKET_NONE = none) */
    int       trinket;
    /* Active challenge id (0 = normal run), mirrored from Game for recalc */
    int       challenge;
    /* Character + special states */
    CharacterType character;
    int   lives;            /* extra lives from Dead Cat etc. */
    int   holy_mantle_active; /* 1 if can absorb next hit */
    int   book_belial_dmg_timer; /* frames remaining of Book of Belial damage boost */
    /* Soul hearts */
    int   soul_hp;          /* extra "soul" HP (consumed before red HP) */
    /* Black hearts (Phase E5): absorbed BEFORE soul hearts; each depleted
       black heart (2 units) deals 40 damage to every enemy in the room */
    int   black_hp;
    /* Familiar slots (Phase E6): item ids, ITEM_NONE = empty */
    ItemType familiar[MAX_FAMILIARS];
    /* Persistent pill stat bonuses (applied during recalc_player_stats) */
    float pill_speed_bonus;
    float pill_fire_rate_bonus;
    float pill_range_bonus;
    float pill_luck_bonus;
    int   pill_max_hp_bonus;
    /* Active item system (charge-based, KEY_X to use) */
    ItemType active_item;      /* ITEM_NONE = no active item held */
    int   active_charge;       /* current charge */
    int   active_max_charge;   /* charge required to use */
    /* R8 (M3): Mega Satan key halves — Uriel drops 1, Gabriel drops 2.
       Per-run (Player lives inside Game; start_new_game memsets Game). */
    int   has_key_piece_1;
    int   has_key_piece_2;
    /* R8 (M8) transformations — per-run counters (memset by start_new_game).
       Guppy pieces: Dead Cat / Guppy's Paw / Head / Tail; at 3+ -> GUPPY!
       (flight + tears spawn friendly blue flies). Mushrooms: Magic Mush /
       Odd Mushroom / Blue Cap; all 3 -> FUN GUY! (+1 heart container). */
    int   guppy_count;
    int   guppy_active;
    int   funguy_count;
    int   funguy_active;
    /* R8 (M6) tarot temp buffs (Empress/Strength/Devil dmg, Empress/Chariot
       speed). While the timer runs, recalc adds the bonus; expiry zeroes the
       bonus and recalcs (same pattern as book_belial_dmg_timer). */
    int   card_dmg_timer;
    float card_dmg_bonus;
    int   card_spd_timer;
    float card_spd_bonus;
    /* R10 (C4) Samson Bloody Lust: hits taken THIS room (+0.15 dmg each,
       capped at +1.0 in recalc); reset on every room change. */
    int   samson_hits;
    /* R10 (C4) Lazarus' Rags: permanent +0.5 dmg per death-respawn this
       run (applied in recalc; accumulates if he gains more lives). */
    float lazarus_dmg_bonus;
} Player;

/* ---------- R8 (M8) friendly blue flies (Guppy / Guppy's Head) ---------- */
#define MAX_BLUE_FLIES 6
typedef struct {
    float x, y;
    int   active;
    int   anim;       /* wobble/orbit phase counter */
} BlueFly;

/* ---------- Blood Decal (permanent floor stain, persists per room) ---------- */
#define MAX_BLOOD_DECALS 24

typedef struct {
    float x, y;
    float scale;
    float rotation;
    int   sprite_idx;    /* index into bullet atlas (blood_splat_*) */
    int   active;
} BloodDecal;

/* ---------- Heart Pickup ---------- */
#define MAX_HEART_PICKUPS 16

typedef enum {
    HEART_RED_FULL = 0,   /* heals 2 HP */
    HEART_RED_HALF,       /* heals 1 HP */
    HEART_SOUL,           /* adds 2 soul-heart units (consumed before red HP) */
    HEART_BLACK           /* adds 2 black-heart units; absorbed BEFORE soul
                             hearts; depleting one damages the whole room */
} HeartType;

typedef struct {
    int active;
    float x, y;
    HeartType type;
    int anim_timer;       /* for bobbing animation */
} HeartPickup;

/* ---------- Room ---------- */
typedef struct {
    RoomType type;
    int      gx, gy;
    int      doors[4];       /* 0=top,1=bottom,2=left,3=right */
    int      door_locked[4]; /* 1 = requires key (treasure) or bomb (secret) */
    int      door_type[4];   /* 0=normal, 1=treasure(key), 2=boss, 3=shop, 4=curse */
    int      cleared;
    int      visited;
    int      enemy_count;
    Enemy    enemies[MAX_ENEMIES];
    int      obstacle_count;
    Obstacle obstacles[MAX_OBSTACLES];
    int      enemies_spawned;
    Pedestal pedestal;       /* item pedestal (for treasure rooms) */
    int      has_trapdoor;   /* trapdoor to next floor */
    int      heart_count;
    HeartPickup hearts[MAX_HEART_PICKUPS];
    /* Consumable pickups */
    int      consumable_count;
    ConsumablePickup consumables[MAX_CONSUMABLE_PICKUPS];
    /* Shop items */
    int      shop_count;
    ShopItem shop_items[MAX_SHOP_ITEMS];
    /* Secret room state */
    int      secret_revealed; /* 1 if adjacent wall was bombed to reveal this room */
    /* Permanent blood stains (gore persists between room visits) */
    int        decal_next;    /* ring-buffer write cursor */
    BloodDecal decals[MAX_BLOOD_DECALS];
    /* Sacrifice room: counts player spike hits toward the reward */
    int      sacrifice_hits;
    int      sacrifice_rewarded;
    /* Secret room upgraded into a paid "black market" (shop_items/shop_count
       reused, purchase code extended to accept this flag alongside ROOM_SHOP) */
    int      is_black_market;
    /* Arcade room: slot machine obstacle (reuses obstacles[]); tracks whether
       it has been paid-for/used this visit so it can't be spammed for free. */
    int      arcade_slot_used;
    /* Phase E2: Mom's Heart kill spawns a "beam of light" end-run object
       next to the trapdoor; touching it wins the run with Ending 1. */
    int      has_ending_beam;
    /* R8 #25: Boss Rush wave progress lives WITH the room so warping out
       and back never refights waves already cleared (the Game-side
       bossrush_active/spawn_timer are still reset on exit/entry). */
    int      bossrush_wave;
    /* R8 (M7): Krampus ambush state for devil rooms.
       0 = normal devil room, 1 = armed (ambush fires ~45f after entry),
       2 = Krampus spawned (never re-arms). */
    int      krampus_state;
} Room;

/* ---------- Dungeon / Floor ---------- */
typedef struct {
    Room rooms[DUNGEON_H][DUNGEON_W];
    int  room_count;
    int  cur_x, cur_y;
    int  start_x, start_y;
    int  boss_x, boss_y;
} Dungeon;

/* ---------- Boss Pool System ---------- */
#define MAX_BOSS_POOL     8        /* max bosses in any floor's pool */
#define BOSS_POOL_TIERS   5        /* number of difficulty tiers */
#define BOSS_HISTORY_SIZE 2        /* how many recent bosses to avoid repeating */

/* Floor difficulty tier (maps floor_num to a boss pool) */
typedef enum {
    BOSS_TIER_BASEMENT = 0,   /* Floors 0-1: Basement I & II */
    BOSS_TIER_CAVES    = 1,   /* Floors 2-3: Caves I & II */
    BOSS_TIER_DEPTHS   = 2,   /* Floor 4:    Depths */
    BOSS_TIER_WOMB     = 3,   /* Floor 5:    Womb (fleshy red) */
    BOSS_TIER_SHEOL    = 4    /* Floors 6-7: Sheol / The Chest (final, dark) */
} BossTier;

/* ---------- Curses ---------- */
typedef enum {
    CURSE_NONE = 0,
    CURSE_DARKNESS,    /* dark vignette across screen */
    CURSE_LOST,        /* minimap hidden all floor */
    CURSE_BLIND,       /* item pedestals show ? icon */
    CURSE_UNKNOWN,     /* heart row hidden in HUD */
    CURSE_MAZE,        /* room transitions may misdirect */
    CURSE_LABYRINTH,   /* bigger floor layouts */
    CURSE_COUNT
} CurseType;

/* ---------- Floor Info ---------- */
typedef struct {
    const char *name;
    int   enemy_hp_bonus;    /* extra HP for enemies */
    int   base_boss_hp;      /* base boss HP for this floor (before scaling) */
    float enemy_speed_mult;  /* enemy speed multiplier */
    float boss_hp_scale;     /* boss HP multiplier for floor depth */
    float boss_speed_scale;  /* boss speed multiplier for floor depth */
    BossTier boss_tier;      /* which boss pool to draw from */
} FloorInfo;

/* ---------- Game ---------- */
typedef struct {
    GameState  state;
    GameMode   game_mode;       /* Story or Infinite */
    Difficulty difficulty;      /* Easy / Normal / Hard */
    int        infinite_loop;   /* how many times floors have looped (infinite mode) */
    int        best_floor;      /* high score: deepest floor reached (infinite mode) */
    int        mode_sel;        /* selection index for mode select screen (0-1) */
    int        diff_sel;        /* selection index for difficulty select screen (0-2) */
    Player     player;
    Tear       tears[MAX_TEARS];
    EnemyShot  enemy_shots[MAX_ENEMY_SHOTS];
    int        score;
    int        frame;
    int        menu_sel;
    int        transition;
    Direction  trans_dir;
    Dungeon    dungeon;
    int        current_floor;    /* 0-indexed floor number */
    int        floor_transition_timer;
    int        rooms_cleared;    /* total rooms cleared across floors */
    /* Polish state */
    float      shake_intensity;  /* current screen shake magnitude */
    int        shake_timer;      /* frames remaining for shake */
    int        boss_intro_timer; /* boss room entry pause */
    int        boss_active;      /* 1 if a boss is alive in the current room */
    const char *boss_name;       /* name of current boss for HUD display */
    int        boss_death_anim;  /* timer for boss death explosion effect */
    float      boss_death_x;     /* where the boss died (death anim anchor) */
    float      boss_death_y;
    EnemyType  boss_history[BOSS_HISTORY_SIZE]; /* recently fought bosses */
    int        boss_history_count; /* how many entries in history (0 to BOSS_HISTORY_SIZE) */
    EnemyType  current_boss_type; /* which boss was selected for this floor */
    int        pickup_flash;     /* flash timer for item pickup */
    ItemType   last_pickup;      /* last item picked up (for flash text) */
    int        room_fade;        /* fade-in counter on room enter */
    /* Rebirth-style sliding room transition */
    int        slide_timer;      /* >0 = camera pan between rooms in progress */
    Direction  slide_dir;        /* direction of travel for the slide */
    int        slide_from_x, slide_from_y; /* grid coords of the room being left */
    /* Red vignette pulse when the player takes damage */
    int        hurt_flash_timer;
    /* Devil deal: set when the player loses red HP this floor (blocks deal) */
    int        floor_red_dmg;
    /* Challenge runs (0 = normal; see CHAL_* in main.c) */
    int        challenge;
    int        chal_sel;         /* selection on challenge select screen */
    /* Floor-intro nameplate timer (Rebirth-style floor title on entry) */
    int        floor_intro_timer;
    /* AAA Menu animation state */
    int        menu_timer;       /* animation timer for menu effects */
    /* Blood splatter particles */
    BloodParticle blood[MAX_BLOOD_PARTICLES];
    /* Active bombs (small pool: player-placed + troll bombs) */
    ActiveBomb bombs[MAX_BOMBS];
    /* Shop feedback */
    int   shop_deny_timer;   /* cooldown timer for "can't afford" feedback */
    /* Settings menu state */
    int   settings_sel;      /* selected item in settings screen (0-based) */
    int   settings_changed;  /* 1 if settings were modified (need save+restart) */
    /* Character select state */
    int   char_sel;          /* selection on character select screen (0-3) */
    CharacterType selected_character;
    /* Item description on pickup */
    int   pickup_msg_timer;  /* frames remaining to show pickup description */
    char  pickup_msg_text[96];
    /* Stats screen */
    int   prev_state;        /* state to return to when un-pausing */
    /* Gameplay stat tracking */
    int   kills;             /* total enemy kills */
    int   play_time_frames;  /* total elapsed game frames (playing) */
    /* Pill color randomization (which color visual maps to which PillEffect) */
    int   pill_color_map[PILL_EFFECT_COUNT];
    int   pill_known[PILL_EFFECT_COUNT];  /* 1 if effect has been identified */
    /* Active creep / hazard tiles */
    CreepTile creep[MAX_CREEP];
    /* Persistent active laser beam (Brimstone/Technology) */
    int   laser_active;
    float laser_dx, laser_dy;
    int   laser_timer;
    int   laser_charge;       /* Brimstone: frames of fire held (fires at 15) */
    int   laser_is_tech;      /* 1 = Technology thin beam, 0 = Brimstone */
    float laser_ex, laser_ey; /* computed beam endpoint (Spoon Bender bend) */
    /* Mom's Knife single entity (0 = held, 1 = thrown, 2 = returning) */
    int   knife_state;
    float knife_x, knife_y;
    float knife_dx, knife_dy; /* unit flight direction */
    float knife_dist;         /* distance traveled while thrown */
    int   knife_hit_cd;       /* frames until the knife can damage again */
    /* Map reveal flag (World card) */
    int   map_revealed;
    /* Magician card homing remaining frames */
    int   homing_timer;
    /* Curse system (Phase 2) */
    int   active_curse;          /* CurseType for current floor */
    int   curse_display_timer;   /* frames remaining to show curse banner */
    /* Unlocks (Phase 2) */
    int   unlocks_scroll;        /* unlocks screen scroll position */
    /* Phase 2 boss tracking for unlocks */
    int   characters_completed_run; /* runtime bitmask of chars who beat a run */
    /* Game-feel: hitstop freeze frames (skips sim updates while > 0) */
    int   hitstop;
    /* Boss Rush room wave state (R8 #25: the wave COUNTER moved into the
       Room struct so progress survives warping out and back in) */
    int   bossrush_active;       /* 1 while the wave machine is running in this room */
    int   bossrush_spawn_timer;  /* frames until next wave spawns (grace period) */
    /* Enemy Brimstone beam (Phase E3 - Satan). Kept fully separate from the
       player's laser_* fields. 0 = off, 1 = telegraph (thin red line, 30f),
       2 = firing (thick beam, 10f, damages the player on line overlap). */
    int   ebeam_state;
    int   ebeam_timer;
    float ebeam_x, ebeam_y;      /* beam origin (boss muzzle) */
    float ebeam_dx, ebeam_dy;    /* unit aim direction (frozen at telegraph) */
    float ebeam_ex, ebeam_ey;    /* wall-clipped endpoint */
    /* R9 (C1 - The Lamb): when set, the enemy beam is a 4-way brimstone
       CROSS centered on (ebeam_x, ebeam_y) — the dx/dy/ex/ey fields are
       ignored and 4 axis-aligned arms run to the room walls instead. */
    int   ebeam_cross;
    /* R9 (C1 - Isaac): Cathedral light columns. Up to 3 vertical beams:
       ground-marker telegraph (~40f) then a full-height damage column
       (~20f). 0 = off, 1 = telegraph, 2 = firing. Driven by Isaac's AI
       case only; reset on every room/floor change alongside ebeam_*. */
    int   vbeam_state;
    int   vbeam_timer;
    int   vbeam_count;
    float vbeam_x[3];
    /* Familiar system (Phase E6): player position history ring buffer the
       followers trail behind, plus per-slot fire cooldowns. Static sizes,
       zero-state valid (head 0 / cds 0; trail refilled on room entry). */
    float fam_hist_x[FAM_TRAIL_LEN];
    float fam_hist_y[FAM_TRAIL_LEN];
    int   fam_hist_head;
    int   fam_cd[MAX_FAMILIARS];
    /* Which ending the win screen shows:
       0 = full escape (The Chest / Mega Satan, light route)
       1 = legacy "Ending 1" (pre-R9 Mom's Heart beam; no longer set)
       2 = light ending (Isaac defeated in the Cathedral — ascension)
       3 = dark ending (The Lamb defeated in the Dark Room — crowned) */
    int   win_ending;
    /* R9 (C1): run route, chosen at the Mom's Heart / It Lives kill.
       0 = undecided (floors 0-5), 1 = LIGHT (beam -> Cathedral -> Chest),
       2 = DARK (trapdoor -> Sheol -> Dark Room). Floors 6/7 change
       identity, palette and boss based on this (see get_floor_info). */
    int   route;
    /* --- R8 (M3/M7) additions (all per-run; zeroed by start_new_game) --- */
    /* Set the first time a devil-room purchase completes this run; from
       then on the post-boss deal flip ALWAYS chooses the devil room. */
    int   took_devil_deal;
    /* Angel statues awakened this run: 0 -> next fight is Uriel,
       1+ -> Gabriel. Incremented at awaken time. */
    int   angels_fought;
    /* Krampus ambush countdown (armed on entering a krampus_state==1 devil
       room; spawns Krampus at 0) + brief lights-dim overlay timer. */
    int   krampus_timer;
    int   krampus_dim;
    /* Head of Krampus player burst: 4-way beam render timer + origin.
       Damage is applied instantly at use; this is presentation only. */
    int   pbeam_timer;
    float pbeam_x, pbeam_y;
    /* Mega Satan golden-door room (floor 7): created lazily in a free grid
       cell the first time the door opens; re-enterable if the player warps
       out (key pieces are consumed on first open). */
    int   mega_created;
    int   mega_gx, mega_gy;
    /* R8 (M8): friendly blue fly pool (Guppy tears / Guppy's Head active).
       Zero-state valid; persists across rooms (flies chase the player until
       they find a target); memset by start_new_game. */
    BlueFly blue_flies[MAX_BLUE_FLIES];
    /* R10 (C4): last-seen total HP pool (red+soul+black). Eve's Whore of
       Babylon and Samson's Bloody Lust are hp-conditional stats computed in
       recalc_player_stats; the STATE_PLAYING update compares this cache each
       frame and recalcs on any change (a decrease = a hit for Samson). */
    int   prev_hp_total;
    /* R10 (C4): latch so a game over increments the lifetime death counter
       exactly once (zeroed by start_new_game's memset). */
    int   death_counted;
} Game;

/* ---------- Function Declarations ---------- */

/* Initialisation */
void game_init(Game *g);
void dungeon_generate(Dungeon *d, int floor_num);
void room_spawn_enemies(Game *g, Room *r);
void init_item_pool(void);

/* Per-frame update */
void game_update(Game *g, u32 kDown, u32 kHeld, circlePosition circlePos);

/* Sub-systems */
void player_update(Game *g, u32 kHeld, circlePosition circlePos);
void tears_update(Game *g);
void enemies_update(Game *g);
void enemy_shots_update(Game *g);
void collisions_update(Game *g);
void shoot_tear(Game *g, Direction dir);
void check_door_transition(Game *g);
void do_room_transition(Game *g, Direction dir);
/* Returns 1 if the item was actually granted, 0 if refused (passive-item
 * cap reached). Purchases must gate their payment on this. */
int  collect_item(Game *g, ItemType item);
void apply_item_stats(Player *p, const ItemDef *def);
void advance_floor(Game *g);
void recalc_player_stats(Player *p);
void trigger_shake(Game *g, float intensity, int frames);

/* Rendering */
void game_render_top(Game *g, C2D_TextBuf textBuf);
void render_menu(Game *g, C2D_TextBuf textBuf);
void render_mode_select(Game *g, C2D_TextBuf textBuf);
void render_challenge_select(Game *g, C2D_TextBuf textBuf);
void render_character_select(Game *g, C2D_TextBuf textBuf);
void render_difficulty_select(Game *g, C2D_TextBuf textBuf);
void render_controls(Game *g, C2D_TextBuf textBuf);
void render_gameover(Game *g, C2D_TextBuf textBuf);
void render_win(Game *g, C2D_TextBuf textBuf);
void render_hud(Game *g, C2D_TextBuf textBuf);
void render_room_gameplay(Game *g, C2D_TextBuf textBuf);
void render_minimap(Game *g, C2D_TextBuf textBuf);
void render_floor_transition(Game *g, C2D_TextBuf textBuf);
void render_stats_overlay(Game *g, C2D_TextBuf textBuf);
void render_pickup_message(Game *g, C2D_TextBuf textBuf);

/* New systems */
void apply_pill_effect(Game *g, PillEffect e);
void apply_tarot_card(Game *g, TarotCard c);
void spawn_pill_pickup(Room *r, float x, float y, int effect);
void spawn_card_pickup(Room *r, float x, float y, int card);
void start_new_game(Game *g);
void apply_character_start(Game *g);
const char *character_name(CharacterType c);
const char *pill_name(PillEffect e, int known);
const char *pill_color_name(int color_idx);
const char *tarot_name(TarotCard c);
const char *champion_name(ChampionType c);
void enemy_make_champion(Enemy *e, ChampionType c);
void enemy_drop_champion_reward(Game *g, Enemy *e);
void spawn_creep(Game *g, float x, float y, int dmg, int frames);
void creep_update(Game *g);

/* Difficulty helpers */
float diff_enemy_hp_mult(Difficulty d);
float diff_heart_drop_rate(Difficulty d);
float diff_shop_price_mult(Difficulty d);

/* Utility */
float clampf(float v, float lo, float hi);
int   randi(int lo, int hi);
float randf(float lo, float hi);

/* Current room helper */
Room *current_room(Game *g);

/* Item pool access */
const ItemDef *get_item_def(ItemType type);
const FloorInfo *get_floor_info(int floor_num);

/* Boss pool system */
EnemyType select_boss_for_floor(Game *g, int floor_num);
int       get_boss_base_hp(EnemyType boss, int floor_num);
void      push_boss_history(Game *g, EnemyType boss);

/* Curse system (Phase 2) */
const char *curse_name(int curse);
void        roll_curse(Game *g);

/* Unlocks (Phase 2) */
void        unlock_check_after_win(Game *g);
const char *character_unlock_name(int char_idx);
void        render_unlocks_screen(Game *g, C2D_TextBuf textBuf);

/* Consumable system */
void spawn_consumable(Room *r, float x, float y, PickupType type);
void place_bomb(Game *g);
void bomb_update(Game *g);

#endif /* GAME_H */
