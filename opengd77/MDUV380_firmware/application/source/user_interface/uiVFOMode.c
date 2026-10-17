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
#include "hardware/HR-C6000.h"
#if defined(PLATFORM_MD9600) || defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
#include "hardware/radioHardwareInterface.h"
#endif
#include "functions/trx.h"
#include "functions/rxPowerSaving.h"
#include "functions/toneFinder.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "utils.h"

typedef enum
{
	VFO_SELECTED_FREQUENCY_INPUT_RX,
	VFO_SELECTED_FREQUENCY_INPUT_TX
} vfoSelectedFrequencyInput_t;

typedef enum
{
	VFO_SCREEN_OPERATION_NORMAL,
	VFO_SCREEN_OPERATION_SCAN,
	VFO_SCREEN_OPERATION_DUAL_SCAN,
	VFO_SCREEN_OPERATION_SWEEP
} vfoScreenOperationMode_t;

typedef enum
{
	SWEEP_SETTING_STEP = 0,
	SWEEP_SETTING_RSSI,
	SWEEP_SETTING_GAIN
} sweepSetting_t;

// internal prototypes
static void handleEvent(uiEvent_t *ev);
static void handleQuickMenuEvent(uiEvent_t *ev);
static void updateQuickMenuScreen(bool isFirstRun);
static void updateFrequency(int frequency, bool announceImmediately);
static void stepFrequency(int increment);
static void toneScan(void);
static void scanning(void);
static void scanInit(void);
static void sweepScanInit(void);
static void sweepScanStep(void);
static void sweepListenTick(void);
static void sweepListenStart(uint32_t freq);
static void sweepListenStop(void);
static void sweepListenDrawInfo(void);
static void updateTrxID(void );
static void setCurrentFreqToScanLimits(void);
static void handleUpKey(uiEvent_t *ev);
static void handleDownKey(uiEvent_t *ev);
static void vfoSweepUpdateSamples(int offset, bool forceRedraw, int bandwidthRescale);
static void setSweepIncDecSetting(sweepSetting_t type, bool increment);
static void vfoSweepDrawSample(int offset);
static void clearNuisance(void);

static vfoSelectedFrequencyInput_t selectedFreq = VFO_SELECTED_FREQUENCY_INPUT_RX;

static const int SCAN_TONE_INTERVAL = 200;//time between each tone for lowest tone. (higher tones take less time.)
static uint8_t scanToneIndex = 0;
static CodeplugCSSTypes_t toneScanType = CSS_TYPE_CTCSS;
static CodeplugCSSTypes_t toneScanCSS = CSS_TYPE_NONE; // Here, CSS_NONE means *ALL* CSS types
static uint16_t prevCSSTone = (CODEPLUG_CSS_TONE_NONE - 1);

static vfoScreenOperationMode_t screenOperationMode[2] = { VFO_SCREEN_OPERATION_NORMAL, VFO_SCREEN_OPERATION_NORMAL };// For VFO A and B

static menuStatus_t menuVFOExitStatus = MENU_STATUS_SUCCESS;
static menuStatus_t menuQuickVFOExitStatus = MENU_STATUS_SUCCESS;

static bool quickmenuNewChannelHandled = false; // Quickmenu new channel confirmation window

static const int VFO_SWEEP_STEP_TIME  = 25;// 25ms

#if defined(PLATFORM_RD5R)
#define VFO_SWEEP_GRAPH_START_Y     8
#define VFO_SWEEP_GRAPH_HEIGHT_Y   30
#elif defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
#define VFO_SWEEP_GRAPH_START_Y    10
#define VFO_SWEEP_GRAPH_HEIGHT_Y   78
#else
#define VFO_SWEEP_GRAPH_START_Y    10
#define VFO_SWEEP_GRAPH_HEIGHT_Y   38
#endif


static uint8_t vfoSweepSamples[VFO_SWEEP_NUM_SAMPLES];
static uint8_t vfoSweepRssiNoiseFloor = VFO_SWEEP_RSSI_NOISE_FLOOR_DEFAULT;
static uint8_t vfoSweepGain = VFO_SWEEP_GAIN_DEFAULT;
static bool vfoSweepSavedBandwidth;

// Sweep "stop & listen": when the spectrum sweep finds a signal it pauses on
// that frequency, opens the audio, and keeps listening until the conversation
// ends (minimum dwell 15 s), then resumes sweeping. Analog FM only.
#define SWEEP_LISTEN_MIN_MS        15000 // minimum time to stay, even for a short transmission
#define SWEEP_LISTEN_MAX_MS        60000 // safety cap so a constant carrier can't trap the sweep
#define SWEEP_LISTEN_HANG_MS       3000  // keep listening this long after the audio drops
#define SWEEP_LISTEN_CONFIRM_MS    1500  // if no audio opens by now, it was a false peak
#define SWEEP_LISTEN_TONE_STEP_MS  180   // per CTCSS candidate (chip needs time to lock onto the tone)
#define SWEEP_LISTEN_INFO_MS       700   // redraw the freq/tone info panel this often
static bool sweepListenActive = false;
static bool sweepListenConfirmed = false;
static ticksTimer_t sweepListenMinTimer;
static ticksTimer_t sweepListenMaxTimer;
static ticksTimer_t sweepListenHangTimer;
static ticksTimer_t sweepListenConfirmTimer;
// Audio-safe CTCSS detection while listening: cycle the chip's CTCSS detector
// through the candidates and read the flag, WITHOUT muting the audio (we never
// call trxSetRxCSS here, so rxCSSactive stays false and the speaker keeps playing).
static uint16_t sweepListenTone = 0;       // detected CTCSS (codeplug fmt, x10), 0 = not found yet
static uint8_t  sweepListenToneIdx = 0;    // CTCSS candidate index under test
static bool     sweepListenToneDone = false; // a full CTCSS pass finished with no match
static ticksTimer_t sweepListenToneTimer;
static ticksTimer_t sweepListenInfoTimer;
const int VFO_SWEEP_SCAN_FREQ_STEP_TABLE[7] 		= {125,250,500,1000,2500,5000,10000};
static uint8_t previousVFONumber = 0xFF; // Keep track of the currently loaded channel data


// Public interface
menuStatus_t uiVFOMode(uiEvent_t *ev, bool isFirstRun)
{
	static uint32_t m = 0, curm = 0;

	if (isFirstRun)
	{
#if ! defined(PLATFORM_GD77S)
		// We're coming back from the lock screen (hence no channel init is needed, at all).
		bool isLockMenu = (menuSystemGetPreviouslyPushedMenuNumber(true) == UI_LOCK_SCREEN);
		if (isLockMenu || lockscreenIsRearming)
		{
			if (isLockMenu)
			{
				menuSystemGetPreviouslyPushedMenuNumber(false); // Clear the previous lock screen trace
			}

			uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
			uiVFOModeUpdateScreen(0);

			if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_SWEEP)
			{
				for(int i = 0; i < VFO_SWEEP_NUM_SAMPLES; i++)
				{
					vfoSweepDrawSample(i);
				}

				displayDrawFastVLine((uiDataGlobal.Scan.sweepSampleIndex) % VFO_SWEEP_NUM_SAMPLES, VFO_SWEEP_GRAPH_START_Y, VFO_SWEEP_GRAPH_HEIGHT_Y, true);// draw solid line in the next location
				displayDrawFastVLine((uiDataGlobal.Scan.sweepSampleIndex + uiDataGlobal.Scan.sweepSampleIndexIncrement) % VFO_SWEEP_NUM_SAMPLES, VFO_SWEEP_GRAPH_START_Y, VFO_SWEEP_GRAPH_HEIGHT_Y, true);// draw solid line in the next location

				displayRenderRows(1, ((8 + VFO_SWEEP_GRAPH_HEIGHT_Y) / 8) + 1);
			}

			return MENU_STATUS_SUCCESS;
		}
#endif

		// Fresh entry into the VFO screen (not a lock-screen resume): start the
		// sweep "skip" list empty, so re-opening the spectrum analyzer never keeps
		// skipping the frequencies that were skipped in a previous session.
		clearNuisance();

		uiDataGlobal.FreqEnter.index = 0;

		uiDataGlobal.isDisplayingQSOData = false;
		uiDataGlobal.reverseRepeaterVFO = false;
		settingsSet(nonVolatileSettings.initialMenuNumber, (uint8_t) UI_VFO_MODE);
		uiDataGlobal.displayQSOStatePrev = QSO_DISPLAY_IDLE;
		currentChannelData = &settingsVFOChannel[nonVolatileSettings.currentVFONumber];
		currentChannelData->libreDMR_Power = 0x00;// Force channel to the Master power

		uiDataGlobal.currentSelectedChannelNumber = CH_DETAILS_VFO_CHANNEL;// This is not a regular channel. Its the special VFO channel!
		uiDataGlobal.displayChannelSettings = false;
		uiDataGlobal.talkaround = false;

#if defined(PLATFORM_MD9600)
		// This could happen if a MK22 codeplug containing 220MHz channel(s) has been flashed.
		if ((trxGetBandFromFrequency(currentChannelData->rxFreq) == FREQUENCY_OUT_OF_BAND) || (trxGetBandFromFrequency(currentChannelData->txFreq) == FREQUENCY_OUT_OF_BAND))
		{
			currentChannelData->rxFreq = OUT_OF_BAND_FALLBACK_FREQUENCY;
			currentChannelData->txFreq = OUT_OF_BAND_FALLBACK_FREQUENCY;
		}
#endif

		radioSetTRxDevice(RADIO_DEVICE_PRIMARY);
		trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, (((currentChannelData->chMode == RADIO_MODE_DIGITAL) && codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_FORCE_DMO)) ? DMR_MODE_DMO : DMR_MODE_AUTO));

#if defined(PLATFORM_MD2017)
		if (true)
		{
			radioSetTRxDevice(RADIO_DEVICE_SECONDARY);
			struct_codeplugChannel_t *secondaryChannel = &settingsVFOChannel[1 - nonVolatileSettings.currentVFONumber];
			radioSetFrequency(secondaryChannel->rxFreq, false);
			trxSetModeAndBandwidth(secondaryChannel->chMode, (codeplugChannelGetFlag(secondaryChannel, CHANNEL_FLAG_BW_25K) != 0));
			radioSetTRxDevice(RADIO_DEVICE_PRIMARY);
		}
#endif

		//Need to load the Rx group if specified even if TG is currently overridden as we may need it later when the left or right button is pressed
		if (currentChannelData->rxGroupList != 0)
		{
			if (currentChannelData->rxGroupList != lastLoadedRxGroup)
			{
				if (codeplugRxGroupGetDataForIndex(currentChannelData->rxGroupList, &currentRxGroupData))
				{
					lastLoadedRxGroup = currentChannelData->rxGroupList;
				}
				else
				{
					lastLoadedRxGroup = -1;
				}
			}
		}
		else
		{
			memset(&currentRxGroupData, 0xFF, sizeof(struct_codeplugRxGroup_t));// If the VFO doesnt have an Rx Group ( TG List) the global var needs to be cleared, otherwise it contains the data from the previous screen e.g. Channel screen
			lastLoadedRxGroup = -1;
		}

		uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;

		lastHeardClearLastID();

		int nextMenu = menuSystemGetPreviouslyPushedMenuNumber(false); // used to determine if this screen has just been loaded after Tx ended (in loadChannelData()))

		uiVFOModeLoadChannelData((((nextMenu == UI_TX_SCREEN) || (nextMenu == UI_LOCK_SCREEN) || (nextMenu == UI_PRIVATE_CALL)) ? false : true));

		if ((uiDataGlobal.displayQSOState == QSO_DISPLAY_CALLER_DATA) && (trxGetMode() == RADIO_MODE_ANALOG))
		{
			uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
		}

		freqEnterReset();
		uiVFOModeUpdateScreen(0);
		settingsSetVFODirty();

		if ((uiDataGlobal.VoicePrompts.inhibitInitial == false) &&
				((uiDataGlobal.Scan.active == false) ||
						(uiDataGlobal.Scan.active && ((uiDataGlobal.Scan.state = SCAN_STATE_SHORT_PAUSED) || (uiDataGlobal.Scan.state = SCAN_STATE_PAUSED)))))
		{
			announceItem(PROMPT_SEQUENCE_CHANNEL_NAME_AND_CONTACT_OR_VFO_FREQ_AND_MODE,
					((nextMenu == UI_TX_SCREEN) || (nextMenu == UI_PRIVATE_CALL)) ? PROMPT_THRESHOLD_NEVER_PLAY_IMMEDIATELY : PROMPT_THRESHOLD_2);
		}

		if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_SCAN)
		{
			// Refresh on every step if scan boundaries is equal to one frequency step.
			uiDataGlobal.Scan.refreshOnEveryStep = ((nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber] - nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber]) <= VFO_FREQ_STEP_TABLE[(currentChannelData->VFOflag5 >> 4)]);
		}

		// Need to do this last, as other things in the screen init, need to know whether the main screen has just changed
		if (uiDataGlobal.VoicePrompts.inhibitInitial)
		{
			uiDataGlobal.VoicePrompts.inhibitInitial = false;
		}

		menuVFOExitStatus = MENU_STATUS_SUCCESS;
	}
	else
	{
		menuVFOExitStatus = MENU_STATUS_SUCCESS;

		if (ev->events == NO_EVENT)
		{
			// We are entering digits, so update the screen as we have a cursor to blink
			if ((uiDataGlobal.FreqEnter.index > 0) && ((ev->time - curm) > 300))
			{
				curm = ev->time;
				uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN; // Redraw will happen just below
			}

			// is there an incoming DMR signal
			if (uiDataGlobal.displayQSOState != QSO_DISPLAY_IDLE)
			{
				uiVFOModeUpdateScreen(0);

				// Force full redraw, due to Notification hidding while sweep scanning
				if (uiVFOModeSweepScanning(true))
				{
					vfoSweepUpdateSamples(0, true, 0);
					displayRender();
				}
			}
			else
			{
				if ((ev->time - m) > RSSI_UPDATE_COUNTER_RELOAD)
				{
					if (rxPowerSavingIsRxOn())
					{
						bool doRendering = true;

						if (uiDataGlobal.Scan.active && (screenOperationMode[nonVolatileSettings.currentVFONumber] != VFO_SCREEN_OPERATION_SWEEP) && (uiDataGlobal.Scan.state == SCAN_STATE_PAUSED))
						{
#if defined(PLATFORM_RD5R)
							displayClearRows(0, 1, false);
#else
							displayClearRows(0, 2, false);
#endif
							uiUtilityRenderHeader(false, false);
						}
						else
						{
							if (uiVFOModeDualWatchIsScanning())
							{
								// Header needs to be updated, if Dual Watch is scanning
								uiUtilityRedrawHeaderOnly(true, false);
								doRendering = false;
							}
							else if (uiVFOModeSweepScanning(true) == false)
							{
								 uiUtilityDrawRSSIBarGraph();
								 uiChannelModeDrawRSSIMeterStrip(); // coloured S-meter + numeric S-value (rows 2-3)
							}
						}

						// Only render the second row which contains the bar graph, if we're not scanning,
						// as there is no need to redraw the rest of the screen
						if (doRendering)
						{
							if (uiNotificationIsVisible())
							{
								displayRender();
							}
							else
							{
								displayRenderRows(((uiDataGlobal.Scan.active && (uiDataGlobal.Scan.state == SCAN_STATE_PAUSED)) ? 0 : 1), 4);
							}
						}
					}

					m = ev->time;
				}

			}

			if (uiDataGlobal.Scan.toneActive)
			{
				toneScan();
			}

#if ! (defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017))
			if (uiDataGlobal.Scan.active)
			{
				if (screenOperationMode[nonVolatileSettings.currentVFONumber] != VFO_SCREEN_OPERATION_SWEEP)
				{
					scanning();
				}
				else
				{
					sweepScanStep();
					sweepListenTick();
				}
			}
#endif
		}
		else
		{
			if (ev->hasEvent)
			{
				// Scanning barrier
				if (uiDataGlobal.Scan.toneActive)
				{
					// Left key (alone) reverse tone scan direction
					if ((ev->events & KEY_EVENT) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0))
					{
						if (
#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380)
								(ev->keys.key == KEY_FRONT_DOWN)
#else
								(ev->keys.key == KEY_LEFT)
#endif
						)
						{
							uiDataGlobal.Scan.direction *= -1;
							keyboardReset();
							return MENU_STATUS_SUCCESS;
						}
					}

#if defined(PLATFORM_RD5R) // virtual ORANGE button will be implemented later, this CPP will be removed then.
					if ((ev->keys.key != 0) && (ev->keys.event & KEY_MOD_UP))
#else
					// PTT key is already handled in main().
					if (((ev->events & BUTTON_EVENT) && BUTTONCHECK_SHORTUP(ev, BUTTON_ORANGE)) ||
							((ev->keys.key != 0) && (ev->keys.event & KEY_MOD_UP)))
#endif
					{
						uiVFOModeStopScanning();
					}

					return MENU_STATUS_SUCCESS;
				}

				handleEvent(ev);
			}
		}

#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
		if (uiDataGlobal.Scan.active)
		{
			if (screenOperationMode[nonVolatileSettings.currentVFONumber] != VFO_SCREEN_OPERATION_SWEEP)
			{
				scanning();
			}
			else
			{
				sweepScanStep();
				sweepListenTick();
			}
		}
#endif
	}
	return menuVFOExitStatus;
}

