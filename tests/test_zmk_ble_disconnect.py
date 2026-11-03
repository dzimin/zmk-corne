#!/usr/bin/env python3
"""Host regression check for ZMK split battery disconnect callbacks.

SPDX-License-Identifier: MIT
Extracted ZMK functions/event declaration:
Copyright (c) 2020 The ZMK Contributors; see the source tree's LICENSE.

Three functions and the event declaration are extracted verbatim from the
provided ZMK source. The supplied patch is applied with patch -p1 to a temporary
copy of that source tree, never to the original. Queues, work scheduling and BLE
objects are stubs, not a Zephyr emulation. The fixtures called left/right stand
for slots 0/1; they do not establish actual physical handedness.

This is NOT a Zephyr firmware build or an RF/power/hardware test.
Requires Python 3.9+, patch, and GCC-compatible cc with ASan and UBSan on Linux or macOS.
"""
from pathlib import Path
import argparse
import hashlib
import shutil
import tempfile
import json
import os
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zmk-root', type=Path, required=True,
                    help='Original ZMK tree containing app/src/split/bluetooth/central.c')
parser.add_argument('--patch', type=Path, required=True,
                    help='Candidate patch with paths relative to the ZMK root after -p1')
parser.add_argument('--output-dir', type=Path, required=True,
                    help='Directory for generated host sources, executables and results')
args = parser.parse_args()
zmk_root = args.zmk_root.resolve(strict=True)
patch_path = args.patch.resolve(strict=True)
output_dir = args.output_dir.resolve()
central_path = zmk_root / 'app/src/split/bluetooth/central.c'
event_path = zmk_root / 'app/include/zmk/events/battery_state_changed.h'
original_bytes = central_path.read_bytes()
original = original_bytes.decode('utf-8')
header = event_path.read_text()

# Apply the caller's actual patch, without recreating its changes in Python.
# A fresh temporary copy ensures the provided ZMK tree remains untouched.
with tempfile.TemporaryDirectory(prefix='zmk-ble-disconnect-repro-') as tmp:
    patched_root = Path(tmp) / 'zmk'
    shutil.copytree(zmk_root, patched_root)
    result = subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i', str(patch_path)],
                            cwd=patched_root, capture_output=True, text=True)
    if result.returncode:
        raise SystemExit('Patch failed in the temporary ZMK copy:\n' + result.stdout + result.stderr)
    patched = (patched_root / 'app/src/split/bluetooth/central.c').read_text()
    if patched == original:
        raise SystemExit('The supplied patch did not change central.c')
    if (patched_root / 'app/include/zmk/events/battery_state_changed.h').read_text() != header:
        raise SystemExit('This regression harness does not support patches changing the event header')

if central_path.read_bytes() != original_bytes:
    raise SystemExit('The original central.c changed during the run; results are not trustworthy')
output_dir.mkdir(parents=True, exist_ok=True)
(output_dir / 'central.patched.c').write_text(patched)
(output_dir / 'provenance.json').write_text(json.dumps({
    'zmk_root': str(zmk_root),
    'patch': str(patch_path),
    'original_central_sha256': hashlib.sha256(original_bytes).hexdigest(),
    'patch_sha256': hashlib.sha256(patch_path.read_bytes()).hexdigest(),
    'scope': 'Extracted-function host test with stubs; not a Zephyr build or hardware test',
}, indent=2) + '\n')

def extract(text, marker):
    """Extract one known function/declaration by matching its braces."""
    start = text.index(marker)
    end = text.index('{', start)
    depth = 1
    end += 1
    while depth:
        if text[end] == '{': depth += 1
        if text[end] == '}': depth -= 1
        end += 1
    return text[start:end], text[:start].count('\n') + 1

