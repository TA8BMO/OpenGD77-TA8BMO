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
#include "user_interface/photoTA8BMO.h" // colour photo shown at the end of the message
#include <string.h>


enum
{
	FIRMWARE_INFO_BUILD_DETAILS = 0 /* then all credits pages */
};

// Three credit pages: page 1 is my "Special Edition" page, page 2 the people who
// wrote OpenGD77, which this firmware is built on, and page 3 the scrolling
// message below. The copyright headers in the source files stay exactly as they
// were - that is what the licence asks for.
#define FIRMWARE_TEAM_PAGE 2
#define FIRMWARE_MSG_PAGE  3
static const int maxCreditsPages = 3; // the manual moved to its own menu (menuUserManual.c)

// The OpenGD77 developers, as the stock firmware's credits page lists them.
static const char * const openGD77Credits[] =
{
	"Roger VK3KYY", "Daniel F1RMB", "Kai DG4KLU", "Colin G4EML", "Alex DL4LEX", "Jason VK7ZJA (SK)"
};

// The heartfelt message, shown on page 2 as a slowly scrolling column (like film
// credits). Two languages: Turkish when the radio's language is Turkish, English
// otherwise. Pre-wrapped by hand to the small font's width so no runtime wrapping
// is needed; blank strings are paragraph gaps. Turkish letters are \xNN escapes
// for the built-in font, and a string is split wherever an escape is followed by
// a hex digit so the two are not read as one longer escape.
static const char * const heartfeltTR[] =
{
		"Bu yaz\x93l\x93m; aylarca",
		"s\xFCren bir tutkunun,",
		"sabahlara kadar uyan\x93k",
		"kal\x93nan gecelerin ve",
		"hi\xE7""bir zaman",
		"vazge\xE7memenin eseridir.",
		"",
		"Herkesin 'bu telsizde",
		"donan\x93msal olarak",
		"imkans\x93z' dedi\x98i \x96""eyi",
		"ba\x96""ard\x93k: art\x93k FM",
		"sesini kendi i\xE7inde",
		"\xE7\xF6z\xFCyor -- 5-ton,",
		"Mors ve APRS, hi\xE7""bir",
		"donan\x93m eklemeden,",
		"yaln\x93zca yaz\x93l\x93mla.",
		"",
		"Bu, d\xFCnyada bir ilktir.",
		"Bu telsizde bunu daha",
		"\xF6nce kimse yapmad\x93.",
		"",
		"Bir hayalin, sat\x93r sat\x93r,",
		"inatla ger\xE7""ek olu\x96u.",
		"",
		"Amat\xF6r telsize",
		"duydu\x98um sevgiyle,",
		"TA8BMO",
};

static const char * const heartfeltEN[] =
{
		"This firmware is the",
		"work of months of",
		"passion, countless",
		"sleepless nights, and",
		"never giving up.",
		"",
		"We achieved what",
		"everyone said was",
		"hardware-impossible on",
		"this radio: it now",
		"decodes FM audio on its",
		"own -- 5-tone, Morse and",
		"APRS, with no added",
		"hardware, software alone.",
		"",
		"A world first. No one had",
		"ever done this on this",
		"radio before.",
		"",
		"A dream come true, line",
		"by line, through sheer",
		"persistence.",
		"",
		"With love for amateur",
		"radio,",
		"TA8BMO",
};

static const char * const heartfeltES[] =
{
		"Este firmware es fruto de",
		"meses de pasi\xF3n, noches",
		"sin dormir y de no",
		"rendirse nunca.",
		"",
		"Logramos lo que todos",
		"dec\xED""an imposible en el",
		"hardware de esta radio:",
		"ahora decodifica sola el",
		"audio FM: 5 tonos, Morse",
		"y APRS, sin hardware",
		"extra, solo con software.",
		"",
		"Una primicia mundial.",
		"Nadie lo hab\xED""a logrado",
		"antes en esta radio.",
		"",
		"Un sue\xF1o hecho realidad,",
		"l\xEDnea a l\xEDnea, a base de",
		"pura constancia.",
		"",
		"Con cari\xF1o por la",
		"radioafici\xF3n,",
		"TA8BMO",
};

