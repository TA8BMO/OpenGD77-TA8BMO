/*
 * Copyright (C) 2019-2024 Roger Clark, VK3KYY / G4KYF
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
#include "user_interface/uiGlobals.h"
#include "functions/calibration.h"
#include "functions/trx.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "hardware/radioHardwareInterface.h"
#include <math.h>


#if defined(PLATFORM_MD2017)
#define SECONDARY_DISPLAY_OFFSET  50
#endif


static menuStatus_t menuRSSIExitCode = MENU_STATUS_SUCCESS;
//static calibrationRSSIMeter_t rssiCalibration; // UNUSED
static void updateScreen(bool forceRedraw, bool firstRun);
static void handleEvent(uiEvent_t *ev);
static void updateVoicePrompts(bool flushIt, bool spellIt);

static int dBm[RADIO_DEVICE_MAX] =
{
		0
#if defined(PLATFORM_MD2017)
		, 0
#endif
};

static uint8_t rawSignal[RADIO_DEVICE_MAX] =
{
		0
#if defined(PLATFORM_MD2017)
		, 0
#endif
};

static uint8_t rawNoise[RADIO_DEVICE_MAX] =
{
		0
#if defined(PLATFORM_MD2017)
		, 0
#endif
};

static bool displayRawValues = false;

static const int barX = 9;
DECLARE_SMETER_ARRAY(rssiMeterBar, (DISPLAY_SIZE_X - (barX - 1)));

menuStatus_t menuRSSIScreen(uiEvent_t *ev, bool isFirstRun)
{
	static uint32_t m = 0;

	if (isFirstRun)
	{
		//calibrationGetRSSIMeterParams(&rssiCalibration); // UNUSED
		menuDataGlobal.numItems = 0;
		displayClearBuf();
		menuDisplayTitle(currentLanguage->rssi);
		displayRenderRows(0, 2);

		updateScreen(true, true);
	}
	else
	{
		menuRSSIExitCode = MENU_STATUS_SUCCESS;
		if (ev->hasEvent)
		{
			handleEvent(ev);
		}

		if((ev->time - m) > RSSI_UPDATE_COUNTER_RELOAD)
		{
			m = ev->time;
			updateScreen(false, false);
		}
	}

	return menuRSSIExitCode;
}

// Returns S-Unit 0..9..10(S9+10dB)..15(S9+60)
static int32_t getSignalStrength(int dbm)
{
	for (int8_t i = 15; i >= 0; i--)
	{
		if (dbm >= DBM_LEVELS[i])
		{
			return i;
		}
	}

	return 0;
}

static void drawMeterGraticule(int16_t vOffset)
{
	// Draw S-Meter outer frame
	displayDrawRect((barX - 2), (vOffset - 2), (DISPLAY_SIZE_X - (barX - 2)), (8 + 4), true);
	// Clear the right V line of the frame
	displayDrawFastVLine((DISPLAY_SIZE_X - 1), (vOffset - 1), (8 + 2), false);
	// S9+xx H Dots
	for (int16_t i = ((barX - 2) + (rssiMeterBar[9] * 2) + 1); i < DISPLAY_SIZE_X; i += 4)
	{
		displayDrawFastHLine(i, (vOffset - 2), 2, false);
	}
	// +10..60dB
	displayFillRect(((barX - 2) + (rssiMeterBar[9] * 2) + 2), (vOffset + 8) + 2,
			(DISPLAY_SIZE_X - ((barX - 2) + (rssiMeterBar[9] * 2) + 2)), 2, false);

	// Draw S, Numbers and ticks
	displayPrintAt(1, vOffset, "S", FONT_SIZE_1_BOLD);

	int xPos;
	int currentMode = trxGetMode();

	for (uint8_t i = 0; i < 10; i++)
	{
		// Scale the bar graph so values S0 - S9 take 70% of the scale width, and signals above S9 take the last 30%
		// On DMR the max signal is S9+10, so teh entire bar can be the sale scale
		// ON FM signals above S9, the scale is compressed to 2/STRONG_SIGNAL_RESCALE
		if ((i <= 9) || (currentMode == RADIO_MODE_DIGITAL))
		{
			xPos = rssiMeterBar[i];
		}
		else
		{
			xPos = ((rssiMeterBar[i] - rssiMeterBar[9]) / STRONG_SIGNAL_RESCALE) + rssiMeterBar[9];
		}
		xPos *= 2;

		// V ticks
		displayDrawFastVLine(((barX - 2) + xPos), (vOffset + 8) + 2, ((i % 2) ? 3 : 1), ((i < 10) ? true : false));

		if ((i % 2) && (i < 10))
		{
			char buf[2];

			sprintf(buf, "%d", i);
			displayPrintAt(((((barX - 2) + xPos) - 2) - 1)/* FONT_2 H offset */, vOffset + 15
