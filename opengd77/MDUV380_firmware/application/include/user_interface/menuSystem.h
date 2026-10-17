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
#ifndef _OPENGD77_MENUSYSTEM_H_
#define _OPENGD77_MENUSYSTEM_H_

#include "main.h"
#include "user_interface/uiGlobals.h"
#include "functions/sound.h"
#include "functions/settings.h"
#include "functions/voicePrompts.h"


#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
#if defined(PLATFORM_VARIANT_DM1701)
#define MENU_MAX_DISPLAYED_ENTRIES 6
#else
#define MENU_MAX_DISPLAYED_ENTRIES 7
#endif
#else
// Black and white smaller height screeens (MD9600 and GD77 etc)
#define MENU_MAX_DISPLAYED_ENTRIES 3
#endif

// These macros are taking care of the odd/even value of MENU_MAX_DISPLAYED_ENTRIES (-x..0..+x)
#define MENU_START_ITERATION_VALUE ((MENU_MAX_DISPLAYED_ENTRIES & 0x01) ? (1 - ((MENU_MAX_DISPLAYED_ENTRIES - 1) / 2) - 1) : (1 - (MENU_MAX_DISPLAYED_ENTRIES / 2)))
#define MENU_END_ITERATION_VALUE ((MENU_MAX_DISPLAYED_ENTRIES & 0x01) ? (MENU_MAX_DISPLAYED_ENTRIES - ((MENU_MAX_DISPLAYED_ENTRIES - 1) / 2) - 1) : (MENU_MAX_DISPLAYED_ENTRIES - (MENU_MAX_DISPLAYED_ENTRIES / 2)))


// Short press event
#define BUTTONCHECK_SHORTUP(e, sk) (((e)->keys.key == 0) && ((e)->buttons & sk ## _SHORT_UP))
// Long press event
#define BUTTONCHECK_LONGDOWN(e, sk) (((e)->keys.key == 0) && ((e)->buttons & sk ## _LONG_DOWN))
// Extra long press event
#define BUTTONCHECK_EXTRALONGDOWN(e, sk) (((e)->keys.key == 0) && ((e)->buttons & sk ## _EXTRA_LONG_DOWN))
// SK*/ORANGE button is down, regardless event status
#define BUTTONCHECK_DOWN(e, sk) (((e)->buttons & sk))
// SK*/ORANGE button is the only one down, regardless event status.
#define BUTTONCHECK_UNIQUE_DOWN(e, sk) (((e)->buttons & sk) && (((e)->buttons & ~(sk | sk ## _SHORT_UP | sk ## _LONG_DOWN | sk ## _EXTRA_LONG_DOWN)) == 0))


typedef enum
{
	NO_EVENT        = 0,
	KEY_EVENT       = (1 << 0),
	BUTTON_EVENT    = (1 << 1),
	FUNCTION_EVENT  = (1 << 2),
	ROTARY_EVENT    = (1 << 3),
	SYNTHETIC_EVENT = (1 << 7)
} uiEventInput_t;

typedef enum
{
	CPS2UI_COMMAND_CLEARBUF = 0,
	CPS2UI_COMMAND_PRINT,
	CPS2UI_COMMAND_RENDER_DISPLAY,
	CPS2UI_COMMAND_BACKLIGHT,
	CPS2UI_COMMAND_GREEN_LED,
	CPS2UI_COMMAND_RED_LED,
	CPS2UI_COMMAND_END
} uiCPSCommand_t;


typedef enum
{
	MENU_STATUS_SUCCESS     = 0,
	MENU_STATUS_ERROR       = (1 << 0),
	MENU_STATUS_LIST_TYPE   = (1 << 1),
	MENU_STATUS_INPUT_TYPE  = (1 << 2),
	MENU_STATUS_FORCE_FIRST = (1 << 3)
} menuStatus_t;


typedef enum
{
	NOTIFICATION_TYPE_SQUELCH = 0,
	NOTIFICATION_TYPE_POWER,
#if defined(HAS_SOFT_VOLUME)
	NOTIFICATION_TYPE_VOLUME,
#endif
	NOTIFICATION_TYPE_MESSAGE,
	NOTIFICATION_TYPE_MAX
} uiNotificationType_t;

typedef enum
{
	NOTIFICATION_ID_NONE = 0,
	NOTIFICATION_ID_SQUELCH,
#if defined(HAS_SOFT_VOLUME)
	NOTIFICATION_ID_VOLUME,
#endif
	NOTIFICATION_ID_POWER,
	NOTIFICATION_ID_MESSAGE,
	NOTIFICATION_ID_USER /* Could use _USER, _USER + 1, + 2, etc... */
} uiNotificationID_t;

