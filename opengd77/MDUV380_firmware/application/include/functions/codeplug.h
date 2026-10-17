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
#ifndef _OPENGD77_CODEPLUG_H_
#define _OPENGD77_CODEPLUG_H_

#include <stdint.h>
#include <stdbool.h>

extern const int CODEPLUG_ADDR_CHANNEL_HEADER_EEPROM;

extern const int CODEPLUG_MAX_VARIABLE_SQUELCH;
extern const int CODEPLUG_MIN_VARIABLE_SQUELCH;
extern const int CODEPLUG_MIN_PER_CHANNEL_POWER;

extern const int VFO_FREQ_STEP_TABLE[8];
extern const int VFO_SWEEP_SCAN_RANGE_SAMPLE_STEP_TABLE[7];

#define CODEPLUG_ADDR_QUICKKEYS                                0x7524 // LSB,HSB
#define CODEPLUG_QUICKKEYS_SIZE                                    10
extern const int CODEPLUG_ADDR_VFO_A_CHANNEL;
extern const int CPS_CODEPLUG_SECTION_2_START;


// Zone
#define CODEPLUG_ZONE_DATA_ORIGINAL_STRUCT_SIZE                    48 // 16 channels per zone
#define CODEPLUG_ZONE_DATA_OPENGD77_STRUCT_SIZE                   176 // 80 channels per zone
#define ZONE_DATA_STRUCT_SIZE                                     184
// (NOTE: **DO NOT REMOVE THE UNSIGNED SUFFIX** of the following two constants
#define CODEPLUG_ZONES_MAX                                         68U // Max number of zones
#define CODEPLUG_ALL_ZONES_MAX                                     (1U + CODEPLUG_ZONES_MAX) // All channels + every zones

// Channel
#define CODEPLUG_CHANNEL_DATA_STRUCT_SIZE                          56
#define CHANNEL_DATA_STRUCT_SIZE                                   57
#define CODEPLUG_CHANNEL_FLAG4_OFFSET                              51

// RXGroup
#define CODEPLUG_RXGROUP_DATA_STRUCT_SIZE                          80

// Contact
#define CODEPLUG_CONTACT_DATA_SIZE                                 24
#define CONTACT_DATA_STRUCT_SIZE                                   28

// DTMF Contact
#define CODEPLUG_DTMF_CONTACT_DATA_STRUCT_SIZE                     32

// General Settings
#define CODEPLUG_GENERAL_SETTINGS_DATA_STRUCT_SIZE                 40

// DTMF Signalling
#define CODEPLUG_SIGNALLING_DTMF_DATA_STRUCT_SIZE                 120
#define SIGNALLING_DTMF_DURATIONS_DATA_STRUCT_SIZE                  5

// Last Used Channel in Zone
#define CODEPLUG_ADDR_LUCZ                                     0x6000


#define CODEPLUG_CONTACTS_MIN                                       1
#define CODEPLUG_CONTACTS_MAX                                    1024

#define CODEPLUG_CONTACT_FLAG_NO_TS_OVERRIDE                      0x01
#define CODEPLUG_CONTACT_FLAG_TS_OVERRIDE_TIMESLOT_MASK           0x02
#define CODEPLUG_CONTACT_FLAG_TS_OVERRIDE_MASK                    0x03

#define CODEPLUG_RX_GROUPLIST_MAX                                  76

#define CODEPLUG_CHANNELS_MIN                                       1
#define CODEPLUG_CHANNELS_MAX                                    1024
#define CODEPLUG_CHANNELS_BANKS_MAX                                 8
#define CODEPLUG_CHANNELS_PER_BANK                                128

#define CODEPLUG_DTMF_CONTACTS_MIN                                  1
#define CODEPLUG_DTMF_CONTACTS_MAX                                 63

#define CODEPLUG_CSS_TONE_NONE                                 0xFFFF
typedef enum
{
	CSS_TYPE_NONE          = (1 << 0),
	CSS_TYPE_CTCSS         = (1 << 1),
	CSS_TYPE_DCS           = (1 << 15),
	CSS_TYPE_DCS_INVERTED  = (1 << 14),
	CSS_TYPE_DCS_MASK      = (CSS_TYPE_DCS | CSS_TYPE_DCS_INVERTED)
} CodeplugCSSTypes_t;

