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

Read-only commands:

  dump-spi    back up the external SPI flash using command 0x52
  dump-int    back up the internal MCU flash (factory bootloader + application)
              using command 0x54
  info        show the headers of the firmware slots
  validate    ask the radio to fully check a slot (header, CRC, vectors)

Slot writing (SPI flash only, slots 1 to 4, never the internal flash):

  write-slot  erase a slot, write a firmware image, then its committed
              header, and validate it

Every command except dump-spi needs a firmware built with ENABLE_MULTIBOOT.

Requires pyserial (pip install pyserial).
"""

import argparse
import struct
import sys
import time
import zlib

BLOCK_SIZE = 128
FRAME_SIZE = BLOCK_SIZE + 4

CMD_READ_SPI = 0x52
CMD_READ_INT = 0x54
CMD_SLOT_ERASE = 0x61
CMD_SLOT_WRITE = 0x62
CMD_SLOT_VALIDATE = 0x63

ACK = 0x06

INT_FLASH_SIZE = 0x10000
APP_SIZE = 0xEC00

SLOT_COUNT = 5
SLOT_STRIDE = 0x10000
SLOT_IMG_OFFSET = 0x1000

# Slot header (driver/mb_flash.h), 64 bytes little endian.
HEADER_FORMAT = "<IHHII16s16s16s"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
SLOT_MAGIC = 0x31424D46  # "FMB1"
HDR_VERSION = 1
FLAG_COMMITTED = 1

ERRORS = {
    0: "OK",
    1: "slot number out of range",
    2: "no slot header",
    3: "unknown header version",
    4: "image not committed",
    5: "image size out of range",
    6: "image CRC mismatch",
    7: "implausible vector table",
    8: "battery too low",
    9: "RAM code load failed",
    10: "read back mismatch",
}


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
        # End of session frame (as sent by the CPS): the firmware toggles the
        # red LED on every valid frame and only switches it off on this one.
        # It is not acknowledged.
        try:
            frame = bytearray([0x35, 0, 0, 0xEE])
            frame.append(checksum(frame))
            self.port.write(frame)
            self.port.flush()
        finally:
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

    def command_ack(self, frame, timeout=1.0):
        """Send a command expecting a single 0x06 byte (no retry: the radio
        may have executed it even if the answer was lost)."""
        frame = bytearray(frame)
        frame.append(checksum(frame))
        self.port.reset_input_buffer()
        self.port.timeout = timeout
        try:
            self.port.write(frame)
            answer = self.port.read(1)
        finally:
            self.port.timeout = 1.0
        if answer != bytes([ACK]):
            raise IOError("command 0x%02X refused (%s)" % (frame[0], answer.hex() or "timeout"))

    def erase_slot(self, slot):
        self.command_ack([CMD_SLOT_ERASE, slot, 0], timeout=5.0)

    def write_block(self, address, data):
        if address % BLOCK_SIZE or len(data) != BLOCK_SIZE:
            raise ValueError("unaligned block write")
        block = address // BLOCK_SIZE
        self.command_ack(bytes([CMD_SLOT_WRITE, block >> 8, block & 0xFF]) + data)

    def validate_slot(self, slot):
        request = bytearray([CMD_SLOT_VALIDATE, slot, 0])
        request.append(checksum(request))
        self.port.reset_input_buffer()
        self.port.timeout = 10.0
        try:
            self.port.write(request)
            answer = self.port.read(8)
        finally:
            self.port.timeout = 1.0
        if len(answer) != 8 or answer[0] != CMD_SLOT_VALIDATE or checksum(answer[:7]) != answer[7]:
            raise IOError("bad answer to validate: %s" % (answer.hex() or "timeout"))
        return answer[2], struct.unpack("<I", answer[3:7])[0]


def parse_header(raw):
    magic, version, flags, size, crc, name, fw_version, _ = struct.unpack(HEADER_FORMAT, raw[:HEADER_SIZE])
    text = lambda b: b.split(b"\0", 1)[0].decode("ascii", "replace")
    if magic != SLOT_MAGIC:
        return None
    return {"version": version, "flags": flags, "size": size, "crc": crc,
            "name": text(name), "fw_version": text(fw_version)}


def info(radio):
    for slot in range(SLOT_COUNT):
        header = parse_header(radio.read_block(CMD_READ_SPI, slot * SLOT_STRIDE // BLOCK_SIZE))
        if header is None:
            print("slot %d: empty" % slot)
            continue
        state = "committed" if header["flags"] & FLAG_COMMITTED else "NOT committed"
        print("slot %d: %-16s %-16s %5d bytes  CRC %08X  v%d %s" % (
            slot, header["name"], header["fw_version"], header["size"], header["crc"], header["version"], state))


def write_slot(radio, slot, image, name, fw_version):
    if not 1 <= slot < SLOT_COUNT:
        raise ValueError("slots 1 to %d only (slot 0 is managed by the radio)" % (SLOT_COUNT - 1))
    if len(image) < 8 or len(image) > APP_SIZE:
        raise ValueError("image size must be 8 to %d bytes, got %d" % (APP_SIZE, len(image)))
    image += b"\xFF" * (-len(image) % 4)
    crc = zlib.crc32(image) & 0xFFFFFFFF
    sp, reset = struct.unpack("<II", image[:8])
    if not 0x20000000 <= sp < 0x20004000 or not reset & 1 or not 0x08001400 <= reset < 0x08001400 + len(image):
        raise ValueError("this does not look like an RT-890 application image (SP %08X, reset %08X)" % (sp, reset))

    base = slot * SLOT_STRIDE
    sys.stderr.write("erasing slot %d\n" % slot)
    radio.erase_slot(slot)

    padded = image + b"\xFF" * (-len(image) % BLOCK_SIZE)
    began = time.time()
    for offset in range(0, len(padded), BLOCK_SIZE):
        chunk = padded[offset:offset + BLOCK_SIZE]
        if chunk != b"\xFF" * BLOCK_SIZE:
            radio.write_block(base + SLOT_IMG_OFFSET + offset, chunk)
        if offset % (64 * BLOCK_SIZE) == 0 or offset + BLOCK_SIZE == len(padded):
            sys.stderr.write("\r0x%05X / 0x%05X  (%.0f s)" % (offset + BLOCK_SIZE, len(padded), time.time() - began))
            sys.stderr.flush()
    sys.stderr.write("\n")

    # Committed header last.
    header = struct.pack(HEADER_FORMAT, SLOT_MAGIC, HDR_VERSION, FLAG_COMMITTED, len(image), crc,
                         name.encode("ascii")[:16], fw_version.encode("ascii")[:16], b"\0" * 16)
    radio.write_block(base, header + b"\xFF" * (BLOCK_SIZE - HEADER_SIZE))

    status, radio_crc = radio.validate_slot(slot)
    if status != 0:
        raise IOError("slot %d refused by the radio: %s (radio CRC %08X, expected %08X)" % (
            slot, ERRORS.get(status, status), radio_crc, crc))
    print("slot %d: %s written and validated, %d bytes, CRC %08X" % (slot, name, len(image), crc))


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

    sub.add_parser("info", help="show the slot headers (read-only)")

    p = sub.add_parser("validate", help="full check of a slot by the radio (read-only)")
    p.add_argument("slot", type=int)

    p = sub.add_parser("write-slot", help="write a firmware image to slot 1-4")
    p.add_argument("slot", type=int)
    p.add_argument("image")
    p.add_argument("--name", help="slot name, 16 characters max (default: file name)")
    p.add_argument("--fw-version", default="", help="version string, 16 characters max")

    args = parser.parse_args()

    radio = Radio(args.port, args.baudrate)
    try:
        if args.command == "dump-spi":
            dump(radio, CMD_READ_SPI, parse_size(args.start), parse_size(args.size), args.output)
            print("saved %s" % args.output)
        elif args.command == "dump-int":
            dump(radio, CMD_READ_INT, 0, INT_FLASH_SIZE, args.output)
            print("saved %s" % args.output)
        elif args.command == "info":
            info(radio)
        elif args.command == "validate":
            status, crc = radio.validate_slot(args.slot)
            print("slot %d: %s, image CRC %08X" % (args.slot, ERRORS.get(status, status), crc))
            return 0 if status == 0 else 1
        elif args.command == "write-slot":
            with open(args.image, "rb") as f:
                image = f.read()
            name = args.name or args.image.replace("\\", "/").split("/")[-1].rsplit(".", 1)[0]
            write_slot(radio, args.slot, image, name, args.fw_version)
    except (IOError, ValueError) as e:
        sys.stderr.write("\nerror: %s\n" % e)
        return 1
    finally:
        radio.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
