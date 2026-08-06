/***************************************************************************//**
 * @file log_task.h
 * @brief Thread-safe FreeRTOS logger task and queue interface.
 ******************************************************************************/
#ifndef LOG_TASK_H
#define LOG_TASK_H

#include <stdint.h>
#include <stdbool.h>

// Maximum character length for a single log line
#define LOG_MAX_LEN  64

// Log message item structure
typedef struct {
  char text[LOG_MAX_LEN];
} log_item_t;

/**
 * @brief Initialize the FreeRTOS logger task and queue.
 */
void log_task_init(void);

/**
 * @brief Thread-safe and ISR-safe function to post a string log to VCOM serial port.
 *
 * @param[in] msg Null-terminated string message.
 * @return true if queued successfully, false otherwise.
 */
bool log_msg(const char *msg);

/**
 * @brief Thread-safe formatted logger (printf style).
 *
 * @param[in] fmt Format string.
 * @return true if queued successfully, false otherwise.
 */
bool log_fmt(const char *fmt, ...);

#endif // LOG_TASK_H
