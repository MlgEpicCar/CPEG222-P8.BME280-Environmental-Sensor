#ifndef I2C_H
#define I2C_H

#include "stm32f4xx.h"
#include <stdint.h>

/* All transfer functions return 0 on success and a negative value on failure
   (NACK, bus error, arbitration loss, timeout, or bus stuck busy). */
#define I2C_OK            0
#define I2C_ERR_NACK     (-1)   // NACK / bus error / arbitration lost
#define I2C_ERR_TIMEOUT  (-2)   // peripheral never reached the expected state
#define I2C_ERR_BUSY     (-3)   // bus stuck busy (SDA or SCL held low)

void I2C1_Init(void);       // Initialize I2C1 (includes bus recovery)
void I2C1_Recover(void);    // Free a stuck bus, then reset and reconfigure I2C1
void I2C1_DelayUs(uint32_t us); // Busy-wait delay (DWT cycle counter)

int I2C1_Write(uint8_t address, const uint8_t *data, uint16_t length);                        // Send data to an I2C device
int I2C1_Read(uint8_t address, uint8_t *data, uint16_t length);                               // Receive data from an I2C device
int I2C1_WriteRegister(uint8_t address, uint8_t reg, const uint8_t *data, uint16_t length);   // Write to a specific device register
int I2C1_ReadRegisters(uint8_t address, uint8_t reg, uint8_t *data, uint16_t length);         // Read length bytes starting at a device register

#endif