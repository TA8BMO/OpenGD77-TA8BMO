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
#include "functions/trx.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "functions/voicePrompts.h"
#include "functions/rxPowerSaving.h"
#include "functions/fiveToneDecode.h"
#include "functions/dtmfDecode.h"
#include "functions/morseDecode.h"
#include "functions/toneFinder.h"
#include "functions/voiceRecorder.h"
#include "interfaces/adc.h"
#if defined(PLATFORM_MD9600) || defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
#include "hardware/radioHardwareInterface.h"
#endif

#if defined(HAS_COLOURS)
#define NAME_BUFFER_LEN   25
#else
#define NAME_BUFFER_LEN   22
#endif

typedef struct
{
	uint16_t index;
	int32_t	 distance;
} channelDistance_t;

#if defined(PLATFORM_GD77S)
typedef enum
{
	GD77S_UIMODE_TG_OR_SQUELCH,
	GD77S_UIMODE_SCAN,
	GD77S_UIMODE_TS,
	GD77S_UIMODE_CC,
	GD77S_UIMODE_FILTER,
	GD77S_UIMODE_DTMF_CONTACTS,
	GD77S_UIMODE_ZONE,
	GD77S_UIMODE_POWER,
	GD77S_UIMODE_ECO,
	GD77S_UIMODE_MAX
} GD77S_UIMODES_t;

typedef struct
{
	bool             firstRun;
	GD77S_UIMODES_t  uiMode;
	bool             channelOutOfBounds;
	uint16_t         dtmfListSelected;
	int32_t          dtmfListCount;
} GD77SParameters_t;

static GD77SParameters_t GD77SParameters =
{
		.firstRun = true,
		.uiMode = GD77S_UIMODE_TG_OR_SQUELCH,
		.channelOutOfBounds = false,
		.dtmfListSelected = 0,
		.dtmfListCount = 0
};

static void buildSpeechUiModeForGD77S(GD77S_UIMODES_t uiMode);

static void checkAndUpdateSelectedChannelForGD77S(uint16_t chanNum, bool forceSpeech);
static void handleEventForGD77S(uiEvent_t *ev);
static uint16_t getCurrentChannelInCurrentZoneForGD77S(void);

#else // ! PLATFORM_GD77S

static void selectPrevNextZone(bool nextZone);
static void handleUpKey(uiEvent_t *ev);
static void handleDownKey(uiEvent_t *ev);

#endif // PLATFORM_GD77S

static void handleEvent(uiEvent_t *ev);

static void updateQuickMenuScreen(bool isFirstRun);
static void handleQuickMenuEvent(uiEvent_t *ev);

static void scanning(void);
static void drawChannelRSSIMeter(void);
static void drawLastFiveToneCall(void);
static void drawMorseWaitingMarker(void);
static bool nsActive(void);
static void nsLiveUpdate(uint32_t now);
static void scanStart(bool longPressBeep);
static void scanSearchForNextChannel(void);
static void scanApplyNextChannel(void);
static void scanStop(bool loadChannel);
static void updateTrxID(void);
static void initSortedChannels(void);

static char currentZoneName[SCREEN_LINE_BUFFER_SIZE];
static int directChannelNumber = 0;

static uint32_t nsFooterTime = 0; // the colour screen's rows (NS_*) are in menuSystem.h

static struct_codeplugChannel_t scanNextChannelData = { .rxFreq = 0 };
static bool scanNextChannelReady = false;
static int scanNextChannelIndex = 0;
static bool scobAlreadyTriggered = false;
static bool quickmenuChannelFromVFOHandled = false; // Quickmenu new channel confirmation window

static menuStatus_t menuChannelExitStatus = MENU_STATUS_SUCCESS;
static menuStatus_t menuQuickChannelExitStatus = MENU_STATUS_SUCCESS;


static void checkChannelLocation(struct_codeplugChannel_t *channelData)
{
	if (nonVolatileSettings.locationLat != SETTINGS_UNITIALISED_LOCATION_LAT && codeplugChannelGetFlag(channelData, CHANNEL_FLAG_USE_LOCATION))
	{
		uint32_t tmp1 = channelData->locationLat2;
		tmp1 = (tmp1 << 8) + channelData->locationLat1;
		tmp1 = (tmp1 << 8) + channelData->locationLat0;

		if (tmp1 != 0)
		{
			uint32_t tmp2 = channelData->locationLon2;
			tmp2 = (tmp2 << 8) + channelData->locationLon1;
			tmp2 = (tmp2 << 8) + channelData->locationLon0;

			if (tmp2 != 0)
			{
				double lat = latLongFixed24ToDouble(tmp1);
				double lon = latLongFixed24ToDouble(tmp2);

				channelData->NOT_IN_CODEPLUG_CALCULATED_DISTANCE_X10 = distanceToLocation(lat, lon) * 10;
				return;
			}
		}
	}

	channelData->NOT_IN_CODEPLUG_CALCULATED_DISTANCE_X10 = -1;
}

menuStatus_t uiChannelMode(uiEvent_t *ev, bool isFirstRun)
{
	static uint32_t m = 0;

	if (isFirstRun)
	{
#if ! defined(PLATFORM_GD77S)
		bool isLockMenu = (menuSystemGetPreviouslyPushedMenuNumber(true) == UI_LOCK_SCREEN);
		// We're coming back from the lock screen (hence no channel init is needed, at all).
		if (isLockMenu || lockscreenIsRearming)
		{
			if (isLockMenu)
			{
				menuSystemGetPreviouslyPushedMenuNumber(false); // Clear the previous lock screen trace
			}

			uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
			uiChannelModeUpdateScreen(0);

			return MENU_STATUS_SUCCESS;
		}

		// GD77S speech can be triggered in main(), so let it ends
		voicePromptsTerminate();
#endif

		settingsSet(nonVolatileSettings.initialMenuNumber, (uint8_t) UI_CHANNEL_MODE);// This menu.
		uiDataGlobal.displayChannelSettings = false;
		scanNextChannelReady = false;
		uiDataGlobal.Scan.refreshOnEveryStep = false;

		uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;

		lastHeardClearLastID();
		uiDataGlobal.displayQSOStatePrev = QSO_DISPLAY_IDLE;
		currentChannelData = &channelScreenChannelData;// Need to set this as currentChannelData is used by functions called by loadChannelData()

		if (currentChannelData->rxFreq != 0x00)
		{
			uiChannelModeLoadChannelData(true, false);
		}
		else
		{
			uiChannelInitializeCurrentZone();
#if defined(PLATFORM_RD5R)
			settingsSet(nonVolatileSettings.currentChannelIndexInZone, codeplugGetLastUsedChannelInCurrentZone());
#endif
			uiChannelModeLoadChannelData(false, false);
		}

		if ((uiDataGlobal.displayQSOState == QSO_DISPLAY_CALLER_DATA) && (trxGetMode() == RADIO_MODE_ANALOG))
		{
			uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
		}

#if defined(PLATFORM_GD77S)
		trxSetPowerFromLevel(nonVolatileSettings.txPowerLevel);// ensure the power level is available for the Power announcement

		//Reset to first UiMode if the radio is in Analog mode and the current UiMode only applies to DMR
		if ((trxGetMode() == RADIO_MODE_ANALOG) &&
			((GD77SParameters.uiMode == GD77S_UIMODE_TS ) ||
			(GD77SParameters.uiMode == GD77S_UIMODE_CC ) ||
			(GD77SParameters.uiMode == GD77S_UIMODE_FILTER )))
		{
			GD77SParameters.uiMode = GD77S_UIMODE_TG_OR_SQUELCH;
		}

		// Ensure the correct channel is loaded, on the very first run
		if (GD77SParameters.firstRun)
		{
			if (voicePromptsIsPlaying() == false)
			{
				GD77SParameters.firstRun = false;
				checkAndUpdateSelectedChannelForGD77S(rotarySwitchGetPosition(), true);
			}


			GD77SParameters.dtmfListCount = codeplugDTMFContactsGetCount();
		}
#endif
		uiChannelModeUpdateScreen(0);

		if (uiDataGlobal.Scan.active == false)
		{
			uiDataGlobal.Scan.state = SCAN_STATE_SCANNING;
		}

		// Need to do this last, as other things in the screen init, need to know whether the main screen has just changed
		if (uiDataGlobal.VoicePrompts.inhibitInitial)
		{
			uiDataGlobal.VoicePrompts.inhibitInitial = false;
		}

		// Scan On Boot is enabled, but has to be run only once.
		if (settingsIsOptionBitSet(BIT_SCAN_ON_BOOT_ENABLED) && (scobAlreadyTriggered == false))
		{
			scanStart(false);
		}

		// Disable ScOB for this session, and also prevent false triggering (like exiting the Options screen)
		scobAlreadyTriggered = true;

		menuChannelExitStatus = MENU_STATUS_SUCCESS; // Due to Orange Quick Menu
	}
	else
	{
		menuChannelExitStatus = MENU_STATUS_SUCCESS;

#if defined(PLATFORM_GD77S)
		uiChannelModeHeartBeatActivityForGD77S(ev);
#endif

		if (ev->events == NO_EVENT)
		{
#if defined(PLATFORM_GD77S)
			// Just ensure rotary's selected channel is matching the already loaded one
			// as rotary selector could be turned while the GD is OFF, or in hotspot mode.
			if ((uiDataGlobal.Scan.active == false) && ((rotarySwitchGetPosition() != getCurrentChannelInCurrentZoneForGD77S()) || (GD77SParameters.firstRun == true)))
			{
				if (voicePromptsIsPlaying() == false)
				{
					checkAndUpdateSelectedChannelForGD77S(rotarySwitchGetPosition(), GD77SParameters.firstRun);

					// Opening channel number announce has not took place yet, probably because it was telling
					// parameter like new hotspot mode selection.
					if (GD77SParameters.firstRun)
					{
						GD77SParameters.firstRun = false;
					}
				}
			}
#endif

			// is there an incoming DMR signal
			if (uiDataGlobal.displayQSOState != QSO_DISPLAY_IDLE)
			{
				uiChannelModeUpdateScreen(0);
			}
			else
			{
				if ((ev->time - m) > RSSI_UPDATE_COUNTER_RELOAD)
				{
					if (rxPowerSavingIsRxOn())
					{
						if (uiDataGlobal.Scan.active && (uiDataGlobal.Scan.state == SCAN_STATE_PAUSED))
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
							uiUtilityDrawRSSIBarGraph();
							if (nsActive())
							{
								nsLiveUpdate(ev->time);
							}
							else
							{
								drawChannelRSSIMeter(); // designed coloured S-meter + numeric S-value (rows 2-3)
							}
						}

						// Render the header bar row plus our meter rows (2-3) so both update live,
						// unless we're scanning (then the whole upper block is redrawn).
						displayRenderRows(((uiDataGlobal.Scan.active && (uiDataGlobal.Scan.state == SCAN_STATE_PAUSED)) ? 0 : 1), 4);
					}
					m = ev->time;
				}
			}
#if ! (defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017))
			if (uiDataGlobal.Scan.active == true)
			{
				scanning();
			}
#endif
		}
		else
		{
			if (ev->hasEvent)
			{
				handleEvent(ev);
			}
		}

#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
		if (uiDataGlobal.Scan.active == true)
		{
			scanning();
		}
#endif

#if defined(PLATFORM_GD77S)
		dtmfSequenceTick(false);
#endif
	}
	return menuChannelExitStatus;
}

#if 0 // rename: we have an union declared (fw_sound.c) with the same name.
uint16_t byteSwap16(uint16_t in)
{
	return ((in & 0xff << 8) | (in >> 8));
}
#endif

static void hidesChannelDetails(void)
{
	if (uiDataGlobal.displayChannelSettings == true)
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

		uiChannelModeUpdateScreen(0);
	}
}

static bool canCurrentZoneBeScanned(int *availableChannels)
{
	int enabledChannels = 0;
	int chanIdx = codeplugGetLastUsedChannelNumberInCurrentZone();

	if (currentZone.NOT_IN_CODEPLUGDATA_numChannelsInZone > 1)
	{
		int chansInZone = currentZone.NOT_IN_CODEPLUGDATA_numChannelsInZone;

		if (CODEPLUG_ZONE_IS_ALLCHANNELS(currentZone))
		{
			do
			{
				do
				{
					chanIdx = ((chanIdx % currentZone.NOT_IN_CODEPLUGDATA_highestIndex) + 1);
				} while (!codeplugAllChannelsIndexIsInUse(chanIdx));

				chansInZone--;
				// Get flag4 only
				codeplugChannelGetDataWithOffsetAndLengthForIndex(chanIdx, &scanNextChannelData, CODEPLUG_CHANNEL_FLAG4_OFFSET, 1);

				if (codeplugChannelGetFlag(&scanNextChannelData, CHANNEL_FLAG_ALL_SKIP) == 0)
				{
					enabledChannels++;
				}

			} while (chansInZone > 0);
		}
		else
		{
			do
			{
				chanIdx = ((chanIdx + 1) % currentZone.NOT_IN_CODEPLUGDATA_numChannelsInZone);

				chansInZone--;
				// Get flag4 only
				codeplugChannelGetDataWithOffsetAndLengthForIndex(currentZone.channels[chanIdx], &scanNextChannelData, CODEPLUG_CHANNEL_FLAG4_OFFSET, 1);

				if (codeplugChannelGetFlag(&scanNextChannelData, CHANNEL_FLAG_ZONE_SKIP) == 0)
				{
					enabledChannels++;
				}

			} while (chansInZone > 0);
		}
	}

	*availableChannels = enabledChannels;

	return (enabledChannels > 1);
}

static void scanSearchForNextChannel(void)
{
	int channel = 0;

	// All Channels virtual zone
	if (CODEPLUG_ZONE_IS_ALLCHANNELS(currentZone))
	{
		do
		{
			do
			{
				// rollover (up/down) CODEPLUG_CHANNELS_MIN .. currentZone.NOT_IN_CODEPLUGDATA_highestIndex
				scanNextChannelIndex = ((uiDataGlobal.Scan.direction == 1) ?
						((scanNextChannelIndex % currentZone.NOT_IN_CODEPLUGDATA_highestIndex) + 1) :
						((((scanNextChannelIndex - 1) + currentZone.NOT_IN_CODEPLUGDATA_highestIndex - 1) % currentZone.NOT_IN_CODEPLUGDATA_highestIndex) + 1));
			} while (!codeplugAllChannelsIndexIsInUse(scanNextChannelIndex));

			// Check if the channel is skipped.
			// Get flag4 only
			codeplugChannelGetDataWithOffsetAndLengthForIndex(scanNextChannelIndex, &scanNextChannelData, CODEPLUG_CHANNEL_FLAG4_OFFSET, 1);

		} while ((codeplugChannelGetFlag(&scanNextChannelData, CHANNEL_FLAG_ALL_SKIP) != 0));

		channel = scanNextChannelIndex;
		codeplugChannelGetDataForIndex(scanNextChannelIndex, &scanNextChannelData);
	}
	else
	{
		do
		{
			// rollover (up/down) 0 .. (currentZone.NOT_IN_CODEPLUGDATA_highestIndex - 1)
			scanNextChannelIndex = ((uiDataGlobal.Scan.direction == 1) ?
					((scanNextChannelIndex + 1) % currentZone.NOT_IN_CODEPLUGDATA_numChannelsInZone) :
					((scanNextChannelIndex + currentZone.NOT_IN_CODEPLUGDATA_numChannelsInZone - 1) % currentZone.NOT_IN_CODEPLUGDATA_numChannelsInZone));

			// Check if the channel is skipped.
			// Get flag4 only
			codeplugChannelGetDataWithOffsetAndLengthForIndex(currentZone.channels[scanNextChannelIndex], &scanNextChannelData, CODEPLUG_CHANNEL_FLAG4_OFFSET, 1);

		} while ((codeplugChannelGetFlag(&scanNextChannelData, CHANNEL_FLAG_ZONE_SKIP) != 0));

		channel = currentZone.channels[scanNextChannelIndex];
		codeplugChannelGetDataForIndex(currentZone.channels[scanNextChannelIndex], &scanNextChannelData);
	}

	//check all nuisance delete entries and skip channel if there is a match
	for (int i = 0; i < MAX_ZONE_SCAN_NUISANCE_CHANNELS; i++)
	{
		if (uiDataGlobal.Scan.nuisanceDelete[i] == -1)
		{
			break;
		}
		else
		{
			if(uiDataGlobal.Scan.nuisanceDelete[i] == channel)
			{
				return;
			}
		}
	}

	scanNextChannelReady = true;
}

static void scanApplyNextChannel(void)
{
	codeplugSetLastUsedChannelInZone(currentZone.NOT_IN_CODEPLUGDATA_indexNumber, scanNextChannelIndex);

	lastHeardClearLastID();

	memcpy(&channelScreenChannelData, &scanNextChannelData, CODEPLUG_CHANNEL_DATA_STRUCT_SIZE);

	uiChannelModeLoadChannelData(true, false);
	uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
	uiChannelModeUpdateScreen(0);

	// In DIGITAL Slow mode, we need at least 120ms to see the HR-C6000 to start the TS ISR.
	if (trxGetMode() == RADIO_MODE_DIGITAL)
	{
		int dwellTime;
		if(uiDataGlobal.Scan.stepTimeMilliseconds > 150)				// if >150ms use DMR Slow mode
		{
			dwellTime = ((currentRadioDevice->trxDMRModeRx == DMR_MODE_DMO) ? SCAN_DMR_SIMPLEX_SLOW_MIN_DWELL_TIME : SCAN_DMR_DUPLEX_SLOW_MIN_DWELL_TIME);
		}
		else
		{
			dwellTime = ((currentRadioDevice->trxDMRModeRx == DMR_MODE_DMO) ? SCAN_DMR_SIMPLEX_FAST_MIN_DWELL_TIME : (SCAN_DMR_DUPLEX_FAST_MIN_DWELL_TIME + SCAN_DMR_DUPLEX_FAST_EXTRA_DWELL_TIME));
		}

		uiDataGlobal.Scan.dwellTime = ((uiDataGlobal.Scan.stepTimeMilliseconds < dwellTime) ? dwellTime : uiDataGlobal.Scan.stepTimeMilliseconds);
	}
	else
	{
		uiDataGlobal.Scan.dwellTime = uiDataGlobal.Scan.stepTimeMilliseconds;
	}

	uiDataGlobal.Scan.timer.timeout = uiDataGlobal.Scan.dwellTime;
	uiDataGlobal.Scan.state = SCAN_STATE_SCANNING;
	scanNextChannelReady = false;
}

void uiChannelModeLoadChannelData(bool useChannelDataInMemory, bool loadVoicePromptAnnouncement)
{
	bool rxGroupValid = true;
#if ! defined(PLATFORM_GD77S) // GD77S handle voice prompts on its own
	bool wasLoadingZone = (currentChannelData->rxFreq == 0x00);
#endif
	int previousSelectedChannelNumber = uiDataGlobal.currentSelectedChannelNumber;

	uiDataGlobal.currentSelectedChannelNumber = codeplugGetLastUsedChannelNumberInCurrentZone();

	if (!useChannelDataInMemory)
	{
		if (currentZone.NOT_IN_CODEPLUGDATA_numChannelsInZone > 0)
		{
			codeplugChannelGetDataForIndex(uiDataGlobal.currentSelectedChannelNumber, &channelScreenChannelData);
		}
		else
		{
			char buffer[SCREEN_LINE_BUFFER_SIZE];

			snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "! %.*s !", 12, currentLanguage->zone_empty); // Limits the language string to 12 characters.

			codeplugUtilConvertStringToBuf(buffer, (char *)&channelScreenChannelData.name, 16);
			channelScreenChannelData.chMode = RADIO_MODE_ANALOG;
			channelScreenChannelData.txFreq = channelScreenChannelData.rxFreq = 14400000;
			channelScreenChannelData.txTone = channelScreenChannelData.rxTone = CODEPLUG_CSS_TONE_NONE;
			channelScreenChannelData.flag4 = CODEPLUG_CHANNEL_FLAG4_RX_ONLY;
			channelScreenChannelData.sql = 10U;
			channelScreenChannelData.NOT_IN_CODEPLUG_flag = 0x00;
			channelScreenChannelData.NOT_IN_CODEPLUG_CALCULATED_DISTANCE_X10 = -1;
		}
	}

	HRC6000ClearActiveDMRID();

#if defined(PLATFORM_MD9600)
	// This could happen if a MK22 codeplug contains out of band channel(s) (like 220MHz) on an PLATFORM_MD9600.
	if ((trxGetBandFromFrequency(currentChannelData->rxFreq) == FREQUENCY_OUT_OF_BAND) || (trxGetBandFromFrequency(currentChannelData->txFreq) == FREQUENCY_OUT_OF_BAND))
	{
		// Flag it as out of band channel
		if (codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_OUT_OF_BAND) == 0)
		{
			codeplugChannelSetFlag(currentChannelData, CHANNEL_FLAG_OUT_OF_BAND, 1);
		}

		trxSetFrequency(OUT_OF_BAND_FALLBACK_FREQUENCY, OUT_OF_BAND_FALLBACK_FREQUENCY, (((currentChannelData->chMode == RADIO_MODE_DIGITAL) && (uiDataGlobal.reverseRepeaterChannel || codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_FORCE_DMO))) ? DMR_MODE_DMO : DMR_MODE_AUTO));
	}
	else
#endif
	{
		uint32_t rxFreq = (uiDataGlobal.reverseRepeaterChannel ? currentChannelData->txFreq : currentChannelData->rxFreq);
		uint32_t txFreq = (uiDataGlobal.talkaround ? rxFreq : (uiDataGlobal.reverseRepeaterChannel ? currentChannelData->rxFreq : currentChannelData->txFreq));

#if defined(PLATFORM_MD9600)
		// Was flagged as out of band channel, it was probably edited and now it's fine, clear the flag
		if (codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_OUT_OF_BAND) != 0)
		{
			codeplugChannelSetFlag(currentChannelData, CHANNEL_FLAG_OUT_OF_BAND, 0);
		}