#define NOTIFICATION_ID_USER_APO (NOTIFICATION_ID_USER)

typedef struct
{
	uint32_t        		buttons;
	keyboardCode_t  		keys;
	uint32_t        		rotary;
	uint16_t        		function;
	uiEventInput_t  		events;
	bool            		hasEvent;
	uint32_t        		time;
} uiEvent_t;

typedef menuStatus_t (*menuFunctionPointer_t)(uiEvent_t *, bool); // Typedef for menu function pointers.  Functions are passed the key, the button and the event data. Event can be a Key or a button or both. Last arg is for when the function is only called to initialise and display its screen.
typedef void (*menuExitCallback_t)(void *data);
typedef struct
{
	const menuFunctionPointer_t	function;
	menuExitCallback_t			exitCallback;
	void					   *data;
	int							lastItemIndex;
} menuFunctionData_t;

typedef struct
{
	int 					currentMenuNumber;
	int 					stackPosition;
	int 					stack[16];
} menuControlDataStruct_t;

typedef struct
{
	const int 				stringOffset; // String offset in stringsTable_t
	const int 				menuNum;
} menuItemNewData_t;

typedef struct
{
	const int				numItems;
	const menuItemNewData_t *items;
} menuItemsList_t;


void menuDisplayTitle(const char *title);
void menuDisplayEntry(int loopOffset, int focusedItem, const char *entryText, int32_t optStart, themeItem_t fgItem, themeItem_t fgOptItem, themeItem_t bgItem);

// menuGetMenuOffset() return values
#define MENU_OFFSET_BEFORE_FIRST_ENTRY -1
#define MENU_OFFSET_AFTER_LAST_ENTRY   -2
int menuGetMenuOffset(int maxMenuItems, int loopOffset);

void uiChannelModeUpdateScreen(int txTimeSecs);
void uiChannelModeLoadChannelData(bool useChannelDataInMemory, bool loadVoicePromptAnnouncement);

void uiChannelModeColdStart(void);
void uiVFOModeUpdateScreen(int txTimeSecs);
void uiVFOModeLoadChannelData(bool forceAPRSReset);

void uiVFOLoadContact(struct_codeplugContact_t *contact);
bool uiVFOModeIsTXFocused(void);
void uiVFOModeStopScanning(void);
bool uiVFOModeIsScanning(void);
bool uiVFOModeDualWatchIsScanning(void);
bool uiVFOModeSweepScanning(bool includePaused);
void uiVFOSweepScanModePause(bool pause, bool forceDigitalOnPause);
bool uiVFOModeFrequencyScanningIsActiveAndEnabled(uint32_t *lowFreq, uint32_t *highFreq);
void uiChannelModeStopScanning(void);
bool uiChannelModeIsScanning(void);
void uiChannelInitializeCurrentZone(void);


void uiCPSUpdate(uiCPSCommand_t command, int x, int y, ucFont_t fontSize, ucTextAlign_t alignment, bool isInverted, char *szMsg);

void menuSystemInit(time_t_custom UTCdateTimeInSecs);
void menuSystemLanguageHasChanged(void);
void displayLightTrigger(bool fromKeyEvent);
void displayLightOverrideTimeout(int timeout);
int menuSystemGetLastItemIndex(int stackPos);
void menuSystemCallExitCallback(void);
void menuSystemRegisterExitCallback(menuExitCallback_t f, void *data);
void menuSystemPushNewMenu(int menuNumber);

void menuSystemSetCurrentMenu(int menuNumber);
int menuSystemGetCurrentMenuNumber(void);
int menuSystemGetPreviousMenuNumber(void);
int menuSystemGetPreviouslyPushedMenuNumber(bool keepIt);

int menuSystemGetRootMenuNumber(void);

void menuSystemPopPreviousMenu(void);
void menuSystemPopAllAndDisplayRootMenu(void);
void menuSystemPopAllAndDisplaySpecificRootMenu(int newRootMenu, bool resetKeyboard);

void menuSystemCallCurrentMenuTick(uiEvent_t *ev);
int menuGetKeypadKeyValue(uiEvent_t *ev, bool digitsOnly);
void menuUpdateCursor(int pos, bool moved, bool render);
void moveCursorLeftInString(char *str, int *pos, bool delete);
void moveCursorRightInString(char *str, int *pos, int max, bool insert);

void menuRadioInfosInit(void);
void menuRadioInfosPushBackVoltage(int32_t voltage);

void menuLockScreenPop(void);

