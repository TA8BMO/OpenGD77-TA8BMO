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
// RX recorder. See voiceRecorder.h.
//
// A bit of history first. I originally wanted a voice keyer - record "this is
// TA8BMO, stations listening please move to channel 2" once and let the radio
// repeat it. It cannot work on this radio: on the MDUV380 the HR-C6000 is the
// FM modulator and it only modulates its analogue line input, so audio from the
// MCU never reaches the transmitter. The only way I got my recording on air was
// cupping my hand over the speaker so the microphone heard it. Funny, but
// useless. So I turned it around and record what I *receive* instead, which
// turned out to be far more useful anyway.
//
// Capture: the five-tone decoder already has the received audio while the
// squelch is open (8kHz on the I2S receive interrupt), and its tap passes every
// 20ms block here too. The block is queued and AMBE encoded in the main loop -
// at about 11ms a frame the encoder is far too slow for the interrupt. While a
// recording runs the APRS and Morse decoders step aside so the encoder gets the
// time it needs; five-tone and DTMF keep going, so I still know who called.
//
// Storage: the space is a ring of 4KB flash sectors. Each sector holds a 64 byte
// header and up to 448 frames of 9 bytes. A recording takes whole sectors from a
// fresh one on; the first carries part 0, and every sector carries the sequence
// number of that first one, so after a power-off the recordings are found again
// just by reading the headers. Fields that are not known when a header is
// written - frame count, caller, deleted flag - start as 0xFF and get filled in
// later: programming flash only ever turns bits from 1 to 0, so a field can be
// written once into an already written page. Sectors are erased while the radio
// is idle, ahead of where the next recording will go, so recording never waits
// for an erase.
//
// Nothing is written or erased unless the flash is the 16MB part, the DMR ID
// database ends below this space, and no computer is on the USB (the CPS might
// be using the flash). Every address is checked against the space's bounds - I
// really did not want to be the guy who wiped his own DMR ID database.
//
// One lesson from testing: when calls came in choppy I blamed this code first.
// It was my squelch, left at 90%. Check that before you blame the recorder :)
// ---------------------------------------------------------------------------
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include "main.h"
#include "functions/voiceRecorder.h"
#include "functions/callLog.h"
#include "functions/codeplug.h"
#include "functions/sound.h"
#include "functions/trx.h"
#include "functions/ticks.h"
#include "functions/voicePrompts.h"
#include "functions/fiveToneDecode.h"
#include "functions/rxPowerSaving.h"
#include "hardware/HR-C6000.h"
#include "hardware/SPI_Flash.h"
#include "hardware/radioHardwareInterface.h"
#include "interfaces/i2s.h"
#include "user_interface/uiGlobals.h"
#include "user_interface/uiUtilities.h"

#define VR_SECTOR_SIZE        4096U
#define VR_SECTORS            ((int)((VOICE_RECORDER_FLASH_END - VOICE_RECORDER_FLASH_START) / VR_SECTOR_SIZE))
#define VR_PAGE_SIZE          256U
#define VR_HEADER_SIZE        64U
#define VR_FRAME_BYTES        9U
#define VR_FRAMES_PER_SECTOR  ((VR_SECTOR_SIZE - VR_HEADER_SIZE) / VR_FRAME_BYTES) // 448, just under 9s
#define VR_MAGIC              0x32435256U // "VRC2": the 64 byte header, with the channel name
#define VR_FLAG_LIVE          0x01U       // programmed to 0 when the recording is deleted

#define VR_MIN_FRAMES         50          // 1s: anything shorter is a repeater keyed up, and never written
#define VR_TAIL_FRAMES        8           // the squelch tail, 160ms, dropped from the end
#define VR_HANG_MS            400         // a dropout shorter than this does not end a recording
#define VR_PCM_FRAMES         8           // 160ms of slack between the interrupt and the encoder
#define VR_PEND_FRAMES        64          // encoded, not yet in the flash
#define VR_PREERASE           16          // sectors kept erased ahead: about two and a half minutes
#define VR_ERASE_GAP_MS       100
#define VR_LIST_MAX           96
#define VR_LATE_CALLER_MS     3000        // a DTMF code is only logged 2s after it ends
#define VR_APRS_FREQ          14480000U   // 144.800MHz: packets, not speech
#define VR_START_DELAY_MS     3000        // let the boot finish - the DMR ID database is read then
#define VR_WS_PROBE_MS        6

