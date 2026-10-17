/*
 * Copyright (C) 2019-2024 Roger Clark, VK3KYY / G4KYF
 *                         Colin Durbridge, G4EML
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

static void updateScreen(bool isFirstRun, bool allowedToSpeakUpdate);
static void updateCursor(bool moved);
static void handleEvent(uiEvent_t *ev);
static void cssDecrementFromEvent(uiEvent_t *ev, uint16_t *tone, uint8_t *index, CodeplugCSSTypes_t *type);
static void cssIncrementFromEvent(uiEvent_t *ev, uint16_t *tone, uint8_t *index, CodeplugCSSTypes_t *type);
static void saveChanges(uiEvent_t *ev);
static void resetChannelData(void);
static void applyShiftOffset(bool increase);
static void exitCallback(void *data);


static uint8_t RxCSSIndex = 0;
static uint8_t TxCSSIndex = 0;
static CodeplugCSSTypes_t RxCSSType = CSS_TYPE_NONE;
static CodeplugCSSTypes_t TxCSSType = CSS_TYPE_NONE;
static const char CHANNEL_UNSET[5] = { 0xFF, 0xDE, 0xAD, 0xBE, 0xEF };
static struct_codeplugChannel_t tmpChannel =  // update a temporary copy of the channel and only write back if green menu is pressed
{
		.name = { 0xFF, 0xDE, 0xAD, 0xBE, 0xEF }
};
static char channelName[SCREEN_LINE_BUFFER_SIZE];
static int namePos;
static bool nameInError = false;
static uint8_t shiftOffsets[] = { 0, 6, 10, 15, 16, 20, 46, 50, 70, 76, 90, 94 };
static int8_t shiftOffsetMax = (sizeof(shiftOffsets) - 1);
static int8_t shiftOffsetIndex = 0; // Beware, could be negative;

static menuStatus_t menuChannelDetailsExitCode = MENU_STATUS_SUCCESS;

// 5-Tone ANI on-radio editing state (ASELSAN-style direct 5-digit keypad entry).
// Digits are edited in this buffer and only written to tmpChannel on save, so
// leading zeros are preserved and a cancel keeps the previous value.
static bool    fiveToneEditActive = false;
static uint8_t fiveToneEditDigits[5] = { 0, 0, 0, 0, 0 };
static uint8_t fiveToneEditCursor = 0;

// Persist the edited 5-Tone code + mode from tmpChannel into the live channel AND
// to flash, so the setting takes effect immediately for TX and survives a power
// cycle (stored per-channel). VFO has no flash slot, so it is only made live.
static void fiveTonePersist(void)
{
	uint8_t d[5];
	aniTxMode_t m = codeplugChannelGetFiveTone(&tmpChannel, d);

	codeplugChannelSetFiveTone(currentChannelData, d, m); // active immediately for TX
	if (uiDataGlobal.currentSelectedChannelNumber != CH_DETAILS_VFO_CHANNEL)
	{
		codeplugChannelSaveDataForIndex(uiDataGlobal.currentSelectedChannelNumber, currentChannelData); // permanent
	}
}

enum
{
	CH_DETAILS_NAME = 0,
	CH_DETAILS_RXFREQ,
	CH_DETAILS_TXFREQ,
	CH_DETAILS_MODE,
	CH_DETAILS_DMRID,
	CH_DETAILS_DMR_CC,
	CH_DETAILS_DMR_TS,
	CH_DETAILS_RXGROUP,
	CH_DETAILS_CONTACT,
	CH_DETAILS_RXCSS,
	CH_DETAILS_TXCSS,
	CH_DETAILS_FIVE_TONE,
	CH_DETAILS_FIVE_TONE_REPLY,
	CH_DETAILS_BANDWIDTH,
	CH_DETAILS_FREQ_STEP,
	CH_DETAILS_TOT,
	CH_DETAILS_RXONLY,
	CH_DETAILS_ZONE_SKIP,
	CH_DETAILS_ALL_SKIP,
	CH_DETAILS_VOX,
	CH_DETAILS_POWER,
	CH_DETAILS_SQUELCH,
	CH_DETAILS_NO_BEEP,
	CH_DETAILS_NO_ECO,
	CH_DETAILS_TA_TX_TS1,
	CH_DETAILS_TA_TX_TS2,
	CH_DETAILS_APRS_CONFIG,
	CH_DETAILS_DMR_FORCE_DMO,
	CH_DETAILS_DELETE, // last, so the QuickKey numbers of the items above are unchanged
	NUM_CH_DETAILS_ITEMS
};// The last item in the list is used so that we automatically get a total number of items in the list

menuStatus_t menuChannelDetails(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		menuDataGlobal.menuOptionsSetQuickkey = 0;
		menuDataGlobal.menuOptionsTimeout = 0;
		menuDataGlobal.numItems = NUM_CH_DETAILS_ITEMS;
		uiDataGlobal.FreqEnter.index = 0;
		nameInError = false;
		shiftOffsetIndex = 0;
		fiveToneEditActive = false;

		if (memcmp(tmpChannel.name, CHANNEL_UNSET, 5) == 0) // Check if the channel was already loaded (TX Screen was triggered within this menu)
		{
			memcpy(&tmpChannel, currentChannelData, CHANNEL_DATA_STRUCT_SIZE);
		}

		freqEnterReset();

		RxCSSType = codeplugGetCSSType(tmpChannel.rxTone);
		RxCSSIndex = cssGetToneIndex(tmpChannel.rxTone, RxCSSType);

		TxCSSType = codeplugGetCSSType(tmpChannel.txTone);
		TxCSSIndex = cssGetToneIndex(tmpChannel.txTone, TxCSSType);

		codeplugUtilConvertBufToString(tmpChannel.name, channelName, 16);
		namePos = strlen(channelName);

		if ((uiDataGlobal.currentSelectedChannelNumber == CH_DETAILS_VFO_CHANNEL) && (namePos == 0)) // In VFO, and VFO has no name in the codeplug
		{
			snprintf(channelName, SCREEN_LINE_BUFFER_SIZE, "VFO %s", (nonVolatileSettings.currentVFONumber == 0 ? "A" : "B"));
			namePos = 5;
		}

		voicePromptsInit();
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		voicePromptsAppendLanguageString(currentLanguage->channel_details);
		voicePromptsAppendLanguageString(currentLanguage->menu);
		voicePromptsAppendPrompt(PROMPT_SILENCE);

		menuSystemRegisterExitCallback(exitCallback, NULL);

		updateScreen(true, true);
		updateCursor(true);

		return (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);
	}
	else
	{
		menuChannelDetailsExitCode = MENU_STATUS_SUCCESS;

		updateCursor(false);

		if (ev->hasEvent || (menuDataGlobal.menuOptionsTimeout > 0))
		{
			handleEvent(ev);
		}
	}
	return menuChannelDetailsExitCode;
}

static void updateCursor(bool moved)
{
	if (uiDataGlobal.currentSelectedChannelNumber != CH_DETAILS_VFO_CHANNEL)
	{
		displayThemeApply(THEME_ITEM_BG, THEME_ITEM_BG_MENU_ITEM_SELECTED);

		switch (menuDataGlobal.currentItemIndex)
		{
		case CH_DETAILS_NAME:
			menuUpdateCursor(MIN(namePos, 15), moved, true);
			break;
		}

		displayThemeResetToDefault();
	}
}

static void updateScreen(bool isFirstRun, bool allowedToSpeakUpdate)
{
	int mNum = 0;
	char buf[SCREEN_LINE_BUFFER_SIZE];
	int tmpVal;
	int val_before_dp;
	int val_after_dp;
	struct_codeplugRxGroup_t rxGroupBuf;
	struct_codeplugContact_t contactBuf;
	char tmpBuf[SCREEN_LINE_BUFFER_SIZE];
	const char *leftSide = NULL;// initialise to please the compiler
	const char *rightSideConst = NULL;// initialise to please the compiler
	char rightSideVar[SCREEN_LINE_BUFFER_SIZE];
	voicePrompt_t rightSideUnitsPrompt;
	const char *rightSideUnitsStr;

	displayClearBuf();

	bool settingOption = uiQuickKeysShowChoices(buf, SCREEN_LINE_BUFFER_SIZE, currentLanguage->channel_details);

	if (uiDataGlobal.FreqEnter.index != 0)
	{
		snprintf(buf, SCREEN_LINE_BUFFER_SIZE, "%c%c%c%s%c%c%c%c%c%s", uiDataGlobal.FreqEnter.digits[0], uiDataGlobal.FreqEnter.digits[1], uiDataGlobal.FreqEnter.digits[2], (menuDataGlobal.currentItemIndex == CH_DETAILS_DMRID) ? "" : ".",
				uiDataGlobal.FreqEnter.digits[3], uiDataGlobal.FreqEnter.digits[4], uiDataGlobal.FreqEnter.digits[5], uiDataGlobal.FreqEnter.digits[6], uiDataGlobal.FreqEnter.digits[7], (menuDataGlobal.currentItemIndex == CH_DETAILS_DMRID) ? "" : " MHz");
		displayPrintCentered(32, buf, FONT_SIZE_3);
	}
	else
	{
		keypadAlphaEnable = (menuDataGlobal.currentItemIndex == CH_DETAILS_NAME);

		for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
		{
			if ((settingOption == false) || (i == 0))
			{
				mNum = menuGetMenuOffset(NUM_CH_DETAILS_ITEMS, i);
				if (mNum == MENU_OFFSET_BEFORE_FIRST_ENTRY)
				{
					continue;
				}
				else if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
				{
					break;
				}

				buf[0] = 0;
				leftSide = NULL;
				rightSideConst = NULL;
				rightSideVar[0] = 0;
				rightSideUnitsPrompt = PROMPT_SILENCE;// use PROMPT_SILENCE as flag that the unit has not been set
				rightSideUnitsStr = NULL;

				switch(mNum)
				{
					case CH_DETAILS_NAME:
						strncpy(rightSideVar, channelName, SCREEN_LINE_BUFFER_SIZE);
					break;
					case CH_DETAILS_MODE:
						leftSide = currentLanguage->mode;
						strcpy(rightSideVar, (tmpChannel.chMode == RADIO_MODE_ANALOG) ? "FM" : "DMR");
						break;
					break;
					case CH_DETAILS_DMRID:
						leftSide = currentLanguage->dmr_id;
						if (tmpChannel.chMode == RADIO_MODE_ANALOG)
						{
							rightSideConst = currentLanguage->n_a;
						}
						else
						{
							uint32_t dmrID = codeplugChannelGetOptionalDMRID(&tmpChannel);
							if (dmrID == 0)
							{
								rightSideConst = currentLanguage->none;
							}
							else
							{
								snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%u", dmrID);
							}
						}
						break;
					case CH_DETAILS_DMR_CC:
						leftSide = currentLanguage->colour_code;
						rightSideConst = currentLanguage->n_a;
						if (tmpChannel.chMode == RADIO_MODE_ANALOG)
						{
							rightSideConst = currentLanguage->n_a;
						}
						else
						{
							snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%u", tmpChannel.txColor);
						}
						break;
					case CH_DETAILS_DMR_TS:
						leftSide = currentLanguage->timeSlot;
						if (tmpChannel.chMode == RADIO_MODE_ANALOG)
						{
							rightSideConst = currentLanguage->n_a;
						}
						else
						{
							snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%u", ((codeplugChannelGetFlag(&tmpChannel, CHANNEL_FLAG_TIMESLOT_TWO) != 0) ? 2 : 1));
						}
						break;
					case CH_DETAILS_RXGROUP:
						leftSide = currentLanguage->tg_list;
						if (tmpChannel.chMode == RADIO_MODE_DIGITAL)
						{
							if (tmpChannel.rxGroupList == 0)
							{
								rightSideConst = currentLanguage->none;
							}
							else
							{
								codeplugRxGroupGetDataForIndex(tmpChannel.rxGroupList, &rxGroupBuf);
								codeplugUtilConvertBufToString(rxGroupBuf.name, tmpBuf, 16);
								snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%s", tmpBuf);
							}
						}
						else
						{
							rightSideConst = currentLanguage->n_a;
						}
						break;
					case CH_DETAILS_CONTACT:
						leftSide = currentLanguage->contact;
						if (tmpChannel.chMode == RADIO_MODE_DIGITAL)
						{
							if (tmpChannel.contact == 0)
							{
								rightSideConst = currentLanguage->none;
							}
							else
							{
								codeplugContactGetDataForIndex(tmpChannel.contact, &contactBuf);
								codeplugUtilConvertBufToString(contactBuf.name, tmpBuf, 16);
								snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%s", tmpBuf);
							}
						}
						else
						{
							rightSideConst = currentLanguage->n_a;
						}
						break;
					case CH_DETAILS_RXCSS:
						if ((tmpChannel.chMode == RADIO_MODE_ANALOG) && (tmpChannel.aprsConfigIndex == 0))
						{
							if (RxCSSType == CSS_TYPE_CTCSS)
							{
								snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "Rx CTCSS:%u.%uHz", tmpChannel.rxTone / 10 , tmpChannel.rxTone % 10);
							}
							else if (RxCSSType & CSS_TYPE_DCS)
							{
								dcsPrintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "Rx DCS:", tmpChannel.rxTone);
							}
							else
							{
								snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "Rx CSS:%s", currentLanguage->none);
							}
						}
						else
						{
							snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "Rx CSS:%s", currentLanguage->n_a);
						}
						break;
					case CH_DETAILS_TXCSS:
						if ((tmpChannel.chMode == RADIO_MODE_ANALOG) && (tmpChannel.aprsConfigIndex == 0))
						{
							if  (TxCSSType == CSS_TYPE_CTCSS)
							{
								snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "Tx CTCSS:%u.%uHz", tmpChannel.txTone / 10 , tmpChannel.txTone % 10);
							}
							else if (TxCSSType & CSS_TYPE_DCS)
							{
								dcsPrintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "Tx DCS:", tmpChannel.txTone);
							}
							else
							{
								snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "Tx CSS:%s", currentLanguage->none);
							}
						}
						else
						{
							snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "Tx CSS:%s", currentLanguage->n_a);
						}
						break;
					case CH_DETAILS_FIVE_TONE:
						if (tmpChannel.chMode == RADIO_MODE_ANALOG)
						{
							if (fiveToneEditActive)
							{
								// Editing: show the 5 digits with a [ ] cursor around the active one
								char b[SCREEN_LINE_BUFFER_SIZE];
								int n = 0;
								b[n++] = '5'; b[n++] = 'T'; b[n++] = ':';
								for (int k = 0; (k < 5) && (n < (SCREEN_LINE_BUFFER_SIZE - 4)); k++)
								{
									if (k == fiveToneEditCursor)
									{
										b[n++] = '['; b[n++] = '0' + fiveToneEditDigits[k]; b[n++] = ']';
									}
									else
									{
										b[n++] = '0' + fiveToneEditDigits[k];
									}
								}
								b[n] = 0;
								strncpy(rightSideVar, b, SCREEN_LINE_BUFFER_SIZE);
							}
							else
							{
								uint8_t d[5];
								aniTxMode_t mode = codeplugChannelGetFiveTone(&tmpChannel, d);
								const char *modeStr = (mode == ANI_MODE_PTT_START) ? "St" :
										((mode == ANI_MODE_PTT_END) ? "En" :
										((mode == ANI_MODE_PTT_BOTH) ? "St+En" : currentLanguage->off));
								snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "5T:%u%u%u%u%u %s",
										d[0], d[1], d[2], d[3], d[4], modeStr);
							}
						}
						else
						{
							snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "5T:%s", currentLanguage->n_a);
						}
						break;
					case CH_DETAILS_FIVE_TONE_REPLY:
						if (tmpChannel.chMode == RADIO_MODE_ANALOG)
						{
							snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, LANG4("5T Cevap:%s", "5T Reply:%s", "5T Resp.:%s", "5T \xCE\xF2\xE2\xE5\xF2:%s"),
									(codeplugChannelGetFiveToneReply(&tmpChannel) ? currentLanguage->on : currentLanguage->off));
						}
						else
						{
							snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, LANG4("5T Cevap:%s", "5T Reply:%s", "5T Resp.:%s", "5T \xCE\xF2\xE2\xE5\xF2:%s"), currentLanguage->n_a);
						}
						break;
					case CH_DETAILS_RXFREQ:
						rightSideUnitsPrompt = PROMPT_MEGAHERTZ;
						rightSideUnitsStr = "MHz";
						val_before_dp = tmpChannel.rxFreq / 100000;
						val_after_dp = tmpChannel.rxFreq - val_before_dp * 100000;
						snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "Rx:%d.%05d", val_before_dp, val_after_dp);
						break;
					case CH_DETAILS_TXFREQ:
						rightSideUnitsPrompt = PROMPT_MEGAHERTZ;
						rightSideUnitsStr = "MHz";
						val_before_dp = tmpChannel.txFreq / 100000;
						val_after_dp = tmpChannel.txFreq - val_before_dp * 100000;
						snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "Tx:%d.%05d", val_before_dp, val_after_dp);
						break;
					case CH_DETAILS_BANDWIDTH:
						// Bandwidth
						leftSide = currentLanguage->bandwidth;
						if (tmpChannel.chMode == RADIO_MODE_DIGITAL)
						{
							rightSideConst = currentLanguage->n_a;
						}
						else
						{
							rightSideUnitsPrompt = PROMPT_KILOHERTZ;
							rightSideUnitsStr = "kHz";
							snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%s", ((codeplugChannelGetFlag(&tmpChannel, CHANNEL_FLAG_BW_25K) != 0)) ? "25" : "12.5");
						}
						break;
					case CH_DETAILS_FREQ_STEP:
						rightSideUnitsPrompt = PROMPT_KILOHERTZ;
						rightSideUnitsStr = "kHz";
						leftSide = currentLanguage->stepFreq;
						tmpVal = VFO_FREQ_STEP_TABLE[(tmpChannel.VFOflag5 >> 4)] / 100;
						snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%u.%02u", tmpVal, VFO_FREQ_STEP_TABLE[(tmpChannel.VFOflag5 >> 4)] - (tmpVal * 100));
						break;
					case CH_DETAILS_TOT:// TOT
						leftSide = currentLanguage->tot;
						if (tmpChannel.tot != 0)
						{
							rightSideUnitsPrompt = PROMPT_SECONDS;
							rightSideUnitsStr = "s";

							snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%u", tmpChannel.tot * 15);
						}
						else
						{
							rightSideConst = currentLanguage->off;
						}
						break;
					case CH_DETAILS_RXONLY:
						leftSide = currentLanguage->rx_only;
						rightSideConst = ((codeplugChannelGetFlag(&tmpChannel, CHANNEL_FLAG_RX_ONLY) != 0) ? currentLanguage->yes : currentLanguage->no);
						break;
					case CH_DETAILS_ZONE_SKIP:						// Zone Scan Skip Channel (Using CPS Auto Scan flag)
						leftSide = currentLanguage->zone_skip;
						rightSideConst = ((codeplugChannelGetFlag(&tmpChannel, CHANNEL_FLAG_ZONE_SKIP) != 0) ? currentLanguage->yes : currentLanguage->no);
						break;
					case CH_DETAILS_ALL_SKIP:					// All Scan Skip Channel (Using CPS Lone Worker flag)
						leftSide = currentLanguage->all_skip;
						rightSideConst = ((codeplugChannelGetFlag(&tmpChannel, CHANNEL_FLAG_ALL_SKIP) != 0) ? currentLanguage->yes : currentLanguage->no);
						break;
					case CH_DETAILS_VOX:
						if (tmpChannel.chMode == RADIO_MODE_DIGITAL || tmpChannel.aprsConfigIndex == 0)
						{
							rightSideConst = ((codeplugChannelGetFlag(&tmpChannel, CHANNEL_FLAG_VOX) != 0) ? currentLanguage->on : currentLanguage->off);
						}
						else
						{
							rightSideConst = currentLanguage->n_a;
						}
						snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "VOX:%s", rightSideConst);
						break;
					case CH_DETAILS_POWER:
						leftSide = currentLanguage->channel_power;
						if (uiDataGlobal.currentSelectedChannelNumber == CH_DETAILS_VFO_CHANNEL)
						{
							rightSideConst = currentLanguage->n_a;
						}
						else
						{
							if (tmpChannel.libreDMR_Power == 0)
							{
								rightSideConst = currentLanguage->from_master;
							}
							else
							{
								int powerIndex = tmpChannel.libreDMR_Power - 1;
								snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%s%s", getPowerLevel(powerIndex), getPowerLevelUnit(powerIndex));
							}
						}
						break;
					case CH_DETAILS_SQUELCH:
						leftSide = currentLanguage->squelch;
						if (tmpChannel.chMode == RADIO_MODE_DIGITAL)
						{
							rightSideConst = currentLanguage->n_a;
						}
						else
						{
							if (tmpChannel.sql == 0)
							{
								rightSideConst = currentLanguage->from_master;
							}
							else
							{
								snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%u%%", (5 * (tmpChannel.sql - 1)));
							}
						}
						break;
					case CH_DETAILS_NO_BEEP:
						leftSide = currentLanguage->beep;
						rightSideConst = ((codeplugChannelGetFlag(&tmpChannel, CHANNEL_FLAG_NO_BEEP) != 0) ? currentLanguage->no : currentLanguage->yes);
						break;
					case CH_DETAILS_NO_ECO:
						leftSide = currentLanguage->eco;
						rightSideConst = ((codeplugChannelGetFlag(&tmpChannel, CHANNEL_FLAG_NO_ECO) != 0) ? currentLanguage->no : currentLanguage->yes);
						break;
					case CH_DETAILS_TA_TX_TS1:
					case CH_DETAILS_TA_TX_TS2:
						{
							bool isTS1 = (mNum == CH_DETAILS_TA_TX_TS1);

							leftSide = (isTS1 ? currentLanguage->transmitTalkerAliasTS1 : currentLanguage->transmitTalkerAliasTS2);
							if (tmpChannel.chMode == RADIO_MODE_DIGITAL)
							{
								switch(codeplugGetTATxForTS(&tmpChannel, (isTS1 ? 0 : 1)))
								{
									case TA_TX_OFF:
										rightSideConst = currentLanguage->off;
										break;
									case TA_TX_APRS:
										rightSideConst = currentLanguage->APRS;
										break;
									case TA_TX_TEXT:
										rightSideConst = currentLanguage->ta_text;
										break;
									case TA_TX_BOTH:
										rightSideConst = currentLanguage->both;
										break;
								}
							}
							else
							{
								rightSideConst = currentLanguage->n_a;
							}
						}
						break;
					case CH_DETAILS_APRS_CONFIG:
						leftSide = currentLanguage->APRS;
						if (tmpChannel.chMode == RADIO_MODE_DIGITAL)
						{
							rightSideConst = currentLanguage->n_a;
						}
						else
						{
							if (tmpChannel.aprsConfigIndex == 0)
							{
								rightSideConst = currentLanguage->none;
							}
							else
							{
								char nameBuf[9];
								codeplugAPRS_Config_t aprsConfig;

								if (codeplugAPRSGetDataForIndex(tmpChannel.aprsConfigIndex, &aprsConfig))
								{
									codeplugUtilConvertBufToString((char *)&aprsConfig.name, nameBuf, sizeof(aprsConfig.name));
								}
								else
								{
									snprintf(nameBuf, sizeof(nameBuf), "%s", "INVALID");
								}

								snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%s", nameBuf);
							}
						}
						break;
					case CH_DETAILS_DMR_FORCE_DMO:
						leftSide = currentLanguage->dmr_force_dmo;
						if (tmpChannel.chMode == RADIO_MODE_ANALOG)
						{
							rightSideConst = currentLanguage->n_a;
						}
						else
						{
							rightSideConst = ((codeplugChannelGetFlag(&tmpChannel, CHANNEL_FLAG_FORCE_DMO) != 0) ? currentLanguage->yes : currentLanguage->no);
						}
						break;
					case CH_DETAILS_DELETE:
						// An action rather than a setting: no "label:value", just the name.
						snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%s", menuChannelZoneDeleteLabel());
						break;
				}

				if (leftSide != NULL)
				{
					snprintf(buf, SCREEN_LINE_BUFFER_SIZE, "%s:%s", leftSide, (rightSideVar[0] ? rightSideVar : (rightSideConst ? rightSideConst : "")));
				}
				else
				{
					strcpy(buf, rightSideVar);
				}

				if ((i == 0) && allowedToSpeakUpdate && (uiDataGlobal.FreqEnter.index == 0))
				{
					if (!isFirstRun && (menuDataGlobal.menuOptionsSetQuickkey == 0))
					{
						voicePromptsInit();
					}

					if ((mNum == CH_DETAILS_NAME) && (rightSideVar[0] == 0))
					{
						if (nameInError)
						{
							voicePromptsAppendLanguageString(currentLanguage->error);
							voicePromptsAppendPrompt(PROMPT_SILENCE);
						}
						voicePromptsAppendLanguageString(currentLanguage->name);
						voicePromptsAppendPrompt(PROMPT_SILENCE);
						voicePromptsAppendLanguageString(currentLanguage->none);
					}
					else if ((mNum == CH_DETAILS_RXCSS) || (mNum == CH_DETAILS_TXCSS))
					{
						if (tmpChannel.chMode == RADIO_MODE_ANALOG)
						{
							switch (mNum)
							{
								case CH_DETAILS_RXCSS:
									announceCSSCode(tmpChannel.rxTone, RxCSSType, DIRECTION_RECEIVE, true, AUDIO_PROMPT_MODE_VOICE_LEVEL_3);
									break;
								case CH_DETAILS_TXCSS:
									announceCSSCode(tmpChannel.txTone, TxCSSType, DIRECTION_TRANSMIT, true, AUDIO_PROMPT_MODE_VOICE_LEVEL_3);
									break;
								default:
									break;
							}
						}
						else
						{
							voicePromptsAppendString(((mNum == CH_DETAILS_RXCSS) ? "Rx CSS" : "Tx CSS"));
							voicePromptsAppendLanguageString(currentLanguage->n_a);
						}
					}
					else if (mNum == CH_DETAILS_VOX)
					{
						voicePromptsAppendPrompt(PROMPT_VOX);
						voicePromptsAppendLanguageString(rightSideConst);
					}
					else if ((mNum == CH_DETAILS_POWER) &&
							((tmpChannel.libreDMR_Power != 0) && (uiDataGlobal.currentSelectedChannelNumber != CH_DETAILS_VFO_CHANNEL)))
					{
						char buf2[17];
						char *p;

						voicePromptsAppendLanguageString(leftSide);
						memcpy(buf2, rightSideVar, 17);
						if ((p = strstr(buf2, "mW")))
						{
							*p = 0;
							voicePromptsAppendString(buf2);
							voicePromptsAppendPrompt(PROMPT_MILLIWATTS);
						}
						else if (strstr(buf2, "+W-"))
						{
							voicePromptsAppendLanguageString(currentLanguage->user_power);
						}
						else if ((p = strstr(buf2, "W")))
						{
							*p = 0;
							voicePromptsAppendString(buf2);
							voicePromptsAppendPrompt((((tmpChannel.libreDMR_Power - 1) == 4) ? PROMPT_WATT : PROMPT_WATTS));
						}
					}
					else
					{
						if (leftSide != NULL)
						{
							voicePromptsAppendLanguageString(leftSide);
						}

						if ((rightSideVar[0] != 0) || ((rightSideVar[0] == 0) && (rightSideConst == NULL)))
						{
							voicePromptsAppendString(rightSideVar);
						}
						else
						{
							voicePromptsAppendLanguageString(rightSideConst);
						}

						if (rightSideUnitsPrompt != PROMPT_SILENCE)
						{
							voicePromptsAppendPrompt(rightSideUnitsPrompt);
						}

						if (rightSideUnitsStr != NULL)
						{
							strncat(rightSideVar, rightSideUnitsStr, SCREEN_LINE_BUFFER_SIZE);
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
					if (rightSideUnitsStr != NULL)
					{
						strncat(buf, rightSideUnitsStr, SCREEN_LINE_BUFFER_SIZE);
					}

					switch(mNum)
					{
						case CH_DETAILS_NAME:
						case CH_DETAILS_DELETE: // no ':' to split at - the default case would look for one
							menuDisplayEntry(i, mNum, buf, -1, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
							break;

						default:
							menuDisplayEntry(i, mNum, buf, (((leftSide != NULL) ? strlen(leftSide) : (strchr(rightSideVar, ':') - rightSideVar)) + 1), THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
							break;
					}
				}
			}
		}
	}
	displayRender();
}

static void updateFrequency(int frequency)
{
	switch (menuDataGlobal.currentItemIndex)
	{
		case CH_DETAILS_RXFREQ:
			tmpChannel.rxFreq = frequency;
			break;
		case CH_DETAILS_TXFREQ:
			tmpChannel.txFreq = frequency;
			break;
	}

	freqEnterReset();
}

static void handleEvent(uiEvent_t *ev)
{
	int tmpVal;
	struct_codeplugRxGroup_t rxGroupBuf;
	struct_codeplugContact_t contactBuf;
	//bool isDirty = false;

	if ((menuDataGlobal.menuOptionsTimeout > 0) && (!BUTTONCHECK_DOWN(ev, BUTTON_SK2)))
	{
		menuDataGlobal.menuOptionsTimeout--;
		if (menuDataGlobal.menuOptionsTimeout == 0)
		{
			resetChannelData();
			menuSystemPopPreviousMenu();
			return;
		}
	}

	if (ev->events & FUNCTION_EVENT)
	{
		if (ev->function == FUNC_REDRAW)
		{
			updateScreen(false, false);
			return;
		}
		else if ((QUICKKEY_TYPE(ev->function) == QUICKKEY_MENU) && (QUICKKEY_ENTRYID(ev->function) < NUM_CH_DETAILS_ITEMS))
		{
			menuDataGlobal.currentItemIndex = QUICKKEY_ENTRYID(ev->function);
		}

		if ((QUICKKEY_FUNCTIONID(ev->function) != 0) &&
			((menuDataGlobal.currentItemIndex != CH_DETAILS_NAME) && (menuDataGlobal.currentItemIndex != CH_DETAILS_RXFREQ) && (menuDataGlobal.currentItemIndex != CH_DETAILS_TXFREQ)))
		{
			menuDataGlobal.menuOptionsTimeout = 1000;
		}

		updateScreen(false, true);
	}

	if (ev->events & BUTTON_EVENT)
	{
		if (BUTTONCHECK_SHORTUP(ev, BUTTON_SK1))
		{
			if (menuDataGlobal.currentItemIndex == CH_DETAILS_RXCSS)
			{
				announceCSSCode(tmpChannel.rxTone, RxCSSType, DIRECTION_RECEIVE, true, PROMPT_THRESHOLD_NEVER_PLAY_IMMEDIATELY);
			}
			else if (menuDataGlobal.currentItemIndex == CH_DETAILS_TXCSS)
			{
				announceCSSCode(tmpChannel.txTone, TxCSSType, DIRECTION_TRANSMIT, true, PROMPT_THRESHOLD_NEVER_PLAY_IMMEDIATELY);
			}
		}

		if (repeatVoicePromptOnSK1(ev))
		{
			return;
		}
	}

	if ((menuDataGlobal.currentItemIndex == CH_DETAILS_RXFREQ) || (menuDataGlobal.currentItemIndex == CH_DETAILS_TXFREQ) || (menuDataGlobal.currentItemIndex == CH_DETAILS_DMRID))
	{
		if (uiDataGlobal.FreqEnter.index != 0)
		{
			if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
			{
				int number = freqEnterRead(0, 8, (menuDataGlobal.currentItemIndex == CH_DETAILS_DMRID));

				if (((menuDataGlobal.currentItemIndex == CH_DETAILS_RXFREQ) || (menuDataGlobal.currentItemIndex == CH_DETAILS_TXFREQ))
						&& (trxGetBandFromFrequency(number) != FREQUENCY_OUT_OF_BAND))
				{
					updateFrequency(number);
				}
				else if ((menuDataGlobal.currentItemIndex == CH_DETAILS_DMRID)
						&& (((number >= MIN_TG_OR_PC_VALUE) && (number <= MAX_TG_OR_PC_VALUE)) || (number == 0)))
				{
					codeplugChannelSetOptionalDMRID(&tmpChannel, number);
					freqEnterReset();
				}
				else
				{
					menuChannelDetailsExitCode |= MENU_STATUS_ERROR;
				}
				updateScreen(false, true);
				return;
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
			{
				freqEnterReset();
				updateScreen(false, true);
				return;
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_LEFT))
			{
				char buf[17];

				uiDataGlobal.FreqEnter.index--;
				uiDataGlobal.FreqEnter.digits[uiDataGlobal.FreqEnter.index] = '-';

				voicePromptsInit();
				snprintf(buf, 17, "%c%c%c%s%c%c%c%c%c", uiDataGlobal.FreqEnter.digits[0], uiDataGlobal.FreqEnter.digits[1], uiDataGlobal.FreqEnter.digits[2], (menuDataGlobal.currentItemIndex == CH_DETAILS_DMRID) ? "" : ".",
						uiDataGlobal.FreqEnter.digits[3], uiDataGlobal.FreqEnter.digits[4], uiDataGlobal.FreqEnter.digits[5], uiDataGlobal.FreqEnter.digits[6], uiDataGlobal.FreqEnter.digits[7]);
				buf[(menuDataGlobal.currentItemIndex != CH_DETAILS_DMRID) ? ((uiDataGlobal.FreqEnter.index > 2) ? (uiDataGlobal.FreqEnter.index + 1) : uiDataGlobal.FreqEnter.index) : uiDataGlobal.FreqEnter.index] = 0;
				voicePromptsAppendString(buf);
				if (menuDataGlobal.currentItemIndex != CH_DETAILS_DMRID)
				{
					voicePromptsAppendPrompt(PROMPT_MEGAHERTZ);
				}
				voicePromptsPlay();

				updateScreen(false, true);

				return;
			}
		}

		if ((uiDataGlobal.FreqEnter.index < 8))
		{
			if (!BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				int keyval = menuGetKeypadKeyValue(ev, true);

				if ((keyval != 99) &&
						((menuDataGlobal.currentItemIndex == CH_DETAILS_DMRID) || (((uiDataGlobal.FreqEnter.index == 0) && (keyval == 0)) == false)))
				{
					voicePromptsInit();
					voicePromptsAppendInteger(keyval);
					voicePromptsPlay();

					uiDataGlobal.FreqEnter.digits[uiDataGlobal.FreqEnter.index] = (char) keyval + '0';
					uiDataGlobal.FreqEnter.index++;

					if (uiDataGlobal.FreqEnter.index == 8)
					{
						int number = freqEnterRead(0, 8, (menuDataGlobal.currentItemIndex == CH_DETAILS_DMRID));

						if (((menuDataGlobal.currentItemIndex == CH_DETAILS_RXFREQ) || (menuDataGlobal.currentItemIndex == CH_DETAILS_TXFREQ))
								&& (trxGetBandFromFrequency(number) != FREQUENCY_OUT_OF_BAND))
						{
							updateFrequency(number);
						}
						else if ((menuDataGlobal.currentItemIndex == CH_DETAILS_DMRID)
								&& (((number >= MIN_TG_OR_PC_VALUE) && (number <= MAX_TG_OR_PC_VALUE)) || (number == 0)))
						{
							codeplugChannelSetOptionalDMRID(&tmpChannel, number);
							freqEnterReset();
						}
						else
						{
							uiDataGlobal.FreqEnter.index--;
							uiDataGlobal.FreqEnter.digits[uiDataGlobal.FreqEnter.index] = '-';
							soundSetMelody(MELODY_ERROR_BEEP);
						}
					}
					updateScreen(false, true);
					return;
				}
			}
			else
			{
				if (KEYCHECK_PRESS_NUMBER(ev->keys))
				{
					soundSetMelody(MELODY_ERROR_BEEP);
				}
			}
		}
	}

	// 5-Tone ANI direct keypad entry (ASELSAN-style): typed digits, cursor, save/cancel.
	// Handled before the generic list key handling so these keys are captured here.
	// Leading zeros are preserved because the 5 digits are edited/stored individually.
	if ((menuDataGlobal.currentItemIndex == CH_DETAILS_FIVE_TONE) && (tmpChannel.chMode == RADIO_MODE_ANALOG))
	{
		int aniKeyval = menuGetKeypadKeyValue(ev, true);

		// A number key 0-9: type a digit directly (leading zeros allowed, e.g. 03310)
		if ((aniKeyval != 99) && !BUTTONCHECK_DOWN(ev, BUTTON_SK2) && !BUTTONCHECK_DOWN(ev, BUTTON_SK1))
		{
			if (fiveToneEditActive == false)
			{
				codeplugChannelGetFiveTone(&tmpChannel, fiveToneEditDigits); // start from stored code
				fiveToneEditActive = true;
				fiveToneEditCursor = 0;
			}

			if (fiveToneEditCursor > 4)
			{
				fiveToneEditCursor = 4;
			}
			fiveToneEditDigits[fiveToneEditCursor] = (uint8_t) aniKeyval;

			if (fiveToneEditCursor < 4)
			{
				fiveToneEditCursor++;
			}
			else
			{
				// 5th digit typed: auto-save; enable ANI (PTT-start) if it was Off
				// A freshly entered code defaults to "send at PTT start" (start only) -
				// the common case. The mode can still be changed afterwards with the
				// left/right keys. Forcing it here also avoids inheriting a stale ANI
				// mode from a previously-unused codeplug byte.
				codeplugChannelSetFiveTone(&tmpChannel, fiveToneEditDigits, ANI_MODE_PTT_START);
					fiveTonePersist();
				fiveToneEditActive = false;
			}
			updateScreen(false, true);
			return;
		}

		if (fiveToneEditActive)
		{
			if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
			{
				// A freshly entered code defaults to "send at PTT start" (start only) -
				// the common case. The mode can still be changed afterwards with the
				// left/right keys. Forcing it here also avoids inheriting a stale ANI
				// mode from a previously-unused codeplug byte.
				codeplugChannelSetFiveTone(&tmpChannel, fiveToneEditDigits, ANI_MODE_PTT_START);
					fiveTonePersist();
				fiveToneEditActive = false;
				updateScreen(false, true);
				return;
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
			{
				fiveToneEditActive = false; // cancel: keep previous stored value
				updateScreen(false, true);
				return;
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_LEFT))
			{
				if (fiveToneEditCursor > 0)
				{
					fiveToneEditCursor--;
				}
				updateScreen(false, true);
				return;
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_RIGHT))
			{
				if (fiveToneEditCursor < 4)
				{
					fiveToneEditCursor++;
				}
				updateScreen(false, true);
				return;
			}
			else if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				fiveToneEditDigits[fiveToneEditCursor] = (fiveToneEditDigits[fiveToneEditCursor] + 1) % 10; // knob-like up
				updateScreen(false, true);
				return;
			}
			else if (BUTTONCHECK_DOWN(ev, BUTTON_SK1))
			{
				fiveToneEditDigits[fiveToneEditCursor] = (fiveToneEditDigits[fiveToneEditCursor] + 9) % 10; // knob-like down
				updateScreen(false, true);
				return;
			}
		}
	}

	// Delete Channel: green opens the password / "are you sure" screen instead of
	// saving. Only for a channel that exists - not the VFO, and not the stand-in
	// shown for an empty zone, whose number is 0.
	if ((menuDataGlobal.currentItemIndex == CH_DETAILS_DELETE) && (ev->events & KEY_EVENT) &&
			KEYCHECK_SHORTUP(ev->keys, KEY_GREEN) && (menuDataGlobal.menuOptionsSetQuickkey == 0) && (menuDataGlobal.menuOptionsTimeout == 0))
	{
		if (codeplugAllChannelsIndexIsInUse(uiDataGlobal.currentSelectedChannelNumber))
		{
			menuChannelZoneStartDelete(uiDataGlobal.currentSelectedChannelNumber);
		}
		else
		{
			menuChannelDetailsExitCode |= MENU_STATUS_ERROR;
		}
		return;
	}

	// Not entering a frequency numeric digit
	bool allowedToSpeakUpdate = true;

	if ((ev->events & KEY_EVENT) && (menuDataGlobal.menuOptionsSetQuickkey == 0) && (menuDataGlobal.menuOptionsTimeout == 0))
	{
		if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
		{
			fiveToneEditActive = false; // leaving the item cancels any in-progress 5-Tone edit
			menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, NUM_CH_DETAILS_ITEMS);
			updateScreen(false, true);
			menuChannelDetailsExitCode |= MENU_STATUS_LIST_TYPE;
		}
		else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
		{
			fiveToneEditActive = false; // leaving the item cancels any in-progress 5-Tone edit
			menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, NUM_CH_DETAILS_ITEMS);
			updateScreen(false, true);
			menuChannelDetailsExitCode |= MENU_STATUS_LIST_TYPE;
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			if (strlen(channelName) == 0) // Do not permit empty names, like CPS does
			{
				menuDataGlobal.currentItemIndex = CH_DETAILS_NAME;
				nameInError = true;
				updateScreen(false, true);
				nameInError = false;
				menuChannelDetailsExitCode |= MENU_STATUS_ERROR;
			}
			else
			{
				saveChanges(ev);

				menuSystemPopAllAndDisplayRootMenu();
			}
			return;
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			menuSystemPopPreviousMenu();
			return;
		}
		else if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && BUTTONCHECK_DOWN(ev, BUTTON_SK2))
		{
			menuDataGlobal.menuOptionsSetQuickkey = ev->keys.key;
			updateScreen(false, false);
		}
	}

	if ((ev->events & (KEY_EVENT | FUNCTION_EVENT)) && (menuDataGlobal.menuOptionsSetQuickkey == 0))
	{
		if (KEYCHECK_LONGDOWN(ev->keys, KEY_RIGHT) || KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_RIGHT))
		{
			if (menuDataGlobal.currentItemIndex == CH_DETAILS_NAME)
			{
				namePos = strlen(channelName);
				updateScreen(false, !voicePromptsIsPlaying());
			}
			else if ((menuDataGlobal.currentItemIndex == CH_DETAILS_RXCSS) || (menuDataGlobal.currentItemIndex == CH_DETAILS_TXCSS))
			{
				goto handlesRightKey;
			}
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_RIGHT)
#if defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
				|| KEYCHECK_SHORTUP(ev->keys, KEY_ROTARY_INCREMENT)
#endif
				|| (QUICKKEY_FUNCTIONID(ev->function) == FUNC_RIGHT))
		{
			handlesRightKey:
			if (menuDataGlobal.menuOptionsTimeout > 0)
			{
				menuDataGlobal.menuOptionsTimeout = 1000;
			}

			switch(menuDataGlobal.currentItemIndex)
			{
				case CH_DETAILS_NAME:
					if (uiDataGlobal.currentSelectedChannelNumber != CH_DETAILS_VFO_CHANNEL)
					{
						moveCursorRightInString(channelName, &namePos, 16, BUTTONCHECK_DOWN(ev, BUTTON_SK2));
						updateCursor(true);
						allowedToSpeakUpdate = (strlen(channelName) == 0);
					}
					break;
				case CH_DETAILS_RXFREQ:
				case CH_DETAILS_TXFREQ:
					applyShiftOffset(true);
					break;
				case CH_DETAILS_MODE:
					if (tmpChannel.chMode == RADIO_MODE_DIGITAL)
					{
						tmpChannel.chMode = RADIO_MODE_ANALOG;
					}
					break;
				case CH_DETAILS_DMR_CC:
					if (tmpChannel.chMode == RADIO_MODE_DIGITAL)
					{
						if (tmpChannel.txColor < 15)
						{
							tmpChannel.txColor++;
							trxSetDMRColourCode(tmpChannel.txColor);
						}
					}
					break;
				case CH_DETAILS_DMR_TS:
					if (tmpChannel.chMode == RADIO_MODE_DIGITAL)
					{
						codeplugChannelSetFlag(&tmpChannel, CHANNEL_FLAG_TIMESLOT_TWO, 1);
					}
					break;
				case CH_DETAILS_RXCSS:
					if (tmpChannel.chMode == RADIO_MODE_ANALOG)
					{
						bool voicePromptWasPlaying = voicePromptsIsPlaying();

						cssIncrementFromEvent(ev, &tmpChannel.rxTone, &RxCSSIndex, &RxCSSType);
						trxSetRxCSS(RADIO_DEVICE_PRIMARY, tmpChannel.rxTone);
						announceCSSCode(tmpChannel.rxTone, RxCSSType,
								(((voicePromptWasPlaying == false) && (nonVolatileSettings.audioPromptMode > AUDIO_PROMPT_MODE_VOICE_LEVEL_2)) ? DIRECTION_RECEIVE : DIRECTION_NONE),
								(voicePromptWasPlaying == false), AUDIO_PROMPT_MODE_VOICE_LEVEL_1);
						allowedToSpeakUpdate = false;
					}
					break;
				case CH_DETAILS_TXCSS:
					if (tmpChannel.chMode == RADIO_MODE_ANALOG)
					{
						bool voicePromptWasPlaying = voicePromptsIsPlaying();

						cssIncrementFromEvent(ev, &tmpChannel.txTone, &TxCSSIndex, &TxCSSType);
						announceCSSCode(tmpChannel.txTone, TxCSSType,
								(((voicePromptWasPlaying == false) && (nonVolatileSettings.audioPromptMode > AUDIO_PROMPT_MODE_VOICE_LEVEL_2)) ? DIRECTION_TRANSMIT : DIRECTION_NONE),
								(voicePromptWasPlaying == false), AUDIO_PROMPT_MODE_VOICE_LEVEL_1);
						allowedToSpeakUpdate = false;
					}
					break;
				case CH_DETAILS_FIVE_TONE:
					// Right/increment cycles the ANI transmit mode forward (Off->St->En->Both).
					// Digit entry / cursor is handled earlier in the keypad-entry block.
					if (tmpChannel.chMode == RADIO_MODE_ANALOG)
					{
						uint8_t d[5];
						aniTxMode_t m = codeplugChannelGetFiveTone(&tmpChannel, d);
						m = (aniTxMode_t)((m + 1) % ANI_MODE_MAX);
						codeplugChannelSetFiveTone(&tmpChannel, d, m);
						fiveTonePersist();
					}
					break;
				case CH_DETAILS_FIVE_TONE_REPLY:
					if (tmpChannel.chMode == RADIO_MODE_ANALOG)
					{
						codeplugChannelSetFiveToneReply(&tmpChannel, true);
						codeplugChannelSetFiveToneReply(currentChannelData, true); // active immediately
					}
					break;
				case CH_DETAILS_BANDWIDTH:
					if (tmpChannel.chMode == RADIO_MODE_ANALOG)
					{
						codeplugChannelSetFlag(&tmpChannel, CHANNEL_FLAG_BW_25K, 1);// set 25kHz bit
					}
					break;
				case CH_DETAILS_FREQ_STEP:
					tmpVal = (tmpChannel.VFOflag5 >> 4) + 1;
					if (tmpVal > 7)
					{
						tmpVal = 7;
					}
					tmpChannel.VFOflag5 &= 0x0F;
					tmpChannel.VFOflag5 |= tmpVal << 4;
					break;
				case CH_DETAILS_TOT:
					if (tmpChannel.tot < 255)
					{
						tmpChannel.tot++;
					}
					break;
				case CH_DETAILS_RXONLY:
					codeplugChannelSetFlag(&tmpChannel, CHANNEL_FLAG_RX_ONLY, 1);// set Channel RX-Only Bit
					break;
				case CH_DETAILS_ZONE_SKIP:
					codeplugChannelSetFlag(&tmpChannel, CHANNEL_FLAG_ZONE_SKIP, 1);// set Channel Zone Skip bit (was Auto Scan)
					break;
				case CH_DETAILS_ALL_SKIP:
					codeplugChannelSetFlag(&tmpChannel, CHANNEL_FLAG_ALL_SKIP, 1);// set Channel All Skip bit (was Lone Worker)
					break;
				case CH_DETAILS_RXGROUP:
					if (tmpChannel.chMode == RADIO_MODE_DIGITAL)
					{
						tmpVal = SAFE_MIN((tmpChannel.rxGroupList + 1), CODEPLUG_RX_GROUPLIST_MAX);

						while (tmpVal <= CODEPLUG_RX_GROUPLIST_MAX) // 1 .. CODEPLUG_RX_GROUPLIST_MAX, codeplugRxGroupGetDataForIndex() is using (index - 1)
						{
							if (codeplugRxGroupGetDataForIndex(tmpVal, &rxGroupBuf))
							{
								tmpChannel.rxGroupList = tmpVal;
								break;
							}
							tmpVal++;
						}

						// RxGroup OR Contact could be selected at once
						if (tmpChannel.rxGroupList > 0)
						{
							tmpChannel.contact = 0;
						}
					}
					break;
				case CH_DETAILS_CONTACT:
					if (tmpChannel.chMode == RADIO_MODE_DIGITAL)
					{
						tmpVal = SAFE_MIN((tmpChannel.contact + 1), CODEPLUG_CONTACTS_MAX);

						while (tmpVal <= CODEPLUG_CONTACTS_MAX) // 1 .. CODEPLUG_CONTACTS_MAX, codeplugContactGetDataForIndex() is using (index - 1)
						{
							if (codeplugContactGetDataForIndex(tmpVal, &contactBuf) && (contactBuf.name[0] != 0xFF))
							{
								tmpChannel.contact = tmpVal;
								break;
							}
							tmpVal++;
						}

						// RxGroup OR Contact could be selected at once
						if (tmpChannel.contact > 0)
						{
							tmpChannel.rxGroupList = 0;
						}
					}
					break;
				case CH_DETAILS_VOX:
					codeplugChannelSetFlag(&tmpChannel, CHANNEL_FLAG_VOX, 1);
					break;
				case CH_DETAILS_POWER:
					if ((uiDataGlobal.currentSelectedChannelNumber != CH_DETAILS_VFO_CHANNEL) &&
						(tmpChannel.libreDMR_Power < (MAX_POWER_SETTING_NUM + CODEPLUG_MIN_PER_CHANNEL_POWER)))
					{

							tmpChannel.libreDMR_Power++;
					}
					break;
				case CH_DETAILS_SQUELCH:
					if (tmpChannel.chMode == RADIO_MODE_ANALOG)
					{
						if (tmpChannel.sql < CODEPLUG_MAX_VARIABLE_SQUELCH)
						{
							tmpChannel.sql++;
						}
					}
					break;
				case CH_DETAILS_NO_BEEP:
					codeplugChannelSetFlag(&tmpChannel, CHANNEL_FLAG_NO_BEEP, 0);
					break;
				case CH_DETAILS_NO_ECO:
					codeplugChannelSetFlag(&tmpChannel, CHANNEL_FLAG_NO_ECO, 0);
					break;
				case CH_DETAILS_TA_TX_TS1:
				case CH_DETAILS_TA_TX_TS2:
					if (tmpChannel.chMode == RADIO_MODE_DIGITAL)
					{
						bool isTS1 = (menuDataGlobal.currentItemIndex == CH_DETAILS_TA_TX_TS1);
						taTxEnum_t v = codeplugGetTATxForTS(&tmpChannel, (isTS1 ? 0 : 1));

						if (v < TA_TX_BOTH)
						{
							v++;
							codeplugSetTATxForTS(&tmpChannel, (isTS1 ? 0 : 1), v);
						}
					}
					break;
				case CH_DETAILS_APRS_CONFIG:
					if (tmpChannel.chMode == RADIO_MODE_ANALOG)
					{
						if (tmpChannel.aprsConfigIndex < codeplugAPRSConfigGetCount())
						{
							tmpChannel.aprsConfigIndex++;
						}
					}
					break;
				case CH_DETAILS_DMR_FORCE_DMO:
					if (tmpChannel.chMode == RADIO_MODE_DIGITAL)
					{
						codeplugChannelSetFlag(&tmpChannel, CHANNEL_FLAG_FORCE_DMO, 1);// Set Channel DMR Force DMO Bit
					}
					break;
			}

			if (ev->events & FUNCTION_EVENT)
			{
				saveChanges(ev);
			}

			updateScreen(false, allowedToSpeakUpdate);
		}
		else if (KEYCHECK_LONGDOWN(ev->keys, KEY_LEFT) || KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_LEFT))
		{
			if (menuDataGlobal.currentItemIndex == CH_DETAILS_NAME)
			{
				namePos = 0;
				updateScreen(false, !voicePromptsIsPlaying());
			}
			else if ((menuDataGlobal.currentItemIndex == CH_DETAILS_RXCSS) || (menuDataGlobal.currentItemIndex == CH_DETAILS_TXCSS))
			{
				goto handlesLeftKey;
			}
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_LEFT)
#if defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
				|| KEYCHECK_SHORTUP(ev->keys, KEY_ROTARY_DECREMENT)
#endif
				|| (QUICKKEY_FUNCTIONID(ev->function) == FUNC_LEFT))
		{
			handlesLeftKey:
			if (menuDataGlobal.menuOptionsTimeout > 0)
			{
				menuDataGlobal.menuOptionsTimeout = 1000;
			}

			switch(menuDataGlobal.currentItemIndex)
			{
				case CH_DETAILS_NAME:
					if (uiDataGlobal.currentSelectedChannelNumber != CH_DETAILS_VFO_CHANNEL)
					{
						moveCursorLeftInString(channelName, &namePos, BUTTONCHECK_DOWN(ev, BUTTON_SK2));
						updateCursor(true);
						allowedToSpeakUpdate = (strlen(channelName) == 0);
					}
					break;
				case CH_DETAILS_RXFREQ:
				case CH_DETAILS_TXFREQ:
					applyShiftOffset(false);
					break;
				case CH_DETAILS_MODE:
					if (tmpChannel.chMode == RADIO_MODE_ANALOG)
					{
						tmpChannel.chMode = RADIO_MODE_DIGITAL;
						codeplugChannelSetFlag(&tmpChannel, CHANNEL_FLAG_BW_25K, 0);// clear 25kHz bit
					}
					break;
				case CH_DETAILS_DMR_CC:
					if (tmpChannel.chMode == RADIO_MODE_DIGITAL)
					{
						if (tmpChannel.txColor > 0)
						{
							tmpChannel.txColor--;
							trxSetDMRColourCode(tmpChannel.txColor);
						}
					}
					break;
				case CH_DETAILS_DMR_TS:
					if (tmpChannel.chMode == RADIO_MODE_DIGITAL)
					{
						codeplugChannelSetFlag(&tmpChannel, CHANNEL_FLAG_TIMESLOT_TWO, 0);
					}
					break;
				case CH_DETAILS_RXCSS:
					if (tmpChannel.chMode == RADIO_MODE_ANALOG)
					{
						bool voicePromptWasPlaying = voicePromptsIsPlaying();

						cssDecrementFromEvent(ev, &tmpChannel.rxTone, &RxCSSIndex, &RxCSSType);
						trxSetRxCSS(RADIO_DEVICE_PRIMARY, tmpChannel.rxTone);
						announceCSSCode(tmpChannel.rxTone, RxCSSType,
								(((voicePromptWasPlaying == false) && (nonVolatileSettings.audioPromptMode > AUDIO_PROMPT_MODE_VOICE_LEVEL_2)) ? DIRECTION_RECEIVE : DIRECTION_NONE),
								(voicePromptWasPlaying == false), AUDIO_PROMPT_MODE_VOICE_LEVEL_1);
						allowedToSpeakUpdate = false;
					}
					break;
				case CH_DETAILS_TXCSS:
					if (tmpChannel.chMode == RADIO_MODE_ANALOG)
					{
						bool voicePromptWasPlaying = voicePromptsIsPlaying();

						cssDecrementFromEvent(ev, &tmpChannel.txTone, &TxCSSIndex, &TxCSSType);
						announceCSSCode(tmpChannel.txTone, TxCSSType,
								(((voicePromptWasPlaying == false) && (nonVolatileSettings.audioPromptMode > AUDIO_PROMPT_MODE_VOICE_LEVEL_2)) ? DIRECTION_TRANSMIT : DIRECTION_NONE),
								(voicePromptWasPlaying == false), AUDIO_PROMPT_MODE_VOICE_LEVEL_1);
						allowedToSpeakUpdate = false;
					}
					break;
				case CH_DETAILS_FIVE_TONE:
					if (tmpChannel.chMode == RADIO_MODE_ANALOG)
					{
						// Left/decrement cycles the ANI transmit mode backward.
						uint8_t d[5];
						aniTxMode_t m = codeplugChannelGetFiveTone(&tmpChannel, d);
						m = (aniTxMode_t)((m + ANI_MODE_MAX - 1) % ANI_MODE_MAX);
						codeplugChannelSetFiveTone(&tmpChannel, d, m);
						fiveTonePersist();
					}
					break;
				case CH_DETAILS_FIVE_TONE_REPLY:
					if (tmpChannel.chMode == RADIO_MODE_ANALOG)
					{
						codeplugChannelSetFiveToneReply(&tmpChannel, false);
						codeplugChannelSetFiveToneReply(currentChannelData, false); // active immediately
					}
					break;
				case CH_DETAILS_BANDWIDTH:
					if (tmpChannel.chMode == RADIO_MODE_ANALOG)
					{
						codeplugChannelSetFlag(&tmpChannel, CHANNEL_FLAG_BW_25K, 0);// clear 25kHz bit
					}
					break;
				case CH_DETAILS_FREQ_STEP:
					tmpVal = (tmpChannel.VFOflag5 >> 4) - 1;
					if (tmpVal < 0)
					{
						tmpVal = 0;
					}
					tmpChannel.VFOflag5 &= 0x0F;
					tmpChannel.VFOflag5 |= tmpVal << 4;
					break;
				case CH_DETAILS_TOT:
					if (tmpChannel.tot > 0)
					{
						tmpChannel.tot--;
					}
					break;
				case CH_DETAILS_RXONLY:
					codeplugChannelSetFlag(&tmpChannel, CHANNEL_FLAG_RX_ONLY, 0);// clear Channel RX-Only Bit
					break;
				case CH_DETAILS_ZONE_SKIP:
					codeplugChannelSetFlag(&tmpChannel, CHANNEL_FLAG_ZONE_SKIP, 0);// clear Channel Zone Skip Bit (was Auto Scan bit)
					break;
				case CH_DETAILS_ALL_SKIP:
					codeplugChannelSetFlag(&tmpChannel, CHANNEL_FLAG_ALL_SKIP, 0);// clear Channel All Skip Bit (was Lone Worker bit)
					break;
				case CH_DETAILS_RXGROUP:
					if (tmpChannel.chMode == RADIO_MODE_DIGITAL)
					{
						tmpVal = tmpChannel.rxGroupList;

						tmpVal = SAFE_MAX((tmpVal - 1), 0);

						if (tmpVal == 0)
						{
							tmpChannel.rxGroupList = tmpVal;
						}
						else
						{
							while (tmpVal > 0)
							{
								if (codeplugRxGroupGetDataForIndex(tmpVal, &rxGroupBuf))
								{
									tmpChannel.rxGroupList = tmpVal;
									break;
								}
								tmpVal--;
							}
						}

						// RxGroup OR Contact could be selected at once
						if (tmpChannel.rxGroupList > 0)
						{
							tmpChannel.contact = 0;
						}
					}
					break;
				case CH_DETAILS_CONTACT:
					if (tmpChannel.chMode == RADIO_MODE_DIGITAL)
					{
						tmpVal = tmpChannel.contact;

						tmpVal = SAFE_MAX((tmpVal - 1), 0);

						if (tmpVal == 0)
						{
							tmpChannel.contact = tmpVal;
						}
						else
						{
							while (tmpVal > 0)
							{
								if (codeplugContactGetDataForIndex(tmpVal, &contactBuf) && (contactBuf.name[0] != 0xFF))
								{
									tmpChannel.contact = tmpVal;
									break;
								}
								tmpVal--;
							}
						}

						// RxGroup OR Contact could be selected at once
						if (tmpChannel.contact > 0)
						{
							tmpChannel.rxGroupList = 0;
						}
					}
					break;
				case CH_DETAILS_VOX:
					codeplugChannelSetFlag(&tmpChannel, CHANNEL_FLAG_VOX, 0);
					break;
				case CH_DETAILS_POWER:
					if ((uiDataGlobal.currentSelectedChannelNumber != CH_DETAILS_VFO_CHANNEL) &&
						(tmpChannel.libreDMR_Power > 0))
					{
						tmpChannel.libreDMR_Power--;
					}
					break;
				case CH_DETAILS_SQUELCH:
					if (tmpChannel.chMode == RADIO_MODE_ANALOG)
					{
						if (tmpChannel.sql >= CODEPLUG_MIN_VARIABLE_SQUELCH) // Here, we permit to reach 0 value, aka global squelch setting.
						{
							tmpChannel.sql--;
						}
					}
					break;
				case CH_DETAILS_NO_BEEP:
					codeplugChannelSetFlag(&tmpChannel, CHANNEL_FLAG_NO_BEEP, 1);
					break;
				case CH_DETAILS_NO_ECO:
					codeplugChannelSetFlag(&tmpChannel, CHANNEL_FLAG_NO_ECO, 1);
					break;
				case CH_DETAILS_TA_TX_TS1:
				case CH_DETAILS_TA_TX_TS2:
					if (tmpChannel.chMode == RADIO_MODE_DIGITAL)
					{
						bool isTS1 = (menuDataGlobal.currentItemIndex == CH_DETAILS_TA_TX_TS1);
						taTxEnum_t v = codeplugGetTATxForTS(&tmpChannel, (isTS1 ? 0 : 1));

						if (v > TA_TX_OFF)
						{
							v--;
							codeplugSetTATxForTS(&tmpChannel, (isTS1 ? 0 : 1), v);
						}
					}
					break;
				case CH_DETAILS_APRS_CONFIG:
					if (tmpChannel.chMode == RADIO_MODE_ANALOG)
					{
						if (tmpChannel.aprsConfigIndex > 0)
						{
							tmpChannel.aprsConfigIndex--;
						}
					}
					break;
				case CH_DETAILS_DMR_FORCE_DMO:
					if (tmpChannel.chMode == RADIO_MODE_DIGITAL)
					{
						codeplugChannelSetFlag(&tmpChannel, CHANNEL_FLAG_FORCE_DMO, 0);// Clear Channel DMR Force DMO Bit
					}
					break;
			}

			if (ev->events & FUNCTION_EVENT)
			{
				saveChanges(ev);
			}

			updateScreen(false, allowedToSpeakUpdate);
		}
		else if (!BUTTONCHECK_DOWN(ev, BUTTON_SK2) && (menuDataGlobal.currentItemIndex == CH_DETAILS_NAME) && (uiDataGlobal.currentSelectedChannelNumber != CH_DETAILS_VFO_CHANNEL))
		{
			// vk3kyy - Commented this out, as BUTTON_SK2 is now changed in the enclosing if, as this was needed so that QuicKeys worked (see also next comment below)
			//if (!BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				if ((ev->keys.event == KEY_MOD_PREVIEW) && (namePos < 16))
				{
					channelName[namePos] = ev->keys.key;
					updateCursor(true);
					announceChar(ev->keys.key);
					updateScreen(false, false);
				}
				else if ((ev->keys.event == KEY_MOD_PRESS) && (namePos < 16))
				{
					channelName[namePos] = ev->keys.key;
					if ((namePos < strlen(channelName)) && (namePos < 15))
					{
						namePos++;
					}
					updateCursor(true);
					announceChar(ev->keys.key);
					updateScreen(false, false);
				}
			}
			/*
			 * vk3kyy - needed to stop this code because it interferes with the QuickKeys and saveing of the channel SK2 + Green
			 * And I don't know what it was intended to do.
			else
			{
				keyboardReset();
				keypadAlphaEnable = true;
				soundSetMelody(MELODY_ERROR_BEEP);
			}*/
		}
		else if ((ev->keys.event & KEY_MOD_PRESS) && (menuDataGlobal.menuOptionsTimeout > 0))
		{
			menuDataGlobal.menuOptionsTimeout = 0;
			resetChannelData();
			menuSystemPopPreviousMenu();
			return;
		}
	}

	if (uiQuickKeysIsStoring(ev))
	{
		uiQuickKeysStore(ev, &menuChannelDetailsExitCode);

		updateScreen(false, true);
	}
}

