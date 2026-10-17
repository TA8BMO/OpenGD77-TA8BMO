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

// ---------------------------------------------------------------------------
// DTMF receive decoder.
//
// Uses the same 8kHz capture of the received FM audio as the five-tone, Morse
// and APRS decoders (fiveToneDecode.c owns it), so it needs no extra hardware.
// It only works while the squelch is open on an analogue channel.
//
// Eight Goertzel filters, one per DTMF frequency, over short 106 sample blocks
// (13.25ms, 75Hz resolution). The short blocks are the whole point. That little
// "beep" you hear at the start of a call is usually a complete PTT-ID or ANI
// string sent at 30-50ms a digit, and a decoder made for hand dialling simply
// misses it. A digit counts once two blocks in a row agree, so anything 40ms or
// longer is caught whatever its alignment, and most digits down to about 30ms.
//
// A block is only taken as a digit if all of these hold:
//  - one low tone and one high tone, each above a minimum level
//  - each clearly the strongest of its group (6dB over the next one)
//  - the two within 12dB of each other ("twist"; FM emphasis tilts them)
//  - the pair holds most of the block's energy - this is what throws out speech
//    and noise, which spread their energy well outside the eight frequencies
//  - the same answer in two blocks in a row
//
// As in the standard, a repeated digit needs a gap between its two tones: two
// quiet blocks clear the "already shown" digit so the next one counts again. A
// pause longer than DTMF_SEQ_GAP_MS starts a new sequence on the display.
//
// All the state is in CCM RAM because main RAM is full. CCM RAM is not cleared
// at start-up, so I only trust the state once it carries DTMF_STATE_MAGIC.
// ---------------------------------------------------------------------------
#include "functions/dtmfDecode.h"
#include "functions/callLog.h"
#include "functions/sound.h"
#include "functions/ticks.h"
#include "interfaces/i2s.h"
#include "user_interface/uiGlobals.h"
#include <string.h>

#define DTMF_STATE_MAGIC    0x44544D46U // "DTMF"
#define DTMF_TONES          8
#define DTMF_BLOCK_N        106
#define DTMF_MIN_BLOCKS     2           // blocks in a row that must agree
#define DTMF_GAP_BLOCKS     2           // quiet blocks that separate a repeated digit
#define DTMF_SEQ_GAP_MS     2000        // a longer pause starts a new sequence
#define DTMF_ACTIVE_MS      150         // how long other decoders stand down after DTMF

// Thresholds, on the Goertzel power: a tone of amplitude A gives (N*A/2)^2.
// 4e7 is A of about 120 on the 16-bit scale - well under any real DTMF signal,
// well over the quieting floor of an open but silent channel.
#define DTMF_MIN_POWER      4.0e7f
#define DTMF_PEAK_RATIO     4.0f        // 6dB over the rest of its group
#define DTMF_TWIST_RATIO    16.0f       // 12dB either way
#define DTMF_ENERGY_SHARE   0.45f       // of the block's energy, in the two tones

// 2*cos(2*pi*f/8000) for 697, 770, 852, 941 (rows) and 1209, 1336, 1477, 1633 Hz (columns)
static const float dtmfCoeff[DTMF_TONES] =
{
	1.7077378f, 1.6452810f, 1.5686870f, 1.4782046f,
	1.1641040f, 0.9963702f, 0.7986184f, 0.5685327f
};

static const char dtmfChar[16] = "123A456B789C*0#D"; // row * 4 + column

typedef struct
{
	uint32_t valid;
	uint32_t lastRawMs;     // last block that looked like DTMF
	uint32_t lastDigitMs;   // last digit added to the sequence
	float    s1[DTMF_TONES];
	float    s2[DTMF_TONES];
	float    sum;           // of the block's samples, for its DC
	float    sumSq;
	int16_t  blockPos;
	int8_t   candidate;     // digit index seen in the last block(s), -1 = none
	int8_t   runs;          // how many blocks in a row it has been seen
	int8_t   stable;        // digit last reported, -1 once a gap has been seen
	int8_t   quiet;         // blocks in a row without a digit
	uint8_t  len;
	uint8_t  haveCode;
	volatile uint8_t pending; // new digit: the screen wants a repaint
	uint8_t  logged;        // the sequence on display has gone into the call log
	char     code[DTMF_DECODE_CODE_LEN + 1];
} dtmfState_t;

