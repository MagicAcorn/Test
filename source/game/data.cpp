#include "game/data.h"
#include <math.h>

// ---------------------------------------------------------------- skills
const SkillDef SKILLS[SK_COUNT] = {
    {"Woodcutting", "Chop", 120, 190, 80, "ip/log_oak"},
    {"Mining", "Mine", 200, 140, 90, "ip/ore_copper"},
    {"Fishing", "Fish", 90, 170, 230, "ip/fish_trout"},
    {"Herbalism", "Harvest", 150, 220, 110, "ip/herb_mint"},
    {"Smithing", "Smith", 230, 120, 70, "ip/ingot_bronze"},
    {"Cooking", "Cook", 240, 180, 90, "ip/fish_cooked"},
    {"Carpentry", "Build", 210, 160, 110, "ip/plank"},
    {"Alchemy", "Brew", 180, 120, 240, "ip/potion_purple"},
    {"Warrior", "Fight", 230, 80, 70, "itm/sword"},
    {"Mage", "Cast", 110, 140, 250, "itm/staff"},
};

// RuneScape-style curve, gentled for a shorter game: level 30 ~ 5.3k xp
// RuneScape-shaped curve, cached: skillLevel() runs every frame for the HUD
static u32 s_xpTable[MAX_LEVEL + 2];

u32 xpForLevel(int level) {
    if (level <= 1) return 0;
    if (level > MAX_LEVEL + 1) level = MAX_LEVEL + 1;
    if (!s_xpTable[2]) {
        f32 pts = 0;
        for (int l = 1; l <= MAX_LEVEL; l++) {
            pts += (f32)l + 300.0f * powf(2.0f, l / 7.0f);
            s_xpTable[l + 1] = (u32)(pts / 5.0f);
        }
    }
    return s_xpTable[level];
}

int levelForXp(u32 xp) {
    int l = 1;
    while (l < MAX_LEVEL && xp >= xpForLevel(l + 1)) l++;
    return l;
}

// ----------------------------------------------------------------- items
#define MAT(n, m, v, d) {n, m, IC_MATERIAL, TOOL_NONE, 0, 1, v, 0, 0, d}
#define GEM(n, m, v, d) {n, m, IC_GEM, TOOL_NONE, 0, 1, v, 0, 0, d}
#define FOOD(n, m, lv, v, heal, d) {n, m, IC_FOOD, TOOL_NONE, 0, lv, v, heal, 0, d}
#define POT(n, m, lv, v, heal, d) {n, m, IC_POTION, TOOL_NONE, 0, lv, v, heal, 0, d}
#define TOOL(n, m, kind, tier, lv, v, sk, d) {n, m, IC_TOOL, kind, tier, lv, v, (s16)(tier * 10), sk, d}
#define WPN(n, m, tier, lv, v, pw, sk, d) {n, m, IC_WEAPON, TOOL_NONE, tier, lv, v, pw, sk, d}
#define ARM(n, m, tier, lv, v, def, d) {n, m, IC_ARMOR, TOOL_NONE, tier, lv, v, def, 0, d}
#define QST(n, m, d) {n, m, IC_QUEST, TOOL_NONE, 0, 1, 0, 0, 0, d}