#endif

		// If reverseRepeater mode is enabled (and mode is DIGITAL), force to DMR Active mode.
		trxSetFrequency(rxFreq, txFreq, (((currentChannelData->chMode == RADIO_MODE_DIGITAL) && (uiDataGlobal.reverseRepeaterChannel || codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_FORCE_DMO))) ? DMR_MODE_DMO : DMR_MODE_AUTO));
	}

	trxSetModeAndBandwidth(currentChannelData->chMode, (codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_BW_25K) != 0));

	if (currentChannelData->chMode == RADIO_MODE_ANALOG)
	{
		if (currentChannelData->aprsConfigIndex != 0)
		{
			currentChannelData->rxTone = CODEPLUG_CSS_TONE_NONE;
			currentChannelData->txTone = CODEPLUG_CSS_TONE_NONE;
			codeplugChannelSetFlag(currentChannelData, CHANNEL_FLAG_VOX, 0);
		}
		trxSetRxCSS(RADIO_DEVICE_PRIMARY, currentChannelData->rxTone);
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
						(((nonVolatileSettings.dmrCcTsFilter & DMR_CC_FILTER_PATTERN) == 0) &&
								((HRC6000CCIsHeld() == false) || (previousSelectedChannelNumber != uiDataGlobal.currentSelectedChannelNumber)))))
		{
			trxSetDMRColourCode(currentChannelData->txColor);
			HRC6000ClearColorCodeSynchronisation();
		}

		if (currentChannelData->rxGroupList != lastLoadedRxGroup)
		{
			rxGroupValid = codeplugRxGroupGetDataForIndex(currentChannelData->rxGroupList, &currentRxGroupData);
			if (rxGroupValid)
			{
				lastLoadedRxGroup = currentChannelData->rxGroupList;
			}
			else
			{
				lastLoadedRxGroup = -1;// RxGroup Contacts are not valid
			}
		}

		// Current contact index is out of group list bounds, select first contact
		if (rxGroupValid && (nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE] > (currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup - 1)))
		{
			settingsSet(nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE], 0);
			menuChannelExitStatus |= (MENU_STATUS_LIST_TYPE | MENU_STATUS_FORCE_FIRST);
		}

		// Check if this channel has an Rx Group
		if (rxGroupValid && nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE] < currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup)
		{
			codeplugContactGetDataForIndex(currentRxGroupData.contacts[nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE]], &currentContactData);
		}
		else
		{
			codeplugContactGetDataForIndex(currentChannelData->contact, &currentContactData);
		}

		trxUpdateTsForCurrentChannelWithSpecifiedContact(&currentContactData);

		if (nonVolatileSettings.overrideTG == 0)
		{
			trxTalkGroupOrPcId = codeplugContactGetPackedId(&currentContactData);
		}
		else
		{
			trxTalkGroupOrPcId = nonVolatileSettings.overrideTG;
		}
	}

	int nextMenu = menuSystemGetPreviouslyPushedMenuNumber(false); // used to determine if this screen has just been loaded after Tx ended (in loadChannelData()))
#if ! defined(PLATFORM_GD77S) // GD77S handle voice prompts on its own
	if (((uiDataGlobal.VoicePrompts.inhibitInitial == false) || loadVoicePromptAnnouncement) &&
			((uiDataGlobal.Scan.active == false) ||
					(uiDataGlobal.Scan.active && ((uiDataGlobal.Scan.state = SCAN_STATE_SHORT_PAUSED) || (uiDataGlobal.Scan.state = SCAN_STATE_PAUSED)))))
	{
		announceItem((wasLoadingZone ? PROMPT_SEQUENCE_ZONE_NAME_CHANNEL_NAME_AND_CONTACT_OR_VFO_FREQ_AND_MODE : PROMPT_SEQUENCE_CHANNEL_NAME_AND_CONTACT_OR_VFO_FREQ_AND_MODE),
				((nextMenu == UI_TX_SCREEN) || (nextMenu == UI_LOCK_SCREEN) || (nextMenu == UI_PRIVATE_CALL) || uiDataGlobal.Scan.active) ? PROMPT_THRESHOLD_NEVER_PLAY_IMMEDIATELY : PROMPT_THRESHOLD_2);
	}
#else
	(void)nextMenu;
	// Force GD77S to always use the Master power level
	currentChannelData->libreDMR_Power = 0x00;
#endif

	if (uiDataGlobal.Scan.active == false)
	{
		checkChannelLocation(currentChannelData);

		if (useChannelDataInMemory == false)
		{
			aprsBeaconingResetTimers();
		}
	}
}

// ---------------------------------------------------------------------------
// The colour channel screen (TA8BMO).
//
// FM only, on channels and on the VFO in its normal state (uiVFOMode.c draws the
// top half there, with these pieces below it): DMR, the VFO's scans and the SK1
// details overlay keep the classic layout, and Display options > Screen >
// Classic brings the old one back everywhere. Every colour goes through uiUtilityScreenColour(), which shows
// the same hue on the glass in normal and in inverse video. Rows:
//    0-13   header and its thin RSSI / mic bar (uiUtilityRenderHeader)
//   15-30   channel name
//   33-64   frequency, 16x32, the last two digits small
//   67-74   zone and channel number | the other frequency
//   77-86   tone, ANI, CW and REC labels | squelch
//   89-104  S-meter while receiving, talk time from 84 while transmitting
//  107-114  last 5-Tone / DTMF caller
//  119-126  clock (or power, transmitting) | battery voltage | temperature
// ---------------------------------------------------------------------------
static bool nsActive(void)
{
	// FM and DMR, a DMR call coming in included: the caller goes on the "Last:"
	// line rather than onto the classic caller screen. Not in hotspot mode, which
	// has a screen of its own.
	if ((uiUtilityNewScreen() == false) || uiDataGlobal.displayChannelSettings || (settingsUsbMode == USB_MODE_HOTSPOT))
	{
		return false;
	}

	switch (menuSystemGetRootMenuNumber())
	{
		case UI_CHANNEL_MODE:
			return true;
		case UI_VFO_MODE:
			return uiVFOModeNewScreenActive();
		default:
			return false;
	}
}

bool uiChannelModeNewScreenActive(void)
{
	return nsActive();
}

static uint16_t nsThemeBg(void)
{
	uint16_t fg, bg;

	displayThemeResetToDefault();
	displayGetForegroundAndBackgroundColours(&fg, &bg);
	return bg;
}

static void nsColour(uint32_t onDark, uint32_t onLight)
{
	uint16_t c = uiUtilityScreenColour(onDark, onLight);

	displaySetForegroundAndBackgroundColours(c, nsThemeBg());
}

static void nsPrint(int16_t x, int16_t y, const char *text, ucFont_t font, ucTextAlign_t align, uint32_t onDark, uint32_t onLight)
{
	nsColour(onDark, onLight);
	displayPrintCore(x, y, text, font, align, false);
}

static void nsClear(int16_t y, int16_t h)
{
	uint16_t bg = nsThemeBg();

	displaySetForegroundAndBackgroundColours(bg, bg);
	displayFillRect(0, y, DISPLAY_SIZE_X, h, false);
}

// Always-on coloured S-meter + numeric S-value for the main channel screen.
// (The stock header RSSI bar was not visible on the MD-UV390; this draws an
// explicit meter in the header/bar row.) Green up to S9, red above S9. The
// numeric value reads S0..S9 then S9+10..S9+60.
// Last decoded 5-Tone caller. Drawn in the gap between the channel name and the
// zone line, and left there - it is only replaced by the next call, cleared on
// a channel change, or lost at power off. The whole point is to be able to walk
// back to the radio and see who called.
// Red CW marker above the channel name, shown while there is decoded Morse
// stored for the frequency in use. Clearing the log in the Morse log screen
// takes it away again; it never appears on a channel it did not arrive on.
static void drawMorseWaitingMarker(void)
{
	if ((trxGetMode() != RADIO_MODE_ANALOG) || uiDataGlobal.displayChannelSettings ||
			(morseDecodeHasTextForCurrentChannel() == false) || nsActive()) // the new screen shows a CW label instead
	{
		return;
	}

	displaySetForegroundAndBackgroundColours(
			PLATFORM_COLOUR_FORMAT_SWAP_BYTES(RGB888_TO_PLATFORM_COLOUR_FORMAT(0xFF0000)),
			PLATFORM_COLOUR_FORMAT_SWAP_BYTES(RGB888_TO_PLATFORM_COLOUR_FORMAT(0x000000)));
	displayPrintCore(0, (DISPLAY_Y_POS_CHANNEL_FIRST_LINE - 16), "CW", FONT_SIZE_3, TEXT_ALIGN_CENTER, false);
	displayThemeResetToDefault();
}

// The DTMF decoder shares this line: whichever of the two was received last is
// the one shown, "5T 12345" or "DT 123*#".
static void drawLastFiveToneCall(void)
{
	char code[FIVETONE_CODE_LEN + 1];
	char dtmf[DTMF_DECODE_CODE_LEN + 1];
	char buf[24];
	uint32_t ftAge = 0;
	uint32_t dtAge = 0;
	bool haveFt;
	bool haveDt;

	if ((trxGetMode() != RADIO_MODE_ANALOG) || uiDataGlobal.displayChannelSettings)
	{
		return;
	}

	haveFt = fiveToneDecodeGetLast(code, &ftAge);
	haveDt = dtmfDecodeGetLast(dtmf, &dtAge);

	// The tone finder (0 key) shares the line too: always while it searches,
	// and afterwards for as long as its answer is newer than any code.
	uint32_t tfAge = 0;
	bool tfActive = false;
	bool haveTf = toneFinderGetText(buf, sizeof(buf), &tfAge, &tfActive);

	if ((haveTf == false) ||
			((tfActive == false) && ((haveFt && (ftAge < tfAge)) || (haveDt && (dtAge < tfAge)))))
	{
		// A caller with a DTMF contact whose code matches is shown by name
		// instead of by number - "5T Ahmet" rather than "5T 12345".
		const char *prefix;
		const char *shown;
		char name[17];

		if (haveDt && ((haveFt == false) || (dtAge < ftAge)))
		{
			prefix = "DT";
			shown = dtmf;
		}
		else if (haveFt)
		{
			prefix = "5T";
			shown = code;
		}
		else
		{
			return;
		}

		if (codeplugDTMFContactFindNameForCode(shown, name))
		{
			shown = name;
		}
		snprintf(buf, sizeof(buf), "%s %s", prefix, shown);
	}

	if (nsActive())
	{
		nsClear(NS_CALLER_Y, 8);
		nsPrint(0, NS_CALLER_Y, buf, FONT_SIZE_2, TEXT_ALIGN_CENTER, 0xD2C3FFU, 0x5A2A9AU);
		displayThemeResetToDefault();
		return;
	}

	// The channel screen has free space below the channel name, but on the VFO
	// screen that same offset lands squarely between the RX and TX frequency
	// lines and overwrites both. The VFO's empty band is above the frequencies,
	// right after the CW marker's row, so the code goes there instead.
	displayThemeApply(THEME_ITEM_FG_CHANNEL_CONTACT_INFO, THEME_ITEM_BG);
	displayPrintCore(0,
			((menuSystemGetCurrentMenuNumber() == UI_VFO_MODE) ?
					DISPLAY_Y_POS_CHANNEL_FIRST_LINE :
					(DISPLAY_Y_POS_CHANNEL_FIRST_LINE + 24)),
			buf, FONT_SIZE_3, TEXT_ALIGN_CENTER, false);
	displayThemeResetToDefault();
}

static void drawChannelRSSIMeter(void)
{
#if defined(HAS_COLOURS)
	// Analog FM only: in DMR the TG/contact line occupies this band. Also skip
	// while transmitting, sweep/tone-scanning, or when the SK1 overlay is shown.
	if ((trxGetMode() != RADIO_MODE_ANALOG) || trxTransmissionEnabled ||
			uiVFOModeSweepScanning(true) || uiDataGlobal.displayChannelSettings ||
			uiDataGlobal.Scan.toneActive)
	{
		return;
	}

	int dbm = trxGetRSSIdBm(RADIO_DEVICE_PRIMARY);
	int s = 0;
	for (int i = 15; i >= 0; i--)
	{
		if (dbm >= DBM_LEVELS[i])
		{
			s = i;
			break;
		}
	}

	char sbuf[10];
	if (s <= 9)
	{
		snprintf(sbuf, sizeof(sbuf), "S%d", s);
	}
	else
	{
		snprintf(sbuf, sizeof(sbuf), "S9+%d", (s - 9) * 10);
	}

	const int textLen = (s <= 9) ? 2 : 5; // "Sn" or "S9+nn"

	// In "black" display mode the panel runs with hardware inverse video (INVON),
	// which flips every framebuffer colour on the glass. Pre-invert our explicit
	// RGB values so the glass shows the intended colours in both modes, and use
	// the theme's real background for the strip so it always matches the screen.
	//
	// Do not be tempted to take these from theme items: THEME_ITEM_FG_DECORATION
	// is the full-brightness text colour, not a dim one, so using it for the
	// unlit segments lights the whole meter up and makes it look stuck at full
	// scale.
	const bool inv = settingsIsOptionBitSet(BIT_INVERSE_VIDEO);
	#define METER_COLOUR(rgb) PLATFORM_COLOUR_FORMAT_SWAP_BYTES(RGB888_TO_PLATFORM_COLOUR_FORMAT(inv ? (0xFFFFFFU ^ (rgb)) : (rgb)))

	const uint16_t green  = METER_COLOUR(0x00C800U);
	const uint16_t orange = METER_COLOUR(0xFF8000U);
	const uint16_t red    = METER_COLOUR(0xFF2020U);
	const uint16_t dim    = METER_COLOUR(inv ? 0x404040U : 0xD0D0D0U); // subtle against either background

	uint16_t themeFg, themeBg;
	displayThemeResetToDefault();
	displayGetForegroundAndBackgroundColours(&themeFg, &themeBg);
	(void)themeFg;

	// Own row, just below the stock header bar (rows 2-3) so the two never overlap.
	const int meterY = 16;
	const int valY   = meterY;      // numeric S-value
	const int barY   = meterY + 9;  // segmented bar
	const int barH   = 6;

	// Segment colour by S zone: green up to S9, orange S9+10..+30, red S9+40 and above.
	#define RSSI_SEG_ZONE(idx) (((idx) <= 8) ? green : (((idx) <= 11) ? orange : red))

	// Clear our two rows with the theme background so the strip blends in.
	displaySetForegroundAndBackgroundColours(themeBg, themeBg);
	displayFillRect(0, meterY, DISPLAY_SIZE_X, 16, false);

	// Numeric S-value, centred, coloured by zone (8px-wide font). S9 stays green.
	uint16_t valColour = (s <= 9) ? green : ((s <= 12) ? orange : red);
	displaySetForegroundAndBackgroundColours(valColour, themeBg);
	displayPrintAt(((DISPLAY_SIZE_X - (textLen * 8)) / 2), valY, sbuf, FONT_SIZE_2);

	// The receive recorder: "REC" at the left of the strip for as long as it is
	// switched on, and a dot after it while a transmission is being recorded.
	// Red on the glass whatever the display mode - the colour is pre-inverted
	// for inverse video like the meter's own.
	if (voiceRecorderGetEnabled() && (voiceRecorderGetStatus() == VOICE_RECORDER_STATUS_OK))
	{
		displaySetForegroundAndBackgroundColours(red, themeBg);
		displayPrintAt(2, valY, "REC", FONT_SIZE_2);
		if (voiceRecorderIsRecording())
		{
			displayFillCircle(30, (valY + 3), 2, false);
		}
	}

	// Segmented meter (15 steps), centred, lit up to the current S-unit; unlit steps dim.
	const int segCount = 15;
	const int segW = 9;
	const int segGap = 1;
	const int barW = (segCount * (segW + segGap)) - segGap;
	const int barX = (DISPLAY_SIZE_X - barW) / 2;
	for (int i = 0; i < segCount; i++)
	{
		displaySetForegroundAndBackgroundColours(((i < s) ? RSSI_SEG_ZONE(i) : dim), themeBg);
		displayFillRect((barX + (i * (segW + segGap))), barY, segW, barH, false);
	}

	#undef RSSI_SEG_ZONE
	#undef METER_COLOUR
	displayThemeResetToDefault();
#endif
}

// Shared entry point so the VFO screen can draw the same meter strip. On the
// colour screen this is its live update instead, rendering its own rows.
void uiChannelModeDrawRSSIMeterStrip(void)
{
	if (nsActive())
	{
		if (uiDataGlobal.FreqEnter.index == 0) // the VFO's digit entry has this part of the screen
		{
			nsLiveUpdate(ticksGetMillis());
		}
	}
	else
	{
		drawChannelRSSIMeter();
	}
}

// Local time, centred in the free band under the S-meter / contact line
// (40..47, clear on channel and VFO in both analog and DMR). The clock itself
// needs no minding: a hardware interrupt counts the seconds and GPS re-syncs it
// automatically whenever it has a fix (gps.c), so unlike a hand-set clock it
// does not drift. This only makes it visible without opening a menu.
static bool clockText(char *buf, size_t len)
{
	struct tm t;
	time_t_custom local;

	// Nothing until the clock has been set to something plausible - a radio
	// that has never had GPS or the CPS would otherwise show 1970.
	if (uiDataGlobal.dateTimeSecs < 1577836800U /* 2020-01-01 */)
	{
		return false;
	}

	// The same timezone arithmetic the Radio Infos screen uses.
	// When no timezone has been set (bit 0x80 clear), default to Turkey - UTC+3,
	// no daylight saving - rather than showing bare UTC, which read three hours
	// behind. An explicitly chosen timezone still takes precedence.
	local = uiDataGlobal.dateTimeSecs +
			((nonVolatileSettings.timezone & 0x80) ? (((nonVolatileSettings.timezone & 0x7F) - 64) * (15 * 60)) : (3 * 60 * 60));

	gmtime_r_Custom(&local, &t);
	snprintf(buf, len, "%02u:%02u", (unsigned int) t.tm_hour, (unsigned int) t.tm_min);
	return true;
}

static void drawClockLine(void)
{
	char buf[8];

	if (uiDataGlobal.displayChannelSettings || nsActive() || (clockText(buf, sizeof(buf)) == false)) // the colour screen has it in its footer
	{
		return;
	}

	displayThemeApply(THEME_ITEM_FG_CHANNEL_NAME, THEME_ITEM_BG);
	displayPrintCore(0, 40, buf, FONT_SIZE_2, TEXT_ALIGN_CENTER, false);
	displayThemeResetToDefault();
}

// Likewise for the received-Morse and 5-Tone markers, so both screens show them.
void uiChannelModeDrawRxDecodeMarkers(void)
{
	drawLastFiveToneCall();
	drawMorseWaitingMarker();

	// Only on the resting screen: caller data draws its own text through this
	// band, and a clock stamped over a callsign helps nobody.
	if (uiDataGlobal.displayQSOStatePrev == QSO_DISPLAY_DEFAULT_SCREEN)
	{
		drawClockLine();
	}
}

// --- The colour channel screen, continued (see nsActive) ---

void uiChannelModeNsPrint(int16_t x, int16_t y, const char *text, ucFont_t font, ucTextAlign_t align, uint32_t onDark, uint32_t onLight)
{
	nsPrint(x, y, text, font, align, onDark, onLight);
}

// A small label on a coloured pad. Returns where the next one goes, or x
// unchanged when it would not fit before maxX.
int16_t uiChannelModeNsChip(int16_t x, int16_t y, int16_t maxX, const char *text, uint32_t padDark, uint32_t padLight, uint32_t textDark, uint32_t textLight)
{
	int16_t w = (int16_t)((strlen(text) * 6) + 4);
	uint16_t pad;

	if ((x + w) > maxX)
	{
		return x;
	}

	pad = uiUtilityScreenColour(padDark, padLight);
	displaySetForegroundAndBackgroundColours(pad, pad);
	displayFillRoundRect(x, y, w, 10, 2, false);
	displaySetForegroundAndBackgroundColours(uiUtilityScreenColour(textDark, textLight), pad);
	displayPrintCore((x + 2), (y + 1), text, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);

	return (x + w + 3);
}

static int16_t nsChip(int16_t x, int16_t maxX, const char *text, uint32_t padDark, uint32_t padLight, uint32_t textDark, uint32_t textLight)
{
	return uiChannelModeNsChip(x, NS_CHIPS_Y, maxX, text, padDark, padLight, textDark, textLight);
}

// The big frequency: 439.237 in 16x32, then MHz over a small 50, in 10Hz units.
void uiChannelModeNsFrequency(uint32_t freq, uint32_t onDark, uint32_t onLight)
{
	const int16_t freqX = 14; // 7 big digits (112) + the small ones / MHz (18), centred
	char buf[12];

	snprintf(buf, sizeof(buf), "%u.%03u", (unsigned) (freq / 100000U), (unsigned) ((freq / 100U) % 1000U));
	nsPrint(freqX, NS_FREQ_Y, buf, FONT_SIZE_4, TEXT_ALIGN_LEFT, onDark, onLight);
	nsPrint((freqX + 114), (NS_FREQ_Y + 2), "MHz", FONT_SIZE_1, TEXT_ALIGN_LEFT, 0x7FA8C9U, 0x506070U);
	snprintf(buf, sizeof(buf), "%02u", (unsigned) (freq % 100U));
	nsPrint((freqX + 114), (NS_FREQ_Y + 16), buf, FONT_SIZE_3, TEXT_ALIGN_LEFT, onDark, onLight);
}

static void nsToneText(char *buf, size_t len, char prefix, uint16_t tone)
{
	if (codeplugGetCSSType(tone) & CSS_TYPE_DCS)
	{
		char p[2] = { prefix, 0 };

		dcsPrintf(buf, len, p, tone);
	}
	else
	{
		snprintf(buf, len, "%c%d.%d", prefix, (tone / 10), (tone % 10));
	}
}

// The S-meter sits lower in DMR, under the talkgroup band.
static int16_t nsMeterY(void)
{
	return ((trxGetMode() == RADIO_MODE_DIGITAL) ? NS_DMR_METER_Y : NS_METER_Y);
}

