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

menuDataGlobal_t menuDataGlobal =
{
		.currentItemIndex 		= 0, // each menu can re-use this var to hold the position in their display list. To save wasted memory if they each had their own variable
		.startIndex 			= 0, // as above
		.numItems 				= 0, // as above
		.lightTimer 			= -1,
		.currentMenuList		= NULL,

		.controlData =
		{
				.stackPosition 	= 0,
				.stack 			=
				{
						MENU_EMPTY, MENU_EMPTY, MENU_EMPTY, MENU_EMPTY, MENU_EMPTY, MENU_EMPTY, MENU_EMPTY, MENU_EMPTY, MENU_EMPTY, MENU_EMPTY, MENU_EMPTY, MENU_EMPTY, MENU_EMPTY, MENU_EMPTY, MENU_EMPTY, MENU_EMPTY
				}
		},

		/*
		 * ---------------------- IMPORTANT ----------------------------
		 *
		 * The menuFunctions array and the menusData array.....
		 *
		 * MUST match the enum MENU_SCREENS in menuSystem.h
		 *
		 * ---------------------- IMPORTANT ----------------------------
		 */
		.data 					=
		{
				&menuDataMainMenu,
				&menuDataContact,
				NULL,// zone
				NULL,// RadioInfos
				NULL,// RSSI
				NULL,// LastHeard
				&menuDataOptions,// Options
				NULL,// General options
				NULL,// Radio options
				NULL,// Display options
				NULL,// Sound options
				NULL,// SatelliteScreen
#if defined(HAS_GPS)
				NULL,// GPS
#endif
				NULL,// Contact List
				NULL,// DTMF Contact List
				NULL,// Contact Quick List (SK2+#)
				NULL,// Contact List Quick Menu
				NULL,// Contact Details
				NULL,// Language
				NULL,// Calibration
#if defined(HAS_COLOURS)
				NULL,// Theme options
#endif
				NULL,// Quick menu - Channel
				NULL,// Quick menu - VFO
				NULL,// Channel Details
				NULL,// Firmwareinfo
#if !defined(PLATFORM_GD77S)
				NULL,// APRS options
#endif
				// *** Add new menus to be accessed using quickkey (ID: 0..31) above this line ***
				NULL,// MessageBox
				NULL,// hotspot mode
				NULL,// CPS
				NULL,// Numerical entry
				NULL,// Tx
				NULL,// splash
				NULL,// power off
				NULL,// vfo mode
				NULL,// channel mode
				NULL,// Lock screen
				NULL,// Private Call
				NULL,// New Contact
#if defined(HAS_COLOURS)
				NULL,// Theme items browser
				NULL,// Colour picker
#endif
				NULL,// Selective Call 5-Tone
				NULL,// Morse
				NULL,// Fox hunt
				NULL,// Games menu
				NULL,// Snake
				NULL,// Tetris
				NULL,// Breakout
				NULL,// 2048
				NULL,// Dino
				NULL,// Scores
				NULL,// Morse log
				NULL,// APRS log
				NULL,// APRS send message
				NULL,// Morse practice
				NULL,// APRS WhatsApp
				NULL,// APRS e-mail
				NULL,// User manual
				NULL,// New channel / zone, delete channel
		}
};

