// Tone playback through the board's ES8311 codec and speaker amp.
// Tones are synthesized as 16 kHz mono PCM in a low-priority task fed by a
// queue, so the caller never blocks.
#include "alarms.h"

#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_check.h"
#include "hal_board.h"

static const char *TAG = "audio";

#define SR        16000
#define CHUNK     512          // samples per write

typedef struct { uint16_t f0, f1; uint16_t at_ms, dur_ms; bool square; } note_t;
typedef struct { const note_t *notes; uint8_t n; } tone_def_t;

static const note_t N_CHIRP[]  = { {1200, 1200, 0, 80, false} };
static const note_t N_DOUBLE[] = { {1000, 1000, 0, 70, false}, {1000, 1000, 160, 70, false} };
static const note_t N_BUZZ[]   = { {180, 180, 0, 260, true} };
static const note_t N_RISING[] = { {600, 1500, 0, 260, false} };
static const note_t N_SIREN[]  = { {900, 900, 0, 120, false}, {1300, 1300, 130, 120, false}, {900, 900, 260, 120, false}, {1300, 1300, 390, 120, false} };
static const tone_def_t TONES[TONE_COUNT] = {
    { NULL, 0 }, { N_CHIRP, 1 }, { N_DOUBLE, 2 }, { N_BUZZ, 1 }, { N_RISING, 1 }, { N_SIREN, 4 },
};

typedef struct { alarm_tone_t tone; uint8_t volume; } req_t;
static QueueHandle_t s_q;
static bool s_ready;

static void render_tone(alarm_tone_t tone, uint8_t volume)
{
    const tone_def_t *t = &TONES[tone];
    uint32_t total_ms = 0;
    for (int i = 0; i < t->n; i++) if (t->notes[i].at_ms + t->notes[i].dur_ms > total_ms) total_ms = t->notes[i].at_ms + t->notes[i].dur_ms;
    uint32_t total = total_ms * SR / 1000 + SR / 20;   // + 50 ms tail
    float amp = 0.5f * (volume / 100.0f) * (volume / 100.0f) * 32767.0f;
    int16_t buf[CHUNK];
    static float phase[8];
    memset(phase, 0, sizeof phase);

    hal_board_audio_write_begin();
    for (uint32_t s0 = 0; s0 < total; s0 += CHUNK) {
        uint32_t n = total - s0 < CHUNK ? total - s0 : CHUNK;
        for (uint32_t k = 0; k < n; k++) {
            uint32_t ms = (s0 + k) * 1000 / SR;
            float v = 0;
            for (int i = 0; i < t->n && i < 8; i++) {
                const note_t *nt = &t->notes[i];
                if (ms < nt->at_ms || ms >= nt->at_ms + nt->dur_ms) continue;
                float pos = (float)(ms - nt->at_ms) / nt->dur_ms;
                float f = nt->f0 + (nt->f1 - nt->f0) * pos;
                float env = pos < 0.1f ? pos * 10.0f : pos > 0.85f ? (1.0f - pos) / 0.15f : 1.0f;
                phase[i] += 2.0f * (float)M_PI * f / SR;
                if (phase[i] > 2.0f * (float)M_PI) phase[i] -= 2.0f * (float)M_PI;
                float w = nt->square ? (phase[i] < (float)M_PI ? 0.6f : -0.6f) : sinf(phase[i]);
                v += w * env;
            }
            if (v > 1) v = 1;
            if (v < -1) v = -1;
            buf[k] = (int16_t)(v * amp);
        }
        hal_board_audio_write(buf, n * sizeof(int16_t));
    }
    hal_board_audio_write_end();
}

static void audio_task(void *arg)
{
    (void)arg;
    req_t r;
    for (;;) {
        if (xQueueReceive(s_q, &r, portMAX_DELAY) == pdTRUE && r.tone != TONE_OFF && r.tone < TONE_COUNT) {
            render_tone(r.tone, r.volume);
        }
    }
}

esp_err_t audio_init(void)
{
    ESP_RETURN_ON_ERROR(hal_board_audio_init(SR), TAG, "codec");
    s_q = xQueueCreate(4, sizeof(req_t));
    ESP_RETURN_ON_FALSE(s_q, ESP_ERR_NO_MEM, TAG, "queue");
    ESP_RETURN_ON_FALSE(xTaskCreate(audio_task, "audio", 4096, NULL, 4, NULL) == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    s_ready = true;
    return ESP_OK;
}

void audio_play(alarm_tone_t tone, uint8_t volume)
{
    if (!s_ready || tone == TONE_OFF) return;
    req_t r = { tone, volume > 100 ? 100 : volume };
    xQueueSend(s_q, &r, 0);   // drop if the queue is full; a missed beep beats a backlog
}
