#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "lvgl.h"
#include "lvgl_kawaii_face.h"

/*
 * ---------------------------------------------------------------------------
 * Board-specific note
 * ---------------------------------------------------------------------------
 * This example does NOT include LCD or LVGL initialisation — those steps are
 * hardware-specific.  Before calling face_animation_init() you must:
 *
 *   1. Initialise your display (SPI/I2C, backlight, reset, …).
 *   2. Initialise LVGL and register your display/input drivers.
 *   3. Start the LVGL tick / port task.
 *
 * If you are using ESP-IDF with esp_lvgl_port, do:
 *
 *   #include "esp_lvgl_port.h"
 *   app_lcd_init();      // your board BSP
 *   app_lvgl_init();     // your board BSP — starts the LVGL task
 *
 * The thread-safety callbacks (lvgl_port_lock / lvgl_port_unlock) are
 * detected automatically by lvgl_kawaii_face when esp_lvgl_port is present.
 * On a custom LVGL task call face_set_lvgl_lock_fns() before
 * face_animation_init() to supply your own mutex pair.
 * ---------------------------------------------------------------------------
 */

static const char *TAG = "kawaii_example";

static const face_emotion_t ALL_EMOTIONS[] = {
    FACE_NEUTRAL,
    FACE_HAPPY,
    FACE_WORRIED,
    FACE_WINK,
    FACE_LOVE,
    FACE_SURPRISED,
    FACE_PLAYFUL,
    FACE_SILLY,
    FACE_SMIRK,
    FACE_WORKING_HARD,
    FACE_EXCITED,
    FACE_CONFUSED,
    FACE_COOL,
    FACE_SLEEPY,
    FACE_SAD,
    FACE_CRY,
    FACE_ANGRY,
};

static const char *EMOTION_NAMES[] = {
    "Neutral",
    "Happy",
    "Worried",
    "Wink",
    "Love",
    "Surprised",
    "Playful",
    "Silly",
    "Smirk",
    "Working Hard",
    "Excited",
    "Confused",
    "Cool",
    "Sleepy",
    "Sad",
    "Cry",
    "Angry",
};

#define NUM_EMOTIONS  (sizeof(ALL_EMOTIONS) / sizeof(ALL_EMOTIONS[0]))
#define HOLD_MS       2500

static void emotion_cycle_task(void *arg)
{
    for (int i = 0; ; i = (i + 1) % NUM_EMOTIONS) {
        face_set_emotion(ALL_EMOTIONS[i], true);
        ESP_LOGI(TAG, "Emotion → %s", EMOTION_NAMES[i]);
        vTaskDelay(pdMS_TO_TICKS(HOLD_MS));
    }
}

void app_main(void)
{
    /*
     * -----------------------------------------------------------------------
     * 1. LCD + LVGL init  (replace with your board BSP)
     * -----------------------------------------------------------------------
     */
    // ESP_ERROR_CHECK(app_lcd_init());
    // ESP_ERROR_CHECK(app_lvgl_init());

    /*
     * -----------------------------------------------------------------------
     * 2. Create a parent panel for the face widget
     *
     *    The face scales automatically to fill whatever object you give it.
     *    Size and position here; the component does the rest.
     * -----------------------------------------------------------------------
     */

    // lvgl_port_lock(0);

    lv_obj_t *face_panel = lv_obj_create(lv_scr_act());
    lv_obj_set_size(face_panel, 135, 135);
    lv_obj_center(face_panel);
    lv_obj_set_style_bg_opa(face_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(face_panel, 0, 0);
    lv_obj_set_style_pad_all(face_panel, 0, 0);
    lv_obj_clear_flag(face_panel, LV_OBJ_FLAG_SCROLLABLE);

    // lvgl_port_unlock();

    /*
     * -----------------------------------------------------------------------
     * 3. Initialise lvgl_kawaii_face
     * -----------------------------------------------------------------------
     */
    face_config_t cfg = {
        .parent          = face_panel,
        .animation_speed = 30,
        .blink_interval  = 3000,
        .auto_blink      = true,
    };
    ESP_ERROR_CHECK(face_animation_init(&cfg));

    face_set_emotion(FACE_NEUTRAL, false);
    ESP_LOGI(TAG, "lvgl_kawaii_face ready — cycling emotions every %d ms", HOLD_MS);

    /*
     * -----------------------------------------------------------------------
     * 4. Start the emotion cycle task
     * -----------------------------------------------------------------------
     */
    xTaskCreate(emotion_cycle_task, "emotion_cycle", 4096, NULL, 5, NULL);
}