const ItemDef ITEMS[IT_COUNT] = {
    {"", "", 0, 0, 0, 0, 0, 0, 0, ""},
    MAT("Oak Log", "ip/log_oak", 4, "Sturdy timber from the vale's oaks."),
    MAT("Birch Log", "ip/log_birch", 8, "Pale, light wood with papery bark."),
    MAT("Pine Log", "ip/log_dark", 14, "Resinous pine from Whisperwood."),
    MAT("Maple Log", "ip/log_autumn", 20, "Warm-grained maple, prized by carpenters."),
    MAT("Willow Log", "ip/log_willow", 28, "Supple willow from the riverbanks."),
    MAT("Ironwood Log", "ip/log_dark", 40, "Dense wood that rings like metal."),
    MAT("Copper Ore", "ip/ore_copper", 4, "Ore with a ruddy sheen."),
    MAT("Tin Ore", "ip/ore_tin", 4, "Soft grey ore. Pairs with copper."),
    MAT("Coal", "ip/ore_coal", 6, "Fuel for the forge."),
    MAT("Iron Ore", "ip/ore_iron", 12, "Heavy red-brown ore."),
    MAT("Silver Ore", "ip/ore_silver", 22, "Gleaming ore from deep veins."),
    MAT("Gold Ore", "ip/ore_gold", 35, "Rare and heavy. The merchants love it."),
    MAT("Aether Crystal", "ip/ore_crystal", 45, "Hums faintly with old magic."),
    MAT("Stone", "ip/ore_stone", 1, "Plain stone. Every mason needs some."),
    GEM("Ruby", "ip/gem_ruby", 60, "A fiery red gem."),
    GEM("Sapphire", "ip/gem_sapphire", 60, "A cool blue gem."),
    GEM("Emerald", "ip/gem_emerald", 70, "A deep green gem."),
    MAT("Trout", "ip/fish_trout", 5, "A speckled pond trout."),
    MAT("Perch", "ip/fish_perch", 9, "A striped river perch."),
    MAT("Salmon", "ip/fish_salmon", 16, "A strong river salmon."),
    MAT("Pike", "ip/fish_pike", 22, "A toothy lake pike."),
    MAT("Golden Carp", "ip/fish_golden", 60, "Legend says it brings good fortune."),
    MAT("Mint", "ip/herb_mint", 3, "Fresh and fragrant."),
    MAT("Sage", "ip/herb_sage", 6, "A soothing woodland herb."),
    MAT("Lavender", "ip/herb_lavender", 10, "Calming purple blossoms."),
    MAT("Sunpetal", "ip/herb_sunpetal", 16, "Petals that hold the warmth of noon."),
    MAT("Emberroot", "ip/herb_emberroot", 22, "A root that is warm to the touch."),
    MAT("Moonbloom", "ip/herb_moonbloom", 34, "Only blooms under moonlight."),
    MAT("Bronze Ingot", "ip/ingot_bronze", 12, "Copper and tin, fused in the forge."),
    MAT("Iron Ingot", "ip/ingot_iron", 30, "Strong, dependable iron."),
    MAT("Silver Ingot", "ip/ingot_silver", 55, "Bright silver, good against the restless dead."),
    MAT("Gold Ingot", "ip/ingot_gold", 90, "Pure gold."),
    MAT("Oak Plank", "ip/plank", 9, "Smoothly planed oak."),
    MAT("Birch Plank", "ip/plank", 17, "Light birch boards."),
    MAT("Pine Plank", "ip/plank", 29, "Fragrant pine boards."),
    MAT("Maple Plank", "ip/plank", 42, "Beautiful maple boards."),
    MAT("Willow Plank", "ip/plank", 58, "Flexible willow boards."),
    MAT("Ironwood Plank", "ip/plank", 85, "Boards hard as iron."),
    MAT("Slime Gel", "ip/potion_green", 6, "Wobbly, sticky, oddly useful."),
    MAT("Old Bone", "hw/bone_a", 8, "Rattles even after it stops moving."),
    MAT("Spirit Ember", "ip/ember", 120, "A spark of the Hearth's lost flame."),
    MAT("Leather", "ip/leather", 14, "Tanned hide."),
    MAT("Cloth", "ip/cloth", 10, "Woven linen."),
    FOOD("Grilled Trout", "ip/fish_cooked", 1, 14, 140, "Restores 140 HP."),
    FOOD("Mint Tea", "itm/mug", 1, 10, 90, "Restores 90 HP and refreshes GP."),
    FOOD("Perch Stew", "dn/plate_food_a", 5, 26, 240, "Restores 240 HP."),
    FOOD("Herb-crusted Salmon", "ip/fish_cooked", 12, 44, 400, "Restores 400 HP."),
    FOOD("Pike Pie", "dn/plate_food_b", 18, 66, 560, "Restores 560 HP."),
    FOOD("Golden Feast", "dn/plate_food_b", 24, 160, 900, "Restores 900 HP. Fit for a festival."),
    POT("Minor Healing Potion", "ip/potion_red", 1, 18, 160, "Restores 160 HP."),
    POT("Sage Tonic", "ip/potion_green", 5, 30, 150, "Restores 150 MP/GP/CP."),
    POT("Lavender Elixir", "ip/potion_purple", 10, 50, 380, "Restores 380 HP."),
    POT("Sunpetal Draught", "ip/potion_gold", 16, 70, 20, "+20% damage for 60 seconds."),
    POT("Ember Tonic", "ip/potion_red", 20, 90, 600, "Restores 600 HP."),
    POT("Moonbloom Elixir", "ip/potion_blue", 26, 160, 1200, "Fully restores HP and MP."),
    TOOL("Worn Hatchet", "itm/axe", TOOL_AXE, 1, 1, 5, SK_WOODCUTTING, "A chipped but trusty hatchet."),
    TOOL("Worn Pickaxe", "itm/pickaxe", TOOL_PICK, 1, 1, 5, SK_MINING, "Old, but it still bites rock."),
    TOOL("Worn Sickle", "itm/sickle", TOOL_SICKLE, 1, 1, 5, SK_HERBALISM, "For gathering herbs."),
    TOOL("Worn Fishing Rod", "itm/rod", TOOL_ROD, 1, 1, 5, SK_FISHING, "Line, hook and hope."),
    TOOL("Bronze Hatchet", "itm/axe", TOOL_AXE, 2, 3, 40, SK_WOODCUTTING, "Gathering +12%."),
    TOOL("Bronze Pickaxe", "itm/pickaxe", TOOL_PICK, 2, 3, 40, SK_MINING, "Gathering +12%."),
    TOOL("Bronze Sickle", "itm/sickle", TOOL_SICKLE, 2, 3, 30, SK_HERBALISM, "Gathering +12%."),
    TOOL("Oak Fishing Rod", "itm/rod", TOOL_ROD, 2, 3, 40, SK_FISHING, "Bites come quicker."),
    TOOL("Iron Hatchet", "itm/axe", TOOL_AXE, 3, 13, 110, SK_WOODCUTTING, "Gathering +24%."),
    TOOL("Iron Pickaxe", "itm/pickaxe", TOOL_PICK, 3, 13, 110, SK_MINING, "Gathering +24%."),
    TOOL("Iron Sickle", "itm/sickle", TOOL_SICKLE, 3, 13, 90, SK_HERBALISM, "Gathering +24%."),
    TOOL("Willow Fishing Rod", "itm/rod", TOOL_ROD, 3, 18, 140, SK_FISHING, "Rare fish bite more often."),
    TOOL("Silver Hatchet", "itm/axe", TOOL_AXE, 4, 20, 260, SK_WOODCUTTING, "Gathering +36%."),
    TOOL("Silver Pickaxe", "itm/pickaxe", TOOL_PICK, 4, 20, 260, SK_MINING, "Gathering +36%."),
    TOOL("Silver Sickle", "itm/sickle", TOOL_SICKLE, 4, 20, 220, SK_HERBALISM, "Gathering +36%."),
    TOOL("Ironwood Fishing Rod", "itm/rod", TOOL_ROD, 4, 26, 300, SK_FISHING, "The best rod in the vale."),
    TOOL("Smithing Hammer", "itm/hammer", TOOL_HAMMER, 1, 1, 20, SK_SMITHING, "Craftsmanship for smithing."),
    TOOL("Carpenter's Saw", "itm/knife", TOOL_SAW, 1, 1, 20, SK_CARPENTRY, "Craftsmanship for carpentry."),
    TOOL("Skillet", "itm/hammer", TOOL_PAN, 1, 1, 20, SK_COOKING, "Craftsmanship for cooking."),
    TOOL("Alembic", "ip/potion_blue", TOOL_FLASK, 1, 1, 20, SK_ALCHEMY, "Craftsmanship for alchemy."),
    WPN("Training Sword", "itm/sword", 1, 1, 10, 14, SK_WARRIOR, "A blunt practice blade."),
    WPN("Bronze Sword", "itm/sword", 2, 5, 60, 24, SK_WARRIOR, "Damage 24."),
    WPN("Iron Sword", "itm/sword", 3, 14, 160, 40, SK_WARRIOR, "Damage 40."),
    WPN("Silver Sword", "itm/sword", 4, 22, 320, 58, SK_WARRIOR, "Damage 58. Bane of the restless."),
    WPN("Emberbrand", "itm/greatsword", 5, 28, 900, 80, SK_WARRIOR, "Damage 80. Warm as the Hearth itself."),
    WPN("Oak Staff", "itm/staff", 1, 1, 12, 14, SK_MAGE, "Magic 14."),
    WPN("Birch Staff", "itm/staff", 2, 6, 70, 25, SK_MAGE, "Magic 25."),
    WPN("Maple Staff", "itm/staff", 3, 15, 170, 41, SK_MAGE, "Magic 41."),
    WPN("Ironwood Staff", "itm/staff", 4, 23, 340, 60, SK_MAGE, "Magic 60."),
    WPN("Hearthstaff", "itm/staff", 5, 28, 950, 82, SK_MAGE, "Magic 82. Crackles with ember-light."),
    ARM("Padded Vest", "ip/cloth", 1, 1, 20, 6, "Defence 6."),
    ARM("Bronze Mail", "itm/shield_round", 2, 6, 90, 16, "Defence 16."),
    ARM("Iron Mail", "itm/shield_round", 3, 16, 220, 30, "Defence 30."),
    ARM("Silver Mail", "itm/shield_badge", 4, 24, 420, 46, "Defence 46."),
    ARM("Copper Ring", "dn/coin", 1, 4, 30, 3, "Defence 3."),
    ARM("Silver Ring", "dn/coin", 3, 18, 180, 8, "Defence 8."),
    ARM("Gold Amulet", "dn/coin_stack_small", 4, 26, 400, 14, "Defence 14."),
    QST("Oak Kindling", "hx/resource_lumber", "Dry kindling to feed the Great Hearth."),
    QST("Bronze Hearth Grate", "ip/ingot_bronze", "A sturdy grate for the Great Hearth."),
    QST("Hearth Ember", "ip/ember", "A glowing ember, warm and alive."),
    QST("Crown of the Barrow King", "dn/chest_gold", "The old king's tarnished crown."),
    QST("Sealed Letter", "ip/scroll", "A letter for the Elder."),
};

