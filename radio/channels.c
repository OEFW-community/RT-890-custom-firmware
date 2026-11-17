/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include <string.h>
#include "app/css.h"
#include "app/fm.h"
#include "app/radio.h"
#include "driver/audio.h"
#include "driver/key.h"
#include "driver/serial-flash.h"
#include "driver/pins.h"
#include "helper/inputbox.h"
#include "misc.h"
#include "radio/channels.h"
#include "radio/settings.h"
#include "ui/helper.h"
#ifdef ENABLE_NOAA
	#include "ui/noaa.h"
#endif
#include "ui/vfo.h"

static const ChannelInfo_t VfoTemplate[2] = {
	{
		.RX = { .Frequency = 43002500, .Code = 0x000, .CodeType = CODE_TYPE_OFF, },
		.TX = { .Frequency = 43002500, .Code = 0x000, .CodeType = CODE_TYPE_OFF, },
		.Golay = 0x000000,

		.Unknown0 = 0,
		.bIs24Bit = 0,
		.bMuteEnabled = 0,
		.Encrypt = 0,

		.Available = 0,
		.gModulationType = 0,
		.BCL = BUSY_LOCK_OFF,
		.ScanAdd = 1,
		.bIsLowPower = 0,
		.bIsNarrow = 0,

		._0x11 = 0x11,
		.Scramble = 0x00,
		.IsInscanList = 0xFF,
		._0x14 = 0xFF,
		._0x15 = 0xFF,
		.Name = "----------",
	},
	{

		.RX = { .Frequency = 43502500, .Code = 0x000, .CodeType = CODE_TYPE_OFF, },
		.TX = { .Frequency = 43502500, .Code = 0x000, .CodeType = CODE_TYPE_OFF, },
		.Golay = 0x00,

		.Unknown0 = 0,
		.bIs24Bit = 0,
		.bMuteEnabled = 0,
		.Encrypt = 0,

		.Available = 0,
		.gModulationType = 0,
		.BCL = BUSY_LOCK_OFF,
		.ScanAdd = 1,
		.bIsLowPower = 0,
		.bIsNarrow = 0,

		._0x11 = 0x11,
		.Scramble = 0x00,
		.IsInscanList = 0xFF,
		._0x14 = 0xFF,
		._0x15 = 0xFF,
		.Name = "----------",
	},
};

#ifdef ENABLE_NOAA
static const ChannelInfo_t gNoaaDefaultChannels[3] = {
	{
		.RX = { .Frequency = 16072500, .Code = 0x000, .CodeType = CODE_TYPE_OFF, },//MCA MSI CH62
		.TX = { .Frequency = 16072500, .Code = 0x000, .CodeType = CODE_TYPE_OFF, },
		.Golay = 0x000000,

		.Unknown0 = 0,
		.bIs24Bit = 0,
		.bMuteEnabled = 0,
		.Encrypt = 0,

		.Available = 0,
		.gModulationType = 0,
		.BCL = BUSY_LOCK_OFF,
		.ScanAdd = 1,
		.bIsLowPower = 0,
		.bIsNarrow = 0,

		._0x11 = 0x11,
		.Scramble = 0x00,
		.IsInscanList = 0xFF,
		._0x14 = 0xFF,
		._0x15 = 0xFF,
		.Name = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	},
	{
		.RX = { .Frequency = 16077500, .Code = 0x000, .CodeType = CODE_TYPE_OFF, },//MCA MSI CH63
		.TX = { .Frequency = 16077500, .Code = 0x000, .CodeType = CODE_TYPE_OFF, },
		.Golay = 0x000000,

		.Unknown0 = 0,
		.bIs24Bit = 0,
		.bMuteEnabled = 0,
		.Encrypt = 0,

		.Available = 0,
		.gModulationType = 0,
		.BCL = BUSY_LOCK_OFF,
		.ScanAdd = 1,
		.bIsLowPower = 0,
		.bIsNarrow = 0,

		._0x11 = 0x11,
		.Scramble = 0x00,
		.IsInscanList = 0xFF,
		._0x14 = 0xFF,
		._0x15 = 0xFF,
		.Name = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	},
	{
		.RX = { .Frequency = 16082500, .Code = 0x000, .CodeType = CODE_TYPE_OFF, },//MCA MSI CH64
		.TX = { .Frequency = 16082500, .Code = 0x000, .CodeType = CODE_TYPE_OFF, },
		.Golay = 0x000000,

		.Unknown0 = 0,
		.bIs24Bit = 0,
		.bMuteEnabled = 0,
		.Encrypt = 0,

		.Available = 0,
		.gModulationType = 0,
		.BCL = BUSY_LOCK_OFF,
		.ScanAdd = 1,
		.bIsLowPower = 0,
		.bIsNarrow = 0,

		._0x11 = 0x11,
		.Scramble = 0x00,
		.IsInscanList = 0xFF,
		._0x14 = 0xFF,
		._0x15 = 0xFF,
		.Name = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	},	
};
#endif

