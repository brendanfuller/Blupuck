#include <string.h>
#include <tusb.h>

// Dev/test VID:PID — not for shipping hardware. Replace before production.
#define BRIDGE_VID 0xCafe
#define BRIDGE_PID 0x4000

enum { ITF_NUM_HID = 0, ITF_NUM_TOTAL };

#define EPNUM_HID 0x81
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = BRIDGE_VID,
    .idProduct = BRIDGE_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

static const uint8_t desc_hid_report[] = {
    TUD_HID_REPORT_DESC_GAMEPAD(),
};

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_hid_report), EPNUM_HID,
                       CFG_TUD_HID_EP_BUFSIZE, 1 /* poll interval ms */),
};

static const char* string_desc[] = {
    (const char[]){0x09, 0x04},  // 0: English (US)
    "Bridge",                    // 1: Manufacturer
    "BT Controller Bridge",      // 2: Product
    "000000000001",              // 3: Serial — TODO read from flash UID
};

const uint8_t* tud_descriptor_device_cb(void) {
    return (const uint8_t*)&desc_device;
}

const uint8_t* tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
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

// HID GET_REPORT — not used; host pulls IN reports directly.
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t* buffer,
                               uint16_t reqlen) {
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
    return 0;
}

// HID SET_REPORT — no OUT reports yet (rumble in a later step).
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type, const uint8_t* buffer,
                           uint16_t bufsize) {
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;
}
