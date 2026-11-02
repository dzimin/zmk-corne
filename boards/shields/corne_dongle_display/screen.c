/* Copyright (c) 2026 Corne Silver contributors
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 * Local status screen inspired by englmaxi/zmk-dongle-display v0.3.
 * See THIRD_PARTY.md for the original source and license.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <lvgl.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/hid_indicators.h>
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/keymap.h>
#include <zmk/physical_layouts.h>
#include <dt-bindings/zmk/matrix_transform.h>
#include <zmk/split/bluetooth/central.h>
#include <zmk/usb.h>
#include <zmk/workqueue.h>
#include "screen_model.h"

static const struct device *const panel = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static struct corne_screen model;
static uint8_t modifier_counts[8];
static bool dirty = true;
static atomic_t ready;
static bool blanked;
K_MUTEX_DEFINE(model_lock);

/* Derive handedness from the existing transform, never from BLE pairing order.
 * Position events use the selected layout, as in zmk_keymap_get_layer_binding_at_idx. */
#define STOCK_TRANSFORM DT_PHANDLE(DT_CHOSEN(zmk_physical_layout), transform)
#define POSITION_IS_RIGHT(node, prop, idx) \
    (KT_COL(DT_PROP_BY_IDX(node, prop, idx)) >= DT_PROP(STOCK_TRANSFORM, columns) / 2),
static const bool stock_position_is_right[] = {
    DT_FOREACH_PROP_ELEM(STOCK_TRANSFORM, map, POSITION_IS_RIGHT)
};
BUILD_ASSERT(CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS == 2,
             "Corne screen expects exactly two keyboard halves");

static void identify_battery_side(const struct zmk_position_state_changed *position) {
    if (!position->state || position->source >= 2) return;
    const uint32_t *map;
    int count = zmk_physical_layouts_get_selected_to_stock_position_map(&map);
    if (count <= 0 || position->position >= (uint32_t)count) return;
    uint32_t stock_position = map[position->position];
    if (stock_position < ARRAY_SIZE(stock_position_is_right))
        corne_screen_peripheral_side(&model, position->source,
                                     stock_position_is_right[stock_position]);
}

/* Indexed 1-bit LVGL image: two BGRA palette entries, then row-major pixels.
 * Static snapshots keep the dedicated display thread's stack small. */
static uint8_t image_data[8 + CORNE_FRAME_BYTES] = {0, 0, 0, 255, 255, 255, 255, 255};
static struct corne_screen snapshot;
static lv_obj_t *image;
static const lv_img_dsc_t image_descriptor = {
    .header = {.cf = LV_IMG_CF_INDEXED_1BIT, .w = 128, .h = 64},
    .data_size = sizeof(image_data),
    .data = image_data,
};

static void render_work_cb(struct k_work *work) {
    k_mutex_lock(&model_lock, K_FOREVER);
    if (!atomic_get(&ready)) {
        k_mutex_unlock(&model_lock);
        return;
    }
    bool visible = corne_screen_visible(&model, k_uptime_get(), CONFIG_ZMK_IDLE_TIMEOUT);
    bool repaint = dirty;
    if (visible && (dirty || blanked)) {
        snapshot = model;
        dirty = false;
        repaint = true;
    }
    k_mutex_unlock(&model_lock);
    if (!visible) {
        if (!blanked && display_blanking_on(panel) == 0) blanked = true;
        return;
    }
    if (repaint) {
        corne_screen_render(&snapshot, image_data + 8);
        lv_obj_invalidate(image);
    }
    if (blanked && display_blanking_off(panel) == 0) blanked = false;
}
K_WORK_DEFINE(render_work, render_work_cb);

static void request_render(void) {
    if (atomic_get(&ready)) k_work_submit_to_queue(zmk_display_work_q(), &render_work);
}

/* Called from a ZMK work queue, never from the display thread. */
static void refresh_status_locked(void) {
    uint8_t layer = zmk_keymap_highest_layer_active();
    const char *name = zmk_keymap_layer_name(layer);
    if (name) snprintf(model.layer, sizeof(model.layer), "%s", name);
    else snprintf(model.layer, sizeof(model.layer), "%u", layer);
    model.usb_output = zmk_endpoints_selected().transport == ZMK_TRANSPORT_USB;
    model.profile = zmk_ble_active_profile_index();
    model.ble_connected = zmk_ble_active_profile_is_connected();
    model.caps_lock = (zmk_hid_indicators_get_current_profile() & 2) != 0;
    for (int source = 0; source < 2; source++) {
        uint8_t ignored;
        bool connected = zmk_split_get_peripheral_battery_level(source, &ignored) == 0;
        corne_screen_peripheral_connection(&model, source, connected);
    }
}

#if IS_ENABLED(CONFIG_CORNE_DONGLE_BATTERY_CONFIRMED)
BUILD_ASSERT(DT_HAS_CHOSEN(zmk_battery), "Confirmed battery needs a chosen sensor");
static const struct device *const gauge = DEVICE_DT_GET(DT_CHOSEN(zmk_battery));

/* Independent of ZMK's activity-stopped battery timer. The same simple lithium
 * voltage approximation as ZMK v0.2 is used; this is an estimate, not a fuel gauge. */
