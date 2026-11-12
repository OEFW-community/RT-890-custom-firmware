# Multiboot: hardware notes (milestone M0)

Findings gathered with the M0 diagnostic build (`ENABLE_MULTIBOOT=1 ENABLE_MB_DIAG=1`) on one radio,
and from a disassembly of the factory bootloader read back with UART command 0x54.
The bootloader binary itself is Radtel code and is not part of this repository.

## Diagnostic screen

| Line | Value | Meaning |
|---|---|---|
| `SPI` | `856016` | Puya (0x85), type 0x60, capacity code 0x16 = 32 Mbit = **4 MB** |
| `FSZ` | `0040` | 64 KB internal flash |
| `USD` | `03FFFFFE` | bit 1 = 1: **FAP (flash access protection) enabled**, low level (bit 26 = 0); bit 2 = 1: watchdog not auto-started; user data bytes 0xFF |
| `EPP` | `FFFFFFFF` | no erase/program protection |
| `SLB` | `00FF0000` | sLib not enabled (only the erased "extension memory" field is set) |

## Factory bootloader (0x08000000-0x080013FF)

* Code ends at 0x080011EB; the rest of the 5 KB is erased.
* At reset it reads three inputs: PB7 (PTT / USART1_RX), PF7 (side key 1) and PA15 (side key 2).
  It stays in flashing mode only when PB7 reads high and **both side keys are held**; otherwise it
  jumps to the application.
* Jump to the application: the only check is that the initial stack pointer at 0x08001400 lies in
  0x20000000-0x20003FFF. Then MSP is loaded and the reset vector at 0x08001404 is called.
  **No checksum, no flag**: an image written by the firmware itself is accepted exactly like one
  written by the bootloader. If the check fails (erased application), the bootloader does not jump
  but still enters flashing mode when the side keys are held, so the radio stays recoverable.
* Command 0x39 (erase) erases 59 sectors of **1 KB** from 0x08001400 to 0x0800FFFF.
  Sector erase sequence: `ctrl.secers = 1`, `addr = sector`, `ctrl.erstr = 1`, wait, clear `secers`.
* Command 0x57 writes 128 bytes at 0x08001400 + offset, as 64 half-word programs.
* After the 472nd block (472 × 128 = 60416 bytes = the whole application area) it acknowledges and
  jumps to the application.
* Its vector table only forwards USART1 to its own handler; every other interrupt is a dead loop,
  which is why the application relocates `SCB->VTOR`.

## Consequences for the design

* The SPI flash is 4 MB, so UART read blocks 0x8000-0xFFFF never address real data: bit 15 of the
  0x52 block number can be used as a "raw (unbanked) read" flag.
* Sector size for the restore routine: 1 KB, 59 sectors, sector 5 (0x08001400) to sector 63.
* Nothing in the bootloader area must ever be erased or written.
* FAP is enabled. Code running from main flash can read it (UART 0x54 works). Whether code running
  from SRAM (after booting from main flash) can still read main flash must be checked on the radio
  before relying on read-back verification in the RAM restore routine.