static __attribute__((section(".ccmram"))) dtmfState_t dt;

static void dtmfEnsureInit(void)
{
	if (dt.valid != DTMF_STATE_MAGIC)
	{
		memset(&dt, 0, sizeof(dt));
		dt.candidate = -1;
		dt.stable = -1;
		dt.valid = DTMF_STATE_MAGIC;
	}
}

void dtmfDecodeReset(void)
{
	dtmfEnsureInit();

	for (int i = 0; i < DTMF_TONES; i++)
	{
		dt.s1[i] = 0.0f;
		dt.s2[i] = 0.0f;
	}
	dt.sum = 0.0f;
	dt.sumSq = 0.0f;
	dt.blockPos = 0;
	dt.candidate = -1;
	dt.runs = 0;
	dt.stable = -1;
	dt.quiet = 0;
}

// Interrupt context.
static void dtmfAddDigit(int digit)
{
	uint32_t now = ticksGetMillis();

	if ((dt.haveCode == 0) || ((uint32_t)(now - dt.lastDigitMs) > DTMF_SEQ_GAP_MS))
	{
		dt.len = 0; // a new sequence replaces the one on display
	}

	if (dt.len >= DTMF_DECODE_CODE_LEN)
	{
		memmove(&dt.code[0], &dt.code[1], (DTMF_DECODE_CODE_LEN - 1));
		dt.len = (DTMF_DECODE_CODE_LEN - 1);
	}

	dt.code[dt.len++] = dtmfChar[digit];
	dt.code[dt.len] = 0;
	dt.haveCode = 1;
	dt.lastDigitMs = now;
	dt.pending = 1;
	dt.logged = 0;
}

// End of one block: decide whether it held a digit. Interrupt context.
static void dtmfEndBlock(void)
{
	float mag[DTMF_TONES];
	int row = 0;  // strongest of the low group, 0..3
	int col = 4;  // strongest of the high group, 4..7
	int detected = -1;

	for (int i = 0; i < DTMF_TONES; i++)
	{
		mag[i] = ((dt.s1[i] * dt.s1[i]) + (dt.s2[i] * dt.s2[i])) - (dtmfCoeff[i] * dt.s1[i] * dt.s2[i]);
		dt.s1[i] = 0.0f;
		dt.s2[i] = 0.0f;

		if (i < 4)
		{
			if (mag[i] > mag[row])
			{
				row = i;
			}
		}
		else if (mag[i] > mag[col])
		{
			col = i;
		}
	}

	// The block's energy without its DC, which a tone pair should mostly account for.
	float energy = (dt.sumSq - ((dt.sum * dt.sum) / (float) DTMF_BLOCK_N));
	dt.sum = 0.0f;
	dt.sumSq = 0.0f;

	float low = mag[row];
	float high = mag[col];
	bool ok = ((low > DTMF_MIN_POWER) && (high > DTMF_MIN_POWER) &&
			(high < (low * DTMF_TWIST_RATIO)) && (low < (high * DTMF_TWIST_RATIO)) &&
			(energy > 0.0f) && ((low + high) > (DTMF_ENERGY_SHARE * ((float) DTMF_BLOCK_N / 2.0f) * energy)));

	for (int i = 0; (i < DTMF_TONES) && ok; i++)
	{
		if ((i != row) && (i != col) && ((mag[i] * DTMF_PEAK_RATIO) > ((i < 4) ? low : high)))
		{
			ok = false;
		}
	}

	if (ok)
	{
		detected = ((row * 4) + (col - 4));
		dt.lastRawMs = ticksGetMillis();
		dt.quiet = 0;
	}
	else if (dt.quiet < DTMF_GAP_BLOCKS)
	{
		if (++dt.quiet >= DTMF_GAP_BLOCKS)
		{
			dt.stable = -1; // a real gap: the same digit may follow
		}
	}

	if ((detected >= 0) && (detected == dt.candidate))
	{
		if (dt.runs < 100)
		{
			dt.runs++;
		}
	}
	else
	{
		dt.candidate = (int8_t) detected;
		dt.runs = 1;
	}

	if ((dt.candidate >= 0) && (dt.runs == DTMF_MIN_BLOCKS) && (dt.candidate != dt.stable))
	{
		dt.stable = dt.candidate;
		dtmfAddDigit(dt.candidate);
	}
}

