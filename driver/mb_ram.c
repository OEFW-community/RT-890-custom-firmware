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

// FLASH->ctrl
#define MB_FLASH_FPRGM	(1U << 0)
#define MB_FLASH_SECERS	(1U << 1)
#define MB_FLASH_ERSTR	(1U << 6)
#define MB_FLASH_OPLK	(1U << 7)
// FLASH->sts (error and done flags are cleared by writing 1)
#define MB_FLASH_OBF	(1U << 0)
#define MB_FLASH_PRGMERR	(1U << 2)
#define MB_FLASH_EPPERR	(1U << 4)
#define MB_FLASH_ODF	(1U << 5)
#define MB_FLASH_FLAGS	(MB_FLASH_PRGMERR | MB_FLASH_EPPERR | MB_FLASH_ODF)

// Busy wait bound, far above the duration of a 1 KB sector erase.
#define MB_FLASH_TIMEOUT	0x01000000U
#define MB_SECTOR_RETRIES	3U
#define MB_SECTOR_WORDS		(MB_INT_SECTOR_SIZE / 4U)

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

// Waits for the end of a flash operation. Returns false on timeout or on a
// programming / protection error. Clears the status flags.
MB_RAM static bool FlashWait(void)
{
	uint32_t Timeout = MB_FLASH_TIMEOUT;
	uint32_t Status;

	while (FLASH->sts & MB_FLASH_OBF) {
		if (--Timeout == 0) {
			return false;
		}
	}
	Status = FLASH->sts;
	FLASH->sts = MB_FLASH_FLAGS;

	return !(Status & (MB_FLASH_PRGMERR | MB_FLASH_EPPERR));
}

MB_RAM static bool FlashEraseSector(uint32_t Address)
{
	bool bOk;

	FLASH->ctrl |= MB_FLASH_SECERS;
	FLASH->addr = Address;
	FLASH->ctrl |= MB_FLASH_ERSTR;
	bOk = FlashWait();
	FLASH->ctrl &= ~MB_FLASH_SECERS;

	return bOk;
}

MB_RAM static bool FlashProgramWord(uint32_t Address, uint32_t Data)
{
	bool bOk;

	FLASH->ctrl |= MB_FLASH_FPRGM;
	*(volatile uint32_t *)Address = Data;
	bOk = FlashWait();
	FLASH->ctrl &= ~MB_FLASH_FPRGM;

	return bOk;
}

MB_RAM __attribute__((noreturn)) static void SystemReset(void)
{
	__DSB();
	SCB->AIRCR = (0x5FAUL << SCB_AIRCR_VECTKEY_Pos) | SCB_AIRCR_SYSRESETREQ_Msk;
	__DSB();
	while (1) {
	}
}

// Erases, programs and verifies one 1 KB sector. Never touches anything
// outside of the application area.
MB_RAM static bool WriteSector(uint32_t Address, const uint32_t *pData)
{
	volatile const uint32_t *pFlash = (volatile const uint32_t *)Address;
	uint32_t i;

	if (Address < MB_INT_APP_BASE || Address > MB_INT_APP_BASE + MB_INT_APP_SIZE - MB_INT_SECTOR_SIZE) {
		return false;
	}
	if (!FlashEraseSector(Address)) {
		return false;
	}
	for (i = 0; i < MB_SECTOR_WORDS; i++) {
		// Erased words read as 0xFFFFFFFF: nothing to program.
		if (pData[i] != 0xFFFFFFFFU && !FlashProgramWord(Address + (i * 4U), pData[i])) {
			return false;
		}
	}
	for (i = 0; i < MB_SECTOR_WORDS; i++) {
		if (pFlash[i] != pData[i]) {
			return false;
		}
	}

	return true;
}

MB_RAM void MB_RamRestore(uint32_t SpiAddress, uint32_t ImageSize, uint32_t *pBuffer)
{
	uint32_t Offset;
	uint32_t Retry;
	uint32_t Size;
	uint32_t i;

	__disable_irq();

	if (FLASH->ctrl & MB_FLASH_OPLK) {
		FLASH->unlock = MB_FLASH_KEY1;
		FLASH->unlock = MB_FLASH_KEY2;
	}
	FLASH->sts = MB_FLASH_FLAGS;

	for (Offset = 0; Offset < MB_INT_APP_SIZE; Offset += MB_INT_SECTOR_SIZE) {
		// Only CRC-validated image bytes are read; the rest of the area is
		// erased (0xFF), never filled with leftovers of the slot.
		for (i = 0; i < MB_SECTOR_WORDS; i++) {
			((volatile uint32_t *)pBuffer)[i] = 0xFFFFFFFFU;
		}
		Size = 0;
		if (Offset < ImageSize) {
			Size = ImageSize - Offset;
			if (Size > MB_INT_SECTOR_SIZE) {
				Size = MB_INT_SECTOR_SIZE;
			}
		}
		SpiRead(SpiAddress + Offset, (uint8_t *)pBuffer, Size);
		for (Retry = 0; Retry < MB_SECTOR_RETRIES; Retry++) {
			if (WriteSector(MB_INT_APP_BASE + Offset, pBuffer)) {
				break;
			}
		}
		if (Retry == MB_SECTOR_RETRIES) {
			// Returning to flash-resident code is impossible once the
			// application has been erased: reset. The factory bootloader
			// (side keys 1 + 2 at power on) is the recovery path.
			break;
		}
		// Progress: toggle the green LED once per sector.
		if (GPIOA->odt & BOARD_GPIOA_LED_GREEN) {
			GPIOA->clr = BOARD_GPIOA_LED_GREEN;
		} else {
			GPIOA->scr = BOARD_GPIOA_LED_GREEN;
		}
	}

	FLASH->ctrl &= ~(MB_FLASH_FPRGM | MB_FLASH_SECERS);
	FLASH->ctrl |= MB_FLASH_OPLK;
	SystemReset();
}