typedef struct __attribute__((packed))
{
	uint32_t magic;
	uint32_t seq;         // one more for every sector written
	uint32_t firstSeq;    // seq of the recording's first sector
	uint16_t frames;      // 0xFFFF until the sector is closed
	uint8_t  part;        // 0: the recording's first sector
	uint8_t  flags;
	uint32_t time;        // UTC seconds
	uint32_t rxFreq;      // 10Hz units
	uint8_t  callerType;  // 0xFF: none
	uint8_t  reserved[3];
	char     caller[16];  // unterminated when all 16 are used; 0xFF filled
	char     name[16];    // the channel's name, as the codeplug has it; 0xFF filled for VFO
	uint8_t  pad[4];
} vrHeader_t;

_Static_assert(sizeof(vrHeader_t) == VR_HEADER_SIZE, "recorder sector header must be 64 bytes");
_Static_assert((VOICE_RECORDER_FLASH_START % VR_SECTOR_SIZE) == 0, "recorder space must start on a sector");
_Static_assert(VR_PREERASE < VR_SECTORS, "erase-ahead must be smaller than the space");

typedef struct
{
	uint32_t firstSeq;
	uint32_t time;
	uint32_t rxFreq;
	uint32_t frames;
	uint16_t firstSector;
	uint8_t  callerType;
	char     caller[17];
	char     name[17];
} vrEntry_t;

__attribute__((section(".ccmram"))) static vrEntry_t vrList[VR_LIST_MAX];
__attribute__((section(".ccmram"))) static vrEntry_t vrPlayEntry;
__attribute__((section(".ccmram"))) static int16_t vrPcm[VR_PCM_FRAMES][WAV_BUFFER_SIZE];
__attribute__((section(".ccmram"))) static uint8_t vrPend[VR_PEND_FRAMES * VR_FRAME_BYTES];
__attribute__((section(".ccmram"))) static uint8_t vrPage[VR_PAGE_SIZE];
__attribute__((section(".ccmram"))) static uint8_t vrPatch[VR_PAGE_SIZE];
__attribute__((section(".ccmram"))) static char vrRecCaller[17];
__attribute__((section(".ccmram"))) static char vrRecName[17];

static voiceRecorderStatus_t vrStatus = VOICE_RECORDER_STATUS_STARTING;
static int vrListCount = 0;
static int vrWriteSector = 0;     // the next sector a recording will take
static uint32_t vrNextSeq = 1;
static int vrErasedAhead = 0;     // erased sectors from vrWriteSector on
static uint32_t vrLastEraseMs = 0;
static bool vrSignal = false;

static volatile bool vrRecording = false;
static volatile uint32_t vrPcmWr = 0; // written by the I2S interrupt
static volatile uint32_t vrPcmRd = 0;
static volatile int vrDropped = 0;
static int vrPendHead = 0;
static int vrPendCount = 0;
static uint32_t vrRecFrames = 0;  // encoded in this recording
static uint32_t vrRecWritten = 0; // of those, in the flash
static bool vrCommitted = false;  // its first sector has been started
static int vrCurSector = 0;
static int vrFirstSector = 0;
static uint32_t vrFirstSeq = 0;
static int vrPart = 0;
static uint32_t vrCurFrames = 0;  // frames in the current sector
static int vrPageIndex = 0;       // page of the current sector being staged
static int vrPageFill = 0;
static uint32_t vrRecTime = 0;
static uint32_t vrRecFreq = 0;
static uint8_t vrRecCallerType = 0xFF;
static bool vrCallerWritten = false;
static uint32_t vrLastSignalMs = 0;
static uint32_t vrLastRecEndMs = 0;
static bool vrLastRecListed = false;

static bool vrPlaying = false;
static int vrPlayIndex = -1;
static uint32_t vrPlayFrame = 0;
static uint32_t vrPlayTotal = 0;

static uint32_t vrUsbLastFrame = 0xFFFFFFFFU;
static uint32_t vrUsbLastMs = 0;
static bool vrUsbLive = false;

// ---------------------------------------------------------------------------
// Guards
// ---------------------------------------------------------------------------

// A computer is on the USB cable if it is sending start-of-frame packets, one a
// millisecond, which the USB core counts in its status register. The stack's own
// "configured" state stays set after the cable comes out on this radio, which has
// no VBUS sensing, so it cannot be used for this.
static bool vrUsbIsLive(void)
{
	uint32_t now = ticksGetMillis();

	if ((uint32_t)(now - vrUsbLastMs) >= 20U)
	{
		USB_OTG_DeviceTypeDef *dev = (USB_OTG_DeviceTypeDef *)(USB_OTG_FS_PERIPH_BASE + USB_OTG_DEVICE_BASE);
		uint32_t frame = (dev->DSTS & USB_OTG_DSTS_FNSOF);

		vrUsbLive = ((vrUsbLastFrame != 0xFFFFFFFFU) && (frame != vrUsbLastFrame));
		vrUsbLastFrame = frame;
		vrUsbLastMs = now;
	}

	return vrUsbLive;
}

