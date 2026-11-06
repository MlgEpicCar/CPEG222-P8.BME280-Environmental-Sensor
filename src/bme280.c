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
static int last_error = BME280_ERR_NONE;

/* ---------------------------------------------------------
   Register access helpers (0 = success)
   --------------------------------------------------------- */

static int BME280_ReadRegs(uint8_t reg, uint8_t *buf, uint16_t len)
{
    return I2C1_ReadRegisters(sensor_address, reg, buf, len);
}

static int BME280_WriteByte(uint8_t reg, uint8_t value)
{
    return I2C1_WriteRegister(sensor_address, reg, &value, 1);
}

/* ---------------------------------------------------------
   Read calibration coefficients (burst reads)
   --------------------------------------------------------- */

static int BME280_ReadCalibration(void)
{
    uint8_t tp[24];   // 0x88..0x9F : T1-T3, P1-P9
    uint8_t h1;       // 0xA1       : H1
    uint8_t h[7];     // 0xE1..0xE7 : H2-H6

    if (BME280_ReadRegs(0x88, tp, sizeof(tp)) != 0) return 0;
    if (BME280_ReadRegs(0xA1, &h1, 1) != 0)         return 0;
    if (BME280_ReadRegs(0xE1, h, sizeof(h)) != 0)   return 0;

    /* Temperature and pressure: little-endian 16-bit values */
    calib.dig_T1 = (uint16_t)(tp[0]  | (tp[1]  << 8));
    calib.dig_T2 = (int16_t) (tp[2]  | (tp[3]  << 8));
    calib.dig_T3 = (int16_t) (tp[4]  | (tp[5]  << 8));

    calib.dig_P1 = (uint16_t)(tp[6]  | (tp[7]  << 8));
    calib.dig_P2 = (int16_t) (tp[8]  | (tp[9]  << 8));
    calib.dig_P3 = (int16_t) (tp[10] | (tp[11] << 8));
    calib.dig_P4 = (int16_t) (tp[12] | (tp[13] << 8));
    calib.dig_P5 = (int16_t) (tp[14] | (tp[15] << 8));
    calib.dig_P6 = (int16_t) (tp[16] | (tp[17] << 8));
    calib.dig_P7 = (int16_t) (tp[18] | (tp[19] << 8));
    calib.dig_P8 = (int16_t) (tp[20] | (tp[21] << 8));
    calib.dig_P9 = (int16_t) (tp[22] | (tp[23] << 8));

    /* Humidity */
    calib.dig_H1 = h1;
    calib.dig_H2 = (int16_t)(h[0] | (h[1] << 8));
    calib.dig_H3 = h[2];

    /* H4 and H5 are packed, signed 12-bit values:
       H4 = E4[7:0] E5[3:0],  H5 = E6[7:0] E5[7:4] */
    uint16_t raw_H4 = (uint16_t)((h[3] << 4) | (h[4] & 0x0F));
    uint16_t raw_H5 = (uint16_t)((h[5] << 4) | (h[4] >> 4));

    calib.dig_H4 = (raw_H4 & 0x0800) ? (int16_t)(raw_H4 - 4096) : (int16_t)raw_H4;
    calib.dig_H5 = (raw_H5 & 0x0800) ? (int16_t)(raw_H5 - 4096) : (int16_t)raw_H5;

    calib.dig_H6 = (int8_t)h[6];

    return 1;
}

/* ---------------------------------------------------------
   Initialize BME280
   --------------------------------------------------------- */

int BME280_Init(uint8_t address)
{
    const uint8_t candidates[2] =
    {
        address,
        (address == BME280_ADDR_LOW) ? BME280_ADDR_HIGH : BME280_ADDR_LOW
    };

    uint8_t id = 0;
    uint8_t found = 0;
    uint8_t answered = 0;

    last_error = BME280_ERR_I2C;

    /* The BME280 needs 2 ms after power-up; leave plenty of margin. */
    I2C1_DelayUs(10000);

    /* Look for the sensor. If it does not answer, free the bus (a slave left
       mid-transfer by a reset can hold SDA low) and try again. */
    for (uint8_t attempt = 0; (attempt < 3U) && !found; attempt++)
    {
        if (attempt > 0)
        {
            I2C1_Recover();
            I2C1_DelayUs(10000);
        }

        for (uint8_t i = 0; (i < 2U) && !found; i++)
        {
            sensor_address = candidates[i];

            if (BME280_ReadRegs(BME280_REG_ID, &id, 1) == 0)
            {
                answered = 1;

                if (id == BME280_CHIP_ID)
                {
                    found = 1;
                }
            }
        }
    }

    if (!found)
    {
        last_error = answered ? BME280_ERR_ID : BME280_ERR_I2C;
        return 0;
    }

    /* Soft reset, then wait for startup and for the NVM calibration copy
       (status bit 0 = im_update) to finish. */
    if (BME280_WriteByte(BME280_REG_RESET, 0xB6) != 0)
    {
        last_error = BME280_ERR_I2C;
        return 0;
    }

    I2C1_DelayUs(5000);

    for (uint8_t i = 0; i < 50U; i++)
    {
        uint8_t status = 0xFF;

        if ((BME280_ReadRegs(BME280_REG_STATUS, &status, 1) == 0) &&
            !(status & 0x01))
        {
            break;
        }

        I2C1_DelayUs(2000);
    }

    /* Read factory calibration coefficients. */
    if (!BME280_ReadCalibration())
    {
        last_error = BME280_ERR_CALIB;
        return 0;
    }

    /* Humidity oversampling x1 (must be written before CTRL_MEAS). */
    int rc = BME280_WriteByte(BME280_REG_CTRL_HUM, 0x01);

    /* Standby time and filter configuration. */
    rc |= BME280_WriteByte(BME280_REG_CONFIG, 0x00);

    /*
       Temperature oversampling: x1
       Pressure oversampling: x1
       Normal operating mode
    */
    rc |= BME280_WriteByte(BME280_REG_CTRL_MEAS, 0x27);

    /* Read back to confirm the configuration actually landed. */
    uint8_t ctrl_meas = 0;
    rc |= BME280_ReadRegs(BME280_REG_CTRL_MEAS, &ctrl_meas, 1);

    if ((rc != 0) || (ctrl_meas != 0x27))
    {
        last_error = BME280_ERR_CONFIG;
        return 0;
    }

    last_error = BME280_ERR_NONE;
    return 1;
}

int BME280_LastError(void)
{
    return last_error;
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

    /*
       Read pressure, temperature and humidity in ONE burst. The sensor
       freezes its output registers during a burst read, so all three values
       come from the same measurement (reading them byte-by-byte could mix
       two different samples).
    */
    if (BME280_ReadRegs(BME280_REG_PRESS_MSB, raw, sizeof(raw)) != 0)
        return 0;

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

    /* 0x80000 is the power-on value: no conversion has finished yet. */
    if (adc_T == 0x80000)
        return 0;

    /* Compensate and store measurements */
    data->temperature = BME280_CompensateTemperature(adc_T);
    data->pressure = BME280_CompensatePressure(adc_P);
    data->humidity = BME280_CompensateHumidity(adc_H);

    return 1;
}