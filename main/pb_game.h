// main/pb_game.h —— 游戏状态机与规则。
// 数值与规则逐条对齐 docs/space-cadet-spec.md(从原版 control.cpp/high_score.cpp
// 提取的书面规格);改动请标出对应的规格条目号。
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
    PB_STATE_PAUSE,     // 暂停菜单(长按 OK 呼出,UP/DOWN 选择,OK 确认)
} pb_state_t;

// 音效事件(实现在 pb_audio.c,设备侧)。
typedef enum {
    PB_SND_FLIP = 0, PB_SND_BUMP, PB_SND_SLING, PB_SND_TARGET,
    PB_SND_LANE, PB_SND_LAUNCH, PB_SND_DRAIN, PB_SND_BONUS, PB_SND_OVER,
} pb_snd_t;

void pb_audio_init(void);
void pb_audio_play(pb_snd_t snd);

#define PB_BALLS_TOTAL 3            // 规格 §4.1 MaxBallCount
#define PB_MULT_COUNT 5             // 规格 §4.2 score_multipliers[] 长度
#define PB_BALL_SAVE_S 5.0f         // 规格 §4.3 lite200 亮 5.0s
#define PB_BUMP_TIER_MAX 3          // 规格 §2.1 control_bump_scores1 下标 0..3
#define PB_UPG_LAMPS PB_UPG_COUNT   // 规格 §3 bmpr_inc_lights 灯数
#define PB_HS_TIERS 4               // 规格 §2.4 control_kickout_score1[0/2/3/4]
#define PB_RANK_MAX 9               // 规格 §3 RankRcArray[9]
#define PB_RING_LAMPS PB_RING_COUNT // 规格 §3 outer_circle 段数
#define PB_HS_SLOTS 5               // 规格 §5.1 最高分槽位数
#define PB_HS_EMPTY (-999)          // 规格 §5.2 空槽哨兵

// 命中点得分飘字(向上飘 + 渐隐,渲染层读取绘制)。
#define PB_POPUPS 3
typedef struct {
    float t;            // 剩余寿命秒数,<=0 空闲
    int16_t x, y;       // 屏幕坐标(命中点)
    uint32_t value;     // 显示 +<value>
} pb_popup;

typedef struct {
    pb_table table;
    pb_state_t state;
    float state_timer;

    // ---- 得分与榜单(规格 §4.6 / §5) ----
    uint32_t score;
    uint32_t high_score;            // hs[0] 的快照,面板/结算页直接读
    int32_t hs[PB_HS_SLOTS];        // 5 槽榜单,-999 = 空槽(原版哨兵)
    int8_t hs_new;                  // 本局入榜槽位,-1 = 未入榜
    uint8_t ball_num;               // 第几个球,1 起
    uint8_t mult_idx;               // 规格 §4.2:倍率是索引 0..4,不是乘数
    uint8_t bump_tier;              // 规格 §2.1:attack bumper 升级档位 0..3
    uint8_t bump_prog;              // 规格 §3:bmpr_inc_lights 已亮灯数 0..3
    uint8_t hs_lights;              // 规格 §2.4:hyperspace 档位 0..3
    uint8_t ring_lit;               // 规格 §3:outer_circle 已亮段数 0..5
    uint8_t rank;                   // 规格 §3:middle_circle 军衔 1..9

    bool lane_lit[PB_LANE_COUNT];
    bool target_down[PB_TARGET_COUNT];
    float target_reset;             // >0:目标组清空后重置倒计时
    float ball_save;                // >0:球保存剩余秒数(每球最多触发一次)
    bool ball_save_used;            // 本球的球保存已用过(防窗口内循环重发=无限球)
    float launch_power;             // 0..1 蓄力

    char msg[20];                   // 信息带文本(规格 §3 info_text_box)
    float msg_timer;
    float ball_prev_x;              // 帧首球 x,顶部车道 rollover 的横向穿越判定用
    float flash_circle[PB_CIRCLE_MAX];
    float flash_target[PB_TARGET_COUNT];
    float flash_sling;
    float flash_upg;                // 升级灯组满组闪(原版 Message(7,5.0))
    float stuck_time;               // 低速滞留计时(防卡死救球)
    bool stuck_side;                // 救球冲量左右交替
    float lost_time;                // 球不活跃又不在踢出洞过场的计时(丢球保险)

    // 三个 kickout 洞各自的过场/冷却(原版 TKickout::Message(55, TimerTime1))。
    float hole_timer;               // 黑洞落球口
    float hole_cooldown;
    float flash_hole;
    float well_timer;               // 引力井(左上)
    float well_cooldown;
    float hs_timer;                 // hyperspace(右上)
    float hs_cooldown;

    // 得分飘字槽
    pb_popup popups[PB_POPUPS];

    // 暂停菜单
    pb_state_t paused_prev;         // 呼出前的状态(RESUME 回去)
    uint8_t pause_sel;              // 0=RESUME 1=RESTART 2=EXIT
    bool new_high;                  // 本局刷新纪录(结算页闪烁提示用)

    // 输入事件环形队列(按键回调单生产者,游戏步进单消费者)。
    volatile uint8_t ev_head, ev_tail;
    uint8_t ev_buf[16];             // (key<<2)|ev 打包
} pb_game;

// 纯游戏侧初始化(不碰 NVS/音频设备)。state=TITLE。
void pb_game_init(pb_game *g);
// 设备侧:从 NVS 读 5 槽榜单(带校验和)。失败/校验不过 → 空表。
void pb_game_nvs_load(pb_game *g);
// 设备侧:把榜单写回 NVS(规格 §5.5:校验和不匹配时整表清零)。
void pb_game_nvs_save(pb_game *g);

// 按键事件入口(可在任意任务/回调里调用,只入队不干活)。
void pb_game_key(pb_game *g, pb_key_t key, pb_key_ev_t ev);
// 倍率值表(规格 §4.2),面板显示与 add_score 共用。
uint32_t pb_mult_value(uint8_t idx);
// 当前 bumper 档位的基础分值(规格 §2.1),ATTACK 面板显示用。
uint32_t pb_bump_score(uint8_t tier);
// 军衔缩写名(1..9),RANK 面板宽 42px 放不下全名。
const char *pb_rank_name(uint8_t rank);
// 每帧推进。dt 秒;必须在 LVGL 任务的 lv_timer 里调用(渲染层同任务同步)。
void pb_game_step(pb_game *g, float dt);
