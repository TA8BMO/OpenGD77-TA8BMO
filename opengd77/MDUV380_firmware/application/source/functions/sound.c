/*
 * Copyright (C) 2019      Kai Ludwig, DG4KLU
 * Copyright (C) 2019-2024 Roger Clark, VK3KYY / G4KYF
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

#include <stdlib.h>
#include "hardware/HR-C6000.h"
#include "hardware/radioHardwareInterface.h"
#include "functions/settings.h"
#include "functions/sound.h"
#include "functions/voicePrompts.h"
#include "functions/rxPowerSaving.h"
#include "interfaces/interrupts.h"


#define MIC_AVERAGE_COUNTER_RELOAD     10

static void soundBeepTaskFunction(void *data);

typedef union byteSwap16
{
	int16_t byte16;
	uint8_t bytes8[2];
} byteSwap16_t;
byteSwap16_t swapper;

Task_t beepTask;


__attribute__((section(".ccmram"))) union sharedDataBuffer audioAndHotspotDataBuffer;
volatile int16_t  wavbuffer_read_idx;
volatile int16_t  wavbuffer_write_idx;
volatile int16_t wavbuffer_count;
volatile uint8_t *currentWaveBuffer;

static const int16_t sine_beep16[] =  {0,101,201,302,402,503,603,704,804,905,1005,1106,1206,1307,1407,1507,1608,1708,1809,1909,2009,2110,2210,2310,2410,2511,2611,2711,2811,2911,3012,3112,3212,3312,3412,3512,3612,3712,3811,3911,4011,4111,4210,4310,4410,4509,4609,4708,4808,4907,5007,5106,5205,5305,5404,5503,5602,5701,5800,5899,5998,6096,6195,6294,6393,6491,6590,6688,6786,6885,6983,7081,7179,7277,7375,7473,7571,7669,7767,7864,7962,8059,8157,8254,8351,8448,8545,8642,8739,8836,8933,9030,9126,9223,9319,9416,9512,9608,9704,9800,9896,9992,10087,10183,10278,10374,10469,10564,10659,10754,10849,10944,11039,11133,11228,11322,11417,11511,11605,11699,11793,11886,11980,12074,12167,12260,12353,12446,12539,12632,12725,12817,12910,13002,13094,13187,13279,13370,13462,13554,13645,13736,13828,13919,14010,14101,14191,14282,14372,14462,14553,14643,14732,14822,14912,15001,15090,15180,15269,15358,15446,15535,15623,15712,15800,15888,15976,16063,16151,16238,16325,16413,16499,16586,16673,16759,16846,16932,17018,17104,17189,17275,17360,17445,17530,17615,17700,17784,17869,17953,18037,18121,18204,18288,18371,18454,18537,18620,18703,18785,18868,18950,19032,19113,19195,19276,19357,19438,19519,19600,19680,19761,19841,19921,20000,20080,20159,20238,20317,20396,20475,20553,20631,20709,20787,20865,20942,21019,21096,21173,21250,21326,21403,21479,21554,21630,21705,21781,21856,21930,22005,22079,22154,22227,22301,22375,22448,22521,22594,22667,22739,22812,22884,22956,23027,23099,23170,23241,23311,23382,23452,23522,23592,23662,23731,23801,23870,23938,24007,24075,24143,24211,24279,24346,24413,24480,24547,24613,24680,24746,24811,24877,24942,25007,25072,25137,25201,25265,25329,25393,25456,25519,25582,25645,25708,25770,25832,25893,25955,26016,26077,26138,26198,26259,26319,26378,26438,26497,26556,26615,26674,26732,26790,26848,26905,26962,27019,27076,27133,27189,27245,27300,27356,27411,27466,27521,27575,27629,27683,27737,27790,27843,27896,27949,28001,28053,28105,28157,28208,28259,28310,28360,28411,28460,28510,28560,28609,28658,28706,28755,28803,28850,28898,28945,28992,29039,29085,29131,29177,29223,29268,29313,29358,29403,29447,29491,29534,29578,29621,29664,29706,29749,29791,29832,29874,29915,29956,29997,30037,30077,30117,30156,30195,30234,30273,30311,30349,30387,30424,30462,30498,30535,30571,30607,30643,30679,30714,30749,30783,30818,30852,30885,30919,30952,30985,31017,31050,31082,31113,31145,31176,31206,31237,31267,31297,31327,31356,31385,31414,31442,31470,31498,31526,31553,31580,31607,31633,31659,31685,31710,31736,31760,31785,31809,31833,31857,31880,31903,31926,31949,31971,31993,32014,32036,32057,32077,32098,32118,32137,32157,32176,32195,32213,32232,32250,32267,32285,32302,32318,32335,32351,32367,32382,32397,32412,32427,32441,32455,32469,32482,32495,32508,32521,32533,32545,32556,32567,32578,32589,32599,32609,32619,32628,32637,32646,32655,32663,32671,32678,32685,32692,32699,32705,32711,32717,32722,32728,32732,32737,32741,32745,32748,32752,32755,32757,32759,32761,32763,32765,32766,32766,32767,32767,32767,32766,32766,32765,32763,32761,32759,32757,32755,32752,32748,32745,32741,32737,32732,32728,32722,32717,32711,32705,32699,32692,32685,32678,32671,32663,32655,32646,32637,32628,32619,32609,32599,32589,32578,32567,32556,32545,32533,32521,32508,32495,32482,32469,32455,32441,32427,32412,32397,32382,32367,32351,32335,32318,32302,32285,32267,32250,32232,32213,32195,32176,32157,32137,32118,32098,32077,32057,32036,32014,31993,31971,31949,31926,31903,31880,31857,31833,31809,31785,31760,31736,31710,31685,31659,31633,31607,31580,31553,31526,31498,31470,31442,31414,31385,31356,31327,31297,31267,31237,31206,31176,31145,31113,31082,31050,31017,30985,30952,30919,30885,30852,30818,30783,30749,30714,30679,30643,30607,30571,30535,30498,30462,30424,30387,30349,30311,30273,30234,30195,30156,30117,30077,30037,29997,29956,29915,29874,29832,29791,29749,29706,29664,29621,29578,29534,29491,29447,29403,29358,29313,29268,29223,29177,29131,29085,29039,28992,28945,28898,28850,28803,28755,28706,28658,28609,28560,28510,28460,28411,28360,28310,28259,28208,28157,28105,28053,28001,27949,27896,27843,27790,27737,27683,27629,27575,27521,27466,27411,27356,27300,27245,27189,27133,27076,27019,26962,26905,26848,26790,26732,26674,26615,26556,26497,26438,26378,26319,26259,26198,26138,26077,26016,25955,25893,25832,25770,25708,25645,25582,25519,25456,25393,25329,25265,25201,25137,25072,25007,24942,24877,24811,24746,24680,24613,24547,24480,24413,24346,24279,24211,24143,24075,24007,23938,23870,23801,23731,23662,23592,23522,23452,23382,23311,23241,23170,23099,23027,22956,22884,22812,22739,22667,22594,22521,22448,22375,22301,22227,22154,22079,22005,21930,21856,21781,21705,21630,21554,21479,21403,21326,21250,21173,21096,21019,20942,20865,20787,20709,20631,20553,20475,20396,20317,20238,20159,20080,20000,19921,19841,19761,19680,19600,19519,19438,19357,19276,19195,19113,19032,18950,18868,18785,18703,18620,18537,18454,18371,18288,18204,18121,18037,17953,17869,17784,17700,17615,17530,17445,17360,17275,17189,17104,17018,16932,16846,16759,16673,16586,16499,16413,16325,16238,16151,16063,15976,15888,15800,15712,15623,15535,15446,15358,15269,15180,15090,15001,14912,14822,14732,14643,14553,14462,14372,14282,14191,14101,14010,13919,13828,13736,13645,13554,13462,13370,13279,13187,13094,13002,12910,12817,12725,12632,12539,12446,12353,12260,12167,12074,11980,11886,11793,11699,11605,11511,11417,11322,11228,11133,11039,10944,10849,10754,10659,10564,10469,10374,10278,10183,10087,9992,9896,9800,9704,9608,9512,9416,9319,9223,9126,9030,8933,8836,8739,8642,8545,8448,8351,8254,8157,8059,7962,7864,7767,7669,7571,7473,7375,7277,7179,7081,6983,6885,6786,6688,6590,6491,6393,6294,6195,6096,5998,5899,5800,5701,5602,5503,5404,5305,5205,5106,5007,4907,4808,4708,4609,4509,4410,4310,4210,4111,4011,3911,3811,3712,3612,3512,3412,3312,3212,3112,3012,2911,2811,2711,2611,2511,2410,2310,2210,2110,2009,1909,1809,1708,1608,1507,1407,1307,1206,1106,1005,905,804,704,603,503,402,302,201,101,0,-101,-201,-302,-402,-503,-603,-704,-804,-905,-1005,-1106,-1206,-1307,-1407,-1507,-1608,-1708,-1809,-1909,-2009,-2110,-2210,-2310,-2410,-2511,-2611,-2711,-2811,-2911,-3012,-3112,-3212,-3312,-3412,-3512,-3612,-3712,-3811,-3911,-4011,-4111,-4210,-4310,-4410,-4509,-4609,-4708,-4808,-4907,-5007,-5106,-5205,-5305,-5404,-5503,-5602,-5701,-5800,-5899,-5998,-6096,-6195,-6294,-6393,-6491,-6590,-6688,-6786,-6885,-6983,-7081,-7179,-7277,-7375,-7473,-7571,-7669,-7767,-7864,-7962,-8059,-8157,-8254,-8351,-8448,-8545,-8642,-8739,-8836,-8933,-9030,-9126,-9223,-9319,-9416,-9512,-9608,-9704,-9800,-9896,-9992,-10087,-10183,-10278,-10374,-10469,-10564,-10659,-10754,-10849,-10944,-11039,-11133,-11228,-11322,-11417,-11511,-11605,-11699,-11793,-11886,-11980,-12074,-12167,-12260,-12353,-12446,-12539,-12632,-12725,-12817,-12910,-13002,-13094,-13187,-13279,-13370,-13462,-13554,-13645,-13736,-13828,-13919,-14010,-14101,-14191,-14282,-14372,-14462,-14553,-14643,-14732,-14822,-14912,-15001,-15090,-15180,-15269,-15358,-15446,-15535,-15623,-15712,-15800,-15888,-15976,-16063,-16151,-16238,-16325,-16413,-16499,-16586,-16673,-16759,-16846,-16932,-17018,-17104,-17189,-17275,-17360,-17445,-17530,-17615,-17700,-17784,-17869,-17953,-18037,-18121,-18204,-18288,-18371,-18454,-18537,-18620,-18703,-18785,-18868,-18950,-19032,-19113,-19195,-19276,-19357,-19438,-19519,-19600,-19680,-19761,-19841,-19921,-20000,-20080,-20159,-20238,-20317,-20396,-20475,-20553,-20631,-20709,-20787,-20865,-20942,-21019,-21096,-21173,-21250,-21326,-21403,-21479,-21554,-21630,-21705,-21781,-21856,-21930,-22005,-22079,-22154,-22227,-22301,-22375,-22448,-22521,-22594,-22667,-22739,-22812,-22884,-22956,-23027,-23099,-23170,-23241,-23311,-23382,-23452,-23522,-23592,-23662,-23731,-23801,-23870,-23938,-24007,-24075,-24143,-24211,-24279,-24346,-24413,-24480,-24547,-24613,-24680,-24746,-24811,-24877,-24942,-25007,-25072,-25137,-25201,-25265,-25329,-25393,-25456,-25519,-25582,-25645,-25708,-25770,-25832,-25893,-25955,-26016,-26077,-26138,-26198,-26259,-26319,-26378,-26438,-26497,-26556,-26615,-26674,-26732,-26790,-26848,-26905,-26962,-27019,-27076,-27133,-27189,-27245,-27300,-27356,-27411,-27466,-27521,-27575,-27629,-27683,-27737,-27790,-27843,-27896,-27949,-28001,-28053,-28105,-28157,-28208,-28259,-28310,-28360,-28411,-28460,-28510,-28560,-28609,-28658,-28706,-28755,-28803,-28850,-28898,-28945,-28992,-29039,-29085,-29131,-29177,-29223,-29268,-29313,-29358,-29403,-29447,-29491,-29534,-29578,-29621,-29664,-29706,-29749,-29791,-29832,-29874,-29915,-29956,-29997,-30037,-30077,-30117,-30156,-30195,-30234,-30273,-30311,-30349,-30387,-30424,-30462,-30498,-30535,-30571,-30607,-30643,-30679,-30714,-30749,-30783,-30818,-30852,-30885,-30919,-30952,-30985,-31017,-31050,-31082,-31113,-31145,-31176,-31206,-31237,-31267,-31297,-31327,-31356,-31385,-31414,-31442,-31470,-31498,-31526,-31553,-31580,-31607,-31633,-31659,-31685,-31710,-31736,-31760,-31785,-31809,-31833,-31857,-31880,-31903,-31926,-31949,-31971,-31993,-32014,-32036,-32057,-32077,-32098,-32118,-32137,-32157,-32176,-32195,-32213,-32232,-32250,-32267,-32285,-32302,-32318,-32335,-32351,-32367,-32382,-32397,-32412,-32427,-32441,-32455,-32469,-32482,-32495,-32508,-32521,-32533,-32545,-32556,-32567,-32578,-32589,-32599,-32609,-32619,-32628,-32637,-32646,-32655,-32663,-32671,-32678,-32685,-32692,-32699,-32705,-32711,-32717,-32722,-32728,-32732,-32737,-32741,-32745,-32748,-32752,-32755,-32757,-32759,-32761,-32763,-32765,-32766,-32766,-32767,-32767,-32767,-32766,-32766,-32765,-32763,-32761,-32759,-32757,-32755,-32752,-32748,-32745,-32741,-32737,-32732,-32728,-32722,-32717,-32711,-32705,-32699,-32692,-32685,-32678,-32671,-32663,-32655,-32646,-32637,-32628,-32619,-32609,-32599,-32589,-32578,-32567,-32556,-32545,-32533,-32521,-32508,-32495,-32482,-32469,-32455,-32441,-32427,-32412,-32397,-32382,-32367,-32351,-32335,-32318,-32302,-32285,-32267,-32250,-32232,-32213,-32195,-32176,-32157,-32137,-32118,-32098,-32077,-32057,-32036,-32014,-31993,-31971,-31949,-31926,-31903,-31880,-31857,-31833,-31809,-31785,-31760,-31736,-31710,-31685,-31659,-31633,-31607,-31580,-31553,-31526,-31498,-31470,-31442,-31414,-31385,-31356,-31327,-31297,-31267,-31237,-31206,-31176,-31145,-31113,-31082,-31050,-31017,-30985,-30952,-30919,-30885,-30852,-30818,-30783,-30749,-30714,-30679,-30643,-30607,-30571,-30535,-30498,-30462,-30424,-30387,-30349,-30311,-30273,-30234,-30195,-30156,-30117,-30077,-30037,-29997,-29956,-29915,-29874,-29832,-29791,-29749,-29706,-29664,-29621,-29578,-29534,-29491,-29447,-29403,-29358,-29313,-29268,-29223,-29177,-29131,-29085,-29039,-28992,-28945,-28898,-28850,-28803,-28755,-28706,-28658,-28609,-28560,-28510,-28460,-28411,-28360,-28310,-28259,-28208,-28157,-28105,-28053,-28001,-27949,-27896,-27843,-27790,-27737,-27683,-27629,-27575,-27521,-27466,-27411,-27356,-27300,-27245,-27189,-27133,-27076,-27019,-26962,-26905,-26848,-26790,-26732,-26674,-26615,-26556,-26497,-26438,-26378,-26319,-26259,-26198,-26138,-26077,-26016,-25955,-25893,-25832,-25770,-25708,-25645,-25582,-25519,-25456,-25393,-25329,-25265,-25201,-25137,-25072,-25007,-24942,-24877,-24811,-24746,-24680,-24613,-24547,-24480,-24413,-24346,-24279,-24211,-24143,-24075,-24007,-23938,-23870,-23801,-23731,-23662,-23592,-23522,-23452,-23382,-23311,-23241,-23170,-23099,-23027,-22956,-22884,-22812,-22739,-22667,-22594,-22521,-22448,-22375,-22301,-22227,-22154,-22079,-22005,-21930,-21856,-21781,-21705,-21630,-21554,-21479,-21403,-21326,-21250,-21173,-21096,-21019,-20942,-20865,-20787,-20709,-20631,-20553,-20475,-20396,-20317,-20238,-20159,-20080,-20000,-19921,-19841,-19761,-19680,-19600,-19519,-19438,-19357,-19276,-19195,-19113,-19032,-18950,-18868,-18785,-18703,-18620,-18537,-18454,-18371,-18288,-18204,-18121,-18037,-17953,-17869,-17784,-17700,-17615,-17530,-17445,-17360,-17275,-17189,-17104,-17018,-16932,-16846,-16759,-16673,-16586,-16499,-16413,-16325,-16238,-16151,-16063,-15976,-15888,-15800,-15712,-15623,-15535,-15446,-15358,-15269,-15180,-15090,-15001,-14912,-14822,-14732,-14643,-14553,-14462,-14372,-14282,-14191,-14101,-14010,-13919,-13828,-13736,-13645,-13554,-13462,-13370,-13279,-13187,-13094,-13002,-12910,-12817,-12725,-12632,-12539,-12446,-12353,-12260,-12167,-12074,-11980,-11886,-11793,-11699,-11605,-11511,-11417,-11322,-11228,-11133,-11039,-10944,-10849,-10754,-10659,-10564,-10469,-10374,-10278,-10183,-10087,-9992,-9896,-9800,-9704,-9608,-9512,-9416,-9319,-9223,-9126,-9030,-8933,-8836,-8739,-8642,-8545,-8448,-8351,-8254,-8157,-8059,-7962,-7864,-7767,-7669,-7571,-7473,-7375,-7277,-7179,-7081,-6983,-6885,-6786,-6688,-6590,-6491,-6393,-6294,-6195,-6096,-5998,-5899,-5800,-5701,-5602,-5503,-5404,-5305,-5205,-5106,-5007,-4907,-4808,-4708,-4609,-4509,-4410,-4310,-4210,-4111,-4011,-3911,-3811,-3712,-3612,-3512,-3412,-3312,-3212,-3112,-3012,-2911,-2811,-2711,-2611,-2511,-2410,-2310,-2210,-2110,-2009,-1909,-1809,-1708,-1608,-1507,-1407,-1307,-1206,-1106,-1005,-905,-804,-704,-603,-503,-402,-302,-201,-101,};
volatile int16_t sine_beep_freq;
volatile int16_t sine_beep_duration;
volatile int micAudioSamplesTotal;

static volatile uint32_t runningMaxValue = 0;
static volatile int micAudioAverageCounter = MIC_AVERAGE_COUNTER_RELOAD;

__attribute__((section(".ccmram"))) int16_t melody_generic[512];// Note. As we don't play long melodies, I think this value can be made smaller.
#define DIT_LENGTH  60
#define DAH_LENGTH  3 * DIT_LENGTH
//const int melody_poweron[] = { 440, 300, 466, 300, 494, 300, -1, -1 };
const int16_t MELODY_POWER_ON[] = { 880, DAH_LENGTH,
								0, DIT_LENGTH,
								880, DIT_LENGTH,
								0, DIT_LENGTH,
								880, DAH_LENGTH,
								0, DIT_LENGTH,
								880, DIT_LENGTH,
								0, DIT_LENGTH,
								880, DAH_LENGTH,
								-1, -1 };
const int16_t MELODY_PRIVATE_CALL[] = {
								880, DIT_LENGTH,
								0, DIT_LENGTH,
								880, DAH_LENGTH,
								0, DIT_LENGTH,
								880, DAH_LENGTH,
								0, DIT_LENGTH,
								880, DIT_LENGTH,

								0, DAH_LENGTH,
								880, DAH_LENGTH,
								0, DIT_LENGTH,
								880, DIT_LENGTH,
								0, DIT_LENGTH,
								880, DAH_LENGTH,
								0, DIT_LENGTH,
								880, DIT_LENGTH

								-1, -1 };// Morse letters PC for Private Call
const int16_t MELODY_KEY_BEEP[] = { 600, 60, -1, -1 };
const int16_t MELODY_KEY_LONG_BEEP[] = { 880, 60, -1, -1 };
/* These melodies are not currently used
const int16_t melody_sk1_beep[] = { 466, 60, 0, 60, 466, 60, -1, -1 };
const int16_t melody_sk2_beep[] = { 494, 60, 0, 60, 494, 60, -1, -1 };
const int16_t melody_orange_beep[] = { 440, 60, 494, 60, 440, 60, 494, 60, -1, -1 };
*/
const int16_t MELODY_ACK_BEEP[] = { 440, 120, 660, 120, 880, 120, -1, -1 };
const int16_t MELODY_NACK_BEEP[] = { 494, 120, 466, 120, -1, -1 };
const int16_t MELODY_ERROR_BEEP[] = { 440, 30, 0, 30, 440, 30, 0, 30, 440, 30, -1, -1 };
const int16_t MELODY_TX_TIMEOUT_BEEP[] = { 440, 60, 494, 60, 440, 60, 494, 60, 440, 60, 494, 60, 440, 60, 494, 60, -1, -1 };
const int16_t MELODY_DMR_TX_START_BEEP[] = { 800, 50, -1, -1 };
const int16_t MELODY_DMR_TX_STOP_BEEP[] = { 500, 50, -1, -1 };
const int16_t MELODY_KEY_BEEP_FIRST_ITEM[] = { 800, 100, -1, -1 };
const int16_t MELODY_LOW_BATTERY[] = { 440, 200, 415, 200, 392, 200, -1, -1 };
const int16_t MELODY_APO_TRIGGERED[] = { 440, 100, 392, 100, 440, 100, 392, 100, 440, 100, 392, 100, -1, -1 };
const int16_t MELODY_QUICKKEYS_CLEAR_ACK_BEEP[] = { 880, 120, 660, 120, 440, 120, 660, 120, 880, 120, -1, -1 };
const int16_t MELODY_RX_TGTSCC_WARNING_BEEP[] = { 1200, 20, 0, 20, 1200, 20, 0, 20, 1200, 20, -1, -1 };
const int16_t MELODY_RX_BEEP_BEGIN_BEEP[] = { 1397, 60, -1, -1 };
const int16_t MELODY_RX_BEEP_END_BEEP[] = { 698, 60, -1, -1 };
const int16_t MELODY_RX_BEEP_CALLER_BEGIN_BEEP[] = { 660, 30, 1200, 30, -1, -1 };
const int16_t MELODY_RX_BEEP_CALLER_END_BEEP[] = { 880, 30, 660, 30, -1, -1 };
const int16_t MELODY_1750[] = {1750, 10000, -1, -1};
const int16_t MELODY_DTMF[] = {1209, 10000, -1, -1};
const int16_t MELODY_SK2_BEEP[] = { 392, 120, 784, 120, -1, -1 };
const int16_t MELODY_NSK2_BEEP[] = {784, 120, 392, 120 , -1, -1 };

