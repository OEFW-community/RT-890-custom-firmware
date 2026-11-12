/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
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

#ifndef DRIVER_SERIAL_FLASH_H
#define DRIVER_SERIAL_FLASH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef ENABLE_MULTIBOOT
// Config range redirected to the active multiboot config bank: settings,
// channels, VFOs, DTMF (0x3C1000-0x3CAFFF), factory reset template
// (0x3CB000-0x3D4FFF) and extended settings (0x3D5000).
#define SFLASH_BANK_START	0x3C1000U
#define SFLASH_BANK_SIZE	0x015000U

// Physical base of the active bank (SFLASH_BANK_START for bank 0). Set once
// at boot, before the settings are loaded.
void SFLASH_SetBankBase(uint32_t Base);
// False until SFLASH_SetBankBase() has been called.
extern bool gSFLASH_BankReady;
// Read without bank translation.
void SFLASH_ReadRaw(void *pBuffer, uint32_t Address, uint16_t Size);
#endif

void SFLASH_Init(void);
void SFLASH_Read(void *pBuffer, uint32_t Address, uint16_t Size);
// Returns manufacturer << 16 | memory type << 8 | capacity (JEDEC 0x9F)
uint32_t SFLASH_ReadJedecId(void);
void SFLASH_Erase(uint32_t Page);
void SFLASH_Write(const void *pBuffer, uint32_t Address, uint16_t Size);
void SFLASH_Update(const void *pBuffer, uint32_t Address, uint16_t Size);

#endif

