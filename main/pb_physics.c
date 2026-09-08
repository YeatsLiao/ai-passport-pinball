// main/pb_physics.c —— 弹球物理实现。
// 参考 sanderdesnaijer/esp32-pinball 的思路:自适应子步长 + 圆/线段/圆碰撞,
// 在此基础上补充挡板动量传递与单向阀,规则与台面在 pb_table/pb_game 中。
#include "pb_physics.h"

#include <math.h>

#define PB_MAX_SUBSTEPS 10

// ---- 小工具 ----

static pb_vec2 v_add(pb_vec2 a, pb_vec2 b) { return (pb_vec2){a.x + b.x, a.y + b.y}; }
static pb_vec2 v_sub(pb_vec2 a, pb_vec2 b) { return (pb_vec2){a.x - b.x, a.y - b.y}; }
static pb_vec2 v_scale(pb_vec2 a, float s) { return (pb_vec2){a.x * s, a.y * s}; }
static float v_dot(pb_vec2 a, pb_vec2 b)   { return a.x * b.x + a.y * b.y; }
static float v_len(pb_vec2 a)              { return sqrtf(v_dot(a, a)); }

// 点 p 到线段 ab 的最近点。
static pb_vec2 closest_on_seg(pb_vec2 a, pb_vec2 b, pb_vec2 p) {
    pb_vec2 ab = v_sub(b, a);
    float denom = v_dot(ab, ab);
    if (denom < 1e-6f) return a;                       // 退化线段
    float t = v_dot(v_sub(p, a), ab) / denom;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return v_add(a, v_scale(ab, t));
}

// ---- 挡板 ----

void pb_flipper_set(pb_flipper *f, bool up) { f->up = up; }

void pb_flipper_step(pb_flipper *f, float dt) {
    float target = f->up ? f->raised : f->rest;
    float diff = target - f->angle;
    if (fabsf(diff) < 1e-4f) {
        f->omega = 0.0f;
        return;
    }
    float step = f->speed * dt;
    if (fabsf(diff) < step) step = diff;
    else step = (diff > 0.0f) ? step : -step;
    f->angle += step;
    f->omega = step / dt;
}

// 挡板上与球最近点 q 处的表面速度(刚体绕 pivot 转动:v = omega x r)。
static pb_vec2 flipper_point_vel(const pb_flipper *f, pb_vec2 q) {
    pb_vec2 r = v_sub(q, f->pivot);
    // omega 绕 z 轴,屏幕 y 向下:负 omega 逆时针(视觉上向上扫),此处直接用叉积分量
    return (pb_vec2){-f->omega * r.y, f->omega * r.x};
}

// ---- 碰撞解算 ----

// 返回 true 表示发生了"命中"(用于上报事件),位置推出总会执行。
static bool collide_seg(pb_ball *ball, const pb_seg *s) {
    if (!s->solid) return false;
    // 单向阀:球快速上行时穿透,发球道口的挡片只拦下落的球
    if (s->gate && ball->vel.y < -60.0f) return false;

    pb_vec2 q = closest_on_seg(s->a, s->b, ball->pos);
    pb_vec2 d = v_sub(ball->pos, q);
    float dist = v_len(d);
    if (dist >= ball->r) return false;
    if (dist < 1e-4f) {
        // 球心落在线段上:沿垂直方向随便取一向外法向
        pb_vec2 ab = v_sub(s->b, s->a);
        float l = v_len(ab);
        d = (pb_vec2){-ab.y / l, ab.x / l};
        dist = 0.0f;
    }
    pb_vec2 n = v_scale(d, 1.0f / (dist > 1e-4f ? dist : 1.0f));
    ball->pos = v_add(q, v_scale(n, ball->r + 0.01f));

    float vn = v_dot(ball->vel, n);
    if (vn >= 0.0f) return false;                       // 正在离开,只推出不反弹
    float e = s->restitution;
    ball->vel = v_sub(ball->vel, v_scale(n, (1.0f + e) * vn));
    if (s->kick > 0.0f) ball->vel = v_add(ball->vel, v_scale(n, s->kick));
    return true;
}

