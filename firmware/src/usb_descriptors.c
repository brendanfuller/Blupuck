#include <string.h>
#include <tusb.h>

// Spoof the Microsoft Xbox 360 Wireless Receiver. Linux `xpad`, Windows
// XInput, and macOS Xbox drivers all bind on this VID/PID and decode the
// wireless-receiver wrapper protocol natively — no phantom controllers,
// and Steam Input treats each slot as a first-class XInput device.
#define BLUPUCK_VID 0x045E
#define BLUPUCK_PID 0x0719

// Interface layout:
//   0,1    CDC (always-on debug)
//   2..5   Vendor-class XInput slots (0xFF / 0x5D / 0x81), xpad-handled
//   6      HID mouse (single-IN endpoint, used as boot wake source)
//   7      Vendor-class WebUSB (bulk IN+OUT, config UI in the browser)
#define ITF_CDC_CTRL    0
#define ITF_CDC_DATA    1
#define ITF_SLOT_FIRST  2
#define NUM_SLOTS       4
#define ITF_MOUSE       6
#define ITF_WEBUSB      7

#define EP_CDC_NOTIF   0x81
#define EP_CDC_OUT     0x02
#define EP_CDC_IN      0x82

// Interrupt IN/OUT pair for slot N. Avoids the CDC endpoints (0x01/0x81/0x82).
#define EP_SLOT_IN(n)  (uint8_t)(0x83 + (n))
#define EP_SLOT_OUT(n) (uint8_t)(0x03 + (n))

#define EP_MOUSE_IN    0x87
#define EP_MOUSE_SIZE  8
#define EP_MOUSE_INTERVAL 10  // ms — mouse is idle except for boot wake

#define EP_WEBUSB_OUT  0x07
#define EP_WEBUSB_IN   0x88
#define EP_WEBUSB_SIZE 64

#define EP_SLOT_SIZE   32
#define EP_SLOT_INTERVAL 4   // ms (matches the real receiver's bInterval)

#define CDC_NOTIF_SIZE 8
#define CDC_BULK_SIZE  64

#define VENDOR_ITF_LEN (9 + 7 + 7)  // interface + 2 endpoints

// WebUSB / MS-OS-2.0 vendor request codes — referenced from BOS below.
#define VENDOR_REQUEST_WEBUSB     1
#define VENDOR_REQUEST_MICROSOFT  2

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
    STR_MOUSE   = 9,
    STR_WEBUSB  = 10,
};

static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0210,  // 2.1+ required for the BOS descriptor that WebUSB uses
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

static const uint8_t desc_hid_mouse_report[] = {
    TUD_HID_REPORT_DESC_MOUSE()
};

#define MOUSE_PART                                                                        \
    TUD_HID_DESCRIPTOR(ITF_MOUSE, STR_MOUSE, HID_ITF_PROTOCOL_MOUSE,                       \
                       sizeof(desc_hid_mouse_report), EP_MOUSE_IN,                          \
                       EP_MOUSE_SIZE, EP_MOUSE_INTERVAL)

#define TOTAL_INTERFACES (2 + NUM_SLOTS + 1 + 1)
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN +                          \
                          NUM_SLOTS * VENDOR_ITF_LEN + TUD_HID_DESC_LEN +                   \
                          TUD_VENDOR_DESC_LEN)

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, TOTAL_INTERFACES, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    CDC_PART,
    SLOT_ITF(0, STR_SLOT_1),
    SLOT_ITF(1, STR_SLOT_2),
    SLOT_ITF(2, STR_SLOT_3),
    SLOT_ITF(3, STR_SLOT_4),
    MOUSE_PART,
    TUD_VENDOR_DESCRIPTOR(ITF_WEBUSB, STR_WEBUSB, EP_WEBUSB_OUT, EP_WEBUSB_IN, EP_WEBUSB_SIZE),
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
    "Blupuck Wake",                // 9
    "Blupuck Config",              // 10: WebUSB interface
};

