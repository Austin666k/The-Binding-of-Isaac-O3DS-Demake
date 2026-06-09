# The Binding of Isaac 3DS — Item Reference

## Base Player Stats

| Stat | Base Value | Formula / Notes |
|------|-----------|-----------------|
| **Damage** | 1.0 | Min 0.5, applied as tear damage |
| **Speed** | 2.2 | Min 1.0, Max 4.0 (movement pixels/frame) |
| **Fire Rate** | 0.0 (modifier) | Cooldown = `12 - fire_rate × 2` frames (min 3, max 30) |
| **Range** | 120.0 px | Min 60, Max 300 (distance tear travels) |
| **Max HP** | 6 (3 hearts) | Min 2, Max 16 (8 hearts). Each heart = 2 HP |

---

## Item Pool (20 Items)

### Stat Boost Items

| # | Item | Damage | Speed | Fire Rate | Range | HP | Special | Description |
|---|------|--------|-------|-----------|-------|----|---------|-------------|
| 1 | **Pentagram** | +0.5 | — | — | — | — | — | Pure damage boost |
| 2 | **The Belt** | — | +0.3 | — | — | — | — | Pure speed boost |
| 3 | **Wire Coat** | — | — | +0.7 | — | — | — | Significantly faster tears |
| 4 | **Lunch** | — | — | — | — | +2 | — | Heals +2 HP on pickup, raises max HP |
| 5 | **Spelunker Hat** | — | — | — | +1.5 | — | — | Large range increase (+45 px effective) |
| 6 | **Blood Martyr** | +0.5 | — | — | — | — | — | Pure damage boost (same as Pentagram) |
| 7 | **Stigmata** | +0.3 | — | — | — | +2 | — | Damage + health hybrid |
| 8 | **Sad Onion** | — | — | +1.0 | — | — | — | Largest single fire rate boost |
| 9 | **Wire Hanger** | — | — | +0.5 | — | — | — | Moderate fire rate boost |
| 10 | **Growth Hormones** | +0.4 | +0.2 | — | — | — | — | Damage + speed hybrid |
| 11 | **Jesus Juice** | +0.5 | — | — | +0.5 | — | — | Damage + range hybrid (+15 px effective) |

### Multi-Stat Items

| # | Item | Damage | Speed | Fire Rate | Range | HP | Special | Description |
|---|------|--------|-------|-----------|-------|----|---------|-------------|
| 12 | **Speed Ball** | — | +0.3 | +0.5 | — | — | — | Speed + fire rate combo |
| 13 | **Magic Mushroom** | +0.5 | — | — | +1.0 | +2 | — | Damage + range (+30 px) + health. One of the best items |
| 14 | **The Halo** | +0.3 | +0.2 | +0.2 | +0.5 | +2 | — | Boosts ALL five stats. Best all-rounder |

### Power Items (with Tradeoffs)

| # | Item | Damage | Speed | Fire Rate | Range | HP | Special | Description |
|---|------|--------|-------|-----------|-------|----|---------|-------------|
| 15 | **Polyphemus** | +1.5 | — | −1.0 | — | — | — | Massive damage, much slower tears |
| 16 | **Sacred Heart** | +1.0 | −0.3 | — | — | — | 🎯 Homing | Huge damage + homing, reduced speed |

### Special Ability Items

| # | Item | Damage | Speed | Fire Rate | Range | HP | Special | Description |
|---|------|--------|-------|-----------|-------|----|---------|-------------|
| 17 | **Cupid's Arrow** | — | — | — | — | — | 🔱 Piercing | Tears pass through enemies |
| 18 | **Spoon Bender** | — | — | — | — | — | 🎯 Homing | Tears track nearest enemy |
| 19 | **Inner Eye** | — | — | −1.5 | — | — | 🔺 Triple Shot | Fires 3 tears, much slower fire rate |
| 20 | **Spirit Sword** | — | — | — | — | — | 👻 Spectral | Tears pass through walls/obstacles |

---

## Special Ability Flags