static const char * const heartfeltRU[] =
{
		"\xDD\xF2\xE0 \xEF\xF0\xEE\xF8\xE8\xE2\xEA\xE0 - \xEF\xEB\xEE\xE4",
		"\xEC\xE5\xF1\xFF\xF6\xE5\xE2 \xF3\xE2\xEB\xE5\xF7\xB8\xED\xED\xEE\xF1\xF2\xE8,",
		"\xE1\xE5\xF1\xF1\xEE\xED\xED\xFB\xF5 \xED\xEE\xF7\xE5\xE9 \xE8",
		"\xF3\xEF\xEE\xF0\xF1\xF2\xE2\xE0.",
		"",
		"\xCC\xFB \xF1\xE4\xE5\xEB\xE0\xEB\xE8 \xF2\xEE, \xF7\xF2\xEE \xE2\xF1\xE5",
		"\xF1\xF7\xE8\xF2\xE0\xEB\xE8 \xED\xE5\xE2\xEE\xE7\xEC\xEE\xE6\xED\xFB\xEC \xED\xE0",
		"\xE6\xE5\xEB\xE5\xE7\xE5 \xFD\xF2\xEE\xE9 \xF0\xE0\xF6\xE8\xE8:",
		"\xF2\xE5\xEF\xE5\xF0\xFC \xEE\xED\xE0 \xF1\xE0\xEC\xE0",
		"\xE4\xE5\xEA\xEE\xE4\xE8\xF0\xF3\xE5\xF2 FM: 5-\xF2\xEE\xED,",
		"\xCC\xEE\xF0\xE7\xE5 \xE8 APRS - \xE1\xE5\xE7",
		"\xE4\xEE\xEF. \xE6\xE5\xEB\xE5\xE7\xE0, \xF2\xEE\xEB\xFC\xEA\xEE",
		"\xEF\xF0\xEE\xE3\xF0\xE0\xEC\xEC\xEE\xE9.",
		"",
		"\xCC\xE8\xF0\xEE\xE2\xE0\xFF \xEF\xF0\xE5\xEC\xFC\xE5\xF0\xE0. \xCD\xE8\xEA\xF2\xEE",
		"\xF0\xE0\xED\xFC\xF8\xE5 \xED\xE5 \xE4\xE5\xEB\xE0\xEB \xFD\xF2\xEE\xE3\xEE",
		"\xED\xE0 \xFD\xF2\xEE\xE9 \xF0\xE0\xF6\xE8\xE8.",
		"",
		"\xCC\xE5\xF7\xF2\xE0, \xF1\xF2\xE0\xE2\xF8\xE0\xFF \xFF\xE2\xFC\xFE,",
		"\xF1\xF2\xF0\xEE\xEA\xE0 \xE7\xE0 \xF1\xF2\xF0\xEE\xEA\xEE\xE9,",
		"\xE1\xEB\xE0\xE3\xEE\xE4\xE0\xF0\xFF \xF3\xEF\xEE\xF0\xF1\xF2\xE2\xF3.",
		"",
		"\xD1 \xEB\xFE\xE1\xEE\xE2\xFC\xFE \xEA",
		"\xF0\xE0\xE4\xE8\xEE\xEB\xFE\xE1\xE8\xF2\xE5\xEB\xFC\xF1\xF2\xE2\xF3,",
		"TA8BMO",
};

