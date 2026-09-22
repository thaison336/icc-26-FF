#ifndef I2C_BUS_H
#define I2C_BUS_H

#if defined(__cplusplus)
extern "C" {
#endif

#include "em_i2c.h"    // Thay sl_i2cspm.h bằng em_i2c.h
#include "FreeRTOS.h"
#include "semphr.h"

#if defined(__cplusplus)
}
#endif

class I2CBus
{
public:
    // Sửa I2CSPM_TypeDef thành I2C_TypeDef
    explicit I2CBus(I2C_TypeDef *i2c);

    bool writeRegister8(uint8_t addr, uint8_t reg, uint8_t value);
    bool readRegister8(uint8_t addr, uint8_t reg, uint8_t &value);
    bool write(uint8_t addr, const uint8_t *data, uint16_t len);
    bool read(uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);

private:
    bool transfer(I2C_TransferSeq_TypeDef &seq);
    
    I2C_TypeDef *m_i2c;
    SemaphoreHandle_t m_mutex;
};

extern I2CBus g_i2c0_bus;

#endif