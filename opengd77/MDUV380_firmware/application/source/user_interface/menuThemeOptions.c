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
#include "functions/settings.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiLocalisation.h"
#include "user_interface/uiUtilities.h"
#include "interfaces/wdog.h"
#include "utils.h"

#if defined(HAS_COLOURS)

static const char *themeEntries[THEME_ITEM_MAX];
static uint16_t userThemeData[THEME_ITEM_MAX] = { 0xDEAD, 0xBEEF };
static int32_t currentThemeEntry = 0;
static DayTime_t currentThemeDaytime = UNDEFINED;

static void updateScreen(bool isFirstRun);
static void handleEvent(uiEvent_t *ev);
static void resetUserThemeData(void);

// ------

#define COLOUR_CHANNEL_UNSET   0xFFFF
#define COLOUR_RGB888_UNSET    0xDEADBEEF

typedef enum
{
	COLOUR_CHANNEL_RED = 0,
	COLOUR_CHANNEL_GREEN,
	COLOUR_CHANNEL_BLUE,
	COLOUR_CHANNEL_MAX
} colourChannel_t;

typedef struct
{
		const char *name;
		int32_t value; // we need to handle < 0 and 0xFFFF
} colourChannelData_t;

static colourChannelData_t colourChannelsData[COLOUR_CHANNEL_MAX] =
{
		{ NULL, COLOUR_CHANNEL_UNSET },
		{ NULL, COLOUR_CHANNEL_UNSET },
		{ NULL, COLOUR_CHANNEL_UNSET }
};

typedef struct
{
		uint16_t maxValue;
		uint8_t step;
} channelConstraint_t;

static channelConstraint_t channelConstraints[COLOUR_CHANNEL_MAX] =
{
		{ 248, 8 }, // RED
		{ 252, 4 }, // GREEN
		{ 248, 8 }, // BLUE
};

static uint32_t colourToEditRGB888 = COLOUR_RGB888_UNSET;

static void updateBrowerScreen(bool isFirstRun);
static void handleBrowserEvent(uiEvent_t *ev);
static void exitBrowserCallback(void *data);
static void updatePickerScreen(bool isFirstRun);
static void handlePickerEvent(uiEvent_t *ev);
static void exitPickerCallback(void *data);
static void resetColourChannelsData(void);

static menuStatus_t menuOptionsExitCode = MENU_STATUS_SUCCESS;

menuStatus_t menuThemeOptions(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		currentThemeDaytime = (DayTime_t)uiDataGlobal.currentSelectedChannelNumber;
		menuDataGlobal.numItems = (NIGHT + 1);
		//menuDataGlobal.currentItemIndex = 0;

		voicePromptsInit();
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		voicePromptsAppendLanguageString(currentLanguage->theme_chooser);
		voicePromptsAppendLanguageString(currentLanguage->menu);
		voicePromptsAppendPrompt(PROMPT_SILENCE);

		updateScreen(true);
		return (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);
	}
	else
	{
		menuOptionsExitCode = MENU_STATUS_SUCCESS;

		if (ev->hasEvent)
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
	const char *leftSide = NULL;// initialize to please the compiler

	displayClearBuf();
	menuDisplayTitle(currentLanguage->theme_chooser);

	for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
	{
		if (menuDataGlobal.numItems <= (i + 1))
		{
			break;
		}

		mNum = menuGetMenuOffset(menuDataGlobal.numItems, i);
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

		switch(mNum)
		{
			default:
				leftSide = (const char *)((((DayTime_t)mNum) == DAY) ? currentLanguage->daytime_theme_day : currentLanguage->daytime_theme_night);
				break;
		}

		snprintf(buf, SCREEN_LINE_BUFFER_SIZE, "%s", leftSide);

		if (i == 0)
		{
			bool wasPlaying = voicePromptsIsPlaying();

			if (!isFirstRun)
			{
				voicePromptsInit();
			}

			if (!wasPlaying || (menuDataGlobal.newOptionSelected || (menuDataGlobal.menuOptionsTimeout > 0)))
			{
				voicePromptsAppendLanguageString(leftSide);
			}

			promptsPlayNotAfterTx();
		}

		menuDisplayEntry(i, mNum, buf, 0, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_COLOUR_NONE, THEME_ITEM_BG);
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

	if (ev->events & FUNCTION_EVENT)
	{
		if (ev->function == FUNC_REDRAW)
		{
			isDirty = true;
			//updateScreen(false);
			//return;
		}
		else if ((QUICKKEY_TYPE(ev->function) == QUICKKEY_MENU) && (QUICKKEY_ENTRYID(ev->function) < (NIGHT + 1)))
		{
			isDirty = true;
			menuDataGlobal.currentItemIndex = QUICKKEY_ENTRYID(ev->function);
			//updateScreen(false);
		}
	}

	if (ev->events & KEY_EVENT)
	{
		if (KEYCHECK_PRESS(ev->keys, KEY_DOWN) && (menuDataGlobal.numItems != 0))
		{
			isDirty = true;
			menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, (NIGHT + 1));
			menuDataGlobal.newOptionSelected = true;
			menuOptionsExitCode |= MENU_STATUS_LIST_TYPE;
		}
		else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
		{
			isDirty = true;
			menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, (NIGHT + 1));
			menuDataGlobal.newOptionSelected = true;
			menuOptionsExitCode |= MENU_STATUS_LIST_TYPE;
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			currentThemeDaytime = (DayTime_t)menuDataGlobal.currentItemIndex;
			menuSystemPushNewMenu(MENU_THEME_ITEMS_BROWSER);
			return;
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			menuSystemPopPreviousMenu();
			return;
		}
		else if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && BUTTONCHECK_DOWN(ev, BUTTON_SK2))
		{
			saveQuickkeyMenuIndex(ev->keys.key, menuSystemGetCurrentMenuNumber(), menuDataGlobal.currentItemIndex, 0);
			isDirty = true;
		}
	}

	if (isDirty)
	{
		updateScreen(false);
	}
}

