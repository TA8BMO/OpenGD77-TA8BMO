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
#include "hardware/HR-C6000.h"
#include "hardware/radioHardwareInterface.h"
#include "hardware/AT1846S.h"
#include "functions/sound.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "interfaces/clockManager.h"
#include "functions/satellite.h"
#include "functions/rxPowerSaving.h"
#include "functions/morseDecode.h"
#include "functions/aprsDecode.h"
#include "functions/callLog.h"
#include "functions/voiceRecorder.h"
#include <math.h>
#if defined(HAS_GPS)
#include "interfaces/gps.h"
#endif
#if !defined(PLATFORM_GD77S)
#include "functions/aprs.h"
#endif

static void updateScreen(void);
static void handleEvent(uiEvent_t *ev);

#define PIT_COUNTS_PER_SECOND    1000U

static int timeInSeconds;
static ticksTimer_t nextSecondTimer = { 0, 0 };
static bool isShowingLastHeard;
static bool startBeepPlayed;
static uint32_t m = 0, micm = 0, mto = 0;
static bool keepScreenShownOnError = false;
static bool pttWasReleased = false;
static bool isTransmittingTone = false;
static bool isTransmittingDTMF = false;
static bool aprsPTTBeaconTriggered = false;
static bool transmitError = false;
uint32_t xmitErrorTimer = 0;
static bool isSatelliteScreen;

// ---------------------------------------------------------------------------
// 5-Tone ANI encoder (table-based, extensible to other standards).
// CCIR is the default standard. Additional standards (ZVEI1/ZVEI2/EEA...) can
// be appended to FIVE_TONE_STANDARDS[] with the same structure and later be
// made user-selectable; the encoder itself is standard-agnostic.
// ---------------------------------------------------------------------------
typedef struct
{
	const char *name;
	uint16_t    toneFreq[16];    // [0..9] = digits, [10..15] = A..F / special tones
	uint16_t    toneDurationMs;  // duration of each tone
	uint16_t    gapMs;           // silence between tones (0 = continuous)
	uint8_t     repeatToneIndex; // tone used when a digit repeats the previous one
} fiveToneStandard_t;

static const fiveToneStandard_t FIVE_TONE_STANDARDS[] =
{
	// [0] CCIR-1, 100 ms  (default / first working profile).
	//   Digit freqs 0-9 per CCIR-1. A(index 10)=2400 Hz is the GROUP tone; the
	//   REPEAT tone is E(index 14)=2110 Hz. Repeated digits use the E repeat tone
	//   (see fiveToneSend); tones are contiguous (0 ms gap).
	{
		"CCIR 100",
		{
			1981, 1124, 1197, 1275, 1358, 1446, 1540, 1640, 1747, 1860, // 0-9
			2400,  930, 2247,  991, 2110, 2600                          // A(group),B,C,D,E(repeat),F
		},
		100, 0, 14
	},
	// [1] CCIR-1, 70 ms  (same frequencies, shorter tone period).
	{
		"CCIR 70",
		{
			1981, 1124, 1197, 1275, 1358, 1446, 1540, 1640, 1747, 1860,
			2400,  930, 2247,  991, 2110, 2600
		},
		70, 0, 14
	},
	// [2] CCIR-1, 20 ms  (fast).
	{
		"CCIR 20",
		{
			1981, 1124, 1197, 1275, 1358, 1446, 1540, 1640, 1747, 1860,
			2400,  930, 2247,  991, 2110, 2600
		},
		20, 0, 14
	},
	// [3] EEA, 40 ms. Same digit frequencies as CCIR; E(14)=2110 repeat tone.
	//     NOTE: A/B/C/D/F extra tones + timing to be verified against a reference
	//     decoder before this profile is relied on in the field.
	{
		"EEA",
		{
			1981, 1124, 1197, 1275, 1358, 1446, 1540, 1640, 1747, 1860,
			1055,  930, 2247,  991, 2110, 2400
		},
		40, 0, 14
	},
	// ZVEI1 / ZVEI2 to be added once their exact frequency tables are confirmed.
};
#define FIVE_TONE_STANDARD_DEFAULT 0 // CCIR 100

// Delay after key-up before the first tone, so the PLL/PA settle. It is also how
// long after PTT the 5-Tone burst starts - 250ms is what felt right to me.
#define FIVE_TONE_TX_SETTLE_MS     250
// Extra settle after switching the AT1846S to its tone generator, so the very
// first tone is fully on-air before its timed duration starts. Without it the
// first digit came out clipped on the other radio.
#define FIVE_TONE_SWITCH_SETTLE_MS 30
// Silent carrier held after the last tone, so the far decoder has a clean edge to
// finish the fifth digit against - see the end of fiveToneSend().
#define FIVE_TONE_TX_TAIL_MS       100

// Delay for `ms` while keeping the side-tone melody advancing. soundTickMelody() is
// normally pumped by the main application loop, which is blocked for the whole burst
// while fiveToneSend() runs, so without ticking it here the operator would hear
// nothing locally (the far radio still hears the tones - those come straight from
// the AT1846S tone generator, not from this melody).
static void fiveToneDelayMs(uint32_t ms)
{
	for (uint32_t t = 0; t < ms; t++)
	{
		soundTickMelody();
		vTaskDelay((1 / portTICK_PERIOD_MS));
	}
}

// Sends 5 digits (each 0..9) as sequential single tones for the selected standard,
// as a real CCIR/Select-5 ANI burst. Analog FM only, TX already active.
//
// CCIR repeat rule: if a digit's own tone would equal the PREVIOUS EMITTED tone,
// the repeat tone E (2110 Hz) is sent instead, so two identical consecutive digits
// never produce two identical adjacent tones. The stored/displayed code is never
// altered - the repeat tone is applied only while encoding. Examples:
//   03310 -> 0,3,E,1,0   (1981,1275,2110,1124,1981)
//   11234 -> 1,E,2,3,4    12234 -> 1,2,E,3,4    11111 -> 1,E,1,E,1
//
// Tones are contiguous (0 ms inter-tone gap). Each tone is a clean single-frequency
// sine produced by the AT1846S hardware tone generator (register 0x35) modulated
// onto the FM carrier - the same path used for CTCSS and the 1750 Hz tone burst.
// This is NOT a square wave / PWM / GPIO toggle. The exact table frequency (x10,
// i.e. tenths of Hz) is written to the register, so there is no rounding drift.
//
// Two audio paths run in parallel and are independent:
//   - AT1846S tone generator -> RF        (the far radio decodes this - the ANI);
//   - a local "melody"        -> speaker  (a separate low-level operator side-tone;
//                                          the RF signal does not depend on it).
static void fiveToneSend(const uint8_t digits[5])
{
	const fiveToneStandard_t *std = &FIVE_TONE_STANDARDS[FIVE_TONE_STANDARD_DEFAULT];
	static int16_t sideToneMelody[(5 * 2) + 2]; // static: stays valid while it plays
	uint16_t emitFreq[5];
	int prevFreq = -1;

	// Encode the 5 digits into the actual emitted tone frequencies (repeat rule).
	for (int i = 0; i < 5; i++)
	{
		uint8_t d = (digits[i] <= 9) ? digits[i] : 0;
		uint16_t f = std->toneFreq[d];
		if ((int) f == prevFreq)
		{
			f = std->toneFreq[std->repeatToneIndex]; // CCIR repeat tone E
		}
		emitFreq[i] = f;
		prevFreq = (int) f;

		sideToneMelody[(i * 2)]     = (int16_t) f;                     // side-tone: same
		sideToneMelody[(i * 2) + 1] = (int16_t) std->toneDurationMs;   // sequence + timing
	}
	sideToneMelody[10] = -1;
	sideToneMelody[11] = -1;

	// Settle after key-up, route the AT1846S to its tone generator (once) with the
	// first tone loaded, and let it settle so the first tone is fully on-air before
	// its timed duration starts. Then start the local side-tone and drive the RF
	// tone frequencies in step, contiguously (no inter-tone gap).
	fiveToneDelayMs(FIVE_TONE_TX_SETTLE_MS);
	radioSetTone1((int) emitFreq[0]);
	fiveToneDelayMs(FIVE_TONE_SWITCH_SETTLE_MS);

	soundSetMelody(sideToneMelody);

	for (int i = 0; i < 5; i++)
	{
		AT1846sWriteTone1Reg(emitFreq[i] * 10); // exact frequency (tenths of Hz)
		fiveToneDelayMs(std->toneDurationMs);
	}

	radioSetTone1(0);  // tone reg -> 0 (no DC left) and restore the microphone path

	// Hold the carrier on for a moment after the last tone.
	//
	// The decoder at the far end measures each tone by how long it lasts, and the
	// fifth one is only complete when the tone stops - not when the carrier does.
	// On a quick press and release the operator has already let go before the
	// burst finishes, so the instant this returns the transmission is terminated
	// and the carrier collapses on the same millisecond as the last tone. The far
	// decoder is still integrating at that point and drops the digit. That is
	// exactly what I kept seeing on the receiving radio: hold the PTT and all five digits
	// arrive, tap it and the last one comes and goes.
	//
	// A silent carrier gives it a clean edge to finish against. It costs nothing
	// when the operator is holding the PTT to speak - the tail runs while they are
	// still pressing, and releasing ends the transmission immediately, as before.
	//
	// Placed here rather than at the call sites so it covers all four: the burst
	// at PTT start, the one at PTT end, the automatic reply and the selective call
	// screen. The last two key the transmitter themselves and drop it directly
	// after the burst, so they had the same race with nothing holding the carrier
	// up at all.
	fiveToneDelayMs(FIVE_TONE_TX_TAIL_MS);

	soundStopMelody(); // stop the side-tone if it outlasts the RF part
}

menuStatus_t menuTxScreen(uiEvent_t *ev, bool isFirstRun)
{
	int radioMode = trxGetMode();

	if (isFirstRun)
	{
		monitorModeData.isEnabled = false;
		voicePromptsTerminateNoTail();
		startBeepPlayed = false;
		aprsPTTBeaconTriggered = false;
		transmitError = false;
		uiDataGlobal.Scan.active = false;
		uiDataGlobal.displayChannelSettings = false;
		isTransmittingTone = false;
		isTransmittingDTMF = false;
		isShowingLastHeard = false;
		keepScreenShownOnError = false;
		timeInSeconds = 0;
		pttWasReleased = false;
		xmitErrorTimer = 0;
		isSatelliteScreen = false;
		ticksTimerReset(&nextSecondTimer);

		if (radioMode == RADIO_MODE_DIGITAL)
		{
			clockManagerSetRunMode(kAPP_PowerModeHsrun, CLOCK_MANAGER_SPEED_HS_RUN);
		}
		else
		{
			isSatelliteScreen = (menuSystemGetPreviousMenuNumber() == MENU_SATELLITE);
		}

#if defined(PLATFORM_GD77S)
		uiChannelModeHeartBeatActivityForGD77S(ev); // Dim all lit LEDs
#endif

		// If the user was currently entering a new frequency and the PTT get pressed, "leave" that input screen.
		if (uiDataGlobal.FreqEnter.index > 0)
		{
			freqEnterReset();
			updateScreen();
		}

		if ((codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_RX_ONLY) == 0) && ((nonVolatileSettings.txFreqLimited == BAND_LIMITS_NONE) || trxCheckFrequencyInAmateurBand(currentChannelData->txFreq)
#if defined(PLATFORM_MD9600)
				|| (codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_OUT_OF_BAND) != 0)
#endif
		))
		{
			ticksTimerStart(&nextSecondTimer, PIT_COUNTS_PER_SECOND);
			timeInSeconds = currentChannelData->tot * 15;

#if defined(HAS_GPS)
			if (nonVolatileSettings.gps > GPS_MODE_OFF)
			{
				gpsDataInputStartStop(false);
			}
#endif

			LedWrite(LED_GREEN, 0);
			LedWrite(LED_RED, 1);

			HRC6000ClearIsWakingState();

			if (radioMode == RADIO_MODE_ANALOG)
			{
				trxSetTxCSS(currentChannelData->txTone);

				if (isSatelliteScreen)
				{
					// Make sure Tx freq is updated before transmission is enabled
					trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, DMR_MODE_AUTO);
				}

				trxSetTX();

				// 5-Tone ANI: send at PTT start if configured (analog FM only, TX active).
				{
					uint8_t aniDigits[5];
					aniTxMode_t aniMode = codeplugChannelGetFiveTone(currentChannelData, aniDigits);
					if ((aniMode == ANI_MODE_PTT_START) || (aniMode == ANI_MODE_PTT_BOTH))
					{
						fiveToneSend(aniDigits);
					}
				}

#if !defined(PLATFORM_GD77S)
				if (isSatelliteScreen && (currentSatelliteFreqIndex == SATELLITE_APRS_FREQ))
				{
					// TXDelay
					uint32_t m = ticksGetMillis();
					while((ticksGetMillis() - m) < APRS_XMIT_TX_DELAY)
					{
						vTaskDelay((1 / portTICK_PERIOD_MS));
					}

					// Aprs.
					aprsBeaconingSendBeacon(true);
				}
#endif
			}
			else
			{
				if (nonVolatileSettings.locationLat != SETTINGS_UNITIALISED_LOCATION_LAT)
				{
					HRC6000SetTalkerAliasLocation(nonVolatileSettings.locationLat, nonVolatileSettings.locationLon);
				}

				// RADIO_MODE_DIGITAL
				if (!((slotState >= DMR_STATE_REPEATER_WAKE_1) && (slotState <= DMR_STATE_REPEATER_WAKE_3)) )
				{
					trxSetTX();
				}
			}

#if !defined(PLATFORM_GD77S)
			if ((isSatelliteScreen == false) || (isSatelliteScreen && (aprsTxProgress == APRS_TX_IDLE)))
#endif
			{
				updateScreen();
			}
		}
		else
		{
			menuTxScreenHandleTxTermination(ev, ((codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_RX_ONLY) != 0) ? TXSTOP_RX_ONLY : TXSTOP_OUT_OF_BAND));
			transmitError = true;
		}

		m = micm = ev->time;
	}
	else
	{
		// Keep displaying the "RX Only" or "Out Of Band" error message
		if (xmitErrorTimer > 0)
		{
			// Wait the voice ends, then count-down 200ms;
			if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
			{
				if (voicePromptsIsPlaying())
				{
					xmitErrorTimer = (20 * 10U);
					return MENU_STATUS_SUCCESS;
				}
			}

			xmitErrorTimer--;

			if (xmitErrorTimer == 0)
			{
				ev->buttons &= ~BUTTON_PTT; // prevent screen lockout if the operator keeps pressing the PTT button.
			}
			else
			{
				return MENU_STATUS_SUCCESS;
			}
		}

#if !defined(PLATFORM_GD77S)
		// APRS Beaconing is running
		if (trxTransmissionEnabled)
		{
			// while in satellite mode => no screen redrawing, just waiting for the process ends, the rest is handled in the APRS subsystem.
			if ((isSatelliteScreen && (currentSatelliteFreqIndex == SATELLITE_APRS_FREQ) && (aprsTxProgress != APRS_TX_IDLE)) ||
					// While PTT beaconing is enabled, the rest is handled in the aprs subsystem.
					((isSatelliteScreen == false) && (aprsBeaconingGetMode() == APRS_BEACONING_MODE_PTT) && (aprsTxProgress != APRS_TX_IDLE)))
			{
				if (ticksTimerHasExpired(&nextSecondTimer))
				{
					ticksTimerStart(&nextSecondTimer, PIT_COUNTS_PER_SECOND);
				}
				return MENU_STATUS_SUCCESS;
			}
		}
#endif

		if (trxTransmissionEnabled && ((HRC6000GetIsWakingState() == WAKING_MODE_NONE) || (HRC6000GetIsWakingState() == WAKING_MODE_AWAKEN)))
		{
			if (ticksTimerHasExpired(&nextSecondTimer))
			{
				ticksTimerStart(&nextSecondTimer, PIT_COUNTS_PER_SECOND);

				if (currentChannelData->tot == 0)
				{
					timeInSeconds++;
				}
				else
				{
					timeInSeconds--;
					if (timeInSeconds <= (nonVolatileSettings.txTimeoutBeepX5Secs * 5))
					{
						if ((timeInSeconds % 5) == 0)
						{
							soundSetMelody(MELODY_KEY_BEEP);
						}
					}
				}

				if ((currentChannelData->tot != 0) && (timeInSeconds == 0))
				{
					menuTxScreenHandleTxTermination(ev, TXSTOP_TIMEOUT);
					keepScreenShownOnError = true;
				}
				else
				{
					if (!isShowingLastHeard)
					{
						updateScreen();
					}
				}
			}
			else
			{
				if (radioMode == RADIO_MODE_DIGITAL)
				{
					if ((nonVolatileSettings.beepOptions & BEEP_TX_START) &&
							(startBeepPlayed == false) && (trxIsTransmitting == true)
							&& (melody_play == NULL))
					{
						startBeepPlayed = true;// set this even if the beep is not actaully played because of the vox, as otherwise this code will get continuously run
						// If VOX is running, don't send a beep as it will reset its the trigger status.
						if ((voxIsEnabled() == false) || (voxIsEnabled() && (voxIsTriggered() == false)))
						{
							soundSetMelody(MELODY_DMR_TX_START_BEEP);
						}
					}
				}

				// Do not update Mic level on Timeout.
#if !defined(PLATFORM_GD77S)
				if (aprsTxProgress == APRS_TX_IDLE)
				{
					if (((((currentChannelData->tot != 0) && (timeInSeconds == 0)) == false) && (ev->time - micm) > 100))
					{
						if (radioMode == RADIO_MODE_DIGITAL)
						{
							uiUtilityDrawDMRMicLevelBarGraph();
						}
						else
						{
							uiUtilityDrawFMMicLevelBarGraph();
						}

						displayRenderRows(1, 2);;//return;// don't do anything as it will affect the transmission !

						micm = ev->time;
					}
				}
#endif
			}
		}

		// Timeout happened, postpone going further otherwise timeout
		// screen won't be visible at all.
		if (((currentChannelData->tot != 0) && (timeInSeconds == 0)) || keepScreenShownOnError)
		{
			// Wait the voice ends, then count-down 500ms;
			if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
			{
				if (voicePromptsIsPlaying())
				{
					mto = ev->time;
					return MENU_STATUS_SUCCESS;
				}
			}

			if ((ev->time - mto) < 500)
			{
				return MENU_STATUS_SUCCESS;
			}

			keepScreenShownOnError = false;
			ev->buttons &= ~BUTTON_PTT; // prevent screen lockout if the operator keeps pressing the PTT button.
		}

		// Once the PTT key has been released, it is forbidden to re-key before the whole TX chain
		// has finished (see the first statement in handleEvent())
		// That's important in DMR mode, otherwise quickly press/release the PTT key will left
		// the system in an unexpected state (RED led on, displayed TXScreen, but PA off).
		// It doesn't have any impact on FM mode.
		if (((ev->buttons & BUTTON_PTT) == 0) && (pttWasReleased == false))
		{
			pttWasReleased = true;
		}

		if ((ev->buttons & BUTTON_PTT) && pttWasReleased)
		{
			ev->buttons &= ~BUTTON_PTT;
		}

		// Got an event, or
		if (ev->hasEvent || // PTT released, Timeout triggered,
				( (((ev->buttons & BUTTON_PTT) == 0) || ((currentChannelData->tot != 0) && (timeInSeconds == 0))) ||
						// or waiting for DMR ending (meanwhile, updating every 100ms)
						((trxTransmissionEnabled == false) && ((ev->time - m) > 100))))
		{
			handleEvent(ev);
			m = ev->time;
		}
		else
		{
			if ((HRC6000GetIsWakingState() == WAKING_MODE_FAILED) && (trxTransmissionEnabled == true))
			{
				trxTransmissionEnabled = false;
				menuTxScreenHandleTxTermination(ev, TXSTOP_TIMEOUT);
				keepScreenShownOnError = true;
			}
		}
	}
	return MENU_STATUS_SUCCESS;
}

bool menuTxScreenDisplaysLastHeard(void)
{
	return isShowingLastHeard;
}

static void updateScreen(void)
{
#if !defined(PLATFORM_GD77S)
	uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;

	if (isSatelliteScreen)
	{
		menuSatelliteTxScreen(timeInSeconds);
	}
	else
	{
		if (menuSystemGetRootMenuNumber() == UI_VFO_MODE)
		{
			uiVFOModeUpdateScreen(timeInSeconds);
		}
		else
		{
			uiChannelModeUpdateScreen(timeInSeconds);
		}
	}

	if (nonVolatileSettings.backlightMode != BACKLIGHT_MODE_BUTTONS)
	{
		displayLightOverrideTimeout(-1);
	}
#endif
}

