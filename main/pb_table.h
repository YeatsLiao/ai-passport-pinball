// main/pb_table.h —— 台面几何与元素定义。
// 布局对标微软三维弹球(Space Cadet)的核心结构:顶部三车道、三个 pop bumper、
// 左侧掉落目标组、双侧弹弓、右侧发球道 + 单向阀。纯 C,无 IDF/LVGL 依赖。
#pragma once

#include <stdint.h>

#include "pb_physics.h"

#define PB_SCREEN_W 240
#define PB_SCREEN_H 320
#define PB_STATUS_H 26          // 顶部状态栏高度(分数/球数/倍率)

#define PB_SEG_MAX 40
#define PB_CIRCLE_MAX 8         // 3 个 pop bumper + 4 个小立柱(kick==0)

// 线段的玩法类别(决定颜色与计分)。
typedef enum {
    PB_SEG_WALL = 0,            // 普通墙/导轨
    PB_SEG_SLING,               // 弹弓(命中加分 + 弹开)
    PB_SEG_TARGET,              // 掉落目标(放倒得分)
    PB_SEG_GATE,                // 单向阀(发球道口)
    PB_SEG_FLOOR,               // 发球道底(不反弹,球静停)
} pb_seg_kind_t;

typedef struct {
    pb_seg segs[PB_SEG_MAX];            // 线段(顺序见 pb_table.c)
    uint8_t kind[PB_SEG_MAX];
    int seg_count;
    pb_circle circles[PB_CIRCLE_MAX];   // pop bumper
    int circle_count;
    pb_flipper flippers[2];             // 0=左 1=右
    pb_world world;                     // 引用上面的数组,直接喂 pb_step

    // 判定用几何常量
    float lane_x[3];                    // 顶部三车道中心 x
    float lane_y;                       // 顶部三车道灯插中心线 y(rollover 按横向穿越判定)
    float drain_y;                      // 低于此线 = 掉落
    float spawn_x, spawn_y;             // 发球位(发球道内)
} pb_table;

// 目标组/车道的固定数量,渲染与规则都要遍历。
#define PB_TARGET_COUNT 3
#define PB_LANE_COUNT 3
#define PB_BALL_R 4.0f

// ---- 洞系(原版 kickout,规格 §2.4) ----
// 黑洞 a_kout3:实机反馈后从落球口 (107,300) 移到徽章行星中心 (104,200),
// 与 SPACE CADET 徽章的行星图案重合成"行星虫洞"。距环灯 ~45、立柱 ~27、
// 侧灯 ~20,球心 r4 与捕获盘 r8 之间有充分通道;冷却期内洞失效球自然穿过。
#define PB_HOLE_X 104.0f
#define PB_HOLE_Y 200.0f
#define PB_HOLE_R 8.0f      // 捕获半径(洞视觉半径 ~13,覆盖行星图案)

// 右道星标 lane_stars:右外墙(204)内侧回球滑道的路过判定点,无碰撞不挡球。
// 三枚收在信息带(底 144)与 ATTACK/RANK 卡片(顶 198)之间的空带内,互不重叠;
// 球心贴墙滑落 x≈199.6,距星标 x=193 约 6.6px < 判定半径 9,必触发。
#define PB_STAR_X   193.0f
#define PB_STAR_Y0  150.0f
#define PB_STAR_DY  17.0f
#define PB_STAR_COUNT 3

// 引力井 a_kout1:左上窄通道尽头的踢出洞。通道净宽 ~16px,球(r4)可通过。
#define PB_WELL_X 21.0f
#define PB_WELL_Y 68.0f
#define PB_WELL_R 5.0f      // 捕获半径(通道窄,洞视觉半径 ~6)

// hyperspace 踢出洞 a_kout2:发球道内墙与右上导轨之间的窄区。
#define PB_HS_X 196.0f
#define PB_HS_Y 68.0f
#define PB_HS_R 5.0f

// ---- 灯组与信息带(台面坐标,美术与渲染层共用同一组常量) ----
// outer_circle 军衔进度环:绕徽章外弧 5 盏(角度 150/120/90/60/30)。
// 徽章圆心 = 环心,所以 pb_art.py 的徽章从这里取 cy。
#define PB_RING_CX 107.0f
#define PB_RING_CY 194.0f
#define PB_RING_R  45.0f
#define PB_RING_COUNT 5

// bmpr_inc_lights bumper 升级灯组:三盏,排在中央 bumper 裙下方的空档里。
#define PB_UPG_CX 107.0f
#define PB_UPG_CY 111.0f
#define PB_UPG_DX 14.0f
#define PB_UPG_COUNT 3

// info_text_box 信息带:徽章上方那条唯一的横向空档。提示文字只允许出现在这里,
// 不再居中叠在徽章弧字/行星上(规格 §3 文本框 + "UI 互不遮挡"要求)。
// 上下边界与 bumper 裙边(123.5)、进度环顶灯座(145.6)各留 ≥1.5px,
// 这组关系由 tests/test_pb_physics.c 的 [spec layout] 用例钉死。
#define PB_INFO_X0 46.0f
#define PB_INFO_Y0 125.0f
#define PB_INFO_X1 160.0f
#define PB_INFO_Y1 144.0f

// 找第 i 个掉落目标 / 弹弓对应的线段索引(台面固定布局,顺序即语义)。
int pb_table_target_seg(const pb_table *t, int i);
int pb_table_sling_seg(const pb_table *t, int side);   // side: 0=左 1=右

// 填充几何、初始化 world 引用。所有 solid 状态复位。
void pb_table_init(pb_table *t);
