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

#include "app/css.h"
#include "app/radio.h"
#include "bsp/gpio.h"
#include "driver/bk4819.h"
#include "driver/delay.h"
#include "driver/pins.h"
#include "driver/speaker.h"
#include "helper/helper.h"
#include "misc.h"
#include "radio/settings.h"
#include <stddef.h>

// PA Control register (0x36) bit field layout.
// Must match REGx36 in driver/bk4819.h if that header is later updated.
typedef union {
	struct {
		uint16_t  PA_Gain2  : 3;   // bits [2:0]  — second gain stage
		uint16_t  PA_Gain1  : 3;   // bits [5:3]  — first gain stage
		uint16_t  _res6     : 1;   // bit  [6]    — reserved
		uint16_t  PA_Enable : 1;   // bit  [7]    — PA on/off
		uint16_t  PA_Bias   : 8;   // bits [15:8] — bias current
	};
	uint16_t val;
} REGx36;

enum {
	GPIO_FILTER_UHF     = 1U << 5,
	GPIO_FILTER_VHF_2_0 = 1U << 6,	// PCB 2.0
	GPIO_FILTER_VHF_2_1 = 1U << 3,	// PCB 2.1
	GPIO_FILTER_UNKWOWN = 1U << 7,
};

// Runtime helper — returns correct VHF filter bit for active PCB revision
#define GPIO_FILTER_VHF \
	(gExtendedSettings.PcbVersion ? GPIO_FILTER_VHF_2_1 : GPIO_FILTER_VHF_2_0)

static void Delay(volatile uint8_t Counter)
{
	while (Counter-- > 0) {
	}
}

static void SDA_SetOutput(void)
{
	gpio_init_type init;

	gpio_default_para_init_ex(&init);
	init.gpio_pins = BOARD_GPIOB_BK4819_SDA;
	init.gpio_mode = GPIO_MODE_OUTPUT;
	init.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
	gpio_init(GPIOB, &init);
}

static void SDA_SetInput(void)
{
	gpio_init_type init;

	gpio_default_para_init_ex(&init);
	init.gpio_pins = BOARD_GPIOB_BK4819_SDA;
	init.gpio_mode = GPIO_MODE_INPUT;
	init.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
	gpio_init(GPIOB, &init);
}


static void I2C_Send(uint8_t Data)
{
	uint8_t i;

	for (i = 0; i < 8; i++) {
		gpio_bits_reset(GPIOB, BOARD_GPIOB_BK4819_SCL);
		if (Data & 0x80U) {
			gpio_bits_set(GPIOB, BOARD_GPIOB_BK4819_SDA);
		} else {
			gpio_bits_reset(GPIOB, BOARD_GPIOB_BK4819_SDA);
		}
		Delay(10);
		gpio_bits_set(GPIOB, BOARD_GPIOB_BK4819_SCL);
		Data <<= 1;
		Delay(10);
	}
}

static uint16_t I2C_RecvU16(void)
{
	uint8_t i;
	uint16_t Data = 0;

	SDA_SetInput();

	gpio_bits_reset(GPIOB, BOARD_GPIOB_BK4819_SCL);
	for (i = 0; i < 16; i++) {
		Data <<= 1;
		gpio_bits_set(GPIOB, BOARD_GPIOB_BK4819_SCL);
		if (gpio_input_data_bit_read(GPIOB, BOARD_GPIOB_BK4819_SDA)) {
			Data |= 1;
		}
		Delay(10);
		gpio_bits_reset(GPIOB, BOARD_GPIOB_BK4819_SCL);
		Delay(10);
	}

	return Data;
}

#if 0
static void DisableAGC(uint32_t Unknown)
{
	BK4819_WriteRegister(0x13, 0x03FF);
	BK4819_WriteRegister(0x12, 0x037B);
	BK4819_WriteRegister(0x11, 0x027B);
	BK4819_WriteRegister(0x10, 0x007A);
	BK4819_WriteRegister(0x14, 0x0019);
	BK4819_WriteRegister(0x49, 0x2A38);
	BK4819_WriteRegister(0x7B, 0x8420);
}
#endif

