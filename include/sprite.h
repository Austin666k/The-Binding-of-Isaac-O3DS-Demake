/*
 * sprite.h - Sprite/texture management for Binding of Isaac 3DS
 * Loads t3x sprite atlases via citro2d and provides helpers for rendering
 */
#ifndef SPRITE_H
#define SPRITE_H

#include <citro2d.h>
#include "sprites_atlas.h"
#include "environment_atlas.h"
#include "ui_items_atlas.h"
#include "menu_text_atlas.h"

/* ---- Sprite Atlas Handles ---- */
extern C2D_SpriteSheet sheet_sprites;     /* entities: player, enemies, bosses, projectiles */
extern C2D_SpriteSheet sheet_environment; /* floor tiles, walls, doors, obstacles */
extern C2D_SpriteSheet sheet_ui_items;    /* UI hearts, healthbar, items */
extern C2D_SpriteSheet sheet_menu_logo;   /* menu logo image */
extern C2D_SpriteSheet sheet_menu_text;   /* hand-drawn menu option text sprites */
extern C2D_SpriteSheet sheet_bullets;     /* tears, blood splatters, impacts */
extern C2D_SpriteSheet sheet_enemies;    /* clotty, pacer animated sprites */

/* ---- Lifecycle ---- */
int  sprites_init(void);   /* Load all sprite sheets; returns 0 on success */
void sprites_free(void);   /* Free all sprite sheets */

/* ---- Drawing helpers ---- */

/* Draw sprite from a sheet at (x,y) centered, with optional scale */
void spr_draw(C2D_SpriteSheet sheet, int idx, float cx, float cy, float scaleX, float scaleY);

/* Draw sprite from a sheet at (x,y) centered with tint (for flash/hit effects) */
void spr_draw_tinted(C2D_SpriteSheet sheet, int idx, float cx, float cy,
                     float scaleX, float scaleY, u32 tintColor, float tintBlend);

/* Draw sprite from a sheet at (x,y) centered with alpha */
void spr_draw_alpha(C2D_SpriteSheet sheet, int idx, float cx, float cy,
                    float scaleX, float scaleY, float alpha);

/* Draw sprite at (x,y) top-left corner (not centered) */
void spr_draw_at(C2D_SpriteSheet sheet, int idx, float x, float y, float scaleX, float scaleY);

/* Draw sprite flipped horizontally */
void spr_draw_fliph(C2D_SpriteSheet sheet, int idx, float cx, float cy, float scaleX, float scaleY);

/* Draw flipped horizontally + tinted (e.g. hit-flash on a left-facing sprite) */
void spr_draw_fliph_tinted(C2D_SpriteSheet sheet, int idx, float cx, float cy,
                           float scaleX, float scaleY, u32 tintColor, float tintBlend);

/* Draw with rotation (angle in radians) centered at (cx,cy) */
void spr_draw_rotated(C2D_SpriteSheet sheet, int idx, float cx, float cy,
                      float scaleX, float scaleY, float angle);

/* Draw with rotation + alpha */
void spr_draw_rotated_alpha(C2D_SpriteSheet sheet, int idx, float cx, float cy,
                            float scaleX, float scaleY, float angle, float alpha);

/* Draw with rotation + tint */
void spr_draw_rotated_tinted(C2D_SpriteSheet sheet, int idx, float cx, float cy,
                             float scaleX, float scaleY, float angle,
                             u32 tintColor, float tintBlend);

/* ---- Player animation helpers ---- */

/* Get the player sprite index based on direction and walk frame */
int player_sprite_idx(int direction, int anim_frame);

/* Get player shooting sprite (crying head) based on shoot direction */
int player_shoot_sprite_idx(int shoot_direction);

/* Get player special state sprites */
int player_hurt_sprite_idx(void);
int player_pickup_sprite_idx(void);
int player_death_sprite_idx(void);

/* Get enemy sprite index based on enemy type */
int enemy_sprite_idx(int enemy_type);

/* Get boss sprite index based on enemy type */
int boss_sprite_idx(int enemy_type);

/* Get item sprite index based on item type (returns index in ui_items sheet) */
int item_sprite_idx(int item_type);

#endif /* SPRITE_H */