// To calculate the pitch use a spreadsheet etc   =ROUND(98*POWER(2, (NOTE_NUMBER/12)),0)
static const int16_t freqs[64] = {0,104,110,117,123,131,139,147,156,165,175,185,196,208,220,233,247,262,277,294,311,330,349,370,392,415,440,466,494,523,554,587,622,659,698,740,784,831,880,932,988,1047,1109,1175,1245,1319,1397,1480,1568,1661,1760,1865,1976,2093,2217,2349,2489,2637,2794,2960,3136,3322,3520,3729};

volatile int16_t *melody_play = NULL;
volatile int16_t melody_idx = 0;
int soundBeepVolumeDivider;
static volatile uint8_t audioAmpStatusMask = 0;
#if defined (MD9600_VERSION_2) || defined (MD9600_VERSION_1)
static const uint32_t DELAYED_BEEP_DURATION_MS = 75;
#else
static const uint32_t DELAYED_BEEP_DURATION_MS = 0;
#endif
static bool isInDelayedStart = false;
static uint32_t delayedStartCounter = 0;

volatile float dmrRxAGCrxPeakAverage = DMR_RX_AGC_DEFAULT_PEAK_SAMPLES;
static volatile uint32_t dmrRxAGCpeakRx = 0;
static volatile int lastDMRRxAGCGain = -99;// use initial out of range value for force reload
static const uint32_t DMR_RX_AGC_PEAK_SAMPLES_WINDOW_AVERAGE_SIZE = 100;
static float dmrRxAgcGain = 1.0;
static int AGC_SETTINGS_LUT[]= {1,2,4,8,16,32,64,128};

