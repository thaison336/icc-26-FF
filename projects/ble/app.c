/***************************************************************************//**
 * @file
 * @brief Core application logic integrated with FreeRTOS BLE tasks.
 *******************************************************************************/
#include "sl_bt_api.h"
#include "sl_main_init.h"
#include "app_assert.h"
#include "app.h"
#include "ble_task.h"
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
        (void)sc;
      }
    }
  }
}

/**************************************************************************//**
 * Hardware Button change callback (Simple Button driver).
 * Called when BTN0 state changes.
 *****************************************************************************/
void sl_simple_button_on_change(const sl_button_t *handle)
{
  if (handle == &sl_button_btn0 && sl_button_get_state(handle) == SL_SIMPLE_BUTTON_PRESSED) {
    // Send "SOS" message via FreeRTOS Queue
    if (ble_send_msg("SOS", 3)) {
      app_proceed();
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

  switch (SL_BT_MSG_ID(evt->header)) {
    // -------------------------------
    // This event indicates the device has started and the radio is ready.
    case sl_bt_evt_system_boot_id:
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
      break;

    // -------------------------------
    // This event indicates that a new connection was opened.
    case sl_bt_evt_connection_opened_id:
      {
        uint8_t conn_handle = evt->data.evt_connection_opened.connection;
        ble_set_connection_state(true, conn_handle);
      }
      break;

    // -------------------------------
    // This event indicates that a connection was closed.
    case sl_bt_evt_connection_closed_id:
      ble_set_connection_state(false, SL_BT_INVALID_CONNECTION_HANDLE);

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
