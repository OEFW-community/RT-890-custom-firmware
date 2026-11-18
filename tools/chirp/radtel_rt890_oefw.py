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

"""CHIRP driver for the Radtel RT-890 running the OEFW community custom
firmware (https://github.com/OEFW-community/RT-890-custom-firmware).

The radio is programmed while powered on normally (not in bootloader mode),
over the programming cable at 115200 8N1, with the commands of app/uart.c:

  0x35 00 00 10 Sum         hello                       -> 06
  0x52 Hi Lo Sum            read 128 bytes at SPI flash address Hi:Lo * 128
                            -> 52 Hi Lo Data[128] Sum
  0x56 00 00 Sum            firmware information (if supported)
                            -> 56 00 00 Info[128] Sum
  0x49 Hi Lo Data[128] Sum  write block Hi:Lo of the configuration area
                            (0x3C1000-0x3CAFFF, erased when block 0 is
                            written)                    -> 06
  0x4D Hi Lo Data[128] Sum  write block Hi:Lo of the extended settings
                            sector (0x3D5000, erased when block 0 is
                            written, if supported)      -> 06
  0x35 00 00 EE Sum         end of session: after a write, the radio copies
                            the configuration to its factory reset template
                            and reboots

The image is the configuration area (settings, channels, VFOs, DTMF), then
the extended settings sector of the custom firmware.
"""

import logging
import time

from chirp import bitwise, chirp_common, directory, errors, memmap
from chirp.settings import (
    RadioSetting, RadioSettingGroup, RadioSettings,
    RadioSettingValueBoolean, RadioSettingValueFloat,
    RadioSettingValueInteger, RadioSettingValueList,
    RadioSettingValueString)

LOG = logging.getLogger(__name__)

BLOCK_SIZE = 128

# SPI flash layout
CONFIG_ADDR = 0x3C1000
CONFIG_SIZE = 0xA000
EXT_ADDR = 0x3D5000
EXT_SIZE = 0x1000

# Image layout
IMG_CONFIG = 0x0000
IMG_EXT = CONFIG_SIZE
MEM_SIZE = CONFIG_SIZE + EXT_SIZE

CMD_HELLO = 0x35
CMD_READ = 0x52
CMD_INFO = 0x56
CMD_WRITE_CONFIG = 0x49
CMD_WRITE_EXT = 0x4D
ACK = 0x06
NAK = 0xFF

INFO_MAGIC = b"RT890CFW"
INFO_FLAG_EXT_SETTINGS = 0x01
INFO_FLAG_MULTIBOOT = 0x02

CHANNEL_COUNT = 999
SPECIALS = {
    "VFO A": 999,
    "VFO B": 1000,
}

MEM_FORMAT = """
char welcome[32];
char device_name[16];

struct {
  u8 display_logo:1,
     display_voltage:1,
     display_label:1,
     unknown_00_4:1,
     startup_ringtone:1,
     voice_prompt:1,
     key_beep:1,
     unknown_00_0:1;
  u8 tx_priority:1,
     roger_beep:2,
     save_mode:1,
     freq_step:4;
  u8 squelch:4,
     repeater_mode:2,
     noaa_alarm:1,
     dual_display:1;
  u8 display_timer;
  u8 lock_timer;
  u8 timeout_timer;
  u8 vox_level:4,
     vox_delay:4;
  ul16 tone_frequency;
  ul16 fm_frequency;
  u8 fm_standby:1,
     dual_standby:1,
     standby_area:1,
     scan_direction:1,
     unknown_0b_1:1,
     work_mode_b:1,
     work_mode:1,
     unknown_0b_0:1;
  ul16 vfo_ch_no[2];
  u8 actions[4];
  u8 tx_lock:1,
     freq_lock:1,
     current_vfo:1,
     tail_tone:1,
     vox:1,
     lock:1,
     enable_display:1,
     use_vhf:1;
  u8 welcome_x;
  u8 welcome_y;
  ul16 border_color;
  u8 unknown_19[7];
  u8 band_info[8];
  u8 unknown_28[20];
  u8 dtmf_state;
  u8 unknown_3d[3];
  ul16 preset_channels[4];
  u8 unknown_48[24];
} settings;

#seekto 0x1000;
struct {
  ul32 rx_freq;
  ul16 rx_codetype:4,
       rx_code:12;
  ul32 tx_freq;
  ul16 tx_codetype:4,
       tx_code:12;
  ul32 encrypt:2,
       mute_enabled:1,
       is_24bit:1,
       unknown_0c:4,
       golay:24;
  u8 narrow:1,
     low_power:1,
     scan_add:1,
     bcl:2,
     modulation:2,
     empty:1;
  u8 unknown_11;
  u8 scramble;
  u8 scanlists;
  u8 unknown_14;
  u8 unknown_15;
  char name[10];
} channels[1001];

struct {
  u8 delay;
  u8 interval;
  u8 mode;
  u8 select;
  u8 display;
  u8 encode_gain;
  u8 decode_threshold;
} dtmf;

#seekto 0x8D30;
struct {
  char code[14];
  u8 padding;
  u8 length;
} dtmf_contacts[16];

struct {
  char code[14];
  u8 padding;
  u8 length;
} dtmf_kill;

struct {
  char code[14];
  u8 padding;
  u8 length;
} dtmf_stun;

struct {
  char code[14];
  u8 padding;
  u8 length;
} dtmf_wake;

#seekto 0xA000;
struct {
  u8 current_scan_list:3,
     scan_blink:1,
     dark_mode:1,
     am_fix:1,
     scan_resume:2;
  u8 key_shortcut[14];
  u8 undefined:1,
     mic_gain:6,
     scan_all:1;
  u8 sq_rssi_base;
  u8 sq_noise_base;
  u8 sq_glitch_base;
  u8 scan_delay:6,
     sq_mode:2;
} ext;
"""