void menuLastHeardUpdateScreen(bool showTitleOrHeader, bool displayDetails, bool isFirstRun);
void menuLastHeardInit(void); // Only used in uiTXScreen
void menuLastHeardHandleEvent(uiEvent_t *ev); // Only used in uiTXScreen

void menuPrivateCallClear(void);
void menuPrivateCallDismiss(void);

void menuHotspotRestoreSettings(void);

bool menuTxScreenDisplaysLastHeard(void);
// Sends a 5-Tone code as an automatic answer to a matching selective call.
bool uiTxScreenSendFiveToneReply(const uint8_t digits[5]);
void menuTxScreenHandleTxTermination(uiEvent_t *ev, txTerminationReason_t reason);

void menuSatelliteScreenClearPredictions(bool reloadKeps);
bool menuSatelliteIsDisplayingHeader(void);
void menuSatelliteTxScreen(uint32_t txTimeSecs);
void menuSatelliteSetFullReload(void);
void menuSystemMenuIncrement(int32_t *O, int32_t M);
void menuSystemMenuDecrement(int32_t *O, int32_t M);

// For QuickKeys
void menuDisplaySettingOption(const char *entryText, const char *valueText);


#if defined(PLATFORM_GD77S)
bool uiChannelModeTransmitDTMFContactForGD77S(void);
void uiChannelModeHeartBeatActivityForGD77S(uiEvent_t *ev);
#endif


void uiNotificationShow(uiNotificationType_t type, uiNotificationID_t id, uint32_t msTimeout, const char *message, bool immediateRender);
void uiNotificationRefresh(void);
bool uiNotificationHasTimedOut(void);
bool uiNotificationIsVisible(void);
void uiNotificationHide(bool immediateRender);
uiNotificationID_t uiNotificationGetId(void);


#if defined(HAS_GPS)
extern const uint8_t MENU_GENERAL_OPTIONS_GPS_ENTRY_NUMBER;
#endif

//
//  ---- Menu internals ----
//

/*
 * ---------------------- IMPORTANT ----------------------------
 *
 * These enums must match the menuFunctions array in menuSystem.c
 *
 * ---------------------- IMPORTANT ----------------------------
 */
enum MENU_SCREENS
{
	MENU_EMPTY = -1,
	MENU_ANY = MENU_EMPTY,
	MENU_MAIN_MENU = 0,
	MENU_CONTACTS_MENU,
	MENU_ZONE_LIST,
	MENU_RADIO_INFOS,
	MENU_RSSI_SCREEN,
	MENU_LAST_HEARD,
	MENU_OPTIONS,// Top level menu 'Options'
	MENU_GENERAL,// General options
	MENU_RADIO,// Radio options
	MENU_DISPLAY,// Display options
	MENU_SOUND,// Sound options
	MENU_SATELLITE,
#if defined(HAS_GPS)
	MENU_GPS,
#endif
	MENU_CONTACT_LIST,
	MENU_DTMF_CONTACT_LIST,
	MENU_CONTACT_QUICKLIST,
	MENU_CONTACT_LIST_SUBMENU,
	MENU_CONTACT_DETAILS,
	MENU_LANGUAGE,
	MENU_CALIBRATION,
#if defined(HAS_COLOURS)
	MENU_THEME,
#endif
	UI_CHANNEL_QUICK_MENU,
	UI_VFO_QUICK_MENU,
	MENU_CHANNEL_DETAILS,
	MENU_FIRMWARE_INFO,
#if !defined(PLATFORM_GD77S)
	MENU_APRS,
#endif
	// *** Add new menus to be accessed using quickkey (ID: 0..31) above this line ***
	UI_MESSAGE_BOX,
	UI_HOTSPOT_MODE,
	UI_CPS,
	MENU_NUMERICAL_ENTRY,
	UI_TX_SCREEN,
	UI_SPLASH_SCREEN,
	UI_POWER_OFF,
	UI_VFO_MODE,
	UI_CHANNEL_MODE,
	UI_LOCK_SCREEN,
	UI_PRIVATE_CALL,
	MENU_CONTACT_NEW,
#if defined(HAS_COLOURS)
	MENU_THEME_ITEMS_BROWSER,
	MENU_COLOUR_PICKER,
#endif
	UI_SELECTIVE_CALL_5TONE,
	UI_MORSE,
	UI_FOX_HUNT,
	MENU_GAMES,
	UI_GAME_SNAKE,
	UI_GAME_TETRIS,
	UI_GAME_BREAKOUT,
	UI_GAME_2048,
	UI_GAME_DINO,
	UI_GAME_SCORES,
	// Must stay after MENU_GAMES: applicationMain.c blocks PTT for every menu
	// from MENU_GAMES onwards, and this screen re-routes the C6000 codec.
	UI_MORSE_LOG,
	UI_APRS_LOG,
	UI_APRS_SEND,
	UI_MORSE_TRAIN,
	UI_APRS_WHATSAPP,
	UI_APRS_EMAIL,
	UI_USER_MANUAL,
	UI_CHANNEL_ZONE, // after MENU_GAMES on purpose: PTT is blocked while it writes the codeplug
	NUM_MENU_ENTRIES
};


