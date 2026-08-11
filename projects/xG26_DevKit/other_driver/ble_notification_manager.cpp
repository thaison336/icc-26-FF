/**
 * @file ble_notification_manager.cpp
 * @brief Hiện thực Hệ thống Truyền tin BLE Đa hành động (Multi-Action Notification)
 */

#include "ble_notification_manager.h"
#include <stdio.h>
#include <string.h>

#if __has_include("gatt_db.h")
#include "gatt_db.h"
#include "sl_bt_api.h"
#include "sl_status.h"
#endif

static uint8_t  active_connection_handle = 0xFF;
static bool     app_is_subscribed        = false;
static uint16_t global_seq_num           = 0;

void somniguard_ble_manager_init(void)
{
    active_connection_handle = 0xFF;
    app_is_subscribed        = false;
    global_seq_num           = 0;
    printf("[BLE MGR] SomniGuard BLE Notification Manager Initialized.\r\n");
}

void somniguard_ble_set_subscribed(uint8_t connection_handle, bool is_subscribed)
{
    active_connection_handle = connection_handle;
    app_is_subscribed        = is_subscribed;
    printf("[BLE MGR] Connection 0x%02X Subscription Status: %s\r\n",
           connection_handle, is_subscribed ? "SUBSCRIBED" : "UNSUBSCRIBED");
}

bool somniguard_ble_is_subscribed(void)
{
    return app_is_subscribed;
}

void somniguard_ble_log_packet(const somniguard_ble_event_pkt_t *pkt)
{
    printf("[BLE NOTIFY] Sent Pkt -> Type: 0x%02X, Code: 0x%02X, Seq: %u, Param1: %u, Param2: %u\r\n",
           pkt->event_type, pkt->event_code, pkt->seq_num, pkt->param1, pkt->param2);
}

bool somniguard_ble_notify_event(somniguard_ble_evt_type_t type,
                                 somniguard_ble_evt_code_t code,
                                 uint16_t param1,
                                 uint16_t param2)
{
    somniguard_ble_event_pkt_t pkt;
    pkt.event_type = (uint8_t)type;
    pkt.event_code = (uint8_t)code;
    pkt.seq_num    = ++global_seq_num;
    pkt.param1     = param1;
    pkt.param2     = param2;

    // Log ra UART console
    somniguard_ble_log_packet(&pkt);

#if defined(gattdb_wearable_data)
    if (active_connection_handle != 0xFF && app_is_subscribed) {
        sl_status_t sc = sl_bt_gatt_server_send_notification(
            active_connection_handle,
            gattdb_wearable_data,
            sizeof(somniguard_ble_event_pkt_t),
            (const uint8_t *)&pkt
        );

        if (sc == SL_STATUS_OK) {
            return true;
        } else {
            printf("[BLE MGR] WARNING: Send notification failed (sc=0x%04X)\r\n", (unsigned int)sc);
            return false;
        }
    } else {
        sl_status_t sc = sl_bt_gatt_server_notify_all(
            gattdb_wearable_data,
            sizeof(somniguard_ble_event_pkt_t),
            (const uint8_t *)&pkt
        );
        return (sc == SL_STATUS_OK);
    }
#else
    return true;
#endif
}
