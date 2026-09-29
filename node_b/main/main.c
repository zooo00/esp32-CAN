// Node B, Phase 1: prints received CAN frames on the USB console, one line per frame:
//   <ts_ms> <ID hex> [<dlc>] <bytes hex>
// Everything else (status, ESP_LOG output) starts with '#'.
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "node_b.h"

volatile uint32_t stat_drop_pkt;

// Keep ESP_LOG output off the frame stream: prefix every log line with '#'.
static int log_vprintf(const char *fmt, va_list ap)
{
    char line[256];
    vsnprintf(line, sizeof(line), fmt, ap);
    return printf("# %s", line);
}

static const char *source_name(uint8_t s)
{
    switch (s) {
    case GSA_SRC_REAL: return "REAL";
    case GSA_SRC_STUB: return "STUB";
    default: return "UNKNOWN";
    }
}

static void print_task(void *arg)
{
    QueueHandle_t q = arg;
    static pkt_t p;
    uint8_t last_src = 0;
    uint32_t last_drops = 0;
    char line[80];

    for (;;) {
        if (xQueueReceive(q, &p, pdMS_TO_TICKS(1000)) != pdTRUE) {
            continue;
        }
        if (p.len == 0) { // link lost: announce the source again on reconnect
            last_src = 0;
            continue;
        }

        gsa_hdr_t h;
        if (p.len < sizeof(h)) {
            continue;
        }
        memcpy(&h, p.data, sizeof(h));
        if (h.version != GSA_PROTO_VERSION) {
            printf("# bad protocol version %u\n", h.version);
            continue;
        }
        if (h.source != last_src) {
            printf("# source=%s\n", source_name(h.source));
            last_src = h.source;
        }
        if (stat_drop_pkt != last_drops) {
            last_drops = stat_drop_pkt;
            printf("# dropped_packets=%" PRIu32 "\n", last_drops);
        }

        for (size_t off = sizeof(h); off + sizeof(frame_t) <= p.len; off += sizeof(frame_t)) {
            frame_t f;
            memcpy(&f, p.data + off, sizeof(f));
            uint8_t dlc = f.dlc > 8 ? 8 : f.dlc;
            int n = snprintf(line, sizeof(line),
                             (f.id & GSA_ID_EXT) ? "%" PRIu32 " %08" PRIX32 " [%u]" : "%" PRIu32 " %03" PRIX32 " [%u]",
                             f.ts_ms, f.id & GSA_ID_MASK, f.dlc);
            for (uint8_t i = 0; i < dlc; i++) {
                n += snprintf(line + n, sizeof(line) - n, " %02X", f.d[i]);
            }
            puts(line);
        }
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init(); // needed by the BT controller
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    esp_log_set_vprintf(log_vprintf);
    printf("# node_b bridge up\n");

    QueueHandle_t q = xQueueCreate(64, sizeof(pkt_t));
    xTaskCreate(print_task, "print", 4096, q, 5, NULL);
    ble_client_start(q);
}
