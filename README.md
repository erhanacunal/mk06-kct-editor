# MK06 KCT Editor and RP2040 converter

Tools for the **Wey Technology MK06** multi-workstation keyboard:

- **KCT editor** (`index.html`): an offline editor for the keyboard's keycode tables (`.kct`) and LCD layer files (`LAYERnn.LAY`).
- **RP2040 converter firmware** (`firmware/`): a QMK keyboard that turns a Raspberry Pi Pico into a USB keyboard for the MK06, connected over DLINK or a serial port. Includes media keys and a USB-to-Host-Port-A bridge for file transfer.
- **Command line tools** (`tools/`): `weytool.py` to list, read, write and delete files on the keyboard (Windows and Linux), and `kct_dump.py` to dump a `.kct` as text.

The file formats below are reverse engineered and tested on a real MK06.

```
index.html                      KCT / LAYER editor (open in a browser)
tools/weytool.py                keyboard file transfer over USB or Host Port A
tools/kct_dump.py               .kct dumper
firmware/uf2/                   prebuilt firmware for the Raspberry Pi Pico
firmware/qmk/keyboards/converter/wey_mk06/
                                QMK source, copy into a QMK tree to build
```

## KCT editor

Open `index.html` in a browser (it also works from GitHub Pages). Drop a `.kct` or `LAYERnn.LAY` file exported
from the keyboard, edit, and download the result. The file never leaves your machine.

- Keyboard view with the real MK06 layout: main block, LCD soft keys and both plug-in keypads
  (Reuters and Bloomberg keypads are drawn with their real legends and double-wide keys).
