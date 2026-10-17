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

#include "hardware/AT1846S.h"
#include "hardware/radioHardwareInterface.h"


#define DCS_PACKED_DATA_NUM          83
#define DCS_PACKED_DATA_ENTRY_SIZE   3

// 83 * 3 bytes(20 bits packed) of DCS code (9bits) + Golay(11bits).
static const uint8_t DCS_PACKED_DATA[(DCS_PACKED_DATA_NUM * DCS_PACKED_DATA_ENTRY_SIZE)] = {
		0x63,  0x87,  0x09,  0xB7,  0x86,  0x0A,  0x5D,  0x06,  0x0B,  0x1F,  0x85,  0x0C,  0xF5,  0x05,  0x0D,  0xB6,
		0x85,  0x11,  0xFD,  0x80,  0x13,  0xCA,  0x87,  0x14,  0xF4,  0x06,  0x16,  0xD1,  0x85,  0x1A,  0x79,  0x86,
		0x1C,  0x93,  0x06,  0x1D,  0xE6,  0x82,  0x1D,  0x47,  0x07,  0x1E,  0x5E,  0x03,  0x26,  0x2B,  0x87,  0x26,
		0xC1,  0x07,  0x27,  0x7B,  0x80,  0x2A,  0xD3,  0x83,  0x2C,  0x39,  0x03,  0x2D,  0xED,  0x02,  0x2E,  0x7A,
		0x83,  0x31,  0xEC,  0x01,  0x35,  0x4D,  0x84,  0x36,  0xA7,  0x04,  0x37,  0xBC,  0x06,  0x39,  0x1D,  0x83,
		0x3A,  0x5F,  0x00,  0x3D,  0x8B,  0x01,  0x3E,  0xE9,  0x86,  0x42,  0x8E,  0x86,  0x49,  0xB0,  0x07,  0x4B,
		0x5B,  0x84,  0x51,  0xFA,  0x01,  0x52,  0x8F,  0x85,  0x52,  0x27,  0x86,  0x54,  0x77,  0x81,  0x58,  0xE8,
		0x85,  0x59,  0x3C,  0x84,  0x5A,  0x94,  0x87,  0x5C,  0xCF,  0x00,  0x63,  0x8D,  0x83,  0x64,  0xC6,  0x86,
		0x66,  0x3E,  0x82,  0x6C,  0x97,  0x82,  0x71,  0xA9,  0x03,  0x73,  0xEB,  0x80,  0x74,  0x85,  0x06,  0x7A,
		0xF0,  0x82,  0x7A,  0x58,  0x81,  0x7C,  0x76,  0x87,  0x84,  0x9C,  0x07,  0x85,  0xE9,  0x83,  0x85,  0xB9,
		0x84,  0x89,  0xC5,  0x86,  0x8C,  0x2F,  0x06,  0x8D,  0xB8,  0x87,  0x92,  0x7E,  0x02,  0x9A,  0x0B,  0x86,
		0x9A,  0xE1,  0x06,  0x9B,  0xC6,  0x83,  0xA1,  0xF8,  0x02,  0xA3,  0x1B,  0x04,  0xA7,  0xE3,  0x00,  0xAD,
		0x9E,  0x01,  0xB3,  0xC7,  0x80,  0xBA,  0xD9,  0x05,  0xC3,  0x71,  0x06,  0xC5,  0xF5,  0x00,  0xCA,  0x1F,
		0x80,  0xCB,  0x28,  0x87,  0xCC,  0xC2,  0x07,  0xCD,  0xC3,  0x04,  0xD6,  0x47,  0x02,  0xD9,  0x93,  0x03,
		0xDA,  0x2B,  0x82,  0xE1,  0xBD,  0x00,  0xE5,  0x98,  0x83,  0xE9,  0xE4,  0x81,  0xEC,  0x0E,  0x01,  0xED,
		0xDA,  0x00,  0xEE,  0x4D,  0x81,  0xF1,  0x0F,  0x02,  0xF6
};

typedef struct
{
	bool cached[2];
	uint8_t lowByte[2];
	uint8_t highByte[2];
} RegCache_t;

static RegCache_t registerCache[RADIO_DEVICE_MAX][127];// all values will be initialised to false,0,0 because its a global
static uint8_t currentRegisterBank[RADIO_DEVICE_MAX] = { 0 }; // offset in cached page array