# Code types of app/css.h
CODE_CTCSS = 0
CODE_DCS_N = 1
CODE_DCS_I = 2
CODE_OFF = 3

# DCS codes offered by the radio menu (DCS_Options of app/css.c)
DTCS_CODES = (
    23, 25, 26, 31, 32, 36, 43, 47, 51, 53, 54, 65, 71, 72, 73, 74,
    114, 115, 116, 122, 125, 131, 132, 134, 143, 145, 152, 155, 156, 162,
    165, 172, 174, 205, 212, 223, 225, 226, 243, 244, 245, 246, 251, 252,
    255, 261, 263, 265, 266, 271, 274, 306, 311, 315, 325, 331, 332, 343,
    346, 351, 356, 364, 365, 371, 411, 412, 413, 423, 431, 432, 445, 446,
    452, 454, 455, 462, 464, 465, 466, 503, 506, 516, 523, 526, 532, 546,
    565, 606, 612, 624, 627, 631, 632, 645, 654, 662, 664, 703, 712, 723,
    731, 732, 734, 743, 754,
)

POWER_LEVELS = [
    chirp_common.PowerLevel("High", watts=5.00),
    chirp_common.PowerLevel("Low", watts=1.00),
]

MODES = ["FM", "NFM", "AM", "NAM", "LSB", "USB"]
MODULATIONS = ["FM", "AM", "LSB", "USB"]

# Channel name: 10 characters, space padded
NAME_LENGTH = 10
VALID_CHARS = chirp_common.CHARSET_ASCII

BCL_LIST = ["Off", "Carrier", "CTC/DCS"]
SCRAMBLER_LIST = ["Off"] + [str(x) for x in range(1, 9)]
ENCRYPT_LIST = ["Off", "Encrypt 1", "Encrypt 2", "Encrypt 3"]
MUTE_CODE_LIST = ["Off", "23 bits", "24 bits"]

STEP_LIST = ["0.25 kHz", "1.25 kHz", "2.5 kHz", "5 kHz", "6.25 kHz",
             "10 kHz", "12.5 kHz", "20 kHz", "25 kHz", "50 kHz", "100 kHz",
             "500 kHz", "1 MHz", "5 MHz", "0.01 kHz"]
REPEATER_MODE_LIST = ["Off", "Talkaround", "Frequency reversal"]
ROGER_LIST = ["Off", "Roger 1", "Roger 2", "Send ID"]
TX_PRIORITY_LIST = ["Edit", "Busy"]
SCAN_DIRECTION_LIST = ["Up", "Down"]
SCAN_RESUME_LIST = ["Carrier operated", "Time operated", "No resume"]
SCAN_LIST_LIST = [str(x) for x in range(1, 9)] + ["All"]
SQUELCH_MODE_LIST = ["RSSI", "RSSI + Noise", "RSSI + Glitch",
                     "RSSI + Noise + Glitch"]
WORK_MODE_LIST = ["Frequency (VFO)", "Channel"]
VFO_LIST = ["A", "B"]
DTMF_MODE_LIST = ["Off", "TX start", "TX end", "TX start and end"]
DTMF_STATE_LIST = ["Normal", "Stunned", "Killed"]
DTMF_DELAY_LIST = ["%d ms" % (x * 100) for x in range(21)]
DTMF_INTERVAL_LIST = ["%d ms" % ((x + 3) * 10) for x in range(18)]
DTMF_SELECT_LIST = ["Contact %d" % x for x in range(1, 17)]
DTMF_CHARS = "0123456789ABCD*#"


def _timer_label(index):
    # helper/helper.c TIMER_Calculate()
    if index == 0:
        return "Off"
    if index < 4:
        return "%d s" % (index * 5)
    return "%d s" % ((index - 2) * 15)


TIMER_LIST = [_timer_label(x) for x in range(43)]

# Actions of task/keyaction.h, in order
ACTION_LIST = [
    "None", "Monitor", "Frequency detect", "Repeater mode", "Preset channel",
    "Local alarm", "Remote alarm", "NOAA", "Send tone", "Roger beep",
    "FM radio", "Frequency scanner", "Flashlight", "AM fix", "VOX",
    "TX power", "Squelch level", "Dual standby", "Backlight",
    "Frequency step", "Key beep", "Toggle scan list", "DTMF decode",
    "Dual display", "TX frequency", "Lock", "Spectrum", "Dark mode",
    "AGC mode", "Register editor", "Mic gain", "Modulation", "Bandwidth",
]
SIDE_KEYS = ["Side key 1 long press", "Side key 1 short press",
             "Side key 2 long press", "Side key 2 short press"]
SHORTCUT_KEYS = ["Key %s long press" % k for k in
                 ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "#",
                  "Menu", "Exit"]]
# task/keyaction.c SetDefaultKeyShortcuts()
DEFAULT_SIDE_KEYS = [2, 1, 12, 9]
DEFAULT_SHORTCUTS = [10, 11, 13, 14, 15, 16, 17, 18, 19, 4, 24, 25, 22, 23]