menuStatus_t menuThemeItemsBrowser(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		menuDataGlobal.menuOptionsSetQuickkey = 0;
		menuDataGlobal.menuOptionsTimeout = 0;
		menuDataGlobal.newOptionSelected = true;
		menuDataGlobal.numItems = THEME_ITEM_MAX;

		themeEntries[THEME_ITEM_FG_DEFAULT] = currentLanguage->theme_fg_default;
		themeEntries[THEME_ITEM_BG] = currentLanguage->theme_bg;
		themeEntries[THEME_ITEM_FG_DECORATION] = currentLanguage->theme_fg_decoration;
		themeEntries[THEME_ITEM_FG_TEXT_INPUT] = currentLanguage->theme_fg_text_input;
		themeEntries[THEME_ITEM_FG_SPLASHSCREEN] = currentLanguage->theme_fg_splashscreen;
		themeEntries[THEME_ITEM_BG_SPLASHSCREEN] = currentLanguage->theme_bg_splashscreen;
		themeEntries[THEME_ITEM_FG_NOTIFICATION] = currentLanguage->theme_fg_notification;
		themeEntries[THEME_ITEM_FG_WARNING_NOTIFICATION] = currentLanguage->theme_fg_warning_notification;
		themeEntries[THEME_ITEM_FG_ERROR_NOTIFICATION] = currentLanguage->theme_fg_error_notification;
		themeEntries[THEME_ITEM_BG_NOTIFICATION] = currentLanguage->theme_bg_notification;
		themeEntries[THEME_ITEM_FG_MENU_NAME] = currentLanguage->theme_fg_menu_name;
		themeEntries[THEME_ITEM_BG_MENU_NAME] = currentLanguage->theme_bg_menu_name;
		themeEntries[THEME_ITEM_FG_MENU_ITEM] = currentLanguage->theme_fg_menu_item;
		themeEntries[THEME_ITEM_BG_MENU_ITEM_SELECTED] = currentLanguage->theme_fg_menu_item_selected;
		themeEntries[THEME_ITEM_FG_OPTIONS_VALUE] = currentLanguage->theme_fg_options_value;
		themeEntries[THEME_ITEM_FG_HEADER_TEXT] = currentLanguage->theme_fg_header_text;
		themeEntries[THEME_ITEM_BG_HEADER_TEXT] = currentLanguage->theme_bg_header_text;
		themeEntries[THEME_ITEM_FG_RSSI_BAR] = currentLanguage->theme_fg_rssi_bar;
		themeEntries[THEME_ITEM_FG_RSSI_BAR_S9P] = currentLanguage->theme_fg_rssi_bar_s9p;
		themeEntries[THEME_ITEM_FG_CHANNEL_NAME] = currentLanguage->theme_fg_channel_name;
		themeEntries[THEME_ITEM_FG_CHANNEL_CONTACT] = currentLanguage->theme_fg_channel_contact;
		themeEntries[THEME_ITEM_FG_CHANNEL_CONTACT_INFO] = currentLanguage->theme_fg_channel_contact_info;
		themeEntries[THEME_ITEM_FG_ZONE_NAME] = currentLanguage->theme_fg_zone_name;
		themeEntries[THEME_ITEM_FG_RX_FREQ] = currentLanguage->theme_fg_rx_freq;
		themeEntries[THEME_ITEM_FG_TX_FREQ] = currentLanguage->theme_fg_tx_freq;
		themeEntries[THEME_ITEM_FG_CSS_SQL_VALUES] = currentLanguage->theme_fg_css_sql_values;
		themeEntries[THEME_ITEM_FG_TX_COUNTER] = currentLanguage->theme_fg_tx_counter;
		themeEntries[THEME_ITEM_FG_POLAR_DRAWING] = currentLanguage->theme_fg_polar_drawing;
		themeEntries[THEME_ITEM_FG_SATELLITE_COLOUR] = currentLanguage->theme_fg_satellite_colour;
		themeEntries[THEME_ITEM_FG_GPS_NUMBER] = currentLanguage->theme_fg_gps_number;
		themeEntries[THEME_ITEM_FG_GPS_COLOUR] = currentLanguage->theme_fg_gps_colour;
		themeEntries[THEME_ITEM_FG_BD_COLOUR] = currentLanguage->theme_fg_bd_colour;

		// Has to make a working copy of the global theme
		if ((userThemeData[0] == 0xDEAD) && (userThemeData[1] == 0xBEEF))
		{
			memcpy(&userThemeData, &themeItems[currentThemeDaytime], (sizeof(uint16_t) * THEME_ITEM_MAX));
		}
		else
		{
			// Apply the edited colour to the edited theme entry
			if (colourToEditRGB888 != COLOUR_RGB888_UNSET)
			{
				userThemeData[menuDataGlobal.currentItemIndex] = displayConvertRGB888ToNative(colourToEditRGB888);
				colourToEditRGB888 = COLOUR_RGB888_UNSET;
			}
		}

		voicePromptsInit();
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		voicePromptsAppendLanguageString(currentLanguage->theme_options);
		voicePromptsAppendLanguageString(currentLanguage->menu);
		voicePromptsAppendPrompt(PROMPT_SILENCE);

		menuSystemRegisterExitCallback(exitBrowserCallback, NULL);

		updateBrowerScreen(true);
		return (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);
	}
	else
	{
		menuOptionsExitCode = MENU_STATUS_SUCCESS;

		if (ev->hasEvent)
		{
			handleBrowserEvent(ev);
		}
	}
	return menuOptionsExitCode;
}