static bool vrFlashIsRightSize(void)
{
	return ((flashChipPartNumber == 0x4018U) || (flashChipPartNumber == 0x7018U)); // 128Mbit, 16MB
}

// Checked before every write and erase, not just once: the CPS can write a new
// DMR ID database at any time.
static bool vrSpaceIsOurs(void)
{
	return (vrFlashIsRightSize() && (dmrIDDatabaseEndAddress() <= VOICE_RECORDER_FLASH_START) && (vrUsbIsLive() == false));
}

static uint32_t vrSectorAddr(int s)
{
	return (VOICE_RECORDER_FLASH_START + ((uint32_t) s * VR_SECTOR_SIZE));
}

static bool vrInSpace(uint32_t addr, uint32_t len)
{
	return ((addr >= VOICE_RECORDER_FLASH_START) && ((addr + len) <= VOICE_RECORDER_FLASH_END));
}

static bool vrReadHeader(int s, vrHeader_t *h)
{
	return SPI_Flash_read(vrSectorAddr(s), (uint8_t *) h, sizeof(vrHeader_t));
}

static bool vrHeaderIsBlank(const vrHeader_t *h)
{
	const uint8_t *p = (const uint8_t *) h;

	for (size_t i = 0; i < sizeof(vrHeader_t); i++)
	{
		if (p[i] != 0xFF)
		{
			return false;
		}
	}

	return true;
}

static bool vrProgramPage(uint32_t page, uint8_t *data)
{
	if (((page % VR_PAGE_SIZE) != 0) || !vrInSpace(page, VR_PAGE_SIZE) || !vrSpaceIsOurs())
	{
		return false;
	}

	return SPI_Flash_writePage(page, data);
}

// A few bytes inside one page, the rest of it left as it is - 0xFF around them
// changes nothing, as programming only takes bits from 1 to 0.
static bool vrProgramBytes(uint32_t addr, const void *data, uint32_t len)
{
	uint32_t page = (addr & ~(VR_PAGE_SIZE - 1U));
	uint32_t off = (addr - page);

	if ((off + len) > VR_PAGE_SIZE)
	{
		return false;
	}

	memset(vrPatch, 0xFF, sizeof(vrPatch));
	memcpy(&vrPatch[off], data, len);

	return vrProgramPage(page, vrPatch);
}

static int vrEntrySectors(const vrEntry_t *e)
{
	int n = (int)((e->frames + VR_FRAMES_PER_SECTOR - 1U) / VR_FRAMES_PER_SECTOR);

	return ((n < 1) ? 1 : n);
}

// A recording that loses any of its sectors is no longer whole: off the list.
static void vrForgetSector(int s)
{
	int i = 0;

	while (i < vrListCount)
	{
		int d = (((s - (int) vrList[i].firstSector) + VR_SECTORS) % VR_SECTORS);

		if (d < vrEntrySectors(&vrList[i]))
		{
			memmove(&vrList[i], &vrList[i + 1], ((size_t)((vrListCount - 1) - i) * sizeof(vrEntry_t)));
			vrListCount--;
		}
		else
		{
			i++;
		}
	}
}

static bool vrErase(int s)
{
	if ((s < 0) || (s >= VR_SECTORS) || !vrSpaceIsOurs())
	{
		return false;
	}

	vrForgetSector(s);

	return SPI_Flash_eraseSector(vrSectorAddr(s));
}

static bool vrI2SClockIsRunning(void)
{
	GPIO_PinState first = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15);
	uint32_t start = ticksGetMillis();

	while ((uint32_t)(ticksGetMillis() - start) < VR_WS_PROBE_MS)
	{
		if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15) != first)
		{
			return true;
		}
	}

	return false;
}

// ---------------------------------------------------------------------------
// Finding the recordings again
// ---------------------------------------------------------------------------

// Frames in a sector that was never closed - the power went during a recording.
static uint32_t vrCountFrames(int s)
{
	uint8_t buf[VR_FRAME_BYTES * 7];
	uint32_t n = 0;

	for (uint32_t off = VR_HEADER_SIZE; n < VR_FRAMES_PER_SECTOR; off += sizeof(buf))
	{
		if (!SPI_Flash_read((vrSectorAddr(s) + off), buf, sizeof(buf)))
		{
			break;
		}

		for (int f = 0; (f < 7) && (n < VR_FRAMES_PER_SECTOR); f++)
		{
			bool blank = true;

			for (int b = 0; b < (int) VR_FRAME_BYTES; b++)
			{
				if (buf[(f * VR_FRAME_BYTES) + b] != 0xFF)
				{
					blank = false;
					break;
				}
			}

			if (blank)
			{
				return n;
			}
			n++;
		}
	}

	return n;
}