static const int heartfeltTRLines = (sizeof(heartfeltTR) / sizeof(heartfeltTR[0]));
static const int heartfeltENLines = (sizeof(heartfeltEN) / sizeof(heartfeltEN[0]));
static const int heartfeltESLines = (sizeof(heartfeltES) / sizeof(heartfeltES[0]));
static const int heartfeltRULines = (sizeof(heartfeltRU) / sizeof(heartfeltRU[0]));
static const char * const heartfeltDE[] =
{
		"Diese Firmware ist das",
		"Werk monatelanger",
		"Leidenschaft, schlafloser",
		"N\xE4""chte und des Nicht-",
		"Aufgebens.",
		"",
		"Wir schafften, was alle",
		"f\xFCr hardware-unm\xF6glich",
		"hielten auf diesem Funk-",
		"ger\xE4t: es decodiert jetzt",
		"FM selbst - 5-Ton, Morse",
		"und APRS, ohne Zusatz-",
		"hardware, nur Software.",
		"",
		"Eine Weltpremiere. Das hat",
		"vorher niemand auf diesem",
		"Funkger\xE4t gemacht.",
		"",
		"Ein Traum, Zeile f\xFCr Zeile",
		"wahr geworden, aus reiner",
		"Beharrlichkeit.",
		"",
		"Mit Liebe zum",
		"Amateurfunk,",
		"TA8BMO",
};
static const int heartfeltDELines = (sizeof(heartfeltDE) / sizeof(heartfeltDE[0]));

// Top of the scrolling column, in pixels. Reset to the bottom of the screen when
// the page is entered so the text always starts from the bottom.
static int heartfeltScrollY = DISPLAY_SIZE_Y;

static void displayCredits(bool playVP, uint32_t pageNumber);
static void displayBuildDetails(bool playVP);
static void updateScreen(bool playVP);
static void handleEvent(uiEvent_t *ev);
static int displayMode = FIRMWARE_INFO_BUILD_DETAILS;
static uint32_t menuFirmwareInfoNextUpdateTime;
static bool blink = false;

menuStatus_t menuFirmwareInfoScreen(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		menuDataGlobal.numItems = 0;
		updateScreen(isFirstRun);
	}
	else
	{
		if (ev->time > menuFirmwareInfoNextUpdateTime)
		{
			// The message page redraws often, to scroll smoothly; the other pages
			// only need the slow blink update.
			menuFirmwareInfoNextUpdateTime = ev->time + ((displayMode == FIRMWARE_MSG_PAGE) ? 60 : 500);
			updateScreen(false);
		}

		if (ev->hasEvent)
		{
			handleEvent(ev);
		}
	}
	return MENU_STATUS_SUCCESS;
}

static void updateScreen(bool playVP)
{
	switch(displayMode)
	{
		case FIRMWARE_INFO_BUILD_DETAILS:
			displayBuildDetails(playVP);
			break;

		default:
			displayCredits(playVP, displayMode);
			break;
	}

	blink = !blink;
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

	if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
	{
		updateScreen(false);
		return;
	}

	if (EVENTCHECK_SHORTUP(ev->keys))
	{
		switch(ev->keys.key)
		{
			case KEY_RED:
				menuSystemPopPreviousMenu();
				return;
				break;

			case KEY_UP:
				if (displayMode > FIRMWARE_INFO_BUILD_DETAILS)
				{
					displayMode--;
					heartfeltScrollY = DISPLAY_SIZE_Y; // restart the message scroll on entry
					updateScreen(true);
				}
				break;

			case KEY_DOWN:
				if (displayMode < maxCreditsPages)
				{
					displayMode++;
					heartfeltScrollY = DISPLAY_SIZE_Y; // restart the message scroll on entry
					updateScreen(true);
				}
				break;
		}
	}
}

#if defined(STM32F405xx) && ! defined(PLATFORM_MD9600)
#if 0
static uint32_t cpuGetUnique32(uint32_t byteNumber)
{
	return (*(__IO uint32_t *) (0x1FFF7A10 + 4 * (byteNumber)));
}
#endif
#endif

