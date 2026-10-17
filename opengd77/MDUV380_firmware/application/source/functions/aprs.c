/*
 * Copyright (C) 2023-2024 Roger Clark, VK3KYY / G4KYF
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

#if !defined(PLATFORM_GD77S)

#include "main.h"
#include "functions/settings.h"
#include "user_interface/uiLocalisation.h"
#include "user_interface/uiUtilities.h"
#include "interfaces/clockManager.h"
#include "functions/rxPowerSaving.h"

#if defined(CPU_MK22FN512VLL12)
#include "fsl_ftm.h"
#else // CPU_MK22FN512VLL12
#if !defined(PLATFORM_MD9600)
#include "hardware/AT1846S.h"
#endif
#endif // CPU_MK22FN512VLL12
#include "functions/aprs.h"
#include "functions/fiveToneDecode.h"
#include "hardware/HR-C6000.h"
#include "functions/satellite.h"
#if defined(HAS_GPS)
#include "interfaces/gps.h"
#endif
#include "user_interface/menuSystem.h"

#if !defined(TWO_PI)
#define TWO_PI 6.283185307179586476925286766559
#endif

#if !defined(SQUARE)
#define SQUARE(a) ((a) * (a))
#endif


#define AX25_PACKET_BUFFER_SIZE        256U
// Flags sent ahead of the frame, after the APRS_XMIT_TX_DELAY settling time:
// 40 at 1200 baud is about 270ms, so with the delay the frame starts some 320ms
// after keying - about the 300ms most trackers use. 16 (about 160ms in all) was
// too short for a digipeater whose squelch or demodulator is slow to open: it
// missed the start of the frame, and with it the whole packet. The
// longest frame (an e-mail, 129 characters) still fits the buffer with worst-case
// bit stuffing, at about 237 bytes.
#define APRS_PREAMBLE_FLAGS            40U
#define SMART_BEACONING_SPEED_MIN       54U // more than 1km/h (0.5399568034557235 kn == 1km/h)
#define APRS_DESTINATION            "APOG77" // MAX 6 char (excluding terminator)
#define APRS_CONFIG_SATELLITE            0U
#define APRS_CONFIG_CHANNEL              1U
#define APRS_ANTI_COLLISION_MS_MAX    2000U
#define APRS_USE_COURSETO_FOR_BEARING     1


#if defined(CPU_MK22FN512VLL12)
// On MK22, isnan macro has a bug, circumvent it
#undef isnan
#define isnan(x) (__builtin_isnan(x))

// On MK22, round() doesn't exist, __builtin_round() is also not functionnal
// from newlib-cygwin:
//    https://sourceware.org/git/gitweb.cgi?p=newlib-cygwin.git;a=blob;f=newlib/libm/common/s_round.c;hb=master
typedef union
{
		double value;
		struct
		{
				uint32_t lsw;
				uint32_t msw;
		} parts;
} ieee_double_shape_type;

#define EXTRACT_WORDS(ix0,ix1,d)         \
		do {                             \
			ieee_double_shape_type ew_u; \
			ew_u.value = (d);            \
			(ix0) = ew_u.parts.msw;      \
			(ix1) = ew_u.parts.lsw;      \
		} while (0)

#define INSERT_WORDS(d,ix0,ix1)          \
		do {                             \
			ieee_double_shape_type iw_u; \
			iw_u.parts.msw = (ix0);      \
			iw_u.parts.lsw = (ix1);      \
			(d) = iw_u.value;            \
		} while (0)

double round(double x)
{
	/* Most significant word, least significant word. */
	int32_t msw, exponent_less_1023;
	uint32_t lsw;

	EXTRACT_WORDS(msw, lsw, x);

	/* Extract exponent field. */
	exponent_less_1023 = ((msw & 0x7ff00000) >> 20) - 1023;

	if (exponent_less_1023 < 20)
	{
		if (exponent_less_1023 < 0)
		{
			msw &= 0x80000000;
			if (exponent_less_1023 == -1)
				/* Result is +1.0 or -1.0. */
				msw |= ((int32_t)1023 << 20);
			lsw = 0;
		}
		else
		{
			uint32_t exponent_mask = 0x000fffff >> exponent_less_1023;
			if ((msw & exponent_mask) == 0 && lsw == 0)
				/* x in an integral value. */
				return x;

			msw += 0x00080000 >> exponent_less_1023;
			msw &= ~exponent_mask;
			lsw = 0;
		}
	}
	else if (exponent_less_1023 > 51)
	{
		if (exponent_less_1023 == 1024)
			/* x is NaN or infinite. */
			return x + x;
		else
			return x;
	}
	else
	{
		uint32_t exponent_mask = 0xffffffff >> (exponent_less_1023 - 20);
		uint32_t tmp;

		if ((lsw & exponent_mask) == 0)
			/* x is an integral value. */
			return x;

		tmp = lsw + (1 << (51 - exponent_less_1023));
		if (tmp < lsw)
			msw += 1;
		lsw = tmp;

		lsw &= ~exponent_mask;
	}
	INSERT_WORDS(x, msw, lsw);

	return x;
}
#endif

typedef struct
{
	uint8_t                      packetBuffer[AX25_PACKET_BUFFER_SIZE];
	uint16_t                     packetBufferBitPosition;
	uint16_t                     bitStuffingCounter;
	uint16_t                     crc;
	bool                         currentBitNRZI;
	bool                         baudIs300;
} AX25Encoder_t;

// Beaconing

typedef struct
{
	double                       latitude;
	double                       longitude;
} aprsBeaconingCoordinates_t;

typedef struct
{
	uint32_t                     time; // ms
	uint16_t                     bearing; // hundredth degree
	uint16_t                     speed; // hundredth of knots
	aprsBeaconingCoordinates_t   coords;
} aprsBeaconingLocation_t;

typedef struct
{
	ticksTimer_t                 checkTimer;
	ticksTimer_t                 nextBeaconTimer;

#if defined(APRS_USE_COURSETO_FOR_BEARING)
	aprsBeaconingCoordinates_t   previousBearingPosition;
	uint16_t                     currentCourse;
#endif

	aprsBeaconingLocation_t      currentLocation;
	aprsBeaconingLocation_t      previousLocation;

	codeplugAPRS_Config_t        aprsConfig[APRS_CONFIG_CHANNEL + 1];

	double                       fixedLocationLat;
	double                       fixedLocationLon;

#if defined(RATE_MESSAGE_FEATURE)
	uint8_t                      rateMessageCount;
#endif

	bool                         triggerBeaconing;
	uint8_t                      decayMult; // 1 to 32

	aprsBeaconingSettings_t      settings;
}  aprsBeaconingData_t;

aprsBeaconingData_t aprsBcnData =
{
	.checkTimer = { 0, 0 },
	.nextBeaconTimer = { 0, 0 },
#if defined(APRS_USE_COURSETO_FOR_BEARING)
	.previousBearingPosition = { 0.0, 0.0 },
	.currentCourse = 0U,
#endif
	.currentLocation = { .time = 0U, .coords = { .latitude = 0.0, .longitude = 0.0 }, .bearing = UINT16_MAX, .speed = 0U },
	.previousLocation = { .time = 0U, .coords = { .latitude = 0.0, .longitude = 0.0 }, .bearing = UINT16_MAX, .speed = 0U },
	.fixedLocationLat = NAN,
	.fixedLocationLon = NAN,
#if defined(RATE_MESSAGE_FEATURE)
	.rateMessageCount = 0,
#endif
	.triggerBeaconing = false,
	.decayMult = APRS_BEACON_DECAY_MULT_MIN,
	.settings =
	{
		.state = (APRS_BEACONING_STATE_LOCATION_FROM_CHANNEL | APRS_BEACONING_STATE_DECAY_ALGO_ENABLED | APRS_BEACONING_STATE_COMPRESSED_FORMAT),
		.mode = APRS_BEACONING_MODE_OFF,
		.initialInterval = APRS_BEACON_INITIAL_INTERVAL_DEFAULT, // Offset in initialIntervals[]
#if defined(RATE_MESSAGE_FEATURE)
		.messageInterval = APRS_BEACON_MESSAGE_INTERVAL_DEFAULT,
#endif
		.smart =
		{
			.slowRate = APRS_SMART_BEACON_SLOW_RATE_DEFAULT,
			.fastRate = APRS_SMART_BEACON_FAST_RATE_DEFAULT,
			.lowSpeed = APRS_SMART_BEACON_LOW_SPEED_DEFAULT,
			.highSpeed = APRS_SMART_BEACON_HIGH_SPEED_DEFAULT,
			.turnAngle = APRS_SMART_BEACON_TURN_ANGLE_DEFAULT,
			.turnSlope = APRS_SMART_BEACON_TURN_SLOPE_DEFAULT,
			.turnTime = APRS_SMART_BEACON_TURN_TIME_DEFAULT,
		}
	}
};

const uint16_t initialIntervalsInSecs[APRS_BEACON_INITIAL_INTERVAL_MAX + 1] = { 12, 30, 60, 120, 180, 300, 600, 1200, 1800, 3600 };

static char myCall[16];
static int lenBits = 0;
static volatile uint32_t lastTone;
static volatile int bitPos = 0;
static volatile uint8_t dataByte;
static AX25Encoder_t encoderData;
static codeplugAPRS_Config_t *aprsConfig;

volatile aprsSendProgress_t aprsTxProgress = APRS_TX_IDLE; // used in the ISR


