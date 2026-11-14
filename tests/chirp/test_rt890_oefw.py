#!/usr/bin/env python3
# Copyright 2026 reppad
# https://github.com/reppad
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

"""Host test of the CHIRP driver tools/chirp/radtel_rt890_oefw.py.

The radio is simulated byte by byte as app/uart.c handles the UART, over a
simulated SPI flash with NOR semantics (a write can only clear bits). The
expected memory layout is built here from the C structures with struct, not
from the driver memory format, so the two are checked against each other.

Needs CHIRP (https://github.com/kk7ds/chirp) importable, for example:

  pip install -e /path/to/chirp
  python3 tests/chirp/test_rt890_oefw.py
"""

import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..",
                                "tools", "chirp"))

from chirp import chirp_common, errors, memmap  # noqa: E402
from chirp.settings import RadioSetting  # noqa: E402

import radtel_rt890_oefw as drv  # noqa: E402

FLASH_SIZE = 0x400000
SETTINGS = 0x3C1030
CHANNELS = 0x3C2000
DTMF = 0x3C9D20
TEMPLATE = 0x3CB000
EXT = 0x3D5000


def channel_bytes(rx, tx, rx_code=(3, 0), tx_code=(3, 0), golay=0,
                  is24=0, mute=0, encrypt=0, available=0, modulation=0,
                  bcl=0, scan_add=1, low=0, narrow=0, scramble=0,
                  lists=0xFF, name=b"          "):
    """ChannelInfo_t of radio/channels.h (GCC little endian bitfields)."""
    word = golay | (is24 << 28) | (mute << 29) | (encrypt << 30)
    flags = (available | (modulation << 1) | (bcl << 3) | (scan_add << 5) |
             (low << 6) | (narrow << 7))
    return struct.pack("<IHIHIBBBBBB10s", rx, rx_code[1] | (rx_code[0] << 12),
                       tx, tx_code[1] | (tx_code[0] << 12), word, flags,
                       0x11, scramble, lists, 0xFF, 0xFF, name)


class SimulatedRadio:
    """Byte level model of HandlerUSART1() and FlashCmd() of app/uart.c."""

    PAGES = {0x40: (0x000, 0x2D0), 0x41: (0x2D0, 0x028),
             0x42: (0x2F8, 0x022), 0x43: (0x31A, 0x002),
             0x47: (0x3B5, 0x00A), 0x48: (0x3BF, 0x001),
             0x49: (0x3C1, 0x00A), 0x4B: (0x3D8, 0x00A),
             0x4C: (0x31C, 0x099), 0x4D: (0x3D5, 0x001)}

    def __init__(self, flash, has_ext=True):
        self.flash = bytearray(flash)
        self.has_ext = has_ext
        self.timeout = 1
        self.rx = bytearray()
        self.buffer = bytearray()
        self.flashing = False
        self.region = 0
        self.rebooted = False
        self.commands = []

    # pyserial subset used by the driver
    def write(self, data):
        for byte in data:
            self._feed(byte)

    def read(self, size=1):
        data = bytes(self.rx[:size])
        del self.rx[:size]
        return data

    def reset_input_buffer(self):
        self.rx.clear()

    def flush(self):
        pass

    def _short(self, cmd):
        return cmd == 0x52 or (self.has_ext and cmd == 0x56)

    def _data(self, cmd):
        return 0x40 <= cmd <= (0x4D if self.has_ext else 0x4C)

    def _feed(self, byte):
        if self.rebooted:
            return
        self.buffer.append(byte)
        cmd = self.buffer[0]
        size = len(self.buffer)
        if size == 1 and cmd != 0x35 and not self._data(cmd) \
                and not self._short(cmd):
            self.rx.append(0xFF)
            self.buffer.clear()
            return
        if (cmd == 0x35 and size == 5) or (self._short(cmd) and size == 4) \
                or (self._data(cmd) and size == 132):
            frame = bytes(self.buffer)
            self.buffer.clear()
            if sum(frame[:-1]) & 0xFF != frame[-1]:
                self.rx.append(0xFF)
                return
            self.commands.append(frame[0])
            if cmd == 0x35:
                if frame[3] == 0x10:
                    self.rx.append(0x06)
                elif frame[3] == 0xEE and self.flashing:
                    if self.region == 2:
                        self.flash[TEMPLATE:TEMPLATE + 0xA000] = \
                            self.flash[0x3C1000:0x3CB000]
                    self.rebooted = True
            else:
                self._flash_cmd(cmd, frame[1], frame[2], frame[3:-1])

    def _flash_cmd(self, cmd, hi, lo, data):
        block = (hi << 8) | lo
        if cmd == 0x56:
            info = bytearray(128)
            info[0:8] = b"RT890CFW"
            info[8] = 1
            info[9] = 0x01
            info[16:23] = b"0123abc"
            frame = bytes([0x56, hi, lo]) + bytes(info)
            self.rx += frame + bytes([sum(frame) & 0xFF])
            return
        if cmd == 0x52:
            address = block * 128
            frame = bytes([0x52, hi, lo]) + self.flash[address:address + 128]
            self.rx += frame + bytes([sum(frame) & 0xFF])
            return
        page, count = self.PAGES.get(cmd, (0, 0))
        if cmd == 0x48:
            self.region = 1
        elif cmd == 0x49:
            self.region = 2
        if cmd == 0x4D and block >= 32:
            self.rx.append(0xFF)
            return
        self.flashing = True
        if block == 0:
            for i in range(count):
                start = (page + i) * 0x1000
                self.flash[start:start + 0x1000] = b"\xFF" * 0x1000
        address = page * 0x1000 + block * 128
        for i, value in enumerate(data):
            self.flash[address + i] &= value
        self.rx.append(0x06)


