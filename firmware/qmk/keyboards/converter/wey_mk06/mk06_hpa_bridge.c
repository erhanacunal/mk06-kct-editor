// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * USB <-> Host Port A bridge.
 *
 * The Pico shows up as a USB serial port (CDC ACM, e.g. /dev/ttyACM0) next to
 * the keyboard. Every byte from that port goes out on UART1 to the MK06 Host
 * Port A, and every byte coming back from Host Port A goes to the USB serial
 * port. With this, tools such as weytool (github.com/funkenzuzzler/weytools)
 * can list, read and write files on the keyboard:
 *
 *     weytool -D /dev/ttyACM0 -l
 *
 * Wiring: Pico GP4 (UART1 TX) -> MAX3232 -> HPCN36 pin 3  (HPA RXD)
 *         Pico GP5 (UART1 RX) <- MAX3232 <- HPCN36 pin 21 (HPA TXD)
 *
 * The baud rate set by the PC on the USB serial port is ignored; the UART
 * always runs at MK06_HPA_BAUD.
 *
 * Enable from a keymap's rules.mk:
 *     VIRTSER_ENABLE = yes
 *     SRC += mk06_hpa_bridge.c
 */

#include <hal.h>
#include "quantum.h"
#include "virtser.h"
#include "eeconfig.h"

#ifndef MK06_HPA_BAUD
#    define MK06_HPA_BAUD 115200
#endif
#ifndef MK06_HPA_TX_PIN
#    define MK06_HPA_TX_PIN SD2_TX_PIN
#endif
#ifndef MK06_HPA_RX_PIN
#    define MK06_HPA_RX_PIN SD2_RX_PIN
#endif
#define MK06_HPA_UART SIOD1

// Bytes from Host Port A are only forwarded while a PC program is talking to
// the bridge: from the first byte it sends until MK06_HPA_IDLE_MS of silence in
// both directions. Otherwise they are dropped, because sending to a USB serial
// port nobody has open blocks for up to 100 ms per packet and would stall typing.
#ifndef MK06_HPA_IDLE_MS
#    define MK06_HPA_IDLE_MS 3000
#endif
static bool     hpa_session;
static uint32_t hpa_last;

#if defined(SD2_PORT) && SD2_PORT
#    error The HPA bridge uses UART1; set SD2_PORT to NO_PORT
#endif

// Bytes from the keyboard. A separate thread empties the UART's 32-byte RX FIFO
// into this ring every 250 us, so nothing is lost while the main loop is busy
// (sending to USB can block it for milliseconds). The main loop is the only
// reader, the thread the only writer.
#define RXBUF_SIZE 4096
static uint8_t           rxbuf[RXBUF_SIZE];
static volatile uint16_t rx_head, rx_tail;

static uint32_t          hpa_baud = MK06_HPA_BAUD;
static volatile uint32_t hpa_tx_count, hpa_rx_count, hpa_err_count, hpa_drop_count;
static volatile bool     hpa_uart_ready;

static void hpa_pump_rx(void) {
    if (!hpa_uart_ready) return;
    while (!sioIsRXEmptyX(&MK06_HPA_UART)) {
        if (sioHasRXErrorsX(&MK06_HPA_UART)) {
            (void)sioGetAndClearErrors(&MK06_HPA_UART);
            hpa_err_count++;
        }
        uint8_t  c    = (uint8_t)sioGetX(&MK06_HPA_UART);
        uint16_t next = (rx_head + 1) % RXBUF_SIZE;
        hpa_rx_count++;
        if (next == rx_tail) {
            hpa_drop_count++;
            continue;
        }
        rxbuf[rx_head] = c;
        rx_head        = next;
    }
}

static THD_WORKING_AREA(wa_hpa_rx, 256);
static THD_FUNCTION(hpa_rx_thread, arg) {
    (void)arg;
    chRegSetThreadName("hpa_rx");
    while (true) {
        hpa_pump_rx();
        chThdSleepMicroseconds(250);
    }
}

static void hpa_uart_start(uint32_t baud) {
    static SIOConfig cfg;
    hpa_uart_ready = false;
    chThdSleepMicroseconds(500); // let the RX thread finish its current pass
    sioStop(&MK06_HPA_UART);
    cfg.baud      = baud;
    cfg.UARTLCR_H = UART_UARTLCR_H_WLEN_8BITS | UART_UARTLCR_H_FEN; // 8N1
    cfg.UARTCR    = 0U;
    cfg.UARTIFLS  = UART_UARTIFLS_RXIFLSEL_1_8F | UART_UARTIFLS_TXIFLSEL_1_8E;
    cfg.UARTDMACR = 0U;
    sioStart(&MK06_HPA_UART, &cfg);
    hpa_baud       = baud;
    hpa_uart_ready = true;
}

// The baud rate found by the Ctrl+Shift+F11 scan is kept in the user EEPROM word.
static bool hpa_baud_valid(uint32_t b) {
    return b >= 1200 && b <= 230400;
}

void mk06_bridge_init(void) {
    palSetLineMode(MK06_HPA_TX_PIN, PAL_MODE_ALTERNATE_UART);
    palSetLineMode(MK06_HPA_RX_PIN, PAL_MODE_ALTERNATE_UART);
    uint32_t saved = eeconfig_read_user();
    hpa_uart_start(hpa_baud_valid(saved) ? saved : MK06_HPA_BAUD);
    chThdCreateStatic(wa_hpa_rx, sizeof(wa_hpa_rx), HIGHPRIO, hpa_rx_thread, NULL);
}