void uiVFOModeUpdateScreen(int txTimeSecs)
{
	static bool blink = false;
	static uint32_t blinkTime = 0;
	char buffer[SCREEN_LINE_BUFFER_SIZE];

	// We don't want QSO info to be displayed while in Sweep scan, or screen redrawing while Sweep is paused
	if (uiVFOModeSweepScanning(true) && ((uiDataGlobal.displayQSOState >= QSO_DISPLAY_CALLER_DATA) || (uiDataGlobal.Scan.state == SCAN_STATE_PAUSED)))
	{
		uiDataGlobal.displayQSOState = QSO_DISPLAY_IDLE;
		return;
	}

	// Only render the header, then wait for the next run
	// Otherwise the screen could remain blank if TG and PC are == 0
	// since uiDataGlobal.displayQSOState won't be set to QSO_DISPLAY_IDLE
	if ((trxGetMode() == RADIO_MODE_DIGITAL) && (HRC6000GetReceivedTgOrPcId() == 0) &&
			((uiDataGlobal.displayQSOState == QSO_DISPLAY_CALLER_DATA) || (uiDataGlobal.displayQSOState == QSO_DISPLAY_CALLER_DATA_UPDATE)))
	{
		uiUtilityRedrawHeaderOnly(uiVFOModeDualWatchIsScanning(), uiVFOModeSweepScanning(true));
		return;
	}

	// We're currently displaying details or entering scan freq limits, and it shouldn't be overridden by QSO data
	if ((uiDataGlobal.displayChannelSettings ||
			((screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_SCAN) && (uiDataGlobal.FreqEnter.index != 0)))
			&& ((uiDataGlobal.displayQSOState == QSO_DISPLAY_CALLER_DATA) || (uiDataGlobal.displayQSOState == QSO_DISPLAY_CALLER_DATA_UPDATE)))
	{
		// We will not restore the previous QSO Data as a new caller just arose.
		uiDataGlobal.displayQSOStatePrev = QSO_DISPLAY_DEFAULT_SCREEN;
		uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
	}

	displayClearBuf();
	uiUtilityRenderHeader(uiVFOModeDualWatchIsScanning(), uiVFOModeSweepScanning(true));

	switch(uiDataGlobal.displayQSOState)
	{
		case QSO_DISPLAY_DEFAULT_SCREEN:
			lastHeardClearLastID();
			if ((uiDataGlobal.Scan.active &&
					(screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_DUAL_SCAN) && (uiDataGlobal.Scan.state == SCAN_STATE_SCANNING)))
			{
				uiUtilityDisplayFrequency(DISPLAY_Y_POS_RX_FREQ, false, false, settingsVFOChannel[CHANNEL_VFO_A].rxFreq, true, true, 1);
				uiUtilityDisplayFrequency(DISPLAY_Y_POS_TX_FREQ, false, false, settingsVFOChannel[CHANNEL_VFO_B].rxFreq, true, true, 2);
			}
			else
			{
				uiDataGlobal.displayQSOStatePrev = QSO_DISPLAY_DEFAULT_SCREEN;
				uiDataGlobal.isDisplayingQSOData = false;
				uiDataGlobal.receivedPcId = 0x00;

				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					if (screenOperationMode[nonVolatileSettings.currentVFONumber] != VFO_SCREEN_OPERATION_SWEEP)
					{
						if (uiDataGlobal.displayChannelSettings)
						{
							uint32_t PCorTG = ((nonVolatileSettings.overrideTG != 0) ? nonVolatileSettings.overrideTG : codeplugContactGetPackedId(&currentContactData));

							snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%s %u",
									(((PCorTG >> 24) == PC_CALL_FLAG) ? currentLanguage->pc : currentLanguage->tg),
									(PCorTG & 0xFFFFFF));
						}
						else
						{
							if (nonVolatileSettings.overrideTG != 0)
							{
								uiUtilityBuildTgOrPCDisplayName(buffer, SCREEN_LINE_BUFFER_SIZE);
								uiUtilityDisplayInformation(NULL, DISPLAY_INFO_CONTACT_OVERRIDE_FRAME, (trxTransmissionEnabled ? DISPLAY_Y_POS_CONTACT_TX_FRAME : -1));
							}
							else
							{
								codeplugUtilConvertBufToString(currentContactData.name, buffer, 16);
							}
						}

						uiUtilityDisplayInformation(buffer, DISPLAY_INFO_CONTACT, (trxTransmissionEnabled ? DISPLAY_Y_POS_CONTACT_TX : -1));
					}
				}
				else
				{
					// Display some channel settings
					if (uiDataGlobal.displayChannelSettings && (screenOperationMode[nonVolatileSettings.currentVFONumber] != VFO_SCREEN_OPERATION_SWEEP))
					{
						uiUtilityDisplayInformation(NULL, DISPLAY_INFO_CHANNEL_DETAILS, -1);
					}

					if(uiDataGlobal.Scan.toneActive)
					{
						if (toneScanType == CSS_TYPE_CTCSS)
						{
							snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "CTCSS %3d.%dHz", currentChannelData->rxTone / 10, currentChannelData->rxTone % 10);
						}
						else if (toneScanType & CSS_TYPE_DCS)
						{
							dcsPrintf(buffer, SCREEN_LINE_BUFFER_SIZE, "DCS ", currentChannelData->rxTone);
						}
						else
						{
							snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%s", "TONE ERROR");
						}

						uiUtilityDisplayInformation(buffer, DISPLAY_INFO_CONTACT, -1);
					}

				}

				if (uiDataGlobal.FreqEnter.index == 0)
				{
					if (!trxTransmissionEnabled)
					{
						uiUtilityDisplayFrequency(((screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_SWEEP) ? DISPLAY_Y_POS_TX_FREQ : DISPLAY_Y_POS_RX_FREQ),
								false, (selectedFreq == VFO_SELECTED_FREQUENCY_INPUT_RX),
								(uiDataGlobal.reverseRepeaterVFO ? currentChannelData->txFreq : currentChannelData->rxFreq), true,
								(screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_SCAN), 0);
					}
					else
					{
						snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, " %d ", txTimeSecs);
						uiUtilityDisplayInformation(buffer, DISPLAY_INFO_TX_TIMER, -1);
					}

					if (((screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_NORMAL) ||
							(screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_DUAL_SCAN) ||
							(screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_SWEEP)) || trxTransmissionEnabled)
					{
						if (screenOperationMode[nonVolatileSettings.currentVFONumber] != VFO_SCREEN_OPERATION_SWEEP)
						{
							uiUtilityDisplayFrequency(DISPLAY_Y_POS_TX_FREQ, true, (selectedFreq == VFO_SELECTED_FREQUENCY_INPUT_TX || trxTransmissionEnabled),
									(uiDataGlobal.reverseRepeaterVFO ? currentChannelData->rxFreq : currentChannelData->txFreq), true, false, 0);
						}
					}
					else
					{
						// Low/High scanning freqs
						snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%u.%03u", nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber] / 100000, (nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber] - (nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber] / 100000) * 100000)/100);

						displayPrintAt(2, DISPLAY_Y_POS_TX_FREQ, buffer, FONT_SIZE_3);

						snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%u.%03u", nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber] / 100000, (nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber] - (nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber] / 100000) * 100000)/100);

						displayPrintAt(DISPLAY_SIZE_X - ((7 * 8) + 2), DISPLAY_Y_POS_TX_FREQ, buffer, FONT_SIZE_3);
						// Scanning direction arrow
						static const int scanDirArrow[2][6] = {
								{ // Down
										59 + DISPLAY_H_OFFSET, (DISPLAY_Y_POS_TX_FREQ + (FONT_SIZE_3_HEIGHT / 2) - 1),
										67 + DISPLAY_H_OFFSET, (DISPLAY_Y_POS_TX_FREQ + (FONT_SIZE_3_HEIGHT / 2) - (FONT_SIZE_3_HEIGHT / 4) - 1),
										67 + DISPLAY_H_OFFSET, (DISPLAY_Y_POS_TX_FREQ + (FONT_SIZE_3_HEIGHT / 2) + (FONT_SIZE_3_HEIGHT / 4) - 1)
								}, // Up
								{
										59 + DISPLAY_H_OFFSET, (DISPLAY_Y_POS_TX_FREQ + (FONT_SIZE_3_HEIGHT / 2) + (FONT_SIZE_3_HEIGHT / 4) - 1),
										59 + DISPLAY_H_OFFSET, (DISPLAY_Y_POS_TX_FREQ + (FONT_SIZE_3_HEIGHT / 2) - (FONT_SIZE_3_HEIGHT / 4) - 1),
										67 + DISPLAY_H_OFFSET, (DISPLAY_Y_POS_TX_FREQ + (FONT_SIZE_3_HEIGHT / 2) - 1)
								}
						};

						displayFillTriangle(scanDirArrow[(uiDataGlobal.Scan.direction > 0)][0], scanDirArrow[(uiDataGlobal.Scan.direction > 0)][1],
								scanDirArrow[(uiDataGlobal.Scan.direction > 0)][2], scanDirArrow[(uiDataGlobal.Scan.direction > 0)][3],
								scanDirArrow[(uiDataGlobal.Scan.direction > 0)][4], scanDirArrow[(uiDataGlobal.Scan.direction > 0)][5], true);
					}
				}
				else // Entering digits
				{
					int16_t xCursor = -1;
					int16_t yCursor = -1;
					int labelsVOffset =
#if defined(PLATFORM_RD5R)
							4;
#else
							0;
#endif

					if ((screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_NORMAL) ||
							(screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_DUAL_SCAN))
					{
						snprintf(buffer, SCREEN_LINE_BUFFER_SIZE,
#if defined(PLATFORM_RD5R)
								"%c%c%c.%c%c%c%c%c",
#else
								"%c%c%c.%c%c%c%c%c MHz",
#endif
								uiDataGlobal.FreqEnter.digits[0], uiDataGlobal.FreqEnter.digits[1], uiDataGlobal.FreqEnter.digits[2],
								uiDataGlobal.FreqEnter.digits[3], uiDataGlobal.FreqEnter.digits[4], uiDataGlobal.FreqEnter.digits[5], uiDataGlobal.FreqEnter.digits[6], uiDataGlobal.FreqEnter.digits[7]);

						displayPrintCentered((selectedFreq == VFO_SELECTED_FREQUENCY_INPUT_TX) ? DISPLAY_Y_POS_TX_FREQ : DISPLAY_Y_POS_RX_FREQ, buffer, FONT_SIZE_3);

						// Cursor
						if (uiDataGlobal.FreqEnter.index < 8)
						{
							xCursor = ((DISPLAY_SIZE_X - (strlen(buffer) * 8)) >> 1) + ((uiDataGlobal.FreqEnter.index + ((uiDataGlobal.FreqEnter.index > 2) ? 1 : 0)) * 8);
							yCursor = ((selectedFreq == VFO_SELECTED_FREQUENCY_INPUT_TX) ? DISPLAY_Y_POS_TX_FREQ : DISPLAY_Y_POS_RX_FREQ) + (FONT_SIZE_3_HEIGHT - 2);
						}
					}
					else
					{
						uint16_t hiX = DISPLAY_SIZE_X - ((7 * 8) + 2) - (DISPLAY_H_OFFSET / 2);
						displayPrintAt(5 + (DISPLAY_H_OFFSET / 2), DISPLAY_Y_POS_RX_FREQ - labelsVOffset, currentLanguage->low, FONT_SIZE_3);
						displayDrawFastVLine(0 + (DISPLAY_H_OFFSET / 2), DISPLAY_Y_POS_RX_FREQ - labelsVOffset, DISPLAY_SIZE_Y - (DISPLAY_Y_POS_RX_FREQ - labelsVOffset), true);
						displayDrawFastHLine(1 + (DISPLAY_H_OFFSET / 2), DISPLAY_Y_POS_TX_FREQ - (labelsVOffset / 2), 57, true);

						sprintf(buffer, "%c%c%c.%c%c%c", uiDataGlobal.FreqEnter.digits[0], uiDataGlobal.FreqEnter.digits[1], uiDataGlobal.FreqEnter.digits[2],
								uiDataGlobal.FreqEnter.digits[3], uiDataGlobal.FreqEnter.digits[4], uiDataGlobal.FreqEnter.digits[5]);

						displayPrintAt(2 + (DISPLAY_H_OFFSET / 2), DISPLAY_Y_POS_TX_FREQ + (DISPLAY_V_EXTRA_PIXELS / 8), buffer, FONT_SIZE_3);

						displayPrintAt(73 + ((DISPLAY_H_OFFSET / 2) * 3), DISPLAY_Y_POS_RX_FREQ - labelsVOffset, currentLanguage->high, FONT_SIZE_3);
						displayDrawFastVLine(68 + ((DISPLAY_H_OFFSET / 2) * 3), DISPLAY_Y_POS_RX_FREQ - labelsVOffset, DISPLAY_SIZE_Y - (DISPLAY_Y_POS_RX_FREQ - labelsVOffset), true);
						displayDrawFastHLine(69 + ((DISPLAY_H_OFFSET / 2) * 3), DISPLAY_Y_POS_TX_FREQ - (labelsVOffset / 2), 57, true);

						sprintf(buffer, "%c%c%c.%c%c%c", uiDataGlobal.FreqEnter.digits[6], uiDataGlobal.FreqEnter.digits[7], uiDataGlobal.FreqEnter.digits[8],
								uiDataGlobal.FreqEnter.digits[9], uiDataGlobal.FreqEnter.digits[10], uiDataGlobal.FreqEnter.digits[11]);

						displayPrintAt(hiX, DISPLAY_Y_POS_TX_FREQ + (DISPLAY_V_EXTRA_PIXELS / 8), buffer, FONT_SIZE_3);

						// Cursor
						if (uiDataGlobal.FreqEnter.index < FREQ_ENTER_DIGITS_MAX)
						{
							xCursor = ((uiDataGlobal.FreqEnter.index < 6) ? 10 + (DISPLAY_H_OFFSET / 2) : hiX) // X start
										+ (((uiDataGlobal.FreqEnter.index < 6) ? (uiDataGlobal.FreqEnter.index - 1) : (uiDataGlobal.FreqEnter.index - 7)) * 8) // Length
										+ ((uiDataGlobal.FreqEnter.index > 2 ? (uiDataGlobal.FreqEnter.index > 8 ? 2 : 1) : 0) * 8); // MHz/kHz separator(s)

							yCursor = DISPLAY_Y_POS_TX_FREQ + (FONT_SIZE_3_HEIGHT - 2) + (DISPLAY_V_EXTRA_PIXELS / 8);
						}
					}

					if ((xCursor >= 0) && (yCursor >= 0))
					{
						displayDrawFastHLine(xCursor + 1, yCursor, 6, blink);

						if ((ticksGetMillis() - blinkTime) > 500)
						{
							blinkTime = ticksGetMillis();
							blink = !blink;
						}
					}

				}
			}
			uiChannelModeDrawRSSIMeterStrip(); // coloured S-meter + numeric S-value
			uiChannelModeDrawRxDecodeMarkers();
			displayRender();
			break;

		case QSO_DISPLAY_CALLER_DATA:
		case QSO_DISPLAY_CALLER_DATA_UPDATE:
			uiDataGlobal.displayQSOStatePrev = QSO_DISPLAY_CALLER_DATA;
			uiDataGlobal.isDisplayingQSOData = true;
			uiDataGlobal.displayChannelSettings = false;
			uiUtilityRenderQSOData();
			uiChannelModeDrawRSSIMeterStrip(); // no-op in DMR (analog-only guard)
			uiChannelModeDrawRxDecodeMarkers();
			displayRender();
			break;

		case QSO_DISPLAY_IDLE:
			break;
	}

	uiDataGlobal.displayQSOState = QSO_DISPLAY_IDLE;
}

bool uiVFOModeIsTXFocused(void)
{
	return (selectedFreq == VFO_SELECTED_FREQUENCY_INPUT_TX);
}

void uiVFOModeStopScanning(void)
{
	bool resetAPRS = true;

	if (uiDataGlobal.Scan.toneActive)
	{
		if (prevCSSTone != (CODEPLUG_CSS_TONE_NONE - 1))
		{
			currentChannelData->rxTone = prevCSSTone;
			prevCSSTone = (CODEPLUG_CSS_TONE_NONE - 1);
		}

		trxSetRxCSS(RADIO_DEVICE_PRIMARY, currentChannelData->rxTone);
		uiDataGlobal.Scan.toneActive = false;
		trxSetAnalogFilterLevel(nonVolatileSettings.analogFilterLevel);// Restore the filter setting after the tone scan
		resetAPRS = false;
	}

	uiDataGlobal.Scan.active = false;
	uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;

	if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_DUAL_SCAN)
	{
		screenOperationMode[CHANNEL_VFO_A] = screenOperationMode[CHANNEL_VFO_B] = VFO_SCREEN_OPERATION_NORMAL;
		settingsSet(nonVolatileSettings.currentVFONumber, nonVolatileSettings.currentVFONumber);

		rxPowerSavingSetLevel(nonVolatileSettings.ecoLevel);// Level is reduced by 1 when Dual Watch , so re-instate it back to the correct setting
	}
	else if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_SWEEP)
	{
		screenOperationMode[nonVolatileSettings.currentVFONumber] = VFO_SCREEN_OPERATION_NORMAL;
		trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, (((currentChannelData->chMode == RADIO_MODE_DIGITAL) && codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_FORCE_DMO)) ? DMR_MODE_DMO : DMR_MODE_AUTO));
		HRC6000ClearColorCodeSynchronisation();
	}

	announceItem(PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ, PROMPT_THRESHOLD_3);
	uiVFOModeUpdateScreen(0); // Needs to redraw the screen now

	if (resetAPRS)
	{
		aprsBeaconingResetTimers();
	}
}

static void updateFrequency(int frequency, bool announceImmediately)
{
	if (selectedFreq == VFO_SELECTED_FREQUENCY_INPUT_TX)
	{
		if (trxGetBandFromFrequency(frequency) != FREQUENCY_OUT_OF_BAND)
		{
			currentChannelData->txFreq = frequency;
			trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, (((currentChannelData->chMode == RADIO_MODE_DIGITAL) && codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_FORCE_DMO)) ? DMR_MODE_DMO : DMR_MODE_AUTO));
			soundSetMelody(MELODY_ACK_BEEP);
		}
	}
	else
	{
		int deltaFrequency = frequency - currentChannelData->rxFreq;
		if (trxGetBandFromFrequency(frequency) != FREQUENCY_OUT_OF_BAND)
		{
			currentChannelData->rxFreq = frequency;
			currentChannelData->txFreq = currentChannelData->txFreq + deltaFrequency;
			trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, (((currentChannelData->chMode == RADIO_MODE_DIGITAL) && codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_FORCE_DMO)) ? DMR_MODE_DMO : DMR_MODE_AUTO));

			if (trxGetBandFromFrequency(currentChannelData->txFreq) != FREQUENCY_OUT_OF_BAND)
			{
				soundSetMelody(MELODY_ACK_BEEP);
			}
			else
			{
				currentChannelData->txFreq = frequency;
				soundSetMelody(MELODY_ERROR_BEEP);
			}
		}
		else
		{
			soundSetMelody(MELODY_ERROR_BEEP);
		}
	}
	announceItem(PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ, announceImmediately);

	menuPrivateCallClear();
	settingsSetVFODirty();
}

void uiVFOModeLoadChannelData(bool forceAPRSReset)
{
	trxSetModeAndBandwidth(currentChannelData->chMode, (codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_BW_25K) != 0));

	if (currentChannelData->chMode == RADIO_MODE_ANALOG)
	{
		if (currentChannelData->aprsConfigIndex != 0)
		{
			currentChannelData->rxTone = CODEPLUG_CSS_TONE_NONE;
			currentChannelData->txTone = CODEPLUG_CSS_TONE_NONE;
			codeplugChannelSetFlag(currentChannelData, CHANNEL_FLAG_VOX, 0);
		}

		if (!uiDataGlobal.Scan.toneActive)
		{
			trxSetRxCSS(RADIO_DEVICE_PRIMARY, currentChannelData->rxTone);
		}

		if (uiDataGlobal.Scan.active == false)
		{
			uiDataGlobal.Scan.state = SCAN_STATE_SCANNING;
		}
	}
	else
	{
		uint32_t channelDMRId = codeplugChannelGetOptionalDMRID(currentChannelData);

		if (uiDataGlobal.manualOverrideDMRId == 0)
		{
			if (channelDMRId == 0)
			{
				trxDMRID = uiDataGlobal.userDMRId;
			}
			else
			{
				trxDMRID = channelDMRId;
			}
		}
		else
		{
			trxDMRID = uiDataGlobal.manualOverrideDMRId;
		}

		// Set CC when:
		//  - scanning
		//  - CC Filter is ON
		//  - CC Filter is OFF but not held anymore or loading a new channel (this avoids restoring Channel's CC when releasing the PTT key, or getting out of menus)
		if (uiDataGlobal.Scan.active ||
				((nonVolatileSettings.dmrCcTsFilter & DMR_CC_FILTER_PATTERN) ||
						(((nonVolatileSettings.dmrCcTsFilter & DMR_CC_FILTER_PATTERN) == 0)
								&& ((HRC6000CCIsHeld() == false) || (previousVFONumber != nonVolatileSettings.currentVFONumber)))))
		{
			trxSetDMRColourCode(currentChannelData->txColor);
			HRC6000ClearColorCodeSynchronisation();
		}

		if (nonVolatileSettings.overrideTG == 0)
		{
			uiVFOLoadContact(&currentContactData);

			// Check whether the contact data seems valid
			if ((currentContactData.name[0] == 0) || (currentContactData.tgNumber == 0) || (currentContactData.tgNumber > 9999999))
			{
				settingsSet(nonVolatileSettings.overrideTG, 9);// If the VFO does not have an Rx Group list assigned to it. We can't get a TG from the codeplug. So use TG 9.
				trxTalkGroupOrPcId = nonVolatileSettings.overrideTG;
				trxSetDMRTimeSlot(codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_TIMESLOT_TWO), true);
				tsSetContactHasBeenOverriden(((Channel_t)nonVolatileSettings.currentVFONumber), false);
			}
			else
			{
				trxTalkGroupOrPcId = codeplugContactGetPackedId(&currentContactData);
				trxUpdateTsForCurrentChannelWithSpecifiedContact(&currentContactData);
			}
		}
		else
		{
			int manTS = tsGetManualOverrideFromCurrentChannel();

			trxTalkGroupOrPcId = nonVolatileSettings.overrideTG;
			trxSetDMRTimeSlot((manTS ? (manTS - 1) : codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_TIMESLOT_TWO)), true);
		}
	}

	if (uiDataGlobal.Scan.active == false)
	{
		if (forceAPRSReset || ((previousVFONumber != nonVolatileSettings.currentVFONumber) && (currentChannelData->chMode == RADIO_MODE_ANALOG)))
		{
			aprsBeaconingResetTimers();
		}
	}

	previousVFONumber = nonVolatileSettings.currentVFONumber;
}

static void checkAndFixIndexInRxGroup(void)
{
	if ((currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup > 0) &&
			(nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber] > (currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup - 1)))
	{
		settingsSet(nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber], 0);
	}
}

void uiVFOLoadContact(struct_codeplugContact_t *contact)
{
	// Check if this channel has an Rx Group
	if ((currentRxGroupData.name[0] != 0) && (nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber] < currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup))
	{
		codeplugContactGetDataForIndex(currentRxGroupData.contacts[nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber]], contact);
	}
	else
	{
		/* 2020.10.27 vk3kyy. The Contact should not be forced to none just because the Rx group list is none
		if (currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup == 0)
		{
			currentChannelData->contact = 0;
		}*/

		codeplugContactGetDataForIndex(currentChannelData->contact, contact);
	}
}

static void toggleAnalogBandwidth(void)
{
	uint8_t bw25k = codeplugChannelSetFlag(currentChannelData, CHANNEL_FLAG_BW_25K, !(codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_BW_25K)));

	if (bw25k)
	{
		nextKeyBeepMelody = (int16_t *)MELODY_KEY_BEEP_FIRST_ITEM;
	}

	// ToDo announce VP for bandwidth perhaps
	trxSetModeAndBandwidth(RADIO_MODE_ANALOG, (bw25k != 0));
}

