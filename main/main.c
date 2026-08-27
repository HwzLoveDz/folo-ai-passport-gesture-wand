#include "gesture_key.h"
#include "gesture_sound.h"
#include "ui_fui.h"

#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "bsp_qmi8658a.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "main";

#define BATTERY_REFRESH_MS    30000U
#define BATTERY_DEFER_POLL_MS 50U

_Static_assert(UI_FUI_TRACE_POINTS == GESTURE_KEY_TRACE_POINTS,
               "UI and gesture trace lengths must match");
_Static_assert(UI_FUI_TRACE_AXES == GESTURE_KEY_TRACE_AXES,
               "UI and gesture trace axes must match");
_Static_assert(UI_FUI_PIN_DIGITS == GESTURE_KEY_PIN_DIGITS,
               "UI and gesture PIN lengths must match");

static ui_fui_t s_ui;
static gesture_key_state_t s_last_state = (gesture_key_state_t)-1;
static uint64_t s_last_values = UINT64_MAX;
static uint32_t s_last_trace_revision = UINT32_MAX;
static uint32_t s_last_link_revision = UINT32_MAX;
static int s_last_battery_soc = -2;
static int s_last_battery_mv = -2;
static bool s_last_battery_available;
static bool s_fatal_error;

static atomic_bool s_battery_available;
static atomic_int s_battery_soc;
static atomic_int s_battery_mv;
static bool s_battery_initialized;
static bool s_battery_failure_logged;
static bool s_ble_battery_failure_logged;

static const char *state_text(gesture_key_state_t state)
{
    switch (state) {
    case GESTURE_KEY_PIN_ENTRY: return "ACCESS REQUIRED";
    case GESTURE_KEY_PIN_ERROR: return "ACCESS DENIED";
    case GESTURE_KEY_PIN_ACCEPTED: return "ACCESS GRANTED";
    case GESTURE_KEY_PAIRING: return "PAIRING";
    case GESTURE_KEY_CALIBRATING: return "ZEROING";
    case GESTURE_KEY_MENU_LIST: return "GESTURE MANAGER";
    case GESTURE_KEY_MENU_DETAIL: return "GESTURE DETAIL";
    case GESTURE_KEY_MENU_CLEAR_CONFIRM: return "CLEAR GESTURE?";
    case GESTURE_KEY_MENU_CLEAR_DONE: return "GESTURE CLEARED";
    case GESTURE_KEY_MENU_CLEAR_ERROR: return "CLEAR FAILED";
    case GESTURE_KEY_LEARN_READY: return "ENROLL READY";
    case GESTURE_KEY_LEARN_RECORDING: return "RECORDING";
    case GESTURE_KEY_LEARN_SAMPLE_OK: return "SAMPLE SAVED";
    case GESTURE_KEY_LEARN_RETRY: return "RECORD AGAIN";
    case GESTURE_KEY_LEARN_RESTART: return "RESTART TRAINING";
    case GESTURE_KEY_LEARN_CONFLICT: return "TOO SIMILAR";
    case GESTURE_KEY_LEARN_SAVED: return "GESTURE SAVED";
    case GESTURE_KEY_READY: return "READY";
    case GESTURE_KEY_VERIFY_RECORDING: return "RECORDING";
    case GESTURE_KEY_MATCHED: return "ACTION SENT";
    case GESTURE_KEY_AMBIGUOUS: return "AMBIGUOUS";
    case GESTURE_KEY_NO_MATCH: return "NO MATCH";
    case GESTURE_KEY_INVALID_MOTION: return "MOVE MORE";
    case GESTURE_KEY_TOO_LONG: return "TRACE TOO LONG";
    case GESTURE_KEY_SEND_ERROR: return "SEND FAILED";
    case GESTURE_KEY_STORAGE_ERROR: return "SAVE FAILED";
    case GESTURE_KEY_BLE_ERROR: return "BLE ERROR";
    default: return "STARTING";
    }
}

