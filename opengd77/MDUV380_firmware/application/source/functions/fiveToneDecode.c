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
// CCIR-1 five-tone (Select-5 / ANI) receive decoder.
//
// This is the file the whole project started from. Everyone, me included,
// thought this radio could not decode anything from received audio, because the
// demodulated audio supposedly never reaches the MCU. Turns out it does. The
// HR-C6000's codec digitises its line input and sends it to the MCU over I2S -
// the same path that carries the microphone to the software AMBE encoder for
// DMR transmit. On FM it just sits unused, because bit 7 of C6000 register
// 0x04/0xE0 (the firmware's own comments call it "CPU Controls Codec") is clear
// in normal receive, so the codec is not running and I2S is not clocked.
//
// Set that bit, clear bit 6 (that one puts the microphone on Line in 1), and the
// received FM audio shows up as 8kHz PCM. I checked it on air: on a bare carrier
// the level drops to the FM quieting floor, and a real selective call from an
// Aselsan set decodes to the right digits. That was a good evening.
//
// The tones are found with one Goertzel filter per frequency, run straight from
// the I2S interrupt. 256-sample blocks give 32ms and 31.25Hz of resolution; the
// closest pair of digit tones, 1124 and 1197Hz, are 73Hz apart, so they come
// apart cleanly. It costs well under 1% of the CPU.
//
// I only take the codec over while the squelch is open and hand it straight back
// otherwise, so an idle radio behaves exactly like stock, power saving included.
// Transmit, DMR and voice prompts also make it let go, since they need the codec
// or the I2S link themselves.
// ---------------------------------------------------------------------------
#include <math.h>
#include "functions/fiveToneDecode.h"
#include "functions/callLog.h"
#include "functions/dtmfDecode.h"
#include "functions/morseDecode.h"
#include "functions/aprsDecode.h"
#include "functions/trx.h"
#include "functions/sound.h"
#include "functions/ticks.h"
#include "functions/voicePrompts.h"
#include "functions/voiceRecorder.h"
#include "hardware/HR-C6000.h"
#include "interfaces/hr-c6000_spi.h"
#include "interfaces/i2s.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiGlobals.h"

#define FT_C6000_PAGE        0x04
#define FT_C6000_CODEC_REG   0xE0
#define FT_CODEC_CPU_BIT     0x80   // "CPU Controls Codec" - starts the codec and the I2S clock
#define FT_CODEC_MIC_BIT     0x40   // "Mic Input to Line in 1" - must be off to hear the receiver

#define FT_SAMPLE_RATE       8000.0f
#define FT_TONE_COUNT        16
#define FT_BLOCK_N           256
#define FT_REPEAT_TONE_IDX   14     // CCIR 'E', 2110Hz, sent in place of a repeated digit

#define FT_MIN_BLOCKS        2      // a tone must hold for 2 blocks (64ms) to count
#define FT_BURST_GAP_MS      250    // silence this long ends a burst
#define FT_WS_PROBE_MS       6      // I2S word clock check, one frame is 125us

// CCIR-1, the same table fiveToneSend() transmits from.
static const uint16_t ftToneFreq[FT_TONE_COUNT] =
{
	1981, 1124, 1197, 1275, 1358, 1446, 1540, 1640, 1747, 1860, // digits 0-9
	2400,  930, 2247,  991, 2110, 2600                          // A(group) B C D E(repeat) F
};
static const char ftToneChar[FT_TONE_COUNT + 1] = "0123456789ABCDEF";

static float   ftCoeff[FT_TONE_COUNT];
static float   ftS1[FT_TONE_COUNT];
static float   ftS2[FT_TONE_COUNT];
static bool    ftCoeffReady = false;
static int     ftBlockPos = 0;

static volatile int   ftBestIdx = -1;
static volatile int   ftBestRatio = 0;

static int      ftCandidateIdx = -1;
static int      ftCandidateRuns = 0;
static int      ftStableIdx = -1;

static uint8_t  ftBurst[FIVETONE_TRACE_LEN];
static int      ftBurstLen = 0;
static uint32_t ftLastToneMs = 0;

static char     ftTrace[FIVETONE_TRACE_LEN + 1] = "";
static int      ftTraceLen = 0;

static char     ftLastCode[FIVETONE_CODE_LEN + 1] = "";
static bool     ftHaveCode = false;
static uint32_t ftLastCodeMs = 0;
static volatile bool ftBurstPending = false; // set by the ISR, consumed by the tick

