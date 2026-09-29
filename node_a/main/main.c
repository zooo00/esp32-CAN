#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "node_a.h"

static const char *TAG = "node_a";

volatile uint32_t stat_rx, stat_drop_src, stat_sent, stat_drop_ble;

void app_main(void)
{
    esp_err_t err = nvs_flash_init(); // needed by the BT controller
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    QueueHandle_t q = xQueueCreate(512, sizeof(qitem_t));
    ble_server_start(q);
    twai_source_start(q);
    stub_source_start(q); // off until "stub on" is typed on the USB console
    console_start();

    uint32_t last_rx = 0, last_sent = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint32_t rx = stat_rx, sent = stat_sent;
        ESP_LOGI(TAG, "source=%s rx=%" PRIu32 "/s sent=%" PRIu32 "/s drop_src=%" PRIu32 " drop_ble=%" PRIu32 " ble=%s",
                 stub_enabled() ? "STUB" : "REAL", rx - last_rx, sent - last_sent, stat_drop_src, stat_drop_ble,
                 ble_server_connected() ? "connected" : "advertising");
        last_rx = rx;
        last_sent = sent;
        twai_source_log_status();
    }
}
