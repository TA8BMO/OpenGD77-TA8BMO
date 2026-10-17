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
// Morse (CW) receive decoder.
//
// Listens to the same captured receive audio as the five-tone decoder (how that
// audio gets to the MCU at all is explained at the top of fiveToneDecode.c).
// That module owns the codec and the I2S transfer and hands me the samples from
// its tap, so there is only ever one capture and nothing to fight over.
//
// The sender never tells you three things, so the decoder has to work them out:
//
//  - Pitch. People send anywhere from about 400 to 1100Hz. A small bank of
//    Goertzel filters covers that and I follow the strongest bin, so it tunes
//    itself to whoever is on.
//  - Level. The keying envelope is the magnitude of that bin. The threshold
//    follows the noise floor and the peaks instead of being fixed, with some
//    hysteresis so a fading signal does not chatter.
//  - Speed. Nobody sends at an exact WPM and hand sending drifts. The dot length
//    is re-estimated all the time from the marks that actually come in, so it
//    follows the operator when they speed up or slow down.
//
// Honest note: clean machine-sent Morse decodes nicely, hand sending in noise
// gets letters wrong. The CW readers in the big transceivers do the same - that
// is just Morse off the air, not something more code would cure.
// ---------------------------------------------------------------------------
#include <math.h>
#include "functions/morseDecode.h"
#include "functions/fiveToneDecode.h"
#include "functions/dtmfDecode.h"
#include "functions/ticks.h"
#include "hardware/EEPROM.h"
#include "interfaces/i2s.h"
#include "functions/sound.h"
#include "user_interface/uiGlobals.h"

// 8ms analysis blocks: at 20WPM a dot is 60ms, so even fast sending gives
// several blocks per element. Resolution is 125Hz, which is ample for picking
// which of the pitch bins an operator is using.
#define CW_BLOCK_N        64
#define CW_BLOCK_MS       8
#define CW_BIN_COUNT      6
#define CW_SAMPLE_RATE    8000.0f

// A dash at the slowest speed we accept. Anything longer is not a Morse element
// - most obviously a five-tone burst, which is five contiguous 100ms tones and
// therefore one unbroken 500ms mark.
#define CW_MARK_MAX_BLOCKS (CW_DOT_MAX_BLOCKS * 3)

// Discard a decoded transmission unless it looks like Morse: speech opens and
// closes the squelch in patterns that match nothing, so it would otherwise fill
// the log with rubbish on every conversation.
#define CW_MIN_CHARS       2
#define CW_MAX_UNKNOWN_PCT 25

// Timing limits, in blocks. Anything outside these is not Morse.
#define CW_DOT_MIN_BLOCKS  4    // 32ms  - about 37WPM
#define CW_DOT_MAX_BLOCKS  25   // 200ms - about 6WPM
#define CW_DOT_INIT        10   // 80ms  - roughly 15WPM to start from

#define CW_IDLE_COMMIT_MS  3000 // silence that ends a transmission
#define CW_SYMBOL_MAX      8

#define CW_STORE_EEPROM_ADDR 0x6400 // free space, same 4K sector as the settings
#define CW_STORE_MAGIC       0x574332U
#define CW_STORE_VERSION     1

// Kept below 930Hz, the lowest CCIR five-tone frequency, so a selective call
// does not leak into the top bin and get read as keying.
static const uint16_t cwBinFreq[CW_BIN_COUNT] = { 400, 500, 600, 700, 800, 900 };

// Pattern table. Order matters only in that it is searched linearly.
static const char * const cwPatterns[] =
{
	".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---",
	"-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-",
	"..-", "...-", ".--", "-..-", "-.--", "--..",
	"-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----.",
	".-.-.-", "--..--", "..--..", "-..-.", "-...-", ".-.-.", "---...", "-....-"
};
static const char cwChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,?/=+:-";

typedef struct
{
	uint32_t freq;
	uint8_t  used;
	uint8_t  len;
	char     text[CW_LOG_TEXT_LEN + 1];
} cwLogEntry_t;