//
// NOTE: register 0xFF is used for osDelay, values are concatenated for the delay value (in ms).
//       use AT_DELAY(ms) macro to add an osDelay() call in the middle of a sequence
//
#define AT_DELAY(ms) {0xFF, ((ms) >> 8), ((ms) & 0xFF)}

//Modified to replicate the values used on the MDUV380 G4EML
static const uint8_t AT1846InitSettings[][AT1846_BYTES_PER_COMMAND] = {
		{0x30, 0x00, 0x01}, // // Soft reset
		AT_DELAY(50),
		{0x30, 0x00, 0x04}, // Poweron 1846s
		{0x04, 0x0F, 0xD0}, // Clock mode 25.6MHz/26MHz
		{0x0A, 0x7C, 0x20}, // Default Value
		{0x13, 0xA1, 0x00}, // Unknown Register
		{0x1F, 0x10, 0x01}, // gpio6 = sq out, GPIO1=CSS_OUT
		{0x31, 0x00, 0x31}, // UNDOCUMENTED - use recommended value
		{0x33, 0x44, 0xA5}, // agc number
		{0x34, 0x2B, 0x89}, // Rx digital gain (recommend value)
		{0x41, 0x41, 0x22}, // Digital voice gain, (bits 6:0) however default value is supposed to be 0x4006 hence some bits are being set outside the documented range
		{0x42, 0x10, 0x52}, // RDA1846 lists this as Vox Shut threshold
		{0x43, 0x01, 0x00}, // FM deviation
		{0x44, 0x07, 0xFF}, // Rx and tx gain controls
		{0x3A, 0x00, 0xC3}, // SQL Config
		{0x59, 0x0B, 0x90}, // Deviation settings
		{0x47, 0x7F, 0x2F}, // UNDOCUMENTED - UV82 and GD77 use the same values
		{0x4F, 0x2C, 0x62}, // Undocumented
		{0x53, 0x00, 0x94}, // UNDOCUMENTED - use recommended value
		{0x54, 0x2A, 0x3C}, // UNDOCUMENTED - use recommended value
		{0x55, 0x00, 0x81}, // UNDOCUMENTED - use recommended value
		{0x56, 0x0B, 0x02}, // SQ detection time (SQ setting)
		{0x57, 0x1C, 0x00}, // bypass rssi_lpfilter
		{0x58, 0x9C, 0xDD}, // Filters custom setting
		{0x5A, 0x06, 0xDB}, // Unknown
		{0x63, 0x16, 0xAD}, // Pre_emphasis bypass threshold (recommended value)
		{0x0F, 0x8A, 0x24}, // Unknown
		{0x05, 0x87, 0x63}, // Unknown

/*these settings are for the DTMF. Probably not needed on the UV380 as we use the HRC6000 for DTMF
		{0x67, 0x06, 0x28}, // Set DTMF Tone (Probably not needed on the UV380)
		{0x68, 0x05, 0xE5}, // Set DTMF Tone (Probably not needed on the UV380)
		{0x69, 0x05, 0x55}, // Set DTMF Tone (Probably not needed on the UV380)
		{0x6A, 0x04, 0xB8}, // Set DTMF Tone (Probably not needed on the UV380)
		{0x6B, 0x02, 0xFE}, // Set DTMF Tone (Probably not needed on the UV380)
		{0x6C, 0x01, 0xDD}, // Set DTMF Tone (Probably not needed on the UV380)
		{0x6D, 0x00, 0xB1}, // Set DTMF Tone (Probably not needed on the UV380)
		{0x6E, 0x0F, 0x82}, // Set DTMF Tone (Probably not needed on the UV380)
		{0x6F, 0x01, 0x7A}, // Set DTMF 2nd Harmonic (Probably not needed on the UV380)
		{0x70, 0x00, 0x4C}, // Set DTMF 2nd Harmonic (Probably not needed on the UV380)
		{0x71, 0x0F, 0x1D}, // Set DTMF 2nd Harmonic (Probably not needed on the UV380)
		{0x72, 0x0D, 0x91}, // Set DTMF 2nd Harmonic (Probably not needed on the UV380)
		{0x73, 0x0A, 0x3E}, // Set DTMF 2nd Harmonic (Probably not needed on the UV380)
		{0x74, 0x09, 0x0F}, // Set DTMF 2nd Harmonic (Probably not needed on the UV380)
		{0x75, 0x08, 0x33}, // Set DTMF 2nd Harmonic (Probably not needed on the UV380)
		{0x76, 0x08, 0x06}, // Set DTMF 2nd Harmonic (Probably not needed on the UV380)
*/
		{0x30, 0x40, 0xA4}, // Setup to calibrate

		{0x30, 0x40, 0xA6}, // chip_cal_en Enable calibration
		AT_DELAY(100),
		{0x30, 0x40, 0x06}, // chip_cal_en Disable calibration
		AT_DELAY(10),
};

