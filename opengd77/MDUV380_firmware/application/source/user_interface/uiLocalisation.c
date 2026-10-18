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
#include "main.h"
#include "user_interface/uiLocalisation.h"

#include "user_interface/languages/english.h"
#if defined(LANGUAGE_BUILD_JAPANESE)
#include "user_interface/languages/japanese.h"
#endif
#if ! defined(LANGUAGE_BUILD_JAPANESE)
#include "user_interface/languages/turkish.h" // built-in Turkish (TA8BMO Special Edition)
#include "user_interface/languages/spanish.h" // built-in Spanish, added in v0.2.0
#include "user_interface/languages/russian.h" // built-in Russian, added in v0.3.0
#include "user_interface/languages/german.h" // built-in German, added in v1.1
#include "functions/settings.h"
#include "hardware/HX8353E.h"
#endif

// The upstream "User" language slot (a stringsTable_t the CPS could fill with a
// downloaded language) is gone here: this build ships the languages built in, so
// nothing referenced it and it only wasted a whole table's worth of flash
// (~4.7KB). The languages[] list below uses the built-in tables directly.


/*
 * Note.
 *
 * Do not re-order the list of languages, unless you also change the MagicNumber in the settings
 * Because otherwise the radio will load a different language than the one the user previously saved when the radio was turned off
 * Add new languages at the end of the list
 *
 */
// Five tables, about 4.7KB each. They sit in the normal flash before the codec:
// the manuals now fill the flash after the codec (.hiconst), so the five language
// tables go back here where there is still room.
const stringsTable_t languages[] =
{
		englishLanguage,        // englishLanguageName
#if defined(LANGUAGE_BUILD_JAPANESE)
		japaneseLanguage       // japaneseLanguageName
#else
		turkishLanguage, // built-in Turkish (replaces the CPS "User" slot)
		spanishLanguage, // built-in Spanish
		russianLanguage, // built-in Russian
		germanLanguage   // built-in German - new ones go at the end, so the others keep their places
#endif
};
const stringsTable_t *currentLanguage;


uint8_t languagesGetCount(void)
{
#if ! defined(LANGUAGE_BUILD_JAPANESE)
	uint8_t magic[3][4] = { LANGUAGE_TAG_MAGIC_NUMBER, LANGUAGE_TAG_VERSION };

	return ((memcmp(languages[1].magicNumber, magic, sizeof(magic)) == 0) ? 5 : 1);
#else
	return 2;
#endif
}

#if ! defined(LANGUAGE_BUILD_JAPANESE)
int languageGetSavedIndex(void)
{
	int index = LANGUAGE_INDEX_ENGLISH;

	if (settingsIsOptionBitSet(BIT_LANGUAGE_GERMAN))
	{
		index = LANGUAGE_INDEX_GERMAN;
	}
	else if (settingsIsOptionBitSet(BIT_LANGUAGE_RUSSIAN))
	{
		index = LANGUAGE_INDEX_RUSSIAN;
	}
	else if (settingsIsOptionBitSet(BIT_LANGUAGE_SPANISH))
	{
		index = LANGUAGE_INDEX_SPANISH;
	}
	else if (settingsIsOptionBitSet(BIT_SECONDARY_LANGUAGE))
	{
		index = LANGUAGE_INDEX_TURKISH;
	}

	return ((index < languagesGetCount()) ? index : LANGUAGE_INDEX_ENGLISH);
}

void languageSetIndex(int index)
{
	if ((index < 0) || (index >= languagesGetCount()))
	{
		index = LANGUAGE_INDEX_ENGLISH;
	}

	settingsSetOptionBit(BIT_SECONDARY_LANGUAGE, (index == LANGUAGE_INDEX_TURKISH));
	settingsSetOptionBit(BIT_LANGUAGE_SPANISH, (index == LANGUAGE_INDEX_SPANISH));
	settingsSetOptionBit(BIT_LANGUAGE_RUSSIAN, (index == LANGUAGE_INDEX_RUSSIAN));
	settingsSetOptionBit(BIT_LANGUAGE_GERMAN, (index == LANGUAGE_INDEX_GERMAN));
	languageApply(index);
}

void languageApply(int index)
{
	if ((index < 0) || (index >= languagesGetCount()))
	{
		index = LANGUAGE_INDEX_ENGLISH;
	}

	currentLanguage = &languages[index];
	displaySetCyrillic(index == LANGUAGE_INDEX_RUSSIAN); // Russian text is Windows-1251
}

// Pointer compares, not string compares: these are asked many times per redraw.
bool languageIsTurkish(void)
{
	return (currentLanguage == &languages[LANGUAGE_INDEX_TURKISH]);
}

bool languageIsSpanish(void)
{
	return (currentLanguage == &languages[LANGUAGE_INDEX_SPANISH]);
}

bool languageIsRussian(void)
{
	return (currentLanguage == &languages[LANGUAGE_INDEX_RUSSIAN]);
}

bool languageIsGerman(void)
{
	return (currentLanguage == &languages[LANGUAGE_INDEX_GERMAN]);
}
#endif

char currentLanguageGetSymbol(LanguageSymbol_t s)
{
	return currentLanguage->symbols[s];
}
