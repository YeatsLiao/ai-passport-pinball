// main/pb_game.h —— 游戏状态机与规则。
// 玩法对标 Space Cadet:3 球、bumper/弹弓/掉落目标计分、顶部三车道点亮
// 加倍率(上限 x5)、掉落目标组清空大额奖励、发球后短暂球保存。
#pragma once

#include <stdint.h>

#include "pb_table.h"

// 与 BSP 解耦的按键抽象(main.c 负责把 bsp_btn_t 映射过来)。
typedef enum {
    PB_KEY_L = 0,       // 左挡板(设备上键)
    PB_KEY_R,           // 右挡板(设备下键)
    PB_KEY_OK,          // 发球/开始(设备确定键)
} pb_key_t;

typedef enum {
    PB_EV_PRESS = 0,
    PB_EV_RELEASE,
    PB_EV_LONG,         // 仅 OK 用:任意时刻长按 OK 退回标题
} pb_key_ev_t;

typedef enum {
    PB_STATE_TITLE = 0, // 标题/最高分
    PB_STATE_LAUNCH,    // 球在发球道,蓄力发射
    PB_STATE_PLAY,      // 球在台面
    PB_STATE_DRAIN,     // 掉落过场
    PB_STATE_OVER,      // 结算
} pb_state_t;

// 音效事件(实现在 pb_audio.c,设备侧)。
typedef enum {
    PB_SND_FLIP = 0, PB_SND_BUMP, PB_SND_SLING, PB_SND_TARGET,
    PB_SND_LANE, PB_SND_LAUNCH, PB_SND_DRAIN, PB_SND_BONUS, PB_SND_OVER,
} pb_snd_t;

void pb_audio_init(void);
void pb_audio_play(pb_snd_t snd);

#define PB_BALLS_TOTAL 3
#define PB_MULT_MAX 5
#define PB_BALL_SAVE_S 8.0f

typedef struct {
    pb_table table;
    pb_state_t state;
    float state_timer;

    uint32_t score;
    uint32_t high_score;
    uint8_t ball_num;               // 第几个球,1 起
    uint8_t mult;                   // 得分倍率 1..5
    bool lane_lit[PB_LANE_COUNT];
    bool target_down[PB_TARGET_COUNT];
    float target_reset;             // >0:目标组清空后重置倒计时
    float ball_save;                // >0:球保存剩余秒数
    float launch_power;             // 0..1 蓄力

    char msg[24];                   // 台面提示文本
    float msg_timer;
    float ball_prev_x;              // 帧首球 x,顶部车道 rollover 的横向穿越判定用
    float flash_circle[PB_CIRCLE_MAX];
    float flash_target[PB_TARGET_COUNT];
    float flash_sling;
    float stuck_time;               // 低速滞留计时(防卡死救球用)
    bool stuck_side;                // 救球冲量左右交替

    // 输入事件环形队列(按键回调单生产者,游戏步进单消费者)。
    volatile uint8_t ev_head, ev_tail;
    uint8_t ev_buf[16];             // (key<<2)|ev 打包
} pb_game;

// 纯游戏侧初始化(不碰 NVS/音频设备)。state=TITLE。
void pb_game_init(pb_game *g);
// 设备侧:从 NVS 读最高分。失败保持 0。
void pb_game_nvs_load(pb_game *g);
// 设备侧:把最高分写回 NVS。
void pb_game_nvs_save(pb_game *g);

// 按键事件入口(可在任意任务/回调里调用,只入队不干活)。
void pb_game_key(pb_game *g, pb_key_t key, pb_key_ev_t ev);
// 每帧推进。dt 秒;必须在 LVGL 任务的 lv_timer 里调用(渲染层同任务同步)。
void pb_game_step(pb_game *g, float dt);