// Automatic answer. The delay lets the caller unkey first. The hold-off is only
// long enough to cover our own transmission, so somebody calling repeatedly gets
// answered every time.
#define FT_REPLY_DELAY_MS    1200
#define FT_REPLY_HOLDOFF_MS  2000

// Runaway guard, the backstop under the hold-off above. Two radios sharing an
// ANI code with answering enabled would otherwise answer each other forever,
// and a radio holding a channel open unattended is a real problem.
//
// Answers closer together than this are treated as one chain. Anything a person
// actually does - even calling three times in a row - leaves far bigger gaps
// than a machine bouncing off another machine, so a genuine caller never trips
// it however many times they call. A loop trips it within seconds and answering
// latches off until the operator changes channel or restarts the radio.
#define FT_RUNAWAY_CHAIN_MS  8000
#define FT_RUNAWAY_LIMIT     4

static bool     ftReplyPending = false;
static uint32_t ftReplyReadyMs = 0;
static uint32_t ftReplySentMs = 0;

static int      ftReplyCount = 0;
static bool     ftRunawayLatched = false;

static bool     ftRunning = false;
static uint8_t  ftSavedE0 = 0;
static bool     ftHaveSavedE0 = false;

static void ftResetDetector(void)
{
	for (int i = 0; i < FT_TONE_COUNT; i++)
	{
		ftS1[i] = 0.0f;
		ftS2[i] = 0.0f;
	}

	ftBlockPos = 0;
	ftBestIdx = -1;
	ftBestRatio = 0;
	ftCandidateIdx = -1;
	ftCandidateRuns = 0;
	ftStableIdx = -1;
	ftBurstLen = 0;
}

// Turn the collected tones into digits. The transmitter substitutes the repeat
// tone whenever a digit would repeat the previous one, so undo that here. The
// last five tones are used, which drops any leading group tone.
static void ftFinishBurst(void)
{
	int start;
	char out[FIVETONE_CODE_LEN + 1];
	int prev = -1;

	if (ftBurstLen < FIVETONE_CODE_LEN)
	{
		ftBurstLen = 0;
		return;
	}

	start = (ftBurstLen - FIVETONE_CODE_LEN);

	for (int i = 0; i < FIVETONE_CODE_LEN; i++)
	{
		int t = (int) ftBurst[start + i];

		if (t == FT_REPEAT_TONE_IDX)
		{
			if (prev < 0)
			{
				ftBurstLen = 0; // repeat tone with nothing to repeat - not a valid code
				return;
			}
			t = prev;
		}

		if (t > 9)
		{
			ftBurstLen = 0; // only plain digits make up an ANI code
			return;
		}

		out[i] = (char)('0' + t);
		prev = t;
	}
	out[FIVETONE_CODE_LEN] = 0;

	memcpy(ftLastCode, out, sizeof(out));
	ftHaveCode = true;
	ftLastCodeMs = ticksGetMillis();
	ftBurstPending = true;
	ftBurstLen = 0;
}

static void ftPushTone(int toneIdx)
{
	if (ftBurstLen < FIVETONE_TRACE_LEN)
	{
		ftBurst[ftBurstLen++] = (uint8_t) toneIdx;
	}
	else
	{
		for (int i = 0; i < (FIVETONE_TRACE_LEN - 1); i++)
		{
			ftBurst[i] = ftBurst[i + 1];
		}
		ftBurst[FIVETONE_TRACE_LEN - 1] = (uint8_t) toneIdx;
	}

	if (ftTraceLen >= FIVETONE_TRACE_LEN)
	{
		for (int i = 0; i < (FIVETONE_TRACE_LEN - 1); i++)
		{
			ftTrace[i] = ftTrace[i + 1];
		}
		ftTraceLen = (FIVETONE_TRACE_LEN - 1);
	}
	ftTrace[ftTraceLen++] = ftToneChar[toneIdx];
	ftTrace[ftTraceLen] = 0;

	ftLastToneMs = ticksGetMillis();
}

