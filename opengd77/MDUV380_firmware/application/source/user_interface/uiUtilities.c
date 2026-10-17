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

#include <math.h>
#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_DM1801) || defined(PLATFORM_DM1801A) || defined(PLATFORM_RD5R)
#include "hardware/EEPROM.h"
#endif
#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "hardware/SPI_Flash.h"
#include "functions/trx.h"
#include "functions/rxPowerSaving.h"
#if defined(PLATFORM_MD9600) || defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
#include "interfaces/batteryAndPowerManagement.h"
#include "hardware/radioHardwareInterface.h"
#endif

#if defined(HAS_GPS)
#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
#include "interfaces/gps.h"
#endif
#endif

static const uint8_t DECOMPRESS_LUT[64] = { ' ', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '.' };

#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
static  __attribute__((section(".ccmram")))
#else // MD9600 and MK22
static  __attribute__((section(".data.$RAM2")))
#endif
LinkItem_t callsList[NUM_LASTHEARD_STORED];

static uint32_t dmrIdDataArea_1_Size;
const uint32_t DMRID_HEADER_LENGTH = 0x0C;
const uint32_t DMRID_MEMORY_LOCATION_1 = 0x30000 + FLASH_ADDRESS_OFFSET;
const uint32_t DMRID_MEMORY_LOCATION_2 = 0xB8000 + FLASH_ADDRESS_OFFSET;
uint32_t dmrIDDatabaseMemoryLocation2 = DMRID_MEMORY_LOCATION_2;

static dmrIDsCache_t dmrIDsCache;
static uint32_t lastTG = 0;

volatile uint32_t lastID = 0;// This needs to be volatile as lastHeardClearLastID() is called from an ISR
LinkItem_t *LinkHead = callsList;

DECLARE_SMETER_ARRAY(rssiMeterHeaderBar, DISPLAY_SIZE_X);

static uint32_t DMRID_IdLength = 4U;

static uint8_t bufferTA[32] = { 0 };
static uint8_t blocksTA = 0x00;
static bool overrideTA = false;
static bool contactDefinedForTA = false; // lockout TA data storage until a valid DMR ID is received.

static void announceChannelNameOrVFOFrequency(bool voicePromptWasPlaying, bool announceVFOName);
static void dmrDbTextDecode(uint8_t *compressedBufIn, uint8_t *decompressedBufOut, int compressedSize);

// Set TS manual override
// chan: CHANNEL_VFO_A, CHANNEL_VFO_B, CHANNEL_CHANNEL
// ts: 1, 2, TS_NO_OVERRIDE
void tsSetManualOverride(Channel_t chan, int8_t ts)
{
	uint16_t tsOverride = nonVolatileSettings.tsManualOverride;

	// Clear TS override for given channel
	tsOverride &= ~(0x03 << (2 * ((int8_t)chan)));
	if (ts != TS_NO_OVERRIDE)
	{
		// Set TS override for given channel
		tsOverride |= (ts << (2 * ((int8_t)chan)));
	}

	settingsSet(nonVolatileSettings.tsManualOverride, tsOverride);
}

// Set TS manual override from contact TS override
// chan: CHANNEL_VFO_A, CHANNEL_VFO_B, CHANNEL_CHANNEL
// contact: apply TS override from contact setting
void tsSetFromContactOverride(Channel_t chan, struct_codeplugContact_t *contact)
{
	if ((contact->reserve1 & CODEPLUG_CONTACT_FLAG_NO_TS_OVERRIDE) == 0x00)
	{
		tsSetManualOverride(chan, (((contact->reserve1 & CODEPLUG_CONTACT_FLAG_TS_OVERRIDE_TIMESLOT_MASK) >> 1) + 1));
	}
	else
	{
		tsSetManualOverride(chan, TS_NO_OVERRIDE);
	}
}

// Get TS override value
// chan: CHANNEL_VFO_A, CHANNEL_VFO_B, CHANNEL_CHANNEL
// returns (TS + 1, 0 no override)
int8_t tsGetManualOverride(Channel_t chan)
{
	return (nonVolatileSettings.tsManualOverride >> (2 * (int8_t)chan)) & 0x03;
}

// Get manually overridden TS, if any, from currentChannelData
// returns (TS + 1, 0 no override)
int8_t tsGetManualOverrideFromCurrentChannel(void)
{
	Channel_t chan = (((currentChannelData->NOT_IN_CODEPLUG_flag & 0x01) == 0x01) ?
			(((currentChannelData->NOT_IN_CODEPLUG_flag & 0x02) == 0x02) ? CHANNEL_VFO_B : CHANNEL_VFO_A) : CHANNEL_CHANNEL);

	return tsGetManualOverride(chan);
}

// Check if TS is overrode
// chan: CHANNEL_VFO_A, CHANNEL_VFO_B, CHANNEL_CHANNEL
// returns true on overrode for the specified channel
bool tsIsManualOverridden(Channel_t chan)
{
	return (nonVolatileSettings.tsManualOverride & (0x03 << (2 * ((int8_t)chan))));
}

// Keep track of an override when the selected contact has a TS override set
// chan: CHANNEL_VFO_A, CHANNEL_VFO_B, CHANNEL_CHANNEL
void tsSetContactHasBeenOverriden(Channel_t chan, bool isOverriden)
{
	uint16_t tsOverride = nonVolatileSettings.tsManualOverride;

	if (isOverriden)
	{
		tsOverride |= (1 << ((3 * 2) + ((int8_t)chan)));
	}
	else
	{
		tsOverride &= ~(1 << ((3 * 2) + ((int8_t)chan)));
	}

	settingsSet(nonVolatileSettings.tsManualOverride, tsOverride);
}

// Get TS override status, of a selected contact which have a TS override set
// chan: CHANNEL_VFO_A, CHANNEL_VFO_B, CHANNEL_CHANNEL
bool tsIsContactHasBeenOverriddenFromCurrentChannel(void)
{
	Channel_t chan = (((currentChannelData->NOT_IN_CODEPLUG_flag & 0x01) == 0x01) ?
			(((currentChannelData->NOT_IN_CODEPLUG_flag & 0x02) == 0x02) ? CHANNEL_VFO_B : CHANNEL_VFO_A) : CHANNEL_CHANNEL);

	return tsIsContactHasBeenOverridden(chan);
}

// Get manual TS override of the selected contact (which has a TS override set)
// chan: CHANNEL_VFO_A, CHANNEL_VFO_B, CHANNEL_CHANNEL
bool tsIsContactHasBeenOverridden(Channel_t chan)
{
	return (nonVolatileSettings.tsManualOverride >> ((3 * 2) + (int8_t)chan)) & 0x01;
}

bool isQSODataAvailableForCurrentTalker(void)
{
	LinkItem_t *item = NULL;
	uint32_t rxID = HRC6000GetReceivedSrcId();

	// We're in digital mode, RXing, and current talker is already at the top of last heard list,
	// hence immediately display complete contact/TG info on screen
	if ((trxTransmissionEnabled == false) && ((trxGetMode() == RADIO_MODE_DIGITAL) && (rxID != 0) && (lastID != 0) && (HRC6000GetReceivedTgOrPcId() != 0)) &&
			(getAudioAmpStatus() & AUDIO_AMP_MODE_RF)
			&& HRC6000CheckTalkGroupFilter() &&
			(((item = lastHeardFindInList(rxID)) != NULL) && (item == LinkHead)))
	{
		return true;
	}

	return false;
}

#if 0
int alignFrequencyToStep(int freq, int step)
{
	int r = freq % step;

	return (r ? freq + (step - r) : freq);
}
#endif

/*
 * Remove space at the end of the array, and return pointer to first non space character
 */
char *chomp(char *str)
{
	char *sp = str, *ep = str;

	while (*ep != '\0')
	{
		ep++;
	}

	// Spaces at the end
	while (ep > str)
	{
		if (*ep == '\0')
		{
		}
		else if (*ep == ' ')
		{
			*ep = '\0';
		}
		else
		{
			break;
		}

		ep--;
	}

	// Spaces at the beginning
	while (*sp == ' ')
	{
		sp++;
	}

	return sp;
}

int32_t getFirstSpacePos(char *str)
{
	char *p = str;

	while(*p != '\0')
	{
		if (*p == ' ')
		{
			return (p - str);
		}

		p++;
	}

	return -1;
}

void lastHeardInitList(void)
{
	LinkHead = callsList;

	for(int i = 0; i < NUM_LASTHEARD_STORED; i++)
	{
		callsList[i].id = 0;
		callsList[i].talkGroupOrPcId = 0;
		callsList[i].contact[0] = 0;
		callsList[i].talkgroup[0] = 0;
		callsList[i].talkerAlias[0] = 0;
		callsList[i].locationLat = NAN;
		callsList[i].locationLon = NAN;
		callsList[i].time = 0;
		callsList[i].receivedTS = 0;
		callsList[i].dmrMode = DMR_MODE_AUTO;
		callsList[i].rxAGCGain = 0;

		if (i == 0)
		{
			callsList[i].prev = NULL;
		}
		else
		{
			callsList[i].prev = &callsList[i - 1];
		}

		if (i < (NUM_LASTHEARD_STORED - 1))
		{
			callsList[i].next = &callsList[i + 1];
		}
		else
		{
			callsList[i].next = NULL;
		}
	}

	uiDataGlobal.lastHeardCount = 0;
}

LinkItem_t *lastHeardFindInList(uint32_t id)
{
	LinkItem_t *item = LinkHead;

	while (item->next != NULL)
	{
		if (item->id == id)
		{
			// found it
			return item;
		}
		item = item->next;
	}
	return NULL;
}

// returns pointer to maidenheadBuffer
uint8_t *coordsToMaidenhead(uint8_t *maidenheadBuffer, double latitude, double longitude)
{
	double l, l2;
	uint8_t c;

	l = longitude;

	for (uint8_t i = 0; i < 2; i++)
	{
		l = l / ((i == 0) ? 20.0 : 10.0) + 9.0;
		c = (uint8_t) l;
		maidenheadBuffer[0 + i] = c + 'A';
		l2 = c;
		l -= l2;
		l *= 10.0;
		c = (uint8_t) l;
		maidenheadBuffer[2 + i] = c + '0';
		l2 = c;
		l -= l2;
		l *= 24.0;
		c = (uint8_t) l;
		maidenheadBuffer[4 + i] = c + 'A';

#if 0
		if (extended)
		{
			l2 = c;
			l -= l2;
			l *= 10.0;
			c = (uint8_t) l;
			maidenheadBuffer[6 + i] = c + '0';
			l2 = c;
			l -= l2;
			l *= 24.0;
			c = (uint8_t) l;
			maidenheadBuffer[8 + i] = c + (extended ? 'A' : 'a');
			l2 = c;
			l -= l2;
			l *= 10.0;
			c = (uint8_t) l;
			maidenheadBuffer[10 + i] = c + '0';
			l2 = c;
			l -= l2;
			l *= 24.0;
			c = (uint8_t) l;
			maidenheadBuffer[12 + i] = c + (extended ? 'A' : 'a');
		}
#endif

		l = latitude;
	}

#if 0
	maidenheadBuffer[extended ? 14 : 6] = '\0';
#else
	maidenheadBuffer[6] = '\0';
#endif

	return maidenheadBuffer;
}

static void buildMaidenHead(char *maidenheadBuffer, uint32_t intPartLat, uint32_t decPartLat, bool isSouthern, uint32_t intPartLon, uint32_t decPartLon, bool isWestern)
{
	double latitude = intPartLat + (((double)decPartLat) /  LOCATION_DECIMAL_PART_MULIPLIER_FIXED_32);
	double longitude = intPartLon + (((double)decPartLon) / LOCATION_DECIMAL_PART_MULIPLIER_FIXED_32);

	if (isSouthern)
	{
		latitude *= -1;
	}

	if (isWestern)
	{
		longitude *= -1;
	}

	coordsToMaidenhead((uint8_t *)maidenheadBuffer, latitude, longitude);
}

void buildLocationAndMaidenheadStrings(char *locationBufferOrNull, char *maidenheadBufferOrNull, bool locIsValid)
{
	if (locIsValid)
	{
		bool southernHemisphere = ((nonVolatileSettings.locationLat & 0x80000000) != 0);
		bool westernHemisphere  = ((nonVolatileSettings.locationLon & 0x80000000) != 0);
		uint32_t intPartLat     = (nonVolatileSettings.locationLat & 0x7FFFFFFF) >> 23;
		uint32_t decPartLat     = (nonVolatileSettings.locationLat & 0x7FFFFF);
		uint32_t intPartLon     = (nonVolatileSettings.locationLon & 0x7FFFFFFF) >> 23;
		uint32_t decPartLon     = (nonVolatileSettings.locationLon & 0x7FFFFF);

		if (maidenheadBufferOrNull)
		{
			buildMaidenHead(maidenheadBufferOrNull, intPartLat, decPartLat, southernHemisphere, intPartLon, decPartLon, westernHemisphere);
		}

		if (locationBufferOrNull)
		{
			snprintf(locationBufferOrNull, LOCATION_TEXT_BUFFER_SIZE, "%02u.%04u%c %03u.%04u%c",
					intPartLat, decPartLat/10, currentLanguageGetSymbol(southernHemisphere ? SYMBOLS_SOUTH : SYMBOLS_NORTH),
					intPartLon, decPartLon/10, currentLanguageGetSymbol(westernHemisphere ? SYMBOLS_WEST : SYMBOLS_EAST));
		}
	}
	else
	{
		if (locationBufferOrNull)
		{
			snprintf(locationBufferOrNull, LOCATION_TEXT_BUFFER_SIZE, "%s", "??.???? ???.????");
		}

		if (maidenheadBufferOrNull)
		{
			maidenheadBufferOrNull[0] = 0;
		}
	}
}

double latLongFixed24ToDouble(uint32_t fixedVal)
{
	// MS bit is the sign
	// Lower 10 bits are the fixed point to the right of the point.
	// Bits 15,14,13,12,11 are the integer part
	double inPart = (fixedVal & 0x7FFFFF) >> 15;
	double decimalPart = (fixedVal & 0x7FFF);

	if (fixedVal & 0x800000)
	{
		inPart *= -1;// MS bit is set, so tha number is negative
		decimalPart *= -1;
	}

	return inPart + (decimalPart / ((double)LOCATION_DECIMAL_PART_MULIPLIER_FIXED_24));
}

uint32_t latLongDoubleToFixed24(double value)
{
	uint32_t intPart = abs((uint32_t)value);
	uint32_t decimalPart = abs(value - intPart);
	uint32_t fixedVal = (intPart << 23) + decimalPart;

	if (value < 0)
	{
		fixedVal |= 0x80000000;// set MSB to indicate negative number
	}

	return fixedVal;
}

double latLongFixed32ToDouble(uint32_t fixedVal)
{
	// MS bit is the sign
	// Lower 10 bits are the fixed point to the right of the point.
	// Bits 15,14,13,12,11 are the integer part
	double inPart = (fixedVal & 0x7FFFFFFF) >> 23;
	double decimalPart = (fixedVal & 0x7FFFFF);

	if (fixedVal & 0x80000000)
	{
		inPart *= -1;// MS bit is set, so tha number is negative
		decimalPart *= -1;
	}

	return inPart + (decimalPart / ((double)LOCATION_DECIMAL_PART_MULIPLIER_FIXED_32));
}

uint32_t latLongDoubleToFixed32(double value)
{
	uint32_t intPart = abs((uint32_t)value);
	uint32_t decimalPart = abs(value - intPart);
	uint32_t fixedVal = (intPart << 23) + decimalPart;

	if (value < 0)
	{
		fixedVal |= 0x80000000;// set MSB to indicate negative number
	}

	return fixedVal;
}

double distanceToLocation(double latitude, double longitude)
{
	double lat1 = latitude;
	double lon1 = longitude;
	double lat2 = latLongFixed32ToDouble(nonVolatileSettings.locationLat);
	double lon2 = latLongFixed32ToDouble(nonVolatileSettings.locationLon);

	double r = 6371; //radius of Earth (KM)
	double p = 0.017453292519943295;//  Pi/180
	double a = 0.5 - cos((lat2 - lat1)*p)/2 + cos(lat1 * p)*cos(lat2 * p) * (1 - cos((lon2 - lon1) * p )) / 2;

	double d = 2 * r * asin(sqrt(a));

	return d;
}

static double roundPosition(double val)
{
	return ((double)((int)((val * 10000) + ((val > 0 ) ? 0.5:-0.5)))) / 10000;
}

static void decodeGPSPosition(uint8_t *data, double *latitude, double *longitude)
{
#if 0
	uint8_t errorI = (data[2U] & 0x0E) >> 1U;
	const char* error;
	switch (errorI) {
	case 0U:
		error = "< 2m";
		break;
	case 1U:
		error = "< 20m";
		break;
	case 2U:
		error = "< 200m";
		break;
	case 3U:
		error = "< 2km";
		break;
	case 4U:
		error = "< 20km";
		break;
	case 5U:
		error = "< 200km";
		break;
	case 6U:
		error = "> 200km";
		break;
	default:
		error = "not known";
		break;
	}
#endif

	int32_t longitudeI = ((data[2U] & 0x01U) << 31) | (data[3U] << 23) | (data[4U] << 15) | (data[5U] << 7);
	longitudeI >>= 7;

	int32_t latitudeI = (data[6U] << 24) | (data[7U] << 16) | (data[8U] << 8);
	latitudeI >>= 8;


	double lon = ((double)longitudeI * 360.0F) / 33554432.0F;
	double lat = ((double)latitudeI  * 180.0F) / 16777216.0F;

	*longitude = roundPosition(lon);
	*latitude =  roundPosition(lat);
}

static uint8_t decodeTA(uint8_t *dest, uint8_t destLen, uint8_t *talkerAlias)
{
	uint8_t TAformat = (talkerAlias[0] >> 6U) & 0x03U;
	uint8_t TAsize   = (talkerAlias[0] >> 1U) & 0x1FU;

	*dest = 0;

	switch (TAformat)
	{
		case 0U:		// 7 bit
			{
				uint8_t *b = &talkerAlias[0];
				uint8_t t1 = 0U, t2 = 0U, c = 0U;

				memset(dest, 0x00U, destLen);

				for (uint8_t i = 0U; (i < 32U) && (t2 < TAsize); i++)
				{
					for (int8_t j = 7; j >= 0; j--)
					{
						c = (c << 1U) | (b[i] >> j);

						if (++t1 == 7U)
						{
							if (i > 0U)
							{
								dest[t2++] = c & 0x7FU;
							}

							t1 = 0U;
							c = 0U;
						}
					}
				}
				dest[TAsize] = 0;
			}
			break;

		case 1U:		// ISO 8 bit
		case 2U:		// UTF8
			memcpy(dest, talkerAlias + 1U, (destLen - 1));
			break;

		case 3U:		// UTF16 poor man's conversion
			{
				uint8_t t2 = 0U;

				memset(dest, 0x00U, destLen);

				for (uint8_t i = 0U; (i < 15U) && (t2 < TAsize); i++)
				{
					if (talkerAlias[2U * i + 1U] == 0)
					{
						dest[t2++] = talkerAlias[2U * i + 2U];
					}
					else
					{
						dest[t2++] = '?';
					}
				}
				dest[TAsize] = 0;
			}
			break;
	}

	return (strlen((char *)dest));
}

void lastHeardClearLastID(void)
{
	memset(bufferTA, 0, 32);// Clear any TA data in TA buffer (used for decode)
	blocksTA = 0x00;
	overrideTA = false;
	contactDefinedForTA = false;
	lastID = 0;
}