static bool aprsBeaconingStateEnabled(aprsBeaconingStates_t s);
static bool aprsBeaconingLocationIsValid(aprsBeaconingLocation_t *location);
static bool gpsFixIsValid(void);
static void enqueueCharNrzi(AX25Encoder_t *encoderData, uint8_t data, bool useBitStuffing);

static void enqueueBit(AX25Encoder_t *encoderData, bool data)
{
	// Never past the end of the buffer. Nothing the radio sends comes near it, but
	// a frame cut short is better than memory after the buffer overwritten.
	if (encoderData->packetBufferBitPosition >= (AX25_PACKET_BUFFER_SIZE * 8U))
	{
		return;
	}

	if (data)
	{
		encoderData->packetBuffer[encoderData->packetBufferBitPosition / 8U] |= 0x01 << (encoderData->packetBufferBitPosition % 8U);
	}
	encoderData->packetBufferBitPosition++;
}

static void updateCRC(AX25Encoder_t *encoderData, bool dataBit)
{
	uint16_t crcXorDataBit = (encoderData->crc ^ dataBit);

	encoderData->crc >>= 1;

	if (crcXorDataBit & 0x01)
	{
		encoderData->crc ^= 0x8408;
	}
}

static void enqueueCRC(AX25Encoder_t *encoderData)
{
	uint8_t crc_lo = (encoderData->crc ^ 0xff);
	uint8_t crc_hi = ((encoderData->crc >> 8) ^ 0xff);

	enqueueCharNrzi(encoderData, crc_lo, true);
	enqueueCharNrzi(encoderData, crc_hi, true);
}

static void enqueuePadOfLength(AX25Encoder_t *encoderData, uint32_t len)
{
	for (uint8_t j = 0; j < len; j++)
	{
		enqueueCharNrzi(encoderData, (' ' << 1), true);
	}
}

static void enqueueCharNrzi(AX25Encoder_t *encoderData, uint8_t data, bool useBitStuffing)
{
	bool currentBit;

	for (uint8_t i = 0; i < 8U; i++)
	{
		currentBit = (data & 0x01);

		updateCRC(encoderData, currentBit);

		if (currentBit)
		{
			enqueueBit(encoderData, encoderData->currentBitNRZI);
			encoderData->bitStuffingCounter++;

			if (useBitStuffing && (encoderData->bitStuffingCounter == 5))
			{
				encoderData->currentBitNRZI ^= 1;
				enqueueBit(encoderData, encoderData->currentBitNRZI);

				encoderData->bitStuffingCounter = 0U;
			}
		}
		else
		{
			encoderData->currentBitNRZI ^= 1;
			enqueueBit(encoderData, encoderData->currentBitNRZI);

			encoderData->bitStuffingCounter = 0U;
		}

		data >>= 1;
	}
}

static void enqueueString(AX25Encoder_t *encoderData, const char *str)
{
	uint8_t i = 0;

	while (str[i] != 0)
	{
		enqueueCharNrzi(encoderData, str[i], true);
		i++;
	};
}

static void enqueueFlagOfLength(AX25Encoder_t *encoderData, uint8_t len)
{
	for (uint8_t i = 0; i < len; i++)
	{
		enqueueCharNrzi(encoderData, 0x7E, false); // 0x7E flag
	}
}

static void enqueueHeader(AX25Encoder_t *encoderData)
{
	//int len = MIN(strlen(APRS_DESTINATION), 6U);

	for (uint32_t i = 0; i < strlen(APRS_DESTINATION); i++)
	{
		enqueueCharNrzi(encoderData, (APRS_DESTINATION[i] << 1), true);
	}

	//if (len < 6U)
	//{
	//	enqueuePadOfLength(encoderData, (6U - len));
	//}

	enqueueCharNrzi(encoderData, ('0' << 1), true);

	uint8_t len = MIN(strlen(myCall), 6U);

	for (uint8_t i = 0; i < len; i++)
	{
		enqueueCharNrzi(encoderData, (myCall[i] << 1), true);
	}

	if (len < 6U)
	{
		enqueuePadOfLength(encoderData, (6U - len));
	}

	enqueueCharNrzi(encoderData, ((aprsConfig->senderSSID + '0') << 1), true);

	uint8_t numPaths = ((strlen(aprsConfig->paths[1].name) == 0) ? 1U : 2U);

	for (uint8_t p = 0; p < numPaths; p++)
	{
		len = MIN(strlen(aprsConfig->paths[p].name), 6U);

		for (uint8_t i = 0; i < len; i++)
		{
			enqueueCharNrzi(encoderData, (aprsConfig->paths[p].name[i] << 1), true);
		}

		if (len < 6U)
		{
			enqueuePadOfLength(encoderData, (6U - len));
		}

		uint8_t isEnd = (p == (numPaths - 1)) ? 1U : 0U;

		enqueueCharNrzi(encoderData, (((aprsConfig->paths[p].SSID + '0') << 1) + isEnd), true);
	}

	enqueueCharNrzi(encoderData, 0x03, true);
	enqueueCharNrzi(encoderData, 0xF0, true);
}

static void enqueuePayload(AX25Encoder_t *encoderData, const char *latStr, const char *lonStr, const char *courseAndSpeed)
{
	static const uint8_t DT_POS = '!';

	uint8_t symbol = (aprsConfig->iconIndex + '!'); //'+'; // + = cross symbol. Y = yacht etc
	uint8_t symTable = ((aprsConfig->iconTable == 0) ? '/' : '\\'); //' = secondary table

	enqueueCharNrzi(encoderData, DT_POS, true);

	if (aprsBeaconingStateEnabled(APRS_BEACONING_STATE_COMPRESSED_FORMAT))
	{
		enqueueCharNrzi(encoderData, symTable, true);
		enqueueString(encoderData, latStr);
		enqueueString(encoderData, lonStr);
		enqueueCharNrzi(encoderData, symbol, true);
		enqueueString(encoderData, (courseAndSpeed ? courseAndSpeed : "  "));
		enqueueCharNrzi(encoderData, (courseAndSpeed ? (/*0x26 (Other)*/ 0x3E /* (RMC)*/ + '!') : '!'), true);
	}
	else
	{
		enqueueString(encoderData, latStr);
		enqueueCharNrzi(encoderData, symTable, true);
		enqueueString(encoderData, lonStr);
		enqueueCharNrzi(encoderData, symbol, true);

		if (courseAndSpeed != NULL)
		{
			enqueueString(encoderData, courseAndSpeed);
		}
	}

	if (aprsConfig->comment[0] != 0)
	{
		enqueueString(encoderData, aprsConfig->comment);
	}
}

static void aprsTxEnded(void)
{
#if defined(CPU_MK22FN512VLL12)
	trxSelectVoiceChannel(AT1846_VOICE_CHANNEL_MIC);
	disableAudioAmp(AUDIO_AMP_MODE_RF);
#else // CPU_MK22FN512VLL12
#if defined(PLATFORM_MD9600)
	trxDTMFoff(true);
	trxSelectVoiceChannel(AT1846_VOICE_CHANNEL_MIC);
#else // PLATFORM_MD9600
	AT1846sWriteTone1Reg(0);

	disableAudioAmp(AUDIO_AMP_MODE_RF);
	HAL_GPIO_WritePin(RX_AUDIO_MUX_GPIO_Port, RX_AUDIO_MUX_Pin, GPIO_PIN_SET);
	radioWriteReg2byte(0x44, 0x06, 0xCC); // set back to FM default

	trxSelectVoiceChannel(AT1846_VOICE_CHANNEL_MIC);
#endif // PLATFORM_MD9600
	HRC6000SetMic(true);
#endif // CPU_MK22FN512VLL12
}

static void aprsCompressLatitude(double lat, char *str)
{
	int latRes;
	int lat0, lat1, lat2, lat3;

	latRes = (int)round(380926.0 * (90.0 - lat));

	lat0 = (latRes / (91 * 91 * 91));
	latRes -= (lat0 * (91 * 91 * 91));

	lat1 = (latRes / (91 * 91));
	latRes -= (lat1 * (91 * 91));

	lat2 = (latRes / 91);
	latRes -= (lat2 * 91);

	lat3 = latRes;

	str[0] = (lat0 + '!');
	str[1] = (lat1 + '!');
	str[2] = (lat2 + '!');
	str[3] = (lat3 + '!');
	str[4] = 0;
}

static void aprsCompressLongitude(double lon, char *str)
{
	int lonRes;
	int lon0, lon1, lon2, lon3;

	lonRes = (int)round(190463.0 * (180.0 + lon));

	lon0 = (lonRes / (91 * 91 * 91));
	lonRes -= (lon0 * (91 * 91 * 91));

	lon1 = (lonRes / (91 * 91));
	lonRes -= (lon1 * (91 * 91));

	lon2 = (lonRes / 91);
	lonRes -= (lon2 * 91);

	lon3 = lonRes;

	str[0] = (lon0 + '!');
	str[1] = (lon1 + '!');
	str[2] = (lon2 + '!');
	str[3] = (lon3 + '!');
	str[4] = 0;
}