uint8_t getAudioAmpStatus(void)
{
	return audioAmpStatusMask;
}

void enableAudioAmp(uint8_t mode)
{
	bool wasEnabled = (audioAmpStatusMask != 0x0);

	audioAmpStatusMask |= mode;

	if (!wasEnabled)
	{
		radioAudioAmp(true);
	}
}

void disableAudioAmp(uint8_t mode)
{
	if (audioAmpStatusMask != 0x0)
	{
		audioAmpStatusMask &= ~mode;

		if (audioAmpStatusMask == 0)
		{
			radioAudioAmp(false);
		}
	}
}

void soundSetMelody(const int16_t *melody)
{
	rxPowerSavingSetState(ECOPHASE_POWERSAVE_INACTIVE);

	if ((melody == MELODY_ERROR_BEEP) || (melody == MELODY_ACK_BEEP))
	{
		voicePromptsTerminate();
	}

	if ((melody == NULL) ||
			((codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_NO_BEEP) == 0) &&
					(((nonVolatileSettings.audioPromptMode == AUDIO_PROMPT_MODE_NO_KEY_BEEP) && // no keypad beeps
							((melody == MELODY_KEY_BEEP) || (melody == MELODY_KEY_LONG_BEEP))) == false)))
	{
		taskENTER_CRITICAL();
		if ((voicePromptsIsPlaying() == false) || (melody == NULL))
		{
			sine_beep_freq = 0;
			sine_beep_duration = 0;
			melody_play = (int16_t *)melody;
			melody_idx = 0;
			isInDelayedStart = false;
		}
		taskEXIT_CRITICAL();
	}
}

