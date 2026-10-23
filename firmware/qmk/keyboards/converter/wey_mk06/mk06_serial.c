// Copyright 2020 MMcM
// RP2040 / modern QMK port 2026
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Wey MK06 serial "matrix".
 *
 * The keyboard sends make/break scan codes over RS-232 / RS-423 serial ports.
 * Each scan code byte is split into row (high nibble) and column (low nibble)
 * of a virtual 16x16 matrix, which QMK then runs through the keymap as usual.
 *
 * Uses QMK's "custom_lite" matrix: we only provide matrix_init_custom() and
 * matrix_scan_custom(); QMK's matrix_common.c provides everything else.
 */

#include <string.h>
#include <hal.h>
#include "quantum.h"
#include "matrix.h"
#include "debug.h"
#include "print.h"
#include "mk06_port.h"

#if !defined(PROTOCOL_CHIBIOS)
#    error Only ChibiOS based MCUs are supported
#endif

#ifndef SD1_PORT
#    define SD1_PORT NO_PORT
#endif
#ifndef SD2_PORT
#    define SD2_PORT NO_PORT
#endif
#ifndef HOST_PORT
#    define HOST_PORT NO_PORT
#endif
#ifndef DLINK_PORT
#    define DLINK_PORT NO_PORT
#endif

#if !SD1_PORT && !SD2_PORT
#    error No port connection defined
#endif

// Key state assembled from received scan codes; copied into QMK's raw matrix on scan.
static matrix_row_t mk06_matrix[MATRIX_ROWS];
static bool         mk06_matrix_changed = false;

static uint8_t current_led = 0;

#ifndef MK06_LED_FORMAT
#    define MK06_LED_FORMAT 1
#endif
static uint8_t mk06_led_format = MK06_LED_FORMAT;
static bool    mk06_led_force  = false; // request a resend on the next scan
static bool    mk06_led_resend = false; // resend during the current scan

__attribute__((weak)) void mk06_port_activate(uint8_t portno) {}
__attribute__((weak)) void mk06_ws_activate(uint8_t ws) {}

// Optional extra tasks (e.g. the Host Port A USB bridge in mk06_hpa_bridge.c).
__attribute__((weak)) void mk06_bridge_init(void) {}
__attribute__((weak)) void mk06_bridge_task(void) {}

void mk06_set_led(uint8_t led) {
    current_led = led;
}

static void mk06_key(bool make, uint8_t code) {
#ifndef MK06_NO_US_ISO_FIXUP
    // Wey's US tables (e.g. PCSUS110) send the ISO "< > |" key (between
    // LShift and Z, AT key 45 = 0x2D) as AT key 14 (0x0E), which has no
    // position in the layout. Fold it back so ISO layouts get the key.
    if (code == 0x0E) {
        code = 0x2D;
    }
#endif
    uint8_t row = code >> 4;
    uint8_t col = code & 0x0F;
    if (make) {
        mk06_matrix[row] |= ((matrix_row_t)1 << col);
    } else {
        mk06_matrix[row] &= ~((matrix_row_t)1 << col);
    }
    mk06_matrix_changed = true;
}

/* ---------------------------------------------------------------------------
 * Hardware layer (RP2040 UART via ChibiOS SIO driver)
 * ------------------------------------------------------------------------- */

typedef SIODriver mk06_uart_t;

static void mk06_uart_start(mk06_uart_t *drv, pin_t tx, pin_t rx, uint32_t baud, bool invert) {
    iomode_t tx_mode = PAL_MODE_ALTERNATE_UART;
    iomode_t rx_mode = PAL_MODE_ALTERNATE_UART;
    if (invert) {
        // Port 3 (Sun style) uses inverted TTL serial.
        tx_mode |= PAL_RP_IOCTRL_OUTOVER_DRVINVPERI;
        rx_mode |= PAL_RP_IOCTRL_INOVER_INV;
    }
    palSetLineMode(tx, tx_mode);
    palSetLineMode(rx, rx_mode);

    // 8N1, FIFOs enabled.
    static SIOConfig cfgs[2];
    SIOConfig       *cfg = (drv == &SIOD0) ? &cfgs[0] : &cfgs[1];
    cfg->baud            = baud;
    cfg->UARTLCR_H       = UART_UARTLCR_H_WLEN_8BITS | UART_UARTLCR_H_FEN;
    cfg->UARTCR          = 0U;
    cfg->UARTIFLS        = UART_UARTIFLS_RXIFLSEL_1_8F | UART_UARTIFLS_TXIFLSEL_1_8E;
    cfg->UARTDMACR       = 0U;
    sioStart(drv, cfg);
}

