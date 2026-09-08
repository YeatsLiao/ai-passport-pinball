// tests/test_pb_physics.c —— 物理核心与台面几何的主机单元测试。
// 不依赖 ESP-IDF/LVGL:用任意 C 编译器直接编译
//   cc -I main -o /tmp/test_pb_physics tests/test_pb_physics.c main/pb_physics.c main/pb_table.c -lm
#include "../main/pb_table.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_fail;

#define CHECK(cond, name) do { \
    if (cond) printf("  PASS %s\n", name); \
    else { printf("  FAIL %s\n", name); g_fail++; } \
} while (0)

// 以固定步长跑 n 秒(与设备 60Hz 相同的调用方式)。
static void run(pb_world *w, float seconds) {
    for (int i = 0; i < (int)(seconds / 0.016f); i++) pb_step(w, 0.016f, NULL);
}

static pb_table t;

static void test_table_sane(void) {
    printf("[table sane]\n");
    pb_table_init(&t);
    CHECK(t.seg_count > 0 && t.seg_count <= PB_SEG_MAX, "seg_count in range");
    CHECK(t.circle_count == 3, "3 bumpers");
    for (int i = 0; i < PB_TARGET_COUNT; i++)
        CHECK(pb_table_target_seg(&t, i) >= 0, "target lookup");
    CHECK(pb_table_sling_seg(&t, 0) >= 0 && pb_table_sling_seg(&t, 1) >= 0, "sling lookup");
}

// 球落在发球道底板上应当静止(地板 restitution=0)。
static void test_ball_rests_on_floor(void) {
    printf("[ball rests on floor]\n");
    pb_table_init(&t);
    t.world.ball.pos.x = t.spawn_x;
    t.world.ball.pos.y = t.spawn_y - 10;
    t.world.ball.vel.y = 0;
    t.world.ball.active = true;
    run(&t.world, 3.0f);
    float gap = fabsf(t.world.ball.pos.y - (306.0f - PB_BALL_R));
    CHECK(gap < 1.5f, "rests on lane floor");
    CHECK(fabsf(t.world.ball.vel.y) < 5.0f, "vertical speed settles");
    CHECK(t.world.ball.pos.x > 204.0f && t.world.ball.pos.x < 230.0f, "stays in lane");
}

// 撞左墙:vx 反向且能量不增(e<1)。
static void test_wall_bounce(void) {
    printf("[wall bounce]\n");
    pb_table_init(&t);
    t.world.ball.pos.x = 10.0f + PB_BALL_R + 3.0f;
    t.world.ball.pos.y = 220.0f;   // 避开目标组背墙(x=24)与弹弓
    t.world.ball.vel.x = -200.0f;
    t.world.ball.vel.y = 0.0f;
    t.world.ball.active = true;
    run(&t.world, 0.5f);
    CHECK(t.world.ball.vel.x > 0.0f, "vx reversed");
    CHECK(t.world.ball.vel.x <= 200.0f * 0.35f + 1.0f, "energy not increased");
}

// 单向阀:快速上行穿过,下行被弹回。
static void test_gate_one_way(void) {
    printf("[gate one-way]\n");
    pb_table_init(&t);
    // 找 gate 段,把球放在其中点下方,分别以向上/向下速度测试
    int g = -1;
    for (int i = 0; i < t.seg_count; i++)
        if (t.kind[i] == PB_SEG_GATE) g = i;
    CHECK(g >= 0, "gate exists");

    float mx = (t.segs[g].a.x + t.segs[g].b.x) * 0.5f;
    float my = (t.segs[g].a.y + t.segs[g].b.y) * 0.5f;

    t.world.ball.pos.x = mx;
    t.world.ball.pos.y = my + 8.0f;
    t.world.ball.vel.x = 0; t.world.ball.vel.y = -500.0f;   // 快速上行
    t.world.ball.active = true;
    run(&t.world, 0.03f);
    CHECK(t.world.ball.pos.y < my, "passes upward");

    pb_table_init(&t);
    t.world.ball.pos.x = mx;
    t.world.ball.pos.y = my - 8.0f;
    t.world.ball.vel.x = 0; t.world.ball.vel.y = +80.0f;    // 缓慢下落
    t.world.ball.active = true;
    run(&t.world, 0.05f);
    CHECK(t.world.ball.vel.y < 0.0f, "blocked downward");
}

