/* Copyright 2026 reppad
 * https://github.com/reppad
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

#include <at32f421.h>
#include "app/mbdiag.h"
#include "driver/key.h"
#include "driver/serial-flash.h"
#include "helper/helper.h"
#include "misc.h"
#include "ui/gfx.h"
#include "ui/helper.h"

// AT32F421 flash size register (in KB), in the system memory area.
#define MBDIAG_FLASH_SIZE_REG	(*(const volatile uint16_t *)0x1FFFF7E0U)

static const char Hex[] = "0123456789ABCDEF";

// Draws "<Label> <Digits hex digits of Value>" on one line.
static void DrawHexLine(uint8_t Y, const char *pLabel, uint32_t Value, uint8_t Digits)
{
	uint8_t i;

	gString[0] = pLabel[0];
	gString[1] = pLabel[1];
	gString[2] = pLabel[2];
	gString[3] = ' ';
	for (i = 0; i < Digits; i++) {
		gString[4 + i] = Hex[(Value >> ((Digits - 1 - i) * 4)) & 0xFU];
	}
	UI_DrawString(0, Y, gString, 4 + Digits);
}

void MBDIAG_CheckBoot(void)
{
	uint32_t JedecId;
	uint8_t Capacity;

	if (KEY_GetButton() != KEY_MENU) {
		return;
	}

	JedecId = SFLASH_ReadJedecId();

	SCREEN_TurnOn();
	DISPLAY_Fill(0, 159, 0, 96, COLOR_BACKGROUND);
	gColorForeground = COLOR_FOREGROUND;

	// SPI flash: manufacturer, type, capacity code and decoded size in KB.
	DrawHexLine(80, "SPI", JedecId, 6);
	Capacity = JedecId & 0xFFU;
	if (Capacity >= 0x11U && Capacity <= 0x19U) {
		Int2Ascii(1UL << (Capacity - 10U), 5);
		UI_DrawString(96, 80, gShortString, 5);
		UI_DrawString(136, 80, "K", 1);
	}

	// Internal flash size (KB) and user system data (FAP is bit 1).
	DrawHexLine(64, "FSZ", MBDIAG_FLASH_SIZE_REG, 4);
	DrawHexLine(48, "USD", FLASH->usd, 8);
	// Erase/program protection: a 0 bit means a protected sector group.
	DrawHexLine(32, "EPP", FLASH->epps, 8);
	// Security library state (slib_enf, em_slib_enf, btm_ap_enf).
	DrawHexLine(16, "SLB", FLASH->slib_sts0, 8);
	UI_DrawString(0, 0, "EXIT to continue", 16);

	while (KEY_GetButton() != KEY_EXIT) {
	}
	while (KEY_GetButton() != KEY_NONE) {
	}
	KEY_KeyCounter = 0;

	DISPLAY_Fill(0, 159, 0, 96, COLOR_BACKGROUND);
}

