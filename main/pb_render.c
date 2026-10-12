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
#define C_TEXT    0xbcd2ec   // 弱化色:UI 盘点 #1 倍率/球数用浅蓝白,和分数拉开权重
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
    lv_obj_t *lbl_score_sh;                 // 分数 1px 黑色叠影(LVGL 无原生描边)
    lv_obj_t *lbl_ball;
    lv_obj_t *lbl_mult;
    lv_obj_t *lbl_msg;
    lv_obj_t *popup_lbl[PB_POPUPS];         // 得分飘字
    lv_obj_t *overlay;                      // 标题/结算/暂停/设置/榜单浮层容器(不遮盖记分板)
    lv_obj_t *lbl_batt;                     // 右上角电量角标(overlay 子,随浮层显隐)
    lv_obj_t *info_card;                    // 结算页卡片(GAME OVER + 总分 + 榜单)
    lv_obj_t *lbl_ov_sub;                   // 5 槽榜单(规格 §5.1)
    lv_obj_t *lbl_ov_hint;                  // 结算页本局总分行
    lv_obj_t *lbl_blink;                    // PRESS ANY KEY / NEW HIGH SCORE 闪烁行
    lv_obj_t *menu_card;                    // 主菜单卡片(标题 + 4 菜单项)
    lv_obj_t *lbl_menu_item[PB_MENU_ROWS];  // START/HOW TO PLAY/SETTINGS/HIGH SCORES
    lv_obj_t *hs_card;                      // 排行榜卡片
    lv_obj_t *lbl_hs_table;                 // 5 槽榜单
    lv_obj_t *pause_card;                   // 暂停菜单卡片
    lv_obj_t *lbl_pause_item[4];            // RESUME/RESTART/EXIT/SETTINGS
    lv_obj_t *settings_card;                // 设置页卡片
    lv_obj_t *lbl_set_row[PB_SET_ROWS];     // 可选行:VOL/BLU/BACK
    lv_obj_t *lbl_set_batt;                 // 电量行(只读,不可选)
    lv_obj_t *lbl_set_hint;                 // 设置页底部操作提示行
    lv_obj_t *guide_card;                   // 玩法图鉴卡片(图形化规则,3 页)
    lv_obj_t *lbl_guide_page;               // 页码指示 "1/3"
    lv_obj_t *guide_page[PB_GUIDE_PAGES];   // 每页一个容器,按页码显隐
    lv_obj_t *ring_lamp[PB_RING_COUNT];     // outer_circle 点亮态(军衔进度)
    lv_obj_t *upg_lamp[PB_UPG_COUNT];       // bmpr_inc_lights 点亮态(bumper 升级)
    lv_obj_t *star_lamp[PB_STAR_COUNT];     // 右道星标点亮态
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
    uint8_t  last_menu_sel;
    uint8_t  last_guide_page;
    uint8_t  last_set_sel;
    uint8_t  last_set_edit;
    uint8_t  last_set_vol, last_set_blu;
    int      last_set_batt;
    int      last_batt;
    uint8_t  last_blink;
    uint8_t  last_hole_frame;
    bool     last_pop_on[PB_POPUPS];
    uint8_t  last_ring_bits, last_ring_lit;
    uint8_t  last_upg_bits;
    uint8_t  last_star_bits;
    uint8_t  last_rank;
    int      panel_flash;                   // 升档/升衔时面板闪白计数(tick)
    bool     last_pf;
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

// ---- 系统 UI 设计规范(街机 HUD 风,全部浮层卡片唯一真源)----
// 字号梯度(受 sdkconfig 限制只有 14/16/20 三档,恰好对应三级层级):
//   F_TITLE 20 = 卡片主标题 / F_BODY 16 = 菜单项与正文 / F_SMALL 14 = 提示、数值、次级信息
// 颜色一律取 UI_* 令牌(值复用台面既有 C_SCORE/C_MSG/C_TEXT),禁止就地写 hex。
#define F_TITLE      (&lv_font_montserrat_20)
#define F_BODY       (&lv_font_montserrat_16)
#define F_SMALL      (&lv_font_montserrat_14)
#define UI_GOLD      C_SCORE     // 主标题 / 选中高亮底 / 分值
#define UI_CYAN      C_MSG       // 功能页标题(设置/图鉴)/ 页码
#define UI_TEXT      0xdde8f5    // 正文
#define UI_DIM       0x8a97ab    // 底部提示 / 次级信息
#define UI_SEL_FG    0x0b111e    // 选中行文字(深底配金)
#define UI_PANEL_BG  0x0b111e    // 卡片底
#define UI_LINE      0x3d5170    // 卡片边框 / 阶梯连线
#define UI_DIVIDER   0x22304a    // 标题下分隔线

// 卡片几何:统一 216x236 居中,同一垂直节奏——
//   标题 y10(高24) → 分隔线 y38 → 内容区 46..210 → 提示行贴底 -8
//   可选行 30px 高,行距 36;行宽 190 居中,圆角 6,垂直居中靠 pad_top 补差。
#define CARD_W       216
#define CARD_H       236
#define CARD_PAD     14          // 卡片内容左右内边距(内容宽 188)
#define CARD_ROW_W   190
#define CARD_ROW_H   30
#define CARD_ROW_PITCH 36        // 可选行起点 y=46,依次 +36
#define CARD_TITLE_Y 10
#define CARD_DIV_Y   38
#define CARD_HINT_DY 8           // 提示行距卡片底边

