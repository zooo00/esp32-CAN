# GSA-CAN — notes for Claude sessions

Wireless CAN telemetry for the user's BMW R1200GS Adventure 2010 (K255). Node A reads the bike's CAN bus and sends raw frames over BLE. Node B receives them and prints them over USB to the Mac, for reverse engineering. See README.md for usage and SPEC.md for scope and the bike wiring notes.

## Status (2026-09-30)
- Firmware for both nodes is written and **compiles with no warnings**. It has **never run on hardware**: nothing has been flashed, and the A→B link is untested.
- `tools/decode.py` has been tested with hand-made frame lines only.
- Nothing is connected to the bike yet. "Step 0" (measuring where CAN is available, see SPEC.md) hasn't been done.
- Git: branch `main`, remote https://github.com/zooo00/esp32-CAN (public). Tags: v0.1.0 = first barebones version, v0.1.1 = + MIT license. `main` after v0.1.1 has the decode.py eval hardening (untagged).

## Git rules (the user works from several computers and AI tools)
Goal: GitHub `main` is always a working snapshot — continuing on another machine = `git pull && . ~/esp/esp-idf/export.sh`.
- Pull before starting: `git switch main && git pull` (the user has `pull.ff only` set).
- Commit small, coherent steps directly on `main`; each pushed state should compile with `make build`.
- Push after each logical step and before stopping. Stopping mid-task? Push a plain `wip:` commit rather than leave uncommitted changes.
- Never force-push, amend, or rebase commits that are already on GitHub (so later work stacks over a `wip:`, never amends it away).
- Tags for milestones only; push them explicitly: `git push origin <tag>`.
- Work branch + PR is the exception — big changes worth reviewing before merge. After merging, delete the branch.

## Next steps
1. Flash both boards at the desk: `make a PORT=…`, `make b PORT=…`. Type `stub on` in Node A's console and confirm Node B prints `# source=STUB`, `# link up` and frame lines. Also check Node A's stats line (rx/sent/drop).
2. Do Step 0 on the bike, then connect Node A (termination jumper OFF) and record logs.
3. Verify the RealDash signals on the K255 with `decode.py --changes`, one control at a time. Fix `reference/BMW_R1200GS_K25_CAN.xml` where needed.
4. Later, Phase 2: Node B becomes a display unit (board TBD, LVGL). Not started; the user wants to keep things simple until then.

## Toolchain
- Native ESP-IDF **v5.5.5** (branch `release/v5.5`) at `~/esp/esp-idf`. Every shell needs `. ~/esp/esp-idf/export.sh` first, including every Bash tool call, since shell state doesn't persist.
- `make build` compiles both nodes. `make a PORT=…` / `make b PORT=…` build, flash and open the monitor. `PORT` is required on purpose: with both boards plugged in, auto-detect can flash the wrong one.
- Each project builds into `<project>/build` with its sdkconfig at `<project>/build/sdkconfig`. Edit the `sdkconfig.defaults` files, not the generated sdkconfig.
- clangd in the editor shows "file not found" errors for IDF headers. Ignore them; `make build` is what counts.
- The user's other ESP project (`~/Dev/esp32-Bandwatch`) uses arduino-cli, not this toolchain.

## Layout
```
components/gsa_common/include/gsa_proto.h  wire format, UUIDs, source IDs (shared)
node_a/main/  main.c (init + 1 Hz stats), ble_server.c (NimBLE peripheral + batching),
              source_twai.c (CAN, listen-only), source_stub.c (fake frames), console.c ("stub on|off")
node_b/main/  main.c (print task, '#' log prefix), ble_client.c (NimBLE central)
reference/    BMW_R1200GS_K25_CAN.xml (RealDash, GitHub copy)
tools/        decode.py (stdlib only)
```

## Design decisions (and why)
- **One Node A firmware for desk and bike.** The TWAI (CAN) source always runs, listen-only. The stub is switched at runtime with `stub on|off` on the USB console, and is **off after every boot**. While it's on, real frames are discarded. Why: the user wants the desk test to run exactly the code that goes on the bike. There is deliberately **no automatic fallback** to the stub when there's no CAN traffic, because a wiring fault on the bike must show up as a fault, not as plausible fake data.
- **Node A never transmits.** It uses `TWAI_MODE_LISTEN_ONLY`, and `source_twai.c` (the only file that includes `driver/twai.h`) has `#pragma GCC poison TWAI_MODE_NORMAL TWAI_MODE_NO_ACK twai_transmit twai_transmit_v2`. Tested: using `TWAI_MODE_NORMAL` gives a compile error. Keep it that way.
- **Node A stays dumb.** It forwards raw frames only; all decoding happens on the Mac.
- **Wire format** (`gsa_proto.h`): each BLE notification is a 2-byte header `[version][source]` followed by N × 17-byte packed `frame_t` records (`ts_ms`, `id`, `dlc`, `d[8]`, little-endian). Bits 31/30 of `id` = extended ID / RTR. `source` is `GSA_SRC_REAL` or `GSA_SRC_STUB`. A batch never mixes sources: the sender flushes when the source changes. A batch is sent when full (MTU−3), after 20 ms, or on a source change.
- **Node B serial output:** frame lines are `<ts_ms> <ID hex> [<dlc>] <bytes hex>`. Everything else starts with `#`: `# source=…`, `# link up/down`, and ESP_LOG output (prefixed via `esp_log_set_vprintf`).
- **Node B finds the CCCD** (the descriptor that switches notifications on) at `val_handle + 1` instead of discovering descriptors. That's valid for Node A's NimBLE server.
- **RealDash XML:** only the GitHub copy is kept. The forum copies have nested `<!--` comments and are invalid XML. `decode.py` reads the XML directly, including signal names from the comment after each `<value>`, and converts RealDash expressions to Python. Default byte order is little-endian (the speed decodes sensibly that way; still unverified on the bike). All signals are K25 and **unverified on the K255**.

## Deliberately left out (the user asked for barebones; don't add these back unasked)
Replay source, filesystem/LittleFS, bench CAN transmitter, fault/stress scenarios, signals.yaml and a header generator, on-device decoding, pytest suite, Phase 2 display/HAL stub, BLE MAC allowlist, status records sent over BLE, WiFi.

## Known limitations (accepted)
- No BLE access control: anyone nearby can connect to Node A and read the raw frames.
- `ts_ms` is taken when Node A reads a frame from the TWAI driver's queue, not when it arrived on the bus (a few ms of skew under load).
- Node A's 1 Hz stats log interleaves with the console prompt.
- Node A's diagnostics (bus errors, drops) are only visible on its USB console, not at Node B.

## Working with this user
- Wants things simple and tidy. Barebones first; no "super extras".
- Asks for reviews and opinions directly. Give a recommendation, then build it.