uint16_t gFreeChannelsCount;

#ifdef ENABLE_833_RETUNE
// 8.33 kHz channel spacing uses x.x000, x.x250, x.x500, x.x750 multiples.
// Because 1/3 is periodic in decimal, every 3rd step can land 1 unit off;
// this function snaps the frequency back to the nearest valid boundary.
// Works for both 8-digit (VFO) and 7-digit (spectrum) frequency representations.
void Snap833Step(uint32_t *frequency)
{
	uint32_t rem = *frequency % 10;
	if (rem == 1)
		*frequency -= 1;      // stepping down: x1 -> x0
	else if (rem == 9)
		*frequency += 1;      // stepping up:   x9 -> x0
	else if (rem == 7)
		*frequency -= 1;      // stepping down: x7 -> x6
	else if (rem == 4)
		*frequency -= 1;      // stepping down: x4 -> x3
}
#endif

bool CHANNELS_NextChannelMr(uint8_t Key, bool OnlyFromScanlist) {
	uint16_t startChannel = gSettings.VfoChNo[gSettings.CurrentVfo];
	do {
		if (Key == KEY_UP) {
			gSettings.VfoChNo[gSettings.CurrentVfo] = CHANNELS_GetChannelUp(gSettings.VfoChNo[gSettings.CurrentVfo], gSettings.CurrentVfo);
		} else {
			gSettings.VfoChNo[gSettings.CurrentVfo] = CHANNELS_GetChannelDown(gSettings.VfoChNo[gSettings.CurrentVfo], gSettings.CurrentVfo);
		}
		if (gSettings.VfoChNo[gSettings.CurrentVfo] == startChannel)
			return false;	// empty list
	} while (OnlyFromScanlist && !((gVfoState[gSettings.CurrentVfo].IsInscanList >> gExtendedSettings.CurrentScanList) & 1));
	RADIO_Tune(gSettings.CurrentVfo);
	UI_DrawVfo(gSettings.CurrentVfo);
	return true;
}

