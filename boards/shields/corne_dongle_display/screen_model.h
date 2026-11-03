/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CORNE_SCREEN_WIDTH 128
#define CORNE_SCREEN_HEIGHT 64
#define CORNE_FRAME_BYTES (CORNE_SCREEN_WIDTH * CORNE_SCREEN_HEIGHT / 8)
#define CORNE_TOKEN_SIZE 22
#define CORNE_KEY_COLUMNS 21

struct corne_battery {
    bool connected;
    bool known;
    uint8_t level;
};

struct corne_screen {
    char layer[16];
    bool usb_output;
    bool ble_connected;
    uint8_t profile;
    uint8_t modifiers;
    bool caps_lock;
    bool usb_powered;
    int64_t last_activity;
    struct corne_battery batteries[3]; /* L, D, R */
    struct corne_battery peripherals[2]; /* BLE slots, not physical sides */
    bool sides_known;
    uint8_t left_source;
    char last_key[CORNE_TOKEN_SIZE];
};

void corne_screen_init(struct corne_screen *s, int64_t now, bool powered);
void corne_screen_power(struct corne_screen *s, int64_t now, bool powered);
void corne_screen_peripheral_connection(struct corne_screen *s, uint8_t source, bool connected);
void corne_screen_peripheral_level(struct corne_screen *s, uint8_t source, uint8_t level);
void corne_screen_peripheral_side(struct corne_screen *s, uint8_t source, bool right);
bool corne_screen_visible(const struct corne_screen *s, int64_t now, int32_t timeout);
bool corne_screen_key(struct corne_screen *s, uint16_t page, uint32_t key, uint8_t mods);
void corne_screen_render(const struct corne_screen *s, uint8_t frame[CORNE_FRAME_BYTES]);
const uint8_t *corne_glyph(char c);
