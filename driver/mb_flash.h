/* Copyright 2026 reppad
 * https://github.com/reppad
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
#define MB_SLOT_STRIDE		0x00010000U
#define MB_SLOT_IMG_OFFSET	0x00001000U
#define MB_SLOT_COUNT		5U
#define MB_SLOT_BASE(Slot)	((uint32_t)(Slot) * MB_SLOT_STRIDE)

// CRC-32 (zlib/IEEE, reflected 0xEDB88320). Pass 0 as Crc for a new
// computation, or a previous result to continue it.
uint32_t MB_Crc32(uint32_t Crc, const uint8_t *pData, uint32_t Size);

// CRC-32 of the whole internal application area.
uint32_t MB_InternalCrc32(void);

// CRC-32 of Size bytes of the SPI flash starting at Address.
uint32_t MB_SpiCrc32(uint32_t Address, uint32_t Size);

// Copies the RAM-resident code over gFlashBuffer and checks the copy.
// gFlashBuffer content is lost.
bool MB_LoadRamCode(void);

// Loads the RAM-resident code and runs its non destructive self test.
// Returns false if the RAM code could not be loaded.
bool MB_RunProbe(MB_Probe_t *pProbe, uint32_t SpiAddress);

// Copies the running firmware (whole application area) into the image area
// of slot 0 and verifies it. Writes the SPI flash only. M1a: no slot header
// yet (milestone M2).
bool MB_BackupInternalToSlot0(void);

#endif

