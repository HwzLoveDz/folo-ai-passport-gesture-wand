#pragma once

#include <stddef.h>
#include <stdint.h>

#define GESTURE_AXIS_COUNT       6U
#define GESTURE_POINT_COUNT     48U
#define GESTURE_MIN_SAMPLES     25U
#define GESTURE_MAX_SAMPLES    260U

typedef struct {
    float gyro_dps[3];
    float accel_g[3];
} gesture_sample_t;

typedef struct {
    float point[GESTURE_POINT_COUNT][GESTURE_AXIS_COUNT];
    float duration_ms;
} gesture_signature_t;

typedef enum {
    GESTURE_BUILD_OK = 0,
    GESTURE_BUILD_TOO_SHORT,
    GESTURE_BUILD_TOO_LONG,
    GESTURE_BUILD_TOO_STILL,
    GESTURE_BUILD_INVALID,
} gesture_build_result_t;

typedef enum {
    GESTURE_CLASSIFY_MATCH = 0,
    GESTURE_CLASSIFY_NO_MATCH,
    GESTURE_CLASSIFY_AMBIGUOUS,
} gesture_classify_result_t;

// 将不同速度、不同采样点数的六轴轨迹归一化为固定长度的姿态路径签名。
gesture_build_result_t gesture_signature_build(const gesture_sample_t *samples,
                                               size_t count,
                                               float sample_period_ms,
                                               gesture_signature_t *out);

// 返回 0..100。时间伸缩由带约束 DTW 吸收，75 分作为设备端默认通过线。
float gesture_similarity(const gesture_signature_t *a,
                         const gesture_signature_t *b);

// 在有效模型中选出最佳匹配。若前两名都足够高且分差不足，返回歧义，
// 但仍通过索引和分数输出两名候选，供界面解释拒绝原因。
gesture_classify_result_t gesture_signature_classify(
    const gesture_signature_t *candidate,
    const gesture_signature_t *models,
    uint32_t valid_mask,
    size_t model_count,
    float minimum_score,
    float ambiguity_floor,
    float ambiguity_margin,
    size_t *best_index,
    size_t *second_index,
    float *best_score,
    float *second_score);

void gesture_signature_average(const gesture_signature_t *items,
                               size_t count,
                               gesture_signature_t *out);

// 三次录入的一致性检查：-1=三次一致，-2=没有任意两次相似，0..2=异常样本索引。
int gesture_enrollment_outlier(const gesture_signature_t items[3],
                               float minimum_pair_score,
                               float pair_scores[3]);