static menuFunctionData_t menuFunctions[] =
{
		{ menuDisplayMenuList,      NULL, NULL, 0 },// display Main menu using the menu display system
		{ menuDisplayMenuList,      NULL, NULL, 0 },// display Contact menu using the menu display system
		{ menuZoneList,             NULL, NULL, 0 },
		{ menuRadioInfos,           NULL, NULL, 0 },
		{ menuRSSIScreen,           NULL, NULL, 0 },
		{ menuLastHeard,            NULL, NULL, 0 },
		{ menuDisplayMenuList,      NULL, NULL, 0 },
		{ menuGeneralOptions,       NULL, NULL, 0 },
		{ menuRadioOptions,         NULL, NULL, 0 },
		{ menuDisplayOptions,       NULL, NULL, 0 },
		{ menuSoundOptions,         NULL, NULL, 0 },
#if defined(USING_EXTERNAL_DEBUGGER)
		{ menuSoundOptions,         NULL, NULL, 0 },// hack to remove satellite screen from the build when external debugger is selected, otherwise there is not enough space in the ROM to build the firmware
#else
		{ menuSatelliteScreen,      NULL, NULL, 0 },
#endif
#if defined(HAS_GPS)
		{ menuGPS,                  NULL, NULL, 0 },
#endif
		{ menuContactList,          NULL, NULL, 0 },
		{ menuContactList,          NULL, NULL, 0 },
		{ menuContactList,          NULL, NULL, 0 },
		{ menuContactListSubMenu,   NULL, NULL, 0 },
		{ menuContactDetails,       NULL, NULL, 0 },
		{ menuLanguage,             NULL, NULL, 0 },
		{ menuCalibration,          NULL, NULL, 0 },
#if defined(HAS_COLOURS)
		{ menuThemeOptions,         NULL, NULL, 0 },
#endif
		{ uiChannelModeQuickMenu,   NULL, NULL, 0 },
		{ uiVFOModeQuickMenu,       NULL, NULL, 0 },
		{ menuChannelDetails,       NULL, NULL, 0 },
		{ menuFirmwareInfoScreen,   NULL, NULL, 0 },
#if !defined(PLATFORM_GD77S)
		{ menuAPRSOptions,          NULL, NULL, 0 },
#endif
		// *** Add new menus to be accessed using quickkey (ID: 0..31) above this line ***
		{ uiMessageBox,             NULL, NULL, 0 },
		{ menuHotspotMode,          NULL, NULL, 0 },
		{ uiCPS,                    NULL, NULL, 0 },
		{ menuNumericalEntry,       NULL, NULL, 0 },
		{ menuTxScreen,             NULL, NULL, 0 },
		{ uiSplashScreen,           NULL, NULL, 0 },
		{ uiPowerOff,               NULL, NULL, 0 },
		{ uiVFOMode,                NULL, NULL, 0 },
		{ uiChannelMode,            NULL, NULL, 0 },
		{ menuLockScreen,           NULL, NULL, 0 },
		{ menuPrivateCall,          NULL, NULL, 0 },
		{ menuContactDetails,       NULL, NULL, 0 }, // Contact New
#if defined(HAS_COLOURS)
		{ menuThemeItemsBrowser,    NULL, NULL, 0 },
		{ menuColourPicker,         NULL, NULL, 0 },
#endif
		{ menuSelectiveCall5Tone,   NULL, NULL, 0 },
		{ menuMorse,                NULL, NULL, 0 },
		{ menuFoxHunt,              NULL, NULL, 0 },
		{ menuGames,                NULL, NULL, 0 },
		{ menuGameSnake,            NULL, NULL, 0 },
		{ menuGameTetris,           NULL, NULL, 0 },
		{ menuGameBreakout,         NULL, NULL, 0 },
		{ menuGame2048,             NULL, NULL, 0 },
		{ menuGameDino,             NULL, NULL, 0 },
		{ menuGameScores,           NULL, NULL, 0 },
		{ menuMorseLog,             NULL, NULL, 0 },
		{ menuAPRSLog,              NULL, NULL, 0 },
		{ menuAPRSSend,             NULL, NULL, 0 },
		{ menuMorseTrain,           NULL, NULL, 0 },
		{ menuAPRSWhatsApp,         NULL, NULL, 0 },
		{ menuAPRSEmail,            NULL, NULL, 0 },
		{ menuUserManual,           NULL, NULL, 0 },
		{ menuChannelZone,          NULL, NULL, 0 },
};

// This table is indexed by the MENU_SCREENS enum, and nothing about a mismatch
// shows up at compile time - the screen simply jumps through whatever entry
// happens to sit at that index, which is a hard fault. Adding a screen means
// adding to both, in the same place; this catches forgetting one of them.
_Static_assert(((sizeof(menuFunctions) / sizeof(menuFunctions[0])) == NUM_MENU_ENTRIES),
		"menuFunctions[] and the MENU_SCREENS enum are out of step");

static void menuSystemCheckForFirstEntryAudible(menuStatus_t status)
{
	if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_BEEP)
	{
		// If VP is currently playing, we should not set the next beep, otherwise it
		// will be played at the wrong time (e.g  entering TG/PC input window, ACK beep will be
		// played on the next beep playback event
		if ((nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD) && voicePromptsIsPlaying())
		{
			return;
		}

		if (status & MENU_STATUS_ERROR)
		{
			nextKeyBeepMelody = (int16_t *)MELODY_ERROR_BEEP;
		}
		else if (((status & MENU_STATUS_LIST_TYPE) && (menuDataGlobal.currentItemIndex == 0)) || (status & MENU_STATUS_FORCE_FIRST))
		{
			nextKeyBeepMelody = (int16_t *)MELODY_KEY_BEEP_FIRST_ITEM;
		}
		else if (status & MENU_STATUS_INPUT_TYPE)
		{
			nextKeyBeepMelody = (int16_t *)MELODY_ACK_BEEP;
		}
	}
}

