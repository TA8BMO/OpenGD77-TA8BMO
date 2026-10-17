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

#define CHAR_CONSTANTS_ONLY // Needed to get FONT_CHAR_* glyph offsets
#if defined(PLATFORM_MD9600)
	#if defined(LANGUAGE_BUILD_JAPANESE)
		#include "hardware/ST7567_charset_JA.h"
	#else
		#include "hardware/ST7567_charset.h"
	#endif
#elif (defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017))
	#if defined(LANGUAGE_BUILD_JAPANESE)
		#include "hardware/HX8353E_charset_JA.h"
	#else
		#include "hardware/HX8353E_charset.h"
	#endif
#else
	#if defined(LANGUAGE_BUILD_JAPANESE)
		#include "hardware/UC1701_charset_JA.h"
	#else
		#include "hardware/UC1701_charset.h"
	#endif
#endif

static void updateScreen(bool isFirstRun);
static void handleEvent(uiEvent_t *ev);
static menuStatus_t menuLanguageExitCode = MENU_STATUS_SUCCESS;


static void clearNonLatinChar(uint8_t *str)
{
#if ! defined(LANGUAGE_BUILD_JAPANESE)
	uint8_t *p = str;

	while (*p != '\0')
	{
		switch (*p)
		{
			case FONT_CHAR_CAPITAL_A_WITH_OGONEK: // Ą
			case FONT_CHAR_SMALL_A_WITH_OGONEK:   // ą
			case 192: // À
			case 193: // Á
			case 194: // Â
			case 195: // Ã
			case 196: // Ä
			case 197: // Å
			case 224: // à
			case 225: // á
			case 226: // â
			case 227: // ã
			case 228: // ä
			case 229: // å
				*p = 'a';
				break;

			case FONT_CHAR_CAPITAL_C_WITH_CARON: // Č
			case FONT_CHAR_SMALL_C_WITH_CARON:   // č
			case FONT_CHAR_CAPITAL_C_WITH_ACUTE: // Ć
			case FONT_CHAR_SMALL_C_WITH_ACUTE:   // ć
			case 199: // Ç
			case 231: // ç
				*p = 'c';
				break;

			case FONT_CHAR_CAPITAL_E_WITH_OGONEK: // Ę
			case FONT_CHAR_SMALL_E_WITH_OGONEK:   // ę
			case FONT_CHAR_CAPITAL_E_WITH_CARON:  // Ě
			case FONT_CHAR_SMALL_E_WITH_CARON:    // ě
			case 200: // È
			case 201: // É
			case 202: // Ê
			case 203: // Ë
			case 232: // è
			case 233: // é
			case 234: // ê
			case 235: // ë
				*p = 'e';
				break;

			case FONT_CHAR_G_CAPITAL_WITH_BREVE: // Ğ
			case FONT_CHAR_G_SMALL_WITH_BREVE:   // ğ
				*p = 'g';
				break;

			case FONT_CHAR_SMALL_I_DOTLESS:    // ı
			case FONT_CHAR_CAPITAL_I_WITH_DOT: // İ
			case 204: // Ì
			case 205: // Í
			case 206: // Î
			case 207: // Ï
			case 236: // ì
			case 237: // í
			case 238: // î
			case 239: // ï
				*p ='i';
				break;

			case FONT_CHAR_CAPITAL_L_WITH_STROKE: // Ł
			case FONT_CHAR_SMALL_L_WITH_STROKE:   // ł
				*p = 'l';
				break;

			case FONT_CHAR_CAPITAL_N_WITH_ACUTE: // Ń
			case FONT_CHAR_SMALL_N_WITH_ACUTE:   // ń
			case 209: // Ñ
			case 241: // ñ
				*p = 'n';
				break;

			case 210: // Ò
			case 211: // Ó
			case 212: // Ô
			case 213: // Õ
			case 214: // Ö
			case 216: // Ø
			case 242: // ò
			case 243: // ó
			case 244: // ô
			case 245: // õ
			case 246: // ö
			case 248: // ø
			case 240: // ð
				*p = 'o';
				break;

			case FONT_CHAR_CAPITAL_R_WITH_CARON: // Ř
			case FONT_CHAR_SMALL_R_WITH_CARON:   // ř
			case 208: // Ð
				*p = 'r';
				break;

			case FONT_CHAR_CAPITAL_S_WITH_ACUTE:   // Ś
			case FONT_CHAR_SMALL_S_WITH_ACUTE:     // ś
			case FONT_CHAR_CAPITAL_S_WITH_CARON:   // Š
			case FONT_CHAR_SMALL_S_WITH_CARON:     // š
			case FONT_CHAR_S_CAPITAL_WITH_CEDILLA: // Ş
			case FONT_CHAR_S_SMALL_WITH_CEDILLA:   // ş
			case 223: // ß
				*p = 's';
				break;

			case FONT_CHAR_CAPITAL_U_WITH_RING_ABOVE: // Ů
			case FONT_CHAR_SMALL_U_WITH_RING_ABOVE:   // ů
			case 217: // Ù
			case 218: // Ú
			case 219: // Û
			case 220: // Ü
			case 249: // ù
			case 250: // ú
			case 251: // û
			case 252: // ü
				*p = 'u';
				break;

			case FONT_CHAR_CAPITAL_Y_WITH_DIAERESIS: // Ÿ
			case 221: // Ý
			case 253: // ý
			case 255: // ÿ
				*p = 'y';
				break;

			case FONT_CHAR_SMALL_Z_WITH_CARON:       // ž
			case FONT_CHAR_CAPITAL_Z_WITH_CARON:     // Ž
			case FONT_CHAR_CAPITAL_Z_WITH_ACUTE:     // Ź
			case FONT_CHAR_SMALL_Z_WITH_ACUTE:       // ź
			case FONT_CHAR_CAPITAL_Z_WITH_DOT_ABOVE: // Ż
			case FONT_CHAR_SMALL_Z_WITH_DOT_ABOVE:   // ż
				*p = 'z';
				break;
		}
		p++;
	}
#endif
}