static void cssIncrementFromEvent(uiEvent_t *ev, uint16_t *tone, uint8_t *index, CodeplugCSSTypes_t *type)
{
	if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
	{
		switch (*type)
		{
			case CSS_TYPE_CTCSS:
				if (*index < (TRX_NUM_CTCSS - 1))
				{
					*index = (TRX_NUM_CTCSS - 1);
					*tone = TRX_CTCSSTones[*index];
				}
				else
				{
					*type = CSS_TYPE_DCS;
					*index = 0;
					*tone = TRX_DCSCodes[*index] | CSS_TYPE_DCS;
				}
				break;
			case CSS_TYPE_DCS:
				if (*index < (TRX_NUM_DCS - 1))
				{
					*index = (TRX_NUM_DCS - 1);
					*tone = TRX_DCSCodes[*index] | CSS_TYPE_DCS;
				}
				else
				{
					*type = (CSS_TYPE_DCS | CSS_TYPE_DCS_INVERTED);
					*index = 0;
					*tone = TRX_DCSCodes[*index] | CSS_TYPE_DCS | CSS_TYPE_DCS_INVERTED;
				}
				break;
			case (CSS_TYPE_DCS | CSS_TYPE_DCS_INVERTED):
				if (*index < (TRX_NUM_DCS - 1))
				{
					*index = (TRX_NUM_DCS - 1);
					*tone = TRX_DCSCodes[*index] | CSS_TYPE_DCS | CSS_TYPE_DCS_INVERTED;
				}
				break;
			case CSS_TYPE_NONE:
				*type = CSS_TYPE_CTCSS;
				*index = 0;
				*tone = TRX_CTCSSTones[*index];
				break;
			default:
				break;
		}
	}
	else
	{
		// Step +5, cssIncrement() handles index overflow
		cssIncrement(tone, index, ((ev->keys.event & KEY_MOD_LONG) ? 5 : 1), type, false, false);
	}
}

