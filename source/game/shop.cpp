// Pip's market (buy/sell with daily prices) and the notice board work orders.
#include <stdio.h>
#include "game/game.h"
#include "ui/ui.h"

namespace shop {

namespace {
const u16 STOCK[] = {IT_WORN_HATCHET, IT_WORN_PICK, IT_WORN_SICKLE, IT_WORN_ROD, IT_SMITH_HAMMER, IT_CARPENTER_SAW, IT_COOK_PAN,
                     IT_ALCHEMY_FLASK, IT_TRAINING_SWORD, IT_OAK_STAFF, IT_PADDED_VEST, IT_CLOTH, IT_LEATHER, IT_COAL,
                     IT_GRILLED_TROUT, IT_POTION_MINOR, IT_MINT};
const int NSTOCK = HV_ARRAY_COUNT(STOCK);
int s_tab = 0;   // 0 buy, 1 sell
int s_sel = 0, s_scroll = 0;
int s_sellList[Inventory::SIZE];
int s_sellCount = 0;
bool s_board = false;

u32 hash32(u32 x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// daily price factor per item: 0.8 .. 1.25 (the "market board")
f32 dayFactor(u16 item) { return 0.8f + (hash32(item * 2654435761u ^ (g.pd.day * 97u)) % 1000) / 1000.0f * 0.45f; }
int buyPrice(u16 item) { return hvMax(1, (int)(ITEMS[item].value * 2.2f * dayFactor(item) + 0.5f)); }
int sellPrice(u16 item, bool hq) { return hvMax(1, (int)(ITEMS[item].value * dayFactor(item) * (hq ? 1.25f : 1.0f) + 0.5f)); }

void buildSell() {
    s_sellCount = 0;
    for (int i = 0; i < Inventory::SIZE; i++) {
        const InvSlot &s = g.pd.inv.slots[i];
        if (!s.item || ITEMS[s.item].cat == IC_QUEST) continue;
        if (s.item == g.pd.weapon || s.item == g.pd.armor || s.item == g.pd.accessory) continue;
        s_sellList[s_sellCount++] = i;
    }
}

// --- notice board
void genOrders() {
    // deliverables scaled to the player's levels
    struct Cand { u16 item; u8 skill; };
    static const Cand C[] = {
        {IT_OAK_LOG, SK_WOODCUTTING}, {IT_BIRCH_LOG, SK_WOODCUTTING}, {IT_PINE_LOG, SK_WOODCUTTING}, {IT_MAPLE_LOG, SK_WOODCUTTING},
        {IT_COPPER_ORE, SK_MINING}, {IT_TIN_ORE, SK_MINING}, {IT_COAL, SK_MINING}, {IT_IRON_ORE, SK_MINING}, {IT_SILVER_ORE, SK_MINING},
        {IT_TROUT, SK_FISHING}, {IT_PERCH, SK_FISHING}, {IT_SALMON, SK_FISHING}, {IT_PIKE, SK_FISHING},
        {IT_MINT, SK_HERBALISM}, {IT_SAGE, SK_HERBALISM}, {IT_LAVENDER, SK_HERBALISM}, {IT_SUNPETAL, SK_HERBALISM},
        {IT_BRONZE_INGOT, SK_SMITHING}, {IT_IRON_INGOT, SK_SMITHING}, {IT_GRILLED_TROUT, SK_COOKING}, {IT_PERCH_STEW, SK_COOKING},
        {IT_OAK_PLANK, SK_CARPENTRY}, {IT_BIRCH_PLANK, SK_CARPENTRY}, {IT_POTION_MINOR, SK_ALCHEMY}, {IT_SAGE_TONIC, SK_ALCHEMY},
    };
    int n = 0;
    for (int tries = 0; tries < 60 && n < 3; tries++) {
        const Cand &c = C[hash32(g.pd.day * 31u + tries * 7u + 1) % HV_ARRAY_COUNT(C)];
        // level gate: is there a node/recipe at the player's level for it?
        int lvl = skillLevel(c.skill);
        int need = 1;
        for (int r = 0; r < NUM_RECIPES; r++)
            if (RECIPES[r].output == c.item) need = RECIPES[r].level;
        static const u16 HIGH[] = {IT_PINE_LOG, IT_MAPLE_LOG, IT_IRON_ORE, IT_SILVER_ORE, IT_SALMON, IT_PIKE, IT_LAVENDER, IT_SUNPETAL, IT_BIRCH_LOG, IT_COAL, IT_PERCH, IT_SAGE};
        static const u8 HIGH_L[] = {15, 20, 12, 18, 14, 10, 12, 18, 8, 8, 6, 6};
        for (int h = 0; h < HV_ARRAY_COUNT(HIGH); h++)
            if (HIGH[h] == c.item) need = HIGH_L[h];
        if (lvl < need) continue;
        bool dup = false;
        for (int k = 0; k < n; k++)
            if (g.pd.orders[k].item == c.item) dup = true;
        if (dup) continue;
        WorkOrder &o = g.pd.orders[n++];
        o.item = c.item;
        o.skill = c.skill;
        o.count = (u8)(3 + hash32(g.pd.day + tries) % 4);
        o.coins = (u16)(ITEMS[c.item].value * o.count * 2 + 10);
        o.xp = (u16)(30 + need * 12 + o.count * 6);
        o.done = false;
    }
    for (int k = n; k < 3; k++) {
        g.pd.orders[k] = WorkOrder();
        g.pd.orders[k].done = true;
    }
    g.pd.ordersDay = g.pd.day + 1;
}
}  // namespace

void newDay() {
    genOrders();
    toast("A new day: fresh work orders and market prices!", ui::GOLD);
}

void open(int st) {
    s_board = st == ST_NOTICEBOARD;
    s_tab = 0;
    s_sel = s_scroll = 0;
    if (s_board) {
        if (g.pd.ordersDay != g.pd.day + 1) genOrders();
        g.mode = MODE_BOARD;
    } else {
        buildSell();
        g.mode = MODE_SHOP;
    }
    audio::sfx(SFX_UI_OK);
}

void update(f32 dt) {
    (void)dt;
    PadState &pad = g.pad;
    if (pad.pressed & BTN_B) {
        g.mode = MODE_PLAY;
        audio::sfx(SFX_UI_BACK);
        return;
    }
    if (s_board) {
        if (pad.pressed & BTN_UP) s_sel = (s_sel + 2) % 3;
        if (pad.pressed & BTN_DOWN) s_sel = (s_sel + 1) % 3;
        if (pad.pressed & BTN_A) {
            WorkOrder &o = g.pd.orders[s_sel];
            if (o.done || !o.item) return;
            if (g.pd.inv.count(o.item) < o.count) {
                toast("You don't have enough.", ui::RED);
                audio::sfx(SFX_FAIL);
                return;
            }
            g.pd.inv.remove(o.item, o.count);
            o.done = true;
            g.pd.coins += o.coins;
            addXp(o.skill, o.xp);
            char b[64];
            snprintf(b, sizeof(b), "Work order complete! +%u coins", o.coins);
            toast(b, ui::GOLD);
            audio::sfx(SFX_COIN);
            fx::burst(g.player.pos + Vec3(0, 1.4f, 0), FX_GOLD, 20);
        }
        return;
    }
    if (pad.pressed & (BTN_R | BTN_L | BTN_X)) {
        s_tab ^= 1;
        s_sel = s_scroll = 0;
        buildSell();
        audio::sfx(SFX_UI_MOVE);
    }
    int count = s_tab == 0 ? NSTOCK : s_sellCount;
    if (!count) return;
    if (pad.pressed & BTN_UP) { s_sel = (s_sel + count - 1) % count; audio::sfx(SFX_UI_MOVE); }
    if (pad.pressed & BTN_DOWN) { s_sel = (s_sel + 1) % count; audio::sfx(SFX_UI_MOVE); }
    if (s_sel < s_scroll) s_scroll = s_sel;
    if (s_sel >= s_scroll + 7) s_scroll = s_sel - 6;
    if (pad.pressed & (BTN_A | BTN_Y)) {
        if (s_tab == 0) {
            u16 it = STOCK[s_sel];
            int price = buyPrice(it);
            if ((int)g.pd.coins < price) {
                toast("Not enough coins.", ui::RED);
                audio::sfx(SFX_FAIL);
                return;
            }
            if (!g.pd.inv.add(it, 1)) {
                toast("Your bag is full.", ui::RED);
                return;
            }
            g.pd.coins -= price;
            audio::sfx(SFX_COIN);
            char b[64];
            snprintf(b, sizeof(b), "Bought %s", ITEMS[it].name);
            toast(b, ui::WHITE, it);
        } else {
            InvSlot &s = g.pd.inv.slots[s_sellList[s_sel]];
            if (!s.item) return;
            int n = (pad.pressed & BTN_Y) ? s.count : 1;
            int price = sellPrice(s.item, s.hq) * n;
            u16 it = s.item;
            s.count = (u8)(s.count - n);
            if (!s.count) s.item = 0;
            g.pd.coins += price;
            audio::sfx(SFX_COIN);
            char b[64];
            snprintf(b, sizeof(b), "Sold %s x%d for %d", ITEMS[it].name, n, price);
            toast(b, ui::GOLD);
            buildSell();
            if (s_sel >= s_sellCount) s_sel = hvMax(0, s_sellCount - 1);
        }
    }
}

void drawUi() {
    f32 W = ui::width();
    char b[96];
    if (s_board) {
        f32 x = W * 0.5f - 260, y = 70;
        ui::panel(x, y, 520, 290, ui::rgba(70, 48, 30, 235));
        ui::text(FONT_BIG, W * 0.5f, y + 6, "Guild Work Orders", ui::GOLD, AL_CENTER, 0.8f);
        snprintf(b, sizeof(b), "Day %u - new orders every morning", g.pd.day + 1);
        ui::text(FONT_SMALL, W * 0.5f, y + 42, b, ui::rgba(230, 210, 180), AL_CENTER);
        for (int k = 0; k < 3; k++) {
            const WorkOrder &o = g.pd.orders[k];
            f32 ry = y + 70 + k * 68;
            bool sel = k == s_sel;
            ui::panel(x + 14, ry, 492, 60, sel ? ui::rgba(250, 236, 200, 240) : ui::rgba(236, 222, 190, 220), 6);
            GXColor ink = ui::rgba(60, 40, 26);
            if (!o.item) {
                ui::text(FONT_UI, x + 30, ry + 18, "No order posted.", ink);
                continue;
            }
            ui::icon(ITEMS[o.item].model, x + 46, ry + 30, 40, sel ? 1.2f : 0.3f);
            snprintf(b, sizeof(b), "Deliver %d %s", o.count, ITEMS[o.item].name);
            ui::text(FONT_UI, x + 80, ry + 8, b, o.done ? ui::rgba(120, 110, 100) : ink);
            snprintf(b, sizeof(b), "%u coins  +  %u %s XP", o.coins, o.xp, SKILLS[o.skill].name);
            ui::text(FONT_SMALL, x + 80, ry + 34, b, ui::rgba(130, 80, 30));
            if (o.done) ui::text(FONT_BIG, x + 470, ry + 10, "DONE", ui::rgba(60, 140, 60), AL_RIGHT, 0.7f);
            else {
                snprintf(b, sizeof(b), "%d/%d", hvMin<int>(g.pd.inv.count(o.item), o.count), o.count);
                ui::text(FONT_UI, x + 490, ry + 18, b, g.pd.inv.count(o.item) >= o.count ? ui::rgba(40, 130, 40) : ink, AL_RIGHT);
            }
        }
        f32 px = x;
        px += ui::prompt(GL_A, px, y + 300, "Turn in");
        ui::prompt(GL_B, px, y + 300, "Close");
        return;
    }
    f32 x = 40, y = 60, w = 380;
    ui::panel(x, y, w, 372, ui::PANEL);
    ui::text(FONT_BIG, x + 16, y + 6, "Pip's Wares", ui::GOLD, AL_LEFT, 0.75f);
    ui::panel(x + 220, y + 12, 70, 28, s_tab == 0 ? ui::SEL : ui::rgba(60, 54, 74, 220), 8);
    ui::text(FONT_SMALL, x + 255, y + 17, "Buy", ui::WHITE, AL_CENTER);
    ui::panel(x + 296, y + 12, 70, 28, s_tab == 1 ? ui::SEL : ui::rgba(60, 54, 74, 220), 8);
    ui::text(FONT_SMALL, x + 331, y + 17, "Sell", ui::WHITE, AL_CENTER);
    int count = s_tab == 0 ? NSTOCK : s_sellCount;
    for (int k = 0; k < 7 && s_scroll + k < count; k++) {
        int i = s_scroll + k;
        f32 ry = y + 54 + k * 44;
        bool sel = i == s_sel;
        u16 it;
        bool hq = false;
        int qty = 0;
        if (s_tab == 0) it = STOCK[i];
        else {
            const InvSlot &s = g.pd.inv.slots[s_sellList[i]];
            it = s.item;
            hq = s.hq;
            qty = s.count;
        }
        if (sel) ui::panel(x + 8, ry - 2, w - 16, 42, ui::rgba(255, 220, 140, 70), 8);
        ui::icon(ITEMS[it].model, x + 32, ry + 19, 32, sel ? 1.2f : 0.3f);
        if (s_tab == 1 && qty > 1) snprintf(b, sizeof(b), "%s%s x%d", hq ? "HQ " : "", ITEMS[it].name, qty);
        else snprintf(b, sizeof(b), "%s%s", hq ? "HQ " : "", ITEMS[it].name);
        ui::text(FONT_UI, x + 56, ry + 8, b, ui::WHITE);
        int price = s_tab == 0 ? buyPrice(it) : sellPrice(it, hq);
        f32 f = dayFactor(it);
        snprintf(b, sizeof(b), "%d", price);
        ui::text(FONT_UI, x + w - 40, ry + 8, b, ui::GOLD, AL_RIGHT);
        ui::text(FONT_SMALL, x + w - 16, ry + 11, f > 1.1f ? "+" : (f < 0.9f ? "-" : ""), f > 1.1f ? ui::GREEN : ui::RED, AL_RIGHT);
    }
    if (!count) ui::text(FONT_UI, x + 20, y + 70, "Nothing to sell.", ui::TEXT_DIM);
    snprintf(b, sizeof(b), "Your coins: %u", g.pd.coins);
    ui::text(FONT_UI, x + 16, y + 344, b, ui::GOLD);
    f32 px = x + w + 20, py = y + 10;
    ui::prompt(GL_A, px, py, s_tab == 0 ? "Buy" : "Sell one");
    if (s_tab == 1) ui::prompt(GL_Y, px, py + 32, "Sell stack");
    ui::prompt(GL_X, px, py + 64, "Buy / Sell tab");
    ui::prompt(GL_B, px, py + 96, "Leave");
    ui::textWrap(FONT_SMALL, px, py + 140, W - px - 24, "Prices change every morning. + means today's price is high, - means low.", ui::TEXT_DIM);
}

}  // namespace shop
