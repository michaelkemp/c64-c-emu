#include "keyboard.h"

#include <string.h>

void keyboard_matrix_init(KeyboardMatrix *kb) {
    memset(kb, 0, sizeof(*kb));
}

void keyboard_matrix_set_key(KeyboardMatrix *kb, C64Key key, bool down) {
    int pa = (int)key / 8;
    int pb = (int)key % 8;
    kb->pressed[pa][pb] = down;
}

bool keyboard_matrix_is_key_down(const KeyboardMatrix *kb, C64Key key) {
    int pa = (int)key / 8;
    int pb = (int)key % 8;
    return kb->pressed[pa][pb];
}

uint8_t keyboard_matrix_sense_pulldown(const KeyboardMatrix *kb, uint8_t select_port_value) {
    uint8_t pulldown = 0;
    for (int pa = 0; pa < 8; pa++) {
        bool column_selected = ((select_port_value >> pa) & 1u) == 0; /* active low */
        if (!column_selected) {
            continue;
        }
        for (int pb = 0; pb < 8; pb++) {
            if (kb->pressed[pa][pb]) {
                pulldown = (uint8_t)(pulldown | (1u << pb));
            }
        }
    }
    return pulldown;
}

void joystick_init(Joystick *js) {
    memset(js, 0, sizeof(*js));
}

uint8_t joystick_pulldown(const Joystick *js) {
    uint8_t mask = 0;
    if (js->up) mask |= 0x01u;
    if (js->down) mask |= 0x02u;
    if (js->left) mask |= 0x04u;
    if (js->right) mask |= 0x08u;
    if (js->fire) mask |= 0x10u;
    return mask;
}
