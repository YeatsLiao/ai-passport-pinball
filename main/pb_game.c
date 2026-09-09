// main/pb_game.c —— 游戏状态机实现。
// 步进约定:pb_game_step 只在 LVGL 任务(lv_timer)里跑,渲染同任务直接读状态;
// 按键回调(其他任务)只通过 pb_game_key 入队,无锁竞争。
//
// 每条规则的注释都指向 docs/space-cadet-spec.md 的条目号(§x.y),分值取自
// SpaceCadetPinball-web/SpaceCadetPinball/control.cpp 的原版数组。规格之外
// 的行为(丢球保险、卡死救球)在 §6 移植偏差表里逐条登记。
#include "pb_game.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"

#define NVS_NS     "pinball"
#define NVS_KEY_HS "hs"       // 旧版单值最高分,只读迁移用
#define NVS_KEY_TBL "hst"     // 5 槽榜单 blob(§5.1/§5.5)

// ---- 分值表(§2) ----
#define SCORE_REBO      500u      // §2.1 rebo3/4 回弹器(本台面的小立柱)
#define SCORE_LANE      2000u     // §2.2 roll1/2/3 再入车道
#define SCORE_LANE_ALL  5000u     // 收尾新增:三车道灯全亮成组加成(§6 偏差登记)
#define SCORE_TGT       500u      // §2.3 target7/8/9 单个
#define SCORE_TGT_BANK  1500u     // §2.3 三个全倒
#define SCORE_HOLE      20000u    // §2.4 a_kout3 黑洞 control_kickout_score2[0]
#define SCORE_WELL      50000u    // §2.4 a_kout1 引力井 control_kickout_score3[0]
#define SCORE_STAR      500u      // 右道星柱单个(实体碰撞)
#define SCORE_STAR_ALL  2500u     // 一局内三枚全亮加成
#define PB_SCORE_MAX    99999999u // 记分板 8 位宽;原版是 1e9 进位(§4.6)

// §2.1 control_bump_scores1[BmpIndex]
static const uint32_t BUMP_SCORES[PB_BUMP_TIER_MAX + 1] = { 500, 1000, 1500, 2000 };
// §2.4 control_kickout_score1 的 0/2/3/4 档(第 1 档是 Special,§6 不移植)
static const uint32_t HS_TIERS[PB_HS_TIERS] = { 10000, 20000, 50000, 150000 };
// §4.2 score_multipliers[]
static const uint32_t MULT_VALUES[PB_MULT_COUNT] = { 1, 2, 3, 5, 10 };
// §3 RankRcArray[9] 的缩写版(RANK 面板净宽 38px)
static const char *const RANK_NAMES[PB_RANK_MAX] = {
    "CDT", "ENS", "LT", "CPT", "LCDR", "CDR", "CMOD", "ADM", "FADM"
};

// 三个 kickout 洞的踢出后冷却。原版读自 .dat 的 TimerTime1(未随源码发布),
// 属可调手感参数,登记在规格 §6 移植偏差表。
#define HOLE_COOLDOWN_S 6.0f
#define WELL_COOLDOWN_S 8.0f
#define HS_COOLDOWN_S   3.0f

uint32_t pb_mult_value(uint8_t idx) {
    return (idx < PB_MULT_COUNT) ? MULT_VALUES[idx] : MULT_VALUES[PB_MULT_COUNT - 1];
}

uint32_t pb_bump_score(uint8_t tier) {
    return (tier <= PB_BUMP_TIER_MAX) ? BUMP_SCORES[tier] : BUMP_SCORES[PB_BUMP_TIER_MAX];
}

const char *pb_rank_name(uint8_t rank) {
    if (rank < 1 || rank > PB_RANK_MAX) rank = 1;
    return RANK_NAMES[rank - 1];
}

static void show_msg(pb_game *g, const char *txt, float dur) {
    snprintf(g->msg, sizeof(g->msg), "%s", txt);
    g->msg_timer = dur;
}

