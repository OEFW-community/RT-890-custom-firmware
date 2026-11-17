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

#include "driver/serial-flash.h"
#include "driver/st7735s.h"
#include "helper/helper.h"
#include "radio/channels.h"
#include "radio/settings.h"
#include "task/keyaction.h"
#include "task/sidekeys.h"
#include "ui/gfx.h"
#include "ui/helper.h"
#include "ui/menu.h"

void UI_DrawSettingOptionEx(const char *pString, uint8_t Length, uint8_t Index)
{
	UI_DrawString(24, 48 - (Index * 24), pString, Length);
}

void UI_DrawSettingOption(const char *pString, uint8_t Index)
{
	UI_DrawString(24, 48 - (Index * 24), pString, 16);
}

void UI_DrawSettingRoger(uint8_t Index)
{
	static const char Mode[4][7] = {
		"Off    ",
		"Roger A",
		"Roger B",
		"FSK ID ",
	};

	UI_DrawSettingOptionEx(Mode[Index], 7, 0);
	UI_DrawSettingOptionEx(Mode[(Index + 1) % 4], 7, 1);
}

void UI_DrawDtmfMode(uint8_t Index)
{
	static const char Mode[4][16] = {
		"Off             ",
		"TX Start        ",
		"TX End          ",
		"TX Start and End",
	};

	UI_DrawSettingOption(Mode[Index], 0);
	UI_DrawSettingOption(Mode[(Index + 1) % 4], 1);
}

void UI_DrawDtmfSelect(uint8_t Index)
{
	Int2Ascii(1 + Index, 2);
	UI_DrawSettingOptionEx(gShortString, 2, 0);

	Int2Ascii(1 + ((Index + 1) % 16), 2);
	UI_DrawSettingOptionEx(gShortString, 2, 1);
}

void UI_DrawToggle(void)
{
	static const char Toggle[2][3] = {
		"Off",
		"On ",
	};
	DISPLAY_Fill(0, 159, 1, 55, COLOR_BACKGROUND);
	UI_DrawSettingOptionEx(Toggle[0], 3, 0);
	UI_DrawSettingOptionEx(Toggle[1], 3, 1);
}

void UI_DrawSettingArrow(uint8_t Selection)
{
	uint16_t Bitmap[6];
	uint8_t i;
	uint8_t j;

	Bitmap[0] = 0x1FFC;
	Bitmap[1] = 0x0FF8;
	Bitmap[2] = 0x07F0;
	Bitmap[3] = 0x03E0;
	Bitmap[4] = 0x01C0;
	Bitmap[5] = 0x0080;

	DISPLAY_Fill(8, 16,  8, 23, COLOR_BACKGROUND);
	DISPLAY_Fill(1, 16, 32, 47, COLOR_BACKGROUND);

	for (i = 0; i < 6; i++) {
		uint16_t Pixel = Bitmap[i];

		ST7735S_SetPosition(8 + i, 32 - (Selection * 24));

		for (j = 0; j < 16; j++) {
			if (Pixel & 0x8000U) {
				ST7735S_SendU16(COLOR_FOREGROUND);
			} else {
				ST7735S_SendU16(gColorBackground);
			}
			Pixel <<= 1;
		}
	}
}

void UI_DrawDtmfInterval(uint8_t Interval)
{
	gShortString[3] = 'm';
	gShortString[4] = 's';
	Int2Ascii((Interval + 3) * 10, 3);
	UI_DrawString(24, 48, gShortString, 5);

	Int2Ascii((3 + ((Interval + 1) % 18)) * 10, 3);
	UI_DrawString(24, 24, gShortString, 5);
}

void UI_DrawDtmfDelay(uint8_t Delay)
{
	gShortString[4] = 'm';
	gShortString[5] = 's';
	Int2Ascii(Delay * 100, 4);
	UI_DrawString(24, 48, gShortString, 6);

	Int2Ascii(((Delay + 1) % 21) * 100, 4);
	UI_DrawString(24, 24, gShortString, 6);
}

