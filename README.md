# The Binding of Isaac: O3DS Demake

A roguelike homebrew demake of *The Binding of Isaac* for Nintendo 3DS, built with devkitPRO / libctru / citro2d.

## What's new in v2.5 — build-depth update

The roadmap's top pick: the systems that make Isaac runs feel different every
time. The active-item system was hardcoded for exactly two items; it's now a
real, extensible slot, which unlocked the rest.

- **Active items are a real slot now.** Picking one up puts it in a dedicated
  L-button slot (swapping the old one), charged by clearing rooms. Five new
  actives join Yum Heart and Book of Belial: **Necronomicon** (damage every
  enemy), **The Bible** (full heal), **Mom's Bra** (wipe all enemy shots),
  **Forget Me Now** (re-roll the whole floor) and **The D6** (re-roll the
  room's item pedestals). **The Battery** doubles your charge so you can bank
  two uses.
- **Trinkets** — a single held passive pickup with a global effect: Cancer
  (+tears), Curved Horn (+damage), Rabbit's Foot (+luck), Swallowed Penny
  (coin per room), Match Stick (bombs), Petrified Poop (better drops) and
  AAA Battery (faster active charging). Walk over a new one to swap.
- **Familiars** — orbiting helpers granted by passive items that auto-fire at
  the nearest enemy: **Brother Bobby**, **Sister Maggy** (heavy shots),
  **Little Steven** (homing) and **Demon Baby** (rapid fire). Stack several
  and they all circle you and shoot.
- **Save format v3** — the run save grew reserved headroom and a
  version-tolerant, size-validated loader, so adding future fields no longer
  wipes an in-progress run, and corrupt/old saves are rejected cleanly instead
  of loading a broken player.

This release was reviewed by a multi-agent adversarial pass; the fixes it
surfaced are included (active-charge no longer refundable via a mid-effect
save, room-wide damage no longer one-shots the splits it spawns, familiar
shots always reach their locked target, trinket swaps can't thrash in corners,
and the save loader now validates struct size and rejects truncated files).

## What's new in v2.4 — graphics-fix release

A dedicated audit of every render path (multi-agent review of draw order,
color formats, sprite math, text layout, and state handling):

- **Creep puddles were invisible** — yellow-champion trails and Widow's
  landing puddles damaged the player but had no draw call at all. They now
  render as fading red puddles under entities.
- **The Curse of Darkness vignette was inverted** — it tried to "punch out"
  a visible circle by stacking translucent *black* circles over a dark
  overlay, which only adds darkness; the halo around Isaac was actually the
  darkest spot on screen. Rebuilt with darkness drawn only outside the
  visible zone plus gradient falloff quads.
- **Hoppers and Leapers jumped downward** — the render applied their jump
  arc with an inverted sign, sinking them into the floor mid-hop.
- **Enemy sprites rendered up to 20% off their hitbox size** — boss and
  enemy draw scales assumed uniform 80px/20px source art, but the atlas
  mixes 16-96px sprites (Gemini drew smaller than its hitbox, Monstro
  larger). Scales now derive from the actual atlas subtexture size.
- **Pausing changed the scene** — the paused frame dropped the Womb/Sheol
  tint, shop price tags, and the darkness vignette (letting you scout
  cursed rooms for free), and floor pickups kept bobbing while everything
  else froze. The world scene is now rendered by one shared path for both
  states, and pickup bobbing derives from the (frozen-while-paused) frame
  counter.
- **Boss intro drew two overlapping titles** — a leftover "! BOSS !" overlay
  printed over (and dimmed) the proper boss-name reveal. Removed.
- **Tarot card pickups had a sky-blue "gold" border** — a raw hex color
  written in the wrong channel order (ARGB vs citro2d's ABGR).
- Pedestal/devil-deal labels now shake with the world instead of floating
  detached during screen shake; boss landing shadows draw under (not over)
  the body; the Gemini companion flashes when *it* is hit instead of
  mirroring the main body; the pacer no longer snaps to face right during
  hit-flash while walking left; floor hearts no longer render bigger than
  Isaac; the no-sprites fallback now shows hidden Hosts, collapsed Globins
  and jump arcs; floor-transition titles are properly centered; and using a
  card shows the blue CARD banner with a smooth fade like every other
  pickup message.

## What's new in v2.3 — bug-fix release

A full-codebase bug hunt. Everything below was found by review and fixed:

- **Gemini's companion (Suture) was unkillable** — it had an HP pool that
  nothing ever decremented, so it stayed a permanent contact hazard for the
  whole fight. It now takes tear damage and dies properly.
- **Invulnerable enemies could still hurt or be hurt** — burrowed Pin dealt
  contact damage from underground; jumping Monstro/Widow hit you from their
  ground hitbox while visually airborne; bombs damaged hidden Hosts and
  burrowed Pin even though tears respect those states. All consistent now.
- **Live bombs followed you between rooms** — a ticking bomb would explode at
  stale coordinates in the next room (and could even falsely reveal that
  room's secret door). Bombs now clear on every door transition and teleport.
- **Combat rooms could be escaped mid-fight** through treasure/curse doors;
  all doors now seal until the room is cleared.
- **Music volume was broken** — every crossfade reset the volume to a
  hardcoded 60%, and a configured volume below 5% was forcibly raised, making
  "music off" impossible. The user's setting is now respected everywhere.
- **The bottom-screen minimap was dead code** — an early `return` made it
  unreachable. The action panel now embeds a live compact minimap (honoring
  Curse of the Lost and Darkness Falls), next to a tightened stats box.
- **Burrowed Pin looked fully present** — it now renders as a travelling dirt
  mound with a faint silhouette; **Widow's jump had physics but no visuals** —
  she now arcs into the air with a landing shadow.
- **Pause-screen stats lied** — "Tears" showed the raw internal fire-rate
  modifier (e.g. 0.00) instead of the computed stat shown elsewhere.
- **No feedback when swallowing an unidentified pill** — the revealed effect
  name now appears in the pickup banner (cards announce their name too).
- Bombs no longer shove bosses 20px (stationary bosses barely budge), homing
  tears no longer chase untargetable enemies, pickups cap at 99, and stale
  menu help text ("CONTINUE stays disabled...") was corrected.

## What's new in v2.2

- **Devil deals** — after a boss kill there's a 40% chance a second, dark
  pedestal appears holding one of the game's strongest items (Brimstone,
  Mom's Knife, Polyphemus...). The price: one heart container, permanently.
  Refused if it would leave you below one full heart.
- **New obstacles** — rooms now mix three obstacle types: rocks (block
  everything), **poop** (destructible with 3 tears or a bomb, ~30% chance to
  drop a pickup) and **spikes** (don't block movement or shots, but cost half
  a heart to walk over; enemies stroll right across them).
- **4 new tarot cards** — *Strength* (+1 damage for the room), *Death*
  (40 damage to every enemy in the room), *The Stars* (teleport to the
  treasure room), *The Sun* (full heal + map reveal + room-wide damage).
- **5 new pills** (15 total) — *Explosive Diarrhea* (+2 bombs), *Payday*
  (+5 coins), *Lock Picker* (+2 keys), *Amnesia* (forgets the map) and
  *Hematemesis* (drop to 1 heart, vomit up 2 full hearts).
- **Pill colors fixed & extended** — the announced pill color now actually
  matches the capsule's rendered color (they disagreed before, which broke
  identification), with 5 new colors for the new effects.
- **Erase Save Data** — new settings entry (arm + confirm) that wipes the
  run save and unlock progression while keeping your audio preferences.

## What's new in v2.1

- **Run save & Continue** — the run is checkpointed to SD at the start of every
  floor (`sdmc:/3ds/binding_of_isaac/run.sav`) and on Save & Quit / app exit.
  The main menu's **Continue** option resumes the run at the start of its floor
  with all items, stats, consumables and score intact (the floor layout is
  re-rolled, roguelike-style). The save is deleted on death or victory.
- **Pause menu** — START now opens a real pause menu: Resume / Restart Run /
  Save & Quit, with the stats overlay still on the bottom screen.
- **Quick restart** — press A on the game-over or victory screen to instantly
  start a new run with the same character, mode and difficulty.
- **Faster builds on O3DS** — compiled with `-ffast-math -fomit-frame-pointer`
  (all vector normalizations are guarded, so this is safe) and a larger text
  glyph buffer so text-heavy screens never drop glyphs.
- **Bug fix** — multi-shot volleys (e.g. The Inner Eye) could kill the same
  enemy twice in one frame, duplicating death drops, splits and score.

## Features

### Start Menu
- Professional menu with **New Game**, **Controls**, and **Quit** options
- Navigate with D-pad or Circle Pad, select with A button
- Decorative border and styled text

### Roguelike Dungeon System
- **Procedurally generated** 5×5 grid dungeon with 7-10 interconnected rooms per floor
- **Room types**:
  - **Start Room** (green on minimap) — Safe starting area, no enemies
  - **Normal Rooms** (gray) — 2-5 enemies with random obstacles
  - **Treasure Room** (gold) — Clears to fully heal the player (+50 score bonus)
  - **Boss Room** (red) — Contains a powerful boss enemy with HP bar
- **Doors** on each wall connect to adjacent rooms
- Doors are **locked** (dark) until all enemies in the room are defeated
- Doors **open** (bright gold with frame) when room is cleared

### Room Generation
- Random walk algorithm ensures all rooms are connected
- Boss room placed at maximum distance from start
- Treasure room placed adjacent to start room when possible
- Obstacles (rocks) randomly placed in combat rooms
- Connectivity verified via flood-fill BFS

### Combat
- 3 regular enemy types: **Fly** (random), **Gaper** (chasing), **Pacer** (horizontal)
- **Boss** enemy with 20 HP, charge attacks, wall bouncing, and visible HP bar
- Tear shooting in 4 directions (ABXY buttons)
- Obstacle collision for player and tears

### Minimap (Bottom Screen)
- Shows all explored rooms color-coded by type
- Current room highlighted in white
- Green dots indicate cleared rooms
- Door connections drawn between rooms
- Legend and exploration counter
- Controls reminder

### Win Condition
- Defeat the boss in the Boss Room to escape the basement!

## Controls

| Input | Action |
|-------|--------|
| D-Pad / Circle Pad | Move Isaac / Navigate menu |
| A | Select (menu) / Shoot Right (game) |
| B | Shoot Down |
| X | Shoot Up |
| Y | Shoot Left |
| START | Start game / Return to menu |
| SELECT | Quit |

## Building

### Prerequisites
- [devkitPRO](https://devkitpro.org/) with devkitARM
- libctru, citro3d, citro2d

### Compile
```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=$DEVKITPRO/devkitARM
export PATH=$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH
make
```

### Run (.3dsx)
- **3DS**: Copy `binding_of_isaac_3ds.3dsx` to your SD card's `/3ds/` folder
- **Citra Emulator**: File → Load File → select `.3dsx`

### Build installable CIA (Windows)
```powershell
cd C:\Users\Admin\The-Binding-of-Isaac-O3DS-Demake
.\build_cia.ps1
```

Install `binding_of_isaac_3ds.cia` on CFW 3DS with FBI.

### Push to GitHub
```powershell
& "C:\Program Files\Git\cmd\git.exe" -C C:\Users\Admin\The-Binding-of-Isaac-O3DS-Demake push -u origin main
```
Git Credential Manager will prompt you to sign in to GitHub the first time.

## Project Structure

```
binding_of_isaac_3ds/
├── Makefile              # Build configuration
├── README.md             # This file
├── include/
│   └── game.h            # Types, constants, function declarations
├── source/
│   └── main.c            # All game logic, rendering, dungeon generation
└── build/                # Build artifacts (generated)
```

## Game Design

- **Dungeon**: 5×5 grid, 7-10 rooms per floor, randomly generated each run
- **Health**: 3 hearts (6 HP), invincibility frames on hit
- **Scoring**: 10 points per enemy, 100 for boss, 50 bonus for treasure room
- **Difficulty**: Rooms have 2-5 enemies with random types and obstacles