static void updateLHItem(LinkItem_t *item)
{
	static const int bufferLen = 33; // displayChannelNameOrRxFrequency() use 6x8 font
	char buffer[bufferLen];// buffer passed to the DMR ID lookup function, needs to be large enough to hold worst case text length that is returned. Currently 16+1
	dmrIdDataStruct_t currentRec;

	if ((item->talkGroupOrPcId >> 24) == PC_CALL_FLAG)
	{
		// Its a Private call
		switch (nonVolatileSettings.contactDisplayPriority)
		{
			case CONTACT_DISPLAY_PRIO_CC_DB_TA:
			case CONTACT_DISPLAY_PRIO_TA_CC_DB:
				if (contactIDLookup(item->id, CONTACT_CALLTYPE_PC, buffer) == true)
				{
					snprintf(item->contact, SCREEN_LINE_BUFFER_SIZE, "%s", buffer);
				}
				else
				{
					dmrIDLookup(item->id, &currentRec);
					snprintf(item->contact, SCREEN_LINE_BUFFER_SIZE, "%s", currentRec.text);
				}
				break;

			case CONTACT_DISPLAY_PRIO_DB_CC_TA:
			case CONTACT_DISPLAY_PRIO_TA_DB_CC:
				if (dmrIDLookup(item->id, &currentRec) == true)
				{
					snprintf(item->contact, SCREEN_LINE_BUFFER_SIZE, "%s", currentRec.text);
				}
				else
				{
					if (contactIDLookup(item->id, CONTACT_CALLTYPE_PC, buffer) == true)
					{
						snprintf(item->contact, SCREEN_LINE_BUFFER_SIZE, "%s", buffer);
					}
					else
					{
						snprintf(item->contact, SCREEN_LINE_BUFFER_SIZE, "%s", currentRec.text);
					}
				}
				break;
		}

		if (item->talkGroupOrPcId != (trxDMRID | (PC_CALL_FLAG << 24)))
		{
			if (contactIDLookup(item->talkGroupOrPcId & 0x00FFFFFF, CONTACT_CALLTYPE_PC, buffer) == true)
			{
				snprintf(item->talkgroup, SCREEN_LINE_BUFFER_SIZE, "%s", buffer);
			}
			else
			{
				dmrIDLookup(item->talkGroupOrPcId & 0x00FFFFFF, &currentRec);
				snprintf(item->talkgroup, SCREEN_LINE_BUFFER_SIZE, "%s", currentRec.text);
			}
		}
	}
	else
	{
		// TalkGroup
		if (contactIDLookup(item->talkGroupOrPcId, CONTACT_CALLTYPE_TG, buffer) == true)
		{
			snprintf(item->talkgroup, SCREEN_LINE_BUFFER_SIZE, "%s", buffer);
		}
		else
		{
			if ((item->talkGroupOrPcId & 0x00FFFFFF) == ALL_CALL_VALUE)
			{
				snprintf(item->talkgroup, SCREEN_LINE_BUFFER_SIZE, "%s", currentLanguage->all_call);
			}
			else
			{
				snprintf(item->talkgroup, SCREEN_LINE_BUFFER_SIZE, "%s %u", currentLanguage->tg, (item->talkGroupOrPcId & 0x00FFFFFF));
			}
		}

		switch (nonVolatileSettings.contactDisplayPriority)
		{
			case CONTACT_DISPLAY_PRIO_CC_DB_TA:
			case CONTACT_DISPLAY_PRIO_TA_CC_DB:
				if (contactIDLookup(item->id, CONTACT_CALLTYPE_PC, buffer) == true)
				{
					snprintf(item->contact, MAX_DMR_ID_CONTACT_TEXT_LENGTH, "%s", buffer);
				}
				else
				{
					dmrIDLookup((item->id & 0x00FFFFFF), &currentRec);
					snprintf(item->contact, MAX_DMR_ID_CONTACT_TEXT_LENGTH, "%s", currentRec.text);
				}
				break;

			case CONTACT_DISPLAY_PRIO_DB_CC_TA:
			case CONTACT_DISPLAY_PRIO_TA_DB_CC:
				if (dmrIDLookup((item->id & 0x00FFFFFF), &currentRec) == true)
				{
					snprintf(item->contact, MAX_DMR_ID_CONTACT_TEXT_LENGTH, "%s", currentRec.text);
				}
				else
				{
					if (contactIDLookup(item->id, CONTACT_CALLTYPE_PC, buffer) == true)
					{
						snprintf(item->contact, MAX_DMR_ID_CONTACT_TEXT_LENGTH, "%s", buffer);
					}
					else
					{
						snprintf(item->contact, MAX_DMR_ID_CONTACT_TEXT_LENGTH, "%s", currentRec.text);
					}
				}
				break;
		}
	}
}

void lastHeardClearWorkingTAData(void)
{
	memset(bufferTA, 0, 32);// Clear any TA data in TA buffer (used for decode)
	blocksTA = 0x00;
	overrideTA = false;
	contactDefinedForTA = false;
}

bool lastHeardListUpdate(uint8_t *dmrDataBuffer, bool forceOnHotspot)
{
	uint32_t talkGroupOrPcId = (dmrDataBuffer[0] << 24) + (dmrDataBuffer[3] << 16) + (dmrDataBuffer[4] << 8) + (dmrDataBuffer[5] << 0);

	if ((HRC6000GetReceivedTgOrPcId() != 0) || forceOnHotspot)
	{
		if (dmrDataBuffer[0] == TG_CALL_FLAG || dmrDataBuffer[0] == PC_CALL_FLAG)
		{
			uint32_t id = (dmrDataBuffer[6] << 16) + (dmrDataBuffer[7] << 8) + (dmrDataBuffer[8] << 0);

			if ((id != 0) && (talkGroupOrPcId != 0))
			{
				if (id != lastID)
				{
					lastHeardClearWorkingTAData();

					lastID = id;

					LinkItem_t *item = lastHeardFindInList(id);

					// Already in the list
					if (item != NULL)
					{
						if (item->talkGroupOrPcId != talkGroupOrPcId)
						{
							item->talkGroupOrPcId = talkGroupOrPcId; // update the TG in case they changed TG
							updateLHItem(item);
						}

						item->time = ticksGetMillis();
						lastTG = talkGroupOrPcId;
						dmrRxAGCrxPeakAverage = item->rxAGCGain;

						if (item == LinkHead)
						{
							uiDataGlobal.displayQSOState = QSO_DISPLAY_CALLER_DATA;// flag that the display needs to update
							contactDefinedForTA = true;
							return true;// already at top of the list
						}
						else
						{
							// not at top of the list
							// Move this item to the top of the list
							LinkItem_t *next = item->next;
							LinkItem_t *prev = item->prev;

							// set the previous item to skip this item and link to 'items' next item.
							prev->next = next;

							if (item->next != NULL)
							{
								// not the last in the list
								next->prev = prev;// backwards link the next item to the item before us in the list.
							}

							item->next = LinkHead;// link our next item to the item at the head of the list

							LinkHead->prev = item;// backwards link the hold head item to the item moving to the top of the list.

							item->prev = NULL;// change the items prev to NULL now we are at teh top of the list
							LinkHead = item;// Change the global for the head of the link to the item that is to be at the top of the list.
							if (item->talkGroupOrPcId != 0)
							{
								uiDataGlobal.displayQSOState = QSO_DISPLAY_CALLER_DATA;// flag that the display needs to update
							}
						}
					}
					else
					{
						// Not in the list
						item = LinkHead;// setup to traverse the list from the top.

						if (uiDataGlobal.lastHeardCount < NUM_LASTHEARD_STORED)
						{
							uiDataGlobal.lastHeardCount++;
						}

						// need to use the last item in the list as the new item at the top of the list.
						// find last item in the list
						while(item->next != NULL)
						{
							item = item->next;
						}
						//item is now the last

						(item->prev)->next = NULL;// make the previous item the last

						LinkHead->prev = item;// set the current head item to back reference this item.
						item->next = LinkHead;// set this items next to the current head
						LinkHead = item;// Make this item the new head

						item->id = id;
						item->talkGroupOrPcId = talkGroupOrPcId;
						item->time = ticksGetMillis();
						item->receivedTS = (dmrMonitorCapturedTS != -1) ? dmrMonitorCapturedTS : trxGetDMRTimeSlot();
						item->dmrMode = currentRadioDevice->trxDMRModeRx;
						dmrRxAGCrxPeakAverage = item->rxAGCGain = DMR_RX_AGC_DEFAULT_PEAK_SAMPLES;
						lastTG = talkGroupOrPcId;

						memset(item->contact, 0, sizeof(item->contact)); // Clear contact's datas
						memset(item->talkgroup, 0, sizeof(item->talkgroup));
						memset(item->talkerAlias, 0, sizeof(item->talkerAlias));
						item->locationLat = NAN;
						item->locationLon = NAN;

						updateLHItem(item);

						if (item->talkGroupOrPcId != 0)
						{
							uiDataGlobal.displayQSOState = QSO_DISPLAY_CALLER_DATA;// flag that the display needs to update
						}
					}

					contactDefinedForTA = true;
				}
				else // update TG even if the DMRID did not change
				{
					LinkItem_t *item = lastHeardFindInList(id);

					if (lastTG != talkGroupOrPcId)
					{
						if (item != NULL)
						{
							// Already in the list
							item->talkGroupOrPcId = talkGroupOrPcId;// update the TG in case they changed TG
							updateLHItem(item);
							item->time = ticksGetMillis();
						}

						lastTG = talkGroupOrPcId;
						lastHeardClearWorkingTAData();
					}

					item->receivedTS = (dmrMonitorCapturedTS != -1) ? dmrMonitorCapturedTS : trxGetDMRTimeSlot();// Always update this in case the TS changed.
					item->dmrMode = currentRadioDevice->trxDMRModeRx;
					contactDefinedForTA = true;
				}
			}
		}
		else
		{
			if (contactDefinedForTA)
			{
				// Data contains the Talker Alias Data
				uint8_t blockID = (forceOnHotspot ? dmrDataBuffer[0] : DMR_frame_buffer[0]);

				if (blockID >= 4)
				{
					blockID -= 4; // shift the blockID for maths reasons

					if (blockID < 4) // ID 0x04..0x07: TA
					{

						// Already stored first byte in block TA Header has changed, lets clear other blocks too
						if ((blockID == 0) && ((blocksTA & (1 << blockID)) != 0) &&
								(bufferTA[0] != (forceOnHotspot ? dmrDataBuffer[2] : DMR_frame_buffer[2])))
						{
							blocksTA &= ~(1 << 0);

							// Clear all other blocks if they're already stored
							if ((blocksTA & (1 << 1)) != 0)
							{
								blocksTA &= ~(1 << 1);
								memset(bufferTA + 7, 0, 7); // Clear 2nd TA block
							}
							if ((blocksTA & (1 << 2)) != 0)
							{
								blocksTA &= ~(1 << 2);
								memset(bufferTA + 14, 0, 7); // Clear 3rd TA block
							}
							if ((blocksTA & (1 << 3)) != 0)
							{
								blocksTA &= ~(1 << 3);
								memset(bufferTA + 21, 0, 7); // Clear 4th TA block
							}
							overrideTA = true;
						}

						// We don't already have this TA block
						if ((blocksTA & (1 << blockID)) == 0)
						{
							static const uint8_t blockLen = 7;
							uint32_t blockOffset = blockID * blockLen;

							blocksTA |= (1 << blockID);

							if ((blockOffset + blockLen) < sizeof(bufferTA))
							{
								memcpy(bufferTA + blockOffset, (void *)(forceOnHotspot ? &dmrDataBuffer[2] : &DMR_frame_buffer[2]), blockLen);

								// Format and length infos are available, we can decode now
								if (bufferTA[0] != 0x0)
								{
									uint8_t bufferTADest[32];
									uint8_t taLen = 0U;

									if ((taLen = decodeTA(&bufferTADest[0], sizeof(bufferTADest), &bufferTA[0])) > 0)
									{
										// TAs doesn't match, update contact and screen.
										if (overrideTA || (taLen > strlen((const char *)&LinkHead->talkerAlias)))
										{
											memcpy(&LinkHead->talkerAlias, &bufferTADest[0], 31);// Brandmeister seems to send callsign as 6 chars only

											if ((blocksTA & (1 << 1)) != 0) // we already received the 2nd TA block, check for 'DMR ID:'
											{
												char *p = NULL;

												// Get rid of 'DMR ID:xxxxxxx' part of the TA, sent by BM
												if (((p = strstr(&LinkHead->talkerAlias[0], "DMR ID:")) != NULL) || ((p = strstr(&LinkHead->talkerAlias[0], "DMR I")) != NULL))
												{
													*p = 0;
												}
											}

											overrideTA = false;
											uiDataGlobal.displayQSOState = QSO_DISPLAY_CALLER_DATA;
										}
									}
								}
							}
						}
					}
					else if (blockID == 4) // ID 0x08: GPS
					{
						double longitude, latitude;
						decodeGPSPosition((uint8_t *)(forceOnHotspot ? &dmrDataBuffer[0] : &DMR_frame_buffer[0]), &latitude,&longitude);

						if ((LinkHead->locationLat != latitude) || (LinkHead->locationLon != longitude))
						{
							LinkHead->locationLat = latitude;
							LinkHead->locationLon = longitude;

							// If we received the location but no TA text, then insert ID:xxxxx so that the rest of the QSO display system works and displays the maindenhead
							if (LinkHead->talkerAlias[0] == 0x00)
							{
								snprintf(LinkHead->talkerAlias, 16, "ID:%u", LinkHead->id);
							}

							uiDataGlobal.displayQSOState = QSO_DISPLAY_CALLER_DATA_UPDATE;
						}
					}
				}
			}
		}
	}

	return true;
}

static bool dmrIDReadContactInFlash(uint32_t contactOffset, uint8_t *data, uint32_t len)
{
	uint32_t address;

	if (contactOffset >= dmrIdDataArea_1_Size)
	{
		address = dmrIDDatabaseMemoryLocation2 + (contactOffset - dmrIdDataArea_1_Size);
	}
	else
	{
		address = DMRID_MEMORY_LOCATION_1 + DMRID_HEADER_LENGTH + contactOffset;
	}

	return SPI_Flash_read(address, data, len);
}

void dmrIDCacheInit(void)
{
	uint8_t headerBuf[32];

	dmrIDCacheClear();
	memset(&headerBuf, 0, sizeof(headerBuf));

	SPI_Flash_read(DMRID_MEMORY_LOCATION_1, headerBuf, DMRID_HEADER_LENGTH);

	// Break backward compatibility with old "ID{N,n} tag, as we had too
	// much problems with corrupted database.
	if ((headerBuf[0] != 'I') || (headerBuf[1] != 'd') )
	{
		return;
	}

	if (headerBuf[2] == 'N' || headerBuf[2] == 'n')
	{
		DMRID_IdLength = 3U;// default is 4
		if (headerBuf[2] == 'n')
		{
			dmrIDDatabaseMemoryLocation2 = VOICE_PROMPTS_FLASH_HEADER_ADDRESS; // overwrite the VP
		}
	}

	dmrIDsCache.contactLength = (uint8_t)headerBuf[3] - 0x4a;
	// Check that data in DMR ID DB does not have a larger record size than the code has
	if (dmrIDsCache.contactLength > sizeof(dmrIdDataStruct_t))
	{
		return;
	}

	dmrIDsCache.entries = ((uint32_t)headerBuf[8] | (uint32_t)headerBuf[9] << 8 | (uint32_t)headerBuf[10] << 16 | (uint32_t)headerBuf[11] << 24);

	// Size of number of complete DMR ID records for the first storage location
	dmrIdDataArea_1_Size = (dmrIDsCache.contactLength * ((0x40000 - DMRID_HEADER_LENGTH) / dmrIDsCache.contactLength));

	if (dmrIDsCache.entries > 0)
	{
		dmrIdDataStruct_t dmrIDContact;

		// Set Min and Max IDs boundaries
		// First available ID

		dmrIDContact.id = 0;
		dmrIDReadContactInFlash(0, (uint8_t *)&dmrIDContact, DMRID_IdLength);
		dmrIDsCache.slices[0] = dmrIDContact.id;

		// Last available ID
		dmrIDContact.id = 0;
		dmrIDReadContactInFlash((dmrIDsCache.contactLength * (dmrIDsCache.entries - 1)), (uint8_t *)&dmrIDContact, DMRID_IdLength);
		dmrIDsCache.slices[ID_SLICES - 1] = dmrIDContact.id;

		if (dmrIDsCache.entries > MIN_ENTRIES_BEFORE_USING_SLICES)
		{
			dmrIDsCache.IDsPerSlice = dmrIDsCache.entries / (ID_SLICES - 1);

			for (uint8_t i = 0; i < (ID_SLICES - 2); i++)
			{
				dmrIDContact.id = 0;
				dmrIDReadContactInFlash((dmrIDsCache.contactLength * ((dmrIDsCache.IDsPerSlice * i) + dmrIDsCache.IDsPerSlice)), (uint8_t *)&dmrIDContact, DMRID_IdLength);
				dmrIDsCache.slices[i + 1] = dmrIDContact.id;
			}
		}
	}
}

// The first address past the DMR ID database in the external flash, or 0 when
// none is loaded. The receive recorder checks it, so that however large a
// database the CPS writes, the recorder never erases a byte of it.
uint32_t dmrIDDatabaseEndAddress(void)
{
	uint32_t total;

	if ((dmrIDsCache.entries == 0) || (dmrIDsCache.contactLength == 0))
	{
		return 0;
	}

	total = (dmrIDsCache.entries * dmrIDsCache.contactLength);

	if (total <= dmrIdDataArea_1_Size)
	{
		return (DMRID_MEMORY_LOCATION_1 + DMRID_HEADER_LENGTH + total);
	}

	return (dmrIDDatabaseMemoryLocation2 + (total - dmrIdDataArea_1_Size));
}

void dmrIDCacheClear(void)
{
	memset(&dmrIDsCache, 0, sizeof(dmrIDsCache_t));
}

uint32_t dmrIDCacheGetCount(void)
{
	return dmrIDsCache.entries;
}

static void dmrDbTextDecode(uint8_t *decompressedBufOut, uint8_t *compressedBufIn, int compressedSize)
{
	uint8_t *outPtr = decompressedBufOut;
	uint8_t cb1, cb2, cb3;
	int d = 0;
	do
	{
		cb1 = compressedBufIn[d++];
		*outPtr++ = DECOMPRESS_LUT[cb1 >> 2];//A
		if (d == compressedSize)
		{
			break;
		}
		cb2 = compressedBufIn[d++];
		*outPtr++ = DECOMPRESS_LUT[((cb1 & 0x03) << 4) + (cb2 >> 4)];//B
		if (d == compressedSize)
		{
			break;
		}
		cb3 = compressedBufIn[d++];
		*outPtr++ = DECOMPRESS_LUT[((cb2 & 0x0F) << 2) + (cb3 >> 6)];//C
		*outPtr++ = DECOMPRESS_LUT[cb3 & 0x3F];//D

	} while (d < compressedSize);

	// algorithm can result in a extra space at the end of the decompressed string
	// so trim the string
	uint32_t l = (outPtr - decompressedBufOut);
	if (l)
	{
		uint8_t *p = ((decompressedBufOut + l) - 1);
		while ((p >= decompressedBufOut) && (*p == ' '))
		{
			*p-- = 0;
		}
	}
}

bool dmrIDLookup(uint32_t targetId, dmrIdDataStruct_t *foundRecord)
{
	uint32_t targetIdBCD;

	if (DMRID_IdLength == 4U)
	{
		targetIdBCD = int2bcd(targetId);
	}
	else
	{
		targetIdBCD = targetId;
	}

	if ((dmrIDsCache.entries > 0) && (targetIdBCD >= dmrIDsCache.slices[0]) && (targetIdBCD <= dmrIDsCache.slices[ID_SLICES - 1]))
	{
		uint32_t startPos = 0;
		uint32_t endPos = dmrIDsCache.entries - 1;
		uint32_t curPos;

		// Contact's text length == (dmrIDsCache.contactLength - DMRID_IdLength) aren't NULL terminated,
		// so clearing the whole destination array is mandatory
		memset(foundRecord->text, 0, sizeof(foundRecord->text));

		uint8_t compressedBuf[MAX_DMR_ID_CONTACT_TEXT_LENGTH];// worst case length with no compression

		if (dmrIDsCache.entries > MIN_ENTRIES_BEFORE_USING_SLICES) // Use slices
		{
			for (uint8_t i = 0; i < ID_SLICES - 1; i++)
			{
				// Check if ID is in slices boundaries, with a special case for the last slice as [ID_SLICES - 1] is the last ID
				if ((targetIdBCD >= dmrIDsCache.slices[i]) &&
						((i == ID_SLICES - 2) ? (targetIdBCD <= dmrIDsCache.slices[i + 1]) : (targetIdBCD < dmrIDsCache.slices[i + 1])))
				{
					// targetID is the min slice limit, don't go further
					if (targetIdBCD == dmrIDsCache.slices[i])
					{
						foundRecord->id = dmrIDsCache.slices[i];

						if (dmrIDReadContactInFlash((dmrIDsCache.contactLength * (dmrIDsCache.IDsPerSlice * i)) + DMRID_IdLength, (uint8_t *)&compressedBuf, (dmrIDsCache.contactLength - DMRID_IdLength)))
						{
							if (DMRID_IdLength == 3U)
							{
								dmrDbTextDecode((uint8_t *)foundRecord->text, (uint8_t *)&compressedBuf, (dmrIDsCache.contactLength - DMRID_IdLength));
							}
							else
							{
								memcpy((uint8_t *)foundRecord->text, (uint8_t *)&compressedBuf, (dmrIDsCache.contactLength - DMRID_IdLength));
							}
							return true;
						}
						else
						{
							goto spiReadFailure;
						}
					}

					startPos = dmrIDsCache.IDsPerSlice * i;
					endPos = (i == ID_SLICES - 2) ? (dmrIDsCache.entries - 1) : dmrIDsCache.IDsPerSlice * (i + 1);

					break;
				}
			}
		}
		else // Not enough contact to use slices
		{
			bool isMin;

			// Check if targetID is equal to the first or the last in the IDs list
			if ((isMin = (targetIdBCD == dmrIDsCache.slices[0])) || (targetIdBCD == dmrIDsCache.slices[ID_SLICES - 1]))
			{
				foundRecord->id = dmrIDsCache.slices[(isMin ? 0 : (ID_SLICES - 1))];

				if (dmrIDReadContactInFlash((dmrIDsCache.contactLength * (dmrIDsCache.IDsPerSlice * (isMin ? 0 : (ID_SLICES - 1)))) + DMRID_IdLength, (uint8_t *) &compressedBuf, (dmrIDsCache.contactLength - DMRID_IdLength)))
				{
					if (DMRID_IdLength == 3U)
					{
						dmrDbTextDecode((uint8_t *)foundRecord->text, (uint8_t *)&compressedBuf, (dmrIDsCache.contactLength - DMRID_IdLength));
					}
					else
					{
						memcpy((uint8_t *)foundRecord->text, (uint8_t *)&compressedBuf, (dmrIDsCache.contactLength - DMRID_IdLength));
					}

					return true;
				}
				else
				{
					goto spiReadFailure;
				}
			}
		}

		// Look for the ID now
		while (startPos <= endPos)
		{
			curPos = (startPos + endPos) >> 1;

			foundRecord->id = 0;

			if (dmrIDReadContactInFlash((dmrIDsCache.contactLength * curPos), (uint8_t *)foundRecord, DMRID_IdLength))
			{
				if (foundRecord->id < targetIdBCD)
				{
					startPos = curPos + 1;
				}
				else if (foundRecord->id > targetIdBCD)
				{
					endPos = curPos - 1;
				}
				else
				{
					dmrIDReadContactInFlash((dmrIDsCache.contactLength * curPos) + DMRID_IdLength, (uint8_t *)&compressedBuf, (dmrIDsCache.contactLength - DMRID_IdLength));

					if (DMRID_IdLength == 3U)
					{
						dmrDbTextDecode((uint8_t *)foundRecord->text, (uint8_t *)&compressedBuf, (dmrIDsCache.contactLength - DMRID_IdLength));
					}
					else
					{
						memcpy((uint8_t *)foundRecord->text, (uint8_t *)&compressedBuf, (dmrIDsCache.contactLength - DMRID_IdLength));
					}

					return true;
				}
			}
			else
			{
				goto spiReadFailure;
			}
		}
	}

	spiReadFailure:
	snprintf(foundRecord->text, MAX_DMR_ID_CONTACT_TEXT_LENGTH, "ID:%d", targetId);
	return false;
}

