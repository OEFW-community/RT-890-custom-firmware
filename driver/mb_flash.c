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

#include <stddef.h>
#include <at32f421.h>
#include "driver/battery.h"
#include "driver/mb_flash.h"
#include "driver/pins.h"
#include "driver/serial-flash.h"
#include "misc.h"
#include "radio/hardware.h"
#include "radio/settings.h"

// Linker symbols (firmware.ld): load image in flash and execution range,
// which aliases gFlashBuffer.
extern const uint8_t __mb_ramfunc_load_start[];
extern uint8_t __mb_ramfunc_start[];
extern uint8_t __mb_ramfunc_end[];

_Static_assert(sizeof(MB_SlotHeader_t) == 64, "slot header must be 64 bytes");
_Static_assert(sizeof(MB_State_t) == 24, "state record must be 24 bytes");

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

uint32_t MB_InternalCrc32(uint32_t Size)
{
	return MB_Crc32(0, (const uint8_t *)MB_INT_APP_BASE, Size);
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

static bool LoadRamCode(void)
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

uint8_t MB_ReadSlotHeader(uint8_t Slot, MB_SlotHeader_t *pHeader)
{
	if (Slot >= MB_SLOT_COUNT) {
		return MB_ERR_SLOT;
	}
	SFLASH_Read(pHeader, MB_SLOT_BASE(Slot), sizeof(*pHeader));
	if (pHeader->Magic != MB_SLOT_MAGIC) {
		return MB_ERR_MAGIC;
	}
	if (pHeader->HdrVersion != MB_HDR_VERSION) {
		return MB_ERR_VERSION;
	}
	if (!(pHeader->Flags & MB_FLAG_COMMITTED)) {
		return MB_ERR_NOT_COMMITTED;
	}
	// At least the vector table, at most the application area.
	if (pHeader->ImageSize < 8U || pHeader->ImageSize > MB_INT_APP_SIZE || (pHeader->ImageSize & 3U)) {
		return MB_ERR_SIZE;
	}

	return MB_OK;
}

uint8_t MB_ValidateSlot(uint8_t Slot, MB_SlotHeader_t *pHeader, uint32_t *pCrc)
{
	uint32_t Vectors[2];
	uint32_t Crc;
	uint8_t Error;

	Error = MB_ReadSlotHeader(Slot, pHeader);
	if (Error != MB_OK) {
		return Error;
	}
	Crc = MB_SpiCrc32(MB_SLOT_IMAGE(Slot), pHeader->ImageSize);
	if (pCrc) {
		*pCrc = Crc;
	}
	if (Crc != pHeader->ImageCrc32) {
		return MB_ERR_CRC;
	}

	// Same checks as the factory bootloader before it jumps to the
	// application, plus a reset vector inside the image.
	SFLASH_Read(Vectors, MB_SLOT_IMAGE(Slot), sizeof(Vectors));
	if (Vectors[0] - 0x20000000U >= 0x4000U ||
		!(Vectors[1] & 1U) ||
		Vectors[1] < MB_INT_APP_BASE ||
		Vectors[1] >= MB_INT_APP_BASE + pHeader->ImageSize) {
		return MB_ERR_IMAGE;
	}

	return MB_OK;
}

uint8_t MB_EraseSlot(uint8_t Slot)
{
	uint32_t i;

	if (Slot >= MB_SLOT_COUNT) {
		return MB_ERR_SLOT;
	}
	HARDWARE_EnableInterrupts(false);
	for (i = 0; i < MB_SLOT_STRIDE / 0x1000U; i++) {
		SFLASH_Erase((MB_SLOT_BASE(Slot) >> 12) + i);
	}
	HARDWARE_EnableInterrupts(true);

	return MB_OK;
}

// Copies the running firmware (whole application area) into slot 0, checks
// it, then writes its committed header (name "Main", version = git hash).
static uint8_t BackupInternalToSlot0(uint32_t Crc)
{
	MB_SlotHeader_t Header;
	uint8_t i;

	MB_EraseSlot(0);

	HARDWARE_EnableInterrupts(false);
	SFLASH_Write((const void *)MB_INT_APP_BASE, MB_SLOT_IMAGE(0), MB_INT_APP_SIZE);
	HARDWARE_EnableInterrupts(true);

	if (MB_SpiCrc32(MB_SLOT_IMAGE(0), MB_INT_APP_SIZE) != Crc) {
		return MB_ERR_VERIFY;
	}

	// Committed header last.
	for (i = 0; i < sizeof(Header); i++) {
		((uint8_t *)&Header)[i] = (i >= offsetof(MB_SlotHeader_t, Name)) ? 0 : 0xFF;
	}
	Header.Magic = MB_SLOT_MAGIC;
	Header.HdrVersion = MB_HDR_VERSION;
	Header.Flags = MB_FLAG_COMMITTED;
	Header.ImageSize = MB_INT_APP_SIZE;
	Header.ImageCrc32 = Crc;
	Header.Name[0] = 'M';
	Header.Name[1] = 'a';
	Header.Name[2] = 'i';
	Header.Name[3] = 'n';
	for (i = 0; i < sizeof(GIT_HASH) - 1 && i < MB_VERSION_LEN; i++) {
		Header.Version[i] = GIT_HASH[i];
	}
	HARDWARE_EnableInterrupts(false);
	SFLASH_Write(&Header, MB_SLOT_BASE(0), sizeof(Header));
	HARDWARE_EnableInterrupts(true);

	return MB_ValidateSlot(0, &Header, NULL) == MB_OK ? MB_OK : MB_ERR_VERIFY;
}

static uint32_t StateCrc(const MB_State_t *pState)
{
	return MB_Crc32(0, (const uint8_t *)pState, offsetof(MB_State_t, StateCrc32));
}

static uint8_t ReadStateCopy(uint32_t Address, MB_State_t *pState)
{
	uint8_t i;

	SFLASH_Read(pState, Address, sizeof(*pState));
	for (i = 0; i < sizeof(*pState) && ((const uint8_t *)pState)[i] == 0xFF; i++) {
	}
	if (i == sizeof(*pState)) {
		return MB_STATE_MISSING;
	}
	if (pState->Magic != MB_STATE_MAGIC ||
		pState->StateCrc32 != StateCrc(pState) ||
		(uint8_t)~pState->Slot != pState->SlotInv ||
		(uint8_t)~pState->Bank != pState->BankInv ||
		pState->Slot >= MB_SLOT_COUNT ||
		pState->ImageSize > MB_INT_APP_SIZE) {
		return MB_STATE_CORRUPT;
	}

	return MB_STATE_VALID;
}

// Returns MB_STATE_*, the newest valid record and the address of the sector
// to use for the next record.
static uint8_t ReadState(MB_State_t *pState, uint32_t *pNext)
{
	MB_State_t B;
	uint8_t StatusA;
	uint8_t StatusB;

	StatusA = ReadStateCopy(MB_STATE_A, pState);
	StatusB = ReadStateCopy(MB_STATE_B, &B);
	if (StatusB == MB_STATE_VALID && (StatusA != MB_STATE_VALID || (int32_t)(B.Generation - pState->Generation) > 0)) {
		*pState = B;
		*pNext = MB_STATE_A;
		return MB_STATE_VALID;
	}
	*pNext = (StatusA == MB_STATE_VALID) ? MB_STATE_B : MB_STATE_A;
	if (StatusA == MB_STATE_VALID) {
		return MB_STATE_VALID;
	}

	return (StatusA == MB_STATE_MISSING && StatusB == MB_STATE_MISSING) ? MB_STATE_MISSING : MB_STATE_CORRUPT;
}

uint8_t MB_ReadState(MB_State_t *pState)
{
	uint32_t Next;

	return ReadState(pState, &Next);
}

// Writes a new active state record in the sector that does not hold the
// newest valid one and reads it back. The previous record stays intact.
static uint8_t CommitState(uint8_t Slot, uint8_t Bank, uint32_t ImageSize, uint32_t ImageCrc32)
{
	MB_State_t State;
	MB_State_t Check;
	uint32_t Next;
	uint32_t Generation;

	Generation = (ReadState(&State, &Next) == MB_STATE_VALID) ? State.Generation + 1 : 1;
	State.Magic = MB_STATE_MAGIC;
	State.Generation = Generation;
	State.ImageSize = ImageSize;
	State.ImageCrc32 = ImageCrc32;
	State.Slot = Slot;
	State.SlotInv = ~Slot;
	State.Bank = Bank;
	State.BankInv = ~Bank;
	State.StateCrc32 = StateCrc(&State);

	HARDWARE_EnableInterrupts(false);
	SFLASH_Erase(Next >> 12);
	SFLASH_Write(&State, Next, sizeof(State));
	HARDWARE_EnableInterrupts(true);

	if (ReadStateCopy(Next, &Check) != MB_STATE_VALID || Check.Generation != Generation) {
		return MB_ERR_STATE;
	}

	return MB_OK;
}

void MB_BootCheck(void)
{
	MB_State_t State;
	uint32_t Crc;

	switch (MB_ReadState(&State)) {
	case MB_STATE_VALID:
		if (MB_InternalCrc32(State.ImageSize) == State.ImageCrc32) {
			// Expected firmware: normal boot.
			return;
		}
		// Flashed outside of the multiboot: this is the new Main.
		break;

	case MB_STATE_MISSING:
		// First multiboot boot: adopt the running firmware as Main.
		break;

	default:
		// Uncertain: never overwrite Main.
		return;
	}

	// Red LED while the SPI flash is written (a few seconds, once).
	gpio_bits_set(GPIOA, BOARD_GPIOA_LED_RED);
	Crc = MB_InternalCrc32(MB_INT_APP_SIZE);
	if (BackupInternalToSlot0(Crc) == MB_OK) {
		// A failure leaves the state unchanged: retried at next boot.
		CommitState(0, 0, MB_INT_APP_SIZE, Crc);
	}
	gpio_bits_reset(GPIOA, BOARD_GPIOA_LED_RED);
}

uint8_t MB_RestoreSlot(uint8_t Slot)
{
	// Called through a volatile pointer: the RAM code is out of BL range
	// and must not be reached through a linker veneer.
	void (*volatile pRestoreFn)(uint32_t, uint32_t, uint32_t *) = MB_RamRestore;
	MB_SlotHeader_t Header;
	uint8_t Error;

	Error = MB_ValidateSlot(Slot, &Header, NULL);
	if (Error != MB_OK) {
		return Error;
	}

	gBatteryVoltage = BATTERY_GetVoltage();
	if (gBatteryVoltage <= gCalibration.BatteryCalibration[2]) {
		return MB_ERR_BATTERY;
	}

	if (!LoadRamCode()) {
		return MB_ERR_RAM_LOAD;
	}

	// Last step before erasing: record the slot as active, so that the next
	// boot recognises the restored firmware. If this fails, the previous
	// record is still the newest valid one and nothing has been erased.
	// (CommitState does not use gFlashBuffer, which now holds the RAM code.)
	if (CommitState(Slot, Slot, Header.ImageSize, Header.ImageCrc32) != MB_OK) {
		return MB_ERR_STATE;
	}

	pRestoreFn(MB_SLOT_IMAGE(Slot), Header.ImageSize, (uint32_t *)(gFlashBuffer + MB_RAM_BUFFER_OFFSET));

	// Not reached: the RAM routine always resets the MCU.
	while (1) {
	}
}