#if defined(PLATFORM_RD5R)
					-1
#endif
					, buf, FONT_SIZE_2);
		}
	}
}

static void updateScreen(bool forceRedraw, bool isFirstRun)
{
	char buffer[LOCATION_TEXT_BUFFER_SIZE];
	int barWidth;
	int rssi[RADIO_DEVICE_MAX];
	int16_t yValuePos, yBarPos;

	for(RadioDevice_t device = RADIO_DEVICE_PRIMARY; device < RADIO_DEVICE_MAX; device++)
	{
		rssi[device] = dBm[device] = trxGetRSSIdBm(device);
		rawSignal[device] = trxGetSignalRaw(device);
		rawNoise[device] = trxGetNoiseRaw(device);
	}

	if (isFirstRun && (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD))
	{
		voicePromptsInit();
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		voicePromptsAppendLanguageString(currentLanguage->rssi);
		voicePromptsAppendLanguageString(currentLanguage->menu);
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		updateVoicePrompts(false, true);
	}

	if (forceRedraw)
	{
		yBarPos = DISPLAY_Y_POS_RSSI_BAR;

		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		// Clear whole drawing region
		displayFillRect(0, 14, DISPLAY_SIZE_X, DISPLAY_SIZE_Y - 14, true);

		for(RadioDevice_t device = RADIO_DEVICE_PRIMARY; device < RADIO_DEVICE_MAX; device++)
		{
			drawMeterGraticule(yBarPos);
#if defined(PLATFORM_MD2017)
			yBarPos += SECONDARY_DISPLAY_OFFSET;
#endif
		}

		displayThemeResetToDefault();
	}
	else
	{
		yValuePos = DISPLAY_Y_POS_RSSI_VALUE;

		for(RadioDevice_t device = RADIO_DEVICE_PRIMARY; device < RADIO_DEVICE_MAX; device++)
		{
			// Clear dBm region value
			displayFillRect((displayRawValues ? 0 : ((DISPLAY_SIZE_X - (7 * 8)) >> 1)), yValuePos, (displayRawValues ? DISPLAY_SIZE_X : (7 * 8)), FONT_SIZE_3_HEIGHT, true);
#if defined(PLATFORM_MD2017)
			yValuePos += SECONDARY_DISPLAY_OFFSET;
#endif
		}
	}

	yValuePos = DISPLAY_Y_POS_RSSI_VALUE;
	yBarPos = DISPLAY_Y_POS_RSSI_BAR;

	for(RadioDevice_t device = RADIO_DEVICE_PRIMARY; device < RADIO_DEVICE_MAX; device++)
	{
		if (displayRawValues)
		{
			snprintf(buffer, LOCATION_TEXT_BUFFER_SIZE, "%d%s [%u %u]", dBm[device], "dBm", rawSignal[device], rawNoise[device]);
		}
		else
		{
			snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%d%s", dBm[device], "dBm");
		}
		displayPrintCentered(yValuePos, buffer, FONT_SIZE_3);

#if 0 // DEBUG
		sprintf(buffer, "%d", currentRadioDevice->trxRxSignal);
		displayFillRect((DISPLAY_SIZE_X - (4 * 8)), yValuePos, (4 * 8), 8, true);
		ucPrintCore((DISPLAY_SIZE_X - ((strlen(buffer) + 1) * 8)), yValuePos, buffer, FONT_SIZE_2, TEXT_ALIGN_RIGHT, false);
#endif

		if ((rssi[device] > SMETER_S9) && (trxGetMode() == RADIO_MODE_ANALOG))
		{
			// In Analog mode, the max RSSI value from the hardware is over S9+60.
			// So scale this to fit in the last 30% of the display
			rssi[device] = ((rssi[device] - SMETER_S9) / STRONG_SIGNAL_RESCALE) + SMETER_S9;
		}
		// Scale the entire bar by 2.
		// Because above S9 the values are scaled to 1/5.
		// This can result in the signal below S9 being doubled in scale (depending on STRONG_SIGNAL_RESCALE)
		// Signals above S9 the scales is compressed to 2/STRONG_SIGNAL_RESCALE.
		rssi[device] = (rssi[device] - SMETER_S0) * 2;

		barWidth = ((rssi[device] * rssiMeterBarNumUnits) / rssiMeterBarDivider);
		barWidth = CLAMP((barWidth - 1), 0, (DISPLAY_SIZE_X - barX));

		if (barWidth)
		{
			displayThemeApply(THEME_ITEM_FG_RSSI_BAR, THEME_ITEM_BG);
			displayFillRect(barX, yBarPos, barWidth, 8, false);
			displayThemeResetToDefault();
		}

		// Clear the end of the bar area, if needed
		if (barWidth < (DISPLAY_SIZE_X - barX))
		{
			displayFillRect(barX + barWidth, yBarPos, (DISPLAY_SIZE_X - barX) - barWidth, 8, true);
		}

#if defined(HAS_COLOURS)
		if (rssi[device] > SMETER_S9)
		{
			int xPos;

			xPos = (rssiMeterBar[9] * 2);

			if (barWidth > xPos)
			{
				displayThemeApply(THEME_ITEM_FG_RSSI_BAR_S9P, THEME_ITEM_BG);
				displayFillRect((barX + xPos), yBarPos, (barWidth - xPos), 8, false);
				displayThemeResetToDefault();
			}
		}
#endif

#if defined(PLATFORM_MD2017)
		yValuePos += SECONDARY_DISPLAY_OFFSET;
		yBarPos += SECONDARY_DISPLAY_OFFSET;
#endif
	}

	if (forceRedraw || uiNotificationIsVisible())
	{
		displayRender();
	}
	else
	{
		int16_t yStartValuePos = DISPLAY_Y_POS_RSSI_VALUE;
		int16_t yStartBarPos = DISPLAY_Y_POS_RSSI_BAR;

		// Y end positions
		yValuePos = DISPLAY_Y_POS_RSSI_VALUE + FONT_SIZE_3_HEIGHT;
		yBarPos = DISPLAY_Y_POS_RSSI_BAR + 8;

#if defined(PLATFORM_RD5R)
#warning CHECK ME (end pos)
		displayRenderRows((DISPLAY_Y_POS_RSSI_VALUE / 8), (yValuePos / 8) + 1);
#else

		for(RadioDevice_t device = RADIO_DEVICE_PRIMARY; device < RADIO_DEVICE_MAX; device++)
		{
			displayRenderRows((yStartValuePos / 8), (yValuePos / 8) + 1);
			displayRenderRows((yStartBarPos / 8), (yBarPos / 8) + 1);

#if defined(PLATFORM_MD2017)
			yStartValuePos += SECONDARY_DISPLAY_OFFSET;
			yStartBarPos += SECONDARY_DISPLAY_OFFSET;
			yValuePos += SECONDARY_DISPLAY_OFFSET;
			yBarPos += SECONDARY_DISPLAY_OFFSET;
#endif
		}
#endif
	}
}