static void vrCopyCaller(char *dst, const char *src)
{
	int i = 0;

	while ((i < 16) && (src[i] != 0) && ((uint8_t) src[i] != 0xFF))
	{
		dst[i] = src[i];
		i++;
	}
	dst[i] = 0;
}

// Newest first: back from the sector after the newest, through the whole ring.
static void vrBuildList(void)
{
	vrHeader_t h;
	vrHeader_t g;

	vrListCount = 0;

	for (int k = 1; (k <= VR_SECTORS) && (vrListCount < VR_LIST_MAX); k++)
	{
		int s = (((vrWriteSector - k) + VR_SECTORS) % VR_SECTORS);
		uint32_t frames = 0;
		int t = s;

		if (!vrReadHeader(s, &h) || (h.magic != VR_MAGIC) || (h.part != 0) || ((h.flags & VR_FLAG_LIVE) == 0))
		{
			continue;
		}

		for (int n = 0; n < VR_SECTORS; n++)
		{
			if (!vrReadHeader(t, &g) || (g.magic != VR_MAGIC) || (g.firstSeq != h.firstSeq))
			{
				break;
			}
			frames += ((g.frames == 0xFFFFU) ? vrCountFrames(t) : g.frames);
			t = ((t + 1) % VR_SECTORS);
		}

		if (frames < 3)
		{
			continue;
		}

		vrList[vrListCount].firstSeq = h.firstSeq;
		vrList[vrListCount].time = h.time;
		vrList[vrListCount].rxFreq = h.rxFreq;
		vrList[vrListCount].frames = frames;
		vrList[vrListCount].firstSector = (uint16_t) s;
		vrList[vrListCount].callerType = h.callerType;
		vrCopyCaller(vrList[vrListCount].caller, h.caller);
		vrCopyCaller(vrList[vrListCount].name, h.name);
		vrListCount++;
	}
}

static void vrInit(void)
{
	vrHeader_t h;
	uint32_t maxSeq = 0;
	int maxSector = -1;

	if (!vrFlashIsRightSize())
	{
		vrStatus = VOICE_RECORDER_STATUS_NO_FLASH;
		return;
	}

	if (dmrIDDatabaseEndAddress() > VOICE_RECORDER_FLASH_START)
	{
		vrStatus = VOICE_RECORDER_STATUS_DMRID_OVERLAP;
		return;
	}

	for (int s = 0; s < VR_SECTORS; s++)
	{
		if (vrReadHeader(s, &h) && (h.magic == VR_MAGIC) && ((maxSector < 0) || (h.seq > maxSeq)))
		{
			maxSeq = h.seq;
			maxSector = s;
		}
	}

	vrWriteSector = ((maxSector < 0) ? 0 : ((maxSector + 1) % VR_SECTORS));
	vrNextSeq = (maxSeq + 1U);
	vrErasedAhead = 0; // counted, sector by sector, by the idle tick

	vrBuildList();

	vrStatus = VOICE_RECORDER_STATUS_OK;
}

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------

// I2S receive interrupt: one call is 20ms, two halves of 80 left-channel samples.
bool voiceRecorderAudioBlock(uint32_t bufNum)
{
	int16_t *dst;

	if (!vrRecording)
	{
		return false;
	}

	if ((vrPcmWr - vrPcmRd) >= VR_PCM_FRAMES)
	{
		vrDropped++; // the encoder has fallen behind
		return true;
	}

	dst = vrPcm[vrPcmWr % VR_PCM_FRAMES];

	for (int h = 0; h < 2; h++)
	{
		for (int i = 0; i < WAV_BUFFER_SIZE; i += 2)
		{
			*dst++ = (int16_t) i2s_Rx_Buffer[bufNum][h][i];
		}
	}

	vrPcmWr++;
	return true;
}

static uint8_t *vrPendSlot(int i)
{
	return &vrPend[((vrPendHead + i) % VR_PEND_FRAMES) * VR_FRAME_BYTES];
}

static void vrPageReset(void)
{
	memset(vrPage, 0xFF, sizeof(vrPage));
	vrPageFill = 0;
}

