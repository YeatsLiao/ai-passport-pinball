// main/pb_table.c —— 台面几何数据。
// 坐标系:240x320 竖屏,y 向下。0..PB_STATUS_H 为状态栏,台面占其余部分。
#include "pb_table.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// 关键 x:左外墙 10,发球道内墙 204,发球道外墙 230,台面中心 ~107。
typedef struct {
    float x1, y1, x2, y2;
    uint8_t kind;
    float rest;
    float kick;
    bool gate;
} seg_def_t;

static const seg_def_t SEGS[] = {
    // ---- 外墙与顶部弧(普通墙,e=0.35) ----
    {  10,  96,  10, 264, PB_SEG_WALL, 0.35f, 0, false },  // 左外墙
    {  10,  96,  14,  56, PB_SEG_WALL, 0.35f, 0, false },  // 左上弧
    {  14,  56,  34,  32, PB_SEG_WALL, 0.35f, 0, false },
    {  34,  32,  70,  26, PB_SEG_WALL, 0.35f, 0, false },
    {  70,  26, 150,  30, PB_SEG_WALL, 0.35f, 0, false },  // 顶部缓拱:慢球总能滚离近水平段
    { 150,  30, 210,  26, PB_SEG_WALL, 0.35f, 0, false },  // 跨发球道口
    { 210,  26, 228,  40, PB_SEG_WALL, 0.35f, 0, false },  // 外墙顶
    { 228,  40, 230,  70, PB_SEG_WALL, 0.35f, 0, false },
    { 230,  70, 230, 306, PB_SEG_WALL, 0.30f, 0, false },  // 右外墙(发球道外侧)
    { 204,  44, 204, 264, PB_SEG_WALL, 0.35f, 0, false },  // 发球道内墙
    {  24, 120,  24, 186, PB_SEG_WALL, 0.35f, 0, false },  // 目标组背墙
    {  30,  58,  27,  86, PB_SEG_WALL, 0.35f, 0, false },  // 左上轨道导轨(与背墙顺接)
    {  27,  86,  25, 122, PB_SEG_WALL, 0.35f, 0, false },
    { 184,  58, 188,  86, PB_SEG_WALL, 0.35f, 0, false },  // 右上轨道导轨
    { 188,  86, 188, 122, PB_SEG_WALL, 0.35f, 0, false },
    // ---- 底部导轨:外墙滑下来的球导入挡板(原版 inlane 的简化) ----
    {  10, 264,  72, 277, PB_SEG_WALL, 0.30f, 0, false },  // 左下导轨 → 左挡板轴
    { 204, 264, 142, 277, PB_SEG_WALL, 0.30f, 0, false },  // 右下导轨 → 右挡板轴
    // ---- 弹弓:三角面斜边主动弹球,其余两边是普通墙 ----
    {  46, 238,  70, 256, PB_SEG_SLING, 0.50f, 280, false },  // 左弹弓面
    {  46, 238,  46, 262, PB_SEG_WALL,  0.35f, 0, false },
    {  46, 262,  70, 256, PB_SEG_WALL,  0.35f, 0, false },
    { 168, 238, 144, 256, PB_SEG_SLING, 0.50f, 280, false },  // 右弹弓面
    { 168, 238, 168, 262, PB_SEG_WALL,  0.35f, 0, false },
    { 168, 262, 144, 256, PB_SEG_WALL,  0.35f, 0, false },
    // ---- 掉落目标组(左侧,朝右迎球) ----
    {  26, 128,  40, 132, PB_SEG_TARGET, 0.60f, 0, false },
    {  26, 150,  40, 154, PB_SEG_TARGET, 0.60f, 0, false },
    {  26, 172,  40, 176, PB_SEG_TARGET, 0.60f, 0, false },
    // ---- 顶部三车道分隔柱 ----
    {  87,  25,  87,  54, PB_SEG_WALL, 0.30f, 0, false },
    { 127,  25, 127,  54, PB_SEG_WALL, 0.30f, 0, false },
    // ---- 发球道:单向阀 + 底板 ----
    { 204,  50, 226,  42, PB_SEG_GATE, 0.20f, 0, true },
    { 204, 306, 230, 306, PB_SEG_FLOOR, 0.0f, 0, false },
};

