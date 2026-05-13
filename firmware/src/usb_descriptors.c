// Composite USB device descriptor.
// Interfaces (per spec §4):
//   0..3  HID gamepad (Players 1–4, XInput-style report layout)
//   4     HID mouse   (wake-on-power)
//   5/6   CDC         (debug serial)
//   7     Vendor      (WebUSB + MS OS 2.0 descriptor)
//
// TODO: device descriptor, config descriptor, HID report descriptors,
// MS OS 2.0 descriptor set, BOS descriptor with WebUSB platform capability.