// --- BOS descriptor: advertises WebUSB + MS OS 2.0 platform capabilities. -----

#define WEBUSB_URL "blupuck.local"

static const tusb_desc_webusb_url_t desc_url = {
    .bLength         = 3 + sizeof(WEBUSB_URL) - 1,
    .bDescriptorType = 3,
    .bScheme         = 1,  // https
    .url             = WEBUSB_URL,
};

#define MS_OS_20_DESC_LEN 0xB2
#define BOS_TOTAL_LEN     (TUD_BOS_DESC_LEN + TUD_BOS_WEBUSB_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)

static const uint8_t desc_bos[] = {
    TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 2),
    TUD_BOS_WEBUSB_DESCRIPTOR(VENDOR_REQUEST_WEBUSB, 1),
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, VENDOR_REQUEST_MICROSOFT),
};

const uint8_t* tud_descriptor_bos_cb(void) {
    return desc_bos;
}

// MS OS 2.0 descriptor: tells Windows to bind WinUSB to the WebUSB interface
// without an INF file. DeviceInterfaceGUIDs registry property is also set so
// userland apps can resolve and open the interface.
static const uint8_t desc_ms_os_20[] = {
    // Set header
    U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR),
    U32_TO_U8S_LE(0x06030000), U16_TO_U8S_LE(MS_OS_20_DESC_LEN),
    // Configuration subset header
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION),
    0, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A),
    // Function subset header — binds the WinUSB driver to our WebUSB interface
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION),
    ITF_WEBUSB, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08),
    // Compatible ID: WINUSB
    U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID),
    'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Registry property — DeviceInterfaceGUIDs (a fresh GUID)
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08 - 0x08 - 0x14),
    U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
    U16_TO_U8S_LE(0x0007), U16_TO_U8S_LE(0x002A),
    'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0, 'I', 0, 'n', 0, 't', 0, 'e', 0,
    'r', 0, 'f', 0, 'a', 0, 'c', 0, 'e', 0, 'G', 0, 'U', 0, 'I', 0, 'D', 0, 's', 0, 0, 0,
    U16_TO_U8S_LE(0x0050),
    '{', 0, '9', 0, '7', 0, '5', 0, 'F', 0, '4', 0, '4', 0, 'D', 0, '9', 0, '-', 0,
    '0', 0, 'D', 0, '0', 0, '8', 0, '-', 0, '4', 0, '3', 0, 'F', 0, 'D', 0, '-', 0,
    '8', 0, 'B', 0, '3', 0, 'E', 0, '-', 0, '1', 0, '2', 0, '7', 0, 'C', 0, 'A', 0,
    '8', 0, 'A', 0, 'F', 0, 'F', 0, 'F', 0, '9', 0, 'D', 0, '}', 0, 0, 0, 0, 0,
};

TU_VERIFY_STATIC(sizeof(desc_ms_os_20) == MS_OS_20_DESC_LEN, "MS OS 2.0 size mismatch");

// Handle WebUSB / MS-OS-2.0 vendor control transfers on EP0. Browsers and
// Windows issue these during enumeration to discover the WebUSB landing page
// and the WinUSB binding.
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                tusb_control_request_t const* request) {
    if (stage != CONTROL_STAGE_SETUP) return true;

    if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR) {
        if (request->bRequest == VENDOR_REQUEST_WEBUSB) {
            return tud_control_xfer(rhport, request,
                                    (void*)(uintptr_t)&desc_url, desc_url.bLength);
        }
        if (request->bRequest == VENDOR_REQUEST_MICROSOFT && request->wIndex == 7) {
            uint16_t total_len;
            memcpy(&total_len, desc_ms_os_20 + 8, 2);
            return tud_control_xfer(rhport, request,
                                    (void*)(uintptr_t)desc_ms_os_20, total_len);
        }
    }
    return false;
}

const uint8_t* tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return desc_hid_mouse_report;
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
