/*
 * Copyright (C) 2019-2024 Roger Clark, VK3KYY / G4KYF
 *                         Daniel Caujolle-Bert, F1RMB
 *
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

#include <hardware/HX8353E.h>
#include <stdlib.h>
//vk3kyy #include "io/display.h"
#if defined(LANGUAGE_BUILD_JAPANESE)
#include "hardware/HX8353E_charset_JA.h"
#else
#include <hardware/HX8353E_charset.h>
#endif
#include "functions/settings.h"
#include "user_interface/uiLocalisation.h"
#include "user_interface/menuSystem.h"
#include "utils.h"
#include "stm32f4xx_hal.h"

// number representing the maximum angle (e.g. if 100, then if you pass in start=0 and end=50, you get a half circle)
// this can be changed with setArcParams function at runtime
#define DEFAULT_ARC_ANGLE_MAX 360.0f
// rotational offset in degrees defining position of value 0 (-90 will put it at the top of circle)
// this can be changed with setAngleOffset function at runtime
#define DEFAULT_ANGLE_OFFSET -90.0f
static float _arcAngleMax = DEFAULT_ARC_ANGLE_MAX;
static float _angleOffset = DEFAULT_ANGLE_OFFSET;
#define DEG_TO_RAD  0.017453292519943295769236907684886f
#define RAD_TO_DEG 57.295779513082320876798154814105f

extern bool headerRowIsDirty;

static bool inverted = false;
#if defined(HAS_COLOURS)
static themeItem_t foregroundThemeItem = THEME_ITEM_FG_DEFAULT;
static uint16_t foregroundColour = PLATFORM_COLOUR_FORMAT_SWAP_BYTES(RGB888_TO_PLATFORM_COLOUR_FORMAT(0x000000));
static themeItem_t backgroundThemeItem = THEME_ITEM_BG;
static uint16_t backgroundColour = PLATFORM_COLOUR_FORMAT_SWAP_BYTES(RGB888_TO_PLATFORM_COLOUR_FORMAT(0xFFFFFFU));
#else
static uint16_t foregroundColour = 0x0000U;
static uint16_t backgroundColour = 0xFFFFU;
#endif

static uint16_t screenBufData[DISPLAY_SIZE_X * DISPLAY_SIZE_Y];
uint16_t *screenBuf = screenBufData;
//#define DISPLAY_CHECK_BOUNDS

#ifdef DISPLAY_CHECK_BOUNDS
static const uint8_t *screenBufEnd = screenBuf + sizeof(screenBuf);
#endif

#if defined(HAS_COLOURS)
DayTime_t themeDaytime = DAY;
uint16_t themeItems[NIGHT + 1][THEME_ITEM_MAX]; // Theme storage
#endif

int16_t displaySetPixel(int16_t x, int16_t y, bool isInverted)
{
	int16_t i = (y * DISPLAY_SIZE_X) + x;

	if ((i < 0) || (i >= (DISPLAY_SIZE_X * DISPLAY_SIZE_Y)))
	{
		return -1;// off the screen
	}

	screenBuf[i] = isInverted ? foregroundColour : backgroundColour;

	return 0;
}

void displayRenderWithoutNotification(void)
{
	displayRenderRows(0, DISPLAY_NUMBER_OF_ROWS);
	headerRowIsDirty = false;
}

void displayRender(void)
{
	if (uiNotificationIsVisible())
	{
		uiNotificationRefresh();
	}
	else
	{
		displayRenderRows(0, DISPLAY_NUMBER_OF_ROWS);
	}
	headerRowIsDirty = false;
}

//#define DISPLAY_CHECK_BOUNDS
#ifdef DISPLAY_CHECK_BOUNDS
static inline bool checkWritePos(uint8_t * writePos)
{
	if (writePos < screenBuf || writePos > screenBufEnd)
	{
#if defined(USING_EXTERNAL_DEBUGGER)
		SEGGER_RTT_printf(0,"Display buffer error\n");
#endif
		return false;
	}
	return true;
}
#endif

#if ! defined(PLATFORM_GD77S)
static uint8_t *getUncompressedChar(uint8_t *dest, uint8_t *currentFont, uint8_t charOffset)
{
	if (charOffset < CHARS_PER_FONT)
	{
		uint8_t count = 0;
		uint8_t *p = currentFont + 8; // skip the header
		uint8_t numOfPairs = *p;

		do
		{
			if (count == charOffset)
			{
				uint8_t *pSrc = p + 1;
				uint8_t *pDest = dest;

				// decode RLE8
				while (numOfPairs > 0)
				{
					uint8_t l = (*(pSrc + 1) + 1);

					memset(pDest, *pSrc, l);

					pDest += l;
					pSrc += 2;
					numOfPairs--;
				}

				break;
			}

			p += ((numOfPairs * 2) + 1);
			numOfPairs = *p;

			count++;

		} while (count < CHARS_PER_FONT);
	}

	return dest;
}
#endif

int displayPrintCore(int16_t xPos, int16_t yPos, const char *szMsg, ucFont_t fontSize, ucTextAlign_t alignment, bool isInverted)
{
#if ! defined(PLATFORM_GD77S)
	int16_t sLen;
	uint8_t *currentCharData;
	int16_t charWidthPixels;
	int16_t charHeightPixels;
	int16_t bytesPerChar;
	int16_t startCode;
	int16_t endCode;
	uint8_t *currentFont;
	bool fontIsCompressed = false;
	uint8_t uncompressChar[64];

	sLen = strlen(szMsg);

	switch(fontSize)
	{
#if defined(PLATFORM_RD5R)
		case FONT_SIZE_1:
			currentFont = (uint8_t *) font_6x8;
			break;
		case FONT_SIZE_1_BOLD:
			currentFont = (uint8_t *) font_6x8_bold;
			break;
		case FONT_SIZE_2:
			currentFont = (uint8_t *) font_8x8;
			break;
		case FONT_SIZE_3:
			currentFont = (uint8_t *) font_8x8; // font_8x16;
			break;
		case FONT_SIZE_4:
			currentFont = (uint8_t *) font_8x16; // font_16x32;
			break;
#else
		case FONT_SIZE_1:
			currentFont = (uint8_t *) font_6x8;
			break;
		case FONT_SIZE_1_BOLD:
			currentFont = (uint8_t *) font_6x8_bold;
			break;
		case FONT_SIZE_2:
			currentFont = (uint8_t *) font_8x8;
			break;
		case FONT_SIZE_3:
			currentFont = (uint8_t *) font_8x16;
			break;
		case FONT_SIZE_4:
			currentFont = (uint8_t *) font_16x32_compressed;
			break;
#endif
		default:
			return -2;// Invalid font selected
			break;
	}

	fontIsCompressed	= (currentFont[0] & 0x01); // does the current font is compressed ?
	startCode   		= currentFont[2];  // get first defined character
	endCode 	  		= currentFont[3];  // get last defined character
	charWidthPixels   	= currentFont[4];  // width in pixel of one char
	charHeightPixels  	= currentFont[5];  // page count per char
	bytesPerChar 		= currentFont[7];  // bytes per char

	if ((charWidthPixels * sLen) + xPos > DISPLAY_SIZE_X)
	{
		sLen = (DISPLAY_SIZE_X - xPos) / charWidthPixels;
	}

	if (sLen < 0)
	{
		return -1;
	}

	switch(alignment)
	{
		case TEXT_ALIGN_LEFT:
			// left aligned, do nothing.
			break;
		case TEXT_ALIGN_CENTER:
			xPos = (DISPLAY_SIZE_X - (charWidthPixels * sLen)) >> 1;
			break;
		case TEXT_ALIGN_RIGHT:
			xPos = DISPLAY_SIZE_X - (charWidthPixels * sLen);
			break;
	}

	for (int16_t i = 0; i < sLen; i++)
	{
		// Skip space character as it's empty (and no more part of the fonts).
		if (szMsg[i] == ' ')
		{
			continue;
		}

		uint32_t charOffset = (szMsg[i] - startCode);

		// End boundary checking.
		if (charOffset > endCode)
		{
			charOffset = ('?' - startCode); // Substitute unsupported ASCII code by a question mark
		}

		if (fontIsCompressed)
		{
			currentCharData = getUncompressedChar(&uncompressChar[0], currentFont, charOffset);
		}
		else
		{
			currentCharData = (uint8_t *)&currentFont[8 + (charOffset * bytesPerChar)];
		}

		uint32_t charPixelOffset = (i * charWidthPixels);

		for(int x = 0; x <  charWidthPixels; x++)
		{
			int xp = x + xPos;

			for(int y =  0; y < charHeightPixels; y++)
			{
				uint8_t rowData = currentCharData[x + ((y / 8) * charWidthPixels)];
				int32_t bOffset = xp + ((yPos + y) * DISPLAY_SIZE_X) + charPixelOffset;

				if ((rowData >> (y % 8) & 0x01) && (bOffset < (DISPLAY_SIZE_X * DISPLAY_SIZE_Y)))
				{
					screenBuf[xp + ((yPos + y) * DISPLAY_SIZE_X) + charPixelOffset] =  isInverted ? backgroundColour : foregroundColour;
				}

			}
		}
	}
#endif // ! PLATFORM_GD77S
	return 0;
}

void displayClearBuf(void)
{
	// may be able to do this using DMA
	for(int i = 0; i < DISPLAY_SIZE_X * DISPLAY_SIZE_Y; i++)
	{
		screenBuf[i] = backgroundColour;
	}
}

void displayClearRows(int16_t startRow, int16_t endRow, bool isInverted)
{
	// Boundaries
	if (((startRow < 0) || (endRow < 0)) || ((startRow > DISPLAY_NUMBER_OF_ROWS) || (endRow > DISPLAY_NUMBER_OF_ROWS)) || (startRow == endRow))
	{
		return;
	}

	if (endRow < startRow)
	{
		SAFE_SWAP(startRow, endRow);
	}

	startRow *= (8 * DISPLAY_SIZE_X);
	endRow *= (8 * DISPLAY_SIZE_X);

	uint16_t fillColour = (isInverted ? foregroundColour : backgroundColour);

	for(int i = startRow; i < endRow; i++)
	{
		screenBuf[i] = fillColour;
	}

}

void displayPrintCentered(uint16_t y, const char *text, ucFont_t fontSize)
{
	displayPrintCore(0, y, text, fontSize, TEXT_ALIGN_CENTER, false);
}

void displayPrintAt(uint16_t x, uint16_t y, const char *text, ucFont_t fontSize)
{
	displayPrintCore(x, y, text, fontSize, TEXT_ALIGN_LEFT, false);
}

// Bresenham's algorithm - thx wikpedia
void displayDrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, bool isInverted)
{
	bool steep = abs(y1 - y0) > abs(x1 - x0);

	if (steep)
	{
		SAFE_SWAP(x0, y0);
		SAFE_SWAP(x1, y1);
	}

	if (x0 > x1)
	{
		SAFE_SWAP(x0, x1);
		SAFE_SWAP(y0, y1);
	}

	int16_t dx, dy;
	dx = x1 - x0;
	dy = abs(y1 - y0);

	int16_t err = dx >> 1;
	int16_t ystep;

	ystep = ((y0 < y1) ? 1 : -1);

	for (; x0 <= x1; x0++)
	{
		if (steep)
		{
			displaySetPixel(y0, x0, isInverted);
		}
		else
		{
			displaySetPixel(x0, y0, isInverted);
		}

		err -= dy;
		if (err < 0)
		{
			y0 += ystep;
			err += dx;
		}
	}
}

void displayDrawFastVLine(int16_t x, int16_t y, int16_t h, bool isInverted)
{
	displayFillRect(x, y, 1, h, !isInverted);
}

void displayDrawFastHLine(int16_t x, int16_t y, int16_t w, bool isInverted)
{
	displayFillRect(x, y, w, 1, !isInverted);
}

// Draw a circle outline
void displayDrawCircle(int16_t x0, int16_t y0, int16_t r, bool isInverted)
{
	int16_t f     = 1 - r;
	int16_t ddF_x = 1;
	int16_t ddF_y = -2 * r;
	int16_t x     = 0;
	int16_t y     = r;

	displaySetPixel(x0    , y0 + r, isInverted);
	displaySetPixel(x0    , y0 - r, isInverted);
	displaySetPixel(x0 + r, y0    , isInverted);
	displaySetPixel(x0 - r, y0    , isInverted);

	while (x < y)
	{
		if (f >= 0)
		{
			y--;
			ddF_y += 2;
			f += ddF_y;
		}

		x++;
		ddF_x += 2;
		f += ddF_x;

		displaySetPixel(x0 + x, y0 + y, isInverted);
		displaySetPixel(x0 - x, y0 + y, isInverted);
		displaySetPixel(x0 + x, y0 - y, isInverted);
		displaySetPixel(x0 - x, y0 - y, isInverted);
		displaySetPixel(x0 + y, y0 + x, isInverted);
		displaySetPixel(x0 - y, y0 + x, isInverted);
		displaySetPixel(x0 + y, y0 - x, isInverted);
		displaySetPixel(x0 - y, y0 - x, isInverted);
	}
}

void displayDrawCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t cornername, bool isInverted)
{
	int16_t f     = 1 - r;
	int16_t ddF_x = 1;
	int16_t ddF_y = -2 * r;
	int16_t x     = 0;
	int16_t y     = r;

	while (x < y)
	{
		if (f >= 0)
		{
			y--;
			ddF_y += 2;
			f     += ddF_y;
		}

		x++;
		ddF_x += 2;
		f     += ddF_x;

		if (cornername & 0x4)
		{
			displaySetPixel(x0 + x, y0 + y, isInverted);
			displaySetPixel(x0 + y, y0 + x, isInverted);
		}

		if (cornername & 0x2)
		{
			displaySetPixel(x0 + x, y0 - y, isInverted);
			displaySetPixel(x0 + y, y0 - x, isInverted);
		}

		if (cornername & 0x8)
		{
			displaySetPixel(x0 - y, y0 + x, isInverted);
			displaySetPixel(x0 - x, y0 + y, isInverted);
		}

		if (cornername & 0x1)
		{
			displaySetPixel(x0 - y, y0 - x, isInverted);
			displaySetPixel(x0 - x, y0 - y, isInverted);
		}
	}
}

/*
 * Used to do circles and roundrects
 */
void displayFillCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t cornername, int16_t delta, bool isInverted)
{
	int16_t f     = 1 - r;
	int16_t ddF_x = 1;
	int16_t ddF_y = -2 * r;
	int16_t x     = 0;
	int16_t y     = r;

	while (x < y)
	{
		if (f >= 0)
		{
			y--;
			ddF_y += 2;
			f     += ddF_y;
		}

		x++;
		ddF_x += 2;
		f     += ddF_x;

		if (cornername & 0x1)
		{
			displayDrawFastVLine(x0 + x, y0 - y, 2 * y + 1 + delta, isInverted);
			displayDrawFastVLine(x0 + y, y0 - x, 2 * x + 1 + delta, isInverted);
		}

		if (cornername & 0x2)
		{
			displayDrawFastVLine(x0 - x, y0 - y, 2 * y + 1 + delta, isInverted);
			displayDrawFastVLine(x0 - y, y0 - x, 2 * x + 1 + delta, isInverted);
		}
	}
}

void displayFillCircle(int16_t x0, int16_t y0, int16_t r, bool isInverted)
{
	displayDrawFastVLine(x0, y0 - r, 2 * r + 1, isInverted);
	displayFillCircleHelper(x0, y0, r, 3, 0, isInverted);
}

/*
 * ***** Arc related functions *****
 */
