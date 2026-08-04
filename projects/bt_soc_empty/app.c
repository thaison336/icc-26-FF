/***************************************************************************//**
 * @file
 * @brief Core application logic.
 *******************************************************************************
 * # License
 * <b>Copyright 2024 Silicon Laboratories Inc. www.silabs.com</b>
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
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "sl_bt_api.h"
#include "sl_main_init.h"
#include "app_assert.h"
#include "led_blinky.h"
#include "gatt_db.h"
#include "sl_simple_button_instances.h"
#include "app.h"

// Standard C printf retargeted to vcom via iostream_retarget_stdio
#define app_log(...) printf(__VA_ARGS__)

// The advertising set handle allocated from Bluetooth stack.
static uint8_t advertising_set_handle = 0xff;

// Track active BLE connection handle (0xFF / SL_BT_INVALID_CONNECTION_HANDLE when disconnected)
static uint8_t active_connection_handle = SL_BT_INVALID_CONNECTION_HANDLE;

// FreeRTOS Task Handle for SOS Application Task
static TaskHandle_t sos_app_task_handle = NULL;

/**************************************************************************//**
 * FreeRTOS Task: SOS Application Task
 * Waits for Direct Task Notification from Button 0 (PC07) ISR and dispatches
 * BLE SOS Notification to connected phone app.
 *****************************************************************************/
static void sos_app_task_func(void *pvParameters)
{
  (void)pvParameters;

  app_log("[FreeRTOS] SOS Application Task Started!\r\n");

  while (1) {
    // Wait indefinitely for direct notification from ISR (Button 0 press)
    uint32_t ulNotificationValue = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    if (ulNotificationValue > 0) {
      app_log("\r\n[FreeRTOS Task] Button 0 (PC07) Notification Received!\r\n");

      if (active_connection_handle != SL_BT_INVALID_CONNECTION_HANDLE) {
        app_log("[SOS] Sending BLE SOS Notification ('SOS') to Phone App...\r\n");

        sl_status_t sc = sl_bt_gatt_server_send_notification(
          active_connection_handle,
          gattdb_sos_characteristic,
          3,
          (const uint8_t *)"SOS"
        );

        if (sc == SL_STATUS_OK) {
          app_log("[SOS] SUCCESS: Notification dispatched (UUID: 0xFFE1)!\r\n");
        } else {
          app_log("[SOS] ERROR: Failed to send notification (status: 0x%04lx)\r\n", sc);
        }
      } else {
        app_log("[SOS] WARNING: No active BLE Connection! Connect Phone App first.\r\n");
      }
    }
  }
}

// Application Init.
void app_init(void)
{
  // Khởi tạo module LED Blinky
  led_blinky_init();

  app_log("\r\n========================================\r\n");
  app_log("  BLE SOS Wearable Dev Kit (FreeRTOS)    \r\n");
  app_log("  Baud Rate: 115200 | Button 0: PC07     \r\n");
  app_log("========================================\r\n");

  // Create FreeRTOS SOS Application Task
  BaseType_t xReturned = xTaskCreate(
    sos_app_task_func,
    "SOS_App_Task",
    1024 / sizeof(StackType_t), // Stack depth
    NULL,
    tskIDLE_PRIORITY + 2,
    &sos_app_task_handle
  );

  if (xReturned != pdPASS) {
    app_log("[ERROR] Failed to create SOS App Task!\r\n");
  } else {
    app_log("[FreeRTOS] SOS App Task created successfully.\r\n");
  }
}

// Application Process Action (Kept for compatibility; work is done in FreeRTOS Task)
void app_process_action(void)
{
  // Non-blocking background actions if needed when running in super loop fallback
}

/**************************************************************************//**
 * Simple Button State Change Callback (ISR context)
 * Triggered on single press of Button 0 (PC07)
 *****************************************************************************/
void sl_button_on_change(const sl_button_t *handle)
{
  if (sl_button_get_state(handle) == SL_SIMPLE_BUTTON_PRESSED) {
    if (sos_app_task_handle != NULL) {
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      vTaskNotifyGiveFromISR(sos_app_task_handle, &xHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
  }
}

/**************************************************************************//**
 * Bluetooth stack event handler.
 * This overrides the default weak implementation.
 *
 * @param[in] evt Event coming from the Bluetooth stack.
 *****************************************************************************/
void sl_bt_on_event(sl_bt_msg_t *evt)
{
  sl_status_t sc;

  // Chuyển sự kiện BLE cho module LED Blinky xử lý nếu có (Soft Timer event...)
  led_blinky_on_event(evt);

  switch (SL_BT_MSG_ID(evt->header)) {
    // -------------------------------
    // This event indicates the device has started and the radio is ready.
    case sl_bt_evt_system_boot_id:
      app_log("[BLE] System Boot Event Received. Radio Ready.\r\n");

      // Create an advertising set.
      sc = sl_bt_advertiser_create_set(&advertising_set_handle);
      app_assert_status(sc);

      // Generate data for advertising
      sc = sl_bt_legacy_advertiser_generate_data(advertising_set_handle,
                                                 sl_bt_advertiser_general_discoverable);
      app_assert_status(sc);

      // Set advertising interval to 100ms.
      sc = sl_bt_advertiser_set_timing(
        advertising_set_handle,
        160, // min. adv. interval (milliseconds * 1.6)
        160, // max. adv. interval (milliseconds * 1.6)
        0,   // adv. duration
        0);  // max. num. adv. events
      app_assert_status(sc);

      // Start advertising and enable connections.
      sc = sl_bt_legacy_advertiser_start(advertising_set_handle,
                                         sl_bt_legacy_advertiser_connectable);
      app_assert_status(sc);
      app_log("[BLE] Started Connectable Advertising (Interval: 100ms)...\r\n");
      break;

    // -------------------------------
    // This event indicates that a new connection was opened.
    case sl_bt_evt_connection_opened_id:
      active_connection_handle = evt->data.evt_connection_opened.connection;
      app_log("\r\n[BLE] *** CONNECTION OPENED *** (Handle: %d)\r\n", active_connection_handle);

      // Bắt đầu chớp nháy LED với chu kỳ 500ms khi có kết nối
      led_blinky_start(500);
      break;

    // -------------------------------
    // This event indicates that a connection was closed.
    case sl_bt_evt_connection_closed_id:
      app_log("\r\n[BLE] *** CONNECTION CLOSED *** (Handle: %d, Reason: 0x%04x)\r\n",
             evt->data.evt_connection_closed.connection,
             evt->data.evt_connection_closed.reason);

      active_connection_handle = SL_BT_INVALID_CONNECTION_HANDLE;

      // Dừng chớp nháy LED và tắt LED khi ngắt kết nối
      led_blinky_stop();

      // Generate data for advertising
      sc = sl_bt_legacy_advertiser_generate_data(advertising_set_handle,
                                                 sl_bt_advertiser_general_discoverable);
      app_assert_status(sc);

      // Restart advertising after client has disconnected.
      sc = sl_bt_legacy_advertiser_start(advertising_set_handle,
                                         sl_bt_legacy_advertiser_connectable);
      app_assert_status(sc);
      app_log("[BLE] Restarted Connectable Advertising...\r\n");
      break;

    default:
      break;
  }
}
