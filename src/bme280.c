
#include "bme280.h"
#include "i2c.h"

/* ---------------------------------------------------------
   BME280 Calibration Coefficients
   --------------------------------------------------------- */

typedef struct
{
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;

    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;

    uint8_t  dig_H1;
    int16_t  dig_H2;
    uint8_t  dig_H3;
    int16_t  dig_H4;
    int16_t  dig_H5;
    int8_t   dig_H6;

} BME280_Calibration;

/* Private variables */
static BME280_Calibration calib;
static uint8_t sensor_address;
static int32_t t_fine;

/* ---------------------------------------------------------
   Read one byte from a BME280 register
   --------------------------------------------------------- */

static uint8_t BME280_ReadByte(uint8_t reg)
{
    uint8_t value = 0;

    I2C1_ReadRegisters(sensor_address, reg, &value, 1);

    return value;
}

/* ---------------------------------------------------------
   Write one byte to a BME280 register
   --------------------------------------------------------- */

static void BME280_WriteByte(uint8_t reg, uint8_t value)
{
    I2C1_WriteRegister(sensor_address, reg, &value, 1);
}

/* ---------------------------------------------------------
   Read calibration coefficients
   --------------------------------------------------------- */

static void BME280_ReadCalibration(void)
{
    uint8_t a, b, c, d, e, f, g;

    /* Temperature calibration */
    calib.dig_T1 =
        (uint16_t)BME280_ReadByte(0x88) |
        ((uint16_t)BME280_ReadByte(0x89) << 8);

    calib.dig_T2 =
        (int16_t)((uint16_t)BME280_ReadByte(0x8A) |
        ((uint16_t)BME280_ReadByte(0x8B) << 8));

    calib.dig_T3 =
        (int16_t)((uint16_t)BME280_ReadByte(0x8C) |
        ((uint16_t)BME280_ReadByte(0x8D) << 8));

    /* Pressure calibration */
    calib.dig_P1 =
        (uint16_t)BME280_ReadByte(0x8E) |
        ((uint16_t)BME280_ReadByte(0x8F) << 8);

    calib.dig_P2 =
        (int16_t)((uint16_t)BME280_ReadByte(0x90) |
        ((uint16_t)BME280_ReadByte(0x91) << 8));

    calib.dig_P3 =
        (int16_t)((uint16_t)BME280_ReadByte(0x92) |
        ((uint16_t)BME280_ReadByte(0x93) << 8));

    calib.dig_P4 =
        (int16_t)((uint16_t)BME280_ReadByte(0x94) |
        ((uint16_t)BME280_ReadByte(0x95) << 8));

    calib.dig_P5 =
        (int16_t)((uint16_t)BME280_ReadByte(0x96) |
        ((uint16_t)BME280_ReadByte(0x97) << 8));

    calib.dig_P6 =
        (int16_t)((uint16_t)BME280_ReadByte(0x98) |
        ((uint16_t)BME280_ReadByte(0x99) << 8));

    calib.dig_P7 =
        (int16_t)((uint16_t)BME280_ReadByte(0x9A) |
        ((uint16_t)BME280_ReadByte(0x9B) << 8));

    calib.dig_P8 =
        (int16_t)((uint16_t)BME280_ReadByte(0x9C) |
        ((uint16_t)BME280_ReadByte(0x9D) << 8));

    calib.dig_P9 =
        (int16_t)((uint16_t)BME280_ReadByte(0x9E) |
        ((uint16_t)BME280_ReadByte(0x9F) << 8));

    /* Humidity calibration */
    calib.dig_H1 = BME280_ReadByte(0xA1);

    calib.dig_H2 =
        (int16_t)((uint16_t)BME280_ReadByte(0xE1) |
        ((uint16_t)BME280_ReadByte(0xE2) << 8));

    calib.dig_H3 = BME280_ReadByte(0xE3);

    a = BME280_ReadByte(0xE4);
    b = BME280_ReadByte(0xE5);
    c = BME280_ReadByte(0xE6);

    /* H4 and H5 are packed 12-bit signed values. */
    d = (uint8_t)((a << 4) | (b & 0x0F));
    e = (uint8_t)((c << 4) | (b >> 4));

    /* Read as unsigned 12-bit values first. */
    uint16_t raw_H4 = ((uint16_t)a << 4) | (b & 0x0F);
    uint16_t raw_H5 = ((uint16_t)c << 4) | (b >> 4);

    /* Sign extend from 12 bits. */
    calib.dig_H4 = (raw_H4 & 0x0800)
                   ? (int16_t)(raw_H4 - 4096)
                   : (int16_t)raw_H4;

    calib.dig_H5 = (raw_H5 & 0x0800)
                   ? (int16_t)(raw_H5 - 4096)
                   : (int16_t)raw_H5;

    calib.dig_H6 = (int8_t)BME280_ReadByte(0xE7);

    (void)d;
    (void)e;
    (void)f;
    (void)g;
}

/* ---------------------------------------------------------
   Initialize BME280
   --------------------------------------------------------- */

