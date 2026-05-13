// Xbox 360 Wireless Receiver emulation.
//
// We register a custom TinyUSB class driver that claims the four
// vendor-class slot interfaces (0xFF / 0x5D / 0x81) declared in
// usb_descriptors.c. On each slot we speak the receiver wrapper protocol:
//
//   Receiver → host (interrupt IN):
//     byte 0: 0x08 = presence change; 0x00 = normal frame
//     byte 1: 0x80 set when controller present; any value != 0x01 means
//             bytes 4+ carry a valid 20-byte Xbox 360 input report
//     bytes 2-3: padding/reserved (zero is fine)
//     bytes 4-23: standard Xbox 360 input report (only when valid pad data)
//
//   Host → receiver (interrupt OUT):
//     08 00 0F C0 ...  presence inquiry — re-emit current slot status
//     00 00 08 4X ...  LED command, low nibble = LED pattern
//     00 01 0F C0 00 strong weak ...  rumble command
//
// Bit layout of the 16-bit button word in the Xbox 360 input report:
//   0=DUp 1=DDn 2=DLt 3=DRt 4=Start 5=Back 6=L3 7=R3
//   8=LB  9=RB  10=Guide 11=- 12=A 13=B 14=X 15=Y

#include "usb_xinput.h"

#include <pico/time.h>
#include <string.h>
#include <tusb.h>
#include <device/usbd_pvt.h>

#include "log.h"

#define XINPUT_ITF_CLASS    0xFF
#define XINPUT_ITF_SUBCLASS 0x5D
#define XINPUT_ITF_PROTOCOL 0x81
#define ITF_SLOT_FIRST      2

#define TX_BUF_LEN          32

// XInput button bit positions (in the 16-bit button word of an Xbox 360
// input report).
enum {
    XB_DPAD_UP    = 1u << 0,
    XB_DPAD_DOWN  = 1u << 1,
    XB_DPAD_LEFT  = 1u << 2,
    XB_DPAD_RIGHT = 1u << 3,
    XB_START      = 1u << 4,
    XB_BACK       = 1u << 5,
    XB_L3         = 1u << 6,
    XB_R3         = 1u << 7,
    XB_LB         = 1u << 8,
    XB_RB         = 1u << 9,
    XB_GUIDE      = 1u << 10,
    XB_A          = 1u << 12,
    XB_B          = 1u << 13,
    XB_X          = 1u << 14,
    XB_Y          = 1u << 15,
};

typedef struct {
    uint8_t ep_in;
    uint8_t ep_out;

    bool present;
    bool present_dirty;
    bool input_dirty;
    bool tx_busy;

    uint8_t tx_buf[TX_BUF_LEN];
    uint8_t rx_buf[TX_BUF_LEN];

    // 20-byte standard Xbox 360 input report payload (without wrapper).
    uint8_t input[20];

    // Latest rumble command from the host, pending pickup by bt_host.
    uint8_t rumble_strong;
    uint8_t rumble_weak;
    bool    rumble_pending;
} slot_t;

static slot_t slots[BRIDGE_MAX_PLAYERS];

static int slot_for_ep(uint8_t ep_addr) {
    for (int i = 0; i < BRIDGE_MAX_PLAYERS; i++) {
        if (slots[i].ep_in == ep_addr || slots[i].ep_out == ep_addr) return i;
    }
    return -1;
}

void usb_xinput_init(void) {
    memset(slots, 0, sizeof(slots));
    // Pre-populate input headers (packet type 0x00, length 0x14).
    for (int i = 0; i < BRIDGE_MAX_PLAYERS; i++) {
        slots[i].input[0] = 0x00;
        slots[i].input[1] = 0x14;
    }
    tusb_init();
}

void usb_xinput_set_controller_present(uint8_t slot, bool present) {
    if (slot >= BRIDGE_MAX_PLAYERS) return;
    log_str("xin: slot ");
    log_u(slot);
    log_line(present ? " present" : " absent");

    slots[slot].present = present;
    slots[slot].present_dirty = true;
    if (!present) {
        // Zero out the input payload so a stale "buttons held" frame can't
        // sneak out if the controller drops mid-press.
        memset(&slots[slot].input[2], 0, sizeof(slots[slot].input) - 2);
        slots[slot].input_dirty = true;
    }
}

static inline int16_t expand_stick(int16_t v) {
    // Bluepad32 axes are roughly [-512, 511]; XInput sticks are int16 full range.
    int32_t s = (int32_t)v * 64;
    if (s > 32767) return 32767;
    if (s < -32768) return -32768;
    return (int16_t)s;
}

static inline uint8_t expand_trigger(uint16_t v) {
    uint32_t t = (uint32_t)v * 255 / 1024;
    if (t > 255) t = 255;
    return (uint8_t)t;
}

