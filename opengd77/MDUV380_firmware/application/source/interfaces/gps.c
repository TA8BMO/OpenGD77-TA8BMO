/*
 * Copyright (C) 2021-2024 Roger Clark, VK3KYY / G4KYF
 *                         Colin Durbridge, G4EML
 *                         Daniel Caujolle-Bert, F1RMB
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
#include "main.h"
#include "user_interface/uiGlobals.h"
#include "user_interface/uiUtilities.h"
#include "interfaces/gps.h"
#include "user_interface/uiLocalisation.h"
#include "usb/usb_com.h"
#if defined(PLATFORM_MD9600)
#include "interfaces/remoteHead.h"
#endif

#if defined(HAS_GPS)

#define GPS_RX_BUFFERS_MAX                  3U

#if defined(LOG_GPS_DATA)
#define LOG_RAM_BUF_SIZE                 4096U

static
#if ! (CPU_MK22FN512VLL12) // Doesn't fit on DM1801
  __attribute__((section(".data.$RAM2")))
#endif
  uint8_t NMEARecordingBuffer[LOG_RAM_BUF_SIZE];

#define LOG_FLASH_16MB_START_ADDRESS  (14 * 1024 * 1024) // Last 2MB
// The receive recorder has the top of the flash; with the track log enabled it
// keeps only the top 1MB and the log gets the 1MB below (settings.h).
#if GPS_TRACK_LOG_TO_FLASH
#define LOG_FLASH_16MB_MEM_SIZE        (1 * 1024 * 1024)
#else
#define LOG_FLASH_16MB_MEM_SIZE        (2 * 1024 * 1024)
#endif
#if defined(CPU_MK22FN512VLL12)
#define LOG_FLASH_2MB_START_ADDRESS    (1 * 1024 * 1024) // Last 1MB
#define LOG_FLASH_2MB_MEM_SIZE         (1 * 1024 * 1024)
#define LOG_FLASH_1MB_START_ADDRESS    DMRID_MEMORY_LOCATION_2 // DMRId section 2
#define LOG_FLASH_1MB_MEM_SIZE        ((1 * 1024 * 1024) - DMRID_MEMORY_LOCATION_2) // Last 288k
#endif // CPU_MK22FN512VLL12

static uint32_t gpsLogFlashStartAddress = 0U;
static uint32_t gpsLogFlashMemSize = 0U;
static uint32_t gpsLogMemOffset = 0U;
static bool gpsIsLogging = false;
#endif // LOG_GPS_DATA

typedef struct
{
	uint8_t data[GPS_LINE_LENGTH];
	uint8_t length;
} gpsReceiveBuffer_t;

typedef struct
{
	gpsReceiveBuffer_t rxBuffers[GPS_RX_BUFFERS_MAX];
	uint8_t linesCount;
	uint8_t bufferIndex;
	uint8_t charPosition;
} gpsReceiveData_t;

gpsData_t gpsData =
{
		.Status = (GPS_STATUS_FIX_UPDATED | GPS_STATUS_FIXTYPE_UPDATED),
		.Latitude = 0U,
		.LatitudeHiRes = 0.0,
		.Longitude = 0U,
		.LongitudeHiRes = 0.0,
		.SatsInViewGP = 0U,
		.SatsInViewBD = 0U,
		.currentGPSIndex = 0U,
		.currentBDIndex = 0U,
		.Time = 0U,
		.AccuracyInCm = 0U,
		.HeightInM = 0U,
};


#if defined(CPU_MK22FN512VLL12)
static volatile gpsReceiveData_t gpsRxData;
#else
static gpsReceiveData_t gpsRxData;
#endif


static uint8_t gpsBufferIndexProcessing = 0U;
#if defined(STM32F405xx)
static uint8_t gpsDMABuf[GPS_DMA_BUFFER_SIZE]; // double buffer (two halves) for GPS UART DMA receive
#endif

//
// Grace number of GSV invalid frames (in GSV) before setting the fix lost in the Status.
// On the MD-9600, from time to time a bunch if invalid frames ('V') are send by the GPS module.
// This prevents to get some "Fix lost" -> "Fix acquired" sequences.
#define GPS_FIX_GRACE_MAX        15U
static uint8_t gpsFixGraceCount = 0U;

//#define GNSS_MULTI_GSV 1 // Uncomment this to support GB, GA and GL GSVs sentences (not with genuine GPS modules).
#define USE_CHECKSUM 1
#if USE_CHECKSUM
static uint16_t gpsLineChecksum = 0xDEAD;
#else
static char gpsLastLine[GPS_LINE_LENGTH];
#endif

//#define USE_DUMMY_GPS_DATA
#ifdef USE_DUMMY_GPS_DATA
const char *DUMMY_GPS_DATA[] = {
		"$GNZDA,074101.000,20,09,2022,,*42",
		"$GNGGA,074102.000,3858.1,N,14602.1,E,1,03,4.38,51.4,M,-1.5,M,,*4F",
		"$GPGSA,A,2,29,25,12,,,,,,,,,,4.49,4.38,1.00,1*16",
		"$BDGSA,A,2,,,,,,,,,,,,,4.49,4.38,1.00,4*0D",
		"$GPGSV,2,1,07,29,63,203,24,2,62,148,,25,61,339,24,20,33,129,*48",
		"$GPGSV,2,2,07,12,32,18,25,26,7,220,,23,1,340,*76",
		"$BDGSV,1,1,04,1,47,0,,4,44,22,,3,34,313,,2,12,289,*5D",
		"$GNRMC,074102.000,A,3758.10000,N,14502.10000,E,5.000,275.00,200922,,,A*53",
		"$GNZDA,074101.000,20,09,2022,,*42",
		"$GNGGA,074102.000,3858.10000,S,14502.10000,E,1,03,4.38,51.4,M,-1.5,M,,*4F",
		"$GPGSA,A,2,29,25,12,,,,,,,,,,4.49,4.38,1.00,1*16",
		"$BDGSA,A,2,,,,,,,,,,,,,4.49,4.38,1.00,4*0D",
		"$GPGSV,2,1,07,29,63,203,24,2,62,148,,25,61,339,24,20,33,129,*48",
		"$GPGSV,2,2,07,12,32,18,25,26,7,220,,23,1,340,*76",
		"$BDGSV,1,1,04,1,47,0,,4,44,22,,3,34,313,,2,12,289,*5D",
		"$GNRMC,074102.000,A,3758.00000,N,14502.00000,E,5.000,275.00,200922,,,A*53"
};
int32_t dummyGpsDataIndex = 0;
#endif

#if defined(CPU_MK22FN512VLL12)
#include "fsl_uart.h"

static bool gpsIrqIsEnabled = false;

extern double round(double); // implementation in aprs.c
#endif

#if defined(LOG_GPS_DATA)
static void gpsLogNMEAData(const char *nmea, uint8_t length);
#endif


void gpsInit(void)
{
	memset((gpsReceiveData_t *)&gpsRxData, 0, sizeof(gpsReceiveData_t));
	gpsBufferIndexProcessing = 0U;

#if defined(LOG_GPS_DATA)

#if defined(STM32F405xx)
	gpsLogFlashStartAddress = LOG_FLASH_16MB_START_ADDRESS;
	gpsLogFlashMemSize = LOG_FLASH_16MB_MEM_SIZE;
#else
	switch (flashChipPartNumber)
	{
		case 0x4015: // 4015 25Q16   16M-bits  2M-bytes, used in the Baofeng DM-1801 ?
		case 0x4017: // 4017 25Q64   64M-bits  8M-bytes, used in Roger's special GD-77 radios modified on the TYT production line.
			gpsLogFlashStartAddress = LOG_FLASH_2MB_START_ADDRESS;
			gpsLogFlashMemSize = LOG_FLASH_2MB_MEM_SIZE;
			break;

		case 0x4018: // 4018 25Q128 128M-bits 16M-bytes, used in MD9600 and Daniel's modified GD-77.
		case 0x7018: // 7018 25Q128JV 128M-bits 16M-bytes, KI5GZK's modified GD-77.
			gpsLogFlashStartAddress = LOG_FLASH_16MB_START_ADDRESS;
			gpsLogFlashMemSize = LOG_FLASH_16MB_MEM_SIZE;
			break;

		case 0x4014: // 4014 25Q80    8M-bits  1M-bytes, used in the GD-77.
		default:
			if (voicePromptDataIsLoaded)
			{
				gpsLogFlashStartAddress = LOG_FLASH_1MB_START_ADDRESS;
				gpsLogFlashMemSize = LOG_FLASH_1MB_MEM_SIZE;
			}
			else
			{
				gpsLogFlashStartAddress = DMRID_MEMORY_LOCATION_1;
				gpsLogFlashMemSize = ((1 * 1024 * 1024) - DMRID_MEMORY_LOCATION_1); // last 832k
			}
			break;
	}
#endif

#endif // LOG_GPS_DATA

#if defined(CPU_MK22FN512VLL12)
	uart_config_t config;

	UART_GetDefaultConfig(&config);
	config.baudRate_Bps = 9600;
	config.enableTx     = false;
	config.enableRx     = true;
	UART_Init(UART0, &config, CLOCK_GetFreq(SYS_CLK));
	UART_EnableRxFIFO(UART0, true);
	UART_EnableInterrupts(UART0, kUART_RxDataRegFullInterruptEnable | kUART_RxOverrunInterruptEnable);
	gpsOff();
#endif
}

#if defined(CPU_MK22FN512VLL12)
void UART0_RX_TX_IRQHandler (void)
{
	uint32_t uartFlags = UART_GetStatusFlags(UART0);

	while ((kUART_RxDataRegFullFlag | kUART_RxOverrunFlag) & uartFlags)
	{
		gpsProcessChar(UART_ReadByte(UART0));
		uartFlags = UART_GetStatusFlags(UART0);
	};
	SDK_ISR_EXIT_BARRIER;
}
#endif

// Converts the GPS data from the format DDDmm.mmmm into our custom format Int<<23 + frac*1000
static uint32_t gpsLatLongConvert(const char *input, double *dValue)
{
	double value = strtod(input, NULL);
	double degrees = (int32_t)(value * 1E-2); // get degrees

	value -= (degrees * 1E2);
	degrees += (value / 60.0);

	*dValue = degrees;

	return ((((uint32_t)degrees) << 23) + ((uint32_t)(round(((degrees - (uint32_t)degrees) * 1E5)))));
}

static time_t_custom gpsTimeConvert(const char *time, const char *date)
{
	struct tm gpsDateTime;
	uint8_t Date;
	uint8_t Month;
	uint8_t Year;
	uint8_t Hours;
	uint8_t Minutes;
	uint8_t Seconds;

	if (strlen(date) == 6)
	{
		Year = ((date[4] - '0') * 10) + (date[5] - '0');
		Month = ((date[2] - '0') * 10) + (date[3] - '0');
		Date = ((date[0] - '0') * 10) + (date[1] - '0');
	}
	else
	{
		Year = 21;
		Month = 12;
		Date = 25;
	}

	if (Year < 70)
	{
		Year = Year + 100;
	}

	if (strlen(time) >= 6)
	{
		Seconds = ((time[4] - '0') * 10) + (time[5] - '0');
		Minutes = ((time[2] - '0') * 10) + (time[3] - '0');
		Hours = ((time[0] - '0') * 10) + (time[1] - '0');
	}
	else
	{
		Seconds = 0;
		Minutes = 0;
		Hours = 0;
	}

	memset(&gpsDateTime, 0x00, sizeof(struct tm)); // clear entire struct
	gpsDateTime.tm_mday = Date;          /* day of the month, 1 to 31 */
	gpsDateTime.tm_mon = Month - 1;      /* months since January, 0 to 11 */
	gpsDateTime.tm_year = Year;          /* years since 1900 */
	gpsDateTime.tm_hour = Hours;
	gpsDateTime.tm_min = Minutes;
	gpsDateTime.tm_sec = Seconds;

	return mktime_custom(&gpsDateTime);
}

