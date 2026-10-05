// Static game data: skills, items, recipes, gathering nodes, enemies, NPCs, quests.
#pragma once
#include "core/types.h"

// ------------------------------------------------------------------ skills
enum Skill : u8 {
    SK_WOODCUTTING, SK_MINING, SK_FISHING, SK_HERBALISM,
    SK_SMITHING, SK_COOKING, SK_CARPENTRY, SK_ALCHEMY,
    SK_WARRIOR, SK_MAGE,
    SK_COUNT
};
static const int MAX_LEVEL = 30;

struct SkillDef {
    const char *name;
    const char *verb;      // "Chop", "Mine"...
    u8 r, g, b;            // UI colour
    const char *icon;      // item-prop model used as icon
};
extern const SkillDef SKILLS[SK_COUNT];

u32 xpForLevel(int level);     // total xp needed to reach level
int levelForXp(u32 xp);

// ------------------------------------------------------------------- items
enum ItemCat : u8 { IC_MATERIAL, IC_TOOL, IC_WEAPON, IC_ARMOR, IC_FOOD, IC_POTION, IC_QUEST, IC_GEM };
enum ToolKind : u8 { TOOL_NONE, TOOL_AXE, TOOL_PICK, TOOL_SICKLE, TOOL_ROD, TOOL_HAMMER, TOOL_PAN, TOOL_SAW, TOOL_FLASK };

enum ItemId : u16 {
    IT_NONE = 0,
    // logs
    IT_OAK_LOG, IT_BIRCH_LOG, IT_PINE_LOG, IT_MAPLE_LOG, IT_WILLOW_LOG, IT_IRONWOOD_LOG,
    // ores
    IT_COPPER_ORE, IT_TIN_ORE, IT_COAL, IT_IRON_ORE, IT_SILVER_ORE, IT_GOLD_ORE, IT_CRYSTAL, IT_STONE,
    // gems
    IT_RUBY, IT_SAPPHIRE, IT_EMERALD,
    // fish
    IT_TROUT, IT_PERCH, IT_SALMON, IT_PIKE, IT_GOLDEN_CARP,
    // herbs
    IT_MINT, IT_SAGE, IT_LAVENDER, IT_SUNPETAL, IT_EMBERROOT, IT_MOONBLOOM,
    // intermediates
    IT_BRONZE_INGOT, IT_IRON_INGOT, IT_SILVER_INGOT, IT_GOLD_INGOT,
    IT_OAK_PLANK, IT_BIRCH_PLANK, IT_PINE_PLANK, IT_MAPLE_PLANK, IT_WILLOW_PLANK, IT_IRONWOOD_PLANK,
    IT_SLIME_GEL, IT_BONE, IT_SPIRIT_EMBER, IT_LEATHER, IT_CLOTH,
    // food
    IT_GRILLED_TROUT, IT_PERCH_STEW, IT_HERB_SALMON, IT_PIKE_PIE, IT_GOLDEN_FEAST, IT_MINT_TEA,
    // potions
    IT_POTION_MINOR, IT_SAGE_TONIC, IT_LAVENDER_ELIXIR, IT_SUNPETAL_DRAUGHT, IT_EMBER_TONIC, IT_MOON_ELIXIR,
    // tools
    IT_WORN_HATCHET, IT_WORN_PICK, IT_WORN_SICKLE, IT_WORN_ROD,
    IT_BRONZE_HATCHET, IT_BRONZE_PICK, IT_BRONZE_SICKLE, IT_OAK_ROD,
    IT_IRON_HATCHET, IT_IRON_PICK, IT_IRON_SICKLE, IT_WILLOW_ROD,
    IT_SILVER_HATCHET, IT_SILVER_PICK, IT_SILVER_SICKLE, IT_IRONWOOD_ROD,
    IT_SMITH_HAMMER, IT_CARPENTER_SAW, IT_COOK_PAN, IT_ALCHEMY_FLASK,
    // weapons
    IT_TRAINING_SWORD, IT_BRONZE_SWORD, IT_IRON_SWORD, IT_SILVER_SWORD, IT_EMBERBRAND,
    IT_OAK_STAFF, IT_BIRCH_STAFF, IT_MAPLE_STAFF, IT_IRONWOOD_STAFF, IT_HEARTHSTAFF,
    // armour (single body slot + accessory)
    IT_PADDED_VEST, IT_BRONZE_MAIL, IT_IRON_MAIL, IT_SILVER_MAIL,
    IT_COPPER_RING, IT_SILVER_RING, IT_GOLD_AMULET,
    // quest
    IT_OAK_KINDLING, IT_BRONZE_GRATE, IT_HEARTH_EMBER, IT_KINGS_CROWN, IT_LETTER,
    IT_COUNT
};

struct ItemDef {
    const char *name;
    const char *model;    // prop model for icon/drop
    u8 cat;
    u8 tool;              // ToolKind for tools
    u8 tier;              // tool/weapon/armor tier 1..5
    u8 level;             // level needed to equip/use
    u16 value;            // coins
    s16 power;            // weapon damage / armour defence / heal amount
    u8 skill;             // skill for tools (gathering skill boosted) / weapon job
    const char *desc;
};
extern const ItemDef ITEMS[IT_COUNT];

