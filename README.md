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
configuration banks. It is **not functional yet**: only milestone M0 (read-only diagnostics) is implemented.

## Build options

| Option | Default | Effect |
|---|---|---|
| `ENABLE_VOICE_PROMPT` | 1 | Voice prompts. Forced to 0 by `ENABLE_MULTIBOOT`. |
| `ENABLE_MULTIBOOT` | 0 | Multiboot support. Also disables voice prompts and spectrum presets, and enables UART command 0x54. |
| `ENABLE_MB_DIAG` | 0 | Diagnostic screen (requires `ENABLE_MULTIBOOT`). |

Run `make clean` whenever you change build options.

```
make clean
make ENABLE_MULTIBOOT=1 ENABLE_MB_DIAG=1
```

## M0: diagnostics (read-only)

Nothing below writes to the radio.

* **Diagnostic screen**: hold **MENU** while powering on. It shows:
  * `SPI`: JEDEC ID of the SPI flash (manufacturer, type, capacity code) and its size in KB,
  * `FSZ`: internal flash size register (KB, hexadecimal),
  * `USD`: user system data register (bit 1 = FAP, flash access protection),
  * `EPP`: erase/program protection status (a 0 bit means a protected sector group),
  * `SLB`: security library (sLib) status.

  Press **EXIT** to continue booting.
* **Backups** with `tools/mb_tool.py` (Python 3 + `pip install pyserial`), radio powered on normally
  (not in bootloader mode), programming cable connected:
  ```
  python3 tools/mb_tool.py -p /dev/ttyUSB0 dump-spi spi_backup.bin
  python3 tools/mb_tool.py -p /dev/ttyUSB0 dump-int int_backup.bin
  ```
  `dump-spi` reads the whole 4 MB SPI flash with command 0x52 (`--size` / `--start` to change the range;
  a full dump takes several minutes). `dump-int` reads the 64 KB internal flash (factory bootloader at
  0x08000000-0x080013FF and application) with the new command 0x54, available when built with `ENABLE_MULTIBOOT`.

  **Keep these backups safe**: the SPI flash holds the calibration of your radio, which is unique to it.

Results and the analysis of the factory bootloader are in [docs/multiboot-hardware-notes.md](docs/multiboot-hardware-notes.md).

## Warnings

* Do not use voice prompt upload tools on a radio running a multiboot firmware: they erase the whole
  0x000000-0x2CFFFF area, which will hold the firmware slots.

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

