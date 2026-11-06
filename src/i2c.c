#include "i2c.h"

#define I2C_TIMEOUT 200000U

// Assumes APB1 clock = 45 MHz
// (180 MHz system clock with APB1 prescaler of /4)
#define I2C_CLOCK_MHZ 45U
#define I2C_CLOCK     (I2C_CLOCK_MHZ * 1000000U)
#define I2C_SPEED     100000U

#define SCL_PIN 8U   // PB8 = I2C1_SCL
#define SDA_PIN 9U   // PB9 = I2C1_SDA

/* ---------------------------------------------------------
   Busy-wait delay using the Cortex-M4 cycle counter
   --------------------------------------------------------- */

void I2C1_DelayUs(uint32_t us)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;

    uint32_t cycles = us * (SystemCoreClock / 1000000U);
    uint32_t start  = DWT->CYCCNT;

    while ((uint32_t)(DWT->CYCCNT - start) < cycles)
    {
    }
}

/* ---------------------------------------------------------
   Low-level helpers
   --------------------------------------------------------- */

// Wait for an SR1 flag. Fails on NACK / bus error / arbitration loss / timeout.
// On failure a STOP is requested so the bus is released.
static int I2C1_Wait(uint32_t flag)
{
    uint32_t timeout = I2C_TIMEOUT;

    for (;;)
    {
        uint32_t sr1 = I2C1->SR1;

        if (sr1 & flag)
        {
            return I2C_OK;
        }

        if (sr1 & (I2C_SR1_AF | I2C_SR1_BERR | I2C_SR1_ARLO))
        {
            I2C1->SR1 &= ~(I2C_SR1_AF | I2C_SR1_BERR | I2C_SR1_ARLO);
            I2C1->CR1 |= I2C_CR1_STOP;
            return I2C_ERR_NACK;
        }

        if (--timeout == 0)
        {
            I2C1->CR1 |= I2C_CR1_STOP;
            return I2C_ERR_TIMEOUT;
        }
    }
}

// Generate START. A fresh START first waits for the bus to be idle;
// a repeated START (bus already ours) must not.
static int I2C1_Start(uint8_t repeated)
{
    if (!repeated)
    {
        uint32_t timeout = I2C_TIMEOUT;

        while (I2C1->SR2 & I2C_SR2_BUSY)
        {
            if (--timeout == 0)
            {
                return I2C_ERR_BUSY;
            }
        }
    }

    I2C1->CR1 |= I2C_CR1_START;

    return I2C1_Wait(I2C_SR1_SB);
}

// Generate STOP and wait until the hardware has actually sent it.
static void I2C1_Stop(void)
{
    uint32_t timeout = I2C_TIMEOUT;

    I2C1->CR1 |= I2C_CR1_STOP;

    while ((I2C1->CR1 & I2C_CR1_STOP) && --timeout)
    {
    }
}

// Send the 7-bit address + R/W bit and wait for the slave to ACK it.
// ADDR is NOT cleared here (a 1/2-byte read must touch ACK/POS first).
static int I2C1_SendAddress(uint8_t address, uint8_t direction)
{
    I2C1->DR = (uint8_t)((address << 1) | direction);

    return I2C1_Wait(I2C_SR1_ADDR);
}

// Clear ADDR by reading SR1 followed by SR2
static void I2C1_ClearAddr(void)
{
    volatile uint32_t temp = I2C1->SR1;
    temp = I2C1->SR2;
    (void)temp;
}

// Send one byte once the data register is empty
static int I2C1_SendByte(uint8_t byte)
{
    int rc = I2C1_Wait(I2C_SR1_TXE);

    if (rc != I2C_OK)
    {
        return rc;
    }

    I2C1->DR = byte;

    return I2C_OK;
}

// Common ending for every transfer: restore ACK/POS and make sure STOP finished.
static int I2C1_Finish(int rc)
{
    if (rc == I2C_OK)
    {
        rc = I2C1_Wait(I2C_SR1_BTF);
    }

    if (rc == I2C_OK)
    {
        I2C1_Stop();
    }
    else
    {
        // I2C1_Wait() already requested STOP
        uint32_t timeout = I2C_TIMEOUT;

        while ((I2C1->CR1 & I2C_CR1_STOP) && --timeout)
        {
        }
    }

    return rc;
}

/* ---------------------------------------------------------
   Receive 'length' bytes. START and the address phase happen here.
   Uses the sequences from the STM32F4 reference manual for
   1, 2 and >2 byte master reception.
   --------------------------------------------------------- */

