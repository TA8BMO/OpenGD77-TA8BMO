/*
 * Copyright (C) 2026 TA8BMO
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
#ifndef _OPENGD77_DTMF_DECODE_H_
#define _OPENGD77_DTMF_DECODE_H_

#include "main.h"

// Longest DTMF sequence kept for display. A longer one keeps its last digits.
#define DTMF_DECODE_CODE_LEN 16

// The DTMF decoder rides on the five-tone decoder's capture of the received
// audio (see fiveToneDecode.c): it does not start or stop anything itself.

// Start of a capture: clears the filters, keeps the last sequence.
void dtmfDecodeReset(void);

// One I2S receive buffer, from the capture's interrupt-time tap.
void dtmfDecodeAudioBlock(uint32_t bufNum);

// Main loop: asks for a screen repaint when new digits have arrived.
void dtmfDecodeTick(void);

// The sequence being received or last received, NUL terminated. Returns false
// if there is none. ageMs (may be NULL) is the time since its last digit.
bool dtmfDecodeGetLast(char code[DTMF_DECODE_CODE_LEN + 1], uint32_t *ageMs);

// Forget it - on a channel change, like the five-tone code.
void dtmfDecodeClearLast(void);

// True while DTMF has been heard in the last moment. The five-tone and Morse
// decoders stand down on it: several DTMF tones sit within a few hertz of CCIR
// tones (1209/1197, 1633/1640), so a DTMF sequence would otherwise come out as
// a false five-tone code, and its tones read as Morse dashes.
bool dtmfDecodeToneRecentlyActive(void);

#endif /* _OPENGD77_DTMF_DECODE_H_ */