static int16_t mk06_uart_getc(mk06_uart_t *drv) {
    if (sioIsRXEmptyX(drv)) {
        return -1;
    }
    msg_t    msg  = chnGetTimeout(drv, TIME_IMMEDIATE);
    uint32_t errs = 0;
    if (sioHasRXErrorsX(drv)) {
        errs = (uint32_t)sioGetAndClearErrors(drv);
    }
#ifdef MK06_RAW_LOG
    // Raw byte log, independent of the protocol state machine.
    if (msg >= 0) {
        xprintf("[%s %02X%s]\n", drv == &SIOD0 ? "U0" : "U1", (uint8_t)(msg & 0xFF), errs ? " ERR" : "");
    } else if (errs) {
        xprintf("[%s ERR %lX]\n", drv == &SIOD0 ? "U0" : "U1", (unsigned long)errs);
    }
#    ifdef MK06_ACTIVITY_LED
    gpio_toggle_pin(MK06_ACTIVITY_LED);
#    endif
#else
    (void)errs;
#endif
    return (msg < 0) ? -1 : (int16_t)(msg & 0xFF);
}

static void mk06_uart_write(mk06_uart_t *drv, const uint8_t *data, size_t len) {
    chnWrite(drv, data, len);
}

/* ---------------------------------------------------------------------------
 * MK06 protocol (unchanged from the original converter)
 * ------------------------------------------------------------------------- */

typedef enum { IDLE, MAKE, BREAK, HOST, DLINK } mk06_port_state_t;

typedef struct {
    uint8_t           portno;
    const char       *name;
    mk06_uart_t      *drv;
    mk06_port_state_t state;
} mk06_port_t;

#if SD1_PORT
static mk06_port_t sd1_port;
#endif
#if SD2_PORT
static mk06_port_t sd2_port;
#endif

static void mk06_port_init(mk06_port_t *port, uint8_t portno, mk06_uart_t *drv) {
    port->portno = portno;
    switch (portno) {
        case PORT_3a:
            port->name = "P3a";
            break;
        case PORT_3b:
            port->name = "P3b";
            break;
        case PORT_4:
            port->name = "P4";
            break;
        case PORT_5:
            port->name = "P5";
            break;
        case PORT_A:
            port->name = "HPA";
            break;
        case PORT_B:
            port->name = "HPB";
            break;
        case PORT_X:
            port->name = "Ext";
            break;
        default:
            port->name = "???";
            break;
    }
    port->drv = drv;
#if HOST_PORT
    if (portno == HOST_PORT) {
        port->state = HOST;
        return;
    }
#endif
#if DLINK_PORT
    if (portno == DLINK_PORT) {
        port->state = DLINK;
        return;
    }
#endif
    port->state = IDLE;
}

static void mk06_port_active(uint8_t portno) {
    static uint8_t active = NO_PORT;
    if (active != portno) {
        active = portno;
        mk06_port_activate(portno);
    }
}

static void host_recv(mk06_port_t *port, uint8_t data) {
    static enum { H_IDLE, H_MAKE, H_BREAK, H_WORKSTATION } state = H_IDLE;
    bool begin = false, end = false;

    switch (state) {
        case H_IDLE:
            begin = true;
            switch (data) {
                case 0x20:
                    state = H_MAKE;
                    break;
                case 0x21:
                    state = H_BREAK;
                    break;
                case 0x22:
                    state = H_WORKSTATION;
                    break;
                case 0x7F:
                case 0xC0:
                    // Just suppress end.
                    break;
                default:
                    end = true;
                    break;
            }
            break;

        case H_MAKE:
        case H_BREAK:
            // data is scan code set 3, mostly. Host Terminal mode only logs;
            // configure the keyboard to send keys via DLINK or an IDLE port.
            state = H_IDLE;
            end   = true;
            break;

        case H_WORKSTATION:
            mk06_ws_activate(data + 1);
            state = H_IDLE;
            end   = true;
            break;
    }

    if (debug_enable) {
        if (begin) {
            xprintf("%s>", port->name);
        }
        xprintf(" %02X", data);
        if (end) {
            xprintf("\n");
        }
    }
}

static void dlink_mouse(uint8_t addr, const uint8_t *mouse) {
#ifdef MOUSE_ENABLE
    report_mouse_t mouse_report = {0};
    if ((mouse[0] & 0x20) != 0) {
        mouse_report.buttons |= 1; // left button
    }
    if ((mouse[0] & 0x10) != 0) {
        mouse_report.buttons |= 2; // right button
    }
    if ((mouse[3] & 0x20) != 0) {
        mouse_report.buttons |= 4; // middle button
    }
    if ((mouse[3] & 0x10) != 0) {
        mouse_report.buttons |= 8; // fourth button
    }
    // x, y bytes seem to always have high bits zero and those bits appear earlier instead.
    mouse_report.x = (int8_t)(mouse[1] | ((mouse[0] & 0x03) << 6));
    mouse_report.y = (int8_t)(mouse[2] | ((mouse[0] & 0x0C) << 4));
    int8_t v       = mouse[3] & 0x0F;
    if (v & 0x08) {
        v |= 0xF0; // sign extend
    }
    mouse_report.v = -v; // direction reversed
    mouse_report.h = 0;
    host_mouse_send(&mouse_report);
#endif
}