static int I2C1_Receive(uint8_t address, uint8_t *data, uint16_t length)
{
    int rc;

    if (length == 1)
    {
        I2C1->CR1 &= ~I2C_CR1_ACK;
    }
    else
    {
        I2C1->CR1 |= I2C_CR1_ACK;

        if (length == 2)
        {
            I2C1->CR1 |= I2C_CR1_POS;
        }
    }

    rc = I2C1_SendAddress(address, 1);

    if (rc == I2C_OK)
    {
        if (length == 1)
        {
            I2C1_ClearAddr();
            I2C1->CR1 |= I2C_CR1_STOP;

            rc = I2C1_Wait(I2C_SR1_RXNE);

            if (rc == I2C_OK)
            {
                data[0] = (uint8_t)I2C1->DR;
            }
        }
        else if (length == 2)
        {
            I2C1_ClearAddr();
            I2C1->CR1 &= ~I2C_CR1_ACK;

            rc = I2C1_Wait(I2C_SR1_BTF);

            if (rc == I2C_OK)
            {
                I2C1->CR1 |= I2C_CR1_STOP;
                data[0] = (uint8_t)I2C1->DR;
                data[1] = (uint8_t)I2C1->DR;
            }
        }
        else
        {
            I2C1_ClearAddr();

            while ((rc == I2C_OK) && (length > 3))
            {
                rc = I2C1_Wait(I2C_SR1_RXNE);

                if (rc == I2C_OK)
                {
                    *data++ = (uint8_t)I2C1->DR;
                    length--;
                }
            }

            // Three bytes left: byte N-2 is in DR, N-1 in the shift register
            if (rc == I2C_OK)
            {
                rc = I2C1_Wait(I2C_SR1_BTF);
            }

            if (rc == I2C_OK)
            {
                I2C1->CR1 &= ~I2C_CR1_ACK;      // NACK the last byte
                *data++ = (uint8_t)I2C1->DR;    // byte N-2

                rc = I2C1_Wait(I2C_SR1_BTF);
            }

            if (rc == I2C_OK)
            {
                I2C1->CR1 |= I2C_CR1_STOP;
                *data++ = (uint8_t)I2C1->DR;    // byte N-1

                rc = I2C1_Wait(I2C_SR1_RXNE);
            }

            if (rc == I2C_OK)
            {
                *data = (uint8_t)I2C1->DR;      // byte N
            }
        }
    }

    // Wait for the STOP to finish, then restore defaults for the next transfer
    {
        uint32_t timeout = I2C_TIMEOUT;

        while ((I2C1->CR1 & I2C_CR1_STOP) && --timeout)
        {
        }
    }

    I2C1->CR1 &= ~I2C_CR1_POS;
    I2C1->CR1 |= I2C_CR1_ACK;

    return rc;
}

/* ---------------------------------------------------------
   Peripheral configuration
   --------------------------------------------------------- */

static void I2C1_Configure(void)
{
    // Reset the peripheral state machine (clears a falsely-set BUSY flag)
    I2C1->CR1 &= ~I2C_CR1_PE;
    I2C1->CR1 |=  I2C_CR1_SWRST;
    I2C1->CR1 &= ~I2C_CR1_SWRST;

    // Peripheral clock frequency in MHz
    I2C1->CR2 = I2C_CLOCK_MHZ;

    // Standard mode: 100 kHz
    I2C1->CCR = I2C_CLOCK / (2U * I2C_SPEED);

    // Maximum rise time for standard mode (1000 ns): FREQ + 1
    I2C1->TRISE = I2C_CLOCK_MHZ + 1U;

    // Enable I2C1 with ACK
    I2C1->CR1 = I2C_CR1_ACK | I2C_CR1_PE;
}

/* ---------------------------------------------------------
   Bus recovery

   If the MCU is reset (or reflashed) in the middle of a transfer, the
   BME280 can be left holding SDA low waiting for clocks that never come.
   The I2C peripheral cannot fix that, and START then never completes.
   Fix: drive SCL by hand until the slave lets go of SDA, send a STOP,
   and only then hand the pins to the peripheral.
   (Same idea as the ST errata workaround for a stuck BUSY flag.)
   --------------------------------------------------------- */

