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
#if defined(PLATFORM_MD9600)
#include "hardware/ST7567.h"
#elif (defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017))
#include "hardware/HX8353E.h"
#else
#include "hardware/UC1701.h"
#endif
#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiLocalisation.h"
#include "user_interface/uiUtilities.h"


static void updateScreen(bool isFirstRun);
static void handleEvent(uiEvent_t *ev);
static void updateBacklightMode(uint8_t mode);
static void setDisplayInvert(bool invert);
static void checkMinBacklightValue(void);
static void buildTimeZoneBufferText(char * buffer);
static void applySettings(void);
static void exitCallback(void *data);

static menuStatus_t menuDisplayOptionsExitCode = MENU_STATUS_SUCCESS;

static const int BACKLIGHT_MAX_TIMEOUT = 30;
#if defined(PLATFORM_RD5R)
static const int CONTRAST_MAX_VALUE = 10;// Maximum value which still seems to be readable
static const int CONTRAST_MIN_VALUE = 0;// Minimum value which still seems to be readable
#elif ! (defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017))
static const int CONTRAST_MAX_VALUE = 30;// Maximum value which still seems to be readable
static const int CONTRAST_MIN_VALUE = 5;// Minimum value which still seems to be readable
#endif

static const int BACKLIGHT_TIMEOUT_STEP = 5;
static const int BACKLIGHT_MAX_PERCENTAGE = 100;
static const int BACKLIGHT_PERCENTAGE_STEP = 10;
static const int BACKLIGHT_PERCENTAGE_STEP_SMALL = 1;

static const char *contactOrders[] = { "Ct/DB/TA", "DB/Ct/TA", "TA/Ct/DB", "TA/DB/Ct" };

enum
{
	DISPLAY_MENU_BRIGHTNESS = 0,
#if ! defined(PLATFORM_GD77S)
	DISPLAY_MENU_BRIGHTNESS_NIGHT,
#endif
	DISPLAY_MENU_BRIGHTNESS_OFF,
#if ! (defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017))
	DISPLAY_MENU_CONTRAST,
#endif
	DISPLAY_MENU_BACKLIGHT_MODE,
	DISPLAY_MENU_TIMEOUT,
	DISPLAY_MENU_SCREEN_INVERT,
#if ! defined(PLATFORM_GD77S)
	DISPLAY_AUTO_NIGHT,
#endif
	DISPLAY_MENU_CONTACT_DISPLAY_ORDER,
	DISPLAY_MENU_CONTACT_DISPLAY_SPLIT_CONTACT,
#if ! defined(PLATFORM_MD9600)
	DISPLAY_BATTERY_UNIT_IN_HEADER,
#endif
	DISPLAY_EXTENDED_INFOS,
#if defined(HAS_SOFT_VOLUME)
	DISPLAY_VISUAL_VOLUME,
#endif
#if ! defined(PLATFORM_MD9600)
	DISPLAY_ALL_LEDS_ENABLED,
#endif
	DISPLAY_TIMEZONE_VALUE,
	DISPLAY_TIME_UTC_OR_LOCAL,
	DISPLAY_SHOW_DISTANCE,
	DISPLAY_CLASSIC_SCREEN, // last, so no existing item (or quick key) moves
	NUM_DISPLAY_MENU_ITEMS
};

menuStatus_t menuDisplayOptions(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		menuDataGlobal.menuOptionsSetQuickkey = 0;
		menuDataGlobal.menuOptionsTimeout = 0;
		menuDataGlobal.newOptionSelected = true;
		menuDataGlobal.numItems = NUM_DISPLAY_MENU_ITEMS;

		if (originalNonVolatileSettings.magicNumber == 0xDEADBEEF)
		{
			// Store original settings, used on cancel event.
			memcpy(&originalNonVolatileSettings, &nonVolatileSettings, sizeof(settingsStruct_t));
		}

		voicePromptsInit();
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		voicePromptsAppendLanguageString(currentLanguage->display_options);
		voicePromptsAppendLanguageString(currentLanguage->menu);
		voicePromptsAppendPrompt(PROMPT_SILENCE);

		menuSystemRegisterExitCallback(exitCallback, NULL);

		updateScreen(true);
		return (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);
	}
	else
	{
		menuDisplayOptionsExitCode = MENU_STATUS_SUCCESS;

		if (ev->hasEvent || (menuDataGlobal.menuOptionsTimeout > 0))
		{
			handleEvent(ev);
		}
	}
	return menuDisplayOptionsExitCode;
}