// Everything after a frame has been assembled: work out its length, choose the
// bit rate, hand the radio over to the tone generator and start the timer that
// clocks the bits out. Factored out of aprsSendPacket so that message frames can
// use the identical, already proven transmit path.
static void aprsStartBitStream(void)
{
	lenBits = encoderData.packetBufferBitPosition; // every bit, including a part-filled last byte
	bitPos = 0;
	lastTone = 0xFFFFFFFF;

#if defined(PLATFORM_MD9600)
	encoderData.baudIs300 = false;
#else // PLATFORM_MD9600
	encoderData.baudIs300 = ((aprsConfig->flags & 0x01) != 0);
#endif // PLATFORM_MD9600

#if defined(CPU_MK22FN512VLL12)
	ftm_config_t ftmInfo;
	FTM_GetDefaultConfig(&ftmInfo);
	ftmInfo.prescale = kFTM_Prescale_Divide_4;
	FTM_Init(FTM1, &ftmInfo);
	FTM_SetTimerPeriod(FTM1, USEC_TO_COUNT((!encoderData.baudIs300 ? 833U : 3333U), (CLOCK_GetFreq(kCLOCK_BusClk) / 4)));//use 2087U instead of 833 if in HS_RUN clock speed mode
	FTM_EnableInterrupts(FTM1, kFTM_TimeOverflowInterruptEnable);

	GPIO_PinWrite(GPIO_RX_audio_mux, Pin_RX_audio_mux, 1);
	enableAudioAmp(AUDIO_AMP_MODE_RF);

	AT1846sWriteTone1Reg(lastTone);
	trxSelectVoiceChannel(AT1846_VOICE_CHANNEL_TONE1);

	aprsTxProgress = APRS_TX_IN_PROGRESS;

	EnableIRQ(FTM1_IRQn);
	FTM_StartTimer(FTM1, kFTM_SystemClock);
#else // CPU_MK22FN512VLL12
	htim6.Instance = TIM6;
	htim6.Init.Prescaler = encoderData.baudIs300 ? 7 : 1; // 7 gives 300 baud. 1 gives 1200 baud
	htim6.Init.CounterMode = TIM_COUNTERMODE_UP;

#if defined(PLATFORM_MD9600)
	htim6.Init.Period = 15000;
#else // PLATFORM_MD9600
	htim6.Init.Period = 30000;
#endif // PLATFORM_MD9600

	htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
	if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
	{
		Error_Handler();
	}

	TIM_MasterConfigTypeDef sMasterConfig = { 0 };
	sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
	sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
	if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
	{
		Error_Handler();
	}

	HRC6000SetMic(false);

#if defined(PLATFORM_MD9600)
	uint32_t lastTone = 1;
	trxSetTone1(lastTone);
#else // PLATFORM_MD9600
	uint32_t lastTone = 0;
	AT1846sWriteTone1Reg(lastTone);
	trxSelectVoiceChannel(AT1846_VOICE_CHANNEL_TONE1);

	int volume = getVolumeControl() + 30;

	if (volume >= 0)
	{
		radioWriteReg2byte(0x44, 0x06, volume);
		HAL_GPIO_WritePin(RX_AUDIO_MUX_GPIO_Port, RX_AUDIO_MUX_Pin, GPIO_PIN_RESET);
		enableAudioAmp(AUDIO_AMP_MODE_RF);
	}
#endif // PLATFORM_MD9600

	aprsTxProgress = APRS_TX_IN_PROGRESS;
	HAL_TIM_Base_Start_IT(&htim6);
#endif // CPU_MK22FN512VLL12
}

static bool aprsSendPacket(codeplugAPRS_Config_t *config, aprsBeaconingLocation_t *previousLocation, aprsBeaconingLocation_t *currentLocation)
{
	double lat;
	double lon;
	char latStr[16];
	char lonStr[16];
	char courseSpeedStr[16];
	bool courseAndSpeed = false;
	bool gpsAndLocationAreValid = (aprsBeaconingStateEnabled(APRS_BEACONING_STATE_LOCATION_FROM_GPS) && aprsBeaconingLocationIsValid(previousLocation) && aprsBeaconingLocationIsValid(currentLocation));

	aprsConfig = config;

	encoderData.bitStuffingCounter = 0;
	encoderData.currentBitNRZI = false; // clear
	memset(encoderData.packetBuffer, 0, AX25_PACKET_BUFFER_SIZE);
	encoderData.packetBufferBitPosition = 0;

	codeplugGetRadioName(myCall);
	myCall[6] = 0; //truncate to 6 chars max

	int ambiguity = (aprsConfig->flags & 0xF0) >> 4;
	bool usePosition = (aprsConfig->flags & 0x02);

	if (usePosition)
	{
		uint32_t tmp1 = aprsConfig->latitude[2];
		tmp1 = (tmp1 << 8) + aprsConfig->latitude[1];
		tmp1 = (tmp1 << 8) + aprsConfig->latitude[0];

		lat = latLongFixed24ToDouble(tmp1);

		uint32_t tmp2 = aprsConfig->longitude[2];
		tmp2 = (tmp2 << 8) + aprsConfig->longitude[1];
		tmp2 = (tmp2 << 8) + aprsConfig->longitude[0];

		lon = latLongFixed24ToDouble(tmp2);
	}
	else
	{
		lat = currentLocation->coords.latitude;
		lon = currentLocation->coords.longitude;
	}

	// I used to refuse a zero position here, to keep "NN!!NN!!" (what 0,0
	// compresses to) off the network. Bad idea: with no GPS fix the position is
	// 0,0, the stock firmware happily sends it, and mine just stayed silent. I'd
	// rather it behave like stock, so that guard is gone. The real cure for
	// NN!!NN!! is a real position, not a silent radio.
	//
	// 0,0 is nobody's position though, and this one had me scratching my head. My beacons
	// were reaching the igate (TB8ATA-8 heard them fine, qAR and all) but I never
	// showed up on aprs.fi, and I blamed the igate. It was the position: the fixed
	// one in the APRS settings was left at zero, and aprs.fi quietly drops a 0,0
	// position. So now the best real position the radio has goes out instead - the
	// GPS's if it has a fix right now, otherwise the last one it saved (only ever
	// from a fix, or typed in) - and only if it has none at all does 0,0 still go.
	if ((fabs(lat) < 1E-6) && (fabs(lon) < 1E-6))
	{
#if defined(HAS_GPS)
		if (gpsFixIsValid())
		{
			lat = gpsData.LatitudeHiRes;
			lon = gpsData.LongitudeHiRes;
		}
		else
#endif
		if (nonVolatileSettings.locationLat != SETTINGS_UNITIALISED_LOCATION_LAT)
		{
			lat = latLongFixed32ToDouble(nonVolatileSettings.locationLat);
			lon = latLongFixed32ToDouble(nonVolatileSettings.locationLon);
		}
	}

	if (ambiguity != 0)
	{
		int tmp;
		double ambCoeff;

		switch (ambiguity)
		{
			case 1: //0.0005 deg
				ambCoeff = 5000;
				break;
			case 2: //0.001 deg
				ambCoeff = 1000;
				break;
			case 3: //0.005 deg
				ambCoeff = 500;
				break;
			case 4: //0.01 deg
				ambCoeff = 100;
				break;
			case 5: //0.05 deg
				ambCoeff = 50;
				break;
			case 6: //0.1 deg
				ambCoeff = 10;
				break;
			case 7: //0.5 deg
				ambCoeff = 5;
				break;
			default:
				ambCoeff = 1;
				break;
		}
		tmp = lat * ambCoeff;
		lat = tmp / ambCoeff;

		tmp = lon * ambCoeff;
		lon = tmp / ambCoeff;
	}

	if (aprsBeaconingStateEnabled(APRS_BEACONING_STATE_COMPRESSED_FORMAT))
	{
		aprsCompressLatitude(lat, latStr);
		aprsCompressLongitude(lon, lonStr);

		if (gpsAndLocationAreValid)
		{
			// Altitude ?

			if (currentLocation->speed > 0)
			{
				int iSpeed = (int)(currentLocation->speed * 1E-2);
				int iCourse = (int)(currentLocation->bearing * 1E-2);
				int c; // speed
				int s; // course

				// Course
				c = (iCourse / 4);

				if (c < 0)
				{
					c += 90;
				}

				if (c >= 90)
				{
					c -= 90;
				}

				c += '!';

				// Speed
				s = (int)round(log(iSpeed + 1.0) / log(1.08));
				s += '!';

				courseSpeedStr[0] = c;
				courseSpeedStr[1] = s;
				courseSpeedStr[2] = 0;
				courseAndSpeed = true;
			}

		}
	}
	else
	{
		bool latHemisphere = (lat >= 0);
		bool lonHemisphere = (lon >= 0);

		lat = fabs(lat);
		lon = fabs(lon);

		int latDeg = (int)lat;
		double latMins = ((lat - latDeg) * 60.0);
		int latMinsInt = (int)latMins;
		int latMinsDecimal = (int)round(((latMins - latMinsInt) * 1E2));
		int lonDeg = (int)lon;
		double lonMins = ((lon - lonDeg) * 60.0);
		int lonMinsInt = (int)lonMins;
		int lonMinsDecimal = (int)round(((lonMins - lonMinsInt) * 1E2));

		snprintf(latStr, 16, "%02d%02d.%02d%c", latDeg, latMinsInt, latMinsDecimal, latHemisphere ? 'N' : 'S');
		snprintf(lonStr, 16, "%03d%02d.%02d%c", lonDeg, lonMinsInt, lonMinsDecimal, lonHemisphere ? 'E' : 'W');

		if (gpsAndLocationAreValid)
		{
			// Altitude ?

			if (currentLocation->speed > 0)
			{
				snprintf(courseSpeedStr, 16, "%03u/%03u", (uint16_t)(currentLocation->bearing * 1E-2), (uint16_t)(currentLocation->speed * 1E-2));
				courseAndSpeed = true;
			}
		}
	}

	enqueueFlagOfLength(&encoderData, APRS_PREAMBLE_FLAGS);

	encoderData.crc = 0xFFFF; // Initialise CRC now, after data has been sent as CRC does is only for data bytes
	enqueueHeader(&encoderData);
	enqueuePayload(&encoderData, latStr, lonStr, (courseAndSpeed ? courseSpeedStr : NULL));
	enqueueCRC(&encoderData);
	enqueueFlagOfLength(&encoderData, 3U);

	aprsStartBitStream();

	return true;
}