// 返回实际得分(含倍率),命中点飘字直接显示这个值。
static uint32_t add_score(pb_game *g, uint32_t base) {
    uint32_t v = base * pb_mult_value(g->mult_idx);
    g->score += v;
    if (g->score > PB_SCORE_MAX) g->score = PB_SCORE_MAX;
    return v;
}

// 在命中点登记一条得分飘字(无空闲槽则丢弃)。
static void popup(pb_game *g, uint32_t v, float x, float y) {
    for (int i = 0; i < PB_POPUPS; i++) {
        pb_popup *p = &g->popups[i];
        if (p->t <= 0) {
            p->t = 0.8f;
            p->x = (int16_t)x;
            p->y = (int16_t)y;
            p->value = v;
            return;
        }
    }
}

// ---- 榜单(§5) ----

// §5.5 Verification:原版是 Σ(名字字符) + Σ(分数);本固件无名字输入(§6 OUT),
// 校验和退化为 Σ分数 + 固定盐值,仍能挡住半写/篡改。
typedef struct {
    int32_t score[PB_HS_SLOTS];
    uint32_t verify;
} hs_blob_t;

static uint32_t hs_checksum(const hs_blob_t *b) {
    uint32_t sum = 0x5ca1ab1eu;
    for (int i = 0; i < PB_HS_SLOTS; i++) sum += (uint32_t)b->score[i];
    return sum;
}

static void hs_sync_top(pb_game *g) {
    g->high_score = (g->hs[0] > 0) ? (uint32_t)g->hs[0] : 0u;
}

static void hs_clear(pb_game *g) {
    for (int i = 0; i < PB_HS_SLOTS; i++) g->hs[i] = PB_HS_EMPTY;   // §5.2 哨兵
    g->hs_new = -1;
    hs_sync_top(g);
}

// §5.3 入榜判定 + §5.4 插入下移。返回是否入榜。
static bool hs_submit(pb_game *g, uint32_t score) {
    g->hs_new = -1;
    if (score == 0) return false;                    // §5.3 score<=0 不入榜
    for (int i = 0; i < PB_HS_SLOTS; i++) {
        if (g->hs[i] >= (int32_t)score) continue;    // 找第一个更小的槽
        for (int k = PB_HS_SLOTS - 1; k > i; k--) g->hs[k] = g->hs[k - 1];
        g->hs[i] = (int32_t)score;
        g->hs_new = (int8_t)i;
        break;
    }
    hs_sync_top(g);
    return g->hs_new >= 0;
}

void pb_game_nvs_load(pb_game *g) {
    hs_clear(g);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;

    hs_blob_t b;
    size_t sz = sizeof(b);
    esp_err_t er = nvs_get_blob(h, NVS_KEY_TBL, &b, &sz);
    if (er == ESP_OK && sz == sizeof(b) && b.verify == hs_checksum(&b)) {
        memcpy(g->hs, b.score, sizeof(b.score));     // 正常读回
    } else if (er == ESP_ERR_NVS_NOT_FOUND) {
        uint32_t legacy = 0;                         // 旧版单值 → 榜首
        if (nvs_get_u32(h, NVS_KEY_HS, &legacy) == ESP_OK && legacy > 0)
            g->hs[0] = (int32_t)legacy;
    }                                                // 其余 = 校验不过,整表清零(§5.5)
    hs_sync_top(g);
    nvs_close(h);
}

void pb_game_nvs_save(pb_game *g) {
    hs_blob_t b;
    memcpy(b.score, g->hs, sizeof(b.score));
    b.verify = hs_checksum(&b);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, NVS_KEY_TBL, &b, sizeof(b));
    nvs_commit(h);
    nvs_close(h);
}

// ---- 输入事件队列 ----

static void ev_push(pb_game *g, pb_key_t key, pb_key_ev_t ev) {
    uint8_t next = (uint8_t)((g->ev_head + 1) & 15);
    if (next == g->ev_tail) return;                 // 满则丢弃最旧来不及处理的事件
    g->ev_buf[g->ev_head] = (uint8_t)((key << 2) | ev);
    g->ev_head = next;
}

