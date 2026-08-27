#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#define UI_FUI_BG          0x02070DU
#define UI_FUI_PANEL       0x07141DU
#define UI_FUI_PANEL_ALT   0x091E29U
#define UI_FUI_CYAN        0x42F5FFU
#define UI_FUI_CYAN_DIM    0x0B5965U
#define UI_FUI_MAGENTA     0xFF43E6U
#define UI_FUI_GREEN       0x54FFA1U
#define UI_FUI_AMBER       0xFFC857U
#define UI_FUI_RED         0xFF4D6DU
#define UI_FUI_TEXT        0xE8FCFFU
#define UI_FUI_MUTED       0x6A94A0U

#define UI_FUI_PROGRESS_SEGMENTS 16U

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *main_frame;
    lv_obj_t *state_label;
    lv_obj_t *detail_label;
    lv_obj_t *status_dot;
    lv_obj_t *progress_box;
    lv_obj_t *progress_segments[UI_FUI_PROGRESS_SEGMENTS];
    lv_obj_t *progress_label;
    lv_obj_t *battery_label;
    lv_obj_t *battery_fill;
    lv_obj_t *battery_voltage;
    lv_obj_t *link_value;
    lv_obj_t *link_dot;
    lv_obj_t *scan_line;
    uint32_t accent;
} ui_fui_t;

void ui_fui_create(ui_fui_t *ui);
void ui_fui_set_status(ui_fui_t *ui, const char *state, const char *detail,
                       uint32_t accent);
void ui_fui_set_progress(ui_fui_t *ui, bool active, uint8_t value);
void ui_fui_set_battery(ui_fui_t *ui, bool available, int soc, int millivolts);
void ui_fui_set_link(ui_fui_t *ui, const char *text, uint32_t color);
void ui_fui_flash_success(ui_fui_t *ui);
