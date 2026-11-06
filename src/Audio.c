#include "stm32f4xx.h"
#include <stdint.h>

/*
 * CPEG222 Project 7 - Theremin
 * Audio generation using:
 * DAC1 Channel 1 (PA4)
 * TIM6 TRGO
 * DMA1 Stream 5, Channel 7
 *
 * Amplifier shutdown: PB6
 * S1 audio switch: PC8
 *
 * Assumes TIM6 clock = 90 MHz.
 */

// Audio hardware pins
#define AUDIO_SD_PIN    6
#define AUDIO_SWITCH    8

// DAC waveform configuration
#define SINE_TABLE_SIZE 64
#define TIM6_CLOCK      90000000U

// 12-bit sine wave centered around 2048
static const uint16_t sine_table[SINE_TABLE_SIZE] =
{
    2048, 2249, 2447, 2642, 2831, 3013, 3185, 3347,
    3495, 3630, 3750, 3853, 3939, 4007, 4056, 4085,
    4095, 4085, 4056, 4007, 3939, 3853, 3750, 3630,
    3495, 3347, 3185, 3013, 2831, 2642, 2447, 2249,
    2048, 1847, 1649, 1454, 1265, 1083, 911, 749,
    601, 466, 346, 243, 157, 89, 40, 11,
    1, 11, 40, 89, 157, 243, 346, 466,
    601, 749, 911, 1083, 1265, 1454, 1649, 1847
};

static uint8_t audio_enabled = 0;
static uint32_t current_frequency = 440;


// --------------------------------------------------
// GPIO INITIALIZATION
// --------------------------------------------------

static void Audio_GPIO_Init(void)
{
    // Enable GPIOA, GPIOB, and GPIOC clocks
    RCC->AHB1ENR |= (1U << 0);
    RCC->AHB1ENR |= (1U << 1);
    RCC->AHB1ENR |= (1U << 2);

    // PA4: Analog mode for DAC output
    GPIOA->MODER |= (3U << (4 * 2));
    GPIOA->PUPDR &= ~(3U << (4 * 2));

    // PB6: Output for amplifier shutdown
    GPIOB->MODER &= ~(3U << (AUDIO_SD_PIN * 2));
    GPIOB->MODER |=  (1U << (AUDIO_SD_PIN * 2));

    // Start with amplifier OFF (shutdown active-low)
    GPIOB->BSRR = (1U << (AUDIO_SD_PIN + 16));

    // PC8: Input for S1
    GPIOC->MODER &= ~(3U << (AUDIO_SWITCH * 2));
    GPIOC->PUPDR &= ~(3U << (AUDIO_SWITCH * 2));
}


// --------------------------------------------------
// DMA INITIALIZATION
// --------------------------------------------------

static void Audio_DMA_Init(void)
{
    // Enable DMA1 clock
    RCC->AHB1ENR |= (1U << 21);

    // Disable DMA1 Stream 5 before configuring
    DMA1_Stream5->CR &= ~DMA_SxCR_EN;

    while (DMA1_Stream5->CR & DMA_SxCR_EN)
    {
        // Wait for stream to disable
    }

    // Clear Stream 5 interrupt flags
    DMA1->HIFCR = (1U << 6)  |
                  (1U << 8)  |
                  (1U << 9)  |
                  (1U << 10) |
                  (1U << 11);

    // Peripheral address: DAC data holding register
    DMA1_Stream5->PAR = (uint32_t)&DAC->DHR12R1;

    // Memory address: sine wave table
    DMA1_Stream5->M0AR = (uint32_t)sine_table;

    // Number of samples
    DMA1_Stream5->NDTR = SINE_TABLE_SIZE;

    DMA1_Stream5->CR = 0;

    // Channel 7
    DMA1_Stream5->CR |= (7U << 25);

    // Memory-to-peripheral direction
    DMA1_Stream5->CR |= (1U << 6);

    // Circular mode
    DMA1_Stream5->CR |= (1U << 8);

    // Memory increment
    DMA1_Stream5->CR |= (1U << 10);

    // Peripheral size: 16 bits
    DMA1_Stream5->CR |= (1U << 11);

    // Memory size: 16 bits
    DMA1_Stream5->CR |= (1U << 13);

    // High priority
    DMA1_Stream5->CR |= (2U << 16);

    // Direct mode enabled (FIFO disabled)
    DMA1_Stream5->FCR = 0;
}


// --------------------------------------------------
// DAC INITIALIZATION
// --------------------------------------------------

static void Audio_DAC_Init(void)
{
    // Enable DAC peripheral clock
    RCC->APB1ENR |= (1U << 29);

    // Disable DAC channel 1 before configuring
    DAC->CR &= ~1U;

    // Set DAC output to midpoint
    DAC->DHR12R1 = 2048;

    /*
     * DAC channel 1:
     * TEN1 = 1      Timer trigger enabled
     * TSEL1 = 000   TIM6_TRGO
     * DMAEN1 = 1    DMA enabled
     * EN1 = 1       DAC enabled
     */

    DAC->CR = (1U << 2) |   // TEN1
              (1U << 12) |  // DMAEN1
              (1U << 0);    // EN1
}


// --------------------------------------------------
// TIM6 INITIALIZATION
// --------------------------------------------------

