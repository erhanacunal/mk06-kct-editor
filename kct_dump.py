#!/usr/bin/env python3
"""
Wey MK06 keycode table (.kct) dumper.

Format as reverse engineered from sample tables (not official, partly guessed):

  0x000  u8   format version (01)
  0x001  u8   emulation type? (0x12 PC, 0x14 RD2000, 0x15 Bloomberg, 0x21 RD3000 ...)
  0x002  u8   00 = PC style, 01 = serial/dealing style?
  0x003  24s  table name, space padded, then 00
  0x023  u8   expected plug-in keypad ID, left  (manual 4.5: 01 RD2000, 04 Bloomberg, 07 EUREX, 0A SUN ...)
  0x024  u8   expected plug-in keypad ID, right
  0x02F  u8   KCT number
  0x030  u8   KCT number of the table it was derived from
  0x080  main table: 8 bytes per internal key number (0x00-0xAF), = 4 big-endian 16-bit
         action words, one per column (verified on a device): used when the key
         is pressed plain | with Shift | with Ctrl | with Alt
         (left and right modifiers alike; AltGr counts as Alt).
         internal key numbers: 0x00-0x7F main keyboard, 0x80-0x8B LCD soft keys,
         0x90-0x9F RIGHT keypad, 0xA0-0xAF LEFT keypad (confirmed on device)
  0x600  extended table for "special" keys (entries 0xC000 in the main table),
         32 bytes (16 words) per special key, 35 keys in the US tables (to 0xA5F)
  0xA60  unknown (96 bytes)
  0xAC0  string area: sequences of 0x40nn (press) / 0xC0nn (release) or 0x01nn
         (raw byte) words, terminated by 0x0000, referenced by 0x8000|offset

Action words:
  0000        unassigned (the keyboard's own macros can be put on such keys)
  40nn        send key position nn (IBM PC/AT key number: 01 = `, 10 = Tab,
              1E = Caps, 2B = Enter, 3D = Space, 6E = Esc, 70-7B = F1-F12 ...)
  C000        special key (modifiers, Tab, Caps, F keys), see extended table
  C0nn        (in sequences) release key nn
  4Mnn        key nn with forced modifiers, M bitmask (verified on a device):
              02 = Shift, 04 = Ctrl, 08 = AltGr (right Alt),
              01 = exact: physically held modifiers are released for the
                   keystroke and pressed again afterwards (without 01 they
                   stay pressed and combine with the forced ones)
  5xnn        serial character code nn (dealing emulations)
  8000|off    send the sequence at 0xAC0 + off
  01nn        send raw byte nn (serial emulations)
"""
import sys

AT_KEYS = {
    0x01: "`", 0x02: "1", 0x03: "2", 0x04: "3", 0x05: "4", 0x06: "5", 0x07: "6",
    0x08: "7", 0x09: "8", 0x0A: "9", 0x0B: "0", 0x0C: "-", 0x0D: "=", 0x0E: "Yen",
    0x0F: "Backspace", 0x10: "Tab", 0x11: "Q", 0x12: "W", 0x13: "E", 0x14: "R",
    0x15: "T", 0x16: "Y", 0x17: "U", 0x18: "I", 0x19: "O", 0x1A: "P", 0x1B: "[",
    0x1C: "]", 0x1D: "\\", 0x1E: "Caps", 0x1F: "A", 0x20: "S", 0x21: "D", 0x22: "F",
    0x23: "G", 0x24: "H", 0x25: "J", 0x26: "K", 0x27: "L", 0x28: ";", 0x29: "'",
    0x2A: "ISO#", 0x2B: "Enter", 0x2C: "LShift", 0x2D: "ISO<", 0x2E: "Z", 0x2F: "X",
    0x30: "C", 0x31: "V", 0x32: "B", 0x33: "N", 0x34: "M", 0x35: ",", 0x36: ".",
    0x37: "/", 0x38: "Ro", 0x39: "RShift", 0x3A: "LCtrl", 0x3B: "LWin", 0x3C: "LAlt",
    0x3D: "Space", 0x3E: "RAlt", 0x3F: "RWin", 0x40: "RCtrl", 0x41: "Menu",
    0x4B: "Insert", 0x4C: "Delete", 0x4F: "Left", 0x50: "Home", 0x51: "End",
    0x53: "Up", 0x54: "Down", 0x55: "PgUp", 0x56: "PgDn", 0x59: "Right",
    0x5A: "NumLock", 0x5B: "KP7", 0x5C: "KP4", 0x5D: "KP1", 0x5F: "KP/", 0x60: "KP8",
    0x61: "KP5", 0x62: "KP2", 0x63: "KP0", 0x64: "KP*", 0x65: "KP9", 0x66: "KP6",
    0x67: "KP3", 0x68: "KP.", 0x69: "KP-", 0x6A: "KP+", 0x6C: "KPEnter", 0x6E: "Esc",
    0x7C: "PrtSc", 0x7D: "ScrLk", 0x7E: "Pause",
}
for i in range(12):
    AT_KEYS[0x70 + i] = f"F{i + 1}"
