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

#ifndef _OPENGD77_MORSE_DECODE_H_
#define _OPENGD77_MORSE_DECODE_H_

#include "main.h"

#define CW_LOG_ENTRIES   4
#define CW_LOG_TEXT_LEN  55

// Fed from the receive audio tap (interrupt context). Handed one half of the
// I2S buffer at a time, same as the five-tone decoder - both run off the single
// capture that fiveToneDecode.c sets up, so they never fight over the codec.
void morseDecodeAudioBlock(uint32_t bufNum);

// Main application loop. Commits a finished transmission to the log and writes
// it out, and notices when the operator changes channel.
void morseDecodeTick(bool hasSignal);

// True when there is stored Morse text for the frequency currently tuned. This
// is what puts the red CW marker on the channel screen.
bool morseDecodeHasTextForCurrentChannel(void);

// Reading the log. Entries are filtered to the current frequency, newest first.
int         morseDecodeGetEntryCount(void);
const char *morseDecodeGetEntryText(int index);

// Text still being decoded right now, "" when idle. Shown live in the log screen.
const char *morseDecodeGetLive(void);

// Wipes every stored entry, on every frequency, and clears the flash copy.
void morseDecodeClearAll(void);

#endif /* _OPENGD77_MORSE_DECODE_H_ */
