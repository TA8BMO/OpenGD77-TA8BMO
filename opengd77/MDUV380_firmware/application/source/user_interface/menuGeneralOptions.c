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
#include "user_interface/menuSystem.h"
#include "user_interface/uiLocalisation.h"
#include "user_interface/uiUtilities.h"
#include "interfaces/wdog.h"
#include "utils.h"
#include "functions/rxPowerSaving.h"
#if defined(PLATFORM_MD9600) || defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
#include "interfaces/batteryAndPowerManagement.h"
#endif
#if defined(HAS_GPS)
#include "interfaces/gps.h"
#endif

static void updateScreen(bool isFirstRun);
static void handleEvent(uiEvent_t *ev);
static void applySettings(void);
static void exitCallback(void *data);

static menuStatus_t menuOptionsExitCode = MENU_STATUS_SUCCESS;

enum
{
	GENERAL_OPTIONS_MENU_KEYPAD_TIMER_LONG = 0U,
	GENERAL_OPTIONS_MENU_KEYPAD_TIMER_REPEAT,
#if !defined(PLATFORM_GD77S)
	GENERAL_OPTIONS_MENU_KEYPAD_AUTOLOCK,
#endif
#if defined(PLATFORM_MD2017)
	GENERAL_OPTIONS_TRACKBALL_ENABLED,
#endif
	GENERAL_OPTIONS_MENU_HOTSPOT_TYPE,
	GENERAL_OPTIONS_MENU_TEMPERATURE_CALIBRATON,
	GENERAL_OPTIONS_MENU_BATTERY_CALIBRATON,
#if !defined(PLATFORM_MD9600) && !defined(PLATFORM_MD380)
	GENERAL_OPTIONS_MENU_ECO_LEVEL,
#endif
#if ! (defined(PLATFORM_RD5R) || defined(PLATFORM_GD77S) || defined(PLATFORM_MD9600) || defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017))
	GENERAL_OPTIONS_MENU_POWEROFF_SUSPEND,
#endif
#if ! (defined(PLATFORM_MD9600) || defined(PLATFORM_GD77S))
	GENERAL_OPTIONS_SAFE_POWER_ON,
#endif
#if !defined(PLATFORM_GD77S)
	GENERAL_OPTIONS_APO,
	GENERAL_OPTIONS_APO_WITH_RF,
#endif
	GENERAL_OPTIONS_MENU_SATELLITE_MANUAL_AUTO,
#if defined(HAS_GPS)
	GENERAL_OPTIONS_GPS,
#endif
	NUM_GENERAL_OPTIONS_MENU_ITEMS
};

#if defined(HAS_GPS)
// Used by menuGPS to enable/disable GPS using QuickKey
const uint8_t MENU_GENERAL_OPTIONS_GPS_ENTRY_NUMBER = GENERAL_OPTIONS_GPS;
#endif

menuStatus_t menuGeneralOptions(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		menuDataGlobal.menuOptionsSetQuickkey = 0;
		menuDataGlobal.menuOptionsTimeout = 0;
		menuDataGlobal.newOptionSelected = true;
		menuDataGlobal.numItems = NUM_GENERAL_OPTIONS_MENU_ITEMS;

		if (originalNonVolatileSettings.magicNumber == 0xDEADBEEF)
		{
			// Store original settings, used on cancel event.
			memcpy(&originalNonVolatileSettings, &nonVolatileSettings, sizeof(settingsStruct_t));
		}

		voicePromptsInit();
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		voicePromptsAppendLanguageString(currentLanguage->general_options);
		voicePromptsAppendLanguageString(currentLanguage->menu);
		voicePromptsAppendPrompt(PROMPT_SILENCE);

		menuSystemRegisterExitCallback(exitCallback, NULL);

		updateScreen(true);
		return (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);
	}
	else
	{
		menuOptionsExitCode = MENU_STATUS_SUCCESS;

		if (ev->hasEvent || (menuDataGlobal.menuOptionsTimeout > 0))
		{
			handleEvent(ev);
		}
	}
	return menuOptionsExitCode;
}

