#include "bt_host.h"

#include <btstack.h>
#include <pico/cyw43_arch.h>
#include <pico/time.h>
#include <string.h>
#include <uni.h>

#include "gamepad_state.h"
#include "log.h"
#include "protocol.h"
#include "translate.h"
#include "usb_webusb.h"
#include "usb_xinput.h"
#include "wake.h"

static void on_init(int argc, const char** argv) {
    (void)argc;
    (void)argv;
}

// Forward declarations — hci_event_handler / status_timer_handler /
// on_init_complete sit further down, after the state variables and
// emit_controller_slot they depend on.
static void on_init_complete(void);

void bt_host_handle_text_command(const char* cmd) {
    if (cmd[0] == 0) return;
    log_str("cmd: ");
    log_line(cmd);
    if (strcmp(cmd, "forget") == 0) {
        uni_bt_del_keys_unsafe();
        log_line("bt: keys cleared");
    } else if (strcmp(cmd, "help") == 0) {
        log_line("commands: forget, help");
    } else {
        log_str("unknown: ");
        log_line(cmd);
    }
}

static uni_error_t on_device_discovered(bd_addr_t addr, const char* name, uint16_t cod, uint8_t rssi) {
    (void)addr; (void)name; (void)cod; (void)rssi;
    return UNI_ERROR_SUCCESS;
}

// Per-slot mouse-mode state. Pressing the Switch Capture button toggles
// whether a controller drives the cursor (right stick → mouse deltas,
// A/B → mouse clicks) or the XInput slot.
static bool mouse_mode[BRIDGE_MAX_PLAYERS];
static uint32_t prev_buttons[BRIDGE_MAX_PLAYERS];

// Scanning is on by default after init; SET_SCANNING from the WebUSB host
// can toggle it.
static bool scanning_enabled = true;

// Last RSSI per slot, written from the async GAP_EVENT_RSSI_MEASUREMENT handler.
static int8_t slot_rssi[BRIDGE_MAX_PLAYERS];

// ms_since_boot of the most recent on_controller_data callback per slot.
// Used by the status timer to force a disconnect on silent slots (BT
// supervision timeout is sometimes 15+ seconds; this trims it).
static uint32_t slot_last_data_ms[BRIDGE_MAX_PLAYERS];

#define STALE_DATA_MS 5000

// 3s status timer + HCI event listener wired up in on_init_complete.
static btstack_timer_source_t status_timer;
static btstack_packet_callback_registration_t hci_event_cb;

// --- WebUSB event emitters ------------------------------------------------

static void emit_hello(void) {
    uint8_t buf[4] = {PROTOCOL_VERSION, BRIDGE_MAX_PLAYERS, 0, 1};
    usb_webusb_send(MSG_HELLO, buf, sizeof(buf));
}

static void emit_scanning(void) {
    uint8_t buf[1] = {scanning_enabled ? 1u : 0u};
    usb_webusb_send(MSG_SCANNING, buf, sizeof(buf));
}

static void emit_controller_slot(int slot) {
    uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(slot);
    // Layout: slot, present, batt, rssi, flags, mac[6], name_len, name[]
    uint8_t buf[5 + PROTOCOL_MAC_LEN + 1 + PROTOCOL_NAME_MAX];
    memset(buf, 0, sizeof(buf));
    buf[0] = (uint8_t)slot;
    if (d == NULL || !uni_bt_conn_is_connected(&d->conn)) {
        buf[1] = 0;     // present = false
        buf[2] = 0;     // battery unavailable (bluepad32 convention)
        buf[3] = 0;     // rssi unknown
        buf[4] = 0;     // flags = none
        buf[11] = 0;    // name_len
        usb_webusb_send(MSG_CONTROLLER, buf, 12);
        return;
    }
    buf[1] = 1;
    buf[2] = d->controller.battery;
    buf[3] = (uint8_t)slot_rssi[slot];
    buf[4] = mouse_mode[slot] ? CONTROLLER_FLAG_MOUSE : 0;
    memcpy(&buf[5], d->conn.btaddr, PROTOCOL_MAC_LEN);
    size_t name_len = strnlen(d->name, PROTOCOL_NAME_MAX);
    buf[11] = (uint8_t)name_len;
    memcpy(&buf[12], d->name, name_len);
    usb_webusb_send(MSG_CONTROLLER, buf, (uint16_t)(12 + name_len));
}

