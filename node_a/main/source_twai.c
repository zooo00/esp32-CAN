// Real CAN: TWAI in listen-only mode, always running. Never transmits, never ACKs.
#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "node_a.h"

// Safety guard: from here on, any use of a transmitting mode or of twai_transmit()
// in this file is a compile error. This is the only file in Node A that includes twai.h.
#pragma GCC poison TWAI_MODE_NORMAL TWAI_MODE_NO_ACK twai_transmit twai_transmit_v2

static const char *TAG = "twai";

static void rx_task(void *arg)
{
    QueueHandle_t q = arg;
    twai_message_t m;
    for (;;) {
        if (twai_receive(&m, portMAX_DELAY) != ESP_OK) {
            continue;
        }
        frame_t f = {
            .ts_ms = (uint32_t)(esp_timer_get_time() / 1000),
            .id = m.identifier | (m.extd ? GSA_ID_EXT : 0) | (m.rtr ? GSA_ID_RTR : 0),
            .dlc = m.data_length_code,
        };
        memcpy(f.d, m.data, sizeof(f.d));
        if (!stub_enabled()) {
            source_push(q, &f, GSA_SRC_REAL);
        }
    }
}

void twai_source_start(QueueHandle_t q)
{
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CONFIG_GSA_CAN_TX_GPIO, CONFIG_GSA_CAN_RX_GPIO,
                                                          TWAI_MODE_LISTEN_ONLY);
    g.rx_queue_len = 256;
    g.tx_queue_len = 0;
    twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    ESP_ERROR_CHECK(twai_driver_install(&g, &t, &f));
    ESP_ERROR_CHECK(twai_start());
    xTaskCreate(rx_task, "twai_rx", 4096, q, 10, NULL);
}

void twai_source_log_status(void)
{
    twai_status_info_t s;
    if (twai_get_status_info(&s) == ESP_OK) {
        ESP_LOGI(TAG, "state=%d bus_err=%" PRIu32 " rx_err_cnt=%" PRIu32 " rx_missed=%" PRIu32 " rx_overrun=%" PRIu32,
                 s.state, s.bus_error_count, s.rx_error_counter, s.rx_missed_count, s.rx_overrun_count);
    }
}
