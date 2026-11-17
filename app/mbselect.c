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

#include "app/mbselect.h"
#include "driver/key.h"
#include "driver/mb_flash.h"
#include "helper/helper.h"
#include "misc.h"
#include "ui/gfx.h"
#include "ui/helper.h"

// Text is drawn upwards from its Y coordinate (rows Y-16 to Y-1): the six
// visible text lines are Y = 96, 80, 64, 48, 32 and 16, 20 characters each.
#define LINE(n)		(96 - ((n) * 16))

static KEY_t WaitKey(void)
{
	KEY_t Key;

	do {
		Key = KEY_GetButton();
	} while (Key == KEY_NONE);
	while (KEY_GetButton() != KEY_NONE) {
	}

	return Key;
}

static void ClearScreen(void)
{
	DISPLAY_Fill(0, 159, 0, 96, COLOR_BACKGROUND);
}

static void DrawText(uint8_t Line, const char *pString)
{
	uint8_t Size = 0;

	while (pString[Size]) {
		Size++;
	}
	UI_DrawString(0, LINE(Line), pString, Size);
}

// Copies at most Size printable characters, space padded.
static void CopyField(char *pDst, const char *pSrc, uint8_t Size)
{
	uint8_t i;

	for (i = 0; i < Size && pSrc[i] >= ' ' && pSrc[i] <= '~'; i++) {
		pDst[i] = pSrc[i];
	}
	for (; i < Size; i++) {
		pDst[i] = ' ';
	}
}

// Draws Text followed by a decimal number (0 to 99) on a line.
static void DrawNumber(uint8_t Line, const char *pText, uint8_t Value)
{
	uint8_t Size = 0;

	while (pText[Size]) {
		gString[Size] = pText[Size];
		Size++;
	}
	if (Value >= 10) {
		gString[Size++] = '0' + Value / 10;
	}
	gString[Size++] = '0' + Value % 10;
	UI_DrawString(0, LINE(Line), gString, Size);
}

static void DrawError(uint8_t Error)
{
	ClearScreen();
	DrawNumber(0, "Error ", Error);
	DrawText(5, "Press a key");
	WaitKey();
}

// "*N name______ versio": active slot mark, slot number, name, version.
static void DrawSlotLine(uint8_t Slot, uint8_t Active, bool bSelected)
{
	MB_SlotHeader_t Header;

	CopyField(gString, "", 20);
	gString[0] = (Slot == Active) ? '*' : ' ';
	gString[1] = '0' + Slot;
	if (MB_ReadSlotHeader(Slot, &Header) == MB_OK) {
		CopyField(gString + 3, Header.Name, 10);
		CopyField(gString + 14, Header.Version, 6);
	} else {
		CopyField(gString + 3, "-", 10);
	}
	gColorForeground = bSelected ? COLOR_GREEN : COLOR_FOREGROUND;
	UI_DrawString(0, LINE(Slot), gString, 20);
	gColorForeground = COLOR_FOREGROUND;
}

// Confirmation page: shows the slot, lets UP/DOWN choose the config bank.
// Returns true to go on with Bank.
static bool Confirm(uint8_t Slot, uint8_t *pBank)
{
	MB_SlotHeader_t Header;
	KEY_t Key;

	ClearScreen();
	MB_ReadSlotHeader(Slot, &Header);
	DrawNumber(0, "Slot ", Slot);
	CopyField(gString, Header.Name, 16);
	UI_DrawString(0, LINE(1), gString, 16);
	CopyField(gString, Header.Version, 16);
	UI_DrawString(0, LINE(2), gString, 16);
	DrawText(4, "UP/DOWN: bank");
	DrawText(5, "MENU: OK  EXIT: no");
	while (1) {
		DrawNumber(3, "Config bank ", *pBank);
		Key = WaitKey();
		if (Key == KEY_UP) {
			*pBank = (*pBank + 1) % MB_BANK_COUNT;
		} else if (Key == KEY_DOWN) {
			*pBank = (*pBank + MB_BANK_COUNT - 1) % MB_BANK_COUNT;
		} else {
			return Key == KEY_MENU;
		}
	}
}

void MBSELECT_CheckBoot(void)
{
	MB_State_t State;
	uint8_t Active = 0xFF;
	uint8_t Cursor = 0;
	uint8_t Bank;
	uint8_t Slot;
	KEY_t Key;

	if (KEY_GetButton() != KEY_MENU) {
		return;
	}
	while (KEY_GetButton() != KEY_NONE) {
	}

	SCREEN_TurnOn();
	if (MB_ReadState(&State) == MB_STATE_VALID) {
		Active = State.Slot;
		Cursor = Active;
	}

	while (1) {
		ClearScreen();
		for (Slot = 0; Slot < MB_SLOT_COUNT; Slot++) {
			DrawSlotLine(Slot, Active, Slot == Cursor);
		}
		DrawNumber(5, "Bank ", MB_ActiveBank());

		Key = WaitKey();
		if (Key == KEY_EXIT) {
			break;
		}
		if (Key == KEY_UP) {
			Cursor = (Cursor + MB_SLOT_COUNT - 1) % MB_SLOT_COUNT;
			continue;
		}
		if (Key == KEY_DOWN) {
			Cursor = (Cursor + 1) % MB_SLOT_COUNT;
			continue;
		}
		if (Key <= KEY_4) {
			Cursor = Key;
		} else if (Key != KEY_MENU) {
			continue;
		}

		// Same slot: by default keep the active bank, otherwise the bank
		// of the slot.
		Bank = (Cursor == Active) ? MB_ActiveBank() : Cursor;
		if (!Confirm(Cursor, &Bank)) {
			continue;
		}
		if (Cursor == Active && Bank == MB_ActiveBank()) {
			break;
		}
		ClearScreen();
		DrawText(0, "Please wait...");
		DrawText(1, "Do not power off");
		// Both only return on error, without modifying the internal flash.
		DrawError(Cursor == Active ? MB_SwitchBank(Bank) : MB_RestoreSlot(Cursor, Bank));
	}
	KEY_KeyCounter = 0;

	ClearScreen();
}

