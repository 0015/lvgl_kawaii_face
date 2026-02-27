# LVGL KAWAII FACE

A kawaii-style animated face widget for [LVGL 9](https://lvgl.io/) on ESP32.  
Renders eyes, eyebrows, blush, and a mouth on LVGL canvases with per-emotion dynamic animations — bouncing, sparkling, pupil movement, tears, sweat drops, and smooth transitions between every expression.

---

## Emotions

| Constant | Expression |
|---|---|
| `FACE_NEUTRAL` | Resting face with idle glance and micro-smile cycle |
| `FACE_HAPPY` | Wide eyes, big smile, energetic bounce, cycling sparkles |
| `FACE_WORRIED` | Nervous smile, twitching raised eyebrows, anxious fidget |
| `FACE_SAD` | Droopy eyes, deep frown, falling tears, melancholy sway |
| `FACE_CRY` | Squinted eyes, sobbing tremor, animated tear streams |
| `FACE_SURPRISED` | Wide eyes, pulsing diamond/O mouth, shock vibration |
| `FACE_ANGRY` | Furrowed brows, rage-flush blush, jaw trembling |
| `FACE_SLEEPY` | Half-closed eyes, slow nod, lazy sweat drip |
| `FACE_WINK` | One eye closed, playful smile, cycling sparkles |
| `FACE_LOVE` | Heart-shaped eyes, heartbeat float, max blush |
| `FACE_PLAYFUL` | Wide grin, wagging tongue, energetic bounce |
| `FACE_SILLY` | Cross-eyed, darting pupils, goofy grin |
| `FACE_SMIRK` | Asymmetric brows, slow side-glance, subtle smirk |
| `FACE_WORKING_HARD` | Gritted teeth, focused gaze, dripping sweat |
| `FACE_EXCITED` | Rapidly darting pupils, huge grin, rapid bounce |
| `FACE_CONFUSED` | Asymmetric brows, Lissajous pupil orbit, head-tilt wobble |
| `FACE_COOL` | Half-lidded squint, slow deliberate glance |

---

## Adding to your project

Copy the `lvgl_kawaii_face/` folder into your project's `components/` directory.  
No other changes to your build files are needed — the component's `CMakeLists.txt` declares all dependencies.

```
my_project/
└── components/
    └── lvgl_kawaii_face/
        ├── CMakeLists.txt
        ├── idf_component.yml
        ├── lvgl_kawaii_face.c
        └── include/
            └── lvgl_kawaii_face.h
```

In any source file that uses it:

```c
#include "lvgl_kawaii_face.h"
```

---

## Usage

### 1. Create a parent panel

The face fills whatever parent object you give it. Size and position are set entirely on the parent — the face scales automatically.

```c
lvgl_port_lock(0);

lv_obj_t *face_panel = lv_obj_create(lv_scr_act());
lv_obj_set_size(face_panel, 135, 135);
lv_obj_align(face_panel, LV_ALIGN_CENTER, 0, 0);
lv_obj_set_style_bg_opa(face_panel, LV_OPA_TRANSP, 0);
lv_obj_set_style_border_width(face_panel, 0, 0);
lv_obj_set_style_pad_all(face_panel, 0, 0);
lv_obj_clear_flag(face_panel, LV_OBJ_FLAG_SCROLLABLE);

lvgl_port_unlock();
```

### 2. Initialise

```c
face_config_t cfg = {
    .parent          = face_panel,  // NULL → fills the active screen
    .animation_speed = 30,          // timer interval in ms (~33 FPS)
    .blink_interval  = 3000,        // auto-blink every 3 s
    .auto_blink      = true,
};
ESP_ERROR_CHECK(face_animation_init(&cfg));
```

### 3. Set an emotion

```c
// Instant switch
face_set_emotion(FACE_HAPPY, false);

// Smooth transition
face_set_emotion(FACE_SURPRISED, true);
```

### 4. Reposition

Move `face_panel` at any time — the face follows it:

```c
lvgl_port_lock(0);
lv_obj_set_pos(face_panel, new_x, new_y);
lvgl_port_unlock();
```

---

## Thread safety

On ESP-IDF, `esp_lvgl_port` lock/unlock is used automatically when the header is available.  
On any other platform — or when you manage your own LVGL task — call `face_set_lvgl_lock_fns()` **before** `face_animation_init()` to supply your own mutex pair.  
Pass `NULL` for both to restore the platform default.

---

## Requirements

| | |
|---|---|
| ESP-IDF | ≥ 5.0 |
| LVGL | ≥ 9.0 |
| `esp_lvgl_port` | Optional (auto-detected at compile time) |

---

## License

MIT License

Copyright (c) 2026 Eric Nam

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
