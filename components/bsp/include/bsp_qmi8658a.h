// QST QMI8658A 六轴 IMU。复用 BSP 共享 I2C 总线,自动探测 0x6A/0x6B。
#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef struct {
    float accel_g[3];       // X/Y/Z,单位 g;当前量程 +/-4 g
    float gyro_dps[3];      // X/Y/Z,单位 degree/s;当前量程 +/-512 dps
} bsp_qmi8658a_sample_t;

// 初始化为 accel +/-4 g、gyro +/-512 dps、6-DoF ODR 224.2 Hz。
// WHO_AM_I 不是 0x05 或两个地址都不应答时返回 ESP_ERR_NOT_FOUND。
esp_err_t bsp_qmi8658a_init(void);

// 读取一帧经时间戳一致性筛查、已换算的六轴数据。尚无新数据时返回 ESP_ERR_NOT_FINISHED。
esp_err_t bsp_qmi8658a_read(bsp_qmi8658a_sample_t *sample);

// 返回探测到的 7-bit I2C 地址;尚未初始化时返回 0。
uint8_t bsp_qmi8658a_address(void);