static const uint8_t AT1846PostinitSettings[][AT1846_BYTES_PER_COMMAND] = {
		{0x15, 0x11, 0x00} // IF tuning bits (12:9)
};

const uint8_t AT1846FM12P5kHzSettings[][AT1846_BYTES_PER_COMMAND] = {
		{0x3A, 0x44, 0xCB}, // 12.5 kHz settings
		{0x15, 0x11, 0x00}, // IF tuning bits (12:9)
		{0x32, 0x44, 0x95}, // agc target power
		{0x3A, 0x00, 0xC3}, // modu_det_sel (SQ setting)
		{0x59, 0x0B, 0x90}, // Deviation settings
		{0x3F, 0x29, 0xD1}, // Rssi3_th (SQ setting)
		{0x3C, 0x1B, 0x34}, // Pk_det_th (SQ setting)
		{0x48, 0x19, 0xB1}, // noise1_th (SQ setting)
		{0x60, 0x0F, 0x17}, // noise2_th (SQ setting)
		{0x62, 0x14, 0x25}, // modu_det_th (SQ setting)
		{0x65, 0x24, 0x94}, // setting th_sif for SQ rssi detect
		{0x66, 0xEB, 0x2E}, // rssi_comp  and afc range
		{0x7F, 0x00, 0x01}, // Goto page 1 registers
		{0x06, 0x00, 0x14}, // AGC Table (recommended value for 12.5kHz bandwidth operation)
		{0x07, 0x02, 0x0C}, // AGC Table (recommended value for 12.5kHz bandwidth operation)
		{0x08, 0x02, 0x14}, // AGC Table (recommended value for 12.5kHz bandwidth operation)
		{0x09, 0x03, 0x0C}, // AGC Table (recommended value for 12.5kHz bandwidth operation)
		{0x0A, 0x03, 0x14}, // AGC Table (recommended value for 12.5kHz bandwidth operation)
		{0x0B, 0x03, 0x24}, // AGC Table (recommended value for 12.5kHz bandwidth operation)
		{0x0C, 0x03, 0x44}, // AGC Table (recommended value for 12.5kHz bandwidth operation)
		{0x0D, 0x13, 0x44}, // AGC Table (recommended value for 12.5kHz bandwidth operation)
		{0x0E, 0x1B, 0x44}, // AGC Table (recommended value for 12.5kHz bandwidth operation)
		{0x0F, 0x3F, 0x44}, // AGC Table (recommended value for 12.5kHz bandwidth operation)
		{0x12, 0xE0, 0xEB}, // AGC Table (recommended value for 12.5kHz bandwidth operation)
		{0x7F, 0x00, 0x00}, // Go back to page 0 registers
};

const uint8_t AT1846FM25kHzSettings[][AT1846_BYTES_PER_COMMAND] = {
		{0x3A, 0x40, 0xCB}, // 25 kHz settings
		{0x15, 0x1F, 0x00}, // IF tuning bits (12:9)
		{0x32, 0x75, 0x64}, // agc target power
		{0x3A, 0x00, 0xC3}, // modu_det_sel (SQ setting)
		{0x59, 0x0B, 0xA0}, // Deviation settings
		{0x3F, 0x29, 0xD1}, // Rssi3_th (SQ setting)
		{0x3C, 0x1B, 0x34}, // Pk_det_th (SQ setting)
		{0x48, 0x1E, 0x38}, // noise1_th (SQ setting)
		{0x60, 0x0F, 0x17}, // noise2_th (SQ setting)
		{0x62, 0x37, 0x67}, // modu_det_th (SQ setting)
		{0x65, 0x24, 0x8A}, // setting th_sif for SQ rssi detect
		{0x66, 0xFF, 0x2E}, // rssi_comp  and afc range
		{0x7F, 0x00, 0x01}, // Goto page 1 registers
		{0x06, 0x00, 0x24}, // AGC Table (recommended value for 25kHz bandwidth operation)
		{0x07, 0x02, 0x14}, // AGC Table (recommended value for 25kHz bandwidth operation)
		{0x08, 0x02, 0x24}, // AGC Table (recommended value for 25kHz bandwidth operation)
		{0x09, 0x03, 0x14}, // AGC Table (recommended value for 25kHz bandwidth operation)
		{0x0A, 0x03, 0x24}, // AGC Table (recommended value for 25kHz bandwidth operation)
		{0x0B, 0x03, 0x44}, // AGC Table (recommended value for 25kHz bandwidth operation)
		{0x0C, 0x03, 0x84}, // AGC Table (recommended value for 25kHz bandwidth operation)
		{0x0D, 0x13, 0x84}, // AGC Table (recommended value for 25kHz bandwidth operation)
		{0x0E, 0x1B, 0x84}, // AGC Table (recommended value for 25kHz bandwidth operation)
		{0x0F, 0x3F, 0x84}, // AGC Table (recommended value for 25kHz bandwidth operation)
		{0x12, 0xE0, 0xEB}, // AGC Table (recommended value for 25kHz bandwidth operation)
		{0x7F, 0x00, 0x00}, // Go back to page 0 registers
};

