# Binding of Isaac 3DS — Expansion & Fidelity Plan

Generated from a 20-agent parallel audit (bugs adversarially verified). Line numbers are
`source/main.c` unless noted. `main.c` is a monolith, so batches touch disjoint regions.

**Build command (the only one that works):**
```
/c/devkitPro/msys2/usr/bin/bash -lc 'cd "/c/Users/Admin/The-Binding-of-Isaac-O3DS-Demake-3DS" && export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM && make 2>&1 | tail -30'
```
(Git Bash drops the env handoff to msys2 `make` — must use devkitPro's own login shell.)

## Progress
- [x] Batch 1 — Crash/safety bugs (champ-black count+HP, sprite null-check ×8, isfinite guard) — built clean
- [x] Batch 2 — Logic bugs (creep absorb, enemy-eye NaN, Dead Cat devil, curse-room softlock) — built clean
- [x] Batch 4 (partial) — counter format `"%d"` white/0.42, obstacle drop-shadows, Rebirth fallback heart palette. TODO later: bottom-left counter stack, heart row-wrap.
- [x] Batch 5 — Room/env render (basement tint+clip, grout grid, wall bevel, door-variant reorder, boss-door sigil, radial vignette) — built clean
- [x] Batch 7 — Game-feel (hitstop, universal hit-shake, enemy hit-squash, blue tear-splash, player walk-bob) — built clean
- [x] Batch 8 — Active-item system (active_item slot + charge bar + KEY_TOUCH use; Yum/Belial routed through it; new The Poop active) — built clean
- [x] Batch 9 (core) — 10 new passive items; 3 new pills; 4 new tarot; golden KEY5 + BATTERY pickups — built clean
- [x] Batch 10 (core) — 3 new enemies (Trite/Fatty/Charger), 2 new bosses (Loki/Gish incl. 2nd Sheol boss) — built clean
- [x] REVIEW pass 1 — found + fixed 3 crash bugs (pill/tarot name tables not grown with enums); rebuilt clean
- [x] Batch 6 — Minimap/menu fidelity (square outlined cells, special-room icons/colors, undiscovered outlines, top-screen overlay) — built clean
- [x] Batch 10 (rooms) — Angel Room + Sacrifice Room (reuse Devil/Curse plumbing; also fixed a pre-existing ROOM_DEVIL render gap) — built clean
- [x] Batch 10 (chars) — 3 new characters: Eve, Samson, Blue Baby "???" (stats + starts + select screen + unlock chain) — built clean
- [x] REVIEW pass 2 — zero issues (parallel-array warning worked) — rebuilt clean
- [x] Round 3 — 8th chapter "The Chest" (MAX_FLOORS 7→8), item synergies (8 pairs), 3 deeper curses (Unknown/Maze/Labyrinth), Boss Rush mode — built clean
- [x] REVIEW pass 3 — found + fixed Boss Rush self-clear (dead feature) + wave-state reset; rebuilt clean
- [x] Round 4 — luck-scaled drops + diversified room-clear rewards, black market / depth-scaled secret rooms, 2 enemies (Keeper/Sucker) + 3 bosses (Steven/Chub/Fistula), HUD polish (bottom-left counters, heart row-wrap, pickup banner) — built clean
- [x] REVIEW pass 4 — found + fixed Unlocks screen boss roster (stale 11-entry table → 16, buffers resized); rebuilt clean
- [x] Round 5 — pixel font (Consolas .bcfnt, graceful fallback), 10 items + 4 trinkets, 2 enemies + boss Scolex, Arcade + Library rooms — built clean
- [x] REVIEW pass 5 — font VRAM risk (subset .bcfnt 2488→95 glyphs, 1MB→512KB) + font leak (C2D_FontFree) fixed; rebuilt clean
- [x] Batch 3 — Pixel font DONE (ASCII-subset .bcfnt in romfs; delete romfs/gamefont.bcfnt to revert to default font, zero code change)
- [x] Round 6 (2026-07-08) — fresh 6-agent audit → 6 serial phases, all built clean:
  - A: 17 bug fixes (unlocks-screen stack-overflow CRASH, dead challenge system, secret-room warp softlock, Death-card no-kill, burrowed-boss invisible contact, pill color/name desync, 14 pentagram-sprite items, bomb POOL overhaul w/ self-damage + troll bombs, door-flee exploit, boss-rush scaling, devil free-deal-at-cap)
  - B: 14 mechanics-parity (FLOAT enemy HP — damage-ups were placebo, live shot-speed stat, player momentum → tears, zero tear spread, full-heart contact scaling, Maw/Red-Maw + Fly/Attack-Fly AI archetypes corrected, Pooter/Host/Leaper patterns, door-spawn safety + entry iframes, devil chance 15/50, loot economy cut to Rebirth-ish, hit-stun removed, fire-rate curve, tear arc landing + dmg-scaled knockback)
  - C: 12 visual-fidelity (creep now VISIBLE, procedural enemy anims, char tints in play, boss death at corpse, ui_boss_healthbar sprite, parchment floor transition, devil/angel door identity, coin spin + heart pulse, hurt-tint gate, pedestal sparkles, Chub/Scolex segments now render, tear spin removed)
  - D: 7 iconic weapons real (Brimstone charge-beam, Technology, Mom's Knife throw/return, Dr./Epic Fetus bomb-tears, Ipecac, Pyromaniac heal, Spoon-Bender beam-bend); precedence KNIFE>BRIM>TECH>FETUS>tears
  - E: story arc (Mom fixed Depths, Mom's Heart Womb + ENDING-1 light beam choice, 3-phase Satan Sheol w/ enemy brimstone, Mega Satan Chest), black hearts, familiar system (Bobby/Ghost/Demon Baby), 4 actives (Necronomicon/Teleport!/Deck/Bible w/ Satan insta-death), 20/20+3 passives, challenges 4-6 (Speed!/Cat Got Your Tongue/Purist); bosses 20, items 67
  - F: 4-reviewer adversarial gauntlet → 19 more fixes (double-kill_enemy state corruption, boom-fly walking-dead, boss-rush spawn-on-player, orphaned Loki/Fistula/Scolex, curse-door absorb chain, C2D_Init 16384, fire-rate base 30→22, Womb-spike tuning, Purist shop gate, ch5 familiar dmg)
  - Net +1,829 lines (main.c 13,656). NOT hardware-tested this round (font/VRAM cleared by user playtest of R5; R6 visuals/feel need eyes-on).
- [x] Round 7 (2026-07-08) — user playtest cleared R6 ("ran fine"); full Isaac-Rebirth UI overhaul + fresh bug pass:
  - UI (15 surfaces, procedural paper aesthetic — spec in session scratchpad ROUND7_UI_SPEC.md): shared palette (PAPER/INK/BLOOD/GOLD) + draw_paper_panel/draw_selector/draw_stat_pips/draw_menu_title helpers; menu background+rows de-neon'd to paper strips w/ blood-scratch selection; HUD → Rebirth layout (no black strip, top-left counter column w/ zero-padded bone-white text, browns/gold active-item box, parchment minimap, demoted floor text); bottom map page full paper + stat pips; pause = paper card w/ run stats; char select = paper cards w/ heart rows + "?" locked; win = spotlight ending screens (both endings); settings/controls/unlocks papered (+ scroll track); floor-transition/gameover routed through the panel helper; pickup/curse banners, boss-splash fallback, shop coin-icon prices (gray-not-red unaffordable); audio debug overlay gated behind SELECT
  - Bugs (7 audit + 1 gauntlet-found): black-heart burst detonating in the WRONG room after door/warp tolls, Boss Rush waves silently spawning 0 bosses (pre-reset clamp), boss music/state ending mid-wave + NEW Fistula last-ball stuck-music regression fixed, redundant blocking config_save per boss kill (SD hitch), sweep-vs-death-spawn slot race, starved boss minion spawners (append-only → slot reuse), Holy-Mantle curse-door lockout
  - Review-pass layout fixes: active-item box vs 3rd heart row (shared hudShift), ability dots vs Time-Attack countdown, challenge-row overlap, spawn-grace AoE window narrowed to same-frame, dead particle fields removed
  - Gauntlet: 2 auditors + 2 reviewers + fix pass; build clean (zero warnings) at every phase. CIA rebuilt. NOT hardware-tested: the entire new UI needs eyes-on.

## Font hardware note
The .bcfnt (~512KB, single glyph sheet) loads into the shared 6MB Old-3DS VRAM alongside sprite atlases.
If sprites go black on Old-3DS hardware (VRAM exhaustion), DELETE `romfs/gamefont.bcfnt` and rebuild —
`g_font` stays NULL and all text falls back to the default font automatically. Needs a hardware playtest to confirm.
- [ ] **On-hardware playtest** (only the user can do this) — validate feel/visuals

## Verified state (2026-07-03)
Three implement→adversarial-review→fix rounds complete. Build: CLEAN (zero warnings). Net **~+2500 lines** this session.
Nothing committed yet (working tree). Could NOT playtest on 3DS hardware this session — visual/feel changes are
code-correct + build-verified + thrice-reviewed but not eyes-on.

## Bugs (verified)
1. **4776** CHAMP_BLACK split: unconditional `enemy_count++` → `if (ei >= r->enemy_count) r->enemy_count = ei+1;`; fix split HP at 4771 to `(e->max_hp>1)?e->max_hp/2:1`.
2. **sprite.c** 8 helpers deref `img.subtex` w/o null check → add `if (!img.subtex) return;` after each `C2D_SpriteSheetGetImage`.
3. **9517** creep dmg bypasses soul hearts → route via `player_absorb_dmg`/`player_check_death`; drop outer holy-mantle if.
4. **974–985** curse-room fallback ignores critical path → delete fallback block.
5. **548 vs 2090** Dead Cat floors max_hp=2 → devil deals unbuyable → pay a life per item when Dead Cat held.
6. **4589** ENEMY_EYE divides by fm==0 → NaN tear never expires → guard `if (fm>0.5f)`, reset timer outside; add `isfinite` guard in spawn_enemy_shot (2806).

Full detail incl. 31 fidelity + 17 expansion items: `scratchpad/synthesis.md` (and confirmed/allFid/allExp JSON).