static void displayBuildDetails(bool playVP)
{
#if !defined(PLATFORM_GD77S)
	char versionBuf[SCREEN_LINE_BUFFER_SIZE];
	const char *radioModel = currentLanguage->openGD77;
	char dateTimeBuf[SCREEN_LINE_BUFFER_SIZE];

	displayClearBuf();

	sprintf(dateTimeBuf, "%d%02d%02d%02d%02d%02d", BUILD_YEAR, BUILD_MONTH, BUILD_DAY, BUILD_HOUR, BUILD_MIN, BUILD_SEC);

#if defined(PLATFORM_MD9600) || defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
	snprintf(versionBuf, SCREEN_LINE_BUFFER_SIZE, "[ %s", XSTRINGIFY(GITVERSION));
#else
	snprintf(versionBuf, SCREEN_LINE_BUFFER_SIZE, "[ %s", GITVERSION);
#endif
	versionBuf[9] = 0; // git hash id 7 char long;
	strcat(versionBuf, (uiDataGlobal.dmrDisabled ? " F ]" : " D ]"));


#if defined(PLATFORM_RD5R)
	displayPrintCentered(0, radioModel, FONT_SIZE_3);
	displayPrintCentered(10, currentLanguage->built, FONT_SIZE_2);
	displayPrintCentered(20, dateTimeBuf , FONT_SIZE_2);
	displayPrintCentered(30, versionBuf, FONT_SIZE_2);
#else
	displayPrintCentered(5, radioModel, FONT_SIZE_3);
	displayPrintCentered(20, currentLanguage->built, FONT_SIZE_2);
	displayPrintCentered(30, dateTimeBuf , FONT_SIZE_2);
	displayPrintCentered(40, versionBuf, FONT_SIZE_2);
#endif

// STM32 platforms (Genuine or Clone)
#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_MD9600) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
	char cpuTypeBuf[SCREEN_LINE_BUFFER_SIZE] = {0};

#if defined(PLATFORM_MD9600) || defined(PLATFORM_VARIANT_UV380_PLUS_10W)
	strncpy(cpuTypeBuf,
#if defined(MD9600_VERSION_1)
			"HW v1 "
#elif defined(MD9600_VERSION_2)
			"HW v2 "
#elif defined(MD9600_VERSION_4)
			"HW v4 "
#elif defined(MD9600_VERSION_5)
			"HW v5 "
#elif defined(PLATFORM_VARIANT_UV380_PLUS_10W)
			"Plus 10W "
#endif
	, SCREEN_LINE_BUFFER_SIZE);
#endif

	strncat(cpuTypeBuf, (NumInterruptPriorityBits == 4) ? "CPU:STM" : "CPU:TYT", (SCREEN_LINE_BUFFER_SIZE - (strlen(cpuTypeBuf) - 1)));
	displayPrintCentered(50, cpuTypeBuf , FONT_SIZE_2);
#endif

	if (playVP && (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD))
	{
		voicePromptsInit();
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		voicePromptsAppendLanguageString(radioModel);
		voicePromptsAppendLanguageString(currentLanguage->built);
		voicePromptsAppendString(dateTimeBuf);
		voicePromptsAppendLanguageString(currentLanguage->gitCommit);
		voicePromptsAppendString(versionBuf);
#if defined(PLATFORM_MD9600) || defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
		voicePromptsAppendString(cpuTypeBuf);
#endif
		promptsPlayNotAfterTx();
	}

#if defined(STM32F405xx) && ! defined(PLATFORM_MD9600)
	sprintf(cpuTypeBuf, "CPU Sig  :0x%04X", cpuGetSignature());
	displayPrintAt(8, 64, cpuTypeBuf , FONT_SIZE_2);
	sprintf(cpuTypeBuf, "CPU Rev  :0x%04X", cpuGetRevision());
	displayPrintAt(8, 74, cpuTypeBuf , FONT_SIZE_2);
	sprintf(cpuTypeBuf, "CPU Pack :0x%04X", cpuGetPackage());
	displayPrintAt(8, 84, cpuTypeBuf , FONT_SIZE_2);
	sprintf(cpuTypeBuf, "CPU Flash:%dkb", cpuGetFlashSize());
	displayPrintAt(8, 94, cpuTypeBuf , FONT_SIZE_2);
#endif

	displayFillTriangle(63 + DISPLAY_H_OFFSET, (DISPLAY_SIZE_Y - 1), 59 + DISPLAY_H_OFFSET, (DISPLAY_SIZE_Y - 3), 67 + DISPLAY_H_OFFSET, (DISPLAY_SIZE_Y - 3), blink);
	displayRender();
#endif
}