static float cosDegrees(float angle)
{
	return cos(angle * DEG_TO_RAD);
}

static float sinDegrees(float angle)
{
	return sin(angle * DEG_TO_RAD);
}

/*
 * DrawArc function thanks to Jnmattern and his Arc_2.0 (https://github.com/Jnmattern)
 */
void displayFillArcOffsetted(uint16_t cx, uint16_t cy, uint16_t radius, uint16_t thickness, float start, float end, bool isInverted)
{
	int16_t xmin = 65535, xmax = -32767, ymin = 32767, ymax = -32767;
	float cosStart, sinStart, cosEnd, sinEnd;
	float r, t;
	float startAngle, endAngle;

	startAngle = (start / _arcAngleMax) * 360.0f;	// 252
	endAngle = (end / _arcAngleMax) * 360.0f;		// 807

	while (startAngle < 0.0f)
	{
		startAngle += 360.0f;
	}
	while (endAngle < 0.0f)
	{
		endAngle += 360.0f;
	}
	while (startAngle > 360.0f)
	{
		startAngle -= 360.0f;
	}
	while (endAngle > 360.0f)
	{
		endAngle -= 360.0f;
	}

	if (startAngle > endAngle)
	{
		displayFillArcOffsetted(cx, cy, radius, thickness, ((startAngle) / 360.0f) * _arcAngleMax, _arcAngleMax, isInverted);
		displayFillArcOffsetted(cx, cy, radius, thickness, 0, ((endAngle) / 360.0f) * _arcAngleMax, isInverted);
	}
	else
	{
		// Calculate bounding box for the arc to be drawn
		cosStart = cosDegrees(startAngle);
		sinStart = sinDegrees(startAngle);
		cosEnd = cosDegrees(endAngle);
		sinEnd = sinDegrees(endAngle);

		r = radius;
		// Point 1: radius & startAngle
		t = r * cosStart;
		if (t < xmin)
		{
			xmin = t;
		}
		if (t > xmax)
		{
			xmax = t;
		}
		t = r * sinStart;
		if (t < ymin)
		{
			ymin = t;
		}
		if (t > ymax)
		{
			ymax = t;
		}

		// Point 2: radius & endAngle
		t = r * cosEnd;
		if (t < xmin)
		{
			xmin = t;
		}
		if (t > xmax)
		{
			xmax = t;
		}
		t = r * sinEnd;
		if (t < ymin)
		{
			ymin = t;
		}
		if (t > ymax)
		{
			ymax = t;
		}

		r = radius - thickness;
		// Point 3: radius-thickness & startAngle
		t = r * cosStart;
		if (t < xmin)
		{
			xmin = t;
		}
		if (t > xmax)
		{
			xmax = t;
		}
		t = r * sinStart;
		if (t < ymin)
		{
			ymin = t;
		}
		if (t > ymax)
		{
			ymax = t;
		}

		// Point 4: radius-thickness & endAngle
		t = r * cosEnd;
		if (t < xmin)
		{
			xmin = t;
		}
		if (t > xmax)
		{
			xmax = t;
		}
		t = r * sinEnd;
		if (t < ymin)
		{
			ymin = t;
		}
		if (t > ymax)
		{
			ymax = t;
		}

		// Corrections if arc crosses X or Y axis
		if ((startAngle < 90.0f) && (endAngle > 90.0f))
		{
			ymax = radius;
		}

		if ((startAngle < 180.0f) && (endAngle > 180.0f))
		{
			xmin = -radius;
		}

		if ((startAngle < 270.0f) && (endAngle > 270.0f))
		{
			ymin = -radius;
		}

		// Slopes for the two sides of the arc
		float sslope = cosStart / sinStart;
		float eslope = cosEnd / sinEnd;

		if (endAngle == 360.0f) eslope = -1000000.0f;

		int ir2 = (radius - thickness) * (radius - thickness);
		int or2 = radius * radius;

		for (int x = xmin; x <= xmax; x++)
		{
			bool y1StartFound = false, y2StartFound = false;
			bool y1EndFound = false, y2EndSearching = false;
			int y1s = 0, y1e = 0, y2s = 0;
			for (int y = ymin; y <= ymax; y++)
			{
				int x2 = x * x;
				int y2 = y * y;

				if (
						(x2 + y2 < or2 && x2 + y2 >= ir2) && (
								(y > 0.0f && startAngle < 180.0f && x <= y * sslope) ||
								(y < 0.0f && startAngle > 180.0f && x >= y * sslope) ||
								(y < 0.0f && startAngle <= 180.0f) ||
								(y == 0.0f && startAngle <= 180.0f && x < 0.0f) ||
								(y == 0.0f && startAngle == 0.0f && x > 0.0f)
						) && (
								(y > 0.0f && endAngle < 180.0f && x >= y * eslope) ||
								(y < 0.0f && endAngle > 180.0f && x <= y * eslope) ||
								(y > 0.0f && endAngle >= 180.0f) ||
								(y == 0.0f && endAngle >= 180.0f && x < 0.0f) ||
								(y == 0.0f && startAngle == 0.0f && x > 0.0f)))
				{
					if (!y1StartFound)	//start of the higher line found
					{
						y1StartFound = true;
						y1s = y;
					}
					else if (y1EndFound && !y2StartFound) //start of the lower line found
					{
						y2StartFound = true;
						//drawPixel_cont(cx+x, cy+y, ILI9341_BLUE);
						y2s = y;
						y += y1e - y1s - 1;	// calculate the most probable end of the lower line (in most cases the length of lower line is equal to length of upper line), in the next loop we will validate if the end of line is really there
						if (y > ymax - 1) // the most probable end of line 2 is beyond ymax so line 2 must be shorter, thus continue with pixel by pixel search
						{
							y = y2s;	// reset y and continue with pixel by pixel search
							y2EndSearching = true;
						}
					}
					else if (y2StartFound && !y2EndSearching)
					{
						// we validated that the probable end of the lower line has a pixel, continue with pixel by pixel search, in most cases next loop with confirm the end of lower line as it will not find a valid pixel
						y2EndSearching = true;
					}
				}
				else
				{
					if (y1StartFound && !y1EndFound) //higher line end found
					{
						y1EndFound = true;
						y1e = y - 1;
						displayDrawFastVLine(cx + x, cy + y1s, y - y1s, isInverted);
						if (y < 0)
						{
							y = abs(y); // skip the empty middle
						}
						else
							break;
					}
					else if (y2StartFound)
					{
						if (y2EndSearching)
						{
							// we found the end of the lower line after pixel by pixel search
							displayDrawFastVLine(cx + x, cy + y2s, y - y2s, isInverted);
							y2EndSearching = false;
							break;
						}
						else
						{
							// the expected end of the lower line is not there so the lower line must be shorter
							y = y2s;	// put the y back to the lower line start and go pixel by pixel to find the end
							y2EndSearching = true;
						}
					}
				}
			}
			if (y1StartFound && !y1EndFound)
			{
				y1e = ymax;
				displayDrawFastVLine(cx + x, cy + y1s, y1e - y1s + 1, isInverted);
			}
			else if (y2StartFound && y2EndSearching)	// we found start of lower line but we are still searching for the end
			{										// which we haven't found in the loop so the last pixel in a column must be the end
				displayDrawFastVLine(cx + x, cy + y2s, ymax - y2s + 1, isInverted);
			}
		}
	}
}

