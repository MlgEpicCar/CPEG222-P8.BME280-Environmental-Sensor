#ifndef BME280_H
#define BME280_H

#include "stm32f4xx.h"
#include <stdint.h>

/* BME280 I2C addresses */
#define BME280_ADDR_LOW   0x76
#define BME280_ADDR_HIGH  0x77

/* BME280 register addresses */
#define BME280_REG_ID         0xD0
#define BME280_REG_RESET      0xE0
#define BME280_REG_CTRL_HUM   0xF2
#define BME280_REG_STATUS     0xF3
#define BME280_REG_CTRL_MEAS  0xF4
#define BME280_REG_CONFIG     0xF5
#define BME280_REG_PRESS_MSB  0xF7

#define BME280_CHIP_ID        0x60

/* Error codes reported by BME280_LastError() after BME280_Init() fails */
#define BME280_ERR_NONE       0
#define BME280_ERR_I2C        1   // sensor never answered on the bus
#define BME280_ERR_ID         2   // something answered, but not a BME280
#define BME280_ERR_CALIB      3   // could not read calibration data
#define BME280_ERR_CONFIG     4   // configuration did not stick

/* Sensor measurement results */
typedef struct
{
    int32_t temperature;       // Temperature in degrees C * 100
    uint32_t pressure;         // Pressure in Pa
    uint32_t humidity;         // Relative humidity in milli-percent

} BME280_Data;

/* Public functions */
int BME280_Init(uint8_t address);   // returns 1 on success, 0 on failure
int BME280_Read(BME280_Data *data); // returns 1 on success, 0 on failure
int BME280_LastError(void);         // BME280_ERR_* code from the last Init

#endif