// Sends an arbitrary APRS information field: a message, or the acknowledgement
// of one. The frame is identical to a beacon apart from what goes after the
// header, so everything here reuses the beacon's own encoder and transmit path
// rather than repeating it.
//
// The radio is keyed here and left transmitting; aprsBeaconingTxStateTick(),
// which the main loop already calls, sees the bit stream finish and unkeys. Do
// not add a second teardown - that state machine owns it.
bool aprsSendInfoField(const char *info)
{
	uint32_t m;

	if ((info == NULL) || (info[0] == 0) || (aprsTxProgress != APRS_TX_IDLE) ||
			trxTransmissionEnabled || trxIsTransmitting ||
			(trxGetMode() != RADIO_MODE_ANALOG) ||
			(codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_RX_ONLY) != 0) ||
			((nonVolatileSettings.txFreqLimited != BAND_LIMITS_NONE) &&
					(trxCheckFrequencyInAmateurBand(currentChannelData->txFreq) == false)))
	{
		return false;
	}

	aprsConfig = &aprsBcnData.aprsConfig[APRS_CONFIG_CHANNEL];

	codeplugGetRadioName(myCall);
	myCall[6] = 0;

	encoderData.bitStuffingCounter = 0;
	encoderData.currentBitNRZI = false;
	memset(encoderData.packetBuffer, 0, AX25_PACKET_BUFFER_SIZE);
	encoderData.packetBufferBitPosition = 0;

	// Hand the codec back before keying. The receive decoders hold it while the
	// squelch is open, and transmit needs it for the microphone path; waiting for
	// their own tick to notice is too late, because the settling delay below runs
	// first and blocks this task.
	fiveToneDecodeReleaseCapture();

	LedWrite(LED_GREEN, 0);
	LedWrite(LED_RED, 1);
	HRC6000ClearIsWakingState();
	trxIsTransmitting = true;
	trxSetTX(); // sets trxTransmissionEnabled itself

	m = ticksGetMillis();
	while ((ticksGetMillis() - m) < APRS_XMIT_TX_DELAY)
	{
		vTaskDelay((1 / portTICK_PERIOD_MS));
	}

	enqueueFlagOfLength(&encoderData, APRS_PREAMBLE_FLAGS);
	encoderData.crc = 0xFFFF; // only covers the data, so start it after the flags
	enqueueHeader(&encoderData);
	enqueueString(&encoderData, info);
	enqueueCRC(&encoderData);
	enqueueFlagOfLength(&encoderData, 3U);

	aprsStartBitStream();
	return true;
}

void aprsBitStreamSender(void)
{
	uint32_t newTone;

	if (aprsTxProgress != APRS_TX_IN_PROGRESS)
	{
		return;
	}

	// Every bit of the frame goes out, the closing flags included. This used to
	// count whole bytes and stop as the last one was loaded, so that byte - and any
	// part-filled one after it, bit stuffing rarely leaves a whole number - never
	// went. Of the three closing flags only about one made it to the air, and a
	// receiver that missed the end threw the whole packet away. For a long time
	// all I heard back from the city digis was the odd short beep; this may well
	// have been part of it. Stopping only here, a full bit period after the
	// last bit was set, lets that last bit play out.
	if (bitPos >= lenBits)
	{
//		AT1846SWriteTone1Reg(0);
		// just stop the ISR and flag that the data has been sent.

#if defined(CPU_MK22FN512VLL12)
		FTM_StopTimer(FTM1);
		DisableIRQ(FTM1_IRQn);
#else // CPU_MK22FN512VLL12
		HAL_TIM_Base_Stop_IT(&htim6);
#endif // CPU_MK22FN512VLL12

		aprsTxProgress = APRS_TX_FINISHED;			// Tell the foreground we've finished, so it can do the speaker and other stuff
		return;
	}

	if ((bitPos % 8) == 0)
	{
		dataByte = encoderData.packetBuffer[bitPos / 8];
	}

#if defined(CPU_MK22FN512VLL12)
	newTone = (encoderData.baudIs300 ? 16000 : 12000) + ((dataByte & 0x01) ? (encoderData.baudIs300 ? 2000 : 10000) : 0);

	if (newTone != lastTone)
	{
		AT1846sWriteTone1Reg(newTone);
	}
#else // CPU_MK22FN512VLL12
#if defined(PLATFORM_MD9600)
	newTone = 1200 + ((dataByte & 0x01) ? 1000 : 0);
	if (newTone != lastTone)
	{
		int tval = (newTone * 65536) / 32000;												//calculate the value required to generate this tone
		uint8_t tH = (tval >> 8) & 0xFF;
		uint8_t tL = tval & 0xFF;
		SPI0WritePageRegByteExtended(0x01, 0x11B, tH);// Set  DTMF tone osc 1 to frequency of the required tone
		SPI0WritePageRegByteExtended(0x01, 0x11A, tL);


		SPI0WritePageRegByteExtended(0x01, 0x123, tH);// Set  DTMF tone osc 2 to frequency of the required tone
		SPI0WritePageRegByteExtended(0x01, 0x122, tL);
		lastTone = newTone;
	}
#else // PLATFORM_MD9600
	newTone = (encoderData.baudIs300 ? 16000 : 12000) + ((dataByte & 0x01) ? (encoderData.baudIs300 ? 2000 : 10000) : 0);
	if (newTone != lastTone)
	{
		AT1846sWriteTone1Reg(newTone);
		lastTone = newTone;
	}
#endif // PLATFORM_MD9600
#endif // CPU_MK22FN512VLL12

	dataByte >>= 1;
	bitPos++;
}

#if defined(CPU_MK22FN512VLL12)
void FTM1_IRQHandler(void)
{
    /* Clear interrupt flag.*/
    FTM_ClearStatusFlags(FTM1, kFTM_TimeOverflowFlag);
	aprsBitStreamSender();
    __DSB();
}

#endif // CPU_MK22FN512VLL12

//
// *****************
// *** Beaconing ***
// *****************
//
// Aknowlegments:
//     - SmartBeaconing™ is a beaconing algorithm invented by Tony Arnerich KD7TA and Steve Bragg KA9MVA.
//     - largely inspired by https://github.com/erstec/APRS-ESP/blob/master/src/smartBeaconing.cpp from Ernest Stepanov LY3PH.
//

static bool aprsBeaconingStateEnabled(aprsBeaconingStates_t s)
{
	return ((aprsBcnData.settings.state & s) == s);
}

// Returns true if the bit value has changed.
static bool aprsBeaconingStateSetEnable(aprsBeaconingStates_t s, bool enabled)
{
	if ((s & (APRS_BEACONING_STATE_LOCATION_FROM_CHANNEL | APRS_BEACONING_STATE_LOCATION_FROM_GPS)) != 0)
	{
		return false;
	}

	bool ret = (((aprsBcnData.settings.state & s) != 0) != enabled);

	if (enabled)
	{
		aprsBcnData.settings.state |= s;
	}
	else
	{
		aprsBcnData.settings.state &= ~s;
	}

	return ret;
}

static bool gpsPVTIsValid(void)
{
#if defined(HAS_GPS)
	if (nonVolatileSettings.gps > GPS_MODE_OFF)
	{
		return ((gpsData.Status & (GPS_STATUS_HAS_FIX | GPS_STATUS_HAS_POSITION | GPS_STATUS_HAS_SPEED | GPS_STATUS_HAS_TIME)) == (GPS_STATUS_HAS_FIX | GPS_STATUS_HAS_POSITION | GPS_STATUS_HAS_SPEED | GPS_STATUS_HAS_TIME));
	}
#endif

	return false;
}

static bool gpsFixIsValid(void)
{
#if defined(HAS_GPS)
	if (nonVolatileSettings.gps > GPS_MODE_OFF)
	{
		return ((gpsData.Status & GPS_STATUS_HAS_FIX) == GPS_STATUS_HAS_FIX);
	}
#endif

	return false;
}

static void aprsBeaconingInvalidateLocation(aprsBeaconingLocation_t *location)
{
	location->bearing = UINT16_MAX;
	location->coords.latitude = location->coords.longitude = 0.0;
	location->speed = 0U;
	location->time = 0U;
}

static bool aprsBeaconingLocationIsValid(aprsBeaconingLocation_t *location)
{
	return (location->bearing != UINT16_MAX);
}

static bool aprsBeaconingCurrentPositionIsValid(void)
{
	return ((aprsBeaconingStateEnabled(APRS_BEACONING_STATE_LOCATION_FROM_GPS) && (nonVolatileSettings.locationLat != SETTINGS_UNITIALISED_LOCATION_LAT)) ||
			(aprsBeaconingStateEnabled(APRS_BEACONING_STATE_LOCATION_FROM_CHANNEL) && ((isnan(aprsBcnData.fixedLocationLat) == 0) && (isnan(aprsBcnData.fixedLocationLon) == 0))));
}

