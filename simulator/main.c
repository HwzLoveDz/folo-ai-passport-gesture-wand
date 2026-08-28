#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "png_writer.h"
#include "ui_fui.h"

#define DISPLAY_WIDTH  240U
#define DISPLAY_HEIGHT 320U
#define DRAW_ROWS       48U

#ifndef MOTE_WAND_ROOT
#error "MOTE_WAND_ROOT must point to the firmware project"
#endif

#define MEDIUM_TTF MOTE_WAND_ROOT \
    "/managed_components/lvgl__lvgl/scripts/built_in_font/Montserrat-Medium.ttf"
#define BOLD_TTF MOTE_WAND_ROOT \
    "/managed_components/lvgl__lvgl/tests/src/test_files/fonts/Montserrat-Bold.ttf"
#define KODE_FONT_DIR MOTE_WAND_ROOT "/main/fonts/kode_mono/"
#define FONT_CACHE MOTE_WAND_ROOT "/simulator/font-cache/"

typedef struct {
    const char *name;
    const char *regular_path;
    const char *bold_path;
    int32_t meta_size;
    int32_t body_size;
    int32_t strong_size;
    int32_t title_size;
    int32_t display_size;
} font_profile_t;

typedef struct {
    uint8_t *regular_data;
    size_t regular_size;
    uint8_t *bold_data;
    size_t bold_size;
    lv_font_t *meta;
    lv_font_t *body;
    lv_font_t *strong;
    lv_font_t *title;
    lv_font_t *display;
} font_resources_t;

static uint16_t s_framebuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT];
static uint8_t s_draw_buffer[DISPLAY_WIDTH * DRAW_ROWS * 2U];

static void display_flush(lv_display_t *display, const lv_area_t *area,
                          uint8_t *pixels)
{
    const unsigned source_width = (unsigned)(area->x2 - area->x1 + 1);
    const uint16_t *source = (const uint16_t *)pixels;
    for (int32_t y = area->y1; y <= area->y2; y++) {
        if (y < 0 || y >= (int32_t)DISPLAY_HEIGHT) continue;
        for (int32_t x = area->x1; x <= area->x2; x++) {
            if (x < 0 || x >= (int32_t)DISPLAY_WIDTH) continue;
            size_t source_index = (size_t)(y - area->y1) * source_width +
                                  (size_t)(x - area->x1);
            s_framebuffer[(size_t)y * DISPLAY_WIDTH + (size_t)x] =
                source[source_index];
        }
    }
    lv_display_flush_ready(display);
}