static void handleEvent(uiEvent_t *ev)
{
	// Sweep "stop & listen": while paused on a found signal, STAR skips that
	// frequency (remembers it) and resumes the sweep - e.g. to skip a repeater's
	// output frequency and keep scanning for the caller's input frequency.
	if (sweepListenActive && KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
	{
		uiDataGlobal.Scan.nuisanceDelete[uiDataGlobal.Scan.nuisanceDeleteIndex] = (int)uiDataGlobal.Scan.scanSweepCurrentFreq;
		uiDataGlobal.Scan.nuisanceDeleteIndex = (uiDataGlobal.Scan.nuisanceDeleteIndex + 1) % MAX_ZONE_SCAN_NUISANCE_CHANNELS;
		sweepListenStop();
		keyboardReset();
		return;
	}

	if (uiDataGlobal.Scan.active && (ev->events & KEY_EVENT))
	{
		if (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0)
		{
			// Right key sets the current frequency as a 'nuisance' frequency.
			if((uiDataGlobal.Scan.state == SCAN_STATE_PAUSED) &&
#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380)
					(ev->keys.key == KEY_STAR)
#else
					(ev->keys.key == KEY_RIGHT)
#endif
					&&
					(screenOperationMode[nonVolatileSettings.currentVFONumber] != VFO_SCREEN_OPERATION_DUAL_SCAN)
					&& (screenOperationMode[nonVolatileSettings.currentVFONumber] != VFO_SCREEN_OPERATION_SWEEP))
			{
				uiDataGlobal.Scan.nuisanceDelete[uiDataGlobal.Scan.nuisanceDeleteIndex] = currentChannelData->rxFreq;
				uiDataGlobal.Scan.nuisanceDeleteIndex = (uiDataGlobal.Scan.nuisanceDeleteIndex + 1) % MAX_ZONE_SCAN_NUISANCE_CHANNELS;
				uiDataGlobal.Scan.timer.timeout = SCAN_SKIP_CHANNEL_INTERVAL;//force scan to continue;
				uiDataGlobal.Scan.state = SCAN_STATE_SCANNING;
				keyboardReset();
				return;
			}

			// Left key reverses the scan direction
			if (
#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380)
					(ev->keys.key == KEY_FRONT_DOWN)
#else
					(ev->keys.key == KEY_LEFT)
#endif
					&&
					(screenOperationMode[nonVolatileSettings.currentVFONumber] != VFO_SCREEN_OPERATION_DUAL_SCAN)
					&& (screenOperationMode[nonVolatileSettings.currentVFONumber] != VFO_SCREEN_OPERATION_SWEEP))
			{
				uiDataGlobal.Scan.direction *= -1;
				keyboardReset();
				return;
			}
		}

		// Stop the scan on any key except UP/ROTARY_INC without SK2 (allows scan to be manually continued)
		// or SK2 on its own (allows Backlight to be triggered)
		if (
#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
				(((
						(ev->keys.key == KEY_FRONT_UP)
#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
						|| (ev->keys.key == KEY_ROTARY_INCREMENT)
#endif
					) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0) &&
						(screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_SCAN)) == false)
				&&
				((((ev->keys.key == KEY_ROTARY_INCREMENT) || (ev->keys.key == KEY_ROTARY_DECREMENT) || (ev->keys.key == KEY_FRONT_UP) || (ev->keys.key == KEY_FRONT_DOWN)
#if defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
						|| (ev->keys.key == KEY_LEFT) || (ev->keys.key == KEY_RIGHT) || (ev->keys.key == KEY_UP) || (ev->keys.key == KEY_DOWN)
#endif
						|| (ev->keys.key == KEY_STAR)) &&
						(screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_SWEEP)) == false)

#else // NOT STM32 PLATFORMS, a.k.a MK22 HTs, and MD9600
				(((ev->keys.key == KEY_UP) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0) &&
						(screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_SCAN)) == false)
				&&
						((((ev->keys.key == KEY_LEFT) || (ev->keys.key == KEY_RIGHT) || (ev->keys.key == KEY_UP) || (ev->keys.key == KEY_DOWN) || (ev->keys.key == KEY_STAR)) &&
								(screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_SWEEP)) == false)
#endif
		)
		{
			uiVFOModeStopScanning();
			keyboardReset();
			announceItem(PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ, PROMPT_THRESHOLD_3);
			return;
		}
	}

	if (ev->events & FUNCTION_EVENT)
	{
		if (ev->function == FUNC_START_SCANNING)
		{
			scanInit();
			setCurrentFreqToScanLimits();
			uiDataGlobal.Scan.active = true;
			return;
		}
		else if (ev->function == FUNC_REDRAW)
		{
			uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
			uiVFOModeUpdateScreen(0);
			return;
		}
	}

	if (handleMonitorMode(ev))
	{
		uiDataGlobal.displayChannelSettings = false;
		uiDataGlobal.reverseRepeaterVFO = false;
		return;
	}

	if (ev->events & BUTTON_EVENT)
	{

#if ! defined(PLATFORM_RD5R)
		// Stop the scan if any button is pressed.
		if (uiDataGlobal.Scan.active && BUTTONCHECK_DOWN(ev, BUTTON_ORANGE))
		{
			uiVFOModeStopScanning();
			return;
		}
#endif

		if (rebuildVoicePromptOnExtraLongSK1(ev))
		{
			return;
		}

		if (repeatVoicePromptOnSK1(ev))
		{
			return;
		}

		uint32_t tg = (LinkHead->talkGroupOrPcId & 0xFFFFFF);

		// If Blue button is pressed during reception it sets the Tx TG to the incoming TG
		if (uiDataGlobal.isDisplayingQSOData && BUTTONCHECK_SHORTUP(ev, BUTTON_SK2) && (trxGetMode() == RADIO_MODE_DIGITAL) &&
				((trxTalkGroupOrPcId != tg) ||
						((dmrMonitorCapturedTS != -1) && (dmrMonitorCapturedTS != trxGetDMRTimeSlot())) ||
						(trxGetDMRColourCode() != currentChannelData->txColor)))
		{
			lastHeardClearLastID();

			// Set TS to overriden TS
			if ((dmrMonitorCapturedTS != -1) && (dmrMonitorCapturedTS != trxGetDMRTimeSlot()))
			{
				trxSetDMRTimeSlot(dmrMonitorCapturedTS, false);
				tsSetManualOverride(((Channel_t)nonVolatileSettings.currentVFONumber), (dmrMonitorCapturedTS + 1));
			}

			if (trxTalkGroupOrPcId != tg)
			{
				trxTalkGroupOrPcId = tg;
				settingsSet(nonVolatileSettings.overrideTG, trxTalkGroupOrPcId);
			}

			currentChannelData->txColor = trxGetDMRColourCode();// Set the CC to the current CC, which may have been determined by the CC finding algorithm in C6000.c

			announceItem(PROMPT_SEQUENCE_CONTACT_TG_OR_PC,PROMPT_THRESHOLD_NEVER_PLAY_IMMEDIATELY);

			uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
			uiVFOModeUpdateScreen(0);
			uiDataGlobal.displayQSOState = QSO_DISPLAY_CALLER_DATA_UPDATE;
			soundSetMelody(MELODY_ACK_BEEP);
			return;
		}

		if ((uiVFOModeSweepScanning(true) == false) && (monitorModeData.isEnabled == false) && (uiDataGlobal.reverseRepeaterVFO == false) && (BUTTONCHECK_DOWN(ev, BUTTON_SK1) && BUTTONCHECK_DOWN(ev, BUTTON_SK2)))
		{
			int prevQSODisp = -1;

			trxSetFrequency(currentChannelData->txFreq, currentChannelData->rxFreq, DMR_MODE_DMO);// Swap Tx and Rx freqs but force DMR Active mode
			uiDataGlobal.reverseRepeaterVFO = true;
			uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;

			if (uiDataGlobal.displayChannelSettings == false)
			{
				prevQSODisp = uiDataGlobal.displayQSOStatePrev;
				uiDataGlobal.displayChannelSettings = true;
			}

			uiVFOModeUpdateScreen(0);

			if (prevQSODisp != -1)
			{
				uiDataGlobal.displayQSOStatePrev = prevQSODisp;
			}
			return;
		}
		else if ((uiDataGlobal.reverseRepeaterVFO == true) && ((BUTTONCHECK_DOWN(ev, BUTTON_SK1) == 0) || (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0)))
		{
			trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, (((currentChannelData->chMode == RADIO_MODE_DIGITAL) && codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_FORCE_DMO)) ? DMR_MODE_DMO : DMR_MODE_AUTO));
			uiDataGlobal.reverseRepeaterVFO = false;

			// We are still displaying channel details (SK1 has been released), force to update the screen
			if (uiDataGlobal.displayChannelSettings && (BUTTONCHECK_DOWN(ev, BUTTON_SK1) == 0))
			{
				uiDataGlobal.displayChannelSettings = false;
			}

			uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
			uiVFOModeUpdateScreen(0);
			return;
		}
		// Display channel settings (CTCSS, Squelch) while SK1 is pressed
		else if ((uiVFOModeSweepScanning(true) == false) && (monitorModeData.isEnabled == false) && (uiDataGlobal.displayChannelSettings == false) && BUTTONCHECK_DOWN(ev, BUTTON_SK1))
		{
			int prevQSODisp = uiDataGlobal.displayQSOStatePrev;

			uiDataGlobal.displayChannelSettings = true;
			uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
			uiVFOModeUpdateScreen(0);
			uiDataGlobal.displayQSOStatePrev = prevQSODisp;
			return;
		}
		else if ((uiDataGlobal.displayChannelSettings == true) && BUTTONCHECK_DOWN(ev, BUTTON_SK1) == 0)
		{
			uiDataGlobal.displayChannelSettings = false;
			uiDataGlobal.displayQSOState = uiDataGlobal.displayQSOStatePrev;

			// Maybe QSO State has been overridden, double check if we could now
			// display QSO Data
			if (uiDataGlobal.displayQSOState == QSO_DISPLAY_DEFAULT_SCREEN)
			{
				if (isQSODataAvailableForCurrentTalker())
				{
					uiDataGlobal.displayQSOState = QSO_DISPLAY_CALLER_DATA;
				}
			}

			// Leaving Channel Details disable reverse repeater feature
			if (uiDataGlobal.reverseRepeaterVFO)
			{
				trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, (((currentChannelData->chMode == RADIO_MODE_DIGITAL) && codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_FORCE_DMO)) ? DMR_MODE_DMO : DMR_MODE_AUTO));
				uiDataGlobal.reverseRepeaterVFO = false;
			}

			uiVFOModeUpdateScreen(0);
			return;
		}

#if !defined(PLATFORM_RD5R)
		if (BUTTONCHECK_SHORTUP(ev, BUTTON_ORANGE))
		{
			if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				announceItem(PROMPT_SEQUENCE_BATTERY, AUDIO_PROMPT_MODE_VOICE_LEVEL_1);
			}
			else
			{
				menuSystemPushNewMenu(UI_VFO_QUICK_MENU);

				// Trick to beep (AudioAssist), since ORANGE button doesn't produce any beep event
				ev->keys.event |= KEY_MOD_UP;
				ev->keys.key = 127;
				menuVFOExitStatus |= (MENU_STATUS_LIST_TYPE | MENU_STATUS_FORCE_FIRST);
				// End Trick
			}

			return;
		}
#endif
	}

	if (ev->events & KEY_EVENT)
	{
		int keyval = 99;

		if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				menuSystemPushNewMenu(MENU_CHANNEL_DETAILS);
				freqEnterReset();
				return;
			}
			else
			{
				if (uiDataGlobal.FreqEnter.index == 0)
				{
					menuSystemPushNewMenu(MENU_MAIN_MENU);
					return;
				}
			}
		}

		if (uiDataGlobal.FreqEnter.index == 0)
		{
#if defined(PLATFORM_MD9600)
			if (KEYCHECK_LONGDOWN(ev->keys, KEY_GREEN))
			{
				if (uiDataGlobal.Scan.active)
				{
					uiVFOModeStopScanning();
				}

				menuSystemPushNewMenu(UI_VFO_QUICK_MENU);

				// Trick to beep (AudioAssist), since ORANGE button doesn't produce any beep event
				ev->keys.event |= KEY_MOD_UP;
				ev->keys.key = 127;
				menuVFOExitStatus |= (MENU_STATUS_LIST_TYPE | MENU_STATUS_FORCE_FIRST);
				// End Trick
				return;
			}
			else
#endif
			if (KEYCHECK_LONGDOWN(ev->keys, KEY_HASH) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_HASH) == false))
			{
				if (uiDataGlobal.Scan.active && (screenOperationMode[nonVolatileSettings.currentVFONumber] != VFO_SCREEN_OPERATION_SWEEP))
				{
					uiVFOModeStopScanning();
				}

				if (screenOperationMode[nonVolatileSettings.currentVFONumber] != VFO_SCREEN_OPERATION_SWEEP)
				{
					sweepScanInit();
					soundSetMelody(MELODY_KEY_LONG_BEEP);
				}
				return;
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
			{
				if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
				{
					menuSystemPushNewMenu(MENU_CONTACT_QUICKLIST);
				}
				else
				{
					menuSystemPushNewMenu(MENU_NUMERICAL_ENTRY);
				}
				return;
			}
			else if (KEYCHECK_LONGDOWN(ev->keys, KEY_0) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_0) == false)
						&& (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0) && (trxGetMode() == RADIO_MODE_ANALOG))
			{
				// Long-press 0 (analog FM only): one-shot 5-Tone selective call screen.
				menuSystemPushNewMenu(UI_SELECTIVE_CALL_5TONE);
				return;
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
			{
				if (uiVFOModeSweepScanning(true) == false)
				{
					if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
					{
						if (trxGetMode() == RADIO_MODE_ANALOG)
						{
							currentChannelData->chMode = RADIO_MODE_DIGITAL;
							checkAndFixIndexInRxGroup();
							uiVFOModeLoadChannelData(true);
							updateTrxID();

							menuVFOExitStatus |= MENU_STATUS_FORCE_FIRST;
						}
						else
						{
							currentChannelData->chMode = RADIO_MODE_ANALOG;
							trxSetModeAndBandwidth(currentChannelData->chMode, (codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_BW_25K) != 0));
						}

						announceItem(PROMPT_SEQUENCE_MODE, PROMPT_THRESHOLD_1);
						uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
					}
					else
					{
						if (trxGetMode() == RADIO_MODE_DIGITAL)
						{
							// Toggle TimeSlot
							trxSetDMRTimeSlot(1 - trxGetDMRTimeSlot(), true);
							tsSetManualOverride(((Channel_t)nonVolatileSettings.currentVFONumber), (trxGetDMRTimeSlot() + 1));

							if ((nonVolatileSettings.overrideTG == 0) && (currentContactData.reserve1 & CODEPLUG_CONTACT_FLAG_NO_TS_OVERRIDE) == 0x00)
							{
								tsSetContactHasBeenOverriden(((Channel_t)nonVolatileSettings.currentVFONumber), true);
							}

							disableAudioAmp(AUDIO_AMP_MODE_RF);
							lastHeardClearLastID();
							uiDataGlobal.displayQSOState = uiDataGlobal.displayQSOStatePrev;
							uiVFOModeUpdateScreen(0);

							if (trxGetDMRTimeSlot() == 0)
							{
								menuVFOExitStatus |= MENU_STATUS_FORCE_FIRST;
							}
							announceItem(PROMPT_SEQUENCE_TS,PROMPT_THRESHOLD_3);
						}
						else
						{
							// STAR on analog opens the Morse sender, matching the
							// channel screen. Bandwidth is still on SK2+STAR below
							// and in the quick menu.
							menuSystemPushNewMenu(UI_MORSE);
						}
					}
				}
				else
				{
					// Sweep scanning is running - keep the stock bandwidth toggle
					// here. Pushing a menu mid-sweep would tear down the scan.
					if (trxGetMode() == RADIO_MODE_ANALOG)
					{
						toggleAnalogBandwidth();
					}
				}
			}
			else if ((uiVFOModeSweepScanning(true) == false) && KEYCHECK_LONGDOWN(ev->keys, KEY_STAR))
			{
				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					tsSetManualOverride(((Channel_t)nonVolatileSettings.currentVFONumber), TS_NO_OVERRIDE);
					tsSetContactHasBeenOverriden(((Channel_t)nonVolatileSettings.currentVFONumber), false);

					// Check if this channel has an Rx Group
					if ((currentRxGroupData.name[0] != 0) &&
							(nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber] < currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup))
					{
						codeplugContactGetDataForIndex(currentRxGroupData.contacts[nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber]], &currentContactData);
					}
					else
					{
						codeplugContactGetDataForIndex(currentChannelData->contact, &currentContactData);
					}

					trxUpdateTsForCurrentChannelWithSpecifiedContact(&currentContactData);

					lastHeardClearLastID();
					uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
					uiVFOModeUpdateScreen(0);
					announceItem(PROMPT_SEQUENCE_TS, PROMPT_THRESHOLD_1);
				}
				else
				{
					// Long STAR on an analog VFO opens the fox-hunt helper.
					menuSystemPushNewMenu(UI_FOX_HUNT);
				}
			}
			else if(uiVFOModeSweepScanning(true) &&  // Reset Sweep noise floor or Sweep Gain
					((KEYCHECK_SHORTUP(ev->keys, KEY_DOWN) || KEYCHECK_SHORTUP(ev->keys, KEY_UP)
#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
							|| KEYCHECK_SHORTUP(ev->keys, KEY_ROTARY_INCREMENT) || KEYCHECK_SHORTUP(ev->keys, KEY_ROTARY_DECREMENT)
#if defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
							|| KEYCHECK_SHORTUP(ev->keys, KEY_FRONT_UP) || KEYCHECK_SHORTUP(ev->keys, KEY_FRONT_DOWN)
#endif
#endif
					)
							&& BUTTONCHECK_DOWN(ev, BUTTON_SK1)))
			{
				vfoSweepRssiNoiseFloor = VFO_SWEEP_RSSI_NOISE_FLOOR_DEFAULT;
				vfoSweepGain = VFO_SWEEP_GAIN_DEFAULT;
				settingsSet(nonVolatileSettings.vfoSweepSettings, ((uiDataGlobal.Scan.sweepStepSizeIndex << 12) | (vfoSweepRssiNoiseFloor << 7) | vfoSweepGain));
				vfoSweepUpdateSamples(0, true, 0);
			}
			else if (
#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
					KEYCHECK_SHORTUP(ev->keys, KEY_ROTARY_DECREMENT)
#if defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
					|| KEYCHECK_SHORTUP(ev->keys, KEY_FRONT_DOWN)
#endif
#else
					KEYCHECK_SHORTUP(ev->keys, KEY_DOWN) || KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_DOWN)
#endif
			)
			{
#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380)
				if (BUTTONCHECK_DOWN(ev, BUTTON_SK2) && uiVFOModeSweepScanning(true))
				{
					setSweepIncDecSetting(SWEEP_SETTING_STEP, false);
					headerRowIsDirty = true;
				}
				else
				{
					handleDownKey(ev);
				}
				return;
#else
				if (uiVFOModeSweepScanning(true) == false)
				{
					handleDownKey(ev);
				}
				else
				{
					if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
					{
						// Sweep noise floor
						setSweepIncDecSetting(SWEEP_SETTING_RSSI, false);
					}
					else
					{
						// Sweep gain
						setSweepIncDecSetting(SWEEP_SETTING_GAIN, false);
					}
					return;
				}
#endif
			}
			else if (
#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
					KEYCHECK_LONGDOWN(ev->keys, KEY_FRONT_DOWN)
#else
					KEYCHECK_LONGDOWN(ev->keys, KEY_DOWN)
#endif
			)
			{
				if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_SCAN)
				{
					screenOperationMode[nonVolatileSettings.currentVFONumber] = VFO_SCREEN_OPERATION_NORMAL;
					nextKeyBeepMelody = (int16_t *)MELODY_ACK_BEEP;
					uiVFOModeStopScanning();
					return;
				}
			}
			else if (
#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
					KEYCHECK_SHORTUP(ev->keys, KEY_ROTARY_INCREMENT)
#if defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
					|| KEYCHECK_SHORTUP(ev->keys, KEY_FRONT_UP)
#endif
#else
					KEYCHECK_SHORTUP(ev->keys, KEY_UP) || KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_UP)
