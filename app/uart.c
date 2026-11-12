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

#include <string.h>
#include "app/uart.h"
#ifdef ENABLE_MULTIBOOT
#include "driver/mb_flash.h"
#endif
#include "bsp/gpio.h"
#include "driver/pins.h"
#include "driver/serial-flash.h"
#include "driver/uart.h"
#include "radio/hardware.h"
#include "radio/settings.h"

#ifdef ENABLE_MULTIBOOT
#define MB_INT_FLASH_BASE	0x08000000U
#define MB_INT_FLASH_SIZE	0x10000U
// 4 byte commands: Cmd Hi Lo Sum
#define IS_SHORT_CMD(c)		((c) == 0x52 || (c) == 0x54 || (c) == 0x61 || (c) == 0x63 || (c) == 0x64)
// 132 byte commands: Cmd Hi Lo Data[128] Sum
#define IS_DATA_CMD(c)		(((c) >= 0x40 && (c) <= 0x4C) || (c) == 0x62)
#else
#define IS_SHORT_CMD(c)		((c) == 0x52)
#define IS_DATA_CMD(c)		((c) >= 0x40 && (c) <= 0x4C)
#endif

static uint8_t Buffer[256];
static uint8_t BufferLength;
static uint8_t Region;
static bool bFlashing;
static uint8_t g_Unused;

uint16_t UART_Timer;
bool UART_IsRunning;

static uint8_t CalcSum(const uint8_t *pBytes, uint8_t Size)
{
	uint8_t Sum = 0;
	uint8_t i;

	for (i = 0; i < Size; i++) {
		Sum += *pBytes++;
	}

	return Sum;
}

#ifdef ENABLE_MULTIBOOT
// Multiboot slot commands. Host writes are limited to slots 1 to 4: slot 0
// (backup of the main firmware), the rest of the SPI flash and the internal
// flash can never be written this way.
//   0x61 Slot 0 Sum           erase a slot                   -> 0x06 / 0xFF
//   0x62 Hi Lo Data[128] Sum  program 128 bytes at block Hi:Lo, i.e. at SPI
//                             address Hi:Lo * 128 (slot must be erased first)
//                                                            -> 0x06 / 0xFF
//   0x63 Slot 0 Sum           validate a slot (header, CRC, vectors)
//                             -> 0x63 Slot Status Crc32[4, little endian] Sum
//   0x64 Bank 0 Sum           reset config bank 1-4 (not the active one): it
//                             is copied again from the active bank on its
//                             next use                       -> 0x06 / 0xFF
static void SlotCmd(uint8_t Command, uint8_t Hi, uint8_t Lo)
{
	MB_SlotHeader_t Header;
	uint32_t Block = (Hi << 8) | Lo;
	uint32_t Crc = 0;

	if (Command == 0x63) {
		Buffer[0] = 0x63;
		Buffer[1] = Hi;
		Buffer[2] = MB_ValidateSlot(Hi, &Header, &Crc);
		memcpy(Buffer + 3, &Crc, 4);
		Buffer[7] = CalcSum(Buffer, 7);
		UART_Send(Buffer, 8);
		return;
	}
	if (Command == 0x64) {
		if (MB_ResetBank(Hi) != MB_OK) {
			UART_SendByte(0xFF);
			return;
		}
	} else if (Command == 0x61) {
		if (Hi == 0 || MB_EraseSlot(Hi) != MB_OK) {
			UART_SendByte(0xFF);
			return;
		}
	} else {
		if (Block < MB_SLOT_BASE(1) / 128U || Block >= MB_SLOT_BASE(MB_SLOT_COUNT) / 128U) {
			UART_SendByte(0xFF);
			return;
		}
		SFLASH_Write(Buffer + 3, Block * 128U, 128U);
	}
	UART_SendByte(0x06);
}
#endif