void soundCreateSong(const uint8_t *melody)
{
	int16_t song_idx = 0;

	for (uint16_t i = 0; i < 255; i++)
	{
		if (melody[(2 * i) + 1] != 0)
		{
			int16_t freqNum = CLAMP(melody[2 * i], 0, 63);

			melody_generic[song_idx++] = freqs[freqNum];
			melody_generic[song_idx++] = melody[(2 * i) + 1] * 27;
		}
		else
		{
			break;
		}
	}

	melody_generic[song_idx++] = -1;
	melody_generic[song_idx] = -1;
}

void soundInitBeepTask(void)
{
	taskENTER_CRITICAL();
	sine_beep_freq = 0;
	sine_beep_duration = 0;
	taskEXIT_CRITICAL();

	xTaskCreate(soundBeepTaskFunction,           /* pointer to the task */
			"beepTask",                          /* task name for kernel awareness debugging */
			1000L / sizeof(portSTACK_TYPE),      /* task stack size */
			NULL,                      			 /* optional task startup argument */
			(UBaseType_t)osPriorityHigh,         /* initial priority */
			&beepTask.Handle 					 /* optional task handle to create */
	);

	beepTask.Running = true;
	beepTask.AliveCount = TASK_FLAGGED_ALIVE;
}

void soundInit(void)
{
//	I2SReset();
	wavbuffer_read_idx = 0;
	wavbuffer_write_idx = 0;
	wavbuffer_count = 0;
}