static bool vrFlushPage(void)
{
	if (vrPageFill == 0)
	{
		return true;
	}

	return vrProgramPage((vrSectorAddr(vrCurSector) + ((uint32_t) vrPageIndex * VR_PAGE_SIZE)), vrPage);
}

static bool vrBeginSector(bool first)
{
	vrHeader_t h;
	int s = vrWriteSector;

	if (!first && (s == vrFirstSector))
	{
		return false; // the whole space is this one recording: stop here
	}

	if (vrErasedAhead > 0)
	{
		vrErasedAhead--;
	}
	else if (!vrErase(s))
	{
		return false;
	}

	vrWriteSector = ((vrWriteSector + 1) % VR_SECTORS);
	vrCurSector = s;

	memset(&h, 0xFF, sizeof(h));
	h.magic = VR_MAGIC;
	h.seq = vrNextSeq++;

	if (first)
	{
		vrFirstSeq = h.seq;
		vrFirstSector = s;
		vrPart = 0;

		if (vrRecCallerType != 0xFF)
		{
			size_t len = strlen(vrRecCaller);

			h.callerType = vrRecCallerType;
			memcpy(h.caller, vrRecCaller, ((len > 16) ? 16 : len));
			if (len < 16)
			{
				h.caller[len] = 0;
			}
			vrCallerWritten = true;
		}
	}
	else
	{
		vrPart++;
	}

	if (first && (vrRecName[0] != 0))
	{
		size_t len = strlen(vrRecName);

		memcpy(h.name, vrRecName, ((len > 16) ? 16 : len));
		if (len < 16)
		{
			h.name[len] = 0;
		}
	}

	h.firstSeq = vrFirstSeq;
	h.part = (uint8_t)((vrPart > 255) ? 255 : vrPart);
	h.time = vrRecTime;
	h.rxFreq = vrRecFreq;

	vrPageReset();
	memcpy(vrPage, &h, sizeof(h));
	vrPageFill = VR_HEADER_SIZE;
	vrPageIndex = 0;
	vrCurFrames = 0;

	return true;
}

static bool vrFinishSector(void)
{
	uint16_t frames = (uint16_t) vrCurFrames;
	bool ok = vrFlushPage();

	vrPageReset();

	return (vrProgramBytes((vrSectorAddr(vrCurSector) + offsetof(vrHeader_t, frames)), &frames, sizeof(frames)) && ok);
}

static bool vrAppendFrame(const uint8_t *frame)
{
	if (vrCurFrames >= VR_FRAMES_PER_SECTOR)
	{
		if (!vrFinishSector() || !vrBeginSector(false))
		{
			return false;
		}
	}

	for (int b = 0; b < (int) VR_FRAME_BYTES; b++)
	{
		vrPage[vrPageFill++] = frame[b];

		if (vrPageFill == (int) VR_PAGE_SIZE)
		{
			if (!vrFlushPage())
			{
				return false;
			}
			vrPageIndex++;
			vrPageReset();
		}
	}

	vrCurFrames++;
	vrRecWritten++;
	return true;
}

// Moves encoded frames into the flash, holding back the last few: they may turn
// out to be the squelch tail. Nothing is written until the recording is a second
// long, so a repeater being keyed up never costs any flash.
static bool vrWriter(int keep)
{
	if (!vrCommitted)
	{
		if (vrRecFrames < VR_MIN_FRAMES)
		{
			return true;
		}
		if (!vrBeginSector(true))
		{
			return false;
		}
		vrCommitted = true;
	}

	while (vrPendCount > keep)
	{
		if (!vrAppendFrame(vrPendSlot(0)))
		{
			return false;
		}
		vrPendHead = ((vrPendHead + 1) % VR_PEND_FRAMES);
		vrPendCount--;
	}

	return true;
}

static void vrStartRecording(void)
{
	vrPcmRd = vrPcmWr;
	vrPendHead = 0;
	vrPendCount = 0;
	vrRecFrames = 0;
	vrRecWritten = 0;
	vrCommitted = false;
	vrRecTime = uiDataGlobal.dateTimeSecs;
	vrRecFreq = currentChannelData->rxFreq;
	vrRecCallerType = 0xFF;
	vrRecCaller[0] = 0;
	vrCallerWritten = false;
	vrLastSignalMs = ticksGetMillis();

	// The channel's name, so the list says "KATRAD" rather than a bare frequency
	// (the first version only showed frequencies, and I kept having to remember
	// which repeater was which). The VFO has no name, so there it is the frequency.
	vrRecName[0] = 0;
	if ((currentChannelData != &settingsVFOChannel[0]) && (currentChannelData != &settingsVFOChannel[1]))
	{
		codeplugUtilConvertBufToString(currentChannelData->name, vrRecName, 16);
	}

	codecInitInternalBuffers(); // a fresh encoder for each recording

	vrRecording = true; // the interrupt starts handing blocks over from here
}

