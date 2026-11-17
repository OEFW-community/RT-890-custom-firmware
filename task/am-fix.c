/* Copyright 2023 OneOfEleven
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "task/am-fix.h"
#include "app/radio.h"
#include "driver/bk4819.h"
#include "radio/settings.h"
#include "misc.h"

#ifdef ENABLE_AM_FIX

// Hysteresis configuration constants (compile-time fallback / unused once table is active)
#define HYSTERESIS_HIGH_DB    2    // Trigger gain reduction if signal is > 2dB above target
#define HYSTERESIS_LOW_DB    -3    // Trigger gain recovery only if signal falls < 5dB below target
#define HOLD_TIME_SHORT      25    // ~90ms hold for minor fluctuations
#define HOLD_TIME_LONG       55    // ~240ms hold for heavy overloads

// --- Runtime-selectable AM Hysteresis profile table ---
typedef struct
{
	int8_t   hysteresis_high_dB;
	int8_t   hysteresis_low_dB;
	uint16_t hold_time_short_ms;	// real milliseconds now -- see README
	uint16_t hold_time_long_ms;	// real milliseconds now -- see README
	uint8_t  ema_samples;	// EMA filter weight: 2=fast (2-sample), 4=smooth (4-sample)
} t_am_hysteresis_profile;

// --- Airband Profile ---
static const t_am_hysteresis_profile am_hysteresis_profiles[5] =
{
    // 0: NARROW – ultra-snappy tower-grade
    {  2, -3,  70, 150, 2 },
    // 1: TIGHT – snappy but slightly forgiving
    {  2, -4,  80, 180, 2 },
    // 2: DEFAULT – balanced general airband
    {  3, -4,  95, 210, 4 },
    // 3: RELAXED – smoother for fades & banking turns
    {  3, -5, 115, 240, 4 },
    // 4: WIDE – very smooth for long-range aircraft
    {  3, -5, 140, 260, 4 },
};

#define AM_HYSTERESIS_PROFILE_COUNT  (ARRAY_SIZE(am_hysteresis_profiles))
#define AM_HYSTERESIS_DEFAULT_INDEX  0

static inline const t_am_hysteresis_profile *AM_fix_get_profile(void)
{
	unsigned int idx = gExtendedSettings.AmFixHysteresis;
	if (idx >= AM_HYSTERESIS_PROFILE_COUNT) {
		idx = AM_HYSTERESIS_DEFAULT_INDEX;
	}
	return &am_hysteresis_profiles[idx];
}

typedef struct
{
	uint16_t reg_val;
	int8_t   gain_dB;
} __attribute__((packed)) t_gain_table;

static const t_gain_table gain_table[] =
{
	{0x0000, -98},          // Index 0: Protection safety floor mapped to minimum gain
	{0x0000, -98},          //  1 .. 0 0 0 0 .. -33dB -24dB -8dB -33dB .. -98dB
	{0x0008, -96},          //  2 .. 0 0 1 0 .. -33dB -24dB -6dB -33dB .. -96dB
	{0x0100, -95},          //  3 .. 1 0 0 0 .. -30dB -24dB -8dB -33dB .. -95dB
	{0x0020, -93},          //  4 .. 0 1 0 0 .. -33dB -19dB -8dB -33dB .. -93dB
	{0x0001, -92},          //  5 .. 0 0 0 1 .. -33dB -24dB -8dB -27dB .. -92dB
	{0x0028, -91},          //  6 .. 0 1 1 0 .. -33dB -19dB -6dB -33dB .. -91dB
	{0x0009, -90},          //  7 .. 0 0 1 1 .. -33dB -24dB -6dB -27dB .. -90dB
	{0x0101, -89},          //  8 .. 1 0 0 1 .. -30dB -24dB -8dB -27dB .. -89dB
	{0x0030, -88},          //  9 .. 0 1 2 0 .. -33dB -19dB -3dB -33dB .. -88dB
	{0x0118, -87},          // 10 .. 1 0 3 0 .. -30dB -24dB  0dB -33dB .. -87dB
	{0x0002, -86},          // 11 .. 0 0 0 2 .. -33dB -24dB -8dB -21dB .. -86dB
	{0x0130, -85},          // 12 .. 1 1 2 0 .. -30dB -19dB -3dB -33dB .. -85dB
	{0x0019, -84},          // 13 .. 0 0 3 1 .. -33dB -24dB  0dB -27dB .. -84dB
	{0x0060, -83},          // 14 .. 0 3 0 0 .. -33dB  -9dB -8dB -33dB .. -83dB
	{0x0138, -82},          // 15 .. 1 1 3 0 .. -30dB -19dB  0dB -33dB .. -82dB
	{0x0119, -81},          // 16 .. 1 0 3 1 .. -30dB -24dB  0dB -27dB .. -81dB
	{0x0058, -80},          // 17 .. 0 2 3 0 .. -33dB -14dB  0dB -33dB .. -80dB
	{0x0141, -79},          // 18 .. 1 2 0 1 .. -30dB -14dB -8dB -27dB .. -79dB
	{0x0070, -78},          // 19 .. 0 3 2 0 .. -33dB  -9dB -3dB -33dB .. -78dB
	{0x0180, -77},          // 20 .. 1 4 0 0 .. -30dB  -6dB -8dB -33dB .. -77dB
	{0x0139, -76},          // 21 .. 1 1 3 1 .. -30dB -19dB  0dB -27dB .. -76dB
	{0x0013, -75},          // 22 .. 0 0 2 3 .. -33dB -24dB -3dB -15dB .. -75dB
	{0x0161, -74},          // 23 .. 1 3 0 1 .. -30dB  -9dB -8dB -27dB .. -74dB
	{0x01C0, -73},          // 24 .. 1 6 0 0 .. -30dB  -2dB -8dB -33dB .. -73dB
	{0x00E8, -72},          // 25 .. 0 7 1 0 .. -33dB   0dB -6dB -33dB .. -72dB
	{0x00D0, -71},          // 26 .. 0 6 2 0 .. -33dB  -2dB -3dB -33dB .. -71dB
	{0x0239, -70},          // 27 .. 2 1 3 1 .. -24dB -19dB  0dB -27dB .. -70dB
	{0x006A, -69},          // 28 .. 0 3 1 2 .. -33dB  -9dB -6dB -21dB .. -69dB
	{0x0006, -68},          // 29 .. 0 0 0 6 .. -33dB -24dB -8dB  -3dB .. -68dB
	{0x00B1, -67},          // 30 .. 0 5 2 1 .. -33dB  -4dB -3dB -27dB .. -67dB
	{0x000E, -66},          // 31 .. 0 0 1 6 .. -33dB -24dB -6dB  -3dB .. -66dB
	{0x015A, -65},          // 32 .. 1 2 3 2 .. -30dB -14dB  0dB -21dB .. -65dB
	{0x022B, -64},          // 33 .. 2 1 1 3 .. -24dB -19dB -6dB -15dB .. -64dB
	{0x01F8, -63},          // 34 .. 1 7 3 0 .. -30dB   0dB  0dB -33dB .. -63dB
	{0x0163, -62},          // 35 .. 1 3 0 3 .. -30dB  -9dB -8dB -15dB .. -62dB
	{0x0035, -61},          // 36 .. 0 1 2 5 .. -33dB -19dB -3dB  -6dB .. -61dB
	{0x0214, -60},          // 37 .. 2 0 2 4 .. -24dB -24dB -3dB  -9dB .. -60dB
	{0x01D9, -59},          // 38 .. 1 6 3 1 .. -30dB  -2dB  0dB -27dB .. -59dB
	{0x0145, -58},          // 39 .. 1 2 0 5 .. -30dB -14dB -8dB  -6dB .. -58dB
	{0x02A2, -57},          // 40 .. 2 5 0 2 .. -24dB  -4dB -8dB -21dB .. -57dB
	{0x02D1, -56},          // 41 .. 2 6 2 1 .. -24dB  -2dB -3dB -27dB .. -56dB
	{0x00B3, -55},          // 42 .. 0 5 2 3 .. -33dB  -4dB -3dB -15dB .. -55dB
	{0x0216, -54},          // 43 .. 2 0 2 6 .. -24dB -24dB -3dB  -3dB .. -54dB
	{0x0066, -53},          // 44 .. 0 3 0 6 .. -33dB  -9dB -8dB  -3dB .. -53dB
	{0x00C4, -52},          // 45 .. 0 6 0 4 .. -33dB  -2dB -8dB  -9dB .. -52dB
	{0x006E, -51},          // 46 .. 0 3 1 6 .. -33dB  -9dB -6dB  -3dB .. -51dB
	{0x015D, -50},          // 47 .. 1 2 3 5 .. -30dB -14dB  0dB  -6dB .. -50dB
	{0x00AD, -49},          // 48 .. 0 5 1 5 .. -33dB  -4dB -6dB  -6dB .. -49dB
	{0x007D, -48},          // 49 .. 0 3 3 5 .. -33dB  -9dB  0dB  -6dB .. -48dB
	{0x00D4, -47},          // 50 .. 0 6 2 4 .. -33dB  -2dB -3dB  -9dB .. -47dB
	{0x01B4, -46},          // 51 .. 1 5 2 4 .. -30dB  -4dB -3dB  -9dB .. -46dB
	{0x030B, -45},          // 52 .. 3 0 1 3 ..   0dB -24dB -6dB -15dB .. -45dB
	{0x00CE, -44},          // 53 .. 0 6 1 6 .. -33dB  -2dB -6dB  -3dB .. -44dB
	{0x01B5, -43},          // 54 .. 1 5 2 5 .. -30dB  -4dB -3dB  -6dB .. -43dB
	{0x0097, -42},          // 55 .. 0 4 2 7 .. -33dB  -6dB -3dB   0dB .. -42dB
	{0x0257, -41},          // 56 .. 2 2 2 7 .. -24dB -14dB -3dB   0dB .. -41dB
	{0x02B4, -40},          // 57 .. 2 5 2 4 .. -24dB  -4dB -3dB  -9dB .. -40dB
	{0x027D, -39},          // 58 .. 2 3 3 5 .. -24dB  -9dB  0dB  -6dB .. -39dB
	{0x01DD, -38},          // 59 .. 1 6 3 5 .. -30dB  -2dB  0dB  -6dB .. -38dB
	{0x02AE, -37},          // 60 .. 2 5 1 6 .. -24dB  -4dB -6dB  -3dB .. -37dB
	{0x0379, -36},          // 61 .. 3 3 3 1 ..   0dB  -9dB  0dB -27dB .. -36dB
	{0x035A, -35},          // 62 .. 3 2 3 2 ..   0dB -14dB  0dB -21dB .. -35dB
	{0x02B6, -34},          // 63 .. 2 5 2 6 .. -24dB  -4dB -3dB  -3dB .. -34dB
	{0x030E, -33},          // 64 .. 3 0 1 6 ..   0dB -24dB -6dB  -3dB .. -33dB
	{0x0307, -32},          // 65 .. 3 0 0 7 ..   0dB -24dB -8dB   0dB .. -32dB
	{0x02BE, -31},          // 66 .. 2 5 3 6 .. -24dB  -4dB  0dB  -3dB .. -31dB
	{0x037A, -30},          // 67 .. 3 3 3 2 ..   0dB  -9dB  0dB -21dB .. -30dB
	{0x02DE, -29},          // 68 .. 2 6 3 6 .. -24dB  -2dB  0dB  -3dB .. -29dB
	{0x0345, -28},          // 69 .. 3 2 0 5 ..   0dB -14dB -8dB  -6dB .. -28dB
	{0x03A3, -27},          // 70 .. 3 5 0 3 ..   0dB  -4dB -8dB -15dB .. -27dB
	{0x0364, -26},          // 71 .. 3 3 0 4 ..   0dB  -9dB -8dB  -9dB .. -26dB
	{0x032F, -25},          // 72 .. 3 1 1 7 ..   0dB -19dB -6dB   0dB .. -25dB
	{0x0393, -24},          // 73 .. 3 4 2 3 ..   0dB  -6dB -3dB -15dB .. -24dB
	{0x0384, -23},          // 74 .. 3 4 0 4 ..   0dB  -6dB -8dB  -9dB .. -23dB
	{0x0347, -22},          // 75 .. 3 2 0 7 ..   0dB -14dB -8dB   0dB .. -22dB
	{0x03EB, -21},          // 76 .. 3 7 1 3 ..   0dB   0dB -6dB -15dB .. -21dB
	{0x03D3, -20},          // 77 .. 3 6 2 3 ..   0dB  -2dB -3dB -15dB .. -20dB
	{0x03BB, -19},          // 78 .. 3 5 3 3 ..   0dB  -4dB  0dB -15dB .. -19dB
	{0x037C, -18},          // 79 .. 3 3 3 4 ..   0dB  -9dB  0dB  -9dB .. -18dB
	{0x03CC, -17},          // 80 .. 3 6 1 4 ..   0dB  -2dB -6dB  -9dB .. -17dB
	{0x03C5, -16},          // 81 .. 3 6 0 5 ..   0dB  -2dB -8dB  -6dB .. -16dB
	{0x03EC, -15},          // 82 .. 3 7 1 4 ..   0dB   0dB -6dB  -9dB .. -15dB
	{0x035F, -14},          // 83 .. 3 2 3 7 ..   0dB -14dB  0dB   0dB .. -14dB
	{0x03BC, -13},          // 84 .. 3 5 3 4 ..   0dB  -4dB  0dB  -9dB .. -13dB
	{0x038F, -12},          // 85 .. 3 4 1 7 ..   0dB  -6dB -6dB   0dB .. -12dB
	{0x03E6, -11},          // 86 .. 3 7 0 6 ..   0dB   0dB -8dB  -3dB .. -11dB
	{0x03AF, -10},          // 87 .. 3 5 1 7 ..   0dB  -4dB -6dB   0dB .. -10dB
	{0x03F5,  -9},          // 88 .. 3 7 2 5 ..   0dB   0dB -3dB  -6dB ..  -9dB
	{0x03D6,  -8},          // 89 .. 3 6 2 6 ..   0dB  -2dB -3dB  -3dB ..  -8dB
	{0x03BE,  -7},          // 90 .. 3 5 3 6 ..   0dB  -4dB  0dB  -3dB ..  -7dB original
	{0x03F6,  -6},          // 91 .. 3 7 2 6 ..   0dB   0dB -3dB  -3dB ..  -6dB
	{0x03DE,  -5},          // 92 .. 3 6 3 6 ..   0dB  -2dB  0dB  -3dB ..  -5dB
	{0x03BF,  -4},          // 93 .. 3 5 3 7 ..   0dB  -4dB  0dB   0dB ..  -4dB
	{0x03F7,  -3},          // 94 .. 3 7 2 7 ..   0dB   0dB -3dB   0dB ..  -3dB
	{0x03DF,  -2},          // 95 .. 3 6 3 7 ..   0dB  -2dB  0dB   0dB ..  -2dB
	{0x03FF,   0},          // 96 .. 3 7 3 7 ..   0dB   0dB  0dB   0dB ..   0dB
};

static const unsigned int original_index = 90;
uint16_t gAmFixCountdown;

unsigned int gain_table_index[2] = {original_index, original_index};

// BUGFIX: tracks whether AM Fix was actively running last time Task_AM_fix
// ran, per vfo. Used to call BK4819_RestoreGainSettings() exactly once on
// the enabled->disabled transition, instead of every idle cycle forever.
static bool am_fix_was_active[2] = {false, false};
unsigned int gain_table_index_prev[2] = {0, 0};
int16_t prev_rssi[2] = {0, 0};
volatile uint16_t hold_counter[2] = {0, 0};

// Stuck-state watchdog variable tracking
volatile uint32_t am_fix_stuck_ms[2] = {0, 0};
#define AM_FIX_STUCK_THRESHOLD_MS 3000 // --- faster response ---

int16_t rssi_gain_diff[2] = {0, 0};
unsigned int max_index = ARRAY_SIZE(gain_table) - 1;

	const int16_t desired_rssi = (-83 + 160) * 2;

void AM_fix_init(void)
{
	unsigned int i;
	for (i = 0; i < 2; i++)
	{
		gain_table_index[i] = original_index;
	}

	if (gExtendedSettings.AmFixRssiTrimDb > 20) {
		gExtendedSettings.AmFixRssiTrimDb = 20;
	} else if (gExtendedSettings.AmFixRssiTrimDb < -20) {
		gExtendedSettings.AmFixRssiTrimDb = -20;
	}
}

void AM_fix_reset(const int vfo)
{
	prev_rssi[vfo] = 0;
	hold_counter[vfo] = 0;
	rssi_gain_diff[vfo] = 0;
	gain_table_index[vfo] = original_index;

	gain_table_index_prev[vfo] = 0;
	am_fix_stuck_ms[vfo] = 0;
}

void AM_FIX_ToggleEnabled(void)
{
	gExtendedSettings.AmFixEnabled = !gExtendedSettings.AmFixEnabled;
	if (!gExtendedSettings.AmFixEnabled) {
		BK4819_RestoreGainSettings();
	}
	SETTINGS_SaveGlobals();
}

void AM_fix_tick(void)
{
	unsigned int i;
	for (i = 0; i < 2; i++) {
		if (hold_counter[i]) {
			hold_counter[i]--;
		}
		
		// Watchdog monitors everything from -98 dB up to -30 dB (Index <= 67).
		// If gain stays clamped anywhere in this heavily attenuated zone for 3s, trigger a reset.
		if (gain_table_index[i] <= 67) {
			if (am_fix_stuck_ms[i] < 0xFFFFFFFFu) {
				am_fix_stuck_ms[i]++;
			}
		} else {
			am_fix_stuck_ms[i] = 0;
		}
	}
}

void Task_AM_fix()
{
	if (gAmFixCountdown != 0) {
		return;
	}

	if (!gExtendedSettings.AmFixEnabled || gVfoState[gSettings.CurrentVfo].gModulationType != 1) {
		const int vfo = gCurrentVfo;
		if (am_fix_was_active[vfo]) {
			BK4819_RestoreGainSettings();
			am_fix_was_active[vfo] = false;
		}
		gAmFixCountdown = 400; // Drop task into idle hibernation
		return;
	}
	am_fix_was_active[gCurrentVfo] = true;

	{ // AM Handling Loop Active
		int16_t diff_dB;
		int16_t rssi;
		const int vfo = gCurrentVfo;
		const t_am_hysteresis_profile *profile = AM_fix_get_profile();

		// Watchdog Breakout: Reset index and commit standard gain directly to hardware
		if (am_fix_stuck_ms[vfo] >= AM_FIX_STUCK_THRESHOLD_MS) {
			AM_fix_reset(vfo);
			BK4819_WriteRegister(0x13, gain_table[original_index].reg_val);
			gAmFixCountdown = 50; 
			return; 
		}

		switch (gRadioMode) {
			case RADIO_MODE_QUIET:
			case RADIO_MODE_TX:
				gAmFixCountdown = 25;
				return;
			case RADIO_MODE_INCOMING:
			case RADIO_MODE_RX:
				break;
		}

		{	// Exponential Moving Average Filter
			const int16_t new_rssi = BK4819_GetRSSI()
				+ ((int16_t)gExtendedSettings.AmFixRssiTrimDb * 2);
			if (prev_rssi[vfo] > 0) {
				if (profile->ema_samples == 2) {
					rssi = (new_rssi + prev_rssi[vfo]) / 2;
				} else {
					rssi = (new_rssi + (3 * prev_rssi[vfo])) / 4;
				}
			} else {
				rssi = new_rssi;
			}
			prev_rssi[vfo] = rssi;
		}

		// Calculate deviation from target RSSI
		diff_dB = (rssi - desired_rssi) / 2;

		// FORCE-RECOVERY: Fast-truncate hold time if signal dropped off heavily
		if (diff_dB < -15 && hold_counter[vfo] > 4) {
			hold_counter[vfo] = 4; // Drop hold counter to 40ms remaining
		}

		// --- HYSTERESIS EVALUATION ---
		if (diff_dB > profile->hysteresis_high_dB) {
			// Signal is too hot, decrease gain immediately
			unsigned int index = gain_table_index[vfo];

			if (diff_dB >= 10) {
				// Rapid jump for heavy overloads
				const int16_t desired_gain_dB = (int16_t)gain_table[index].gain_dB - diff_dB + 4;
				while (index > 1) {
					if (gain_table[--index].gain_dB <= desired_gain_dB)
						break;
				}
			} else {
				// Step down incrementally for minor modifications
				if (index > 1) {
					index--;
				}
			}

			// Underflow Floor Boundaries Protection
			if (index < 1) index = 1;
			if (index > max_index) index = max_index;

			if (gain_table_index[vfo] != index) {
				gain_table_index[vfo] = index;

				// TIMING FIX: Convert millisecond targets to 10ms SysTick ticks
				const uint16_t target_ms = (diff_dB > 15) ? profile->hold_time_long_ms : profile->hold_time_short_ms;
				hold_counter[vfo] = target_ms / 10;
			}
		} 
		else if (diff_dB < profile->hysteresis_low_dB) {
			// Signal has dropped into the recovery zone
			if (hold_counter[vfo] == 0) {
				unsigned int index = gain_table_index[vfo];
				
				// Rapid Upward Recovery
				if (diff_dB <= -10) {
					index += 4; // Fast up-climb
				} else {
					index++; // Standard incremental step
				}

				if (index > max_index) index = max_index;
				gain_table_index[vfo] = index;
				
				// Quick 40ms breathing window between recovery steps (4 ticks @ 10ms/tick)
				hold_counter[vfo] = 4; 
			}
		}

		{	// Commit changes to the RF hardware
			const unsigned int index = gain_table_index[vfo];
			gain_table_index_prev[vfo] = index;

			BK4819_WriteRegister(0x13, gain_table[index].reg_val);

			// Calculate compensation offset for S-meter readings
			rssi_gain_diff[vfo] = ((int16_t)gain_table[index].gain_dB - gain_table[original_index].gain_dB) * 2;
		}

		gAmFixCountdown = 50;
	}
}

#endif