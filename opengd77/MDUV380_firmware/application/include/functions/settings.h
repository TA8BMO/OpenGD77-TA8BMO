/*
 * Copyright (C) 2019      Kai Ludwig, DG4KLU
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

#ifndef _OPENGD77_SETTINGS_H_
#define _OPENGD77_SETTINGS_H_

#include "functions/codeplug.h"
#include "functions/trx.h"

enum USB_MODE { USB_MODE_CPS, USB_MODE_HOTSPOT, USB_MODE_DEBUG };
enum SETTINGS_UI_MODE { SETTINGS_CHANNEL_MODE = 0, SETTINGS_VFO_A_MODE, SETTINGS_VFO_B_MODE };
enum BACKLIGHT_MODE { BACKLIGHT_MODE_AUTO = 0, BACKLIGHT_MODE_SQUELCH, BACKLIGHT_MODE_MANUAL, BACKLIGHT_MODE_BUTTONS, BACKLIGHT_MODE_NONE };
enum HOTSPOT_TYPE { HOTSPOT_TYPE_OFF = 0, HOTSPOT_TYPE_MMDVM, HOTSPOT_TYPE_BLUEDV };
enum CONTACT_DISPLAY_PRIO { CONTACT_DISPLAY_PRIO_CC_DB_TA = 0, CONTACT_DISPLAY_PRIO_DB_CC_TA, CONTACT_DISPLAY_PRIO_TA_CC_DB, CONTACT_DISPLAY_PRIO_TA_DB_CC };
enum SCAN_MODE { SCAN_MODE_HOLD = 0, SCAN_MODE_PAUSE, SCAN_MODE_STOP };
enum SPLIT_CONTACT { SPLIT_CONTACT_SINGLE_LINE_ONLY = 0, SPLIT_CONTACT_ON_TWO_LINES, SPLIT_CONTACT_AUTO };
enum ALLOW_PRIVATE_CALLS_MODE { ALLOW_PRIVATE_CALLS_OFF = 0, ALLOW_PRIVATE_CALLS_ON, ALLOW_PRIVATE_CALLS_PTT };//, ALLOW_PRIVATE_CALLS_AUTO };
enum BAND_LIMITS_ENUM { BAND_LIMITS_NONE = 0 , BAND_LIMITS_ON_LEGACY_DEFAULT, BAND_LIMITS_FROM_CPS };
enum INFO_ON_SCREEN { INFO_ON_SCREEN_OFF = 0x00, INFO_ON_SCREEN_TS = 0x01, INFO_ON_SCREEN_PWR = 0x02, INFO_ON_SCREEN_BOTH = 0x03 };

#if defined(HAS_GPS)
// GPS track logging to the external flash: the GPS "Log" mode, whose NMEA record
// is read back with the CPS. Off here: the top 2MB of the flash it would use
// belong to the receive recorder (voiceRecorder.c) instead. To bring it back, set
// this to 1 and rebuild - the recorder then keeps only the top 1MB, and the track
// log gets the 1MB below it (gps.c, voiceRecorder.h). LOG_GPS_DATA, and with it
// the layout of the saved settings, is left exactly as it was either way.
#define GPS_TRACK_LOG_TO_FLASH   0

typedef enum
{
	GPS_NOT_DETECTED,
	GPS_MODE_OFF,
	GPS_MODE_ON,
	GPS_MODE_ON_NMEA,
#if defined(LOG_GPS_DATA)
	GPS_MODE_ON_LOG,
#endif
	NUM_GPS_MODES,
} gpsMode_t;
#endif

#define ECO_LEVEL_MAX          5

#define SETTINGS_TIMEZONE_UTC 64
extern const uint32_t SETTINGS_UNITIALISED_LOCATION_LAT;

// Bit patterns for DMR Beep
#define BEEP_TX_NONE             0x00
#define BEEP_TX_START            0x01
#define BEEP_TX_STOP             0x02
#define BEEP_RX_CARRIER          0x04
#define BEEP_RX_TALKER           0x08
#define BEEP_RX_TALKER_BEGIN     0x10

#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_DM1801) || defined(PLATFORM_DM1801A) || defined(PLATFORM_RD5R)
#define SETTINGS_DMR_MIC_ZERO	11U
#define SETTINGS_FM_MIC_ZERO	16U
#elif defined(PLATFORM_MD9600)
#define SETTINGS_DMR_MIC_ZERO	 8U
#define SETTINGS_FM_MIC_ZERO	 6U
#else
#define SETTINGS_DMR_MIC_ZERO	 5U
#define SETTINGS_FM_MIC_ZERO	 4U
#endif

#define LOCATION_DECIMAL_PART_MULIPLIER_FIXED_32 100000
#define LOCATION_DECIMAL_PART_MULIPLIER_FIXED_24 10000

extern int settingsCurrentChannelNumber;
extern int16_t *nextKeyBeepMelody;
extern struct_codeplugChannel_t settingsVFOChannel[2];
extern struct_codeplugGeneralSettings_t settingsCodeplugGeneralSettings;

typedef enum
{
	BIT_INVERSE_VIDEO               = (1 << 0),
	BIT_PTT_LATCH                   = (1 << 1),
	BIT_UNUSED_2			        = (1 << 2),
	BIT_BATTERY_VOLTAGE_IN_HEADER   = (1 << 3),
	BIT_SETTINGS_UPDATED            = (1 << 4),
	BIT_TX_RX_FREQ_LOCK             = (1 << 5),
	BIT_ALL_LEDS_DISABLED           = (1 << 6),
	BIT_SCAN_ON_BOOT_ENABLED        = (1 << 7),
#if !defined(STM32F405xx)
	BIT_POWEROFF_SUSPEND            = (1 << 8),
#endif
	BIT_SATELLITE_MANUAL_AUTO       = (1 << 9),
	BIT_UNUSED_1			       	= (1 << 10),
#if defined(PLATFORM_MD9600)
	BIT_SPEAKER_CLICK_SUPPRESS      = (1 << 11),
#endif
	BIT_DMR_CRC_IGNORED             = (1 << 12),
	BIT_APO_WITH_RF                 = (1 << 13),
	BIT_SAFE_POWER_ON               = (1 << 14),
	BIT_AUTO_NIGHT                  = (1 << 15),
	BIT_AUTO_NIGHT_OVERRIDE         = (1 << 16),
	BIT_AUTO_NIGHT_DAYTIME          = (1 << 17),
#if defined(HAS_SOFT_VOLUME)
	BIT_VISUAL_VOLUME               = (1 << 18),
#endif
	BIT_SECONDARY_LANGUAGE          = (1 << 19),
	BIT_SORT_CHANNEL_DISTANCE       = (1 << 20),
	BIT_DISPLAY_CHANNEL_DISTANCE    = (1 << 21),
#if defined(PLATFORM_MD2017)
	BIT_TRACKBALL_ENABLED           = (1 << 22),
	BIT_TRACKBALL_FAST_MOTION       = (1 << 23),
#endif
#if defined(PLATFORM_MDUV380) && !defined(PLATFORM_VARIANT_UV380_PLUS_10W)
	BIT_FORCE_10W_RADIO             = (1 << 24),
#endif
	BIT_RX_RECORDER                 = (1 << 25), // receive recorder on (voiceRecorder.c); a bit never used before, so off on every existing radio
	BIT_LANGUAGE_SPANISH            = (1 << 26), // third built-in language; wins over BIT_SECONDARY_LANGUAGE. Never set before, so nobody's radio changes language
} bitfieldOptions_t;

#if defined(PLATFORM_MD9600)
#define RADIO_BANDS_TOTAL_NUM_SQUELCH 2
#else
#define RADIO_BANDS_TOTAL_NUM_SQUELCH 3
#endif

typedef struct
{
	uint32_t 		magicNumber;
	// The following settings won't be reset default from magicNumber 0x4761
	uint32_t		locationLat;// fixed point encoded as 1 sign bit, 8 bits integer, 23 bits as decimal
	uint32_t		locationLon;// fixed point encoded as 1 sign bit, 8 bits integer, 23 bits as decimal
	uint8_t			timezone;// Lower 7 bits are the timezone. 64 = UTC, values < 64 are negative TZ values.  Bit 8 is a flag which indicates TZ/UTC. 0 = UTC
	// -----------------------------------------------
	uint8_t			beepOptions; // 2 pairs of bits + 1 (TX and RX beeps)
	uint16_t		vfoSweepSettings; // 3bits: channel step | 5 bits: RSSI noise floor | 7bits: gain
	uint32_t		overrideTG;
	uint32_t		vfoScanLow[2]; // low frequency for VFO Scanning
	uint32_t		vfoScanHigh[2]; // High frequency for VFO Scanning
	uint32_t		bitfieldOptions; // see bitfieldOptions_t
	uint32_t		aprsBeaconingSettingsPart1[2];
#if defined(LOG_GPS_DATA)
	uint32_t		gpsLogMemOffset; // Current offset from the NMEA logging flash memory address start.
#endif
	int16_t			currentIndexInTRxGroupList[3]; // Current Channel, VFO A and VFO B
	int16_t			currentZone;
	uint16_t		userPower;
	uint16_t		tsManualOverride;
#if defined(PLATFORM_RD5R)
	int16_t			currentChannelIndexInZone;
	int16_t			currentChannelIndexInAllZone;
#else // These two has to be used on any platform but RD5R
	int16_t			UNUSED_1;
	int16_t			UNUSED_2;
#endif
	uint16_t		aprsBeaconingSettingsPart2;
	uint8_t			txPowerLevel;
	uint8_t			txTimeoutBeepX5Secs;
	uint8_t			beepVolumeDivider;
	uint8_t			micGainDMR;
	uint8_t			micGainFM;
	uint8_t			backlightMode; // see BACKLIGHT_MODE enum
	uint8_t			backLightTimeout; // 0 = never timeout. 1 - 255 time in seconds
	int8_t			displayContrast;
	int8_t			displayBacklightPercentage[NIGHT + 1];
	int8_t			displayBacklightPercentageOff; // backlight level when "off"
	uint8_t			initialMenuNumber;
	uint8_t			extendedInfosOnScreen;
	uint8_t			txFreqLimited;
	uint8_t			scanModePause;
	uint8_t			scanDelay;
	uint8_t			DMR_RxAGC;
	uint8_t			hotspotType;
	uint8_t			scanStepTime;
	uint8_t			currentVFONumber;
	uint8_t			dmrDestinationFilter;
	uint8_t			dmrCaptureTimeout;
	uint8_t			dmrCcTsFilter;
	uint8_t			analogFilterLevel;
	uint8_t    		privateCalls;
	uint8_t			contactDisplayPriority;
	uint8_t			splitContact;
	uint8_t			voxThreshold; // 0: disabled
	uint8_t			voxTailUnits; // 500ms units
	uint8_t			audioPromptMode;
	int8_t			temperatureCalibration;// Units of 0.5 deg C
	uint8_t			batteryCalibration; // Units of 0.01V (NOTE: only the 4 lower bits are used)
	uint8_t			squelchDefaults[RADIO_BANDS_TOTAL_NUM_SQUELCH]; // VHF, 200 and UHF
	uint8_t			ecoLevel;// Power saving / economy level
	uint8_t			apo; // unit: 30 minutes (5 is skipped, as we want 0, 30, 60, 90, 120 and 180)
	uint8_t			keypadTimerLong;
	uint8_t			keypadTimerRepeat;
	uint8_t			autolockTimer; // in minutes
#if defined(HAS_GPS)
	uint8_t			gps; // Off / wait for fix / On
#endif
} settingsStruct_t;

typedef enum DMR_DESTINATION_FILTER_TYPE
{
	DMR_DESTINATION_FILTER_NONE = 0,
	DMR_DESTINATION_FILTER_TG,
	DMR_DESTINATION_FILTER_DC,
	DMR_DESTINATION_FILTER_RXG,
	NUM_DMR_DESTINATION_FILTER_LEVELS
} dmrDestinationFilter_t;

// Bit patterns
#define DMR_CC_FILTER_PATTERN  0x01
#define DMR_TS_FILTER_PATTERN  0x02

// NOTE. THIS ENUM IS USED AS BIT PATTERNS, DO NOT CHANGE THE DEFINED VALUES WITHOUT UPDATING ANY CODE WHICH USES THIS ENUM
typedef enum DMR_CCTS_FILTER_TYPE
{
	DMR_CCTS_FILTER_NONE = 0,
	DMR_CCTS_FILTER_CC = DMR_CC_FILTER_PATTERN,
	DMR_CCTS_FILTER_TS = DMR_TS_FILTER_PATTERN,
	DMR_CCTS_FILTER_CC_TS = DMR_CC_FILTER_PATTERN | DMR_TS_FILTER_PATTERN,
	NUM_DMR_CCTS_FILTER_LEVELS
} dmrCcTsFilter_t;

typedef enum ANALOG_FILTER_TYPE
{
	ANALOG_FILTER_NONE = 0,
	ANALOG_FILTER_CSS,
	NUM_ANALOG_FILTER_LEVELS
} analogFilter_t;

typedef enum AUDIO_PROMPT_MODE
{
	AUDIO_PROMPT_MODE_SILENT = 0,
	AUDIO_PROMPT_MODE_BEEP,
	AUDIO_PROMPT_MODE_NO_KEY_BEEP,
	AUDIO_PROMPT_MODE_VOICE_LEVEL_1,
	AUDIO_PROMPT_MODE_VOICE_LEVEL_2,
	AUDIO_PROMPT_MODE_VOICE_LEVEL_3 ,
	NUM_AUDIO_PROMPT_MODES,
	AUDIO_PROMPT_MODE_VOICE_THRESHOLD = AUDIO_PROMPT_MODE_VOICE_LEVEL_1
} audioPromptMode_t;

typedef enum PROMPT_AUTOPLAY_THRESHOLD
{
	PROMPT_THRESHOLD_1 = AUDIO_PROMPT_MODE_VOICE_LEVEL_1,
	PROMPT_THRESHOLD_2,
	PROMPT_THRESHOLD_3,
	PROMPT_THRESHOLD_NEVER_PLAY_IMMEDIATELY
} audioPromptThreshold_t;

typedef struct
{
	volatile bool   triggered;
	volatile bool	isEnabled;
	volatile bool	qsoInfoUpdated;
	volatile bool   dmrIsValid;
	int				dmrTimeout;
	uint8_t			dmrFrameSkip;
	int 			savedRadioMode;
	uint8_t			savedSquelch;
	int 			savedDMRCcTsFilter;
	int 			savedDMRDestinationFilter;
	uint8_t 		savedDMRCc;
	int 			savedDMRTs;
} monitorModeSettingsStruct_t;

extern settingsStruct_t nonVolatileSettings;
extern struct_codeplugChannel_t *currentChannelData;
extern struct_codeplugChannel_t channelScreenChannelData;
extern struct_codeplugContact_t contactListContactData;
extern struct_codeplugDTMFContact_t contactListDTMFContactData;
extern int contactListContactIndex;
extern volatile int settingsUsbMode;
extern monitorModeSettingsStruct_t monitorModeData;

// Do not use the following settingsSet<TYPE>(...) functions, use settingsSet() instead
void settingsSetBOOL(bool *s, bool v);
void settingsSetINT8(int8_t *s, int8_t v);
void settingsSetUINT8(uint8_t *s, uint8_t v);
void settingsSetINT16(int16_t *s, int16_t v);
void settingsSetUINT16(uint16_t *s, uint16_t v);
void settingsSetINT32(int32_t *s, int32_t v);
void settingsSetUINT32(uint32_t *s, uint32_t v);

// Do not use the following settingsInc/Dec<TYPE>(...) functions, use settingsIncrement/Decrement() instead
void settingsIncINT8(int8_t *s, int8_t v);
void settingsIncUINT8(uint8_t *s, uint8_t v);
void settingsIncINT16(int16_t *s, int16_t v);
void settingsIncUINT16(uint16_t *s, uint16_t v);
void settingsIncINT32(int32_t *s, int32_t v);
void settingsIncUINT32(uint32_t *s, uint32_t v);

void settingsDecINT8(int8_t *s, int8_t v);
void settingsDecUINT8(uint8_t *s, uint8_t v);
void settingsDecINT16(int16_t *s, int16_t v);
void settingsDecUINT16(uint16_t *s, uint16_t v);
void settingsDecINT32(int32_t *s, int32_t v);
void settingsDecUINT32(uint32_t *s, uint32_t v);

// Workaround for Eclipse's CDT parser angriness because it doesn't support C11 yet
#ifdef __CDT_PARSER__
#define settingsSet(S, V) do { /* It uses C11's _Generic() at compile time */ S = V; } while(0)
#else
#define settingsSet(S, V) _Generic((S),   \
	bool:     settingsSetBOOL,            \
	int8_t:   settingsSetINT8,            \
	uint8_t:  settingsSetUINT8,           \
	int16_t:  settingsSetINT16,           \
	uint16_t: settingsSetUINT16,          \
	int32_t:  settingsSetINT32,           \
	uint32_t: settingsSetUINT32           \
	)(&S, V)