static void updateScreen(bool isFirstRun)
{
	int mNum = 0;
	char buf[SCREEN_LINE_BUFFER_SIZE];
	char buf2[SCREEN_LINE_BUFFER_SIZE];
	const char *leftSide = NULL;// initialize to please the compiler
	const char *rightSideConst = NULL;// initialize to please the compiler
	char rightSideVar[SCREEN_LINE_BUFFER_SIZE];
	voicePrompt_t rightSideUnitsPrompt;
	const char *rightSideUnitsStr;

	displayClearBuf();
	bool settingOption = uiQuickKeysShowChoices(buf, SCREEN_LINE_BUFFER_SIZE, currentLanguage->general_options);

	for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
	{
		if ((settingOption == false) || (i == 0))
		{
			mNum = menuGetMenuOffset(NUM_GENERAL_OPTIONS_MENU_ITEMS, i);
			if (mNum == MENU_OFFSET_BEFORE_FIRST_ENTRY)
			{
				continue;
			}
			else if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
			{
				break;
			}

			buf[0] = 0;
			buf[2] = 0;
			leftSide = NULL;
			rightSideConst = NULL;
			rightSideVar[0] = 0;
			rightSideUnitsPrompt = PROMPT_SILENCE;// use PROMPT_SILENCE as flag that the unit has not been set
			rightSideUnitsStr = NULL;

			switch(mNum)
			{
				case GENERAL_OPTIONS_MENU_KEYPAD_TIMER_LONG:// Timer longpress
					leftSide = currentLanguage->key_long;
					snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%1d.%1d", nonVolatileSettings.keypadTimerLong / 10, nonVolatileSettings.keypadTimerLong % 10);
					rightSideUnitsPrompt = PROMPT_SECONDS;
					rightSideUnitsStr = "s";
					break;
				case GENERAL_OPTIONS_MENU_KEYPAD_TIMER_REPEAT:// Timer repeat
					leftSide = currentLanguage->key_repeat;
					snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%1d.%1d", nonVolatileSettings.keypadTimerRepeat / 10, nonVolatileSettings.keypadTimerRepeat % 10);
					rightSideUnitsPrompt = PROMPT_SECONDS;
					rightSideUnitsStr = "s";
					break;
#if !defined(PLATFORM_GD77S)
				case GENERAL_OPTIONS_MENU_KEYPAD_AUTOLOCK:
					leftSide = currentLanguage->auto_lock;
					if (nonVolatileSettings.autolockTimer > 0)
					{
						double seconds = (nonVolatileSettings.autolockTimer * 0.5); // 30 seconds steps / 60
						uint8_t decMins =  (uint8_t)((seconds - (uint8_t)seconds) * 1E1);

						if (decMins)
						{
							snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%u.%1u", (uint8_t)seconds, decMins);
						}
						else
						{
							snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%u", (uint8_t)seconds);
						}
						rightSideUnitsPrompt = PROMPT_MINUTES;
						rightSideUnitsStr = "min";
					}
					else
					{
						rightSideConst = currentLanguage->off;
					}
					break;
#endif
#if defined(PLATFORM_MD2017)
				case GENERAL_OPTIONS_TRACKBALL_ENABLED:
					leftSide = currentLanguage->trackball;
					rightSideConst = (settingsIsOptionBitSet(BIT_TRACKBALL_ENABLED) ?
							(settingsIsOptionBitSet(BIT_TRACKBALL_FAST_MOTION) ? currentLanguage->high : currentLanguage->low) : currentLanguage->off);
					break;
#endif
				case GENERAL_OPTIONS_MENU_HOTSPOT_TYPE:
					leftSide = currentLanguage->hotspot_mode;
#if defined(PLATFORM_RD5R)
					rightSideConst = currentLanguage->n_a;
#else
					// DMR (digital) is disabled.
					if (uiDataGlobal.dmrDisabled)
					{
						rightSideConst = currentLanguage->n_a;
					}
					else
					{
						const char *hsTypes[] = { "MMDVM", "BlueDV" };
						if (nonVolatileSettings.hotspotType == 0)
						{
							rightSideConst = currentLanguage->off;
						}
						else
						{
							snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%s", hsTypes[nonVolatileSettings.hotspotType - 1]);
						}
					}
#endif
					break;
				case GENERAL_OPTIONS_MENU_TEMPERATURE_CALIBRATON:
					{
						int absValue = abs(nonVolatileSettings.temperatureCalibration);
						leftSide = currentLanguage->temperature_calibration;
						snprintf(buf2, SCREEN_LINE_BUFFER_SIZE, "%c%d.%d", (nonVolatileSettings.temperatureCalibration == 0 ? ' ' :
								(nonVolatileSettings.temperatureCalibration > 0 ? '+' : '-')), ((absValue) / 2), ((absValue % 2) * 5));
						snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%s%s", buf2, currentLanguage->celcius);
					}
					break;
				case GENERAL_OPTIONS_MENU_BATTERY_CALIBRATON:
					{
						int batCal = (nonVolatileSettings.batteryCalibration & 0x0F) - 5;
						leftSide = currentLanguage->battery_calibration;
						snprintf(buf2, SCREEN_LINE_BUFFER_SIZE, "%c0.%d", (batCal == 0 ? ' ' : (batCal > 0 ? '+' : '-')), abs(batCal));
						snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%sV", buf2);
					}
					break;
#if !defined(PLATFORM_MD9600) && !defined(PLATFORM_MD380)
				case GENERAL_OPTIONS_MENU_ECO_LEVEL:
					leftSide = currentLanguage->eco_level;
					snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%d", (nonVolatileSettings.ecoLevel));
					break;
#endif
#if ! (defined(PLATFORM_RD5R) || defined(PLATFORM_GD77S) || defined(PLATFORM_MD9600) || defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017))
				case GENERAL_OPTIONS_MENU_POWEROFF_SUSPEND:
					leftSide = currentLanguage->suspend;
					rightSideConst = (settingsIsOptionBitSet(BIT_POWEROFF_SUSPEND) ? currentLanguage->on : currentLanguage->off);
					break;
#endif
#if ! (defined(PLATFORM_MD9600) || defined(PLATFORM_GD77S))
				case GENERAL_OPTIONS_SAFE_POWER_ON:
					leftSide = currentLanguage->safe_power_on;
					rightSideConst = (settingsIsOptionBitSet(BIT_SAFE_POWER_ON) ? currentLanguage->on : currentLanguage->off);
					break;
#endif
#if !defined(PLATFORM_GD77S)
				case GENERAL_OPTIONS_APO:
					leftSide = currentLanguage->auto_power_off;

					if (nonVolatileSettings.apo == 0)
					{
						rightSideConst = currentLanguage->no;
					}
					else
					{
						snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%d", (nonVolatileSettings.apo * 30));
						rightSideUnitsPrompt = PROMPT_MINUTES;
					}
					break;
				case GENERAL_OPTIONS_APO_WITH_RF:
					leftSide = currentLanguage->apo_with_rf;
					if (nonVolatileSettings.apo == 0)
					{
						rightSideConst = currentLanguage->n_a;
					}
					else
					{
						rightSideConst = (settingsIsOptionBitSet(BIT_APO_WITH_RF) ? currentLanguage->yes : currentLanguage->no);
					}
					break;
#endif
				case GENERAL_OPTIONS_MENU_SATELLITE_MANUAL_AUTO:
					leftSide = currentLanguage->satellite_short;
					rightSideConst = (settingsIsOptionBitSet(BIT_SATELLITE_MANUAL_AUTO) ? currentLanguage->Auto : currentLanguage->manual);
					break;
#if defined(HAS_GPS)
				case GENERAL_OPTIONS_GPS:
					leftSide = currentLanguage->gps;

					switch(nonVolatileSettings.gps)
					{
						case GPS_MODE_ON_NMEA:
							snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%s", "NMEA");
							break;
#if defined(LOG_GPS_DATA)
						case GPS_MODE_ON_LOG:
							snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%s", "Log");
							break;
#endif
						case GPS_MODE_ON:
							rightSideConst = currentLanguage->on;// On all the time
							break;
						case GPS_MODE_OFF:
							rightSideConst = currentLanguage->off;
							break;
						case GPS_NOT_DETECTED:
							rightSideConst = currentLanguage->none;
							break;
					}
					break;
#endif
			}

			snprintf(buf, SCREEN_LINE_BUFFER_SIZE, "%s:%s", leftSide, (rightSideVar[0] ? rightSideVar : (rightSideConst ? rightSideConst : "")));

			if (i == 0)
			{
				bool wasPlaying = voicePromptsIsPlaying();

				if (!isFirstRun && (menuDataGlobal.menuOptionsSetQuickkey == 0))
				{
					voicePromptsInit();
				}

				if (!wasPlaying || (menuDataGlobal.newOptionSelected || (menuDataGlobal.menuOptionsTimeout > 0)))
				{
					voicePromptsAppendLanguageString(leftSide);
				}

				if ((rightSideVar[0] != 0) || ((rightSideVar[0] == 0) && (rightSideConst == NULL)))
				{
					if (mNum == GENERAL_OPTIONS_MENU_TEMPERATURE_CALIBRATON)
					{
						voicePromptsAppendString(buf2);
						voicePromptsAppendLanguageString(currentLanguage->celcius);
					}
					else if (mNum == GENERAL_OPTIONS_MENU_BATTERY_CALIBRATON)
					{
						voicePromptsAppendString(buf2);
						voicePromptsAppendPrompt(PROMPT_VOLTS);
					}
					else
					{
						voicePromptsAppendString(rightSideVar);
					}
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

				menuDisplayEntry(i, mNum, buf, (strlen(leftSide) + 1), THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
			}
		}
	}

	displayRender();
}

static void handleEvent(uiEvent_t *ev)
{
	bool isDirty = false;

	if (ev->events & BUTTON_EVENT)
	{
		if (repeatVoicePromptOnSK1(ev))
		{
			return;
		}
	}

	if ((menuDataGlobal.menuOptionsTimeout > 0) && (!BUTTONCHECK_DOWN(ev, BUTTON_SK2)))
	{
		if (voicePromptsIsPlaying() == false)
		{
			menuDataGlobal.menuOptionsTimeout--;
			if (menuDataGlobal.menuOptionsTimeout == 0)
			{
				applySettings();
				menuSystemPopPreviousMenu();
				return;
			}
		}
	}

	if (ev->events & FUNCTION_EVENT)
	{
		isDirty = true;
		if (ev->function == FUNC_REDRAW)
		{
			updateScreen(false);
			return;
		}
		else if ((QUICKKEY_TYPE(ev->function) == QUICKKEY_MENU) && (QUICKKEY_ENTRYID(ev->function) < NUM_GENERAL_OPTIONS_MENU_ITEMS))
		{
			menuDataGlobal.currentItemIndex = QUICKKEY_ENTRYID(ev->function);
		}

		if ((QUICKKEY_FUNCTIONID(ev->function) != 0))
		{
			menuDataGlobal.menuOptionsTimeout = 1000;
		}
	}

	if ((ev->events & KEY_EVENT) && (menuDataGlobal.menuOptionsSetQuickkey == 0) && (menuDataGlobal.menuOptionsTimeout == 0))
	{
		if (KEYCHECK_PRESS(ev->keys, KEY_DOWN) && (menuDataGlobal.numItems != 0))
		{
			isDirty = true;
			menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, NUM_GENERAL_OPTIONS_MENU_ITEMS);
			menuDataGlobal.newOptionSelected = true;
			menuOptionsExitCode |= MENU_STATUS_LIST_TYPE;
		}
		else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
		{
			isDirty = true;
			menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, NUM_GENERAL_OPTIONS_MENU_ITEMS);
			menuDataGlobal.newOptionSelected = true;
			menuOptionsExitCode |= MENU_STATUS_LIST_TYPE;
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			applySettings();
			menuSystemPopAllAndDisplayRootMenu();
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
				isDirty = true;
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
			isDirty = true;
			menuDataGlobal.newOptionSelected = false;
			switch(menuDataGlobal.currentItemIndex)
			{
				case GENERAL_OPTIONS_MENU_KEYPAD_TIMER_LONG:
					if (nonVolatileSettings.keypadTimerLong < 90)
					{
						settingsIncrement(nonVolatileSettings.keypadTimerLong, 1);
					}
					break;
				case GENERAL_OPTIONS_MENU_KEYPAD_TIMER_REPEAT:
					if (nonVolatileSettings.keypadTimerRepeat < 90)
					{
						settingsIncrement(nonVolatileSettings.keypadTimerRepeat, 1);
					}
					break;
#if !defined(PLATFORM_GD77S)
				case GENERAL_OPTIONS_MENU_KEYPAD_AUTOLOCK:
					if (nonVolatileSettings.autolockTimer < 30)
					{
						settingsIncrement(nonVolatileSettings.autolockTimer, 1);
					}
					break;
#endif
#if defined(PLATFORM_MD2017)
				case GENERAL_OPTIONS_TRACKBALL_ENABLED:
					if (settingsIsOptionBitSet(BIT_TRACKBALL_ENABLED) == false)
					{
						settingsSetOptionBit(BIT_TRACKBALL_ENABLED, true);
					}
					else
					{
						if (settingsIsOptionBitSet(BIT_TRACKBALL_FAST_MOTION) == false)
						{
							settingsSetOptionBit(BIT_TRACKBALL_FAST_MOTION, true);
							trackballSetMotion(true);
						}
					}
					break;
#endif
				case GENERAL_OPTIONS_MENU_HOTSPOT_TYPE:
#if !defined(PLATFORM_RD5R)
					if ((uiDataGlobal.dmrDisabled == false) && (nonVolatileSettings.hotspotType < HOTSPOT_TYPE_BLUEDV))
					{
						settingsIncrement(nonVolatileSettings.hotspotType, 1);
					}
#endif
					break;
				case GENERAL_OPTIONS_MENU_TEMPERATURE_CALIBRATON:
					if (nonVolatileSettings.temperatureCalibration < 20)
					{
						settingsIncrement(nonVolatileSettings.temperatureCalibration, 1);
					}
					break;
				case GENERAL_OPTIONS_MENU_BATTERY_CALIBRATON:
					{
						uint32_t batVal = nonVolatileSettings.batteryCalibration & 0x0F;// lower 4 bits
						if (batVal < 10) // = +0.5V as val is (batteryCalibration -5 ) /10
						{
							settingsSet(nonVolatileSettings.batteryCalibration, (nonVolatileSettings.batteryCalibration & 0xF0) + (batVal + 1));
						}
					}
					break;
#if !defined(PLATFORM_MD9600) && !defined(PLATFORM_MD380)
				case GENERAL_OPTIONS_MENU_ECO_LEVEL:
					if (nonVolatileSettings.ecoLevel < ECO_LEVEL_MAX)
					{
						settingsIncrement(nonVolatileSettings.ecoLevel, 1);
					}
					break;
#endif
#if ! (defined(PLATFORM_RD5R) || defined(PLATFORM_GD77S) || defined(PLATFORM_MD9600) || defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017))
				case GENERAL_OPTIONS_MENU_POWEROFF_SUSPEND:
					if (settingsIsOptionBitSet(BIT_POWEROFF_SUSPEND) == false)
					{
						settingsSetOptionBit(BIT_POWEROFF_SUSPEND, true);
					}
					break;
#endif
#if ! (defined(PLATFORM_MD9600) || defined(PLATFORM_GD77S))
				case GENERAL_OPTIONS_SAFE_POWER_ON:
					if (settingsIsOptionBitSet(BIT_SAFE_POWER_ON) == false)
					{
						settingsSetOptionBit(BIT_SAFE_POWER_ON, true);
					}
					break;
#endif
#if !defined(PLATFORM_GD77S)
				case GENERAL_OPTIONS_APO:
					if (nonVolatileSettings.apo < 6) // 180 minutes max
					{
						settingsIncrement(nonVolatileSettings.apo, ((nonVolatileSettings.apo == 4) ? 2 : 1));
					}
					break;
				case GENERAL_OPTIONS_APO_WITH_RF:
					if ((nonVolatileSettings.apo != 0) && (settingsIsOptionBitSet(BIT_APO_WITH_RF) == false))
					{
						settingsSetOptionBit(BIT_APO_WITH_RF, true);
					}
					break;
#endif
				case GENERAL_OPTIONS_MENU_SATELLITE_MANUAL_AUTO:
					if (settingsIsOptionBitSet(BIT_SATELLITE_MANUAL_AUTO) == false)
					{
						settingsSetOptionBit(BIT_SATELLITE_MANUAL_AUTO, true);
					}
					break;
#if defined(HAS_GPS)
				case GENERAL_OPTIONS_GPS:
					// With the track log switched off (settings.h) its "Log" mode,
					// the last one, is not offered: NMEA is as far as it goes.
					if ((nonVolatileSettings.gps != GPS_NOT_DETECTED) &&
#if defined(LOG_GPS_DATA) && ! GPS_TRACK_LOG_TO_FLASH
							(nonVolatileSettings.gps < GPS_MODE_ON_NMEA)
#else
							(nonVolatileSettings.gps < (NUM_GPS_MODES - 1))
#endif
					)
					{
						if (nonVolatileSettings.gps == GPS_MODE_OFF)
						{
							gpsOn();
						}
						settingsIncrement(nonVolatileSettings.gps, 1);
					}
					break;
#endif
			}
		}
		else if (KEYCHECK_PRESS(ev->keys, KEY_LEFT)
#if defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
				|| KEYCHECK_SHORTUP(ev->keys, KEY_ROTARY_DECREMENT)
#endif
				|| (QUICKKEY_FUNCTIONID(ev->function) == FUNC_LEFT))
		{
			isDirty = true;
			menuDataGlobal.newOptionSelected = false;
			switch(menuDataGlobal.currentItemIndex)
			{
				case GENERAL_OPTIONS_MENU_KEYPAD_TIMER_LONG:
					if (nonVolatileSettings.keypadTimerLong > 1)
					{
						settingsDecrement(nonVolatileSettings.keypadTimerLong, 1);
					}
					break;
				case GENERAL_OPTIONS_MENU_KEYPAD_TIMER_REPEAT:
					if (nonVolatileSettings.keypadTimerRepeat > 1) // Don't set it to zero, otherwise watchdog may kicks in.
					{
						settingsDecrement(nonVolatileSettings.keypadTimerRepeat, 1);
					}
					break;
#if !defined(PLATFORM_GD77S)
				case GENERAL_OPTIONS_MENU_KEYPAD_AUTOLOCK:
					if (nonVolatileSettings.autolockTimer >= 1)
					{
						settingsDecrement(nonVolatileSettings.autolockTimer, 1);
					}
					break;
#endif
#if defined(PLATFORM_MD2017)
				case GENERAL_OPTIONS_TRACKBALL_ENABLED:
					if (settingsIsOptionBitSet(BIT_TRACKBALL_ENABLED))
					{
						if (settingsIsOptionBitSet(BIT_TRACKBALL_FAST_MOTION))
						{
							settingsSetOptionBit(BIT_TRACKBALL_FAST_MOTION, false);
							trackballSetMotion(false);
						}
						else
						{
							settingsSetOptionBit(BIT_TRACKBALL_ENABLED, false);
						}
					}
					break;
#endif
				case GENERAL_OPTIONS_MENU_HOTSPOT_TYPE:
#if !defined(PLATFORM_RD5R)
					if ((uiDataGlobal.dmrDisabled == false) && (nonVolatileSettings.hotspotType > HOTSPOT_TYPE_OFF))
					{
						settingsDecrement(nonVolatileSettings.hotspotType, 1);
					}
#endif
					break;
				case GENERAL_OPTIONS_MENU_TEMPERATURE_CALIBRATON:
					if (nonVolatileSettings.temperatureCalibration > -20)
					{
						settingsDecrement(nonVolatileSettings.temperatureCalibration, 1);
					}
					break;
				case GENERAL_OPTIONS_MENU_BATTERY_CALIBRATON:
					{
						uint32_t batVal = nonVolatileSettings.batteryCalibration & 0x0F;// lower 4 bits
						if (batVal > 0)
						{
							settingsSet(nonVolatileSettings.batteryCalibration, (nonVolatileSettings.batteryCalibration & 0xF0) + (batVal - 1));
						}
					}
					break;
#if !defined(PLATFORM_MD9600) && !defined(PLATFORM_MD380)
				case GENERAL_OPTIONS_MENU_ECO_LEVEL:
					if (nonVolatileSettings.ecoLevel > 0)
					{
						settingsDecrement(nonVolatileSettings.ecoLevel, 1);
					}
					break;
#endif
#if ! (defined(PLATFORM_RD5R) || defined(PLATFORM_GD77S) || defined(PLATFORM_MD9600) || defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017))
				case GENERAL_OPTIONS_MENU_POWEROFF_SUSPEND:
					if (settingsIsOptionBitSet(BIT_POWEROFF_SUSPEND))
					{
						settingsSetOptionBit(BIT_POWEROFF_SUSPEND, false);
					}
					break;
#endif
#if ! (defined(PLATFORM_MD9600) || defined(PLATFORM_GD77S))
				case GENERAL_OPTIONS_SAFE_POWER_ON:
					if (settingsIsOptionBitSet(BIT_SAFE_POWER_ON))
					{
						settingsSetOptionBit(BIT_SAFE_POWER_ON, false);
					}
					break;
#endif
#if !defined(PLATFORM_GD77S)
				case GENERAL_OPTIONS_APO:
					if (nonVolatileSettings.apo > 0)
					{
						settingsDecrement(nonVolatileSettings.apo, ((nonVolatileSettings.apo == 6) ? 2 : 1));
					}
					break;
				case GENERAL_OPTIONS_APO_WITH_RF:
					if ((nonVolatileSettings.apo != 0) && settingsIsOptionBitSet(BIT_APO_WITH_RF))
					{
						settingsSetOptionBit(BIT_APO_WITH_RF, false);
					}
					break;
#endif
				case GENERAL_OPTIONS_MENU_SATELLITE_MANUAL_AUTO:
					if (settingsIsOptionBitSet(BIT_SATELLITE_MANUAL_AUTO))
					{
						settingsSetOptionBit(BIT_SATELLITE_MANUAL_AUTO, false);
					}
					break;
#if defined(HAS_GPS)
				case GENERAL_OPTIONS_GPS:
					if (nonVolatileSettings.gps > GPS_MODE_OFF)
					{
						settingsDecrement(nonVolatileSettings.gps, 1);
						if (nonVolatileSettings.gps == GPS_MODE_OFF)
						{
							gpsOff();
						}
					}
					break;
#endif
			}
		}
		else if ((ev->keys.event & KEY_MOD_PRESS) && (menuDataGlobal.menuOptionsTimeout > 0))
		{
			menuDataGlobal.menuOptionsTimeout = 0;
			resetOriginalSettingsData();
			menuSystemPopPreviousMenu();
			return;
		}
	}

	if (uiQuickKeysIsStoring(ev))
	{
		uiQuickKeysStore(ev, &menuOptionsExitCode);
		isDirty = true;
	}

	if (isDirty)
	{
		updateScreen(false);
	}
}