// End of one analysis block. Interrupt context - kept to plain arithmetic.
static void ftEndBlock(void)
{
	float bestMag = 0.0f;
	float secondMag = 0.0f;
	int bestIdx = -1;
	int detected = -1;

	for (int i = 0; i < FT_TONE_COUNT; i++)
	{
		float m = ((ftS1[i] * ftS1[i]) + (ftS2[i] * ftS2[i])) - (ftCoeff[i] * ftS1[i] * ftS2[i]);

		ftS1[i] = 0.0f;
		ftS2[i] = 0.0f;

		if (m > bestMag)
		{
			secondMag = bestMag;
			bestMag = m;
			bestIdx = i;
		}
		else if (m > secondMag)
		{
			secondMag = m;
		}
	}

	// Noise spreads its energy over every bin, so requiring the winner to stand
	// well clear of the runner-up rejects it without needing an absolute level
	// that would have to be calibrated per radio. The floor just keeps silence
	// from producing a winner at all.
	ftBestIdx = bestIdx;
	ftBestRatio = (int)((secondMag > 1.0f) ? (bestMag / secondMag) : 999.0f);

	// Nothing counts while DTMF is on the air: 1209, 1477 and 1633Hz are within
	// a bin of the CCIR tones for 2, 5 and 7, so a DTMF string would otherwise
	// leave a false five-tone code behind it.
	if ((bestIdx >= 0) && (bestMag > 5.0e8f) && (bestMag > (secondMag * 5.0f)) && (dtmfDecodeToneRecentlyActive() == false))
	{
		detected = bestIdx;
	}

	if ((detected >= 0) && (detected == ftCandidateIdx))
	{
		ftCandidateRuns++;
	}
	else
	{
		ftCandidateIdx = detected;
		ftCandidateRuns = 1;
	}

	if ((ftCandidateIdx >= 0) && (ftCandidateRuns == FT_MIN_BLOCKS) && (ftCandidateIdx != ftStableIdx))
	{
		ftStableIdx = ftCandidateIdx;
		ftPushTone(ftStableIdx);
	}
	else if (detected < 0)
	{
		ftStableIdx = -1; // silence, so an identical tone next time still counts
	}
}

// I2S receive tap: one call is WAV_BUFFER_SIZE left-channel samples, 20ms.
//
// This module owns the capture, so it also hands the same samples to the Morse
// decoder. Both analysers therefore run off one takeover of the codec, and
// there is never a fight over the hardware.
static void ftAudioTap(uint32_t bufNum)
{
	// The receive recorder takes its copy first. While it records, the APRS and
	// Morse decoders - the heaviest, and of no use on a voice channel - stand
	// aside, leaving the encoder the time it needs. DTMF and five-tone go on, so
	// the recording knows who was calling.
	bool recording = voiceRecorderAudioBlock(bufNum);

	dtmfDecodeAudioBlock(bufNum); // first, so the others below already know if this is DTMF
	if (!recording)
	{
		morseDecodeAudioBlock(bufNum);
		aprsDecodeAudioBlock(bufNum);
	}

	for (int h = 0; h < 2; h++)
	{
		for (int i = 0; i < WAV_BUFFER_SIZE; i += 2)
		{
			float x = (float)(int16_t) i2s_Rx_Buffer[bufNum][h][i];

			for (int t = 0; t < FT_TONE_COUNT; t++)
			{
				float s0 = ((ftCoeff[t] * ftS1[t]) - ftS2[t]) + x;

				ftS2[t] = ftS1[t];
				ftS1[t] = s0;
			}

			if (++ftBlockPos >= FT_BLOCK_N)
			{
				ftBlockPos = 0;
				ftEndBlock();
			}
		}
	}
}

// i2s.c waits for the word clock in an unbounded loop before starting its DMA,
// which would hang the radio if the C6000 is not clocking. Check it first.
static bool ftI2SClockIsRunning(void)
{
	GPIO_PinState first = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15);
	uint32_t start = ticksGetMillis();

	while ((uint32_t)(ticksGetMillis() - start) < FT_WS_PROBE_MS)
	{
		if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15) != first)
		{
			return true;
		}
	}

	return false;
}

static void ftStop(void)
{
	if (!ftRunning)
	{
		return;
	}

	i2sRxTapCallback = NULL;
	I2STerminateTransfers();

	if (ftHaveSavedE0)
	{
		SPI0WritePageRegByte(FT_C6000_PAGE, FT_C6000_CODEC_REG, ftSavedE0);
		ftHaveSavedE0 = false;
	}

	ftRunning = false;
}

