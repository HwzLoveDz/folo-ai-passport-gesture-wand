#include "gesture_key.h"

#include <math.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bsp_qmi8658a.h"
#include "esp_bt.h"
#include "esp_hid_common.h"
#include "esp_hidd.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gesture_model.h"
#include "gesture_sound.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "sdkconfig.h"

static const char *TAG = "gesture_key";

#define GESTURE_KEY_NAME              "Mote Wand"
#define HID_SERVICE_UUID              0x1812
#define SAMPLE_PERIOD_MS              10U
#define CALIBRATION_SAMPLES           64U
#define RECORD_ARM_MS                 90U
#define VERIFY_SCORE_MIN              75.0f
#define ENROLL_PAIR_SCORE_MIN         72.0f
#define MODEL_MAGIC                   0x47535452U
#define MODEL_VERSION                 1U
#define MODEL_NAMESPACE               "gesture_key"
#define MODEL_KEY                     "model"

#define HID_KEY_ENTER                 0x28U
#define ACTION_SEQUENCE_MAX_LENGTH    32U

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} button_event_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t signature_size;
    gesture_signature_t signature;
    uint32_t crc32;
} stored_model_t;

static const uint8_t s_keyboard_report_map[] = {
    0x05, 0x01,                    // Usage Page (Generic Desktop)
    0x09, 0x06,                    // Usage (Keyboard)
    0xA1, 0x01,                    // Collection (Application)
    0x05, 0x07,                    //   Usage Page (Keyboard/Keypad)
    0x19, 0xE0,                    //   Usage Minimum (Left Control)
    0x29, 0xE7,                    //   Usage Maximum (Right GUI)
    0x15, 0x00,                    //   Logical Minimum (0)
    0x25, 0x01,                    //   Logical Maximum (1)
    0x75, 0x01,                    //   Report Size (1)
    0x95, 0x08,                    //   Report Count (8)
    0x81, 0x02,                    //   Input (Data, Variable, Absolute)
    0x95, 0x01,                    //   Report Count (1)
    0x75, 0x08,                    //   Report Size (8)
    0x81, 0x01,                    //   Input (Constant)
    0x95, 0x06,                    //   Report Count (6)
    0x75, 0x08,                    //   Report Size (8)
    0x15, 0x00,                    //   Logical Minimum (0)
    0x25, 0x65,                    //   Logical Maximum (101)
    0x05, 0x07,                    //   Usage Page (Keyboard/Keypad)
    0x19, 0x00,                    //   Usage Minimum (None)
    0x29, 0x65,                    //   Usage Maximum (Application)
    0x81, 0x00,                    //   Input (Data, Array, Absolute)
    0xC0,                          // End Collection
};

static esp_hid_raw_report_map_t s_report_maps[] = {
    {
        .data = s_keyboard_report_map,
        .len = sizeof(s_keyboard_report_map),
    },
};

static const esp_hid_device_config_t s_hid_config = {
    .vendor_id = 0x303A,
    .product_id = 0x8659,
    .version = 0x0100,
    .device_name = GESTURE_KEY_NAME,
    .manufacturer_name = "FoloToy",
    .serial_number = "MOTE-WAND-01",
    .report_maps = s_report_maps,
    .report_maps_len = 1,
};

// The local sdkconfig value is ignored by Git. Never log this sequence.
static const char s_action_sequence[] = CONFIG_MOTE_WAND_ACTION_SEQUENCE;
_Static_assert(sizeof(s_action_sequence) <= ACTION_SEQUENCE_MAX_LENGTH + 1U,
               "Mote Wand action sequence is too long");

static struct ble_hs_adv_fields s_adv_fields;
static ble_uuid16_t s_hid_uuid = BLE_UUID16_INIT(HID_SERVICE_UUID);
static esp_hidd_dev_t *s_hid_dev;
static QueueHandle_t s_button_queue;

static atomic_bool s_connected;
static atomic_bool s_encrypted;
static atomic_bool s_ble_failed;
static atomic_int s_state;
static atomic_uchar s_calibration_percent;
static atomic_uchar s_recording_percent;
static atomic_uchar s_enrollment_sample;
static atomic_uchar s_last_score;
static atomic_uchar s_rejected_sample;

