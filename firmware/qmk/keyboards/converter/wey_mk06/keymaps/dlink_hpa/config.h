// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// UART0 on the MK06 External Port (RS-423) running DLINK, as in the dlink keymap.
// Pico GP0/GP1 -> MAX3232 -> HPCN36 pin 28 (EXT RXD) / pin 10 (EXT TXD).
#undef SD1_PORT
#define SD1_PORT PORT_X
#undef SD2_PORT
#define SD2_PORT NO_PORT
#undef HOST_PORT
#define HOST_PORT NO_PORT
#undef DLINK_PORT
#define DLINK_PORT PORT_X

// UART1 is a plain USB serial bridge to Host Port A (see mk06_hpa_bridge.c).
// Pico GP4/GP5 -> MAX3232 (second channel) -> HPCN36 pin 3 (HPA RXD) / pin 21 (HPA TXD).
#define MK06_HPA_BAUD 115200