bool contactIDLookup(uint32_t id, uint32_t calltype, char *buffer)
{
	struct_codeplugContact_t contact;
	int8_t manTS = tsGetManualOverrideFromCurrentChannel();

	int contactIndex = codeplugContactIndexByTGorPC((id & 0x00FFFFFF), calltype, &contact,
			((monitorModeData.isEnabled && (dmrMonitorCapturedTS != -1)) ? (dmrMonitorCapturedTS + 1) :
			(manTS ? manTS : (trxGetDMRTimeSlot() + 1))));

	if (contactIndex != -1)
	{
		codeplugUtilConvertBufToString(contact.name, buffer, 16);
		return true;
	}

	return false;
}

static void displayChannelNameOrRxFrequency(char *buffer, size_t maxLen)
{
	if (menuSystemGetCurrentMenuNumber() == UI_CHANNEL_MODE)
	{
#if defined(PLATFORM_MD9600)
		if (codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_OUT_OF_BAND) != 0)
		{
			snprintf(buffer, maxLen, "%s", currentLanguage->out_of_band);
		}
		else
#endif
		{
			codeplugUtilConvertBufToString(currentChannelData->name, buffer, 16);
			displayThemeApply(THEME_ITEM_FG_CHANNEL_NAME, THEME_ITEM_BG);
		}
	}
	else
	{
		int val_before_dp = currentChannelData->rxFreq / 100000;
		int val_after_dp = currentChannelData->rxFreq - val_before_dp * 100000;

		snprintf(buffer, maxLen, "%d.%05d MHz", val_before_dp, val_after_dp);
		displayThemeApply(THEME_ITEM_FG_RX_FREQ, THEME_ITEM_BG);
	}

	uiUtilityDisplayInformation(buffer, DISPLAY_INFO_ZONE, -1);
}

static void displaySplitOrSpanText(uint8_t y, char *text)
{
	if (text != NULL)
	{
		uint8_t len = strlen(text);

		if (len == 0)
		{
			return;
		}
		else if (len <= 16)
		{
			displayPrintCentered(y, text, FONT_SIZE_3);
		}
		else
		{
			uint8_t nLines = len / 21 + (((len % 21) != 0) ? 1 : 0);

			if (nLines > 2)
			{
				nLines = 2;
				len = 42; // 2 lines max.
			}

			if (nLines > 1)
			{
				char buffer[MAX_DMR_ID_CONTACT_TEXT_LENGTH]; // to fit 2 * 21 chars + NULL, but needs larger

				memcpy(buffer, text, len + 1);
				buffer[len + 1] = 0;

				char *p = buffer + 20;

				// Find a space backward
				while ((*p != ' ') && (p > buffer))
				{
					p--;
				}

#if defined(PLATFORM_RD5R)
				y -= 6; // Avoid to write the 2nd line off the screen
#endif

				uint8_t rest = (uint8_t)((buffer + strlen(buffer)) - p) - ((*p == ' ') ? 1 : 0);

				// rest is too long, just split the line in two chunks
				if (rest > 21)
				{
					char c = buffer[21];

					buffer[21] = 0;

					displayPrintCentered(y, chomp(buffer), FONT_SIZE_1); // 2 pixels are saved, could center

					buffer[21] = c;

					displayPrintCentered(y + 8, chomp(buffer + 21), FONT_SIZE_1);
				}
				else
				{
					*p = 0;

					displayPrintCentered(y, chomp(buffer), FONT_SIZE_1);
					displayPrintCentered(y + 8, chomp(p + 1), FONT_SIZE_1);
				}
			}
			else // One line of 21 chars max
			{
				displayPrintCentered(y
#if ! defined(PLATFORM_RD5R)
						+ 4
#endif
						, text, FONT_SIZE_1);
			}
		}
	}
}

/*
 * Try to extract callsign and extra text from TA or DMR ID data, then display that on
 * two lines, if possible.
 * We don't care if extra text is larger than 16 chars, ucPrint*() functions cut them.
 *.
 */
static void displayContactTextInfos(char *text, size_t maxLen, bool isFromTalkerAlias)
{
	// Max for TalkerAlias is 37: TA 27 (in 7bit format) + ' [' + 6 (Maidenhead)  + ']' + NULL
	// Max for DMRID Database is MAX_DMR_ID_CONTACT_TEXT_LENGTH (50 + NULL)
	char buffer[MAX_DMR_ID_CONTACT_TEXT_LENGTH];

	displayThemeApply(THEME_ITEM_FG_CHANNEL_CONTACT_INFO, THEME_ITEM_BG);

	if (strlen(text) >= 5 && isFromTalkerAlias) // if it's Talker Alias and there is more text than just the callsign, split across 2 lines
	{
		char    *pbuf;
		int32_t  cpos;

		// User prefers to not span the TA info over two lines, check it that could fit
		if ((nonVolatileSettings.splitContact == SPLIT_CONTACT_SINGLE_LINE_ONLY) ||
				((nonVolatileSettings.splitContact == SPLIT_CONTACT_AUTO) && (strlen(text) <= 16)))
		{
			memcpy(buffer, text, 16);
			buffer[16] = 0;
			uiUtilityDisplayInformation(chomp(buffer), DISPLAY_INFO_CHANNEL, -1);
			displayThemeApply(THEME_ITEM_FG_CHANNEL_CONTACT_INFO, THEME_ITEM_BG);
			displayChannelNameOrRxFrequency(buffer, (sizeof(buffer) / sizeof(buffer[0])));
			displayThemeResetToDefault();
			return;
		}

		if ((cpos = getFirstSpacePos(text)) != -1)
		{
			// Callsign found
			memcpy(buffer, text, cpos);
			buffer[cpos] = 0;

			uiUtilityDisplayInformation(chomp(buffer), DISPLAY_INFO_CHANNEL, -1);
			displayThemeApply(THEME_ITEM_FG_CHANNEL_CONTACT_INFO, THEME_ITEM_BG);

			memcpy(buffer, text + (cpos + 1), (maxLen - (cpos + 1)));
			buffer[(maxLen - (cpos + 1))] = 0;

			pbuf = chomp(buffer);

			if (strlen(pbuf))
			{
				displaySplitOrSpanText(DISPLAY_Y_POS_CHANNEL_SECOND_LINE, pbuf);
			}
			else
			{
				displayChannelNameOrRxFrequency(buffer, (sizeof(buffer) / sizeof(buffer[0])));
			}
		}
		else
		{
			// No space found, use a chainsaw
			memcpy(buffer, text, 16);
			buffer[16] = 0;
			uiUtilityDisplayInformation(chomp(buffer), DISPLAY_INFO_CHANNEL, -1);
			displayThemeApply(THEME_ITEM_FG_CHANNEL_CONTACT_INFO, THEME_ITEM_BG);

			memcpy(buffer, text + 16, (maxLen - 16));
			buffer[(strlen(text) - 16)] = 0;

			pbuf = chomp(buffer);

			if (strlen(pbuf))
			{
				displaySplitOrSpanText(DISPLAY_Y_POS_CHANNEL_SECOND_LINE, pbuf);
			}
			else
			{
				displayChannelNameOrRxFrequency(buffer, (sizeof(buffer) / sizeof(buffer[0])));
			}
		}
	}
	else
	{
		memcpy(buffer, text, 16);
		buffer[16] = 0;
		uiUtilityDisplayInformation(chomp(buffer), DISPLAY_INFO_CHANNEL, -1);
		displayThemeApply(THEME_ITEM_FG_CHANNEL_CONTACT_INFO, THEME_ITEM_BG);
		displayChannelNameOrRxFrequency(buffer, (sizeof(buffer) / sizeof(buffer[0])));
	}

	displayThemeResetToDefault();
}

void uiUtilityDisplayInformation(const char *str, displayInformation_t line, int8_t yOverride)
{
	bool inverted = false;

	switch (line)
	{
		case DISPLAY_INFO_CONTACT_INVERTED:
			displayThemeApply(THEME_ITEM_FG_CHANNEL_CONTACT, THEME_ITEM_BG);
#if defined(PLATFORM_RD5R) || defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
			displayFillRect(0, DISPLAY_Y_POS_CONTACT
#if defined(PLATFORM_RD5R)
					+ 1
#endif
					, DISPLAY_SIZE_X, MENU_ENTRY_HEIGHT, false);
#else
			displayClearRows(2, 4, true);
#endif
			inverted = true;
		case DISPLAY_INFO_CONTACT:
			displayThemeApply(THEME_ITEM_FG_CHANNEL_CONTACT, THEME_ITEM_BG);
			displayPrintCore(0, ((yOverride == -1) ? (DISPLAY_Y_POS_CONTACT + V_OFFSET) : yOverride), str, FONT_SIZE_3, TEXT_ALIGN_CENTER, inverted);
			break;

		case DISPLAY_INFO_CONTACT_OVERRIDE_FRAME:
			displayThemeApply(THEME_ITEM_FG_CHANNEL_CONTACT, THEME_ITEM_BG);
			displayDrawRect(0, ((yOverride == -1) ? DISPLAY_Y_POS_CONTACT : yOverride), DISPLAY_SIZE_X, OVERRIDE_FRAME_HEIGHT, true);
			break;

		case DISPLAY_INFO_CHANNEL_INVERTED:
			displayThemeApply(THEME_ITEM_FG_CHANNEL_NAME, THEME_ITEM_BG);
			inverted = true;
			{
#if defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
				displayFillRect(0, ((yOverride == -1) ? DISPLAY_Y_POS_CHANNEL_FIRST_LINE : yOverride), DISPLAY_SIZE_X, OVERRIDE_FRAME_HEIGHT, false);
#else
				int row = ((yOverride == -1) ? DISPLAY_Y_POS_CHANNEL_FIRST_LINE : yOverride)/8;
				displayClearRows(row, row + 2, true);
#endif
			}
		case DISPLAY_INFO_CHANNEL:
#if defined(HAS_COLOURS)
			if (! displayThemeIsForegroundColourEqualTo(THEME_ITEM_FG_CHANNEL_CONTACT_INFO)) // displayContactTextInfos() override theme color
			{
				displayThemeApply(THEME_ITEM_FG_CHANNEL_NAME, THEME_ITEM_BG);
			}
#endif
			displayPrintCore(0, ((yOverride == -1) ? DISPLAY_Y_POS_CHANNEL_FIRST_LINE : yOverride), str, FONT_SIZE_3, TEXT_ALIGN_CENTER, inverted);
			break;

		case DISPLAY_INFO_CHANNEL_DETAILS:
		{
			static const int bufLen = 24;
			char buf[bufLen];
			char *p = buf;
			int radioMode = trxGetMode();

			if (radioMode == RADIO_MODE_ANALOG)
			{
				CodeplugCSSTypes_t type = codeplugGetCSSType(currentChannelData->rxTone);

				p += snprintf(p, bufLen, "Rx:");

				if (type == CSS_TYPE_CTCSS)
				{
					p += snprintf(p, bufLen - (p - buf), "%d.%dHz", currentChannelData->rxTone / 10 , currentChannelData->rxTone % 10);
				}
				else if (type & CSS_TYPE_DCS)
				{
					p += dcsPrintf(p, bufLen - (p - buf), NULL, currentChannelData->rxTone);
				}
				else
				{
					p += snprintf(p, bufLen - (p - buf), "%s", currentLanguage->none);
				}

				p += snprintf(p, bufLen - (p - buf), "|Tx:");

				type = codeplugGetCSSType(currentChannelData->txTone);
				if (type == CSS_TYPE_CTCSS)
				{
					p += snprintf(p, bufLen - (p - buf), "%d.%dHz", currentChannelData->txTone / 10 , currentChannelData->txTone % 10);
				}
				else if (type & CSS_TYPE_DCS)
				{
					p += dcsPrintf(p, bufLen - (p - buf), NULL, currentChannelData->txTone);
				}
				else
				{
					p += snprintf(p, bufLen - (p - buf), "%s", currentLanguage->none);
				}

				displayThemeApply(THEME_ITEM_FG_CSS_SQL_VALUES, THEME_ITEM_BG);
				displayPrintCentered(DISPLAY_Y_POS_CSS_INFO, buf, FONT_SIZE_1);

				p = buf;
				p += snprintf(p, bufLen, "SQL:%d%%", 5 * (((currentChannelData->sql == 0) ? nonVolatileSettings.squelchDefaults[currentRadioDevice->trxCurrentBand[TRX_RX_FREQ_BAND]] : currentChannelData->sql) - 1));
			}
			else
			{
				// Digital
				uint32_t PCorTG = ((nonVolatileSettings.overrideTG != 0) ? nonVolatileSettings.overrideTG : codeplugContactGetPackedId(&currentContactData));

				p = buf;
				p += snprintf(p, bufLen, "%s %u",
						(((PCorTG >> 24) == PC_CALL_FLAG) ? currentLanguage->pc : currentLanguage->tg),
						(PCorTG & 0xFFFFFF));
			}

#if (defined(CPU_MK22FN512VLL12) || defined(PLATFORM_MD9600))
				if (currentChannelData->NOT_IN_CODEPLUG_CALCULATED_DISTANCE_X10 != -1)
				{
					int intPart = currentChannelData->NOT_IN_CODEPLUG_CALCULATED_DISTANCE_X10 / 10;
					int decPart = currentChannelData->NOT_IN_CODEPLUG_CALCULATED_DISTANCE_X10 - (intPart * 10);
					p += snprintf(p, bufLen, "|%d.%dkm",intPart,decPart);
				}
				displayPrintCentered((radioMode == RADIO_MODE_DIGITAL)? (DISPLAY_Y_POS_CSS_INFO + (FONT_SIZE_2_HEIGHT / 2)): DISPLAY_Y_POS_SQL_INFO, buf, (radioMode == RADIO_MODE_DIGITAL)? FONT_SIZE_2: FONT_SIZE_1);
#else
				displayPrintCentered((radioMode == RADIO_MODE_DIGITAL)? (DISPLAY_Y_POS_CSS_INFO): DISPLAY_Y_POS_SQL_INFO, buf, (radioMode == RADIO_MODE_DIGITAL)? FONT_SIZE_3: FONT_SIZE_1);
#endif
		}
		break;

		case DISPLAY_INFO_TX_TIMER:
			displayThemeApply(THEME_ITEM_FG_TX_COUNTER, THEME_ITEM_BG);
			displayPrintCentered(DISPLAY_Y_POS_TX_TIMER, str, FONT_SIZE_4);
			break;

		case DISPLAY_INFO_ZONE:
			{
				bool distanceEnabled = (((uiDataGlobal.Scan.active == false) || (uiDataGlobal.Scan.active && (uiDataGlobal.Scan.state == SCAN_STATE_PAUSED))) &&
						(settingsIsOptionBitSet(BIT_SORT_CHANNEL_DISTANCE) && (CODEPLUG_ZONE_IS_ALLCHANNELS(currentZone) == false)));
				bool displayingZone = (yOverride == -2);

				if (displayingZone)
				{
					if (distanceEnabled)
					{
						displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
#if defined(HAS_COLOURS)
						displayDrawRoundRect(5, (DISPLAY_Y_POS_ZONE - 3), (DISPLAY_SIZE_X - 10), (8 + 2 + 3), 3, true);
#else
						displayFillRect(0, (DISPLAY_Y_POS_ZONE - 2), DISPLAY_SIZE_X, (8 + 2 + 2), false);
#endif
					}

					displayThemeApply(THEME_ITEM_FG_ZONE_NAME, THEME_ITEM_BG);
				}

#if defined(HAS_COLOURS)
				displayPrintCentered(DISPLAY_Y_POS_ZONE, str, FONT_SIZE_1);
#else
				displayPrintCore(0, DISPLAY_Y_POS_ZONE, str, FONT_SIZE_1, TEXT_ALIGN_CENTER, (displayingZone && distanceEnabled));
#endif
			}
			break;
	}

	displayThemeResetToDefault();
}

void uiUtilityRenderQSODataAndUpdateScreen(void)
{
	if (isQSODataAvailableForCurrentTalker())
	{
		displayClearBuf();
		uiUtilityRenderHeader(false, false);
		uiUtilityRenderQSOData();
		displayRender();
	}
}

void uiUtilityRenderQSOData(void)
{
	displayThemeApply(THEME_ITEM_FG_CHANNEL_CONTACT_INFO, THEME_ITEM_BG);

	uiDataGlobal.receivedPcId = 0x00; //reset the received PcId

	/*
	 * Note.
	 * When using Brandmeister reflectors. TalkGroups can be used to select reflectors e.g. TG 4009, and TG 5000 to check the connnection
	 * Under these conditions Brandmeister seems to respond with a message via a private call even if the command was sent as a TalkGroup,
	 * and this caused the Private Message acceptance system to operate.
	 * Brandmeister seems respond on the same ID as the keyed TG, so the code
	 * (LinkHead->id & 0xFFFFFF) != (trxTalkGroupOrPcId & 0xFFFFFF)  is designed to stop the Private call system tiggering in these instances
	 *
	 * FYI. Brandmeister seems to respond with a TG value of the users on ID number,
	 * but I thought it was safer to disregard any PC's from IDs the same as the current TG
	 * rather than testing if the TG is the user's ID, though that may work as well.
	 */
	if (HRC6000GetReceivedTgOrPcId() != 0)
	{
		if ((LinkHead->talkGroupOrPcId >> 24) == PC_CALL_FLAG) // &&  (LinkHead->id & 0xFFFFFF) != (trxTalkGroupOrPcId & 0xFFFFFF))
		{
			// Its a Private call
			displayPrintCentered(16, LinkHead->contact, FONT_SIZE_3);

			displayPrintCentered(DISPLAY_Y_POS_CHANNEL_FIRST_LINE, currentLanguage->private_call, FONT_SIZE_3);

			if (LinkHead->talkGroupOrPcId != (trxDMRID | (PC_CALL_FLAG << 24)))
			{
				uiUtilityDisplayInformation((((LinkHead->talkGroupOrPcId & 0x00FFFFFF) == ALL_CALL_VALUE) ? currentLanguage->all_call : LinkHead->talkgroup), DISPLAY_INFO_ZONE, -1);
				displayThemeApply(THEME_ITEM_FG_CHANNEL_CONTACT_INFO, THEME_ITEM_BG);
				displayPrintAt(1, DISPLAY_Y_POS_ZONE, "=>", FONT_SIZE_1);
			}
		}
		else
		{
			// Group call
			bool different = (((LinkHead->talkGroupOrPcId & 0xFFFFFF) != trxTalkGroupOrPcId ) ||
					(((currentRadioDevice->trxDMRModeRx != DMR_MODE_DMO) && (dmrMonitorCapturedTS != -1)) && (dmrMonitorCapturedTS != trxGetDMRTimeSlot())) ||
					(trxGetDMRColourCode() != currentChannelData->txColor));

			uiUtilityDisplayInformation(LinkHead->talkgroup, different ? DISPLAY_INFO_CONTACT_INVERTED : DISPLAY_INFO_CONTACT, -1);

			// If voice prompt feedback is enabled. Play a short beep to indicate the inverse video display showing the TG / TS / CC does not match the current Tx config
			if (different && nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_LEVEL_2)
			{
				soundSetMelody(MELODY_RX_TGTSCC_WARNING_BEEP);
			}

			displayThemeApply(THEME_ITEM_FG_CHANNEL_CONTACT_INFO, THEME_ITEM_BG);
			switch (nonVolatileSettings.contactDisplayPriority)
			{
				case CONTACT_DISPLAY_PRIO_CC_DB_TA:
				case CONTACT_DISPLAY_PRIO_DB_CC_TA:
					// No contact found in codeplug and DMRIDs, use TA as fallback, if any.
					if ((strncmp(LinkHead->contact, "ID:", 3) == 0) && (LinkHead->talkerAlias[0] != 0x00))
					{
						if (LinkHead->locationLat <= 90)
						{
							char tmpBufferTA[37]; // TA + ' [' + Maidenhead + ']' + NULL

							memset(tmpBufferTA, 0, sizeof(tmpBufferTA));
							char maidenheadBuffer[8];

							coordsToMaidenhead((uint8_t *)maidenheadBuffer, LinkHead->locationLat, LinkHead->locationLon);
							snprintf(tmpBufferTA, 37, "%s [%s]", LinkHead->talkerAlias, maidenheadBuffer);
							displayContactTextInfos(tmpBufferTA, sizeof(tmpBufferTA), true);
						}
						else
						{
							displayContactTextInfos(LinkHead->talkerAlias, sizeof(LinkHead->talkerAlias), !(nonVolatileSettings.splitContact == SPLIT_CONTACT_SINGLE_LINE_ONLY));
						}
					}
					else
					{
						displayContactTextInfos(LinkHead->contact, sizeof(LinkHead->contact), !(nonVolatileSettings.splitContact == SPLIT_CONTACT_SINGLE_LINE_ONLY));
					}
					break;

				case CONTACT_DISPLAY_PRIO_TA_CC_DB:
				case CONTACT_DISPLAY_PRIO_TA_DB_CC:
					// Talker Alias have the priority here
					if (LinkHead->talkerAlias[0] != 0x00)
					{
						if (LinkHead->locationLat <= 90)
						{
							char tmpBufferTA[37]; // TA + ' [' + Maidenhead + ']' + NULL

							memset(tmpBufferTA, 0, sizeof(tmpBufferTA));
							char maidenheadBuffer[8];

							coordsToMaidenhead((uint8_t *)maidenheadBuffer, LinkHead->locationLat, LinkHead->locationLon);
							snprintf(tmpBufferTA, 37, "%s [%s]", LinkHead->talkerAlias, maidenheadBuffer);
							displayContactTextInfos(tmpBufferTA, sizeof(tmpBufferTA), true);
						}
						else
						{
							displayContactTextInfos(LinkHead->talkerAlias, sizeof(LinkHead->talkerAlias), !(nonVolatileSettings.splitContact == SPLIT_CONTACT_SINGLE_LINE_ONLY));
						}
					}
					else // No TA, then use the one extracted from Codeplug or DMRIdDB
					{
						displayContactTextInfos(LinkHead->contact, sizeof(LinkHead->contact), !(nonVolatileSettings.splitContact == SPLIT_CONTACT_SINGLE_LINE_ONLY));
					}
					break;
			}
		}
	}

	displayThemeResetToDefault();
}

