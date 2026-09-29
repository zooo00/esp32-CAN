#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "gsa_proto.h"

// One received notification. len == 0 means "link lost".
typedef struct {
    uint16_t len;
    uint8_t data[GSA_MAX_NOTIFY];
} pkt_t;

// BLE central: finds Node A, subscribes, pushes every notification into q.
void ble_client_start(QueueHandle_t q);

extern volatile uint32_t stat_drop_pkt; // print queue full
