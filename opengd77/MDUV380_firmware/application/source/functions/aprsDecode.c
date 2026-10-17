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
// APRS receive: Bell 202 AFSK demodulator, HDLC framing and AX.25 parsing.
//
// The firmware could always send APRS, but it could never hear it - the story
// was that the demodulated audio never reaches the MCU. It does (the notes at
// the top of fiveToneDecode.c tell how), and once the five-tone decoder worked
// I could not resist trying packets too. That module owns the codec and the I2S
// capture and hands the samples on, so all the receive decoders share one
// takeover of the hardware.
//
// Demodulation
// ------------
// The codec runs at 8kHz and Bell 202 is 1200 baud: only 6.67 samples per
// symbol. That is too few for a filter bank to tell 1200Hz from 2200Hz
// reliably - a seven-sample Goertzel window resolves no better than about
// 1140Hz, and the tones are only 1000Hz apart.
//
// So instead I use a non-coherent FSK detector: the signal is multiplied by a
// sine and a cosine of each tone, each product is summed over one symbol, and
// the two powers are compared. Nothing is delayed, so the sums are exact after
// exactly one symbol, which is about as good as it gets when a symbol is under
// seven samples long. Four multiplies per sample, nothing for this CPU.
//
// Bit clock
// ---------
// A counter adds 1200 per sample and takes a bit whenever it passes 8000, which
// gives exactly 6.67 samples per bit on average. Every transition in the sliced
// signal pulls the counter back towards mid-bit, so it stays locked to the
// sender's clock.
//
// Framing
// -------
// NRZI (a zero is a transition, a one is not), HDLC bit destuffing, 0x7E flags,
// and the X.25 FCS. A frame is only published if the checksum is right, so a
// weak signal gives you nothing at all rather than garbage on the screen.
// ---------------------------------------------------------------------------
#include <math.h>
#include "functions/aprsDecode.h"
#include "functions/fiveToneDecode.h"
#include "functions/ticks.h"
#include "functions/sound.h"
#include "interfaces/i2s.h"
#include "user_interface/uiGlobals.h"
#include "functions/codeplug.h"

#define AX25_FLAG          0x7E
#define AX25_MIN_FRAME     18    // 2 addresses + control + PID + FCS
// 256 rather than 330. A real APRS packet is far smaller than either - two
// addresses, three digipeaters, control and PID, then the information field,
// which comes to about 120 bytes - and the room this frees is what pays for the
// extra decoders below. Anything longer than this is dropped at the byte store,
// where the bound is checked, so a shorter buffer cannot corrupt memory; it can
// only refuse an implausibly long frame.
#define AX25_MAX_FRAME     256
#define AX25_FCS_RESIDUE   0xF0B8U // what the running CRC leaves when a frame is good

#define AFSK_BAUD          1200
#define AFSK_SAMPLE_RATE   8000

typedef struct
{
	uint16_t heardMin;  // ticksGetMillis() in minutes when heard; wraps after 45 days, which only ages matter for
	char    from[APRS_CALL_LEN + 1];
	char    info[APRS_INFO_LEN + 1];
	uint8_t forMe;      // an APRS message addressed to this station
	uint8_t textAt;     // offset into info[] where the message text starts
	char    msgId[6];   // identifier to quote in an acknowledgement, "" if none
	char    via[APRS_CALL_LEN + 1]; // the digipeater it was heard through, "" if heard direct
} aprsEntry_t;

// Main RAM is full, so everything sizeable lives in CCMRAM.
__attribute__((section(".ccmram"))) static aprsEntry_t aprsLog[APRS_LOG_ENTRIES];
__attribute__((section(".ccmram"))) static uint8_t     aprsPending[AX25_MAX_FRAME];

static int  aprsLogCount = 0;

// Input bandpass, 1700Hz centre, to keep hiss and anything down at CTCSS out of
// the correlators. RBJ biquad, transposed direct form II.
//
// Q=1.2. This was briefly widened to Q=0.7 on the argument that a second-order
// section delays the signal by 2Q/(2*pi*fc) - 225us at Q=1.2, 27% of an 833us
// symbol - and that the delay is not flat from 1200 to 2200Hz, so the two tones
// arrive skewed and every transition is smeared. The reasoning is sound but the
// radio disagreed: the wider filter measured worse, so it is reverted.
//
// The likely reason is noise bandwidth. The correlators integrate over only
// seven samples, which makes each one about 1140Hz wide, so they cannot reject
// what this filter lets past - it is the only real selectivity in the chain, and
// widening it fed the detector more noise than the flatter delay was worth.
//
// Kept because it measured better, not because the theory says so. Do not widen
// it again without a measurement that says otherwise.
static const float bpB0 =  0.2884f;
static const float bpB2 = -0.2884f;
static const float bpA1 = -0.3322f;
static const float bpA2 =  0.4233f;
static float bpZ1 = 0.0f;
static float bpZ2 = 0.0f;

// Correlator tables. 40 samples holds a whole number of cycles of both tones at
// 8kHz - six of 1200Hz and eleven of 2200Hz - so one table serves both without
// a phase accumulator.
#define AFSK_TABLE_LEN 40
#define AFSK_CORR_LEN  7   // one symbol; this is the matched filter length

static float afskTabC1[AFSK_TABLE_LEN];
static float afskTabS1[AFSK_TABLE_LEN];
static float afskTabC2[AFSK_TABLE_LEN];
static float afskTabS2[AFSK_TABLE_LEN];
static bool  afskTablesReady = false;
static int   afskTabPos = 0;

static float afskCorrBuf[4][AFSK_CORR_LEN];
static float afskCorrSum[4];
static int   afskCorrPos = 0;

// Space gain. This is the correction for something that had been noted in a
// comment further down and then never acted on: the C6000 applies de-emphasis to
// received audio (HR-C6000.c, page 0x04 register 0x34), which tilts the two tones
// against each other before they ever reach the detector.
//
// A single-pole 750us de-emphasis has its corner at 212Hz, so it passes 1200Hz at
// 0.174 and 2200Hz at 0.096 - the space tone arrives at 0.551 of the mark, 5.2dB
// down. The input bandpass above adds 0.46dB back, leaving 4.7dB, a power ratio
// of 0.34.
//
// The old decision was d = (m2-m1)/(m1+m2) with a fixed zero threshold. For a
// *pure* tone that is fine, since both ends still reach +/-1. The damage is at
// every transition: d=0 needs the space tone to carry three times the power of
// the mark, so the crossover does not sit half way. Solving 0.551*t = 1-t puts it
// at t=0.647 - the zero crossings are displaced by 15% of a symbol, and the
// displacement has opposite sign on the two edges, so mark bits read wider than
// space bits. At 6.67 samples per symbol that is a whole sample of bias, the same
// size as everything the interpolated timing recovery was worth.
//
// The exact figure depends on a time constant that is not documented for this
// chip, so it is not guessed at: three gains run side by side, each feeding its
// own HDLC decoder, and whichever passes a checksum wins. This is what Dire Wolf
// does with its multiple slicers, for the same reason - the right slicing level
// varies between packets but holds within one.
//
// 1.0 is the old behaviour exactly, so this cannot decode fewer frames than
// before. 3.0 is where the arithmetic above points. The win counters say which is
// actually paying, so the set can be moved rather than guessed at next time.
// Where in the bit to sample, as the clock counter's starting value after an
// edge. The counter adds 1200 a sample and fires at 8000, so a smaller number
// here means more samples pass before the bit is read - the sampling instant
// moves later.
//
// This axis replaces two rounds spent on tone tilt, both of which the benchmark
// rejected. Compensating the data slicer changed nothing, and neither did
// compensating the clock steering: 41 packets of 54 on a flat signal and 17 on a
// tilted one, identical to the digit across both attempts. The reason is that the
// benchmark's tilt is made by sending the space tone at 0.55 amplitude against
// flat noise, so those bits arrive with 5.2dB less energy. That is a signal to
// noise loss, not a threshold error, and no slicer or clock trick recovers energy
// that is not there. The flat row is the number that measures this decoder.
//
// What is left is a real defect. The data bit is the best of the slices at
// samples t, t-1 and t-2, so it represents the signal at t-1; the timing recovery
// reads the decision value at t. Every bit is therefore read one sample earlier
// than the clock intends - 15% of a symbol that is only 6.67 samples long, out
// towards where the neighbouring symbols bleed in most.
//
// This is the axis that finally paid. Adding it took the benchmark from 41 of 54
// to 53 on a flat signal and from 17 to 33 on a tilted one, and the tally showed
// 29 of the 86 recovered packets were carried by an offset other than the
// original - packets the old phase simply did not get.
//
// It also revises the reading of the tilt result. Calling that purely a signal to
// noise loss was too strong: a tilted signal displaces the crossings as well, and
// offsetting the sampling instant covers that displacement whatever caused it -
// the median's lag, the tilt, or the filter's delay. One axis, all three.
//
// One sample is 1200 of these units, so the four here span nothing to half a
// sample in sixths.
//
// Narrowed three times, every time from the tally rather than from theory. A full
// sample late won 4 packets against 25 for a half; three quarters won none at all
// against 13 for a quarter; and then, with the range down to nothing-to-half, the
// original phase itself came last - 16 packets against 24 for a third of a sample,
// and that is with ties handed to it for being first in the array.
//
// So the original is no longer in the set. Keeping it was a way of guaranteeing
// the decoder could not get worse, and that guarantee is no longer worth a
// decoder: the benchmark now catches a regression directly, which is a better
// safeguard than carrying the least productive phase forever. These four step by
// an eighth of a sample around the one that measured best.
#define AFSK_PHASE_COUNT 4
static const float afskPhaseStart[AFSK_PHASE_COUNT] = { 5104.0f, 4954.0f, 4804.0f, 4654.0f };