static void handleEvent(uiEvent_t *ev)
{
	if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
	{
		updateScreen(true, false);
		return;
	}

	if (ev->events & BUTTON_EVENT)
	{
		bool wasPlaying = false;

		if (BUTTONCHECK_SHORTUP(ev, BUTTON_SK1) && (ev->keys.key == 0))
		{
			// Stop playback or update signal strength
			if ((wasPlaying = voicePromptsIsPlaying()) == false)
			{
				updateVoicePrompts(true, false);
			}
		}

		if (repeatVoicePromptOnSK1(ev))
		{
			if (wasPlaying && voicePromptsIsPlaying())
			{
				voicePromptsTerminate();
			}
			return;
		}
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN) || KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		menuSystemPopPreviousMenu();
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
	{
		displayRawValues = !displayRawValues;
		updateScreen(true, false);
	}
	else if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2)))
	{
		saveQuickkeyMenuIndex(ev->keys.key, menuSystemGetCurrentMenuNumber(), 0, 0);
	}
}

static void updateVoicePrompts(bool flushIt, bool spellIt)
{
	if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
	{
		uint8_t S = getSignalStrength(dBm[RADIO_DEVICE_PRIMARY]);

		if (flushIt)
		{
			voicePromptsInit();
		}

		voicePromptsAppendPrompt(PROMPT_S);
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		if (S > 9)
		{
			voicePromptsAppendPrompt(PROMPT_9);
			voicePromptsAppendPrompt(PROMPT_PLUS);
			voicePromptsAppendInteger(10 * ((S - 10) + 1));
		}
		else
		{
			voicePromptsAppendPrompt(PROMPT_0 + S);
		}

		if (spellIt)
		{
			promptsPlayNotAfterTx();
		}
	}
}

