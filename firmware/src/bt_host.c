#include "bt_host.h"

#include <btstack.h>
#include <hardware/structs/ioqspi.h>
#include <hardware/structs/sio.h>
#include <hardware/sync.h>
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

// Scanning is OFF at boot — active BT inquiry + page scan consumes radio
// bandwidth that interferes with established controller links (especially
// during a new pairing's subcommand storm). User flips it on through the
// WebUI when they want to pair or wake a bonded controller.
static bool scanning_enabled = false;

// Last RSSI per slot, written from the async GAP_EVENT_RSSI_MEASUREMENT handler.
static int8_t slot_rssi[BRIDGE_MAX_PLAYERS];

// ms_since_boot of the most recent on_controller_data callback per slot.
// Used by the status timer to force a disconnect on silent slots (BT
// supervision timeout is sometimes 15+ seconds; this trims it).
static uint32_t slot_last_data_ms[BRIDGE_MAX_PLAYERS];

// Bumped from 5 s after observing controller drops during a 3-way pair —
// btstack stalls on other inputs while finishing a new handshake, and a
// short timeout was misreading the stall as "controller off".
#define STALE_DATA_MS 15000

// Identify (LED-blink) state per slot. Counter decrements every 250 ms; LED
// toggles on each step, restoring the slot's mouse-mode LED on the final step.
static uint8_t identify_remaining[BRIDGE_MAX_PLAYERS];
static btstack_timer_source_t identify_timer;
static bool identify_timer_active;

// MAC-keyed input mirroring. Each entry copies the source MAC's input into the
// slot owned by the target MAC, OR-merging with the target's own state. The
// source slot vanishes from the host while the rule is active.
typedef struct {
    bool active;
    uint8_t source_mac[6];
    uint8_t target_mac[6];
} forward_entry_t;

static forward_entry_t forwards[BRIDGE_MAX_PLAYERS];

// Latest per-slot input state, cached so that the target's data callback can
// merge in stale source state and vice versa. Without caching, the two streams
// race and we'd see flicker on every other frame.
static bridge_gp_state_t slot_state[BRIDGE_MAX_PLAYERS];

// MAC → preferred player position (0..3). Set by the host via MSG_SET_SLOT
// when the user reorders the UI; used on connect to drive both the player
// LED and the bp_slot → xinput_slot routing.
typedef struct {
    bool active;
    uint8_t mac[6];
    uint8_t position;
} slot_pref_t;

static slot_pref_t slot_prefs[BRIDGE_MAX_PLAYERS];

// Permutation: which XInput instance does each bluepad32 slot drive? Defaults
// to identity, mutated by slot_pref_set. UI position == xinput instance.
static uint8_t bp_to_xi[BRIDGE_MAX_PLAYERS] = {0, 1, 2, 3};

// Last LED mask we actually issued to each slot, so refresh_mac() doesn't
// re-send the same subcommand on every protocol message. Subcommands share
// btstack's tiny ACL buffer budget — duplicates were the third-controller
// pairing pressure that kept dropping the first one.
static uint8_t last_led_mask[BRIDGE_MAX_PLAYERS] = {0xFF, 0xFF, 0xFF, 0xFF};

static int bp_for_xi(uint8_t xi) {
    for (int i = 0; i < BRIDGE_MAX_PLAYERS; i++) {
        if (bp_to_xi[i] == xi) return i;
    }
    return -1;
}

static int slot_for_mac(const uint8_t mac[6]) {
    for (int i = 0; i < BRIDGE_MAX_PLAYERS; i++) {
        uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(i);
        if (d == NULL || !uni_bt_conn_is_connected(&d->conn)) continue;
        if (memcmp(d->conn.btaddr, mac, 6) == 0) return i;
    }
    return -1;
}

static bool mac_is_active_mirror_source(const uint8_t mac[6]) {
    for (int i = 0; i < BRIDGE_MAX_PLAYERS; i++) {
        if (!forwards[i].active) continue;
        if (memcmp(forwards[i].source_mac, mac, 6) == 0) return true;
    }
    return false;
}

static int find_mirror_target_for_source(const uint8_t mac[6]) {
    for (int i = 0; i < BRIDGE_MAX_PLAYERS; i++) {
        if (!forwards[i].active) continue;
        if (memcmp(forwards[i].source_mac, mac, 6) != 0) continue;
        return slot_for_mac(forwards[i].target_mac);
    }
    return -1;
}