// --------------------------------------------------------------- recipes
#define R(out, n, sk, lv, st, a, an, b, bn, c, cn, prog, qual, dur, xp) \
    {out, n, sk, lv, st, {{a, an}, {b, bn}, {c, cn}}, prog, qual, dur, xp}

const Recipe RECIPES[] = {
    // smelting
    R(IT_BRONZE_INGOT, 1, SK_SMITHING, 1, ST_FORGE, IT_COPPER_ORE, 1, IT_TIN_ORE, 1, 0, 0, 50, 300, 40, 22),
    R(IT_IRON_INGOT, 1, SK_SMITHING, 12, ST_FORGE, IT_IRON_ORE, 2, IT_COAL, 1, 0, 0, 120, 900, 60, 48),
    R(IT_SILVER_INGOT, 1, SK_SMITHING, 18, ST_FORGE, IT_SILVER_ORE, 2, IT_COAL, 1, 0, 0, 170, 1400, 60, 72),
    R(IT_GOLD_INGOT, 1, SK_SMITHING, 24, ST_FORGE, IT_GOLD_ORE, 2, IT_COAL, 2, 0, 0, 220, 1900, 70, 96),
    // forging
    R(IT_BRONZE_GRATE, 1, SK_SMITHING, 1, ST_ANVIL, IT_BRONZE_INGOT, 3, 0, 0, 0, 0, 70, 400, 40, 40),
    R(IT_BRONZE_SICKLE, 1, SK_SMITHING, 2, ST_ANVIL, IT_BRONZE_INGOT, 1, IT_OAK_PLANK, 1, 0, 0, 70, 420, 40, 34),
    R(IT_BRONZE_HATCHET, 1, SK_SMITHING, 3, ST_ANVIL, IT_BRONZE_INGOT, 2, IT_OAK_PLANK, 1, 0, 0, 80, 480, 40, 40),
    R(IT_BRONZE_PICK, 1, SK_SMITHING, 4, ST_ANVIL, IT_BRONZE_INGOT, 2, IT_OAK_PLANK, 1, 0, 0, 85, 520, 40, 44),
    R(IT_BRONZE_SWORD, 1, SK_SMITHING, 5, ST_ANVIL, IT_BRONZE_INGOT, 3, IT_OAK_PLANK, 1, 0, 0, 95, 600, 50, 55),
    R(IT_COPPER_RING, 1, SK_SMITHING, 6, ST_ANVIL, IT_COPPER_ORE, 3, IT_RUBY, 1, 0, 0, 90, 650, 40, 60),
    R(IT_BRONZE_MAIL, 1, SK_SMITHING, 8, ST_ANVIL, IT_BRONZE_INGOT, 5, IT_LEATHER, 1, 0, 0, 120, 800, 60, 80),
    R(IT_IRON_SICKLE, 1, SK_SMITHING, 13, ST_ANVIL, IT_IRON_INGOT, 1, IT_PINE_PLANK, 1, 0, 0, 140, 1000, 60, 90),
    R(IT_IRON_HATCHET, 1, SK_SMITHING, 14, ST_ANVIL, IT_IRON_INGOT, 2, IT_PINE_PLANK, 1, 0, 0, 150, 1100, 60, 100),
    R(IT_IRON_PICK, 1, SK_SMITHING, 15, ST_ANVIL, IT_IRON_INGOT, 2, IT_PINE_PLANK, 1, 0, 0, 155, 1150, 60, 104),
    R(IT_IRON_SWORD, 1, SK_SMITHING, 16, ST_ANVIL, IT_IRON_INGOT, 3, IT_LEATHER, 1, 0, 0, 165, 1250, 70, 115),
    R(IT_IRON_MAIL, 1, SK_SMITHING, 18, ST_ANVIL, IT_IRON_INGOT, 5, IT_LEATHER, 2, 0, 0, 190, 1450, 70, 140),
    R(IT_SILVER_SICKLE, 1, SK_SMITHING, 20, ST_ANVIL, IT_SILVER_INGOT, 1, IT_MAPLE_PLANK, 1, 0, 0, 200, 1600, 70, 150),
    R(IT_SILVER_HATCHET, 1, SK_SMITHING, 20, ST_ANVIL, IT_SILVER_INGOT, 2, IT_MAPLE_PLANK, 1, 0, 0, 205, 1650, 70, 155),
    R(IT_SILVER_PICK, 1, SK_SMITHING, 21, ST_ANVIL, IT_SILVER_INGOT, 2, IT_MAPLE_PLANK, 1, 0, 0, 210, 1700, 70, 160),
    R(IT_SILVER_RING, 1, SK_SMITHING, 20, ST_ANVIL, IT_SILVER_INGOT, 1, IT_SAPPHIRE, 1, 0, 0, 200, 1700, 60, 165),
    R(IT_SILVER_SWORD, 1, SK_SMITHING, 22, ST_ANVIL, IT_SILVER_INGOT, 3, IT_RUBY, 1, IT_LEATHER, 1, 220, 1850, 80, 185),
    R(IT_SILVER_MAIL, 1, SK_SMITHING, 24, ST_ANVIL, IT_SILVER_INGOT, 5, IT_LEATHER, 2, 0, 0, 240, 2100, 80, 210),
    R(IT_GOLD_AMULET, 1, SK_SMITHING, 26, ST_ANVIL, IT_GOLD_INGOT, 2, IT_EMERALD, 1, 0, 0, 260, 2400, 70, 240),
    R(IT_EMBERBRAND, 1, SK_SMITHING, 28, ST_ANVIL, IT_SILVER_SWORD, 1, IT_SPIRIT_EMBER, 3, IT_GOLD_INGOT, 1, 300, 3000, 90, 320),
    // carpentry
    R(IT_OAK_KINDLING, 1, SK_CARPENTRY, 1, ST_WORKBENCH, IT_OAK_LOG, 3, 0, 0, 0, 0, 55, 300, 40, 30),
    R(IT_OAK_PLANK, 1, SK_CARPENTRY, 1, ST_WORKBENCH, IT_OAK_LOG, 1, 0, 0, 0, 0, 45, 280, 40, 18),
    R(IT_OAK_ROD, 1, SK_CARPENTRY, 3, ST_WORKBENCH, IT_OAK_PLANK, 2, IT_CLOTH, 1, 0, 0, 70, 420, 40, 36),
    R(IT_OAK_STAFF, 1, SK_CARPENTRY, 4, ST_WORKBENCH, IT_OAK_PLANK, 3, 0, 0, 0, 0, 80, 480, 40, 42),
    R(IT_BIRCH_PLANK, 1, SK_CARPENTRY, 6, ST_WORKBENCH, IT_BIRCH_LOG, 1, 0, 0, 0, 0, 80, 520, 40, 30),
    R(IT_BIRCH_STAFF, 1, SK_CARPENTRY, 8, ST_WORKBENCH, IT_BIRCH_PLANK, 3, IT_SAPPHIRE, 1, 0, 0, 105, 700, 50, 70),
    R(IT_PINE_PLANK, 1, SK_CARPENTRY, 12, ST_WORKBENCH, IT_PINE_LOG, 1, 0, 0, 0, 0, 120, 900, 50, 50),
    R(IT_MAPLE_PLANK, 1, SK_CARPENTRY, 17, ST_WORKBENCH, IT_MAPLE_LOG, 1, 0, 0, 0, 0, 160, 1250, 60, 70),
    R(IT_MAPLE_STAFF, 1, SK_CARPENTRY, 17, ST_WORKBENCH, IT_MAPLE_PLANK, 3, IT_RUBY, 1, 0, 0, 175, 1350, 60, 130),
    R(IT_WILLOW_ROD, 1, SK_CARPENTRY, 19, ST_WORKBENCH, IT_WILLOW_PLANK, 2, IT_CLOTH, 2, 0, 0, 185, 1450, 60, 140),
    R(IT_WILLOW_PLANK, 1, SK_CARPENTRY, 22, ST_WORKBENCH, IT_WILLOW_LOG, 1, 0, 0, 0, 0, 200, 1650, 70, 90),
    R(IT_IRONWOOD_PLANK, 1, SK_CARPENTRY, 26, ST_WORKBENCH, IT_IRONWOOD_LOG, 1, 0, 0, 0, 0, 240, 2200, 70, 120),
    R(IT_IRONWOOD_STAFF, 1, SK_CARPENTRY, 24, ST_WORKBENCH, IT_IRONWOOD_PLANK, 3, IT_CRYSTAL, 1, 0, 0, 250, 2300, 80, 210),
    R(IT_IRONWOOD_ROD, 1, SK_CARPENTRY, 27, ST_WORKBENCH, IT_IRONWOOD_PLANK, 2, IT_CLOTH, 2, 0, 0, 260, 2500, 80, 230),
    R(IT_HEARTHSTAFF, 1, SK_CARPENTRY, 29, ST_WORKBENCH, IT_IRONWOOD_STAFF, 1, IT_SPIRIT_EMBER, 3, IT_CRYSTAL, 2, 300, 3100, 90, 330),
    // cooking
    R(IT_GRILLED_TROUT, 1, SK_COOKING, 1, ST_COOKPOT, IT_TROUT, 1, 0, 0, 0, 0, 45, 300, 40, 20),
    R(IT_MINT_TEA, 1, SK_COOKING, 3, ST_COOKPOT, IT_MINT, 2, 0, 0, 0, 0, 55, 380, 40, 28),
    R(IT_PERCH_STEW, 1, SK_COOKING, 6, ST_COOKPOT, IT_PERCH, 1, IT_SAGE, 1, 0, 0, 85, 560, 40, 45),
    R(IT_HERB_SALMON, 1, SK_COOKING, 13, ST_COOKPOT, IT_SALMON, 1, IT_LAVENDER, 1, 0, 0, 140, 1000, 60, 80),
    R(IT_PIKE_PIE, 1, SK_COOKING, 19, ST_COOKPOT, IT_PIKE, 1, IT_SUNPETAL, 2, 0, 0, 190, 1500, 70, 120),
    R(IT_GOLDEN_FEAST, 1, SK_COOKING, 25, ST_COOKPOT, IT_GOLDEN_CARP, 1, IT_EMBERROOT, 1, IT_MOONBLOOM, 1, 250, 2300, 80, 220),
    // alchemy
    R(IT_POTION_MINOR, 1, SK_ALCHEMY, 1, ST_ALCHEMY, IT_MINT, 2, 0, 0, 0, 0, 50, 320, 40, 24),
    R(IT_SAGE_TONIC, 1, SK_ALCHEMY, 5, ST_ALCHEMY, IT_SAGE, 2, 0, 0, 0, 0, 80, 520, 40, 40),
    R(IT_LAVENDER_ELIXIR, 1, SK_ALCHEMY, 11, ST_ALCHEMY, IT_LAVENDER, 2, IT_SLIME_GEL, 1, 0, 0, 130, 950, 50, 70),
    R(IT_SUNPETAL_DRAUGHT, 1, SK_ALCHEMY, 16, ST_ALCHEMY, IT_SUNPETAL, 2, IT_CRYSTAL, 1, 0, 0, 170, 1350, 60, 100),
    R(IT_EMBER_TONIC, 1, SK_ALCHEMY, 21, ST_ALCHEMY, IT_EMBERROOT, 2, IT_BONE, 1, 0, 0, 210, 1750, 70, 140),
    R(IT_MOON_ELIXIR, 1, SK_ALCHEMY, 26, ST_ALCHEMY, IT_MOONBLOOM, 2, IT_SPIRIT_EMBER, 1, 0, 0, 260, 2400, 80, 230),
};
const int NUM_RECIPES = HV_ARRAY_COUNT(RECIPES);

