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
#define SCORE_HOLE   5000

// 伪随机(虫洞弹出车道用,确定性即可)。
static uint32_t rnd32(void) {
    static uint32_t s = 0x9e3779b9u;
    s = s * 1664525u + 1013904223u;
    return s >> 8;
}

static void show_msg(pb_game *g, const char *txt, float dur) {
    snprintf(g->msg, sizeof(g->msg), "%s", txt);
    g->msg_timer = dur;
}

// 返回实际得分(含倍率),命中点飘字直接显示这个值。
static uint32_t add_score(pb_game *g, uint32_t base) {
    uint32_t v = base * g->mult;
    g->score += v;
    if (g->score > 9999999u) g->score = 9999999u;
    return v;
}

// 在命中点登记一条得分飘字(无空闲槽则丢弃最不重要的:直接不显)。
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
    g->bump_combo = 0;
    g->new_high = false;
    g->hole_timer = 0;
    g->hole_cooldown = 0;
    g->flash_hole = 0;
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
    uint32_t got = add_score(g, SCORE_TARGET);
    popup(g, got, g->table.world.ball.pos.x, g->table.world.ball.pos.y - 10.0f);
    g->flash_target[idx] = 0.3f;
    pb_audio_play(PB_SND_TARGET);

    bool all = true;
    for (int i = 0; i < PB_TARGET_COUNT; i++) all &= g->target_down[i];
    if (all) {
        uint32_t got = add_score(g, SCORE_BANK);
        popup(g, got, g->table.world.ball.pos.x, g->table.world.ball.pos.y - 12.0f);
        show_msg(g, "BONUS 5000", 2.0f);
        g->target_reset = 1.2f;                 // 稍后整组立起
        pb_audio_play(PB_SND_BONUS);
    }
}

static void on_drain(pb_game *g) {
    g->table.world.ball.active = false;
    g->bump_combo = 0;                  // 连击随球结束
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
    for (int i = 0; i < PB_POPUPS; i++)
        if (g->popups[i].t > 0) g->popups[i].t -= dt;
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
            if (key == PB_KEY_L)     g->pause_sel = (uint8_t)((g->pause_sel + 2) % 3);
            else if (key == PB_KEY_R) g->pause_sel = (uint8_t)((g->pause_sel + 1) % 3);
            else if (key == PB_KEY_OK) {
                if (g->pause_sel == 0)       g->state = g->paused_prev;   // RESUME
                else if (g->pause_sel == 1)  new_game(g);                // RESTART
                else { g->state = PB_STATE_TITLE; g->state_timer = 0; }  // EXIT
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
        // 挡板在 pb_step 内随细分同步推进(LAUNCH 时球在道内,挡板对它无影响)
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

        // 中央虫洞:球滚进洞心被捕获,大额得分后从随机车道口弹出。
        // 冷却期防刚弹出就被吸回。
        if (g->flash_hole > 0) g->flash_hole -= dt;
        if (g->hole_cooldown > 0) {
            g->hole_cooldown -= dt;
        } else if (b->active) {
            float hx = b->pos.x - PB_HOLE_X, hy = b->pos.y - PB_HOLE_Y;
            if (hx * hx + hy * hy < PB_HOLE_R * PB_HOLE_R) {
                float px = b->pos.x, py = b->pos.y;
                b->active = false;
                uint32_t got = add_score(g, SCORE_HOLE);
                popup(g, got, px, py - 10.0f);
                show_msg(g, "WORMHOLE!", 1.5f);
                g->flash_hole = 0.9f;
                g->hole_timer = 0.9f;
                pb_audio_play(PB_SND_BONUS);
            }
        }
        if (g->hole_timer > 0) {
            g->hole_timer -= dt;
            if (g->hole_timer <= 0) {
                g->hole_lane = (uint8_t)(rnd32() % PB_LANE_COUNT);
                b->pos.x = g->table.lane_x[g->hole_lane];
                b->pos.y = 56.0f;
                b->vel.x = 0;
                b->vel.y = 60.0f;
                b->active = true;
                g->hole_cooldown = 4.0f;
            }
        }

        if (hit.circle >= 0) {
            if (g->bump_combo < 5) g->bump_combo++;     // 连击:分值随本球内命中数递增
            uint32_t got = add_score(g, SCORE_BUMP * g->bump_combo);
            g->flash_circle[hit.circle] = 0.25f;
            popup(g, got, b->pos.x, b->pos.y - 10.0f);
            pb_audio_play(PB_SND_BUMP);
        }
        if (hit.seg >= 0) {
            uint8_t kind = g->table.kind[hit.seg];
            if (kind == PB_SEG_SLING) {
                uint32_t got = add_score(g, SCORE_SLING);
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
                if (g->score > g->high_score) {
                    g->high_score = g->score;
                    g->new_high = true;
                    pb_game_nvs_save(g);
                } else {
                    g->new_high = false;
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