void OpenAudio(bool bIsNarrow, uint8_t gModulationType)
{
	switch(gModulationType) {
		case 0:
			BK4819_SetAF(BK4819_AF_OPEN);
			break;
		case 1:
			BK4819_SetAF(BK4819_AF_AM);
			break;
		case 2:
			BK4819_SetAF(BK4819_AF_LSB);
			break;
		case 3:
			BK4819_SetAF(BK4819_AF_USB);
			break;
	}
	if (bIsNarrow) {
		BK4819_SetAfGain(gFrequencyBandInfo.RX_DAC_GainNarrow);
	} else {
		BK4819_SetAfGain(gFrequencyBandInfo.RX_DAC_GainWide);
	}
}

// Public

uint16_t BK4819_ReadRegister(uint8_t Reg)
{
	uint16_t Data;

	TMR1->ctrl1_bit.tmren = FALSE;

	SDA_SetOutput();

	gpio_bits_reset(GPIOB, BOARD_GPIOB_BK4819_SCL);
	gpio_bits_reset(GPIOB, BOARD_GPIOB_BK4819_CS);

	I2C_Send(0x80U | Reg);
	Delay(10);
	Data = I2C_RecvU16();

	gpio_bits_set(GPIOB, BOARD_GPIOB_BK4819_CS);

	TMR1->ctrl1_bit.tmren = TRUE;

	return Data;
}

void BK4819_WriteRegister(uint8_t Reg, uint16_t Data)
{
	TMR1->ctrl1_bit.tmren = FALSE;

	SDA_SetOutput();

	gpio_bits_reset(GPIOB, BOARD_GPIOB_BK4819_SCL);
	gpio_bits_reset(GPIOB, BOARD_GPIOB_BK4819_CS);

	I2C_Send(Reg);
	I2C_Send((Data >> 8) & 0xFFU);
	I2C_Send((Data >> 0) & 0xFFU);

	gpio_bits_set(GPIOB, BOARD_GPIOB_BK4819_CS);

	TMR1->ctrl1_bit.tmren = TRUE;
}

uint16_t BK4819_GetRSSI(void)
{
	return BK4819_ReadRegister(0x67) & 0x01FF;
}

void BK4819_Init(void)
{
	BK4819_WriteRegister(0x00, 0x8000);
	BK4819_WriteRegister(0x00, 0x0000);
	BK4819_WriteRegister(0x37, 0x1D0F);
	// PCB 2.1 uses different GPIO filter mapping for register 0x33
	BK4819_WriteRegister(0x33, gExtendedSettings.PcbVersion ? 0x5700 : 0x1F00);
	BK4819_WriteRegister(0x35, 0x0000);
	BK4819_WriteRegister(0x1E, 0x4C58);
	BK4819_WriteRegister(0x1F, 0xA656);
	BK4819_WriteRegister(0x3E, gCalibration.BandSelectionThreshold);
	BK4819_WriteRegister(0x3F, 0x0000);
	BK4819_WriteRegister(0x2A, 0x4F18);
	BK4819_WriteRegister(0x53, 0xE678);
	BK4819_WriteRegister(0x2C, 0x5705);
	BK4819_WriteRegister(0x4B, 0x7102);
	//BK4819_WriteRegister(0x77, 0x88EF);
	BK4819_WriteRegister(0x26, 0x13A0);
	BK4819_SetAFResponseCoefficients(false, true,  gCalibration.RX_3000Hz_Coefficient);
	BK4819_SetAFResponseCoefficients(false, false, gCalibration.RX_300Hz_Coefficient);
	BK4819_SetAFResponseCoefficients(true,  true,  gCalibration.TX_3000Hz_Coefficient);
	BK4819_SetAFResponseCoefficients(true,  false, gCalibration.TX_300Hz_Coefficient);
	BK4819_EnableRX();
	BK4819_SetAF(BK4819_AF_MUTE);
	BK4819_RestoreGainSettings();
}