static void updateBrowerScreen(bool isFirstRun)
{
	int mNum = 0;
	char buf[SCREEN_LINE_BUFFER_SIZE];
	const char *leftSide = NULL;// initialize to please the compiler

	displayClearBuf();
	menuDisplayTitle(currentLanguage->theme_options);

	for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
	{
		mNum = menuGetMenuOffset(THEME_ITEM_MAX, i);
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

		switch(mNum)
		{
			default:
				leftSide = themeEntries[mNum];
				break;
		}

		snprintf(buf, SCREEN_LINE_BUFFER_SIZE, "%s", leftSide);

		if (i == 0)
		{
			bool wasPlaying = voicePromptsIsPlaying();

			if (!isFirstRun)
			{
				voicePromptsInit();
			}

			if (!wasPlaying || (menuDataGlobal.newOptionSelected || (menuDataGlobal.menuOptionsTimeout > 0)))
			{
				voicePromptsAppendLanguageString(leftSide);
			}

			promptsPlayNotAfterTx();
		}

		menuDisplayEntry(i, mNum, buf, 0, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_COLOUR_NONE, THEME_ITEM_BG);

		// Draw the color rectangle
		int16_t x = (DISPLAY_X_POS_MENU_TEXT_OFFSET + (16 * 8) + 2);
		int16_t y = menuEntryY(i) + 2; // the row menuDisplayEntry() drew, which moves on the colour menus
		uint16_t fgColour, bgColour;

		displayGetForegroundAndBackgroundColours(&fgColour, &bgColour);
		displaySetForegroundAndBackgroundColours(userThemeData[mNum], themeItems[DAYTIME_CURRENT][THEME_ITEM_BG]);
		displayFillRect(x, y, 14, 12, false);

		// Outline the color rectangle when it's equal to background or menu focused colour
		if (((i != 0) && (bgColour == userThemeData[mNum])) ||
				((i == 0) && (userThemeData[mNum] == themeItems[DAYTIME_CURRENT][THEME_ITEM_BG_MENU_ITEM_SELECTED])))
		{
			uint32_t invertedColour = (0xFFFFFF - __builtin_bswap16(PLATFORM_COLOUR_FORMAT_TO_RGB888(userThemeData[mNum])));

			displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(invertedColour), themeItems[DAYTIME_CURRENT][THEME_ITEM_BG_MENU_ITEM_SELECTED]);
			displayDrawRect(x, y, 14, 12, true);
		}

		displayThemeResetToDefault();
	}

	displayRender();
}

