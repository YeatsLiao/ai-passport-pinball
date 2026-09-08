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

// 记分板三块 LCD 面板(与 pb_art.py 的 PANEL_* 常量一致)。
// BALL 面板加宽:14 号字的 "BALL 3/3" 约 62px,窄面板会溢出到相邻面板上。
#define PANEL_SCORE_X0 5
#define PANEL_SCORE_X1 110
#define PANEL_MULT_X0  114
#define PANEL_MULT_X1  150
#define PANEL_BALL_X0  154
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
    lv_obj_t *hole;                         // 虫洞光环(2 帧换)
    lv_obj_t *flip[2];
    lv_obj_t *shadow;                       // 球影(贴在球下)
    lv_obj_t *ball;
    lv_obj_t *plunger;                      // 蓄力条
    lv_obj_t *lbl_score;
    lv_obj_t *lbl_ball;
    lv_obj_t *lbl_mult;
    lv_obj_t *lbl_msg;
    lv_obj_t *popup_lbl[PB_POPUPS];         // 得分飘字
    lv_obj_t *overlay;                      // 标题/结算/暂停浮层容器(不遮盖记分板)
    lv_obj_t *info_card;                    // 标题/结算卡片
    lv_obj_t *lbl_ov_title;
    lv_obj_t *lbl_ov_sub;                   // 5 槽榜单(规格 §5.1)
    lv_obj_t *lbl_ov_hint;                  // 操作提示行(仅标题页)
    lv_obj_t *lbl_blink;                    // PRESS OK / NEW HIGH SCORE 闪烁行
    lv_obj_t *pause_card;                   // 暂停菜单卡片(金色边框明显区分)
    lv_obj_t *lbl_pause_item[3];
    lv_obj_t *ring_lamp[PB_RING_COUNT];     // outer_circle 点亮态(军衔进度)
    lv_obj_t *upg_lamp[PB_UPG_COUNT];       // bmpr_inc_lights 点亮态(bumper 升级)
    lv_obj_t *lbl_attack;                   // ATTACK 面板:当前 bumper 档位分值
    lv_obj_t *lbl_rank;                     // RANK 面板军衔
    lv_obj_t *well_glow;                    // 引力井吞球闪光
    lv_obj_t *hs_glow;                      // hyperspace 洞吞球闪光

    // 脏值缓存:任何 set_src/set_pos/set_text 都会让 LVGL 标脏重画,
    // 所以只有值真的变了才去碰 LVGL。
    uint32_t last_score;
    uint8_t  last_ball, last_mult_idx, last_bump_tier;
    int      last_ball_x, last_ball_y;
    bool     last_ball_active;
    int8_t   last_flip_frame[2];
    uint8_t  last_lane_bits, last_target_bits, last_bump_bits;
    bool     last_sling_flash;
    char     last_msg[24];
    bool     last_msg_on;
    int      last_plunger_h;
    pb_state_t last_state;
    uint8_t  last_pause_sel;
    uint8_t  last_blink;
    uint8_t  last_hole_frame;
    bool     last_pop_on[PB_POPUPS];
    uint8_t  last_ring_bits, last_ring_lit;
    uint8_t  last_upg_bits;
    uint8_t  last_rank;
    bool     last_well_glow, last_hs_glow;
    uint32_t tick;                          // 帧计数:闪烁动画相位
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

// 洞口吞球闪光:半透明圆形光斑,默认隐藏,吞球过场期间显示
static lv_obj_t *mk_glow(lv_obj_t *parent, float cx, float cy, uint32_t color) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, 18, 18);
    lv_obj_set_pos(o, (int)cx - 9, (int)cy - 9);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_50, 0);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
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

// 灯点亮态:小圆点叠在背景烘焙好的灯座上(座子由 pb_art.py 画,坐标同源)。
static lv_obj_t *mk_lamp(lv_obj_t *parent, int cx, int cy, int d, uint32_t color) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_pos(o, cx - d / 2, cy - d / 2);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}

// ---- 构建 ----

