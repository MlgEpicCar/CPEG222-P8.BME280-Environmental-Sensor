#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

void Audio_Init(void);
void Audio_SetFrequency(float frequency_hz);    // (re)start tone at a frequency
void Audio_SetEnabled(uint8_t enable);          // start / stop sound from code
void Audio_Stop(void);

/* Project 7 theremin helpers (unused in Project 8) */
void Audio_UpdateSwitch(void);
uint32_t Audio_UpdateDistance(uint32_t distance_hundredths);
float Audio_GetCurrentNote(void);

#endif