typedef enum
{
	// LibreDMR_flag1
	CHANNEL_FLAG_OPTIONAL_DMRID = 0,
	CHANNEL_FLAG_NO_BEEP,
	CHANNEL_FLAG_NO_ECO,
	CHANNEL_FLAG_OUT_OF_BAND, // MD-9600 Only
	CHANNEL_FLAG_USE_LOCATION,
	CHANNEL_FLAG_FORCE_DMO,
	// flag2
	CHANNEL_FLAG_TIMESLOT_TWO,
	// flag3
	CHANNEL_FLAG_STE,
	CHANNEL_FLAG_NON_STE,
	// flag4
	CHANNEL_FLAG_POWER,
	CHANNEL_FLAG_VOX,
	CHANNEL_FLAG_ZONE_SKIP,
	CHANNEL_FLAG_ALL_SKIP,
	CHANNEL_FLAG_RX_ONLY,
	CHANNEL_FLAG_BW_25K,
	CHANNEL_FLAG_SQUELCH,
} ChannelFlag_t;

// LibreDMR_flag1
#define CODEPLUG_CHANNEL_LIBREDMR_FLAG1_OPTIONAL_DMRID           0x80
#define CODEPLUG_CHANNEL_LIBREDMR_FLAG1_NO_BEEP                  0x40
#define CODEPLUG_CHANNEL_LIBREDMR_FLAG1_NO_ECO                   0x20
#define CODEPLUG_CHANNEL_LIBREDMR_FLAG1_OUT_OF_BAND              0x10 // MD-9600 Only
#define CODEPLUG_CHANNEL_LIBREDMR_FLAG1_USE_LOCATION             0x08
#define CODEPLUG_CHANNEL_LIBREDMR_FLAG1_FORCE_DMO                0x04
// flag2
#define CODEPLUG_CHANNEL_FLAG2_TIMESLOT_TWO                      0x40
// flag3
#define CODEPLUG_CHANNEL_FLAG3_STE                               0xC0
#define CODEPLUG_CHANNEL_FLAG3_NON_STE                           0x20
// flag4
#define CODEPLUG_CHANNEL_FLAG4_SQUELCH                           0x01
#define CODEPLUG_CHANNEL_FLAG4_BW_25K                            0x02
#define CODEPLUG_CHANNEL_FLAG4_RX_ONLY                           0x04
#define CODEPLUG_CHANNEL_FLAG4_ALL_SKIP                          0x10
#define CODEPLUG_CHANNEL_FLAG4_ZONE_SKIP                         0x20
#define CODEPLUG_CHANNEL_FLAG4_VOX                               0x40
#define CODEPLUG_CHANNEL_FLAG4_POWER                             0x80

extern int codeplugChannelsPerZone;

#if defined(PLATFORM_MD9600) || defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
#define FLASH_ADDRESS_OFFSET  (128 * 1024)
#else
#define FLASH_ADDRESS_OFFSET  (0)
#endif

enum CONTACT_CALLTYPE_SELECT
{
	CONTACT_CALLTYPE_TG = 0,
	CONTACT_CALLTYPE_PC,
	CONTACT_CALLTYPE_ALL
};

typedef enum
{
     CHANNEL_VFO_A = 0,
     CHANNEL_VFO_B,
     CHANNEL_CHANNEL
} Channel_t;

// AllChannels zone indexNumber is -1
#define CODEPLUG_ZONE_IS_ALLCHANNELS(z) ((z).NOT_IN_CODEPLUGDATA_indexNumber < 0)


typedef struct
{
	char		name[16];
	uint16_t	channels[80];// 16 for the original codeplug, but set this to  80 to allow for the new codeplug zones format
	int			NOT_IN_CODEPLUGDATA_numChannelsInZone;// This property is not part of the codeplug data, its initialised by the code
	int			NOT_IN_CODEPLUGDATA_highestIndex; // Highest index in the array of channels
	int			NOT_IN_CODEPLUGDATA_indexNumber;// This property is not part of the codeplug data, its initialised by the code. Index >= 0 are real zone. -1 indicates the virtual "All channels" zone
} struct_codeplugZone_t;

