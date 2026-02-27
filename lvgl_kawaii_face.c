/**
 * @file lvgl_kawaii_face.c
 * @brief Dynamic facial animation implementation using LVGL 9 canvas
 */

#include "lvgl_kawaii_face.h"
#include <stdlib.h>
#include <math.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#define FACE_LOGI(tag, ...) ESP_LOGI(tag, __VA_ARGS__)
#define FACE_LOGE(tag, ...) ESP_LOGE(tag, __VA_ARGS__)
#define FACE_LOGW(tag, ...) ESP_LOGW(tag, __VA_ARGS__)
#else
#include <stdio.h>
#define FACE_LOGI(tag, fmt, ...) printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define FACE_LOGE(tag, fmt, ...) printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define FACE_LOGW(tag, fmt, ...) printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#endif

#if defined(ESP_PLATFORM) && __has_include("esp_heap_caps.h")
#include "esp_heap_caps.h"
#define FACE_MALLOC_DMA(size) heap_caps_malloc((size), MALLOC_CAP_DMA)

static inline void *face_malloc_canvas(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (p)
        return p;
#if defined(CONFIG_SPIRAM)
    p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p)
        return p;
#endif
    return malloc(size);
}
#define FACE_MALLOC_CANVAS(size) face_malloc_canvas(size)
#else
#define FACE_MALLOC_DMA(size) malloc(size)
#define FACE_MALLOC_CANVAS(size) malloc(size)
#endif

#if defined(ESP_PLATFORM) && __has_include("esp_lvgl_port.h")
#include "esp_lvgl_port.h"
#define _FACE_DEFAULT_LOCK() lvgl_port_lock(0)
#define _FACE_DEFAULT_UNLOCK() lvgl_port_unlock()
#else
#define _FACE_DEFAULT_LOCK() ((void)0)
#define _FACE_DEFAULT_UNLOCK() ((void)0)
#endif

static void (*s_face_lock_fn)(void) = NULL;
static void (*s_face_unlock_fn)(void) = NULL;

static void face_lock(void)
{
    if (s_face_lock_fn)
        s_face_lock_fn();
    else
        _FACE_DEFAULT_LOCK();
}
static void face_unlock(void)
{
    if (s_face_unlock_fn)
        s_face_unlock_fn();
    else
        _FACE_DEFAULT_UNLOCK();
}

void face_set_lvgl_lock_fns(void (*lock_fn)(void), void (*unlock_fn)(void))
{
    s_face_lock_fn = lock_fn;
    s_face_unlock_fn = unlock_fn;
}

static const char *TAG = "face_anim";

#define DEFAULT_EYE_WIDTH 40
#define DEFAULT_EYE_HEIGHT 40
#define DEFAULT_MOUTH_WIDTH 60
#define DEFAULT_MOUTH_HEIGHT 30
#define DEFAULT_ANIM_SPEED_MS 30
#define DEFAULT_BLINK_INTERVAL 3000

typedef struct
{
    lv_obj_t *left_eye_canvas;
    lv_obj_t *right_eye_canvas;
    lv_obj_t *mouth_canvas;
    lv_obj_t *face_container;

    lv_color_t *left_eye_buf;
    lv_color_t *right_eye_buf;
    lv_color_t *mouth_buf;

    face_config_t config;
    face_emotion_t current_emotion;
    face_emotion_t target_emotion;

    uint8_t left_eye_openness;
    uint8_t right_eye_openness;
    int8_t mouth_curve;
    int8_t left_eyebrow_angle;
    int8_t right_eyebrow_angle;
    int8_t eyebrow_height;
    uint8_t transition_progress;

    uint32_t last_blink_time;
    bool is_blinking;
    uint8_t blink_phase;

    uint8_t blush_intensity;
    int8_t bounce_offset;
    uint8_t sparkle_phase;
    uint8_t heart_beat_phase;

    int8_t pupil_offset_x;
    int8_t pupil_offset_y;
    uint8_t tear_fall_offset;
    uint8_t diamond_mouth_phase;
    uint8_t sweat_drop_offset;

    uint16_t face_sz;
    uint16_t eye_cw;
    uint16_t mouth_cw;
    uint16_t mouth_ch;

    lv_timer_t *anim_timer;
    bool initialized;
} face_state_t;

static face_state_t face_state = {0};

static void draw_eye(lv_obj_t *canvas, uint8_t openness, bool is_left);
static void draw_mouth(lv_obj_t *canvas, int8_t curve);
static void update_emotion_parameters(face_emotion_t emotion, uint8_t *left_eye, uint8_t *right_eye, int8_t *mouth,
                                      int8_t *left_brow, int8_t *right_brow, int8_t *brow_height);
static void animation_timer_cb(lv_timer_t *timer);

