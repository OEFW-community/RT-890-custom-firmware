#include <stdbool.h>
#include <stdint.h>
#define SFLASH_BANK_START	0x3C1000U
#define SFLASH_BANK_SIZE	0x015000U
void SFLASH_Read(void *pBuffer, uint32_t Address, uint16_t Size);
void SFLASH_ReadRaw(void *pBuffer, uint32_t Address, uint16_t Size);
void SFLASH_Erase(uint32_t Page);
void SFLASH_Write(const void *pBuffer, uint32_t Address, uint16_t Size);
void SFLASH_SetBankBase(uint32_t Base);
extern bool gSFLASH_BankReady;
