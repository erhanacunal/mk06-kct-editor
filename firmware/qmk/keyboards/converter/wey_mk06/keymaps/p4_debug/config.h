// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Single keyboard on MK06 Port 4 (RS-232), no Host Terminal / DLINK.
#undef SD1_PORT
#define SD1_PORT PORT_4
#undef SD2_PORT
#define SD2_PORT NO_PORT
#undef HOST_PORT
#define HOST_PORT NO_PORT
#undef DLINK_PORT
#define DLINK_PORT NO_PORT

// Debug build: log every received UART byte to the QMK console
// and toggle the Pico's onboard LED (GP25; not on Pico W) per byte.
#define MK06_RAW_LOG
#define MK06_ACTIVITY_LED GP25