// 卡片显隐统一入口(本工程 LVGL 版本无 lv_obj_set_hidden)。
static void card_show(lv_obj_t *c, bool on) {
    if (on) lv_obj_clear_flag(c, LV_OBJ_FLAG_HIDDEN);
    else    lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
}

// 统一面板卡片:全部系统页共用尺寸/圆角/底色/边框。
static lv_obj_t *mk_panel(lv_obj_t *parent) {
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, CARD_W, CARD_H);
    lv_obj_align(c, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(c, lv_color_hex(UI_PANEL_BG), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(UI_LINE), 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_radius(c, 10, 0);
    return c;
}

// 卡片主标题:20 号居中,颜色按页种区分(金=游戏系 / 青=系统系)。
static lv_obj_t *mk_panel_title(lv_obj_t *card, const char *txt, uint32_t color) {
    lv_obj_t *t = lv_label_create(card);
    lv_obj_set_width(t, CARD_W - 2 * CARD_PAD);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, CARD_TITLE_Y);
    lv_obj_set_style_text_color(t, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(t, F_TITLE, 0);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(t, txt);
    return t;
}

// 标题下分隔线:统一 y=38,宽 = 内容宽。
static void mk_panel_divider(lv_obj_t *card) {
    lv_obj_t *d = lv_obj_create(card);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, CARD_W - 2 * CARD_PAD, 1);
    lv_obj_align(d, LV_ALIGN_TOP_MID, 0, CARD_DIV_Y);
    lv_obj_set_style_bg_color(d, lv_color_hex(UI_DIVIDER), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
}

// 底部操作提示:14 号弱化色,固定贴卡片底边(全页同基线)。
// 宽度取 LV_SIZE_CONTENT:提示行永远保持单行——固定宽度下最长文案(如设置页
// "UP/DOWN: SELECT   OK: EDIT")会换行并向上生长,压到上一行(BACK)。单行不向上扩展。
static lv_obj_t *mk_panel_hint(lv_obj_t *card, const char *txt) {
    lv_obj_t *h = lv_label_create(card);
    lv_obj_set_width(h, LV_SIZE_CONTENT);            // 自动宽度→恒定单行,不会换行上生长
    lv_obj_align(h, LV_ALIGN_BOTTOM_MID, 0, -CARD_HINT_DY);
    lv_obj_set_style_text_color(h, lv_color_hex(UI_DIM), 0);
    lv_obj_set_style_text_font(h, F_SMALL, 0);
    lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(h, txt);
    return h;
}

// 可选行(主菜单/暂停/设置共用):16 号居中,190x30 圆角 6,
// 选中态由 sync 统一刷成金底黑字。pad 6 = (30 - 字高~17)/2,保证垂直居中。
static lv_obj_t *mk_sel_row(lv_obj_t *card, int y) {
    lv_obj_t *it = lv_label_create(card);
    lv_obj_set_size(it, CARD_ROW_W, CARD_ROW_H);
    lv_obj_align(it, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_text_align(it, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(it, F_BODY, 0);
    lv_obj_set_style_radius(it, 6, 0);
    lv_obj_set_style_bg_color(it, lv_color_hex(UI_GOLD), 0);
    lv_obj_set_style_pad_top(it, 6, 0);
    lv_obj_set_style_pad_bottom(it, 0, 0);
    return it;
}

// 选中态刷色(主菜单/暂停/设置三处共用同一高亮样式)。
static void sel_row_style(lv_obj_t *it, bool sel) {
    lv_obj_set_style_bg_opa(it, sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(it,
        lv_color_hex(sel ? UI_SEL_FG : UI_TEXT), 0);
}

// ---- 玩法图鉴:图形化规则页(色块/阶梯/键位图,不堆文字) ----

// 小色块图标。颜色代表台面元素身份(图例专用,非 UI 框架色)。
static lv_obj_t *guide_swatch(lv_obj_t *page, int x, int y, int d, uint32_t color, bool circle) {
    lv_obj_t *o = lv_obj_create(page);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_radius(o, circle ? LV_RADIUS_CIRCLE : 3, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

// 图鉴页内小标题(青色 16 号,位于分隔线下方 y=44)。
static void guide_subtitle(lv_obj_t *page, const char *txt) {
    lv_obj_t *t = lv_label_create(page);
    lv_obj_set_width(t, CARD_W - 2 * CARD_PAD);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_set_style_text_color(t, lv_color_hex(UI_CYAN), 0);
    lv_obj_set_style_text_font(t, F_BODY, 0);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(t, txt);
}

// 得分表一行:左色块 + 元素名(14 正文,固定列宽不越界)+ 右对齐分值(14 金)。
// 名字/分值各占固定列,靠长模式 CLIP 兜底,保证任何文案都不横向重叠。
#define GUIDE_VAL_X  132   // 分值列左边界(右对齐到内容右边)
static void guide_score_row(lv_obj_t *page, int y, uint32_t color, bool circle,
                            const char *name, const char *val) {
    guide_swatch(page, CARD_PAD, y + 3, 11, color, circle);
    lv_obj_t *n = lv_label_create(page);
    lv_obj_set_pos(n, CARD_PAD + 18, y);
    lv_obj_set_width(n, GUIDE_VAL_X - 6 - (CARD_PAD + 18));   // 名字列固定宽
    lv_label_set_long_mode(n, LV_LABEL_LONG_CLIP);            // 超长裁切,绝不压到分值
    lv_obj_set_style_text_color(n, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_text_font(n, F_SMALL, 0);
    lv_label_set_text(n, name);
    lv_obj_t *v = lv_label_create(page);
    lv_obj_set_pos(v, GUIDE_VAL_X, y);
    lv_obj_set_width(v, CARD_W - CARD_PAD - GUIDE_VAL_X);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(v, lv_color_hex(UI_GOLD), 0);
    lv_obj_set_style_text_font(v, F_SMALL, 0);
    lv_label_set_text(v, val);
}

// 升级阶梯:标题 + 连线串 n 个节点,每节点下标数值。内容宽内均分。
static void guide_ladder(lv_obj_t *page, int y, const char *title,
                         const char *const *nodes, int n, uint32_t color) {
    lv_obj_t *t = lv_label_create(page);
    lv_obj_set_pos(t, CARD_PAD, y);
    lv_obj_set_style_text_color(t, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_text_font(t, F_SMALL, 0);
    lv_label_set_text(t, title);

    int first_cx = CARD_PAD + 16, last_cx = CARD_W - CARD_PAD - 16;
    int span = n > 1 ? (last_cx - first_cx) / (n - 1) : 0;
    int cy = y + 20;                              // 节点圆心行
    lv_obj_t *line = lv_obj_create(page);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, last_cx - first_cx, 2);
    lv_obj_set_pos(line, first_cx, cy - 1);
    lv_obj_set_style_bg_color(line, lv_color_hex(UI_LINE), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    for (int i = 0; i < n; i++) {
        int cx = first_cx + span * i;
        guide_swatch(page, cx - 5, cy - 5, 10, color, true);
        lv_obj_t *lb = lv_label_create(page);
        lv_obj_set_width(lb, 44);
        lv_obj_set_style_text_align(lb, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(lb, lv_color_hex(UI_GOLD), 0);
        lv_obj_set_style_text_font(lb, F_SMALL, 0);
        lv_obj_set_pos(lb, cx - 22, cy + 7);
        lv_label_set_text(lb, nodes[i]);
    }
}

// 键位图一行:键名徽标(描边方块) + 箭头 + 作用文字。
static void guide_key_row(lv_obj_t *page, int y, const char *key, const char *desc) {
    lv_obj_t *chip = lv_obj_create(page);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, 64, 20);
    lv_obj_set_pos(chip, CARD_PAD, y);
    lv_obj_set_style_radius(chip, 4, 0);
    lv_obj_set_style_border_color(chip, lv_color_hex(UI_GOLD), 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_bg_color(chip, lv_color_hex(UI_PANEL_BG), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_t *kt = lv_label_create(chip);
    lv_obj_center(kt);
    lv_obj_set_style_text_color(kt, lv_color_hex(UI_GOLD), 0);
    lv_obj_set_style_text_font(kt, F_SMALL, 0);
    lv_label_set_text(kt, key);

    lv_obj_t *ar = lv_label_create(page);
    lv_obj_set_pos(ar, CARD_PAD + 70, y + 2);
    lv_obj_set_style_text_color(ar, lv_color_hex(UI_DIM), 0);
    lv_obj_set_style_text_font(ar, F_SMALL, 0);
    lv_label_set_text(ar, "\u2192");            // →

    lv_obj_t *d = lv_label_create(page);
    lv_obj_set_pos(d, CARD_PAD + 86, y + 2);
    lv_obj_set_style_text_color(d, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_text_font(d, F_SMALL, 0);
    lv_label_set_text(d, desc);
}

static lv_obj_t *guide_make_page(lv_obj_t *card) {
    lv_obj_t *pg = lv_obj_create(card);
    lv_obj_remove_style_all(pg);
    lv_obj_set_size(pg, CARD_W, CARD_H);
    lv_obj_set_pos(pg, 0, 0);
    lv_obj_clear_flag(pg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pg, LV_OBJ_FLAG_HIDDEN);
    return pg;
}

static void build_guide(lv_obj_t *parent) {
    R.guide_card = mk_panel(parent);
    mk_panel_title(R.guide_card, "HOW TO PLAY", UI_GOLD);
    mk_panel_divider(R.guide_card);
    mk_panel_hint(R.guide_card, "LEFT/RIGHT: PAGE   OK: BACK");

    // 页码角标(右上,14 青):下移到副标题同一行,不再与主标题"HOW TO PLAY"挤同一行。
    R.lbl_guide_page = lv_label_create(R.guide_card);
    lv_obj_align(R.lbl_guide_page, LV_ALIGN_TOP_RIGHT, -CARD_PAD, CARD_DIV_Y + 6);
    lv_obj_set_style_text_color(R.lbl_guide_page, lv_color_hex(UI_CYAN), 0);
    lv_obj_set_style_text_font(R.lbl_guide_page, F_SMALL, 0);
    lv_label_set_text(R.lbl_guide_page, "1/3");

    // 页 0:得分表(分值镜像 pb_game.c 的 §2 表;行高 24 从 y=64 起)
    lv_obj_t *p0 = guide_make_page(R.guide_card);
    R.guide_page[0] = p0;
    guide_subtitle(p0, "SCORING");
    guide_score_row(p0, 64,  0xffd24a, true,  "BUMPERS",      "500-2000");
    guide_score_row(p0, 88,  0x7fd4ff, false, "LANES",        "2,000");
    guide_score_row(p0, 112, 0xff6b6b, false, "TARGETS",      "500 / 1500");
    guide_score_row(p0, 136, 0xb07bff, true,  "BLACK HOLE",   "20,000");
    guide_score_row(p0, 160, 0x4fd6ff, true,  "GRAVITY WELL", "50,000");
    guide_score_row(p0, 184, 0x6dffb0, false, "HYPERSPACE",   "10K-150K");

    // 页 1:升级阶梯(bumper 档 + 倍率档)
    lv_obj_t *p1 = guide_make_page(R.guide_card);
    R.guide_page[1] = p1;
    guide_subtitle(p1, "UPGRADES");
    static const char *const bump_nodes[4] = { "500", "1K", "1.5K", "2K" };
    static const char *const mult_nodes[5] = { "x1", "x2", "x3", "x5", "x10" };
    guide_ladder(p1, 64, "ATTACK BUMPERS", bump_nodes, 4, 0xffd24a);
    guide_ladder(p1, 130, "MULTIPLIER", mult_nodes, 5, 0x7fd4ff);

    // 页 2:键位与目标
    lv_obj_t *p2 = guide_make_page(R.guide_card);
    R.guide_page[2] = p2;
    guide_subtitle(p2, "CONTROLS");
    guide_key_row(p2, 64,  "UP",      "LEFT FLIPPER");
    guide_key_row(p2, 90,  "DOWN",    "RIGHT FLIPPER");
    guide_key_row(p2, 116, "OK",      "LAUNCH / CHARGE");
    guide_key_row(p2, 142, "HOLD OK", "PAUSE MENU");
    lv_obj_t *note = lv_label_create(p2);
    lv_obj_set_width(note, CARD_W - 2 * CARD_PAD);
    lv_obj_align(note, LV_ALIGN_TOP_MID, 0, 170);
    lv_obj_set_style_text_color(note, lv_color_hex(UI_DIM), 0);
    lv_obj_set_style_text_font(note, F_SMALL, 0);
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(note, "LIT LANES + TARGETS UPGRADE");
}

static void build_overlay(lv_obj_t *parent) {
    // 浮层只盖台面,不盖顶部记分板:暂停/结算时当前分数、球数、倍率仍然可见。
    R.overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(R.overlay);
    lv_obj_set_size(R.overlay, PB_SCREEN_W, PB_SCREEN_H - PB_STATUS_H);
    lv_obj_set_pos(R.overlay, 0, PB_STATUS_H);
    lv_obj_set_style_bg_color(R.overlay, lv_color_hex(0x04060c), 0);
    lv_obj_set_style_bg_opa(R.overlay, LV_OPA_90, 0);

    // 右上角电量角标:overlay 子,最后创建保证置顶;卡片居中下移,不重叠。
    R.lbl_batt = lv_label_create(R.overlay);
    lv_obj_set_style_text_color(R.lbl_batt, lv_color_hex(UI_DIM), 0);
    lv_obj_set_style_text_font(R.lbl_batt, F_SMALL, 0);
    lv_obj_align(R.lbl_batt, LV_ALIGN_TOP_RIGHT, -6, 4);
    lv_label_set_text(R.lbl_batt, "");

    // 可选行统一垂直节奏:起 y=46,行距 CARD_ROW_PITCH(36)。
    // ---- 主菜单卡片:标题 + 4 项(START/HOW TO PLAY/SETTINGS/HIGH SCORES)----
    R.menu_card = mk_panel(R.overlay);
    mk_panel_title(R.menu_card, "SPACE PINBALL", UI_GOLD);
    mk_panel_divider(R.menu_card);
    for (int i = 0; i < PB_MENU_ROWS; i++)
        R.lbl_menu_item[i] = mk_sel_row(R.menu_card, 46 + i * CARD_ROW_PITCH);
    mk_panel_hint(R.menu_card, "UP/DOWN: SELECT   OK: GO");

    // ---- 结算卡片(GAME OVER):标题 + 本局总分 + 5 槽榜单 + 闪烁行 ----
    R.info_card = mk_panel(R.overlay);
    mk_panel_title(R.info_card, "GAME OVER", UI_GOLD);
    mk_panel_divider(R.info_card);

    R.lbl_ov_hint = lv_label_create(R.info_card);            // 本局总分
    lv_obj_set_width(R.lbl_ov_hint, CARD_W - 2 * CARD_PAD);
    lv_obj_align(R.lbl_ov_hint, LV_ALIGN_TOP_MID, 0, 46);
    lv_obj_set_style_text_color(R.lbl_ov_hint, lv_color_hex(UI_GOLD), 0);
    lv_obj_set_style_text_font(R.lbl_ov_hint, F_BODY, 0);
    lv_obj_set_style_text_align(R.lbl_ov_hint, LV_TEXT_ALIGN_CENTER, 0);

    R.lbl_ov_sub = lv_label_create(R.info_card);             // 5 槽榜单
    lv_obj_set_size(R.lbl_ov_sub, CARD_W - 2 * CARD_PAD, 90);
    lv_obj_align(R.lbl_ov_sub, LV_ALIGN_TOP_MID, 0, 74);
    lv_obj_set_style_text_color(R.lbl_ov_sub, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_text_font(R.lbl_ov_sub, F_SMALL, 0);
    lv_obj_set_style_text_align(R.lbl_ov_sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_all(R.lbl_ov_sub, 0, 0);

    R.lbl_blink = lv_label_create(R.info_card);              // PRESS ANY KEY / NEW HIGH SCORE
    lv_obj_set_width(R.lbl_blink, CARD_W - 2 * CARD_PAD);
    lv_obj_align(R.lbl_blink, LV_ALIGN_BOTTOM_MID, 0, -CARD_HINT_DY);
    lv_obj_set_style_text_font(R.lbl_blink, F_SMALL, 0);
    lv_obj_set_style_text_align(R.lbl_blink, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(R.lbl_blink, LV_OBJ_FLAG_HIDDEN);

    // ---- 排行榜卡片:标题 + 5 槽榜单 + 提示 ----
    R.hs_card = mk_panel(R.overlay);
    mk_panel_title(R.hs_card, "HIGH SCORES", UI_GOLD);
    mk_panel_divider(R.hs_card);
    R.lbl_hs_table = lv_label_create(R.hs_card);
    lv_obj_set_size(R.lbl_hs_table, CARD_W - 2 * CARD_PAD, 100);
    lv_obj_align(R.lbl_hs_table, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_text_color(R.lbl_hs_table, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_text_font(R.lbl_hs_table, F_SMALL, 0);
    lv_obj_set_style_text_align(R.lbl_hs_table, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_all(R.lbl_hs_table, 0, 0);
    mk_panel_hint(R.hs_card, "OK: BACK");

    // ---- 暂停菜单卡片:标题 + 4 项(RESUME/RESTART/EXIT/SETTINGS)----
    R.pause_card = mk_panel(R.overlay);
    mk_panel_title(R.pause_card, "PAUSED", UI_GOLD);
    mk_panel_divider(R.pause_card);
    for (int i = 0; i < 4; i++)
        R.lbl_pause_item[i] = mk_sel_row(R.pause_card, 46 + i * CARD_ROW_PITCH);
    mk_panel_hint(R.pause_card, "UP/DOWN: SELECT   OK: GO");

    // ---- 设置页卡片:标题 + 音量/背光两可选行 + 电量只读行 + 返回行 ----
    // 四个行槽位等间距(间距 38,行高 30→间隙 8px,不再贴挤):
    //   VOL(46) / BLU(84) / BATTERY(122 只读) / BACK(160)。
    R.settings_card = mk_panel(R.overlay);
    mk_panel_title(R.settings_card, "SETTINGS", UI_GOLD);
    mk_panel_divider(R.settings_card);
    static const int SET_ROW_Y[PB_SET_ROWS] = { 46, 84, 160 };
    for (int i = 0; i < PB_SET_ROWS; i++)
        R.lbl_set_row[i] = mk_sel_row(R.settings_card, SET_ROW_Y[i]);

    R.lbl_set_batt = lv_label_create(R.settings_card);       // 只读,弱化小字
    lv_obj_set_size(R.lbl_set_batt, CARD_ROW_W, CARD_ROW_H);
    lv_obj_align(R.lbl_set_batt, LV_ALIGN_TOP_MID, 0, 122);
    lv_obj_set_style_text_align(R.lbl_set_batt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(R.lbl_set_batt, F_SMALL, 0);
    lv_obj_set_style_text_color(R.lbl_set_batt, lv_color_hex(UI_DIM), 0);
    lv_obj_set_style_pad_top(R.lbl_set_batt, 8, 0);

    R.lbl_set_hint = mk_panel_hint(R.settings_card, "UP/DOWN: SELECT   OK: EDIT");

    // ---- 玩法图鉴卡片(3 页图形化规则)----
    build_guide(R.overlay);
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

    // 记分板:三块 LCD 面板上的字。UI 盘点 #1:分数是最重要信息,放大到 20px
    // 亮黄 + 叠影描边;倍率/球数保持 14px 弱化权重。
    R.lbl_score_sh = lv_label_create(parent);       // 影层先建,压在主字下
    lv_obj_set_size(R.lbl_score_sh, PANEL_SCORE_X1 - PANEL_SCORE_X0 - 4, 20);
    lv_obj_set_pos(R.lbl_score_sh, PANEL_SCORE_X0 + 3, 5);
    lv_obj_set_style_text_align(R.lbl_score_sh, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(R.lbl_score_sh, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(R.lbl_score_sh, &lv_font_montserrat_20, 0);
    lv_obj_set_style_pad_all(R.lbl_score_sh, 0, 0);
    lv_label_set_text(R.lbl_score_sh, "0");
    R.lbl_score = lv_label_create(parent);
    lv_obj_set_size(R.lbl_score, PANEL_SCORE_X1 - PANEL_SCORE_X0 - 4, 20);
    lv_obj_set_pos(R.lbl_score, PANEL_SCORE_X0 + 2, 4);
    lv_obj_set_style_text_align(R.lbl_score, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(R.lbl_score, lv_color_hex(C_SCORE), 0);
    lv_obj_set_style_text_font(R.lbl_score, &lv_font_montserrat_20, 0);
    lv_obj_set_style_pad_all(R.lbl_score, 0, 0);
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
    // UI 盘点 #2:深色半透明底板代替描边(LVGL 无文字描边),星空背景上不融字
    lv_obj_set_style_bg_color(R.lbl_msg, lv_color_hex(0x060a14), 0);
    lv_obj_set_style_bg_opa(R.lbl_msg, LV_OPA_70, 0);
    lv_obj_set_style_radius(R.lbl_msg, 3, 0);
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
        R.upg_lamp[i] = mk_lamp(parent, lx, (int)PB_UPG_CY, 5, C_MSG);
    }

    // 右道星标点亮态:路过即亮金色(判定逻辑在 pb_game.c on_star)。
    for (int i = 0; i < PB_STAR_COUNT; i++) {
        int sy = (int)(PB_STAR_Y0 + (float)i * PB_STAR_DY);
        R.star_lamp[i] = mk_lamp(parent, (int)PB_STAR_X, sy, 6, C_SCORE);
    }

    // ATTACK/RANK 面板数值:背景招牌在 y 202..210,数值必须落在招牌下方。
    // 之前 lbl_fuel 放在 y=205 与 "FUEL" 招牌重叠(差距清单 C4)。
    R.lbl_attack = lv_label_create(parent);
    lv_obj_set_size(R.lbl_attack, 42, 18);
    lv_obj_set_style_text_align(R.lbl_attack, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(R.lbl_attack, lv_color_hex(C_SCORE), 0);
    lv_obj_set_style_text_font(R.lbl_attack, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_all(R.lbl_attack, 0, 0);
    lv_obj_set_pos(R.lbl_attack, 16, 212);
    lv_label_set_text_fmt(R.lbl_attack, "%lu",
                          (unsigned long)pb_bump_score(0));
    R.lbl_rank = lv_label_create(parent);
    lv_obj_set_size(R.lbl_rank, 42, 18);
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
// 全部左对齐:montserrat 数字宽度不等,%7ld 填充右对齐会参差不齐(实机反馈)。
static void fmt_hs_table(pb_game *g, char *buf, size_t n) {
    size_t off = 0;
    for (int i = 0; i < PB_HS_SLOTS; i++) {
        const char *tail = (i + 1 < PB_HS_SLOTS) ? "\n" : "";
        int w;
        if (g->hs[i] > 0)
            w = snprintf(buf + off, n - off, "%d. %ld%s%s", i + 1, (long)g->hs[i],
                         g->hs_new == (int8_t)i ? " *" : "", tail);
        else
            w = snprintf(buf + off, n - off, "%d. %s%s", i + 1, "-------", tail);
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
        lv_label_set_text_fmt(R.lbl_score_sh, "%lu", (unsigned long)g->score);
        lv_label_set_text_fmt(R.lbl_score, "%lu", (unsigned long)g->score);
    }
    if (force || g->ball_num != R.last_ball) {
        R.last_ball = g->ball_num;
        // 开局前(主菜单/图鉴等 HUD 常驻可见)ball_num=0,不防 HUD 会显示“BALL 0/3”;
        // 无局时默认展示第一球。真实局内 ball_num 从 1 起(新局/球耗尽递增),不影响。
        lv_label_set_text_fmt(R.lbl_ball, "BALL %d/%d",
                              g->ball_num ? g->ball_num : 1, PB_BALLS_TOTAL);
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

    // 台面提示:事件消息优先;PLAY 空闲时轮播灯组触发条件一行式提示
    // (实机反馈 #5:灯组"感觉没用"=玩家不知道规则,规则常驻信息带)。
    bool msg_on = (g->msg_timer > 0 && g->msg[0]);
    const char *txt = msg_on ? g->msg : NULL;
    if (!msg_on && g->state == PB_STATE_PLAY) {
        static const char *const IDLE_HINTS[] = {
            "FLIPS->UPG", "TARGET->MULT", "RING->RANK", "STAR->BONUS",
            "LANES->BONUS",
        };
        txt = IDLE_HINTS[(R.tick / 180) % 5];       // 3s 轮播一条
    }
    if (force || (txt != NULL) != R.last_msg_on
        || (txt != NULL && strcmp(txt, R.last_msg) != 0)) {
        R.last_msg_on = (txt != NULL);
        if (txt) {
            snprintf(R.last_msg, sizeof(R.last_msg), "%s", txt);
            lv_label_set_text(R.lbl_msg, txt);
            // 轮播提示用暗灰蓝,与事件消息的金色区分权重
            lv_obj_set_style_text_color(R.lbl_msg,
                lv_color_hex(msg_on ? C_MSG : 0x7d8ba0), 0);
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

    // 浮层六张卡片:主菜单(标题)/结算/暂停/设置/排行榜/图鉴,按状态显隐。
    if (force || g->state != R.last_state) {
        R.last_state = g->state;
        R.last_blink = 0xFF;                    // 闪烁行强制按新状态重建
        bool show_menu  = (g->state == PB_STATE_TITLE);
        bool show_over  = (g->state == PB_STATE_OVER);
        bool show_hs    = (g->state == PB_STATE_HS);
        bool show_pause = (g->state == PB_STATE_PAUSE);
        bool show_set   = (g->state == PB_STATE_SETTINGS);
        bool show_guide = (g->state == PB_STATE_GUIDE);
        if (show_menu || show_over || show_hs || show_pause || show_set || show_guide) {
            lv_obj_clear_flag(R.overlay, LV_OBJ_FLAG_HIDDEN);
            card_show(R.menu_card, show_menu);
            card_show(R.info_card, show_over);
            card_show(R.hs_card, show_hs);
            card_show(R.pause_card, show_pause);
            card_show(R.settings_card, show_set);
            card_show(R.guide_card, show_guide);
            // 电量角标:设置页自带 BATTERY 行,隐藏右上角角标避免重复显示/贴挤。
            if (show_set) lv_obj_add_flag(R.lbl_batt, LV_OBJ_FLAG_HIDDEN);
            else          lv_obj_clear_flag(R.lbl_batt, LV_OBJ_FLAG_HIDDEN);
            if (show_over) {
                char tbl[80];
                fmt_hs_table(g, tbl, sizeof tbl);
                lv_label_set_text(R.lbl_ov_sub, tbl);
                lv_label_set_text_fmt(R.lbl_ov_hint, "SCORE %lu",
                                      (unsigned long)g->score);
            }
            if (show_hs) {
                char tbl[80];
                fmt_hs_table(g, tbl, sizeof tbl);
                lv_label_set_text(R.lbl_hs_table, tbl);
            }
            if (show_menu) R.last_menu_sel = 0xFF;   // 强制重建主菜单高亮
            if (show_set)  R.last_set_sel  = 0xFF;   // 强制重建设置行
            if (show_guide) R.last_guide_page = 0xFF; // 强制切换图鉴页
        } else {
            lv_obj_add_flag(R.overlay, LV_OBJ_FLAG_HIDDEN);
        }
        // 菜单类页面(标题/排行榜/设置/图鉴)无进行中对局，隐藏顶部分数/球数/倍率读数；
        // 暂停与结算仍保留(当前分数有意义)，游戏中(else 分支)自然显示。仅在状态跳变时刷。
        bool hud_hide = show_menu || show_hs || show_set || show_guide;
        lv_obj_t *hud[4] = { R.lbl_score, R.lbl_score_sh, R.lbl_ball, R.lbl_mult };
        for (int i = 0; i < 4; i++) {
            if (hud_hide) lv_obj_add_flag(hud[i], LV_OBJ_FLAG_HIDDEN);
            else          lv_obj_clear_flag(hud[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    // 右上角电量角标:overlay 子,随浮层一起显隐;电量变化时重写。
    if (force || g->battery != R.last_batt) {
        R.last_batt = g->battery;
        if (g->battery >= 0) lv_label_set_text_fmt(R.lbl_batt, "BAT %d%%", g->battery);
        else                 lv_label_set_text(R.lbl_batt, "BAT --");
    }

    // 主菜单选中项(统一高亮样式)。
    if (g->state == PB_STATE_TITLE &&
        (force || g->menu_sel != R.last_menu_sel)) {
        R.last_menu_sel = g->menu_sel;
        static const char *const mitems[PB_MENU_ROWS] = { "START", "HOW TO PLAY", "SETTINGS", "HIGH SCORES" };
        for (int i = 0; i < PB_MENU_ROWS; i++) {
            lv_obj_t *it = R.lbl_menu_item[i];
            lv_label_set_text(it, mitems[i]);
            sel_row_style(it, i == g->menu_sel);
        }
    }

    // 暂停菜单选中项(4 项,同一高亮样式)。
    if (force || (g->state == PB_STATE_PAUSE && g->pause_sel != R.last_pause_sel)) {
        R.last_pause_sel = g->pause_sel;
        static const char *const items[4] = { "RESUME", "RESTART", "EXIT TO TITLE", "SETTINGS" };
        for (int i = 0; i < 4; i++) {
            lv_obj_t *it = R.lbl_pause_item[i];
            lv_label_set_text(it, items[i]);
            sel_row_style(it, i == g->pause_sel);
        }
    }

    // 设置页:选中/编辑态/数值/电量任一变化时重建三行 + 电量行。
    // 调节态把数值用尖括号包起 + 高亮底色,提示此时 UP/DOWN 改的是这一行。
    if (g->state == PB_STATE_SETTINGS &&
        (force || g->set_sel != R.last_set_sel || g->set_edit != R.last_set_edit ||
         g->vol != R.last_set_vol || g->blu != R.last_set_blu ||
         g->battery != R.last_set_batt)) {
        R.last_set_sel = g->set_sel;
        R.last_set_edit = g->set_edit;
        R.last_set_vol = g->vol;
        R.last_set_blu = g->blu;
        R.last_set_batt = g->battery;
        for (int i = 0; i < PB_SET_ROWS; i++) {
            lv_obj_t *it = R.lbl_set_row[i];
            bool sel = (i == g->set_sel);
            bool editing = sel && g->set_edit;
            char buf[24];
            if (i == PB_SET_ROW_VOL)
                snprintf(buf, sizeof buf, editing ? "VOLUME <%d>" : "VOLUME  %d", g->vol);
            else if (i == PB_SET_ROW_BLU)
                snprintf(buf, sizeof buf, editing ? "LIGHT <%d>" : "LIGHT  %d", g->blu);
            else
                snprintf(buf, sizeof buf, "BACK");
            lv_label_set_text(it, buf);
            // 选中行高亮底(与主菜单/暂停页同一金底黑字样式)。
            sel_row_style(it, sel);
        }
        if (g->battery >= 0)
            lv_label_set_text_fmt(R.lbl_set_batt, "BATTERY  %d%%", g->battery);
        else
            lv_label_set_text(R.lbl_set_batt, "BATTERY  --");
        lv_label_set_text(R.lbl_set_hint,
                          g->set_edit ? "UP/DOWN: +/-   OK: DONE"
                                      : "UP/DOWN: SELECT   OK: EDIT");
    }

    // 图鉴页切换:只显当前页容器,页码角标同步(内容静态,建卡时已填好)。
    if (g->state == PB_STATE_GUIDE &&
        (force || g->guide_page != R.last_guide_page)) {
        R.last_guide_page = g->guide_page;
        for (int i = 0; i < PB_GUIDE_PAGES; i++)
            card_show(R.guide_page[i], i == g->guide_page);
        lv_label_set_text_fmt(R.lbl_guide_page, "%d/%d",
                              g->guide_page + 1, PB_GUIDE_PAGES);
    }

    // 闪烁行:仅结算页(lbl_blink 是 info_card 子,只在 GAME OVER 浮层可见)。
    // 标题页已改为完整主菜单,自带底部提示,不再用此闪烁行。
    if (g->state == PB_STATE_OVER) {
        uint8_t blink = (uint8_t)((R.tick / 24) & 1);       // ~0.4s 相位
        if (blink != R.last_blink) {
            R.last_blink = blink;
            if (g->new_high) {
                lv_label_set_text(R.lbl_blink, "NEW HIGH SCORE!");
                lv_obj_set_style_text_color(R.lbl_blink,
                    lv_color_hex(blink ? UI_GOLD : 0xf5f5f5), 0);
            } else {
                lv_label_set_text(R.lbl_blink, "PRESS ANY KEY");
                lv_obj_set_style_text_color(R.lbl_blink,
                    lv_color_hex(blink ? UI_TEXT : UI_DIM), 0);
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

    // 右道星标点亮态:路过即亮,全亮加成后整组重置(逻辑在 pb_game.c on_star)。
    uint8_t star_bits = 0;
    for (int i = 0; i < PB_STAR_COUNT; i++)
        if (g->star_lit[i]) star_bits |= (uint8_t)(1u << i);
    if (force || star_bits != R.last_star_bits) {
        for (int i = 0; i < PB_STAR_COUNT; i++) {
            bool on = (star_bits >> i) & 1;
            bool was = (R.last_star_bits >> i) & 1;
            if (force || on != was) {
                if (on) lv_obj_clear_flag(R.star_lamp[i], LV_OBJ_FLAG_HIDDEN);
                else    lv_obj_add_flag(R.star_lamp[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        R.last_star_bits = star_bits;
    }

    // ATTACK 面板 = 当前 bumper 档位实际分值(规格 §2.1),RANK = 9 级缩写(§3)。
    if (force || g->ring_lit != R.last_ring_lit || g->bump_tier != R.last_bump_tier
        || g->rank != R.last_rank) {
        if (!force && (g->bump_tier != R.last_bump_tier || g->rank != R.last_rank))
            R.panel_flash = 48;               // 升档/升衔:面板闪白 ~0.8s 强化感知
        R.last_ring_lit = g->ring_lit;
        R.last_bump_tier = g->bump_tier;
        R.last_rank = g->rank;
        lv_label_set_text_fmt(R.lbl_attack, "%lu",
                              (unsigned long)pb_bump_score(g->bump_tier));
        lv_label_set_text(R.lbl_rank, pb_rank_name(g->rank));
    }
    if (R.panel_flash > 0) R.panel_flash--;
    bool pf = R.panel_flash > 0 && ((R.tick / 4) & 1);
    if (force || pf != R.last_pf) {
        R.last_pf = pf;
        uint32_t pcol = pf ? 0xffffff : C_SCORE;
        lv_obj_set_style_text_color(R.lbl_attack, lv_color_hex(pcol), 0);
        lv_obj_set_style_text_color(R.lbl_rank, lv_color_hex(pcol), 0);
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
