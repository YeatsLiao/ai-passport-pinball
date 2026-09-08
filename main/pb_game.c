// main/pb_game.c —— 游戏状态机实现。
// 步进约定:pb_game_step 只在 LVGL 任务(lv_timer)里跑,渲染同任务直接读状态;
// 按键回调(其他任务)只通过 pb_game_key 入队,无锁竞争。
#include "pb_game.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"

#define NVS_NS   "pinball"
#define NVS_KEY  "hs"

// ---- 计分表(对标原版量级) ----
#define SCORE_BUMP   1000
#define SCORE_SLING  250
#define SCORE_TARGET 500
#define SCORE_BANK   5000
#define SCORE_LANE   100
#define SCORE_MULT   2500

static void show_msg(pb_game *g, const char *txt, float dur) {
    snprintf(g->msg, sizeof(g->msg), "%s", txt);
    g->msg_timer = dur;
}

static void add_score(pb_game *g, uint32_t base) {
    g->score += base * g->mult;
    if (g->score > 9999999u) g->score = 9999999u;
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
    for (int i = 0; i < PB_TARGET_COUNT; i++) {
        g->target_down[i] = false;
        int s = pb_table_target_seg(&g->table, i);
        if (s >= 0) g->table.segs[s].solid = true;
    }
    g->mult = 1;
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
    g->mult = 1;
}

void pb_game_nvs_load(pb_game *g) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        uint32_t hs = 0;
        if (nvs_get_u32(h, NVS_KEY, &hs) == ESP_OK) g->high_score = hs;
        nvs_close(h);
    }
}

void pb_game_nvs_save(pb_game *g) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u32(h, NVS_KEY, g->high_score);
    nvs_commit(h);
    nvs_close(h);
}

// ---- 规则命中处理 ----

static void on_lane(pb_game *g, int lane) {
    if (g->lane_lit[lane]) return;
    g->lane_lit[lane] = true;
    add_score(g, SCORE_LANE);
    pb_audio_play(PB_SND_LANE);
    bool all = true;
    for (int i = 0; i < PB_LANE_COUNT; i++) all &= g->lane_lit[i];
    if (all) {
        for (int i = 0; i < PB_LANE_COUNT; i++) g->lane_lit[i] = false;
        if (g->mult < PB_MULT_MAX) {
            g->mult++;
            char buf[20];
            snprintf(buf, sizeof(buf), "MULTIPLIER x%d", g->mult);
            show_msg(g, buf, 2.0f);
            add_score(g, SCORE_MULT);
        } else {
            add_score(g, SCORE_MULT);           // 满倍率时车道照常给分
        }
        pb_audio_play(PB_SND_BONUS);
    }
}

static void on_target(pb_game *g, int idx) {
    if (g->target_down[idx]) return;
    g->target_down[idx] = true;
    int s = pb_table_target_seg(&g->table, idx);
    if (s >= 0) g->table.segs[s].solid = false;
    add_score(g, SCORE_TARGET);
    g->flash_target[idx] = 0.3f;
    pb_audio_play(PB_SND_TARGET);

    bool all = true;
    for (int i = 0; i < PB_TARGET_COUNT; i++) all &= g->target_down[i];
    if (all) {
        add_score(g, SCORE_BANK);
        show_msg(g, "BONUS 5000", 2.0f);
        g->target_reset = 1.2f;                 // 稍后整组立起
        pb_audio_play(PB_SND_BONUS);
    }
}

static void on_drain(pb_game *g) {
    g->table.world.ball.active = false;
    if (g->ball_save > 0) {
        spawn_ball_in_lane(g);
        g->state = PB_STATE_LAUNCH;
        g->state_timer = 0;
        show_msg(g, "BALL SAVED", 2.0f);
        pb_audio_play(PB_SND_BONUS);
        return;
    }
    pb_audio_play(PB_SND_DRAIN);
    g->state = PB_STATE_DRAIN;
    g->state_timer = 0;
}

// ---- 主步进 ----

void pb_game_step(pb_game *g, float dt) {
    g->state_timer += dt;
    if (g->msg_timer > 0) g->msg_timer -= dt;
    if (g->ball_save > 0) g->ball_save -= dt;
    for (int i = 0; i < g->table.circle_count && i < PB_CIRCLE_MAX; i++)
        if (g->flash_circle[i] > 0) g->flash_circle[i] -= dt;
    for (int i = 0; i < PB_TARGET_COUNT; i++)
        if (g->flash_target[i] > 0) g->flash_target[i] -= dt;
    if (g->flash_sling > 0) g->flash_sling -= dt;

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
            // 任意状态长按 OK 退回标题(结算/标题页无效)
            if (g->state == PB_STATE_PLAY || g->state == PB_STATE_LAUNCH) {
                g->table.world.ball.active = false;
                g->state = PB_STATE_TITLE;
                g->state_timer = 0;
                continue;
            }
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
            if (ev == PB_EV_PRESS) pb_audio_play(PB_SND_FLIP);
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
        // 挡板物理始终推进(LAUNCH 时球在道内,挡板对它无影响)
        pb_flipper_step(&g->table.flippers[0], dt);
        pb_flipper_step(&g->table.flippers[1], dt);
        pb_hit hit = {-1, -1, false};
        g->ball_prev_x = g->table.world.ball.pos.x;
        pb_step(&g->table.world, dt, &hit);

        pb_ball *b = &g->table.world.ball;
        if (b->pos.y > g->table.drain_y) { on_drain(g); break; }

        // 顶部车道 rollover:球横向滚过灯插的 x(原版就是这么触发的)。
        // y 容差把判定锁在灯插那一条带上,球在台面下方横穿时不会误触。
        if (fabsf(b->pos.y - g->table.lane_y) < 10.0f) {
            for (int i = 0; i < PB_LANE_COUNT; i++) {
                float lx = g->table.lane_x[i];
                if ((g->ball_prev_x - lx) * (b->pos.x - lx) < 0.0f) on_lane(g, i);
            }
        }

        if (hit.circle >= 0) {
            add_score(g, SCORE_BUMP);
            g->flash_circle[hit.circle] = 0.25f;
            pb_audio_play(PB_SND_BUMP);
        }
        if (hit.seg >= 0) {
            uint8_t kind = g->table.kind[hit.seg];
            if (kind == PB_SEG_SLING) {
                add_score(g, SCORE_SLING);
                g->flash_sling = 0.25f;
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
                if (g->score > g->high_score) {
                    g->high_score = g->score;
                    pb_game_nvs_save(g);
                }
                g->state = PB_STATE_OVER;
                pb_audio_play(PB_SND_OVER);
            } else {
                g->ball_num++;
                g->mult = 1;                    // 倍率与车道逐球重置(同原版 bonus)
                for (int i = 0; i < PB_LANE_COUNT; i++) g->lane_lit[i] = false;
                spawn_ball_in_lane(g);
                g->state = PB_STATE_LAUNCH;
                char buf[16];
                snprintf(buf, sizeof(buf), "BALL %d", g->ball_num);
                show_msg(g, buf, 1.5f);
            }
            g->state_timer = 0;
        }
        break;
    default:
        break;
    }
}
