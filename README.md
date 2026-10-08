# MK06 KCT Editor

A single-file, offline editor for the keycode tables (`.kct`) of the **Wey Technology MK06** multi-workstation keyboard.

Open `index.html` in a browser, drop a `.kct` file exported from the keyboard, edit keys, and download the result. The file never leaves your machine.

## Features

- Keyboard view with the real MK06 layout: main block, LCD soft keys and both plug-in keypads
  (Reuters and Bloomberg keypads are drawn with their real legends and double-wide keys).
- Four actions per key, one for each modifier state: plain, Shift, Ctrl, Alt.
- Per action: a key with forced modifiers (Shift / Ctrl / AltGr / Exact), a key sequence, keyboard-handled special key, or a raw word.
- Key sequences can be typed directly or generated from text for the US or Turkish Q host layout.
- Header editing: table name and plug-in keypad IDs.
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
| `0x030` | 1 | KCT number of the table it was derived from |
| `0x080` | 0x580 | Main table: 4 words per internal key `0x00`–`0xAF` |
| `0x600` | … | Special-key table (modifiers, Tab, Caps, F keys), 16 words per key |
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

Keypad keys only follow the table when the header keypad IDs are set (the keyboard shows the installed
keypads under *Software Info*; IDs follow the solder-bridge coding in the MK06 manual, e.g. `04` Bloomberg, `0B` Reuters standard).
