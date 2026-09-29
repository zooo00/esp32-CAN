// BLE peripheral: one service, one notify characteristic carrying batched frames.
#include <stddef.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "node_a.h"

#define FLUSH_US 20000 // send a partial batch after 20 ms

static const char *TAG = "ble";

static const ble_uuid128_t s_svc_uuid = BLE_UUID128_INIT(GSA_SVC_UUID);
static const ble_uuid128_t s_chr_uuid = BLE_UUID128_INIT(GSA_CHR_UUID);

static QueueHandle_t s_q;
static uint8_t s_own_addr_type;
static uint16_t s_val_handle;
static volatile uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static volatile bool s_subscribed;

static int chr_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return BLE_ATT_ERR_UNLIKELY; // notify only
}

static const struct ble_gatt_svc_def s_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &s_chr_uuid.u,
                .access_cb = chr_access,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_val_handle,
            },
            {0},
        },
    },
    {0},
};

static int gap_event(struct ble_gap_event *e, void *arg);

static void advertise(void)
{
    struct ble_hs_adv_fields adv = {0};
    adv.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv.uuids128 = (ble_uuid128_t *)&s_svc_uuid;
    adv.num_uuids128 = 1;
    adv.uuids128_is_complete = 1;
    ESP_ERROR_CHECK(ble_gap_adv_set_fields(&adv));

    struct ble_hs_adv_fields rsp = {0};
    rsp.name = (uint8_t *)GSA_DEVICE_NAME;
    rsp.name_len = strlen(GSA_DEVICE_NAME);
    rsp.name_is_complete = 1;
    ESP_ERROR_CHECK(ble_gap_adv_rsp_set_fields(&rsp));

    struct ble_gap_adv_params p = {0};
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    int rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv start failed: %d", rc);
    }
}

static int gap_event(struct ble_gap_event *e, void *arg)
{
    switch (e->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (e->connect.status == 0) {
            s_conn = e->connect.conn_handle;
            ESP_LOGI(TAG, "connected");
        } else {
            advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected, reason=%d", e->disconnect.reason);
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        s_subscribed = false;
        advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (e->subscribe.attr_handle == s_val_handle) {
            s_subscribed = e->subscribe.cur_notify;
            ESP_LOGI(TAG, "notify %s", s_subscribed ? "on" : "off");
        }
        break;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "mtu=%d", e->mtu.value);
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;
    }
    return 0;
}

static void on_sync(void)
{
    ESP_ERROR_CHECK(ble_hs_util_ensure_addr(0));
    ESP_ERROR_CHECK(ble_hs_id_infer_auto(0, &s_own_addr_type));
    advertise();
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "host reset, reason=%d", reason);
}

static void host_task(void *arg)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// NimBLE can run out of buffers during bursts: wait a tick and retry before dropping the batch.
static void send_batch(const uint8_t *buf, size_t len)
{
    uint32_t n = (len - sizeof(gsa_hdr_t)) / sizeof(frame_t);
    for (int tries = 0; tries < 5; tries++) {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, len);
        int rc = om ? ble_gatts_notify_custom(s_conn, s_val_handle, om) : BLE_HS_ENOMEM;
        if (rc == 0) {
            stat_sent += n;
            return;
        }
        if (rc != BLE_HS_ENOMEM) {
            break;
        }
        vTaskDelay(1);
    }
    stat_drop_ble += n;
}

// Drains the source queue into notifications: flush when the next frame won't fit, after FLUSH_US,
// or when the source changes (a batch never mixes REAL and STUB frames).
static void sender_task(void *arg)
{
    static uint8_t buf[GSA_MAX_NOTIFY];
    size_t len = 0;
    int64_t started = 0;
    qitem_t it;

    for (;;) {
        bool got = xQueueReceive(s_q, &it, pdMS_TO_TICKS(5)) == pdTRUE;
        if (!s_subscribed) {
            len = 0; // nobody listening: discard
            continue;
        }

        size_t cap = ble_att_mtu(s_conn) - 3;
        if (cap > sizeof(buf)) {
            cap = sizeof(buf);
        }

        if (got && len > 0 && buf[offsetof(gsa_hdr_t, source)] != it.src) {
            send_batch(buf, len);
            len = 0;
        }

        if (got) {
            if (len == 0) {
                gsa_hdr_t h = {.version = GSA_PROTO_VERSION, .source = it.src};
                memcpy(buf, &h, sizeof(h));
                len = sizeof(h);
                started = esp_timer_get_time();
            }
            memcpy(buf + len, &it.f, sizeof(it.f));
            len += sizeof(it.f);
        }

        if (len > sizeof(gsa_hdr_t) &&
            (len + sizeof(frame_t) > cap || esp_timer_get_time() - started >= FLUSH_US)) {
            send_batch(buf, len);
            len = 0;
        }
    }
}

void ble_server_start(QueueHandle_t q)
{
    s_q = q;

    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ESP_ERROR_CHECK(ble_gatts_count_cfg(s_svcs));
    ESP_ERROR_CHECK(ble_gatts_add_svcs(s_svcs));
    ESP_ERROR_CHECK(ble_svc_gap_device_name_set(GSA_DEVICE_NAME));

    nimble_port_freertos_init(host_task);
    xTaskCreate(sender_task, "ble_tx", 4096, NULL, 9, NULL);
}

bool ble_server_connected(void)
{
    return s_conn != BLE_HS_CONN_HANDLE_NONE;
}