static void updateScreen(bool isFirstRun)
{
	int mNum = 0;
	char buf[SCREEN_LINE_BUFFER_SIZE];
	const char *leftSide = NULL;// initialize to please the compiler
	const char *rightSideConst = NULL;// initialize to please the compiler
	char rightSideVar[SCREEN_LINE_BUFFER_SIZE];
	voicePrompt_t rightSideUnitsPrompt;
	const char *rightSideUnitsStr;

	displayClearBuf();
	bool settingOption = uiQuickKeysShowChoices(buf, SCREEN_LINE_BUFFER_SIZE, currentLanguage->display_options);

	for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
	{
		if ((settingOption == false) || (i == 0))
		{
			mNum = menuGetMenuOffset(NUM_DISPLAY_MENU_ITEMS, i);
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
				case DISPLAY_MENU_BRIGHTNESS:
					leftSide = currentLanguage->brightness;
					snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%d%%", nonVolatileSettings.displayBacklightPercentage[DAY]);
					break;

#if ! defined(PLATFORM_GD77S)
				case DISPLAY_MENU_BRIGHTNESS_NIGHT:
					leftSide = currentLanguage->brightness_night;
					snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%d%%", nonVolatileSettings.displayBacklightPercentage[NIGHT]);
					break;
#endif
				case DISPLAY_MENU_BRIGHTNESS_OFF:
					leftSide = currentLanguage->brightness_off;
					snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%d%%", nonVolatileSettings.displayBacklightPercentageOff);
					break;

#if ! (defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017))
				case DISPLAY_MENU_CONTRAST:
					leftSide = currentLanguage->contrast;
					snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%d", nonVolatileSettings.displayContrast);
					break;
#endif
				case DISPLAY_MENU_BACKLIGHT_MODE:
					{
						const char *backlightModes[] = { currentLanguage->Auto, currentLanguage->squelch, currentLanguage->manual, currentLanguage->buttons, currentLanguage->none };
						leftSide = currentLanguage->mode;
						rightSideConst = backlightModes[nonVolatileSettings.backlightMode];
					}
					break;

				case DISPLAY_MENU_TIMEOUT:
					leftSide = currentLanguage->backlight_timeout;
					if ((nonVolatileSettings.backlightMode == BACKLIGHT_MODE_AUTO) ||
							(nonVolatileSettings.backlightMode == BACKLIGHT_MODE_SQUELCH) ||
							(nonVolatileSettings.backlightMode == BACKLIGHT_MODE_BUTTONS))
					{
						if (nonVolatileSettings.backLightTimeout == 0)
						{
							rightSideConst = currentLanguage->no;
						}
						else
						{
							snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%d", nonVolatileSettings.backLightTimeout);
							rightSideUnitsPrompt = PROMPT_SECONDS;
							rightSideUnitsStr = "s";
						}
					}
					else
					{
						rightSideConst = currentLanguage->n_a;
					}
					break;

				case DISPLAY_MENU_SCREEN_INVERT:
					leftSide = currentLanguage->display_screen_invert;
					rightSideConst = settingsIsOptionBitSet(BIT_INVERSE_VIDEO) ? currentLanguage->screen_invert : currentLanguage->screen_normal;
					break;

#if ! defined(PLATFORM_GD77S)
				case DISPLAY_AUTO_NIGHT:
					leftSide = currentLanguage->auto_night;
					rightSideConst = settingsIsOptionBitSet(BIT_AUTO_NIGHT) ? currentLanguage->on : currentLanguage->off;
					break;
#endif
				case DISPLAY_MENU_CONTACT_DISPLAY_ORDER:
					leftSide = currentLanguage->priority_order;
					snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%s", contactOrders[nonVolatileSettings.contactDisplayPriority]);
					break;

				case DISPLAY_MENU_CONTACT_DISPLAY_SPLIT_CONTACT:
					{
						const char *splitContact[] = { currentLanguage->one_line, currentLanguage->two_lines, currentLanguage->Auto };
						leftSide = currentLanguage->contact;
						rightSideConst = splitContact[nonVolatileSettings.splitContact];
					}
					break;

#if ! defined(PLATFORM_MD9600)
				case DISPLAY_BATTERY_UNIT_IN_HEADER:
					leftSide = currentLanguage->battery;
					if (settingsIsOptionBitSet(BIT_BATTERY_VOLTAGE_IN_HEADER))
					{
						rightSideUnitsPrompt = PROMPT_VOLTS;
						rightSideUnitsStr = "V";
					}
					else
					{
						rightSideUnitsPrompt = PROMPT_PERCENT;
						rightSideUnitsStr = "%";
					}
					break;
#endif
				case DISPLAY_EXTENDED_INFOS:
					{
						const char *extendedInfos[] = { currentLanguage->off, currentLanguage->ts, currentLanguage->pwr, currentLanguage->both };
						leftSide = currentLanguage->info;
						rightSideConst = extendedInfos[nonVolatileSettings.extendedInfosOnScreen];
					}
					break;

#if defined(HAS_SOFT_VOLUME)
				case DISPLAY_VISUAL_VOLUME:
					leftSide = currentLanguage->volume;
					rightSideConst = settingsIsOptionBitSet(BIT_VISUAL_VOLUME) ? currentLanguage->on : currentLanguage->off;
					break;
#endif

#if ! defined(PLATFORM_MD9600)
				case DISPLAY_ALL_LEDS_ENABLED:
					leftSide = currentLanguage->leds;
					rightSideConst = settingsIsOptionBitSet(BIT_ALL_LEDS_DISABLED) ? currentLanguage->off : currentLanguage->on;
					break;
#endif
				case DISPLAY_TIMEZONE_VALUE:
					leftSide = currentLanguage->timeZone;
					buildTimeZoneBufferText(rightSideVar);
					break;

				case DISPLAY_TIME_UTC_OR_LOCAL:
					leftSide = currentLanguage->time;
					rightSideConst = (nonVolatileSettings.timezone & 0x80) ? currentLanguage->local : currentLanguage->UTC;
					break;

				case DISPLAY_SHOW_DISTANCE:
					leftSide = currentLanguage->show_distance;
					rightSideConst = settingsIsOptionBitSet(BIT_DISPLAY_CHANNEL_DISTANCE) ? currentLanguage->on : currentLanguage->off;
					break;

				case DISPLAY_CLASSIC_SCREEN:
					// Not in the language tables: the value goes in rightSideVar, and the
					// label is read out with voicePromptsAppendString() below, as text.
					leftSide = LANG5("Ekran", "Screen", "Pantalla", "\xDD\xEA\xF0\xE0\xED", "Anzeige");
					snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%s", (settingsIsOptionBitSet(BIT_CLASSIC_SCREEN) ?
							LANG5("Klasik", "Classic", "Cl\xE1sico", "\xCE\xE1\xFB\xF7\xED\xFB\xE9", "Klassisch") :
							LANG5("Renkli", "Colour", "Color", "\xD6\xE2\xE5\xF2\xED\xEE\xE9", "Farbig")));
					break;
			}

			// workaround for non standard format of line for colour display
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
					if (mNum == DISPLAY_CLASSIC_SCREEN)
					{
						voicePromptsAppendString(leftSide); // not a language table entry
					}
					else
					{
						voicePromptsAppendLanguageString(leftSide);
					}
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
		else if ((QUICKKEY_TYPE(ev->function) == QUICKKEY_MENU) && (QUICKKEY_ENTRYID(ev->function) < NUM_DISPLAY_MENU_ITEMS))
		{
			menuDataGlobal.currentItemIndex = QUICKKEY_ENTRYID(ev->function);

#if ! defined(PLATFORM_GD77S)
			// Control the brigthness of the current daytime.
			if ((menuDataGlobal.currentItemIndex == DISPLAY_MENU_BRIGHTNESS) || (menuDataGlobal.currentItemIndex == DISPLAY_MENU_BRIGHTNESS_NIGHT))
			{
				if (DAYTIME_CURRENT == DAY)
				{
					menuDataGlobal.currentItemIndex = DISPLAY_MENU_BRIGHTNESS;
				}
				else
				{
					menuDataGlobal.currentItemIndex = DISPLAY_MENU_BRIGHTNESS_NIGHT;
				}
			}
#endif
		}

		if ((QUICKKEY_FUNCTIONID(ev->function) != 0))
		{
			menuDataGlobal.menuOptionsTimeout = 1000;
		}
	}

	if (ev->events & BUTTON_EVENT)
	{
		if (repeatVoicePromptOnSK1(ev))
		{
			return;
		}
	}

	if ((ev->events & KEY_EVENT) && (menuDataGlobal.menuOptionsSetQuickkey == 0) && (menuDataGlobal.menuOptionsTimeout == 0))
	{
		if (KEYCHECK_PRESS(ev->keys, KEY_DOWN) && (menuDataGlobal.numItems != 0))
		{
			isDirty = true;
			menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, NUM_DISPLAY_MENU_ITEMS);
			menuDataGlobal.newOptionSelected = true;
			menuDisplayOptionsExitCode |= MENU_STATUS_LIST_TYPE;
		}
		else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
		{
			isDirty = true;
			menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, NUM_DISPLAY_MENU_ITEMS);
			menuDataGlobal.newOptionSelected = true;
			menuDisplayOptionsExitCode |= MENU_STATUS_LIST_TYPE;
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
		bool displayIsLit = displayIsBacklightLit();

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
				case DISPLAY_MENU_BRIGHTNESS:
					settingsIncrement(nonVolatileSettings.displayBacklightPercentage[DAY],
							(int8_t) ((nonVolatileSettings.displayBacklightPercentage[DAY] < BACKLIGHT_PERCENTAGE_STEP) ? BACKLIGHT_PERCENTAGE_STEP_SMALL : BACKLIGHT_PERCENTAGE_STEP));

					if (nonVolatileSettings.displayBacklightPercentage[DAY] > BACKLIGHT_MAX_PERCENTAGE)
					{
						settingsSet(nonVolatileSettings.displayBacklightPercentage[DAY], (int8_t) BACKLIGHT_MAX_PERCENTAGE);
					}