# radio/settings.c SETTINGS_LoadSettings() defaults for an erased sector
DEFAULT_MIC_GAIN = 19
DEFAULT_SCAN_DELAY = 35
DEFAULT_SQ_RSSI = 0x5E
DEFAULT_SQ_NOISE = 0x44
DEFAULT_SQ_GLITCH = 0x11

# radio/channels.c VfoTemplate
VFO_DEFAULT_FREQ = {999: 430025000, 1000: 435025000}


def _checksum(data):
    return sum(data) & 0xFF


def _frame(command, hi, lo, data=b""):
    frame = bytes([command, hi, lo]) + bytes(data)
    return frame + bytes([_checksum(frame)])


def _clamp(value, count, default):
    value = int(value)
    return value if 0 <= value < count else default


def _dcs_to_raw(code):
    """DCS code as printed (e.g. 754) to the 9-bit value of the radio."""
    return int(str(code), 8)


def _raw_to_dcs(raw):
    return int("%o" % (int(raw) & 0x1FF))


def _get_string(field, length=None):
    raw = bytes(int(x) for x in field.get_raw())
    if length is not None:
        raw = raw[:length]
    out = ""
    for byte in raw:
        if byte in (0x00, 0xFF):
            break
        char = chr(byte)
        out += char if char in VALID_CHARS else " "
    return out.rstrip()


def _set_string(field, value, size):
    value = str(value)[:size].ljust(size)
    field.set_raw(value.encode("ascii", "replace"))


