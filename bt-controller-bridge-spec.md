# Bluetooth Controller Bridge — Design Specification

## 1. Purpose

A dedicated Bluetooth-to-USB bridge that connects multiple Nintendo Switch controllers (Joy-Cons and Pro Controllers) to a PC, presenting them as standard USB HID gamepads. The bridge exists to solve specific shortcomings of pairing Switch controllers directly to a PC over Bluetooth:

- Unreliable wake-from-sleep behavior on the host PC
- High latency and jitter caused by the host's general-purpose BT stack sharing radio time with other paired devices (mice, headsets, phones)
- Awkward re-pairing rituals after each power cycle
- No clean way to handle four-plus controllers simultaneously

By moving the BT work onto a dedicated radio and presenting clean USB HID to the PC, the bridge gives the host a wired-feeling experience while keeping the wireless convenience of the original controllers.

## 2. Hardware

### 2.1 Target Boards

| Board | Chip | SRAM | USB | Bluetooth | Status |
|---|---|---|---|---|---|
| Raspberry Pi Pico W | RP2040 + CYW43439 | 264 KB | Native device | BT 5.2 Classic + LE | Prototype target |
| Raspberry Pi Pico 2 W | RP2350 + CYW43439 | 520 KB | Native device | BT 5.2 Classic + LE | Production target |

Both boards share the same CYW43439 radio module, so all Bluetooth-side code is identical between them. Migration is a single CMake setting change (`PICO_BOARD pico_w` → `PICO_BOARD pico2_w`) plus a recompile.

### 2.2 Rationale

The CYW43439 is the only widely-available radio chip on a sub-$15 board that simultaneously supports:

- Bluetooth Classic (BR/EDR), required for Switch controllers
- Bluetooth Low Energy, useful for future controller support
- Tight integration with a microcontroller that has native USB device capability

The Pico W family also has the most mature open-source Bluetooth gaming controller ecosystem (Bluepad32, GP2040-CE, multiple reference projects), making it the lowest-risk hardware choice for this design.

### 2.3 Power and Connectivity

- USB-C or Micro-USB (board-dependent) provides power and acts as the host-facing data link
- No external antenna required; the on-module PCB antenna is adequate for desk-range operation
- No additional components required for the minimum viable bridge

## 3. System Architecture

### 3.1 High-Level Block Diagram

```
┌──────────────┐       ┌─────────────────────────────────┐       ┌──────┐
│ Joy-Con 1    │◀─BT──▶│                                 │       │      │
│ Joy-Con 2    │◀─BT──▶│  Pico W / Pico 2 W              │◀─USB─▶│  PC  │
│ Joy-Con 3    │◀─BT──▶│  (CYW43439 + RP2040/RP2350)     │       │      │
│ Joy-Con 4    │◀─BT──▶│                                 │       │      │
└──────────────┘       └─────────────────────────────────┘       └──────┘
                              │
                              ├─ BT host: Bluepad32 + BTstack
                              ├─ State translation
                              └─ USB device: TinyUSB composite
```

### 3.2 Data Flow

1. Each paired Switch controller streams input reports over BT Classic HID at ~60-120 Hz
2. Bluepad32 parses the reports into normalized controller state structs
3. A translation layer maps Switch button/axis layout onto an XInput-style gamepad layout
4. TinyUSB sends the result as HID input reports to the host PC over USB

The bridge does no input filtering, deadzone processing, or remapping. All such logic is deferred to Steam Input on the host. The firmware's job is to be a transparent, low-latency conduit.

## 4. USB Composite Device Design

The bridge presents to the host as a single USB composite device exposing multiple interfaces:

| Interface | Class | Purpose |
|---|---|---|
| 0 | HID | Gamepad — Player 1 |
| 1 | HID | Gamepad — Player 2 |
| 2 | HID | Gamepad — Player 3 |
| 3 | HID | Gamepad — Player 4 |
| 4 | HID | Mouse (wake-on-power + optional touchpad emulation) |
| 5 / 6 | CDC | Virtual serial port for debug output |
| 7 | Vendor (WebUSB) | Browser-based configuration channel |

### 4.1 Gamepad Interfaces (0–3)

Each gamepad interface declares an independent HID report descriptor matching an XInput-style layout: two analog sticks (16-bit each axis), two analog triggers, four face buttons, four shoulder/menu buttons, an 8-way D-pad, and two stick clicks. Each interface has its own report ID so the host treats them as four physically distinct gamepads.