static bool ev_pop(pb_game *g, pb_key_t *key, pb_key_ev_t *ev) {
    if (g->ev_tail == g->ev_head) return false;
    uint8_t v = g->ev_buf[g->ev_tail];
    g->ev_tail = (uint8_t)((g->ev_tail + 1) & 15);
    *key = (pb_key_t)(v >> 2);
    *ev = (pb_key_ev_t)(v & 3);
    return true;
}

void pb_game_key(pb_game *g, pb_key_t key, pb_key_ev_t ev) {
    ev_push(g, key, ev);
}

// ---- 生命周期 ----

static void reset_playfield(pb_game *g) {
    for (int i = 0; i < PB_LANE_COUNT; i++) g->lane_lit[i] = false;
    for (int i = 0; i < PB_STAR_COUNT; i++) g->star_lit[i] = false;
    for (int i = 0; i < PB_TARGET_COUNT; i++) {
        g->target_down[i] = false;
        int s = pb_table_target_seg(&g->table, i);
        if (s >= 0) g->table.segs[s].solid = true;
    }
    g->mult_idx = 0;            // §4.2 倍率索引从 x1 起
    g->bump_prog = 0;           // §3 bmpr_inc_lights 清空
    g->target_reset = 0;
}

static void spawn_ball_in_lane(pb_game *g) {
    pb_ball *b = &g->table.world.ball;
    b->pos.x = g->table.spawn_x;
    b->pos.y = g->table.spawn_y;
    b->vel.x = 0;
    b->vel.y = 0;
    b->active = true;
    g->launch_power = 0;
}

static void new_game(pb_game *g) {
    g->score = 0;
    g->ball_num = 1;
    g->ball_save = 0;
    g->ball_save_used = false;
    g->new_high = false;
    g->hs_new = -1;
    g->bump_tier = 0;           // §2.1 攻击档位随新局重置
    g->hs_lights = 0;           // §2.4 hyperspace 档位随新局重置
    g->ring_lit = 0;            // §3 outer_circle
    g->rank = 1;                // §3 起始军衔 = Cadet(见 §6 移植偏差)
    g->hole_timer = 0;
    g->hole_cooldown = 0;
    g->flash_hole = 0;
    g->well_timer = 0;
    g->well_cooldown = 0;
    g->hs_timer = 0;
    g->hs_cooldown = 0;
    g->flash_upg = 0;
    g->stuck_time = 0;
    g->lost_time = 0;
    for (int i = 0; i < PB_POPUPS; i++) g->popups[i].t = 0;
    reset_playfield(g);
    spawn_ball_in_lane(g);
    g->state = PB_STATE_LAUNCH;
    g->state_timer = 0;
    show_msg(g, "BALL 1", 1.5f);
}

void pb_game_init(pb_game *g) {
    memset(g, 0, sizeof(*g));
    pb_table_init(&g->table);
    g->state = PB_STATE_TITLE;
    g->state_timer = 0;
    g->rank = 1;
    hs_clear(g);
}

// ---- 规则命中处理 ----

// 顶部车道灯(收尾规则,§6 移植偏差再登记):穿过即亮(原版是切换式),
// 三盏全亮 = 车道组加成后整组清空 —— 给顶部三灯明确的成组意义
// (实机反馈连续两轮问"顶部三个灯是干嘛的")。
static void on_lane(pb_game *g, int lane) {
    add_score(g, SCORE_LANE);                       // §2.2 穿越即 2000,与灯态无关
    pb_audio_play(PB_SND_LANE);
    if (!g->lane_lit[lane]) {
        g->lane_lit[lane] = true;
        bool all = true;
        for (int i = 0; i < PB_LANE_COUNT; i++) all &= g->lane_lit[i];
        if (all) {
            pb_ball *b = &g->table.world.ball;
            uint32_t got = add_score(g, SCORE_LANE_ALL);
            popup(g, got, b->pos.x, b->pos.y - 10.0f);
            show_msg(g, "LANES BONUS", 2.0f);
            pb_audio_play(PB_SND_BONUS);
            for (int i = 0; i < PB_LANE_COUNT; i++) g->lane_lit[i] = false;
        }
    }
    // 升档条件(收尾简化):升级灯满后穿过任意车道即升档。
    if (g->bump_prog < PB_UPG_LAMPS) return;

    g->bump_prog = 0;
    g->flash_upg = 1.0f;
    if (g->bump_tier < PB_BUMP_TIER_MAX) {
        g->bump_tier++;                             // §2.1 BmpIndex+1
        show_msg(g, "WEAPONS UP", 2.0f);            // RC 5 "Weapons Upgraded" 缩短版
        pb_audio_play(PB_SND_BONUS);
    }
}