void uiUtilityRenderHeader(bool isVFODualWatchScanning, bool isVFOSweepScanning)
{
	const int MODE_TEXT_X_OFFSET = 1;
#if defined(PLATFORM_MD9600)
	const int FILTER_TEXT_X_CENTER = 32;
	const int POWER_X_CENTER = 60;
	const int COLOR_CODE_X_CENTER = 89;
#else
	const int FILTER_TEXT_X_CENTER = 34;
	const int POWER_X_CENTER = 63;
	const int COLOR_CODE_X_CENTER = 92;
#endif
	char buffer[SCREEN_LINE_BUFFER_SIZE];
	static bool scanBlinkPhase = true;
	static uint32_t blinkTime = 0;
	uint8_t powerLevel = trxGetPowerLevel();
	bool isPerChannelPower = (currentChannelData->libreDMR_Power != 0x00);
	bool scanIsActive = (uiDataGlobal.Scan.active || uiDataGlobal.Scan.toneActive);
	bool batteryIsLow = batteryIsLowWarning();
	int volts = -1, mvolts = -1;
	int batteryPercentage = -1;
	int16_t itemOffset = 0;

	if (settingsIsOptionBitSet(BIT_BATTERY_VOLTAGE_IN_HEADER))
	{
		getBatteryVoltage(&volts, &mvolts);

		// It's not possible to have 2 digits voltage on HTs
#if defined(PLATFORM_MD9600)
		if (volts < 10)
		{
			itemOffset = 2;
		}
#endif
	}
	else
	{
		batteryPercentage = getBatteryPercentage();

		if (batteryPercentage < 100)
		{
#if defined(PLATFORM_MD9600)
			itemOffset = 2;
#else
			itemOffset = 1;
#endif
		}
	}

	if (isVFODualWatchScanning == false)
	{
		if (!trxTransmissionEnabled)
		{
			if (!isVFOSweepScanning)
			{
				uiUtilityDrawRSSIBarGraph();
			}
		}
		else
		{
			if (trxGetMode() == RADIO_MODE_DIGITAL)
			{
				uiUtilityDrawDMRMicLevelBarGraph();
			}
			else
			{
				uiUtilityDrawFMMicLevelBarGraph();
			}
		}
	}

	displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
#if defined(HAS_COLOURS)
	displayFillRect(0, 0, DISPLAY_SIZE_X, DISPLAY_Y_POS_BAR, true);
#endif

	if (scanIsActive || batteryIsLow)
	{
		if ((ticksGetMillis() - blinkTime) > (scanBlinkPhase ? 500 : 1000))
		{
			blinkTime = ticksGetMillis();
			scanBlinkPhase = !scanBlinkPhase;
		}
	}
	else
	{
		scanBlinkPhase = false;
	}

	if (isVFOSweepScanning)
	{
		int span = (VFO_SWEEP_SCAN_RANGE_SAMPLE_STEP_TABLE[uiDataGlobal.Scan.sweepStepSizeIndex] * VFO_SWEEP_NUM_SAMPLES) / (VFO_SWEEP_PIXELS_PER_STEP * 100);

		sprintf(buffer, "+/-%dkHz", (span >> 1));
		displayPrintCore(MODE_TEXT_X_OFFSET, DISPLAY_Y_POS_HEADER, buffer, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	}

	switch(trxGetMode())
	{
		case RADIO_MODE_ANALOG:
			if (isVFOSweepScanning == false)
			{
				bool isInverted = (scanIsActive ? scanBlinkPhase : false);
				int16_t modePixelLen;

				if (isVFODualWatchScanning)
				{
					strcpy(buffer, "[DW]");
				}
				else
				{
					strcpy(buffer, (trxGetBandwidthIs25kHz() ? "FM" : "FMN"));
				}

				modePixelLen = (strlen(buffer) * 6);

				if (uiDataGlobal.talkaround)
				{
					isInverted = true;
					displayFillRect(1, 1, modePixelLen, 8 + 1, false);
				}

				displayPrintCore(MODE_TEXT_X_OFFSET, DISPLAY_Y_POS_HEADER, buffer,
						(((nonVolatileSettings.hotspotType != HOTSPOT_TYPE_OFF) && (uiDataGlobal.dmrDisabled == false)) ? FONT_SIZE_1_BOLD : FONT_SIZE_1), TEXT_ALIGN_LEFT, isInverted);


				if (currentChannelData->aprsConfigIndex != 0)
				{
					bool aprsSuspended = false;

					strcpy(buffer, "APRS");

#if ! defined(PLATFORM_GD77S)
					if ((aprsBeaconingGetMode() == APRS_BEACONING_MODE_OFF) || aprsBeaconingIsSuspended())
					{
						aprsSuspended = true;
					}
#endif
					displayPrintCore(MODE_TEXT_X_OFFSET + (modePixelLen + 6), DISPLAY_Y_POS_HEADER, buffer,
							(aprsSuspended ? FONT_SIZE_1 : FONT_SIZE_1_BOLD), TEXT_ALIGN_LEFT, false);
				}
			}

			if ((monitorModeData.isEnabled == false) && (isVFOSweepScanning == false) &&
					((currentChannelData->txTone != CODEPLUG_CSS_TONE_NONE) || (currentChannelData->rxTone != CODEPLUG_CSS_TONE_NONE)))
			{
				bool cssTextInverted = (trxGetAnalogFilterLevel() == ANALOG_FILTER_NONE);

				if (currentChannelData->txTone != CODEPLUG_CSS_TONE_NONE)
				{
					strcpy(buffer, ((codeplugGetCSSType(currentChannelData->txTone) & CSS_TYPE_DCS) ? "DT" : "CT"));
				}
				else // tx and/or rx tones are enabled, no need to check for this
				{
					strcpy(buffer, ((codeplugGetCSSType(currentChannelData->rxTone) & CSS_TYPE_DCS) ? "D" : "C"));
				}

				// There is no room to display if rxTone is CTCSS or DCS, when txTone is set.
				if (currentChannelData->rxTone != CODEPLUG_CSS_TONE_NONE)
				{
					strcat(buffer, "R");
				}

				int16_t cssPixLen = (strlen(buffer) * 6);
				int16_t cssXPos = ((FILTER_TEXT_X_CENTER + (itemOffset * 1)) - (cssPixLen >> 1));
				if (cssTextInverted)
				{
					// Inverted rectangle width is fixed size, large enough to fit 3 characters
					displayFillRect((cssXPos - 1), DISPLAY_Y_POS_HEADER - 1, (cssPixLen + 1), 9, false);
				}

				displayPrintCore(cssXPos, DISPLAY_Y_POS_HEADER, buffer, FONT_SIZE_1, TEXT_ALIGN_LEFT, cssTextInverted);
			}
			break;

		case RADIO_MODE_DIGITAL:
			if (settingsUsbMode != USB_MODE_HOTSPOT)
			{
				bool contactTSActive = false;
				bool tsManOverride = false;

				if (nonVolatileSettings.extendedInfosOnScreen & (INFO_ON_SCREEN_TS & INFO_ON_SCREEN_BOTH))
				{
					contactTSActive = ((nonVolatileSettings.overrideTG == 0) && ((currentContactData.reserve1 & CODEPLUG_CONTACT_FLAG_NO_TS_OVERRIDE) == 0x00));
					tsManOverride = (contactTSActive ? tsIsContactHasBeenOverriddenFromCurrentChannel() : (tsGetManualOverrideFromCurrentChannel() != 0));
				}

				if ((isVFODualWatchScanning == false) && (isVFOSweepScanning == false))
				{
					if ((scanIsActive ? (scanBlinkPhase == false) : true) && (nonVolatileSettings.dmrDestinationFilter > DMR_DESTINATION_FILTER_NONE))
					{
						displayFillRect(0, DISPLAY_Y_POS_HEADER - 1, 20, 9, false);
					}
				}

				if (isVFOSweepScanning == false)
				{
					if (scanIsActive ? scanBlinkPhase == false : true)
					{
						bool isInverted = isVFODualWatchScanning ? false : ((scanIsActive ? scanBlinkPhase : false) ^ (nonVolatileSettings.dmrDestinationFilter > DMR_DESTINATION_FILTER_NONE));

						if (uiDataGlobal.talkaround)
						{
							isInverted = true;
							displayFillRect(1, 1, (3 * 6), 8 + 1, false);
						}

						displayPrintCore(MODE_TEXT_X_OFFSET, DISPLAY_Y_POS_HEADER, isVFODualWatchScanning ? "[DW]" : "DMR", ((nonVolatileSettings.hotspotType != HOTSPOT_TYPE_OFF) ? FONT_SIZE_1_BOLD : FONT_SIZE_1), TEXT_ALIGN_LEFT, isInverted);
					}

					if (isVFODualWatchScanning == false)
					{
						bool tsInverted = false;

						if (codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_FORCE_DMO) == 0)
						{
							snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%s%d",
									((contactTSActive && (monitorModeData.isEnabled == false)) ? "cS" : currentLanguage->ts),
									((monitorModeData.isEnabled && (dmrMonitorCapturedTS != -1))? (dmrMonitorCapturedTS + 1) : trxGetDMRTimeSlot() + 1));
						}
						else
						{
							strncpy(buffer, "DMO", SCREEN_LINE_BUFFER_SIZE);
						}

						int16_t tsPixLen = (strlen(buffer) * 6);
						int16_t tsXPos = ((FILTER_TEXT_X_CENTER + (itemOffset * 1)) - (tsPixLen >> 1));
						if (!(nonVolatileSettings.dmrCcTsFilter & DMR_TS_FILTER_PATTERN))
						{
							displayFillRect((tsXPos - 1), DISPLAY_Y_POS_HEADER - 1, (tsPixLen + 1), 9, false);
							tsInverted = true;
						}

						displayPrintCore(tsXPos, DISPLAY_Y_POS_HEADER, buffer, (tsManOverride ? FONT_SIZE_1_BOLD : FONT_SIZE_1), TEXT_ALIGN_LEFT, tsInverted);
					}
				}
			}
			break;
	}

	// Power
	sprintf(buffer, "%s%s", getPowerLevel(powerLevel), getPowerLevelUnit(powerLevel));

	if (isVFOSweepScanning) // Need to shift to the right due to sweep span
	{
		displayPrintCore((COLOR_CODE_X_CENTER - 11) - ((strlen(buffer) * 6) >> 1), DISPLAY_Y_POS_HEADER, buffer, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	}
	else
	{
		displayPrintCore(((POWER_X_CENTER + (itemOffset * 2)) - ((strlen(buffer) * 6) >> 1)), DISPLAY_Y_POS_HEADER, buffer,
				((isPerChannelPower && (nonVolatileSettings.extendedInfosOnScreen & (INFO_ON_SCREEN_PWR & INFO_ON_SCREEN_BOTH))) ? FONT_SIZE_1_BOLD : FONT_SIZE_1), TEXT_ALIGN_LEFT, false);
	}

	// In hotspot mode the CC is show as part of the rest of the display and in Analog mode the CC is meaningless
	if((isVFODualWatchScanning == false) && (isVFOSweepScanning == false) && (((settingsUsbMode == USB_MODE_HOTSPOT) || (trxGetMode() == RADIO_MODE_ANALOG)) == false))
	{
		uint8_t ccode = trxGetDMRColourCode();
		bool isNotFilteringCC = !(nonVolatileSettings.dmrCcTsFilter & DMR_CC_FILTER_PATTERN);

		snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "C%u", ccode);

		int16_t ccPixLen = (strlen(buffer) * 6);
		int16_t ccXPos = ((COLOR_CODE_X_CENTER + (itemOffset * 3)) - (ccPixLen >> 1));
		if (isNotFilteringCC)
		{
			displayFillRect((ccXPos - 1), DISPLAY_Y_POS_HEADER - 1, (ccPixLen + 1), 9, false);
		}

		displayPrintCore(ccXPos, DISPLAY_Y_POS_HEADER, buffer, FONT_SIZE_1, TEXT_ALIGN_LEFT, isNotFilteringCC);
	}

#if defined(HAS_GPS)
#if defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
	if (nonVolatileSettings.gps >= GPS_MODE_ON)
	{
		displayPrintCore(DISPLAY_SIZE_X - 50, DISPLAY_Y_POS_HEADER, currentLanguage->gps, ((gpsData.Status & GPS_STATUS_HAS_FIX) ? FONT_SIZE_1_BOLD : FONT_SIZE_1), TEXT_ALIGN_LEFT, false);
	}
#endif
#endif

	// Display battery percentage/voltage
	bool apoEnabled = (nonVolatileSettings.apo > 0);
	if (settingsIsOptionBitSet(BIT_BATTERY_VOLTAGE_IN_HEADER))
	{
		int16_t xV = (DISPLAY_SIZE_X - ((4 * 6) + 3));

		snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%2d", volts);
		displayPrintCore(xV, DISPLAY_Y_POS_HEADER, buffer, (apoEnabled ? FONT_SIZE_1_BOLD : FONT_SIZE_1), TEXT_ALIGN_LEFT, ((batteryIsLow ? scanBlinkPhase : false)));

		displayDrawRect(xV + (6 * 2), DISPLAY_Y_POS_HEADER + 5, 2, 2, ((batteryIsLow ? !scanBlinkPhase : true)));

		snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%1dV", mvolts);
		displayPrintCore(xV + (6 * 2) + 3, DISPLAY_Y_POS_HEADER, buffer, (apoEnabled ? FONT_SIZE_1_BOLD : FONT_SIZE_1), TEXT_ALIGN_LEFT, ((batteryIsLow ? scanBlinkPhase : false)));
	}
	else
	{
		snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%d%%", batteryPercentage);
		displayPrintCore(0, DISPLAY_Y_POS_HEADER, buffer, (apoEnabled ? FONT_SIZE_1_BOLD : FONT_SIZE_1), TEXT_ALIGN_RIGHT, ((batteryIsLow ? scanBlinkPhase : false)));// Display battery percentage at the right
	}

	displayThemeResetToDefault();
}

void uiUtilityRedrawHeaderOnly(bool isVFODualWatchScanning, bool isVFOSweepScanning)
{
	if (isVFOSweepScanning)
	{
		displayFillRect(0, 0, DISPLAY_SIZE_X, 9, true);
	}
	else
	{
#if defined(PLATFORM_RD5R)
		displayClearRows(0, 1, false);
#else
		displayClearRows(0, 2, false);
#endif
	}

	uiUtilityRenderHeader(isVFODualWatchScanning, isVFOSweepScanning);
	displayRenderRows(0, 2);
}

static void drawHeaderBar(int *barWidth, int16_t barHeight)
{
	*barWidth = CLAMP(*barWidth, 0, DISPLAY_SIZE_X);

	displayThemeApply(THEME_ITEM_FG_RSSI_BAR, THEME_ITEM_BG_HEADER_TEXT);
	if (*barWidth)
	{
		displayFillRect(0, DISPLAY_Y_POS_BAR, *barWidth, barHeight, false);
	}

	// Clear the end of the bar area, if needed
	if (*barWidth < DISPLAY_SIZE_X)
	{
		displayFillRect(*barWidth, DISPLAY_Y_POS_BAR, (DISPLAY_SIZE_X - *barWidth), barHeight, true);
	}

	displayThemeResetToDefault();
}

void uiUtilityDrawRSSIBarGraph(void)
{
	int rssi = trxGetRSSIdBm(RADIO_DEVICE_PRIMARY);

	if ((rssi > SMETER_S9) && (trxGetMode() == RADIO_MODE_ANALOG))
	{
		// In Analog mode, the max RSSI value from the hardware is over S9+60.
		// So scale this to fit in the last 30% of the display
		rssi = ((rssi - SMETER_S9) / STRONG_SIGNAL_RESCALE) + SMETER_S9;

		// in Digital mode. The maximum signal is around S9+10 dB.
		// So no scaling is required, as the full scale value is approximately S9+10dB
	}


	// Scale the entire bar by 2.
	// Because above S9 the values are scaled to 1/5. This results in the signal below S9 being doubled in scale
	// Signals above S9 the scales is compressed to 2/5.
	rssi = (rssi - SMETER_S0) * 2;

	int barWidth = ((rssi * rssiMeterHeaderBarNumUnits) / rssiMeterHeaderBarDivider);

	drawHeaderBar(&barWidth, 4);

#if defined(HAS_COLOURS)
	int xPos = 0;

	if (rssi > SMETER_S9)
	{
		xPos = (rssiMeterHeaderBar[9] * 2);

		if (barWidth > xPos)
		{
			displayThemeApply(THEME_ITEM_FG_RSSI_BAR_S9P, THEME_ITEM_BG_HEADER_TEXT);
			displayFillRect(xPos, DISPLAY_Y_POS_BAR, (barWidth - xPos), 4, false);
			displayThemeResetToDefault();
		}
	}
#endif

#if 0 // Commented for now, maybe an option later.
	int currentMode = trxGetMode();
	for (uint8_t i = 1; ((i < 10) && (xPos <= barWidth)); i += 2)
	{
		if ((i <= 9) || (currentMode == RADIO_MODE_DIGITAL))
		{
			xPos = rssiMeterHeaderBar[i];
		}
		else
		{
			xPos = ((rssiMeterHeaderBar[i] - rssiMeterHeaderBar[9]) / STRONG_SIGNAL_RESCALE) + rssiMeterHeaderBar[9];
		}
		xPos *= 2;

		ucDrawFastVLine(xPos, (DISPLAY_Y_POS_BAR + 1), 2, false);
	}
#endif
}

void uiUtilityDrawFMMicLevelBarGraph(void)
{
	trxReadVoxAndMicStrength();

	uint16_t micdB =
#if defined(PLATFORM_MD9600) || defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
			trxTxMic + // trxTxMic is in 0.2dB unit
			((nonVolatileSettings.micGainFM) * 15)   //  mic gain adjustment is 3dB per increment of micGainFM, so scale it to 0.2dB increments

#else
			(trxTxMic >> 1) // trxTxMic is in 0.5dB unit, displaying 50dB .. 100dB
#endif
			;

	int barWidth = SAFE_MIN(
#if defined(PLATFORM_MD9600) || defined(PLATFORM_MD380) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
			((uint16_t)(((float)DISPLAY_SIZE_X / 200.0) * ((float)micdB - 150.0))) // display from 30dB to 70dB, = 150 to  350 units and span over 160pix
#else
			((uint16_t)(((float)DISPLAY_SIZE_X / 50.0) * ((float)micdB - 50.0))) // display from 50dB to 100dB, span over 128pix
#endif
			, DISPLAY_SIZE_X);

	drawHeaderBar(&barWidth, 4);
}

void uiUtilityDrawDMRMicLevelBarGraph(void)
{
	int barWidth = ((uint16_t)(sqrt(micAudioSamplesTotal) * 1.5));
	drawHeaderBar(&barWidth, 4);
}