typedef struct
{
	char name[16];
	uint32_t rxFreq;
	uint32_t txFreq;
	uint8_t chMode;
	uint8_t libreDMR_Power;
	uint8_t locationLat0;// Latitude LS byte
	uint8_t tot;
	uint8_t locationLat1;// Latitude
	uint8_t locationLat2;// Latitude MS byte
	uint8_t locationLon0;// Longitude LS byte
	uint8_t locationLon1;
	uint16_t rxTone;
	uint16_t txTone;
	uint8_t locationLon2;// Latitude MS byte
	uint8_t _UNUSED_1;
	uint8_t LibreDMR_flag1; // was unmuteRule. 0x80: Optional DMRID sets, 0x40: no beep, 0x20: no Eco, 0x10: OutOfBand(MD9600 only, never saved in codeplug)
	uint8_t rxSignaling;    // +--
	uint8_t artsInterval;   // | These 3 bytes were repurposed for optional DMRID
	uint8_t encrypt;        // +--
	uint8_t _UNUSED_2;
	uint8_t rxGroupList; // a.k.a TG List
	uint8_t txColor;
	uint8_t aprsConfigIndex;
	uint16_t contact;
	uint8_t flag1;// lower 4 bits TA Tx control
	uint8_t flag2; // bits... 0x40 = TS
	uint8_t flag3;// bits... 0x20 = NonSTE, 0xC0 = STE
	uint8_t flag4;// bits... 0x80 = Power, 0x40 = Vox, 0x20 = ZoneSkip (AutoScan), 0x10 = AllSkip (LoneWoker), 0x08 = AllowTalkaround(UNUSED), 0x04 = OnlyRx, 0x02 = Channel width, 0x01 = Squelch
	uint16_t VFOoffsetFreq;// NOT USED ?
	uint8_t VFOflag5;// upper 4 bits are the step frequency 2.5,5,6.25,10,12.5,25,30,50kHz
	uint8_t sql;// Does not seem to be used in the official firmware and seems to be always set to 0
	uint8_t NOT_IN_CODEPLUG_flag; // bit 0x01 = vfo channel
	int	NOT_IN_CODEPLUG_CALCULATED_DISTANCE_X10;// -1 = no distance available
} struct_codeplugChannel_t;

typedef enum
{
	TA_TX_OFF = 0,
	TA_TX_APRS,
	TA_TX_TEXT,
	TA_TX_BOTH
} taTxEnum_t;

typedef struct
{
	char name[16];
	uint16_t contacts[32];
	int	NOT_IN_CODEPLUG_numTGsInGroup;// NOT IN THE
	uint32_t NOT_IN_CODEPLUG_contactsTG[32];
} struct_codeplugRxGroup_t;

typedef struct
{
	char 		name[16];
	uint32_t 	tgNumber;
	uint8_t		callType;
	uint8_t		callRxTone;
	uint8_t		ringStyle;
	uint8_t		reserve1; // TS override: bit0 1 = no TS override, bit1 + 1: timeslot override value
	int         NOT_IN_CODEPLUGDATA_indexNumber;
} struct_codeplugContact_t;

typedef struct
{
	char       name[16];
	uint8_t    code[16];
} struct_codeplugDTMFContact_t;

typedef struct
{
	char 		radioName[8];// callsign
	uint32_t	radioId;// DMD Id
	uint8_t		libreDMRCodeplugVersion;
	uint8_t		reserve[3];// was 4 bytes of reserved data but we use the first byte as the codeplug version
	uint8_t		arsInitDly;
	uint8_t		txPreambleDur;
	uint8_t		monitorType;
	uint8_t		voxSense;
	uint8_t		rxLowBatt;
	uint8_t		callAlertDur;
	uint8_t		respTmr;
	uint8_t		reminderTmr;
	uint8_t		grpHang;
	uint8_t		privateHang;
	uint8_t		flag1;
	uint8_t		flag2;
	uint8_t		flag3;
	uint8_t		flag4;
	uint8_t		reserver2[2];
	char 		prgPwd[8];
} struct_codeplugGeneralSettings_t;