// Edge detector state. One set: every phase offset works from the same measured
// crossing and only places the sampling instant differently.
static float afskDPrev = 0.0f;
static int   afskSinceEdge[2] = { 0 }; // one per swing gate, sized by AFSK_GATE_COUNT below

static uint8_t afskSlice = 0; // last three data slicer outputs

// How large a move between two samples has to be before it is believed to be a
// real transition rather than noise wandering across the threshold. Index is
// (phase * AFSK_GATE_COUNT) + gate.
//
// This axis used to be the loop filter, and that question is now settled: taking
// a quarter of each measured error beat both taking all of it (20 packets to 49)
// and taking an eighth (13 to 55). A quarter it is, for every clock, and the
// memory is better spent here.
//
// Here, because of what the single-bit correction found out. Frames turned out
// almost never to fail by one bit - two in a whole benchmark run - so the losses
// are not scattered bit errors but whole-frame events: the clock losing its
// lock, a false flag, bit stuffing going astray. A gate that is too high is one
// way to lose lock, because a genuine edge in a weak signal swings less, gets
// rejected, and the clock then free-runs until the frame is gone.
//
// 0.30 is the value everything so far was measured with. 0.15 admits weaker
// edges, and some noise with them; whichever is right, the tally will say.
#define AFSK_GATE_COUNT 2
static const float afskSwingGate[AFSK_GATE_COUNT] = { 0.30f, 0.15f };
#define AFSK_LOOP_DIV 4 // settled: see above
#define AFSK_CLOCK_COUNT  (AFSK_PHASE_COUNT * AFSK_GATE_COUNT)
static int afskClock[AFSK_CLOCK_COUNT] = { 0 };

// HDLC state
// Decoders run side by side over the same audio and whichever produces a valid
// checksum wins. They differ along two axes: how the bit clock is steered, and
// what space gain the slicer uses.
//
// Clock. A symbol is just 6.67 samples long, so a phase off by a quarter of a
// symbol reads every bit close to where the decision is still swinging through
// zero. Clock A snaps the phase onto each measured edge, which tracks a drifting
// sender closely but is thrown by one mis-measured edge; clock B filters it, so a
// bad edge barely moves it, at the cost of pulling in more slowly.
//
// Gain. See afskSpaceGain above - this is the axis that was missing, and the one
// carrying a systematic error rather than a random one.
//
// The trick is that each combination gets its own complete state - its own bit
// accumulator, CRC and frame buffer - and the outputs are NOT voted on. That
// distinction is the whole point. Voting would collapse them back into one
// answer, and a bit only one of them read correctly would be outvoted and lost;
// keeping them separate means any one of them can carry the frame home on its
// own. Dire Wolf's array is indexed the same way, per channel, per demodulator,
// per slicer.
//
// The layout is (gain * AFSK_CLOCK_COUNT) + clock, so index 0 is gain 1.0 on
// clock A - byte for byte the behaviour that has already been measured. This can
// therefore only add frames, never take any away.
typedef struct
{
	uint8_t  raw;
	uint8_t  byte;
	int      bitCount;
	int      ones;
	int      len;
	bool     inFrame;
	uint16_t crc;
	bool     nrziPrev;
	int      flagRun;
	uint8_t  frame[AX25_MAX_FRAME];
} hdlcState_t;

// One decoder per clock. They all see the same data bit and differ only in when
// they sample it, which is where the evidence says the losses are.
#define APRS_DECODER_COUNT AFSK_CLOCK_COUNT
__attribute__((section(".ccmram"))) static hdlcState_t hdlc[APRS_DECODER_COUNT];

// Which combination recovered each frame. Without this there is no way to know
// whether the gain axis or the clock axis is doing the work, and no basis for
// choosing the next set of values except guesswork - which has already cost
// several rounds on this decoder.
// Read with care: when several decoders read the same frame the first one in the
// array publishes it and the rest are dropped, so ties always go to the lowest
// index. These say which combination gets there first, not which could have
// managed it - a packet no combination recovers is what actually shows a
// weakness, and the benchmark totals are where to look for that.
static volatile uint32_t aprsWinByGain[AFSK_PHASE_COUNT] = { 0 };   // by phase offset
static volatile uint32_t aprsWinByClock[AFSK_GATE_COUNT] = { 0 }; // by phase filter

static volatile uint32_t aprsFlagCount = 0;
static volatile uint32_t aprsGoodCount = 0;
static volatile uint32_t aprsBadCount = 0;
static volatile uint32_t aprsHeardCount = 0; // transmissions that reached the decoder
static volatile uint32_t aprsPlausibleCount = 0;
static volatile uint32_t aprsPreambleCount = 0;
// Running averages of the power in each tone, taken only while that tone is
// clearly the one present. Their ratio is the measured space gain - see where
// they are updated for why this is measured rather than assumed.
static float             aprsMarkAvg = 0.0f;
static float             aprsSpaceAvg = 0.0f;
static float             aprsToneSum = 0.0f;
static int               aprsToneCount = 0;
static volatile int      aprsTonePeakPct = 0;
static char              aprsLastBadFrom[APRS_CALL_LEN + 1] = "";
static volatile int      aprsLastBadLen = 0;
static uint8_t           aprsLastBadBytes[3] = { 0, 0, 0 };
static volatile int      aprsPendingLen = 0; // set by the interrupt, cleared by the tick

// Set while the benchmark is driving the detector with a generated signal.
// Declared here because the audio tap, further down, has to check it.
static volatile bool     btRunning = false;

// ---------------------------------------------------------------------------
// Frame handling
// ---------------------------------------------------------------------------
static void aprsCrcUpdate(hdlcState_t *h, uint8_t b)
{
	h->crc ^= b;
	for (int i = 0; i < 8; i++)
	{
		h->crc = ((h->crc & 1) ? ((h->crc >> 1) ^ 0x8408U) : (h->crc >> 1));
	}
}

static void aprsFrameStart(hdlcState_t *h)
{
	h->len = 0;
	h->crc = 0xFFFF;
	h->bitCount = 0;
	h->byte = 0;
	h->ones = 0;
	h->inFrame = true;
}