void displayFillArc(uint16_t x, uint16_t y, uint16_t radius, uint16_t thickness, float start, float end, bool isInverted)
{
	if (start == 0.0f && end == _arcAngleMax)
	{
		displayFillArcOffsetted(x, y, radius, thickness, 0, _arcAngleMax, isInverted);
	}
	else
	{
		displayFillArcOffsetted(x, y, radius, thickness, start + (_angleOffset / 360.0f)*_arcAngleMax, end + (_angleOffset / 360.0f)*_arcAngleMax, isInverted);
	}
}
/*
 * ***** End of Arc related functions *****
 */

void displayDrawEllipse(int16_t x0, int16_t y0, int16_t x1, int16_t y1, bool isInverted)
{
	int16_t a = abs(x1 - x0), b = abs(y1 - y0), b1 = b & 1; /* values of diameter */
	long dx = 4 * (1 - a) * b * b, dy = 4 * (b1 + 1) * a * a; /* error increment */
	long err = dx + dy + b1 * a * a, e2; /* error of 1.step */

	if (x0 > x1)
	{
		x0 = x1;
		x1 += a;
	} /* if called with swapped points */
	if (y0 > y1)
	{
		y0 = y1; /* .. exchange them */
	}
	y0 += (b + 1) >> 1; /* starting pixel */
	y1 = y0 - b1;
	a *= 8 * a;
	b1 = 8 * b * b;

	do {
		displaySetPixel(x1, y0, isInverted); /*   I. Quadrant */
		displaySetPixel(x0, y0, isInverted); /*  II. Quadrant */
		displaySetPixel(x0, y1, isInverted); /* III. Quadrant */
		displaySetPixel(x1, y1, isInverted); /*  IV. Quadrant */
		e2 = 2 * err;
		if (e2 >= dx)
		{
			x0++;
			x1--;
			err += dx += b1;
		} /* x step */
		if (e2 <= dy)
		{
			y0++;
			y1--;
			err += dy += a;
		}  /* y step */
	} while (x0 <= x1);

	while (y0 - y1 < b) /* too early stop of flat ellipses a=1 */
	{
		displaySetPixel(x0 - 1, ++y0, isInverted); /* -> complete tip of ellipse */
		displaySetPixel(x0 - 1, --y1, isInverted);
	}
}