void setOverrideTGorPC(uint32_t tgOrPc, bool privateCall)
{
	uiDataGlobal.tgBeforePcMode = 0;

	if (privateCall == true)
	{
		// Private Call

		if ((trxTalkGroupOrPcId >> 24) != PC_CALL_FLAG)
		{
			// if the current Tx TG is a TalkGroup then save it so it can be restored after the end of the private call
			uiDataGlobal.tgBeforePcMode = trxTalkGroupOrPcId;
		}

		tgOrPc |= (PC_CALL_FLAG << 24);
	}

	settingsSet(nonVolatileSettings.overrideTG, tgOrPc);
}

void uiUtilityDisplayFrequency(uint8_t y, bool isTX, bool hasFocus, uint32_t frequency, bool displayVFOChannel, bool isScanMode, uint8_t dualWatchVFO)
{
	char buffer[SCREEN_LINE_BUFFER_SIZE];
	int val_before_dp = frequency / 100000;
	int val_after_dp = frequency - val_before_dp * 100000;

	displayThemeApply(isTX ? THEME_ITEM_FG_TX_FREQ : THEME_ITEM_FG_RX_FREQ, THEME_ITEM_BG);

	// Focus + direction
	snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%c%c", ((hasFocus && !isScanMode)? '>' : ' '), (isTX ? 'T' : 'R'));

	displayPrintAt(0, y, buffer, FONT_SIZE_3);
	// VFO
	if (displayVFOChannel)
	{
		displayPrintAt(16, y + VFO_LETTER_Y_OFFSET, (((dualWatchVFO == 0) && (nonVolatileSettings.currentVFONumber == 0)) || (dualWatchVFO == 1)) ? "A" : "B", FONT_SIZE_1);
	}
	// Frequency
	snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%d.%05d", val_before_dp, val_after_dp);
	displayPrintAt(FREQUENCY_X_POS, y, buffer, FONT_SIZE_3);
	displayPrintAt(DISPLAY_SIZE_X - (3 * 8), y, "MHz", FONT_SIZE_3);

	displayThemeResetToDefault();
}

size_t dcsPrintf(char *dest, size_t maxLen, char *prefix, uint16_t tone)
{
	return snprintf(dest, maxLen, "%sD%03X%c", (prefix ? prefix : ""), (tone & ~CSS_TYPE_DCS_MASK), ((tone & CSS_TYPE_DCS_INVERTED) ? 'I' : 'N'));
}

void freqEnterReset(void)
{
	memset(uiDataGlobal.FreqEnter.digits, '-', FREQ_ENTER_DIGITS_MAX);
	uiDataGlobal.FreqEnter.index = 0;
}

int freqEnterRead(int startDigit, int endDigit, bool simpleDigits)
{
	int result = 0;

	if (((startDigit >= 0) && (startDigit <= FREQ_ENTER_DIGITS_MAX)) && ((endDigit >= 0) && (endDigit <= FREQ_ENTER_DIGITS_MAX)))
	{
		for (int i = startDigit; i < endDigit; i++)
		{
			result = result * 10;
			if ((uiDataGlobal.FreqEnter.digits[i] >= '0') && (uiDataGlobal.FreqEnter.digits[i] <= '9'))
			{
				result = result + uiDataGlobal.FreqEnter.digits[i] - '0';
			}
			else
			{
				if (simpleDigits) // stop here, simple numbers are wanted
				{
					result /= 10;
					break;
				}
			}
		}
	}

	return result;
}

int getBatteryPercentage(void)
{
	return SAFE_MAX(0, SAFE_MIN(((int)(((averageBatteryVoltage - CUTOFF_VOLTAGE_UPPER_HYST) * 100) / (BATTERY_MAX_VOLTAGE - CUTOFF_VOLTAGE_UPPER_HYST))), 100));
}

void getBatteryVoltage(int *volts, int *mvolts)
{
	*volts = (int)(averageBatteryVoltage / 10);
	*mvolts = (int)(averageBatteryVoltage - (*volts * 10));
}

bool increasePowerLevel(bool allowFullPower)
{
	bool powerHasChanged = false;

	if (currentChannelData->libreDMR_Power != 0x00)
	{
		if (currentChannelData->libreDMR_Power < (MAX_POWER_SETTING_NUM - 1 + CODEPLUG_MIN_PER_CHANNEL_POWER) + (allowFullPower ? 1 : 0))
		{
			currentChannelData->libreDMR_Power++;
			trxSetPowerFromLevel(currentChannelData->libreDMR_Power - 1);
			powerHasChanged = true;
		}
	}
	else
	{
		if (nonVolatileSettings.txPowerLevel < (MAX_POWER_SETTING_NUM - 1 + (allowFullPower ? 1 : 0)))
		{
			settingsIncrement(nonVolatileSettings.txPowerLevel, 1);
			trxSetPowerFromLevel(nonVolatileSettings.txPowerLevel);
			powerHasChanged = true;
		}
	}

	announceItem(PROMPT_SEQUENCE_POWER, PROMPT_THRESHOLD_3);

	uiNotificationShow(NOTIFICATION_TYPE_POWER, NOTIFICATION_ID_POWER, 1000, NULL, true);

	return powerHasChanged;
}

bool decreasePowerLevel(void)
{
	bool powerHasChanged = false;

	if (currentChannelData->libreDMR_Power != 0x00)
	{
		if (currentChannelData->libreDMR_Power > CODEPLUG_MIN_PER_CHANNEL_POWER)
		{
			currentChannelData->libreDMR_Power--;
			trxSetPowerFromLevel(currentChannelData->libreDMR_Power - 1);
			powerHasChanged = true;
		}
	}
	else
	{
		if (nonVolatileSettings.txPowerLevel > 0)
		{
			settingsDecrement(nonVolatileSettings.txPowerLevel, 1);
			trxSetPowerFromLevel(nonVolatileSettings.txPowerLevel);
			powerHasChanged = true;
		}
	}

	announceItem(PROMPT_SEQUENCE_POWER, PROMPT_THRESHOLD_3);

	uiNotificationShow(NOTIFICATION_TYPE_POWER, NOTIFICATION_ID_POWER, 1000, NULL, true);

	return powerHasChanged;
}

ANNOUNCE_STATIC void announceRadioMode(bool voicePromptWasPlaying)
{
	int radioMode = trxGetMode();

	if (!voicePromptWasPlaying)
	{
		voicePromptsAppendLanguageString(currentLanguage->mode);
	}
	voicePromptsAppendPrompt(((radioMode == RADIO_MODE_DIGITAL) ? PROMPT_DMR : PROMPT_FM));

	if ((radioMode == RADIO_MODE_ANALOG) && (currentChannelData->aprsConfigIndex != 0))
	{
		voicePromptsAppendLanguageString(currentLanguage->APRS);
	}
}

ANNOUNCE_STATIC void announceZoneName(bool voicePromptWasPlaying)
{
	char nameBuf[17];

	if (!voicePromptWasPlaying)
	{
		voicePromptsAppendLanguageString(currentLanguage->zone);
	}

	codeplugUtilConvertBufToString(currentZone.name, nameBuf, 16);

	if (strcmp(nameBuf, currentLanguage->all_channels) == 0)
	{
		voicePromptsAppendLanguageString(currentLanguage->all_channels);
	}
	else
	{
		voicePromptsAppendString(nameBuf);
	}
}

ANNOUNCE_STATIC void announceDistance(bool voicePromptWasPlaying)
{
	char distBuf[17];

	if ((currentChannelData->NOT_IN_CODEPLUG_CALCULATED_DISTANCE_X10 != -1) &&
		(settingsIsOptionBitSet(BIT_DISPLAY_CHANNEL_DISTANCE) || (settingsIsOptionBitSet(BIT_SORT_CHANNEL_DISTANCE) && (CODEPLUG_ZONE_IS_ALLCHANNELS(currentZone) == false))))
	{
		if (!voicePromptWasPlaying)
		{
			voicePromptsAppendPrompt(PROMPT_DISTANCE);
		}

		uint32_t intPart = (currentChannelData->NOT_IN_CODEPLUG_CALCULATED_DISTANCE_X10 / 10);
		uint32_t decPart = (currentChannelData->NOT_IN_CODEPLUG_CALCULATED_DISTANCE_X10 - (intPart * 10));

		if (decPart != 0)
		{
			snprintf(distBuf, SCREEN_LINE_BUFFER_SIZE, "%u.%u", intPart, decPart);
		}
		else
		{
			snprintf(distBuf, SCREEN_LINE_BUFFER_SIZE, "%u", intPart);
		}

		voicePromptsAppendString(distBuf);

		voicePromptsAppendPrompt(PROMPT_KILOMETERS);
	}
}


ANNOUNCE_STATIC void announceContactNameTgOrPc(bool voicePromptWasPlaying)
{
	if (nonVolatileSettings.overrideTG == 0)
	{
		if (!voicePromptWasPlaying)
		{
			voicePromptsAppendLanguageString(currentLanguage->contact);
		}
		char nameBuf[17];

		codeplugUtilConvertBufToString(currentContactData.name, nameBuf, 16);
		voicePromptsAppendString(nameBuf);
	}
	else
	{
		char buf[17];

		itoa(nonVolatileSettings.overrideTG & 0xFFFFFF, buf, 10);
		if ((nonVolatileSettings.overrideTG >> 24) == PC_CALL_FLAG)
		{
			if (!voicePromptWasPlaying)
			{
				voicePromptsAppendLanguageString(currentLanguage->private_call);
			}
			voicePromptsAppendString("ID");
		}
		else
		{
			voicePromptsAppendPrompt(PROMPT_TALKGROUP);
		}
		voicePromptsAppendString(buf);
	}
}

ANNOUNCE_STATIC void announcePowerLevel(bool voicePromptWasPlaying)
{
	uint8_t powerLevel = trxGetPowerLevel();

	if (!voicePromptWasPlaying)
	{
		voicePromptsAppendPrompt(PROMPT_POWER);
	}

	if (powerLevel < 9)
	{
		voicePromptsAppendString((char *)getPowerLevel(powerLevel));
		switch(powerLevel)
		{
			case 0://50mW
			case 1://250mW
			case 2://500mW
			case 3://750mW
				voicePromptsAppendPrompt(PROMPT_MILLIWATTS);
				break;

			case 4://1W
				voicePromptsAppendPrompt(PROMPT_WATT);
				break;

			default:
				voicePromptsAppendPrompt(PROMPT_WATTS);
				break;
		}
	}
	else
	{
		voicePromptsAppendLanguageString(currentLanguage->user_power);
	}
}

#if defined(PLATFORM_GD77S)
void announceEcoLevel(bool voicePromptWasPlaying)
{

	if (!voicePromptWasPlaying)
	{
		voicePromptsAppendLanguageString(currentLanguage->eco_level);
	}

	voicePromptsAppendInteger(nonVolatileSettings.ecoLevel);
}
#endif

ANNOUNCE_STATIC void announceTemperature(bool voicePromptWasPlaying)
{
	char buffer[17];
	int temperature = getTemperature();
	if (!voicePromptWasPlaying)
	{
		voicePromptsAppendLanguageString(currentLanguage->temperature);
	}
	snprintf(buffer, 17, "%d.%1d", (temperature / 10), (temperature % 10));
	voicePromptsAppendString(buffer);
	voicePromptsAppendLanguageString(currentLanguage->celcius);
}

ANNOUNCE_STATIC void announceBatteryVoltage(void)
{
	char buffer[17];
	int volts, mvolts;

	voicePromptsAppendLanguageString(currentLanguage->battery);
	getBatteryVoltage(&volts,  &mvolts);
	snprintf(buffer, 17, " %1d.%1d", volts, mvolts);
	voicePromptsAppendString(buffer);
	voicePromptsAppendPrompt(PROMPT_VOLTS);
}

ANNOUNCE_STATIC void announceBatteryPercentage(void)
{
	voicePromptsAppendLanguageString(currentLanguage->battery);
	voicePromptsAppendInteger(getBatteryPercentage());
	voicePromptsAppendPrompt(PROMPT_PERCENT);
}

ANNOUNCE_STATIC void announceTS(void)
{
	if (codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_FORCE_DMO) == 0)
	{
		voicePromptsAppendLanguageString(currentLanguage->timeSlot);
	}
	else
	{
		voicePromptsAppendString("DMO");
	}
	voicePromptsAppendInteger(trxGetDMRTimeSlot() + 1);
}

ANNOUNCE_STATIC void announceCC(void)
{
	voicePromptsAppendLanguageString(currentLanguage->colour_code);
	voicePromptsAppendInteger(trxGetDMRColourCode());
}

ANNOUNCE_STATIC void announceChannelName(bool voicePromptWasPlaying)
{

	if (!voicePromptWasPlaying)
	{
		voicePromptsAppendPrompt(PROMPT_CHANNEL);
	}

	if (uiDataGlobal.reverseRepeaterChannel)
	{
		voicePromptsAppendPrompt(PROMPT_REVERSE_REPEATER);
	}

	if(uiDataGlobal.talkaround)
	{
		voicePromptsAppendLanguageString(currentLanguage->talkaround);
	}

	if (currentZone.NOT_IN_CODEPLUGDATA_numChannelsInZone > 0)
	{
		char voiceBuf[17];
		codeplugUtilConvertBufToString(channelScreenChannelData.name, voiceBuf, 16);
		voicePromptsAppendString(voiceBuf);
	}
	else
	{
		voicePromptsAppendLanguageString(currentLanguage->zone_empty);
	}
}

static void removeUnnecessaryZerosFromVoicePrompts(char *str)
{
	const int NUM_DECIMAL_PLACES = 1;
	int len = strlen(str);

	for(int i = len; i > 2; i--)
	{
		if ((str[i - 1] != '0') || (str[i - (NUM_DECIMAL_PLACES + 1)] == '.'))
		{
			str[i] = 0;
			return;
		}
	}
}

static void announceQRG(uint32_t qrg, bool unit)
{
	char buffer[17];
	int val_before_dp = qrg / 100000;
	int val_after_dp = qrg - val_before_dp * 100000;

	snprintf(buffer, 17, "%d.%05d", val_before_dp, val_after_dp);
	removeUnnecessaryZerosFromVoicePrompts(buffer);
	voicePromptsAppendString(buffer);

	if (unit)
	{
		voicePromptsAppendPrompt(PROMPT_MEGAHERTZ);
	}
}

ANNOUNCE_STATIC void announceFrequency(void)
{
	bool duplex = (currentChannelData->txFreq != currentChannelData->rxFreq);

	if (duplex)
	{
		voicePromptsAppendPrompt(PROMPT_RECEIVE);
	}

	announceQRG(currentChannelData->rxFreq, true);

	if (duplex)
	{
		voicePromptsAppendPrompt(PROMPT_TRANSMIT);
		announceQRG(currentChannelData->txFreq, true);
	}
}

ANNOUNCE_STATIC void announceVFOChannelName(void)
{
	voicePromptsAppendPrompt(PROMPT_VFO);
	voicePromptsAppendString((nonVolatileSettings.currentVFONumber == 0) ? "A" : "B");
	voicePromptsAppendPrompt(PROMPT_SILENCE);
}

ANNOUNCE_STATIC void announceVFOAndFrequency(bool announceVFOName)
{
	if (announceVFOName)
	{
		announceVFOChannelName();
	}
	announceFrequency();
}

ANNOUNCE_STATIC void announceSquelchLevel(bool voicePromptWasPlaying)
{
	static const int BUFFER_LEN = 8;
	char buf[BUFFER_LEN];

	if (!voicePromptWasPlaying)
	{
		voicePromptsAppendLanguageString(currentLanguage->squelch);
	}

	snprintf(buf, BUFFER_LEN, "%d%%", 5 * (((currentChannelData->sql == 0) ? nonVolatileSettings.squelchDefaults[currentRadioDevice->trxCurrentBand[TRX_RX_FREQ_BAND]] : currentChannelData->sql)-1));
	voicePromptsAppendString(buf);
}

