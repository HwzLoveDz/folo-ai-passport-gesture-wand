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
#include "services/bas/ble_svc_bas.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "sdkconfig.h"

#ifndef CONFIG_BT_NIMBLE_NVS_PERSIST
#error "Mote Wand requires persistent NimBLE bonds across device restarts"
#endif

#ifndef CONFIG_BT_NIMBLE_SVC_BAS_BATTERY_LEVEL_NOTIFY
#error "Mote Wand requires BLE Battery Service notifications"
#endif

static const char *TAG = "gesture_key";

#define GESTURE_KEY_NAME              "Mote Wand"
#define HID_SERVICE_UUID              0x1812
#define SAMPLE_PERIOD_MS              10U
#define CALIBRATION_SAMPLES           64U
#define CALIBRATION_GYRO_MAX_SQ       25.0f
#define CALIBRATION_ACCEL_STEP_MAX_SQ  0.0064f
#define IDLE_GYRO_DELTA_MAX            0.8f
#define IDLE_ACCEL_STEP_MAX_SQ         0.0025f
#define RECORD_ARM_MS                 90U
#define VERIFY_SCORE_MIN              75.0f
#define ENROLL_PAIR_SCORE_MIN         72.0f
#define VERIFY_AMBIGUITY_FLOOR        70.0f
#define VERIFY_AMBIGUITY_MARGIN        8.0f
#define ENROLL_CONFLICT_SCORE         84.0f
#define MODEL_MAGIC                   0x47535452U
#define MODEL_VERSION                 2U
#define LEGACY_MODEL_VERSION          1U
#define MODEL_NAMESPACE               "gesture_key"
#define LEGACY_MODEL_KEY              "model"
#define RESET_EPOCH_KEY               "reset_epoch"
#define GATT_SCHEMA_EPOCH_KEY         "gatt_epoch"
#define GATT_SCHEMA_EPOCH             1U

#define HID_KEY_ENTER                 0x28U
#define HID_KEY_L                     0x0FU
#define HID_KEY_T                     0x17U
#define HID_MOD_LEFT_CTRL             0x01U
#define HID_MOD_LEFT_GUI              0x08U
#define ACTION_SEQUENCE_MAX_LENGTH    32U
#define ACTION_SEQUENCE_DIGITS         6U
#define RESULT_HOLD_MS              1000U
#define PIN_RESULT_HOLD_MS RESULT_HOLD_MS
#define LINK_QUALITY_POLL_MS          500U
#define INVALID_CONN_HANDLE        UINT16_MAX

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
static const char s_boot_pin[] = CONFIG_MOTE_WAND_BOOT_PIN;
_Static_assert(sizeof(s_action_sequence) <= ACTION_SEQUENCE_MAX_LENGTH + 1U,
               "Mote Wand action sequence is too long");
_Static_assert(sizeof(s_action_sequence) == 1U ||
               sizeof(s_action_sequence) == ACTION_SEQUENCE_DIGITS + 1U,
               "Mote Wand HID action sequence must be empty or six digits");
_Static_assert(sizeof(s_boot_pin) == GESTURE_KEY_PIN_DIGITS + 1U,
               "Mote Wand boot PIN must be four digits");
_Static_assert(GESTURE_MACRO_COUNT <= 8U,
               "Gesture model mask only supports eight macros");

static const char *const s_macro_names[GESTURE_MACRO_COUNT] = {
    [GESTURE_MACRO_AUTH_SEQUENCE] = GESTURE_MACRO_AUTH_SEQUENCE_NAME,
    [GESTURE_MACRO_LOCK_HOST] = GESTURE_MACRO_LOCK_HOST_NAME,
    [GESTURE_MACRO_NEW_TAB] = GESTURE_MACRO_NEW_TAB_NAME,
};

static const char *const s_model_keys[GESTURE_MACRO_COUNT] = {
    [GESTURE_MACRO_AUTH_SEQUENCE] = "auth_v2",
    [GESTURE_MACRO_LOCK_HOST] = "lock_v2",
    [GESTURE_MACRO_NEW_TAB] = "tab_v2",
};

static struct ble_hs_adv_fields s_adv_fields;
static ble_uuid16_t s_hid_uuid = BLE_UUID16_INIT(HID_SERVICE_UUID);
static esp_hidd_dev_t *s_hid_dev;
static QueueHandle_t s_button_queue;

static atomic_bool s_connected;
static atomic_bool s_encrypted;
static atomic_bool s_ble_failed;
static atomic_bool s_bas_ready;
static atomic_int s_battery_level = ATOMIC_VAR_INIT(-1);
static atomic_int s_state;
static atomic_uchar s_calibration_percent;
static atomic_uchar s_recording_percent;
static atomic_uchar s_enrollment_sample;
static atomic_uchar s_last_score;
static atomic_uchar s_rejected_sample;
static atomic_uchar s_model_mask;
static atomic_uchar s_menu_selected;
static atomic_uchar s_menu_action;
static atomic_uchar s_menu_confirm;
static atomic_uchar s_active_macro;
static atomic_uchar s_conflicting_macro;
static atomic_uchar s_pin_position;
static atomic_uchar s_pin_digit;
static atomic_bool s_boot_unlocked;
static atomic_uint_fast32_t s_trace_revision;
static atomic_uint_fast16_t s_conn_handle = ATOMIC_VAR_INIT(INVALID_CONN_HANDLE);
static atomic_int s_link_rssi = ATOMIC_VAR_INIT(GESTURE_KEY_RSSI_UNAVAILABLE);
static atomic_uint_fast32_t s_link_rssi_revision;

static gesture_sample_t s_raw_samples[GESTURE_MAX_SAMPLES];
static portMUX_TYPE s_trace_mux = portMUX_INITIALIZER_UNLOCKED;
static size_t s_raw_count;
static bool s_recording;
static bool s_record_overflow;
static bool s_record_for_enrollment;
static int64_t s_record_arm_us;

static uint8_t s_pin_digits[GESTURE_KEY_PIN_DIGITS];
static int64_t s_pin_deadline_us;

