# The Binding of Isaac: O3DS Demake

A roguelike homebrew demake of *The Binding of Isaac* for Nintendo 3DS, built with devkitPRO / libctru / citro2d.

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
