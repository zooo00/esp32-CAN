#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "gsa_proto.h"

// Queue item: a frame plus where it came from (GSA_SRC_*)
typedef struct {
    frame_t f;
    uint8_t src;
} qitem_t;

// Real bike: TWAI listen-only, always running (source_twai.c)
void twai_source_start(QueueHandle_t q);
void twai_source_log_status(void);

// Fake frames for desk testing, off at every boot (source_stub.c)
void stub_source_start(QueueHandle_t q);
void stub_set_enabled(bool on);
bool stub_enabled(void);

// USB console with the "stub on|off" command (console.c)
void console_start(void);

// BLE server: drains the queue and sends batched notifications
void ble_server_start(QueueHandle_t q);
bool ble_server_connected(void);

// Counters, printed once per second by main.c
extern volatile uint32_t stat_rx;       // frames queued for sending
extern volatile uint32_t stat_drop_src; // queue full
extern volatile uint32_t stat_sent;     // frames sent over BLE
extern volatile uint32_t stat_drop_ble; // notify failed

static inline void source_push(QueueHandle_t q, const frame_t *f, uint8_t src)
{
    qitem_t it = {.f = *f, .src = src};
    stat_rx++;
    if (xQueueSend(q, &it, 0) != pdTRUE) {
        stat_drop_src++;
    }
}
