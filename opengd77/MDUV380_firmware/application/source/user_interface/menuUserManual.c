/*
 * Copyright (C) 2026 TA8BMO
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
#include "io/keyboard.h"
#include "user_interface/userManualText.h"
#include <string.h>

// ---------------------------------------------------------------------------
// User manual.
//
// A menu of its own (Menu > Kilavuz), deliberately not part of the credits
// screen. Two levels: the topic list, drawn exactly like every other list menu,
// and a topic page - the text of one topic, scrolled a line at a time with the
// arrows (held, they repeat) and a screen at a time with the channel knob, with a
// scroll bar down the right-hand edge.
//
// The text itself is in userManualText.h, which is generated from
// tools/usermanual/kilavuz_tr.txt by genUserManual.ps1. Edit the .txt and run the
// script rather than editing the header: it is several hundred lines of font
// escapes, and the script is what keeps them right.
// ---------------------------------------------------------------------------
#define UM_BODY_TOP    18  // first text row, clear of the title bar
#define UM_LINE_PITCH  9   // the 6px font is 8 high, plus a pixel between rows
#define UM_BODY_LINES  12  // rows 18..117; the last one's glyphs end at 125
#define UM_BAR_X       (DISPLAY_SIZE_X - 3)

static bool umReading = false; // false: the topic list; true: one topic's text

// The language set is chosen by asking each time rather than storing the choice:
// RAM is full to within a few bytes, and remembering it pushed the build over.
// Turkish, English or Spanish, following the radio's language - the same rule as
// the firmware info message.
#define UM_TOPICS  LANG5(umTopicsTR, umTopicsEN, umTopicsES, umTopicsRU, umTopicsDE)
#define UM_COUNT   LANG5(UM_TOPIC_COUNT_TR, UM_TOPIC_COUNT_EN, UM_TOPIC_COUNT_ES, UM_TOPIC_COUNT_RU, UM_TOPIC_COUNT_DE)

// Line n of the manual. The text is one packed block per language with a table
// of where each line starts, kept in the flash after the codec (.hiconst) -
// see userManualText.h.
static const char *umLine(int n)
{
	return LANG5((umTextTR + umOffsTR[n]), (umTextEN + umOffsEN[n]), (umTextES + umOffsES[n]), (umTextRU + umOffsRU[n]), (umTextDE + umOffsDE[n]));
}
static int  umScroll  = 0;     // first line of the topic that is on screen

static int umMaxScroll(int topic)
{
	int over = ((int) UM_TOPICS[topic].count - UM_BODY_LINES);

	return ((over > 0) ? over : 0);
}

static void umDrawList(void)
{
	displayClearBuf();
	menuDisplayTitle(LANG5("KULLANIM KILAVUZU", "USER MANUAL", "MANUAL", "\xD0\xD3\xCA\xCE\xC2\xCE\xC4\xD1\xD2\xC2\xCE", "HANDBUCH"));

	for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
	{
		int mNum = menuGetMenuOffset(UM_COUNT, i);

		if (mNum == MENU_OFFSET_BEFORE_FIRST_ENTRY)
		{
			continue;
		}
		else if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		menuDisplayEntry(i, mNum, UM_TOPICS[mNum].name, -1, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
	}

	displayRender();
}

static void umDrawPage(void)
{
	int t = menuDataGlobal.currentItemIndex;
	int count = UM_TOPICS[t].count;
	int maxScroll = umMaxScroll(t);

	displayClearBuf();
	menuDisplayTitle(UM_TOPICS[t].name);

	for (int i = 0; (i < UM_BODY_LINES) && ((umScroll + i) < count); i++)
	{
		displayPrintAt(2, (UM_BODY_TOP + (i * UM_LINE_PITCH)), (char *) umLine(UM_TOPICS[t].first + umScroll + i), FONT_SIZE_1);
	}

	// Scroll bar: a thin track with a thumb sized to the share of the topic that
	// is on screen. Only when the topic runs past one screen - a short topic has
	// nothing to scroll and the bar would only suggest otherwise.
	if (maxScroll > 0)
	{
		int trackH = (DISPLAY_SIZE_Y - UM_BODY_TOP);
		int thumbH = ((trackH * UM_BODY_LINES) / count);
		int thumbY;

		if (thumbH < 6)
		{
			thumbH = 6;
		}
		thumbY = (UM_BODY_TOP + (((trackH - thumbH) * umScroll) / maxScroll));

		// The two take opposite flags for the same colour: in this driver a line
		// drawn with true is foreground, but a filled rectangle with true is
		// background - which is what made the thumb invisible at first.
		displayDrawFastVLine((UM_BAR_X + 1), UM_BODY_TOP, trackH, true);
		displayFillRect(UM_BAR_X, thumbY, 3, thumbH, false);
	}

	displayRender();
}

static void umRedraw(void)
{
	if (umReading)
	{
		umDrawPage();
	}
	else
	{
		umDrawList();
	}
}

menuStatus_t menuUserManual(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		keypadAlphaEnable = false; // plain keys: nothing here types text
		menuDataGlobal.numItems = UM_COUNT;

		if ((menuDataGlobal.currentItemIndex < 0) || (menuDataGlobal.currentItemIndex >= UM_COUNT))
		{
			menuDataGlobal.currentItemIndex = 0;
		}

		umReading = false;
		umScroll = 0;
		umDrawList();
		return (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);
	}

	if (ev->hasEvent == false)
	{
		return MENU_STATUS_SUCCESS;
	}

	if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
	{
		umRedraw();
		return MENU_STATUS_SUCCESS;
	}

	// The arrows act on press, so holding one keeps scrolling. The channel knob
	// comes through as LEFT/RIGHT outside the channel and VFO screens, one press
	// per click.
	bool down  = (KEYCHECK_PRESS(ev->keys, KEY_DOWN) || KEYCHECK_PRESS(ev->keys, KEY_FRONT_DOWN));
	bool up    = (KEYCHECK_PRESS(ev->keys, KEY_UP) || KEYCHECK_PRESS(ev->keys, KEY_FRONT_UP));
	bool right = (KEYCHECK_PRESS(ev->keys, KEY_RIGHT) || KEYCHECK_PRESS(ev->keys, KEY_ROTARY_INCREMENT));
	bool left  = (KEYCHECK_PRESS(ev->keys, KEY_LEFT) || KEYCHECK_PRESS(ev->keys, KEY_ROTARY_DECREMENT));

	if (umReading)
	{
		if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			umReading = false;
			umDrawList();
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			// On to the next topic, so the manual can be read straight through.
			menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, UM_COUNT);
			umScroll = 0;
			umDrawPage();
		}
		else if (down || up || right || left)
		{
			int maxScroll = umMaxScroll(menuDataGlobal.currentItemIndex);
			int step = ((right || left) ? (UM_BODY_LINES - 1) : 1); // a screen, keeping one line for context

			umScroll += ((down || right) ? step : -step);

			if (umScroll > maxScroll)
			{
				umScroll = maxScroll;
			}
			if (umScroll < 0)
			{
				umScroll = 0;
			}
			umDrawPage();
		}
		return MENU_STATUS_SUCCESS;
	}

	if (down || right)
	{
		menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, UM_COUNT);
		umDrawList();
	}
	else if (up || left)
	{
		menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, UM_COUNT);
		umDrawList();
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		umReading = true;
		umScroll = 0;
		umDrawPage();
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		menuSystemPopPreviousMenu();
	}

	return MENU_STATUS_SUCCESS;
}
