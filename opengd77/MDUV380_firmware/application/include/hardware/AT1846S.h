/*
 * Copyright (C) 2019      Kai Ludwig, DG4KLU
 * Copyright (C) 2020-2024 Roger Clark, VK3KYY / G4KYF
 *                         Daniel Caujolle-Bert, F1RMB
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

#ifndef _OPENGD77_AT1846S_H_
#define _OPENGD77_AT1846S_H_

#include "main.h"

#define AT1846_BYTES_PER_COMMAND 3
#define BANDWIDTH_12P5KHZ false
#define BANDWIDTH_25KHZ true

#define AT1846_VOICE_CHANNEL_NONE   0x00
#define AT1846_VOICE_CHANNEL_TONE1  0x10
#define AT1846_VOICE_CHANNEL_TONE2  0x20
#define AT1846_VOICE_CHANNEL_DTMF   0x30
#define AT1846_VOICE_CHANNEL_MIC    0x40

bool radioWriteReg2byte(uint8_t reg, uint8_t val1, uint8_t val2);
void radioSetFMRxAudioGain(int8_t volume);
bool radioReadReg2byte(uint8_t reg, uint8_t *val1, uint8_t *val2);
bool radioSetClearReg2byteWithMask(uint8_t reg, uint8_t mask1, uint8_t mask2, uint8_t val1, uint8_t val2);
void I2C_AT1846S_send_Settings(const uint8_t settings[][AT1846_BYTES_PER_COMMAND], int numSettings);
void I2C_AT1846_set_register_with_mask(uint8_t reg, uint16_t mask, uint16_t value, uint8_t shift);

void AT1846sInit(void);
void AT1846sPostInit(void);
void AT1846sSetBandWidth(bool Is25K);
void AT1846sSetMode(int mode);
void AT1846sSetRxCSSOff(RadioDevice_t deviceId);
void AT1846sSetRxCTCSS(RadioDevice_t deviceId, uint16_t tone);
void AT1846sSetRxDCS(RadioDevice_t deviceId, uint16_t code, bool inverted);
void AT1846sSetTxCTCSS(uint16_t tone);
void AT1846sSetTxDCS(uint16_t code, bool inverted);

bool AT1846sCheckCSS(uint16_t tone, CodeplugCSSTypes_t type);
bool AT1846sWriteTone1Reg(uint16_t toneFreqVal);
void AT1846sSelectVoiceChannel(uint8_t channel, uint8_t *voiceGainTx, uint16_t *deviation);


#endif /* _OPENGD77_AT1846S_H_ */