event, _ = extract(header, 'struct zmk_peripheral_battery_state_changed {')
stubs = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define ZMK_SPLIT_BLE_PERIPHERAL_COUNT 2
#define CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING 1
#define CONFIG_ZMK_INPUT_SPLIT 0
#define IS_ENABLED(x) (x)
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define BT_ADDR_LE_STR_LEN 32
#define K_NO_WAIT 0
#define LOG_DBG(...) ((void)0)
#define LOG_WRN(...) ((void)0)
struct bt_conn { int identity; };
struct peripheral_slot { struct bt_conn *conn; };
struct k_work { int unused; };
static struct peripheral_slot peripherals[ZMK_SPLIT_BLE_PERIPHERAL_COUNT];
static uint8_t peripheral_battery_levels[ZMK_SPLIT_BLE_PERIPHERAL_COUNT];
static int peripheral_batt_lvl_msgq;
static struct k_work peripheral_batt_lvl_work;
static int get_index, put_index, submit_count, raised_count, released_count, scan_count;
static struct zmk_peripheral_battery_state_changed queued[32];
static struct zmk_peripheral_battery_state_changed raised[32];
static int k_msgq_put(void *q, const void *ev, int timeout) {
    (void)q; (void)timeout;
    assert(put_index < (int)ARRAY_SIZE(queued));
    queued[put_index++] = *(const struct zmk_peripheral_battery_state_changed *)ev;
    return 0;
}
static int k_msgq_get(void *q, void *ev, int timeout) {
    (void)q; (void)timeout;
    if (get_index == put_index) return -EAGAIN;
    *(struct zmk_peripheral_battery_state_changed *)ev = queued[get_index++];
    return 0;
}
static int k_work_submit(struct k_work *work) {
    (void)work; submit_count++; return 0;
}
static int raise_zmk_peripheral_battery_state_changed(struct zmk_peripheral_battery_state_changed ev) {
    assert(raised_count < (int)ARRAY_SIZE(raised));
    raised[raised_count++] = ev;
    return 0;
}
static const struct bt_conn *bt_conn_get_dst(const struct bt_conn *conn) { return conn; }
static void bt_addr_le_to_str(const struct bt_conn *conn, char *out, size_t len) {
    snprintf(out, len, "conn-%d", conn->identity);
}
static int start_scanning(void) { scan_count++; return 0; }
int peripheral_slot_index_for_conn(struct bt_conn *conn);
static int release_peripheral_slot_for_conn(struct bt_conn *conn) {
    int idx = peripheral_slot_index_for_conn(conn);
    if (idx < 0) return idx;
    peripherals[idx].conn = NULL;
    released_count++;
    return 0;
}
'''
tests = r'''
static void reset_test(struct bt_conn *left, struct bt_conn *right) {
    peripherals[0].conn = left;
    peripherals[1].conn = right;
    peripheral_battery_levels[0] = 91;
    peripheral_battery_levels[1] = 72;
    get_index = put_index = submit_count = raised_count = released_count = scan_count = 0;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    struct bt_conn left = {11}, right = {22}, host = {33};
    reset_test(&left, &right);
    if (!strcmp(argv[1], "left") || !strcmp(argv[1], "right")) {
        int idx = !strcmp(argv[1], "right");
        split_central_disconnected(idx ? &right : &left, 0x13);
        assert(put_index == 1 && queued[0].source == idx && queued[0].state_of_charge == 0);
        assert(released_count == 1 && scan_count == 1 && submit_count == 1);
        peripheral_batt_lvl_change_callback(NULL);
        assert(peripheral_battery_levels[idx] == 0);
        assert(peripheral_battery_levels[!idx] == (idx ? 91 : 72));
        assert(raised_count == 1 && raised[0].source == idx);
    } else if (!strcmp(argv[1], "host")) {
        split_central_disconnected(&host, 0x13);
        fprintf(stderr, "host detach: queued=%d source=%d (expected no split event)\n",
                put_index, put_index ? queued[0].source : -1);
        peripheral_batt_lvl_change_callback(NULL);
        assert(put_index == 0 && submit_count == 0 && raised_count == 0);
        assert(released_count == 0 && scan_count == 0);
        assert(peripheral_battery_levels[0] == 91 && peripheral_battery_levels[1] == 72);
    } else if (!strcmp(argv[1], "invalid_source")) {
        struct zmk_peripheral_battery_state_changed invalid = {.source = 234, .state_of_charge = 0};
        struct zmk_peripheral_battery_state_changed valid = {.source = 1, .state_of_charge = 65};
        k_msgq_put(&peripheral_batt_lvl_msgq, &invalid, K_NO_WAIT);
        k_msgq_put(&peripheral_batt_lvl_msgq, &valid, K_NO_WAIT);
        peripheral_batt_lvl_change_callback(NULL);
        assert(peripheral_battery_levels[0] == 91 && peripheral_battery_levels[1] == 65);
        assert(raised_count == 1 && raised[0].source == 1 && raised[0].state_of_charge == 65);
    } else {
        assert(!"Unknown case");
    }
    printf("PASS %s\n", argv[1]);
    return 0;
}
'''
records = []
env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='print_stacktrace=1')
for name, text in (('before', original), ('after', patched)):
    chunks = ['/* Copyright (c) 2020 The ZMK Contributors; SPDX-License-Identifier: MIT */\n', '#include <stdint.h>\n', event + ';\n', stubs]
    for marker in ('int peripheral_slot_index_for_conn(', 'void peripheral_batt_lvl_change_callback(',
                   'static void split_central_disconnected('):
        body, line = extract(text, marker)
        chunks += [f'\n#line {line} "central.{name}.c"\n', body + '\n']
    chunks += ['\n#line 1 "host_test.c"\n', tests]
    code = output_dir / f'{name}.c'
    code.write_text(''.join(chunks))
    binary = output_dir / name
    subprocess.run(['cc', '-std=c11', '-g', '-O1', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-fno-omit-frame-pointer',
                    str(code), '-o', str(binary)], check=True)
    for case in ('left', 'right', 'host', 'invalid_source'):
        result = subprocess.run([str(binary), case], capture_output=True, text=True, env=env)
        expected_failure = name == 'before' and case in ('host', 'invalid_source')
        ok = result.returncode != 0 if expected_failure else result.returncode == 0
        if expected_failure:
            ok = ok and 'index 234 out of bounds' in result.stderr
        record = dict(revision=name, case=case, returncode=result.returncode,
                      expected_failure=expected_failure, expectation_met=ok,
                      stdout=result.stdout, stderr=result.stderr)
        records.append(record)
        print(json.dumps(record, ensure_ascii=False))
(output_dir / 'results.json').write_text(json.dumps(records, indent=2, ensure_ascii=False) + '\n')
if len(records) != 8 or not all(r['expectation_met'] for r in records):
    raise SystemExit('Unexpected regression test result; see results.json')
print('All 8 expected outcomes verified. No hardware or firmware build tested.')
