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

// 按键回调运行在 button 组件的定时器任务:只转成游戏事件,不做重活。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    pb_key_t k;
    switch (btn) {
    case BSP_BTN_UP:   k = PB_KEY_L;  break;
    case BSP_BTN_DOWN: k = PB_KEY_R;  break;
    default:           k = PB_KEY_OK; break;
    }
    pb_key_ev_t e = (ev == BSP_BTN_PRESS) ? PB_EV_PRESS
                  : (ev == BSP_BTN_LONG)  ? PB_EV_LONG
                                          : PB_EV_RELEASE;
    pb_game_key(&s_game, k, e);
}

static void game_timer_cb(lv_timer_t *t) {
    (void)t;
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
    bsp_button_init(on_key, NULL);

    if (bsp_lvgl_lock(1000)) {
        lv_obj_t *scr = lv_screen_active();
        pb_render_build(&s_game, scr);
        lv_timer_create(game_timer_cb, 16, NULL);
        bsp_lvgl_unlock();
    }

    ESP_LOGI(TAG, "就绪,高分 %lu", (unsigned long)s_game.high_score);
}