// Called on a closing flag. Running the CRC across the data *and* the two FCS
// bytes leaves a fixed residue when the frame is intact, which is cheaper than
// keeping the FCS aside and comparing.
static void aprsFrameEnd(hdlcState_t *h)
{
	if (!h->inFrame)
	{
		return;
	}
	h->inFrame = false;

	if ((h->len < AX25_MIN_FRAME) || (h->len > AX25_MAX_FRAME))
	{
		return;
	}

	// Packets heard, counted once each. The first decoder assembles a frame for
	// anything the others do too - they all see the same flags - so this is the
	// number of transmissions that reached the decoder, whether or not any of
	// them got it out. Without it the on-air figures cannot be read: "bad" counts
	// only this decoder while "good" counts a recovery by any of the eight, so a
	// packet the others rescued was being added to both.
	if (h == &hdlc[0])
	{
		aprsHeardCount++;
	}

	if (h->crc != AX25_FCS_RESIDUE)
	{
		// Only the first decoder is counted, so the figure still means "frames
		// that arrived and failed" and stays comparable with what it read before
		// the second decoder existed. Counting both would double every failure.
		if (h == &hdlc[0])
		{
			aprsBadCount++;
		}

		// What the frame actually assembled to. With the self test the answer is
		// known - 28 bytes starting 82 A0 A4 - so this says immediately whether
		// the fault is bit errors, a slipped bit, or the checksum itself.
		aprsLastBadLen = h->len;
		for (int i = 0; i < 3; i++)
		{
			aprsLastBadBytes[i] = ((i < h->len) ? h->frame[i] : 0);
		}

		// Is this a real packet we nearly got, or just noise between two
		// accidental flags? An AX.25 address field is six characters shifted
		// left one bit, so on a genuine frame every one of them decodes to a
		// letter, a digit or a space. Noise almost never manages that for all
		// fourteen bytes, so this separates "close" from "nothing there".
		{
			bool plausible = true;

			for (int i = 0; i < 14; i++)
			{
				char c = (char)(h->frame[i] >> 1);

				if (((c < 'A') || (c > 'Z')) && ((c < '0') || (c > '9')) && (c != ' '))
				{
					plausible = false;
					break;
				}
			}

			if (plausible)
			{
				aprsPlausibleCount++;

				// Keep the source callsign so it can be shown. It has not passed
				// its checksum, so it is only ever labelled as a failed attempt.
				for (int i = 0, n = 0; i < 6; i++)
				{
					char c = (char)(h->frame[7 + i] >> 1);

					if (c != ' ')
					{
						aprsLastBadFrom[n++] = c;
					}
					aprsLastBadFrom[n] = 0;
				}
			}
		}
		return;
	}

	// Hand it to the main loop; parsing is not interrupt work.
	//
	// The count goes here rather than above, so it means "packets recovered" -
	// one per packet. When both decoders read the same frame correctly, the
	// second finds a frame already waiting and is dropped, and it must not be
	// counted again either.
	if (aprsPendingLen == 0)
	{
		int idx = (int)(h - &hdlc[0]);

		memcpy(aprsPending, h->frame, (size_t)(h->len - 2)); // drop the FCS
		aprsPendingLen = (h->len - 2);
		aprsGoodCount++;

		// Credited to the one that got there first. Several often read the same
		// frame correctly, and the later ones are dropped by the guard above, so
		// this is a tally of which combination is worth keeping rather than of how
		// many can manage a given frame.
		if ((idx >= 0) && (idx < APRS_DECODER_COUNT))
		{
			aprsWinByGain[idx / AFSK_GATE_COUNT]++;
			aprsWinByClock[idx % AFSK_GATE_COUNT]++;
		}
	}
}

static void aprsHdlcBit(hdlcState_t *h, bool bit)
{
	h->raw = (uint8_t)((h->raw >> 1) | (bit ? 0x80 : 0x00));

	if (h->raw == AX25_FLAG)
	{
		aprsFlagCount++;

		// Flags back to back with nothing between them. This is the one thing
		// noise does not do: the pattern turns up in random data about once
		// every 256 bits, but never several times in a row. A real transmission
		// opens with a long run of them, so this counter answers the question
		// the raw flag count cannot - was an actual packet on the air.
		if (h->inFrame && (h->len == 0) && (h->bitCount == 0))
		{
			if (++h->flagRun == 3)
			{
				aprsPreambleCount++;
			}
		}
		else
		{
			h->flagRun = 0;
		}

		aprsFrameEnd(h);
		aprsFrameStart(h);
		return;
	}

	// Seven ones in a row is an abort, and cannot appear in stuffed data.
	if ((h->raw & 0xFE) == 0xFE)
	{
		h->inFrame = false;
		h->ones = 0;
		return;
	}

	if (!h->inFrame)
	{
		return;
	}

	// A zero after exactly five ones was inserted by the sender - drop it.
	if (h->ones == 5)
	{
		h->ones = 0;
		if (bit == false)
		{
			return;
		}
	}

	h->ones = (bit ? (h->ones + 1) : 0);

	h->byte = (uint8_t)((h->byte >> 1) | (bit ? 0x80 : 0x00));
	if (++h->bitCount == 8)
	{
		if (h->len < AX25_MAX_FRAME)
		{
			h->frame[h->len++] = h->byte;
			aprsCrcUpdate(h, h->byte);
		}
		else
		{
			h->inFrame = false; // runaway, wait for the next flag
		}
		h->bitCount = 0;
		h->byte = 0;
	}
}

// ---------------------------------------------------------------------------
// Demodulator
// ---------------------------------------------------------------------------