#define SEG_DEF_COUNT (sizeof(SEGS) / sizeof(SEGS[0]))

// pop bumper:中间大、两侧小(对标原版 bumpers)。
static const pb_circle CIRCLES[] = {
    { {107,  84}, 15, 0.60f, 180, true },
    { { 70, 110}, 12, 0.60f, 170, true },
    { {144, 110}, 12, 0.60f, 170, true },
    { { 58, 180}, 4.5, 0.65f,   0, true },   // 小立柱:给中场加弹点(无 kick)
    { {156, 180}, 4.5, 0.65f,   0, true },
    { {107, 150}, 4.5, 0.65f,   0, true },   // 中场立柱:bumper 三角下方的回弹点
    { { 78, 208}, 4.0, 0.65f,   0, true },   // 弹弓顶小柱:进挡板区前多一双向弹
    { {136, 208}, 4.0, 0.65f,   0, true },
};

// 挡板:rest/raised 为弧度。左挡板 rest≈+32°(指向右下),raised≈-26°。
static void init_flipper(pb_flipper *f, float px, float py, float rest, float raised) {
    f->pivot = (pb_vec2){px, py};
    f->len = 34.0f;
    f->radius = 2.4f;                  // 杆半宽,与精灵宽度对齐(精灵半宽 ~2.3-2.8)
    f->rest = rest;
    f->raised = raised;
    f->speed = 20.0f;
    f->angle = rest;
    f->omega = 0.0f;
    f->up = false;
}

void pb_table_init(pb_table *t) {
    t->seg_count = (int)SEG_DEF_COUNT;
    for (int i = 0; i < t->seg_count; i++) {
        const seg_def_t *d = &SEGS[i];
        t->segs[i].a = (pb_vec2){d->x1, d->y1};
        t->segs[i].b = (pb_vec2){d->x2, d->y2};
        t->segs[i].restitution = d->rest;
        t->segs[i].kick = d->kick;
        t->segs[i].gate = d->gate;
        t->segs[i].solid = true;
        t->kind[i] = d->kind;
    }

    t->circle_count = (int)(sizeof(CIRCLES) / sizeof(CIRCLES[0]));
    for (int i = 0; i < t->circle_count; i++) t->circles[i] = CIRCLES[i];

    init_flipper(&t->flippers[0],  70, 280, 0.56f, -0.45f);
    init_flipper(&t->flippers[1], 144, 280, (float)M_PI - 0.56f, (float)M_PI + 0.45f);

    t->lane_x[0] = 67; t->lane_x[1] = 107; t->lane_x[2] = 147;
    // lane_y 是三条车道灯插的中心线:球从发球道冲上来后是横着滚过顶部的,
    // 所以 rollover 按"横向穿越灯插 x"判定(和原版一致),而不是竖向穿线。
    t->lane_y = 42;
    t->drain_y = (float)PB_SCREEN_H;    // 出屏即掉落
    t->spawn_x = 217; t->spawn_y = 298;

    t->world.ball = (pb_ball){{0, 0}, {0, 0}, PB_BALL_R, false};
    t->world.segs = t->segs;
    t->world.seg_count = t->seg_count;
    t->world.circles = t->circles;
    t->world.circle_count = t->circle_count;
    t->world.flippers = t->flippers;
    t->world.flipper_count = 2;
    t->world.gravity = 800.0f;          // 像素/秒^2,手感调校值
    t->world.drag = 0.10f;              // 每秒 ~10% 速度衰减
    t->world.max_speed = 1300.0f;
}

int pb_table_target_seg(const pb_table *t, int i) {
    int seen = 0;
    for (int k = 0; k < t->seg_count; k++) {
        if (t->kind[k] == PB_SEG_TARGET) {
            if (seen == i) return k;
            seen++;
        }
    }
    return -1;
}

int pb_table_sling_seg(const pb_table *t, int side) {
    int seen = 0;
    for (int k = 0; k < t->seg_count; k++) {
        if (t->kind[k] == PB_SEG_SLING) {
            if (seen == side) return k;
            seen++;
        }
    }
    return -1;
}
