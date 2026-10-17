/*
 * Copyright (C) 2019 Kai Ludwig, DG4KLU
 *           (C) 2020-2024 Roger Clark, VK3KYY / G4KYF
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

#ifndef _OPENGD77_CODEC_H_
#define _OPENGD77_CODEC_H_

#include "hardware/HR-C6000.h"

#include "functions/sound.h"


#if defined(PLATFORM_MD9600) || defined(PLATFORM_MDUV380)  || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701)
#define CODEC_LOCATION_1 0x807537C
#else
#define CODEC_LOCATION_1 0x4400
#define CODEC_LOCATION_2 0x54000
#endif
#define CODEC_DECODE_CONFIG_DATA_LENGTH 0x7ec
#define CODEC_ENCODE_CONFIG_DATA_LENGTH 0x2000
#define CODEC_ECC_CONFIG_DATA_LENGTH 0x100

#define QUAUX(X) #X
#define QU(X) QUAUX(X)

#if defined(PLATFORM_MD9600) || defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380)  || defined(PLATFORM_RT84_DM1701)
#define AMBE_DECODE 0x08075955
#define AMBE_ENCODE 0x080754ad
#define AMBE_ENCODE_ECC 0x08075865
#else
#define AMBE_DECODE 0x0005543D
#define AMBE_ENCODE 0x00054F95
#define AMBE_ENCODE_ECC 0x0005534D
#endif
extern uint8_t ambebuffer_decode[CODEC_DECODE_CONFIG_DATA_LENGTH];
extern uint8_t ambebuffer_encode[CODEC_ENCODE_CONFIG_DATA_LENGTH];
extern uint8_t ambebuffer_encode_ecc[CODEC_ECC_CONFIG_DATA_LENGTH];

void initFrame(uint8_t *indata, uint16_t bitbufferDecode[49]);
void codecInit(bool fromVoicePrompts);
bool codecIsAvailable(void);
void codecInitInternalBuffers(void);
void codecDecode(uint8_t *indata_ptr, int numbBlocks);
void codecEncode(uint8_t *outdata_ptr, int numbBlocks);
void codecEncodeBlock(uint8_t *outdata_ptr);
// 20ms of audio the caller supplies - two WAV_BUFFER_SIZE halves back to back,
// in the I2S buffers' own sample format - encoded to one 9-byte frame. Leaves the
// sound buffers, which DMR transmit owns, alone.
void codecEncodeBlockFromPCM(uint8_t *outdata_ptr, const uint8_t *pcm);

#endif /* _OPENGD77_CODEC_H_ */
