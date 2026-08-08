#ifndef SENSOR_COMMAND_H
#define SENSOR_COMMAND_H

#include <stdint.h>

typedef enum
{
    SENSOR_CMD_NONE = 0,

    SENSOR_CMD_SET_SAMPLE_RATE,
    SENSOR_CMD_SET_LED_RED,
    SENSOR_CMD_SET_LED_IR,
    SENSOR_CMD_SET_LED_GREEN,

    SENSOR_CMD_SET_ADC_RANGE,
    SENSOR_CMD_SET_PULSE_WIDTH,

    SENSOR_CMD_CLEAR_FIFO,

    SENSOR_CMD_ENABLE_DATA_RDY,
    SENSOR_CMD_DISABLE_DATA_RDY,

    SENSOR_CMD_SOFT_RESET,

} SensorCommandType;

typedef struct
{
    SensorCommandType type;

    uint32_t value;

} SensorCommand;

#endif