// ---------------------------------------------------------------------------
// Fox hunt (ARDF) helper.
//
// A single antenna cannot give a bearing electronically, so this does what a
// real hunt does: monitor one frequency and show a fast signal meter with a
// peak hold. The operator sweeps a directional antenna (or uses body shielding)
// and reads the direction off the peak.
//
// It also estimates the transmitter distance from a typed TX power using the
// path-loss equation. That is a rough figure - terrain and obstacles dominate -
// so both a "typical terrain" and a "free space" (best case) value are shown.
// ---------------------------------------------------------------------------
// Settings the operator can page through with UP/DOWN and edit in place.
#define FOX_SET_FREQ   0
#define FOX_SET_POWER  1
#define FOX_SET_SOUND  2
#define FOX_SET_COUNT  3

static uint32_t foxSavedRxFreq = 0;
static uint32_t foxSavedTxFreq = 0;
static uint32_t foxFreq = 0;
static int      foxPowerW = 5;
static int      foxPeakDbm = -200;
static uint8_t  foxSetting = FOX_SET_FREQ;
static char     foxEntryBuf[8];
static uint8_t  foxEntryLen = 0;
static bool     foxSoundOn = false;
static int      foxSmoothDbm = -200; // slow average, used for the rising/falling arrow
static int      foxTrend = 0;        // +1 getting stronger, -1 weaker, 0 steady
static uint32_t foxLastUpdate = 0;
static uint32_t foxLastBeep = 0;

static void foxDistanceString(char *buf, int len, int dbm)
{
	if ((foxPowerW <= 0) || (dbm <= -125))
	{
		snprintf(buf, len, LANG4("%dW: sinyal zayif", "%dW: signal weak", "%dW: se\xF1""al d\xE9""bil", "%dW: \xF1\xEB\xE0\xE1\xFB\xE9 \xF1\xE8\xE3\xED\xE0\xEB"), foxPowerW);
		return;
	}

	float txDbm    = 10.0f * log10f((float) foxPowerW * 1000.0f); // W -> dBm
	float pathLoss = txDbm - (float) dbm;
	float fMHz     = (float) foxFreq / 100000.0f;
	float k        = (20.0f * log10f(fMHz)) + 32.45f;

	float dTyp  = powf(10.0f, (pathLoss - k) / 30.0f); // n=3, mixed terrain
	float dFree = powf(10.0f, (pathLoss - k) / 20.0f); // n=2, free space

	if (dTyp > 999.0f)
	{
		dTyp = 999.0f;
	}
	if (dFree > 999.0f)
	{
		dFree = 999.0f;
	}

	snprintf(buf, len, LANG4("%dW ~%dkm (ac %d)", "%dW ~%dkm (open %d)", "%dW ~%dkm (lib. %d)", "%dW ~%d\xEA\xEC (\xEE\xF2\xEA\xF0 %d)"), foxPowerW, (int)(dTyp + 0.5f), (int)(dFree + 0.5f));
}

