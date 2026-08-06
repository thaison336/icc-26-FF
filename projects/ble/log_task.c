/***************************************************************************//**
 * @file log_task.c
 * @brief Thread-safe FreeRTOS logger task and queue implementation over VCOM.
 ******************************************************************************/
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "log_task.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "app_assert.h"

#define LOG_TASK_NAME        "log_task"
#define LOG_TASK_STACK_SIZE  512u
#define LOG_TASK_PRIO        (tskIDLE_PRIORITY + 1u)
#define LOG_QUEUE_SIZE       20u

static QueueHandle_t log_queue = NULL;
static TaskHandle_t  log_task_handle = NULL;

/***************************************************************************//**
 * FreeRTOS Task to handle VCOM serial logging.
 * Pops messages from log_queue and prints them to UART VCOM stdout via printf().
 ******************************************************************************/
static void log_task(void *p_arg)
{
  (void)p_arg;
  log_item_t item;
  while (1) {
    if (xQueueReceive(log_queue, &item, portMAX_DELAY) == pdTRUE) {
      printf("%s", item.text);
    }
  }
}

void log_task_init(void)
{
  log_queue = xQueueCreate(LOG_QUEUE_SIZE, sizeof(log_item_t));
  app_assert(log_queue != NULL, "log_queue creation failed.");

  BaseType_t ret = xTaskCreate(log_task,
                              LOG_TASK_NAME,
                              LOG_TASK_STACK_SIZE,
                              NULL,
                              LOG_TASK_PRIO,
                              &log_task_handle);
  app_assert(ret == pdPASS, "log_task creation failed.");

  log_msg("\r\n========================================\r\n");
  log_msg("  FreeRTOS System Logger Initialized!  \r\n");
  log_msg("========================================\r\n");
}

bool log_msg(const char *msg)
{
  if (msg == NULL || log_queue == NULL) {
    return false;
  }

  log_item_t item;
  strncpy(item.text, msg, LOG_MAX_LEN - 1);
  item.text[LOG_MAX_LEN - 1] = '\0';

  if (xPortIsInsideInterrupt()) {
    BaseType_t woken = pdFALSE;
    BaseType_t res = xQueueSendFromISR(log_queue, &item, &woken);
    portYIELD_FROM_ISR(woken);
    return (res == pdTRUE);
  } else {
    return (xQueueSend(log_queue, &item, 0) == pdTRUE);
  }
}

bool log_fmt(const char *fmt, ...)
{
  if (fmt == NULL || log_queue == NULL) {
    return false;
  }

  log_item_t item;
  va_list args;
  va_start(args, fmt);
  vsnprintf(item.text, LOG_MAX_LEN, fmt, args);
  va_end(args);

  if (xPortIsInsideInterrupt()) {
    BaseType_t woken = pdFALSE;
    BaseType_t res = xQueueSendFromISR(log_queue, &item, &woken);
    portYIELD_FROM_ISR(woken);
    return (res == pdTRUE);
  } else {
    return (xQueueSend(log_queue, &item, 0) == pdTRUE);
  }
}