void BK4819_SetAFResponseCoefficients(bool bTx, bool bLowPass, uint8_t Index)
{
	uint16_t HighPass;
	uint16_t LowPass;

	if (bLowPass) {
		LowPass = 0;
		switch (Index) {
		case 0: HighPass = 58908; break;
		case 1: HighPass = 57122; break;
		case 2: HighPass = 54317; break;
		case 3: HighPass = 52277; break;
		case 4: HighPass = 64002; break;
		default: HighPass = 62731; break;
		}
	} else {
		switch (Index) {
		case 0: HighPass = 35799; LowPass = 13575; break;
		case 1: HighPass = 35565; LowPass = 13793; break;
		case 2: HighPass = 35478; LowPass = 13830; break;
		case 3: HighPass = 35080; LowPass = 14201; break;
		case 4: HighPass = 36318; LowPass = 13146; break;
		case 5: HighPass = 36531; LowPass = 12986; break;
		case 6: HighPass = 36744; LowPass = 12801; break;
		default: HighPass = 35811; LowPass = 13613; break;
		}
	}
	if (bTx) {
		if (!bLowPass) {
			BK4819_WriteRegister(0x44, HighPass);
			BK4819_WriteRegister(0x45, LowPass);
		} else {
			BK4819_WriteRegister(0x74, HighPass);
		}
	} else {
		if (!bLowPass) {
			BK4819_WriteRegister(0x54, HighPass);
			BK4819_WriteRegister(0x55, LowPass);
		} else {
			BK4819_WriteRegister(0x75, HighPass);
		}
	}
}

void BK4819_EnableRX(void)
{
	BK4819_WriteRegister(0x37, 0x1F0F);
	DELAY_WaitMS(10);
	BK4819_WriteRegister(0x30, 0x0200);
	BK4819_WriteRegister(0x30, 0xBFF1);
}

void BK4819_SetAF(BK4819_AF_Type_t Type)
{
	BK4819_WriteRegister(0x47, 0x6040 | (Type << 8));
}

void BK4819_SetFrequency(uint32_t Frequency)
{
	FREQUENCY_SelectBand(Frequency);
	Frequency = (Frequency - 32768U) + gFrequencyBandInfo.FrequencyOffset;
	BK4819_WriteRegister(0x38, (Frequency >>  0) & 0xFFFFU);
	BK4819_WriteRegister(0x39, (Frequency >> 16) & 0xFFFFU);
}

void BK4819_SetSquelchGlitch(bool bIsNarrow)
{

#ifdef ENABLE_ALT_SQUELCH
	uint16_t Value;

	static const uint8_t SquelchGlitchOpenLevel[10] = {
		0x5A,
		0x10,
		0x9,
		0x8,
		0x7,
		0x6,
		0x5,
		0x4,
		0x3,
		0x2,
	};

	if (gSettings.Squelch == 0){
		Value = 255;
	} else {
		Value = SquelchGlitchOpenLevel[gSettings.Squelch];
	}

	BK4819_WriteRegister(0x4E, (BK4819_ReadRegister(0x4E) & 0xFF00) | Value);

	if (gSettings.Squelch == 0){
		Value = 255;
	} else {
		Value = (Value * 10) / 9;
	}

	BK4819_WriteRegister(0x4D, 0xA0 << 8 | Value);
#else

	static const uint8_t gSquelchGlitchLevel[11] = {
		0x20,
		0x1F,  // was 0x20 (same as level 0), now distinct
		0x1E,
		0x1C,
		0x1A,
		0x18,
		0x16,
		0x14,
		0x12,
		0x10,
		0x0E,
	};
	
	if (bIsNarrow) {
		BK4819_WriteRegister(0x4D, gSquelchGlitchLevel[gSettings.Squelch] + 0x9FFF);
		BK4819_WriteRegister(0x4E, gSquelchGlitchLevel[gSettings.Squelch] + 0x4DFE);
	} else {
		BK4819_WriteRegister(0x4D, gSquelchGlitchLevel[gSettings.Squelch] + 0xA000);
		BK4819_WriteRegister(0x4E, gSquelchGlitchLevel[gSettings.Squelch] + 0x4DFF);
	}
#endif

}