void soundTerminateSound(void)
{
	I2STerminateTransfers();
}

void soundSetupBuffer(void)
{
	currentWaveBuffer = (uint8_t *)audioAndHotspotDataBuffer.wavbuffer[wavbuffer_write_idx];// cast just to prevent compiler warning
}

void soundStoreBuffer(void)
{
	taskENTER_CRITICAL();
	int16_t tmp_wavbuffer_count = wavbuffer_count;

	if (tmp_wavbuffer_count < WAV_BUFFER_COUNT)
	{
		wavbuffer_write_idx = ((wavbuffer_write_idx + 1) % WAV_BUFFER_COUNT);
		wavbuffer_count++;
	}
	taskEXIT_CRITICAL();
}

void soundRetrieveBuffer(void)
{
	taskENTER_CRITICAL();
	if (wavbuffer_count > 0)
	{
		currentWaveBuffer = (uint8_t *)audioAndHotspotDataBuffer.wavbuffer[wavbuffer_read_idx];// cast just to prevent compiler warning
		wavbuffer_read_idx = ((wavbuffer_read_idx + 1) % WAV_BUFFER_COUNT);
		wavbuffer_count--;
	}
	taskEXIT_CRITICAL();
}

// This function is used to initially fill the I2S buffer
// Subsequent data fills are done using soundRefillData()
bool soundFillData(void)
{
	for(int i = 0; i < 2; i++)
	{
		soundRefillData(i);
	}
	isReceiving = false;
	isSending = true;
	soundStartDMA();
	return true;
}

