// main/pb_render.h —— LVGL 渲染层。
// 与 pb_game 同任务运行(lv_timer),无需加锁。对象式绘制,靠 LVGL 局部重绘
// 控制内存与带宽(无 PSRAM,不能开全帧缓冲)。
#pragma once

#include "pb_game.h"
#include "lvgl.h"

// 在 parent(通常为活动 screen)上创建全部界面对象。
void pb_render_build(pb_game *g, lv_obj_t *parent);
// 每帧同步一次:球位置/挡板角度/闪光/文本。须在 pb_game_step 之后调用。
void pb_render_sync(pb_game *g);
