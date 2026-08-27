#pragma once

#include <stdbool.h>

typedef enum {
    GESTURE_SOUND_STARTUP = 0,
    GESTURE_SOUND_CONNECTED,
    GESTURE_SOUND_DISCONNECTED,
    GESTURE_SOUND_CALIBRATED,
    GESTURE_SOUND_RECORD_START,
    GESTURE_SOUND_RECORD_STOP,
    GESTURE_SOUND_SAMPLE_OK,
    GESTURE_SOUND_RETRY,
    GESTURE_SOUND_SAVED,
    GESTURE_SOUND_MATCH,
    GESTURE_SOUND_REJECT,
    GESTURE_SOUND_RESET,
    GESTURE_SOUND_ERROR,
} gesture_sound_cue_t;

// 独立音频任务；任何状态机/按键回调都只入队，不会阻塞 IMU 采样。
bool gesture_sound_start(void);
void gesture_sound_play(gesture_sound_cue_t cue);