bool soundRefillData(uint32_t bufNum)
{
	uint32_t samp;

	if (wavbuffer_count >= 2)
	{
		for(int j = 0; j < 2; j++)
		{
			if (((wavbuffer_read_idx % 16) == 0) && !voicePromptsIsPlaying())
			{
				if ((nonVolatileSettings.DMR_RxAGC != 0) && (dmrRxAGCrxPeakAverage != 0))
				{
					dmrRxAgcGain  = DMR_RX_AGC_DEFAULT_PEAK_SAMPLES / dmrRxAGCrxPeakAverage;
					dmrRxAgcGain *= AGC_SETTINGS_LUT[nonVolatileSettings.DMR_RxAGC - 1];

					// hack alert. Arbitrary gain limit
					if (dmrRxAgcGain > 32)
					{
						dmrRxAgcGain = 32;
					}
				}
			}

			dmrRxAGCpeakRx = 0;

			for (int i = 0; i < (WAV_BUFFER_SIZE / 2); i++)
			{
				swapper.bytes8[0] = audioAndHotspotDataBuffer.wavbuffer[wavbuffer_read_idx][2 * i];
				swapper.bytes8[1] = audioAndHotspotDataBuffer.wavbuffer[wavbuffer_read_idx][(2 * i) + 1];
				i2s_Tx_Buffer[bufNum][j][2 * i] = (int16_t)(swapper.byte16 * dmrRxAgcGain);				// Only fill the Left Channel. Right Channel is not used by the HRC6000

				samp = abs(swapper.byte16);

				if (samp > dmrRxAGCpeakRx)
				{
					dmrRxAGCpeakRx = samp;
				}
			}

			// filter out some but not all kerchunkers
			if ((dmrRxAGCpeakRx > 200) && !voicePromptsIsPlaying())
			{
				dmrRxAGCrxPeakAverage -= dmrRxAGCrxPeakAverage / DMR_RX_AGC_PEAK_SAMPLES_WINDOW_AVERAGE_SIZE;
				dmrRxAGCrxPeakAverage += ((float)dmrRxAGCpeakRx) / DMR_RX_AGC_PEAK_SAMPLES_WINDOW_AVERAGE_SIZE;
				if (dmrRxAGCpeakRx > 500)
				{
					LinkHead->rxAGCGain = dmrRxAGCrxPeakAverage;
				}
			}

			wavbuffer_read_idx = ((wavbuffer_read_idx + 1) % WAV_BUFFER_COUNT);
			wavbuffer_count--;
		}
		return (wavbuffer_count > 0);
	}
	else
	{
		memset(i2s_Tx_Buffer[bufNum], 0x00, (2 * (WAV_BUFFER_SIZE * sizeof(uint16_t))));

		dmrRxAGCrxPeakAverage = DMR_RX_AGC_DEFAULT_PEAK_SAMPLES;
#if 0
#if defined(USING_EXTERNAL_DEBUGGER)
		SEGGER_RTT_printf(0, "END %d\n",LinkHead->rxAGCGain);
#endif
#endif
	}

	return false;
}