static void sample_receiver_battery(void) {
    struct sensor_value voltage;
    bool known = device_is_ready(gauge) &&
                 sensor_sample_fetch_chan(gauge, SENSOR_CHAN_VOLTAGE) == 0 &&
                 sensor_channel_get(gauge, SENSOR_CHAN_VOLTAGE, &voltage) == 0;
    int percent = 0;
    if (known) {
        int mv = voltage.val1 * 1000 + voltage.val2 / 1000;
        percent = mv >= 4200 ? 100 : mv <= 3450 ? 0 : mv * 2 / 15 - 459;
    }
    k_mutex_lock(&model_lock, K_FOREVER);
    model.batteries[1].known = known;
    model.batteries[1].level = percent;
    dirty = true;
    k_mutex_unlock(&model_lock);
}
#endif

static void status_work_cb(struct k_work *work) {
    static int64_t last_battery_poll = -60000;
    int64_t now = k_uptime_get();
    k_mutex_lock(&model_lock, K_FOREVER);
    refresh_status_locked();
    bool visible = corne_screen_visible(&model, now, CONFIG_ZMK_IDLE_TIMEOUT);
    dirty = true;
    k_mutex_unlock(&model_lock);
#if IS_ENABLED(CONFIG_CORNE_DONGLE_BATTERY_CONFIRMED)
    if (visible && now - last_battery_poll >= 60000) {
        sample_receiver_battery();
        last_battery_poll = now;
    }
#else
    ARG_UNUSED(visible);
    ARG_UNUSED(last_battery_poll);
#endif
    request_render();
}
K_WORK_DEFINE(status_work, status_work_cb);

static void status_timer_cb(struct k_timer *timer) {
    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &status_work);
}
K_TIMER_DEFINE(status_timer, status_timer_cb, NULL);

/* A separate short timer gives an exact idle deadline without depending on
 * activity events that have already become idle during a long USB session. */
static void blank_timer_cb(struct k_timer *timer) { request_render(); }
K_TIMER_DEFINE(blank_timer, blank_timer_cb, NULL);

static void reset_blank_timer(void) {
    if (model.usb_powered) k_timer_stop(&blank_timer);
    else k_timer_start(&blank_timer, K_MSEC(CONFIG_ZMK_IDLE_TIMEOUT), K_NO_WAIT);
}

static int screen_listener(const zmk_event_t *eh) {
    k_mutex_lock(&model_lock, K_FOREVER);
    const struct zmk_position_state_changed *position = as_zmk_position_state_changed(eh);
    const struct zmk_keycode_state_changed *key = as_zmk_keycode_state_changed(eh);
    const struct zmk_usb_conn_state_changed *usb = as_zmk_usb_conn_state_changed(eh);
    const struct zmk_peripheral_battery_state_changed *battery =
        as_zmk_peripheral_battery_state_changed(eh);
    if (position) {
        model.last_activity = k_uptime_get();
        reset_blank_timer();
    }
    if (usb) {
        corne_screen_power(&model, k_uptime_get(), zmk_usb_is_powered());
        reset_blank_timer();
    }
    if (key) {
        uint8_t explicit = key->explicit_modifiers;
        if (key->usage_page == 7 && key->keycode >= 0xe0 && key->keycode <= 0xe7)
            explicit |= 1 << (key->keycode - 0xe0);
        for (int bit = 0; bit < 8; bit++) {
            if (!(explicit & (1 << bit))) continue;
            if (key->state && modifier_counts[bit] < UINT8_MAX) modifier_counts[bit]++;
            else if (!key->state && modifier_counts[bit]) modifier_counts[bit]--;
        }
        model.modifiers = 0;
        for (int bit = 0; bit < 8; bit++)
            if (modifier_counts[bit]) model.modifiers |= 1 << bit;
        if (key->state) {
            model.modifiers |= key->implicit_modifiers;
            corne_screen_key(&model, key->usage_page, key->keycode, model.modifiers);
        }
    }
    refresh_status_locked();
    if (position) identify_battery_side(position);
    if (battery)
        corne_screen_peripheral_level(&model, battery->source, battery->state_of_charge);
    dirty = true;
    k_mutex_unlock(&model_lock);
    request_render();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(corne_dongle_screen, screen_listener);
ZMK_SUBSCRIPTION(corne_dongle_screen, zmk_position_state_changed);
ZMK_SUBSCRIPTION(corne_dongle_screen, zmk_keycode_state_changed);
ZMK_SUBSCRIPTION(corne_dongle_screen, zmk_usb_conn_state_changed);
ZMK_SUBSCRIPTION(corne_dongle_screen, zmk_peripheral_battery_state_changed);
ZMK_SUBSCRIPTION(corne_dongle_screen, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(corne_dongle_screen, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(corne_dongle_screen, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(corne_dongle_screen, zmk_hid_indicators_changed);

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, 128, 64);
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    image = lv_img_create(screen);
    lv_img_set_src(image, &image_descriptor);
    lv_obj_set_pos(image, 0, 0);
    k_mutex_lock(&model_lock, K_FOREVER);
    model.batteries[1].connected = true;
    model.last_activity = k_uptime_get();
    model.usb_powered = zmk_usb_is_powered();
    refresh_status_locked();
    snapshot = model;
    atomic_set(&ready, 1);
    reset_blank_timer();
    k_mutex_unlock(&model_lock);
    corne_screen_render(&snapshot, image_data + 8);
    k_timer_start(&status_timer, K_SECONDS(1), K_SECONDS(1));
    return screen;
}
