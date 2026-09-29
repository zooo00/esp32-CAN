// Fake frames so the A -> B link can be tested at the desk. Switched on/off at runtime
// ("stub on|off" on the USB console), off after every boot. While on, real CAN frames are
// discarded and everything is sent with source = STUB.
// IDs and contents are made up; they are NOT BMW signals.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "node_a.h"

static const char *TAG = "stub";

static volatile bool s_enabled;

static const struct {
    uint32_t id;
    uint32_t period_ms; // multiple of 10
} k_ids[] = {
    {0x100, 10},
    {0x200, 20},
    {0x300, 100},
    {0x3F0, 1000},
};

static void stub_task(void *arg)
{
    QueueHandle_t q = arg;
    uint16_t counter[sizeof(k_ids) / sizeof(k_ids[0])] = {0};
    uint32_t tick = 0;
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(10));
        if (!s_enabled) {
            continue;
        }
        tick++;
        for (size_t i = 0; i < sizeof(k_ids) / sizeof(k_ids[0]); i++) {
            if (tick % (k_ids[i].period_ms / 10) != 0) {
                continue;
            }
            uint16_t c = counter[i]++;
            uint16_t ramp = (tick * 7) % 8000; // slowly rising, wraps every ~11 s
            frame_t f = {
                .ts_ms = (uint32_t)(esp_timer_get_time() / 1000),
                .id = k_ids[i].id,
                .dlc = 8,
                .d = {c & 0xFF, c >> 8, ramp & 0xFF, ramp >> 8, 0x00, 0x00, 0x00, esp_random() & 0xFF},
            };
            source_push(q, &f, GSA_SRC_STUB);
        }
    }
}

void stub_source_start(QueueHandle_t q)
{
    xTaskCreate(stub_task, "stub", 3072, q, 10, NULL);
}

void stub_set_enabled(bool on)
{
    s_enabled = on;
    ESP_LOGW(TAG, "stub %s", on ? "ON: sending FAKE frames, real CAN frames are discarded" : "off: sending real CAN frames");
}

bool stub_enabled(void)
{
    return s_enabled;
}