static uint8_t *read_file(const char *path, size_t *size)
{
    if (!path || !size) return NULL;
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length <= 0L || fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    uint8_t *data = malloc((size_t)length);
    if (!data) {
        fclose(file);
        return NULL;
    }
    if (fread(data, 1U, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return data;
}

static const font_profile_t *find_font_profile(const char *name)
{
    static const font_profile_t profiles[] = {
        {
            .name = "weighted",
            .regular_path = MEDIUM_TTF,
            .bold_path = BOLD_TTF,
            .meta_size = 10,
            .body_size = 12,
            .strong_size = 12,
            .title_size = 14,
            .display_size = 20,
        },
        {
            .name = "kode",
            .regular_path = KODE_FONT_DIR "KodeMono-Regular.ttf",
            .bold_path = KODE_FONT_DIR "KodeMono-Bold.ttf",
            .meta_size = 11,
            .body_size = 13,
            .strong_size = 13,
            .title_size = 15,
            .display_size = 21,
        },
        {
            .name = "space",
            .regular_path = FONT_CACHE "SpaceMono-Regular.ttf",
            .bold_path = FONT_CACHE "SpaceMono-Bold.ttf",
            .meta_size = 10,
            .body_size = 12,
            .strong_size = 12,
            .title_size = 14,
            .display_size = 20,
        },
        {
            .name = "plex",
            .regular_path = FONT_CACHE "IBMPlexMono-Regular.ttf",
            .bold_path = FONT_CACHE "IBMPlexMono-Bold.ttf",
            .meta_size = 10,
            .body_size = 12,
            .strong_size = 12,
            .title_size = 14,
            .display_size = 20,
        },
    };
    for (size_t index = 0; index < sizeof(profiles) / sizeof(profiles[0]);
         index++) {
        if (strcmp(name, profiles[index].name) == 0) return &profiles[index];
    }
    return NULL;
}

static bool load_typography(font_resources_t *resources,
                            ui_fui_typography_t *typography,
                            const font_profile_t *profile)
{
    if (!resources || !typography || !profile) return false;
    memset(resources, 0, sizeof(*resources));
    resources->regular_data = read_file(profile->regular_path,
                                        &resources->regular_size);
    resources->bold_data = read_file(profile->bold_path,
                                     &resources->bold_size);
    if (!resources->regular_data || !resources->bold_data) return false;

    resources->meta = lv_tiny_ttf_create_data(
        resources->regular_data, resources->regular_size, profile->meta_size);
    resources->body = lv_tiny_ttf_create_data(
        resources->regular_data, resources->regular_size, profile->body_size);
    resources->strong = lv_tiny_ttf_create_data(
        resources->bold_data, resources->bold_size, profile->strong_size);
    resources->title = lv_tiny_ttf_create_data(
        resources->bold_data, resources->bold_size, profile->title_size);
    resources->display = lv_tiny_ttf_create_data(
        resources->bold_data, resources->bold_size, profile->display_size);
    if (!resources->meta || !resources->body || !resources->strong ||
        !resources->title || !resources->display) {
        return false;
    }
    *typography = (ui_fui_typography_t){
        .meta = resources->meta,
        .body = resources->body,
        .strong = resources->strong,
        .title = resources->title,
        .display = resources->display,
    };
    return true;
}

static void make_trace(int16_t trace[][UI_FUI_TRACE_AXES])
{
    for (unsigned point = 0; point < UI_FUI_TRACE_POINTS; point++) {
        int phase = (int)(point % 16U);
        int triangle = phase < 8 ? phase : 15 - phase;
        trace[point][0] = (int16_t)(triangle * 15 - 50);
        trace[point][1] = (int16_t)(50 - triangle * 12);
        trace[point][2] = (int16_t)(((int)(point * 17U) % 90) - 45);
    }
}

static void make_link_history(ui_fui_t *ui)
{
    static const int rssi_samples[UI_FUI_LINK_POINTS] = {
        -72, -64, -69, -58, -62, -53, -43, -39,
    };
    for (unsigned i = 0; i < UI_FUI_LINK_POINTS; i++) {
        ui_fui_set_link_quality(ui, true, rssi_samples[i]);
    }
}

static bool configure_state(ui_fui_t *ui, const char *state)
{
    static const char *const names[UI_FUI_MENU_ITEMS] = {
        "AUTH_SEQUENCE", "LOCK_HOST", "NEW_TAB",
    };
    int16_t trace[UI_FUI_TRACE_POINTS][UI_FUI_TRACE_AXES];
    make_trace(trace);
    ui_fui_set_battery(ui, true, 76, 3970);

    if (strcmp(state, "home") == 0) {
        ui_fui_set_status(ui, "READY", "HOLD OK | MOVE | RELEASE",
                          UI_FUI_ORANGE);
        ui_fui_set_link(ui, "ONLINE", UI_FUI_GREEN);
        make_link_history(ui);
        ui_fui_set_trace(ui, trace, UI_FUI_TRACE_POINTS);
        ui_fui_set_progress(ui, false, 0U);
    } else if (strcmp(state, "recording") == 0) {
        ui_fui_set_status(ui, "RECORDING", "LOCK_HOST 2/3 | RELEASE",
                          UI_FUI_AMBER);
        ui_fui_set_link(ui, "ONLINE", UI_FUI_GREEN);
        make_link_history(ui);
        ui_fui_set_trace(ui, trace, 23U);
        ui_fui_set_progress(ui, true, 68U);
    } else if (strcmp(state, "success") == 0) {
        ui_fui_set_status(ui, "ACTION SENT", "LOCK_HOST | 91% MATCH",
                          UI_FUI_GREEN);
        ui_fui_set_link(ui, "ONLINE", UI_FUI_GREEN);
        make_link_history(ui);
        ui_fui_set_trace(ui, trace, UI_FUI_TRACE_POINTS);
        ui_fui_set_trace_result(ui, UI_FUI_TRACE_RESULT_PASS);
    } else if (strcmp(state, "menu") == 0) {
        ui_fui_set_manager(ui, UI_FUI_MANAGER_LIST, 2U, 0U,
                           UI_FUI_CONFIRM_CANCEL, 0x03U, names);
    } else if (strcmp(state, "detail") == 0) {
        ui_fui_set_manager(ui, UI_FUI_MANAGER_DETAIL, 2U, 0U,
                           UI_FUI_CONFIRM_CANCEL, 0x03U, names);
    } else if (strcmp(state, "clear-confirm") == 0) {
        ui_fui_set_manager(ui, UI_FUI_MANAGER_CLEAR_CONFIRM, 1U, 1U,
                           UI_FUI_CONFIRM_CLEAR, 0x03U, names);
    } else if (strcmp(state, "clear-done") == 0) {
        ui_fui_set_manager(ui, UI_FUI_MANAGER_CLEAR_DONE, 1U, 1U,
                           UI_FUI_CONFIRM_CLEAR, 0x01U, names);
    } else if (strcmp(state, "clear-error") == 0) {
        ui_fui_set_manager(ui, UI_FUI_MANAGER_CLEAR_ERROR, 1U, 1U,
                           UI_FUI_CONFIRM_CLEAR, 0x03U, names);
    } else if (strcmp(state, "pin") == 0) {
        ui_fui_set_pin(ui, 1U, 0U, UI_FUI_PIN_INPUT);
    } else if (strcmp(state, "pin-denied") == 0) {
        ui_fui_set_pin(ui, 3U, 0U, UI_FUI_PIN_REJECTED);
    } else if (strcmp(state, "pin-accepted") == 0) {
        ui_fui_set_pin(ui, 3U, 0U, UI_FUI_PIN_ACCEPTED);
    } else {
        return false;
    }
    return true;
}

static void print_usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s --state <home|recording|success|menu|detail|"
            "clear-confirm|clear-done|clear-error|pin|"
            "pin-denied|pin-accepted> "
            "--font <current|weighted|kode|space|plex> "
            "--output <preview.png>\n",
            program);
}