static void handleBrowserEvent(uiEvent_t *ev)
{
	bool isDirty = false;

	if (ev->events & BUTTON_EVENT)
	{
		if (repeatVoicePromptOnSK1(ev))
		{
			return;
		}
	}

	if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
	{
		updateBrowerScreen(false);
		return;
	}

	if (ev->events & KEY_EVENT)
	{
		if (KEYCHECK_PRESS(ev->keys, KEY_DOWN) && (menuDataGlobal.numItems != 0))
		{
			isDirty = true;
			menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, THEME_ITEM_MAX);
			menuDataGlobal.newOptionSelected = true;
			menuOptionsExitCode |= MENU_STATUS_LIST_TYPE;
		}
		else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
		{
			isDirty = true;
			menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, THEME_ITEM_MAX);
			menuDataGlobal.newOptionSelected = true;
			menuOptionsExitCode |= MENU_STATUS_LIST_TYPE;
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			// User ask to go back to the default (hardcoded) theme
			if (BUTTONCHECK_DOWN(ev, BUTTON_SK1))
			{
				themeInitToDefaultValues(currentThemeDaytime, ((currentThemeDaytime == DAY) ? false : true));
			}
			else
			{
				// Apply edited theme
				memcpy(&themeItems[currentThemeDaytime], &userThemeData, (sizeof(uint16_t) * THEME_ITEM_MAX));
			}

			// Save user's theme to flash, as a CustomData block
			if (BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				displayThemeSaveToFlash(currentThemeDaytime);
			}

			displayThemeResetToDefault();
			menuSystemPopAllAndDisplayRootMenu();
			return;
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			menuSystemPopPreviousMenu();
			return;
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
		{
			colourToEditRGB888 = PLATFORM_COLOUR_FORMAT_TO_RGB888(__builtin_bswap16(userThemeData[menuDataGlobal.currentItemIndex]));
			currentThemeEntry = menuDataGlobal.currentItemIndex;
			menuSystemPushNewMenu(MENU_COLOUR_PICKER);
			return;
		}
	}

	if (isDirty)
	{
		updateBrowerScreen(false);
	}
}

static void exitBrowserCallback(void *data)
{
	resetUserThemeData();
}