#endif
			)
			{
#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380)
				if (BUTTONCHECK_DOWN(ev, BUTTON_SK2) && uiVFOModeSweepScanning(true))
				{
					setSweepIncDecSetting(SWEEP_SETTING_STEP, true);
					headerRowIsDirty = true;
				}
				else
				{
					handleUpKey(ev);
				}
				return;
#else
				if (uiVFOModeSweepScanning(true) == false)
				{
					handleUpKey(ev);
				}
				else
				{
					if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
					{
						// Sweep noise floor
						setSweepIncDecSetting(SWEEP_SETTING_RSSI, true);
					}
					else
					{
						// Sweep gain
						setSweepIncDecSetting(SWEEP_SETTING_GAIN, true);
					}
					return;
				}
#endif
			}
			else if (KEYCHECK_LONGDOWN(ev->keys,
#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
					KEY_FRONT_UP
#else
					KEY_UP
#endif
					) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0))
			{
				if ((screenOperationMode[nonVolatileSettings.currentVFONumber] != VFO_SCREEN_OPERATION_SCAN) &&
						(screenOperationMode[nonVolatileSettings.currentVFONumber] != VFO_SCREEN_OPERATION_DUAL_SCAN) &&
						(screenOperationMode[nonVolatileSettings.currentVFONumber] != VFO_SCREEN_OPERATION_SWEEP))
				{
					scanInit();
					return;
				}
				else
				{
					if ((screenOperationMode[nonVolatileSettings.currentVFONumber] != VFO_SCREEN_OPERATION_DUAL_SCAN) &&
							(screenOperationMode[nonVolatileSettings.currentVFONumber] != VFO_SCREEN_OPERATION_SWEEP))
					{
						setCurrentFreqToScanLimits();
						if (uiDataGlobal.Scan.active == false)
						{
							// User maybe has change the mode, update.
							// In DIGITAL mode, we need at least 120ms to see the HR-C6000 to start the TS ISR.
							if (trxGetMode() == RADIO_MODE_DIGITAL)
							{
								int dwellTime;
								if(uiDataGlobal.Scan.stepTimeMilliseconds > 150)				// if >150ms use DMR Slow mode
								{
									dwellTime = ((currentRadioDevice->trxDMRModeRx == DMR_MODE_DMO) ? SCAN_DMR_SIMPLEX_SLOW_MIN_DWELL_TIME : SCAN_DMR_DUPLEX_SLOW_MIN_DWELL_TIME);
								}
								else
								{
									dwellTime = ((currentRadioDevice->trxDMRModeRx == DMR_MODE_DMO) ? SCAN_DMR_SIMPLEX_FAST_MIN_DWELL_TIME : SCAN_DMR_DUPLEX_FAST_MIN_DWELL_TIME);
								}

								uiDataGlobal.Scan.dwellTime = ((uiDataGlobal.Scan.stepTimeMilliseconds < dwellTime) ? dwellTime : uiDataGlobal.Scan.stepTimeMilliseconds);
							}
							else
							{
								uiDataGlobal.Scan.dwellTime = uiDataGlobal.Scan.stepTimeMilliseconds;
							}

							clearNuisance();

							uiDataGlobal.Scan.active = true;
							if (voicePromptsIsPlaying())
							{
								voicePromptsTerminate();
							}
							soundSetMelody(MELODY_KEY_LONG_BEEP);
							keyboardReset();
						}
					}
				}
			}
#if defined(PLATFORM_DM1801)
			else if (KEYCHECK_LONGDOWN(ev->keys, KEY_RED) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_RED) == false))
			{
				if (BUTTONCHECK_DOWN(ev, BUTTON_SK1))
				{
					uiChannelModeOrVFOModeThemeDaytimeChange(false, false);
					return;
				}
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_A_B))
			{
#else
			else if (KEYCHECK_LONGDOWN(ev->keys, KEY_RED) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_RED) == false))
			{
				if (BUTTONCHECK_DOWN(ev, BUTTON_SK1))
				{
					uiChannelModeOrVFOModeThemeDaytimeChange(false, false);
					return;
				}
				else
#endif
				{
					settingsSet(nonVolatileSettings.currentVFONumber, (1 - nonVolatileSettings.currentVFONumber));// Switch to other VFO
					currentChannelData = &settingsVFOChannel[nonVolatileSettings.currentVFONumber];
					uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;

					menuVFOExitStatus = MENU_STATUS_SUCCESS;
					menuSystemPopAllAndDisplayRootMenu(); // Force to set all TX/RX settings.

#if defined(PLATFORM_DM1801)
					if (nonVolatileSettings.currentVFONumber == 0)
#else
					if (nonVolatileSettings.currentVFONumber == 1) // Yes, inverted here, as the beep will apply to other VFO
#endif
					{
						menuVFOExitStatus |= MENU_STATUS_FORCE_FIRST;
					}
					else
					{
						menuVFOExitStatus = MENU_STATUS_SUCCESS;
					}
				}
				return;
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
			{
				if (BUTTONCHECK_DOWN(ev, BUTTON_SK1))
				{
					uiChannelModeOrVFOModeThemeDaytimeChange(true, false);
					return;
				}
				if (BUTTONCHECK_DOWN(ev, BUTTON_SK2) && (uiDataGlobal.tgBeforePcMode != 0))
				{
					settingsSet(nonVolatileSettings.overrideTG, uiDataGlobal.tgBeforePcMode);
					updateTrxID();
					uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;// Force redraw
					menuPrivateCallClear();
					uiVFOModeUpdateScreen(0);
					return;// The event has been handled
				}

#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_DM1801A) || defined(PLATFORM_MD9600) || defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
				if ((trxGetMode() == RADIO_MODE_DIGITAL) && (getAudioAmpStatus() & AUDIO_AMP_MODE_RF))
				{
					HRC6000ClearActiveDMRID();
				}
				menuVFOExitStatus |= MENU_STATUS_FORCE_FIRST;// Audible signal that the Channel screen has been selected
				menuSystemSetCurrentMenu(UI_CHANNEL_MODE);
				aprsBeaconingResetTimers();
#endif
				return;
			}
#if defined(PLATFORM_DM1801) || defined(PLATFORM_RD5R)
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_VFO_MR))
			{
				if ((trxGetMode() == RADIO_MODE_DIGITAL) && (getAudioAmpStatus() & AUDIO_AMP_MODE_RF))
				{
					HRC6000ClearActiveDMRID();
				}
				menuVFOExitStatus |= MENU_STATUS_FORCE_FIRST;// Audible signal that the Channel screen has been selected
				menuSystemSetCurrentMenu(UI_CHANNEL_MODE);
				aprsBeaconingResetTimers();
				return;
			}
#endif
#if defined(PLATFORM_RD5R)
			else if (KEYCHECK_LONGDOWN(ev->keys, KEY_VFO_MR) && (BUTTONCHECK_DOWN(ev, BUTTON_SK1) == 0))
			{
				if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
				{
					announceItem(PROMPT_SEQUENCE_BATTERY, AUDIO_PROMPT_MODE_VOICE_LEVEL_1);
				}
				else
				{
					menuSystemPushNewMenu(UI_VFO_QUICK_MENU);

					// Trick to beep (AudioAssist), since ORANGE button doesn't produce any beep event
					ev->keys.event |= KEY_MOD_UP;
					ev->keys.key = 127;
					menuVFOExitStatus |= (MENU_STATUS_LIST_TYPE | MENU_STATUS_FORCE_FIRST);
					// End Trick
				}

				return;
			}
#endif
			else if (KEYCHECK_LONGDOWN(ev->keys, KEY_INCREASE) && BUTTONCHECK_DOWN(ev, BUTTON_SK2)) // set as KEY_RIGHT on some platforms + SK2
			{
				// Long press allows the 5W+ power setting to be selected immediately (but not while sweep scanning)
				if ((uiVFOModeSweepScanning(true) == false) && increasePowerLevel(true))
				{
					headerRowIsDirty = true;
				}
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_INCREASE)) // set as KEY_RIGHT on some platforms
			{
				if (uiVFOModeSweepScanning(true))
				{
					if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380)
					// In Sweep scan, Right increase RSSI or GAIN
					{
						setSweepIncDecSetting(SWEEP_SETTING_RSSI, true);
						return;
					}
					else
					{
						setSweepIncDecSetting(SWEEP_SETTING_GAIN, true);
						return;
					}
#else
					// In Sweep scan, Right increase RX freq
					{
						setSweepIncDecSetting(SWEEP_SETTING_STEP, false);
						headerRowIsDirty = true;
						return;
					}
					else
					{
						handleUpKey(ev);
					}
#endif
				}
				else
				{
					if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
					{
						if (increasePowerLevel(false))
						{
							headerRowIsDirty = true;
						}
					}
					else
					{
#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380)
						if (uiDataGlobal.Scan.active == false)
						{
#endif
							if (trxGetMode() == RADIO_MODE_DIGITAL)
							{
								if (currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup != 0)
								{
									if (currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup > 1)
									{
										if (nonVolatileSettings.overrideTG == 0)
										{
											settingsIncrement(nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber], 1);
											checkAndFixIndexInRxGroup();
										}

										if (nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber] == 0)
										{
											menuVFOExitStatus |= (MENU_STATUS_LIST_TYPE | MENU_STATUS_FORCE_FIRST);
										}
									}
									settingsSet(nonVolatileSettings.overrideTG, 0);// setting the override TG to 0 indicates the TG is not overridden
								}
								menuPrivateCallClear();
								updateTrxID();
								// We're in digital mode, RXing, and current talker is already at the top of last heard list,
								// hence immediately display complete contact/TG info on screen
								uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;//(isQSODataAvailableForCurrentTalker() ? QSO_DISPLAY_CALLER_DATA : QSO_DISPLAY_DEFAULT_SCREEN);
								if (isQSODataAvailableForCurrentTalker())
								{
									(void)addTimerCallback(uiUtilityRenderQSODataAndUpdateScreen, 2000, UI_VFO_MODE, true);
								}
								uiVFOModeUpdateScreen(0);
								announceItem(PROMPT_SEQUENCE_CONTACT_TG_OR_PC,PROMPT_THRESHOLD_3);
							}
							else
							{
								if(currentChannelData->sql == 0) //If we were using default squelch level
								{
									currentChannelData->sql = nonVolatileSettings.squelchDefaults[currentRadioDevice->trxCurrentBand[TRX_RX_FREQ_BAND]];//start the adjustment from that point.
								}

								if (currentChannelData->sql < CODEPLUG_MAX_VARIABLE_SQUELCH)
								{
									currentChannelData->sql++;
								}

								announceItem(PROMPT_SQUENCE_SQUELCH,PROMPT_THRESHOLD_3);

								uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
								uiNotificationShow(NOTIFICATION_TYPE_SQUELCH, NOTIFICATION_ID_SQUELCH, 1000, NULL, false);
								uiVFOModeUpdateScreen(0);
							}
#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380)
						}
						else
						{
							handleUpKey(ev);      //Up key while scan paused continues the scan
						}
#endif
					}
				}
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_DECREASE)) // set as KEY_LEFT on some platforms
			{
				if (uiVFOModeSweepScanning(true))
				{
					if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380)
					// In Sweep scan, Right decrease RSSI or GAIN
					{
						setSweepIncDecSetting(SWEEP_SETTING_RSSI, false);
					}
					else
					{
						setSweepIncDecSetting(SWEEP_SETTING_GAIN, false);
					}
					return;
#else
					// In Sweep scan, Left decrease RX freq
					{
						setSweepIncDecSetting(SWEEP_SETTING_STEP, true);
						headerRowIsDirty = true;
						return;
					}
					else
					{
						handleDownKey(ev);
					}
#endif
				}
				else
				{
					if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
					{
						if (decreasePowerLevel())
						{
							headerRowIsDirty = true;
						}

						if (trxGetPowerLevel() == 0)
						{
							menuVFOExitStatus |= (MENU_STATUS_LIST_TYPE | MENU_STATUS_FORCE_FIRST);
						}
					}
					else
					{
						if (trxGetMode() == RADIO_MODE_DIGITAL)
						{
							if (currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup != 0)
							{
								if (currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup > 1)
								{
									// To Do change TG in on same channel freq
									if (nonVolatileSettings.overrideTG == 0)
									{
										settingsDecrement(nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber], 1);
										if (nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber] < 0)
										{
											settingsSet(nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber],
													(int16_t) (currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup - 1));
										}

										if(nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber] == 0)
										{
											menuVFOExitStatus |= MENU_STATUS_FORCE_FIRST;
										}
									}
								}
								settingsSet(nonVolatileSettings.overrideTG, 0);// setting the override TG to 0 indicates the TG is not overridden
							}
							menuPrivateCallClear();
							updateTrxID();
							// We're in digital mode, RXing, and current talker is already at the top of last heard list,
							// hence immediately display complete contact/TG info on screen
							uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;//(isQSODataAvailableForCurrentTalker() ? QSO_DISPLAY_CALLER_DATA : QSO_DISPLAY_DEFAULT_SCREEN);
							if (isQSODataAvailableForCurrentTalker())
							{
								(void)addTimerCallback(uiUtilityRenderQSODataAndUpdateScreen, 2000, UI_VFO_MODE, true);
							}
							uiVFOModeUpdateScreen(0);
							announceItem(PROMPT_SEQUENCE_CONTACT_TG_OR_PC,PROMPT_THRESHOLD_3);
						}
						else
						{
							if(currentChannelData->sql == 0) //If we were using default squelch level
							{
								currentChannelData->sql = nonVolatileSettings.squelchDefaults[currentRadioDevice->trxCurrentBand[TRX_RX_FREQ_BAND]];//start the adjustment from that point.
							}

							if (currentChannelData->sql > CODEPLUG_MIN_VARIABLE_SQUELCH)
							{
								currentChannelData->sql--;
							}

							announceItem(PROMPT_SQUENCE_SQUELCH,PROMPT_THRESHOLD_3);

							uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
							uiNotificationShow(NOTIFICATION_TYPE_SQUELCH, NOTIFICATION_ID_SQUELCH, 1000, NULL, false);
							uiVFOModeUpdateScreen(0);
						}
					}
				}
			}
		}
		else // (uiDataGlobal.FreqEnter.index == 0)
		{
			if (KEYCHECK_PRESS(ev->keys, KEY_DECREASE)) // set as KEY_LEFT on some platforms
			{
				uiDataGlobal.FreqEnter.index--;
				uiDataGlobal.FreqEnter.digits[uiDataGlobal.FreqEnter.index] = '-';
				uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
			{
				freqEnterReset();
				soundSetMelody(MELODY_NACK_BEEP);
				uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
				announceItem(PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ, PROMPT_THRESHOLD_NEVER_PLAY_IMMEDIATELY);
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
			{
				if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_NORMAL)
				{
					int newFrequency = freqEnterRead(0, 8, false);

					if (trxGetBandFromFrequency(newFrequency) != FREQUENCY_OUT_OF_BAND)
					{
						updateFrequency(newFrequency, PROMPT_THRESHOLD_3);
						HRC6000ClearColorCodeSynchronisation();
						freqEnterReset();
					}
					else
					{
						menuVFOExitStatus |= MENU_STATUS_ERROR;
					}

					uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
				}
				else if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_SCAN)
				{
					// Complete frequencies with zeros

					// Low
					if (uiDataGlobal.FreqEnter.index > 0 && uiDataGlobal.FreqEnter.index < 6)
					{
						memset(uiDataGlobal.FreqEnter.digits + uiDataGlobal.FreqEnter.index, '0', (6 - uiDataGlobal.FreqEnter.index) - 1);
						uiDataGlobal.FreqEnter.index = 5;
						keyval = 0;
					} // High
					else if (uiDataGlobal.FreqEnter.index > 6 && uiDataGlobal.FreqEnter.index < 12)
					{
						memset(uiDataGlobal.FreqEnter.digits + uiDataGlobal.FreqEnter.index, '0', (6 - (uiDataGlobal.FreqEnter.index - 6)) - 1);
						uiDataGlobal.FreqEnter.index = 11;
						keyval = 0;
					}
					else
					{
						if (uiDataGlobal.FreqEnter.index != 0)
						{
							menuVFOExitStatus |= MENU_STATUS_ERROR;
						}
					}
				}
			}
		}

		// Short 0 with no frequency being typed - a frequency never starts with 0,
		// so it was never a digit here: find the station's CTCSS/DCS. On release,
		// so a long press still opens the selective call.
		if ((uiDataGlobal.FreqEnter.index == 0) && KEYCHECK_SHORTUP(ev->keys, KEY_0) && (trxGetMode() == RADIO_MODE_ANALOG) &&
				(screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_NORMAL) &&
				(BUTTONCHECK_DOWN(ev, BUTTON_SK1) == false) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == false) &&
				(uiDataGlobal.Scan.active == false) && (uiDataGlobal.Scan.toneActive == false))
		{
			toneFinderToggle();
			return;
		}

		if (uiDataGlobal.FreqEnter.index < ((screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_NORMAL) ? 8 : 12))
		{
			// GREEN key was pressed while entering scan freq if keyval is != 99
			if (keyval == 99)
			{
				keyval = menuGetKeypadKeyValue(ev, true);

#if !defined(PLATFORM_GD77S)
				if ((keyval < 10) && BUTTONCHECK_DOWN(ev, BUTTON_SK1) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == false))
				{
					if (keyval == 1)
					{
						aprsBeaconingToggles();
					}
					else if (keyval == 2)
					{
						if (aprsBeaconingGetMode() == APRS_BEACONING_MODE_MANUAL)
						{
							aprsBeaconingSendBeacon(false);
						}
					}

					return;
				}
#endif
			}

			if ((keyval != 99) &&
					// Not first '0' digit in frequencies: we don't support < 100 MHz
					((((uiDataGlobal.FreqEnter.index == 0) && (keyval == 0)) == false) &&
							(((screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_SCAN) && (uiDataGlobal.FreqEnter.index == 6) && (keyval == 0)) == false)) &&
							(BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0))
			{
				voicePromptsInit();
				voicePromptsAppendPrompt(PROMPT_0 +  keyval);
				if ((uiDataGlobal.FreqEnter.index == 2) || (uiDataGlobal.FreqEnter.index == 8))
				{
					voicePromptsAppendPrompt(PROMPT_POINT);
				}
				voicePromptsPlay();

				uiDataGlobal.FreqEnter.digits[uiDataGlobal.FreqEnter.index] = (char) keyval + '0';
				uiDataGlobal.FreqEnter.index++;

				if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_NORMAL)
				{
					if (uiDataGlobal.FreqEnter.index == 8)
					{
						int newFreq = freqEnterRead(0, 8, false);

						if (trxGetBandFromFrequency(newFreq) != FREQUENCY_OUT_OF_BAND)
						{
							updateFrequency(newFreq, AUDIO_PROMPT_MODE_BEEP);
							HRC6000ClearColorCodeSynchronisation();
							freqEnterReset();
							soundSetMelody(MELODY_ACK_BEEP);
						}
						else
						{
							uiDataGlobal.FreqEnter.index--;
							uiDataGlobal.FreqEnter.digits[uiDataGlobal.FreqEnter.index] = '-';
							soundSetMelody(MELODY_ERROR_BEEP);
							menuVFOExitStatus |= MENU_STATUS_ERROR;
						}
					}
				}
				else if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_SCAN)
				{
					// Check low boundary
					if (uiDataGlobal.FreqEnter.index == 6)
					{
						int fLower = freqEnterRead(0, 6, false) * 100;

						if (trxGetBandFromFrequency(fLower) == FREQUENCY_OUT_OF_BAND)
						{
							uiDataGlobal.FreqEnter.index--;
							uiDataGlobal.FreqEnter.digits[uiDataGlobal.FreqEnter.index] = '-';
							soundSetMelody(MELODY_ERROR_BEEP);
							menuVFOExitStatus |= MENU_STATUS_ERROR;
						}
					}
					else if (uiDataGlobal.FreqEnter.index == FREQ_ENTER_DIGITS_MAX)
					{
						int fStep = VFO_FREQ_STEP_TABLE[(currentChannelData->VFOflag5 >> 4)];
						int fLower = freqEnterRead(0, 6, false) * 100;
						int fUpper = freqEnterRead(6, 12, false) * 100;

						// Reorg min/max
						if (fLower > fUpper)
						{
							SAFE_SWAP(fLower, fUpper);
						}

						// At least on step diff
						if ((fUpper - fLower) < fStep)
						{
							fUpper = fLower + fStep;
						}

						// Refresh on every step if scan boundaries is equal to one frequency step.
						uiDataGlobal.Scan.refreshOnEveryStep = ((fUpper - fLower) <= fStep);

						if ((trxGetBandFromFrequency(fLower) != FREQUENCY_OUT_OF_BAND) && (trxGetBandFromFrequency(fUpper) != FREQUENCY_OUT_OF_BAND))
						{
							settingsSet(nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber], (uint32_t) fLower);
							settingsSet(nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber], (uint32_t) fUpper);

							freqEnterReset();
							soundSetMelody(MELODY_ACK_BEEP);
							announceItem(PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ, PROMPT_THRESHOLD_3);
						}
						else
						{
							uiDataGlobal.FreqEnter.index--;
							uiDataGlobal.FreqEnter.digits[uiDataGlobal.FreqEnter.index] = '-';
							soundSetMelody(MELODY_ERROR_BEEP);
							menuVFOExitStatus |= MENU_STATUS_ERROR;
						}
					}
				}

				uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
			}
		}
	}
}

