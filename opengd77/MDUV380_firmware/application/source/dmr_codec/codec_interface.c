/*
 * Copyright (C) 2019 Kai Ludwig, DG4KLU
 *           (C) 2020-2024 Roger Clark, VK3KYY / G4KYF
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

#include "dmr_codec/codec.h"
#include "functions/voicePrompts.h"
#include <string.h>

static uint16_t bitbuffer_encode[72];

void codecDecode(uint8_t *indata_ptr, int numbBlocks)
{
	uint16_t bitbuffer_decode[49];


	register int r0 asm ("r0") __attribute__((unused));
	register int r1 asm ("r1") __attribute__((unused));
	register int r2 asm ("r2") __attribute__((unused));

    for (int idx = 0; idx < numbBlocks; idx++)
    {
		initFrame(indata_ptr, bitbuffer_decode);
		indata_ptr += 9;

		soundSetupBuffer();// this just sets currentWaveBuffer but the compiler seems to optimise out the code if I try to do it in this file
		r2 = (int)bitbuffer_decode;
		r0 = (int)currentWaveBuffer;
		r1 = (int)ambebuffer_decode;

		asm volatile (
			"PUSH {R4-R11}\n"
			"SUB SP, SP, #0x10\n"
			"STR R1, [SP, #0x08]\n"
			"LDR R1, =0\n"
			"STR R1, [SP, #0x04]\n"
			"LDR R1, =0\n"
			"STR R1, [SP, #0x00]\n"
			"LDR R3, =0\n"
			"LDR R1, =80\n"
			"LDR R12, =" QU(AMBE_DECODE) "\n"
			"BLX R12\n"
			"ADD SP, SP, #0x10\n"
			"POP {R4-R11}"
		);

		soundStoreBuffer();

		soundSetupBuffer();// this just sets currentWaveBuffer but the compiler seems to optimise out the code if I try to do it in this file
		r2 = (int)bitbuffer_decode;
		r0 = (int)currentWaveBuffer;
		r1 = (int)ambebuffer_decode;

		asm volatile (
			"PUSH {R4-R11}\n"
			"SUB SP, SP, #0x10\n"
			"STR R1, [SP, #0x08]\n"
			"LDR R1, =1\n"
			"STR R1, [SP, #0x04]\n"
			"LDR R1, =0\n"
			"STR R1, [SP, #0x00]\n"
			"LDR R3, =0\n"
			"LDR R1, =80\n"
			"LDR R12, =" QU(AMBE_DECODE) "\n"
			"BLX R12\n"
			"ADD SP, SP, #0x10\n"
			"POP {R4-R11}"
		);

		soundStoreBuffer();
    }
}

// The same three codec calls as codecEncodeBlock() below, on audio the caller
// hands in rather than the next two microphone buffers. The registers go in as asm
// operands, which pins them in r0-r2 at each call whatever the optimiser does
// around them; the clobbers cover what the codec, as an ordinary AAPCS function,
// may change.
void codecEncodeBlockFromPCM(uint8_t *outdata_ptr, const uint8_t *pcm)
{
	memset((uint8_t *)outdata_ptr, 0, 9);
	memset(bitbuffer_encode, 0, sizeof(bitbuffer_encode));

	{
		register int r0 asm ("r0") = (int)bitbuffer_encode;
		register int r1 asm ("r1") = (int)ambebuffer_encode;
		register int r2 asm ("r2") = (int)pcm;

		asm volatile (
			"PUSH {R4-R11}\n"
			"SUB SP, SP, #0x14\n"
			"STR R1, [SP, #0x0C]\n"
			"LDR R1, =0x00002000\n"
			"STR R1, [SP, #0x08]\n"
			"LDR R1, =0\n"
			"STR R1, [SP, #0x04]\n"
			"LDR R1, =0x00001840\n"
			"STR R1, [SP, #0x00]\n"
			"LDR R3, =80\n"
			"LDR R1, =0\n"
			"LDR R12, =" QU(AMBE_ENCODE) "\n"
			"BLX R12\n"
			"ADD SP, SP, #0x14\n"
			"POP {R4-R11}"
			: "+r" (r0), "+r" (r1), "+r" (r2)
			:
			: "r3", "r12", "lr", "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7"
		);
	}

	{
		register int r0 asm ("r0") = (int)bitbuffer_encode;
		register int r1 asm ("r1") = (int)ambebuffer_encode;
		register int r2 asm ("r2") = (int)(pcm + WAV_BUFFER_SIZE);

		asm volatile (
			"PUSH {R4-R11}\n"
			"SUB SP, SP, #0x14\n"
			"STR R1, [SP, #0x0C]\n"
			"LDR R1, =0x00002000\n"
			"STR R1, [SP, #0x08]\n"
			"LDR R1, =1\n"
			"STR R1, [SP, #0x04]\n"
			"LDR R1, =0x00000800\n"
			"STR R1, [SP, #0x00]\n"
			"LDR R3, =80\n"
			"LDR R1, =0\n"
			"LDR R12, =" QU(AMBE_ENCODE) "\n"
			"BLX R12\n"
			"ADD SP, SP, #0x14\n"
			"POP {R4-R11}"
			: "+r" (r0), "+r" (r1), "+r" (r2)
			:
			: "r3", "r12", "lr", "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7"
		);
	}

	{
		register int r0 asm ("r0") = (int)bitbuffer_encode;
		register int r1 asm ("r1") = (int)ambebuffer_encode_ecc;

		asm volatile (
			"PUSH {R4-R11}\n"
			"SUB SP, SP, #0x14\n"
			"MOV R3, R1\n"
			"LDR R2, =0\n"
			"MOV R1, R0\n"
			"LDR R12, =" QU(AMBE_ENCODE_ECC) "\n"
			"BLX R12\n"
			"ADD SP, SP, #0x14\n"
			"POP {R4-R11}"
			: "+r" (r0), "+r" (r1)
			:
			: "r2", "r3", "r12", "lr", "cc", "memory", "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7"
		);
	}

	for (int i = 0; i < 72; i++)
	{
		if (bitbuffer_encode[i] & 1)
		{
			outdata_ptr[i >> 3] |= 128 >> (i & 7);
		}
	}
}

void codecEncodeBlock(uint8_t *outdata_ptr)
{
	register int r0 asm ("r0") __attribute__((unused));
	register int r1 asm ("r1") __attribute__((unused));
	register int r2 asm ("r2") __attribute__((unused));

	memset((uint8_t *)outdata_ptr, 0, 9);// fills with zeros
	memset(bitbuffer_encode, 0, sizeof(bitbuffer_encode));// faster to call memset as it will be compiled as optimised code


	soundRetrieveBuffer();// gets currentWaveBuffer pointer used as input r2 to the encoder

	r0 = (int)bitbuffer_encode;
	r2 = (int)currentWaveBuffer;//tmp_wavbuffer;
	r1 = (int)ambebuffer_encode;// seems to be a hard coded (defined) memory address of 0x1FFF6B60. I'm not sure why it has to be hard coded, since its passed as a paramater (register)

	asm volatile (
		"PUSH {R4-R11}\n"
		"SUB SP, SP, #0x14\n"
		"STR R1, [SP, #0x0C]\n"
		"LDR R1, =0x00002000\n"
		"STR R1, [SP, #0x08]\n"
		"LDR R1, =0\n"
		"STR R1, [SP, #0x04]\n"
		"LDR R1, =0x00001840\n"
		"STR R1, [SP, #0x00]\n"
		"LDR R3, =80\n"
		"LDR R1, =0\n"
		"LDR R12, =" QU(AMBE_ENCODE) "\n"
		"BLX R12\n"
		"ADD SP, SP, #0x14\n"
		"POP {R4-R11}"
	);

	soundRetrieveBuffer();// gets currentWaveBuffer pointer used as input r2 to the encoder

	r0 = (int)bitbuffer_encode;
	r2 = (int)currentWaveBuffer;//tmp_wavbuffer;
	r1 = (int)ambebuffer_encode;

	asm volatile (
		"PUSH {R4-R11}\n"
		"SUB SP, SP, #0x14\n"
		"STR R1, [SP, #0x0C]\n"
		"LDR R1, =0x00002000\n"
		"STR R1, [SP, #0x08]\n"
		"LDR R1, =1\n"
		"STR R1, [SP, #0x04]\n"
		"LDR R1, =0x00000800\n"
		"STR R1, [SP, #0x00]\n"
		"LDR R3, =80\n"
		"LDR R1, =0\n"
		"LDR R12, =" QU(AMBE_ENCODE) "\n"
		"BLX R12\n"
		"ADD SP, SP, #0x14\n"
		"POP {R4-R11}"
	);

	r0 = (int)bitbuffer_encode;
	r1 = (int)ambebuffer_encode_ecc;

	asm volatile (
		"PUSH {R4-R11}\n"
		"SUB SP, SP, #0x14\n"
		"MOV R3, R1\n"
		"LDR R2, =0\n"
		"MOV R1, R0\n"
		"LDR R12, =" QU(AMBE_ENCODE_ECC) "\n"
		"BLX R12\n"
		"ADD SP, SP, #0x14\n"
		"POP {R4-R11}"
	);

	for (int i = 0; i < 72; i++)
	{
		if (bitbuffer_encode[i] & 1)
		{
			outdata_ptr[i >> 3] |= 128 >> (i & 7);
		}
	}
}
