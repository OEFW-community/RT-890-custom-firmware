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

#include "app/mbdiag.h"
#include "driver/key.h"
#include "driver/mb_flash.h"
#include "helper/helper.h"
#include "misc.h"
#include "ui/gfx.h"
#include "ui/helper.h"

static const char Hex[] = "0123456789ABCDEF";

static void ToHex(char *pString, uint32_t Value)
{
	uint8_t i;

	for (i = 0; i < 8; i++) {
		pString[i] = Hex[(Value >> ((7 - i) * 4)) & 0xFU];
	}
}

static void WaitKey(KEY_t Key)
{
	while (KEY_GetButton() != Key) {
	}
	while (KEY_GetButton() != KEY_NONE) {
	}
}

// Text is drawn upwards from its Y coordinate (rows Y-16 to Y-1): the six
// visible text lines are Y = 96, 80, 64, 48, 32 and 16.
static void ClearScreen(void)
{
	DISPLAY_Fill(0, 159, 0, 96, COLOR_BACKGROUND);
}

static void DrawMessage(const char *pString, uint8_t Error)
{
	uint8_t Size = 0;

	while (pString[Size]) {
		gString[Size] = pString[Size];
		Size++;
	}
	if (Error) {
		gString[Size++] = ' ';
		gString[Size++] = '0' + Error / 10;
		gString[Size++] = '0' + Error % 10;
	}
	ClearScreen();
	UI_DrawString(0, 80, gString, Size);
	UI_DrawString(0, 16, "EXIT: back", 10);
	WaitKey(KEY_EXIT);
}

// One line per slot: "<n> <name, 8 chars> <claimed CRC>", or the header
// error code. The image CRC itself is checked before any restore.
static void DrawSlots(void)
{
	MB_SlotHeader_t Header;
	MB_State_t State;
	uint8_t Slot;
	uint8_t Error;
	uint8_t i;

	ClearScreen();
	for (Slot = 0; Slot < MB_SLOT_COUNT; Slot++) {
		for (i = 0; i < 19; i++) {
			gString[i] = ' ';
		}
		gString[0] = '0' + Slot;
		Error = MB_ReadSlotHeader(Slot, &Header);
		if (Error == MB_OK) {
			for (i = 0; i < 8 && Header.Name[i] >= ' ' && Header.Name[i] <= '~'; i++) {
				gString[2 + i] = Header.Name[i];
			}
			ToHex(gString + 11, Header.ImageCrc32);
		} else {
			gString[2] = '-';
			gString[3] = '0' + Error;
		}
		UI_DrawString(0, 96 - (Slot * 16), gString, 19);
	}
	// Running firmware CRC and active slot recorded in the state
	// ('-' no state, '?' corrupt state).
	gString[0] = 'I';
	gString[1] = 'N';
	gString[2] = 'T';
	gString[3] = ' ';
	ToHex(gString + 4, MB_InternalCrc32(MB_INT_APP_SIZE));
	gString[12] = ' ';
	gString[13] = 'A';
	switch (MB_ReadState(&State)) {
	case MB_STATE_VALID:
		gString[14] = '0' + State.Slot;
		break;
	case MB_STATE_MISSING:
		gString[14] = '-';
		break;
	default:
		gString[14] = '?';
		break;
	}
	// Active config bank.
	gString[15] = ' ';
	gString[16] = 'B';
	gString[17] = '0' + MB_ActiveBank();
	UI_DrawString(0, 16, gString, 18);
}

static void RestoreSlot(uint8_t Slot)
{
	KEY_t Key;

	ClearScreen();
	gString[0] = 'R';
	gString[1] = 'e';
	gString[2] = 's';
	gString[3] = 't';
	gString[4] = 'o';
	gString[5] = 'r';
	gString[6] = 'e';
	gString[7] = ' ';
	gString[8] = '0' + Slot;
	gString[9] = '?';
	UI_DrawString(0, 80, gString, 10);
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
	UI_DrawString(0, 80, "Please wait...", 14);
	UI_DrawString(0, 64, "Do not power off", 16);
	// Only returns if the slot was refused: nothing has been modified.
	DrawMessage("Refused, error", MB_RestoreSlot(Slot));
}

void MBDIAG_CheckBoot(void)
{
	KEY_t Key;

	if (KEY_GetButton() != KEY_MENU) {
		return;
	}

	SCREEN_TurnOn();
	gColorForeground = COLOR_FOREGROUND;
	WaitKey(KEY_NONE);

	while (1) {
		DrawSlots();
		do {
			Key = KEY_GetButton();
		} while (Key != KEY_EXIT && Key > KEY_4);
		WaitKey(KEY_NONE);
		if (Key == KEY_EXIT) {
			break;
		}
		RestoreSlot(Key);
	}
	KEY_KeyCounter = 0;

	ClearScreen();
}
