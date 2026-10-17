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

#ifndef _OPENGD77_HX8353E_H_
#define _OPENGD77_HX8353E_H_

#include "main.h"
#include <stdbool.h>
#include <math.h>
#include <FreeRTOS.h>
#include <task.h>


typedef enum
{
	FONT_SIZE_1 = 0,
	FONT_SIZE_1_BOLD,
	FONT_SIZE_2,
	FONT_SIZE_3,
	FONT_SIZE_4
} ucFont_t;

typedef enum
{
	TEXT_ALIGN_LEFT = 0,
	TEXT_ALIGN_CENTER,
	TEXT_ALIGN_RIGHT
} ucTextAlign_t;

typedef enum
{
	CHOICE_OK = 0,
#if defined(PLATFORM_MD9600)
	CHOICE_ENT,
#endif
	CHOICE_YESNO,
	CHOICE_DISMISS,
	CHOICES_OKARROWS,// QuickKeys
	CHOICES_NUM
} ucChoice_t;

typedef enum
{
	THEME_ITEM_FG_DEFAULT,              // default text foreground colour.
	THEME_ITEM_BG,                      // global background colour.
	THEME_ITEM_FG_DECORATION,           // like borders/drop-shadow/menu line separator/etc.
	THEME_ITEM_FG_TEXT_INPUT,           // foreground colour of text input.
	THEME_ITEM_FG_SPLASHSCREEN,         // foreground colour of SplashScreen.
	THEME_ITEM_BG_SPLASHSCREEN,         // background colour of SplashScreen.
	THEME_ITEM_FG_NOTIFICATION,         // foreground colour of notification text (+ squelch bargraph).
	THEME_ITEM_FG_WARNING_NOTIFICATION, // foreground colour of warning notification/messages.
	THEME_ITEM_FG_ERROR_NOTIFICATION,   // foreground colour of error notification/messages.
	THEME_ITEM_BG_NOTIFICATION,         // foreground colour of notification background.
	THEME_ITEM_FG_MENU_NAME,            // foreground colour of menu name (header).
	THEME_ITEM_BG_MENU_NAME,            // background colour of menu name (header).
	THEME_ITEM_FG_MENU_ITEM,            // foreground colour of menu entries.
	THEME_ITEM_BG_MENU_ITEM_SELECTED,   // foreground colour of selected menu entry.
	THEME_ITEM_FG_OPTIONS_VALUE,        // foreground colour for settings values (options/quickmenus/channel details/etc).
	THEME_ITEM_FG_HEADER_TEXT,          // foreground colour of Channel/VFO header (radio mode/PWR/battery/etc).
	THEME_ITEM_BG_HEADER_TEXT,          // background colour of Channel/VFO header (radio mode/PWR/battery/etc).
	THEME_ITEM_FG_RSSI_BAR,             // foreground colour of RSSI bars <= S9 (Channel/VFO/RSSI screen/Satellite).
	THEME_ITEM_FG_RSSI_BAR_S9P,         // foreground colour of RSSI bars at > S9 (Channel/VFO/RSSI screen).
	THEME_ITEM_FG_CHANNEL_NAME,         // foreground colour of the channel name.
	THEME_ITEM_FG_CHANNEL_CONTACT,      // foreground colour of the contact name (aka TG/PC).
	THEME_ITEM_FG_CHANNEL_CONTACT_INFO, // foreground colour of contact info (DB/Ct/TA).
	THEME_ITEM_FG_ZONE_NAME,            // foreground colour of zone name.
	THEME_ITEM_FG_RX_FREQ,              // foreground colour of RX frequency.
	THEME_ITEM_FG_TX_FREQ,              // foreground colour of TX frequency.
	THEME_ITEM_FG_CSS_SQL_VALUES,       // foreground colour of CSS & Squelch values in Channel/VFO screens.
	THEME_ITEM_FG_TX_COUNTER,           // foreground colour of timer value in TX screen.
	THEME_ITEM_FG_POLAR_DRAWING,        // foreground colour of the polar drawing in the satellite/GPS screens.
	THEME_ITEM_FG_SATELLITE_COLOUR,     // foreground colour of the satellites spots in the satellite screen.
	THEME_ITEM_FG_GPS_NUMBER,           // foreground colour of the GPS number in the GPS screen.
	THEME_ITEM_FG_GPS_COLOUR,           // foreground colour of the GPS bar and spots in the GPS screens.
	THEME_ITEM_FG_BD_COLOUR,            // foreground colour of the BEIDOU bar and spots in the GPS screens.
	THEME_ITEM_MAX,
	THEME_ITEM_COLOUR_NONE              // special none colour, used when colour has not to be changed.
} themeItem_t;

