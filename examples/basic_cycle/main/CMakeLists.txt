idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        lvgl_kawaii_face
        esp_timer
        freertos
)
