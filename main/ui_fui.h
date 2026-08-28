#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

#define UI_FUI_BG          0x04070BU
#define UI_FUI_PANEL       0x0C1117U
#define UI_FUI_PANEL_ALT   0x171216U
#define UI_FUI_ORANGE      0xFF6A1AU
#define UI_FUI_RUST        0x762311U
#define UI_FUI_CREAM       0xF1E8D2U
#define UI_FUI_TEAL        0x7AD8D1U
#define UI_FUI_CYAN        UI_FUI_ORANGE
#define UI_FUI_CYAN_DIM    UI_FUI_RUST
#define UI_FUI_MAGENTA     0xC43D1BU
#define UI_FUI_GREEN       UI_FUI_TEAL
#define UI_FUI_AMBER       0xFF9B35U
#define UI_FUI_RED         0xFF4D6DU
#define UI_FUI_TEXT        UI_FUI_CREAM
#define UI_FUI_MUTED       0x829097U

#define UI_FUI_BATTERY_SEGMENTS   5U
#define UI_FUI_MENU_ITEMS         3U
#define UI_FUI_MENU_ACTIONS       2U
#define UI_FUI_PIN_DIGITS         4U
#define UI_FUI_TRACE_POINTS      32U
#define UI_FUI_TRACE_AXES         3U
#define UI_FUI_LINK_POINTS        8U
#define UI_FUI_CONFIRM_CHOICES    2U

typedef enum {
    UI_FUI_TRACE_RESULT_NONE = 0,
    UI_FUI_TRACE_RESULT_PASS,
    UI_FUI_TRACE_RESULT_FAIL,
} ui_fui_trace_result_t;

typedef enum {
    UI_FUI_PIN_INPUT = 0,
    UI_FUI_PIN_REJECTED,
    UI_FUI_PIN_ACCEPTED,
} ui_fui_pin_result_t;

typedef enum {
    UI_FUI_MANAGER_LIST = 0,
    UI_FUI_MANAGER_DETAIL,
    UI_FUI_MANAGER_CLEAR_CONFIRM,
    UI_FUI_MANAGER_CLEAR_DONE,
    UI_FUI_MANAGER_CLEAR_ERROR,
} ui_fui_manager_view_t;

typedef enum {
    UI_FUI_CONFIRM_CANCEL = 0,
    UI_FUI_CONFIRM_CLEAR,
} ui_fui_confirm_choice_t;

// Typography tokens keep hierarchy decisions separate from layout. Firmware
// uses embedded Kode Mono fonts by default; the host simulator can still inject
// alternate families for side-by-side design comparisons.
typedef struct {
    const lv_font_t *meta;
    const lv_font_t *body;
    const lv_font_t *strong;
    const lv_font_t *title;
    const lv_font_t *display;
} ui_fui_typography_t;

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *home_layer;
    lv_obj_t *menu_layer;
    lv_obj_t *pin_layer;
    lv_obj_t *main_frame;
    lv_obj_t *state_label;
    lv_obj_t *detail_label;
    lv_obj_t *status_dot;
    lv_obj_t *trace_frame;
    lv_obj_t *trace_chart;
    lv_chart_series_t *trace_series[UI_FUI_TRACE_AXES];
    lv_obj_t *trace_counter;
    lv_obj_t *trace_result_overlay;
    lv_obj_t *trace_result_lines[2];
    lv_point_precise_t trace_result_points[2][2];
    lv_obj_t *battery_segments[UI_FUI_BATTERY_SEGMENTS];
    lv_obj_t *link_value;
    lv_obj_t *link_dot;
    lv_obj_t *link_chart;
    lv_chart_series_t *link_series;
    lv_obj_t *scan_line;
    lv_obj_t *home_help;
    lv_obj_t *menu_list;
    lv_obj_t *menu_detail;
    lv_obj_t *menu_rows[UI_FUI_MENU_ITEMS];
    lv_obj_t *menu_row_index[UI_FUI_MENU_ITEMS];
    lv_obj_t *menu_row_name[UI_FUI_MENU_ITEMS];
    lv_obj_t *menu_row_state[UI_FUI_MENU_ITEMS];
    lv_obj_t *menu_detail_index;
    lv_obj_t *menu_detail_name;
    lv_obj_t *menu_detail_state;
    lv_obj_t *menu_action_rows[UI_FUI_MENU_ACTIONS];
    lv_obj_t *menu_action_codes[UI_FUI_MENU_ACTIONS];
    lv_obj_t *menu_action_labels[UI_FUI_MENU_ACTIONS];
    lv_obj_t *menu_help;
    lv_obj_t *menu_dialog_scrim;
    lv_obj_t *menu_dialog;
    lv_obj_t *menu_dialog_title;
    lv_obj_t *menu_dialog_name;
    lv_obj_t *menu_dialog_detail;
    lv_obj_t *menu_confirm_rows[UI_FUI_CONFIRM_CHOICES];
    lv_obj_t *menu_confirm_labels[UI_FUI_CONFIRM_CHOICES];
    lv_obj_t *pin_digit_boxes[UI_FUI_PIN_DIGITS];
    lv_obj_t *pin_digit_labels[UI_FUI_PIN_DIGITS];
    lv_obj_t *pin_state_label;
    lv_obj_t *pin_detail_label;
    lv_obj_t *pin_help;
    ui_fui_typography_t typography;
    uint32_t accent;
    bool menu_visible;
    bool pin_visible;
} ui_fui_t;

void ui_fui_create(ui_fui_t *ui);
void ui_fui_create_with_typography(ui_fui_t *ui,
                                   const ui_fui_typography_t *typography);
void ui_fui_set_status(ui_fui_t *ui, const char *state, const char *detail,
                       uint32_t accent);
void ui_fui_set_progress(ui_fui_t *ui, bool active, uint8_t value);
void ui_fui_set_trace(ui_fui_t *ui,
                      const int16_t values[][UI_FUI_TRACE_AXES], size_t count);
void ui_fui_set_trace_result(ui_fui_t *ui, ui_fui_trace_result_t result);
void ui_fui_set_battery(ui_fui_t *ui, bool available, int soc, int millivolts);
void ui_fui_set_link(ui_fui_t *ui, const char *text, uint32_t color);
void ui_fui_set_link_quality(ui_fui_t *ui, bool available, int rssi_dbm);
void ui_fui_set_manager(ui_fui_t *ui, ui_fui_manager_view_t view,
                        uint8_t selected, uint8_t action_selected,
                        ui_fui_confirm_choice_t confirm_choice,
                        uint8_t valid_mask,
                        const char *const names[UI_FUI_MENU_ITEMS]);
void ui_fui_set_pin(ui_fui_t *ui, uint8_t position, uint8_t digit,
                    ui_fui_pin_result_t result);