static uint8_t preferred_position_for_mac(const uint8_t mac[6], uint8_t fallback) {
    for (int i = 0; i < BRIDGE_MAX_PLAYERS; i++) {
        if (!slot_prefs[i].active) continue;
        if (memcmp(slot_prefs[i].mac, mac, 6) == 0) return slot_prefs[i].position;
    }
    return fallback;
}

static void merge_state(bridge_gp_state_t* dst, const bridge_gp_state_t* src) {
    dst->buttons |= src->buttons;
    dst->dpad |= src->dpad;
    if (src->lt > dst->lt) dst->lt = src->lt;
    if (src->rt > dst->rt) dst->rt = src->rt;
    int32_t a, b;
    a = src->lx < 0 ? -src->lx : src->lx; b = dst->lx < 0 ? -dst->lx : dst->lx;
    if (a > b) dst->lx = src->lx;
    a = src->ly < 0 ? -src->ly : src->ly; b = dst->ly < 0 ? -dst->ly : dst->ly;
    if (a > b) dst->ly = src->ly;
    a = src->rx < 0 ? -src->rx : src->rx; b = dst->rx < 0 ? -dst->rx : dst->rx;
    if (a > b) dst->rx = src->rx;
    a = src->ry < 0 ? -src->ry : src->ry; b = dst->ry < 0 ? -dst->ry : dst->ry;
    if (a > b) dst->ry = src->ry;
}

// Re-evaluate XInput presence + LED for the controller with this MAC. Updating
// one MAC per protocol command avoids the LED-write storm that was stalling
// bluepad32 during rapid reorders.
static void refresh_mac(const uint8_t mac[6]) {
    int bp = slot_for_mac(mac);
    if (bp < 0) return;
    uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(bp);
    if (d == NULL) return;
    uint8_t xi = bp_to_xi[bp];
    bool is_source = mac_is_active_mirror_source(mac);
    usb_xinput_set_controller_present(xi, !is_source);
    if (d->report_parser.set_player_leds != NULL) {
        uint8_t want = (uint8_t)(1u << (xi & 0x03));
        if (last_led_mask[bp] != want) {
            d->report_parser.set_player_leds(d, want);
            last_led_mask[bp] = want;
        }
    }
}

// 3s status timer + HCI event listener wired up in on_init_complete.
static btstack_timer_source_t status_timer;
static btstack_packet_callback_registration_t hci_event_cb;

// Onboard LED + BOOTSEL button. A single 100 ms tick timer polls the button
// and drives the LED. States:
//   - solid on:        running, idle
//   - blink (10 s):    pairing window open (started by a BOOTSEL press)
//   - 3-blink burst:   a controller just finished pairing
static btstack_timer_source_t led_timer;
static uint8_t led_blinks_left;        // connect-burst toggles remaining
static uint32_t pairing_until_ms;      // 0 = not pairing; else blink + scan until this time
static bool bootsel_prev;
static uint8_t led_tick;               // free-running, drives blink cadence

// Reads the BOOTSEL button by briefly tri-stating the QSPI flash CS line and
// sampling it. Must run from RAM with interrupts off (flash is inaccessible
// during the read). Returns true while the button is held.
static bool __no_inline_not_in_flash_func(read_bootsel)(void) {
    const uint CS_PIN_INDEX = 1;
    uint32_t flags = save_and_disable_interrupts();
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    for (volatile int i = 0; i < 1000; ++i)
        ;
    // Button pulls the pin LOW when pressed.
    bool pressed = !(sio_hw->gpio_hi_in & (1u << CS_PIN_INDEX));
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    restore_interrupts(flags);
    return pressed;
}

// --- WebUSB event emitters ------------------------------------------------

static void emit_hello(void) {
    uint8_t buf[4] = {PROTOCOL_VERSION, BRIDGE_MAX_PLAYERS, 0, 1};
    usb_webusb_send(MSG_HELLO, buf, sizeof(buf));
}

static void emit_scanning(void) {
    uint8_t buf[1] = {scanning_enabled ? 1u : 0u};
    usb_webusb_send(MSG_SCANNING, buf, sizeof(buf));
}