// One I2S receive buffer, left channel only - the same walk as the five-tone tap.
void dtmfDecodeAudioBlock(uint32_t bufNum)
{
	if (dt.valid != DTMF_STATE_MAGIC)
	{
		return; // the capture always resets first, so this is only a guard
	}

	for (int h = 0; h < 2; h++)
	{
		for (int i = 0; i < WAV_BUFFER_SIZE; i += 2)
		{
			float x = (float)(int16_t) i2s_Rx_Buffer[bufNum][h][i];

			dt.sum += x;
			dt.sumSq += (x * x);

			for (int t = 0; t < DTMF_TONES; t++)
			{
				float s0 = ((dtmfCoeff[t] * dt.s1[t]) - dt.s2[t]) + x;

				dt.s2[t] = dt.s1[t];
				dt.s1[t] = s0;
			}

			if (++dt.blockPos >= DTMF_BLOCK_N)
			{
				dt.blockPos = 0;
				dtmfEndBlock();
			}
		}
	}
}

// A sequence goes into the call log once it is over - the same pause that
// starts a new one - and only if it has two digits or more: a lone digit is as
// likely to be speech that happened to pass the checks.
static void dtmfLogIfDue(bool force)
{
	if ((dt.haveCode != 0) && (dt.logged == 0) &&
			(force || ((uint32_t)(ticksGetMillis() - dt.lastDigitMs) > DTMF_SEQ_GAP_MS)))
	{
		dt.logged = 1;

		if (dt.len >= 2)
		{
			callLogAdd(CALL_LOG_TYPE_DTMF, dt.code);
		}
	}
}

void dtmfDecodeTick(void)
{
	if (dt.valid != DTMF_STATE_MAGIC)
	{
		return;
	}

	if (dt.pending != 0)
	{
		dt.pending = 0;
		uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN; // show the new digit
	}

	dtmfLogIfDue(false);
}

bool dtmfDecodeGetLast(char code[DTMF_DECODE_CODE_LEN + 1], uint32_t *ageMs)
{
	if ((dt.valid != DTMF_STATE_MAGIC) || (dt.haveCode == 0))
	{
		return false;
	}

	memcpy(code, dt.code, (DTMF_DECODE_CODE_LEN + 1));
	code[DTMF_DECODE_CODE_LEN] = 0;

	if (ageMs != NULL)
	{
		*ageMs = (uint32_t)(ticksGetMillis() - dt.lastDigitMs);
	}
	return true;
}

void dtmfDecodeClearLast(void)
{
	if (dt.valid == DTMF_STATE_MAGIC)
	{
		dtmfLogIfDue(true); // a channel change must not lose a sequence not yet logged
		dt.haveCode = 0;
		dt.len = 0;
		dt.code[0] = 0;
	}
}

bool dtmfDecodeToneRecentlyActive(void)
{
	return ((dt.valid == DTMF_STATE_MAGIC) && (dt.lastRawMs != 0) &&
			((uint32_t)(ticksGetMillis() - dt.lastRawMs) < DTMF_ACTIVE_MS));
}
