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
};

// CRC-32 (zlib/IEEE, reflected 0xEDB88320). Pass 0 as Crc for a new
// computation, or a previous result to continue it.
uint32_t MB_Crc32(uint32_t Crc, const uint8_t *pData, uint32_t Size);

// CRC-32 of the whole internal application area.
uint32_t MB_InternalCrc32(void);

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

// Copies the running firmware (whole application area) into slot 0, checks
// it, then writes its committed header (name "Main", version = git hash).
// SPI flash only.
uint8_t MB_BackupInternalToSlot0(void);

// Reflashes the internal application area from a slot. The slot is fully
// validated and the battery checked before anything is erased. On success the
// radio resets and this function never returns; otherwise it returns an
// MB_ERR_* code and nothing has been modified.
uint8_t MB_RestoreSlot(uint8_t Slot);

#endif

