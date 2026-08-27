idf_component_register(
    SRCS "main.c"
         "gesture_key.c"
         "gesture_model.c"
         "gesture_sound.c"
         "ui_fui.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_timer esp_hid nvs_flash bt
)
