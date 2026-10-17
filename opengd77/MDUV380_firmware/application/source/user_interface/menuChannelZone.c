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
#include "functions/aprs.h"
#include "functions/codeplug.h"
#include "functions/settings.h"
#include "functions/sound.h"
#include "functions/trx.h"
#include "io/keyboard.h"
#include <string.h>

// ---------------------------------------------------------------------------
// New channel / new zone, and deleting a channel.
//
// One screen with several modes rather than a screen each: every entry in the
// menu tables costs 20 bytes of main RAM, and main RAM is full to the byte.
//
//  - the first list: "new channel" and "new zone"
//  - the channel form: name, RX and TX frequency, RX and TX tone, bandwidth,
//    the zone it goes into, and a save row
//  - the zone form: just the name
//  - deleting a channel, reached from Channel Details rather than this menu:
//    the power-on password first if the radio has one, then a yes/no question
//    that starts on "no"
//
// The codeplug work is in codeplug.c (codeplugZoneCreate, codeplugChannelDelete);
// this file is only the screens.
//
// All of the state is in CCM RAM, for the same reason as above. CCM RAM is not
// cleared at start-up and holds noise after power-on, so the state carries a
// 32-bit marker and is reset the first time it is found without one. The delete
// request has a marker of its own, so noise can never look like one.
// ---------------------------------------------------------------------------

#define CZ_STATE_MAGIC    0x435A5334U // "CZS4" - changed whenever czState_t changes
#define CZ_DELETE_MAGIC   0x44454C31U // "DEL1"
#define CZ_NAME_MAX       16
#define CZ_TEXT_MAX       23          // longest text typed: an APRS comment (24 bytes with its NUL)
#define CZ_APRS_MAX       8           // the codeplug holds up to 8 APRS configs
#define CZ_FREQ_DIGITS    8           // 145.50000 is typed as 14550000, in 10 Hz units
#define CZ_PIN_MAX        6
#define CZ_ROW_LEN        32
#define CZ_ROW_CHARS      19          // what fits on a menu row in the 8 pixel font

enum
{
	CZ_MODE_LIST = 0,
	CZ_MODE_CHANNEL,
	CZ_MODE_ZONE,
	CZ_MODE_PIN,
	CZ_MODE_CONFIRM,
	CZ_MODE_FT_LIST,   // 5T Contacts: the list
	CZ_MODE_FT_EDIT,   // 5T Contacts: one contact's name and code
	CZ_MODE_FT_DELETE, // 5T Contacts: "delete this contact?"
	CZ_MODE_ZONE_PICK,     // Edit zones: which zone
	CZ_MODE_ZONE_MENU,     // Edit zones: rename / channels / delete
	CZ_MODE_ZONE_CHANNELS, // Edit zones: every channel, ticked if in the zone
	CZ_MODE_ZONE_DELETE,   // Edit zones: "delete this zone?"
	CZ_MODE_PW_NEW,        // Power-on password: the new one
	CZ_MODE_PW_AGAIN,      // Power-on password: the new one again
	CZ_MODE_APRS_LIST,     // APRS settings: the configs
	CZ_MODE_APRS_EDIT,     // APRS settings: one config
	CZ_MODE_TEXT,          // a line of text: APRS name, path or comment, or the callsign
	CZ_MODE_APRS_DELETE    // APRS settings: "delete this config?"
};

enum
{
	CZ_LIST_CHANNEL = 0,
	CZ_LIST_ZONE,
	CZ_LIST_ZONE_EDIT,
	CZ_LIST_CALLSIGN,
	CZ_LIST_APRS,
	CZ_LIST_PASSWORD,
	CZ_LIST_COUNT
};

enum // rows of the APRS config form
{
	CZA_NAME = 0,
	CZA_SSID,
	CZA_PATH1,
	CZA_PATH2,
	CZA_SYMBOL,
	CZA_COMMENT,
	CZA_POSITION,
	CZA_SAVE,
	CZA_DELETE,   // only for a config that already exists
	CZA_COUNT
};

#define CZ_APRS_FLAG_FIXED_POSITION 0x02 // config flags: send the stored position, not the GPS one

enum // what the text editor is editing
{
	CZT_APRS_NAME = 0,
	CZT_PATH1,
	CZT_PATH2,
	CZT_COMMENT,
	CZT_CALLSIGN
};

enum // what can be done to a zone
{
	CZM_RENAME = 0,
	CZM_CHANNELS,
	CZM_DELETE,
	CZM_COUNT
};

enum
{
	CZF_NAME = 0,
	CZF_RX,
	CZF_TX,
	CZF_RX_TONE,
	CZF_TX_TONE,
	CZF_BANDWIDTH,
	CZF_ZONE,
	CZF_SAVE,
	CZF_COUNT
};

enum // rows of the 5T contact form
{
	CZE_NAME = 0,
	CZE_CODE,
	CZE_SAVE,
	CZE_DELETE, // only when editing a contact that exists
	CZE_COUNT
};

// CCM RAM is nearly full too, so what only one screen uses shares space with
// what only another uses: the new channel form and the APRS config being
// edited are never open together, nor are the names and the longer APRS text.
typedef struct
{
	uint32_t valid;          // CZ_STATE_MAGIC once the state has been initialised
	uint32_t magic;          // CZ_DELETE_MAGIC while a delete request waits for the screen to open
	int32_t  pinTyped;
	int32_t  pinFirst;       // new password as first typed, to compare with the second
	int16_t  deleteChannel;
	int16_t  newZone;        // the zone made last, offered first for the next channel; -1 = none
	int16_t  ftIndex;        // 5T contact slot being edited, 0 = a new contact
	int16_t  ftListItem;     // where the 5T contact list was, to go back to it
	int16_t  zoneEdit;       // zone number being edited (Edit zones)
	uint8_t  pinFirstLen;
	uint8_t  pinNext;        // mode to go on to once the password is right
	uint8_t  renaming;       // the zone name editor is renaming zoneEdit, not making a zone
	uint8_t  mode;
	uint8_t  freqLen;        // digits typed into the frequency being entered, 0 = not entering one
	uint8_t  pinLen;
	uint8_t  confirmYes;
	uint8_t  dirty;          // the codeplug changed: the channel screen must re-read it
	uint8_t  aprsIndex;      // APRS config being edited, 1..8, 0 = a new one
	uint8_t  textTarget;     // what CZ_MODE_TEXT is editing (CZT_...)
	union
	{
		struct // the new channel form
		{
			uint32_t rxFreq;         // 10 Hz units, 0 = not entered yet
			uint32_t txFreq;
			uint16_t rxTone;
			uint16_t txTone;
			int16_t  zoneSel;        // zone number the new channel goes into; All Channels' number means no zone
			uint8_t  wide;           // 25 kHz rather than 12.5 kHz
			char     freqDigits[CZ_FREQ_DIGITS];
		};
		codeplugAPRS_Config_t aprs; // APRS settings: the config being edited, written on save
	};
	union
	{
		struct
		{
			char name[CZ_NAME_MAX + 1];
			char code[CZ_NAME_MAX + 1]; // 5T contact code: 0-9, A-D, '*', '#'
		};
		char text[CZ_TEXT_MAX + 1];  // CZ_MODE_TEXT
	};
} czState_t;

static __attribute__((section(".ccmram"))) czState_t cz;

static void czEnsureInit(void)
{
	if (cz.valid != CZ_STATE_MAGIC)
	{
		memset(&cz, 0, sizeof(cz));
		cz.newZone = -1;
		cz.valid = CZ_STATE_MAGIC;
	}
}

static bool czIsTurkish(void)
{
	return (strcmp(currentLanguage->LANGUAGE_NAME, "T\xFC""rk\xE7""e") == 0);
}

#define CZ(tr, en) (czIsTurkish() ? (tr) : (en))

const char *menuChannelZoneDeleteLabel(void)
{
	return CZ("Kanal\x93 Sil", "Delete Channel");
}

// The notification box holds 15 characters, so every message here is kept to that.
static void czNotify(const char *tr, const char *en)
{
	uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 1500, CZ(tr, en), false);
}

static int czZoneCount(void)
{
	return codeplugZonesGetCount(); // real zones plus All Channels, which is always the last
}

static void czZoneName(int zoneNum, char *out) // out: at least 17 bytes
{
	struct_codeplugZone_t zone;

	codeplugZoneGetDataForNumber(zoneNum, &zone);
	codeplugUtilConvertBufToString(zone.name, out, 16);
}

// Which item goes on screen row i, for a list drawn from the top: the first
// item on the first row, the cursor moving down, and the list scrolling only
// once the cursor reaches the bottom. (OpenGD77's own menuGetMenuOffset keeps
// the cursor in the middle and wraps the end of the list round above it, which
// put "new contact" halfway down its own list.) Rows are numbered the way
// menuDisplayEntry takes them, MENU_START_ITERATION_VALUE being the top one.
static int czRowItem(int count, int i)
{
	const int rows = ((MENU_END_ITERATION_VALUE - MENU_START_ITERATION_VALUE) + 1);
	int sel = menuDataGlobal.currentItemIndex;
	int top = ((sel < rows) ? 0 : ((sel - rows) + 1));
	int item = (top + (i - MENU_START_ITERATION_VALUE));

	return ((item < count) ? item : MENU_OFFSET_AFTER_LAST_ENTRY);
}

// Letters only where a name is typed; the frequency rows need plain digits.
static void czUpdateKeypad(void)
{
	keypadAlphaEnable = (((cz.mode == CZ_MODE_CHANNEL) && (menuDataGlobal.currentItemIndex == CZF_NAME)) ||
			((cz.mode == CZ_MODE_FT_EDIT) && (menuDataGlobal.currentItemIndex == CZE_NAME)) ||
			(cz.mode == CZ_MODE_ZONE) || (cz.mode == CZ_MODE_TEXT));
}

