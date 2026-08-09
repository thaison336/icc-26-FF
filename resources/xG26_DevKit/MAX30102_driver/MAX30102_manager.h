#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include "MAX30105.h"
#include "SensorCommand.h"
#define MAX30102_INT_PORT gpioPortB
#define MAX30102_INT_PIN 7
#define MAX30102_INT_CH 7 // Kênh ngắt (thường chọn trùng số Pin)

#include "FreeRTOS.h"
#include "queue.h"

class MAX30102_manager
{
public:
    MAX30102_manager();

    bool begin(I2CBus *bus,
               uint32_t speed = 100000,
               uint8_t address = MAX30105_ADDRESS);

    //---------------------------------------
    // Task dùng để đọc dữ liệu
    //---------------------------------------

    MAX30105 &driver();

    //---------------------------------------
    // Queue
    //---------------------------------------

    QueueHandle_t commandQueue();

    //---------------------------------------
    // API giữ nguyên tên
    //---------------------------------------

    bool setup(uint8_t powerLevel,
               uint8_t sampleAverage,
               uint8_t ledMode,
               int sampleRate,
               int pulseWidth,
               int adcRange);

    bool setSampleRate(uint16_t sampleRate);

    bool setPulseAmplitudeRed(uint8_t amp);

    bool setPulseAmplitudeIR(uint8_t amp);

    bool setPulseAmplitudeGreen(uint8_t amp);

    bool setPulseWidth(uint8_t width);

    bool setADCRange(uint8_t range);

    bool clearFIFO();

    bool enableDATARDY();

    bool disableDATARDY();

    bool softReset();
    uint8_t available();

    uint32_t getRed();

    uint32_t getIR();

    uint32_t getGreen();

    uint32_t getFIFORed();

    uint32_t getFIFOIR();

    uint32_t getFIFOGreen();

    bool nextSample();

    uint8_t getRevisionID();

    void task();

    void notifyFromISR();
    uint32_t getADCrange();

private:
    bool sendCommand(SensorCommandType cmd,
                     uint32_t value = 0);

private:
    MAX30105 m_sensor;

    QueueHandle_t m_cmdQueue;

    TaskHandle_t m_taskHandle;

    volatile bool m_dataReady = false;
};

void init_MAX30102_Interrupt(MAX30102_manager *MAX30102_manager);
#endif