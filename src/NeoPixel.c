#include "stm32f4xx.h"
#include "NeoPixel.h"

#define NUM_LEDS       4U
#define BITS_PER_LED   24U
#define RESET_SLOTS    60U
#define TOTAL_SLOTS    (NUM_LEDS * BITS_PER_LED + RESET_SLOTS)

#define TIMER_ARR      111U   // 112 ticks @ 90 MHz = 1.244 us
#define ZERO_HIGH      36U    // ~400 ns
#define ONE_HIGH       72U    // ~800 ns

static uint8_t  pixel_data[NUM_LEDS][3];
static uint32_t dma_buffer[TOTAL_SLOTS];     // 32-bit entries now

void NeoPixel_Init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_DMA1EN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    // PB11 = TIM2_CH4 (AF1), high speed, no pull
    GPIOB->MODER   &= ~(3U << (11U * 2U));
    GPIOB->MODER   |=  (2U << (11U * 2U));
    GPIOB->OSPEEDR |=  (3U << (11U * 2U));
    GPIOB->PUPDR   &= ~(3U << (11U * 2U));
    GPIOB->AFR[1]  &= ~(0xFU << ((11U - 8U) * 4U));
    GPIOB->AFR[1]  |=  (0x1U << ((11U - 8U) * 4U));

    // TIM2: 90 MHz, PWM mode 1 on CH4 with preload
    TIM2->CR1  = 0;
    TIM2->PSC  = 0;
    TIM2->ARR  = TIMER_ARR;
    TIM2->CCR4 = 0;
    TIM2->CCMR2 &= ~TIM_CCMR2_OC4M;
    TIM2->CCMR2 |= (6U << TIM_CCMR2_OC4M_Pos) | TIM_CCMR2_OC4PE;
    TIM2->CCER  |= TIM_CCER_CC4E;
    TIM2->DIER  &= ~(TIM_DIER_CC4DE | TIM_DIER_UDE);

    // DMA1 Stream 7, Channel 3 (TIM2_UP): mem -> periph, 32-bit, mem increment
    DMA1_Stream7->CR &= ~DMA_SxCR_EN;
    while (DMA1_Stream7->CR & DMA_SxCR_EN) {}

    DMA1_Stream7->PAR  = (uint32_t)&TIM2->CCR4;
    DMA1_Stream7->M0AR = (uint32_t)dma_buffer;
    DMA1_Stream7->NDTR = TOTAL_SLOTS;
    DMA1_Stream7->FCR  = 0;
    DMA1_Stream7->CR   = (3U << DMA_SxCR_CHSEL_Pos) |
                         DMA_SxCR_DIR_0 |
                         DMA_SxCR_MINC  |
                         DMA_SxCR_MSIZE_1 |
                         DMA_SxCR_PSIZE_1;

    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR  = 0;
    TIM2->CR1 |= TIM_CR1_CEN;     // runs forever, CCR4 = 0 keeps the line low

    NeoPixel_Clear();
}

void NeoPixel_SetPixel(uint8_t index, uint8_t red, uint8_t green, uint8_t blue)
{
    if (index >= NUM_LEDS) return;
    pixel_data[index][0] = green;   // GRB order
    pixel_data[index][1] = red;
    pixel_data[index][2] = blue;
}

void NeoPixel_Show(void)
{
    // Stop any previous transfer; no DMA requests while we reload the timer
    DMA1_Stream7->CR &= ~DMA_SxCR_EN;
    while (DMA1_Stream7->CR & DMA_SxCR_EN) {}
    TIM2->DIER &= ~TIM_DIER_UDE;

    // Build the buffer: 24 bits per LED, MSB first, then LOW reset slots
    uint32_t pos = 0;
    for (uint32_t led = 0; led < NUM_LEDS; led++)
        for (uint32_t c = 0; c < 3; c++)
            for (int bit = 7; bit >= 0; bit--)
                dma_buffer[pos++] = (pixel_data[led][c] & (1U << bit)) ? ONE_HIGH : ZERO_HIGH;
    while (pos < TOTAL_SLOTS)
        dma_buffer[pos++] = 0;

    // Force output low and reload timer WITHOUT a DMA request
    TIM2->CCR4 = 0;
    TIM2->EGR  = TIM_EGR_UG;
    TIM2->SR   = 0;

    DMA1->HIFCR = DMA_HIFCR_CTCIF7 | DMA_HIFCR_CHTIF7 | DMA_HIFCR_CTEIF7 |
                  DMA_HIFCR_CDMEIF7 | DMA_HIFCR_CFEIF7;

    DMA1_Stream7->M0AR = (uint32_t)dma_buffer;
    DMA1_Stream7->NDTR = TOTAL_SLOTS;

    DMA1_Stream7->CR |= DMA_SxCR_EN;
    TIM2->CNT   = 0;
    TIM2->DIER |= TIM_DIER_UDE;    // each update event now fetches the next slot

    while (!(DMA1->HISR & DMA_HISR_TCIF7)) {}   // all slots transferred

    DMA1_Stream7->CR &= ~DMA_SxCR_EN;
    while (DMA1_Stream7->CR & DMA_SxCR_EN) {}
    TIM2->DIER &= ~TIM_DIER_UDE;
    TIM2->CCR4 = 0;                // line stays low
}

void NeoPixel_Clear(void)
{
    for (uint32_t i = 0; i < NUM_LEDS; i++)
        pixel_data[i][0] = pixel_data[i][1] = pixel_data[i][2] = 0;
    NeoPixel_Show();
}