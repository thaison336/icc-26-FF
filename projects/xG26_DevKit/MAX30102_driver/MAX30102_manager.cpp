#include "MAX30102_manager.h"
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
    vTaskNotifyGiveFromISR(m_taskHandle, &hpw);
    portYIELD_FROM_ISR(hpw);
}
void init_MAX30102_Interrupt(MAX30102_manager *MAX30102Sensor)
{
    g_MAX30102_manager = MAX30102Sensor;
    // Khởi tạo thư viện quản lý ngắt của SDK (nếu chưa được hệ thống gọi)
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

    // Không nên dùng portMAX_DELAY. Đặt timeout ngắn (vd 100ms) để tránh treo caller task.
    if (xQueueSend(m_cmdQueue, &command, pdMS_TO_TICKS(100)) == pdPASS)
    {
        // Đánh thức task bên dưới để xử lý command ngay lập tức
        if (m_taskHandle != nullptr)
        {
            xTaskNotifyGive(m_taskHandle);
        }
        return true;
    }
    printf("Queue FULL!\r\n");
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
        // Xử lý command trước
        //-----------------------------------
        //-----------------------------------
        // Chờ interrupt
        //-----------------------------------

        ulTaskNotifyTake(pdTRUE,
                         portMAX_DELAY);
        if (m_dataReady)
        {
            // printf("[MAX30102] Interrupt!!!!!\n");
            m_dataReady = false;
            uint8_t int1 = m_sensor.getINT1();
            uint8_t int2 = m_sensor.getINT2();
            // printf("Elapsed Time: %lu ms\n", (unsigned long)xTaskGetTickCount() * portTICK_PERIOD_MS);
            int n = m_sensor.check();
            //     printf("INT1=%02X INT2=%02X RP=%u WP=%u samples=%d\n",
            //    int1,
            //    int2,
            //    m_sensor.getReadPointer(),
            //    m_sensor.getWritePointer(),
            //    n);
        }
        while (xQueueReceive(m_cmdQueue,
                             &cmd,
                             0) == pdPASS)
        {
            // printf("Receive command %d\n", cmd.type);
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
                m_sensor.clearDataBuffer();
                break;

            case SENSOR_CMD_SET_LED_IR:

                m_sensor.setPulseAmplitudeIR(cmd.value);
                m_sensor.clearFIFO();
                m_sensor.clearDataBuffer();
                break;

            case SENSOR_CMD_SET_LED_GREEN:

                m_sensor.setPulseAmplitudeGreen(cmd.value);

                break;

            case SENSOR_CMD_SET_ADC_RANGE:

                m_sensor.setADCRange(cmd.value);
                m_sensor.clearFIFO();
                m_sensor.clearDataBuffer();

                break;

            case SENSOR_CMD_SET_PULSE_WIDTH:

                m_sensor.setPulseWidth(cmd.value);
                m_sensor.clearFIFO();
                m_sensor.clearDataBuffer();
                break;

            case SENSOR_CMD_CLEAR_FIFO:

                m_sensor.clearFIFO();
                m_sensor.clearDataBuffer();
                m_sensor.clearFIFO();

                // printf("After clear: RP=%u WP=%u OVF=%u\r\n",
                //        m_sensor.getReadPointer(),
                //        m_sensor.getWritePointer(),
                //        m_sensor.getOverflowCounter());

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
        // Đọc FIFO
        //-----------------------------------
    }
}

uint32_t MAX30102_manager ::getADCrange()
{
    return m_sensor.getADCrange();
}
