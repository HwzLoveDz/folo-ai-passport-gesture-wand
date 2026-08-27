#ifndef LV_CONF_H
#define LV_CONF_H

// Match the ESP32 panel so desktop previews use the same color quantization.
#define LV_COLOR_DEPTH 16

#define LV_USE_STDLIB_MALLOC  LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING  LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB
#define LV_USE_OS             LV_OS_NONE

#define LV_USE_DRAW_SW 1
#define LV_DEF_REFR_PERIOD 16
#define LV_DPI_DEF 130

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_CHART 1
#define LV_USE_LINE 1

// Host-only runtime fonts let us compare weights before generating embedded
// C font assets. No FreeType or system font dependency is required.
#define LV_USE_TINY_TTF 1
#define LV_TINY_TTF_FILE_SUPPORT 0
#define LV_TINY_TTF_CACHE_GLYPH_CNT 128

#define LV_USE_LOG 0

#endif