static gesture_signature_t s_enrollment[3];
static size_t s_enrollment_count;
static gesture_signature_t s_models[GESTURE_MACRO_COUNT];
static bool s_models_valid[GESTURE_MACRO_COUNT];
static gesture_macro_t s_enrollment_target;
static bool s_enrollment_from_menu;

static float s_gyro_sum[3];
static float s_gyro_bias[3];
static float s_calibration_prev_accel[3];
static float s_idle_prev_accel[3];
static unsigned s_calibration_count;
static bool s_calibrated;
static bool s_calibration_have_accel;
static bool s_idle_have_accel;
static bool s_was_hid_ready;
static bool s_gatt_schema_update_pending;

static int64_t s_transient_deadline_us;
static gesture_key_state_t s_after_transient;

void ble_store_config_init(void);
static esp_err_t ble_start(void);

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

static bool credentials_config_valid(void)
{
    for (size_t i = 0; i < GESTURE_KEY_PIN_DIGITS; i++) {
        if (s_boot_pin[i] < '0' || s_boot_pin[i] > '9') return false;
    }
    if (s_action_sequence[0] == '\0') return true;
    for (size_t i = 0; i < ACTION_SEQUENCE_DIGITS; i++) {
        if (s_action_sequence[i] < '0' || s_action_sequence[i] > '9') {
            return false;
        }
    }
    return true;
}

static bool boot_pin_matches(void)
{
    for (size_t i = 0; i < GESTURE_KEY_PIN_DIGITS; i++) {
        if (s_pin_digits[i] != (uint8_t)(s_boot_pin[i] - '0')) {
            return false;
        }
    }
    return true;
}

static void reset_pin_entry(void)
{
    memset(s_pin_digits, 0, sizeof(s_pin_digits));
    atomic_store(&s_pin_position, 0U);
    atomic_store(&s_pin_digit, 0U);
    s_pin_deadline_us = 0;
    set_state(GESTURE_KEY_PIN_ENTRY);
}

static void process_pin_click(bsp_btn_t button)
{
    if (gesture_key_state() != GESTURE_KEY_PIN_ENTRY) return;
    uint8_t digit = atomic_load(&s_pin_digit);
    if (button == BSP_BTN_UP || button == BSP_BTN_DOWN) {
        digit = button == BSP_BTN_UP ? (uint8_t)((digit + 1U) % 10U) :
                                      (uint8_t)((digit + 9U) % 10U);
        atomic_store(&s_pin_digit, digit);
        gesture_sound_play(GESTURE_SOUND_PIN_MOVE);
        return;
    }
    if (button != BSP_BTN_OK) return;

    uint8_t position = atomic_load(&s_pin_position);
    if (position >= GESTURE_KEY_PIN_DIGITS) position = 0U;
    s_pin_digits[position] = digit;
    if (position + 1U < GESTURE_KEY_PIN_DIGITS) {
        atomic_store(&s_pin_position, (uint8_t)(position + 1U));
        atomic_store(&s_pin_digit, 0U);
        gesture_sound_play(GESTURE_SOUND_PIN_CONFIRM);
        return;
    }

    s_pin_deadline_us = esp_timer_get_time() +
        (int64_t)PIN_RESULT_HOLD_MS * 1000LL;
    if (boot_pin_matches()) {
        set_state(GESTURE_KEY_PIN_ACCEPTED);
        gesture_sound_play(GESTURE_SOUND_PIN_ACCEPTED);
    } else {
        set_state(GESTURE_KEY_PIN_ERROR);
        gesture_sound_play(GESTURE_SOUND_PIN_REJECTED);
    }
}