void CHANNELS_NextChannelVfo(uint8_t Key)
{
	ChannelInfo_t *pInfo = &gVfoState[gSettings.CurrentVfo];

	if (!gFrequencyReverse) {
		if (Key == KEY_UP) {
			pInfo->RX.Frequency += gFrequencyStep;
			if (gSettings.bFLock) {
				if (pInfo->RX.Frequency > 13600000 && pInfo->RX.Frequency < 14400000) {
					pInfo->RX.Frequency = 14400000;
				}
				if (pInfo->RX.Frequency > 14600000 && pInfo->RX.Frequency < 43000000) {
					pInfo->RX.Frequency = 43000000;
				}
				if (pInfo->RX.Frequency > 44000000) {
					pInfo->RX.Frequency = 10800000;
				}
			} else if (pInfo->RX.Frequency > gExtendedSettings.ScanRangeFinish) {
				// Wrap to Scan Start instead of the old clamp-and-stall at
				// the hardware ceiling -- see settings.h ScanRangeFinish.
				pInfo->RX.Frequency = gExtendedSettings.ScanRangeStart;
			}
		} else {
			pInfo->RX.Frequency -= gFrequencyStep;
			if (gSettings.bFLock) {
				if (pInfo->RX.Frequency < 10800000) {
					pInfo->RX.Frequency = 44000000;
				}
				if (pInfo->RX.Frequency > 13600000 && pInfo->RX.Frequency < 14400000) {
					pInfo->RX.Frequency = 13600000;
				}
				if (pInfo->RX.Frequency > 14600000 && pInfo->RX.Frequency < 43000000) {
					pInfo->RX.Frequency = 14600000;
				}
			} else if (pInfo->RX.Frequency < gExtendedSettings.ScanRangeStart) {
				// Wrap to Scan Finish instead of the old clamp-and-stall at
				// the hardware floor -- see settings.h ScanRangeStart.
				pInfo->RX.Frequency = gExtendedSettings.ScanRangeFinish;
			}
		}

		#ifdef ENABLE_833_RETUNE
		if (gFrequencyStep == 833) {
			uint32_t freq = pInfo->RX.Frequency;
			Snap833Step(&freq);
			pInfo->RX.Frequency = freq;
		}
		#endif

		pInfo->TX.Frequency = pInfo->RX.Frequency;
		gVfoInfo[gSettings.CurrentVfo].Frequency = pInfo->RX.Frequency;
	} else {
		if (Key == KEY_UP) {
			pInfo->TX.Frequency += gFrequencyStep;
			if (gSettings.bFLock) {
				if (pInfo->TX.Frequency > 13600000 && pInfo->TX.Frequency < 14400000) {
					pInfo->TX.Frequency = 14400000;
				}
				if (pInfo->TX.Frequency > 14600000 && pInfo->TX.Frequency < 43000000) {
					pInfo->TX.Frequency = 43000000;
				}
				if (pInfo->TX.Frequency > 44000000) {
					pInfo->TX.Frequency = 10800000;
				}
			} else if (pInfo->TX.Frequency > gExtendedSettings.ScanRangeFinish) {
				pInfo->TX.Frequency = gExtendedSettings.ScanRangeStart;
			}
		} else {
			pInfo->TX.Frequency -= gFrequencyStep;
			if (gSettings.bFLock) {
				if (pInfo->TX.Frequency < 10800000) {
					pInfo->TX.Frequency = 44000000;
				}
				if (pInfo->TX.Frequency > 13600000 && pInfo->TX.Frequency < 14400000) {
					pInfo->TX.Frequency = 13600000;
				}
				if (pInfo->TX.Frequency > 14600000 && pInfo->TX.Frequency < 43000000) {
					pInfo->TX.Frequency = 14600000;
				}
			} else if (pInfo->TX.Frequency < gExtendedSettings.ScanRangeStart) {
				pInfo->TX.Frequency = gExtendedSettings.ScanRangeFinish;
			}
		}
		gVfoInfo[gSettings.CurrentVfo].Frequency = pInfo->TX.Frequency;
	}

	UI_DrawVfo(gSettings.CurrentVfo);
}

#ifdef ENABLE_NOAA
void CHANNELS_NextNOAA(uint8_t Key)
{
	if (gRadioMode == RADIO_MODE_RX) {
		RADIO_EndAudio();
	}
	if (Key == KEY_UP) {
		gNOAA_ChannelNow = (gNOAA_ChannelNow + 1) % 3;
	} else {
		gNOAA_ChannelNow = (gNOAA_ChannelNow + 2) % 3;
	}
	UI_DrawNOAA(gNOAA_ChannelNow);
	CHANNELS_SetNoaaChannel(gNOAA_ChannelNow);
	RADIO_Tune(2);
}
#endif

void CHANNELS_NextFM(uint8_t Key)
{
	if (Key == KEY_UP) {
		gSettings.FmFrequency++;
		if (gSettings.FmFrequency > 1080) {
			gSettings.FmFrequency = 640;
		}
	} else {
		gSettings.FmFrequency--;
		if (gSettings.FmFrequency < 640) {
			gSettings.FmFrequency = 1080;
		}
	}
	SETTINGS_SaveGlobals();
	UI_DrawFMFrequency(gSettings.FmFrequency);
	FM_Play();
}