This layout is chosen because Steam Input recognizes XInput-style descriptors universally and provides the same per-game remapping, gyro mapping, and radial menu features that motivated the project in the first place. Emulating a specific controller VID/PID (Steam Controller, Pro Controller, etc.) is explicitly avoided — those identifiers carry fragile vendor-specific handshake expectations.

### 4.2 Mouse Interface (4)

A minimal HID mouse interface exists primarily to enable wake-from-sleep. On firmware boot, the bridge sends a single 1-pixel relative move report, which wakes the host if "Allow this device to wake the computer" is enabled in Device Manager. After the initial wake event the mouse remains idle.

Optionally, this interface can later be wired to a gesture mapped from one of the connected controllers (e.g. right stick → mouse cursor for desktop navigation between game sessions).

### 4.3 CDC Serial (5–6)

A standard virtual serial port for `printf`-style firmware debugging. Enumerates as `COM*` on Windows or `/dev/ttyACM*` on Linux/macOS. Used for development, log streaming, and any text-mode configuration commands.

### 4.4 WebUSB Vendor Interface (7)

A vendor-specific bulk endpoint that allows a browser (Chromium-based, via the WebUSB API) to communicate directly with the bridge from a hosted configuration web page. Used for:

- Live controller mapping ("press the button you want to bind to X")
- Per-controller battery level display
- Saving and loading mapping profiles
- Firmware update streaming

The interface declares an MS OS 2.0 descriptor so Windows automatically associates it with WinUSB, allowing driverless operation.

### 4.5 Interface Association

Windows historically struggles with composite devices exceeding seven interfaces or grouping CDC and vendor-class without explicit hints. The descriptor uses Interface Association Descriptors (IADs) to group the CDC pair and the vendor interface, ensuring clean plug-and-play behavior on Windows 10/11.

## 5. Bluetooth Side

### 5.1 Controller Support

The bridge targets the following controllers, all using Bluetooth Classic HID:

- Nintendo Switch Pro Controller
- Nintendo Switch Joy-Con (Left)
- Nintendo Switch Joy-Con (Right)

Each Joy-Con appears as a separate virtual gamepad. Joy-Cons are not combined into a single virtual controller; users can pair Joy-Con pairs as two separate "players" or use Pro Controllers as single units.

### 5.2 Pairing Behavior

- On first pairing, a controller is put into sync mode (Sync button on Pro Controller / SL+SR on Joy-Con) and the bridge discovers and bonds it
- Bonded controller MAC addresses are persisted to RP2040/RP2350 flash storage
- On subsequent power-ups, the bridge initiates reconnection to all known bonded MACs without user intervention
- A long-press of a designated control (e.g. holding BOOTSEL during power-on, or a software combo) clears all bonds for re-pairing

### 5.3 Connection Parameters

- WiFi is explicitly disabled in firmware to eliminate radio coexistence overhead
- Page scanning is disabled once all known controllers are connected
- HID input reports are requested in standard mode (Switch report mode 0x30) for full IMU and battery data

### 5.4 Multi-Controller Limits

- 4 controllers: comfortable, default configuration, well within radio bandwidth
- 5–7 controllers: supported by raising `BP32_MAX_GAMEPADS` and BTstack connection limits, but air-time contention causes increasing jitter
- 8 controllers: not supported on a single radio (Bluetooth Classic piconet spec limit is 7 active slaves per master). To exceed 4 with low jitter, the recommended path is two bridge devices on separate USB ports, each handling 4 controllers

## 6. Latency Budget

Target end-to-end latency from physical button press to host PC input event:

| Stage | Typical | Notes |
|---|---|---|
| Controller internal poll | ~1 ms | Fixed in controller firmware |
| BT air time + scheduling | 8–15 ms | Dominant cost |
| Bluepad32 / BTstack processing | <1 ms | |
| State translation | <1 ms | |
| USB HID poll interval | 1 ms | Set via `bInterval = 1` in descriptor |
| **Total** | **~12–18 ms typical** | Comparable to direct Switch-to-Joy-Con |

Worst-case under heavy load with 4–7 controllers: 20–25 ms. Acceptable for all single-player and most multiplayer scenarios; not competitive with a dedicated proprietary 2.4 GHz protocol (~8 ms), but substantially better than typical Windows-direct BT HID (~25–40 ms).