static void emit_bond_list(void) {
    // Btstack's link-key iterator would let us enumerate; bluepad32 doesn't
    // re-export it cleanly yet. For now we send an empty list as a
    // placeholder until proper enumeration lands.
    uint8_t buf[1] = {0};
    usb_webusb_send(MSG_BOND_LIST, buf, sizeof(buf));
}

static void emit_full_status(void) {
    emit_hello();
    for (int slot = 0; slot < BRIDGE_MAX_PLAYERS; slot++) {
        emit_controller_slot(slot);
    }
    emit_scanning();
    emit_bond_list();
}

static void hci_event_handler(uint8_t packet_type, uint16_t channel,
                              uint8_t* packet, uint16_t size) {
    (void)channel; (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != GAP_EVENT_RSSI_MEASUREMENT) return;

    hci_con_handle_t handle = gap_event_rssi_measurement_get_con_handle(packet);
    int8_t rssi = (int8_t)gap_event_rssi_measurement_get_rssi(packet);
    for (int i = 0; i < BRIDGE_MAX_PLAYERS; i++) {
        uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(i);
        if (d != NULL && d->conn.handle == handle) {
            slot_rssi[i] = rssi;
            break;
        }
    }
}

static void status_timer_handler(btstack_timer_source_t* ts) {
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    for (int i = 0; i < BRIDGE_MAX_PLAYERS; i++) {
        uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(i);
        if (d == NULL || !uni_bt_conn_is_connected(&d->conn)) continue;

        // If we haven't heard from this controller in 5 s, force-drop it.
        // The BT supervision timeout often takes 15+ s to notice an off
        // controller; this gives the host a much snappier signal.
        if (now - slot_last_data_ms[i] > STALE_DATA_MS) {
            log_str("bt: forcing disconnect on silent slot ");
            log_u((uint32_t)i);
            log_line("");
            uni_hid_device_disconnect(d);
            continue;
        }

        gap_read_rssi(d->conn.handle);
        emit_controller_slot(i);
    }
    btstack_run_loop_set_timer(ts, 3000);
    btstack_run_loop_add_timer(ts);
}

static void on_init_complete(void) {
    log_line("bt: init complete, scanning");
    uni_bt_start_scanning_and_autoconnect_unsafe();
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);

    hci_event_cb.callback = &hci_event_handler;
    hci_add_event_handler(&hci_event_cb);

    status_timer.process = &status_timer_handler;
    btstack_run_loop_set_timer(&status_timer, 3000);
    btstack_run_loop_add_timer(&status_timer);
}

void bt_host_handle_webusb_command(uint8_t type, const uint8_t* payload, uint16_t len) {
    switch (type) {
        case MSG_GET_STATUS:
            log_line("webusb: get_status");
            emit_full_status();
            break;
        case MSG_SET_SCANNING:
            if (len < 1) break;
            scanning_enabled = (payload[0] != 0);
            log_str("webusb: set_scanning ");
            log_line(scanning_enabled ? "on" : "off");
            if (scanning_enabled) {
                uni_bt_start_scanning_and_autoconnect_safe();
            } else {
                uni_bt_stop_scanning_safe();
            }
            emit_scanning();
            break;
        case MSG_FORGET_ALL:
            log_line("webusb: forget_all");
            uni_bt_del_keys_unsafe();
            emit_bond_list();
            break;
        case MSG_FORGET_MAC:
        case MSG_SET_SLOT:
            // TODO: per-MAC forget + slot preference table in flash.
            break;
        default:
            log_str("webusb: unknown cmd ");
            log_u(type);
            log_line("");
            break;
    }
}

static int8_t stick_to_mouse_delta(int16_t v) {
    // Offset deadzone — output 0 below threshold; above it, scale from 0 so
    // the cursor doesn't twitch at idle. Switch sticks drift up to ±60 in
    // practice; threshold sits above typical drift.
    const int DEAD = 80;
    if (v > -DEAD && v < DEAD) return 0;
    int32_t s = (v > 0 ? v - DEAD : v + DEAD) / 32;
    if (s > 30)  s = 30;
    if (s < -30) s = -30;
    return (int8_t)s;
}

static void on_device_connected(uni_hid_device_t* d) {
    log_str("bt: connected idx=");
    log_u((uint32_t)uni_hid_device_get_idx_for_instance(d));
    log_line("");
}

static void on_device_disconnected(uni_hid_device_t* d) {
    const int idx = uni_hid_device_get_idx_for_instance(d);
    log_str("bt: disconnected idx=");
    log_u((uint32_t)idx);
    log_line("");
    if (idx < 0 || idx >= BRIDGE_MAX_PLAYERS) return;
    mouse_mode[idx] = false;
    prev_buttons[idx] = 0;
    slot_rssi[idx] = 0;
    usb_xinput_set_controller_present((uint8_t)idx, false);
    emit_controller_slot(idx);
}