// Emit MSG_CONTROLLER for the XInput position `xi`. Looks up whichever
// bluepad32 slot is currently routed there; if none, sends an "empty" frame.
static void emit_xi_slot(uint8_t xi) {
    int bp = bp_for_xi(xi);
    uni_hid_device_t* d = (bp >= 0) ? uni_hid_device_get_instance_for_idx(bp) : NULL;
    uint8_t buf[5 + PROTOCOL_MAC_LEN + 1 + PROTOCOL_NAME_MAX];
    memset(buf, 0, sizeof(buf));
    buf[0] = xi;
    if (d == NULL || !uni_bt_conn_is_connected(&d->conn)) {
        buf[1] = 0;
        buf[2] = 0;
        buf[3] = 0;
        buf[4] = 0;
        buf[11] = 0;
        usb_webusb_send(MSG_CONTROLLER, buf, 12);
        return;
    }
    buf[1] = 1;
    buf[2] = d->controller.battery;
    buf[3] = (uint8_t)slot_rssi[bp];
    buf[4] = mouse_mode[bp] ? CONTROLLER_FLAG_MOUSE : 0;
    memcpy(&buf[5], d->conn.btaddr, PROTOCOL_MAC_LEN);
    size_t name_len = strnlen(d->name, PROTOCOL_NAME_MAX);
    buf[11] = (uint8_t)name_len;
    memcpy(&buf[12], d->name, name_len);
    usb_webusb_send(MSG_CONTROLLER, buf, (uint16_t)(12 + name_len));
}

