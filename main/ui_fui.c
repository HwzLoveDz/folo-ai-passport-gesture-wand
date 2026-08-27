#include "ui_fui.h"

#include <stddef.h>
#include <string.h>

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

static void add_grid(lv_obj_t *screen)
{
    for (int x = 0; x < 240; x += 40) {
        box(screen, x, 44, 1, 254, UI_FUI_CYAN_DIM, 26);
    }
    for (int y = 44; y < 298; y += 36) {
        box(screen, 0, y, 240, 1, UI_FUI_CYAN_DIM, 24);
    }
}

static void add_frame_corners(lv_obj_t *parent, int width, int height,
                              uint32_t color)
{
    const int inset = 2;
    const int right = width - inset;
    const int bottom = height - inset;

    // 左上角原本完整；另外三角向左/上内缩 2px，避免末端落在
    // 容器裁剪边界之外而只显示一根线。
    box(parent, 0, 0, 22, 2, color, LV_OPA_COVER);
    box(parent, 0, 0, 2, 14, color, LV_OPA_COVER);
    box(parent, right - 22, 0, 22, 2, color, LV_OPA_COVER);
    box(parent, right - 2, 0, 2, 14, color, LV_OPA_COVER);
    box(parent, 0, bottom - 2, 22, 2, color, LV_OPA_COVER);
    box(parent, 0, bottom - 14, 2, 14, color, LV_OPA_COVER);
    box(parent, right - 22, bottom - 2, 22, 2, color, LV_OPA_COVER);
    box(parent, right - 2, bottom - 14, 2, 14, color, LV_OPA_COVER);
}

static void scan_y(void *object, int32_t y)
{
    lv_obj_set_y((lv_obj_t *)object, y);
}

static void pulse_opacity(void *object, int32_t opacity)
{
    lv_obj_set_style_opa((lv_obj_t *)object, (lv_opa_t)opacity, 0);
}

static void start_scan_animation(lv_obj_t *scan_line)
{
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, scan_line);
    lv_anim_set_exec_cb(&animation, scan_y);
    lv_anim_set_values(&animation, 31, 67);
    lv_anim_set_duration(&animation, 1350);
    lv_anim_set_repeat_delay(&animation, 220);
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

static void flash_width(void *object, int32_t width)
{
    lv_obj_set_style_border_width((lv_obj_t *)object, width, 0);
}