static void czSetMode(uint8_t mode)
{
	cz.mode = mode;
	cz.freqLen = 0;
	czUpdateKeypad();
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
static void czFormatFreq(char *out, size_t len, const char *label, uint32_t freq, bool entering)
{
	if (entering)
	{
		char d[CZ_FREQ_DIGITS];

		for (int i = 0; i < CZ_FREQ_DIGITS; i++)
		{
			d[i] = ((i < cz.freqLen) ? cz.freqDigits[i] : '-');
		}
		snprintf(out, len, "%s%c%c%c.%c%c%c%c%c", label, d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
	}
	else if (freq == 0)
	{
		snprintf(out, len, "%s---.-----", label);
	}
	else
	{
		snprintf(out, len, "%s%u.%05u", label, (unsigned) (freq / 100000), (unsigned) (freq % 100000));
	}
}

static void czFormatTone(char *out, size_t len, const char *label, uint16_t tone)
{
	CodeplugCSSTypes_t type = codeplugGetCSSType(tone);

	if (type == CSS_TYPE_CTCSS)
	{
		snprintf(out, len, "%s%u.%uHz", label, (tone / 10), (tone % 10));
	}
	else if (type & CSS_TYPE_DCS)
	{
		dcsPrintf(out, len, (char *) label, tone);
	}
	else
	{
		snprintf(out, len, "%s%s", label, currentLanguage->none);
	}
}

// A label, then as much of the text as fits, keeping the end of it in view -
// that is where the typing happens.
static void czFormatText(char *out, size_t len, const char *label, const char *text, bool cursor)
{
	int room = (CZ_ROW_CHARS - (int) strlen(label) - (cursor ? 1 : 0));
	int textLen = (int) strlen(text);
	const char *start = ((textLen > room) ? (text + (textLen - room)) : text);

	snprintf(out, len, "%s%s%s", label, start, (cursor ? "_" : ""));
}

// The name as typed so far, plus the letter still being chosen on the keypad.
static void czNameWithPreview(char *out, size_t len, char preview)
{
	snprintf(out, len, "%s%c", cz.name, preview);
}

static void czDrawList(void)
{
	static const char * const labelsTR[CZ_LIST_COUNT] =
	{
		"Yeni Kanal Olu\x96tur", "Yeni Zon Olu\x96tur", "Zonlar\x93 D\xFCzenle",
		"\xC7""a\x98r\x93 \x94\x96""areti", "APRS Ayarlar\x93", "A\xE7\x93l\x93\x96 Parolas\x93"
	};
	static const char * const labelsEN[CZ_LIST_COUNT] =
	{
		"New Channel", "New Zone", "Edit Zones", "Callsign", "APRS Settings", "Power-on Password"
	};

	displayClearBuf();
	menuDisplayTitle(CZ("TELS\x94Z KURULUMU", "RADIO SETUP"));

	for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
	{
		int mNum = czRowItem(CZ_LIST_COUNT, i);

		if (mNum == MENU_OFFSET_BEFORE_FIRST_ENTRY)
		{
			continue;
		}
		else if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		menuDisplayEntry(i, mNum, CZ(labelsTR[mNum], labelsEN[mNum]),
				-1, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
	}

	displayRender();
}

static void czDrawChannel(char preview)
{
	char row[CZ_ROW_LEN];
	char text[CZ_NAME_MAX + 2];

	displayClearBuf();
	menuDisplayTitle(CZ("YEN\x94 KANAL", "NEW CHANNEL"));

	for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
	{
		int mNum = czRowItem(CZF_COUNT, i);
		const char *label = "";

		if (mNum == MENU_OFFSET_BEFORE_FIRST_ENTRY)
		{
			continue;
		}
		else if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		switch (mNum)
		{
			case CZF_NAME:
				label = CZ("Ad:", "Name:");
				czNameWithPreview(text, sizeof(text), preview);
				czFormatText(row, sizeof(row), label, text,
						((mNum == menuDataGlobal.currentItemIndex) && (preview == 0) && (strlen(cz.name) < CZ_NAME_MAX)));
				break;
			case CZF_RX:
				label = "RX:";
				czFormatFreq(row, sizeof(row), label, cz.rxFreq, ((cz.freqLen > 0) && (menuDataGlobal.currentItemIndex == CZF_RX)));
				break;
			case CZF_TX:
				label = "TX:";
				czFormatFreq(row, sizeof(row), label, cz.txFreq, ((cz.freqLen > 0) && (menuDataGlobal.currentItemIndex == CZF_TX)));
				break;
			case CZF_RX_TONE:
				label = CZ("RX Ton:", "RX Tone:");
				czFormatTone(row, sizeof(row), label, cz.rxTone);
				break;
			case CZF_TX_TONE:
				label = CZ("TX Ton:", "TX Tone:");
				czFormatTone(row, sizeof(row), label, cz.txTone);
				break;
			case CZF_BANDWIDTH:
				label = CZ("Bant:", "BW:");
				snprintf(row, sizeof(row), "%s%s", label, ((cz.wide != 0) ? "25kHz" : "12.5kHz"));
				break;
			case CZF_ZONE:
				label = CZ("Zon:", "Zone:");
				czZoneName(cz.zoneSel, text);
				czFormatText(row, sizeof(row), label, text, false);
				break;
			case CZF_SAVE:
				snprintf(row, sizeof(row), "%s", CZ("   * KAYDET *", "   * SAVE *"));
				break;
		}

		menuDisplayEntry(i, mNum, row, ((label[0] != 0) ? (int32_t) strlen(label) : 0),
				THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
	}

	displayRender();
}

static void czDrawZone(char preview)
{
	char row[CZ_ROW_LEN];
	char text[CZ_NAME_MAX + 2];

	displayClearBuf();
	menuDisplayTitle((cz.renaming != 0) ? CZ("ZON ADI", "ZONE NAME") : CZ("YEN\x94 ZON", "NEW ZONE"));

	displayPrintAt(4, 22, CZ("Zon ad\x93:", "Zone name:"), FONT_SIZE_3);

	czNameWithPreview(text, sizeof(text), preview);
	czFormatText(row, sizeof(row), "", text, ((preview == 0) && (strlen(cz.name) < CZ_NAME_MAX)));
	displayPrintAt(4, 42, row, FONT_SIZE_3);

	snprintf(row, sizeof(row), CZ("Zon say\x93s\x93: %d", "Zones: %d"), (czZoneCount() - 1));
	displayPrintAt(4, 76, row, FONT_SIZE_1);
	displayPrintAt(4, 94, ((cz.renaming != 0) ? CZ("Ye\x96il: Kaydet", "Green: save") : CZ("Ye\x96il: Olu\x96tur", "Green: create")), FONT_SIZE_1);
	displayPrintAt(4, 106, CZ("K\x93rm\x93z\x93: Sil / Geri", "Red: delete / back"), FONT_SIZE_1);

	displayRender();
}

// The password screens: the current one is asked for hidden; a new one is shown
// as typed, since it is typed twice and a mistake would otherwise go unseen.
static void czDrawPin(void)
{
	char typed[CZ_PIN_MAX + 2];
	bool isNew = ((cz.mode == CZ_MODE_PW_NEW) || (cz.mode == CZ_MODE_PW_AGAIN));
	int i;

	if (isNew && (cz.pinLen > 0))
	{
		snprintf(typed, sizeof(typed), "%0*ld", cz.pinLen, (long) cz.pinTyped);
		i = cz.pinLen;
	}
	else
	{
		for (i = 0; i < cz.pinLen; i++)
		{
			typed[i] = '*';
		}
	}
	if (i < CZ_PIN_MAX)
	{
		typed[i++] = '_';
	}
	typed[i] = 0;

	displayClearBuf();

	if (cz.mode == CZ_MODE_PW_NEW)
	{
		menuDisplayTitle(CZ("YEN\x94 PAROLA", "NEW PASSWORD"));
		displayPrintCentered(26, CZ("Yeni parola", "New password"), FONT_SIZE_3);
		displayPrintAt(4, 82, CZ("0-6 rakam. Bo\x96 b\x93rak\x93rsan", "0-6 digits. Leave it empty"), FONT_SIZE_1);
		displayPrintAt(4, 92, CZ("parola kald\x93r\x93l\x93r.", "to remove the password."), FONT_SIZE_1);
	}
	else if (cz.mode == CZ_MODE_PW_AGAIN)
	{
		menuDisplayTitle(CZ("YEN\x94 PAROLA", "NEW PASSWORD"));
		displayPrintCentered(26, CZ("Tekrar yaz", "Type it again"), FONT_SIZE_3);
	}
	else
	{
		menuDisplayTitle(CZ("PAROLA", "PASSWORD"));
		displayPrintCentered(26, CZ("Parolay\x93 girin", "Enter password"), FONT_SIZE_3);
	}

	displayPrintCentered(50, typed, FONT_SIZE_3);
	displayPrintAt(4, 106, CZ("Ye\x96il: Tamam  K\x93rm\x93z\x93: Sil", "Green: OK  Red: delete"), FONT_SIZE_1);
	displayRender();
}

// Drawn the way a selected menu row is: the selection colour filled in behind
// the text, which is then printed inverted.
static void czDrawButton(int16_t x, const char *text, bool selected)
{
	const int16_t y = 96;
	const int16_t w = 60;
	const int16_t h = 20;

	if (selected)
	{
		displayThemeApply(THEME_ITEM_BG_MENU_ITEM_SELECTED, THEME_ITEM_BG);
		displayFillRoundRect(x, y, w, h, 3, true);
	}
	else
	{
		displayThemeApply(THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_BG);
		displayDrawRoundRect(x, y, w, h, 3, true);
	}

	displayThemeApply(THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_BG);
	displayPrintCore((x + ((w - ((int16_t) strlen(text) * 8)) / 2)), (y + 2), text, FONT_SIZE_3, TEXT_ALIGN_LEFT, selected);
	displayThemeResetToDefault();
}

// The yes/no question, for a channel or a 5T contact. Starts on "no".
static void czDrawConfirmBox(const char *title, const char *name, const char *line1, const char *line2, const char *note)
{
	displayClearBuf();
	menuDisplayTitle(title);
	displayPrintCentered(24, name, FONT_SIZE_3);
	displayPrintCentered(46, line1, FONT_SIZE_3);
	displayPrintCentered(62, line2, FONT_SIZE_3);
	if (note != NULL)
	{
		displayPrintCentered(84, note, FONT_SIZE_1);
	}
	czDrawButton(14, CZ("EVET", "YES"), (cz.confirmYes != 0));
	czDrawButton(86, CZ("HAYIR", "NO"), (cz.confirmYes == 0));
	displayRender();
}

static void czDrawConfirm(void)
{
	struct_codeplugChannel_t channel;
	char name[CZ_NAME_MAX + 1];

	codeplugChannelGetDataForIndex(cz.deleteChannel, &channel);
	codeplugUtilConvertBufToString(channel.name, name, 16);

	czDrawConfirmBox(CZ("KANALI S\x94L", "DELETE CHANNEL"), name, CZ("Kanal\x93 silmek", "Delete this"), CZ("istiyor musun?", "channel?"), NULL);
}

// ---------------------------------------------------------------------------
// 5T Contacts: the codeplug's DTMF contacts, edited on the radio. A contact's
// name is what a received 5-Tone or DTMF code with the contact's code is shown
// as (codeplugDTMFContactFindNameForCode), and the existing DTMF contact list
// sends from the same contacts. Reached from Menu > Contacts.
// ---------------------------------------------------------------------------
static int czFtEditRows(void)
{
	return ((cz.ftIndex != 0) ? CZE_COUNT : CZE_DELETE); // no delete row for a new contact
}

static void czDrawFtList(void)
{
	char row[CZ_ROW_LEN];
	int count = (codeplugDTMFContactsGetCount() + 1); // row 0 is "new contact"

	displayClearBuf();
	menuDisplayTitle(CZ("5T K\x94\x95\x94LER\x94", "5T CONTACTS"));

	for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
	{
		int mNum = czRowItem(count, i);
		int32_t optStart = -1;

		if (mNum == MENU_OFFSET_BEFORE_FIRST_ENTRY)
		{
			continue;
		}
		else if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		if (mNum == 0)
		{
			snprintf(row, sizeof(row), "%s", CZ("+ Yeni Ki\x96i", "+ New Contact"));
		}
		else
		{
			// "name code", the name cut short if the two do not fit, and the code
			// in the value colour. The name is held to 15 so the split point stays
			// inside menuDisplayEntry's 17-byte buffer.
			struct_codeplugDTMFContact_t contact;
			char name[CZ_NAME_MAX + 1];
			char code[CZ_NAME_MAX + 1];
			int nameRoom;

			codeplugDTMFContactGetDataForNumber(mNum, &contact);
			codeplugUtilConvertBufToString(contact.name, name, 16);
			codeplugDTMFContactCodeToString(contact.code, code);

			nameRoom = (CZ_ROW_CHARS - 1 - (int) strlen(code));
			nameRoom = ((nameRoom < 6) ? 6 : ((nameRoom > 15) ? 15 : nameRoom));

			snprintf(row, sizeof(row), "%.*s", nameRoom, name);
			optStart = (int32_t) (strlen(row) + 1);
			snprintf((row + strlen(row)), (sizeof(row) - strlen(row)), " %.*s", (CZ_ROW_CHARS - (int) optStart), code);
		}

		menuDisplayEntry(i, mNum, row, optStart, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
	}

	displayRender();
}

static void czDrawFtEdit(char preview)
{
	char row[CZ_ROW_LEN];
	char text[CZ_NAME_MAX + 2];
	int rows = czFtEditRows();

	displayClearBuf();
	menuDisplayTitle((cz.ftIndex != 0) ? CZ("K\x94\x95\x94 D\xDCZENLE", "EDIT CONTACT") : CZ("YEN\x94 K\x94\x95\x94", "NEW CONTACT"));

	for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
	{
		int mNum = czRowItem(rows, i);
		const char *label = "";

		if (mNum == MENU_OFFSET_BEFORE_FIRST_ENTRY)
		{
			continue;
		}
		else if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		switch (mNum)
		{
			case CZE_NAME:
				label = CZ("Ad:", "Name:");
				czNameWithPreview(text, sizeof(text), preview);
				czFormatText(row, sizeof(row), label, text,
						((mNum == menuDataGlobal.currentItemIndex) && (preview == 0) && (strlen(cz.name) < CZ_NAME_MAX)));
				break;
			case CZE_CODE:
				label = CZ("Kod:", "Code:");
				czFormatText(row, sizeof(row), label, cz.code,
						((mNum == menuDataGlobal.currentItemIndex) && (strlen(cz.code) < CZ_NAME_MAX)));
				break;
			case CZE_SAVE:
				snprintf(row, sizeof(row), "%s", CZ("   * KAYDET *", "   * SAVE *"));
				break;
			case CZE_DELETE:
				snprintf(row, sizeof(row), "%s", CZ("   * S\x94L *", "   * DELETE *"));
				break;
		}

		menuDisplayEntry(i, mNum, row, ((label[0] != 0) ? (int32_t) strlen(label) : 0),
				THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
	}

	displayRender();
}

// ---------------------------------------------------------------------------
// Edit zones: pick a zone, then rename it, choose its channels, or delete it.
// ---------------------------------------------------------------------------

// Channels that exist, counted and looked up in index order. Only bit tests on
// the in-use cache, so cheap enough to do per row.
static int czChannelCount(void)
{
	int n = 0;

	for (int i = CODEPLUG_CHANNELS_MIN; i <= CODEPLUG_CHANNELS_MAX; i++)
	{
		if (codeplugAllChannelsIndexIsInUse(i))
		{
			n++;
		}
	}
	return n;
}

static int czChannelAt(int item)
{
	for (int i = CODEPLUG_CHANNELS_MIN; i <= CODEPLUG_CHANNELS_MAX; i++)
	{
		if (codeplugAllChannelsIndexIsInUse(i))
		{
			if (item == 0)
			{
				return i;
			}
			item--;
		}
	}
	return 0;
}

static bool czZoneHas(struct_codeplugZone_t *zone, int channelIndex)
{
	for (int i = 0; i < zone->NOT_IN_CODEPLUGDATA_numChannelsInZone; i++)
	{
		if (zone->channels[i] == channelIndex)
		{
			return true;
		}
	}
	return false;
}

static void czDrawZonePick(void)
{
	char name[CZ_NAME_MAX + 1];
	int count = (czZoneCount() - 1); // real zones; All Channels cannot be edited

	displayClearBuf();
	menuDisplayTitle(CZ("ZONLAR", "ZONES"));

	for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
	{
		int mNum = czRowItem(count, i);

		if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		czZoneName(mNum, name);
		menuDisplayEntry(i, mNum, name, -1, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
	}

	displayRender();
}

static void czDrawZoneMenu(void)
{
	static const char * const labelsTR[CZM_COUNT] = { "Ad\x93n\x93 De\x98i\x96tir", "Kanallar", "Zonu Sil" };
	static const char * const labelsEN[CZM_COUNT] = { "Rename", "Channels", "Delete Zone" };
	char name[CZ_NAME_MAX + 1];

	czZoneName(cz.zoneEdit, name);
	displayClearBuf();
	menuDisplayTitle(name);

	for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
	{
		int mNum = czRowItem(CZM_COUNT, i);

		if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		menuDisplayEntry(i, mNum, CZ(labelsTR[mNum], labelsEN[mNum]), -1, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
	}

	displayRender();
}

// Every channel, "[*]" and in the value colour if it is in the zone. Green
// adds or removes the one under the cursor.
static void czDrawZoneChannels(void)
{
	struct_codeplugZone_t zone;
	struct_codeplugChannel_t channel;
	char row[CZ_ROW_LEN];
	char name[CZ_NAME_MAX + 1];
	int count = czChannelCount();

	codeplugZoneGetDataForNumber(cz.zoneEdit, &zone);
	codeplugUtilConvertBufToString(zone.name, name, 16);

	displayClearBuf();
	menuDisplayTitle(name);

	if (count == 0)
	{
		displayPrintCentered(50, CZ("Kanal yok", "No channels"), FONT_SIZE_3);
	}

	for (int i = MENU_START_ITERATION_VALUE; (i <= MENU_END_ITERATION_VALUE) && (count > 0); i++)
	{
		int mNum = czRowItem(count, i);
		int ch;
		bool inZone;

		if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		ch = czChannelAt(mNum);
		inZone = czZoneHas(&zone, ch);
		codeplugChannelGetDataWithOffsetAndLengthForIndex(ch, &channel, 0, 16);
		codeplugUtilConvertBufToString(channel.name, name, 16);
		snprintf(row, sizeof(row), "%s %.15s", (inZone ? "[*]" : "[ ]"), name);

		menuDisplayEntry(i, mNum, row, (inZone ? -1 : 0), THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
	}

	displayRender();
}

// ---------------------------------------------------------------------------
// APRS settings: the codeplug's APRS configs, which a channel picks in Channel
// Details. Only what the beacon puts on the air is edited here; the position
// source, ambiguity and 300 baud bits of a config are left as they are.
// ---------------------------------------------------------------------------

// The symbols offered, in the order the knob steps through them. A config with
// some other symbol shows it as "?" plus its characters until it is changed.
typedef struct
{
	char        table; // '/' primary, '\\' alternate
	char        symbol;
	const char *nameTR;
	const char *nameEN;
} czSymbol_t;

static const czSymbol_t czSymbols[] =
{
	{ '/', '[',  "Yaya",          "Person"     },
	{ '/', '>',  "Araba",         "Car"        },
	{ '/', '-',  "Ev",            "House"      },
	{ '/', 'y',  "Ev+Anten",      "Yagi house" },
	{ '/', '<',  "Motosiklet",    "Motorcycle" },
	{ '/', 'b',  "Bisiklet",      "Bicycle"    },
	{ '/', 'k',  "Kamyon",        "Truck"      },
	{ '/', 'j',  "Jeep",          "Jeep"       },
	{ '/', 'U',  "Otob\xFCs",     "Bus"        },
	{ '/', 'R',  "Karavan",       "RV"         },
	{ '/', 's',  "Gemi",          "Ship"       },
	{ '/', 'Y',  "Yat",           "Yacht"      },
	{ '/', '\'', "U\xE7""ak",     "Aircraft"   },
	{ '/', 'O',  "Balon",         "Balloon"    },
	{ '/', 'r',  "R\xF6le",       "Repeater"   },
	{ '/', '#',  "Digi",          "Digi"       },
	{ '\\', '&', "I-Gate",        "I-Gate"     },
	{ '/', '_',  "Hava ist.",     "Weather"    },
	{ '/', '$',  "Telefon",       "Phone"      },
};
#define CZ_SYMBOL_COUNT ((int) (sizeof(czSymbols) / sizeof(czSymbols[0])))

static int czSymbolFind(void)
{
	char table = ((cz.aprs.iconTable == 0) ? '/' : '\\');
	char symbol = (char) (cz.aprs.iconIndex + '!');

	for (int i = 0; i < CZ_SYMBOL_COUNT; i++)
	{
		if ((czSymbols[i].table == table) && (czSymbols[i].symbol == symbol))
		{
			return i;
		}
	}
	return -1;
}

// Codeplug text fields are fixed-length, padded with 0xFF or 0: as a C string.
static void czFieldToString(const char *field, int len, char *out)
{
	int i;

	for (i = 0; (i < len) && (field[i] != 0) && ((uint8_t) field[i] != 0xFF); i++)
	{
		out[i] = field[i];
	}
	out[i] = 0;
}

// A path as typed and shown: "WIDE1-1", or "WIDE1" when its SSID is 0.
static void czPathToString(int p, char *out) // out: 10 bytes
{
	char name[7];

	czFieldToString(cz.aprs.paths[p].name, 6, name);
	if ((name[0] != 0) && (cz.aprs.paths[p].SSID > 0))
	{
		snprintf(out, 10, "%s-%u", name, (unsigned) cz.aprs.paths[p].SSID);
	}
	else
	{
		snprintf(out, 10, "%s", name);
	}
}

// Positions. The radio's own (nonVolatileSettings.locationLat/Lon, kept up to
// date by the GPS) is 32-bit fixed point: sign in bit 31, whole degrees in bits
// 23-30, the fraction as a decimal number of 1/100000ths in the low 23 bits. An
// APRS config holds 24 bits: sign in bit 23, degrees in bits 15-22, the fraction
// in 1/10000ths in the low 15 bits (aprs.c reads it with latLongFixed24ToDouble).
// Converted with integers only: latLongDoubleToFixed24() in uiUtilities.c drops
// the fraction and shifts the degrees into the wrong bits, so it is not used.
static uint32_t czFixed32ToFixed24(uint32_t v)
{
	uint32_t degrees = ((v & 0x7FFFFFFFU) >> 23);
	uint32_t fraction = (((v & 0x7FFFFFU) + 5U) / 10U); // 1/100000ths to 1/10000ths, rounded

	if (fraction > 9999U)
	{
		fraction = 0U;
		degrees++;
	}

	return (((degrees & 0xFFU) << 15) | fraction | (((v & 0x80000000U) != 0U) ? 0x800000U : 0U));
}

static uint32_t czAprsGetFixed24(const char *bytes)
{
	return ((uint32_t) (uint8_t) bytes[0] | ((uint32_t) (uint8_t) bytes[1] << 8) | ((uint32_t) (uint8_t) bytes[2] << 16));
}

static void czAprsSetFixed24(char *bytes, uint32_t v)
{
	bytes[0] = (char) (v & 0xFFU);
	bytes[1] = (char) ((v >> 8) & 0xFFU);
	bytes[2] = (char) ((v >> 16) & 0xFFU);
}

// "41.00" - two decimals is all a menu row has room for; the stored value keeps four.
static void czFormatFixed24(char *out, size_t len, uint32_t v)
{
	snprintf(out, len, "%s%u.%02u", (((v & 0x800000U) != 0U) ? "-" : ""), (unsigned) ((v & 0x7FFFFFU) >> 15), (unsigned) ((v & 0x7FFFU) / 100U));
}

// Takes the radio's current position as the config's fixed one. False if the
// radio has never had one (no GPS fix since the settings were reset).
static bool czAprsTakePosition(void)
{
	if (nonVolatileSettings.locationLat == SETTINGS_UNITIALISED_LOCATION_LAT)
	{
		return false;
	}

	czAprsSetFixed24(cz.aprs.latitude, czFixed32ToFixed24(nonVolatileSettings.locationLat));
	czAprsSetFixed24(cz.aprs.longitude, czFixed32ToFixed24(nonVolatileSettings.locationLon));
	cz.aprs.flags |= CZ_APRS_FLAG_FIXED_POSITION;
	return true;
}

// List rows: "+ new" first while there is room, then the configs by name.
static bool czAprsHasNewRow(void)
{
	return (codeplugAPRSConfigGetCount() < CZ_APRS_MAX);
}

// The form's rows: a config already in the codeplug also has "delete".
static int czAprsEditRows(void)
{
	return ((cz.aprsIndex != 0) ? CZA_COUNT : (CZA_COUNT - 1));
}

static int czAprsRowCount(void)
{
	return (codeplugAPRSConfigGetCount() + (czAprsHasNewRow() ? 1 : 0));
}

static void czDrawAprsList(void)
{
	codeplugAPRS_Config_t config;
	char row[CZ_ROW_LEN];
	char name[9];
	char call[9];
	int count = czAprsRowCount();

	codeplugGetRadioName(call);
	call[6] = 0; // APRS sends the first six

	displayClearBuf();
	menuDisplayTitle(CZ("APRS AYARLARI", "APRS SETTINGS"));

	for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
	{
		int mNum = czRowItem(count, i);
		int32_t optStart = -1;

		if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		if (czAprsHasNewRow() && (mNum == 0))
		{
			snprintf(row, sizeof(row), "%s", CZ("+ Yeni APRS Ayar\x93", "+ New APRS Setting"));
		}
		else
		{
			codeplugAPRSGetDataForIndex((czAprsHasNewRow() ? mNum : (mNum + 1)), &config);
			czFieldToString(config.name, 8, name);
			snprintf(row, sizeof(row), "%s", name);
			optStart = (int32_t) (strlen(row) + 1);
			snprintf((row + strlen(row)), (sizeof(row) - strlen(row)), " %s-%u", call, (unsigned) config.senderSSID);
		}

		menuDisplayEntry(i, mNum, row, optStart, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
	}

	displayRender();
}

static void czDrawAprsEdit(void)
{
	char row[CZ_ROW_LEN];
	char text[CZ_TEXT_MAX + 2];

	displayClearBuf();
	menuDisplayTitle((cz.aprsIndex != 0) ? CZ("APRS AYARI", "APRS SETTING") : CZ("YEN\x94 APRS AYARI", "NEW APRS SETTING"));

	for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
	{
		int mNum = czRowItem(czAprsEditRows(), i);
		const char *label = "";

		if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		switch (mNum)
		{
			case CZA_NAME:
				label = CZ("Ad:", "Name:");
				czFieldToString(cz.aprs.name, 8, text);
				snprintf(row, sizeof(row), "%s%s", label, text);
				break;
			case CZA_SSID:
				label = "SSID:";
				codeplugGetRadioName(text);
				text[6] = 0;
				snprintf(row, sizeof(row), "%s%s-%u", label, text, (unsigned) cz.aprs.senderSSID);
				break;
			case CZA_PATH1:
			case CZA_PATH2:
				label = ((mNum == CZA_PATH1) ? CZ("Yol 1:", "Path 1:") : CZ("Yol 2:", "Path 2:"));
				czPathToString((mNum - CZA_PATH1), text);
				snprintf(row, sizeof(row), "%s%s", label, ((text[0] != 0) ? text : CZ("Yok", "None")));
				break;
			case CZA_SYMBOL:
			{
				int s = czSymbolFind();

				label = CZ("Simge:", "Icon:");
				snprintf(row, sizeof(row), "%s%s %c%c", label, ((s >= 0) ? CZ(czSymbols[s].nameTR, czSymbols[s].nameEN) : "?"),
						((cz.aprs.iconTable == 0) ? '/' : '\\'), (char) (cz.aprs.iconIndex + '!'));
			}
				break;
			case CZA_COMMENT:
				label = CZ("Yorum:", "Comment:");
				czFieldToString(cz.aprs.comment, CZ_TEXT_MAX, text);
				czFormatText(row, sizeof(row), label, text, false);
				break;
			case CZA_POSITION:
				label = CZ("Konum:", "Pos:");
				if ((cz.aprs.flags & CZ_APRS_FLAG_FIXED_POSITION) != 0)
				{
					char lat[10];
					char lon[10];

					czFormatFixed24(lat, sizeof(lat), czAprsGetFixed24(cz.aprs.latitude));
					czFormatFixed24(lon, sizeof(lon), czAprsGetFixed24(cz.aprs.longitude));
					snprintf(row, sizeof(row), "%s%s,%s", label, lat, lon);
				}
				else
				{
					snprintf(row, sizeof(row), "%sGPS", label);
				}
				break;
			case CZA_SAVE:
				snprintf(row, sizeof(row), "%s", CZ("   * KAYDET *", "   * SAVE *"));
				break;
			case CZA_DELETE:
				snprintf(row, sizeof(row), "%s", CZ("   * S\x94L *", "   * DELETE *"));
				break;
		}

		menuDisplayEntry(i, mNum, row, ((label[0] != 0) ? (int32_t) strlen(label) : 0),
				THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
	}

	displayRender();
}

static int czTextMax(void)
{
	switch (cz.textTarget)
	{
		case CZT_APRS_NAME:
		case CZT_CALLSIGN:
			return 8;
		case CZT_PATH1:
		case CZT_PATH2:
			return 9; // WIDE1-15
		default:
			return CZ_TEXT_MAX;
	}
}

// One line of text, typed on the keypad. Shown over two lines in the large font
// when it is longer than one.
static void czDrawText(char preview)
{
	static const char * const titlesTR[] = { "APRS ADI", "YOL 1", "YOL 2", "YORUM", "\xC7""a\x98r\x93 \x94\x96""areti" };
	static const char * const titlesEN[] = { "APRS NAME", "PATH 1", "PATH 2", "COMMENT", "CALLSIGN" };
	static const char * const hintsTR[]  = { "En fazla 8 karakter", "\xD6rnek: WIDE1-1", "\xD6rnek: WIDE2-1, bo\x96 = yok",
			"En fazla 23 karakter", "Harf ve rakam, en fazla 8" };
	static const char * const hintsEN[]  = { "Up to 8 characters", "Example: WIDE1-1", "e.g. WIDE2-1, empty=none",
			"Up to 23 characters", "Letters/digits, up to 8" }; // 26 characters fit the small font
	char shown[CZ_TEXT_MAX + 3];
	int len;

	snprintf(shown, sizeof(shown), "%s%c", cz.text, preview);
	len = (int) strlen(shown);
	if ((preview == 0) && ((int) strlen(cz.text) < czTextMax()))
	{
		shown[len++] = '_';
		shown[len] = 0;
	}

	displayClearBuf();
	menuDisplayTitle(CZ(titlesTR[cz.textTarget], titlesEN[cz.textTarget]));

	if (len > 18)
	{
		char first[19];

		memcpy(first, shown, 18);
		first[18] = 0;
		displayPrintAt(4, 26, first, FONT_SIZE_3);
		displayPrintAt(4, 44, (shown + 18), FONT_SIZE_3);
	}
	else
	{
		displayPrintAt(4, 34, shown, FONT_SIZE_3);
	}

	displayPrintAt(4, 80, CZ(hintsTR[cz.textTarget], hintsEN[cz.textTarget]), FONT_SIZE_1);
	displayPrintAt(4, 94, CZ("Ye\x96il: Tamam", "Green: OK"), FONT_SIZE_1);
	displayPrintAt(4, 106, CZ("K\x93rm\x93z\x93: Sil / Geri", "Red: delete / back"), FONT_SIZE_1);
	displayRender();
}

static void czRedraw(char preview)
{
	switch (cz.mode)
	{
		case CZ_MODE_CHANNEL:
			czDrawChannel(preview);
			break;
		case CZ_MODE_ZONE:
			czDrawZone(preview);
			break;
		case CZ_MODE_PIN:
			czDrawPin();
			break;
		case CZ_MODE_CONFIRM:
			czDrawConfirm();
			break;
		case CZ_MODE_FT_LIST:
			czDrawFtList();
			break;
		case CZ_MODE_FT_EDIT:
			czDrawFtEdit(preview);
			break;
		case CZ_MODE_FT_DELETE:
			czDrawConfirmBox(CZ("K\x94\x95\x94Y\x94 S\x94L", "DELETE CONTACT"), cz.name,
					CZ("Ki\x96iyi silmek", "Delete this"), CZ("istiyor musun?", "contact?"), NULL);
			break;
		case CZ_MODE_ZONE_PICK:
			czDrawZonePick();
			break;
		case CZ_MODE_ZONE_MENU:
			czDrawZoneMenu();
			break;
		case CZ_MODE_ZONE_CHANNELS:
			czDrawZoneChannels();
			break;
		case CZ_MODE_ZONE_DELETE:
		{
			char name[CZ_NAME_MAX + 1];

			czZoneName(cz.zoneEdit, name);
			czDrawConfirmBox(CZ("ZONU S\x94L", "DELETE ZONE"), name, CZ("Zonu silmek", "Delete this"), CZ("istiyor musun?", "zone?"),
					CZ("Kanallar silinmez", "Its channels are kept"));
		}
			break;
		case CZ_MODE_PW_NEW:
		case CZ_MODE_PW_AGAIN:
			czDrawPin();
			break;
		case CZ_MODE_APRS_LIST:
			czDrawAprsList();
			break;
		case CZ_MODE_APRS_EDIT:
			czDrawAprsEdit();
			break;
		case CZ_MODE_TEXT:
			czDrawText(preview);
			break;
		case CZ_MODE_APRS_DELETE:
		{
			char name[9];

			czFieldToString(cz.aprs.name, 8, name);
			czDrawConfirmBox(CZ("APRS S\x94L", "DELETE APRS"), name, CZ("Ayar\x93 silmek", "Delete this"), CZ("istiyor musun?", "setting?"),
					CZ("Kullanan kanalda APRS kapan\x93r", "Channels using it: no APRS"));
		}
			break;
		default:
			czDrawList();
			break;
	}
}

// ---------------------------------------------------------------------------
// Getting in and out
// ---------------------------------------------------------------------------

// Called from Channel Details. The request waits in CCM RAM for the screen to
// open, because opening it is what decides which mode it starts in.
void menuChannelZoneStartDelete(int channelIndex)
{
	czEnsureInit();
	cz.deleteChannel = (int16_t) channelIndex;
	cz.magic = CZ_DELETE_MAGIC;
	menuSystemPushNewMenu(UI_CHANNEL_ZONE);
}

// Runs whenever this screen is left, whichever way. If the codeplug was written,
// the channel screen is told to read its zone and channel again: its copies were
// taken before the change.
static void czExitCallback(void *data)
{
	(void) data;

	keypadAlphaEnable = false;

	if (cz.dirty != 0)
	{
		cz.dirty = 0;
		channelScreenChannelData.rxFreq = 0;
	}
}

// Leaves the screen. After a change it goes straight to the channel screen, so
// that it re-reads the codeplug at once, rather than to the main menu.
static void czLeave(void)
{
	keypadAlphaEnable = false;

	if (cz.dirty != 0)
	{
		menuSystemPopAllAndDisplayRootMenu();
	}
	else
	{
		menuSystemPopPreviousMenu();
	}
}

static void czBackToList(int item)
{
	menuDataGlobal.currentItemIndex = item;
	menuDataGlobal.numItems = CZ_LIST_COUNT;
	czSetMode(CZ_MODE_LIST);
}

static void czStartChannel(void)
{
	int zones = czZoneCount();

	memset(cz.name, 0, sizeof(cz.name));
	cz.rxFreq = 0;
	cz.txFreq = 0;
	cz.rxTone = CODEPLUG_CSS_TONE_NONE;
	cz.txTone = CODEPLUG_CSS_TONE_NONE;
	cz.wide = 1;

	// A zone just made here is the likely home for the channel; otherwise the
	// zone the radio is on.
	if ((cz.newZone >= 0) && (cz.newZone < (zones - 1)))
	{
		cz.zoneSel = cz.newZone;
	}
	else if ((nonVolatileSettings.currentZone >= 0) && (nonVolatileSettings.currentZone < zones))
	{
		cz.zoneSel = nonVolatileSettings.currentZone;
	}
	else
	{
		cz.zoneSel = (int16_t) (zones - 1);
	}

	menuDataGlobal.currentItemIndex = CZF_NAME;
	menuDataGlobal.numItems = CZF_COUNT;
	czSetMode(CZ_MODE_CHANNEL);
}

static void czStartZone(void)
{
	memset(cz.name, 0, sizeof(cz.name));
	cz.renaming = 0;
	czSetMode(CZ_MODE_ZONE);
}

// Edit zones, the list of zones. With none to edit it says so and stays put.
static bool czStartZonePick(int item)
{
	int count = (czZoneCount() - 1);

	if (count <= 0)
	{
		czNotify("Zon yok", "No zones");
		return false;
	}

	menuDataGlobal.currentItemIndex = (((item >= 0) && (item < count)) ? item : 0);
	menuDataGlobal.numItems = count;
	czSetMode(CZ_MODE_ZONE_PICK);
	return true;
}

static void czStartZoneMenu(int item)
{
	menuDataGlobal.currentItemIndex = item;
	menuDataGlobal.numItems = CZM_COUNT;
	czSetMode(CZ_MODE_ZONE_MENU);
}

static void czStartAprsList(int item)
{
	int count = czAprsRowCount();

	menuDataGlobal.currentItemIndex = (((item >= 0) && (item < count)) ? item : 0);
	menuDataGlobal.numItems = count;
	czSetMode(CZ_MODE_APRS_LIST);
}

// index: 1..8 for a config that exists, 0 for a new one. The config is edited
// in CCM RAM and only written on save - every write erases a flash sector.
static void czStartAprsEdit(int index)
{
	cz.aprsIndex = (uint8_t) index;

	if ((index == 0) || (codeplugAPRSGetDataForIndex(index, &cz.aprs) == false))
	{
		codeplugAPRS_Config_t first;
		char name[9];

		// A handheld's usual settings: SSID 7, WIDE1-1,WIDE2-1, a person.
		memset(&cz.aprs, 0, sizeof(cz.aprs));
		snprintf(name, sizeof(name), "APRS%d", (codeplugAPRSConfigGetCount() + 1));
		codeplugUtilConvertStringToBuf(name, cz.aprs.name, 8);
		cz.aprs.senderSSID = 7;
		memcpy(cz.aprs.paths[0].name, "WIDE1", 5);
		cz.aprs.paths[0].SSID = 1;
		memcpy(cz.aprs.paths[1].name, "WIDE2", 5);
		cz.aprs.paths[1].SSID = 1;
		cz.aprs.iconTable = 0;
		cz.aprs.iconIndex = (uint8_t) ('[' - '!');
		// The same format marker as the configs already there, so whichever CPS
		// wrote those reads this one the same way. 0 gets the default on save.
		cz.aprs.magicVer = (codeplugAPRSGetDataForIndex(1, &first) ? first.magicVer : 0);
		cz.aprsIndex = 0;
	}

	menuDataGlobal.currentItemIndex = CZA_NAME;
	menuDataGlobal.numItems = czAprsEditRows();
	czSetMode(CZ_MODE_APRS_EDIT);
}

// Opens the text editor on one field, with what it holds now.
static void czStartText(uint8_t target)
{
	cz.textTarget = target;
	memset(cz.text, 0, sizeof(cz.text));

	switch (target)
	{
		case CZT_APRS_NAME:
			czFieldToString(cz.aprs.name, 8, cz.text);
			break;
		case CZT_PATH1:
		case CZT_PATH2:
			czPathToString((target - CZT_PATH1), cz.text);
			break;
		case CZT_COMMENT:
			czFieldToString(cz.aprs.comment, CZ_TEXT_MAX, cz.text);
			break;
		case CZT_CALLSIGN:
		{
			char call[9];

			codeplugGetRadioName(call);
			snprintf(cz.text, sizeof(cz.text), "%s", call);
		}
			break;
	}

	czSetMode(CZ_MODE_TEXT);
}

// Asks for the power-on password first when the radio has one, then goes on to
// the mode next. Red on an empty password goes back to wherever next would.
static void czRequirePinThen(uint8_t next)
{
	int32_t pin = 0;

	cz.pinLen = 0;
	cz.pinTyped = 0;
	cz.confirmYes = 0;
	cz.pinNext = next;
	czSetMode((codeplugGetPasswordPin(&pin) > 0) ? CZ_MODE_PIN : next);
}

static void czStartFtList(int item)
{
	int count = (codeplugDTMFContactsGetCount() + 1);

	menuDataGlobal.currentItemIndex = (((item >= 0) && (item < count)) ? item : 0);
	menuDataGlobal.numItems = count;
	czSetMode(CZ_MODE_FT_LIST);
}

// number: the contact's place in the list (1..), or 0 for a new contact.
static void czStartFtEdit(int number)
{
	memset(cz.name, 0, sizeof(cz.name));
	memset(cz.code, 0, sizeof(cz.code));
	cz.ftIndex = 0;

	if (number > 0)
	{
		struct_codeplugDTMFContact_t contact;
		int index = codeplugDTMFContactGetDataForNumber(number, &contact);

		if (index > 0)
		{
			codeplugUtilConvertBufToString(contact.name, cz.name, 16);
			codeplugDTMFContactCodeToString(contact.code, cz.code);
			cz.ftIndex = (int16_t) index;
		}
	}

	menuDataGlobal.currentItemIndex = CZE_NAME;
	menuDataGlobal.numItems = czFtEditRows();
	czSetMode(CZ_MODE_FT_EDIT);
}

// Checked before writing: a name, a code, and no other contact with the same
// code - two names for one code could only ever show the first.
static bool czSaveFtContact(void)
{
	struct_codeplugDTMFContact_t contact;
	char code[CZ_NAME_MAX + 1];
	int count = codeplugDTMFContactsGetCount();

	if (cz.name[0] == 0)
	{
		menuDataGlobal.currentItemIndex = CZE_NAME;
		czNotify("Ad gerekli", "Name needed");
		return false;
	}

	if (cz.code[0] == 0)
	{
		menuDataGlobal.currentItemIndex = CZE_CODE;
		czNotify("Kod gerekli", "Code needed");
		return false;
	}

	for (int n = CODEPLUG_DTMF_CONTACTS_MIN; n <= count; n++)
	{
		int index = codeplugDTMFContactGetDataForNumber(n, &contact);

		if ((index > 0) && (index != cz.ftIndex))
		{
			codeplugDTMFContactCodeToString(contact.code, code);

			if (strcmp(code, cz.code) == 0)
			{
				menuDataGlobal.currentItemIndex = CZE_CODE;
				czNotify("Kod zaten var", "Code exists");
				return false;
			}
		}
	}

	if (codeplugDTMFContactWrite(cz.ftIndex, cz.name, cz.code) == 0)
	{
		czNotify(((cz.ftIndex == 0) ? "Liste dolu" : "Yazma hatas\x93"), ((cz.ftIndex == 0) ? "List full" : "Write failed"));
		return false;
	}

	return true;
}

// ---------------------------------------------------------------------------
// Saving
// ---------------------------------------------------------------------------

// Takes the typed digits as a frequency. Fewer than eight are padded with
// zeros, so 145 and green gives 145.00000. A frequency the radio cannot use is
// refused and left on screen to be corrected.
static bool czCommitFreq(void)
{
	uint32_t freq = 0;

	for (int i = 0; i < CZ_FREQ_DIGITS; i++)
	{
		freq = (freq * 10) + ((i < cz.freqLen) ? (uint32_t) (cz.freqDigits[i] - '0') : 0);
	}

	if (trxGetBandFromFrequency(freq) == FREQUENCY_OUT_OF_BAND)
	{
		czNotify("Bant d\x93\x96\x93", "Out of band");
		return false;
	}

	if (menuDataGlobal.currentItemIndex == CZF_RX)
	{
		cz.rxFreq = freq;
		if (cz.txFreq == 0)
		{
			cz.txFreq = freq; // simplex unless a TX frequency is typed as well
		}
	}
	else
	{
		cz.txFreq = freq;
	}

	cz.freqLen = 0;
	menuDataGlobal.currentItemIndex++;
	return true;
}

// Everything is checked before anything is written, so a refusal leaves the
// codeplug untouched. Then the channel, then its in-use bit, then the zone - the
// same order as VFO -> New Channel.
static bool czSaveChannel(void)
{
	struct_codeplugZone_t zone;
	struct_codeplugChannel_t channel;
	int zones = czZoneCount();
	bool intoZone = ((cz.zoneSel >= 0) && (cz.zoneSel < (zones - 1))); // the last zone is All Channels
	int index;

	if (cz.name[0] == 0)
	{
		menuDataGlobal.currentItemIndex = CZF_NAME;
		czNotify("Ad gerekli", "Name needed");
		return false;
	}

	if (cz.rxFreq == 0)
	{
		menuDataGlobal.currentItemIndex = CZF_RX;
		czNotify("RX gerekli", "RX needed");
		return false;
	}

	if (cz.txFreq == 0)
	{
		cz.txFreq = cz.rxFreq;
	}

	for (index = CODEPLUG_CHANNELS_MIN; index <= CODEPLUG_CHANNELS_MAX; index++)
	{
		if (codeplugAllChannelsIndexIsInUse(index) == false)
		{
			break;
		}
	}

	if (index > CODEPLUG_CHANNELS_MAX)
	{
		czNotify("Kanal yeri yok", "No free channel");
		return false;
	}

	if (intoZone)
	{
		if ((codeplugZoneGetDataForNumber(cz.zoneSel, &zone) == false) || (zone.NOT_IN_CODEPLUGDATA_indexNumber < 0))
		{
			czNotify("Zon okunamad\x93", "Zone read error");
			return false;
		}

		if (zone.NOT_IN_CODEPLUGDATA_numChannelsInZone >= codeplugChannelsPerZone)
		{
			menuDataGlobal.currentItemIndex = CZF_ZONE;
			czNotify("Zon dolu", "Zone is full");
			return false;
		}
	}

	// An analogue channel with everything else at its neutral value: master
	// power, master squelch, no timeout, no 5-Tone, no APRS.
	memset(&channel, 0, sizeof(channel));
	codeplugUtilConvertStringToBuf(cz.name, channel.name, 16);
	channel.rxFreq = cz.rxFreq;
	channel.txFreq = cz.txFreq;
	channel.chMode = RADIO_MODE_ANALOG;
	channel.rxTone = cz.rxTone;
	channel.txTone = cz.txTone;
	channel.txColor = 1;
	channel.flag4 = ((cz.wide != 0) ? CODEPLUG_CHANNEL_FLAG4_BW_25K : 0);
	channel.NOT_IN_CODEPLUG_CALCULATED_DISTANCE_X10 = -1;

	if (codeplugChannelSaveDataForIndex(index, &channel) == false)
	{
		czNotify("Yazma hatas\x93", "Write failed");
		return false;
	}

	codeplugAllChannelsIndexSetUsed(index);
	cz.dirty = 1;

	if (intoZone && (codeplugZoneAddChannelToZoneAndSave(index, &zone) == false))
	{
		// The channel exists - it is in All Channels - but not in the zone.
		czNotify("Zona eklenemedi", "Zone add failed");
		return false;
	}

	return true;
}

// skip: a zone number not to compare with (the one being renamed), or -1.
static bool czZoneNameExists(const char *name, int skip)
{
	char existing[CZ_NAME_MAX + 1];
	int realZones = (czZoneCount() - 1);

	for (int n = 0; n < realZones; n++)
	{
		if (n == skip)
		{
			continue;
		}

		czZoneName(n, existing);

		if (strcmp(existing, name) == 0)
		{
			return true;
		}
	}

	return false;
}

static bool czSaveZone(void)
{
	int zoneNum;

	if (cz.name[0] == 0)
	{
		czNotify("Ad gerekli", "Name needed");
		return false;
	}

	if (czZoneNameExists(cz.name, ((cz.renaming != 0) ? cz.zoneEdit : -1)))
	{
		czNotify("Ayn\x93 isim var", "Name exists");
		return false;
	}

	if (cz.renaming != 0)
	{
		if (codeplugZoneRename(cz.zoneEdit, cz.name) == false)
		{
			czNotify("Yazma hatas\x93", "Write failed");
			return false;
		}

		cz.dirty = 1; // the channel screen shows the zone's name
		return true;
	}

	zoneNum = codeplugZoneCreate(cz.name);

	if (zoneNum < 0)
	{
		czNotify("Zon yeri yok", "No zone slot");
		return false;
	}

	cz.newZone = (int16_t) zoneNum;
	cz.dirty = 1;
	return true;
}

// ---------------------------------------------------------------------------
// Keys
// ---------------------------------------------------------------------------

// Multi-tap text into cz.name. Returns true when the key was a character.
static bool czTypeName(uiEvent_t *ev)
{
	if ((ev->keys.key >= ' ') && (ev->keys.key < 127))
	{
		if (ev->keys.event & KEY_MOD_PREVIEW)
		{
			czRedraw((char) ev->keys.key);
			return true;
		}

		if (ev->keys.event & KEY_MOD_PRESS)
		{
			int len = (int) strlen(cz.name);

			if (len < CZ_NAME_MAX)
			{
				cz.name[len] = (char) ev->keys.key;
				cz.name[len + 1] = 0;
			}
			czRedraw(0);
			return true;
		}
	}

	return false;
}

static bool czDeleteLastChar(void)
{
	int len = (int) strlen(cz.name);

	if (len > 0)
	{
		cz.name[len - 1] = 0;
		return true;
	}

	return false;
}

static menuStatus_t czHandleList(uiEvent_t *ev, bool down, bool up)
{
	if (down)
	{
		menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, CZ_LIST_COUNT);
		czRedraw(0);
		return MENU_STATUS_LIST_TYPE;
	}
	else if (up)
	{
		menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, CZ_LIST_COUNT);
		czRedraw(0);
		return MENU_STATUS_LIST_TYPE;
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		switch (menuDataGlobal.currentItemIndex)
		{
			case CZ_LIST_CHANNEL:
				czStartChannel();
				break;
			case CZ_LIST_ZONE:
				czStartZone();
				break;
			case CZ_LIST_ZONE_EDIT:
				if (czStartZonePick(0) == false)
				{
					czRedraw(0);
					return MENU_STATUS_ERROR;
				}
				break;
			case CZ_LIST_CALLSIGN:
				czStartText(CZT_CALLSIGN);
				break;
			case CZ_LIST_APRS:
				czStartAprsList(0);
				break;
			case CZ_LIST_PASSWORD:
				czRequirePinThen(CZ_MODE_PW_NEW); // the current password before a new one
				break;
		}
		czRedraw(0);
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		czLeave();
	}

	return MENU_STATUS_SUCCESS;
}

static menuStatus_t czHandleChannel(uiEvent_t *ev, bool down, bool up, bool right, bool left)
{
	int field = menuDataGlobal.currentItemIndex;

	// Held red throws the form away.
	if (KEYCHECK_LONGDOWN(ev->keys, KEY_RED) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_RED) == false))
	{
		czBackToList(CZ_LIST_CHANNEL);
		czRedraw(0);
		return MENU_STATUS_SUCCESS;
	}

	if (down || up)
	{
		if (down)
		{
			menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, CZF_COUNT);
		}
		else
		{
			menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, CZF_COUNT);
		}
		czSetMode(CZ_MODE_CHANNEL); // drops a half-typed frequency, sets the keypad for the new row
		czRedraw(0);
		return MENU_STATUS_LIST_TYPE;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		if (cz.freqLen > 0)
		{
			cz.freqLen--;
		}
		else if ((field == CZF_NAME) && czDeleteLastChar())
		{
			// one character fewer
		}
		else
		{
			czBackToList(CZ_LIST_CHANNEL);
		}
		czRedraw(0);
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		menuStatus_t status = MENU_STATUS_SUCCESS;

		if (cz.freqLen > 0)
		{
			if (czCommitFreq() == false)
			{
				status = MENU_STATUS_ERROR;
			}
		}
		else if (field == CZF_SAVE)
		{
			if (czSaveChannel())
			{
				nextKeyBeepMelody = (int16_t *) MELODY_ACK_BEEP;
				czNotify("Kaydedildi", "Saved");
				czBackToList(CZ_LIST_CHANNEL);
			}
			else
			{
				status = MENU_STATUS_ERROR;
			}
		}
		else
		{
			menuDataGlobal.currentItemIndex++; // green on a row moves on to the next one
		}

		czUpdateKeypad();
		czRedraw(0);
		return status;
	}

	switch (field)
	{
		case CZF_NAME:
			czTypeName(ev);
			break;

		case CZF_RX:
		case CZF_TX:
			if (KEYCHECK_SHORTUP_NUMBER(ev->keys))
			{
				menuStatus_t status = MENU_STATUS_SUCCESS;

				if (cz.freqLen < CZ_FREQ_DIGITS)
				{
					cz.freqDigits[cz.freqLen++] = (char) ev->keys.key;

					if ((cz.freqLen == CZ_FREQ_DIGITS) && (czCommitFreq() == false))
					{
						status = MENU_STATUS_ERROR;
					}
				}

				czUpdateKeypad();
				czRedraw(0);
				return status;
			}
			break;

		case CZF_RX_TONE:
		case CZF_TX_TONE:
			if (right || left)
			{
				uint16_t *tone = ((field == CZF_RX_TONE) ? &cz.rxTone : &cz.txTone);
				CodeplugCSSTypes_t type = codeplugGetCSSType(*tone);
				uint8_t index = cssGetToneIndex(*tone, type);

				// No wrap-around: the list runs None, CTCSS, DCS, inverted DCS
				// and stops at both ends, so "none" is always back at the start.
				if (right)
				{
					cssIncrement(tone, &index, 1, &type, false, false);
				}
				else
				{
					cssDecrement(tone, &index, 1, &type, false, false);
				}
				czRedraw(0);
			}
			break;

		case CZF_BANDWIDTH:
			if (right || left)
			{
				cz.wide = ((cz.wide != 0) ? 0 : 1);
				czRedraw(0);
			}
			break;

		case CZF_ZONE:
			if (right || left)
			{
				int zones = czZoneCount();

				cz.zoneSel = (int16_t) ((cz.zoneSel + (right ? 1 : (zones - 1))) % zones);
				czRedraw(0);
			}
			break;
	}

	return MENU_STATUS_SUCCESS;
}

// Where red leaves the zone name editor for: the zone's own menu when renaming,
// the first list when making a zone.
static void czZoneEditorBack(void)
{
	if (cz.renaming != 0)
	{
		czStartZoneMenu(CZM_RENAME);
	}
	else
	{
		czBackToList(CZ_LIST_ZONE);
	}
}

static menuStatus_t czHandleZone(uiEvent_t *ev)
{
	if (KEYCHECK_LONGDOWN(ev->keys, KEY_RED) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_RED) == false))
	{
		czZoneEditorBack();
		czRedraw(0);
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		if (czDeleteLastChar() == false)
		{
			czZoneEditorBack();
		}
		czRedraw(0);
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		if (czSaveZone())
		{
			nextKeyBeepMelody = (int16_t *) MELODY_ACK_BEEP;

			if (cz.renaming != 0)
			{
				czNotify("Kaydedildi", "Saved");
				czStartZoneMenu(CZM_RENAME);
			}
			else
			{
				czNotify("Zon olu\x96turuldu", "Zone created");
				czBackToList(CZ_LIST_CHANNEL); // next step is usually a channel for it
			}
			czRedraw(0);
			return MENU_STATUS_SUCCESS;
		}
		czRedraw(0);
		return MENU_STATUS_ERROR;
	}

	czTypeName(ev);
	return MENU_STATUS_SUCCESS;
}

// Red on an empty password: back to wherever the password stood in front of.
static void czPinCancel(void)
{
	if (cz.mode == CZ_MODE_PW_AGAIN)
	{
		cz.pinLen = 0;
		cz.pinTyped = 0;
		czSetMode(CZ_MODE_PW_NEW);
	}
	else if ((cz.mode == CZ_MODE_PW_NEW) || (cz.pinNext == CZ_MODE_PW_NEW))
	{
		czBackToList(CZ_LIST_PASSWORD);
	}
	else if (cz.pinNext == CZ_MODE_ZONE_DELETE)
	{
		czStartZoneMenu(CZM_DELETE);
	}
	else
	{
		czLeave(); // channel delete: back to Channel Details, nothing deleted
	}
}

// The current password (CZ_MODE_PIN) and a new one (CZ_MODE_PW_NEW, then
// CZ_MODE_PW_AGAIN) are typed the same way; green does something different.
static menuStatus_t czHandlePin(uiEvent_t *ev)
{
	if (KEYCHECK_SHORTUP_NUMBER(ev->keys))
	{
		if (cz.pinLen < CZ_PIN_MAX)
		{
			cz.pinTyped = ((cz.pinTyped * 10) + (ev->keys.key - '0'));
			cz.pinLen++;
		}
		czRedraw(0);
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		if (cz.pinLen > 0)
		{
			cz.pinLen--;
			cz.pinTyped /= 10;
		}
		else
		{
			czPinCancel();
			if (menuSystemGetCurrentMenuNumber() != UI_CHANNEL_ZONE)
			{
				return MENU_STATUS_SUCCESS; // left the screen: another one is showing now
			}
		}
		czRedraw(0);
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		if (cz.mode == CZ_MODE_PW_NEW)
		{
			cz.pinFirst = cz.pinTyped;
			cz.pinFirstLen = cz.pinLen;
			cz.pinTyped = 0;
			cz.pinLen = 0;
			czSetMode(CZ_MODE_PW_AGAIN);
		}
		else if (cz.mode == CZ_MODE_PW_AGAIN)
		{
			bool same = ((cz.pinLen == cz.pinFirstLen) && (cz.pinTyped == cz.pinFirst));

			cz.pinTyped = 0;
			cz.pinLen = 0;

			if (same == false)
			{
				czNotify("Uyu\x96muyor", "No match");
				czSetMode(CZ_MODE_PW_NEW);
				czRedraw(0);
				return MENU_STATUS_ERROR;
			}

			if (codeplugSetPasswordPin(cz.pinFirst, cz.pinFirstLen) == false)
			{
				czNotify("Yazma hatas\x93", "Write failed");
				czBackToList(CZ_LIST_PASSWORD);
				czRedraw(0);
				return MENU_STATUS_ERROR;
			}

			nextKeyBeepMelody = (int16_t *) MELODY_ACK_BEEP;
			czNotify(((cz.pinFirstLen > 0) ? "Kaydedildi" : "Parola silindi"), ((cz.pinFirstLen > 0) ? "Saved" : "Password off"));
			czBackToList(CZ_LIST_PASSWORD);
		}
		else
		{
			int32_t pin = 0;
			int pinLength = codeplugGetPasswordPin(&pin);

			// Both the digits and how many: 0012 and 12 are different passwords.
			if ((pinLength == cz.pinLen) && (pin == cz.pinTyped))
			{
				cz.pinTyped = 0;
				cz.pinLen = 0;
				cz.confirmYes = 0;
				czSetMode(cz.pinNext);
			}
			else
			{
				cz.pinLen = 0;
				cz.pinTyped = 0;
				czNotify("Parola hatal\x93", "Wrong password");
				czRedraw(0);
				return MENU_STATUS_ERROR;
			}
		}
		czRedraw(0);
	}

	return MENU_STATUS_SUCCESS;
}

static menuStatus_t czHandleConfirm(uiEvent_t *ev, bool down, bool up, bool right, bool left)
{
	if (down || up || right || left)
	{
		cz.confirmYes = ((cz.confirmYes != 0) ? 0 : 1);
		czRedraw(0);
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED) || (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN) && (cz.confirmYes == 0)))
	{
		czLeave(); // "no": back to Channel Details
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		bool deleted = codeplugChannelDelete(cz.deleteChannel);

		// Even a failure may have changed some zones before it stopped, so the
		// channel screen re-reads everything either way.
		cz.dirty = 1;

		if (deleted)
		{
			nextKeyBeepMelody = (int16_t *) MELODY_ACK_BEEP;
			czNotify("Kanal silindi", "Channel deleted");
		}
		else
		{
			czNotify("Silinemedi", "Delete failed");
		}

		czLeave(); // to the channel screen, which now shows the next channel
		return (deleted ? MENU_STATUS_SUCCESS : MENU_STATUS_ERROR);
	}

	return MENU_STATUS_SUCCESS;
}

static menuStatus_t czHandleFtList(uiEvent_t *ev, bool down, bool up)
{
	int count = (codeplugDTMFContactsGetCount() + 1);

	if (down || up)
	{
		if (down)
		{
			menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, count);
		}
		else
		{
			menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, count);
		}
		czRedraw(0);
		return MENU_STATUS_LIST_TYPE;
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		cz.ftListItem = (int16_t) menuDataGlobal.currentItemIndex;
		czStartFtEdit(menuDataGlobal.currentItemIndex);
		czRedraw(0);
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		czLeave(); // back to the Contacts menu
	}

	return MENU_STATUS_SUCCESS;
}