static void ftStart(void)
{
	volatile uint8_t cur = 0;
	uint8_t want;

	if (ftRunning)
	{
		return;
	}

	if (!ftCoeffReady)
	{
		for (int i = 0; i < FT_TONE_COUNT; i++)
		{
			ftCoeff[i] = 2.0f * cosf((2.0f * (float) M_PI * (float) ftToneFreq[i]) / FT_SAMPLE_RATE);
		}
		ftCoeffReady = true;
	}

	if (SPI0ReadPageRegByte(FT_C6000_PAGE, FT_C6000_CODEC_REG, &cur) != 0)
	{
		return;
	}

	ftSavedE0 = (uint8_t) cur;
	ftHaveSavedE0 = true;

	// Run the codec, and take the microphone off its input so what gets
	// digitised is the receiver rather than the operator.
	want = (uint8_t)((ftSavedE0 | FT_CODEC_CPU_BIT) & (uint8_t)~FT_CODEC_MIC_BIT);
	SPI0WritePageRegByte(FT_C6000_PAGE, FT_C6000_CODEC_REG, want);

	if (!ftI2SClockIsRunning())
	{
		// Codec did not come up - put the register back and try again later.
		SPI0WritePageRegByte(FT_C6000_PAGE, FT_C6000_CODEC_REG, ftSavedE0);
		ftHaveSavedE0 = false;
		return;
	}

	// Both flags clear so the sound system's own callbacks leave the data alone;
	// the transmit half is cleared so nothing stale is sent back to the C6000.
	isSending = false;
	isReceiving = false;
	memset((void *)i2s_Rx_Buffer, 0, sizeof(i2s_Rx_Buffer));
	memset((void *)i2s_Tx_Buffer, 0, sizeof(i2s_Tx_Buffer));

	ftResetDetector();
	dtmfDecodeReset();
	soundStartDMA();
	i2sRxTapCallback = ftAudioTap;
	ftRunning = true;
}

