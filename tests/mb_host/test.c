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

// Host (PC) test of the multiboot state logic of driver/mb_flash.c: the SPI
// flash is simulated as NOR (erase to 0xFF, writes clear bits), the internal
// flash is mapped at its real address, the RAM restore routine is a stub.
// Run with: make -C tests/mb_host

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <zlib.h>
#include <setjmp.h>
static jmp_buf jb;
#define CHECK0(c) do{ if(!(c)){printf("FAIL %s\n",#c);exit(1);} }while(0)
#include "at32f421.h"
#include "radio/settings.h"
#include "driver/serial-flash.h"
#include "driver/mb_flash.h"
gpio_type GPIOA_s; void gpio_bits_set(gpio_type *g, uint16_t p){} void gpio_bits_reset(gpio_type *g, uint16_t p){}
uint8_t gBatteryVoltage; uint8_t BATTERY_GetVoltage(void){return 200;}
Calibration_t gCalibration; uint8_t gFlashBuffer[8192];
const uint8_t __mb_ramfunc_load_start[16]={1,2,3}; uint8_t __mb_ramfunc_start[16];
void HARDWARE_EnableInterrupts(bool b){}
static uint8_t spi[0x400000]; static int writes;
// Same bank translation as driver/serial-flash.c.
static uint32_t bankoff; bool gSFLASH_BankReady;
static uint32_t tr(uint32_t a){ if(a-SFLASH_BANK_START<SFLASH_BANK_SIZE) a+=bankoff; return a; }
void SFLASH_SetBankBase(uint32_t base){ bankoff=base-SFLASH_BANK_START; gSFLASH_BankReady=true; }
void SFLASH_ReadRaw(void*b,uint32_t a,uint16_t n){CHECK0(a+n<=sizeof spi); memcpy(b,spi+a,n);}
void SFLASH_Read(void*b,uint32_t a,uint16_t n){SFLASH_ReadRaw(b,tr(a),n);}
void SFLASH_Erase(uint32_t p){uint32_t a=tr(p<<12); CHECK0(!(a&0xFFF)); memset(spi+a,0xFF,4096);writes++;}
void SFLASH_Write(const void*b,uint32_t a,uint16_t n){const uint8_t*s=b;a=tr(a);CHECK0(a+n<=sizeof spi);for(int i=0;i<n;i++)spi[a+i]&=s[i];writes++;}
static uint8_t *iflash=(uint8_t*)MB_INT_APP_BASE;
static int restored=-1; static uint32_t rsize;
void MB_RamRestore(uint32_t a, uint32_t size, uint32_t *buf){ restored=a; rsize=size; CHECK0(buf==(uint32_t*)(gFlashBuffer+0x1000)); memset(iflash,0xFF,MB_INT_APP_SIZE); memcpy(iflash,spi+a,size); longjmp(jb,1); }
#define CHECK(c) do{ if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);exit(1);} }while(0)
static void fillimg(uint8_t *p,int seed){ srand(seed); for(int i=0;i<MB_INT_APP_SIZE;i++)p[i]=rand(); uint32_t v[2]={0x20003FF0,0x08001500|1}; memcpy(p,v,8);}
static void writeslot(int slot,int seed,uint32_t size){ uint8_t img[MB_INT_APP_SIZE]; fillimg(img,seed); uint32_t b=slot*0x10000;
 memset(spi+b,0xFF,0x10000); memcpy(spi+b+0x1000,img,size); MB_SlotHeader_t h; memset(&h,0,sizeof h); h.Magic=MB_SLOT_MAGIC;h.HdrVersion=1;h.Flags=1;h.ImageSize=size;h.ImageCrc32=crc32(0,img,size); strcpy(h.Name,"T"); memcpy(spi+b,&h,64);}