// One sample through the whole chain. Split out so the self test below can
// drive it with a synthesised signal instead of the radio.
//
// This is a non-coherent FSK detector: the sample is multiplied by a sine and a
// cosine of each tone, each product is summed over one symbol, and the two
// resulting powers are compared. It replaced a delay-and-multiply correlator,
// which cannot work at this sample rate however it is tuned. That one used a
// four sample delay, so it mixed the old and new tone for four samples after
// every transition, and its boxcar added six more - eleven samples of settling
// against a symbol that is only 6.67 samples long. The output never reached its
// full value inside a bit, so the eye was shut wherever it was sampled and no
// amount of adjusting the threshold could open it.
//
// Here nothing is delayed: the correlator sums are exact after one symbol, which
// is the theoretical best a detector can do.
static void aprsProcessSample(int16_t raw16)
{
	float raw = (float) raw16;
	float x;
	float m1, m2, d;
	bool bitLevel; // the data bit, shared by every clock
	bool quiet;
	int t;

	if (!afskTablesReady)
	{
		for (int i = 0; i < AFSK_TABLE_LEN; i++)
		{
			float w1 = ((2.0f * (float) M_PI * 1200.0f * (float) i) / (float) AFSK_SAMPLE_RATE);
			float w2 = ((2.0f * (float) M_PI * 2200.0f * (float) i) / (float) AFSK_SAMPLE_RATE);

			afskTabC1[i] = cosf(w1);
			afskTabS1[i] = sinf(w1);
			afskTabC2[i] = cosf(w2);
			afskTabS2[i] = sinf(w2);
		}
		// The decoder state has to be cleared here, not left to the loader. It
		// lives in CCMRAM, and the startup code does not initialise that region
		// at all - so on a cold start every one of these would begin at whatever
		// happened to be in the RAM: a length part way through a frame, a CRC
		// mid-stream, inFrame already set.
		//
		// The bit clocks and slicer histories are deliberately not here. They sit
		// in .bss, which the startup code does zero, and keeping them out of
		// CCMRAM is what makes that safe.
		for (int i = 0; i < APRS_DECODER_COUNT; i++)
		{
			memset(&hdlc[i], 0, sizeof(hdlc[i]));
			hdlc[i].crc = 0xFFFF;
		}

		afskTablesReady = true;
	}

	// Bandpass first, to keep out of band noise out of the correlators.
	x = ((bpB0 * raw) + bpZ1);
	bpZ1 = (bpZ2 - (bpA1 * x));
	bpZ2 = ((bpB2 * raw) - (bpA2 * x));

	x /= 256.0f; // keeps the squares below in a comfortable range

	t = afskTabPos;
	afskTabPos = ((t + 1) % AFSK_TABLE_LEN);

	{
		float v0 = (x * afskTabC1[t]);
		float v1 = (x * afskTabS1[t]);
		float v2 = (x * afskTabC2[t]);
		float v3 = (x * afskTabS2[t]);

		afskCorrSum[0] += (v0 - afskCorrBuf[0][afskCorrPos]);
		afskCorrSum[1] += (v1 - afskCorrBuf[1][afskCorrPos]);
		afskCorrSum[2] += (v2 - afskCorrBuf[2][afskCorrPos]);
		afskCorrSum[3] += (v3 - afskCorrBuf[3][afskCorrPos]);

		afskCorrBuf[0][afskCorrPos] = v0;
		afskCorrBuf[1][afskCorrPos] = v1;
		afskCorrBuf[2][afskCorrPos] = v2;
		afskCorrBuf[3][afskCorrPos] = v3;

		afskCorrPos = ((afskCorrPos + 1) % AFSK_CORR_LEN);
	}

	m1 = ((afskCorrSum[0] * afskCorrSum[0]) + (afskCorrSum[1] * afskCorrSum[1]));
	m2 = ((afskCorrSum[2] * afskCorrSum[2]) + (afskCorrSum[3] * afskCorrSum[3]));

	// Normalise: the ratio of the difference to the total, so the decision is
	// about which tone is present and not about how loud it is. Zero is then the
	// correct threshold in every case and no tracking is needed.
	//
	// The de-emphasis in the C6000 leaves 2200Hz roughly 5dB below 1200Hz, and
	// two earlier attempts to cope with that - first a peak-and-trough midpoint,
	// then a running average - both failed for the same underlying reason: an
	// HDLC preamble is not balanced. In NRZI a flag holds one tone for seven bits
	// and the other for one, so any threshold derived from the signal's own
	// statistics gets dragged onto the majority tone and the single-bit pulses
	// stop being detected. That is exactly what the counters showed - half the
	// flags missed, their bits then swallowed into the frame, giving 31 bytes
	// where 28 were sent. Dividing by the total removes the amplitude imbalance
	// outright: a clean tone reads about -0.96 or +0.96 whatever its level.
	{
		float total = (m1 + m2);

		quiet = (total <= 1.0f);
		d = (quiet ? 0.0f : ((m2 - m1) / total));
	}

	// How tone-like the audio is, measured rather than assumed. |d| is the
	// normalised confidence of the decision: a clean Bell 202 signal holds it
	// near 0.96, noise wanders around 0.2-0.4. Averaged over 100ms windows with
	// the best window kept, so a packet lasting under a second still shows up
	// among the noise either side of it. This is the one number that says
	// whether AFSK is reaching the demodulator at all.
	aprsToneSum += ((d < 0.0f) ? -d : d);
	if (++aprsToneCount >= 800)
	{
		int pct = (int)(aprsToneSum / 8.0f);

		if (pct > aprsTonePeakPct)
		{
			aprsTonePeakPct = pct;
		}
		aprsToneSum = 0.0f;
		aprsToneCount = 0;
	}

	// Measure the tone balance instead of calculating it.
	//
	// The space gains above rest on an assumption that has not actually been
	// checked: that the C6000's de-emphasis sits in the path feeding the I2S tap.
	// It is switched on in HR-C6000.c, but if it is applied on the way out to the
	// speaker rather than on the way in to the ADC, then the tilt it would cause
	// never reaches this decoder and compensating for it is wasted effort.
	//
	// So take the answer from the signal. Only samples where the decision is
	// already confident are used, which means one tone is clearly present, and
	// its power goes into that tone's running average. The ratio of the two is
	// then the tilt as it actually arrives here - a single packet settles it,
	// where inferring the same thing from which gain wins would need many.
	//
	// Expect roughly 3.0 if the calculation was right, or 1.0 if the de-emphasis
	// is not in this path at all.
	{
		float aNow = ((d < 0.0f) ? -d : d);

		if (aNow > 0.8f)
		{
			if (d < 0.0f)
			{
				aprsMarkAvg += (0.01f * (m1 - aprsMarkAvg));
			}
			else
			{
				aprsSpaceAvg += (0.01f * (m2 - aprsSpaceAvg));
			}
		}
	}

	// The data bit. One slicer, not three: the benchmark showed the tilt costs 24
	// packets in 54 and that compensating here recovered none of them, because at
	// the centre of a symbol the right tone wins whatever the gain. Compared
	// rather than divided, and m2 > m1 is exactly d > 0.
	//
	// The best of the last three samples is kept rather than the single one the
	// clock landed on. That is what lifted a marginal signal from unreadable to
	// readable: at 6.67 samples per symbol one noisy sample was enough to flip a
	// bit, and one flipped bit fails a whole frame.
	{
		bool level = (quiet ? false : (m2 > m1));
		int ones;

		afskSlice = (uint8_t)(((afskSlice << 1) | (level ? 1U : 0U)) & 0x07U);

		ones = ((((afskSlice & 0x01U) != 0U) ? 1 : 0) +
				(((afskSlice & 0x02U) != 0U) ? 1 : 0) +
				(((afskSlice & 0x04U) != 0U) ? 1 : 0));

		bitLevel = (ones >= 2); // best of three, so one wrong slice is outvoted
	}

	// Timing recovery from the interpolated zero crossing of the decision
	// variable, rather than from the sample on which the sign happened to flip.
	//
	// This is where the frames were being lost. Locking to whole samples leaves
	// up to half a sample of error, which is 7% of a symbol that is only 6.67
	// samples long, and every bit is then read that far off centre - where
	// neighbouring symbols bleed in most. A frame only needs one bit to land
	// wrong to fail its checksum, which is exactly the picture the counters
	// showed: 45 frames assembled and 5 survived.
	//
	// Where d crosses zero between two samples is known from their relative
	// sizes, so the true edge is found to a fraction of a sample and the
	// sampling instant placed a proper half-symbol after it.
	{
		float aNow = ((d < 0.0f) ? -d : d);
		float aPrev = ((afskDPrev < 0.0f) ? -afskDPrev : afskDPrev);
		float swing = (d - afskDPrev);
		bool crossed = ((d > 0.0f) != (afskDPrev > 0.0f));
		float f = (((aPrev + aNow) > 0.0f) ? (aPrev / (aPrev + aNow)) : 0.5f);

		if (swing < 0.0f)
		{
			swing = -swing;
		}

		// Each gate decides for itself whether this is an edge, so each keeps its
		// own count of how long it has been since the last one - a gate that
		// believes more of them resets more often.
		for (int g = 0; g < AFSK_GATE_COUNT; g++)
		{
			afskSinceEdge[g]++;

			// A crossing counts only when the two samples straddle zero, the move
			// between them clears this gate, and a bit period has nearly passed.
			if (crossed && (swing > afskSwingGate[g]) && (afskSinceEdge[g] >= 4))
			{
				// The crossing is found once; each phase offset then places its
				// own sampling instant relative to it. The f term shifts them all
				// together as the crossing moves between the two samples.
				for (int s = 0; s < AFSK_PHASE_COUNT; s++)
				{
					int *clk = &afskClock[(s * AFSK_GATE_COUNT) + g];
					int e = ((int)(afskPhaseStart[s] - (1200.0f * f)) - *clk);

					// Unwrapped first, since the counter is modular.
					while (e < -(AFSK_SAMPLE_RATE / 2))
					{
						e += AFSK_SAMPLE_RATE;
					}
					while (e >= (AFSK_SAMPLE_RATE / 2))
					{
						e -= AFSK_SAMPLE_RATE;
					}

					*clk += (e / AFSK_LOOP_DIV);

					while (*clk < 0)
					{
						*clk += AFSK_SAMPLE_RATE;
					}
					while (*clk >= AFSK_SAMPLE_RATE)
					{
						*clk -= AFSK_SAMPLE_RATE;
					}
				}

				afskSinceEdge[g] = 0;
			}
		}
	}

	afskDPrev = d;

	// Feed every combination. Each clock advances on its own phase, and when one
	// reaches a bit boundary all the gains are sampled on it - each into its own
	// decoder, with its own bit.
	//
	// No vote is taken across the gains. That is the change: the bit that only the
	// correctly compensated slicer read right now reaches a decoder that can carry
	// the whole frame, instead of being outvoted by two slicers biased the same
	// way. They usually agree, and when they do the first to finish queues the
	// frame and the rest are dropped by the pending-frame guard, so a packet is
	// never logged twice.
	for (int c = 0; c < AFSK_CLOCK_COUNT; c++)
	{
		afskClock[c] += AFSK_BAUD;

		if (afskClock[c] >= AFSK_SAMPLE_RATE)
		{
			hdlcState_t *h = &hdlc[c];

			afskClock[c] -= AFSK_SAMPLE_RATE;

			aprsHdlcBit(h, (bitLevel == h->nrziPrev)); // NRZI
			h->nrziPrev = bitLevel;
		}
	}
}

void aprsDecodeAudioBlock(uint32_t bufNum)
{
	// The benchmark drives the detector directly. Real audio arriving in the
	// middle of that would make the score depend on what was on the air, which
	// is exactly what the benchmark exists to avoid.
	if (btRunning)
	{
		return;
	}

	// NOTE: there is deliberately no five-tone gate here, unlike the Morse
	// decoder. Standing aside while the five-tone decoder hears something looks
	// sensible until the frequencies are compared: APRS marks at 1200Hz and the
	// CCIR table has a digit at 1197Hz, three hertz away. In NRZI the level holds
	// for seven bit periods, so across one of that decoder's 32ms blocks the
	// audio is about 87% one tone - easily enough to be called a clean digit.
	// Every real packet therefore switched this decoder off for the two seconds
	// that followed, which is the whole transmission. Flags still accumulated
	// from noise between packets, so the counters looked alive while nothing was
	// ever decoded.
	//
	// A five-tone burst ahead of a packet does corrupt some bits, but the frame
	// checksum throws that away by itself. That is the right way to handle it.
	for (int h = 0; h < 2; h++)
	{
		for (int i = 0; i < WAV_BUFFER_SIZE; i += 2)
		{
			aprsProcessSample((int16_t) i2s_Rx_Buffer[bufNum][h][i]);
		}
	}
}

