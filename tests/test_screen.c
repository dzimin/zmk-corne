/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "screen_model.h"

static const char *last(const struct corne_screen *s) {
    return s->last_key;
}

static void expect_key(struct corne_screen *s, unsigned page, unsigned key,
                       unsigned mods, const char *expected) {
    assert(corne_screen_key(s, page, key, mods));
    assert(strcmp(last(s), expected) == 0);
}

static void test_power(void) {
    struct corne_screen s;
    corne_screen_init(&s, 100, false);
    assert(corne_screen_visible(&s, 30099, 30000));
    assert(!corne_screen_visible(&s, 30100, 30000));
    s.last_activity = 40000;
    assert(corne_screen_visible(&s, 40000, 30000));
    corne_screen_power(&s, 50000, true);
    assert(corne_screen_visible(&s, 10000000, 30000));
    corne_screen_power(&s, 10000000, false);
    assert(corne_screen_visible(&s, 10029999, 30000));
    assert(!corne_screen_visible(&s, 10030000, 30000));
    corne_screen_power(&s, 10040000, true);
    assert(corne_screen_visible(&s, 10040000, 30000));
}

static void test_keys(void) {
    struct corne_screen s;
    corne_screen_init(&s, 0, false);
    expect_key(&s, 7, 4, 0, "a");
    expect_key(&s, 7, 4, 2, "A");
    expect_key(&s, 7, 4, 32, "A");
    expect_key(&s, 7, 6, 8, "Cmd+C");
    expect_key(&s, 7, 6, 0x0a, "Cmd+S+C");
    expect_key(&s, 7, 30, 2, "!");
    expect_key(&s, 7, 39, 2, ")");
    expect_key(&s, 7, 47, 2, "{");
    expect_key(&s, 7, 52, 0, "'");
    expect_key(&s, 7, 52, 2, "\"");
    expect_key(&s, 7, 55, 4, "Alt+.");
    expect_key(&s, 7, 42, 0, "Backspace");
    expect_key(&s, 7, 40, 0, "Enter");
    expect_key(&s, 7, 80, 0, "Left");
    expect_key(&s, 7, 80, 2, "S+Left");
    expect_key(&s, 7, 42, 15, "C+A+G+S+Backspace");
    expect_key(&s, 7, 69, 0, "F12");
    expect_key(&s, 7, 115, 0, "F24");
    expect_key(&s, 12, 0xe9, 0, "Vol+");
    expect_key(&s, 7, 0xfe, 0, "KFE");
    assert(!corne_screen_key(&s, 7, 0xe0, 0));
    assert(strcmp(last(&s), "KFE") == 0);
    s.caps_lock = true;
    expect_key(&s, 7, 4, 0, "A");
    expect_key(&s, 7, 4, 2, "a");
}

