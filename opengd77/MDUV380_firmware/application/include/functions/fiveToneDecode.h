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

#ifndef _OPENGD77_FIVETONE_DECODE_H_
#define _OPENGD77_FIVETONE_DECODE_H_

#include "main.h"

#define FIVETONE_CODE_LEN 5

// The selective calling standards, chosen in General Options. Sending and
// decoding both use the chosen one; only one is decoded at a time, since the
// standards give the same frequencies to different digits (2400Hz is CCIR's
// group tone and ZVEI's 0).
typedef struct
{
	const char *name;
	uint16_t    toneFreq[16];    // [0..9] digits, [10..15] A..F; E (14) is the repeat tone
	uint16_t    toneDurationMs;  // each tone, sent back to back
	uint16_t    decodeBlockN;    // samples per analysis block: two blocks must fit in a tone
} fiveToneStandard_t;

#define FIVETONE_STANDARD_COUNT 6

const fiveToneStandard_t *fiveToneStandard(void); // the one in use
const char *fiveToneStandardName(int index);
int fiveToneStandardIndex(void);
void fiveToneSetStandardIndex(int index);

// Number of decoded tones kept for the test/monitor display.
#define FIVETONE_TRACE_LEN 8

// Called once per pass of the main application loop, with the squelch state that
// loop just worked out. Everything - taking over the C6000 codec, starting and
// stopping the I2S capture, running the filters - is driven from here.
void fiveToneDecodeTick(bool hasSignal);

// Most recently decoded code, as 5 ASCII digits plus a terminator. Returns false
// if nothing has been decoded yet. ageMs (may be NULL) is how long ago it landed.
bool fiveToneDecodeGetLast(char code[FIVETONE_CODE_LEN + 1], uint32_t *ageMs);

// Forget the displayed code - call when changing channel.
void fiveToneDecodeClearLast(void);

// Live view for the monitor screen: raw tone characters as they are detected.
const char *fiveToneDecodeGetTrace(void);
void fiveToneDecodeClearTrace(void);

// Dominant tone of the last analysis block, for the diagnostic display.
// index is -1 when nothing dominates. ratio is how far it beat the runner-up.
void fiveToneDecodeGetLevels(int *toneIndex, uint16_t *toneFreq, int *ratio);

// True while the decoder currently owns the codec and is capturing.
bool fiveToneDecodeIsRunning(void);

// Hands the codec and the I2S transfer back at once. Anything about to key the
// radio must call this first: transmit needs the codec for the microphone, and
// the capture is otherwise only released on the next pass of the tick.
void fiveToneDecodeReleaseCapture(void);

// True if a CCIR tone was detected in the last couple of seconds. The Morse
// decoder uses this to stand down: a five-tone burst is five contiguous tones
// with no gaps, which to an envelope detector looks like one long dash.
bool fiveToneDecodeToneRecentlyActive(void);

#endif /* _OPENGD77_FIVETONE_DECODE_H_ */
