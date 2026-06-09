# 🎨 Sprite Audit — The Binding of Isaac 3DS

> **Generated:** 2026-05-24  
> **Scope:** All game entities vs. all sprite assets across 6 texture atlases  
> **Project:** `binding_of_isaac_3ds/`

---

## Table of Contents

1. [Summary Dashboard](#summary-dashboard)
2. [Player Sprites](#player-sprites)
3. [Enemies](#enemies)
4. [Bosses](#bosses)
5. [Items](#items)
6. [Projectiles & Effects](#projectiles--effects)
7. [Environment & Tiles](#environment--tiles)
8. [UI Elements](#ui-elements)
9. [Menu](#menu)
10. [Unused Sprites](#-unused-sprites-in-atlases-but-not-referenced-in-code)
11. [Entities Using Placeholder Graphics](#-entities-using-placeholder-procedural-graphics)
12. [Items with Mismatched/Shared Sprites](#-items-with-mismatchedshared-sprites)
13. [Uploaded Assets Not Yet Integrated](#-uploaded-assets-not-yet-integrated)
14. [Recommendations](#-recommendations)

---

## Summary Dashboard

| Category | Total Entities | ✅ Has Sprite | 🔧 Placeholder Only | ❌ Missing | 📦 Unused Atlas Sprite |
|---|---|---|---|---|---|
| Player | 1 (15 poses) | 15 | 0 | 0 | 0 |
| Enemies | 6 regular | 6 | 0 | 0 | 0 |
| Bosses | 5 | 5 | 0 | 0 | 0 |
| Items | 20 | 20* | 0 | 0 | 0 |
| Tears (player) | 3 color sets | 3 | 0 | 0 | 0 |
| Enemy Shots | 1 type | 0 | 1 | 1 | 0 |
| Effects | 5 types | 2 | 0 | 0 | 3 |
| Environment | 12 tile types | 10 | 0 | 0 | 2 |
| Doors | 12 variants | 12 | 0 | 0 | 0 |
| UI / HUD | 8 elements | 5 | 1 | 0 | 2 |
| Menu | 1 | 1 | 0 | 0 | 0 |

**\*20 items share only 21 unique sprites — 9 items borrow thematically incorrect sprites. See [Items with Mismatched Sprites](#-items-with-mismatchedshared-sprites).**

**Totals: 8 unused atlas sprites · 2 entities using procedural-only rendering · 9 items need unique sprites**

---

## Player Sprites

**Atlas:** `sprites.t3x` (`sheet_sprites`) — `include/sprites_atlas.h`  
**Renderer:** `render_player()` (main.c:3306) — full sprite rendering with procedural fallback

| Sprite | Atlas Index | Used In | Status |
|---|---|---|---|
| `player_down` | 0 | Walk down frame 0 / idle | ✅ |
| `player_down2` | 1 | Walk down frame 1 | ✅ |
| `player_up` | 2 | Walk up frame 0 / idle | ✅ |
| `player_up2` | 3 | Walk up frame 1 | ✅ |
| `player_left` | 4 | Walk left frame 0 / idle | ✅ |
| `player_left2` | 5 | Walk left frame 1 | ✅ |
| `player_right` | 6 | Walk right frame 0 / idle | ✅ |
| `player_right2` | 7 | Walk right frame 1 | ✅ |
| `player_shoot_down` | 8 | Shooting downward | ✅ |
| `player_shoot_up` | 9 | Shooting upward | ✅ |
| `player_shoot_left` | 10 | Shooting left | ✅ |
| `player_shoot_right` | 11 | Shooting right | ✅ |
| `player_hurt` | 12 | Damage flinch | ✅ |
| `player_pickup` | 13 | Item pickup pose | ✅ |
| `player_death` | 14 | Death animation | ✅ |

**Files:** `gfx_clean/resized/player_*.png` (15 files)  
**Procedural fallback:** Circle body + dot eyes (main.c:3367-3373)

---

## Enemies

### Regular Enemies (sprites_atlas — static sprites)

**Atlas:** `sprites.t3x` (`sheet_sprites`)  
**Renderer:** `render_enemies()` (main.c ~2800-3000)

| Enemy | Enum | Atlas Sprite | Index | Status |
|---|---|---|---|---|
| Fly | `ENEMY_FLY` | `enemy_fly` | 15 | ✅ |
| Gaper | `ENEMY_GAPER` | `enemy_gaper` | 16 | ✅ |
| Pacer | `ENEMY_PACER` | `enemy_pacer` | 17 | ✅ (also has animated atlas) |
| Spider | `ENEMY_SPIDER` | `enemy_spider` | 18 | ✅ |
| Clotty | `ENEMY_CLOTTY` | `enemy_clotty` | 19 | ✅ (also has animated atlas) |
| Small Gaper | `ENEMY_GAPER_SMALL` | `enemy_gaper` (reused) | 16 | ✅ (scaled down) |

**Files:** `gfx_clean/resized/enemy_*.png` (5 files)  
**Procedural fallback:** Colored circles/rectangles (main.c render_enemies else-branch)

### Animated Enemy Sprites (enemies_atlas)

**Atlas:** `enemies.t3x` (`sheet_enemies`) — `include/enemies_atlas.h`  
**Used for:** Clotty and Pacer only — animated multi-frame rendering

| Sprite Set | Frames | Index Range | Used By |
|---|---|---|---|
| `clotty_idle_0..3` | 4 | 0–3 | Clotty idle animation |
| `clotty_shoot_0..3` | 4 | 4–7 | Clotty shooting animation |
| `clotty_angry_0..2` | 3 | 8–10 | Clotty angry state |
| `pacer_walk_00..21` | 22 | 11–32 | Pacer walk cycle |

**Files:** `gfx_clean/resized/enemies/` (33 files)

---

## Bosses

**Atlas:** `sprites.t3x` (`sheet_sprites`) — `include/sprites_atlas.h`  
**Renderer:** `render_enemies()` boss branch (main.c ~2900-3000)

| Boss | Enum | Atlas Sprite | Index | Status | Notes |
|---|---|---|---|---|---|
| Duke of Flies | `ENEMY_BOSS_DUKE` | `boss_duke_of_flies` | 20 | ✅ | Spawns flies (which have sprites) |
| Monstro | `ENEMY_BOSS_MONSTRO` | `boss_monstro` | 21 | ✅ | Jump shadow is procedural circle |
| Gemini | `ENEMY_BOSS_GEMINI` | `boss_gemini` | 22 | ✅ | Small twin uses same sprite (scaled) |
| Larry Jr. | `ENEMY_BOSS_LARRY` | `boss_larry_jr` | 23 | ✅ | Segments reuse same sprite |
| Famine | `ENEMY_BOSS_FAMINE` | `boss_famine` | 24 | ✅ | Charge phase uses same sprite |

**Files:** `gfx_clean/resized/boss_*.png` (5 files)  
**Procedural fallback:** Colored circles with unique colors per boss

---

## Items

**Atlas:** `ui_items.t3x` (`sheet_ui_items`) — `include/ui_items_atlas.h`  
**Renderer:** Pedestal items via `item_sprite_idx()` → `spr_draw()` (main.c:3184-3185)  
**Mapping:** `sprite.c:item_sprite_idx()` (lines 166-223)

| Item | Enum | Maps To Sprite | Atlas Index | Status | Accurate? |
|---|---|---|---|---|---|
| Pentagram | `ITEM_PENTAGRAM` | `item_pentagram` | 8 | ✅ | ✅ Correct |
| The Belt | `ITEM_BELT` | `item_the_belt` | 9 | ✅ | ✅ Correct |
| Wire Coat Hanger | `ITEM_WIRE_COAT` | `item_technology` | 18 | ✅ | ⚠️ Wrong sprite |
| Lunch | `ITEM_LUNCH` | `item_coin` | 24 | ✅ | ⚠️ Wrong sprite |
| Cupid's Arrow | `ITEM_CUPIDS_ARROW` | `item_moms_knife` | 19 | ✅ | ⚠️ Wrong sprite |
| Spoon Bender | `ITEM_SPOON_BENDER` | `item_spoon_bender` | 27 | ✅ | ✅ Correct |
| Polyphemus | `ITEM_POLYPHEMUS` | `item_polyphemus` | 12 | ✅ | ✅ Correct |
| Inner Eye | `ITEM_INNER_EYE` | `item_inner_eye` | 28 | ✅ | ✅ Correct |
| Spelunker Hat | `ITEM_SPELUNKER_HAT` | `item_bomb` | 22 | ✅ | ⚠️ Wrong sprite |
| Speed Ball | `ITEM_SPEED_BALL` | `item_key` | 23 | ✅ | ⚠️ Wrong sprite |
| Magic Mushroom | `ITEM_MAGIC_MUSH` | `item_magic_mushroom` | 10 | ✅ | ✅ Correct |
| Sacred Heart | `ITEM_SACRED_HEART` | `item_sacred_heart` | 11 | ✅ | ✅ Correct |
| Spirit Sword | `ITEM_SPIRIT_SWORD` | `item_brimstone` | 17 | ✅ | ⚠️ Wrong sprite |
| Blood of the Martyr | `ITEM_BLOOD_OF_MARTYR` | `item_dead_cat` | 16 | ✅ | ⚠️ Wrong sprite |
| Stigmata | `ITEM_STIGMATA` | `item_crickets_head` | 15 | ✅ | ⚠️ Wrong sprite |
| Sad Onion | `ITEM_SAD_ONION` | `item_sad_onion` | 13 | ✅ | ✅ Correct |
| Wire Hanger | `ITEM_WIRE_HANGER` | `item_the_pact` | 20 | ✅ | ⚠️ Wrong sprite |
| Growth Hormones | `ITEM_GROWTH_HORMONES` | `item_steven` | 26 | ✅ | ⚠️ Borrowed |
| Jesus Juice | `ITEM_JESUS_JUICE` | `item_steven` | 26 | ✅ | ⚠️ Borrowed (duplicate) |
| The Halo | `ITEM_HALO` | `item_halo` | 14 | ✅ | ✅ Correct |

**Files:** `gfx_clean/resized/item_*.png` (21 files — all in atlas)

---

## Projectiles & Effects

**Atlas:** `bulletatlas.t3x` (`sheet_bullets`) — `include/bulletatlas.h`  
**Atlas:** `sprites.t3x` (`sheet_sprites`) — effects in sprites atlas

### Player Tears ✅

**Renderer:** `render_tears()` (main.c:3387-3470)

| Tear Type | Sprites | Index Range | Status |
|---|---|---|---|
| Blue (normal) | `tear_blue_1..7` | 0–6 | ✅ 7 size variants |
| Red (enemy) | `tear_red_1..7` | 7–13 | ✅ 7 size variants |
| Dark (homing) | `tear_dark_1..7` | 14–20 | ✅ 7 size variants |

### Tear Effects ✅

| Effect | Sprites | Index Range | Status |
|---|---|---|---|
| Tear pop/splash | `tear_pop_1..4` | 21–24 | ✅ Used in `spawn_tear_pop()` |
| Blood splat (small) | `blood_splat_small_1..3` | 25–27 | ✅ Used in `spawn_blood_splatter()` |
| Blood splat (medium) | `blood_splat_med_1` | 28 | ✅ Used in blood particles |
| Blood splat (large) | `blood_splat_large_1..3` | 29–31 | ✅ Used in blood particles |
| Blood splat (huge) | `blood_splat_huge_1..2` | 32–33 | ✅ Used in blood particles |

### Enemy Shots ❌

**Renderer:** `render_enemy_shots()` (main.c:3893-3901) — **procedural only!**

| Entity | Current Rendering | Status |
|---|---|---|
| `EnemyShot` | `C2D_DrawCircleSolid()` — dark red circle + highlight | 🔧 **Placeholder — no sprite** |

Red tear sprites (`tear_red_*`) exist in the bullet atlas and could be used.

### Unused Projectile/Effect Sprites

| Sprite | Atlas | Index Range | Status |
|---|---|---|---|
| `tear_trail_1..3` | bulletatlas | 34–36 | 📦 **Unused** — not referenced in code |
| `projectile_tear` | sprites | 25 | 📦 **Unused** — bullet atlas tears used instead |
| `effect_blood_1` | sprites | 26 | 📦 **Unused** — bullet atlas blood splats used instead |
| `effect_blood_2` | sprites | 27 | 📦 **Unused** — bullet atlas blood splats used instead |
| `effect_explosion` | sprites | 28 | 📦 **Unused** — no explosion mechanic implemented |

---

## Environment & Tiles

**Atlas:** `environment.t3x` (`sheet_environment`) — `include/environment_atlas.h`  
**Renderer:** `render_room()` (main.c:3035-3300)

### Floor Tiles ✅

| Sprite | Index | Used For | Status |
|---|---|---|---|
| `env_floor_clean` | 0 | Normal rooms (floors 0-1) | ✅ |
| `env_floor_bloody` | 1 | Boss rooms | ✅ |
| `env_floor_cracked` | 2 | Deep floors (≥2) | ✅ |

### Wall Tiles ✅

| Sprite | Index | Used For | Status |
|---|---|---|---|
| `env_wall_top` | 16 | Top wall | ✅ |
| `env_wall_bottom` | 17 | Bottom wall | ✅ |
| `env_wall_left` | 18 | Left wall | ✅ |
| `env_wall_right` | 19 | Right wall | ✅ |
| `env_corner_tl` | 12 | Top-left corner | ✅ |
| `env_corner_tr` | 13 | Top-right corner | ✅ |
| `env_corner_bl` | 14 | Bottom-left corner | ✅ |
| `env_corner_br` | 15 | Bottom-right corner | ✅ |

### Doors ✅

| Direction | Normal | Treasure | Boss |
|---|---|---|---|
| Top | ✅ `env_door_top` (6) | ✅ `env_door_treasure_top` (20) | ✅ `env_door_boss_top` (24) |
| Bottom | ✅ `env_door_bottom` (7) | ✅ `env_door_treasure_bottom` (21) | ✅ `env_door_boss_bottom` (25) |
| Left | ✅ `env_door_left` (8) | ✅ `env_door_treasure_left` (22) | ✅ `env_door_boss_left` (26) |
| Right | ✅ `env_door_right` (9) | ✅ `env_door_treasure_right` (23) | ✅ `env_door_boss_right` (27) |

### Obstacles & Objects ✅

| Sprite | Index | Used For | Status |
|---|---|---|---|
| `env_rock` | 4 | Rock obstacles | ✅ |
| `env_trapdoor` | 5 | Floor trapdoor | ✅ |

### Unused Environment Sprites

| Sprite | Index | Status | Notes |
|---|---|---|---|
| `env_stone_wall` | 3 | 📦 **Unused** | Generic wall texture — directional walls used instead |
| `env_wall_graffiti` | 10 | 📦 **Unused** | Decorative wall overlay — never rendered |
| `env_wall_blood` | 11 | 📦 **Unused** | Decorative wall overlay — never rendered |

---

## UI Elements

**Atlas:** `ui_items.t3x` (`sheet_ui_items`) — `include/ui_items_atlas.h`  
**Renderer:** `render_hud()` (main.c:2674-2875)

### Hearts (HUD) ✅

| Sprite | Index | Used In | Status |
|---|---|---|---|
| `heart_red_full` | 0 | HUD + pickups | ✅ |
| `heart_red_half` | 1 | HUD + pickups | ✅ |
| `heart_red_empty` | 2 | HUD empty containers | ✅ |
| `heart_soul_full` | 3 | Soul heart pickups | ✅ |

### Unused Heart Sprites

| Sprite | Index | Status | Notes |
|---|---|---|---|
| `heart_soul_half` | 4 | 📦 **Unused** | Half soul hearts not implemented in gameplay |
| `heart_black_full` | 5 | 📦 **Unused** | Black hearts not implemented in gameplay |

### Other UI ✅

| Sprite | Index | Used In | Status |
|---|---|---|---|
| `ui_boss_healthbar` | 6 | Boss health bar bg | ✅ |
| `ui_item_pickup` | 7 | Item pickup overlay | 🔧 **Unclear** — pedestal rendering is procedural glow |

### Pedestal Rendering 🔧

The item pedestal base is rendered **procedurally** (rectangles + circles at main.c:3152-3179). The `ui_item_pickup` sprite exists but the pedestal itself doesn't use it — only the item on top uses `item_sprite_idx()`.

---

## Menu

**Atlas:** `menu_logo.t3x` (`sheet_menu_logo`) — `include/menu_logo_atlas.h`  
**Renderer:** `render_menu()` (main.c:2614-2696)

| Sprite | Index | Status | Notes |
|---|---|---|---|
| `menu_logo` | 0 | ✅ | Animated with pulse + bob + glow shadow |

**Fallback:** Text title "The Binding of Isaac" rendered with C2D_Text if logo fails to load.

---

## 📦 Unused Sprites (In Atlases But Not Referenced in Code)

| # | Sprite | Atlas | File Path | Why Unused |
|---|---|---|---|---|
| 1 | `env_stone_wall` | environment | `gfx_clean/resized/env_stone_wall.png` | Replaced by directional wall tiles |
| 2 | `env_wall_graffiti` | environment | `gfx_clean/resized/env_wall_graffiti.png` | Decorative overlay never implemented |
| 3 | `env_wall_blood` | environment | `gfx_clean/resized/env_wall_blood.png` | Decorative overlay never implemented |
| 4 | `heart_soul_half` | ui_items | `gfx_clean/resized/ui_hearts/heart_soul_half.png` | Half soul hearts not in gameplay |
| 5 | `heart_black_full` | ui_items | `gfx_clean/resized/ui_hearts/heart_black_full.png` | Black hearts not in gameplay |
| 6 | `projectile_tear` | sprites | `gfx_clean/resized/projectile_tear.png` | Superseded by bullet atlas tear variants |
| 7 | `effect_blood_1` | sprites | `gfx_clean/resized/effect_blood_1.png` | Superseded by bullet atlas blood splats |
| 8 | `effect_blood_2` | sprites | `gfx_clean/resized/effect_blood_2.png` | Superseded by bullet atlas blood splats |
| 9 | `effect_explosion` | sprites | `gfx_clean/resized/effect_explosion.png` | No explosion mechanic in game |
| 10 | `tear_trail_1..3` | bulletatlas | `gfx_clean/resized/bullets/tear_trail_*.png` | Trail rendering not implemented |

---

## 🔧 Entities Using Placeholder (Procedural) Graphics

| # | Entity | Current Rendering | Suggested Fix |
|---|---|---|---|
| 1 | **Enemy Shots** (`EnemyShot`) | Dark red circle + highlight dot (main.c:3898-3899) | Use `tear_red_*` sprites from bullet atlas with smaller scale |
| 2 | **Item Pedestal base** | Procedural rectangles + glow circles (main.c:3152-3179) | Could use `ui_item_pickup` sprite for pedestal base |

### Entities with Both Sprite + Procedural Paths (Working Correctly)

Every major entity has a procedural fallback gated by `g_sprites_loaded`. These fallbacks activate only if atlas loading fails:

- **Player:** Circle body + dot eyes → 15 sprite poses
- **All enemies:** Colored circles → individual sprites + animated sheets
- **All bosses:** Colored circles → boss sprites
- **Tears:** Colored circles → 21 sized tear sprites
- **Room (floor/walls/doors):** Colored rectangles → tiled sprites
- **Obstacles:** Brown rectangles → rock sprites
- **Trapdoor:** Concentric circles → trapdoor sprite
- **Hearts (HUD):** Procedural heart shape (`draw_heart()`) → heart sprites
- **Boss health bar:** Green rectangle → `ui_boss_healthbar` sprite
- **Menu logo:** Text fallback → logo sprite

---

## ⚠️ Items with Mismatched/Shared Sprites

These items are implemented in gameplay but display **another item's** sprite on the pedestal:

| # | Item (Game Logic) | Displays As | Actual Sprite | Why |
|---|---|---|---|---|
| 1 | Wire Coat Hanger | Technology | `item_technology` | No wire coat hanger sprite exists |
| 2 | Lunch | Coin | `item_coin` | No lunch sprite exists |
| 3 | Cupid's Arrow | Mom's Knife | `item_moms_knife` | No cupid's arrow sprite exists |
| 4 | Spelunker Hat | Bomb | `item_bomb` | No spelunker hat sprite exists |
| 5 | Speed Ball | Key | `item_key` | No speed ball sprite exists |
| 6 | Spirit Sword | Brimstone | `item_brimstone` | No spirit sword sprite exists |
| 7 | Blood of the Martyr | Dead Cat | `item_dead_cat` | No blood of martyr sprite exists |
| 8 | Stigmata | Cricket's Head | `item_crickets_head` | No stigmata sprite exists |
| 9 | Wire Hanger | The Pact | `item_the_pact` | No wire hanger sprite exists |
| 10 | Growth Hormones | Steven | `item_steven` | No growth hormones sprite exists |
| 11 | Jesus Juice | Steven | `item_steven` | No jesus juice sprite exists (shares with #10) |

**Items with correct, unique sprites (9/20):** Pentagram, The Belt, Spoon Bender, Polyphemus, Inner Eye, Magic Mushroom, Sacred Heart, Sad Onion, The Halo

---

## 📥 Uploaded Assets Not Yet Integrated

The following cleaned assets exist in `/home/ubuntu/Downloads/` but are **not** yet added to any atlas:

| File | Potential Use | Corresponding Atlas Entry |
|---|---|---|
| `item_belt_clean-Photoroom.png` | Redundant — Belt already in atlas | `item_the_belt` ✅ already exists |
| `item_trophy_clean-Photoroom.png` | **New** — no trophy item in atlas | Could be used for a new item |
| `item_cricket_clean-Photoroom.png` | Redundant — Cricket's Head already in atlas | `item_crickets_head` ✅ already exists |
| `item_cat_clean-Photoroom.png` | Redundant — Dead Cat already in atlas | `item_dead_cat` ✅ already exists |
| `item_knife_clean-Photoroom.png` | Redundant — Mom's Knife already in atlas | `item_moms_knife` ✅ already exists |
| `item_onion_clean-Photoroom.png` | Redundant — Sad Onion already in atlas | `item_sad_onion` ✅ already exists |
| `item_pact_clean-Photoroom.png` | Redundant — The Pact already in atlas | `item_the_pact` ✅ already exists |
| `item_pentagram_clean-Photoroom.png` | Redundant — Pentagram already in atlas | `item_pentagram` ✅ already exists |
| `item_tarot_clean-Photoroom.png` | Redundant — Tarot Card already in atlas | `item_tarot_card` ✅ already exists |
| `item_tech_clean-Photoroom.png` | Redundant — Technology already in atlas | `item_technology` ✅ already exists |
| `item_bomb_clean-Photoroom.png` | Redundant | `item_bomb` ✅ already exists |
| `item_brimstone_clean-Photoroom.png` | Redundant | `item_brimstone` ✅ already exists |
| `item_coin_clean-Photoroom.png` | Redundant | `item_coin` ✅ already exists |
| `item_halo_clean-Photoroom.png` | Redundant | `item_halo` ✅ already exists |
| `item_key_clean-Photoroom.png` | Redundant | `item_key` ✅ already exists |
| `env_*_clean-Photoroom.png` (10 files) | Redundant — environment tiles already in atlas | All environment sprites ✅ already exist |
| `ui_*_clean-Photoroom.png` (5 files) | Redundant — UI sprites already in atlas | All UI sprites ✅ already exist |
| `projectile_tear_clean-Photoroom.png` | Redundant | `projectile_tear` exists (though unused) |

---

## 🔧 Recommendations

### Priority 1 — Quick Wins (No new art needed)

1. **Use red tear sprites for enemy shots** — `render_enemy_shots()` currently draws procedural circles. Replace with `tear_red_*` sprites from the bullet atlas at a smaller scale. This is a ~10 line code change.

2. **Use `ui_item_pickup` for pedestal base** — The sprite exists in the atlas but isn't used. Could replace the procedural pedestal rectangles.

3. **Implement tear trails** — `tear_trail_1..3` sprites exist in the bullet atlas. Could be drawn behind fast-moving or piercing tears (the piercing trail currently reuses the tear sprite itself).

### Priority 2 — Decorative Polish (No new art needed)

4. **Use `env_wall_graffiti` and `env_wall_blood`** — Randomly overlay these on wall segments for visual variety.

5. **Implement soul/black heart types** — Sprites for `heart_soul_half` and `heart_black_full` are already in the atlas. Gameplay code just needs to support these heart types.

### Priority 3 — New Art Needed

6. **Create unique sprites for 11 mismatched items:**
   - Wire Coat Hanger, Lunch, Cupid's Arrow, Spelunker Hat, Speed Ball
   - Spirit Sword, Blood of the Martyr, Stigmata, Wire Hanger
   - Growth Hormones, Jesus Juice

7. **Use `effect_explosion` sprite** — If bomb items or explosive effects are ever added, this sprite is ready.

### Priority 4 — Cleanup

8. **Remove `projectile_tear` from sprites atlas** — It's fully superseded by the bullet atlas tear variants. Removing it saves atlas space.

9. **Remove `effect_blood_1/2` from sprites atlas** — Fully superseded by bullet atlas blood splats.

---

## Atlas Summary

| Atlas File | Sheet Handle | Header | Sprites | Size |
|---|---|---|---|---|
| `romfs/sprites.t3x` | `sheet_sprites` | `sprites_atlas.h` | 29 | Player(15) + Enemy(5) + Boss(5) + Projectile/Effect(4) |
| `romfs/environment.t3x` | `sheet_environment` | `environment_atlas.h` | 28 | Floor(3) + Walls(8) + Doors(12) + Rock + Trapdoor + Decor(3) |
| `romfs/ui_items.t3x` | `sheet_ui_items` | `ui_items_atlas.h` | 29 | Hearts(6) + UI(2) + Items(21) |
| `romfs/bulletatlas.t3x` | `sheet_bullets` | `bulletatlas.h` | 37 | Tears(21) + Pop(4) + Blood(9) + Trail(3) |
| `romfs/enemies.t3x` | `sheet_enemies` | `enemies_atlas.h` | 33 | Clotty(11) + Pacer(22) |
| `romfs/menu_logo.t3x` | `sheet_menu_logo` | `menu_logo_atlas.h` | 1 | Menu logo |

**Total atlas sprites: 157**  
**Actually used in rendering code: 147**  
**Unused: 10 sprites across 3 atlases**