static void test_battery_sides(void) {
    for (uint8_t left_source = 0; left_source < 2; left_source++) {
        uint8_t right_source = 1 - left_source;
        struct corne_screen s;
        corne_screen_init(&s, 100, false);
        corne_screen_peripheral_connection(&s, left_source, true);
        corne_screen_peripheral_connection(&s, right_source, true);
        corne_screen_peripheral_level(&s, left_source, 87);
        corne_screen_peripheral_level(&s, right_source, 12);
        /* BAS can arrive first. Do not guess which physical half it belongs to. */
        assert(!s.batteries[0].known && !s.batteries[2].known);
        assert(s.batteries[0].connected && s.batteries[2].connected);
        corne_screen_peripheral_side(&s, right_source, true);
        assert(s.batteries[0].known && s.batteries[0].level == 87);
        assert(s.batteries[2].known && s.batteries[2].level == 12);
        corne_screen_peripheral_side(&s, left_source, false);
        assert(s.left_source == left_source);
        corne_screen_peripheral_level(&s, right_source, 0);
        assert(s.batteries[2].known && s.batteries[2].level == 0);
        /* Sleep/disconnection invalidates only that half; no stale charge on reconnect. */
        corne_screen_peripheral_connection(&s, right_source, false);
        corne_screen_peripheral_level(&s, right_source, 55);
        corne_screen_peripheral_side(&s, right_source, false);
        assert(!s.batteries[2].connected && !s.batteries[2].known);
        assert(s.batteries[0].known && s.batteries[0].level == 87);
        corne_screen_peripheral_connection(&s, right_source, true);
        assert(s.batteries[2].connected && !s.batteries[2].known);
        corne_screen_peripheral_level(&s, right_source, 54);
        assert(s.batteries[2].known && s.batteries[2].level == 54);
        corne_screen_peripheral_level(&s, right_source, 101);
        assert(!s.batteries[2].known);
        /* Reject local/generated events and out-of-range BLE slots. */
        struct corne_screen before = s;
        for (unsigned source = 2; source <= UINT8_MAX; source++) {
            corne_screen_peripheral_connection(&s, source, true);
            corne_screen_peripheral_level(&s, source, 50);
            corne_screen_peripheral_side(&s, source, false);
        }
        assert(memcmp(&s, &before, sizeof(s)) == 0);
        assert(s.last_activity == 100); /* Battery updates do not count as typing. */
        /* After receiver restart a key can precede the first BAS report. */
        corne_screen_init(&s, 0, false);
        corne_screen_peripheral_connection(&s, left_source, true);
        corne_screen_peripheral_side(&s, left_source, false);
        assert(s.batteries[0].connected && !s.batteries[0].known);
        assert(!s.batteries[2].connected);
        corne_screen_peripheral_level(&s, left_source, 77);
        assert(s.batteries[0].known && s.batteries[0].level == 77);
    }
}

static void test_last_key(void) {
    struct corne_screen s;
    corne_screen_init(&s, 0, true);
    assert(s.last_key[0] == 0);
    expect_key(&s, 7, 42, 15, "C+A+G+S+Backspace");
    expect_key(&s, 7, 4, 0, "a"); /* A shorter event replaces the whole previous one. */
    expect_key(&s, 7, 6, 8, "Cmd+C");
    for (unsigned key = 0xe0; key <= 0xe7; key++)
        assert(!corne_screen_key(&s, 7, key, 0));
    assert(strcmp(last(&s), "Cmd+C") == 0);
    s.modifiers = 0; /* Releasing modifiers must not erase the last combination. */
    corne_screen_power(&s, 0, false);
    assert(!corne_screen_visible(&s, 30000, 30000));
    corne_screen_power(&s, 40000, true);
    assert(strcmp(last(&s), "Cmd+C") == 0);
    /* A rapid macro retains only its final processed key, even before a render. */
    expect_key(&s, 7, 45, 0, "-");
    expect_key(&s, 7, 55, 4, "Alt+.");
    for (unsigned key = 1000; key < 1100; key++) corne_screen_key(&s, 7, key, 0);
    assert(strcmp(last(&s), "K44B") == 0);
    assert(strlen(last(&s)) <= CORNE_KEY_COLUMNS);
    corne_screen_init(&s, 0, false);
    assert(s.last_key[0] == 0);
}

static bool lit(const uint8_t *f, int x, int y) {
    return (f[y * 16 + x/8] & (0x80 >> (x%8))) != 0;
}

static void expect_text_pixels(const uint8_t *frame, int x, int y, const char *value,
                               int fill_end) {
    for (size_t i = 0; value[i]; i++) {
        const uint8_t *glyph = corne_glyph(value[i]);
        for (int dy = 0; dy < 7; dy++) {
            for (int dx = 0; dx < (value[i+1] ? 6 : 5); dx++) {
                int px = x + (int)i*6 + dx;
                bool stroke = dx < 5 && (glyph[dy] & (16 >> dx));
                assert(lit(frame, px, y+dy) == ((px < fill_end) != stroke));
            }
        }
    }
}