static void cssDecrementFromEvent(uiEvent_t *ev, uint16_t *tone, uint8_t *index, CodeplugCSSTypes_t *type)
{
	if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
	{
		switch (*type)
		{
			case CSS_TYPE_CTCSS:
				if (*index > 0)
				{
					*index = 0;
					*tone = TRX_CTCSSTones[*index];
				}
				else
				{
					*type = CSS_TYPE_NONE;
					*index = 0;
					*tone = CODEPLUG_CSS_TONE_NONE;
				}
				break;
			case CSS_TYPE_DCS:
				if (*index > 0)
				{
					*index = 0;
					*tone = TRX_DCSCodes[*index] | CSS_TYPE_DCS;
				}
				else
				{
					*type = CSS_TYPE_CTCSS;
					*index = (TRX_NUM_CTCSS - 1);
					*tone = TRX_CTCSSTones[*index];
				}
				break;
			case (CSS_TYPE_DCS | CSS_TYPE_DCS_INVERTED):
				if (*index > 0)
				{
					*index = 0;
					*tone = TRX_DCSCodes[*index] | CSS_TYPE_DCS | CSS_TYPE_DCS_INVERTED;
				}
				else
				{
					*type = CSS_TYPE_DCS;
					*index = (TRX_NUM_DCS - 1);
					*tone = TRX_DCSCodes[*index] | CSS_TYPE_DCS;
				}
				break;
			default:
				break;
		}
	}
	else
	{
		// Step -5 on long press, cssDecrement() handles index < 0
		cssDecrement(tone, index, ((ev->keys.event & KEY_MOD_LONG) ? 5 : 1), type, false, false);
	}
}

