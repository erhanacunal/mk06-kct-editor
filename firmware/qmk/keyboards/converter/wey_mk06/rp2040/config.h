// Copyright 2026 QMK
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/*
 * RP2040 pin assignment (Raspberry Pi Pico numbering).
 *
 *   SD1 = UART0: TX GP0, RX GP1   (alternatives: GP12/GP13, GP16/GP17)
 *   SD2 = UART1: TX GP4, RX GP5   (alternatives: GP8/GP9)
 *
 * TX/RX are from the converter's point of view: converter TX goes to the
 * keyboard's RXD, converter RX comes from the keyboard's TXD.
 *
 * RS-232 / RS-423 ports need a 3.3V level shifter such as a MAX3232.
 * The RP2040 is NOT 5V tolerant, so Port 3 (5V TTL) needs level shifting too.
 */
#ifndef SD1_TX_PIN
#    define SD1_TX_PIN GP0
#endif
#ifndef SD1_RX_PIN
#    define SD1_RX_PIN GP1
#endif
#ifndef SD2_TX_PIN
#    define SD2_TX_PIN GP4
#endif
#ifndef SD2_RX_PIN
#    define SD2_RX_PIN GP5
#endif

// Double-tap reset to enter the UF2 bootloader.
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT 500U
