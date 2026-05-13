#include <string.h>
#include <tusb.h>

// Dev/test VID:PID — not for shipping hardware. Replace before production.
#define BLUPUCK_VID 0xCafe
#define BLUPUCK_PID 0x4000

// All 4 HID interfaces are always enumerated; we don't do live USB
// re-enumeration as slots fill/empty. This keeps Steam Input and the host
// USB stack happy at the cost of four "phantom" gamepads visible to the
// host even with no controllers paired. The phantom slots stay silent
// (no reports sent) until a controller is bound to them.

static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    // Use a composite-device class triplet so Windows treats the device as a
    // multi-function container and lets each interface bind its own driver.
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = BLUPUCK_VID,
    .idProduct = BLUPUCK_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

static const uint8_t desc_hid_report[] = {
    TUD_HID_REPORT_DESC_GAMEPAD(),
};

enum {
    STR_LANG    = 0,
    STR_MFR     = 1,
    STR_PRODUCT = 2,
    STR_SERIAL  = 3,
    STR_CDC     = 4,
    STR_CTRL_1  = 5,
    STR_CTRL_2  = 6,
    STR_CTRL_3  = 7,
    STR_CTRL_4  = 8,
};

// CDC occupies interfaces 0 and 1 and is always present (it's the always-on
// debug channel). HID gamepads, when present, start at interface 2.
#define ITF_CDC_CTRL   0
#define ITF_CDC_DATA   1
#define ITF_HID_FIRST  2

// CDC endpoints (notification IN, data OUT, data IN).
#define EP_CDC_NOTIF   0x81
#define EP_CDC_OUT     0x02
#define EP_CDC_IN      0x82

// HID endpoint IN for the Nth gamepad instance.
#define EP_HID_IN(n)   (uint8_t)(0x83 + (n))

#define CDC_NOTIF_SIZE 8
#define CDC_BULK_SIZE  64

#define HID_ITF(n, str_idx)                                                 \
    TUD_HID_DESCRIPTOR((uint8_t)(ITF_HID_FIRST + (n)), str_idx,              \
                       HID_ITF_PROTOCOL_NONE,                                \
                       sizeof(desc_hid_report), EP_HID_IN(n),                \
                       CFG_TUD_HID_EP_BUFSIZE, /*poll ms*/ 1)

#define CONFIG_LEN_0 (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)
#define CONFIG_LEN_1 (CONFIG_LEN_0 + TUD_HID_DESC_LEN)
#define CONFIG_LEN_2 (CONFIG_LEN_0 + 2 * TUD_HID_DESC_LEN)
#define CONFIG_LEN_3 (CONFIG_LEN_0 + 3 * TUD_HID_DESC_LEN)
#define CONFIG_LEN_4 (CONFIG_LEN_0 + 4 * TUD_HID_DESC_LEN)

#define CDC_PART \
    TUD_CDC_DESCRIPTOR(ITF_CDC_CTRL, STR_CDC, EP_CDC_NOTIF, CDC_NOTIF_SIZE,  \
                       EP_CDC_OUT, EP_CDC_IN, CDC_BULK_SIZE)

static const uint8_t desc_config_0[] = {
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, CONFIG_LEN_0, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    CDC_PART,
};

static const uint8_t desc_config_1[] = {
    TUD_CONFIG_DESCRIPTOR(1, 3, 0, CONFIG_LEN_1, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    CDC_PART,
    HID_ITF(0, STR_CTRL_1),
};

static const uint8_t desc_config_2[] = {
    TUD_CONFIG_DESCRIPTOR(1, 4, 0, CONFIG_LEN_2, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    CDC_PART,
    HID_ITF(0, STR_CTRL_1),
    HID_ITF(1, STR_CTRL_2),
};

static const uint8_t desc_config_3[] = {
    TUD_CONFIG_DESCRIPTOR(1, 5, 0, CONFIG_LEN_3, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    CDC_PART,
    HID_ITF(0, STR_CTRL_1),
    HID_ITF(1, STR_CTRL_2),
    HID_ITF(2, STR_CTRL_3),
};

static const uint8_t desc_config_4[] = {
    TUD_CONFIG_DESCRIPTOR(1, 6, 0, CONFIG_LEN_4, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    CDC_PART,
    HID_ITF(0, STR_CTRL_1),
    HID_ITF(1, STR_CTRL_2),
    HID_ITF(2, STR_CTRL_3),
    HID_ITF(3, STR_CTRL_4),
};

static const uint8_t* const desc_configs[] = {
    desc_config_0, desc_config_1, desc_config_2, desc_config_3, desc_config_4,
};

static const char* string_desc[] = {
    (const char[]){0x09, 0x04},   // 0: English (US) langid
    "Blupuck",                    // 1: Manufacturer
    "Blupuck",                    // 2: Product
    "000000000001",               // 3: Serial — TODO read from flash UID
    "Blupuck Debug",              // 4: CDC interface name
    "Blupuck Controller 1",       // 5
    "Blupuck Controller 2",       // 6
    "Blupuck Controller 3",       // 7
    "Blupuck Controller 4",       // 8
};

const uint8_t* tud_descriptor_device_cb(void) {
    return (const uint8_t*)&desc_device;
}

const uint8_t* tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_config_4;
}

const uint8_t* tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return desc_hid_report;
}

static uint16_t string_desc_buf[32];

const uint16_t* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;

    uint8_t chr_count;
    if (index == 0) {
        memcpy(&string_desc_buf[1], string_desc[0], 2);
        chr_count = 1;
    } else if (index < (sizeof(string_desc) / sizeof(string_desc[0]))) {
        const char* s = string_desc[index];
        chr_count = (uint8_t)strlen(s);
        if (chr_count > 31) chr_count = 31;
        for (uint8_t i = 0; i < chr_count; i++) {
            string_desc_buf[1 + i] = s[i];
        }
    } else {
        return NULL;
    }

    string_desc_buf[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return string_desc_buf;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t* buffer,
                               uint16_t reqlen) {
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type, const uint8_t* buffer,
                           uint16_t bufsize) {
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;
}