class _Session:
    """Programming session with the radio over its programming cable."""

    def __init__(self, radio):
        self.radio = radio
        self.pipe = radio.pipe

    def _drain(self):
        time.sleep(0.1)
        self.pipe.reset_input_buffer()

    def _read_exact(self, size, timeout):
        self.pipe.timeout = timeout
        data = b""
        deadline = time.monotonic() + timeout
        while len(data) < size and time.monotonic() < deadline:
            chunk = self.pipe.read(size - len(data))
            if chunk:
                data += chunk
        return data

    def hello(self):
        last = b""
        for _ in range(3):
            self.pipe.reset_input_buffer()
            self.pipe.write(_frame(CMD_HELLO, 0, 0, b"\x10"))
            last = self._read_exact(1, 1.0)
            if last == bytes([ACK]):
                return
            self._drain()
        if not last:
            raise errors.RadioError(
                "No response from the radio. Check the cable and make sure "
                "the radio is powered on normally (not in bootloader mode).")
        raise errors.RadioError(
            "Unexpected answer from the radio (0x%02X)" % last[0])

    def end(self):
        # Not acknowledged. After a write, the radio reboots.
        self.pipe.write(_frame(CMD_HELLO, 0, 0, b"\xEE"))
        self.pipe.flush()

    def _query(self, command, block, refusal_ok=False):
        request = _frame(command, (block >> 8) & 0xFF, block & 0xFF)
        error = "no answer"
        for _ in range(3):
            self.pipe.reset_input_buffer()
            self.pipe.write(request)
            first = self._read_exact(1, 1.0)
            if not first:
                error = "timeout"
                continue
            if first[0] != command:
                error = "refused (0x%02X)" % first[0]
                self._drain()
                if refusal_ok:
                    return None
                continue
            frame = first + self._read_exact(BLOCK_SIZE + 3, 1.0)
            if len(frame) != BLOCK_SIZE + 4:
                error = "short answer (%d bytes)" % len(frame)
                self._drain()
                continue
            if frame[1:3] != request[1:3]:
                error = "wrong block in answer"
                self._drain()
                continue
            if _checksum(frame[:-1]) != frame[-1]:
                error = "bad checksum"
                continue
            return frame[3:3 + BLOCK_SIZE]
        raise errors.RadioError(
            "Command 0x%02X block 0x%04X: %s" % (command, block, error))

    def read_block(self, address):
        return self._query(CMD_READ, address // BLOCK_SIZE)

    def info(self):
        """Firmware information, or None if the firmware has no command
        0x56. Probing is safe: an older firmware rejects every byte of the
        request on its own with 0xFF."""
        try:
            data = self._query(CMD_INFO, 0, refusal_ok=True)
        except errors.RadioError:
            self._drain()
            return None
        if data is None or not data.startswith(INFO_MAGIC):
            return None
        return {
            "version": data[8],
            "flags": data[9],
            "build": data[16:32].split(b"\x00")[0].decode("ascii", "replace"),
        }

    def write_block(self, command, block, data, timeout):
        request = _frame(command, (block >> 8) & 0xFF, block & 0xFF, data)
        answer = b""
        for _ in range(3):
            self.pipe.reset_input_buffer()
            self.pipe.write(request)
            answer = self._read_exact(1, timeout)
            if answer == bytes([ACK]):
                return
            self._drain()
        raise errors.RadioError(
            "Command 0x%02X block 0x%04X refused (%s)" % (
                command, block, answer.hex() or "timeout"))


@directory.register
class RT890OEFWRadio(chirp_common.CloneModeRadio):
    """Radtel RT-890 with the OEFW community custom firmware"""
    VENDOR = "Radtel"
    MODEL = "RT-890"
    VARIANT = "OEFW"
    BAUD_RATE = 115200
    NEEDS_COMPAT_SERIAL = False

    _memsize = MEM_SIZE

    @classmethod
    def get_prompts(cls):
        rp = chirp_common.RadioPrompts()
        rp.experimental = (
            "This driver is for the RT-890 running the OEFW community custom "
            "firmware only. Back up the SPI flash of the radio before "
            "writing to it.")
        rp.pre_download = (
            "1. Turn the radio on normally (not in bootloader mode).\n"
            "2. Connect the programming cable.\n"
            "3. Click OK to read the radio.")
        rp.pre_upload = (
            "1. Turn the radio on normally (not in bootloader mode).\n"
            "2. Connect the programming cable.\n"
            "3. Click OK to write the radio. It reboots at the end.\n\n"
            "The extended settings (scan, squelch, key shortcuts, dark "
            "theme, mic gain, AM fix) are only written by a firmware that "
            "supports command 0x4D.")
        return rp

    @classmethod
    def match_model(cls, filedata, filename):
        return len(filedata) == MEM_SIZE

    def get_features(self):
        rf = chirp_common.RadioFeatures()
        rf.has_settings = True
        rf.has_bank = False
        rf.has_ctone = True
        rf.has_cross = True
        rf.has_rx_dtcs = True
        rf.has_dtcs_polarity = True
        rf.has_tuning_step = False
        rf.has_nostep_tuning = True
        rf.has_comment = False
        rf.can_odd_split = True
        rf.valid_name_length = NAME_LENGTH
        rf.valid_characters = VALID_CHARS
        rf.valid_modes = list(MODES)
        rf.valid_tmodes = ["", "Tone", "TSQL", "DTCS", "Cross"]
        rf.valid_cross_modes = ["Tone->Tone", "Tone->DTCS", "DTCS->Tone",
                                "->Tone", "->DTCS", "DTCS->", "DTCS->DTCS"]
        rf.valid_duplexes = ["", "-", "+", "split"]
        rf.valid_power_levels = POWER_LEVELS
        rf.valid_skips = []
        rf.valid_tones = chirp_common.TONES
        rf.valid_dtcs_codes = list(DTCS_CODES)
        rf.valid_dtcs_pols = ["NN", "NR", "RN", "RR"]
        rf.valid_bands = [(10000000, 1300000000)]
        rf.valid_tuning_steps = [0.01, 0.25, 1.25, 2.5, 5.0, 6.25, 10.0,
                                 12.5, 20.0, 25.0, 50.0, 100.0, 500.0,
                                 1000.0, 5000.0]
        rf.valid_special_chans = list(SPECIALS.keys())
        rf.memory_bounds = (1, CHANNEL_COUNT)
        return rf

    def process_mmap(self):
        self._memobj = bitwise.parse(MEM_FORMAT, self._mmap)

    # ------------------------------------------------------------------
    # Clone

    def _status(self, cur, total, msg):
        status = chirp_common.Status()
        status.cur = cur
        status.max = total
        status.msg = msg
        self.status_fn(status)

    def sync_in(self):
        try:
            self._sync_in()
        except errors.RadioError:
            raise
        except Exception as e:
            LOG.exception("Download failed")
            raise errors.RadioError("Failed to communicate with the radio: "
                                    "%s" % e)

    def sync_out(self):
        try:
            self._sync_out()
        except errors.RadioError:
            raise
        except Exception as e:
            LOG.exception("Upload failed")
            raise errors.RadioError("Failed to communicate with the radio: "
                                    "%s" % e)

    def _sync_in(self):
        session = _Session(self)
        session.hello()
        info = session.info()
        LOG.info("Firmware information: %s", info)
        data = b""
        areas = [(CONFIG_ADDR, CONFIG_SIZE), (EXT_ADDR, EXT_SIZE)]
        total = MEM_SIZE // BLOCK_SIZE
        done = 0
        try:
            for base, size in areas:
                for offset in range(0, size, BLOCK_SIZE):
                    data += session.read_block(base + offset)
                    done += 1
                    self._status(done, total, "Reading from radio")
        finally:
            session.end()
        self._mmap = memmap.MemoryMapBytes(data)
        self.process_mmap()

    def _sync_out(self):
        session = _Session(self)
        session.hello()
        info = session.info()
        LOG.info("Firmware information: %s", info)
        ext_supported = bool(info and info["flags"] & INFO_FLAG_EXT_SETTINGS)
        image = self._mmap.get_byte_compatible().get_packed()

        jobs = []
        if ext_supported:
            for offset in range(0, EXT_SIZE, BLOCK_SIZE):
                jobs.append((CMD_WRITE_EXT, offset // BLOCK_SIZE,
                             image[IMG_EXT + offset:
                                   IMG_EXT + offset + BLOCK_SIZE]))
        else:
            LOG.warning("Firmware without command 0x4D: extended settings "
                        "are not written")
        for offset in range(0, CONFIG_SIZE, BLOCK_SIZE):
            jobs.append((CMD_WRITE_CONFIG, offset // BLOCK_SIZE,
                         image[IMG_CONFIG + offset:
                               IMG_CONFIG + offset + BLOCK_SIZE]))

        try:
            for index, (command, block, data) in enumerate(jobs):
                # Block 0 erases the whole area before it is acknowledged.
                timeout = 10.0 if block == 0 else 1.0
                session.write_block(command, block, data, timeout)
                self._status(index + 1, len(jobs), "Writing to radio")
        finally:
            session.end()

        if not ext_supported:
            raise errors.RadioError(
                "Channels and settings were written, but this firmware "
                "cannot receive the extended settings (scan, squelch, key "
                "shortcuts, dark theme, mic gain, AM fix). Update the "
                "firmware to write them.")

    # ------------------------------------------------------------------
    # Memories

    @staticmethod
    def _index(number):
        if isinstance(number, str):
            return SPECIALS[number]
        return number - 1

    def get_raw_memory(self, number):
        return repr(self._memobj.channels[self._index(number)])

    @staticmethod
    def _decode_tone(codetype, code):
        codetype = int(codetype)
        code = int(code)
        if codetype == CODE_CTCSS:
            tone = code / 10.0
            if chirp_common.VALIDTONE(tone):
                return "Tone", tone, None
        elif codetype in (CODE_DCS_N, CODE_DCS_I):
            return ("DTCS", _raw_to_dcs(code),
                    "N" if codetype == CODE_DCS_N else "R")
        return "", None, None

    @staticmethod
    def _encode_tone(mode, value, polarity):
        if mode == "Tone":
            return CODE_CTCSS, int(round(value * 10))
        if mode == "DTCS":
            return (CODE_DCS_N if polarity == "N" else CODE_DCS_I,
                    _dcs_to_raw(value))
        return CODE_OFF, 0

    def get_memory(self, number):
        index = self._index(number)
        _mem = self._memobj.channels[index]

        mem = chirp_common.Memory()
        if index >= CHANNEL_COUNT:
            mem.number = index + 1
            mem.extd_number = [k for k, v in SPECIALS.items()
                               if v == index][0]
            mem.immutable = ["empty"]
        else:
            mem.number = number

        is_special = index >= CHANNEL_COUNT
        invalid = int(_mem.rx_freq) in (0, 0xFFFFFFFF)
        if not is_special and (_mem.empty or invalid):
            mem.empty = True
            return mem

        if invalid:
            mem.freq = VFO_DEFAULT_FREQ[index]
        else:
            mem.freq = int(_mem.rx_freq) * 10

        tx_freq = int(_mem.tx_freq)
        if invalid or tx_freq in (0, 0xFFFFFFFF):
            tx_freq = mem.freq
        else:
            tx_freq *= 10
        if tx_freq == mem.freq:
            mem.duplex = ""
            mem.offset = 0
        elif abs(tx_freq - mem.freq) > 70000000:
            mem.duplex = "split"
            mem.offset = tx_freq
        elif tx_freq > mem.freq:
            mem.duplex = "+"
            mem.offset = tx_freq - mem.freq
        else:
            mem.duplex = "-"
            mem.offset = mem.freq - tx_freq

        txtone = self._decode_tone(_mem.tx_codetype, _mem.tx_code)
        rxtone = self._decode_tone(_mem.rx_codetype, _mem.rx_code)
        chirp_common.split_tone_decode(mem, txtone, rxtone)

        modulation = int(_mem.modulation)
        if modulation == 0:
            mem.mode = "NFM" if _mem.narrow else "FM"
        elif modulation == 1:
            mem.mode = "NAM" if _mem.narrow else "AM"
        else:
            mem.mode = MODULATIONS[modulation]

        mem.power = POWER_LEVELS[1 if _mem.low_power else 0]
        mem.name = _get_string(_mem.name)

        mem.extra = RadioSettingGroup("extra", "Extra")

        rs = RadioSetting("bcl", "Busy channel lockout",
                          RadioSettingValueList(
                              BCL_LIST,
                              current_index=_clamp(_mem.bcl, 3, 0)))
        mem.extra.append(rs)

        rs = RadioSetting("scramble", "Scrambler",
                          RadioSettingValueList(
                              SCRAMBLER_LIST,
                              current_index=_clamp(_mem.scramble, 9, 0)))
        mem.extra.append(rs)

        rs = RadioSetting("encrypt", "DCS encrypt",
                          RadioSettingValueList(
                              ENCRYPT_LIST,
                              current_index=int(_mem.encrypt)))
        mem.extra.append(rs)

        if not _mem.mute_enabled:
            mute = 0
        else:
            mute = 2 if _mem.is_24bit else 1
        rs = RadioSetting("mute_code", "DCS mute code",
                          RadioSettingValueList(MUTE_CODE_LIST,
                                                current_index=mute))
        mem.extra.append(rs)

        rs = RadioSetting("golay", "DCS mute code value",
                          RadioSettingValueInteger(0, 0xFFFFFF,
                                                   int(_mem.golay)))
        mem.extra.append(rs)

        scanlists = int(_mem.scanlists)
        for i in range(8):
            rs = RadioSetting("scanlist%d" % (i + 1),
                              "In scan list %d" % (i + 1),
                              RadioSettingValueBoolean(
                                  bool(scanlists & (1 << i))))
            mem.extra.append(rs)

        return mem

    @staticmethod
    def _init_channel(_mem):
        """Same defaults as a channel saved from the radio menu."""
        _mem.fill_raw(b"\x00")
        _mem.rx_codetype = CODE_OFF
        _mem.tx_codetype = CODE_OFF
        _mem.scan_add = 1
        _mem.unknown_11 = 0x11
        _mem.scanlists = 0xFF
        _mem.unknown_14 = 0xFF
        _mem.unknown_15 = 0xFF
        _set_string(_mem.name, "", NAME_LENGTH)

    def set_memory(self, mem):
        index = self._index(mem.extd_number or mem.number)
        _mem = self._memobj.channels[index]

        if mem.empty:
            if index >= CHANNEL_COUNT:
                raise errors.InvalidValueError("VFOs cannot be deleted")
            # As "Delete CH" in the radio menu
            _mem.fill_raw(b"\xFF")
            _set_string(_mem.name, "", NAME_LENGTH)
            return

        if _mem.empty or int(_mem.rx_freq) in (0, 0xFFFFFFFF):
            self._init_channel(_mem)
        _mem.empty = 0

        _mem.rx_freq = mem.freq // 10
        if mem.duplex == "+":
            tx_freq = mem.freq + mem.offset
        elif mem.duplex == "-":
            tx_freq = mem.freq - mem.offset
        elif mem.duplex == "split":
            tx_freq = mem.offset
        else:
            tx_freq = mem.freq
        _mem.tx_freq = tx_freq // 10

        txtone, rxtone = chirp_common.split_tone_encode(mem)
        _mem.tx_codetype, _mem.tx_code = self._encode_tone(*txtone)
        _mem.rx_codetype, _mem.rx_code = self._encode_tone(*rxtone)

        if mem.mode in ("FM", "NFM"):
            _mem.modulation = 0
            _mem.narrow = mem.mode == "NFM"
        elif mem.mode in ("AM", "NAM"):
            _mem.modulation = 1
            _mem.narrow = mem.mode == "NAM"
        else:
            # The bandwidth setting is kept for SSB
            _mem.modulation = MODULATIONS.index(mem.mode)

        _mem.low_power = mem.power == POWER_LEVELS[1]
        _set_string(_mem.name, mem.name.rstrip(), NAME_LENGTH)

        for setting in mem.extra:
            name = setting.get_name()
            value = setting.value
            if name == "bcl":
                _mem.bcl = BCL_LIST.index(str(value))
            elif name == "scramble":
                _mem.scramble = SCRAMBLER_LIST.index(str(value))
            elif name == "encrypt":
                _mem.encrypt = ENCRYPT_LIST.index(str(value))
            elif name == "mute_code":
                mute = MUTE_CODE_LIST.index(str(value))
                _mem.mute_enabled = mute != 0
                _mem.is_24bit = mute == 2
            elif name == "golay":
                _mem.golay = int(value)
            elif name.startswith("scanlist"):
                bit = 1 << (int(name[8:]) - 1)
                if bool(value):
                    _mem.scanlists |= bit
                else:
                    _mem.scanlists &= ~bit & 0xFF

    # ------------------------------------------------------------------
    # Settings

    @staticmethod
    def _add(group, name, label, value, callback, doc=None):
        rs = RadioSetting(name, label, value)
        rs.set_apply_callback(callback)
        if doc:
            rs.set_doc(doc)
        group.append(rs)
        return rs

    def _add_list(self, group, name, label, options, index, setter,
                  doc=None):
        def apply(setting):
            setter(options.index(str(setting.value)))
        value = RadioSettingValueList(options, current_index=index)
        return self._add(group, name, label, value, apply, doc)

    def _add_field_list(self, group, obj, field, label, options,
                        default=0, doc=None):
        index = _clamp(getattr(obj, field), len(options), default)
        return self._add_list(group, field, label, options, index,
                              lambda v: setattr(obj, field, v), doc)

    def _add_bool(self, group, obj, field, label, doc=None, name=None):
        def apply(setting):
            setattr(obj, field, 1 if setting.value else 0)
        value = RadioSettingValueBoolean(bool(getattr(obj, field)))
        return self._add(group, name or field, label, value, apply, doc)

    def _add_int(self, group, name, label, minimum, maximum, current,
                 setter, doc=None):
        def apply(setting):
            setter(int(setting.value))
        current = min(max(int(current), minimum), maximum)
        value = RadioSettingValueInteger(minimum, maximum, current)
        return self._add(group, name, label, value, apply, doc)

    def _add_text(self, group, name, label, field, size, doc=None):
        def apply(setting):
            _set_string(field, str(setting.value).rstrip(), size)
        value = RadioSettingValueString(0, size, _get_string(field),
                                        autopad=False,
                                        charset=VALID_CHARS)
        return self._add(group, name, label, value, apply, doc)

    def _add_dtmf_code(self, group, name, label, obj, doc=None):
        def apply(setting):
            code = str(setting.value).strip().upper()
            raw = code.encode("ascii") + b"\xFF" * (14 - len(code))
            obj.code.set_raw(raw)
            obj.padding = 0xFF
            obj.length = len(code)
        length = int(obj.length)
        if length > 14:
            current = ""
        else:
            raw = bytes(int(x) for x in obj.code.get_raw())[:length]
            current = "".join(chr(c) for c in raw if chr(c) in DTMF_CHARS)
        value = RadioSettingValueString(0, 14, current, autopad=False,
                                        charset=DTMF_CHARS)
        return self._add(group, name, label, value, apply, doc)

    def _get_basic(self):
        _set = self._memobj.settings
        group = RadioSettingGroup("basic", "Basic settings")

        self._add_field_list(group, _set, "squelch", "Squelch level",
                             [str(x) for x in range(10)], 0)
        self._add_field_list(group, _set, "freq_step", "VFO step",
                             STEP_LIST, 8)
        self._add_field_list(group, _set, "repeater_mode", "Repeater mode",
                             REPEATER_MODE_LIST)
        self._add_field_list(group, _set, "tx_priority", "TX priority",
                             TX_PRIORITY_LIST)
        self._add_bool(group, _set, "tail_tone", "TX tail tone")
        self._add_field_list(group, _set, "roger_beep", "Roger beep",
                             ROGER_LIST)
        self._add_field_list(group, _set, "timeout_timer",
                             "TX timeout timer (TOT)", TIMER_LIST)
        self._add_bool(group, _set, "vox", "VOX")
        self._add_field_list(group, _set, "vox_level", "VOX level",
                             [str(x) for x in range(10)])
        self._add_field_list(group, _set, "vox_delay", "VOX delay",
                             [str(x) for x in range(10)])
        self._add_bool(group, _set, "save_mode", "RX save mode")
        self._add_field_list(group, _set, "scan_direction", "Scan direction",
                             SCAN_DIRECTION_LIST)
        self._add_bool(group, _set, "dual_display", "Dual display")
        self._add_bool(group, _set, "dual_standby", "Dual standby")
        self._add_bool(group, _set, "fm_standby", "FM standby")
        self._add_bool(group, _set, "noaa_alarm", "NOAA monitor",
                       doc="Only with a firmware built with NOAA support")
        self._add_bool(group, _set, "key_beep", "Key beep")
        self._add_bool(group, _set, "voice_prompt", "Voice prompt",
                       doc="Not available in multiboot builds")
        self._add_field_list(group, _set, "display_timer", "Screen timer",
                             TIMER_LIST)
        self._add_field_list(group, _set, "lock_timer", "Key lock timer",
                             TIMER_LIST)
        self._add_bool(group, _set, "lock", "Keypad locked")
        self._add_bool(group, _set, "freq_lock",
                       "Frequency lock (108-136, 144-146, 430-440 MHz)",
                       doc="Same as holding side key 2 and # at power on")
        self._add_bool(group, _set, "tx_lock", "TX lock (RX only)",
                       doc="Same as holding side key 1 and # at power on")

        def set_fm(value):
            _set.fm_frequency = int(round(value * 10))
        fm = int(_set.fm_frequency)
        fm = fm / 10.0 if 640 <= fm <= 1080 else 88.0
        self._add(group, "fm_frequency", "FM radio frequency (MHz)",
                  RadioSettingValueFloat(64.0, 108.0, fm, 0.1, 1),
                  lambda s: set_fm(float(s.value)))

        self._add_int(group, "tone_frequency", "Send tone frequency (Hz)",
                      0, 6000, _set.tone_frequency,
                      lambda v: setattr(_set, "tone_frequency", v))
        return group

    def _get_work_mode(self):
        _set = self._memobj.settings
        group = RadioSettingGroup("work", "Work mode")

        # Each VFO has its own mode: work_mode is VFO A, work_mode_b VFO B.
        self._add_field_list(group, _set, "work_mode",
                             "VFO %s work mode" % VFO_LIST[0], WORK_MODE_LIST)
        self._add_field_list(group, _set, "work_mode_b",
                             "VFO %s work mode" % VFO_LIST[1], WORK_MODE_LIST)
        self._add_field_list(group, _set, "current_vfo", "Active VFO",
                             VFO_LIST)
        for vfo in range(2):
            def setter(value, vfo=vfo):
                _set.vfo_ch_no[vfo] = value - 1
            self._add_int(group, "vfo_ch_no%d" % vfo,
                          "VFO %s channel" % VFO_LIST[vfo], 1, CHANNEL_COUNT,
                          _clamp(_set.vfo_ch_no[vfo], CHANNEL_COUNT, 0) + 1,
                          setter)
        for slot in range(4):
            def setter(value, slot=slot):
                _set.preset_channels[slot] = value - 1
            self._add_int(group, "preset_channel%d" % slot,
                          "Preset channel %d" % (slot + 1), 1, CHANNEL_COUNT,
                          _clamp(_set.preset_channels[slot], CHANNEL_COUNT,
                                 0) + 1,
                          setter)
        return group

    def _get_display(self):
        _mem = self._memobj
        _set = _mem.settings
        group = RadioSettingGroup("display", "Display and startup")

        self._add_bool(group, _set, "display_logo", "Startup logo")
        self._add_bool(group, _set, "display_voltage", "Startup voltage")
        self._add_bool(group, _set, "startup_ringtone", "Startup tone")
        self._add_bool(group, _set, "display_label", "Startup text")
        self._add_text(group, "welcome", "Startup text content",
                       _mem.welcome, 32)
        self._add_int(group, "welcome_x", "Startup text X position", 0, 159,
                      _set.welcome_x,
                      lambda v: setattr(_set, "welcome_x", v))
        self._add_int(group, "welcome_y", "Startup text Y position", 0, 127,
                      _set.welcome_y,
                      lambda v: setattr(_set, "welcome_y", v))
        self._add_text(group, "device_name", "Personal ID",
                       _mem.device_name, 16)
        self._add_int(group, "border_color", "Border color (RGB565)",
                      0, 0xFFFF, _set.border_color,
                      lambda v: setattr(_set, "border_color", v))
        return group

    def _get_extended(self):
        _ext = self._memobj.ext
        group = RadioSettingGroup("extended", "Extended settings")

        self._add_bool(group, _ext, "dark_mode", "Dark theme")
        self._add_bool(group, _ext, "am_fix", "AM fix")
        self._add_int(group, "mic_gain", "Mic gain", 0, 31,
                      _ext.mic_gain if _ext.mic_gain <= 31
                      else DEFAULT_MIC_GAIN,
                      lambda v: setattr(_ext, "mic_gain", v))
        self._add_int(group, "scan_delay", "Scan delay", 0, 61,
                      _ext.scan_delay if _ext.scan_delay <= 61
                      else DEFAULT_SCAN_DELAY,
                      lambda v: setattr(_ext, "scan_delay", v))

        resume = int(_ext.scan_resume)
        self._add_list(group, "scan_resume", "Scan resume", SCAN_RESUME_LIST,
                       resume - 1 if resume else 0,
                       lambda v: setattr(_ext, "scan_resume", v + 1))
        self._add_bool(group, _ext, "scan_blink", "Scan blink (LED)")

        def set_scan_list(value):
            _ext.scan_all = value == 8
            if value < 8:
                _ext.current_scan_list = value
        self._add_list(group, "scan_list", "List to scan", SCAN_LIST_LIST,
                       8 if _ext.scan_all else int(_ext.current_scan_list),
                       set_scan_list)

        self._add_field_list(group, _ext, "sq_mode", "Squelch mode",
                             SQUELCH_MODE_LIST)
        erased = int(_ext.sq_glitch_base) == 0xFF
        bases = [("sq_rssi_base", "Squelch RSSI", DEFAULT_SQ_RSSI),
                 ("sq_noise_base", "Squelch noise", DEFAULT_SQ_NOISE),
                 ("sq_glitch_base", "Squelch glitch", DEFAULT_SQ_GLITCH)]
        for field, label, default in bases:
            current = default if erased else int(getattr(_ext, field))
            self._add_int(group, field, label, 0, 254, current,
                          lambda v, f=field: setattr(_ext, f, v))
        return group

    def _get_keys(self):
        _set = self._memobj.settings
        _ext = self._memobj.ext
        group = RadioSettingGroup("keys", "Key actions")

        for i, label in enumerate(SIDE_KEYS):
            current = _clamp(_set.actions[i], len(ACTION_LIST),
                             DEFAULT_SIDE_KEYS[i])
            self._add_list(group, "side_key%d" % i, label, ACTION_LIST,
                           current,
                           lambda v, i=i: _set.actions.__setitem__(i, v))

        erased = int(_ext.key_shortcut[0]) == 0xFF
        for i, label in enumerate(SHORTCUT_KEYS):
            if erased:
                current = DEFAULT_SHORTCUTS[i]
            else:
                current = _clamp(_ext.key_shortcut[i], len(ACTION_LIST),
                                 DEFAULT_SHORTCUTS[i])
            self._add_list(group, "key_shortcut%d" % i, label, ACTION_LIST,
                           current,
                           lambda v, i=i: _ext.key_shortcut.__setitem__(i, v),
                           doc="Extended setting")
        return group

    def _get_dtmf(self):
        _mem = self._memobj
        _dtmf = _mem.dtmf
        group = RadioSettingGroup("dtmf", "DTMF")

        self._add_field_list(group, _dtmf, "delay", "DTMF delay",
                             DTMF_DELAY_LIST)
        self._add_field_list(group, _dtmf, "interval", "DTMF interval",
                             DTMF_INTERVAL_LIST)
        self._add_field_list(group, _dtmf, "mode", "DTMF mode",
                             DTMF_MODE_LIST)
        self._add_field_list(group, _dtmf, "select", "DTMF select",
                             DTMF_SELECT_LIST)
        self._add_bool(group, _dtmf, "display", "DTMF display")
        self._add_int(group, "encode_gain", "DTMF encode gain", 0, 127,
                      _dtmf.encode_gain if _dtmf.encode_gain <= 127 else 0,
                      lambda v: setattr(_dtmf, "encode_gain", v))
        self._add_int(group, "decode_threshold", "DTMF decode threshold",
                      0, 63,
                      _dtmf.decode_threshold
                      if _dtmf.decode_threshold <= 63 else 0,
                      lambda v: setattr(_dtmf, "decode_threshold", v))
        self._add_field_list(group, _mem.settings, "dtmf_state",
                             "Radio state", DTMF_STATE_LIST,
                             doc="Set to Normal to revive a stunned or "
                                 "killed radio")
        self._add_dtmf_code(group, "dtmf_kill", "Kill code", _mem.dtmf_kill)
        self._add_dtmf_code(group, "dtmf_stun", "Stun code", _mem.dtmf_stun)
        self._add_dtmf_code(group, "dtmf_wake", "Wake code", _mem.dtmf_wake)
        for i in range(16):
            self._add_dtmf_code(group, "dtmf_contact%d" % i,
                                "Contact %d" % (i + 1),
                                _mem.dtmf_contacts[i])
        return group

    def get_settings(self):
        return RadioSettings(self._get_basic(), self._get_work_mode(),
                             self._get_display(), self._get_extended(),
                             self._get_keys(), self._get_dtmf())

    def set_settings(self, settings):
        for element in settings:
            if not isinstance(element, RadioSetting):
                self.set_settings(element)
                continue
            if not element.changed():
                continue
            if element.has_apply_callback():
                LOG.debug("Applying %s", element.get_name())
                element.run_apply_callback()
