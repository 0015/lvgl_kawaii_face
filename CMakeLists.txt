idf_component_register(
    SRCS "lvgl_kawaii_face.c"
    INCLUDE_DIRS "include"
    REQUIRES
        lvgl__lvgl
    PRIV_REQUIRES
        log
        esp_timer
        heap
)

# esp_lvgl_port is optional (compile-time, via __has_include).
# Link it only when it is actually present in the build.
if(TARGET __idf_espressif__esp_lvgl_port OR TARGET espressif__esp_lvgl_port)
    target_link_libraries(${COMPONENT_LIB} PRIVATE espressif__esp_lvgl_port)
endif()
