// main/pb_render.c —— LVGL 渲染实现(贴图方案)。
//
// 静态台面在构建前由 tools/gen_assets.py 烘焙成一张 240x320 RGB565 位图
// (pb_img_bg,含导轨/塑料件/招牌/记分板边框),运行时只把会动的部件作为
// RGB565A8 精灵贴上去:换帧 = lv_image_set_src,移动 = lv_obj_set_pos。
// 静态台面一帧都不重画,单缓冲 + 40MHz SPI 下脏区只剩精灵本身那一小块。
#include "pb_render.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "pb_assets.h"

// ---- 文本配色(记分板 LCD 面板上的字) ----
#define C_SCORE   0xffd24a
#define C_TEXT    0x9aa7b8
#define C_MSG     0x7fd4ff
#define C_PLUNGER 0xffd24a

// 记分板三块 LCD 面板(与 pb_art.py 的 PANEL_* 常量一致)
#define PANEL_SCORE_X0 5
#define PANEL_SCORE_X1 131
#define PANEL_MULT_X0  136
#define PANEL_MULT_X1  176
#define PANEL_BALL_X0  180
#define PANEL_BALL_X1  235
#define PANEL_Y0       4
#define PANEL_H        18

typedef struct {
    lv_obj_t *scr;
    lv_obj_t *bg;
    lv_obj_t *bump[PB_ART_BUMP_COUNT];
    lv_obj_t *lane[PB_ART_LANE_COUNT];
    lv_obj_t *tgt[PB_ART_TGT_COUNT];
    lv_obj_t *sling[PB_ART_SLING_COUNT];
    lv_obj_t *flip[2];
    lv_obj_t *ball;
    lv_obj_t *plunger;                      // 蓄力条
    lv_obj_t *lbl_score;
    lv_obj_t *lbl_ball;
    lv_obj_t *lbl_mult;
    lv_obj_t *lbl_msg;
    lv_obj_t *overlay;                      // 标题/结算浮层
    lv_obj_t *lbl_ov_title;
    lv_obj_t *lbl_ov_sub;

    // 脏值缓存:任何 set_src/set_pos/set_text 都会让 LVGL 标脏重画,
    // 所以只有值真的变了才去碰 LVGL。
    uint32_t last_score;
    uint8_t  last_ball, last_mult;
    int      last_ball_x, last_ball_y;
    bool     last_ball_active;
    int8_t   last_flip_frame[2];
    uint8_t  last_lane_bits, last_target_bits, last_bump_bits;
    bool     last_sling_flash;
    char     last_msg[24];
    bool     last_msg_on;
    int      last_plunger_h;
    pb_state_t last_state;
    bool     primed;
} pb_render_t;

static pb_render_t R;

// ---- 小工具 ----

static lv_obj_t *mk_img(lv_obj_t *parent, const lv_image_dsc_t *dsc, int x, int y) {
    lv_obj_t *o = lv_image_create(parent);
    lv_image_set_src(o, dsc);
    lv_obj_set_pos(o, x, y);
    return o;
}

static lv_obj_t *mk_panel_label(lv_obj_t *parent, int x0, int x1, lv_text_align_t align,
                                uint32_t color) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_size(l, x1 - x0 - 4, PANEL_H - 2);
    lv_obj_set_pos(l, x0 + 2, PANEL_Y0 + 1);
    lv_obj_set_style_text_align(l, align, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_all(l, 0, 0);
    return l;
}

// ---- 构建 ----