// Draws the Turkish-flag emblem (white crescent + 5-point star) centred at (cx,cy).
// The display abstraction is 2-colour (isInverted true = foreground/white), so this
// is a monochrome rendition of the flag's iconic elements rather than a red field.
static void drawTurkishFlag(int16_t cx, int16_t cy, int16_t r)
{
	int16_t moonCx = cx - (r / 4);

	// Crescent: a white disc with a slightly smaller background disc cut out to the
	// right, leaving a crescent that opens towards the star.
	displayFillCircle(moonCx, cy, r, true);
	displayFillCircle(moonCx + (r / 4) + 2, cy, (r * 9) / 10, false);

	// Five-pointed star sitting in the crescent's opening. Vertices are an
	// up-pointing star in fixed point (outer radius 64), scaled to `s` pixels.
	static const int8_t starX[10] = {   0,  15,  61,  24,  38,   0, -38, -24, -61, -15 };
	static const int8_t starY[10] = { -64, -20, -20,   8,  52,  25,  52,   8, -20, -20 };
	int16_t sx = cx + (r / 2) + 1;
	int16_t sy = cy;
	int16_t s  = (r * 3) / 5;

	for (int i = 0; i < 10; i++)
	{
		int j = (i + 1) % 10;
		displayFillTriangle(sx, sy,
				sx + ((starX[i] * s) / 64), sy + ((starY[i] * s) / 64),
				sx + ((starX[j] * s) / 64), sy + ((starY[j] * s) / 64), true);
	}
}

// Blits the colour photo into the screen buffer with its top-left row at topY,
// centred horizontally. Only rows on screen are touched, so it can be scrolled in
// and out. The photo is stored as ordinary RGB565; the display's own native
// format is worked out at runtime (genuine and clone panels differ in byte order
// and RGB/BGR), so each pixel is expanded back to RGB888 and handed to the
// firmware's own converter - which keeps it correct on every radio.
// Blits the greyscale photo into the screen buffer with its top-left row at topY,
// centred horizontally. Only rows on screen are touched, so it can be scrolled in
// and out. Each pixel is a single grey level fanned out to R=G=B, so it does not
// matter how this particular panel wires its colour channels - grey is grey
// whichever way round they are. Colour was tried first and came out wrong because
// this panel's channel order could not be pinned down; greyscale sidesteps it.
static void drawPhotoTA8BMO(int topY)
{
	uint16_t *sb = displayGetScreenBuffer();
	int xStart = ((DISPLAY_SIZE_X - PHOTO_TA8BMO_W) / 2);
	// "Colour: inverse" is done by the panel itself (inverse video), which turns
	// the whole screen negative - the photo included. A photo should not follow
	// the theme, so in that mode it is drawn as its own negative, and the panel's
	// inversion brings it back to the right way round.
	uint32_t flip = (settingsIsOptionBitSet(BIT_INVERSE_VIDEO) ? 255U : 0U);

	for (int row = 0; row < PHOTO_TA8BMO_H; row++)
	{
		int y = (topY + row);

		if ((y < 0) || (y >= DISPLAY_SIZE_Y))
		{
			continue;
		}

		for (int col = 0; col < PHOTO_TA8BMO_W; col++)
		{
			uint32_t v = (photoTA8BMO[(row * PHOTO_TA8BMO_W) + col] ^ flip);

			sb[(y * DISPLAY_SIZE_X) + (xStart + col)] =
					displayConvertRGB888ToNative((v << 16) | (v << 8) | v);
		}
	}
}

