/***************************************************************************//**
 * @file
 * @brief Core application logic integrated with FreeRTOS BLE and Logger tasks.
 *******************************************************************************/
#include "sl_bt_api.h"
#include "sl_main_init.h"
#include "app_assert.h"
#include "app.h"
#include "ble_task.h"
#include "log_task.h"
#include "sl_simple_button_instances.h"
#include "sl_simple_button.h"

// The advertising set handle allocated from Bluetooth stack.
static uint8_t advertising_set_handle = 0xff;

// Application Init.
void app_init(void)
{
  /////////////////////////////////////////////////////////////////////////////
  // Custom application init code                                            //
  /////////////////////////////////////////////////////////////////////////////
}

// Application Process Action.
void app_process_action(void)
{
  if (app_is_process_required()) {
    // Process outgoing BLE messages from ble_tx_queue
    ble_msg_t msg;
    while (xQueueReceive(ble_tx_queue, &msg, 0) == pdTRUE) {
      if (ble_is_connected()) {
        uint8_t conn_handle = ble_get_connection_handle();
        sl_status_t sc = sl_bt_gatt_server_send_notification(
          conn_handle,
          msg.characteristic_id,
          msg.len,
          msg.payload
        );
        if (sc == SL_STATUS_OK) {
          log_fmt("[BLE TX] Notification sent! Handle=%d, Payload='%.*s'\r\n", conn_handle, msg.len, msg.payload);
        } else {
          log_fmt("[BLE TX ERR] Notification failed with status 0x%04X\r\n", sc);
        }
      } else {
        log_msg("[BLE TX WARN] Discarded notification (Not connected)\r\n");
      }
    }
  }
}

/**************************************************************************//**
 * Hardware Button change callback (Simple Button driver).
 * Called when BTN0 or BTN1 state changes.
 *****************************************************************************/
void sl_button_on_change(const sl_button_t *handle)
{
  sl_button_state_t state = sl_button_get_state(handle);
  const char *state_str = (state == SL_SIMPLE_BUTTON_PRESSED) ? "PRESSED" : "RELEASED";

  // Check if the event is from BTN0 or BTN1 (if defined)
  if (handle == &sl_button_btn0) {
    log_fmt("[STATUS] BTN0 State: %s\r\n", state_str);
  } else {
    log_fmt("[STATUS] BTN1 State: %s\r\n", state_str);
  }

  // Trigger SOS alert on press event
  if (state == SL_SIMPLE_BUTTON_PRESSED) {
    log_msg("[BTN0] SOS Alert Triggered! Queuing BLE notification...\r\n");
    if (ble_send_msg("SOS", 3)) {
      app_proceed();
    }
  }
}

void sl_simple_button_on_change(const sl_button_t *handle)
{
  sl_button_on_change(handle);
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

  switch (SL_BT_MSG_ID(evt->header)) {
    // -------------------------------
    // This event indicates the device has started and the radio is ready.
    case sl_bt_evt_system_boot_id:
      log_msg("[BLE] System boot event received. Initializing advertiser...\r\n");
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
      log_msg("[BLE] Advertising started. Ready for phone connection.\r\n");
      break;

    // -------------------------------
    // This event indicates that a new connection was opened.
    case sl_bt_evt_connection_opened_id:
      {
        uint8_t conn_handle = evt->data.evt_connection_opened.connection;
        ble_set_connection_state(true, conn_handle);
        log_fmt("[BLE] Connection opened! Active handle: %d\r\n", conn_handle);
      }
      break;

    // -------------------------------
    // This event indicates that a connection was closed.
    case sl_bt_evt_connection_closed_id:
      ble_set_connection_state(false, SL_BT_INVALID_CONNECTION_HANDLE);
      log_msg("[BLE] Connection closed. Restarting advertising...\r\n");

      // Generate data for advertising
      sc = sl_bt_legacy_advertiser_generate_data(advertising_set_handle,
                                                 sl_bt_advertiser_general_discoverable);
      app_assert_status(sc);

      // Restart advertising after client has disconnected.
      sc = sl_bt_legacy_advertiser_start(advertising_set_handle,
                                         sl_bt_legacy_advertiser_connectable);
      app_assert_status(sc);
      break;

    default:
      break;
  }
}