// S-value, dBm and a 15 step bar: green to S9, orange to S9+30, red above.
// Redrawn on its own by the RSSI tick, so it clears its rows first.
static void nsDrawMeter(void)
{
	int dbm = trxGetRSSIdBm(RADIO_DEVICE_PRIMARY);
	int s = 0;
	char buf[12];
	const int16_t meterY = nsMeterY();

	nsClear(meterY, 16);

	if (uiDataGlobal.Scan.toneActive) // the tone scan has this band
	{
		return;
	}

	for (int i = 15; i >= 0; i--)
	{
		if (dbm >= DBM_LEVELS[i])
		{
			s = i;
			break;
		}
	}

	uint32_t valDark = ((s <= 9) ? 0x3BD16FU : ((s <= 12) ? 0xFF9F40U : 0xFF4A3DU));
	uint32_t valLight = ((s <= 9) ? 0x107A30U : ((s <= 12) ? 0xA05A00U : 0xB01010U));

	if (s <= 9)
	{
		snprintf(buf, sizeof(buf), "S%d", s);
	}
	else
	{
		snprintf(buf, sizeof(buf), "S9+%d", ((s - 9) * 10));
	}
	nsPrint(3, meterY, buf, FONT_SIZE_2, TEXT_ALIGN_LEFT, valDark, valLight);

	snprintf(buf, sizeof(buf), "%ddBm", dbm);
	nsPrint(0, meterY, buf, FONT_SIZE_1, TEXT_ALIGN_RIGHT, 0xB9C6DEU, 0x404858U);

	for (int i = 0; i < 15; i++)
	{
		if (i < s)
		{
			if (i <= 8)
			{
				nsColour(0x3BD16FU, 0x109A40U);
			}
			else if (i <= 11)
			{
				nsColour(0xFF9F40U, 0xD07000U);
			}
			else
			{
				nsColour(0xFF4A3DU, 0xD01818U);
			}
		}
		else
		{
			nsColour(0x24302AU, 0xD4D8DCU); // unlit
		}
		displayFillRect((5 + (i * 10)), (meterY + 9), 9, 6, false);
	}

	displayThemeResetToDefault();
}

// Clock (power while transmitting), battery voltage, temperature.
static void nsDrawFooter(void)
{
	char buf[16];
	int volts, mvolts;
	int temperature = getTemperature();

	nsClear(NS_FOOT_Y, 8);

	if (trxTransmissionEnabled)
	{
		uint8_t powerLevel = trxGetPowerLevel();

		snprintf(buf, sizeof(buf), "PWR %s%s", getPowerLevel(powerLevel), getPowerLevelUnit(powerLevel));
		nsPrint(3, NS_FOOT_Y, buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, 0xFFB347U, 0xA05A00U);
	}
	else if (clockText(buf, sizeof(buf)))
	{
		nsPrint(3, NS_FOOT_Y, buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, 0x9AA6BDU, 0x404858U);
	}

	getBatteryVoltage(&volts, &mvolts);
	snprintf(buf, sizeof(buf), "%d.%dV", volts, mvolts);
	nsPrint(0, NS_FOOT_Y, buf, FONT_SIZE_1, TEXT_ALIGN_CENTER, 0xFFD84AU, 0x806000U);

	snprintf(buf, sizeof(buf), "%d\xB0""C", (temperature / 10));
	nsPrint(0, NS_FOOT_Y, buf, FONT_SIZE_1, TEXT_ALIGN_RIGHT, 0x9AA6BDU, 0x404858U);

	displayThemeResetToDefault();
}

void uiChannelModeNsFooter(void)
{
	nsDrawFooter();
}

extern LinkItem_t *LinkHead; // uiUtilities.c: the Last Heard list, newest first

// DMR: who was heard last, and how long ago - "Last: TA8BMO Arda   2m". While a
// call is on, the classic caller screen shows the caller instead; this is what
// is left on the channel screen afterwards.
static void nsDrawLastHeard(void)
{
	char who[24];
	char age[8];
	const char *label = LANG5("Son:", "Last:", "\xDAlt:", "\xCF\xEE\xF1\xEB:", "Zul:");
	int16_t whoX = (int16_t) (3 + ((strlen(label) + 1) * 6));
	int maxChars;
	uint32_t secs;

	nsClear(NS_DMR_LAST_Y, 8);
	nsPrint(3, NS_DMR_LAST_Y, label, FONT_SIZE_1, TEXT_ALIGN_LEFT, 0x9AA6BDU, 0x404858U);

	if ((LinkHead == NULL) || (LinkHead->id == 0))
	{
		nsPrint(whoX, NS_DMR_LAST_Y, "-", FONT_SIZE_1, TEXT_ALIGN_LEFT, 0x9AA6BDU, 0x404858U);
		return;
	}

	secs = ((ticksGetMillis() - LinkHead->time) / 1000U);
	if (secs < 60)
	{
		snprintf(age, sizeof(age), "%us", (unsigned) secs);
	}
	else if (secs < 3600)
	{
		snprintf(age, sizeof(age), "%um", (unsigned) (secs / 60U));
	}
	else
	{
		snprintf(age, sizeof(age), "%uh", (unsigned) (secs / 3600U));
	}

	// The name the DMR ID database gives, else the talker alias, else the bare ID;
	// cut short so the age always fits at the right. An ID the database does not
	// know comes back as "ID:2864001" (dmrIDLookup) rather than empty, which showed
	// the number even when the station was sending its callsign as talker alias.
	// And, when the station sent its position (DMR GPS embedded data) and this
	// radio knows its own, how far and which way: "EB5IRA  2450km KD  3m".
	char dist[16];
	bool haveDist = uiAprsDistanceText(LinkHead->locationLat, LinkHead->locationLon, dist, sizeof(dist));
	int rightChars = (int) (strlen(age) + 1 + (haveDist ? (strlen(dist) + 1) : 0));

	maxChars = (int) ((152 - whoX - (rightChars * 6)) / 6);
	if (haveDist)
	{
		nsPrint((int16_t) (152 - (rightChars * 6) + 6), NS_DMR_LAST_Y, dist, FONT_SIZE_1, TEXT_ALIGN_LEFT, 0xFFB347U, 0xA05A00U);
	}

	if ((LinkHead->contact[0] != 0) && (strncmp(LinkHead->contact, "ID:", 3) != 0))
	{
		snprintf(who, sizeof(who), "%.*s", maxChars, LinkHead->contact);
	}
	else if (LinkHead->talkerAlias[0] != 0)
	{
		snprintf(who, sizeof(who), "%.*s", maxChars, LinkHead->talkerAlias);
	}
	else if (LinkHead->contact[0] != 0)
	{
		snprintf(who, sizeof(who), "%.*s", maxChars, LinkHead->contact);
	}
	else
	{
		snprintf(who, sizeof(who), "%u", (unsigned) LinkHead->id);
	}

	nsPrint(whoX, NS_DMR_LAST_Y, who, FONT_SIZE_1, TEXT_ALIGN_LEFT, 0x8FE3B0U, 0x207040U);
	nsPrint(0, NS_DMR_LAST_Y, age, FONT_SIZE_1, TEXT_ALIGN_RIGHT, 0x9AA6BDU, 0x404858U);
	displayThemeResetToDefault();
}

// The S-meter and, every two seconds, the footer: drawn and rendered on their
// own between full redraws.
static void nsLiveUpdate(uint32_t now)
{
	nsDrawMeter();
	displayRenderRows((nsMeterY() / 8), ((nsMeterY() + 16 + 7) / 8));

	if ((now - nsFooterTime) > 2000)
	{
		nsDrawFooter();
		displayRenderRows((NS_FOOT_Y / 8), (DISPLAY_SIZE_Y / 8));

		if ((trxGetMode() == RADIO_MODE_DIGITAL) && (trxTransmissionEnabled == false))
		{
			nsDrawLastHeard(); // its age
			displayRenderRows((NS_DMR_LAST_Y / 8), ((NS_DMR_LAST_Y + 8 + 7) / 8));
		}

		nsFooterTime = now;
	}
}

// Everything below the frequency lines, shared with the VFO: the talk time
// while transmitting, otherwise the labels, S-meter and last caller; then the
// footer.
void uiChannelModeNsLowerHalf(int txTimeSecs)
{
	char buf[SCREEN_LINE_BUFFER_SIZE];
	char other[16];

	if (trxGetMode() == RADIO_MODE_DIGITAL)
	{
		// DMR: the talkgroup or contact on a band of its own - amber when it is a
		// manual override, as the classic screen frames it - then the S-meter, or
		// the talk time while transmitting. The timeslot and colour code are in
		// the header.
		bool overridden = (nonVolatileSettings.overrideTG != 0);
		uint16_t pad = uiUtilityScreenColour((overridden ? 0x4A3410U : 0x0F3A4DU), (overridden ? 0xF8E6C0U : 0xD0E8F4U));

		if (overridden)
		{
			uiUtilityBuildTgOrPCDisplayName(buf, SCREEN_LINE_BUFFER_SIZE);
		}
		else
		{
			codeplugUtilConvertBufToString(currentContactData.name, buf, 16);
		}

		displaySetForegroundAndBackgroundColours(pad, pad);
		displayFillRoundRect(3, NS_DMR_TG_Y, (DISPLAY_SIZE_X - 6), 16, 3, false);
		displaySetForegroundAndBackgroundColours(uiUtilityScreenColour((overridden ? 0xFFC857U : 0x7FE3FFU), (overridden ? 0x7A4A00U : 0x0B4A70U)), pad);
		displayPrintCore(0, NS_DMR_TG_Y, buf, FONT_SIZE_3, TEXT_ALIGN_CENTER, false);

		if (trxTransmissionEnabled)
		{
			snprintf(buf, sizeof(buf), "%d:%02d", (txTimeSecs / 60), (txTimeSecs % 60));
			nsPrint(0, (NS_DMR_LAST_Y + 4), buf, FONT_SIZE_3, TEXT_ALIGN_CENTER, 0xFFFFFFU, 0x101010U);
		}
		else
		{
			nsDrawLastHeard();
			nsDrawMeter();
		}
	}
	else if (trxTransmissionEnabled)
	{
		// Talk time, big. The mic level is the thin bar under the header.
		snprintf(buf, sizeof(buf), "%d:%02d", (txTimeSecs / 60), (txTimeSecs % 60));
		nsPrint(0, NS_TIMER_Y, buf, FONT_SIZE_4, TEXT_ALIGN_CENTER, 0xFFFFFFU, 0x101010U);
	}
	else
	{
		// Labels, as many as fit before the squelch.
		int16_t x = 3;
		const int16_t maxX = 112;
		uint8_t digits[5];

		if (currentChannelData->txTone != CODEPLUG_CSS_TONE_NONE)
		{
			nsToneText(other, sizeof(other), 'T', currentChannelData->txTone);
			x = nsChip(x, maxX, other, 0x0F4D4AU, 0xCDEFEAU, 0x7FF2DFU, 0x0A5A52U);
		}
		if ((currentChannelData->rxTone != CODEPLUG_CSS_TONE_NONE) && (currentChannelData->rxTone != currentChannelData->txTone))
		{
			nsToneText(other, sizeof(other), 'R', currentChannelData->rxTone);
			x = nsChip(x, maxX, other, 0x0F4D4AU, 0xCDEFEAU, 0x7FF2DFU, 0x0A5A52U);
		}
		if (codeplugChannelGetFiveTone(currentChannelData, digits) != ANI_MODE_OFF)
		{
			x = nsChip(x, maxX, "ANI", 0x3A2A66U, 0xE2DAFAU, 0xD2C3FFU, 0x4A2A8AU);
		}
		if (morseDecodeHasTextForCurrentChannel())
		{
			x = nsChip(x, maxX, "CW", 0x4A1416U, 0xF8D4D4U, 0xFF8080U, 0xA01010U);
		}
		if (voiceRecorderGetEnabled() && (voiceRecorderGetStatus() == VOICE_RECORDER_STATUS_OK))
		{
			int16_t recX = x;

			x = nsChip(x, maxX, (voiceRecorderIsRecording() ? "  REC" : "REC"), 0x4A1416U, 0xF8D4D4U, 0xFFB3B3U, 0xA01010U);
			if ((x != recX) && voiceRecorderIsRecording())
			{
				nsColour(0xFF4D4DU, 0xD01010U);
				displayFillCircle((recX + 6), (NS_CHIPS_Y + 4), 2, false);
			}
		}

		snprintf(buf, sizeof(buf), "SQL %d%%", (5 * (((currentChannelData->sql == 0) ? nonVolatileSettings.squelchDefaults[currentRadioDevice->trxCurrentBand[TRX_RX_FREQ_BAND]] : currentChannelData->sql) - 1)));
		nsPrint(0, (NS_CHIPS_Y + 1), buf, FONT_SIZE_1, TEXT_ALIGN_RIGHT, 0x8FE3B0U, 0x207040U);

		nsDrawMeter();
		drawLastFiveToneCall();
	}

	nsDrawFooter();
	displayThemeResetToDefault();
}

static void drawNewChannelScreen(int txTimeSecs)
{
	char buf[SCREEN_LINE_BUFFER_SIZE];
	char other[16];
	uint32_t rxFreq = (uiDataGlobal.reverseRepeaterChannel ? currentChannelData->txFreq : currentChannelData->rxFreq);
	uint32_t txFreq = (uiDataGlobal.talkaround ? rxFreq : (uiDataGlobal.reverseRepeaterChannel ? currentChannelData->rxFreq : currentChannelData->txFreq));

	// Channel name, inverted for a reversed repeater channel as on the classic screen.
	codeplugUtilConvertBufToString(currentChannelData->name, buf, 16);
	nsColour(0xFFC857U, 0x8A5A00U);
	displayPrintCore(0, NS_NAME_Y, buf, FONT_SIZE_3, TEXT_ALIGN_CENTER, uiDataGlobal.reverseRepeaterChannel);

	if (trxTransmissionEnabled)
	{
		uiChannelModeNsFrequency(txFreq, 0xFF7A45U, 0xC03000U);
	}
	else
	{
		uiChannelModeNsFrequency(rxFreq, 0x4FD8FFU, 0x0068A0U);
	}

	// Zone and channel number (or the channel being typed in), and the other frequency.
	if (directChannelNumber > 0)
	{
		snprintf(buf, sizeof(buf), "%.10s %d", currentLanguage->gotoChannel, directChannelNumber);
	}
	else if (settingsIsOptionBitSet(BIT_DISPLAY_CHANNEL_DISTANCE) && (currentChannelData->NOT_IN_CODEPLUG_CALCULATED_DISTANCE_X10 != -1))
	{
		snprintf(buf, sizeof(buf), "%.8s %d.%dkm", currentZoneName,
				(currentChannelData->NOT_IN_CODEPLUG_CALCULATED_DISTANCE_X10 / 10), (currentChannelData->NOT_IN_CODEPLUG_CALCULATED_DISTANCE_X10 % 10));
	}
	else if (CODEPLUG_ZONE_IS_ALLCHANNELS(currentZone))
	{
		snprintf(buf, sizeof(buf), "%.10s %d", currentLanguage->all_channels, codeplugGetLastUsedChannelInCurrentZone());
	}
	else
	{
		snprintf(buf, sizeof(buf), "%.8s %d/%d", currentZoneName, (codeplugGetLastUsedChannelInCurrentZone() + 1), currentZone.NOT_IN_CODEPLUGDATA_numChannelsInZone);
	}
	nsPrint(3, NS_INFO_Y, buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, 0x9AA6BDU, 0x404858U);

	if (trxTransmissionEnabled)
	{
		snprintf(other, sizeof(other), "RX %u.%03u", (unsigned) (rxFreq / 100000U), (unsigned) ((rxFreq / 100U) % 1000U));
		nsPrint(0, NS_INFO_Y, other, FONT_SIZE_1, TEXT_ALIGN_RIGHT, 0x4FD8FFU, 0x0068A0U);
	}
	else if (txFreq != rxFreq)
	{
		snprintf(other, sizeof(other), "TX %u.%03u", (unsigned) (txFreq / 100000U), (unsigned) ((txFreq / 100U) % 1000U));
		nsPrint(0, NS_INFO_Y, other, FONT_SIZE_1, TEXT_ALIGN_RIGHT, 0xC9A7FFU, 0x6A3AAAU);
	}

	uiChannelModeNsLowerHalf(txTimeSecs);
}

// ---------------------------------------------------------------------------
// DMR Finder (TA8BMO): long-press 0 on a DMR channel or the VFO.
//
// The DMR counterpart of the tone finder. While it is open the radio hears
// every talkgroup, private call, timeslot and colour code on the frequency -
// the destination and CC/TS filters are switched off, and put back by the exit
// callback however the screen is left - and lists what it heard: TG or PC,
// timeslot, colour code, the last caller, how many calls and how long ago.
// Green puts the selected talkgroup and timeslot on the channel, as the Last
// Heard screen does. PTT is blocked here (it sits after MENU_GAMES).
//
// The filters are changed in the live settings without marking them for saving,
// so nothing reaches the flash unless something else saves while it is open.
// ---------------------------------------------------------------------------
#define DF_MAX   12
#define DF_ROWS  6
#define DF_ROW_Y 26

typedef struct
{
	uint32_t tgOrPc;  // with the PC flag in the top byte for a private call
	uint32_t lastMs;
	uint32_t src;     // the last caller's DMR ID, for their position in the Last Heard list
	uint16_t count;
	uint8_t  ts;      // 0 or 1
	uint8_t  cc;
	char     who[11]; // the last caller, as the DMR ID database names them
} dfEntry_t;

__attribute__((section(".ccmram"))) static dfEntry_t dfList[DF_MAX]; // CCM is not cleared at start: dfCount says what is valid
static int dfCount = 0;
static int dfSel = 0;
static uint32_t dfActiveTg = 0;
static uint32_t dfActiveSrc = 0;
static uint32_t dfLastDraw = 0;
static uint8_t dfSavedDestination = 0;
static uint8_t dfSavedCcTs = 0;
static bool dfFiltersOff = false;

static void dfExitCallback(void *data)
{
	(void) data;

	if (dfFiltersOff)
	{
		nonVolatileSettings.dmrDestinationFilter = dfSavedDestination;
		nonVolatileSettings.dmrCcTsFilter = dfSavedCcTs;
		trxSetDMRColourCode(currentChannelData->txColor); // the CC probe may have moved it
		dfFiltersOff = false;
	}
}

// Most recently heard first.
static void dfOrder(int order[DF_MAX])
{
	for (int i = 0; i < dfCount; i++)
	{
		order[i] = i;
	}

	for (int i = 1; i < dfCount; i++)
	{
		int v = order[i];
		int j = (i - 1);

		while ((j >= 0) && (dfList[order[j]].lastMs < dfList[v].lastMs))
		{
			order[j + 1] = order[j];
			j--;
		}
		order[j + 1] = v;
	}
}

// Notes the call being heard, if there is one. True when a new call started.
static bool dfCollect(uint32_t now)
{
	uint32_t tg = HRC6000GetReceivedTgOrPcId();
	uint32_t src = HRC6000GetReceivedSrcId();
	dfEntry_t *e = NULL;
	bool newCall;
	uint8_t ts;

	if ((tg == 0) || (src == 0) || trxTransmissionEnabled)
	{
		dfActiveTg = 0;
		dfActiveSrc = 0;
		return false;
	}

	ts = (uint8_t) ((dmrMonitorCapturedTS >= 0) ? dmrMonitorCapturedTS : trxGetDMRTimeSlot());

	for (int i = 0; i < dfCount; i++)
	{
		if ((dfList[i].tgOrPc == tg) && (dfList[i].ts == ts))
		{
			e = &dfList[i];
			break;
		}
	}

	if (e == NULL)
	{
		if (dfCount < DF_MAX)
		{
			e = &dfList[dfCount++];
		}
		else
		{
			e = &dfList[0]; // full: the one heard longest ago makes room
			for (int i = 1; i < DF_MAX; i++)
			{
				if (dfList[i].lastMs < e->lastMs)
				{
					e = &dfList[i];
				}
			}
		}
		memset(e, 0, sizeof(*e));
		e->tgOrPc = tg;
		e->ts = ts;
	}

	newCall = ((tg != dfActiveTg) || (src != dfActiveSrc));
	if (newCall)
	{
		e->count++;
	}

	// Looked up all through the call: the talker alias only arrives a moment
	// after the call starts. A name from the DMR ID database first, else the
	// talker alias, else the ID ("ID:..." is the database saying it does not know).
	{
		LinkItem_t *item = lastHeardFindInList(src);

		if ((item != NULL) && (item->contact[0] != 0) && (strncmp(item->contact, "ID:", 3) != 0))
		{
			snprintf(e->who, sizeof(e->who), "%s", item->contact);
		}
		else if ((item != NULL) && (item->talkerAlias[0] != 0))
		{
			snprintf(e->who, sizeof(e->who), "%s", item->talkerAlias);
		}
		else
		{
			snprintf(e->who, sizeof(e->who), "%u", (unsigned) src);
		}
	}

	e->lastMs = now;
	e->src = src;
	e->cc = trxGetDMRColourCode();
	dfActiveTg = tg;
	dfActiveSrc = src;

	return newCall;
}