// ---------------------------------------------------------------------------
// AX.25 parsing - main loop context
// ---------------------------------------------------------------------------
// Addresses are six characters shifted left one bit, then an SSID byte.
static void aprsCallsign(const uint8_t *addr, char *out)
{
	int n = 0;

	for (int i = 0; i < 6; i++)
	{
		char c = (char)(addr[i] >> 1);

		if (c != ' ')
		{
			out[n++] = c;
		}
	}

	{
		int ssid = ((addr[6] >> 1) & 0x0F);

		if (ssid != 0)
		{
			out[n++] = '-';
			if (ssid >= 10)
			{
				out[n++] = '1';
				ssid -= 10;
			}
			out[n++] = (char)('0' + ssid);
		}
	}

	out[n] = 0;
}

// Turkish letters as the radio's own font has them, so "Tüm amatörlere" shows
// as written.
//
// This used to reduce them to plain ASCII, for fear of guessing the sender's
// encoding wrong. But aprsCopyText below already settles that: a valid UTF-8
// sequence is taken as UTF-8 and anything else as ISO-8859-9 (the Turkish set,
// equal to Latin-1 apart from these letters), so by the time a letter gets here
// its code point is known. The font has every Turkish letter except a capital
// g with a breve, which becomes a plain G.
static char aprsTurkishToAscii(uint16_t cp)
{
	switch (cp)
	{
		case 0x00FC: return (char) 0xFC; // u umlaut - the font keeps the Latin-1 ones where Latin-1 has them
		case 0x00DC: return (char) 0xDC;
		case 0x00F6: return (char) 0xF6; // o umlaut
		case 0x00D6: return (char) 0xD6;
		case 0x00E7: return (char) 0xE7; // c cedilla
		case 0x00C7: return (char) 0xC7;
		case 0x0131: return (char) 0x93; // dotless i
		case 0x0130: return (char) 0x94; // I with dot
		case 0x011F: return (char) 0x98; // g breve
		case 0x011E: return 'G';         // not in the font
		case 0x015F: return (char) 0x96; // s cedilla
		case 0x015E: return (char) 0x95;
		default:     return 0;
	}
}

// Copies packet text into the display buffer, whatever encoding it arrived in.
//
// Everything is first turned into a Unicode code point, then Turkish letters are
// written as ASCII and anything else is kept as a single byte. Doing the
// conversion in one place means UTF-8, ISO-8859-9 and plain Latin-1 all end up
// looking the same on screen.
static void aprsCopyText(char *out, const uint8_t *in, int inLen, int outMax)
{
	int n = 0;
	int i = 0;

	while ((i < inLen) && (n < outMax))
	{
		uint8_t c = in[i];
		uint16_t cp;
		char ascii;

		if (c < 0x80)
		{
			out[n++] = (char)(((c >= 0x20) && (c != 0x7F)) ? c : 0x20);
			i++;
			continue;
		}

		// A valid two byte UTF-8 sequence carries its code point directly.
		if ((c >= 0xC2) && (c <= 0xDF) && ((i + 1) < inLen) && ((in[i + 1] & 0xC0) == 0x80))
		{
			cp = (uint16_t)(((c & 0x1F) << 6) | (in[i + 1] & 0x3F));
			i += 2;
		}
		else if ((c >= 0xE0) && (c <= 0xF4) && ((i + 1) < inLen) && ((in[i + 1] & 0xC0) == 0x80))
		{
			// Three or four bytes - nothing we need, skip it whole.
			i += ((c < 0xF0) ? 3 : 4);
			continue;
		}
		else
		{
			// A single byte set. ISO-8859-9 is the usual Turkish one and puts
			// five letters outside Latin-1; everywhere else it matches, so the
			// byte is the code point.
			switch (c)
			{
				case 0xF0: cp = 0x011F; break; // g breve
				case 0xD0: cp = 0x011E; break;
				case 0xFD: cp = 0x0131; break; // dotless i
				case 0xDD: cp = 0x0130; break;
				case 0xFE: cp = 0x015F; break; // s cedilla
				case 0xDE: cp = 0x015E; break;
				default:   cp = c;      break;
			}
			i++;
		}

		ascii = aprsTurkishToAscii(cp);
		if (ascii != 0)
		{
			out[n++] = ascii;
		}
		else if (cp < 0x0100)
		{
			out[n++] = (char) cp; // other Latin-1, the font has these
		}
		else
		{
			out[n++] = '?'; // outside anything we can show
		}
	}

	out[n] = 0;
}

// Works out whether a packet is an APRS message meant for this station.
//
// The format is fixed: a colon, exactly nine characters of addressee padded
// with spaces, another colon, the text, and optionally a brace and an
// identifier that an acknowledgement has to quote back. The nine characters are
// not negotiable - a shorter callsign is padded - so the second colon must land
// at offset ten, and that alone rules out most other packet types.
//
// The addressee carries the SSID, as in TA8BMO-7. Our own callsign comes from
// the radio name and the SSID from the APRS configuration, so both are compared:
// a message to the bare callsign is accepted as well, since that is what a
// station replying by hand will usually type.
static void aprsExamineMessage(aprsEntry_t *entry)
{
	char myCall[APRS_CALL_LEN + 1];
	char addressee[12];
	int n = 0;
	int textAt;

	if ((entry->info[0] != ':') || (entry->info[10] != ':'))
	{
		return;
	}

	// Trim the padding off the addressee.
	for (int i = 1; i <= 9; i++)
	{
		char c = entry->info[i];

		if (c == ' ')
		{
			break;
		}
		addressee[n++] = c;
	}
	addressee[n] = 0;

	codeplugGetRadioName(myCall);
	myCall[6] = 0;
	for (int i = 0; myCall[i] != 0; i++)
	{
		if (myCall[i] == ' ')
		{
			myCall[i] = 0;
			break;
		}
	}

	{
		int myLen = (int) strlen(myCall);
		bool match = false;

		if (strcmp(addressee, myCall) == 0)
		{
			match = true; // addressed to the bare callsign
		}
		else if ((strncmp(addressee, myCall, (size_t) myLen) == 0) && (addressee[myLen] == '-'))
		{
			match = true; // callsign plus an SSID
		}

		if (!match)
		{
			return;
		}
	}

	entry->forMe = 1;
	textAt = 11;
	entry->textAt = (uint8_t) textAt;

	// A trailing brace introduces the identifier to acknowledge.
	for (int i = textAt; entry->info[i] != 0; i++)
	{
		if (entry->info[i] == '{')
		{
			int k = 0;

			for (int j = (i + 1); (entry->info[j] != 0) && (k < (int)(sizeof(entry->msgId) - 1)); j++)
			{
				entry->msgId[k++] = entry->info[j];
			}
			entry->msgId[k] = 0;
			entry->info[i] = 0; // keep the identifier out of the displayed text
			break;
		}
	}
}

// Mic-E, which most Kenwood and Yaesu handhelds send, splits the position: the
// latitude, north/south, east/west and a longitude offset are coded into the
// six characters of the destination address, and only the longitude is in the
// information field. The log keeps the information field and not the
// destination - there is no room for it - so a Mic-E position is rewritten here,
// while the destination is still to hand, as an ordinary uncompressed one
// ("!DDMM.hhN/DDDMM.hhE>" then the comment). Everything that reads positions
// from the log then understands it with no change. Returns its length, or 0 if
// this is not Mic-E or does not decode to a sensible position.
static int aprsMicEToPosition(const uint8_t *frame, const uint8_t *info, int infoLen, char *out, int outSize)
{
	char dest[6];
	int digit[6];
	int n;

	if ((infoLen < 9) || ((info[0] != 0x60) && (info[0] != 0x27) && (info[0] != 0x1C) && (info[0] != 0x1D)))
	{
		return 0;
	}

	for (int i = 0; i < 6; i++)
	{
		char c = (char) (frame[i] >> 1);

		if ((c >= '0') && (c <= '9'))
		{
			digit[i] = (c - '0');
		}
		else if ((c >= 'A') && (c <= 'J'))
		{
			digit[i] = (c - 'A');
		}
		else if ((c >= 'P') && (c <= 'Y'))
		{
			digit[i] = (c - 'P');
		}
		else if ((c == 'K') || (c == 'L') || (c == 'Z'))
		{
			digit[i] = 0; // a digit blanked out for position ambiguity
		}
		else
		{
			return 0;
		}
		dest[i] = c;
	}

	// Characters 4-6 also carry a flag each: P-Z sets it, 0-9 and L do not.
	bool north = (dest[3] >= 'P');
	bool lonOffset = (dest[4] >= 'P');
	bool west = (dest[5] >= 'P');

	int latDeg = ((digit[0] * 10) + digit[1]);
	int latMin = ((digit[2] * 10) + digit[3]);
	int latHun = ((digit[4] * 10) + digit[5]);

	int lonDeg = ((int) info[1] - 28);
	int lonMin = ((int) info[2] - 28);
	int lonHun = ((int) info[3] - 28);

	if (lonOffset)
	{
		lonDeg += 100;
	}
	if ((lonDeg >= 180) && (lonDeg <= 189))
	{
		lonDeg -= 80;
	}
	else if ((lonDeg >= 190) && (lonDeg <= 199))
	{
		lonDeg -= 190;
	}
	if (lonMin >= 60)
	{
		lonMin -= 60;
	}

	if ((latDeg > 89) || (latMin > 59) || (lonDeg < 0) || (lonDeg > 179) || (lonMin < 0) || (lonMin > 59) ||
			(lonHun < 0) || (lonHun > 99) || (info[7] < 0x21) || (info[7] > 0x7E) || (info[8] < 0x21) || (info[8] > 0x7E))
	{
		return 0;
	}

	n = snprintf(out, outSize, "!%02d%02d.%02d%c%c%03d%02d.%02d%c%c", latDeg, latMin, latHun, (north ? 'N' : 'S'),
			(char) info[8], lonDeg, lonMin, lonHun, (west ? 'W' : 'E'), (char) info[7]);

	// The rest - comment, altitude, telemetry - follows as the comment.
	for (int i = 9; (i < infoLen) && (n < (outSize - 1)); i++)
	{
		out[n++] = (char) info[i];
	}
	out[n] = 0;

	return n;
}