static void update_pin_state(void)
{
    gesture_key_state_t state = gesture_key_state();
    if ((state != GESTURE_KEY_PIN_ERROR &&
         state != GESTURE_KEY_PIN_ACCEPTED) ||
        s_pin_deadline_us <= 0 || esp_timer_get_time() < s_pin_deadline_us) {
        return;
    }
    if (state == GESTURE_KEY_PIN_ERROR) {
        reset_pin_entry();
        return;
    }

    s_pin_deadline_us = 0;
    atomic_store(&s_boot_unlocked, true);
    set_state(GESTURE_KEY_STARTING);
    esp_err_t err = ble_start();
    if (err != ESP_OK) {
        atomic_store(&s_ble_failed, true);
        set_state(GESTURE_KEY_BLE_ERROR);
        ESP_LOGE(TAG, "BLE HID init failed after access grant: %s",
                 esp_err_to_name(err));
        return;
    }
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

static uint8_t current_model_mask(void)
{
    uint8_t mask = 0U;
    for (unsigned i = 0; i < GESTURE_MACRO_COUNT; i++) {
        if (s_models_valid[i]) mask |= (uint8_t)(1U << i);
    }
    return mask;
}

static void publish_model_mask(void)
{
    atomic_store(&s_model_mask, current_model_mask());
}

static bool load_model_key(const char *key, uint16_t expected_version,
                           gesture_signature_t *signature)
{
    if (!key || !signature) return false;
    nvs_handle_t handle;
    if (nvs_open(MODEL_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
    stored_model_t stored;
    size_t size = sizeof(stored);
    esp_err_t err = nvs_get_blob(handle, key, &stored, &size);
    nvs_close(handle);
    if (err != ESP_OK || size != sizeof(stored) ||
        stored.magic != MODEL_MAGIC || stored.version != expected_version ||
        stored.signature_size != sizeof(gesture_signature_t)) {
        return false;
    }
    uint32_t expected = model_crc32(&stored, offsetof(stored_model_t, crc32));
    if (expected != stored.crc32 || !signature_valid(&stored.signature)) return false;
    *signature = stored.signature;
    return true;
}

static esp_err_t save_model(gesture_macro_t macro,
                            const gesture_signature_t *signature)
{
    if ((unsigned)macro >= GESTURE_MACRO_COUNT || !signature ||
        !signature_valid(signature)) {
        return ESP_ERR_INVALID_ARG;
    }
    stored_model_t stored = {
        .magic = MODEL_MAGIC,
        .version = MODEL_VERSION,
        .signature_size = sizeof(gesture_signature_t),
        .signature = *signature,
    };
    stored.crc32 = model_crc32(&stored, offsetof(stored_model_t, crc32));

    nvs_handle_t handle;
    esp_err_t err = nvs_open(MODEL_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, s_model_keys[macro], &stored, sizeof(stored));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static esp_err_t erase_optional_key(nvs_handle_t handle, const char *key)
{
    esp_err_t err = nvs_erase_key(handle, key);
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

static esp_err_t erase_model(gesture_macro_t macro)
{
    if ((unsigned)macro >= GESTURE_MACRO_COUNT) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(MODEL_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = erase_optional_key(handle, s_model_keys[macro]);
    if (err == ESP_OK && macro == GESTURE_MACRO_AUTH_SEQUENCE) {
        // Prevent the V1 compatibility blob from restoring a cleared gesture
        // on the next boot.
        err = erase_optional_key(handle, LEGACY_MODEL_KEY);
    }
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) return err;

    memset(&s_models[macro], 0, sizeof(s_models[macro]));
    s_models_valid[macro] = false;
    publish_model_mask();
    return ESP_OK;
}

static esp_err_t apply_gesture_reset_epoch(void)
{
#if CONFIG_MOTE_WAND_GESTURE_RESET_EPOCH > 0
    const int32_t configured_epoch = CONFIG_MOTE_WAND_GESTURE_RESET_EPOCH;
    int32_t applied_epoch = 0;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(MODEL_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_get_i32(handle, RESET_EPOCH_KEY, &applied_epoch);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }
    if (err == ESP_OK && configured_epoch <= applied_epoch) {
        nvs_close(handle);
        return ESP_OK;
    }

    err = erase_optional_key(handle, LEGACY_MODEL_KEY);
    for (unsigned i = 0; err == ESP_OK && i < GESTURE_MACRO_COUNT; i++) {
        err = erase_optional_key(handle, s_model_keys[i]);
    }
    if (err == ESP_OK) err = nvs_set_i32(handle, RESET_EPOCH_KEY, configured_epoch);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
#else
    return ESP_OK;
#endif
}

static esp_err_t prepare_gatt_schema_update(void)
{
    s_gatt_schema_update_pending = false;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(MODEL_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_gatt_schema_update_pending = true;
        return ESP_OK;
    }
    if (err != ESP_OK) return err;

    uint32_t applied_epoch = 0U;
    err = nvs_get_u32(handle, GATT_SCHEMA_EPOCH_KEY, &applied_epoch);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_gatt_schema_update_pending = true;
        return ESP_OK;
    }
    if (err != ESP_OK) return err;

    s_gatt_schema_update_pending = applied_epoch < GATT_SCHEMA_EPOCH;
    return ESP_OK;
}

static esp_err_t commit_gatt_schema_epoch(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(MODEL_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u32(handle, GATT_SCHEMA_EPOCH_KEY, GATT_SCHEMA_EPOCH);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static void load_models(void)
{
    memset(s_models, 0, sizeof(s_models));
    memset(s_models_valid, 0, sizeof(s_models_valid));
    for (unsigned i = 0; i < GESTURE_MACRO_COUNT; i++) {
        s_models_valid[i] = load_model_key(s_model_keys[i], MODEL_VERSION,
                                           &s_models[i]);
    }

    // V1 only had AUTH_SEQUENCE. Import it without deleting the old blob so an
    // interrupted migration can never discard the user's trained gesture.
    if (!s_models_valid[GESTURE_MACRO_AUTH_SEQUENCE] &&
        load_model_key(LEGACY_MODEL_KEY, LEGACY_MODEL_VERSION,
                       &s_models[GESTURE_MACRO_AUTH_SEQUENCE])) {
        s_models_valid[GESTURE_MACRO_AUTH_SEQUENCE] = true;
        (void)save_model(GESTURE_MACRO_AUTH_SEQUENCE,
                         &s_models[GESTURE_MACRO_AUTH_SEQUENCE]);
    }
    publish_model_mask();
}

static bool hid_ready(void)
{
    return s_hid_dev && atomic_load(&s_connected) && atomic_load(&s_encrypted) &&
           esp_hidd_dev_connected(s_hid_dev);
}

static esp_err_t send_key_report(uint8_t modifier, uint8_t usage)
{
    if (!hid_ready()) return ESP_ERR_INVALID_STATE;
    uint8_t report[8] = {0};
    report[0] = modifier;
    report[2] = usage;
    esp_err_t err = esp_hidd_dev_input_set(s_hid_dev, 0, 0, report, sizeof(report));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(32));
    memset(report, 0, sizeof(report));
    err = esp_hidd_dev_input_set(s_hid_dev, 0, 0, report, sizeof(report));
    vTaskDelay(pdMS_TO_TICKS(24));
    return err;
}

static esp_err_t send_key(uint8_t usage)
{
    return send_key_report(0U, usage);
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

static esp_err_t execute_macro(gesture_macro_t macro)
{
    switch (macro) {
    case GESTURE_MACRO_AUTH_SEQUENCE:
        return send_action_sequence();
    case GESTURE_MACRO_LOCK_HOST:
        // Windows lock shortcut. The BLE HID report is released immediately
        // after the chord so no modifier can remain latched on the host.
        return send_key_report(HID_MOD_LEFT_GUI, HID_KEY_L);
    case GESTURE_MACRO_NEW_TAB:
        // Ctrl+T opens a tab in Chrome and other common Windows browsers.
        return send_key_report(HID_MOD_LEFT_CTRL, HID_KEY_T);
    default:
        return ESP_ERR_INVALID_ARG;
    }
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
    memset(s_calibration_prev_accel, 0, sizeof(s_calibration_prev_accel));
    memset(s_idle_prev_accel, 0, sizeof(s_idle_prev_accel));
    s_calibration_count = 0U;
    s_calibrated = false;
    s_calibration_have_accel = false;
    s_idle_have_accel = false;
    atomic_store(&s_calibration_percent, 0U);
}

static float update_accel_step_sq(const float accel[3], float previous[3],
                                  bool *have_previous)
{
    float step_sq = 0.0f;
    if (*have_previous) {
        for (size_t axis = 0; axis < 3U; axis++) {
            float delta = accel[axis] - previous[axis];
            step_sq += delta * delta;
        }
    }
    memcpy(previous, accel, sizeof(float) * 3U);
    bool had_previous = *have_previous;
    *have_previous = true;
    return had_previous ? step_sq : INFINITY;
}

static bool add_calibration_sample(const bsp_qmi8658a_sample_t *sample)
{
    float accel[3];
    float gyro[3];
    map_sensor_axes(sample, accel, gyro);
    float gyro2 = gyro[0] * gyro[0] + gyro[1] * gyro[1] + gyro[2] * gyro[2];
    float accel_step2 = update_accel_step_sq(accel, s_calibration_prev_accel,
                                             &s_calibration_have_accel);

    // This phase estimates gyro zero bias only. Absolute acceleration depends
    // on mounting, scale error and sensor health; frame-to-frame change plus
    // gyro magnitude is the correct stationary gate here.
    if (gyro2 > CALIBRATION_GYRO_MAX_SQ ||
        accel_step2 > CALIBRATION_ACCEL_STEP_MAX_SQ) {
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
    float accel_step2 = update_accel_step_sq(accel, s_idle_prev_accel,
                                             &s_idle_have_accel);
    bool still = accel_step2 <= IDLE_ACCEL_STEP_MAX_SQ;
    for (size_t axis = 0; axis < 3U; axis++) {
        if (fabsf(gyro[axis] - s_gyro_bias[axis]) >= IDLE_GYRO_DELTA_MAX) {
            still = false;
        }
    }
    if (!still) return;
    for (size_t axis = 0; axis < 3U; axis++) {
        s_gyro_bias[axis] += 0.0015f * (gyro[axis] - s_gyro_bias[axis]);
    }
}

static bool any_model_valid(void)
{
    return current_model_mask() != 0U;
}

static gesture_macro_t selected_macro(void)
{
    unsigned selected = atomic_load(&s_menu_selected);
    return selected < GESTURE_MACRO_COUNT ? (gesture_macro_t)selected :
                                            GESTURE_MACRO_AUTH_SEQUENCE;
}

static void prepare_enrollment(gesture_macro_t macro, bool from_menu)
{
    if ((unsigned)macro >= GESTURE_MACRO_COUNT) {
        macro = GESTURE_MACRO_AUTH_SEQUENCE;
    }
    memset(s_enrollment, 0, sizeof(s_enrollment));
    s_enrollment_count = 0U;
    s_enrollment_target = macro;
    s_enrollment_from_menu = from_menu;
    atomic_store(&s_active_macro, macro);
    atomic_store(&s_conflicting_macro, GESTURE_MACRO_NONE);
    atomic_store(&s_enrollment_sample, 1U);
    atomic_store(&s_rejected_sample, 0U);
}

static void enter_operational_state(void)
{
    if (any_model_valid()) {
        atomic_store(&s_active_macro, GESTURE_MACRO_NONE);
        set_state(GESTURE_KEY_READY);
    } else {
        prepare_enrollment(GESTURE_MACRO_AUTH_SEQUENCE, false);
        set_state(GESTURE_KEY_LEARN_READY);
    }
}

static void begin_recording(void)
{
    gesture_key_state_t state = gesture_key_state();
    bool enrollment = state == GESTURE_KEY_LEARN_READY;
    bool verification = state == GESTURE_KEY_READY;
    if (!hid_ready() || !s_calibrated || (!enrollment && !verification)) return;

    taskENTER_CRITICAL(&s_trace_mux);
    s_raw_count = 0U;
    taskEXIT_CRITICAL(&s_trace_mux);
    atomic_fetch_add(&s_trace_revision, 1U);
    s_record_overflow = false;
    s_record_for_enrollment = enrollment;
    if (!enrollment) {
        atomic_store(&s_active_macro, GESTURE_MACRO_NONE);
        atomic_store(&s_conflicting_macro, GESTURE_MACRO_NONE);
    }
    s_recording = true;
    s_record_arm_us = esp_timer_get_time() + (int64_t)RECORD_ARM_MS * 1000LL;
    atomic_store(&s_recording_percent, 0U);
    set_state(enrollment ? GESTURE_KEY_LEARN_RECORDING : GESTURE_KEY_VERIFY_RECORDING);
    gesture_sound_play(GESTURE_SOUND_RECORD_START);
}

static void set_invalid_result(gesture_build_result_t result)
{
    gesture_key_state_t next = s_record_for_enrollment ? GESTURE_KEY_LEARN_READY :
                                                        GESTURE_KEY_READY;
    if (result == GESTURE_BUILD_TOO_LONG) {
        set_transient_state(GESTURE_KEY_TOO_LONG, RESULT_HOLD_MS, next);
    } else {
        set_transient_state(GESTURE_KEY_INVALID_MOTION, RESULT_HOLD_MS, next);
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
        set_transient_state(GESTURE_KEY_LEARN_SAMPLE_OK, RESULT_HOLD_MS,
                            GESTURE_KEY_LEARN_READY);
        gesture_sound_play(GESTURE_SOUND_SAMPLE_OK);
        return;
    }

    float pair_scores[3];
    int outlier = gesture_enrollment_outlier(s_enrollment, ENROLL_PAIR_SCORE_MIN,
                                             pair_scores);
    if (outlier >= 0) {
        keep_consistent_enrollment_pair(outlier);
        set_transient_state(GESTURE_KEY_LEARN_RETRY, RESULT_HOLD_MS,
                            GESTURE_KEY_LEARN_READY);
        gesture_sound_play(GESTURE_SOUND_RETRY);
        return;
    }
    if (outlier == -2) {
        s_enrollment_count = 0U;
        atomic_store(&s_enrollment_sample, 1U);
        atomic_store(&s_rejected_sample, 0U);
        set_transient_state(GESTURE_KEY_LEARN_RESTART, RESULT_HOLD_MS,
                            GESTURE_KEY_LEARN_READY);
        gesture_sound_play(GESTURE_SOUND_RETRY);
        return;
    }

    gesture_signature_t new_model;
    gesture_signature_average(s_enrollment, 3U, &new_model);
    for (unsigned i = 0; i < GESTURE_MACRO_COUNT; i++) {
        if (i == (unsigned)s_enrollment_target || !s_models_valid[i]) continue;
        float score = gesture_similarity(&new_model, &s_models[i]);
        if (score >= ENROLL_CONFLICT_SCORE) {
            atomic_store(&s_conflicting_macro, (uint8_t)i);
            s_enrollment_count = 0U;
            atomic_store(&s_enrollment_sample, 1U);
            atomic_store(&s_rejected_sample, 0U);
            set_transient_state(GESTURE_KEY_LEARN_CONFLICT, RESULT_HOLD_MS,
                                GESTURE_KEY_LEARN_READY);
            gesture_sound_play(GESTURE_SOUND_RETRY);
            return;
        }
    }

    atomic_store(&s_enrollment_sample, 3U);
    if (save_model(s_enrollment_target, &new_model) != ESP_OK) {
        gesture_key_state_t after = s_enrollment_from_menu ?
            GESTURE_KEY_MENU_DETAIL : GESTURE_KEY_LEARN_READY;
        set_transient_state(GESTURE_KEY_STORAGE_ERROR, RESULT_HOLD_MS, after);
        gesture_sound_play(GESTURE_SOUND_ERROR);
        return;
    }
    s_models[s_enrollment_target] = new_model;
    s_models_valid[s_enrollment_target] = true;
    publish_model_mask();
    gesture_key_state_t after = s_enrollment_from_menu ?
        GESTURE_KEY_MENU_DETAIL : GESTURE_KEY_READY;
    set_transient_state(GESTURE_KEY_LEARN_SAVED, RESULT_HOLD_MS, after);
    gesture_sound_play(GESTURE_SOUND_SAVED);
}

static void finish_verification(const gesture_signature_t *candidate)
{
    size_t best_index = SIZE_MAX;
    size_t second_index = SIZE_MAX;
    float best_score = -1.0f;
    float second_score = -1.0f;
    gesture_classify_result_t result = gesture_signature_classify(
        candidate, s_models, current_model_mask(), GESTURE_MACRO_COUNT,
        VERIFY_SCORE_MIN, VERIFY_AMBIGUITY_FLOOR, VERIFY_AMBIGUITY_MARGIN,
        &best_index, &second_index, &best_score, &second_score);
    gesture_macro_t best = best_index < GESTURE_MACRO_COUNT ?
        (gesture_macro_t)best_index : GESTURE_MACRO_NONE;
    gesture_macro_t second = second_index < GESTURE_MACRO_COUNT ?
        (gesture_macro_t)second_index : GESTURE_MACRO_NONE;

    uint8_t rounded = best_score > 0.0f ? (uint8_t)lroundf(best_score) : 0U;
    if (rounded > 100U) rounded = 100U;
    atomic_store(&s_last_score, rounded);
    atomic_store(&s_active_macro, best);
    atomic_store(&s_conflicting_macro, second);
    if (result == GESTURE_CLASSIFY_NO_MATCH) {
        set_transient_state(GESTURE_KEY_NO_MATCH, RESULT_HOLD_MS,
                            GESTURE_KEY_READY);
        gesture_sound_play(GESTURE_SOUND_REJECT);
        return;
    }
    if (result == GESTURE_CLASSIFY_AMBIGUOUS) {
        set_transient_state(GESTURE_KEY_AMBIGUOUS, RESULT_HOLD_MS,
                            GESTURE_KEY_READY);
        gesture_sound_play(GESTURE_SOUND_REJECT);
        return;
    }

    set_transient_state(GESTURE_KEY_MATCHED, RESULT_HOLD_MS, GESTURE_KEY_READY);
    gesture_sound_play(best == GESTURE_MACRO_LOCK_HOST ? GESTURE_SOUND_LOCK :
                                                        GESTURE_SOUND_MATCH);
    vTaskDelay(pdMS_TO_TICKS(90));
    if (execute_macro(best) != ESP_OK) {
        set_transient_state(GESTURE_KEY_SEND_ERROR, RESULT_HOLD_MS,
                            GESTURE_KEY_READY);
        gesture_sound_play(GESTURE_SOUND_ERROR);
    } else {
        // Macro transmission can take a few hundred milliseconds. Restart the
        // deadline after it completes so the confirmed result remains visible
        // for a full second instead of being shortened by HID report latency.
        s_transient_deadline_us = esp_timer_get_time() +
            (int64_t)RESULT_HOLD_MS * 1000LL;
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

static void open_manager(void)
{
    if (gesture_key_state() != GESTURE_KEY_READY || !hid_ready()) return;
    atomic_store(&s_active_macro, selected_macro());
    atomic_store(&s_menu_action, GESTURE_MENU_RE_RECORD);
    set_state(GESTURE_KEY_MENU_LIST);
    gesture_sound_play(GESTURE_SOUND_MENU_OPEN);
}

static void close_manager(void)
{
    atomic_store(&s_active_macro, GESTURE_MACRO_NONE);
    enter_operational_state();
    gesture_sound_play(GESTURE_SOUND_MENU_BACK);
}

static void move_manager_selection(int direction)
{
    int selected = (int)selected_macro() + direction;
    if (selected < 0) selected = GESTURE_MACRO_COUNT - 1;
    if (selected >= GESTURE_MACRO_COUNT) selected = 0;
    atomic_store(&s_menu_selected, (uint8_t)selected);
    atomic_store(&s_menu_action, GESTURE_MENU_RE_RECORD);
    atomic_store(&s_active_macro, (uint8_t)selected);
    gesture_sound_play(GESTURE_SOUND_MENU_MOVE);
}

static void move_manager_action(int direction)
{
    int action = (int)atomic_load(&s_menu_action) + direction;
    if (action < 0) action = GESTURE_MENU_ACTION_COUNT - 1;
    if (action >= GESTURE_MENU_ACTION_COUNT) action = 0;
    atomic_store(&s_menu_action, (uint8_t)action);
    gesture_sound_play(GESTURE_SOUND_MENU_MOVE);
}

static void request_selected_relearn(void)
{
    gesture_macro_t target = selected_macro();
    prepare_enrollment(target, true);
    atomic_store(&s_active_macro, target);
    atomic_store(&s_conflicting_macro, GESTURE_MACRO_NONE);
    set_state(GESTURE_KEY_LEARN_READY);
    gesture_sound_play(GESTURE_SOUND_MENU_SELECT);
}

static void open_clear_confirmation(void)
{
    atomic_store(&s_menu_confirm, GESTURE_MENU_CONFIRM_CANCEL);
    set_state(GESTURE_KEY_MENU_CLEAR_CONFIRM);
    gesture_sound_play(GESTURE_SOUND_MENU_SELECT);
}

static void move_clear_confirmation(void)
{
    gesture_menu_confirm_t choice =
        (gesture_menu_confirm_t)atomic_load(&s_menu_confirm);
    choice = choice == GESTURE_MENU_CONFIRM_CLEAR ?
             GESTURE_MENU_CONFIRM_CANCEL : GESTURE_MENU_CONFIRM_CLEAR;
    atomic_store(&s_menu_confirm, choice);
    gesture_sound_play(GESTURE_SOUND_MENU_MOVE);
}

static void perform_selected_clear(void)
{
    gesture_macro_t target = selected_macro();
    if (!s_models_valid[target]) {
        set_transient_state(GESTURE_KEY_MENU_CLEAR_ERROR, RESULT_HOLD_MS,
                            GESTURE_KEY_MENU_DETAIL);
        gesture_sound_play(GESTURE_SOUND_ERROR);
        return;
    }
    atomic_store(&s_active_macro, target);
    if (erase_model(target) != ESP_OK) {
        set_transient_state(GESTURE_KEY_MENU_CLEAR_ERROR, RESULT_HOLD_MS,
                            GESTURE_KEY_MENU_DETAIL);
        gesture_sound_play(GESTURE_SOUND_ERROR);
        return;
    }
    set_transient_state(GESTURE_KEY_MENU_CLEAR_DONE, RESULT_HOLD_MS,
                        GESTURE_KEY_MENU_DETAIL);
    gesture_sound_play(GESTURE_SOUND_RESET);
}

static void resolve_clear_confirmation(void)
{
    if (atomic_load(&s_menu_confirm) == GESTURE_MENU_CONFIRM_CLEAR) {
        perform_selected_clear();
    } else {
        set_state(GESTURE_KEY_MENU_DETAIL);
        gesture_sound_play(GESTURE_SOUND_MENU_BACK);
    }
}

static void run_selected_manager_action(void)
{
    if (atomic_load(&s_menu_action) == GESTURE_MENU_CLEAR) {
        open_clear_confirmation();
    } else {
        request_selected_relearn();
    }
}

static void cancel_management_action(void)
{
    if (!s_enrollment_from_menu) return;
    s_recording = false;
    s_transient_deadline_us = 0;
    memset(s_enrollment, 0, sizeof(s_enrollment));
    s_enrollment_count = 0U;
    atomic_store(&s_enrollment_sample, 1U);
    atomic_store(&s_rejected_sample, 0U);
    atomic_store(&s_active_macro, selected_macro());
    set_state(GESTURE_KEY_MENU_DETAIL);
    gesture_sound_play(GESTURE_SOUND_MENU_BACK);
}

static void process_button_events(void)
{
    button_event_t button;
    while (xQueueReceive(s_button_queue, &button, 0) == pdTRUE) {
        if (button.btn == BSP_BTN_OK && button.event == BSP_BTN_PRESS) {
            begin_recording();
        } else if (button.btn == BSP_BTN_OK && button.event == BSP_BTN_RELEASE) {
            end_recording();
        } else if (button.event == BSP_BTN_CLICK) {
            gesture_key_state_t state = gesture_key_state();
            if (state == GESTURE_KEY_PIN_ENTRY ||
                state == GESTURE_KEY_PIN_ERROR ||
                state == GESTURE_KEY_PIN_ACCEPTED) {
                process_pin_click(button.btn);
            } else if (state == GESTURE_KEY_READY && button.btn == BSP_BTN_UP) {
                open_manager();
            } else if (state == GESTURE_KEY_MENU_LIST) {
                if (button.btn == BSP_BTN_UP) {
                    move_manager_selection(-1);
                } else if (button.btn == BSP_BTN_DOWN) {
                    move_manager_selection(1);
                } else if (button.btn == BSP_BTN_OK) {
                    atomic_store(&s_menu_action, GESTURE_MENU_RE_RECORD);
                    set_state(GESTURE_KEY_MENU_DETAIL);
                    gesture_sound_play(GESTURE_SOUND_MENU_SELECT);
                }
            } else if (state == GESTURE_KEY_MENU_DETAIL) {
                if (button.btn == BSP_BTN_OK) {
                    run_selected_manager_action();
                } else if (button.btn == BSP_BTN_UP) {
                    move_manager_action(-1);
                } else if (button.btn == BSP_BTN_DOWN) {
                    move_manager_action(1);
                }
            } else if (state == GESTURE_KEY_MENU_CLEAR_CONFIRM) {
                if (button.btn == BSP_BTN_OK) {
                    resolve_clear_confirmation();
                } else if (button.btn == BSP_BTN_UP ||
                           button.btn == BSP_BTN_DOWN) {
                    move_clear_confirmation();
                }
            }
        } else if (button.btn == BSP_BTN_UP && button.event == BSP_BTN_LONG) {
            gesture_key_state_t state = gesture_key_state();
            if (state == GESTURE_KEY_MENU_LIST) {
                close_manager();
            } else if (state == GESTURE_KEY_MENU_DETAIL) {
                atomic_store(&s_menu_action, GESTURE_MENU_RE_RECORD);
                set_state(GESTURE_KEY_MENU_LIST);
                gesture_sound_play(GESTURE_SOUND_MENU_BACK);
            } else if (state == GESTURE_KEY_MENU_CLEAR_CONFIRM) {
                atomic_store(&s_menu_confirm, GESTURE_MENU_CONFIRM_CANCEL);
                set_state(GESTURE_KEY_MENU_DETAIL);
                gesture_sound_play(GESTURE_SOUND_MENU_BACK);
            } else if (state == GESTURE_KEY_LEARN_READY ||
                       state == GESTURE_KEY_LEARN_SAMPLE_OK ||
                       state == GESTURE_KEY_LEARN_RETRY ||
                       state == GESTURE_KEY_LEARN_RESTART ||
                       state == GESTURE_KEY_LEARN_CONFLICT) {
                cancel_management_action();
            }
        }
    }
}

static void capture_sample(const bsp_qmi8658a_sample_t *sample)
{
    if (esp_timer_get_time() < s_record_arm_us) return;

    float accel[3];
    float gyro[3];
    map_sensor_axes(sample, accel, gyro);

    taskENTER_CRITICAL(&s_trace_mux);
    if (s_raw_count >= GESTURE_MAX_SAMPLES) {
        taskEXIT_CRITICAL(&s_trace_mux);
        s_record_overflow = true;
        atomic_store(&s_recording_percent, 100U);
        return;
    }
    size_t index = s_raw_count;
    for (size_t axis = 0; axis < 3U; axis++) {
        s_raw_samples[index].gyro_dps[axis] = gyro[axis] - s_gyro_bias[axis];
        s_raw_samples[index].accel_g[axis] = accel[axis];
    }
    s_raw_count = index + 1U;
    size_t count = s_raw_count;
    taskEXIT_CRITICAL(&s_trace_mux);

    atomic_fetch_add(&s_trace_revision, 1U);
    atomic_store(&s_recording_percent,
                 (uint8_t)(count * 100U / GESTURE_MAX_SAMPLES));
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
        else enter_operational_state();
    }
}

static void gesture_task(void *arg)
{
    (void)arg;
    reset_calibration();
    atomic_store(&s_enrollment_sample, 1U);
    TickType_t wake = xTaskGetTickCount();

    while (true) {
        process_button_events();
        update_pin_state();
        if (atomic_load(&s_boot_unlocked)) update_connection_state();

        bsp_qmi8658a_sample_t sample;
        esp_err_t err = bsp_qmi8658a_read(&sample);
        if (err == ESP_OK) {
            // Zero-bias calibration is a sensor concern, not a BLE concern.
            // Keep accumulating while the host reconnects so a transient link
            // loss cannot pin IMU ZEROING at 0%.
            if (!s_calibrated) {
                if (add_calibration_sample(&sample)) {
                    if (atomic_load(&s_boot_unlocked)) {
                        gesture_sound_play(GESTURE_SOUND_CALIBRATED);
                    }
                    if (hid_ready()) enter_operational_state();
                }
            } else if (s_recording && hid_ready()) {
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

static void publish_link_rssi(int rssi_dbm)
{
    int previous = atomic_exchange(&s_link_rssi, rssi_dbm);
    // Repeated real samples are chart history, even when the rounded value is
    // unchanged. An unavailable link only needs one clear notification.
    if (rssi_dbm != GESTURE_KEY_RSSI_UNAVAILABLE || previous != rssi_dbm) {
        atomic_fetch_add(&s_link_rssi_revision, 1U);
    }
}

static void link_quality_task(void *arg)
{
    (void)arg;
    while (true) {
        if (!gesture_key_motion_active()) {
            const uint16_t handle = (uint16_t)atomic_load(&s_conn_handle);
            int8_t rssi = 0;
            if (atomic_load(&s_connected) && handle != INVALID_CONN_HANDLE &&
                ble_gap_conn_rssi(handle, &rssi) == 0) {
                publish_link_rssi((int)rssi);
            } else {
                publish_link_rssi(GESTURE_KEY_RSSI_UNAVAILABLE);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(LINK_QUALITY_POLL_MS));
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
            atomic_store(&s_conn_handle, INVALID_CONN_HANDLE);
            publish_link_rssi(GESTURE_KEY_RSSI_UNAVAILABLE);
            (void)start_advertising();
        } else {
            atomic_store(&s_connected, true);
            atomic_store(&s_encrypted, false);
            atomic_store(&s_conn_handle, event->connect.conn_handle);
            rc = ble_gap_security_initiate(event->connect.conn_handle);
            if (rc != 0 && rc != BLE_HS_EALREADY) {
                ESP_LOGW(TAG, "BLE security start failed: %d", rc);
            }
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        atomic_store(&s_connected, false);
        atomic_store(&s_encrypted, false);
        atomic_store(&s_conn_handle, INVALID_CONN_HANDLE);
        publish_link_rssi(GESTURE_KEY_RSSI_UNAVAILABLE);
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

static esp_err_t publish_battery_level(void)
{
    const int level = atomic_load(&s_battery_level);
    if (!atomic_load(&s_bas_ready) || level < 0 || level > 100) return ESP_OK;

    const int rc = ble_svc_bas_battery_level_set((uint8_t)level);
    if (rc != 0) {
        ESP_LOGW(TAG, "BLE battery update failed: %d", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void hidd_event(void *handler_args, esp_event_base_t base, int32_t id,
                       void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_hidd_event_data_t *data = event_data;
    switch ((esp_hidd_event_t)id) {
    case ESP_HIDD_START_EVENT:
        // esp_hid creates BAS during host sync. Seed it before advertising so
        // a host never discovers the service with NimBLE's default 0% value.
        atomic_store(&s_bas_ready, true);
        if (publish_battery_level() != ESP_OK) atomic_store(&s_ble_failed, true);
        if (s_gatt_schema_update_pending) {
            // Adding BAS notifications shifts the following GATT handles.
            // Queue one persistent Service Changed indication for existing
            // bonded peers so their cached HID/BAS tables refresh safely.
            ble_svc_gatt_changed(0x0001U, UINT16_MAX);
            esp_err_t epoch_err = commit_gatt_schema_epoch();
            if (epoch_err == ESP_OK) {
                s_gatt_schema_update_pending = false;
            } else {
                ESP_LOGW(TAG, "GATT schema epoch save failed: %s",
                         esp_err_to_name(epoch_err));
            }
        }
        if (start_advertising() != ESP_OK) atomic_store(&s_ble_failed, true);
        break;
    case ESP_HIDD_CONNECT_EVENT:
        atomic_store(&s_connected, data && data->connect.status == ESP_OK);
        if (data && data->connect.status == ESP_OK) (void)publish_battery_level();
        break;
    case ESP_HIDD_DISCONNECT_EVENT:
        atomic_store(&s_connected, false);
        atomic_store(&s_encrypted, false);
        atomic_store(&s_conn_handle, INVALID_CONN_HANDLE);
        publish_link_rssi(GESTURE_KEY_RSSI_UNAVAILABLE);
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
    if (!credentials_config_valid()) return ESP_ERR_INVALID_ARG;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        if ((err = nvs_flash_erase()) == ESP_OK) err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;

    err = apply_gesture_reset_epoch();
    if (err != ESP_OK) return err;
    err = prepare_gatt_schema_update();
    if (err != ESP_OK) return err;

    load_models();
    s_button_queue = xQueueCreate(12, sizeof(button_event_t));
    if (!s_button_queue) return ESP_ERR_NO_MEM;

    atomic_store(&s_connected, false);
    atomic_store(&s_encrypted, false);
    atomic_store(&s_ble_failed, false);
    atomic_store(&s_bas_ready, false);
    atomic_store(&s_boot_unlocked, false);
    atomic_store(&s_calibration_percent, 0U);
    atomic_store(&s_recording_percent, 0U);
    atomic_store(&s_last_score, 0U);
    atomic_store(&s_rejected_sample, 0U);
    atomic_store(&s_menu_selected, GESTURE_MACRO_AUTH_SEQUENCE);
    atomic_store(&s_menu_action, GESTURE_MENU_RE_RECORD);
    atomic_store(&s_menu_confirm, GESTURE_MENU_CONFIRM_CANCEL);
    atomic_store(&s_active_macro, GESTURE_MACRO_NONE);
    atomic_store(&s_conflicting_macro, GESTURE_MACRO_NONE);
    atomic_store(&s_trace_revision, 0U);
    atomic_store(&s_conn_handle, INVALID_CONN_HANDLE);
    atomic_store(&s_link_rssi, GESTURE_KEY_RSSI_UNAVAILABLE);
    atomic_store(&s_link_rssi_revision, 0U);
    s_enrollment_target = GESTURE_MACRO_AUTH_SEQUENCE;
    s_enrollment_from_menu = false;
    reset_pin_entry();

    if (xTaskCreate(gesture_task, "gesture_key", 8192, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(link_quality_task, "link_quality", 2048, NULL, 2, NULL) !=
        pdPASS) {
        // Link history is supplemental telemetry; gesture/HID operation must
        // remain available if this low-priority task cannot be allocated.
        ESP_LOGW(TAG, "BLE RSSI task unavailable");
    }
    return ESP_OK;
}

esp_err_t gesture_key_set_battery_level(uint8_t percent)
{
    if (percent > 100U) return ESP_ERR_INVALID_ARG;
    atomic_store(&s_battery_level, percent);
    return publish_battery_level();
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

uint8_t gesture_key_model_mask(void)
{
    return atomic_load(&s_model_mask);
}

gesture_macro_t gesture_key_menu_selected(void)
{
    return (gesture_macro_t)atomic_load(&s_menu_selected);
}

gesture_menu_action_t gesture_key_menu_action(void)
{
    return (gesture_menu_action_t)atomic_load(&s_menu_action);
}

gesture_menu_confirm_t gesture_key_menu_confirm(void)
{
    return (gesture_menu_confirm_t)atomic_load(&s_menu_confirm);
}

gesture_macro_t gesture_key_active_macro(void)
{
    return (gesture_macro_t)atomic_load(&s_active_macro);
}

gesture_macro_t gesture_key_conflicting_macro(void)
{
    return (gesture_macro_t)atomic_load(&s_conflicting_macro);
}

uint8_t gesture_key_pin_position(void)
{
    return atomic_load(&s_pin_position);
}

uint8_t gesture_key_pin_digit(void)
{
    return atomic_load(&s_pin_digit);
}

const char *gesture_key_macro_name(gesture_macro_t macro)
{
    return (unsigned)macro < GESTURE_MACRO_COUNT ? s_macro_names[macro] : "AUTO_SELECT";
}

static int16_t trace_display_value(float gyro_dps)
{
    if (!isfinite(gyro_dps)) return 0;
    float magnitude = fabsf(gyro_dps);
    float value = 100.0f * gyro_dps / (magnitude + 160.0f);
    if (value < -100.0f) value = -100.0f;
    if (value > 100.0f) value = 100.0f;
    return (int16_t)lroundf(value);
}

size_t gesture_key_trace_snapshot(
    int16_t values[][GESTURE_KEY_TRACE_AXES], size_t capacity)
{
    if (!values || capacity == 0U) return 0U;
    if (capacity > GESTURE_KEY_TRACE_POINTS) capacity = GESTURE_KEY_TRACE_POINTS;

    float snapshot[GESTURE_KEY_TRACE_POINTS][GESTURE_KEY_TRACE_AXES];
    size_t output_count;
    taskENTER_CRITICAL(&s_trace_mux);
    size_t source_count = s_raw_count;
    output_count = source_count < capacity ? source_count : capacity;
    for (size_t point = 0; point < output_count; point++) {
        size_t source = point;
        if (source_count > output_count && output_count > 1U) {
            source = point * (source_count - 1U) / (output_count - 1U);
        }
        for (size_t axis = 0; axis < GESTURE_KEY_TRACE_AXES; axis++) {
            snapshot[point][axis] = s_raw_samples[source].gyro_dps[axis];
        }
    }
    taskEXIT_CRITICAL(&s_trace_mux);

    for (size_t point = 0; point < output_count; point++) {
        for (size_t axis = 0; axis < GESTURE_KEY_TRACE_AXES; axis++) {
            values[point][axis] = trace_display_value(snapshot[point][axis]);
        }
    }
    return output_count;
}

uint32_t gesture_key_trace_revision(void)
{
    return (uint32_t)atomic_load(&s_trace_revision);
}

int gesture_key_link_rssi(void)
{
    return atomic_load(&s_link_rssi);
}

uint32_t gesture_key_link_revision(void)
{
    return (uint32_t)atomic_load(&s_link_rssi_revision);
}

bool gesture_key_motion_active(void)
{
    gesture_key_state_t state = gesture_key_state();
    return state == GESTURE_KEY_LEARN_RECORDING ||
           state == GESTURE_KEY_VERIFY_RECORDING;
}