void announceChar(char ch)
{
	if (nonVolatileSettings.audioPromptMode < AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
	{
		return;
	}

	char buf[2] = { ch, 0 };

	voicePromptsInit();
	voicePromptsAppendString(buf);
	voicePromptsPlay();
}

void buildCSSCodeVoicePrompts(uint16_t tone, CodeplugCSSTypes_t cssType, Direction_t direction, bool announceType)
{
	static const int BUFFER_LEN = 6;
	char buf[BUFFER_LEN];

	switch(direction)
	{
		case DIRECTION_RECEIVE:
			voicePromptsAppendString("RX");
			break;

		case DIRECTION_TRANSMIT:
			voicePromptsAppendString("TX");
			break;

		default:
			break;
	}

	voicePromptsAppendPrompt(PROMPT_SILENCE);

	if (cssType == CSS_TYPE_NONE)
	{
		voicePromptsAppendString("CSS");
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		voicePromptsAppendLanguageString(currentLanguage->none);
	}
	else if (cssType == CSS_TYPE_CTCSS)
	{
		if (announceType)
		{
			voicePromptsAppendString("CTCSS");
			voicePromptsAppendPrompt(PROMPT_SILENCE);
		}
		snprintf(buf, BUFFER_LEN, "%d.%d", tone / 10, tone % 10);
		voicePromptsAppendString(buf);
		voicePromptsAppendPrompt(PROMPT_HERTZ);
	}
	else if (cssType & CSS_TYPE_DCS)
	{
		if (announceType)
		{
			voicePromptsAppendString("DCS");
			voicePromptsAppendPrompt(PROMPT_SILENCE);
		}

		dcsPrintf(buf, BUFFER_LEN, NULL, tone);
		voicePromptsAppendString(buf);
	}
}


void announceCSSCode(uint16_t tone, CodeplugCSSTypes_t cssType, Direction_t direction, bool announceType, audioPromptThreshold_t immediateAnnounceThreshold)
{
	if (nonVolatileSettings.audioPromptMode < AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
	{
		return;
	}

	bool voicePromptWasPlaying = voicePromptsIsPlaying();

	voicePromptsInit();

	buildCSSCodeVoicePrompts(tone, cssType, direction, announceType);

	// Follow-on when voicePromptWasPlaying is enabled on voice prompt level 2 and above
	// Prompts are voiced immediately on voice prompt level 3
	if ((voicePromptWasPlaying && (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_LEVEL_2)) ||
			(nonVolatileSettings.audioPromptMode >= immediateAnnounceThreshold))
	{
		voicePromptsPlay();
	}
}

static void announceChannelNameOrVFOFrequency(bool voicePromptWasPlaying, bool announceVFOName)
{
	if (menuSystemGetCurrentMenuNumber() == UI_CHANNEL_MODE)
	{
		announceChannelName(voicePromptWasPlaying);
	}
	else
	{
		announceVFOAndFrequency(announceVFOName);
	}
}

void announceItemWithInit(bool init, voicePromptItem_t item, audioPromptThreshold_t immediateAnnounceThreshold)
{
	if (nonVolatileSettings.audioPromptMode < AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
	{
		return;
	}

	bool voicePromptWasPlaying = voicePromptsIsPlaying();

	if (init)
	{
		voicePromptsInit();
	}

	switch(item)
	{
		case PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ:
		case PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ_AND_MODE:
		case PROMPT_SEQUENCE_CHANNEL_NAME_AND_CONTACT_OR_VFO_FREQ_AND_MODE:
		case PROMPT_SEQUENCE_ZONE_NAME_CHANNEL_NAME_AND_CONTACT_OR_VFO_FREQ_AND_MODE:
		case PROMPT_SEQUENCE_ZONE_NAME_CHANNEL_NAME_AND_CONTACT_OR_VFO_FREQ_AND_MODE_AND_TS_AND_CC:
		case PROMPT_SEQUENCE_CHANNEL_NAME_AND_CONTACT_OR_VFO_FREQ_AND_MODE_AND_TS_AND_CC:
		case PROMPT_SEQUENCE_VFO_FREQ_UPDATE:
		{
			uint32_t lFreq, hFreq;

			if (((nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_LEVEL_2) || (immediateAnnounceThreshold == PROMPT_THRESHOLD_NEVER_PLAY_IMMEDIATELY)) &&
					((item == PROMPT_SEQUENCE_ZONE_NAME_CHANNEL_NAME_AND_CONTACT_OR_VFO_FREQ_AND_MODE) ||
							(item == PROMPT_SEQUENCE_ZONE_NAME_CHANNEL_NAME_AND_CONTACT_OR_VFO_FREQ_AND_MODE_AND_TS_AND_CC)))
			{
				announceZoneName(voicePromptWasPlaying);

				announceDistance(voicePromptWasPlaying);
			}

			announceChannelNameOrVFOFrequency(voicePromptWasPlaying, (item != PROMPT_SEQUENCE_VFO_FREQ_UPDATE));

			if (uiVFOModeFrequencyScanningIsActiveAndEnabled(&lFreq, &hFreq))
			{
				voicePromptsAppendPrompt(PROMPT_SCAN_MODE);
				voicePromptsAppendLanguageString(currentLanguage->low);
				announceQRG(lFreq, true);
				voicePromptsAppendLanguageString(currentLanguage->high);
				announceQRG(hFreq, true);
			}

			if (item == PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ)
			{
				break;
			}
			else if (item == PROMPT_SEQUENCE_VFO_FREQ_UPDATE)
			{
				announceVFOChannelName();
			}

			announceRadioMode(voicePromptWasPlaying);

			if (item == PROMPT_SEQUENCE_CHANNEL_NAME_OR_VFO_FREQ_AND_MODE)
			{
				break;
			}

			if (trxGetMode() == RADIO_MODE_DIGITAL)
			{
				if ((item == PROMPT_SEQUENCE_ZONE_NAME_CHANNEL_NAME_AND_CONTACT_OR_VFO_FREQ_AND_MODE_AND_TS_AND_CC) ||
						(item == PROMPT_SEQUENCE_CHANNEL_NAME_AND_CONTACT_OR_VFO_FREQ_AND_MODE_AND_TS_AND_CC))
				{
					announceTS();
					announceCC();
				}

				announceContactNameTgOrPc(false);// false = force the title "Contact" to be played to always separate the Channel name announcement from the Contact name
			}
		}
		break;

		case PROMPT_SEQUENCE_ZONE:
			announceZoneName(voicePromptWasPlaying);
			break;

		case PROMPT_SEQUENCE_MODE:
			announceRadioMode(voicePromptWasPlaying);
			break;

		case PROMPT_SEQUENCE_CONTACT_TG_OR_PC:
			announceContactNameTgOrPc(voicePromptWasPlaying);
			break;

		case PROMPT_SEQUENCE_TS:
			announceTS();
			break;

		case PROMPT_SEQUENCE_CC:
			announceCC();
			break;

		case PROMPT_SEQUENCE_POWER:
			announcePowerLevel(voicePromptWasPlaying);
			break;

		case PROMPT_SEQUENCE_BATTERY:
			if (settingsIsOptionBitSet(BIT_BATTERY_VOLTAGE_IN_HEADER))
			{
				announceBatteryVoltage();
			}
			else
			{
				announceBatteryPercentage();
			}
			break;

		case PROMPT_SQUENCE_SQUELCH:
			announceSquelchLevel(voicePromptWasPlaying);
			break;

		case PROMPT_SEQUENCE_TEMPERATURE:
			announceTemperature(voicePromptWasPlaying);
			break;

		case PROMPT_SEQUENCE_DIRECTION_TX:
			voicePromptsAppendString("TX");
			break;

		case PROMPT_SEQUENCE_DIRECTION_RX:
			voicePromptsAppendString("RX");
			break;

		default:
			break;
	}
	// Follow-on when voicePromptWasPlaying is enabled on voice prompt level 2 and above
	// Prompts are voiced immediately on voice prompt level 3
	if ((voicePromptWasPlaying && (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_LEVEL_2)) ||
			(nonVolatileSettings.audioPromptMode >= immediateAnnounceThreshold))
	{
		voicePromptsPlay();
	}
}

void announceItem(voicePromptItem_t item, audioPromptThreshold_t immediateAnnounceThreshold)
{
	announceItemWithInit(true, item, immediateAnnounceThreshold);
}

void promptsPlayNotAfterTx(void)
{
	if (menuSystemGetPreviouslyPushedMenuNumber(false) != UI_TX_SCREEN)
	{
		voicePromptsPlay();
	}
}

void uiUtilityBuildTgOrPCDisplayName(char *nameBuf, int bufferLen)
{
	int contactIndex;
	struct_codeplugContact_t contact;
	uint32_t id = (trxTalkGroupOrPcId & 0x00FFFFFF);
	int8_t manTS = tsGetManualOverrideFromCurrentChannel();

	if ((trxTalkGroupOrPcId >> 24) == TG_CALL_FLAG)
	{
		contactIndex = codeplugContactIndexByTGorPC(id, CONTACT_CALLTYPE_TG, &contact, (manTS ? manTS : (trxGetDMRTimeSlot() + 1)));
		if (contactIndex == -1)
		{
			if (id == ALL_CALL_VALUE)
			{
				snprintf(nameBuf, bufferLen, "%s", currentLanguage->all_call);
			}
			else
			{
				snprintf(nameBuf, bufferLen, "%s %u", currentLanguage->tg, (trxTalkGroupOrPcId & 0x00FFFFFF));
			}
		}
		else
		{
			codeplugUtilConvertBufToString(contact.name, nameBuf, 16);
		}
	}
	else
	{
		contactIndex = codeplugContactIndexByTGorPC(id, CONTACT_CALLTYPE_PC, &contact, (manTS ? manTS : (trxGetDMRTimeSlot() + 1)));
		if (contactIndex == -1)
		{
			dmrIdDataStruct_t currentRec;
			if (dmrIDLookup(id, &currentRec))
			{
				snprintf(nameBuf, bufferLen, currentRec.text);
			}
			else
			{
				// check LastHeard for TA data.
				LinkItem_t *item = lastHeardFindInList(id);
				if ((item != NULL) && (strlen(item->talkerAlias) != 0))
				{
					snprintf(nameBuf, bufferLen, item->talkerAlias);
				}
				else
				{
					if (id == ALL_CALL_VALUE)
					{
						snprintf(nameBuf, bufferLen, "%s", currentLanguage->all_call);
					}
					else
					{
						snprintf(nameBuf, bufferLen, "ID:%u", id);
					}
				}
			}
		}
		else
		{
			codeplugUtilConvertBufToString(contact.name, nameBuf, 16);
		}
	}
}

void acceptPrivateCall(uint32_t id, int timeslot)
{
	uiDataGlobal.PrivateCall.state = PRIVATE_CALL;
	uiDataGlobal.PrivateCall.lastID = (id & 0xffffff);
	uiDataGlobal.receivedPcId = 0x00;

	setOverrideTGorPC(uiDataGlobal.PrivateCall.lastID, true);

#if !defined(PLATFORM_GD77S)
	if (timeslot != trxGetDMRTimeSlot())
	{
		trxSetDMRTimeSlot(timeslot, true);
		tsSetManualOverride(((menuSystemGetRootMenuNumber() == UI_CHANNEL_MODE) ? CHANNEL_CHANNEL : (CHANNEL_VFO_A + nonVolatileSettings.currentVFONumber)), (timeslot + 1));
	}
#else
	UNUSED_PARAMETER(timeslot);
#endif

	announceItem(PROMPT_SEQUENCE_CONTACT_TG_OR_PC, PROMPT_THRESHOLD_3);
}

bool rebuildVoicePromptOnExtraLongSK1(uiEvent_t *ev)
{
	if (BUTTONCHECK_EXTRALONGDOWN(ev, BUTTON_SK1) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0) && (BUTTONCHECK_SHORTUP(ev, BUTTON_SK2) == 0) && (ev->keys.key == 0))
	{
		// SK2 is still held down, but SK1 just get released (reverseRepeater in VFO only, as now the one in Channel mode doesn't use the SK1+SK2 anymore).
		if (uiDataGlobal.reverseRepeaterVFO)
		{
			return false;
		}

		if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
		{
			int currentMenu = menuSystemGetCurrentMenuNumber();

			if ((((currentMenu == UI_CHANNEL_MODE) && (uiDataGlobal.Scan.active && (uiDataGlobal.Scan.state != SCAN_STATE_PAUSED))) ||
					((currentMenu == UI_VFO_MODE) && ((uiDataGlobal.Scan.active && (uiDataGlobal.Scan.state != SCAN_STATE_PAUSED)) || uiDataGlobal.Scan.toneActive))) == false)
			{
				if (voicePromptsIsPlaying())
				{
					voicePromptsTerminate();
				}
				else
				{
					announceItem(((currentMenu == UI_VFO_MODE) ?
							PROMPT_SEQUENCE_CHANNEL_NAME_AND_CONTACT_OR_VFO_FREQ_AND_MODE_AND_TS_AND_CC : PROMPT_SEQUENCE_ZONE_NAME_CHANNEL_NAME_AND_CONTACT_OR_VFO_FREQ_AND_MODE_AND_TS_AND_CC),
							PROMPT_THRESHOLD_NEVER_PLAY_IMMEDIATELY);

					if (trxGetMode() == RADIO_MODE_ANALOG)
					{
						CodeplugCSSTypes_t type = codeplugGetCSSType(currentChannelData->rxTone);
						if ((type & CSS_TYPE_NONE) == 0)
						{
							buildCSSCodeVoicePrompts(currentChannelData->rxTone, type, DIRECTION_RECEIVE, true);
							voicePromptsAppendPrompt(PROMPT_SILENCE);
						}

						type = codeplugGetCSSType(currentChannelData->txTone);
						if ((type & CSS_TYPE_NONE) == 0)
						{
							buildCSSCodeVoicePrompts(currentChannelData->txTone, type, DIRECTION_TRANSMIT, true);
							voicePromptsAppendPrompt(PROMPT_SILENCE);
						}
					}

					announceItemWithInit(false, PROMPT_SEQUENCE_POWER, PROMPT_THRESHOLD_NEVER_PLAY_IMMEDIATELY);

					if (currentMenu == UI_VFO_MODE)
					{
						announceItemWithInit(false, (uiVFOModeIsTXFocused() ? PROMPT_SEQUENCE_DIRECTION_TX : PROMPT_SEQUENCE_DIRECTION_RX), PROMPT_THRESHOLD_NEVER_PLAY_IMMEDIATELY);
					}

					voicePromptsPlay();
				}

				return true;
			}
		}
	}

	return false;
}

bool repeatVoicePromptOnSK1(uiEvent_t *ev)
{
	if (BUTTONCHECK_SHORTUP(ev, BUTTON_SK1) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0) && (ev->keys.key == 0))
	{
		if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
		{
			int currentMenu = menuSystemGetCurrentMenuNumber();

			if ((((currentMenu == UI_CHANNEL_MODE) && (uiDataGlobal.Scan.active && (uiDataGlobal.Scan.state != SCAN_STATE_PAUSED))) ||
					((currentMenu == UI_VFO_MODE) && ((uiDataGlobal.Scan.active && (uiDataGlobal.Scan.state != SCAN_STATE_PAUSED)) || uiDataGlobal.Scan.toneActive))) == false)
			{
				if (!voicePromptsIsPlaying())
				{
					// The following updates the VP buffer, in VFO mode only, and if frequency scanning mode is active
					if (uiVFOModeFrequencyScanningIsActiveAndEnabled(NULL, NULL) && (voicePromptsDoesItContainPrompt(PROMPT_SCAN_MODE) == false))
					{
						voicePromptsAppendPrompt(PROMPT_SCAN_MODE);
					}

					voicePromptsPlay();
				}
				else
				{
					voicePromptsTerminate();
				}
			}

			return true;
		}
	}

	return false;
}

bool handleMonitorMode(uiEvent_t *ev)
{
	// Time by which a DMR signal should have been decoded, including locking on to a DMR signal on a different CC
	const int DMR_MODE_CC_DETECT_TIME_MS = 375;// WAS:250;// Normally it seems to take about 125mS to detect DMR even if the CC is incorrect.

	if (monitorModeData.isEnabled)
	{
#if defined(PLATFORM_GD77S)
		if ((BUTTONCHECK_DOWN(ev, BUTTON_SK2) == 0) || (BUTTONCHECK_DOWN(ev, BUTTON_SK1) == 0) || BUTTONCHECK_DOWN(ev, BUTTON_ORANGE) || (ev->events & ROTARY_EVENT))
#else
		if ((BUTTONCHECK_UNIQUE_DOWN(ev, BUTTON_SK2) && (ev->keys.key == 0)) == false)
#endif
		{
			monitorModeData.isEnabled = false;

			// Blindly set mode and BW, as maybe only FM BW has changed (MonitorMode ends to FM 25kHz if no DMR is detected)
			// Anyway, trxSetModeAndBandwidth() won't do anything if the current mode and BW are identical,
			trxSetModeAndBandwidth(currentChannelData->chMode, (codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_BW_25K) != 0));

			currentChannelData->sql = monitorModeData.savedSquelch;
			nonVolatileSettings.dmrCcTsFilter = monitorModeData.savedDMRCcTsFilter;
			nonVolatileSettings.dmrDestinationFilter = monitorModeData.savedDMRDestinationFilter;

			switch (monitorModeData.savedRadioMode)
			{
				case RADIO_MODE_ANALOG:
					trxSetRxCSS(RADIO_DEVICE_PRIMARY, currentChannelData->rxTone);
					break;
				case RADIO_MODE_DIGITAL:
					trxSetDMRColourCode(monitorModeData.savedDMRCc);
					trxSetDMRTimeSlot(monitorModeData.savedDMRTs, true);
					HRC6000InitDigitalDmrRx();
					disableAudioAmp(AUDIO_AMP_MODE_RF);
					break;
				default: // RADIO_MODE_NONE
					break;
			}

			headerRowIsDirty = true;

			if (uiVFOModeSweepScanning(true))
			{
				uiVFOSweepScanModePause(false, false);
			}
			else
			{
				uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
				(void)addTimerCallback(uiUtilityRenderQSODataAndUpdateScreen, 2000, menuSystemGetCurrentMenuNumber(), true);
			}

			return true;
		}
	}
	else
	{
#if defined(PLATFORM_GD77S)
		if (BUTTONCHECK_LONGDOWN(ev, BUTTON_SK1) && BUTTONCHECK_LONGDOWN(ev, BUTTON_SK2)
#else
		if ((ev->keys.key == 0) && (ev->buttons == (BUTTON_SK2_EXTRA_LONG_DOWN | BUTTON_SK2))
#endif
				&& (trxGetMode() != RADIO_MODE_NONE)
				&& (((uiDataGlobal.Scan.toneActive == false) &&
						((uiDataGlobal.Scan.active == false) || (uiDataGlobal.Scan.active && (uiDataGlobal.Scan.state == SCAN_STATE_PAUSED))))
						|| uiVFOModeSweepScanning(false))
		)
		{
			if (voicePromptsIsPlaying())
			{
				voicePromptsTerminate();
			}

			if (uiVFOModeSweepScanning(false))
			{
				uiVFOSweepScanModePause(true, true);
			}

			monitorModeData.savedRadioMode = trxGetMode();
			monitorModeData.savedSquelch = currentChannelData->sql;
			monitorModeData.savedDMRCcTsFilter = nonVolatileSettings.dmrCcTsFilter;
			monitorModeData.savedDMRDestinationFilter = nonVolatileSettings.dmrDestinationFilter;
			monitorModeData.savedDMRCc = trxGetDMRColourCode();
			monitorModeData.savedDMRTs = trxGetDMRTimeSlot();

			// Temporary override DMR filtering
			nonVolatileSettings.dmrCcTsFilter = DMR_CCTS_FILTER_NONE;
			nonVolatileSettings.dmrDestinationFilter = DMR_DESTINATION_FILTER_NONE;
			monitorModeData.dmrTimeout = DMR_MODE_CC_DETECT_TIME_MS;

			monitorModeData.dmrIsValid = false;
			monitorModeData.qsoInfoUpdated = true;
			monitorModeData.triggered = true;
			monitorModeData.isEnabled = true;

			// Start with DMR autodetection
			if (monitorModeData.savedRadioMode != RADIO_MODE_DIGITAL)
			{
				trxSetModeAndBandwidth(RADIO_MODE_DIGITAL, false);

				// current Channels|VFO mode has reverseRepeater set, handle this.
				if ((uiDataGlobal.reverseRepeaterChannel && (menuSystemGetCurrentMenuNumber() == UI_CHANNEL_MODE)) ||
						(uiDataGlobal.reverseRepeaterVFO && (menuSystemGetCurrentMenuNumber() == UI_VFO_MODE)))
				{
					uint32_t rxFreq = currentChannelData->txFreq;
					uint32_t txFreq = (uiDataGlobal.talkaround ? rxFreq : currentChannelData->rxFreq);

					trxSetFrequency(rxFreq, txFreq, DMR_MODE_DMO);
				}
			}

			trxSetDMRColourCode(0);
			trxSetDMRTimeSlot(0, true);
			HRC6000ClearActiveDMRID();
			HRC6000ClearColorCodeSynchronisation();
			lastHeardClearLastID();

			HRC6000InitDigitalDmrRx();
			disableAudioAmp(AUDIO_AMP_MODE_RF);

			headerRowIsDirty = true;
			return true;
		}
	}
	return false;
}

// Helper function that manages the returned value from the codeplug quickkey code
static bool setQuickkeyFunctionID(char key, uint16_t functionId, bool silent)
{
	if (
#if defined(PLATFORM_RD5R)
			// '5' is reserved for torch on RD-5R
			(key != '5') &&
#endif
			codeplugSetQuickkeyFunctionID(key, functionId))
	{
		if (silent == false)
		{
			nextKeyBeepMelody = (int16_t *)MELODY_ACK_BEEP;
		}
		return true;
	}

	nextKeyBeepMelody = (int16_t *)MELODY_ERROR_BEEP;

	return false;
}

void saveQuickkeyMenuIndex(char key, uint8_t menuId, uint8_t entryId, uint8_t function)
{
	uint16_t functionID;

	functionID = QUICKKEY_MENUVALUE(menuId, entryId, function);
	if (setQuickkeyFunctionID(key, functionID, false))
	{
		menuDataGlobal.menuOptionsTimeout = -1;// Flag to indicate that a QuickKey has just been set.
	}
}

void saveQuickkeyMenuLongValue(char key, uint8_t menuId, uint16_t entryId)
{
	uint16_t functionID;

	functionID = QUICKKEY_MENULONGVALUE(menuId, entryId);
	setQuickkeyFunctionID(key, functionID, ((menuId == 0) && (entryId == 0)));
}

void saveQuickkeyContactIndex(char key, uint16_t contactId)
{
	setQuickkeyFunctionID(key, QUICKKEY_CONTACTVALUE(contactId), false);
}

// Returns the index in either the CTCSS or DCS list of the tone (or closest match)
uint8_t cssGetToneIndex(uint16_t tone, CodeplugCSSTypes_t type)
{
	uint16_t *start = NULL;
	uint16_t *end = NULL;

	if (type & CSS_TYPE_CTCSS)
	{
		start = (uint16_t *)TRX_CTCSSTones;
		end = start + (TRX_NUM_CTCSS - 1);
	}
	else if (type & CSS_TYPE_DCS)
	{
		tone &= ~CSS_TYPE_DCS_MASK;
		start = (uint16_t *)TRX_DCSCodes;
		end = start + (TRX_NUM_DCS - 1);
	}

	if (start && end)
	{
		uint16_t *p = start;

		while((p <= end) && (*p < tone))
		{
			p++;
		}

		if (p <= end)
		{
			return (p - start);
		}
	}

	return 0U;
}

uint16_t cssGetToneFromIndex(uint8_t index, CodeplugCSSTypes_t type)
{
	if (type & CSS_TYPE_CTCSS)
	{
		if (index >= TRX_NUM_CTCSS)
		{
			index = 0;
		}
		return TRX_CTCSSTones[index];
	}
	else if (type & CSS_TYPE_DCS)
	{
		if (index >= TRX_NUM_DCS)
		{
			index = 0;
		}
		return (TRX_DCSCodes[index] | type);
	}

	return CODEPLUG_CSS_TONE_NONE;
}

void cssIncrement(uint16_t *tone, uint8_t *index, uint8_t step, CodeplugCSSTypes_t *type, bool loop, bool stayInCSSType)
{
	*index += step;

	if (*type & CSS_TYPE_CTCSS)
	{
		if (*index >= TRX_NUM_CTCSS)
		{
			if (stayInCSSType)
			{
				*index = 0;
			}
			else
			{
				*tone = cssGetToneFromIndex((*index = 0), (*type = CSS_TYPE_DCS));
				return;
			}
		}
		*tone = TRX_CTCSSTones[*index];
	}
	else if (*type & CSS_TYPE_DCS)
	{
		if (*index >= TRX_NUM_DCS)
		{
			if (stayInCSSType)
			{
				*index = 0;
			}
			else
			{
				if (*type & CSS_TYPE_DCS_INVERTED)
				{
					if (loop)
					{
						*tone = cssGetToneFromIndex((*index = 0), (*type = CSS_TYPE_CTCSS));
						return;
					}
					// We are at the end of whole list
					*index = TRX_NUM_DCS - 1;
				}
				else
				{
					*tone = cssGetToneFromIndex((*index = 0), (*type = (CSS_TYPE_DCS | CSS_TYPE_DCS_INVERTED)));
					return;
				}
			}
		}
		*tone = TRX_DCSCodes[*index] | *type;
	}
	else if (*type & CSS_TYPE_NONE)
	{
		*tone = cssGetToneFromIndex((*index = 0), (*type = CSS_TYPE_CTCSS));
	}
}

void cssDecrement(uint16_t *tone, uint8_t *index, uint8_t step, CodeplugCSSTypes_t *type, bool loop, bool stayInCSSType)
{
	int16_t idx = *index - step;

	if (*type & CSS_TYPE_CTCSS)
	{
		if (idx < 0)
		{
			if (stayInCSSType)
			{
				idx = TRX_NUM_CTCSS - 1;
			}
			else
			{
				if (loop)
				{
					*tone = cssGetToneFromIndex((*index = (TRX_NUM_DCS - 1)), (*type = (CSS_TYPE_DCS | CSS_TYPE_DCS_INVERTED)));
				}
				else
				{
					*tone = cssGetToneFromIndex((*index = 0), (*type = CSS_TYPE_NONE));
				}
				return;
			}
		}
		*tone = TRX_CTCSSTones[(*index = (uint8_t)idx)];
	}
	else if (*type & CSS_TYPE_DCS)
	{
		if (idx < 0)
		{
			if (stayInCSSType)
			{
				idx = TRX_NUM_DCS - 1;
			}
			else
			{
				if (*type & CSS_TYPE_DCS_INVERTED)
				{
					*tone = cssGetToneFromIndex((*index = (TRX_NUM_DCS - 1)), (*type = CSS_TYPE_DCS));
				}
				else
				{
					*tone = cssGetToneFromIndex((*index = (TRX_NUM_CTCSS - 1)), (*type = CSS_TYPE_CTCSS));
				}
				return;
			}
		}
		*tone = TRX_DCSCodes[(*index = (uint8_t)idx)] | *type;
	}
	else if (*type & CSS_TYPE_NONE)
	{
		if (loop)
		{
			*tone = cssGetToneFromIndex((*index = (TRX_NUM_DCS - 1)), (*type = (CSS_TYPE_DCS | CSS_TYPE_DCS_INVERTED)));
		}
		else
		{
			*tone = cssGetToneFromIndex((*index = 0), (*type = CSS_TYPE_NONE));
		}
	}
}

bool uiQuickKeysShowChoices(char *buf, const int bufferLen, const char *menuTitle)
{
	bool settingOption = (menuDataGlobal.menuOptionsSetQuickkey != 0) || (menuDataGlobal.menuOptionsTimeout > 0);

	if (menuDataGlobal.menuOptionsSetQuickkey != 0)
	{
		snprintf(buf, bufferLen, "%s %c", currentLanguage->set_quickkey, menuDataGlobal.menuOptionsSetQuickkey);
		menuDisplayTitle(buf);
		displayDrawChoice(CHOICES_OKARROWS, true);

		if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
		{
			voicePromptsInit();
			voicePromptsAppendLanguageString(currentLanguage->set_quickkey);
			voicePromptsAppendPrompt(PROMPT_0 + (menuDataGlobal.menuOptionsSetQuickkey - '0'));
		}
	}
	else if (settingOption == false)
	{
		menuDisplayTitle(menuTitle);
	}

	return settingOption;
}

bool uiQuickKeysIsStoring(uiEvent_t *ev)
{
	return ((ev->events & KEY_EVENT) && (menuDataGlobal.menuOptionsSetQuickkey != 0) && (menuDataGlobal.menuOptionsTimeout == 0));
}

void uiQuickKeysStore(uiEvent_t *ev, menuStatus_t *status)
{
	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		menuDataGlobal.menuOptionsSetQuickkey = 0;
		menuDataGlobal.menuOptionsTimeout = 0;
		*status |= MENU_STATUS_ERROR;
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		saveQuickkeyMenuIndex(menuDataGlobal.menuOptionsSetQuickkey, menuSystemGetCurrentMenuNumber(), menuDataGlobal.currentItemIndex, 0);
		menuDataGlobal.menuOptionsSetQuickkey = 0;
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_LEFT))
	{
		saveQuickkeyMenuIndex(menuDataGlobal.menuOptionsSetQuickkey, menuSystemGetCurrentMenuNumber(), menuDataGlobal.currentItemIndex, FUNC_LEFT);
		menuDataGlobal.menuOptionsSetQuickkey = 0;
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RIGHT))
	{
		saveQuickkeyMenuIndex(menuDataGlobal.menuOptionsSetQuickkey, menuSystemGetCurrentMenuNumber(), menuDataGlobal.currentItemIndex, FUNC_RIGHT);
		menuDataGlobal.menuOptionsSetQuickkey = 0;
	}
}

