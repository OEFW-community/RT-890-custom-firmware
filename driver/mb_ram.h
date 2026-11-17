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

#ifndef DRIVER_MB_RAM_H
#define DRIVER_MB_RAM_H

#include <stdint.h>

// Code in this module is linked in .mb_ramfunc: it executes from RAM, over
// gFlashBuffer, after MB_LoadRamCode() (driver/mb_flash.c) has copied it there.
// It must never call or read anything located in the internal flash, which is
// unavailable while the restore routine erases and programs it. This is
// enforced after linking by tools/check_mb_ramfunc.py.

// Rewrites the whole internal application area (59 sectors of 1 KB from
// 0x08001400) with the ImageSize bytes stored in the SPI flash at SpiAddress
// (the rest of the area is left erased), then resets the MCU. The first
// sector (vector table) is erased first and only programmed once the CRC-32
// of the whole image matches ImageCrc32, so a partial or wrong image is never
// bootable. pBuffer is a 1 KB, word aligned RAM buffer. Interrupts are
// disabled and the routine never returns: on an unrecoverable sector error it
// relocks the flash controller and resets immediately. The caller must have
// validated the image beforehand. Must only be called through
// MB_RestoreSlot() (driver/mb_flash.c).
void MB_RamRestore(uint32_t SpiAddress, uint32_t ImageSize, uint32_t ImageCrc32, uint32_t *pBuffer) __attribute__((noreturn));

#endif