static menuStatus_t czHandleFtEdit(uiEvent_t *ev, bool down, bool up, bool right, bool left)
{
	int field = menuDataGlobal.currentItemIndex;
	int rows = czFtEditRows();
	int codeLen = (int) strlen(cz.code);

	if (KEYCHECK_LONGDOWN(ev->keys, KEY_RED) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_RED) == false))
	{
		czStartFtList(cz.ftListItem); // held red: leave without saving
		czRedraw(0);
		return MENU_STATUS_SUCCESS;
	}

	if (down || up)
	{
		if (down)
		{
			menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, rows);
		}
		else
		{
			menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, rows);
		}
		czUpdateKeypad();
		czRedraw(0);
		return MENU_STATUS_LIST_TYPE;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		if ((field == CZE_NAME) && czDeleteLastChar())
		{
			// one character fewer
		}
		else if ((field == CZE_CODE) && (codeLen > 0))
		{
			cz.code[codeLen - 1] = 0;
		}
		else
		{
			czStartFtList(cz.ftListItem);
		}
		czRedraw(0);
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		menuStatus_t status = MENU_STATUS_SUCCESS;

		if (field == CZE_SAVE)
		{
			if (czSaveFtContact())
			{
				nextKeyBeepMelody = (int16_t *) MELODY_ACK_BEEP;
				czNotify("Kaydedildi", "Saved");
				czStartFtList(cz.ftListItem);
			}
			else
			{
				status = MENU_STATUS_ERROR;
			}
		}
		else if (field == CZE_DELETE)
		{
			cz.confirmYes = 0;
			czSetMode(CZ_MODE_FT_DELETE);
		}
		else
		{
			menuDataGlobal.currentItemIndex++; // on to the next row
		}

		czUpdateKeypad();
		czRedraw(0);
		return status;
	}

	if (field == CZE_NAME)
	{
		czTypeName(ev);
	}
	else if (field == CZE_CODE)
	{
		// Digits, '*' and '#' from the keypad. A-D are not on it, so the knob
		// (or left/right) steps the last character through every DTMF symbol.
		static const char symbols[] = "0123456789ABCD*#";
		char typed = 0;

		if (KEYCHECK_SHORTUP_NUMBER(ev->keys))
		{
			typed = (char) ev->keys.key;
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
		{
			typed = '*';
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
		{
			typed = '#';
		}

		if (typed != 0)
		{
			if (codeLen < CZ_NAME_MAX)
			{
				cz.code[codeLen] = typed;
				cz.code[codeLen + 1] = 0;
			}
			czRedraw(0);
		}
		else if (right || left)
		{
			if (codeLen == 0)
			{
				cz.code[0] = (right ? '0' : '#');
				cz.code[1] = 0;
			}
			else
			{
				const char *p = strchr(symbols, cz.code[codeLen - 1]);
				int at = ((p != NULL) ? (int) (p - symbols) : 0);

				cz.code[codeLen - 1] = symbols[(at + (right ? 1 : 15)) % 16];
			}
			czRedraw(0);
		}
	}

	return MENU_STATUS_SUCCESS;
}

static menuStatus_t czHandleFtDelete(uiEvent_t *ev, bool down, bool up, bool right, bool left)
{
	if (down || up || right || left)
	{
		cz.confirmYes = ((cz.confirmYes != 0) ? 0 : 1);
		czRedraw(0);
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED) || (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN) && (cz.confirmYes == 0)))
	{
		czSetMode(CZ_MODE_FT_EDIT); // "no": back to the contact
		menuDataGlobal.currentItemIndex = CZE_DELETE;
		czUpdateKeypad();
		czRedraw(0);
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		bool deleted = codeplugDTMFContactErase(cz.ftIndex);

		if (deleted)
		{
			nextKeyBeepMelody = (int16_t *) MELODY_ACK_BEEP;
			czNotify("Ki\x96i silindi", "Deleted");
		}
		else
		{
			czNotify("Silinemedi", "Delete failed");
		}

		czStartFtList(0);
		czRedraw(0);
		return (deleted ? MENU_STATUS_SUCCESS : MENU_STATUS_ERROR);
	}

	return MENU_STATUS_SUCCESS;
}