#endif

#ifdef __CDT_PARSER__
#define settingsIncrement(S, V) do { /* It uses C11's _Generic() at compile time */ S += V; } while(0)
#else
#define settingsIncrement(S, V) _Generic((S),   \
	int8_t:   settingsIncINT8,            \
	uint8_t:  settingsIncUINT8,           \
	int16_t:  settingsIncINT16,           \
	uint16_t: settingsIncUINT16,          \
	int32_t:  settingsIncINT32,           \
	uint32_t: settingsIncUINT32           \
	)(&S, V)
#endif

#ifdef __CDT_PARSER__
#define settingsDecrement(S, V) do { /* It uses C11's _Generic() at compile time */ S -= V; } while(0)
#else
#define settingsDecrement(S, V) _Generic((S),   \
	int8_t:   settingsDecINT8,            \
	uint8_t:  settingsDecUINT8,           \
	int16_t:  settingsDecINT16,           \
	uint16_t: settingsDecUINT16,          \
	int32_t:  settingsDecINT32,           \
	uint32_t: settingsDecUINT32           \
	)(&S, V)
#endif

void settingsSetOptionBit(bitfieldOptions_t bit, bool set);
bool settingsIsOptionBitSetFromSettings(settingsStruct_t *sets, bitfieldOptions_t bit);
bool settingsIsOptionBitSet(bitfieldOptions_t bit);

void settingsSetDirty(void);
void settingsSetVFODirty(void);
void settingsSaveIfNeeded(bool immediately);
bool settingsSaveSettings(bool includeVFOs);
bool settingsLoadSettings(bool reset);
bool settingsRestoreDefaultSettings(void);
void settingsEraseCustomContent(void);
//void settingsInitVFOChannel(int vfoNumber);
void enableVoicePromptsIfLoaded(bool enableFullPrompts);
int settingsGetScanStepTimeMilliseconds(void);

#endif
