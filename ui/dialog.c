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

#include "misc.h"
#include "radio/scheduler.h"
#include "ui/dialog.h"
#include "ui/gfx.h"
#include "ui/helper.h"

void UI_DrawDialogText(UI_DialogText_t Text, bool bSet)
{
	UI_DrawDialog();
	gColorForeground = COLOR_FOREGROUND;

	switch (Text) {
	case DIALOG_AM_FIX:
		if (bSet) {
			UI_DrawString(10, 38, "AM Fix On ", 10);
		} else {
			UI_DrawString(10, 38, "AM Fix Off", 10);
		}
		break;

	case DIALOG_TX_PRIORITY:
		if (bSet) {
			UI_DrawString(10, 38, "TX Pri Busy", 11);
		} else {
			UI_DrawString(10, 38, "TX Pri Edit", 11);
		}
		break;

	case DIALOG_VOX:
		if (bSet) {
			UI_DrawString(10, 38, "VOX On ", 7);
		} else {
			UI_DrawString(10, 38, "VOX Off", 7);
		}
		break;
		
	case DIALOG_TX_POWER:
		if (bSet) {
			UI_DrawString(10, 38, "Low TX Power ", 13);
		} else {
			UI_DrawString(10, 38, "High TX Power", 13);
		}
		break;

	case DIALOG_DUAL_STANDBY:
		if (bSet) {
			UI_DrawString(10, 38, "Dual Watch On ", 14);
		} else {
			UI_DrawString(10, 38, "Dual Watch Off", 14);
		}
		break;
	
	case DIALOG_KEY_BEEP:
		if (bSet) {
			UI_DrawString(10, 38, "Key Beep On ", 12);
		} else {
			UI_DrawString(10, 38, "Key Beep Off", 12);
		}
		break;

	case DIALOG_TOGGLE_SCANLIST:
		if (bSet) {
			UI_DrawString(10, 38, "Add Ch To Bank  ", 16);
		} else {
			UI_DrawString(10, 38, "Rem Ch From Bank", 16);
		}
		break;

	/*case DIALOG_PLEASE_CHARGE:
		UI_DrawString(10, 38, "Please Charge!", 14);
		break;*/

	case DIALOG_NO_CH_AVAILABLE:
		UI_DrawString(10, 38, "No Ch Saved", 11);
		break;

	case DIALOG_PROHIBIT_TX:
		if (bSet) {
			UI_DrawString(10, 38, "Prohibit TX On ", 15);
		} else {
			UI_DrawString(10, 38, "Prohibit TX Off", 15);
		}
		break;
	}

	gRedrawScreen = true;
	VOX_Timer = 1200;
}