int main(void){
 CHECK(mmap((void*)0x08000000,0x10000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==(void*)0x08000000);
 // The former voice prompt area is not erased on a real radio.
 srand(42); for(size_t i=0;i<sizeof spi;i++) spi[i]=rand(); MB_State_t st; MB_SlotHeader_t h;
 CHECK(MB_Crc32(0,(uint8_t*)"123456789",9)==0xCBF43926);
 fillimg(iflash,1);
 // 1. fresh radio, state sectors full of voice prompt data
 CHECK(MB_ReadState(&st)==MB_STATE_MISSING);
 CHECK(MB_ReadSlotHeader(2,&h)==MB_ERR_MAGIC);
 MB_BootCheck(); CHECK(MB_ReadState(&st)==MB_STATE_VALID && st.Slot==0 && st.Bank==0 && st.Generation==1);
 CHECK(MB_ValidateSlot(0,&h,NULL)==MB_OK && h.ImageCrc32==crc32(0,iflash,MB_INT_APP_SIZE) && !memcmp(h.Name,"Main",4));
 writes=0; MB_BootCheck(); CHECK(writes==0); // normal boot writes nothing
 // 2. flashed via bootloader
 fillimg(iflash,2); MB_BootCheck(); CHECK(MB_ReadState(&st)==MB_STATE_VALID && st.Generation==2 && st.Slot==0);
 CHECK(MB_ValidateSlot(0,&h,NULL)==MB_OK && h.ImageCrc32==crc32(0,iflash,MB_INT_APP_SIZE));
 CHECK(st.ImageCrc32==h.ImageCrc32);
 // 3. restore slot 1 (smaller image)
 writeslot(1,7,0xE000); CHECK(MB_ValidateSlot(1,&h,NULL)==MB_OK);

 #define RESTORE(n) (setjmp(jb)==0 ? MB_RestoreSlot(n) : 100)
 restored=-1; CHECK(RESTORE(1)==100 && restored==0x11000 && rsize==0xE000);
 CHECK(MB_ReadState(&st)==MB_STATE_VALID && st.Generation==3 && st.Slot==1 && st.Bank==1 && st.ImageSize==0xE000);
 writes=0; MB_BootCheck(); CHECK(writes==0); // restored firmware recognised
 // 4. refused restores modify nothing
 CHECK(MB_RestoreSlot(2)==MB_ERR_MAGIC); spi[0x11000+100]^=1; CHECK(MB_RestoreSlot(1)==MB_ERR_CRC); spi[0x11000+100]^=1;
 CHECK(MB_ReadState(&st)==MB_STATE_VALID && st.Generation==3);
 gCalibration.BatteryCalibration[2]=250; CHECK(MB_RestoreSlot(1)==MB_ERR_BATTERY); gCalibration.BatteryCalibration[2]=0;
 // 5. power loss while writing a new record: sector erased, nothing written
 // gen3 is in sector A (gen1 A, gen2 B, gen3 A): the next record goes to B
 memset(spi+MB_STATE_B,0xFF,4096); CHECK(MB_ReadState(&st)==MB_STATE_VALID && st.Generation==3);
 // half written record in B: corrupt copy ignored, A still used
 memset(spi+MB_STATE_B,0xFF,4096); memcpy(spi+MB_STATE_B,"FMP3garbage",11); CHECK(MB_ReadState(&st)==MB_STATE_VALID && st.Generation==3);
 // 6. back to slot 0
 restored=-1; CHECK(RESTORE(0)==100 && restored==0x1000);
 CHECK(MB_ReadState(&st)==MB_STATE_VALID && st.Generation==4 && st.Slot==0 && st.Bank==0);
 writes=0; MB_BootCheck(); CHECK(writes==0);
 uint8_t main_hdr[64]; memcpy(main_hdr,spi,64);
 // 7. both records corrupt: never overwrite Main, even if the firmware changed
 spi[MB_STATE_A+8]^=1; spi[MB_STATE_B+8]^=1; CHECK(MB_ReadState(&st)==MB_STATE_CORRUPT);
 fillimg(iflash,3); writes=0; MB_BootCheck(); CHECK(writes==0 && !memcmp(main_hdr,spi,64));
 // 8. both records erased again: adopt the running firmware
 memset(spi+MB_STATE_A,0xFF,8192); fillimg(iflash,4); MB_BootCheck(); CHECK(MB_ReadState(&st)==MB_STATE_VALID && st.Generation==1);
 CHECK(MB_ValidateSlot(0,&h,NULL)==MB_OK && h.ImageCrc32==crc32(0,iflash,MB_INT_APP_SIZE));
 // 9. one record corrupt, the other sector holds foreign data: uncertain
 for(int i=0;i<4096;i++) spi[MB_STATE_B+i]=rand();
 CHECK(MB_ReadState(&st)==MB_STATE_VALID); // record in A, foreign data in B
 spi[MB_STATE_A+8]^=1; CHECK(MB_ReadState(&st)==MB_STATE_CORRUPT);
 uint8_t hdr0[64]; memcpy(hdr0,spi,64); fillimg(iflash,5); writes=0; MB_BootCheck(); CHECK(writes==0 && !memcmp(hdr0,spi,64));
 // 10. config banks
 { memset(spi+MB_STATE_A,0xFF,8192); fillimg(iflash,6); MB_BootCheck();   // fresh state, slot 0, bank 0
   CHECK(MB_ActiveBank()==0 && gSFLASH_BankReady);
   uint8_t cfg[SFLASH_BANK_SIZE]; for(size_t i=0;i<sizeof cfg;i++) cfg[i]=rand(); memcpy(spi+SFLASH_BANK_START,cfg,sizeof cfg);
   // banks 1-4 hold voice data, never initialised
   for(int i=0x60000;i<0xE0000;i++) spi[i]=rand();
   writeslot(1,7,0xE000); writeslot(2,8,0xEC00);
   restored=-1; CHECK(RESTORE(1)==100 && restored==0x11000);
   CHECK(!memcmp(spi+0x60000,cfg,sizeof cfg));              // bank 1 copied from bank 0
   CHECK(MB_ReadState(&st)==MB_STATE_VALID && st.Slot==1 && st.Bank==1);
   MB_BootCheck(); CHECK(MB_ActiveBank()==1);
   // config accesses now go to bank 1
   uint8_t b[16]; SFLASH_Read(b,0x3C1030,16); CHECK(!memcmp(b,cfg+0x30,16));
   SFLASH_Erase(0x3C1); SFLASH_Write("bank1",0x3C1000,5); CHECK(!memcmp(spi+0x60000,"bank1",5) && !memcmp(spi+SFLASH_BANK_START,cfg,16));
   SFLASH_ReadRaw(b,0x3C1000,16); CHECK(!memcmp(b,cfg,16));   // raw read ignores the bank
   // calibration and fonts are shared
   SFLASH_Read(b,0x3BF000,16); CHECK(!memcmp(b,spi+0x3BF000,16)); SFLASH_Read(b,0x3D6000,16); CHECK(!memcmp(b,spi+0x3D6000,16));
   // slot 2 from bank 1: bank 2 is a copy of bank 1
   restored=-1; CHECK(RESTORE(2)==100 && restored==0x21000);
   CHECK(!memcmp(spi+0x80000,"bank1",5) && !memcmp(spi+0x80000+0x1000,cfg+0x1000,sizeof cfg-0x1000));
   MB_BootCheck(); CHECK(MB_ActiveBank()==2);
   // back to slot 1: bank 1 already initialised, kept as is
   memcpy(b,spi+0x60000,16); restored=-1; CHECK(RESTORE(1)==100); CHECK(!memcmp(spi+0x60000,b,16));
   MB_BootCheck(); CHECK(MB_ActiveBank()==1);
   // back to slot 0: bank 0 untouched by all of this
   restored=-1; CHECK(RESTORE(0)==100); MB_BootCheck(); CHECK(MB_ActiveBank()==0 && !memcmp(spi+SFLASH_BANK_START,cfg,sizeof cfg));
   // a bank whose marker is lost is never used
   restored=-1; CHECK(RESTORE(1)==100); spi[0x60000+MB_BANK_MARKER]^=1; MB_BootCheck(); CHECK(MB_ActiveBank()==0);
 }
 printf("all host tests passed\n");
 return 0;}
