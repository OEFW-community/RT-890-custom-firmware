/* Copyright 2026 reppad
 * https://github.com/reppad
 *
 * Inspired by the multiboot of the UV-K1/UV-K5 V3 custom firmware,
 * Copyright 2026 Armel F4HWN
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
#include "driver/battery.h"
#include "driver/mb_flash.h"
#include "driver/serial-flash.h"
#include "misc.h"
#include "radio/hardware.h"
#include "radio/settings.h"

// Linker symbols (firmware.ld): load image in flash and execution range,
// which aliases gFlashBuffer.
extern const uint8_t __mb_ramfunc_load_start[];
extern uint8_t __mb_ramfunc_start[];
extern uint8_t __mb_ramfunc_end[];

uint32_t MB_Crc32(uint32_t Crc, const uint8_t *pData, uint32_t Size)
{
	uint8_t i;

	Crc = ~Crc;
	while (Size--) {
		Crc ^= *pData++;
		for (i = 0; i < 8; i++) {
			Crc = (Crc >> 1) ^ (0xEDB88320U & -(Crc & 1U));
		}
	}

	return ~Crc;
}

uint32_t MB_InternalCrc32(void)
{
	return MB_Crc32(0, (const uint8_t *)MB_INT_APP_BASE, MB_INT_APP_SIZE);
}

uint32_t MB_SpiCrc32(uint32_t Address, uint32_t Size)
{
	uint8_t Buffer[256];
	uint32_t Crc = 0;
	uint32_t Chunk;

	while (Size) {
		Chunk = Size < sizeof(Buffer) ? Size : sizeof(Buffer);
		SFLASH_Read(Buffer, Address, Chunk);
		Crc = MB_Crc32(Crc, Buffer, Chunk);
		Address += Chunk;
		Size -= Chunk;
	}

	return Crc;
}

bool MB_LoadRamCode(void)
{
	const volatile uint8_t *pSrc = __mb_ramfunc_load_start;
	volatile uint8_t *pDst = __mb_ramfunc_start;

	// Volatile byte copies: no call to a flash-resident memcpy.
	while (pDst < __mb_ramfunc_end) {
		*pDst++ = *pSrc++;
	}
	__DSB();
	__ISB();

	pSrc = __mb_ramfunc_load_start;
	pDst = __mb_ramfunc_start;
	while (pDst < __mb_ramfunc_end) {
		if (*pDst++ != *pSrc++) {
			return false;
		}
	}

	return true;
}

bool MB_RunProbe(MB_Probe_t *pProbe, uint32_t SpiAddress)
{
	// Called through a volatile pointer: the RAM code is out of BL range
	// and must not be reached through a linker veneer.
	void (*volatile pProbeFn)(MB_Probe_t *, uint32_t) = MB_RamProbe;

	if (!MB_LoadRamCode()) {
		return false;
	}
	pProbeFn(pProbe, SpiAddress);

	return true;
}

bool MB_BackupInternalToSlot0(void)
{
	const uint8_t *pImage = (const uint8_t *)MB_INT_APP_BASE;
	uint8_t Buffer[256];
	uint32_t Offset;
	uint32_t i;

	HARDWARE_EnableInterrupts(false);
	for (i = 0; i < MB_SLOT_STRIDE / 0x1000U; i++) {
		SFLASH_Erase((MB_SLOT_BASE(0) >> 12) + i);
	}
	SFLASH_Write(pImage, MB_SLOT_BASE(0) + MB_SLOT_IMG_OFFSET, MB_INT_APP_SIZE);
	HARDWARE_EnableInterrupts(true);

	for (Offset = 0; Offset < MB_INT_APP_SIZE; Offset += sizeof(Buffer)) {
		SFLASH_Read(Buffer, MB_SLOT_BASE(0) + MB_SLOT_IMG_OFFSET + Offset, sizeof(Buffer));
		for (i = 0; i < sizeof(Buffer); i++) {
			if (Buffer[i] != pImage[Offset + i]) {
				return false;
			}
		}
	}

	return true;
}

uint8_t MB_RestoreSlot0(void)
{
	void (*volatile pRestoreFn)(uint32_t, uint32_t *) = MB_RamRestore;
	const uint32_t Image = MB_SLOT_BASE(0) + MB_SLOT_IMG_OFFSET;
	uint32_t Vectors[2];

	if (MB_SpiCrc32(Image, MB_INT_APP_SIZE) != MB_InternalCrc32()) {
		return MB_ERR_CRC;
	}

	// Same checks as the factory bootloader before it jumps to the
	// application, plus a reset vector inside the application area.
	SFLASH_Read(Vectors, Image, sizeof(Vectors));
	if (Vectors[0] - 0x20000000U >= 0x4000U ||
		!(Vectors[1] & 1U) ||
		Vectors[1] < MB_INT_APP_BASE ||
		Vectors[1] >= MB_INT_APP_BASE + MB_INT_APP_SIZE) {
		return MB_ERR_IMAGE;
	}

	gBatteryVoltage = BATTERY_GetVoltage();
	if (gBatteryVoltage <= gCalibration.BatteryCalibration[2]) {
		return MB_ERR_BATTERY;
	}

	if (!MB_LoadRamCode()) {
		return MB_ERR_RAM_LOAD;
	}
	pRestoreFn(Image, (uint32_t *)(gFlashBuffer + MB_RAM_BUFFER_OFFSET));

	// Not reached: the RAM routine always resets the MCU.
	while (1) {
	}
}