#if defined(HAS_COLOURS)
extern DayTime_t themeDaytime;
extern uint16_t themeItems[NIGHT + 1][THEME_ITEM_MAX]; // Theme storage
#endif


#define HX8583_CMD_NOP          0x00 // No Operation
#define HX8583_CMD_SWRESET      0x01 // Software reset
#define HX8583_CMD_RDDIDIF      0x04 // Read Display ID Info
#define HX8583_CMD_RDDST        0x09 // Read Display Status
#define HX8583_CMD_RDDPM        0x0a // Read Display Power
#define HX8583_CMD_RDD_MADCTL   0x0b // Read Display
#define HX8583_CMD_RDD_COLMOD   0x0c // Read Display Pixel
#define HX8583_CMD_RDDDIM       0x0d // Read Display Image
#define HX8583_CMD_RDDSM        0x0e // Read Display Signal
#define HX8583_CMD_RDDSDR       0x0f // Read display self-diagnostic resut
#define HX8583_CMD_SLPIN        0x10 // Sleep in & booster off
#define HX8583_CMD_SLPOUT       0x11 // Sleep out & booster on
#define HX8583_CMD_PTLON        0x12 // Partial mode on
#define HX8583_CMD_NORON        0x13 // Partial off (Normal)
#define HX8583_CMD_INVOFF       0x20 // Display inversion off
#define HX8583_CMD_INVON        0x21 // Display inversion on
#define HX8583_CMD_GAMSET       0x26 // Gamma curve select
#define HX8583_CMD_DISPOFF      0x28 // Display off
#define HX8583_CMD_DISPON       0x29 // Display on
#define HX8583_CMD_CASET        0x2a // Column address set
#define HX8583_CMD_RASET        0x2b // Row address set
#define HX8583_CMD_RAMWR        0x2c // Memory write
#define HX8583_CMD_RGBSET       0x2d // LUT parameter (16-to-18 color mapping)
#define HX8583_CMD_RAMRD        0x2e // Memory read
#define HX8583_CMD_PTLAR        0x30 // Partial start/end address set
#define HX8583_CMD_VSCRDEF      0x31 // Vertical Scrolling Direction
#define HX8583_CMD_TEOFF        0x34 // Tearing effect line off
#define HX8583_CMD_TEON         0x35 // Tearing effect mode set & on
#define HX8583_CMD_MADCTL       0x36 // Memory data access control
#define HX8583_CMD_VSCRSADD     0x37 // Vertical scrolling start address
#define HX8583_CMD_IDMOFF       0x38 // Idle mode off
#define HX8583_CMD_IDMON        0x39 // Idle mode on
#define HX8583_CMD_COLMOD       0x3a // Interface pixel format
#define HX8583_CMD_RDID1        0xda // Read ID1
#define HX8583_CMD_RDID2        0xdb // Read ID2
#define HX8583_CMD_RDID3        0xdc // Read ID3