- Four actions per key, one for each modifier state: plain, Shift, Ctrl, Alt.
- Per action: a key with forced modifiers (Shift / Ctrl / AltGr / Exact), a key sequence, a keyboard-handled special key, or a raw word.
- Key sequences can be typed directly or generated from text for the US or Turkish Q host layout.
- Header editing: table name, plug-in keypad IDs and the four Prev/Next states (LCD layer + page each).
- Per-state actions for soft keys, F keys and modifiers (Prev/Next states 1–4).
- Media keys for the RP2040 converter (see [Media keys](#media-keys)).
- LCD layer editor: three 6-character lines per key, inverse text and key colour.
- Change list, per-key revert and a usage meter for the sequence area.
- Download as a zip containing the `.kct` (in the same 4096-byte format).

Load the result onto the keyboard with a USB stick, or with `tools/weytool.py` (below).

## RP2040 converter

A QMK converter based on MMcM's Wey MK06 converter (originally for Teensy, from [Alectardy98/vial-qmk-alectardy98_06](https://github.com/Alectardy98/vial-qmk-alectardy98_06)), ported to the RP2040.
The Pico reads the keyboard's serial output and appears on USB as a normal keyboard (and mouse). Which keys the keyboard
sends is set by the active KCT; the QMK keymap only maps those key numbers to USB keycodes.

### Prebuilt firmware

Hold BOOTSEL while plugging in the Pico (or double-tap reset once QMK is installed) and copy a `.uf2` from `firmware/uf2/` to the `RPI-RP2` drive.

| File | Use |
|---|---|
| `converter_wey_mk06_rp2040_dlink.uf2` | **Recommended.** DLINK on the External Port, with media keys. |
| `converter_wey_mk06_rp2040_dlink_hpa.uf2` | `dlink`, plus a USB serial bridge to Host Port A for `weytool.py`. |
| `converter_wey_mk06_rp2040_dlink_hpa_diag.uf2` | `dlink_hpa` with the diagnostic shortcuts below (for setting up the Host Port A wiring). |
| `converter_wey_mk06_rp2040_dlink_debug.uf2` | `dlink`, plus logging of every received byte to the QMK console and the Pico LED blinking on each byte. |
| `converter_wey_mk06_rp2040_p4.uf2` | Plain serial keyboard on Port 4 (RS-232), no DLINK. |
| `converter_wey_mk06_rp2040_p4_debug.uf2` | `p4` with byte logging. |
| `converter_wey_mk06_rp2040_default.uf2` | Upstream default: Host Port A as host terminal on UART0, DLINK on UART1. |

Built from QMK `master` (October 2026).

### Wiring

The RS-232 / RS-423 ports need a 3.3 V level shifter such as a **MAX3232** module (check that its charge pump
actually produces about ±5 V on V+ / V−; a dead pump gives working RX but no TX). The RP2040 is not 5 V tolerant.

Keyboard connector: 36-pin half-pitch Centronics (HPCN36). GND is on pins 1, 11 and 19.

| Signal | Pico | MAX3232 | HPCN36 pin | Used by |
|---|---|---|---|---|
| UART0 TX | GP0 | T1IN → T1OUT | 28 (EXT RXD) | `dlink*`, `p4*` (Port 4: pin 17) |
| UART0 RX | GP1 | R1OUT ← R1IN | 10 (EXT TXD) | `dlink*`, `p4*` (Port 4: pin 35) |
| UART1 TX | GP4 | T2IN → T2OUT | 3 (HPA RXD) | `dlink_hpa` |
| UART1 RX | GP5 | R2OUT ← R2IN | 21 (HPA TXD) | `dlink_hpa` |
| GND | GND | GND | 1 / 11 / 19 | all |

RX/TX on the keyboard side are from the keyboard's point of view. In the keyboard's Setup menu, set the External Port to DLINK.

**Cabling.** Run every signal in its own twisted pair together with a GND wire, e.g. a CAT5/CAT6 cable:

| Pair | Solid colour | White-striped |
|---|---|---|
| Orange | EXT TXD (pin 10) | GND |
| Green | EXT RXD (pin 28) | GND |
| Blue | HPA TXD (pin 21) | GND |
| Brown | HPA RXD (pin 3) | GND |

Connect all white-striped wires to GND at both ends; connect a cable shield at the Pico end only. Do not put two
signals in one pair: with TX and RX in the D+/D− pair of a USB cable, long transfers on Host Port A at 115200 baud were corrupted.

### Media keys

The `dlink` keymaps put consumer keys on key numbers `0xA0`–`0xAF`. In the KCT editor pick them from the
*Media (QMK converter)* group, or type their names in a sequence (`VolUp VolUp Mute`).

| Code | Key | Code | Key | Code | Key | Code | Key |
|---|---|---|---|---|---|---|---|
| A0 | Play/Pause | A4 | Mute | A8 | Mail | AC | Web Search |
| A1 | Stop | A5 | Volume − | A9 | Web Home | AD | My Computer |
| A2 | Previous | A6 | Volume + | AA | Web Back | AE | Brightness − |
| A3 | Next | A7 | Calculator | AB | Web Forward | AF | Brightness + |

These codes are the raw numbers of the left plug-in keypad. A left keypad key whose KCT entry still sends its own
number (`40Ax`) therefore also acts as a media key.

### Host Port A bridge (`dlink_hpa`)

The Pico also appears as a USB serial port (`COMx` / `/dev/ttyACM0`). Everything written to it goes out on Host Port A
and the replies come back, so `weytool.py -D COMx` can transfer files while the keyboard keeps typing over DLINK.
The UART runs at 115200 baud by default (the baud rate set on the PC side is ignored); it must match the keyboard's Host Port A setting.

Diagnostics, only in `dlink_hpa_diag`, typed into the focused window (e.g. Notepad). A baud rate saved with Ctrl+Shift+F11
is kept when you switch back to `dlink_hpa`:

| Shortcut | Action |
|---|---|
| Ctrl+Shift+F10 | Ask the keyboard for its ID on Host Port A and type the reply (`HPA 115200 YANIT 31` = OK) |
| Ctrl+Shift+F11 | Try every baud rate from 115200 down to 1200; the first that answers is saved in EEPROM (`KAYDEDILDI`) |
| Ctrl+Shift+F12 | Type baud rate and counters: bytes sent (`TX`), received (`RX`), UART errors (`HATA`), dropped (`KAYIP`) |

### Building

```sh
python3 -m pip install qmk
qmk setup                       # clones qmk_firmware
cp -r firmware/qmk/keyboards/converter/wey_mk06 ~/qmk_firmware/keyboards/converter/
qmk compile -kb converter/wey_mk06/rp2040 -km dlink
```

## Tools

### weytool.py

Python port of [weytool](https://github.com/funkenzuzzler/weytools): list, read, write and delete files on the keyboard.

```sh
pip install pyusb libusb-package pyserial
python tools/weytool.py -l                       # keyboard USB port
python tools/weytool.py -D COM12 -l              # Host Port A, e.g. through the dlink_hpa bridge
python tools/weytool.py -B backup                # download every readable file
python tools/weytool.py -w 8,10,KCT10_MY.kct     # upload a KCT to index 8, slot 10
python tools/weytool.py -w LAYER10.LAY           # upload an LCD layer
python tools/weytool.py --rawcmd 7f,e8 --rawrx 1 # raw command: keyboard ID ('1' = MK06)
```

Over the keyboard's own USB port on Windows, the keyboard's *Interface 1* needs the WinUSB driver (install with Zadig;
leave Interface 0, the keyboard itself, alone). The original C weytool works on Linux without that step.
Back up with `-B` before writing, and never write index 16 (PIN code).

File indexes: 1 system setup, 2 user menu, 4/5 bitmaps, 8 KCT, 9 LCD layers, 10 user macros, 16 PIN code.

### kct_dump.py

```sh
python tools/kct_dump.py KCT10_MY.kct
```

Prints the header (name, keypads, Prev/Next states) and every assigned key with its four actions.

## KCT format

Reverse engineered from the tables shipped with the keyboard and verified on a device.
All 16-bit words are big-endian.

| Offset | Size | Content |
|---|---|---|
| `0x000` | 1 | Format version (`01`) |
| `0x001` | 1 | Emulation type (`12` PC, `14` Reuters D2000, `15` Bloomberg, `21` Reuters 3000 / serial …) |
| `0x002` | 1 | `00` PC style, `01` serial style |
| `0x003` | 24 | Table name, space padded |
| `0x01B` | 1 | Country code (`00` international, `01` US) |
| `0x023` | 1 | Expected **right** keypad ID |
| `0x024` | 1 | Expected **left** keypad ID |
| `0x02F` | 1 | KCT number |
| `0x030` | 4 × 8 | Prev/Next states 1–4 at `0x30`, `0x38`, `0x40`, `0x48`. Byte 0 = LCD layer number (`LAYERnn.LAY`), byte 1 = page in that layer file (`0`–`3`). Layer `00` in states 2–4 = state not used. Remaining 6 bytes: always `00` in observed files |
| `0x050` | 1 | Unknown |
| `0x080` | 0x580 | Main table: 4 words per internal key `0x00`–`0xAF` |
| `0x600` | 0x460 | Special-key table: 35 keys × 4 states × 4 columns |
| `0xAC0` | … | Key sequences, each ending with `0000` |

**Internal key numbers:** `0x00`–`0x7F` main keyboard, `0x80`–`0x8B` LCD soft keys,
`0x90`–`0x9F` right keypad, `0xA0`–`0xAF` left keypad (row by row; a keypad key shown as
`ADR nn` / `ADL nn` in the keyboard's test mode is base + nn − 1; a double-wide key reports its right-hand cell).

**Columns:** the four words of a key are used when it is pressed plain, with Shift, with Ctrl, or with Alt
(left/right modifiers alike; AltGr counts as Alt).

**Action words:**

| Word | Meaning |
|---|---|
| `0000` | Unassigned. The keyboard's own user macros only run on unassigned keys. |
| `4Mnn` | Send key position `nn` (IBM PC/AT key number) with modifier bits `M`: `2` Shift, `4` Ctrl, `8` AltGr, `1` exact (held modifiers are released for the keystroke and pressed again afterwards) |
| `C000` | Special key, defined in the table at `0x600` |
| `8000`+off | Play the sequence at `0xAC0` + off |
| `5xnn` | Serial character code (Reuters emulations) |
| `01nn` | Raw byte (serial emulations) |

Inside sequences `40nn` presses and `C0nn` releases key `nn`.

**Special keys and layers:** keys whose main entry is `C000` take their actions from the special-key table,
in this fixed slot order: Tab, Caps, LShift, RShift, LCtrl, LWin, LAlt, AltGr, RWin, Menu, RCtrl, F1–F12, LCD soft keys 1–12.
Each slot holds 4 groups of 4 columns, one group per Prev/Next state: group *n* is used while the keyboard is in state *n*
(and the LCD shows that state's layer and page). Prev/Next cycles through the states that have a layer set.
A keyboard user macro on a soft key wins over state 1.

## LAYER format (`LAYERnn.LAY`, 2560 bytes)

| Offset | Size | Content |
|---|---|---|
| `0x000` | 160 | Unused in all observed files (spaces) |
| `0x0A0` | 96 | Key colours: 4 pages × 24 keys; `0` white, `1` green, `2` red, `3` yellow |
| `0x100` | 2304 | Labels: 4 pages × 24 keys × 24 bytes |

Keys 0–11 are the F1–F12 row below the LCD, 12–23 the soft keys above it. A label is 4 lines of 6 characters;
the LCD shows the first 3. Characters with the top bit set are drawn inverse (white on black).
A soft key with a label is treated as assigned, so the keyboard's own macro legend and icon are only shown on keys left blank.

Keypad keys only follow the table when the header keypad IDs are set (the keyboard shows the installed
keypads under *Software Info*; IDs follow the solder-bridge coding in the MK06 manual, e.g. `04` Bloomberg, `0B` Reuters standard).

## Credits and licences

- RP2040 converter: based on the Wey MK06 converter by [MMcM](https://github.com/MMcM) for [QMK](https://github.com/qmk/qmk_firmware);
  the `firmware/` folder is GPL-2.0-or-later like QMK.
- `weytool.py`: protocol from [funkenzuzzler/weytools](https://github.com/funkenzuzzler/weytools).
- `KCT09_QMK149.kct` in the firmware folder comes from the upstream converter repository.
