
#include "i2c.h"

#define I2C_TIMEOUT       1000000U

// Assumes APB1 clock = 45 MHz
// (180 MHz system clock with APB1 prescaler of /4)
#define I2C_CLOCK         45000000U
#define I2C_SPEED         100000U

static void I2C1_Wait(volatile uint32_t *reg, uint32_t flag)
{
    uint32_t timeout = I2C_TIMEOUT;

    while (((*reg) & flag) == 0)
    {
        if (--timeout == 0)
        {
            I2C1->CR1 |= I2C_CR1_STOP;
            return;
        }
    }
}

static void I2C1_Start(void)
{
    I2C1->CR1 |= I2C_CR1_START;

    I2C1_Wait(&I2C1->SR1, I2C_SR1_SB);
}

static void I2C1_Stop(void)
{
    I2C1->CR1 |= I2C_CR1_STOP;
}

static void I2C1_SendAddress(uint8_t address, uint8_t direction)
{
    I2C1->DR = (address << 1) | direction;

    I2C1_Wait(&I2C1->SR1, I2C_SR1_ADDR);

    // Clear ADDR flag by reading SR1 followed by SR2
    volatile uint32_t temp = I2C1->SR1;
    temp = I2C1->SR2;
    (void)temp;
}

void I2C1_Init(void)
{
    // Enable GPIOB clock
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;

    // Enable I2C1 clock
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;

    // PB8 = SCL, PB9 = SDA
    // Alternate function mode
    GPIOB->MODER &= ~((3U << (8 * 2)) | (3U << (9 * 2)));
    GPIOB->MODER |=  ((2U << (8 * 2)) | (2U << (9 * 2)));

    // Open-drain output
    GPIOB->OTYPER |= (1U << 8) | (1U << 9);

    // High speed
    GPIOB->OSPEEDR |= (3U << (8 * 2)) | (3U << (9 * 2));

    // Enable internal pull-ups
    GPIOB->PUPDR &= ~((3U << (8 * 2)) | (3U << (9 * 2)));
    GPIOB->PUPDR |=  ((1U << (8 * 2)) | (1U << (9 * 2)));

    // AF4 = I2C1
    GPIOB->AFR[1] &= ~((0xFU << 0) | (0xFU << 4));
    GPIOB->AFR[1] |=  ((4U << 0) | (4U << 4));

    // Reset I2C1 peripheral
    RCC->APB1RSTR |= RCC_APB1RSTR_I2C1RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_I2C1RST;

    // Disable I2C before configuration
    I2C1->CR1 &= ~I2C_CR1_PE;

    // Peripheral clock frequency in MHz
    I2C1->CR2 = 45U;

    // Standard mode: 100 kHz
    I2C1->CCR = I2C_CLOCK / (2U * I2C_SPEED);

    // Maximum rise time for standard mode
    I2C1->TRISE = 46U;

    // Enable I2C1
    I2C1->CR1 |= I2C_CR1_PE;
}

void I2C1_Write(uint8_t address, uint8_t *data, uint16_t length)
{
    I2C1_Start();

    I2C1_SendAddress(address, 0);

    for (uint16_t i = 0; i < length; i++)
    {
        I2C1->DR = data[i];

        I2C1_Wait(&I2C1->SR1, I2C_SR1_TXE);
    }

    I2C1_Wait(&I2C1->SR1, I2C_SR1_BTF);

    I2C1_Stop();
}

void I2C1_Read(uint8_t address, uint8_t *data, uint16_t length)
{
    if (length == 0)
        return;

    I2C1_Start();

    if (length == 1)
    {
        // Disable ACK before receiving one byte
        I2C1->CR1 &= ~I2C_CR1_ACK;

        I2C1_SendAddress(address, 1);

        I2C1_Stop();

        I2C1_Wait(&I2C1->SR1, I2C_SR1_RXNE);

        data[0] = I2C1->DR;
    }
    else
    {
        I2C1->CR1 |= I2C_CR1_ACK;

        I2C1_SendAddress(address, 1);

        uint16_t remaining = length;

        while (remaining > 3)
        {
            I2C1_Wait(&I2C1->SR1, I2C_SR1_RXNE);

            *data++ = I2C1->DR;
            remaining--;
        }

        // Three bytes remain
        I2C1_Wait(&I2C1->SR1, I2C_SR1_BTF);

        // NACK the final byte
        I2C1->CR1 &= ~I2C_CR1_ACK;

        *data++ = I2C1->DR;
        remaining--;

        I2C1_Wait(&I2C1->SR1, I2C_SR1_BTF);

        I2C1_Stop();

        *data++ = I2C1->DR;
        remaining--;

        *data = I2C1->DR;
    }

    // Restore ACK for future reads
    I2C1->CR1 |= I2C_CR1_ACK;
}

void I2C1_WriteRegister(uint8_t address, uint8_t reg,
                        uint8_t *data, uint16_t length)
{
    I2C1_Start();

    I2C1_SendAddress(address, 0);

    // Send register address
    I2C1->DR = reg;
    I2C1_Wait(&I2C1->SR1, I2C_SR1_TXE);

    // Send data
    for (uint16_t i = 0; i < length; i++)
    {
        I2C1->DR = data[i];
        I2C1_Wait(&I2C1->SR1, I2C_SR1_TXE);
    }

    I2C1_Wait(&I2C1->SR1, I2C_SR1_BTF);

    I2C1_Stop();
}


void I2C1_ReadRegisters(uint8_t address, uint8_t reg,
                       uint8_t *data, uint16_t length)
{
    if (length != 1 || data == 0)
        return;

    // START
    I2C1_Start();

    // Send device address (write)
    I2C1_SendAddress(address, 0);

    // Send register address
    I2C1->DR = reg;
    I2C1_Wait(&I2C1->SR1, I2C_SR1_TXE);
    I2C1_Wait(&I2C1->SR1, I2C_SR1_BTF);

    // Repeated START
    I2C1_Start();

    // Send device address (read)
    I2C1->DR = (address << 1) | 1;

    I2C1_Wait(&I2C1->SR1, I2C_SR1_ADDR);

    // Disable ACK BEFORE clearing ADDR
    I2C1->CR1 &= ~I2C_CR1_ACK;

    // Clear ADDR
    volatile uint32_t temp = I2C1->SR1;
    temp = I2C1->SR2;
    (void)temp;

    // STOP
    I2C1_Stop();

    // Receive byte
    I2C1_Wait(&I2C1->SR1, I2C_SR1_RXNE);

    *data = I2C1->DR;

    // Restore ACK
    I2C1->CR1 |= I2C_CR1_ACK;
}