void BK4819_SetSquelchNoise(bool bIsNarrow)
{
#ifdef ENABLE_ALT_SQUELCH

	uint16_t Value;

	static const uint8_t SquelchNoiseOpenLevel[10] = {
		0xB4,
		0x3E,
		0x38,
		0x32,
		0x2C,
		0x26,
		0x20,
		0x1A,
		0x14,
		0x0E,
	};

	if (gSettings.Squelch == 0){
		Value = (127 << 8) | 127;
	} else {
		Value = SquelchNoiseOpenLevel[gSettings.Squelch];
		if (bIsNarrow) {
			uint8_t closeVal = (Value > 5) ? Value - 5 : 0;  // clamp to prevent underflow
			Value = ((((closeVal * 10) / 9) << 8) | Value);
		} else {
			Value = (((Value * 10) / 9) << 8) | Value;
		}
	}

	BK4819_WriteRegister(0x4F, Value);

#else

	static const uint8_t gSquelchNoiseLevel[11] = {
		0x0A,
		0x09,  // was 0x0A (same as level 0), now distinct
		0x09,
		0x08,
		0x07,
		0x06,
		0x05,
		0x04,
		0x03,
		0x02,
		0x01,
	};

	uint8_t Level;
	uint16_t Value;

	Level = gSquelchNoiseLevel[gSettings.Squelch];
	if (bIsNarrow) {
		Value = ((gSquelchNoiseNarrow + 12 + Level) << 8) | (gSquelchNoiseNarrow + 6 + Level);
	} else {
		Value = ((gSquelchNoiseWide   + 12 + Level) << 8) | (gSquelchNoiseWide   - 6 + Level);
	}

	BK4819_WriteRegister(0x4F, Value);

#endif

}

void BK4819_SetSquelchRSSI(bool bIsNarrow)
{
#ifdef ENABLE_ALT_SQUELCH

	uint16_t Value;

	static const uint8_t SquelchRssiOpenLevel[10] = {
		0x0A,
		0x66,
		0x6E,
		0x76,
		0x7E,
		0x86,
		0x8E,
		0x96,
		0x9E,
		0xA6,
	};

	if (gSettings.Squelch == 0){
		Value = 0;
	} else {
		Value = SquelchRssiOpenLevel[gSettings.Squelch];
		Value = (Value << 8) | (Value * 82) / 100;  // ~18% hysteresis gap (was 10%)
	}

	BK4819_WriteRegister(0x78, Value);
#else

	static const uint8_t gSquelchRssiLevel[11] = {
		0x00,
		0x00,
		0x02,
		0x04,
		0x06,
		0x0A,
		0x10,
		0x16,
		0x1C,
		0x22,
		0x26,
	};

	uint8_t Level;
	uint16_t Value;

	Level = gSquelchRssiLevel[gSettings.Squelch];
	if (bIsNarrow) {
		Value = ((gSquelchRSSINarrow - 8 + Level) << 8) | (gSquelchRSSINarrow - 14 + Level);
	} else {
		Value = ((gSquelchRSSIWide   - 8 + Level) << 8) | (gSquelchRSSIWide   - 14 + Level);
	}

	BK4819_WriteRegister(0x78, Value);

#endif

}

void BK4819_SetFilterBandwidth(bool bIsNarrow)
{
	// Check if modulation is FM
	if (gMainVfo->gModulationType == 0) { // if FM
#ifndef ENABLE_REGISTER_EDIT
		if (bIsNarrow) {
			BK4819_WriteRegister(0x43, 0x4048);// 0x4048 stock seems to work better on PCB 2.1, (OEFWCOM 0x1408)
		} else {
			BK4819_WriteRegister(0x43, 0x3028);// 0x4048 stock seems to work better on PCB 2.1, (OEFWCOM 0x1408)
		}
#else
		uint16_t Value = BK4819_ReadRegister(0x43); 
		if (bIsNarrow) {
			BK4819_WriteRegister(0x43, (Value & ~0x30) | 0);
		} else {
			BK4819_WriteRegister(0x43, (Value & ~0x30) | 32);
		}
#endif
	}
}