esp_err_t face_animation_init(face_config_t *config)
{
    if (face_state.initialized)
    {
        FACE_LOGW(TAG, "Face animation already initialized");
        return ESP_OK;
    }

    face_lock();

    if (config != NULL)
    {
        face_state.config = *config;
    }
    else
    {
        face_state.config.parent = NULL;
        face_state.config.animation_speed = DEFAULT_ANIM_SPEED_MS;
        face_state.config.blink_interval = DEFAULT_BLINK_INTERVAL;
        face_state.config.auto_blink = true;
    }

    lv_obj_t *parent_obj = (face_state.config.parent != NULL)
                               ? face_state.config.parent
                               : lv_scr_act();
    lv_obj_update_layout(parent_obj);
    int32_t parent_w = lv_obj_get_width(parent_obj);
    int32_t parent_h = lv_obj_get_height(parent_obj);
    uint16_t face_sz = (uint16_t)((parent_w < parent_h) ? parent_w : parent_h);

    face_state.face_sz = face_sz;
    face_state.eye_cw = (uint16_t)(face_sz * 0.45f);
    face_state.mouth_cw = (uint16_t)(face_sz * 0.45f);
    face_state.mouth_ch = (uint16_t)(face_sz * 0.38f);

    FACE_LOGI(TAG, "Parent: %dx%d, face_sz: %u, eye: %upx, mouth: %ux%upx",
              parent_w, parent_h, face_sz,
              face_state.eye_cw, face_state.mouth_cw, face_state.mouth_ch);

    face_state.face_container = lv_obj_create(parent_obj);
    lv_obj_set_size(face_state.face_container, face_sz, face_sz);
    lv_obj_center(face_state.face_container);
    lv_obj_set_style_bg_opa(face_state.face_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(face_state.face_container, 0, 0);
    lv_obj_set_style_pad_all(face_state.face_container, 0, 0);

    lv_obj_clear_flag(face_state.face_container, LV_OBJ_FLAG_SCROLLABLE);

    size_t eye_buf_size = face_state.eye_cw * face_state.eye_cw;
    size_t mouth_buf_size = face_state.mouth_cw * face_state.mouth_ch;

    face_state.left_eye_buf = FACE_MALLOC_CANVAS(eye_buf_size * sizeof(lv_color_t));
    face_state.right_eye_buf = FACE_MALLOC_CANVAS(eye_buf_size * sizeof(lv_color_t));
    face_state.mouth_buf = FACE_MALLOC_CANVAS(mouth_buf_size * sizeof(lv_color_t));

    if (!face_state.left_eye_buf || !face_state.right_eye_buf || !face_state.mouth_buf)
    {
        FACE_LOGE(TAG, "Failed to allocate canvas buffers");
        face_unlock();
        return ESP_ERR_NO_MEM;
    }

    int16_t eye_gap = face_state.eye_cw / 4;
    int16_t eye_y = (int16_t)(face_sz * 0.12f);
    int16_t left_eye_x = (int16_t)(face_sz / 2) - face_state.eye_cw - eye_gap / 2;
    int16_t right_eye_x = (int16_t)(face_sz / 2) + eye_gap / 2;
    int16_t mouth_y = (int16_t)(face_sz * 0.62f);
    int16_t mouth_x = (int16_t)(face_sz / 2) - (int16_t)(face_state.mouth_cw / 2);

    face_state.left_eye_canvas = lv_canvas_create(face_state.face_container);
    lv_canvas_set_buffer(face_state.left_eye_canvas, face_state.left_eye_buf,
                         face_state.eye_cw, face_state.eye_cw,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(face_state.left_eye_canvas, left_eye_x, eye_y);

    face_state.right_eye_canvas = lv_canvas_create(face_state.face_container);
    lv_canvas_set_buffer(face_state.right_eye_canvas, face_state.right_eye_buf,
                         face_state.eye_cw, face_state.eye_cw,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(face_state.right_eye_canvas, right_eye_x, eye_y);

    face_state.mouth_canvas = lv_canvas_create(face_state.face_container);
    lv_canvas_set_buffer(face_state.mouth_canvas, face_state.mouth_buf,
                         face_state.mouth_cw, face_state.mouth_ch,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(face_state.mouth_canvas, mouth_x, mouth_y);

    face_state.current_emotion = FACE_NEUTRAL;
    face_state.target_emotion = FACE_NEUTRAL;
    face_state.left_eye_openness = 100;
    face_state.right_eye_openness = 100;
    face_state.mouth_curve = 0;
    face_state.left_eyebrow_angle = 0;
    face_state.right_eyebrow_angle = 0;
    face_state.eyebrow_height = 0;
    face_state.transition_progress = 100;
    face_state.last_blink_time = lv_tick_get();

    face_state.blush_intensity = 0;
    face_state.bounce_offset = 0;
    face_state.sparkle_phase = 0;
    face_state.heart_beat_phase = 0;

    draw_eye(face_state.left_eye_canvas, face_state.left_eye_openness, true);
    draw_eye(face_state.right_eye_canvas, face_state.right_eye_openness, false);
    draw_mouth(face_state.mouth_canvas, face_state.mouth_curve);

    face_state.anim_timer = lv_timer_create(animation_timer_cb,
                                            face_state.config.animation_speed,
                                            NULL);

    face_unlock();

    face_state.initialized = true;
    FACE_LOGI(TAG, "Face animation initialized successfully");

    return ESP_OK;
}

static void draw_eye(lv_obj_t *canvas, uint8_t openness, bool is_left)
{
    if (!canvas)
        return;

    uint16_t width = face_state.eye_cw;
    uint16_t height = face_state.eye_cw;

    lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    int16_t eye_width = width * 0.75;
    int16_t eye_height = (eye_width * openness) / 100;
    if (eye_height < 8)
        eye_height = 8;
    int16_t center_x = width / 2;
    int16_t center_y = (height * 0.6) + face_state.bounce_offset;

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_make(80, 60, 40);
    line_dsc.width = 4;
    line_dsc.opa = LV_OPA_COVER;

    int8_t eyebrow_angle = is_left ? face_state.left_eyebrow_angle : face_state.right_eyebrow_angle;
    int16_t eyebrow_y = center_y - eye_width / 2 - 6 + face_state.eyebrow_height;
    int16_t eyebrow_width = eye_width * 0.9;

    float angle_rad = eyebrow_angle * 3.14159 / 180.0;
    int16_t y_offset = eyebrow_width * 0.25 * sin(angle_rad);

    line_dsc.round_start = 1;
    line_dsc.round_end = 1;

    if (is_left)
    {
        line_dsc.p1.x = center_x - eyebrow_width * 0.5;
        line_dsc.p1.y = eyebrow_y - y_offset;
        line_dsc.p2.x = center_x + eyebrow_width * 0.5;
        line_dsc.p2.y = eyebrow_y + y_offset;
    }
    else
    {
        line_dsc.p1.x = center_x - eyebrow_width * 0.5;
        line_dsc.p1.y = eyebrow_y + y_offset;
        line_dsc.p2.x = center_x + eyebrow_width * 0.5;
        line_dsc.p2.y = eyebrow_y - y_offset;
    }

    lv_draw_line(&layer, &line_dsc);

    if (face_state.blush_intensity > 0)
    {
        lv_draw_rect_dsc_t blush_dsc;
        lv_draw_rect_dsc_init(&blush_dsc);
        blush_dsc.bg_color = lv_color_make(255, 150, 180);
        blush_dsc.bg_opa = (face_state.blush_intensity * LV_OPA_COVER) / 100;
        blush_dsc.radius = 8;
        blush_dsc.border_width = 0;

        lv_area_t blush_area;
        blush_area.x1 = center_x - 10;
        blush_area.y1 = center_y + eye_width / 2 + 2;
        blush_area.x2 = center_x + 10;
        blush_area.y2 = center_y + eye_width / 2 + 8;

        lv_draw_rect(&layer, &blush_dsc, &blush_area);
    }

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);

    if (face_state.current_emotion == FACE_LOVE && openness > 20)
    {
        int16_t heart_size = eye_width * 0.9;

        lv_color_t heart_color = lv_color_make(255, 60, 120);

        rect_dsc.bg_color = heart_color;
        rect_dsc.bg_opa = LV_OPA_COVER;
        rect_dsc.border_width = 0;

        rect_dsc.radius = heart_size * 0.18;
        lv_area_t bottom_tip;
        bottom_tip.x1 = center_x - heart_size * 0.08;
        bottom_tip.y1 = center_y + heart_size * 0.35;
        bottom_tip.x2 = center_x + heart_size * 0.08;
        bottom_tip.y2 = center_y + heart_size * 0.52;
        lv_draw_rect(&layer, &rect_dsc, &bottom_tip);

        rect_dsc.radius = heart_size * 0.15;
        lv_area_t lower_mid;
        lower_mid.x1 = center_x - heart_size * 0.22;
        lower_mid.y1 = center_y + heart_size * 0.12;
        lower_mid.x2 = center_x + heart_size * 0.22;
        lower_mid.y2 = center_y + heart_size * 0.42;
        lv_draw_rect(&layer, &rect_dsc, &lower_mid);

        rect_dsc.radius = heart_size * 0.12;
        lv_area_t upper_mid;
        upper_mid.x1 = center_x - heart_size * 0.38;
        upper_mid.y1 = center_y - heart_size * 0.12;
        upper_mid.x2 = center_x + heart_size * 0.38;
        upper_mid.y2 = center_y + heart_size * 0.22;
        lv_draw_rect(&layer, &rect_dsc, &upper_mid);

        rect_dsc.radius = LV_RADIUS_CIRCLE;
        int16_t bump_size = heart_size * 0.32;

        lv_area_t left_bump;
        left_bump.x1 = center_x - heart_size * 0.24 - bump_size;
        left_bump.y1 = center_y - heart_size * 0.28 - bump_size;
        left_bump.x2 = center_x - heart_size * 0.24 + bump_size;
        left_bump.y2 = center_y - heart_size * 0.28 + bump_size;
        lv_draw_rect(&layer, &rect_dsc, &left_bump);

        lv_area_t right_bump;
        right_bump.x1 = center_x + heart_size * 0.24 - bump_size;
        right_bump.y1 = center_y - heart_size * 0.28 - bump_size;
        right_bump.x2 = center_x + heart_size * 0.24 + bump_size;
        right_bump.y2 = center_y - heart_size * 0.28 + bump_size;
        lv_draw_rect(&layer, &rect_dsc, &right_bump);

        rect_dsc.radius = heart_size * 0.14;
        lv_area_t center_fill;
        center_fill.x1 = center_x - heart_size * 0.12;
        center_fill.y1 = center_y - heart_size * 0.32;
        center_fill.x2 = center_x + heart_size * 0.12;
        center_fill.y2 = center_y - heart_size * 0.05;
        lv_draw_rect(&layer, &rect_dsc, &center_fill);

        rect_dsc.radius = heart_size * 0.16;

        lv_area_t left_smooth;
        left_smooth.x1 = center_x - heart_size * 0.42;
        left_smooth.y1 = center_y - heart_size * 0.08;
        left_smooth.x2 = center_x - heart_size * 0.25;
        left_smooth.y2 = center_y + heart_size * 0.18;
        lv_draw_rect(&layer, &rect_dsc, &left_smooth);

        lv_area_t right_smooth;
        right_smooth.x1 = center_x + heart_size * 0.25;
        right_smooth.y1 = center_y - heart_size * 0.08;
        right_smooth.x2 = center_x + heart_size * 0.42;
        right_smooth.y2 = center_y + heart_size * 0.18;
        lv_draw_rect(&layer, &rect_dsc, &right_smooth);

        rect_dsc.bg_color = lv_color_white();
        rect_dsc.bg_opa = LV_OPA_80;
        rect_dsc.border_width = 0;
        rect_dsc.radius = LV_RADIUS_CIRCLE;

        int16_t hl_size = heart_size * 0.2;
        lv_area_t highlight;
        highlight.x1 = center_x - heart_size * 0.2 - hl_size / 2;
        highlight.y1 = center_y - heart_size * 0.2 - hl_size / 2;
        highlight.x2 = center_x - heart_size * 0.2 + hl_size / 2;
        highlight.y2 = center_y - heart_size * 0.2 + hl_size / 2;
        lv_draw_rect(&layer, &rect_dsc, &highlight);

        rect_dsc.bg_opa = LV_OPA_60;
        int16_t hl_small = heart_size * 0.12;
        highlight.x1 = center_x + heart_size * 0.05 - hl_small / 2;
        highlight.y1 = center_y - heart_size * 0.12 - hl_small / 2;
        highlight.x2 = center_x + heart_size * 0.05 + hl_small / 2;
        highlight.y2 = center_y - heart_size * 0.12 + hl_small / 2;
        lv_draw_rect(&layer, &rect_dsc, &highlight);

        if (face_state.sparkle_phase > 0)
        {
            rect_dsc.bg_color = lv_color_make(255, 240, 100);
            rect_dsc.bg_opa = (face_state.sparkle_phase * LV_OPA_COVER) / 100;
            rect_dsc.radius = 2;

            for (int i = 0; i < 6; i++)
            {
                float angle = (i * 60 + face_state.sparkle_phase * 5) * 3.14159 / 180.0;
                int16_t spark_dist = heart_size * 0.6;
                int16_t spark_x = center_x + spark_dist * cos(angle);
                int16_t spark_y = center_y + spark_dist * sin(angle) * 0.85;

                lv_area_t spark_area;
                spark_area.x1 = spark_x - 2;
                spark_area.y1 = spark_y - 2;
                spark_area.x2 = spark_x + 2;
                spark_area.y2 = spark_y + 2;

                lv_draw_rect(&layer, &rect_dsc, &spark_area);
            }
        }
    }
    else if (openness > 20)
    {

        rect_dsc.bg_color = lv_color_white();
        rect_dsc.bg_opa = LV_OPA_COVER;
        rect_dsc.border_color = lv_color_black();
        rect_dsc.border_width = 3;
        rect_dsc.border_opa = LV_OPA_COVER;
        rect_dsc.radius = 15;

        lv_area_t eye_area;
        eye_area.x1 = center_x - eye_width / 2;
        eye_area.y1 = center_y - eye_height / 2;
        eye_area.x2 = center_x + eye_width / 2;
        eye_area.y2 = center_y + eye_height / 2;

        lv_draw_rect(&layer, &rect_dsc, &eye_area);

        if (openness > 30 && eye_height > 16)
        {
            int16_t iris_width = eye_width * 0.55;
            int16_t iris_height = eye_height * 0.75;
            if (iris_height > iris_width)
                iris_height = iris_width;

            int16_t iris_center_x = center_x + face_state.pupil_offset_x;
            int16_t iris_center_y = center_y + face_state.pupil_offset_y;

            if (iris_center_x - iris_width / 2 < center_x - eye_width / 2 + 3)
            {
                iris_center_x = center_x - eye_width / 2 + iris_width / 2 + 3;
            }
            if (iris_center_x + iris_width / 2 > center_x + eye_width / 2 - 3)
            {
                iris_center_x = center_x + eye_width / 2 - iris_width / 2 - 3;
            }
            if (iris_center_y - iris_height / 2 < center_y - eye_height / 2 + 3)
            {
                iris_center_y = center_y - eye_height / 2 + iris_height / 2 + 3;
            }
            if (iris_center_y + iris_height / 2 > center_y + eye_height / 2 - 3)
            {
                iris_center_y = center_y + eye_height / 2 - iris_height / 2 - 3;
            }

            rect_dsc.bg_color = lv_color_make(50, 180, 255);
            rect_dsc.border_width = 2;
            rect_dsc.border_color = lv_color_make(30, 140, 230);
            rect_dsc.radius = 8;

            lv_area_t iris_area;
            iris_area.x1 = iris_center_x - iris_width / 2;
            iris_area.y1 = iris_center_y - iris_height / 2;
            iris_area.x2 = iris_center_x + iris_width / 2;
            iris_area.y2 = iris_center_y + iris_height / 2;

            lv_draw_rect(&layer, &rect_dsc, &iris_area);

            int16_t pupil_width = iris_width * 0.5;
            int16_t pupil_height = iris_height * 0.6;
            rect_dsc.bg_color = lv_color_black();
            rect_dsc.border_width = 0;
            rect_dsc.radius = 6;

            lv_area_t pupil_area;
            pupil_area.x1 = iris_center_x - pupil_width / 2;
            pupil_area.y1 = iris_center_y - pupil_height / 2;
            pupil_area.x2 = iris_center_x + pupil_width / 2;
            pupil_area.y2 = iris_center_y + pupil_height / 2;

            lv_draw_rect(&layer, &rect_dsc, &pupil_area);

            int16_t highlight_w = pupil_width * 0.4;
            int16_t highlight_h = pupil_height * 0.4;
            if (highlight_w < 4)
                highlight_w = 4;
            if (highlight_h < 4)
                highlight_h = 4;

            rect_dsc.bg_color = lv_color_white();
            rect_dsc.radius = 3;

            lv_area_t highlight_area;
            highlight_area.x1 = iris_center_x - pupil_width / 3 - highlight_w / 2;
            highlight_area.y1 = iris_center_y - pupil_height / 3 - highlight_h / 2;
            highlight_area.x2 = iris_center_x - pupil_width / 3 + highlight_w / 2;
            highlight_area.y2 = iris_center_y - pupil_height / 3 + highlight_h / 2;

            lv_draw_rect(&layer, &rect_dsc, &highlight_area);

            int16_t small_w = highlight_w / 2;
            int16_t small_h = highlight_h / 2;
            if (small_w < 2)
                small_w = 2;
            if (small_h < 2)
                small_h = 2;

            rect_dsc.radius = 2;

            highlight_area.x1 = iris_center_x + pupil_width / 4 - small_w / 2;
            highlight_area.y1 = iris_center_y - pupil_height / 4 - small_h / 2;
            highlight_area.x2 = iris_center_x + pupil_width / 4 + small_w / 2;
            highlight_area.y2 = iris_center_y - pupil_height / 4 + small_h / 2;

            lv_draw_rect(&layer, &rect_dsc, &highlight_area);
        }

        if (face_state.sparkle_phase > 0)
        {
            rect_dsc.bg_color = lv_color_make(255, 255, 100);
            rect_dsc.bg_opa = (face_state.sparkle_phase * LV_OPA_COVER) / 100;
            rect_dsc.border_width = 0;
            rect_dsc.radius = 2;

            for (int i = 0; i < 3; i++)
            {
                float angle = (i * 120 + face_state.sparkle_phase * 3.6) * 3.14159 / 180.0;
                int16_t spark_x = center_x + (eye_width / 2 + 8) * cos(angle);
                int16_t spark_y = center_y + (eye_width / 2 + 8) * sin(angle);

                lv_area_t spark_area;
                spark_area.x1 = spark_x - 2;
                spark_area.y1 = spark_y - 2;
                spark_area.x2 = spark_x + 2;
                spark_area.y2 = spark_y + 2;

                lv_draw_rect(&layer, &rect_dsc, &spark_area);
            }
        }
    }
    else
    {

        line_dsc.color = lv_color_black();
        line_dsc.width = 4;
        line_dsc.opa = LV_OPA_COVER;
        line_dsc.round_start = 1;
        line_dsc.round_end = 1;

        line_dsc.p1.x = center_x - eye_width / 2;
        line_dsc.p1.y = center_y;
        line_dsc.p2.x = center_x + eye_width / 2;
        line_dsc.p2.y = center_y;
        lv_draw_line(&layer, &line_dsc);

        line_dsc.width = 2;
        for (int i = 0; i < 4; i++)
        {
            int16_t x = center_x - eye_width / 3 + (eye_width * i / 4);
            int16_t lash_length = 6;

            line_dsc.p1.x = x;
            line_dsc.p1.y = center_y;
            line_dsc.p2.x = x + (is_left ? -2 : 2);
            line_dsc.p2.y = center_y - lash_length;
            lv_draw_line(&layer, &line_dsc);
        }
    }

    bool show_sweat = (face_state.current_emotion == FACE_WORKING_HARD) ||
                      (face_state.current_emotion == FACE_SLEEPY && is_left);
    if (show_sweat)
    {
        bool is_working = (face_state.current_emotion == FACE_WORKING_HARD);

        lv_draw_rect_dsc_t sweat_dsc;
        lv_draw_rect_dsc_init(&sweat_dsc);

        uint8_t drop_offset;
        if (is_working)
        {
            drop_offset = is_left ? face_state.sweat_drop_offset
                                  : (uint8_t)((face_state.sweat_drop_offset + 50) % 100);
        }
        else
        {
            drop_offset = face_state.sweat_drop_offset;
        }

        int16_t drop_x = is_left ? (center_x - eye_width / 2 + 2)
                                 : (center_x + eye_width / 2 - 2);
        int16_t drop_start_y = eyebrow_y - 8;
        if (drop_start_y < 2)
            drop_start_y = 2;
        int16_t drop_range = (int16_t)height - 6 - drop_start_y;
        if (drop_range < 10)
            drop_range = 10;
        int16_t drop_y = drop_start_y + (drop_offset * drop_range) / 100;

        int16_t drop_w = is_working ? 4 : 3;
        int16_t drop_top = is_working ? 10 : 7;
        int16_t drop_bot = is_working ? 4 : 3;

        sweat_dsc.bg_color = lv_color_make(120, 200, 255);
        sweat_dsc.bg_opa = is_working ? LV_OPA_90 : LV_OPA_70;
        sweat_dsc.border_color = lv_color_make(80, 150, 240);
        sweat_dsc.border_width = 1;
        sweat_dsc.border_opa = LV_OPA_60;
        sweat_dsc.radius = 6;

        lv_area_t drop_area;
        drop_area.x1 = drop_x - drop_w;
        drop_area.y1 = drop_y - drop_top;
        drop_area.x2 = drop_x + drop_w;
        drop_area.y2 = drop_y + drop_bot;
        lv_draw_rect(&layer, &sweat_dsc, &drop_area);

        sweat_dsc.bg_color = lv_color_white();
        sweat_dsc.bg_opa = LV_OPA_80;
        sweat_dsc.border_width = 0;
        sweat_dsc.radius = 3;

        lv_area_t shine_area;
        int16_t shine_w = is_working ? 2 : 1;
        shine_area.x1 = drop_x - shine_w;
        shine_area.y1 = drop_y - drop_top + 2;
        shine_area.x2 = drop_x;
        shine_area.y2 = drop_y - drop_top + (is_working ? 5 : 4);
        lv_draw_rect(&layer, &sweat_dsc, &shine_area);
    }

    if (face_state.current_emotion == FACE_CRY && openness > 30)
    {
        lv_draw_rect_dsc_t rect_dsc;
        lv_draw_rect_dsc_init(&rect_dsc);

        rect_dsc.bg_color = lv_color_make(150, 200, 255);
        rect_dsc.bg_opa = LV_OPA_80;
        rect_dsc.border_width = 0;
        rect_dsc.radius = 5;

        int16_t tear_x = center_x + (is_left ? -eye_width / 3 : eye_width / 3);
        int16_t tear_y = center_y + eye_height / 2 + 5 + face_state.tear_fall_offset;

        lv_area_t tear_area;
        tear_area.x1 = tear_x - 3;
        tear_area.y1 = tear_y - 5;
        tear_area.x2 = tear_x + 3;
        tear_area.y2 = tear_y + 5;
        lv_draw_rect(&layer, &rect_dsc, &tear_area);

        line_dsc.color = lv_color_make(150, 200, 255);
        line_dsc.width = 2;
        line_dsc.opa = LV_OPA_40;
        line_dsc.round_start = 1;
        line_dsc.round_end = 1;

        line_dsc.p1.x = tear_x;
        line_dsc.p1.y = center_y + eye_height / 2 + 2;
        line_dsc.p2.x = tear_x + (is_left ? -1 : 1);
        line_dsc.p2.y = tear_y - 5;
        lv_draw_line(&layer, &line_dsc);
    }

    lv_canvas_finish_layer(canvas, &layer);
}

static void draw_mouth(lv_obj_t *canvas, int8_t curve)
{
    if (!canvas)
        return;

    uint16_t width = face_state.mouth_cw;
    uint16_t height = face_state.mouth_ch;

    lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    int16_t center_x = width / 2;
    int16_t mouth_width = width * 0.85;

    int16_t curve_offset = (height * curve) / 140;

    int16_t center_y = height / 2 + face_state.bounce_offset;

    int16_t margin = 5;
    int16_t min_y = margin;
    int16_t max_y = height - margin;

    if (curve > 35 && curve < 65)
    {
        int16_t sparkle_distance = mouth_width / 4;

        int16_t min_center = min_y + sparkle_distance - curve_offset;
        int16_t max_center = max_y - sparkle_distance - curve_offset;
        if (center_y < min_center)
            center_y = min_center;
        if (center_y > max_center)
            center_y = max_center;
    }

    else if (curve > 65)
    {
        int16_t mouth_h = height * 0.5;

        int16_t half_offset = curve_offset / 2;
        int16_t half_mouth = mouth_h / 2 + 5;
        int16_t min_center = min_y + half_mouth - half_offset;
        int16_t max_center = max_y - half_mouth - half_offset;
        if (center_y < min_center)
            center_y = min_center;
        if (center_y > max_center)
            center_y = max_center;
    }

    else if (curve < -35)
    {
        int16_t mouth_h = height * 0.35;

        int16_t half_offset = curve_offset / 2;
        int16_t min_center = min_y - half_offset;
        int16_t max_center = max_y - mouth_h - half_offset - 5;
        if (center_y < min_center)
            center_y = min_center;
        if (center_y > max_center)
            center_y = max_center;
    }
    else
    {

        int16_t extent = abs(curve_offset) + 10;
        if (center_y - extent < min_y)
            center_y = min_y + extent;
        if (center_y + extent > max_y)
            center_y = max_y - extent;
    }

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);

    if (face_state.current_emotion == FACE_WORKING_HARD)
    {
        int16_t mouth_h = height * 0.28;
        int16_t grip_width = mouth_width * 0.78;
        int16_t adjusted_y = center_y - mouth_h / 2;

        if (adjusted_y < 4)
            adjusted_y = 4;
        if (adjusted_y + mouth_h > height - 4)
            adjusted_y = height - 4 - mouth_h;

        rect_dsc.bg_color = lv_color_make(200, 60, 80);
        rect_dsc.bg_opa = LV_OPA_COVER;
        rect_dsc.border_color = lv_color_black();
        rect_dsc.border_width = 3;
        rect_dsc.border_opa = LV_OPA_COVER;
        rect_dsc.radius = 8;

        lv_area_t mouth_area;
        mouth_area.x1 = center_x - grip_width / 2;
        mouth_area.y1 = adjusted_y;
        mouth_area.x2 = center_x + grip_width / 2;
        mouth_area.y2 = adjusted_y + mouth_h;
        lv_draw_rect(&layer, &rect_dsc, &mouth_area);

        int16_t t_margin = 4;
        rect_dsc.bg_color = lv_color_make(245, 245, 240);
        rect_dsc.bg_opa = LV_OPA_90;
        rect_dsc.border_width = 0;
        rect_dsc.radius = 3;

        lv_area_t teeth_area;
        teeth_area.x1 = mouth_area.x1 + t_margin;
        teeth_area.y1 = adjusted_y + t_margin;
        teeth_area.x2 = mouth_area.x2 - t_margin;
        teeth_area.y2 = adjusted_y + mouth_h - t_margin;
        lv_draw_rect(&layer, &rect_dsc, &teeth_area);

        line_dsc.color = lv_color_make(180, 180, 170);
        line_dsc.width = 1;
        line_dsc.opa = LV_OPA_70;
        line_dsc.round_start = 1;
        line_dsc.round_end = 1;

        int16_t teeth_total_w = teeth_area.x2 - teeth_area.x1;
        for (int i = 1; i < 4; i++)
        {
            int16_t tooth_x = teeth_area.x1 + (teeth_total_w * i) / 4;
            line_dsc.p1.x = tooth_x;
            line_dsc.p1.y = teeth_area.y1;
            line_dsc.p2.x = tooth_x;
            line_dsc.p2.y = teeth_area.y2;
            lv_draw_line(&layer, &line_dsc);
        }
    }

    else if (curve > 65)
    {

        int16_t mouth_h = height * 0.5;
        int16_t adjusted_y = center_y + (curve_offset / 2);

        rect_dsc.bg_color = lv_color_make(220, 60, 80);
        rect_dsc.bg_opa = LV_OPA_90;
        rect_dsc.border_color = lv_color_black();
        rect_dsc.border_width = 3;
        rect_dsc.border_opa = LV_OPA_COVER;
        rect_dsc.radius = 12;

        lv_area_t mouth_area;
        mouth_area.x1 = center_x - mouth_width / 2;
        mouth_area.y1 = adjusted_y - mouth_h / 2;
        mouth_area.x2 = center_x + mouth_width / 2;
        mouth_area.y2 = adjusted_y + mouth_h / 2;

        lv_draw_rect(&layer, &rect_dsc, &mouth_area);

        if (curve > 100)
        {
            rect_dsc.bg_color = lv_color_make(255, 140, 160);
            rect_dsc.bg_opa = LV_OPA_90;
            rect_dsc.border_color = lv_color_make(200, 80, 100);
            rect_dsc.border_width = 2;
            rect_dsc.radius = 8;

            lv_area_t tongue_area;
            int16_t tongue_w = mouth_width / 5;
            int16_t tongue_h = mouth_h / 3;
            tongue_area.x1 = center_x - tongue_w / 2;
            tongue_area.y1 = adjusted_y + mouth_h / 5;
            tongue_area.x2 = center_x + tongue_w / 2;
            tongue_area.y2 = adjusted_y + mouth_h / 5 + tongue_h;
            lv_draw_rect(&layer, &rect_dsc, &tongue_area);
        }

        if (curve > 85)
        {
            rect_dsc.bg_color = lv_color_make(255, 255, 180);
            rect_dsc.bg_opa = LV_OPA_60;
            rect_dsc.radius = 2;

            for (int i = 0; i < 2; i++)
            {
                int16_t side = (i == 0) ? -1 : 1;
                int16_t spark_x = center_x + side * (mouth_width / 2 + 8);
                int16_t spark_y = adjusted_y;

                lv_area_t spark_area;
                spark_area.x1 = spark_x - 2;
                spark_area.y1 = spark_y - 2;
                spark_area.x2 = spark_x + 2;
                spark_area.y2 = spark_y + 2;
                lv_draw_rect(&layer, &rect_dsc, &spark_area);
            }
        }
    }

    else if (curve > 35 && curve < 65)
    {

        float diamond_factor = face_state.diamond_mouth_phase / 100.0;

        if (diamond_factor > 0.3)
        {

            int16_t stretch = 3 + (diamond_factor * 8);

            rect_dsc.bg_color = lv_color_make(200, 70, 90);
            rect_dsc.bg_opa = LV_OPA_90;
            rect_dsc.border_color = lv_color_black();
            rect_dsc.border_width = 3;
            rect_dsc.border_opa = LV_OPA_COVER;
            rect_dsc.radius = 4;

            lv_area_t diamond_area;
            diamond_area.x1 = center_x - 6;
            diamond_area.y1 = center_y + curve_offset - stretch - 6;
            diamond_area.x2 = center_x + 6;
            diamond_area.y2 = center_y + curve_offset - 2;
            lv_draw_rect(&layer, &rect_dsc, &diamond_area);

            diamond_area.x1 = center_x + 2;
            diamond_area.y1 = center_y + curve_offset - 6;
            diamond_area.x2 = center_x + stretch + 6;
            diamond_area.y2 = center_y + curve_offset + 6;
            lv_draw_rect(&layer, &rect_dsc, &diamond_area);

            diamond_area.x1 = center_x - 6;
            diamond_area.y1 = center_y + curve_offset + 2;
            diamond_area.x2 = center_x + 6;
            diamond_area.y2 = center_y + curve_offset + stretch + 6;
            lv_draw_rect(&layer, &rect_dsc, &diamond_area);

            diamond_area.x1 = center_x - stretch - 6;
            diamond_area.y1 = center_y + curve_offset - 6;
            diamond_area.x2 = center_x - 2;
            diamond_area.y2 = center_y + curve_offset + 6;
            lv_draw_rect(&layer, &rect_dsc, &diamond_area);

            rect_dsc.border_width = 0;
            rect_dsc.radius = 2;
            diamond_area.x1 = center_x - 4;
            diamond_area.y1 = center_y + curve_offset - 4;
            diamond_area.x2 = center_x + 4;
            diamond_area.y2 = center_y + curve_offset + 4;
            lv_draw_rect(&layer, &rect_dsc, &diamond_area);
        }
        else
        {

            int16_t mouth_width_oval = mouth_width / 3.5;
            int16_t mouth_height_oval = mouth_width / 4;

            rect_dsc.bg_color = lv_color_make(200, 70, 90);
            rect_dsc.bg_opa = LV_OPA_90;
            rect_dsc.border_color = lv_color_black();
            rect_dsc.border_width = 3;
            rect_dsc.border_opa = LV_OPA_COVER;
            rect_dsc.radius = 8;

            lv_area_t mouth_area;
            mouth_area.x1 = center_x - mouth_width_oval;
            mouth_area.y1 = center_y + curve_offset - mouth_height_oval;
            mouth_area.x2 = center_x + mouth_width_oval;
            mouth_area.y2 = center_y + curve_offset + mouth_height_oval;

            lv_draw_rect(&layer, &rect_dsc, &mouth_area);
        }

        rect_dsc.bg_color = lv_color_make(255, 255, 150);
        rect_dsc.bg_opa = LV_OPA_70;
        rect_dsc.border_width = 0;
        rect_dsc.radius = 2;

        for (int i = 0; i < 4; i++)
        {
            float angle = i * 90 * 3.14159 / 180.0;
            int16_t spark_x = center_x + (mouth_width / 3) * cos(angle);
            int16_t spark_y = center_y + curve_offset + (mouth_width / 3) * sin(angle);

            lv_area_t spark_area;
            spark_area.x1 = spark_x - 2;
            spark_area.y1 = spark_y - 2;
            spark_area.x2 = spark_x + 2;
            spark_area.y2 = spark_y + 2;
            lv_draw_rect(&layer, &rect_dsc, &spark_area);
        }
    }

    else if (curve < -35)
    {
        int16_t mouth_h = height * 0.35;
        int16_t adjusted_y = center_y + (curve_offset / 2);

        rect_dsc.bg_color = lv_color_make(180, 50, 70);
        rect_dsc.bg_opa = LV_OPA_90;
        rect_dsc.border_color = lv_color_black();
        rect_dsc.border_width = 3;
        rect_dsc.border_opa = LV_OPA_COVER;
        rect_dsc.radius = 8;

        lv_area_t mouth_area;
        mouth_area.x1 = center_x - mouth_width / 2;
        mouth_area.y1 = adjusted_y;
        mouth_area.x2 = center_x + mouth_width / 2;
        mouth_area.y2 = adjusted_y + mouth_h;

        lv_draw_rect(&layer, &rect_dsc, &mouth_area);

        if (curve < -50)
        {
            rect_dsc.bg_color = lv_color_make(150, 200, 255);
            rect_dsc.bg_opa = LV_OPA_70;
            rect_dsc.border_width = 0;
            rect_dsc.radius = 4;

            int16_t tear_base_y = center_y - 8;
            int16_t tear_y = tear_base_y + face_state.tear_fall_offset;

            int16_t tear_x_left = center_x - mouth_width / 2 - 10;

            lv_area_t tear_area;
            tear_area.x1 = tear_x_left - 4;
            tear_area.y1 = tear_y - 4;
            tear_area.x2 = tear_x_left + 4;
            tear_area.y2 = tear_y + 4;
            lv_draw_rect(&layer, &rect_dsc, &tear_area);

            int16_t tear_x_right = center_x + mouth_width / 2 + 10;
            tear_area.x1 = tear_x_right - 4;
            tear_area.y1 = tear_y - 4;
            tear_area.x2 = tear_x_right + 4;
            tear_area.y2 = tear_y + 4;
            lv_draw_rect(&layer, &rect_dsc, &tear_area);

            line_dsc.color = lv_color_make(150, 200, 255);
            line_dsc.width = 2;
            line_dsc.opa = LV_OPA_50;
            line_dsc.round_start = 1;
            line_dsc.round_end = 1;

            line_dsc.p1.x = tear_x_left;
            line_dsc.p1.y = tear_base_y;
            line_dsc.p2.x = tear_x_left - 1;
            line_dsc.p2.y = tear_y - 4;
            lv_draw_line(&layer, &line_dsc);

            line_dsc.p1.x = tear_x_right;
            line_dsc.p1.y = tear_base_y;
            line_dsc.p2.x = tear_x_right + 1;
            line_dsc.p2.y = tear_y - 4;
            lv_draw_line(&layer, &line_dsc);
        }
    }

    else
    {
        int16_t mouth_h = height * 0.28;
        int16_t smile_width = mouth_width * 0.65;

        bool is_slight_smile = (curve > 5);

        if (is_slight_smile)
        {

            rect_dsc.bg_color = lv_color_make(210, 80, 100);
            rect_dsc.bg_opa = LV_OPA_80;
        }
        else
        {

            rect_dsc.bg_color = lv_color_make(190, 60, 80);
            rect_dsc.bg_opa = LV_OPA_90;
        }

        rect_dsc.border_color = lv_color_black();
        rect_dsc.border_width = 2;
        rect_dsc.border_opa = LV_OPA_COVER;
        rect_dsc.radius = 6;

        lv_area_t mouth_area;
        mouth_area.x1 = center_x - smile_width / 2;
        mouth_area.y1 = center_y;
        mouth_area.x2 = center_x + smile_width / 2;
        mouth_area.y2 = center_y + mouth_h;

        lv_draw_rect(&layer, &rect_dsc, &mouth_area);
    }

    lv_canvas_finish_layer(canvas, &layer);
}

static void update_emotion_parameters(face_emotion_t emotion, uint8_t *left_eye, uint8_t *right_eye, int8_t *mouth,
                                      int8_t *left_brow, int8_t *right_brow, int8_t *brow_height)
{
    switch (emotion)
    {
    case FACE_NEUTRAL:
        *left_eye = 100;
        *right_eye = 100;
        *mouth = 0;
        *left_brow = 0;
        *right_brow = 0;
        *brow_height = 0;
        face_state.blush_intensity = 0;
        break;

    case FACE_HAPPY:
        *left_eye = 96;
        *right_eye = 96;
        *mouth = 90;
        *left_brow = -4;
        *right_brow = -4;
        *brow_height = -5;
        face_state.blush_intensity = 82;
        face_state.sparkle_phase = 90;
        face_state.heart_beat_phase = 40;
        break;

    case FACE_WORRIED:
        *left_eye = 78;
        *right_eye = 78;
        *mouth = 28;
        *left_brow = 18;
        *right_brow = 18;
        *brow_height = -7;
        face_state.blush_intensity = 20;
        face_state.sparkle_phase = 0;
        face_state.heart_beat_phase = 0;
        break;

    case FACE_SAD:
        *left_eye = 60;
        *right_eye = 60;
        *mouth = -75;
        *left_brow = -15;
        *right_brow = 15;
        *brow_height = 3;
        face_state.blush_intensity = 0;
        face_state.sparkle_phase = 0;
        face_state.heart_beat_phase = 0;
        break;

    case FACE_SURPRISED:
        *left_eye = 100;
        *right_eye = 100;
        *mouth = 50;
        *left_brow = 0;
        *right_brow = 0;
        *brow_height = -10;
        face_state.blush_intensity = 20;
        face_state.sparkle_phase = 60;
        face_state.heart_beat_phase = 0;
        break;

    case FACE_ANGRY:
        *left_eye = 75;
        *right_eye = 75;
        *mouth = -45;
        *left_brow = 25;
        *right_brow = -25;
        *brow_height = 5;
        face_state.blush_intensity = 50;
        face_state.sparkle_phase = 0;
        face_state.heart_beat_phase = 0;
        break;

    case FACE_SLEEPY:
        *left_eye = 35;
        *right_eye = 35;
        *mouth = -5;
        *left_brow = -5;
        *right_brow = 5;
        *brow_height = 8;
        face_state.blush_intensity = 30;
        face_state.sparkle_phase = 0;
        face_state.heart_beat_phase = 0;
        break;

    case FACE_WINK:
        *left_eye = 85;
        *right_eye = 15;
        *mouth = 70;
        *left_brow = 8;
        *right_brow = -8;
        *brow_height = -2;
        face_state.blush_intensity = 60;
        face_state.sparkle_phase = 75;
        face_state.heart_beat_phase = 0;
        break;

    case FACE_LOVE:
        *left_eye = 95;
        *right_eye = 95;
        *mouth = 80;
        *left_brow = 3;
        *right_brow = 3;
        *brow_height = -3;
        face_state.blush_intensity = 90;
        face_state.sparkle_phase = 100;
        face_state.heart_beat_phase = 100;
        break;

    case FACE_PLAYFUL:
        *left_eye = 78;
        *right_eye = 80;
        *mouth = 110;
        *left_brow = 12;
        *right_brow = -8;
        *brow_height = 0;
        face_state.blush_intensity = 45;
        face_state.sparkle_phase = 85;
        face_state.heart_beat_phase = 0;
        break;

    case FACE_SILLY:
        *left_eye = 95;
        *right_eye = 92;
        *mouth = 75;
        *left_brow = 25;
        *right_brow = -18;
        *brow_height = 4;
        face_state.blush_intensity = 55;
        face_state.sparkle_phase = 65;
        face_state.heart_beat_phase = 0;
        break;

    case FACE_SMIRK:
        *left_eye = 80;
        *right_eye = 75;
        *mouth = 40;
        *left_brow = 15;
        *right_brow = -5;
        *brow_height = -5;
        face_state.blush_intensity = 25;
        face_state.sparkle_phase = 50;
        face_state.heart_beat_phase = 0;
        break;

    case FACE_CRY:
        *left_eye = 70;
        *right_eye = 70;
        *mouth = -70;
        *left_brow = -15;
        *right_brow = 15;
        *brow_height = 8;
        face_state.blush_intensity = 35;
        face_state.sparkle_phase = 0;
        face_state.heart_beat_phase = 0;
        break;

    case FACE_WORKING_HARD:
        *left_eye = 65;
        *right_eye = 65;
        *mouth = 0;
        *left_brow = 22;
        *right_brow = -22;
        *brow_height = 4;
        face_state.blush_intensity = 60;
        face_state.sparkle_phase = 0;
        face_state.heart_beat_phase = 0;
        break;

    case FACE_EXCITED:
        *left_eye = 100;
        *right_eye = 100;
        *mouth = 95;
        *left_brow = 8;
        *right_brow = 8;
        *brow_height = -8;
        face_state.blush_intensity = 85;
        face_state.sparkle_phase = 100;
        face_state.heart_beat_phase = 80;
        break;

    case FACE_CONFUSED:
        *left_eye = 88;
        *right_eye = 75;
        *mouth = 12;
        *left_brow = -18;
        *right_brow = 8;
        *brow_height = -3;
        face_state.blush_intensity = 15;
        face_state.sparkle_phase = 0;
        face_state.heart_beat_phase = 0;
        break;

    case FACE_COOL:
        *left_eye = 48;
        *right_eye = 48;
        *mouth = 35;
        *left_brow = 5;
        *right_brow = -3;
        *brow_height = -4;
        face_state.blush_intensity = 10;
        face_state.sparkle_phase = 40;
        face_state.heart_beat_phase = 0;
        break;

    default:
        *left_eye = 100;
        *right_eye = 100;
        *mouth = 0;
        *left_brow = 0;
        *right_brow = 0;
        *brow_height = 0;
        face_state.blush_intensity = 0;
        face_state.sparkle_phase = 0;
        face_state.heart_beat_phase = 0;
        break;
    }
}

static void animation_timer_cb(lv_timer_t *timer)
{
    if (!face_state.initialized)
        return;

    uint32_t current_time = lv_tick_get();
    bool needs_redraw = false;

    if (face_state.is_blinking)
    {
        face_state.blink_phase += 20;
        if (face_state.blink_phase >= 100)
        {
            face_state.blink_phase = 0;
            face_state.is_blinking = false;
            face_state.last_blink_time = current_time;
        }

        uint8_t blink_openness;
        if (face_state.blink_phase < 50)
        {
            blink_openness = 100 - (face_state.blink_phase * 2);
        }
        else
        {
            blink_openness = (face_state.blink_phase - 50) * 2;
        }

        face_state.left_eye_openness = blink_openness;
        face_state.right_eye_openness = blink_openness;
        needs_redraw = true;
    }

    else if (face_state.config.auto_blink &&
             (current_time - face_state.last_blink_time) > face_state.config.blink_interval)
    {
        face_trigger_blink();
    }

    else if (face_state.current_emotion != face_state.target_emotion &&
             face_state.transition_progress < 100)
    {
        face_state.transition_progress += 10;

        if (face_state.transition_progress >= 100)
        {
            face_state.transition_progress = 100;
            face_state.current_emotion = face_state.target_emotion;
        }

        uint8_t target_left, target_right;
        int8_t target_mouth, target_left_brow, target_right_brow, target_brow_height;
        update_emotion_parameters(face_state.target_emotion, &target_left, &target_right, &target_mouth,
                                  &target_left_brow, &target_right_brow, &target_brow_height);

        uint8_t current_left, current_right;
        int8_t current_mouth, current_left_brow, current_right_brow, current_brow_height;
        update_emotion_parameters(face_state.current_emotion, &current_left, &current_right, &current_mouth,
                                  &current_left_brow, &current_right_brow, &current_brow_height);

        face_state.left_eye_openness = current_left +
                                       ((target_left - current_left) * face_state.transition_progress) / 100;
        face_state.right_eye_openness = current_right +
                                        ((target_right - current_right) * face_state.transition_progress) / 100;
        face_state.mouth_curve = current_mouth +
                                 ((target_mouth - current_mouth) * face_state.transition_progress) / 100;
        face_state.left_eyebrow_angle = current_left_brow +
                                        ((target_left_brow - current_left_brow) * face_state.transition_progress) / 100;
        face_state.right_eyebrow_angle = current_right_brow +
                                         ((target_right_brow - current_right_brow) * face_state.transition_progress) / 100;
        face_state.eyebrow_height = current_brow_height +
                                    ((target_brow_height - current_brow_height) * face_state.transition_progress) / 100;

        needs_redraw = true;
    }

    static uint32_t bounce_counter = 0;
    static uint32_t pupil_counter = 0;
    bounce_counter++;
    pupil_counter++;

    switch (face_state.current_emotion)
    {
    case FACE_HAPPY:

    {
        float ha = (pupil_counter % 80) * 0.1572f;
        face_state.pupil_offset_x = (int8_t)(7.0f * cosf(ha));
        face_state.pupil_offset_y = (int8_t)(4.0f * sinf(ha));
        if (pupil_counter % 2 == 0)
            needs_redraw = true;
    }
    break;

    case FACE_WORRIED:

        face_state.pupil_offset_x = (int8_t)(5.0f * sinf(pupil_counter * 0.06f));
        face_state.pupil_offset_y = (int8_t)(1.0f * sinf(pupil_counter * 0.09f));
        if (pupil_counter % 4 == 0)
            needs_redraw = true;
        break;

    case FACE_PLAYFUL:
    case FACE_LOVE:

        if (pupil_counter % 100 < 50)
        {
            float angle = (pupil_counter % 100) * 0.125;
            face_state.pupil_offset_x = 6 * cos(angle);
            face_state.pupil_offset_y = 4 * sin(angle);
            if (pupil_counter % 2 == 0)
                needs_redraw = true;
        }
        else
        {

            face_state.pupil_offset_x = face_state.pupil_offset_x * 0.8;
            face_state.pupil_offset_y = face_state.pupil_offset_y * 0.8;
            if (pupil_counter % 3 == 0)
                needs_redraw = true;
        }
        break;

    case FACE_SURPRISED:

        face_state.pupil_offset_x = 0;
        face_state.pupil_offset_y = -8;
        break;

    case FACE_SLEEPY:

        face_state.pupil_offset_x = 0;
        face_state.pupil_offset_y = 5;
        break;

    case FACE_SILLY:

        face_state.pupil_offset_x = ((pupil_counter / 5) % 2) ? 10 : -10;
        face_state.pupil_offset_y = 0;
        if (pupil_counter % 5 == 0)
            needs_redraw = true;
        break;

    case FACE_WINK:
    case FACE_SMIRK:

        face_state.pupil_offset_x = 5;
        face_state.pupil_offset_y = 0;
        break;

    case FACE_WORKING_HARD:

        face_state.pupil_offset_x = 0;
        face_state.pupil_offset_y = 4;
        break;

    case FACE_EXCITED:

        face_state.pupil_offset_x = (int8_t)(((pupil_counter / 3) % 2) ? 9 : -9);
        face_state.pupil_offset_y = (int8_t)(((pupil_counter / 5) % 2) ? 7 : -7);
        if (pupil_counter % 3 == 0)
            needs_redraw = true;
        break;

    case FACE_CONFUSED:

        face_state.pupil_offset_x = (int8_t)(7.0f * cosf(pupil_counter * 0.03f));
        face_state.pupil_offset_y = (int8_t)(5.0f * sinf(pupil_counter * 0.05f));
        if (pupil_counter % 2 == 0)
            needs_redraw = true;
        break;

    case FACE_COOL:
    {

        uint32_t cp = pupil_counter % 240;
        if (cp < 60)
        {
            face_state.pupil_offset_x = (int8_t)(8.0f * (cp / 60.0f));
            face_state.pupil_offset_y = 0;
        }
        else if (cp < 120)
        {
            face_state.pupil_offset_x = 8;
            face_state.pupil_offset_y = 0;
        }
        else if (cp < 180)
        {
            face_state.pupil_offset_x = (int8_t)(8.0f * (1.0f - (cp - 120) / 60.0f));
            face_state.pupil_offset_y = 0;
        }
        else
        {
            face_state.pupil_offset_x = 0;
            face_state.pupil_offset_y = 0;
        }
        if (pupil_counter % 3 == 0)
            needs_redraw = true;
        break;
    }

    case FACE_NEUTRAL:
    case FACE_SAD:
    case FACE_CRY:
    case FACE_ANGRY:
    default:

        face_state.pupil_offset_x = 0;
        face_state.pupil_offset_y = 0;
        break;
    }

    if (face_state.current_emotion == FACE_SAD || face_state.current_emotion == FACE_CRY)
    {
        face_state.tear_fall_offset += 2;
        if (face_state.tear_fall_offset > 80)
        {
            face_state.tear_fall_offset = 0;
        }
        needs_redraw = true;
    }
    else
    {
        face_state.tear_fall_offset = 0;
    }

    if (face_state.current_emotion == FACE_WORKING_HARD)
    {
        face_state.sweat_drop_offset += 3;
        if (face_state.sweat_drop_offset > 100)
            face_state.sweat_drop_offset = 0;
        needs_redraw = true;
    }
    else if (face_state.current_emotion == FACE_SLEEPY)
    {
        face_state.sweat_drop_offset += 1;
        if (face_state.sweat_drop_offset > 100)
            face_state.sweat_drop_offset = 0;
        needs_redraw = true;
    }
    else
    {
        face_state.sweat_drop_offset = 0;
    }

    if (face_state.current_emotion == FACE_SURPRISED)
    {

        static int8_t diamond_direction = 1;
        face_state.diamond_mouth_phase += diamond_direction * 8;
        if (face_state.diamond_mouth_phase >= 100)
        {
            face_state.diamond_mouth_phase = 100;
            diamond_direction = -1;
        }
        else if (face_state.diamond_mouth_phase <= 50)
        {
            face_state.diamond_mouth_phase = 50;
            diamond_direction = 1;
        }
        needs_redraw = true;
    }
    else
    {
        face_state.diamond_mouth_phase = 0;
    }

    bool transition_done = (face_state.transition_progress == 100);

    switch (face_state.current_emotion)
    {

    case FACE_HAPPY:

        face_state.bounce_offset = (int8_t)(3.5f * sinf(bounce_counter * 0.28f));

        if (transition_done && !face_state.is_blinking)
        {
            face_state.left_eye_openness = (uint8_t)(87 + (uint8_t)(13 * fabsf(sinf(bounce_counter * 0.28f))));
            face_state.right_eye_openness = face_state.left_eye_openness;
        }

        face_state.sparkle_phase = (uint8_t)(65 + (uint8_t)(35 * fabsf(sinf(bounce_counter * 0.20f))));
        face_state.blush_intensity = (uint8_t)(72 + (uint8_t)(18 * fabsf(sinf(bounce_counter * 0.13f))));

        if (transition_done)
        {
            face_state.mouth_curve = (int8_t)(87 + (int8_t)(8 * fabsf(sinf(bounce_counter * 0.28f))));
        }
        if (bounce_counter % 2 == 0)
            needs_redraw = true;
        break;

    case FACE_WORRIED:

        face_state.bounce_offset = (int8_t)(1.2f * sinf(bounce_counter * 0.10f) + 0.8f * sinf(bounce_counter * 0.23f));

        if (transition_done)
        {
            face_state.left_eyebrow_angle = (int8_t)(16 + (int8_t)(7 * fabsf(sinf(bounce_counter * 0.17f))));
            face_state.right_eyebrow_angle = face_state.left_eyebrow_angle;
            face_state.eyebrow_height = (int8_t)(-6 - (int8_t)(4 * fabsf(sinf(bounce_counter * 0.17f))));

            face_state.mouth_curve = (int8_t)(22 + (int8_t)(12 * fabsf(sinf(bounce_counter * 0.13f))));
        }
        if (bounce_counter % 3 == 0)
            needs_redraw = true;
        break;

    case FACE_LOVE:

        face_state.bounce_offset = (int8_t)(2.0 * sin(bounce_counter * 0.12));

        if (transition_done && !face_state.is_blinking)
        {
            face_state.left_eye_openness = (uint8_t)(88 + 12 * fabs(sin(bounce_counter * 0.15)));
            face_state.right_eye_openness = face_state.left_eye_openness;
        }

        face_state.sparkle_phase = (uint8_t)(72 + 28 * fabs(sin(bounce_counter * 0.25)));
        face_state.heart_beat_phase = (uint8_t)(65 + 35 * fabs(sin(bounce_counter * 0.20)));
        face_state.blush_intensity = (uint8_t)(80 + 15 * fabs(sin(bounce_counter * 0.15)));
        if (bounce_counter % 2 == 0)
            needs_redraw = true;
        break;

    case FACE_ANGRY:

        face_state.blush_intensity = (uint8_t)(40 + 28 * fabs(sin(bounce_counter * 0.3)));
        if (transition_done)
        {

            face_state.mouth_curve = (int8_t)(-42 + (int8_t)(8 * sin(bounce_counter * 0.5)));

            face_state.left_eyebrow_angle = (int8_t)(22 + (int8_t)(5 * sin(bounce_counter * 0.4)));
            face_state.right_eyebrow_angle = (int8_t)(-22 - (int8_t)(5 * sin(bounce_counter * 0.4)));
        }

        face_state.bounce_offset = (bounce_counter % 8 < 2) ? 1 : 0;
        if (bounce_counter % 2 == 0)
            needs_redraw = true;
        break;

    case FACE_SLEEPY:

        face_state.bounce_offset = (int8_t)(3.0 * sin(bounce_counter * 0.04));

        if (transition_done && !face_state.is_blinking)
        {
            int16_t droop = (int16_t)(20 * fabs(sin(bounce_counter * 0.03)));
            int16_t new_open = 35 - droop;
            face_state.left_eye_openness = (uint8_t)(new_open < 10 ? 10 : new_open);
            face_state.right_eye_openness = face_state.left_eye_openness;
        }
        if (bounce_counter % 3 == 0)
            needs_redraw = true;
        break;

    case FACE_SURPRISED:

        face_state.bounce_offset = (bounce_counter % 4) - 2;

        if (transition_done && !face_state.is_blinking)
        {
            face_state.left_eye_openness = (uint8_t)(93 + (int8_t)(7 * fabs(sin(bounce_counter * 0.4))));
            face_state.right_eye_openness = face_state.left_eye_openness;
        }
        if (bounce_counter % 2 == 0)
            needs_redraw = true;
        break;

    case FACE_CRY:

        face_state.bounce_offset = (int8_t)(2 * sin(bounce_counter * 0.6));

        if (transition_done && !face_state.is_blinking)
        {
            int16_t squeeze = (int16_t)(20 * fabs(sin(bounce_counter * 0.3)));
            int16_t new_open = 65 - squeeze;
            face_state.left_eye_openness = (uint8_t)(new_open < 30 ? 30 : new_open);
            face_state.right_eye_openness = face_state.left_eye_openness;
        }
        face_state.blush_intensity = (uint8_t)(27 + (int8_t)(18 * fabs(sin(bounce_counter * 0.3))));
        if (bounce_counter % 2 == 0)
            needs_redraw = true;
        break;

    case FACE_SAD:

        face_state.bounce_offset = (int8_t)(1.5 * sin(bounce_counter * 0.06));

        face_state.pupil_offset_y = (int8_t)(3 + (int8_t)(3 * fabs(sin(bounce_counter * 0.08))));
        if (bounce_counter % 4 == 0)
            needs_redraw = true;
        break;

    case FACE_WINK:

        face_state.sparkle_phase = (uint8_t)(42 + (int8_t)(38 * fabs(sin(bounce_counter * 0.2))));

        face_state.bounce_offset = (int8_t)(1.5 * sin(bounce_counter * 0.25));
        if (bounce_counter % 3 == 0)
            needs_redraw = true;
        break;

    case FACE_SMIRK:

        if (transition_done)
        {
            face_state.left_eyebrow_angle = (int8_t)(12 + (int8_t)(8 * sin(bounce_counter * 0.10)));
            face_state.eyebrow_height = (int8_t)(-5 + (int8_t)(4 * sin(bounce_counter * 0.10)));
        }

        face_state.pupil_offset_x = (int8_t)(3 + (int8_t)(4 * sin(bounce_counter * 0.07)));
        face_state.sparkle_phase = (uint8_t)(25 + (int8_t)(30 * fabs(sin(bounce_counter * 0.15))));
        face_state.bounce_offset = (int8_t)(sin(bounce_counter * 0.10));
        if (bounce_counter % 3 == 0)
            needs_redraw = true;
        break;

    case FACE_PLAYFUL:

        if (transition_done)
        {
            face_state.mouth_curve = (int8_t)(105 + (int8_t)(10 * sin(bounce_counter * 0.35)));
        }
        face_state.sparkle_phase = (uint8_t)(62 + (int8_t)(28 * fabs(sin(bounce_counter * 0.28))));

        face_state.bounce_offset = (int8_t)(2.5 * sin(bounce_counter * 0.30));
        if (bounce_counter % 2 == 0)
            needs_redraw = true;
        break;

    case FACE_SILLY:

        face_state.bounce_offset = (int8_t)(3.5 * sin(bounce_counter * 0.25));
        face_state.sparkle_phase = (uint8_t)(38 + (int8_t)(37 * fabs(sin(bounce_counter * 0.30))));
        if (bounce_counter % 2 == 0)
            needs_redraw = true;
        break;

    case FACE_WORKING_HARD:

        face_state.bounce_offset = (bounce_counter % 6 < 3) ? 1 : -1;
        if (bounce_counter % 6 == 0)
            needs_redraw = true;
        break;

    case FACE_EXCITED:
    {

        face_state.bounce_offset = (int8_t)(3.5f * sinf(bounce_counter * 0.55f));

        if (transition_done && !face_state.is_blinking)
        {
            face_state.left_eye_openness = (uint8_t)(90 + (uint8_t)(10 * fabsf(sinf(bounce_counter * 0.55f))));
            face_state.right_eye_openness = face_state.left_eye_openness;
        }

        face_state.sparkle_phase = (uint8_t)(80 + (uint8_t)(20 * fabsf(sinf(bounce_counter * 0.40f))));
        face_state.blush_intensity = (uint8_t)(75 + (uint8_t)(20 * fabsf(sinf(bounce_counter * 0.20f))));
        if (bounce_counter % 2 == 0)
            needs_redraw = true;
        break;
    }

    case FACE_CONFUSED:
    {

        face_state.bounce_offset = (int8_t)(2.0f * sinf(bounce_counter * 0.07f) + 1.0f * sinf(bounce_counter * 0.19f));
        if (transition_done)
        {

            float brow_wave = sinf(bounce_counter * 0.06f);
            face_state.left_eyebrow_angle = (int8_t)(-18 + (int8_t)(12 * brow_wave));
            face_state.right_eyebrow_angle = (int8_t)(8 - (int8_t)(6 * brow_wave));
            face_state.eyebrow_height = (int8_t)(-3 - (int8_t)(4 * fabsf(brow_wave)));
        }
        if (bounce_counter % 2 == 0)
            needs_redraw = true;
        break;
    }

    case FACE_COOL:
    {

        face_state.bounce_offset = (int8_t)(1.5f * sinf(bounce_counter * 0.04f));

        face_state.sparkle_phase = (uint8_t)(15 + (uint8_t)(30 * fabsf(sinf(bounce_counter * 0.08f))));

        if (transition_done && !face_state.is_blinking)
        {
            uint8_t squint = (uint8_t)(8 * fabsf(sinf(bounce_counter * 0.05f)));
            face_state.left_eye_openness = (uint8_t)(48 - (squint > 38 ? 38 : squint));
            face_state.right_eye_openness = face_state.left_eye_openness;
        }
        if (bounce_counter % 3 == 0)
            needs_redraw = true;
        break;
    }

    case FACE_NEUTRAL:
    {

        static uint32_t idle = 0;
        if (transition_done)
            idle++;

        face_state.bounce_offset = (int8_t)(1.2f * sinf(idle * 0.05f));

        uint32_t gp = idle % 420;
        if (gp < 160)
        {

            face_state.pupil_offset_x = 0;
            face_state.pupil_offset_y = 0;
        }
        else if (gp < 195)
        {

            float t = (gp - 160) / 35.0f;
            face_state.pupil_offset_x = (int8_t)(7.0f * t);
            face_state.pupil_offset_y = 0;
        }
        else if (gp < 240)
        {

            face_state.pupil_offset_x = 7;
            face_state.pupil_offset_y = 0;
        }
        else if (gp < 275)
        {

            float t = (gp - 240) / 35.0f;
            face_state.pupil_offset_x = (int8_t)(7.0f * (1.0f - t));
            face_state.pupil_offset_y = 0;
        }
        else if (gp < 340)
        {

            face_state.pupil_offset_x = 0;
            face_state.pupil_offset_y = 0;
        }
        else if (gp < 368)
        {

            float t = (gp - 340) / 28.0f;
            face_state.pupil_offset_x = (int8_t)(-5.0f * t);
            face_state.pupil_offset_y = (int8_t)(5.0f * t);
        }
        else if (gp < 390)
        {

            face_state.pupil_offset_x = -5;
            face_state.pupil_offset_y = 5;
        }
        else
        {

            float t = (gp - 390) / 30.0f;
            face_state.pupil_offset_x = (int8_t)(-5.0f * (1.0f - t));
            face_state.pupil_offset_y = (int8_t)(5.0f * (1.0f - t));
        }

        if (transition_done)
        {

            uint32_t bp = idle % 280;
            if (bp >= 230 && bp < 280)
            {

                float raw_t = (bp - 230) / 25.0f;
                float intensity = (raw_t <= 1.0f) ? raw_t : (2.0f - raw_t);
                face_state.left_eyebrow_angle = (int8_t)(8.0f * intensity);
                face_state.right_eyebrow_angle = (int8_t)(-2.0f * intensity);
                face_state.eyebrow_height = (int8_t)(-4.0f * intensity);
            }
            else
            {
                face_state.left_eyebrow_angle = 0;
                face_state.right_eyebrow_angle = 0;
                face_state.eyebrow_height = 0;
            }

            uint32_t sp = idle % 360;
            if (sp >= 300 && sp < 360)
            {

                float raw_t = (sp - 300) / 30.0f;
                float intensity = (raw_t <= 1.0f) ? raw_t : (2.0f - raw_t);
                face_state.mouth_curve = (int8_t)(14.0f * intensity);
            }
            else
            {
                face_state.mouth_curve = 0;
            }
        }

        if (idle % 2 == 0)
            needs_redraw = true;
        break;
    }

    default:
        face_state.bounce_offset = (int8_t)(sinf(bounce_counter * 0.1f) * 0.5f);
        if (bounce_counter % 10 == 0)
            needs_redraw = true;
        break;
    }

    if (face_state.current_emotion == FACE_NEUTRAL ||
        face_state.current_emotion == FACE_ANGRY ||
        face_state.current_emotion == FACE_SAD ||
        face_state.current_emotion == FACE_CRY ||
        face_state.current_emotion == FACE_SLEEPY ||
        face_state.current_emotion == FACE_SURPRISED ||
        face_state.current_emotion == FACE_WORKING_HARD ||
        face_state.current_emotion == FACE_CONFUSED ||
        face_state.current_emotion == FACE_WORRIED)
    {
        if (face_state.sparkle_phase > 0)
        {
            face_state.sparkle_phase = (face_state.sparkle_phase >= 2)
                                           ? face_state.sparkle_phase - 2
                                           : 0;
            needs_redraw = true;
        }
    }

    if (face_state.current_emotion != FACE_LOVE)
    {
        if (face_state.heart_beat_phase > 0)
        {
            static int8_t heart_direction = -1;
            face_state.heart_beat_phase += heart_direction * 5;
            if (face_state.heart_beat_phase <= 0)
            {
                face_state.heart_beat_phase = 0;
                heart_direction = 1;
            }
            else if (face_state.heart_beat_phase >= 100)
            {
                face_state.heart_beat_phase = 100;
                heart_direction = -1;
            }
        }
    }

    if (face_state.transition_progress < 100 && face_state.blush_intensity > 0)
    {
        needs_redraw = true;
    }

    if (needs_redraw)
    {
        draw_eye(face_state.left_eye_canvas, face_state.left_eye_openness, true);
        draw_eye(face_state.right_eye_canvas, face_state.right_eye_openness, false);
        draw_mouth(face_state.mouth_canvas, face_state.mouth_curve);
    }
}

void face_set_emotion(face_emotion_t emotion, bool smooth)
{
    if (!face_state.initialized || emotion >= FACE_EMOTION_COUNT)
        return;

    face_state.target_emotion = emotion;

    if (!smooth)
    {
        face_state.current_emotion = emotion;
        face_state.transition_progress = 100;

        uint8_t left, right;
        int8_t mouth, left_brow, right_brow, brow_height;
        update_emotion_parameters(emotion, &left, &right, &mouth, &left_brow, &right_brow, &brow_height);

        face_state.left_eye_openness = left;
        face_state.right_eye_openness = right;
        face_state.mouth_curve = mouth;
        face_state.left_eyebrow_angle = left_brow;
        face_state.right_eyebrow_angle = right_brow;
        face_state.eyebrow_height = brow_height;

        face_lock();
        draw_eye(face_state.left_eye_canvas, face_state.left_eye_openness, true);
        draw_eye(face_state.right_eye_canvas, face_state.right_eye_openness, false);
        draw_mouth(face_state.mouth_canvas, face_state.mouth_curve);
        face_unlock();
    }
    else
    {
        face_state.transition_progress = 0;
    }
}

face_emotion_t face_get_emotion(void)
{
    return face_state.current_emotion;
}

void face_animation_update(void)
{
    if (face_state.anim_timer)
    {
        animation_timer_cb(face_state.anim_timer);
    }
}

void face_set_eye_openness(uint8_t left_eye, uint8_t right_eye)
{
    if (!face_state.initialized)
        return;

    face_state.left_eye_openness = left_eye > 100 ? 100 : left_eye;
    face_state.right_eye_openness = right_eye > 100 ? 100 : right_eye;

    face_lock();
    draw_eye(face_state.left_eye_canvas, face_state.left_eye_openness, true);
    draw_eye(face_state.right_eye_canvas, face_state.right_eye_openness, false);
    face_unlock();
}

void face_set_mouth_shape(int8_t value)
{
    if (!face_state.initialized)
        return;

    if (value > 100)
        value = 100;
    if (value < -100)
        value = -100;

    face_state.mouth_curve = value;

    face_lock();
    draw_mouth(face_state.mouth_canvas, face_state.mouth_curve);
    face_unlock();
}

void face_set_auto_blink(bool enable)
{
    face_state.config.auto_blink = enable;
}

void face_trigger_blink(void)
{
    if (!face_state.initialized || face_state.is_blinking)
        return;

    face_state.is_blinking = true;
    face_state.blink_phase = 0;
}

void face_set_position(int16_t x, int16_t y)
{
    if (!face_state.initialized)
        return;

    face_lock();

    lv_obj_set_pos(face_state.face_container, x, y);
    face_unlock();
}

lv_obj_t *face_get_container(void)
{
    return face_state.initialized ? face_state.face_container : NULL;
}

void face_animation_deinit(void)
{
    if (!face_state.initialized)
        return;

    face_lock();

    if (face_state.anim_timer)
    {
        lv_timer_del(face_state.anim_timer);
        face_state.anim_timer = NULL;
    }

    if (face_state.left_eye_canvas)
        lv_obj_del(face_state.left_eye_canvas);
    if (face_state.right_eye_canvas)
        lv_obj_del(face_state.right_eye_canvas);
    if (face_state.mouth_canvas)
        lv_obj_del(face_state.mouth_canvas);
    if (face_state.face_container)
        lv_obj_del(face_state.face_container);

    face_unlock();

    if (face_state.left_eye_buf)
        free(face_state.left_eye_buf);
    if (face_state.right_eye_buf)
        free(face_state.right_eye_buf);
    if (face_state.mouth_buf)
        free(face_state.mouth_buf);

    memset(&face_state, 0, sizeof(face_state_t));

    FACE_LOGI(TAG, "Face animation deinitialized");
}