#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
					displayLightTrigger(true);
#endif
					break;

				case DISPLAY_MENU_BRIGHTNESS_OFF:
					if ((nonVolatileSettings.displayBacklightPercentageOff < nonVolatileSettings.displayBacklightPercentage[DAY]) &&
							(nonVolatileSettings.displayBacklightPercentageOff < nonVolatileSettings.displayBacklightPercentage[NIGHT]))
					{
						settingsIncrement(nonVolatileSettings.displayBacklightPercentageOff,
								(int8_t) ((nonVolatileSettings.displayBacklightPercentageOff < BACKLIGHT_PERCENTAGE_STEP) ? BACKLIGHT_PERCENTAGE_STEP_SMALL : BACKLIGHT_PERCENTAGE_STEP));

						if (nonVolatileSettings.displayBacklightPercentageOff > BACKLIGHT_MAX_PERCENTAGE)
						{
							settingsSet(nonVolatileSettings.displayBacklightPercentageOff, (int8_t) BACKLIGHT_MAX_PERCENTAGE);
						}

						checkMinBacklightValue();

						if ((nonVolatileSettings.backlightMode == BACKLIGHT_MODE_MANUAL) && (!displayIsLit))
						{
							gpioSetDisplayBacklightIntensityPercentage(nonVolatileSettings.displayBacklightPercentageOff);
						}
					}
					break;

