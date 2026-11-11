#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""CW Key USB MIDI settings tool for Windows (docs/midi-settings-spec.md). No extra packages: uses winmm.

    python tools/cwkey_midi.py list                 MIDI ports
    python tools/cwkey_midi.py info                 firmware version, chip
    python tools/cwkey_midi.py get                  all settings
    python tools/cwkey_midi.py set <id> <value>     change a setting (RAM; autosaved after 5 s)
    python tools/cwkey_midi.py save                 store settings in flash now
    python tools/cwkey_midi.py defaults             factory defaults (RAM)
    python tools/cwkey_midi.py bootloader           switch the key into the UF2 bootloader
    python tools/cwkey_midi.py update [file.uf2]    full update over MIDI: bootloader -> copy the .uf2 for the
                                                    detected chip (default: firmware/latest) -> check the new version
"""
import ctypes
import hashlib
import json
import os
import shutil
import string
import struct
import sys
import time
from ctypes import wintypes

winmm = ctypes.WinDLL("winmm")
MHDR_DONE = 0x1
NAMES = {1: "wpm", 2: "keyer_mode", 3: "dah_ratio_x10", 4: "weight_pct", 5: "element_memory", 6: "paddle_swap",
         7: "usb_mode", 8: "sidetone_on", 9: "tone_hz", 10: "volume_pct", 11: "ptt_enabled", 12: "ptt_lead_ms",
         13: "ptt_hang_ms", 14: "stream_key"}
ERRORS = {1: "unknown command", 2: "unknown setting", 3: "out of range", 4: "busy (keying / PTT)",
          5: "not supported", 6: "flash error", 7: "bad confirmation", 8: "bad frame"}
HERE = os.path.dirname(os.path.abspath(__file__))


class MIDIHDR(ctypes.Structure):
    _fields_ = [("lpData", ctypes.c_void_p), ("dwBufferLength", wintypes.DWORD), ("dwBytesRecorded", wintypes.DWORD),
                ("dwUser", ctypes.c_size_t), ("dwFlags", wintypes.DWORD), ("lpNext", ctypes.c_void_p),
                ("reserved", ctypes.c_size_t), ("dwOffset", wintypes.DWORD), ("dwReserved", ctypes.c_size_t * 8)]


class MIDIINCAPSW(ctypes.Structure):
    _fields_ = [("wMid", wintypes.WORD), ("wPid", wintypes.WORD), ("vDriverVersion", wintypes.UINT),
                ("szPname", wintypes.WCHAR * 32), ("dwSupport", wintypes.DWORD)]


class MIDIOUTCAPSW(ctypes.Structure):
    _fields_ = [("wMid", wintypes.WORD), ("wPid", wintypes.WORD), ("vDriverVersion", wintypes.UINT),
                ("szPname", wintypes.WCHAR * 32), ("wTechnology", wintypes.WORD), ("wVoices", wintypes.WORD),
                ("wNotes", wintypes.WORD), ("wChannelMask", wintypes.WORD), ("dwSupport", wintypes.DWORD)]


def ports():
    ins, outs = [], []
    for i in range(winmm.midiInGetNumDevs()):
        c = MIDIINCAPSW()
        if winmm.midiInGetDevCapsW(i, ctypes.byref(c), ctypes.sizeof(c)) == 0:
            ins.append((i, c.szPname))
    for i in range(winmm.midiOutGetNumDevs()):
        c = MIDIOUTCAPSW()
        if winmm.midiOutGetDevCapsW(i, ctypes.byref(c), ctypes.sizeof(c)) == 0:
            outs.append((i, c.szPname))
    return ins, outs


class CwKey:
    """Opens the "CW Key" MIDI ports; SysEx in/out with polling (no callbacks)."""

    def __init__(self):
        ins, outs = ports()
        pin = next((i for i, n in ins if "cw key" in n.lower()), None)
        pout = next((i for i, n in outs if "cw key" in n.lower()), None)
        if pin is None or pout is None:
            raise SystemExit('No MIDI port "CW Key" found (firmware 0.4.0+ connected? port used by a browser?)')
        self.hin, self.hout = wintypes.HANDLE(), wintypes.HANDLE()
        if winmm.midiInOpen(ctypes.byref(self.hin), pin, 0, 0, 0) != 0:
            raise SystemExit("cannot open the MIDI input (in use by another program?)")
        if winmm.midiOutOpen(ctypes.byref(self.hout), pout, 0, 0, 0) != 0:
            raise SystemExit("cannot open the MIDI output (in use by another program?)")
        self.bufs = []
        for _ in range(8):
            mem = ctypes.create_string_buffer(256)
            h = MIDIHDR(lpData=ctypes.cast(mem, ctypes.c_void_p), dwBufferLength=256)
            winmm.midiInPrepareHeader(self.hin, ctypes.byref(h), ctypes.sizeof(h))
            winmm.midiInAddBuffer(self.hin, ctypes.byref(h), ctypes.sizeof(h))
            self.bufs.append((h, mem))
        winmm.midiInStart(self.hin)
        self.seq = 0

    def close(self):
        winmm.midiInStop(self.hin)
        winmm.midiInReset(self.hin)
        for h, _ in self.bufs:
            winmm.midiInUnprepareHeader(self.hin, ctypes.byref(h), ctypes.sizeof(h))
        winmm.midiInClose(self.hin)
        winmm.midiOutClose(self.hout)

    def _send(self, data):
        mem = ctypes.create_string_buffer(bytes(data))
        h = MIDIHDR(lpData=ctypes.cast(mem, ctypes.c_void_p), dwBufferLength=len(data))
        winmm.midiOutPrepareHeader(self.hout, ctypes.byref(h), ctypes.sizeof(h))
        winmm.midiOutLongMsg(self.hout, ctypes.byref(h), ctypes.sizeof(h))
        t0 = time.time()
        while not (h.dwFlags & MHDR_DONE) and time.time() - t0 < 1:
            time.sleep(0.001)
        winmm.midiOutUnprepareHeader(self.hout, ctypes.byref(h), ctypes.sizeof(h))

    def _frames(self):
        out = []
        for h, mem in self.bufs:
            if h.dwFlags & MHDR_DONE:
                out.append(bytes(mem.raw[:h.dwBytesRecorded]))
                h.dwBytesRecorded = 0
                h.dwFlags &= ~MHDR_DONE
                winmm.midiInAddBuffer(self.hin, ctypes.byref(h), ctypes.sizeof(h))
        return [f for f in out if len(f) >= 8 and f[:4] == b"\xF0\x7D\x43\x4B"]

    def request(self, cmd, payload=(), until=(0x43, 0x44, 0x41), timeout=1.5):
        """Sends a command and collects response frames until one of `until` with our seq arrives."""
        self.seq = (self.seq + 1) & 0x7F
        self._send([0xF0, 0x7D, 0x43, 0x4B, 0x01, cmd, self.seq] + [b & 0x7F for b in payload] + [0xF7])
        got, t0 = [], time.time()
        while time.time() - t0 < timeout:
            for f in self._frames():
                if f[6] in (self.seq, 0):
                    got.append(f)
                    if f[5] in until and f[6] == self.seq:
                        return got
            time.sleep(0.002)
        raise SystemExit(f"no answer to command {cmd} within {timeout} s")


def check_nak(frames):
    for f in frames:
        if f[5] == 0x44:
            raise SystemExit(f"NAK: {ERRORS.get(f[8], f[8])}")


def info(k):
    f = k.request(0x01)[-1]
    p = f[7:-1]
    name = p[8:8 + p[7]].decode()
    return {"name": name, "version": f"{p[1]}.{p[2]}.{p[3]}", "chip": "f401" if p[4] else "f411",
            "protocol": p[0], "capabilities": p[6]}


def get_all(k):
    frames = k.request(0x02, until=(0x43, 0x44))
    check_nak(frames)
    return {f[7]: f[8] | (f[9] << 7) for f in frames if f[5] == 0x42}


def find_drive(labels=("STMF411BOOT", "BlackPill"), timeout=10):
    t0 = time.time()
    while time.time() - t0 < timeout:
        for letter in string.ascii_uppercase:
            root = f"{letter}:\\"
            info_file = os.path.join(root, "INFO_UF2.TXT")
            if os.path.exists(info_file):
                buf = ctypes.create_unicode_buffer(64)
                ctypes.windll.kernel32.GetVolumeInformationW(root, buf, 64, None, None, None, None, 0)
                if buf.value in labels:
                    return root, buf.value, open(info_file, encoding="ascii", errors="replace").read()
        time.sleep(0.25)
    return None, None, None


def uf2_ok(data):
    for i in range(0, len(data), 512):
        m0, m1, _flags, addr, _size, _bno, _nb, fam = struct.unpack("<8I", data[i:i + 32])
        if m0 != 0x0A324655 or m1 != 0x9E5D5157 or fam != 0x57755A57 or addr < 0x08010000:
            return False
    return len(data) > 0 and len(data) % 512 == 0


def update(uf2_path=None):
    k = CwKey()
    before = info(k)
    print(f"key: {before['name']} {before['version']} ({before['chip']})")
    frames = k.request(0x07, [0x55, 0x46, 0x32])
    check_nak(frames)
    k.close()
    print("bootloader requested, waiting for the USB drive ...")
    root, label, text = find_drive()
    if not root:
        raise SystemExit("bootloader drive did not appear within 10 s")
    chip = "f411" if "STM32F411" in text else "f401" if "STM32F401" in text else None
    print(f"drive {root} {label}, chip {chip}")
    if chip is None:
        raise SystemExit("unknown board in INFO_UF2.TXT")
    if uf2_path is None:
        latest = os.path.normpath(os.path.join(HERE, "..", "firmware", "latest"))
        man = json.load(open(os.path.join(latest, "manifest.json"), encoding="utf-8"))
        target = next(t for t in man["targets"] if t["uf2"]["file"] == f"cwkey-{chip}.uf2")
        uf2_path = os.path.join(latest, target["uf2"]["file"])
        data = open(uf2_path, "rb").read()
        if hashlib.sha256(data).hexdigest() != target["uf2"]["sha256"]:
            raise SystemExit("SHA-256 mismatch against manifest.json")
        print(f"image {os.path.basename(uf2_path)} {man['version']}, SHA-256 OK")
    data = open(uf2_path, "rb").read()
    if not uf2_ok(data):
        raise SystemExit("refusing: not a valid STM32F4 UF2 with all blocks at >= 0x08010000")
    if f"-{chip}" not in os.path.basename(uf2_path).lower():
        raise SystemExit(f"refusing: {os.path.basename(uf2_path)} does not look like an image for {chip}")
    try:
        shutil.copyfile(uf2_path, os.path.join(root, "firmware.uf2"))
    except OSError:
        pass   # the drive may vanish before the copy returns: the key restarts after the last block
    print("copied, waiting for the key ...")
    t0 = time.time()
    while time.time() - t0 < 15:
        time.sleep(0.5)
        if os.path.exists(os.path.join(root, "INFO_UF2.TXT")):
            continue
        try:
            k = CwKey()
        except SystemExit:
            continue
        after = info(k)
        k.close()
        print(f"key back: {after['name']} {after['version']} ({after['chip']})")
        return
    raise SystemExit("the key did not come back as a MIDI device within 15 s")


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "info"
    if cmd == "list":
        ins, outs = ports()
        print("in: ", ins)
        print("out:", outs)
    elif cmd == "update":
        update(sys.argv[2] if len(sys.argv) > 2 else None)
    else:
        k = CwKey()
        try:
            if cmd == "info":
                print(info(k))
            elif cmd == "get":
                for i, v in get_all(k).items():
                    print(f"{i:2d} {NAMES.get(i, '?'):15s} {v}")
            elif cmd == "set":
                sid, val = int(sys.argv[2], 0), int(sys.argv[3], 0)
                check_nak(k.request(0x04, [sid, val & 0x7F, (val >> 7) & 0x7F]))
                print("OK")
            elif cmd == "save":
                check_nak(k.request(0x05, until=(0x43, 0x44), timeout=7))
                print("saved")
            elif cmd == "defaults":
                check_nak(k.request(0x06))
                print("defaults restored (RAM)")
            elif cmd == "bootloader":
                check_nak(k.request(0x07, [0x55, 0x46, 0x32]))
                print("switching to the bootloader")
            else:
                print(__doc__)
        finally:
            k.close()


if __name__ == "__main__":
    main()