static bool collide_circle(pb_ball *ball, const pb_circle *c) {
    if (!c->solid) return false;
    pb_vec2 d = v_sub(ball->pos, c->c);
    float dist = v_len(d);
    float sum = ball->r + c->r;
    if (dist >= sum) return false;
    if (dist < 1e-4f) d = (pb_vec2){0.0f, -1.0f};
    pb_vec2 n = v_scale(d, 1.0f / (dist > 1e-4f ? dist : 1.0f));
    ball->pos = v_add(c->c, v_scale(n, sum + 0.01f));

    float vn = v_dot(ball->vel, n);
    if (vn < 0.0f) {
        ball->vel = v_sub(ball->vel, v_scale(n, (1.0f + c->restitution) * vn));
    }
    if (c->kick > 0.0f) ball->vel = v_add(ball->vel, v_scale(n, c->kick));
    return true;
}

// 挡板:与旋转杆的碰撞 + 动量传递(接触点表面速度参与相对速度)。
static bool collide_flipper(pb_ball *ball, const pb_flipper *f) {
    pb_vec2 tip = v_add(f->pivot, (pb_vec2){cosf(f->angle) * f->len, sinf(f->angle) * f->len});
    pb_vec2 q = closest_on_seg(f->pivot, tip, ball->pos);
    pb_vec2 d = v_sub(ball->pos, q);
    float dist = v_len(d);
    if (dist >= ball->r) return false;
    if (dist < 1e-4f) d = (pb_vec2){0.0f, -1.0f};
    pb_vec2 n = v_scale(d, 1.0f / (dist > 1e-4f ? dist : 1.0f));
    ball->pos = v_add(q, v_scale(n, ball->r + 0.01f));

    pb_vec2 vf = flipper_point_vel(f, q);
    pb_vec2 vrel = v_sub(ball->vel, vf);
    float vn = v_dot(vrel, n);
    if (vn >= 0.0f) return false;
    // 挡板面板偏"弹性",拍球有明显的加速感
    ball->vel = v_sub(ball->vel, v_scale(n, 1.35f * vn));
    return true;
}

// ---- 主步进 ----

void pb_step(pb_world *w, float dt, pb_hit *hit) {
    pb_ball *b = &w->ball;
    if (hit) hit->seg = hit->circle = -1, hit->flipper = false;
    if (!b->active || dt <= 0.0f) return;

    // 限速:子步长再细也架不住无限大速度
    float sp = v_len(b->vel);
    if (sp > w->max_speed) {
        b->vel = v_scale(b->vel, w->max_speed / sp);
        sp = w->max_speed;
    }

    // 自适应细分:单步位移不超过半径一半
    int n = 1;
    if (b->r > 0.0f) n = (int)(sp * dt / (b->r * 0.5f)) + 1;
    if (n < 1) n = 1;
    if (n > PB_MAX_SUBSTEPS) n = PB_MAX_SUBSTEPS;
    float h = dt / (float)n;

    for (int i = 0; i < n; i++) {
        b->vel.y += w->gravity * h;
        float keep = expf(-w->drag * h);
        b->vel = v_scale(b->vel, keep);
        b->pos = v_add(b->pos, v_scale(b->vel, h));

        for (int k = 0; k < w->seg_count; k++) {
            if (collide_seg(b, &w->segs[k]) && hit) hit->seg = k;
        }
        for (int k = 0; k < w->circle_count; k++) {
            if (collide_circle(b, &w->circles[k]) && hit) hit->circle = k;
        }
        for (int k = 0; k < w->flipper_count; k++) {
            if (collide_flipper(b, &w->flippers[k]) && hit) hit->flipper = true;
        }
    }
}