static void vrListAddNewest(void)
{
	if (vrListCount >= VR_LIST_MAX)
	{
		vrListCount = (VR_LIST_MAX - 1);
	}

	memmove(&vrList[1], &vrList[0], ((size_t) vrListCount * sizeof(vrEntry_t)));

	vrList[0].firstSeq = vrFirstSeq;
	vrList[0].time = vrRecTime;
	vrList[0].rxFreq = vrRecFreq;
	vrList[0].frames = vrRecWritten;
	vrList[0].firstSector = (uint16_t) vrFirstSector;
	vrList[0].callerType = vrRecCallerType;
	snprintf(vrList[0].caller, sizeof(vrList[0].caller), "%s", vrRecCaller);
	snprintf(vrList[0].name, sizeof(vrList[0].name), "%s", vrRecName);
	vrListCount++;
}

static void vrStopRecording(void)
{
	int drop;

	vrRecording = false;

	// The squelch tail - the last few frames - is noise.
	drop = ((vrPendCount < VR_TAIL_FRAMES) ? vrPendCount : VR_TAIL_FRAMES);
	vrPendCount -= drop;
	vrRecFrames -= (uint32_t) drop;

	vrLastRecListed = false;

	if (vrCommitted || (vrRecFrames >= VR_MIN_FRAMES))
	{
		(void) vrWriter(0);

		if (vrCommitted)
		{
			(void) vrFinishSector();

			if (vrRecWritten >= 3)
			{
				vrListAddNewest();
				vrLastRecListed = true;
			}
		}
	}

	vrPendCount = 0;
	vrLastRecEndMs = ticksGetMillis();

	codecInitInternalBuffers(); // leave the encoder fresh, as DMR expects to find it
}

static void vrRecordTick(void)
{
	// One frame a pass - about 11ms - so the main loop, which also runs the
	// squelch, the keys and the screen, is never held up for longer than that.
	// The loop goes round well over 50 times a second, so it still keeps up; the
	// interrupt's queue takes up any slack.
	for (int n = 0; (n < 1) && (vrPcmRd != vrPcmWr); n++)
	{
		if (vrPendCount < VR_PEND_FRAMES)
		{
			codecEncodeBlockFromPCM(vrPendSlot(vrPendCount), (const uint8_t *) vrPcm[vrPcmRd % VR_PCM_FRAMES]);
			vrPendCount++;
			vrRecFrames++;
		}
		else
		{
			vrDropped++; // the flash has stalled
		}
		vrPcmRd++;
	}

	if (!vrWriter(VR_TAIL_FRAMES))
	{
		vrStopRecording();
	}
}

void voiceRecorderNoteCaller(uint8_t type, const char *code)
{
	uint8_t field[20];
	size_t len;

	if ((vrStatus != VOICE_RECORDER_STATUS_OK) || (code == NULL) || (code[0] == 0))
	{
		return;
	}

	len = strlen(code);
	memset(field, 0xFF, sizeof(field));
	field[0] = type;
	memcpy(&field[4], code, ((len > 16) ? 16 : len));
	if (len < 16)
	{
		field[4 + len] = 0;
	}

	if (vrRecording)
	{
		if (vrRecCallerType != 0xFF)
		{
			return; // the first code of a transmission is the caller
		}

		vrRecCallerType = type;
		snprintf(vrRecCaller, sizeof(vrRecCaller), "%s", code);

		if (vrCommitted && !vrCallerWritten)
		{
			if ((vrCurSector == vrFirstSector) && (vrPageIndex == 0))
			{
				memcpy(&vrPage[offsetof(vrHeader_t, callerType)], field, sizeof(field)); // still being staged
			}
			else
			{
				(void) vrProgramBytes((vrSectorAddr(vrFirstSector) + offsetof(vrHeader_t, callerType)), field, sizeof(field));
			}
			vrCallerWritten = true;
		}
		return;
	}

	// A DTMF code is only logged once it has ended, often after the squelch has
	// closed - so it can still belong to the recording that has just finished.
	if (vrLastRecListed && (vrListCount > 0) && (vrList[0].callerType == 0xFF) &&
			((uint32_t)(ticksGetMillis() - vrLastRecEndMs) < VR_LATE_CALLER_MS))
	{
		if (vrProgramBytes((vrSectorAddr(vrList[0].firstSector) + offsetof(vrHeader_t, callerType)), field, sizeof(field)))
		{
			vrList[0].callerType = type;
			snprintf(vrList[0].caller, sizeof(vrList[0].caller), "%s", code);
		}
		vrLastRecListed = false;
	}
}

