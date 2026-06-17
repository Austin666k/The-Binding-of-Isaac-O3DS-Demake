/*
 * sprite.c - Sprite/texture management for Binding of Isaac 3DS
 */

#include "sprite.h"
#include "game.h"

/* ---- Global sheet handles ---- */
C2D_SpriteSheet sheet_sprites     = NULL;
C2D_SpriteSheet sheet_environment = NULL;
C2D_SpriteSheet sheet_ui_items    = NULL;
C2D_SpriteSheet sheet_menu_logo   = NULL;
C2D_SpriteSheet sheet_menu_text   = NULL;
C2D_SpriteSheet sheet_bullets     = NULL;
C2D_SpriteSheet sheet_enemies    = NULL;

/* ---- Lifecycle ---- */

int sprites_init(void) {
    sheet_sprites     = C2D_SpriteSheetLoad("romfs:/sprites.t3x");
    sheet_environment = C2D_SpriteSheetLoad("romfs:/environment.t3x");
    sheet_ui_items    = C2D_SpriteSheetLoad("romfs:/ui_items.t3x");
    sheet_menu_logo   = C2D_SpriteSheetLoad("romfs:/menu_logo.t3x");
    sheet_menu_text   = C2D_SpriteSheetLoad("romfs:/menu_text.t3x");
    sheet_bullets     = C2D_SpriteSheetLoad("romfs:/bulletatlas.t3x");
    sheet_enemies    = C2D_SpriteSheetLoad("romfs:/enemies.t3x");

    if (!sheet_sprites || !sheet_environment || !sheet_ui_items) {
        /* Fallback: if any sheet fails, free what loaded and return error */
        sprites_free();
        return -1;
    }
    return 0;
}

void sprites_free(void) {
    if (sheet_sprites)     { C2D_SpriteSheetFree(sheet_sprites);     sheet_sprites = NULL; }
    if (sheet_environment) { C2D_SpriteSheetFree(sheet_environment); sheet_environment = NULL; }
    if (sheet_ui_items)    { C2D_SpriteSheetFree(sheet_ui_items);    sheet_ui_items = NULL; }
    if (sheet_menu_logo)   { C2D_SpriteSheetFree(sheet_menu_logo);   sheet_menu_logo = NULL; }
    if (sheet_menu_text)   { C2D_SpriteSheetFree(sheet_menu_text);   sheet_menu_text = NULL; }
    if (sheet_bullets)     { C2D_SpriteSheetFree(sheet_bullets);     sheet_bullets = NULL; }
    if (sheet_enemies)    { C2D_SpriteSheetFree(sheet_enemies);    sheet_enemies = NULL; }
}

/* ---- Drawing helpers ---- */

void spr_draw(C2D_SpriteSheet sheet, int idx, float cx, float cy,
              float scaleX, float scaleY) {
    if (!sheet) return;
    C2D_Image img = C2D_SpriteSheetGetImage(sheet, idx);
    float w = img.subtex->width * scaleX;
    float h = img.subtex->height * scaleY;
    C2D_DrawImageAt(img, cx - w / 2.0f, cy - h / 2.0f, 0, NULL, scaleX, scaleY);
}

void spr_draw_tinted(C2D_SpriteSheet sheet, int idx, float cx, float cy,
                     float scaleX, float scaleY, u32 tintColor, float tintBlend) {
    if (!sheet) return;
    C2D_Image img = C2D_SpriteSheetGetImage(sheet, idx);
    float w = img.subtex->width * scaleX;
    float h = img.subtex->height * scaleY;

    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, tintColor, tintBlend);

    C2D_DrawImageAt(img, cx - w / 2.0f, cy - h / 2.0f, 0, &tint, scaleX, scaleY);
}

void spr_draw_alpha(C2D_SpriteSheet sheet, int idx, float cx, float cy,
                    float scaleX, float scaleY, float alpha) {
    if (!sheet) return;
    C2D_Image img = C2D_SpriteSheetGetImage(sheet, idx);
    float w = img.subtex->width * scaleX;
    float h = img.subtex->height * scaleY;

    C2D_ImageTint tint;
    C2D_AlphaImageTint(&tint, alpha);

    C2D_DrawImageAt(img, cx - w / 2.0f, cy - h / 2.0f, 0, &tint, scaleX, scaleY);
}

