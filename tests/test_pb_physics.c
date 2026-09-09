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

// 圆(含裙边/灯座半径)与矩形是否分离。圆心在矩形内时返回 false。
static bool circle_outside_rect(float cx, float cy, float r,
                                float x0, float y0, float x1, float y1) {
    float dx = fmaxf(x0 - cx, cx - x1);
    float dy = fmaxf(y0 - cy, cy - y1);
    return dx * dx + dy * dy > r * r;
}

static void test_table_sane(void) {
    printf("[table sane]\n");
    pb_table_init(&t);
    CHECK(t.seg_count > 0 && t.seg_count <= PB_SEG_MAX, "seg_count in range");
    // 规格 §2.1:kick>0 的才是 pop bumper(走档位分),kick==0 的是回弹立柱(走 500)。
    int bumps = 0, posts = 0;
    for (int i = 0; i < t.circle_count; i++)
        if (t.circles[i].kick > 0.0f) bumps++; else posts++;
    CHECK(bumps == 3, "3 pop bumpers");
    CHECK(posts == 4, "4 rebound posts");
    CHECK(t.circle_count <= PB_CIRCLE_MAX, "circle_count in range");
    for (int i = 0; i < PB_TARGET_COUNT; i++)
        CHECK(pb_table_target_seg(&t, i) >= 0, "target lookup");
    CHECK(pb_table_sling_seg(&t, 0) >= 0 && pb_table_sling_seg(&t, 1) >= 0, "sling lookup");
}

// 规格 §1/§2.4 几何不变量 + 硬性要求"UI 各元素互不遮挡"。
// "黑洞位置不对 → 无限球"和"文字错乱"都回归过不止一次,这里把布局关系钉死。
static void test_spec_layout(void) {
    printf("[spec layout]\n");
    pb_table_init(&t);

    // 黑洞 a_kout3:实机反馈后移到徽章行星中心,与环灯/立柱/侧灯保持安全距离
    // (环灯 45、立柱 32、侧灯 23,均大于捕获盘 r8 + 球心 r4 的活动半径)。
    CHECK(PB_HOLE_X > 100.0f && PB_HOLE_X < 114.0f, "black hole at badge center X");
    CHECK(PB_HOLE_Y > 188.0f && PB_HOLE_Y < 200.0f, "black hole at badge center Y");
    CHECK(PB_HOLE_Y > PB_INFO_Y1 + 8.0f && PB_HOLE_Y < 230.0f,
          "black hole clear of info band and panels");
    // 引力井/hyperspace 分居左右窄通道,不与黑洞重合。
    CHECK(PB_WELL_X < 40.0f && PB_HS_X > 180.0f, "side holes on opposite flanks");
    CHECK(PB_INFO_Y1 - PB_INFO_Y0 >= 18.0f, "info band fits one 14px line");

    int bad = 0;
    for (int i = 0; i < t.circle_count; i++) {
        float r = t.circles[i].r + 1.5f;          // +1.5 = 烘焙裙边外沿
        bad += !circle_outside_rect(t.circles[i].c.x, t.circles[i].c.y, r,
                                    PB_INFO_X0, PB_INFO_Y0, PB_INFO_X1, PB_INFO_Y1);
    }
    CHECK(bad == 0, "no bumper/post skirt inside the info band");

    bad = 0;
    for (int i = 0; i < PB_RING_COUNT; i++) {     // 角度表与 pb_art.py 一致
        float a = (150.0f - 30.0f * i) * 3.14159265f / 180.0f;
        bad += !circle_outside_rect(PB_RING_CX + PB_RING_R * cosf(a),
                                    PB_RING_CY - PB_RING_R * sinf(a), 3.4f,
                                    PB_INFO_X0, PB_INFO_Y0, PB_INFO_X1, PB_INFO_Y1);
    }
    CHECK(bad == 0, "ring lamps clear of the info band");

    bad = 0;
    for (int i = 0; i < PB_UPG_COUNT; i++) {
        float lx = PB_UPG_CX + (i - (PB_UPG_COUNT - 1) / 2.0f) * PB_UPG_DX;
        for (int c = 0; c < 3; c++)
            bad += !circle_outside_rect(t.circles[c].c.x, t.circles[c].c.y,
                                        t.circles[c].r + 1.5f,
                                        lx - 5.4f, PB_UPG_CY - 4.4f,
                                        lx + 5.4f, PB_UPG_CY + 4.4f);
    }
    CHECK(bad == 0, "upgrade lamps clear of every bumper skirt");

    // 升级灯组与信息带、信息带与徽章弧(环心 - 环半径 - 灯座)也要分开。
    CHECK(PB_UPG_CY + 4.4f < PB_INFO_Y0, "upgrade lamps above the info band");
    CHECK(PB_RING_CY - PB_RING_R - 3.4f > PB_INFO_Y1, "badge ring below the info band");
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
        pb_step(&t.world, 0.016f, NULL);        // 挡板在 pb_step 内随细分推进
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
    test_spec_layout();
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