// ---------------------------------------------------------------------------
// Playback - the voice prompt path
// ---------------------------------------------------------------------------
static uint32_t vrFrameAddr(int firstSector, uint32_t k)
{
	int s = ((firstSector + (int)(k / VR_FRAMES_PER_SECTOR)) % VR_SECTORS);

	return (vrSectorAddr(s) + VR_HEADER_SIZE + ((k % VR_FRAMES_PER_SECTOR) * VR_FRAME_BYTES));
}

bool voiceRecorderPlay(int index)
{
	if ((vrStatus != VOICE_RECORDER_STATUS_OK) || (index < 0) || (index >= vrListCount) || vrRecording || vrPlaying || vrSignal ||
			(trxGetMode() != RADIO_MODE_ANALOG) || trxTransmissionEnabled || trxIsTransmitting || voicePromptsIsPlaying() || vrUsbIsLive())
	{
		return false;
	}

	vrPlayEntry = vrList[index];
	vrPlayIndex = index;
	vrPlayFrame = 0;
	vrPlayTotal = ((vrPlayEntry.frames / 3U) * 3U); // decoded three at a time, as a DMR burst

	if (vrPlayTotal == 0)
	{
		return false;
	}

	vrPlaying = true; // first, so the five-tone capture lets go and stays off
	fiveToneDecodeReleaseCapture();
	rxPowerSavingSetState(ECOPHASE_POWERSAVE_INACTIVE);

	taskENTER_CRITICAL();
	radioSetAudioPath(false); // C6000 -> audio amp, as a voice prompt
	enableAudioAmp(AUDIO_AMP_MODE_PROMPT);
	codecInit(true);
	taskEXIT_CRITICAL();

	// Starting the I2S link waits for its clock with no time limit; make sure it
	// is there first rather than risk the radio hanging.
	if (!vrI2SClockIsRunning())
	{
		voiceRecorderStop();
		return false;
	}

	return true;
}

void voiceRecorderStop(void)
{
	if (!vrPlaying)
	{
		return;
	}

	disableAudioAmp(AUDIO_AMP_MODE_PROMPT);

	taskENTER_CRITICAL();
	soundTerminateSound();
	codecInit(true);
	taskEXIT_CRITICAL();

	if (trxCarrierDetected(RADIO_DEVICE_PRIMARY))
	{
		radioSetAudioPath(true); // back to the receiver
	}

	vrPlaying = false;
	vrPlayIndex = -1;
}