void soundStartDMA(void)
{
	I2SStartDMA((uint16_t *)i2s_Tx_Buffer, (uint16_t *)i2s_Rx_Buffer, (NUM_I2S_BUFFERS * 2 * WAV_BUFFER_SIZE));
}

// This function is used when receiving
void soundSendData(void)
{
	if (g_TX_SAI_in_use == false)
	{
		g_TX_SAI_in_use = true;
		radioSetAudioPath(false);
		soundFillData();
	}
}

// This function is used at the start of transmission.
bool soundReceiveData(void)
{
	if (trxTransmissionEnabled == false)
	{
		return false;
	}
    isSending = false;
    isReceiving = true;
	soundStartDMA();
	return true;
}

void soundReceiveRefillData(uint32_t bufNum)
{
	if (wavbuffer_count <= (WAV_BUFFER_COUNT - 2))
	{
		for(int j = 0; j < 2; j++)
		{
			for (int i = 0; i < (WAV_BUFFER_SIZE / 2); i++)
			{
				swapper.byte16 = i2s_Rx_Buffer[bufNum][j][i * 2];             // only use the Left Channel of the Mic Audio. Right Channel contains a duplicate.
				audioAndHotspotDataBuffer.wavbuffer[wavbuffer_write_idx][(2 * i) + 1] = swapper.bytes8[1];
				audioAndHotspotDataBuffer.wavbuffer[wavbuffer_write_idx][2 * i] = swapper.bytes8[0];
				if (abs(swapper.byte16) > runningMaxValue)
				{
					runningMaxValue = abs(swapper.byte16);
				}
			}

			if (micAudioAverageCounter-- == 0)
			{
				micAudioAverageCounter = MIC_AVERAGE_COUNTER_RELOAD;
				micAudioSamplesTotal = runningMaxValue;
				runningMaxValue = 0;
			}

			wavbuffer_write_idx = ((wavbuffer_write_idx + 1) % WAV_BUFFER_COUNT);
			wavbuffer_count++;
		}
	}
}


void soundTickRXBuffer(void)
{
	// The AMBE codec decodes 1 DMR frame into 6 buffers.
	// Hence waiting for 12 or more buffers delays the sound playback by 1 DMR frame which gives some effective buffering
	// Max value for this has to be lower than WAV_BUFFER_COUNT.
	if ((wavbuffer_count >= WAV_BUFFER_AMBE_PREBUFFERING_COUNT) && (trxTransmissionEnabled == false))
	{
		soundSendData();
	}
}

void soundStopMelody(void)
{
	if (trxGetMode() == RADIO_MODE_ANALOG)
	{
		/*
		 *  The DM-1801 audio amplifier seems very slow to respond to the control signal
		 *  probably because the amp does not have an integrated enabled / disable pin,
		 *  and instead the power to the amp is turned on and and off via a transistor.
		 *  In FM mode when there is no signal, this results in the unsquelched hiss being heard at the end of the beep,
		 *  in the time between the beep ending and the amp turning off.
		 *  To resolve this problem, the audio mux control to the amp input, is left set to the output of the C6000
		 *  unless there is a RF audio signal.
		 *
		 *  Note. Its quicker just to read the Green LED pin to see if there is an RF signal, but its safer to get he audio amp status
		 *  to see if the amp is already on because of an RF signal.
		 *
		 *  On the GD-77. A click also seems to be induced at the end of the beep when the mux is changed back to the RF chip (AT1846)
		 *  So this fix seems to slightly improve the GD-77 beep on FM
		 */
		if (getAudioAmpStatus() & AUDIO_AMP_MODE_RF)
		{
			HRC6000MuteFmAudio(false);					// Unmute the FM audio
		}
	}

	disableAudioAmp(AUDIO_AMP_MODE_BEEP);
	soundSetMelody(NULL);
}

