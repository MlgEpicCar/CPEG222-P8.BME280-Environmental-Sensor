// ****************************************************************
// * Carlos Munar
// * Project 8 - BME280 Environmental Sensor - 11/6/2026
// ****************************************************************

#include "stm32f4xx.h"
#include "OLED.h"
#include "ssd.h"
#include "ssd_timer.h"
#include "NeoPixel.h"
#include "SysClock180MHz.h"
#include "i2c.h"
#include "bme280.h"

#include <stdint.h>
#include <stdio.h>

/* Switch Definitions */
#define S1 8
#define SWITCH_PORT GPIOC

/* BME280 Definitions */
#define BME280_ADDRESS 0x76
#define BME280_CHIP_ID_REGISTER 0xD0
#define BME280_EXPECTED_ID 0x60

/* Global Variables */
uint32_t initialClock;
uint32_t finalClock;

char buffer[90];

int main(void)
{
    uint8_t chip_id = 0;

    /* Enable Cortex-M4 floating-point unit. */
    SCB->CPACR |= (0xFU << 20U);
    __DSB();
    __ISB();

    /* Configure system clock to 180 MHz. */
    SystemCoreClockUpdate();
    initialClock = SystemCoreClock;

    SystemClock_Config();

    SystemCoreClockUpdate();
    finalClock = SystemCoreClock;

    /* Configure S1 (PC8) as input. */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;

    SWITCH_PORT->MODER &= ~(3U << (S1 * 2U));
    SWITCH_PORT->PUPDR &= ~(3U << (S1 * 2U));

    /* Initialize existing peripherals. */
    OLED_Init();
    SSD_Init();
    SSD_Timer_Init();
    NeoPixel_Init();
    I2C1_Init();

    BME280_Data sensor;

    if (!BME280_Init(BME280_ADDR_LOW))
    {
        OLED_Print(3, 0, "BME280 Error!");
        while (1);
    }

    OLED_Clear();
    OLED_Print(0, 0, "Environmental Data");

    if (BME280_Read(&sensor))
    {
        int32_t temp = sensor.temperature;

        // Convert temperature to a positive magnitude
        uint32_t temp_abs = (temp < 0) ? -temp : temp;

        // Display temperature
        snprintf(buffer, sizeof(buffer),
                "Temp: %s%lu.%02lu C",
                (temp < 0) ? "-" : "",
                (unsigned long)(temp_abs / 100),
                (unsigned long)(temp_abs % 100));

        OLED_Print(2, 0, buffer);

        // Display humidity
        snprintf(buffer, sizeof(buffer),
                "RH: %lu.%02lu %%",
                (unsigned long)(sensor.humidity / 1000),
                (unsigned long)((sensor.humidity % 1000) / 10));

        OLED_Print(3, 0, buffer);

        // Convert pressure from Pa to hPa
        snprintf(buffer, sizeof(buffer),
                "Pressure: %lu.%02lu hPa",
                (unsigned long)(sensor.pressure / 100),
                (unsigned long)(sensor.pressure % 100));

        OLED_Print(4, 0, buffer);
    }
    else
    {
        OLED_Print(2, 0, "Sensor Read Error");
    }

    while (1)
    {
        
        if (BME280_Read(&sensor))
    {
        int32_t temp = sensor.temperature;

        // Convert temperature to a positive magnitude
        uint32_t temp_abs = (temp < 0) ? -temp : temp;

        // Display temperature
        snprintf(buffer, sizeof(buffer),
                "Temp: %s%lu.%02lu C",
                (temp < 0) ? "-" : "",
                (unsigned long)(temp_abs / 100),
                (unsigned long)(temp_abs % 100));

        OLED_Print(2, 0, buffer);

        // Display humidity
        snprintf(buffer, sizeof(buffer),
                "RH: %lu.%02lu %%",
                (unsigned long)(sensor.humidity / 1000),
                (unsigned long)((sensor.humidity % 1000) / 10));

        OLED_Print(3, 0, buffer);

        // Convert pressure from Pa to hPa
        snprintf(buffer, sizeof(buffer),
                "Pressure: %lu.%02lu hPa",
                (unsigned long)(sensor.pressure / 100),
                (unsigned long)(sensor.pressure % 100));

        OLED_Print(4, 0, buffer);
    }
    else
    {
        OLED_Print(2, 0, "Sensor Read Error");
    }
        SSD_Refresh();
    }

    return 0;
}