static void dfDraw(void)
{
	char buf[24];
	int order[DF_MAX];
	int top = ((dfSel < DF_ROWS) ? 0 : ((dfSel - DF_ROWS) + 1));
	uint32_t now = ticksGetMillis();

	dfOrder(order);
	displayClearBuf();
	menuDisplayTitle(LANG5("DMR BULUCU", "DMR FINDER", "BUSCADOR DMR", "\xCF\xCE\xC8\xD1\xCA DMR", "DMR SUCHE"));

	// What the radio hears right now.
	if (dmrMonitorCapturedTS >= 0)
	{
		snprintf(buf, sizeof(buf), "CC%u  TS%d  %ddBm", (unsigned) trxGetDMRColourCode(), (dmrMonitorCapturedTS + 1), trxGetRSSIdBm(RADIO_DEVICE_PRIMARY));
	}
	else
	{
		snprintf(buf, sizeof(buf), "CC%u  TS-  %ddBm", (unsigned) trxGetDMRColourCode(), trxGetRSSIdBm(RADIO_DEVICE_PRIMARY));
	}
	nsPrint(0, 16, buf, FONT_SIZE_1, TEXT_ALIGN_CENTER, 0x7FD0FFU, 0x0B4A88U);

	if (dfCount == 0)
	{
		nsPrint(0, 56, LANG5("Dinleniyor...", "Listening...", "Escuchando...", "\xD1\xEB\xF3\xF8\xE0\xFE...", "H\xF6re..."), FONT_SIZE_3, TEXT_ALIGN_CENTER, 0xC8D2E6U, 0x203050U);
		nsPrint(0, 80, LANG5("Duyulan TG'ler burada", "Heard TGs appear here", "Aqu\xED salen los TG", "\xC7\xE4\xE5\xF1\xFC \xE1\xF3\xE4\xF3\xF2 TG", "Geh\xF6rte TGs hier"), FONT_SIZE_1, TEXT_ALIGN_CENTER, 0x9AA6BDU, 0x404858U);
	}

	for (int r = 0; r < DF_ROWS; r++)
	{
		int k = (top + r);
		const dfEntry_t *e;
		int16_t y = (int16_t) (DF_ROW_Y + (r * 16));
		bool focused;
		uint16_t bg;
		uint32_t secs;

		if (k >= dfCount)
		{
			break;
		}

		e = &dfList[order[k]];
		focused = (k == dfSel);
		bg = nsThemeBg();

		if (focused)
		{
			bg = uiUtilityScreenColour(0x1F4F8FU, 0x1F5FC0U);
			displaySetForegroundAndBackgroundColours(bg, bg);
			displayFillRoundRect(3, y, 151, 16, 3, false);
			displaySetForegroundAndBackgroundColours(uiUtilityScreenColour(0x4FD8FFU, 0x0B2A60U), bg);
			displayFillRect(3, y, 3, 16, false);
		}

		// TG 91 / PC 2864001, and its timeslot and colour code.
		snprintf(buf, sizeof(buf), "%s %u", (((e->tgOrPc >> 24) == PC_CALL_FLAG) ? "PC" : "TG"), (unsigned) (e->tgOrPc & 0x00FFFFFFU));
		displaySetForegroundAndBackgroundColours((focused ? uiUtilityScreenColour(0xFFFFFFU, 0xFFFFFFU) : uiUtilityScreenColour(0x7FE3FFU, 0x0B4A70U)), bg);
		displayPrintCore(8, y, buf, FONT_SIZE_2, TEXT_ALIGN_LEFT, false);

		snprintf(buf, sizeof(buf), "TS%u CC%u", (unsigned) (e->ts + 1), (unsigned) e->cc);
		displaySetForegroundAndBackgroundColours((focused ? uiUtilityScreenColour(0xFFD84AU, 0xFFE38AU) : uiUtilityScreenColour(0xFFB347U, 0xA05A00U)), bg);
		displayPrintCore((int16_t) (152 - (strlen(buf) * 6)), y, buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);

		// The last caller - and how far and which way, when they sent a position -
		// then how many calls and how long ago.
		{
			LinkItem_t *item = lastHeardFindInList(e->src);
			char dist[16];
			char who[12];
			bool haveDist = ((item != NULL) && uiAprsDistanceText(item->locationLat, item->locationLon, dist, sizeof(dist)));

			secs = ((now - e->lastMs) / 1000U);
			if (secs < 60)
			{
				snprintf(buf, sizeof(buf), "%ux %us", (unsigned) e->count, (unsigned) secs);
			}
			else
			{
				snprintf(buf, sizeof(buf), "%ux %um", (unsigned) e->count, (unsigned) (secs / 60U));
			}

			// The name gets what is left of the line's 24 characters.
			snprintf(who, sizeof(who), "%.*s", (24 - (int) strlen(buf) - 1 - (haveDist ? ((int) strlen(dist) + 1) : 0)), e->who);
			displaySetForegroundAndBackgroundColours((focused ? uiUtilityScreenColour(0xFFFFFFU, 0xFFFFFFU) : uiUtilityScreenColour(0xC8D2E6U, 0x203050U)), bg);
			displayPrintCore(8, (int16_t) (y + 8), who, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);

			if (haveDist)
			{
				displaySetForegroundAndBackgroundColours((focused ? uiUtilityScreenColour(0xFFD84AU, 0xFFE38AU) : uiUtilityScreenColour(0xFFB347U, 0xA05A00U)), bg);
				displayPrintCore((int16_t) (152 - ((strlen(buf) + 1 + strlen(dist)) * 6)), (int16_t) (y + 8), dist, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
			}
		}
		displaySetForegroundAndBackgroundColours((focused ? uiUtilityScreenColour(0xFFFFFFU, 0xFFFFFFU) : uiUtilityScreenColour(0x9AA6BDU, 0x404858U)), bg);
		displayPrintCore((int16_t) (152 - (strlen(buf) * 6)), (int16_t) (y + 8), buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	}

	displayThemeResetToDefault();
	displayRender();
}

menuStatus_t menuDMRFinder(uiEvent_t *ev, bool isFirstRun)
{
	bool redraw = false;

	if (isFirstRun)
	{
		if (dfFiltersOff == false)
		{
			dfSavedDestination = nonVolatileSettings.dmrDestinationFilter;
			dfSavedCcTs = nonVolatileSettings.dmrCcTsFilter;
			nonVolatileSettings.dmrDestinationFilter = DMR_DESTINATION_FILTER_NONE;
			nonVolatileSettings.dmrCcTsFilter = DMR_CCTS_FILTER_NONE;
			dfFiltersOff = true;
		}

		dfCount = 0;
		dfSel = 0;
		dfActiveTg = 0;
		dfActiveSrc = 0;
		menuDataGlobal.numItems = 0;
		menuSystemRegisterExitCallback(dfExitCallback, NULL);

		dfDraw();
		dfLastDraw = ticksGetMillis();
		return MENU_STATUS_SUCCESS;
	}

	redraw = dfCollect(ev->time);

	if (ev->hasEvent && (ev->events & KEY_EVENT))
	{
		if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			menuSystemPopPreviousMenu();
			return MENU_STATUS_SUCCESS;
		}
		else if ((KEYCHECK_SHORTUP(ev->keys, KEY_DOWN) || KEYCHECK_SHORTUP(ev->keys, KEY_RIGHT)) && (dfCount > 0))
		{
			dfSel = ((dfSel + 1) % dfCount);
			redraw = true;
		}
		else if ((KEYCHECK_SHORTUP(ev->keys, KEY_UP) || KEYCHECK_SHORTUP(ev->keys, KEY_LEFT)) && (dfCount > 0))
		{
			dfSel = ((dfSel == 0) ? (dfCount - 1) : (dfSel - 1));
			redraw = true;
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN) && (dfCount > 0))
		{
			// The talkgroup (or private call) and timeslot onto the channel, as Last Heard does.
			int order[DF_MAX];
			const dfEntry_t *e;

			dfOrder(order);
			e = &dfList[order[dfSel]];

			setOverrideTGorPC((e->tgOrPc & 0x00FFFFFFU), ((e->tgOrPc >> 24) == PC_CALL_FLAG));
			if (e->ts != trxGetDMRTimeSlot())
			{
				trxSetDMRTimeSlot(e->ts, true);
				tsSetManualOverride(((menuSystemGetRootMenuNumber() == UI_CHANNEL_MODE) ? CHANNEL_CHANNEL : (CHANNEL_VFO_A + nonVolatileSettings.currentVFONumber)), (e->ts + 1));
			}

			announceItem(PROMPT_SEQUENCE_CONTACT_TG_OR_PC, PROMPT_THRESHOLD_3);
			menuSystemPopAllAndDisplayRootMenu();
			return MENU_STATUS_SUCCESS;
		}
	}

	// Once a second as well, for the ages and the live line.
	if (redraw || ((ev->time - dfLastDraw) > 1000))
	{
		dfDraw();
		dfLastDraw = ev->time;
	}

	return MENU_STATUS_SUCCESS;
}

void uiChannelModeUpdateScreen(int txTimeSecs)
{
	char nameBuf[NAME_BUFFER_LEN];
	char buffer[SCREEN_LINE_BUFFER_SIZE];

	// Only render the header, then wait for the next run
	// Otherwise the screen could remain blank if TG and PC are == 0
	// since uiDataGlobal.displayQSOState won't be set to QSO_DISPLAY_IDLE
	if ((trxGetMode() == RADIO_MODE_DIGITAL) && (HRC6000GetReceivedTgOrPcId() == 0) &&
			((uiDataGlobal.displayQSOState == QSO_DISPLAY_CALLER_DATA) || (uiDataGlobal.displayQSOState == QSO_DISPLAY_CALLER_DATA_UPDATE)))
	{
		uiUtilityRedrawHeaderOnly(false, false);
		return;
	}

	// We're currently displaying details, and it shouldn't be overridden by QSO data
	if (uiDataGlobal.displayChannelSettings && ((uiDataGlobal.displayQSOState == QSO_DISPLAY_CALLER_DATA)
			|| (uiDataGlobal.displayQSOState == QSO_DISPLAY_CALLER_DATA_UPDATE)))
	{
		// We will not restore the previous QSO Data as a new caller just arose.
		uiDataGlobal.displayQSOStatePrev = QSO_DISPLAY_DEFAULT_SCREEN;
		uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
	}

	displayClearBuf();
	uiUtilityRenderHeader(false, false);

	switch(uiDataGlobal.displayQSOState)
	{
		case QSO_DISPLAY_DEFAULT_SCREEN:
			lastHeardClearLastID();
			uiDataGlobal.displayQSOStatePrev = QSO_DISPLAY_DEFAULT_SCREEN;
			uiDataGlobal.isDisplayingQSOData = false;
			uiDataGlobal.receivedPcId = 0x00;

			if (nsActive())
			{
				drawNewChannelScreen(txTimeSecs);
				displayRender();
				break;
			}

			if (trxTransmissionEnabled)
			{
				snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, " %d ", txTimeSecs);
				uiUtilityDisplayInformation(buffer, DISPLAY_INFO_TX_TIMER, -1);
			}
			else
			{
				// Display some channel settings
				if (uiDataGlobal.displayChannelSettings)
				{
					uint32_t rxFreq = (uiDataGlobal.reverseRepeaterChannel ? currentChannelData->txFreq : currentChannelData->rxFreq);
					uint32_t txFreq = (uiDataGlobal.talkaround ? rxFreq : (uiDataGlobal.reverseRepeaterChannel ? currentChannelData->rxFreq : currentChannelData->txFreq));

					uiUtilityDisplayInformation(NULL, DISPLAY_INFO_CHANNEL_DETAILS, -1);

					uiUtilityDisplayFrequency(DISPLAY_Y_POS_RX_FREQ, false, false, rxFreq, false, false, 0);
					uiUtilityDisplayFrequency(DISPLAY_Y_POS_TX_FREQ, true, false, txFreq, false, false, 0);
				}
				else
				{
					if (directChannelNumber > 0)
					{
						snprintf(nameBuf, NAME_BUFFER_LEN, "%s %d", currentLanguage->gotoChannel, directChannelNumber);
					}
					else
					{
						if (((uiDataGlobal.Scan.active == false) || (uiDataGlobal.Scan.active && (uiDataGlobal.Scan.state == SCAN_STATE_PAUSED))) &&
								(settingsIsOptionBitSet(BIT_DISPLAY_CHANNEL_DISTANCE) || (settingsIsOptionBitSet(BIT_SORT_CHANNEL_DISTANCE) && (CODEPLUG_ZONE_IS_ALLCHANNELS(currentZone) == false))))
						{
							if (currentChannelData->NOT_IN_CODEPLUG_CALCULATED_DISTANCE_X10 != -1)
							{
								int decLen;
								char distBuf[SCREEN_LINE_BUFFER_SIZE];
								char zNameBuf[SCREEN_LINE_BUFFER_SIZE];
								uint32_t intPart = (currentChannelData->NOT_IN_CODEPLUG_CALCULATED_DISTANCE_X10 / 10);
								uint32_t decPart = (currentChannelData->NOT_IN_CODEPLUG_CALCULATED_DISTANCE_X10 - (intPart * 10));

								if (decPart != 0)
								{
									decLen = snprintf(distBuf, SCREEN_LINE_BUFFER_SIZE, "| %u.%u km", intPart, decPart);
								}
								else
								{
									decLen = snprintf(distBuf, SCREEN_LINE_BUFFER_SIZE, "| %u km", intPart);
								}

								int zLen = snprintf(zNameBuf, SCREEN_LINE_BUFFER_SIZE, "%s", currentZoneName);

								// Truncate (with ellipsis) the zone name if the whole string doesn't fit the screen
								if ((zLen + decLen + 1) > (NAME_BUFFER_LEN - 1))
								{
									int nOffset = MAX(0, (zLen - (((decLen + zLen + 1) - (NAME_BUFFER_LEN - 1)) + 3)));

									memset(zNameBuf + nOffset, '.', 3);
									zNameBuf[nOffset + 3] = 0;
							     }

								snprintf(nameBuf, NAME_BUFFER_LEN, "%s %s", zNameBuf, distBuf);
							}
							else
							{
								snprintf(nameBuf, NAME_BUFFER_LEN, "%s | --- km", currentZoneName);
							}
						}
						else
						{
							snprintf(nameBuf, NAME_BUFFER_LEN, "%s Ch:%d",
									(CODEPLUG_ZONE_IS_ALLCHANNELS(currentZone) ? currentLanguage->all_channels : currentZoneName),
									(codeplugGetLastUsedChannelInCurrentZone() + (CODEPLUG_ZONE_IS_ALLCHANNELS(currentZone) ? 0 : 1)));
						}
					}

					uiUtilityDisplayInformation(nameBuf, DISPLAY_INFO_ZONE, -2);
				}
			}

			if (!uiDataGlobal.displayChannelSettings)
			{
#if defined(PLATFORM_MD9600)
				if (codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_OUT_OF_BAND) != 0)
				{
					snprintf(nameBuf, NAME_BUFFER_LEN, "%s", currentLanguage->out_of_band);
				}
				else
#endif
				{
					codeplugUtilConvertBufToString(currentChannelData->name, nameBuf, 16);
				}

				uiUtilityDisplayInformation(nameBuf, ((uiDataGlobal.reverseRepeaterChannel == true) ? DISPLAY_INFO_CHANNEL_INVERTED : DISPLAY_INFO_CHANNEL), (trxTransmissionEnabled ? DISPLAY_Y_POS_CHANNEL_SECOND_LINE : -1));
			}

			if (trxGetMode() == RADIO_MODE_DIGITAL)
			{
				if (!uiDataGlobal.displayChannelSettings)
				{
					if (nonVolatileSettings.overrideTG != 0)
					{
						uiUtilityBuildTgOrPCDisplayName(nameBuf, SCREEN_LINE_BUFFER_SIZE);
						uiUtilityDisplayInformation(NULL, DISPLAY_INFO_CONTACT_OVERRIDE_FRAME, (trxTransmissionEnabled ? DISPLAY_Y_POS_CONTACT_TX_FRAME : -1));
					}
					else
					{
						codeplugUtilConvertBufToString(currentContactData.name, nameBuf, 16);
					}

					uiUtilityDisplayInformation(nameBuf, DISPLAY_INFO_CONTACT, (trxTransmissionEnabled ? DISPLAY_Y_POS_CONTACT_TX : -1));
				}

			}

			if (!trxTransmissionEnabled)
			{
				drawChannelRSSIMeter(); // always-on coloured S-meter + numeric S-value
				drawLastFiveToneCall(); // last 5-Tone caller, held until superseded
				drawMorseWaitingMarker();
			}
			displayRender();
			break;

		case QSO_DISPLAY_CALLER_DATA:
		case QSO_DISPLAY_CALLER_DATA_UPDATE:
			uiDataGlobal.displayQSOStatePrev = QSO_DISPLAY_CALLER_DATA;
			uiDataGlobal.isDisplayingQSOData = true; // kept: SK2 then takes the caller's talkgroup
			uiDataGlobal.displayChannelSettings = false;

			// The colour screen stays up while a DMR call comes in; the caller is
			// on its "Last:" line, as the Last Heard list has them.
			if (nsActive())
			{
				drawNewChannelScreen(0);
				displayRender();
				break;
			}

			uiUtilityRenderQSOData();
			if (!trxTransmissionEnabled)
			{
				drawChannelRSSIMeter(); // always-on coloured S-meter + numeric S-value
				drawLastFiveToneCall(); // last 5-Tone caller, held until superseded
				drawMorseWaitingMarker();
			}
			displayRender();
			break;

		case QSO_DISPLAY_IDLE:
			break;
	}

	uiDataGlobal.displayQSOState = QSO_DISPLAY_IDLE;
}

static void handleEvent(uiEvent_t *ev)
{
#if defined(PLATFORM_GD77S)
	handleEventForGD77S(ev);
	return;
#else

	if (uiDataGlobal.Scan.active && (ev->events & KEY_EVENT))
	{
		// Key pressed during scanning

		if (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0)
		{
			// if we are scanning and down key is pressed then enter current channel into nuisance delete array.
			if((uiDataGlobal.Scan.state == SCAN_STATE_PAUSED) &&
#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380)
					(ev->keys.key == KEY_STAR)
#else
					(ev->keys.key == KEY_RIGHT)
#endif
			)
			{
				// There is two channels available in the Zone, just stop scanning
				if (uiDataGlobal.Scan.nuisanceDeleteIndex == (uiDataGlobal.Scan.availableChannelsCount - 2))
				{
					uiDataGlobal.Scan.lastIteration = true;
				}

				uiDataGlobal.Scan.nuisanceDelete[uiDataGlobal.Scan.nuisanceDeleteIndex] = uiDataGlobal.currentSelectedChannelNumber;
				uiDataGlobal.Scan.nuisanceDeleteIndex = (uiDataGlobal.Scan.nuisanceDeleteIndex + 1) % MAX_ZONE_SCAN_NUISANCE_CHANNELS;
				uiDataGlobal.Scan.timer.timeout = SCAN_SKIP_CHANNEL_INTERVAL;	//force scan to continue;
				uiDataGlobal.Scan.state = SCAN_STATE_SCANNING;
				keyboardReset();
				return;
			}

			// KEY_DOWN/KEY_LEFT reverse the scan direction
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
				return;
			}
		}

		// stop the scan on any button except:
		//    - MD9600: UP without Shift (allows scan to be manually continued), or Right
		//    - MDUV380/DM1701/MD2017: UP/ROTARY_INC without SK2 (allows scan to be manually continued)
		//    - MK22: UP without Shift (allows scan to be manually continued)
		// or SK2 on its own (allows Backlight to be triggered)
		if ((
#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
				((ev->keys.key == KEY_FRONT_UP) || (ev->keys.key == KEY_ROTARY_INCREMENT))
#elif defined(PLATFORM_MD9600)
				((ev->keys.key == KEY_UP) || (ev->keys.key == KEY_RIGHT))
#else
				(ev->keys.key == KEY_UP)
#endif
				&& (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0)) == false)
		{
			scanStop(true);
			keyboardReset();
			return;
		}
	}

	if (ev->events & FUNCTION_EVENT)
	{
		if (ev->function == FUNC_START_SCANNING)
		{
			directChannelNumber = 0;
			if (uiDataGlobal.Scan.active == false)
			{
				scanStart(false);
			}
			return;
		}
		else if (ev->function == FUNC_REDRAW)
		{
			uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
			uiChannelModeUpdateScreen(0);
			return;
		}
	}

	if (handleMonitorMode(ev))
	{
		uiDataGlobal.displayChannelSettings = false;
		return;
	}

	if (ev->events & BUTTON_EVENT)
	{
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
				tsSetManualOverride(CHANNEL_CHANNEL, (dmrMonitorCapturedTS + 1));
			}

			if (trxTalkGroupOrPcId != tg)
			{
				trxTalkGroupOrPcId = tg;
				settingsSet(nonVolatileSettings.overrideTG, trxTalkGroupOrPcId);
			}

			currentChannelData->txColor = trxGetDMRColourCode();// Set the CC to the current CC, which may have been determined by the CC finding algorithm in C6000.c

			announceItem(PROMPT_SEQUENCE_CONTACT_TG_OR_PC,PROMPT_THRESHOLD_NEVER_PLAY_IMMEDIATELY);

			uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
			uiChannelModeUpdateScreen(0);
			uiDataGlobal.displayQSOState = QSO_DISPLAY_CALLER_DATA_UPDATE;
			soundSetMelody(MELODY_ACK_BEEP);
			return;
		}

		// Display channel settings (RX/TX/etc) while SK1 is pressed
		if ((uiDataGlobal.displayChannelSettings == false) && (monitorModeData.isEnabled == false) && BUTTONCHECK_DOWN(ev, BUTTON_SK1))
		{
			if ((uiDataGlobal.Scan.active == false) || (uiDataGlobal.Scan.active && (uiDataGlobal.Scan.state == SCAN_STATE_PAUSED)))
			{
				int prevQSODisp = uiDataGlobal.displayQSOStatePrev;
				uiDataGlobal.displayChannelSettings = true;
				uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
				uiChannelModeUpdateScreen(0);
				uiDataGlobal.displayQSOStatePrev = prevQSODisp;
			}
			return;

		}
		else if ((uiDataGlobal.displayChannelSettings == true) && (BUTTONCHECK_DOWN(ev, BUTTON_SK1) == 0))
		{
			hidesChannelDetails();
			return;
		}

#if !defined(PLATFORM_RD5R)
		if (BUTTONCHECK_SHORTUP(ev, BUTTON_ORANGE) && (BUTTONCHECK_DOWN(ev, BUTTON_SK1) == 0))
		{
			if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				announceItem(PROMPT_SEQUENCE_BATTERY, AUDIO_PROMPT_MODE_VOICE_LEVEL_1);
			}
			else
			{
				// Quick Menu
				menuSystemPushNewMenu(UI_CHANNEL_QUICK_MENU);

				// Trick to beep (AudioAssist), since ORANGE button doesn't produce any beep event
				ev->keys.event |= KEY_MOD_UP;
				ev->keys.key = 127;
				menuChannelExitStatus |= (MENU_STATUS_LIST_TYPE | MENU_STATUS_FORCE_FIRST);
				// End Trick
			}

			return;
		}
