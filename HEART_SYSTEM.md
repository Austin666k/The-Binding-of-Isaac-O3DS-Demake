# Heart System Documentation

## Overview
The game now has proper UI heart sprites extracted from the official *The Binding of Isaac: Rebirth* sprite sheets. These hearts support multiple types for different health mechanics.

## Heart Types

### Red Hearts (Standard Health)
- **Full**: `heart_red_full` (index 0)
- **Half**: `heart_red_half` (index 1)
- **Empty**: `heart_red_empty` (index 2)

Red hearts are the standard health containers. Each full heart = 2 hit points.

### Soul Hearts (Bonus Health)
- **Full**: `heart_soul_full` (index 3)
- **Half**: `heart_soul_half` (index 4)

Soul hearts float above red hearts and are consumed first when taking damage. They're typically blue/white in appearance.

### Black Hearts (Special Health)
- **Full**: `heart_black_full` (index 5)

Black hearts provide 2 hit points and deal damage to all enemies in the room when depleted.

## File Locations

### Source Sprites (Original Size: ~16x15)
- `gfx_clean/ui/hearts/heart_red_full.png`
- `gfx_clean/ui/hearts/heart_red_half.png`
- `gfx_clean/ui/hearts/heart_red_empty.png`
- `gfx_clean/ui/hearts/heart_soul_full.png`
- `gfx_clean/ui/hearts/heart_soul_half.png`
- `gfx_clean/ui/hearts/heart_black_full.png`

### Resized Sprites (2x Scale: 32x30)
Located in: `gfx_clean/resized/ui_hearts/`

These are scaled 2x using nearest-neighbor for pixel-perfect upscaling, making them more visible on the 3DS screen.

## Atlas Integration

### Atlas File: `gfx/ui_items.t3s`
The heart sprites are now included at the beginning of the UI items atlas. To rebuild the atlas after changes:

```bash
./rebuild_ui_atlas.sh
```

Or manually:
```bash
tex3ds -i gfx/ui_items.t3s -o romfs/ui_items.t3x
```

### Header Definitions: `include/ui_items_atlas.h`

New heart indices:
```c
#define ui_items_atlas_heart_red_full_idx 0
#define ui_items_atlas_heart_red_half_idx 1
#define ui_items_atlas_heart_red_empty_idx 2
#define ui_items_atlas_heart_soul_full_idx 3
#define ui_items_atlas_heart_soul_half_idx 4
#define ui_items_atlas_heart_black_full_idx 5
```

Legacy compatibility names (for existing code):
```c
#define ui_items_atlas_ui_heart_full_idx 0  // = heart_red_full
#define ui_items_atlas_ui_heart_half_idx 1  // = heart_red_half
#define ui_items_atlas_ui_heart_empty_idx 2  // = heart_red_empty
```

## Implementation Example

### Drawing Hearts in Game Code

```c
// Draw red hearts
for (int i = 0; i < game->player.max_health / 2; i++) {
    int heart_idx;
    int health_left = game->player.health - (i * 2);
    
    if (health_left >= 2) {
        heart_idx = ui_items_atlas_heart_red_full_idx;
    } else if (health_left == 1) {
        heart_idx = ui_items_atlas_heart_red_half_idx;
    } else {
        heart_idx = ui_items_atlas_heart_red_empty_idx;
    }
    
    C2D_DrawImageAt(
        C2D_SpriteSheetGetImage(ui_items_atlas, heart_idx),
        10 + (i * 34),  // Spacing between hearts
        10,             // Y position at top
        0.5f            // Depth
    );
}

// Draw soul hearts
for (int i = 0; i < game->player.soul_hearts / 2; i++) {
    int heart_idx = (game->player.soul_hearts % 2 && i == game->player.soul_hearts / 2)
        ? ui_items_atlas_heart_soul_half_idx
        : ui_items_atlas_heart_soul_full_idx;
    
    C2D_DrawImageAt(
        C2D_SpriteSheetGetImage(ui_items_atlas, heart_idx),
        10 + ((game->player.max_health / 2 + i) * 34),
        10,
        0.5f
    );
}
```

## Future Enhancements

### Additional Heart Types (Not Yet Implemented)
The original game includes:
- **Eternal Hearts**: White hearts that become permanent after clearing a floor
- **Bone Hearts**: Red outlined hearts that slowly recharge when empty
- **Rotten Hearts**: Green hearts that occupy a red heart container
- **Golden Hearts**: Temporary hearts that prevent damage

These can be added by:
1. Extracting sprites from the official sprite sheets
2. Adding them to `gfx_clean/resized/ui_hearts/`
3. Updating `gfx/ui_items.t3s`
4. Regenerating the atlas
5. Adding defines to `include/ui_items_atlas.h`

## Technical Notes

- **Sprite Size**: Original hearts are ~16x15 pixels, scaled 2x to 32x30 for 3DS
- **Format**: RGBA8888 for proper transparency
- **Spacing**: Hearts should be spaced ~34 pixels apart horizontally (32px width + 2px gap)
- **Z-Depth**: Draw at 0.5f depth to appear above gameplay but below text
- **Performance**: Hearts are pre-rendered in the atlas, so drawing them is very efficient

## Credits
Heart sprites extracted from *The Binding of Isaac: Rebirth* official sprite sheets.
© Edmund McMillen, Nicalis
