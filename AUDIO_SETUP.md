# Audio Setup - Binding of Isaac 3DS

## Zero Setup Required

**Audio just works.** Install the CIA, launch the game, and audio plays.

The game uses the 3DS **CSND** audio system, which talks to the sound
hardware directly without needing the DSP firmware (`dspfirm.cdc`).
No DSP1 homebrew, no firmware dumping, no SD card setup - nothing.

## Why CSND, Not NDSP?

The "modern" NDSP audio system requires the DSP firmware to be present
and valid.  On many 3DS systems this firmware is missing or corrupt, and
`ndspInit()` crashes (Data Abort) before any error code can be returned.
No amount of pre-checking can prevent it - the crash happens inside the
init call itself.

CSND uses the legacy `csnd:SND` service.  It accesses the sound hardware
directly, doesn't load any firmware, and is supported on every 3DS console
ever made.  Many homebrew games (including Subway Surfers) use CSND
successfully.

## Channel Allocation

- Channel 8: Music (looping, one track at a time)
- Channels 9-15: Sound effects (round-robin, 7 channels)

## Audio Files

All audio is bundled inside the CIA - nothing to install separately:
- `romfs:/music/mus_*.wav` - 5 music tracks (16-bit PCM)
- `romfs:/sfx/sfx_*.wav` - 13 sound effects (16-bit PCM)

## Settings

You can change audio behavior in-game (main menu -> Settings):
- **Audio Enabled** - turn audio on/off completely
- **SFX Volume** - 0-100%
- **Music Volume** - 0-100%

Settings are saved to `sdmc:/3ds/binding_of_isaac/config.ini`.

## Troubleshooting

If the Settings screen shows audio status other than "Working":

| Status | Meaning | Fix |
|--------|---------|-----|
| Working | Audio is active | No fix needed |
| Disabled (user config) | You turned audio off | Settings -> Audio Enabled: ON |
| CSND init failed | csnd:SND service unavailable (rare) | Reboot 3DS, ensure Luma3DS is up to date |
| Disabled (error) | Runtime error | Restart the game |
