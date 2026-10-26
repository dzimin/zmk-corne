/* Copyright (c) 2026 Corne Silver contributors
 * SPDX-License-Identifier: MIT
 */
#include "screen_model.h"
#include <stdio.h>
#include <string.h>

void corne_screen_init(struct corne_screen *s, int64_t now, bool powered) {
    memset(s, 0, sizeof(*s));
    s->usb_powered = powered;
    s->last_activity = now;
    s->batteries[1].connected = true;
}

void corne_screen_power(struct corne_screen *s, int64_t now, bool powered) {
    if (s->usb_powered && !powered) {
        s->last_activity = now;
    }
    s->usb_powered = powered;
}

bool corne_screen_visible(const struct corne_screen *s, int64_t now, int32_t timeout) {
    return s->usb_powered || now - s->last_activity < timeout;
}

static void append(char *text, const char *part) {
    size_t used = strlen(text);
    snprintf(text + used, CORNE_TOKEN_SIZE - used, "%s", part);
}

bool corne_screen_key(struct corne_screen *s, uint16_t page, uint32_t key, uint8_t mods) {
    if (page == 7 && key >= 0xe0 && key <= 0xe7) {
        return false;
    }
    char token[CORNE_TOKEN_SIZE] = {0};
    bool ctrl = mods & 0x11, shift = mods & 0x22;
    bool alt = mods & 0x44, gui = mods & 0x88;
    if (ctrl) append(token, "Ctrl+");
    if (alt) append(token, "Alt+");
    if (gui) append(token, "Cmd+");
    bool chord = ctrl || alt || gui;
    if (shift && chord) append(token, "S+");

    char name[16] = {0};
    if (page == 7 && key >= 4 && key <= 29) {
        name[0] = (char)((chord || (shift != s->caps_lock) ? 'A' : 'a') + key - 4);
    } else if (page == 7 && key >= 30 && key <= 39) {
        name[0] = (shift && !chord ? "!@#$%^&*()" : "1234567890")[key - 30];
    } else if (page == 7 && key >= 45 && key <= 56) {
        /* ISO non-US # (usage 50) has its own slot. These are US HID legends. */
        const char *plain = "-=[]\\#;'`,./";
        const char *shifted = "_+{}|~:\"~<>?";
        name[0] = (shift && !chord ? shifted : plain)[key - 45];
    } else if (page == 7 && key >= 58 && key <= 69) {
        snprintf(name, sizeof(name), "F%u", (unsigned)(key - 57));
    } else if (page == 7 && key >= 104 && key <= 115) {
        snprintf(name, sizeof(name), "F%u", (unsigned)(key - 91));
    } else if (page == 7) {
        const char *special = NULL;
        switch (key) {
        case 40: special = "Enter"; break;
        case 41: special = "Esc"; break;
        case 42: special = "Backspace"; break;
        case 43: special = "Tab"; break;
        case 44: special = "Space"; break;
        case 57: special = "Caps"; break;
        case 70: special = "Print"; break;
        case 71: special = "Scroll"; break;
        case 72: special = "Pause"; break;
        case 73: special = "Insert"; break;
        case 74: special = "Home"; break;
        case 75: special = "PgUp"; break;
        case 76: special = "Delete"; break;
        case 77: special = "End"; break;
        case 78: special = "PgDn"; break;
        case 79: special = "Right"; break;
        case 80: special = "Left"; break;
        case 81: special = "Down"; break;
        case 82: special = "Up"; break;
        }
        if (special) {
            if (shift && !chord) append(token, "S+");
            snprintf(name, sizeof(name), "%s", special);
        } else {
            snprintf(name, sizeof(name), "K%lX", (unsigned long)key);
        }
    } else if (page == 12) {
        const char *media = NULL;
        switch (key) {
        case 0xe2: media = "Mute"; break;
        case 0xe9: media = "Vol+"; break;
        case 0xea: media = "Vol-"; break;
        case 0xcd: media = "Play"; break;
        case 0xb5: media = "Next"; break;
        case 0xb6: media = "Prev"; break;
        }
        if (media) snprintf(name, sizeof(name), "%s", media);
        else snprintf(name, sizeof(name), "C%lX", (unsigned long)key);
    } else {
        snprintf(name, sizeof(name), "%X:%lX", page, (unsigned long)key);
    }
    if (strlen(token) + strlen(name) > CORNE_HISTORY_COLUMNS) {
        token[0] = 0;
        if (ctrl) append(token, "C+");
        if (alt) append(token, "A+");
        if (gui) append(token, "G+");
        if (shift) append(token, "S+");
    }
    append(token, name);
    /* The longest possible chord is abbreviated to fit one whole 21-cell row. */
    snprintf(s->history[s->next], CORNE_TOKEN_SIZE, "%s", token);
    s->next = (s->next + 1) % CORNE_HISTORY_SIZE;
    if (s->count < CORNE_HISTORY_SIZE) s->count++;
    return true;
}