| Flag | Bit | Effect |
|------|-----|--------|
| `ITEM_FLAG_HOMING` | `1 << 0` | Tears steer toward nearest enemy (0.18 strength/frame) |
| `ITEM_FLAG_PIERCING` | `1 << 1` | Tears don't disappear on enemy hit |
| `ITEM_FLAG_SPECTRAL` | `1 << 2` | Tears pass through walls and obstacles |
| `ITEM_FLAG_TRIPLE` | `1 << 3` | Fires 3 tears (center + 2 angled at ±0.25 spread) |

Flags are **cumulative** — collecting both Cupid's Arrow and Spoon Bender gives homing + piercing tears.

---

## Stat Application Details

### How Stats Stack
All item stats are **additive**. When a player collects an item:
1. All held items' bonuses are re-summed from base stats
2. Stats are clamped to min/max bounds
3. Ability flags are OR'd together

### Fire Rate Formula
```
tear_cooldown = max(3, min(30, 12 - fire_rate_modifier × 2))
```
- Base: 12 frames between shots (~5 tears/sec at 60fps)
- With Sad Onion (+1.0): cooldown = 10 frames (~6 tears/sec)
- With Wire Coat + Sad Onion (+1.7): cooldown = 8.6 → 8 frames (~7.5 tears/sec)
- Minimum possible: 3 frames (~20 tears/sec)

### Range Formula
```
effective_range = base_range + range_bonus × 30.0
```
Range bonus is multiplied by 30 before adding to the 120px base.

### HP on Pickup
Items with HP bonus heal the player for that amount on pickup AND increase max HP permanently.

---

## Item Spawn System

- Items appear on **pedestals** in **Treasure Rooms** (one per dungeon floor)
- Each treasure room contains exactly one random item
- Items are drawn from the full pool of 20 items randomly
- Maximum 16 items can be held simultaneously
- Item sprites are rendered from `ui_items.t3x` atlas on pedestals

---

## Tear Visual Variants

Items affect tear appearance via the bullet atlas system:

| Condition | Tear Sprite | Color | Effect |
|-----------|-------------|-------|--------|
| Normal | Blue tear (7 sizes) | Blue | Standard blue tear |
| Homing | Dark blue tear | Purple tinted | Rotation follows travel direction |
| Spectral | Blue tear | Ghostly/transparent | Semi-transparent rendering |
| Piercing | Any + trail | Same + fading copies | 2 trailing ghost copies behind |
| High damage (>2.0) | Larger variant | Same | 1.2× scale multiplier |
| Triple Shot | 3× normal | Same | Center + 2 angled projectiles |

---

## Item Tier List (Subjective Power Ranking)

### S Tier — Game-Changing
| Item | Why |
|------|-----|
| **Sacred Heart** | +1.0 dmg AND homing. Best offensive item in the game |
| **Magic Mushroom** | +0.5 dmg, +30 range, +2 HP. Incredible value |
| **The Halo** | Boosts every stat including +2 HP. No downsides |

### A Tier — Excellent
| Item | Why |
|------|-----|
| **Polyphemus** | +1.5 dmg is massive, fire rate penalty is manageable |
| **Inner Eye** | Triple shot triples effective DPS despite slower fire rate |
| **Cupid's Arrow** | Piercing is extremely strong in rooms with lined-up enemies |
| **Spoon Bender** | Homing eliminates need to aim, great for mobile enemies |

### B Tier — Good
| Item | Why |
|------|-----|
| **Sad Onion** | +1.0 fire rate is very noticeable |
| **Spirit Sword** | Spectral tears bypass cover, strong in obstacle-heavy rooms |
| **Speed Ball** | Speed + fire rate combo, no downsides |
| **Jesus Juice** | Solid dmg + range combo |

### C Tier — Decent
| Item | Why |
|------|-----|
| **Pentagram** | +0.5 dmg is always welcome |
| **Blood Martyr** | Same as Pentagram |
| **Wire Coat** | Good fire rate boost |
| **Growth Hormones** | Decent dmg + speed |
| **Stigmata** | Damage + healing is nice |
| **Spelunker Hat** | Range is situationally useful |

### D Tier — Filler
| Item | Why |
|------|-----|
| **The Belt** | Speed-only, less impactful |
| **Wire Hanger** | Smaller fire rate boost |
| **Lunch** | HP-only, no offensive benefit |
