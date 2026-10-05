// Central game state shared by all gameplay modules.
#pragma once
#include "game/actor.h"
#include "game/data.h"
#include "gfx/renderer.h"
#include "platform/platform.h"

// --------------------------------------------------------------- inventory
struct InvSlot {
    u16 item;
    u8 count;
    u8 hq;
};

struct Inventory {
    static const int SIZE = 40;
    InvSlot slots[SIZE];
    void clear() { memset(slots, 0, sizeof(slots)); }
    int count(u16 item) const;
    int add(u16 item, int n, bool hq = false);   // returns number added
    bool remove(u16 item, int n);
    int freeSlots() const;
    void sort();
};

enum OptionFlags : u8 { OPT_WIDE_SET = 1, OPT_WIDE = 2 };

enum QuestStatus : u8 { QST_LOCKED, QST_AVAILABLE, QST_ACTIVE, QST_DONE };
struct QuestState {
    u8 status, step, progress, pad;
};

static const int MAX_QUESTS = 16;
static const int SAVE_VERSION = 3;

struct WorkOrder {
    u16 item;
    u8 count;
    u8 skill;
    u16 coins;
    u16 xp;
    bool done;
};

// Everything that is saved to the memory card.
struct PlayerData {
    u32 version;
    char name[20];
    u8 look;          // appearance (model index)
    u8 job;           // SK_WARRIOR / SK_MAGE
    u8 options;       // OPT_* flags
    u8 pad0;
    u32 xp[SK_COUNT];
    Inventory inv;
    u16 weapon, armor, accessory, pad1;
    u32 coins;
    QuestState quests[MAX_QUESTS];
    f32 hour;
    u32 day;
    f32 px, py, pz, pyaw;
    u8 hearthLit, kingDefeated, festival, introSeen;
    u32 playSeconds;
    u32 kills, crafted, gathered, hqCrafted;
    WorkOrder orders[3];
    u32 ordersDay;
    u32 checksum;
};

enum GameMode : u8 {
    MODE_TITLE, MODE_CREATE, MODE_PLAY, MODE_DIALOGUE, MODE_MENU, MODE_GATHER, MODE_FISH,
    MODE_CRAFT_SELECT, MODE_CRAFT, MODE_SHOP, MODE_BOARD, MODE_CUTSCENE, MODE_DEAD, MODE_REST
};

// ------------------------------------------------------------- entities
static const int MAX_ACTORS = 96;

struct Node {
    u8 type;
    Vec3 pos;
    f32 yaw;
    f32 respawn;     // >0 while depleted
    u8 integrity;
    bool fishing;
    f32 sparkle;
};
static const int MAX_NODES = 160;

struct StationInst {
    u8 type;
    Vec3 pos;
    f32 radius;
};
static const int MAX_STATIONS = 24;

// -------------------------------------------------------------- the game
struct Game {
    GameMode mode = MODE_TITLE;
    PlayerData pd;
    Actor player;
    Actor actors[MAX_ACTORS];
    Node nodes[MAX_NODES];
    int numNodes = 0;
    StationInst stations[MAX_STATIONS];
    int numStations = 0;
    Camera cam;
    f32 camYaw = 0, camPitch = 0.32f, camDist = 9.0f, camYawTarget = 0;
    PadState pad;
    f32 time = 0;            // seconds since boot (real)
    f32 dt = 1.0f / 60.0f;

    // player runtime
    int hp() const { return player.hp; }
    s32 gp = 0, maxGp = 0;
    int target = -1;         // actor index of combat target
    f32 gcd = 0;
    f32 combatTimer = 0;     // >0 while in combat
    u8 combo = 0;
    f32 comboTimer = 0;
    f32 castTimer = 0, castTotal = 0;
    u8 castAbility = 0;
    f32 cooldowns[8] = {0};
    f32 buffRampart = 0, buffDamage = 0, wardShield = 0;
    f32 dodgeTimer = 0;
    Vec3 dodgeDir;
    f32 actionLock = 0;      // can't move while > 0 (gather swing etc.)
    bool guarding = false;   // holding L in combat

    // interaction
    enum InteractKind : u8 { IK_NONE, IK_NPC, IK_NODE, IK_STATION, IK_FISH, IK_ENEMY };
    InteractKind interact = IK_NONE;
    int interactIndex = -1;

    // ui state shared by screens
    int menuTab = 0, menuSel = 0, menuScroll = 0;
    int dialogueNpc = 0;
    int dialogueQuest = -1;
    int dialogueKind = 0;
    int dialogueLine = 0;
    f32 dialogueChars = 0;
    const char *dialogueLines[8];
    int dialogueCount = 0;
    char dialogueBuf[8][160];
    int stationOpen = 0;

    // notifications
    char banner[64];
    char bannerSub[64];
    f32 bannerTimer = 0;
    struct Toast {
        char text[64];
        GXColor color;
        f32 timer;
        u16 icon;
    } toasts[6];
    char regionName[32];
    f32 regionTimer = 0;
    int currentRegion = -1;
    bool saving = false;
    f32 saveMsgTimer = 0;
    char saveMsg[48];