void spr_draw_at(C2D_SpriteSheet sheet, int idx, float x, float y,
                 float scaleX, float scaleY) {
    if (!sheet) return;
    C2D_Image img = C2D_SpriteSheetGetImage(sheet, idx);
    C2D_DrawImageAt(img, x, y, 0, NULL, scaleX, scaleY);
}

void spr_draw_fliph(C2D_SpriteSheet sheet, int idx, float cx, float cy,
                    float scaleX, float scaleY) {
    if (!sheet) return;
    C2D_Image img = C2D_SpriteSheetGetImage(sheet, idx);
    float w = img.subtex->width * scaleX;
    float h = img.subtex->height * scaleY;
    /* Negative scaleX = horizontal flip; adjust position so it stays centered */
    C2D_DrawImageAt(img, cx + w / 2.0f, cy - h / 2.0f, 0, NULL, -scaleX, scaleY);
}

void spr_draw_fliph_tinted(C2D_SpriteSheet sheet, int idx, float cx, float cy,
                           float scaleX, float scaleY, u32 tintColor, float tintBlend) {
    if (!sheet) return;
    C2D_Image img = C2D_SpriteSheetGetImage(sheet, idx);
    float w = img.subtex->width * scaleX;
    float h = img.subtex->height * scaleY;
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, tintColor, tintBlend);
    /* Negative scaleX = horizontal flip; adjust position so it stays centered */
    C2D_DrawImageAt(img, cx + w / 2.0f, cy - h / 2.0f, 0, &tint, -scaleX, scaleY);
}

void spr_draw_rotated(C2D_SpriteSheet sheet, int idx, float cx, float cy,
                      float scaleX, float scaleY, float angle) {
    if (!sheet) return;
    C2D_Image img = C2D_SpriteSheetGetImage(sheet, idx);
    C2D_Sprite spr;
    C2D_SpriteFromImage(&spr, img);
    C2D_SpriteSetCenter(&spr, 0.5f, 0.5f);
    C2D_SpriteSetPos(&spr, cx, cy);
    C2D_SpriteSetScale(&spr, scaleX, scaleY);
    C2D_SpriteSetRotation(&spr, angle);
    C2D_DrawSprite(&spr);
}

void spr_draw_rotated_alpha(C2D_SpriteSheet sheet, int idx, float cx, float cy,
                            float scaleX, float scaleY, float angle, float alpha) {
    if (!sheet) return;
    C2D_Image img = C2D_SpriteSheetGetImage(sheet, idx);
    C2D_Sprite spr;
    C2D_SpriteFromImage(&spr, img);
    C2D_SpriteSetCenter(&spr, 0.5f, 0.5f);
    C2D_SpriteSetPos(&spr, cx, cy);
    C2D_SpriteSetScale(&spr, scaleX, scaleY);
    C2D_SpriteSetRotation(&spr, angle);
    C2D_ImageTint tint;
    C2D_AlphaImageTint(&tint, alpha);
    C2D_DrawSpriteTinted(&spr, &tint);
}

void spr_draw_rotated_tinted(C2D_SpriteSheet sheet, int idx, float cx, float cy,
                             float scaleX, float scaleY, float angle,
                             u32 tintColor, float tintBlend) {
    if (!sheet) return;
    C2D_Image img = C2D_SpriteSheetGetImage(sheet, idx);
    C2D_Sprite spr;
    C2D_SpriteFromImage(&spr, img);
    C2D_SpriteSetCenter(&spr, 0.5f, 0.5f);
    C2D_SpriteSetPos(&spr, cx, cy);
    C2D_SpriteSetScale(&spr, scaleX, scaleY);
    C2D_SpriteSetRotation(&spr, angle);
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, tintColor, tintBlend);
    C2D_DrawSpriteTinted(&spr, &tint);
}

/* ---- Player animation helpers ---- */

int player_sprite_idx(int direction, int anim_frame) {
    int frame = (anim_frame % 2);  /* 0 or 1 for walking animation */
    switch (direction) {
    case DIR_DOWN:  return frame == 0 ? sprites_atlas_player_down_idx : sprites_atlas_player_down2_idx;
    case DIR_UP:    return frame == 0 ? sprites_atlas_player_up_idx   : sprites_atlas_player_up2_idx;
    case DIR_LEFT:  return frame == 0 ? sprites_atlas_player_left_idx : sprites_atlas_player_left2_idx;
    case DIR_RIGHT: return frame == 0 ? sprites_atlas_player_right_idx: sprites_atlas_player_right2_idx;
    default:        return sprites_atlas_player_down_idx;
    }
}