static gesture_sample_t s_raw_samples[GESTURE_MAX_SAMPLES];
static size_t s_raw_count;
static bool s_recording;
static bool s_record_overflow;
static bool s_record_for_enrollment;
static bool s_record_for_relearn;
static int64_t s_record_arm_us;

static gesture_signature_t s_enrollment[3];
static size_t s_enrollment_count;
static gesture_signature_t s_model;
static bool s_model_valid;

static float s_gyro_sum[3];
static float s_gyro_bias[3];
static unsigned s_calibration_count;
static bool s_calibrated;
static bool s_was_hid_ready;

static int64_t s_transient_deadline_us;
static gesture_key_state_t s_after_transient;

void ble_store_config_init(void);

static void set_state(gesture_key_state_t state)
{
    atomic_store(&s_state, state);
}

static void set_transient_state(gesture_key_state_t state, uint32_t duration_ms,
                                gesture_key_state_t after)
{
    set_state(state);
    s_after_transient = after;
    s_transient_deadline_us = esp_timer_get_time() + (int64_t)duration_ms * 1000LL;
}

static uint32_t model_crc32(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < size; i++) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8U; bit++) {
            uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

static bool signature_valid(const gesture_signature_t *signature)
{
    if (!signature || !isfinite(signature->duration_ms) ||
        signature->duration_ms < 200.0f || signature->duration_ms > 4000.0f) {
        return false;
    }
    for (size_t point = 0; point < GESTURE_POINT_COUNT; point++) {
        for (size_t axis = 0; axis < GESTURE_AXIS_COUNT; axis++) {
            if (!isfinite(signature->point[point][axis]) ||
                fabsf(signature->point[point][axis]) > 20.0f) {
                return false;
            }
        }
    }
    return true;
}

static bool load_model(void)
{
    nvs_handle_t handle;
    if (nvs_open(MODEL_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
    stored_model_t stored;
    size_t size = sizeof(stored);
    esp_err_t err = nvs_get_blob(handle, MODEL_KEY, &stored, &size);
    nvs_close(handle);
    if (err != ESP_OK || size != sizeof(stored) ||
        stored.magic != MODEL_MAGIC || stored.version != MODEL_VERSION ||
        stored.signature_size != sizeof(gesture_signature_t)) {
        return false;
    }
    uint32_t expected = model_crc32(&stored, offsetof(stored_model_t, crc32));
    if (expected != stored.crc32 || !signature_valid(&stored.signature)) return false;
    s_model = stored.signature;
    return true;
}

static esp_err_t save_model(void)
{
    stored_model_t stored = {
        .magic = MODEL_MAGIC,
        .version = MODEL_VERSION,
        .signature_size = sizeof(gesture_signature_t),
        .signature = s_model,
    };
    stored.crc32 = model_crc32(&stored, offsetof(stored_model_t, crc32));

    nvs_handle_t handle;
    esp_err_t err = nvs_open(MODEL_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, MODEL_KEY, &stored, sizeof(stored));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static void erase_model(void)
{
    nvs_handle_t handle;
    if (nvs_open(MODEL_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        (void)nvs_erase_key(handle, MODEL_KEY);
        (void)nvs_commit(handle);
        nvs_close(handle);
    }
}

static bool hid_ready(void)
{
    return s_hid_dev && atomic_load(&s_connected) && atomic_load(&s_encrypted) &&
           esp_hidd_dev_connected(s_hid_dev);
}

static esp_err_t send_key(uint8_t usage)
{
    if (!hid_ready()) return ESP_ERR_INVALID_STATE;
    uint8_t report[8] = {0};
    report[2] = usage;
    esp_err_t err = esp_hidd_dev_input_set(s_hid_dev, 0, 0, report, sizeof(report));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(32));
    memset(report, 0, sizeof(report));
    err = esp_hidd_dev_input_set(s_hid_dev, 0, 0, report, sizeof(report));
    vTaskDelay(pdMS_TO_TICKS(24));
    return err;
}

static bool hid_usage_for_digit(char digit, uint8_t *usage)
{
    if (!usage || digit < '0' || digit > '9') return false;
    *usage = digit == '0' ? 0x27U : (uint8_t)(0x1EU + digit - '1');
    return true;
}

static esp_err_t send_action_sequence(void)
{
    if (s_action_sequence[0] == '\0') return ESP_ERR_INVALID_STATE;
    for (size_t i = 0; s_action_sequence[i] != '\0'; i++) {
        uint8_t usage;
        if (!hid_usage_for_digit(s_action_sequence[i], &usage)) {
            return ESP_ERR_INVALID_ARG;
        }
        esp_err_t err = send_key(usage);
        if (err != ESP_OK) return err;
    }
    return send_key(HID_KEY_ENTER);
}

static void map_sensor_axes(const bsp_qmi8658a_sample_t *sample,
                            float accel[3], float gyro[3])
{
    // 沿用 Motion 实机确认坐标：交换板级 X/Y，并翻转 Z 保持右手系。
    accel[0] = sample->accel_g[1];
    accel[1] = sample->accel_g[0];
    accel[2] = -sample->accel_g[2];
    gyro[0] = sample->gyro_dps[1];
    gyro[1] = sample->gyro_dps[0];
    gyro[2] = -sample->gyro_dps[2];
}

static void reset_calibration(void)
{
    memset(s_gyro_sum, 0, sizeof(s_gyro_sum));
    memset(s_gyro_bias, 0, sizeof(s_gyro_bias));
    s_calibration_count = 0U;
    s_calibrated = false;
    atomic_store(&s_calibration_percent, 0U);
}

static bool add_calibration_sample(const bsp_qmi8658a_sample_t *sample)
{
    float accel[3];
    float gyro[3];
    map_sensor_axes(sample, accel, gyro);
    float accel2 = accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2];
    float gyro2 = gyro[0] * gyro[0] + gyro[1] * gyro[1] + gyro[2] * gyro[2];
    if (accel2 < 0.64f || accel2 > 1.44f || gyro2 > 25.0f) {
        memset(s_gyro_sum, 0, sizeof(s_gyro_sum));
        s_calibration_count = 0U;
        atomic_store(&s_calibration_percent, 0U);
        return false;
    }

    for (size_t axis = 0; axis < 3U; axis++) s_gyro_sum[axis] += gyro[axis];
    s_calibration_count++;
    atomic_store(&s_calibration_percent,
                 (uint8_t)(s_calibration_count * 100U / CALIBRATION_SAMPLES));
    if (s_calibration_count < CALIBRATION_SAMPLES) return false;
    for (size_t axis = 0; axis < 3U; axis++) {
        s_gyro_bias[axis] = s_gyro_sum[axis] / (float)CALIBRATION_SAMPLES;
    }
    s_calibrated = true;
    atomic_store(&s_calibration_percent, 100U);
    return true;
}

static void track_idle_bias(const bsp_qmi8658a_sample_t *sample)
{
    float accel[3];
    float gyro[3];
    map_sensor_axes(sample, accel, gyro);
    float accel2 = accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2];
    bool still = accel2 > 0.85f && accel2 < 1.17f;
    for (size_t axis = 0; axis < 3U; axis++) {
        if (fabsf(gyro[axis] - s_gyro_bias[axis]) >= 0.8f) still = false;
    }
    if (!still) return;
    for (size_t axis = 0; axis < 3U; axis++) {
        s_gyro_bias[axis] += 0.0015f * (gyro[axis] - s_gyro_bias[axis]);
    }
}

static void begin_recording(void)
{
    gesture_key_state_t state = gesture_key_state();
    bool enrollment = !s_model_valid && state == GESTURE_KEY_LEARN_READY;
    bool relearn = s_model_valid && state == GESTURE_KEY_RELEARN_AUTH;
    bool verification = s_model_valid && (state == GESTURE_KEY_READY || relearn);
    if (!hid_ready() || !s_calibrated || (!enrollment && !verification)) return;

    s_raw_count = 0U;
    s_record_overflow = false;
    s_record_for_enrollment = enrollment;
    s_record_for_relearn = relearn;
    s_recording = true;
    s_record_arm_us = esp_timer_get_time() + (int64_t)RECORD_ARM_MS * 1000LL;
    atomic_store(&s_recording_percent, 0U);
    set_state(enrollment ? GESTURE_KEY_LEARN_RECORDING : GESTURE_KEY_VERIFY_RECORDING);
    gesture_sound_play(GESTURE_SOUND_RECORD_START);
}

static void set_invalid_result(gesture_build_result_t result)
{
    gesture_key_state_t next = !s_model_valid ? GESTURE_KEY_LEARN_READY :
        (s_record_for_relearn ? GESTURE_KEY_RELEARN_AUTH : GESTURE_KEY_READY);
    if (result == GESTURE_BUILD_TOO_LONG) {
        set_transient_state(GESTURE_KEY_TOO_LONG, 1400U, next);
    } else {
        set_transient_state(GESTURE_KEY_INVALID_MOTION, 1400U, next);
    }
    gesture_sound_play(GESTURE_SOUND_RETRY);
}

static void keep_consistent_enrollment_pair(int outlier)
{
    gesture_signature_t kept[2];
    size_t count = 0U;
    for (int i = 0; i < 3; i++) {
        if (i != outlier) kept[count++] = s_enrollment[i];
    }
    s_enrollment[0] = kept[0];
    s_enrollment[1] = kept[1];
    s_enrollment_count = 2U;
    atomic_store(&s_enrollment_sample, 3U);
    atomic_store(&s_rejected_sample, (uint8_t)(outlier + 1));
}

static void finish_enrollment(const gesture_signature_t *candidate)
{
    s_enrollment[s_enrollment_count++] = *candidate;
    if (s_enrollment_count < 3U) {
        atomic_store(&s_enrollment_sample, (uint8_t)(s_enrollment_count + 1U));
        set_transient_state(GESTURE_KEY_LEARN_SAMPLE_OK, 750U,
                            GESTURE_KEY_LEARN_READY);
        gesture_sound_play(GESTURE_SOUND_SAMPLE_OK);
        return;
    }

    float pair_scores[3];
    int outlier = gesture_enrollment_outlier(s_enrollment, ENROLL_PAIR_SCORE_MIN,
                                             pair_scores);
    if (outlier >= 0) {
        keep_consistent_enrollment_pair(outlier);
        set_transient_state(GESTURE_KEY_LEARN_RETRY, 1700U,
                            GESTURE_KEY_LEARN_READY);
        gesture_sound_play(GESTURE_SOUND_RETRY);
        return;
    }
    if (outlier == -2) {
        s_enrollment_count = 0U;
        atomic_store(&s_enrollment_sample, 1U);
        atomic_store(&s_rejected_sample, 0U);
        set_transient_state(GESTURE_KEY_LEARN_RESTART, 1900U,
                            GESTURE_KEY_LEARN_READY);
        gesture_sound_play(GESTURE_SOUND_RETRY);
        return;
    }

    gesture_signature_average(s_enrollment, 3U, &s_model);
    s_model_valid = true;
    atomic_store(&s_enrollment_sample, 3U);
    if (save_model() != ESP_OK) {
        set_transient_state(GESTURE_KEY_STORAGE_ERROR, 1800U, GESTURE_KEY_READY);
        gesture_sound_play(GESTURE_SOUND_ERROR);
        return;
    }
    set_transient_state(GESTURE_KEY_LEARN_SAVED, 1800U, GESTURE_KEY_READY);
    gesture_sound_play(GESTURE_SOUND_SAVED);
}

static void finish_verification(const gesture_signature_t *candidate)
{
    float score = gesture_similarity(candidate, &s_model);
    uint8_t rounded = (uint8_t)lroundf(score);
    if (rounded > 100U) rounded = 100U;
    atomic_store(&s_last_score, rounded);
    if (score < VERIFY_SCORE_MIN) {
        set_transient_state(GESTURE_KEY_NO_MATCH, 1300U,
                            s_record_for_relearn ? GESTURE_KEY_RELEARN_AUTH :
                                                   GESTURE_KEY_READY);
        gesture_sound_play(GESTURE_SOUND_REJECT);
        return;
    }

    if (s_record_for_relearn) {
        erase_model();
        memset(&s_model, 0, sizeof(s_model));
        memset(s_enrollment, 0, sizeof(s_enrollment));
        s_model_valid = false;
        s_enrollment_count = 0U;
        atomic_store(&s_enrollment_sample, 1U);
        atomic_store(&s_rejected_sample, 0U);
        set_transient_state(GESTURE_KEY_MODEL_RESET, 1300U,
                            GESTURE_KEY_LEARN_READY);
        gesture_sound_play(GESTURE_SOUND_RESET);
        return;
    }

    set_transient_state(GESTURE_KEY_MATCHED, 1700U, GESTURE_KEY_READY);
    gesture_sound_play(GESTURE_SOUND_MATCH);
    vTaskDelay(pdMS_TO_TICKS(90));
    if (send_action_sequence() != ESP_OK) {
        set_transient_state(GESTURE_KEY_SEND_ERROR, 1600U, GESTURE_KEY_READY);
        gesture_sound_play(GESTURE_SOUND_ERROR);
    }
}

static void end_recording(void)
{
    if (!s_recording) return;
    s_recording = false;
    gesture_sound_play(GESTURE_SOUND_RECORD_STOP);

    if (s_record_overflow) {
        set_invalid_result(GESTURE_BUILD_TOO_LONG);
        return;
    }

    gesture_signature_t candidate;
    gesture_build_result_t result = gesture_signature_build(
        s_raw_samples, s_raw_count, (float)SAMPLE_PERIOD_MS, &candidate);
    if (result != GESTURE_BUILD_OK) {
        set_invalid_result(result);
        return;
    }
    if (s_record_for_enrollment) {
        finish_enrollment(&candidate);
    } else {
        finish_verification(&candidate);
    }
}

static void request_relearn(void)
{
    if (s_recording || !s_calibrated || !hid_ready()) return;
    if (!s_model_valid) {
        memset(s_enrollment, 0, sizeof(s_enrollment));
        s_enrollment_count = 0U;
        atomic_store(&s_enrollment_sample, 1U);
        atomic_store(&s_rejected_sample, 0U);
        set_transient_state(GESTURE_KEY_MODEL_RESET, 900U,
                            GESTURE_KEY_LEARN_READY);
        gesture_sound_play(GESTURE_SOUND_RESET);
        return;
    }

    if (gesture_key_state() == GESTURE_KEY_RELEARN_AUTH) {
        set_state(GESTURE_KEY_READY);
        gesture_sound_play(GESTURE_SOUND_REJECT);
    } else if (gesture_key_state() == GESTURE_KEY_READY) {
        set_state(GESTURE_KEY_RELEARN_AUTH);
        gesture_sound_play(GESTURE_SOUND_CONNECTED);
    }
}

static void process_button_events(void)
{
    button_event_t button;
    while (xQueueReceive(s_button_queue, &button, 0) == pdTRUE) {
        if (button.btn == BSP_BTN_OK && button.event == BSP_BTN_PRESS) {
            begin_recording();
        } else if (button.btn == BSP_BTN_OK && button.event == BSP_BTN_RELEASE) {
            end_recording();
        } else if (button.btn == BSP_BTN_UP && button.event == BSP_BTN_LONG) {
            request_relearn();
        }
    }
}

static void capture_sample(const bsp_qmi8658a_sample_t *sample)
{
    if (esp_timer_get_time() < s_record_arm_us) return;
    if (s_raw_count >= GESTURE_MAX_SAMPLES) {
        s_record_overflow = true;
        atomic_store(&s_recording_percent, 100U);
        return;
    }

    float accel[3];
    float gyro[3];
    map_sensor_axes(sample, accel, gyro);
    for (size_t axis = 0; axis < 3U; axis++) {
        s_raw_samples[s_raw_count].gyro_dps[axis] = gyro[axis] - s_gyro_bias[axis];
        s_raw_samples[s_raw_count].accel_g[axis] = accel[axis];
    }
    s_raw_count++;
    atomic_store(&s_recording_percent,
                 (uint8_t)(s_raw_count * 100U / GESTURE_MAX_SAMPLES));
}

static void update_connection_state(void)
{
    bool ready = hid_ready();
    if (!ready) {
        if (s_was_hid_ready) gesture_sound_play(GESTURE_SOUND_DISCONNECTED);
        s_was_hid_ready = false;
        s_recording = false;
        s_transient_deadline_us = 0;
        set_state(atomic_load(&s_ble_failed) ? GESTURE_KEY_BLE_ERROR :
                                             GESTURE_KEY_PAIRING);
        return;
    }
    if (!s_was_hid_ready) {
        s_was_hid_ready = true;
        gesture_sound_play(GESTURE_SOUND_CONNECTED);
        if (!s_calibrated) set_state(GESTURE_KEY_CALIBRATING);
        else set_state(s_model_valid ? GESTURE_KEY_READY : GESTURE_KEY_LEARN_READY);
    }
}

static void gesture_task(void *arg)
{
    (void)arg;
    reset_calibration();
    atomic_store(&s_enrollment_sample, 1U);
    TickType_t wake = xTaskGetTickCount();

    while (true) {
        update_connection_state();
        process_button_events();

        bsp_qmi8658a_sample_t sample;
        esp_err_t err = bsp_qmi8658a_read(&sample);
        if (err == ESP_OK && hid_ready()) {
            if (!s_calibrated) {
                if (add_calibration_sample(&sample)) {
                    gesture_sound_play(GESTURE_SOUND_CALIBRATED);
                    set_state(s_model_valid ? GESTURE_KEY_READY : GESTURE_KEY_LEARN_READY);
                }
            } else if (s_recording) {
                capture_sample(&sample);
            } else {
                track_idle_bias(&sample);
            }
        } else if (err != ESP_OK && err != ESP_ERR_NOT_FINISHED) {
            ESP_LOGE(TAG, "QMI8658A read failed: %s", esp_err_to_name(err));
        }

        if (s_transient_deadline_us > 0 &&
            esp_timer_get_time() >= s_transient_deadline_us && hid_ready()) {
            s_transient_deadline_us = 0;
            set_state(s_after_transient);
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}

static esp_err_t start_advertising(void);

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            atomic_store(&s_connected, false);
            atomic_store(&s_encrypted, false);
            (void)start_advertising();
        } else {
            atomic_store(&s_connected, true);
            atomic_store(&s_encrypted, false);
            rc = ble_gap_security_initiate(event->connect.conn_handle);
            if (rc != 0 && rc != BLE_HS_EALREADY) {
                ESP_LOGW(TAG, "BLE security start failed: %d", rc);
            }
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        atomic_store(&s_connected, false);
        atomic_store(&s_encrypted, false);
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        atomic_store(&s_encrypted,
                     event->enc_change.status == 0 && rc == 0 && desc.sec_state.encrypted);
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        if (rc == 0) ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        (void)start_advertising();
        return 0;

    default:
        return 0;
    }
}

static esp_err_t start_advertising(void)
{
    if (ble_gap_adv_active()) return ESP_OK;
    int rc = ble_gap_adv_set_fields(&s_adv_fields);
    if (rc != 0) return ESP_FAIL;

    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = BLE_GAP_ADV_ITVL_MS(30);
    params.itvl_max = BLE_GAP_ADV_ITVL_MS(50);
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &params, gap_event, NULL);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

static void hidd_event(void *handler_args, esp_event_base_t base, int32_t id,
                       void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_hidd_event_data_t *data = event_data;
    switch ((esp_hidd_event_t)id) {
    case ESP_HIDD_START_EVENT:
        if (start_advertising() != ESP_OK) atomic_store(&s_ble_failed, true);
        break;
    case ESP_HIDD_CONNECT_EVENT:
        atomic_store(&s_connected, data && data->connect.status == ESP_OK);
        break;
    case ESP_HIDD_DISCONNECT_EVENT:
        atomic_store(&s_connected, false);
        atomic_store(&s_encrypted, false);
        (void)start_advertising();
        break;
    default:
        break;
    }
}

static void nimble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static esp_err_t ble_start(void)
{
    esp_err_t err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (err != ESP_OK) return err;
    esp_bt_controller_config_t controller_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((err = esp_bt_controller_init(&controller_cfg)) != ESP_OK) return err;
    if ((err = esp_bt_controller_enable(ESP_BT_MODE_BLE)) != ESP_OK) return err;
    if ((err = esp_nimble_init()) != ESP_OK) return err;

    memset(&s_adv_fields, 0, sizeof(s_adv_fields));
    s_adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    s_adv_fields.appearance = ESP_HID_APPEARANCE_KEYBOARD;
    s_adv_fields.appearance_is_present = 1;
    s_adv_fields.name = (uint8_t *)GESTURE_KEY_NAME;
    s_adv_fields.name_len = strlen(GESTURE_KEY_NAME);
    s_adv_fields.name_is_complete = 1;
    s_adv_fields.uuids16 = &s_hid_uuid;
    s_adv_fields.num_uuids16 = 1;
    s_adv_fields.uuids16_is_complete = 1;

    // 键盘无安全输入通道，采用 LE Secure Connections Just Works，并持久化绑定密钥。
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;

    err = esp_hidd_dev_init(&s_hid_config, ESP_HID_TRANSPORT_BLE, hidd_event,
                            &s_hid_dev);
    if (err != ESP_OK) return err;
    if (ble_svc_gap_device_name_set(GESTURE_KEY_NAME) != 0) return ESP_FAIL;
    ble_store_config_init();
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    return esp_nimble_enable(nimble_host_task);
}

esp_err_t gesture_key_start(void)
{
    if (s_button_queue) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        if ((err = nvs_flash_erase()) == ESP_OK) err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;

    s_model_valid = load_model();
    s_button_queue = xQueueCreate(12, sizeof(button_event_t));
    if (!s_button_queue) return ESP_ERR_NO_MEM;

    atomic_store(&s_connected, false);
    atomic_store(&s_encrypted, false);
    atomic_store(&s_ble_failed, false);
    atomic_store(&s_state, GESTURE_KEY_STARTING);
    atomic_store(&s_calibration_percent, 0U);
    atomic_store(&s_recording_percent, 0U);
    atomic_store(&s_last_score, 0U);
    atomic_store(&s_rejected_sample, 0U);

    err = ble_start();
    if (err != ESP_OK) {
        atomic_store(&s_ble_failed, true);
        set_state(GESTURE_KEY_BLE_ERROR);
        ESP_LOGE(TAG, "BLE HID init failed: %s", esp_err_to_name(err));
        return err;
    }
    if (xTaskCreate(gesture_task, "gesture_key", 8192, NULL, 5, NULL) != pdPASS) {
        atomic_store(&s_ble_failed, true);
        set_state(GESTURE_KEY_BLE_ERROR);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void gesture_key_button_event(bsp_btn_t btn, bsp_btn_ev_t event, void *user)
{
    (void)user;
    if (!s_button_queue) return;
    const button_event_t button = {.btn = btn, .event = event};
    (void)xQueueSend(s_button_queue, &button, 0);
}

gesture_key_state_t gesture_key_state(void)
{
    return (gesture_key_state_t)atomic_load(&s_state);
}

uint8_t gesture_key_calibration_percent(void)
{
    return atomic_load(&s_calibration_percent);
}

uint8_t gesture_key_recording_percent(void)
{
    return atomic_load(&s_recording_percent);
}

uint8_t gesture_key_enrollment_sample(void)
{
    return atomic_load(&s_enrollment_sample);
}

uint8_t gesture_key_last_score(void)
{
    return atomic_load(&s_last_score);
}

uint8_t gesture_key_rejected_sample(void)
{
    return atomic_load(&s_rejected_sample);
}

bool gesture_key_motion_active(void)
{
    gesture_key_state_t state = gesture_key_state();
    return state == GESTURE_KEY_LEARN_RECORDING ||
           state == GESTURE_KEY_VERIFY_RECORDING;
}