#define HX8583_CMD_SETOSC       0xb0 // Set internal oscillator
#define HX8583_CMD_SETPWCTR     0xb1 // Set power control
#define HX8583_CMD_SETDISPLAY   0xb2 // Set display control
#define HX8583_CMD_SETCYC       0xb4 // Set display cycle
#define HX8583_CMD_SETBGP       0xb5 // Set BGP voltage
#define HX8583_CMD_SETVCOM      0xb6 // Set VCOM voltage
#define HX8583_CMD_SETEXTC      0xb9 // Enter extension command
#define HX8583_CMD_SETOTP       0xbb // Set OTP
#define HX8583_CMD_SETSTBA      0xc0 // Set Source option
#define HX8583_CMD_SETID        0xc3 // Set ID
#define HX8583_CMD_SETPANEL     0xcc // Set Panel characteristics
#define HX8583_CMD_GETHID       0xd0 // Read Himax internal ID
#define HX8583_CMD_SETGAMMA     0xe0 // Set Gamma
#define HX8583_CMD_SET_SPI_RDEN 0xfe // Set SPI Read address (and enable)
#define HX8583_CMD_GET_SPI_RDEN 0xff // Get FE A[7:0] parameter

#define LCD_FSMC_ADDR_COMMAND 0x60000000
#define LCD_FSMC_ADDR_DATA    0x60040000

#define FONT_SIZE_2_HEIGHT                       8
#define FONT_SIZE_3_HEIGHT                       16
#define FONT_SIZE_4_HEIGHT                       32
#if defined(PLATFORM_VARIANT_DM1701)
#define DISPLAY_Y_OFFSET						 8
#define DISPLAY_SIZE_Y                          (128 - DISPLAY_Y_OFFSET)
#else
#define DISPLAY_Y_OFFSET						 0
#define DISPLAY_SIZE_Y                          128
#endif
#define DISPLAY_SIZE_X                          160
#define DISPLAY_NUMBER_OF_ROWS  (DISPLAY_SIZE_Y / 8)

#if defined(HAS_COLOURS)
// Platform format could be RGB565 or BGR565
#define RGB888_TO_PLATFORM_COLOUR_FORMAT(x) ((displayLCD_Type & DIPLAYLCD_TYPE_RGB) \
		? ((uint16_t) (((x & 0xf80000) >> 8) + ((x & 0xfc00) >> 5) + ((x & 0xf8) >> 3))) \
		: ((uint16_t) (((x & 0xf80000) >> 19) + ((x & 0xfc00) >> 5) + ((x & 0xf8) << 8))))
#define PLATFORM_COLOUR_FORMAT_TO_RGB888(x) ((displayLCD_Type & DIPLAYLCD_TYPE_RGB) \
		? ((uint32_t) (((x & 0xf800) << 8) + ((x & 0x7e0) << 5) + ((x & 0x1f) << 3))) \
		: ((uint32_t) (((x & 0x1f) << 19) + ((x & 0x7e0) << 5) + ((x & 0xf800) >> 8))))
#define PLATFORM_COLOUR_FORMAT_SWAP_BYTES(n) (((n & 0xFF) << 8) | ((n & 0xFF00) >> 8))
#endif // HAS_COLOURS

#ifndef DEG_TO_RAD
#define DEG_TO_RAD  0.017453292519943295769236907684886f
#endif
#ifndef RAD_TO_DEG
#define RAD_TO_DEG 57.295779513082320876798154814105f
#endif

void displayBegin(bool isInverted, bool SPIFlashAvailable);
void displayClearBuf(void);
void displayClearRows(int16_t startRow, int16_t endRow, bool isInverted);
void displayRenderWithoutNotification(void);
void displayRender(void);
void displayRenderRows(int16_t startRow, int16_t endRow);
void displayPrintCentered(uint16_t y, const char *text, ucFont_t fontSize);
void displayPrintAt(uint16_t x, uint16_t y, const  char *text, ucFont_t fontSize);
int displayPrintCore(int16_t x, int16_t y, const char *szMsg, ucFont_t fontSize, ucTextAlign_t alignment, bool isInverted);
#if ! defined(LANGUAGE_BUILD_JAPANESE)
void displaySetCyrillic(bool on); // draw 0xA8, 0xB8 and 0xC0-0xFF as Windows-1251 Cyrillic (Russian)
bool displayGetCyrillic(void);
#endif