void BK4819_EnableFilter(bool bEnable)
{
	uint16_t Value;

	Value = BK4819_ReadRegister(0x33);
	Value |= 0
		| GPIO_FILTER_UHF
		| GPIO_FILTER_VHF
		| GPIO_FILTER_UNKWOWN;

	if (bEnable) {
		if (gCalibration.BandSelectionThreshold == 0xAAAA) {
			if (!gUseUhfFilter) {
				Value &= ~GPIO_FILTER_UNKWOWN;
			} else {
				Value &= ~GPIO_FILTER_VHF;
			}
		}
		else if (!gUseUhfFilter) {
			Value &= ~GPIO_FILTER_VHF;
		} else {
			Value &= ~GPIO_FILTER_UHF;
		}
	}
	BK4819_WriteRegister(0x33, Value);
}

void BK4819_EnableScramble(uint8_t Scramble)
{
	uint16_t Value;

	BK4819_WriteRegister(0x71, 0x68DC | (Scramble * 1032));

	Value = BK4819_ReadRegister(0x31);
	if (Scramble) {
		Value |= 2;
	} else {
		Value &= ~2;
	}
	BK4819_WriteRegister(0x31, Value);
}

void BK4819_EnableCompander(bool bEnable)
{
	BK4819_WriteRegister(0x31, BK4819_ReadRegister(0x31) & ~8U);
	if (bEnable) {
		BK4819_WriteRegister(0x28, gCalibration.AF_RX_Expander);
		BK4819_WriteRegister(0x29, gCalibration.AF_TX_Compress);
	} else {
		BK4819_WriteRegister(0x28, 0x0000);
		BK4819_WriteRegister(0x29, 0x0000);
	}
}

void BK4819_EnableVox(bool bEnable)
{
	uint16_t Value;

	Value = BK4819_ReadRegister(0x31);
	if (bEnable) {
		Value |= 4U;
	} else {
		Value &= ~4U;
	}
	BK4819_WriteRegister(0x31, Value);
}

void BK4819_RestoreGainSettings()
{
	// REG_13: LNA Short[9:8], LNA[7:5], Mixer[4:3], PGA[2:0]
	// REG_10: AGC gain table index (used when AGC is in fixed mode)
	// Default values tuned for VHF/UHF airband reception
	// 0x03BE matches am-fix original_index 90 (LNAS=3, LNA=5, MIX=3, PGA=6, -7dB)
	// so toggling AM fix on/off causes no abrupt gain jump
	BK4819_WriteRegister(0x13, 0x03BE);  // match am-fix original_index (-7dB)
	BK4819_WriteRegister(0x12, 0x037B);  // AGC gain table entry 2
	BK4819_WriteRegister(0x11, 0x027B);  // AGC gain table entry 1
	BK4819_WriteRegister(0x10, 0x007A);  // AGC gain table entry 0
	BK4819_WriteRegister(0x14, 0x0019);  // AGC speed
}	

void BK4819_ToggleAGCMode()
{
	// REG_7E[15] - AGC Mode
	// 1 - Fixed, 0 - Auto

	uint16_t Value = BK4819_ReadRegister(0x7E);
	uint16_t AGCIndex = (Value & 0x7000) >> 12; // Extract bits 14, 13 and 12

	if (!(Value & 0x8000U) || AGCIndex == 4){ //Bit 15 = 0 (AGC on) or 14:12 = 110 (index 4/min)
		Value ^= 0x8000U; //Toggle AGC
	} else {
		Value |= 0x8000U; //Turn AGC off
		AGCIndex = (AGCIndex + 7) % 8; //Decrement ACG Index
		Value =  (Value & 0x8FFFU) | (AGCIndex << 12); // Set bits 14:12 (AGC Index)
	}
	BK4819_WriteRegister(0x7E, Value);
}

void BK4819_SetToneFrequency(bool Tone2, uint16_t Tone)
{
	BK4819_WriteRegister(0x71 + Tone2, (Tone * 103U) / 10U);
}