//
// Colour Picker
//
menuStatus_t menuColourPicker(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		menuDataGlobal.menuOptionsSetQuickkey = 0;
		menuDataGlobal.menuOptionsTimeout = 0;
		menuDataGlobal.newOptionSelected = true;
		menuDataGlobal.numItems = COLOUR_CHANNEL_MAX;

		if ((colourChannelsData[COLOUR_CHANNEL_RED].value == COLOUR_CHANNEL_UNSET) &&
				(colourChannelsData[COLOUR_CHANNEL_GREEN].value == COLOUR_CHANNEL_UNSET) &&
				(colourChannelsData[COLOUR_CHANNEL_BLUE].value == COLOUR_CHANNEL_UNSET))
		{
			colourChannelsData[COLOUR_CHANNEL_RED].name = currentLanguage->theme_colour_picker_red;
			colourChannelsData[COLOUR_CHANNEL_RED].value = (colourToEditRGB888 & 0xFF0000) >> 16;

			colourChannelsData[COLOUR_CHANNEL_GREEN].name = currentLanguage->theme_colour_picker_green;
			colourChannelsData[COLOUR_CHANNEL_GREEN].value = (colourToEditRGB888 & 0xFF00) >> 8;

			colourChannelsData[COLOUR_CHANNEL_BLUE].name = currentLanguage->theme_colour_picker_blue;
			colourChannelsData[COLOUR_CHANNEL_BLUE].value = (colourToEditRGB888 & 0xFF);
		}

		voicePromptsInit();
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		voicePromptsAppendLanguageString(currentLanguage->theme_options);
		voicePromptsAppendLanguageString(themeEntries[currentThemeEntry]);
		voicePromptsAppendPrompt(PROMPT_SILENCE);

		menuSystemRegisterExitCallback(exitPickerCallback, NULL);

		updatePickerScreen(true);
		return (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);
	}
	else
	{
		menuOptionsExitCode = MENU_STATUS_SUCCESS;

		if (ev->hasEvent)
		{
			handlePickerEvent(ev);
		}
	}

	return menuOptionsExitCode;
}

static uint32_t getColourInEditionRGB888(void)
{
	return ((colourChannelsData[COLOUR_CHANNEL_RED].value << 16) | (colourChannelsData[COLOUR_CHANNEL_GREEN].value << 8) | colourChannelsData[COLOUR_CHANNEL_BLUE].value);
}

static void updatePickerScreen(bool isFirstRun)
{
	int mNum = 0;
	char buf[SCREEN_LINE_BUFFER_SIZE];
	const char *leftSide = NULL;// initialize to please the compiler
	char rightSideVar[SCREEN_LINE_BUFFER_SIZE];

	displayClearBuf();
	menuDisplayTitle(themeEntries[currentThemeEntry]);

	for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
	{
		mNum = menuGetMenuOffset(COLOUR_CHANNEL_MAX, i);
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
		rightSideVar[0] = 0;

		switch(mNum)
		{
			default:
				leftSide = colourChannelsData[mNum].name;
				snprintf(rightSideVar, SCREEN_LINE_BUFFER_SIZE, "%d",
						(settingsIsOptionBitSet(BIT_INVERSE_VIDEO) ? (channelConstraints[mNum].maxValue - colourChannelsData[mNum].value) : colourChannelsData[mNum].value));
				break;
		}

		snprintf(buf, SCREEN_LINE_BUFFER_SIZE, "%s:%s", leftSide, rightSideVar);

		if (i == 0)
		{
			bool wasPlaying = voicePromptsIsPlaying();

			if (!isFirstRun)
			{
				voicePromptsInit();
			}

			if (!wasPlaying || (menuDataGlobal.newOptionSelected || (menuDataGlobal.menuOptionsTimeout > 0)))
			{
				voicePromptsAppendLanguageString(leftSide);
			}

			voicePromptsAppendString(rightSideVar);
			promptsPlayNotAfterTx();
		}

		// We're cheating the menu vertical position, moving it to the bottom of the screen.
		menuDisplayEntry((i + 2), mNum, buf, (strlen(leftSide) + 1), THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);

		// Display color sample.
		uint32_t invertedColour = (0xFFFFFF - __builtin_bswap16(PLATFORM_COLOUR_FORMAT_TO_RGB888(userThemeData[THEME_ITEM_BG])));

		displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(getColourInEditionRGB888()), themeItems[DAYTIME_CURRENT][THEME_ITEM_BG]);
		displayFillRect(20, 25, (DISPLAY_SIZE_X - 40), 48, false);

		displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(invertedColour), themeItems[DAYTIME_CURRENT][THEME_ITEM_BG]);
		displayDrawRect(20, 25, (DISPLAY_SIZE_X - 40), 48, true);
		displayDrawRect(21, 26, (DISPLAY_SIZE_X - 40) - 2, 46, true);

		displayThemeResetToDefault();
	}

	displayRender();
}

