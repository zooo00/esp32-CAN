# GSA-CAN

Reads the CAN bus of a 2010 BMW R1200GS Adventure (K255) and sends the raw frames over BLE to a receiver that prints them on the Mac.

- **Node A** (Waveshare ESP32-S3-RS485-CAN, on the bike): reads CAN in listen-only mode and sends frames over BLE.
- **Node B** (ESP32-S3-DevKitC, on the Mac's USB): receives the frames and prints one line per frame.

Background, wiring and bike notes: [SPEC.md](SPEC.md).

## Setup
ESP-IDF v5.5.x, native:
```sh
git clone -b release/v5.5 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf
~/esp/esp-idf/install.sh esp32s3
. ~/esp/esp-idf/export.sh      # in every new shell
```

## Desk test (no bike)
Two boards, two terminals:
```sh
make a PORT=/dev/cu.usbmodemAAAA   # Node A, then type:  stub on
make b PORT=/dev/cu.usbmodemBBBB   # Node B, prints the frames
```
Node A is one firmware for desk and bike. CAN is always read (listen-only). `stub on` on Node A's USB console replaces the real frames with fake ones, and `stub off` switches back. The stub is **off after every boot**, so fake data can't end up on the bike by accident.

Node B output:
```
# source=STUB
# link up
52311 100 [8] 2A 01 B8 0F 00 00 00 5C
52311 200 [8] 95 00 B8 0F 00 00 00 E1
```
Lines starting with `#` are status messages. Everything else is `<ts_ms> <ID hex> [<dlc>] <bytes>`. `ts_ms` is Node A's clock.
Stub frames are sent with source STUB. The stub sends 4 made-up IDs (0x100, 0x200, 0x300, 0x3F0) at 10/20/100/1000 ms. They are **not** BMW signals.

Log to a file without the monitor:
```sh
(stty raw -echo; cat) < /dev/cu.usbmodemBBBB | tee can_$(date +%H%M).log
```

## Understanding the frames
`reference/BMW_R1200GS_K25_CAN.xml` is the RealDash definition for the K25, from [RealDash-extras](https://github.com/janimm/RealDash-extras/tree/master/RealDash-CAN/XML-files/BMW). `tools/decode.py` (Python 3, stdlib only) adds its signals to Node B's lines:
```sh
(stty raw -echo; cat) < /dev/cu.usbmodemBBBB | python3 tools/decode.py
# 1010 2A8 [8] 00 00 A0 02 00 00 00 00  | Speed=42.0km/h

python3 tools/decode.py --changes can_1432.log     # only print values that change
# 1070 2BC Gear engaged: 3 -> 2
```
`--changes` is the reverse-engineering mode: operate one control at a time and watch what changes. IDs that aren't in the XML are passed through unchanged.
The XML covers the K25 and is **unverified on the K255**. Fuel level (33 L tank), speed and optional equipment may differ. RPM in the XML is rounded to 50 rpm; the raw value is `V / 4`. To correct or add a signal, edit the XML.

## On the bike
```sh
make a PORT=/dev/cu.usbmodemAAAA   # same firmware as at the desk
```
- Measure first where CAN is available (Step 0 in SPEC.md).
- The **120 Ω termination jumper on Node A must be OFF**. The bike's bus is already terminated.
- Node A never transmits. `source_twai.c` uses `TWAI_MODE_LISTEN_ONLY`, and a `#pragma GCC poison` makes any transmitting mode or `twai_transmit()` a compile error.
- Node A's USB console prints TWAI state and bus errors once per second. Wrong wiring or no CAN on the connector shows up there.

## Layout
```
components/gsa_common/   wire format + UUIDs (gsa_proto.h)
node_a/                  BLE server; source_twai.c (CAN) + source_stub.c (fake frames, console switch)
node_b/                  BLE client -> USB serial
reference/               RealDash XML (K25 signal definitions)
tools/decode.py          adds signal names/values to Node B's output
```

## Wire format
Each BLE notification is a 2-byte header `[version][source]` followed by up to 14 frames of 17 bytes:
`uint32 ts_ms, uint32 id, uint8 dlc, uint8 d[8]`, little-endian. Bit 31 of `id` = extended ID, bit 30 = RTR.
Node A sends a batch when it is full or 20 ms after its first frame.

## License
MIT, see [LICENSE](LICENSE). `reference/BMW_R1200GS_K25_CAN.xml` comes from [RealDash-extras](https://github.com/janimm/RealDash-extras) and is public domain (Unlicense).