// ------------------------------------------------------------------ nodes
static const NodeDef NODES[] = {
    // trees
    {"Oak Tree", "pr/oak_b", "hx/tree_single_a_cut", SK_WOODCUTTING, 1, TOOL_AXE, 4, 25, 45, 1.0f, {{IT_OAK_LOG, 1, 90}, {0, 0, 0}, {0, 0, 0}}, false},
    {"Birch Tree", "pr/birch_a", "hx/tree_single_a_cut", SK_WOODCUTTING, 8, TOOL_AXE, 4, 42, 50, 1.0f, {{IT_BIRCH_LOG, 8, 85}, {IT_OAK_LOG, 1, 95}, {0, 0, 0}}, false},
    {"Pine Tree", "pr/pine_c", "hx/tree_single_b_cut", SK_WOODCUTTING, 15, TOOL_AXE, 4, 62, 55, 1.0f, {{IT_PINE_LOG, 15, 80}, {0, 0, 0}, {0, 0, 0}}, false},
    {"Maple Tree", "pr/autumn_b", "hx/tree_single_a_cut", SK_WOODCUTTING, 20, TOOL_AXE, 4, 84, 60, 1.1f, {{IT_MAPLE_LOG, 20, 78}, {0, 0, 0}, {0, 0, 0}}, false},
    {"Willow Tree", "pr/willow_a", "hx/tree_single_a_cut", SK_WOODCUTTING, 25, TOOL_AXE, 4, 105, 65, 1.1f, {{IT_WILLOW_LOG, 25, 75}, {0, 0, 0}, {0, 0, 0}}, false},
    {"Ironwood Tree", "pr/darkoak_a", "hx/tree_single_b_cut", SK_WOODCUTTING, 28, TOOL_AXE, 3, 140, 80, 1.15f, {{IT_IRONWOOD_LOG, 28, 70}, {0, 0, 0}, {0, 0, 0}}, false},
    // ore
    {"Copper Vein", "pr/ore_copper", "pr/rock_1", SK_MINING, 1, TOOL_PICK, 4, 25, 40, 1.0f, {{IT_COPPER_ORE, 1, 90}, {IT_STONE, 1, 95}, {IT_RUBY, 10, 6}}, false},
    {"Tin Vein", "pr/ore_tin", "pr/rock_1", SK_MINING, 1, TOOL_PICK, 4, 25, 40, 1.0f, {{IT_TIN_ORE, 1, 90}, {IT_STONE, 1, 95}, {IT_SAPPHIRE, 10, 6}}, false},
    {"Coal Seam", "pr/ore_coal", "pr/rock_1", SK_MINING, 8, TOOL_PICK, 4, 40, 45, 1.0f, {{IT_COAL, 8, 85}, {IT_STONE, 1, 95}, {0, 0, 0}}, false},
    {"Iron Vein", "pr/ore_iron", "pr/rock_1", SK_MINING, 12, TOOL_PICK, 4, 55, 50, 1.0f, {{IT_IRON_ORE, 12, 82}, {IT_STONE, 1, 95}, {IT_RUBY, 14, 8}}, false},
    {"Silver Vein", "pr/ore_silver", "pr/rock_1", SK_MINING, 18, TOOL_PICK, 4, 78, 60, 1.0f, {{IT_SILVER_ORE, 18, 78}, {IT_SAPPHIRE, 18, 10}, {IT_EMERALD, 20, 6}}, false},
    {"Gold Vein", "pr/ore_gold", "pr/rock_1", SK_MINING, 24, TOOL_PICK, 3, 100, 75, 1.0f, {{IT_GOLD_ORE, 24, 74}, {IT_EMERALD, 24, 10}, {IT_RUBY, 24, 10}}, false},
    {"Aether Crystal", "pr/ore_crystal", "pr/rock_1", SK_MINING, 28, TOOL_PICK, 3, 130, 90, 1.0f, {{IT_CRYSTAL, 28, 70}, {0, 0, 0}, {0, 0, 0}}, false},
    // herbs
    {"Mint Patch", "pr/herb_mint", nullptr, SK_HERBALISM, 1, TOOL_SICKLE, 3, 20, 35, 1.6f, {{IT_MINT, 1, 92}, {0, 0, 0}, {0, 0, 0}}, false},
    {"Sage Bush", "pr/herb_sage", nullptr, SK_HERBALISM, 6, TOOL_SICKLE, 3, 34, 40, 1.6f, {{IT_SAGE, 6, 88}, {IT_MINT, 1, 92}, {0, 0, 0}}, false},
    {"Lavender", "pr/herb_lavender", nullptr, SK_HERBALISM, 12, TOOL_SICKLE, 3, 50, 45, 1.6f, {{IT_LAVENDER, 12, 84}, {0, 0, 0}, {0, 0, 0}}, false},
    {"Sunpetal", "pr/herb_sunpetal", nullptr, SK_HERBALISM, 18, TOOL_SICKLE, 3, 68, 50, 1.6f, {{IT_SUNPETAL, 18, 80}, {0, 0, 0}, {0, 0, 0}}, false},
    {"Emberroot", "pr/herb_emberroot", nullptr, SK_HERBALISM, 22, TOOL_SICKLE, 3, 86, 55, 1.6f, {{IT_EMBERROOT, 22, 76}, {0, 0, 0}, {0, 0, 0}}, false},
    {"Moonbloom", "pr/herb_moonbloom", nullptr, SK_HERBALISM, 27, TOOL_SICKLE, 3, 115, 60, 1.6f, {{IT_MOONBLOOM, 27, 72}, {0, 0, 0}, {0, 0, 0}}, true},
    // fishing spots (loot level gates which fish can bite)
    {"Fishing Hole", nullptr, nullptr, SK_FISHING, 1, TOOL_ROD, 0, 24, 0, 1.0f, {{IT_TROUT, 1, 90}, {0, 0, 0}, {0, 0, 0}}, false},
    {"River Run", nullptr, nullptr, SK_FISHING, 1, TOOL_ROD, 0, 36, 0, 1.0f, {{IT_TROUT, 1, 70}, {IT_PERCH, 6, 60}, {IT_SALMON, 14, 40}}, false},
    {"Mirror Lake", nullptr, nullptr, SK_FISHING, 1, TOOL_ROD, 0, 46, 0, 1.0f, {{IT_TROUT, 1, 60}, {IT_PIKE, 10, 50}, {IT_GOLDEN_CARP, 24, 12}}, false},
};