static void handlePickerEvent(uiEvent_t *ev)
{
	bool isDirty = false;
	bool isInverted = settingsIsOptionBitSet(BIT_INVERSE_VIDEO);

	if (ev->events & BUTTON_EVENT)
	{
		if (repeatVoicePromptOnSK1(ev))
		{
			return;
		}
	}

	if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
	{
		isDirty = true;
	}

	if (ev->events & KEY_EVENT)
	{
		if (KEYCHECK_PRESS(ev->keys, KEY_DOWN) && (menuDataGlobal.numItems != 0))
		{
			isDirty = true;
			menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, COLOUR_CHANNEL_MAX);
			menuDataGlobal.newOptionSelected = true;
			menuOptionsExitCode |= MENU_STATUS_LIST_TYPE;
		}
		else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
		{
			isDirty = true;
			menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, COLOUR_CHANNEL_MAX);
			menuDataGlobal.newOptionSelected = true;
			menuOptionsExitCode |= MENU_STATUS_LIST_TYPE;
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			// Build New Colour
			colourToEditRGB888 = getColourInEditionRGB888();
			resetColourChannelsData();
			menuSystemPopPreviousMenu();
			return;
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			menuSystemPopPreviousMenu();
			return;
		}
		else if (KEYCHECK_PRESS(ev->keys, (isInverted ? KEY_LEFT : KEY_RIGHT))
#if defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
				|| KEYCHECK_SHORTUP(ev->keys, (isInverted ? KEY_ROTARY_DECREMENT : KEY_ROTARY_INCREMENT))
#endif
		)
		{
			isDirty = true;
			colourChannelsData[menuDataGlobal.currentItemIndex].value += (channelConstraints[menuDataGlobal.currentItemIndex].step * (BUTTONCHECK_DOWN(ev, BUTTON_SK2) ? 3 : 1));

			if (colourChannelsData[menuDataGlobal.currentItemIndex].value > channelConstraints[menuDataGlobal.currentItemIndex].maxValue)
			{
				colourChannelsData[menuDataGlobal.currentItemIndex].value = channelConstraints[menuDataGlobal.currentItemIndex].maxValue;
			}
		}
		else if (KEYCHECK_PRESS(ev->keys, (isInverted ? KEY_RIGHT : KEY_LEFT))
#if defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
				|| KEYCHECK_SHORTUP(ev->keys, (isInverted ? KEY_ROTARY_INCREMENT : KEY_ROTARY_DECREMENT))
#endif
		)
		{
			isDirty = true;
			colourChannelsData[menuDataGlobal.currentItemIndex].value -= (channelConstraints[menuDataGlobal.currentItemIndex].step * (BUTTONCHECK_DOWN(ev, BUTTON_SK2) ? 3 : 1));

			if (colourChannelsData[menuDataGlobal.currentItemIndex].value < 0)
			{
				colourChannelsData[menuDataGlobal.currentItemIndex].value = 0;
			}
		}
	}

	if (isDirty)
	{
		updatePickerScreen(false);
	}
}

static void exitPickerCallback(void *data)
{
	if ((colourChannelsData[COLOUR_CHANNEL_RED].value != COLOUR_CHANNEL_UNSET) &&
			(colourChannelsData[COLOUR_CHANNEL_GREEN].value != COLOUR_CHANNEL_UNSET) &&
			(colourChannelsData[COLOUR_CHANNEL_BLUE].value != COLOUR_CHANNEL_UNSET))
	{
		colourToEditRGB888 = COLOUR_RGB888_UNSET;
		resetColourChannelsData();
	}
}

static void resetUserThemeData(void)
{
	userThemeData[0] = 0xDEAD;
	userThemeData[1] = 0xBEEF;
}

static void resetColourChannelsData(void)
{
	colourChannelsData[COLOUR_CHANNEL_RED].value = colourChannelsData[COLOUR_CHANNEL_GREEN].value = colourChannelsData[COLOUR_CHANNEL_BLUE].value = COLOUR_CHANNEL_UNSET;
}
#endif // HAS_COLOURS

