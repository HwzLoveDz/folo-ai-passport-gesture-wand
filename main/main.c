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

static ui_fui_t s_ui;
static gesture_key_state_t s_last_state = (gesture_key_state_t)-1;
static uint64_t s_last_values = UINT64_MAX;
static int s_last_battery_soc = -2;
static int s_last_battery_mv = -2;
static bool s_last_battery_available;
static bool s_fatal_error;

static atomic_bool s_battery_available;
static atomic_int s_battery_soc;
static atomic_int s_battery_mv;
static bool s_battery_initialized;
static bool s_battery_failure_logged;

static const char *state_text(gesture_key_state_t state)
{
    switch (state) {
    case GESTURE_KEY_PAIRING: return "LINK SEARCH";
    case GESTURE_KEY_CALIBRATING: return "IMU ZEROING";
    case GESTURE_KEY_LEARN_READY: return "ENROLL TRACE";
    case GESTURE_KEY_LEARN_RECORDING: return "TRACE CAPTURE";
    case GESTURE_KEY_LEARN_SAMPLE_OK: return "SAMPLE VERIFIED";
    case GESTURE_KEY_LEARN_RETRY: return "ANOMALY FOUND";
    case GESTURE_KEY_LEARN_RESTART: return "SIGNAL DIVERGED";
    case GESTURE_KEY_LEARN_SAVED: return "SIGNATURE LOCKED";
    case GESTURE_KEY_READY: return "AUTH STANDBY";
    case GESTURE_KEY_RELEARN_AUTH: return "RESET VERIFY";
    case GESTURE_KEY_VERIFY_RECORDING: return "TRACE MATCH";
    case GESTURE_KEY_MATCHED: return "ACCESS GRANTED";
    case GESTURE_KEY_NO_MATCH: return "ACCESS DENIED";
    case GESTURE_KEY_INVALID_MOTION: return "TRACE INVALID";
    case GESTURE_KEY_TOO_LONG: return "BUFFER LIMIT";
    case GESTURE_KEY_MODEL_RESET: return "MEMORY CLEARED";
    case GESTURE_KEY_SEND_ERROR: return "LINK FAILURE";
    case GESTURE_KEY_STORAGE_ERROR: return "STORAGE FAULT";
    case GESTURE_KEY_BLE_ERROR: return "RADIO FAULT";
    default: return "BOOT SEQUENCE";
    }
}

static uint32_t state_accent(gesture_key_state_t state)
{
    switch (state) {
    case GESTURE_KEY_MATCHED:
    case GESTURE_KEY_LEARN_SAVED:
    case GESTURE_KEY_LEARN_SAMPLE_OK:
    case GESTURE_KEY_MODEL_RESET:
        return UI_FUI_GREEN;
    case GESTURE_KEY_CALIBRATING:
    case GESTURE_KEY_LEARN_RECORDING:
    case GESTURE_KEY_VERIFY_RECORDING:
        return UI_FUI_AMBER;
    case GESTURE_KEY_PAIRING:
    case GESTURE_KEY_LEARN_READY:
    case GESTURE_KEY_RELEARN_AUTH:
        return UI_FUI_MAGENTA;
    case GESTURE_KEY_LEARN_RETRY:
    case GESTURE_KEY_LEARN_RESTART:
    case GESTURE_KEY_NO_MATCH:
    case GESTURE_KEY_INVALID_MOTION:
    case GESTURE_KEY_TOO_LONG:
    case GESTURE_KEY_SEND_ERROR:
    case GESTURE_KEY_STORAGE_ERROR:
    case GESTURE_KEY_BLE_ERROR:
        return UI_FUI_RED;
    default:
        return UI_FUI_CYAN;
    }
}