static void handleUpKey(uiEvent_t *ev)
{
	uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
	if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
	{
		// Don't permit to switch from RX/TX while scanning
		if ((screenOperationMode[nonVolatileSettings.currentVFONumber] != VFO_SCREEN_OPERATION_SCAN) &&
				(screenOperationMode[nonVolatileSettings.currentVFONumber] != VFO_SCREEN_OPERATION_DUAL_SCAN) &&
				(screenOperationMode[nonVolatileSettings.currentVFONumber] != VFO_SCREEN_OPERATION_SWEEP))
		{
			selectedFreq = VFO_SELECTED_FREQUENCY_INPUT_RX;
			announceItem(PROMPT_SEQUENCE_DIRECTION_RX, PROMPT_THRESHOLD_1);
		}
	}
	else
	{
		if (uiDataGlobal.Scan.active)
		{
			if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_SWEEP)
			{
				stepFrequency(VFO_SWEEP_SCAN_FREQ_STEP_TABLE[uiDataGlobal.Scan.sweepStepSizeIndex]);
				uiVFOModeUpdateScreen(0);
				vfoSweepUpdateSamples(1, false, 0);
			}
			else
			{
				stepFrequency(VFO_FREQ_STEP_TABLE[(currentChannelData->VFOflag5 >> 4)] * uiDataGlobal.Scan.direction);
				uiDataGlobal.Scan.timer.timeout = 500;
				uiDataGlobal.Scan.state = SCAN_STATE_SCANNING;
				uiVFOModeUpdateScreen(0);
			}
		}
		else
		{
			stepFrequency(VFO_FREQ_STEP_TABLE[(currentChannelData->VFOflag5 >> 4)]);
			uiVFOModeUpdateScreen(0);
		}

	}

	settingsSetVFODirty();
}

static void handleDownKey(uiEvent_t *ev)
{
	uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
	if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
	{
		// Don't permit to switch from RX/TX while scanning
		if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_NORMAL)
		{
			selectedFreq = VFO_SELECTED_FREQUENCY_INPUT_TX;
			announceItem(PROMPT_SEQUENCE_DIRECTION_TX, PROMPT_THRESHOLD_1);
		}
	}
	else
	{
		if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_SWEEP)
		{
			stepFrequency(VFO_SWEEP_SCAN_FREQ_STEP_TABLE[uiDataGlobal.Scan.sweepStepSizeIndex] * -1);
		}
		else
		{
			stepFrequency(VFO_FREQ_STEP_TABLE[(currentChannelData->VFOflag5 >> 4)] * -1);
		}

		uiVFOModeUpdateScreen(0);
		settingsSetVFODirty();

		if (uiDataGlobal.Scan.active && (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_SWEEP))
		{
			vfoSweepUpdateSamples(-1, false, 0);
		}
	}
}

static void vfoSweepDrawSample(int offset)
{
	int16_t graphHeight = MAX(vfoSweepSamples[offset] - vfoSweepRssiNoiseFloor, 0);
	graphHeight = (graphHeight * VFO_SWEEP_GRAPH_HEIGHT_Y) / vfoSweepGain;
	graphHeight = MIN(VFO_SWEEP_GRAPH_HEIGHT_Y, graphHeight);

	int16_t levelTop = ((VFO_SWEEP_GRAPH_START_Y + VFO_SWEEP_GRAPH_HEIGHT_Y) - graphHeight);

	// Draw the level
	displayThemeApply(THEME_ITEM_FG_RSSI_BAR, THEME_ITEM_BG);
	displayDrawFastVLine(offset, VFO_SWEEP_GRAPH_START_Y, (VFO_SWEEP_GRAPH_HEIGHT_Y - graphHeight), false); // Clear
	displayDrawFastVLine(offset, levelTop, graphHeight, true); // Level

	// center freq marker
	if (offset == (DISPLAY_SIZE_X >> 1))
	{
		bool markerTopPosition = (graphHeight < ((VFO_SWEEP_GRAPH_HEIGHT_Y / 3) << 1));
		int16_t markerStarts = (markerTopPosition ? VFO_SWEEP_GRAPH_START_Y : ((VFO_SWEEP_GRAPH_START_Y + VFO_SWEEP_GRAPH_HEIGHT_Y) - (VFO_SWEEP_GRAPH_HEIGHT_Y / 3)));
		int16_t markerEnds = (markerTopPosition ? levelTop : (VFO_SWEEP_GRAPH_START_Y + VFO_SWEEP_GRAPH_HEIGHT_Y));

		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		for (int16_t y = markerStarts; y < markerEnds; y += 2)
		{
			displaySetPixel(offset, y, true);
			displaySetPixel(offset, (y + 1), false);
		}
	}

	displayThemeResetToDefault();
}

static void vfoSweepUpdateSamples(int offset, bool forceRedraw, int bandwidthRescale)
{
	const int SHIFT_DISTANCE[7] = {6,6,6,6,8,8,8};
	offset *= SHIFT_DISTANCE[uiDataGlobal.Scan.sweepStepSizeIndex];// real offset in samples;

	if (offset != 0)
	{
		if (offset > 0)
		{
			uiDataGlobal.Scan.sweepSampleIndex = VFO_SWEEP_NUM_SAMPLES - 1 - offset;
			memcpy(&vfoSweepSamples[0], &vfoSweepSamples[offset], VFO_SWEEP_NUM_SAMPLES - offset);
			memset(&vfoSweepSamples[VFO_SWEEP_NUM_SAMPLES - offset], 0x00,  offset);
		}
		else
		{
			uiDataGlobal.Scan.sweepSampleIndex = 0;
			offset *= -1;
			memmove(&vfoSweepSamples[offset], &vfoSweepSamples[0], VFO_SWEEP_NUM_SAMPLES - offset);
			memset(&vfoSweepSamples[0], 0x00, offset);
		}
	}

	if (bandwidthRescale != 0)
	{
		uint8_t tmp[VFO_SWEEP_NUM_SAMPLES];
		memset(tmp, 0x00, VFO_SWEEP_NUM_SAMPLES);
		int newStartSample = (VFO_SWEEP_NUM_SAMPLES / 2) - (VFO_SWEEP_NUM_SAMPLES / 4);

		if (bandwidthRescale > 0)
		{
			for(int i = 0; i < VFO_SWEEP_NUM_SAMPLES; i+= 2)
			{
				int average = 0;
				for(int j = 0; j < 2; j++ )
				{
					average += vfoSweepSamples[i + j];
				}
				volatile int outBufPos =  (i / 2) + newStartSample;
				tmp[outBufPos] = average / 2;
			}
			uiDataGlobal.Scan.sweepSampleIndex = ((VFO_SWEEP_NUM_SAMPLES * 3) / 4);// Most efficient is to start filling in from the area revealed on the right side of the screen
		}
		else
		{
			int newValue;
			for(int i = 0; i < (VFO_SWEEP_NUM_SAMPLES - 1); i++)
			{
				newValue = (vfoSweepSamples[newStartSample + (i / 2)] + vfoSweepSamples[newStartSample + (i / 2) + 1]) / 2; // use simple 2 point expansion averaging
				tmp[i] = newValue;
			}
			tmp[VFO_SWEEP_NUM_SAMPLES - 1] = newValue;

			uiDataGlobal.Scan.sweepSampleIndex = 0;
		}

		memcpy(vfoSweepSamples, tmp, VFO_SWEEP_NUM_SAMPLES * sizeof(uint8_t));
	}

	if (forceRedraw || (offset != 0))
	{
		for(int i = 0; i < VFO_SWEEP_NUM_SAMPLES; i++)
		{
			vfoSweepDrawSample(i);
		}

		displayRenderRows(1, ((8 + VFO_SWEEP_GRAPH_HEIGHT_Y) / 8) + 1);
	}

	if (uiDataGlobal.Scan.state == SCAN_STATE_SCANNING)
	{
		uiDataGlobal.Scan.scanSweepCurrentFreq = currentChannelData->rxFreq + (VFO_SWEEP_SCAN_RANGE_SAMPLE_STEP_TABLE[uiDataGlobal.Scan.sweepStepSizeIndex] * (uiDataGlobal.Scan.sweepSampleIndex - (VFO_SWEEP_NUM_SAMPLES / 2))) / VFO_SWEEP_PIXELS_PER_STEP;
		trxSetFrequency(uiDataGlobal.Scan.scanSweepCurrentFreq, currentChannelData->txFreq, (((currentChannelData->chMode == RADIO_MODE_DIGITAL) && codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_FORCE_DMO)) ? DMR_MODE_DMO : DMR_MODE_AUTO));
		ticksTimerStart(&uiDataGlobal.Scan.timer, VFO_SWEEP_STEP_TIME);
	}
}

static void setSweepIncDecSetting(sweepSetting_t type, bool increment)
{
	bool apply = false;
	uint16_t setting = nonVolatileSettings.vfoSweepSettings;
	int bandwidthRescaleDirection = 0;
	switch (type)
	{
		case SWEEP_SETTING_STEP:
			{
				int oldStepIndex = uiDataGlobal.Scan.sweepStepSizeIndex;
				if (increment)
				{
					uiDataGlobal.Scan.sweepStepSizeIndex = SAFE_MIN(((sizeof(VFO_SWEEP_SCAN_RANGE_SAMPLE_STEP_TABLE) / sizeof(VFO_SWEEP_SCAN_RANGE_SAMPLE_STEP_TABLE[0])) - 1), (uiDataGlobal.Scan.sweepStepSizeIndex + 1));
					bandwidthRescaleDirection = 1;
				}
				else
				{
					uiDataGlobal.Scan.sweepStepSizeIndex = SAFE_MAX(0, (uiDataGlobal.Scan.sweepStepSizeIndex - 1));
					bandwidthRescaleDirection = -1;
				}

				if (oldStepIndex != uiDataGlobal.Scan.sweepStepSizeIndex)
				{
					apply = true;
					setting = (uiDataGlobal.Scan.sweepStepSizeIndex << 12) | (nonVolatileSettings.vfoSweepSettings & 0xFFF);
				}
			}
			break;
		case SWEEP_SETTING_RSSI:
			{
				if (increment)
				{
					if (vfoSweepRssiNoiseFloor > VFO_SWEEP_RSSI_NOISE_FLOOR_MIN)
					{
						vfoSweepRssiNoiseFloor--;
						apply = true;
					}
				}
				else
				{
					if (vfoSweepRssiNoiseFloor < VFO_SWEEP_RSSI_NOISE_FLOOR_MAX)
					{
						vfoSweepRssiNoiseFloor++;
						apply = true;
					}
				}

				if (apply)
				{
					setting &= 0xF07F;
					setting |= (vfoSweepRssiNoiseFloor << 7);
				}
			}
			break;
		case SWEEP_SETTING_GAIN:
			{
				if (increment)
				{
					if (vfoSweepGain > VFO_SWEEP_GAIN_STEP)
					{
						vfoSweepGain -= VFO_SWEEP_GAIN_STEP;
						apply = true;
					}

				}
				else
				{
					if (vfoSweepGain < VFO_SWEEP_GAIN_MAX)
					{
						vfoSweepGain += VFO_SWEEP_GAIN_STEP;
						apply = true;
					}
				}

				if (apply)
				{
					setting &= 0xFF80;
					setting |= vfoSweepGain;
				}
			}
			break;
	}

	settingsSet(nonVolatileSettings.vfoSweepSettings, setting);
	settingsSaveIfNeeded(true);

	if (apply)
	{
		vfoSweepUpdateSamples(0, true, bandwidthRescaleDirection);
	}
}

static void stepFrequency(int increment)
{
	int newTxFreq;
	int newRxFreq;

	if (selectedFreq == VFO_SELECTED_FREQUENCY_INPUT_TX)
	{
		newTxFreq  = currentChannelData->txFreq + increment;
		newRxFreq  = currentChannelData->rxFreq; // Needed later for the band limited checking
	}
	else
	{
		// VFO_SELECTED_FREQUENCY_INPUT_RX
		newRxFreq  = currentChannelData->rxFreq + increment;
		if (!uiDataGlobal.QuickMenu.tmpTxRxLockMode)
		{
			newTxFreq  = currentChannelData->txFreq + increment;
		}
		else
		{
			newTxFreq  = currentChannelData->txFreq;// Needed later for the band limited checking
		}
	}

	// Out of frequency in the current band, update freq to the next or prev band.
	if (trxGetBandFromFrequency(newRxFreq) == FREQUENCY_OUT_OF_BAND)
	{
		int band = trxGetNextOrPrevBandFromFrequency(newRxFreq, (increment > 0));

		if (band != -1)
		{
			newRxFreq = ((increment > 0) ? RADIO_HARDWARE_FREQUENCY_BANDS[band].minFreq : RADIO_HARDWARE_FREQUENCY_BANDS[band].maxFreq);
			newTxFreq = newRxFreq;
		}
		else
		{
			soundSetMelody(MELODY_ERROR_BEEP);
			return;
		}
	}

	if (trxGetBandFromFrequency(newRxFreq) != FREQUENCY_OUT_OF_BAND)
	{
		currentChannelData->txFreq = newTxFreq;
		currentChannelData->rxFreq = newRxFreq;

		trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, (((currentChannelData->chMode == RADIO_MODE_DIGITAL) && codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_FORCE_DMO)) ? DMR_MODE_DMO : DMR_MODE_AUTO));

		if (screenOperationMode[nonVolatileSettings.currentVFONumber] != VFO_SCREEN_OPERATION_SWEEP)
		{
			HRC6000ClearColorCodeSynchronisation();
		}

		if ((uiDataGlobal.Scan.active == false) || (uiDataGlobal.Scan.active && (uiDataGlobal.Scan.state == SCAN_STATE_PAUSED)))
		{
			announceItem(PROMPT_SEQUENCE_VFO_FREQ_UPDATE, PROMPT_THRESHOLD_3);
		}
	}
	else
	{
		soundSetMelody(MELODY_ERROR_BEEP);
	}
}

// ---------------------------------------- Quick Menu functions -------------------------------------------------------------------
menuStatus_t uiVFOModeQuickMenu(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		if (quickmenuNewChannelHandled)
		{
			quickmenuNewChannelHandled = false;
			menuSystemPopAllAndDisplayRootMenu();
			return MENU_STATUS_SUCCESS;
		}

		uiDataGlobal.QuickMenu.tmpDmrDestinationFilterLevel = nonVolatileSettings.dmrDestinationFilter;
		uiDataGlobal.QuickMenu.tmpDmrCcTsFilterLevel = nonVolatileSettings.dmrCcTsFilter;
		uiDataGlobal.QuickMenu.tmpAnalogFilterLevel = nonVolatileSettings.analogFilterLevel;
		uiDataGlobal.QuickMenu.tmpTxRxLockMode = settingsIsOptionBitSet(BIT_TX_RX_FREQ_LOCK);
		uiDataGlobal.QuickMenu.tmpVFONumber = nonVolatileSettings.currentVFONumber;
		uiDataGlobal.QuickMenu.tmpToneScanCSS = toneScanCSS;
		
		menuDataGlobal.numItems = NUM_VFO_SCREEN_QUICK_MENU_ITEMS;

		menuDataGlobal.menuOptionsSetQuickkey = 0;
		menuDataGlobal.menuOptionsTimeout = 0;
		menuDataGlobal.newOptionSelected = true;

		voicePromptsInit();
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		voicePromptsAppendLanguageString(currentLanguage->quick_menu);
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		voicePromptsAppendPrompt(PROMPT_SILENCE);

		updateQuickMenuScreen(true);
		return (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);
	}
	else
	{
		menuQuickVFOExitStatus = MENU_STATUS_SUCCESS;

		if (ev->hasEvent || (menuDataGlobal.menuOptionsTimeout > 0))
		{
			handleQuickMenuEvent(ev);
		}
	}
	return menuQuickVFOExitStatus;
}

static bool validateNewChannel(void)
{
	quickmenuNewChannelHandled = true;

	if (uiDataGlobal.MessageBox.keyPressed == KEY_GREEN)
	{
		int16_t newChannelIndex;

		//look for empty channel
		for (newChannelIndex = CODEPLUG_CHANNELS_MIN; newChannelIndex <= CODEPLUG_CHANNELS_MAX; newChannelIndex++)
		{
			if (!codeplugAllChannelsIndexIsInUse(newChannelIndex))
			{
				break;
			}
		}

		if (newChannelIndex <= CODEPLUG_CONTACTS_MAX)
		{
			int currentTS = trxGetDMRTimeSlot();
			char nameBuf[SCREEN_LINE_BUFFER_SIZE];
			struct_codeplugChannel_t tempChannel = channelScreenChannelData;

			memcpy(&tempChannel.rxFreq, &settingsVFOChannel[nonVolatileSettings.currentVFONumber].rxFreq, CODEPLUG_CHANNEL_DATA_STRUCT_SIZE - 16);// Don't copy the name of the vfo, which are in the first 16 bytes
			tempChannel.rxTone = currentChannelData->rxTone;
			tempChannel.txTone = currentChannelData->txTone;

			// Codeplug string aren't NULL terminated.
			snprintf(nameBuf, SCREEN_LINE_BUFFER_SIZE, "%s %d", currentLanguage->new_channel, newChannelIndex);
			memset(&tempChannel.name, 0xFF, sizeof(tempChannel.name));
			memcpy(&tempChannel.name, nameBuf, strlen(nameBuf));

			// change the TS on the new channel to whatever the radio is currently set to.
			codeplugChannelSetFlag(&tempChannel, CHANNEL_FLAG_TIMESLOT_TWO, ((currentTS != 0) ? 1 : 0));

			if (codeplugChannelSaveDataForIndex(newChannelIndex, &tempChannel))
			{
				codeplugAllChannelsIndexSetUsed(newChannelIndex); //Set channel index as valid
			}

			// Check if currentZone is initialized
			if (currentZone.NOT_IN_CODEPLUGDATA_indexNumber == 0xDEADBEEF)
			{
				uiChannelInitializeCurrentZone();
			}

			// check if its real zone and or the virtual zone "All Channels" whose index is -1
			if (CODEPLUG_ZONE_IS_ALLCHANNELS(currentZone))
			{
				// All Channels virtual zone
				settingsSet(nonVolatileSettings.currentZone, (int16_t) (codeplugZonesGetCount() - 1));//set zone to all channels and channel index to free channel found

				// Change to the index of the new channel
				codeplugSetLastUsedChannelInZone(currentZone.NOT_IN_CODEPLUGDATA_indexNumber, newChannelIndex);

				settingsSet(nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE], nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber]);
				currentZone.NOT_IN_CODEPLUGDATA_numChannelsInZone++;
			}
			else
			{
				if (codeplugZoneAddChannelToZoneAndSave(newChannelIndex, &currentZone))
				{
					codeplugSetLastUsedChannelInZone(currentZone.NOT_IN_CODEPLUGDATA_indexNumber, (currentZone.NOT_IN_CODEPLUGDATA_numChannelsInZone - 1));
				}
				else
				{
					// channelScreenChannelData wasn't modified, only a new channel has been added, and it's available in AllZone.
					nextKeyBeepMelody = (int16_t *)MELODY_NACK_BEEP;
					return true;
				}
			}

			// Channel saving succeeded, now we're sure that channel could
			// be used in the channel screen.
			memcpy(&channelScreenChannelData, &tempChannel, sizeof(tempChannel));
			uiDataGlobal.currentSelectedChannelNumber = newChannelIndex;

			channelScreenChannelData.rxFreq = 0; // NOT SURE IF THIS IS NECESSARY... Flag to the Channel screen that the channel data is now invalid and needs to be reloaded
			uiDataGlobal.VoicePrompts.inhibitInitial = true;
			tsSetManualOverride(((Channel_t) CHANNEL_CHANNEL), (currentTS + 1)); //copy current TS

			// Just override TG/PC blindly, if not already set
			if (nonVolatileSettings.overrideTG == 0)
			{
				settingsSet(nonVolatileSettings.overrideTG, trxTalkGroupOrPcId);
			}

			menuSystemPopAllAndDisplaySpecificRootMenu(UI_CHANNEL_MODE, true);
			nextKeyBeepMelody = (int16_t *)MELODY_ACK_BEEP;
			quickmenuNewChannelHandled = false; // Need to do this, as uiVFOModeQuickMenu() won't be re-entered on the next menu iteration
			return true;
		}

		nextKeyBeepMelody = (int16_t *)MELODY_NACK_BEEP;
	}

	return true;
}