static void vrPlayTick(void)
{
	if (vrPlayFrame < vrPlayTotal)
	{
		if (wavbuffer_count <= WAV_BUFFER_AMBE_PREBUFFERING_COUNT)
		{
			uint8_t buf[VR_FRAME_BYTES * 3];

			for (uint32_t f = 0; f < 3; f++)
			{
				(void) SPI_Flash_read(vrFrameAddr(vrPlayEntry.firstSector, (vrPlayFrame + f)), &buf[f * VR_FRAME_BYTES], VR_FRAME_BYTES);
			}

			taskENTER_CRITICAL();
			codecDecode(buf, 3);
			soundTickRXBuffer();
			taskEXIT_CRITICAL();

			vrPlayFrame += 3;
		}
		else
		{
			taskENTER_CRITICAL();
			soundTickRXBuffer();
			taskEXIT_CRITICAL();
		}
	}
	else if (wavbuffer_count == 0)
	{
		voiceRecorderStop();
	}
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------
void voiceRecorderTick(bool hasSignal)
{
	uint32_t now = ticksGetMillis();
	bool usb;
	bool canRecord;

	if (vrStatus == VOICE_RECORDER_STATUS_STARTING)
	{
		if (now >= VR_START_DELAY_MS)
		{
			vrInit();
		}
		return;
	}

	if (vrStatus != VOICE_RECORDER_STATUS_OK)
	{
		return;
	}

	vrSignal = hasSignal;
	usb = vrUsbIsLive();

	if (vrPlaying)
	{
		// A live transmission comes first: the playback stops and that is heard,
		// and recorded, instead.
		if (hasSignal || trxTransmissionEnabled || trxIsTransmitting || (trxGetMode() != RADIO_MODE_ANALOG) || usb)
		{
			voiceRecorderStop();
		}
		else
		{
			vrPlayTick();
		}
		return;
	}

	canRecord = (voiceRecorderGetEnabled() && (usb == false) && (trxGetMode() == RADIO_MODE_ANALOG) &&
			(trxTransmissionEnabled == false) && (trxIsTransmitting == false) && (voicePromptsIsPlaying() == false) &&
			(currentChannelData != NULL) && (currentChannelData->rxFreq != VR_APRS_FREQ));

	if (vrRecording)
	{
		if (hasSignal)
		{
			vrLastSignalMs = now;
		}

		if (!canRecord || ((uint32_t)(now - vrLastSignalMs) > VR_HANG_MS))
		{
			vrStopRecording();
		}
		else
		{
			vrRecordTick();
		}
		return;
	}

	if (canRecord && hasSignal)
	{
		vrStartRecording();
		return;
	}

	// Idle: keep sectors erased ahead of the next recording, one at a time.
	if (voiceRecorderGetEnabled() && (usb == false) && (hasSignal == false) && (vrErasedAhead < VR_PREERASE) &&
			((uint32_t)(now - vrLastEraseMs) >= VR_ERASE_GAP_MS))
	{
		int s = ((vrWriteSector + vrErasedAhead) % VR_SECTORS);
		vrHeader_t h;

		vrLastEraseMs = now;

		if (vrReadHeader(s, &h) && vrHeaderIsBlank(&h))
		{
			vrErasedAhead++;
		}
		else if (vrErase(s))
		{
			vrErasedAhead++;
		}
	}
}

// ---------------------------------------------------------------------------
// The rest of the interface
// ---------------------------------------------------------------------------
voiceRecorderStatus_t voiceRecorderGetStatus(void)
{
	return vrStatus;
}

bool voiceRecorderGetEnabled(void)
{
	return settingsIsOptionBitSet(BIT_RX_RECORDER);
}

void voiceRecorderSetEnabled(bool enabled)
{
	settingsSetOptionBit(BIT_RX_RECORDER, enabled);

	if (!enabled && vrRecording)
	{
		vrStopRecording();
	}
}

bool voiceRecorderIsRecording(void)
{
	return vrRecording;
}

bool voiceRecorderIsPlaying(void)
{
	return vrPlaying;
}

int voiceRecorderDroppedFrames(void)
{
	return vrDropped;
}

int voiceRecorderCount(void)
{
	return vrListCount;
}

bool voiceRecorderGet(int index, voiceRecorderInfo_t *info)
{
	if ((index < 0) || (index >= vrListCount))
	{
		return false;
	}

	info->time = vrList[index].time;
	info->rxFreq = vrList[index].rxFreq;
	info->frames = vrList[index].frames;
	info->callerType = vrList[index].callerType;
	snprintf(info->caller, sizeof(info->caller), "%s", vrList[index].caller);
	snprintf(info->channel, sizeof(info->channel), "%s", vrList[index].name);
	return true;
}

int voiceRecorderPlayingIndex(void)
{
	return (vrPlaying ? vrPlayIndex : -1);
}

uint32_t voiceRecorderPlayedFrames(void)
{
	return (vrPlaying ? vrPlayFrame : 0);
}

bool voiceRecorderDelete(int index)
{
	uint8_t flags = (uint8_t) ~VR_FLAG_LIVE;

	if ((index < 0) || (index >= vrListCount))
	{
		return false; // the one being recorded is not on the list yet, so any listed one can go
	}

	if (vrPlaying)
	{
		voiceRecorderStop();
	}

	if (!vrProgramBytes((vrSectorAddr(vrList[index].firstSector) + offsetof(vrHeader_t, flags)), &flags, 1))
	{
		return false;
	}

	memmove(&vrList[index], &vrList[index + 1], ((size_t)((vrListCount - 1) - index) * sizeof(vrEntry_t)));
	vrListCount--;
	return true;
}

// Every recording in the space, not only the listed ones, so none comes back
// after the next power-on.
void voiceRecorderDeleteAll(void)
{
	vrHeader_t h;
	uint8_t flags = (uint8_t) ~VR_FLAG_LIVE;

	if ((vrStatus != VOICE_RECORDER_STATUS_OK) || vrRecording)
	{
		return;
	}

	if (vrPlaying)
	{
		voiceRecorderStop();
	}

	for (int s = 0; s < VR_SECTORS; s++)
	{
		if (vrReadHeader(s, &h) && (h.magic == VR_MAGIC) && (h.part == 0) && ((h.flags & VR_FLAG_LIVE) != 0))
		{
			(void) vrProgramBytes((vrSectorAddr(s) + offsetof(vrHeader_t, flags)), &flags, 1);
		}
	}

	vrListCount = 0;
}