void ui_fui_create(ui_fui_t *ui)
{
    if (!ui) return;
    memset(ui, 0, sizeof(*ui));
    ui->accent = UI_FUI_CYAN;

    lv_obj_t *screen = lv_obj_create(NULL);
    ui->screen = screen;
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(UI_FUI_BG), 0);
    add_grid(screen);

    box(screen, 0, 0, 240, 44, UI_FUI_PANEL, LV_OPA_COVER);
    box(screen, 0, 41, 240, 2, UI_FUI_CYAN, LV_OPA_COVER);
    box(screen, 0, 43, 156, 1, UI_FUI_MAGENTA, LV_OPA_70);

    lv_obj_t *title = label(screen, "MOTE WAND", 10, 3,
                            &lv_font_montserrat_14, UI_FUI_TEXT);
    lv_obj_set_style_text_letter_space(title, 1, 0);
    lv_obj_t *subtitle = label(screen, "MOTION / BLE HID", 10, 23,
                               &lv_font_montserrat_12, UI_FUI_MUTED);
    lv_obj_set_style_text_letter_space(subtitle, 1, 0);

    ui->battery_label = label(screen, "--%", 170, 3,
                              &lv_font_montserrat_14, UI_FUI_CYAN);
    lv_obj_set_width(ui->battery_label, 48);
    lv_obj_set_style_text_align(ui->battery_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t *battery_shell = box(screen, 177, 26, 39, 10,
                                  UI_FUI_BG, LV_OPA_COVER);
    lv_obj_set_style_border_width(battery_shell, 1, 0);
    lv_obj_set_style_border_color(battery_shell, lv_color_hex(UI_FUI_CYAN_DIM), 0);
    box(screen, 216, 28, 3, 5, UI_FUI_CYAN_DIM, LV_OPA_COVER);
    ui->battery_fill = box(battery_shell, 2, 2, 1, 4,
                           UI_FUI_CYAN, LV_OPA_COVER);

    ui->main_frame = box(screen, 8, 50, 224, 151,
                         UI_FUI_PANEL, LV_OPA_90);
    lv_obj_set_style_border_width(ui->main_frame, 1, 0);
    lv_obj_set_style_border_color(ui->main_frame, lv_color_hex(UI_FUI_CYAN_DIM), 0);
    add_frame_corners(ui->main_frame, 224, 151, UI_FUI_CYAN);

    label(ui->main_frame, "MOTION SIGNATURE", 28, 4,
          &lv_font_montserrat_12, UI_FUI_MUTED);
    ui->status_dot = box(ui->main_frame, 204, 7, 6, 6,
                         UI_FUI_CYAN, LV_OPA_COVER);
    lv_obj_set_style_radius(ui->status_dot, LV_RADIUS_CIRCLE, 0);
    start_dot_animation(ui->status_dot);
    box(ui->main_frame, 12, 21, 200, 1, UI_FUI_CYAN_DIM, LV_OPA_60);

    ui->scan_line = box(ui->main_frame, 92, 31, 40, 1,
                        UI_FUI_CYAN, LV_OPA_60);
    start_scan_animation(ui->scan_line);

    lv_obj_t *scope_outer = box(ui->main_frame, 91, 28, 42, 42,
                                UI_FUI_BG, LV_OPA_TRANSP);
    lv_obj_set_style_radius(scope_outer, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(scope_outer, 1, 0);
    lv_obj_set_style_border_color(scope_outer, lv_color_hex(UI_FUI_CYAN_DIM), 0);
    lv_obj_t *scope_inner = box(ui->main_frame, 99, 36, 26, 26,
                                UI_FUI_BG, LV_OPA_TRANSP);
    lv_obj_set_style_radius(scope_inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(scope_inner, 1, 0);
    lv_obj_set_style_border_color(scope_inner, lv_color_hex(0x123D48), 0);
    box(ui->main_frame, 111, 25, 1, 49, UI_FUI_CYAN_DIM, LV_OPA_50);
    box(ui->main_frame, 86, 49, 52, 1, UI_FUI_CYAN_DIM, LV_OPA_50);

    ui->state_label = label(ui->main_frame, "BOOT SEQUENCE", 7, 76,
                            &lv_font_montserrat_14, UI_FUI_CYAN);
    lv_obj_set_width(ui->state_label, 210);
    lv_obj_set_style_text_align(ui->state_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(ui->state_label, 1, 0);

    ui->detail_label = label(ui->main_frame, "INITIALIZING SYSTEM", 14, 96,
                             &lv_font_montserrat_12, UI_FUI_TEXT);
    lv_obj_set_size(ui->detail_label, 196, 29);
    lv_obj_set_style_text_align(ui->detail_label, LV_TEXT_ALIGN_CENTER, 0);

    ui->progress_box = box(ui->main_frame, 12, 129, 200, 17,
                           UI_FUI_BG, LV_OPA_80);
    lv_obj_set_style_border_width(ui->progress_box, 1, 0);
    lv_obj_set_style_border_color(ui->progress_box, lv_color_hex(UI_FUI_CYAN_DIM), 0);
    for (unsigned i = 0; i < UI_FUI_PROGRESS_SEGMENTS; i++) {
        ui->progress_segments[i] = box(ui->progress_box, 5 + (int)i * 9, 5,
                                       6, 7, UI_FUI_CYAN_DIM, LV_OPA_60);
    }
    ui->progress_label = label(ui->progress_box, "STBY", 155, 0,
                               &lv_font_montserrat_12, UI_FUI_MUTED);
    lv_obj_set_width(ui->progress_label, 39);
    lv_obj_set_style_text_align(ui->progress_label, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *telemetry = box(screen, 8, 209, 224, 82,
                              UI_FUI_PANEL_ALT, LV_OPA_90);
    lv_obj_set_style_border_width(telemetry, 1, 0);
    lv_obj_set_style_border_color(telemetry, lv_color_hex(UI_FUI_CYAN_DIM), 0);
    add_frame_corners(telemetry, 224, 82, UI_FUI_MAGENTA);
    label(telemetry, "SYSTEM TELEMETRY", 28, 4,
          &lv_font_montserrat_12, UI_FUI_MUTED);
    box(telemetry, 12, 21, 200, 1, UI_FUI_CYAN_DIM, LV_OPA_60);

    ui->link_dot = box(telemetry, 14, 30, 5, 5,
                       UI_FUI_MAGENTA, LV_OPA_COVER);
    lv_obj_set_style_radius(ui->link_dot, LV_RADIUS_CIRCLE, 0);
    label(telemetry, "LINK", 26, 25, &lv_font_montserrat_12, UI_FUI_MUTED);
    ui->link_value = label(telemetry, "PAIRING HOST", 72, 25,
                           &lv_font_montserrat_12, UI_FUI_MAGENTA);

    label(telemetry, "IMU", 14, 43, &lv_font_montserrat_12, UI_FUI_MUTED);
    label(telemetry, "QMI8658A / 100HZ", 72, 43,
          &lv_font_montserrat_12, UI_FUI_TEXT);
    label(telemetry, "PWR", 14, 61, &lv_font_montserrat_12, UI_FUI_MUTED);
    ui->battery_voltage = label(telemetry, "--% / ----mV", 72, 61,
                                &lv_font_montserrat_12, UI_FUI_TEXT);

    box(screen, 8, 298, 224, 1, UI_FUI_CYAN_DIM, LV_OPA_COVER);
    lv_obj_t *help = label(screen, "OK TRACE  /  UP RESET", 0, 302,
                           &lv_font_montserrat_12, UI_FUI_MUTED);
    lv_obj_set_width(help, 240);
    lv_obj_set_style_text_align(help, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(help, 1, 0);

    lv_screen_load(screen);
}

void ui_fui_set_status(ui_fui_t *ui, const char *state, const char *detail,
                       uint32_t accent)
{
    if (!ui || !ui->state_label || !ui->detail_label) return;
    ui->accent = accent;
    lv_label_set_text(ui->state_label, state ? state : "UNKNOWN");
    lv_label_set_text(ui->detail_label, detail ? detail : "");
    lv_obj_set_style_text_color(ui->state_label, lv_color_hex(accent), 0);
    lv_obj_set_style_bg_color(ui->status_dot, lv_color_hex(accent), 0);
    lv_obj_set_style_border_color(ui->main_frame, lv_color_hex(accent), 0);
    lv_obj_set_style_bg_color(ui->scan_line, lv_color_hex(accent), 0);
}

void ui_fui_set_progress(ui_fui_t *ui, bool active, uint8_t value)
{
    if (!ui || !ui->progress_box) return;
    if (value > 100U) value = 100U;
    unsigned filled = active ?
        ((unsigned)value * UI_FUI_PROGRESS_SEGMENTS + 99U) / 100U : 0U;
    for (unsigned i = 0; i < UI_FUI_PROGRESS_SEGMENTS; i++) {
        bool on = i < filled;
        lv_obj_set_style_bg_color(ui->progress_segments[i],
            lv_color_hex(on ? ui->accent : UI_FUI_CYAN_DIM), 0);
        lv_obj_set_style_bg_opa(ui->progress_segments[i],
            on ? LV_OPA_COVER : LV_OPA_40, 0);
    }
    if (active) {
        lv_label_set_text_fmt(ui->progress_label, "%u%%", value);
        lv_obj_set_style_text_color(ui->progress_label,
                                    lv_color_hex(ui->accent), 0);
    } else {
        lv_label_set_text(ui->progress_label, "STBY");
        lv_obj_set_style_text_color(ui->progress_label,
                                    lv_color_hex(UI_FUI_MUTED), 0);
    }
}

void ui_fui_set_battery(ui_fui_t *ui, bool available, int soc, int millivolts)
{
    if (!ui || !ui->battery_label || !ui->battery_fill ||
        !ui->battery_voltage) return;
    if (!available || soc < 0 || soc > 100) {
        lv_label_set_text(ui->battery_label, "--%");
        lv_obj_set_style_text_color(ui->battery_label,
                                    lv_color_hex(UI_FUI_CYAN), 0);
        lv_obj_set_width(ui->battery_fill, 1);
        lv_obj_set_style_bg_color(ui->battery_fill,
                                  lv_color_hex(UI_FUI_CYAN_DIM), 0);
        lv_label_set_text(ui->battery_voltage, "--% / ----mV");
        lv_obj_set_style_text_color(ui->battery_voltage,
                                    lv_color_hex(UI_FUI_TEXT), 0);
        return;
    }

    uint32_t color = soc < 15 ? UI_FUI_RED :
                     soc < 30 ? UI_FUI_AMBER : UI_FUI_GREEN;
    lv_label_set_text_fmt(ui->battery_label, "%d%%", soc);
    lv_obj_set_style_text_color(ui->battery_label, lv_color_hex(color), 0);
    int width = soc == 0 ? 1 : (33 * soc + 99) / 100;
    lv_obj_set_width(ui->battery_fill, width);
    lv_obj_set_style_bg_color(ui->battery_fill, lv_color_hex(color), 0);
    if (millivolts > 0) {
        lv_label_set_text_fmt(ui->battery_voltage, "%d%% / %dmV",
                              soc, millivolts);
    } else {
        lv_label_set_text_fmt(ui->battery_voltage, "%d%% / ----mV", soc);
    }
    lv_obj_set_style_text_color(ui->battery_voltage, lv_color_hex(color), 0);
}

void ui_fui_set_link(ui_fui_t *ui, const char *text, uint32_t color)
{
    if (!ui || !ui->link_value || !ui->link_dot) return;
    lv_label_set_text(ui->link_value, text ? text : "UNKNOWN");
    lv_obj_set_style_text_color(ui->link_value, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(ui->link_dot, lv_color_hex(color), 0);
}

void ui_fui_flash_success(ui_fui_t *ui)
{
    if (!ui || !ui->main_frame) return;
    lv_anim_delete(ui->main_frame, flash_width);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, ui->main_frame);
    lv_anim_set_exec_cb(&animation, flash_width);
    lv_anim_set_values(&animation, 1, 4);
    lv_anim_set_duration(&animation, 100);
    lv_anim_set_playback_duration(&animation, 260);
    lv_anim_set_path_cb(&animation, lv_anim_path_step);
    lv_anim_start(&animation);
}