void CHANNELS_UpdateChannel(void)
{
	uint16_t Channel = 0;
	uint8_t i;

	for (i = 0; i < 3; i++) {
		Channel = (Channel * 10) + gInputBox[i];
	}
	gInputBoxWriteIndex = 0;
	INPUTBOX_Pad(0, 10);
	if (Channel != 0) {
		Channel--;
		if (!CHANNELS_LoadChannel(Channel, gSettings.CurrentVfo)) {
			if (gSettings.VfoChNo[gSettings.CurrentVfo] != Channel) {
				gSettings.VfoChNo[gSettings.CurrentVfo] = Channel;
				SETTINGS_SaveGlobals();
				RADIO_Tune(gSettings.CurrentVfo);
				UI_DrawVfo(gSettings.CurrentVfo);
			}
			return;
		}
	}
	CHANNELS_LoadChannel(gSettings.VfoChNo[gSettings.CurrentVfo], gSettings.CurrentVfo);
	RADIO_Tune(gSettings.CurrentVfo);
	UI_DrawVfo(gSettings.CurrentVfo);
}

void CHANNELS_UpdateVFO(void)
{
	uint32_t Frequency = 0;
	uint8_t i;

	RADIO_CancelMode();
	for (i = 0; i < 8; i++) {
		Frequency = (Frequency * 10) + gInputBox[i];
	}

	gInputBoxWriteIndex = 0;
	INPUTBOX_Pad(0, 10);

	CHANNELS_UpdateVFOFreq(Frequency);
}

void CHANNELS_UpdateVFOFreq(uint32_t Frequency)
{
	if (
		(!gSettings.bFLock && (Frequency >= 1000000  && Frequency <= 130000000)) ||
		(gSettings.bFLock && (
				(Frequency >= 14400000 && Frequency <= 14600000) ||
				(Frequency >= 43000000 && Frequency <= 44000000) ||
				(Frequency >= 10800000 && Frequency <= 13600000)))
		) {
		if (!gFrequencyReverse) {
			#ifdef ENABLE_833_RETUNE
			if (gFrequencyStep == 833)
				Snap833Step(&Frequency);
			#endif
			gVfoState[gSettings.CurrentVfo].RX.Frequency = Frequency;
		}
		gVfoState[gSettings.CurrentVfo].TX.Frequency = Frequency;
		// A manually-entered frequency (keypad direct entry or a spectrum-analyzer
		// tap - the only two callers of this function) is a deliberate "go here"
		// action, not a retune of the channel that was already loaded. Without
		// this, RX/TX CodeType and Code are left untouched, so the new frequency
		// silently inherits whatever CTCSS/DCS tone was active on the previously
		// loaded channel/VFO - which is what was showing up as unexplained DCS
		// codes on channels the user never set a tone on.
		gVfoState[gSettings.CurrentVfo].RX.CodeType = CODE_TYPE_OFF;
		gVfoState[gSettings.CurrentVfo].RX.Code = 0;
		gVfoState[gSettings.CurrentVfo].TX.CodeType = CODE_TYPE_OFF;
		gVfoState[gSettings.CurrentVfo].TX.Code = 0;
		// Same reasoning as the tone reset above, for bank membership.
		// VfoTemplate seeds a fresh VFO with IsInscanList = 0xFF (every
		// bank), and nothing here ever cleared it - so a VFO slot that's
		// never had its banks explicitly touched carries that "every
		// bank" default forward indefinitely, and any channel saved
		// from it inherits membership in all 8 banks. Reset to none;
		// the user opts a new channel into whichever bank(s) they
		// actually want it in.
		gVfoState[gSettings.CurrentVfo].IsInscanList = 0x00;
		CHANNELS_SaveChannel(gSettings.CurrentVfo ? 1000 : 999, &gVfoState[gSettings.CurrentVfo]);
		CHANNELS_LoadChannel(gSettings.CurrentVfo ? 1000 : 999, gSettings.CurrentVfo);
		RADIO_Tune(gSettings.CurrentVfo);
	}
	UI_DrawVfo(gSettings.CurrentVfo);
}

bool CHANNELS_LoadChannel(uint16_t ChNo, uint8_t Vfo)
{
	uint32_t Frequency;

	SFLASH_Read(&gVfoState[Vfo], 0x3C2000 + (ChNo * sizeof(ChannelInfo_t)), sizeof(ChannelInfo_t));
	if (gSettings.bFLock) {
		Frequency = gVfoState[Vfo].RX.Frequency;
		if (Frequency > 44000000) {
			return true;
		}
		if (Frequency > 14600000 && Frequency < 43000000) {
			return true;
		}
		if (Frequency > 13600000 && Frequency < 14400000) {
			return true;
		}
		if (Frequency < 10800000) {
			return true;
		}
		Frequency = gVfoState[Vfo].TX.Frequency;
		if (Frequency > 44000000) {
			return true;
		}
		if (Frequency > 14600000 && Frequency < 43000000) {
			return true;
		}
		if (Frequency > 13600000 && Frequency < 14400000) {
			return true;
		}
		if (Frequency < 10800000) {
			return true;
		}
	}

	return gVfoState[Vfo].Available;
}

