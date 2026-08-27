#include "gesture_model.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#define GESTURE_DTW_BAND 8
#define GESTURE_INFINITY 1.0e30f

static float min3(float a, float b, float c)
{
    float value = a < b ? a : b;
    return value < c ? value : c;
}

static float clampf(float value, float low, float high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static void angular_path_at(const gesture_sample_t *samples, size_t count,
                            float position, float dt_s, float angle[3])
{
    memset(angle, 0, sizeof(float) * 3U);
    size_t lower = (size_t)position;
    if (lower >= count) lower = count - 1U;

    for (size_t i = 1; i <= lower; i++) {
        for (size_t axis = 0; axis < 3U; axis++) {
            angle[axis] += 0.5f *
                (samples[i - 1U].gyro_dps[axis] + samples[i].gyro_dps[axis]) * dt_s;
        }
    }

    if (lower + 1U < count) {
        float fraction = position - (float)lower;
        for (size_t axis = 0; axis < 3U; axis++) {
            angle[axis] += fraction * 0.5f *
                (samples[lower].gyro_dps[axis] + samples[lower + 1U].gyro_dps[axis]) * dt_s;
        }
    }
}

static float vector_magnitude(const float values[3])
{
    return sqrtf(values[0] * values[0] + values[1] * values[1] +
                 values[2] * values[2]);
}

gesture_build_result_t gesture_signature_build(const gesture_sample_t *samples,
                                               size_t count,
                                               float sample_period_ms,
                                               gesture_signature_t *out)
{
    if (!samples || !out || !isfinite(sample_period_ms) || sample_period_ms <= 0.0f) {
        return GESTURE_BUILD_INVALID;
    }
    if (count < GESTURE_MIN_SAMPLES) return GESTURE_BUILD_TOO_SHORT;
    if (count > GESTURE_MAX_SAMPLES) return GESTURE_BUILD_TOO_LONG;

    memset(out, 0, sizeof(*out));
    float accel_origin[3] = {0};
    size_t origin_count = count < 5U ? count : 5U;
    for (size_t i = 0; i < origin_count; i++) {
        for (size_t axis = 0; axis < 3U; axis++) {
            if (!isfinite(samples[i].gyro_dps[axis]) ||
                !isfinite(samples[i].accel_g[axis])) {
                return GESTURE_BUILD_INVALID;
            }
            accel_origin[axis] += samples[i].accel_g[axis];
        }
    }
    for (size_t axis = 0; axis < 3U; axis++) accel_origin[axis] /= origin_count;

    const float dt_s = sample_period_ms / 1000.0f;
    float max_angle = 0.0f;
    float max_accel = 0.0f;
    for (size_t point = 0; point < GESTURE_POINT_COUNT; point++) {
        float position = (float)point * (float)(count - 1U) /
                         (float)(GESTURE_POINT_COUNT - 1U);
        size_t lower = (size_t)position;
        if (lower >= count - 1U) lower = count - 1U;
        size_t upper = lower + 1U < count ? lower + 1U : lower;
        float fraction = position - (float)lower;

        float angle[3];
        angular_path_at(samples, count, position, dt_s, angle);
        for (size_t axis = 0; axis < 3U; axis++) {
            float accel = samples[lower].accel_g[axis] +
                fraction * (samples[upper].accel_g[axis] - samples[lower].accel_g[axis]);
            out->point[point][axis] = angle[axis];
            out->point[point][axis + 3U] = accel - accel_origin[axis];
        }
        float angle_magnitude = vector_magnitude(out->point[point]);
        float accel_magnitude = vector_magnitude(&out->point[point][3]);
        if (angle_magnitude > max_angle) max_angle = angle_magnitude;
        if (accel_magnitude > max_accel) max_accel = accel_magnitude;
    }

    // 至少需要明显转动或平移，避免静止噪声被归一化后看起来像有效手势。
    if (max_angle < 12.0f && max_accel < 0.12f) return GESTURE_BUILD_TOO_STILL;

    // 以起点为零，并轻度平滑固定长度路径。
    float smoothed[GESTURE_POINT_COUNT][GESTURE_AXIS_COUNT];
    for (size_t point = 0; point < GESTURE_POINT_COUNT; point++) {
        for (size_t axis = 0; axis < GESTURE_AXIS_COUNT; axis++) {
            float start = out->point[0][axis];
            size_t before = point > 0U ? point - 1U : point;
            size_t after = point + 1U < GESTURE_POINT_COUNT ? point + 1U : point;
            smoothed[point][axis] =
                (out->point[before][axis] + 2.0f * out->point[point][axis] +
                 out->point[after][axis]) * 0.25f - start;
        }
    }
    memcpy(out->point, smoothed, sizeof(smoothed));

    float angle_energy = 0.0f;
    float accel_energy = 0.0f;
    for (size_t point = 0; point < GESTURE_POINT_COUNT; point++) {
        for (size_t axis = 0; axis < 3U; axis++) {
            angle_energy += out->point[point][axis] * out->point[point][axis];
            accel_energy += out->point[point][axis + 3U] *
                            out->point[point][axis + 3U];
        }
    }
    float angle_rms = sqrtf(angle_energy / (float)GESTURE_POINT_COUNT);
    float accel_rms = sqrtf(accel_energy / (float)GESTURE_POINT_COUNT);
    float angle_scale = 1.0f / fmaxf(angle_rms, 8.0f);
    float accel_scale = 0.40f / fmaxf(accel_rms, 0.08f);

    for (size_t point = 0; point < GESTURE_POINT_COUNT; point++) {
        for (size_t axis = 0; axis < 3U; axis++) {
            out->point[point][axis] *= angle_scale;
            out->point[point][axis + 3U] *= accel_scale;
        }
    }
    out->duration_ms = (float)(count - 1U) * sample_period_ms;
    return GESTURE_BUILD_OK;
}

static float point_distance(const float a[GESTURE_AXIS_COUNT],
                            const float b[GESTURE_AXIS_COUNT])
{
    float sum = 0.0f;
    for (size_t axis = 0; axis < GESTURE_AXIS_COUNT; axis++) {
        float delta = a[axis] - b[axis];
        sum += delta * delta;
    }
    return sum / (float)GESTURE_AXIS_COUNT;
}

float gesture_similarity(const gesture_signature_t *a,
                         const gesture_signature_t *b)
{
    if (!a || !b || a->duration_ms <= 0.0f || b->duration_ms <= 0.0f) return 0.0f;

    float previous[GESTURE_POINT_COUNT + 1U];
    float current[GESTURE_POINT_COUNT + 1U];
    for (size_t i = 0; i <= GESTURE_POINT_COUNT; i++) previous[i] = GESTURE_INFINITY;
    previous[0] = 0.0f;

    for (size_t i = 1; i <= GESTURE_POINT_COUNT; i++) {
        for (size_t j = 0; j <= GESTURE_POINT_COUNT; j++) current[j] = GESTURE_INFINITY;
        size_t first = i > GESTURE_DTW_BAND ? i - GESTURE_DTW_BAND : 1U;
        size_t last = i + GESTURE_DTW_BAND;
        if (last > GESTURE_POINT_COUNT) last = GESTURE_POINT_COUNT;
        for (size_t j = first; j <= last; j++) {
            float cost = point_distance(a->point[i - 1U], b->point[j - 1U]);
            current[j] = cost + min3(previous[j], current[j - 1U], previous[j - 1U]);
        }
        memcpy(previous, current, sizeof(previous));
    }

    float rmse = sqrtf(previous[GESTURE_POINT_COUNT] / (float)GESTURE_POINT_COUNT);
    float shape_score = 100.0f * expf(-1.35f * rmse);
    float duration_ratio = a->duration_ms / b->duration_ms;
    float duration_score = expf(-0.45f * fabsf(logf(duration_ratio)));
    float score = shape_score * (0.92f + 0.08f * duration_score);
    return clampf(score, 0.0f, 100.0f);
}

void gesture_signature_average(const gesture_signature_t *items,
                               size_t count,
                               gesture_signature_t *out)
{
    if (!items || !out || count == 0U) return;
    memset(out, 0, sizeof(*out));
    for (size_t item = 0; item < count; item++) {
        out->duration_ms += items[item].duration_ms;
        for (size_t point = 0; point < GESTURE_POINT_COUNT; point++) {
            for (size_t axis = 0; axis < GESTURE_AXIS_COUNT; axis++) {
                out->point[point][axis] += items[item].point[point][axis];
            }
        }
    }
    float scale = 1.0f / (float)count;
    out->duration_ms *= scale;
    for (size_t point = 0; point < GESTURE_POINT_COUNT; point++) {
        for (size_t axis = 0; axis < GESTURE_AXIS_COUNT; axis++) {
            out->point[point][axis] *= scale;
        }
    }
}

int gesture_enrollment_outlier(const gesture_signature_t items[3],
                               float minimum_pair_score,
                               float pair_scores[3])
{
    if (!items) return -2;
    float local_scores[3] = {
        gesture_similarity(&items[0], &items[1]),
        gesture_similarity(&items[0], &items[2]),
        gesture_similarity(&items[1], &items[2]),
    };
    if (pair_scores) memcpy(pair_scores, local_scores, sizeof(local_scores));

    if (local_scores[0] >= minimum_pair_score &&
        local_scores[1] >= minimum_pair_score &&
        local_scores[2] >= minimum_pair_score) {
        return -1;
    }

    size_t best_pair = 0U;
    if (local_scores[1] > local_scores[best_pair]) best_pair = 1U;
    if (local_scores[2] > local_scores[best_pair]) best_pair = 2U;
    if (local_scores[best_pair] < minimum_pair_score) return -2;

    static const int outlier_for_pair[3] = {2, 1, 0};
    return outlier_for_pair[best_pair];
}