static uni_error_t on_device_ready(uni_hid_device_t* d) {
    const int idx = uni_hid_device_get_idx_for_instance(d);
    log_str("bt: ready idx=");
    log_u((uint32_t)idx);
    log_line("");

    if (idx < 0 || idx >= BRIDGE_MAX_PLAYERS) return UNI_ERROR_SUCCESS;

    slot_last_data_ms[idx] = to_ms_since_boot(get_absolute_time());

    // Light a single player LED matching the controller's slot.
    if (d->report_parser.set_player_leds != NULL) {
        d->report_parser.set_player_leds(d, (uint8_t)(1u << idx));
    }
    usb_xinput_set_controller_present((uint8_t)idx, true);
    emit_controller_slot(idx);
    return UNI_ERROR_SUCCESS;
}

static void on_controller_data(uni_hid_device_t* d, uni_controller_t* ctl) {
    if (ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD) return;
    const int idx = uni_hid_device_get_idx_for_instance(d);
    if (idx < 0 || idx >= BRIDGE_MAX_PLAYERS) return;

    slot_last_data_ms[idx] = to_ms_since_boot(get_absolute_time());

    bridge_gp_state_t state;
    translate_gamepad(&ctl->gamepad, &state);

    // Capture button rising-edge toggles mouse mode for this slot.
    const uint32_t pressed = state.buttons & ~prev_buttons[idx];
    prev_buttons[idx] = state.buttons;
    if (pressed & BRIDGE_BTN_CAPTURE) {
        mouse_mode[idx] = !mouse_mode[idx];
        log_str("xin: slot ");
        log_u((uint32_t)idx);
        log_line(mouse_mode[idx] ? " mouse mode ON" : " mouse mode OFF");
        // Visual indicator: HOME-button ring LED. Player LEDs stay at the
        // single-slot indicator (set once in on_device_ready) so the user
        // always knows which player slot this controller is in.
        if (d->report_parser.set_home_led != NULL) {
            d->report_parser.set_home_led(d, mouse_mode[idx] ? 1 : 0);
        }
        // Push the new mode out to any connected WebUSB client immediately.
        emit_controller_slot(idx);
    }

    if (mouse_mode[idx]) {
        // Either stick drives the cursor — sums of left + right so one-handed
        // use works either way. A = left click, B = right click. Suppress
        // XInput so games don't see ghost input from the same stick.
        int dx = (int)stick_to_mouse_delta(state.lx) + (int)stick_to_mouse_delta(state.rx);
        int dy = (int)stick_to_mouse_delta(state.ly) + (int)stick_to_mouse_delta(state.ry);
        if (dx > 127)  dx = 127;
        if (dx < -127) dx = -127;
        if (dy > 127)  dy = 127;
        if (dy < -127) dy = -127;
        bool lmb = (state.buttons & BRIDGE_BTN_A) != 0;
        bool rmb = (state.buttons & BRIDGE_BTN_B) != 0;
        mouse_send((int8_t)dx, (int8_t)dy, lmb, rmb);

        bridge_gp_state_t neutral = {0};
        usb_xinput_set_gamepad((uint8_t)idx, &neutral);
    } else {
        usb_xinput_set_gamepad((uint8_t)idx, &state);
    }

    // Pump XInput rumble commands back to the physical controller.
    uint8_t strong, weak;
    if (usb_xinput_poll_rumble((uint8_t)idx, &strong, &weak)) {
        if (d->report_parser.play_dual_rumble != NULL) {
            d->report_parser.play_dual_rumble(d, 0, 250, weak, strong);
        }
    }
}

static const uni_property_t* get_property(uni_property_idx_t idx) {
    (void)idx;
    return NULL;
}

static void on_oob_event(uni_platform_oob_event_t event, void* data) {
    (void)event;
    (void)data;
}

struct uni_platform* bt_host_get_platform(void) {
    static struct uni_platform plat = {
        .name = "bridge",
        .init = on_init,
        .on_init_complete = on_init_complete,
        .on_device_discovered = on_device_discovered,
        .on_device_connected = on_device_connected,
        .on_device_disconnected = on_device_disconnected,
        .on_device_ready = on_device_ready,
        .on_oob_event = on_oob_event,
        .on_controller_data = on_controller_data,
        .get_property = get_property,
    };
    return &plat;
}
