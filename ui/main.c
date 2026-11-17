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

#include "app/radio.h"
#include "driver/battery.h"
#include "helper/dtmf.h"
#include "helper/inputbox.h"
#include "misc.h"
#include "radio/settings.h"
#include "ui/gfx.h"
#include "ui/helper.h"
#include "ui/main.h"
#include "ui/vfo.h"

void DrawStatusBar(void)
{
	DISPLAY_Fill(0, 159, 0, 96, COLOR_BACKGROUND);
	DISPLAY_DrawRectangle0(0, 82, 160, 1, gColorForeground);
	//DISPLAY_DrawRectangle0(0, 41, 160, 1, gColorForeground);
	
	
	if (gSettings.DtmfState == DTMF_STATE_STUNNED) {
		UI_DrawStatusIcon(2, ICON_LOCK, true, COLOR_FOREGROUND);
	} else {
		UI_DrawStatusIcon(2, ICON_LOCK, gSettings.Lock, COLOR_FOREGROUND);
	}
// frequency step is common to all VFOs
	#define SPECTRUM_STEPS_COUNT 15
	static const char StepStrings[SPECTRUM_STEPS_COUNT][5] = {
		"10 HZ",
		"250HZ",
		"1.25K",
		"2.50K",
		"   5K",
		"6.25K",
		"8.33K",
		"  10K",
		"12.5K",
		"  20K",
		"  25K",
		"  50K",
		" 100K",
		" 500K",
		"1 MHZ"
		//"5.00M"
	};
	gColorForeground = COLOR_FOREGROUND;
	UI_DrawSmallString(80, 86, StepStrings[gSettings.FrequencyStep], 5);

	// squelch is common to all VFOs
	//UI_DrawSmallCharacter(48 + 5 + 1, 86, 'S');
	UI_DrawSmallCharacter(60, 86, '0' + gSettings.Squelch);
	UI_DrawSmallString(54, 86, "Q", 1);
	
	if (gSettings.DualStandby == true) {
		gColorForeground = COLOR_FOREGROUND;
		UI_DrawSmallString(114, 86, "D", 1); 
	} else {
		gColorForeground = COLOR_FOREGROUND;
		UI_DrawSmallString(114, 86, " ", 1);
	}

	if (gSettings.Vox == true) {
		gColorForeground = COLOR_FOREGROUND;
		UI_DrawSmallString(70, 86, "V", 1); 
	}	else {
		gColorForeground = COLOR_FOREGROUND;
		UI_DrawSmallString(70, 86, " ", 1);
	}

	UI_DrawRoger();
	UI_DrawRepeaterMode();
	UI_DrawStatusIcon(139, ICON_BATTERY, true, COLOR_FOREGROUND);
	UI_DrawBattery();
}

void UI_DrawMain(bool bSkipStatus)
{
	if (bSkipStatus) {
		DISPLAY_Fill(0, 159, 0, 81, COLOR_BACKGROUND);
		DISPLAY_DrawRectangle0(0, 82, 160, 1, gColorForeground);
	} else {
		DrawStatusBar();
	}

	if (gSettings.DualDisplay == 0 && (gRadioMode != RADIO_MODE_RX || gSettings.CurrentVfo == gCurrentVfo)) {
		UI_DrawVfo(gSettings.CurrentVfo);
		UI_DrawVoltage(!gSettings.CurrentVfo);
	} else {
		UI_DrawVfo(!gCurrentVfo);
		UI_DrawVfo(gCurrentVfo);
	}

	if (gInputBoxWriteIndex != 0) {
		if (gSettings.WorkMode) {
			UI_DrawDigits(gInputBox, gSettings.CurrentVfo);
		} else {
			UI_DrawFrequencyEx(gInputBox, gSettings.CurrentVfo, gFrequencyReverse);
		}
	}
	if (gRadioMode != RADIO_MODE_RX && gRadioMode != RADIO_MODE_TX) {
		UI_DrawMainBitmap(true, gSettings.CurrentVfo);
	}
	if (gRadioMode == RADIO_MODE_RX && gDTMF_WriteIndex != 0) {
		gDataDisplay = false;
		UI_DrawDTMFString();
		if (gDTMF_Settings.Display) {
			gDataDisplay = true;
		}
	}
}

// Correct order
void UI_DrawRepeaterMode(void)
{
	switch (gSettings.RepeaterMode) {
	case 1:
		gColorForeground = COLOR_FOREGROUND;
		UI_DrawSmallString(122, 86, "T", 1);
		break;
	case 2:
		gColorForeground = COLOR_FOREGROUND;
		UI_DrawSmallString(122, 86, "R", 1);
		break;
	default:
		gColorForeground = COLOR_FOREGROUND;
		UI_DrawSmallString(122, 86, " ", 1);
		break;
	}
}

void UI_DrawBattery(void)
{
	uint8_t i;
	uint16_t Color;

	for (i = 15; i > 0; i--) {
		if (gBatteryVoltage > gCalibration.BatteryCalibration[i - 1]) {
			break;
		}
	}
	if (i < 6) {
		Color = COLOR_FOREGROUND;
	} else if (i < 11) {
		Color = COLOR_FOREGROUND;
	} else {
		Color = COLOR_FOREGROUND;
	}
	DISPLAY_DrawRectangle0(142, 86, 15 - i, 8, gColorBackground);
	DISPLAY_DrawRectangle0(157 - i, 86, i, 8, Color);
}

