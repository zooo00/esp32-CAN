# GSA-CAN — spec (barebones)

## Goal
Passively read the CAN bus of a BMW R1200GS Adventure 2010 (K255) with Node A and stream the raw frames over BLE to Node B, which prints them on the Mac for reverse engineering.
Later (Phase 2, not now): Node B becomes a display unit on the bike.

## Scope now
- **Node A**: frame source → queue → BLE notifications. One firmware for desk and bike:
  - TWAI, listen-only, 500 kbit/s, always running.
  - Stub: fake frames so the A→B link can be tested without the motorcycle. Switched with `stub on|off` on the USB console, off after every boot. While on, real frames are discarded.
- **Node B**: BLE client → one line per frame on USB serial.
- **Mac:** `tools/decode.py` labels frames using the RealDash XML in `reference/`.
- Nothing else: no decoding on the ESP32s, no display, no filesystem, no WiFi.

## Hardware
- Node A: Waveshare ESP32-S3-RS485-CAN (isolated CAN, 7–36 V). CAN TX = GPIO15, RX = GPIO16 (Kconfig).
- Node B: ESP32-S3-DevKitC N16R8 on USB.
- Desk test: Node A runs on the Waveshare or any ESP32-S3 board.

## Toolchain
ESP-IDF v5.5.x native, NimBLE, console on USB-Serial-JTAG. `make a | b | build [PORT=...]`.

## Wire format
See `components/gsa_common/include/gsa_proto.h`. Notification = `[version][source]` + N × 17-byte frames (`ts_ms`, `id` with EXT/RTR flag bits, `dlc`, `d[8]`). Source is REAL or STUB, and Node B prints `# source=...` so stub data is always identifiable.

## Safety
- Node A never transmits on the bike's bus: `TWAI_MODE_LISTEN_ONLY`, and `#pragma GCC poison` blocks transmitting modes and `twai_transmit()` at compile time.
- The stub can't be on by accident: it is off after every boot and can only be switched on over USB. Stub frames are always labelled STUB, and a batch never mixes REAL and STUB frames.

## Connecting to the bike

### Step 0: find out where CAN is (do this first)
A RealDash forum user found **no CAN on the 10-pin diagnostic connector** of his 2010 R1200GS. Measure the socket under the seat:
- Pins 7/9: both ≈ 2.5 V with ignition on, ≈ 60 Ω between them with the battery disconnected → CAN is there, use Option 1.
- Otherwise use Option 2.
- Other pins: 1 = K-line, 4 = GND, 6 = +12 V permanent, 10 = +12 V switched.

### Option 1: adapter cable
BMW 10-pin → OBD2 adapter with the OBD2 end cut off. 10 → +12 V (via 1 A fuse), 4 → GND, 7 → CAN-H, 9 → CAN-L. Check the adapter's pinout with a multimeter.

### Option 2: splice into the harness
- Find the CAN-H/CAN-L twisted pair using the K25/K255 wiring diagram (instrument cluster, ZFE, or the rear alarm connector).
- Solder + heat shrink or Posi-Taps. Stub < 30 cm, twisted. 1 A fuse on the +12 V tap.
- Identify by measurement: CAN-H/CAN-L ≈ 2.5 V idle, ≈ 60 Ω between them with the battery disconnected; switched +12 V only with the ignition on.

### Always
- **Termination jumper on Node A OFF.**
- Power from switched +12 V, so there is no battery drain.

## Bus facts
CAN 2.0A, 500 kbit/s, periodic broadcast frames. Diagnostic requests (0x6F1/0x660) and K-line are out of scope.

## Signal references (for later decoding)
- https://github.com/janimm/RealDash-extras/tree/master/RealDash-CAN/XML-files/BMW
- https://forum.realdash.net/t/bmw-r1200gs-2010-can-xml-file-creation-and-questions/7473
- The K255 may differ from the K25 in fuel level (33 L tank), speed scaling and optional equipment.