int player_shoot_sprite_idx(int shoot_direction) {
    switch (shoot_direction) {
    case DIR_DOWN:  return sprites_atlas_player_shoot_down_idx;
    case DIR_UP:    return sprites_atlas_player_shoot_up_idx;
    case DIR_LEFT:  return sprites_atlas_player_shoot_left_idx;
    case DIR_RIGHT: return sprites_atlas_player_shoot_right_idx;
    default:        return sprites_atlas_player_shoot_down_idx;
    }
}

int player_hurt_sprite_idx(void) {
    return sprites_atlas_player_hurt_idx;
}

int player_pickup_sprite_idx(void) {
    return sprites_atlas_player_pickup_idx;
}

int player_death_sprite_idx(void) {
    return sprites_atlas_player_death_idx;
}

int enemy_sprite_idx(int enemy_type) {
    switch (enemy_type) {
    case ENEMY_FLY:         return sprites_atlas_enemy_fly_idx;
    case ENEMY_GAPER:       return sprites_atlas_enemy_gaper_idx;
    case ENEMY_GAPER_SMALL: return sprites_atlas_enemy_gaper_idx;
    case ENEMY_PACER:       return sprites_atlas_enemy_pacer_idx;
    case ENEMY_SPIDER:      return sprites_atlas_enemy_spider_idx;
    case ENEMY_CLOTTY:      return sprites_atlas_enemy_clotty_idx;
    /* New enemies */
    case ENEMY_ATTACK_FLY:  return sprites_atlas_enemy_attack_fly_idx;
    case ENEMY_POOTER:      return sprites_atlas_enemy_pooter_idx;
    case ENEMY_HOPPER:      return sprites_atlas_enemy_hopper_idx;
    case ENEMY_BABY:        return sprites_atlas_enemy_baby_idx;
    case ENEMY_GLOBIN:      return sprites_atlas_enemy_globin_idx;
    case ENEMY_BOOM_FLY:    return sprites_atlas_enemy_boom_fly_idx;
    case ENEMY_MAW:         return sprites_atlas_enemy_maw_idx;
    case ENEMY_MULLIGAN:    return sprites_atlas_enemy_mulligan_idx;
    case ENEMY_HOST:        return sprites_atlas_enemy_host_idx;
    case ENEMY_RED_MAW:     return sprites_atlas_enemy_red_maw_idx;
    case ENEMY_LEAPER:      return sprites_atlas_enemy_leaper_idx;
    case ENEMY_VIS:         return sprites_atlas_enemy_vis_idx;
    /* Phase 2 minions: reuse compatible sprites until atlas adds dedicated ones */
    case ENEMY_EYE:         return sprites_atlas_enemy_fly_idx;       /* Peep's detached eyes */
    case ENEMY_LIL_HAUNT:   return sprites_atlas_enemy_fly_idx;       /* Haunt's small ghosts */
    default:                return sprites_atlas_enemy_fly_idx;
    }
}

int boss_sprite_idx(int enemy_type) {
    switch (enemy_type) {
    case ENEMY_BOSS_DUKE:    return sprites_atlas_boss_duke_of_flies_idx;
    case ENEMY_BOSS_MONSTRO: return sprites_atlas_boss_monstro_idx;
    case ENEMY_BOSS_GEMINI:  return sprites_atlas_boss_gemini_idx;
    case ENEMY_BOSS_LARRY:   return sprites_atlas_boss_larry_jr_idx;
    case ENEMY_BOSS_FAMINE:  return sprites_atlas_boss_famine_idx;
    /* Phase 2 bosses: reuse closest matching sprite until atlas adds dedicated ones */
    case ENEMY_BOSS_PEEP:      return sprites_atlas_boss_monstro_idx;     /* Peep - bouncy fat boss like Monstro */
    case ENEMY_BOSS_GURDY:     return sprites_atlas_boss_monstro_idx;     /* Gurdy - large stationary boss */
    case ENEMY_BOSS_PIN:       return sprites_atlas_boss_larry_jr_idx;    /* Pin - worm like Larry Jr */
    case ENEMY_BOSS_HAUNT:     return sprites_atlas_boss_duke_of_flies_idx;/* Haunt - ghostly boss */
    case ENEMY_BOSS_WIDOW:     return sprites_atlas_boss_duke_of_flies_idx;/* Widow - spider boss */
    case ENEMY_BOSS_MEGA_SATAN: return sprites_atlas_boss_famine_idx;     /* Mega Satan - hellish final boss */
    default:                 return sprites_atlas_boss_duke_of_flies_idx;
    }
}