static void foxDrawScreen(int dbm)
{
	char buf[32];
	int32_t s = getSignalStrength(dbm);

	displayClearBuf();
	displayThemeResetToDefault();

	snprintf(buf, sizeof(buf), "%u.%04u", (unsigned)(foxFreq / 100000), (unsigned)((foxFreq % 100000) / 10));
	displayPrintCentered(0, buf, FONT_SIZE_3);

	// Signal bar across the full S range (S0 .. S9+60).
	{
		int w = (int)((s * (DISPLAY_SIZE_X - 8)) / 15);

		displayFillRect(4, 18, (DISPLAY_SIZE_X - 8), 10, true); // clear
		if (w > 0)
		{
			displayFillRect(4, 18, w, 10, false);               // level
		}
	}

	if (s <= 9)
	{
		snprintf(buf, sizeof(buf), "%ddBm S%d  T:%d", dbm, (int) s, foxPeakDbm);
	}
	else
	{
		snprintf(buf, sizeof(buf), "%ddBm S9+%d T:%d", dbm, (int)((s - 9) * 10), foxPeakDbm);
	}
	displayPrintCentered(31, buf, FONT_SIZE_1);

	// Turn-guide: follow the arrow while rotating the antenna to reach the peak.
	{
		const int cx = 18;
		const int ty = 44;
		const char *txt;

		if ((dbm > -120) && ((foxPeakDbm - dbm) <= 2))
		{
			txt = LANG4("TEPE!", "PEAK!", "PICO!", "\xCF\xC8\xCA!");
			displayFillRect((cx - 8), (ty + 4), 16, 9, false);                          // solid = on the peak
		}
		else if (foxTrend > 0)
		{
			txt = LANG4("GUCLENIYOR", "RISING", "SUBIENDO", "\xD0\xC0\xD1\xD2\xA8\xD2");
			displayFillTriangle(cx, ty, (cx - 9), (ty + 14), (cx + 9), (ty + 14), false); // up
		}
		else if (foxTrend < 0)
		{
			txt = LANG4("ZAYIFLIYOR", "FALLING", "BAJANDO", "\xCF\xC0\xC4\xC0\xC5\xD2");
			displayFillTriangle(cx, (ty + 14), (cx - 9), ty, (cx + 9), ty, false);        // down
		}
		else
		{
			txt = LANG4("SABIT", "STEADY", "ESTABLE", "\xD0\xCE\xC2\xCD\xCE");
			displayFillRect((cx - 9), (ty + 6), 18, 3, false);
		}
		displayPrintAt(36, (ty + 3), txt, FONT_SIZE_2);
	}

	foxDistanceString(buf, sizeof(buf), dbm);
	displayPrintCentered(64, buf, FONT_SIZE_1);

	// The setting currently being edited (UP/DOWN pages through them).
	switch (foxSetting)
	{
		case FOX_SET_FREQ:
			if (foxEntryLen > 0)
			{
				snprintf(buf, sizeof(buf), LANG4(">Frek %s_ kHz", ">Freq %s_ kHz", ">Frec %s_ kHz", ">\xD7\xE0\xF1\xF2 %s_ \xEA\xC3\xF6"), foxEntryBuf);
			}
			else
			{
				snprintf(buf, sizeof(buf), LANG4(">Frek %u kHz", ">Freq %u kHz", ">Frec %u kHz", ">\xD7\xE0\xF1\xF2 %u \xEA\xC3\xF6"), (unsigned)(foxFreq / 100));
			}
			break;

		case FOX_SET_POWER:
			if (foxEntryLen > 0)
			{
				snprintf(buf, sizeof(buf), LANG4(">Guc %s_ W", ">Power %s_ W", ">Pot. %s_ W", ">\xCC\xEE\xF9\xED %s_ \xC2\xF2"), foxEntryBuf);
			}
			else
			{
				snprintf(buf, sizeof(buf), LANG4(">Guc %d W", ">Power %d W", ">Pot. %d W", ">\xCC\xEE\xF9\xED %d \xC2\xF2"), foxPowerW);
			}
			break;

		default:
			snprintf(buf, sizeof(buf), LANG4(">Ses %s", ">Sound %s", ">Sonido %s", ">\xC7\xE2\xF3\xEA %s"), (foxSoundOn ? LANG4("Acik", "On", "S\xED", "\xC2\xEA\xEB") : LANG4("Kapali", "Off", "No", "\xC2\xFB\xEA\xEB")));
			break;
	}
	displayPrintCentered(80, buf, FONT_SIZE_2);

	displayPrintCentered(97, LANG4("YUK/ASA=Ayar  0-9=Yaz", "UP/DN=Select 0-9=Type", "ARR/AB=Ajuste 0-9=Teclear", "\xC2\xC2/\xC2\xCD=\xC2\xFB\xE1\xEE\xF0 0-9=\xC2\xE2\xEE\xE4"), FONT_SIZE_1);
	displayPrintCentered(109, LANG4("YESIL=OK #=Tepe R=Cik", "GREEN=OK #=Peak R=Exit", "VERDE=OK #=Pico R=Salir", "\xC7\xC5\xCB=OK #=\xCF\xE8\xEA \xCA=\xC2\xFB\xF5\xEE\xE4"), FONT_SIZE_1);

	displayRender();
}