// Up/down through a list of count items. True if it moved (and redrew).
static bool czListMove(int count, bool down, bool up)
{
	if (count <= 0)
	{
		return false;
	}

	if (down)
	{
		menuSystemMenuIncrement(&menuDataGlobal.currentItemIndex, count);
	}
	else if (up)
	{
		menuSystemMenuDecrement(&menuDataGlobal.currentItemIndex, count);
	}
	else
	{
		return false;
	}

	czRedraw(0);
	return true;
}

static menuStatus_t czHandleZonePick(uiEvent_t *ev, bool down, bool up)
{
	if (czListMove((czZoneCount() - 1), down, up))
	{
		return MENU_STATUS_LIST_TYPE;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		cz.zoneEdit = (int16_t) menuDataGlobal.currentItemIndex;
		czStartZoneMenu(CZM_RENAME);
		czRedraw(0);
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		czBackToList(CZ_LIST_ZONE_EDIT);
		czRedraw(0);
	}

	return MENU_STATUS_SUCCESS;
}

static menuStatus_t czHandleZoneMenu(uiEvent_t *ev, bool down, bool up)
{
	if (czListMove(CZM_COUNT, down, up))
	{
		return MENU_STATUS_LIST_TYPE;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		switch (menuDataGlobal.currentItemIndex)
		{
			case CZM_RENAME:
				czZoneName(cz.zoneEdit, cz.name);
				cz.renaming = 1;
				czSetMode(CZ_MODE_ZONE);
				break;
			case CZM_CHANNELS:
				menuDataGlobal.currentItemIndex = 0;
				menuDataGlobal.numItems = czChannelCount();
				czSetMode(CZ_MODE_ZONE_CHANNELS);
				break;
			case CZM_DELETE:
				czRequirePinThen(CZ_MODE_ZONE_DELETE);
				break;
		}
		czRedraw(0);
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		if (czStartZonePick(cz.zoneEdit) == false)
		{
			czBackToList(CZ_LIST_ZONE_EDIT);
		}
		czRedraw(0);
	}

	return MENU_STATUS_SUCCESS;
}