static void menuSystemPushMenuFirstRun(void)
{
	uiEvent_t ev = { .buttons = 0, .keys = NO_KEYCODE, .rotary = 0, .function = 0, .events = NO_EVENT, .hasEvent = false, .time = ticksGetMillis() };
	menuStatus_t status;

	if (uiNotificationIsVisible() && uiNotificationHasTimedOut())
	{
		uiNotificationHide(false);
	}

	// Due to QuickKeys, menu list won't go through menuDisplayMenuList() first, so those
	// two members won't get always initialized. Hence, we need to tag them as uninitialized,
	// and check if they got initialized after entering a menu.
	menuDataGlobal.numItems = INT32_MIN;
	menuDataGlobal.currentMenuList = NULL;
	menuDataGlobal.currentItemIndex = menuFunctions[menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition]].lastItemIndex;
	menuFunctions[menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition]].exitCallback = NULL;
	menuFunctions[menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition]].data = NULL;
	displayLightTrigger(false);

	// This only happens when on Cal Data or Flash init errors.
	if (menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition] == MENU_EMPTY)
	{
		status = MENU_STATUS_SUCCESS;
		displayClearBuf();
		displayPrintCentered(((DISPLAY_SIZE_Y - FONT_SIZE_3_HEIGHT) >> 1), globalFailureMessage, FONT_SIZE_3);
		displayRender();
	}
	else
	{
		status = menuFunctions[menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition]].function(&ev, true);
	}

	if (menuDataGlobal.numItems == INT32_MIN)
	{
		menuDataGlobal.numItems = ((menuDataGlobal.data[menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition]] != NULL) ? menuDataGlobal.data[menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition]]->numItems : 0);
	}

	if (menuDataGlobal.currentMenuList == NULL)
	{
		menuDataGlobal.currentMenuList = ((menuDataGlobal.data[menuDataGlobal.controlData.stackPosition] != NULL) ? (menuItemNewData_t *)menuDataGlobal.data[menuDataGlobal.controlData.stackPosition]->items : NULL);
	}

	if (menuDataGlobal.currentItemIndex > menuDataGlobal.numItems)
	{
		menuDataGlobal.currentItemIndex = 0;
	}
	menuSystemCheckForFirstEntryAudible(status);
}

int menuSystemGetLastItemIndex(int stackPos)
{
	if ((stackPos >= 0) && (stackPos <= menuDataGlobal.controlData.stackPosition))
	{
		return menuFunctions[menuDataGlobal.controlData.stack[stackPos]].lastItemIndex;
	}

	return -1;
}

void menuSystemRegisterExitCallback(menuExitCallback_t f, void *data)
{
	menuFunctions[menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition]].exitCallback = f;
	menuFunctions[menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition]].data = data;
}

void menuSystemPushNewMenu(int menuNumber)
{
	if (menuDataGlobal.controlData.stackPosition < 15)
	{
		keyboardReset();
		menuFunctions[menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition]].lastItemIndex = menuDataGlobal.currentItemIndex;
		menuDataGlobal.controlData.stackPosition++;
		menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition] = menuNumber;
#if defined(PLATFORM_MD9600)
		if ((menuNumber != UI_VFO_MODE) && (menuNumber != UI_CHANNEL_MODE))
		{
			uiDataGlobal.sk2latched = false;
		}
#endif
		menuSystemPushMenuFirstRun();
	}
}

void menuSystemCallExitCallback(void)
{
	if (menuFunctions[menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition]].exitCallback != NULL)
	{
		menuFunctions[menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition]].exitCallback(menuFunctions[menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition]].data);
		menuFunctions[menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition]].exitCallback = NULL;
		menuFunctions[menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition]].data = NULL;
	}
}

void menuSystemPopPreviousMenu(void)
{
	keyboardReset();

	menuFunctions[menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition]].lastItemIndex = menuDataGlobal.currentItemIndex;

	menuSystemCallExitCallback();

	// Clear stackPosition + 1 menu trace
	if (menuDataGlobal.controlData.stackPosition < 15)
	{
		menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition + 1] = MENU_EMPTY;
	}

	// Avoid crashing if something goes wrong.
	if (menuDataGlobal.controlData.stackPosition > 0)
	{
		menuDataGlobal.controlData.stackPosition -= 1;
	}
	menuSystemPushMenuFirstRun();
}

void menuSystemPopAllAndDisplayRootMenu(void)
{
	keyboardReset();

	menuFunctions[menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition]].lastItemIndex = menuDataGlobal.currentItemIndex;

	while (menuDataGlobal.controlData.stackPosition >= 1)
	{
		menuSystemCallExitCallback();
		menuDataGlobal.controlData.stackPosition--;
	}

	// MENU_EMPTY is equal to -1 (0xFFFFFFFF), hence the following works, even if it's an int32_t array
	memset(&menuDataGlobal.controlData.stack[1], MENU_EMPTY, sizeof(menuDataGlobal.controlData.stack) - sizeof(int));
	menuDataGlobal.controlData.stackPosition = 0;
	menuSystemPushMenuFirstRun();
}

void menuSystemPopAllAndDisplaySpecificRootMenu(int newRootMenu, bool resetKeyboard)
{
	if (resetKeyboard)
	{
		keyboardReset();
	}

	menuFunctions[menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition]].lastItemIndex = menuDataGlobal.currentItemIndex;

	while (menuDataGlobal.controlData.stackPosition >= 1)
	{
		menuSystemCallExitCallback();
		menuDataGlobal.controlData.stackPosition--;
	}

	// MENU_EMPTY is equal to -1 (0xFFFFFFFF), hence the following works, even if it's an int32_t array
	memset(&menuDataGlobal.controlData.stack[1], MENU_EMPTY, sizeof(menuDataGlobal.controlData.stack) - sizeof(int));
	menuDataGlobal.controlData.stack[0] = newRootMenu;
	menuDataGlobal.controlData.stackPosition = 0;
	menuSystemPushMenuFirstRun();
}

