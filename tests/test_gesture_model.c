#include "gesture_model.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static uint32_t s_rng = 1U;

static float noise(float amplitude)
{
    s_rng = s_rng * 1664525U + 1013904223U;
    return amplitude * ((float)((s_rng >> 8) & 0xFFFFU) / 32767.5f - 1.0f);
}

static void make_trace(gesture_sample_t *samples, size_t count, float duration_s,
                       float warp, float noise_level, bool wrong)
{
    memset(samples, 0, count * sizeof(*samples));
    float dt = duration_s / (float)(count - 1U);
    for (size_t i = 0; i < count; i++) {
        float t = (float)i / (float)(count - 1U);
        float u = powf(t, warp);
        float du_dt = i == 0U ? 0.0f :
            warp * powf(fmaxf(t, 0.0001f), warp - 1.0f) / duration_s;

        // 一条先右转、再上挑、最后回收的三维姿态轨迹。
        float dax_du = 55.0f * (float)M_PI * cosf((float)M_PI * u);
        float day_du = 36.0f * 2.0f * (float)M_PI * cosf(2.0f * (float)M_PI * u);
        float daz_du = 22.0f * (float)M_PI * sinf((float)M_PI * u);
        if (wrong) {
            dax_du = -0.25f * dax_du;
            day_du = 70.0f * (float)M_PI * sinf(3.0f * (float)M_PI * u);
            daz_du = -daz_du;
        }
        samples[i].gyro_dps[0] = dax_du * du_dt + noise(noise_level);
        samples[i].gyro_dps[1] = day_du * du_dt + noise(noise_level);
        samples[i].gyro_dps[2] = daz_du * du_dt + noise(noise_level);

        samples[i].accel_g[0] = (wrong ? -0.28f : 0.28f) * sinf((float)M_PI * u) +
                                noise(noise_level * 0.002f);
        samples[i].accel_g[1] = 0.20f * sinf(2.0f * (float)M_PI * u) +
                                noise(noise_level * 0.002f);
        samples[i].accel_g[2] = 1.0f - 0.12f * sinf((float)M_PI * u) +
                                noise(noise_level * 0.002f);
    }
    (void)dt;
}

static gesture_signature_t build_trace(size_t count, float duration_s, float warp,
                                       float noise_level, bool wrong)
{
    gesture_sample_t samples[GESTURE_MAX_SAMPLES];
    make_trace(samples, count, duration_s, warp, noise_level, wrong);
    gesture_signature_t signature;
    gesture_build_result_t result = gesture_signature_build(
        samples, count, duration_s * 1000.0f / (float)(count - 1U), &signature);
    assert(result == GESTURE_BUILD_OK);
    return signature;
}

static gesture_signature_t build_scaled_trace(float scale)
{
    gesture_sample_t samples[120];
    make_trace(samples, 120U, 1.20f, 1.0f, 0.5f, false);
    for (size_t i = 0; i < 120U; i++) {
        for (size_t axis = 0; axis < 3U; axis++) samples[i].gyro_dps[axis] *= scale;
        samples[i].accel_g[0] *= scale;
        samples[i].accel_g[1] *= scale;
        samples[i].accel_g[2] = 1.0f + (samples[i].accel_g[2] - 1.0f) * scale;
    }
    gesture_signature_t signature;
    assert(gesture_signature_build(samples, 120U, 1200.0f / 119.0f, &signature) ==
           GESTURE_BUILD_OK);
    return signature;
}

int main(void)
{
    gesture_signature_t reference = build_trace(120U, 1.20f, 1.0f, 0.0f, false);
    gesture_signature_t noisy = build_trace(104U, 1.05f, 1.0f, 1.5f, false);
    gesture_signature_t warped = build_trace(145U, 1.45f, 1.35f, 0.8f, false);
    gesture_signature_t softer = build_scaled_trace(0.65f);
    gesture_signature_t wrong = build_trace(120U, 1.20f, 1.0f, 0.5f, true);

    float same_score = gesture_similarity(&reference, &reference);
    float noisy_score = gesture_similarity(&reference, &noisy);
    float warped_score = gesture_similarity(&reference, &warped);
    float softer_score = gesture_similarity(&reference, &softer);
    float wrong_score = gesture_similarity(&reference, &wrong);
    printf("same=%.1f noisy=%.1f warped=%.1f softer=%.1f wrong=%.1f\n",
           same_score, noisy_score, warped_score, softer_score, wrong_score);
    assert(same_score > 99.0f);
    assert(noisy_score >= 75.0f);
    assert(warped_score >= 75.0f);
    assert(softer_score >= 75.0f);
    assert(wrong_score < 70.0f);

    gesture_sample_t still[60];
    memset(still, 0, sizeof(still));
    for (size_t i = 0; i < 60U; i++) still[i].accel_g[2] = 1.0f;
    gesture_signature_t unused;
    assert(gesture_signature_build(still, 60U, 10.0f, &unused) ==
           GESTURE_BUILD_TOO_STILL);
    assert(gesture_signature_build(still, 10U, 10.0f, &unused) ==
           GESTURE_BUILD_TOO_SHORT);

    gesture_signature_t enrollment[3] = {reference, noisy, wrong};
    float pair_scores[3];
    assert(gesture_enrollment_outlier(enrollment, 72.0f, pair_scores) == 2);
    enrollment[2] = warped;
    assert(gesture_enrollment_outlier(enrollment, 72.0f, pair_scores) == -1);

    gesture_signature_t average;
    gesture_signature_average(enrollment, 3U, &average);
    assert(gesture_similarity(&reference, &average) >= 75.0f);
    puts("gesture_model tests passed");
    return 0;
}
