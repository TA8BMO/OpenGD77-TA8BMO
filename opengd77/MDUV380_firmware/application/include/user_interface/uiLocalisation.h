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
#ifndef _OPENGD77_UILOCALISATION_H_
#define _OPENGD77_UILOCALISATION_H_

#include "user_interface/languages/uiLanguage.h"

extern const stringsTable_t languages[];
extern const stringsTable_t *currentLanguage;

typedef enum
{
	englishLanguageName = 0,
#if ! defined(LANGUAGE_BUILD_JAPANESE)
	userLanguageName
#else
	japaneseLanguageName
#endif
} languageNamesOrder_t;

extern const languageNamesOrder_t LANGUAGE_DISPLAY_ORDER[];

typedef enum
{
	SYMBOLS_NORTH = 0,
	SYMBOLS_SOUTH,
	SYMBOLS_EAST,
	SYMBOLS_WEST
} LanguageSymbol_t;


uint8_t languagesGetCount(void);
char currentLanguageGetSymbol(LanguageSymbol_t s);

// The built-in languages, in the order of languages[]: English, Turkish, Spanish,
// Russian. The index is kept in option bits (BIT_SECONDARY_LANGUAGE,
// BIT_LANGUAGE_SPANISH, BIT_LANGUAGE_RUSSIAN) rather than a settings field, so
// the settings layout does not change.
#define LANGUAGE_INDEX_ENGLISH 0
#define LANGUAGE_INDEX_TURKISH 1
#define LANGUAGE_INDEX_SPANISH 2
#define LANGUAGE_INDEX_RUSSIAN 3
#define LANGUAGE_INDEX_GERMAN  4

int languageGetSavedIndex(void);          // what the option bits say, clamped to what exists
void languageApply(int index);            // currentLanguage and the display's Cyrillic mode, nothing saved
void languageSetIndex(int index);         // languageApply() plus the option bits; the caller saves
bool languageIsTurkish(void);
bool languageIsSpanish(void);
bool languageIsRussian(void);
bool languageIsGerman(void);

// My own screens pick their text with these: Turkish, English, Spanish, Russian,
// German. Russian text is Windows-1251 (see HX8353E_charset_cyrillic.h); the rest
// are Latin-1. Where a shorter macro leaves a language out, it falls back to the
// English text.
#define LANG5(tr, en, es, ru, de) (languageIsTurkish() ? (tr) : (languageIsSpanish() ? (es) : (languageIsRussian() ? (ru) : (languageIsGerman() ? (de) : (en)))))
#define LANG4(tr, en, es, ru) LANG5((tr), (en), (es), (ru), (en))
#define LANG3(tr, en, es) LANG5((tr), (en), (es), (en), (en))

#endif