void menuSystemSetCurrentMenu(int menuNumber)
{
	keyboardReset();
	menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition] = menuNumber;
	menuSystemPushMenuFirstRun();
}

int menuSystemGetCurrentMenuNumber(void)
{
	return menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition];
}

int menuSystemGetPreviousMenuNumber(void)
{
	if (menuDataGlobal.controlData.stackPosition >= 1)
	{
		return menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition - 1];
	}

	return MENU_ANY;
}

int menuSystemGetPreviouslyPushedMenuNumber(bool keepIt)
{
	if (menuDataGlobal.controlData.stackPosition < 15)
	{
		int m = menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition + 1];

		if (keepIt == false)
		{
			menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition + 1] = MENU_EMPTY;
		}
		return m;
	}

	return MENU_EMPTY;
}

int menuSystemGetRootMenuNumber(void)
{
	return menuDataGlobal.controlData.stack[0];
}

static void menuSystemPreProcessEvent(uiEvent_t *ev)
{
#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_DM1801) || defined(PLATFORM_DM1801A) || defined(PLATFORM_RD5R)
	if (ev->hasEvent || ((uiDataGlobal.displayQSOState == QSO_DISPLAY_CALLER_DATA) && (nonVolatileSettings.backlightMode != BACKLIGHT_MODE_BUTTONS)))
	{
		if ((ev->events & SYNTHETIC_EVENT) == 0)
		{
			displayLightTrigger(true);
		}
	}
#else // ! MK22
	if (ev->hasEvent)
	{
		if ((!trxIsTransmitting) || (menuSystemGetCurrentMenuNumber() == MENU_CALIBRATION))
		{
			int currentMenu = menuSystemGetCurrentMenuNumber();

			if ((currentMenu != UI_CHANNEL_MODE) && (currentMenu != UI_VFO_MODE))
			{
				if ((currentMenu == MENU_NUMERICAL_ENTRY) && (trxGetMode() == RADIO_MODE_ANALOG))
				{
					switch(ev->keys.key)
					{
						case KEY_UP:
							ev->keys.key = KEY_RED;
							break;
					}
				}
				else
				{
					switch(ev->keys.key)
					{
#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_MD9600)
						case KEY_ROTARY_INCREMENT:
							ev->keys.key = KEY_RIGHT;
							break;
						case KEY_ROTARY_DECREMENT:
							ev->keys.key = KEY_LEFT;
							break;
#endif
#if defined(PLATFORM_MD9600)
						case KEY_B:
							ev->keys.key = KEY_RIGHT;
							break;
						case KEY_C:
							ev->keys.key = KEY_LEFT;
							break;
						case KEY_A:
							ev->keys.key = KEY_RED;
							break;
#endif
						case KEY_FRONT_UP:
							ev->keys.key = KEY_UP;
							break;
						case KEY_FRONT_DOWN:
							ev->keys.key = KEY_DOWN;
							break;
					}
				}
			}
			else
			{
#if defined(PLATFORM_MD9600) || !defined(PLATFORM_RT84_DM1701) // This block is for MD9600, MDUV380 or MD380
				switch(ev->keys.key)
				{
#if ! (defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017) || defined(PLATFORM_MD9600))
					case KEY_UP:
						ev->keys.key = KEY_LEFT;
						break;
					case KEY_DOWN:
						ev->keys.key = KEY_RIGHT;
						break;
#elif defined(PLATFORM_MD2017)
					// The two following events are possibly issued from the Trackball, check that and convert if needed
					case KEY_UP:
						// In Channel and VFO, KEY_UP has to be remapped to KEY_FONT_UP
						if (ev->keys.event == (KEY_MOD_UP | KEY_MOD_PRESS))
						{
							ev->keys.key = KEY_FRONT_UP;
						}
						break;
					case KEY_DOWN:
						// In Channel and VFO, KEY_DOWN has to be remapped to KEY_FONT_DOWN
						if (ev->keys.event == (KEY_MOD_UP | KEY_MOD_PRESS))
						{
							ev->keys.key = KEY_FRONT_DOWN;
						}
						break;
#endif
#if defined(PLATFORM_MD9600)
					case KEY_B:
						ev->keys.key = KEY_RIGHT;
						break;
					case KEY_C:
						ev->keys.key = KEY_LEFT;
						break;
					case KEY_A:
						ev->keys.key = KEY_RED;
						break;
					case KEY_ROTARY_INCREMENT:
						ev->keys.key = KEY_UP;
						break;
					case KEY_ROTARY_DECREMENT:
						ev->keys.key = KEY_DOWN;
						break;
					case KEY_FRONT_UP:
						if ((ev->keys.event & (KEY_MOD_DOWN | KEY_MOD_LONG)) == (KEY_MOD_DOWN | KEY_MOD_LONG))
						{
							ev->keys.key = KEY_UP;
						}
						else
						{
							ev->keys.key = KEY_RIGHT;
						}
						break;
					case KEY_FRONT_DOWN:
						if ((ev->keys.event & (KEY_MOD_DOWN | KEY_MOD_LONG)) == (KEY_MOD_DOWN | KEY_MOD_LONG))
						{
							ev->keys.key = KEY_DOWN;
						}
						else
						{
							ev->keys.key = KEY_LEFT;
						}
						break;
#endif
#if defined(PLATFORM_MD9600) || !defined(PLATFORM_RT84_DM1701)
				}
#endif
#endif
			}
		}

		if ((ev->events & SYNTHETIC_EVENT) == 0)
		{
			displayLightTrigger(true);
		}
	}
	else
	{
		if (((uiDataGlobal.displayQSOState == QSO_DISPLAY_CALLER_DATA) && (nonVolatileSettings.backlightMode != BACKLIGHT_MODE_BUTTONS)) )
		{
			displayLightTrigger(true);
		}
	}
#endif // ! MK22
}