static uint32_t state_accent(gesture_key_state_t state)
{
    switch (state) {
    case GESTURE_KEY_MATCHED:
    case GESTURE_KEY_LEARN_SAVED:
    case GESTURE_KEY_LEARN_SAMPLE_OK:
    case GESTURE_KEY_MENU_CLEAR_DONE:
    case GESTURE_KEY_PIN_ACCEPTED:
        return UI_FUI_GREEN;
    case GESTURE_KEY_CALIBRATING:
    case GESTURE_KEY_LEARN_RECORDING:
    case GESTURE_KEY_VERIFY_RECORDING:
        return UI_FUI_AMBER;
    case GESTURE_KEY_PAIRING:
    case GESTURE_KEY_LEARN_READY:
    case GESTURE_KEY_PIN_ENTRY:
        return UI_FUI_MAGENTA;
    case GESTURE_KEY_LEARN_RETRY:
    case GESTURE_KEY_LEARN_RESTART:
    case GESTURE_KEY_LEARN_CONFLICT:
    case GESTURE_KEY_AMBIGUOUS:
    case GESTURE_KEY_NO_MATCH:
    case GESTURE_KEY_INVALID_MOTION:
    case GESTURE_KEY_TOO_LONG:
    case GESTURE_KEY_SEND_ERROR:
    case GESTURE_KEY_STORAGE_ERROR:
    case GESTURE_KEY_MENU_CLEAR_ERROR:
    case GESTURE_KEY_BLE_ERROR:
    case GESTURE_KEY_PIN_ERROR:
        return UI_FUI_RED;
    default:
        return UI_FUI_CYAN;
    }
}

static ui_fui_trace_result_t trace_result(gesture_key_state_t state)
{
    switch (state) {
    case GESTURE_KEY_MATCHED:
    case GESTURE_KEY_LEARN_SAMPLE_OK:
    case GESTURE_KEY_LEARN_SAVED:
        return UI_FUI_TRACE_RESULT_PASS;
    case GESTURE_KEY_LEARN_RETRY:
    case GESTURE_KEY_LEARN_RESTART:
    case GESTURE_KEY_LEARN_CONFLICT:
    case GESTURE_KEY_AMBIGUOUS:
    case GESTURE_KEY_NO_MATCH:
    case GESTURE_KEY_INVALID_MOTION:
    case GESTURE_KEY_TOO_LONG:
    case GESTURE_KEY_SEND_ERROR:
    case GESTURE_KEY_STORAGE_ERROR:
        return UI_FUI_TRACE_RESULT_FAIL;
    default:
        return UI_FUI_TRACE_RESULT_NONE;
    }
}

static void format_detail(gesture_key_state_t state, uint8_t sample,
                          uint8_t score, uint8_t rejected,
                          gesture_macro_t active, gesture_macro_t conflicting,
                          char detail[96])
{
    const char *macro = gesture_key_macro_name(active);
    const char *other = gesture_key_macro_name(conflicting);
    switch (state) {
    case GESTURE_KEY_PAIRING:
        snprintf(detail, 96, "SELECT MOTE WAND ON HOST");
        break;
    case GESTURE_KEY_CALIBRATING:
        snprintf(detail, 96, "KEEP DEVICE STILL");
        break;
    case GESTURE_KEY_LEARN_READY:
        snprintf(detail, 96, "%s %u/3 | HOLD OK",
                 macro, sample);
        break;
    case GESTURE_KEY_LEARN_RECORDING:
        snprintf(detail, 96, "%s %u/3 | RELEASE",
                 macro, sample);
        break;
    case GESTURE_KEY_LEARN_SAMPLE_OK:
        snprintf(detail, 96, "SAMPLE %u/3 | SAVED",
                 sample > 0U ? sample - 1U : 0U);
        break;
    case GESTURE_KEY_LEARN_RETRY:
        snprintf(detail, 96, "SAMPLE %u | TRY AGAIN", rejected);
        break;
    case GESTURE_KEY_LEARN_RESTART:
        snprintf(detail, 96, "RECORD ALL 3 AGAIN");
        break;
    case GESTURE_KEY_LEARN_CONFLICT:
        snprintf(detail, 96, "USE A DIFFERENT MOTION");
        break;
    case GESTURE_KEY_LEARN_SAVED:
        snprintf(detail, 96, "%s | SAVED", macro);
        break;
    case GESTURE_KEY_READY:
        snprintf(detail, 96, "HOLD OK | MOVE | RELEASE");
        break;
    case GESTURE_KEY_VERIFY_RECORDING:
        snprintf(detail, 96, "RELEASE TO CHECK");
        break;
    case GESTURE_KEY_MATCHED:
        snprintf(detail, 96, "%s | %u%% MATCH", macro, score);
        break;
    case GESTURE_KEY_AMBIGUOUS:
        snprintf(detail, 96, "%s / %s", macro, other);
        break;
    case GESTURE_KEY_NO_MATCH:
        snprintf(detail, 96, "%u%% MATCH | NEED 75%%", score);
        break;
    case GESTURE_KEY_INVALID_MOTION:
        snprintf(detail, 96, "MOVE FOR AT LEAST 0.3S");
        break;
    case GESTURE_KEY_TOO_LONG:
        snprintf(detail, 96, "RELEASE WITHIN 2.6S");
        break;
    case GESTURE_KEY_SEND_ERROR:
        snprintf(detail, 96, "CHECK BLUETOOTH");
        break;
    case GESTURE_KEY_STORAGE_ERROR:
        snprintf(detail, 96, "SAVE FAILED");
        break;
    case GESTURE_KEY_BLE_ERROR:
        snprintf(detail, 96, "RESTART DEVICE");
        break;
    default:
        snprintf(detail, 96, "PLEASE WAIT");
        break;
    }
}