// ----------------------------------------------------------------- recipes
enum Station : u8 { ST_NONE, ST_FORGE, ST_ANVIL, ST_COOKPOT, ST_WORKBENCH, ST_ALCHEMY, ST_HEARTH, ST_NOTICEBOARD, ST_CAMPFIRE, ST_MARKET, ST_BED };

struct Ingredient {
    u16 item;
    u8 count;
};

struct Recipe {
    u16 output;
    u8 outCount;
    u8 skill;
    u8 level;
    u8 station;
    Ingredient in[3];
    u16 progress;      // difficulty
    u16 quality;       // max quality
    u8 durability;
    u16 xp;
};
extern const Recipe RECIPES[];
extern const int NUM_RECIPES;

// --------------------------------------------------------- gathering nodes
enum NodeType : u8 {
    NT_NONE = 0,
    NT_OAK = 1, NT_BIRCH, NT_PINE, NT_MAPLE, NT_WILLOW, NT_IRONWOOD,
    NT_COPPER = 10, NT_TIN, NT_COAL, NT_IRON, NT_SILVER, NT_GOLD, NT_CRYSTAL,
    NT_MINT = 20, NT_SAGE, NT_LAVENDER, NT_SUNPETAL, NT_EMBERROOT, NT_MOONBLOOM,
    NT_FISH_POND = 30, NT_FISH_RIVER, NT_FISH_LAKE,
    NT_MAX = 40
};

struct NodeLoot {
    u16 item;
    u8 level;      // level needed to see/gather
    u8 chance;     // base success % at that level
};

struct NodeDef {
    const char *name;
    const char *model;
    const char *depletedModel;
    u8 skill;
    u8 level;
    u8 tool;
    u8 integrity;
    u16 xp;
    u16 respawn;     // seconds
    f32 scale;
    NodeLoot loot[3];
    bool nightOnly;
};
const NodeDef *nodeDef(int type);

// ----------------------------------------------------------------- enemies
enum EnemyType : u8 {
    EN_NONE, EN_SLIME_GREEN, EN_SLIME_BLUE, EN_SLIME_RED, EN_SK_MINION, EN_SK_WARRIOR, EN_SK_ROGUE, EN_SK_MAGE,
    EN_BARROW_KING, EN_SLIME_GOLD, EN_SLIME_PURPLE, EN_COUNT
};

struct LootDrop {
    u16 item;
    u8 chance;   // %
    u8 count;
};

struct EnemyDef {
    const char *name;
    const char *model;
    u8 level;
    u16 hp;
    u16 damage;
    f32 attackDelay;
    f32 speed;
    f32 aggroRange;
    f32 attackRange;
    f32 scale;
    u16 xp;
    u16 coins;
    LootDrop loot[3];
    bool slime;
    bool nightOnly;
    u8 r, g, b;          // tint
};
extern const EnemyDef ENEMIES[EN_COUNT];

// -------------------------------------------------------------------- NPCs
enum NpcId : u16 {
    NPC_NONE = 0, NPC_ELDER = 1, NPC_SMITH, NPC_COOK, NPC_CARPENTER, NPC_ALCHEMIST, NPC_FISHER, NPC_CAPTAIN,
    NPC_MERCHANT, NPC_FARMER, NPC_MINER, NPC_HERBALIST, NPC_WOODSMAN, NPC_BARD, NPC_PRIEST, NPC_CHILD,
    NPC_WATCHMAN = 114,
    NPC_ADVENTURER = 50,
};

struct NpcDef {
    u16 id;
    const char *name;
    const char *title;
    const char *model;
    const char *held;      // held item model or null
    const char *idleAnim;
    const char *greeting;
    const char *smallTalk[3];
};
const NpcDef *npcDef(int id);

// ------------------------------------------------------------------ quests
enum QuestStepType : u8 { QS_TALK, QS_GATHER, QS_CRAFT, QS_KILL, QS_DELIVER, QS_USE_STATION, QS_REACH, QS_LEVEL };

struct QuestStep {
    u8 type;
    u16 target;        // npc id / item id / enemy type / station / region / skill
    u8 count;
    u16 npc;           // npc to talk to on completion (0 = auto complete)
    const char *objective;   // tracker text
    const char *lines[4];    // dialogue when this step is completed at npc (or started)
};

struct QuestDef {
    const char *title;
    u16 giver;
    u8 prereq;          // quest index that must be complete (0xFF none)
    const char *offer[4];
    QuestStep steps[6];
    u8 numSteps;
    u16 rewardCoins;
    u16 rewardItem;
    u8 rewardItemCount;
    u8 rewardSkill;
    u16 rewardXp;
    bool main;
};
extern const QuestDef QUESTS[];
extern const int NUM_QUESTS;

// adventurer names for the simulated MMO population
extern const char *const ADVENTURER_NAMES[];
extern const int NUM_ADVENTURER_NAMES;
extern const char *const CHAT_LINES[];
extern const int NUM_CHAT_LINES;
