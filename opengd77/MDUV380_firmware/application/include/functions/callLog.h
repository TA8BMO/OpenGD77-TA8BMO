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
#ifndef _OPENGD77_CALL_LOG_H_
#define _OPENGD77_CALL_LOG_H_

#include "main.h"

#define CALL_LOG_ENTRIES   32
#define CALL_LOG_CODE_LEN  16

typedef enum
{
	CALL_LOG_TYPE_5T   = 1,
	CALL_LOG_TYPE_DTMF = 2
} callLogType_t;

typedef struct
{
	uint32_t time;    // UTC seconds, as uiDataGlobal.dateTimeSecs; below 2020 means the clock was not set
	uint32_t rxFreq;  // 10 Hz units
	uint8_t  type;    // callLogType_t
	char     code[CALL_LOG_CODE_LEN + 1];
} callLogEntry_t;

// Records a received call. Writes flash (one sector erase), so call it from
// the main loop, never from an interrupt.
void callLogAdd(callLogType_t type, const char *code);

// The calls, newest first: fills slots[] (CALL_LOG_ENTRIES bytes) with the
// storage slot of each and returns how many there are. Read them with
// callLogReadSlot. Done in two steps so that a screen showing several rows
// sorts once rather than once per row.
int callLogSortedSlots(uint8_t *slots);
bool callLogReadSlot(int slot, callLogEntry_t *entry);

int callLogCount(void);
void callLogClear(void);

#endif /* _OPENGD77_CALL_LOG_H_ */