#if ! (defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017))
				case DISPLAY_MENU_CONTRAST:
					if (nonVolatileSettings.displayContrast < CONTRAST_MAX_VALUE)
					{
						settingsIncrement(nonVolatileSettings.displayContrast, 1);
					}
					displaySetContrast(nonVolatileSettings.displayContrast);
					break;
#endif
				case DISPLAY_MENU_BACKLIGHT_MODE:
#if (defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017))
					if (nonVolatileSettings.backlightMode < BACKLIGHT_MODE_BUTTONS )
#else
					if (nonVolatileSettings.backlightMode < BACKLIGHT_MODE_NONE)
#endif
					{
						settingsIncrement(nonVolatileSettings.backlightMode, 1);
						updateBacklightMode(nonVolatileSettings.backlightMode);
					}
					break;

				case DISPLAY_MENU_TIMEOUT:
					if ((nonVolatileSettings.backlightMode == BACKLIGHT_MODE_AUTO) ||
							(nonVolatileSettings.backlightMode == BACKLIGHT_MODE_SQUELCH) ||
							(nonVolatileSettings.backlightMode == BACKLIGHT_MODE_BUTTONS))
					{
						settingsIncrement(nonVolatileSettings.backLightTimeout, (uint8_t) BACKLIGHT_TIMEOUT_STEP);
						if (nonVolatileSettings.backLightTimeout > BACKLIGHT_MAX_TIMEOUT)
						{
							settingsSet(nonVolatileSettings.backLightTimeout, (uint8_t) BACKLIGHT_MAX_TIMEOUT);
						}
					}
					break;

				case DISPLAY_MENU_SCREEN_INVERT:
					setDisplayInvert(true);
					break;

#if ! defined(PLATFORM_GD77S)
				case DISPLAY_AUTO_NIGHT:
					if (settingsIsOptionBitSet(BIT_AUTO_NIGHT) == false)
					{
						settingsSetOptionBit(BIT_AUTO_NIGHT, true);
					}
					break;

				case DISPLAY_MENU_BRIGHTNESS_NIGHT:
					settingsIncrement(nonVolatileSettings.displayBacklightPercentage[NIGHT],
							(int8_t) ((nonVolatileSettings.displayBacklightPercentage[NIGHT] < BACKLIGHT_PERCENTAGE_STEP) ? BACKLIGHT_PERCENTAGE_STEP_SMALL : BACKLIGHT_PERCENTAGE_STEP));

					if (nonVolatileSettings.displayBacklightPercentage[NIGHT] > BACKLIGHT_MAX_PERCENTAGE)
					{
						settingsSet(nonVolatileSettings.displayBacklightPercentage[NIGHT], (int8_t) BACKLIGHT_MAX_PERCENTAGE);
					}

#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
					displayLightTrigger(true);
#endif
					break;
#endif
				case DISPLAY_MENU_CONTACT_DISPLAY_ORDER:
					if (nonVolatileSettings.contactDisplayPriority < CONTACT_DISPLAY_PRIO_TA_DB_CC)
					{
						settingsIncrement(nonVolatileSettings.contactDisplayPriority, 1);
					}
					break;

				case DISPLAY_MENU_CONTACT_DISPLAY_SPLIT_CONTACT:
					if (nonVolatileSettings.splitContact < SPLIT_CONTACT_AUTO)
					{
						settingsIncrement(nonVolatileSettings.splitContact, 1);
					}
					break;

