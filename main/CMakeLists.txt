idf_component_register(
    SRCS "main.c"
         "gesture_key.c"
         "gesture_model.c"
         "gesture_sound.c"
         "ui_fui.c"
         "fonts/kode_mono/ui_font_kode_regular_11.c"
         "fonts/kode_mono/ui_font_kode_regular_13.c"
         "fonts/kode_mono/ui_font_kode_bold_13.c"
         "fonts/kode_mono/ui_font_kode_bold_15.c"
         "fonts/kode_mono/ui_font_kode_bold_21.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_timer esp_hid nvs_flash bt
)
