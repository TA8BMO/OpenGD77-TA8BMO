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

#ifndef _OPENGD77_APRS_DECODE_H_
#define _OPENGD77_APRS_DECODE_H_

#include "main.h"

#define APRS_LOG_ENTRIES  16   // stations, not packets: one heard again replaces its older entry
#define APRS_CALL_LEN     10   // BASECALL-SS plus terminator
#define APRS_INFO_LEN     97   // long beacons were being cut off at 67; 97 rather than 99 leaves room in CCM RAM for when each was heard

// Fed from the receive audio tap, in interrupt context, one half of the I2S
// buffer at a time - the same capture the five-tone and Morse decoders use.
void aprsDecodeAudioBlock(uint32_t bufNum);

// Main application loop: publishes anything the interrupt has completed.
void aprsDecodeTick(bool hasSignal);

// Decoded packets, newest first: index 0 is the most recent, in every one of these.
int         aprsDecodeGetCount(void);
const char *aprsDecodeGetFrom(int index);
const char *aprsDecodeGetInfo(int index);
uint16_t    aprsDecodeGetAgeMinutes(int index); // minutes since the packet was heard
// The digipeater it was heard through - the last one in the path to have
// repeated it - or "" if it was heard straight from the sender.
const char *aprsDecodeGetVia(int index);

// True when the packet is an APRS message addressed to this station. The
// message text alone is then in aprsDecodeGetText(); the identifier, if the
// sender gave one, is what an acknowledgement has to quote back.
bool        aprsDecodeIsMessageForMe(int index);
const char *aprsDecodeGetMessageText(int index);
const char *aprsDecodeGetMessageId(int index);

// Diagnostics. Flags counts HDLC flags seen, so it shows whether the
// demodulator is locking onto anything at all even when no frame passes its
// checksum; good and bad count frames that did and did not.
void aprsDecodeGetStats(uint32_t *flags, uint32_t *good, uint32_t *bad, uint32_t *plausible, uint32_t *preamble);
// Source callsign of the most recent frame that failed its checksum but whose
// address field was well formed. Diagnostic only - it has NOT been verified.
const char *aprsDecodeGetLastBadFrom(void);

// Transmissions that reached the decoder, counted once each. "good" divided by
// this is the real success rate; good over bad is not, because bad counts only
// the first decoder while good counts a recovery by any of the eight.
uint32_t aprsDecodeGetHeard(void);

// Several decoders run in parallel over the same audio, differing in slicer gain
// and in bit clock. These say which combination recovered the frames, so the next
// set of values can be chosen from evidence instead of guessed at. Three gains,
// two clocks; out of range returns 0.
uint32_t aprsDecodeGetGainWins(int gainIndex);
uint32_t aprsDecodeGetClockWins(int clockIndex);

// Measured ratio of mark power to space power while a tone is clearly present,
// in tenths - i.e. the space gain that would level them up. 30 means the tilt
// the de-emphasis would cause is really there; 10 means it is not in this path.
// 0 means not enough signal has been heard yet to say.
int aprsDecodeGetTiltX10(void);

// Repeatable benchmark. Generates a known packet at a fixed set of noise levels
// and tone tilts, feeds it through the real detector, and counts what decoded.
// The same firmware always returns the same score, so two builds can be compared
// - which the on-air counters cannot do, having measured the same binary at 10/10
// one evening and 1/9 the next.
//
// Blocks for a fraction of a second and suspends the live audio tap while it runs.
void aprsDecodeRunBenchmark(void);
int  aprsDecodeGetBenchScore(int tilt, int level); // tilt 0=flat 1=de-emphasised
int  aprsDecodeGetBenchTotal(void);                // packets tried per cell
int  aprsDecodeGetBenchLevels(void);               // noise steps per row
int  aprsDecodeGetBenchGainWin(int gainIndex);     // which sampling phase carried them
int  aprsDecodeGetBenchFilterWin(int filterIndex); // which loop filter carried them

// Best 100ms average of the demodulator confidence, as a percentage. Clean
// Bell 202 sits near 96; noise stays around 20-40. Says whether AFSK is
// reaching the demodulator at all.
int aprsDecodeGetTonePeakPct(void);

void aprsDecodeClear(void);

// Pre-fills the composer's addressee, so replying to a message is one key.
void uiAPRSMessagePresetTo(const char *call);


#endif /* _OPENGD77_APRS_DECODE_H_ */