#if ! defined(PLATFORM_MD9600)
				case DISPLAY_BATTERY_UNIT_IN_HEADER:
					if (settingsIsOptionBitSet(BIT_BATTERY_VOLTAGE_IN_HEADER) == false)
					{
						settingsSetOptionBit(BIT_BATTERY_VOLTAGE_IN_HEADER, true);
					}
					break;
#endif
				case DISPLAY_EXTENDED_INFOS:
					if (nonVolatileSettings.extendedInfosOnScreen < INFO_ON_SCREEN_BOTH)
					{
						settingsIncrement(nonVolatileSettings.extendedInfosOnScreen, 1);
					}
					break;

#if defined(HAS_SOFT_VOLUME)
				case DISPLAY_VISUAL_VOLUME:
					if (settingsIsOptionBitSet(BIT_VISUAL_VOLUME) == false)
					{
						settingsSetOptionBit(BIT_VISUAL_VOLUME, true);
					}
					break;
#endif

#if ! defined(PLATFORM_MD9600)
				case DISPLAY_ALL_LEDS_ENABLED:
					if (settingsIsOptionBitSet(BIT_ALL_LEDS_DISABLED))
					{
						uint8_t state = LedRead(LED_GREEN);
						settingsSetOptionBit(BIT_ALL_LEDS_DISABLED, false);
						LedWriteDirect(LED_GREEN, state);
					}
					break;
#endif
				case DISPLAY_TIMEZONE_VALUE:
					{
						int tz = (nonVolatileSettings.timezone & 0x7F);

						if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
						{
							tz++;
						}
						else
						{
							tz += 4;
						}

						if (tz <= ((14 * 4) + SETTINGS_TIMEZONE_UTC))
						{
							settingsSet(nonVolatileSettings.timezone, ((nonVolatileSettings.timezone & ~0x7F) + tz));
						}
					}
					break;

				case DISPLAY_TIME_UTC_OR_LOCAL:
					settingsSet(nonVolatileSettings.timezone, (uint8_t) (nonVolatileSettings.timezone | 0x80));
					break;
				case DISPLAY_SHOW_DISTANCE:
					if (settingsIsOptionBitSet(BIT_DISPLAY_CHANNEL_DISTANCE) == false)
					{
						settingsSetOptionBit(BIT_DISPLAY_CHANNEL_DISTANCE, true);
					}
					break;
				case DISPLAY_CLASSIC_SCREEN:
					if (settingsIsOptionBitSet(BIT_CLASSIC_SCREEN) == false)
					{
						settingsSetOptionBit(BIT_CLASSIC_SCREEN, true);
					}
					break;
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
				case DISPLAY_MENU_BRIGHTNESS:
					settingsDecrement(nonVolatileSettings.displayBacklightPercentage[DAY],
							(int8_t) ((nonVolatileSettings.displayBacklightPercentage[DAY] <= BACKLIGHT_PERCENTAGE_STEP) ? 1 : BACKLIGHT_PERCENTAGE_STEP));

					if (nonVolatileSettings.displayBacklightPercentage[DAY] < BACKLIGHT_MIN_USABLE_VALUE)
					{
						settingsSet(nonVolatileSettings.displayBacklightPercentage[DAY], BACKLIGHT_MIN_USABLE_VALUE);
					}

					checkMinBacklightValue();
#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
					displayLightTrigger(true);
#endif
					break;

				case DISPLAY_MENU_BRIGHTNESS_OFF:
					settingsDecrement(nonVolatileSettings.displayBacklightPercentageOff,
							(int8_t) ((nonVolatileSettings.displayBacklightPercentageOff <= BACKLIGHT_PERCENTAGE_STEP) ? BACKLIGHT_PERCENTAGE_STEP_SMALL : BACKLIGHT_PERCENTAGE_STEP));

					if (nonVolatileSettings.displayBacklightPercentageOff < 0)
					{
						settingsSet(nonVolatileSettings.displayBacklightPercentageOff, 0);
					}

					if ((nonVolatileSettings.backlightMode == BACKLIGHT_MODE_MANUAL) && (!displayIsLit))
					{
						gpioSetDisplayBacklightIntensityPercentage(nonVolatileSettings.displayBacklightPercentageOff);
					}
					break;

#if ! (defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017))
				case DISPLAY_MENU_CONTRAST:
					if (nonVolatileSettings.displayContrast > CONTRAST_MIN_VALUE)
					{
						settingsDecrement(nonVolatileSettings.displayContrast, 1);
					}
					displaySetContrast(nonVolatileSettings.displayContrast);
					break;
