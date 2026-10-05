# Hearthvale

A cozy, stylized 3D crafting adventure for the **Nintendo GameCube**. It also runs on a **modded Wii**, either through the Homebrew Channel or in GameCube mode. It plays like a small offline MMO: ten skills to level, gathering and crafting in the style of FF14, cross-hotbar combat, quests, a living town, and adventurers who go about their day around you.

The Great Hearth of Emberwick has gone cold. When it went out, the guilds scattered and the dead under the old Barrow began to stir. Chop, mine, fish, forage, smith, cook, build and brew your way to rekindling it. Then deal with what wakes up.

## Playing it

Prebuilt binaries are in [`release/`](release/).

| How | What to copy | Notes |
| --- | --- | --- |
| **Wii – Homebrew Channel** (recommended) | `release/apps/hearthvale/` → `SD:/apps/hearthvale/` | Native Wii build. Supports the GameCube controller (port 1) or a Classic Controller (Wii Remote 1). Saves go to the SD card. HOME exits and saves. |
| **GameCube / Wii GameCube mode** | `release/hearthvale_gc.dol` | Boot it with Swiss, or with any DOL loader on GameCube hardware. Saves go to the memory card in slot A. |
| **Dolphin emulator** | either `.dol` | Open the file directly. |

Saving works in two ways. The game autosaves when you complete a quest or rest at the Hearth. You can also save from **System → Save Game**, and quitting with HOME/reset saves too. **System → Screen** switches between 4:3 and 16:9. On a Wii this follows the console's widescreen setting by default.

### Controls (GameCube pad)

| Button | In the world | In combat |
| --- | --- | --- |
| Control stick | Move | Move |
| C-stick | Camera | Camera |
| A | Talk / gather / use station | Weaponskill 1 (starts the combo) |
| X | – | Weaponskill 2 |
| Y | Open menu | Weaponskill 3 |
| B | Back | Dodge roll |
| Z | Re-centre camera | Target / cycle targets |
| R + A/X/Y/B | GP abilities while gathering | Job skill set 2 |
| L + A/X/Y/B | – | Potions and food |
| D-pad ↑/↓ | Camera zoom | Camera zoom |
| START | Menu | Menu |

Crafting puts every action on the face buttons. Hold L or R for more actions. As in FF14, you balance progress, quality, durability and CP.

## Building

Requirements:
- [devkitPro](https://devkitpro.org/wiki/Getting_Started) with devkitPPC and libogc (`gamecube-dev`, `wii-dev`).
- To rebuild assets: Python 3 with numpy and Pillow.

```sh
export DEVKITPRO=/opt/devkitpro DEVKITPPC=$DEVKITPRO/devkitPPC
make            # hearthvale.dol      (GameCube)
make wii        # hearthvale_wii.dol  (Wii / Homebrew Channel)
```

`data/assets.pak` is committed, so building the game needs no asset sources. To regenerate it:

```sh
sh tools/fetch_assets.sh          # KayKit packs (CC0) into third_party/
python3 tools/build_assets.py     # -> data/assets.pak
```

### PC test harness

`make -C pc` builds the game against **gxemu**, a software implementation of the GX subset the game uses. gxemu also validates every GX call. It runs headless and is used for automated play-throughs and screenshots:

```sh
HV_AUTOSTART=0 HV_SCENARIO=town ./pc/hearthvale_pc --frames 62 --shot 60 out/town.png
HV_AUTOSTART=0 HV_SCENARIO=quests ./pc/hearthvale_pc --frames 4      # main-quest logic check
HV_AUTOSTART=0 HV_SCENARIO=town HV_PROFILE=40 ./pc/hearthvale_pc --frames 42   # per-section GPU load
```

Scenarios live in `source/platform/pc/scenarios.cpp`. Pass `--wav out.wav` to record the audio output.

## How it is built for the hardware

- **Rendering:** all GX, one pass, in the cel-shaded Wind Waker style. Lighting runs in the transform unit and indexes a toon ramp through `GX_TG_SRTG`, with a rim-light TEV stage. Characters use J3D-style envelope skinning: the CPU blends a few draw matrices and the GPU does the rest.
- **Geometry:** precompiled triangle-strip display lists with 16-bit indexed vertex data. Heavy models get automatic distance LODs at two levels. Terrain is chunked with LODs, and wind-blown grass is batched per chunk. The busiest view in town submits about 148k vertices per frame, down from 509k before strips and LODs.
- **Frame pacing:** the game targets 60 fps. If a scene keeps overrunning a frame, it drops to a steady 30 fps until there is headroom again.
- **Memory:** everything ships inside the DOL (about 7 MB), well inside the GameCube's 24 MB.
- **Audio:** every sound effect and all music is synthesized at runtime, so the game ships no sample data. A mixer thread feeds double-buffered audio DMA.

See [DESIGN.md](DESIGN.md) for the game design and [CREDITS.md](CREDITS.md) for asset credits.