static void updateQuickMenuScreen(bool isFirstRun)
{
	int mNum = 0;
	char buf[SCREEN_LINE_BUFFER_SIZE];
	const char *leftSide;// initialise to please the compiler
	const char *rightSideConst;// initialise to please the compiler
	char rightSideVar[SCREEN_LINE_BUFFER_SIZE];
	int prompt;// For voice prompts

	displayClearBuf();
	bool settingOption = uiQuickKeysShowChoices(buf, SCREEN_LINE_BUFFER_SIZE, currentLanguage->quick_menu);

	for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
	{
		if ((settingOption == false) || (i == 0))
		{
			mNum = menuGetMenuOffset(NUM_VFO_SCREEN_QUICK_MENU_ITEMS, i);
			if (mNum == MENU_OFFSET_BEFORE_FIRST_ENTRY)
			{
				continue;
			}
			else if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
			{
				break;
			}

			prompt = -1;// Prompt not used
			buf[0] = 0;
			rightSideVar[0] = 0;
			rightSideConst = NULL;
			leftSide = NULL;

			switch(mNum)
			{
#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_RD5R) || defined(PLATFORM_DM1801A) || defined(PLATFORM_MD9600) || defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
				case VFO_SCREEN_QUICK_MENU_VFO_A_B:
					sprintf(rightSideVar, "VFO %c", ((uiDataGlobal.QuickMenu.tmpVFONumber == 0) ? 'A' : 'B'));
					break;
#endif
				case VFO_SCREEN_QUICK_MENU_TX_SWAP_RX:
					prompt = PROMPT_VFO_EXCHANGE_TX_RX;
					strcpy(rightSideVar, "Tx <--> Rx");
					break;
				case VFO_SCREEN_QUICK_MENU_BOTH_TO_RX:
					prompt = PROMPT_VFO_COPY_RX_TO_TX;
					strcpy(rightSideVar, "Rx --> Tx");
					break;
				case VFO_SCREEN_QUICK_MENU_BOTH_TO_TX:
					prompt = PROMPT_VFO_COPY_TX_TO_RX;
					strcpy(rightSideVar, "Tx --> Rx");
					break;
				case VFO_SCREEN_QUICK_MENU_FILTER_FM:
					leftSide = currentLanguage->filter;
					if (uiDataGlobal.QuickMenu.tmpAnalogFilterLevel == 0)
					{
						rightSideConst = currentLanguage->none;
					}
					else
					{
						snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%s", ANALOG_FILTER_LEVELS[uiDataGlobal.QuickMenu.tmpAnalogFilterLevel - 1]);
					}
					break;
				case VFO_SCREEN_QUICK_MENU_FILTER_DMR:
					leftSide = currentLanguage->dmr_filter;
					if (uiDataGlobal.QuickMenu.tmpDmrDestinationFilterLevel == 0)
					{
						rightSideConst = currentLanguage->none;
					}
					else
					{
						snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%s", DMR_DESTINATION_FILTER_LEVELS[uiDataGlobal.QuickMenu.tmpDmrDestinationFilterLevel - 1]);
					}
					break;
				case VFO_SCREEN_QUICK_MENU_DMR_CC_SCAN:
					leftSide = currentLanguage->dmr_cc_scan;
					rightSideConst = (uiDataGlobal.QuickMenu.tmpDmrCcTsFilterLevel & DMR_CC_FILTER_PATTERN) ? currentLanguage->off : currentLanguage->on;
					break;
				case VFO_SCREEN_QUICK_MENU_FILTER_DMR_TS:
					leftSide = currentLanguage->dmr_ts_filter;
					rightSideConst = (uiDataGlobal.QuickMenu.tmpDmrCcTsFilterLevel & DMR_TS_FILTER_PATTERN) ? currentLanguage->on : currentLanguage->off;
					break;
				case VFO_SCREEN_QUICK_MENU_VFO_TO_NEW:
					rightSideConst = currentLanguage->vfoToNewChannel;
					break;
				case VFO_SCREEN_QUICK_MENU_TONE_SCAN:
					leftSide = currentLanguage->tone_scan;
					if(trxGetMode() == RADIO_MODE_ANALOG)
					{
						const char *scanCSS[] = { currentLanguage->all, "CTCSS", "DCS", "iDCS" };
						uint8_t offset = 0;

						if (uiDataGlobal.QuickMenu.tmpToneScanCSS == CSS_TYPE_NONE)
						{
							offset = 0;
						}
						else if (uiDataGlobal.QuickMenu.tmpToneScanCSS == CSS_TYPE_CTCSS)
						{
							offset = 1;
						}
						else if (uiDataGlobal.QuickMenu.tmpToneScanCSS == CSS_TYPE_DCS)
						{
							offset = 2;
						}
						else if (uiDataGlobal.QuickMenu.tmpToneScanCSS == (CSS_TYPE_DCS | CSS_TYPE_DCS_INVERTED))
						{
							offset = 3;
						}

						snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%s", scanCSS[offset]);

						if (uiDataGlobal.QuickMenu.tmpToneScanCSS == CSS_TYPE_NONE)
						{
							rightSideConst = currentLanguage->all;
						}
					}
					else
					{
						rightSideConst = currentLanguage->n_a;
					}
					break;
				case VFO_SCREEN_QUICK_MENU_DUAL_SCAN:
					rightSideConst = currentLanguage->dual_watch;
					break;
				case VFO_SCREEN_QUICK_MENU_FREQ_BIND_MODE:
					leftSide = currentLanguage->vfo_freq_bind_mode;
					rightSideConst = (!uiDataGlobal.QuickMenu.tmpTxRxLockMode) ? currentLanguage->on : currentLanguage->off;
					break;
				default:
					strcpy(buf, "");
					break;
			}

			if (leftSide != NULL)
			{
				snprintf(buf, SCREEN_LINE_BUFFER_SIZE, "%s:%s", leftSide, (rightSideVar[0] ? rightSideVar : rightSideConst));
			}
			else
			{
				snprintf(buf, SCREEN_LINE_BUFFER_SIZE, "%s", ((rightSideVar[0] != 0) ? rightSideVar : rightSideConst));
			}

			if (i == 0)
			{
				if (!isFirstRun && (menuDataGlobal.menuOptionsSetQuickkey == 0))
				{
					voicePromptsInit();
				}

				if (prompt != -1)
				{
					voicePromptsAppendPrompt(prompt);
				}
				else
				{
					if ((leftSide != NULL) || menuDataGlobal.newOptionSelected)
					{
						voicePromptsAppendLanguageString(leftSide);
					}

					if ((rightSideVar[0] != 0) && (rightSideConst == NULL))
					{
						voicePromptsAppendString(rightSideVar);
					}
					else
					{
						voicePromptsAppendLanguageString(rightSideConst);
					}
				}

				if (menuDataGlobal.menuOptionsTimeout != -1)
				{
					promptsPlayNotAfterTx();
				}
				else
				{
					menuDataGlobal.menuOptionsTimeout = 0;// clear flag indicating that a QuickKey has just been set
				}
			}

			// QuickKeys
			if (menuDataGlobal.menuOptionsTimeout > 0)
			{
				menuDisplaySettingOption(leftSide, (rightSideVar[0] ? rightSideVar : rightSideConst));
			}
			else
			{
				switch (mNum)
				{
					case VFO_SCREEN_QUICK_MENU_FILTER_FM:
					case VFO_SCREEN_QUICK_MENU_FILTER_DMR:
					case VFO_SCREEN_QUICK_MENU_DMR_CC_SCAN:
					case VFO_SCREEN_QUICK_MENU_FILTER_DMR_TS:
					case VFO_SCREEN_QUICK_MENU_TONE_SCAN:
					case VFO_SCREEN_QUICK_MENU_FREQ_BIND_MODE:
						menuDisplayEntry(i, mNum, buf, (strlen(leftSide) + 1), THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
						break;

					default:
						menuDisplayEntry(i, mNum, buf, 0, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_COLOUR_NONE, THEME_ITEM_BG);
						break;
				}
			}
		}
	}
	displayRender();
}

static void handleQuickMenuEvent(uiEvent_t *ev)
{
	bool isDirty = false;
	bool executingQuickKey = false;

	if ((menuDataGlobal.menuOptionsTimeout > 0) && (!BUTTONCHECK_DOWN(ev, BUTTON_SK2)))
	{
		menuDataGlobal.menuOptionsTimeout--;
		if (menuDataGlobal.menuOptionsTimeout == 0)
		{
			// Let the QuickKey's VP playback to ends before
			// going back to the previous menu
			if (voicePromptsIsPlaying())
			{
				menuDataGlobal.menuOptionsTimeout++;
				return;
			}

			menuSystemPopPreviousMenu();
			return;
		}
	}

	if (ev->events & BUTTON_EVENT)
	{
		if (repeatVoicePromptOnSK1(ev))
		{
			return;
		}
	}

	if (ev->events & FUNCTION_EVENT)
	{
		isDirty = true;
		if (ev->function == FUNC_REDRAW)
		{
			updateQuickMenuScreen(false);
			return;
		}
		else if ((QUICKKEY_TYPE(ev->function) == QUICKKEY_MENU) && (QUICKKEY_ENTRYID(ev->function) < NUM_VFO_SCREEN_QUICK_MENU_ITEMS))
		{
			menuDataGlobal.currentItemIndex = QUICKKEY_ENTRYID(ev->function);
		}

		if ((QUICKKEY_FUNCTIONID(ev->function) != 0))
		{
			menuDataGlobal.menuOptionsTimeout = 1000;
			executingQuickKey = true;
		}
	}

	if ((ev->events & (KEY_EVENT | BUTTON_EVENT)) && (menuDataGlobal.menuOptionsSetQuickkey == 0) && (menuDataGlobal.menuOptionsTimeout == 0))
	{
		if (KEYCHECK_PRESS(ev->keys, KEY_DOWN) && (menuDataGlobal.numItems != 0))
		{
			isDirty = true;
			menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, NUM_VFO_SCREEN_QUICK_MENU_ITEMS);
			menuDataGlobal.newOptionSelected = true;
			menuQuickVFOExitStatus |= MENU_STATUS_LIST_TYPE;
		}
		else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
		{
			isDirty = true;
			menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, NUM_VFO_SCREEN_QUICK_MENU_ITEMS);
			menuDataGlobal.newOptionSelected = true;
			menuQuickVFOExitStatus |= MENU_STATUS_LIST_TYPE;
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{

			quickKeyApply: // branching here when to quickkey was used.

#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_RD5R) || defined(PLATFORM_DM1801A) || defined(PLATFORM_MD9600) || defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
			if (nonVolatileSettings.currentVFONumber != uiDataGlobal.QuickMenu.tmpVFONumber)
			{
				settingsSet(nonVolatileSettings.currentVFONumber, uiDataGlobal.QuickMenu.tmpVFONumber);
				currentChannelData = &settingsVFOChannel[nonVolatileSettings.currentVFONumber];
			}
#endif
			toneScanCSS = uiDataGlobal.QuickMenu.tmpToneScanCSS;

			switch(menuDataGlobal.currentItemIndex)
			{
				case VFO_SCREEN_QUICK_MENU_TX_SWAP_RX:
				{
					int tmpFreq = currentChannelData->txFreq;
					currentChannelData->txFreq = currentChannelData->rxFreq;
					currentChannelData->rxFreq = tmpFreq;
					trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, (((currentChannelData->chMode == RADIO_MODE_DIGITAL) && codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_FORCE_DMO)) ? DMR_MODE_DMO : DMR_MODE_AUTO));
					announceItem(PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ, PROMPT_THRESHOLD_3);
				}
				break;

				case VFO_SCREEN_QUICK_MENU_BOTH_TO_RX:
					currentChannelData->txFreq = currentChannelData->rxFreq;
					trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, (((currentChannelData->chMode == RADIO_MODE_DIGITAL) && codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_FORCE_DMO)) ? DMR_MODE_DMO : DMR_MODE_AUTO));
					announceItem(PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ, PROMPT_THRESHOLD_3);
					break;

				case VFO_SCREEN_QUICK_MENU_BOTH_TO_TX:
					currentChannelData->rxFreq = currentChannelData->txFreq;
					trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, (((currentChannelData->chMode == RADIO_MODE_DIGITAL) && codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_FORCE_DMO)) ? DMR_MODE_DMO : DMR_MODE_AUTO));
					announceItem(PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ, PROMPT_THRESHOLD_3);
					break;

				case VFO_SCREEN_QUICK_MENU_VFO_TO_NEW:
					if (quickmenuNewChannelHandled == false)
					{
						snprintf(uiDataGlobal.MessageBox.message, MESSAGEBOX_MESSAGE_LEN_MAX, "%s\n%s", currentLanguage->new_channel, currentLanguage->please_confirm);
						uiDataGlobal.MessageBox.type = MESSAGEBOX_TYPE_INFO;
						uiDataGlobal.MessageBox.decoration = MESSAGEBOX_DECORATION_FRAME;
						uiDataGlobal.MessageBox.buttons = MESSAGEBOX_BUTTONS_YESNO;
						uiDataGlobal.MessageBox.validatorCallback = validateNewChannel;
						menuSystemPushNewMenu(UI_MESSAGE_BOX);

						voicePromptsInit();
						voicePromptsAppendLanguageString(currentLanguage->new_channel);
						voicePromptsAppendLanguageString(currentLanguage->please_confirm);
						voicePromptsPlay();
					}
					return;
					break;

				case VFO_SCREEN_QUICK_MENU_TONE_SCAN:
					if (trxGetMode() == RADIO_MODE_ANALOG)
					{
						trxSetAnalogFilterLevel(ANALOG_FILTER_CSS);
						bool cssTypesDiffer = false;
						CodeplugCSSTypes_t currentCSSType = codeplugGetCSSType(currentChannelData->rxTone);

						// Check if the current CSS differs from the one set to scan.
						if (((currentCSSType & CSS_TYPE_NONE) == 0) && ((toneScanCSS & CSS_TYPE_NONE) == 0) && (toneScanCSS != currentCSSType))
						{
							cssTypesDiffer = true;
						}

						//                                          CTCSS or DCS         no CSS
						toneScanType = (((toneScanCSS & CSS_TYPE_NONE) == 0) ? toneScanCSS : currentCSSType);
						prevCSSTone = currentChannelData->rxTone;

						if ((currentCSSType == CSS_TYPE_NONE) || cssTypesDiffer)
						{
							// CSS type are different, start from index 0
							scanToneIndex = 0;
							if (toneScanType == CSS_TYPE_NONE)
							{
								toneScanType = CSS_TYPE_CTCSS;
							}
							currentChannelData->rxTone = cssGetToneFromIndex(scanToneIndex, toneScanType);
						}
						else
						{
							// Get the tone index in the current type array.
							scanToneIndex = cssGetToneIndex(currentChannelData->rxTone, toneScanType);

							// Set the tone to the next one
							cssIncrement(&currentChannelData->rxTone, &scanToneIndex, 1, &toneScanType, true, (toneScanCSS != CSS_TYPE_NONE));
						}

						disableAudioAmp(AUDIO_AMP_MODE_RF);
						trxRxAndTxOff(true);
						trxSetRxCSS(RADIO_DEVICE_PRIMARY, currentChannelData->rxTone);
						trxRxOn(true);

						uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
						uiDataGlobal.Scan.toneActive = true;
						uiDataGlobal.Scan.refreshOnEveryStep = false;
						uiDataGlobal.Scan.timer.timeout = ((toneScanType == CSS_TYPE_CTCSS) ? (SCAN_TONE_INTERVAL - (scanToneIndex * 2)) : SCAN_TONE_INTERVAL);
						uiDataGlobal.Scan.direction = 1;
					}
					break;

				case VFO_SCREEN_QUICK_MENU_DUAL_SCAN:
					uiDataGlobal.Scan.active = true;
					uiDataGlobal.Scan.stepTimeMilliseconds = settingsGetScanStepTimeMilliseconds();
					uiDataGlobal.Scan.dwellTime = 135;// for Dual Watch, use a larger step time than normally scanning, and which does not synchronise with the DMR 30ms timeslots
					uiDataGlobal.Scan.timer.timeout = uiDataGlobal.Scan.dwellTime;
					uiDataGlobal.Scan.refreshOnEveryStep = false;
					screenOperationMode[CHANNEL_VFO_A] = screenOperationMode[CHANNEL_VFO_B] = VFO_SCREEN_OPERATION_DUAL_SCAN;
					uiDataGlobal.VoicePrompts.inhibitInitial = true;
					uiDataGlobal.Scan.scanType = SCAN_TYPE_DUAL_WATCH;
					int currentPowerSavingLevel = rxPowerSavingGetLevel();
					if (currentPowerSavingLevel > 1)
					{
						rxPowerSavingSetLevel(currentPowerSavingLevel - 1);
					}
					break;

				default:
					// VFO_SCREEN_QUICK_MENU_FILTER_FM
					if (nonVolatileSettings.analogFilterLevel != uiDataGlobal.QuickMenu.tmpAnalogFilterLevel)
					{
						settingsSet(nonVolatileSettings.analogFilterLevel, uiDataGlobal.QuickMenu.tmpAnalogFilterLevel);
						trxSetAnalogFilterLevel(nonVolatileSettings.analogFilterLevel);
					}

					// VFO_SCREEN_QUICK_MENU_FILTER_DMR
					if (nonVolatileSettings.dmrDestinationFilter != uiDataGlobal.QuickMenu.tmpDmrDestinationFilterLevel)
					{
						settingsSet(nonVolatileSettings.dmrDestinationFilter, uiDataGlobal.QuickMenu.tmpDmrDestinationFilterLevel);
						if (trxGetMode() == RADIO_MODE_DIGITAL)
						{
							HRC6000InitDigitalDmrRx();
							disableAudioAmp(AUDIO_AMP_MODE_RF);
						}
					}

					// VFO_SCREEN_QUICK_MENU_DMR_CC_FILTER
					// VFO_SCREEN_QUICK_MENU_FILTER_DMR_TS
					if (nonVolatileSettings.dmrCcTsFilter != uiDataGlobal.QuickMenu.tmpDmrCcTsFilterLevel)
					{
						settingsSet(nonVolatileSettings.dmrCcTsFilter, uiDataGlobal.QuickMenu.tmpDmrCcTsFilterLevel);
						if (trxGetMode() == RADIO_MODE_DIGITAL)
						{
							HRC6000InitDigitalDmrRx();
							HRC6000ResyncTimeSlot();
							disableAudioAmp(AUDIO_AMP_MODE_RF);
						}
					}

					// VFO_SCREEN_QUICK_MENU_FREQ_BIND_MODE
					if (settingsIsOptionBitSet(BIT_TX_RX_FREQ_LOCK) != uiDataGlobal.QuickMenu.tmpTxRxLockMode)
					{
						settingsSetOptionBit(BIT_TX_RX_FREQ_LOCK, uiDataGlobal.QuickMenu.tmpTxRxLockMode);
					}
					break;
			}

			if (executingQuickKey)
			{
				updateQuickMenuScreen(false);
			}
			else
			{
				menuSystemPopPreviousMenu();
			}
			return;
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			uiVFOModeStopScanning();
			menuSystemPopPreviousMenu();
			return;
		}
#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_RD5R) || defined(PLATFORM_DM1801A) || defined(PLATFORM_MD9600) || defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_DM1801A) || defined(PLATFORM_MD9600) || defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
		else if (((ev->events & BUTTON_EVENT) && BUTTONCHECK_SHORTUP(ev, BUTTON_ORANGE)) && (menuDataGlobal.currentItemIndex == VFO_SCREEN_QUICK_MENU_VFO_A_B))
#elif defined(PLATFORM_RD5R)
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_VFO_MR) && (menuDataGlobal.currentItemIndex == VFO_SCREEN_QUICK_MENU_VFO_A_B))
#endif
		{
			settingsSet(nonVolatileSettings.currentVFONumber, (1 - uiDataGlobal.QuickMenu.tmpVFONumber));// Switch to other VFO
			currentChannelData = &settingsVFOChannel[nonVolatileSettings.currentVFONumber];
			menuSystemPopPreviousMenu();
			if (nonVolatileSettings.currentVFONumber == 0)
			{
				// Trick to beep (AudioAssist), since ORANGE button doesn't produce any beep event
				ev->keys.event |= KEY_MOD_UP;
				ev->keys.key = 127;
				// End Trick

				menuQuickVFOExitStatus |= MENU_STATUS_FORCE_FIRST;
			}
			return;
		}