static void applySettings(void)
{
#if defined(HAS_GPS) && defined(LOG_GPS_DATA)
	// gpsOn()/gpsOff() has already being called.
	if (originalNonVolatileSettings.gps != nonVolatileSettings.gps)
	{
		if (originalNonVolatileSettings.gps == GPS_MODE_ON_LOG)
		{
			gpsLoggingStop();
		}
		else if (nonVolatileSettings.gps == GPS_MODE_ON_LOG)
		{
			gpsLoggingStart();
		}
	}
#endif

	settingsSaveIfNeeded(true);
	resetOriginalSettingsData();
	rxPowerSavingSetLevel(nonVolatileSettings.ecoLevel);
#if !defined(PLATFORM_GD77S)
	ticksTimerStart(&apoTimer, ((nonVolatileSettings.apo * 30) * 60000U));
	ticksTimerStart(&autolockTimer, (nonVolatileSettings.autolockTimer * 30000U));
#endif
}

static void exitCallback(void *data)
{
	if (originalNonVolatileSettings.magicNumber != 0xDEADBEEF)
	{
#if defined(HAS_GPS)
		// Revert GPS mode
		if (originalNonVolatileSettings.gps != nonVolatileSettings.gps)
		{
			if ((originalNonVolatileSettings.gps >= GPS_MODE_ON) && (nonVolatileSettings.gps < GPS_MODE_ON))
			{
				gpsOn();
			}
			else if ((originalNonVolatileSettings.gps < GPS_MODE_ON) && (nonVolatileSettings.gps >= GPS_MODE_ON))
			{
				gpsOff();
			}
		}
#endif

		// Restore original settings.
		memcpy(&nonVolatileSettings, &originalNonVolatileSettings, sizeof(settingsStruct_t));
		settingsSaveIfNeeded(true);
		trxUpdate_PA_DAC_Drive();
#if defined(PLATFORM_MD2017)
		trackballSetMotion(settingsIsOptionBitSet(BIT_TRACKBALL_FAST_MOTION));
#endif

		resetOriginalSettingsData();
	}
}