typedef union
{
	struct
	{
		uint32_t     magic;
		uint16_t     version;
		uint16_t     count;
		cwLogEntry_t entry[CW_LOG_ENTRIES];
		uint32_t     checksum;
	} d;
	uint8_t raw[288];
} cwStore_t;

// Main RAM is pretty much full on this build, so the log lives in CCMRAM.
__attribute__((section(".ccmram"))) static cwStore_t cwStore;
static bool cwStoreLoaded = false;

static float    cwCoeff[CW_BIN_COUNT];
static float    cwS1[CW_BIN_COUNT];
static float    cwS2[CW_BIN_COUNT];
static bool     cwCoeffReady = false;
static int      cwBlockPos = 0;

static int      cwLockedBin = -1;
static float    cwFloor = 0.0f;
static float    cwPeak = 0.0f;
static bool     cwKeyDown = false;
static int      cwRunBlocks = 0;

static int      cwDotBlocks = CW_DOT_INIT;
static char     cwSymbol[CW_SYMBOL_MAX + 1];
static int      cwSymbolLen = 0;

// Written by the audio interrupt, read by the main loop. Kept as a plain buffer
// with a length, and only ever appended to from the interrupt.
static volatile char cwLive[CW_LOG_TEXT_LEN + 1];
static volatile int  cwLiveLen = 0;
static volatile uint32_t cwLastActivityMs = 0;
static volatile bool cwHasActivity = false;

static uint32_t cwWatchedFreq = 0;

// ---------------------------------------------------------------------------
// Storage
// ---------------------------------------------------------------------------
static uint32_t cwChecksum(void)
{
	uint32_t sum = 0x5A5A5A5AU;

	for (int i = 0; i < (int)(sizeof(cwStore.raw) - sizeof(uint32_t)); i++)
	{
		sum = ((sum << 1) | (sum >> 31)) ^ (uint32_t) cwStore.raw[i];
	}
	return sum;
}

static void cwStoreReset(void)
{
	memset(&cwStore, 0, sizeof(cwStore));
	cwStore.d.magic = CW_STORE_MAGIC;
	cwStore.d.version = CW_STORE_VERSION;
}

static void cwStoreLoad(void)
{
	if (cwStoreLoaded)
	{
		return;
	}
	cwStoreLoaded = true;

	if (EEPROM_Read(CW_STORE_EEPROM_ADDR, cwStore.raw, sizeof(cwStore.raw)))
	{
		uint32_t stored = cwStore.d.checksum;

		cwStore.d.checksum = 0;
		if ((cwStore.d.magic == CW_STORE_MAGIC) && (cwStore.d.version == CW_STORE_VERSION) &&
				(stored == cwChecksum()))
		{
			cwStore.d.checksum = stored;
			return;
		}
	}

	cwStoreReset();
}

static void cwStoreSave(void)
{
	cwStore.d.magic = CW_STORE_MAGIC;
	cwStore.d.version = CW_STORE_VERSION;
	cwStore.d.checksum = 0;
	cwStore.d.checksum = cwChecksum();

	(void) EEPROM_Write(CW_STORE_EEPROM_ADDR, cwStore.raw, sizeof(cwStore.raw));
}

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------
static void cwAppendLive(char c)
{
	if (cwLiveLen < CW_LOG_TEXT_LEN)
	{
		cwLive[cwLiveLen++] = c;
		cwLive[cwLiveLen] = 0;
	}
	cwHasActivity = true;
}

static void cwFlushSymbol(void)
{
	if (cwSymbolLen == 0)
	{
		return;
	}

	cwSymbol[cwSymbolLen] = 0;

	for (int i = 0; i < (int)(sizeof(cwPatterns) / sizeof(cwPatterns[0])); i++)
	{
		if (strcmp(cwSymbol, cwPatterns[i]) == 0)
		{
			cwAppendLive(cwChars[i]);
			cwSymbolLen = 0;
			return;
		}
	}

	// Nothing matched - a corrupted element run. Mark it rather than dropping
	// it silently, so a bad copy is visible as such instead of looking clean.
	cwAppendLive('*');
	cwSymbolLen = 0;
}