/*
 * Draw a triangle
 */
void displayDrawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, bool isInverted)
{
	displayDrawLine(x0, y0, x1, y1, isInverted);
	displayDrawLine(x1, y1, x2, y2, isInverted);
	displayDrawLine(x2, y2, x0, y0, isInverted);
}

/*
 * Fill a triangle
 */
void displayFillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, bool isInverted)
{
	int16_t a, b, y, last;

	// Sort coordinates by Y order (y2 >= y1 >= y0)
	if (y0 > y1)
	{
		SAFE_SWAP(y0, y1); SAFE_SWAP(x0, x1);
	}
	if (y1 > y2)
	{
		SAFE_SWAP(y2, y1); SAFE_SWAP(x2, x1);
	}
	if (y0 > y1)
	{
		SAFE_SWAP(y0, y1); SAFE_SWAP(x0, x1);
	}

	if(y0 == y2) // Handle awkward all-on-same-line case as its own thing
	{
		a = b = x0;
		if(x1 < a)
		{
			a = x1;
		}
		else if(x1 > b)
		{
			b = x1;
		}

		if(x2 < a)
		{
			a = x2;
		}
		else if(x2 > b)
		{
			b = x2;
		}

		displayDrawFastHLine(a, y0, b - a + 1, isInverted);
		return;
	}

	int16_t dx01 = x1 - x0,
			dy01 = y1 - y0,
			dx02 = x2 - x0,
			dy02 = y2 - y0,
			dx12 = x2 - x1,
			dy12 = y2 - y1,
			sa   = 0,
			sb   = 0;

	// For upper part of triangle, find scanline crossings for segments
	// 0-1 and 0-2.  If y1=y2 (flat-bottomed triangle), the scanline y1
	// is included here (and second loop will be skipped, avoiding a /0
	// error there), otherwise scanline y1 is skipped here and handled
	// in the second loop...which also avoids a /0 error here if y0=y1
	// (flat-topped triangle).
	if(y1 == y2) last = y1;   // Include y1 scanline
	else         last = y1-1; // Skip it

	for(y = y0; y <= last; y++)
	{
		a   = x0 + sa / dy01;
		b   = x0 + sb / dy02;
		sa += dx01;
		sb += dx02;
		/* longhand:
		a = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
		b = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
		 */
		if(a > b)
		{
			SAFE_SWAP(a,b);
		}

		displayDrawFastHLine(a, y, b - a + 1, isInverted);
	}

	// For lower part of triangle, find scanline crossings for segments
	// 0-2 and 1-2.  This loop is skipped if y1=y2.
	sa = dx12 * (y - y1);
	sb = dx02 * (y - y0);

	for(; y <= y2; y++)
	{
		a   = x1 + sa / dy12;
		b   = x0 + sb / dy02;
		sa += dx12;
		sb += dx02;
		/* longhand:
		a = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
		b = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
		 */
		if(a > b)
		{
			SAFE_SWAP(a,b);
		}

		displayDrawFastHLine(a, y, b - a + 1, isInverted);
	}
}

/*
 * Draw a rounded rectangle
 */
void displayDrawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, bool isInverted)
{
	// smarter version
	displayDrawFastHLine(x + r    , y        , w - 2 * r, isInverted); // Top
	displayDrawFastHLine(x + r    , y + h - 1, w - 2 * r, isInverted); // Bottom
	displayDrawFastVLine(x        , y + r    , h - 2 * r, isInverted); // Left
	displayDrawFastVLine(x + w - 1, y + r    , h - 2 * r, isInverted); // Right
	// draw four corners
	displayDrawCircleHelper(x + r        , y + r        , r, 1, isInverted);
	displayDrawCircleHelper(x + w - r - 1, y + r        , r, 2, isInverted);
	displayDrawCircleHelper(x + w - r - 1, y + h - r - 1, r, 4, isInverted);
	displayDrawCircleHelper(x + r        , y + h - r - 1, r, 8, isInverted);
}

/*
 * Fill a rounded rectangle
 */
void displayFillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, bool isInverted)
{
	displayFillRect(x + r, y, w - 2 * r, h, !isInverted);

	// draw four corners
	displayFillCircleHelper(x+w-r-1, y + r, r, 1, h - 2 * r - 1, isInverted);
	displayFillCircleHelper(x+r    , y + r, r, 2, h - 2 * r - 1, isInverted);
}

/*
 *
 */
void displayDrawRoundRectWithDropShadow(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, bool isInverted)
{
	displayFillRoundRect(x + 2, y, w, h, r, isInverted); // Shadow
	displayFillRoundRect(x, y - 2, w, h, r, !isInverted); // Empty box
	displayDrawRoundRect(x, y - 2, w, h, r, isInverted); // Outline
}

/*
 * Draw a rectangle
 */
void displayDrawRect(int16_t x, int16_t y, int16_t w, int16_t h, bool isInverted)
{
	displayDrawFastHLine(x        , y        , w, isInverted);
	displayDrawFastHLine(x        , y + h - 1, w, isInverted);
	displayDrawFastVLine(x        , y        , h, isInverted);
	displayDrawFastVLine(x + w - 1, y        , h, isInverted);
}

/*
 * Fill a rectangle
 */
void displayFillRect(int16_t x, int16_t y, int16_t width, int16_t height, bool isInverted)
{
	uint32_t lineStartOffset;

	for(int yp = 0; yp < height; yp++)
	{
		lineStartOffset = (y + yp) * DISPLAY_SIZE_X;
		for(int xp = 0; xp < width; xp++)
		{
			screenBuf[lineStartOffset + x + xp] = isInverted ? backgroundColour : foregroundColour;
		}
	}
}

/*
 *
 */
void displayDrawRectWithDropShadow(int16_t x, int16_t y, int16_t w, int16_t h, bool isInverted)
{
	displayFillRect(x + 2, y, w, h, !isInverted); // Shadow
	displayFillRect(x, y - 2, w, h, isInverted); // Empty box
	displayDrawRect(x, y - 2, w, h, isInverted); // Outline
}

/*
 * Draw a 1-bit image at the specified (x,y) position.
 */
