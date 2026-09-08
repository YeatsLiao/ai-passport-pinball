// main/pb_assets.h —— 由 tools/gen_assets.py 生成,请勿手改。
//
// 台面美术在构建前烘焙成一张 240x320 RGB565 背景 + 一组 RGB565A8 精灵,
// 打包进 main/assets/pb_art.bin(EMBED_FILES 嵌入 .rodata,不占 RAM)。
// 运行时渲染层只做 lv_image 换 src / 挪位置,静态台面一帧都不用重画。
#pragma once

#include <stdint.h>
#include "lvgl.h"

#define PB_ART_FLIP_FRAMES 12
#define PB_ART_BUMP_COUNT  3
#define PB_ART_LANE_COUNT  3
#define PB_ART_TGT_COUNT   3
#define PB_ART_SLING_COUNT 2

// 台面背景(整屏)。
extern const lv_image_dsc_t pb_img_bg;

// 球。pb_ball_ofs 是精灵左上角相对球心的偏移。
extern const lv_image_dsc_t pb_img_ball;
extern const int16_t pb_ball_ofs[2];

// 挡板:[side][frame],frame 0 = 静止,末帧 = 抬起。
// pb_flip_ofs 是各帧包围盒左上角相对转轴的偏移。
extern const lv_image_dsc_t *const pb_img_flip[2][PB_ART_FLIP_FRAMES];
extern const int16_t pb_flip_ofs[2][PB_ART_FLIP_FRAMES][2];

// pop bumper 帽:[i][0]=常态 [i][1]=命中闪光。pb_bump_pos 为屏幕绝对坐标。
extern const lv_image_dsc_t *const pb_img_bump[PB_ART_BUMP_COUNT][2];
extern const int16_t pb_bump_pos[PB_ART_BUMP_COUNT][2];

// 顶部车道灯芯:[0]=灭 [1]=亮,三个车道共用一套。
extern const lv_image_dsc_t *const pb_img_lane[2];
extern const int16_t pb_lane_pos[PB_ART_LANE_COUNT][2];

// 掉落目标:[0]=立着 [1]=放倒,三个目标共用一套。
extern const lv_image_dsc_t *const pb_img_tgt[2];
extern const int16_t pb_tgt_pos[PB_ART_TGT_COUNT][2];

// 弹弓橡皮筋:[side][0]=常态 [1]=闪光。
extern const lv_image_dsc_t *const pb_img_sling[PB_ART_SLING_COUNT][2];
extern const int16_t pb_sling_pos[PB_ART_SLING_COUNT][2];
