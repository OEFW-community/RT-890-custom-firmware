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

#include "helper/helper.h"
#include "ui/gfx.h"
#include "ui/helper.h"
#include "ui/noaa.h"
#include "misc.h"

static const uint32_t gNoaaFrequencyTable[3] = {
	16072500,// MCA MSI CH62 (Maritime Safety Information)
	16077500,// MCA MSI CH63 (Maritime Safety Information)
	16082500,// MCA MSI CH64 (Maritime Safety Information)
};

void UI_DrawNOAA(uint8_t Channel)
{
	Int2Ascii(gNoaaFrequencyTable[Channel], 8);
	gColorForeground = COLOR_FOREGROUND;
	UI_DrawString( 8, 64, gShortString, 3);
	UI_DrawString(32, 64, ".", 1);
	UI_DrawString(40, 64, gShortString + 3, 5);
	UI_DrawSmallString( 8, 32, "MSI CH ", 7);
	Int2Ascii(Channel + 62, 2);
	UI_DrawSmallString(50, 32, gShortString, 2);
	gColorForeground = COLOR_FOREGROUND;
	gScreenMode = SCREEN_NOAA;
}