const uint8_t AT1846FMSettings[][AT1846_BYTES_PER_COMMAND] = {
		{0x33, 0x44, 0xA5}, // agc number (recommended value)
		{0x41, 0x44, 0x31}, // Digital voice gain, (bits 6:0) however default value is supposed to be 0x4006 hence some bits are being set outside the documented range
		{0x42, 0x10, 0xF0}, // RDA1846 lists this as Vox Shut threshold
		{0x43, 0x00, 0xA9}, // FM deviation
		{0x58, 0xBC, 0x85}, // Enable some filters for FM e.g. High and Low Pass Filters. G4EML...De-emphasis turned off as this is done by the HRC6000 on the MDUV380.
		{0x44, 0x06, 0xCC}, // set internal volume to 80% .
		{0x3A, 0x00, 0xC3}, // modu_det_sel (SQ setting)
		{0x40, 0x00, 0x30}  // UNDOCUMENTED. THIS IS THE MAGIC REGISTER WHICH ALLOWS LOW FREQ AUDIO BY SETTING THE LS BIT. So it should be cleared to receive FM
};

//Modified to reflect the values used by the UV380 G4EML

const uint8_t AT1846DMRSettings[][AT1846_BYTES_PER_COMMAND] = {
		{0x40, 0x00, 0x31}, // UNDOCUMENTED. THIS IS THE MAGIC REGISTER WHICH ALLOWS LOW FREQ AUDIO BY SETTING THE LS BIT
		{0x15, 0x11, 0x00}, // IF tuning bits (12:9)
		{0x32, 0x44, 0x95}, // agc target power
		{0x3A, 0x00, 0xC3}, // modu_det_sel (SQ setting). Tx No mic input, as the DMR signal directly modulates the master reference oscillator
		{0x3C, 0x1B, 0x34}, // Pk_det_th (SQ setting)
		{0x3F, 0x29, 0xD1}, // Rssi3_th (SQ setting)
		{0x41, 0x41, 0x22}, // Digital voice gain, (bits 6:0) however default value is supposed to be 0x4006 hence some bits are being set outside the documented range
		{0x42, 0x10, 0x52}, // RDA1846 lists this as Vox Shut threshold
		{0x43, 0x01, 0x00}, // FM deviation
		{0x48, 0x19, 0xB1}, // noise1_th (SQ setting)
		{0x58, 0x9C, 0xDD}, // Disable all filters in DMR mode
		{0x44, 0x07, 0xFF}, // set internal volume to 100% (doesn't seem to decode correctly at lower levels on this radio)
};

#define AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT (0x5CU)

void AT1846sInit(void)
{
	memset(&registerCache[currentRadioDeviceId], 0, sizeof(registerCache[currentRadioDeviceId]));

	I2C_AT1846S_send_Settings(AT1846InitSettings, sizeof(AT1846InitSettings) / AT1846_BYTES_PER_COMMAND);

	I2C_AT1846S_send_Settings(AT1846FM12P5kHzSettings, sizeof(AT1846FM12P5kHzSettings) / AT1846_BYTES_PER_COMMAND);// initially set the bandwidth for 12.5 kHz

	osDelay(200);
}

void AT1846sPostInit(void)
{
	I2C_AT1846S_send_Settings(AT1846PostinitSettings, sizeof(AT1846PostinitSettings) / AT1846_BYTES_PER_COMMAND);
}

