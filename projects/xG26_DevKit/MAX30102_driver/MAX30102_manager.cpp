#include "MAX30102_manager.h"
#include "imu.h"
#include "stdio.h"
#include "em_gpio.h"
#include "gpiointerrupt.h"
///////Setup interrupt for MAX30102//////////////////////
static MAX30102_manager *g_MAX30102_manager = nullptr;
volatile bool m_dataReady = false;
void max30102_gpio_callback(uint8_t pin)
{
    if (pin == MAX30102_INT_PIN)
    {
        g_MAX30102_manager->notifyFromISR();
    }
}
void MAX30102_manager::notifyFromISR()
{
    m_dataReady = true;

    BaseType_t hpw = pdFALSE;
    if (m_taskHandle != nullptr)
    {
        vTaskNotifyGiveFromISR(m_taskHandle, &hpw);
        portYIELD_FROM_ISR(hpw);
    }
}
void init_MAX30102_Interrupt(MAX30102_manager *MAX30102Sensor)
{
    g_MAX30102_manager = MAX30102Sensor;
    // Khá»Ÿi táº¡o thÆ° viá»‡n quáº£n lÃ½ ngáº¯t cá»§a SDK (náº¿u chÆ°a Ä‘Æ°á»£c há»‡ thá»‘ng gá»i)
    GPIOINT_Init();
    NVIC_SetPriority(GPIO_EVEN_IRQn, 5);

    GPIO_PinModeSet(MAX30102_INT_PORT, MAX30102_INT_PIN, gpioModeInputPullFilter, 1);
    GPIO_ExtIntConfig(MAX30102_INT_PORT, MAX30102_INT_PIN, MAX30102_INT_CH, false, true, true);
    GPIOINT_CallbackRegister(MAX30102_INT_CH, max30102_gpio_callback);
}
// end of interrupt setup for MAX30102////////

MAX30102_manager::MAX30102_manager()
{
    m_cmdQueue = nullptr;

    m_taskHandle = nullptr;
}

bool MAX30102_manager::begin(I2CBus *bus,
                             uint32_t speed,
                             uint8_t address)
{
    if (m_cmdQueue == nullptr)
    {
        m_cmdQueue = xQueueCreate(10, sizeof(SensorCommand));
    }
    return m_sensor.begin(
        bus,
        speed,
        address);
}

MAX30105 &MAX30102_manager::driver()
{
    return m_sensor;
}

QueueHandle_t MAX30102_manager::commandQueue()
{
    return m_cmdQueue;
}

bool MAX30102_manager::sendCommand(
    SensorCommandType cmd,
    uint32_t value)
{
    SensorCommand command;
    command.type = cmd;
    command.value = value;

    // KhÃ´ng nÃªn dÃ¹ng portMAX_DELAY. Äáº·t timeout ngáº¯n (vd 100ms) Ä‘á»ƒ trÃ¡nh treo caller task.
    if (xQueueSend(m_cmdQueue, &command, pdMS_TO_TICKS(100)) == pdPASS)
    {
        // ÄÃ¡nh thá»©c task bÃªn dÆ°á»›i Ä‘á»ƒ xá»­ lÃ½ command ngay láº­p tá»©c
        if (m_taskHandle != nullptr)
        {
            xTaskNotifyGive(m_taskHandle);
        }
        return true;
    }
  //  printf("Queue FULL!\r\n");
    return false;
}
bool MAX30102_manager::setSampleRate(uint16_t rate)
{
    return sendCommand(
        SENSOR_CMD_SET_SAMPLE_RATE,
        rate);
}

bool MAX30102_manager::setPulseAmplitudeRed(uint8_t amp)
{
    return sendCommand(
        SENSOR_CMD_SET_LED_RED,
        amp);
}

bool MAX30102_manager::setPulseAmplitudeIR(uint8_t amp)
{
    return sendCommand(
        SENSOR_CMD_SET_LED_IR,
        amp);
}

bool MAX30102_manager::setPulseAmplitudeGreen(uint8_t amp)
{
    return sendCommand(
        SENSOR_CMD_SET_LED_GREEN,
        amp);
}

bool MAX30102_manager::setPulseWidth(uint8_t width)
{
    return sendCommand(
        SENSOR_CMD_SET_PULSE_WIDTH,
        width);
}

bool MAX30102_manager::setADCRange(uint8_t range)
{
    return sendCommand(
        SENSOR_CMD_SET_ADC_RANGE,
        range);
}

bool MAX30102_manager::clearFIFO()
{
    return sendCommand(
        SENSOR_CMD_CLEAR_FIFO);
}

bool MAX30102_manager::enableDATARDY()
{
    return sendCommand(
        SENSOR_CMD_ENABLE_DATA_RDY);
}

bool MAX30102_manager::disableDATARDY()
{
    return sendCommand(
        SENSOR_CMD_DISABLE_DATA_RDY);
}