typedef struct
{
	 char		selfId[8];
	 char		killCode[16];
	 char		wakeCode[16];
	 uint8_t	delimiter;
	 uint8_t	groupCode;
	 uint8_t	decodeResp;
	 uint8_t	autoResetTimer;
	 uint8_t	flag1;
	 uint8_t	reserve1[3];
	 char		pttidUpCode[30];
	 uint16_t	reserve2;
	 char		pttidDownCode[30];
	 uint16_t	reserve3;
	 uint8_t	respHoldTime;
	 uint8_t	decTime;
	 uint8_t	fstDigitDly;
	 uint8_t	fstDur;
	 uint8_t	otherDur;
	 uint8_t	rate;
	 uint8_t	flag2;
	 uint8_t	reserve4;
} struct_codeplugSignalling_DTMF_t;


typedef struct
{
	uint8_t 	fstDigitDly;	// First Digit Delay (unit: hundreds of ms)
	uint8_t 	fstDur;			// First Digit Duration (unit: hundreds of ms)
	uint8_t 	otherDur;		// * and # Duration (unit: hundreds of ms)
	uint8_t 	rate;			// DTMF Rate: 1 .. 10 digits / second
	uint8_t		libreDMR_Tail;	// TX Tail duration (unit: hundreds of ms)
} struct_codeplugSignalling_DTMFDurations_t;

typedef struct
{
	uint16_t	minUHFFreq;
	uint16_t	maxUHFFreq;
	uint16_t	minVHFFreq;
	uint16_t	maxVHFFreq;
	uint8_t		lastPrgTime[6];
	uint16_t	reserve2;
	uint8_t		model[8];
	uint8_t		sn[16];
	uint8_t		cpsSwVer[8];
	uint8_t		hardwareVer[8];
	uint8_t		firmwareVer[8];
	uint8_t		dspFwVer[24];
	uint8_t		reserve3[8];
} struct_codeplugDeviceInfo_t;


typedef enum
{
	CODEPLUG_CUSTOM_DATA_TYPE_EMPTY = 0xFFFFFFFF,
	CODEPLUG_CUSTOM_DATA_TYPE_IMAGE = 1,
	CODEPLUG_CUSTOM_DATA_TYPE_BEEP,
	CODEPLUG_CUSTOM_DATA_TYPE_SATELLITE_TLE,
	CODEPLUG_CUSTOM_DATA_TYPE_THEME_DAY,
	CODEPLUG_CUSTOM_DATA_TYPE_THEME_NIGHT,
} codeplugCustomDataType_t;


typedef struct
{
	char 		name[6];
	uint8_t		SSID;
} APRS_Paths_t;

typedef struct
{
	char 			name[8];
	uint8_t 		senderSSID;
	char			latitude[3];
	char			longitude[3];
	APRS_Paths_t	paths[2];
	uint8_t			iconTable;
	uint8_t			iconIndex;
	char 			comment[24];
	uint8_t			reserved[6];
	uint8_t			flags;
	uint16_t		magicVer;
} 	codeplugAPRS_Config_t;


/*
 * deprecated. Use our own non volatile storage instead
 *
void codeplugZoneGetSelected(int *selectedZone,int *selectedChannel);
void codeplugZoneSetSelected(int selectedZone,int selectedChannel);
 */
void codeplugZonesInitCache(void);
int codeplugZonesGetCount(void);
bool codeplugZoneGetDataForNumber(int indexNum,struct_codeplugZone_t *returnBuf);
uint32_t codeplugChannelGetOptionalDMRID(struct_codeplugChannel_t *channelBuf);
void codeplugChannelSetOptionalDMRID(struct_codeplugChannel_t *channelBuf, uint32_t dmrID);
uint8_t codeplugChannelGetFlag(struct_codeplugChannel_t *channelBuf, ChannelFlag_t flag);
uint8_t codeplugChannelSetFlag(struct_codeplugChannel_t *channelBuf, ChannelFlag_t flag, uint8_t value);