static void menuSystemPostProcessEvent(uiEvent_t *ev)
{
	if (uiDataGlobal.displayQSOState == QSO_DISPLAY_CALLER_DATA)
	{
		uiDataGlobal.displayQSOState = QSO_DISPLAY_IDLE;
	}
}

void menuSystemCallCurrentMenuTick(uiEvent_t *ev)
{
	menuStatus_t status;

	menuSystemPreProcessEvent(ev);

	if (menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition] == MENU_EMPTY)
	{
		status = MENU_STATUS_SUCCESS;
		displayClearBuf();
		displayPrintCentered(((DISPLAY_SIZE_Y - FONT_SIZE_3_HEIGHT) >> 1), globalFailureMessage, FONT_SIZE_3);
		displayRender();
	}
	else
	{
		status = menuFunctions[menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition]].function(ev, false);
	}

	menuSystemPostProcessEvent(ev);

	if (ev->hasEvent)
	{
		menuSystemCheckForFirstEntryAudible(status);
	}
}

void displayLightTrigger(bool fromKeyEvent)
{
	// BACKLIGHT_MODE_MANUAL is handled in main.c
	if ((menuSystemGetCurrentMenuNumber() != UI_TX_SCREEN) &&
			(((nonVolatileSettings.backlightMode == BACKLIGHT_MODE_AUTO) || (nonVolatileSettings.backlightMode == BACKLIGHT_MODE_SQUELCH))
					|| ((nonVolatileSettings.backlightMode == BACKLIGHT_MODE_BUTTONS) && fromKeyEvent)))
	{
		menuDataGlobal.lightTimer = nonVolatileSettings.backLightTimeout * 1000;

		displayEnableBacklight(true, nonVolatileSettings.displayBacklightPercentageOff);
	}
}

// use -1 to force LED on all the time
void displayLightOverrideTimeout(int timeout)
{
	int prevTimer = menuDataGlobal.lightTimer;

	menuDataGlobal.lightTimer = timeout;

	if ((nonVolatileSettings.backlightMode == BACKLIGHT_MODE_AUTO) || (nonVolatileSettings.backlightMode == BACKLIGHT_MODE_SQUELCH))
	{
		// Backlight is OFF, or timeout override (-1) as just been set
		if ((displayIsBacklightLit() == false) || ((timeout == -1) && (prevTimer != -1)))
		{
			displayEnableBacklight(true,nonVolatileSettings.displayBacklightPercentageOff);
		}
	}
}

void menuSystemInit(time_t_custom UTCdateTimeInSecs)
{
	uiEvent_t ev = { .buttons = 0, .keys = NO_KEYCODE, .rotary = 0, .function = 0, .events = NO_EVENT, .hasEvent = false, .time = ticksGetMillis() };

	uiSetUTCDateTimeInSecs(UTCdateTimeInSecs);

	menuDataGlobal.lightTimer = -1;
	menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition] = UI_SPLASH_SCREEN;// set the very first screen as the splash screen
	menuDataGlobal.currentItemIndex = 0;

	if ((nonVolatileSettings.backlightMode == BACKLIGHT_MODE_MANUAL) ||
			(nonVolatileSettings.backlightMode == BACKLIGHT_MODE_BUTTONS) ||
			(nonVolatileSettings.backlightMode == BACKLIGHT_MODE_SQUELCH))
	{
		if (nonVolatileSettings.displayBacklightPercentageOff > 0)
		{
			displayEnableBacklight(false, nonVolatileSettings.displayBacklightPercentageOff);
		}
	}
	else
	{
		displayLightTrigger(false);
	}

	menuFunctions[menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition]].function(&ev, true);// Init and display this screen
}

void menuSystemLanguageHasChanged(void)
{
	// Force full update of menuChannelMode() on next call (if isFirstRun arg. is true)
	if (menuSystemGetRootMenuNumber() == UI_CHANNEL_MODE)
	{
		uiChannelModeColdStart();
	}
}