void UI_DrawActions(uint8_t Index)
{
	static const char Actions[][12] = {
		"None        ",
		"Monitor     ",
		"Freq Detect ",
		"Repeater    ",
		"Preset Ch   ",
		"Local Alarm ",
		"Remote Alarm",
#ifdef ENABLE_NOAA
		"MCA MSI     ",
#else
		"[DISABLED]  ",
#endif
		"Send Tone   ",
		"TX Tone     ",
		"FM Radio    ",
		"Freq Scanner",
		"Flashlight  ",
#ifdef ENABLE_AM_FIX
		"AM Fix      ",
#else
		"[DISABLED]  ",
#endif
		"VOX         ",
		"TX Power    ",
		"Squelch     ",
		"Dual Watch  ",
		"Backlight   ",
		"Freq Step   ",
		"Key Beep    ",
		"Bank +/- Ch ",
		"DTMF Input  ",
		"Dual Display",
		"TX Frequency",
		"Lock        ",
		#ifdef ENABLE_SPECTRUM
		"Spectrum    ",
#else
		"[DISABLED]  ",
#endif
		"Dark Theme  ",
		"RF Gain     ",
#ifdef ENABLE_REGISTER_EDIT
		"Reg Editor  ",
#else
		"[DISABLED]  ",
#endif
		"Mic Gain    ",
		"Modulation  ",
		"Bandwidth   ",
		"TX CTCSS/DCS",
		"TX Priority ",
		"Prohibit TX ",
	};

	UI_DrawSettingOptionEx(Actions[Index], 12, 0);
	UI_DrawSettingOptionEx(Actions[(Index + 1) % ACTIONS_COUNT], 12, 1);
}

void UI_DrawChannelName(uint16_t Channel)
{
	char String[12];
	ChannelInfo_t Info;

	String[0] = 'C';
	String[1] = 'H';
	String[2] = '-';
	String[3] = '0';
	String[4] = '0';
	String[5] = '1';
	String[6] = ' ';
	String[7] = ' ';
	String[8] = 'E';
	String[9] = 0;
	String[10] = 0;
	String[11] = 0;

	Int2Ascii(Channel + 1, 3);
	String[3] = gShortString[0];
	String[4] = gShortString[1];
	String[5] = gShortString[2];
	SFLASH_Read(&Info, 0x3C2000 + (Channel * sizeof(Info)), sizeof(Info));
	if (Info.Available) {
		String[8] = 'E';
	} else {
		String[8] = 'F';
	}
	UI_DrawString(24, 48, String, 9);

	Int2Ascii(1 + (Channel + 1U) % 999U, 3);
	String[3] = gShortString[0];
	String[4] = gShortString[1];
	String[5] = gShortString[2];
	SFLASH_Read(&Info, 0x3C2000 + ((Channel + 1U) % 999U) * sizeof(Info), sizeof(Info));
	if (Info.Available) {
		String[8] = 'E';
	} else {
		String[8] = 'F';
	}
	UI_DrawString(24, 24, String, 9);
}

void UI_DrawMute(uint8_t Index, uint16_t Golay, bool bEnabled)
{
	static const char Mode[3][6] = {
		"Off   ",
		"23bits",
		"24bits",
	};

	UI_DrawSettingOptionEx(Mode[Index], 6, 0);

	if (bEnabled) {
		Int2Ascii(Golay, 8);
		UI_DrawString(24, 24, gShortString, 8);
	}
}

void UI_DrawEncrypt(uint8_t Index)
{
	static const char Mode[4][9] = {
		"Off      ",
		"Encrypt 1",
		"Encrypt 2",
		"Encrypt 3",
	};

	UI_DrawSettingOptionEx(Mode[Index], 9, 0);
	UI_DrawSettingOptionEx(Mode[(Index + 1) % 4], 9, 1);
}

void UI_DrawScrambler(uint8_t Index)
{
	char String[2];

	String[0] = ' ';
	String[1] = '0' + Index;
	UI_DrawSettingOptionEx(String, 2, 0);
	String[1] = '0' + (Index + 1) % 9;
	UI_DrawSettingOptionEx(String, 2, 1);
}