// pop bumper:命中后速度明显增大(kick 生效)。
static void test_bumper_kick(void) {
    printf("[bumper kick]\n");
    pb_table_init(&t);
    pb_vec2 c = t.circles[1].c;
    float r = t.circles[1].r;
    t.world.ball.pos.x = c.x;
    t.world.ball.pos.y = c.y + r + PB_BALL_R + 2.0f;
    t.world.ball.vel.x = 0; t.world.ball.vel.y = -50.0f;
    t.world.ball.active = true;
    run(&t.world, 0.1f);
    CHECK(hypotf(t.world.ball.vel.x, t.world.ball.vel.y) > 150.0f, "kicked away");
}

// 挡板拍球:抬起瞬间球获得向上速度分量。
static void test_flipper_kick(void) {
    printf("[flipper kick]\n");
    pb_table_init(&t);
    pb_flipper *f = &t.flippers[0];
    // 球静置在左挡板杆中部上方
    float mid = f->len * 0.6f;
    pb_vec2 tip = { f->pivot.x + cosf(f->angle) * mid, f->pivot.y + sinf(f->angle) * mid };
    t.world.ball.pos.x = tip.x;
    t.world.ball.pos.y = tip.y - PB_BALL_R - 0.5f;
    t.world.ball.vel.x = 0; t.world.ball.vel.y = 0;
    t.world.ball.active = true;
    pb_flipper_set(f, true);
    float vy_min = 0.0f;
    for (int i = 0; i < 30; i++) {              // 抬起过程 ~50ms
        pb_flipper_step(f, 0.016f);
        pb_step(&t.world, 0.016f, NULL);
        if (t.world.ball.vel.y < vy_min) vy_min = t.world.ball.vel.y;
    }
    CHECK(vy_min < -150.0f, "flipper launches ball up");
}

// 两挡板静止时中央留有漏球缝(掉落路径存在)。
static void test_drain_gap(void) {
    printf("[drain gap]\n");
    pb_table_init(&t);
    pb_flipper *l = &t.flippers[0], *r = &t.flippers[1];
    pb_vec2 tl = { l->pivot.x + cosf(l->angle) * l->len, l->pivot.y + sinf(l->angle) * l->len };
    pb_vec2 tr = { r->pivot.x + cosf(r->angle) * r->len, r->pivot.y + sinf(r->angle) * r->len };
    float gap = tr.x - tl.x;
    CHECK(gap > PB_BALL_R * 2.0f, "gap fits ball");
    CHECK(gap < 20.0f, "gap not absurdly wide");

    // 直落中央应掉出去
    t.world.ball.pos.x = (tl.x + tr.x) * 0.5f;
    t.world.ball.pos.y = 290.0f;
    t.world.ball.vel.x = 0; t.world.ball.vel.y = 0;
    t.world.ball.active = true;
    run(&t.world, 1.5f);
    CHECK(t.world.ball.pos.y > t.drain_y, "ball drains through gap");
}

// 从发球道满力发射:球应能进入台面(越过内墙顶端高度)。
static void test_launch_reaches_playfield(void) {
    printf("[launch]\n");
    pb_table_init(&t);
    t.world.ball.pos.x = t.spawn_x;
    t.world.ball.pos.y = t.spawn_y;
    t.world.ball.vel.x = 0; t.world.ball.vel.y = -1250.0f;   // 满蓄力(800+450)
    t.world.ball.active = true;
    run(&t.world, 1.2f);
    CHECK(t.world.ball.pos.x < 204.0f, "entered playfield");
    CHECK(t.world.ball.pos.y < 150.0f, "reached upper table");
}

int main(void) {
    test_table_sane();
    test_ball_rests_on_floor();
    test_wall_bounce();
    test_gate_one_way();
    test_bumper_kick();
    test_flipper_kick();
    test_drain_gap();
    test_launch_reaches_playfield();
    printf(g_fail ? "\n%d test(s) FAILED\n" : "\nALL TESTS PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