int16_t displaySetPixel(int16_t x, int16_t y, bool isInverted);

void displayDrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, bool isInverted);
void displayDrawFastVLine(int16_t x, int16_t y, int16_t h, bool isInverted);
void displayDrawFastHLine(int16_t x, int16_t y, int16_t w, bool isInverted);

void displayDrawCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t cornername, bool isInverted);
void displayDrawCircle(int16_t x0, int16_t y0, int16_t r, bool isInverted);
void displayFillCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t cornername, int16_t delta, bool isInverted);
void displayFillCircle(int16_t x0, int16_t y0, int16_t r, bool isInverted);

void displayDrawEllipse(int16_t x0, int16_t y0, int16_t x1, int16_t y1, bool isInverted);

void displayDrawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, bool isInverted);
void displayFillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, bool isInverted);

void displayFillArcOffsetted(uint16_t cx, uint16_t cy, uint16_t radius, uint16_t thickness, float start, float end, bool isInverted);
void displayFillArc(uint16_t x, uint16_t y, uint16_t radius, uint16_t thickness, float start, float end, bool isInverted);

void displayDrawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, bool isInverted);
void displayFillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, bool isInverted);
void displayDrawRoundRectWithDropShadow(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, bool isInverted);

void displayDrawRect(int16_t x, int16_t y, int16_t w, int16_t h, bool isInverted);
void displayFillRect(int16_t x, int16_t y, int16_t width, int16_t height, bool isInverted);
void displayDrawRectWithDropShadow(int16_t x, int16_t y, int16_t w, int16_t h, bool isInverted);

void displayDrawBitmap(int16_t x, int16_t y, uint8_t *bitmap, int16_t w, int16_t h, bool isInverted);
void displayDrawXBitmap(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, bool isInverted);

void displaySetContrast(uint8_t contrast);
void displaySetInverseVideo(bool isInverted);

void displaySetDisplayPowerMode(bool wake);

void displayDrawChoice(ucChoice_t choice, bool clearRegion);

uint16_t *displayGetScreenBuffer(void);
void displayRestorePrimaryScreenBuffer(void);
uint16_t *displayGetPrimaryScreenBuffer(void);
void displayOverrideScreenBuffer(uint16_t *buffer);

void displayConvertGD77ImageData(uint8_t *dataBuf);

#if defined(HAS_COLOURS)
uint16_t displayConvertRGB888ToNative(uint32_t RGB888);
#endif

//
// Native color format (swapped RGB565/BGR565, swapped bytes) functions
//
void displaySetForegroundAndBackgroundColours(uint16_t fgColour, uint16_t bgColour);
void displayGetForegroundAndBackgroundColours(uint16_t *fgColour, uint16_t *bgColour);

#if defined(HAS_COLOURS)
void themeInitToDefaultValues(DayTime_t daytime, bool invert);
void themeInit(bool SPIFlashAvailable);
void displayThemeApply(themeItem_t fgItem, themeItem_t bgItem);
void displayThemeResetToDefault(void);
bool displayThemeIsForegroundColourEqualTo(themeItem_t fgItem);
void displayThemeGetForegroundAndBackgroundItems(themeItem_t *fgItem, themeItem_t *bgItem);
bool displayThemeSaveToFlash(DayTime_t daytime);
#else
#define themeInit(x) do {} while(0)
#define displayThemeApply(x, y) do {} while(0)
#define displayThemeResetToDefault() do {} while(0)
#define displayThemeGetForegroundAndBackgroundItems(x, y) do { UNUSED_PARAMETER(x); UNUSED_PARAMETER(y); } while(0)
#endif

#endif /* _OPENGD77_HX8353E_H_ */