#endif
				case DISPLAY_MENU_BACKLIGHT_MODE:
					if (nonVolatileSettings.backlightMode > BACKLIGHT_MODE_AUTO)
					{
						settingsDecrement(nonVolatileSettings.backlightMode, 1);
						updateBacklightMode(nonVolatileSettings.backlightMode);
					}
					break;

				case DISPLAY_MENU_TIMEOUT:
					if (((nonVolatileSettings.backlightMode == BACKLIGHT_MODE_AUTO)
							&& (nonVolatileSettings.backLightTimeout >= BACKLIGHT_TIMEOUT_STEP)) ||
							(((nonVolatileSettings.backlightMode == BACKLIGHT_MODE_SQUELCH) || (nonVolatileSettings.backlightMode == BACKLIGHT_MODE_BUTTONS))
									&& (nonVolatileSettings.backLightTimeout >= (BACKLIGHT_TIMEOUT_STEP * 2))))
					{
						settingsDecrement(nonVolatileSettings.backLightTimeout, (uint8_t) BACKLIGHT_TIMEOUT_STEP);
					}
					break;

				case DISPLAY_MENU_SCREEN_INVERT:
					setDisplayInvert(false);
					break;

#if ! defined(PLATFORM_GD77S)
				case DISPLAY_AUTO_NIGHT:
					if (settingsIsOptionBitSet(BIT_AUTO_NIGHT))
					{
						settingsSetOptionBit(BIT_AUTO_NIGHT, false);
					}
					break;

				case DISPLAY_MENU_BRIGHTNESS_NIGHT:
					settingsDecrement(nonVolatileSettings.displayBacklightPercentage[NIGHT],
							(int8_t) ((nonVolatileSettings.displayBacklightPercentage[NIGHT] <= BACKLIGHT_PERCENTAGE_STEP) ? 1 : BACKLIGHT_PERCENTAGE_STEP));

					if (nonVolatileSettings.displayBacklightPercentage[NIGHT] < BACKLIGHT_MIN_USABLE_VALUE)
					{
						settingsSet(nonVolatileSettings.displayBacklightPercentage[NIGHT], BACKLIGHT_MIN_USABLE_VALUE);
					}

					checkMinBacklightValue();
#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
					displayLightTrigger(true);
#endif
					break;
#endif
				case DISPLAY_MENU_CONTACT_DISPLAY_ORDER:
					if (nonVolatileSettings.contactDisplayPriority > CONTACT_DISPLAY_PRIO_CC_DB_TA)
					{
						settingsDecrement(nonVolatileSettings.contactDisplayPriority, 1);
					}
					break;

				case DISPLAY_MENU_CONTACT_DISPLAY_SPLIT_CONTACT:
					if (nonVolatileSettings.splitContact > SPLIT_CONTACT_SINGLE_LINE_ONLY)
					{
						settingsDecrement(nonVolatileSettings.splitContact, 1);
					}
					break;

#if ! defined(PLATFORM_MD9600)
				case DISPLAY_BATTERY_UNIT_IN_HEADER:
					if (settingsIsOptionBitSet(BIT_BATTERY_VOLTAGE_IN_HEADER))
					{
						settingsSetOptionBit(BIT_BATTERY_VOLTAGE_IN_HEADER, false);
					}
					break;
#endif
				case DISPLAY_EXTENDED_INFOS:
					if (nonVolatileSettings.extendedInfosOnScreen > INFO_ON_SCREEN_OFF)
					{
						settingsDecrement(nonVolatileSettings.extendedInfosOnScreen, 1);
					}
					break;

#if defined(HAS_SOFT_VOLUME)
				case DISPLAY_VISUAL_VOLUME:
					if (settingsIsOptionBitSet(BIT_VISUAL_VOLUME))
					{
						settingsSetOptionBit(BIT_VISUAL_VOLUME, false);
					}
					break;
#endif

#if ! defined(PLATFORM_MD9600)
				case DISPLAY_ALL_LEDS_ENABLED:
					if (settingsIsOptionBitSet(BIT_ALL_LEDS_DISABLED) == false)
					{
						LedWriteDirect(LED_GREEN, 0);
						settingsSetOptionBit(BIT_ALL_LEDS_DISABLED, true);
					}
					break;
#endif
				case DISPLAY_TIMEZONE_VALUE:
					{
						int tz = (nonVolatileSettings.timezone & 0x7F);

						if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
						{
							tz--;
						}
						else
						{
							tz -= 4;
						}

						if (tz >= ((-12 * 4) + SETTINGS_TIMEZONE_UTC))
						{
							settingsSet(nonVolatileSettings.timezone, ((nonVolatileSettings.timezone & ~0x7F) + tz));
						}
					}
					break;

				case DISPLAY_TIME_UTC_OR_LOCAL:
					settingsSet(nonVolatileSettings.timezone, (uint8_t) (nonVolatileSettings.timezone & ~0x80));
					break;
				case DISPLAY_SHOW_DISTANCE:
					if (settingsIsOptionBitSet(BIT_DISPLAY_CHANNEL_DISTANCE))
					{
						settingsSetOptionBit(BIT_DISPLAY_CHANNEL_DISTANCE, false);
					}
					break;
				case DISPLAY_CLASSIC_SCREEN:
					if (settingsIsOptionBitSet(BIT_CLASSIC_SCREEN))
					{
						settingsSetOptionBit(BIT_CLASSIC_SCREEN, false);
					}
					break;
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
		uiQuickKeysStore(ev, &menuDisplayOptionsExitCode);
		isDirty = true;
	}

	if (isDirty)
	{
		updateScreen(false);
	}
}