bool soundMelodyIsPlaying(void)
{
	return (melody_play != NULL);
}

void soundTickMelody(void)
{
	taskENTER_CRITICAL();
	if (melody_play != NULL)
	{
		// <= rather than ==: soundSetMelody() can zero the duration from another
		// task in between the beep task's "> 0" test and its decrement, which
		// leaves it at -1. On == that never matches again, so the melody never
		// advances, soundStopMelody() never runs, and the audio path stays stuck
		// in beep mode until the radio is power cycled.
		if (sine_beep_duration <= 0)
		{
			if (melody_play[melody_idx] == -1)
			{
				soundStopMelody();
			}
			else
			{
				if ((melody_idx == 0) && (!isInDelayedStart))
				{
					HRC6000MuteFmAudio(true);			          //disables the FM audio
					enableAudioAmp(AUDIO_AMP_MODE_BEEP);
					isInDelayedStart = true;
					delayedStartCounter = DELAYED_BEEP_DURATION_MS;
				}
				if (delayedStartCounter == 0)
				{
					sine_beep_freq = melody_play[melody_idx];
					sine_beep_duration = melody_play[melody_idx + 1];
					melody_idx = melody_idx + 2;
				}
				else
				{
					delayedStartCounter--;
				}
			}
		}
	}
	taskEXIT_CRITICAL();
}

static void soundBeepTaskFunction(void *data)
{
	const int WAIT_TIMEOUT_COUNT = 10;
	uint8_t tmp_val;
	int beep_idx = 0;
	bool beep = false;
	uint8_t task_spi_sound[32];
	int waitTimeout;

	while (1U)
	{
			beepTask.AliveCount = TASK_FLAGGED_ALIVE;

			if (sine_beep_duration > 0)
			{
				if (rxPowerSavingIsRxOn() == false)
				{
					rxPowerSavingSetState(ECOPHASE_POWERSAVE_INACTIVE);
				}

				if (!beep)
				{
					waitTimeout = WAIT_TIMEOUT_COUNT;
					// Set C6000 audio path to "OpenMusic" for beep

					interruptsDisableC6000Interrupts();

					while ((SPI0ClearPageRegByteWithMask(0x04, 0x06, 0xFD, 0x02) == -1) &&  ((waitTimeout--) > 0))
					{
						vTaskDelay((1 / portTICK_PERIOD_MS));
					}

					interruptsEnableC6000Interrupts();

					beep = true;
				}

				waitTimeout = WAIT_TIMEOUT_COUNT;

				interruptsDisableC6000Interrupts();

				while ((SPI0ReadPageRegByte(0x04, 0x88, &tmp_val) == -1) &&  ((waitTimeout--) > 0))
				{
					vTaskDelay((1 / portTICK_PERIOD_MS));
				}

				interruptsEnableC6000Interrupts();


				if ( !(tmp_val & 1))
				{
					for (int i = 0; i < 16; i++)
					{
						swapper.byte16 = ((int)sine_beep16[beep_idx]) >> soundBeepVolumeDivider;
						task_spi_sound[(2 * i) + 1] = swapper.bytes8[0];// low byte
						task_spi_sound[2 * i] = swapper.bytes8[1];// high byte

						if (sine_beep_freq != 0)
						{
							beep_idx = beep_idx + (int)(sine_beep_freq / 3.915f);

							// Stay in sine_beep16[] boundaries.
							while (beep_idx >= 0x0800)
							{
								beep_idx = beep_idx - 0x0800;
							}
						}
					}
					waitTimeout = WAIT_TIMEOUT_COUNT;

					interruptsDisableC6000Interrupts();

					while ((SPI0WritePageRegByteArray(0x03, 0x00, task_spi_sound, 0x20) == -1) &&  ((waitTimeout--) > 0))
					{
						vTaskDelay((1 / portTICK_PERIOD_MS));
					}

					interruptsEnableC6000Interrupts();
				}

				// Guarded: another task may have zeroed this while we were busy
				// with the SPI transfers above, and it must never go negative.
				if (sine_beep_duration > 0)
				{
					sine_beep_duration--;
				}
			}
			else
			{
				if (beep)
				{
					waitTimeout = WAIT_TIMEOUT_COUNT;

					interruptsDisableC6000Interrupts();

					while ((SPI0ClearPageRegByteWithMask(0x04, 0x06, 0xFD, 0x00) == -1) &&  ((waitTimeout--) >> 0))
					{
						vTaskDelay((1 / portTICK_PERIOD_MS));
					}

					interruptsEnableC6000Interrupts();

					beep = false;
				}
			}

		vTaskDelay((1 / portTICK_PERIOD_MS));
	}
}