// QuickMenus
enum CHANNEL_SCREEN_QUICK_MENU_ITEMS
{
	CH_SCREEN_QUICK_MENU_COPY2VFO = 0,
	CH_SCREEN_QUICK_MENU_COPY_FROM_VFO,
	CH_SCREEN_QUICK_MENU_FILTER_FM,
	CH_SCREEN_QUICK_MENU_FILTER_DMR,
	CH_SCREEN_QUICK_MENU_DMR_CC_SCAN,
	CH_SCREEN_QUICK_MENU_FILTER_DMR_TS,
	CH_SCREEN_QUICK_MENU_TALKAROUND,
	CH_SCREEN_QUICK_MENU_DISTANCE_SORT,
	NUM_CH_SCREEN_QUICK_MENU_ITEMS
};

enum VFO_SCREEN_QUICK_MENU_ITEMS
{
#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_RD5R) || defined(PLATFORM_DM1801A) || defined(PLATFORM_MD9600) || defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
	VFO_SCREEN_QUICK_MENU_VFO_A_B = 0,
	VFO_SCREEN_QUICK_MENU_TX_SWAP_RX,
#elif defined(PLATFORM_DM1801)
	VFO_SCREEN_QUICK_MENU_TX_SWAP_RX = 0,
#endif
	VFO_SCREEN_QUICK_MENU_BOTH_TO_RX,
	VFO_SCREEN_QUICK_MENU_BOTH_TO_TX,
	VFO_SCREEN_QUICK_MENU_FILTER_FM,
	VFO_SCREEN_QUICK_MENU_FILTER_DMR,
	VFO_SCREEN_QUICK_MENU_DMR_CC_SCAN,
	VFO_SCREEN_QUICK_MENU_FILTER_DMR_TS,
	VFO_SCREEN_QUICK_MENU_VFO_TO_NEW,
	VFO_SCREEN_QUICK_MENU_TONE_SCAN,
	VFO_SCREEN_QUICK_MENU_DUAL_SCAN,
	VFO_SCREEN_QUICK_MENU_FREQ_BIND_MODE,
	NUM_VFO_SCREEN_QUICK_MENU_ITEMS
};


// QuickKeys
#define QUICKKEY_CONTACT  0x0
#define QUICKKEY_MENU     0x1
#define QUICKKEY_TYPE(f)              ((f>>15) & 0x1)    // 1 bits
#define QUICKKEY_MENUID(f)            ((f>>10) & 0x1f)   // 5 bits
#define QUICKKEY_ENTRYID(f)           ((f>>5) & 0x1f)    // 5 bits
#define QUICKKEY_FUNCTIONID(f)        (f & 0x1f)         // 5 bits
#define QUICKKEY_LONGENTRYID(f)       (f & 0x3ff)        // 10 bits   (Zones)
#define QUICKKEY_MENUVALUE(m, e, f)   ((QUICKKEY_MENU << 15) | ((m & 0x1f) << 10) | ((e & 0x1f) << 5) | (f & 0x1f))
#define QUICKKEY_MENULONGVALUE(m, e)  ((QUICKKEY_MENU << 15) | ((m & 0x1f) << 10) | (e & 0x1ff))
#define QUICKKEY_CONTACTVALUE(c)      (c)

enum QUICK_FUNCTIONS
{
	FUNC_START_SCANNING = QUICKKEY_MENUVALUE(0, 0, 1),
	FUNC_RIGHT = 0x11,
	FUNC_LEFT = 0x12,
	FUNC_TOGGLE_TORCH = QUICKKEY_MENUVALUE(0, 0, 2),
	FUNC_REDRAW = QUICKKEY_MENUVALUE(0, 0, 3)
};

// This is used to store current position in menus. The system keeps track of its value, e.g entering in
// a submenu, it will be restored exiting that submenu. It's up to the menuFunction() to override its
// value when isFirstRun == true.