void BK4819_EnableFFSK1200(bool bEnable)
{
	if (bEnable) {
		BK4819_WriteRegister(0x70, 0x00E0);
		BK4819_WriteRegister(0x72, 0x3065);
		BK4819_WriteRegister(0x58, 0x37C3);
		BK4819_WriteRegister(0x5C, 0x5665);
		BK4819_WriteRegister(0x5D, 0x0F00);
	} else {
		BK4819_WriteRegister(0x70, 0x0000);
		BK4819_WriteRegister(0x58, 0x0000);
	}
}

void BK4819_ResetFSK(void)
{
	BK4819_WriteRegister(0x3F, 0x0000);
	BK4819_WriteRegister(0x59, 0x0028);
	//BK4819_WriteRegister(0x30, 0x0000);
}

void BK4819_StartAudio(void)
{
	gpio_bits_set(GPIOA, BOARD_GPIOA_LED_GREEN);
	gRadioMode = RADIO_MODE_RX;
	OpenAudio(gMainVfo->bIsNarrow, gMainVfo->gModulationType);
	if (gMainVfo->gModulationType == 0) {
		BK4819_WriteRegister(0x4D, 0xA080);
		BK4819_WriteRegister(0x4E, 0x6F7C);
	}

	if (gMainVfo->gModulationType > 0) {
		// AM, SSB
		BK4819_EnableScramble(0);
		BK4819_EnableCompander(false);
		// Set bit 4 of register 73 (Auto Frequency Control Disable)
		uint16_t reg_73 = BK4819_ReadRegister(0x73);
		BK4819_WriteRegister(0x73, reg_73 | 0x10U);
		// BK4819_WriteRegister(0x43, 0b0100000001011000); // Filter 6.25KHz
		if (gMainVfo->gModulationType > 1) { // if SSB
			BK4819_WriteRegister(0x43, 0b0010000001011000); // Filter 6.25KHz
			BK4819_WriteRegister(0x37, 0b0001011000001111);
    		BK4819_WriteRegister(0x3D, 0b0010101101000101);
    		BK4819_WriteRegister(0x48, 0b0000001110101000);
		}
	} else {
		// FM
		BK4819_EnableScramble(gMainVfo->Scramble);
		BK4819_EnableCompander(true);
		// BK4819_WriteRegister(0x43, 0x3028); // restore filter just in case -
											// this gets overwritten by sane defaults anyway.
		// Unset bit 4 of register 73 (Auto Frequency Control Disable)
		uint16_t reg_73 = BK4819_ReadRegister(0x73);
		BK4819_WriteRegister(0x73, reg_73 & ~0x10U);
		if (gMainVfo->Scramble == 0) {
			BK4819_SetAFResponseCoefficients(false, true, gCalibration.RX_3000Hz_Coefficient);
		} else {
			BK4819_SetAFResponseCoefficients(false, true, 4);
		}
	}
	if (!gReceptionMode) {
		BK4819_EnableFFSK1200(true);
	}
	SPEAKER_TurnOn(SPEAKER_OWNER_RX);
}

void BK4819_SetAfGain(uint16_t Gain)
{
	if (gMainVfo->gModulationType) { // AM, SSB
		if ((Gain & 15) > 4) {
			Gain -= 4;
		}
		BK4819_WriteRegister(0x48, Gain);
	} else { // FM
		switch (BK4819_ReadRegister(0x01)) {
		case 0: case 4:
			BK4819_WriteRegister(0x48, Gain);
			break;
		case 2: case 3: case 6: case 7:
			if ((Gain & 15) > 3) {
				Gain -= 3;
			}
			BK4819_WriteRegister(0x48, Gain);
			break;
		default:
			BK4819_WriteRegister(0x48, Gain);
			break;
		}
	}
}

bool BK4819_CheckSquelchLink(void)
{
	if (gSettings.Squelch && !gMonitorMode) {
		return ((BK4819_ReadRegister(0x0C) >> 1) & 1);
	}

	return true;
}

