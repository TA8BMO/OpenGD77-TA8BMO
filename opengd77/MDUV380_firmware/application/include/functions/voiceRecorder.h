/*
 * Copyright (C) 2026 TA8BMO
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

#ifndef _OPENGD77_VOICE_RECORDER_H_
#define _OPENGD77_VOICE_RECORDER_H_

#include <stdbool.h>
#include <stdint.h>
#include "functions/settings.h"

// Receive recorder.
//
// On FM, every transmission heard with the squelch open is recorded - AMBE
// compressed, 450 bytes a second - into the top of the external flash, where it
// survives a power-off. The space is used as a ring: when it is full the oldest
// recordings go. Anything under a second (a repeater being keyed up) is never
// written, nor anything on the APRS frequency. A five-tone or DTMF code heard
// during a transmission is kept with its recording, so the list can say who it was.

// The flash it owns. The GPS track log once had these 2MB; with that switched
// back on (settings.h) the recorder keeps only the top 1MB.
#define VOICE_RECORDER_FLASH_END        (16U * 1024U * 1024U)
#if defined(GPS_TRACK_LOG_TO_FLASH) && GPS_TRACK_LOG_TO_FLASH
#define VOICE_RECORDER_FLASH_START      (15U * 1024U * 1024U)
#else
#define VOICE_RECORDER_FLASH_START      (14U * 1024U * 1024U)
#endif

typedef enum
{
	VOICE_RECORDER_STATUS_STARTING = 0, // not looked at the flash yet
	VOICE_RECORDER_STATUS_OK,
	VOICE_RECORDER_STATUS_NO_FLASH,     // the flash is not the 16MB part this needs
	VOICE_RECORDER_STATUS_DMRID_OVERLAP // the DMR ID database reaches into the space
} voiceRecorderStatus_t;

typedef struct
{
	uint32_t time;          // when it started, UTC seconds (0 if the clock was not set)
	uint32_t rxFreq;        // in 10Hz units
	uint32_t frames;        // 20ms each
	uint8_t  callerType;    // CALL_LOG_TYPE_5T / CALL_LOG_TYPE_DTMF, 0xFF for none
	char     caller[17];    // the code heard, "" for none
	char     channel[17];   // the channel's name, "" on the VFO
} voiceRecorderInfo_t;

// Main loop, every pass.
void voiceRecorderTick(bool hasSignal);

// I2S receive interrupt, through the five-tone decoder's tap: one block, 20ms.
// True while a recording is taking it.
bool voiceRecorderAudioBlock(uint32_t bufNum);

// The call log, whenever a five-tone or DTMF code is decoded.
void voiceRecorderNoteCaller(uint8_t type, const char *code);

voiceRecorderStatus_t voiceRecorderGetStatus(void);
bool voiceRecorderGetEnabled(void);
void voiceRecorderSetEnabled(bool enabled);
bool voiceRecorderIsRecording(void);
bool voiceRecorderIsPlaying(void);
int  voiceRecorderDroppedFrames(void); // lost because the encoder fell behind, since power-on

// The recordings, newest first.
int  voiceRecorderCount(void);
bool voiceRecorderGet(int index, voiceRecorderInfo_t *info);
bool voiceRecorderPlay(int index);
void voiceRecorderStop(void);
int  voiceRecorderPlayingIndex(void);       // -1 when not playing
uint32_t voiceRecorderPlayedFrames(void);
bool voiceRecorderDelete(int index);
void voiceRecorderDeleteAll(void);

#endif /* _OPENGD77_VOICE_RECORDER_H_ */