static double aprsGetFixedPositionLatitude(void)
{
	double lat = 0.0;

	if (isnan(aprsBcnData.fixedLocationLat) == 0)
	{
		return aprsBcnData.fixedLocationLat;
	}
	else
	{
		if (aprsBeaconingStateEnabled(APRS_BEACONING_STATE_HAS_APRS_CONFIG))
		{
			if (aprsBcnData.aprsConfig[APRS_CONFIG_CHANNEL].flags & 0x02)
			{
				uint32_t tLat = aprsBcnData.aprsConfig[APRS_CONFIG_CHANNEL].latitude[2];
				tLat = (tLat << 8) + aprsBcnData.aprsConfig[APRS_CONFIG_CHANNEL].latitude[1];
				tLat = (tLat << 8) + aprsBcnData.aprsConfig[APRS_CONFIG_CHANNEL].latitude[0];

				aprsBcnData.fixedLocationLat = latLongFixed24ToDouble(tLat);

				return aprsBcnData.fixedLocationLat;
			}
		}

		// Returns NAN if no position is valid, not even the one stored in the settings.
		if (nonVolatileSettings.locationLat == SETTINGS_UNITIALISED_LOCATION_LAT)
		{
			return NAN;
		}
		else
		{
			aprsBcnData.fixedLocationLat = lat = latLongFixed32ToDouble(nonVolatileSettings.locationLat);
		}
	}

	return lat;
}

static double aprsGetFixedPositionLongitude(void)
{
	double lon = 0.0;

	if (isnan(aprsBcnData.fixedLocationLon) == 0)
	{
		return aprsBcnData.fixedLocationLon;
	}
	else
	{
		if (aprsBeaconingStateEnabled(APRS_BEACONING_STATE_HAS_APRS_CONFIG))
		{
			if (aprsBcnData.aprsConfig[APRS_CONFIG_CHANNEL].flags & 0x02)
			{
				uint32_t tLon = aprsBcnData.aprsConfig[APRS_CONFIG_CHANNEL].longitude[2];
				tLon = (tLon << 8) + aprsBcnData.aprsConfig[APRS_CONFIG_CHANNEL].longitude[1];
				tLon = (tLon << 8) + aprsBcnData.aprsConfig[APRS_CONFIG_CHANNEL].longitude[0];

				aprsBcnData.fixedLocationLon = latLongFixed24ToDouble(tLon);

				return aprsBcnData.fixedLocationLon;
			}
		}

		if (nonVolatileSettings.locationLat == SETTINGS_UNITIALISED_LOCATION_LAT)
		{
			return NAN;
		}
		else
		{
			aprsBcnData.fixedLocationLon = lon = latLongFixed32ToDouble(nonVolatileSettings.locationLon);
		}
	}

	return lon;
}

static double aprsGetBearingAngleDiff(double a, double b)
{
	double delta = fmod(fabs(a - b), 360.0);

	return (delta <= 180.0 ? delta : (360.0 - delta));
}

static double DistanceBetweenTwoCoords(double lat1, double lon1, double lat2, double lon2)
{
	// returns distance in meters between two positions, both specified
	// as signed decimal-degrees latitude and longitude. Uses great-circle
	// distance computation for hypothetical sphere of radius 6372795 meters.
	// Because Earth is no exact sphere, rounding errors may be up to 0.5%.
	// Courtesy of Maarten Lamers
	double delta = ((lon1 - lon2) * DEG_TO_RAD);
	double sdlon = sin(delta);
	double cdlon = cos(delta);
	lat1 = (lat1 * DEG_TO_RAD);
	lat2 = (lat2 * DEG_TO_RAD);
	double slat1 = sin(lat1);
	double clat1 = cos(lat1);
	double slat2 = sin(lat2);
	double clat2 = cos(lat2);
	delta = (clat1 * slat2) - (slat1 * clat2 * cdlon);
	delta = SQUARE(delta);
	delta += SQUARE(clat2 * sdlon);
	delta = sqrt(delta);
	double denom = (slat1 * slat2) + (clat1 * clat2 * cdlon);
	delta = atan2(delta, denom);

	return (delta * 6372795);
}

#if defined(APRS_USE_COURSETO_FOR_BEARING)

static double courseTo(double lat1, double lon1, double lat2, double lon2)
{
	// returns course in degrees (North=0, West=270) from position 1 to position 2,
	// both specified as signed decimal-degrees latitude and longitude.
	// Because Earth is no exact sphere, calculated course may be off by a tiny fraction.
	// Courtesy of Maarten Lamers
	double dlon = ((lon2 - lon1) * DEG_TO_RAD);
	lat1 = (lat1 * DEG_TO_RAD);
	lat2 = (lat2 * DEG_TO_RAD);
	double a1 = sin(dlon) * cos(lat2);
	double a2 = sin(lat1) * cos(lat2) * cos(dlon);
	a2 = cos(lat1) * sin(lat2) - a2;
	a2 = atan2(a1, a2);

	if (a2 < 0.0)
	{
		a2 += TWO_PI;
	}

	return (a2 * RAD_TO_DEG);
}

uint16_t aprsBeaconingGetBearing(void)
{
	if (DistanceBetweenTwoCoords(aprsBcnData.previousBearingPosition.latitude, aprsBcnData.previousBearingPosition.longitude,
			aprsBcnData.currentLocation.coords.latitude, aprsBcnData.currentLocation.coords.longitude) > 2.0)
	{
		aprsBcnData.currentCourse = (uint16_t)(courseTo(aprsBcnData.previousBearingPosition.latitude, aprsBcnData.previousBearingPosition.longitude,
				aprsBcnData.currentLocation.coords.latitude, aprsBcnData.currentLocation.coords.longitude) * 1E2);

		memcpy(&aprsBcnData.previousBearingPosition, &aprsBcnData.currentLocation.coords, sizeof(aprsBeaconingCoordinates_t));
	}

	return aprsBcnData.currentCourse;
}
#endif // APRS_USE_COURSETO_FOR_BEARING

static bool aprsSmartBeaconingCornerPegging(double *currentSpeedMPS, uint32_t *timeDiff)
{
	*currentSpeedMPS = ((aprsBcnData.currentLocation.speed * 1E-2) * MPS_PER_KNOT);
	*timeDiff = (aprsBcnData.currentLocation.time - aprsBcnData.previousLocation.time);

	if (aprsBcnData.currentLocation.speed > SMART_BEACONING_SPEED_MIN)
	{
		if (aprsBeaconingLocationIsValid(&aprsBcnData.currentLocation) == false)
		{
			return false;
		}

		// if last bearing unknown, deploy turn_time
		if (aprsBeaconingLocationIsValid(&aprsBcnData.previousLocation) == false)
		{
			return ((*timeDiff / MILLISECS_PER_SEC) >= aprsBcnData.settings.smart.turnTime);
		}

		double turnDiff = aprsGetBearingAngleDiff((aprsBcnData.currentLocation.bearing * 1E-2), (aprsBcnData.previousLocation.bearing * 1E-2));
		double turnThreshold = MIN(120.0, ((aprsBcnData.settings.smart.turnAngle * 1.0) + ((aprsBcnData.settings.smart.turnSlope * 10.0) / (*currentSpeedMPS * MPS_TO_MPH))));

		return (((*timeDiff / MILLISECS_PER_SEC) >= ((uint32_t)aprsBcnData.settings.smart.turnTime)) && (turnDiff > turnThreshold));
	}

	return false;
}

static double aprsSmartBeaconingGetMaxSpeed(double currentSpeedMPS, uint32_t timeDiff)
{
    double dist = DistanceBetweenTwoCoords(aprsBcnData.currentLocation.coords.latitude, aprsBcnData.currentLocation.coords.longitude, aprsBcnData.previousLocation.coords.latitude, aprsBcnData.previousLocation.coords.longitude);

    return MAX(MAX((dist / (timeDiff / MILLISECS_PER_SEC)), currentSpeedMPS), ((aprsBcnData.previousLocation.speed * 1E-2) * MPS_PER_KNOT));
}

static uint32_t aprsSmartBeaconingSpeedRate(double speedMPS)
{
	int32_t slowRate = (aprsBcnData.settings.smart.slowRate * 60); // min => s
	int32_t fastRate = aprsBcnData.settings.smart.fastRate; // s
	double lowSpeed = (aprsBcnData.settings.smart.lowSpeed * MPS_PER_KMPH); // => m/s
	double highSpeed = (aprsBcnData.settings.smart.highSpeed * MPS_PER_KMPH); // => m/s

	if (speedMPS <= lowSpeed)
	{
		return slowRate;
	}
	else if (speedMPS >= highSpeed)
	{
		return fastRate;
	}

	return (uint32_t)(fastRate + (slowRate - fastRate) * (highSpeed - speedMPS) / (highSpeed - lowSpeed));
}

static bool aprsSmartBeaconingCheck(void)
{
	double currentSpeedMPS;
	uint32_t timeDiff;

	if ((aprsBeaconingLocationIsValid(&aprsBcnData.previousLocation) == false) || aprsSmartBeaconingCornerPegging(&currentSpeedMPS, &timeDiff))
	{
		return true;
	}

	uint32_t speedRate = aprsSmartBeaconingSpeedRate(aprsSmartBeaconingGetMaxSpeed(currentSpeedMPS, timeDiff));

	// timediff in seconds.
	if ((timeDiff / MILLISECS_PER_SEC) >= speedRate)
	{
		return true;
	}

	return false;
}

