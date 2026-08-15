/***************************************************************************/ /**
                                                                               * @file
                                                                               * @brief Device Information GATT Service Override
                                                                               *******************************************************************************
                                                                               * # License
                                                                               * <b>Copyright 2025 Silicon Laboratories Inc. www.silabs.com</b>
                                                                               *******************************************************************************
                                                                               *
                                                                               * SPDX-License-Identifier: Zlib
                                                                               *
                                                                               * The licensor of this software is Silicon Laboratories Inc.
                                                                               *
                                                                               * This software is provided 'as-is', without any express or implied
                                                                               * warranty. In no event will the authors be held liable for any damages
                                                                               * arising from the use of this software.
                                                                               *
                                                                               * Permission is granted to anyone to use this software for any purpose,
                                                                               * including commercial applications, and to alter it and redistribute it
                                                                               * freely, subject to the following restrictions:
                                                                               *
                                                                               * 1. The origin of this software must not be misrepresented; you must not
                                                                               *    claim that you wrote the original software. If you use this software
                                                                               *    in a product, an acknowledgment in the product documentation would be
                                                                               *    appreciated but is not required.
                                                                               * 2. Altered source versions must be plainly marked as such, and must not be
                                                                               *    misrepresented as being the original software.
                                                                               * 3. This notice may not be removed or altered from any source distribution.
                                                                               *
                                                                               ******************************************************************************/

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "sl_status.h"
#include "gatt_db.h"
#include "app_assert.h"
#include "sl_gatt_service_device_information_override.h"

// -----------------------------------------------------------------------------
// Set default values for the characteristics if not provided externally.

#if !defined(FIRMWARE_REVISION_STRING)
// Derive firmware revision string from Bluetooth major, minor and patch versions.
#include "sl_bt_version.h"
#define STR(s) #s
#define XSTR(s) STR(s)

#define FIRMWARE_REVISION_STRING \
  XSTR(SL_BT_VERSION_MAJOR)      \
  "." XSTR(SL_BT_VERSION_MINOR) "." XSTR(SL_BT_VERSION_PATCH)
#endif

#if !defined(MODEL_NUMBER_STRING) && defined(SL_BOARD_NAME)
// Use board name as model number string
#define MODEL_NUMBER_STRING SL_BOARD_NAME
#endif

#if !defined(HARDWARE_REVISION_STRING) && defined(SL_BOARD_REV)
// Use board revision as hardware revision string
#define HARDWARE_REVISION_STRING SL_BOARD_REV
#endif

// -----------------------------------------------------------------------------
// Check if the default values fit into the characteristic value buffers.

#if defined(gattdb_firmware_revision_string) && defined(gattdb_firmware_revision_string_len) && defined(FIRMWARE_REVISION_STRING)
#define FIRMWARE_REVISION_STRING_LEN (sizeof(FIRMWARE_REVISION_STRING) - 1)
static_assert(gattdb_firmware_revision_string_len >= FIRMWARE_REVISION_STRING_LEN,
              "Bluetooth stack version does not fit into firmware revision string characteristic. Please adjust GATT configuration.");
static const uint8_t firmware_revision_string[] = FIRMWARE_REVISION_STRING;
#endif

#if defined(gattdb_model_number_string) && defined(gattdb_model_number_string_len) && defined(MODEL_NUMBER_STRING)
#define MODEL_NUMBER_STRING_LEN (sizeof(MODEL_NUMBER_STRING) - 1)
static_assert(gattdb_model_number_string_len >= MODEL_NUMBER_STRING_LEN,
              "Board name does not fit into model number string characteristic. Please adjust GATT configuration.");
static const uint8_t model_number_string[] = MODEL_NUMBER_STRING;
#endif

#if defined(gattdb_hardware_revision_string) && defined(gattdb_hardware_revision_string_len) && defined(HARDWARE_REVISION_STRING)
#define HARDWARE_REVISION_STRING_LEN (sizeof(HARDWARE_REVISION_STRING) - 1)
static_assert(gattdb_hardware_revision_string_len >= HARDWARE_REVISION_STRING_LEN,
              "Board revision does not fit into hardware revision string characteristic. Please adjust GATT configuration.");
static const uint8_t hardware_revision_string[] = HARDWARE_REVISION_STRING;
#endif