// Called by QMK for every byte the PC writes to the USB serial port.
void virtser_recv(uint8_t c) {
    hpa_session = true;
    hpa_last    = timer_read32();
    while (sioIsTXFullX(&MK06_HPA_UART)) {
        chThdSleepMicroseconds(50); // the RX thread keeps receiving meanwhile
    }
    sioPutX(&MK06_HPA_UART, c);
    hpa_tx_count++;
}

#ifdef MK06_HPA_DIAG
/* ---------------------------------------------------------------------------
 * Diagnostics (only with MK06_HPA_DIAG defined), typed into the focused window (only A-Z, 0-9 and spaces, so the
 * text reads the same with US and Turkish Q host layouts):
 *   mk06_hpa_probe(false)  send "7F E8" (keyboard ID) at the current baud rate
 *                          and type the reply, e.g. "HPA 115200 YANIT 31"
 *   mk06_hpa_probe(true)   same for every rate in hpa_bauds; the first rate
 *                          that gets a reply stays selected
 *   mk06_hpa_status()      type baud rate and byte/error counters
 * ------------------------------------------------------------------------- */

static const uint32_t hpa_bauds[] = {115200, 57600, 38400, 19200, 9600, 4800, 2400, 1200};
#define HPA_NBAUDS (sizeof(hpa_bauds) / sizeof(hpa_bauds[0]))
#define HPA_PROBE_MS 400

static int8_t   probe_step = -1; // -1 idle, else index into hpa_bauds (or 0 for single)
static bool     probe_scan, probe_found;
static uint32_t probe_start;
static uint8_t  probe_buf[8], probe_len;
static uint32_t probe_errs0;
static char     report[160];
static uint8_t  report_len;


static void report_add(const char *str) {
    while (*str && report_len < sizeof(report) - 1) report[report_len++] = *str++;
    report[report_len] = 0;
}

static void report_num(uint32_t v) {
    char tmp[11]; int n = 0;
    do { tmp[n++] = '0' + v % 10; v /= 10; } while (v);
    while (n) { char c[2] = {tmp[--n], 0}; report_add(c); }
}

static void report_hex(uint8_t v) {
    static const char hx[] = "0123456789ABCDEF";
    char c[3] = {hx[v >> 4], hx[v & 15], 0};
    report_add(c);
}

static void probe_send(void) {
    probe_len   = 0;
    probe_errs0 = hpa_err_count;
    probe_start = timer_read32();
    sioPutX(&MK06_HPA_UART, 0x7F);
    sioPutX(&MK06_HPA_UART, 0xE8);
    hpa_tx_count += 2;
}

void mk06_hpa_probe(bool scan) {
    if (probe_step >= 0) return;
    probe_scan  = scan;
    probe_found = false;
    report_len  = 0;
    report[0]   = 0;
    probe_step  = 0;
    if (scan) hpa_uart_start(hpa_bauds[0]);
    probe_send();
}

void mk06_hpa_status(void) {
    report_len = 0;
    report_add("HPA ");
    report_num(hpa_baud);
    report_add(" TX ");
    report_num(hpa_tx_count);
    report_add(" RX ");
    report_num(hpa_rx_count);
    report_add(" HATA ");
    report_num(hpa_err_count);
    report_add(" KAYIP ");
    report_num(hpa_drop_count);
    report_add(" ");
}

static void probe_task(void) {
    if (probe_step < 0 || timer_elapsed32(probe_start) < HPA_PROBE_MS) return;
    report_add("HPA ");
    report_num(hpa_baud);
    if (probe_len) {
        report_add(" YANIT");
        for (uint8_t i = 0; i < probe_len; i++) { report_add(" "); report_hex(probe_buf[i]); }
        probe_found = true;
    } else {
        report_add(" YOK");
    }
    if (hpa_err_count != probe_errs0) {
        report_add(" HATA ");
        report_num(hpa_err_count - probe_errs0);
    }
    report_add(probe_scan ? "  " : " ");

    if (probe_scan && !probe_found && ++probe_step < (int8_t)HPA_NBAUDS) {
        hpa_uart_start(hpa_bauds[probe_step]);
        probe_send();
        return;
    }
    if (probe_scan && !probe_found) {
        report_add("SONUC YOK ");
        hpa_uart_start(MK06_HPA_BAUD);
    }
    if (probe_scan && probe_found) {
        eeconfig_update_user(hpa_baud);
        report_add("KAYDEDILDI ");
    }
    probe_step = -1;
}

static void report_task(void) {
    // Type the report once the probe is done and Ctrl/Shift have been released.
    if (!report_len || probe_step >= 0 || get_mods()) return;
    report_len = 0;
    send_string(report);
}

#endif // MK06_HPA_DIAG

// Called once per matrix scan.
void mk06_bridge_task(void) {
#ifdef MK06_HPA_DIAG
    probe_task();
    report_task();

    if (probe_step >= 0) {
        // Probe running: its reply goes to probe_buf, not to the PC.
        while (rx_tail != rx_head) {
            if (probe_len < sizeof(probe_buf)) probe_buf[probe_len++] = rxbuf[rx_tail];
            rx_tail = (rx_tail + 1) % RXBUF_SIZE;
        }
        return;
    }
#endif
    if (hpa_session && timer_elapsed32(hpa_last) > MK06_HPA_IDLE_MS) {
        hpa_session = false;
    }
    if (!hpa_session) {
        rx_tail = rx_head; // nobody listening: drop
        return;
    }
    if (rx_tail != rx_head) {
        hpa_last = timer_read32();
    }
    while (rx_tail != rx_head) {
        virtser_send(rxbuf[rx_tail]);
        rx_tail = (rx_tail + 1) % RXBUF_SIZE;
    }
}