const NodeDef *nodeDef(int type) {
    int idx = -1;
    if (type >= NT_OAK && type <= NT_IRONWOOD) idx = type - NT_OAK;
    else if (type >= NT_COPPER && type <= NT_CRYSTAL) idx = 6 + type - NT_COPPER;
    else if (type >= NT_MINT && type <= NT_MOONBLOOM) idx = 13 + type - NT_MINT;
    else if (type >= NT_FISH_POND && type <= NT_FISH_LAKE) idx = 19 + type - NT_FISH_POND;
    return idx >= 0 ? &NODES[idx] : nullptr;
}

// ---------------------------------------------------------------- enemies
const EnemyDef ENEMIES[EN_COUNT] = {
    {"", "", 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}, false, false, 255, 255, 255},
    {"Meadow Slime", "pr/slime_green", 2, 90, 8, 2.6f, 2.6f, 0.0f, 2.0f, 1.0f, 26, 3, {{IT_SLIME_GEL, 60, 1}, {IT_MINT, 20, 1}, {0, 0, 0}}, true, false, 255, 255, 255},
    {"Lake Slime", "pr/slime_blue", 5, 190, 15, 2.5f, 2.8f, 8.0f, 2.0f, 1.15f, 55, 6, {{IT_SLIME_GEL, 70, 1}, {IT_TROUT, 25, 1}, {0, 0, 0}}, true, false, 255, 255, 255},
    {"Ember Slime", "pr/slime_red", 9, 330, 26, 2.4f, 3.0f, 10.0f, 2.2f, 1.25f, 95, 10, {{IT_SLIME_GEL, 70, 2}, {IT_EMBERROOT, 15, 1}, {0, 0, 0}}, true, false, 255, 255, 255},
    {"Restless Bones", "chr/sk_minion", 8, 300, 22, 2.6f, 3.4f, 12.0f, 2.4f, 1.0f, 110, 12, {{IT_BONE, 70, 1}, {IT_CLOTH, 25, 1}, {0, 0, 0}}, false, false, 255, 255, 255},
    {"Barrow Warrior", "chr/sk_warrior", 11, 520, 34, 2.8f, 3.4f, 12.0f, 2.6f, 1.05f, 160, 18, {{IT_BONE, 80, 2}, {IT_LEATHER, 30, 1}, {IT_IRON_ORE, 20, 2}}, false, false, 255, 255, 255},
    {"Barrow Stalker", "chr/sk_rogue", 12, 420, 30, 1.7f, 4.6f, 14.0f, 2.4f, 1.0f, 170, 18, {{IT_BONE, 70, 1}, {IT_CLOTH, 40, 2}, {IT_LEATHER, 30, 1}}, false, false, 255, 255, 255},
    {"Barrow Hexer", "chr/sk_mage", 14, 400, 44, 3.0f, 3.0f, 16.0f, 14.0f, 1.0f, 200, 24, {{IT_BONE, 60, 1}, {IT_CRYSTAL, 12, 1}, {IT_SPIRIT_EMBER, 5, 1}}, false, true, 255, 255, 255},
    {"The Barrow King", "chr/sk_warrior", 18, 4200, 68, 3.0f, 3.6f, 18.0f, 3.6f, 1.75f, 1500, 300, {{IT_KINGS_CROWN, 100, 1}, {IT_SPIRIT_EMBER, 100, 3}, {IT_GOLD_ORE, 100, 4}}, false, true, 255, 220, 140},
    {"Gilded Slime", "pr/slime_gold", 14, 700, 30, 2.2f, 4.2f, 0.0f, 2.2f, 1.4f, 260, 80, {{IT_GOLD_ORE, 90, 2}, {IT_EMERALD, 30, 1}, {IT_SLIME_GEL, 100, 2}}, true, false, 255, 255, 255},
    {"Dusk Slime", "pr/slime_purple", 12, 450, 32, 2.4f, 3.2f, 11.0f, 2.2f, 1.3f, 140, 14, {{IT_SLIME_GEL, 80, 2}, {IT_LAVENDER, 20, 1}, {IT_SAPPHIRE, 4, 1}}, true, false, 255, 255, 255},
};