static void getParam(const char *line, char *result, int entryno, int reslen)
{
	int count = 0;
	int respoint = 0;

	for (size_t i = 0; i < strlen(line); i++)
	{
		if (line[i] == ',')
		{
			count++;
			if (count == entryno)
			{
				result[respoint] = 0;
				return;
			}
			else
			{
				respoint = 0;
			}
		}
		else
		{
			result[respoint++] = line[i];
			if (respoint > (reslen - 1))
			{
				result[0] = 0;
				return;
			}
		}
	}

	result[0] = 0;
}

#if !(defined(PLATFORM_MD9600) || defined(CPU_MK22FN512VLL12))
static
#endif
void gpsProcessChar(uint8_t rxchar)
{
	if ((rxchar != '\r') && (gpsRxData.charPosition < GPS_LINE_LENGTH))
	{
		if (rxchar >= '!') // Ignore '\n'
		{
			gpsRxData.rxBuffers[gpsRxData.bufferIndex].data[gpsRxData.charPosition++] = rxchar;
		}
	}
	else
	{
		gpsRxData.rxBuffers[gpsRxData.bufferIndex].data[gpsRxData.charPosition] = 0;
		gpsRxData.rxBuffers[gpsRxData.bufferIndex].length = gpsRxData.charPosition;
		gpsRxData.linesCount++;
		gpsRxData.bufferIndex = (gpsRxData.bufferIndex + 1) % GPS_RX_BUFFERS_MAX;
		gpsRxData.rxBuffers[gpsRxData.bufferIndex].length = 0U;
		gpsRxData.charPosition = 0U;
	}
}

