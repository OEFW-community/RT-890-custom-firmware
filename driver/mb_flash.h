/* Copyright 2026 reppad
 * https://github.com/reppad
 *
 * The slot header layout follows the multiboot of the UV-K1/UV-K5 V3 custom
 * firmware, Copyright 2026 Armel F4HWN
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

#ifndef DRIVER_MB_FLASH_H
#define DRIVER_MB_FLASH_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/mb_ram.h"

// Internal flash: the factory bootloader occupies 0x08000000-0x080013FF and
// must never be touched. The application area is 59 sectors of 1 KB.
#define MB_INT_APP_BASE		0x08001400U
#define MB_INT_APP_SIZE		0x0000EC00U
#define MB_INT_SECTOR_SIZE	0x00000400U

// SPI flash (4 MB), former voice prompt area 0x000000-0x2CFFFF.
// Each firmware slot is 64 KB: a 4 KB header sector followed by the image.
// Slot 0 ("Main") is written by the firmware only (backup of the running
// firmware); slots 1 to 4 are written by the host over UART.
#define MB_SLOT_STRIDE		0x00010000U
#define MB_SLOT_IMG_OFFSET	0x00001000U
#define MB_SLOT_COUNT		5U
#define MB_SLOT_BASE(Slot)	((uint32_t)(Slot) * MB_SLOT_STRIDE)
#define MB_SLOT_IMAGE(Slot)	(MB_SLOT_BASE(Slot) + MB_SLOT_IMG_OFFSET)

// Offset of the 1 KB sector buffer of the restore routine in gFlashBuffer,
// after the RAM-resident code (checked by firmware.ld).
#define MB_RAM_BUFFER_OFFSET	0x1000U

// Slot header, at the start of the slot (64 bytes, little endian). The host
// writes the image first and this header, with MB_FLAG_COMMITTED, last.
#define MB_SLOT_MAGIC		0x31424D46U	// "FMB1"
#define MB_HDR_VERSION		1U
#define MB_FLAG_COMMITTED	(1U << 0)
#define MB_NAME_LEN		16U
#define MB_VERSION_LEN		16U

typedef struct {
	uint32_t Magic;
	uint16_t HdrVersion;
	uint16_t Flags;
	uint32_t ImageSize;		// bytes, multiple of 4, <= MB_INT_APP_SIZE
	uint32_t ImageCrc32;		// CRC-32 (zlib) of the ImageSize bytes
	char Name[MB_NAME_LEN];		// not necessarily NUL terminated
	char Version[MB_VERSION_LEN];
	uint8_t Reserved[16];
} MB_SlotHeader_t;

enum {
	MB_OK = 0,
	MB_ERR_SLOT,		// 1: slot number out of range
	MB_ERR_MAGIC,		// 2: no slot header
	MB_ERR_VERSION,		// 3: unknown header version
	MB_ERR_NOT_COMMITTED,	// 4: image not marked complete
	MB_ERR_SIZE,		// 5: image size out of range
	MB_ERR_CRC,		// 6: image CRC mismatch
	MB_ERR_IMAGE,		// 7: implausible vector table
	MB_ERR_BATTERY,		// 8: battery too low to reflash safely
	MB_ERR_RAM_LOAD,	// 9: RAM code copy mismatch
	MB_ERR_VERIFY,		// 10: read back mismatch after a write
	MB_ERR_STATE,		// 11: active state could not be written
	MB_ERR_BANK,		// 12: config bank copy failed
};

// Config banks. Bank 0 is the historical config range (SFLASH_BANK_START,
// 0x3C1000-0x3D5FFF, see driver/serial-flash.h); banks 1 to 4 are 128 KB
// areas holding a copy of that range. A bank is initialised by copying the
// active bank into it, then writing a marker in its last sector (the area is
// never assumed erased: it may still hold voice prompt data).
#define MB_BANK_COUNT		5U
#define MB_BANK1_BASE		0x00060000U
#define MB_BANK_STRIDE		0x00020000U
#define MB_BANK_MARKER		0x0001F000U	// marker offset in a bank
#define MB_BANK_MAGIC		0x4B424D46U	// "FMBK"

// Active state: identity of the firmware expected in the internal flash and
// the slot / config bank it comes from. Stored in two alternating SPI flash
// sectors: a new record is written and verified in the sector that does not
// hold the newest valid one, so a power loss always leaves a usable record.
// The record layout follows the "FMP3" record of the F4HWN multiboot.
#define MB_STATE_A		0x00050000U
#define MB_STATE_B		0x00051000U
#define MB_STATE_MAGIC		0x33504D46U	// "FMP3"

typedef struct {
	uint32_t Magic;
	uint32_t Generation;		// incremented by every new record
	uint32_t ImageSize;		// expected internal image size
	uint32_t ImageCrc32;		// expected CRC-32 of its ImageSize bytes
	uint8_t Slot;			// slot the running firmware comes from
	uint8_t SlotInv;		// ~Slot
	uint8_t Bank;			// active config bank (milestone M5)
	uint8_t BankInv;		// ~Bank
	uint32_t StateCrc32;		// CRC-32 of all preceding fields
} MB_State_t;

// MB_ReadState() results. MISSING (no record magic in either sector: erased,
// or still holding voice prompt data) means a radio that never ran a
// multiboot firmware: adopting the running firmware as Main is safe. CORRUPT
// (a record with a bad integrity check and no valid one) means uncertain:
// Main must never be overwritten then.
enum {
	MB_STATE_VALID = 0,
	MB_STATE_MISSING,
	MB_STATE_CORRUPT,
};

// CRC-32 (zlib/IEEE, reflected 0xEDB88320). Pass 0 as Crc for a new
// computation, or a previous result to continue it.
uint32_t MB_Crc32(uint32_t Crc, const uint8_t *pData, uint32_t Size);

// CRC-32 of the first Size bytes of the internal application area.
uint32_t MB_InternalCrc32(uint32_t Size);

// CRC-32 of Size bytes of the SPI flash starting at Address.
uint32_t MB_SpiCrc32(uint32_t Address, uint32_t Size);

// Reads the header of a slot and checks it (no image CRC). Returns MB_OK or
// an MB_ERR_* code. pHeader is always filled when Slot is valid.
uint8_t MB_ReadSlotHeader(uint8_t Slot, MB_SlotHeader_t *pHeader);

// Full validation of a slot: header, image CRC and vector table.
// pCrc (may be NULL) receives the computed image CRC.
uint8_t MB_ValidateSlot(uint8_t Slot, MB_SlotHeader_t *pHeader, uint32_t *pCrc);

// Erases a whole slot (header and image). SPI flash only.
uint8_t MB_EraseSlot(uint8_t Slot);

// Physical base of a config bank.
uint32_t MB_BankBase(uint8_t Bank);

// Config bank selected at boot by MB_BootCheck().
uint8_t MB_ActiveBank(void);

// Forgets a config bank (1 to 4, not the active one) by erasing its marker:
// it is initialised again from the active bank on its next use.
uint8_t MB_ResetBank(uint8_t Bank);

// Reads the newest valid active state record. Returns MB_STATE_*.
uint8_t MB_ReadState(MB_State_t *pState);

// Boot time check, before the settings are loaded. If the running firmware
// is not the one recorded in the active state (it was flashed outside of the
// multiboot, e.g. with the factory bootloader), or if there is no state yet,
// the running firmware becomes "Main": it is copied to slot 0 and recorded as
// active (slot 0, bank 0). Nothing is written when the state is corrupt.
// Then selects the config bank of the state (bank 0 if there is no valid
// state, or if that bank is not initialised). SPI flash only.
void MB_BootCheck(void);

// Reflashes the internal application area from a slot and activates a
// config bank. The slot is fully validated and the battery checked before
// anything is erased. If the bank is not initialised yet, the active bank is
// copied into it. Then the slot and bank are recorded as active. On success
// the radio resets and this function never returns; otherwise it returns an
// MB_ERR_* code and the internal flash has not been modified.
uint8_t MB_RestoreSlot(uint8_t Slot, uint8_t Bank);

// Activates another config bank for the running firmware (no reflash): the
// bank is initialised from the active one if needed, recorded in the state,
// and the radio resets. Requires a valid state. Only returns on error.
uint8_t MB_SwitchBank(uint8_t Bank);

#endif