// Green adds the channel under the cursor to the zone, or takes it out if it is
// already there. A new one goes at the end of the zone.
static menuStatus_t czHandleZoneChannels(uiEvent_t *ev, bool down, bool up)
{
	if (czListMove(czChannelCount(), down, up))
	{
		return MENU_STATUS_LIST_TYPE;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		struct_codeplugZone_t zone;
		int ch = czChannelAt(menuDataGlobal.currentItemIndex);
		menuStatus_t status = MENU_STATUS_SUCCESS;

		if ((ch == 0) || (codeplugZoneGetDataForNumber(cz.zoneEdit, &zone) == false))
		{
			return MENU_STATUS_ERROR;
		}

		if (czZoneHas(&zone, ch))
		{
			if (codeplugZoneRemoveChannel(cz.zoneEdit, ch))
			{
				czNotify("\xC7\x93kar\x93ld\x93", "Removed");
			}
			else
			{
				czNotify("Yazma hatas\x93", "Write failed");
				status = MENU_STATUS_ERROR;
			}
		}
		else if (zone.NOT_IN_CODEPLUGDATA_numChannelsInZone >= codeplugChannelsPerZone)
		{
			czNotify("Zon dolu", "Zone is full");
			status = MENU_STATUS_ERROR;
		}
		else if (codeplugZoneAddChannelToZoneAndSave(ch, &zone))
		{
			czNotify("Eklendi", "Added");
		}
		else
		{
			czNotify("Yazma hatas\x93", "Write failed");
			status = MENU_STATUS_ERROR;
		}

		cz.dirty = 1;
		czRedraw(0);
		return status;
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		czStartZoneMenu(CZM_CHANNELS);
		czRedraw(0);
	}

	return MENU_STATUS_SUCCESS;
}

