/**
 * @file lvgl_kawaii_face.h
 * @brief Dynamic facial animation system using LVGL 9 canvas
 * 
 * Features:
 * - Canvas-based eye and mouth rendering
 * - Multiple emotion states (happy, sad, surprised, angry, neutral, blink)
 * - Smooth transitions between emotions
 * - Automatic blinking animation
 */

#ifndef LVGL_KAWAII_FACE_H
#define LVGL_KAWAII_FACE_H

#include "lvgl.h"

/* Portable error type — on ESP32 (IDF or Arduino-ESP32) we use the real
 * esp_err.h; on other platforms we provide a minimal compatible typedef. */
#ifdef ESP_PLATFORM
#  include "esp_err.h"
#else
   typedef int esp_err_t;
#  ifndef ESP_OK
#    define ESP_OK          ((esp_err_t) 0)
#    define ESP_FAIL        ((esp_err_t)-1)
#    define ESP_ERR_NO_MEM  ((esp_err_t) 0x101)
#  endif
#endif

/**
 * @brief Facial emotion states
 */
typedef enum {
    FACE_NEUTRAL,      // Default neutral expression
    FACE_HAPPY,        // Genuinely happy — wide eyes, big smile, energetic bounce
    FACE_WORRIED,      // Nervous smile — squinted eyes with raised, angled brows
    FACE_SAD,          // Sad with frown
    FACE_SURPRISED,    // Surprised with wide eyes and open mouth
    FACE_ANGRY,        // Angry with furrowed brows
    FACE_SLEEPY,       // Sleepy with half-closed eyes
    FACE_WINK,         // Playful wink (one eye closed)
    FACE_LOVE,         // Love expression with hearts
    FACE_PLAYFUL,      // Playful with tongue out
    FACE_SILLY,        // Silly cross-eyed look
    FACE_SMIRK,        // Mischievous smirk
    FACE_CRY,          // Crying with tears falling from eyes
    FACE_WORKING_HARD, // Hard at work, dripping sweat with straining expression
    FACE_EXCITED,      // Super excited with rapid sparkles and darting eyes
    FACE_CONFUSED,     // Puzzled with asymmetric brows and wandering pupils
    FACE_COOL,         // Laid-back squint, slow confident glance
    FACE_BLINK,        // Blinking animation state
    FACE_EMOTION_COUNT
} face_emotion_t;

/**
 * @brief Face animation configuration
 *
 * The face is treated as an LVGL widget that fills a parent object you supply.
 * Size and position are controlled entirely by the parent object — create and
 * size it however you like before calling face_animation_init().
 *
 * Typical usage:
 *
 *   // 1. Create a panel at the desired size & position
 *   lv_obj_t *face_panel = lv_obj_create(lv_scr_act());
 *   lv_obj_set_size(face_panel, 135, 135);
 *   lv_obj_center(face_panel);
 *   lv_obj_set_style_bg_opa(face_panel, LV_OPA_TRANSP, 0);
 *   lv_obj_set_style_border_width(face_panel, 0, 0);
 *   lv_obj_clear_flag(face_panel, LV_OBJ_FLAG_SCROLLABLE);
 *
 *   // 2. Pass the panel as the parent — face fills it automatically
 *   face_config_t cfg = {
 *       .parent          = face_panel,
 *       .animation_speed = 30,
 *       .blink_interval  = 3000,
 *       .auto_blink      = true,
 *   };
 *   face_animation_init(&cfg);
 *
 *   // 3. Move the face any time — just move the panel
 *   lv_obj_set_pos(face_panel, new_x, new_y);
 *
 * Passing NULL uses the active screen as parent (face fills screen).
 *
 * All internal canvas dimensions are derived automatically from the
 * parent object's size, so proportions stay correct at any resolution.
 */
typedef struct {
    lv_obj_t *parent;          // LVGL parent object  (NULL = active screen)
    uint32_t animation_speed;  // Animation update interval in ms
    uint32_t blink_interval;   // Auto-blink interval in ms
    bool     auto_blink;       // Enable automatic blinking
} face_config_t;

/**
 * @brief Initialize the face animation system
 *
 * LVGL thread-safety note:
 *   On ESP-IDF, esp_lvgl_port lock/unlock callbacks are used automatically.
 *   On Arduino (single-threaded LVGL) the defaults are no-ops.
 *   Call face_set_lvgl_lock_fns() BEFORE face_animation_init() to supply
 *   your own mutex if needed.
 *
 * @param config Pointer to configuration structure (NULL for defaults)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t face_animation_init(face_config_t *config);

/**
 * @brief Override the LVGL lock/unlock callbacks
 *
 * Use this when you manage the LVGL task yourself instead of using
 * esp_lvgl_port, or to integrate a custom RTOS mutex.
 *
 * Pass NULL for both to restore the platform defaults.
 *
 * @param lock_fn   Called before every LVGL object access
 * @param unlock_fn Called after every LVGL object access
 */
void face_set_lvgl_lock_fns(void (*lock_fn)(void), void (*unlock_fn)(void));

/**
 * @brief Set the current emotion
 * 
 * @param emotion The emotion to display
 * @param smooth If true, transition smoothly to new emotion
 */
void face_set_emotion(face_emotion_t emotion, bool smooth);

/**
 * @brief Get the current emotion
 * 
 * @return face_emotion_t Current emotion state
 */
face_emotion_t face_get_emotion(void);

/**
 * @brief Update face animation (called by timer)
 * This handles smooth transitions and automatic blinking
 */
void face_animation_update(void);

/**
 * @brief Set custom eye openness (0-100)
 * 
 * @param left_eye Left eye openness percentage (0=closed, 100=fully open)
 * @param right_eye Right eye openness percentage (0=closed, 100=fully open)
 */
void face_set_eye_openness(uint8_t left_eye, uint8_t right_eye);

/**
 * @brief Set custom mouth shape (-100 to 100)
 * 
 * @param value Mouth expression (-100=frown, 0=neutral, 100=smile)
 */
void face_set_mouth_shape(int8_t value);

/**
 * @brief Enable or disable automatic blinking
 * 
 * @param enable true to enable, false to disable
 */
void face_set_auto_blink(bool enable);

/**
 * @brief Trigger a single blink animation
 */
void face_trigger_blink(void);

/**
 * @brief Set the position of the face on screen
 * 
 * @param x X coordinate for center of face
 * @param y Y coordinate for center of face
 */
void face_set_position(int16_t x, int16_t y);

/**
 * @brief Get the LVGL container object for the face widget
 *
 * Use this to reposition or resize the face widget from main.c after init:
 *
 *   lv_obj_set_pos(face_get_container(), x, y);
 *
 * @return lv_obj_t* Pointer to the face container, or NULL if not initialised
 */
lv_obj_t *face_get_container(void);

/**
 * @brief Clean up face animation resources
 */
void face_animation_deinit(void);

#endif // FACE_ANIMATION_H