static void FlashCmd(uint8_t Command, uint8_t Hi, uint8_t Lo)
{
	uint16_t Count = 0;
	uint16_t Page = 0;
	uint16_t Block;
	uint16_t i;

	Block = (Hi << 8) | Lo;
#ifdef ENABLE_MULTIBOOT
	// Nothing touches the SPI flash before the config bank is selected.
	if (!gSFLASH_BankReady) {
		UART_SendByte(0xFF);
		return;
	}
#endif
	if (Command == 0x52) {
		Buffer[0] = 0x52;
		Buffer[1] = Hi;
		Buffer[2] = Lo;
#ifdef ENABLE_MULTIBOOT
		// The flash is 4 MB (blocks 0x0000-0x7FFF): bit 15 of the block
		// number requests a raw read, without config bank translation.
		if (Block & 0x8000U) {
			SFLASH_ReadRaw(Buffer + 3, (Block & 0x7FFFU) * 128U, 128);
		} else
#endif
		SFLASH_Read(Buffer + 3, Block * 128, 128);
		Buffer[131] = CalcSum(Buffer, 0x83);
		UART_Send(Buffer, 132);
		return;
	}
#ifdef ENABLE_MULTIBOOT
	if (Command == 0x54) {
		// Read-only dump of the internal MCU flash (bootloader + application),
		// 128 bytes per block from 0x08000000. Used to back up and analyse the
		// factory bootloader and to verify a multiboot restore.
		if (Block >= MB_INT_FLASH_SIZE / 128U) {
			UART_SendByte(0xFF);
			return;
		}
		Buffer[0] = 0x54;
		Buffer[1] = Hi;
		Buffer[2] = Lo;
		memcpy(Buffer + 3, (const void *)(MB_INT_FLASH_BASE + (Block * 128U)), 128);
		Buffer[131] = CalcSum(Buffer, 0x83);
		UART_Send(Buffer, 132);
		return;
	}
	if (Command >= 0x61 && Command <= 0x64) {
		SlotCmd(Command, Hi, Lo);
		return;
	}
#endif

	TMR1->ctrl1_bit.tmren = FALSE;
	// Why? Is this some left over from another radio?
	USART2->ctrl1_bit.uen = FALSE;

	switch (Command) {
	case 0x40:
		Page = 0x000;
		Count = 0x2D0;
		break;
	case 0x41:
		Page = 0x2D0;
		Count = 0x028;
		break;
	case 0x42:
		Page = 0x2F8;
		Count = 0x022;
		break;
	case 0x43:
		Page = 0x31A;
		Count = 0x002;
		break;
	case 0x47:
		Page = 0x3B5;
		Count = 0x00A;
		break;
	case 0x48:
		Page = 0x3BF;
		Count = 0x001;
		Region = 1;
		break;
	case 0x49:
		Page = 0x3C1;
		Count = 0x00A;
		Region = 2;
		break;
	case 0x4B:
		Page = 0x3D8;
		Count = 0x00A;
		break;
	case 0x4C:
		Page = 0x31C;
		Count = 0x99;
		break;
	}

	bFlashing = true;

	if (Block == 0) {
		for (i = 0; i < Count; i++) {
			SFLASH_Erase(Page + i);
		}
	}

	SFLASH_Write(Buffer + 3, (Page * 4096U) + (Block * 128U), 128U);
	UART_SendByte(0x06);
}

void HandlerUSART1(void)
{
	if (USART1->ctrl1_bit.rdbfien && USART1->sts & USART_RDBF_FLAG) {
		uint8_t Cmd;

		Buffer[BufferLength++] = USART1->dt;

		BufferLength %= 256;
		Cmd = Buffer[0];
		if (BufferLength == 1 && Cmd != 0x35 && !IS_DATA_CMD(Cmd) && !IS_SHORT_CMD(Cmd)) {
			UART_IsRunning = false;
			UART_Timer = 0;
			UART_SendByte(0xFF);
			BufferLength = 0;
		} else {
			if ((Cmd == 0x35 && BufferLength == 5) || (IS_SHORT_CMD(Cmd) && BufferLength == 4) || (IS_DATA_CMD(Cmd) && BufferLength == 132)) {
				if (CalcSum(Buffer, BufferLength - 1) == Buffer[BufferLength - 1]) {
					gpio_bits_flip(GPIOA, BOARD_GPIOA_LED_RED);
					UART_IsRunning = true;
					UART_Timer = 1000;
					if (Cmd == 0x35) {
						if (Buffer[3] == 16) {
							g_Unused = 0;
							UART_SendByte(0x06);
						} else if (Buffer[3] == 0xEE) {
							gpio_bits_reset(GPIOA, BOARD_GPIOA_LED_RED);
							if (bFlashing) {
								if (Region == 1) {
									SETTINGS_BackupCalibration();
								} else if (Region == 2) {
									SETTINGS_BackupSettings();
								}
								gpio_bits_set(GPIOA, BOARD_GPIOA_LED_GREEN);
								Region = 0;
								HARDWARE_Reboot();
							}
							UART_IsRunning = false;
							UART_Timer = 0;
						}
					} else {
						FlashCmd(Cmd, Buffer[1], Buffer[2]);
					}
				} else {
					gpio_bits_reset(GPIOA, BOARD_GPIOA_LED_RED);
					UART_SendByte(0xFF);
				}
				BufferLength = 0;
			} else if (Cmd == 0x32 && BufferLength == 5) {
				if (CalcSum(Buffer, 4) + 1 == Buffer[4]) {
					UART_IsRunning = true;
					UART_Timer = 1000;
					if (Buffer[3] != 0x16 && Buffer[3] == 0x10) {
						UART_SendByte(6);
					}
				} else {
					gpio_bits_reset(GPIOA, BOARD_GPIOA_LED_RED);
					UART_SendByte(0xFF);
					UART_IsRunning = false;
					UART_Timer = 0;
				}
				BufferLength = 0;
			}
		}
	}
}