static void handleEvent(uiEvent_t *ev)
{
	// Xmiting ends (normal or timeouted)
	if (((ev->buttons & BUTTON_PTT) == 0) || (((currentChannelData->tot != 0) && (timeInSeconds == 0))))
	{
		if (trxTransmissionEnabled)
		{
#if !defined(PLATFORM_GD77S)
			if ((transmitError == false) &&
					(isSatelliteScreen == false) && (aprsBeaconingGetMode() == APRS_BEACONING_MODE_PTT) && (aprsPTTBeaconTriggered == false))
			{
				aprsPTTBeaconTriggered = true;

				if (aprsBeaconingSendBeacon(false))
				{
					return;
				}
			}
#endif

			trxTransmissionEnabled = false;

			if(isTransmittingTone || isTransmittingDTMF)
			{
#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_DM1801) || defined(PLATFORM_DM1801A) || defined(PLATFORM_RD5R)
				trxSelectVoiceChannel(AT1846_VOICE_CHANNEL_MIC);
				disableAudioAmp(AUDIO_AMP_MODE_RF);
#else

#if defined(PLATFORM_MD9600)
				trxDTMFoff(true);
#else // PLATFORM_MD9600
				if(isTransmittingDTMF)
				{
					trxDTMFoff(true);
				}
				else
				{
					trxSetTone1(0);
				}
#endif // PLATFORM_MD9600

				if(soundMelodyIsPlaying())
				{
					soundStopMelody();
				}
#endif
				isTransmittingTone = false;
				isTransmittingDTMF = false;
			}

			if (trxGetMode() == RADIO_MODE_ANALOG)
			{
				// 5-Tone ANI: send at PTT end if configured, while RF is still active.
				// Bounded loop (5 tones) so releasing PTT cannot lock up or loop forever.
				{
					uint8_t aniDigits[5];
					aniTxMode_t aniMode = codeplugChannelGetFiveTone(currentChannelData, aniDigits);
					if ((aniMode == ANI_MODE_PTT_END) || (aniMode == ANI_MODE_PTT_BOTH))
					{
						fiveToneSend(aniDigits);
					}
				}

				// In analog mode. Stop transmitting immediately
				LedWrite(LED_RED, 0);

#if defined(HAS_GPS)
				if (nonVolatileSettings.gps > GPS_MODE_OFF)
				{
					gpsDataInputStartStop(true);
				}
#endif

				// Need to wrap this in Task Critical to avoid bus contention on the I2C bus.
				trxSetRxCSS(RADIO_DEVICE_PRIMARY, currentChannelData->rxTone);
				trxActivateRx(true);
				trxIsTransmitting = false;

				menuSystemPopPreviousMenu();
				uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN; // we need immediate redraw
			}
			else
			{
				HRC6000ClearIsWakingState();

				if (isShowingLastHeard)
				{
					isShowingLastHeard = false;
					updateScreen();
				}
			}
			// When not in analog mode, only the trxIsTransmitting flag is cleared
			// This screen keeps getting called via the handleEvent function and goes into the else clause - below.
		}
		else
		{
			// In DMR mode, wait for the DMR system to finish before exiting
			if (trxIsTransmitting == false)
			{
				if ((nonVolatileSettings.beepOptions & BEEP_TX_STOP) && (melody_play == NULL) && (HRC6000GetIsWakingState() != WAKING_MODE_FAILED))
				{
					soundSetMelody(MELODY_DMR_TX_STOP_BEEP);
				}

				LedWrite(LED_RED, 0);

#if defined(HAS_GPS)
				if (nonVolatileSettings.gps > GPS_MODE_OFF)
				{
					gpsDataInputStartStop(true);
				}
#endif

				// If there is a signal, lit the Green LED
				if ((LedRead(LED_GREEN) == 0) && (trxCarrierDetected(RADIO_DEVICE_PRIMARY) || (getAudioAmpStatus() & AUDIO_AMP_MODE_RF)))
				{
					LedWrite(LED_GREEN, 1);
				}

				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					clockManagerSetRunMode(kAPP_PowerModeRun, CLOCK_MANAGER_SPEED_RUN);
				}

				HRC6000ClearIsWakingState();

				menuSystemPopPreviousMenu();
				uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN; // we need immediate redraw
			}
		}

		// Waiting for TX to end, no need to go further.
		return;
	}

	// Key action while xmitting (ANALOG), Tone triggering
	if ((isTransmittingTone == false) && (isTransmittingDTMF == false) &&
			((ev->buttons & BUTTON_PTT) != 0) && trxTransmissionEnabled && (trxGetMode() == RADIO_MODE_ANALOG))
	{
		int keyval = menuGetKeypadKeyValue(ev, false);
		// Send 1750Hz
		if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
		{
			if (isSatelliteScreen)
			{
				if (satelliteDataNative[uiDataGlobal.SatelliteAndAlarmData.currentSatellite].freqs[currentSatelliteFreqIndex].armCTCSS != 0)
				{
					trxActivateRx(true);
					trxSetTxCSS(satelliteDataNative[uiDataGlobal.SatelliteAndAlarmData.currentSatellite].freqs[currentSatelliteFreqIndex].armCTCSS);
					trxSetTX();
				}
			}
#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_DM1801) || defined(PLATFORM_DM1801A) || defined(PLATFORM_RD5R)
			else
			{
				trxSetTone1(1750);
				trxSelectVoiceChannel(AT1846_VOICE_CHANNEL_TONE1);
				enableAudioAmp(AUDIO_AMP_MODE_RF);
				GPIO_PinWrite(GPIO_RX_audio_mux, Pin_RX_audio_mux, 1);
				isTransmittingTone = true;
			}
#elif ! defined(PLATFORM_MD9600)
			else
			{	//send 1750
				trxSetTone1(1750);
				soundSetMelody(MELODY_1750);
				isTransmittingTone = true;
			}
#endif
		}
#if defined(PLATFORM_MD9600)
		else if ((keyval == 16)	&& (isSatelliteScreen == false)) // A/B key
		{	//send 1750
			trxSetTone1(1750);
			soundSetMelody(MELODY_1750);
			isTransmittingTone = true;
		}
#endif
		else
		{	// Send DTMF
			if ((keyval != 99) && (isSatelliteScreen == false))
			{
				trxSetDTMF(keyval);
				isTransmittingDTMF = true;
#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_DM1801) || defined(PLATFORM_DM1801A) || defined(PLATFORM_RD5R)
				trxSelectVoiceChannel(AT1846_VOICE_CHANNEL_DTMF);
				enableAudioAmp(AUDIO_AMP_MODE_RF);
				GPIO_PinWrite(GPIO_RX_audio_mux, Pin_RX_audio_mux, 1);
#else
				soundSetMelody(MELODY_DTMF);
#endif
			}
		}
	}

	// Stop xmitting Tone
	if ((isTransmittingTone || isTransmittingDTMF) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0) && ((ev->keys.key == 0) || (ev->keys.event & KEY_MOD_UP)))
	{
		if (isSatelliteScreen)
		{
			if (satelliteDataNative[uiDataGlobal.SatelliteAndAlarmData.currentSatellite].freqs[currentSatelliteFreqIndex].armCTCSS != 0)
			{
				trxActivateRx(true);
				trxSetTxCSS(currentChannelData->txTone);
				trxSetTX();
			}
		}
		else
		{
#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_DM1801) || defined(PLATFORM_DM1801A) || defined(PLATFORM_RD5R)
			trxSelectVoiceChannel(AT1846_VOICE_CHANNEL_MIC);
			disableAudioAmp(AUDIO_AMP_MODE_RF);
#else

#if defined(PLATFORM_MD9600)
			trxDTMFoff(true);
#else // PLATFORM_MD9600
			if(isTransmittingDTMF)
			{
				trxDTMFoff(true);
			}
			else
			{
				trxSetTone1(0);
			}
#endif // PLATFORM_MD9600

			if(soundMelodyIsPlaying())
			{
				soundStopMelody();
			}
#endif
		}

		isTransmittingTone = false;
		isTransmittingDTMF = false;
	}

#if !defined(PLATFORM_GD77S)
	if ((trxGetMode() == RADIO_MODE_DIGITAL) && BUTTONCHECK_SHORTUP(ev, BUTTON_SK1) && (trxTransmissionEnabled == true))
	{
		isShowingLastHeard = !isShowingLastHeard;
		if (isShowingLastHeard)
		{
			menuLastHeardInit();
			menuLastHeardUpdateScreen(false, false, false);
		}
		else
		{
			updateScreen();
		}
	}

	// Forward key events to LH screen, if shown
	if (isShowingLastHeard && (ev->events & KEY_EVENT))
	{
		menuLastHeardHandleEvent(ev);
	}
#endif

}

void menuTxScreenHandleTxTermination(uiEvent_t *ev, txTerminationReason_t reason)
{
	PTTToggledDown = false;
	voxReset();

	voicePromptsTerminateNoTail();
	voicePromptsInit();

#if !defined(PLATFORM_GD77S)
	displayClearBuf();
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG_NOTIFICATION);
	displayDrawRoundRectWithDropShadow(4, 4, 120 + DISPLAY_H_EXTRA_PIXELS, (DISPLAY_SIZE_Y - 6), 5, true);
#endif

	switch (reason)
	{
		case TXSTOP_RX_ONLY:
		case TXSTOP_OUT_OF_BAND:
#if !defined(PLATFORM_GD77S)
			displayThemeApply(THEME_ITEM_FG_ERROR_NOTIFICATION, THEME_ITEM_BG_NOTIFICATION);
			displayPrintCentered(4 + (DISPLAY_V_EXTRA_PIXELS / 4), currentLanguage->error, FONT_SIZE_4);
#endif

			voicePromptsAppendLanguageString(currentLanguage->error);
			voicePromptsAppendPrompt(PROMPT_SILENCE);

			if (codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_RX_ONLY) != 0)
			{
#if !defined(PLATFORM_GD77S)
				displayPrintCentered((DISPLAY_SIZE_Y - 24) - (DISPLAY_V_EXTRA_PIXELS / 4), currentLanguage->rx_only, FONT_SIZE_3);
#endif
				voicePromptsAppendLanguageString(currentLanguage->rx_only);
			}
			else
			{
#if !defined(PLATFORM_GD77S)
				displayPrintCentered((DISPLAY_SIZE_Y - 24) - (DISPLAY_V_EXTRA_PIXELS / 4), currentLanguage->out_of_band, FONT_SIZE_3);
#endif
				voicePromptsAppendLanguageString(currentLanguage->out_of_band);
			}
			xmitErrorTimer = (100 * 10U);
			break;

		case TXSTOP_TIMEOUT:
#if !defined(PLATFORM_GD77S)
			displayThemeApply(THEME_ITEM_FG_WARNING_NOTIFICATION, THEME_ITEM_BG_NOTIFICATION);
			displayPrintCentered(((DISPLAY_SIZE_Y - FONT_SIZE_4_HEIGHT) / 2), currentLanguage->timeout, FONT_SIZE_4);
#endif

			// From G4EML commit:
			//      Timeout Voice Prompt doesn't work on DMR.  It actually sends a distorted version of the prompt on the transmission.
			//      Presumably because the codec cant handle encoding and decoding at the same time.
#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_DM1801) || defined(PLATFORM_DM1801A) || defined(PLATFORM_RD5R)
			voicePromptsAppendLanguageString(currentLanguage->timeout);
#endif

			if (menuSystemGetCurrentMenuNumber() == UI_TX_SCREEN)
			{
				mto = ev->time;
			}
			break;
	}

#if !defined(PLATFORM_GD77S)
	displayThemeResetToDefault();
	displayRender();
	displayLightOverrideTimeout(-1);
#endif

	if ((nonVolatileSettings.audioPromptMode < AUDIO_PROMPT_MODE_VOICE_THRESHOLD) || (reason == TXSTOP_TIMEOUT))
	{
		soundSetMelody((reason == TXSTOP_TIMEOUT) ? MELODY_TX_TIMEOUT_BEEP : MELODY_ERROR_BEEP);
	}
	else
	{
		voicePromptsPlay();
	}
}

// ---------------------------------------------------------------------------
// Selective call ("Secmeli Cagri"): one-shot 5-Tone transmission with a code
// typed on the fly, without touching the channel's stored ANI code.
// Entered with SK2+# from the channel/VFO screens in analog FM mode.
// The TX keying sequence mirrors the DTMF contact path in dtmfSequenceTick().
// ---------------------------------------------------------------------------
static uint8_t selCallDigits[5];
static uint8_t selCallNumDigits = 0;
static bool    selCallSending   = false;

static void selCallShowScreen(bool sending)
{
	char buf[SCREEN_LINE_BUFFER_SIZE];
	int p = 0;

	displayClearBuf();
	displayThemeResetToDefault();
	displayPrintCentered(4, LANG4("Secmeli Cagri", "Selective Call", "Llamada selectiva", "\xD1\xE5\xEB\xE5\xEA\xF2. \xE2\xFB\xE7\xEE\xE2"), FONT_SIZE_3);

	for (int i = 0; i < 5; i++)
	{
		buf[p++] = ((i < selCallNumDigits) ? ('0' + selCallDigits[i]) : '-');
		if (i < 4)
		{
			buf[p++] = ' ';
		}
	}
	buf[p] = 0;
	displayPrintCentered(44, buf, FONT_SIZE_3);

	if (sending)
	{
		displayPrintCentered((DISPLAY_SIZE_Y - 20), LANG4("Gonderiliyor...", "Sending...", "Enviando...", "\xCF\xE5\xF0\xE5\xE4\xE0\xF7\xE0..."), FONT_SIZE_1);
	}
	else
	{
		displayPrintCentered((DISPLAY_SIZE_Y - 26), LANG4("YESIL = Gonder", "GREEN = Send", "VERDE = Enviar", "\xC7\xC5\xCB = \xCF\xE5\xF0\xE5\xE4\xE0\xF2\xFC"), FONT_SIZE_1);
		displayPrintCentered((DISPLAY_SIZE_Y - 14), LANG4("KIRMIZI = Sil / Cikis", "RED = Delete / Exit", "ROJO = Borrar / Salir", "\xCA\xD0\xC0\xD1 = \xD3\xE4\xE0\xEB / \xC2\xFB\xF5\xEE\xE4"), FONT_SIZE_1);
	}
	displayRender();
}

// Blocking one-shot 5-Tone TX. Mirrors the DTMF contact keying path
// (dtmfSequenceTick): same enable, same restore. Does NOT touch the channel's
// stored ANI code - it transmits the RAM buffer typed on this screen.
// Three short beeps, so an operator who was away from the radio knows it
// answered a call while they were gone.
static const int16_t fiveToneReplyMelody[] =
{
	880, 90, 0, 70, 880, 90, 0, 70, 880, 90, -1, -1
};

// Automatic answer to a selective call addressed to this channel's own ANI.
// Same sequence as the manual selective call below, driven from the decoder
// rather than the keypad. Returns false when the radio is in no state to send.
bool uiTxScreenSendFiveToneReply(const uint8_t digits[5])
{
	if (selCallSending || trxTransmissionEnabled || trxIsTransmitting || (trxGetMode() != RADIO_MODE_ANALOG) ||
			(codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_RX_ONLY) != 0) ||
			((nonVolatileSettings.txFreqLimited != BAND_LIMITS_NONE) && (trxCheckFrequencyInAmateurBand(currentChannelData->txFreq) == false)))
	{
		return false;
	}

	selCallSending = true;

	voicePromptsTerminateNoTail();
	rxPowerSavingSetState(ECOPHASE_POWERSAVE_INACTIVE);
	trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, DMR_MODE_AUTO);
	trxSetTxCSS(currentChannelData->txTone);
	HRC6000ClearIsWakingState();

	trxEnableTransmission();

	fiveToneSend(digits); // blocking: TX settle + 5 tones + local side-tone

	trxDisableTransmission();

	if (trxTransmissionEnabled)
	{
		trxTransmissionEnabled = false;
		trxSetRX();
		LedWrite(LED_GREEN, 0);
		trxSetModeAndBandwidth(currentChannelData->chMode, (codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_BW_25K) != 0));
		trxSetTxCSS(currentChannelData->txTone);
	}

	selCallSending = false;

	// This is an alert for an operator who may be across the room, so it is
	// played louder than an ordinary key beep. The divider is a right shift, so
	// each step down is 6dB; drop two and put the operator's setting back once
	// the melody has finished. fiveToneDelayMs() keeps soundTickMelody() running
	// while we wait, since the main loop is still blocked in here.
	{
		int savedDivider = soundBeepVolumeDivider;

		soundBeepVolumeDivider = ((savedDivider > 2) ? (savedDivider - 2) : 0);
		soundSetMelody((int16_t *) fiveToneReplyMelody);
		fiveToneDelayMs(600);
		soundBeepVolumeDivider = savedDivider;
	}

	return true;
}

static void selCallTransmit(void)
{
	if (selCallSending || trxTransmissionEnabled || (trxGetMode() != RADIO_MODE_ANALOG) ||
			(codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_RX_ONLY) != 0) ||
			((nonVolatileSettings.txFreqLimited != BAND_LIMITS_NONE) && (trxCheckFrequencyInAmateurBand(currentChannelData->txFreq) == false)))
	{
		soundSetMelody(MELODY_NACK_BEEP);
		return;
	}

	selCallSending = true;

	// Bring the radio fully awake and quiet, matching menuTxScreen's proven PTT
	// sequence (ECO power-up, stop voice prompts, clear C6000 wake state) so the
	// blocking burst never writes to a half-powered AT1846S/C6000.
	voicePromptsTerminateNoTail();
	rxPowerSavingSetState(ECOPHASE_POWERSAVE_INACTIVE);
	trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, DMR_MODE_AUTO);
	trxSetTxCSS(currentChannelData->txTone);
	HRC6000ClearIsWakingState();

	trxEnableTransmission();

	fiveToneSend(selCallDigits); // blocking: TX settle + 5 tones + local side-tone

	trxDisableTransmission();

	if (trxTransmissionEnabled)
	{
		trxTransmissionEnabled = false;
		trxSetRX();
		LedWrite(LED_GREEN, 0);
		trxSetModeAndBandwidth(currentChannelData->chMode, (codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_BW_25K) != 0));
		trxSetTxCSS(currentChannelData->txTone);
	}

	selCallSending = false;
}

menuStatus_t menuSelectiveCall5Tone(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		selCallNumDigits = 0;
		selCallSending = false;
		memset(selCallDigits, 0, sizeof(selCallDigits));
		selCallShowScreen(false);
		return (MENU_STATUS_INPUT_TYPE | MENU_STATUS_SUCCESS);
	}

	if (ev->hasEvent)
	{
		// RED: delete one digit; when empty, leave the screen.
		if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			if (selCallNumDigits > 0)
			{
				selCallNumDigits--;
				selCallShowScreen(false);
			}
			else
			{
				menuSystemPopPreviousMenu();
			}
			return MENU_STATUS_SUCCESS;
		}

		// GREEN: transmit the entered 5 tones once, then stay on-screen to resend/edit.
		if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			if (selCallNumDigits == 5)
			{
				selCallShowScreen(true);
				selCallTransmit();
				selCallShowScreen(false);
			}
			else
			{
				soundSetMelody(MELODY_NACK_BEEP);
			}
			return MENU_STATUS_SUCCESS;
		}

		// Digit keys 0-9: append until 5 are entered.
		if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && (selCallNumDigits < 5))
		{
			selCallDigits[selCallNumDigits++] = (uint8_t)(ev->keys.key - '0');
			selCallShowScreen(false);
			return MENU_STATUS_SUCCESS;
		}
	}

	return MENU_STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// Morse (CW) text sender. STAR on an analog channel opens it. Type with the
// keypad (multi-tap, like channel names); GREEN keys the text out as Morse on
// the AT1846S tone generator; RED backspaces; long RED clears and exits.
// Analog FM only; never touches any stored setting.
// ---------------------------------------------------------------------------
#define MORSE_MAX_LEN  20
#define CW_TONE_HZ     700

// Sending speed. A dot is 1200/WPM milliseconds by the usual definition, so 12
// WPM gives the 100ms dot this used to be fixed at. Held in RAM only: it is a
// per-session preference, and writing it to flash would mean touching the
// decoder's stored log.
#define MORSE_WPM_MIN  5
#define MORSE_WPM_MAX  30
static uint8_t morseWpm = 12;

static int morseDotMs(void)
{
	return (1200 / morseWpm);
}

static char morseText[MORSE_MAX_LEN + 1];
static uint8_t morseTextLen = 0; // uint8_t: main RAM is full, and none of these passes 255
static bool morseSending = false;

static const char * const MORSE_AZ[26] = {
	".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---",
	"-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-",
	"..-", "...-", ".--", "-..-", "-.--", "--.."
};
static const char * const MORSE_09[10] = {
	"-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----."
};

// The reverse lookup, for turning what the operator keys back into a letter.
static char morseCharForPattern(const char *pat)
{
	if (pat[0] == 0)
	{
		return 0;
	}

	for (int i = 0; i < 26; i++)
	{
		if (strcmp(pat, MORSE_AZ[i]) == 0)
		{
			return (char)('A' + i);
		}
	}
	for (int i = 0; i < 10; i++)
	{
		if (strcmp(pat, MORSE_09[i]) == 0)
		{
			return (char)('0' + i);
		}
	}
	return 0;
}

// ---------------------------------------------------------------------------
// PTT used as a straight key.
//
// The radio must not transmit while this is in use - PTT is blocked for these
// screens in applicationMain.c - so the only output is the side-tone in the
// operator's own speaker. Timing follows the usual proportions: an element is a
// dash when held for two dots or longer, a character ends after three dots of
// silence, and a word after seven.
// ---------------------------------------------------------------------------
#define MORSE_KEY_MAX_ELEMENTS 8
#define MORSE_KEY_DEBOUNCE_MS  20

static struct
{
	bool     down;
	uint32_t edgeMs;
	char     sym[MORSE_KEY_MAX_ELEMENTS + 1];
	int      symLen;
	bool     spaceDone;
} morseKey;

static bool morseKeyerSpaces = true; // set by whichever screen owns the key

static int16_t morseSideMelody[4];

static void morseSideToneOn(void)
{
	morseSideMelody[0] = CW_TONE_HZ;
	morseSideMelody[1] = 5000; // held; stopped on key up
	morseSideMelody[2] = -1;
	morseSideMelody[3] = -1;
	soundSetMelody(morseSideMelody);
}

static void morseSideToneOff(void)
{
	soundStopMelody();
}

static void morseKeyerReset(void)
{
	morseKey.down = false;
	morseKey.edgeMs = ticksGetMillis();
	morseKey.sym[0] = 0;
	morseKey.symLen = 0;
	morseKey.spaceDone = true;
	morseSideToneOff();
}

// Call every pass. Returns a decoded character once one completes, a space at a
// word gap, or 0 when there is nothing yet. Unrecognised element runs come back
// as '*' rather than being dropped, so a mistake is visible.
static char morseKeyerTick(bool pttDown)
{
	uint32_t now = ticksGetMillis();
	int dot = morseDotMs();

	if (pttDown != morseKey.down)
	{
		uint32_t held = (now - morseKey.edgeMs);

		morseKey.down = pttDown;
		morseKey.edgeMs = now;

		if (pttDown)
		{
			morseSideToneOn();
			morseKey.spaceDone = false;
		}
		else
		{
			morseSideToneOff();

			if ((held >= MORSE_KEY_DEBOUNCE_MS) && (morseKey.symLen < MORSE_KEY_MAX_ELEMENTS))
			{
				morseKey.sym[morseKey.symLen++] = ((held >= (uint32_t)(2 * dot)) ? '-' : '.');
				morseKey.sym[morseKey.symLen] = 0;
			}
		}
		return 0;
	}

	if (morseKey.down == false)
	{
		uint32_t idle = (now - morseKey.edgeMs);

		if ((morseKey.symLen > 0) && (idle >= (uint32_t)(3 * dot)))
		{
			char c = morseCharForPattern(morseKey.sym);

			morseKey.sym[0] = 0;
			morseKey.symLen = 0;
			return ((c != 0) ? c : '*');
		}

		// Word gaps are optional, because whether they help depends on the
		// screen. Seven dots is the textbook figure, but anyone still learning
		// pauses far longer than that between letters, so on the practice screen
		// - where the target is a single word - every letter was arriving with a
		// space after it and nothing ever matched.
		if (morseKeyerSpaces && (morseKey.symLen == 0) && (morseKey.spaceDone == false) &&
				(idle >= (uint32_t)(7 * dot)))
		{
			morseKey.spaceDone = true;
			return ' ';
		}
	}

	return 0;
}


// True while there is anything worth showing - the key is down, or elements are
// waiting for the character to close.
static bool morseKeyerIsActive(void)
{
	return (morseKey.down || (morseKey.symLen > 0));
}

// The element run including the one under the key at this instant.
//
// morseKeyerCurrent() only grows on release, so nothing appeared on screen until
// after the key came up. Showing the element as it forms - starting as a dot and
// becoming a dash if the key is held - is what makes the difference between the
// two learnable.
#define MORSE_KEYER_LIVE_LEN (MORSE_KEY_MAX_ELEMENTS + 2)

static void morseKeyerLive(char *out, int outSize)
{
	int n = 0;

	while ((morseKey.sym[n] != 0) && (n < (outSize - 2)))
	{
		out[n] = morseKey.sym[n];
		n++;
	}

	if (morseKey.down && (n < (outSize - 1)))
	{
		uint32_t held = (ticksGetMillis() - morseKey.edgeMs);

		if (held >= MORSE_KEY_DEBOUNCE_MS)
		{
			out[n++] = ((held >= (uint32_t)(2 * morseDotMs())) ? '-' : '.');
		}
	}

	out[n] = 0;
}