static void displayCredits(bool playVP, uint32_t pageNumber)
{
	if (playVP && (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD))
	{
		voicePromptsInit();
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		voicePromptsAppendLanguageString(currentLanguage->credits);
		voicePromptsAppendLanguageString(currentLanguage->menu);
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		promptsPlayNotAfterTx();
	}

	displayClearBuf();

	if (pageNumber == FIRMWARE_MSG_PAGE)
	{
		// Page 3: my little message, rolling up like film credits, with my photo
		// at the very end. No title bar here, so the text gets the whole screen
		// and reads as one piece.
		const char * const *lines = LANG5(heartfeltTR, heartfeltEN, heartfeltES, heartfeltRU, heartfeltDE);
		int nLines = LANG5(heartfeltTRLines, heartfeltENLines, heartfeltESLines, heartfeltRULines, heartfeltDELines);
		const int lineH = 9;
		const int photoGap = 12;                          // blank space between text and photo
		int photoTop = (heartfeltScrollY + (nLines * lineH) + photoGap);
		int totalH = ((nLines * lineH) + photoGap + PHOTO_TA8BMO_H); // whole column, for the loop

		for (int i = 0; i < nLines; i++)
		{
			int y = (heartfeltScrollY + (i * lineH));

			// Only lines fully on the screen are drawn - the small font is not
			// clipped by the print routine, and a partly-off line would wrap onto
			// the wrong row.
			if ((y >= 0) && (y <= (DISPLAY_SIZE_Y - 8)))
			{
				displayPrintCentered(y, (char *)lines[i], FONT_SIZE_1);
			}
		}

		drawPhotoTA8BMO(photoTop);

		// Advance the scroll, and loop once the whole column - text and photo -
		// has climbed off the top.
		heartfeltScrollY -= 1;
		if ((heartfeltScrollY + totalH) < 0)
		{
			heartfeltScrollY = DISPLAY_SIZE_Y;
		}

		displayRender();
		return;
	}

	menuDisplayTitle(currentLanguage->credits);

	if (pageNumber == 1)
	{
		// Page 1: "Developed by TA8BMO - Special Edition" branding, nicely laid out.
		displayPrintCentered(16, "Developed by", FONT_SIZE_2);
		displayPrintCentered(30, "TA8BMO", FONT_SIZE_3);
		displayPrintCentered(50, "Special Edition", FONT_SIZE_2);
		// Turkish flag emblem just below the text.
		drawTurkishFlag((DISPLAY_SIZE_X / 2), 82, 15);
	}
	else if (pageNumber == FIRMWARE_TEAM_PAGE)
	{
		// Page 2: the OpenGD77 team.
		displayPrintCentered(18, "OpenGD77", FONT_SIZE_3);
		for (int i = 0; i < (int) (sizeof(openGD77Credits) / sizeof(openGD77Credits[0])); i++)
		{
			displayPrintCentered((40 + (i * 12)), (char *) openGD77Credits[i], FONT_SIZE_2);
		}
	}

	if ((maxCreditsPages > 1) && (pageNumber <= maxCreditsPages))
	{
		displayFillTriangle(63 + DISPLAY_H_OFFSET, (DISPLAY_SIZE_Y - 1), 59 + DISPLAY_H_OFFSET, (DISPLAY_SIZE_Y - 3), 67 + DISPLAY_H_OFFSET, (DISPLAY_SIZE_Y - 3), blink);
	}

	displayFillTriangle(63 + DISPLAY_H_OFFSET, (DISPLAY_SIZE_Y - 5), 59 + DISPLAY_H_OFFSET, (DISPLAY_SIZE_Y - 3), 67 + DISPLAY_H_OFFSET, (DISPLAY_SIZE_Y - 3), blink);

	displayRender();
}
