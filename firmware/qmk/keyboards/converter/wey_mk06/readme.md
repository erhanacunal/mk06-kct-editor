# Wey MK06

A converter for the Wey Technologies MK06. Should also work with other keyboards supporting the same Connector Boxes.

Keyboard Maintainer: [MMcM](https://github.com/MMcM)  
Hardware Supported: MK06; RP2040 (Raspberry Pi Pico and compatibles)

Make example for this keyboard (after setting up your build environment):

    qmk compile -kb converter/wey_mk06/rp2040 -km default

## Hardware

The keyboard has 9 direct ports, implementing various hardware bit-serial protocols.

| Port  | Protocol                  |
|-------|---------------------------|
| P1    | TTL Clock/Data (AT/PS2)   |
| P2    | TTL Clock/Data (AT/PS2)   |
| P3a   | TTL serial inverted (Sun) |
| P3d   | TTL serial inverted (Sun) |
| P4    | RS-232                    |
| P5    | RS-232                    |
| EXT   | RS-423                    |
| HPA   | RS-423                    |
| HPB   | RS-423                    |

A Connector Box exposes the first 6 of these with appropriate connectors (mini-DIN, D-Sub).

Up to 14 virtual ports can be defined by assigning them a DLINK address using the keyboard's Setup
menus. Again, a Connector Box would expose some DLINK addresses as physical connectors. One of the 3
RS-423 ports then needs to be defined as the DLINK port, which communicates with the Connector Box
-- or this converter -- to multiplex them.

16 workstations (WS) are selectable using special keys on the keyboard and assigned to these various ports.

### Connections

The main keyboard connector is a 36-pin mini-Centronics, also known as Half-Pitch Centronics
(HPCN36) or Mini Delta Ribbon (MDR). It was also used by some HP printers and some SCSI cables.  A
cable will have 18 twisted pairs and the signals often come in pairs.

| Pin | Signal      | | Pin | Signal      |
|----:|-------------|-|----:|-------------|
|   1 | GND         | |  19 | GND         |
|   2 | speaker L+  | |  20 | speaker L-  |
|   3 | HPA RXD     | |  21 | HPA TXD     |
|   4 | HPB RXD     | |  22 | HPB TXD     |
|   5 | +12V        | |  23 | RES2        |
|   6 | RES1        | |  24 | ??          |
|   7 | RES5        | |  25 | RES4        |
|   8 | speaker R+  | |  26 | speaker R-  |
|   9 | microphone  | |  27 | microphone  |
|  10 | EXT TXD     | |  28 | EXT RXD     |
|  11 | GND         | |  29 | +5V ??      |
|  12 | P1 CLK      | |  30 | P1 DAT      |
|  13 | P2 CLK      | |  31 | P2 DAT      |
|  14 | P3b RXD     | |  32 | P3b TXD     |
|  15 | P3a RXD     | |  33 | P3a TXD     |
|  16 | P5 RXD      | |  34 | P5 TXD      |
|  17 | P4 RXD      | |  35 | P4 TXD      |
|  18 | +12V        | |  36 | +12V        |

(RX/TX is from the perspective of the keyboard.)

The keyboard also has:
* 1 USB device port, implementing a CDC device (and not a keyboard)
* 2 USB host ports, for plugging in a mouse and thumb drive
* 1 PS2 mouse port
* 1 headphone jack
* 1 microphone jack

## Interfacing

The easiest way to interface without a Connector Box using PS2 and not this converter. Build a cable
that gives 12VDC from a wall wart to the two ends of the HPCN36 and connects Clock, Data, and Ground
in the center to a 6-pin mini-DIN male.
Then, if USB is required, use a dumb converter or Soarer's or TMK.

Even though Port 2 is labeled on Connector Boxes for a mouse, it can also be assigned to a WS. This
means that by connecting two DIN connectors to each of P1 and P2 it is possible to build two-way
KVM output without any components other than connectors.

This converter interfaces to any of the remaining serial ports and to DLINK ports. It enumerates to
USB as a single keyboard (and mouse). Different layouts can be assigned to different workstations or
ports.

### RP2040 wiring

The RP2040 has 2 UARTs, assigned to MK06 ports in `config.h`. The normal setup is UART0 to Host Port A
as Host Terminal and UART1 to External Port as DLINK.

| UART | Pico TX | Pico RX | Default MK06 port                     |
|------|---------|---------|---------------------------------------|
| SD1 / UART0 | GP0 | GP1 | HPA (Host Terminal), HPCN36 pins 3/21 |
| SD2 / UART1 | GP4 | GP5 | EXT (DLINK), HPCN36 pins 10/28        |

Pico TX goes to the keyboard RXD, Pico RX comes from the keyboard TXD (through the level shifter).
Pins can be moved to other UART-capable pins with `SD1_TX_PIN` etc. in `rp2040/config.h`.

The RS-232 / RS-423 ports need a level shifter that runs from 3.3V, such as a MAX3232.
The RP2040 is not 5V tolerant, so Port 3 (5V TTL, inverted) also needs level shifting;
signal inversion for Port 3 is done in the RP2040 GPIO block.

Keys only reach USB from DLINK or plain serial ports. A port in Host Terminal mode is used
for workstation switching and is otherwise only logged to the QMK console.

To flash: hold BOOTSEL while plugging in (or double-tap reset once QMK is installed)
and copy the `.uf2` file to the `RPI-RP2` drive.

### Keymaps

| Keymap | Ports |
|---|---|
| `default` | UART0 Host Port A (host terminal), UART1 External Port (DLINK) |
| `dlink` | UART0 External Port (DLINK), GP0/GP1. Media keys on key numbers 0xA0-0xAF |
| `dlink_hpa` | `dlink`, plus UART1 (GP4/GP5) as a USB serial bridge to Host Port A (`mk06_hpa_bridge.c`) |
| `dlink_hpa_diag` | `dlink_hpa` with Ctrl+Shift+F10/F11/F12 diagnostics (`MK06_HPA_DIAG`) |
| `dlink_debug` | `dlink` with every received byte logged to the console |
| `p4`, `p4_debug` | UART0 on Port 4 (RS-232) |

See the README at the top of this repository for wiring, media keys and the Host Port A bridge.

### Firmware Variants

* `rp2040` for RP2040 boards such as the Raspberry Pi Pico.

The original Teensy LC / 3.2 variants are in the upstream converter and are not part of this port.
