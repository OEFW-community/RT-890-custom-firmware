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

typedef struct {
	// Internal flash words read by RAM-resident code (FAP check).
	uint32_t IntFirst;
	uint32_t IntLast;
	// FLASH->ctrl before unlocking, after unlocking and after locking again.
	uint32_t CtrlBefore;
	uint32_t CtrlUnlocked;
	uint32_t CtrlLocked;
	// Bytes read from the SPI flash by the RAM-resident bit-banged driver.
	uint8_t Spi[16];
} MB_Probe_t;

// Non destructive self test of the building blocks of the restore routine:
// reads internal flash words, unlocks then relocks the flash controller (no
// erase, no program) and reads 16 bytes of SPI flash at SpiAddress.
// Must only be called through MB_RunProbe() (driver/mb_flash.c).
void MB_RamProbe(MB_Probe_t *pProbe, uint32_t SpiAddress);

#endif