void gpsDataInputStartStop(bool enable)
{
#if defined(STM32F405xx)
	if (enable)
	{
		HAL_UART_Receive_DMA(
#if defined(PLATFORM_MD9600) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
				&huart1
#elif defined(PLATFORM_MD380)
				&huart3
#endif
				, gpsDMABuf, GPS_DMA_BUFFER_SIZE);

	}
	else
	{
		HAL_UART_DMAStop(
#if defined(PLATFORM_MD9600) || defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
				&huart1
#elif defined(PLATFORM_MD380)
				&huart3
#endif
		);
	}
#elif defined(CPU_MK22FN512VLL12)
	if (enable)
	{
		EnableIRQ(UART0_RX_TX_IRQn);
	}
	else
	{
		DisableIRQ(UART0_RX_TX_IRQn);
	}

	gpsIrqIsEnabled = enable;
#endif // STM32F405xx

	memset((gpsReceiveData_t *)&gpsRxData, 0, sizeof(gpsReceiveData_t));
	gpsBufferIndexProcessing = 0U;
}

void gpsOn(void)
{
	memset((uint8_t *)&gpsData, 0x00, sizeof(gpsData_t));// reset everything
	gpsFixGraceCount = 0U;
	gpsData.Status |= (GPS_STATUS_FIX_UPDATED | GPS_STATUS_FIXTYPE_UPDATED);
#if ! defined(PLATFORM_MD9600)
	gpsPower(true);			//Turn on the power to the GPS module, except on MD9600 where it also powers the mic so is turned on all the time in application main
#endif

#if defined(PLATFORM_MD9600)
	if(remoteHeadActive)
	{
		remoteHeadGpsDataInputStartStop(true);
	}
	else
#endif
	{
		gpsDataInputStartStop(true);
	}
}

void gpsOff(void)
{
#if ! defined(PLATFORM_MD9600)
	gpsPower(false);		//Turn off the power to the GPS module except on MD9600 where the mic and GPS are powered at the same time.
#endif

#if defined(PLATFORM_MD9600)
	if(remoteHeadActive)
	{
		remoteHeadGpsDataInputStartStop(false);
	}
	else
#endif
	{
		gpsDataInputStartStop(false);
	}
	memset((uint8_t *)&gpsData, 0x00, sizeof(gpsData_t));// reset everything
	gpsFixGraceCount = 0U;
	gpsData.Status |= (GPS_STATUS_FIX_UPDATED | GPS_STATUS_FIXTYPE_UPDATED);
}

#if defined(STM32F405xx)
void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *huart)
{
#if defined(PLATFORM_MD9600)
	if(huart == &huart3)
	{
		remoteHead_UART_RxHalfCpltCallback();
	}
	else
#endif
	{
#if defined(PLATFORM_MD9600)
		// GPS module power is never turned off on this platform, so we must ignore any data from it.
		if (nonVolatileSettings.gps == GPS_MODE_OFF)
		{
			return;
		}
#endif

		for (size_t i = 0; i < 32; i++)
		{
			gpsProcessChar(gpsDMABuf[i]);
		}
	}
}
#endif // STM32F405xx