static void aprsBeaconingDecayTick(bool reset)
{
	if ((aprsBcnData.settings.mode != APRS_BEACONING_MODE_SMART_BEACONING) && aprsBeaconingStateEnabled(APRS_BEACONING_STATE_DECAY_ALGO_ENABLED))
	{
		if (reset)
		{
			aprsBcnData.decayMult = APRS_BEACON_DECAY_MULT_MIN;
		}
		else if (aprsBcnData.decayMult < APRS_BEACON_DECAY_MULT_MAX)
		{
			aprsBcnData.decayMult *= APRS_BEACON_DECAY_MULT_STEP;
		}
	}
}

static void aprsBeaconingTxStateTick(uiEvent_t *ev)
{
	switch(aprsTxProgress)
	{
		case APRS_TX_IDLE:
		case APRS_TX_IN_PROGRESS:
			break;

		case APRS_TX_FINISHED:
			aprsTxEnded();// foreground parts of APRS send finished
			// deliberate fall through
		case APRS_TX_TERMINATE:
			trxTransmissionEnabled = false;
			// In analog mode. Stop transmitting immediately
			LedWrite(LED_RED, 0);
#if defined(HAS_GPS)
			if (nonVolatileSettings.gps > GPS_MODE_OFF)
			{
				gpsDataInputStartStop(true);
			}
#endif
			// Need to wrap this in Task Critical to avoid bus contention on the I2C bus.
			trxSetRxCSS(RADIO_DEVICE_PRIMARY, currentChannelData->rxTone);
			trxActivateRx(true);
			trxIsTransmitting = false;
			PTTToggledDown = false;

			aprsTxProgress = APRS_TX_WAIT_PTT_OFF;
			break;

		case APRS_TX_WAIT_PTT_OFF:
			if ((ev->buttons & BUTTON_PTT) == 0)
			{
				aprsTxProgress = APRS_TX_IDLE;

				if (aprsBcnData.settings.mode == APRS_BEACONING_MODE_MANUAL)
				{
					uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
				}
			}
			break;
	}
}

void aprsBeaconingInit(void)
{
	aprsBcnData.decayMult = APRS_BEACON_DECAY_MULT_MIN;
	aprsBeaconingStateSetEnable(APRS_BEACONING_STATE_ENABLED, false);
	aprsBeaconingStateSetEnable(APRS_BEACONING_STATE_HAS_GPS_FIX, false);
}

void aprsBeaconingStart(void)
{
	if (aprsBcnData.settings.mode != APRS_BEACONING_MODE_OFF)
	{
		aprsBeaconingResetTimers();
		aprsBeaconingInvalidateLocation(&aprsBcnData.currentLocation);
		aprsBeaconingStateSetEnable(APRS_BEACONING_STATE_ENABLED, true);
		aprsBeaconingStateSetEnable(APRS_BEACONING_STATE_HAS_GPS_FIX, false);
	}
}

void aprsBeaconingStop(void)
{
	if (aprsBcnData.settings.mode != APRS_BEACONING_MODE_OFF)
	{
		if (aprsTxProgress != APRS_TX_IDLE)
		{
#if defined(PLATFORM_MD9600)
			uint16_t frontPanelButtons;
			uiEvent_t ev = { .buttons = buttonsRead(&frontPanelButtons, uiDataGlobal.sk2latched), .keys = NO_KEYCODE, .rotary = 0, .function = 0, .events = NO_EVENT, .hasEvent = false, .time = ticksGetMillis() };
#else
			uiEvent_t ev = { .buttons = buttonsRead(), .keys = NO_KEYCODE, .rotary = 0, .function = 0, .events = NO_EVENT, .hasEvent = false, .time = ticksGetMillis() };
#endif

			aprsTxProgress = APRS_TX_FINISHED;
			aprsBeaconingTxStateTick(&ev);
		}

		aprsBeaconingStateSetEnable(APRS_BEACONING_STATE_ENABLED, false);
		ticksTimerReset(&aprsBcnData.checkTimer);
	}
}

void aprsBeaconingResetTimers(void)
{
	if ((uiDataGlobal.Scan.active == false) && (aprsBcnData.settings.mode != APRS_BEACONING_MODE_OFF))
	{
		uint8_t APRSConfigIndex = currentChannelData->aprsConfigIndex;
		bool    hasAPRSCfg = false;

#if defined(RATE_MESSAGE_FEATURE)
		aprsBcnData.rateMessageCount = 0;
#endif
		aprsBcnData.decayMult = APRS_BEACON_DECAY_MULT_MIN;

		aprsBeaconingInvalidateLocation(&aprsBcnData.previousLocation);
		aprsBeaconingInvalidateFixedPosition();

		ticksTimerStart(&aprsBcnData.checkTimer, MILLISECS_PER_SEC); // 1s timer
		ticksTimerReset(&aprsBcnData.nextBeaconTimer);

		if (APRSConfigIndex != 0)
		{
			if (codeplugAPRSGetDataForIndex(APRSConfigIndex, &aprsBcnData.aprsConfig[APRS_CONFIG_CHANNEL]))
			{
				hasAPRSCfg = true;
			}
		}

		aprsBeaconingStateSetEnable(APRS_BEACONING_STATE_HAS_APRS_CONFIG, hasAPRSCfg);
	}

	if (aprsBeaconingStateEnabled(APRS_BEACONING_STATE_HAS_APRS_SATELLITE_CONFIG))
	{
		aprsBeaconingStateSetEnable(APRS_BEACONING_STATE_HAS_APRS_SATELLITE_CONFIG, false);
	}
}

void aprsBeaconingPrepareSatelliteConfig(void)
{
	bool hasAprsConfig = false;
	int APRSConfigIndex = codeplugAPRSGetIndexOfName(currentActiveSatellite->name);

	if (APRSConfigIndex != 0)
	{
		if (codeplugAPRSGetDataForIndex(APRSConfigIndex, &aprsBcnData.aprsConfig[APRS_CONFIG_SATELLITE]))
		{
			hasAprsConfig = true;
		}
	}
	else
	{
		if (satelliteDataNative[uiDataGlobal.SatelliteAndAlarmData.currentSatellite].AdditionalData[0] != 0)
		{
			aprsBcnData.aprsConfig[APRS_CONFIG_SATELLITE].senderSSID = 7U;
			aprsBcnData.aprsConfig[APRS_CONFIG_SATELLITE].comment[0] = 0U;
			aprsBcnData.aprsConfig[APRS_CONFIG_SATELLITE].iconTable = 0U;
			aprsBcnData.aprsConfig[APRS_CONFIG_SATELLITE].iconIndex = ('0' - '!');

			memcpy(aprsBcnData.aprsConfig[APRS_CONFIG_SATELLITE].paths[0].name, &satelliteDataNative[uiDataGlobal.SatelliteAndAlarmData.currentSatellite].AdditionalData[0], 6U);
			aprsBcnData.aprsConfig[APRS_CONFIG_SATELLITE].paths[0].SSID = satelliteDataNative[uiDataGlobal.SatelliteAndAlarmData.currentSatellite].AdditionalData[6] - '0';
			memcpy(aprsBcnData.aprsConfig[APRS_CONFIG_SATELLITE].paths[1].name, &satelliteDataNative[uiDataGlobal.SatelliteAndAlarmData.currentSatellite].AdditionalData[7], 6U);
			aprsBcnData.aprsConfig[APRS_CONFIG_SATELLITE].paths[1].SSID = satelliteDataNative[uiDataGlobal.SatelliteAndAlarmData.currentSatellite].AdditionalData[13] - '0';

			hasAprsConfig = true;
		}
	}

	aprsBeaconingStateSetEnable(APRS_BEACONING_STATE_HAS_APRS_SATELLITE_CONFIG, hasAprsConfig);

	// Ensure to get fixed position updated, when entering the Satellite
	// screen, as aprsBeaconingTick() could invalidate it when Satellite screen
	// isn't running.
	if (hasAprsConfig)
	{
		(void)aprsGetFixedPositionLatitude();
		(void)aprsGetFixedPositionLongitude();
	}
}

void aprsBeaconingInvalidateFixedPosition(void)
{
	aprsBcnData.fixedLocationLat = aprsBcnData.fixedLocationLon = NAN;
}

bool aprsBeaconingIsSuspended(void)
{
	return ((aprsBcnData.settings.mode != APRS_BEACONING_MODE_OFF) && (aprsBeaconingStateEnabled(APRS_BEACONING_STATE_ENABLED) == false));
}

void aprsBeaconingSetSuspend(bool suspend)
{
	if (aprsBcnData.settings.mode != APRS_BEACONING_MODE_OFF)
	{
		if (suspend)
		{
			aprsBeaconingStateSetEnable(APRS_BEACONING_STATE_ENABLED, false);
		}
		else
		{
			if (aprsBeaconingStateEnabled(APRS_BEACONING_STATE_ENABLED) == false)
			{
				//aprsBcnData.decayMult = APRS_BEACON_DECAY_MULT_MIN;
				ticksTimerStart(&aprsBcnData.checkTimer, MILLISECS_PER_SEC); // 1s timer
				aprsBeaconingStateSetEnable(APRS_BEACONING_STATE_ENABLED, true);
			}
		}
	}
}

