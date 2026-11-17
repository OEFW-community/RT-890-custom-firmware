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

#include <at32f421.h>
#include "driver/crm.h"

uint32_t gSystemCoreClock;

static const uint8_t gClockShiftTable[] = {
        0, 0, 0, 0,
        0, 0, 0, 0,
        1, 2, 3, 4,
        6, 7, 8, 9,
};

static void EnablePLL(void)
{
        uint32_t i;

        // Enable HICK
        CRM->ctrl_bit.hicken = 1;

        for (i = 0; i != 0xFFFF; i++) {
                if (CRM->ctrl_bit.hickstbl) {
                        // HICK stable
                        break;
                }
        }
        if (CRM->ctrl_bit.hickstbl) {
                // HICK stable — REVERTED to stock 72MHz.
                // CONFIRMED from git history: original 72MHz value was
                // 0x152 (2 wait states), not the 120MHz value of 0x153
                // (3 wait states) that was in the placeholder here before.
                FLASH->psr = 0x152;

                CRM->cfg = CRM->cfg;
                // Clear apb2div → APB2 = AHB / 1 = 72 MHz
                CRM->cfg_bit.apb2div = 0;
                CRM->cfg = CRM->cfg;
                // Clear ahbdiv → AHB = Core / 1 = 72 MHz
                CRM->cfg_bit.ahbdiv = 0;
                CRM->cfg = CRM->cfg;
                // Clear pllrcs, pllhextdiv, pllmult_l, pllmult_h
                CRM->cfg &= 0x1FC0FFFF;

                // --- CLOCK SELECTION ---
                CRM->cfg |= 0x20040000;    // 72 MHz: HICK 8 MHz × 9 = 72 MHz  (REVERTED)
                // CRM->cfg |= 0x20340000; // 120 MHz: HICK 8 MHz × 15 = 120 MHz (commented out -- this was the 120MHz change)

                CRM->ctrl_bit.pllen = 1;
                while (CRM->ctrl_bit.pllstbl == 0) {
                        // Wait PLL stable
                }
                CRM->cfg_bit.sclksel = CRM_SCLK_HICK;
                CRM->cfg_bit.sclksel = CRM_SCLK_PLL;
                while (CRM->cfg_bit.sclksts != CRM_SCLK_PLL) {
                        // Wait for PLL as system clock
                }
        }
}

void CRM_Init(void)
{
        CRM->ctrl_bit.hicken = 1;
        CRM->cfg   &= 0xE8FF000C;
        CRM->ctrl  &= 0xFEF6FFFF;
        CRM->ctrl  &= 0xFFFBFFFF;
        CRM->cfg   &= 0x1700FFFF;
        CRM->misc1 &= 0xFEFEFF00;
        CRM->clkint = 0x009F0000;
        EnablePLL();
        // Redirect vector table to bootloader
        SCB->VTOR = 0x08000000;
}

void CRM_GetCoreClock(void)
{
        switch (CRM->cfg_bit.sclksts) {
        case CRM_SCLK_HICK:
                if (CRM->misc1_bit.hickdiv && CRM->misc2_bit.hick_to_sclk) {
                        gSystemCoreClock = 48000000;
                } else {
                        gSystemCoreClock = 8000000;
                }
                break;

        case CRM_SCLK_HEXT:
                gSystemCoreClock = 8000000;
                break;

        case CRM_SCLK_PLL:
                if (CRM->pll_bit.pllcfgen) {
                        uint32_t Base;
                        uint32_t Multiplier;

                        switch (CRM->pll_bit.pllfr) {
                        case 0: Multiplier = 1; break;
                        case 1: Multiplier = 2; break;
                        case 2: Multiplier = 4; break;
                        case 3: Multiplier = 8; break;
                        case 4: Multiplier = 16; break;
                        case 5: Multiplier = 32; break;
                        default: Multiplier = 1; break;
                        }
                        Base = 4000000;
                        if (CRM->cfg_bit.pllrcs) {
                                Base = 8000000;
                                if (CRM->cfg_bit.pllhextdiv) {
                                        Base = 4000000;
                                }
                        }
                        gSystemCoreClock = (Base * CRM->pll_bit.pllns) / (Multiplier * CRM->pll_bit.pllms);
                } else {
                        uint32_t Multiplier;

                        if (CRM->cfg_bit.pllmult_h == 0 && CRM->cfg_bit.pllmult_l != CRM_PLL_MULT_16) {
                                Multiplier = 2;
                        } else {
                                Multiplier = 1;
                        }
                        Multiplier += (CRM->cfg_bit.pllmult_h << 4) | CRM->cfg_bit.pllmult_l;

                        // REVERTED: 8 MHz × 9 = 72,000,000
                        if (CRM->cfg_bit.pllrcs == 0) {
                                gSystemCoreClock = 4000000 * Multiplier;
                        } else if (CRM->cfg_bit.pllhextdiv) {
                                gSystemCoreClock = 4000000 * Multiplier;
                        } else {
                                gSystemCoreClock = 8000000 * Multiplier;
                        }
                }
                break;

        default:
                gSystemCoreClock = 8000000;
                break;
        }

        // Apply AHB divider — 0 = no divide → 72 MHz
        gSystemCoreClock >>= gClockShiftTable[CRM->cfg_bit.ahbdiv];
}

void CRM_InitPeripherals(void)
{
        // ADC clock: 72 MHz / 6 = 12 MHz, within the 36 MHz maximum.
        crm_adc_clock_div_set(CRM_ADC_DIV_6);

        // Enable required peripheral clocks
        crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
        crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);
        crm_periph_clock_enable(CRM_GPIOC_PERIPH_CLOCK, TRUE);
        crm_periph_clock_enable(CRM_GPIOF_PERIPH_CLOCK, TRUE);
        crm_periph_clock_enable(CRM_DMA1_PERIPH_CLOCK, TRUE);
        crm_periph_clock_enable(CRM_ADC1_PERIPH_CLOCK, TRUE);
        crm_periph_clock_enable(CRM_USART1_PERIPH_CLOCK, TRUE);
        crm_periph_clock_enable(CRM_TMR1_PERIPH_CLOCK, TRUE);
        crm_periph_clock_enable(CRM_TMR3_PERIPH_CLOCK, TRUE);
        crm_periph_clock_enable(CRM_TMR6_PERIPH_CLOCK, TRUE);
}
