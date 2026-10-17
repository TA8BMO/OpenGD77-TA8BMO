/*
 * Copyright (C) 2021-2024 Roger Clark, VK3KYY / G4KYF
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

#include "functions/rxPowerSaving.h"
#include "functions/trx.h"
#include "functions/ticks.h"
#include <interfaces/clockManager.h>
#include "usb/usb_com.h"


#define LOW_SPEED_CLOCK_ECO_THRESHOLD 2

// The APRS calling frequency, in the codeplug's units of 10Hz.
//
// Eco mode saves power by switching the receiver off for most of each cycle,
// which is fine for voice - a caller holds the button long enough to be caught -
// but not for APRS. A packet lasts well under a second, so most of them land
// while the receiver is off and are simply never heard. The give-away was that
// packets only ever appeared while the log screen was open: pressing keys there
// kept eco from engaging at all.
//
// Rather than a menu setting to forget about, eco simply stands down whenever
// the radio is tuned here. Everywhere else it behaves exactly as before, so
// battery life is untouched on normal channels. This covers the VFO as well,
// since currentChannelData points at the VFO's own channel in that mode.
#define APRS_CALLING_FREQUENCY  14480000 // 144.800MHz
static ecoPhase_t rxPowerSavingState = ECOPHASE_POWERSAVE_INACTIVE;
static ticksTimer_t ecoPhaseTimer = { 0, 0 };
volatile static int powerSavingLevel = 1;
static bool hrc6000IsPoweredOff = false;



bool rxPowerSavingIsRxOn(void)
{
	return (rxPowerSavingState != ECOPHASE_POWERSAVE_ACTIVE___RX_IS_OFF);
}

void rxPowerSavingSetLevel(int newLevel)
{
	if (powerSavingLevel != newLevel)
	{
		if (newLevel == 0)
		{
			rxPowerSavingSetState(ECOPHASE_POWERSAVE_INACTIVE);
		}

		powerSavingLevel = newLevel;
	}
}

int rxPowerSavingGetLevel(void)
{
	return powerSavingLevel;
}

static void resumeBeepAndC6000Tasks(void)
{
	vTaskResume(hrc6000Task.Handle);
	hrc6000Task.Running = true;
	hrc6000Task.AliveCount = TASK_FLAGGED_ALIVE;
	vTaskResume(beepTask.Handle);
	beepTask.Running = true;
	beepTask.AliveCount = TASK_FLAGGED_ALIVE;
	//HRC6000FlushMusic();							//G4EML   Needed after restarting the HRC6000 Task to prevent missing DMR audio;
}

static void suspendBeepAndC6000Tasks(void)
{
	vTaskSuspend(beepTask.Handle);
	beepTask.Running = false;
	vTaskSuspend(hrc6000Task.Handle);
	hrc6000Task.Running = false;
}

void rxPowerSavingSetState(ecoPhase_t newState)
{
	if (rxPowerSavingState != newState)
	{
		if (rxPowerSavingState == ECOPHASE_POWERSAVE_ACTIVE___RX_IS_OFF)
		{
			hrc6000IsPoweredOff = trxPowerUpDownRxAndC6000(true, hrc6000IsPoweredOff, false);
		}

		rxPowerSavingState = newState;

		// Avoid to instantly jump to power saving mode
		if (rxPowerSavingState == ECOPHASE_POWERSAVE_INACTIVE)
		{
			if (powerSavingLevel >= LOW_SPEED_CLOCK_ECO_THRESHOLD)
			{
				clockManagerSetRunMode(kAPP_PowerModeRun, CLOCK_MANAGER_SPEED_RUN);
				resumeBeepAndC6000Tasks();
			}

			ticksTimerStart(&ecoPhaseTimer, ((12 - (MIN(powerSavingLevel, 4) * 2)) * 1000));
		}
	}
}

void rxPowerSavingTick(uiEvent_t *ev, bool hasSignal)
{
	if ((settingsUsbMode != USB_MODE_HOTSPOT) || (rxPowerSavingState != ECOPHASE_POWERSAVE_INACTIVE))
	{
		if (USB_DeviceIsResetting() || isCompressingAMBE || hasSignal || trxTransmissionEnabled || trxIsTransmitting ||
				(menuSystemGetCurrentMenuNumber() == UI_TX_SCREEN) || (menuSystemGetCurrentMenuNumber() == UI_CPS) ||
				(uiDataGlobal.Scan.active && uiDataGlobal.Scan.scanType == SCAN_TYPE_NORMAL_STEP) || ev->hasEvent)
		{
			if (rxPowerSavingState != ECOPHASE_POWERSAVE_INACTIVE)
			{
				if (rxPowerSavingState == ECOPHASE_POWERSAVE_ACTIVE___RX_IS_OFF)
				{
					hrc6000IsPoweredOff = trxPowerUpDownRxAndC6000(true, hrc6000IsPoweredOff, false);// Power up AT1846S, C6000 and preamp
				}

				if (powerSavingLevel >= LOW_SPEED_CLOCK_ECO_THRESHOLD)
				{
					clockManagerSetRunMode(kAPP_PowerModeRun, CLOCK_MANAGER_SPEED_RUN);
					resumeBeepAndC6000Tasks();
				}

				rxPowerSavingState = ECOPHASE_POWERSAVE_INACTIVE;
			}

			// Postpone entering the ECOPHASE_POWERSAVE_INACTIVE
			ticksTimerStart(&ecoPhaseTimer, ((12 - (MIN(powerSavingLevel, 4) * 2)) * 1000));
		}
		else
		{
			if (ticksTimerHasExpired(&ecoPhaseTimer) &&
					((powerSavingLevel > 0) && (codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_NO_ECO) == 0)) &&
					((currentChannelData == NULL) || (currentChannelData->rxFreq != APRS_CALLING_FREQUENCY)) &&
					(melody_play == NULL) && (voicePromptsIsPlaying() == false)
#if defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701) || defined(PLATFORM_MD2017)
					&& (voxIsEnabled() == false)
#endif
			)
			{
				int rxDuration = (130 - (10 * powerSavingLevel));

				switch(rxPowerSavingState)
				{
					case ECOPHASE_POWERSAVE_INACTIVE:// wait before shutting down

						ticksTimerStart(&ecoPhaseTimer, ((12 - (MIN(powerSavingLevel, 4) * 2)) * 1000));
						rxPowerSavingState = ECOPHASE_POWERSAVE_WAIT_START;
						break;

					case ECOPHASE_POWERSAVE_WAIT_START:
						if ((powerSavingLevel >= LOW_SPEED_CLOCK_ECO_THRESHOLD) && (nonVolatileSettings.gps <= GPS_MODE_OFF))
						{
							suspendBeepAndC6000Tasks();
							clockManagerSetRunMode(kAPP_PowerModeRun, CLOCK_MANAGER_RUN_ECO_POWER_MODE);
						}

						rxPowerSavingState = ECOPHASE_POWERSAVE_ACTIVE___RX_IS_ON;
						// drop through

					case ECOPHASE_POWERSAVE_ACTIVE___RX_IS_ON:
						hrc6000IsPoweredOff = trxPowerUpDownRxAndC6000(false, (powerSavingLevel > 1), false);// Power down AT1846S, C6000 and preamp
						ticksTimerStart(&ecoPhaseTimer, (rxDuration * (1 << (powerSavingLevel - 1))));
						rxPowerSavingState = ECOPHASE_POWERSAVE_ACTIVE___RX_IS_OFF;
						break;

					case ECOPHASE_POWERSAVE_ACTIVE___RX_IS_OFF:
						//USB_DEBUG_printf("ECOPHASE_POWERSAVE_ACTIVE___RX_IS_ON\n");
						hrc6000IsPoweredOff = trxPowerUpDownRxAndC6000(true, hrc6000IsPoweredOff, false);// Power up AT1846S, C6000 and preamps
						ticksTimerStart(&ecoPhaseTimer, (rxDuration * 1));
						trxPostponeReadRSSIAndNoise(0); // Give it a bit of time, after powering up, before checking the RSSI and Noise values
						rxPowerSavingState = ECOPHASE_POWERSAVE_ACTIVE___RX_IS_ON;
						break;
				}
			}
		}
	}
}
