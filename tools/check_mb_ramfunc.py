#!/usr/bin/env python3
# Copyright 2026 reppad
# https://github.com/reppad
#
# Port of cmake/check_mb_ramfunc.cmake from the UV-K1/UV-K5 V3 custom firmware,
# Copyright 2026 Armel F4HWN
# (https://github.com/armel/uv-k1-k5v3-firmware-custom, Apache License 2.0).
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

"""Post-link isolation check of the multiboot RAM-resident code.

The internal flash is unavailable while the restore routine runs, so every
direct branch must stay inside .mb_ramfunc, indirect branches other than
"bx lr" are refused, and the input object must not contain any relocation in
.mb_ramfunc (a relocation means a reference to code or data outside of it).
"""

import argparse
import re
import subprocess
import sys

BRANCH = re.compile(r"^(b|bl|blx|bx|cbz|cbnz)(eq|ne|cs|cc|hs|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al)?(\.[nw])?$")
INSN = re.compile(r"^\s*([0-9a-f]+):\s+([a-z][a-z0-9.]*)(?:\s+(.*))?$")
TARGET = re.compile(r"(?:^|[\s,])(?:0x)?([0-9a-f]+)(?:\s+<[^>]*>)?\s*$")


def run(objdump, *args):
    return subprocess.run([objdump] + list(args), check=True, capture_output=True, text=True).stdout


def fail(message):
    sys.stderr.write("check_mb_ramfunc: %s\n" % message)
    sys.exit(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--objdump", default="arm-none-eabi-objdump")
    parser.add_argument("elf")
    parser.add_argument("obj")
    args = parser.parse_args()

    match = re.search(r"\s\.mb_ramfunc\s+([0-9a-f]+)\s+([0-9a-f]+)", run(args.objdump, "-h", args.elf))
    if not match:
        fail(".mb_ramfunc not found in %s" % args.elf)
    size, start = int(match.group(1), 16), int(match.group(2), 16)
    end = start + size
    if size == 0:
        fail(".mb_ramfunc is empty")

    branches = 0
    for line in run(args.objdump, "--no-show-raw-insn", "-d", "-j", ".mb_ramfunc", args.elf).splitlines():
        insn = INSN.match(line)
        if not insn:
            continue
        mnemonic, operands = insn.group(2), (insn.group(3) or "").split(";")[0].strip()
        if not BRANCH.match(mnemonic):
            continue
        branches += 1
        if mnemonic.startswith("bx") or (mnemonic.startswith("blx") and not TARGET.search(operands)):
            if operands not in ("lr", "r14"):
                fail("unsafe indirect branch: %s" % line.strip())
            continue
        target = TARGET.search(operands)
        if not target:
            fail("unresolved branch: %s" % line.strip())
        address = int(target.group(1), 16)
        if not start <= address < end:
            fail("branch leaves .mb_ramfunc (0x%08x-0x%08x): %s" % (start, end, line.strip()))

    # Data accesses through registers cannot be checked by address: any
    # symbolic reference (flash constant, global variable, library call) shows
    # up as a relocation in the input object instead. Peripheral addresses are
    # plain constants and need none.
    relocations = run(args.objdump, "-r", "-j", ".mb_ramfunc", args.obj)
    if "R_ARM_" in relocations:
        fail("external reference in .mb_ramfunc of %s:\n%s" % (args.obj, relocations))

    print("Multiboot RAM code isolation OK: %d bytes at 0x%08x, %d branches checked" % (size, start, branches))


if __name__ == "__main__":
    main()