void CHANNELS_CheckFreeChannels(void)
{
	uint16_t i;

	gFreeChannelsCount = 0;
	for (i = 0; i < 999; i++) {
		if (!CHANNELS_LoadChannel(i, 0)) {
			gFreeChannelsCount++;
		}
	}
	if (gFreeChannelsCount == 0) {
		gSettings.WorkMode = 0;
		SETTINGS_SaveGlobals();
		CHANNELS_LoadVfoMode();
	}
}

void CHANNELS_LoadVfoMode(void)
{
	ChannelInfo_t VfoState[2];

	memcpy(&VfoState[0], &VfoTemplate, sizeof(VfoState));

	while (CHANNELS_LoadChannel(999, 0)) {
		SFLASH_Update(&VfoState[0], 0x3C9CE0, sizeof(VfoState[0]));
	}

	while (CHANNELS_LoadChannel(1000, 1)) {
		SFLASH_Update(&VfoState[1], 0x3C9D00, sizeof(VfoState[1]));
	}

	if (gSettings.CurrentVfo) {
		CHANNELS_LoadChannel(1000, 1);
	} else {
		CHANNELS_LoadChannel(999, 0);
	}
	if (gScreenMode == SCREEN_MAIN && gSettings.DtmfState != DTMF_STATE_KILLED) {
		AUDIO_PlaySampleOptional(0);
	}
}

void CHANNELS_LoadWorkMode(void)
{
	if (CHANNELS_LoadChannel(gSettings.VfoChNo[0], 0)) {
		gSettings.VfoChNo[0] = CHANNELS_GetChannelUp(gSettings.VfoChNo[0], 0);
		SETTINGS_SaveGlobals();
	}
	if (CHANNELS_LoadChannel(gSettings.VfoChNo[1], 1)) {
		gSettings.VfoChNo[1] = CHANNELS_GetChannelUp(gSettings.VfoChNo[1], 1);
		SETTINGS_SaveGlobals();
	}
	CHANNELS_LoadChannel(gSettings.VfoChNo[gSettings.CurrentVfo], gSettings.CurrentVfo);
	if (gScreenMode == SCREEN_MAIN && gSettings.DtmfState != DTMF_STATE_KILLED) {
		AUDIO_PlayChannelNumber();
	}
}

uint16_t CHANNELS_GetChannelUp(uint16_t Channel, uint8_t Vfo)
{
	Channel++;
	while (1) {
		Channel %= 999;
		if (!CHANNELS_LoadChannel(Channel, Vfo)) {
			break;
		}
		Channel++;
	}

	return Channel;
}

uint16_t CHANNELS_GetChannelDown(uint16_t Channel, uint8_t Vfo)
{
	Channel += 998;
	while (1) {
		Channel %= 999;
		if (!CHANNELS_LoadChannel(Channel, Vfo)) {
			break;
		}
		Channel += 998;
	}

	return Channel;
}

void CHANNELS_SaveChannel(uint16_t Channel, const ChannelInfo_t *pChannel)
{
	SFLASH_Update(pChannel, 0x3C2000 + (Channel * sizeof(*pChannel)), sizeof(*pChannel));
}

#ifdef ENABLE_NOAA
void CHANNELS_SetNoaaChannel(uint8_t Channel)
{
	gVfoState[2] = gNoaaDefaultChannels[Channel];
}
#endif

void CHANNELS_SaveVfo(void)
{
	if (gSettings.WorkMode) {
		CHANNELS_SaveChannel(gSettings.VfoChNo[gSettings.CurrentVfo], &gVfoState[gSettings.CurrentVfo]);
	} else {
		CHANNELS_SaveChannel(gSettings.CurrentVfo ? 1000 : 999, &gVfoState[gSettings.CurrentVfo]);
	}

	RADIO_Tune(gSettings.CurrentVfo);
}
