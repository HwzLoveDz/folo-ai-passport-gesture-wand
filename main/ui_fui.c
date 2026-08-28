#include "ui_fui.h"

#include <stddef.h>
#include <string.h>

LV_FONT_DECLARE(ui_font_kode_regular_11);
LV_FONT_DECLARE(ui_font_kode_regular_13);
LV_FONT_DECLARE(ui_font_kode_bold_13);
LV_FONT_DECLARE(ui_font_kode_bold_15);
LV_FONT_DECLARE(ui_font_kode_bold_21);

#define HEADER_PANEL_RIGHT       232
#define BATTERY_SEGMENT_SIZE       9
#define BATTERY_SEGMENT_GAP        3
#define LINK_RSSI_LIVE_THRESHOLD (-44)

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int width, int height,
                     uint32_t background, lv_opa_t opacity)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(background), 0);
    lv_obj_set_style_bg_opa(object, opacity, 0);
    return object;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y,
                       const lv_font_t *font, uint32_t color)
{
    lv_obj_t *object = lv_label_create(parent);
    lv_label_set_text(object, text);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    return object;
}

static lv_obj_t *centered_label(lv_obj_t *parent, const char *text,
                                const lv_font_t *font, uint32_t color)
{
    lv_obj_t *object = label(parent, text, 0, 0, font, color);
    lv_obj_center(object);
    return object;
}

static void add_section_rule(lv_obj_t *parent, int x, int y, int width)
{
    if (width <= 18) return;
    box(parent, x, y, 14, 2, UI_FUI_ORANGE, LV_OPA_COVER);
    box(parent, x + 18, y, width - 18, 2, UI_FUI_CREAM, LV_OPA_80);
}

static void add_grid(lv_obj_t *screen)
{
    for (int x = 0; x < 240; x += 40) {
        box(screen, x, 44, 1, 254, UI_FUI_CYAN_DIM, 26);
    }
    for (int y = 44; y < 298; y += 36) {
        box(screen, 0, y, 240, 1, UI_FUI_CYAN_DIM, 24);
    }
}

static void link_chart_draw_event(lv_event_t *event)
{
    lv_draw_task_t *draw_task = lv_event_get_draw_task(event);
    if (!draw_task || lv_draw_task_get_type(draw_task) != LV_DRAW_TASK_TYPE_FILL) {
        return;
    }
    lv_draw_dsc_base_t *base =
        (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(draw_task);
    if (!base || base->part != LV_PART_ITEMS) return;

    lv_obj_t *chart = lv_event_get_target_obj(event);
    lv_chart_series_t *series = lv_chart_get_series_next(chart, NULL);
    lv_draw_fill_dsc_t *fill = lv_draw_task_get_fill_dsc(draw_task);
    if (!series || !fill) return;

    uint32_t count = lv_chart_get_point_count(chart);
    if (count == 0U || base->id2 >= count) return;
    int32_t *values = lv_chart_get_series_y_array(chart, series);
    if (!values) return;
    uint32_t start = lv_chart_get_x_start_point(chart, series);
    uint32_t point = (start + base->id2) % count;
    int32_t value = values[point];
    if (value == LV_CHART_POINT_NONE) return;

    // -100..-30 dBm is normalized to 0..100%; -44 dBm is the 80% point.
    // Only the strongest samples keep the live LINK color. Everything below
    // 80% returns to the orange FUI theme color.
    fill->color = value >= LINK_RSSI_LIVE_THRESHOLD ?
        lv_chart_get_series_color(chart, series) : lv_color_hex(UI_FUI_CYAN);
}

static void scan_y(void *object, int32_t y)
{
    lv_obj_set_y((lv_obj_t *)object, y);
}

static void pulse_opacity(void *object, int32_t opacity)
{
    lv_obj_set_style_opa((lv_obj_t *)object, (lv_opa_t)opacity, 0);
}

static void start_scan_animation(lv_obj_t *scan_line, int32_t from, int32_t to)
{
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, scan_line);
    lv_anim_set_exec_cb(&animation, scan_y);
    lv_anim_set_values(&animation, from, to);
    lv_anim_set_duration(&animation, 1350);
    lv_anim_set_playback_duration(&animation, 1350);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_start(&animation);
}

static void start_dot_animation(lv_obj_t *dot)
{
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, dot);
    lv_anim_set_exec_cb(&animation, pulse_opacity);
    lv_anim_set_values(&animation, LV_OPA_30, LV_OPA_COVER);
    lv_anim_set_duration(&animation, 620);
    lv_anim_set_playback_duration(&animation, 620);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&animation);
}

void ui_fui_create(ui_fui_t *ui)
{
    ui_fui_create_with_typography(ui, NULL);
}

