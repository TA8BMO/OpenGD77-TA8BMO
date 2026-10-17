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

// ---------------------------------------------------------------------------
// Call log: every 5-Tone and DTMF code received, with when and on what
// frequency, and it survives a power-off.
//
// It lives in the emulated EEPROM, in the 4K sector it shares with the
// settings, the game scores and the Morse log, in space nothing else uses: 32
// records of 32 bytes at 0x6600, then a "cleared up to" marker at 0x6A00. It
// takes no RAM at all - main RAM and CCM RAM are both full, believe me I have
// checked - so everything is read from flash when needed. That is cheap, a read
// is a few bytes over SPI.
//
// Writing is the expensive part, since every write erases the sector. So each
// record carries its own sequence number instead of a header with a write
// pointer: a new call is a single write, into the empty or the oldest slot.
// Clearing is one write too - the marker says which sequence numbers are gone.
// ---------------------------------------------------------------------------
#include "functions/callLog.h"
#include "functions/voiceRecorder.h"
#include "functions/settings.h"
#include "functions/trx.h"
#include "hardware/EEPROM.h"
#include "user_interface/uiGlobals.h"
#include <string.h>

#define CALL_LOG_ADDR        0x6600 // free space after the Morse log (0x6400..0x651F)
#define CALL_LOG_RECORD_SIZE 32
#define CALL_LOG_CLEAR_ADDR  (CALL_LOG_ADDR + (CALL_LOG_ENTRIES * CALL_LOG_RECORD_SIZE)) // 0x6A00
#define CALL_LOG_MAGIC1      0xC1
#define CALL_LOG_MAGIC2      0x5A
#define CALL_LOG_CLEAR_MAGIC 0x434C5231U // "CLR1"

typedef struct
{
	uint32_t seq;      // 1 and up; the highest is the newest
	uint32_t time;
	uint32_t rxFreq;
	uint8_t  type;
	uint8_t  magic1;
	uint8_t  magic2;
	uint8_t  reserved;
	char     code[CALL_LOG_CODE_LEN]; // NUL padded; all 16 used means no NUL
} callLogRecord_t;

typedef struct
{
	uint32_t magic;
	uint32_t seq;      // records up to and including this one were cleared
} callLogClearMark_t;

_Static_assert(sizeof(callLogRecord_t) == CALL_LOG_RECORD_SIZE, "call log record must be 32 bytes");

// The sequence number of the record in a slot, or 0 if it holds none. Erased
// flash reads 0xFF, which fails both markers and the type, and so does
// anything else that was there before.
static uint32_t callLogReadSeq(int slot, callLogRecord_t *record)
{
	if (EEPROM_Read((CALL_LOG_ADDR + (slot * CALL_LOG_RECORD_SIZE)), (uint8_t *) record, CALL_LOG_RECORD_SIZE) &&
			(record->magic1 == CALL_LOG_MAGIC1) && (record->magic2 == CALL_LOG_MAGIC2) &&
			((record->type == CALL_LOG_TYPE_5T) || (record->type == CALL_LOG_TYPE_DTMF)) &&
			(record->seq != 0U) && (record->seq != 0xFFFFFFFFU))
	{
		return record->seq;
	}

	return 0U;
}

static uint32_t callLogClearedSeq(void)
{
	callLogClearMark_t mark;

	if (EEPROM_Read(CALL_LOG_CLEAR_ADDR, (uint8_t *) &mark, sizeof(mark)) && (mark.magic == CALL_LOG_CLEAR_MAGIC))
	{
		return mark.seq;
	}

	return 0U;
}

void callLogAdd(callLogType_t type, const char *code)
{
	callLogRecord_t record;
	uint32_t newest = callLogClearedSeq();
	uint32_t oldest = 0xFFFFFFFFU;
	int target = -1;

	if ((code == NULL) || (code[0] == 0))
	{
		return;
	}

	voiceRecorderNoteCaller((uint8_t) type, code); // the recording of this transmission says who it was

	// The new record goes into an empty slot if there is one, otherwise over
	// the oldest record.
	for (int slot = 0; slot < CALL_LOG_ENTRIES; slot++)
	{
		uint32_t seq = callLogReadSeq(slot, &record);

		if (seq == 0U)
		{
			if ((target < 0) || (oldest != 0U))
			{
				target = slot;
				oldest = 0U; // an empty slot beats any record
			}
			continue;
		}

		if (seq > newest)
		{
			newest = seq;
		}

		if (seq < oldest)
		{
			oldest = seq;
			target = slot;
		}
	}

	memset(&record, 0, sizeof(record));
	record.seq = (newest + 1U);
	record.time = uiDataGlobal.dateTimeSecs;
	record.rxFreq = ((currentChannelData != NULL) ? currentChannelData->rxFreq : 0U);
	record.type = (uint8_t) type;
	record.magic1 = CALL_LOG_MAGIC1;
	record.magic2 = CALL_LOG_MAGIC2;
	strncpy(record.code, code, CALL_LOG_CODE_LEN);

	(void) EEPROM_Write((CALL_LOG_ADDR + (target * CALL_LOG_RECORD_SIZE)), (uint8_t *) &record, CALL_LOG_RECORD_SIZE);
}

int callLogSortedSlots(uint8_t *slots)
{
	callLogRecord_t record;
	uint32_t seqs[CALL_LOG_ENTRIES];
	uint32_t cleared = callLogClearedSeq();
	int count = 0;

	for (int slot = 0; slot < CALL_LOG_ENTRIES; slot++)
	{
		uint32_t seq = callLogReadSeq(slot, &record);

		if (seq > cleared)
		{
			// Insertion sort, newest first - 32 at most.
			int i = count++;

			while ((i > 0) && (seqs[i - 1] < seq))
			{
				seqs[i] = seqs[i - 1];
				slots[i] = slots[i - 1];
				i--;
			}
			seqs[i] = seq;
			slots[i] = (uint8_t) slot;
		}
	}

	return count;
}

bool callLogReadSlot(int slot, callLogEntry_t *entry)
{
	callLogRecord_t record;

	if ((slot < 0) || (slot >= CALL_LOG_ENTRIES) || (callLogReadSeq(slot, &record) == 0U))
	{
		return false;
	}

	entry->time = record.time;
	entry->rxFreq = record.rxFreq;
	entry->type = record.type;
	memcpy(entry->code, record.code, CALL_LOG_CODE_LEN);
	entry->code[CALL_LOG_CODE_LEN] = 0;
	return true;
}

int callLogCount(void)
{
	uint8_t slots[CALL_LOG_ENTRIES];

	return callLogSortedSlots(slots);
}

// Hides every record so far: one write of the marker, not one per record.
void callLogClear(void)
{
	callLogRecord_t record;
	callLogClearMark_t mark = { .magic = CALL_LOG_CLEAR_MAGIC, .seq = callLogClearedSeq() };

	for (int slot = 0; slot < CALL_LOG_ENTRIES; slot++)
	{
		uint32_t seq = callLogReadSeq(slot, &record);

		if (seq > mark.seq)
		{
			mark.seq = seq;
		}
	}

	(void) EEPROM_Write(CALL_LOG_CLEAR_ADDR, (uint8_t *) &mark, sizeof(mark));
}