static uint16_t map_buttons(uint32_t bb, uint8_t dpad) {
    uint16_t out = 0;
    if (dpad & BRIDGE_DPAD_UP)        out |= XB_DPAD_UP;
    if (dpad & BRIDGE_DPAD_DOWN)      out |= XB_DPAD_DOWN;
    if (dpad & BRIDGE_DPAD_LEFT)      out |= XB_DPAD_LEFT;
    if (dpad & BRIDGE_DPAD_RIGHT)     out |= XB_DPAD_RIGHT;
    if (bb & BRIDGE_BTN_START)        out |= XB_START;
    if (bb & BRIDGE_BTN_BACK)         out |= XB_BACK;
    if (bb & BRIDGE_BTN_THUMB_L)      out |= XB_L3;
    if (bb & BRIDGE_BTN_THUMB_R)      out |= XB_R3;
    if (bb & BRIDGE_BTN_SHOULDER_L)   out |= XB_LB;
    if (bb & BRIDGE_BTN_SHOULDER_R)   out |= XB_RB;
    if (bb & BRIDGE_BTN_SYSTEM)       out |= XB_GUIDE;
    if (bb & BRIDGE_BTN_A)            out |= XB_A;
    if (bb & BRIDGE_BTN_B)            out |= XB_B;
    if (bb & BRIDGE_BTN_X)            out |= XB_X;
    if (bb & BRIDGE_BTN_Y)            out |= XB_Y;
    return out;
}

bool usb_xinput_poll_rumble(uint8_t slot, uint8_t* strong, uint8_t* weak) {
    if (slot >= BRIDGE_MAX_PLAYERS) return false;
    if (!slots[slot].rumble_pending) return false;
    if (strong) *strong = slots[slot].rumble_strong;
    if (weak)   *weak   = slots[slot].rumble_weak;
    slots[slot].rumble_pending = false;
    return true;
}

void usb_xinput_set_gamepad(uint8_t slot, const bridge_gp_state_t* state) {
    if (slot >= BRIDGE_MAX_PLAYERS) return;

    slot_t* s = &slots[slot];
    uint8_t* r = s->input;

    r[0] = 0x00;
    r[1] = 0x14;

    uint16_t btn = map_buttons(state->buttons, state->dpad);
    r[2] = (uint8_t)(btn & 0xFF);
    r[3] = (uint8_t)(btn >> 8);

    // Triggers — bluepad32 lt/rt are 0..1023 analog, plus digital
    // BRIDGE_BTN_TRIGGER_L/R for the Switch's digital ZL/ZR.
    uint8_t lt = expand_trigger(state->lt);
    uint8_t rt = expand_trigger(state->rt);
    if (lt == 0 && (state->buttons & BRIDGE_BTN_TRIGGER_L)) lt = 255;
    if (rt == 0 && (state->buttons & BRIDGE_BTN_TRIGGER_R)) rt = 255;
    r[4] = lt;
    r[5] = rt;

    // xpad applies `~(__s16)` to the Y axes on the way in (Linux convention
    // wants Y up = positive). Pre-invert here so the round-trip lands right
    // side up for bluepad32's "Y down = positive" reporting.
    int16_t lx = expand_stick(state->lx);
    int16_t ly = expand_stick(-state->ly);
    int16_t rx = expand_stick(state->rx);
    int16_t ry = expand_stick(-state->ry);
    r[6]  = (uint8_t)(lx & 0xFF);  r[7]  = (uint8_t)(lx >> 8);
    r[8]  = (uint8_t)(ly & 0xFF);  r[9]  = (uint8_t)(ly >> 8);
    r[10] = (uint8_t)(rx & 0xFF);  r[11] = (uint8_t)(rx >> 8);
    r[12] = (uint8_t)(ry & 0xFF);  r[13] = (uint8_t)(ry >> 8);

    s->input_dirty = true;
}

// --- TinyUSB class driver -------------------------------------------------

static void xinput_class_init(void) {
    // Slot state cleared in usb_xinput_init(); nothing to do here.
}

static bool xinput_class_deinit(void) {
    return true;
}

static void xinput_class_reset(uint8_t rhport) {
    (void)rhport;
    for (int i = 0; i < BRIDGE_MAX_PLAYERS; i++) {
        slots[i].ep_in = 0;
        slots[i].ep_out = 0;
        slots[i].tx_busy = false;
    }
}