typedef struct
{
	int32_t 				currentItemIndex;
	int32_t 				startIndex;
	int32_t 				numItems;
	int 					lightTimer;
	menuItemNewData_t		*currentMenuList;
	menuControlDataStruct_t	controlData;
	char					menuOptionsSetQuickkey;
	int 					menuOptionsTimeout;
	bool 					newOptionSelected;
	// Sized, deliberately. This was a flexible array member, so it was exactly as
	// long as the initialiser list in menuSystem.c happened to be - and adding a
	// screen to MENU_SCREENS without adding a line there left the system indexing
	// past the end of the struct, taking whatever followed in memory as a pointer.
	// The radio locked up hard enough that it could not be switched off.
	//
	// With a size, a forgotten entry is simply NULL, which is what most of them
	// are anyway, and too many entries is a compile error instead of a crash.
	const menuItemsList_t	*data[NUM_MENU_ENTRIES];
} menuDataGlobal_t;

enum
{
	RADIO_INFOS_BATTERY_LEVEL = 0,
	RADIO_INFOS_CURRENT_TIME,
	RADIO_INFOS_DATE,
	RADIO_INFOS_LOCATION,
	RADIO_INFOS_TEMPERATURE_LEVEL,
	RADIO_INFOS_BATTERY_GRAPH,
	NUM_RADIO_INFOS_MENU_ITEMS,
	RADIO_INFOS_UP_TIME,
	RADIO_INFOS_TIME_ALARM
};

extern menuDataGlobal_t 		menuDataGlobal;
extern const menuItemsList_t 	menuDataMainMenu;
extern const menuItemsList_t 	menuDataContact;
extern const menuItemsList_t 	menuDataOptions;
extern bool lockscreenIsRearming;

menuStatus_t uiVFOMode(uiEvent_t *event, bool isFirstRun);
menuStatus_t uiVFOModeQuickMenu(uiEvent_t *event, bool isFirstRun);
menuStatus_t uiChannelMode(uiEvent_t *event, bool isFirstRun);
menuStatus_t uiChannelModeQuickMenu(uiEvent_t *event, bool isFirstRun);
menuStatus_t uiCPS(uiEvent_t *event, bool isFirstRun);
menuStatus_t uiSplashScreen(uiEvent_t *event, bool isFirstRun);
menuStatus_t uiPowerOff(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuZoneList(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuDisplayMenuList(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuRadioInfos(uiEvent_t *event, bool isFirstRun);

menuStatus_t menuFirmwareInfoScreen(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuNumericalEntry(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuTxScreen(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuSelectiveCall5Tone(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuMorse(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuFoxHunt(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuGames(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuGameSnake(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuGameTetris(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuGameBreakout(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuGame2048(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuGameDino(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuGameScores(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuMorseLog(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuAPRSLog(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuAPRSSend(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuAPRSWhatsApp(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuAPRSEmail(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuUserManual(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuChannelZone(uiEvent_t *event, bool isFirstRun);
void menuChannelZoneStartDelete(int channelIndex); // opens the delete confirmation for this channel
const char *menuChannelZoneDeleteLabel(void);     // "Kanali Sil" / "Delete Channel"
menuStatus_t menuMorseTrain(uiEvent_t *event, bool isFirstRun);
void uiChannelModeDrawRSSIMeterStrip(void);
void uiChannelModeDrawRxDecodeMarkers(void);
menuStatus_t menuRSSIScreen(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuLastHeard(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuGeneralOptions(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuRadioOptions(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuDisplayOptions(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuSoundOptions(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuCredits(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuChannelDetails(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuHotspotMode(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuLockScreen(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuContactList(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuContactListSubMenu(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuContactDetails(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuLanguage(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuPrivateCall(uiEvent_t *event, bool isFirstRun);
menuStatus_t uiMessageBox(uiEvent_t *event, bool isFirstRun);
menuStatus_t menuSatelliteScreen(uiEvent_t *ev, bool isFirstRun);
#if defined(HAS_GPS)
menuStatus_t menuGPS(uiEvent_t *event, bool isFirstRun);
#endif
menuStatus_t menuCalibration(uiEvent_t *ev, bool isFirstRun);
#if !defined(PLATFORM_GD77S)
menuStatus_t menuAPRSOptions(uiEvent_t *ev, bool isFirstRun);
#endif
#if defined(HAS_COLOURS)
menuStatus_t menuThemeOptions(uiEvent_t *ev, bool isFirstRun);
menuStatus_t menuThemeItemsBrowser(uiEvent_t *ev, bool isFirstRun);
menuStatus_t menuColourPicker(uiEvent_t *ev, bool isFirstRun);
#endif

#endif
