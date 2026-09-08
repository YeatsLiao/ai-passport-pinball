// main/main.c —— ai-passport-pinball 入口。
// 初始化 BSP(显示/LVGL/按键/音频),然后一个 lv_timer 以 ~60Hz 驱动
// pb_game_step + pb_render_sync。所有 LVGL 对象访问都在 LVGL 任务内完成。
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_pins.h"
#include "pb_game.h"
#include "pb_render.h"
#include "pb_audio.h"

#include "lvgl.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "pinball";

static pb_game s_game;

// ---- 按键:60Hz 轮询分压电压,不走 iot_button 的事件链 ----
// 原因:释放事件靠 SINGLE_CLICK 在短按超时后补发(~200ms 延迟),长按会先发
// LONG 把按住中的挡板当成抬起;且实际电压贴近窗口分界时分类反复跳变,消抖
// 计数被清零,表现为按键迟钝。这里自己分类:30mV 迟滞 + 连续 2 帧确认,
// 按下/抬起边沿最迟 32ms 送达。bsp_button_init 仍要调(拿共用的 ADC 句柄),
// 回调传 NULL 屏蔽它自己的事件。
#define KEY_ZONE_NONE 3

// 迟滞分类:返回 0=UP 1=DOWN 2=OK 3=松开。从当前档往高档走要越过分界+H,
// 往低档走要低于分界-H,消除 ADC 在分界附近的抖动。
static int key_zone(int mv, int cur) {
    static const int T[3] = { 150, 447, 1900 };     // 与 BSP_BTN_MV_TABLE 分界一致
    const int H = 30;
    if (mv < 0) return cur;                         // 读失败:保持原状
    int target = (mv < T[0]) ? 0 : (mv < T[1]) ? 1 : (mv < T[2]) ? 2 : KEY_ZONE_NONE;
    if (target == cur) return cur;
    if (target > cur) {
        for (int i = cur; i < target; i++)
            if (mv <= T[i] + H) return i;           // 没越过迟滞带:留在当前档
        return target;
    }
    for (int i = target; i < cur; i++)
        if (mv >= T[i] - H) return i + 1;
    return target;
}

static void key_poll(void) {
    static int cur = KEY_ZONE_NONE, pend = KEY_ZONE_NONE, cnt = 0;
    static int ok_hold = 0;
    static bool long_sent = false;
    int z = key_zone(bsp_button_read_mv(), cur);
    if (z != cur) {
        if (z == pend) {
            if (++cnt >= 2) {                       // 连续 2 帧一致才切换
                if (cur < KEY_ZONE_NONE)
                    pb_game_key(&s_game, (pb_key_t)cur, PB_EV_RELEASE);
                if (z < KEY_ZONE_NONE)
                    pb_game_key(&s_game, (pb_key_t)z, PB_EV_PRESS);
                cur = z; pend = KEY_ZONE_NONE; cnt = 0;
            }
        } else { pend = z; cnt = 1; }
    } else { pend = KEY_ZONE_NONE; cnt = 0; }

    // OK 长按 1.2s → 呼出暂停菜单。阈值要明显大于蓄力发射的 0.9s 满力时间,
    // 否则蓄力时手一慢就弹菜单。
    if (cur == 2) {
        if (!long_sent && ++ok_hold > 72) {
            long_sent = true;
            pb_game_key(&s_game, PB_KEY_OK, PB_EV_LONG);
        }
    } else { ok_hold = 0; long_sent = false; }
}

static void game_timer_cb(lv_timer_t *t) {
    (void)t;
    key_poll();
    pb_game_step(&s_game, 0.016f);
    pb_render_sync(&s_game);
}

void app_main(void) {
    ESP_LOGI(TAG, "ai-passport-pinball 启动");

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败,检查 SPI 接线"
                      "(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);

    // NVS:最高分持久化。失败不阻塞,只是最高分不保存。
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    pb_game_init(&s_game);
    pb_game_nvs_load(&s_game);
    pb_audio_init();
    bsp_button_init(NULL, NULL);                    // 只要 ADC 句柄与校准,事件走 key_poll

    if (bsp_lvgl_lock(1000)) {
        lv_obj_t *scr = lv_screen_active();
        pb_render_build(&s_game, scr);
        lv_timer_create(game_timer_cb, 16, NULL);
        bsp_lvgl_unlock();
    }

    ESP_LOGI(TAG, "就绪,高分 %lu", (unsigned long)s_game.high_score);
}