// ------------------------------------------------------------------- NPCs
static const NpcDef NPCS[] = {
    {NPC_ELDER, "Elder Maren", "Elder of Emberwick", "hm/elder", "itm/staff", "idle",
     "Welcome, traveller. Warm yourself... if you can.",
     {"The Hearth has burned since the founding. Now look at it.", "The guilds scattered when the flame went out.", "Every craft feeds the Hearth, in its way."}},
    {NPC_SMITH, "Torvald", "Blacksmith", "hm/smith", "itm/hammer", "idle_2h",
     "Hah! Another pair of hands. Can you swing a hammer?",
     {"Copper and tin make bronze. Bronze makes everything else.", "Silver bites the restless dead. Remember that.", "Keep your strikes steady. Mend before it cracks."}},
    {NPC_COOK, "Bess", "Tavern Keeper", "hm/cook", "itm/mug", "idle",
     "Sit, sit! Nobody leaves the Kettle & Crow hungry.",
     {"A good stew fixes most things.", "Old Tam swears the golden carp only bites at dusk.", "Fresh trout, a pinch of mint. Perfect."}},
    {NPC_CARPENTER, "Wren", "Carpenter", "hm/carpenter", "itm/axe", "idle",
     "Mind the sawdust. Need something built?",
     {"Oak for beginners, birch for staves, ironwood for masters.", "The willows by the river grow the finest rods.", "A plank is just a log that listened."}},
    {NPC_ALCHEMIST, "Isolde", "Alchemist", "hm/alchemist", "itm/spellbook", "idle",
     "Careful - that one bubbles. Herbs, you say?",
     {"Mint soothes, sage restores, lavender mends.", "Moonbloom only opens under the moon.", "Slime gel binds an elixir beautifully."}},
    {NPC_FISHER, "Old Tam", "Fisherman", "hm/fisher", "itm/rod", "idle",
     "Shh. You'll scare the fish.",
     {"Wait for the splash, then pull.", "Pike lurk in the deep middle of the lake.", "I've seen the golden carp. Twice!"}},
    {NPC_CAPTAIN, "Captain Rhea", "Captain of the Watch", "hm/captain", "itm/sword", "idle_2h",
     "Stay sharp. Strange things walk at night.",
     {"Hit hard, then step out of the glow on the ground.", "Slimes are a nuisance. Skeletons are a threat.", "The Barrow King... I thought it a story."}},
    {NPC_MERCHANT, "Pip", "Travelling Merchant", "hm/merchant", nullptr, "idle",
     "Buying, selling, haggling! Mostly haggling.",
     {"Prices change every morning. Watch the board.", "Gold ore? Name your price. Well, my price.", "I came for the festival. Then the Hearth went out."}},
    {NPC_FARMER, "Hollis", "Farmer", "hm/farmer", "itm/sickle", "idle",
     "Fine weather for it, eh?",
     {"Lavender grows thick along the field edges.", "The mill's turning again, at least.", "Slimes keep nibbling the cabbages."}},
    {NPC_MINER, "Dunstan", "Quarry Foreman", "hm/miner", "itm/pickaxe", "idle",
     "Copperhill provides, if you dig.",
     {"Copper and tin near the entrance. Iron deeper in.", "Gold's rare. Silver's rarer than it should be.", "Bring coal for the forge - Torvald always wants coal."}},
    {NPC_HERBALIST, "Fern", "Herbalist", "hm/herbalist", "itm/sickle", "idle",
     "The wood is talking today. Can you hear it?",
     {"Sage likes the shade under old oaks.", "Never pick more than the patch can spare.", "Glowcaps light the way at night."}},
    {NPC_WOODSMAN, "Ash", "Ranger", "hm/hunter", "itm/crossbow", "idle",
     "Camp's open to all adventurers. Grab a log.",
     {"Adventurers come through here every day.", "The Barrow's busier at night. Go in a group.", "Whisperwood's pines are tall and old."}},
    {NPC_BARD, "Lyra", "Bard", "hm/bard", "itm/mug", "idle",
     "A song for the cold Hearth? No... a song for when it burns again!",
     {"Every craft has a rhythm.", "I'll write a ballad about you. Make it a good one.", "La la la... the Hearth, the Hearth..."}},
    {NPC_PRIEST, "Brother Aldous", "Priest", "hm/priest", "itm/spellbook", "idle",
     "Peace be with you, child.",
     {"The dead stir because the flame faltered.", "Light the Hearth and the vale will heal.", "Silver and fire. That is what they fear."}},
    {NPC_CHILD, "Tilly", "Village Child", "hm/child", nullptr, "idle",
     "Are you a real adventurer?!",
     {"I saw a GOLD slime once!", "When I grow up I'll be a smith. Or a dragon.", "Elder Maren is grumpy because the fire's out."}},
    {NPC_WATCHMAN, "Garrick", "Barrow Watchman", "hm/watchman", "itm/shield_round", "block",
     "Turn back unless you mean to fight.",
     {"They rise thicker after dark.", "The crypt doors... I hear scraping.", "Take potions. Lots of potions."}},
};

