/*
 * Copyright (C) 2019      Kai Ludwig, DG4KLU
 * Copyright (C) 2019-2024 Roger Clark, VK3KYY / G4KYF
 *                         Colin, G4EML
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
#include "functions/calibration.h"
#include "functions/ticks.h"
#include "hardware/HR-C6000.h"
#include "hardware/AT1846S.h"
#include "functions/settings.h"
#include "functions/trx.h"
#include "functions/rxPowerSaving.h"
#include "functions/aprs.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include <FreeRTOS.h>
#include <string.h>


#if USE_DATASHEET_RANGES
const frequencyHardwareBand_t RADIO_HARDWARE_FREQUENCY_BANDS[RADIO_BANDS_TOTAL_NUM] =  {
													{
														.minFreq=13600000,
														.maxFreq=17400000
													},// VHF
#if !(defined(PLATFORM_MD9600) || defined(PLATORM_MD380)
													{
														.minFreq=20000000,
														.maxFreq=26000000
													},// 220Mhz
#endif
													{
														.minFreq=40000000,
														.maxFreq=52000000
													}// UHF
};
#else
const frequencyHardwareBand_t RADIO_HARDWARE_FREQUENCY_BANDS[RADIO_BANDS_TOTAL_NUM] =  {
													{
														.calIQTableMinFreq = 13600000,
														.calPowerTableMinFreq = 13500000,
														.minFreq=12700000,
														.maxFreq=17800000
													},// VHF
#if !(defined(PLATFORM_MD9600) || defined(PLATFORM_MD380))
													{
														.calIQTableMinFreq = 13600000,
														.calPowerTableMinFreq = 13500000,
														.minFreq=19000000,
														.maxFreq=28200000
													},// 220Mhz
#endif
													{
														.calIQTableMinFreq = 40000000,
														.calPowerTableMinFreq = 40000000,
														.minFreq=38000000,
														.maxFreq=56400000
													}// UHF
};
#endif

#define TRX_SQUELCH_MAX    70
#define TRX_SQUELCH_HIST  3
#define TRX_SQUELCH_INC   3
const uint8_t TRX_NUM_CTCSS = 50U;

const uint16_t TRX_CTCSSTones[] = {
		670,  693,  719,  744,  770,  797,  825,  854,  885,  915,
		948,  974, 1000, 1035, 1072, 1109, 1148, 1188, 1230, 1273,
		1318, 1365, 1413, 1462, 1514, 1567, 1598, 1622, 1655, 1679,
		1713, 1738, 1773, 1799, 1835, 1862, 1899, 1928, 1966, 1995,
		2035, 2065, 2107, 2181, 2257, 2291, 2336, 2418, 2503, 2541
};

const uint16_t TRX_DCS_TONE = 13440;  // 134.4Hz is the data rate of the DCS bitstream (and a reason not to use that tone for CTCSS)
const uint8_t TRX_NUM_DCS = 83U;

const uint16_t TRX_DCSCodes[] = {
		0x023, 0x025, 0x026, 0x031, 0x032, 0x043, 0x047, 0x051, 0x054, 0x065, 0x071, 0x072, 0x073, 0x074,
		0x114, 0x115, 0x116, 0x125, 0x131, 0x132, 0x134, 0x143, 0x152, 0x155, 0x156, 0x162, 0x165, 0x172, 0x174,
		0x205, 0x223, 0x226, 0x243, 0x244, 0x245, 0x251, 0x261, 0x263, 0x265, 0x271,
		0x306, 0x311, 0x315, 0x331, 0x343, 0x345, 0x351, 0x364, 0x365, 0x371,
		0x411, 0x412, 0x413, 0x423, 0x431, 0x432, 0x445, 0x464, 0x465, 0x466,
		0x503, 0x506, 0x516, 0x532, 0x546, 0x565,
		0x606, 0x612, 0x624, 0x627, 0x631, 0x632, 0x654, 0x662, 0x664,
		0x703, 0x712, 0x723, 0x731, 0x732, 0x734, 0x743, 0x754
};


frequencyBand_t USER_FREQUENCY_BANDS[RADIO_BANDS_TOTAL_NUM] =  {
													{
														.minFreq=14400000,
														.maxFreq=14800000
													},// VHF
													{
														.minFreq=22200000,
														.maxFreq=22500000
													},// 220Mhz
													{
														.minFreq=42000000,
														.maxFreq=45000000
													}// UHF
};

const frequencyBand_t DEFAULT_USER_FREQUENCY_BANDS[RADIO_BANDS_TOTAL_NUM] =  {
													{
														.minFreq=14400000,
														.maxFreq=14800000
													},// VHF
													{
														.minFreq=22200000,
														.maxFreq=22500000
													},// 220Mhz
													{
														.minFreq=42000000,
														.maxFreq=45000000
													}// UHF
};

//const uint32_t RSSI_NOISE_SAMPLE_PERIOD_PIT = 25U;// 25 milliseconds

static volatile ticksTimer_t trxNextRssiNoiseSampleTimer = { 0, 0 };
static volatile ticksTimer_t trxNextSquelchCheckingTimer = { 0, 0 };

static uint8_t trxCssMeasureCount = 0;

static uint8_t currentCC = 1;

#define CTCSS_HOLD_DELAY            6
#define SQUELCH_CLOSE_DELAY         1

#define SIZE_OF_FILL_BUFFER       128 // Tested by Jose EA5SW, and it's needed, 64 makes the beeps and audio to disappear.

static bool rxCSSactive = false;
//static uint8_t rxCSSTriggerCount = 0;
static int trxCurrentDMRTimeSlot;

volatile uint8_t trxTxVox;
volatile uint8_t trxTxMic;

volatile uint16_t txDACDrivePower;
volatile uint8_t analogIGain;
volatile uint8_t analogQGain;
volatile uint8_t digitalIGain;
volatile uint8_t digitalQGain;
volatile int8_t Mod2Offset;

volatile bool trxIsTransmittingDMR;
volatile uint32_t trxDMRstartTime;

static uint8_t voice_gain_tx = 0x31; // default voice_gain_tx fro calibration, needs to be declared here in case calibration:OFF

volatile bool trxTransmissionEnabled = false;
volatile bool trxIsTransmitting = false;
volatile bool txPAEnabled = false;

uint32_t trxTalkGroupOrPcId = 9;// Set to local TG just in case there is some problem with it not being loaded
uint32_t trxDMRID = 0;// Set ID to 0. Not sure if its valid. This value needs to be loaded from the codeplug.


// DTMF Order: 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, A, B, C, D, *, #
const int trxDTMFfreq1[] = { 1336, 1209, 1336, 1477, 1209, 1336, 1477, 1209, 1336, 1477, 1633, 1633, 1633, 1633, 1209, 1477 };
const int trxDTMFfreq2[] = {  941,  697,  697,  697,  770,  770,  770,  852,  852,  852,  697,  770,  852,  941,  941,  941 };

calibrationPowerValues_t trxPowerSettings;

static bool powerUpDownState = true;

static uint8_t trxAnalogFilterLevel = ANALOG_FILTER_CSS;

volatile bool trxDMRSynchronisedRSSIReadPending = false;

static uint8_t trxSaveVoiceGainTx = 0xff;
static uint16_t trxSaveDeviation = 0xff;


static void trxUpdateC6000Calibration(void);
static void trxUpdateRadioCalibration(void);

//
// =================================================================
//

uint8_t trxGetAnalogFilterLevel(void)
{
	return trxAnalogFilterLevel;
}

void trxSetAnalogFilterLevel(uint8_t newFilterLevel)
{
	trxAnalogFilterLevel = newFilterLevel;
}

int trxGetMode(void)
{
	return currentRadioDevice->currentMode;
}

bool trxGetBandwidthIs25kHz(void)
{
	return currentRadioDevice->currentBandWidthIs25kHz;
}

void trxSetModeAndBandwidth(int mode, bool bandwidthIs25kHz)
{
	if (rxPowerSavingIsRxOn() == false)
	{
		rxPowerSavingSetState(ECOPHASE_POWERSAVE_INACTIVE);
	}

	currentRadioDevice->digitalSignalReceived = false;
	currentRadioDevice->analogSignalReceived = false;
	ticksTimerStart((ticksTimer_t *)&trxNextRssiNoiseSampleTimer, RSSI_NOISE_SAMPLE_PERIOD_PIT);
	ticksTimerStart((ticksTimer_t *)&trxNextSquelchCheckingTimer, RSSI_NOISE_SAMPLE_PERIOD_PIT);
	trxCssMeasureCount = 0;

	// DMR (digital) is disabled, hence force it
	// to RADIO_MODE_ANALOG (has we could currently be in RADIO_MODE_NONE (CPS))
	if (uiDataGlobal.dmrDisabled && (mode == RADIO_MODE_DIGITAL))
	{
		mode = RADIO_MODE_ANALOG;
	}

	if ((mode != currentRadioDevice->currentMode) || (bandwidthIs25kHz != currentRadioDevice->currentBandWidthIs25kHz))
	{
		currentRadioDevice->currentMode = mode;

		taskENTER_CRITICAL();
		switch(mode)
		{
			case RADIO_MODE_NONE:// not truly off
				soundTerminateSound();
				HRC6000TerminateDigital();
				radioSetMode(RADIO_MODE_NONE);
				trxUpdateC6000Calibration();
				trxUpdateRadioCalibration();
				break;
			case RADIO_MODE_ANALOG:
				currentRadioDevice->currentBandWidthIs25kHz = bandwidthIs25kHz;
				//radioSetAudioPath(true);							//select the FM audio Path
				HRC6000TerminateDigital();
				radioSetMode(RADIO_MODE_ANALOG);
				trxUpdateC6000Calibration();
				radioSetIF(currentRadioDevice->trxCurrentBand[TRX_RX_FREQ_BAND], currentRadioDevice->currentBandWidthIs25kHz);
				trxUpdateRadioCalibration();
				break;
			case RADIO_MODE_DIGITAL:
				currentRadioDevice->currentBandWidthIs25kHz = BANDWIDTH_12P5KHZ;// DMR bandwidth is 12.5kHz
				radioSetMode(RADIO_MODE_DIGITAL);
				trxUpdateC6000Calibration();
				radioSetIF(currentRadioDevice->trxCurrentBand[TRX_RX_FREQ_BAND], currentRadioDevice->currentBandWidthIs25kHz);
				trxUpdateRadioCalibration();
				HRC6000InitDigital();
				break;
		}
		taskEXIT_CRITICAL();
	}
	else
	{
		switch (mode)
		{
			case RADIO_MODE_ANALOG:
				disableAudioAmp(AUDIO_AMP_MODE_RF);
				break;

			case RADIO_MODE_DIGITAL:
				HRC6000ResetTimeSlotDetection();
				// We need to reset the slot state because some part of the UI
				// are getting stuck, like the green LED and QSO info, while navigating the UI.
				if (slotState != DMR_STATE_IDLE)
				{
					slotState = DMR_STATE_RX_END;
				}
				break;

			case RADIO_MODE_NONE:
				// nop
				break;
		}
	}
}

uint32_t trxGetNextOrPrevBandFromFrequency(uint32_t frequency, bool nextBand)
{
	if (nextBand)
	{
		if (frequency > RADIO_HARDWARE_FREQUENCY_BANDS[RADIO_BANDS_TOTAL_NUM - 1].maxFreq)
		{
			return 0; // First band
		}

		for(uint32_t band = 0; band < RADIO_BANDS_TOTAL_NUM - 1; band++)
		{
			if (frequency > RADIO_HARDWARE_FREQUENCY_BANDS[band].maxFreq && frequency < RADIO_HARDWARE_FREQUENCY_BANDS[band + 1].minFreq)
			{
				return (band + 1); // Next band
			}
		}
	}
	else
	{
		if (frequency < RADIO_HARDWARE_FREQUENCY_BANDS[0].minFreq)
		{
			return (RADIO_BANDS_TOTAL_NUM - 1); // Last band
		}

		for (uint32_t band = 1; band < RADIO_BANDS_TOTAL_NUM; band++)
		{
			if (frequency < RADIO_HARDWARE_FREQUENCY_BANDS[band].minFreq && frequency > RADIO_HARDWARE_FREQUENCY_BANDS[band - 1].maxFreq)
			{
				return (band - 1); // Prev band
			}
		}
	}

	return FREQUENCY_OUT_OF_BAND;
}

uint32_t trxGetBandFromFrequency(uint32_t frequency)
{
	for (uint32_t i = 0; i < RADIO_BANDS_TOTAL_NUM; i++)
	{
		if ((frequency >= RADIO_HARDWARE_FREQUENCY_BANDS[i].minFreq) && (frequency <= RADIO_HARDWARE_FREQUENCY_BANDS[i].maxFreq))
		{
			return i;
		}
	}

	return FREQUENCY_OUT_OF_BAND;
}

bool trxCheckFrequencyInAmateurBand(uint32_t frequency)
{
	if (nonVolatileSettings.txFreqLimited == BAND_LIMITS_FROM_CPS)
	{
		return ((frequency >= USER_FREQUENCY_BANDS[RADIO_BAND_VHF].minFreq) && (frequency <= USER_FREQUENCY_BANDS[RADIO_BAND_VHF].maxFreq)) ||
			((frequency >= USER_FREQUENCY_BANDS[RADIO_BAND_UHF].minFreq) && (frequency <= USER_FREQUENCY_BANDS[RADIO_BAND_UHF].maxFreq));
	}
	else if (nonVolatileSettings.txFreqLimited == BAND_LIMITS_ON_LEGACY_DEFAULT)
	{
		return ((frequency >= DEFAULT_USER_FREQUENCY_BANDS[RADIO_BAND_VHF].minFreq) && (frequency <= DEFAULT_USER_FREQUENCY_BANDS[RADIO_BAND_VHF].maxFreq)) ||
#if !(defined(PLATFORM_MD9600) || defined(PLATFORM_MD380))
			((frequency >= DEFAULT_USER_FREQUENCY_BANDS[RADIO_BAND_220MHz].minFreq) && (frequency <= DEFAULT_USER_FREQUENCY_BANDS[RADIO_BAND_220MHz].maxFreq)) ||
#endif
			((frequency >= DEFAULT_USER_FREQUENCY_BANDS[RADIO_BAND_UHF].minFreq) && (frequency <= DEFAULT_USER_FREQUENCY_BANDS[RADIO_BAND_UHF].maxFreq));
	}

	return true;// Setting must be BAND_LIMITS_NONE
}

void trxReadVoxAndMicStrength(void)
{
	radioReadVoxAndMicStrength();
}

// Need to postpone the next AT1846ReadRSSIAndNoise() call (see trxReadRSSIAndNoise())
// msOverride parameter is used if > 0
void trxPostponeReadRSSIAndNoise(uint32_t msOverride)
{
	ticksTimerStart((ticksTimer_t *)&trxNextRssiNoiseSampleTimer, (msOverride > 0 ? msOverride : RSSI_NOISE_SAMPLE_PERIOD_PIT));
}

// Check RSSI and Noise
void trxReadRSSIAndNoise(bool force)
{
	if (rxPowerSavingIsRxOn() && (ticksTimerHasExpired((ticksTimer_t *)&trxNextRssiNoiseSampleTimer) || force))
	{
		radioReadRSSIAndNoiseForBand(currentRadioDevice->trxCurrentBand[TRX_RX_FREQ_BAND]);
		ticksTimerStart((ticksTimer_t *)&trxNextRssiNoiseSampleTimer, RSSI_NOISE_SAMPLE_PERIOD_PIT);
	}
}

bool trxCarrierDetected(RadioDevice_t deviceId)
{
	TRXDevice_t *radioDevice = &radioDevices[deviceId];// Get pointer to device to make code below more efficient
	uint8_t squelch = 0;

	trxReadRSSIAndNoise(true); // We need to get the RSSI and noise now.

	switch(radioDevice->currentMode)
	{
		case RADIO_MODE_NONE:
			return false;
			break;

		case RADIO_MODE_ANALOG:
			if (currentChannelData->sql != 0)
			{
				squelch = TRX_SQUELCH_MAX - ((currentChannelData->sql - 1) * TRX_SQUELCH_INC);
			}
			else
			{
				squelch = TRX_SQUELCH_MAX - ((nonVolatileSettings.squelchDefaults[radioDevice->trxCurrentBand[TRX_RX_FREQ_BAND]] - 1) * TRX_SQUELCH_INC);
			}
			break;

		case RADIO_MODE_DIGITAL:
			squelch = TRX_SQUELCH_MAX - ((nonVolatileSettings.squelchDefaults[radioDevice->trxCurrentBand[TRX_RX_FREQ_BAND]] - 1) * TRX_SQUELCH_INC);
			break;
	}
	return (radioDevice->trxRxNoise < squelch);
}

bool trxCheckDigitalSquelch(RadioDevice_t deviceId)
{
	TRXDevice_t *radioDevice = &radioDevices[deviceId];// Get pointer to device to make code below more efficient

	if (ticksTimerHasExpired((ticksTimer_t *)&trxNextSquelchCheckingTimer))
	{
		if (radioDevice->currentMode != RADIO_MODE_NONE)
		{
			uint8_t squelch;

			squelch = TRX_SQUELCH_MAX - ((nonVolatileSettings.squelchDefaults[radioDevice->trxCurrentBand[TRX_RX_FREQ_BAND]] - 1) * TRX_SQUELCH_INC);

			if (radioDevice->trxRxNoise < squelch)
			{
				if ((uiDataGlobal.rxBeepState & RX_BEEP_CARRIER_HAS_STARTED) == 0)
				{
					uiDataGlobal.rxBeepState |= (RX_BEEP_CARRIER_HAS_STARTED | RX_BEEP_CARRIER_HAS_STARTED_EXEC);
				}

				if(!radioDevice->digitalSignalReceived)
				{
					radioDevice->digitalSignalReceived = true;
					LedWrite(LED_GREEN, 1);
				}
			}
			else
			{
				if (radioDevice->digitalSignalReceived)
				{
					radioDevice->digitalSignalReceived = false;
					LedWrite(LED_GREEN, 0);
				}

				if (uiDataGlobal.rxBeepState & RX_BEEP_CARRIER_HAS_STARTED)
				{
					uiDataGlobal.rxBeepState = RX_BEEP_CARRIER_HAS_ENDED;
				}
			}
		}

		ticksTimerStart((ticksTimer_t *)&trxNextSquelchCheckingTimer, RSSI_NOISE_SAMPLE_PERIOD_PIT);
	}
	return radioDevice->digitalSignalReceived;
}

void trxTerminateCheckAnalogSquelch(RadioDevice_t deviceId)
{
	disableAudioAmp(AUDIO_AMP_MODE_RF);
	radioDevices[deviceId].analogSignalReceived = false;
	radioDevices[deviceId].analogTriggeredAudio = false;
	trxCssMeasureCount = 0;
}

bool trxCheckAnalogSquelch(void)
{
	if (trxIsTransmitting)
	{
		if (aprsTxProgress != APRS_TX_IN_PROGRESS)
		{
			disableAudioAmp(AUDIO_AMP_MODE_RF);
		}
		currentRadioDevice->analogSignalReceived = false;
		currentRadioDevice->analogTriggeredAudio = false;
		return false;
	}

	if (uiVFOModeSweepScanning(false) || (currentRadioDevice->currentMode == RADIO_MODE_NONE))
	{
		return false;
	}

	trxReadRSSIAndNoise(0);

	if (ticksTimerHasExpired((ticksTimer_t *)&trxNextSquelchCheckingTimer))
	{
		uint8_t squelch;

		// check for variable squelch control
		if (currentChannelData->sql != 0)
		{
			squelch = TRX_SQUELCH_MAX - ((currentChannelData->sql - 1) * TRX_SQUELCH_INC);
		}
		else
		{
			squelch = TRX_SQUELCH_MAX - ((nonVolatileSettings.squelchDefaults[currentRadioDevice->trxCurrentBand[TRX_RX_FREQ_BAND]] - 1) * TRX_SQUELCH_INC);
		}

		if (currentRadioDevice->trxRxNoise < squelch) //noise less than squelch level = signal present.
		{
			if ((currentRadioDevice->analogSignalReceived == false) || (getAudioAmpStatus() & AUDIO_AMP_MODE_RF) == 0)            // open squelch if this is the first occurrence or if the audio amp was turned off by something else.
			{
				currentRadioDevice->analogSignalReceived = true;
				LedWrite(LED_GREEN, 1);

				// FM: Replace Carrier beeps with Talker beeps if Caller beep option is selected.
				if (((nonVolatileSettings.beepOptions & BEEP_RX_CARRIER) == 0) && (nonVolatileSettings.beepOptions & BEEP_RX_TALKER))
				{
					if ((uiDataGlobal.rxBeepState & RX_BEEP_TALKER_HAS_STARTED) == 0)
					{
						uiDataGlobal.rxBeepState |= (RX_BEEP_TALKER_HAS_STARTED | RX_BEEP_TALKER_HAS_STARTED_EXEC);
					}
				}
				else
				{
					if ((uiDataGlobal.rxBeepState & RX_BEEP_CARRIER_HAS_STARTED) == 0)
					{
						uiDataGlobal.rxBeepState |= (RX_BEEP_CARRIER_HAS_STARTED | RX_BEEP_CARRIER_HAS_STARTED_EXEC);
					}
				}

				currentRadioDevice->analogTriggeredAudio = true;
				trxCssMeasureCount = 0;
			}
		}
		if (currentRadioDevice->trxRxNoise > squelch + TRX_SQUELCH_HIST)							//add hysteresis to delay squelch closing. This prevents squelch chattering.
		{
			if (currentRadioDevice->analogSignalReceived || LedRead(LED_GREEN))
			{
				currentRadioDevice->analogSignalReceived = false;
				LedWrite(LED_GREEN, 0);

				// FM: Replace Carrier beeps with Talker beeps if Caller beep option is selected.
				if (((nonVolatileSettings.beepOptions & BEEP_RX_CARRIER) == 0) && (nonVolatileSettings.beepOptions & BEEP_RX_TALKER))
				{
					if (uiDataGlobal.rxBeepState & RX_BEEP_TALKER_HAS_STARTED)
					{
						uiDataGlobal.rxBeepState |= (RX_BEEP_TALKER_HAS_ENDED | RX_BEEP_TALKER_HAS_ENDED_EXEC);
					}
				}
				else
				{
					if (uiDataGlobal.rxBeepState & RX_BEEP_CARRIER_HAS_STARTED)
					{
						uiDataGlobal.rxBeepState = RX_BEEP_CARRIER_HAS_ENDED;
					}
				}

				currentRadioDevice->analogTriggeredAudio = false;
				trxCssMeasureCount = 0;
			}
		}

		bool cssFlag = (rxCSSactive ? trxCheckCSSFlag(currentChannelData->rxTone) : false);

		if (currentRadioDevice->analogSignalReceived)
		{
			if (((getAudioAmpStatus() & AUDIO_AMP_MODE_RF) == 0) && ((rxCSSactive == false) || cssFlag))
			{
				if (currentRadioDevice->analogTriggeredAudio) // Execute that block of code just once after valid signal is received.
				{
					taskENTER_CRITICAL();
					if (!voicePromptsIsPlaying())
					{
						radioSetAudioPath(true);						//Select the FM Audio Path
						enableAudioAmp(AUDIO_AMP_MODE_RF);
						displayLightTrigger(false);
						currentRadioDevice->analogTriggeredAudio = false;
						trxCssMeasureCount = 0;
					}
					taskEXIT_CRITICAL();
				}
			}
			else if (getAudioAmpStatus() & AUDIO_AMP_MODE_RF)
			{
				if (rxCSSactive && (cssFlag == false)) // CSS disappeared.
				{
					trxCssMeasureCount++;
					// If using CTCSS or DCS and signal isn't lost, allow some loss of tone / code.
					// Note:
					//    It's not unusual to have the CSS detection failing (CTCSS, depending of the sub-tone) if
					//    the signal is over-modulated/deviated, so waiting for 150ms is fine, and almost needed.
					//    Waiting for shorter time will just constantly disable and enable the audio Amp.
					if (trxCssMeasureCount >= CTCSS_HOLD_DELAY)
					{
						disableAudioAmp(AUDIO_AMP_MODE_RF);
						currentRadioDevice->analogSignalReceived = false;
						currentRadioDevice->analogTriggeredAudio = false;
						trxCssMeasureCount = 0;
					}
				}
				else
				{
					trxCssMeasureCount = 0;
				}
			}
		}
		else
		{
			if (getAudioAmpStatus() & AUDIO_AMP_MODE_RF)
			{
				trxCssMeasureCount++;
				// If using CTCSS or DCS and signal isn't lost, allow some loss of tone / code
				//
				// NOTE: Currently it is NOT waiting at all.
				//
				if ((rxCSSactive == false) || (trxCssMeasureCount >= SQUELCH_CLOSE_DELAY))
				{
					disableAudioAmp(AUDIO_AMP_MODE_RF);
					trxCssMeasureCount = 0;
				}
			}
		}

		ticksTimerStart((ticksTimer_t *)&trxNextSquelchCheckingTimer, RSSI_NOISE_SAMPLE_PERIOD_PIT);
	}

	return currentRadioDevice->analogSignalReceived;
}

void trxResetSquelchesState(RadioDevice_t deviceId)
{
	radioDevices[deviceId].digitalSignalReceived = false;
	radioDevices[deviceId].analogSignalReceived = false;
}

void trxSetFrequency(uint32_t fRx, uint32_t fTx, int dmrMode)
{
	//
	// Freq could be identical, but not the power of the current channel
	//
	if (currentChannelData->libreDMR_Power != 0x00)
	{
		currentRadioDevice->txPowerLevel = currentChannelData->libreDMR_Power - 1;
	}
	else
	{
		currentRadioDevice->txPowerLevel = nonVolatileSettings.txPowerLevel;
	}

	if (dmrMode == DMR_MODE_AUTO)
	{
		// Most DMR radios determine whether to use Active or Passive DMR depending on whether the Tx and Rx freq are the same
		// This prevents split simplex operation, but since no other radio appears to support split freq simplex
		// Its easier to do things the same way as othe radios, and revisit this again in the future if split freq simplex is required.
		if (fRx == fTx)
		{
			currentRadioDevice->trxDMRModeTx = DMR_MODE_DMO;
			currentRadioDevice->trxDMRModeRx = DMR_MODE_DMO;
		}
		else
		{
			currentRadioDevice->trxDMRModeTx = DMR_MODE_RMO;
			currentRadioDevice->trxDMRModeRx = DMR_MODE_RMO;
		}
	}
	else
	{
		currentRadioDevice->trxDMRModeTx = dmrMode;
		currentRadioDevice->trxDMRModeRx = dmrMode;
	}

	if ((currentRadioDevice->currentRxFrequency != fRx) || (currentRadioDevice->currentTxFrequency != fTx))
	{
		if (rxPowerSavingIsRxOn() == false)
		{
			rxPowerSavingSetState(ECOPHASE_POWERSAVE_INACTIVE);
		}

		taskENTER_CRITICAL();
		currentRadioDevice->trxCurrentBand[TRX_RX_FREQ_BAND] = trxGetBandFromFrequency(fRx);

		currentRadioDevice->currentRxFrequency = fRx;
		currentRadioDevice->currentTxFrequency = fTx;

		if (currentRadioDevice->currentMode == RADIO_MODE_DIGITAL)
		{
			HRC6000TerminateDigital();
		}

		trxUpdateC6000Calibration();
		trxUpdateRadioCalibration();

		radioSetFrequency(currentRadioDevice->currentRxFrequency, false);
		trxSetRX();
		radioSetIF(currentRadioDevice->trxCurrentBand[TRX_RX_FREQ_BAND], currentRadioDevice->currentBandWidthIs25kHz);

		if (currentRadioDevice->currentMode == RADIO_MODE_DIGITAL)
		{
			HRC6000InitDigital();
		}

		ticksTimerStart((ticksTimer_t *)&trxNextRssiNoiseSampleTimer, RSSI_NOISE_SAMPLE_PERIOD_PIT);
		ticksTimerStart((ticksTimer_t *)&trxNextSquelchCheckingTimer, RSSI_NOISE_SAMPLE_PERIOD_PIT);
		taskEXIT_CRITICAL();
	}
}

uint32_t trxGetFrequency(void)
{
	if (trxTransmissionEnabled)
	{
		return currentRadioDevice->currentTxFrequency;
	}

	return currentRadioDevice->currentRxFrequency;
}

void trxSetRX(void)
{
	if (currentRadioDevice->currentMode == RADIO_MODE_ANALOG)
	{
		trxActivateRx(true);
	}
}

void trxConfigurePA_DAC_ForFrequencyBand(void)
{
	currentRadioDevice->trxCurrentBand[TRX_TX_FREQ_BAND] = trxGetBandFromFrequency(currentRadioDevice->currentTxFrequency);
	calibrationGetPowerForFrequency(currentRadioDevice->currentTxFrequency, &trxPowerSettings);
	currentRadioDevice->lastSetTxFrequency = currentRadioDevice->currentTxFrequency;
	currentRadioDevice->lastSetTxPowerLevel = currentRadioDevice->txPowerLevel;

	trxUpdate_PA_DAC_Drive();
}

void trxSetTX(void)
{
	trxConfigurePA_DAC_ForFrequencyBand();

	trxTransmissionEnabled = true;

	if (currentRadioDevice->currentMode == RADIO_MODE_ANALOG)
	{
		trxActivateTx(true);
	}
}

void trxActivateRx(bool critical)
{
	UNUSED_PARAMETER(critical);

	trxIsTransmittingDMR = false;
	radioSetRx(currentRadioDevice->trxCurrentBand[TRX_RX_FREQ_BAND]);
	radioSetFrequency(currentRadioDevice->currentRxFrequency, false);
	radioSetIF(currentRadioDevice->trxCurrentBand[TRX_RX_FREQ_BAND], currentRadioDevice->currentBandWidthIs25kHz);

	trxUpdateC6000Calibration();// This seems to be needed, otherwise after transmission has ended the Rx appears to have a considerable freq offset

	ticksTimerStart((ticksTimer_t *)&trxNextRssiNoiseSampleTimer, RSSI_NOISE_SAMPLE_PERIOD_PIT);
	ticksTimerStart((ticksTimer_t *)&trxNextSquelchCheckingTimer, RSSI_NOISE_SAMPLE_PERIOD_PIT);
}

void trxActivateTx(bool critical)
{
	UNUSED_PARAMETER(critical);

	if (currentRadioDevice->currentMode == RADIO_MODE_NONE)
	{
		return;
	}

	txPAEnabled = true;
	currentRadioDevice->trxRxSignal = 0;
	currentRadioDevice->trxRxNoise = 255;

	radioSetFrequency(currentRadioDevice->currentTxFrequency, true);

	radioSetTx(currentRadioDevice->trxCurrentBand[TRX_TX_FREQ_BAND]);
}

//start of DMR transmission so do a full activation of the transmitter
void trxActivateDMRTx(void)
{
	trxActivateTx(false);
	trxIsTransmittingDMR = true;
}

//Already transmitting so just turn the output signal On or off
void trxFastDMRTx(bool tx)
{
	radioFastTx(tx);
}

void trxSetPowerFromLevel(uint8_t powerLevel)
{
	currentRadioDevice->txPowerLevel = powerLevel;
}

void trxUpdate_PA_DAC_Drive(void)
{
	static const float fractionalPowers[3][4] = {
#if defined(PLATFORM_RT84_DM1701)
		// DM1701 or RT84 which have same RF hardware
		{0.45f, 0.75f, 0.25f, 0.53f},// VHF
		{0.45f, 0.75f, 0.25f, 0.53f},// 220Mhz - ESTIMATED - NOT TESTED PROBABLY NOT CORRECT
		{0.46f, 0.73f, 0.14f, 0.36f},// UHF

#else
	#if defined(PLATFORM_VARIANT_UV380_PLUS_10W)
		// 10W UV380
		{0.58f, 0.83f, 0.21f, 0.45f},// VHF
		{0.58f, 0.83f, 0.21f, 0.45f},// 220Mhz - ESTIMATED - NOT TESTED PROBABLY NOT CORRECT
		{0.55f, 0.75f, 0.17f, 0.43f},// UHF

	#else
		//  5W UV380
		{0.35f, 0.70f, 0.34f, 0.61f},// VHF
		{0.38f, 0.70f, 0.30f, 0.59f},// 220Mhz - ESTIMATED - NOT TESTED PROBABLY NOT CORRECT
		{0.40f, 0.70f, 0.25f, 0.55f},// UHF
	#endif
#endif
	};//fractionalPowers

#if defined(PLATFORM_VARIANT_UV380_PLUS_10W)
	switch(currentRadioDevice->txPowerLevel)
	{
		case 0:// 50mW
			if(trxPowerSettings.veryLowPower > 160)
			{
				txDACDrivePower = trxPowerSettings.veryLowPower - 160 ;		           //50mW power setting using a typical value for low gain radios
			}
			else
			{
				txDACDrivePower = 0 ;		           //min power setting for high gain radios (may still be more than 50mW)
			}
			break;
		case 1:// 250mW
			txDACDrivePower = trxPowerSettings.veryLowPower;
			break;
		case 2:// 500mW
			txDACDrivePower = trxPowerSettings.veryLowPower + ((trxPowerSettings.lowPower - trxPowerSettings.veryLowPower) * fractionalPowers[currentRadioDevice->trxCurrentBand[TRX_TX_FREQ_BAND]][0]);
			break;
		case 3:// 750mW
			txDACDrivePower = trxPowerSettings.veryLowPower + ((trxPowerSettings.lowPower - trxPowerSettings.veryLowPower) * fractionalPowers[currentRadioDevice->trxCurrentBand[TRX_TX_FREQ_BAND]][1]);
			break;
		case 4:// 1W
			txDACDrivePower = trxPowerSettings.lowPower;
			break;
		case 5:// 2W
			txDACDrivePower = trxPowerSettings.lowPower + ((trxPowerSettings.midPower - trxPowerSettings.lowPower) * fractionalPowers[currentRadioDevice->trxCurrentBand[TRX_TX_FREQ_BAND]][2]);//calculate based on mid and low datapoints
			break;
		case 6:// 3W
			txDACDrivePower = trxPowerSettings.lowPower + ((trxPowerSettings.midPower - trxPowerSettings.lowPower) * fractionalPowers[currentRadioDevice->trxCurrentBand[TRX_TX_FREQ_BAND]][3]);//calculate based on mid and low datapoints
			break;
		case 7:// 5W
			txDACDrivePower = trxPowerSettings.midPower;
			break;
		case 8:// 10W
			txDACDrivePower = trxPowerSettings.highPower;
			break;
		case 9:// +W-
			txDACDrivePower = nonVolatileSettings.userPower;
			break;
		default:
			txDACDrivePower = trxPowerSettings.lowPower;
			break;
	}
#else
	switch(currentRadioDevice->txPowerLevel)
	{
		case 0:// 50mW
			if(trxPowerSettings.veryLowPower > 160)
			{
				txDACDrivePower = trxPowerSettings.veryLowPower - 160 ;		           //50mW power setting using a typical value for low gain radios
			}
			else
			{
				txDACDrivePower = 0 ;		           //min power setting for high gain radios (may still be more than 50mW)
			}
			break;
		case 1:// 250mW
			txDACDrivePower = trxPowerSettings.veryLowPower;
			break;
		case 2:// 500mW
			txDACDrivePower = trxPowerSettings.veryLowPower + ((trxPowerSettings.lowPower - trxPowerSettings.veryLowPower) * fractionalPowers[currentRadioDevice->trxCurrentBand[TRX_TX_FREQ_BAND]][0]);
			break;
		case 3:// 750mW
			txDACDrivePower = trxPowerSettings.veryLowPower + ((trxPowerSettings.lowPower - trxPowerSettings.veryLowPower) * fractionalPowers[currentRadioDevice->trxCurrentBand[TRX_TX_FREQ_BAND]][1]);
			break;
		case 4:// 1W
			txDACDrivePower = trxPowerSettings.lowPower;
			break;
		case 5:// 2W
			txDACDrivePower = trxPowerSettings.midPower;// 2W on 5W radios
			break;
		case 6:// 3W
			txDACDrivePower = trxPowerSettings.midPower + ((trxPowerSettings.highPower - trxPowerSettings.midPower) * fractionalPowers[currentRadioDevice->trxCurrentBand[TRX_TX_FREQ_BAND]][2]);//calculate based on high and mid datapoints
			break;
		case 7:// 4W
			txDACDrivePower = trxPowerSettings.midPower + ((trxPowerSettings.highPower - trxPowerSettings.midPower) * fractionalPowers[currentRadioDevice->trxCurrentBand[TRX_TX_FREQ_BAND]][3]);//calculate based on high and mid datapoints
			break;
		case 8:// 5W
			txDACDrivePower = trxPowerSettings.highPower;
			break;
		case 9:// +W-
			txDACDrivePower = nonVolatileSettings.userPower;
			break;
		default:
			txDACDrivePower = trxPowerSettings.lowPower;
			break;
	}
#endif

	if (txDACDrivePower > MAX_PA_DAC_VALUE)
	{
		txDACDrivePower = MAX_PA_DAC_VALUE;
	}
}

uint16_t trxGetPA_DAC_Drive(void)
{
	return txDACDrivePower;
}

uint8_t trxGetPowerLevel(void)
{
	return currentRadioDevice->txPowerLevel;
}

void trxCalcBandAndFrequencyOffset(CalibrationBand_t *calibrationBand, uint32_t *freq_offset)
{
// NOTE. For crossband duplex DMR, the calibration potentially needs to be changed every time the Tx/Rx is switched over on each 30ms cycle
// But at the moment this is an unnecessary complication and I'll just use the Rx frequency to get the calibration offsets

	if (currentRadioDevice->trxCurrentBand[TRX_RX_FREQ_BAND] == RADIO_BAND_UHF)
	{
		*calibrationBand = CalibrationBandUHF;
		*freq_offset = (currentRadioDevice->currentTxFrequency - 40000000) / 1000000;
		if (*freq_offset > 8)
		{
			*freq_offset = 8;
		}
	}
	else
	{
		*calibrationBand = CalibrationBandVHF;
		*freq_offset = (currentRadioDevice->currentTxFrequency - 13600000) / 950000;
		if (*freq_offset > 4)
		{
			*freq_offset = 4;
		}
	}
}

static void trxUpdateC6000Calibration(void)
{
	int8_t cal = calibrationGetMod2Offset(currentRadioDevice->trxCurrentBand[trxTransmissionEnabled ? TRX_TX_FREQ_BAND : TRX_RX_FREQ_BAND]);
	SPI0WritePageRegByte(0x04, 0x47, cal);			// Set the reference tuning offset
	SPI0WritePageRegByte(0x04, 0x48, ((cal < 0) ? 0x03 : 0x00));
	SPI0WritePageRegByte(0x04, 0x04, cal);									//Set MOD 2 Offset (Cal Value)
}

static void trxUpdateRadioCalibration(void)
{
	analogIGain = calibrationGetAnalogIGainForFrequency(currentRadioDevice->currentTxFrequency);
	analogQGain = calibrationGetAnalogQGainForFrequency(currentRadioDevice->currentTxFrequency);
	digitalIGain = calibrationGetDigitalIGainForFrequency(currentRadioDevice->currentTxFrequency);
	digitalQGain = calibrationGetDigitalQGainForFrequency(currentRadioDevice->currentTxFrequency);
	Mod2Offset = calibrationGetMod2Offset(currentRadioDevice->trxCurrentBand[trxTransmissionEnabled ? TRX_TX_FREQ_BAND : TRX_RX_FREQ_BAND]);
}

void trxSetDMRColourCode(uint8_t colourCode)
{
	if (rxPowerSavingIsRxOn() == false)
	{
		rxPowerSavingSetState(ECOPHASE_POWERSAVE_INACTIVE);
	}

	SPI0WritePageRegByte(0x04, 0x1F, (colourCode << 4)); // DMR Colour code in upper 4 bits.
	currentCC = colourCode;
}

uint8_t trxGetDMRColourCode(void)
{
	return currentCC;
}

int trxGetDMRTimeSlot(void)
{
	return trxCurrentDMRTimeSlot;
}

void trxSetDMRTimeSlot(int timeslot, bool resync)
{
	if (rxPowerSavingIsRxOn() == false)
	{
		rxPowerSavingSetState(ECOPHASE_POWERSAVE_INACTIVE);
	}

	trxCurrentDMRTimeSlot = timeslot;

	if (resync)
	{
		HRC6000ResyncTimeSlot();
	}
}

void trxUpdateTsForCurrentChannelWithSpecifiedContact(struct_codeplugContact_t *contactData)
{
	// Contact TS override ?
	if ((nonVolatileSettings.overrideTG == 0) && (contactData->reserve1 & CODEPLUG_CONTACT_FLAG_NO_TS_OVERRIDE) == 0x00)
	{
		if (tsIsContactHasBeenOverriddenFromCurrentChannel())
		{
			trxCurrentDMRTimeSlot = (tsGetManualOverrideFromCurrentChannel() - 1);
		}
		else
		{
			trxCurrentDMRTimeSlot = ((contactData->reserve1 & CODEPLUG_CONTACT_FLAG_TS_OVERRIDE_TIMESLOT_MASK) != 0) ? 1 : 0;
		}
	}
	else
	{
		int8_t overriddenTS = tsGetManualOverrideFromCurrentChannel();

		// No manual override
		if (overriddenTS == 0)
		{
			// Apply channnel TS
			trxCurrentDMRTimeSlot = (codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_TIMESLOT_TWO) != 0) ? 1 : 0;
		}
		else
		{
			// Restore overriden TS (as previous contact may have changed it
			trxCurrentDMRTimeSlot = (overriddenTS - 1);
		}
	}

	HRC6000ResyncTimeSlot();
}

// Codeplug format (hex) -> octal
static uint16_t convertCSSNative2BinaryCodedOctal(uint16_t nativeCSS)
{
	uint16_t octalCSS = 0;
	uint16_t shift = 0;

	while (nativeCSS)
	{
		octalCSS += (nativeCSS & 0xF) << shift;
		nativeCSS >>= 4;
		shift += 3;
	}
	return octalCSS;
}

void trxSetTxCSS(uint16_t tone)
{
	CodeplugCSSTypes_t type = codeplugGetCSSType(tone);

	if (type == CSS_TYPE_NONE)
	{
		radioTxCSSOff();
	}
	else if (type == CSS_TYPE_CTCSS)
	{
		// value that is stored is 100 time the tone freq but its stored in the codeplug as freq times 10
		tone *= 10;
		radioTxCTCSOn(tone);
	}
	else if (type & CSS_TYPE_DCS)
	{
		uint16_t code = convertCSSNative2BinaryCodedOctal(tone & ~CSS_TYPE_DCS_MASK);

		radioTxDCSOn(code, ((type & CSS_TYPE_DCS_INVERTED) != 0));
	}
}

void trxSetRxCSS(RadioDevice_t deviceId, uint16_t tone)
{
	CodeplugCSSTypes_t type = codeplugGetCSSType(tone);

	if (type == CSS_TYPE_NONE)
	{
		radioRxCSSOff(deviceId);
		rxCSSactive = false;
	}
	else if (type == CSS_TYPE_CTCSS)
	{
		// value that is stored is 100 time the tone freq but its stored in the codeplug as freq times 10
		tone *= 10;
		radioRxCTCSOn(deviceId, tone);
		rxCSSactive = (trxAnalogFilterLevel != ANALOG_FILTER_NONE);
		// Force closing the AudioAmp
		disableAudioAmp(AUDIO_AMP_MODE_RF);
		radioDevices[deviceId].analogSignalReceived = false;
		radioDevices[deviceId].analogTriggeredAudio = false;
	}
	else if (type & CSS_TYPE_DCS)
	{
		uint16_t code = convertCSSNative2BinaryCodedOctal(tone & ~CSS_TYPE_DCS_MASK);

		radioRxDCSOn(deviceId, code, ((type & CSS_TYPE_DCS_INVERTED) != 0));
		rxCSSactive = (trxAnalogFilterLevel != ANALOG_FILTER_NONE);
		// Force closing the AudioAmp
		disableAudioAmp(AUDIO_AMP_MODE_RF);
		radioDevices[deviceId].analogSignalReceived = false;
		radioDevices[deviceId].analogTriggeredAudio = false;
	}
}

// Arms the CTCSS/DCS detector for one tone without making it the squelch:
// rxCSSactive and the audio amplifier are left alone, so whatever is being
// received stays audible. For the tone finder, which tries tones one at a
// time and reads trxCheckCSSFlag() after each; trxSetRxCSS() puts the
// channel's own tone back afterwards.
void trxArmRxCSSDetector(uint16_t tone)
{
	CodeplugCSSTypes_t type = codeplugGetCSSType(tone);

	if (type == CSS_TYPE_CTCSS)
	{
		radioRxCTCSOn(RADIO_DEVICE_PRIMARY, (tone * 10)); // the chip wants 100x the frequency, the codeplug keeps 10x
	}
	else if (type & CSS_TYPE_DCS)
	{
		radioRxDCSOn(RADIO_DEVICE_PRIMARY, convertCSSNative2BinaryCodedOctal(tone & ~CSS_TYPE_DCS_MASK), ((type & CSS_TYPE_DCS_INVERTED) != 0));
	}
}

bool trxCheckCSSFlag(uint16_t tone)
{
	CodeplugCSSTypes_t type = codeplugGetCSSType(tone);

	return ((type != CSS_TYPE_NONE) && (radioCheckCSS(tone, type)));
}

uint8_t trxGetCalibrationVoiceGainTx(void)
{
	return voice_gain_tx;
}

void trxSetTone1(int toneFreq)
{
    radioSetTone1(toneFreq);
}

void trxSetDTMF(int code)
{
	if (code < 16)
	{
		HRC6000SetDTMF(code);
	}
}

void trxDTMFoff(bool enableMic)
{
	HRC6000DTMFoff(enableMic);
}

void trxSetMicGainFM(uint8_t gain)
{
	radioSetMicGainFM(gain);
}

void trxEnableTransmission(void)
{
	LedWrite(LED_GREEN, 0);
	LedWrite(LED_RED, 1);
	trxSetTX();
}

void trxDisableTransmission(void)
{
	LedWrite(LED_RED, 0);
	trxActivateRx(true);
}

// Returns true if the HR-C6000 has been powered off
bool trxPowerUpDownRxAndC6000(bool powerUp, bool includeC6000, bool includeMic)
{
	bool status = false;

	// Check the radio is not transmitting.
	if ((powerUp == powerUpDownState) || trxTransmissionEnabled || trxIsTransmitting)
	{
		return false;
	}

	// Force HRC6000 to power cycles in any ECO mode on STM32 platforms, as it seems
	// there are some bad batches that breaks Beep and Audio until the operator enables
	// monitor mode or restarts the radio.
#if defined(STM32F405xx)
	includeC6000 = true;
#endif

	if (powerUp)
	{
		radioPowerOn();
		radioSetBandwidth(currentRadioDevice->currentBandWidthIs25kHz);

		if (includeC6000 && (voicePromptsIsPlaying() == false))
		{
			uint8_t spi_values[SIZE_OF_FILL_BUFFER];

			// Always power up the C6000 even if its may already be powered up, because VP was playing
			HAL_GPIO_WritePin(C6000_PWD_GPIO_Port, C6000_PWD_Pin, GPIO_PIN_RESET); // Power Up the C6000
			// Allow some time to the C6000 to get ready
			vTaskDelay((10 / portTICK_PERIOD_MS));

			HRC6000SetDmrRxGain(0);							//temporarily set the gain to 0. Any less and the buffer flush doesn't seem to work.
			memset(spi_values, 0xAA, SIZE_OF_FILL_BUFFER);
			SPI0ClearPageRegByteWithMask(0x04, 0x06, 0xFD, 0x02); // SET OpenMusic bit (play Boot sound and Call Prompts)
			SPI0WritePageRegByteArray(0x03, 0x00, spi_values, SIZE_OF_FILL_BUFFER);
			SPI0ClearPageRegByteWithMask(0x04, 0x06, 0xFD, 0x00); // CLEAR OpenMusic bit (play Boot sound and Call Prompts)

			SPI0WritePageRegByte(0x04, 0x06, 0x21); // Use SPI vocoder under MCU control

			HRC6000SetDmrRxGain(getVolumeControl());			//restore gain to the volume control setting

			// Needs to reset all the audio (I2S BUS/buffering and sound counters).
			vTaskDelay((10 / portTICK_PERIOD_MS));
			soundInit();
		}

#if 0
		// Enable the IRQ, conditionally.
		if (NVIC_GetEnableIRQ(PORTC_IRQn) == 0)
		{
			NVIC_EnableIRQ(PORTC_IRQn);
		}
#endif
	}
	else
	{
		taskENTER_CRITICAL();

		radioPowerOff(false, includeMic);

#ifdef USE_AT1846S_DEEP_SLEEP
		radioWriteReg2byte(0x30, 0x00, 0x00); // Now enter power down mode
#endif

		if (!voicePromptsIsPlaying())
		{
			if (includeC6000)
			{
				// Ensure the ISR has exited before powering off the chip.
				while (HRC6000IRQHandlerIsRunning());

				HAL_GPIO_WritePin(C6000_PWD_GPIO_Port, C6000_PWD_Pin, GPIO_PIN_SET); // Power Up the C6000
				status = true;
			}
		}
		taskEXIT_CRITICAL();
	}

	powerUpDownState = powerUp;

	return status;
}

void trxInvalidateCurrentFrequency(void)
{
	currentRadioDevice->currentRxFrequency = FREQUENCY_UNSET;
	currentRadioDevice->currentTxFrequency = FREQUENCY_UNSET;
	currentRadioDevice->currentMode = RADIO_MODE_NONE;
}

void trxSelectVoiceChannel(uint8_t channel)
{
	radioSelectVoiceChannel(channel, &trxSaveVoiceGainTx, &trxSaveDeviation);
}

void trxRxAndTxOff(bool critical)
{
	UNUSED_PARAMETER(critical);
}

void trxRxOn(bool critical)
{
	UNUSED_PARAMETER(critical);
}

#if defined(MDUV380_VERSION_2) || defined (MDUV380_VERSION_4) || defined (MDUV380_VERSION_1)
#define VHF_RSSI_OFFSET -135
#define UHF_RSSI_OFFSET -145
#define VHF_RSSI_DIVISOR 2
#define UHF_RSSI_DIVISOR 2
#else
#define VHF_RSSI_OFFSET -155
#define UHF_RSSI_OFFSET -155
#define VHF_RSSI_DIVISOR 1.95
#define UHF_RSSI_DIVISOR 1.95
#endif

int trxGetRSSIdBm(RadioDevice_t deviceId)
{
	int dBm = 0;

	if (radioDevices[deviceId].trxCurrentBand[TRX_RX_FREQ_BAND] == RADIO_BAND_UHF)
	{
		// Use fixed point maths to scale the RSSI value to dBm, based on data from VK4JWT and VK7ZJA
		dBm = -151 + radioDevices[deviceId].trxRxSignal;// Note no the RSSI value on UHF does not need to be scaled like it does on VHF
	}
	else
	{
		// VHF
		// Use fixed point maths to scale the RSSI value to dBm, based on data from VK4JWT and VK7ZJA
		dBm = -164 + ((radioDevices[deviceId].trxRxSignal * 32) / 27);
	}

	return dBm;
}

int trxGetNoisedBm(RadioDevice_t deviceId)
{
	int dBm = 0;

	if (radioDevices[deviceId].trxCurrentBand[TRX_RX_FREQ_BAND] == RADIO_BAND_UHF)
	{
		dBm = -151 + radioDevices[deviceId].trxRxNoise;// Note no the RSSI value on UHF does not need to be scaled like it does on VHF
	}
	else
	{
		// VHF
		dBm = -164 + ((radioDevices[deviceId].trxRxNoise * 32) / 27);
	}

	return dBm;
}

int trxGetSNRMargindBm(RadioDevice_t deviceId)
{
	return (trxGetRSSIdBm(deviceId) - trxGetNoisedBm(deviceId));
}

uint8_t trxGetSignalRaw(RadioDevice_t deviceId)
{
	return radioDevices[deviceId].trxRxSignal;
}

uint8_t trxGetNoiseRaw(RadioDevice_t deviceId)
{
	return radioDevices[deviceId].trxRxNoise;
}