### 6.1 Latency Tuning Requirements

The firmware must observe these constraints to hit the latency targets:

- USB HID descriptor sets `bInterval = 1` (1 ms polling)
- WiFi disabled at `cyw43_arch_init` (do not enable STA mode)
- No filtering, smoothing, or deadzone processing in firmware
- BT page scanning disabled once known controllers are connected
- HID report buffers pre-allocated; no heap allocation in hot path

## 7. Technology Stack

### 7.1 Core Libraries

| Component | Library | Purpose |
|---|---|---|
| SDK | pico-sdk | Hardware abstraction, CMake build system |
| BT host | Bluepad32 | Multi-controller BT HID host abstraction |
| BT stack | BTstack | Underlying BT Classic + LE protocol stack |
| USB device | TinyUSB | Composite USB device implementation |
| WiFi radio (disabled) | cyw43-driver | Linked but not initialized |

All four are open-source and bundled with or compatible with pico-sdk's standard build flow.

### 7.2 Languages and Frameworks

- **C11** for firmware (matches Bluepad32 and TinyUSB native interfaces)
- **Vue 3** (Composition API, `<script setup>` SFCs) + **Vite** for the WebUSB configuration UI
- **TypeScript** strongly recommended for the web app — the WebUSB API surface is small but easy to mis-type, and binary protocol framing benefits from strict types

The web app is built with Vite and deployed as a static bundle. It can be hosted on GitHub Pages, a personal domain, or served locally (`vite preview`) — there is no backend.

**Why Vue 3 + Vite over a single static HTML page:**

The configuration UI is expected to grow into non-trivial functionality:

- **Controller remapping** — drag-and-drop binding of physical inputs to virtual gamepad outputs per-controller, with live visual feedback as buttons are pressed. This is fundamentally state-heavy and benefits from a reactive framework.
- **Controller mirroring** — letting one physical controller drive multiple virtual gamepad slots simultaneously, or merging two Joy-Cons into a single virtual gamepad. Configuration UI for this requires multi-panel state coordination.
- **Profile management** — saving, naming, importing, and exporting mapping profiles. CRUD-style UI that benefits from component composition.
- **Live diagnostics** — real-time display of per-controller input state, battery, signal quality, and latency, updated at 30+ Hz from WebUSB. Vue's reactivity handles this cleanly without manual DOM thrash.

Vue 3 + Vite is chosen specifically because:

- **Fast iteration.** Vite's HMR makes developing against a live device pleasant; changes to the UI appear instantly without losing the WebUSB connection.
- **Small bundle.** A Vue 3 + Vite production build for a single-page config app comes in around 50–80 KB gzipped, lightweight enough to host anywhere.
- **Composable.** The remapping and mirroring features can be developed as independent components without entangling the core "connect and show status" UI.
- **No build-time backend.** Pure static output, no Node runtime needed in production.

Alternatives considered and not chosen: vanilla JS (insufficient for the planned state complexity), React (heavier, more boilerplate for this scale), Svelte (good fit but smaller ecosystem for things like drag-and-drop libraries).

### 7.3 Build and Flash

- pico-sdk via CMake
- Cross-compilation with `arm-none-eabi-gcc`
- VS Code with the official Raspberry Pi Pico extension recommended for development
- Flash via UF2 drag-and-drop (BOOTSEL button) or SWD debugger for iterative work

The Arduino IDE / arduino-pico path is not used. Direct pico-sdk gives finer control over USB descriptors, BT configuration, and memory layout, all of which matter for this design.

### 7.4 Repository Layout

