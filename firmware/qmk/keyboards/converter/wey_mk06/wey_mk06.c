// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"

extern void mk06_set_led(uint8_t led);

// Forward host LED state to the MK06 (sent over serial by mk06_serial.c).
bool led_update_kb(led_t led_state) {
    bool res = led_update_user(led_state);
    if (res) {
        uint8_t led = 0;
        if (led_state.scroll_lock) led |= 1;
        if (led_state.num_lock) led |= 2;
        if (led_state.caps_lock) led |= 4;
        mk06_set_led(led);
    }
    return res;
}
