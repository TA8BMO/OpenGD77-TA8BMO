/*
 * Copyright (C) 2019      Kai Ludwig, DG4KLU
 * Copyright (C) 2019-2024 Roger Clark, VK3KYY / G4KYF
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
#include "main.h"
#include "interfaces/i2s.h"

volatile bool g_TX_SAI_in_use = false;
volatile bool isSending = false;
volatile bool isReceiving = false;

volatile bool stopOnNextI2SDMAInterrupt = false;

// Optional listener on the receive stream. Set it to have a half-buffer of
// freshly captured samples handed over as soon as the DMA finishes it, without
// disturbing what the sound system does with the same data. Runs in interrupt
// context - keep whatever it points at short.
void (*i2sRxTapCallback)(uint32_t bufNum) = NULL;

uint16_t i2s_Tx_Buffer[NUM_I2S_BUFFERS][2][WAV_BUFFER_SIZE];
uint16_t i2s_Rx_Buffer[NUM_I2S_BUFFERS][2][WAV_BUFFER_SIZE];

static void clearI2SBuffersAndFlags(void)
{
	memset(i2s_Tx_Buffer, 0x00, (NUM_I2S_BUFFERS * 2 * (WAV_BUFFER_SIZE * sizeof(uint16_t))));
	memset(i2s_Rx_Buffer, 0x00, (NUM_I2S_BUFFERS * 2 * (WAV_BUFFER_SIZE * sizeof(uint16_t))));
	stopOnNextI2SDMAInterrupt = false;
	isSending = false;
	isReceiving = false;
	 HAL_I2S_DMAStop(&hi2s3);
	 __HAL_I2SEXT_FLUSH_RX_DR(&hi2s3);
	g_TX_SAI_in_use = false;
	soundInit();
}

void HAL_I2SEx_TxRxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
	if (!stopOnNextI2SDMAInterrupt)
	{
		if (isSending)
		{
			g_TX_SAI_in_use = soundRefillData(0);
		}
		else if(isReceiving)
		{
			soundReceiveRefillData(0);
		}

		if (i2sRxTapCallback != NULL)
		{
			i2sRxTapCallback(0);
		}
	}
	else
	{
		clearI2SBuffersAndFlags();
	}
}

void HAL_I2SEx_TxRxCpltCallback(I2S_HandleTypeDef *hi2s)
{
	if (!stopOnNextI2SDMAInterrupt)
	{
		if (isSending)
		{
			g_TX_SAI_in_use = soundRefillData(1);
		}
		else if(isReceiving)
		{
			soundReceiveRefillData(1);
		}

		if (i2sRxTapCallback != NULL)
		{
			i2sRxTapCallback(1);
		}
	}
	else
	{
		clearI2SBuffersAndFlags();
	}
}

void I2SStartDMA(uint16_t *txbuff, uint16_t *rxbuff, size_t bufferLen)
{
	// Need to wait for WS line has just toggled from logic low to high
	while(!HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15));
	while(HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15));
	while(!HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15));
	stopOnNextI2SDMAInterrupt = false;
	HAL_I2SEx_TransmitReceive_DMA(&hi2s3, txbuff, rxbuff, bufferLen);
}

void I2SReset(void)
{

}

void I2STerminateTransfers(void)
{
	stopOnNextI2SDMAInterrupt = true;
}

void init_I2S(void)
{

}
