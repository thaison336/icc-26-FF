/***************************************************************************//**
 * @file ble_task.h
 * @brief Modular FreeRTOS interface for BLE tasks, queues, and event groups.
 ******************************************************************************/
#ifndef BLE_TASK_H
#define BLE_TASK_H

#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "event_groups.h"
#include "queue.h"

// Event Group bit definitions
#define BLE_CONNECTED_BIT    (1 << 0)
#define BLE_ADVERTISING_BIT  (1 << 1)

// Maximum payload buffer length for an outgoing BLE message
#define BLE_MAX_PAYLOAD_LEN  32

// Message structure for outgoing BLE GATT notifications
typedef struct {
  uint16_t characteristic_id;
  uint16_t len;
  uint8_t payload[BLE_MAX_PAYLOAD_LEN];
} ble_msg_t;

// FreeRTOS Synchronization Handles
extern EventGroupHandle_t ble_event_group;
extern QueueHandle_t      ble_tx_queue;

/**
 * @brief Initialize BLE FreeRTOS queues, event groups, and system tasks.
 */
void ble_task_init(void);

/**
 * @brief Thread-safe API to post an outgoing notification string/bytes to the BLE queue.
 * Safe to call from any FreeRTOS task or ISR context.
 *
 * @param[in] data Payload byte array or null-terminated string.
 * @param[in] len Length of the data to transmit (max BLE_MAX_PAYLOAD_LEN).
 * @return true if successfully queued, false otherwise.
 */
bool ble_send_msg(const char *data, uint16_t len);

/**
 * @brief Update the BLE connection state in the FreeRTOS Event Group.
 *
 * @param[in] connected True if connected, false if disconnected.
 * @param[in] conn_handle Active connection handle (or SL_BT_INVALID_CONNECTION_HANDLE).
 */
void ble_set_connection_state(bool connected, uint8_t conn_handle);

/**
 * @brief Thread-safe check if BLE is currently connected.
 * @return true if connected, false otherwise.
 */
bool ble_is_connected(void);

/**
 * @brief Get the active BLE connection handle.
 * @return Connection handle ID.
 */
uint8_t ble_get_connection_handle(void);

#endif // BLE_TASK_H