void UI_DrawActivateBy(void)
{
	DISPLAY_Fill(1, 158, 1, 19, gColorBackground);
	DISPLAY_DrawRectangle0(1, 20, 159, 1, gColorForeground);
	gColorForeground = COLOR_FOREGROUND;
	UI_DrawString(20, 18, "Activate by [#]", 15);
	gColorForeground = COLOR_FOREGROUND;
}


void UI_DrawCursor(uint8_t X, bool bVisible)
{
	uint16_t Color;

	if (bVisible) {
		Color = COLOR_FOREGROUND;
	} else {
		Color = gColorBackground;
	}
	DISPLAY_DrawRectangle1(4 + (X * 8), 32, 20, 1, Color);
}

void UI_DrawTxPriority(void)
{
	UI_DrawSettingOptionEx("Edit", 4, 0);
	UI_DrawSettingOptionEx("Busy", 4, 1);
}

void UI_DrawFrequencyStep(uint8_t Index)
{
	#define MENU_STEPS_COUNT  15
	#define MENU_STEPS_LENGTH 8
	static const char Mode[MENU_STEPS_COUNT][MENU_STEPS_LENGTH] = {
		"10 Hz   ",
		"250 Hz  ",
		"1.25 kHz",
		"2.5 kHz ",
		"5 kHz   ",
		"6.25 kHz",
		"8.33 kHz",
		"10 kHz  ",
		"12.5 kHz",
		"20 kHz  ",
		"25 kHz  ",
		"50 kHz  ",
		"100 kHz ",
		"500 kHz ",
		"1 MHz   "
		//"5 MHz   ",
	};

	UI_DrawSettingOptionEx(Mode[Index], MENU_STEPS_LENGTH, 0);
	UI_DrawSettingOptionEx(Mode[(Index + 1) % MENU_STEPS_COUNT], MENU_STEPS_LENGTH, 1);
}

void UI_DrawTimer(uint8_t Index)
{
	char String[4];
	uint16_t Timer;

	String[0] = ' ';
	String[1] = ' ';
	String[2] = ' ';
	String[3] = ' ';

	switch (Index) {
	case 0:
		UI_DrawSettingOptionEx(" Off", 4, 0);
		UI_DrawSettingOptionEx("   5", 4, 1);
		break;

	case 1:
		UI_DrawSettingOptionEx("   5", 4, 0);
		UI_DrawSettingOptionEx("  10", 4, 1);
		break;

	case 2:
		UI_DrawSettingOptionEx("  10", 4, 0);
		UI_DrawSettingOptionEx("  15", 4, 1);
		break;

	case 42:
		UI_DrawSettingOptionEx(" 600", 4, 0);
		UI_DrawSettingOptionEx(" Off", 4, 1);
		break;

	default:
		Timer = (Index - 2) * 15;
		if (Timer < 100) {
			Int2Ascii(Timer, 2);
			String[2] = gShortString[0];
			String[3] = gShortString[1];
			UI_DrawSettingOptionEx(String, 4, 0);
		} else {
			Int2Ascii(Timer, 3);
			String[1] = gShortString[0];
			String[2] = gShortString[1];
			String[3] = gShortString[2];
			UI_DrawSettingOptionEx(String, 4, 0);
		}
		Timer = (Index - 1) * 15;
		if (Timer < 100) {
			Int2Ascii(Timer, 2);
			String[2] = gShortString[0];
			String[3] = gShortString[1];
			UI_DrawSettingOptionEx(String, 4, 1);
		} else {
			Int2Ascii(Timer, 3);
			String[1] = gShortString[0];
			String[2] = gShortString[1];
			String[3] = gShortString[2];
			UI_DrawSettingOptionEx(String, 4, 1);
		}
		break;
	}
}

void UI_DrawLevel(uint8_t Index)
{
	char Digit;

	gColorForeground = COLOR_FOREGROUND;
	Digit = '0' + Index;
	UI_DrawSettingOptionEx(&Digit, 1, 0);
	Digit = '0' + (Index + 1) % 10;
	UI_DrawSettingOptionEx(&Digit, 1, 1);
}