void BK4819_EnableTone1(bool bEnable)
{
	uint16_t Value;

	Value = BK4819_ReadRegister(0x70);
	Value = (Value & ~0x8000U) | (bEnable << 15);
	BK4819_WriteRegister(0x70, 0x4000 | Value);

	if (bEnable) {
		if (gRadioMode != RADIO_MODE_TX) {
			BK4819_WriteRegister(0x30, 0x0302);
			BK4819_SetAF(BK4819_AF_ALAM);
		} else {
			BK4819_WriteRegister(0x30, 0xC3FA);
			BK4819_SetAF(BK4819_AF_BEEP);
		}
	} else {
		if (gRadioMode == RADIO_MODE_TX) {
			BK4819_WriteRegister(0x30, 0xC1FE);
		} else {
			BK4819_WriteRegister(0x30, 0xBFF1);
		}
		if (gRadioMode != RADIO_MODE_RX) {
			BK4819_SetAF(BK4819_AF_MUTE);
		} else {
			OpenAudio(gMainVfo->bIsNarrow, gMainVfo->gModulationType);
			if (gMainVfo->gModulationType > 0) {
				BK4819_EnableScramble(0); // AM, SSB
			} else {
				BK4819_EnableScramble(gMainVfo->Scramble); // FM
			}
		}
	}
}

void BK4819_GenTail(bool bIsNarrow)
{
	if (gSettings.TailTone) {
		if (bIsNarrow) {
			BK4819_WriteRegister(0x51, gFrequencyBandInfo.CtcssTxGainNarrow | 0x9000);
		} else {
			BK4819_WriteRegister(0x51, gFrequencyBandInfo.CtcssTxGainWide | 0x9000);
		}
		if (gTxCodeType == CODE_TYPE_OFF || gTxCodeType == CODE_TYPE_CTCSS) {
			BK4819_WriteRegister(0x07, 1135);
		} else {
			BK4819_WriteRegister(0x52, 0x823F);
		}
		DELAY_WaitMS(250);
	}
}

void BK4819_SetupPowerAmplifier(uint8_t level, uint32_t frequency)
{
	REGx36 reg;
	reg._res6 = 0;

	// PMR446: 446.00625-446.19375 MHz (10 Hz units).
	// ERC/REC 70-03 annex 8 — fixed 0.5 W ERP max, no user power control.
	// Check frequency FIRST before anything else so high power setting
	// cannot bypass this — it is a legal requirement not a user preference.
#define PMR446_FREQ_LOW   44599000U   // extended low to catch 446.000 MHz
#define PMR446_FREQ_HIGH  44619375U
	const bool bPMR446 = (frequency >= PMR446_FREQ_LOW && frequency <= PMR446_FREQ_HIGH);

	if (level == 0) {
		// PA fully off - used by RADIO_EndTX()
		reg.PA_Bias   = 0;
		reg.PA_Gain1  = 0;
		reg.PA_Gain2  = 0;
		reg.PA_Enable = 0;
		BK4819_WriteRegister(0x36, reg.val);
		return;
	}

	// ----------------------------------------------------------------
	// HIGH POWER: maximum gains, flash calibration bias.
	// PMR446 is explicitly excluded and falls through to low power path.
	// ----------------------------------------------------------------
	if (!gMainVfo->bIsLowPower && !bPMR446) {
		reg.PA_Gain1  = 7;
		reg.PA_Gain2  = 7;
		reg.PA_Enable = 1;
		reg.PA_Bias   = level;   // gTxPowerLevelHigh from flash
		BK4819_WriteRegister(0x36, reg.val);
		return;
	}

	// ----------------------------------------------------------------
	// LOW POWER: reduced gain stages + fixed per-band bias values.
	// gTxPowerLevelLow from flash was calibrated for OEM max-gain
	// settings and is not usable here.  These bias values are starting
	// points — measure with a wattmeter and adjust to taste.
	//
	// Target: ~0.5 W on all bands.
	//
	// Band boundaries match FREQUENCY_SelectBand() in frequencies.c
	// (all values in 10 Hz units).
	// ----------------------------------------------------------------

	// PMR446 bias — tuned from wattmeter. 0x10 gave 0.57 W; 0x0E targets 0.5 W.
#define PMR446_PA_BIAS    0x14U   // 0x0E gave 0.14 W on Ch1; raised toward 0.5 W

	// VHF: 136-280 MHz
#define VHF_LOW_PA_GAIN1  1
#define VHF_LOW_PA_GAIN2  2
#define VHF_LOW_PA_BIAS   0x16U   // 0x18 gave 0.78 W, 0x10 gave 0 W; midpoint toward 0.5 W

	// UHF: 280-560 MHz (covers 70cm and PMR446)
#define UHF_LOW_PA_GAIN1  2
#define UHF_LOW_PA_GAIN2  4
#define UHF_LOW_PA_BIAS   0x12U   // 0x18 gave 0.67 W, 0x12 gave 0.46 W — close to target

	if (frequency >= PMR446_FREQ_LOW && frequency <= PMR446_FREQ_HIGH) {
		// PMR446 — legal hard clamp, same gain as UHF low
		reg.PA_Gain1 = UHF_LOW_PA_GAIN1;
		reg.PA_Gain2 = UHF_LOW_PA_GAIN2;
		reg.PA_Bias  = PMR446_PA_BIAS;
	} else if (frequency >= 13600000U && frequency < 28000000U) {
		// VHF 136-174 MHz and 174-240 MHz bands
		reg.PA_Gain1 = VHF_LOW_PA_GAIN1;
		reg.PA_Gain2 = VHF_LOW_PA_GAIN2;
		reg.PA_Bias  = VHF_LOW_PA_BIAS;
	} else {
		// UHF 240-560 MHz bands
		reg.PA_Gain1 = UHF_LOW_PA_GAIN1;
		reg.PA_Gain2 = UHF_LOW_PA_GAIN2;
		reg.PA_Bias  = UHF_LOW_PA_BIAS;
	}

	reg.PA_Enable = 1;
	BK4819_WriteRegister(0x36, reg.val);
}

