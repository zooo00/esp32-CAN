// BLE central: scan for Node A's service UUID, connect, raise MTU, find the
// characteristic, enable notifications. Starts scanning again on any failure or disconnect.
#include <stdio.h>
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "node_b.h"

static const char *TAG = "ble";

static const ble_uuid128_t s_svc_uuid = BLE_UUID128_INIT(GSA_SVC_UUID);
static const ble_uuid128_t s_chr_uuid = BLE_UUID128_INIT(GSA_CHR_UUID);

static QueueHandle_t s_q;
static uint8_t s_own_addr_type;
static uint16_t s_svc_start, s_svc_end, s_val_handle;

static int gap_event(struct ble_gap_event *e, void *arg);

static void scan(void)
{
    struct ble_gap_disc_params p = {0};
    p.passive = 1;
    p.filter_duplicates = 1;
    int rc = ble_gap_disc(s_own_addr_type, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "scan start failed: %d", rc);
    }
}

static void fail(uint16_t conn, const char *what, int status)
{
    ESP_LOGE(TAG, "%s failed: %d", what, status);
    ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM); // DISCONNECT event restarts scanning
}

static int on_subscribed(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    if (err->status != 0) {
        fail(conn, "subscribe", err->status);
    } else {
        printf("# link up\n");
    }
    return 0;
}

static int on_chr(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_chr *chr, void *arg)
{
    if (err->status == 0) {
        s_val_handle = chr->val_handle;
    } else if (err->status == BLE_HS_EDONE && s_val_handle != 0) {
        // Node A's characteristic has only the CCCD, which NimBLE places right after the value.
        static const uint8_t enable[2] = {0x01, 0x00};
        int rc = ble_gattc_write_flat(conn, s_val_handle + 1, enable, sizeof(enable), on_subscribed, NULL);
        if (rc != 0) {
            fail(conn, "cccd write", rc);
        }
    } else {
        fail(conn, "characteristic discovery", err->status);
    }
    return 0;
}

static int on_svc(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_svc *svc, void *arg)
{
    if (err->status == 0) {
        s_svc_start = svc->start_handle;
        s_svc_end = svc->end_handle;
    } else if (err->status == BLE_HS_EDONE && s_svc_end != 0) {
        int rc = ble_gattc_disc_chrs_by_uuid(conn, s_svc_start, s_svc_end, &s_chr_uuid.u, on_chr, NULL);
        if (rc != 0) {
            fail(conn, "characteristic discovery", rc);
        }
    } else {
        fail(conn, "service discovery", err->status);
    }
    return 0;
}

static int on_mtu(uint16_t conn, const struct ble_gatt_error *err, uint16_t mtu, void *arg)
{
    ESP_LOGI(TAG, "mtu=%u", mtu);
    s_svc_start = s_svc_end = s_val_handle = 0;
    int rc = ble_gattc_disc_svc_by_uuid(conn, &s_svc_uuid.u, on_svc, NULL);
    if (rc != 0) {
        fail(conn, "service discovery", rc);
    }
    return 0;
}

static bool adv_has_service(const struct ble_gap_disc_desc *d)
{
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, d->data, d->length_data) != 0) {
        return false;
    }
    for (int i = 0; i < fields.num_uuids128; i++) {
        if (ble_uuid_cmp(&fields.uuids128[i].u, &s_svc_uuid.u) == 0) {
            return true;
        }
    }
    return false;
}

static void connect_to(const ble_addr_t *addr)
{
    ble_gap_disc_cancel();

    // Short connection interval (7.5-15 ms) so batches go out quickly.
    struct ble_gap_conn_params cp = {
        .scan_itvl = 0x10,
        .scan_window = 0x10,
        .itvl_min = 6,
        .itvl_max = 12,
        .latency = 0,
        .supervision_timeout = 400, // 4 s
    };
    int rc = ble_gap_connect(s_own_addr_type, addr, 10000, &cp, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "connect failed: %d", rc);
        scan();
    }
}

static int gap_event(struct ble_gap_event *e, void *arg)
{
    switch (e->type) {
    case BLE_GAP_EVENT_DISC:
        if (!ble_gap_conn_active() && adv_has_service(&e->disc)) {
            connect_to(&e->disc.addr);
        }
        break;
    case BLE_GAP_EVENT_CONNECT:
        if (e->connect.status == 0) {
            ble_gattc_exchange_mtu(e->connect.conn_handle, on_mtu, NULL);
        } else {
            scan();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT: {
        printf("# link down reason=%d\n", e->disconnect.reason);
        pkt_t marker = {.len = 0};
        xQueueSend(s_q, &marker, 0);
        scan();
        break;
    }
    case BLE_GAP_EVENT_NOTIFY_RX: {
        static pkt_t p; // host task only
        uint16_t len = 0;
        ble_hs_mbuf_to_flat(e->notify_rx.om, p.data, sizeof(p.data), &len);
        p.len = len;
        if (xQueueSend(s_q, &p, 0) != pdTRUE) {
            stat_drop_pkt++;
        }
        break;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
        break;
    }
    return 0;
}

static void on_sync(void)
{
    ESP_ERROR_CHECK(ble_hs_util_ensure_addr(0));
    ESP_ERROR_CHECK(ble_hs_id_infer_auto(0, &s_own_addr_type));
    printf("# scanning for %s\n", GSA_DEVICE_NAME);
    scan();
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

void ble_client_start(QueueHandle_t q)
{
    s_q = q;
    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_svc_gap_init();
    nimble_port_freertos_init(host_task);
}
