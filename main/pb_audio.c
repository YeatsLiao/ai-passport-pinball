// main/pb_audio.c —— 合成音效实现。
// 原版音效素材不能直接搬(版权 + 体积),这里用方波/扫频合成替代,
// 命中/得分/掉落各有辨识度。播放走独立任务,不阻塞 LVGL 与按键回调。
#include "pb_audio.h"

#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <math.h>
#include <string.h>

static const char *TAG = "pb_audio";

#define PB_SR        16000                 // 采样率
#define PB_BUF_MS    600                   // 最长单音效时长
#define PB_BUF_SAMP  (PB_SR * PB_BUF_MS / 1000)

// 音效定义:起始/结束频率(Hz)+ 时长。f1==f2 为固定音,否则线性扫频。
typedef struct {
    float f1, f2;
    int ms;
} snd_def_t;

static const snd_def_t SND_DEFS[] = {
    [PB_SND_FLIP]   = { 240.0f,  240.0f,  30 },   // 挡板:短促"嗒"
    [PB_SND_BUMP]   = { 900.0f,  700.0f,  60 },   // bumper:"叮"
    [PB_SND_SLING]  = { 620.0f,  880.0f,  50 },   // 弹弓:上扬
    [PB_SND_TARGET] = { 1320.0f, 1320.0f, 60 },   // 目标:高音
    [PB_SND_LANE]   = { 990.0f,  990.0f,  50 },   // 车道
    [PB_SND_LAUNCH] = { 200.0f,  900.0f, 120 },   // 发射:快速上扫
    [PB_SND_DRAIN]  = { 300.0f,  70.0f,  400 },   // 掉落:下坠
    [PB_SND_BONUS]  = { 1046.0f, 1568.0f, 180 },  // 奖励:双音上扬
    [PB_SND_OVER]   = { 440.0f,  110.0f, 550 },   // 结束:长下扫
};

static QueueHandle_t s_queue;
static int16_t s_pcm[PB_BUF_SAMP];

static void audio_task(void *arg) {
    (void)arg;
    for (;;) {
        pb_snd_t snd;
        if (xQueueReceive(s_queue, &snd, portMAX_DELAY) != pdTRUE) continue;
        if (snd >= sizeof(SND_DEFS) / sizeof(SND_DEFS[0])) continue;
        const snd_def_t *d = &SND_DEFS[snd];

        int n = PB_SR * d->ms / 1000;
        if (n > PB_BUF_SAMP) n = PB_BUF_SAMP;
        float phase = 0.0f;
        for (int i = 0; i < n; i++) {
            float t = (float)i / n;                       // 0..1
            float f = d->f1 + (d->f2 - d->f1) * t;
            phase += f / PB_SR;
            if (phase >= 1.0f) phase -= 1.0f;
            // 方波 + 指数衰减包络(衰减到一半音长以内)
            float env = expf(-3.0f * t);
            int16_t s = (int16_t)((phase < 0.5f ? 1.0f : -1.0f) * 9000.0f * env);
            s_pcm[i] = s;
        }
        bsp_audio_write(s_pcm, (size_t)n * 2);
    }
}

void pb_audio_init(void) {
    static bool s_started = false;
    if (s_started) return;
    if (bsp_audio_init() != ESP_OK) {
        ESP_LOGW(TAG, "音频初始化失败,静音运行");
        return;
    }
    bsp_audio_set_format(PB_SR, 16, 1);
    bsp_audio_set_volume(70);
    s_queue = xQueueCreate(6, sizeof(pb_snd_t));
    if (s_queue && xTaskCreate(audio_task, "pb_audio", 3072, NULL, 5, NULL) == pdPASS) {
        s_started = true;
    } else {
        ESP_LOGW(TAG, "音频任务创建失败,静音运行");
    }
}

void pb_audio_play(pb_snd_t snd) {
    if (!s_queue) return;
    // 调用方都在任务语境(按键组件定时器任务/游戏任务),用普通 send 即可
    xQueueSend(s_queue, &snd, 0);
}