int main(int argc, char **argv)
{
    const char *state = "home";
    const char *font_mode = "current";
    const char *output = "mote-wand-preview.png";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--state") == 0 && i + 1 < argc) {
            state = argv[++i];
        } else if (strcmp(argv[i], "--font") == 0 && i + 1 < argc) {
            font_mode = argv[++i];
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else {
            print_usage(argv[0]);
            return 2;
        }
    }
    const font_profile_t *font_profile = find_font_profile(font_mode);
    if (strcmp(font_mode, "current") != 0 && !font_profile) {
        print_usage(argv[0]);
        return 2;
    }

    lv_init();
    lv_display_t *display = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    if (!display) {
        fprintf(stderr, "Unable to create LVGL display\n");
        return 1;
    }
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, display_flush);
    lv_display_set_buffers(display, s_draw_buffer, NULL, sizeof(s_draw_buffer),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_default(display);

    ui_fui_t ui;
    font_resources_t fonts = {0};
    ui_fui_typography_t typography = {0};
    if (font_profile) {
        if (!load_typography(&fonts, &typography, font_profile)) {
            fprintf(stderr,
                    "Unable to load simulator typography assets for %s\n",
                    font_mode);
            return 1;
        }
        ui_fui_create_with_typography(&ui, &typography);
    } else {
        ui_fui_create(&ui);
    }

    if (!configure_state(&ui, state)) {
        fprintf(stderr, "Unknown preview state: %s\n", state);
        print_usage(argv[0]);
        return 2;
    }

    // Move the scan animation into the chart instead of capturing its first
    // frame, then force one deterministic full refresh.
    lv_tick_inc(620U);
    lv_timer_handler();
    lv_obj_update_layout(ui.screen);
    lv_refr_now(display);

    if (png_write_rgb565(output, s_framebuffer,
                         DISPLAY_WIDTH, DISPLAY_HEIGHT) != 0) {
        fprintf(stderr, "Unable to write preview: %s\n", output);
        return 1;
    }
    printf("rendered state=%s font=%s output=%s\n",
           state, font_mode, output);
    return 0;
}