// --- DTMF contact list playback ---

static uint32_t dtmfGetToneDuration(uint32_t duration)
{
	bool starOrHash = ((uiDataGlobal.DTMFContactList.buffer[uiDataGlobal.DTMFContactList.poPtr] == 14) || (uiDataGlobal.DTMFContactList.buffer[uiDataGlobal.DTMFContactList.poPtr] == 15));

	/*
	 * https://www.sigidwiki.com/wiki/Dual_Tone_Multi_Frequency_(DTMF):
	 *    Standard Whelen timing is 40ms tone, 20ms space, where standard Motorola rate is 250ms tone, 250ms space.
	 *    Federal Signal ranges from 35ms tone 5ms space to 1000ms tone 1000ms space.
	 *    Genave Superfast rate is 20ms tone 20ms space. Genave claims their decoders can even respond to 20ms tone 5ms space.
	 *
	 *
	 * ETSI: https://www.etsi.org/deliver/etsi_es/201200_201299/20123502/01.01.01_60/es_20123502v010101p.pdf
	 * 4.2.4 Signal timing
	 *    4.2.4.1 Tone duration
	 *      Where the DTMF signalling tone duration is controlled automatically by the transmitter, the duration of any individual
	 *      DTMF tone combination sent shall not be less than 65 ms. The time shall be measured from the time when the tone
	 *      reaches 90 % of its steady-state value, until it has dropped to 90 % of its steady-state value.
	 *
	 *         NOTE: For correct operation of supplementary services such as SCWID (Spontaneous Call Waiting
	 *               Identification) and ADSI (Analogue Display Services Interface), DTMF tone bursts should not be longer
	 *               than 90 ms.
	 *
	 *    4.2.4.2 Pause duration
	 *       Where the DTMF signalling pause duration is controlled automatically by the transmitter the duration of the pause
	 *       between any individual DTMF tone combination shall not be less than 65 ms. The time shall be measured from the time
	 *       when the tone has dropped to 10 % of its steady-state value, until it has risen to 10 % of its steady-state value.
	 *
	 *         NOTE: In order to ensure correct reception of all the digits in a network address sequence, some networks may
	 *               require a sufficient pause after the last DTMF digit signalled and before normal transmission starts.
	 */

	// First digit
	if ((uiDataGlobal.DTMFContactList.poPtr == 0) && (uiDataGlobal.DTMFContactList.durations.fstDur > 0))
	{
		/*
		 * First digit duration:
		 *    - Example 1： "DTMF rate" is set to 10 digits per second (duration is 50 milliseconds).
		 *        The first digit time is set to 100 milliseconds. Thus, the actual length of the first digit duration is 150 milliseconds.
		 *        However, if the launch starts with a "*" or "#" tone, the intercom will compare the duration with "* and #" and whichever
		 *        is longer for both.
		 *    - Example 2： "DTMF rate" is set to 10 digits per second (duration is 50 milliseconds).
		 *        The first digit time is set to 100 milliseconds. "* And # tone" is set to 500 milliseconds.
		 *        Thus, the actual length of the first "*" or "#" tone is 550 milliseconds.
		 */
		return ((starOrHash ? (uiDataGlobal.DTMFContactList.durations.otherDur * 10) : (uiDataGlobal.DTMFContactList.durations.fstDur * 10)) + duration);
	}

	/*
	 * '*' '#' Duration:
	 *    - Example 1： "DTMF rate" is set to 10 digits per second (duration is 50 milliseconds).
	 *        "* And # tone" is set to 500 milliseconds. Thus, the actual length of "* and # sounds" is 550 milliseconds.
	 *        However, if the launch starts with * and # sounds, the intercom compares the duration of the pitch with
	 *        the "first digit time" and uses the longer one of the two.
	 *    - Example 2： "DTMF rate" is set to 10 digits per second (duration is 50 milliseconds).
	 *        The first digit time is set to 100 milliseconds. "* And # tone" is set to 500 milliseconds.
	 *        Therefore, the actual number of the first digit * or # is 550 milliseconds.
	 */
	return ((starOrHash ? (uiDataGlobal.DTMFContactList.durations.otherDur * 10) : 0) + duration);
}

static void dtmfStopLocalTone(bool enableMic)
{
#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_DM1801) || defined(PLATFORM_DM1801A) || defined(PLATFORM_RD5R)
	trxSelectVoiceChannel(AT1846_VOICE_CHANNEL_NONE);
#else
	trxDTMFoff(enableMic);
	if(soundMelodyIsPlaying())
	{
		soundStopMelody();
	}
#endif
}

static void dtmfProcess(void)
{
	if (uiDataGlobal.DTMFContactList.poLen == 0U)
	{
		return;
	}

	if (ticksTimerHasExpired(&uiDataGlobal.DTMFContactList.nextPeriodTimer))
	{
		uint32_t duration = (1000 / (uiDataGlobal.DTMFContactList.durations.rate * 2));

		if (uiDataGlobal.DTMFContactList.buffer[uiDataGlobal.DTMFContactList.poPtr] != 0xFFU)
		{
			// Set voice channel (and tone), accordingly to the next inTone state
			if (uiDataGlobal.DTMFContactList.inTone == false)
			{
				trxSetDTMF(uiDataGlobal.DTMFContactList.buffer[uiDataGlobal.DTMFContactList.poPtr]);
#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_DM1801) || defined(PLATFORM_DM1801A) || defined(PLATFORM_RD5R)
				trxSelectVoiceChannel(AT1846_VOICE_CHANNEL_DTMF);
#else
				soundSetMelody(MELODY_DTMF);
#endif
			}
			else
			{
				dtmfStopLocalTone(false);
			}

			uiDataGlobal.DTMFContactList.inTone = !uiDataGlobal.DTMFContactList.inTone;
		}
		else
		{
			// Pause after last digit
			if (uiDataGlobal.DTMFContactList.inTone)
			{
				uiDataGlobal.DTMFContactList.inTone = false;
				dtmfStopLocalTone(true);
				ticksTimerStart(&uiDataGlobal.DTMFContactList.nextPeriodTimer, (duration + (uiDataGlobal.DTMFContactList.durations.libreDMR_Tail * 100)));
				return;
			}
		}

		if (uiDataGlobal.DTMFContactList.inTone)
		{
			// Move forward in the sequence, set tone duration
			ticksTimerStart(&uiDataGlobal.DTMFContactList.nextPeriodTimer, dtmfGetToneDuration(duration));
			uiDataGlobal.DTMFContactList.poPtr++;
		}
		else
		{
			// No next character, last iteration pause has already been processed.
			// Move the pointer (offset) beyond the end of the sequence (handled in the next statement)
			if (uiDataGlobal.DTMFContactList.buffer[uiDataGlobal.DTMFContactList.poPtr] == 0xFFU)
			{
				uiDataGlobal.DTMFContactList.poPtr++;
			}
			else
			{
				// Set pause time in-between tone duration
				ticksTimerStart(&uiDataGlobal.DTMFContactList.nextPeriodTimer, duration);
			}
		}

		if (uiDataGlobal.DTMFContactList.poPtr > uiDataGlobal.DTMFContactList.poLen)
		{
			uiDataGlobal.DTMFContactList.poPtr = 0U;
			uiDataGlobal.DTMFContactList.poLen = 0U;
		}
	}
}

void dtmfSequenceReset(void)
{
	uiDataGlobal.DTMFContactList.poLen = 0U;
	uiDataGlobal.DTMFContactList.poPtr = 0U;
	uiDataGlobal.DTMFContactList.isKeying = false;
}

bool dtmfSequenceIsKeying(void)
{
	return uiDataGlobal.DTMFContactList.isKeying;
}

void dtmfSequencePrepare(uint8_t *seq, bool autoStart)
{
	uint8_t len = 16U;

	dtmfSequenceReset();

	memcpy(uiDataGlobal.DTMFContactList.buffer, seq, 16);
	uiDataGlobal.DTMFContactList.buffer[16] = 0xFFU;

	// non empty
	if (uiDataGlobal.DTMFContactList.buffer[0] != 0xFFU)
	{
		// Find the sequence length
		for (uint8_t i = 0; i < 16; i++)
		{
			if (uiDataGlobal.DTMFContactList.buffer[i] == 0xFFU)
			{
				len = i;
				break;
			}
		}

		uiDataGlobal.DTMFContactList.poLen = len;
		uiDataGlobal.DTMFContactList.isKeying = (autoStart ? (len > 0) : false);
	}
}

void dtmfSequenceStart(void)
{
	if (uiDataGlobal.DTMFContactList.isKeying == false)
	{
		uiDataGlobal.DTMFContactList.isKeying = (uiDataGlobal.DTMFContactList.poLen > 0);
	}
}

void dtmfSequenceStop(void)
{
	uiDataGlobal.DTMFContactList.poLen = 0U;
	dtmfStopLocalTone(true);
}

void dtmfSequenceTick(bool popPreviousMenuOnEnding)
{
	if (uiDataGlobal.DTMFContactList.isKeying)
	{
		if (!trxTransmissionEnabled)
		{
			if (xmitErrorTimer > 0)
			{
				// Wait the voice ends, then count-down 200ms;
				if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
				{
					if (voicePromptsIsPlaying())
					{
						xmitErrorTimer = (20 * 10U);
						return;
					}
				}

				xmitErrorTimer--;

				if (xmitErrorTimer == 0)
				{
					dtmfSequenceReset();
					menuSystemPopAllAndDisplayRootMenu();
				}

				return;
			}

			rxPowerSavingSetState(ECOPHASE_POWERSAVE_INACTIVE);

			if ((codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_RX_ONLY) == 0) && ((nonVolatileSettings.txFreqLimited == BAND_LIMITS_NONE) || trxCheckFrequencyInAmateurBand(currentChannelData->txFreq)))
			{

				// Start TX DTMF, prepare for ANALOG
				if (trxGetMode() != RADIO_MODE_ANALOG)
				{
					trxSetModeAndBandwidth(RADIO_MODE_ANALOG, (codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_BW_25K) != 0));
				}

				// Make sure Tx freq is updated before transmission is enabled
				// Maybe the satellite menu was entered, but we can't use the GetPreviousMenu()
				// as this one is a sub-sub menu.
				trxSetFrequency(currentChannelData->rxFreq, currentChannelData->txFreq, DMR_MODE_AUTO);
				//
				trxSetTxCSS(currentChannelData->txTone);

				trxEnableTransmission();

#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_DM1801) || defined(PLATFORM_DM1801A) || defined(PLATFORM_RD5R)
				trxSelectVoiceChannel(AT1846_VOICE_CHANNEL_NONE);
				enableAudioAmp(AUDIO_AMP_MODE_RF);
				GPIO_PinWrite(GPIO_RX_audio_mux, Pin_RX_audio_mux, 1);
#endif

				uiDataGlobal.DTMFContactList.inTone = false;
				ticksTimerStart(&uiDataGlobal.DTMFContactList.nextPeriodTimer, (uiDataGlobal.DTMFContactList.durations.fstDigitDly * 100)); // Sequence preamble
			}
			else
			{
				uiEvent_t ev = { .buttons = 0, .keys = NO_KEYCODE, .rotary = 0, .function = 0, .events = NO_EVENT, .hasEvent = false, .time = 0 };

				menuTxScreenHandleTxTermination(&ev, ((codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_RX_ONLY) != 0) ? TXSTOP_RX_ONLY : TXSTOP_OUT_OF_BAND));
			}
		}

		// DTMF has been TXed, restore DIGITAL/ANALOG
		if (uiDataGlobal.DTMFContactList.poLen == 0U)
		{
			trxDisableTransmission();

			if (trxTransmissionEnabled)
			{
				// Stop TXing;
				trxTransmissionEnabled = false;
				trxSetRX();
				LedWrite(LED_GREEN, 0);

#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S) || defined(PLATFORM_DM1801) || defined(PLATFORM_DM1801A) || defined(PLATFORM_RD5R)
				trxSelectVoiceChannel(AT1846_VOICE_CHANNEL_MIC);
				disableAudioAmp(AUDIO_AMP_MODE_RF);
#endif

				if (currentChannelData->chMode == RADIO_MODE_ANALOG)
				{
					trxSetModeAndBandwidth(currentChannelData->chMode, (codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_BW_25K) != 0));
					trxSetTxCSS(currentChannelData->txTone);
				}
				else
				{
					trxSetModeAndBandwidth(currentChannelData->chMode, false);// bandwidth false = 12.5Khz as DMR uses 12.5kHz
					trxSetDMRColourCode(currentChannelData->txColor);
				}
			}

			uiDataGlobal.DTMFContactList.isKeying = false;

			if (popPreviousMenuOnEnding)
			{
				menuSystemPopPreviousMenu();
			}

			return;
		}

		if (uiDataGlobal.DTMFContactList.poLen > 0U)
		{
			dtmfProcess();
		}
	}
}

void resetOriginalSettingsData(void)
{
	static const uint32_t unsetValue = 0xDEADBEEF;

	originalNonVolatileSettings.magicNumber = unsetValue;
#if !defined(PLATFORM_GD77S)
	memcpy(((uint32_t *)&aprsSettingsCopy.smart.slowRate), &unsetValue, sizeof(uint32_t));
#endif
}

void showErrorMessage(const char *message)
{
	displayClearBuf();
	displayThemeApply(THEME_ITEM_FG_ERROR_NOTIFICATION, THEME_ITEM_BG);
	displayPrintCentered(((DISPLAY_SIZE_Y - FONT_SIZE_3_HEIGHT) >> 1), message, FONT_SIZE_3);
	displayThemeResetToDefault();
	displayRender();
}

const char *getPowerLevel(uint8_t level)
{
#if defined(PLATFORM_MDUV380) && !defined(PLATFORM_VARIANT_UV380_PLUS_10W)
	return POWER_LEVELS[(settingsIsOptionBitSet(BIT_FORCE_10W_RADIO) ? 1 : 0)][level];
#else
	return POWER_LEVELS[level];
#endif
}

const char *getPowerLevelUnit(uint8_t level)
{
	return POWER_LEVEL_UNITS[level];
}

void uiSetUTCDateTimeInSecs(time_t_custom UTCdateTimeInSecs)
{
	uiDataGlobal.dateTimeSecs = UTCdateTimeInSecs;
#if ! defined(PLATFORM_GD77S)
	daytimeThemeChangeUpdate(true);
#endif
}

#ifdef USE_RTC
// Sun = 0.
uint8_t getDayOfTheWeek(uint8_t day, uint8_t month, uint8_t year)
{
	return ((day += (month < 3 ? year-- : (year - 2))), (23 * month / 9 + day + 4 + year / 4 - year / 100 + year / 400)) % 7;
}

time_t getEpochTime(RTC_TimeTypeDef *rtcTime, RTC_DateTypeDef *rtcDate)
{
	uint8_t   hh = rtcTime->Hours;
	uint8_t   mm = rtcTime->Minutes;
	uint8_t   ss = rtcTime->Seconds;
	uint8_t   d = rtcDate->Date;
	uint8_t   m = rtcDate->Month;
	uint16_t  y = rtcDate->Year;
	uint16_t  yr = (uint16_t)(y + (2000 - 1900));
	struct tm tim = { 0 };

	tim.tm_year = yr;
	tim.tm_mon = m - 1;
	tim.tm_mday = d;
	tim.tm_hour = hh;
	tim.tm_min = mm;
	tim.tm_sec = ss;

	return mktime(&tim);
}
#endif


/*
 * gmtime_r.c
 * Original Author: Adapted from tzcode maintained by Arthur David Olson.
 * Modifications:
 * - Changed to mktm_r and added __tzcalc_limits - 04/10/02, Jeff Johnston
 * - Fixed bug in mday computations - 08/12/04, Alex Mogilnikov <alx@intellectronika.ru>
 * - Fixed bug in __tzcalc_limits - 08/12/04, Alex Mogilnikov <alx@intellectronika.ru>
 * - Move code from _mktm_r() to gmtime_r() - 05/09/14, Freddie Chopin <freddie_chopin@op.pl>
 * - Fixed bug in calculations for dates after year 2069 or before year 1901. Ideas for
 *   solution taken from musl's __secs_to_tm() - 07/12/2014, Freddie Chopin
 *   <freddie_chopin@op.pl>
 * - Use faster algorithm from civil_from_days() by Howard Hinnant - 12/06/2014,
 * Freddie Chopin <freddie_chopin@op.pl>
 *
 * Converts the calendar time pointed to by tim_p into a broken-down time
 * expressed as local time. Returns a pointer to a structure containing the
 * broken-down time.
 */


/* Move epoch from 01.01.1970 to 01.03.0000 (yes, Year 0) - this is the first
 * day of a 400-year long "era", right after additional day of leap year.
 * This adjustment is required only for date calculation, so instead of
 * modifying time_t value (which would require 64-bit operations to work
 * correctly) it's enough to adjust the calculated number of days since epoch.
 */
#define EPOCH_ADJUSTMENT_DAYS	719468L
/* year to which the adjustment was made */
#define ADJUSTED_EPOCH_YEAR	0
/* 1st March of year 0 is Wednesday */
#define ADJUSTED_EPOCH_WDAY	3
/* there are 97 leap years in 400-year periods. ((400 - 97) * 365 + 97 * 366) */
#define DAYS_PER_ERA		146097L
/* there are 24 leap years in 100-year periods. ((100 - 24) * 365 + 24 * 366) */
#define DAYS_PER_CENTURY	36524L
/* there is one leap year every 4 years */
#define DAYS_PER_4_YEARS	(3 * 365 + 366)
/* number of days in a non-leap year */
#define DAYS_PER_YEAR		365
/* number of days in January */
#define DAYS_IN_JANUARY		31
/* number of days in non-leap February */
#define DAYS_IN_FEBRUARY	28
/* number of years per era */
#define YEARS_PER_ERA		400

