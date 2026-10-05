// Quest progression, rewards and the Hearth rekindling.
#include <stdio.h>
#include "game/game.h"
#include "ui/ui.h"

void startHearthCutscene();

namespace quests {

namespace {
void refreshAvailability() {
    for (int q = 0; q < NUM_QUESTS && q < MAX_QUESTS; q++) {
        QuestState &s = g.pd.quests[q];
        if (s.status != QST_LOCKED) continue;
        u8 pre = QUESTS[q].prereq;
        if (pre == 0xFF || (pre < MAX_QUESTS && g.pd.quests[pre].status == QST_DONE)) s.status = QST_AVAILABLE;
    }
}

const QuestStep &cur(int q) { return QUESTS[q].steps[g.pd.quests[q].step]; }

void gift(int q) {
    // starting gifts handed over when a quest is accepted
    switch (q) {
        case 1: giveItem(IT_WORN_PICK, 1); break;
        case 2: giveItem(IT_WORN_ROD, 1); break;
        case 4:
            giveItem(IT_TRAINING_SWORD, 1);
            giveItem(IT_OAK_STAFF, 1);
            if (!g.pd.weapon) {
                g.pd.weapon = g.pd.job == SK_MAGE ? IT_OAK_STAFF : IT_TRAINING_SWORD;
                g.player.setHeld(ITEMS[g.pd.weapon].model);
                toast("Weapon equipped. Press Z to target enemies.", ui::GOLD);
            }
            break;
    }
}

void progress(u8 type, u16 target, int n) {
    for (int q = 0; q < NUM_QUESTS; q++) {
        QuestState &s = g.pd.quests[q];
        if (s.status != QST_ACTIVE) continue;
        const QuestStep &st = cur(q);
        if (st.type != type || st.target != target) continue;
        if (s.progress >= st.count) continue;
        s.progress = (u8)hvMin<int>(st.count, s.progress + n);
        char b[96];
        snprintf(b, sizeof(b), "%s  %d/%d", st.objective, s.progress, st.count);
        toast(b, ui::GOLD);
        if (s.progress >= st.count) {
            if (st.npc == 0) advance(q);
            else {
                const NpcDef *d = npcDef(st.npc);
                snprintf(b, sizeof(b), "Return to %s", d ? d->name : "the quest giver");
                toast(b, ui::GOLD);
                audio::sfx(SFX_QUEST);
            }
        }
    }
}
}  // namespace

void init() { refreshAvailability(); }

bool isDone(int q) { return q >= 0 && q < NUM_QUESTS && g.pd.quests[q].status == QST_DONE; }
bool isActive(int q) { return q >= 0 && q < NUM_QUESTS && g.pd.quests[q].status == QST_ACTIVE; }

void accept(int q) {
    QuestState &s = g.pd.quests[q];
    if (s.status != QST_AVAILABLE) return;
    s.status = QST_ACTIVE;
    s.step = 0;
    s.progress = 0;
    char b[96];
    snprintf(b, sizeof(b), "Quest accepted: %s", QUESTS[q].title);
    toast(b, ui::GOLD);
    showBanner(QUESTS[q].main ? "New Story Quest" : "New Quest", QUESTS[q].title, 3.0f);
    audio::sfx(SFX_QUEST);
    gift(q);
    // some steps may already be satisfied by items in hand (deliveries)
}

void advance(int q) {
    QuestState &s = g.pd.quests[q];
    if (s.status != QST_ACTIVE) return;
    const QuestStep &st = cur(q);
    if (st.type == QS_DELIVER) g.pd.inv.remove(st.target, st.count);
    if (q == 0 && s.step == 0) giveItem(IT_WORN_HATCHET, 1);
    s.step++;
    s.progress = 0;
    if (s.step >= QUESTS[q].numSteps) {
        complete(q);
        return;
    }
    audio::sfx(SFX_QUEST);
    char b[96];
    snprintf(b, sizeof(b), "New objective: %s", cur(q).objective);
    toast(b, ui::GOLD);
}

void complete(int q) {
    QuestState &s = g.pd.quests[q];
    const QuestDef &d = QUESTS[q];
    s.status = QST_DONE;
    showBanner("Quest Complete!", d.title, 4.0f);
    audio::sfx(SFX_QUEST);
    if (d.rewardCoins) {
        g.pd.coins += d.rewardCoins;
        char b[48];
        snprintf(b, sizeof(b), "+%u coins", d.rewardCoins);
        toast(b, ui::GOLD);
    }
    if (d.rewardItem) giveItem(d.rewardItem, d.rewardItemCount);
    if (d.rewardSkill < SK_COUNT && d.rewardXp) addXp(d.rewardSkill, d.rewardXp);
    fx::burst(g.player.pos + Vec3(0, 1.2f, 0), FX_GOLD, 30);
    if (q == 7) {
        g.pd.kingDefeated = 1;
        g.pd.festival = 1;
        showBanner("Hearthvale Reborn", "The vale celebrates its hero!", 6.0f);
    }
    refreshAvailability();
    save::write();
}

void onGather(u16 item, int n) { progress(QS_GATHER, item, n); }
void onCraft(u16 item, int n) { progress(QS_CRAFT, item, n); }
void onKill(int type) { progress(QS_KILL, (u16)type, 1); }

void onTalk(int npc) {
    for (int q = 0; q < NUM_QUESTS; q++) {
        QuestState &s = g.pd.quests[q];
        if (s.status != QST_ACTIVE) continue;
        const QuestStep &st = cur(q);
        if (st.type == QS_TALK && st.target == npc && st.npc == 0) advance(q);
    }
}

void onStation(int station) {
    if (station == ST_HEARTH) {
        for (int q = 0; q < NUM_QUESTS; q++) {
            QuestState &s = g.pd.quests[q];
            if (s.status != QST_ACTIVE) continue;
            const QuestStep &st = cur(q);
            if (st.type != QS_USE_STATION || st.target != ST_HEARTH) continue;
            if (g.pd.inv.count(IT_OAK_KINDLING) < 1 || g.pd.inv.count(IT_BRONZE_GRATE) < 1) {
                toast("You need Oak Kindling and a Bronze Hearth Grate.", ui::RED);
                audio::sfx(SFX_FAIL);
                return;
            }
            g.pd.inv.remove(IT_OAK_KINDLING, 1);
            g.pd.inv.remove(IT_BRONZE_GRATE, 1);
            s.progress = st.count;
            startHearthCutscene();
            return;
        }
        if (g.pd.hearthLit) {
            // resting at the hearth restores health and saves
            g.player.hp = g.player.maxHp;
            g.player.mp = g.player.maxMp;
            g.gp = g.maxGp;
            toast("The Hearth's warmth restores you.", ui::GOLD);
            fx::burst(g.player.pos + Vec3(0, 1, 0), FX_HEAL, 20);
            save::write();
        } else {
            toast("The Great Hearth is cold and dark.", ui::TEXT_DIM);
        }
        return;
    }
}

int availableFrom(int npc) {
    for (int q = 0; q < NUM_QUESTS; q++)
        if (g.pd.quests[q].status == QST_AVAILABLE && QUESTS[q].giver == npc) return q;
    return -1;
}

int stepNpcWaiting(int npc) {
    for (int q = 0; q < NUM_QUESTS; q++) {
        QuestState &s = g.pd.quests[q];
        if (s.status != QST_ACTIVE) continue;
        const QuestStep &st = cur(q);
        if (st.type == QS_TALK && st.target == npc) return q;
        if (st.npc != npc) continue;
        switch (st.type) {
            case QS_GATHER:
            case QS_CRAFT:
            case QS_KILL:
            case QS_USE_STATION:
                if (s.progress >= st.count) return q;
                break;
            case QS_DELIVER:
                if (g.pd.inv.count(st.target) >= st.count) return q;
                break;
        }
    }
    return -1;
}

void onDeliverCheck(int npc) { (void)npc; }

int trackedQuest() {
    for (int pass = 0; pass < 2; pass++)
        for (int q = 0; q < NUM_QUESTS; q++)
            if (g.pd.quests[q].status == QST_ACTIVE && QUESTS[q].main == (pass == 0)) return q;
    return -1;
}

const char *objectiveText(int q, char *buf, int n) {
    const QuestState &s = g.pd.quests[q];
    const QuestStep &st = QUESTS[q].steps[s.step];
    bool counted = st.type == QS_GATHER || st.type == QS_CRAFT || st.type == QS_KILL;
    if (counted && s.progress >= st.count && st.npc) {
        const NpcDef *d = npcDef(st.npc);
        snprintf(buf, n, "Return to %s", d ? d->name : "?");
    } else if (counted) {
        snprintf(buf, n, "%s (%d/%d)", st.objective, s.progress, st.count);
    } else if (st.type == QS_DELIVER) {
        snprintf(buf, n, "%s (%d/%d)", st.objective, hvMin<int>(g.pd.inv.count(st.target), st.count), st.count);
    } else {
        snprintf(buf, n, "%s", st.objective);
    }
    return buf;
}

}  // namespace quests
