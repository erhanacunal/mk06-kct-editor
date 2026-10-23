// Copyright 2026 QMK
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "mk06_port.h"

/*
 * Which MK06 port each UART is wired to.
 * Normal setup: SD1 to Host Port A as Host Terminal, SD2 to External Port as DLINK.
 * Set a port to NO_PORT to leave that UART unused.
 */
#ifndef SD1_PORT
#    define SD1_PORT PORT_A
#endif
#ifndef SD2_PORT
#    define SD2_PORT PORT_X
#endif

#ifndef HOST_PORT
#    define HOST_PORT PORT_A
#endif
#ifndef DLINK_PORT
#    define DLINK_PORT PORT_X
#endif

#ifndef SD1_BAUD
#    define SD1_BAUD 9600
#endif
#ifndef SD2_BAUD
#    define SD2_BAUD 9600
#endif
