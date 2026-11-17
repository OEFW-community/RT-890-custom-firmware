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

// CRC-32 (zlib) update, without the initial and final inversions.
MB_RAM static uint32_t Crc32Update(uint32_t Crc, const volatile uint8_t *pData, uint32_t Size)
{
	uint32_t i;

	while (Size--) {
		Crc ^= *pData++;
		for (i = 0; i < 8; i++) {
			Crc = (Crc >> 1) ^ (0xEDB88320U & -(Crc & 1U));
		}
	}

	return Crc;
}

// Fills pBuffer with the image bytes of the sector at Offset. Only image
// bytes are read; the rest of the area stays erased (0xFF), never filled with
// leftovers of the slot.
MB_RAM static void LoadSector(uint32_t SpiAddress, uint32_t Offset, uint32_t ImageSize, uint32_t *pBuffer)
{
	uint32_t Size = 0;
	uint32_t i;

	for (i = 0; i < MB_SECTOR_WORDS; i++) {
		((volatile uint32_t *)pBuffer)[i] = 0xFFFFFFFFU;
	}
	if (Offset < ImageSize) {
		Size = ImageSize - Offset;
		if (Size > MB_INT_SECTOR_SIZE) {
			Size = MB_INT_SECTOR_SIZE;
		}
	}
	SpiRead(SpiAddress + Offset, (uint8_t *)pBuffer, Size);
}

MB_RAM static bool ProgramSector(uint32_t Address, const uint32_t *pData)
{
	uint32_t Retry;

	for (Retry = 0; Retry < MB_SECTOR_RETRIES; Retry++) {
		if (WriteSector(Address, pData)) {
			return true;
		}
	}

	return false;
}

MB_RAM void MB_RamRestore(uint32_t SpiAddress, uint32_t ImageSize, uint32_t ImageCrc32, uint32_t *pBuffer)
{
	uint32_t Offset;
	uint32_t Crc;
	uint32_t Retry;

	__disable_irq();

	if (FLASH->ctrl & MB_FLASH_OPLK) {
		FLASH->unlock = MB_FLASH_KEY1;
		FLASH->unlock = MB_FLASH_KEY2;
	}
	FLASH->sts = MB_FLASH_FLAGS;

	// 1. The first sector (vector table) is erased first and programmed
	// last: until the whole image has been written and checked, the initial
	// stack pointer reads 0xFFFFFFFF and the factory bootloader never jumps
	// to a partial image. An interrupted or failed restore therefore leaves a
	// radio that only starts in bootloader mode (side keys 1 + 2), and a
	// partial image can never run and be mistaken for a new Main.
	for (Retry = 0; Retry < MB_SECTOR_RETRIES && !FlashEraseSector(MB_INT_APP_BASE); Retry++) {
	}
	if (Retry == MB_SECTOR_RETRIES) {
		goto Done;
	}

	// 2. All the other sectors.
	for (Offset = MB_INT_SECTOR_SIZE; Offset < MB_INT_APP_SIZE; Offset += MB_INT_SECTOR_SIZE) {
		LoadSector(SpiAddress, Offset, ImageSize, pBuffer);
		if (!ProgramSector(MB_INT_APP_BASE + Offset, pBuffer)) {
			// Returning to flash-resident code is impossible: reset.
			goto Done;
		}
		// Progress: toggle the green LED once per sector.
		if (GPIOA->odt & BOARD_GPIOA_LED_GREEN) {
			GPIOA->clr = BOARD_GPIOA_LED_GREEN;
		} else {
			GPIOA->scr = BOARD_GPIOA_LED_GREEN;
		}
	}

	// 3. CRC of the whole image as it will run: first sector from the SPI
	// flash, the rest from the programmed internal flash. Catches any SPI read
	// error during the copy and any change of the slot since its validation.
	LoadSector(SpiAddress, 0, ImageSize, pBuffer);
	if (ImageSize <= MB_INT_SECTOR_SIZE) {
		Crc = Crc32Update(0xFFFFFFFFU, (const uint8_t *)pBuffer, ImageSize);
	} else {
		Crc = Crc32Update(0xFFFFFFFFU, (const uint8_t *)pBuffer, MB_INT_SECTOR_SIZE);
		Crc = Crc32Update(Crc, (const volatile uint8_t *)(MB_INT_APP_BASE + MB_INT_SECTOR_SIZE), ImageSize - MB_INT_SECTOR_SIZE);
	}

	// 4. Only a checked image becomes bootable.
	if (~Crc == ImageCrc32) {
		ProgramSector(MB_INT_APP_BASE, pBuffer);
	}

Done:
	FLASH->ctrl &= ~(MB_FLASH_FPRGM | MB_FLASH_SECERS);
	FLASH->ctrl |= MB_FLASH_OPLK;
	SystemReset();
}
