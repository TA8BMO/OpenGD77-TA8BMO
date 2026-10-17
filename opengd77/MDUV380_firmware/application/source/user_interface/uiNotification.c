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
#include "utils.h"


#if defined(PLATFORM_RD5R)
#define YCENTER         ((DISPLAY_SIZE_Y / 2) + 3)
#define INNERBOX_H      (FONT_SIZE_3_HEIGHT + 7)
#define YBAR            (YCENTER - ((FONT_SIZE_3_HEIGHT / 2) + 2))
#define YTEXT           (YBAR + 1)
#else
#define YCENTER         (DISPLAY_SIZE_Y / 2)
#define INNERBOX_H      (FONT_SIZE_3_HEIGHT + 6)
#define YBAR            (YCENTER - (FONT_SIZE_3_HEIGHT / 2))
#define YTEXT           (YBAR - 2)
#endif
#define XBAR            69
#define YBOX            (YCENTER - (INNERBOX_H / 2))

#if defined(PLATFORM_MD9600) || defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_DM1801) || defined(PLATFORM_DM1801A) || defined(PLATFORM_RD5R)
static __attribute__((section(".data.$RAM2"))) uint8_t screenNotificationBufData[((DISPLAY_SIZE_X * DISPLAY_SIZE_Y) >> 3)];
#else
// Every notification - squelch, volume, power, message - is drawn inside one band
// across the middle of the screen: the box sits two rows above YBOX and its drop
// shadow ends at the bottom of YBOX + INNERBOX_H. Only that band, with a few rows
// of margin either side, has to be put back afterwards, so only that band is
// saved - 10KB rather than a copy of the whole 40KB screen.
#define NOTIFICATION_SAVE_MARGIN  4
#define NOTIFICATION_SAVE_Y       ((YBOX - 2) - NOTIFICATION_SAVE_MARGIN)
#define NOTIFICATION_SAVE_ROWS    ((INNERBOX_H + 2) + (2 * NOTIFICATION_SAVE_MARGIN))
_Static_assert((NOTIFICATION_SAVE_Y >= 0) && ((NOTIFICATION_SAVE_Y + NOTIFICATION_SAVE_ROWS) <= DISPLAY_SIZE_Y), "notification band must lie within the screen");
static  __attribute__((section(".ccmram"))) uint16_t screenNotificationBufData[DISPLAY_SIZE_X * NOTIFICATION_SAVE_ROWS];
#endif

typedef struct
{
	bool                   visible;
	ticksTimer_t           hideTimer;
	uiNotificationType_t   type;
	uiNotificationID_t     id;
	char                   message[16]; // 15 char + NULL
} uiNotificationData_t;

static uiNotificationData_t notificationData =
{
		.visible = false,
		.hideTimer = { 0, 0 },
		.type = NOTIFICATION_TYPE_MAX,
		.id = NOTIFICATION_ID_NONE,
		.message = { 0 }
};

void uiNotificationShow(uiNotificationType_t type, uiNotificationID_t id, uint32_t msTimeout, const char *message, bool immediateRender)
{
	bool valid = true;

	if (notificationData.visible)
	{
		if (notificationData.id != id)
		{
			notificationData.visible = false;
			displayRender();
		}
	}

	memset(notificationData.message, 0, sizeof(notificationData.message));

	notificationData.type = type;
	notificationData.id = id;

	switch (type)
	{
		case NOTIFICATION_TYPE_SQUELCH:
#if defined(HAS_SOFT_VOLUME)
		case NOTIFICATION_TYPE_VOLUME:
#endif
			break;

		case NOTIFICATION_TYPE_POWER:
			break;

		case NOTIFICATION_TYPE_MESSAGE:
			if (message)
			{
				snprintf(&notificationData.message[0], sizeof(notificationData.message), "%s", message);
			}
			break;

		default:
			valid = false;
			break;
	}

	if (valid)
	{
		notificationData.visible = true;
		if (immediateRender)
		{
			uiNotificationRefresh();
		}
		ticksTimerStart(&notificationData.hideTimer, msTimeout);
	}
}