static menuStatus_t czHandleZoneDelete(uiEvent_t *ev, bool down, bool up, bool right, bool left)
{
	if (down || up || right || left)
	{
		cz.confirmYes = ((cz.confirmYes != 0) ? 0 : 1);
		czRedraw(0);
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED) || (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN) && (cz.confirmYes == 0)))
	{
		czStartZoneMenu(CZM_DELETE); // "no"
		czRedraw(0);
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		bool deleted = codeplugZoneDelete(cz.zoneEdit);

		if (deleted)
		{
			cz.dirty = 1;
			cz.newZone = -1; // zone numbers above it have moved
			nextKeyBeepMelody = (int16_t *) MELODY_ACK_BEEP;
			czNotify("Zon silindi", "Zone deleted");
		}
		else
		{
			czNotify("Silinemedi", "Delete failed");
		}

		if (czStartZonePick(cz.zoneEdit) == false) // the next zone along, or the list if none are left
		{
			czBackToList(CZ_LIST_ZONE_EDIT);
		}
		czRedraw(0);
		return (deleted ? MENU_STATUS_SUCCESS : MENU_STATUS_ERROR);
	}

	return MENU_STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// APRS settings and the callsign: keys
// ---------------------------------------------------------------------------
static menuStatus_t czHandleAprsList(uiEvent_t *ev, bool down, bool up)
{
	if (czListMove(czAprsRowCount(), down, up))
	{
		return MENU_STATUS_LIST_TYPE;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		int item = menuDataGlobal.currentItemIndex;

		if (czAprsHasNewRow())
		{
			czStartAprsEdit(item); // row 0 is "new", row n is config n
		}
		else
		{
			czStartAprsEdit(item + 1);
		}
		czRedraw(0);
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		czBackToList(CZ_LIST_APRS);
		czRedraw(0);
	}

	return MENU_STATUS_SUCCESS;
}

