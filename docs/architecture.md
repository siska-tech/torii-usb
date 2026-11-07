# Architecture and Phase 1 plan

**Status:** firmware builds and is flashed to the connected XIAO RP2040; its
native CDC interface enumerates. No USB keyboard E2E test has been completed.
Last reviewed: 2026-11-07.

## Feasibility assessment

The concept is technically plausible. The RP2040 has PIO, DMA, two cores, and
an exposed GPIO set sufficient for a separate PIO USB data pair and UART.
Pico-PIO-USB reports USB Full-Speed and Low-Speed Host support, and its example
uses TinyUSB Host for HID while TinyUSB Device provides CDC on the RP2040's
native USB port. This is strong prior art, not proof that the XIAO pinout,
power path, build configuration, or target keyboards work without changes.

Upstream Pico-PIO-USB reports use of one PIO block, three state machines, all
32 PIO instructions, two GPIOs, about 15 KB ROM/RAM, and a 1 ms repeating
timer for Host. Its default configuration uses DMA channel 0 for TX. Confirm
the exact resource use and conflicts against the pinned source revision during
Phase 0; do not assume every RP2040 PIO/DMA resource remains available.

## Module boundaries

```text
firmware/
  src/
    main.c              startup and core service loops
    usb_host.c          TinyUSB/Pico-PIO-USB integration and HID lifecycle
    usb_descriptors.c   native USB CDC debug descriptors
    protocol.c          framing, CRC, and host state recovery
    uart_transport.c    hardware UART and bounded packet queue
  include/
    torii_protocol.h
  tests/
    (host-buildable protocol tests live in ../tests)
host/
  c/                    CMake target for the reference frame decoder
  micropython/          later UART driver
  python/               later diagnostic/test utility
```

USB Host runs on core 1 and native USB CDC/UART servicing runs on core 0, as
in the upstream Host-to-CDC example. HID callbacks copy state and enqueue
frames; UART transmission checks FIFO readiness and never blocks USB polling.
The TX ring has seven usable packet slots and reports overflows. Portable
framing/state code builds on a PC without the Pico SDK.

## Phase 1 MVP scope

- One USB device at a time; USB HID boot-protocol keyboard.
- Key state, modifiers, connection/disconnection, and error/resynchronization
  frames over 115200 8N1 UART.
- Full keyboard state snapshots (modifier byte and six Usage IDs), not
  character conversion and not key-repeat generation.
- One other HID interface can be forwarded as raw reports up to 32 bytes;
  report descriptors and gamepad controls are not parsed.
- C host-side decoder and PC unit tests.
- No normalized mouse/gamepad support, hub, arbitrary HID report parser,
  MicroPython driver, or KAGURA-specific dependency in the MVP.

Boot keyboard reports have a six-key array and do not provide NKRO. Some
keyboards may not expose/use boot protocol; record compatibility rather than
claiming all USB keyboards work.

## Phase 1 implementation tasks and acceptance

| Task | Acceptance condition |
|---|---|
| Pin toolchain and dependencies | **Done:** XIAO UF2 builds with pinned SDK/TinyUSB/Pico-PIO-USB revisions; dependency licenses recorded. |
| Bring up PIO USB Host | **Partially verified:** Onza (`1689:FD00`) enumerates through the custom driver; LED output and a 20-byte input report were observed. Keyboard enumeration and reconnects remain untested. |
| Implement keyboard state | **Implemented, not hardware-verified:** modifier/Usage ID snapshots, rollover, connect/disconnect and periodic state refresh; the C host API derives press/release edges independent of key-slot order. |
| Implement protocol | **Done on PC:** COBS, version/length checks, CRC-16, sequence tracking and delimiter resynchronization have tests. |
| Implement bounded UART path | **Implemented, not hardware-verified:** non-blocking UART service, bounded packet queue, drop counter and periodic latest-state refresh. |
| Implement C decoder | **Done on PC:** frame validation, sequence-gap reporting, disconnect clearing and stale-state expiry. |
| Run PC tests | **Done:** golden CRC/frame vectors, malformed frames, resynchronization, state transitions, sequence wrap, rollover and timeout. |
| Verify hardware E2E | At least three boot keyboards from two vendors pass press/release/modifier/multikey checks; 10,000 scripted transitions with dwell longer than the device report interval have no missing or incorrect state transition under nominal UART load; forced UART stalls recover the current state without a stuck key. Report p95 input-to-UART latency <=10 ms and p99 <=20 ms in the documented test setup. |
| Validate operating conditions | Repeated unplug/replug and a 24-hour run show no unrecovered USB state; VBUS current/temperature and UART-stall recovery are measured and documented. |

The latency numbers are initial **targets**, not measurements or guarantees.
Measure from the HID report callback to the end of the corresponding UART
frame (for example with debug GPIO markers and a logic analyzer). Record test
firmware, keyboard models, UART adapter, baud, report interval, and measurement
method with results. If a target is missed, report the measured result and
investigate before changing the acceptance target.

## Key-state loss recovery

Every keyboard update is a complete state snapshot. The receiving decoder
compares it with its previous state to derive key transitions; it never
assumes a missing release can be inferred from an event-only stream. Attach
and detach force state initialization/reset. A sequence discontinuity is
reported to the host and the next valid snapshot replaces its state.

`torii_host_state_diff()` derives key and modifier edges by comparing key sets,
so a keyboard changing the order of its six HID key slots does not create
false transitions. Use `torii_host_state_expire()` with the host's monotonic
millisecond clock to release stale keys and clear stale connection state if
periodic frames stop. A later keyboard snapshot re-establishes the connection
if a lifecycle packet was lost.

Snapshots repair the current held-key state, but cannot recreate a brief
press-and-release that occurred entirely while the UART path was stalled.
The firmware therefore uses a bounded non-blocking queue, counts drops, and
prioritizes the newest state for recovery. This limitation must be visible to
the host API and tested under stopped-host conditions.

## Evidence and open questions

- Pico-PIO-USB upstream README documents FS/LS Host, resource use, and a
  TinyUSB HID-to-native-CDC example:
  <https://github.com/sekigon-gonnoc/Pico-PIO-USB>
- Its Host example configures the PIO root port before `tuh_init()`, runs
  `tuh_task()`, and receives HID mount/report/unmount callbacks:
  <https://github.com/sekigon-gonnoc/Pico-PIO-USB/blob/main/examples/host_hid_to_device_cdc/host_hid_to_device_cdc.c>
- TinyUSB documents Host HID support (including keyboard/mouse/generic) and
  is MIT licensed:
  <https://github.com/hathach/tinyusb>
- XIAO RP2040 pin map and electrical notes:
  <https://wiki.seeedstudio.com/XIAO-RP2040/>
- Dependency versions and licenses are listed in
  [docs/dependencies.md](dependencies.md).
- Not yet established: tested XIAO pin pair, actual keyboard compatibility,
  VBUS source/current budget, ESD/signal integrity, latency, throughput, and
  sustained stability. These need hardware and pinned-source verification.