void AT1846sSetMode(int mode)
{
	if (mode == RADIO_MODE_ANALOG)
	{
		I2C_AT1846S_send_Settings(AT1846FMSettings, sizeof(AT1846FMSettings) / AT1846_BYTES_PER_COMMAND);
	}
	else
	{
		I2C_AT1846S_send_Settings(AT1846DMRSettings, sizeof(AT1846DMRSettings) / AT1846_BYTES_PER_COMMAND);
	}
}

void AT1846sSetBandWidth(bool Is25K)
{
	if (Is25K)
	{
		// 25 kHz settings
		I2C_AT1846S_send_Settings(AT1846FM25kHzSettings, sizeof(AT1846FM25kHzSettings) / AT1846_BYTES_PER_COMMAND);

		taskENTER_CRITICAL();
		radioSetClearReg2byteWithMask(0x30, 0xCF, 0x9F, 0x30, 0x00); // Set the 25Khz Bits and turn off the Rx and Tx
	}
	else
	{
		// 12.5 kHz settings
		I2C_AT1846S_send_Settings(AT1846FM12P5kHzSettings, sizeof(AT1846FM12P5kHzSettings) / AT1846_BYTES_PER_COMMAND);

		taskENTER_CRITICAL();
		radioSetClearReg2byteWithMask(0x30, 0xCF, 0x9F, 0x20, 0x00); // Clear the 25Khz Bit and turn off the Rx and Tx
	}

	radioSetClearReg2byteWithMask(0x30, 0xFF, 0x9F, 0x00, 0x20); // Turn the Rx On
	taskEXIT_CRITICAL();
}

bool radioWriteReg2byte(uint8_t reg, uint8_t val1, uint8_t val2)
{
	if (reg == 0xFF)
	{
		osDelay(((uint32_t)(val1 << 8 | val2)));
		return true;
	}
	else if (reg == 0x7f)
	{
		currentRegisterBank[currentRadioDeviceId] = val2;
	}
	else
	{
		if ((registerCache[currentRadioDeviceId][reg].cached[currentRegisterBank[currentRadioDeviceId]]) &&
				(registerCache[currentRadioDeviceId][reg].highByte[currentRegisterBank[currentRadioDeviceId]] == val1) &&
				(registerCache[currentRadioDeviceId][reg].lowByte[currentRegisterBank[currentRadioDeviceId]] == val2))
		{
			return true;
		}
	}

	uint8_t data[] = { reg, val1, val2 };
	int8_t retries = 3;
	bool ret = false;

	do
	{
		ret = (HAL_I2C_Master_Transmit(&hi2c3, AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, data, 3, HAL_MAX_DELAY) == HAL_OK);

		if (!ret)
		{
			osDelay(1U);
		}
	} while ((ret == false) && (retries-- > 0));

	if (ret)
	{
		if (reg != 0x7F)
		{
			registerCache[currentRadioDeviceId][reg].cached[currentRegisterBank[currentRadioDeviceId]] = true;
			registerCache[currentRadioDeviceId][reg].highByte[currentRegisterBank[currentRadioDeviceId]] = val1;
			registerCache[currentRadioDeviceId][reg].lowByte[currentRegisterBank[currentRadioDeviceId]] = val2;
		}
	}

	return ret;
}

// Extra FM receive attenuation, for listening quietly.
//
// The normal volume control is the C6000 line-out gain, and it bottoms out at
// -31dB - low, but still enough to fill a silent room at night. This winds the
// AT1846S's own audio volume down as well over the lower half of the knob,
// stacking on top of the line-out gain so the quietest setting is genuinely
// low, while the upper half keeps the stock level. FM only: the AT1846S is not
// in the DMR audio path.
//
// The volume sits in register 0x44, whose stock value is 0x06CC - the low byte
// 0xCC being two equal nibbles, which the init comments call "80%". Both nibbles
// are kept equal here, so whatever their exact meaning the value stays the same
// symmetric shape as the factory one and only its level changes.
void radioSetFMRxAudioGain(int8_t volume)
{
	uint8_t nib;

	if (volume >= 0)
	{
		nib = 0x0C; // stock: low byte 0xCC
	}
	else
	{
		int g = ((volume < -31) ? -31 : (int) volume); // -31..-1 (guard the -99 mute)

		// -31..0 maps onto nibble 2..12, so the bottom of the travel is a small
		// fraction of full and it climbs smoothly back to the stock level by the
		// midpoint of the knob.
		nib = (uint8_t)(2 + (((g + 31) * (12 - 2)) / 31));
	}

	radioWriteReg2byte(0x44, 0x06, (uint8_t)((nib << 4) | nib));
}