void displayDrawBitmap(int16_t x, int16_t y, uint8_t *bitmap, int16_t w, int16_t h, bool isInverted)
{
	int16_t byteWidth = (w + 7) / 8; // Bitmap scanline pad = whole byte
	uint8_t byte = 0;

	for(int16_t j = 0; j < h; j++, y++)
	{
		for(int16_t i = 0; i < w; i++)
		{
			if(i & 7)
			{
				byte <<= 1;
			}
			else
			{
				byte = *(bitmap + (j * byteWidth + i / 8));
			}

			if(byte & 0x80)
			{
				displaySetPixel(x + i, y, isInverted);
			}
		}
	}
}

/*
 * Draw XBitMap Files (*.xbm), e.g. exported from GIMP.
 */
void displayDrawXBitmap(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, bool isInverted)
{
	int16_t byteWidth = (w + 7) / 8; // Bitmap scanline pad = whole byte
	uint8_t byte = 0;

	for(int16_t j = 0; j < h; j++, y++)
	{
		for(int16_t i = 0; i < w; i++)
		{
			if(i & 7)
			{
				byte >>= 1;
			}
			else
			{
				byte = *(bitmap + (j * byteWidth + i / 8));
			}
			// Nearly identical to drawBitmap(), only the bit order
			// is reversed here (left-to-right = LSB to MSB):
			if(byte & 0x01)
			{
				displaySetPixel(x + i, y, isInverted);
			}
		}
	}
}

void displayDrawChoice(ucChoice_t choice, bool clearRegion)
{
#if defined(PLATFORM_RD5R)
	const uint8_t TEXT_Y = 40;
	const uint8_t FILLRECT_Y = 32;
#elif defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
	const uint8_t TEXT_Y = 113 - DISPLAY_Y_OFFSET;
	const uint8_t FILLRECT_Y = 112 - DISPLAY_Y_OFFSET;
#else
	const uint8_t TEXT_Y = 49;
	const uint8_t FILLRECT_Y = 48;
#endif
	const uint8_t TEXT_L_CENTER_X = 12;
	const uint8_t TEXT_R_CENTER_X = (DISPLAY_SIZE_X - 13);

	struct
	{
			char *lText;
			char *rText;
	} choices[] =
	{
			{ "OK"                                       , NULL                                       }, // UC1701_CHOICE_OK
#if defined(PLATFORM_MD9600)
			{ "ENT"                                      , NULL                                       }, // UC1701_CHOICE_ENT
#endif
			{ (char *)currentLanguage->yes___in_uppercase, (char *)currentLanguage->no___in_uppercase }, // UC1701_CHOICE_YESNO
			{ NULL                                       , (char *)currentLanguage->DISMISS           }, // UC1701_CHOICE_DISMISS
			{ "OK"                                       , NULL                                       }  // UC1701_CHOICE_OKARROWS
	};
	char *lText = NULL;
	char *rText = NULL;
	const int ucTriangleArrows[2][6] = {
			{ // Down
					(DISPLAY_SIZE_X/2)-12, (TEXT_Y + (FONT_SIZE_3_HEIGHT / 2) - 1),
					(DISPLAY_SIZE_X/2)-4, (TEXT_Y + (FONT_SIZE_3_HEIGHT / 2) - (FONT_SIZE_3_HEIGHT / 4) - 1),
					(DISPLAY_SIZE_X/2)-4, (TEXT_Y + (FONT_SIZE_3_HEIGHT / 2) + (FONT_SIZE_3_HEIGHT / 4) - 1)
			}, // Up
			{
					(DISPLAY_SIZE_X/2)+4, (TEXT_Y + (FONT_SIZE_3_HEIGHT / 2) + (FONT_SIZE_3_HEIGHT / 4) - 1),
					(DISPLAY_SIZE_X/2)+4, (TEXT_Y + (FONT_SIZE_3_HEIGHT / 2) - (FONT_SIZE_3_HEIGHT / 4) - 1),
					(DISPLAY_SIZE_X/2)+12, (TEXT_Y + (FONT_SIZE_3_HEIGHT / 2) - 1)
			}
	};

	displayThemeResetToDefault();
	if (clearRegion)
	{
		displayFillRect(0, FILLRECT_Y, DISPLAY_SIZE_X, 16, true);
	}

	if (choice >= CHOICES_NUM)
	{
		return;
	}

	lText = choices[choice].lText;
	rText = choices[choice].rText;

	displayThemeApply(THEME_ITEM_FG_NOTIFICATION, THEME_ITEM_BG);
	if (lText)
	{
		int16_t x = (TEXT_L_CENTER_X - ((strlen(lText) * 8) >> 1));

		if (x < 2)
		{
			x = 2;
		}
		displayPrintAt(x, TEXT_Y, lText, FONT_SIZE_3);
	}

	if(rText)
	{
		size_t len = (strlen(rText) * 8);
		int16_t x = (TEXT_R_CENTER_X - (len >> 1));

		if ((x + len) > (DISPLAY_SIZE_X - 2))
		{
			x = ((DISPLAY_SIZE_X - 2) - len);
		}
		displayPrintAt(x, TEXT_Y, rText, FONT_SIZE_3);
	}

	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	if (choice == CHOICES_OKARROWS)
	{
		displayFillTriangle(ucTriangleArrows[0][0], ucTriangleArrows[0][1],
				ucTriangleArrows[0][2], ucTriangleArrows[0][3],
				ucTriangleArrows[0][4], ucTriangleArrows[0][5], true);
		displayFillTriangle(ucTriangleArrows[1][0], ucTriangleArrows[1][1],
				ucTriangleArrows[1][2], ucTriangleArrows[1][3],
				ucTriangleArrows[1][4], ucTriangleArrows[1][5], true);
	}
	displayThemeResetToDefault();
}

uint16_t *displayGetScreenBuffer(void)
{
	return screenBuf;
}

void displayRestorePrimaryScreenBuffer(void)
{
	screenBuf = screenBufData;
}

uint16_t *displayGetPrimaryScreenBuffer(void)
{
	return &screenBufData[0];
}

void displayOverrideScreenBuffer(uint16_t *buffer)
{
	screenBuf = buffer;
}

static bool isAwake = true;

#if 0
static void dmaCompleteCallback(DMA_HandleTypeDef *hdma)
{
	HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);

	*((volatile uint8_t*) LCD_FSMC_ADDR_DATA) = 0;// write 0 to the display pins , to pull them all low, so keyboard reads don't need to
}
#endif

