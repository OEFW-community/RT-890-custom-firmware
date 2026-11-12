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
#include "driver/mb_flash.h"
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

static void DrawResult(uint8_t Y, const char *pLabel, bool bOk)
{
	UI_DrawString(0, Y, pLabel, 4);
	UI_DrawString(40, Y, bOk ? "OK  " : "FAIL", 4);
}

static void WaitKey(KEY_t Key)
{
	while (KEY_GetButton() != Key) {
	}
	while (KEY_GetButton() != KEY_NONE) {
	}
}

static void ClearScreen(void)
{
	DISPLAY_Fill(0, 159, 0, 96, COLOR_BACKGROUND);
}

// Page 2 (milestone M1a): non destructive test of the restore building blocks
// running from RAM, and backup of the running firmware to SPI slot 0.
static void DrawProbePage(void)
{
	MB_Probe_t Probe;
	uint8_t Spi[sizeof(Probe.Spi)];
	bool bLoaded;
	uint8_t i;

	ClearScreen();
	UI_DrawString(0, 80, "Testing...", 10);

	// Calibration page: known, non erased content.
	SFLASH_Read(Spi, 0x3BF000, sizeof(Spi));
	bLoaded = MB_RunProbe(&Probe, 0x3BF000);
	for (i = 0; i < sizeof(Spi) && Probe.Spi[i] == Spi[i]; i++) {
	}

	ClearScreen();
	if (!bLoaded) {
		DrawResult(80, "LOAD", false);
	} else {
		// FAP: internal flash readable from RAM-resident code.
		DrawResult(80, "RRD ",
			Probe.IntFirst == *(const uint32_t *)MB_INT_APP_BASE &&
			Probe.IntLast == *(const uint32_t *)(MB_INT_APP_BASE + MB_INT_APP_SIZE - 4U));
		// Flash controller unlocked (oplk = 0), then locked again (oplk = 1).
		DrawResult(64, "UNLK", !(Probe.CtrlUnlocked & 0x80U) && (Probe.CtrlLocked & 0x80U));
		// SPI flash read by the RAM-resident bit-banged driver.
		DrawResult(48, "SPI ", i == sizeof(Spi));
	}
	DrawHexLine(32, "INT", MB_InternalCrc32(), 8);
	DrawHexLine(16, "SL0", MB_SpiCrc32(MB_SLOT_BASE(0) + MB_SLOT_IMG_OFFSET, MB_INT_APP_SIZE), 8);
	UI_DrawString(0, 0, "1:Bkp 2:Rst EXIT", 16);
}

// Milestone M1b: reflash the internal flash from slot 0, after confirmation.
static void RestorePage(void)
{
	uint8_t Error;
	KEY_t Key;

	ClearScreen();
	UI_DrawString(0, 80, "Restore slot 0?", 15);
	UI_DrawString(0, 48, "MENU: yes", 9);
	UI_DrawString(0, 32, "EXIT: no", 8);
	do {
		Key = KEY_GetButton();
	} while (Key != KEY_MENU && Key != KEY_EXIT);
	WaitKey(KEY_NONE);
	if (Key != KEY_MENU) {
		return;
	}

	ClearScreen();
	UI_DrawString(0, 80, "Restoring...", 12);
	UI_DrawString(0, 64, "Do not power off", 16);
	// Only returns if the image was refused: nothing has been modified.
	Error = MB_RestoreSlot0();
	ClearScreen();
	UI_DrawString(0, 80, "Refused, error", 14);
	gString[0] = '0' + Error;
	UI_DrawString(120, 80, gString, 1);
	UI_DrawString(0, 0, "EXIT: next", 10);
	WaitKey(KEY_EXIT);
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
	ClearScreen();
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
	UI_DrawString(0, 0, "EXIT: next", 10);
	WaitKey(KEY_EXIT);

	DrawProbePage();
	while (1) {
		KEY_t Key = KEY_GetButton();

		if (Key == KEY_EXIT) {
			break;
		}
		if (Key == KEY_1) {
			WaitKey(KEY_NONE);
			ClearScreen();
			UI_DrawString(0, 80, "Backup slot 0...", 16);
			DrawResult(64, "BKP ", MB_BackupInternalToSlot0());
			UI_DrawString(0, 0, "EXIT: next", 10);
			WaitKey(KEY_EXIT);
			DrawProbePage();
		}
		if (Key == KEY_2) {
			WaitKey(KEY_NONE);
			RestorePage();
			DrawProbePage();
		}
	}
	WaitKey(KEY_NONE);
	KEY_KeyCounter = 0;

	ClearScreen();
}

