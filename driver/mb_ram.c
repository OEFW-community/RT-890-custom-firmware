/* Copyright 2026 reppad
 * https://github.com/reppad
 *
 * The RAM-resident restore approach (code overlaid on a RAM buffer, raw
 * register access only, post-link isolation check) follows the multiboot of
 * the UV-K1/UV-K5 V3 custom firmware, Copyright 2026 Armel F4HWN
 * (https://github.com/armel/uv-k1-k5v3-firmware-custom, Apache License 2.0).
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
#include "driver/mb_flash.h"
#include "driver/mb_ram.h"
#include "driver/pins.h"

// Everything here lives in .mb_ramfunc. Helpers are static so that calls
// between them are resolved by the assembler (no relocation, no veneer).
#define MB_RAM		__attribute__((section(".mb_ramfunc"), noinline, used))

#define MB_FLASH_KEY1	0x45670123U
#define MB_FLASH_KEY2	0xCDEF89ABU
#define MB_FLASH_OPLK	(1U << 7)

// SPI flash pins (bit-banged). The board names are from the flash point of
// view inverted: SF_MISO (PB3) is the MCU output, SF_MOSI (PA7) the input.
#define MB_SF_CS	BOARD_GPIOB_SF_CS
#define MB_SF_CLK	BOARD_GPIOB_SF_CLK
#define MB_SF_OUT	BOARD_GPIOB_SF_MISO
#define MB_SF_IN	BOARD_GPIOA_SF_MOSI

MB_RAM static uint8_t SpiTransfer(uint8_t Output)
{
	uint8_t Input = 0;
	uint8_t i;

	for (i = 0; i < 8; i++) {
		GPIOB->clr = MB_SF_CLK;
		if (Output & 0x80U) {
			GPIOB->scr = MB_SF_OUT;
		} else {
			GPIOB->clr = MB_SF_OUT;
		}
		Output <<= 1;
		__NOP();
		__NOP();
		GPIOB->scr = MB_SF_CLK;
		__NOP();
		__NOP();
		Input <<= 1;
		if (GPIOA->idt & MB_SF_IN) {
			Input |= 1U;
		}
	}
	GPIOB->clr = MB_SF_CLK;

	return Input;
}

MB_RAM static void SpiRead(uint32_t Address, uint8_t *pBuffer, uint32_t Size)
{
	uint32_t i;

	GPIOB->clr = MB_SF_CS;
	SpiTransfer(0x03);
	SpiTransfer((Address >> 16) & 0xFFU);
	SpiTransfer((Address >>  8) & 0xFFU);
	SpiTransfer((Address >>  0) & 0xFFU);
	for (i = 0; i < Size; i++) {
		pBuffer[i] = SpiTransfer(0xFF);
	}
	GPIOB->scr = MB_SF_CS;
}

MB_RAM void MB_RamProbe(MB_Probe_t *pProbe, uint32_t SpiAddress)
{
	uint32_t PriMask = __get_PRIMASK();

	__disable_irq();

	pProbe->IntFirst = *(const volatile uint32_t *)MB_INT_APP_BASE;
	pProbe->IntLast = *(const volatile uint32_t *)(MB_INT_APP_BASE + MB_INT_APP_SIZE - 4U);

	pProbe->CtrlBefore = FLASH->ctrl;
	if (FLASH->ctrl & MB_FLASH_OPLK) {
		FLASH->unlock = MB_FLASH_KEY1;
		FLASH->unlock = MB_FLASH_KEY2;
	}
	pProbe->CtrlUnlocked = FLASH->ctrl;
	FLASH->ctrl |= MB_FLASH_OPLK;
	pProbe->CtrlLocked = FLASH->ctrl;

	SpiRead(SpiAddress, pProbe->Spi, sizeof(pProbe->Spi));

	__set_PRIMASK(PriMask);
}