bool radioReadReg2byte(uint8_t reg, uint8_t *val1, uint8_t *val2)
{
	uint8_t data[2] = {reg, 0x00};
	int8_t retries = 3;
	bool ret = false;

	do
	{
		ret = (HAL_I2C_Master_Transmit(&hi2c3, AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, data, 1, HAL_MAX_DELAY) == HAL_OK);

		if (!ret)
		{
			osDelay(1U);
		}
	} while ((ret == false) && (retries-- > 0));

	if (ret)
	{
		retries = 3;
		do
		{
			ret = (HAL_I2C_Master_Receive(&hi2c3, AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, data, 2, HAL_MAX_DELAY) == HAL_OK);

			if (!ret)
			{
				osDelay(1U);
			}
		} while ((ret == false) && (retries-- > 0));

		if (ret)
		{
			*val1 = data[0];
			*val2 = data[1];
		}
	}
	return ret;
}

bool radioSetClearReg2byteWithMask(uint8_t reg, uint8_t mask1, uint8_t mask2, uint8_t val1, uint8_t val2)
{
	bool status;
	uint8_t tmp_val1, tmp_val2;

	if ((registerCache[currentRadioDeviceId][reg].cached[currentRegisterBank[currentRadioDeviceId]]))
	{
		tmp_val1 = registerCache[currentRadioDeviceId][reg].highByte[currentRegisterBank[currentRadioDeviceId]];
		tmp_val2 = registerCache[currentRadioDeviceId][reg].lowByte[currentRegisterBank[currentRadioDeviceId]];
	}
	else
	{
		status = radioReadReg2byte(reg, &tmp_val1, &tmp_val2);
		if (!status)
		{
			return status;
		}
	}

	tmp_val1 = val1 | (tmp_val1 & mask1);
	tmp_val2 = val2 | (tmp_val2 & mask2);
	status = radioWriteReg2byte(reg, tmp_val1, tmp_val2);

	return status;
}

void I2C_AT1846_set_register_with_mask(uint8_t reg, uint16_t mask, uint16_t value, uint8_t shift)
{
	taskENTER_CRITICAL();
	radioSetClearReg2byteWithMask(reg, (mask & 0xff00) >> 8, (mask & 0x00ff) >> 0, ((value << shift) & 0xff00) >> 8, ((value << shift) & 0x00ff) >> 0);
	taskEXIT_CRITICAL();
}

void I2C_AT1846S_send_Settings(const uint8_t settings[][AT1846_BYTES_PER_COMMAND], int numSettings)
{
	taskENTER_CRITICAL();
	for(int i = 0; i < numSettings; i++)
	{
		radioWriteReg2byte(settings[i][0], settings[i][1], settings[i][2]);
	}
	taskEXIT_CRITICAL();
}

// Lookup for Golay pattern, then returns the full bit pattern for given DCS code
static uint32_t dcsGetBitPatternFromCode(uint16_t dcs)
{
	uint16_t startPos = 0;
	uint16_t endPos = (DCS_PACKED_DATA_NUM - 1);
	uint16_t curPos;
	uint8_t *p = (uint8_t *)DCS_PACKED_DATA;

	while (startPos <= endPos)
	{
		curPos = (startPos + endPos) >> 1;

		uint32_t entry = *(uint32_t *)(p + (DCS_PACKED_DATA_ENTRY_SIZE * curPos)) & 0x00FFFFFF;
		uint16_t foundCode = (entry >> 15) & 0x1FF;

		if (foundCode < dcs)
		{
			startPos = curPos + 1;
		}
		else if (foundCode > dcs)
		{
			endPos = curPos - 1;
		}
		else
		{
			return (((entry & 0x7FF) << 12) | 0x800 | dcs);
		}
	}

	return 0x00;
}

void AT1846sSetRxCSSOff(RadioDevice_t deviceId)
{
	UNUSED_PARAMETER(deviceId);

	taskENTER_CRITICAL();
	// tone value of 0xffff in the codeplug seem to be a flag that no tone has been selected
	// Zero the CTCSS1 Register
	radioWriteReg2byte(0x4a, 0x00, 0x00);
	// Zero the CTCSS2 Register
	radioWriteReg2byte(0x4d, 0x00, 0x00);
	// disable the transmit CTCSS/DCS
	radioSetClearReg2byteWithMask(0x4e, 0xF9, 0xFF, 0x00, 0x00);
	taskEXIT_CRITICAL();
}