static const char *morseForChar(char c)
{
	if ((c >= 'a') && (c <= 'z'))
	{
		c -= 32; // to upper case
	}
	if ((c >= 'A') && (c <= 'Z'))
	{
		return MORSE_AZ[c - 'A'];
	}
	if ((c >= '0') && (c <= '9'))
	{
		return MORSE_09[c - '0'];
	}
	switch (c)
	{
		case '.': return ".-.-.-";
		case ',': return "--..--";
		case '?': return "..--..";
		case '/': return "-..-.";
		case '=': return "-...-";
		case '-': return "-....-";
		case '+': return ".-.-.";
		case '@': return ".--.-.";
	}
	return NULL; // unknown character -> skipped
}

static void morseSend(const char *text)
{
	static int16_t morseBeep[4];

	if (morseSending || trxTransmissionEnabled || (trxGetMode() != RADIO_MODE_ANALOG) ||
			(codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_RX_ONLY) != 0) ||
			((nonVolatileSettings.txFreqLimited != BAND_LIMITS_NONE) && (trxCheckFrequencyInAmateurBand(currentChannelData->txFreq) == false)))
	{
		soundSetMelody(MELODY_NACK_BEEP);
		return;
	}

	morseSending = true;

	// Same wake/TX sequence as the selective-call sender (proven path).
	voicePromptsTerminateNoTail();
	rxPowerSavingSetState(ECOPHASE_POWERSAVE_INACTIVE);
	trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, DMR_MODE_AUTO);
	trxSetTxCSS(currentChannelData->txTone);
	HRC6000ClearIsWakingState();
	trxEnableTransmission();

	fiveToneDelayMs(200); // PA/PLL settle

	for (int i = 0; text[i] != 0; i++)
	{
		char c = text[i];

		if (c == ' ')
		{
			fiveToneDelayMs(4 * morseDotMs()); // word gap (~7 units total)
			continue;
		}

		const char *pat = morseForChar(c);
		if (pat == NULL)
		{
			continue;
		}

		for (int e = 0; pat[e] != 0; e++)
		{
			int onMs = (pat[e] == '-') ? (3 * morseDotMs()) : morseDotMs();

			morseBeep[0] = CW_TONE_HZ;
			morseBeep[1] = (int16_t) onMs;
			morseBeep[2] = -1;
			morseBeep[3] = -1;

			radioSetTone1(CW_TONE_HZ); // transmitted tone ON
			soundSetMelody(morseBeep); // local side-tone
			fiveToneDelayMs(onMs);
			radioSetTone1(0);          // tone OFF for the gap
			soundStopMelody();
			fiveToneDelayMs(morseDotMs()); // 1-unit inter-element gap
		}

		fiveToneDelayMs(2 * morseDotMs()); // + 2 units => 3-unit inter-character gap
	}

	radioSetTone1(0);
	soundStopMelody();

	trxDisableTransmission();
	if (trxTransmissionEnabled)
	{
		trxTransmissionEnabled = false;
		trxSetRX();
		LedWrite(LED_GREEN, 0);
		trxSetModeAndBandwidth(currentChannelData->chMode, (codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_BW_25K) != 0));
		trxSetTxCSS(currentChannelData->txTone);
	}

	morseSending = false;
}

static void morseShowScreen(bool sending, char preview)
{
	char line[MORSE_MAX_LEN + 4];
	int n = 0;

	for (int i = 0; i < morseTextLen; i++)
	{
		line[n++] = morseText[i];
	}
	if (preview != 0)
	{
		line[n++] = preview;
	}
	line[n++] = '_';
	line[n] = 0;

	displayClearBuf();
	displayThemeResetToDefault();
	displayPrintCentered(6, LANG4("Mors Gonder", "Send Morse", "Enviar Morse", "\xCF\xE5\xF0\xE5\xE4\xE0\xF2\xFC CW"), FONT_SIZE_3);
	displayPrintCentered(34, line, FONT_SIZE_1);

	if (sending)
	{
		displayPrintCentered((DISPLAY_SIZE_Y - 20), LANG4("Gonderiliyor...", "Sending...", "Enviando...", "\xCF\xE5\xF0\xE5\xE4\xE0\xF7\xE0..."), FONT_SIZE_1);
	}
	else
	{
		char info[24];

		// Speed, and whatever is part-keyed on the PTT right now.
		snprintf(info, sizeof(info), LANG4("Hiz: %u WPM", "Speed: %u WPM", "Vel.: %u WPM", "\xD1\xEA\xEE\xF0.: %u WPM"), (unsigned int) morseWpm);
		displayPrintCentered(50, info, FONT_SIZE_1);

		{
			char live[MORSE_KEYER_LIVE_LEN];

			morseKeyerLive(live, sizeof(live));

			if (live[0] != 0)
			{
				displayPrintCentered(62, live, FONT_SIZE_3);
			}
		}

		displayPrintCentered((DISPLAY_SIZE_Y - 40), LANG4("PTT = mors yaz", "PTT = key Morse", "PTT = manipular", "PTT = \xEA\xEB\xFE\xF7 \xCC\xEE\xF0\xE7\xE5"), FONT_SIZE_1);
		displayPrintCentered((DISPLAY_SIZE_Y - 30), LANG4("Yukari/Asagi = Hiz", "Up/Down = Speed", "Arriba/Abajo = Vel.", "\xC2\xE2\xE5\xF0\xF5/\xC2\xED\xE8\xE7 = \xD1\xEA\xEE\xF0."), FONT_SIZE_1);
		displayPrintCentered((DISPLAY_SIZE_Y - 20), LANG4("YESIL = Gonder", "GREEN = Send", "VERDE = Enviar", "\xC7\xC5\xCB = \xCF\xE5\xF0\xE5\xE4\xE0\xF2\xFC"), FONT_SIZE_1);
		displayPrintCentered((DISPLAY_SIZE_Y - 10), LANG4("KIRMIZI = Sil / Basili = Cikis", "RED = Delete / Hold = Exit", "ROJO=Borrar / Largo=Salir", "\xCA\xD0\xC0\xD1=\xD3\xE4\xE0\xEB / \xC4\xEE\xEB\xE3\xEE=\xC2\xFB\xF5\xEE\xE4"), FONT_SIZE_1);
	}
	displayRender();
}

menuStatus_t menuMorse(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		morseTextLen = 0;
		morseText[0] = 0;
		morseSending = false;
		morseKeyerReset();
		morseKeyerSpaces = true;  // sentences here, so word gaps are wanted
		keypadAlphaEnable = true; // keypad now types letters (driver multi-tap)
		morseShowScreen(false, 0);
		return (MENU_STATUS_INPUT_TYPE | MENU_STATUS_SUCCESS);
	}

	// The PTT works as a straight key here, alongside the keypad. It cannot
	// transmit: applicationMain.c holds PTT off for this screen. Polled on every
	// pass, not only on events, because the timing is what carries the meaning.
	if (morseSending == false)
	{
		char keyed = morseKeyerTick((ev->buttons & BUTTON_PTT) != 0);

		if (keyed != 0)
		{
			if ((morseTextLen < MORSE_MAX_LEN) && ((keyed != ' ') || (morseTextLen > 0)))
			{
				morseText[morseTextLen++] = keyed;
				morseText[morseTextLen] = 0;
			}
			morseShowScreen(false, 0);
		}
		else if (morseKeyerIsActive())
		{
			static uint32_t lastKeyDraw = 0;

			// Show the element run as it builds, but not on every pass - a full
			// repaint each time would starve the keypad.
			if ((uint32_t)(ev->time - lastKeyDraw) >= 40)
			{
				lastKeyDraw = ev->time;
				morseShowScreen(false, 0);
			}
		}
	}

	if (ev->hasEvent)
	{
		// Long RED: clear everything and leave.
		if (KEYCHECK_LONGDOWN(ev->keys, KEY_RED) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_RED) == false))
		{
			morseTextLen = 0;
			morseText[0] = 0;
			morseSideToneOff();
			keypadAlphaEnable = false;
			menuSystemPopPreviousMenu();
			return MENU_STATUS_SUCCESS;
		}

		// Short RED: delete the last character; leave when already empty.
		if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			if (morseTextLen > 0)
			{
				morseTextLen--;
				morseText[morseTextLen] = 0;
				morseShowScreen(false, 0);
			}
			else
			{
				morseSideToneOff();
				keypadAlphaEnable = false;
				menuSystemPopPreviousMenu();
			}
			return MENU_STATUS_SUCCESS;
		}

		// Sending speed, shared with the training screen and with what goes out
		// on the air.
		if (KEYCHECK_SHORTUP(ev->keys, KEY_UP) || KEYCHECK_SHORTUP(ev->keys, KEY_DOWN))
		{
			if (KEYCHECK_SHORTUP(ev->keys, KEY_UP))
			{
				if (morseWpm < MORSE_WPM_MAX)
				{
					morseWpm++;
				}
			}
			else if (morseWpm > MORSE_WPM_MIN)
			{
				morseWpm--;
			}
			morseShowScreen(false, 0);
			return MENU_STATUS_SUCCESS;
		}

		// STAR again: through to the decoded-Morse log. Checked before the text
		// entry block below, which would otherwise type a '*' into the message.
		if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
		{
			morseSideToneOff();
			keypadAlphaEnable = false;
			menuSystemPushNewMenu(UI_MORSE_LOG);
			return MENU_STATUS_SUCCESS;
		}

		// GREEN: key the text out as Morse, then stay on-screen to resend/edit.
		if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			if (morseTextLen > 0)
			{
				morseShowScreen(true, 0);
				morseSend(morseText);
				morseShowScreen(false, 0);
			}
			else
			{
				soundSetMelody(MELODY_NACK_BEEP);
			}
			return MENU_STATUS_SUCCESS;
		}

		// Keypad text entry - the driver resolves multi-tap to a letter in keys.key.
		if ((ev->keys.key >= ' ') && (ev->keys.key < 127) && (morseTextLen < MORSE_MAX_LEN))
		{
			if (ev->keys.event & KEY_MOD_PREVIEW)
			{
				morseShowScreen(false, (char) ev->keys.key); // live preview while cycling
				return MENU_STATUS_SUCCESS;
			}
			if (ev->keys.event & KEY_MOD_PRESS)
			{
				morseText[morseTextLen++] = (char) ev->keys.key;
				morseText[morseTextLen] = 0;
				morseShowScreen(false, 0);
				return MENU_STATUS_SUCCESS;
			}
		}
	}

	return MENU_STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// Decoded Morse log. Reached with STAR from the Morse send screen, so one key
// gets to sending and a second to reading. Entries are filtered to the tuned
// frequency, so a message copied on one channel does not show up on another.
// ---------------------------------------------------------------------------
// SCREEN_LINE_BUFFER_SIZE is 17 - sixteen characters - because it is sized for
// the 8 pixel font. These log screens use the 6 pixel one, which fits 26 across,
// so they need their own buffer: with the shared one snprintf was quietly
// cutting every line at sixteen while the wrap still advanced by 26, losing ten
// characters per line.
#define APRS_LINE_CHARS 26
#define APRS_LINE_BUF   (APRS_LINE_CHARS + 2)

static int  morseLogIndex = 0;
static bool morseLogConfirmClear = false;

static void morseLogShowScreen(void)
{
	// Own buffer, for the same reason as the APRS screen above: the shared one
	// is sized for 16 characters of the larger font, and this uses the small one.
	char buf[APRS_LINE_BUF];
	const char *live = morseDecodeGetLive();
	int count = morseDecodeGetEntryCount();
	int y;

	displayClearBuf();
	menuDisplayTitle(LANG4("MORS KAYIT", "MORSE LOG", "REGISTRO MORSE", "\xC6\xD3\xD0\xCD\xC0\xCB CW"));

	if (morseLogConfirmClear)
	{
		displayPrintCentered(40, LANG4("TUM KAYITLAR", "DELETE ALL", "BORRAR TODOS", "\xD3\xC4\xC0\xCB\xC8\xD2\xDC \xC2\xD1\xC5"), FONT_SIZE_3);
		displayPrintCentered(58, LANG4("SILINSIN MI?", "ENTRIES?", "LOS REGISTROS?", "\xC7\xC0\xCF\xC8\xD1\xC8?"), FONT_SIZE_3);
		displayPrintCentered(90, LANG4("# Evet", "# Yes", "# S\xED", "# \xC4\xE0"), FONT_SIZE_3);
		displayPrintCentered(108, LANG4("Kirmizi Hayir", "Red No", "Rojo No", "\xCA\xF0\xE0\xF1 \xCD\xE5\xF2"), FONT_SIZE_3);
		displayRender();
		return;
	}

	// Whatever is being copied right now, before it has been committed.
	if (live[0] != 0)
	{
		displayPrintAt(2, 18, LANG4("Simdi:", "Now:", "Ahora:", "\xD1\xE5\xE9\xF7\xE0\xF1:"), FONT_SIZE_1);
		snprintf(buf, sizeof(buf), "%.*s", APRS_LINE_CHARS, live);
		displayPrintAt(2, 27, buf, FONT_SIZE_1);
	}

	if (count == 0)
	{
		displayPrintCentered(60, LANG4("Kayit yok", "No entries", "Sin registros", "\xCD\xE5\xF2 \xE7\xE0\xEF\xE8\xF1\xE5\xE9"), FONT_SIZE_3);
		displayPrintAt(2, 108, LANG4("* Mors gonder", "* Send Morse", "* Enviar Morse", "* \xCF\xE5\xF0\xE5\xE4\xE0\xF2\xFC CW"), FONT_SIZE_1);
		displayPrintAt(2, 117, LANG4("Kirmizi Cikis", "Red Exit", "Rojo Salir", "\xCA\xF0\xE0\xF1 \xC2\xFB\xF5\xEE\xE4"), FONT_SIZE_1);
		displayRender();
		return;
	}

	if (morseLogIndex >= count)
	{
		morseLogIndex = (count - 1);
	}

	snprintf(buf, sizeof(buf), LANG4("Kayit %d / %d", "Entry %d / %d", "Registro %d / %d", "\xC7\xE0\xEF\xE8\xF1\xFC %d / %d"), (morseLogIndex + 1), count);
	displayPrintAt(2, 40, buf, FONT_SIZE_1);

	// 26 characters fit on a line in the small font, so wrap across three.
	{
		const char *txt = morseDecodeGetEntryText(morseLogIndex);
		int len = (int) strlen(txt);
		int pos = 0;

		y = 52;
		while ((pos < len) && (y < 96))
		{
			snprintf(buf, sizeof(buf), "%.*s", APRS_LINE_CHARS, (txt + pos));
			displayPrintAt(2, y, buf, FONT_SIZE_1);
			pos += APRS_LINE_CHARS;
			y += 10;
		}
	}

	displayPrintAt(2, 99, LANG4("Yuk/Asa kayitlar", "Up/Down entries", "Arr/Aba registros", "\xC2\xE2\xE5\xF0\xF5/\xC2\xED\xE8\xE7 \xE7\xE0\xEF\xE8\xF1\xE8"), FONT_SIZE_1);
	displayPrintAt(2, 108, LANG4("# Hepsini sil  * Gonder", "# Delete all  * Send", "# Borrar todo  * Enviar", "# \xD3\xE4\xE0\xEB. \xE2\xF1\xE5  * \xCF\xE5\xF0\xE5\xE4\xE0\xF2\xFC"), FONT_SIZE_1);
	displayPrintAt(2, 117, LANG4("Kirmizi Cikis", "Red Exit", "Rojo Salir", "\xCA\xF0\xE0\xF1 \xC2\xFB\xF5\xEE\xE4"), FONT_SIZE_1);
	displayRender();
}

menuStatus_t menuMorseLog(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		morseLogIndex = 0;
		morseLogConfirmClear = false;
		morseLogShowScreen();
		return MENU_STATUS_SUCCESS;
	}

	if (ev->hasEvent)
	{
		if (morseLogConfirmClear)
		{
			if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
			{
				morseDecodeClearAll();
				morseLogConfirmClear = false;
				morseLogIndex = 0;
				soundSetMelody(MELODY_ACK_BEEP);
				morseLogShowScreen();
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
			{
				morseLogConfirmClear = false;
				morseLogShowScreen();
			}
			return MENU_STATUS_SUCCESS;
		}

		if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			menuSystemPopPreviousMenu();
			return MENU_STATUS_SUCCESS;
		}

		// STAR again: on to the practice screen, so the key walks through
		// sending, the log and practice in turn. RED comes back.
		if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
		{
			menuSystemPushNewMenu(UI_MORSE_TRAIN);
			return MENU_STATUS_SUCCESS;
		}

		if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
		{
			if (morseDecodeGetEntryCount() > 0)
			{
				morseLogConfirmClear = true;
			}
			morseLogShowScreen();
			return MENU_STATUS_SUCCESS;
		}

		if (KEYCHECK_SHORTUP(ev->keys, KEY_DOWN))
		{
			if (morseLogIndex < (morseDecodeGetEntryCount() - 1))
			{
				morseLogIndex++;
			}
			morseLogShowScreen();
			return MENU_STATUS_SUCCESS;
		}

		if (KEYCHECK_SHORTUP(ev->keys, KEY_UP))
		{
			if (morseLogIndex > 0)
			{
				morseLogIndex--;
			}
			morseLogShowScreen();
			return MENU_STATUS_SUCCESS;
		}
	}

	return MENU_STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// Decoded APRS packets, with the demodulator counters alongside.
//
// The counters matter while this is being proven: "Bayrak" counts HDLC flags,
// so it shows the demodulator is locking onto the bit stream at all, even when
// nothing survives its checksum. Flags climbing with zero good frames means the
// demodulator works and the framing does not; no flags at all means the problem
// is earlier, in the audio or the tone discrimination.
// ---------------------------------------------------------------------------
// The APRS information field is structured, so it is worth taking apart rather
// than printing raw. The first character says what kind of report it is; for a
// position report the latitude, symbol, longitude and comment follow at fixed
// offsets. See the example that first decoded off air:
//
//   !3730.64NW03649.55E#TA.DX
//   ^ type   ^sym table  ^sym code
//    ^latitude   ^longitude     ^comment
// Turkish letters are written as \xNN escapes, matching turkish.h, so this file
// stays plain ASCII and cannot be broken by a re-encoding. The display font maps
// them itself: 0x93 i-dotless, 0x94 I-dot, 0x95/0x96 S-cedilla, 0x98 g-breve,
// 0xC7/0xE7 C-cedilla, 0xD6/0xF6 O-umlaut, 0xDC/0xFC U-umlaut.
static const char *aprsSymbolName(char code)
{
	switch (code)
	{
		case '#': return "Digipeater";
		case '&': return "Gateway";
		case 'I': return "IGate";
		case '-': return LANG4("Ev", "House", "Casa", "\xC4\xEE\xEC");
		case '>': return LANG4("Araba", "Car", "Coche", "\xC0\xE2\xF2\xEE");
		case 'k': return LANG4("Kamyon", "Truck", "Cami\xF3n", "\xC3\xF0\xF3\xE7\xEE\xE2\xE8\xEA");
		case 'u': return LANG4("Kamyonet", "Pickup", "Furgoneta", "\xCF\xE8\xEA\xE0\xEF");
		case 'v': return LANG4("Minib\xFC""s", "Van", "Minib\xFAs", "\xCC\xE8\xEA\xF0\xEE\xE0\xE2\xF2\xEE\xE1\xF3\xF1");
		case '<': return LANG4("Motosiklet", "Motorcycle", "Moto", "\xCC\xEE\xF2\xEE\xF6\xE8\xEA\xEB");
		case 'j': return "Jeep";
		case 'b': return LANG4("Bisiklet", "Bicycle", "Bicicleta", "\xC2\xE5\xEB\xEE\xF1\xE8\xEF\xE5\xE4");
		case '[': return LANG4("Yaya", "Person", "Persona", "\xD7\xE5\xEB\xEE\xE2\xE5\xEA");
		case '_': return LANG4("Hava ist.", "Weather", "Meteo", "\xCC\xE5\xF2\xE5\xEE");
		case 'r': return LANG4("R\xF6""le", "Repeater", "Repetidor", "\xD0\xE5\xF2\xF0\xE0\xED\xF1\xEB\xFF\xF2\xEE\xF0");
		case 'Y': return LANG4("Tekne", "Boat", "Barca", "\xCB\xEE\xE4\xEA\xE0");
		case 's': return LANG4("Gemi", "Ship", "Barco", "\xCA\xEE\xF0\xE0\xE1\xEB\xFC");
		case '^': return LANG4("U\xE7""ak", "Aircraft", "Avi\xF3n", "\xD1\xE0\xEC\xEE\xEB\xB8\xF2");
		case 'O': return LANG4("Balon", "Balloon", "Globo", "\xD8\xE0\xF0");
		case 'a': return LANG4("Ambulans", "Ambulance", "Ambulancia", "\xD1\xEA\xEE\xF0\xE0\xFF");
		case 'f': return LANG4("\x94""tfaiye", "Fire brigade", "Bomberos", "\xCF\xEE\xE6\xE0\xF0\xED\xFB\xE5");
		case 'P': return LANG4("Polis", "Police", "Polic\xED""a", "\xCF\xEE\xEB\xE8\xF6\xE8\xFF");
		case '$': return LANG4("Telefon", "Phone", "Tel\xE9""fono", "\xD2\xE5\xEB\xE5\xF4\xEE\xED");
		default:  return NULL;
	}
}

// Degrees and minutes to degrees with four decimals, in integers only: this
// build links newlib-nano, whose printf has no floating point at all.
static int aprsMinutesToFrac(int mm, int hundredths)
{
	return ((((mm * 100) + hundredths) * 10) + 3) / 6;
}

static int aprsDigits(const char *s, int n)
{
	int v = 0;

	for (int i = 0; i < n; i++)
	{
		if ((s[i] < '0') || (s[i] > '9'))
		{
			return -1;
		}
		v = ((v * 10) + (s[i] - '0'));
	}
	return v;
}