void gpsOnUsingQuickKey(bool on)
{
	uiEvent_t e = { .buttons = BUTTON_NONE, .keys = NO_KEYCODE, .rotary = 0, .events = FUNCTION_EVENT, .hasEvent = true, .time = ticksGetMillis() };
	bool sendEvent = false;

	// Use QuickKey to change GPS power status
	if (on && (nonVolatileSettings.gps > GPS_NOT_DETECTED) && (nonVolatileSettings.gps < (NUM_GPS_MODES - 1)))
	{
		e.function = QUICKKEY_MENUVALUE(MENU_GENERAL, MENU_GENERAL_OPTIONS_GPS_ENTRY_NUMBER, FUNC_RIGHT);
		sendEvent = true;
	}
	else if((on == false) && (nonVolatileSettings.gps > GPS_MODE_OFF))
	{
		e.function = QUICKKEY_MENUVALUE(MENU_GENERAL, MENU_GENERAL_OPTIONS_GPS_ENTRY_NUMBER, FUNC_LEFT);
		sendEvent = true;
	}

	if (sendEvent)
	{
		menuSystemPushNewMenu(MENU_GENERAL);
		menuSystemCallCurrentMenuTick(&e);
	}

}

#if defined(STM32F405xx)
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
#if defined(PLATFORM_MD9600)
	if(huart == &huart3)
	{
		remoteHead_UART_RxCpltCallback();
	}
	else
#endif
	{
		/* Debugging only
		 HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
		 */

#if defined(PLATFORM_MD9600)
		// GPS module power is never turned off on this platform, so we must ignore any data from it.
		if (nonVolatileSettings.gps == GPS_MODE_OFF)
		{
			return;
		}
#endif

		for (size_t i = 0; i < 32; i++)
		{
			gpsProcessChar(gpsDMABuf[(GPS_DMA_BUFFER_SIZE / 2) + i]);
		}
	}
}
#endif // STM32F405xx


#if defined(PLATFORM_MD9600)
//turn the GPS on and off. Note this also turns the Microphone power on and off, so the GPS must always be On when the radio is in use.
void gpsAndMicPower(bool on)
{
	if (on)
	{
		GPIOA->MODER = (GPIOA->MODER & ~GPIO_MODER_MODER9_Msk) | GPIO_MODER_MODER9_0;     //Set the GPIOA Pin 9 to GPIO mode
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_SET);                               //set it high
	}
	else
	{
		GPIOA->MODER = (GPIOA->MODER & ~GPIO_MODER_MODER9_Msk) | GPIO_MODER_MODER9_0;     // Set the GPIOA Pin 9 to GPIO mode
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_RESET);                             //set it Low
	}
}
#endif

void gpsPower(bool on)
{
#if defined(STM32F405xx)
	if (on)
	{
#if defined(PLATFORM_MD380)
		GPIOD->MODER = (GPIOD->MODER & ~GPIO_MODER_MODER8_Msk) | GPIO_MODER_MODER8_0;     //Set the GPIOD Pin 8 to GPIO mode
		HAL_GPIO_WritePin(GPIOD, GPIO_PIN_8, GPIO_PIN_SET);                               //set it high
#else // PLATFORM_MD380

#if defined(PLATFORM_MD9600)
		if(remoteHeadActive)
		{
			remoteHeadGpsPower(true);                   //G4EML... if remote head is fitted then send command to power on the GPS/Mic
		}                                               //G4EML... always also use the hardware power control as the mic might be locally connected.
#endif // PLATFORM_MD9600

		GPIOA->MODER = (GPIOA->MODER & ~GPIO_MODER_MODER9_Msk) | GPIO_MODER_MODER9_0;     //Set the GPIOA Pin 9 to GPIO mode
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_SET);                               //set it high
#endif // PLATFORM_MD380
	}
	else
	{
#if defined(PLATFORM_MD380)
		GPIOD->MODER = (GPIOD->MODER & ~GPIO_MODER_MODER8_Msk) | GPIO_MODER_MODER8_0;     // Set the GPIOD Pin 8 to GPIO mode
		HAL_GPIO_WritePin(GPIOD, GPIO_PIN_8, GPIO_PIN_RESET);                             //set it Low
#else // PLATFORM_MD380

#if defined(PLATFORM_MD9600)
		if(remoteHeadActive)
		{
			remoteHeadGpsPower(false);              //G4EML... if remote head is fitted then send command to power on the GPS/Mic
		}                                           //G4EML... always also use the hardware power control as the mic might be locally connected.
#endif // PLATFORM_MD9600

		GPIOA->MODER = (GPIOA->MODER & ~GPIO_MODER_MODER9_Msk) | GPIO_MODER_MODER9_0;     // Set the GPIOA Pin 9 to GPIO mode
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_RESET);                             //set it Low
#endif // PLATFORM_MD380
	}
#elif defined(CPU_MK22FN512VLL12)
	GPIO_PinWrite(GPIO_GPS_Power, Pin_GPS_Power, (on ? 1U : 0U));
#endif // STM32F405xx
}

static bool jumpToNextField(char **pField, uint8_t *curLength)
{
	// Searching end of the current field.
	while (**pField != '\0' && (*curLength > 0))
	{
		(*pField)++;
		(*curLength)--;
	}

	// Another field exists, position *pField on it.
	if (*curLength > 0)
	{
		(*pField)++;
		(*curLength)--;

		return true;
	}

	return false;
}