static void build_overlay(lv_obj_t *parent) {
    // 浮层只盖台面,不盖顶部记分板:暂停/结算时当前分数、球数、倍率仍然可见。
    // 之前整屏半透明层把记分板糊成一片,是"层级不清"的主因(差距清单 C1)。
    R.overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(R.overlay);
    lv_obj_set_size(R.overlay, PB_SCREEN_W, PB_SCREEN_H - PB_STATUS_H);
    lv_obj_set_pos(R.overlay, 0, PB_STATUS_H);
    lv_obj_set_style_bg_color(R.overlay, lv_color_hex(0x04060c), 0);
    lv_obj_set_style_bg_opa(R.overlay, LV_OPA_90, 0);

    // 中央不透明卡片:完全压住背后的台面花纹,文字才有对比度。
    // 高度按"标题 20 号 + 提示行 + 5 行榜单 + 闪烁行"逐段预算,互不重叠。
    lv_obj_t *card = lv_obj_create(R.overlay);
    R.info_card = card;
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 216, 180);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0b111e), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x3d5170), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 8, 0);

    R.lbl_ov_title = lv_label_create(card);
    lv_obj_set_width(R.lbl_ov_title, 200);                  // 限宽换行,不溢出卡片
    lv_label_set_long_mode(R.lbl_ov_title, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(R.lbl_ov_title, lv_color_hex(C_SCORE), 0);
    lv_obj_set_style_text_font(R.lbl_ov_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(R.lbl_ov_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(R.lbl_ov_title, LV_ALIGN_TOP_MID, 0, 6);    // 6..34

    // 副标题行:标题页放操作提示(规格 §4 操作方式),结算页放本局总分。
    R.lbl_ov_hint = lv_label_create(card);
    lv_obj_set_width(R.lbl_ov_hint, 200);
    lv_obj_set_style_text_color(R.lbl_ov_hint, lv_color_hex(0x8a97ab), 0);
    lv_obj_set_style_text_font(R.lbl_ov_hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(R.lbl_ov_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(R.lbl_ov_hint, LV_ALIGN_TOP_MID, 0, 36);    // 36..56

    // 5 槽榜单(规格 §5.1):左对齐成表,数字用 %7ld 右靠齐(等宽数字)。
    R.lbl_ov_sub = lv_label_create(card);
    lv_obj_set_size(R.lbl_ov_sub, 150, 100);
    lv_obj_set_style_text_color(R.lbl_ov_sub, lv_color_hex(0xdde8f5), 0);
    lv_obj_set_style_text_font(R.lbl_ov_sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(R.lbl_ov_sub, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_pad_all(R.lbl_ov_sub, 0, 0);
    lv_obj_align(R.lbl_ov_sub, LV_ALIGN_TOP_MID, 0, 56);     // 56..156

    // 底部闪烁行:标题页 PRESS OK / 结算页新纪录提示
    R.lbl_blink = lv_label_create(card);
    lv_obj_set_width(R.lbl_blink, 200);
    lv_obj_set_style_text_font(R.lbl_blink, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(R.lbl_blink, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(R.lbl_blink, LV_ALIGN_BOTTOM_MID, 0, -2);   // 158..178
    lv_obj_add_flag(R.lbl_blink, LV_OBJ_FLAG_HIDDEN);

    // 暂停菜单卡片:金色边框 + 大字选项,与台面小字/信息卡片明显区分。
    lv_obj_t *pc = lv_obj_create(R.overlay);
    R.pause_card = pc;
    lv_obj_remove_style_all(pc);
    lv_obj_set_size(pc, 216, 170);
    lv_obj_align(pc, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(pc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(pc, lv_color_hex(0x0d1020), 0);
    lv_obj_set_style_bg_opa(pc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(pc, lv_color_hex(C_SCORE), 0);
    lv_obj_set_style_border_width(pc, 2, 0);
    lv_obj_set_style_radius(pc, 10, 0);

    lv_obj_t *ttl = lv_label_create(pc);
    lv_obj_set_style_text_color(ttl, lv_color_hex(C_SCORE), 0);
    lv_obj_set_style_text_font(ttl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(ttl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ttl, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(ttl, "PAUSED");

    for (int i = 0; i < 3; i++) {
        lv_obj_t *it = lv_label_create(pc);
        lv_obj_set_size(it, 170, 28);
        lv_obj_align(it, LV_ALIGN_TOP_MID, 0, 44 + i * 32);
        lv_obj_set_style_text_align(it, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(it, &lv_font_montserrat_16, 0);
        lv_obj_set_style_radius(it, 5, 0);
        lv_obj_set_style_bg_color(it, lv_color_hex(C_SCORE), 0);
        lv_obj_set_style_pad_top(it, 5, 0);
        R.lbl_pause_item[i] = it;
    }

    lv_obj_t *hint = lv_label_create(pc);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x8a97ab), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_label_set_text(hint, "UP/DOWN: SELECT  OK: GO");
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

    // 虫洞光环(洞心处,吸入时换亮帧)
    R.hole = mk_img(parent, pb_img_hole[0], pb_hole_pos[0], pb_hole_pos[1]);

    // 挡板:帧 0 = 静止角
    for (int i = 0; i < 2; i++) {
        R.flip[i] = mk_img(parent, pb_img_flip[i][0],
                           (int)g->table.flippers[i].pivot.x + pb_flip_ofs[i][0][0],
                           (int)g->table.flippers[i].pivot.y + pb_flip_ofs[i][0][1]);
    }

    // 球影(先建,压在球下面)+ 球
    R.shadow = mk_img(parent, &pb_img_shadow, 0, 0);
    lv_obj_add_flag(R.shadow, LV_OBJ_FLAG_HIDDEN);
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

    // 台面提示:只能落在信息带 PB_INFO_* 里(背景已烘焙成深色凹槽)。
    // 之前居中叠在徽章上,和 "SPACE CADET" 弧字/行星花纹糊成一团(差距清单 C2)。
    R.lbl_msg = lv_label_create(parent);
    lv_obj_set_size(R.lbl_msg, (int)(PB_INFO_X1 - PB_INFO_X0) - 2,
                    (int)(PB_INFO_Y1 - PB_INFO_Y0));
    lv_obj_set_pos(R.lbl_msg, (int)PB_INFO_X0 + 1, (int)PB_INFO_Y0);
    lv_label_set_long_mode(R.lbl_msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(R.lbl_msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(R.lbl_msg, lv_color_hex(C_MSG), 0);
    lv_obj_set_style_text_font(R.lbl_msg, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_all(R.lbl_msg, 0, 0);
    lv_label_set_text(R.lbl_msg, "");
    lv_obj_add_flag(R.lbl_msg, LV_OBJ_FLAG_HIDDEN);

    // 得分飘字(命中点向上飘 + 渐隐)
    for (int i = 0; i < PB_POPUPS; i++) {
        R.popup_lbl[i] = lv_label_create(parent);
        lv_obj_set_style_text_color(R.popup_lbl[i], lv_color_hex(C_SCORE), 0);
        lv_obj_set_style_text_font(R.popup_lbl[i], &lv_font_montserrat_14, 0);
        lv_obj_add_flag(R.popup_lbl[i], LV_OBJ_FLAG_HIDDEN);
        R.last_pop_on[i] = false;
    }

    // outer_circle 军衔进度环点亮态:5 盏,角度表 150/120/90/60/30 与 pb_art.py
    // 的 paint_ring_lamps 一致(规格 §3)。
    for (int i = 0; i < PB_RING_COUNT; i++) {
        float a = (150.0f - 30.0f * i) * 3.14159265f / 180.0f;
        int lx = (int)(PB_RING_CX + PB_RING_R * cosf(a));
        int ly = (int)(PB_RING_CY - PB_RING_R * sinf(a));
        R.ring_lamp[i] = mk_lamp(parent, lx, ly, 6, C_SCORE);
    }

    // bmpr_inc_lights bumper 升级灯组点亮态:3 盏(规格 §3)。
    for (int i = 0; i < PB_UPG_COUNT; i++) {
        int lx = (int)(PB_UPG_CX + (i - (PB_UPG_COUNT - 1) / 2.0f) * PB_UPG_DX);
        R.upg_lamp[i] = mk_lamp(parent, lx, (int)PB_UPG_CY, 6, C_MSG);
    }

    // ATTACK/RANK 面板数值:背景招牌在 y 202..210,数值必须落在招牌下方。
    // 之前 lbl_fuel 放在 y=205 与 "FUEL" 招牌重叠(差距清单 C4)。
    R.lbl_attack = lv_label_create(parent);
    lv_obj_set_size(R.lbl_attack, 42, 20);
    lv_obj_set_style_text_align(R.lbl_attack, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(R.lbl_attack, lv_color_hex(C_SCORE), 0);
    lv_obj_set_style_text_font(R.lbl_attack, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_all(R.lbl_attack, 0, 0);
    lv_obj_set_pos(R.lbl_attack, 16, 212);
    lv_label_set_text_fmt(R.lbl_attack, "%lu",
                          (unsigned long)pb_bump_score(0));
    R.lbl_rank = lv_label_create(parent);
    lv_obj_set_size(R.lbl_rank, 42, 20);
    lv_obj_set_style_text_align(R.lbl_rank, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(R.lbl_rank, lv_color_hex(C_SCORE), 0);
    lv_obj_set_style_text_font(R.lbl_rank, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_all(R.lbl_rank, 0, 0);
    lv_obj_set_pos(R.lbl_rank, 156, 212);
    lv_label_set_text(R.lbl_rank, pb_rank_name(1));

    // 侧洞吞球闪光(蓝=引力井 金=hyperspace)
    R.well_glow = mk_glow(parent, PB_WELL_X, PB_WELL_Y, 0x6aa0f0);
    R.hs_glow = mk_glow(parent, PB_HS_X, PB_HS_Y, 0xffc860);

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

// 5 槽榜单文本(规格 §5.1/§5.2):空槽显示 -------,本局入榜的那行打 *。
// 数字用 %7ld 右靠齐(montserrat 数字等宽,能对齐成表)。
static void fmt_hs_table(pb_game *g, char *buf, size_t n) {
    size_t off = 0;
    for (int i = 0; i < PB_HS_SLOTS; i++) {
        const char *tail = (i + 1 < PB_HS_SLOTS) ? "\n" : "";
        int w;
        if (g->hs[i] > 0)
            w = snprintf(buf + off, n - off, "%d. %7ld%s%s", i + 1, (long)g->hs[i],
                         g->hs_new == (int8_t)i ? " *" : "", tail);
        else
            w = snprintf(buf + off, n - off, "%d. %7s%s", i + 1, "-------", tail);
        if (w <= 0 || (size_t)w >= n - off) break;
        off += (size_t)w;
    }
}

void pb_render_sync(pb_game *g) {
    const bool force = !R.primed;
    R.tick++;                                   // 闪烁动画相位

    // 分数/球数/倍率:文本变了才重写(每次 set_text_fmt 都会重分配 + 标脏)
    if (force || g->score != R.last_score) {
        R.last_score = g->score;
        lv_label_set_text_fmt(R.lbl_score, "%lu", (unsigned long)g->score);
    }
    if (force || g->ball_num != R.last_ball) {
        R.last_ball = g->ball_num;
        lv_label_set_text_fmt(R.lbl_ball, "BALL %d/%d", g->ball_num, PB_BALLS_TOTAL);
    }
    if (force || g->mult_idx != R.last_mult_idx) {
        R.last_mult_idx = g->mult_idx;
        lv_label_set_text_fmt(R.lbl_mult, "x%lu",
                              (unsigned long)pb_mult_value(g->mult_idx));
    }

    // 球:整数像素坐标变了才移动
    const pb_ball *b = &g->table.world.ball;
    if (b->active != R.last_ball_active) {
        R.last_ball_active = b->active;
        if (b->active) {
            lv_obj_clear_flag(R.ball, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(R.shadow, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(R.ball, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(R.shadow, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (b->active) {
        int bx = (int)b->pos.x + pb_ball_ofs[0];
        int by = (int)b->pos.y + pb_ball_ofs[1];
        if (force || bx != R.last_ball_x || by != R.last_ball_y) {
            R.last_ball_x = bx;
            R.last_ball_y = by;
            lv_obj_set_pos(R.ball, bx, by);
            // 球影贴着球一起挪:两精灵锚点都是相对球心的,直接用偏移差
            lv_obj_set_pos(R.shadow,
                           bx + (pb_shadow_ofs[0] - pb_ball_ofs[0]),
                           by + (pb_shadow_ofs[1] - pb_ball_ofs[1]));
        }
    }

    // 虫洞光环:吸入/弹出期间换亮帧
    int hole_fr = (g->flash_hole > 0 || g->hole_timer > 0) ? 1 : 0;
    if (force || hole_fr != (int)R.last_hole_frame) {
        R.last_hole_frame = (uint8_t)hole_fr;
        lv_image_set_src(R.hole, pb_img_hole[hole_fr]);
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

    // 得分飘字:激活时写文本,存活期内上飘 + 渐隐(寿命 0.8s,与 pb_game.c 一致)
    for (int i = 0; i < PB_POPUPS; i++) {
        const pb_popup *p = &g->popups[i];
        bool on = p->t > 0;
        if (on && !R.last_pop_on[i]) {
            R.last_pop_on[i] = true;
            lv_label_set_text_fmt(R.popup_lbl[i], "+%lu", (unsigned long)p->value);
            lv_obj_clear_flag(R.popup_lbl[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (on) {
            float k = 1.0f - p->t / 0.8f;                   // 0(出生)->1(消散)
            lv_obj_set_pos(R.popup_lbl[i], p->x - 20, (int)(p->y - 8.0f - 16.0f * k));
            static const lv_opa_t fade[5] =
                { LV_OPA_80, LV_OPA_60, LV_OPA_40, LV_OPA_20, LV_OPA_10 };
            int idx = (int)(k * 4.99f);
            if (idx < 0) idx = 0;
            if (idx > 4) idx = 4;
            lv_obj_set_style_text_opa(R.popup_lbl[i], fade[idx], 0);
        } else if (R.last_pop_on[i]) {
            R.last_pop_on[i] = false;
            lv_obj_add_flag(R.popup_lbl[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    // 标题/结算/暂停浮层:状态切换时重建文本
    if (force || g->state != R.last_state) {
        R.last_state = g->state;
        R.last_blink = 0xFF;                    // 闪烁行强制按新状态重建
        bool show_info = (g->state == PB_STATE_TITLE || g->state == PB_STATE_OVER);
        bool show_pause = (g->state == PB_STATE_PAUSE);
        if (show_info || show_pause) {
            lv_obj_clear_flag(R.overlay, LV_OBJ_FLAG_HIDDEN);
            if (show_info) {
                char tbl[80];
                lv_obj_clear_flag(R.info_card, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(R.pause_card, LV_OBJ_FLAG_HIDDEN);
                fmt_hs_table(g, tbl, sizeof tbl);
                lv_label_set_text(R.lbl_ov_sub, tbl);
                lv_obj_clear_flag(R.lbl_ov_hint, LV_OBJ_FLAG_HIDDEN);
                if (g->state == PB_STATE_TITLE) {
                    lv_label_set_text(R.lbl_ov_title, "SPACE PINBALL");
                    lv_label_set_text(R.lbl_ov_hint, "UP/DOWN:FLIP OK:LAUNCH");
                    lv_obj_set_style_text_color(R.lbl_ov_hint,
                                                lv_color_hex(0x8a97ab), 0);
                } else {
                    // 总分放在榜单上方的固定行;当前分数/倍率/球数在顶部记分板
                    // 仍然可见(浮层不再遮盖),所以这里不重复 HIGH 行。
                    lv_label_set_text(R.lbl_ov_title, "GAME OVER");
                    lv_label_set_text_fmt(R.lbl_ov_hint, "SCORE %lu",
                                          (unsigned long)g->score);
                    lv_obj_set_style_text_color(R.lbl_ov_hint,
                                                lv_color_hex(C_SCORE), 0);
                }
            } else {
                // 暂停卡片金色边框 + 大字菜单,和台面/信息卡片一眼区分
                lv_obj_add_flag(R.info_card, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(R.pause_card, LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            lv_obj_add_flag(R.overlay, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // 暂停菜单选中项:金底黑字 vs 透明底白字
    if (force || (g->state == PB_STATE_PAUSE && g->pause_sel != R.last_pause_sel)) {
        R.last_pause_sel = g->pause_sel;
        static const char *const items[3] = { "RESUME", "RESTART", "EXIT TO TITLE" };
        for (int i = 0; i < 3; i++) {
            lv_obj_t *it = R.lbl_pause_item[i];
            lv_label_set_text(it, items[i]);
            bool sel = (i == g->pause_sel);
            lv_obj_set_style_bg_opa(it, sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
            lv_obj_set_style_text_color(it,
                lv_color_hex(sel ? 0x10141e : 0xdde8f5), 0);
        }
    }

    // 闪烁行:标题页 PRESS OK / 结算页新纪录
    if (g->state == PB_STATE_TITLE || g->state == PB_STATE_OVER) {
        uint8_t blink = (uint8_t)((R.tick / 24) & 1);       // ~0.4s 相位
        if (blink != R.last_blink) {
            R.last_blink = blink;
            if (g->state == PB_STATE_TITLE) {
                lv_label_set_text(R.lbl_blink, "PRESS OK TO START");
                lv_obj_set_style_text_color(R.lbl_blink,
                    lv_color_hex(blink ? C_SCORE : 0x303a4c), 0);
            } else if (g->new_high) {
                lv_label_set_text(R.lbl_blink, "NEW HIGH SCORE!");
                lv_obj_set_style_text_color(R.lbl_blink,
                    lv_color_hex(blink ? C_SCORE : 0xf5f5f5), 0);
            } else {
                lv_label_set_text(R.lbl_blink, "PRESS ANY KEY");
                lv_obj_set_style_text_color(R.lbl_blink,
                    lv_color_hex(blink ? 0xdde8f5 : 0x5a6a84), 0);
            }
            lv_obj_clear_flag(R.lbl_blink, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // 军衔进度环 outer_circle 点亮态(规格 §3)
    uint8_t ring_bits = 0;
    for (int i = 0; i < PB_RING_COUNT; i++)
        if (i < g->ring_lit) ring_bits |= (uint8_t)(1u << i);
    if (force || ring_bits != R.last_ring_bits) {
        for (int i = 0; i < PB_RING_COUNT; i++) {
            bool on = (ring_bits >> i) & 1;
            bool was = (R.last_ring_bits >> i) & 1;
            if (force || on != was) {
                if (on) lv_obj_clear_flag(R.ring_lamp[i], LV_OBJ_FLAG_HIDDEN);
                else    lv_obj_add_flag(R.ring_lamp[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        R.last_ring_bits = ring_bits;
    }

    // bmpr_inc_lights 升级灯组(规格 §3):已亮 bump_prog 盏;满组后
    // flash_upg > 0 期间整组闪几下(原版 Message(7, 5.0) 的 flash 语义)。
    uint8_t upg_bits = 0;
    for (int i = 0; i < PB_UPG_COUNT; i++)
        if (i < g->bump_prog) upg_bits |= (uint8_t)(1u << i);
    if (g->flash_upg > 0 && ((R.tick / 8) & 1)) upg_bits = 0;
    if (force || upg_bits != R.last_upg_bits) {
        for (int i = 0; i < PB_UPG_COUNT; i++) {
            bool on = (upg_bits >> i) & 1;
            bool was = (R.last_upg_bits >> i) & 1;
            if (force || on != was) {
                if (on) lv_obj_clear_flag(R.upg_lamp[i], LV_OBJ_FLAG_HIDDEN);
                else    lv_obj_add_flag(R.upg_lamp[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        R.last_upg_bits = upg_bits;
    }

    // ATTACK 面板 = 当前 bumper 档位实际分值(规格 §2.1),RANK = 9 级缩写(§3)。
    if (force || g->ring_lit != R.last_ring_lit || g->bump_tier != R.last_bump_tier
        || g->rank != R.last_rank) {
        R.last_ring_lit = g->ring_lit;
        R.last_bump_tier = g->bump_tier;
        R.last_rank = g->rank;
        lv_label_set_text_fmt(R.lbl_attack, "%lu",
                              (unsigned long)pb_bump_score(g->bump_tier));
        lv_label_set_text(R.lbl_rank, pb_rank_name(g->rank));
    }

    // 侧洞吞球闪光
    bool wg = g->well_timer > 0, hg = g->hs_timer > 0;
    if (force || wg != R.last_well_glow) {
        R.last_well_glow = wg;
        if (wg) lv_obj_clear_flag(R.well_glow, LV_OBJ_FLAG_HIDDEN);
        else    lv_obj_add_flag(R.well_glow, LV_OBJ_FLAG_HIDDEN);
    }
    if (force || hg != R.last_hs_glow) {
        R.last_hs_glow = hg;
        if (hg) lv_obj_clear_flag(R.hs_glow, LV_OBJ_FLAG_HIDDEN);
        else    lv_obj_add_flag(R.hs_glow, LV_OBJ_FLAG_HIDDEN);
    }

    R.primed = true;
}