static const uint8_t led_cmds[8] = {
    0x08, // None
    0x14, // Scroll Lock
    0x12, // Num Lock
    0x1A, // Num Lock, Scroll Lock
    0x10, // Caps Lock
    0x15, // Caps Lock, Scroll Lock
    0x19, // Caps Lock, Num Lock
    0x1D  // Caps Lock, Num Lock, Scroll Lock
};

static void dlink_recv(mk06_port_t *port, uint8_t data) {
    static enum { D_IDLE, D_MAKE, D_BREAK, MOUSE_1, MOUSE_2, MOUSE_3, MOUSE_4, GPIO, GPIO_OFF, GPIO_ON, GPIO_ON_2, IGNORE_1, IGNORE_2 } state = D_IDLE;
    static uint8_t prefix_addr = 0;
    bool           begin = false, end = false;

    switch (state) {
        case D_IDLE: {
            uint8_t addr = data & 0xF0;
            uint8_t cmd  = data & 0x0F;
            if (addr == 0x00 || addr == 0xF0) {
                begin = true;
                if (data == 0xF0) {
                    // F0 might be assigned VS-DEF DLINK module (how to tell?).
                    state = GPIO;
                    break;
                    // 00 08, FF 08, FD x0 08 sent at bootup.
                } else if (data == 0x00 || data == 0xFF) {
                    state = IGNORE_1;
                    break;
                } else if (data == 0xFD) {
                    state = IGNORE_2;
                    break;
                }
                end = true;
                break;
            }
            if (cmd == 0x06) {
                // Send LED changes to all active WS, since we are only one USB keyboard.
                static uint8_t leds[16] = {0};
                uint8_t        idx      = addr >> 4;
                if (leds[idx] != current_led) {
                    leds[idx] = current_led;

                    uint8_t msg[3];
                    msg[0] = addr;
                    msg[1] = led_cmds[current_led & 7];
                    msg[2] = 0x00;
                    mk06_uart_write(port->drv, msg, 3);

                    dprintf("%s< %02X %02X %02X\n", port->name, msg[0], msg[1], msg[2]);
                }
                // Do not dprint these; they are sent too often.
                return;
            }
            begin = true;
            switch (cmd) {
                case 0x00:
                    state = D_MAKE;
                    break;
                case 0x01:
                    state = D_BREAK;
                    break;
                case 0x02:
                    state = MOUSE_1;
                    break;
                default:
                    end = true;
                    break;
            }
            prefix_addr = addr;
            break;
        }

        case D_MAKE:
        case D_BREAK:
            mk06_port_active(PORT_DLINK + (prefix_addr >> 4));
            mk06_key(state == D_MAKE, data);
            state = D_IDLE;
            end   = true;
            break;

        case MOUSE_1:
            if ((data & 0x40) == 0) {
                // A0 seems to indicate fifth button.
                state = D_IDLE;
                end   = true;
                break;
            }
            /* else falls through */
        case MOUSE_2:
        case MOUSE_3:
        case MOUSE_4: {
            static uint8_t mouse[4];
            mouse[state - MOUSE_1] = data;
            if (state == MOUSE_4) {
                dlink_mouse(prefix_addr, mouse);
                state = D_IDLE;
                end   = true;
            } else {
                state++;
            }
            break;
        }

        case GPIO:
            switch (data) {
                case 0x81:
                    state = GPIO_OFF;
                    break;
                case 0x82:
                    state = GPIO_ON;
                    break;
                default:
                    end = true;
            }
            break;
        case GPIO_OFF:
            // TODO: Turn off data bit.
            state = D_IDLE;
            end   = true;
            break;
        case GPIO_ON:
            // TODO: Turn on data bit.
            state = GPIO_ON_2;
            break;
        case GPIO_ON_2:
            state = D_IDLE;
            end   = true;
            break;

        case IGNORE_1:
            state = D_IDLE;
            end   = true;
            break;
        case IGNORE_2:
            state = IGNORE_1;
            break;
    }

    if (debug_enable) {
        if (begin) {
            xprintf("%s>", port->name);
        }
        xprintf(" %02X", data);
        if (end) {
            xprintf("\n");
        }
    }
}

