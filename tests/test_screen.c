/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "screen_model.h"

static const char *last(const struct corne_screen *s) {
    return s->history[(s->next + CORNE_HISTORY_SIZE - 1) % CORNE_HISTORY_SIZE];
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
    size_t count = s.count;
    assert(!corne_screen_key(&s, 7, 0xe0, 0));
    assert(s.count == count);
    s.caps_lock = true;
    expect_key(&s, 7, 4, 0, "A");
    expect_key(&s, 7, 4, 2, "a");
}

static void test_history(void) {
    struct corne_screen s;
    corne_screen_init(&s, 0, true);
    for (unsigned key = 1000; key < 1100; key++) corne_screen_key(&s, 7, key, 0);
    assert(s.count == 64);
    assert(strcmp(s.history[s.next], "K40C") == 0); /* oldest surviving event, 1036 */
    char lines[3][22];
    corne_screen_history_lines(&s, lines);
    assert(strstr(lines[2], "K44B") != NULL);
    for (int row = 0; row < 3; row++) assert(strlen(lines[row]) <= 21);
    corne_screen_key(&s, 7, 42, 0);
    assert(strcmp(last(&s), "Backspace") == 0 && s.count == 64);
    corne_screen_history_lines(&s, lines);
    assert(strstr(lines[2], "Backspace") != NULL);
    corne_screen_init(&s, 0, false);
    corne_screen_history_lines(&s, lines);
    assert(!lines[0][0] && !lines[1][0] && !lines[2][0]);
    /* Rapid macro events must all survive before the first render. */
    expect_key(&s, 7, 45, 0, "-");
    expect_key(&s, 7, 55, 4, "Alt+.");
    corne_screen_history_lines(&s, lines);
    assert(strcmp(lines[0], "- Alt+.") == 0);
}

static bool lit(const uint8_t *f, int x, int y) {
    return (f[y * 16 + x/8] & (0x80 >> (x%8))) != 0;
}

static void test_pixels(void) {
    struct corne_screen s;
    corne_screen_init(&s, 0, false);
    snprintf(s.layer, sizeof(s.layer), "Default");
    s.usb_output = true;
    s.batteries[0] = (struct corne_battery){true, true, 0};
    s.batteries[1] = (struct corne_battery){true, true, 20};
    s.batteries[2] = (struct corne_battery){true, true, 100};
    struct {uint8_t before[16], frame[1024], after[16];} guarded;
    memset(&guarded, 0xa5, sizeof(guarded));
    corne_screen_render(&s, guarded.frame);
    for (int i = 0; i < 16; i++) {
        assert(guarded.before[i] == 0xa5 && guarded.after[i] == 0xa5);
    }
    assert(!lit(guarded.frame, 10, 12)); /* empty at 0% */
    assert(lit(guarded.frame, 52, 12) && !lit(guarded.frame, 56, 12)); /* 20% */
    assert(lit(guarded.frame, 94, 12) && lit(guarded.frame, 110, 12)); /* full */
    uint8_t known[1024];
    memcpy(known, guarded.frame, sizeof(known));
    s.batteries[0].known = false;
    corne_screen_render(&s, guarded.frame);
    assert(memcmp(known, guarded.frame, sizeof(known)) != 0);
    memcpy(known, guarded.frame, sizeof(known));
    s.batteries[0].connected = false;
    corne_screen_render(&s, guarded.frame);
    assert(memcmp(known, guarded.frame, sizeof(known)) != 0);
    s.usb_powered = true;
    corne_screen_render(&s, guarded.frame);
    assert(lit(guarded.frame, 79, 10)); /* external power marker */
}

static void save_preview(const char *path) {
    struct corne_screen s;
    corne_screen_init(&s, 0, true);
    snprintf(s.layer, sizeof(s.layer), "Default");
    s.usb_output = true;
    s.modifiers = 8;
    s.batteries[0] = (struct corne_battery){true, true, 85};
    s.batteries[2] = (struct corne_battery){true, true, 65};
    const unsigned keys[] = {20,26,8,4,40,6,25,80,42,4,5,6};
    const unsigned mods[] = {0,0,0,2,0,8,8,0,0,0,0,0};
    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); i++)
        corne_screen_key(&s, 7, keys[i], mods[i]);
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
    test_power(); test_keys(); test_history(); test_pixels();
    if (argc == 2) save_preview(argv[1]);
    puts("Screen model: power, key formatting, history and pixels passed");
    return 0;
}