static bool czSaveAprs(void)
{
	int index = ((cz.aprsIndex != 0) ? cz.aprsIndex : (codeplugAPRSConfigGetCount() + 1));

	if (index > CZ_APRS_MAX)
	{
		czNotify("Liste dolu", "List full");
		return false;
	}

	if (codeplugAPRSSetDataForIndex(index, &cz.aprs) == false)
	{
		czNotify("Yazma hatas\x93", "Write failed");
		return false;
	}

	cz.aprsIndex = (uint8_t) index;
	aprsBeaconingResetTimers(); // the beacon re-reads its channel's config, in case it is this one
	return true;
}

static menuStatus_t czHandleAprsEdit(uiEvent_t *ev, bool down, bool up, bool right, bool left)
{
	int field = menuDataGlobal.currentItemIndex;

	if ((KEYCHECK_LONGDOWN(ev->keys, KEY_RED) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_RED) == false)) ||
			KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		czStartAprsList(0); // leaves without saving
		czRedraw(0);
		return MENU_STATUS_SUCCESS;
	}

	if (czListMove(czAprsEditRows(), down, up))
	{
		return MENU_STATUS_LIST_TYPE;
	}

	if (right || left)
	{
		if (field == CZA_SSID)
		{
			cz.aprs.senderSSID = (uint8_t) ((cz.aprs.senderSSID + (right ? 1 : 15)) % 16);
		}
		else if (field == CZA_SYMBOL)
		{
			int s = czSymbolFind();

			s = ((s < 0) ? 0 : ((s + (right ? 1 : (CZ_SYMBOL_COUNT - 1))) % CZ_SYMBOL_COUNT));
			cz.aprs.iconTable = ((czSymbols[s].table == '/') ? 0 : 1);
			cz.aprs.iconIndex = (uint8_t) (czSymbols[s].symbol - '!');
		}
		else if (field == CZA_POSITION)
		{
			// GPS <-> fixed. Going to fixed with no position stored yet takes the
			// radio's current one, so it never sends 0,0 by accident.
			if ((cz.aprs.flags & CZ_APRS_FLAG_FIXED_POSITION) != 0)
			{
				cz.aprs.flags &= (uint8_t) ~CZ_APRS_FLAG_FIXED_POSITION;
			}
			else if ((czAprsGetFixed24(cz.aprs.latitude) == 0U) && (czAprsGetFixed24(cz.aprs.longitude) == 0U))
			{
				if (czAprsTakePosition() == false)
				{
					czNotify("Konum yok", "No position");
					czRedraw(0);
					return MENU_STATUS_ERROR;
				}
			}
			else
			{
				cz.aprs.flags |= CZ_APRS_FLAG_FIXED_POSITION;
			}
		}
		czRedraw(0);
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		menuStatus_t status = MENU_STATUS_SUCCESS;

		switch (field)
		{
			case CZA_NAME:
				czStartText(CZT_APRS_NAME);
				break;
			case CZA_PATH1:
				czStartText(CZT_PATH1);
				break;
			case CZA_PATH2:
				czStartText(CZT_PATH2);
				break;
			case CZA_COMMENT:
				czStartText(CZT_COMMENT);
				break;
			case CZA_POSITION:
				// Green takes where the radio is now as the fixed position.
				if (czAprsTakePosition())
				{
					czNotify("Konum al\x93nd\x93", "Position set");
				}
				else
				{
					czNotify("Konum yok", "No position");
					status = MENU_STATUS_ERROR;
				}
				break;
			case CZA_SAVE:
				if (czSaveAprs())
				{
					nextKeyBeepMelody = (int16_t *) MELODY_ACK_BEEP;
					czNotify("Kaydedildi", "Saved");
					czStartAprsList(cz.aprsIndex - (czAprsHasNewRow() ? 0 : 1));
				}
				else
				{
					status = MENU_STATUS_ERROR;
				}
				break;
			case CZA_DELETE:
				cz.confirmYes = 0; // the question starts on "no"
				czSetMode(CZ_MODE_APRS_DELETE);
				break;
			default:
				menuDataGlobal.currentItemIndex++; // SSID, symbol: on to the next row
				break;
		}
		czRedraw(0);
		return status;
	}

	return MENU_STATUS_SUCCESS;
}