// --- 5-Tone ANI (firmware-only feature, analog FM channels) ---
// Per-channel fixed-length 5-digit code + ANI transmit mode. Stored WITHOUT
// changing the 56-byte on-disk channel record, so CPS binary compatibility and
// struct alignment are preserved. Layout (reuses otherwise-unused bytes):
//   VFOoffsetFreq (offset 52-53): 4 BCD digits d0..d3 (d0=bits12-15 ... d3=bits0-3)
//   _UNUSED_1     (offset 37)   : high nibble = d4, low nibble = ANI mode
// Digits are stored as BCD nibbles so leading zeros are always preserved
// (e.g. "03310" is stored as digits {0,3,3,1,0}, never collapsed to an int).
// NOTE: OpenGD77 CPS zeroes these bytes on write, so a CPS codeplug upload
//       reverts the value to OFF/00000 and it must be re-entered on the radio.
typedef enum
{
	ANI_MODE_OFF = 0,
	ANI_MODE_PTT_START,
	ANI_MODE_PTT_END,
	ANI_MODE_PTT_BOTH,
	ANI_MODE_MAX
} aniTxMode_t;

// Fills digitsOut[5] with digits 0..9 (invalid/garbage nibbles are clamped to 0).
// Returns the per-channel ANI transmit mode.
aniTxMode_t codeplugChannelGetFiveTone(struct_codeplugChannel_t *channelBuf, uint8_t digitsOut[5]);
// Stores digits[5] (each expected 0..9) and the ANI transmit mode.
void codeplugChannelSetFiveTone(struct_codeplugChannel_t *channelBuf, const uint8_t digits[5], aniTxMode_t mode);
// Per-channel automatic answer: when a received selective call matches this
// channel's own ANI code, transmit that code back once. Stored in bit 0 of
// _UNUSED_2 (offset 42), independent of the ANI transmit mode above.
bool codeplugChannelGetFiveToneReply(struct_codeplugChannel_t *channelBuf);
void codeplugChannelSetFiveToneReply(struct_codeplugChannel_t *channelBuf, bool enabled);
void codeplugChannelGetDataWithOffsetAndLengthForIndex(int index, struct_codeplugChannel_t *channelBuf, uint8_t offset, int length);
void codeplugChannelGetDataForIndex(int index, struct_codeplugChannel_t *channelBuf);
void codeplugUtilConvertBufToString(char *codeplugBuf, char *outBuf, int len);
void codeplugUtilConvertStringToBuf(char *inBuf, char *outBuf, int len);
uint32_t byteSwap32(uint32_t n);
uint32_t bcd2int(uint32_t i);
uint32_t int2bcd(uint32_t i);
uint16_t codeplugCSSToInt(uint16_t css);
uint16_t codeplugIntToCSS(uint16_t i);

bool codeplugRxGroupGetDataForIndex(int index, struct_codeplugRxGroup_t *rxGroupBuf);

bool codeplugContactGetDataForIndex(int index, struct_codeplugContact_t *contact);
bool codeplugDTMFContactGetDataForIndex(int index, struct_codeplugDTMFContact_t *contact);

uint32_t codeplugGetUserDMRID(void);
void codeplugSetUserDMRID(uint32_t dmrId);
void codeplugGetRadioName(char *buf);
void codeplugGetBootScreenData(char *line1, char *line2, uint8_t *displayType);
void codeplugGetVFO_ChannelData(struct_codeplugChannel_t *vfoBuf, Channel_t VFONumber);
void codeplugSetVFO_ChannelData(struct_codeplugChannel_t *vfoBuf, Channel_t VFONumber);
bool codeplugAllChannelsIndexIsInUse(int index);
void codeplugAllChannelsIndexSetUsed(int index);
bool codeplugChannelSaveDataForIndex(int index, struct_codeplugChannel_t *channelBuf);
CodeplugCSSTypes_t codeplugGetCSSType(uint16_t tone);
void codeplugConvertChannelInternalToCodeplug(struct_codeplugChannel_t *codeplugChannel, struct_codeplugChannel_t *internalChannel);


