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
// Tone finder: which CTCSS or DCS is that station on this frequency using?
//
// The tones sit below the audio the radio passes on - its own high-pass filter
// takes them out before anything here could hear them - so the AT1846S's own
// detector does the work. All you can ask it is "is it this one?", so I ask it
// about every tone in turn: the 50 CTCSS tones, then the 83 DCS codes, then the
// same codes inverted. Unlike the VFO's tone scan, the detector is armed without
// making the tone the squelch (trxArmRxCSSDetector), so you keep hearing the
// station the whole time and nothing gets changed: the tone is only shown, on
// the line under the channel name.
//
// A tone is only there while the station is transmitting, so the search waits
// whenever the carrier drops and carries on when it comes back. A hit is checked
// a second time before I believe it. Each CTCSS tone takes 200ms and each DCS
// code 300ms - the detector needs that long to lock - so CTCSS turns up within
// 10 seconds of carrier and DCS within another 50. A bit of patience, that's all.
// ---------------------------------------------------------------------------
#include "functions/toneFinder.h"
#include "functions/codeplug.h"
#include "functions/settings.h"
#include "functions/sound.h"
#include "functions/ticks.h"
#include "functions/trx.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiGlobals.h"
#include "user_interface/uiLocalisation.h"
#include "user_interface/uiUtilities.h"
#include <string.h>

#define TF_CTCSS_STEP_MS  200
#define TF_DCS_STEP_MS    300
#define TF_REDRAW_EVERY   5   // candidates between repaints of the progress

enum
{
	TF_OFF = 0,
	TF_SEARCHING,
	TF_CONFIRMING,   // the detector said yes once; asking again
	TF_FOUND,
	TF_NOT_FOUND
};

static struct
{
	uint32_t stepStartMs;
	uint32_t watchedFreq;    // the search and its answer belong to this frequency
	uint32_t resultMs;
	uint16_t result;         // tone found, in codeplug form
	uint8_t  index;          // candidate: CTCSS, then DCS, then inverted DCS
	uint8_t  state;
	uint8_t  restorePending; // stopped by a transmission: put the squelch tone back after it
	uint8_t  carrier;        // last pass had a carrier, for repainting when that changes
} tf;

#define TF(tr, en, es, ru, de) LANG5((tr), (en), (es), (ru), (de)) // Turkish, English, Spanish, Russian, German

static int tfCandidateCount(void)
{
	return (TRX_NUM_CTCSS + (2 * TRX_NUM_DCS));
}

static uint16_t tfCandidate(int i)
{
	if (i < TRX_NUM_CTCSS)
	{
		return TRX_CTCSSTones[i];
	}

	i -= TRX_NUM_CTCSS;

	if (i < TRX_NUM_DCS)
	{
		return (TRX_DCSCodes[i] | CSS_TYPE_DCS);
	}

	return (TRX_DCSCodes[i - TRX_NUM_DCS] | CSS_TYPE_DCS | CSS_TYPE_DCS_INVERTED);
}

static bool tfSearching(void)
{
	return ((tf.state == TF_SEARCHING) || (tf.state == TF_CONFIRMING));
}

// Puts the channel's own squelch tone back (or none) in the detector.
static void tfRestore(void)
{
	if (currentChannelData != NULL)
	{
		trxSetRxCSS(RADIO_DEVICE_PRIMARY, currentChannelData->rxTone);
	}
}

static void tfRepaint(void)
{
	uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
}

static void tfFormatTone(char *out, size_t len, uint16_t tone)
{
	if (codeplugGetCSSType(tone) == CSS_TYPE_CTCSS)
	{
		snprintf(out, len, "%u.%uHz", (unsigned) (tone / 10), (unsigned) (tone % 10));
	}
	else
	{
		dcsPrintf(out, len, (char *) "", tone);
	}
}

void toneFinderToggle(void)
{
	if (tfSearching())
	{
		tf.state = TF_OFF; // stopped by hand
		tfRestore();
		tfRepaint();
		return;
	}

	if ((currentChannelData == NULL) || (trxGetMode() != RADIO_MODE_ANALOG) || trxTransmissionEnabled || trxIsTransmitting)
	{
		return;
	}

	tf.state = TF_SEARCHING;
	tf.index = 0;
	tf.restorePending = 0;
	tf.carrier = 0;
	tf.watchedFreq = currentChannelData->rxFreq;
	tf.stepStartMs = ticksGetMillis();
	tf.resultMs = tf.stepStartMs;
	trxArmRxCSSDetector(tfCandidate(0));
	tfRepaint();
}