void fiveToneDecodeTick(bool hasSignal)
{
	// Drop the displayed code when the operator moves somewhere else. Keying off
	// the tuned frequency catches a channel step, a zone change and a hop to VFO
	// alike, without having to hook each of those paths.
	if (currentChannelData != NULL)
	{
		static uint32_t ftWatchedRxFreq = 0;

		if (currentChannelData->rxFreq != ftWatchedRxFreq)
		{
			char dtmfCode[DTMF_DECODE_CODE_LEN + 1];
			bool wasShowing = (ftHaveCode || dtmfDecodeGetLast(dtmfCode, NULL));

			ftWatchedRxFreq = currentChannelData->rxFreq;
			fiveToneDecodeClearLast();
			dtmfDecodeClearLast();

			// Moving channel is the operator showing up, so release the guard.
			ftRunawayLatched = false;
			ftReplyCount = 0;


			// The screen is painted before this runs, so on the pass where the
			// channel changes it has already drawn the old code. Ask for another
			// paint, or the stale text sits there until something else redraws.
			if (wasShowing)
			{
				uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
			}
		}
	}

	// Everything that needs the codec or the I2S link for itself takes priority.
	bool allowed = (hasSignal &&
			(trxGetMode() == RADIO_MODE_ANALOG) &&
			(trxTransmissionEnabled == false) &&
			(trxIsTransmitting == false) &&
			(voicePromptsIsPlaying() == false) &&
			(voiceRecorderIsPlaying() == false));

	if (allowed)
	{
		if (!ftRunning)
		{
			ftStart();
		}
		else if (!ftI2SClockIsRunning())
		{
			// The C6000 stopped clocking, most likely because it was powered
			// down and re-initialised. Drop everything and pick it up again.
			ftStop();
		}
	}
	else
	{
		if (ftRunning)
		{
			// Close out anything still in progress before handing the codec back.
			if ((ftBurstLen >= FIVETONE_CODE_LEN))
			{
				ftFinishBurst();
			}
			ftStop();
		}
	}

	// A burst ends on silence rather than on a tone count, so a leading group
	// tone does not shift the code.
	if ((ftBurstLen > 0) && ((uint32_t)(ticksGetMillis() - ftLastToneMs) > FT_BURST_GAP_MS))
	{
		ftFinishBurst();
	}

	dtmfDecodeTick(); // DTMF digits are shown as they arrive

	// A new code just landed: nudge the screen so it repaints. The code itself
	// stays on display until the channel changes, the radio is switched off, or
	// somebody else calls - the point is to be able to see who called last.
	if (ftBurstPending)
	{
		ftBurstPending = false;
		uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
		callLogAdd(CALL_LOG_TYPE_5T, ftLastCode); // here, not where the code lands: that is an interrupt

		// Was that call addressed to us?
		if ((currentChannelData != NULL) && codeplugChannelGetFiveToneReply(currentChannelData))
		{
			uint8_t mine[FIVETONE_CODE_LEN];
			bool match = true;

			(void) codeplugChannelGetFiveTone(currentChannelData, mine);

			for (int i = 0; i < FIVETONE_CODE_LEN; i++)
			{
				if (ftLastCode[i] != (char)('0' + mine[i]))
				{
					match = false;
					break;
				}
			}

			if (match && (ftRunawayLatched == false) && ((ftReplySentMs == 0) ||
					((uint32_t)(ticksGetMillis() - ftReplySentMs) > FT_REPLY_HOLDOFF_MS)))
			{
				ftReplyPending = true;
			}
		}
	}

	// Answer only once the caller has unkeyed, otherwise we would transmit on
	// top of them and they would never hear it.
	if (ftReplyPending && (hasSignal == false) && (trxTransmissionEnabled == false) && (trxIsTransmitting == false))
	{
		if (ftReplyReadyMs == 0)
		{
			ftReplyReadyMs = ticksGetMillis();
		}
		else if ((uint32_t)(ticksGetMillis() - ftReplyReadyMs) > FT_REPLY_DELAY_MS)
		{
			uint8_t mine[FIVETONE_CODE_LEN];

			ftReplyPending = false;
			ftReplyReadyMs = 0;

			(void) codeplugChannelGetFiveTone(currentChannelData, mine);

			// Hand the codec back before keying up - transmit needs the
			// microphone on the codec input, which is exactly what we took away.
			ftStop();

			if (uiTxScreenSendFiveToneReply(mine))
			{
				uint32_t now = ticksGetMillis();
				bool chained = ((ftReplySentMs != 0) && ((uint32_t)(now - ftReplySentMs) < FT_RUNAWAY_CHAIN_MS));

				ftReplySentMs = now;
				ftReplyCount = (chained ? (ftReplyCount + 1) : 1);

				if (ftReplyCount >= FT_RUNAWAY_LIMIT)
				{
					ftRunawayLatched = true;
				}
			}
		}
	}
	else if (hasSignal)
	{
		ftReplyReadyMs = 0; // still talking, restart the wait
	}
}

bool fiveToneDecodeGetLast(char code[FIVETONE_CODE_LEN + 1], uint32_t *ageMs)
{
	if (!ftHaveCode)
	{
		return false;
	}

	memcpy(code, ftLastCode, (FIVETONE_CODE_LEN + 1));
	if (ageMs != NULL)
	{
		*ageMs = (uint32_t)(ticksGetMillis() - ftLastCodeMs);
	}
	return true;
}

// Called when the operator moves to another channel: the displayed code belongs
// to the channel it arrived on, so it should not follow them around.
void fiveToneDecodeClearLast(void)
{
	ftHaveCode = false;
	ftLastCode[0] = 0;
	ftBurstLen = 0;
}

const char *fiveToneDecodeGetTrace(void)
{
	return ftTrace;
}

void fiveToneDecodeClearTrace(void)
{
	ftTrace[0] = 0;
	ftTraceLen = 0;
}

void fiveToneDecodeGetLevels(int *toneIndex, uint16_t *toneFreq, int *ratio)
{
	int idx = ftBestIdx;

	if (toneIndex != NULL)
	{
		*toneIndex = idx;
	}
	if (toneFreq != NULL)
	{
		*toneFreq = ((idx >= 0) ? ftToneFreq[idx] : 0);
	}
	if (ratio != NULL)
	{
		*ratio = ftBestRatio;
	}
}

bool fiveToneDecodeIsRunning(void)
{
	return ftRunning;
}

bool fiveToneDecodeToneRecentlyActive(void)
{
	return ((ftLastToneMs != 0) && ((uint32_t)(ticksGetMillis() - ftLastToneMs) < 2000));
}

void fiveToneDecodeReleaseCapture(void)
{
	if (ftRunning)
	{
		ftStop();
	}
}