int codeplugDTMFContactsGetCount(void);
int codeplugContactsGetCount(uint32_t callType);
int codeplugContactGetDataForNumberInType(int number, uint32_t callType, struct_codeplugContact_t *contact);
int codeplugDTMFContactGetDataForNumber(int number, struct_codeplugDTMFContact_t *contact);
// Name of the DTMF contact whose code is a received 5-Tone/DTMF code. nameOut: 17 bytes.
bool codeplugDTMFContactFindNameForCode(const char *code, char *nameOut);
// On-radio editing of DTMF contacts (5T Contacts menu).
void codeplugDTMFContactsInitCache(void);
void codeplugDTMFContactCodeToString(const uint8_t *code, char *out); // out: 17 bytes, "0-9A-D*#"
int codeplugDTMFContactWrite(int index, const char *name, const char *code); // index 0 = first free; returns slot or 0
bool codeplugDTMFContactErase(int index);
int codeplugContactIndexByTGorPCFromNumber(int number, uint32_t tgorpc, uint32_t callType, struct_codeplugContact_t *contact, uint8_t optionalTS);
int codeplugContactIndexByTGorPC(uint32_t tgorpc, uint32_t callType, struct_codeplugContact_t *contact, uint8_t optionalTS);
int codeplugContactSaveDataForIndex(int index, struct_codeplugContact_t *contact);
uint32_t codeplugContactGetPackedId(struct_codeplugContact_t *contact);
int codeplugContactGetFreeIndex(void);
bool codeplugContactGetRXGroup(int index);
void codeplugInitChannelsPerZone(void);
bool codeplugGetOpenGD77CustomData(codeplugCustomDataType_t dataType, uint8_t *dataBuf);
bool codeplugSetOpenGD77CustomData(codeplugCustomDataType_t dataType, uint8_t *dataBuf, int len);

void codeplugAllChannelsInitCache(void);
void codeplugInitCaches(void);

bool codeplugContactsContainsPC(uint32_t pc);
bool codeplugGetGeneralSettings(struct_codeplugGeneralSettings_t *generalSettingsBuffer);
bool codeplugGetSignallingDTMF(struct_codeplugSignalling_DTMF_t *signallingDTMFBuffer);
bool codeplugGetSignallingDTMFDurations(struct_codeplugSignalling_DTMFDurations_t *signallingDTMFDurationsBuffer);
bool codeplugZoneAddChannelToZoneAndSave(int channelIndex, struct_codeplugZone_t *zoneBuf);
int codeplugZoneCreate(const char *name); // number of the new zone, or -1
bool codeplugChannelDelete(int channelIndex);
bool codeplugZoneDelete(int zoneNum);
bool codeplugZoneRename(int zoneNum, const char *name);
bool codeplugZoneRemoveChannel(int zoneNum, int channelIndex);
bool codeplugSetPasswordPin(int32_t pin, int len); // len 0 turns the power-on password off
bool codeplugGetDeviceInfo(struct_codeplugDeviceInfo_t *deviceInfoBuffer);

uint16_t codeplugGetQuickkeyFunctionID(char key);
bool codeplugSetQuickkeyFunctionID(char key, uint16_t functionId);

int codeplugGetRepeaterWakeAttempts(void);
int codeplugGetPasswordPin(int32_t *pinCode);

void codeplugSetTATxForTS(struct_codeplugChannel_t *channelBuf, uint8_t ts, taTxEnum_t taTXValue);
taTxEnum_t codeplugGetTATxForTS(struct_codeplugChannel_t *channelBuf, uint8_t ts);

void codeplugInitLastUsedChannelInZone(void);
int16_t codeplugGetLastUsedChannelInZone(int zoneNum);
int16_t codeplugGetLastUsedChannelInCurrentZone(void);
int16_t codeplugGetLastUsedChannelNumberInCurrentZone(void);
int16_t codeplugSetLastUsedChannelInZone(int zoneNum, int16_t channelNum);
int16_t codeplugSetLastUsedChannelInCurrentZone(int16_t channelNum);
bool codeplugSaveLastUsedChannelInZone(void);

int codeplugAPRSConfigGetCount(void);
bool codeplugAPRSGetDataForIndex(int index, codeplugAPRS_Config_t *APRS_Buf);
bool codeplugAPRSSetDataForIndex(int index, codeplugAPRS_Config_t *config); // index up to count + 1
bool codeplugAPRSDeleteIndex(int index); // 1..count; later configs move up, channels are renumbered to match
bool codeplugSetRadioName(const char *name);
int codeplugAPRSGetIndexOfName(char *configName);

#endif