def factory_flash():
    flash = bytearray(b"\xFF" * FLASH_SIZE)
    flash[0x3C1000:0x3C1020] = b"OEFW RT-890".ljust(32, b" ")
    flash[0x3C1020:0x3C1030] = b"F4ABC".ljust(16, b" ")
    s = bytearray(b"\x00" * 0x60)
    # KeyBeep, DisplayLabel, DisplayLogo
    s[0x00] = (1 << 1) | (1 << 5) | (1 << 7)
    # FrequencyStep 8 (25K), RogerBeep 2, TxPriority 1
    s[0x01] = 8 | (2 << 5) | (1 << 7)
    # DualDisplay, RepeaterMode 1, Squelch 4
    s[0x02] = 1 | (1 << 2) | (4 << 4)
    s[0x03] = 4      # display timer: 30 s
    s[0x04] = 0      # lock timer: off
    s[0x05] = 42     # TOT: 600 s
    s[0x06] = 3 | (5 << 4)   # VoxDelay 3, VoxLevel 5
    struct.pack_into("<HH", s, 0x07, 1750, 1015)
    # WorkMode (VFO A) 1, WorkModeB (VFO B) 0, DualStandby 1
    s[0x0B] = (1 << 1) | (1 << 6)
    struct.pack_into("<HH", s, 0x0C, 0, 2)
    s[0x10:0x14] = bytes([2, 1, 12, 9])
    # bEnableDisplay, CurrentVfo
    s[0x14] = (1 << 1) | (1 << 5)
    s[0x15] = 10
    s[0x16] = 50
    struct.pack_into("<H", s, 0x17, 0x1234)
    struct.pack_into("<HHHH", s, 0x40, 0, 1, 2, 3)
    flash[SETTINGS:SETTINGS + 0x60] = s

    channels = {
        0: channel_bytes(14550000, 14550000, rx_code=(0, 885),
                         tx_code=(0, 885), name=b"S20       "),
        1: channel_bytes(43900000, 43140000, tx_code=(0, 1230),
                         rx_code=(2, 0o754), narrow=1, low=1, bcl=2,
                         scramble=3, encrypt=1, mute=1, is24=1,
                         golay=0x123456, lists=0b00000101, name=b"F1ZXX"
                         b"-R   "),
        2: channel_bytes(1450000, 1450000, modulation=3, name=b"USB       "),
        999: channel_bytes(14400000, 14400000, name=b"----------"),
        1000: channel_bytes(43502500, 43502500, name=b"----------"),
    }
    for index, data in channels.items():
        address = CHANNELS + index * 32
        flash[address:address + 32] = data

    # DTMF settings and codes
    flash[DTMF:DTMF + 7] = bytes([3, 5, 1, 2, 1, 10, 20])
    flash[0x3C9D30:0x3C9D40] = b"123A".ljust(14, b"\xFF") + b"\xFF\x04"
    flash[0x3C9E30:0x3C9E40] = b"AB#".ljust(14, b"\xFF") + b"\xFF\x03"
    return flash