// §3 AddRankProgress:外环满 → 军衔 +1。原版这里**不计分**,本实现照此办理。
static void add_ring(pb_game *g) {
    if (g->ring_lit < PB_RING_LAMPS) g->ring_lit++;
    if (g->ring_lit < PB_RING_LAMPS) return;
    g->ring_lit = 0;
    char buf[20];
    if (g->rank < PB_RANK_MAX) {
        g->rank++;
        snprintf(buf, sizeof buf, "PROMOTED %s", pb_rank_name(g->rank));  // RC 83
    } else {
        snprintf(buf, sizeof buf, "TOP RANK");
    }
    show_msg(g, buf, 2.0f);
    pb_audio_play(PB_SND_BONUS);
}

// §2.3 倍率目标组:单个 500;三个全倒 1500 + 倍率升一档(RC 56-59)。
static void on_target(pb_game *g, int idx) {
    if (g->target_down[idx]) return;
    g->target_down[idx] = true;
    int s = pb_table_target_seg(&g->table, idx);
    if (s >= 0) g->table.segs[s].solid = false;
    pb_ball *b = &g->table.world.ball;
    uint32_t got = add_score(g, SCORE_TGT);
    popup(g, got, b->pos.x, b->pos.y - 10.0f);
    g->flash_target[idx] = 0.3f;
    pb_audio_play(PB_SND_TARGET);

    bool all = true;
    for (int i = 0; i < PB_TARGET_COUNT; i++) all &= g->target_down[i];
    if (!all) return;

    got = add_score(g, SCORE_TGT_BANK);
    popup(g, got, b->pos.x, b->pos.y - 12.0f);
    if (g->mult_idx < PB_MULT_COUNT - 1) g->mult_idx++;    // §4.2 索引推进
    char buf[20];
    snprintf(buf, sizeof buf, "MULT x%lu",
             (unsigned long)pb_mult_value(g->mult_idx));
    show_msg(g, buf, 2.0f);
    add_ring(g);                                          // §3 组完成推进 1 段
    g->target_reset = 1.2f;                               // 稍后整组立起
    pb_audio_play(PB_SND_BONUS);
}

// 右道星柱:实体回弹柱,击中即亮(物理反弹由 circles 碰撞体负责)。
// 一局内三枚全亮 → 加成后整组重置,可循环再点。
static void on_star(pb_game *g, int idx) {
    if (g->star_lit[idx]) return;             // 已亮不重复计分
    g->star_lit[idx] = true;
    pb_ball *b = &g->table.world.ball;
    uint32_t got = add_score(g, SCORE_STAR);
    popup(g, got, b->pos.x - 6.0f, b->pos.y - 8.0f);
    pb_audio_play(PB_SND_BUMP);

    bool all = true;
    for (int i = 0; i < PB_STAR_COUNT; i++) all &= g->star_lit[i];
    if (!all) return;

    got = add_score(g, SCORE_STAR_ALL);
    popup(g, got, b->pos.x - 6.0f, b->pos.y - 18.0f);
    pb_audio_play(PB_SND_BONUS);
    for (int i = 0; i < PB_STAR_COUNT; i++) g->star_lit[i] = false;
}

