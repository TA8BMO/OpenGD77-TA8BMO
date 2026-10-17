/*
 * Copyright (C) 2020-2024 Roger Clark, VK3KYY / G4KYF
 *
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * 4. Use of this source code or binary releases for commercial purposes is strictly forbidden. This includes, without limitation,
 *    incorporation in a commercial product or incorporation into a product or project which allows commercial use.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include "stm32f4xx_hal.h"
#include "main.h"
#include "interfaces/gpio.h"
#include <stdint.h>

// use 100 + 1 elements so that we can achieve 100% by putting the reset pattern beyond the end of the DMA loop range
#define dimingTableSize  101U
uint32_t dimingPattern[dimingTableSize];

static uint8_t currentDisplayPercentage = 0;

// MD-UV390 Plus 10W backlight investigation:
//   Hypothesis #1 (active-LOW steady DC) was tested on hardware and did NOT light
//   the backlight. Since neither a steady HIGH (original) nor a steady LOW turns
//   it on, the Plus 10W backlight LED driver most likely needs a *switching* (PWM)
//   drive rather than a steady level. Hypothesis #2 (this build): keep the normal
//   active-HIGH polarity but never drive a steady 100% - cap the Plus 10W maximum
//   so the DMA pattern always toggles the pin (see the clamp below). If this still
//   does not work, the backlight may be on a different GPIO/timer on the Plus 10W.
#define BACKLIGHT_INVERTED 0

#if BACKLIGHT_INVERTED
#define BKLIGHT_FULL_ON_STATE   GPIO_PIN_RESET
#define BKLIGHT_FULL_OFF_STATE  GPIO_PIN_SET
#define BKLIGHT_BSRR_ON         ((uint32_t)LCD_BKLIGHT_Pin << 16U)  // reset pin => backlight on
#define BKLIGHT_BSRR_OFF        ((uint32_t)LCD_BKLIGHT_Pin)         // set pin   => backlight off
#else
#define BKLIGHT_FULL_ON_STATE   GPIO_PIN_SET
#define BKLIGHT_FULL_OFF_STATE  GPIO_PIN_RESET
#define BKLIGHT_BSRR_ON         ((uint32_t)LCD_BKLIGHT_Pin)
#define BKLIGHT_BSRR_OFF        ((uint32_t)LCD_BKLIGHT_Pin << 16U)
#endif

#if 0
void gpioInitButtons(void)
{
}

void gpioInitCommon(void)
{
}
#endif

void gpioInitDisplay()
{
	HAL_TIM_Base_Start(&htim1);
	HAL_TIM_OC_Start(&htim1, TIM_CHANNEL_1);
	__HAL_TIM_ENABLE_DMA(&htim1, TIM_DMA_CC1);
}

void gpioSetDisplayBacklightIntensityPercentage(uint8_t intensityPercentage)
{
	if (intensityPercentage > 100U)
	{
		intensityPercentage = 100U;
	}

#if defined(PLATFORM_VARIANT_UV380_PLUS_10W)
	// Plus 10W: never drive a steady 100% DC level. Cap at 99% so the DMA pattern
	// keeps toggling the backlight pin (see backlight hypothesis #2 above).
	if (intensityPercentage > 99U)
	{
		intensityPercentage = 99U;
	}
#endif

	if (intensityPercentage == currentDisplayPercentage)
	{
		return;
	}

	// Switching from dimmed to 100% or 0% value. Stop the DMA
	if ((intensityPercentage == 100U) || (intensityPercentage == 0U))
	{
		HAL_DMA_Abort(&hdma_tim1_ch1); // kill the DMA

		HAL_GPIO_WritePin(LCD_BKLIGHT_GPIO_Port, LCD_BKLIGHT_Pin, ((intensityPercentage == 100U) ? BKLIGHT_FULL_ON_STATE : BKLIGHT_FULL_OFF_STATE));
	}
	else
	{
		// Changing from 100% or 0% to dimmed value. So run the DMA
		if ((currentDisplayPercentage == 100U) || (currentDisplayPercentage == 0U))
		{
			HAL_DMA_Start(&hdma_tim1_ch1, (uint32_t)dimingPattern, (uint32_t)&(LCD_BKLIGHT_GPIO_Port->BSRR), 100U);// 100 steps
		}
		//HAL_GPIO_WritePin(LCD_BKLIGHT_GPIO_Port, LCD_BKLIGHT_Pin, GPIO_PIN_RESET);
	}

	dimingPattern[currentDisplayPercentage] = 0U;// remove the previous pin reset pattern

	dimingPattern[0U] = BKLIGHT_BSRR_ON;// turn on the backlight at the start of the array
	dimingPattern[intensityPercentage] = BKLIGHT_BSRR_OFF;// Turn off the pin at the appropriate position in the array
	currentDisplayPercentage = intensityPercentage;
}

uint8_t gpioGetDisplayBacklightIntensityPercentage(void)
{
	return currentDisplayPercentage;
}

#if 0
void gpioInitFlash(void)
{
}
#endif

void gpioInitKeyboard(void)
{
}

void gpioInitLEDs(void)
{
}

void gpioInitRotarySwitch(void)
{
// config is via the STM32Cube config tool
}

#if 0
void gpioInitC6000Interface(void)
{
}
#endif