class DriverTest(unittest.TestCase):

    def make_radio(self, flash=None, has_ext=True):
        sim = SimulatedRadio(flash or factory_flash(), has_ext)
        radio = drv.RT890OEFWRadio(sim)
        radio.status_fn = lambda status: None
        return radio, sim

    def download(self, **kwargs):
        radio, sim = self.make_radio(**kwargs)
        radio.sync_in()
        self.assertFalse(sim.rebooted)
        return radio, sim

    def settings(self, radio, tree=None):
        found = {}

        def walk(group):
            for element in group:
                if isinstance(element, RadioSetting):
                    found[element.get_name()] = element
                else:
                    walk(element)
        walk(tree if tree is not None else radio.get_settings())
        return found

    def test_download_memories(self):
        radio, _ = self.download()

        mem = radio.get_memory(1)
        self.assertEqual(mem.freq, 145500000)
        self.assertEqual(mem.duplex, "")
        self.assertEqual(mem.tmode, "TSQL")
        self.assertEqual(mem.ctone, 88.5)
        self.assertEqual(mem.mode, "FM")
        self.assertEqual(mem.power, drv.POWER_LEVELS[0])
        self.assertEqual(mem.name, "S20")

        mem = radio.get_memory(2)
        self.assertEqual(mem.freq, 439000000)
        self.assertEqual(mem.duplex, "-")
        self.assertEqual(mem.offset, 7600000)
        self.assertEqual(mem.tmode, "Cross")
        self.assertEqual(mem.cross_mode, "Tone->DTCS")
        self.assertEqual(mem.rtone, 123.0)
        self.assertEqual(mem.rx_dtcs, 754)
        self.assertEqual(mem.dtcs_polarity, "NR")
        self.assertEqual(mem.mode, "NFM")
        self.assertEqual(mem.power, drv.POWER_LEVELS[1])
        self.assertEqual(mem.name, "F1ZXX-R")
        extra = {s.get_name(): str(s.value) for s in mem.extra}
        self.assertEqual(extra["bcl"], "CTC/DCS")
        self.assertEqual(extra["scramble"], "3")
        self.assertEqual(extra["encrypt"], "Encrypt 1")
        self.assertEqual(extra["mute_code"], "24 bits")
        self.assertEqual(extra["golay"], str(0x123456))
        self.assertEqual(extra["scanlist1"], "True")
        self.assertEqual(extra["scanlist2"], "False")
        self.assertEqual(extra["scanlist3"], "True")

        self.assertEqual(radio.get_memory(3).mode, "USB")
        self.assertEqual(radio.get_memory(3).freq, 14500000)
        self.assertTrue(radio.get_memory(4).empty)

        vfo = radio.get_memory("VFO B")
        self.assertEqual(vfo.freq, 435025000)
        self.assertEqual(vfo.number, 1001)
        self.assertEqual(vfo.name, "----------")

    def test_download_settings(self):
        radio, _ = self.download()
        s = self.settings(radio)
        self.assertEqual(str(s["squelch"].value), "4")
        self.assertEqual(str(s["freq_step"].value), "25 kHz")
        self.assertEqual(str(s["roger_beep"].value), "Roger 2")
        self.assertEqual(str(s["tx_priority"].value), "Busy")
        self.assertEqual(str(s["repeater_mode"].value), "Talkaround")
        self.assertEqual(str(s["display_timer"].value), "30 s")
        self.assertEqual(str(s["lock_timer"].value), "Off")
        self.assertEqual(str(s["timeout_timer"].value), "600 s")
        self.assertEqual(str(s["vox_level"].value), "5")
        self.assertEqual(str(s["vox_delay"].value), "3")
        self.assertEqual(float(s["fm_frequency"].value), 101.5)
        self.assertEqual(int(s["tone_frequency"].value), 1750)
        self.assertEqual(str(s["work_mode"].value), "Channel")
        self.assertEqual(str(s["work_mode_b"].value), "Frequency (VFO)")
        self.assertEqual(str(s["current_vfo"].value), "B")
        self.assertEqual(int(s["vfo_ch_no1"].value), 3)
        self.assertEqual(int(s["preset_channel3"].value), 4)
        self.assertTrue(bool(s["key_beep"].value))
        self.assertTrue(bool(s["dual_standby"].value))
        self.assertFalse(bool(s["voice_prompt"].value))
        self.assertEqual(str(s["welcome"].value), "OEFW RT-890")
        self.assertEqual(str(s["device_name"].value), "F4ABC")
        self.assertEqual(int(s["border_color"].value), 0x1234)
        self.assertEqual(str(s["side_key2"].value), "Flashlight")
        # Erased extended settings: firmware defaults
        self.assertEqual(int(s["mic_gain"].value), 19)
        self.assertEqual(int(s["scan_delay"].value), 35)
        self.assertEqual(int(s["sq_rssi_base"].value), 0x5E)
        self.assertEqual(str(s["scan_resume"].value), "No resume")
        self.assertEqual(str(s["scan_list"].value), "All")
        self.assertEqual(str(s["key_shortcut0"].value), "FM radio")
        self.assertEqual(str(s["key_shortcut13"].value), "Dual display")
        # DTMF
        self.assertEqual(str(s["delay"].value), "300 ms")
        self.assertEqual(str(s["interval"].value), "80 ms")
        self.assertEqual(str(s["select"].value), "Contact 3")
        self.assertEqual(str(s["dtmf_contact0"].value), "123A")
        self.assertEqual(str(s["dtmf_kill"].value), "AB#")
        self.assertEqual(str(s["dtmf_contact1"].value), "")

    def edit_and_upload(self, has_ext=True):
        radio, _ = self.download()

        mem = chirp_common.Memory()
        mem.number = 5
        mem.freq = 446006250
        mem.mode = "NFM"
        mem.tmode = "DTCS"
        mem.dtcs = 23
        mem.dtcs_polarity = "RN"
        mem.name = "PMR1"
        mem.power = drv.POWER_LEVELS[1]
        radio.set_memory(mem)

        mem = radio.get_memory(2)
        mem.duplex = "split"
        mem.offset = 145000000
        for setting in mem.extra:
            if setting.get_name() == "scanlist8":
                setting.value = True
            elif setting.get_name() == "bcl":
                setting.value = "Carrier"
        radio.set_memory(mem)

        mem = radio.get_memory(1)
        mem.empty = True
        radio.set_memory(mem)

        tree = radio.get_settings()
        s = self.settings(radio, tree)
        s["squelch"].value = "7"
        s["dark_mode"].value = True
        s["mic_gain"].value = 25
        s["scan_list"].value = "3"
        s["key_shortcut2"].value = "Spectrum"
        s["timeout_timer"].value = "180 s"
        s["dtmf_contact1"].value = "*99#"
        s["welcome"].value = "HELLO"
        s["dtmf_state"].value = "Normal"
        s["work_mode"].value = "Frequency (VFO)"
        s["work_mode_b"].value = "Channel"
        radio.set_settings(tree)

        sim = SimulatedRadio(factory_flash(), has_ext)
        radio.set_pipe(sim)
        return radio, sim

    def test_upload(self):
        radio, sim = self.edit_and_upload()
        radio.sync_out()
        flash = sim.flash
        self.assertTrue(sim.rebooted)

        address = CHANNELS + 4 * 32
        self.assertEqual(
            bytes(flash[address:address + 32]),
            channel_bytes(44600625, 44600625, rx_code=(1, 0o023),
                          tx_code=(2, 0o023), narrow=1, low=1,
                          name=b"PMR1      "))

        address = CHANNELS + 1 * 32
        self.assertEqual(
            bytes(flash[address:address + 32]),
            channel_bytes(43900000, 14500000, tx_code=(0, 1230),
                          rx_code=(2, 0o754), narrow=1, low=1, bcl=1,
                          scramble=3, encrypt=1, mute=1, is24=1,
                          golay=0x123456, lists=0b10000101,
                          name=b"F1ZXX-R   "))

        address = CHANNELS
        self.assertEqual(bytes(flash[address:address + 22]), b"\xFF" * 22)
        self.assertEqual(bytes(flash[address + 22:address + 32]), b" " * 10)

        self.assertEqual(flash[SETTINGS + 2] >> 4, 7)
        self.assertEqual(flash[SETTINGS + 5], 14)   # 180 s
        self.assertEqual((flash[SETTINGS + 0x0B] >> 1) & 1, 0)  # VFO A: freq
        self.assertEqual((flash[SETTINGS + 0x0B] >> 2) & 1, 1)  # VFO B: mem
        self.assertEqual(bytes(flash[0x3C1000:0x3C1020]),
                         b"HELLO".ljust(32, b" "))
        self.assertEqual(bytes(flash[0x3C9D40:0x3C9D50]),
                         b"*99#".ljust(14, b"\xFF") + b"\xFF\x04")
        # Factory reset template updated by the radio at the end
        self.assertEqual(bytes(flash[TEMPLATE:TEMPLATE + 0xA000]),
                         bytes(flash[0x3C1000:0x3CB000]))

        ext = flash[EXT:EXT + 0x14]
        self.assertEqual((ext[0] >> 3) & 1, 1)     # dark mode
        self.assertEqual(ext[0] >> 5, 2)           # list 3
        self.assertEqual((ext[0x0F] >> 1) & 0x3F, 25)
        self.assertEqual(ext[0x0F] & 1, 0)         # not all lists
        self.assertEqual(ext[1 + 2], 26)           # key 2: spectrum
        # Untouched erased extended settings stay erased (firmware defaults)
        self.assertEqual(ext[0x10:0x13], b"\xFF\xFF\xFF")

        # Read back: same image
        image = radio.get_mmap().get_packed()
        radio2, _ = self.download(flash=flash)
        self.assertEqual(radio2.get_mmap().get_packed(), image)

    def test_upload_old_firmware(self):
        radio, sim = self.edit_and_upload(has_ext=False)
        before = bytes(sim.flash)
        with self.assertRaises(errors.RadioError):
            radio.sync_out()
        self.assertTrue(sim.rebooted)
        self.assertNotIn(0x4D, sim.commands)
        self.assertEqual(sim.flash[EXT:EXT + 0x1000], before[EXT:EXT + 0x1000])
        self.assertEqual(sim.flash[:0x3C1000], before[:0x3C1000])
        self.assertEqual(sim.flash[TEMPLATE + 0xA000:],
                         before[TEMPLATE + 0xA000:])
        self.assertEqual(sim.flash[SETTINGS + 2] >> 4, 7)

    def test_download_old_firmware(self):
        radio, sim = self.download(has_ext=False)
        self.assertEqual(radio.get_memory(1).name, "S20")
        # Only hello, the 0x52 reads and the end frame reached the handler
        self.assertEqual(set(sim.commands), {0x35, 0x52})

    def test_file_match(self):
        radio, _ = self.download()
        data = radio.get_mmap().get_packed()
        self.assertTrue(drv.RT890OEFWRadio.match_model(data, "x.img"))
        loaded = drv.RT890OEFWRadio(memmap.MemoryMapBytes(data))
        self.assertEqual(loaded.get_memory(2).name, "F1ZXX-R")


if __name__ == "__main__":
    unittest.main()