static void updateBacklightMode(uint8_t mode)
{
	settingsSet(nonVolatileSettings.backlightMode, mode);

	switch (mode)
	{
		case BACKLIGHT_MODE_MANUAL:
		case BACKLIGHT_MODE_NONE:
#if ! (defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017))
			displayEnableBacklight(false, nonVolatileSettings.displayBacklightPercentageOff); // Could be MANUAL previously, but in OFF state, so turn it OFF blindly.
#endif
			break;
		case BACKLIGHT_MODE_SQUELCH:
		case BACKLIGHT_MODE_BUTTONS:
			if (nonVolatileSettings.backLightTimeout < BACKLIGHT_TIMEOUT_STEP)
			{
				settingsSet(nonVolatileSettings.backLightTimeout, (uint8_t) BACKLIGHT_TIMEOUT_STEP);
			}
		case BACKLIGHT_MODE_AUTO:
			displayLightTrigger(true);
			break;
	}
}

static void setDisplayInvert(bool invert)
{
	if (invert == settingsIsOptionBitSet(BIT_INVERSE_VIDEO))
	{
		return;// Don't update unless the setting is actually changing
	}

	settingsSetOptionBit(BIT_INVERSE_VIDEO, invert);
	// Need to perform a full reset on the display to change back to non-inverted
#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
	displaySetInvertedState(settingsIsOptionBitSet(BIT_INVERSE_VIDEO));
#else
	displayInit(settingsIsOptionBitSet(BIT_INVERSE_VIDEO));
#endif
}

static void checkMinBacklightValue(void)
{
	if ((nonVolatileSettings.displayBacklightPercentageOff >= nonVolatileSettings.displayBacklightPercentage[DAY]) ||
			(nonVolatileSettings.displayBacklightPercentageOff >= nonVolatileSettings.displayBacklightPercentage[NIGHT]))
	{
		int8_t minBCL = SAFE_MIN(nonVolatileSettings.displayBacklightPercentage[DAY], nonVolatileSettings.displayBacklightPercentage[NIGHT]);

		settingsSet(nonVolatileSettings.displayBacklightPercentageOff,
				(int8_t) (minBCL ? (minBCL - ((nonVolatileSettings.displayBacklightPercentageOff <= BACKLIGHT_PERCENTAGE_STEP) ? BACKLIGHT_PERCENTAGE_STEP_SMALL : BACKLIGHT_PERCENTAGE_STEP)) : 0));
	}
}

static void buildTimeZoneBufferText(char *buffer)
{
	int tz 		    = (nonVolatileSettings.timezone & 0x7F);
	int hoursPart 	= abs((tz - SETTINGS_TIMEZONE_UTC) / 4);
	int minutesPart = 15 * abs(tz % 4);// optimisation . No need to subtract the SETTINGS_TIMEZONE_UTC as we just extra act the modulus 4 part.

	snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%c%2u:%02u", (tz >= SETTINGS_TIMEZONE_UTC) ? '+' : '-', abs(hoursPart), minutesPart);
}

static void applySettings(void)
{
	// Reset last heard list, otherwise entries won't get updated, accordingly to the new setting value
	if (nonVolatileSettings.contactDisplayPriority != originalNonVolatileSettings.contactDisplayPriority)
	{
		lastHeardInitList();
	}

	// if auto night is enabled, disable override
	if (settingsIsOptionBitSet(BIT_AUTO_NIGHT) && settingsIsOptionBitSet(BIT_AUTO_NIGHT_OVERRIDE))
	{
		settingsSetOptionBit(BIT_AUTO_NIGHT_OVERRIDE, false);
		uiDataGlobal.daytimeOverridden = UNDEFINED;
	}
	else if ((settingsIsOptionBitSet(BIT_AUTO_NIGHT) == false) && (uiDataGlobal.daytimeOverridden != UNDEFINED))
	{
		settingsSetOptionBit(BIT_AUTO_NIGHT_OVERRIDE, true);
		settingsSetOptionBit(BIT_AUTO_NIGHT_DAYTIME, (uiDataGlobal.daytimeOverridden == NIGHT));
	}

#if ! defined(PLATFORM_GD77S)
	daytimeThemeApply(DAYTIME_CURRENT);
	daytimeThemeChangeUpdate(true);
#endif
	// All parameters has already been applied
	settingsSaveIfNeeded(true);
	resetOriginalSettingsData();
}