static void aprsPublish(const uint8_t *frame, int len)
{
	int addrEnd = 0;
	char from[APRS_CALL_LEN + 1];
	char micE[APRS_INFO_LEN + 1];
	int micELen;
	int infoStart;
	int infoLen;
	aprsEntry_t entry;

	// Addresses run until one has bit 0 of its SSID byte set.
	while ((addrEnd < len) && (addrEnd < (7 * 10)))
	{
		if (frame[addrEnd + 6] & 0x01)
		{
			addrEnd += 7;
			break;
		}
		addrEnd += 7;
	}

	if ((addrEnd < 14) || ((addrEnd + 2) > len))
	{
		return;
	}

	// Source is the second address; destination is first.
	aprsCallsign(frame + 7, from);

	infoStart = (addrEnd + 2); // skip control and PID
	infoLen = (len - infoStart);
	if (infoLen < 0)
	{
		infoLen = 0;
	}

	micELen = aprsMicEToPosition(frame, (frame + infoStart), infoLen, micE, sizeof(micE));
	if (infoLen > APRS_INFO_LEN)
	{
		infoLen = APRS_INFO_LEN;
	}

	// Built aside first, so it can be compared with what is already logged.
	memset(&entry, 0, sizeof(entry));
	memcpy(entry.from, from, sizeof(from));
	entry.heardMin = (uint16_t) (ticksGetMillis() / 60000U);

	// The station actually heard over the air is the last digipeater whose
	// has-been-repeated bit (bit 7 of the SSID byte) is set. None set means it
	// came straight from the sender. Only the digipeater addresses are looked
	// at: in the destination and source that bit means something else.
	for (int a = 14; (a + 7) <= addrEnd; a += 7)
	{
		if (frame[a + 6] & 0x80)
		{
			aprsCallsign(frame + a, entry.via);
		}
	}

	// aprsCopyText terminates the string itself; the decoded length is shorter
	// than the input whenever a multi-byte sequence was folded down.
	if (micELen > 0)
	{
		aprsCopyText(entry.info, (const uint8_t *) micE, micELen, APRS_INFO_LEN);
	}
	else
	{
		aprsCopyText(entry.info, (frame + infoStart), infoLen, APRS_INFO_LEN);
	}

	aprsExamineMessage(&entry);

	// One entry per station, so a station beaconing every minute cannot push
	// everyone else out: hearing it again replaces its older entry, and the new
	// one goes to the top. Messages to this station are the exception - a later
	// beacon must not wipe one out before it has been read - so they are only
	// replaced by the very same message again (a retry, or another digipeater's
	// copy of it), and a message never replaces the station's position either.
	for (int i = (aprsLogCount - 1); i >= 0; i--)
	{
		const aprsEntry_t *old = &aprsLog[i];
		bool same = ((strcmp(old->info, entry.info) == 0) && (strcmp(old->msgId, entry.msgId) == 0) && (old->forMe == entry.forMe));

		if ((strcmp(old->from, entry.from) == 0) && (((old->forMe == 0) && (entry.forMe == 0)) || same))
		{
			memmove(&aprsLog[i], &aprsLog[i + 1], ((size_t) ((aprsLogCount - 1) - i) * sizeof(aprsEntry_t)));
			aprsLogCount--;
		}
	}

	// Newest last in the array; when full, the oldest goes.
	if (aprsLogCount >= APRS_LOG_ENTRIES)
	{
		memmove(&aprsLog[0], &aprsLog[1], ((APRS_LOG_ENTRIES - 1) * sizeof(aprsEntry_t)));
		aprsLogCount = (APRS_LOG_ENTRIES - 1);
	}

	aprsLog[aprsLogCount] = entry;
	aprsLogCount++;
}

void aprsDecodeTick(bool hasSignal)
{
	(void) hasSignal;

	if (aprsPendingLen > 0)
	{
		int len = aprsPendingLen;

		aprsPublish(aprsPending, len);
		aprsPendingLen = 0;

		soundSetMelody(MELODY_ACK_BEEP);
		uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
	}
}

int aprsDecodeGetCount(void)
{
	return aprsLogCount;
}

const char *aprsDecodeGetFrom(int index)
{
	int i = (aprsLogCount - 1 - index);

	return (((i >= 0) && (i < APRS_LOG_ENTRIES)) ? aprsLog[i].from : "");
}

uint16_t aprsDecodeGetAgeMinutes(int index)
{
	int i = (aprsLogCount - 1 - index); // newest first, like the rest

	if ((i < 0) || (i >= aprsLogCount))
	{
		return 0;
	}

	return (uint16_t) ((uint16_t) (ticksGetMillis() / 60000U) - aprsLog[i].heardMin);
}

const char *aprsDecodeGetVia(int index)
{
	int i = (aprsLogCount - 1 - index);

	return (((i >= 0) && (i < aprsLogCount)) ? aprsLog[i].via : "");
}

const char *aprsDecodeGetInfo(int index)
{
	int i = (aprsLogCount - 1 - index);

	return (((i >= 0) && (i < APRS_LOG_ENTRIES)) ? aprsLog[i].info : "");
}

void aprsDecodeGetStats(uint32_t *flags, uint32_t *good, uint32_t *bad, uint32_t *plausible, uint32_t *preamble)
{
	if (flags != NULL)
	{
		*flags = aprsFlagCount;
	}
	if (good != NULL)
	{
		*good = aprsGoodCount;
	}
	if (bad != NULL)
	{
		*bad = aprsBadCount;
	}
	if (preamble != NULL)
	{
		*preamble = aprsPreambleCount;
	}
	if (plausible != NULL)
	{
		*plausible = aprsPlausibleCount;
	}
}

// ---------------------------------------------------------------------------
// Benchmark
//
// Why this exists. Tuning was being done against what the radio happened to hear,
// and the same firmware measured 10 good out of 10 one evening and 1 out of 9 the
// next. Nine to twenty frames is far too small a sample, and the traffic differs
// every time - how many stations collided, how many were mobile and fading. Every
// conclusion drawn from those numbers, including "the space gain doubled the
// rate", was within the noise of the measurement.
//
// So the signal is generated here instead. A known packet, a fixed PRNG seed, a
// fixed set of noise levels: the same firmware returns exactly the same score
// every run, and a difference between two builds is therefore real. This is the
// same idea as Dire Wolf's atest against a reference recording.
//
// Two tilts are run. One flat, one with the space tone at 0.55 - the ratio a
// 750us de-emphasis would produce. That way the gain diversity is exercised
// without assuming which of the two the radio actually delivers; the on-air
// "Egim" reading is what answers that. A benchmark cannot, because the tilt in
// it is whatever is put there.
// ---------------------------------------------------------------------------
// The levels below are spaced across the region where the decoder actually
// fails, not across the whole range. The first attempt used 0.05, 0.30 and 0.60
// and scored 5/5, 5/5, 0/5 - perfect, perfect, nothing. A packet needs about
// 2600 consecutive bits right, so its error rate collapses over a very narrow
// band of signal to noise, and a benchmark that steps straight over that band
// reads the same for every build. Nine packets at each of six closely spaced
// levels puts the whole transition on screen as a curve, where an improvement
// shows up as the digits moving right.
// One tilt, not two - and it is the tilted one that was kept.
//
// This was briefly the other way round. The tilt measurement read 1.1, which
// would have meant the two tones arrived level and the de-emphasis sat outside
// this path, so the tilted row was dropped as unrealistic. That reading came from
// two packets. With fifty-two it reads 2.7, against 2.96 from the arithmetic, so
// the tilt is real and it is the flat row that never happens on this radio.
//
// The lesson is the one this decoder has already been taught twice: a number
// taken from a handful of samples is not a measurement. It cost a row of the
// benchmark being calibrated against a condition that does not exist.
//
// The time the second row took goes into this one instead, at twice the packets
// per cell. Same run length, half the sampling error.
#define BT_TILTS    1
#define BT_LEVELS   6
// Eighteen rather than nine. At nine the sampling error on a cell is about 1.5
// counts and on a row total about 4, which is the same size as the improvements
// now being looked for - a flat row read 6,6,6,0,0,1, where the 1 at the hardest
// level and the 6 at the easiest are both just noise. Doubling it halves that.
//
// Row totals stay comparable across this change when read as a proportion, since
// the noise levels are unchanged and this alters only how many times each is
// tried. The decoder does not see this number at all.
#define BT_PACKETS  36