void aprsBeaconingToggles(void)
{
	if (aprsBcnData.settings.mode != APRS_BEACONING_MODE_OFF)
	{
		char buf[SCREEN_LINE_BUFFER_SIZE];
		bool running = aprsBeaconingStateEnabled(APRS_BEACONING_STATE_ENABLED);

		aprsBeaconingSetSuspend(running);

		snprintf(buf, SCREEN_LINE_BUFFER_SIZE, "Beaconing: %s", (running ? currentLanguage->off : currentLanguage->on));
		uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 1000, buf, false);
	}
}

aprsBeaconingMode_t aprsBeaconingGetMode(void)
{
	return aprsBcnData.settings.mode;
}

// Returns true if the beacon has been sent.
bool aprsBeaconingSendBeacon(bool fromSatScreen)
{
	if (uiDataGlobal.Scan.active || (trxGetMode() != RADIO_MODE_ANALOG) ||
			((fromSatScreen == false) && ((aprsBcnData.settings.mode == APRS_BEACONING_MODE_OFF) || (aprsBeaconingStateEnabled(APRS_BEACONING_STATE_ENABLED) == false)))
			|| (aprsTxProgress != APRS_TX_IDLE)
	)
	{
		return false;
	}

	// Ignore PTT rate when in satellite mode.
	if ((fromSatScreen == false) && (aprsBcnData.settings.mode == APRS_BEACONING_MODE_PTT))
	{
		if ((ticksTimerHasExpired(&aprsBcnData.nextBeaconTimer) == false)
				|| (aprsBeaconingStateEnabled(APRS_BEACONING_STATE_HAS_APRS_CONFIG) == false))
		{
			return false;
		}
	}

	/// Build and Send beacon Here
#if defined(RATE_MESSAGE_FEATURE)
	if (((aprsBcnData.rateMessageCount % aprsBcnData.settings.messageInterval) == 0))
	{

	}
#endif

	bool anyManualMode = (fromSatScreen || (aprsBcnData.settings.mode == APRS_BEACONING_MODE_PTT) || (aprsBcnData.settings.mode == APRS_BEACONING_MODE_MANUAL));

	// check config validity
	if (anyManualMode)
	{
		char buffer[SCREEN_LINE_BUFFER_SIZE];
		bool configIsValid = ((fromSatScreen && aprsBeaconingStateEnabled(APRS_BEACONING_STATE_HAS_APRS_SATELLITE_CONFIG)) ||
				((fromSatScreen == false) && aprsBeaconingStateEnabled(APRS_BEACONING_STATE_HAS_APRS_CONFIG)));
		bool positionIsValid = aprsBeaconingCurrentPositionIsValid();

		snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%s%c",
				((configIsValid && positionIsValid) ? "APRS Tx" : (positionIsValid ? currentLanguage->APRS : currentLanguage->location)),
				((configIsValid && positionIsValid) ? '\0' : '?'));

		if (configIsValid && positionIsValid)
		{
			//DISPLAY_Y_POS_TX_TIMER
			int16_t yPos = (fromSatScreen ? DISPLAY_Y_POS_CONTACT : DISPLAY_Y_POS_TX_TIMER);
			ucFont_t font = (fromSatScreen ? FONT_SIZE_3 : FONT_SIZE_4);
			int16_t fontHeight = (fromSatScreen ? FONT_SIZE_3_HEIGHT : FONT_SIZE_4_HEIGHT);
#if ! defined(HAS_COLOURS)
			int16_t tBorder = (fromSatScreen ? 0 : 6);
			int16_t bBorder = (fromSatScreen ? -1 : 12);
#endif

			displayThemeApply(THEME_ITEM_FG_TX_COUNTER, THEME_ITEM_BG);
#if defined(HAS_COLOURS)
			displayFillRect(0, yPos, DISPLAY_SIZE_X, fontHeight, true);
#else
			displayFillRect(0, (yPos + tBorder), DISPLAY_SIZE_X, (fontHeight - bBorder), true);
#endif
			displayPrintCentered(yPos, buffer, font);
			displayThemeResetToDefault();
			displayRender();
		}
		else
		{
			uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 1500, buffer, true);
			soundSetMelody(MELODY_ERROR_BEEP);
			return false;
		}
	}

	if (fromSatScreen == false)
	{
		// toggle xmit
		switch (aprsBcnData.settings.mode)
		{
			case APRS_BEACONING_MODE_MANUAL:
			case APRS_BEACONING_MODE_AUTO:
			case APRS_BEACONING_MODE_SMART_BEACONING:
				if ((currentChannelData->txFreq != 0) &&
						(codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_RX_ONLY) == 0) &&
						((nonVolatileSettings.txFreqLimited == BAND_LIMITS_NONE) || trxCheckFrequencyInAmateurBand(currentChannelData->txFreq)
#if defined(PLATFORM_MD9600)
								|| (codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_OUT_OF_BAND) != 0)
#endif
				) && aprsBeaconingStateEnabled(APRS_BEACONING_STATE_HAS_APRS_CONFIG))
				{
					rxPowerSavingSetState(ECOPHASE_POWERSAVE_INACTIVE);

#if defined(HAS_GPS)
					if (nonVolatileSettings.gps > GPS_MODE_OFF)
					{
						gpsDataInputStartStop(false);
					}
#endif

					watchdogRun(false);

					// Avoid collision.
					uint32_t waitingForXmit = (APRS_ANTI_COLLISION_MS_MAX / RSSI_NOISE_SAMPLE_PERIOD_PIT);
					while((waitingForXmit > 0) && (LedRead(LED_GREEN) || ((getAudioAmpStatus() & AUDIO_AMP_MODE_RF) != 0)))
					{
						vTaskDelay((RSSI_NOISE_SAMPLE_PERIOD_PIT / portTICK_PERIOD_MS));
						waitingForXmit--;
						trxCheckAnalogSquelch();
					}

					watchdogRun(true);

					if (waitingForXmit == 0U)
					{
#if defined(HAS_GPS)
						if (nonVolatileSettings.gps > GPS_MODE_OFF)
						{
							gpsDataInputStartStop(true);
						}
#endif
						return false;
					}

					LedWrite(LED_GREEN, 0);
					LedWrite(LED_RED, 1);

					HRC6000ClearIsWakingState();
					trxSetTX();

					// TX Delay;
					uint32_t m = ticksGetMillis();
					while((ticksGetMillis() - m) < APRS_XMIT_TX_DELAY)
					{
						vTaskDelay((1 / portTICK_PERIOD_MS));
					}
				}
				else
				{
					return false;
				}
				break;

			default:
				break;

		}
	}

	aprsSendPacket(&aprsBcnData.aprsConfig[(fromSatScreen ? APRS_CONFIG_SATELLITE : APRS_CONFIG_CHANNEL)], &aprsBcnData.previousLocation, &aprsBcnData.currentLocation);

#if defined(RATE_MESSAGE_FEATURE)
	aprsBcnData.rateMessageCount = (aprsBcnData.rateMessageCount + 1) % aprsBcnData.settings.messageInterval;
#endif

	if (anyManualMode)
	{
		if (aprsBcnData.settings.mode == APRS_BEACONING_MODE_PTT)
		{
			ticksTimerStart(&aprsBcnData.nextBeaconTimer, (initialIntervalsInSecs[aprsBcnData.settings.initialInterval] * MILLISECS_PER_SEC));
		}

		memcpy(&aprsBcnData.previousLocation, &aprsBcnData.currentLocation, sizeof(aprsBeaconingLocation_t));
	}

	return true;
}

void aprsBeaconingUpdateConfigurationFromSystemSettings(void)
{
	// Unpack
	aprsBcnData.settings.mode            = ((nonVolatileSettings.aprsBeaconingSettingsPart1[0] >> 29) & 0x7);
	aprsBcnData.settings.smart.lowSpeed  = ((nonVolatileSettings.aprsBeaconingSettingsPart1[0] >> 24) & 0x1F);
	aprsBcnData.settings.smart.fastRate  = ((nonVolatileSettings.aprsBeaconingSettingsPart1[0] >> 16) & 0xFF);
	aprsBcnData.settings.smart.turnSlope = ((nonVolatileSettings.aprsBeaconingSettingsPart1[0] >> 8) & 0xFF);
	aprsBcnData.settings.smart.turnTime  =  (nonVolatileSettings.aprsBeaconingSettingsPart1[0] & 0xFF);

	uint8_t messageInterval = 0;

	messageInterval                      = (((nonVolatileSettings.aprsBeaconingSettingsPart1[1] >> 21) & 0x1F) + APRS_BEACON_MESSAGE_INTERVAL_MIN);
	aprsBcnData.settings.smart.slowRate  = ((nonVolatileSettings.aprsBeaconingSettingsPart1[1] >> 14) & 0x7F);
	aprsBcnData.settings.smart.highSpeed = ((nonVolatileSettings.aprsBeaconingSettingsPart1[1] >> 7) & 0x7F);
	aprsBcnData.settings.smart.turnAngle =  (nonVolatileSettings.aprsBeaconingSettingsPart1[1] & 0x7F);

#if defined(RATE_MESSAGE_FEATURE)
	aprsBcnData.settings.messageInterval = messageInterval;
#else
	(void)messageInterval;
#endif

	aprsBcnData.settings.state = ((nonVolatileSettings.aprsBeaconingSettingsPart2 >> 4) & 0xFF);
	aprsBcnData.settings.initialInterval = (nonVolatileSettings.aprsBeaconingSettingsPart2 & 0xF);
}

