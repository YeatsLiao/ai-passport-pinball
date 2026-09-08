// main/pb_audio.h —— 合成音效(无需外部素材,固件内生成方波/扫频)。
#pragma once

#include "pb_game.h"

// 初始化 bsp_audio 并启动播放任务。幂等。
void pb_audio_init(void);
// 请求播放一个音效。非阻塞:入队失败(队满)直接丢弃。
void pb_audio_play(pb_snd_t snd);