// Finds a position anywhere in the payload rather than at a fixed offset.
//
// Working from the type character alone only handles the plain position
// reports. Object reports carry a name and a timestamp first, items differ
// again, and some stations wrap the position in ways of their own - all of
// which were being shown as raw text with the coordinates buried in them.
// Scanning for the shape of the thing instead - four digits, a point, two
// digits, N or S, then the same again nine characters later for longitude -
// picks the position out of any of them.
// The compressed form of a position report, which is what most trackers send.
//
// Instead of readable digits it packs the coordinates into four base-91
// characters each, so the payload looks like line noise - "!/5L!!<*e7>7P[" is a
// perfectly good position report. Without decoding it there is nothing to show
// but that jumble, which is what made some received packets look corrupted.
//
// Layout after the data type character: symbol table, four characters of
// latitude, four of longitude, symbol code, two of course/speed, one of
// compression type.
static bool aprsFindCompressedPosition(const char *info, int *latDeg, int *latFrac, char *latHemi,
		int *lonDeg, int *lonFrac, char *lonHemi, char *symCode, int *commentAt)
{
	int len = (int) strlen(info);
	int p;
	char t = info[0];

	if ((t == '!') || (t == '='))
	{
		p = 1;
	}
	else if ((t == '/') || (t == '@'))
	{
		p = 8; // a timestamp comes first
	}
	else
	{
		return false;
	}

	if (len < (p + 13))
	{
		return false;
	}

	// The symbol table is '/', '\', or an overlay character.
	{
		char tab = info[p];

		if ((tab != '/') && (tab != '\\') && (((tab < 'A') || (tab > 'Z')) && ((tab < '0') || (tab > '9'))))
		{
			return false;
		}
	}

	// Eight characters of base-91, which is printable 33 to 124.
	for (int i = 1; i <= 8; i++)
	{
		char c = info[p + i];

		if ((c < 33) || (c > 124))
		{
			return false;
		}
	}

	{
		int32_t y = 0;
		int32_t x = 0;
		double lat, lon;

		for (int i = 0; i < 4; i++)
		{
			y = ((y * 91) + (info[p + 1 + i] - 33));
			x = ((x * 91) + (info[p + 5 + i] - 33));
		}

		lat = (90.0 - ((double) y / 380926.0));
		lon = (-180.0 + ((double) x / 190463.0));

		if ((lat < -90.0) || (lat > 90.0) || (lon < -180.0) || (lon > 180.0))
		{
			return false;
		}

		*latHemi = ((lat < 0.0) ? 'S' : 'N');
		*lonHemi = ((lon < 0.0) ? 'W' : 'E');
		if (lat < 0.0)
		{
			lat = -lat;
		}
		if (lon < 0.0)
		{
			lon = -lon;
		}

		*latDeg = (int) lat;
		*latFrac = (int)((lat - (double) *latDeg) * 10000.0);
		*lonDeg = (int) lon;
		*lonFrac = (int)((lon - (double) *lonDeg) * 10000.0);
	}

	*symCode = info[p + 9];
	*commentAt = (p + 13);
	return true;
}

static bool aprsFindPosition(const char *info, int *latDeg, int *latFrac, char *latHemi,
		int *lonDeg, int *lonFrac, char *lonHemi, char *symCode, int *commentAt)
{
	int len = (int) strlen(info);

	if (aprsFindCompressedPosition(info, latDeg, latFrac, latHemi, lonDeg, lonFrac, lonHemi,
			symCode, commentAt))
	{
		return true;
	}

	for (int p = 0; p <= (len - 19); p++)
	{
		int dd, mm, hh, dd2, mm2, hh2;

		if ((info[p + 4] != '.') || ((info[p + 7] != 'N') && (info[p + 7] != 'S')))
		{
			continue;
		}
		if ((info[p + 14] != '.') || ((info[p + 17] != 'E') && (info[p + 17] != 'W')))
		{
			continue;
		}

		dd = aprsDigits(info + p, 2);
		mm = aprsDigits(info + p + 2, 2);
		hh = aprsDigits(info + p + 5, 2);
		dd2 = aprsDigits(info + p + 9, 3);
		mm2 = aprsDigits(info + p + 12, 2);
		hh2 = aprsDigits(info + p + 15, 2);

		if ((dd < 0) || (mm < 0) || (hh < 0) || (dd2 < 0) || (mm2 < 0) || (hh2 < 0))
		{
			continue;
		}
		if ((dd > 90) || (mm > 59) || (dd2 > 180) || (mm2 > 59))
		{
			continue;
		}

		*latDeg = dd;
		*latFrac = aprsMinutesToFrac(mm, hh);
		*latHemi = info[p + 7];
		*lonDeg = dd2;
		*lonFrac = aprsMinutesToFrac(mm2, hh2);
		*lonHemi = info[p + 17];
		*symCode = info[p + 18];
		*commentAt = (p + 19);
		return true;
	}

	return false;
}

// Distance and direction to a station heard. From where the radio is: a live GPS
// fix, otherwise the last position it kept (the GPS updates it, and it can be
// set in the settings). Nothing is shown when the radio has never had one.
static bool aprsLogOwnPosition(double *lat, double *lon)
{
#if defined(HAS_GPS)
	if ((nonVolatileSettings.gps > GPS_MODE_OFF) && ((gpsData.Status & GPS_STATUS_HAS_FIX) == GPS_STATUS_HAS_FIX))
	{
		*lat = gpsData.LatitudeHiRes;
		*lon = gpsData.LongitudeHiRes;
		return true;
	}
#endif

	if (nonVolatileSettings.locationLat != SETTINGS_UNITIALISED_LOCATION_LAT)
	{
		*lat = latLongFixed32ToDouble(nonVolatileSettings.locationLat);
		*lon = latLongFixed32ToDouble(nonVolatileSettings.locationLon);
		return true;
	}

	return false;
}

#define APRS_TR(tr, en, es, ru) LANG4((tr), (en), (es), (ru)) // Turkish, English, Spanish, Russian

// What can be said about where a station is.
typedef enum
{
	APRS_POS_NONE = 0, // the packet carries no position
	APRS_POS_ZERO,     // it does, but 0,0 - a beacon sent with no GPS fix
	APRS_POS_NO_OWN,   // it does, but this radio has never had a position to measure from
	APRS_POS_OK
} aprsPosState_t;

typedef struct
{
	aprsPosState_t state;
	char   symbol;     // APRS symbol code, 0 when there is no position
	int    commentAt;  // where the comment starts in the info field
	int    latDeg, latFrac, lonDeg, lonFrac; // fractions in 1/10000ths of a degree
	char   latHemi, lonHemi;
	double km;
	int    degrees;    // bearing from here to the station, 0 = north, clockwise
} aprsStation_t;

// Position, distance and bearing of one packet. Great-circle distance and the
// initial bearing, from this radio's own position.
static void aprsLogLocate(const char *info, aprsStation_t *s)
{
	const double toRad = 0.017453292519943295;
	double ownLat;
	double ownLon;

	memset(s, 0, sizeof(*s));

	if (aprsFindPosition(info, &s->latDeg, &s->latFrac, &s->latHemi, &s->lonDeg, &s->lonFrac, &s->lonHemi,
			&s->symbol, &s->commentAt) == false)
	{
		s->state = APRS_POS_NONE;
		s->symbol = 0;
		s->commentAt = 0;
		return;
	}

	if ((s->latDeg == 0) && (s->latFrac == 0) && (s->lonDeg == 0) && (s->lonFrac == 0))
	{
		s->state = APRS_POS_ZERO;
		return;
	}

	if (aprsLogOwnPosition(&ownLat, &ownLon) == false)
	{
		s->state = APRS_POS_NO_OWN;
		return;
	}

	double lat = ((double) s->latDeg + ((double) s->latFrac / 10000.0));
	double lon = ((double) s->lonDeg + ((double) s->lonFrac / 10000.0));
	double p1 = (ownLat * toRad);
	double p2 = (((s->latHemi == 'S') ? -lat : lat) * toRad);
	double dl = ((((s->lonHemi == 'W') ? -lon : lon) - ownLon) * toRad);
	double a = ((sin((p2 - p1) / 2.0) * sin((p2 - p1) / 2.0)) + (cos(p1) * cos(p2) * sin(dl / 2.0) * sin(dl / 2.0)));
	double bearing = (atan2((sin(dl) * cos(p2)), ((cos(p1) * sin(p2)) - (sin(p1) * cos(p2) * cos(dl)))) / toRad);

	if (bearing < 0.0)
	{
		bearing += 360.0;
	}

	s->km = (2.0 * 6371.0 * atan2(sqrt(a), sqrt(1.0 - a)));
	s->degrees = (((int) (bearing + 0.5)) % 360);
	s->state = APRS_POS_OK;
}

// "12.3 km" under ten kilometres, "123 km" above. Integer formatting only -
// newlib-nano's printf has no floating point. compact drops the space.
static void aprsLogDistanceText(char *out, size_t len, double km, bool compact)
{
	if (km < 10.0)
	{
		int tenths = (int) ((km * 10.0) + 0.5);

		snprintf(out, len, "%d.%d%skm", (tenths / 10), (tenths % 10), (compact ? "" : " "));
	}
	else
	{
		snprintf(out, len, "%d%skm", (int) (km + 0.5), (compact ? "" : " "));
	}
}

