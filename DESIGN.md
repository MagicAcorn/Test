# Hearthvale — Game Design

## Pitch
Hearthvale is a cozy, single-player "offline MMO" for the GameCube, which also runs on a modded Wii. You arrive in the Vale of Hearth as a new adventurer and grow through **ten skills**. Gathering feeds crafting, crafting feeds gear, and gear carries you through the story. The game takes its skill and crafting systems from FF14, its leveling grind from RuneScape, and its chunky, bright look from WoW and RuneScape: Dragonwilds. **It is not a survival game:** there is no hunger, no base upkeep and no punishing death.

## Story
The **Great Hearth** of Emberwick burned for three hundred years. When it went out, the crafting guilds scattered, and the dead beneath the **Old Barrow** began to wake. The main quest has eight chapters:

1. **The Cold Hearth** — Wren the carpenter teaches woodcutting; craft oak kindling.
2. **Bronze for the Grate** — mine copper and tin at Copperhill, smelt bronze, forge a hearth grate.
3. **A Warm Meal** — fish Mirror Lake and cook trout for the town.
4. **Remedies** — gather mint and brew potions for the watch.
5. **Slime Season** — take up sword or staff and clear the meadows.
6. **Rekindling** — lay the kindling and grate in the Hearth, which plays a cutscene. The Hearth now heals you and saves your game.
7. **Restless Dead** — push back the skeletons at the Barrow.
8. **The Barrow King** — a night-only boss with telegraphed attacks. Beating him starts the festival in town.

Five side quests add more to do, such as Tam's golden carp and Fern's moonbloom, which only appears at night.

## Skills
Levels run from 1 to 30 on a RuneScape-shaped XP curve. Tools and recipes unlock tiers as you level.

| Gathering | Crafting | Combat |
| --- | --- | --- |
| Woodcutting, Mining, Fishing, Herbalism | Smithing, Cooking, Carpentry, Alchemy | Warrior, Mage |

- **Gathering** works like FF14. Each node has an *integrity* (number of attempts), a success chance and HQ chance per item, and *GP abilities* (hold R) that raise yield or chance. Fishing is cast, wait, bite and hook, with time-of-day catches.
- **Crafting** is a forge minigame like Dragon Quest XI's Fun-Sized Forge. An item is a row of 2–6 parts, each with a gauge and a target zone. Techniques (hit, heavy hit, precise tap, sweep, inspire, cool) each cost focus and can crit. Landing parts in their zones raises the score, which decides HQ and XP. Each skill names its techniques its own way (Strike/Sear/Sand/Infuse…).
- **Combat** is action combat built for the GameCube pad. B runs a 3-hit combo with auto-aim, X rolls (or Blinks for mages), L guards, Y uses a quick item and A jumps. Holding R opens a skill wheel in slow motion: tilt the stick at a skill and release R to use it. Enemies telegraph area attacks on the ground. Warrior and Mage are separate jobs you can swap between.

## The world
The world is a 384×384 m vale with a day/night cycle (one in-game hour per real minute):
- **Emberwick** — the town hub. It has the Hearth, the forge, the tavern, the market, the notice board and NPCs.
- **Whisperwood** — oak, birch and willow, plus herbs.
- **Copperhill Quarry** — ores from copper up to gold and crystal.
- **Mirror Lake** and **the Emberrun** river — fishing.
- **Hollis Farm**, **Sunny Meadows** — slimes, flowers, herbs.
- **The Old Barrow** — skeletons, a crypt and the Barrow King (at night).

The town feels lived in. A daily **market** shifts prices and the **notice board** posts three work orders each morning. Ten simulated **adventurers** gather, fight, craft and chat around you, and **world events** give bonus XP for a skill.

## Look
The art is stylized and low-poly with cel shading: a toon ramp, rim light and a soft distance fog tinted to the sky. Sky colors follow the time of day, the sky has stars and 3D clouds, the water is depth-tinted with scrolling layers, and the grass sways in the wind. The characters, buildings and props are KayKit CC0 packs. Trees, ores, herbs, slimes, stations and item props are generated procedurally to match.

## Technical budget (GameCube)
- **Resolution:** 640×480 (or anamorphic 16:9). The target is 60 fps, with an automatic drop to a steady 30 fps if a scene overruns.
- **Vertex load:** about 150k vertices per frame in the busiest view. This comes from triangle-strip display lists, two automatic LOD levels per heavy model, frustum and fog culling, and chunked terrain.
- **Memory:** about 7 MB executable including all assets, out of 24 MB main RAM.
- **Audio:** fully synthesized at boot. The music is played by a step sequencer, so the game ships no sample data.
- **Saves:** memory card slot A on GameCube; SD card on the Wii build.
