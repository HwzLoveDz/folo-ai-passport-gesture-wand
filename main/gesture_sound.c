#include "gesture_sound.h"

#include <stdint.h>
#include <string.h>

#include "bsp_audio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define SOUND_SAMPLE_RATE       16000U
#define SOUND_CHUNK_SAMPLES       128U
#define SOUND_AMPLITUDE           5200
#define SOUND_VOLUME                90U

typedef struct {
    uint16_t frequency_hz;
    uint16_t duration_ms;
    uint16_t gap_ms;
} tone_step_t;

typedef struct {
    const tone_step_t *steps;
    uint8_t count;
} tone_pattern_t;

static const tone_step_t s_startup[] = {{520, 55, 25}, {760, 75, 0}};
static const tone_step_t s_connected[] = {{720, 55, 25}, {1040, 90, 0}};
static const tone_step_t s_disconnected[] = {{560, 70, 20}, {330, 110, 0}};
static const tone_step_t s_calibrated[] = {{980, 80, 0}};
static const tone_step_t s_record_start[] = {{1320, 48, 0}};
static const tone_step_t s_record_stop[] = {{820, 42, 0}};
static const tone_step_t s_sample_ok[] = {{880, 45, 18}, {1100, 60, 0}};
static const tone_step_t s_retry[] = {{470, 90, 25}, {310, 140, 0}};
static const tone_step_t s_saved[] = {{620, 60, 22}, {850, 65, 22}, {1160, 110, 0}};
static const tone_step_t s_match[] = {{900, 55, 18}, {1220, 65, 18}, {1540, 120, 0}};
static const tone_step_t s_lock[] = {{1180, 52, 18}, {760, 58, 18}, {420, 100, 0}};
static const tone_step_t s_reject[] = {{280, 120, 25}, {210, 150, 0}};
static const tone_step_t s_reset[] = {{820, 55, 20}, {580, 60, 20}, {820, 80, 0}};
static const tone_step_t s_menu_open[] = {{540, 35, 15}, {820, 55, 0}};
static const tone_step_t s_menu_move[] = {{680, 28, 0}};
static const tone_step_t s_menu_select[] = {{780, 35, 15}, {1080, 55, 0}};
static const tone_step_t s_menu_back[] = {{720, 35, 15}, {480, 50, 0}};
static const tone_step_t s_pin_move[] = {{760, 32, 0}};
static const tone_step_t s_pin_confirm[] = {{920, 35, 12}, {1120, 48, 0}};
static const tone_step_t s_pin_accepted[] = {
    {720, 45, 16}, {1020, 55, 16}, {1420, 95, 0},
};
static const tone_step_t s_pin_rejected[] = {
    {360, 70, 22}, {260, 95, 22}, {180, 130, 0},
};
static const tone_step_t s_error[] = {{190, 110, 45}, {190, 110, 45}, {190, 160, 0}};

static const tone_pattern_t s_patterns[] = {
    [GESTURE_SOUND_STARTUP] = {s_startup, 2},
    [GESTURE_SOUND_CONNECTED] = {s_connected, 2},
    [GESTURE_SOUND_DISCONNECTED] = {s_disconnected, 2},
    [GESTURE_SOUND_CALIBRATED] = {s_calibrated, 1},
    [GESTURE_SOUND_RECORD_START] = {s_record_start, 1},
    [GESTURE_SOUND_RECORD_STOP] = {s_record_stop, 1},
    [GESTURE_SOUND_SAMPLE_OK] = {s_sample_ok, 2},
    [GESTURE_SOUND_RETRY] = {s_retry, 2},
    [GESTURE_SOUND_SAVED] = {s_saved, 3},
    [GESTURE_SOUND_MATCH] = {s_match, 3},
    [GESTURE_SOUND_LOCK] = {s_lock, 3},
    [GESTURE_SOUND_REJECT] = {s_reject, 2},
    [GESTURE_SOUND_RESET] = {s_reset, 3},
    [GESTURE_SOUND_MENU_OPEN] = {s_menu_open, 2},
    [GESTURE_SOUND_MENU_MOVE] = {s_menu_move, 1},
    [GESTURE_SOUND_MENU_SELECT] = {s_menu_select, 2},
    [GESTURE_SOUND_MENU_BACK] = {s_menu_back, 2},
    [GESTURE_SOUND_PIN_MOVE] = {s_pin_move, 1},
    [GESTURE_SOUND_PIN_CONFIRM] = {s_pin_confirm, 2},
    [GESTURE_SOUND_PIN_ACCEPTED] = {s_pin_accepted, 3},
    [GESTURE_SOUND_PIN_REJECTED] = {s_pin_rejected, 3},
    [GESTURE_SOUND_ERROR] = {s_error, 3},
};