static void foxApplyEntry(void)
{
	int value = 0;

	// The sound guide is a toggle, not a typed value.
	if (foxSetting == FOX_SET_SOUND)
	{
		foxSoundOn = !foxSoundOn;
		return;
	}

	if (foxEntryLen == 0)
	{
		return;
	}

	for (int i = 0; i < foxEntryLen; i++)
	{
		value = (value * 10) + (foxEntryBuf[i] - '0');
	}

	if (foxSetting == FOX_SET_FREQ)
	{
		uint32_t f = (uint32_t) value * 100U; // typed in kHz -> 10 Hz units

		if ((f >= 13000000U) && (f <= 47000000U)) // 130 - 470 MHz
		{
			foxFreq = f;
			trxSetFrequency(foxFreq, foxFreq, DMR_MODE_AUTO);
			foxPeakDbm = -200;
			foxSmoothDbm = -200;
		}
		else
		{
			soundSetMelody(MELODY_NACK_BEEP);
		}
	}
	else // FOX_SET_POWER
	{
		if ((value > 0) && (value <= 1000))
		{
			foxPowerW = value;
		}
		else
		{
			soundSetMelody(MELODY_NACK_BEEP);
		}
	}

	foxEntryLen = 0;
	foxEntryBuf[0] = 0;
}