void AT1846sSetRxCTCSS(RadioDevice_t deviceId, uint16_t tone)
{
	int threshold = (25000 - tone) / 1000;  // adjust threshold value to match tone frequency.

#if !defined(PLATFORM_MD2017)
	UNUSED_PARAMETER(deviceId);
#endif

	if (tone > 24000)
	{
		threshold = 1;
	}

	taskENTER_CRITICAL();
	// Zero the CTCSS1 Register
	radioWriteReg2byte(0x4a, 0x00, 0x00);
	// Zero the CDCSS
	radioWriteReg2byte(0x4b, 0x00, 0x00);
	radioWriteReg2byte(0x4c, 0x00, 0x00);

	radioWriteReg2byte(0x4d, (tone >> 8) & 0xFF, (tone & 0xFF));
	//set the detection thresholds
	radioWriteReg2byte(0x5b, (threshold & 0xFF), (threshold & 0xFF));
	//set detection to CTCSS2
	radioSetClearReg2byteWithMask(0x3a, 0xFF, 0xE0, 0x00, 0x08);
	taskEXIT_CRITICAL();
}

void AT1846sSetRxDCS(RadioDevice_t deviceId, uint16_t code, bool inverted)
{
#if !defined(PLATFORM_MD2017)
	UNUSED_PARAMETER(deviceId);
#endif

	taskENTER_CRITICAL();
	// Set the CTCSS1 Register to 134.4Hz (DCS data rate)
	radioWriteReg2byte(0x4a, (TRX_DCS_TONE >> 8) & 0xFF, TRX_DCS_TONE & 0xFF);
	// Zero the CTCSS2 Register
	radioWriteReg2byte(0x4d, 0x00, 0x00);

	// The AT1846S wants the Golay{23,12} encoding of the DCS code, rather than just the code itself.
	uint32_t encoded = dcsGetBitPatternFromCode(code);
	radioWriteReg2byte(0x4b, 0x00, (encoded >> 16) & 0xFF);           // init cdcss_code
	radioWriteReg2byte(0x4c, (encoded >> 8) & 0xFF, encoded & 0xFF);  // init cdcss_code

	uint8_t reg4e_high = (inverted ? 0x05 : 0x04);
	uint8_t reg3a_low = (inverted ? 0x04 : 0x02);
	// The cdcss_sel bits have to be set for DCS receive to work
	radioSetClearReg2byteWithMask(0x4e, 0x38, 0x3F, reg4e_high, 0x00); // enable transmit DCS
	radioSetClearReg2byteWithMask(0x3a, 0xFF, 0xE0, 0x00, reg3a_low); // enable receive DCS
	taskEXIT_CRITICAL();
}

void AT1846sSetTxCTCSS(uint16_t tone)
{
	taskENTER_CRITICAL();
	if (tone > 0)
	{
		// CTCSS 1
		radioWriteReg2byte(0x4a, (tone >> 8) & 0xff, (tone & 0xff));
		// Zero CTCSS 2
		radioWriteReg2byte(0x4d, 0x00, 0x00);
		// init cdcss_code
		radioWriteReg2byte(0x4b, 0x00, 0x00);
		radioWriteReg2byte(0x4c, 0x00, 0x00);
		// enable the transmit CTCSS
		radioSetClearReg2byteWithMask(0x4e, 0xF9, 0xFF, 0x06, 0x00);
	}
	else
	{
		// tone value of 0xffff in the codeplug seem to be a flag that no tone has been selected
		// Zero the CTCSS1 Register
		radioWriteReg2byte(0x4a, 0x00, 0x00);
		// Zero the CTCSS2 Register
		radioWriteReg2byte(0x4d, 0x00, 0x00);
		// disable the transmit CTCSS/DCS
		radioSetClearReg2byteWithMask(0x4e, 0xF9, 0xFF, 0x00, 0x00);
	}
	taskEXIT_CRITICAL();
}