static QueueHandle_t s_queue;

static int16_t triangle_sample(uint32_t phase, int amplitude)
{
    uint32_t folded = phase < 32768U ? phase : 65535U - phase;
    int32_t sample = (int32_t)(folded * (uint32_t)(amplitude * 4) / 65535U) -
                     amplitude;
    return (int16_t)sample;
}

static void play_silence(uint16_t duration_ms)
{
    int16_t pcm[SOUND_CHUNK_SAMPLES] = {0};
    uint32_t remaining = (uint32_t)duration_ms * SOUND_SAMPLE_RATE / 1000U;
    while (remaining > 0U) {
        uint32_t count = remaining < SOUND_CHUNK_SAMPLES ? remaining : SOUND_CHUNK_SAMPLES;
        (void)bsp_audio_write(pcm, count * sizeof(pcm[0]));
        remaining -= count;
    }
}

static void play_tone(const tone_step_t *step)
{
    int16_t pcm[SOUND_CHUNK_SAMPLES];
    uint32_t phase = 0;
    uint32_t total = (uint32_t)step->duration_ms * SOUND_SAMPLE_RATE / 1000U;
    uint32_t written = 0;
    uint32_t phase_step = (uint32_t)step->frequency_hz * 65536U / SOUND_SAMPLE_RATE;

    while (written < total) {
        uint32_t count = total - written;
        if (count > SOUND_CHUNK_SAMPLES) count = SOUND_CHUNK_SAMPLES;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t position = written + i;
            uint32_t edge = total / 6U;
            int amplitude = SOUND_AMPLITUDE;
            if (edge > 0U && position < edge) {
                amplitude = SOUND_AMPLITUDE * (int)position / (int)edge;
            } else if (edge > 0U && position + edge > total) {
                amplitude = SOUND_AMPLITUDE * (int)(total - position) / (int)edge;
            }
            phase = (phase + phase_step) & 0xFFFFU;
            pcm[i] = triangle_sample(phase, amplitude);
        }
        if (bsp_audio_write(pcm, count * sizeof(pcm[0])) != ESP_OK) return;
        written += count;
    }
    if (step->gap_ms > 0U) play_silence(step->gap_ms);
}

static void sound_task(void *arg)
{
    (void)arg;
    gesture_sound_cue_t cue;
    while (true) {
        if (xQueueReceive(s_queue, &cue, portMAX_DELAY) != pdTRUE) continue;
        if ((unsigned)cue >= sizeof(s_patterns) / sizeof(s_patterns[0])) continue;
        const tone_pattern_t *pattern = &s_patterns[cue];
        for (uint8_t i = 0; i < pattern->count; i++) play_tone(&pattern->steps[i]);
    }
}

bool gesture_sound_start(void)
{
    if (s_queue) return true;
    if (bsp_audio_init() != ESP_OK ||
        bsp_audio_set_format(SOUND_SAMPLE_RATE, 16, 1) != ESP_OK) {
        return false;
    }
    bsp_audio_set_volume(SOUND_VOLUME);
    s_queue = xQueueCreate(12, sizeof(gesture_sound_cue_t));
    if (!s_queue) return false;
    if (xTaskCreate(sound_task, "gesture_sound", 3072, NULL, 3, NULL) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return false;
    }
    gesture_sound_play(GESTURE_SOUND_STARTUP);
    return true;
}

void gesture_sound_play(gesture_sound_cue_t cue)
{
    if (!s_queue) return;
    (void)xQueueSend(s_queue, &cue, 0);
}