#if defined(gattdb_system_id) && defined(gattdb_system_id_len)
static_assert(gattdb_system_id_len == 8,
              "System ID does not fit into the System ID characteristic. Please adjust GATT configuration.");
#endif

#include "other_driver/ble_notification_manager.h"

static uint8_t s_advertising_set_handle = 0xFF;

/**************************************************************************/ /**
                                                                              * Bluetooth stack event handler.
                                                                              *****************************************************************************/
void sl_gatt_service_device_information_override_on_event(sl_bt_msg_t *evt)
{
  sl_status_t sc;

  // Handle stack events
  switch (SL_BT_MSG_ID(evt->header))
  {
  case sl_bt_evt_system_boot_id:
    // Firmware Revision String
#if defined(gattdb_firmware_revision_string) && defined(gattdb_firmware_revision_string_len) && defined(FIRMWARE_REVISION_STRING)
    sc = sl_bt_gatt_server_write_attribute_value(gattdb_firmware_revision_string,
                                                 0,
                                                 FIRMWARE_REVISION_STRING_LEN,
                                                 firmware_revision_string);
    app_assert_status(sc);
#else
// Skip setting Firmware Revision String.
#endif
    // Model Number String
#if defined(gattdb_model_number_string) && defined(gattdb_model_number_string_len) && defined(MODEL_NUMBER_STRING)
    sc = sl_bt_gatt_server_write_attribute_value(gattdb_model_number_string,
                                                 0,
                                                 MODEL_NUMBER_STRING_LEN,
                                                 model_number_string);
    app_assert_status(sc);
#else
// Skip setting Model Number String characteristic.
#endif

    // Hardware Revision String
#if defined(gattdb_hardware_revision_string) && defined(gattdb_hardware_revision_string_len) && defined(HARDWARE_REVISION_STRING)
    sc = sl_bt_gatt_server_write_attribute_value(gattdb_hardware_revision_string,
                                                 0,
                                                 HARDWARE_REVISION_STRING_LEN,
                                                 hardware_revision_string);
    app_assert_status(sc);
#else
// Skip setting Hardware Revision String.
#endif

    // System ID
#if defined(gattdb_system_id) && defined(gattdb_system_id_len)
    bd_addr address;
    uint8_t address_type;
    uint8_t system_id[gattdb_system_id_len];
    // Extract unique ID from BT Address.
    sc = sl_bt_gap_get_identity_address(&address, &address_type);
    app_assert_status(sc);

    // Pad and reverse unique ID to get System ID.
    system_id[0] = address.addr[5];
    system_id[1] = address.addr[4];
    system_id[2] = address.addr[3];
    system_id[3] = 0xFF;
    system_id[4] = 0xFE;
    system_id[5] = address.addr[2];
    system_id[6] = address.addr[1];
    system_id[7] = address.addr[0];

    sc = sl_bt_gatt_server_write_attribute_value(gattdb_system_id,
                                                 0,
                                                 gattdb_system_id_len,
                                                 system_id);
    app_assert_status(sc);
#else
// Skip setting System ID.
#endif

#if defined(gattdb_device_name)
    // Cáº­p nháº­t tÃªn Bluetooth Device Name thÃ nh 'SomniGuard'
    const char ble_device_name[] = "SomniGuard";
    sl_bt_gatt_server_write_attribute_value(gattdb_device_name,
                                            0,
                                            sizeof(ble_device_name) - 1,
                                            (const uint8_t *)ble_device_name);
#endif

    // Khá»Ÿi táº¡o vÃ  báº¯t Ä‘áº§u phÃ¡t sÃ³ng BLE Advertising (chá»©a tÃªn 'SomniGuard')
    if (s_advertising_set_handle == 0xFF)
    {
      sc = sl_bt_advertiser_create_set(&s_advertising_set_handle);
    }
    else
    {
      sc = SL_STATUS_OK;
    }
    if (sc == SL_STATUS_OK)
    {
      sl_status_t sc_gen = sl_bt_legacy_advertiser_generate_data(s_advertising_set_handle, sl_bt_advertiser_general_discoverable);
      sl_status_t sc_timing = sl_bt_advertiser_set_timing(s_advertising_set_handle, 160, 160, 0, 0);
      sl_status_t sc_start = sl_bt_legacy_advertiser_start(s_advertising_set_handle, sl_bt_legacy_advertiser_connectable);
      // printf("[BLE ADV] Set created ok (handle=%d). Data gen: 0x%04X, Timing: 0x%04X, Start: 0x%04X\r\n",
      //        s_advertising_set_handle, (unsigned int)sc_gen, (unsigned int)sc_timing, (unsigned int)sc_start);
    }
    else
    {
      // printf("[BLE ADV ERR] Failed to create advertiser set! sc=0x%04X\r\n", (unsigned int)sc);
    }
    break;

  case sl_bt_evt_gatt_server_characteristic_status_id:
  {
    uint8_t conn = evt->data.evt_gatt_server_characteristic_status.connection;
    uint16_t config_flags = evt->data.evt_gatt_server_characteristic_status.client_config_flags;
    // client_config_flags > 0 nghÄ©a lÃ  Ä‘Ã£ báº­t Notify (0x0001) hoáº·c Indicate (0x0002)
    bool is_sub = (config_flags > 0);
    somniguard_ble_set_subscribed(conn, is_sub);
    break;
  }

  case sl_bt_evt_gatt_server_user_write_request_id:
  {
    uint16_t att = evt->data.evt_gatt_server_user_write_request.characteristic;
    const uint8_t *val = evt->data.evt_gatt_server_user_write_request.value.data;
    uint16_t val_len = evt->data.evt_gatt_server_user_write_request.value.len;
    somniguard_ble_handle_downlink_cmd(val, val_len, NULL);
    sl_bt_gatt_server_send_user_write_response(
        evt->data.evt_gatt_server_user_write_request.connection,
        att, SL_STATUS_OK);
    break;
  }

  case sl_bt_evt_gatt_server_attribute_value_id:
  {
    const uint8_t *val = evt->data.evt_gatt_server_attribute_value.value.data;
    uint16_t val_len = evt->data.evt_gatt_server_attribute_value.value.len;
    somniguard_ble_handle_downlink_cmd(val, val_len, NULL);
    break;
  }

  case sl_bt_evt_connection_closed_id:
    somniguard_ble_set_subscribed(0xFF, false);
    // Tá»± Ä‘á»™ng phÃ¡t sÃ³ng láº¡i sau khi ngáº¯t káº¿t ná»‘i
    if (s_advertising_set_handle != 0xFF)
    {
      sl_bt_legacy_advertiser_generate_data(s_advertising_set_handle, sl_bt_advertiser_general_discoverable);
      sl_bt_advertiser_set_timing(s_advertising_set_handle, 160, 160, 0, 0);
      sl_bt_legacy_advertiser_start(s_advertising_set_handle, sl_bt_legacy_advertiser_connectable);
    }
    break;

  default:
    break;
  }
}