static void test_pixels(void) {
    struct corne_screen s;
    corne_screen_init(&s, 0, false);
    snprintf(s.layer, sizeof(s.layer), "Default");
    s.usb_output = true;
    struct {uint8_t before[16], frame[1024], after[16];} guarded;
    memset(&guarded, 0xa5, sizeof(guarded));
    /* Every percentage, on every battery: glyphs invert individually where
     * the fill crosses them, including the digit-width changes at 10 and 100. */
    for (int level = 0; level <= 100; level++) {
        for (int slot = 0; slot < 3; slot++)
            s.batteries[slot] = (struct corne_battery){true, true, level};
        corne_screen_render(&s, guarded.frame);
        for (int slot = 0; slot < 3; slot++) {
            int x = slot*45, width = (32*level + 50)/100;
            char value[5];
            snprintf(value, sizeof(value), "%d%%", level);
            expect_text_pixels(guarded.frame, x+(36-((int)strlen(value)*6-1))/2,
                               26, value, x+2+width);
            for (int dx = 0; dx < 32; dx++) {
                assert(lit(guarded.frame, x+2+dx, 23) == (dx < width));
                assert(lit(guarded.frame, x+2+dx, 35) == (dx < width));
            }
            assert(lit(guarded.frame, x, 21) && lit(guarded.frame, x+35, 37));
            assert(!lit(guarded.frame, x+1, 25)); /* Keep the outline gap. */
            assert(lit(guarded.frame, x+37, 31));
            assert(lit(guarded.frame, x+20, 11) == (level <= 20)); /* ! */
        }
    }
    for (int i = 0; i < 16; i++)
        assert(guarded.before[i] == 0xa5 && guarded.after[i] == 0xa5);
    s.batteries[0].level = 255;
    corne_screen_render(&s, guarded.frame);
    expect_text_pixels(guarded.frame, 6, 26, "100%", 34);
    s.batteries[0].known = false;
    corne_screen_render(&s, guarded.frame);
    expect_text_pixels(guarded.frame, 15, 26, "?", -1);
    assert(!lit(guarded.frame, 2, 23) && !lit(guarded.frame, 20, 11));
    s.batteries[0].connected = false;
    corne_screen_render(&s, guarded.frame);
    expect_text_pixels(guarded.frame, 15, 26, "x", -1);
    assert(!lit(guarded.frame, 2, 35));
    s.usb_powered = true;
    s.usb_output = false; s.profile = 5; s.ble_connected = false;
    s.modifiers = 0xff; s.caps_lock = true;
    corne_screen_key(&s, 7, 42, 15);
    corne_screen_render(&s, guarded.frame);
    expect_text_pixels(guarded.frame, 105, 0, "BT5?", -1);
    assert(lit(guarded.frame, 70, 10)); /* USB power independent of selected output. */
    expect_text_pixels(guarded.frame, 117, 44, "C", -1);
    expect_text_pixels(guarded.frame, 0, 57, "C+A+G+S+Backspace", -1);
    assert(lit(guarded.frame, 0, 42) && lit(guarded.frame, 108, 52));
    s.modifiers = 0;
    corne_screen_render(&s, guarded.frame);
    assert(!lit(guarded.frame, 0, 42));
    expect_text_pixels(guarded.frame, 0, 57, "C+A+G+S+Backspace", -1);
    corne_screen_init(&s, 0, false);
    corne_screen_render(&s, guarded.frame);
    expect_text_pixels(guarded.frame, 0, 57, "-", -1);
}

static void save_preview(const char *path) {
    struct corne_screen s;
    corne_screen_init(&s, 0, true);
    snprintf(s.layer, sizeof(s.layer), "Default");
    s.usb_output = true;
    s.modifiers = 8;
    s.batteries[0] = (struct corne_battery){true, true, 85};
    s.batteries[2] = (struct corne_battery){true, true, 52};
    corne_screen_key(&s, 7, 6, 8);
    uint8_t frame[1024];
    corne_screen_render(&s, frame);
    FILE *out = fopen(path, "wb");
    assert(out);
    fprintf(out, "P4\n128 64\n");
    /* PBM uses 1 for black, whereas the screen uses 1 for white. */
    for (size_t i = 0; i < sizeof(frame); i++) fputc(frame[i] ^ 255, out);
    assert(fclose(out) == 0);
}

int main(int argc, char **argv) {
    test_power(); test_keys(); test_battery_sides(); test_last_key(); test_pixels();
    if (argc == 2) save_preview(argv[1]);
    puts("Screen model: power, battery sides, key formatting, last key and inverted battery pixels passed");
    return 0;
}