menuStatus_t menuFoxHunt(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		keypadAlphaEnable = false; // plain digits on this screen

		foxSavedRxFreq = currentChannelData->rxFreq;
		foxSavedTxFreq = currentChannelData->txFreq;
		foxFreq = currentChannelData->rxFreq;
		foxPeakDbm = -200;
		foxSmoothDbm = -200;
		foxTrend = 0;
		foxSetting = FOX_SET_FREQ;
		foxEntryLen = 0;
		foxEntryBuf[0] = 0;
		foxLastUpdate = 0;
		foxLastBeep = 0;

		trxSetFrequency(foxFreq, foxFreq, DMR_MODE_AUTO);
		foxDrawScreen(trxGetRSSIdBm(RADIO_DEVICE_PRIMARY));
		return MENU_STATUS_SUCCESS;
	}

	if (ev->hasEvent)
	{
		// RED: discard a half-typed value, otherwise restore the radio and leave.
		if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			if (foxEntryLen > 0)
			{
				foxEntryLen = 0;
				foxEntryBuf[0] = 0;
				foxDrawScreen(trxGetRSSIdBm(RADIO_DEVICE_PRIMARY));
			}
			else
			{
				trxSetFrequency(foxSavedRxFreq, foxSavedTxFreq, DMR_MODE_AUTO);
				menuSystemPopPreviousMenu();
			}
			return MENU_STATUS_SUCCESS;
		}

		// UP / DOWN: page through the settings the keypad edits.
		if (KEYCHECK_SHORTUP(ev->keys, KEY_FRONT_UP) || KEYCHECK_SHORTUP(ev->keys, KEY_UP) ||
				KEYCHECK_SHORTUP(ev->keys, KEY_ROTARY_INCREMENT))
		{
			foxSetting = (uint8_t)((foxSetting + 1) % FOX_SET_COUNT);
			foxEntryLen = 0;
			foxEntryBuf[0] = 0;
			foxDrawScreen(trxGetRSSIdBm(RADIO_DEVICE_PRIMARY));
			return MENU_STATUS_SUCCESS;
		}
		if (KEYCHECK_SHORTUP(ev->keys, KEY_FRONT_DOWN) || KEYCHECK_SHORTUP(ev->keys, KEY_DOWN) ||
				KEYCHECK_SHORTUP(ev->keys, KEY_ROTARY_DECREMENT))
		{
			foxSetting = (uint8_t)((foxSetting == 0) ? (FOX_SET_COUNT - 1) : (foxSetting - 1));
			foxEntryLen = 0;
			foxEntryBuf[0] = 0;
			foxDrawScreen(trxGetRSSIdBm(RADIO_DEVICE_PRIMARY));
			return MENU_STATUS_SUCCESS;
		}

		// GREEN: apply the typed value, or toggle the audio guide.
		if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			foxApplyEntry();
			foxDrawScreen(trxGetRSSIdBm(RADIO_DEVICE_PRIMARY));
			return MENU_STATUS_SUCCESS;
		}

		// HASH: clear the peak hold.
		if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
		{
			foxPeakDbm = -200;
			foxDrawScreen(trxGetRSSIdBm(RADIO_DEVICE_PRIMARY));
			return MENU_STATUS_SUCCESS;
		}

		// Digits go into whichever setting is selected.
		if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && (foxSetting != FOX_SET_SOUND))
		{
			if (foxEntryLen < (sizeof(foxEntryBuf) - 1))
			{
				foxEntryBuf[foxEntryLen++] = (char) ev->keys.key;
				foxEntryBuf[foxEntryLen] = 0;
			}
			foxDrawScreen(trxGetRSSIdBm(RADIO_DEVICE_PRIMARY));
			return MENU_STATUS_SUCCESS;
		}
	}

	// Live meter - fast, so sweeping the antenna shows the peak clearly.
	if ((ev->time - foxLastUpdate) > 100)
	{
		int dbm = trxGetRSSIdBm(RADIO_DEVICE_PRIMARY);

		foxLastUpdate = ev->time;

		if (foxSmoothDbm < -190)
		{
			foxSmoothDbm = dbm;
		}

		// Compare against the slow average -> "keep turning" / "turn back".
		if (dbm >= (foxSmoothDbm + 2))
		{
			foxTrend = 1;
		}
		else if (dbm <= (foxSmoothDbm - 2))
		{
			foxTrend = -1;
		}
		else
		{
			foxTrend = 0;
		}
		foxSmoothDbm = (((foxSmoothDbm * 7) + dbm) / 8);

		if (dbm > foxPeakDbm)
		{
			foxPeakDbm = dbm;
		}

		// Audio guide: a blip whose pitch rises with the signal (eyes-free hunting).
		if (foxSoundOn && ((ev->time - foxLastBeep) > 300) && (dbm > -120))
		{
			static int16_t foxBeep[4];
			int32_t sv = getSignalStrength(dbm);

			foxLastBeep = ev->time;
			foxBeep[0] = (int16_t)(400 + ((sv * 1600) / 15));
			foxBeep[1] = 60;
			foxBeep[2] = -1;
			foxBeep[3] = -1;
			soundSetMelody(foxBeep);
		}

		foxDrawScreen(dbm);
	}

	return MENU_STATUS_SUCCESS;
}