static uint16_t xinput_class_open(uint8_t rhport,
                                  tusb_desc_interface_t const* itf_desc,
                                  uint16_t max_len) {
    if (itf_desc->bInterfaceClass    != XINPUT_ITF_CLASS    ||
        itf_desc->bInterfaceSubClass != XINPUT_ITF_SUBCLASS ||
        itf_desc->bInterfaceProtocol != XINPUT_ITF_PROTOCOL) {
        return 0;
    }

    const int slot = (int)itf_desc->bInterfaceNumber - ITF_SLOT_FIRST;
    if (slot < 0 || slot >= BRIDGE_MAX_PLAYERS) return 0;

    uint8_t const* p = (uint8_t const*)itf_desc + itf_desc->bLength;
    uint16_t consumed = itf_desc->bLength;

    for (int i = 0; i < 2 && consumed < max_len; i++) {
        tusb_desc_endpoint_t const* ep = (tusb_desc_endpoint_t const*)p;
        if (ep->bDescriptorType != TUSB_DESC_ENDPOINT) break;

        if (!usbd_edpt_open(rhport, ep)) return 0;

        if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_IN) {
            slots[slot].ep_in = ep->bEndpointAddress;
        } else {
            slots[slot].ep_out = ep->bEndpointAddress;
            usbd_edpt_xfer(rhport, ep->bEndpointAddress,
                           slots[slot].rx_buf, TX_BUF_LEN);
        }

        consumed += ep->bLength;
        p += ep->bLength;
    }

    log_str("xin: opened slot ");
    log_u((uint32_t)slot);
    log_line("");

    // Mark this slot dirty so the host gets an initial presence frame.
    slots[slot].present_dirty = true;
    return consumed;
}

static bool xinput_class_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                         tusb_control_request_t const* request) {
    (void)rhport; (void)stage; (void)request;
    return false;  // no class-specific control requests for the receiver
}

static void handle_host_command(int slot, const uint8_t* data, uint32_t len) {
    if (len < 4) return;
    if (data[0] == 0x08 && data[1] == 0x00 && data[2] == 0x0F && data[3] == 0xC0) {
        // Presence inquiry — host wants current state re-sent.
        slots[slot].present_dirty = true;
    } else if (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x08) {
        // LED command. data[3] = 0x40 + pattern; we just log for now.
        log_str("xin: led slot ");
        log_u((uint32_t)slot);
        log_str(" pattern ");
        log_u((uint32_t)(data[3] & 0x0F));
        log_line("");
    } else if (data[0] == 0x00 && data[1] == 0x01 && data[2] == 0x0F && data[3] == 0xC0) {
        // Rumble command: data[5] = strong (low-frequency), data[6] = weak.
        slots[slot].rumble_strong = data[5];
        slots[slot].rumble_weak   = data[6];
        slots[slot].rumble_pending = true;
        log_str("xin: rumble slot ");
        log_u((uint32_t)slot);
        log_str(" s=");
        log_u(data[5]);
        log_str(" w=");
        log_u(data[6]);
        log_line("");
    }
}

static bool xinput_class_xfer_cb(uint8_t rhport, uint8_t ep_addr,
                                 xfer_result_t result, uint32_t xferred_bytes) {
    const int slot = slot_for_ep(ep_addr);
    if (slot < 0) return false;

    if (ep_addr == slots[slot].ep_in) {
        slots[slot].tx_busy = false;
    } else if (ep_addr == slots[slot].ep_out) {
        if (result == XFER_RESULT_SUCCESS) {
            handle_host_command(slot, slots[slot].rx_buf, xferred_bytes);
        }
        // Re-arm the OUT endpoint to keep receiving host commands.
        usbd_edpt_xfer(rhport, ep_addr, slots[slot].rx_buf, TX_BUF_LEN);
    }
    return true;
}

static const usbd_class_driver_t xinput_driver = {
    .name            = "XInput",
    .init            = xinput_class_init,
    .deinit          = xinput_class_deinit,
    .reset           = xinput_class_reset,
    .open            = xinput_class_open,
    .control_xfer_cb = xinput_class_control_xfer_cb,
    .xfer_cb         = xinput_class_xfer_cb,
    .sof             = NULL,
};

const usbd_class_driver_t* usbd_app_driver_get_cb(uint8_t* driver_count) {
    *driver_count = 1;
    return &xinput_driver;
}

// --- TX pump --------------------------------------------------------------

static void send_presence(slot_t* s) {
    memset(s->tx_buf, 0, TX_BUF_LEN);
    s->tx_buf[0] = 0x08;                         // presence-change frame
    s->tx_buf[1] = s->present ? 0x80 : 0x00;     // bit 7 = controller present
    s->tx_busy = true;
    s->present_dirty = false;
    usbd_edpt_xfer(0, s->ep_in, s->tx_buf, 29);
}

static void send_input(slot_t* s) {
    memset(s->tx_buf, 0, TX_BUF_LEN);
    s->tx_buf[0] = 0x00;
    s->tx_buf[1] = 0x01;        // xpad's xpad360w gates input on data[1] == 0x01 exactly
    memcpy(&s->tx_buf[4], s->input, sizeof(s->input));
    s->tx_busy = true;
    s->input_dirty = false;
    usbd_edpt_xfer(0, s->ep_in, s->tx_buf, 29);
}

void usb_xinput_tick(void) {
    tud_task();

    if (!tud_ready()) return;

    for (int i = 0; i < BRIDGE_MAX_PLAYERS; i++) {
        slot_t* s = &slots[i];
        if (s->ep_in == 0) continue;
        if (s->tx_busy) continue;
        if (!usbd_edpt_ready(0, s->ep_in)) continue;

        if (s->present_dirty) {
            send_presence(s);
        } else if (s->input_dirty && s->present) {
            send_input(s);
        }
    }
}