// A mark (key down) just ended, lasting `blocks`.
static void cwMarkEnded(int blocks)
{
	if ((blocks < CW_DOT_MIN_BLOCKS) || (blocks > CW_MARK_MAX_BLOCKS))
	{
		// Too short to be an element, or far too long to be one. Either way it
		// must not be allowed near the speed estimate: a single over-long mark
		// used to drag the dot length up and leave the decoder unable to find a
		// character boundary again, so nothing was logged from then on.
		cwSymbolLen = 0;
		return;
	}

	if (blocks < (cwDotBlocks * 2))
	{
		// A dot. Ease the estimate towards it so the decoder follows the sender.
		cwDotBlocks = (((cwDotBlocks * 3) + blocks) / 4);
		if (cwSymbolLen < CW_SYMBOL_MAX)
		{
			cwSymbol[cwSymbolLen++] = '.';
		}
	}
	else
	{
		// A dash is three dots long, so it also tells us the dot length.
		int impliedDot = (blocks / 3);

		if ((impliedDot >= CW_DOT_MIN_BLOCKS) && (impliedDot <= CW_DOT_MAX_BLOCKS))
		{
			cwDotBlocks = (((cwDotBlocks * 3) + impliedDot) / 4);
		}
		if (cwSymbolLen < CW_SYMBOL_MAX)
		{
			cwSymbol[cwSymbolLen++] = '-';
		}
	}

	if (cwDotBlocks < CW_DOT_MIN_BLOCKS)
	{
		cwDotBlocks = CW_DOT_MIN_BLOCKS;
	}
	else if (cwDotBlocks > CW_DOT_MAX_BLOCKS)
	{
		cwDotBlocks = CW_DOT_MAX_BLOCKS;
	}
}

// A space (key up) has now lasted `blocks`. Called while it is still running,
// so the character and word boundaries are emitted as they are reached.
static void cwSpaceRunning(int blocks)
{
	// Between elements of one character the gap is one dot; at two dots the
	// character has ended.
	if ((cwSymbolLen > 0) && (blocks >= (cwDotBlocks * 2)))
	{
		cwFlushSymbol();
	}

	// Seven dots is a word gap; five is used here because sending is rarely
	// that precise and a missed space is more annoying than an extra one.
	if ((blocks == (cwDotBlocks * 5)) && (cwLiveLen > 0) && (cwLive[cwLiveLen - 1] != ' '))
	{
		cwAppendLive(' ');
	}
}

// Puts the detector back to a known state, so nothing learned from one
// transmission can corrupt the next.
static void cwResetDetector(void)
{
	cwDotBlocks = CW_DOT_INIT;
	cwSymbolLen = 0;
	cwLockedBin = -1;
	cwKeyDown = false;
	cwRunBlocks = 0;
	cwFloor = 0.0f;
	cwPeak = 0.0f;
}