const menuItemNewData_t mainMenuItems[] =
{
	{   3, MENU_ZONE_LIST       },
	{   6, MENU_CONTACTS_MENU   },
	{  12, MENU_CHANNEL_DETAILS },
	{   4, MENU_RSSI_SCREEN     },
	{   8, MENU_FIRMWARE_INFO   },
	{   9, MENU_OPTIONS         },
	{   7, MENU_LAST_HEARD      },
	{ 150, MENU_RADIO_INFOS     },
	{ 173, MENU_SATELLITE       },
#if defined(HAS_GPS)
	{ 195, MENU_GPS		        },
#endif
	{ 274, MENU_GAMES           },// stringsTable_t.games ("Oyunlar")
	{ 275, UI_APRS_LOG          },// stringsTable_t.aprsLog ("APRS Kayit")
	{ 276, UI_USER_MANUAL       },// stringsTable_t.userManual ("Kilavuz")
	{ 277, UI_CHANNEL_ZONE      },// stringsTable_t.channelZone ("Yeni Kanal/Zon")
};

// Main menu entries name their label by its position in the language table, not
// by field, so a new string has to land exactly at the number its entry gives.
// Checked here rather than trusted, because a mismatch is silent: the menu would
// just show some other label.
_Static_assert(((__builtin_offsetof(stringsTable_t, userManual) - __builtin_offsetof(stringsTable_t, LANGUAGE_NAME)) / LANGUAGE_TEXTS_LENGTH) == 276,
		"stringsTable_t.userManual is not string 276 - the main menu would show the wrong label");
_Static_assert(((__builtin_offsetof(stringsTable_t, channelZone) - __builtin_offsetof(stringsTable_t, LANGUAGE_NAME)) / LANGUAGE_TEXTS_LENGTH) == 277,
		"stringsTable_t.channelZone is not string 277 - the main menu would show the wrong label");

const menuItemsList_t menuDataMainMenu =
{
	.numItems = (sizeof(mainMenuItems) / sizeof(mainMenuItems[0])),
	.items = mainMenuItems
};

static const menuItemNewData_t contactMenuItems[] =
{
	{ 15,  MENU_CONTACT_LIST      },
	{ 139, MENU_DTMF_CONTACT_LIST },
	{ 14,  MENU_CONTACT_NEW       },
	{ 278, UI_CHANNEL_ZONE        },// stringsTable_t.fiveToneContacts; the screen sees it was opened from here
};
_Static_assert(((__builtin_offsetof(stringsTable_t, fiveToneContacts) - __builtin_offsetof(stringsTable_t, LANGUAGE_NAME)) / LANGUAGE_TEXTS_LENGTH) == 278,
		"stringsTable_t.fiveToneContacts is not string 278 - the Contacts menu would show the wrong label");

const menuItemsList_t menuDataContact =
{
	.numItems = (sizeof(contactMenuItems) / sizeof(contactMenuItems[0])),
	.items = contactMenuItems
};

static const menuItemNewData_t optionsMenuItems[] =
{
	{ 190, MENU_GENERAL         },
	{ 191, MENU_RADIO           },
	{  10, MENU_DISPLAY         },
	{  11, MENU_SOUND           },
	{  13, MENU_LANGUAGE        },
	{ 206, MENU_CALIBRATION     },
#if defined(HAS_COLOURS)
	{ 218, MENU_THEME           },
#endif
#if !defined(PLATFORM_GD77S)
	{ 257, MENU_APRS            },
#endif
};

const menuItemsList_t menuDataOptions =
{
	.numItems = (sizeof(optionsMenuItems) / sizeof(optionsMenuItems[0])),
	.items = optionsMenuItems
};

void menuDisplayTitle(const char *title)
{
	themeItem_t fgItem, bgItem;

	displayThemeGetForegroundAndBackgroundItems(&fgItem, &bgItem);

#if defined(HAS_COLOURS)
	displayThemeApply(THEME_ITEM_BG_MENU_NAME, THEME_ITEM_COLOUR_NONE);
	displayFillRect(0, 0, DISPLAY_SIZE_X, 14, false);
#endif

	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayDrawFastHLine(0, 13, DISPLAY_SIZE_X, true);

	displayThemeApply(THEME_ITEM_FG_MENU_NAME, THEME_ITEM_BG);
	displayPrintCore(0, 3, title, FONT_SIZE_2, TEXT_ALIGN_CENTER, false);

	displayThemeApply(fgItem, bgItem);
}

