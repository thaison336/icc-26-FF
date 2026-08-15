#include "i2c_ctl.h"
#include "stdio.h"
// ÄÃ£ sá»­a láº¡i tÃªn class I2CBus cho liá»n máº¡ch
I2CBus::I2CBus(I2C_TypeDef *i2c)
{
    m_i2c = i2c;
    m_mutex = xSemaphoreCreateMutex();
}

bool I2CBus::transfer(I2C_TransferSeq_TypeDef &seq)
{
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(50)) != pdTRUE)
        return false;

    I2C_TransferReturn_TypeDef status;
    status = I2C_TransferInit(m_i2c, &seq);
    TickType_t start = xTaskGetTickCount();

    while (status == i2cTransferInProgress)
    {
        status = I2C_Transfer(m_i2c);

        // Timeout báº£o vá»‡ chá»‘ng treo (5ms)
        if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(50))
        {
            xSemaphoreGive(m_mutex);
            return false;
        }

        // NhÆ°á»ng CPU cho task khÃ¡c cÃ³ cÃ¹ng má»©c Æ°u tiÃªn
        // taskYIELD();
    }

    xSemaphoreGive(m_mutex);
    if (status != i2cTransferDone)
{
    //printf("I2C status = %d\n", status);
}
    return (status == i2cTransferDone);
}

bool I2CBus::readRegister8(uint8_t addr,
                           uint8_t reg,
                           uint8_t &value)
{
    //printf("[I2CBus] Reading register 0x%02X from address 0x%02X\n", reg, addr);
    return read(addr, reg, &value, 1);
}

bool I2CBus::writeRegister8(uint8_t addr,
                            uint8_t reg,
                            uint8_t value)
{
    // Dá»¯ liá»‡u tx náº±m trÃªn stack nhÆ°ng hÃ m write (vÃ  transfer) 
    // lÃ  Ä‘á»“ng bá»™ (cháº·n cho Ä‘áº¿n khi xong), nÃªn viá»‡c nÃ y hoÃ n toÃ n an toÃ n.
    //printf("[I2CBus] Writing register 0x%02X to address 0x%02X\n", reg, addr);
    uint8_t tx[2] = {reg, value};
    return write(addr, tx, 2);
}

bool I2CBus::read(uint8_t addr,
                  uint8_t reg,
                  uint8_t *data,
                  uint16_t len)
{

    I2C_TransferSeq_TypeDef seq;

    seq.addr = addr << 1;
    seq.flags = I2C_FLAG_WRITE_READ;

    seq.buf[0].data = &reg;
    seq.buf[0].len = 1;

    seq.buf[1].data = data;
    seq.buf[1].len = len;

    return transfer(seq);
}

bool I2CBus::write(uint8_t addr,
                   const uint8_t *data,
                   uint16_t len)
{
    I2C_TransferSeq_TypeDef seq;

    seq.addr = addr << 1;
    seq.flags = I2C_FLAG_WRITE;

    seq.buf[0].data = const_cast<uint8_t *>(data);
    seq.buf[0].len = len;

    return transfer(seq);
}