static void saveChanges(uiEvent_t *ev)
{
	if (uiDataGlobal.currentSelectedChannelNumber != CH_DETAILS_VFO_CHANNEL)
	{
		codeplugUtilConvertStringToBuf(channelName, (char *)&tmpChannel.name, 16);
	}
	memcpy(currentChannelData, &tmpChannel, CODEPLUG_CHANNEL_DATA_STRUCT_SIZE); // Leave channel's NOT_IN_CODEPLUG_flag out of the copy

	// uiDataGlobal.currentSelectedChannelNumber is -1 when in VFO mode
	// But the VFO is stored in the nonVolatile settings, and not saved back to the codeplug
	// Also don't store this back to the codeplug unless the Function key (Blue / SK2 ) is pressed at the same time.
	if (!(ev->events & FUNCTION_EVENT) && ((uiDataGlobal.currentSelectedChannelNumber != CH_DETAILS_VFO_CHANNEL) && BUTTONCHECK_DOWN(ev, BUTTON_SK2)))
	{
		codeplugChannelSaveDataForIndex(uiDataGlobal.currentSelectedChannelNumber, currentChannelData);
	}

	if ((uiDataGlobal.currentSelectedChannelNumber == CH_DETAILS_VFO_CHANNEL) || (currentChannelData->libreDMR_Power == 0))
	{
		trxSetPowerFromLevel(nonVolatileSettings.txPowerLevel);
	}
	else
	{
		trxSetPowerFromLevel(currentChannelData->libreDMR_Power - 1);
	}

	settingsSetVFODirty();
	settingsSaveIfNeeded(true);
	aprsBeaconingInvalidateFixedPosition(); // Because of possible APRS config changes.
	aprsBeaconingResetTimers(); // Maybe edited the APRS config
}