void UI_DrawScanDirection(void)
{
	UI_DrawSettingOptionEx("Up  ", 4, 0);
	UI_DrawSettingOptionEx("Down", 4, 1);
}

void UI_DrawDeviceName(const char *pName)
{
	UI_DrawString(4, 48, pName, 16);
}

void UI_DrawSettingRepeaterMode(uint8_t Index)
{
	static const char Mode[3][14] = {
		"Off           ",
		"No RX TSQL    ",
		"Monitor input ",
	};

	UI_DrawSettingOptionEx(Mode[Index], 13, 0);
	UI_DrawSettingOptionEx(Mode[(Index + 1) % 3], 13, 1);
}

void UI_DrawSettingTxPower(void)
{
	UI_DrawSettingOptionEx("High", 4, 0);
	UI_DrawSettingOptionEx("Low ", 4, 1);
}

void UI_DrawSettingModulation(uint8_t Index)
{
	static const char Mode[4][3] = {
		"FM ",
		"AM ",
		"LSB",
		"USB",
	};

	UI_DrawSettingOptionEx(Mode[Index], 3, 0);
	UI_DrawSettingOptionEx(Mode[(Index + 1) % 4], 3, 1);
}

void UI_DrawSettingBandwidth(void)
{
	UI_DrawSettingOptionEx("25.0 kHz", 8, 0);
	UI_DrawSettingOptionEx("12.5 kHz", 8, 1);
}

void UI_DrawSettingBusyLock(uint8_t Index)
{
	static const char Mode[3][7] = {
		"Off    ",
		"Carrier",
		"CTC/DCS",
	};

	UI_DrawSettingOptionEx(Mode[Index], 7, 0);
	UI_DrawSettingOptionEx(Mode[(Index + 1) % 3], 7, 1);
}

void UI_DrawSettingScanResume(uint8_t Index)
{
	static const char Mode[3][16] = {
			"CO (5.5s)       ",	// 1
			"TO (2.5s)       ",	// 2
			"SE (>[]<)       ",	// 3
	};
	// Values start at 1 instead of 0 for this setting
	UI_DrawSettingOption(Mode[Index], 0);
	UI_DrawSettingOption(Mode[(Index + 1) % 3], 1);
}

void UI_DrawSettingAmHysteresis(uint8_t Index)
{
	static const char Mode[5][16] = {
			"Narrow          ",	// 0
			"Tight           ",	// 1
			"Default         ",	// 2
			"Relaxed         ",	// 3
			"Wide            ",	// 4
	};
	UI_DrawSettingOption(Mode[Index], 0);
	UI_DrawSettingOption(Mode[(Index + 1) % 5], 1);
}

void UI_DrawSettingVhfCutoff(uint8_t Index)
{
	static const char Mode[3][16] = {
			"240 MHz         ",	// 0 - default
			"280 MHz         ",	// 1 - SATCOM
			"300 MHz         ",	// 2 - SATCOM
	};
	UI_DrawSettingOption(Mode[Index], 0);
	UI_DrawSettingOption(Mode[(Index + 1) % 3], 1);
}

void UI_DrawSettingPcbVersion(uint8_t Index)
{
	static const char Mode[2][16] = {
			"PCB 2.0         ",	// 0 - original hardware
			"PCB 2.1         ",	// 1 - revised hardware
	};
	UI_DrawSettingOption(Mode[Index], 0);
	UI_DrawSettingOption(Mode[(Index + 1) % 2], 1);
}