static void cwEndBlock(void)
{
	float best = 0.0f;
	int bestIdx = -1;
	float mag[CW_BIN_COUNT];
	float level;

	// Stand down while a five-tone burst is on the air. Its tones are sent back
	// to back with no gaps, so an envelope detector sees one long mark and reads
	// it as a dash - which is where the stray "TE" entries came from. DTMF the
	// same: each digit is a steady tone pair that reads as a mark.
	if (fiveToneDecodeToneRecentlyActive() || dtmfDecodeToneRecentlyActive())
	{
		cwResetDetector();
		cwLiveLen = 0;
		cwLive[0] = 0;
		cwHasActivity = false;
		return;
	}

	for (int i = 0; i < CW_BIN_COUNT; i++)
	{
		float m = ((cwS1[i] * cwS1[i]) + (cwS2[i] * cwS2[i])) - (cwCoeff[i] * cwS1[i] * cwS2[i]);

		cwS1[i] = 0.0f;
		cwS2[i] = 0.0f;
		mag[i] = m;

		if (m > best)
		{
			best = m;
			bestIdx = i;
		}
	}

	// Follow the loudest bin only while the key is up, so a momentary dropout
	// mid-character cannot drag the decoder onto a different pitch.
	if ((cwKeyDown == false) && (bestIdx >= 0))
	{
		cwLockedBin = bestIdx;
	}
	if (cwLockedBin < 0)
	{
		return;
	}

	level = mag[cwLockedBin];

	// Envelope tracking: the floor creeps up towards quiet blocks and the peak
	// decays, so the threshold sits between them wherever the signal is.
	if (level < cwFloor)
	{
		cwFloor = level;
	}
	else
	{
		cwFloor += ((level - cwFloor) * 0.002f);
	}

	if (level > cwPeak)
	{
		cwPeak = level;
	}
	else
	{
		cwPeak -= ((cwPeak - cwFloor) * 0.002f);
	}

	{
		float span = (cwPeak - cwFloor);
		bool nowDown;

		// Without a real signal the span is tiny and everything below is noise.
		if (span < 2.0e8f)
		{
			nowDown = false;
		}
		else if (cwKeyDown)
		{
			nowDown = (level > (cwFloor + (span * 0.35f))); // hysteresis
		}
		else
		{
			nowDown = (level > (cwFloor + (span * 0.60f)));
		}

		if (nowDown != cwKeyDown)
		{
			if (cwKeyDown)
			{
				cwMarkEnded(cwRunBlocks);
			}
			cwKeyDown = nowDown;
			cwRunBlocks = 0;

			if (nowDown)
			{
				cwLastActivityMs = ticksGetMillis();
			}
		}

		cwRunBlocks++;

		if (cwKeyDown == false)
		{
			cwSpaceRunning(cwRunBlocks);
		}
	}
}

