# MK06 KCT Editor

A single-file, offline editor for the keycode tables (`.kct`) of the **Wey Technology MK06** multi-workstation keyboard.

Open `index.html` in a browser, drop a `.kct` file exported from the keyboard, edit keys, and download the result. The file never leaves your machine.

## Features

- Keyboard view with the real MK06 layout: main block, LCD soft keys and both plug-in keypads
  (Reuters and Bloomberg keypads are drawn with their real legends and double-wide keys).
- Four actions per key, one for each modifier state: plain, Shift, Ctrl, Alt.
- Per action: a key with forced modifiers (Shift / Ctrl / AltGr / Exact), a key sequence, keyboard-handled special key, or a raw word.
- Key sequences can be typed directly or generated from text for the US or Turkish Q host layout.
- Header editing: table name, plug-in keypad IDs and the four Prev/Next states (LCD layer + page each).
- Per-state actions for soft keys, F keys and modifiers (Prev/Next states 1–4).
- LCD layer editor (`LAYERnn.LAY`): three 6-character lines per key, inverse text and key colour.
- Change list, per-key revert and a usage meter for the sequence area.
- Download as a zip containing the `.kct` (in the same 4096-byte format).

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
