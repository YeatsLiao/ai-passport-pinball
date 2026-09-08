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
#define PB_CIRCLE_MAX 8         // 3 个 pop bumper + 中场/弹弓顶小立柱

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

// 中央虫洞(黑洞):球滚进洞心被捕获,得分后从随机车道口弹出。
// 位置在两挡板之间的球路必经点:徽章圆盘底缘 y=230,黑洞顶缘 233 不压徽章;
// 距两侧弹弓面/挡板抬起端都留有安全间距。台面美术的工具链也从这里读坐标
// (tools/gen_assets.py),改这里即可全同步。
#define PB_HOLE_X 107.0f
#define PB_HOLE_Y 248.0f
#define PB_HOLE_R 7.0f      // 捕获半径(洞口视觉半径 ~13)

// 找第 i 个掉落目标 / 弹弓对应的线段索引(台面固定布局,顺序即语义)。
int pb_table_target_seg(const pb_table *t, int i);
int pb_table_sling_seg(const pb_table *t, int side);   // side: 0=左 1=右

// 填充几何、初始化 world 引用。所有 solid 状态复位。
void pb_table_init(pb_table *t);
