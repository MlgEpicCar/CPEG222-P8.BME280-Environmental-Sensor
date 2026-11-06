
#ifndef I2C_H
#define I2C_H

#include "stm32f4xx.h"
#include <stdint.h>

// Initialize I2C1
void I2C1_Init(void);

// Send data to an I2C device
void I2C1_Write(uint8_t address, uint8_t *data, uint16_t length);

// Receive data from an I2C device
void I2C1_Read(uint8_t address, uint8_t *data, uint16_t length);

// Write to a specific device register
void I2C1_WriteRegister(uint8_t address, uint8_t reg,
                        uint8_t *data, uint16_t length);

// Read from a specific device register
void I2C1_ReadRegisters(uint8_t address, uint8_t reg,
                        uint8_t *data, uint16_t length);

#endif