int BME280_Init(uint8_t address)
{
    sensor_address = address;

    /* Check sensor chip ID. */
    if (BME280_ReadByte(BME280_REG_ID) != BME280_CHIP_ID)
    {
        return 0;
    }

    /* Reset sensor. */
    BME280_WriteByte(BME280_REG_RESET, 0xB6);

    /* Allow time for reset. */
    for (volatile uint32_t i = 0; i < 100000U; i++)
    {
        __NOP();
    }

    /* Read factory calibration coefficients. */
    BME280_ReadCalibration();

    /* Humidity oversampling x1. */
    BME280_WriteByte(BME280_REG_CTRL_HUM, 0x01);

    /* Standby time and filter configuration. */
    BME280_WriteByte(BME280_REG_CONFIG, 0x00);

    /*
       Temperature oversampling: x1
       Pressure oversampling: x1
       Normal operating mode
    */
    BME280_WriteByte(BME280_REG_CTRL_MEAS, 0x27);

    return 1;
}

/* ---------------------------------------------------------
   Read and compensate temperature
   Result: degrees Celsius * 100
   --------------------------------------------------------- */

static int32_t BME280_CompensateTemperature(int32_t adc_T)
{
    int32_t var1, var2, temperature;

    var1 = ((((adc_T >> 3) -
              ((int32_t)calib.dig_T1 << 1))) *
              (int32_t)calib.dig_T2) >> 11;

    var2 = (((((adc_T >> 4) -
               (int32_t)calib.dig_T1) *
              ((adc_T >> 4) -
               (int32_t)calib.dig_T1)) >> 12) *
              (int32_t)calib.dig_T3) >> 14;

    t_fine = var1 + var2;

    temperature = (t_fine * 5 + 128) >> 8;

    return temperature;
}

/* ---------------------------------------------------------
   Compensate pressure
   Result: Pascals
   --------------------------------------------------------- */

static uint32_t BME280_CompensatePressure(int32_t adc_P)
{
    int64_t var1, var2, pressure;

    var1 = ((int64_t)t_fine) - 128000;

    var2 = var1 * var1 * calib.dig_P6;
    var2 = var2 + ((var1 * calib.dig_P5) << 17);
    var2 = var2 + (((int64_t)calib.dig_P4) << 35);

    var1 = ((var1 * var1 * calib.dig_P3) >> 8) +
           ((var1 * calib.dig_P2) << 12);

    var1 = (((((int64_t)1) << 47) + var1) *
             calib.dig_P1) >> 33;

    if (var1 == 0)
    {
        return 0;
    }

    pressure = 1048576 - adc_P;

    pressure = (((pressure << 31) - var2) * 3125) / var1;

    var1 = ((int64_t)calib.dig_P9 *
            (pressure >> 13) *
            (pressure >> 13)) >> 25;

    var2 = ((int64_t)calib.dig_P8 * pressure) >> 19;

    pressure = ((pressure + var1 + var2) >> 8) +
               (((int64_t)calib.dig_P7) << 4);

    /* Pressure is in Q24.8 Pa. */
    return (uint32_t)(pressure >> 8);
}

/* ---------------------------------------------------------
   Compensate humidity
   Result: milli-percent RH
   --------------------------------------------------------- */

static uint32_t BME280_CompensateHumidity(int32_t adc_H)
{
    int32_t v;

    v = t_fine - 76800;

    v = (((((adc_H << 14) -
            ((int32_t)calib.dig_H4 << 20) -
            ((int32_t)calib.dig_H5 * v)) + 16384) >> 15) *
         (((((((v * calib.dig_H6) >> 10) *
              (((v * calib.dig_H3) >> 11) + 32768)) >> 10) +
            2097152) * calib.dig_H2 + 8192) >> 14));

    v = v - (((((v >> 15) * (v >> 15)) >> 7) *
               calib.dig_H1) >> 4);

    /* Clamp to 0-100% RH. */
    if (v < 0)
        v = 0;

    if (v > 419430400)
        v = 419430400;

    /* Bosch result is %RH * 1024. */
    return (uint32_t)(((int64_t)(v >> 12) * 1000) / 1024);
}

/* ---------------------------------------------------------
   Read all sensor measurements
   --------------------------------------------------------- */

int BME280_Read(BME280_Data *data)
{
    uint8_t raw[8];

    if (data == 0)
        return 0;

    /* Wait until sensor is not measuring. */
    uint32_t timeout = 100000U;

    while (BME280_ReadByte(BME280_REG_STATUS) & 0x08)
    {
        if (--timeout == 0)
            return 0;
    }

    /*
       Read pressure, temperature and humidity.
       Current I2C driver supports one-byte reads,
       so read each register individually.
    */
    for (uint8_t i = 0; i < 8; i++)
    {
        raw[i] = BME280_ReadByte(BME280_REG_PRESS_MSB + i);
    }

    /* Assemble 20-bit pressure measurement. */
    int32_t adc_P =
        ((int32_t)raw[0] << 12) |
        ((int32_t)raw[1] << 4) |
        (raw[2] >> 4);

    /* Assemble 20-bit temperature measurement. */
    int32_t adc_T =
        ((int32_t)raw[3] << 12) |
        ((int32_t)raw[4] << 4) |
        (raw[5] >> 4);

    /* Assemble 16-bit humidity measurement. */
    int32_t adc_H =
        ((int32_t)raw[6] << 8) |
        raw[7];

    /* Compensate and store measurements. */
    data->temperature = BME280_CompensateTemperature(adc_T);

    data->pressure = BME280_CompensatePressure(adc_P);

    data->humidity = BME280_CompensateHumidity(adc_H);

    return 1;
}
