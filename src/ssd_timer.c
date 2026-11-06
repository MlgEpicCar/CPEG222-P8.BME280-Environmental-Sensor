#include "stm32f4xx.h"
#include "ssd_timer.h"
#include "ssd.h"

void SSD_Timer_Init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM5EN; // Enable TIM5 clock

    TIM5->CR1 = 0; // Stop timer while configuring
    TIM5->CNT = 0; // Set the "counted-to" number to zero

    TIM5->PSC = 8999;  // Prescalar
    TIM5->ARR = 99;    // Period
    // RECALL THAT TO CONVERT Hz to Seconds, you divide 1 by it (or like the inverse)
    // So in this case 1/100 Seconds is 10 Milliseconds (The right amount for this project) 

    TIM5->EGR = TIM_EGR_UG; // Applies the configuration that was just set

    TIM5->DIER |= TIM_DIER_UIE; // Allows TIM5's to repquest interrupts
    TIM5->SR = 0; // Clears any prexisting flags (which might be leftover i guess)

    NVIC_SetPriority(TIM5_IRQn, 1); // Set's interupts from TIM5 to high priority (lower number = higher priority)
    NVIC_EnableIRQ(TIM5_IRQn); // Allows the proccessor to accept interepts from TIM5 (cus having line 19 wasn't enough, they also needed 23)

    TIM5->CR1 |= TIM_CR1_CEN; // Starts the timer
}

void TIM5_IRQHandler(void)
{
    if (TIM5->SR & TIM_SR_UIF) // Basically "if we get a timer intereupt"
    {
        TIM5->SR &= ~TIM_SR_UIF; // Clears the flag so another one can be recieved

        SSD_Refresh(); // It's in the name gng :sob:
    }
}