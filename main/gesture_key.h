#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bsp_button.h"
#include "esp_err.h"

typedef enum {
    GESTURE_KEY_STARTING = 0,
    GESTURE_KEY_PAIRING,
    GESTURE_KEY_CALIBRATING,
    GESTURE_KEY_LEARN_READY,
    GESTURE_KEY_LEARN_RECORDING,
    GESTURE_KEY_LEARN_SAMPLE_OK,
    GESTURE_KEY_LEARN_RETRY,
    GESTURE_KEY_LEARN_RESTART,
    GESTURE_KEY_LEARN_SAVED,
    GESTURE_KEY_READY,
    GESTURE_KEY_RELEARN_AUTH,
    GESTURE_KEY_VERIFY_RECORDING,
    GESTURE_KEY_MATCHED,
    GESTURE_KEY_NO_MATCH,
    GESTURE_KEY_INVALID_MOTION,
    GESTURE_KEY_TOO_LONG,
    GESTURE_KEY_MODEL_RESET,
    GESTURE_KEY_SEND_ERROR,
    GESTURE_KEY_STORAGE_ERROR,
    GESTURE_KEY_BLE_ERROR,
} gesture_key_state_t;

esp_err_t gesture_key_start(void);

// 可直接交给 bsp_button_init()；回调只入队，轨迹处理在独立任务完成。
void gesture_key_button_event(bsp_btn_t btn, bsp_btn_ev_t event, void *user);

gesture_key_state_t gesture_key_state(void);
uint8_t gesture_key_calibration_percent(void);
uint8_t gesture_key_recording_percent(void);
uint8_t gesture_key_enrollment_sample(void);
uint8_t gesture_key_last_score(void);
uint8_t gesture_key_rejected_sample(void);

// 按住 OK 录制或正在处理刚结束的轨迹时为 true。
// 共享 I2C 总线上的低优先级访问应在此期间顺延。
bool gesture_key_motion_active(void);