// §4.3 球保存只在发射瞬间武装、每球一次;用掉即熄灯(原版 Message(20))。
static void on_drain(pb_game *g) {
    pb_ball *b = &g->table.world.ball;
    b->active = false;
    // 清所有过场残留:计时挂着会在到期时把下一颗球瞬移进台面(丢球/无限球根因)
    g->hole_timer = 0;
    g->well_timer = 0;
    g->hs_timer = 0;
    g->flash_hole = 0;
    g->lost_time = 0;
    g->bump_prog = 0;                             // §3 掉球清空升级灯组
    if (g->ball_save > 0) {
        g->ball_save = 0;
        g->ball_save_used = true;
        spawn_ball_in_lane(g);
        g->state = PB_STATE_LAUNCH;
        g->state_timer = 0;
        show_msg(g, "RE-DEPLOY", 2.0f);          // RC 96
        pb_audio_play(PB_SND_BONUS);
        return;
    }
    pb_audio_play(PB_SND_DRAIN);
    g->state = PB_STATE_DRAIN;
    g->state_timer = 0;
}

// 局末/中途退场统一结算:入榜 + 快照榜首 + 落盘。
// 之前只有打满 3 球才存,暂停菜单 EXIT/RESTART 直接丢分——"高分不记录"的根因。
static void finalize_score(pb_game *g) {
    bool in = hs_submit(g, g->score);
    g->new_high = in;
    if (in) pb_game_nvs_save(g);
}

// ---- 主步进 ----

// 三个 kickout 洞共用"捕获 → 过场 → 向上/向下踢回"的骨架。
static void kickout_capture(pb_game *g, uint32_t base, const char *label,
                            float *timer, float *cooldown, float cooldown_s) {
    pb_ball *b = &g->table.world.ball;
    float px = b->pos.x, py = b->pos.y;
    b->active = false;
    uint32_t got = add_score(g, base);
    popup(g, got, px, py - 10.0f);
    show_msg(g, label, 2.0f);
    *timer = 0.8f;
    *cooldown = cooldown_s;                     // §2.4 踢出后进入冷却,防立即回吸
    pb_audio_play(PB_SND_BONUS);
}

// 黑洞踢回时的左右微偏:每次交替,避免连续落洞的球沿同一条轨迹反复被吸。
static uint8_t hole_kick_side;
static float hole_kick_sign(void) {
    hole_kick_side ^= 1;
    return hole_kick_side ? 1.0f : -1.0f;
}