static void mk06_port_recv(mk06_port_t *port, uint8_t data) {
    switch (port->state) {
        case HOST:
            host_recv(port, data);
            return;
        case DLINK:
            dlink_recv(port, data);
            return;
        case IDLE:
            dprintf("%s> %02X", port->name, data);
            switch (data) {
                case 0x20:
                    port->state = MAKE;
                    break;
                case 0x21:
                    port->state = BREAK;
                    break;
                case 0x26:
                    // LED request, as on DLINK (addr 0x20 | cmd 6).
                    dprintf(" (LED req)\n");
                    mk06_led_force = true;
                    break;
                default:
                    dprintf("\n");
                    break;
            }
            return;
        case MAKE:
        case BREAK:
            dprintf(" %02X\n", data);
            mk06_port_active(port->portno);
            mk06_key(port->state == MAKE, data);
            port->state = IDLE;
            return;
    }
}

/*
 * LED output format for direct serial ports (P3/P4/P5). Not documented;
 * the keyboard sends DLINK-style frames with address 0x20 (0x20 make,
 * 0x21 break), so format 1 answers the same way the DLINK code does.
 *   0: raw bitmask (1=Scroll, 2=Num, 4=Caps)         - original converter
 *   1: 0x20, led_cmds[mask], 0x00                    - DLINK reply, addr 0x20
 *   2: led_cmds[mask]
 *   3: 0x20, led_cmds[mask]
 */

void mk06_set_led_format(uint8_t format) {
    mk06_led_format = format & 3;
    mk06_led_force  = true;
}

static void mk06_send_led(mk06_port_t *port) {
    uint8_t msg[3];
    uint8_t n   = 0;
    uint8_t cmd = led_cmds[current_led & 7];
    switch (mk06_led_format) {
        case 0:
            msg[n++] = current_led;
            break;
        case 1:
            msg[n++] = 0x20;
            msg[n++] = cmd;
            msg[n++] = 0x00;
            break;
        case 2:
            msg[n++] = cmd;
            break;
        case 3:
            msg[n++] = 0x20;
            msg[n++] = cmd;
            break;
    }
    mk06_uart_write(port->drv, msg, n);
    dprintf("%s< fmt%u:", port->name, mk06_led_format);
    for (uint8_t i = 0; i < n; i++) dprintf(" %02X", msg[i]);
    dprintf("\n");
}

static void mk06_port_scan(mk06_port_t *port, uint8_t idx) {
    if (port->state == IDLE) {
        static uint8_t leds[2] = {0};
        if (leds[idx] != current_led || mk06_led_resend) {
            leds[idx] = current_led;
            mk06_send_led(port);
        }
    }

    // Drain available bytes, but stop after the first key change so that a
    // make and break of the same key never collapse into one matrix scan.
    while (!mk06_matrix_changed) {
        int16_t c = mk06_uart_getc(port->drv);
        if (c < 0) {
            break;
        }
        mk06_port_recv(port, (uint8_t)c);
    }
}

/* ---------------------------------------------------------------------------
 * QMK custom_lite matrix hooks
 * ------------------------------------------------------------------------- */

#define MK06_IS_INVERTED(p) ((p) == PORT_3a || (p) == PORT_3b)

void matrix_init_custom(void) {
    memset(mk06_matrix, 0, sizeof(mk06_matrix));

#if defined(MK06_RAW_LOG) && defined(MK06_ACTIVITY_LED)
    gpio_set_pin_output(MK06_ACTIVITY_LED);
    gpio_write_pin_high(MK06_ACTIVITY_LED); // on at boot; toggles on every received byte
#endif

#if SD1_PORT
    mk06_uart_start(&SIOD0, SD1_TX_PIN, SD1_RX_PIN, SD1_BAUD, MK06_IS_INVERTED(SD1_PORT));
    mk06_port_init(&sd1_port, SD1_PORT, &SIOD0);
#endif
#if SD2_PORT
    mk06_uart_start(&SIOD1, SD2_TX_PIN, SD2_RX_PIN, SD2_BAUD, MK06_IS_INVERTED(SD2_PORT));
    mk06_port_init(&sd2_port, SD2_PORT, &SIOD1);
#endif
    mk06_bridge_init();
}

bool matrix_scan_custom(matrix_row_t current_matrix[]) {
    mk06_matrix_changed = false;
    mk06_led_resend     = mk06_led_force;
    mk06_led_force      = false;

#if SD1_PORT
    mk06_port_scan(&sd1_port, 0);
#endif
#if SD2_PORT
    mk06_port_scan(&sd2_port, 1);
#endif
    mk06_bridge_task();

    if (!mk06_matrix_changed) {
        return false;
    }
    memcpy(current_matrix, mk06_matrix, sizeof(mk06_matrix));
    return true;
}