// optStart:
//  - if less than 0, entryText will use fgOptItem colour
//  - if equal to 0, no coloured option will be handled
void menuDisplayEntry(int loopOffset, int focusedItem, const char *entryText, int32_t optStart, themeItem_t fgItem, themeItem_t fgOptItem, themeItem_t bgItem)
{
	bool focused = (focusedItem == menuDataGlobal.currentItemIndex);

	if (focused)
	{
		displayThemeApply(THEME_ITEM_BG_MENU_ITEM_SELECTED, bgItem);
		displayFillRoundRect(DISPLAY_X_POS_MENU_OFFSET, DISPLAY_Y_POS_MENU_ENTRY_HIGHLIGHT
#if defined(PLATFORM_RD5R)
				- 1 // Small V offset due to small font usage
#endif
				+ (loopOffset * MENU_ENTRY_HEIGHT), DISPLAY_SIZE_X - (DISPLAY_X_POS_MENU_OFFSET * 2), MENU_ENTRY_HEIGHT, 2, true);
	}

	displayThemeApply(fgItem, bgItem);

	if ((focused == false) && ((optStart < 0) || (optStart > 0)))
	{
		char buffer[SCREEN_LINE_BUFFER_SIZE];

		snprintf(buffer, (optStart > 0) ? (optStart + 1) : (strlen(entryText) + 1), "%s", entryText);

		if (optStart < 0)
		{
			displayThemeApply(fgOptItem, bgItem);
		}

		displayPrintCore(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_Y_POS_MENU_ENTRY_HIGHLIGHT + (loopOffset * MENU_ENTRY_HEIGHT), buffer, FONT_SIZE_3, TEXT_ALIGN_LEFT, focused);

		if (optStart > 0)
		{
			displayThemeApply(fgOptItem, bgItem);
			displayPrintCore(DISPLAY_X_POS_MENU_TEXT_OFFSET + (optStart * 8), DISPLAY_Y_POS_MENU_ENTRY_HIGHLIGHT + (loopOffset * MENU_ENTRY_HEIGHT), (entryText + optStart), FONT_SIZE_3, TEXT_ALIGN_LEFT, focused);
		}
	}
	else
	{
		displayPrintCore(DISPLAY_X_POS_MENU_TEXT_OFFSET, DISPLAY_Y_POS_MENU_ENTRY_HIGHLIGHT + (loopOffset * MENU_ENTRY_HEIGHT), entryText, FONT_SIZE_3, TEXT_ALIGN_LEFT, focused);
	}

	displayThemeResetToDefault();
}

// Returns menu offset, -1 if the line is before the first menu item, -2 if the line is after the last menu item
int menuGetMenuOffset(int maxMenuItems, int loopOffset)
{
	int offset = menuDataGlobal.currentItemIndex + loopOffset;
	int startOffset = 0;
	int iter = (loopOffset + (MENU_MAX_DISPLAYED_ENTRIES / 2) + ((MENU_MAX_DISPLAYED_ENTRIES & 0x01) ? 1 : 0));

	if (maxMenuItems < MENU_MAX_DISPLAYED_ENTRIES)
	{
		startOffset = (((MENU_MAX_DISPLAYED_ENTRIES - ((MENU_MAX_DISPLAYED_ENTRIES & 0x01) ? 0 : 1)) - maxMenuItems) / 2);

		if (iter <= startOffset)
		{
			return MENU_OFFSET_BEFORE_FIRST_ENTRY;
		}
		else if (iter > (startOffset + maxMenuItems))
		{
			return MENU_OFFSET_AFTER_LAST_ENTRY;
		}
	}

	if (offset < 0)
	{
		if ((maxMenuItems == 1) && (maxMenuItems < MENU_MAX_DISPLAYED_ENTRIES))
		{
			offset = MENU_MAX_DISPLAYED_ENTRIES - 1;
		}
		else
		{
			offset = maxMenuItems + offset;
		}
	}

	if (offset >= maxMenuItems)
	{
		offset = offset - maxMenuItems;
	}

	return offset;
}

/*
 * Returns 99 if key is unknown, or not numerical when digitsOnly is true
 */
#if defined(PLATFORM_MD9600)
#define NON_NUMERICAL_KEYS 7
#else
#define NON_NUMERICAL_KEYS 6
#endif
int menuGetKeypadKeyValue(uiEvent_t *ev, bool digitsOnly)
{
#if !defined(PLATFORM_GD77S)
	uint32_t keypadKeys[] =
	{
#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380)
			KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
			KEY_FRONT_UP, 0, KEY_FRONT_DOWN, 0 , KEY_STAR, KEY_HASH
#elif defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
			KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
			KEY_LEFT, KEY_FRONT_UP, KEY_FRONT_DOWN, KEY_RIGHT, KEY_STAR, KEY_HASH
#elif defined(PLATFORM_MD9600)
			KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
			KEY_A, KEY_B, KEY_C, KEY_D, KEY_STAR, KEY_HASH, KEY_GREEN
#else // MK22, minus GD77S
			KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
			KEY_LEFT, KEY_UP, KEY_DOWN, KEY_RIGHT, KEY_STAR, KEY_HASH
#endif
	};

	for (int i = 0; i < ((sizeof(keypadKeys) / sizeof(keypadKeys[0])) - (digitsOnly ? NON_NUMERICAL_KEYS : 0 )); i++)
	{
		if (
#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380)
				(keypadKeys[i] != 0) &&
#endif
				KEYCHECK_PRESS(ev->keys, keypadKeys[i]))
		{
#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380)
			if (BUTTONCHECK_DOWN(ev, BUTTON_SK1))
			{
				if (i == 10) // Simulate 'B'
				{
					return 11;
				}
				else if (i == 12) // Simulate 'D'
				{
					return 13;
				}

			}
#endif
			return i;
		}
	}
#endif // GD77S

	return 99;
}