void displayRenderRows(int16_t startRow, int16_t endRow)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	// GD77 display controller has 8 lines per row.
	startRow *= 8;
	endRow *= 8;

	// Display shares its pins with the keypad, so the pind need to be put into alternate mode to work with the FSMC
	GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	GPIO_InitStruct.Alternate = GPIO_AF12_FSMC;

	GPIO_InitStruct.Pin = LCD_D0_Pin | LCD_D1_Pin | LCD_D2_Pin | LCD_D3_Pin;
	HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

	GPIO_InitStruct.Pin = LCD_D4_Pin | LCD_D5_Pin | LCD_D6_Pin | LCD_D7_Pin;
	HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

	HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);

	// Set start and end rows of the transfer
	{
#if defined(PLATFORM_VARIANT_DM1701)
		uint8_t opts[] = { 0x00, startRow + DISPLAY_Y_OFFSET, 0x00, endRow + DISPLAY_Y_OFFSET };
#else
		uint8_t opts[] = { 0x00, startRow, 0x00, endRow };
#endif
		displayWriteCmds(HX8583_CMD_RASET, sizeof(opts), opts);
	}

	displayWriteCmd(HX8583_CMD_RAMWR);

	uint8_t *framePtr = (uint8_t *)screenBuf + (DISPLAY_SIZE_X * startRow * sizeof(uint16_t));

	//if (HAL_DMA_RegisterCallback(&hdma_memtomem_dma2_stream0, HAL_DMA_XFER_CPLT_CB_ID, dmaCompleteCallback)== HAL_OK)
	{
		HAL_StatusTypeDef status = HAL_DMA_Start(&hdma_memtomem_dma2_stream0, (uint32_t)framePtr, LCD_FSMC_ADDR_DATA, (endRow - startRow) * DISPLAY_SIZE_X * sizeof(uint16_t));
		if (status == HAL_OK)
		{
			HAL_DMA_PollForTransfer(&hdma_memtomem_dma2_stream0, HAL_DMA_FULL_TRANSFER, HAL_MAX_DELAY);
			// need to wait for completion otherwise we CS gets disabled immediately.
			// This could be done using a transfer complete callback
		}
	}


#if false
	// fallback
	for(int y = 0; y < (endRow - startRow) * DISPLAY_SIZE_X * sizeof(uint16_t); y++)
	{
		*((volatile uint8_t*) LCD_FSMC_ADDR_DATA) = *framePtr++;
	}
#endif

	HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);

	*((volatile uint8_t*) LCD_FSMC_ADDR_DATA) = 0;// write 0 to the display pins , to pull them all low, so keyboard reads don't need to
}

void displaySetInverseVideo(bool isInverted)
{
	inverted = isInverted;
}

// Native color format (RGB565)
void displayBegin(bool isInverted, bool SPIFlashAvailable)
{
	displaySetInvertedState(isInverted);
	themeInit(SPIFlashAvailable);
}

void displaySetContrast(uint8_t contrast)
{
	// this display does not have a contrast control
}

// Note.
// Entering "Sleep" mode makes the display go blank
void displaySetDisplayPowerMode(bool wake)
{
	if (isAwake == wake)
	{
		return;
	}

	isAwake = wake;
}

void displayConvertGD77ImageData(uint8_t *dataBuf)
{
	const uint32_t startOffset = (32 * DISPLAY_SIZE_X) + (DISPLAY_SIZE_X - 128) / 2;

	for(int y = 0; y < 8; y++ )
	{
		for(int x = 0; x < 128; x++)
		{
			uint8_t d = dataBuf[(y * 128) + x];
			for(int r = 0; r < 8; r++)
			{
				screenBuf[startOffset + (((y * 8) + r) * DISPLAY_SIZE_X) + x] = ((d >> r) & 0x01) ? backgroundColour : foregroundColour;
			}
		}
	}

	// clear beginning of display buff used to store the image read from flash
	for(int i = 0; i < (128 * 64 / 8 / 2); i++)
	{
		screenBuf[i] = backgroundColour;
	}
}

//
// Colour related functions
//

#if defined(HAS_COLOURS)
uint16_t displayConvertRGB888ToNative(uint32_t RGB888)
{
	// Convert to native format (RGB565/BGR565, swapped bytes)
	return __builtin_bswap16(RGB888_TO_PLATFORM_COLOUR_FORMAT(RGB888));
}
#endif

//
// Native color format (swapped RGB565/BGR565, swapped bytes) functions
//
void displaySetForegroundAndBackgroundColours(uint16_t fgNativeColour, uint16_t bgNativeColour)
{
	foregroundColour = fgNativeColour;
	backgroundColour = bgNativeColour;
}

void displayGetForegroundAndBackgroundColours(uint16_t *fgNativeColour, uint16_t *bgNativeColour)
{
	*fgNativeColour = foregroundColour;
	*bgNativeColour = backgroundColour;
}