static const char *aprsLogCompassPoint(int degrees)
{
	static const char * const pointsTR[8] = { "K", "KD", "D", "GD", "G", "GB", "B", "KB" };
	static const char * const pointsEN[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
	static const char * const pointsES[8] = { "N", "NE", "E", "SE", "S", "SO", "O", "NO" };
	static const char * const pointsRU[8] = { "\xD1", "\xD1\xC2", "\xC2", "\xDE\xC2", "\xDE", "\xDE\xC7", "\xC7", "\xD1\xC7" };

	return APRS_TR(pointsTR, pointsEN, pointsES, pointsRU)[((degrees + 22) / 45) % 8];
}

// ---------------------------------------------------------------------------
// Symbol pictures, 12x12. The common APRS symbols each get a little drawing;
// anything else gets a diamond. A '#' is a lit pixel.
// ---------------------------------------------------------------------------
typedef struct
{
	const char *codes;      // the symbol codes that use this picture
	const char  rows[12][13];
} aprsIcon_t;

static const aprsIcon_t aprsIcons[] =
{
	{ ">jaPf", { // car - and the jeep, ambulance, police and fire engine with it
		"............",
		"............",
		"...######...",
		"..#...#..#..",
		".#....#...#.",
		"############",
		"############",
		"############",
		".###....###.",
		".###....###.",
		"............",
		"............" } },
	{ "<", { // motorcycle
		"........##..",
		".........#..",
		"...#####.#..",
		"..#######...",
		"..#.###.#...",
		".###...###..",
		"#...#.#...#.",
		"#...#.#...#.",
		"#...#.#...#.",
		".###...###..",
		"............",
		"............" } },
	{ "-yH", { // house
		".....##.....",
		"....####....",
		"...######...",
		"..########..",
		".##########.",
		"..#......#..",
		"..#.##...#..",
		"..#.##.###..",
		"..#.##.###..",
		"..#.##...#..",
		"..########..",
		"............" } },
	{ "[", { // person
		".....##.....",
		".....##.....",
		"............",
		"....####....",
		"...#.##.#...",
		"..#..##..#..",
		".....##.....",
		".....##.....",
		"....#..#....",
		"....#..#....",
		"...#....#...",
		"...#....#..." } },
	{ "b", { // bicycle
		"......##....",
		".......#....",
		"....####....",
		".....#.#....",
		"....#...#...",
		".##.#..###..",
		"#..##..#..#.",
		"#..#....#.#.",
		"#..#...#..#.",
		".##.....##..",
		"............",
		"............" } },
	{ "kuvU", { // truck, van, bus
		"#######.....",
		"#.....#.###.",
		"#.....#.#..#",
		"#.....#.#..#",
		"#.....#.####",
		"############",
		"############",
		".##.....##..",
		".##.....##..",
		"............",
		"............",
		"............" } },
	{ "#rI&", { // digipeater, repeater, gateway: a mast
		"#....#....#.",
		".#...#...#..",
		"..#..#..#...",
		"...#.#.#....",
		".....#......",
		"....#.#.....",
		"....#.#.....",
		"...#.#.#....",
		"...#...#....",
		"..#.#.#.#...",
		"..#.....#...",
		".#########.." } },
	{ "_W", { // weather station: a cloud
		"............",
		"....###.....",
		"..##...##...",
		".#.......##.",
		"#..........#",
		"#..........#",
		".##########.",
		"............",
		"..#..#..#...",
		".#..#..#....",
		"#..#..#.....",
		"............" } },
	{ "sYC", { // boat, yacht, ship
		".....#......",
		".....##.....",
		".....###....",
		".....####...",
		".....#####..",
		".....#......",
		"############",
		".##########.",
		"..########..",
		"............",
		"............",
		"............" } },
	{ "^'g", { // aircraft
		".....##.....",
		".....##.....",
		"....####....",
		"..########..",
		"############",
		".....##.....",
		".....##.....",
		".....##.....",
		"....####....",
		"...######...",
		"............",
		"............" } },
	{ "O", { // balloon
		"....####....",
		"...######...",
		"..########..",
		"..########..",
		"..########..",
		"...######...",
		"....####....",
		".....##.....",
		".....##.....",
		"....####....",
		"....####....",
		"............" } },
};

static const char aprsIconOther[12][13] = // any other symbol: a diamond
{
	".....##.....",
	"....####....",
	"...######...",
	"..########..",
	".##########.",
	"############",
	"############",
	".##########.",
	"..########..",
	"...######...",
	"....####....",
	".....##....."
};

// Draws a symbol's picture with its top-left at x,y, at 1x or 2x. In the colour
// set by the current theme: the foreground normally, the background when
// inverted - on a highlighted row, where the text is drawn the same way.
static void aprsDrawIcon(int16_t x, int16_t y, char symbol, int scale, bool inverted)
{
	const char (*rows)[13] = aprsIconOther;

	for (unsigned int i = 0; (i < (sizeof(aprsIcons) / sizeof(aprsIcons[0]))) && (symbol != 0); i++)
	{
		if (strchr(aprsIcons[i].codes, symbol) != NULL)
		{
			rows = aprsIcons[i].rows;
			break;
		}
	}

	for (int r = 0; r < 12; r++)
	{
		for (int c = 0; c < 12; c++)
		{
			if (rows[r][c] == '#')
			{
				if (scale == 1)
				{
					displaySetPixel((x + c), (y + r), (inverted == false));
				}
				else
				{
					displayFillRect((x + (c * scale)), (y + (r * scale)), scale, scale, inverted); // false = foreground
				}
			}
		}
	}
}

// An arrow from cx,cy pointing along a bearing (0 = up, clockwise), len long.
static void aprsDrawArrow(int16_t cx, int16_t cy, int len, int degrees, bool inverted)
{
	float rad = ((float) degrees * 0.017453293f);
	float ux = sinf(rad);
	float uy = -cosf(rad);
	float px = -uy; // perpendicular
	float py = ux;
	float wing = ((float) len * 0.45f);
	float back = ((float) len * 0.15f);
	int16_t tx = (int16_t) (cx + (ux * len));
	int16_t ty = (int16_t) (cy + (uy * len));

	// Head: a triangle from the tip to two wings a little behind the centre.
	displayFillTriangle(tx, ty,
			(int16_t) (cx - (ux * back) + (px * wing)), (int16_t) (cy - (uy * back) + (py * wing)),
			(int16_t) (cx - (ux * back) - (px * wing)), (int16_t) (cy - (uy * back) - (py * wing)), (inverted == false));

	// Tail, two pixels wide, so the arrow reads even when small.
	for (int w = 0; w < 2; w++)
	{
		displayDrawLine((int16_t) (cx + (px * w)), (int16_t) (cy + (py * w)),
				(int16_t) (cx - (ux * len) + (px * w)), (int16_t) (cy - (uy * len) + (py * w)), (inverted == false));
	}
}

static int aprsLogIndex = 0;      // row in the list, newest first
static bool aprsCardOpen = false; // showing one station in full, rather than the list
// The station being looked at, so the view stays on it while the list reorders
// underneath - a station heard again moves to the top, a new one pushes the rest
// down. Empty when the row number itself is what was just chosen.
static char aprsSelCall[APRS_CALL_LEN + 1] = "";
static bool aprsSelForMe = false;
// Once the benchmark has been run its scores take over the bottom two lines, so
// the repeatable figures are what is on screen rather than the on-air counters
// they exist to replace. Cleared on entry, so it never shows a stale result.
static bool aprsBenchRun = false;
static uint32_t aprsLogLastDraw = 0;

// Benchmark scores: one row per tone tilt, one column per noise level, cleanest
// first. Drawn from both the empty and the populated views - the test is most
// likely to be run precisely when nothing has decoded, and that view returns
// early, so putting this only in the populated one left the screen unchanged
// apart from the acknowledging beep.
// One digit per noise level, cleanest on the left, so a row reads as the curve
// of how the decoder gives way. Nine packets per level keeps every cell to a
// single digit. The trailing figure is the row total, which is the one number
// to compare between two builds.
static void aprsLogDrawBenchRow(int tilt, const char *label, int y)
{
	char buf[APRS_LINE_BUF];
	int levels = aprsDecodeGetBenchLevels();
	int pos = 0;
	int total = 0;

	pos += snprintf(buf, sizeof(buf), "%s ", label);

	for (int l = 0; ((l < levels) && (pos < (int)(sizeof(buf) - 8))); l++)
	{
		int s = aprsDecodeGetBenchScore(tilt, l);
		int n = aprsDecodeGetBenchTotal();

		total += s;
		// Scaled to one digit so the row still reads as a curve now that a cell
		// holds more than nine packets. The figure after it is the true total,
		// and that is the one to compare.
		pos += snprintf((buf + pos), (sizeof(buf) - (size_t)pos), "%d",
				((n > 0) ? ((s * 9) / n) : 0));
	}

	snprintf((buf + pos), (sizeof(buf) - (size_t)pos), " =%d", total);
	displayPrintAt(2, y, buf, FONT_SIZE_1);
}

static void aprsLogDrawBenchScores(void)
{
	char buf[APRS_LINE_BUF];

	// Which sampling phase carried the test's packets. Ties go to the first, so
	// the leading figure is inflated; what matters is the total held by the rest,
	// which is what the original phase was missing.
	snprintf(buf, sizeof(buf), "F%d/%d/%d/%d S%d/%d",
			aprsDecodeGetBenchGainWin(0),
			aprsDecodeGetBenchGainWin(1),
			aprsDecodeGetBenchGainWin(2),
			aprsDecodeGetBenchGainWin(3),
			aprsDecodeGetBenchFilterWin(0),
			aprsDecodeGetBenchFilterWin(1));
	displayPrintAt(2, 101, buf, FONT_SIZE_1);

	// One row: the signal as this radio actually delivers it, space tone 5dB down.
	// See BT_TILTS for why the flat row went and this one stayed.
	aprsLogDrawBenchRow(0, LANG4("E\x98""ik", "Thr.", "Umbral", "\xCF\xEE\xF0\xEE\xE3"), 110);
	displayPrintAt(2, 119, LANG4("SK2 tekrar", "SK2 again", "SK2 repetir", "SK2 \xEF\xEE\xE2\xF2\xEE\xF0"), FONT_SIZE_1);
}

// How long ago a packet was heard, short enough to share a line with the symbol.
static void aprsLogAgeText(char *out, size_t len, uint16_t minutes)
{
	if (minutes == 0)
	{
		snprintf(out, len, "%s", APRS_TR("\x96imdi", "now", "ahora", "\xF1\xE5\xE9\xF7\xE0\xF1"));
	}
	else if (minutes < 60)
	{
		snprintf(out, len, APRS_TR("%u dk", "%u min", "%u min", "%u \xEC\xE8\xED"), (unsigned) minutes);
	}
	else if (minutes < 1440)
	{
		snprintf(out, len, APRS_TR("%u sa", "%u h", "%u h", "%u \xF7"), (unsigned) (minutes / 60));
	}
	else
	{
		snprintf(out, len, APRS_TR("%u g\xFCn", "%u d", "%u d", "%u \xE4"), (unsigned) (minutes / 1440));
	}
}

// The list: one row per station, newest first - its symbol, callsign, distance,
// and a small arrow pointing its way. Drawn from the top like the other lists
// here; there are only ever a few, so it rarely has to scroll.
static void aprsLogDrawList(int count)
{
	char buf[APRS_LINE_BUF];
	char dist[12];
	aprsStation_t st;
	const int rows = ((MENU_END_ITERATION_VALUE - MENU_START_ITERATION_VALUE) + 1);
	int top = ((aprsLogIndex < rows) ? 0 : ((aprsLogIndex - rows) + 1));

	snprintf(buf, sizeof(buf), APRS_TR("APRS  %d istasyon", "APRS  %d stations", "APRS  %d estaciones", "APRS  %d \xF1\xF2\xE0\xED\xF6\xE8\xE9"), count);
	menuDisplayTitle(buf);

	menuDataGlobal.currentItemIndex = aprsLogIndex; // what menuDisplayEntry highlights

	for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
	{
		int row = (top + (i - MENU_START_ITERATION_VALUE));
		int entry;
		bool focused;
		int16_t y;

		if (row >= count)
		{
			break;
		}

		entry = row; // aprsDecodeGet*() count from the newest already
		focused = (row == aprsLogIndex);
		y = (DISPLAY_Y_POS_MENU_ENTRY_HIGHLIGHT + (i * MENU_ENTRY_HEIGHT));

		aprsLogLocate(aprsDecodeGetInfo(entry), &st);

		if (st.state == APRS_POS_OK)
		{
			aprsLogDistanceText(dist, sizeof(dist), st.km, true);
		}
		else if (aprsDecodeIsMessageForMe(entry))
		{
			snprintf(dist, sizeof(dist), "%s", APRS_TR("Mesaj", "Msg", "Msj", "\xD1\xEE\xEE\xE1"));
		}
		else
		{
			snprintf(dist, sizeof(dist), "--");
		}

		// Two spaces leave room for the picture, drawn over them afterwards.
		snprintf(buf, sizeof(buf), "  %-9.9s %s", aprsDecodeGetFrom(entry), dist);
		menuDisplayEntry(i, row, buf, 12, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);

		displayThemeApply(THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_BG);
		aprsDrawIcon(DISPLAY_X_POS_MENU_TEXT_OFFSET, (y + 2), st.symbol, 1, focused);
		if (st.state == APRS_POS_OK)
		{
			aprsDrawArrow((DISPLAY_SIZE_X - 7), (y + 8), 5, st.degrees, focused);
		}
		displayThemeResetToDefault();
	}
}

// One station in full: a large picture and the callsign, what it is and how
// long ago it was heard; then how far and which way, with a compass whose
// arrow points at it (north up), the coordinates, and the comment.
static void aprsLogDrawCard(int count)
{
	char buf[APRS_LINE_BUF];
	char part[16];
	aprsStation_t st;
	int entry = aprsLogIndex;
	const char *info = aprsDecodeGetInfo(entry);
	const char *via = aprsDecodeGetVia(entry);
	const char *text;
	int y;

	// The title says how it got here: through which digipeater, or direct.
	if (via[0] != 0)
	{
		snprintf(buf, sizeof(buf), APRS_TR("%d/%d Digi %s", "%d/%d via %s", "%d/%d v\xED""a %s", "%d/%d \xF7\xE5\xF0\xE5\xE7 %s"), (aprsLogIndex + 1), count, via);
	}
	else
	{
		snprintf(buf, sizeof(buf), APRS_TR("%d/%d Direkt", "%d/%d Direct", "%d/%d Directo", "%d/%d \xCF\xF0\xFF\xEC\xEE"), (aprsLogIndex + 1), count);
	}
	menuDisplayTitle(buf);

	aprsLogLocate(info, &st);

	displayThemeApply(THEME_ITEM_FG_CHANNEL_NAME, THEME_ITEM_BG);
	aprsDrawIcon(4, 18, st.symbol, 2, false);
	displayPrintAt(32, 17, aprsDecodeGetFrom(entry), FONT_SIZE_3);
	displayThemeResetToDefault();

	{
		const char *name = ((st.symbol != 0) ? aprsSymbolName(st.symbol) : NULL);

		aprsLogAgeText(part, sizeof(part), aprsDecodeGetAgeMinutes(entry));
		if (name != NULL)
		{
			snprintf(buf, sizeof(buf), "%s  %s", name, part);
		}
		else
		{
			snprintf(buf, sizeof(buf), "%s", part);
		}
		displayPrintAt(32, 34, buf, FONT_SIZE_1);
	}

	text = (info + st.commentAt);
	y = 46;

	if (aprsDecodeIsMessageForMe(entry))
	{
		displayThemeApply(THEME_ITEM_FG_RSSI_BAR_S9P, THEME_ITEM_BG);
		displayPrintAt(4, y, APRS_TR(">> SANA MESAJ", ">> FOR YOU", ">> PARA TI", ">> \xC2\xC0\xCC"), FONT_SIZE_3);
		displayThemeResetToDefault();
		text = aprsDecodeGetMessageText(entry);
		y = 64;
	}
	else if (st.state == APRS_POS_OK)
	{
		aprsLogDistanceText(part, sizeof(part), st.km, false);
		displayThemeApply(THEME_ITEM_FG_CHANNEL_CONTACT_INFO, THEME_ITEM_BG);
		displayPrintAt(4, 46, part, FONT_SIZE_3);
		snprintf(buf, sizeof(buf), "%s %d", aprsLogCompassPoint(st.degrees), st.degrees);
		displayPrintAt(4, 64, buf, FONT_SIZE_3);

		// The compass: north at the top, the arrow pointing at the station.
		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		displayDrawCircle(134, 62, 21, true);
		displayPrintAt(132, 43, APRS_TR("K", "N", "N", "\xD1"), FONT_SIZE_1);
		displayThemeApply(THEME_ITEM_FG_CHANNEL_CONTACT_INFO, THEME_ITEM_BG);
		aprsDrawArrow(134, 62, 15, st.degrees, false);
		displayThemeResetToDefault();
		y = 84;
	}
	else if (st.state == APRS_POS_ZERO)
	{
		displayPrintAt(4, 46, APRS_TR("Konum yok", "No position", "Sin posici\xF3n", "\xCD\xE5\xF2 \xEF\xEE\xE7\xE8\xF6\xE8\xE8"), FONT_SIZE_3);
		displayPrintAt(4, 64, APRS_TR("G\xF6nderenin GPS'i yoktu", "The sender had no GPS fix", "El emisor no ten\xED""a GPS", "\xD3 \xEE\xF2\xEF\xF0\xE0\xE2\xE8\xF2\xE5\xEB\xFF \xED\xE5\xF2 GPS"), FONT_SIZE_1);
		y = 76;
	}
	else if (st.state == APRS_POS_NONE)
	{
		if (info[0] == ':')
		{
			char to[10];

			snprintf(to, sizeof(to), "%.9s", (info + 1));
			snprintf(buf, sizeof(buf), APRS_TR("Mesaj -> %s", "Message -> %s", "Mensaje -> %s", "\xD1\xEE\xEE\xE1\xF9. -> %s"), to);
			text = (info + (((int) strlen(info) > 11) ? 11 : (int) strlen(info)));
		}
		else if (info[0] == '>')
		{
			snprintf(buf, sizeof(buf), "%s", APRS_TR("Durum", "Status", "Estado", "\xD1\xF2\xE0\xF2\xF3\xF1"));
			text = (info + 1);
		}
		else
		{
			buf[0] = 0; // something else: shown whole below
		}

		if (buf[0] != 0)
		{
			displayThemeApply(THEME_ITEM_FG_CHANNEL_CONTACT_INFO, THEME_ITEM_BG);
			displayPrintAt(4, 46, buf, FONT_SIZE_1);
			displayThemeResetToDefault();
			y = 58;
		}
	}

	// Coordinates, wherever there is a real position to give.
	if (((st.state == APRS_POS_OK) || (st.state == APRS_POS_NO_OWN)) && (aprsDecodeIsMessageForMe(entry) == false))
	{
		snprintf(buf, sizeof(buf), "%d.%04d%c %d.%04d%c",
				st.latDeg, st.latFrac, ((st.latHemi == 'S') ? APRS_TR('G', 'S', 'S', '\xDE') : APRS_TR('K', 'N', 'N', '\xD1')),
				st.lonDeg, st.lonFrac, ((st.lonHemi == 'W') ? APRS_TR('B', 'W', 'O', '\xC7') : APRS_TR('D', 'E', 'E', '\xC2')));
		displayPrintAt(4, ((st.state == APRS_POS_OK) ? 84 : 46), buf, FONT_SIZE_1);

		if (st.state == APRS_POS_NO_OWN)
		{
			displayPrintAt(4, 58, APRS_TR("Senin konumun yok", "Your position is unknown", "No s\xE9 tu posici\xF3n", "\xC2\xE0\xF8\xE0 \xEF\xEE\xE7\xE8\xF6\xE8\xFF \xED\xE5\xE8\xE7\xE2\xE5\xF1\xF2\xED\xE0"), FONT_SIZE_1);
			y = 70;
		}
		else
		{
			y = 94;
		}
	}

	// The comment or message text, wrapped, in what room is left.
	{
		int len = (int) strlen(text);
		int pos = 0;
		int yEnd = (aprsBenchRun ? 100 : 117);

		while ((pos < len) && ((y + 8) <= yEnd))
		{
			snprintf(buf, sizeof(buf), "%.*s", APRS_LINE_CHARS, (text + pos));
			displayPrintAt(4, y, buf, FONT_SIZE_1);
			pos += APRS_LINE_CHARS;
			y += 9;
		}
	}

	if (aprsBenchRun)
	{
		aprsLogDrawBenchScores();
	}
	else
	{
		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		displayPrintAt(2, 119, APRS_TR("*:mesaj #:sil K\x93rm\x93z\x93:geri", "*:msg  #:clear  Red:back", "*:msj #:borrar Rojo:atr\xE1s", "*:\xF1\xEE\xEE\xE1 #:\xF1\xF2\xE5\xF0 \xCA\xF0\xE0\xF1:\xED\xE0\xE7\xE0\xE4"), FONT_SIZE_1);
		displayThemeResetToDefault();
	}
}

static void aprsLogShowScreen(void)
{
	char buf[APRS_LINE_BUF];
	int count = aprsDecodeGetCount();

	displayClearBuf();

	if (count == 0)
	{
		aprsCardOpen = false;
		aprsSelCall[0] = 0;
		menuDisplayTitle("APRS");
		displayPrintCentered(50, APRS_TR("Paket yok", "No packets", "Sin paquetes", "\xCD\xE5\xF2 \xEF\xE0\xEA\xE5\xF2\xEE\xE2"), FONT_SIZE_3);
		snprintf(buf, sizeof(buf), APRS_TR("Sinyal %%%d", "Signal %%%d", "Se\xF1""al %%%d", "\xD1\xE8\xE3\xED\xE0\xEB %%%d"), aprsDecodeGetTonePeakPct());
		displayPrintCentered(74, buf, FONT_SIZE_1);

		if (aprsBenchRun)
		{
			aprsLogDrawBenchScores();
		}
		else
		{
			displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
			displayPrintAt(2, 119, APRS_TR("*:mesaj    K\x93rm\x93z\x93:geri", "*:message    Red:back", "*:mensaje   Rojo:atr\xE1s", "*:\xF1\xEE\xEE\xE1\xF9.   \xCA\xF0\xE0\xF1:\xED\xE0\xE7\xE0\xE4"), FONT_SIZE_1);
			displayThemeResetToDefault();
		}
		displayRender();
		return;
	}

	// Follow the station being looked at to wherever it now is in the list.
	if (aprsSelCall[0] != 0)
	{
		for (int r = 0; r < count; r++)
		{
			if ((strcmp(aprsDecodeGetFrom(r), aprsSelCall) == 0) && (aprsDecodeIsMessageForMe(r) == aprsSelForMe))
			{
				aprsLogIndex = r;
				break;
			}
		}
	}

	if (aprsLogIndex >= count)
	{
		aprsLogIndex = (count - 1);
	}
	if (aprsLogIndex < 0)
	{
		aprsLogIndex = 0;
	}

	snprintf(aprsSelCall, sizeof(aprsSelCall), "%s", aprsDecodeGetFrom(aprsLogIndex));
	aprsSelForMe = aprsDecodeIsMessageForMe(aprsLogIndex);

	if (aprsCardOpen)
	{
		aprsLogDrawCard(count);
	}
	else
	{
		aprsLogDrawList(count);
	}

	displayRender();
}

// ---------------------------------------------------------------------------
// Logs (Menu > Kayitlar): the screen that was the APRS log is now the way into
// every log - APRS, the call log and the Morse log - so they are all in one
// place. Done inside this screen rather than as a menu of its own, because a
// menu entry costs main RAM and there is none left.
//
//  - the list of logs
//  - the APRS log, as it always was
//  - the call log: every 5-Tone and DTMF code received (callLog.c), newest
//    first, by name where a 5T contact has the code; green opens one call
//  - the Morse log is its own screen, opened from here
// ---------------------------------------------------------------------------
enum
{
	LOG_MODE_HUB = 0,
	LOG_MODE_APRS,
	LOG_MODE_CALLS,
	LOG_MODE_CALL_DETAIL,
	LOG_MODE_RECORDER,
	LOG_MODE_REC_FOLDER
};

enum
{
	LOG_ITEM_APRS = 0,
	LOG_ITEM_CALLS,
	LOG_ITEM_MORSE,
	LOG_ITEM_RECORDER,
	LOG_ITEM_COUNT
};

static uint8_t logMode = LOG_MODE_HUB;

#define LOG_TR(tr, en, es, ru) LANG4((tr), (en), (es), (ru)) // Turkish, English, Spanish, Russian

// A list drawn from the top, as in Radio Setup: first item on the first row,
// scrolling only once the cursor reaches the bottom.
static int logRowItem(int count, int i)
{
	const int rows = ((MENU_END_ITERATION_VALUE - MENU_START_ITERATION_VALUE) + 1);
	int sel = menuDataGlobal.currentItemIndex;
	int top = ((sel < rows) ? 0 : ((sel - rows) + 1));
	int item = (top + (i - MENU_START_ITERATION_VALUE));

	return ((item < count) ? item : MENU_OFFSET_AFTER_LAST_ENTRY);
}

// Local time of a call, as the clock on the channel screen shows it: the set
// timezone, or Turkey's UTC+3 when none is set. "--:--" if the clock was not
// set when the call came in.
static void logFormatTime(uint32_t utc, char *out, size_t len, bool withDate)
{
	struct tm t;
	time_t_custom local;

	if (utc < 1577836800U) // 2020-01-01
	{
		snprintf(out, len, "%s", (withDate ? "--.--.---- --:--" : "--:--"));
		return;
	}

	local = utc + ((nonVolatileSettings.timezone & 0x80) ? (((nonVolatileSettings.timezone & 0x7F) - 64) * (15 * 60)) : (3 * 60 * 60));
	gmtime_r_Custom(&local, &t);

	if (withDate)
	{
		snprintf(out, len, "%02u.%02u.%04u %02u:%02u", (unsigned) t.tm_mday, (unsigned) (t.tm_mon + 1), (unsigned) (t.tm_year + 1900),
				(unsigned) t.tm_hour, (unsigned) t.tm_min);
	}
	else
	{
		snprintf(out, len, "%02u:%02u", (unsigned) t.tm_hour, (unsigned) t.tm_min);
	}
}

static void logDrawHub(void)
{
	static const char * const labelsTR[LOG_ITEM_COUNT] = { "APRS Kayd\x93", "\xC7""a\x98r\x93 Kayd\x93", "Mors Kayd\x93", "Dinleme Kayd\x93" };
	static const char * const labelsEN[LOG_ITEM_COUNT] = { "APRS Log", "Call Log", "Morse Log", "RX Recorder" };
	static const char * const labelsES[LOG_ITEM_COUNT] = { "Registro APRS", "Llamadas", "Registro Morse", "Grabador RX" };
	static const char * const labelsRU[LOG_ITEM_COUNT] = { "\xC6\xF3\xF0\xED\xE0\xEB APRS", "\xC6\xF3\xF0\xED\xE0\xEB \xE2\xFB\xE7\xEE\xE2\xEE\xE2", "\xC6\xF3\xF0\xED\xE0\xEB CW", "\xC7\xE0\xEF\xE8\xF1\xFC \xEF\xF0\xE8\xB8\xEC\xE0" };

	displayClearBuf();
	menuDisplayTitle(LOG_TR("KAYITLAR", "LOGS", "REGISTROS", "\xC6\xD3\xD0\xCD\xC0\xCB\xDB"));

	for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
	{
		int mNum = logRowItem(LOG_ITEM_COUNT, i);

		if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		menuDisplayEntry(i, mNum, LOG_TR(labelsTR[mNum], labelsEN[mNum], labelsES[mNum], labelsRU[mNum]), -1, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
	}

	displayRender();
}

// The code as shown: the 5T contact's name if one has this code.
static const char *logCallShown(callLogEntry_t *entry, char *name)
{
	return (codeplugDTMFContactFindNameForCode(entry->code, name) ? name : entry->code);
}

static void logDrawCalls(void)
{
	uint8_t slots[CALL_LOG_ENTRIES];
	int count = callLogSortedSlots(slots);
	callLogEntry_t entry;
	char row[32];
	char time[8];
	char name[17];

	displayClearBuf();
	menuDisplayTitle(LOG_TR("\xC7""a\x98r\x93 Kayd\x93", "Call Log", "Llamadas", "\xC2\xFB\xE7\xEE\xE2\xFB"));

	if (count == 0)
	{
		displayPrintCentered(50, LOG_TR("Kay\x93t yok", "No calls", "Sin llamadas", "\xCD\xE5\xF2 \xE2\xFB\xE7\xEE\xE2\xEE\xE2"), FONT_SIZE_3);
	}

	for (int i = MENU_START_ITERATION_VALUE; (i <= MENU_END_ITERATION_VALUE) && (count > 0); i++)
	{
		int mNum = logRowItem(count, i);

		if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		if (callLogReadSlot(slots[mNum], &entry) == false)
		{
			continue;
		}

		// "14:32 5T Ahmet": the time in the item colour, the rest in the value colour.
		logFormatTime(entry.time, time, sizeof(time), false);
		snprintf(row, sizeof(row), "%s %s %.10s", time, ((entry.type == CALL_LOG_TYPE_5T) ? "5T" : "DT"), logCallShown(&entry, name));
		menuDisplayEntry(i, mNum, row, 6, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
	}

	displayRender();
}

static void logDrawCallDetail(void)
{
	uint8_t slots[CALL_LOG_ENTRIES];
	int count = callLogSortedSlots(slots);
	int n = menuDataGlobal.currentItemIndex;
	callLogEntry_t entry;
	char buf[32];
	char name[17];

	if ((n < 0) || (n >= count) || (callLogReadSlot(slots[n], &entry) == false))
	{
		logMode = LOG_MODE_CALLS;
		logDrawCalls();
		return;
	}

	displayClearBuf();
	snprintf(buf, sizeof(buf), "%s %d/%d", LOG_TR("\xC7""a\x98r\x93", "Call", "Llamada", "\xC2\xFB\xE7\xEE\xE2"), (n + 1), count);
	menuDisplayTitle(buf);

	snprintf(buf, sizeof(buf), "%s  %u.%04u MHz", ((entry.type == CALL_LOG_TYPE_5T) ? LOG_TR("5-Ton", "5-Tone", "5-Tonos", "5-\xD2\xEE\xED") : "DTMF"),
			(unsigned) (entry.rxFreq / 100000U), (unsigned) ((entry.rxFreq % 100000U) / 10U));
	displayPrintCentered(20, buf, FONT_SIZE_1);

	displayPrintCentered(34, entry.code, FONT_SIZE_3);
	displayPrintCentered(54, (codeplugDTMFContactFindNameForCode(entry.code, name) ? name : "-"), FONT_SIZE_3);

	logFormatTime(entry.time, buf, sizeof(buf), true);
	displayPrintCentered(76, buf, FONT_SIZE_3);

	displayPrintAt(4, 106, LOG_TR("Yukar\x93/A\x96""a\x98\x93: \xF6nceki/sonraki", "Up/Down: newer/older", "Arriba/Abajo: nueva/vieja", "\xC2\xE2\xE5\xF0\xF5/\xC2\xED\xE8\xE7: \xED\xEE\xE2./\xF1\xF2\xE0\xF0."), FONT_SIZE_1);
	displayPrintAt(4, 116, LOG_TR("K\x93rm\x93z\x93: Geri", "Red: back", "Rojo: atr\xE1s", "\xCA\xF0\xE0\xF1: \xED\xE0\xE7\xE0\xE4"), FONT_SIZE_1);
	displayRender();
}

// Receive recorder, filed in folders: one per channel - by its name, or by its
// frequency on the VFO, which has none. The top level is the on/off switch and
// the folders, the one most recently recorded into first; inside a folder are
// its recordings, newest first - when, how long, and who if a code was heard.
#define LOG_REC_MAX        96   // the recorder lists at most this many

enum
{
	LOG_REC_CONFIRM_NONE = 0,
	LOG_REC_CONFIRM_ALL,
	LOG_REC_CONFIRM_FOLDER
};

__attribute__((section(".ccmram"))) static char     logRecKeyName[LOG_REC_MAX][17];
__attribute__((section(".ccmram"))) static uint32_t logRecKeyFreq[LOG_REC_MAX];
__attribute__((section(".ccmram"))) static uint8_t  logRecFolderFirst[LOG_REC_MAX]; // its newest recording
__attribute__((section(".ccmram"))) static uint8_t  logRecFolderCount[LOG_REC_MAX];
__attribute__((section(".ccmram"))) static char     logRecOpenName[17];
static uint32_t logRecOpenFreq = 0;
static int      logRecEntries = 0;
static int      logRecFolders = 0;
static uint8_t  logRecConfirm = LOG_REC_CONFIRM_NONE;

static bool logRecSameKey(const char *nameA, uint32_t freqA, const char *nameB, uint32_t freqB)
{
	if (nameA[0] != 0)
	{
		return (strcmp(nameA, nameB) == 0);
	}

	return ((nameB[0] == 0) && (freqA == freqB));
}

// Files the recordings into folders - only the keys are compared, so this is
// cheap enough to do on every redraw.
static void logRecBuildFolders(void)
{
	voiceRecorderInfo_t info;

	logRecEntries = voiceRecorderCount();
	if (logRecEntries > LOG_REC_MAX)
	{
		logRecEntries = LOG_REC_MAX;
	}
	logRecFolders = 0;

	for (int i = 0; i < logRecEntries; i++)
	{
		int f;

		if (!voiceRecorderGet(i, &info))
		{
			info.channel[0] = 0;
			info.rxFreq = 0;
		}
		snprintf(logRecKeyName[i], sizeof(logRecKeyName[i]), "%s", info.channel);
		logRecKeyFreq[i] = info.rxFreq;

		for (f = 0; f < logRecFolders; f++)
		{
			int first = logRecFolderFirst[f];

			if (logRecSameKey(logRecKeyName[first], logRecKeyFreq[first], logRecKeyName[i], logRecKeyFreq[i]))
			{
				logRecFolderCount[f]++;
				break;
			}
		}

		if (f == logRecFolders)
		{
			logRecFolderFirst[logRecFolders] = (uint8_t) i;
			logRecFolderCount[logRecFolders] = 1;
			logRecFolders++;
		}
	}
}

static bool logRecInOpenFolder(int i)
{
	return logRecSameKey(logRecOpenName, logRecOpenFreq, logRecKeyName[i], logRecKeyFreq[i]);
}

// The n-th recording of the open folder, as an index into the whole list; -1 past its end.
static int logRecFolderEntry(int n)
{
	for (int i = 0; i < logRecEntries; i++)
	{
		if (logRecInOpenFolder(i) && (n-- == 0))
		{
			return i;
		}
	}

	return -1;
}

static int logRecOpenCount(void)
{
	int c = 0;

	for (int i = 0; i < logRecEntries; i++)
	{
		if (logRecInOpenFolder(i))
		{
			c++;
		}
	}

	return c;
}

static void logRecFolderLabel(const char *name, uint32_t freq, char *out, size_t len)
{
	if (name[0] != 0)
	{
		snprintf(out, len, "%s", name);
	}
	else
	{
		snprintf(out, len, "%u.%04u", (unsigned)(freq / 100000U), (unsigned)((freq % 100000U) / 10U));
	}
}

// The title: what it is doing if anything, otherwise where you are.
static void logRecTitle(const char *idle)
{
	char buf[32];

	if (voiceRecorderIsPlaying())
	{
		voiceRecorderInfo_t info;

		info.frames = 0;
		(void) voiceRecorderGet(voiceRecorderPlayingIndex(), &info);
		snprintf(buf, sizeof(buf), LOG_TR("\xC7""al\x93yor %u/%u sn", "Playing %u/%u s", "Sonando %u/%u s", "\xC8\xE3\xF0\xE0\xE5\xF2 %u/%u \xF1"),
				(unsigned)(voiceRecorderPlayedFrames() / 50U), (unsigned)(info.frames / 50U));
	}
	else if (voiceRecorderIsRecording())
	{
		snprintf(buf, sizeof(buf), "%s", LOG_TR("KAYDED\x94L\x94YOR...", "RECORDING...", "GRABANDO...", "\xC7\xC0\xCF\xC8\xD1\xDC..."));
	}
	else if (voiceRecorderDroppedFrames() > 0)
	{
		// Frames lost because the encoder fell behind, since power-on: should
		// stay 0; shown only when it is not.
		snprintf(buf, sizeof(buf), "%.12s K:%d", idle, voiceRecorderDroppedFrames());
	}
	else
	{
		snprintf(buf, sizeof(buf), "%s", idle);
	}

	menuDisplayTitle(buf);
}

static void logDrawRecorderConfirm(void)
{
	char label[24];

	menuDisplayTitle(LOG_TR("D\x94NLEME KAYDI", "RX RECORDER", "GRABADOR RX", "\xC7\xC0\xCF\xC8\xD1\xDC \xCF\xD0\xC8\xA8\xCC\xC0"));

	if (logRecConfirm == LOG_REC_CONFIRM_ALL)
	{
		displayPrintCentered(40, LOG_TR("T\xFCm kay\x93tlar", "Delete every", "Borrar todo", "\xD3\xE4\xE0\xEB\xE8\xF2\xFC \xE2\xF1\xE5"), FONT_SIZE_3);
	}
	else
	{
		logRecFolderLabel(logRecOpenName, logRecOpenFreq, label, sizeof(label));
		displayPrintCentered(40, label, FONT_SIZE_3);
	}
	displayPrintCentered(58, LOG_TR("silinsin mi?", "recording?", "lo grabado?", "\xE7\xE0\xEF\xE8\xF1\xE8?"), FONT_SIZE_3);
	displayPrintCentered(96, LOG_TR("Ye\x96il: evet  K\x93rm\x93z\x93: hay\x93r", "Green: yes  Red: no", "Verde: s\xED  Rojo: no", "\xC7\xE5\xEB: \xE4\xE0  \xCA\xF0\xE0\xF1: \xED\xE5\xF2"), FONT_SIZE_1);
}

// Top level: the switch, then the folders.
static void logDrawRecorder(void)
{
	voiceRecorderStatus_t status = voiceRecorderGetStatus();
	char buf[40];
	char label[24];
	char time[8];
	int total;

	logRecBuildFolders();
	total = (logRecFolders + 1);

	displayClearBuf();

	if (logRecConfirm != LOG_REC_CONFIRM_NONE)
	{
		logDrawRecorderConfirm();
		displayRender();
		return;
	}

	if (menuDataGlobal.currentItemIndex >= total)
	{
		menuDataGlobal.currentItemIndex = (total - 1); // the oldest went to make room
	}

	snprintf(buf, sizeof(buf), LOG_TR("D\x94NLEME KAYDI (%d)", "RX RECORDER (%d)", "GRABADOR RX (%d)", "\xC7\xC0\xCF\xC8\xD1\xDC \xCF\xD0\xC8\xA8\xCC\xC0 (%d)"), logRecEntries);
	logRecTitle(buf);

	for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
	{
		int mNum = logRowItem(total, i);

		if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		if (mNum == 0)
		{
			if (status == VOICE_RECORDER_STATUS_OK)
			{
				snprintf(buf, sizeof(buf), "%s: %s", LOG_TR("Kay\x93t", "Record", "Grabar", "\xC7\xE0\xEF\xE8\xF1\xFC"),
						(voiceRecorderGetEnabled() ? LOG_TR("A\xE7\x93k", "On", "S\xED", "\xC2\xEA\xEB") : LOG_TR("Kapal\x93", "Off", "No", "\xC2\xFB\xEA\xEB")));
			}
			else if (status == VOICE_RECORDER_STATUS_NO_FLASH)
			{
				snprintf(buf, sizeof(buf), "%s", LOG_TR("Flash uygun de\x98il", "Unsuitable flash", "Flash no apta", "Flash \xED\xE5 \xEF\xEE\xE4\xF5\xEE\xE4\xE8\xF2"));
			}
			else if (status == VOICE_RECORDER_STATUS_DMRID_OVERLAP)
			{
				snprintf(buf, sizeof(buf), "%s", LOG_TR("DMR ID DB b\xFCy\xFCk", "DMR ID DB too big", "BD DMR ID grande", "\xC1\xE0\xE7\xE0 DMR ID \xE2\xE5\xEB\xE8\xEA\xE0"));
			}
			else
			{
				snprintf(buf, sizeof(buf), "%s", LOG_TR("Haz\x93rlan\x93yor...", "Starting...", "Preparando...", "\xCF\xEE\xE4\xE3\xEE\xF2\xEE\xE2\xEA\xE0..."));
			}
			menuDisplayEntry(i, mNum, buf, -1, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
		}
		else
		{
			int first = logRecFolderFirst[mNum - 1];
			voiceRecorderInfo_t info;

			info.time = 0;
			(void) voiceRecorderGet(first, &info);
			logFormatTime(info.time, time, sizeof(time), false);
			logRecFolderLabel(logRecKeyName[first], logRecKeyFreq[first], label, sizeof(label));

			// "14:32 KATRAD (12)": the time of its newest recording in the item
			// colour, the folder and how many it holds in the value colour.
			snprintf(buf, sizeof(buf), "%s %s (%d)", time, label, logRecFolderCount[mNum - 1]);
			menuDisplayEntry(i, mNum, buf, 6, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
		}
	}

	displayRender();
}

// Inside a folder: its recordings.
static void logDrawRecorderFolder(void)
{
	char buf[40];
	char label[24];
	char time[8];
	char who[24];
	char name[17];
	int count;

	logRecBuildFolders();
	count = logRecOpenCount();

	displayClearBuf();

	if (logRecConfirm != LOG_REC_CONFIRM_NONE)
	{
		logDrawRecorderConfirm();
		displayRender();
		return;
	}

	if ((count > 0) && (menuDataGlobal.currentItemIndex >= count))
	{
		menuDataGlobal.currentItemIndex = (count - 1);
	}

	logRecFolderLabel(logRecOpenName, logRecOpenFreq, label, sizeof(label));
	snprintf(buf, sizeof(buf), "%.14s (%d)", label, count);
	logRecTitle(buf);

	if (count == 0)
	{
		displayPrintCentered(50, LOG_TR("Kay\x93t yok", "No recordings", "Sin grabaciones", "\xCD\xE5\xF2 \xE7\xE0\xEF\xE8\xF1\xE5\xE9"), FONT_SIZE_3);
	}

	for (int i = MENU_START_ITERATION_VALUE; (i <= MENU_END_ITERATION_VALUE) && (count > 0); i++)
	{
		int mNum = logRowItem(count, i);
		int entry;
		voiceRecorderInfo_t info;
		unsigned secs;

		if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		entry = logRecFolderEntry(mNum);
		if ((entry < 0) || !voiceRecorderGet(entry, &info))
		{
			continue;
		}

		logFormatTime(info.time, time, sizeof(time), false);
		secs = (unsigned)(info.frames / 50U);

		// Who was talking, if a five-tone or DTMF code was heard.
		who[0] = 0;
		if ((info.callerType != 0xFF) && (info.caller[0] != 0))
		{
			snprintf(who, sizeof(who), " %s %s", ((info.callerType == CALL_LOG_TYPE_5T) ? "5T" : "DT"),
					(codeplugDTMFContactFindNameForCode(info.caller, name) ? name : info.caller));
		}

		if (secs < 60)
		{
			snprintf(buf, sizeof(buf), "%s %us%s", time, secs, who);
		}
		else
		{
			snprintf(buf, sizeof(buf), "%s %um%02u%s", time, (secs / 60), (secs % 60), who);
		}
		menuDisplayEntry(i, mNum, buf, 6, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
	}

	displayRender();
}

static void logRecOpenFolderOf(int entry)
{
	snprintf(logRecOpenName, sizeof(logRecOpenName), "%s", logRecKeyName[entry]);
	logRecOpenFreq = logRecKeyFreq[entry];
}

// Back to the top level, on the row of the folder that was open.
static void logRecBackToFolders(void)
{
	int row = 0;

	logRecBuildFolders();
	for (int f = 0; f < logRecFolders; f++)
	{
		if (logRecInOpenFolder(logRecFolderFirst[f]))
		{
			row = (f + 1);
			break;
		}
	}

	logMode = LOG_MODE_RECORDER;
	menuDataGlobal.currentItemIndex = row;
	logDrawRecorder();
}

static void logRecDeleteOpenFolder(void)
{
	// From the oldest end, so the indices still to come do not move.
	for (int i = (logRecEntries - 1); i >= 0; i--)
	{
		if (logRecInOpenFolder(i))
		{
			(void) voiceRecorderDelete(i);
		}
	}
}

static void logRedraw(void)
{
	switch (logMode)
	{
		case LOG_MODE_APRS:
			aprsLogShowScreen();
			break;
		case LOG_MODE_CALLS:
			logDrawCalls();
			break;
		case LOG_MODE_CALL_DETAIL:
			logDrawCallDetail();
			break;
		case LOG_MODE_RECORDER:
			logDrawRecorder();
			break;
		case LOG_MODE_REC_FOLDER:
			logDrawRecorderFolder();
			break;
		default:
			logDrawHub();
			break;
	}
}

static void logBackToHub(int item)
{
	logMode = LOG_MODE_HUB;
	menuDataGlobal.currentItemIndex = item;
	logDrawHub();
}

// Keys for everything but the APRS log.
static void logHandleKeys(uiEvent_t *ev)
{
	bool down = (KEYCHECK_PRESS(ev->keys, KEY_DOWN) || KEYCHECK_PRESS(ev->keys, KEY_RIGHT));
	bool up = (KEYCHECK_PRESS(ev->keys, KEY_UP) || KEYCHECK_PRESS(ev->keys, KEY_LEFT));

	if (logMode == LOG_MODE_HUB)
	{
		if (down || up)
		{
			if (down)
			{
				menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, LOG_ITEM_COUNT);
			}
			else
			{
				menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, LOG_ITEM_COUNT);
			}
			logDrawHub();
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			switch (menuDataGlobal.currentItemIndex)
			{
				case LOG_ITEM_APRS:
					logMode = LOG_MODE_APRS;
					aprsLogIndex = 0;     // newest station
					aprsSelCall[0] = 0;
					aprsCardOpen = false; // the list first
					aprsBenchRun = false;
					aprsLogShowScreen();
					break;
				case LOG_ITEM_CALLS:
					logMode = LOG_MODE_CALLS;
					menuDataGlobal.currentItemIndex = 0;
					logDrawCalls();
					break;
				case LOG_ITEM_MORSE:
					menuSystemPushNewMenu(UI_MORSE_LOG); // back here, on this row, when it closes
					break;
				case LOG_ITEM_RECORDER:
					logMode = LOG_MODE_RECORDER;
					logRecConfirm = LOG_REC_CONFIRM_NONE;
					// The most recent folder ready under the cursor: Green opens it.
					menuDataGlobal.currentItemIndex = ((voiceRecorderCount() > 0) ? 1 : 0);
					logDrawRecorder();
					break;
			}
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			menuSystemPopPreviousMenu();
		}
	}
	else if (logMode == LOG_MODE_CALLS)
	{
		int count = callLogCount();

		if ((down || up) && (count > 0))
		{
			if (down)
			{
				menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, count);
			}
			else
			{
				menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, count);
			}
			logDrawCalls();
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN) && (count > 0))
		{
			logMode = LOG_MODE_CALL_DETAIL;
			logDrawCallDetail();
		}
		else if (KEYCHECK_LONGDOWN(ev->keys, KEY_HASH) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_HASH) == false))
		{
			// Held, not pressed: the call log is kept through power-off, so it
			// should not go on a slip of the thumb.
			callLogClear();
			menuDataGlobal.currentItemIndex = 0;
			soundSetMelody(MELODY_ACK_BEEP);
			logDrawCalls();
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			logBackToHub(LOG_ITEM_CALLS);
		}
	}
	else if (logMode == LOG_MODE_CALL_DETAIL)
	{
		int count = callLogCount();

		if (down && (menuDataGlobal.currentItemIndex < (count - 1)))
		{
			menuDataGlobal.currentItemIndex++;
			logDrawCallDetail();
		}
		else if (up && (menuDataGlobal.currentItemIndex > 0))
		{
			menuDataGlobal.currentItemIndex--;
			logDrawCallDetail();
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			logMode = LOG_MODE_CALLS;
			logDrawCalls();
		}
	}
	else if ((logMode == LOG_MODE_RECORDER) || (logMode == LOG_MODE_REC_FOLDER))
	{
		bool inFolder = (logMode == LOG_MODE_REC_FOLDER);
		int sel = menuDataGlobal.currentItemIndex;
		int total;

		logRecBuildFolders();
		total = (inFolder ? logRecOpenCount() : (logRecFolders + 1));

		if (logRecConfirm != LOG_REC_CONFIRM_NONE)
		{
			if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
			{
				if ((logRecConfirm == LOG_REC_CONFIRM_ALL) && voiceRecorderIsRecording())
				{
					soundSetMelody(MELODY_ERROR_BEEP); // not in the middle of a recording
				}
				else
				{
					if (logRecConfirm == LOG_REC_CONFIRM_ALL)
					{
						voiceRecorderDeleteAll();
					}
					else
					{
						logRecDeleteOpenFolder();
					}
					soundSetMelody(MELODY_ACK_BEEP);
				}
				logRecConfirm = LOG_REC_CONFIRM_NONE;
				logMode = LOG_MODE_RECORDER; // the folder may be gone
				menuDataGlobal.currentItemIndex = 0;
				logDrawRecorder();
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
			{
				logRecConfirm = LOG_REC_CONFIRM_NONE;
				logRedraw();
			}
			return;
		}

		if (down || up)
		{
			if (total > 0)
			{
				if (down)
				{
					menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, total);
				}
				else
				{
					menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, total);
				}
			}
			logRedraw();
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			if (!inFolder)
			{
				if (sel == 0)
				{
					if (voiceRecorderGetStatus() == VOICE_RECORDER_STATUS_OK)
					{
						voiceRecorderSetEnabled(!voiceRecorderGetEnabled());
					}
					else
					{
						soundSetMelody(MELODY_ERROR_BEEP);
					}
				}
				else if (sel <= logRecFolders)
				{
					// Into the folder, on its newest recording: Green again plays it.
					logRecOpenFolderOf(logRecFolderFirst[sel - 1]);
					logMode = LOG_MODE_REC_FOLDER;
					menuDataGlobal.currentItemIndex = 0;
				}
			}
			else
			{
				int entry = logRecFolderEntry(sel);

				if (entry >= 0)
				{
					if (voiceRecorderPlayingIndex() == entry)
					{
						voiceRecorderStop();
					}
					else
					{
						voiceRecorderStop();
						if (!voiceRecorderPlay(entry))
						{
							soundSetMelody(MELODY_ERROR_BEEP); // a station is on the air, or not on FM
						}
					}
				}
			}
			logRedraw();
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
		{
			if (!inFolder)
			{
				// The top row: every recording. A folder: everything in it. Both ask first.
				if (sel == 0)
				{
					if (logRecEntries > 0)
					{
						logRecConfirm = LOG_REC_CONFIRM_ALL;
					}
				}
				else if (sel <= logRecFolders)
				{
					logRecOpenFolderOf(logRecFolderFirst[sel - 1]);
					logRecConfirm = LOG_REC_CONFIRM_FOLDER;
				}
				logRedraw();
			}
			else
			{
				// One recording: at once.
				int entry = logRecFolderEntry(sel);

				if ((entry >= 0) && voiceRecorderDelete(entry))
				{
					soundSetMelody(MELODY_ACK_BEEP);
				}
				else
				{
					soundSetMelody(MELODY_ERROR_BEEP);
				}

				logRecBuildFolders();
				if (logRecOpenCount() == 0)
				{
					logRecBackToFolders(); // the folder is empty now
				}
				else
				{
					logRedraw();
				}
			}
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			voiceRecorderStop();
			if (inFolder)
			{
				logRecBackToFolders();
			}
			else
			{
				logBackToHub(LOG_ITEM_RECORDER);
			}
		}
	}
}

