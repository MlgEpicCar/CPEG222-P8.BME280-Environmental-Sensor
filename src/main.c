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

/* Global Variables */
uint32_t initialClock;
uint32_t finalClock;

volatile uint32_t milliseconds = 0;
volatile uint8_t sensor_update_flag = 0;

char buffer[90];

/* SysTick Interrupt Handler */
void SysTick_Handler(void)
{
    milliseconds++;

    if (milliseconds >= 1000)
    {
        milliseconds = 0;
        sensor_update_flag = 1;
    }
}

/* Initialize Humidity LEDs (PD0-PD7) */
void Humidity_LED_Init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;

    // Configure PD0-PD7 as outputs.
    GPIOD->MODER &= ~0x0000FFFFU;
    GPIOD->MODER |=  0x00005555U;

    // Turn all LEDs off.
    GPIOD->ODR &= ~0x00FFU;
}

/* Update Humidity LEDs */
void Humidity_LED_Update(uint32_t humidity)
{
    uint32_t led_count;

    // Each LED represents 12.5% humidity.
    led_count = (humidity * 8U) / 100000U;

    if (led_count > 8U)
    {
        led_count = 8U;
    }

    // Generate LED output mask.
    uint32_t led_mask = (1U << led_count) - 1U;

    // Update PD0-PD7.
    GPIOD->ODR &= ~0x00FFU;
    GPIOD->ODR |= led_mask;
}

/* Convert Celsius to Fahrenheit.
   Input and output are both scaled by 100. */
int32_t Celsius_To_Fahrenheit(int32_t temperature)
{
    return (temperature * 9) / 5 + 3200;
}

int main(void)
{
    BME280_Data sensor;

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

    /* Initialize peripherals. */
    OLED_Init();
    SSD_Init();
    SSD_Timer_Init();
    NeoPixel_Init();

    I2C1_Init();
    Humidity_LED_Init();

    /* Initialize BME280 sensor. */
    OLED_Clear();

    if (!BME280_Init(BME280_ADDR_LOW))
    {
        OLED_Print(3, 0, "BME280 Error!");

        while (1)
        {
            SSD_Refresh();
        }
    }

    OLED_Clear();
    OLED_Print(0, 0, "CPEG222-F26 Project 8");
    OLED_Print(1, 0, "Carlos Munar 11/6/26");

    /* Start SysTick: interrupt every 1 ms. */
    SysTick_Config(SystemCoreClock / 1000U);

    while (1)
    {
        if (sensor_update_flag)
        {
            sensor_update_flag = 0;

            if (BME280_Read(&sensor))
            {
                int32_t temperature = sensor.temperature;

                /* Read S1 and select temperature units. */
                uint8_t use_celsius =
                    (GPIOC->IDR & (1U << S1)) ? 1U : 0U;

                if (!use_celsius)
                {
                    temperature =
                        Celsius_To_Fahrenheit(temperature);
                }

                /* Calculate absolute temperature. */
                uint32_t temp_abs =
                    (temperature < 0) ?
                    (uint32_t)(-temperature) :
                    (uint32_t)temperature;

                /* Display temperature. */
                snprintf(buffer, sizeof(buffer),
                         "Temp:  %s%lu.%02lu %c",
                         (temperature < 0) ? "-" : "",
                         (unsigned long)(temp_abs / 100U),
                         (unsigned long)(temp_abs % 100U),
                         use_celsius ? 'C' : 'F');

                OLED_Print(3, 0, buffer);

                /* Display pressure in hPa. */
                snprintf(buffer, sizeof(buffer),
                         "Press: %lu.%02lu hPa",
                         (unsigned long)(sensor.pressure / 100U),
                         (unsigned long)(sensor.pressure % 100U));

                OLED_Print(4, 0, buffer);

                /* Display humidity. */
                snprintf(buffer, sizeof(buffer),
                         "Hum:   %lu.%02lu %%RH",
                         (unsigned long)(sensor.humidity / 1000U),
                         (unsigned long)((sensor.humidity % 1000U) / 10U));

                OLED_Print(5, 0, buffer);

                /* Update humidity LEDs. */
                Humidity_LED_Update(sensor.humidity);

                /* Update seven-segment display. */
                SSD_DisplayValue(temperature);
            }
            else
            {
                OLED_Print(0, 0, "Sensor Read Error");
            }
        }

        /* Keep refreshing multiplexed SSD. */
        SSD_Refresh();
    }

    return 0;
}