#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#define CFG_TUSB_MCU            OPT_MCU_RP2040
#define CFG_TUSB_OS             OPT_OS_PICO
#define CFG_TUSB_RHPORT0_MODE   (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE  64
#endif

// CDC is the dev/debug serial channel (NOT the Step 9 WebUSB config channel).
// HID is up to 4 gamepad interfaces — only those tied to a currently-paired
// controller are advertised at any moment.
#define CFG_TUD_HID             4
#define CFG_TUD_CDC             1
#define CFG_TUD_VENDOR          0
#define CFG_TUD_MSC             0
#define CFG_TUD_MIDI            0

#define CFG_TUD_HID_EP_BUFSIZE  64
#define CFG_TUD_CDC_RX_BUFSIZE  256
#define CFG_TUD_CDC_TX_BUFSIZE  256

#ifdef __cplusplus
}
#endif

#endif
