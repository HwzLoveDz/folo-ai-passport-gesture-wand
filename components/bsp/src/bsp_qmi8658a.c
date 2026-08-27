// QMI8658A 最小 6-DoF 驱动。
// 寄存器配置依据 QST QMI8658A Datasheet Rev D。
#include "bsp_qmi8658a.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>

static const char *TAG = "bsp_qmi8658a";

#define QMI_REG_WHO_AM_I       0x00
#define QMI_REG_CTRL1          0x02
#define QMI_REG_CTRL2          0x03
#define QMI_REG_CTRL3          0x04
#define QMI_REG_CTRL7          0x08
#define QMI_REG_TIMESTAMP_L    0x30
#define QMI_REG_AX_L           0x35
#define QMI_REG_RESET_RESULT   0x4D
#define QMI_REG_RESET          0x60

#define QMI_CHIP_ID            0x05
#define QMI_RESET_CMD          0xB0

// CTRL1:ADDR_AI=1、BE=0;CTRL2:+/-4g @ 224.2Hz;CTRL3:+/-512dps @ 224.2Hz。
#define QMI_CTRL1_CONFIG       0x40
#define QMI_CTRL2_CONFIG       0x15
#define QMI_CTRL3_CONFIG       0x55
#define QMI_CTRL7_DISABLE      0x00
#define QMI_CTRL7_6DOF         0x03

#define QMI_I2C_SPEED_HZ       400000
#define QMI_I2C_TIMEOUT_MS     5

static i2c_master_dev_handle_t s_dev;
static uint8_t s_address;
static uint32_t s_last_timestamp;
static bool s_have_timestamp;

static esp_err_t qmi_read(uint8_t reg, uint8_t *buf, size_t len) {
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, QMI_I2C_TIMEOUT_MS);
}

static esp_err_t qmi_write(uint8_t reg, uint8_t value) {
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t data[2] = { reg, value };
    return i2c_master_transmit(s_dev, data, sizeof(data), QMI_I2C_TIMEOUT_MS);
}