int item_sprite_idx(int item_type) {
    switch (item_type) {
    case ITEM_PENTAGRAM:       return ui_items_atlas_item_pentagram_idx;
    case ITEM_BELT:            return ui_items_atlas_item_the_belt_idx;
    case ITEM_MAGIC_MUSH:      return ui_items_atlas_item_magic_mushroom_idx;
    case ITEM_SACRED_HEART:    return ui_items_atlas_item_sacred_heart_idx;
    case ITEM_POLYPHEMUS:      return ui_items_atlas_item_polyphemus_idx;
    case ITEM_SAD_ONION:       return ui_items_atlas_item_sad_onion_idx;
    case ITEM_HALO:            return ui_items_atlas_item_halo_idx;
    case ITEM_DEAD_CAT:        return ui_items_atlas_item_dead_cat_idx;
    case ITEM_BRIMSTONE:       return ui_items_atlas_item_brimstone_idx;
    case ITEM_TECHNOLOGY:      return ui_items_atlas_item_technology_idx;
    case ITEM_MOMS_KNIFE:      return ui_items_atlas_item_moms_knife_idx;
    case ITEM_NUMBER_ONE:      return ui_items_atlas_item_number_one_idx;
    case ITEM_SPOON_BENDER:    return ui_items_atlas_item_spoon_bender_idx;
    case ITEM_INNER_EYE:       return ui_items_atlas_item_inner_eye_idx;
    case ITEM_YUM_HEART:       return ui_items_atlas_heart_red_full_idx;
    case ITEM_BOOK_OF_BELIAL:  return ui_items_atlas_item_the_pact_idx;
    /* --- Fixed: each item now has its own unique sprite --- */
    case ITEM_WIRE_COAT:       return ui_items_atlas_item_wire_coat_hanger_idx;
    case ITEM_LUNCH:           return ui_items_atlas_item_lunch_idx;
    case ITEM_CUPIDS_ARROW:    return ui_items_atlas_item_cupids_arrow_idx;
    case ITEM_SPELUNKER_HAT:   return ui_items_atlas_item_spelunker_hat_idx;
    case ITEM_SPEED_BALL:      return ui_items_atlas_item_speed_ball_idx;
    case ITEM_SPIRIT_SWORD:    return ui_items_atlas_item_spirit_sword_idx;
    case ITEM_BLOOD_OF_MARTYR: return ui_items_atlas_item_blood_of_martyr_idx;
    case ITEM_STIGMATA:        return ui_items_atlas_item_stigmata_idx;
    case ITEM_WIRE_HANGER:     return ui_items_atlas_item_wire_hanger_idx;
    case ITEM_GROWTH_HORMONES: return ui_items_atlas_item_growth_hormones_idx;
    case ITEM_JESUS_JUICE:     return ui_items_atlas_item_jesus_juice_idx;
    /* --- New actives / battery / familiars: map to the closest existing art
     * (no dedicated icons yet; better than everything showing a pentagram) --- */
    case ITEM_NECRONOMICON:    return ui_items_atlas_item_the_pact_idx;
    case ITEM_BIBLE:           return ui_items_atlas_item_the_pact_idx;
    case ITEM_FORGET_ME_NOW:   return ui_items_atlas_item_the_pact_idx;
    case ITEM_MOMS_BRA:        return ui_items_atlas_item_moms_knife_idx;
    case ITEM_BATTERY:         return ui_items_atlas_item_technology_idx;
    case ITEM_BROTHER_BOBBY:   return ui_items_atlas_item_sacred_heart_idx;
    case ITEM_SISTER_MAGGY:    return ui_items_atlas_heart_red_full_idx;
    case ITEM_LITTLE_STEVEN:   return ui_items_atlas_item_spoon_bender_idx;
    case ITEM_DEMON_BABY:      return ui_items_atlas_item_pentagram_idx;
    case ITEM_D6:              return ui_items_atlas_item_pentagram_idx;
    default:                   return ui_items_atlas_item_pentagram_idx;
    }
}