static void update_battery_ui(void)
{
    bool available = atomic_load(&s_battery_available);
    int soc = atomic_load(&s_battery_soc);
    int millivolts = atomic_load(&s_battery_mv);
    if (available == s_last_battery_available && soc == s_last_battery_soc &&
        millivolts == s_last_battery_mv) {
        return;
    }
    s_last_battery_available = available;
    s_last_battery_soc = soc;
    s_last_battery_mv = millivolts;
    ui_fui_set_battery(&s_ui, available, soc, millivolts);
}

static void ui_tick(lv_timer_t *timer)
{
    (void)timer;
    if (s_fatal_error) return;
    update_battery_ui();

    gesture_key_state_t state = gesture_key_state();
    uint8_t calibration = gesture_key_calibration_percent();
    uint8_t recording = gesture_key_recording_percent();
    uint8_t sample = gesture_key_enrollment_sample();
    uint8_t score = gesture_key_last_score();
    uint8_t rejected = gesture_key_rejected_sample();
    uint8_t model_mask = gesture_key_model_mask();
    gesture_macro_t menu_selected = gesture_key_menu_selected();
    gesture_menu_action_t menu_action = gesture_key_menu_action();
    gesture_menu_confirm_t menu_confirm = gesture_key_menu_confirm();
    gesture_macro_t active = gesture_key_active_macro();
    gesture_macro_t conflicting = gesture_key_conflicting_macro();
    uint8_t pin_position = gesture_key_pin_position();
    uint8_t pin_digit = gesture_key_pin_digit();
    uint32_t trace_revision = gesture_key_trace_revision();
    int link_rssi = gesture_key_link_rssi();
    uint32_t link_revision = gesture_key_link_revision();
    uint8_t ui_result = (uint8_t)((rejected & 0x0FU) |
                                  ((pin_digit & 0x0FU) << 4));
    uint8_t ui_navigation = (uint8_t)(((uint8_t)menu_selected & 0x03U) |
        (((uint8_t)menu_action & 0x03U) << 2) |
        ((pin_position & 0x07U) << 4) |
        (((uint8_t)menu_confirm & 0x01U) << 7));
    uint64_t values = calibration | ((uint64_t)recording << 8) |
                      ((uint64_t)sample << 16) | ((uint64_t)score << 24) |
                      ((uint64_t)ui_result << 32) | ((uint64_t)model_mask << 40) |
                      ((uint64_t)ui_navigation << 48) | ((uint64_t)active << 56);
    if (state == s_last_state && values == s_last_values &&
        trace_revision == s_last_trace_revision &&
        link_revision == s_last_link_revision) {
        return;
    }
    bool state_changed = state != s_last_state;
    bool trace_changed = trace_revision != s_last_trace_revision;
    bool link_changed = link_revision != s_last_link_revision;
    s_last_state = state;
    s_last_values = values;
    s_last_trace_revision = trace_revision;
    s_last_link_revision = link_revision;

    if (state == GESTURE_KEY_PIN_ENTRY || state == GESTURE_KEY_PIN_ERROR ||
        state == GESTURE_KEY_PIN_ACCEPTED) {
        ui_fui_pin_result_t result = state == GESTURE_KEY_PIN_ACCEPTED ?
            UI_FUI_PIN_ACCEPTED : state == GESTURE_KEY_PIN_ERROR ?
            UI_FUI_PIN_REJECTED : UI_FUI_PIN_INPUT;
        ui_fui_set_pin(&s_ui, pin_position, pin_digit, result);
        return;
    }

    if (state == GESTURE_KEY_MENU_LIST || state == GESTURE_KEY_MENU_DETAIL ||
        state == GESTURE_KEY_MENU_CLEAR_CONFIRM ||
        state == GESTURE_KEY_MENU_CLEAR_DONE ||
        state == GESTURE_KEY_MENU_CLEAR_ERROR) {
        const char *names[UI_FUI_MENU_ITEMS] = {
            gesture_key_macro_name(GESTURE_MACRO_AUTH_SEQUENCE),
            gesture_key_macro_name(GESTURE_MACRO_LOCK_HOST),
        };
        ui_fui_manager_view_t view = UI_FUI_MANAGER_LIST;
        if (state == GESTURE_KEY_MENU_DETAIL) {
            view = UI_FUI_MANAGER_DETAIL;
        } else if (state == GESTURE_KEY_MENU_CLEAR_CONFIRM) {
            view = UI_FUI_MANAGER_CLEAR_CONFIRM;
        } else if (state == GESTURE_KEY_MENU_CLEAR_DONE) {
            view = UI_FUI_MANAGER_CLEAR_DONE;
        } else if (state == GESTURE_KEY_MENU_CLEAR_ERROR) {
            view = UI_FUI_MANAGER_CLEAR_ERROR;
        }
        ui_fui_set_manager(&s_ui, view, (uint8_t)menu_selected,
                           (uint8_t)menu_action,
                           (ui_fui_confirm_choice_t)menu_confirm,
                           model_mask, names);
        return;
    }

    char detail[96];
    format_detail(state, sample, score, rejected, active, conflicting, detail);
    ui_fui_set_status(&s_ui, state_text(state), detail, state_accent(state));

    if (trace_changed || state_changed) {
        int16_t trace[UI_FUI_TRACE_POINTS][UI_FUI_TRACE_AXES] = {0};
        size_t trace_count = gesture_key_trace_snapshot(
            trace, UI_FUI_TRACE_POINTS);
        ui_fui_set_trace(&s_ui, trace, trace_count);
    }
    ui_fui_set_trace_result(&s_ui, trace_result(state));

    bool progress_active = false;
    uint8_t progress = 0U;
    if (state == GESTURE_KEY_CALIBRATING) {
        progress_active = true;
        progress = calibration;
    } else if (state == GESTURE_KEY_LEARN_RECORDING ||
               state == GESTURE_KEY_VERIFY_RECORDING) {
        progress_active = true;
        progress = recording;
    }
    ui_fui_set_progress(&s_ui, progress_active, progress);

    if (state == GESTURE_KEY_PAIRING) {
        ui_fui_set_link(&s_ui, "PAIRING", UI_FUI_MAGENTA);
    } else if (state == GESTURE_KEY_BLE_ERROR) {
        ui_fui_set_link(&s_ui, "OFFLINE", UI_FUI_RED);
    } else if (state == GESTURE_KEY_STARTING) {
        ui_fui_set_link(&s_ui, "STARTING", UI_FUI_AMBER);
    } else {
        ui_fui_set_link(&s_ui, "ONLINE", UI_FUI_GREEN);
    }

    bool link_online = state != GESTURE_KEY_PAIRING &&
                       state != GESTURE_KEY_BLE_ERROR &&
                       state != GESTURE_KEY_STARTING;
    if (!link_online) {
        if (link_changed || state_changed) {
            ui_fui_set_link_quality(&s_ui, false, 0);
        }
    } else if (link_changed || state_changed) {
        ui_fui_set_link_quality(&s_ui,
            link_rssi != GESTURE_KEY_RSSI_UNAVAILABLE, link_rssi);
    }
}