static void convertCommasToDelimiters(char *line, uint8_t len)
{
	for (uint8_t i = 0; i < len ; i++)
	{
		if ((line[i] == ',') || (line[i] == '*'))
		{
			line[i] = '\0';
		}
	}
}

static int getNmeaInt(const char *str)
{
	if (*str != '\0')
	{
		return atoi(str);
	}

	return -1;
}

#if defined(GNSS_MULTI_GSV)
static bool populateSatelliteData(char **NMEA, uint8_t *curLength, gpsSatellitesData_t *sat, uint8_t *counter, bool *isDifferent, int8_t sub)
#else
static bool populateSatelliteData(char **NMEA, uint8_t *curLength, gpsSatellitesData_t *sat, uint8_t *counter, bool *isDifferent)
#endif
{
	gpsSatellitesData_t pSat;
	int prn;

	memcpy(&pSat, sat, sizeof(gpsSatellitesData_t));

	prn = getNmeaInt(*NMEA);
	sat->Number =
#if defined(GNSS_MULTI_GSV)
			((sub && (prn > sub)) ? (prn - sub) : prn);
#else
			prn;
#endif
	jumpToNextField(NMEA, curLength);

	sat->El = getNmeaInt(*NMEA);
	jumpToNextField(NMEA, curLength);

	sat->Az = getNmeaInt(*NMEA);
	jumpToNextField(NMEA, curLength);

	sat->RSSI = getNmeaInt(*NMEA);

	(*counter)++;

	*isDifferent = (memcmp(&pSat, sat, sizeof(gpsSatellitesData_t)) != 0);

	return ((*counter < GPS_STORAGE_MAX) && jumpToNextField(NMEA, curLength));
}

#if defined(GNSS_MULTI_GSV)
static uint16_t processGSV(char *line, uint8_t lineLength, gpsSatellitesData_t *satsStorage, uint8_t *counter, bool *satsAreDifferents, int8_t sub)
#else
static uint16_t processGSV(char *line, uint8_t lineLength, gpsSatellitesData_t *satsStorage, uint8_t *counter, bool *satsAreDifferents)
#endif
{
	uint16_t totalSatsInView = 0;

	if (lineLength > 7)
	{
		int msgNumber;
		bool satIsDifferent;
		char *pos = &line[7]; // skip message header

		convertCommasToDelimiters(line, lineLength);

		lineLength -= 7; // take care of header skipping

		// Skip total messages
		jumpToNextField(&pos, &lineLength);
		msgNumber = atoi(pos);

		if (msgNumber == 1)
		{
			// Reset storage counter
			*counter = 0;
		}

		jumpToNextField(&pos, &lineLength);
		totalSatsInView = atoi(pos);

		jumpToNextField(&pos, &lineLength);

		while(*counter < GPS_STORAGE_MAX)
		{
#if defined(GNSS_MULTI_GSV)
			bool res = (populateSatelliteData(&pos, &lineLength, (satsStorage + *counter), counter, &satIsDifferent, sub) && (lineLength > 4));
#else
			bool res = (populateSatelliteData(&pos, &lineLength, (satsStorage + *counter), counter, &satIsDifferent) && (lineLength > 4));
#endif

			*satsAreDifferents |= satIsDifferent;

			if (!res) // we have to stop the parsing here
			{
				break;
			}

		}
	}

	return totalSatsInView;
}

#if USE_CHECKSUM
// https://en.wikipedia.org/wiki/Fletcher%27s_checksum
// Slightly modified, as length will never be > GPS_LINE_LENGTH
static uint16_t fletcher16(const uint8_t *data, uint8_t len)
{
	uint32_t c0 = 0;
	uint32_t c1 = 0;

	do
	{
		c0 = c0 + *data++;
		c1 = c1 + c0;
	} while (--len);

	c0 = c0 % 255;
	c1 = c1 % 255;

   return (c1 << 8 | c0);
}
#endif