menuStatus_t menuLanguage(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		menuDataGlobal.numItems = languagesGetCount();
		menuDataGlobal.currentItemIndex = languageGetSavedIndex(); // open on the language in use

		voicePromptsInit();
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		voicePromptsAppendLanguageString(currentLanguage->language);
		voicePromptsAppendLanguageString(currentLanguage->menu);
		voicePromptsAppendPrompt(PROMPT_SILENCE);

		updateScreen(true);
		return (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);
	}
	else
	{
		menuLanguageExitCode = MENU_STATUS_SUCCESS;

		if (ev->hasEvent)
		{
			handleEvent(ev);
		}
	}
	return menuLanguageExitCode;
}

static void updateScreen(bool isFirstRun)
{
	int mNum = 0;

	displayClearBuf();
	menuDisplayTitle(currentLanguage->language);

	for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
	{
		mNum = menuGetMenuOffset(languagesGetCount(), i);
		if (mNum == MENU_OFFSET_BEFORE_FIRST_ENTRY)
		{
			continue;
		}
		else if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		if (mNum < languagesGetCount())
		{
			menuDisplayEntry(i, mNum, (char *)languages[mNum].LANGUAGE_NAME, -1, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);

			if (i == 0)
			{
				if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
				{
					char buffer[17];

					snprintf(buffer, 17, "%s", (char *)languages[mNum].LANGUAGE_NAME);

					clearNonLatinChar((uint8_t *)&buffer[0]);

					if (isFirstRun == false)
					{
						voicePromptsInit();
					}

					voicePromptsAppendString(buffer);
					promptsPlayNotAfterTx();
				}
			}
		}
	}

	displayRender();
}

static void handleEvent(uiEvent_t *ev)
{
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
			updateScreen(false);
		}
		else if ((QUICKKEY_TYPE(ev->function) == QUICKKEY_MENU) && (QUICKKEY_ENTRYID(ev->function) < languagesGetCount()))
		{
			menuDataGlobal.currentItemIndex = QUICKKEY_ENTRYID(ev->function);
			languageSetIndex(menuDataGlobal.currentItemIndex);
			settingsSaveIfNeeded(true);
			menuSystemLanguageHasChanged();
			menuSystemPopAllAndDisplayRootMenu();
		}
		return;
	}

	if (KEYCHECK_PRESS(ev->keys, KEY_DOWN) && (menuDataGlobal.numItems != 0))
	{
		menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, languagesGetCount());
		updateScreen(false);
		menuLanguageExitCode |= MENU_STATUS_LIST_TYPE;
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
	{
		menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, languagesGetCount());
		updateScreen(false);
		menuLanguageExitCode |= MENU_STATUS_LIST_TYPE;
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		languageSetIndex(menuDataGlobal.currentItemIndex);
		settingsSaveIfNeeded(true);
		menuSystemLanguageHasChanged();
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
		saveQuickkeyMenuIndex(ev->keys.key, menuSystemGetCurrentMenuNumber(), menuDataGlobal.currentItemIndex, 0);
		return;
	}
}