static bool battery_initialize(void)
{
    if (s_battery_initialized) return true;

    esp_err_t error = bsp_battery_init();
    if (error != ESP_OK) {
        if (!s_battery_failure_logged) {
            ESP_LOGW(TAG, "CW2017 unavailable: %s", esp_err_to_name(error));
            s_battery_failure_logged = true;
        }
        return false;
    }
    s_battery_initialized = true;
    return true;
}

static bool battery_sample_once(void)
{
    if (!battery_initialize()) return false;

    int soc = -1;
    int millivolts = -1;
    esp_err_t error = bsp_battery_read(&soc, &millivolts);
    if (error != ESP_OK || soc < 0 || soc > 100) {
        if (!s_battery_failure_logged) {
            ESP_LOGW(TAG, "CW2017 SOC unavailable: %s, soc=%d, mv=%d",
                     esp_err_to_name(error), soc, millivolts);
            s_battery_failure_logged = true;
        }
        // 短暂 I2C/SOC 未就绪时保留上次有效的芯片读数。
        return false;
    }

    if (millivolts < 2500 || millivolts > 5000) millivolts = -1;

    s_battery_failure_logged = false;
    atomic_store(&s_battery_soc, soc);
    atomic_store(&s_battery_mv, millivolts);
    atomic_store(&s_battery_available, true);

    const esp_err_t ble_error = gesture_key_set_battery_level((uint8_t)soc);
    if (ble_error != ESP_OK) {
        if (!s_ble_battery_failure_logged) {
            ESP_LOGW(TAG, "BLE battery publication failed: %s",
                     esp_err_to_name(ble_error));
            s_ble_battery_failure_logged = true;
        }
    } else {
        s_ble_battery_failure_logged = false;
    }
    return true;
}