void morseDecodeAudioBlock(uint32_t bufNum)
{
	if (!cwCoeffReady)
	{
		return;
	}

	for (int h = 0; h < 2; h++)
	{
		for (int i = 0; i < WAV_BUFFER_SIZE; i += 2)
		{
			float x = (float)(int16_t) i2s_Rx_Buffer[bufNum][h][i];

			for (int b = 0; b < CW_BIN_COUNT; b++)
			{
				float s0 = ((cwCoeff[b] * cwS1[b]) - cwS2[b]) + x;

				cwS2[b] = cwS1[b];
				cwS1[b] = s0;
			}

			if (++cwBlockPos >= CW_BLOCK_N)
			{
				cwBlockPos = 0;
				cwEndBlock();
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Log
// ---------------------------------------------------------------------------
static void cwCommit(void)
{
	int slot = -1;
	int unknown = 0;
	int printable = 0;

	if ((cwLiveLen == 0) || (currentChannelData == NULL))
	{
		cwLiveLen = 0;
		cwLive[0] = 0;
		cwHasActivity = false;
		cwResetDetector();
		return;
	}

	// Was that actually Morse? Speech keys the envelope in patterns that match
	// no character, so a high proportion of unknowns means this is not worth
	// keeping - otherwise every conversation would leave junk in the log.
	for (int i = 0; i < cwLiveLen; i++)
	{
		if (cwLive[i] == '*')
		{
			unknown++;
		}
		else if (cwLive[i] != ' ')
		{
			printable++;
		}
	}

	if ((printable < CW_MIN_CHARS) ||
			(((unknown * 100) / (unknown + printable)) > CW_MAX_UNKNOWN_PCT))
	{
		cwLiveLen = 0;
		cwLive[0] = 0;
		cwHasActivity = false;
		cwResetDetector();
		return;
	}

	cwStoreLoad();

	// Prefer a free slot; otherwise drop the oldest, which is slot 0 because
	// entries are shuffled down on insert.
	for (int i = 0; i < CW_LOG_ENTRIES; i++)
	{
		if (cwStore.d.entry[i].used == 0)
		{
			slot = i;
			break;
		}
	}

	if (slot < 0)
	{
		for (int i = 0; i < (CW_LOG_ENTRIES - 1); i++)
		{
			cwStore.d.entry[i] = cwStore.d.entry[i + 1];
		}
		slot = (CW_LOG_ENTRIES - 1);
	}

	cwStore.d.entry[slot].used = 1;
	cwStore.d.entry[slot].freq = currentChannelData->rxFreq;
	cwStore.d.entry[slot].len = (uint8_t) cwLiveLen;
	memcpy(cwStore.d.entry[slot].text, (const void *)cwLive, (size_t)(cwLiveLen + 1));

	cwStoreSave();

	cwLiveLen = 0;
	cwLive[0] = 0;
	cwHasActivity = false;
	cwResetDetector();
}

void morseDecodeTick(bool hasSignal)
{
	if (!cwCoeffReady)
	{
		for (int i = 0; i < CW_BIN_COUNT; i++)
		{
			cwCoeff[i] = 2.0f * cosf((2.0f * (float) M_PI * (float) cwBinFreq[i]) / CW_SAMPLE_RATE);
			cwS1[i] = 0.0f;
			cwS2[i] = 0.0f;
		}
		cwCoeffReady = true;
	}

	cwStoreLoad();

	if (currentChannelData != NULL)
	{
		if (currentChannelData->rxFreq != cwWatchedFreq)
		{
			cwWatchedFreq = currentChannelData->rxFreq;

			// Whatever was part-decoded belongs to where we just left.
			cwLiveLen = 0;
			cwLive[0] = 0;
			cwHasActivity = false;
			cwSymbolLen = 0;
			cwResetDetector();
			uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
		}
	}

	// A transmission is over once the key has been up long enough, or as soon
	// as the squelch shuts. Commit whatever was decoded.
	if (cwHasActivity)
	{
		if ((hasSignal == false) ||
				((uint32_t)(ticksGetMillis() - cwLastActivityMs) > CW_IDLE_COMMIT_MS))
		{
			cwFlushSymbol();
			cwCommit();
			uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
		}
	}
}

bool morseDecodeHasTextForCurrentChannel(void)
{
	if (currentChannelData == NULL)
	{
		return false;
	}

	cwStoreLoad();

	for (int i = 0; i < CW_LOG_ENTRIES; i++)
	{
		if (cwStore.d.entry[i].used && (cwStore.d.entry[i].freq == currentChannelData->rxFreq))
		{
			return true;
		}
	}
	return false;
}

int morseDecodeGetEntryCount(void)
{
	int n = 0;

	if (currentChannelData == NULL)
	{
		return 0;
	}

	cwStoreLoad();

	for (int i = 0; i < CW_LOG_ENTRIES; i++)
	{
		if (cwStore.d.entry[i].used && (cwStore.d.entry[i].freq == currentChannelData->rxFreq))
		{
			n++;
		}
	}
	return n;
}

// Newest first, so index 0 is the most recent.
const char *morseDecodeGetEntryText(int index)
{
	int n = 0;

	if (currentChannelData == NULL)
	{
		return "";
	}

	cwStoreLoad();

	for (int i = (CW_LOG_ENTRIES - 1); i >= 0; i--)
	{
		if (cwStore.d.entry[i].used && (cwStore.d.entry[i].freq == currentChannelData->rxFreq))
		{
			if (n == index)
			{
				return cwStore.d.entry[i].text;
			}
			n++;
		}
	}
	return "";
}

const char *morseDecodeGetLive(void)
{
	return (const char *) cwLive;
}

void morseDecodeClearAll(void)
{
	cwStoreLoad();
	cwStoreReset();
	cwStoreSave();

	cwLiveLen = 0;
	cwLive[0] = 0;
	cwHasActivity = false;
	cwSymbolLen = 0;
}
