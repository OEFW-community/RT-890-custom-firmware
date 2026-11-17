# Multiboot

Multiboot builds (`ENABLE_MULTIBOOT=1`) keep up to five firmware images and five configurations in the SPI flash
of the radio, in the area formerly used by the voice prompts, and switch between them from a menu at power on.
The design follows the multiboot of Armel F4HWN for the UV-K1/UV-K5 V3
([uv-k1-k5v3-firmware-custom](https://github.com/armel/uv-k1-k5v3-firmware-custom), Apache License 2.0).

* **Slot 0 "Main"** always holds the firmware you flashed with the usual flasher: the radio copies it there by
  itself. Slots 1 to 4 are written from a PC with `tools/mb_tool.py`.
* Each slot has its **own configuration** (settings, channels, VFOs, DTMF, factory reset template). Calibration,
  fonts and logo are shared.
* Switching firmware rewrites the internal flash of the MCU from the SPI flash. Every image is checked before
  anything is erased, and the factory bootloader is never touched, so the radio can always be recovered with the
  usual flasher.

## What a multiboot build removes

`ENABLE_MULTIBOOT=1` disables **voice prompts** (their SPI flash area is reused). To fit in the 59 KB of internal
flash, the reference multiboot build (the one made by the CI) also disables **NOAA** (North America only) and the
**frequency detector** (`ENABLE_FREQ_DETECTOR=0`: the "Freq Detect" key action does nothing). The spectrum of this
firmware has no presets. If the build still does not fit with your options,
disable the register editor (`ENABLE_REGISTER_EDIT=0`) or enable LTO (`ENABLE_LTO=1`).

## Building

```
git submodule update --init --recursive --depth=1
make clean
make ENABLE_MULTIBOOT=1 ENABLE_NOAA=0 ENABLE_FREQ_DETECTOR=0
```

Run `make clean` whenever you change build options. Python 3 is needed for the post-link check of the restore
code. The default build (`make`) is unchanged and contains no multiboot code.

| Option | Default | Effect |
|---|---|---|
| `ENABLE_VOICE_PROMPT` | 1 | Voice prompts. Forced to 0 by `ENABLE_MULTIBOOT`. |
| `ENABLE_FREQ_DETECTOR` | 1 | Frequency detector key action and its radar picture (about 2.2 kB). |
| `ENABLE_MULTIBOOT` | 0 | Multiboot support. Forces voice prompts off and adds UART commands 0x54 and 0x61-0x64. |

## Installation

1. **Back up the SPI flash first**, with the radio running its current firmware and the programming cable
   connected (Python 3 and `pip install pyserial`). It takes several minutes and contains the calibration of
   your radio, which is unique to it:
   ```
   python3 tools/mb_tool.py -p /dev/ttyUSB0 dump-spi spi_backup.bin
   ```
   `dump-spi` works with any firmware of this repository; the other `mb_tool.py` commands need a multiboot
   build.
2. Flash the multiboot `firmware.bin` with [RT-890-Flasher](https://github.com/OEFW-community/radtel-rt-890-flasher)
   or [RT-890-Flasher-CLI](https://github.com/OEFW-community/radtel-rt-890-flasher-cli) as usual (factory
   bootloader: hold **side keys 1 and 2** while powering on).
3. At the first start the **red LED** stays on for a few seconds: the radio copies its firmware to slot 0 ("Main").
   Your configuration is kept: it becomes configuration bank 0.

The same happens every time you flash a firmware with the usual flasher: it becomes the new Main.

## Boot selector

Hold **MENU** while powering on.

```
*0 Main       b35572
 1 Beep       beep1
 2 -
 3 -
 4 -
Bank 0
```

Each line shows a slot: `*` for the running slot, its number, name and version (`-` for an empty slot). The
bottom line shows the active configuration bank.

* **UP** / **DOWN** move the cursor (green line), **0** to **4** select a slot directly, **MENU** opens it,
  **EXIT** continues booting normally.
* The confirmation page shows the name and version of the slot and the **configuration bank** to use: by default
  the bank with the same number as the slot (the active bank for the running slot). **UP** / **DOWN** choose
  another one. **MENU** confirms, **EXIT** goes back.
* Another slot: "Please wait..." is shown, the green LED blinks about 59 times while the internal flash is
  rewritten, then the radio restarts with that firmware.
* The running slot with another bank: only the configuration changes, the radio restarts immediately.
* `Error N` means the operation was refused and nothing was modified (codes below).

## Adding firmware with `tools/mb_tool.py`

The radio must run a multiboot build, be powered on normally (not in bootloader mode) and be connected with the
programming cable.

```
python3 tools/mb_tool.py -p /dev/ttyUSB0 info
python3 tools/mb_tool.py -p /dev/ttyUSB0 write-slot 1 my-firmware.bin --name "My build" --fw-version 1.2
python3 tools/mb_tool.py -p /dev/ttyUSB0 validate 1
python3 tools/mb_tool.py -p /dev/ttyUSB0 erase-slot 1
python3 tools/mb_tool.py -p /dev/ttyUSB0 reset-bank 1
python3 tools/mb_tool.py -p /dev/ttyUSB0 dump-spi spi_backup.bin
python3 tools/mb_tool.py -p /dev/ttyUSB0 dump-int int_backup.bin
```

* `info`: active slot and bank, initialised configuration banks, slot names, versions, sizes and CRCs.
* `write-slot`: slots 1 to 4 only, and only files that look like an RT-890 application. The slot is erased,
  the image written, then its header, and the radio checks the result. The name (16 characters) and version
  (16 characters, 6 shown in the selector) are free text.
* `reset-bank`: forget configuration bank 1-4 (not the active one); it is copied again from the active bank the
  next time it is used.
* `dump-spi`: physical image of the SPI flash (4 MB by default, `--start` / `--size` for a range; `--banked` reads
  the configuration area as the CPS sees it). `dump-int`: the 64 KB internal flash.

## Configuration banks

* Bank 0 is the historical configuration area: nothing changes for existing users.
* The first time a bank is used it is initialised with a copy of the active configuration (a few seconds).
  Afterwards each bank evolves on its own.
* The CPS (and CHIRP) read and write the configuration of the running slot, including the extended settings
  page (UART command 0x4A). A factory reset only resets the active bank.
* A firmware without multiboot support always uses bank 0.

## Going back

* **To another multiboot build**: flash it as usual, it becomes Main; slots and banks are kept.
* **To a firmware without multiboot** (default build of this repository, other firmware, stock Radtel firmware):
  flash it as usual, or load it from a slot. It uses configuration bank 0. It has no selector: to come back,
  flash a multiboot build with the usual flasher.
* **To get the voice prompts back**: flash a default build, then upload the voice prompts with the usual tool.
  This erases the slots, the multiboot state and banks 1 to 4.

## Warnings

* Keep the SPI flash backup made before the installation: the calibration in it is unique to your radio.
* Multiboot builds refuse UART command 0x40 (voice prompt upload): it would erase the slots and banks.
  A firmware without multiboot accepts it: do not upload voice prompts while you want to keep your slots.
* A firmware without multiboot support may read the slots as voice prompts: disable "Voice Prompt" in it.
* Do not power the radio off while "Please wait..." is shown. If a slot change is interrupted or fails, the
  radio stays dark at the next power on (the partial image is never started): hold **side keys 1 and 2** while
  powering on and flash a firmware with the usual flasher. The factory bootloader is never modified, and your
  slots and configurations are kept.
* The battery must be above the "please charge" level to change slot (error 8).

## Technical reference

### SPI flash map

| Address | Content |
|---|---|
| 0x000000-0x00FFFF | Slot 0 "Main", written by the radio only |
| 0x010000-0x04FFFF | Slots 1 to 4 (64 KB each), written by the host |
| 0x050000 / 0x051000 | Active state, two alternating 4 KB sectors |
| 0x052000-0x05FFFF | Free |
| 0x060000 / 0x080000 / 0x0A0000 / 0x0C0000 | Configuration banks 1 to 4 (128 KB each) |
| 0x0E0000-0x2CFFFF | Free (former voice prompt area) |
| 0x3BF000-0x3C0FFF | Calibration and its backup, shared, never written by the multiboot |
| 0x3C1000-0x3D5FFF | Configuration bank 0: settings, channels, VFOs, DTMF (0x3C1000-0x3CAFFF), factory reset template (0x3CB000-0x3D4FFF), extended settings (0x3D5000, also written by UART command 0x4A) |

The unknown areas 0x2D0000-0x2F7FFF, 0x31C000-0x3B4FFF and 0x3D8000-0x3E1FFF are never written by the multiboot.

### Slot format

Each slot is 64 KB: a 4 KB header sector, then the image (at most 60416 bytes) at offset 0x1000. The 64 byte
header (little endian): magic `FMB1` (u32 0x31424D46), header version (u16, 1), flags (u16, bit 0 = committed),
image size (u32, multiple of 4), image CRC-32 (u32, zlib), name (16 bytes), version (16 bytes), reserved
(16 bytes). The image is written first and the committed header last.

### Active state and Main

The active state records the identity (size and CRC-32) of the firmware expected in the internal flash, its slot
and the configuration bank. 24 byte record: magic `FMP3`, generation, image size, image CRC-32, slot, ~slot, bank,
~bank, CRC-32 of the record. A new record is written and read back in the sector that does not hold the newest
valid one, so a power loss always leaves a valid record. A sector without the magic holds no record (erased or
voice prompt data).

At every boot, before the settings are loaded:

* running firmware = state: normal boot, nothing is written;
* no record, or another firmware (flashed with the usual flasher): the running firmware is copied to slot 0,
  verified, and recorded as active (slot 0, bank 0);
* a record with a bad integrity check and no valid one: nothing is written, Main is never overwritten;
* then the bank of the state is selected (bank 0 if there is no valid state or if that bank has no marker).

### Slot restore

Before anything is erased: full validation of the slot (header, image CRC, vector table with the same checks as
the factory bootloader), battery level, initialisation of the target bank if needed, copy of the restore code to
RAM, then the new state is recorded. Error codes: 1 slot or bank number, 2 no header, 3 header version, 4 not
committed, 5 image size, 6 image CRC, 7 vector table, 8 battery, 9 RAM code load, 10 read back, 11 active state
write, 12 configuration bank copy.

The restore routine (`driver/mb_ram.c`, section `.mb_ramfunc`) is linked over `gFlashBuffer` and copied there
right before use. It runs from RAM with interrupts disabled and uses raw register accesses only:

1. the first sector (vector table) is erased and left erased;
2. each of the 58 other sectors of 1 KB is read from the SPI flash, erased, programmed word by word and read
   back, with up to 3 attempts; bytes past the image size are left erased;
3. the CRC-32 of the whole image is computed (first sector from the SPI flash, the rest from the programmed
   internal flash) and compared with the slot header;
4. only then is the first sector programmed.

Until step 4 the initial stack pointer reads 0xFFFFFFFF, so the factory bootloader never starts a partial or
wrong image (and such an image can never be mistaken for a new Main). Addresses outside 0x08001400-0x0800FFFF
are refused by the routine itself. It then relocks the flash controller and resets the MCU, also on an error.
All interrupts are masked from the validation of the slot to the reset.
`tools/check_mb_ramfunc.py` runs after every multiboot link: every branch of the routine must stay inside its
section and its object must not contain any relocation (any reference to code or data in the internal flash).

### Configuration banks

Every SPI flash access to 0x3C1000-0x3D5FFF is redirected to the active bank in the SPI flash driver
(`driver/serial-flash.c`). Banks 1 to 4 sit below 0x3C1000, so a redirected address is never redirected again.
A bank is initialised by copying the active bank, verified by CRC, then a marker (`FMBK`, bank number) is written
in its last sector; the old marker is erased first, so an interrupted copy is never used.

### UART commands

Same framing and checksum as the existing commands. All flash commands are refused until the configuration bank
is selected at boot.

| Command | Frame | Answer |
|---|---|---|
| 0x40 | voice prompt upload | refused (`FF`) in multiboot builds |
| 0x52 | `52 Hi Lo Sum` | existing read command; bit 15 of Hi:Lo set = raw read, without bank translation |
| 0x54 | `54 Hi Lo Sum` | internal flash block Hi:Lo (128 bytes from 0x08000000), like 0x52 |
| 0x61 | `61 Slot 00 Sum` | erase slot 1-4, `06` or `FF` |
| 0x62 | `62 Hi Lo Data[128] Sum` | program 128 bytes at SPI address Hi:Lo × 128, slots 1-4 only, `06` or `FF` |
| 0x63 | `63 Slot 00 Sum` | `63 Slot Status Crc32[4] Sum`: full validation of a slot |
| 0x64 | `64 Bank 00 Sum` | reset configuration bank 1-4 (not the active one), `06` or `FF` |

### Tests

* `make -C tests/mb_host`: PC test of the state, Main backup and bank logic of `driver/mb_flash.c`, with a
  simulated SPI flash (native gcc and zlib).
* Hardware findings and the analysis of the factory bootloader:
  [multiboot-hardware-notes.md](multiboot-hardware-notes.md).