void corne_screen_history_lines(const struct corne_screen *s, char lines[3][22]) {
    memset(lines, 0, 3 * 22);
    size_t row = 0, used = 0;
    for (size_t i = 0; i < s->count; i++) {
        const char *token = s->history[(s->next + CORNE_HISTORY_SIZE - s->count + i) %
                                       CORNE_HISTORY_SIZE];
        size_t len = strlen(token);
        if (used && used + 1 + len > CORNE_HISTORY_COLUMNS) {
            if (row == 2) {
                memcpy(lines[0], lines[1], 22);
                memcpy(lines[1], lines[2], 22);
                memset(lines[2], 0, 22);
            } else row++;
            used = 0;
        }
        if (used) lines[row][used++] = ' ';
        memcpy(lines[row] + used, token, len + 1);
        used += len;
    }
}

static void pixel(uint8_t *frame, int x, int y) {
    if (x >= 0 && x < 128 && y >= 0 && y < 64)
        frame[y * 16 + x / 8] |= (uint8_t)(0x80 >> (x % 8));
}

static void rect(uint8_t *f, int x, int y, int w, int h, bool fill) {
    for (int dy = 0; dy < h; dy++)
        for (int dx = 0; dx < w; dx++)
            if (fill || dx == 0 || dx == w-1 || dy == 0 || dy == h-1)
                pixel(f, x+dx, y+dy);
}

static void text(uint8_t *f, int x, int y, const char *s, int cells) {
    for (int i = 0; s[i] && i < cells; i++) {
        const uint8_t *rows = corne_glyph(s[i]);
        for (int dy = 0; dy < 7; dy++)
            for (int dx = 0; dx < 5; dx++)
                if (rows[dy] & (16 >> dx)) pixel(f, x+i*6+dx, y+dy);
    }
}

static void battery(uint8_t *f, int x, char label, struct corne_battery b) {
    char name[] = {label, 0};
    text(f, x, 11, name, 1);
    rect(f, x+8, 10, 21, 9, false);
    rect(f, x+29, 13, 2, 3, true);
    if (!b.connected) {
        text(f, x+17, 11, "x", 1);
    } else if (!b.known) {
        text(f, x+17, 11, "?", 1);
    } else {
        int level = b.level > 100 ? 100 : b.level;
        rect(f, x+10, 12, (17 * level + 50) / 100, 5, true);
        if (level <= 20) text(f, x+31, 11, "!", 1);
    }
}

void corne_screen_render(const struct corne_screen *s, uint8_t frame[CORNE_FRAME_BYTES]) {
    memset(frame, 0, CORNE_FRAME_BYTES);
    text(frame, 0, 0, s->layer, 14);
    char output[8];
    if (s->usb_output) snprintf(output, sizeof(output), "USB");
    else snprintf(output, sizeof(output), "BT%u%s", s->profile, s->ble_connected ? "" : "?");
    text(frame, 128 - (int)strlen(output) * 6, 0, output, 7);
    battery(frame, 0, 'L', s->batteries[0]);
    battery(frame, 42, 'D', s->batteries[1]);
    battery(frame, 84, 'R', s->batteries[2]);
    if (s->usb_powered) {
        /* A separate cable/plug marker, never a charging lightning bolt. */
        rect(frame, 79, 12, 4, 4, false);
        pixel(frame, 79, 10); pixel(frame, 82, 10);
        pixel(frame, 79, 11); pixel(frame, 82, 11);
        rect(frame, 80, 16, 2, 3, true);
    }
    const char *mods[] = {"Ctrl", "Alt", "Cmd", "Shift"};
    const uint8_t masks[] = {0x11, 0x44, 0x88, 0x22};
    const int xs[] = {1, 31, 55, 79};
    for (int i = 0; i < 4; i++) {
        text(frame, xs[i], 23, mods[i], 5);
        if (s->modifiers & masks[i]) rect(frame, xs[i]-1, 21, strlen(mods[i])*6+1, 11, false);
    }
    if (s->caps_lock) text(frame, 117, 23, "C", 1);
    for (int x = 0; x < 128; x += 2) pixel(frame, x, 33);
    char lines[3][22];
    corne_screen_history_lines(s, lines);
    for (int row = 0; row < 3; row++) text(frame, 0, 36 + row*10, lines[row], 21);
}