void AT1846sSetTxDCS(uint16_t code, bool inverted)
{
	taskENTER_CRITICAL();
	// Set the CTCSS1 Register to 134.4Hz (DCS data rate)
	radioWriteReg2byte(0x4a, (TRX_DCS_TONE >> 8) & 0xff, TRX_DCS_TONE & 0xff);
	// Zero the CTCSS2 Register
	radioWriteReg2byte(0x4d, 0x00, 0x00);

	// The AT1846S wants the Golay{23,12} encoding of the DCS code, rather than just the code itself.
	uint32_t encoded = dcsGetBitPatternFromCode(code);
	radioWriteReg2byte(0x4b, 0x00, (encoded >> 16) & 0xff);           // init cdcss_code
	radioWriteReg2byte(0x4c, (encoded >> 8) & 0xff, encoded & 0xff);  // init cdcss_code

	uint8_t reg4e_high = (inverted ? 0x05 : 0x04);
	radioSetClearReg2byteWithMask(0x4e, 0x38, 0x3F, reg4e_high, 0x00); // enable transmit DCS
	taskEXIT_CRITICAL();
}

bool AT1846sCheckCSS(uint16_t tone, CodeplugCSSTypes_t type)
{
	//test if CTCSS or DCS is being received and return true if it is
	bool retval;
	uint8_t FlagsH;
	uint8_t FlagsL;
	uint8_t flagLBits = (0x01 | ((type & CSS_TYPE_DCS) ? ((type & CSS_TYPE_DCS_INVERTED) ? 0x40 : 0x80) : 0x00));

	taskENTER_CRITICAL();
	retval = radioReadReg2byte(0x1c, &FlagsH, &FlagsL);
	taskEXIT_CRITICAL();

	return (retval && ((FlagsL & flagLBits) == flagLBits) && ((type & CSS_TYPE_CTCSS) ? ((FlagsH & 0x01) != 0) : true));
}

bool AT1846sWriteTone1Reg(uint16_t toneFreqVal)
{
	uint8_t reg = 0x35;// Tone 1 is reg 0x35
	uint8_t val1 = (toneFreqVal >> 8) & 0xff;
	uint8_t val2 = (toneFreqVal & 0xff);
	uint8_t data[] = { reg, val1, val2 };
	int8_t retries = 3;
	bool ret = false;

	do
	{
		ret = (HAL_I2C_Master_Transmit(&hi2c3, AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, data, 3, HAL_MAX_DELAY) == HAL_OK);

		if (!ret)
		{
			osDelay(1U);
		}
	} while ((ret == false) && (retries-- > 0));

	return ret;
}

void AT1846sSelectVoiceChannel(uint8_t channel, uint8_t *voiceGainTx, uint16_t *deviation)
{
	uint8_t valh;
	uint8_t vall;

	taskENTER_CRITICAL();
	switch (channel)
	{
		case AT1846_VOICE_CHANNEL_TONE1:
		case AT1846_VOICE_CHANNEL_TONE2:
		case AT1846_VOICE_CHANNEL_DTMF:
			radioSetClearReg2byteWithMask(0x79, 0xff, 0xff, 0xc0, 0x00); // Select single tone
			radioSetClearReg2byteWithMask(0x57, 0xff, 0xfe, 0x00, 0x01); // Audio feedback on

			radioReadReg2byte(0x41, &valh, voiceGainTx);
			*voiceGainTx &= 0x7f;

			radioReadReg2byte(0x59, &valh, &vall);
#warning REASON FOR HARDCODED VALUE ??
			*deviation = (vall + (valh << 8)) >> 6;
			*deviation = 0x40;

			I2C_AT1846_set_register_with_mask(0x59, 0x003f, *deviation, 6);
			//radioSetClearReg2byteWithMask(0x41, 0xFF,0x80, 0x00, 0);// 0x0E is Tone deviation value from the normal GD77 calibration data
			break;

		default:
			radioSetClearReg2byteWithMask(0x57, 0xff, 0xfe, 0x00, 0x00); // Audio feedback off
			if (*voiceGainTx != 0xFF)
			{
				I2C_AT1846_set_register_with_mask(0x41, 0xFF80, *voiceGainTx, 0);
				*voiceGainTx = 0xFF;
			}

			if (*deviation != 0xFF)
			{
				I2C_AT1846_set_register_with_mask(0x59, 0x003f, *deviation, 6);
				*deviation = 0xFF;
			}
			break;
	}
	radioSetClearReg2byteWithMask(0x3a, 0x8f, 0xff, channel, 0x00);
	taskEXIT_CRITICAL();
}
