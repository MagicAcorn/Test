# Design Draft — Working Title: *Hearthvale*

> Status: **draft for agreement**. Nothing here is locked in yet.

## Pitch

A single-player "offline MMO" for a modded Wii. You arrive in a frontier region
as a new adventurer and build yourself up through **lots of skills**:
gathering, crafting and combat. There are quests, towns full of NPCs, world
events and gear progression. It's inspired by FF14's job and crafting
systems, RuneScape's skill grind, and the chunky, colourful look of WoW and
RuneScape: Dragonwilds.

**This is not a survival game.** There's no hunger, thirst or base-building.
You progress through skills, quests and gear.

## Platform

| | |
|---|---|
| Target | Nintendo Wii (modded, Homebrew Channel), tested in Dolphin |
| Output | `boot.dol` in `SD:/apps/hearthvale/`, saves on SD card |
| Toolchain | devkitPPC + libogc (Wii target), C++ |
| Hardware budget | 88 MB RAM, 729 MHz CPU, fixed-function GX GPU, 640×480 |
| Controls | Wii Remote + Nunchuk (primary), GameCube controller, Classic Controller |

We'll use the Wii rather than GameCube mode because it gives us 3–4× the RAM,
SD card storage and more controller options.

## Not building everything from scratch

| Need | Use |
|---|---|
| 3D models (characters, monsters, props, nature, buildings) | CC0 low-poly packs: **KayKit** (Adventurers, Dungeon, Forest, Medieval Hexagon), **Quaternius** (RPG characters, monsters, nature, buildings), **Kenney** |
| Icons and UI | Kenney UI packs, CC0 RPG icon sets (OpenGameArt) |
| Music and SFX | CC0/CC-BY packs from OpenGameArt / Kenney audio |
| Graphics, input, audio | libogc (GX, WPAD/PAD, ASND), devkitPro portlibs (zlib, libpng) |
| Asset conversion | Offline Python tool: glTF → compact binary meshes and skeletons, PNG → GX textures |
| Game content | Data files (JSON → binary at build time) for items, recipes, skills, quests, NPCs, so adding content means editing data, not code |

These packs are low-poly with flat or gradient-texture shading. That fits the
target art style and the Wii's polygon budget. Distance fog hides short draw
distances, the same way WoW did it.

## Core systems

### Skills (RuneScape-style XP, levels 1–50 for the first release)
- **Gathering:** Woodcutting, Mining, Fishing, Herbalism
- **Crafting:** Smithing, Carpentry, Cooking, Alchemy, Leatherworking
- **Combat jobs:** Warrior (melee), Ranger (bow), Mage (staff)

### FF14 influences
- **Swap jobs by swapping gear.** Equip a pickaxe and you're a Miner. Equip a
  staff and you're a Mage. Each job has its own level and hotbar.
- **Crafting minigame.** It isn't just "click craft". Each craft has
  *Progress*, *Quality* and *Durability* bars, and you spend *CP* on actions
  such as Basic Synthesis, Careful Touch and Steady Hand. Higher levels
  unlock more actions, and high-quality results give better items.
- **Gathering minigame (light).** Each node has limited attempts, and you can
  spend points on yield or quality buffs.
- **Levequests / work orders.** Repeatable requests from town boards that
  give XP and gil.

### The "offline MMO" feel
- Simulated NPC adventurers who wander, gather, fight and chat in towns.
- World events (FATE-style), such as "Bandits are raiding the mill!"
- A simulated market board with prices that drift and NPC buy/sell listings.
- Main story quest plus side quests. Dungeons use a small AI party (tank,
  healer, you).

### Combat
Tab-target and hotbar like an MMO, adapted for a controller: target with
**Z**, abilities on **A/B/D-pad**, auto-attack, and cooldowns and a GCD like
FF14. There are no twitch-dodging requirements, but enemy attacks are
telegraphed with ground markers.

## First milestone (vertical slice)

One zone with one town, so we prove every system works before adding more:

1. A character walks around a small low-poly zone with a camera and fog
2. Woodcutting, Mining and Smithing all work, with XP and levels
3. The crafting minigame for Smithing
4. Inventory, equipment and job swap
5. One combat job, 3 enemy types and 1 quest chain
6. Save and load to SD card

After that, we add skills, zones, dungeons and the market board one by one.

## Open questions
- Working title and setting: a generic high fantasy region, or something more specific?
- Which controller do you mainly play with: Wiimote + Nunchuk or GameCube pad?
- Should character creation be simple (pick a preset model and colours) or more detailed?
