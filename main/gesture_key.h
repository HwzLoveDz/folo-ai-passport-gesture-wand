#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bsp_button.h"
#include "esp_err.h"

#define GESTURE_MACRO_AUTH_SEQUENCE_NAME "AUTH_SEQUENCE"
#define GESTURE_MACRO_LOCK_HOST_NAME     "LOCK_HOST"
#define GESTURE_MACRO_NEW_TAB_NAME       "NEW_TAB"
#define GESTURE_KEY_TRACE_POINTS          32U
#define GESTURE_KEY_TRACE_AXES             3U
#define GESTURE_KEY_PIN_DIGITS             4U
#define GESTURE_KEY_RSSI_UNAVAILABLE INT16_MIN

typedef enum {
    GESTURE_MACRO_AUTH_SEQUENCE = 0,
    GESTURE_MACRO_LOCK_HOST,
    GESTURE_MACRO_NEW_TAB,
    GESTURE_MACRO_COUNT,
    GESTURE_MACRO_NONE = 0xFF,
} gesture_macro_t;

typedef enum {
    GESTURE_MENU_RE_RECORD = 0,
    GESTURE_MENU_CLEAR,
    GESTURE_MENU_ACTION_COUNT,
} gesture_menu_action_t;

typedef enum {
    GESTURE_MENU_CONFIRM_CANCEL = 0,
    GESTURE_MENU_CONFIRM_CLEAR,
    GESTURE_MENU_CONFIRM_COUNT,
} gesture_menu_confirm_t;

typedef enum {
    GESTURE_KEY_STARTING = 0,
    GESTURE_KEY_PIN_ENTRY,
    GESTURE_KEY_PIN_ERROR,
    GESTURE_KEY_PIN_ACCEPTED,
    GESTURE_KEY_PAIRING,
    GESTURE_KEY_CALIBRATING,
    GESTURE_KEY_MENU_LIST,
    GESTURE_KEY_MENU_DETAIL,
    GESTURE_KEY_MENU_CLEAR_CONFIRM,
    GESTURE_KEY_MENU_CLEAR_DONE,
    GESTURE_KEY_MENU_CLEAR_ERROR,
    GESTURE_KEY_LEARN_READY,
    GESTURE_KEY_LEARN_RECORDING,
    GESTURE_KEY_LEARN_SAMPLE_OK,
    GESTURE_KEY_LEARN_RETRY,
    GESTURE_KEY_LEARN_RESTART,
    GESTURE_KEY_LEARN_CONFLICT,
    GESTURE_KEY_LEARN_SAVED,
    GESTURE_KEY_READY,
    GESTURE_KEY_VERIFY_RECORDING,
    GESTURE_KEY_MATCHED,
    GESTURE_KEY_AMBIGUOUS,
    GESTURE_KEY_NO_MATCH,
    GESTURE_KEY_INVALID_MOTION,
    GESTURE_KEY_TOO_LONG,
    GESTURE_KEY_SEND_ERROR,
    GESTURE_KEY_STORAGE_ERROR,
    GESTURE_KEY_BLE_ERROR,
} gesture_key_state_t;

esp_err_t gesture_key_start(void);

// Queue the real fuel-gauge SOC for the BLE Battery Service. The value is
// retained before BLE starts, then published before advertising and whenever
// the connected host can consume an update.
esp_err_t gesture_key_set_battery_level(uint8_t percent);

// 可直接交给 bsp_button_init()；回调只入队，轨迹处理在独立任务完成。
void gesture_key_button_event(bsp_btn_t btn, bsp_btn_ev_t event, void *user);

gesture_key_state_t gesture_key_state(void);
uint8_t gesture_key_calibration_percent(void);
uint8_t gesture_key_recording_percent(void);
uint8_t gesture_key_enrollment_sample(void);
uint8_t gesture_key_last_score(void);
uint8_t gesture_key_rejected_sample(void);
uint8_t gesture_key_model_mask(void);
gesture_macro_t gesture_key_menu_selected(void);
gesture_menu_action_t gesture_key_menu_action(void);
gesture_menu_confirm_t gesture_key_menu_confirm(void);
gesture_macro_t gesture_key_active_macro(void);
gesture_macro_t gesture_key_conflicting_macro(void);
uint8_t gesture_key_pin_position(void);
uint8_t gesture_key_pin_digit(void);
const char *gesture_key_macro_name(gesture_macro_t macro);

// Copy a display-ready, evenly downsampled gyro trace in the range -100..100.
// The snapshot remains valid after recording so the UI can show its verdict
// over the exact trace that was classified.
size_t gesture_key_trace_snapshot(
    int16_t values[][GESTURE_KEY_TRACE_AXES], size_t capacity);
uint32_t gesture_key_trace_revision(void);

// The latest controller-measured BLE RSSI. Revision advances for each real
// sample so the UI can preserve link history even when the value is unchanged.
int gesture_key_link_rssi(void);
uint32_t gesture_key_link_revision(void);

// 按住 OK 录制或正在处理刚结束的轨迹时为 true。
// 共享 I2C 总线上的低优先级访问应在此期间顺延。
bool gesture_key_motion_active(void);
