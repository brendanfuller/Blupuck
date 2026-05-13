# WebUSB Binary Protocol

Wire format for the vendor interface (USB interface 7) shared between the
bridge firmware and the Vue config app.

Authoritative for both sides:
- `firmware/include/protocol.h`
- `webui/src/protocol/types.ts` + `messages.ts`

## Status

Not yet defined. Pinned down during firmware Step 9 (WebUSB vendor interface).

## Sections to fill in

- Framing (length prefix, message id, payload)
- Endianness (little-endian, to match RP2040/RP2350)
- Versioning + handshake
- Message catalog:
  - Status read (per-controller state, battery, signal)
  - Live input stream (subscribe / unsubscribe)
  - Remap table read/write
  - Mirror configuration read/write
  - Profile list / load / save / delete
  - Firmware update chunk
- Error codes