#endif
	}

	if (ev->events & KEY_EVENT)
	{
#if defined(PLATFORM_MD9600)
		if (KEYCHECK_LONGDOWN(ev->keys, KEY_GREEN))
		{
			if (uiDataGlobal.Scan.active)
			{
				uiChannelModeStopScanning();
			}

			menuSystemPushNewMenu(UI_CHANNEL_QUICK_MENU);

			// Trick to beep (AudioAssist), since ORANGE button doesn't produce any beep event
			ev->keys.event |= KEY_MOD_UP;
			ev->keys.key = 127;
			menuChannelExitStatus |= (MENU_STATUS_LIST_TYPE | MENU_STATUS_FORCE_FIRST);
			// End Trick
			return;
		}
		else
#endif
		if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			if (directChannelNumber > 0)
			{
				if(CODEPLUG_ZONE_IS_ALLCHANNELS(currentZone))
				{
					// All Channels virtual zone
					if (codeplugAllChannelsIndexIsInUse(directChannelNumber))
					{
						codeplugSetLastUsedChannelInZone(currentZone.NOT_IN_CODEPLUGDATA_indexNumber, directChannelNumber);
						uiChannelModeLoadChannelData(false, true);

					}
					else
					{
						soundSetMelody(MELODY_ERROR_BEEP);
					}
				}
				else
				{
					if ((directChannelNumber - 1) < currentZone.NOT_IN_CODEPLUGDATA_numChannelsInZone)
					{
						codeplugSetLastUsedChannelInZone(currentZone.NOT_IN_CODEPLUGDATA_indexNumber, (directChannelNumber - 1));
						uiChannelModeLoadChannelData(false, true);
					}
					else
					{
						soundSetMelody(MELODY_ERROR_BEEP);
					}

				}
				directChannelNumber = 0;
				uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
				uiChannelModeUpdateScreen(0);
			}
			else if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				menuSystemPushNewMenu(MENU_CHANNEL_DETAILS);
			}
			else
			{
				menuSystemPushNewMenu(MENU_MAIN_MENU);
			}
			return;
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
		{
			if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				// Assignment for SK2 + #
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
		else if (KEYCHECK_LONGDOWN(ev->keys, KEY_0) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_0) == false)
					&& (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0) && (trxGetMode() == RADIO_MODE_DIGITAL) && (settingsUsbMode != USB_MODE_HOTSPOT))
		{
			// Long-press 0 on DMR: the DMR finder (talkgroups, timeslot and colour code heard).
			menuSystemPushNewMenu(UI_DMR_FINDER);
			return;
		}
		else if (KEYCHECK_LONGDOWN(ev->keys, KEY_RED) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_RED) == false) && BUTTONCHECK_DOWN(ev, BUTTON_SK1))
		{
			uiChannelModeOrVFOModeThemeDaytimeChange(false, true);
			return;
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			if (BUTTONCHECK_DOWN(ev, BUTTON_SK1))
			{
				uiChannelModeOrVFOModeThemeDaytimeChange(true, true);
				return;
			}
			else if (BUTTONCHECK_DOWN(ev, BUTTON_SK2) && (uiDataGlobal.tgBeforePcMode != 0))
			{
				settingsSet(nonVolatileSettings.overrideTG, uiDataGlobal.tgBeforePcMode);
				menuPrivateCallClear();

				updateTrxID();
				uiDataGlobal.displayQSOState= QSO_DISPLAY_DEFAULT_SCREEN;// Force redraw
				uiChannelModeUpdateScreen(0);
				return;// The event has been handled
			}

			if(directChannelNumber > 0)
			{
				announceItem(PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ, PROMPT_THRESHOLD_NEVER_PLAY_IMMEDIATELY);

				directChannelNumber = 0;
				uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
				uiChannelModeUpdateScreen(0);
			}
			else
			{
#if ! (defined(PLATFORM_DM1801) || defined(PLATFORM_RD5R))
				menuSystemSetCurrentMenu(UI_VFO_MODE);
#endif
				return;
			}
		}
#if defined(PLATFORM_DM1801) || defined(PLATFORM_RD5R)
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_VFO_MR))
		{
			directChannelNumber = 0;
			menuSystemSetCurrentMenu(UI_VFO_MODE);
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
				menuSystemPushNewMenu(UI_CHANNEL_QUICK_MENU);

				// Trick to beep (AudioAssist), since ORANGE button doesn't produce any beep event
				ev->keys.event |= KEY_MOD_UP;
				ev->keys.key = 127;
				menuChannelExitStatus |= (MENU_STATUS_LIST_TYPE | MENU_STATUS_FORCE_FIRST);
				// End Trick
			}

			return;
		}
#endif
		else if (KEYCHECK_LONGDOWN(ev->keys, KEY_INCREASE) // set as KEY_RIGHT on some platforms
#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380)
				&& BUTTONCHECK_DOWN(ev, BUTTON_SK2)
#endif
		)
		{
			// Long press allows the 5W+ power setting to be selected immediately
#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380)
			if (increasePowerLevel(true))
			{
				uiUtilityRedrawHeaderOnly(false, false);
			}
#else
			if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				if (increasePowerLevel(true))
				{
					uiUtilityRedrawHeaderOnly(false, false);
				}
			}
#endif
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_INCREASE)) // set as KEY_RIGHT on some platforms
#if defined(PLATFORM_MD9600) || defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380)
		{
			if (uiDataGlobal.Scan.active == true)
			{
				handleUpKey(ev);
			}
			else
#endif
			{
				if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
				{
					if (increasePowerLevel(false))
					{
						uiUtilityRedrawHeaderOnly(false, false);
					}
				}
				else
				{
					if (trxGetMode() == RADIO_MODE_DIGITAL)
					{
						if (currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup > 1)
						{
							if (nonVolatileSettings.overrideTG == 0)
							{
								settingsIncrement(nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE], 1);
								if (nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE] > (currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup - 1))
								{
									settingsSet(nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE], 0);
									menuChannelExitStatus |= (MENU_STATUS_LIST_TYPE | MENU_STATUS_FORCE_FIRST);
								}
							}
						}
						settingsSet(nonVolatileSettings.overrideTG, 0);// setting the override TG to 0 indicates the TG is not overridden
						menuPrivateCallClear();
						updateTrxID();
						// We're in digital mode, RXing, and current talker is already at the top of last heard list,
						// hence immediately display complete contact/TG info on screen
						uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;//(isQSODataAvailableForCurrentTalker() ? QSO_DISPLAY_CALLER_DATA : QSO_DISPLAY_DEFAULT_SCREEN);
						if (isQSODataAvailableForCurrentTalker())
						{
							(void)addTimerCallback(uiUtilityRenderQSODataAndUpdateScreen, 2000, UI_CHANNEL_MODE, true);
						}
						uiChannelModeUpdateScreen(0);
						announceItem(PROMPT_SEQUENCE_CONTACT_TG_OR_PC, PROMPT_THRESHOLD_3);
					}
					else
					{
						if(currentChannelData->sql == 0)			//If we were using default squelch level
						{
							currentChannelData->sql = nonVolatileSettings.squelchDefaults[currentRadioDevice->trxCurrentBand[TRX_RX_FREQ_BAND]];			//start the adjustment from that point.
						}

						if (currentChannelData->sql < CODEPLUG_MAX_VARIABLE_SQUELCH)
						{
							currentChannelData->sql++;
						}

						announceItem(PROMPT_SQUENCE_SQUELCH, PROMPT_THRESHOLD_3);

						uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
						uiNotificationShow(NOTIFICATION_TYPE_SQUELCH, NOTIFICATION_ID_SQUELCH, 1000, NULL, false);
						uiChannelModeUpdateScreen(0);
					}
				}
#if defined(PLATFORM_MD9600) || defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380)
			}
#endif
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_DECREASE)) // set as KEY_LEFT on some platforms
		{
			if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				if (decreasePowerLevel())
				{
					uiUtilityRedrawHeaderOnly(false, false);
				}

				if (trxGetPowerLevel() == 0)
				{
					menuChannelExitStatus |= (MENU_STATUS_LIST_TYPE | MENU_STATUS_FORCE_FIRST);
				}
			}
			else
			{
				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					if (currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup > 1)
					{
						// To Do change TG in on same channel freq
						if (nonVolatileSettings.overrideTG == 0)
						{
							settingsDecrement(nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE], 1);
							if (nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE] < 0)
							{
								settingsSet(nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE],
										(int16_t) (currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup - 1));
							}

							if (nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE] == 0)
							{
								menuChannelExitStatus |= (MENU_STATUS_LIST_TYPE | MENU_STATUS_FORCE_FIRST);
							}
						}
					}
					settingsSet(nonVolatileSettings.overrideTG, 0);// setting the override TG to 0 indicates the TG is not overridden
					menuPrivateCallClear();
					updateTrxID();
					// We're in digital mode, RXing, and current talker is already at the top of last heard list,
					// hence immediately display complete contact/TG info on screen
					uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;//(isQSODataAvailableForCurrentTalker() ? QSO_DISPLAY_CALLER_DATA : QSO_DISPLAY_DEFAULT_SCREEN);
					if (isQSODataAvailableForCurrentTalker())
					{
						(void)addTimerCallback(uiUtilityRenderQSODataAndUpdateScreen, 2000, UI_CHANNEL_MODE, true);
					}
					uiChannelModeUpdateScreen(0);
					announceItem(PROMPT_SEQUENCE_CONTACT_TG_OR_PC,PROMPT_THRESHOLD_3);
				}
				else
				{
					if(currentChannelData->sql == 0)			//If we were using default squelch level
					{
						currentChannelData->sql = nonVolatileSettings.squelchDefaults[currentRadioDevice->trxCurrentBand[TRX_RX_FREQ_BAND]];			//start the adjustment from that point.
					}

					if (currentChannelData->sql > CODEPLUG_MIN_VARIABLE_SQUELCH)
					{
						currentChannelData->sql--;
					}

					announceItem(PROMPT_SQUENCE_SQUELCH, PROMPT_THRESHOLD_3);

					uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
					uiNotificationShow(NOTIFICATION_TYPE_SQUELCH, NOTIFICATION_ID_SQUELCH, 1000, NULL, false);
					uiChannelModeUpdateScreen(0);
				}

			}
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
		{
			if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))  // Toggle Channel Mode
			{
				if (trxGetMode() == RADIO_MODE_ANALOG)
				{
					currentChannelData->chMode = RADIO_MODE_DIGITAL;
					uiDataGlobal.VoicePrompts.inhibitInitial = true;// Stop VP playing in loadChannelData
					uiChannelModeLoadChannelData(true, false);
					uiDataGlobal.VoicePrompts.inhibitInitial = false;
					menuChannelExitStatus |= MENU_STATUS_FORCE_FIRST;
				}
				else
				{
					currentChannelData->chMode = RADIO_MODE_ANALOG;
					trxSetModeAndBandwidth(currentChannelData->chMode, (codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_BW_25K) != 0));
					trxSetRxCSS(RADIO_DEVICE_PRIMARY, currentChannelData->rxTone);
				}

				announceItem(PROMPT_SEQUENCE_MODE, PROMPT_THRESHOLD_1);
				uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
				uiChannelModeUpdateScreen(0);
			}
			else
			{
				if (trxGetMode() == RADIO_MODE_DIGITAL)
				{
					// Toggle timeslot
					trxSetDMRTimeSlot(1 - trxGetDMRTimeSlot(), true);
					tsSetManualOverride(CHANNEL_CHANNEL, (trxGetDMRTimeSlot() + 1));

					if ((nonVolatileSettings.overrideTG == 0) && (currentContactData.reserve1 & CODEPLUG_CONTACT_FLAG_NO_TS_OVERRIDE) == 0x00)
					{
						tsSetContactHasBeenOverriden(CHANNEL_CHANNEL, true);
					}

					disableAudioAmp(AUDIO_AMP_MODE_RF);
					lastHeardClearLastID();
					uiDataGlobal.displayQSOState = uiDataGlobal.displayQSOStatePrev;
					uiChannelModeUpdateScreen(0);

					if (trxGetDMRTimeSlot() == 0)
					{
						menuChannelExitStatus |= MENU_STATUS_FORCE_FIRST;
					}
					announceItem(PROMPT_SEQUENCE_TS,PROMPT_THRESHOLD_3);
				}
				else
				{
					// STAR on an analog channel opens the Morse (CW) text sender.
					menuSystemPushNewMenu(UI_MORSE);
				}
			}
		}
		else if (KEYCHECK_LONGDOWN(ev->keys, KEY_STAR) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0))
		{
			if (trxGetMode() == RADIO_MODE_DIGITAL)
			{
				tsSetManualOverride(CHANNEL_CHANNEL, TS_NO_OVERRIDE);
				tsSetContactHasBeenOverriden(CHANNEL_CHANNEL, false);

				if ((currentRxGroupData.name[0] != 0) && (nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE] < currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup))
				{
					codeplugContactGetDataForIndex(currentRxGroupData.contacts[nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE]], &currentContactData);
				}
				else
				{
					codeplugContactGetDataForIndex(currentChannelData->contact, &currentContactData);
				}

				trxUpdateTsForCurrentChannelWithSpecifiedContact(&currentContactData);

				lastHeardClearLastID();
				uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
				uiChannelModeUpdateScreen(0);
				announceItem(PROMPT_SEQUENCE_TS, PROMPT_THRESHOLD_1);
			}
			else
			{
				// Long STAR on an analog channel opens the fox-hunt helper.
				menuSystemPushNewMenu(UI_FOX_HUNT);
			}
		}
		else if (KEYCHECK_LONGDOWN(ev->keys, KEY_HASH) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_HASH) == false))
		{
			uiDataGlobal.talkaround = false; // Talkaround and reverseRepeater are mutually exclusive.
			uiDataGlobal.reverseRepeaterChannel = !uiDataGlobal.reverseRepeaterChannel;

			uint32_t rxFreq = (uiDataGlobal.reverseRepeaterChannel ? currentChannelData->txFreq : currentChannelData->rxFreq);
			uint32_t txFreq = (uiDataGlobal.reverseRepeaterChannel ? currentChannelData->rxFreq : currentChannelData->txFreq);

			// If reverseRepeater mode is enabled (and mode is DIGITAL), force to DMR Active mode.
			trxSetFrequency(rxFreq, txFreq, (((currentChannelData->chMode == RADIO_MODE_DIGITAL) && (uiDataGlobal.reverseRepeaterChannel || codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_FORCE_DMO))) ? DMR_MODE_DMO : DMR_MODE_AUTO));

			announceItem(PROMPT_SEQUENCE_CHANNEL_NAME_AND_CONTACT_OR_VFO_FREQ_AND_MODE, PROMPT_THRESHOLD_NEVER_PLAY_IMMEDIATELY);
			uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
			uiChannelModeUpdateScreen(0);
			return;
		}
		else if (
#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
				KEYCHECK_SHORTUP(ev->keys, KEY_ROTARY_DECREMENT)
#if defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
				|| KEYCHECK_SHORTUP(ev->keys, KEY_FRONT_DOWN)
#endif
#else
				(KEYCHECK_SHORTUP(ev->keys, KEY_DOWN) || KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_DOWN))
#endif
		)
		{
			uiDataGlobal.talkaround = false;
			handleDownKey(ev);
			return;
		}
		else if (
#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
				KEYCHECK_SHORTUP(ev->keys, KEY_ROTARY_INCREMENT)
#if defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
				|| KEYCHECK_SHORTUP(ev->keys, KEY_FRONT_UP)
#endif
#else
				(KEYCHECK_SHORTUP(ev->keys, KEY_UP) || KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_UP))
#endif
		)
		{
			uiDataGlobal.talkaround = false;
			handleUpKey(ev);
			return;
		}
		else if (KEYCHECK_LONGDOWN(ev->keys,
#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
				KEY_FRONT_UP
#else
				KEY_UP
#endif
				) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0))
		{
			if (uiDataGlobal.Scan.active == false)
			{
				scanStart(true);
#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
				return;
#endif
			}
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_0) && (directChannelNumber == 0) && (trxGetMode() == RADIO_MODE_ANALOG) &&
				(BUTTONCHECK_DOWN(ev, BUTTON_SK1) == false) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == false) && (uiDataGlobal.Scan.active == false))
		{
			// Short 0, with no channel number being typed (there is no channel 0,
			// so a leading 0 was never a digit): find the station's CTCSS/DCS.
			// On release, so a long press still opens the selective call.
			toneFinderToggle();
			return;
		}
		else
		{
			int keyval = menuGetKeypadKeyValue(ev, true);

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
			}
			else if ((keyval < 10) && (!BUTTONCHECK_DOWN(ev, BUTTON_SK2)))
			{
				directChannelNumber = (directChannelNumber * 10) + keyval;
				if (CODEPLUG_ZONE_IS_ALLCHANNELS(currentZone))
				{
					// All Channels virtual zone
					if(directChannelNumber > CODEPLUG_CONTACTS_MAX)
					{
						directChannelNumber = 0;
						soundSetMelody(MELODY_ERROR_BEEP);
					}
				}
				else
				{
					if(directChannelNumber > currentZone.NOT_IN_CODEPLUGDATA_numChannelsInZone)
					{
						directChannelNumber = 0;
						soundSetMelody(MELODY_ERROR_BEEP);
					}
				}

				if (directChannelNumber > 0)
				{
					voicePromptsInit();
					if (directChannelNumber < 10)
					{
						voicePromptsAppendLanguageString(currentLanguage->gotoChannel);
					}
					voicePromptsAppendPrompt(PROMPT_0 + keyval);
					voicePromptsPlay();
				}
				else
				{
					announceItem(PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ, PROMPT_THRESHOLD_3);
				}

				uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
				uiChannelModeUpdateScreen(0);
			}
		}
	}
#endif // ! PLATFORM_GD77S
}

#if ! defined(PLATFORM_GD77S)
static void selectPrevNextZone(bool nextZone)
{
	int numZones = codeplugZonesGetCount();

	if (nextZone)
	{
		settingsIncrement(nonVolatileSettings.currentZone, 1);

		if (nonVolatileSettings.currentZone >= numZones)
		{
			settingsSet(nonVolatileSettings.currentZone, 0);
		}
	}
	else
	{
		if (nonVolatileSettings.currentZone == 0)
		{
			settingsSet(nonVolatileSettings.currentZone, (int16_t) (numZones - 1));
		}
		else
		{
			settingsDecrement(nonVolatileSettings.currentZone, 1);
		}
	}

	currentChannelData->rxFreq = 0x00; // Flag to the Channel screen that the channel data is now invalid and needs to be reloaded
}

static void handleUpKey(uiEvent_t *ev)
{
	if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
	{
		selectPrevNextZone(true);
		menuSystemPopAllAndDisplaySpecificRootMenu(UI_CHANNEL_MODE, false);
		uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN; // Force screen redraw

		if (nonVolatileSettings.currentZone == 0)
		{
			menuChannelExitStatus |= (MENU_STATUS_LIST_TYPE | MENU_STATUS_FORCE_FIRST);
		}
		return;
	}
	else
	{
		int16_t nextChan = -1;

		if (uiDataGlobal.Scan.active)
		{
			// We are scanning downward
			if (uiDataGlobal.Scan.state == SCAN_STATE_PAUSED)
			{
				if (uiDataGlobal.Scan.direction == -1)
				{
					handleDownKey(ev);
					return;
				}

				// Let's select the next channel
			}
			else
			{
				// We're not paused, we shouldn't change the current channel
				return;
			}
		}

		if (CODEPLUG_ZONE_IS_ALLCHANNELS(currentZone))
		{
			if (currentZone.NOT_IN_CODEPLUGDATA_numChannelsInZone > 1)
			{
				nextChan = codeplugGetLastUsedChannelInCurrentZone();

				// All Channels virtual zone
				do
				{
					nextChan = ((nextChan % currentZone.NOT_IN_CODEPLUGDATA_highestIndex) + 1);

				} while (!codeplugAllChannelsIndexIsInUse(nextChan));

				if (codeplugSetLastUsedChannelInZone(currentZone.NOT_IN_CODEPLUGDATA_indexNumber, nextChan) == CODEPLUG_CHANNELS_MIN)
				{
					menuChannelExitStatus |= (MENU_STATUS_LIST_TYPE | MENU_STATUS_FORCE_FIRST);
				}
			}
		}
		else
		{
			if (currentZone.NOT_IN_CODEPLUGDATA_numChannelsInZone > 1)
			{
				nextChan = ((codeplugGetLastUsedChannelInCurrentZone() + 1) % currentZone.NOT_IN_CODEPLUGDATA_numChannelsInZone);

				if (codeplugSetLastUsedChannelInZone(currentZone.NOT_IN_CODEPLUGDATA_indexNumber, nextChan) == 0)
				{
					menuChannelExitStatus |= (MENU_STATUS_LIST_TYPE | MENU_STATUS_FORCE_FIRST);
				}
			}
		}

		if (uiDataGlobal.Scan.active && (nextChan != -1))
		{
			scanNextChannelIndex = nextChan;
			scanNextChannelReady = true;
		}

		uiDataGlobal.Scan.timer.timeout = (uiDataGlobal.Scan.active ? 0 : 500); // when scanning is running, don't let it pick another channel
		uiDataGlobal.Scan.state = SCAN_STATE_SCANNING;
	}

	lastHeardClearLastID();
	uiChannelModeLoadChannelData(false, true);
	uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
	uiChannelModeUpdateScreen(0);
}

