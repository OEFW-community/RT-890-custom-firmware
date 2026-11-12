#!/usr/bin/env python3
# Copyright 2026 reppad
# https://github.com/reppad
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
#     Unless required by applicable law or agreed to in writing, software
#     distributed under the License is distributed on an "AS IS" BASIS,
#     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#     See the License for the specific language governing permissions and
#     limitations under the License.

"""RT-890 multiboot host tool.

Talks to the RT-890 custom firmware (radio powered on normally, NOT in
bootloader mode) over its programming cable at 115200 8N1.

Milestone M0 commands are strictly read-only:

  dump-spi  back up the external SPI flash using command 0x52
  dump-int  back up the internal MCU flash (factory bootloader + application)
            using command 0x54 (requires a firmware built with ENABLE_MULTIBOOT)

Requires pyserial (pip install pyserial).
"""

import argparse
import sys
import time

BLOCK_SIZE = 128
FRAME_SIZE = BLOCK_SIZE + 4

CMD_READ_SPI = 0x52
CMD_READ_INT = 0x54

INT_FLASH_SIZE = 0x10000


def checksum(data):
    return sum(data) & 0xFF


def parse_size(text):
    text = text.strip().upper()
    mult = 1
    if text.endswith("K"):
        mult, text = 1024, text[:-1]
    elif text.endswith("M"):
        mult, text = 1024 * 1024, text[:-1]
    return int(text, 0) * mult


class Radio:
    def __init__(self, port, baudrate=115200, timeout=1.0, retries=3):
        import serial  # imported here so --help works without pyserial

        self.port = serial.Serial(port, baudrate=baudrate, timeout=timeout)
        self.retries = retries

    def close(self):
        self.port.close()

    def read_block(self, command, block):
        """Read one 128-byte block, retrying on timeout or bad checksum."""
        request = bytearray([command, (block >> 8) & 0xFF, block & 0xFF, 0])
        request[3] = checksum(request[:3])
        last_error = "no answer"
        for _ in range(self.retries):
            self.port.reset_input_buffer()
            self.port.write(request)
            first = self.port.read(1)
            if not first:
                last_error = "timeout"
                continue
            if first[0] != command:
                # 0xFF = command refused (unknown command or block out of range)
                last_error = "refused (0x%02X)" % first[0]
                time.sleep(0.05)
                continue
            frame = first + self.port.read(FRAME_SIZE - 1)
            if len(frame) != FRAME_SIZE:
                last_error = "short frame (%d bytes)" % len(frame)
                continue
            if frame[1:3] != request[1:3]:
                last_error = "wrong block echoed"
                continue
            if checksum(frame[:-1]) != frame[-1]:
                last_error = "bad checksum"
                continue
            return bytes(frame[3:3 + BLOCK_SIZE])
        raise IOError("block 0x%04X: %s" % (block, last_error))


def dump(radio, command, start, size, out_path):
    if start % BLOCK_SIZE or size % BLOCK_SIZE:
        raise ValueError("start and size must be multiples of %d" % BLOCK_SIZE)
    first = start // BLOCK_SIZE
    count = size // BLOCK_SIZE
    if first + count > 0x10000:
        raise ValueError("range exceeds the 16-bit block address space")
    began = time.time()
    with open(out_path, "wb") as out:
        for i in range(count):
            out.write(radio.read_block(command, first + i))
            if i % 64 == 0 or i == count - 1:
                done = (i + 1) * BLOCK_SIZE
                rate = done / max(time.time() - began, 1e-3)
                sys.stderr.write("\r0x%06X / 0x%06X  (%.1f KB/s)" % (start + done, start + size, rate / 1024))
                sys.stderr.flush()
    sys.stderr.write("\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-p", "--port", required=True, help="serial port, e.g. /dev/ttyUSB0 or COM3")
    parser.add_argument("-b", "--baudrate", type=int, default=115200)
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("dump-spi", help="back up the external SPI flash (read-only)")
    p.add_argument("output")
    p.add_argument("--start", default="0", help="start address (default 0)")
    p.add_argument("--size", default="4M", help="number of bytes (default 4M)")

    p = sub.add_parser("dump-int", help="back up the internal MCU flash, 64 KB (read-only)")
    p.add_argument("output")

    args = parser.parse_args()

    radio = Radio(args.port, args.baudrate)
    try:
        if args.command == "dump-spi":
            dump(radio, CMD_READ_SPI, parse_size(args.start), parse_size(args.size), args.output)
        elif args.command == "dump-int":
            dump(radio, CMD_READ_INT, 0, INT_FLASH_SIZE, args.output)
    except (IOError, ValueError) as e:
        sys.stderr.write("\nerror: %s\n" % e)
        return 1
    finally:
        radio.close()
    print("saved %s" % args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