static void exitCallback(void *data)
{
	if (originalNonVolatileSettings.magicNumber != 0xDEADBEEF)
	{
		bool displayIsLit = displayIsBacklightLit();

		if (nonVolatileSettings.displayContrast != originalNonVolatileSettings.displayContrast)
		{
			settingsSet(nonVolatileSettings.displayContrast, originalNonVolatileSettings.displayContrast);
			displaySetContrast(nonVolatileSettings.displayContrast);
		}

		if (settingsIsOptionBitSet(BIT_INVERSE_VIDEO) != settingsIsOptionBitSetFromSettings(&originalNonVolatileSettings, BIT_INVERSE_VIDEO))
		{
			settingsSetOptionBit(BIT_INVERSE_VIDEO, settingsIsOptionBitSetFromSettings(&originalNonVolatileSettings, BIT_INVERSE_VIDEO));
			// Need to perform a full reset on the display to change back to non-inverted
#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
			displaySetInvertedState(settingsIsOptionBitSet(BIT_INVERSE_VIDEO));
#else
			displayInit(settingsIsOptionBitSet(BIT_INVERSE_VIDEO));
#endif
		}

		settingsSet(nonVolatileSettings.displayBacklightPercentage[DAY], originalNonVolatileSettings.displayBacklightPercentage[DAY]);
		settingsSet(nonVolatileSettings.displayBacklightPercentage[NIGHT], originalNonVolatileSettings.displayBacklightPercentage[NIGHT]);
		settingsSet(nonVolatileSettings.displayBacklightPercentageOff, originalNonVolatileSettings.displayBacklightPercentageOff);
		settingsSet(nonVolatileSettings.backLightTimeout, originalNonVolatileSettings.backLightTimeout);

		if (nonVolatileSettings.backlightMode != originalNonVolatileSettings.backlightMode)
		{
			updateBacklightMode(originalNonVolatileSettings.backlightMode);
		}

		if ((nonVolatileSettings.backlightMode == BACKLIGHT_MODE_MANUAL) && (!displayIsLit))
		{
			gpioSetDisplayBacklightIntensityPercentage(nonVolatileSettings.displayBacklightPercentageOff);
		}

		settingsSet(nonVolatileSettings.contactDisplayPriority, originalNonVolatileSettings.contactDisplayPriority);
		settingsSet(nonVolatileSettings.splitContact, originalNonVolatileSettings.splitContact);

		if (settingsIsOptionBitSet(BIT_BATTERY_VOLTAGE_IN_HEADER) != settingsIsOptionBitSetFromSettings(&originalNonVolatileSettings, BIT_BATTERY_VOLTAGE_IN_HEADER))
		{
			settingsSetOptionBit(BIT_BATTERY_VOLTAGE_IN_HEADER, settingsIsOptionBitSetFromSettings(&originalNonVolatileSettings, BIT_BATTERY_VOLTAGE_IN_HEADER));
		}

		settingsSet(nonVolatileSettings.extendedInfosOnScreen, originalNonVolatileSettings.extendedInfosOnScreen);

		if (settingsIsOptionBitSet(BIT_ALL_LEDS_DISABLED) != settingsIsOptionBitSetFromSettings(&originalNonVolatileSettings, BIT_ALL_LEDS_DISABLED))
		{
			uint8_t state = LedRead(LED_GREEN);

			settingsSetOptionBit(BIT_ALL_LEDS_DISABLED, settingsIsOptionBitSetFromSettings(&originalNonVolatileSettings, BIT_ALL_LEDS_DISABLED));
			LedWriteDirect(LED_GREEN, (settingsIsOptionBitSetFromSettings(&originalNonVolatileSettings, BIT_ALL_LEDS_DISABLED) ? 0 : state));
		}

		if (settingsIsOptionBitSet(BIT_AUTO_NIGHT) != settingsIsOptionBitSetFromSettings(&originalNonVolatileSettings, BIT_AUTO_NIGHT))
		{
			settingsSetOptionBit(BIT_AUTO_NIGHT, settingsIsOptionBitSetFromSettings(&originalNonVolatileSettings, BIT_AUTO_NIGHT));
		}

		if (settingsIsOptionBitSet(DISPLAY_SHOW_DISTANCE) != settingsIsOptionBitSetFromSettings(&originalNonVolatileSettings, DISPLAY_SHOW_DISTANCE))
		{
			settingsSetOptionBit(DISPLAY_SHOW_DISTANCE, settingsIsOptionBitSetFromSettings(&originalNonVolatileSettings, DISPLAY_SHOW_DISTANCE));
		}

		if (settingsIsOptionBitSet(BIT_CLASSIC_SCREEN) != settingsIsOptionBitSetFromSettings(&originalNonVolatileSettings, BIT_CLASSIC_SCREEN))
		{
			settingsSetOptionBit(BIT_CLASSIC_SCREEN, settingsIsOptionBitSetFromSettings(&originalNonVolatileSettings, BIT_CLASSIC_SCREEN));
		}

		settingsSaveIfNeeded(true);
		resetOriginalSettingsData();
	}
}