void gpsTick(void)
{
	char gpsLine[GPS_LINE_LENGTH] = { 0 };
	char param[6][20];
	char line[20];
	char *p;
	uint8_t lineLength = 0U;

	if ((menuSystemGetCurrentMenuNumber() != UI_TX_SCREEN) &&
			(nonVolatileSettings.gps >= GPS_MODE_OFF) &&
			((ticksGetMillis() % 500) == 0)
#if defined(STM32F405xx)
			&& (HAL_DMA_GetState(&hdma_usart1_rx) != HAL_DMA_STATE_BUSY)
#elif defined(CPU_MK22FN512VLL12)
			&& (gpsIrqIsEnabled == false)
#endif
	)
	{
		gpsDataInputStartStop(true);
	}

	if (gpsRxData.linesCount > 0U)
	{
		lineLength = gpsRxData.rxBuffers[gpsBufferIndexProcessing].length;
		memcpy(gpsLine, (uint8_t *)&gpsRxData.rxBuffers[gpsBufferIndexProcessing].data[0], (lineLength + 1));
		gpsRxData.linesCount--;
		gpsBufferIndexProcessing = (gpsBufferIndexProcessing + 1) % GPS_RX_BUFFERS_MAX;

		if (nonVolatileSettings.gps == GPS_NOT_DETECTED)
		{
			nonVolatileSettings.gps = GPS_MODE_OFF;
			gpsOff();
			return;
		}

#ifdef USE_DUMMY_GPS_DATA
		strcpy(gpsLine, DUMMY_GPS_DATA[dummyGpsDataIndex % 16]);
		dummyGpsDataIndex++;
#endif

#if USE_CHECKSUM
		uint16_t checksum = fletcher16((const uint8_t *)gpsLine, lineLength);
		if (checksum != gpsLineChecksum) // New line
		{
			gpsLineChecksum = checksum; // Store new checksum
#else
		if (memcmp(gpsLine, gpsLastLine, lineLength) != 0) // New line
		{
			memcpy(gpsLastLine, gpsLine, GPS_LINE_LENGTH); // backup last gps line
#endif

			//gpsData.MessageCount++;

			if (gpsLine[0] == '$')
			{
				if (nonVolatileSettings.gps >= GPS_MODE_ON_NMEA)
				{
					USB_DEBUG_printf("%s\r\n", gpsLine);// Note. NMEA protocol requires CR LF

#if defined(LOG_GPS_DATA)
					// log everything once per minute
					if ((gpsData.Time % 60) == 0)
					{
						gpsLogNMEAData(gpsLine, lineLength);
					}
#endif
				}

				if (memcmp(&gpsLine[3], "GGA", 3) == 0)// message that contains accuracy (HDOP) and altitude
				{
					getParam(gpsLine, line, 9, 20);// get accuracy (HDOP)

					uint16_t hdop = (uint16_t)((strtod(line, NULL)) * 1E2);
					if (hdop != gpsData.AccuracyInCm)
					{
						gpsData.AccuracyInCm = hdop;
						gpsData.Status |= (GPS_STATUS_HDOP_UPDATED | GPS_STATUS_HAS_HDOP);
					}

					getParam(gpsLine, line, 10, 20);// get height
					p = strchr(line, '.');
					if (p != NULL)
					{
						*p = '\0';
					}

					int16_t height = atoi(line);
					if (height != gpsData.HeightInM)
					{
						gpsData.HeightInM = height;
						gpsData.Status |= (GPS_STATUS_HEIGHT_UPDATED | GPS_STATUS_HAS_HEIGHT);
					}
				}
				else if (memcmp(&gpsLine[3], "RMC", 3) == 0)			//is this the LAT Long and Time Message?
				{
					char statusLetter[20];
					int currentMenu = menuSystemGetCurrentMenuNumber();

#if defined(LOG_GPS_DATA)
					if ((gpsData.Time % 60) != 0)
					{
						gpsLogNMEAData(gpsLine, lineLength);
					}
#endif
					// check if it has the date and time.
					getParam(gpsLine, param[0], 2, 20);			//get parameter 2 which is GMT Time as hhmmss.sss
					getParam(gpsLine, statusLetter, 3, 20);
					getParam(gpsLine, param[5], 10, 20);	    	//get parameter 10 which is Date as ddmmyy

					if ((param[0][0] != 0) && (param[5][0] != 0))
					{
						gpsData.Time = gpsTimeConvert(param[0], param[5]);

						// Clock skew ?
						if ((statusLetter[0] == 'A') /* GPS time valid (2D fix is enough); no 3D needed, so the clock resyncs whenever GPS locks, even after the battery was out */ &&
								(abs(uiDataGlobal.dateTimeSecs - gpsData.Time) > 5))
						{
							uiSetUTCDateTimeInSecs(gpsData.Time);
#if defined(STM32F405xx)
							setRtc_custom(uiDataGlobal.dateTimeSecs);
#endif
							// Update Satellite screen (re-enter)
							bool restartSatMenu = (currentMenu == MENU_SATELLITE);
							if (restartSatMenu)
							{
								menuDataGlobal.currentItemIndex = 0; // will restart in prediction list
								menuSystemPopPreviousMenu();
								menuSatelliteSetFullReload();
							}

							menuSatelliteScreenClearPredictions(false);

							if (restartSatMenu)
							{
								menuSystemPushNewMenu(MENU_SATELLITE);
							}
						}

						gpsData.Status |= (GPS_STATUS_TIME_UPDATED | GPS_STATUS_HAS_TIME);
					}

					// Have a fix
					//
					if (statusLetter[0] == 'A')
					{
						gpsFixGraceCount = GPS_FIX_GRACE_MAX;

						if ((gpsData.Status & GPS_STATUS_HAS_FIX) == 0)
						{
							gpsData.Status |= (GPS_STATUS_HAS_FIX | GPS_STATUS_FIX_UPDATED);
						}
					}
					else // Have no fix
					{
						if (gpsFixGraceCount > 0U)
						{
							gpsFixGraceCount--;
						}
						else
						{
							// Clear fix type status
							if (gpsData.Status & (GPS_STATUS_2D_FIX | GPS_STATUS_3D_FIX))
							{
								gpsData.Status &= ~(GPS_STATUS_2D_FIX | GPS_STATUS_3D_FIX);
								gpsData.Status |= GPS_STATUS_FIXTYPE_UPDATED;
							}

							// Loosing fix status
							if (gpsData.Status & GPS_STATUS_HAS_FIX)
							{
								gpsData.Status &= ~(GPS_STATUS_HAS_FIX | GPS_STATUS_HAS_POSITION | GPS_STATUS_HAS_HDOP | GPS_STATUS_HAS_COURSE | GPS_STATUS_HAS_SPEED | GPS_STATUS_HAS_HEIGHT | GPS_STATUS_HAS_TIME);
								gpsData.Status |= GPS_STATUS_FIX_UPDATED;
							}
						}

						return;
					}

					getParam(gpsLine, param[1] , 4, 20);			//get parameter 4 which is Latitude a ddmm.mmmm
					getParam(gpsLine, param[2] , 5, 20);			//get parameter 5 which is N/S
					getParam(gpsLine, param[3] , 6, 20);			//get parameter 6 which is Longitude a dddmm.mmmm
					getParam(gpsLine, param[4] , 7, 20);			//get parameter 7 which is E/W

					gpsData.Latitude = gpsLatLongConvert(param[1], &gpsData.LatitudeHiRes);

					if (param[2][0] == 'S')
					{
						gpsData.Latitude = gpsData.Latitude | 0x80000000;
						gpsData.LatitudeHiRes = -gpsData.LatitudeHiRes;
					}

					gpsData.Longitude = gpsLatLongConvert(param[3], &gpsData.LongitudeHiRes);

					if (param[4][0] == 'W')
					{
						gpsData.Longitude = gpsData.Longitude | 0x80000000;
						gpsData.LongitudeHiRes = -gpsData.LongitudeHiRes;
					}

					if (((currentMenu != UI_TX_SCREEN) && (currentMenu != MENU_SATELLITE)) &&
							(nonVolatileSettings.locationLat != gpsData.Latitude || nonVolatileSettings.locationLon != gpsData.Longitude))
					{
						nonVolatileSettings.locationLat = gpsData.Latitude;
						nonVolatileSettings.locationLon = gpsData.Longitude;

						menuSatelliteScreenClearPredictions(false);

						gpsData.Status |= (GPS_STATUS_POSITION_UPDATED | GPS_STATUS_HAS_POSITION);
					}

					getParam(gpsLine, line, 8, 20);
					if (strchr(line, '.') != NULL) // There is a value
					{
						uint16_t v = (uint16_t)((strtod(line, NULL)) * 1E2);
						if (v != gpsData.SpeedInHundredthKn)
						{
							gpsData.SpeedInHundredthKn = v;
							gpsData.Status |= (GPS_STATUS_SPEED_UPDATED | GPS_STATUS_HAS_SPEED);
						}
					}
					else if (gpsData.Status & GPS_STATUS_HAS_SPEED) // Value cleared
					{
						gpsData.SpeedInHundredthKn = 0U;
						gpsData.Status &= ~GPS_STATUS_HAS_SPEED;
						gpsData.Status |= GPS_STATUS_SPEED_UPDATED;
					}

					getParam(gpsLine, line, 9, 20);
					if (strchr(line, '.') != NULL) // There is a value
					{
						uint16_t v = (uint16_t)((strtod(line, NULL)) * 1E2);
						if (v != gpsData.CourseInHundredthDeg)
						{
							gpsData.CourseInHundredthDeg = v;
							gpsData.Status |= (GPS_STATUS_COURSE_UPDATED | GPS_STATUS_HAS_COURSE);
						}
					}
					else if (gpsData.Status & GPS_STATUS_HAS_COURSE) // Value cleared
					{
						gpsData.CourseInHundredthDeg = 0U;
						gpsData.Status &= ~GPS_STATUS_HAS_COURSE;
						gpsData.Status |= GPS_STATUS_COURSE_UPDATED;
					}
				}
				else if (memcmp(&gpsLine[3], "GSA", 3) == 0) // DOP and active satellites
				{
					getParam(gpsLine, param[0], 2, 20);	// get parameter 2 which is mode 'A' or 'M'
					getParam(gpsLine, param[1], 3, 20);	// get parameter 3 which is Fix type

					if (param[0][0] == 'A')
					{
						if (gpsData.Status & GPS_STATUS_HAS_FIX)
						{
							// We just got a 3D fix
							if ((param[1][0] == '3') && ((gpsData.Status & GPS_STATUS_3D_FIX) == 0))
							{
								gpsData.Status &= ~GPS_STATUS_2D_FIX;
								gpsData.Status |= (GPS_STATUS_3D_FIX | GPS_STATUS_FIXTYPE_UPDATED);
							} // We just got a 2D fix
							else if ((param[1][0] == '2') && ((gpsData.Status & GPS_STATUS_2D_FIX) == 0))
							{
								gpsData.Status &= ~GPS_STATUS_3D_FIX;
								gpsData.Status |= (GPS_STATUS_2D_FIX | GPS_STATUS_FIXTYPE_UPDATED);
							}
						}
					}
					else
					{
						// Clear 2D and 3D fix, if any sets
						if (gpsData.Status & (GPS_STATUS_2D_FIX | GPS_STATUS_3D_FIX))
						{
							gpsData.Status &= ~(GPS_STATUS_2D_FIX | GPS_STATUS_3D_FIX);
							gpsData.Status |= GPS_STATUS_FIXTYPE_UPDATED;
						}
					}
				}
				else if (memcmp(&gpsLine[3], "GSV", 3) == 0)
				{
					uint16_t            *pSatsInView = NULL;
					gpsSatellitesData_t *pSats = NULL;
					uint8_t             *pCurrentGPSIndex = NULL;
					uint16_t             prevSatsInView = 0;
					uint32_t             gpsStatus;
#if defined(GNSS_MULTI_GSV)
					int8_t               prnSub = 0;
					bool                 gbSatSub = false;
					bool                 glSatSub = false;
#endif

					if ((gpsLine[1] == 'G') && (gpsLine[2] == 'P')) // GPS GSV
					{
						pSatsInView      = &gpsData.SatsInViewGP;
						pSats            = &gpsData.GPSatellites[0];
						pCurrentGPSIndex = &gpsData.currentGPSIndex;
						gpsStatus        = GPS_STATUS_GPS_SATS_UPDATED;
					}
					else if((gpsLine[1] == 'B') // (BD) BeiDou GSV`
#if defined(GNSS_MULTI_GSV)
							|| ((gpsLine[1] == 'G') &&
									((gbSatSub = (gpsLine[2] == 'B'))  // (GB) BeiDou GSV (100 should be subtracted to the PRN number to determine the BeiDou PRN number)
											|| (gpsLine[2] == 'A') // (GA) Galileo GSV
											|| ((glSatSub = (gpsLine[2] == 'L'))) // (GL) GLONASS GSV (64 should be subtracted to the PRN number to determine the GLONASS PRN number)
									)
							)
#endif
					)
					{
						pSatsInView      = &gpsData.SatsInViewBD;
						pSats            = &gpsData.BDSatellites[0];
						pCurrentGPSIndex = &gpsData.currentBDIndex;
						gpsStatus        = GPS_STATUS_BD_SATS_UPDATED;
#if defined(GNSS_MULTI_GSV)
						prnSub           = (gbSatSub ? 100 : (glSatSub ? 64 : 0));
#endif
					}

					if (pSatsInView && pSats && pCurrentGPSIndex)
					{
						bool satsAreDifferents = false;

						prevSatsInView = *pSatsInView;

#if defined(GNSS_MULTI_GSV)
						*pSatsInView = processGSV(gpsLine, lineLength, pSats, pCurrentGPSIndex, &satsAreDifferents, prnSub);
#else
						*pSatsInView = processGSV(gpsLine, lineLength, pSats, pCurrentGPSIndex, &satsAreDifferents);
#endif
						if ((*pSatsInView != prevSatsInView) || satsAreDifferents)
						{
							gpsData.Status |= gpsStatus;
						}
					}
				}
			}
		}
	}
}

#if defined(LOG_GPS_DATA)
static void gpsLogByte(char data)
{
	NMEARecordingBuffer[gpsLogMemOffset % LOG_RAM_BUF_SIZE] = data;
	gpsLogMemOffset++;

	if ((gpsLogMemOffset % LOG_RAM_BUF_SIZE) == 0)
	{
		SPI_Flash_write((gpsLogFlashStartAddress + (((gpsLogMemOffset / LOG_RAM_BUF_SIZE) - 1) * LOG_RAM_BUF_SIZE)), NMEARecordingBuffer, LOG_RAM_BUF_SIZE);
		gpsLogMemOffset %= gpsLogFlashMemSize;
	}
}

static void gpsLogNMEAData(const char *nmea, uint8_t length)
{
	if ((nonVolatileSettings.gps == GPS_MODE_ON_LOG) && gpsIsLogging && ((gpsData.Status & GPS_STATUS_HAS_FIX) != 0))
	{
		for(uint8_t i = 0; i < length; i++)
		{
			gpsLogByte(nmea[i]);
		}
	}
}

void gpsLoggingStart(void)
{
#if ! GPS_TRACK_LOG_TO_FLASH
	return; // switched off: that flash belongs to the receive recorder (settings.h)
#endif

	if (nonVolatileSettings.gps == GPS_MODE_ON_LOG)
	{
		if (gpsIsLogging == false)
		{
#if defined(CPU_MK22FN512VLL12)
			// Invalidate the whole DMRIDs, if exist, on MK22.
			// Except the ones with 16Mb flash chip
			if ((gpsLogFlashStartAddress != LOG_FLASH_16MB_START_ADDRESS) && (dmrIDCacheGetCount() > 0))
			{
				dmrIDCacheClear(); // Ensure dmrIDLookup() fails

				memset(NMEARecordingBuffer, 0x00, DMRID_HEADER_LENGTH);
				SPI_Flash_write(DMRID_MEMORY_LOCATION_1, NMEARecordingBuffer, DMRID_HEADER_LENGTH);
			}
#endif
			// Load the last stored flash block
			gpsLogMemOffset = nonVolatileSettings.gpsLogMemOffset;
			SPI_Flash_read((((gpsLogFlashStartAddress + gpsLogMemOffset) / LOG_RAM_BUF_SIZE) * LOG_RAM_BUF_SIZE), NMEARecordingBuffer, LOG_RAM_BUF_SIZE);
			// write start marker that can be read as text
			gpsLogByte('A');
			gpsLogByte('A');
			gpsLogByte('A');
			gpsLogByte('A');

			gpsIsLogging = true;
		}
	}
}

void gpsLoggingStop(void)
{
	if (gpsIsLogging)
	{
		gpsIsLogging = false;

		// write end marker which can be read as text
		gpsLogByte('Z');
		gpsLogByte('Z');

		if (gpsLogMemOffset % LOG_RAM_BUF_SIZE)
		{
			SPI_Flash_write((gpsLogFlashStartAddress + ((gpsLogMemOffset / LOG_RAM_BUF_SIZE) * LOG_RAM_BUF_SIZE)), NMEARecordingBuffer, (gpsLogMemOffset % LOG_RAM_BUF_SIZE));
		}

		settingsSet(nonVolatileSettings.gpsLogMemOffset, gpsLogMemOffset);
	}
}

void gpsLoggingClear(void)
{
	uint32_t numFlashPages = gpsLogFlashMemSize / LOG_RAM_BUF_SIZE;
	uint32_t addr;

#if ! GPS_TRACK_LOG_TO_FLASH
	// Switched off: erasing "the log" would erase the receive recordings.
	(void) numFlashPages;
	(void) addr;
	return;
#endif

	watchdogRun(false);
	for(uint32_t i = 0; i < numFlashPages; i++)
	{
		addr = gpsLogFlashStartAddress + (i * LOG_RAM_BUF_SIZE);

		SPI_Flash_eraseSector(addr);
	}
	watchdogRun(true);
}
#endif // LOG_GPS_DATA
#endif // HAS_GPS