void I2C1_Recover(void)
{
    I2C1->CR1 &= ~I2C_CR1_PE;

    // PB8/PB9: open-drain, pull-up, released (high), general-purpose output
    GPIOB->OTYPER |= (1U << SCL_PIN) | (1U << SDA_PIN);
    GPIOB->PUPDR  &= ~((3U << (SCL_PIN * 2U)) | (3U << (SDA_PIN * 2U)));
    GPIOB->PUPDR  |=  ((1U << (SCL_PIN * 2U)) | (1U << (SDA_PIN * 2U)));
    GPIOB->BSRR    = (1U << SCL_PIN) | (1U << SDA_PIN);
    GPIOB->MODER  &= ~((3U << (SCL_PIN * 2U)) | (3U << (SDA_PIN * 2U)));
    GPIOB->MODER  |=  ((1U << (SCL_PIN * 2U)) | (1U << (SDA_PIN * 2U)));
    I2C1_DelayUs(10);

    // Clock out whatever byte the slave thinks it is still sending
    for (uint8_t i = 0; (i < 9U) && !(GPIOB->IDR & (1U << SDA_PIN)); i++)
    {
        GPIOB->BSRR = (1U << (SCL_PIN + 16U));   // SCL low
        I2C1_DelayUs(5);
        GPIOB->BSRR = (1U << SCL_PIN);           // SCL high
        I2C1_DelayUs(5);
    }

    // Manual STOP: SDA low -> SCL high -> SDA high
    GPIOB->BSRR = (1U << (SDA_PIN + 16U));
    I2C1_DelayUs(5);
    GPIOB->BSRR = (1U << SCL_PIN);
    I2C1_DelayUs(5);
    GPIOB->BSRR = (1U << SDA_PIN);
    I2C1_DelayUs(5);

    // Hand the pins to I2C1 (AF4, open-drain)
    GPIOB->MODER  &= ~((3U << (SCL_PIN * 2U)) | (3U << (SDA_PIN * 2U)));
    GPIOB->MODER  |=  ((2U << (SCL_PIN * 2U)) | (2U << (SDA_PIN * 2U)));

    I2C1_Configure();
}

void I2C1_Init(void)
{
    // Enable GPIOB clock
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;

    // Enable I2C1 clock
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;

    // High speed pins
    GPIOB->OSPEEDR |= (3U << (SCL_PIN * 2U)) | (3U << (SDA_PIN * 2U));

    // AF4 = I2C1 on PB8 (SCL) and PB9 (SDA)
    GPIOB->AFR[1] &= ~((0xFU << ((SCL_PIN - 8U) * 4U)) | (0xFU << ((SDA_PIN - 8U) * 4U)));
    GPIOB->AFR[1] |=  ((4U   << ((SCL_PIN - 8U) * 4U)) | (4U   << ((SDA_PIN - 8U) * 4U)));

    // Free the bus if needed, switch pins to I2C and enable the peripheral
    I2C1_Recover();
}

/* ---------------------------------------------------------
   Public transfer functions
   --------------------------------------------------------- */

int I2C1_Write(uint8_t address, const uint8_t *data, uint16_t length)
{
    int rc = I2C1_Start(0);

    if (rc != I2C_OK)
    {
        return rc;
    }

    rc = I2C1_SendAddress(address, 0);

    if (rc == I2C_OK)
    {
        I2C1_ClearAddr();

        for (uint16_t i = 0; (i < length) && (rc == I2C_OK); i++)
        {
            rc = I2C1_SendByte(data[i]);
        }
    }

    return I2C1_Finish(rc);
}

int I2C1_Read(uint8_t address, uint8_t *data, uint16_t length)
{
    if ((length == 0) || (data == 0))
    {
        return I2C_ERR_NACK;
    }

    int rc = I2C1_Start(0);

    if (rc != I2C_OK)
    {
        return rc;
    }

    return I2C1_Receive(address, data, length);
}

int I2C1_WriteRegister(uint8_t address, uint8_t reg,
                       const uint8_t *data, uint16_t length)
{
    int rc = I2C1_Start(0);

    if (rc != I2C_OK)
    {
        return rc;
    }

    rc = I2C1_SendAddress(address, 0);

    if (rc == I2C_OK)
    {
        I2C1_ClearAddr();

        // Register address, then data
        rc = I2C1_SendByte(reg);

        for (uint16_t i = 0; (i < length) && (rc == I2C_OK); i++)
        {
            rc = I2C1_SendByte(data[i]);
        }
    }

    return I2C1_Finish(rc);
}

int I2C1_ReadRegisters(uint8_t address, uint8_t reg,
                       uint8_t *data, uint16_t length)
{
    if ((length == 0) || (data == 0))
    {
        return I2C_ERR_NACK;
    }

    int rc = I2C1_Start(0);

    if (rc != I2C_OK)
    {
        return rc;
    }

    // Address phase (write) + register pointer
    rc = I2C1_SendAddress(address, 0);

    if (rc == I2C_OK)
    {
        I2C1_ClearAddr();
        rc = I2C1_SendByte(reg);
    }

    if (rc == I2C_OK)
    {
        rc = I2C1_Wait(I2C_SR1_BTF);
    }

    if (rc != I2C_OK)
    {
        return I2C1_Finish(rc);
    }

    // Repeated START, then read the bytes
    rc = I2C1_Start(1);

    if (rc != I2C_OK)
    {
        return I2C1_Finish(rc);
    }

    return I2C1_Receive(address, data, length);
}