menuStatus_t menuAPRSLog(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		int returningFrom = menuSystemGetPreviouslyPushedMenuNumber(false);

		// Back from writing an APRS message, WhatsApp or e-mail: those are
		// reached from the APRS log, so that is where to land. Back from the lock
		// screen: stay where it was. Anything else - including the Morse log -
		// starts at the list of logs.
		if ((returningFrom == UI_APRS_SEND) || (returningFrom == UI_APRS_WHATSAPP) || (returningFrom == UI_APRS_EMAIL))
		{
			logMode = LOG_MODE_APRS;
		}
		else if ((returningFrom != UI_LOCK_SCREEN) || (logMode > LOG_MODE_REC_FOLDER))
		{
			logMode = LOG_MODE_HUB;
		}

		if ((logMode == LOG_MODE_HUB) &&
				((menuDataGlobal.currentItemIndex < 0) || (menuDataGlobal.currentItemIndex >= LOG_ITEM_COUNT)))
		{
			menuDataGlobal.currentItemIndex = 0;
		}

		if (logMode == LOG_MODE_APRS)
		{
			aprsBenchRun = false; // back from writing a message: same station, same view
		}
		aprsLogLastDraw = ticksGetMillis();
		logRedraw();
		return MENU_STATUS_SUCCESS;
	}

	if (logMode != LOG_MODE_APRS)
	{
		if (ev->hasEvent && (ev->events & KEY_EVENT))
		{
			logHandleKeys(ev);
		}
		else if (ev->hasEvent && (ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
		{
			logRedraw();
		}
		else if (((logMode == LOG_MODE_RECORDER) || (logMode == LOG_MODE_REC_FOLDER)) && ((uint32_t)(ev->time - aprsLogLastDraw) >= 500))
		{
			// Recording and playing change the screen on their own.
			aprsLogLastDraw = ev->time;
			logRedraw();
		}
		return MENU_STATUS_SUCCESS;
	}

	if (ev->hasEvent)
	{
		// Red: from a station back to the list, from the list back to the logs.
		if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			if (aprsCardOpen)
			{
				aprsCardOpen = false;
				aprsLogShowScreen();
			}
			else
			{
				logBackToHub(LOG_ITEM_APRS);
			}
			return MENU_STATUS_SUCCESS;
		}

		// Green opens the highlighted station.
		if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			if ((aprsCardOpen == false) && (aprsDecodeGetCount() > 0))
			{
				aprsCardOpen = true;
				aprsLogShowScreen();
			}
			return MENU_STATUS_SUCCESS;
		}

		if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
		{
			aprsDecodeClear();
			aprsLogIndex = 0;
			aprsSelCall[0] = 0;
			aprsCardOpen = false;
			soundSetMelody(MELODY_ACK_BEEP);
			aprsLogShowScreen();
			return MENU_STATUS_SUCCESS;
		}

		// SK2 runs the repeatable benchmark. A side button rather than a keypad
		// key, because the driver hands non-letter keys to the multi-tap logic
		// and swallows them on screens where text can be entered.
		if (BUTTONCHECK_SHORTUP(ev, BUTTON_SK2))
		{
			aprsBenchRun = true; // shown instead of the on-air counters
			aprsDecodeRunBenchmark();
			soundSetMelody(MELODY_ACK_BEEP);
			aprsLogShowScreen();
			return MENU_STATUS_SUCCESS;
		}

		// STAR opens the composer. If the highlighted packet was a message to
		// us, its sender goes in as the addressee so replying is one key.
		if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
		{
			int count = aprsDecodeGetCount();
			int entry = aprsLogIndex; // the list is newest first, as aprsDecodeGet*() are

			if ((count > 0) && aprsDecodeIsMessageForMe(entry))
			{
				uiAPRSMessagePresetTo(aprsDecodeGetFrom(entry));
			}
			menuSystemPushNewMenu(UI_APRS_SEND);
			return MENU_STATUS_SUCCESS;
		}

		// Arrows and the channel knob: through the list, or from one station to
		// the next when one is open.
		if (KEYCHECK_PRESS(ev->keys, KEY_DOWN) || KEYCHECK_PRESS(ev->keys, KEY_RIGHT))
		{
			if (aprsLogIndex < (aprsDecodeGetCount() - 1))
			{
				aprsLogIndex++;
			}
			aprsSelCall[0] = 0; // the row just chosen, not the station that was there
			aprsLogShowScreen();
			return MENU_STATUS_SUCCESS;
		}

		if (KEYCHECK_PRESS(ev->keys, KEY_UP) || KEYCHECK_PRESS(ev->keys, KEY_LEFT))
		{
			if (aprsLogIndex > 0)
			{
				aprsLogIndex--;
			}
			aprsSelCall[0] = 0;
			aprsLogShowScreen();
			return MENU_STATUS_SUCCESS;
		}
	}

	// Repaint so the counters move while packets arrive - but on a timer. Doing
	// it every pass meant a full 40KB frame going out to the LCD on every turn
	// of the menu loop, which left no time to service the keypad and made the
	// screen look frozen.
	if ((uint32_t)(ev->time - aprsLogLastDraw) >= 250)
	{
		aprsLogLastDraw = ev->time;
		aprsLogShowScreen();
	}

	return MENU_STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// APRS message composer.
//
// Two fields - who it goes to and what it says - with up/down moving between
// them and the keypad typing into whichever is selected. Reached with STAR from
// the received list, which pre-fills the addressee when the highlighted packet
// was a message to us, so replying takes one key.
//
// The frame itself is built by aprsSendInfoField(), which reuses the beacon's
// encoder and transmit path. Nothing here touches the radio directly.
// ---------------------------------------------------------------------------
#define APRS_MSG_CALL_MAX 9
#define APRS_MSG_TEXT_MAX 60

static char aprsMsgTo[APRS_MSG_CALL_MAX + 1] = "";
static char aprsMsgText[APRS_MSG_TEXT_MAX + 1] = "";
static uint8_t aprsMsgToLen = 0; // uint8_t: main RAM is full, and none of these passes 255
static uint8_t aprsMsgTextLen = 0; // uint8_t: main RAM is full, and none of these passes 255
static bool aprsMsgFieldIsText = false; // false = addressee, true = message
static bool aprsMsgIncludePosition = false;
static uint8_t aprsMsgSentCount = 0; // uint8_t: main RAM is full, and none of these passes 255
static uint8_t aprsMsgIdCounter = 1;

// A live GPS fix, if the receiver has one and is switched on.
static bool aprsMsgPositionIsAvailable(void)
{
#if defined(HAS_GPS)
	return ((nonVolatileSettings.gps > GPS_MODE_OFF) &&
			((gpsData.Status & GPS_STATUS_HAS_FIX) == GPS_STATUS_HAS_FIX));
#else
	return false;
#endif
}

// Appends the position to the message as readable degrees.
//
// An APRS message field has no place for coordinates - that is what a position
// report is for - so this goes in the text where the person reading it can see
// it. Written out with integer arithmetic because newlib-nano's printf has no
// floating point at all.
static void aprsMsgAppendPosition(char *dest, int destSize)
{
#if defined(HAS_GPS)
	double lat = gpsData.LatitudeHiRes;
	double lon = gpsData.LongitudeHiRes;
	char latHemi = ((lat < 0.0) ? 'G' : 'K');
	char lonHemi = ((lon < 0.0) ? 'B' : 'D');
	int latWhole, latFrac, lonWhole, lonFrac;

	if (lat < 0.0)
	{
		lat = -lat;
	}
	if (lon < 0.0)
	{
		lon = -lon;
	}

	latWhole = (int) lat;
	latFrac = (int)((lat - (double) latWhole) * 10000.0);
	lonWhole = (int) lon;
	lonFrac = (int)((lon - (double) lonWhole) * 10000.0);

	snprintf((dest + strlen(dest)), (size_t)(destSize - (int) strlen(dest)),
			" %d.%04d%c %d.%04d%c", latWhole, latFrac, latHemi, lonWhole, lonFrac, lonHemi);
#else
	(void) dest;
	(void) destSize;
#endif
}

// Called from the received list so a reply starts already addressed.
void uiAPRSMessagePresetTo(const char *call)
{
	snprintf(aprsMsgTo, sizeof(aprsMsgTo), "%.*s", APRS_MSG_CALL_MAX, ((call != NULL) ? call : ""));
	aprsMsgToLen = (int) strlen(aprsMsgTo);
	aprsMsgFieldIsText = (aprsMsgToLen > 0); // already addressed, so start in the text
}