```
bridge/
├── firmware/
│   ├── CMakeLists.txt
│   ├── pico_sdk_import.cmake
│   ├── src/
│   │   ├── main.c                  # Entry, init, main loop
│   │   ├── bt_host.c               # Bluepad32 setup, controller callbacks
│   │   ├── bt_bonds.c              # MAC persistence in flash
│   │   ├── usb_descriptors.c       # Composite descriptor
│   │   ├── usb_hid.c               # 4× gamepad + mouse report generation
│   │   ├── usb_cdc.c               # Debug serial routing
│   │   ├── usb_webusb.c            # Vendor interface for browser config
│   │   ├── translate.c             # Switch report → XInput report mapping
│   │   ├── mapping.c               # User-defined remap tables (future)
│   │   └── wake.c                  # Boot-time mouse jiggle
│   ├── include/
│   │   ├── tusb_config.h           # TinyUSB feature flags
│   │   └── protocol.h              # Shared WebUSB framing definitions
│   └── lib/
│       ├── bluepad32/              # Submodule
│       ├── btstack/                # Submodule (via bluepad32)
│       └── tinyusb/                # Submodule (via pico-sdk)
│
├── webui/
│   ├── index.html                  # Vite entry
│   ├── vite.config.ts
│   ├── package.json
│   ├── tsconfig.json
│   ├── src/
│   │   ├── main.ts                 # Vue app bootstrap
│   │   ├── App.vue                 # Root component
│   │   ├── components/
│   │   │   ├── DeviceConnector.vue # WebUSB connect/disconnect
│   │   │   ├── ControllerCard.vue  # Per-controller status + battery
│   │   │   ├── LiveInputView.vue   # Real-time button/stick visualization
│   │   │   ├── RemapEditor.vue     # Drag-to-bind remap UI (future)
│   │   │   ├── MirrorConfig.vue    # Multi-slot mirror setup (future)
│   │   │   └── ProfileManager.vue  # Save/load/export profiles (future)
│   │   ├── composables/
│   │   │   ├── useWebUsb.ts        # WebUSB connection + framing
│   │   │   ├── useControllers.ts   # Reactive controller state store
│   │   │   └── useProfiles.ts      # Profile persistence
│   │   ├── protocol/
│   │   │   ├── messages.ts         # Binary message encode/decode
│   │   │   └── types.ts            # TypeScript types for protocol
│   │   └── views/
│   │       ├── Dashboard.vue       # Main status view
│   │       └── Settings.vue        # Configuration view
│   └── public/                     # Static assets
│
└── docs/
    ├── protocol.md                 # WebUSB message format spec
    └── pairing.md                  # End-user pairing guide
```

The repository is split into `firmware/` and `webui/` because the two halves have entirely different toolchains (CMake + GCC vs. npm + Vite), different release cadences, and benefit from independent CI workflows. A shared `docs/protocol.md` documents the binary message format used over the WebUSB endpoint, which both halves must agree on.

## 8. Configuration Channel

### 8.1 During Development

CDC serial provides immediate `printf` debug output and accepts simple text commands (`pair`, `forget`, `status`, `dump`). Any serial terminal works.

### 8.2 For End Users

The WebUSB interface is paired with a Vue 3 single-page app that hosts the user-facing configuration experience. Workflow:

1. User plugs the bridge into the PC
2. User opens the hosted config page in Chrome or Edge
3. The app calls `navigator.usb.requestDevice()` with the bridge's vendor ID filter
4. User selects the bridge in the browser's device picker
5. Vue's reactive store subscribes to the WebUSB endpoint and renders live controller state
6. User configures bindings, profiles, or mirroring via the UI; changes are written back over WebUSB and persisted to the bridge's flash

No drivers, no native app, no app store. The Vite-built static bundle is hostable anywhere (GitHub Pages is the suggested default).

**Planned UI surfaces (incremental release):**

- **v1 — Dashboard.** Live connection status per controller, battery levels, signal indicator, basic diagnostics. Read-only.
- **v2 — Remapping.** Per-controller binding editor. User selects a virtual gamepad button on screen, then presses the physical button they want to bind to it. Bindings save to a profile on the device.
- **v3 — Mirroring.** Assign a single physical controller to drive multiple virtual gamepad slots simultaneously (useful for testing or co-op patterns), or merge two Joy-Cons into a unified virtual gamepad.
- **v4 — Profile management.** Named profiles, per-application autoswitching (where the host can signal which app is active), import/export as JSON.

Each surface is a separate Vue component that mounts as the corresponding feature ships in firmware, so the UI can roll out incrementally without ever shipping a broken or half-functional dashboard.

### 8.3 Why Not WiFi for Configuration

WiFi on the CYW43439 shares its single antenna with Bluetooth. Active WiFi traffic — even idle scanning — adds measurable jitter to BT input reports, degrading the latency targets in Section 6. USB-side configuration avoids this entirely and is faster (USB Full-Speed is 12 Mbps with predictable scheduling), more reliable, and works without network setup.