// "Delete this APRS setting?" - starts on "no". The channels that used it are
// left without APRS and the later settings move up a place (codeplug.c).
static menuStatus_t czHandleAprsDelete(uiEvent_t *ev, bool down, bool up, bool right, bool left)
{
	if (down || up || right || left)
	{
		cz.confirmYes = ((cz.confirmYes != 0) ? 0 : 1);
		czRedraw(0);
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED) || (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN) && (cz.confirmYes == 0)))
	{
		menuDataGlobal.currentItemIndex = CZA_DELETE; // "no": back to the setting
		menuDataGlobal.numItems = czAprsEditRows();
		czSetMode(CZ_MODE_APRS_EDIT);
		czRedraw(0);
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		bool deleted = codeplugAPRSDeleteIndex(cz.aprsIndex);

		if (deleted)
		{
			nextKeyBeepMelody = (int16_t *) MELODY_ACK_BEEP;
			czNotify("Silindi", "Deleted");
			aprsBeaconingResetTimers(); // the beacon re-reads its channel's config, which may be gone
		}
		else
		{
			czNotify("Silinemedi", "Delete failed");
		}

		cz.aprsIndex = 0;
		czStartAprsList(0);
		czRedraw(0);
		return (deleted ? MENU_STATUS_SUCCESS : MENU_STATUS_ERROR);
	}

	return MENU_STATUS_SUCCESS;
}

// Back from the text editor to where it was opened from.
static void czTextBack(void)
{
	static const uint8_t rowFor[] = { CZA_NAME, CZA_PATH1, CZA_PATH2, CZA_COMMENT };

	if (cz.textTarget == CZT_CALLSIGN)
	{
		czBackToList(CZ_LIST_CALLSIGN);
	}
	else
	{
		menuDataGlobal.currentItemIndex = rowFor[cz.textTarget];
		menuDataGlobal.numItems = czAprsEditRows();
		czSetMode(CZ_MODE_APRS_EDIT);
	}
}

static bool czIsCallChar(char c)
{
	return (((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')));
}

// Puts the typed text back into its field. Paths and the callsign are made
// upper case - the keypad gives lower case first - and must be letters and
// digits, as AX.25 addresses are. False, with a message, if it will not do.
static bool czCommitText(void)
{
	char *t = cz.text;
	int len = (int) strlen(t);

	if ((cz.textTarget == CZT_PATH1) || (cz.textTarget == CZT_PATH2) || (cz.textTarget == CZT_CALLSIGN))
	{
		for (int i = 0; i < len; i++)
		{
			if ((t[i] >= 'a') && (t[i] <= 'z'))
			{
				t[i] = (char) (t[i] - ('a' - 'A'));
			}
		}
	}

	switch (cz.textTarget)
	{
		case CZT_APRS_NAME:
			if (len == 0)
			{
				czNotify("Ad gerekli", "Name needed");
				return false;
			}
			codeplugUtilConvertStringToBuf(t, cz.aprs.name, 8);
			return true;

		case CZT_PATH1:
		case CZT_PATH2:
		{
			APRS_Paths_t *path = &cz.aprs.paths[cz.textTarget - CZT_PATH1];
			char *dash = strchr(t, '-');
			int nameLen = ((dash != NULL) ? (int) (dash - t) : len);
			int ssid = 0;

			if (len == 0)
			{
				// The beacon always sends the first path, so only the second may be empty.
				if (cz.textTarget == CZT_PATH1)
				{
					czNotify("Yol 1 gerekli", "Path 1 needed");
					return false;
				}
				memset(path, 0, sizeof(*path));
				return true;
			}

			bool ok = ((nameLen >= 1) && (nameLen <= 6));

			for (int i = 0; ok && (i < nameLen); i++)
			{
				ok = czIsCallChar(t[i]);
			}

			if (ok && (dash != NULL))
			{
				const char *d = (dash + 1);

				ok = ((*d >= '0') && (*d <= '9'));
				while (ok && (*d != 0))
				{
					ok = ((*d >= '0') && (*d <= '9'));
					ssid = ((ssid * 10) + (*d - '0'));
					d++;
				}
				ok = (ok && (ssid <= 15));
			}

			if (ok == false)
			{
				czNotify("Ge\xE7""ersiz yol", "Invalid path");
				return false;
			}

			memset(path, 0, sizeof(*path));
			memcpy(path->name, t, nameLen);
			path->SSID = (uint8_t) ssid;
			return true;
		}

		case CZT_COMMENT:
			memset(cz.aprs.comment, 0, sizeof(cz.aprs.comment));
			memcpy(cz.aprs.comment, t, ((len > CZ_TEXT_MAX) ? CZ_TEXT_MAX : len));
			return true;

		case CZT_CALLSIGN:
		{
			bool ok = (len > 0);

			for (int i = 0; ok && (i < len); i++)
			{
				ok = czIsCallChar(t[i]);
			}

			if (ok == false)
			{
				czNotify("Ge\xE7""ersiz", "Invalid");
				return false;
			}

			if (codeplugSetRadioName(t) == false)
			{
				czNotify("Yazma hatas\x93", "Write failed");
				return false;
			}
			return true;
		}
	}

	return false;
}

// Multi-tap text into cz.text, up to the field's length.
static void czTypeText(uiEvent_t *ev)
{
	if ((ev->keys.key >= ' ') && (ev->keys.key < 127))
	{
		if (ev->keys.event & KEY_MOD_PREVIEW)
		{
			czRedraw((char) ev->keys.key);
		}
		else if (ev->keys.event & KEY_MOD_PRESS)
		{
			int len = (int) strlen(cz.text);

			if (len < czTextMax())
			{
				cz.text[len] = (char) ev->keys.key;
				cz.text[len + 1] = 0;
			}
			czRedraw(0);
		}
	}
}

static menuStatus_t czHandleText(uiEvent_t *ev)
{
	int len = (int) strlen(cz.text);

	if (KEYCHECK_LONGDOWN(ev->keys, KEY_RED) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_RED) == false))
	{
		czTextBack(); // nothing changed
		czRedraw(0);
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		if (len > 0)
		{
			cz.text[len - 1] = 0;
		}
		else
		{
			czTextBack();
		}
		czRedraw(0);
		return MENU_STATUS_SUCCESS;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		if (czCommitText() == false)
		{
			czRedraw(0);
			return MENU_STATUS_ERROR;
		}

		if (cz.textTarget == CZT_CALLSIGN)
		{
			nextKeyBeepMelody = (int16_t *) MELODY_ACK_BEEP;
			czNotify("Kaydedildi", "Saved"); // APRS settings are saved with the whole config
		}
		czTextBack();
		czRedraw(0);
		return MENU_STATUS_SUCCESS;
	}

	czTypeText(ev);
	return MENU_STATUS_SUCCESS;
}

menuStatus_t menuChannelZone(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		czEnsureInit();
		menuSystemRegisterExitCallback(czExitCallback, NULL);

		if (cz.magic == CZ_DELETE_MAGIC)
		{
			cz.magic = 0;
			czRequirePinThen(CZ_MODE_CONFIRM);
		}
		else if ((menuDataGlobal.controlData.stackPosition > 0) &&
				(menuDataGlobal.controlData.stack[menuDataGlobal.controlData.stackPosition - 1] == MENU_CONTACTS_MENU))
		{
			// Opened from Menu > Contacts: this is "5T Contacts". The same screen
			// rather than one of its own, because a menu entry costs main RAM.
			cz.ftListItem = 0;
			czStartFtList(0);
		}
		else
		{
			czBackToList(CZ_LIST_CHANNEL);
		}

		czRedraw(0);
		return (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);
	}

	if (ev->hasEvent == false)
	{
		return MENU_STATUS_SUCCESS;
	}

	if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
	{
		czRedraw(0);
		return MENU_STATUS_SUCCESS;
	}

	if ((ev->events & KEY_EVENT) == 0)
	{
		return MENU_STATUS_SUCCESS;
	}

	// The arrows act on press, so holding one repeats. The channel knob comes
	// through as LEFT/RIGHT outside the channel and VFO screens.
	bool down  = (KEYCHECK_PRESS(ev->keys, KEY_DOWN) || KEYCHECK_PRESS(ev->keys, KEY_FRONT_DOWN));
	bool up    = (KEYCHECK_PRESS(ev->keys, KEY_UP) || KEYCHECK_PRESS(ev->keys, KEY_FRONT_UP));
	bool right = (KEYCHECK_PRESS(ev->keys, KEY_RIGHT) || KEYCHECK_PRESS(ev->keys, KEY_ROTARY_INCREMENT));
	bool left  = (KEYCHECK_PRESS(ev->keys, KEY_LEFT) || KEYCHECK_PRESS(ev->keys, KEY_ROTARY_DECREMENT));

	switch (cz.mode)
	{
		case CZ_MODE_CHANNEL:
			return czHandleChannel(ev, down, up, right, left);
		case CZ_MODE_ZONE:
			return czHandleZone(ev);
		case CZ_MODE_PIN:
			return czHandlePin(ev);
		case CZ_MODE_CONFIRM:
			return czHandleConfirm(ev, down, up, right, left);
		case CZ_MODE_FT_LIST:
			return czHandleFtList(ev, (down || right), (up || left));
		case CZ_MODE_FT_EDIT:
			return czHandleFtEdit(ev, down, up, right, left);
		case CZ_MODE_FT_DELETE:
			return czHandleFtDelete(ev, down, up, right, left);
		case CZ_MODE_ZONE_PICK:
			return czHandleZonePick(ev, (down || right), (up || left));
		case CZ_MODE_ZONE_MENU:
			return czHandleZoneMenu(ev, (down || right), (up || left));
		case CZ_MODE_ZONE_CHANNELS:
			return czHandleZoneChannels(ev, (down || right), (up || left));
		case CZ_MODE_ZONE_DELETE:
			return czHandleZoneDelete(ev, down, up, right, left);
		case CZ_MODE_PW_NEW:
		case CZ_MODE_PW_AGAIN:
			return czHandlePin(ev);
		case CZ_MODE_APRS_LIST:
			return czHandleAprsList(ev, (down || right), (up || left));
		case CZ_MODE_APRS_EDIT:
			return czHandleAprsEdit(ev, down, up, right, left);
		case CZ_MODE_TEXT:
			return czHandleText(ev);
		case CZ_MODE_APRS_DELETE:
			return czHandleAprsDelete(ev, down, up, right, left);
		default:
			return czHandleList(ev, (down || right), (up || left));
	}
}