static uint8_t       btScore[BT_TILTS][BT_LEVELS];
// Which slicer gain carried the packets during the test. The flat and tilted
// rows scoring the same tells us nothing on its own - the gains might be doing
// the rescuing, or the tilt might simply not matter. This says which.
static uint8_t       btWinGain[AFSK_PHASE_COUNT];
// The phase filter tally has never been shown, so whether the snapping and the
// filtered clock both earn their memory is still unknown - and that axis costs
// half of the eight decoders. If one of them takes almost nothing it can be
// spent on something that does.
static uint8_t       btWinFilter[AFSK_GATE_COUNT];
static uint32_t      btRng = 1;
static float         btPhase = 0.0f;
static int           btSampAcc = 0;
static bool          btNrzi = false;
static int           btOnes = 0;
static float         btMarkAmp = 0.0f;
static float         btSpaceAmp = 0.0f;
static float         btNoiseAmp = 0.0f;

// Uniform, and deliberately not seeded from anything that varies. Reproducibility
// is the entire point - a benchmark that moves on its own is no better than the
// on-air counters it replaces.
static float btRand(void)
{
	btRng = ((btRng * 1664525u) + 1013904223u);

	return ((((float)((btRng >> 8) & 0xFFFFu)) / 32768.0f) - 1.0f);
}

// Three uniforms summed, scaled back to the same RMS. Receiver noise is Gaussian
// and its tails produce bit errors that a flat distribution never does, so a
// single uniform value flatters the decoder exactly at the margin - which is the
// region this benchmark exists to resolve.
static float btNoise(void)
{
	return (((btRand() + btRand() + btRand())) * 0.5774f);
}

static uint16_t btFrameCrc(const uint8_t *d, int n)
{
	uint16_t c = 0xFFFF;

	for (int i = 0; i < n; i++)
	{
		c ^= d[i];
		for (int b = 0; b < 8; b++)
		{
			c = ((c & 1) ? ((c >> 1) ^ 0x8408U) : (c >> 1));
		}
	}
	return (uint16_t)~c;
}

// A realistic AX.25 UI frame: two addresses, control, PID, a position report.
static int btBuildFrame(uint8_t *f)
{
	static const char dest[6] = { 'A', 'P', 'R', 'S', ' ', ' ' };
	static const char src[6]  = { 'T', 'A', '8', 'B', 'M', 'O' };
	static const char info[]  = "!4058.00N/02901.00E-Test";
	int n = 0;

	for (int i = 0; i < 6; i++)
	{
		f[n++] = (uint8_t)(dest[i] << 1);
	}
	f[n++] = 0x60; // SSID 0, more addresses follow

	for (int i = 0; i < 6; i++)
	{
		f[n++] = (uint8_t)(src[i] << 1);
	}
	f[n++] = 0x61; // SSID 0, last address

	f[n++] = 0x03; // UI
	f[n++] = 0xF0; // no layer 3

	for (int i = 0; info[i] != 0; i++)
	{
		f[n++] = (uint8_t)info[i];
	}

	{
		uint16_t c = btFrameCrc(f, n);

		f[n++] = (uint8_t)(c & 0xFFu);
		f[n++] = (uint8_t)(c >> 8);
	}
	return n;
}

// One channel bit, modulated and fed straight into the detector. NRZI matches the
// decoder's convention: a one holds the tone, a zero changes it.
//
// The sample count alternates 6,7,7 through an accumulator rather than being
// rounded, because 8000/1200 is 6.667 and rounding it would drift the symbol rate
// by 5% - which would test the clock recovery against a fault that is not real.
static void btEmitBit(bool one)
{
	float f;
	int n;

	if (one == false)
	{
		btNrzi = !btNrzi;
	}

	f = (btNrzi ? 2200.0f : 1200.0f);

	btSampAcc += AFSK_SAMPLE_RATE;
	n = (btSampAcc / AFSK_BAUD);
	btSampAcc -= (n * AFSK_BAUD);

	for (int i = 0; i < n; i++)
	{
		float a = (btNrzi ? btSpaceAmp : btMarkAmp);
		float s = ((a * sinf(btPhase)) + (btNoiseAmp * btNoise()));

		btPhase += ((2.0f * (float) M_PI * f) / (float) AFSK_SAMPLE_RATE);
		if (btPhase > (2.0f * (float) M_PI))
		{
			btPhase -= (2.0f * (float) M_PI);
		}

		aprsProcessSample((int16_t) s);
	}
}

static void btEmitByte(uint8_t b, bool stuff)
{
	for (int i = 0; i < 8; i++)
	{
		bool bit = ((b & (1u << i)) != 0u);

		btEmitBit(bit);

		if (stuff)
		{
			if (bit)
			{
				if (++btOnes == 5)
				{
					btEmitBit(false); // stuffed zero
					btOnes = 0;
				}
			}
			else
			{
				btOnes = 0;
			}
		}
	}
}

static bool btRunOnePacket(void)
{
	uint8_t frame[64];
	int n = btBuildFrame(frame);

	// Everything the detector carries between samples has to start from a known
	// state, or the previous packet's tail decides this one's result.
	for (int i = 0; i < APRS_DECODER_COUNT; i++)
	{
		memset(&hdlc[i], 0, sizeof(hdlc[i]));
		hdlc[i].crc = 0xFFFF;
	}
	for (int i = 0; i < AFSK_PHASE_COUNT; i++)
	{
	}
	for (int i = 0; i < AFSK_CLOCK_COUNT; i++)
	{
		afskClock[i] = 0;
	}
	for (int i = 0; i < 4; i++)
	{
		afskCorrSum[i] = 0.0f;
		for (int j = 0; j < AFSK_CORR_LEN; j++)
		{
			afskCorrBuf[i][j] = 0.0f;
		}
	}
	bpZ1 = 0.0f;
	bpZ2 = 0.0f;
	afskCorrPos = 0;
	afskTabPos = 0;
	afskSlice = 0;
	aprsPendingLen = 0;

	btPhase = 0.0f;
	btSampAcc = 0;
	btNrzi = false;
	btOnes = 0;

	for (int i = 0; i < 20; i++) // preamble
	{
		btEmitByte(AX25_FLAG, false);
	}
	btOnes = 0;

	for (int i = 0; i < n; i++)
	{
		btEmitByte(frame[i], true);
	}
	btOnes = 0;

	for (int i = 0; i < 4; i++) // closing flags
	{
		btEmitByte(AX25_FLAG, false);
	}

	return (aprsPendingLen > 0);
}