void ui_fui_create_with_typography(ui_fui_t *ui,
                                   const ui_fui_typography_t *typography)
{
    if (!ui) return;
    memset(ui, 0, sizeof(*ui));
    const ui_fui_typography_t defaults = {
        .meta = &ui_font_kode_regular_11,
        .body = &ui_font_kode_regular_13,
        .strong = &ui_font_kode_bold_13,
        .title = &ui_font_kode_bold_15,
        .display = &ui_font_kode_bold_21,
    };
    ui->typography = typography ? *typography : defaults;
    if (!ui->typography.meta) ui->typography.meta = defaults.meta;
    if (!ui->typography.body) ui->typography.body = defaults.body;
    if (!ui->typography.strong) ui->typography.strong = defaults.strong;
    if (!ui->typography.title) ui->typography.title = defaults.title;
    if (!ui->typography.display) ui->typography.display = defaults.display;
    ui->accent = UI_FUI_CYAN;

    lv_obj_t *screen = lv_obj_create(NULL);
    ui->screen = screen;
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(UI_FUI_BG), 0);
    add_grid(screen);

    box(screen, 0, 0, 240, 44, UI_FUI_PANEL, LV_OPA_COVER);
    box(screen, 0, 41, 52, 2, UI_FUI_ORANGE, LV_OPA_COVER);
    box(screen, 56, 41, 184, 2, UI_FUI_CREAM, LV_OPA_80);
    box(screen, 0, 43, 156, 1, UI_FUI_RUST, LV_OPA_70);
    lv_obj_t *header_code_top = box(screen, 8, 5, 31, 15,
                                    UI_FUI_RUST, LV_OPA_COVER);
    lv_obj_t *header_code_bottom = box(screen, 8, 21, 31, 15,
                                       UI_FUI_ORANGE, LV_OPA_COVER);
    centered_label(header_code_top, "MW", ui->typography.strong,
                   UI_FUI_CREAM);
    centered_label(header_code_bottom, "02", ui->typography.strong,
                   UI_FUI_CREAM);

    lv_obj_t *title = label(screen, "MOTE WAND", 0, 0,
                            ui->typography.title, UI_FUI_TEXT);
    lv_obj_set_style_text_letter_space(title, 1, 0);
    lv_obj_align_to(title, header_code_top, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    lv_obj_t *subtitle = label(screen, "GESTURE CONTROL CORE", 0, 0,
                               ui->typography.meta, UI_FUI_MUTED);
    lv_obj_align_to(subtitle, header_code_bottom, LV_ALIGN_OUT_RIGHT_MID,
                    8, 0);

    const int battery_width =
        (int)UI_FUI_BATTERY_SEGMENTS * BATTERY_SEGMENT_SIZE +
        ((int)UI_FUI_BATTERY_SEGMENTS - 1) * BATTERY_SEGMENT_GAP;
    lv_obj_t *battery_group = box(screen, 0, 0, battery_width,
                                  BATTERY_SEGMENT_SIZE,
                                  UI_FUI_BG, LV_OPA_TRANSP);
    lv_obj_align_to(battery_group, title, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
    lv_obj_set_x(battery_group, HEADER_PANEL_RIGHT - battery_width);
    for (unsigned i = 0; i < UI_FUI_BATTERY_SEGMENTS; i++) {
        int x = (int)i * (BATTERY_SEGMENT_SIZE + BATTERY_SEGMENT_GAP);
        ui->battery_segments[i] = box(battery_group, x, 0,
                                      BATTERY_SEGMENT_SIZE,
                                      BATTERY_SEGMENT_SIZE,
                                      UI_FUI_CYAN_DIM, LV_OPA_40);
    }

    ui->home_layer = box(screen, 0, 44, 240, 276,
                         UI_FUI_BG, LV_OPA_TRANSP);
    ui->main_frame = box(ui->home_layer, 8, 6, 224, 163,
                         UI_FUI_PANEL, LV_OPA_90);
    lv_obj_set_style_border_width(ui->main_frame, 1, 0);
    lv_obj_set_style_border_color(ui->main_frame, lv_color_hex(UI_FUI_CYAN_DIM), 0);

    box(ui->main_frame, 0, 22, 2, 137, UI_FUI_RUST, LV_OPA_COVER);
    box(ui->main_frame, 4, 22, 1, 137, UI_FUI_ORANGE, LV_OPA_70);
    label(ui->main_frame, "MOTION SIGNATURE", 12, 4,
          ui->typography.meta, UI_FUI_MUTED);
    add_section_rule(ui->main_frame, 146, 10, 52);
    ui->status_dot = box(ui->main_frame, 204, 7, 6, 6,
                         UI_FUI_CYAN, LV_OPA_COVER);
    lv_obj_set_style_radius(ui->status_dot, LV_RADIUS_CIRCLE, 0);
    start_dot_animation(ui->status_dot);
    box(ui->main_frame, 12, 21, 200, 1, UI_FUI_CYAN_DIM, LV_OPA_60);

    lv_obj_t *trace_code_top = box(ui->main_frame, 12, 27, 28, 36,
                                   UI_FUI_RUST, LV_OPA_COVER);
    lv_obj_t *trace_code_bottom = box(ui->main_frame, 12, 63, 28, 36,
                                      UI_FUI_ORANGE, LV_OPA_COVER);
    label(trace_code_top, "01", 5, 10,
          ui->typography.title, UI_FUI_CREAM);
    label(trace_code_bottom, "GS", 5, 10,
          ui->typography.title, UI_FUI_CREAM);

    ui->trace_frame = box(ui->main_frame, 44, 27, 168, 72,
                          UI_FUI_BG, LV_OPA_80);
    lv_obj_set_style_border_width(ui->trace_frame, 1, 0);
    lv_obj_set_style_border_color(ui->trace_frame,
                                  lv_color_hex(UI_FUI_CYAN_DIM), 0);

    ui->trace_chart = lv_chart_create(ui->trace_frame);
    lv_obj_remove_flag(ui->trace_chart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(ui->trace_chart, 2, 3);
    lv_obj_set_size(ui->trace_chart, 164, 67);
    lv_obj_set_style_pad_all(ui->trace_chart, 0, 0);
    lv_obj_set_style_border_width(ui->trace_chart, 0, 0);
    lv_obj_set_style_radius(ui->trace_chart, 0, 0);
    lv_obj_set_style_bg_opa(ui->trace_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_line_color(ui->trace_chart,
                                lv_color_hex(UI_FUI_CYAN_DIM), LV_PART_MAIN);
    lv_obj_set_style_line_opa(ui->trace_chart, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_line_width(ui->trace_chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_width(ui->trace_chart, 1, LV_PART_ITEMS);
    lv_obj_set_style_size(ui->trace_chart, 3, 3, LV_PART_INDICATOR);
    lv_obj_set_style_radius(ui->trace_chart, LV_RADIUS_CIRCLE,
                            LV_PART_INDICATOR);
    lv_chart_set_type(ui->trace_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(ui->trace_chart, UI_FUI_TRACE_POINTS);
    lv_chart_set_axis_range(ui->trace_chart, LV_CHART_AXIS_PRIMARY_Y,
                            -100, 100);
    lv_chart_set_div_line_count(ui->trace_chart, 3, 5);
    const uint32_t trace_colors[UI_FUI_TRACE_AXES] = {
        UI_FUI_ORANGE, UI_FUI_TEAL, UI_FUI_MAGENTA,
    };
    for (unsigned axis = 0; axis < UI_FUI_TRACE_AXES; axis++) {
        ui->trace_series[axis] = lv_chart_add_series(
            ui->trace_chart, lv_color_hex(trace_colors[axis]),
            LV_CHART_AXIS_PRIMARY_Y);
        lv_chart_set_all_values(ui->trace_chart, ui->trace_series[axis],
                                LV_CHART_POINT_NONE);
    }

    ui->trace_result_overlay = box(ui->trace_frame, 1, 1, 166, 70,
                                   UI_FUI_BG, LV_OPA_80);
    for (unsigned i = 0; i < 2U; i++) {
        ui->trace_result_lines[i] = lv_line_create(ui->trace_result_overlay);
        lv_obj_set_style_line_width(ui->trace_result_lines[i], 5, 0);
        lv_obj_set_style_line_rounded(ui->trace_result_lines[i], true, 0);
    }
    lv_obj_add_flag(ui->trace_result_overlay, LV_OBJ_FLAG_HIDDEN);

    ui->scan_line = box(ui->trace_frame, 2, 3, 164, 1,
                        UI_FUI_CYAN, LV_OPA_60);
    start_scan_animation(ui->scan_line, 3, 68);
    ui->trace_counter = label(ui->trace_frame, "", 124, 1,
                              ui->typography.meta, UI_FUI_MUTED);
    lv_obj_set_width(ui->trace_counter, 40);
    lv_obj_set_style_text_align(ui->trace_counter, LV_TEXT_ALIGN_RIGHT, 0);

    ui->state_label = label(ui->main_frame, "STARTING", 7, 104,
                            ui->typography.display, UI_FUI_CYAN);
    lv_obj_set_size(ui->state_label, 210, 25);
    lv_label_set_long_mode(ui->state_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(ui->state_label, LV_TEXT_ALIGN_CENTER, 0);

    ui->detail_label = label(ui->main_frame, "PLEASE WAIT", 14, 135,
                             ui->typography.body, UI_FUI_TEXT);
    lv_obj_set_size(ui->detail_label, 196, 18);
    lv_label_set_long_mode(ui->detail_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(ui->detail_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *link_panel = box(ui->home_layer, 8, 177, 224, 69,
                               UI_FUI_PANEL_ALT, LV_OPA_90);
    lv_obj_set_style_border_width(link_panel, 1, 0);
    lv_obj_set_style_border_color(link_panel, lv_color_hex(UI_FUI_CYAN_DIM), 0);
    box(link_panel, 0, 22, 2, 43, UI_FUI_RUST, LV_OPA_COVER);
    box(link_panel, 4, 22, 1, 43, UI_FUI_ORANGE, LV_OPA_70);
    label(link_panel, "HOST LINK", 12, 4,
          ui->typography.meta, UI_FUI_MUTED);
    add_section_rule(link_panel, 90, 10, 122);
    box(link_panel, 12, 21, 200, 1, UI_FUI_CYAN_DIM, LV_OPA_60);

    lv_obj_t *link_code_top = box(link_panel, 12, 31, 32, 14,
                                  UI_FUI_RUST, LV_OPA_COVER);
    lv_obj_t *link_code_bottom = box(link_panel, 12, 45, 32, 14,
                                     UI_FUI_ORANGE, LV_OPA_COVER);
    label(link_code_top, "02", 8, 0,
          ui->typography.strong, UI_FUI_CREAM);
    label(link_code_bottom, "BT", 8, 0,
          ui->typography.strong, UI_FUI_CREAM);

    // The status unit spans the complete panel body. Side telemetry is allowed
    // to sit behind it, so the pixel + text are mathematically centered in the
    // panel instead of merely centered in the leftover middle column.
    lv_obj_t *link_status = box(link_panel, 0, 22, 224, 46,
                                UI_FUI_BG, LV_OPA_TRANSP);
    lv_obj_set_flex_flow(link_status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(link_status, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(link_status, 9, 0);
    ui->link_dot = box(link_status, 0, 0, 6, 6,
                       UI_FUI_MAGENTA, LV_OPA_COVER);
    ui->link_value = label(link_status, "PAIRING", 0, 0,
                           ui->typography.display, UI_FUI_MAGENTA);
    // Preserve the optical correction for Kode Mono, then apply the final
    // one-pixel right/up trim requested against the square status pixel.
    lv_obj_set_style_translate_x(ui->link_value, 1, 0);
    lv_obj_set_style_translate_y(ui->link_value, 2, 0);

    // Compact telemetry bars float without a frame or grid; only live signal
    // data remains, which keeps the small LINK panel visually quiet.
    ui->link_chart = lv_chart_create(link_panel);
    lv_obj_remove_flag(ui->link_chart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(ui->link_chart, 174, 29);
    lv_obj_set_size(ui->link_chart, 38, 31);
    lv_obj_set_style_pad_all(ui->link_chart, 0, 0);
    lv_obj_set_style_radius(ui->link_chart, 0, 0);
    lv_obj_set_style_bg_color(ui->link_chart,
                              lv_color_hex(UI_FUI_RUST), 0);
    lv_obj_set_style_bg_opa(ui->link_chart, LV_OPA_30, 0);
    lv_obj_set_style_border_width(ui->link_chart, 0, 0);
    lv_obj_set_style_line_opa(ui->link_chart, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_column(ui->link_chart, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->link_chart, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_radius(ui->link_chart, 0, LV_PART_ITEMS);
    lv_chart_set_type(ui->link_chart, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(ui->link_chart, UI_FUI_LINK_POINTS);
    lv_chart_set_axis_range(ui->link_chart, LV_CHART_AXIS_PRIMARY_Y, -100, -30);
    lv_chart_set_div_line_count(ui->link_chart, 0, 0);
    ui->link_series = lv_chart_add_series(ui->link_chart,
                                          lv_color_hex(UI_FUI_TEAL),
                                          LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_values(ui->link_chart, ui->link_series,
                            LV_CHART_POINT_NONE);
    lv_obj_add_event_cb(ui->link_chart, link_chart_draw_event,
                        LV_EVENT_DRAW_TASK_ADDED, NULL);
    lv_obj_add_flag(ui->link_chart, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);

    box(ui->home_layer, 8, 254, 224, 1, UI_FUI_CYAN_DIM, LV_OPA_COVER);
    ui->home_help = label(ui->home_layer, "OK TRACE | UP MANAGER", 0, 257,
                          ui->typography.meta, UI_FUI_MUTED);
    lv_obj_set_width(ui->home_help, 240);
    lv_obj_set_style_text_align(ui->home_help, LV_TEXT_ALIGN_CENTER, 0);

    ui->menu_layer = box(screen, 0, 44, 240, 276,
                         UI_FUI_BG, LV_OPA_COVER);
    lv_obj_t *manager_header = box(ui->menu_layer, 8, 6, 224, 38,
                                   UI_FUI_PANEL_ALT, LV_OPA_COVER);
    lv_obj_set_style_border_width(manager_header, 1, 0);
    lv_obj_set_style_border_color(manager_header, lv_color_hex(UI_FUI_RUST), 0);
    lv_obj_t *manager_code_top = box(manager_header, 0, 0, 38, 18,
                                     UI_FUI_RUST, LV_OPA_COVER);
    lv_obj_t *manager_code_bottom = box(manager_header, 0, 18, 38, 18,
                                        UI_FUI_ORANGE, LV_OPA_COVER);
    label(manager_code_top, "03", 11, 2,
          ui->typography.strong, UI_FUI_CREAM);
    label(manager_code_bottom, "GM", 11, 2,
          ui->typography.strong, UI_FUI_CREAM);
    label(manager_header, "GESTURE MANAGER", 49, 4,
          ui->typography.title, UI_FUI_CREAM);
    label(manager_header, "02 GESTURES / ON DEVICE", 49, 21,
          ui->typography.meta, UI_FUI_MUTED);

    ui->menu_list = box(ui->menu_layer, 8, 52, 224, 178,
                        UI_FUI_BG, LV_OPA_TRANSP);
    for (unsigned i = 0; i < UI_FUI_MENU_ITEMS; i++) {
        int y = (int)i * 74;
        ui->menu_rows[i] = box(ui->menu_list, 0, y, 224, 66,
                               UI_FUI_PANEL, LV_OPA_COVER);
        lv_obj_set_style_border_width(ui->menu_rows[i], 1, 0);
        lv_obj_set_style_border_color(ui->menu_rows[i],
                                      lv_color_hex(UI_FUI_RUST), 0);
        ui->menu_row_index[i] = box(ui->menu_rows[i], 0, 0, 38, 64,
                                    UI_FUI_RUST, LV_OPA_COVER);
        label(ui->menu_row_index[i], i == 0U ? "01" : "02", 10, 8,
              ui->typography.title, UI_FUI_CREAM);
        label(ui->menu_row_index[i], "GS", 11, 32,
              ui->typography.strong, UI_FUI_ORANGE);
        ui->menu_row_name[i] = label(ui->menu_rows[i], "MACRO", 50, 9,
                                     ui->typography.title, UI_FUI_CREAM);
        ui->menu_row_state[i] = label(ui->menu_rows[i], "NOT RECORDED", 50, 35,
                                      ui->typography.meta, UI_FUI_MUTED);
        lv_obj_set_size(ui->menu_row_state[i], 88, 16);
        lv_label_set_long_mode(ui->menu_row_state[i], LV_LABEL_LONG_CLIP);
        label(ui->menu_rows[i], "REC / CLR", 143, 35,
              ui->typography.meta, UI_FUI_MUTED);
        label(ui->menu_rows[i], ">", 203, 20,
              ui->typography.title, UI_FUI_ORANGE);
    }

    ui->menu_detail = box(ui->menu_layer, 8, 52, 224, 178,
                          UI_FUI_PANEL, LV_OPA_COVER);
    lv_obj_set_style_border_width(ui->menu_detail, 1, 0);
    lv_obj_set_style_border_color(ui->menu_detail, lv_color_hex(UI_FUI_RUST), 0);
    lv_obj_t *detail_code = box(ui->menu_detail, 0, 0, 42, 54,
                                UI_FUI_RUST, LV_OPA_COVER);
    box(detail_code, 0, 27, 42, 27, UI_FUI_ORANGE, LV_OPA_COVER);
    ui->menu_detail_index = label(detail_code, "01", 12, 4,
                                  ui->typography.title, UI_FUI_CREAM);
    label(detail_code, "GS", 13, 31,
          ui->typography.strong, UI_FUI_CREAM);
    ui->menu_detail_name = label(ui->menu_detail, "AUTH_SEQUENCE", 54, 7,
                                 ui->typography.title, UI_FUI_CREAM);
    lv_obj_set_width(ui->menu_detail_name, 158);
    ui->menu_detail_state = label(ui->menu_detail, "RECORDED", 54, 31,
                                  ui->typography.meta, UI_FUI_TEAL);
    box(ui->menu_detail, 12, 63, 200, 1, UI_FUI_RUST, LV_OPA_COVER);
    label(ui->menu_detail, "ACTIONS", 12, 68,
          ui->typography.meta, UI_FUI_MUTED);
    const char *const action_names[UI_FUI_MENU_ACTIONS] = {
        "RE-RECORD", "CLEAR GESTURE",
    };
    for (unsigned i = 0; i < UI_FUI_MENU_ACTIONS; i++) {
        int y = 84 + (int)i * 40;
        ui->menu_action_rows[i] = box(ui->menu_detail, 12, y, 200, 35,
                                      UI_FUI_PANEL_ALT, LV_OPA_COVER);
        lv_obj_set_style_border_width(ui->menu_action_rows[i], 1, 0);
        lv_obj_set_style_border_color(ui->menu_action_rows[i],
                                      lv_color_hex(UI_FUI_RUST), 0);
        ui->menu_action_codes[i] = box(ui->menu_action_rows[i], 0, 0, 36, 33,
                                       UI_FUI_RUST, LV_OPA_COVER);
        label(ui->menu_action_codes[i], i == 0U ? "R" : "C", 14, 9,
              ui->typography.strong, UI_FUI_CREAM);
        ui->menu_action_labels[i] = label(ui->menu_action_rows[i],
                                          action_names[i], 48, 9,
                                          ui->typography.strong,
                                          i == 0U ? UI_FUI_CREAM : UI_FUI_RED);
    }

    box(ui->menu_layer, 8, 254, 224, 1, UI_FUI_RUST, LV_OPA_COVER);
    ui->menu_help = label(ui->menu_layer,
                          "UP/DN | OK OPEN | HOLD UP EXIT", 0, 257,
                          ui->typography.meta, UI_FUI_MUTED);
    lv_obj_set_width(ui->menu_help, 240);
    lv_obj_set_style_text_align(ui->menu_help, LV_TEXT_ALIGN_CENTER, 0);

    ui->menu_dialog_scrim = box(ui->menu_layer, 0, 0, 240, 276,
                                UI_FUI_BG, LV_OPA_70);
    ui->menu_dialog = box(ui->menu_dialog_scrim, 20, 70, 200, 136,
                          UI_FUI_PANEL_ALT, LV_OPA_COVER);
    lv_obj_set_style_border_width(ui->menu_dialog, 2, 0);
    lv_obj_set_style_border_color(ui->menu_dialog,
                                  lv_color_hex(UI_FUI_ORANGE), 0);
    box(ui->menu_dialog, 0, 0, 4, 132, UI_FUI_ORANGE, LV_OPA_COVER);
    label(ui->menu_dialog, "04 / SECURE ACTION", 12, 7,
          ui->typography.meta, UI_FUI_MUTED);
    add_section_rule(ui->menu_dialog, 150, 13, 38);
    ui->menu_dialog_title = label(ui->menu_dialog, "CLEAR GESTURE?", 10, 28,
                                  ui->typography.title, UI_FUI_ORANGE);
    lv_obj_set_width(ui->menu_dialog_title, 180);
    lv_obj_set_style_text_align(ui->menu_dialog_title, LV_TEXT_ALIGN_CENTER, 0);
    ui->menu_dialog_name = label(ui->menu_dialog, "AUTH_SEQUENCE", 12, 51,
                                 ui->typography.strong, UI_FUI_CREAM);
    lv_obj_set_width(ui->menu_dialog_name, 176);
    lv_obj_set_style_text_align(ui->menu_dialog_name, LV_TEXT_ALIGN_CENTER, 0);
    ui->menu_dialog_detail = label(ui->menu_dialog, "SELECT AN ACTION", 12, 70,
                                   ui->typography.meta, UI_FUI_MUTED);
    lv_obj_set_width(ui->menu_dialog_detail, 176);
    lv_obj_set_style_text_align(ui->menu_dialog_detail, LV_TEXT_ALIGN_CENTER, 0);
    const char *const confirm_names[UI_FUI_CONFIRM_CHOICES] = {
        "CANCEL", "CLEAR",
    };
    for (unsigned i = 0; i < UI_FUI_CONFIRM_CHOICES; i++) {
        ui->menu_confirm_rows[i] = box(ui->menu_dialog,
                                       i == 0U ? 12 : 104, 94, 84, 30,
                                       UI_FUI_PANEL, LV_OPA_COVER);
        lv_obj_set_style_border_width(ui->menu_confirm_rows[i], 1, 0);
        lv_obj_set_style_border_color(ui->menu_confirm_rows[i],
                                      lv_color_hex(UI_FUI_RUST), 0);
        ui->menu_confirm_labels[i] = label(ui->menu_confirm_rows[i],
                                           confirm_names[i], 0, 8,
                                           ui->typography.strong,
                                           i == 0U ? UI_FUI_CREAM : UI_FUI_RED);
        lv_obj_set_width(ui->menu_confirm_labels[i], 84);
        lv_obj_set_style_text_align(ui->menu_confirm_labels[i],
                                    LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_add_flag(ui->menu_dialog_scrim, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_flag(ui->menu_detail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui->menu_layer, LV_OBJ_FLAG_HIDDEN);
    ui->menu_visible = false;

    ui->pin_layer = box(screen, 0, 44, 240, 276,
                        UI_FUI_BG, LV_OPA_COVER);
    lv_obj_t *pin_frame = box(ui->pin_layer, 8, 6, 224, 221,
                              UI_FUI_PANEL, LV_OPA_COVER);
    lv_obj_set_style_border_width(pin_frame, 1, 0);
    lv_obj_set_style_border_color(pin_frame, lv_color_hex(UI_FUI_RUST), 0);
    box(pin_frame, 0, 22, 2, 195, UI_FUI_RUST, LV_OPA_COVER);
    box(pin_frame, 4, 22, 1, 195, UI_FUI_ORANGE, LV_OPA_70);
    label(pin_frame, "BOOT ACCESS", 12, 4,
          ui->typography.meta, UI_FUI_MUTED);
    add_section_rule(pin_frame, 104, 10, 108);
    box(pin_frame, 12, 21, 200, 1, UI_FUI_RUST, LV_OPA_COVER);

    lv_obj_t *pin_code_top = box(pin_frame, 12, 32, 34, 26,
                                  UI_FUI_RUST, LV_OPA_COVER);
    lv_obj_t *pin_code_bottom = box(pin_frame, 12, 58, 34, 26,
                                     UI_FUI_ORANGE, LV_OPA_COVER);
    label(pin_code_top, "00", 7, 4,
          ui->typography.title, UI_FUI_CREAM);
    label(pin_code_bottom, "ID", 8, 4,
          ui->typography.title, UI_FUI_CREAM);
    label(pin_frame, "SECURITY GATE", 56, 33,
          ui->typography.title, UI_FUI_CREAM);
    label(pin_frame, "4 DIGIT ACCESS CODE", 56, 58,
          ui->typography.meta, UI_FUI_MUTED);
    box(pin_frame, 12, 91, 200, 1, UI_FUI_RUST, LV_OPA_COVER);

    for (unsigned i = 0; i < UI_FUI_PIN_DIGITS; i++) {
        ui->pin_digit_boxes[i] = box(pin_frame, 16 + (int)i * 50, 100,
                                     42, 46, UI_FUI_PANEL_ALT, LV_OPA_COVER);
        lv_obj_set_style_border_width(ui->pin_digit_boxes[i], 1, 0);
        lv_obj_set_style_border_color(ui->pin_digit_boxes[i],
                                      lv_color_hex(UI_FUI_RUST), 0);
        ui->pin_digit_labels[i] = label(ui->pin_digit_boxes[i], "-", 0, 9,
                                        ui->typography.display,
                                        UI_FUI_MUTED);
        lv_obj_set_size(ui->pin_digit_labels[i], 42, 25);
        lv_obj_set_style_text_align(ui->pin_digit_labels[i],
                                    LV_TEXT_ALIGN_CENTER, 0);
    }

    ui->pin_state_label = label(pin_frame, "ACCESS REQUIRED", 7, 156,
                                ui->typography.display, UI_FUI_ORANGE);
    lv_obj_set_size(ui->pin_state_label, 210, 25);
    lv_label_set_long_mode(ui->pin_state_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(ui->pin_state_label, LV_TEXT_ALIGN_CENTER, 0);
    ui->pin_detail_label = label(pin_frame, "DIGIT 1 OF 4", 14, 187,
                                 ui->typography.body, UI_FUI_TEXT);
    lv_obj_set_size(ui->pin_detail_label, 196, 18);
    lv_obj_set_style_text_align(ui->pin_detail_label, LV_TEXT_ALIGN_CENTER, 0);

    box(ui->pin_layer, 8, 254, 224, 1, UI_FUI_RUST, LV_OPA_COVER);
    ui->pin_help = label(ui->pin_layer, "UP/DN CHANGE | OK NEXT", 0, 257,
                         ui->typography.meta, UI_FUI_MUTED);
    lv_obj_set_width(ui->pin_help, 240);
    lv_obj_set_style_text_align(ui->pin_help, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_add_flag(ui->pin_layer, LV_OBJ_FLAG_HIDDEN);
    ui->pin_visible = false;

    lv_screen_load(screen);
}

void ui_fui_set_status(ui_fui_t *ui, const char *state, const char *detail,
                       uint32_t accent)
{
    if (!ui || !ui->state_label || !ui->detail_label) return;
    if (ui->menu_visible) {
        lv_obj_add_flag(ui->menu_layer, LV_OBJ_FLAG_HIDDEN);
        ui->menu_visible = false;
    }
    if (ui->pin_visible) {
        lv_obj_add_flag(ui->pin_layer, LV_OBJ_FLAG_HIDDEN);
        ui->pin_visible = false;
    }
    lv_obj_remove_flag(ui->home_layer, LV_OBJ_FLAG_HIDDEN);
    ui->accent = accent;
    lv_label_set_text(ui->state_label, state ? state : "UNKNOWN");
    lv_label_set_text(ui->detail_label, detail ? detail : "");
    lv_obj_set_style_text_color(ui->state_label, lv_color_hex(accent), 0);
    lv_obj_set_style_bg_color(ui->status_dot, lv_color_hex(accent), 0);
    lv_obj_set_style_border_color(ui->main_frame, lv_color_hex(accent), 0);
    lv_obj_set_style_border_color(ui->trace_frame, lv_color_hex(accent), 0);
    lv_obj_set_style_bg_color(ui->scan_line, lv_color_hex(accent), 0);
}

void ui_fui_set_progress(ui_fui_t *ui, bool active, uint8_t value)
{
    if (!ui || !ui->trace_counter) return;
    if (value > 100U) value = 100U;
    if (active) {
        lv_label_set_text_fmt(ui->trace_counter, "%u%%", value);
        lv_obj_set_style_text_color(ui->trace_counter,
                                    lv_color_hex(ui->accent), 0);
    } else {
        lv_label_set_text(ui->trace_counter, "");
        lv_obj_set_style_text_color(ui->trace_counter,
                                    lv_color_hex(UI_FUI_MUTED), 0);
    }
}

void ui_fui_set_trace(ui_fui_t *ui,
                      const int16_t values[][UI_FUI_TRACE_AXES], size_t count)
{
    if (!ui || !ui->trace_chart || !values) return;
    if (count > UI_FUI_TRACE_POINTS) count = UI_FUI_TRACE_POINTS;

    for (unsigned axis = 0; axis < UI_FUI_TRACE_AXES; axis++) {
        int32_t *series = lv_chart_get_series_y_array(
            ui->trace_chart, ui->trace_series[axis]);
        if (!series) continue;
        lv_chart_set_x_start_point(ui->trace_chart, ui->trace_series[axis], 0U);
        for (size_t point = 0; point < UI_FUI_TRACE_POINTS; point++) {
            series[point] = point < count ? values[point][axis] :
                                            LV_CHART_POINT_NONE;
        }
    }
    lv_chart_refresh(ui->trace_chart);
}

void ui_fui_set_trace_result(ui_fui_t *ui, ui_fui_trace_result_t result)
{
    if (!ui || !ui->trace_result_overlay) return;
    if (result == UI_FUI_TRACE_RESULT_NONE) {
        lv_obj_add_flag(ui->trace_result_overlay, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    uint32_t color = result == UI_FUI_TRACE_RESULT_PASS ? UI_FUI_GREEN :
                                                           UI_FUI_RED;
    lv_obj_remove_flag(ui->trace_result_overlay, LV_OBJ_FLAG_HIDDEN);
    if (result == UI_FUI_TRACE_RESULT_PASS) {
        ui->trace_result_points[0][0] = (lv_point_precise_t){.x = 2, .y = 20};
        ui->trace_result_points[0][1] = (lv_point_precise_t){.x = 13, .y = 31};
        ui->trace_result_points[1][0] = (lv_point_precise_t){.x = 13, .y = 31};
        ui->trace_result_points[1][1] = (lv_point_precise_t){.x = 39, .y = 4};
    } else {
        ui->trace_result_points[0][0] = (lv_point_precise_t){.x = 2, .y = 2};
        ui->trace_result_points[0][1] = (lv_point_precise_t){.x = 37, .y = 37};
        ui->trace_result_points[1][0] = (lv_point_precise_t){.x = 37, .y = 2};
        ui->trace_result_points[1][1] = (lv_point_precise_t){.x = 2, .y = 37};
    }

    for (unsigned i = 0; i < 2U; i++) {
        lv_line_set_points_mutable(ui->trace_result_lines[i],
                                   ui->trace_result_points[i], 2U);
        lv_obj_set_pos(ui->trace_result_lines[i], 62, 15);
        lv_obj_set_style_line_color(ui->trace_result_lines[i],
                                    lv_color_hex(color), 0);
    }
    lv_obj_invalidate(ui->trace_result_overlay);
}

void ui_fui_set_battery(ui_fui_t *ui, bool available, int soc, int millivolts)
{
    if (!ui || !ui->battery_segments[0]) return;
    (void)millivolts;
    if (!available || soc < 0 || soc > 100) {
        for (unsigned i = 0; i < UI_FUI_BATTERY_SEGMENTS; i++) {
            lv_obj_set_style_bg_color(ui->battery_segments[i],
                                      lv_color_hex(UI_FUI_CYAN_DIM), 0);
            lv_obj_set_style_bg_opa(ui->battery_segments[i], LV_OPA_40, 0);
        }
        return;
    }

    uint32_t color = soc < 15 ? UI_FUI_RED : UI_FUI_CYAN;
    unsigned filled = soc > 0 ?
        ((unsigned)soc * UI_FUI_BATTERY_SEGMENTS + 99U) / 100U : 0U;
    for (unsigned i = 0; i < UI_FUI_BATTERY_SEGMENTS; i++) {
        bool on = i < filled;
        lv_obj_set_style_bg_color(ui->battery_segments[i],
            lv_color_hex(on ? color : UI_FUI_CYAN_DIM), 0);
        lv_obj_set_style_bg_opa(ui->battery_segments[i],
            on ? LV_OPA_COVER : LV_OPA_40, 0);
    }
}

void ui_fui_set_link(ui_fui_t *ui, const char *text, uint32_t color)
{
    if (!ui || !ui->link_value || !ui->link_dot || !ui->link_chart ||
        !ui->link_series) {
        return;
    }
    lv_label_set_text(ui->link_value, text ? text : "UNKNOWN");
    lv_obj_set_style_text_color(ui->link_value, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(ui->link_dot, lv_color_hex(color), 0);
    lv_chart_set_series_color(ui->link_chart, ui->link_series,
                              lv_color_hex(color));
}

void ui_fui_set_link_quality(ui_fui_t *ui, bool available, int rssi_dbm)
{
    if (!ui || !ui->link_chart || !ui->link_series) return;
    if (!available) {
        lv_chart_set_all_values(ui->link_chart, ui->link_series,
                                LV_CHART_POINT_NONE);
        return;
    }
    if (rssi_dbm < -100) rssi_dbm = -100;
    if (rssi_dbm > -30) rssi_dbm = -30;
    lv_chart_set_next_value(ui->link_chart, ui->link_series, rssi_dbm);
}

void ui_fui_set_manager(ui_fui_t *ui, ui_fui_manager_view_t view,
                        uint8_t selected, uint8_t action_selected,
                        ui_fui_confirm_choice_t confirm_choice,
                        uint8_t valid_mask,
                        const char *const names[UI_FUI_MENU_ITEMS])
{
    if (!ui || !ui->menu_layer || !ui->home_layer || !names) return;
    if (selected >= UI_FUI_MENU_ITEMS) selected = 0U;
    if (action_selected >= UI_FUI_MENU_ACTIONS) action_selected = 0U;
    if (confirm_choice >= UI_FUI_CONFIRM_CHOICES) {
        confirm_choice = UI_FUI_CONFIRM_CANCEL;
    }
    if (ui->pin_visible) {
        lv_obj_add_flag(ui->pin_layer, LV_OBJ_FLAG_HIDDEN);
        ui->pin_visible = false;
    }
    if (!ui->menu_visible) {
        lv_obj_add_flag(ui->home_layer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui->menu_layer, LV_OBJ_FLAG_HIDDEN);
        ui->menu_visible = true;
    }

    for (unsigned i = 0; i < UI_FUI_MENU_ITEMS; i++) {
        bool trained = (valid_mask & (1U << i)) != 0U;
        bool focused = i == selected;
        lv_label_set_text(ui->menu_row_name[i], names[i] ? names[i] : "UNNAMED");
        lv_label_set_text(ui->menu_row_state[i],
                          trained ? "RECORDED" : "NOT RECORDED");
        lv_obj_set_style_text_color(ui->menu_row_state[i],
                                    lv_color_hex(trained ? UI_FUI_TEAL :
                                                           UI_FUI_AMBER), 0);
        lv_obj_set_style_bg_color(ui->menu_rows[i],
                                  lv_color_hex(focused ? UI_FUI_PANEL_ALT :
                                                         UI_FUI_PANEL), 0);
        lv_obj_set_style_border_color(ui->menu_rows[i],
                                      lv_color_hex(focused ? UI_FUI_ORANGE :
                                                         UI_FUI_RUST), 0);
        lv_obj_set_style_bg_color(ui->menu_row_index[i],
                                  lv_color_hex(focused ? UI_FUI_ORANGE :
                                                         UI_FUI_RUST), 0);
    }

    bool detail = view != UI_FUI_MANAGER_LIST;
    if (detail) {
        bool trained = (valid_mask & (1U << selected)) != 0U;
        lv_obj_add_flag(ui->menu_list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui->menu_detail, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ui->menu_detail_index, selected == 0U ? "01" : "02");
        lv_label_set_text(ui->menu_detail_name,
                          names[selected] ? names[selected] : "UNNAMED");
        lv_label_set_text(ui->menu_detail_state,
                          trained ? "RECORDED" : "NOT RECORDED");
        lv_obj_set_style_text_color(ui->menu_detail_state,
                                    lv_color_hex(trained ? UI_FUI_TEAL :
                                                           UI_FUI_AMBER), 0);
        for (unsigned i = 0; i < UI_FUI_MENU_ACTIONS; i++) {
            bool focused = i == action_selected;
            bool clear = i == 1U;
            lv_label_set_text(ui->menu_action_labels[i],
                clear ? "CLEAR GESTURE" :
                        (trained ? "RE-RECORD" : "RECORD GESTURE"));
            uint32_t text_color = clear ? (trained ? UI_FUI_RED : UI_FUI_MUTED) :
                                          UI_FUI_CREAM;
            lv_obj_set_style_text_color(ui->menu_action_labels[i],
                                        lv_color_hex(text_color), 0);
            lv_obj_set_style_bg_color(ui->menu_action_rows[i],
                lv_color_hex(focused ? UI_FUI_PANEL_ALT : UI_FUI_PANEL), 0);
            lv_obj_set_style_border_color(ui->menu_action_rows[i],
                lv_color_hex(focused ? UI_FUI_ORANGE : UI_FUI_RUST), 0);
            lv_obj_set_style_bg_color(ui->menu_action_codes[i],
                lv_color_hex(focused ? UI_FUI_ORANGE : UI_FUI_RUST), 0);
        }
        lv_label_set_text(ui->menu_help,
                          "UP/DN | OK RUN | HOLD UP BACK");
    } else {
        lv_obj_remove_flag(ui->menu_list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui->menu_detail, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ui->menu_help,
                          "UP/DN | OK OPEN | HOLD UP EXIT");
    }

    if (view == UI_FUI_MANAGER_CLEAR_CONFIRM ||
        view == UI_FUI_MANAGER_CLEAR_DONE ||
        view == UI_FUI_MANAGER_CLEAR_ERROR) {
        lv_obj_remove_flag(ui->menu_dialog_scrim, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ui->menu_dialog_name,
                          names[selected] ? names[selected] : "UNNAMED");
        bool confirmation = view == UI_FUI_MANAGER_CLEAR_CONFIRM;
        for (unsigned i = 0; i < UI_FUI_CONFIRM_CHOICES; i++) {
            if (confirmation) {
                lv_obj_remove_flag(ui->menu_confirm_rows[i], LV_OBJ_FLAG_HIDDEN);
                bool focused = i == (unsigned)confirm_choice;
                lv_obj_set_style_bg_color(ui->menu_confirm_rows[i],
                    lv_color_hex(focused ? UI_FUI_RUST : UI_FUI_PANEL), 0);
                lv_obj_set_style_border_color(ui->menu_confirm_rows[i],
                    lv_color_hex(focused ? UI_FUI_ORANGE : UI_FUI_RUST), 0);
                lv_obj_set_style_text_color(ui->menu_confirm_labels[i],
                    lv_color_hex(focused ? UI_FUI_CREAM :
                                 (i == UI_FUI_CONFIRM_CLEAR ? UI_FUI_RED :
                                                                  UI_FUI_MUTED)),
                    0);
            } else {
                lv_obj_add_flag(ui->menu_confirm_rows[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (confirmation) {
            lv_label_set_text(ui->menu_dialog_title, "CLEAR GESTURE?");
            lv_label_set_text(ui->menu_dialog_detail, "UP/DN SELECT | OK CONFIRM");
            lv_obj_set_style_text_color(ui->menu_dialog_title,
                                        lv_color_hex(UI_FUI_ORANGE), 0);
        } else if (view == UI_FUI_MANAGER_CLEAR_DONE) {
            lv_label_set_text(ui->menu_dialog_title, "GESTURE CLEARED");
            lv_label_set_text(ui->menu_dialog_detail, "RETURNING TO MENU");
            lv_obj_set_style_text_color(ui->menu_dialog_title,
                                        lv_color_hex(UI_FUI_GREEN), 0);
        } else {
            lv_label_set_text(ui->menu_dialog_title, "CLEAR FAILED");
            lv_label_set_text(ui->menu_dialog_detail, "GESTURE WAS NOT CHANGED");
            lv_obj_set_style_text_color(ui->menu_dialog_title,
                                        lv_color_hex(UI_FUI_RED), 0);
        }
    } else {
        lv_obj_add_flag(ui->menu_dialog_scrim, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_fui_set_pin(ui_fui_t *ui, uint8_t position, uint8_t digit,
                    ui_fui_pin_result_t result)
{
    if (!ui || !ui->pin_layer || !ui->pin_state_label) return;
    if (position >= UI_FUI_PIN_DIGITS) position = UI_FUI_PIN_DIGITS - 1U;
    if (digit > 9U) digit = 0U;

    if (ui->menu_visible) {
        lv_obj_add_flag(ui->menu_layer, LV_OBJ_FLAG_HIDDEN);
        ui->menu_visible = false;
    }
    if (!ui->pin_visible) {
        lv_obj_add_flag(ui->home_layer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui->pin_layer, LV_OBJ_FLAG_HIDDEN);
        ui->pin_visible = true;
    }

    char digit_text[2] = {(char)('0' + digit), '\0'};
    for (unsigned i = 0; i < UI_FUI_PIN_DIGITS; i++) {
        const char *text = "-";
        uint32_t color = UI_FUI_MUTED;
        uint32_t border = UI_FUI_RUST;
        uint32_t background = UI_FUI_PANEL_ALT;
        if (result == UI_FUI_PIN_ACCEPTED) {
            text = "*";
            color = UI_FUI_GREEN;
            border = UI_FUI_GREEN;
        } else if (result == UI_FUI_PIN_REJECTED) {
            text = "X";
            color = UI_FUI_RED;
            border = UI_FUI_RED;
        } else if (i < position) {
            text = "*";
            color = UI_FUI_CREAM;
        } else if (i == position) {
            text = digit_text;
            color = UI_FUI_ORANGE;
            border = UI_FUI_ORANGE;
            background = UI_FUI_RUST;
        }
        lv_label_set_text(ui->pin_digit_labels[i], text);
        lv_obj_set_style_text_color(ui->pin_digit_labels[i],
                                    lv_color_hex(color), 0);
        lv_obj_set_style_border_color(ui->pin_digit_boxes[i],
                                      lv_color_hex(border), 0);
        lv_obj_set_style_bg_color(ui->pin_digit_boxes[i],
                                  lv_color_hex(background), 0);
    }

    if (result == UI_FUI_PIN_ACCEPTED) {
        lv_label_set_text(ui->pin_state_label, "ACCESS GRANTED");
        lv_label_set_text(ui->pin_detail_label, "STARTING SECURE SERVICES");
        lv_label_set_text(ui->pin_help, "SECURE SESSION OPEN");
        lv_obj_set_style_text_color(ui->pin_state_label,
                                    lv_color_hex(UI_FUI_GREEN), 0);
    } else if (result == UI_FUI_PIN_REJECTED) {
        lv_label_set_text(ui->pin_state_label, "ACCESS DENIED");
        lv_label_set_text(ui->pin_detail_label, "CODE CLEARED | TRY AGAIN");
        lv_label_set_text(ui->pin_help, "PLEASE WAIT");
        lv_obj_set_style_text_color(ui->pin_state_label,
                                    lv_color_hex(UI_FUI_RED), 0);
    } else {
        lv_label_set_text(ui->pin_state_label, "ACCESS REQUIRED");
        lv_label_set_text_fmt(ui->pin_detail_label, "DIGIT %u OF %u",
                              position + 1U, UI_FUI_PIN_DIGITS);
        lv_label_set_text(ui->pin_help, "UP/DN CHANGE | OK NEXT");
        lv_obj_set_style_text_color(ui->pin_state_label,
                                    lv_color_hex(UI_FUI_ORANGE), 0);
    }
}