static int16_t qmi_le_i16(const uint8_t *p) {
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static esp_err_t qmi_read_timestamp(uint32_t *timestamp) {
    uint8_t raw[3];
    esp_err_t err = qmi_read(QMI_REG_TIMESTAMP_L, raw, sizeof(raw));
    if (err != ESP_OK) return err;
    *timestamp = (uint32_t)raw[0] | ((uint32_t)raw[1] << 8) | ((uint32_t)raw[2] << 16);
    return ESP_OK;
}

static esp_err_t qmi_wait_reset_result(uint8_t *result, int timeout_ms) {
    esp_err_t last_err = ESP_OK;
    *result = 0;
    for (int elapsed_ms = 0; elapsed_ms < timeout_ms; elapsed_ms += 10) {
        last_err = qmi_read(QMI_REG_RESET_RESULT, result, 1);
        if (last_err == ESP_OK && *result == 0x80) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return last_err == ESP_OK ? ESP_ERR_TIMEOUT : last_err;
}

static esp_err_t qmi_read_frame(uint8_t raw[12]) {
    // 无 INT 引脚时，用时间戳前后校验筛除大多数恰逢 ODR 更新的撕裂帧。
    for (int retry = 0; retry < 4; retry++) {
        uint32_t before = 0, after = 0;
        esp_err_t err = qmi_read_timestamp(&before);
        if (err != ESP_OK) return err;
        if (s_have_timestamp && before == s_last_timestamp) return ESP_ERR_NOT_FINISHED;

        err = qmi_read(QMI_REG_AX_L, raw, 12);
        if (err != ESP_OK) return err;
        err = qmi_read_timestamp(&after);
        if (err != ESP_OK) return err;
        if (before != after) continue;

        s_last_timestamp = after;
        s_have_timestamp = true;

        // 实机启动故障时观察到 gyro 三轴同时保持 0x8000；这是防御性判定，不是协议哨兵。
        if (qmi_le_i16(&raw[6]) == INT16_MIN &&
            qmi_le_i16(&raw[8]) == INT16_MIN &&
            qmi_le_i16(&raw[10]) == INT16_MIN) {
            return ESP_ERR_NOT_FINISHED;
        }
        return ESP_OK;
    }
    return ESP_ERR_NOT_FINISHED;
}

static esp_err_t qmi_remove_device(void) {
    esp_err_t err = ESP_OK;
    if (s_dev) {
        err = i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
    s_address = 0;
    s_have_timestamp = false;
    return err;
}

static void qmi_best_effort_reset(void) {
    if (!s_dev) return;

    // 关闭传感器后软复位，恢复寄存器默认值。
    (void)qmi_write(QMI_REG_CTRL7, QMI_CTRL7_DISABLE);
    if (qmi_write(QMI_REG_RESET, QMI_RESET_CMD) == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_have_timestamp = false;
}

static esp_err_t qmi_try_address(uint8_t address) {
    esp_err_t err = i2c_master_probe(bsp_i2c_bus(), address, QMI_I2C_TIMEOUT_MS);
    if (err != ESP_OK) return err;

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = QMI_I2C_SPEED_HZ,
    };
    err = i2c_master_bus_add_device(bsp_i2c_bus(), &cfg, &s_dev);
    if (err != ESP_OK) return err;

    uint8_t chip_id = 0;
    err = qmi_read(QMI_REG_WHO_AM_I, &chip_id, 1);
    if (err == ESP_OK && chip_id == QMI_CHIP_ID) {
        s_address = address;
        return ESP_OK;
    }

    if (err == ESP_OK) {
        ESP_LOGW(TAG, "0x%02X 应答但 WHO_AM_I=0x%02X,不是 QMI8658A", address, chip_id);
    }
    esp_err_t remove_err = qmi_remove_device();
    if (err != ESP_OK) return err;
    return remove_err == ESP_OK ? ESP_ERR_NOT_FOUND : remove_err;
}

esp_err_t bsp_qmi8658a_init(void) {
    if (s_dev) return ESP_OK;

    esp_err_t err = bsp_i2c_init();
    if (err != ESP_OK) return err;

    // 数据手册要求上电后至少等待 15ms 再访问。
    vTaskDelay(pdMS_TO_TICKS(20));

    const uint8_t addresses[] = {
        BSP_I2C_QMI8658A_ADDR_HIGH,
        BSP_I2C_QMI8658A_ADDR_LOW,
    };
    for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); i++) {
        err = qmi_try_address(addresses[i]);
        if (err == ESP_OK) break;
        if (err != ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "探测 QMI8658A @ 0x%02X 失败: %s", addresses[i], esp_err_to_name(err));
            return err;
        }
    }
    if (!s_dev) {
        ESP_LOGW(TAG, "QMI8658A 未找到 —— 预期地址 0x%02X/0x%02X;检查供电、CS、SA0 与总线上拉",
                 BSP_I2C_QMI8658A_ADDR_HIGH, BSP_I2C_QMI8658A_ADDR_LOW);
        return ESP_ERR_NOT_FOUND;
    }

    // RESET 寄存器写 0xB0;Rev A 数据手册正文另有一处 0x0B 的已知笔误。
    err = qmi_write(QMI_REG_RESET, QMI_RESET_CMD);
    if (err != ESP_OK) goto fail;
    // 至少等规格要求的 15ms，再采用旧驱动的做法轮询完整复位结果。
    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t reset_result = 0;
    err = qmi_wait_reset_result(&reset_result, 500);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "复位未完成: RESET_RESULT=0x%02X (%s)", reset_result, esp_err_to_name(err));
        goto fail;
    }

    uint8_t chip_id = 0;
    err = qmi_read(QMI_REG_WHO_AM_I, &chip_id, 1);
    if (err != ESP_OK) goto fail;
    if (chip_id != QMI_CHIP_ID) {
        err = ESP_ERR_NOT_FOUND;
        goto fail;
    }

    // 最小 Non-SyncSample 基线：地址自增、Little Endian、6DoF 224.2Hz。
    if ((err = qmi_write(QMI_REG_CTRL7, QMI_CTRL7_DISABLE)) != ESP_OK) goto fail;
    if ((err = qmi_write(QMI_REG_CTRL1, QMI_CTRL1_CONFIG)) != ESP_OK) goto fail;
    if ((err = qmi_write(QMI_REG_CTRL2, QMI_CTRL2_CONFIG)) != ESP_OK) goto fail;
    if ((err = qmi_write(QMI_REG_CTRL3, QMI_CTRL3_CONFIG)) != ESP_OK) goto fail;
    s_have_timestamp = false;

    if ((err = qmi_write(QMI_REG_CTRL7, QMI_CTRL7_6DOF)) != ESP_OK) goto fail;

    // 224.2Hz 下 gyro 典型启动时间约 164ms；留出余量后验证一帧即可。
    vTaskDelay(pdMS_TO_TICKS(250));
    uint8_t discard[12];
    for (int retry = 0; retry < 75; retry++) {
        err = qmi_read_frame(discard);
        if (err == ESP_OK) break;
        if (err != ESP_ERR_NOT_FINISHED) goto fail;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (err != ESP_OK) {
        err = ESP_ERR_TIMEOUT;
        goto fail;
    }
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "QMI8658A 初始化失败 @ 0x%02X: %s", s_address, esp_err_to_name(err));
    qmi_best_effort_reset();
    esp_err_t remove_err = qmi_remove_device();
    if (remove_err != ESP_OK) {
        ESP_LOGE(TAG, "移除 QMI8658A I2C 句柄失败: %s", esp_err_to_name(remove_err));
    }
    return err;
}

esp_err_t bsp_qmi8658a_read(bsp_qmi8658a_sample_t *sample) {
    if (!sample) return ESP_ERR_INVALID_ARG;
    if (!s_dev) return ESP_ERR_INVALID_STATE;

    uint8_t raw[12];
    esp_err_t err = qmi_read_frame(raw);
    if (err != ESP_OK) return err;

    for (int axis = 0; axis < 3; axis++) {
        sample->accel_g[axis] = qmi_le_i16(&raw[axis * 2]) / 8192.0f;
        sample->gyro_dps[axis] = qmi_le_i16(&raw[6 + axis * 2]) / 64.0f;
    }
    return ESP_OK;
}

uint8_t bsp_qmi8658a_address(void) {
    return s_address;
}
