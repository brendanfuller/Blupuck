#ifndef PROTOCOL_H
#define PROTOCOL_H

// Wire format for the WebUSB control channel (Interface 7).
// Authoritative spec: docs/protocol.md. Must stay in sync with the matching
// TypeScript definitions in webui/src/protocol/types.ts.
//
// Frame layout (little-endian):
//
//     [ type : u8 ][ len : u16 ][ payload : len bytes ]
//
// All numeric fields in payloads are little-endian. MAC addresses are raw
// 6-byte arrays in big-endian "human-readable" order (so addr[0] is the OUI
// high byte). Strings are length-prefixed (u8 length + UTF-8 bytes), max 63
// bytes of payload.

#include <stdint.h>

#define PROTOCOL_VERSION       1
#define PROTOCOL_MAX_PAYLOAD   240
#define PROTOCOL_MAC_LEN       6
#define PROTOCOL_NAME_MAX      63

// ----------------------------------------------------------------------------
// Bridge → host events (0x10..0x1F)
// ----------------------------------------------------------------------------

// Sent unsolicited on first GET_STATUS and any time the bridge wants the host
// to know what it is.
//   u8  protocol_version
//   u8  max_slots             (4)
//   u8  firmware_major
//   u8  firmware_minor
#define MSG_HELLO              0x10

// Sent whenever a slot's connection / mode state changes. Also sent for
// every slot in response to GET_STATUS, and from the 3 s status timer.
//   u8   slot                 (0..3)
//   u8   present              (0 = no controller in this slot, 1 = present)
//   u8   battery              (bluepad32 convention: 0 = unavailable,
//                              1 = empty, 255 = full)
//   i8   rssi                 (BT Classic golden-range relative; 0 ≈ ok)
//   u8   flags                (bit 0 = mouse mode)
//   u8   mac[6]               (zeroed if present == 0)
//   u8   name_len
//   u8   name[name_len]       (UTF-8, max PROTOCOL_NAME_MAX)
#define MSG_CONTROLLER         0x11

#define CONTROLLER_FLAG_MOUSE  0x01

// Sent on state change and in response to GET_STATUS.
//   u8   enabled              (0 = scanning paused, 1 = actively scanning)
#define MSG_SCANNING           0x12

// Sent in response to GET_STATUS or LIST_BONDS, and any time the bond table
// changes (new pair, forget).
//   u8   count
//   N × {
//       u8 mac[6]
//       u8 preferred_slot     (0..3, 0xFF = no preference yet)
//   }
#define MSG_BOND_LIST          0x13

// ----------------------------------------------------------------------------
// Host → bridge commands (0x80..0x8F)
// ----------------------------------------------------------------------------

// No payload. Bridge replies with HELLO + 4× CONTROLLER + SCANNING + BOND_LIST.
#define MSG_GET_STATUS         0x80

//   u8   enabled              (0 = stop, 1 = start)
#define MSG_SET_SCANNING       0x81

//   u8   mac[6]
#define MSG_FORGET_MAC         0x82

// No payload. Wipes every stored bond.
#define MSG_FORGET_ALL         0x83

//   u8   mac[6]
//   u8   preferred_slot       (0..3)
#define MSG_SET_SLOT           0x84

// Blink the HOME-button ring LED on the named slot for a few seconds so the
// user can physically identify which controller is in which slot.
//   u8   slot
#define MSG_IDENTIFY           0x85

// "Mirror" `source_mac`'s inputs into the slot owned by `target_mac`. The
// source's own slot disappears from the host (present=false) and its input
// state is OR-merged (buttons / dpad) and max-magnitude-merged (sticks /
// triggers) with the target's into the target's slot. Setting `target_mac`
// to all-zero clears the mirror for that source.
//   u8   source_mac[6]
//   u8   target_mac[6]
#define MSG_SET_FORWARD        0x86

#endif