void toneFinderTick(void)
{
	uint32_t now;
	bool carrier;

	if ((tf.state == TF_OFF) && (tf.restorePending == 0))
	{
		return;
	}

	bool transmitting = (trxTransmissionEnabled || trxIsTransmitting);

	// Never write the detector during a transmission: setting the receive tone
	// clears the chip's transmit tone register too.
	if (tf.restorePending != 0)
	{
		if (transmitting == false)
		{
			tf.restorePending = 0;
			tfRestore();
		}
		return;
	}

	// Another channel or frequency: the answer was about the old one.
	if ((currentChannelData == NULL) || (currentChannelData->rxFreq != tf.watchedFreq) || (trxGetMode() != RADIO_MODE_ANALOG))
	{
		if (tfSearching() && (transmitting == false))
		{
			tfRestore();
		}
		tf.state = TF_OFF;
		tfRepaint();
		return;
	}

	if (tfSearching() == false)
	{
		return;
	}

	if (transmitting)
	{
		tf.state = TF_OFF; // stop; the squelch tone goes back once the transmission ends
		tf.restorePending = 1;
		tfRepaint();
		return;
	}

	now = ticksGetMillis();
	carrier = trxCarrierDetected(RADIO_DEVICE_PRIMARY);

	if (carrier != (tf.carrier != 0))
	{
		tf.carrier = (carrier ? 1 : 0);
		tfRepaint(); // "waiting for a signal" comes and goes
	}

	// No carrier, no tone: wait on the same candidate, starting its time again.
	if (carrier == false)
	{
		tf.stepStartMs = now;
		tf.state = TF_SEARCHING;
		return;
	}

	if ((uint32_t)(now - tf.stepStartMs) < ((tf.index < TRX_NUM_CTCSS) ? TF_CTCSS_STEP_MS : TF_DCS_STEP_MS))
	{
		return;
	}

	uint16_t candidate = tfCandidate(tf.index);

	if (trxCheckCSSFlag(candidate))
	{
		if (tf.state == TF_CONFIRMING)
		{
			char toneText[12];
			char message[16];

			tf.result = candidate;
			tf.resultMs = now;
			tf.state = TF_FOUND;
			tfRestore();

			tfFormatTone(toneText, sizeof(toneText), candidate);
			snprintf(message, sizeof(message), TF("Ton %s", "Tone %s", "Tono %s", "\xD2\xEE\xED %s", "Ton %s"), toneText);
			uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 2500, message, false);
			soundSetMelody(MELODY_ACK_BEEP);
			tfRepaint();
			return;
		}

		tf.state = TF_CONFIRMING; // once more on the same tone before believing it
		tf.stepStartMs = now;
		return;
	}

	tf.state = TF_SEARCHING;
	tf.index++;

	if (tf.index >= tfCandidateCount())
	{
		tf.state = TF_NOT_FOUND;
		tf.resultMs = now;
		tfRestore();
		uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 2500, TF("Ton yok", "No tone", "Sin tono", "\xCD\xE5\xF2 \xF2\xEE\xED\xE0", "Kein Ton"), false);
		tfRepaint();
		return;
	}

	trxArmRxCSSDetector(tfCandidate(tf.index));
	tf.stepStartMs = now;

	if (((tf.index % TF_REDRAW_EVERY) == 0) || (tf.index == TRX_NUM_CTCSS))
	{
		tfRepaint(); // progress
	}
}

bool toneFinderGetText(char *buf, size_t len, uint32_t *ageMs, bool *active)
{
	bool searching = tfSearching();

	if (active != NULL)
	{
		*active = searching;
	}
	if (ageMs != NULL)
	{
		*ageMs = (uint32_t)(ticksGetMillis() - tf.resultMs);
	}

	switch (tf.state)
	{
		case TF_SEARCHING:
		case TF_CONFIRMING:
			if (tf.carrier == 0)
			{
				snprintf(buf, len, "%s", TF("Ton: sinyal yok", "Tone: no signal", "Tono: sin se\xF1""al", "\xD2\xEE\xED: \xED\xE5\xF2 \xF1\xE8\xE3\xED.", "Ton: kein Signal"));
			}
			else
			{
				snprintf(buf, len, "%s %s %d%%", TF("Ton", "Tone", "Tono", "\xD2\xEE\xED", "Ton"), ((tf.index < TRX_NUM_CTCSS) ? "CTCSS" : "DCS"),
						((tf.index * 100) / tfCandidateCount()));
			}
			return true;

		case TF_FOUND:
		{
			char toneText[12];

			tfFormatTone(toneText, sizeof(toneText), tf.result);
			snprintf(buf, len, "%s %s", TF("Ton", "Tone", "Tono", "\xD2\xEE\xED", "Ton"), toneText);
		}
			return true;

		case TF_NOT_FOUND:
			snprintf(buf, len, "%s", TF("Ton yok", "No tone", "Sin tono", "\xCD\xE5\xF2 \xF2\xEE\xED\xE0", "Kein Ton"));
			return true;

		default:
			return false;
	}
}