static void aprsMsgShowScreen(char preview)
{
	char buf[APRS_LINE_BUF];
	int y;

	displayClearBuf();
	menuDisplayTitle(LANG4("MESAJ G\xD6""NDER", "SEND MESSAGE", "ENVIAR MENSAJE", "\xCE\xD2\xCF\xD0\xC0\xC2\xC8\xD2\xDC \xD1\xCE\xCE\xC1\xD9."));

	// Both fields are drawn from a working copy that already has the character
	// being cycled on the end. Multi-tap is unusable without it: the operator
	// has to see which letter the key is currently on before the timeout commits
	// it. An underscore was going in instead of the letter, which showed that a
	// key was down but not what it would type.
	{
		char toShown[APRS_MSG_CALL_MAX + 2];
		char textShown[APRS_MSG_TEXT_MAX + 2];
		int pos = 0;

		snprintf(toShown, sizeof(toShown), "%s", aprsMsgTo);
		snprintf(textShown, sizeof(textShown), "%s", aprsMsgText);

		if (preview != 0)
		{
			if (aprsMsgFieldIsText)
			{
				int l = (int) strlen(textShown);

				if (l < APRS_MSG_TEXT_MAX)
				{
					textShown[l] = preview;
					textShown[l + 1] = 0;
				}
			}
			else
			{
				int l = (int) strlen(toShown);

				if (l < APRS_MSG_CALL_MAX)
				{
					toShown[l] = (char)(((preview >= 'a') && (preview <= 'z')) ? (preview - 32) : preview);
					toShown[l + 1] = 0;
				}
			}
		}

		// Addressee
		displayThemeApply((aprsMsgFieldIsText ? THEME_ITEM_FG_MENU_ITEM : THEME_ITEM_FG_CHANNEL_NAME), THEME_ITEM_BG);
		snprintf(buf, sizeof(buf), LANG4("Kime: %s", "To: %s", "Para: %s", "\xCA\xEE\xEC\xF3: %s"), toShown);
		displayPrintAt(2, 20, buf, FONT_SIZE_1);
		if (aprsMsgFieldIsText == false)
		{
			displayDrawFastHLine(2, 30, (DISPLAY_SIZE_X - 4), true);
		}

		// Message, wrapped
		displayThemeApply((aprsMsgFieldIsText ? THEME_ITEM_FG_CHANNEL_NAME : THEME_ITEM_FG_MENU_ITEM), THEME_ITEM_BG);
		displayPrintAt(2, 36, LANG4("Mesaj:", "Message:", "Mensaje:", "\xD1\xEE\xEE\xE1\xF9\xE5\xED\xE8\xE5:"), FONT_SIZE_1);
		y = 47;

		// Stop on the length, not on finding a terminator at the start of a line.
		//
		// The loop stepped 26 characters at a time and tested textShown[pos], so
		// with a 15 character message it was asking about textShown[26] - past the
		// terminator, in a stack buffer that snprintf leaves untouched beyond the
		// string it wrote. Whenever that byte happened not to be zero the loop ran
		// on and printed whatever was left on the stack, which is where the
		// nonsense under the message came from. It appeared only sometimes for the
		// same reason: it depended on what had last used that stack.
		{
			int len = (int) strlen(textShown);

			while ((pos < len) && (y < 80))
			{
				snprintf(buf, sizeof(buf), "%.*s", APRS_LINE_CHARS, (textShown + pos));
				displayPrintAt(2, y, buf, FONT_SIZE_1);
				pos += APRS_LINE_CHARS;
				y += 9;
			}
		}
		if ((textShown[0] == 0) && aprsMsgFieldIsText)
		{
			y += 9;
		}
	}
	displayThemeResetToDefault();

	// Position, and whether it goes out with the message.
	displayThemeApply((aprsMsgIncludePosition ? THEME_ITEM_FG_CHANNEL_CONTACT_INFO : THEME_ITEM_FG_DECORATION),
			THEME_ITEM_BG);
	if (aprsMsgPositionIsAvailable())
	{
		snprintf(buf, sizeof(buf), LANG4("Konum: %s", "Position: %s", "Posici\xF3n: %s", "\xCF\xEE\xE7\xE8\xF6\xE8\xFF: %s"), (aprsMsgIncludePosition ? LANG4("EKLE", "ADD", "S\xCD", "\xC4\xC0") : LANG4("ekleme", "no", "no", "\xED\xE5\xF2")));
	}
	else
	{
		snprintf(buf, sizeof(buf), LANG4("Konum: yok (GPS)", "Position: none (GPS)", "Posici\xF3n: no (GPS)", "\xCF\xEE\xE7\xE8\xF6\xE8\xFF: \xED\xE5\xF2 (GPS)"));
	}
	displayPrintAt(2, 82, buf, FONT_SIZE_1);
	displayThemeResetToDefault();

	snprintf(buf, sizeof(buf), LANG4("%d/%d karakter   %d g\xF6""nd.", "%d/%d chars   %d sent", "%d/%d caract.  %d env.", "%d/%d \xF1\xE8\xEC\xE2.  %d \xEE\xF2\xEF\xF0."),
			aprsMsgTextLen, APRS_MSG_TEXT_MAX, aprsMsgSentCount);
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayPrintAt(2, 94, buf, FONT_SIZE_1);
	displayThemeResetToDefault();

	displayPrintAt(2, 106, LANG4("Yukar\x93""/A\x96""a\x98""\x93"" alan  SK2 konum", "Up/Down field  SK2 pos.", "Arr/Abajo campo  SK2 pos.", "\xC2\xE2\xE5\xF0\xF5/\xC2\xED\xE8\xE7 \xEF\xEE\xEB\xE5 SK2 \xEF\xEE\xE7"), FONT_SIZE_1);
	displayPrintAt(2, 115, LANG4("Ye\x96""il G\xF6""nder  K\x93""rm\x93""z\x93"" sil", "Green send  Red delete", "Verde enviar  Rojo borrar", "\xC7\xE5\xEB \xEE\xF2\xEF\xF0.  \xCA\xF0\xE0\xF1 \xF3\xE4\xE0\xEB."), FONT_SIZE_1);
	displayRender();
}

static void aprsMsgSend(void)
{
	char info[APRS_MSG_CALL_MAX + APRS_MSG_TEXT_MAX + 52];

	if ((aprsMsgToLen == 0) || (aprsMsgTextLen == 0))
	{
		soundSetMelody(MELODY_NACK_BEEP);
		return;
	}

	// ":ADDRESSEE:text{id" - the addressee field is exactly nine characters,
	// padded with spaces, which is what makes the receiver's colon at offset ten
	// a reliable way of recognising a message.
	{
		char body[APRS_MSG_TEXT_MAX + 32];

		snprintf(body, sizeof(body), "%s", aprsMsgText);

		if (aprsMsgIncludePosition && aprsMsgPositionIsAvailable())
		{
			aprsMsgAppendPosition(body, (int) sizeof(body));
		}

		snprintf(info, sizeof(info), ":%-9.9s:%s{%d", aprsMsgTo, body, (int) aprsMsgIdCounter);
	}

	if (aprsSendInfoField(info))
	{
		aprsMsgSentCount++;
		aprsMsgIdCounter = (uint8_t)((aprsMsgIdCounter >= 99) ? 1 : (aprsMsgIdCounter + 1));
		soundSetMelody(MELODY_ACK_BEEP);
	}
	else
	{
		soundSetMelody(MELODY_NACK_BEEP);
	}
}

menuStatus_t menuAPRSSend(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		aprsMsgTextLen = (int) strlen(aprsMsgText);
		keypadAlphaEnable = true; // multi-tap letters from the driver
		aprsMsgShowScreen(0);
		return (MENU_STATUS_INPUT_TYPE | MENU_STATUS_SUCCESS);
	}

	if (ev->hasEvent)
	{
		// Long RED wipes both fields but stays on the screen, so the way out is
		// always the same key: delete until empty, then press once more. Same
		// behaviour as the Morse composer.
		if (KEYCHECK_LONGDOWN(ev->keys, KEY_RED) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_RED) == false))
		{
			aprsMsgText[0] = 0;
			aprsMsgTextLen = 0;
			aprsMsgTo[0] = 0;
			aprsMsgToLen = 0;
			aprsMsgFieldIsText = false;
			aprsMsgShowScreen(0);
			return MENU_STATUS_SUCCESS;
		}

		if (KEYCHECK_SHORTUP(ev->keys, KEY_UP) || KEYCHECK_SHORTUP(ev->keys, KEY_DOWN))
		{
			aprsMsgFieldIsText = !aprsMsgFieldIsText;
			aprsMsgShowScreen(0);
			return MENU_STATUS_SUCCESS;
		}

		// SK2 toggles whether the position goes out with the message.
		//
		// This was on HASH, which did not work: with keypadAlphaEnable set the
		// driver treats HASH as the key that commits a pending multi-tap letter,
		// so the screen never sees it, and STAR is a typed character in its own
		// right. Neither can act as a control key while text entry is on. The
		// side buttons go through a separate path and are unaffected.
		if (BUTTONCHECK_SHORTUP(ev, BUTTON_SK2))
		{
			if (aprsMsgPositionIsAvailable())
			{
				aprsMsgIncludePosition = !aprsMsgIncludePosition;
			}
			else
			{
				soundSetMelody(MELODY_NACK_BEEP); // no fix, nothing to send
			}
			aprsMsgShowScreen(0);
			return MENU_STATUS_SUCCESS;
		}

		// Short RED deletes one character. With nothing left to delete it is the
		// way out - so the operator never has to remember a separate exit key.
		// keypadAlphaEnable must be cleared on every path out of here, or the
		// channel screen inherits multi-tap and its digits stop working.
		if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			if (aprsMsgFieldIsText && (aprsMsgTextLen > 0))
			{
				aprsMsgText[--aprsMsgTextLen] = 0;
			}
			else if ((aprsMsgFieldIsText == false) && (aprsMsgToLen > 0))
			{
				aprsMsgTo[--aprsMsgToLen] = 0;
			}
			else if ((aprsMsgTextLen == 0) && (aprsMsgToLen == 0))
			{
				keypadAlphaEnable = false;
				menuSystemPopPreviousMenu();
				return MENU_STATUS_SUCCESS;
			}
			else
			{
				// This field is empty but the other is not - move to it rather
				// than leaving, which would throw away what is still typed.
				aprsMsgFieldIsText = !aprsMsgFieldIsText;
			}
			aprsMsgShowScreen(0);
			return MENU_STATUS_SUCCESS;
		}

		if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			aprsMsgSend();
			aprsMsgShowScreen(0);
			return MENU_STATUS_SUCCESS;
		}

		// STAR opens the WhatsApp composer, and SK1 does the same.
		//
		// Acted on when the key is released, and every other form of it swallowed
		// so it is never typed into the message. Both parts matter. With
		// keypadAlphaEnable set the driver resolves STAR into a character and
		// hands it over like a letter, which is why it has to be swallowed - the
		// same trap that stopped HASH being used for the position toggle. And the
		// first press of it produces only a preview, with the press event
		// following a second later when multi-tap times out, so acting on the
		// press made the screen appear to ignore the key.
		//
		// The release arrives in both keypad modes, since KEY_STAR is '*' and the
		// driver reports the raw code on release. The cost is that a literal '*'
		// can no longer be typed into a message, which none of them need.
		//
		// SK1 does the same, through the separate side-button path.
		if ((ev->keys.key == KEY_STAR) || BUTTONCHECK_SHORTUP(ev, BUTTON_SK1))
		{
			if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR) || BUTTONCHECK_SHORTUP(ev, BUTTON_SK1))
			{
				keypadAlphaEnable = false;
				menuSystemPushNewMenu(UI_APRS_WHATSAPP);
			}
			return MENU_STATUS_SUCCESS;
		}

		// Typing. The driver resolves multi-tap and hands back the letter.
		if ((ev->keys.key >= ' ') && (ev->keys.key < 127))
		{
			if (ev->keys.event & KEY_MOD_PREVIEW)
			{
				aprsMsgShowScreen((char) ev->keys.key);
				return MENU_STATUS_SUCCESS;
			}

			if (ev->keys.event & KEY_MOD_PRESS)
			{
				if (aprsMsgFieldIsText)
				{
					if (aprsMsgTextLen < APRS_MSG_TEXT_MAX)
					{
						aprsMsgText[aprsMsgTextLen++] = (char) ev->keys.key;
						aprsMsgText[aprsMsgTextLen] = 0;
					}
				}
				else
				{
					// Callsigns are upper case, and the addressee has to match
					// exactly for the far station to see the message.
					if (aprsMsgToLen < APRS_MSG_CALL_MAX)
					{
						char c = (char) ev->keys.key;

						if ((c >= 'a') && (c <= 'z'))
						{
							c -= 32;
						}
						aprsMsgTo[aprsMsgToLen++] = c;
						aprsMsgTo[aprsMsgToLen] = 0;
					}
				}
				aprsMsgShowScreen(0);
				return MENU_STATUS_SUCCESS;
			}
		}
	}

	return MENU_STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// WhatsApp composer, over the APRS message gateway.
//
// The gateway takes an ordinary APRS message addressed to WTSAPP whose text
// begins with the recipient's number in international form, an at sign in front
// and a space after it, then the message. Doing that by hand from the normal
// composer means remembering the addressee, the at sign, and converting the
// number - so this screen holds the first two and does the conversion.
//
// Two fields. The number takes digits only, which is why it runs with
// keypadAlphaEnable off: with multi-tap on, a keypress would resolve into a
// letter. The message field turns it back on. Switching fields therefore has to
// switch that flag too, and every way out of here has to clear it, or the channel
// screen inherits multi-tap and its digits stop working.
// ---------------------------------------------------------------------------
#define WA_NUM_MAX  15   // E.164 allows fifteen digits

// What is left of the message field once the number is in it, and the limit is
// the protocol rather than the screen. APRS allows 67 characters of message text
// after the addressee; the recipient sits inside that, as an at sign, up to
// fifteen digits and a space - seventeen characters at worst - which leaves 50.
// Five lines of the display hold 130, so there is room to spare there; going past
// 50 would risk something between here and the gateway truncating the message
// instead.
#define WA_TEXT_MAX 50

static char waNum[WA_NUM_MAX + 1] = "";
static char waText[WA_TEXT_MAX + 1] = "";
static uint8_t waNumLen = 0; // uint8_t: main RAM is full, and none of these passes 255
static uint8_t waTextLen = 0; // uint8_t: main RAM is full, and none of these passes 255
static bool waFieldIsText = false;
static uint8_t waSentCount = 0; // uint8_t: main RAM is full, and none of these passes 255

// A Turkish mobile is written 0 5xx xxx xxxx at home and 90 5xx xxx xxxx abroad,
// and the gateway needs the second form. So a leading zero is the trunk prefix and
// goes; two of them are the international dialling prefix and also go, which
// leaves a number that already starts with the country code. Anything still
// starting with a subscriber digit gets 90 put in front.
//
// A number typed with a leading '+' is a foreign one in full international form
// (+1..., +7..., +49...): it goes exactly as typed, minus the plus, and is never
// given the Turkish 90.
static void waBuildNumber(char *dest, int destSize, const char *raw)
{
	const char *p = raw;

	if (*p == '+')
	{
		snprintf(dest, (size_t) destSize, "@%s", (p + 1));
		return;
	}

	while (*p == '0')
	{
		p++;
	}

	if (*p == '9')
	{
		snprintf(dest, (size_t) destSize, "@%s", p);
	}
	else
	{
		snprintf(dest, (size_t) destSize, "@90%s", p);
	}
}

static void waShowScreen(char preview)
{
	char buf[APRS_LINE_BUF];
	char textShown[WA_TEXT_MAX + 2];
	int pos = 0;
	int y;

	displayClearBuf();
	menuDisplayTitle("WHATSAPP");

	snprintf(textShown, sizeof(textShown), "%s", waText);

	// The character being cycled has to be on the end of the working copy, or
	// multi-tap is unusable - the operator cannot see which letter the key is on
	// before the timeout commits it.
	if ((preview != 0) && waFieldIsText)
	{
		int l = (int) strlen(textShown);

		if (l < WA_TEXT_MAX)
		{
			textShown[l] = preview;
			textShown[l + 1] = 0;
		}
	}

	// Number, as typed.
	displayThemeApply((waFieldIsText ? THEME_ITEM_FG_MENU_ITEM : THEME_ITEM_FG_CHANNEL_NAME), THEME_ITEM_BG);
	snprintf(buf, sizeof(buf), LANG4("Numara: %s", "Number: %s", "N\xFAmero: %s", "\xCD\xEE\xEC\xE5\xF0: %s"), waNum);
	displayPrintAt(2, 20, buf, FONT_SIZE_1);
	if (waFieldIsText == false)
	{
		displayDrawFastHLine(2, 30, (DISPLAY_SIZE_X - 4), true);
	}
	displayThemeResetToDefault();

	// The converted number used to be echoed on a line of its own here. It went:
	// once the conversion is understood it says nothing the number above does not,
	// and the line is worth more as a line of message.

	// Message, wrapped.
	displayThemeApply((waFieldIsText ? THEME_ITEM_FG_CHANNEL_NAME : THEME_ITEM_FG_MENU_ITEM), THEME_ITEM_BG);
	displayPrintAt(2, 36, LANG4("Mesaj:", "Message:", "Mensaje:", "\xD1\xEE\xEE\xE1\xF9\xE5\xED\xE8\xE5:"), FONT_SIZE_1);
	y = 47;

	// Stop on the length. Testing for a terminator every 26 characters looks past
	// the end of the string into stack that snprintf never wrote - see the same
	// loop in aprsMsgShowScreen for what that put on the screen.
	{
		int len = (int) strlen(textShown);

		while ((pos < len) && (y < 90))
		{
			snprintf(buf, sizeof(buf), "%.*s", APRS_LINE_CHARS, (textShown + pos));
			displayPrintAt(2, y, buf, FONT_SIZE_1);
			pos += APRS_LINE_CHARS;
			y += 9;
		}
	}
	displayThemeResetToDefault();

	// While the number is being typed, this line says how to enter a foreign one;
	// on the message it goes back to counting characters.
	if (waFieldIsText == false)
	{
		snprintf(buf, sizeof(buf), LANG4("Yabanc\x93"" no: 1'e 2 kez = +", "Foreign no: 1 twice = +", "Extranjero: 1 dos veces=+", "\xC8\xED\xEE\xF1\xF2\xF0. \xED\xEE\xEC: 1 \xE4\xE2\xE0\xE6\xE4\xFB=+"));
	}
	else
	{
		snprintf(buf, sizeof(buf), LANG4("%d/%d karakter   %d g\xF6""nd.", "%d/%d chars   %d sent", "%d/%d caract.  %d env.", "%d/%d \xF1\xE8\xEC\xE2.  %d \xEE\xF2\xEF\xF0."), waTextLen, WA_TEXT_MAX, waSentCount);
	}
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayPrintAt(2, 94, buf, FONT_SIZE_1);
	displayThemeResetToDefault();

	displayPrintAt(2, 106, LANG4("Yukar\x93""/A\x96""a\x98""\x93"" alan", "Up/Down: field", "Arriba/Abajo: campo", "\xC2\xE2\xE5\xF0\xF5/\xC2\xED\xE8\xE7: \xEF\xEE\xEB\xE5"), FONT_SIZE_1);
	displayPrintAt(2, 115, LANG4("Ye\x96""il G\xF6""nder  K\x93""rm\x93""z\x93"" sil", "Green send  Red delete", "Verde enviar  Rojo borrar", "\xC7\xE5\xEB \xEE\xF2\xEF\xF0.  \xCA\xF0\xE0\xF1 \xF3\xE4\xE0\xEB."), FONT_SIZE_1);
	displayRender();
}

static void waSend(void)
{
	char info[WA_NUM_MAX + WA_TEXT_MAX + 40];
	char num[WA_NUM_MAX + 4];

	// Ten digits is the shortest a Turkish mobile can be once the trunk zero is
	// off, so anything shorter is a half-typed number rather than a real one. A
	// foreign number ('+' then country code) can be shorter: seven digits after
	// the plus is the least any country uses.
	if ((((waNum[0] == '+') ? (waNumLen - 1) : waNumLen) < ((waNum[0] == '+') ? 7 : 10)) || (waTextLen == 0))
	{
		soundSetMelody(MELODY_NACK_BEEP);
		return;
	}

	waBuildNumber(num, (int) sizeof(num), waNum);

	// Same shape as an ordinary APRS message - the addressee field is exactly
	// nine characters, space padded - so the gateway sees it as one. The
	// identifier is shared with the normal composer so two messages sent close
	// together cannot end up quoting the same one back.
	snprintf(info, sizeof(info), ":%-9.9s:%s %s{%d", "WTSAPP", num, waText, (int) aprsMsgIdCounter);

	if (aprsSendInfoField(info))
	{
		waSentCount++;
		aprsMsgIdCounter = (uint8_t)((aprsMsgIdCounter >= 99) ? 1 : (aprsMsgIdCounter + 1));
		soundSetMelody(MELODY_ACK_BEEP);
	}
	else
	{
		soundSetMelody(MELODY_NACK_BEEP);
	}
}

menuStatus_t menuAPRSWhatsApp(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		waNumLen = (int) strlen(waNum);
		waTextLen = (int) strlen(waText);
		waFieldIsText = false;
		keypadAlphaEnable = false; // the number field takes digits only
		waShowScreen(0);
		return (MENU_STATUS_INPUT_TYPE | MENU_STATUS_SUCCESS);
	}

	if (ev->hasEvent)
	{
		// Long RED wipes both fields but stays here, so the way out is always the
		// same key: delete until empty, then press once more.
		if (KEYCHECK_LONGDOWN(ev->keys, KEY_RED) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_RED) == false))
		{
			waNum[0] = 0;
			waNumLen = 0;
			waText[0] = 0;
			waTextLen = 0;
			waFieldIsText = false;
			keypadAlphaEnable = false;
			waShowScreen(0);
			return MENU_STATUS_SUCCESS;
		}

		// The keypad arrows produce KEY_UP and KEY_DOWN; KEY_FRONT_UP and
		// KEY_FRONT_DOWN are a separate pair of buttons on variants that have
		// them. Both are accepted so either works.
		if (KEYCHECK_SHORTUP(ev->keys, KEY_FRONT_UP) || KEYCHECK_SHORTUP(ev->keys, KEY_UP) ||
				KEYCHECK_SHORTUP(ev->keys, KEY_FRONT_DOWN) || KEYCHECK_SHORTUP(ev->keys, KEY_DOWN))
		{
			waFieldIsText = !waFieldIsText;
			keypadAlphaEnable = waFieldIsText; // letters in the message, digits in the number
			waShowScreen(0);
			return MENU_STATUS_SUCCESS;
		}

		if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			if (waFieldIsText && (waTextLen > 0))
			{
				waText[--waTextLen] = 0;
			}
			else if ((waFieldIsText == false) && (waNumLen > 0))
			{
				waNum[--waNumLen] = 0;
			}
			else if ((waTextLen == 0) && (waNumLen == 0))
			{
				keypadAlphaEnable = false;
				menuSystemPopPreviousMenu();
				return MENU_STATUS_SUCCESS;
			}
			else
			{
				// This field is empty and the other is not: move rather than
				// leave, which would throw away what is still typed.
				waFieldIsText = !waFieldIsText;
				keypadAlphaEnable = waFieldIsText;
			}
			waShowScreen(0);
			return MENU_STATUS_SUCCESS;
		}

		if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			waSend();
			waShowScreen(0);
			return MENU_STATUS_SUCCESS;
		}

		// STAR goes on to the e-mail composer. See the note on the same key in
		// menuAPRSSend for why it is handled this way rather than with a plain
		// key test.
		if (ev->keys.key == KEY_STAR)
		{
			if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
			{
				keypadAlphaEnable = false;
				menuSystemPushNewMenu(UI_APRS_EMAIL);
			}
			return MENU_STATUS_SUCCESS; // swallows the character forms as well
		}

		// The number field, on a short release of a digit key.
		//
		// This is the idiom the other plain-digit screens use, and it is not
		// interchangeable with the one below: KEY_MOD_PRESS carries a resolved
		// multi-tap character and only means anything while keypadAlphaEnable is
		// set. With it clear - which is what makes this field digits only - the
		// press event is not what a screen reads, and reading it was why nothing
		// was typed here at all.
		if (waFieldIsText == false)
		{
			if (KEYCHECK_SHORTUP_NUMBER(ev->keys))
			{
				// The 1 key gives the '+' of a foreign number, the way phones do:
				// pressed twice at the start of the number, the "1" it typed becomes
				// "+", and the digits carry on after it (1 1 then 7 gives +7).
				// Decided from what is already typed rather than from a timer, so
				// it needs no state - RAM is full to within a few bytes. No country
				// code is 11 and no Turkish number starts with it, so nothing real
				// is lost.
				if ((ev->keys.key == '1') && (waNumLen == 1) && (waNum[0] == '1'))
				{
					waNum[0] = '+';
				}
				else if (waNumLen < WA_NUM_MAX)
				{
					waNum[waNumLen++] = (char) ev->keys.key;
					waNum[waNumLen] = 0;
				}
				waShowScreen(0);
				return MENU_STATUS_SUCCESS;
			}
		}
		// The message field, on the multi-tap character the driver hands back.
		else if ((ev->keys.key >= ' ') && (ev->keys.key < 127))
		{
			if (ev->keys.event & KEY_MOD_PREVIEW)
			{
				waShowScreen((char) ev->keys.key);
				return MENU_STATUS_SUCCESS;
			}

			if (ev->keys.event & KEY_MOD_PRESS)
			{
				if (waTextLen < WA_TEXT_MAX)
				{
					waText[waTextLen++] = (char) ev->keys.key;
					waText[waTextLen] = 0;
				}
				waShowScreen(0);
				return MENU_STATUS_SUCCESS;
			}
		}
	}

	return MENU_STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// E-mail composer, over the APRS message gateway.