// Convenience: emit by bp_slot, looking up the XInput position for us.
static void emit_controller_slot(int bp) {
    if (bp < 0 || bp >= BRIDGE_MAX_PLAYERS) return;
    emit_xi_slot(bp_to_xi[bp]);
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
    // Iterate XInput positions so the UI sees them in player order.
    for (uint8_t xi = 0; xi < BRIDGE_MAX_PLAYERS; xi++) {
        emit_xi_slot(xi);
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

static void set_scanning(bool enabled) {
    if (scanning_enabled == enabled) return;
    scanning_enabled = enabled;
    if (enabled)
        uni_bt_start_scanning_and_autoconnect_safe();
    else
        uni_bt_stop_scanning_safe();
    emit_scanning();
}

// Connect-burst: blink 3× then settle. Independent of the pairing window.
static void led_blink_connect(void) {
    led_blinks_left = 6;
}

// Single 100 ms tick: poll BOOTSEL, run the pairing window, drive the LED.
static void led_timer_handler(btstack_timer_source_t* ts) {
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    led_tick++;

    // --- BOOTSEL edge detect ---
    bool pressed = read_bootsel();
    if (pressed && !bootsel_prev) {
        // Open a 10 s pairing window: enable scanning + blink.
        pairing_until_ms = now + 10000;
        set_scanning(true);
        log_line("bootsel: pairing window opened");
    }
    bootsel_prev = pressed;

    // --- pairing window expiry ---
    if (pairing_until_ms != 0 && now >= pairing_until_ms) {
        pairing_until_ms = 0;
        set_scanning(false);
        log_line("bootsel: pairing window closed");
    }

    // --- LED state ---
    bool led;
    if (pairing_until_ms != 0) {
        led = (led_tick & 1);                 // ~5 Hz blink while pairing
    } else if (led_blinks_left > 0) {
        led_blinks_left--;
        led = !(led_blinks_left & 1);         // 3-blink connect burst
    } else {
        led = true;                           // solid-on idle
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led ? 1 : 0);

    btstack_run_loop_set_timer(ts, 100);
    btstack_run_loop_add_timer(ts);
}

static void status_timer_handler(btstack_timer_source_t* ts) {
    // Stale-data eviction removed: it was false-positiving during 3-way
    // pairings when btstack briefly starved one controller's input pipeline
    // while bringing the new one up. BT's own supervision timeout handles
    // real disconnects within ~20 s.
    (void)slot_last_data_ms;

    for (int i = 0; i < BRIDGE_MAX_PLAYERS; i++) {
        uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(i);
        if (d == NULL || !uni_bt_conn_is_connected(&d->conn)) continue;
        emit_xi_slot(bp_to_xi[i]);
    }
    btstack_run_loop_set_timer(ts, 5000);
    btstack_run_loop_add_timer(ts);
}

static void on_init_complete(void) {
    log_line("bt: init complete (scanning off)");
    // Intentionally not starting scanning — the WebUI toggles it on demand.
    // LED stays solid on once init is done — main turned it on at boot.
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

    hci_event_cb.callback = &hci_event_handler;
    hci_add_event_handler(&hci_event_cb);

    status_timer.process = &status_timer_handler;
    btstack_run_loop_set_timer(&status_timer, 5000);
    btstack_run_loop_add_timer(&status_timer);

    led_timer.process = &led_timer_handler;
    btstack_run_loop_set_timer(&led_timer, 100);
    btstack_run_loop_add_timer(&led_timer);
}

static void identify_timer_handler(btstack_timer_source_t* ts) {
    bool any_active = false;
    for (int i = 0; i < BRIDGE_MAX_PLAYERS; i++) {
        if (identify_remaining[i] == 0) continue;
        any_active = true;
        uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(i);
        if (d == NULL || d->report_parser.set_home_led == NULL) {
            identify_remaining[i] = 0;
            continue;
        }
        identify_remaining[i]--;
        if (identify_remaining[i] == 0) {
            // Restore the slot's normal LED state (mouse-mode-aware).
            d->report_parser.set_home_led(d, mouse_mode[i] ? 1 : 0);
        } else {
            // Toggle: odd remaining = LED on, even = LED off.
            d->report_parser.set_home_led(d, (identify_remaining[i] & 1) ? 1 : 0);
        }
    }
    if (any_active) {
        btstack_run_loop_set_timer(ts, 250);
        btstack_run_loop_add_timer(ts);
    } else {
        identify_timer_active = false;
    }
}

static void identify_kick(uint8_t slot) {
    if (slot >= BRIDGE_MAX_PLAYERS) return;
    identify_remaining[slot] = 6;  // 3 on/off cycles ≈ 1.5 s
    if (identify_timer_active) return;
    identify_timer.process = &identify_timer_handler;
    btstack_run_loop_set_timer(&identify_timer, 0);
    btstack_run_loop_add_timer(&identify_timer);
    identify_timer_active = true;
}

static void forward_set(const uint8_t* source, const uint8_t* target) {
    // Replace any existing entry with the same source; clear if target is zero.
    bool target_zero = true;
    for (int j = 0; j < 6; j++) if (target[j] != 0) { target_zero = false; break; }

    int free_slot = -1;
    for (int i = 0; i < BRIDGE_MAX_PLAYERS; i++) {
        if (forwards[i].active && memcmp(forwards[i].source_mac, source, 6) == 0) {
            if (target_zero) {
                forwards[i].active = false;
            } else {
                memcpy(forwards[i].target_mac, target, 6);
            }
            refresh_mac(source);
            return;
        }
        if (!forwards[i].active && free_slot < 0) free_slot = i;
    }
    if (target_zero || free_slot < 0) return;
    forwards[free_slot].active = true;
    memcpy(forwards[free_slot].source_mac, source, 6);
    memcpy(forwards[free_slot].target_mac, target, 6);
    refresh_mac(source);
}

// Stash the MAC's preferred XInput position so it survives reconnects.
static void slot_pref_store(const uint8_t* mac, uint8_t position) {
    int free_slot = -1;
    for (int i = 0; i < BRIDGE_MAX_PLAYERS; i++) {
        if (slot_prefs[i].active && memcmp(slot_prefs[i].mac, mac, 6) == 0) {
            slot_prefs[i].position = position;
            return;
        }
        if (!slot_prefs[i].active && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) return;
    slot_prefs[free_slot].active = true;
    memcpy(slot_prefs[free_slot].mac, mac, 6);
    slot_prefs[free_slot].position = position;
}

// Move MAC to XInput position `target_xi`. If another controller currently
// drives that XInput instance, swap them — keeps bp_to_xi a permutation.
static void slot_pref_set(const uint8_t* mac, uint8_t target_xi) {
    if (target_xi >= BRIDGE_MAX_PLAYERS) return;
    slot_pref_store(mac, target_xi);

    int my_bp = slot_for_mac(mac);
    if (my_bp < 0) return;
    uint8_t my_old_xi = bp_to_xi[my_bp];
    if (my_old_xi == target_xi) return;

    int other_bp = bp_for_xi(target_xi);
    bp_to_xi[my_bp] = target_xi;
    if (other_bp >= 0 && other_bp != my_bp) {
        bp_to_xi[other_bp] = my_old_xi;
        // Refresh the displaced controller's XInput presence + LED too.
        uni_hid_device_t* other = uni_hid_device_get_instance_for_idx(other_bp);
        if (other != NULL) refresh_mac(other->conn.btaddr);
    }
    refresh_mac(mac);
}

void bt_host_handle_webusb_command(uint8_t type, const uint8_t* payload, uint16_t len) {
    switch (type) {
        case MSG_GET_STATUS:
            log_line("webusb: get_status");
            emit_full_status();
            break;
        case MSG_SET_SCANNING:
            if (len < 1) break;
            log_str("webusb: set_scanning ");
            log_line(payload[0] ? "on" : "off");
            // Manual scan toggle also cancels any open BOOTSEL pairing window.
            pairing_until_ms = 0;
            set_scanning(payload[0] != 0);
            break;
        case MSG_FORGET_ALL:
            log_line("webusb: forget_all");
            uni_bt_del_keys_unsafe();
            emit_bond_list();
            break;
        case MSG_FORGET_MAC:
            // TODO: per-MAC forget; today FORGET_ALL covers it.
            break;
        case MSG_SET_SLOT:
            if (len < 7) break;
            slot_pref_set(&payload[0], payload[6]);
            log_line("webusb: slot pref set");
            break;
        case MSG_IDENTIFY:
            if (len < 1) break;
            log_str("webusb: identify slot ");
            log_u(payload[0]);
            log_line("");
            identify_kick(payload[0]);
            break;
        case MSG_SET_FORWARD:
            if (len < 12) break;
            forward_set(&payload[0], &payload[6]);
            log_line("webusb: set forward");
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
    last_led_mask[idx] = 0xFF;  // forget LED state — fresh controller may need its mask re-sent
    uint8_t xi = bp_to_xi[idx];
    usb_xinput_set_controller_present(xi, false);
    emit_xi_slot(xi);
}

static uni_error_t on_device_ready(uni_hid_device_t* d) {
    const int idx = uni_hid_device_get_idx_for_instance(d);
    log_str("bt: ready idx=");
    log_u((uint32_t)idx);
    log_line("");

    if (idx < 0 || idx >= BRIDGE_MAX_PLAYERS) return UNI_ERROR_SUCCESS;

    slot_last_data_ms[idx] = to_ms_since_boot(get_absolute_time());
    led_blink_connect();

    // If we have a stored preference for this MAC, route it to that XInput
    // position (swapping with whichever bp currently holds the spot).
    uint8_t pref = preferred_position_for_mac(d->conn.btaddr, (uint8_t)idx);
    if (pref != bp_to_xi[idx]) {
        slot_pref_set(d->conn.btaddr, pref);
        // refresh_mac was called inside slot_pref_set; done.
    } else {
        // No remap needed — still apply LED + presence in case of mirror rules.
        refresh_mac(d->conn.btaddr);
    }
    emit_xi_slot(bp_to_xi[idx]);
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

    // If any active forward names THIS controller as its target, the source
    // owns this slot for the duration of the forward — skip emitting our own
    // input so the two streams don't fight each frame.
    bool i_am_target = false;
    for (int f = 0; f < BRIDGE_MAX_PLAYERS; f++) {
        if (!forwards[f].active) continue;
        if (memcmp(forwards[f].target_mac, d->conn.btaddr, 6) == 0) {
            i_am_target = true;
            break;
        }
    }

    // Cache our latest state for any mirror merge that reads it later.
    slot_state[idx] = state;

    uint8_t my_xi = bp_to_xi[idx];
    int mirror_target_bp = find_mirror_target_for_source(d->conn.btaddr);

    if (mouse_mode[idx]) {
        // Either stick drives the cursor — sums of left + right so one-handed
        // use works either way. A = left click, B = right click.
        int dx = (int)stick_to_mouse_delta(state.lx) + (int)stick_to_mouse_delta(state.rx);
        int dy = (int)stick_to_mouse_delta(state.ly) + (int)stick_to_mouse_delta(state.ry);
        if (dx > 127)  dx = 127;
        if (dx < -127) dx = -127;
        if (dy > 127)  dy = 127;
        if (dy < -127) dy = -127;
        bool lmb = (state.buttons & BRIDGE_BTN_A) != 0;
        bool rmb = (state.buttons & BRIDGE_BTN_B) != 0;
        mouse_send((int8_t)dx, (int8_t)dy, lmb, rmb);
        if (!i_am_target) {
            bridge_gp_state_t neutral = {0};
            usb_xinput_set_gamepad(my_xi, &neutral);
        }
    } else if (mirror_target_bp >= 0) {
        // Mirror source: send merged state to target's XInput slot, not own.
        bridge_gp_state_t merged = slot_state[mirror_target_bp];
        merge_state(&merged, &state);
        usb_xinput_set_gamepad(bp_to_xi[mirror_target_bp], &merged);
    } else if (i_am_target) {
        bridge_gp_state_t merged = state;
        for (int f = 0; f < BRIDGE_MAX_PLAYERS; f++) {
            if (!forwards[f].active) continue;
            if (memcmp(forwards[f].target_mac, d->conn.btaddr, 6) != 0) continue;
            int source_bp = slot_for_mac(forwards[f].source_mac);
            if (source_bp < 0) continue;
            merge_state(&merged, &slot_state[source_bp]);
        }
        usb_xinput_set_gamepad(my_xi, &merged);
    } else {
        usb_xinput_set_gamepad(my_xi, &state);
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
