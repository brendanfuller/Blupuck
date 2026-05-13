#include <string.h>
#include <tusb.h>

// Spoof the Microsoft Xbox 360 Wireless Receiver. Linux `xpad`, Windows
// XInput, and macOS Xbox drivers all bind on this VID/PID and decode the
// wireless-receiver wrapper protocol natively — no phantom controllers,
// and Steam Input treats each slot as a first-class XInput device.
#define BLUPUCK_VID 0x045E
#define BLUPUCK_PID 0x0719

// CDC is interfaces 0 and 1 (always-on debug). Four "slot" interfaces
// follow at 2..5. Each slot is vendor-class (0xFF / 0x5D / 0x81) with
// one interrupt IN and one interrupt OUT endpoint, exactly as xpad expects.
#define ITF_CDC_CTRL   0
#define ITF_CDC_DATA   1
#define ITF_SLOT_FIRST 2
#define NUM_SLOTS      4

#define EP_CDC_NOTIF   0x81
#define EP_CDC_OUT     0x02
#define EP_CDC_IN      0x82

// Interrupt IN/OUT pair for slot N. Avoids the CDC endpoints (0x01/0x81/0x82).
#define EP_SLOT_IN(n)  (uint8_t)(0x83 + (n))
#define EP_SLOT_OUT(n) (uint8_t)(0x03 + (n))

#define EP_SLOT_SIZE   32
#define EP_SLOT_INTERVAL 4   // ms (matches the real receiver's bInterval)

#define CDC_NOTIF_SIZE 8
#define CDC_BULK_SIZE  64

#define VENDOR_ITF_LEN (9 + 7 + 7)  // interface + 2 endpoints

enum {
    STR_LANG    = 0,
    STR_MFR     = 1,
    STR_PRODUCT = 2,
    STR_SERIAL  = 3,
    STR_CDC     = 4,
    STR_SLOT_1  = 5,
    STR_SLOT_2  = 6,
    STR_SLOT_3  = 7,
    STR_SLOT_4  = 8,
};

static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    // Composite descriptor with IAD — needed so Windows treats the CDC
    // function and the vendor-class slots as independent.
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = BLUPUCK_VID,
    .idProduct = BLUPUCK_PID,
    .bcdDevice = 0x0114,  // matches what real receivers report
    .iManufacturer = STR_MFR,
    .iProduct = STR_PRODUCT,
    .iSerialNumber = STR_SERIAL,
    .bNumConfigurations = 1,
};

#define SLOT_ITF(n, str_idx)                                                              \
    /* Interface descriptor: vendor 0xFF / 0x5D / 0x81 (wireless controller slot). */     \
    9, TUSB_DESC_INTERFACE,                                                                \
        (uint8_t)(ITF_SLOT_FIRST + (n)), /*alt*/ 0, /*eps*/ 2,                             \
        TUSB_CLASS_VENDOR_SPECIFIC, 0x5D, 0x81, str_idx,                                   \
    /* Interrupt IN endpoint (receiver → host). */                                         \
    7, TUSB_DESC_ENDPOINT, EP_SLOT_IN(n),                                                  \
        TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(EP_SLOT_SIZE), EP_SLOT_INTERVAL,                 \
    /* Interrupt OUT endpoint (host → receiver: LED, rumble, presence inquiry). */         \
    7, TUSB_DESC_ENDPOINT, EP_SLOT_OUT(n),                                                 \
        TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(EP_SLOT_SIZE), EP_SLOT_INTERVAL * 2

#define CDC_PART                                                                          \
    TUD_CDC_DESCRIPTOR(ITF_CDC_CTRL, STR_CDC, EP_CDC_NOTIF, CDC_NOTIF_SIZE,                \
                       EP_CDC_OUT, EP_CDC_IN, CDC_BULK_SIZE)

#define TOTAL_INTERFACES (2 + NUM_SLOTS)
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + NUM_SLOTS * VENDOR_ITF_LEN)

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, TOTAL_INTERFACES, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    CDC_PART,
    SLOT_ITF(0, STR_SLOT_1),
    SLOT_ITF(1, STR_SLOT_2),
    SLOT_ITF(2, STR_SLOT_3),
    SLOT_ITF(3, STR_SLOT_4),
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
    return desc_configuration;
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
