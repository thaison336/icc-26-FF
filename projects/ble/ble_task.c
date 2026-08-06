/***************************************************************************//**
 * @file ble_task.c
 * @brief Implementation of modular FreeRTOS tasks, queues, and event groups for BLE.
 ******************************************************************************/
#include <string.h>
#include "ble_task.h"
#include "task.h"
#include "sl_bt_api.h"
#include "sl_simple_led_instances.h"
#include "sl_simple_led.h"
#include "app_assert.h"
#include "gatt_db.h"

#ifndef gattdb_wearable_data
#ifdef gattdb_gattdb_wearable_data
#define gattdb_wearable_data gattdb_gattdb_wearable_data
#else
#define gattdb_wearable_data 27
#endif
#endif

// FreeRTOS Handles
EventGroupHandle_t ble_event_group = NULL;
QueueHandle_t      ble_tx_queue     = NULL;

// Active connection handle
static uint8_t active_connection = SL_BT_INVALID_CONNECTION_HANDLE;

// LED Task Configuration
#define LED_TASK_NAME        "led_task"
#define LED_TASK_STACK_SIZE  512u
#define LED_TASK_PRIO        (tskIDLE_PRIORITY + 1u)

static TaskHandle_t led_task_handle = NULL;

/***************************************************************************//**
 * FreeRTOS Task to handle LED0 state based on BLE Connection State.
 * - Disconnected / Advertising: LED0 stays ON solid.
 * - Connected: LED0 toggles every 700ms.
 ******************************************************************************/
static void led_task(void *p_arg)
{
  (void)p_arg;
  while (1) {
    EventBits_t uxBits = xEventGroupGetBits(ble_event_group);
    if (uxBits & BLE_CONNECTED_BIT) {
      // Connected: Toggle LED0 every 700ms
      sl_simple_led_toggle(sl_led_led0.context);
      vTaskDelay(pdMS_TO_TICKS(700));
    } else {
      // Disconnected: Keep LED0 ON solid continuously
      sl_simple_led_turn_on(sl_led_led0.context);
      vTaskDelay(pdMS_TO_TICKS(200));
    }
  }
}

void ble_task_init(void)
{
  // Create Event Group
  ble_event_group = xEventGroupCreate();
  app_assert(ble_event_group != NULL, "ble_event_group creation failed.");

  // Set default state to advertising (disconnected)
  xEventGroupSetBits(ble_event_group, BLE_ADVERTISING_BIT);

  // Create TX Queue (capacity for 10 messages)
  ble_tx_queue = xQueueCreate(10, sizeof(ble_msg_t));
  app_assert(ble_tx_queue != NULL, "ble_tx_queue creation failed.");

  // Create LED Task
  BaseType_t ret = xTaskCreate(led_task,
                              LED_TASK_NAME,
                              LED_TASK_STACK_SIZE,
                              NULL,
                              LED_TASK_PRIO,
                              &led_task_handle);
  app_assert(ret == pdPASS, "LED task creation failed.");
}

bool ble_send_msg(const char *data, uint16_t len)
{
  if (data == NULL || len == 0 || len > BLE_MAX_PAYLOAD_LEN) {
    return false;
  }

  ble_msg_t msg;
  msg.characteristic_id = gattdb_wearable_data;
  msg.len = len;
  memcpy(msg.payload, data, len);

  if (xPortIsInsideInterrupt()) {
    BaseType_t woken = pdFALSE;
    BaseType_t res = xQueueSendFromISR(ble_tx_queue, &msg, &woken);
    portYIELD_FROM_ISR(woken);
    return (res == pdTRUE);
  } else {
    return (xQueueSend(ble_tx_queue, &msg, 0) == pdTRUE);
  }
}

void ble_set_connection_state(bool connected, uint8_t conn_handle)
{
  if (connected) {
    active_connection = conn_handle;
    xEventGroupClearBits(ble_event_group, BLE_ADVERTISING_BIT);
    xEventGroupSetBits(ble_event_group, BLE_CONNECTED_BIT);
  } else {
    active_connection = SL_BT_INVALID_CONNECTION_HANDLE;
    xEventGroupClearBits(ble_event_group, BLE_CONNECTED_BIT);
    xEventGroupSetBits(ble_event_group, BLE_ADVERTISING_BIT);
  }
}

bool ble_is_connected(void)
{
  if (ble_event_group == NULL) return false;
  EventBits_t bits = xEventGroupGetBits(ble_event_group);
  return ((bits & BLE_CONNECTED_BIT) != 0);
}

uint8_t ble_get_connection_handle(void)
{
  return active_connection;
}