void BK4819_EnableRfTxDeviation(void)
{
	uint16_t Deviation;

	Deviation = gMainVfo->bIsNarrow ? gFrequencyBandInfo.TxDeviationNarrow : gFrequencyBandInfo.TxDeviationWide;
	if (gMainVfo->Scramble) {
		Deviation -= 200;
	}
	BK4819_WriteRegister(0x40, Deviation);
}

void BK4819_SetMicSensitivityTuning(void)
{
	BK4819_WriteRegister(0x7D, 0xE940 | (gExtendedSettings.MicGainLevel & 0x1F));
}

void BK4819_EnableTX(bool bUseMic)
{
	BK4819_WriteRegister(0x37, 0x1D0F);
	DELAY_WaitMS(10);
	BK4819_WriteRegister(0x52, 0x028F);
	BK4819_WriteRegister(0x30, 0x0200);
	if (bUseMic) {
		BK4819_WriteRegister(0x30, 0xC1FE);
	} else {
		BK4819_WriteRegister(0x30, 0xC3FA);
	}
}

void BK4819_StartFrequencyScan(void)
{
	BK4819_WriteRegister(0x32, 0x0B01);
	DELAY_WaitMS(200);
}

void BK4819_StopFrequencyScan(void)
{
	BK4819_WriteRegister(0x32, 0x0000);
}

void BK4819_DisableAutoCssBW(void)
{
	BK4819_WriteRegister(0x51, 0x0300);
	DELAY_WaitMS(200);
	BK4819_EnableRX();
}

#ifdef ENABLE_SPECTRUM
void BK4819_set_rf_frequency(const uint32_t frequency, const bool trigger_update)
{
	BK4819_WriteRegister(0x38, (frequency >> 0) & 0xFFFF);
	BK4819_WriteRegister(0x39, (frequency >> 16) & 0xFFFF);

	if (trigger_update)
	{ // trigger a PLL/VCO update
		const uint16_t reg = BK4819_ReadRegister(0x30);
		BK4819_WriteRegister(0x30, reg & ~BK4819_REG_30_ENABLE_VCO_CALIB);
		BK4819_WriteRegister(0x30, reg);
	}
}
#endif