static void resetChannelData(void)
{
	memcpy(tmpChannel.name, CHANNEL_UNSET, 5);
}

static void applyShiftOffset(bool increase)
{
	// Currently entering frequency;
	if (uiDataGlobal.FreqEnter.index != 0)
	{
		return;
	}

	if ((increase && (shiftOffsetIndex < shiftOffsetMax)) ||
			((increase == false) && (shiftOffsetIndex > -shiftOffsetMax)))
	{
		int8_t prevIndex = shiftOffsetIndex;

		shiftOffsetIndex += (increase ? 1 : -1);

		int8_t shitOffsetValue = (shiftOffsets[abs(shiftOffsetIndex)] * ((shiftOffsetIndex < 0) ? -1 : 1));
		uint32_t txFreq = (tmpChannel.rxFreq + (shitOffsetValue * 10000));

		// Check Frequency validity
		if (trxGetBandFromFrequency(txFreq) != FREQUENCY_OUT_OF_BAND)
		{
			char buf[SCREEN_LINE_BUFFER_SIZE];

			tmpChannel.txFreq = txFreq;

			snprintf(buf, SCREEN_LINE_BUFFER_SIZE, "%s%d.%d MHz", ((shiftOffsetIndex < 0) ? "-" : ""),
					(shiftOffsets[abs(shiftOffsetIndex)] / 10),	(shiftOffsets[abs(shiftOffsetIndex)] - ((shiftOffsets[abs(shiftOffsetIndex)] / 10) * 10)));

			uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 1000, buf, false);
		}
		else
		{
			shiftOffsetIndex = prevIndex;
			uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 1000, currentLanguage->out_of_band, false);
		}
	}
}

static void exitCallback(void *data)
{
	freqEnterReset();
	resetChannelData();
}