/**************************************************************************/ /**
                                                                              * Bluetooth Stack Central Event Callback (Silicon Labs SDK)
                                                                              *****************************************************************************/
void sl_bt_on_event(sl_bt_msg_t *evt)
{
  // 1. Cháº¡y handler cáº¥u hÃ¬nh thÃ´ng tin thiáº¿t bá»‹ & GATT Advertising
  sl_gatt_service_device_information_override_on_event(evt);

  // 2. In log UART Console Ä‘á»ƒ theo dÃµi trá»±c tiáº¿p tráº¡ng thÃ¡i BLE cá»§a DevKit
  switch (SL_BT_MSG_ID(evt->header))
  {
  case sl_bt_evt_system_boot_id:
    // printf("\r\n=======================================================\r\n");
    // printf("[BLE STACK] >>> Bluetooth Stack Booted Successfully! <<<\r\n");
    // printf("[BLE STACK] BLE Device Name: 'SomniGuard'\r\n");
    // printf("[BLE STACK] Status: Advertising is ACTIVE over the air.\r\n");
    // printf("=======================================================\r\n\r\n");
    break;

  case sl_bt_evt_connection_opened_id:
    // printf("\r\n[BLE STACK] >>> MOBILE APP CONNECTED! (Conn ID: %d) <<<\r\n\r\n",
    //        (int)evt->data.evt_connection_opened.connection);
    break;

  case sl_bt_evt_connection_closed_id:
    // printf("\r\n[BLE STACK] >>> MOBILE APP DISCONNECTED! (Reason: 0x%04X) <<<\r\n",
    //        (unsigned int)evt->data.evt_connection_closed.reason);
    // printf("[BLE STACK] Restarting BLE Advertising for 'SomniGuard'...\r\n\r\n");
    break;

  default:
    break;
  }
}