for i in range(12):
    AT_KEYS[0x80 + i] = f"Soft{i + 1}"
for i in range(16):
    AT_KEYS[0x90 + i] = f"KPR{i:X}"
    AT_KEYS[0xA0 + i] = f"KPL{i:X}"


STRING_BASE = 0xAC0
DATA = b""


def seq_str(off):
    out, a = [], STRING_BASE + off
    while a + 1 < len(DATA):
        w = int.from_bytes(DATA[a:a + 2], "big")
        a += 2
        if w == 0:
            break
        hi, lo = w >> 8, w & 0xFF
        if hi == 0x40:
            out.append("+" + AT_KEYS.get(lo, f"{lo:02X}"))
        elif hi == 0xC0:
            out.append("-" + AT_KEYS.get(lo, f"{lo:02X}"))
        elif hi == 0x01:
            out.append(f"#{lo:02X}")
        else:
            out.append(f"{w:04X}")
    return " ".join(out)


def word_str(w):
    hi, lo = w >> 8, w & 0xFF
    if w == 0:
        return "-"
    if hi == 0x40:
        return AT_KEYS.get(lo, f"key{lo:02X}")
    if 0x41 <= hi <= 0x4F:
        m = hi & 0x0F
        mods = [n for b, n in ((2, "Shift"), (4, "Ctrl"), (8, "AltGr"), (1, "exact")) if m & b]
        return "+".join(mods + [AT_KEYS.get(lo, f"key{lo:02X}")])
    if hi == 0x01:
        return f"byte:{lo:02X}"
    if w == 0xC000:
        return "<special>"
    if hi & 0xF0 == 0x50:
        return f"chr{hi & 0x0F}:{lo:02X}"
    if hi & 0xF0 == 0x80:
        return "seq[" + seq_str(w & 0x7FFF) + "]"
    return f"{w:04X}"


def main(path):
    global DATA
    d = DATA = open(path, "rb").read()
    name = d[3:27].decode("latin1").rstrip(" \0")
    print(f"{path}: name={name!r} type={d[1]:02X} style={d[2]:02X} country?={d[0x1B]:02X} "
          f"keypads L={d[0x23]:02X} R={d[0x24]:02X} kct={d[0x2F]} base={d[0x30]}")
    for i in range(0xB0):
        e = d[0x80 + i * 8:0x88 + i * 8]
        words = [int.from_bytes(e[k:k + 2], "big") for k in range(0, 8, 2)]
        if not any(words):
            continue
        cols = [word_str(w) for w in words]
        if len(set(cols)) == 1:
            cols = [cols[0]]
        print(f"  {i:02X} {AT_KEYS.get(i, ''):>9} -> {' | '.join(cols)}")


if __name__ == "__main__":
    for p in sys.argv[1:]:
        main(p)