#define SECSPERMIN	60L
#define MINSPERHOUR	60L
#define HOURSPERDAY	24L
#define SECSPERHOUR	(SECSPERMIN * MINSPERHOUR)
#define SECSPERDAY	(SECSPERHOUR * HOURSPERDAY)
#define DAYSPERWEEK	7
#define MONSPERYEAR	12
#define YEAR_BASE	1900
#define EPOCH_YEAR      1970
#define EPOCH_WDAY      4
#define EPOCH_YEARS_SINCE_LEAP 2
#define EPOCH_YEARS_SINCE_CENTURY 70
#define EPOCH_YEARS_SINCE_LEAP_CENTURY 370

#define isleap(y) ((((y) % 4) == 0 && ((y) % 100) != 0) || ((y) % 400) == 0)

struct tm *gmtime_r_Custom(const time_t_custom *__restrict tim_p, struct tm *__restrict res)
{
	long days, rem;
	const time_t_custom lcltime = *tim_p;
	int era, weekday, year;
	unsigned erayear, yearday, month, day;
	unsigned long eraday;

	days = lcltime / SECSPERDAY + EPOCH_ADJUSTMENT_DAYS;
	rem = lcltime % SECSPERDAY;
	if (rem < 0)
	{
		rem += SECSPERDAY;
		--days;
	}

	/* compute hour, min, and sec */
	res->tm_hour = (int) (rem / SECSPERHOUR);
	rem %= SECSPERHOUR;
	res->tm_min = (int) (rem / SECSPERMIN);
	res->tm_sec = (int) (rem % SECSPERMIN);

	/* compute day of week */
	if ((weekday = ((ADJUSTED_EPOCH_WDAY + days) % DAYSPERWEEK)) < 0)
		weekday += DAYSPERWEEK;
	res->tm_wday = weekday;

	/* compute year, month, day & day of year */
	/* for description of this algorithm see
	 * http://howardhinnant.github.io/date_algorithms.html#civil_from_days */
	era = (days >= 0 ? days : days - (DAYS_PER_ERA - 1)) / DAYS_PER_ERA;
	eraday = days - era * DAYS_PER_ERA;	/* [0, 146096] */
	erayear = (eraday - eraday / (DAYS_PER_4_YEARS - 1) + eraday / DAYS_PER_CENTURY -
			eraday / (DAYS_PER_ERA - 1)) / 365;	/* [0, 399] */
	yearday = eraday - (DAYS_PER_YEAR * erayear + erayear / 4 - erayear / 100);	/* [0, 365] */
	month = (5 * yearday + 2) / 153;	/* [0, 11] */
	day = yearday - (153 * month + 2) / 5 + 1;	/* [1, 31] */
	month += month < 10 ? 2 : -10;
	year = ADJUSTED_EPOCH_YEAR + erayear + era * YEARS_PER_ERA + (month <= 1);

	res->tm_yday = yearday >= DAYS_PER_YEAR - DAYS_IN_JANUARY - DAYS_IN_FEBRUARY ?
			yearday - (DAYS_PER_YEAR - DAYS_IN_JANUARY - DAYS_IN_FEBRUARY) :
			yearday + DAYS_IN_JANUARY + DAYS_IN_FEBRUARY + isleap(erayear);
	res->tm_year = year - YEAR_BASE;
	res->tm_mon = month;
	res->tm_mday = day;

	res->tm_isdst = 0;

	return (res);
}

time_t_custom mktime_custom(const struct tm * tb)
{
	const int totalDays[] = { -1, 30, 58, 89, 119, 150, 180, 211, 242, 272, 303, 333, 364 };
	time_t_custom t1;
	int days;

    days = totalDays[tb->tm_mon];
    if (!(tb->tm_year & 3) && (tb->tm_mon > 1))
	{
        days++;
	}

    t1 = (tb->tm_year - (EPOCH_YEAR - 1900)) * DAYS_PER_YEAR + ((tb->tm_year - 1L) / 4) - 17 + days + tb->tm_mday;
    t1 = (t1 * HOURSPERDAY) + tb->tm_hour;
	t1 = (t1 * MINSPERHOUR) + tb->tm_min;
	t1 = (t1 * SECSPERMIN) + tb->tm_sec;

    return (time_t_custom) t1;
}

#if defined(PLATFORM_MD9600) || defined(PLATFORM_MDUV380) || defined(PLATFORM_MD380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
time_t_custom getRtcTime_custom(void)
{
	RTC_TimeTypeDef rtcTime = { 0 };
	RTC_DateTypeDef rtcDate = { 0 };
	struct tm RTCDateTime;

	HAL_RTC_GetTime(&hrtc, &rtcTime, RTC_FORMAT_BIN);
	HAL_RTC_GetDate(&hrtc, &rtcDate, RTC_FORMAT_BIN);


	memset(&RTCDateTime,0x00,sizeof(struct tm)); // clear entire struct
	RTCDateTime.tm_mday = rtcDate.Date;          /* day of the month, 1 to 31 */
	RTCDateTime.tm_mon = rtcDate.Month - 1;      /* months since January, 0 to 11 */
	RTCDateTime.tm_year = rtcDate.Year;          /* years since 1900 */
	RTCDateTime.tm_hour = rtcTime.Hours;
	RTCDateTime.tm_min = rtcTime.Minutes;
	RTCDateTime.tm_sec = rtcTime.Seconds;

	return mktime_custom(&RTCDateTime);
}

void setRtc_custom(time_t_custom tc)
{
	RTC_TimeTypeDef rtcTime = { 0 };
	RTC_DateTypeDef rtcDate = { 0 };
	struct tm RTCDateTime;

	gmtime_r_Custom(&tc, &RTCDateTime);

	rtcDate.Date = RTCDateTime.tm_mday;        /* day of the month, 1 to 31 */
	rtcDate.Month = RTCDateTime.tm_mon + 1 ;   /* months since January, 0 to 11 */
	rtcDate.Year = RTCDateTime.tm_year ;       /* years since 1900 */
	rtcTime.Hours = RTCDateTime.tm_hour;
	rtcTime.Minutes = RTCDateTime.tm_min;
	rtcTime.Seconds = RTCDateTime.tm_sec ;
	rtcTime.TimeFormat = RTC_HOURFORMAT12_PM;
	rtcTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
	rtcTime.StoreOperation = RTC_STOREOPERATION_RESET;

	HAL_RTC_SetDate(&hrtc, &rtcDate, RTC_FORMAT_BIN);
	HAL_RTC_SetTime(&hrtc, &rtcTime, RTC_FORMAT_BIN);
}
#endif


#if ! defined(PLATFORM_GD77S)
ticksTimer_t daytimeThemeTimer;
/*

SUNRISET.C - computes Sun rise/set times, start/end of twilight, and
             the length of the day at any date and latitude

Written as DAYLEN.C, 1989-08-16

Modified to SUNRISET.C, 1992-12-01

(c) Paul Schlyter, 1989, 1992

Released to the public domain by Paul Schlyter, December 1992

*/
/* +++Date last modified: 05-Jul-1997 */

/******************************************************************/
/* This function reduces any angle to within the first revolution */
/* by subtracting or adding even multiples of 360.0 until the     */
/* result is >= 0.0 and < 360.0                                   */
/******************************************************************/
#define INV360    (1.0 / 360.0)

/*****************************************/
/* Reduce angle to within 0..360 degrees */
/*****************************************/
static double revolution(double x)
{
	return(x - 360.0 * floor(x * INV360));
}  /* revolution */

/*********************************************/
/* Reduce angle to within -180..+180 degrees */
/*********************************************/
static double rev180(double x)
{
	return(x - 360.0 * floor(x * INV360 + 0.5));
}  /* revolution */

/*******************************************************************/
/* This function computes GMST0, the Greenwhich Mean Sidereal Time */
/* at 0h UT (i.e. the sidereal time at the Greenwhich meridian at  */
/* 0h UT).  GMST is then the sidereal time at Greenwich at any     */
/* time of the day.  I've generelized GMST0 as well, and define it */
/* as:  GMST0 = GMST - UT  --  this allows GMST0 to be computed at */
/* other times than 0h UT as well.  While this sounds somewhat     */
/* contradictory, it is very practical:  instead of computing      */
/* GMST like:                                                      */
/*                                                                 */
/*  GMST = (GMST0) + UT * (366.2422/365.2422)                      */
/*                                                                 */
/* where (GMST0) is the GMST last time UT was 0 hours, one simply  */
/* computes:                                                       */
/*                                                                 */
/*  GMST = GMST0 + UT                                              */
/*                                                                 */
/* where GMST0 is the GMST "at 0h UT" but at the current moment!   */
/* Defined in this way, GMST0 will increase with about 4 min a     */
/* day.  It also happens that GMST0 (in degrees, 1 hr = 15 degr)   */
/* is equal to the Sun's mean longitude plus/minus 180 degrees!    */
/* (if we neglect aberration, which amounts to 20 seconds of arc   */
/* or 1.33 seconds of time)                                        */
/*                                                                 */
/*******************************************************************/
static double greenwhich_mean_sideral_time_at_0_UT(double d)
{
    double sidtim0;

    /* Sidtime at 0h UT = L (Sun's mean longitude) + 180.0 degr  */
    /* L = M + w, as defined in sunpos().  Since I'm too lazy to */
    /* add these numbers, I'll let the C compiler do it for me.  */
    /* Any decent C compiler will add the constants at compile   */
    /* time, imposing no runtime or code overhead.               */
    sidtim0 = revolution((180.0 + 356.0470 + 282.9404) + (0.9856002585 + 4.70935E-5) * d);
    return sidtim0;
}

/******************************************************/
/* Computes the Sun's ecliptic longitude and distance */
/* at an instant given in d, number of days since     */
/* 2000 Jan 0.0.  The Sun's ecliptic latitude is not  */
/* computed, since it's always very near 0.           */
/******************************************************/
static void sunpos(double d, double *lon, double *r)
{
    double M,         /* Mean anomaly of the Sun */
           w,         /* Mean longitude of perihelion */
           /* Note: Sun's mean longitude = M + w */
           e,         /* Eccentricity of Earth's orbit */
           E,         /* Eccentric anomaly */
           x, y,      /* x, y coordinates in orbit */
           v;         /* True anomaly */

    /* Compute mean elements */
    M = revolution(356.0470 + 0.9856002585 * d);
    w = 282.9404 + 4.70935E-5 * d;
    e = 0.016709 - 1.151E-9 * d;

    /* Compute true longitude and radius vector */
    E = M + e * RADEG * SIND(M) * (1.0 + e * COSD(M));
    x = COSD(E) - e;
    y = sqrt(1.0 - e*e) * SIND(E);
    *r = sqrt(x*x + y*y);              /* Solar distance */
    v = ATAN2D(y, x);                  /* True anomaly */
    *lon = v + w;                        /* True solar longitude */
    if (*lon >= 360.0)
    {
        *lon -= 360.0;                   /* Make it 0..360 degrees */
    }
}

/******************************************************/
/* Computes the Sun's equatorial coordinates RA, Decl */
/* and also its distance, at an instant given in d,   */
/* the number of days since 2000 Jan 0.0.             */
/******************************************************/
static void sun_RA_dec(double d, double *RA, double *dec, double *r)
{
	double lon, obl_ecl, x, y, z;

	/* Compute Sun's ecliptical coordinates */
	sunpos(d, &lon, r);

	/* Compute ecliptic rectangular coordinates (z=0) */
	x = *r * COSD(lon);
	y = *r * SIND(lon);

	/* Compute obliquity of ecliptic (inclination of Earth's axis) */
	obl_ecl = 23.4393 - 3.563E-7 * d;

	/* Convert to equatorial rectangular coordinates - x is unchanged */
	z = y * SIND(obl_ecl);
	y = y * COSD(obl_ecl);

	/* Convert to spherical coordinates */
	*RA = ATAN2D(y, x);
	*dec = ATAN2D(z, sqrt(x*x + y*y));
}

/***************************************************************************/
/* Note: year,month,date = calendar date, 1801-2099 only.             */
/*       Eastern longitude positive, Western longitude negative       */
/*       Northern latitude positive, Southern latitude negative       */
/*       The longitude value IS critical in this function!            */
/*       altit = the altitude which the Sun should cross              */
/*               Set to -35/60 degrees for rise/set, -6 degrees       */
/*               for civil, -12 degrees for nautical and -18          */
/*               degrees for astronomical twilight.                   */
/*         upper_limb: non-zero -> upper limb, zero -> center         */
/*               Set to non-zero (e.g. 1) when computing rise/set     */
/*               times, and to zero when computing start/end of       */
/*               twilight.                                            */
/*        *rise = where to store the rise time                        */
/*        *set  = where to store the set  time                        */
/*                Both times are relative to the specified altitude,  */
/*                and thus this function can be used to comupte       */
/*                various twilight times, as well as rise/set times   */
/* Return value:  0 = sun rises/sets this day, times stored at        */
/*                    *trise and *tset.                               */
/*               +1 = sun above the specified "horizon" 24 hours.     */
/*                    *trise set to time when the sun is at south,    */
/*                    minus 12 hours while *tset is set to the south  */
/*                    time plus 12 hours. "Day" length = 24 hours     */
/*               -1 = sun is below the specified "horizon" 24 hours   */
/*                    "Day" length = 0 hours, *trise and *tset are    */
/*                    both set to the time when the sun is at south.  */
/*                                                                    */
/**********************************************************************/
int sunriset(int year, int month, int day, double lon, double lat, double altit, int upper_limb, double *trise, double *tset)
{
	double  d,  /* Days since 2000 Jan 0.0 (negative before) */
	sr,         /* Solar distance, astronomical units */
	sRA,        /* Sun's Right Ascension */
	sdec,       /* Sun's declination */
	sradius,    /* Sun's apparent radius */
	t,          /* Diurnal arc */
	tsouth,     /* Time when Sun is at south */
	sidtime;    /* Local sidereal time */

	int rc = 0; /* Return cde from function - usually 0 */

	/* Compute d of 12h local mean solar time */
	d = DAYS_SINCE_2000_JAN_0(year, month, day) + 0.5 - lon / 360.0;

	/* Compute local sideral time of this moment */
	//Greenwhich Mean Sidereal Time at 0h UT
	sidtime = revolution(greenwhich_mean_sideral_time_at_0_UT(d) + 180.0 + lon);

	/* Compute Sun's RA + Decl at this moment */
	sun_RA_dec(d, &sRA, &sdec, &sr);

	/* Compute time when Sun is at south - in hours UT */
	tsouth = 12.0 - rev180(sidtime - sRA)/15.0;

	/* Compute the Sun's apparent radius, degrees */
	sradius = 0.2666 / sr;

	/* Do correction to upper limb, if necessary */
	if (upper_limb)
	{
		altit -= sradius;
	}

	/* Compute the diurnal arc that the Sun traverses to reach */
	/* the specified altitide altit: */
	{
		double cost;
		cost = (SIND(altit) - SIND(lat) * SIND(sdec)) / (COSD(lat) * COSD(sdec));
		if (cost >= 1.0)
		{
			rc = -1; t = 0.0;       /* Sun always below altit */
		}
		else if (cost <= -1.0)
		{
			rc = +1; t = 12.0;      /* Sun always above altit */
		}
		else
		{
			t = ACOSD(cost)/15.0;   /* The diurnal arc, hours */
		}
	}

	/* Store rise and set times - in hours UT */
	*trise = tsouth - t;
	*tset  = tsouth + t;

	return rc;
}  /* __sunriset__ */

void daytimeThemeChangeUpdate(bool startup)
{
	if ((nonVolatileSettings.locationLat != SETTINGS_UNITIALISED_LOCATION_LAT) && settingsIsOptionBitSet(BIT_AUTO_NIGHT))
	{
		ticksTimerStart(&daytimeThemeTimer, (startup ? 2000 : DAYTIME_THEME_TIMER_INTERVAL));
		return;
	}

	ticksTimerReset(&daytimeThemeTimer);
}

void daytimeThemeApply(DayTime_t daytime)
{
	uiEvent_t e = { .buttons = 0, .keys = NO_KEYCODE, .rotary = 0, .function = FUNC_REDRAW, .events = FUNCTION_EVENT, .hasEvent = true, .time = ticksGetMillis() };

	if (uiDataGlobal.daytimeOverridden == UNDEFINED)
	{
		uiDataGlobal.daytime = daytime;
	}

#if defined(HAS_COLOURS)
	displayThemeResetToDefault(); // Apply new theme.
#else
	// Need to perform a full reset on the display to change back to non-inverted
	displayInit(((daytime == NIGHT) ^ settingsIsOptionBitSet(BIT_INVERSE_VIDEO)));
#endif
	uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 1000, ((daytime == DAY) ? currentLanguage->daytime_theme_day : currentLanguage->daytime_theme_night), true);
	menuSystemCallCurrentMenuTick(&e); // redraw the current screen.

	displayLightTrigger(true);

	// Don't interrupt VP or melody for this.
	if ((voicePromptsIsPlaying() == false) && (soundMelodyIsPlaying() == false))
	{
		voicePromptsInit();
		voicePromptsAppendLanguageString(((daytime == DAY) ? currentLanguage->daytime_theme_day : currentLanguage->daytime_theme_night));
		voicePromptsPlay();
	}
}

void daytimeThemeTick(void)
{
	struct tm gmt;
	time_t_custom now = uiDataGlobal.dateTimeSecs;

	if (gmtime_r_Custom(&now, &gmt))
	{
		double trise, tset; // offsets (+/-, in hours) from midnight UTC
		bool afterSunrise = false;
		bool afterSunset = false;

		if (sunriset((gmt.tm_year + 1900), (gmt.tm_mon + 1), gmt.tm_mday,
				latLongFixed32ToDouble(nonVolatileSettings.locationLon), latLongFixed32ToDouble(nonVolatileSettings.locationLat), (-35.0 / 60.0), 1, &trise, &tset) == 0)
		{
			struct tm gmtOfMidnightToday = { .tm_year = gmt.tm_year, .tm_mon = gmt.tm_mon, .tm_mday = gmt.tm_mday, .tm_hour = 0, .tm_min = 0, .tm_sec = 0 };
			time_t_custom timeMidnightToday = mktime_custom(&gmtOfMidnightToday);
			time_t_custom timeRise = timeMidnightToday + (3600.0 * trise);
			time_t_custom timeSet = timeMidnightToday + (3600.0 * tset);

			// We past the current computed sunset time, then add 24 hours
			// to get the next day Sunrise and Sunset times (approx).
			if (now > timeSet)
			{
				timeRise += 86400;
				timeSet += 86400;
			}

			if ((now >= timeRise) || (now < timeSet))
			{
				afterSunrise = true;
			}

			if ((now >= timeSet) || (now < timeRise))
			{
				afterSunset = true;
			}

			if (afterSunrise && (afterSunset == false) && (uiDataGlobal.daytime == NIGHT) && (uiDataGlobal.daytimeOverridden == UNDEFINED))
			{
				daytimeThemeApply(DAY);
			}
			else if (afterSunset && (uiDataGlobal.daytime == DAY) && (uiDataGlobal.daytimeOverridden == UNDEFINED))
			{
				daytimeThemeApply(NIGHT);
			}
		}
	}
}

void uiChannelModeOrVFOModeThemeDaytimeChange(bool toggle, bool isChannelMode)
{
	if (toggle)
	{
		if (uiDataGlobal.daytimeOverridden == UNDEFINED)
		{
			uiDataGlobal.daytimeOverridden = ((uiDataGlobal.daytime == DAY) ? NIGHT : DAY);
		}
		else
		{
			uiDataGlobal.daytimeOverridden = ((uiDataGlobal.daytimeOverridden == DAY) ? NIGHT : DAY);
		}

		if (settingsIsOptionBitSet(BIT_AUTO_NIGHT) == false)
		{
			settingsSetOptionBit(BIT_AUTO_NIGHT_OVERRIDE, true);
			settingsSetOptionBit(BIT_AUTO_NIGHT_DAYTIME, (bool)uiDataGlobal.daytimeOverridden);
		}
	}
	else
	{
		uiDataGlobal.daytimeOverridden = UNDEFINED;

		if (settingsIsOptionBitSet(BIT_AUTO_NIGHT) == false)
		{
			settingsSetOptionBit(BIT_AUTO_NIGHT_OVERRIDE, false);
		}
	}

	daytimeThemeApply(toggle ? uiDataGlobal.daytimeOverridden : uiDataGlobal.daytime);
	uiDataGlobal.displayChannelSettings = false;
	uiDataGlobal.displayQSOStatePrev = QSO_DISPLAY_DEFAULT_SCREEN;
	uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;

	if (isChannelMode)
	{
		uiChannelModeUpdateScreen(0);
	}
	else
	{
		uiVFOModeUpdateScreen(0);
	}
}

#endif

#if defined(STM32F405xx) && ! defined(PLATFORM_MD9600)
uint32_t cpuGetSignature(void)
{
	return (DBGMCU->IDCODE & 0x00000FFF);
}

uint32_t cpuGetRevision(void)
{
	return ((DBGMCU->IDCODE >> 16) & 0x0000FFFF);
}

uint32_t cpuGetPackage(void)
{
	return (((*(__IO uint16_t *) (0x1FFF7BF0)) & 0x0700) >> 8);
}

int32_t cpuGetFlashSize(void)
{
	return (*(__IO uint16_t *) (0x1FFF7A22));
}
#endif

