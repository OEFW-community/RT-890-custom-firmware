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

#ifndef DRIVER_AUDIO_H
#define DRIVER_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef ENABLE_VOICE_PROMPT
extern uint16_t gAudioTimer;
extern bool gAudioPlaying;
extern uint8_t gAudioOffsetLast;
extern uint8_t gAudioOffsetIndex;

void AUDIO_PlaySample(uint16_t Period, uint32_t Offset);
void AUDIO_PlayMenuSample(uint8_t ID);
void AUDIO_PlaySampleOptional(uint8_t Index);
void AUDIO_PlayChannelNumber(void);
void AUDIO_PlayDigit(uint8_t Digit);

// Drop any queued voice prompt.
static inline void AUDIO_CancelQueue(void) { gAudioOffsetIndex = gAudioOffsetLast; }
#else
// Voice prompts compiled out: the SPI flash area they used is free for other uses.
static inline void AUDIO_PlaySampleOptional(uint8_t Index) { (void)Index; }
static inline void AUDIO_PlayChannelNumber(void) { }
static inline void AUDIO_PlayDigit(uint8_t Digit) { (void)Digit; }
static inline void AUDIO_CancelQueue(void) { }
#endif

#endif

