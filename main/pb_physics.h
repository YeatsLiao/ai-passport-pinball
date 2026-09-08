// main/pb_physics.h —— 弹球物理核心。
// 纯 C、无 ESP-IDF/LVGL 依赖:同一份代码既跑在设备上,也跑在主机单元测试里。
#pragma once

#include <stdbool.h>

// 2D 向量。单位:像素、像素/秒。屏幕坐标系 y 向下为正。
typedef struct {
    float x, y;
} pb_vec2;

// 球。
typedef struct {
    pb_vec2 pos, vel;
    float r;            // 半径
    bool active;        // false 时跳过整个物理
} pb_ball;

// 线段墙。
typedef struct {
    pb_vec2 a, b;
    float restitution;  // 法向反弹系数,0=完全不反弹(地板),1=完全弹性
    float kick;         // 命中后沿法向的额外冲量(弹簧炮/弹弓用),普通墙为 0
    bool gate;          // 单向阀:球快速上行时穿透(发球道口防回吞),其余视为实心
    bool solid;         // false = 暂时失效(掉落目标放倒后)
} pb_seg;

// 圆形障碍(pop bumper 等)。
typedef struct {
    pb_vec2 c;
    float r;
    float restitution;
    float kick;         // pop bumper 主动弹球
    bool solid;
} pb_circle;

// 挡板(flipper):绕 pivot 旋转的杆(碰撞体为带半宽的胶囊)。
typedef struct {
    pb_vec2 pivot;
    float len;
    float radius;       // 杆的半宽(胶囊半径),视觉与手感对齐用
    float angle;        // 当前角度(弧度,y 向下坐标系)
    float omega;        // 当前角速度(弧度/秒,用于给球传递动量)
    float rest;         // 静止角
    float raised;       // 抬起角
    float speed;        // 摆动角速度(弧度/秒)
    bool up;            // 目标状态
} pb_flipper;

// 一次物线步进内命中的元素(由调用者清零后传入,用于计分/音效)。
typedef struct {
    int seg;            // 命中的线段索引,-1 = 无
    int circle;         // 命中的圆索引,-1 = 无
    bool flipper;       // 命中挡板(左右共两块,上层按需区分)
} pb_hit;

// 世界:一张台面 + 一个球。多球不在范围内(原版单球)。
typedef struct {
    pb_ball ball;
    pb_seg *segs;
    int seg_count;
    pb_circle *circles;
    int circle_count;
    pb_flipper *flippers;
    int flipper_count;
    float gravity;      // 像素/秒^2
    float drag;         // 每秒速度保留率倒数指数,如 0.10 ≈ 每秒衰 10%
    float max_speed;    // 限速,防穿模
} pb_world;

// 挡板控制。
void pb_flipper_set(pb_flipper *f, bool up);
// 推进挡板角度并更新 omega。dt 单位秒。
void pb_flipper_step(pb_flipper *f, float dt);

// 推进世界一个帧步长(dt 秒)。内部自适应细分,保证单步位移(球的移动和
// 挡板扫动)不超过半径一半;挡板也随细分同步推进,调用方无需再单独步进。
// 命中信息累加进 *hit(传 NULL 忽略)。同一帧多次命中同一元素只报一次由调用者去重,
// 本函数对每次细分步内的不同元素都上报。
void pb_step(pb_world *w, float dt, pb_hit *hit);