bool MAX30102_manager::softReset()
{
    return sendCommand(
        SENSOR_CMD_SOFT_RESET);
}
bool MAX30102_manager::setup(
    uint8_t power,
    uint8_t avg,
    uint8_t ledMode,
    int sampleRate,
    int pulseWidth,
    int adcRange)
{
    m_sensor.setup(
        power,
        avg,
        ledMode,
        sampleRate,
        pulseWidth,
        adcRange);

    return true;
}
uint8_t MAX30102_manager::available()
{
    return m_sensor.available();
}

uint32_t MAX30102_manager::getRed()
{
    return m_sensor.getRed();
}

uint32_t MAX30102_manager::getIR()
{
    return m_sensor.getIR();
}

uint32_t MAX30102_manager::getGreen()
{
    return m_sensor.getGreen();
}

uint32_t MAX30102_manager::getFIFORed()
{
    return m_sensor.getFIFORed();
}

uint32_t MAX30102_manager::getFIFOIR()
{
    return m_sensor.getFIFOIR();
}

uint32_t MAX30102_manager::getFIFOGreen()
{
    return m_sensor.getFIFOGreen();
}

bool MAX30102_manager::nextSample()
{
    return m_sensor.nextSample();
}

uint8_t MAX30102_manager::getRevisionID()
{
    return m_sensor.getRevisionID();
}

void MAX30102_manager::task()
{
    m_taskHandle = xTaskGetCurrentTaskHandle();

    SensorCommand cmd;
    m_sensor.getINT1();
    m_sensor.getINT2();
    vTaskDelay(pdMS_TO_TICKS(20));
    while (true)
    {
        //-----------------------------------
        // Xá»­ lÃ½ command trÆ°á»›c
        //-----------------------------------
        //-----------------------------------
        // Chá» interrupt
        //-----------------------------------

        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (m_dataReady)
        {
            m_dataReady = false;
            uint8_t int1 = m_sensor.getINT1();
            uint8_t int2 = m_sensor.getINT2();
            int n = m_sensor.check();
            if (n > 0)
            {
                // Đồng bộ lấy dữ liệu MPU6050 1 lần cho mỗi đợt ngắt batch (tránh nghẽn I2C)
                IMU::getInstance().processInterrupt();
            }
        }
        while (xQueueReceive(m_cmdQueue,
                             &cmd,
                             0) == pdPASS)
        {
        //    printf("Receive command %d\n", cmd.type);
            switch (cmd.type)
            {

            case SENSOR_CMD_SET_SAMPLE_RATE:

                m_sensor.clearFIFO();

                // vTaskDelay(pdMS_TO_TICKS(5));

                m_sensor.Max30102_setSampleRate(cmd.value);
                m_sensor.clearDataBuffer();
                break;

            case SENSOR_CMD_SET_LED_RED:

                m_sensor.setPulseAmplitudeRed(cmd.value);
                m_sensor.clearFIFO();
                m_sensor.Max30102_setSampleRate(50);
                m_sensor.clearDataBuffer();
                break;

            case SENSOR_CMD_SET_LED_IR:

                m_sensor.setPulseAmplitudeIR(cmd.value);
                m_sensor.clearFIFO();
                m_sensor.Max30102_setSampleRate(50);
                m_sensor.clearDataBuffer();
                break;

            case SENSOR_CMD_SET_LED_GREEN:

                m_sensor.setPulseAmplitudeGreen(cmd.value);

                break;

            case SENSOR_CMD_SET_ADC_RANGE:

                m_sensor.setADCRange(cmd.value);
                m_sensor.clearFIFO();
                m_sensor.Max30102_setSampleRate(50);
                m_sensor.clearDataBuffer();

                break;

            case SENSOR_CMD_SET_PULSE_WIDTH:

                m_sensor.setPulseWidth(cmd.value);
                m_sensor.clearFIFO();
                m_sensor.Max30102_setSampleRate(50);
                m_sensor.clearDataBuffer();
                break;

            case SENSOR_CMD_CLEAR_FIFO:

                m_sensor.clearFIFO();
                m_sensor.Max30102_setSampleRate(50);
                m_sensor.clearDataBuffer();
                m_sensor.clearFIFO();
                m_sensor.Max30102_setSampleRate(50);

                break;

            case SENSOR_CMD_ENABLE_DATA_RDY:

                m_sensor.enableDATARDY();

                break;

            case SENSOR_CMD_DISABLE_DATA_RDY:

                m_sensor.disableDATARDY();

                break;

            case SENSOR_CMD_SOFT_RESET:

                m_sensor.softReset();
                m_sensor.clearFIFO();
                m_sensor.clearDataBuffer();

                break;

            default:
                break;
            }
        }

        //-----------------------------------
        // Äá»c FIFO
        //-----------------------------------
    }
}

uint32_t MAX30102_manager ::getADCrange()
{
    return m_sensor.getADCrange();
}