static void Audio_TIM6_Init(void)
{
    // Enable TIM6 clock
    RCC->APB1ENR |= (1U << 4);

    // Stop timer during configuration
    // ARPE: ARR changes take effect at the next update (no glitch when sliding pitch)
    TIM6->CR1 = TIM_CR1_ARPE;

    // No prescaling
    TIM6->PSC = 0;

    // Trigger output on update event (TRGO)
    TIM6->CR2 &= ~(7U << 4);
    TIM6->CR2 |=  (2U << 4);

    // Timer starts with a default frequency of 440 Hz
    TIM6->ARR = (TIM6_CLOCK / (SINE_TABLE_SIZE * 440U)) - 1U;

    TIM6->CNT = 0;

    // Generate an update event to load registers
    TIM6->EGR = TIM_EGR_UG;

    // Clear update flag
    TIM6->SR &= ~TIM_SR_UIF;
}


// --------------------------------------------------
// AUDIO INITIALIZATION
// --------------------------------------------------

void Audio_Init(void)
{
    Audio_GPIO_Init();
    Audio_DMA_Init();
    Audio_DAC_Init();
    Audio_TIM6_Init();

    audio_enabled = 0;
    current_frequency = 440;

    // Amplifier starts OFF
    GPIOB->BSRR = (1U << (AUDIO_SD_PIN + 16));
}


// --------------------------------------------------
// PLAY A FREQUENCY
// --------------------------------------------------

void Audio_SetFrequency(float frequency_hz)
{
    current_frequency = frequency_hz;

    // Do not start audio if S1 is OFF
    if (!audio_enabled)
    {
        return;
    }

    // Turn amplifier OFF before changing the waveform
    GPIOB->BSRR = (1U << (AUDIO_SD_PIN + 16));

    // Stop timer and DMA
    TIM6->CR1 &= ~TIM_CR1_CEN;

    DMA1_Stream5->CR &= ~DMA_SxCR_EN;

    while (DMA1_Stream5->CR & DMA_SxCR_EN)
    {
        // Wait for DMA to stop
    }

    // Clear DMA flags
    DMA1->HIFCR = (1U << 6)  |
                  (1U << 8)  |
                  (1U << 9)  |
                  (1U << 10) |
                  (1U << 11);

    // Reset DMA memory transfer
    DMA1_Stream5->M0AR = (uint32_t)sine_table;
    DMA1_Stream5->NDTR = SINE_TABLE_SIZE;

    // Calculate timer reload value
    // Sample frequency = 64 * desired audio frequency
    uint32_t reload =
        (TIM6_CLOCK / (SINE_TABLE_SIZE * frequency_hz)) - 1U;

    TIM6->ARR = reload;
    TIM6->CNT = 0;

    // Reload timer registers
    TIM6->EGR = TIM_EGR_UG;
    TIM6->SR &= ~TIM_SR_UIF;

    // Enable DMA
    DMA1_Stream5->CR |= DMA_SxCR_EN;

    // Start timer
    TIM6->CR1 |= TIM_CR1_CEN;

    // Allow waveform to begin before enabling amplifier
    for (volatile uint32_t i = 0; i < 10000; i++)
    {
        __NOP();
    }

    // Enable amplifier
    GPIOB->BSRR = (1U << AUDIO_SD_PIN);
}

// --------------------------------------------------
// STOP AUDIO
// --------------------------------------------------

void Audio_Stop(void)
{
    // Shut down amplifier first
    GPIOB->BSRR = (1U << (AUDIO_SD_PIN + 16));

    // Stop timer
    TIM6->CR1 &= ~TIM_CR1_CEN;

    // Stop DMA
    DMA1_Stream5->CR &= ~DMA_SxCR_EN;

    while (DMA1_Stream5->CR & DMA_SxCR_EN)
    {
        // Wait for DMA to stop
    }

    // Set DAC output to midpoint
    DAC->DHR12R1 = 2048;
}


// --------------------------------------------------
// ENABLE / DISABLE AUDIO FROM CODE (added for Project 8)
// In Project 8 S1 selects C/F, so audio is no longer gated by the switch.
// --------------------------------------------------

void Audio_SetEnabled(uint8_t enable)
{
    if (enable && !audio_enabled)
    {
        audio_enabled = 1;
        Audio_SetFrequency(current_frequency);
    }
    else if (!enable && audio_enabled)
    {
        audio_enabled = 0;
        Audio_Stop();
    }
}


// --------------------------------------------------
// CHECK S1 SWITCH
// (Project 7 only - do NOT call in Project 8, S1 is the temperature-unit switch)
// --------------------------------------------------

void Audio_UpdateSwitch(void)
{
    uint8_t switch_state =
        (GPIOC->IDR & (1U << AUDIO_SWITCH)) ? 1 : 0;

    if (switch_state && !audio_enabled)
    {
        audio_enabled = 1;
        Audio_SetFrequency(current_frequency);
    }
    else if (!switch_state && audio_enabled)
    {
        audio_enabled = 0;
        Audio_Stop();
    }
}

// --------------------------------------------------
// DISTANCE TO FREQUENCY MAPPING
// --------------------------------------------------
static int current_note = -1;
static const float notes[13] =
        {261.63,277.18,293.66,311.13,329.63,349.23,369.99,392.00,415.30,440.00,466.16,493.88,0.0};

uint32_t Audio_UpdateDistance(uint32_t distance_hundredths)
{
    uint32_t inches = distance_hundredths / 100U;
    if (inches < 4U) inches = 4U;
    if (inches > 15U) {
        Audio_Stop();}

    int new_note = (int)(inches - 4U);
    if (new_note != current_note)
    {
        current_note = new_note;
        Audio_SetFrequency(notes[new_note]);
    }
    return (uint32_t)current_note;
}

float Audio_GetCurrentNote(void)
{
    return notes[current_note];
}