void menuUpdateCursor(int pos, bool moved, bool render)
{
#if defined(PLATFORM_RD5R)
	const int MENU_CURSOR_Y = 35;
#elif defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
#if defined(PLATFORM_VARIANT_DM1701)
	const int MENU_CURSOR_Y = 66;
#else
	const int MENU_CURSOR_Y = 78;
#endif
#else
	const int MENU_CURSOR_Y = 46;
#endif

	static uint32_t lastBlink = 0;
	static bool     blink = false;
	uint32_t        m = ticksGetMillis();

	if (moved)
	{
		blink = true;
	}

	if (moved || (m - lastBlink) > 500)
	{
		displayDrawFastHLine(((pos * 8) + DISPLAY_X_POS_MENU_TEXT_OFFSET), MENU_CURSOR_Y, 8, blink);

		blink = !blink;
		lastBlink = m;

		if (render)
		{
			displayRenderRows(MENU_CURSOR_Y / 8, MENU_CURSOR_Y / 8 + 1);
		}
	}
}

void moveCursorLeftInString(char *str, int *pos, bool delete)
{
	int nLen = strlen(str);

	if (*pos > 0)
	{
		if (nLen == 16)
		{
			if (*pos != 15)
			{
				*pos -=1;
			}
		}
		else
		{
			*pos -=1;
		}

		announceChar(str[*pos]); // speak the new char or the char about to be backspaced out.

		if (delete)
		{
			for (int i = *pos; i <= nLen; i++)
			{
				str[i] = str[i + 1];
			}
		}
	}
}

void moveCursorRightInString(char *str, int *pos, int max, bool insert)
{
	int nLen = strlen(str);

	if (*pos < strlen(str))
	{
		if (insert)
		{
			if (nLen < max)
			{
				for (int i = nLen; i > *pos; i--)
				{
					str[i] = str[i - 1];
				}
				str[*pos] = ' ';
			}
		}

		if (*pos < max-1)
		{
			*pos += 1;
			announceChar(str[*pos]); // speak the new char or the char about to be backspaced out.
		}
	}
}

void menuSystemMenuIncrement(int32_t *currentItem, int32_t numItems)
{
	*currentItem = (*currentItem + 1) % numItems;
}

void menuSystemMenuDecrement(int32_t *currentItem, int32_t numItems)
{
	*currentItem = (*currentItem + numItems - 1) % numItems;
}

// For QuickKeys
void menuDisplaySettingOption(const char *entryText, const char *valueText)
{
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);

#if defined(PLATFORM_RD5R)
	displayDrawRoundRect(2, DISPLAY_Y_POS_MENU_ENTRY_HIGHLIGHT - MENU_ENTRY_HEIGHT - 6, DISPLAY_SIZE_X - 4, (MENU_ENTRY_HEIGHT * 2) + 8, 2, true);
	displayFillRoundRect(2, DISPLAY_Y_POS_MENU_ENTRY_HIGHLIGHT - MENU_ENTRY_HEIGHT - 6, DISPLAY_SIZE_X - 4, MENU_ENTRY_HEIGHT + 3, 2, true);

	displayThemeApply(THEME_ITEM_FG_NOTIFICATION, THEME_ITEM_BG);
	if (entryText)
	{
		displayPrintCore(0, DISPLAY_Y_POS_MENU_START - MENU_ENTRY_HEIGHT - 4, entryText, FONT_SIZE_2, TEXT_ALIGN_CENTER, true);
	}
#else
	displayDrawRoundRect(2, DISPLAY_Y_POS_MENU_ENTRY_HIGHLIGHT - MENU_ENTRY_HEIGHT - 2, DISPLAY_SIZE_X - 4, (MENU_ENTRY_HEIGHT * 2) + 4, 2, true);
	displayFillRoundRect(2, DISPLAY_Y_POS_MENU_ENTRY_HIGHLIGHT - MENU_ENTRY_HEIGHT - 2, DISPLAY_SIZE_X - 4, MENU_ENTRY_HEIGHT, 2, true);

	displayThemeApply(THEME_ITEM_FG_NOTIFICATION, THEME_ITEM_BG);
	if (entryText)
	{
		displayPrintCore(0, DISPLAY_Y_POS_MENU_START - MENU_ENTRY_HEIGHT + DISPLAY_V_OFFSET + 2, entryText, FONT_SIZE_2, TEXT_ALIGN_CENTER, true);
	}
#endif

	displayPrintCore(0, DISPLAY_Y_POS_MENU_START + DISPLAY_V_OFFSET, valueText, FONT_SIZE_3, TEXT_ALIGN_CENTER, false);

	displayThemeResetToDefault();
}
