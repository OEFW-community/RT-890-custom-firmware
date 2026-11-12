# Radtel RT-890 Custom Firmware

This project is an effort to improve the firmware of the Radtel RT-890 in terms of features and radio performance.

It is based on [DualTachyon's OEFW](https://github.com/OEFW-community/radtel-rt-890-oefw) which is reversed from the original Radtel 1.34 firmware.  
Thanks to him for making this possible!

## Disclaimer
This firmware is a work in progress and could be unstable; it could alter your radio and its data.  
Use at your own risk and remember to [back up your SPI memory](https://github.com/OEFW-community/RT-890-custom-firmware/wiki/SPI) before installing any custom firmware.  DO NOT SKIP THIS STEP.

## Features
- All stock features: [check user's manual](https://cdn.shopifycdn.net/s/files/1/0564/8855/8800/files/RT-890_user_manual.pdf?v=1670288968)
- RX frequency can be set from 10 to 1300 MHz (results may vary)
- SSB reception
- Light theme
- AM Fix (improvement in AM reception with strong signals, port of @OneOfEleven's excellent work on the Quansheng UV-K5)
- Sensitivty improvements in gain and squelch 
- Full control over side key and main key shortcuts
- New configurable actions (FM Radio, Scanner, FLashlight)
- 0.01K step
- Displaying registers in single VFO mode
- Displaying dBM when receiving
- Reworked scanner
  - 8 Scan lists
  - Faster scanning
  - Resume mode: Time, Carrier, No
  - Change scan direction while scanning (up/down keys)
  - Force scan resume (up/down keys)
- Spectrum view with waterfall display
- Reworked main menu
- Ability to disable LED toggling when scanning
- And much more!

## Usage and feature instructions
See the [Wiki in this repository](https://github.com/OEFW-community/RT-890-custom-firmware/wiki) for detailed usage instructions.

## Pre-built firmware
You can find pre-built firmwares in the [Actions](https://github.com/OEFW-community/RT-890-custom-firmware/actions)

## Telegram group
If you want to discuss this project, you can join the [Telegram group](https://t.me/RT890_OEFW).


---
_Original OEFW readme_

# Support

* If you like my work, you can support me through https://ko-fi.com/DualTachyon

# Open reimplementation of the Radtel RT-890 v1.34 firmware

This repository is a preservation project of the Radtel RT-890 v1.34 firmware.
It is dedicated to understanding how the radio works and help developers making their own customisations/fixes/etc.
It is by no means fully understood or has all variables/functions properly named, as this is best effort only.
As a result, this repository will not include any customisations or improvements over the original firmware.

# Compiler

arm-none-eabi GCC version 10.3.1 is recommended, which is the current version on Ubuntu 22.04.03 LTS.
Other versions may generate a flash file that is too big.
You can get an appropriate version from: https://developer.arm.com/downloads/-/gnu-rm

# Building

To build the firmware, you need to fetch the submodules and then run make:
```
git submodule update --init --recursive --depth=1
make
```

# Flashing

* Use the firmware.bin file with either [RT-890-Flasher](https://github.com/OEFW-community/radtel-rt-890-flasher) or [RT-890-Flasher-CLI](https://github.com/OEFW-community/radtel-rt-890-flasher-cli)

# Multiboot (work in progress)

A multiboot / multiconfig feature, inspired by the one of Armel F4HWN for the UV-K1/UV-K5 V3, is being developed.
It reuses the SPI flash area of the voice prompts (0x000000-0x2CFFFF) to store several firmware images and
configuration banks. Implemented so far: firmware slots with validated headers, restore of a slot into the
internal flash, active state and automatic backup of the main firmware, host tool to write slots (milestones
M0 to M3). Not yet: boot selector, configuration banks.

## Build options

| Option | Default | Effect |
|---|---|---|
| `ENABLE_VOICE_PROMPT` | 1 | Voice prompts. Forced to 0 by `ENABLE_MULTIBOOT`. |
| `ENABLE_MULTIBOOT` | 0 | Multiboot support. Also disables voice prompts and spectrum presets, and enables UART commands 0x54 and 0x61-0x63. |
| `ENABLE_MB_DIAG` | 0 | Temporary multiboot test screen (requires `ENABLE_MULTIBOOT`). |

Run `make clean` whenever you change build options.

```
make clean
make ENABLE_MULTIBOOT=1 ENABLE_MB_DIAG=1
```

## Memory map

| SPI flash address | Content |
|---|---|
| 0x000000-0x00FFFF | Slot 0 "Main": copy of the firmware flashed with the usual flasher, written by the radio only |
| 0x010000-0x04FFFF | Slots 1 to 4, written by the host |
| 0x050000 / 0x051000 | Active state, two alternating 4 KB sectors |
| 0x052000-0x2CFFFF | Reserved (configuration banks, future use) |

Each slot is 64 KB: a 4 KB header sector, then the image (at most 60416 bytes) at offset 0x1000.
The 64 byte header (little endian) is: magic `FMB1` (u32 0x31424D46), header version (u16, 1), flags (u16, bit 0 =
committed), image size (u32, multiple of 4), image CRC-32 (u32, zlib), name (16 bytes), version (16 bytes),
reserved (16 bytes). The image is written first and the committed header last.

## Main firmware and active state

The active state records the identity (size and CRC-32) of the firmware expected in the internal flash, the
slot it comes from and the configuration bank (24 byte record: magic `FMP3`, generation, image size, image
CRC-32, slot, ~slot, bank, ~bank, CRC-32 of the record). A new record is written and read back in the sector that
does not hold the newest valid one, so a power loss always leaves a valid record.

At every boot, before the settings are loaded, the CRC of the running firmware is compared with the active state:

* match: normal boot, nothing is written;
* no state at all (first multiboot boot) or mismatch (firmware flashed with the usual flasher): the running
  firmware becomes **Main**. It is copied to slot 0, verified, and recorded as active (slot 0, bank 0). The red
  LED is lit during this copy (a few seconds, once);
* corrupt state: nothing is written, Main is never overwritten.

Restoring a slot records it as active (with configuration bank = slot) right before the internal flash is erased,
so the restored firmware is recognised at the next boot.

The state logic is tested on a PC with a simulated SPI flash: `make -C tests/mb_host` (native gcc and zlib).

## Test screen (`ENABLE_MB_DIAG`)

Hold **MENU** while powering on. Each line shows a slot: number, name and CRC of its header, or `-N` with the
header error code. The bottom line shows the CRC-32 of the running firmware (`INT`), which is also the CRC-32 of
its 60416 byte `firmware.bin`, and the active slot recorded in the state (`A:n`, `A:-` no state, `A:?` corrupt).

* **0** to **4**, then **MENU** to confirm: restore that slot into the internal flash.
* **EXIT**: continue booting.

Before anything is erased, the slot is fully validated (header, image CRC, vector table, the same checks as the
factory bootloader) and the battery level checked. Otherwise `Refused, error N` is shown and nothing is
modified. Error codes: 1 slot number, 2 no header, 3 header version, 4 not committed, 5 image size, 6 image CRC,
7 vector table, 8 battery, 9 RAM code load, 10 read back, 11 active state write.

The restore routine runs from RAM with interrupts disabled. For each of the 59 sectors of 1 KB it reads the SPI
flash, erases the sector, programs it word by word and reads it back, with up to 3 attempts; bytes past the
image size are left erased. The green LED toggles after each sector, then the radio resets. Addresses below
0x08001400 (factory bootloader) are refused by the routine itself. If the radio does not restart correctly,
power it on while holding **side keys 1 and 2** to enter the factory bootloader and flash a firmware with the
usual flasher.

The RAM-resident code (`driver/mb_ram.c`, section `.mb_ramfunc`, linked over `gFlashBuffer`) is checked after
every multiboot link by `tools/check_mb_ramfunc.py`: all its branches must stay inside the section and its object
must not contain any relocation, i.e. any reference to code or data in the internal flash.

## Host tool

`tools/mb_tool.py` (Python 3 + `pip install pyserial`), radio powered on normally (not in bootloader mode),
programming cable connected:

```
python3 tools/mb_tool.py -p /dev/ttyUSB0 dump-spi spi_backup.bin
python3 tools/mb_tool.py -p /dev/ttyUSB0 dump-int int_backup.bin
python3 tools/mb_tool.py -p /dev/ttyUSB0 info
python3 tools/mb_tool.py -p /dev/ttyUSB0 write-slot 1 firmware.bin --name "My build" --fw-version 1.0
python3 tools/mb_tool.py -p /dev/ttyUSB0 validate 1
```

* `dump-spi` reads the SPI flash (whole 4 MB by default, `--start` / `--size` to change the range; several
  minutes). **Keep this backup safe**: the SPI flash holds the calibration of your radio, which is unique to it.
* `dump-int` reads the 64 KB internal flash (factory bootloader and application).
* `write-slot` only accepts slots 1 to 4 and images that look like an RT-890 application (vector table).

UART commands added by `ENABLE_MULTIBOOT` (same framing and checksum as the existing commands):

| Command | Frame | Answer |
|---|---|---|
| 0x54 | `54 Hi Lo Sum` | internal flash block Hi:Lo (128 bytes from 0x08000000), like 0x52 |
| 0x61 | `61 Slot 00 Sum` | erase slot 1-4, `06` or `FF` |
| 0x62 | `62 Hi Lo Data[128] Sum` | program 128 bytes at SPI address Hi:Lo × 128, slots 1-4 only, `06` or `FF` |
| 0x63 | `63 Slot 00 Sum` | `63 Slot Status Crc32[4] Sum`: full validation of a slot |

Hardware findings and the analysis of the factory bootloader are in
[docs/multiboot-hardware-notes.md](docs/multiboot-hardware-notes.md).

## Warnings

* Do not use voice prompt upload tools on a radio running a multiboot firmware: they erase the whole
  0x000000-0x2CFFFF area, which will hold the firmware slots.
* Restoring a firmware without multiboot support (stock Radtel firmware, other builds) is a one way trip:
  it has no way back to the slots. Use the factory bootloader (side keys 1 + 2 at power on) to return.
  Such a firmware may also read the slots as voice prompts: disable "Voice Prompt" first.

# License

Copyright 2023 Dual Tachyon
https://github.com/DualTachyon

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.