#if defined(HAS_COLOURS)
void themeInitToDefaultValues(DayTime_t daytime, bool invert)
{
	foregroundColour = PLATFORM_COLOUR_FORMAT_SWAP_BYTES(RGB888_TO_PLATFORM_COLOUR_FORMAT((invert ? 0xFFFFFFU : 0x000000)));
	backgroundColour = PLATFORM_COLOUR_FORMAT_SWAP_BYTES(RGB888_TO_PLATFORM_COLOUR_FORMAT((invert ? 0x000000 : 0xFFFFFFU)));

	foregroundThemeItem = THEME_ITEM_FG_DEFAULT;
	backgroundThemeItem = THEME_ITEM_BG;

	themeItems[daytime][THEME_ITEM_FG_DEFAULT] = foregroundColour;
	themeItems[daytime][THEME_ITEM_BG] = backgroundColour;
	themeItems[daytime][THEME_ITEM_FG_DECORATION] = foregroundColour;
	themeItems[daytime][THEME_ITEM_FG_TEXT_INPUT] = foregroundColour;
	themeItems[daytime][THEME_ITEM_FG_SPLASHSCREEN] = foregroundColour;
	themeItems[daytime][THEME_ITEM_BG_SPLASHSCREEN] = backgroundColour;
	themeItems[daytime][THEME_ITEM_FG_NOTIFICATION] = foregroundColour;
	themeItems[daytime][THEME_ITEM_FG_WARNING_NOTIFICATION] = foregroundColour;
	themeItems[daytime][THEME_ITEM_FG_ERROR_NOTIFICATION] = foregroundColour;
	themeItems[daytime][THEME_ITEM_BG_NOTIFICATION] = backgroundColour;
	themeItems[daytime][THEME_ITEM_FG_MENU_NAME] = foregroundColour;
	themeItems[daytime][THEME_ITEM_BG_MENU_NAME] = backgroundColour;
	themeItems[daytime][THEME_ITEM_FG_MENU_ITEM] = foregroundColour;
	themeItems[daytime][THEME_ITEM_BG_MENU_ITEM_SELECTED] = foregroundColour;
	themeItems[daytime][THEME_ITEM_FG_OPTIONS_VALUE] = foregroundColour;
	themeItems[daytime][THEME_ITEM_FG_HEADER_TEXT] = foregroundColour;
	themeItems[daytime][THEME_ITEM_BG_HEADER_TEXT] = backgroundColour;
	// Coloured S-meter: green up to S9, red above S9 (S9+). The RSSI bar in the
	// header uses THEME_ITEM_FG_RSSI_BAR_S9P for the part beyond S9 (see
	// uiUtilityDrawRSSIBarGraph), so a strong signal (e.g. S9+40) shows red.
	themeItems[daytime][THEME_ITEM_FG_RSSI_BAR] = PLATFORM_COLOUR_FORMAT_SWAP_BYTES(RGB888_TO_PLATFORM_COLOUR_FORMAT(0x00C000U));     // green (<= S9)
	themeItems[daytime][THEME_ITEM_FG_RSSI_BAR_S9P] = PLATFORM_COLOUR_FORMAT_SWAP_BYTES(RGB888_TO_PLATFORM_COLOUR_FORMAT(0xFF0000U)); // red   (> S9)
	themeItems[daytime][THEME_ITEM_FG_CHANNEL_NAME] = foregroundColour;
	themeItems[daytime][THEME_ITEM_FG_CHANNEL_CONTACT] = foregroundColour;
	themeItems[daytime][THEME_ITEM_FG_CHANNEL_CONTACT_INFO] = foregroundColour;
	themeItems[daytime][THEME_ITEM_FG_ZONE_NAME] = foregroundColour;
	themeItems[daytime][THEME_ITEM_FG_RX_FREQ] = foregroundColour;
	themeItems[daytime][THEME_ITEM_FG_TX_FREQ] = foregroundColour;
	themeItems[daytime][THEME_ITEM_FG_CSS_SQL_VALUES] = foregroundColour;
	themeItems[daytime][THEME_ITEM_FG_TX_COUNTER] = foregroundColour;
	themeItems[daytime][THEME_ITEM_FG_POLAR_DRAWING] = foregroundColour;
	themeItems[daytime][THEME_ITEM_FG_SATELLITE_COLOUR] = foregroundColour;
	themeItems[daytime][THEME_ITEM_FG_GPS_NUMBER] = foregroundColour;
	themeItems[daytime][THEME_ITEM_FG_GPS_COLOUR] = PLATFORM_COLOUR_FORMAT_SWAP_BYTES(RGB888_TO_PLATFORM_COLOUR_FORMAT(0x0000FFU));
	themeItems[daytime][THEME_ITEM_FG_BD_COLOUR] = PLATFORM_COLOUR_FORMAT_SWAP_BYTES(RGB888_TO_PLATFORM_COLOUR_FORMAT(0xFF0000U));
}

void themeInit(bool SPIFlashAvailable)
{
	themeInitToDefaultValues(NIGHT, true);
	themeInitToDefaultValues(DAY, false);

	// Read and apply user's theme, if any
	if (SPIFlashAvailable)
	{
		uint16_t themingTmp[THEME_ITEM_MAX];

		if (codeplugGetOpenGD77CustomData(CODEPLUG_CUSTOM_DATA_TYPE_THEME_DAY, (uint8_t *) &themingTmp))
		{
			memcpy(&themeItems[DAY], &themingTmp, sizeof(themingTmp));

			foregroundColour = themeItems[DAY][THEME_ITEM_FG_DEFAULT];
			backgroundColour = themeItems[DAY][THEME_ITEM_BG];
		}

		if (codeplugGetOpenGD77CustomData(CODEPLUG_CUSTOM_DATA_TYPE_THEME_NIGHT, (uint8_t *) &themingTmp))
		{
			memcpy(&themeItems[NIGHT], &themingTmp, sizeof(themingTmp));
		}
	}

	if (settingsIsOptionBitSet(BIT_AUTO_NIGHT_OVERRIDE) && (uiDataGlobal.daytimeOverridden == NIGHT))
	{
		displayThemeResetToDefault(); // Update colours to NIGHT theme (as default is DAY).
	}
}

void displayThemeApply(themeItem_t fgItem, themeItem_t bgItem)
{
	if (fgItem < THEME_ITEM_MAX)
	{
		foregroundThemeItem = fgItem;
		foregroundColour = themeItems[DAYTIME_CURRENT][fgItem];
	}

	if (bgItem < THEME_ITEM_MAX)
	{
		backgroundThemeItem = bgItem;
		backgroundColour = themeItems[DAYTIME_CURRENT][bgItem];
	}
}

void displayThemeResetToDefault(void)
{
	foregroundThemeItem = THEME_ITEM_FG_DEFAULT;
	backgroundThemeItem = THEME_ITEM_BG;
	displaySetForegroundAndBackgroundColours(themeItems[DAYTIME_CURRENT][THEME_ITEM_FG_DEFAULT], themeItems[DAYTIME_CURRENT][THEME_ITEM_BG]);
}

bool displayThemeIsForegroundColourEqualTo(themeItem_t fgItem)
{
	return (foregroundThemeItem == fgItem);
}

void displayThemeGetForegroundAndBackgroundItems(themeItem_t *fgItem, themeItem_t *bgItem)
{
	*fgItem = foregroundThemeItem;
	*bgItem = backgroundThemeItem;
}

bool displayThemeSaveToFlash(DayTime_t daytime)
{
	return codeplugSetOpenGD77CustomData(((daytime == DAY) ? CODEPLUG_CUSTOM_DATA_TYPE_THEME_DAY : CODEPLUG_CUSTOM_DATA_TYPE_THEME_NIGHT),
			(uint8_t *)&themeItems[daytime], (sizeof(uint16_t) * THEME_ITEM_MAX));
}
#endif // HAS_COLOURS