static void battery_task(void *argument)
{
    (void)argument;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(BATTERY_REFRESH_MS));
        // 电量计与 QMI8658A 共用 I2C；到期时如果正在录制/匹配，
        // 就等手势完成后再执行这次短采样。
        while (gesture_key_motion_active()) {
            vTaskDelay(pdMS_TO_TICKS(BATTERY_DEFER_POLL_MS));
        }
        (void)battery_sample_once();
    }
}

static void start_battery_monitor(void)
{
    atomic_store(&s_battery_available, false);
    atomic_store(&s_battery_soc, -1);
    atomic_store(&s_battery_mv, -1);
    // 开机立即直接读取一次；后续固定 30 秒采样，手势期间自动顺延。
    (void)battery_sample_once();
    // 即使首次初始化失败也启动任务，后续会低优先级重试。
    if (xTaskCreate(battery_task, "battery", 2048, NULL, 2, NULL) != pdPASS) {
        ESP_LOGW(TAG, "battery monitor task not started");
    }
}

static void build_ui(void)
{
    ui_fui_create(&s_ui);
    (void)lv_timer_create(ui_tick, 80, NULL);
    ui_tick(NULL);
}

static void show_fatal_error(const char *title, const char *detail)
{
    if (!bsp_lvgl_lock(500)) return;
    s_fatal_error = true;
    ui_fui_set_status(&s_ui, title, detail, UI_FUI_RED);
    ui_fui_set_progress(&s_ui, false, 0U);
    ui_fui_set_link(&s_ui, "SYSTEM HALTED", UI_FUI_RED);
    bsp_lvgl_unlock();
}

void app_main(void)
{
    if (bsp_i2c_init() != ESP_OK || bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display init failed (MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }

    if (bsp_lvgl_lock(1000)) {
        build_ui();
        bsp_lvgl_unlock();
        // 复杂 FUI 的首帧交给 7KB 的 LVGL 任务绘制；若在默认
        // 3.5KB main 任务中同步 lv_refr_now()，字体栅格化会撑爆其栈。
        vTaskDelay(pdMS_TO_TICKS(120));
        bsp_display_backlight(100);
    }

    start_battery_monitor();
    if (bsp_qmi8658a_init() != ESP_OK) {
        show_fatal_error("SENSOR FAULT", "QMI8658A NOT DETECTED\nCHECK SHARED I2C BUS");
        return;
    }
    if (!gesture_sound_start()) {
        show_fatal_error("AUDIO FAULT", "ES8311 START FAILED\nCHECK CODEC / SPEAKER");
        return;
    }
    if (bsp_button_init(gesture_key_button_event, NULL) != ESP_OK ||
        gesture_key_start() != ESP_OK) {
        gesture_sound_play(GESTURE_SOUND_ERROR);
        show_fatal_error("START FAULT", "CORE SERVICE START FAILED\nRESTART DEVICE");
    }
}