void uiNotificationRefresh(void)
{
	if (notificationData.visible)
	{
#if defined(PLATFORM_MD9600) || defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_DM1801) || defined(PLATFORM_DM1801A) || defined(PLATFORM_RD5R)
		// copy the primary screen content
		memcpy(screenNotificationBufData, displayGetPrimaryScreenBuffer(), sizeof(screenNotificationBufData));
		displayOverrideScreenBuffer(screenNotificationBufData);
#else
		// save the band the notification is about to draw over
		memcpy(screenNotificationBufData, (displayGetPrimaryScreenBuffer() + (NOTIFICATION_SAVE_Y * DISPLAY_SIZE_X)), sizeof(screenNotificationBufData));
#endif

		// Draw whatever
		switch (notificationData.type)
		{
			case NOTIFICATION_TYPE_SQUELCH:
#if defined(HAS_SOFT_VOLUME)
			case NOTIFICATION_TYPE_VOLUME:
#endif
			{
				char buffer[SCREEN_LINE_BUFFER_SIZE];
				const int16_t totalBarLength = (((DISPLAY_SIZE_X - XBAR) - 4) - 4);
				double slope;
				int16_t bargraph;

#if defined(HAS_SOFT_VOLUME)
				if (notificationData.type == NOTIFICATION_TYPE_VOLUME)
				{
					uint16_t volValue = CLAMP((lastVolume + 32), 0, 62);

					slope = 1.0 * (totalBarLength) / 62.0;
					bargraph = slope * volValue;

					strncpy(buffer, currentLanguage->volume, 9);
				}
				else
#endif
				{
					slope = 1.0 * (totalBarLength) / (CODEPLUG_MAX_VARIABLE_SQUELCH - CODEPLUG_MIN_VARIABLE_SQUELCH);
					bargraph = slope * (currentChannelData->sql - CODEPLUG_MIN_VARIABLE_SQUELCH);

					strncpy(buffer, currentLanguage->squelch, 9);
				}
				buffer[8] = 0;

				size_t sLen = (strlen(buffer) * 8);

				displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG_NOTIFICATION);
				displayDrawRoundRectWithDropShadow(1, YBOX, DISPLAY_SIZE_X - 4, INNERBOX_H, 3, true);

				displayThemeApply(THEME_ITEM_FG_NOTIFICATION, THEME_ITEM_BG_NOTIFICATION);
				displayPrintAt(2 + ((sLen < (XBAR - 2)) ? (((XBAR - 2) - sLen) >> 1) : 0), YTEXT, buffer, FONT_SIZE_3);

				displayDrawRect((XBAR - 2), YBAR, (totalBarLength + 4), (SQUELCH_BAR_H + 4), true);

#if defined(HAS_SOFT_VOLUME)
				if (notificationData.type == NOTIFICATION_TYPE_VOLUME)
				{
					// Green, orange then red as the level rises, using exactly the
					// same colour handling as the S-meter on the main screen -
					// explicit values, pre-inverted when the panel is running in
					// inverse video so they come out the same on the glass.
					//
					// Nothing is drawn over the unfilled part of the bar. An
					// earlier version filled it with THEME_ITEM_FG_DECORATION as
					// a "dim track", but that item is the full-brightness text
					// colour, so the bar looked permanently full.
					uint16_t curFg, curBg;
					const bool inv = settingsIsOptionBitSet(BIT_INVERSE_VIDEO);
#define VOL_COLOUR(rgb) PLATFORM_COLOUR_FORMAT_SWAP_BYTES(RGB888_TO_PLATFORM_COLOUR_FORMAT(inv ? (0xFFFFFFU ^ (rgb)) : (rgb)))
					const struct { int16_t endAt; uint16_t colour; } zone[3] =
					{
						{ (int16_t)((totalBarLength * 60) / 100), VOL_COLOUR(0x00C800U) },
						{ (int16_t)((totalBarLength * 85) / 100), VOL_COLOUR(0xFF8000U) },
						{ totalBarLength,                         VOL_COLOUR(0xFF2020U) },
					};
#undef VOL_COLOUR
					int16_t from = 0;

					displayGetForegroundAndBackgroundColours(&curFg, &curBg);

					for (int z = 0; z < 3; z++)
					{
						int16_t to = ((bargraph < zone[z].endAt) ? bargraph : zone[z].endAt);

						if (to > from)
						{
							displaySetForegroundAndBackgroundColours(zone[z].colour, curBg);
							displayFillRect((XBAR + from), (YBAR + 2), (to - from), SQUELCH_BAR_H, false);
						}
						from = zone[z].endAt;
					}

					displaySetForegroundAndBackgroundColours(curFg, curBg);
				}
				else
#endif
				{
					displayFillRect(XBAR, (YBAR + 2), bargraph, SQUELCH_BAR_H, false);
				}
			}
			break;

			case NOTIFICATION_TYPE_POWER:
			{
				char buffer[SCREEN_LINE_BUFFER_SIZE];
				uint8_t powerLevel = trxGetPowerLevel();

				displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG_NOTIFICATION);
				displayDrawRoundRectWithDropShadow((DISPLAY_SIZE_X / 4), YBOX, (DISPLAY_SIZE_X / 2), INNERBOX_H, 3, true);
				sprintf(buffer, "%s%s", getPowerLevel(powerLevel), getPowerLevelUnit(powerLevel));
				displayThemeApply(THEME_ITEM_FG_NOTIFICATION, THEME_ITEM_BG_NOTIFICATION);
				displayPrintCentered(YTEXT, buffer, FONT_SIZE_3);
			}
			break;

			case NOTIFICATION_TYPE_MESSAGE:
				displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG_NOTIFICATION);
				displayDrawRoundRectWithDropShadow(1, YBOX, (DISPLAY_SIZE_X - 4), INNERBOX_H, 3, true);
				displayThemeApply(THEME_ITEM_FG_NOTIFICATION, THEME_ITEM_BG_NOTIFICATION);
				displayPrintCentered(YTEXT, notificationData.message, FONT_SIZE_3);
				break;

			default:
				break;
		}

		displayThemeResetToDefault();
		displayRenderWithoutNotification();
#if defined(PLATFORM_MD9600) || defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_DM1801) || defined(PLATFORM_DM1801A) || defined(PLATFORM_RD5R)
		displayRestorePrimaryScreenBuffer();
#else
		memcpy((displayGetPrimaryScreenBuffer() + (NOTIFICATION_SAVE_Y * DISPLAY_SIZE_X)), screenNotificationBufData, sizeof(screenNotificationBufData));
#endif
	}
}

bool uiNotificationHasTimedOut(void)
{
	return (notificationData.visible && ticksTimerHasExpired(&notificationData.hideTimer));
}

bool uiNotificationIsVisible(void)
{
	return notificationData.visible;
}

void uiNotificationHide(bool immediateRender)
{
	notificationData.visible = false;
	uiDataGlobal.displayQSOState = uiDataGlobal.displayQSOStatePrev;

	if (immediateRender)
	{
		displayRender();
	}
}

uiNotificationID_t uiNotificationGetId(void)
{
	return notificationData.id;
}