    f32 levelUpFlash = 0;
    f32 fadeIn = 1.0f;
    u32 frame = 0;
};

extern Game g;

// ------------------------------------------------------------ helpers
int skillLevel(int skill);
void addXp(int skill, u32 amount);
void giveItem(u16 item, int n, bool hq = false, bool notify = true);
void toast(const char *text, GXColor c, u16 icon = 0);
void showBanner(const char *title, const char *sub, f32 seconds = 3.5f);
void playerRecalcStats();
int bestTool(int toolKind);          // item id or 0
int toolTier(int toolKind);
const char *lookModel(int look);
Vec3 screenProject(const Vec3 &world, bool *onScreen);
f32 dayHour();
bool isNight();

// systems (implemented across game/*.cpp)
namespace nodes {
void init();
void update(f32 dt);
void draw();
int nearest(const Vec3 &p, f32 maxDist, bool fishingOnly);
}
namespace gather {
void begin(int node);
void update(f32 dt);
void drawUi();
void beginFishing(int node);
void updateFishing(f32 dt);
void drawFishingUi();
void drawWorld();
int currentNode();
int fishState();   // 0 ready, 1 cast, 2 wait, 3 bite, 4 reel, 5 result
}
namespace craft {
void openStation(int stationType);
void updateSelect(f32 dt);
void drawSelectUi();
void update(f32 dt);
void drawUi();
int selectedRecipe();   // recipe under the cursor in the select list, -1 otherwise
bool synthesisDone();
int forgeParts();          // >0 while the forge minigame is running
int forgeSelected();
void forgePart(int i, int *val, int *lo, int *hi);
}
namespace npcs {
void init();
void update(f32 dt);
void draw();
int nearestTalkable(const Vec3 &p, f32 maxDist);
void talk(int actorIndex);
void updateDialogue(f32 dt);
void drawDialogueUi();
int actorForNpc(int npcId);
}
namespace quests {
void init();
void onGather(u16 item, int n);
void onCraft(u16 item, int n);
void onKill(int enemyType);
void onTalk(int npcId);
void onStation(int station);
void onDeliverCheck(int npcId);
int availableFrom(int npcId);
int stepNpcWaiting(int npcId);      // quest index whose current step completes at this npc, else -1
int trackedQuest();
const char *objectiveText(int q, char *buf, int n);
void accept(int q);
void advance(int q);
void complete(int q);
bool isDone(int q);
bool isActive(int q);
}
namespace combat {
void init();
void update(f32 dt);
void playerUpdate(f32 dt);
void draw();
void drawUi();
void damagePlayer(int amount, int sourceActor);
int spawnEnemy(int type, const Vec3 &pos, int spawnIndex);
void clearTarget();
bool inCombat();
int nearestEnemy(const Vec3 &p, f32 maxDist, int skip);
bool wheelOpen();          // R held: skill wheel shown, world in slow motion
f32 timeScale();
const char *quickItemName();
int quickSlot();
}
namespace sim {
void init();
void update(f32 dt);
void onPlayerLevelUp();
void drawUi();
}
namespace shop {
void open(int stationType);
void update(f32 dt);
void drawUi();
void newDay();
}
namespace fx {
void init();
void update(f32 dt);
void draw();
void burst(const Vec3 &p, int kind, int count);
void floatText(const Vec3 &p, const char *text, GXColor c, f32 scale = 1.0f);
void drawFloatTexts();
void projectile(const Vec3 &from, int targetActor, int kind, int damage, bool fromPlayer);
void aoe(const Vec3 &p, f32 radius, f32 delay, int damage, int kind, bool hitsPlayer);
void hearthFire(bool lit);
}
enum FxKind { FX_SPARK, FX_LEAF, FX_DUST, FX_ROCK, FX_SPLASH, FX_LEVEL, FX_MAGIC, FX_FIRE, FX_HEAL, FX_HIT, FX_GOLD, FX_SMOKE, FX_ICE, FX_BOLT };
namespace hud {
void draw();
void drawMenu();
void updateMenu(f32 dt);
void drawPause();
}
namespace save {
bool write();
bool read();
void newGame(const char *name, int look, int job);
}
namespace audio {
void init();
void update(f32 dt);
void sfx(int id, f32 vol = 1.0f, f32 pitch = 1.0f);
void music(int track);
}
enum SfxId { SFX_CHOP, SFX_MINE, SFX_SPLASH, SFX_PICK, SFX_UI_MOVE, SFX_UI_OK, SFX_UI_BACK, SFX_LEVEL, SFX_HIT, SFX_HURT,
             SFX_SWING, SFX_FIRE, SFX_ICE, SFX_HEAL, SFX_CRAFT_STEP, SFX_CRAFT_DONE, SFX_COIN, SFX_QUEST, SFX_BITE, SFX_SLIME, SFX_BONES, SFX_STEP, SFX_FAIL, SFX_COUNT };
enum MusicId { MUS_NONE, MUS_TITLE, MUS_TOWN, MUS_FIELD, MUS_NIGHT, MUS_BATTLE, MUS_FESTIVAL };