//
// Same idea as the WhatsApp screen above and reached the same way, with STAR - so
// one press of it gets to WhatsApp and a second to here. The gateway takes a
// message addressed to EMAIL-2 whose text is the recipient's address, a space,
// then the message.
//
// Both fields run with keypadAlphaEnable on, unlike the WhatsApp screen where the
// number field had to have it off. An address needs the at sign and the dot, and
// both are on the 1 key: its multi-tap set is "1.!,@-:?()~/[]#<>=*+$%'`&|_^{}",
// so a dot is the second press and an at sign the fifth.
// ---------------------------------------------------------------------------
#define EMAIL_ADDR_MAX 40
#define EMAIL_TEXT_MAX 50

static char emailAddr[EMAIL_ADDR_MAX + 1] = "";
static char emailText[EMAIL_TEXT_MAX + 1] = "";
static uint8_t emailAddrLen = 0; // uint8_t: main RAM is full, and none of these passes 255
static uint8_t emailTextLen = 0; // uint8_t: main RAM is full, and none of these passes 255
static bool emailFieldIsText = false;
static uint8_t emailSentCount = 0; // uint8_t: main RAM is full, and none of these passes 255

// How much message there is room for, which depends on how long the address is.
// APRS allows 67 characters of message text after the addressee and the recipient
// sits inside that, with a space after it. A fixed limit would either waste the
// allowance on short addresses or overrun it on long ones.
static int emailTextRoom(void)
{
	int room = (67 - (emailAddrLen + 1));

	if (room > EMAIL_TEXT_MAX)
	{
		room = EMAIL_TEXT_MAX;
	}
	return ((room < 0) ? 0 : room);
}

// Enough of a check to catch a half-typed address rather than to validate one:
// an at sign that is not the first character, and a dot somewhere after it.
static bool emailAddrLooksValid(void)
{
	const char *at = strchr(emailAddr, '@');

	return ((at != NULL) && (at != emailAddr) && (at[1] != 0) && (strchr(at, '.') != NULL));
}

// Wrapped text, returning the y to carry on from.
static int emailDrawWrapped(const char *s, int y, int yLimit)
{
	char buf[APRS_LINE_BUF];
	int len = (int) strlen(s);
	int pos = 0;

	// On the length, not on a terminator sampled every 26 characters - that reads
	// past the end of the string. See aprsMsgShowScreen.
	while ((pos < len) && (y < yLimit))
	{
		snprintf(buf, sizeof(buf), "%.*s", APRS_LINE_CHARS, (s + pos));
		displayPrintAt(2, y, buf, FONT_SIZE_1);
		pos += APRS_LINE_CHARS;
		y += 9;
	}
	return y;
}

static void emailShowScreen(char preview)
{
	char buf[APRS_LINE_BUF];
	char addrShown[EMAIL_ADDR_MAX + 2];
	char textShown[EMAIL_TEXT_MAX + 2];

	displayClearBuf();
	menuDisplayTitle(LANG4("E-POSTA", "E-MAIL", "CORREO", "E-MAIL"));

	snprintf(addrShown, sizeof(addrShown), "%s", emailAddr);
	snprintf(textShown, sizeof(textShown), "%s", emailText);

	// The character being cycled has to be on the end of the working copy, or
	// multi-tap is unusable - the operator cannot see which letter the key is on
	// before the timeout commits it.
	if (preview != 0)
	{
		char *dst = (emailFieldIsText ? textShown : addrShown);
		int max = (emailFieldIsText ? EMAIL_TEXT_MAX : EMAIL_ADDR_MAX);
		int l = (int) strlen(dst);

		if (l < max)
		{
			dst[l] = preview;
			dst[l + 1] = 0;
		}
	}

	displayThemeApply((emailFieldIsText ? THEME_ITEM_FG_MENU_ITEM : THEME_ITEM_FG_CHANNEL_NAME), THEME_ITEM_BG);
	displayPrintAt(2, 20, LANG4("E-posta:", "E-mail:", "Correo:", "E-mail:"), FONT_SIZE_1);
	emailDrawWrapped(addrShown, 31, 49);
	if (emailFieldIsText == false)
	{
		displayDrawFastHLine(2, 49, (DISPLAY_SIZE_X - 4), true);
	}
	displayThemeResetToDefault();

	displayThemeApply((emailFieldIsText ? THEME_ITEM_FG_CHANNEL_NAME : THEME_ITEM_FG_MENU_ITEM), THEME_ITEM_BG);
	displayPrintAt(2, 53, LANG4("Mesaj:", "Message:", "Mensaje:", "\xD1\xEE\xEE\xE1\xF9\xE5\xED\xE8\xE5:"), FONT_SIZE_1);
	emailDrawWrapped(textShown, 64, 92);
	displayThemeResetToDefault();

	snprintf(buf, sizeof(buf), LANG4("%d/%d karakter   %d g\xF6""nd.", "%d/%d chars   %d sent", "%d/%d caract.  %d env.", "%d/%d \xF1\xE8\xEC\xE2.  %d \xEE\xF2\xEF\xF0."),
			emailTextLen, emailTextRoom(), emailSentCount);
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayPrintAt(2, 94, buf, FONT_SIZE_1);
	displayThemeResetToDefault();

	displayPrintAt(2, 106, LANG4("Yukar\x93""/A\x96""a\x98""\x93"" alan", "Up/Down: field", "Arriba/Abajo: campo", "\xC2\xE2\xE5\xF0\xF5/\xC2\xED\xE8\xE7: \xEF\xEE\xEB\xE5"), FONT_SIZE_1);
	displayPrintAt(2, 115, LANG4("Ye\x96""il G\xF6""nder  K\x93""rm\x93""z\x93"" sil", "Green send  Red delete", "Verde enviar  Rojo borrar", "\xC7\xE5\xEB \xEE\xF2\xEF\xF0.  \xCA\xF0\xE0\xF1 \xF3\xE4\xE0\xEB."), FONT_SIZE_1);
	displayRender();
}

static void emailSend(void)
{
	char info[EMAIL_ADDR_MAX + EMAIL_TEXT_MAX + 40];

	if ((emailAddrLooksValid() == false) || (emailTextLen == 0))
	{
		soundSetMelody(MELODY_NACK_BEEP);
		return;
	}

	// Same shape as an ordinary APRS message - the addressee field is exactly
	// nine characters, space padded - so the gateway sees it as one. The
	// identifier is shared with the other composers so two messages sent close
	// together cannot end up quoting the same one back.
	snprintf(info, sizeof(info), ":%-9.9s:%s %s{%d", "EMAIL-2", emailAddr, emailText, (int) aprsMsgIdCounter);

	if (aprsSendInfoField(info))
	{
		emailSentCount++;
		aprsMsgIdCounter = (uint8_t)((aprsMsgIdCounter >= 99) ? 1 : (aprsMsgIdCounter + 1));
		soundSetMelody(MELODY_ACK_BEEP);
	}
	else
	{
		soundSetMelody(MELODY_NACK_BEEP);
	}
}

menuStatus_t menuAPRSEmail(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		emailAddrLen = (int) strlen(emailAddr);
		emailTextLen = (int) strlen(emailText);
		emailFieldIsText = false;
		keypadAlphaEnable = true; // both fields take letters
		emailShowScreen(0);
		return (MENU_STATUS_INPUT_TYPE | MENU_STATUS_SUCCESS);
	}

	if (ev->hasEvent)
	{
		// Long RED wipes both fields but stays here, so the way out is always the
		// same key: delete until empty, then press once more.
		if (KEYCHECK_LONGDOWN(ev->keys, KEY_RED) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_RED) == false))
		{
			emailAddr[0] = 0;
			emailAddrLen = 0;
			emailText[0] = 0;
			emailTextLen = 0;
			emailFieldIsText = false;
			emailShowScreen(0);
			return MENU_STATUS_SUCCESS;
		}

		if (KEYCHECK_SHORTUP(ev->keys, KEY_UP) || KEYCHECK_SHORTUP(ev->keys, KEY_DOWN) ||
				KEYCHECK_SHORTUP(ev->keys, KEY_FRONT_UP) || KEYCHECK_SHORTUP(ev->keys, KEY_FRONT_DOWN))
		{
			emailFieldIsText = !emailFieldIsText;
			emailShowScreen(0);
			return MENU_STATUS_SUCCESS;
		}

		// keypadAlphaEnable must be cleared on every path out of here, or the
		// channel screen inherits multi-tap and its digits stop working.
		if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			if (emailFieldIsText && (emailTextLen > 0))
			{
				emailText[--emailTextLen] = 0;
			}
			else if ((emailFieldIsText == false) && (emailAddrLen > 0))
			{
				emailAddr[--emailAddrLen] = 0;
			}
			else if ((emailTextLen == 0) && (emailAddrLen == 0))
			{
				keypadAlphaEnable = false;
				menuSystemPopPreviousMenu();
				return MENU_STATUS_SUCCESS;
			}
			else
			{
				emailFieldIsText = !emailFieldIsText;
			}
			emailShowScreen(0);
			return MENU_STATUS_SUCCESS;
		}

		if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			emailSend();
			emailShowScreen(0);
			return MENU_STATUS_SUCCESS;
		}

		// Typing. The driver resolves multi-tap and hands back the character.
		if ((ev->keys.key >= ' ') && (ev->keys.key < 127))
		{
			if (ev->keys.event & KEY_MOD_PREVIEW)
			{
				emailShowScreen((char) ev->keys.key);
				return MENU_STATUS_SUCCESS;
			}

			if (ev->keys.event & KEY_MOD_PRESS)
			{
				if (emailFieldIsText)
				{
					if (emailTextLen < emailTextRoom())
					{
						emailText[emailTextLen++] = (char) ev->keys.key;
						emailText[emailTextLen] = 0;
					}
				}
				else
				{
					if (emailAddrLen < EMAIL_ADDR_MAX)
					{
						emailAddr[emailAddrLen++] = (char) ev->keys.key;
						emailAddr[emailAddrLen] = 0;

						// A longer address leaves less room for the message, so
						// what is already typed may no longer fit.
						if (emailTextLen > emailTextRoom())
						{
							emailTextLen = emailTextRoom();
							emailText[emailTextLen] = 0;
						}
					}
				}
				emailShowScreen(0);
				return MENU_STATUS_SUCCESS;
			}
		}
	}

	return MENU_STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// Morse practice.
//
// Reached with STAR from the decoded-Morse log, so one key gets to sending, a
// second to the log and a third to here. Nothing is transmitted: PTT is held off
// for this screen in applicationMain.c, and the only output is the side-tone.
//
// Two modes:
//   Free      - key whatever you like and see it come back as letters.
//   Follow    - type a word on the keypad, read off the dots and dashes it
//               should be sent as, then key it and see how much matched.
// ---------------------------------------------------------------------------
#define MORSE_TRAIN_TARGET_MAX  16
#define MORSE_TRAIN_TYPED_MAX   48

static bool morseTrainFollowMode = false;
static char morseTrainTarget[MORSE_TRAIN_TARGET_MAX + 1] = "";
static uint8_t morseTrainTargetLen = 0; // uint8_t: main RAM is full, and none of these passes 255
static char morseTrainTyped[MORSE_TRAIN_TYPED_MAX + 1] = "";
static uint8_t morseTrainTypedLen = 0; // uint8_t: main RAM is full, and none of these passes 255

// How many characters of the target the operator has keyed correctly so far.
static int morseTrainMatchLen(void)
{
	int n = 0;

	while ((n < morseTrainTypedLen) && (n < morseTrainTargetLen) &&
			(morseTrainTyped[n] == morseTrainTarget[n]))
	{
		n++;
	}
	return n;
}


// Prints text wrapped across the width, and returns the y it finished at.
static int morseTrainWrap(const char *text, int x, int y, int maxY)
{
	int len = (int) strlen(text);
	int pos = 0;

	while ((pos < len) && (y <= maxY))
	{
		char line[APRS_LINE_BUF];

		snprintf(line, sizeof(line), "%.*s", APRS_LINE_CHARS, (text + pos));
		displayPrintAt(x, y, line, FONT_SIZE_1);
		pos += APRS_LINE_CHARS;
		y += 9;
	}
	return y;
}

// No key legend here on purpose: the screen is small and the room is worth more
// spent on a longer word and the pattern to send it by.
static void morseTrainShowScreen(char preview)
{
	char buf[APRS_LINE_BUF];
	int y;

	displayClearBuf();
	menuDisplayTitle(LANG4("MORS ANTRENMAN", "MORSE PRACTICE", "PR\xC1""CTICA MORSE", "\xD2\xD0\xC5\xCD\xC0\xC6\xA8\xD0 CW"));

	snprintf(buf, sizeof(buf), "%s  %u WPM",
			(morseTrainFollowMode ? LANG4("Takip", "Follow", "Seguir", "\xCF\xEE\xE2\xF2\xEE\xF0") : LANG4("Serbest", "Free", "Libre", "\xD1\xE2\xEE\xE1\xEE\xE4\xED\xEE")), (unsigned int) morseWpm);
	displayThemeApply(THEME_ITEM_FG_CHANNEL_CONTACT_INFO, THEME_ITEM_BG);
	displayPrintAt(2, 17, buf, FONT_SIZE_1);
	displayThemeResetToDefault();

	y = 28;

	if (morseTrainFollowMode)
	{
		char shown[MORSE_TRAIN_TARGET_MAX + 2];

		snprintf(shown, sizeof(shown), "%s", morseTrainTarget);
		if (preview != 0)
		{
			int l = (int) strlen(shown);

			if (l < MORSE_TRAIN_TARGET_MAX)
			{
				shown[l] = (char)(((preview >= 'a') && (preview <= 'z')) ? (preview - 32) : preview);
				shown[l + 1] = 0;
			}
		}

		displayThemeApply(THEME_ITEM_FG_CHANNEL_NAME, THEME_ITEM_BG);
		y = morseTrainWrap(shown, 2, y, 37);
		displayThemeResetToDefault();

		// The pattern to send it by, wrapped over as many lines as it needs.
		if (shown[0] != 0)
		{
			char pattern[(MORSE_TRAIN_TARGET_MAX * 7) + 2];
			int n = 0;

			for (int i = 0; (shown[i] != 0) && (n < (int)(sizeof(pattern) - 8)); i++)
			{
				const char *pat = morseForChar(shown[i]);

				if (pat == NULL)
				{
					continue;
				}
				for (int e = 0; pat[e] != 0; e++)
				{
					pattern[n++] = pat[e];
				}
				pattern[n++] = ' ';
			}
			pattern[n] = 0;

			y = morseTrainWrap(pattern, 2, (y + 2), 74);
		}
		y = 78;
	}

	// What has been keyed. Wrapped, and stopping short of the two lines below.
	displayThemeApply(THEME_ITEM_FG_RX_FREQ, THEME_ITEM_BG);
	y = morseTrainWrap(morseTrainTyped, 2, y, 86);
	displayThemeResetToDefault();

	// The element run being keyed at this moment, on its own line.
	//
	// This used to sit at y=118. With a 16 pixel font that runs to 134 on a 128
	// pixel screen, and the bottom was simply cut off - which is why dashes were
	// visible and dots, which sit at the bottom of the character cell, were not.
	{
		char live[MORSE_KEYER_LIVE_LEN];

		morseKeyerLive(live, sizeof(live));

		if (live[0] != 0)
		{
			displayThemeApply(THEME_ITEM_FG_CHANNEL_CONTACT_INFO, THEME_ITEM_BG);
			displayPrintCore(0, 96, live, FONT_SIZE_3, TEXT_ALIGN_CENTER, false);
			displayThemeResetToDefault();
		}
	}

	if (morseTrainFollowMode && (morseTrainTargetLen > 0))
	{
		int match = morseTrainMatchLen();

		snprintf(buf, sizeof(buf), "%d / %d", match, morseTrainTargetLen);
		displayThemeApply(((match == morseTrainTargetLen) ? THEME_ITEM_FG_RSSI_BAR : THEME_ITEM_FG_MENU_ITEM),
				THEME_ITEM_BG);
		displayPrintAt(2, 116, buf, FONT_SIZE_1);
		displayThemeResetToDefault();
	}

	displayRender();
}

menuStatus_t menuMorseTrain(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		morseTrainTyped[0] = 0;
		morseTrainTypedLen = 0;
		morseKeyerReset();
		morseKeyerSpaces = false; // single words here, so no automatic word gaps
		keypadAlphaEnable = true; // for typing the target word in follow mode
		morseTrainShowScreen(0);
		return (MENU_STATUS_INPUT_TYPE | MENU_STATUS_SUCCESS);
	}

	// The key itself, polled every pass - the timing is the whole point.
	{
		char keyed = morseKeyerTick((ev->buttons & BUTTON_PTT) != 0);

		if (keyed != 0)
		{
			if (morseTrainTypedLen < MORSE_TRAIN_TYPED_MAX)
			{
				morseTrainTyped[morseTrainTypedLen++] = keyed;
				morseTrainTyped[morseTrainTypedLen] = 0;
			}
			morseTrainShowScreen(0);
		}
		else if (morseKeyerIsActive())
		{
			static uint32_t lastTrainDraw = 0;

			// Fast enough to watch a dot turn into a dash as the key is held.
			if ((uint32_t)(ev->time - lastTrainDraw) >= 40)
			{
				lastTrainDraw = ev->time;
				morseTrainShowScreen(0);
			}
		}
	}

	if (ev->hasEvent)
	{
		// SK2 switches mode. A side button is used rather than STAR or HASH
		// because with keypadAlphaEnable set the driver takes both of those for
		// text entry, so neither reaches this screen.
		if (BUTTONCHECK_SHORTUP(ev, BUTTON_SK2))
		{
			morseTrainFollowMode = !morseTrainFollowMode;
			morseTrainTyped[0] = 0;
			morseTrainTypedLen = 0;
			morseKeyerReset();
			morseTrainShowScreen(0);
			return MENU_STATUS_SUCCESS;
		}

		if (KEYCHECK_LONGDOWN(ev->keys, KEY_RED) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_RED) == false))
		{
			morseSideToneOff();
			keypadAlphaEnable = false;
			menuSystemPopPreviousMenu();
			return MENU_STATUS_SUCCESS;
		}

		// SK1 edits the target word: a press takes back one letter, holding it
		// clears the lot.
		//
		// This was on RED and appeared not to work at all. With text entry
		// enabled the driver uses any non-letter key to commit the multi-tap
		// letter still pending, and swallows that keypress - so the first RED
		// after typing went on finishing the word instead of deleting, and only
		// a second press did anything. A side button avoids that path entirely.
		if (morseTrainFollowMode && BUTTONCHECK_LONGDOWN(ev, BUTTON_SK1))
		{
			morseTrainTarget[0] = 0;
			morseTrainTargetLen = 0;
			morseTrainShowScreen(0);
			return MENU_STATUS_SUCCESS;
		}

		if (morseTrainFollowMode && BUTTONCHECK_SHORTUP(ev, BUTTON_SK1))
		{
			if (morseTrainTargetLen > 0)
			{
				morseTrainTarget[--morseTrainTargetLen] = 0;
			}
			morseTrainShowScreen(0);
			return MENU_STATUS_SUCCESS;
		}

		if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			morseSideToneOff();
			keypadAlphaEnable = false;
			menuSystemPopPreviousMenu();
			return MENU_STATUS_SUCCESS;
		}

		// GREEN held clears everything keyed so far, for another go at the same
		// word. Checked before the short press, or the short one fires first.
		if (KEYCHECK_LONGDOWN(ev->keys, KEY_GREEN) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_GREEN) == false))
		{
			morseTrainTyped[0] = 0;
			morseTrainTypedLen = 0;
			morseKeyerReset();
			morseTrainShowScreen(0);
			return MENU_STATUS_SUCCESS;
		}

		// A single press takes back one character, so a slip does not cost the
		// whole attempt.
		if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			if (morseTrainTypedLen > 0)
			{
				morseTrainTyped[--morseTrainTypedLen] = 0;
			}
			morseKeyerReset();
			morseTrainShowScreen(0);
			return MENU_STATUS_SUCCESS;
		}

		if (KEYCHECK_SHORTUP(ev->keys, KEY_UP) || KEYCHECK_SHORTUP(ev->keys, KEY_DOWN))
		{
			if (KEYCHECK_SHORTUP(ev->keys, KEY_UP))
			{
				if (morseWpm < MORSE_WPM_MAX)
				{
					morseWpm++;
				}
			}
			else if (morseWpm > MORSE_WPM_MIN)
			{
				morseWpm--;
			}
			morseTrainShowScreen(0);
			return MENU_STATUS_SUCCESS;
		}

		// Typing the target word, follow mode only.
		if (morseTrainFollowMode && (ev->keys.key >= ' ') && (ev->keys.key < 127))
		{
			if (ev->keys.event & KEY_MOD_PREVIEW)
			{
				morseTrainShowScreen((char) ev->keys.key);
				return MENU_STATUS_SUCCESS;
			}

			if (ev->keys.event & KEY_MOD_PRESS)
			{
				if (morseTrainTargetLen < MORSE_TRAIN_TARGET_MAX)
				{
					char c = (char) ev->keys.key;

					if ((c >= 'a') && (c <= 'z'))
					{
						c -= 32;
					}
					morseTrainTarget[morseTrainTargetLen++] = c;
					morseTrainTarget[morseTrainTargetLen] = 0;
				}
				morseTrainShowScreen(0);
				return MENU_STATUS_SUCCESS;
			}
		}
	}

	return MENU_STATUS_SUCCESS;
}