#endif
		else if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && BUTTONCHECK_DOWN(ev, BUTTON_SK2))
		{
			isDirty = true;
			menuDataGlobal.menuOptionsSetQuickkey = ev->keys.key;
		}
	}


	if ((ev->events & (KEY_EVENT | FUNCTION_EVENT)) && (menuDataGlobal.menuOptionsSetQuickkey == 0))
	{
		if (KEYCHECK_PRESS(ev->keys, KEY_RIGHT)
#if defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
				|| KEYCHECK_SHORTUP(ev->keys, KEY_ROTARY_INCREMENT)
#endif
				|| (QUICKKEY_FUNCTIONID(ev->function) == FUNC_RIGHT))
		{
			if (menuDataGlobal.menuOptionsTimeout > 0)
			{
				menuDataGlobal.menuOptionsTimeout = 1000;
			}
			isDirty = true;
			menuDataGlobal.newOptionSelected = false;

			switch(menuDataGlobal.currentItemIndex)
			{
#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_RD5R) || defined(PLATFORM_DM1801A) || defined(PLATFORM_MD9600) || defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
				case VFO_SCREEN_QUICK_MENU_VFO_A_B:
					if (uiDataGlobal.QuickMenu.tmpVFONumber == 0)
					{
						uiDataGlobal.QuickMenu.tmpVFONumber = 1;
					}
					break;
#endif
				case VFO_SCREEN_QUICK_MENU_FILTER_FM:
					if (uiDataGlobal.QuickMenu.tmpAnalogFilterLevel < NUM_ANALOG_FILTER_LEVELS - 1)
					{
						uiDataGlobal.QuickMenu.tmpAnalogFilterLevel++;
					}
					break;
				case VFO_SCREEN_QUICK_MENU_FILTER_DMR:
					if (uiDataGlobal.QuickMenu.tmpDmrDestinationFilterLevel < NUM_DMR_DESTINATION_FILTER_LEVELS - 1)
					{
						uiDataGlobal.QuickMenu.tmpDmrDestinationFilterLevel++;
					}
					break;
				case VFO_SCREEN_QUICK_MENU_DMR_CC_SCAN:
					if (uiDataGlobal.QuickMenu.tmpDmrCcTsFilterLevel & DMR_CC_FILTER_PATTERN)
					{
						uiDataGlobal.QuickMenu.tmpDmrCcTsFilterLevel &= ~DMR_CC_FILTER_PATTERN;
					}
					break;
				case VFO_SCREEN_QUICK_MENU_FILTER_DMR_TS:
					if (!(uiDataGlobal.QuickMenu.tmpDmrCcTsFilterLevel & DMR_TS_FILTER_PATTERN))
					{
						uiDataGlobal.QuickMenu.tmpDmrCcTsFilterLevel |= DMR_TS_FILTER_PATTERN;
					}
					break;
				case VFO_SCREEN_QUICK_MENU_TONE_SCAN:
					if (trxGetMode() == RADIO_MODE_ANALOG)
					{
						if (uiDataGlobal.QuickMenu.tmpToneScanCSS == CSS_TYPE_NONE)
						{
							uiDataGlobal.QuickMenu.tmpToneScanCSS = CSS_TYPE_CTCSS;
						}
						else if (uiDataGlobal.QuickMenu.tmpToneScanCSS == CSS_TYPE_CTCSS)
						{
							uiDataGlobal.QuickMenu.tmpToneScanCSS = CSS_TYPE_DCS;
						}
						else if (uiDataGlobal.QuickMenu.tmpToneScanCSS == CSS_TYPE_DCS)
						{
							uiDataGlobal.QuickMenu.tmpToneScanCSS = (CSS_TYPE_DCS | CSS_TYPE_DCS_INVERTED);
						}
					}
					break;
				case VFO_SCREEN_QUICK_MENU_FREQ_BIND_MODE:
					uiDataGlobal.QuickMenu.tmpTxRxLockMode = false;
					break;
			}

			if (executingQuickKey) // Instantly apply new setting
			{
				goto quickKeyApply;
			}
		}
		else if (KEYCHECK_PRESS(ev->keys, KEY_LEFT)
#if defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
				|| KEYCHECK_SHORTUP(ev->keys, KEY_ROTARY_DECREMENT)
#endif
				|| (QUICKKEY_FUNCTIONID(ev->function) == FUNC_LEFT))
		{
			if (menuDataGlobal.menuOptionsTimeout > 0)
			{
				menuDataGlobal.menuOptionsTimeout = 1000;
			}
			isDirty = true;
			menuDataGlobal.newOptionSelected = false;

			switch(menuDataGlobal.currentItemIndex)
			{
#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_RD5R) || defined(PLATFORM_DM1801A) || defined(PLATFORM_MD9600) || defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
				case VFO_SCREEN_QUICK_MENU_VFO_A_B:
					if (uiDataGlobal.QuickMenu.tmpVFONumber == 1)
					{
						uiDataGlobal.QuickMenu.tmpVFONumber = 0;
					}
					menuQuickVFOExitStatus |= MENU_STATUS_FORCE_FIRST;
					break;
#endif
				case VFO_SCREEN_QUICK_MENU_FILTER_FM:
					if (uiDataGlobal.QuickMenu.tmpAnalogFilterLevel > ANALOG_FILTER_NONE)
					{
						uiDataGlobal.QuickMenu.tmpAnalogFilterLevel--;
					}
					break;
				case VFO_SCREEN_QUICK_MENU_FILTER_DMR:
					if (uiDataGlobal.QuickMenu.tmpDmrDestinationFilterLevel > DMR_DESTINATION_FILTER_NONE)
					{
						uiDataGlobal.QuickMenu.tmpDmrDestinationFilterLevel--;
					}
					break;
				case VFO_SCREEN_QUICK_MENU_DMR_CC_SCAN:
					if (!(uiDataGlobal.QuickMenu.tmpDmrCcTsFilterLevel & DMR_CC_FILTER_PATTERN))
					{
						uiDataGlobal.QuickMenu.tmpDmrCcTsFilterLevel |= DMR_CC_FILTER_PATTERN;
					}
					break;
				case VFO_SCREEN_QUICK_MENU_FILTER_DMR_TS:
					if (uiDataGlobal.QuickMenu.tmpDmrCcTsFilterLevel & DMR_TS_FILTER_PATTERN)
					{
						uiDataGlobal.QuickMenu.tmpDmrCcTsFilterLevel &= ~DMR_TS_FILTER_PATTERN;
					}
					break;
				case VFO_SCREEN_QUICK_MENU_TONE_SCAN:
					if (trxGetMode() == RADIO_MODE_ANALOG)
					{
						if (uiDataGlobal.QuickMenu.tmpToneScanCSS == CSS_TYPE_CTCSS)
						{
							uiDataGlobal.QuickMenu.tmpToneScanCSS = CSS_TYPE_NONE;
						}
						else if (uiDataGlobal.QuickMenu.tmpToneScanCSS == CSS_TYPE_DCS)
						{
							uiDataGlobal.QuickMenu.tmpToneScanCSS = CSS_TYPE_CTCSS;
						}
						else if (uiDataGlobal.QuickMenu.tmpToneScanCSS == (CSS_TYPE_DCS | CSS_TYPE_DCS_INVERTED))
						{
							uiDataGlobal.QuickMenu.tmpToneScanCSS = CSS_TYPE_DCS;
						}
					}
					break;
				case VFO_SCREEN_QUICK_MENU_FREQ_BIND_MODE:
					uiDataGlobal.QuickMenu.tmpTxRxLockMode = true;
					break;
			}

			if (executingQuickKey) // Instantly apply new setting
			{
				goto quickKeyApply;
			}
		}
		else if ((ev->keys.event & KEY_MOD_PRESS) && (menuDataGlobal.menuOptionsTimeout > 0))
		{
			menuDataGlobal.menuOptionsTimeout = 0;
			menuSystemPopPreviousMenu();
			return;
		}
	}

	if (uiQuickKeysIsStoring(ev))
	{
		uiQuickKeysStore(ev, &menuQuickVFOExitStatus);
		isDirty = true;
	}

	if (isDirty)
	{
		updateQuickMenuScreen(false);
	}
}

bool uiVFOModeIsScanning(void)
{
	return (uiDataGlobal.Scan.toneActive || uiDataGlobal.Scan.active);
}

bool uiVFOModeDualWatchIsScanning(void)
{
	return ((menuSystemGetCurrentMenuNumber() == UI_VFO_MODE) && uiDataGlobal.Scan.active &&
			(uiDataGlobal.Scan.state == SCAN_STATE_SCANNING) && (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_DUAL_SCAN));
}

bool uiVFOModeSweepScanning(bool includePaused)
{
	return ((menuSystemGetCurrentMenuNumber() == UI_VFO_MODE) &&
			uiDataGlobal.Scan.active &&
			(includePaused ? ((uiDataGlobal.Scan.state == SCAN_STATE_SCANNING) || (uiDataGlobal.Scan.state == SCAN_STATE_PAUSED)) : (uiDataGlobal.Scan.state == SCAN_STATE_SCANNING)) &&
			(screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_SWEEP));
}

bool uiVFOModeFrequencyScanningIsActiveAndEnabled(uint32_t *lowFreq, uint32_t *highFreq)
{
	bool ret = ((menuSystemGetCurrentMenuNumber() == UI_VFO_MODE) && (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_SCAN));

	if (ret && lowFreq && highFreq)
	{
		*lowFreq = nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber];
		*highFreq = nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber];
	}

	return ret;
}

static void toneScan(void)
{
	if (getAudioAmpStatus() & AUDIO_AMP_MODE_RF)
	{
		currentChannelData->txTone = currentChannelData->rxTone;
		uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
		uiVFOModeUpdateScreen(0);
		prevCSSTone = (CODEPLUG_CSS_TONE_NONE - 1);
		uiDataGlobal.Scan.toneActive = false;
		return;
	}

	if (uiDataGlobal.Scan.timer.timeout > 0)
	{
		uiDataGlobal.Scan.timer.timeout--;
	}
	else
	{
		if (uiDataGlobal.Scan.direction == 1)
		{
			cssIncrement(&currentChannelData->rxTone, &scanToneIndex, 1, &toneScanType, true, (toneScanCSS != CSS_TYPE_NONE));
		}
		else
		{
			cssDecrement(&currentChannelData->rxTone, &scanToneIndex, 1, &toneScanType, true, (toneScanCSS != CSS_TYPE_NONE));
		}
		trxRxAndTxOff(true);
		trxSetRxCSS(RADIO_DEVICE_PRIMARY, currentChannelData->rxTone);
		uiDataGlobal.Scan.timer.timeout = ((toneScanType == CSS_TYPE_CTCSS) ? (SCAN_TONE_INTERVAL - (scanToneIndex * 2)) : SCAN_TONE_INTERVAL);
		trxRxOn(true);
		uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
		uiVFOModeUpdateScreen(0);
	}
}

static void updateTrxID(void)
{
	if (nonVolatileSettings.overrideTG != 0)
	{
		trxTalkGroupOrPcId = nonVolatileSettings.overrideTG;
	}
	else
	{
		//tsSetManualOverride(((Channel_t)nonVolatileSettings.currentVFONumber), TS_NO_OVERRIDE);

		// Check if this channel has an Rx Group
		if ((currentRxGroupData.name[0] != 0) && (nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber] < currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup))
		{
			codeplugContactGetDataForIndex(currentRxGroupData.contacts[nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE + nonVolatileSettings.currentVFONumber]], &currentContactData);
		}
		else
		{
			codeplugContactGetDataForIndex(currentChannelData->contact, &currentContactData);
		}

		trxTalkGroupOrPcId = codeplugContactGetPackedId(&currentContactData);

		tsSetContactHasBeenOverriden(((Channel_t)nonVolatileSettings.currentVFONumber), false);

		trxUpdateTsForCurrentChannelWithSpecifiedContact(&currentContactData);
	}
	lastHeardClearLastID();
	menuPrivateCallClear();
}

static void setCurrentFreqToScanLimits(void)
{
	if((currentChannelData->rxFreq < nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber]) ||
			(currentChannelData->rxFreq > nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber]))    //if we are not already inside the Low and High Limit freqs then move to the low limit.
	{
		int offset = currentChannelData->txFreq - currentChannelData->rxFreq;

		currentChannelData->rxFreq = nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber];
		currentChannelData->txFreq = currentChannelData->rxFreq + offset;
		trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, (((currentChannelData->chMode == RADIO_MODE_DIGITAL) && codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_FORCE_DMO)) ? DMR_MODE_DMO : DMR_MODE_AUTO));
		announceItem(PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ, PROMPT_THRESHOLD_3);
	}
}

void uiVFOSweepScanModePause(bool pause, bool forceDigitalOnPause)
{
	if (pause)
	{
		uiDataGlobal.Scan.state = SCAN_STATE_PAUSED;
		vfoSweepSavedBandwidth = trxGetBandwidthIs25kHz();
		if (forceDigitalOnPause)
		{
			trxSetModeAndBandwidth(RADIO_MODE_DIGITAL, false);
		}
		trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, (((currentChannelData->chMode == RADIO_MODE_DIGITAL) && codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_FORCE_DMO)) ? DMR_MODE_DMO : DMR_MODE_AUTO));
		vfoSweepUpdateSamples(0, true, 0);// Force redraw to get rid of the cursor (perhaps we should draw it in the middle);
	}
	else
	{
		trxTerminateCheckAnalogSquelch(RADIO_DEVICE_PRIMARY);
		trxSetModeAndBandwidth(currentChannelData->chMode, vfoSweepSavedBandwidth);
		uiDataGlobal.Scan.state = SCAN_STATE_SCANNING;
		LedWrite(LED_GREEN, 0);
		LedWrite(LED_RED, 0);
		vfoSweepUpdateSamples(0, true, 0);
		headerRowIsDirty = true;
	}
}

static void sweepScanInit(void)
{
	trxTerminateCheckAnalogSquelch(RADIO_DEVICE_PRIMARY);

	uiDataGlobal.Scan.active = true;
	uiDataGlobal.Scan.state = SCAN_STATE_SCANNING;
	uiDataGlobal.Scan.scanType = SCAN_TYPE_NORMAL_STEP;
	sweepListenActive = false; // clear any stale stop-and-listen state
	clearNuisance();           // fresh skip list for this sweep session

	uiDataGlobal.VoicePrompts.inhibitInitial = true;

	if (voicePromptsIsPlaying())
	{
		voicePromptsTerminate();
	}

	if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_LEVEL_2)
	{
		voicePromptsInit();
		voicePromptsAppendPrompt(PROMPT_SWEEP_SCAN_MODE);
		voicePromptsPlay();
	}

	uiDataGlobal.Scan.sweepStepSizeIndex = ((nonVolatileSettings.vfoSweepSettings >> 12) & 0x7);
	vfoSweepRssiNoiseFloor = ((nonVolatileSettings.vfoSweepSettings >> 7) & 0x1F);
	vfoSweepGain = (nonVolatileSettings.vfoSweepSettings & 0x7F);

	screenOperationMode[nonVolatileSettings.currentVFONumber] = VFO_SCREEN_OPERATION_SWEEP;

	uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;

	memset(vfoSweepSamples, 0x00, VFO_SWEEP_NUM_SAMPLES * sizeof(uint8_t));

	menuSystemPopAllAndDisplaySpecificRootMenu(UI_VFO_MODE, true);

	vfoSweepUpdateSamples(0, true, 0);
	headerRowIsDirty = true;

	// trxCheck*Squelch() won't be called while sweeping, blindly turn the
	// green and red LED off, to avoid being lit while scanning.
	LedWrite(LED_GREEN, 0);
	LedWrite(LED_RED, 0);
}


// True if the frequency is on the sweep skip ("nuisance") list - i.e. the operator
// pressed STAR on it, so the sweep should not stop there again (e.g. a repeater output).
static bool sweepFreqIsNuisance(uint32_t freq)
{
	int tol = (VFO_SWEEP_SCAN_RANGE_SAMPLE_STEP_TABLE[uiDataGlobal.Scan.sweepStepSizeIndex] / VFO_SWEEP_PIXELS_PER_STEP) + 1;

	for (int i = 0; i < MAX_ZONE_SCAN_NUISANCE_CHANNELS; i++)
	{
		if (uiDataGlobal.Scan.nuisanceDelete[i] == -1)
		{
			continue;
		}

		int diff = (int)freq - uiDataGlobal.Scan.nuisanceDelete[i];
		if (diff < 0)
		{
			diff = -diff;
		}
		if (diff <= tol)
		{
			return true;
		}
	}
	return false;
}

// Draw the listened frequency and detected tone over the (frozen) sweep graph.
static void sweepListenDrawInfo(void)
{
	char buf[SCREEN_LINE_BUFFER_SIZE];
	uint32_t f = uiDataGlobal.Scan.scanSweepCurrentFreq;

	displayThemeResetToDefault();
	displayFillRect(0, VFO_SWEEP_GRAPH_START_Y, DISPLAY_SIZE_X, 34, true); // clear a panel over the graph

	snprintf(buf, SCREEN_LINE_BUFFER_SIZE, "%u.%04u", (f / 100000), ((f % 100000) / 10));
	displayPrintCentered((VFO_SWEEP_GRAPH_START_Y + 3), buf, FONT_SIZE_3);

	if (sweepListenTone != 0)
	{
		snprintf(buf, SCREEN_LINE_BUFFER_SIZE, "CTCSS %u.%uHz", (sweepListenTone / 10), (sweepListenTone % 10));
	}
	else if (sweepListenToneDone)
	{
		snprintf(buf, SCREEN_LINE_BUFFER_SIZE, "Ton yok");
	}
	else
	{
		snprintf(buf, SCREEN_LINE_BUFFER_SIZE, "Ton araniyor");
	}
	displayPrintCentered((VFO_SWEEP_GRAPH_START_Y + 19), buf, FONT_SIZE_3);

	displayRenderRows(1, (((VFO_SWEEP_GRAPH_START_Y + 34) / 8) + 1));
}

// Pause the sweep on the given frequency and start listening to it.
static void sweepListenStart(uint32_t freq)
{
	uiVFOSweepScanModePause(true, false); // stop sweeping, restore normal FM bandwidth
	trxSetFrequency(freq, currentChannelData->txFreq, DMR_MODE_AUTO); // tune to the found signal
	trxSetRxCSS(RADIO_DEVICE_PRIMARY, CODEPLUG_CSS_TONE_NONE); // hear any signal, ignore the channel's RX CTCSS/DCS
	uiDataGlobal.Scan.scanSweepCurrentFreq = freq;

	sweepListenActive = true;
	sweepListenConfirmed = false;
	ticksTimerStart(&sweepListenMinTimer, SWEEP_LISTEN_MIN_MS);
	ticksTimerStart(&sweepListenMaxTimer, SWEEP_LISTEN_MAX_MS);
	ticksTimerStart(&sweepListenConfirmTimer, SWEEP_LISTEN_CONFIRM_MS);
	ticksTimerStart(&sweepListenHangTimer, SWEEP_LISTEN_HANG_MS);

	// Arm the audio-safe CTCSS detector on the first candidate and show the panel.
	sweepListenTone = 0;
	sweepListenToneIdx = 0;
	sweepListenToneDone = false;
	radioRxCTCSOn(RADIO_DEVICE_PRIMARY, (TRX_CTCSSTones[0] * 10));
	ticksTimerStart(&sweepListenToneTimer, SWEEP_LISTEN_TONE_STEP_MS);
	ticksTimerStart(&sweepListenInfoTimer, SWEEP_LISTEN_INFO_MS);

	sweepListenDrawInfo();
}

// Resume sweeping after a listen.
static void sweepListenStop(void)
{
	sweepListenActive = false;
	trxSetRxCSS(RADIO_DEVICE_PRIMARY, currentChannelData->rxTone); // restore the channel's RX CSS

	// Step past the bin we just listened to so the sweep always moves forward and
	// never re-triggers on the same frequency (which would freeze the cursor).
	uiDataGlobal.Scan.sweepSampleIndex += uiDataGlobal.Scan.sweepSampleIndexIncrement;
	if (uiDataGlobal.Scan.sweepSampleIndex >= VFO_SWEEP_NUM_SAMPLES)
	{
		uiDataGlobal.Scan.sweepSampleIndex = 0;
	}
	else if (uiDataGlobal.Scan.sweepSampleIndex < 0)
	{
		uiDataGlobal.Scan.sweepSampleIndex = (VFO_SWEEP_NUM_SAMPLES - 1);
	}

	uiVFOSweepScanModePause(false, false);
}

// Runs every tick while paused-and-listening. The main loop's analog squelch
// opens/closes the audio (getAudioAmpStatus RF bit) - we use that to know if the
// conversation is still going. Resume once it has ended (hang time) and the
// minimum dwell has elapsed. A false peak (no audio at all) resumes quickly.
static void sweepListenTick(void)
{
	if (!sweepListenActive)
	{
		return;
	}

	bool audioOn = ((getAudioAmpStatus() & AUDIO_AMP_MODE_RF) != 0);

	if (audioOn)
	{
		sweepListenConfirmed = true;
		ticksTimerStart(&sweepListenHangTimer, SWEEP_LISTEN_HANG_MS); // refresh while there is audio
	}

	// Audio-safe CTCSS detection: once a real signal is confirmed, walk the chip's
	// CTCSS detector across the candidates and read the flag. This does not touch
	// the audio path, so the operator keeps hearing the conversation meanwhile.
	if (sweepListenConfirmed && (sweepListenTone == 0) && (sweepListenToneDone == false) &&
			ticksTimerHasExpired(&sweepListenToneTimer))
	{
		if (trxCheckCSSFlag(TRX_CTCSSTones[sweepListenToneIdx]))
		{
			sweepListenTone = TRX_CTCSSTones[sweepListenToneIdx];
			sweepListenDrawInfo();
		}
		else
		{
			sweepListenToneIdx++;
			if (sweepListenToneIdx >= TRX_NUM_CTCSS)
			{
				sweepListenToneIdx = 0;
				sweepListenToneDone = true; // no CTCSS -> carrier-only signal
				sweepListenDrawInfo();
			}
			else
			{
				radioRxCTCSOn(RADIO_DEVICE_PRIMARY, (TRX_CTCSSTones[sweepListenToneIdx] * 10));
			}
		}
		ticksTimerStart(&sweepListenToneTimer, SWEEP_LISTEN_TONE_STEP_MS);
	}

	// Keep the freq/tone panel on screen (something else may repaint the area).
	if (ticksTimerHasExpired(&sweepListenInfoTimer))
	{
		sweepListenDrawInfo();
		ticksTimerStart(&sweepListenInfoTimer, SWEEP_LISTEN_INFO_MS);
	}

	// Safety cap: never let a constant carrier trap the sweep forever.
	if (ticksTimerHasExpired(&sweepListenMaxTimer))
	{
		sweepListenStop();
		return;
	}

	if (!sweepListenConfirmed)
	{
		// No audio opened yet: if the confirm window elapsed, it was a false peak.
		if (ticksTimerHasExpired(&sweepListenConfirmTimer))
		{
			sweepListenStop();
		}
		return;
	}

	// Heard a real signal: resume only after the minimum dwell AND the
	// conversation has been silent for the hang time.
	if (ticksTimerHasExpired(&sweepListenMinTimer) && ticksTimerHasExpired(&sweepListenHangTimer))
	{
		sweepListenStop();
	}
}