static void handleDownKey(uiEvent_t *ev)
{
	if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
	{
		selectPrevNextZone(false);
		menuSystemPopAllAndDisplaySpecificRootMenu(UI_CHANNEL_MODE, false);
		uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN; // Force screen redraw

		if (nonVolatileSettings.currentZone == 0)
		{
			menuChannelExitStatus |= (MENU_STATUS_LIST_TYPE | MENU_STATUS_FORCE_FIRST);
		}

		return;
	}
	else
	{
		int16_t prevChan = -1;

		if (uiDataGlobal.Scan.active)
		{
			// We are scanning downward
			if (uiDataGlobal.Scan.state == SCAN_STATE_PAUSED)
			{
				if (uiDataGlobal.Scan.direction == 1)
				{
					handleUpKey(ev);
					return;
				}

				// Let's select the previous channel
			}
			else
			{
				// We're not paused, we shouldn't change the current channel
				return;
			}
		}

		if (CODEPLUG_ZONE_IS_ALLCHANNELS(currentZone))
		{
			if (currentZone.NOT_IN_CODEPLUGDATA_numChannelsInZone > 1)
			{
				prevChan = codeplugGetLastUsedChannelInCurrentZone();

				// All Channels virtual zone
				do
				{
					prevChan = ((((prevChan - 1) + currentZone.NOT_IN_CODEPLUGDATA_highestIndex - 1) % currentZone.NOT_IN_CODEPLUGDATA_highestIndex) + 1);

				} while (!codeplugAllChannelsIndexIsInUse(prevChan));

				if (codeplugSetLastUsedChannelInZone(currentZone.NOT_IN_CODEPLUGDATA_indexNumber, prevChan) == CODEPLUG_CHANNELS_MIN)
				{
					menuChannelExitStatus |= (MENU_STATUS_LIST_TYPE | MENU_STATUS_FORCE_FIRST);
				}
			}
		}
		else
		{
			if (currentZone.NOT_IN_CODEPLUGDATA_numChannelsInZone > 1)
			{
				prevChan = ((codeplugGetLastUsedChannelInCurrentZone() + currentZone.NOT_IN_CODEPLUGDATA_numChannelsInZone - 1) % currentZone.NOT_IN_CODEPLUGDATA_numChannelsInZone);

				if (codeplugSetLastUsedChannelInZone(currentZone.NOT_IN_CODEPLUGDATA_indexNumber, prevChan) == 0)
				{
					menuChannelExitStatus |= (MENU_STATUS_LIST_TYPE | MENU_STATUS_FORCE_FIRST);
				}
			}
		}

		if (uiDataGlobal.Scan.active && (prevChan != -1))
		{
			scanNextChannelIndex = prevChan;
			scanNextChannelReady = true;
		}

		uiDataGlobal.Scan.timer.timeout = (uiDataGlobal.Scan.active ? 0 : 500); // when scanning is running, don't let it pick another channel
		uiDataGlobal.Scan.state = SCAN_STATE_SCANNING;
	}

	lastHeardClearLastID();
	uiChannelModeLoadChannelData(false, true);
	uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
	uiChannelModeUpdateScreen(0);
}

#endif // ! PLATFORM_GD77S


// ---------------------------------------- Quick Menu functions -------------------------------------------------------------------
menuStatus_t uiChannelModeQuickMenu(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		if (quickmenuChannelFromVFOHandled)
		{
			quickmenuChannelFromVFOHandled = false;
			menuSystemPopAllAndDisplayRootMenu();
			return MENU_STATUS_SUCCESS;
		}

		uiChannelModeStopScanning();
		uiDataGlobal.QuickMenu.tmpDmrDestinationFilterLevel = nonVolatileSettings.dmrDestinationFilter;
		uiDataGlobal.QuickMenu.tmpDmrCcTsFilterLevel = nonVolatileSettings.dmrCcTsFilter;
		uiDataGlobal.QuickMenu.tmpAnalogFilterLevel = nonVolatileSettings.analogFilterLevel;
		uiDataGlobal.QuickMenu.tmpTalkaround = uiDataGlobal.talkaround;
		uiDataGlobal.QuickMenu.tmpSortOrderIsDistance = settingsIsOptionBitSet(BIT_SORT_CHANNEL_DISTANCE);

		menuDataGlobal.numItems = NUM_CH_SCREEN_QUICK_MENU_ITEMS;

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
		menuQuickChannelExitStatus = MENU_STATUS_SUCCESS;

		if ((ev->hasEvent) || (menuDataGlobal.menuOptionsTimeout > 0))
		{
			handleQuickMenuEvent(ev);
		}
	}

	return menuQuickChannelExitStatus;
}