const NpcDef *npcDef(int id) {
    for (const NpcDef &n : NPCS)
        if (n.id == id) return &n;
    return nullptr;
}

// ----------------------------------------------------------------- quests
const QuestDef QUESTS[] = {
    // 0
    {"The Cold Hearth", NPC_ELDER, 0xFF,
     {"You arrive on a cold morning. The Great Hearth in the square is dark.",
      "It has burned for three hundred years. When it went out, the guilds scattered and the dead began to stir.",
      "Help us rekindle it. Wren the carpenter can show you how to prepare kindling.", nullptr},
     {{QS_TALK, NPC_CARPENTER, 1, NPC_CARPENTER, "Speak with Wren the carpenter",
       {"Kindling? For the Hearth? About time someone asked!", "Take this old hatchet. Oaks grow just north of town.", nullptr, nullptr}},
      {QS_GATHER, IT_OAK_LOG, 4, 0, "Chop Oak Logs (sparkling oaks, north)", {nullptr, nullptr, nullptr, nullptr}},
      {QS_CRAFT, IT_OAK_KINDLING, 1, NPC_CARPENTER, "Craft Oak Kindling at the workbench",
       {"Good, even cuts. You've got the knack.", "Now the grate - Torvald will need copper and tin from Copperhill.", nullptr, nullptr}}},
     3, 30, IT_WORN_SICKLE, 1, SK_CARPENTRY, 60, true},
    // 1
    {"Bronze for the Grate", NPC_SMITH, 0,
     {"The old grate rusted through. I'll need bronze for a new one.", "Here, take a pickaxe. Copperhill is east, over the bridge.", nullptr, nullptr},
     {{QS_GATHER, IT_COPPER_ORE, 3, 0, "Mine Copper Ore", {nullptr, nullptr, nullptr, nullptr}},
      {QS_GATHER, IT_TIN_ORE, 3, 0, "Mine Tin Ore", {nullptr, nullptr, nullptr, nullptr}},
      {QS_CRAFT, IT_BRONZE_INGOT, 3, 0, "Smelt Bronze Ingots at the forge", {nullptr, nullptr, nullptr, nullptr}},
      {QS_CRAFT, IT_BRONZE_GRATE, 1, NPC_SMITH, "Forge the Bronze Hearth Grate at the anvil",
       {"Now THAT is a grate. You've a smith's hands.", "Keep the pickaxe. And come back when you want to make tools.", nullptr, nullptr}}},
     4, 40, IT_BRONZE_INGOT, 2, SK_SMITHING, 80, true},
    // 2
    {"A Warm Meal", NPC_COOK, 1,
     {"Folk are cold and hungry. A hot meal would lift spirits.", "Old Tam fishes at the lake. Borrow a rod, catch some trout,", "then cook them in my pot out back.", nullptr},
     {{QS_GATHER, IT_TROUT, 3, 0, "Catch Trout", {nullptr, nullptr, nullptr, nullptr}},
      {QS_CRAFT, IT_GRILLED_TROUT, 3, NPC_COOK, "Cook Grilled Trout at the tavern pot",
       {"Mmm! Crispy skin, flaky inside. Lovely.", "Here, keep a few coins. You've earned them.", nullptr, nullptr}}},
     2, 50, IT_MINT_TEA, 2, SK_COOKING, 70, true},
    // 3
    {"Remedies", NPC_ALCHEMIST, 2,
     {"The watch keeps coming back bruised. I need potions.", "Gather mint from the meadows and brew Minor Healing Potions.", nullptr, nullptr},
     {{QS_GATHER, IT_MINT, 4, 0, "Harvest Mint", {nullptr, nullptr, nullptr, nullptr}},
      {QS_CRAFT, IT_POTION_MINOR, 2, NPC_ALCHEMIST, "Brew Minor Healing Potions",
       {"Perfectly clear. Wonderful work.", "Keep some for yourself - you'll need them.", nullptr, nullptr}}},
     2, 50, IT_POTION_MINOR, 3, SK_ALCHEMY, 70, true},
    // 4
    {"Slime Season", NPC_CAPTAIN, 3,
     {"Slimes are creeping into the fields. Ready to fight?", "Take up a sword or a staff - whatever suits you - and thin them out.", nullptr, nullptr},
     {{QS_KILL, EN_SLIME_GREEN, 4, NPC_CAPTAIN, "Defeat Meadow Slimes",
       {"Nicely done. You fight like you mean it.", "Now - the Elder is waiting. Take her that kindling and grate.", nullptr, nullptr}}},
     1, 60, IT_PADDED_VEST, 1, SK_WARRIOR, 80, true},
    // 5
    {"Rekindling", NPC_ELDER, 4,
     {"You have the kindling and the grate? Then let us try.", "Lay them in the Hearth. Let the vale see the flame again.", nullptr, nullptr},
     {{QS_USE_STATION, ST_HEARTH, 1, NPC_ELDER, "Rekindle the Great Hearth",
       {"Look! LOOK! It burns!", "Three hundred years... and a stranger brings it back.", "The guilds will return. But the dead at the Barrow will not rest easy.", nullptr}}},
     1, 120, IT_HEARTH_EMBER, 1, SK_COUNT, 0, true},
    // 6
    {"Restless Dead", NPC_CAPTAIN, 5,
     {"With the Hearth lit, the Barrow's dead are angry. They're coming out.", "Push them back. Go north-east past the second bridge.", nullptr, nullptr},
     {{QS_KILL, EN_SK_MINION, 5, 0, "Defeat Restless Bones", {nullptr, nullptr, nullptr, nullptr}},
      {QS_KILL, EN_SK_WARRIOR, 2, NPC_CAPTAIN, "Defeat Barrow Warriors",
       {"You came back. Good.", "Garrick says something huge moves in the crypt at night.", nullptr, nullptr}}},
     2, 150, IT_LAVENDER_ELIXIR, 2, SK_WARRIOR, 300, true},
    // 7
    {"The Barrow King", NPC_CAPTAIN, 6,
     {"The old king of the barrow wakes after dark.", "Silver and fire, Brother Aldous says. Prepare well. Then end it.", nullptr, nullptr},
     {{QS_KILL, EN_BARROW_KING, 1, NPC_ELDER, "Defeat the Barrow King (at night)",
       {"The crown... so the stories were true.", "Emberwick owes you everything. Stay, hero. This is your home now.", nullptr, nullptr}}},
     1, 500, IT_SPIRIT_EMBER, 2, SK_COUNT, 0, true},
    // 8 side quests
    {"Logs for the Mill", NPC_WOODSMAN, 0,
     {"The camp's firewood is running low. Bring some birch?", nullptr, nullptr, nullptr},
     {{QS_DELIVER, IT_BIRCH_LOG, 5, NPC_WOODSMAN, "Deliver Birch Logs to Ash",
       {"Thanks, friend. Stay warm.", nullptr, nullptr, nullptr}}},
     1, 80, IT_OAK_ROD, 1, SK_WOODCUTTING, 150, false},
    {"Coal for Torvald", NPC_MINER, 1,
     {"Torvald's forge eats coal like a dragon. Dig some up?", nullptr, nullptr, nullptr},
     {{QS_DELIVER, IT_COAL, 6, NPC_SMITH, "Deliver Coal to Torvald",
       {"Coal! Bless you. The forge sings.", nullptr, nullptr, nullptr}}},
     1, 90, IT_SMITH_HAMMER, 1, SK_MINING, 180, false},
    {"Tam's Tall Tale", NPC_FISHER, 2,
     {"Prove the golden carp is real and I'll give you my lucky lure.", nullptr, nullptr, nullptr},
     {{QS_GATHER, IT_GOLDEN_CARP, 1, NPC_FISHER, "Catch a Golden Carp at Mirror Lake",
       {"HA! I KNEW IT! Wait till the tavern hears!", nullptr, nullptr, nullptr}}},
     1, 300, IT_WILLOW_ROD, 1, SK_FISHING, 500, false},
    {"Moonlit Harvest", NPC_HERBALIST, 3,
     {"Moonbloom grows near the Barrow, only at night. Brave enough?", nullptr, nullptr, nullptr},
     {{QS_DELIVER, IT_MOONBLOOM, 3, NPC_HERBALIST, "Bring Moonbloom to Fern",
       {"They glow even in your hands. Thank you.", nullptr, nullptr, nullptr}}},
     1, 260, IT_MOON_ELIXIR, 1, SK_HERBALISM, 600, false},
    {"Gilded Rumours", NPC_CHILD, 4,
     {"There's a GOLD slime by the lake! Southwest! Go see!", nullptr, nullptr, nullptr},
     {{QS_KILL, EN_SLIME_GOLD, 1, NPC_CHILD, "Find and defeat the Gilded Slime",
       {"You did?! You're the best adventurer EVER!", nullptr, nullptr, nullptr}}},
     1, 200, IT_EMERALD, 1, SK_COUNT, 0, false},
};
const int NUM_QUESTS = HV_ARRAY_COUNT(QUESTS);

// ------------------------------------------------- the simulated population
const char *const ADVENTURER_NAMES[] = {
    "Brannoc", "Elsie Thornwick", "Kaelen", "Mirabel", "Osric", "Sunniva", "Pellam", "Rowan Ashby",
    "Gwendolyn", "Torrin", "Ysolde", "Barnaby Quill", "Linnea", "Corwin", "Hestia Vale", "Fennick",
};
const int NUM_ADVENTURER_NAMES = HV_ARRAY_COUNT(ADVENTURER_NAMES);

const char *const CHAT_LINES[] = {
    "LFG Barrow tonight, anyone?", "WTS copper ore, cheap!", "Is the Hearth really lit again?!",
    "grats on the level!", "Anyone know where the iron is?", "brb, cooking",
    "These slimes are adorable. And sticky.", "Need a smith for bronze mail", "o/",
    "Golden carp spotted at the lake?!", "The view from the bridge is lovely", "Watch out for the glowing circles!",
    "Selling potions at the plaza", "Who's crafting tonight?", "First time in Hearthvale. Wow.",
    "Moonbloom is up, it's night!",
};
const int NUM_CHAT_LINES = HV_ARRAY_COUNT(CHAT_LINES);