static void build_overlay(lv_obj_t *parent) {
    R.overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(R.overlay);
    lv_obj_set_size(R.overlay, PB_SCREEN_W, PB_SCREEN_H);
    lv_obj_set_pos(R.overlay, 0, 0);
    lv_obj_set_style_bg_color(R.overlay, lv_color_hex(0x060810), 0);
    lv_obj_set_style_bg_opa(R.overlay, LV_OPA_80, 0);

    R.lbl_ov_title = lv_label_create(R.overlay);
    lv_obj_set_style_text_color(R.lbl_ov_title, lv_color_hex(C_SCORE), 0);
    lv_obj_set_style_text_font(R.lbl_ov_title, &lv_font_montserrat_20, 0);
    lv_obj_center(R.lbl_ov_title);

    R.lbl_ov_sub = lv_label_create(R.overlay);
    lv_obj_set_style_text_color(R.lbl_ov_sub, lv_color_hex(C_TEXT), 0);
    lv_obj_set_style_text_font(R.lbl_ov_sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(R.lbl_ov_sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(R.lbl_ov_sub, LV_ALIGN_CENTER, 0, 34);
}

void pb_render_build(pb_game *g, lv_obj_t *parent) {
    (void)g;
    R.scr = parent;
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x060810), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(parent, 0, 0);

    // 静态台面(整屏一张图,建好后永远不动)
    R.bg = mk_img(parent, &pb_img_bg, 0, 0);

    // 弹弓橡皮筋 / 掉落目标 / 车道灯芯 / bumper 帽:位置由生成器按几何导出
    for (int i = 0; i < PB_ART_SLING_COUNT; i++)
        R.sling[i] = mk_img(parent, pb_img_sling[i][0], pb_sling_pos[i][0], pb_sling_pos[i][1]);
    for (int i = 0; i < PB_ART_TGT_COUNT; i++)
        R.tgt[i] = mk_img(parent, pb_img_tgt[0], pb_tgt_pos[i][0], pb_tgt_pos[i][1]);
    for (int i = 0; i < PB_ART_LANE_COUNT; i++)
        R.lane[i] = mk_img(parent, pb_img_lane[0], pb_lane_pos[i][0], pb_lane_pos[i][1]);
    for (int i = 0; i < PB_ART_BUMP_COUNT; i++)
        R.bump[i] = mk_img(parent, pb_img_bump[i][0], pb_bump_pos[i][0], pb_bump_pos[i][1]);

    // 挡板:帧 0 = 静止角
    for (int i = 0; i < 2; i++) {
        R.flip[i] = mk_img(parent, pb_img_flip[i][0],
                           (int)g->table.flippers[i].pivot.x + pb_flip_ofs[i][0][0],
                           (int)g->table.flippers[i].pivot.y + pb_flip_ofs[i][0][1]);
    }

    // 球
    R.ball = mk_img(parent, &pb_img_ball, 0, 0);
    lv_obj_add_flag(R.ball, LV_OBJ_FLAG_HIDDEN);

    // 蓄力条(发球道底部,自下而上充能)
    R.plunger = lv_obj_create(parent);
    lv_obj_remove_style_all(R.plunger);
    lv_obj_set_size(R.plunger, 6, 0);
    lv_obj_set_pos(R.plunger, 221, 304);
    lv_obj_set_style_bg_color(R.plunger, lv_color_hex(C_PLUNGER), 0);
    lv_obj_set_style_bg_opa(R.plunger, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(R.plunger, 2, 0);
    lv_obj_add_flag(R.plunger, LV_OBJ_FLAG_HIDDEN);

    // 记分板:三块 LCD 面板上的字
    R.lbl_score = mk_panel_label(parent, PANEL_SCORE_X0, PANEL_SCORE_X1,
                                 LV_TEXT_ALIGN_RIGHT, C_SCORE);
    lv_label_set_text(R.lbl_score, "0");
    R.lbl_mult = mk_panel_label(parent, PANEL_MULT_X0, PANEL_MULT_X1,
                                LV_TEXT_ALIGN_CENTER, C_TEXT);
    R.lbl_ball = mk_panel_label(parent, PANEL_BALL_X0, PANEL_BALL_X1,
                                LV_TEXT_ALIGN_CENTER, C_TEXT);

    // 台面提示(叠在中央徽章上,对标原版把任务状态放在台面中心)
    R.lbl_msg = lv_label_create(parent);
    lv_obj_set_style_text_color(R.lbl_msg, lv_color_hex(C_MSG), 0);
    lv_obj_set_style_text_font(R.lbl_msg, &lv_font_montserrat_14, 0);
    lv_obj_align(R.lbl_msg, LV_ALIGN_CENTER, 0, 26);
    lv_label_set_text(R.lbl_msg, "");
    lv_obj_add_flag(R.lbl_msg, LV_OBJ_FLAG_HIDDEN);

    build_overlay(parent);
}

// ---- 每帧同步 ----

// 挡板角度 -> 精灵帧号。rest/raised 之间线性插值,量化到 PB_ART_FLIP_FRAMES 帧。
static int flipper_frame(const pb_flipper *f) {
    float span = f->raised - f->rest;
    if (fabsf(span) < 1e-4f) return 0;
    float t = (f->angle - f->rest) / span;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return (int)(t * (PB_ART_FLIP_FRAMES - 1) + 0.5f);
}

// 只在变化时写 LVGL:下面 sync 里全部走 "比对缓存 -> 变了才调 setter" 的路子

void pb_render_sync(pb_game *g) {
    const bool force = !R.primed;

    // 分数/球数/倍率:文本变了才重写(每次 set_text_fmt 都会重分配 + 标脏)
    if (force || g->score != R.last_score) {
        R.last_score = g->score;
        lv_label_set_text_fmt(R.lbl_score, "%lu", (unsigned long)g->score);
    }
    if (force || g->ball_num != R.last_ball) {
        R.last_ball = g->ball_num;
        lv_label_set_text_fmt(R.lbl_ball, "BALL %d/%d", g->ball_num, PB_BALLS_TOTAL);
    }
    if (force || g->mult != R.last_mult) {
        R.last_mult = g->mult;
        lv_label_set_text_fmt(R.lbl_mult, "x%d", g->mult);
    }

    // 球:整数像素坐标变了才移动
    const pb_ball *b = &g->table.world.ball;
    if (b->active != R.last_ball_active) {
        R.last_ball_active = b->active;
        if (b->active) lv_obj_clear_flag(R.ball, LV_OBJ_FLAG_HIDDEN);
        else           lv_obj_add_flag(R.ball, LV_OBJ_FLAG_HIDDEN);
    }
    if (b->active) {
        int bx = (int)b->pos.x + pb_ball_ofs[0];
        int by = (int)b->pos.y + pb_ball_ofs[1];
        if (force || bx != R.last_ball_x || by != R.last_ball_y) {
            R.last_ball_x = bx;
            R.last_ball_y = by;
            lv_obj_set_pos(R.ball, bx, by);
        }
    }

    // 挡板:帧号变了才换 src + 挪位置(静止时完全不碰 LVGL)
    for (int i = 0; i < 2; i++) {
        int fr = flipper_frame(&g->table.flippers[i]);
        if (force || fr != R.last_flip_frame[i]) {
            R.last_flip_frame[i] = (int8_t)fr;
            lv_image_set_src(R.flip[i], pb_img_flip[i][fr]);
            lv_obj_set_pos(R.flip[i],
                           (int)g->table.flippers[i].pivot.x + pb_flip_ofs[i][fr][0],
                           (int)g->table.flippers[i].pivot.y + pb_flip_ofs[i][fr][1]);
        }
    }

    // 目标组/车道灯/bumper 闪光/弹弓闪光:用位图比对,只改变化的那一个
    uint8_t tgt_bits = 0, lane_bits = 0, bump_bits = 0;
    for (int i = 0; i < PB_TARGET_COUNT; i++) if (g->target_down[i]) tgt_bits |= (uint8_t)(1u << i);
    for (int i = 0; i < PB_LANE_COUNT; i++)   if (g->lane_lit[i])    lane_bits |= (uint8_t)(1u << i);
    for (int i = 0; i < PB_ART_BUMP_COUNT; i++)
        if (g->flash_circle[i] > 0) bump_bits |= (uint8_t)(1u << i);

    if (force || tgt_bits != R.last_target_bits) {
        for (int i = 0; i < PB_TARGET_COUNT; i++) {
            bool now = (tgt_bits >> i) & 1;
            bool was = (R.last_target_bits >> i) & 1;
            if (force || now != was) lv_image_set_src(R.tgt[i], pb_img_tgt[now ? 1 : 0]);
        }
        R.last_target_bits = tgt_bits;
    }
    if (force || lane_bits != R.last_lane_bits) {
        for (int i = 0; i < PB_LANE_COUNT; i++) {
            bool now = (lane_bits >> i) & 1;
            bool was = (R.last_lane_bits >> i) & 1;
            if (force || now != was) lv_image_set_src(R.lane[i], pb_img_lane[now ? 1 : 0]);
        }
        R.last_lane_bits = lane_bits;
    }
    if (force || bump_bits != R.last_bump_bits) {
        for (int i = 0; i < PB_ART_BUMP_COUNT; i++) {
            bool now = (bump_bits >> i) & 1;
            bool was = (R.last_bump_bits >> i) & 1;
            if (force || now != was) lv_image_set_src(R.bump[i], pb_img_bump[i][now ? 1 : 0]);
        }
        R.last_bump_bits = bump_bits;
    }
    bool sling_now = g->flash_sling > 0;
    if (force || sling_now != R.last_sling_flash) {
        R.last_sling_flash = sling_now;
        for (int side = 0; side < PB_ART_SLING_COUNT; side++)
            lv_image_set_src(R.sling[side], pb_img_sling[side][sling_now ? 1 : 0]);
    }

    // 蓄力条:高度取整后比对
    int ph = (g->state == PB_STATE_LAUNCH && g->launch_power > 0)
             ? (int)(60 * g->launch_power) : -1;
    if (force || ph != R.last_plunger_h) {
        R.last_plunger_h = ph;
        if (ph < 0) {
            lv_obj_add_flag(R.plunger, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(R.plunger, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_size(R.plunger, 6, ph);
            lv_obj_set_pos(R.plunger, 221, 304 - ph);
        }
    }

    // 台面提示
    bool msg_on = (g->msg_timer > 0 && g->msg[0]);
    if (force || msg_on != R.last_msg_on || (msg_on && strcmp(g->msg, R.last_msg) != 0)) {
        R.last_msg_on = msg_on;
        if (msg_on) {
            snprintf(R.last_msg, sizeof(R.last_msg), "%s", g->msg);
            lv_label_set_text(R.lbl_msg, g->msg);
            lv_obj_clear_flag(R.lbl_msg, LV_OBJ_FLAG_HIDDEN);
        } else {
            R.last_msg[0] = '\0';
            lv_obj_add_flag(R.lbl_msg, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // 标题/结算浮层:只在状态切换时重建文本
    if (force || g->state != R.last_state) {
        R.last_state = g->state;
        if (g->state == PB_STATE_TITLE || g->state == PB_STATE_OVER) {
            lv_obj_clear_flag(R.overlay, LV_OBJ_FLAG_HIDDEN);
            if (g->state == PB_STATE_TITLE) {
                lv_label_set_text(R.lbl_ov_title, "SPACE PINBALL");
                lv_label_set_text_fmt(R.lbl_ov_sub,
                    "HIGH %lu\nL=LEFT  R=RIGHT\nHOLD OK TO LAUNCH",
                    (unsigned long)g->high_score);
            } else {
                lv_label_set_text(R.lbl_ov_title, "GAME OVER");
                lv_label_set_text_fmt(R.lbl_ov_sub, "SCORE %lu\nHIGH %lu\nPRESS ANY KEY",
                                      (unsigned long)g->score, (unsigned long)g->high_score);
            }
        } else {
            lv_obj_add_flag(R.overlay, LV_OBJ_FLAG_HIDDEN);
        }
    }

    R.primed = true;
}