// Signed-dB line drawer for the antenna A/B trim menu. Sign handling
// mirrors ConvertRssiToDbm() in ui/helper.c.
//
// Always draws a fixed width of 7 characters, zero-padded to 2 digits.
// UI_DrawSettingOptionEx() does not clear the row before drawing, so a
// variable-width value here would leave stale characters behind when a
// shorter string follows a longer one. This matches the fixed-width
// convention already used by UI_DrawRxDBM() and the battery voltage
// display elsewhere in this file.
static void DrawSignedDbLine(uint8_t Index, uint8_t Line)
{
	int16_t dB = (int16_t)Index - 20;
	uint16_t uAbs = (dB < 0) ? (uint16_t)(-dB) : (uint16_t)dB;

	// Always exactly 2 digits, zero-padded (e.g. "03", not "3").
	Int2Ascii(uAbs, 2);

	// Shift right one slot to make room for a sign character -- always,
	// even for positive/zero, so the field width never changes.
	gShortString[2] = gShortString[1];
	gShortString[1] = gShortString[0];
	gShortString[0] = (dB < 0) ? '-' : ' ';

	gShortString[3] = ' ';
	gShortString[4] = 'd';
	gShortString[5] = 'B';
	gShortString[6] = ' ';

	gColorForeground = COLOR_FOREGROUND;
	// Fixed length of 7 on every call, regardless of value -- see the
	// function comment above; do not make this width-dependent.
	UI_DrawSettingOptionEx(gShortString, 7, Line);
}

void UI_DrawSettingAmRssiTrim(uint8_t Index)
{
	DrawSignedDbLine(Index, 0);
	DrawSignedDbLine((Index + 1) % 41, 1);
}

// Scan Start / Scan Finish: MHz.decimal display, e.g. "128.10000" for
// 128.1MHz. Raw is in the same x10Hz units as RX.Frequency, so the
// fractional part (Raw % 100000) already carries the full 5-digit / 10Hz
// precision those units support. Fixed width every call (same reasoning
// as DrawSignedDbLine above -- see its comment for why variable-width
// redraws cause stale characters to linger on screen). Single
// non-scrolling value, no "next" preview line.
void UI_DrawScanRangeMHz(uint32_t Raw)
{
	char Frac[5];
	uint8_t i;

	if (Raw > 130000000UL) {
		Raw = 130000000UL;
	}

	// Grab the 5 fractional digits first -- the Int2Ascii call below for
	// the whole-MHz part reuses/overwrites gShortString, so they'd
	// otherwise get clobbered before we can place them (same reason
	// DrawCss above computes CTCSS/DCS digits before rearranging them).
	Int2Ascii(Raw % 100000UL, 5);
	for (i = 0; i < 5; i++) {
		Frac[i] = gShortString[i];
	}
	Int2Ascii(Raw / 100000UL, 3);
	gShortString[3] = '.';
	for (i = 0; i < 5; i++) {
		gShortString[4 + i] = Frac[i];
	}
	gColorForeground = COLOR_FOREGROUND;
	UI_DrawString(24, 48, gShortString, 9);
}

// Live preview while typing -- pString holds up to 8 raw digit values
// (0-9), or '-' placeholders for not-yet-typed positions (same convention
// as UI_DrawChannelNumber). The decimal point sits at a fixed position
// after the 3rd digit regardless of how many digits have been typed so
// far, matching the confirmed-value layout above.
void UI_DrawScanRangeDigits(const char *pString)
{
	uint8_t i;

	for (i = 0; i < 3; i++) {
		gShortString[i] = (pString[i] == '-') ? '-' : ('0' + pString[i]);
	}
	gShortString[3] = '.';
	for (i = 3; i < 8; i++) {
		gShortString[i + 1] = (pString[i] == '-') ? '-' : ('0' + pString[i]);
	}
	gColorForeground = COLOR_FOREGROUND;
	UI_DrawString(24, 48, gShortString, 9);
}

void UI_DrawSettingScanlist(uint8_t Index)
{
	static const char Mode[9][8] = {
			"Bank 1  ",
			"Bank 2  ",
			"Bank 3  ",
			"Bank 4  ",
			"Bank 5  ",
			"Bank 6  ",
			"Bank 7  ",
			"Bank 8  ",
			"All     ",
	};

	UI_DrawSettingOptionEx(Mode[Index], 8, 0);
	UI_DrawSettingOptionEx(Mode[(Index + 1) % 9], 8, 1);
}

void UI_DrawSettingMicGain(uint8_t Index)
{
	gColorForeground = COLOR_FOREGROUND;
	Int2Ascii(Index, 2);
	UI_DrawString(24, 48, gShortString, 2);
	Int2Ascii((Index + 1) % 32, 2);
	UI_DrawString(24, 24, gShortString, 2);
}