// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Single UART on the MK06 External Port (RS-423) running DLINK.
// Pico GP0/GP1 -> MAX3232 -> HPCN36 pin 28 (EXT RXD) / pin 10 (EXT TXD).
#undef SD1_PORT
#define SD1_PORT PORT_X
#undef SD2_PORT
#define SD2_PORT NO_PORT
#undef HOST_PORT
#define HOST_PORT NO_PORT
#undef DLINK_PORT
#define DLINK_PORT PORT_X

// First try: log every received byte and blink the Pico LED (GP25).
#define MK06_RAW_LOG
#define MK06_ACTIVITY_LED GP25
