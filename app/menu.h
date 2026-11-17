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

#ifndef APP_MENU_H
#define APP_MENU_H

#include <stdbool.h>
#include <stdint.h>

enum {
	// --- GROUP 1: Radio ---
	MENU_SQ_LEVEL = 0,
	MENU_FREQ_STEP,
	MENU_MODULATION,
	MENU_BAND_WIDTH,
	MENU_AM_HYSTERESIS,
	MENU_AM_RSSI_TRIM,	// dB trim applied to AM Fix RSSI on BOTH PCB versions -- antenna A/B calibration
	MENU_AM_RSSI_TRIM_RESET,	// Yes/No confirm -- zeroes AmFixRssiTrimDb and saves
	MENU_VHF_CUTOFF,
	MENU_MIC_GAIN,
	MENU_TX_POWER,
	MENU_TX_PRIORITY,
	MENU_REPEATER_MODE,
	MENU_TAIL_TONE,
	MENU_ROGER_BEEP,
	// --- GROUP 2: Squelch / Codes / Privacy ---
	MENU_CTCSS_DCS,
	MENU_RX_CTCSS_DCS,
	MENU_TX_CTCSS_DCS,
	MENU_BUSY_LOCK,
	MENU_SCRAMBLER,
	MENU_DCS_ENCRYPT,
	MENU_MUTE_CODE,
	MENU_PROHIBIT_TX,
	// --- GROUP 3: Scanning ---
	MENU_SCAN_START,	// Scan/search range lower bound, 8-digit MHz.decimal entry (e.g. 128.10000)
	MENU_SCAN_FINISH,	// Scan/search range upper bound, 8-digit MHz.decimal entry (e.g. 128.10000)
	MENU_SCAN_RESUME,
	MENU_SCAN_DIR,
	MENU_LIST_TO_SCAN,
	MENU_SCANLIST_1,
	MENU_SCANLIST_2,
	MENU_SCANLIST_3,
	MENU_SCANLIST_4,
	MENU_SCANLIST_5,
	MENU_SCANLIST_6,
	MENU_SCANLIST_7,
	MENU_SCANLIST_8,
	// --- GROUP 4: Channel Management ---
	MENU_CH_NAME,
	MENU_SAVE_CH,
	MENU_DELETE_CH,
	// --- GROUP 5: VOX ---
	MENU_VOX_LEVEL,
	MENU_VOX_DELAY,
	// --- GROUP 6: DTMF ---
	MENU_DTMF_MODE,
	MENU_DTMF_SELECT,
	MENU_DTMF_DELAY,
	MENU_DTMF_INTERVAL,
	MENU_DTMF_DISPLAY,
	// --- GROUP 7: Key Assignments ---
	MENU_K1_LONG,
	MENU_K1_SHORT,
	MENU_K2_LONG,
	MENU_K2_SHORT,
	MENU_0_LONG,
	MENU_1_LONG,
	MENU_2_LONG,
	MENU_3_LONG,
	MENU_4_LONG,
	MENU_5_LONG,
	MENU_6_LONG,
	MENU_7_LONG,
	MENU_8_LONG,
	MENU_9_LONG,
	MENU_STAR_LONG,
	MENU_HASH_LONG,
	MENU_MENU_LONG,
	MENU_EXIT_LONG,
	MENU_KEYS_RESET,
	// --- GROUP 8: Display / UI ---
	MENU_DUAL_DISPLAY,
	MENU_DARK_MODE,
	MENU_STARTUP_LOGO,
	MENU_PROMPT_TEXT,
	MENU_VOLTAGE,
	MENU_LED_TIMER,
	MENU_SCAN_BLINK,
	MENU_LED_HEARTBEAT,
	MENU_RINGTONE,
	MENU_VOICE_PROMPT,
	MENU_KEY_BEEP,
	MENU_PERSONAL_ID,
	// --- GROUP 9: Power / Radio Behaviour ---
	MENU_FM_STANDBY,
	MENU_SAVE_MODE,
	MENU_LOCK_TIMER,
	MENU_TOT,
	// --- GROUP 10: System ---
	MENU_PCB_VERSION,
	MENU_INITIALIZE,
	MENU_VERSION,

};

extern uint16_t gSettingGolay;
extern uint8_t gMenuIndex;
extern uint8_t gSettingIndex;
extern uint8_t gSettingsCount;

void MENU_SetMaxCssValues(void);
void MENU_AcceptSetting(void);
void MENU_DrawSetting(void);
void MENU_Redraw(bool bFlag);
void MENU_KeyHandler(uint8_t Key);
void MENU_Next(uint8_t Key);
void MENU_SettingKeyHandler(uint8_t Key);
void MENU_ScrollSetting(uint8_t Key);
void MENU_PlayAudio(uint8_t ID);

#endif