void pb_game_step(pb_game *g, float dt) {
    g->state_timer += dt;
    if (g->msg_timer > 0) g->msg_timer -= dt;
    if (g->ball_save > 0) g->ball_save -= dt;
    for (int i = 0; i < PB_POPUPS; i++)
        if (g->popups[i].t > 0) g->popups[i].t -= dt;
    for (int i = 0; i < g->table.circle_count && i < PB_CIRCLE_MAX; i++)
        if (g->flash_circle[i] > 0) g->flash_circle[i] -= dt;
    for (int i = 0; i < PB_TARGET_COUNT; i++)
        if (g->flash_target[i] > 0) g->flash_target[i] -= dt;
    if (g->flash_sling > 0) g->flash_sling -= dt;
    if (g->flash_upg > 0) g->flash_upg -= dt;
    if (g->flash_hole > 0) g->flash_hole -= dt;
    if (g->hole_cooldown > 0) g->hole_cooldown -= dt;
    if (g->well_cooldown > 0) g->well_cooldown -= dt;
    if (g->hs_cooldown > 0) g->hs_cooldown -= dt;

    // 目标组整组重置
    if (g->target_reset > 0) {
        g->target_reset -= dt;
        if (g->target_reset <= 0) {
            for (int i = 0; i < PB_TARGET_COUNT; i++) {
                g->target_down[i] = false;
                int s = pb_table_target_seg(&g->table, i);
                if (s >= 0) g->table.segs[s].solid = true;
            }
        }
    }

    // 消费按键事件
    pb_key_t key; pb_key_ev_t ev;
    while (ev_pop(g, &key, &ev)) {
        if (key == PB_KEY_OK && ev == PB_EV_LONG) {
            // 长按 OK 呼出暂停菜单(进行中状态);标题/结算页无效
            if (g->state == PB_STATE_PLAY || g->state == PB_STATE_LAUNCH) {
                g->paused_prev = g->state;
                g->pause_sel = 0;
                g->state = PB_STATE_PAUSE;
                g->state_timer = 0;
                continue;
            }
        }
        // 暂停菜单:UP/DOWN 移动选项,OK 确认
        if (g->state == PB_STATE_PAUSE) {
            if (ev != PB_EV_PRESS) continue;
            if (key == PB_KEY_L)      g->pause_sel = (uint8_t)((g->pause_sel + 2) % 3);
            else if (key == PB_KEY_R) g->pause_sel = (uint8_t)((g->pause_sel + 1) % 3);
            else if (key == PB_KEY_OK) {
                if (g->pause_sel == 0)      g->state = g->paused_prev;   // RESUME
                else if (g->pause_sel == 1) {                            // RESTART
                    finalize_score(g);
                    new_game(g);
                } else {                                                 // EXIT
                    finalize_score(g);
                    g->state = PB_STATE_TITLE;
                    g->state_timer = 0;
                }
            }
            continue;
        }
        // 只响应按下事件,避免长按 OK 退出后的抬起事件误触发新游戏
        if (g->state == PB_STATE_TITLE && g->state_timer > 0.4f && ev == PB_EV_PRESS) {
            new_game(g);
            pb_audio_play(PB_SND_LAUNCH);
            continue;
        }
        if (g->state == PB_STATE_OVER && g->state_timer > 1.0f && ev == PB_EV_PRESS) {
            g->state = PB_STATE_TITLE;
            g->state_timer = 0;
            continue;
        }
        if (g->state == PB_STATE_LAUNCH && key == PB_KEY_OK) {
            if (ev == PB_EV_PRESS) g->launch_power = 0.001f;
            if (ev == PB_EV_RELEASE && g->launch_power > 0) {
                // 蓄力发射:力度决定初速,八成力以上能绕过顶部弧
                float v = 800.0f + 450.0f * g->launch_power;
                g->table.world.ball.vel.x = 0;
                g->table.world.ball.vel.y = -v;
                if (!g->ball_save_used)         // §4.3 球保存只在每球首次发射时武装
                    g->ball_save = PB_BALL_SAVE_S;
                g->state = PB_STATE_PLAY;
                g->state_timer = 0;
                show_msg(g, "", 0);
                pb_audio_play(PB_SND_LAUNCH);
            }
            continue;
        }
        if (g->state == PB_STATE_PLAY && key != PB_KEY_OK) {
            pb_flipper_set(&g->table.flippers[key == PB_KEY_L ? 0 : 1],
                           ev == PB_EV_PRESS);
            if (ev == PB_EV_PRESS) {
                pb_audio_play(PB_SND_FLIP);
                // §3 挡板挥动推进 bmpr_inc_lights(原版是循环移位,本固件从全灭
                // 起,故实现为递增,见 §6 移植偏差)
                if (g->bump_prog < PB_UPG_LAMPS) g->bump_prog++;
                // 满灯瞬间提示下一步:穿过顶部车道即升档(实机反馈"看不懂灯组")。
                if (g->bump_prog == PB_UPG_LAMPS) show_msg(g, "UPG READY", 2.0f);
            }
        }
    }

    // 发球蓄力(按住期间线性增长,松开即射)
    if (g->state == PB_STATE_LAUNCH && g->launch_power > 0) {
        g->launch_power += dt / 0.9f;
        if (g->launch_power > 1) g->launch_power = 1;
    }

    // 状态推进
    switch (g->state) {
    case PB_STATE_LAUNCH:
    case PB_STATE_PLAY: {
        // 挡板在 pb_step 内随细分同步推进(LAUNCH 时球在道内,挡板对它无影响)
        pb_hit hit = {-1, -1, false};
        g->ball_prev_x = g->table.world.ball.pos.x;
        pb_step(&g->table.world, dt, &hit);

        pb_ball *b = &g->table.world.ball;
        if (b->pos.y > g->table.drain_y) { on_drain(g); break; }

        // 顶部车道 rollover:球横向滚过灯插的 x(§2.2 原版即按穿越判定)。
        // y 容差把判定锁在灯插那一条带上,球在台面下方横穿时不会误触。
        if (fabsf(b->pos.y - g->table.lane_y) < 10.0f) {
            for (int i = 0; i < PB_LANE_COUNT; i++) {
                float lx = g->table.lane_x[i];
                if ((g->ball_prev_x - lx) * (b->pos.x - lx) < 0.0f) on_lane(g, i);
            }
        }

        // 防卡死:低速滞留(顶弧夹角/柱缝)1.2s 后斜向给一记救球冲量。
        // 只在挡板区以上生效——球停在放下的挡板上等击球是正常状态。
        if (b->active) {
            float sp2 = b->vel.x * b->vel.x + b->vel.y * b->vel.y;
            if (sp2 < 900.0f && b->pos.y < 230.0f) {
                g->stuck_time += dt;
                if (g->stuck_time > 1.2f) {
                    g->stuck_time = 0;
                    g->stuck_side = !g->stuck_side;
                    b->vel.x = g->stuck_side ? -110.0f : 110.0f;
                    b->vel.y = -380.0f;
                }
            } else {
                g->stuck_time = 0;
            }
        }

        // 黑洞 a_kout3:徽章行星中心的"行星虫洞"(实机反馈后从落球口移入)。
        // §2.4 20000 分 + 向上踢回挡板区;冷却期内不捕获,球自然从旁穿过。
        if (g->hole_cooldown <= 0 && b->active) {
            float hx = b->pos.x - PB_HOLE_X, hy = b->pos.y - PB_HOLE_Y;
            if (hx * hx + hy * hy < PB_HOLE_R * PB_HOLE_R) {
                g->flash_hole = 0.9f;
                kickout_capture(g, SCORE_HOLE, "BLACK HOLE",
                                &g->hole_timer, &g->hole_cooldown, HOLE_COOLDOWN_S);
            }
        }
        // 引力井 a_kout1:§2.4 50000 分,长冷却,沿左上窄通道向上踢回顶拱。
        if (g->well_cooldown <= 0 && b->active) {
            float hx = b->pos.x - PB_WELL_X, hy = b->pos.y - PB_WELL_Y;
            if (hx * hx + hy * hy < PB_WELL_R * PB_WELL_R) {
                kickout_capture(g, SCORE_WELL, "GRAVITY WELL",
                                &g->well_timer, &g->well_cooldown, WELL_COOLDOWN_S);
            }
        }
        // hyperspace a_kout2:§2.4 按已亮档数取档,第 4 档清环(原版满 4 段清)。
        if (g->hs_cooldown <= 0 && b->active) {
            float hx = b->pos.x - PB_HS_X, hy = b->pos.y - PB_HS_Y;
            if (hx * hx + hy * hy < PB_HS_R * PB_HS_R) {
                kickout_capture(g, HS_TIERS[g->hs_lights], "HYPERSPACE",
                                &g->hs_timer, &g->hs_cooldown, HS_COOLDOWN_S);
                g->hs_lights = (uint8_t)((g->hs_lights + 1) % PB_HS_TIERS);
            }
        }
        // 各洞的踢出动作(位置都在通道/落球口内,不会把球塞进墙里)
        if (g->hole_timer > 0) {
            g->hole_timer -= dt;
            if (g->hole_timer <= 0) {
                b->pos.x = PB_HOLE_X;
                b->pos.y = PB_HOLE_Y - 4.0f;
                b->vel.x = hole_kick_sign() * 40.0f;
                b->vel.y = -540.0f;                 // 向上踢回挡板区,避开分隔柱
                b->active = true;
            }
        }
        if (g->well_timer > 0) {
            g->well_timer -= dt;
            if (g->well_timer <= 0) {
                b->pos.x = PB_WELL_X;
                b->pos.y = PB_WELL_Y + 8.0f;
                b->vel.x = 12.0f;
                b->vel.y = -420.0f;   // 向上踢:冲出左上窄通道回顶拱(实机反馈 #3:
                                      // 原向下吐回左导轨,捕获感像白吞一球)
                b->active = true;
            }
        }
        if (g->hs_timer > 0) {
            g->hs_timer -= dt;
            if (g->hs_timer <= 0) {
                b->pos.x = PB_HS_X;
                b->pos.y = PB_HS_Y + 8.0f;
                b->vel.x = -50.0f;
                b->vel.y = -400.0f;   // 向上踢:冲出右上窄通道回顶拱(实机反馈:
                                      // 右墙洞得到后应往上弹出去,原向下吐右道)
                b->active = true;
            }
        }
        // 丢球保险(§6 登记的异常恢复,不计分、不改玩法):球不活跃且三个洞过场
        // 全空闲时 2.2s 后按掉球处理,杜绝"球消失且球数不变"的假死。
        if (!b->active && g->hole_timer <= 0 && g->well_timer <= 0 && g->hs_timer <= 0) {
            g->lost_time += dt;
            if (g->lost_time > 2.2f) {
                g->lost_time = 0;
                on_drain(g);
                break;
            }
        } else {
            g->lost_time = 0;
        }

        if (hit.circle >= 0) {
            int ci = hit.circle;
            int star_base = g->table.circle_count - PB_STAR_COUNT;
            if (ci >= star_base) {
                // 右道星柱:实体碰撞命中,亮灯计分(反弹已由物理层完成)。
                g->flash_circle[ci] = 0.25f;
                on_star(g, ci - star_base);
            } else {
                // §2.1 只有 kick>0 的三个是 pop bumper(走档位分);kick==0 的小立柱
                // 走 §2.1 的 rebo 固定 500。之前全部按 bumper 计分 = 白拿最高 2000。
                uint32_t base = (ci < g->table.circle_count && g->table.circles[ci].kick > 0.0f)
                                ? pb_bump_score(g->bump_tier) : SCORE_REBO;
                uint32_t got = add_score(g, base);
                g->flash_circle[ci] = 0.25f;
                popup(g, got, b->pos.x, b->pos.y - 10.0f);
                pb_audio_play(PB_SND_BUMP);
            }
        }
        if (hit.seg >= 0) {
            uint8_t kind = g->table.kind[hit.seg];
            if (kind == PB_SEG_SLING) {
                uint32_t got = add_score(g, SCORE_REBO);      // §2.1 弹弓 = rebo 500
                g->flash_sling = 0.25f;
                popup(g, got, b->pos.x, b->pos.y - 8.0f);
                pb_audio_play(PB_SND_SLING);
            } else if (kind == PB_SEG_TARGET) {
                for (int i = 0; i < PB_TARGET_COUNT; i++)
                    if (pb_table_target_seg(&g->table, i) == hit.seg) on_target(g, i);
            }
        }
        break;
    }
    case PB_STATE_DRAIN:
        if (g->state_timer >= 1.2f) {
            if (g->ball_num >= PB_BALLS_TOTAL) {
                finalize_score(g);
                g->state = PB_STATE_OVER;
                pb_audio_play(PB_SND_OVER);
            } else {
                g->ball_num++;
                g->mult_idx = 0;                  // §4.2 每球结束倍率归 x1
                g->ball_save_used = false;        // 新球重新获得一次球保存资格
                for (int i = 0; i < PB_LANE_COUNT; i++) g->lane_lit[i] = false;
                for (int i = 0; i < PB_STAR_COUNT; i++) g->star_lit[i] = false;
                spawn_ball_in_lane(g);
                g->state = PB_STATE_LAUNCH;
                char buf[16];
                snprintf(buf, sizeof buf, "BALL %d", g->ball_num);
                show_msg(g, buf, 1.5f);
            }
            g->state_timer = 0;
        }
        break;
    default:
        break;
    }
}