static void format_detail(gesture_key_state_t state, uint8_t sample,
                          uint8_t score, uint8_t rejected,
                          char detail[96])
{
    switch (state) {
    case GESTURE_KEY_PAIRING:
        snprintf(detail, 96, "PAIR MOTE WAND\nFROM HOST BLUETOOTH");
        break;
    case GESTURE_KEY_CALIBRATING:
        snprintf(detail, 96, "KEEP DEVICE STILL\nCALIBRATING ZERO BIAS");
        break;
    case GESTURE_KEY_LEARN_READY:
        snprintf(detail, 96, "SAMPLE %u / 3\nHOLD OK / MOVE / RELEASE", sample);
        break;
    case GESTURE_KEY_LEARN_RECORDING:
        snprintf(detail, 96, "CAPTURING SAMPLE %u / 3\nRELEASE OK TO FINISH", sample);
        break;
    case GESTURE_KEY_LEARN_SAMPLE_OK:
        snprintf(detail, 96, "SAMPLE %u / 3 VALIDATED\nPREPARE NEXT TRACE",
                 sample > 0U ? sample - 1U : 0U);
        break;
    case GESTURE_KEY_LEARN_RETRY:
        snprintf(detail, 96, "SAMPLE %u DEVIATED\nRECORD THIS SAMPLE AGAIN", rejected);
        break;
    case GESTURE_KEY_LEARN_RESTART:
        snprintf(detail, 96, "NO CONSISTENT PAIR\nRECORD ALL 3 AGAIN");
        break;
    case GESTURE_KEY_LEARN_SAVED:
        snprintf(detail, 96, "3 TRACES CONSISTENT\nMODEL COMMITTED TO NVS");
        break;
    case GESTURE_KEY_READY:
        snprintf(detail, 96, "HOLD OK / REPEAT TRACE\nRELEASE TO AUTHENTICATE");
        break;
    case GESTURE_KEY_RELEARN_AUTH:
        snprintf(detail, 96, "REPEAT OLD TRACE\nTO AUTHORIZE MODEL RESET");
        break;
    case GESTURE_KEY_VERIFY_RECORDING:
        snprintf(detail, 96, "COMPARING LIVE TRAJECTORY\nTHRESHOLD 75 PERCENT");
        break;
    case GESTURE_KEY_MATCHED:
        snprintf(detail, 96, "MATCH %u PERCENT\nACTION SEQUENCE SENT", score);
        break;
    case GESTURE_KEY_NO_MATCH:
        snprintf(detail, 96, "MATCH %u PERCENT / NEED 75\nTRACE REJECTED", score);
        break;
    case GESTURE_KEY_INVALID_MOTION:
        snprintf(detail, 96, "MOTION ENERGY TOO LOW\nMOVE FOR AT LEAST 0.3 SEC");
        break;
    case GESTURE_KEY_TOO_LONG:
        snprintf(detail, 96, "TRACE BUFFER FULL\nRELEASE WITHIN 2.6 SEC");
        break;
    case GESTURE_KEY_MODEL_RESET:
        snprintf(detail, 96, "OLD SIGNATURE ERASED\nREADY FOR NEW ENROLLMENT");
        break;
    case GESTURE_KEY_SEND_ERROR:
        snprintf(detail, 96, "LINK OR CONFIG ERROR\nACTION NOT SENT");
        break;
    case GESTURE_KEY_STORAGE_ERROR:
        snprintf(detail, 96, "NVS WRITE FAILED\nMODEL VALID UNTIL RESTART");
        break;
    case GESTURE_KEY_BLE_ERROR:
        snprintf(detail, 96, "BLE HID START FAILED\nRESTART DEVICE");
        break;
    default:
        snprintf(detail, 96, "INITIALIZING MOTION CORE\nWAIT FOR SYSTEM READY");
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
    uint64_t values = calibration | ((uint64_t)recording << 8) |
                      ((uint64_t)sample << 16) | ((uint64_t)score << 24) |
                      ((uint64_t)rejected << 32);
    if (state == s_last_state && values == s_last_values) return;
    bool state_changed = state != s_last_state;
    s_last_state = state;
    s_last_values = values;

    char detail[96];
    format_detail(state, sample, score, rejected, detail);
    ui_fui_set_status(&s_ui, state_text(state), detail, state_accent(state));

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
        ui_fui_set_link(&s_ui, "PAIRING HOST", UI_FUI_MAGENTA);
    } else if (state == GESTURE_KEY_BLE_ERROR) {
        ui_fui_set_link(&s_ui, "RADIO OFFLINE", UI_FUI_RED);
    } else if (state == GESTURE_KEY_STARTING) {
        ui_fui_set_link(&s_ui, "INITIALIZING", UI_FUI_AMBER);
    } else {
        ui_fui_set_link(&s_ui, "HID ENCRYPTED", UI_FUI_GREEN);
    }

    if (state_changed && (state == GESTURE_KEY_MATCHED ||
                          state == GESTURE_KEY_LEARN_SAVED)) {
        ui_fui_flash_success(&s_ui);
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
