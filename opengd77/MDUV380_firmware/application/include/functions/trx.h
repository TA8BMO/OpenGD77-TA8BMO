/*
 * Copyright (C) 2019      Kai Ludwig, DG4KLU
 * Copyright (C) 2019-2024 Roger Clark, VK3KYY / G4KYF
 *                         Colin, G4EML
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

#ifndef _OPENGD77_TRX_H_
#define _OPENGD77_TRX_H_

#include "functions/sound.h"
//#include "interfaces/i2c.h"
#include "functions/calibration.h"
#include "functions/codeplug.h"
#include "hardware/radioHardwareInterface.h"

#define RSSI_NOISE_SAMPLE_PERIOD_PIT  25U // 25 milliseconds

// AT1846S defines

#define BANDWIDTH_12P5KHZ false
#define BANDWIDTH_25KHZ   true

// Note these do not apply to the MD9600 but have been included to allow stub functions to compile
#define AT1846_VOICE_CHANNEL_NONE   0x00
#define AT1846_VOICE_CHANNEL_TONE1  0x10
#define AT1846_VOICE_CHANNEL_TONE2  0x20
#define AT1846_VOICE_CHANNEL_DTMF   0x30
#define AT1846_VOICE_CHANNEL_MIC    0x40


#if defined(MD9600_VERSION_5)
#define STRONG_SIGNAL_RESCALE	1.4f
#define STRONG_SIGNAL_RSSI_BARS	20
#else
#define STRONG_SIGNAL_RESCALE	5
#define STRONG_SIGNAL_RSSI_BARS	4
#endif

typedef struct
{
	int calPowerTableMinFreq;
	int calIQTableMinFreq;
	uint32_t minFreq;
	uint32_t maxFreq;
} frequencyHardwareBand_t;

typedef struct
{
	uint32_t minFreq;
	uint32_t maxFreq;
} frequencyBand_t;


enum RADIO_MODE { RADIO_MODE_NONE, RADIO_MODE_ANALOG, RADIO_MODE_DIGITAL };
enum DMR_ADMIT_CRITERIA { ADMIT_CRITERIA_ALWAYS, ADMIT_CRITERIA_CHANNEL_FREE, ADMIT_CRITERIA_COLOR_CODE };
enum DMR_MODE { DMR_MODE_AUTO, DMR_MODE_DMO, DMR_MODE_RMO, DMR_MODE_SFR };


enum RADIO_FREQUENCY_BAND_NAMES { RADIO_BAND_VHF = 0, RADIO_BAND_220MHz, RADIO_BAND_UHF, RADIO_BANDS_TOTAL_NUM };




enum TRX_FREQ_BAND { TRX_RX_FREQ_BAND = 0, TRX_TX_FREQ_BAND };

extern const frequencyHardwareBand_t 	RADIO_HARDWARE_FREQUENCY_BANDS[RADIO_BANDS_TOTAL_NUM];
extern const frequencyBand_t			DEFAULT_USER_FREQUENCY_BANDS[RADIO_BANDS_TOTAL_NUM];
extern frequencyBand_t					USER_FREQUENCY_BANDS[RADIO_BANDS_TOTAL_NUM];

extern const uint8_t TRX_NUM_CTCSS;
extern const uint16_t TRX_CTCSSTones[];

extern const uint16_t TRX_DCS_TONE;
extern const uint8_t TRX_NUM_DCS;
extern const uint16_t TRX_DCSCodes[];

extern volatile bool trxTransmissionEnabled;
extern volatile bool trxIsTransmitting;
extern uint32_t trxTalkGroupOrPcId;
extern uint32_t trxDMRID;
extern volatile uint8_t trxTxVox;
extern volatile uint8_t trxTxMic;
extern calibrationPowerValues_t trxPowerSettings;
extern volatile bool txPAEnabled;
extern volatile bool trxDMRSynchronisedRSSIReadPending;

extern volatile uint16_t txDACDrivePower;
extern volatile uint8_t analogIGain;
extern volatile uint8_t analogQGain;
extern volatile uint8_t digitalIGain;
extern volatile uint8_t digitalQGain;
extern volatile int8_t Mod2Offset;

extern volatile bool trxIsTransmittingDMR;
extern volatile uint32_t trxDMRstartTime;

bool trxCarrierDetected(RadioDevice_t deviceId);
bool trxCheckDigitalSquelch(RadioDevice_t deviceId);
bool trxCheckAnalogSquelch(void);
void trxResetSquelchesState(RadioDevice_t deviceId);
int trxGetMode(void);
bool trxGetBandwidthIs25kHz(void);
uint32_t trxGetFrequency(void);
void trxSetModeAndBandwidth(int mode, bool bandwidthIs25kHz);
void trxSetFrequency(uint32_t fRx, uint32_t fTx, int dmrMode);
void trxSetRX(void);
void trxSetTX(void);
void trxRxAndTxOff(bool critical);
void trxRxOn(bool critical);
void trxActivateRx(bool critical);
void trxActivateTx(bool critical);
void trxSetPowerFromLevel(uint8_t powerLevel);
void trxUpdate_PA_DAC_Drive(void);
uint16_t trxGetPA_DAC_Drive(void);
uint8_t trxGetPowerLevel(void);
void trxCalcBandAndFrequencyOffset(CalibrationBand_t *calibrationBand, uint32_t *freq_offset);
void trxSetDMRColourCode(uint8_t colourCode);
uint8_t trxGetDMRColourCode(void);
int trxGetDMRTimeSlot(void);
void trxSetDMRTimeSlot(int timeslot, bool resync);
void trxSetTxCSS(uint16_t tone);
void trxSetRxCSS(RadioDevice_t deviceId, uint16_t tone);
bool trxCheckCSSFlag(uint16_t tone);
void trxArmRxCSSDetector(uint16_t tone); // detector only: the squelch and audio are left as they are
bool trxCheckFrequencyInAmateurBand(uint32_t frequency);
uint32_t trxGetBandFromFrequency(uint32_t frequency);
uint32_t trxGetNextOrPrevBandFromFrequency(uint32_t frequency, bool nextBand);
void trxReadVoxAndMicStrength(void);
void trxPostponeReadRSSIAndNoise(uint32_t msOverride);
void trxReadRSSIAndNoise(bool force);
uint8_t trxGetCalibrationVoiceGainTx(void);
void trxSelectVoiceChannel(uint8_t channel);
void trxSetTone1(int toneFreq);
void trxSetTone2(int toneFreq);
void trxSetDTMF(int code);
void trxDTMFoff(bool enableMic);
void trxUpdateTsForCurrentChannelWithSpecifiedContact(struct_codeplugContact_t *contactData);
void trxSetMicGainFM(uint8_t gain);

void trxEnableTransmission(void);
void trxDisableTransmission(void);
bool trxPowerUpDownRxAndC6000(bool powerUp, bool includeC6000, bool includeMic);
uint8_t trxGetAnalogFilterLevel(void);
void trxSetAnalogFilterLevel(uint8_t newFilterLevel);
void trxTerminateCheckAnalogSquelch(RadioDevice_t deviceId);
void trxInvalidateCurrentFrequency(void);
void trxActivateDMRTx(void);
void trxFastDMRTx(bool tx);

int trxGetRSSIdBm(RadioDevice_t deviceId);
int trxGetNoisedBm(RadioDevice_t deviceId);
int trxGetSNRMargindBm(RadioDevice_t deviceId);
uint8_t trxGetSignalRaw(RadioDevice_t deviceId);
uint8_t trxGetNoiseRaw(RadioDevice_t deviceId);
void trxSelectVoiceChannel(uint8_t channel);
void trxConfigurePA_DAC_ForFrequencyBand(void);

#endif /* _OPENGD77_TRX_H_ */