void aprsDecodeRunBenchmark(void)
{
	// The space tone at 0.55 of the mark, because that is what this radio
	// delivers. The measured tilt is 2.7 in power over 52 packets, which is an
	// amplitude ratio of 0.61 - near enough, and near enough to the 2.96 the
	// de-emphasis arithmetic predicted.
	//
	// Flat noise against a tilted signal is the physically right combination
	// here, not an approximation: an FM discriminator's noise rises as the square
	// of frequency and de-emphasis attenuates as its inverse, so past that filter
	// the noise comes out flat while the tone, sent at equal deviation, comes out
	// 5dB down at 2200Hz.
	static const float tiltSpace[BT_TILTS] = { 0.55f };
	// Geometric, 0.6dB apart, bracketing the collapse the last run put between
	// 0.643 and 0.769. Two earlier sets were too coarse in opposite directions -
	// one had five of six levels at full marks, the next had four of six at zero -
	// and a benchmark against its ceiling or its floor measures nothing either way.
	//
	// These are the last change to them. Moving the levels makes a run
	// incomparable with the one before it, and that has now cost three baselines;
	// from here the numbers stand still so the decoder can be judged against them.
	static const float noiseLvl[BT_LEVELS] = { 0.600f, 0.645f, 0.693f, 0.745f, 0.801f, 0.861f };

	// The tilt costs about 5dB on half the bits, so the levels above have to be
	// scaled back to put the collapse in view rather than off the end of the
	// scale. 0.64 is what measured a usable gradient when this ran as its own row.
	static const float noiseScale[BT_TILTS] = { 0.64f };

	// The live counters are what the operator reads, so the test must not appear
	// in them. Saved and put back rather than suppressed at every increment.
	uint32_t sFlag = aprsFlagCount, sGood = aprsGoodCount, sBad = aprsBadCount;
	uint32_t sPlaus = aprsPlausibleCount, sPre = aprsPreambleCount;
	uint32_t sHeard = aprsHeardCount;
	uint32_t sWinG[AFSK_PHASE_COUNT], sWinC[AFSK_GATE_COUNT];
	float sMark = aprsMarkAvg, sSpace = aprsSpaceAvg, sToneSum = aprsToneSum;
	int sToneCount = aprsToneCount, sTonePeak = aprsTonePeakPct;

	for (int i = 0; i < AFSK_PHASE_COUNT; i++)
	{
		sWinG[i] = aprsWinByGain[i];
	}
	for (int i = 0; i < AFSK_GATE_COUNT; i++)
	{
		sWinC[i] = aprsWinByClock[i];
	}

	btRunning = true; // stop the live audio tap feeding the detector mid-test
	btRng = 12345u;

	// Zeroed so the tallies that follow count only the test's own packets.
	for (int i = 0; i < AFSK_PHASE_COUNT; i++)
	{
		aprsWinByGain[i] = 0;
	}
	for (int i = 0; i < AFSK_GATE_COUNT; i++)
	{
		aprsWinByClock[i] = 0;
	}
	aprsHeardCount = 0;

	for (int t = 0; t < BT_TILTS; t++)
	{
		for (int l = 0; l < BT_LEVELS; l++)
		{
			int ok = 0;

			btMarkAmp = 8000.0f;
			btSpaceAmp = (8000.0f * tiltSpace[t]);
			btNoiseAmp = (8000.0f * noiseLvl[l] * noiseScale[t]);

			for (int p = 0; p < BT_PACKETS; p++)
			{
				if (btRunOnePacket())
				{
					ok++;
				}
			}
			btScore[t][l] = (uint8_t) ok;
		}
	}

	aprsPendingLen = 0;

	// Read the test's own tallies before the live counters go back.
	for (int i = 0; i < AFSK_PHASE_COUNT; i++)
	{
		uint32_t w = aprsWinByGain[i];

		btWinGain[i] = (uint8_t)((w > 255u) ? 255u : w);
	}
	for (int i = 0; i < AFSK_GATE_COUNT; i++)
	{
		uint32_t w = aprsWinByClock[i];

		btWinFilter[i] = (uint8_t)((w > 255u) ? 255u : w);
	}

	aprsFlagCount = sFlag;
	aprsGoodCount = sGood;
	aprsBadCount = sBad;
	aprsHeardCount = sHeard;
	aprsPlausibleCount = sPlaus;
	aprsPreambleCount = sPre;
	aprsMarkAvg = sMark;
	aprsSpaceAvg = sSpace;
	aprsToneSum = sToneSum;
	aprsToneCount = sToneCount;
	aprsTonePeakPct = sTonePeak;
	for (int i = 0; i < AFSK_PHASE_COUNT; i++)
	{
		aprsWinByGain[i] = sWinG[i];
	}
	for (int i = 0; i < AFSK_GATE_COUNT; i++)
	{
		aprsWinByClock[i] = sWinC[i];
	}

	btRunning = false;
}

int aprsDecodeGetBenchScore(int tilt, int level)
{
	if ((tilt < 0) || (tilt >= BT_TILTS) || (level < 0) || (level >= BT_LEVELS))
	{
		return 0;
	}
	return (int) btScore[tilt][level];
}

int aprsDecodeGetBenchTotal(void)
{
	return BT_PACKETS;
}

int aprsDecodeGetBenchLevels(void)
{
	return BT_LEVELS;
}

int aprsDecodeGetBenchGainWin(int gainIndex)
{
	if ((gainIndex < 0) || (gainIndex >= AFSK_PHASE_COUNT))
	{
		return 0;
	}
	return (int) btWinGain[gainIndex];
}

int aprsDecodeGetBenchFilterWin(int filterIndex)
{
	if ((filterIndex < 0) || (filterIndex >= AFSK_GATE_COUNT))
	{
		return 0;
	}
	return (int) btWinFilter[filterIndex];
}

void aprsDecodeClear(void)
{
	aprsLogCount = 0;
	aprsFlagCount = 0;
	aprsGoodCount = 0;
	aprsBadCount = 0;
	aprsHeardCount = 0;
	aprsPlausibleCount = 0;
	aprsPreambleCount = 0;
	aprsToneSum = 0.0f;
	aprsToneCount = 0;
	aprsTonePeakPct = 0;
	aprsMarkAvg = 0.0f;
	aprsSpaceAvg = 0.0f;
	aprsLastBadFrom[0] = 0;
	aprsPendingLen = 0;

	for (int i = 0; i < AFSK_PHASE_COUNT; i++)
	{
		aprsWinByGain[i] = 0;
	}
	// Two separate counts: one phase filter tally, but a clock for every steering
	// gain crossed with every filter. Sharing one loop would run off the end of
	// the shorter array.
	for (int i = 0; i < AFSK_GATE_COUNT; i++)
	{
		aprsWinByClock[i] = 0;
	}
	for (int i = 0; i < AFSK_CLOCK_COUNT; i++)
	{
		afskClock[i] = 0;
	}

	for (int i = 0; i < APRS_DECODER_COUNT; i++)
	{
		memset(&hdlc[i], 0, sizeof(hdlc[i]));
		hdlc[i].crc = 0xFFFF;
	}
}

const char *aprsDecodeGetLastBadFrom(void)
{
	return aprsLastBadFrom;
}

// Which slicer gain, and which clock, actually recovered the frames. The point of
// showing these is to replace guesswork: if the highest gain takes most of them,
// the de-emphasis correction is doing the work and the set should move further in
// that direction; if the lowest still wins, the tilt is smaller than calculated.
// The measured tone tilt, as the space gain that would correct it, in tenths.
// 30 means the space tone arrives three times weaker in power and gain 3.0 is
// right; 10 means the two arrive level and there is nothing to correct.
//
// Returns 0 until enough confident samples have been seen to mean anything -
// there is no point showing a number derived from silence.
uint32_t aprsDecodeGetHeard(void)
{
	return aprsHeardCount;
}

int aprsDecodeGetTiltX10(void)
{
	if ((aprsSpaceAvg < 1.0f) || (aprsMarkAvg < 1.0f))
	{
		return 0;
	}

	{
		int v = (int)((aprsMarkAvg * 10.0f) / aprsSpaceAvg);

		return ((v > 999) ? 999 : v);
	}
}

uint32_t aprsDecodeGetGainWins(int gainIndex)
{
	if ((gainIndex < 0) || (gainIndex >= AFSK_PHASE_COUNT))
	{
		return 0;
	}
	return aprsWinByGain[gainIndex];
}

uint32_t aprsDecodeGetClockWins(int clockIndex)
{
	if ((clockIndex < 0) || (clockIndex >= AFSK_GATE_COUNT))
	{
		return 0;
	}
	return aprsWinByClock[clockIndex];
}


int aprsDecodeGetTonePeakPct(void)
{
	return aprsTonePeakPct;
}

bool aprsDecodeIsMessageForMe(int index)
{
	int i = (aprsLogCount - 1 - index);

	return (((i >= 0) && (i < APRS_LOG_ENTRIES)) ? (aprsLog[i].forMe != 0) : false);
}

const char *aprsDecodeGetMessageText(int index)
{
	int i = (aprsLogCount - 1 - index);

	if ((i < 0) || (i >= APRS_LOG_ENTRIES) || (aprsLog[i].forMe == 0))
	{
		return "";
	}
	return (aprsLog[i].info + aprsLog[i].textAt);
}

const char *aprsDecodeGetMessageId(int index)
{
	int i = (aprsLogCount - 1 - index);

	return (((i >= 0) && (i < APRS_LOG_ENTRIES)) ? aprsLog[i].msgId : "");
}