void aprsBeaconingUpdateSystemSettingsFromConfiguration(void)
{
	uint8_t messageInterval = APRS_BEACON_MESSAGE_INTERVAL_MIN;
	uint16_t v16;
	uint32_t v32;

	// Pack
	v32 = (((aprsBcnData.settings.mode & 0x7) << 29) | ((aprsBcnData.settings.smart.lowSpeed & 0x1F) << 24) |
			(aprsBcnData.settings.smart.fastRate << 16) | (aprsBcnData.settings.smart.turnSlope << 8) |
			aprsBcnData.settings.smart.turnTime);

	settingsSet(nonVolatileSettings.aprsBeaconingSettingsPart1[0], v32);

#if defined(RATE_MESSAGE_FEATURE)
	messageInterval = (aprsBcnData.settings.messageInterval - APRS_BEACON_MESSAGE_INTERVAL_MIN);
#endif

	v32 = (((messageInterval & 0x1F) << 21) | ((aprsBcnData.settings.smart.slowRate & 0x7F) << 14) |
			((aprsBcnData.settings.smart.highSpeed & 0x7F) << 7) | (aprsBcnData.settings.smart.turnAngle & 0x7F));

	settingsSet(nonVolatileSettings.aprsBeaconingSettingsPart1[1], v32);

	v16 = (((aprsBcnData.settings.state & 0xFF) << 4) | (aprsBcnData.settings.initialInterval & 0xF));

	settingsSet(nonVolatileSettings.aprsBeaconingSettingsPart2, v16);
}

void aprsBeaconingGetSettings(aprsBeaconingSettings_t *dest)
{
	memcpy(dest, &aprsBcnData.settings, sizeof(aprsBeaconingSettings_t));
}

void aprsBeaconingSetSettings(aprsBeaconingSettings_t *src)
{
	if (memcmp(src, &aprsBcnData.settings, sizeof(aprsBeaconingSettings_t)) != 0)
	{
		aprsBeaconingStop();
		memcpy(&aprsBcnData.settings, src, sizeof(aprsBeaconingSettings_t));
		aprsBeaconingUpdateSystemSettingsFromConfiguration();
		aprsBeaconingStart();
	}
}

void aprsBeaconingTick(uiEvent_t *ev)
{
	if (aprsTxProgress != APRS_TX_IDLE)
	{
		aprsBeaconingTxStateTick(ev);
		return;
	}

	int currentMenu = menuSystemGetCurrentMenuNumber();

	// Do not automatically beaconing while in Satellite or TX screens
	if ((currentMenu == MENU_SATELLITE) || (currentMenu == UI_TX_SCREEN) || (currentMenu == UI_HOTSPOT_MODE))
	{
		return;
	}

	if (((uiDataGlobal.Scan.active == false) && (aprsBcnData.settings.mode != APRS_BEACONING_MODE_OFF) && aprsBeaconingStateEnabled(APRS_BEACONING_STATE_ENABLED))
			&& aprsBeaconingStateEnabled(APRS_BEACONING_STATE_HAS_APRS_CONFIG))
	{
		if (ticksTimerHasExpired(&aprsBcnData.checkTimer))
		{
			bool locFromGPS = aprsBeaconingStateEnabled(APRS_BEACONING_STATE_LOCATION_FROM_GPS);
			bool gpsHasPVT = (locFromGPS ? gpsPVTIsValid() : false);
			bool forceEntering = (locFromGPS ? ((isnan(aprsBcnData.fixedLocationLat) == 0) ? false : true) : true); // every second check when GPS is OFF

			if (gpsHasPVT || forceEntering)
			{
				bool gpsFix             = (locFromGPS ? gpsFixIsValid() : true);
				bool checkForBeaconing  = (locFromGPS ? ((isnan(aprsBcnData.fixedLocationLat) == 0) ? gpsFix : true) : true);

				// Reset stored location when the Fix is lost or just acquired
				if (aprsBeaconingStateEnabled(APRS_BEACONING_STATE_LOCATION_FROM_GPS))
				{
					if (((gpsFix == false) && aprsBeaconingLocationIsValid(&aprsBcnData.previousLocation)) ||
							(gpsFix && (aprsBeaconingStateEnabled(APRS_BEACONING_STATE_HAS_GPS_FIX) == false)))
					{
						aprsBeaconingInvalidateLocation(&aprsBcnData.previousLocation);
						aprsBeaconingStateSetEnable(APRS_BEACONING_STATE_HAS_GPS_FIX, gpsFix);
					}
				}

				if (checkForBeaconing)
				{
					aprsBcnData.currentLocation.time = ticksGetMillis();

					if (locFromGPS)
					{
						aprsBcnData.currentLocation.coords.latitude  =
#if defined(HAS_GPS)
								gpsFix ? gpsData.LatitudeHiRes :
#endif
										aprsGetFixedPositionLatitude();

						aprsBcnData.currentLocation.coords.longitude =
#if defined(HAS_GPS)
								gpsFix ? gpsData.LongitudeHiRes :
#endif
										aprsGetFixedPositionLongitude();

						aprsBcnData.currentLocation.bearing          =
#if defined(HAS_GPS)
#if defined(APRS_USE_COURSETO_FOR_BEARING)
								(gpsFix && (gpsData.SpeedInHundredthKn > SMART_BEACONING_SPEED_MIN)) ? aprsBeaconingGetBearing() :
#else
								(gpsFix && (gpsData.SpeedInHundredthKn > SMART_BEACONING_SPEED_MIN)) ? gpsData.CourseInHundredthDeg :
#endif
#endif
										0U;
						aprsBcnData.currentLocation.speed            =
#if defined(HAS_GPS)
								(gpsFix && (gpsData.SpeedInHundredthKn > GPS_SPEED_THRESHOLD_MIN)) ? gpsData.SpeedInHundredthKn :
#endif
										0U;
					}
					else
					{
						aprsBcnData.currentLocation.coords.latitude  = aprsGetFixedPositionLatitude();
						aprsBcnData.currentLocation.coords.longitude = aprsGetFixedPositionLongitude();
						aprsBcnData.currentLocation.bearing          = 0U;
						aprsBcnData.currentLocation.speed            = 0U; // Fixed position
					}

					// No position is usable (GPS/Channel and settings ones), don't do anything for now.
					if (isnan(aprsBcnData.currentLocation.coords.latitude) != 0)
					{
						aprsBeaconingInvalidateLocation(&aprsBcnData.currentLocation);
						aprsBeaconingInvalidateLocation(&aprsBcnData.previousLocation);
						aprsBeaconingInvalidateFixedPosition();
						goto exitRearmTimer;
					}

					if (aprsBcnData.triggerBeaconing == false)
					{
						if (aprsBcnData.settings.mode == APRS_BEACONING_MODE_SMART_BEACONING)
						{
							if (aprsSmartBeaconingCheck())
							{
								aprsBcnData.triggerBeaconing = true;
							}
						}
						else if ((aprsBcnData.settings.mode == APRS_BEACONING_MODE_AUTO) && ticksTimerHasExpired(&aprsBcnData.nextBeaconTimer))
						{
							aprsBcnData.triggerBeaconing = true;
						}
					}
				}
			}

			if (aprsBcnData.triggerBeaconing)
			{
				bool beaconSent = false;

				// in MANUAL and PTT modes, we just need the updated position (even on fixed one, as aprsBeaconingResetTimers() also invalidate positions).
				if ((aprsBcnData.settings.mode == APRS_BEACONING_MODE_AUTO) || (aprsBcnData.settings.mode == APRS_BEACONING_MODE_SMART_BEACONING))
				{
					beaconSent = aprsBeaconingSendBeacon(false);

					if (beaconSent && (aprsBcnData.settings.mode != APRS_BEACONING_MODE_SMART_BEACONING) )// Pure time driven
					{
						double dist = 0.0;

						if (aprsBeaconingLocationIsValid(&aprsBcnData.currentLocation) && aprsBeaconingLocationIsValid(&aprsBcnData.previousLocation))
						{
							dist = DistanceBetweenTwoCoords(aprsBcnData.currentLocation.coords.latitude, aprsBcnData.currentLocation.coords.longitude, aprsBcnData.previousLocation.coords.latitude, aprsBcnData.previousLocation.coords.longitude);
						}

						ticksTimerStart(&aprsBcnData.nextBeaconTimer, ((initialIntervalsInSecs[aprsBcnData.settings.initialInterval] * MILLISECS_PER_SEC) * aprsBcnData.decayMult));

						// Reset the Decay mult if we moved outside a radius of APRS_BEACON_DECAY_RESET_DISTANCE_MIN meters.
						aprsBeaconingDecayTick((dist > APRS_BEACON_DECAY_RESET_DISTANCE_MIN));
					}
				}

				// store current position as previous one for the next run
				if (beaconSent)
				{
					aprsBcnData.triggerBeaconing = false;
					memcpy(&aprsBcnData.previousLocation, &aprsBcnData.currentLocation, sizeof(aprsBeaconingLocation_t));
				}
			}

			exitRearmTimer:
			ticksTimerStart(&aprsBcnData.checkTimer, MILLISECS_PER_SEC); // 1s timer
		}
	}
}
#endif // PLATFORM_GD77S