static bool validateOverwriteChannel(void)
{
	quickmenuChannelFromVFOHandled = true;

	if (uiDataGlobal.MessageBox.keyPressed == KEY_GREEN)
	{
		struct_codeplugContact_t vfoContact;
		int8_t vfoTS = -1;

		uiVFOLoadContact(&vfoContact);
		memcpy(&currentChannelData->rxFreq, &settingsVFOChannel[nonVolatileSettings.currentVFONumber].rxFreq, CODEPLUG_CHANNEL_DATA_STRUCT_SIZE - 16);// Don't copy the name of the vfo, which are in the first 16 bytes
		// Find out which TS was in use.
		if ((nonVolatileSettings.overrideTG == 0) && (vfoContact.reserve1 & CODEPLUG_CONTACT_FLAG_NO_TS_OVERRIDE) == 0x00)
		{
			vfoTS = ((vfoContact.reserve1 & CODEPLUG_CONTACT_FLAG_TS_OVERRIDE_TIMESLOT_MASK) != 0);
		}
		else
		{
			vfoTS = tsGetManualOverride((Channel_t)nonVolatileSettings.currentVFONumber);

			// No Override, use the TS from the Channel
			if (vfoTS == 0)
			{
				vfoTS = ((codeplugChannelGetFlag(&settingsVFOChannel[nonVolatileSettings.currentVFONumber], CHANNEL_FLAG_TIMESLOT_TWO) != 0) ? 1 : 0);
			}
			else
			{
				vfoTS--; // convert to real TS
			}
		}

		codeplugChannelSetFlag(currentChannelData, CHANNEL_FLAG_TIMESLOT_TWO, ((vfoTS == 0) ? 0 : 1));
		codeplugChannelSaveDataForIndex(uiDataGlobal.currentSelectedChannelNumber, currentChannelData);
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

	displayClearBuf();
	bool settingOption = uiQuickKeysShowChoices(buf, SCREEN_LINE_BUFFER_SIZE, currentLanguage->quick_menu);

	for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
	{
		if ((settingOption == false) || (i == 0))
		{
			mNum = menuGetMenuOffset(NUM_CH_SCREEN_QUICK_MENU_ITEMS, i);
			if (mNum == MENU_OFFSET_BEFORE_FIRST_ENTRY)
			{
				continue;
			}
			else if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
			{
				break;
			}

			buf[0] = 0;
			rightSideVar[0] = 0;
			rightSideConst = NULL;
			leftSide = NULL;

			switch(mNum)
			{
				case CH_SCREEN_QUICK_MENU_COPY2VFO:
					rightSideConst = currentLanguage->channelToVfo;
					break;
				case CH_SCREEN_QUICK_MENU_COPY_FROM_VFO:
					rightSideConst = currentLanguage->vfoToChannel;
					break;
				case CH_SCREEN_QUICK_MENU_FILTER_FM:
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
				case CH_SCREEN_QUICK_MENU_FILTER_DMR:
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
				case CH_SCREEN_QUICK_MENU_DMR_CC_SCAN:
					leftSide = currentLanguage->dmr_cc_scan;
					rightSideConst = (uiDataGlobal.QuickMenu.tmpDmrCcTsFilterLevel & DMR_CC_FILTER_PATTERN) ? currentLanguage->off : currentLanguage->on;
					break;
				case CH_SCREEN_QUICK_MENU_FILTER_DMR_TS:
					leftSide = currentLanguage->dmr_ts_filter;
					rightSideConst = (uiDataGlobal.QuickMenu.tmpDmrCcTsFilterLevel & DMR_TS_FILTER_PATTERN) ? currentLanguage->on : currentLanguage->off;
					break;
				case CH_SCREEN_QUICK_MENU_TALKAROUND:
					leftSide = currentLanguage->talkaround;
					rightSideConst = ((currentChannelData->txFreq != currentChannelData->rxFreq) ? (uiDataGlobal.QuickMenu.tmpTalkaround ? currentLanguage->on : currentLanguage->off) : currentLanguage->n_a);
					break;
				case CH_SCREEN_QUICK_MENU_DISTANCE_SORT:
					leftSide = currentLanguage->distance_sort;
					rightSideConst = (CODEPLUG_ZONE_IS_ALLCHANNELS(currentZone)) ? currentLanguage->n_a : (uiDataGlobal.QuickMenu.tmpSortOrderIsDistance ? currentLanguage->on : currentLanguage->off);
					break;
				default:
					buf[0] = 0;
			}

			if (leftSide != NULL)
			{
				snprintf(buf, SCREEN_LINE_BUFFER_SIZE, "%s:%s", leftSide, (rightSideVar[0] ? rightSideVar : rightSideConst));
			}
			else
			{
				snprintf(buf, SCREEN_LINE_BUFFER_SIZE, "%s", (rightSideVar[0] ? rightSideVar : rightSideConst));
			}

			if (i == 0)
			{
				if (!isFirstRun && (menuDataGlobal.menuOptionsSetQuickkey == 0))
				{
					voicePromptsInit();
				}

				if ((leftSide != NULL) || menuDataGlobal.newOptionSelected)
				{
					voicePromptsAppendLanguageString(leftSide);
				}

				if (rightSideVar[0] != 0)
				{
					voicePromptsAppendString(rightSideVar);
				}
				else
				{
					voicePromptsAppendLanguageString(rightSideConst);
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
					case CH_SCREEN_QUICK_MENU_FILTER_FM:
					case CH_SCREEN_QUICK_MENU_FILTER_DMR:
					case CH_SCREEN_QUICK_MENU_DMR_CC_SCAN:
					case CH_SCREEN_QUICK_MENU_FILTER_DMR_TS:
					case CH_SCREEN_QUICK_MENU_TALKAROUND:
					case CH_SCREEN_QUICK_MENU_DISTANCE_SORT:
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
		else if ((QUICKKEY_TYPE(ev->function) == QUICKKEY_MENU) && (QUICKKEY_ENTRYID(ev->function) < NUM_CH_SCREEN_QUICK_MENU_ITEMS))
		{
			menuDataGlobal.currentItemIndex = QUICKKEY_ENTRYID(ev->function);
		}

		if ((QUICKKEY_FUNCTIONID(ev->function) != 0))
		{
			menuDataGlobal.menuOptionsTimeout = 1000;
			executingQuickKey = true;
		}
	}

	if ((ev->events & KEY_EVENT) && (menuDataGlobal.menuOptionsSetQuickkey == 0) && (menuDataGlobal.menuOptionsTimeout == 0))
	{
		if (KEYCHECK_PRESS(ev->keys, KEY_DOWN) && (menuDataGlobal.numItems != 0))
		{
			isDirty = true;
			menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, NUM_CH_SCREEN_QUICK_MENU_ITEMS);
			menuDataGlobal.newOptionSelected = true;
			menuQuickChannelExitStatus |= MENU_STATUS_LIST_TYPE;
		}
		else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
		{
			isDirty = true;
			menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, NUM_CH_SCREEN_QUICK_MENU_ITEMS);
			menuDataGlobal.newOptionSelected = true;
			menuQuickChannelExitStatus |= MENU_STATUS_LIST_TYPE;
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			quickKeyApply: // branching here when to quickkey was used.

			switch(menuDataGlobal.currentItemIndex)
			{
				case CH_SCREEN_QUICK_MENU_COPY2VFO:
				{
					int8_t currentTS = trxGetDMRTimeSlot();

					memcpy(&settingsVFOChannel[nonVolatileSettings.currentVFONumber].rxFreq, &currentChannelData->rxFreq, CODEPLUG_CHANNEL_DATA_STRUCT_SIZE - 16);// Don't copy the name of channel, which are in the first 16 bytes
					settingsVFOChannel[nonVolatileSettings.currentVFONumber].rxTone = currentChannelData->rxTone;
					settingsVFOChannel[nonVolatileSettings.currentVFONumber].txTone = currentChannelData->txTone;

					if (nonVolatileSettings.overrideTG == 0)
					{
						nonVolatileSettings.overrideTG = trxTalkGroupOrPcId;
					}

					// Set TS override
					tsSetManualOverride(((Channel_t)nonVolatileSettings.currentVFONumber), currentTS + 1);

					codeplugChannelSetFlag(&settingsVFOChannel[nonVolatileSettings.currentVFONumber], CHANNEL_FLAG_TIMESLOT_TWO, ((currentTS == 0) ? 0 : 1));

					menuSystemPopAllAndDisplaySpecificRootMenu(UI_VFO_MODE, true);
					return;
				}
				break;

				case CH_SCREEN_QUICK_MENU_COPY_FROM_VFO:
					if (quickmenuChannelFromVFOHandled == false)
					{
						snprintf(uiDataGlobal.MessageBox.message, MESSAGEBOX_MESSAGE_LEN_MAX, "%s\n%s", currentLanguage->overwrite_qm, currentLanguage->please_confirm);
						uiDataGlobal.MessageBox.type = MESSAGEBOX_TYPE_INFO;
						uiDataGlobal.MessageBox.decoration = MESSAGEBOX_DECORATION_FRAME;
						uiDataGlobal.MessageBox.buttons = MESSAGEBOX_BUTTONS_YESNO;
						uiDataGlobal.MessageBox.validatorCallback = validateOverwriteChannel;

						menuSystemPushNewMenu(UI_MESSAGE_BOX);
						voicePromptsInit();
						voicePromptsAppendLanguageString(currentLanguage->overwrite_qm);
						voicePromptsAppendLanguageString(currentLanguage->please_confirm);
						voicePromptsPlay();
					}
					return;
					break;

				default:
					// CH_SCREEN_QUICK_MENU_FILTER_FM
					if (nonVolatileSettings.analogFilterLevel != uiDataGlobal.QuickMenu.tmpAnalogFilterLevel)
					{
						settingsSet(nonVolatileSettings.analogFilterLevel, uiDataGlobal.QuickMenu.tmpAnalogFilterLevel);
						trxSetAnalogFilterLevel(nonVolatileSettings.analogFilterLevel);
					}

					// CH_SCREEN_QUICK_MENU_FILTER_DMR
					if (nonVolatileSettings.dmrDestinationFilter != uiDataGlobal.QuickMenu.tmpDmrDestinationFilterLevel)
					{
						settingsSet(nonVolatileSettings.dmrDestinationFilter, uiDataGlobal.QuickMenu.tmpDmrDestinationFilterLevel);
						if (trxGetMode() == RADIO_MODE_DIGITAL)
						{
							HRC6000InitDigitalDmrRx();
							disableAudioAmp(AUDIO_AMP_MODE_RF);
						}
					}

					// CH_SCREEN_QUICK_MENU_FILTER_DMR_CC
					// CH_SCREEN_QUICK_MENU_FILTER_DMR_TS
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

					// Talkaround
					if ((currentChannelData->txFreq != currentChannelData->rxFreq) && (uiDataGlobal.talkaround != uiDataGlobal.QuickMenu.tmpTalkaround))
					{
						uiDataGlobal.talkaround = uiDataGlobal.QuickMenu.tmpTalkaround;

						// Talkaround and reverseRepeater are mutually exclusive.
						if (uiDataGlobal.reverseRepeaterChannel && uiDataGlobal.talkaround)
						{
							uiDataGlobal.reverseRepeaterChannel = false;
						}
					}

					if (settingsIsOptionBitSet(BIT_SORT_CHANNEL_DISTANCE) != uiDataGlobal.QuickMenu.tmpSortOrderIsDistance)
					{
						settingsSetOptionBit(BIT_SORT_CHANNEL_DISTANCE, uiDataGlobal.QuickMenu.tmpSortOrderIsDistance);

						codeplugSetLastUsedChannelInZone(currentZone.NOT_IN_CODEPLUGDATA_indexNumber, 0);
						currentChannelData->rxFreq = 0; // force reload of zone
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
			uiChannelModeStopScanning();
			menuSystemPopPreviousMenu();
			return;
		}
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
				case CH_SCREEN_QUICK_MENU_FILTER_FM:
					if (uiDataGlobal.QuickMenu.tmpAnalogFilterLevel < NUM_ANALOG_FILTER_LEVELS - 1)
					{
						uiDataGlobal.QuickMenu.tmpAnalogFilterLevel++;
					}
					break;
				case CH_SCREEN_QUICK_MENU_FILTER_DMR:
					if (uiDataGlobal.QuickMenu.tmpDmrDestinationFilterLevel < NUM_DMR_DESTINATION_FILTER_LEVELS - 1)
					{
						uiDataGlobal.QuickMenu.tmpDmrDestinationFilterLevel++;
					}
					break;
				case CH_SCREEN_QUICK_MENU_DMR_CC_SCAN:
					if ((uiDataGlobal.QuickMenu.tmpDmrCcTsFilterLevel & DMR_CC_FILTER_PATTERN))
					{
						uiDataGlobal.QuickMenu.tmpDmrCcTsFilterLevel &= ~DMR_CC_FILTER_PATTERN;
					}
					break;
				case CH_SCREEN_QUICK_MENU_FILTER_DMR_TS:
					if (!(uiDataGlobal.QuickMenu.tmpDmrCcTsFilterLevel & DMR_TS_FILTER_PATTERN))
					{
						uiDataGlobal.QuickMenu.tmpDmrCcTsFilterLevel |= DMR_TS_FILTER_PATTERN;
					}
					break;
				case CH_SCREEN_QUICK_MENU_TALKAROUND:
					if (currentChannelData->txFreq != currentChannelData->rxFreq)
					{
						if (uiDataGlobal.QuickMenu.tmpTalkaround == false)
						{
							uiDataGlobal.QuickMenu.tmpTalkaround = true;
						}
					}
					break;
				case CH_SCREEN_QUICK_MENU_DISTANCE_SORT:
					if (CODEPLUG_ZONE_IS_ALLCHANNELS(currentZone) == false)
					{
						if (uiDataGlobal.QuickMenu.tmpSortOrderIsDistance == false)
						{
							uiDataGlobal.QuickMenu.tmpSortOrderIsDistance = true;
						}
					}
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
				case CH_SCREEN_QUICK_MENU_FILTER_FM:
					if (uiDataGlobal.QuickMenu.tmpAnalogFilterLevel > ANALOG_FILTER_NONE)
					{
						uiDataGlobal.QuickMenu.tmpAnalogFilterLevel--;
					}
					break;
				case CH_SCREEN_QUICK_MENU_FILTER_DMR:
					if (uiDataGlobal.QuickMenu.tmpDmrDestinationFilterLevel > DMR_DESTINATION_FILTER_NONE)
					{
						uiDataGlobal.QuickMenu.tmpDmrDestinationFilterLevel--;
					}
					break;
				case CH_SCREEN_QUICK_MENU_DMR_CC_SCAN:
					if (!(uiDataGlobal.QuickMenu.tmpDmrCcTsFilterLevel & DMR_CC_FILTER_PATTERN))
					{
						uiDataGlobal.QuickMenu.tmpDmrCcTsFilterLevel |= DMR_CC_FILTER_PATTERN;
					}
					break;
				case CH_SCREEN_QUICK_MENU_FILTER_DMR_TS:
					if ((uiDataGlobal.QuickMenu.tmpDmrCcTsFilterLevel & DMR_TS_FILTER_PATTERN))
					{
						uiDataGlobal.QuickMenu.tmpDmrCcTsFilterLevel &= ~DMR_TS_FILTER_PATTERN;
					}
					break;
				case CH_SCREEN_QUICK_MENU_TALKAROUND:
					if (currentChannelData->txFreq != currentChannelData->rxFreq)
					{
						if (uiDataGlobal.QuickMenu.tmpTalkaround)
						{
							uiDataGlobal.QuickMenu.tmpTalkaround = false;
						}
					}
					break;
				case CH_SCREEN_QUICK_MENU_DISTANCE_SORT:
					if (!CODEPLUG_ZONE_IS_ALLCHANNELS(currentZone))
					{
						if (uiDataGlobal.QuickMenu.tmpSortOrderIsDistance)
						{
							uiDataGlobal.QuickMenu.tmpSortOrderIsDistance = false;
							//codeplugSetLastUsedChannelInZone(currentZone.NOT_IN_CODEPLUGDATA_indexNumber, 0);
							//currentChannelData->rxFreq = 0;// force reload of zone
						}
					}
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
		uiQuickKeysStore(ev, &menuQuickChannelExitStatus);
		isDirty = true;
	}

	if (isDirty)
	{
		updateQuickMenuScreen(false);
	}
}

//Scan Mode
static void scanStart(bool longPressBeep)
{
	// At least two channels are needed to run a scan process.
	if (canCurrentZoneBeScanned(&uiDataGlobal.Scan.availableChannelsCount) == false)
	{
		menuChannelExitStatus |= MENU_STATUS_ERROR;
		return;
	}

	if (voicePromptsIsPlaying())
	{
		voicePromptsTerminate();
	}

	directChannelNumber = 0;
	uiDataGlobal.Scan.direction = 1;
	uiDataGlobal.talkaround = false;

	// Clear all nuisance delete channels at start of scanning
	for (int i = 0; i < MAX_ZONE_SCAN_NUISANCE_CHANNELS; i++)
	{
		uiDataGlobal.Scan.nuisanceDelete[i] = -1;
	}
	uiDataGlobal.Scan.nuisanceDeleteIndex = 0;

	uiDataGlobal.Scan.active = true;
	uiDataGlobal.Scan.state = SCAN_STATE_SCANNING;
	uiDataGlobal.Scan.lastIteration = false;

	uiDataGlobal.Scan.stepTimeMilliseconds = settingsGetScanStepTimeMilliseconds();

	// In DIGITAL (slow) mode, we need at least 120ms to see the HR-C6000 to start the TS ISR.
	if (trxGetMode() == RADIO_MODE_DIGITAL)
	{
		int dwellTime;
		if(uiDataGlobal.Scan.stepTimeMilliseconds > 150)				// if >150ms use DMR Slow mode
		{
			dwellTime = ((currentRadioDevice->trxDMRModeRx == DMR_MODE_DMO) ? SCAN_DMR_SIMPLEX_SLOW_MIN_DWELL_TIME : SCAN_DMR_DUPLEX_SLOW_MIN_DWELL_TIME);
		}
		else
		{
			dwellTime = ((currentRadioDevice->trxDMRModeRx == DMR_MODE_DMO) ? SCAN_DMR_SIMPLEX_FAST_MIN_DWELL_TIME : (SCAN_DMR_DUPLEX_FAST_MIN_DWELL_TIME + SCAN_DMR_DUPLEX_FAST_EXTRA_DWELL_TIME));
		}

		uiDataGlobal.Scan.dwellTime = ((uiDataGlobal.Scan.stepTimeMilliseconds < dwellTime) ? dwellTime : uiDataGlobal.Scan.stepTimeMilliseconds);
	}
	else
	{
		uiDataGlobal.Scan.dwellTime = uiDataGlobal.Scan.stepTimeMilliseconds;
	}

	uiDataGlobal.Scan.timer.timeout = uiDataGlobal.Scan.dwellTime;
	uiDataGlobal.Scan.scanType = SCAN_TYPE_NORMAL_STEP;

	// Need to set the melody here, otherwise long press will remain silent
	// since beeps aren't allowed while scanning
	if (longPressBeep)
	{
		soundSetMelody(MELODY_KEY_LONG_BEEP);
	}

	// Set current channel index
	scanNextChannelIndex = codeplugGetLastUsedChannelNumberInCurrentZone();
	scanNextChannelReady = false;
}

static void updateTrxID(void)
{
	if (nonVolatileSettings.overrideTG != 0)
	{
		trxTalkGroupOrPcId = nonVolatileSettings.overrideTG;
	}
	else
	{
		/*
		 * VK3KYY
		 * I can't see a compelling reason to remove the TS override when changing TG
		 * The function trxUpdateTsForCurrentChannelWithSpecifiedContact(), used below, is used to handle the TS
		 *
			tsSetManualOverride(CHANNEL_CHANNEL, TS_NO_OVERRIDE);
		*/
		if ((currentRxGroupData.name[0] != 0) && (nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE] < currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup))
		{
			codeplugContactGetDataForIndex(currentRxGroupData.contacts[nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE]], &currentContactData);
		}
		else
		{
			codeplugContactGetDataForIndex(currentChannelData->contact, &currentContactData);
		}

		tsSetContactHasBeenOverriden(CHANNEL_CHANNEL, false);

		trxUpdateTsForCurrentChannelWithSpecifiedContact(&currentContactData);
		trxTalkGroupOrPcId = codeplugContactGetPackedId(&currentContactData);
	}
	lastHeardClearLastID();
	menuPrivateCallClear();
}

static void scanning(void)
{
	// After initial settling time
	if((uiDataGlobal.Scan.state == SCAN_STATE_SCANNING) && (uiDataGlobal.Scan.timer.timeout > SCAN_SKIP_CHANNEL_INTERVAL) && (uiDataGlobal.Scan.timer.timeout < (uiDataGlobal.Scan.dwellTime - SCAN_FREQ_CHANGE_SETTLING_INTERVAL)))
	{
		//Signal detect for DMR channels
		if (trxGetMode() == RADIO_MODE_DIGITAL)
		{
			if(uiDataGlobal.Scan.stepTimeMilliseconds > 150)  // if >150ms use DMR Slow mode
			{                                                 // DMR Scan Slow mode for signal detect
				if (((nonVolatileSettings.dmrCcTsFilter & DMR_TS_FILTER_PATTERN)
						&&
						((slotState != DMR_STATE_IDLE) && ((dmrMonitorCapturedTS != -1) &&
								(((currentRadioDevice->trxDMRModeRx == DMR_MODE_DMO) && (dmrMonitorCapturedTS == trxGetDMRTimeSlot())) ||
										(currentRadioDevice->trxDMRModeRx == DMR_MODE_RMO)))))
						||
						// As soon as the HRC6000 get sync, timeCode != -1 or TS ISR is running
						HRC6000HasGotSync())
				{
					uiDataGlobal.Scan.state = SCAN_STATE_SHORT_PAUSED;
#if defined(PLATFORM_MD9600)
					uiDataGlobal.Scan.clickDiscriminator = CLICK_DISCRIMINATOR;
#endif

#if ! defined(PLATFORM_GD77S) // GD77S handle voice prompts on its own
					// Reload the channel as voice prompts aren't set while scanning
					if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
					{
						uiChannelModeLoadChannelData(false, true);
					}
#endif
					uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN; // Force screen refresh
					uiDataGlobal.Scan.timer.timeout = ((TIMESLOT_DURATION * 12) + TIMESLOT_DURATION) * 4; // (1 superframe + 1 TS) * 4 = TS Sync + incoming audio
				}
			}
			else //DMR Scan fast mode signal detect
			{
				if(trxCarrierDetected(RADIO_DEVICE_PRIMARY))
				{
#if ! defined(PLATFORM_GD77S) // GD77S handle voice prompts on its own
					// Reload the channel as voice prompts aren't set while scanning
					if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
					{
						uiChannelModeLoadChannelData(false, true);
					}
#endif
					uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN; // Force screen refresh

					if (((nonVolatileSettings.dmrCcTsFilter & DMR_TS_FILTER_PATTERN) == 0))
					{
						uiDataGlobal.Scan.timer.timeout = SCAN_SHORT_PAUSE_TIME * 2;	//needs longer delay if in DMR mode and TS Filter is off to allow full detection of signal
					}
					else
					{
						uiDataGlobal.Scan.timer.timeout = SCAN_SHORT_PAUSE_TIME;	//start short delay to allow full detection of signal
					}

					uiDataGlobal.Scan.state = SCAN_STATE_SHORT_PAUSED;		//state 1 = pause and test for valid signal that produces audio
#if defined(PLATFORM_MD9600)
					uiDataGlobal.Scan.clickDiscriminator = CLICK_DISCRIMINATOR;
#endif
				}
			}
		}
		else //Signal Detect for FM channels
		{
			if(trxCarrierDetected(RADIO_DEVICE_PRIMARY))
			{
#if ! defined(PLATFORM_GD77S) // GD77S handle voice prompts on its own
				// Reload the channel as voice prompts aren't set while scanning
				if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
				{
					uiChannelModeLoadChannelData(false, true);
				}
#endif
				uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN; // Force screen refresh

				uiDataGlobal.Scan.timer.timeout = SCAN_SHORT_PAUSE_TIME;	//start short delay to allow full detection of signal
				uiDataGlobal.Scan.state = SCAN_STATE_SHORT_PAUSED;		//state 1 = pause and test for valid signal that produces audio
#if defined(PLATFORM_MD9600)
				uiDataGlobal.Scan.clickDiscriminator = CLICK_DISCRIMINATOR;
#endif
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
					uiChannelModeStopScanning();
					return;
				}
				else
				{
					uiDataGlobal.Scan.timer.timeout = nonVolatileSettings.scanDelay * 1000;

					if ((uiDataGlobal.Scan.state == SCAN_STATE_SHORT_PAUSED) &&
							(settingsIsOptionBitSet(BIT_DISPLAY_CHANNEL_DISTANCE) || settingsIsOptionBitSet(BIT_SORT_CHANNEL_DISTANCE)))
					{
						uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN; // Force screen refresh once
					}

					uiDataGlobal.Scan.state = SCAN_STATE_PAUSED;
				}
			}
#if defined(PLATFORM_MD9600)
		}
#endif
	}

	if(uiDataGlobal.Scan.timer.timeout > 0)
	{
		if (scanNextChannelReady == false)
		{
			scanSearchForNextChannel();
		}

		uiDataGlobal.Scan.timer.timeout--;
	}
	else
	{
		if (scanNextChannelReady)
		{
			hidesChannelDetails();

			scanApplyNextChannel();

			// When less than 2 channel remain in the Zone
			if (uiDataGlobal.Scan.lastIteration)
			{
				scanStop(false);
				return;
			}
		}

		uiDataGlobal.Scan.state = SCAN_STATE_SCANNING;
	}
}

static void scanStop(bool loadChannel)
{
	int prevTS = dmrMonitorCapturedTS; // Always -1 on Simplex
	bool wasRXingDigital = ((trxGetMode() == RADIO_MODE_DIGITAL) && (slotState != DMR_STATE_IDLE) && (prevTS != -1));

	uiChannelModeStopScanning();
	uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
	uiChannelModeUpdateScreen(0);

	// It loads the channel data when exiting scan mode (as it was partialy readed)
	if (loadChannel)
	{
		uiChannelModeLoadChannelData(false, true);
	}

	// We were receiving something in Digital (Duplex), but since the TS filtering is OFF, we
	// will double check if selected TS stills match the previous one, when scan was paused.
	if (wasRXingDigital && ((nonVolatileSettings.dmrCcTsFilter & DMR_TS_FILTER_PATTERN) == 0))
	{
		int retries = 120;

		// Wait a bit until the TS is detected
		watchdogRun(false); // Temporary disable the watchdog here, as it could take too much time for it.
		do
		{
			vTaskDelay((10 / portTICK_PERIOD_MS));
		}
		while ((dmrMonitorCapturedTS == -1) && (retries-- > 0));
		watchdogRun(true);

		// Okay, we can now toggle the TS, if required
		if (((dmrMonitorCapturedTS != -1) && (prevTS != -1)) && (prevTS != dmrMonitorCapturedTS))
		{
			trxSetDMRTimeSlot(trxGetDMRTimeSlot(), true);
		}
	}
}

void uiChannelModeStopScanning(void)
{
	uiDataGlobal.Scan.active = false;
	uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN; // Force screen refresh

#if ! defined(PLATFORM_GD77S) // GD77S handle voice prompts on its own
	// Reload the channel as voice prompts aren't set while scanning
	if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
	{
		uiChannelModeLoadChannelData(false, true);
	}
#endif
}

bool uiChannelModeIsScanning(void)
{
	return uiDataGlobal.Scan.active;
}

void uiChannelModeColdStart(void)
{
	currentChannelData->rxFreq = 0x00;	// Force to re-read codeplug data (needed due to "All Channels" translation)
}

// This can also be called from the VFO, on VFO -> New Channel, as currentZone could be uninitialized.
void uiChannelInitializeCurrentZone(void)
{
	codeplugZoneGetDataForNumber(nonVolatileSettings.currentZone, &currentZone);
	codeplugUtilConvertBufToString(currentZone.name, currentZoneName, 16);// need to convert to zero terminated string

	if (settingsIsOptionBitSet(BIT_SORT_CHANNEL_DISTANCE) &&
			(nonVolatileSettings.locationLat != SETTINGS_UNITIALISED_LOCATION_LAT) &&
			(CODEPLUG_ZONE_IS_ALLCHANNELS(currentZone) == false))
	{
		initSortedChannels();
	}
}

static void quicksortChannelByDistance(channelDistance_t *channelDistances, uint16_t first, uint16_t last)
{
	channelDistance_t temp;
	uint16_t i, j, pivot;

	if(first < last)
	{
		pivot = first;
		i = first;
		j = last;

		while (i < j)
		{
			while ((channelDistances[i].distance <= channelDistances[pivot].distance) && (i < last))
			{
				i++;
			}

			while (channelDistances[j].distance > channelDistances[pivot].distance)
			{
				j--;
			}

			if (i < j)
			{
				memcpy(&temp, &channelDistances[i], sizeof(channelDistance_t));
				memcpy(&channelDistances[i], &channelDistances[j], sizeof(channelDistance_t));
				memcpy(&channelDistances[j], &temp, sizeof(channelDistance_t));
			}
		}

		memcpy(&temp, &channelDistances[pivot], sizeof(channelDistance_t));
		memcpy(&channelDistances[pivot], &channelDistances[j], sizeof(channelDistance_t));
		memcpy(&channelDistances[j], &temp, sizeof(channelDistance_t));

		if (j > 0)
		{
			quicksortChannelByDistance(channelDistances, first, (j - 1));
		}

		quicksortChannelByDistance(channelDistances, (j + 1), last);
	}
}

static void initSortedChannels(void)
{
	if (currentZone.NOT_IN_CODEPLUGDATA_numChannelsInZone > 0)
	{
		channelDistance_t channelDistances[80];// Temporary storage while sorting a zone
		struct_codeplugChannel_t channelData;

		for (uint16_t i = 0; i < currentZone.NOT_IN_CODEPLUGDATA_highestIndex; i++)
		{
			channelDistances[i].index = currentZone.channels[i];

			codeplugChannelGetDataForIndex(channelDistances[i].index, &channelData);
			checkChannelLocation(&channelData);

			if ((channelData.NOT_IN_CODEPLUG_CALCULATED_DISTANCE_X10 != -1) && (channelData.NOT_IN_CODEPLUG_CALCULATED_DISTANCE_X10 < 65535))
			{
				channelDistances[i].distance = channelData.NOT_IN_CODEPLUG_CALCULATED_DISTANCE_X10;
			}
			else
			{
				// Use large positive value for any channel which does not have a location, so that is sorted to be at the end of the list
				// Max real value is the radius of the Earth at the equator 40,075
				// This value is in steps of 0.1km   hence 50,000 * 10 is outside of the actual possible range and is used to indicate a channel with no location
				// the index value is added so that each channel with no location, is sorted correctly to retain its position in the zone, relatative to the other channels with no location

				channelDistances[i].distance = (500000 + i);
			}
		}

		quicksortChannelByDistance(channelDistances, 0U, (uint16_t)(currentZone.NOT_IN_CODEPLUGDATA_highestIndex - 1));

		for (uint16_t i = 0; i < currentZone.NOT_IN_CODEPLUGDATA_highestIndex; i++)
		{
			currentZone.channels[i] = channelDistances[i].index;
		}
	}
}


#if defined(PLATFORM_GD77S)
bool uiChannelModeTransmitDTMFContactForGD77S(void)
{
	if (GD77SParameters.uiMode == GD77S_UIMODE_DTMF_CONTACTS)
	{
		if (GD77SParameters.dtmfListCount > 0)
		{
			// start dtmf sequence
			if(dtmfSequenceIsKeying() == false)
			{
				struct_codeplugDTMFContact_t dtmfContact;

				if (voicePromptsIsPlaying())
				{
					voicePromptsTerminate();
				}

				codeplugDTMFContactGetDataForNumber(GD77SParameters.dtmfListSelected + 1, &dtmfContact);
				dtmfSequencePrepare(dtmfContact.code, true);
			}
			else
			{
				dtmfSequenceStop();
				dtmfSequenceTick(false);
				dtmfSequenceReset();
			}
		}
		else
		{
			voicePromptsInit();
			voicePromptsAppendLanguageString(currentLanguage->list_empty);
			voicePromptsPlay();
		}

		return true;
	}

	return false;
}

void toggleTimeslotForGD77S(void)
{
	if (trxGetMode() == RADIO_MODE_DIGITAL)
	{
		// Toggle timeslot
		trxSetDMRTimeSlot(1 - trxGetDMRTimeSlot(), true);
		tsSetManualOverride(CHANNEL_CHANNEL, (trxGetDMRTimeSlot() + 1));

		disableAudioAmp(AUDIO_AMP_MODE_RF);
		HRC6000ClearActiveDMRID();
		lastHeardClearLastID();
		uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
		uiChannelModeUpdateScreen(0);
	}
}

void uiChannelModeHeartBeatActivityForGD77S(uiEvent_t *ev)
{
	static const uint32_t periods[] = { 6000, 100, 100, 100, 100, 100 };
	static const uint32_t periodsScan[] = { 2000, 50, 2000, 50, 2000, 50 };
	static uint8_t        beatRoll = 0;
	static uint32_t       mTime = 0;

	// <paranoid_mode>
	//   We use real time GPIO readouts, as LED could be turned on/off by another task.
	// </paranoid_mode>
	if ((LedRead(LED_RED) || LedRead(LED_GREEN)) // Any led is ON
			&& (trxTransmissionEnabled || (uiDataGlobal.DTMFContactList.isKeying) || (ev->buttons & BUTTON_PTT) || (getAudioAmpStatus() & (AUDIO_AMP_MODE_RF | AUDIO_AMP_MODE_BEEP | AUDIO_AMP_MODE_PROMPT)) || trxCarrierDetected() || ev->hasEvent)) // we're transmitting, or receiving, or user interaction.
	{
		// Turn off the red LED, if not transmitting
		if (LedRead(LED_RED) // Red is ON
				&& ((uiDataGlobal.DTMFContactList.isKeying == false) && ((trxTransmissionEnabled == false) || ((ev->buttons & BUTTON_PTT) == 0)))) // No TX
		{
			if ((rxPowerSavingIsRxOn() == false) && (ev->hasEvent == false))
			{
				rxPowerSavingSetState(ECOPHASE_POWERSAVE_INACTIVE);
			}

			LedWrite(LED_RED, 0);
		}

		// Turn off the green LED, if not receiving, or no AF output
		if (LedRead(LED_GREEN)) // Green is ON
		{
			if ((trxTransmissionEnabled || (uiDataGlobal.DTMFContactList.isKeying) || (ev->buttons & BUTTON_PTT))
					|| ((trxGetMode() == RADIO_MODE_DIGITAL) && (slotState != DMR_STATE_IDLE))
					|| (((getAudioAmpStatus() & (AUDIO_AMP_MODE_RF | AUDIO_AMP_MODE_BEEP | AUDIO_AMP_MODE_PROMPT)) != 0) || trxCarrierDetected()))
			{
				if ((ev->buttons & BUTTON_PTT) && (trxTransmissionEnabled == false)) // RX Only or Out of Band
				{
					if ((rxPowerSavingIsRxOn() == false) && (ev->hasEvent == false))
					{
						rxPowerSavingSetState(ECOPHASE_POWERSAVE_INACTIVE);
					}

					LedWrite(LED_GREEN, 0);
				}
			}
			else
			{
				if ((rxPowerSavingIsRxOn() == false) && (ev->hasEvent == false))
				{
					rxPowerSavingSetState(ECOPHASE_POWERSAVE_INACTIVE);
				}

				LedWrite(LED_GREEN, 0);
			}
		}

		// Reset pattern sequence
		beatRoll = 0;
		// And update the timer for the next first starting (OFF for 5 seconds) blink sequence.
		mTime = ev->time;
		return;
	}

	if (!rxPowerSavingIsRxOn())
	{
		return;
	}

	// Nothing is happening, blink
	if (((trxTransmissionEnabled == false) && (uiDataGlobal.DTMFContactList.isKeying == false) && ((ev->buttons & BUTTON_PTT) == 0))
			&& ((ev->hasEvent == false) && ((getAudioAmpStatus() & (AUDIO_AMP_MODE_RF | AUDIO_AMP_MODE_BEEP | AUDIO_AMP_MODE_PROMPT)) == 0) && (trxCarrierDetected() == false)))
	{
		// Blink both LEDs to have Orange color
		if ((ev->time - mTime) > (uiDataGlobal.Scan.active ? periodsScan[beatRoll] : periods[beatRoll]))
		{
			if ((nonVolatileSettings.ecoLevel > 0) && (ev->hasEvent == false))
			{
				rxPowerSavingSetState(ECOPHASE_POWERSAVE_INACTIVE);
			}

			mTime = ev->time;
			beatRoll = (beatRoll + 1) % (uiDataGlobal.Scan.active ? (sizeof(periodsScan) / sizeof(periodsScan[0])) : (sizeof(periods) / sizeof(periods[0])));
			LedWrite(LED_RED, (beatRoll % 2));
			LedWrite(LED_GREEN, (beatRoll % 2));
		}
	}
	else
	{
		// Reset pattern sequence
		beatRoll = 0;
		// And update the timer for the next first starting (OFF for 5 seconds) blink sequence.
		mTime = ev->time;
	}
}

static uint16_t getCurrentChannelInCurrentZoneForGD77S(void)
{
	int16_t chanIdx = codeplugGetLastUsedChannelInCurrentZone();
	return (CODEPLUG_ZONE_IS_ALLCHANNELS(currentZone) ? chanIdx : (chanIdx + 1));
}

static void checkAndUpdateSelectedChannelForGD77S(uint16_t chanNum, bool forceSpeech)
{
	bool updateDisplay = false;

	if(CODEPLUG_ZONE_IS_ALLCHANNELS(currentZone))
	{
		GD77SParameters.channelOutOfBounds = false;
		if (codeplugAllChannelsIndexIsInUse(chanNum))
		{
			if (chanNum != codeplugGetLastUsedChannelInCurrentZone())
			{
				codeplugSetLastUsedChannelInZone(currentZone.NOT_IN_CODEPLUGDATA_indexNumber, chanNum);
				uiChannelModeLoadChannelData(false, false);
				updateDisplay = true;
			}
		}
		else
		{
			GD77SParameters.channelOutOfBounds = true;
			voicePromptsInit();
			voicePromptsAppendPrompt(PROMPT_CHANNEL);
			voicePromptsAppendLanguageString(currentLanguage->error);
			voicePromptsPlay();
		}
	}
	else
	{
		if ((chanNum - 1) < currentZone.NOT_IN_CODEPLUGDATA_numChannelsInZone)
		{
			GD77SParameters.channelOutOfBounds = false;
			if ((chanNum - 1) != codeplugGetLastUsedChannelInCurrentZone())
			{
				codeplugSetLastUsedChannelInZone(currentZone.NOT_IN_CODEPLUGDATA_indexNumber, (chanNum - 1));
				uiChannelModeLoadChannelData(false, false);
				updateDisplay = true;
			}
		}
		else
		{
			GD77SParameters.channelOutOfBounds = true;
			voicePromptsInit();
			voicePromptsAppendPrompt(PROMPT_CHANNEL);
			voicePromptsAppendLanguageString(currentLanguage->error);
			voicePromptsPlay();
		}
	}

	// Prevent TXing while an invalid channel is selected
	if (getCurrentChannelInCurrentZoneForGD77S() != chanNum)
	{
		PTTLocked = true;
	}
	else
	{
		if (PTTLocked)
		{
			PTTLocked = false;
			forceSpeech = true;
		}
	}

	if (updateDisplay || forceSpeech)
	{
		// Remove TS override when a new channel is selected, otherwise it will be set until the zone is changed.
		if (updateDisplay && (tsGetManualOverride(CHANNEL_CHANNEL) != 0))
		{
			tsSetManualOverride(CHANNEL_CHANNEL, TS_NO_OVERRIDE);
		}

		if (GD77SParameters.channelOutOfBounds == false)
		{
			char buf[17];

			voicePromptsInit();
			voicePromptsAppendPrompt(PROMPT_CHANNEL);
			voicePromptsAppendInteger(chanNum);
			voicePromptsAppendPrompt(PROMPT_SILENCE);
			codeplugUtilConvertBufToString(currentChannelData->name, buf, 16);
			voicePromptsAppendString(buf);
			voicePromptsPlay();
		}

		if (!forceSpeech)
		{
			uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
			uiChannelModeUpdateScreen(0);
		}
	}
}

static void buildSpeechChannelDetailsForGD77S()
{
	char buf[17];

	codeplugUtilConvertBufToString(currentChannelData->name, buf, 16);
	voicePromptsAppendString(buf);

	voicePromptsAppendPrompt(PROMPT_SILENCE);
	announceFrequency();
	voicePromptsAppendPrompt(PROMPT_SILENCE);

	if (trxGetMode() == RADIO_MODE_DIGITAL)
	{
		announceContactNameTgOrPc(voicePromptsIsPlaying());
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		announceTS();
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		announceCC();
	}
	else
	{
		CodeplugCSSTypes_t type = codeplugGetCSSType(currentChannelData->rxTone);
		if ((type & CSS_TYPE_NONE) == 0)
		{
			buildCSSCodeVoicePrompts(currentChannelData->rxTone, type, DIRECTION_RECEIVE, true);
			voicePromptsAppendPrompt(PROMPT_SILENCE);
		}

		type = codeplugGetCSSType(currentChannelData->txTone);
		if ((type & CSS_TYPE_NONE) == 0)
		{
			buildCSSCodeVoicePrompts(currentChannelData->txTone, type, DIRECTION_TRANSMIT, true);
		}
	}
}

static void buildSpeechUiModeForGD77S(GD77S_UIMODES_t uiMode)
{
	char buf[17];

	if (voicePromptsIsPlaying())
	{
		voicePromptsTerminate();
	}

	switch (uiMode)
	{
		case GD77S_UIMODE_TG_OR_SQUELCH: // Channel
			codeplugUtilConvertBufToString(currentChannelData->name, buf, 16);
			voicePromptsAppendString(buf);

			if (trxGetMode() == RADIO_MODE_DIGITAL)
			{
				announceTS();
			}
			else
			{
				announceSquelchLevel(voicePromptsIsPlaying());
			}
			break;

		case GD77S_UIMODE_SCAN: // Scan
			voicePromptsAppendLanguageString(currentLanguage->scan);
			voicePromptsAppendLanguageString(uiDataGlobal.Scan.active ? currentLanguage->on : currentLanguage->off);
			break;

		case GD77S_UIMODE_TS: // Timeslot
			if (trxGetMode() == RADIO_MODE_DIGITAL)
			{
				announceTS();
			}
			break;

		case GD77S_UIMODE_CC: // Color code
			if (trxGetMode() == RADIO_MODE_DIGITAL)
			{
				announceCC();
			}
			break;

		case GD77S_UIMODE_FILTER: // DMR/Analog filter
			voicePromptsAppendLanguageString(currentLanguage->filter);
			if (trxGetMode() == RADIO_MODE_DIGITAL)
			{
				if (nonVolatileSettings.dmrDestinationFilter == DMR_DESTINATION_FILTER_NONE)
				{
					voicePromptsAppendLanguageString(currentLanguage->none);
				}
				else
				{
					voicePromptsAppendString(DMR_DESTINATION_FILTER_LEVELS[nonVolatileSettings.dmrDestinationFilter - 1]);
				}

			}
			else
			{
				if (nonVolatileSettings.analogFilterLevel == ANALOG_FILTER_NONE)
				{
					voicePromptsAppendLanguageString(currentLanguage->none);
				}
				else
				{
					voicePromptsAppendString((char *)ANALOG_FILTER_LEVELS[nonVolatileSettings.analogFilterLevel - 1]);
				}
			}
			break;

		case GD77S_UIMODE_DTMF_CONTACTS:
			if (GD77SParameters.dtmfListCount > 0)
			{
				struct_codeplugDTMFContact_t dtmfContact;

				codeplugDTMFContactGetDataForNumber(GD77SParameters.dtmfListSelected + 1, &dtmfContact);
				codeplugUtilConvertBufToString(dtmfContact.name, buf, 16);
				voicePromptsAppendString(buf);
			}
			else
			{
				voicePromptsAppendLanguageString(currentLanguage->list_empty);
			}
			break;

		case GD77S_UIMODE_ZONE: // Zone
			announceZoneName(voicePromptsIsPlaying());
			break;

		case GD77S_UIMODE_POWER: // Power
			announcePowerLevel(voicePromptsIsPlaying());
			break;

		case GD77S_UIMODE_ECO:
			announceEcoLevel(voicePromptsIsPlaying());
			break;

		case GD77S_UIMODE_MAX:
			break;
	}
}

static void handleEventForGD77S(uiEvent_t *ev)
{
	if (ev->events & ROTARY_EVENT)
	{
		if (dtmfSequenceIsKeying())
		{
			dtmfSequenceStop();
		}

		if (!trxTransmissionEnabled && (ev->rotary > 0))
		{
			if (uiDataGlobal.Scan.active)
			{
				scanStop(false);
			}

			settingsSet(nonVolatileSettings.overrideTG, 0);
			checkAndUpdateSelectedChannelForGD77S(ev->rotary, false);
			HRC6000ClearActiveDMRID();
			lastHeardClearLastID();
		}
	}

	if (handleMonitorMode(ev))
	{
		return;
	}

	if (ev->events & BUTTON_EVENT)
	{
		if (dtmfSequenceIsKeying() && (ev->buttons & (BUTTON_SK1 | BUTTON_SK2 | BUTTON_ORANGE)))
		{
			dtmfSequenceStop();
		}

		if (BUTTONCHECK_SHORTUP(ev, BUTTON_ORANGE) && uiDataGlobal.Scan.active)
		{
			scanStop(false);

			voicePromptsInit();
			buildSpeechUiModeForGD77S(GD77S_UIMODE_SCAN);
			voicePromptsPlay();
			return;
		}

		if (BUTTONCHECK_LONGDOWN(ev, BUTTON_ORANGE) && (uiDataGlobal.DTMFContactList.isKeying == false))
		{
			announceItem(PROMPT_SEQUENCE_BATTERY, PROMPT_THRESHOLD_3);
			return;
		}
		else if (BUTTONCHECK_SHORTUP(ev, BUTTON_ORANGE) && (uiDataGlobal.DTMFContactList.isKeying == false))
		{
			voicePrompt_t vp = NUM_VOICE_PROMPTS;
			const char *vpString = NULL;

			GD77SParameters.uiMode = (GD77S_UIMODES_t) (GD77SParameters.uiMode + 1) % GD77S_UIMODE_MAX;

			//skip over Digital controls if the radio is in Analog mode
			if (trxGetMode() == RADIO_MODE_ANALOG)
			{
				// Analog
				if ((GD77SParameters.uiMode == GD77S_UIMODE_TS) ||
					(GD77SParameters.uiMode == GD77S_UIMODE_CC))
				{
					GD77SParameters.uiMode = GD77S_UIMODE_FILTER;
				}
			}
			else
			{
				// digital
				if (GD77SParameters.uiMode == GD77S_UIMODE_DTMF_CONTACTS)
				{
					GD77SParameters.uiMode = GD77S_UIMODE_ZONE;
				}
			}

			switch (GD77SParameters.uiMode)
			{
				case GD77S_UIMODE_TG_OR_SQUELCH: // Channel Mode
					if (trxGetMode() == RADIO_MODE_DIGITAL)
					{
						vp = PROMPT_CHANNEL_MODE;
					}
					else
					{
						vpString = currentLanguage->squelch;
					}
					break;

				case GD77S_UIMODE_SCAN:
					vp = PROMPT_SCAN_MODE;
					break;

				case GD77S_UIMODE_TS: // Timeslot Mode
					vp = PROMPT_TIMESLOT_MODE;
					break;

				case GD77S_UIMODE_CC: // ColorCode Mode
					vp = PROMPT_COLORCODE_MODE;
					break;

				case GD77S_UIMODE_FILTER: // DMR/Analog Filter
					vp = PROMPT_FILTER_MODE;
					break;

				case GD77S_UIMODE_DTMF_CONTACTS:
					vpString = currentLanguage->dtmf_contact_list;
					break;

				case GD77S_UIMODE_ZONE: // Zone Mode
					vp = PROMPT_ZONE_MODE;
					break;

				case GD77S_UIMODE_POWER: // Power Mode
					vp = PROMPT_POWER_MODE;
					break;

				case GD77S_UIMODE_ECO:
					vp = PROMPT_ECO_MODE;
					break;

				case GD77S_UIMODE_MAX:
					break;
			}

			if ((vp != NUM_VOICE_PROMPTS) || (vpString != NULL))
			{
				voicePromptsInit();
				if (vpString)
				{
					voicePromptsAppendLanguageString(vpString);
					voicePromptsAppendPrompt(PROMPT_MODE);
				}
				else
				{
					voicePromptsAppendPrompt(vp);
				}
				voicePromptsAppendPrompt(PROMPT_SILENCE);
				buildSpeechUiModeForGD77S(GD77SParameters.uiMode);
				voicePromptsPlay();
			}
		}
		else if (BUTTONCHECK_LONGDOWN(ev, BUTTON_SK1) && (monitorModeData.isEnabled == false) && (uiDataGlobal.DTMFContactList.isKeying == false))
		{
			if (GD77SParameters.channelOutOfBounds == false)
			{
				voicePromptsInit();
				buildSpeechChannelDetailsForGD77S();
				voicePromptsPlay();
			}
		}
		else if (BUTTONCHECK_SHORTUP(ev, BUTTON_SK1) && (uiDataGlobal.DTMFContactList.isKeying == false))
		{
			switch (GD77SParameters.uiMode)
			{
				case GD77S_UIMODE_TG_OR_SQUELCH:
					if (trxGetMode() == RADIO_MODE_DIGITAL)
					{
						// Next in TGList
						if (nonVolatileSettings.overrideTG == 0)
						{
							settingsIncrement(nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE], 1);
							if (nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE] > (currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup - 1))
							{
								settingsSet(nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE], 0);
							}
						}
						settingsSet(nonVolatileSettings.overrideTG, 0);// setting the override TG to 0 indicates the TG is not overridden
						menuPrivateCallClear();
						updateTrxID();
						uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
						uiChannelModeUpdateScreen(0);
						announceItem(PROMPT_SEQUENCE_CONTACT_TG_OR_PC, PROMPT_THRESHOLD_3);
					}
					else
					{
						if(currentChannelData->sql == 0)			//If we were using default squelch level
						{
							currentChannelData->sql = nonVolatileSettings.squelchDefaults[currentRadioDevice->trxCurrentBand[TRX_RX_FREQ_BAND]];			//start the adjustment from that point.
						}

						if (currentChannelData->sql < CODEPLUG_MAX_VARIABLE_SQUELCH)
						{
							currentChannelData->sql++;
						}

						announceItem(PROMPT_SQUENCE_SQUELCH, PROMPT_THRESHOLD_3);
					}
					break;

				case GD77S_UIMODE_SCAN:
					if (uiDataGlobal.Scan.active)
					{
						scanStop(false);
					}
					else
					{
						scanStart(false);
					}

					voicePromptsInit();
					voicePromptsAppendLanguageString(currentLanguage->scan);
					voicePromptsAppendLanguageString(uiDataGlobal.Scan.active ? currentLanguage->on : currentLanguage->off);
					voicePromptsPlay();
					break;

				case GD77S_UIMODE_TS:
					if (trxGetMode() == RADIO_MODE_DIGITAL)
					{
						toggleTimeslotForGD77S();
						announceItem(PROMPT_SEQUENCE_TS, PROMPT_THRESHOLD_3);
					}
					break;

				case GD77S_UIMODE_CC:
					if (trxGetMode() == RADIO_MODE_DIGITAL)
					{
						if (currentChannelData->txColor < 15)
						{
							currentChannelData->txColor++;
							trxSetDMRColourCode(currentChannelData->txColor);
						}

						voicePromptsInit();
						announceCC();
						voicePromptsPlay();
					}
					break;

				case GD77S_UIMODE_FILTER:
					if (trxGetMode() == RADIO_MODE_DIGITAL)
					{
						if (nonVolatileSettings.dmrDestinationFilter < NUM_DMR_DESTINATION_FILTER_LEVELS - 1)
						{
							settingsIncrement(nonVolatileSettings.dmrDestinationFilter, 1);
							HRC6000InitDigitalDmrRx();
							disableAudioAmp(AUDIO_AMP_MODE_RF);
						}
					}
					else
					{
						if (nonVolatileSettings.analogFilterLevel < NUM_ANALOG_FILTER_LEVELS - 1)
						{
							settingsIncrement(nonVolatileSettings.analogFilterLevel, 1);
							trxSetAnalogFilterLevel(nonVolatileSettings.analogFilterLevel);
						}
					}

					voicePromptsInit();
					buildSpeechUiModeForGD77S(GD77SParameters.uiMode);
					voicePromptsPlay();
					break;

				case GD77S_UIMODE_DTMF_CONTACTS:
					// select next DTMF contact and spell it
					if (GD77SParameters.dtmfListCount > 0)
					{
						GD77SParameters.dtmfListSelected = (GD77SParameters.dtmfListSelected + 1) % GD77SParameters.dtmfListCount;
					}
					voicePromptsInit();
					buildSpeechUiModeForGD77S(GD77SParameters.uiMode);
					voicePromptsPlay();
					break;

				case GD77S_UIMODE_ZONE: // Zones
					// No "All Channels" on GD77S
					nonVolatileSettings.currentZone = (nonVolatileSettings.currentZone + 1) % (codeplugZonesGetCount() - 1);

					settingsSet(nonVolatileSettings.overrideTG, 0); // remove any TG override
					tsSetManualOverride(CHANNEL_CHANNEL, TS_NO_OVERRIDE);
					currentChannelData->rxFreq = 0x00; // Flag to the Channel screen that the channel data is now invalid and needs to be reloaded

					menuSystemPopAllAndDisplaySpecificRootMenu(UI_CHANNEL_MODE, true);
					checkAndUpdateSelectedChannelForGD77S(rotarySwitchGetPosition(), false);
					GD77SParameters.uiMode = GD77S_UIMODE_ZONE;

					announceItem(PROMPT_SEQUENCE_ZONE, PROMPT_THRESHOLD_3);
					break;

				case GD77S_UIMODE_POWER: // Power
					increasePowerLevel(true);// true = Allow 5W++
					break;

				case GD77S_UIMODE_ECO:
					if (nonVolatileSettings.ecoLevel < ECO_LEVEL_MAX)
					{
						settingsIncrement(nonVolatileSettings.ecoLevel, 1);
						rxPowerSavingSetLevel(nonVolatileSettings.ecoLevel);
					}
					voicePromptsInit();
					buildSpeechUiModeForGD77S(GD77SParameters.uiMode);
					voicePromptsPlay();
					break;

				case GD77S_UIMODE_MAX:
					break;
			}
		}
		else if (BUTTONCHECK_LONGDOWN(ev, BUTTON_SK2) && (monitorModeData.isEnabled == false) && (uiDataGlobal.DTMFContactList.isKeying == false))
		{
			uint32_t tg = (LinkHead->talkGroupOrPcId & 0xFFFFFF);

			// If Blue button is long pressed during reception it sets the Tx TG to the incoming TG
			if (uiDataGlobal.isDisplayingQSOData && BUTTONCHECK_DOWN(ev, BUTTON_SK2) && (trxGetMode() == RADIO_MODE_DIGITAL) &&
					((trxTalkGroupOrPcId != tg) ||
							((dmrMonitorCapturedTS != -1) && (dmrMonitorCapturedTS != trxGetDMRTimeSlot())) ||
							(trxGetDMRColourCode() != currentChannelData->txColor)))
			{
				lastHeardClearLastID();

				// Set TS to overriden TS
				if ((dmrMonitorCapturedTS != -1) && (dmrMonitorCapturedTS != trxGetDMRTimeSlot()))
				{
					trxSetDMRTimeSlot(dmrMonitorCapturedTS, true);
					tsSetManualOverride(CHANNEL_CHANNEL, (dmrMonitorCapturedTS + 1));
				}
				if (trxTalkGroupOrPcId != tg)
				{
					if ((tg >> 24) & PC_CALL_FLAG)
					{
						acceptPrivateCall(tg & 0xffffff, -1);
					}
					else
					{
						trxTalkGroupOrPcId = tg;
						settingsSet(nonVolatileSettings.overrideTG, trxTalkGroupOrPcId);
					}
				}

				currentChannelData->txColor = trxGetDMRColourCode();// Set the CC to the current CC, which may have been determined by the CC finding algorithm in C6000.c

				uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
				uiChannelModeUpdateScreen(0);

				voicePromptsInit();
				voicePromptsAppendLanguageString(currentLanguage->select_tx);
				voicePromptsPlay();
				return;
			}
		}
		else if (BUTTONCHECK_SHORTUP(ev, BUTTON_SK2) && (uiDataGlobal.DTMFContactList.isKeying == false))
		{
			switch (GD77SParameters.uiMode)
			{
				case GD77S_UIMODE_TG_OR_SQUELCH: // Previous in TGList
					if (trxGetMode() == RADIO_MODE_DIGITAL)
					{
						// To Do change TG in on same channel freq
						if (nonVolatileSettings.overrideTG == 0)
						{
							settingsDecrement(nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE], 1);
							if (nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE] < 0)
							{
								settingsSet(nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE], (int16_t) (currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup - 1));
							}
						}
						settingsSet(nonVolatileSettings.overrideTG, 0);// setting the override TG to 0 indicates the TG is not overridden
						menuPrivateCallClear();
						updateTrxID();
						uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
						uiChannelModeUpdateScreen(0);
						announceItem(PROMPT_SEQUENCE_CONTACT_TG_OR_PC, PROMPT_THRESHOLD_3);
					}
					else
					{
						if(currentChannelData->sql == 0)			//If we were using default squelch level
						{
							currentChannelData->sql = nonVolatileSettings.squelchDefaults[currentRadioDevice->trxCurrentBand[TRX_RX_FREQ_BAND]];			//start the adjustment from that point.
						}

						if (currentChannelData->sql > CODEPLUG_MIN_VARIABLE_SQUELCH)
						{
							currentChannelData->sql--;
						}

						announceItem(PROMPT_SQUENCE_SQUELCH, PROMPT_THRESHOLD_3);
					}
					break;

				case GD77S_UIMODE_SCAN:
					if (uiDataGlobal.Scan.active)
					{
						// if we are scanning and down key is pressed then enter current channel into nuisance delete array.
						if(uiDataGlobal.Scan.state == SCAN_STATE_PAUSED)
						{
							// There is two channels available in the Zone, just stop scanning
							if (uiDataGlobal.Scan.nuisanceDeleteIndex == (uiDataGlobal.Scan.availableChannelsCount - 2))
							{
								uiDataGlobal.Scan.lastIteration = true;
							}

							uiDataGlobal.Scan.nuisanceDelete[uiDataGlobal.Scan.nuisanceDeleteIndex] = uiDataGlobal.currentSelectedChannelNumber;
							uiDataGlobal.Scan.nuisanceDeleteIndex = (uiDataGlobal.Scan.nuisanceDeleteIndex + 1) % MAX_ZONE_SCAN_NUISANCE_CHANNELS;
							uiDataGlobal.Scan.timer.timeout = SCAN_SKIP_CHANNEL_INTERVAL;	//force scan to continue;
							uiDataGlobal.Scan.state = SCAN_STATE_SCANNING;
							return;
						}

						// Left key reverses the scan direction
						if (uiDataGlobal.Scan.state == SCAN_STATE_SCANNING)
						{
							uiDataGlobal.Scan.direction *= -1;
							return;
						}
					}
					break;

				case GD77S_UIMODE_TS:
					if (trxGetMode() == RADIO_MODE_DIGITAL)
					{
						toggleTimeslotForGD77S();
						announceItem(PROMPT_SEQUENCE_TS, PROMPT_THRESHOLD_3);
					}
					break;

				case GD77S_UIMODE_CC:
					if (trxGetMode() == RADIO_MODE_DIGITAL)
					{
						if (currentChannelData->txColor > 0)
						{
							currentChannelData->txColor--;
							trxSetDMRColourCode(currentChannelData->txColor);
						}

						voicePromptsInit();
						announceCC();
						voicePromptsPlay();
					}
					break;

				case GD77S_UIMODE_FILTER:
					if (trxGetMode() == RADIO_MODE_DIGITAL)
					{
						if (nonVolatileSettings.dmrDestinationFilter > DMR_DESTINATION_FILTER_NONE)
						{
							settingsDecrement(nonVolatileSettings.dmrDestinationFilter, 1);
							HRC6000InitDigitalDmrRx();
							disableAudioAmp(AUDIO_AMP_MODE_RF);
						}
					}
					else
					{
						if (nonVolatileSettings.analogFilterLevel > ANALOG_FILTER_NONE)
						{
							settingsDecrement(nonVolatileSettings.analogFilterLevel, 1);
							trxSetAnalogFilterLevel(nonVolatileSettings.analogFilterLevel);
						}
					}

					voicePromptsInit();
					buildSpeechUiModeForGD77S(GD77SParameters.uiMode);
					voicePromptsPlay();
					break;

				case GD77S_UIMODE_DTMF_CONTACTS:
					// select previous DTMF contact and spell it
					if (GD77SParameters.dtmfListCount > 0)
					{
						GD77SParameters.dtmfListSelected = (GD77SParameters.dtmfListSelected + GD77SParameters.dtmfListCount - 1) % GD77SParameters.dtmfListCount;
					}
					voicePromptsInit();
					buildSpeechUiModeForGD77S(GD77SParameters.uiMode);
					voicePromptsPlay();
					break;

				case GD77S_UIMODE_ZONE: // Zones
					// No "All Channels" on GD77S
					nonVolatileSettings.currentZone = (nonVolatileSettings.currentZone + (codeplugZonesGetCount() - 1) - 1) % (codeplugZonesGetCount() - 1);

					settingsSet(nonVolatileSettings.overrideTG, 0); // remove any TG override
					tsSetManualOverride(CHANNEL_CHANNEL, TS_NO_OVERRIDE);
					currentChannelData->rxFreq = 0x00; // Flag to the Channel screeen that the channel data is now invalid and needs to be reloaded

					menuSystemPopAllAndDisplaySpecificRootMenu(UI_CHANNEL_MODE, true);
					checkAndUpdateSelectedChannelForGD77S(rotarySwitchGetPosition(), false);
					GD77SParameters.uiMode = GD77S_UIMODE_ZONE;

					announceItem(PROMPT_SEQUENCE_ZONE, PROMPT_THRESHOLD_3);
					break;

				case GD77S_UIMODE_POWER: // Power
					decreasePowerLevel();
					break;

				case GD77S_UIMODE_ECO:
					if (nonVolatileSettings.ecoLevel > 0)
					{
						settingsDecrement(nonVolatileSettings.ecoLevel, 1);
						rxPowerSavingSetLevel(nonVolatileSettings.ecoLevel);
					}
					voicePromptsInit();
					buildSpeechUiModeForGD77S(GD77SParameters.uiMode);
					voicePromptsPlay();
					break;

				case GD77S_UIMODE_MAX:
					break;
			}
		}
	}
}
#endif // PLATFORM_GD77S