static void sweepScanStep(void)
{
	if (uiDataGlobal.Scan.state != SCAN_STATE_SCANNING)
	{
		return;
	}

	if (ticksTimerHasExpired(&uiDataGlobal.Scan.timer))
	{
		ticksTimerStart(&uiDataGlobal.Scan.timer, VFO_SWEEP_STEP_TIME);

		if (uiDataGlobal.Scan.sweepSampleIndex < VFO_SWEEP_NUM_SAMPLES)
		{
#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
			radioReadRSSIAndNoiseForBand(currentRadioDevice->trxCurrentBand[TRX_RX_FREQ_BAND]);
#else
			radioReadRSSIAndNoise();
#endif

			vfoSweepSamples[uiDataGlobal.Scan.sweepSampleIndex] = radioDevices[RADIO_DEVICE_PRIMARY].trxRxSignal;// Need to save the samples so for when the freq is changed and we need to scroll the display

			// Stop & listen: analog FM only. Use the radio's own carrier detect
			// (noise below the squelch level) so we only stop on a REAL signal and
			// the sweep keeps flowing over the noise. scanSweepCurrentFreq still holds
			// the frequency that was just measured (it is tuned one step ahead).
			if ((currentChannelData->chMode == RADIO_MODE_ANALOG) && trxCarrierDetected(RADIO_DEVICE_PRIMARY)
					&& (sweepFreqIsNuisance(uiDataGlobal.Scan.scanSweepCurrentFreq) == false))
			{
				sweepListenStart(uiDataGlobal.Scan.scanSweepCurrentFreq);
				return;
			}

			vfoSweepDrawSample(uiDataGlobal.Scan.sweepSampleIndex);

			uiDataGlobal.Scan.sweepSampleIndex += uiDataGlobal.Scan.sweepSampleIndexIncrement;

			displayThemeApply(THEME_ITEM_FG_RX_FREQ, THEME_ITEM_BG);
			displayDrawFastVLine((uiDataGlobal.Scan.sweepSampleIndex) % VFO_SWEEP_NUM_SAMPLES, VFO_SWEEP_GRAPH_START_Y, VFO_SWEEP_GRAPH_HEIGHT_Y, true);// draw solid line in the next location
			displayDrawFastVLine((uiDataGlobal.Scan.sweepSampleIndex + uiDataGlobal.Scan.sweepSampleIndexIncrement) % VFO_SWEEP_NUM_SAMPLES, VFO_SWEEP_GRAPH_START_Y, VFO_SWEEP_GRAPH_HEIGHT_Y, true);// draw solid line in the next location
			displayThemeResetToDefault();

			if (uiNotificationIsVisible())
			{
				displayRender();
			}
			else
			{
				displayRenderRows(1, ((8 + VFO_SWEEP_GRAPH_HEIGHT_Y) / 8) + 1);
			}
		}
		else
		{
			uiDataGlobal.Scan.sweepSampleIndex = 0;
			uiDataGlobal.Scan.sweepSampleIndexIncrement = 1;// go back to normal increment at the end of the special sweep step used just after the graph is zoomed in
		}

		uiDataGlobal.Scan.scanSweepCurrentFreq = currentChannelData->rxFreq +
				(VFO_SWEEP_SCAN_RANGE_SAMPLE_STEP_TABLE[uiDataGlobal.Scan.sweepStepSizeIndex] *
						(uiDataGlobal.Scan.sweepSampleIndex -
#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
								(VFO_SWEEP_NUM_SAMPLES / 2)
#else
								64
#endif
						)) / VFO_SWEEP_PIXELS_PER_STEP;

		trxSetFrequency(uiDataGlobal.Scan.scanSweepCurrentFreq, currentChannelData->txFreq, (((currentChannelData->chMode == RADIO_MODE_DIGITAL) && codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_FORCE_DMO)) ? DMR_MODE_DMO : DMR_MODE_AUTO));
	}
}

static void scanInit(void)
{
	if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_DUAL_SCAN)
	{
		return;
	}

	uiDataGlobal.Scan.stepTimeMilliseconds = settingsGetScanStepTimeMilliseconds();

	// In DIGITAL mode, we need at least 120ms to see the HR-C6000 to start the TS ISR.
	if (trxGetMode() == RADIO_MODE_DIGITAL)
	{
		int dwellTime;
		if(uiDataGlobal.Scan.stepTimeMilliseconds > 150)				// if >150ms use DMR Slow mode
		{
			dwellTime = ((currentRadioDevice->trxDMRModeRx == DMR_MODE_DMO) ? SCAN_DMR_SIMPLEX_SLOW_MIN_DWELL_TIME : SCAN_DMR_DUPLEX_SLOW_MIN_DWELL_TIME);
		}
		else
		{
			dwellTime = ((currentRadioDevice->trxDMRModeRx == DMR_MODE_DMO) ? SCAN_DMR_SIMPLEX_FAST_MIN_DWELL_TIME : SCAN_DMR_DUPLEX_FAST_MIN_DWELL_TIME);
		}

		uiDataGlobal.Scan.dwellTime = ((uiDataGlobal.Scan.stepTimeMilliseconds < dwellTime) ? dwellTime : uiDataGlobal.Scan.stepTimeMilliseconds);
	}
	else
	{
		uiDataGlobal.Scan.dwellTime = uiDataGlobal.Scan.stepTimeMilliseconds;
	}
	uiDataGlobal.Scan.scanType = SCAN_TYPE_NORMAL_STEP;

	screenOperationMode[nonVolatileSettings.currentVFONumber] = VFO_SCREEN_OPERATION_SCAN;
	uiDataGlobal.Scan.direction = 1;

	// If scan limits have not been defined. Set them to the current Rx freq .. +1MHz
	if ((nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber] == 0) || (nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber] == 0))
	{
		int limitDown = currentChannelData->rxFreq;
		int limitUp = currentChannelData->rxFreq + 100000;

		// If the limitUp in not valid, set it to the next band's minFreq
		if (trxGetBandFromFrequency(limitUp) == FREQUENCY_OUT_OF_BAND)
		{
			int band = trxGetNextOrPrevBandFromFrequency(limitUp, true);

			if (band != -1)
			{
				limitUp = RADIO_HARDWARE_FREQUENCY_BANDS[band].minFreq;
			}
		}

		settingsSet(nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber], SAFE_MIN(limitUp, limitDown));
		settingsSet(nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber], SAFE_MAX(limitUp, limitDown));
	}

	// Refresh on every step if scan boundaries is equal to one frequency step.
	uiDataGlobal.Scan.refreshOnEveryStep = ((nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber] - nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber]) <= VFO_FREQ_STEP_TABLE[(currentChannelData->VFOflag5 >> 4)]);

	clearNuisance();

	selectedFreq = VFO_SELECTED_FREQUENCY_INPUT_RX;

	uiDataGlobal.Scan.timer.timeout = 500;
	uiDataGlobal.Scan.state = SCAN_STATE_SCANNING;

	nextKeyBeepMelody = (int16_t *)MELODY_ACK_BEEP;// Indicate via beep that something different had happened

	menuSystemPopAllAndDisplaySpecificRootMenu(UI_VFO_MODE, true);
}

static void scanning(void)
{
	static bool scanPaused = false;
	static bool voicePromptsAnnounced = true;

	if (!rxPowerSavingIsRxOn())
	{
		uiDataGlobal.Scan.dwellTime = 10000;
		uiDataGlobal.Scan.timer.timeout = 0;
		return;
	}

	//After initial settling time
	if((uiDataGlobal.Scan.state == SCAN_STATE_SCANNING) && (uiDataGlobal.Scan.timer.timeout > SCAN_SKIP_CHANNEL_INTERVAL) && (uiDataGlobal.Scan.timer.timeout < (uiDataGlobal.Scan.dwellTime - SCAN_FREQ_CHANGE_SETTLING_INTERVAL)))
	{
		// Test for presence of RF Carrier.

		if (trxGetMode() == RADIO_MODE_DIGITAL)
		{
			if(uiDataGlobal.Scan.stepTimeMilliseconds > 150)				// if >150ms use DMR Slow mode
			{
				//DMR Slow MOde
				if (((nonVolatileSettings.dmrCcTsFilter & DMR_TS_FILTER_PATTERN)
						&&
						((slotState != DMR_STATE_IDLE) && ((dmrMonitorCapturedTS != -1) &&
								(((currentRadioDevice->trxDMRModeRx == DMR_MODE_DMO) && (dmrMonitorCapturedTS == trxGetDMRTimeSlot())) ||
										(currentRadioDevice->trxDMRModeRx == DMR_MODE_RMO)))))
						||
						// As soon as the HRC6000 get sync, timeCode != -1 or TS ISR is running
						HRC6000HasGotSync())
				{
					announceItem(PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ,
							((nonVolatileSettings.scanModePause == SCAN_MODE_STOP) ? AUDIO_PROMPT_MODE_VOICE_LEVEL_3 : PROMPT_THRESHOLD_NEVER_PLAY_IMMEDIATELY));


#if defined(PLATFORM_MD9600)
					uiDataGlobal.Scan.clickDiscriminator = CLICK_DISCRIMINATOR;
#endif
					uiDataGlobal.Scan.state = SCAN_STATE_SHORT_PAUSED;
					uiDataGlobal.Scan.timer.timeout = ((TIMESLOT_DURATION * 12) + TIMESLOT_DURATION) * 4; // (1 superframe + 1 TS) * 4 = TS Sync + incoming audio
					scanPaused = true;
					voicePromptsAnnounced = false;
				}
			}
			else
			{
				//DMR Fast Mode
				if(trxCarrierDetected(RADIO_DEVICE_PRIMARY))
				{
					announceItem(PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ,
							((nonVolatileSettings.scanModePause == SCAN_MODE_STOP) ? AUDIO_PROMPT_MODE_VOICE_LEVEL_3 : PROMPT_THRESHOLD_NEVER_PLAY_IMMEDIATELY));

					if ((nonVolatileSettings.dmrCcTsFilter & DMR_TS_FILTER_PATTERN) == 0)
					{
						uiDataGlobal.Scan.timer.timeout = SCAN_SHORT_PAUSE_TIME * 2;	//needs longer delay if in DMR mode and TS Filter is off to allow full detection of signal
					}
					else
					{
						uiDataGlobal.Scan.timer.timeout = SCAN_SHORT_PAUSE_TIME;	//start short delay to allow full detection of signal
					}

					uiDataGlobal.Scan.state = SCAN_STATE_SHORT_PAUSED; //state 1 = pause and test for valid signal that produces audio
					scanPaused = true;
					voicePromptsAnnounced = false;
#if defined(PLATFORM_MD9600)
					uiDataGlobal.Scan.clickDiscriminator = CLICK_DISCRIMINATOR;
#endif

					// Force screen redraw in Analog mode, Dual Watch scanning
					if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_DUAL_SCAN)
					{
						uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
					}
				}
			}
		}
		else
		{
			if(trxCarrierDetected(RADIO_DEVICE_PRIMARY))
			{
				//FM  Mode
				announceItem(PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ,
						((nonVolatileSettings.scanModePause == SCAN_MODE_STOP) ? AUDIO_PROMPT_MODE_VOICE_LEVEL_3 : PROMPT_THRESHOLD_NEVER_PLAY_IMMEDIATELY));

				if (nonVolatileSettings.scanModePause == SCAN_MODE_STOP)
				{
					uiVFOModeStopScanning();
					// Just update the header (to prevent hidden mode)
					displayClearRows(0, 2, false);
					uiUtilityRenderHeader(false, false);
					displayRenderRows(0, 2);
					return;
				}
				else
				{
					uiDataGlobal.Scan.timer.timeout = SCAN_SHORT_PAUSE_TIME; //start short delay to allow full detection of signal
					uiDataGlobal.Scan.state = SCAN_STATE_SHORT_PAUSED; //state 1 = pause and test for valid signal that produces audio
					scanPaused = true;
					voicePromptsAnnounced = false;
#if defined(PLATFORM_MD9600)
					uiDataGlobal.Scan.clickDiscriminator = CLICK_DISCRIMINATOR;
#endif

					// Force screen redraw in Analog mode, Dual Watch scanning
					if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_DUAL_SCAN)
					{
						uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
					}
				}
			}
		}
	}

	// Only do this once if scan mode is PAUSE do it every time if scan mode is HOLD
	if(((uiDataGlobal.Scan.state == SCAN_STATE_PAUSED) && (nonVolatileSettings.scanModePause == SCAN_MODE_HOLD)) || (uiDataGlobal.Scan.state == SCAN_STATE_SHORT_PAUSED))
	{
#if defined(PLATFORM_MD9600)
		if (uiDataGlobal.Scan.clickDiscriminator > 0)
		{
			uiDataGlobal.Scan.clickDiscriminator--;
		}
		else
		{
#endif
			if (getAudioAmpStatus() & AUDIO_AMP_MODE_RF)
			{
				if (nonVolatileSettings.scanModePause == SCAN_MODE_STOP)
				{
					uiVFOModeStopScanning();
					// Just update the header (to prevent hidden mode)
					displayClearRows(0, 2, false);
					uiUtilityRenderHeader(false, false);
					displayRenderRows(0, 2);
					return;
				}
				else
				{
					uiDataGlobal.Scan.timer.timeout = nonVolatileSettings.scanDelay * 1000;
					uiDataGlobal.Scan.state = SCAN_STATE_PAUSED;
				}

				if (voicePromptsAnnounced == false)
				{
					if (nonVolatileSettings.audioPromptMode > AUDIO_PROMPT_MODE_VOICE_LEVEL_2)
					{
						voicePromptsPlay();
					}
					voicePromptsAnnounced = true;
				}
			}
#if defined(PLATFORM_MD9600)
		}
#endif
	}

	if(uiDataGlobal.Scan.timer.timeout > 0)
	{
		uiDataGlobal.Scan.timer.timeout--;
	}
	else
	{
		// We are in Dual Watch scanning mode
		if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_DUAL_SCAN)
		{
			// Select and set the next VFO
			//
			// Note: nonVolatileSettings.currentVFONumber is not changed using settingsSet(), to prevent crazy EEPROM
			//       writes. uiVFOModeStopScanning() is doing this, when the scanning process ends (for any reason).
			nonVolatileSettings.currentVFONumber = (1 - nonVolatileSettings.currentVFONumber);
			currentChannelData = &settingsVFOChannel[nonVolatileSettings.currentVFONumber];

			currentChannelData->libreDMR_Power = 0x00;// Force channel to the Master power

			trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, (((currentChannelData->chMode == RADIO_MODE_DIGITAL) && codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_FORCE_DMO)) ? DMR_MODE_DMO : DMR_MODE_AUTO));

			//Need to load the Rx group if specified even if TG is currently overridden as we may need it later when the left or right button is pressed
			if (currentChannelData->rxGroupList != 0)
			{
				if (currentChannelData->rxGroupList != lastLoadedRxGroup)
				{
					if (codeplugRxGroupGetDataForIndex(currentChannelData->rxGroupList, &currentRxGroupData))
					{
						lastLoadedRxGroup = currentChannelData->rxGroupList;
					}
					else
					{
						lastLoadedRxGroup = -1;
					}
				}
			}
			else
			{
				memset(&currentRxGroupData, 0xFF, sizeof(struct_codeplugRxGroup_t));// If the VFO doesnt have an Rx Group ( TG List) the global var needs to be cleared, otherwise it contains the data from the previous screen e.g. Channel screen
				lastLoadedRxGroup = -1;
			}

			uiVFOModeLoadChannelData(false);

			// In DIGITAL mode, we need at least 120ms to see the HR-C6000 to start the TS ISR.
			if (trxGetMode() == RADIO_MODE_DIGITAL)
			{
				int dwellTime;
				if(uiDataGlobal.Scan.stepTimeMilliseconds > 150)				// if >150ms use DMR Slow mode
				{
					dwellTime = ((currentRadioDevice->trxDMRModeRx == DMR_MODE_DMO) ? SCAN_DMR_SIMPLEX_SLOW_MIN_DWELL_TIME : SCAN_DMR_DUPLEX_SLOW_MIN_DWELL_TIME);
				}
				else
				{
					dwellTime = ((currentRadioDevice->trxDMRModeRx == DMR_MODE_DMO) ? SCAN_DMR_SIMPLEX_FAST_MIN_DWELL_TIME : SCAN_DMR_DUPLEX_FAST_MIN_DWELL_TIME);
				}
				uiDataGlobal.Scan.dwellTime = ((uiDataGlobal.Scan.stepTimeMilliseconds < dwellTime) ? dwellTime : uiDataGlobal.Scan.stepTimeMilliseconds);
			}
			else
			{
				uiDataGlobal.Scan.dwellTime = uiDataGlobal.Scan.stepTimeMilliseconds;
			}

			if (scanPaused)
			{
				uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN; // Force screen redraw on scan resume
				scanPaused = false;
			}
		}
		else // Frequency scanning mode
		{
			uiEvent_t tmpEvent = { .buttons = 0, .keys = NO_KEYCODE, .rotary = 0, .function = 0, .events = NO_EVENT, .hasEvent = 0, .time = 0 };
			int fStep = VFO_FREQ_STEP_TABLE[(currentChannelData->VFOflag5 >> 4)];

			if (uiDataGlobal.Scan.direction == 1)
			{
				if(currentChannelData->rxFreq + fStep <= nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber])
				{
					handleUpKey(&tmpEvent);
				}
				else
				{
					int offset = currentChannelData->txFreq - currentChannelData->rxFreq;

					currentChannelData->rxFreq = nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber];
					currentChannelData->txFreq = currentChannelData->rxFreq + offset;
					trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, (((currentChannelData->chMode == RADIO_MODE_DIGITAL) && codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_FORCE_DMO)) ? DMR_MODE_DMO : DMR_MODE_AUTO));
					HRC6000ClearColorCodeSynchronisation();
				}
			}
			else
			{
				if(currentChannelData->rxFreq + fStep >= nonVolatileSettings.vfoScanLow[nonVolatileSettings.currentVFONumber])
				{
					handleUpKey(&tmpEvent);
				}
				else
				{
					int offset = currentChannelData->txFreq - currentChannelData->rxFreq;
					currentChannelData->rxFreq = nonVolatileSettings.vfoScanHigh[nonVolatileSettings.currentVFONumber];
					currentChannelData->txFreq = currentChannelData->rxFreq+offset;
					trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, (((currentChannelData->chMode == RADIO_MODE_DIGITAL) && codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_FORCE_DMO)) ? DMR_MODE_DMO : DMR_MODE_AUTO));
					HRC6000ClearColorCodeSynchronisation();
				}
			}

			if (uiDataGlobal.Scan.refreshOnEveryStep)
			{
				uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
			}
		}

		uiDataGlobal.Scan.timer.timeout = uiDataGlobal.Scan.dwellTime;
		uiDataGlobal.Scan.state = SCAN_STATE_SCANNING; // Settling and test for carrier presence.

		if (screenOperationMode[nonVolatileSettings.currentVFONumber] == VFO_SCREEN_OPERATION_SCAN)
		{
			//check all nuisance delete entries and skip channel if there is a match
			for(int i = 0; i < MAX_ZONE_SCAN_NUISANCE_CHANNELS; i++)
			{
				if (uiDataGlobal.Scan.nuisanceDelete[i] == -1)
				{
					break;
				}
				else
				{
					if(uiDataGlobal.Scan.nuisanceDelete[i] == currentChannelData->rxFreq)
					{
						uiDataGlobal.Scan.timer.timeout = SCAN_SKIP_CHANNEL_INTERVAL;
						break;
					}
				}
			}
		}
	}
}

static void clearNuisance(void)
{
	//clear all nuisance delete channels at start of scanning
	for(int i = 0; i < MAX_ZONE_SCAN_NUISANCE_CHANNELS; i++)
	{
		uiDataGlobal.Scan.nuisanceDelete[i] = -1;
	}
	uiDataGlobal.Scan.nuisanceDeleteIndex = 0;
}