WiFi may be added in a future revision as an opt-in mode triggered by a button-press, where it runs as a SoftAP for untethered configuration when the user explicitly requests it. It will not run concurrently with active gameplay.

## 9. Implementation Order

Recommended build sequence for incremental, testable progress.

**Firmware phase:**

1. **Toolchain validation** — Build and flash Bluepad32's `examples/pico_w` unmodified. Confirm serial output works.
2. **Single-controller pairing** — Pair one Joy-Con, verify input events arrive in firmware.
3. **Multi-controller pairing** — Pair all four target controllers. Verify all four show up with distinct indices and concurrent input.
4. **USB HID gamepad (single)** — Add TinyUSB, expose one XInput-style gamepad interface, forward controller 0 to it. Verify PC sees the gamepad in `joy.cpl` and Steam.
5. **USB HID gamepad (composite x4)** — Expand descriptor to four gamepad interfaces, route each Bluepad32 controller index to its matching virtual gamepad.
6. **Mouse + wake-on-power** — Add the mouse interface, send a 1-pixel relative move at boot, enable remote wake in descriptor, test PC wake from sleep.
7. **MAC bond persistence** — Save bonded controller MACs to flash, auto-reconnect on power-up.
8. **CDC debug serial** — Add the CDC interface, redirect firmware logs to it.
9. **WebUSB vendor interface** — Add the vendor interface and MS OS 2.0 descriptor, define a minimal binary protocol for status reads, verify a hand-rolled `navigator.usb` script can read controller state.

**Web app phase:**

10. **Vite + Vue 3 scaffold** — `npm create vite@latest webui -- --template vue-ts`. Establish the project, set up linting, push to repo. Verify a blank `App.vue` builds and serves.
11. **WebUSB composable** — Build `useWebUsb.ts` as the single source of truth for device connection, framing, and reconnection. Cover error paths (device unplugged mid-session, browser denied permission).
12. **Dashboard v1** — Live controller status, battery, signal strength, connection indicator. Pure read-only. This is the minimum that proves end-to-end value.
13. **Live input visualization** — On-screen rendering of stick positions and button states, updated at the WebUSB report rate. Validates that the protocol can handle real-time data.
14. **Remap editor (v2)** — Drag-to-bind UI for per-controller button mapping. Requires firmware-side remap table storage and a protocol message to write it. Single profile only at this stage.
15. **Controller mirroring (v3)** — UI and firmware logic for routing one physical controller to multiple virtual slots, and for merging Joy-Con pairs.
16. **Profile management (v4)** — Multiple named profiles, JSON import/export, per-application autoswitching if a host signaling mechanism is added.

Each step produces a working artifact. If a later step breaks something, the previous step is a known-good rollback point. Firmware steps 1–9 must complete before any web app step is useful; within the web app phase, each step is releasable on its own.

## 10. Open Questions / Future Work

- **Two-bridge mode for 5–8 controllers.** Two Pico 2 W units on separate USB ports, each handling 4 controllers. Requires no firmware coordination between units; the PC simply sees 8 HID gamepads. Worth validating once 4-controller mode is stable.
- **Gyro and rumble passthrough.** Switch controllers expose IMU data and HD rumble. Steam Input can consume gyro from any controller that exposes it via HID. Rumble passthrough requires bidirectional HID output reports back to Bluepad32 and onward to the controller, which Bluepad32 supports for some controllers.
- **Battery reporting.** Joy-Cons and Pro Controllers report battery in their input reports. This data can be exposed both to the WebUSB UI and as a HID battery system feature for the host PC to display.
- **Suspend behavior.** When the PC sleeps, the bridge should park controllers gracefully (LED off, low-power BT) rather than aggressively reconnecting. Needs investigation of CYW43439 deep sleep options.

## 11. References

- Bluepad32 documentation: https://bluepad32.readthedocs.io/
- Bluepad32 Pico W example: https://github.com/ricardoquesada/bluepad32/tree/main/examples/pico_w
- TinyUSB device examples (especially `hid_composite`, `cdc_msc_hid`, `webusb_serial`): https://github.com/hathach/tinyusb/tree/master/examples/device
- pico-sdk documentation: https://www.raspberrypi.com/documentation/pico-sdk/
- BTstack documentation: https://bluekitchen-gmbh.com/btstack/
- WebUSB specification: https://wicg.github.io/webusb/
- MS OS 2.0 descriptor reference: https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-os-2-0-descriptors-specification
