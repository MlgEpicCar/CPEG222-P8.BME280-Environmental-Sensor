#ifndef NEOPIXEL_H
#define NEOPIXEL_H

#include <stdint.h>

void NeoPixel_Init(void);
void NeoPixel_SetPixel(uint8_t index, uint8_t red,
                       uint8_t green, uint8_t blue);
void NeoPixel_Show(void);
void NeoPixel_Clear(void);

#endif