# Implementation audit

**Reviewed:** 2026-11-07
**Scope:** repository at the Audit / Test Baseline change. The latest recorded
Onza CDC session is prior hardware evidence; the board was not connected during
this source audit. No keyboard or physical UART test is claimed.

## Maturity summary

| Area | Status | Evidence and limits |
|---|---|---|
| USB Host stack and dependencies | Implemented and software-tested | Firmware links TinyUSB Host and Pico-PIO-USB and configures the PIO root port before `tuh_init()` in [`firmware/CMakeLists.txt`](../firmware/CMakeLists.txt), [`tusb_config.h`](../firmware/src/tusb_config.h), and [`usb_host.c`](../firmware/src/usb_host.c#L459). The XIAO firmware build succeeds. SDK 2.2.0, TinyUSB 0.18.0, and Pico-PIO-USB 0.7.3 are pinned/documented in [`dependencies.md`](dependencies.md). Exact SRAM/flash use and runtime PIO/DMA contention are not measured. |
| USB enumeration | Implemented and hardware-verified for one device | The recorded CDC log reported Onza VID/PID `1689:FD00` configured and its custom class opened interface 0. Generic mount/unmount reporting is in [`usb_host.c`](../firmware/src/usb_host.c#L423). This does not demonstrate broad device compatibility or failure-path recovery. |
| Descriptor and endpoint handling | Partial | TinyUSB parses standard configuration descriptors. The Onza-specific class independently scans interface 0 descriptors and opens its interrupt endpoints in [`xpad_host.c`](../firmware/src/xpad_host.c#L72). That parser has no host-side malformed-descriptor tests; fuzzing, endpoint-stall handling, and enumeration-failure counters are absent. |
| HID Boot Keyboard | Implemented but hardware-unverified | The pinned TinyUSB Host HID driver requests Boot Protocol for boot-subclass keyboard interfaces; TORII accepts only the keyboard interface protocol and converts the 8-byte boot report to a complete state snapshot in [`usb_host.c`](../firmware/src/usb_host.c#L261) and [`usb_host.c`](../firmware/src/usb_host.c#L337). Modifier/key-set diffs, rollover, disconnect clearing, sequence gaps, and timeout are covered in PC tests. No physical keyboard test is recorded. |
| HID Report Protocol | Partial | TinyUSB fetches report descriptors, but TORII does not parse them for normalization. One non-keyboard HID interface is passed through as a raw `HID_REPORT` up to 32 bytes. NKRO keyboards, report IDs/layouts, and report-protocol keyboard parsing are not implemented. |
| Mouse | Partial | A recognized non-keyboard HID interface can be forwarded as a raw report. There is no normalized relative-motion, wheel, or button event model, nor mouse hardware test. |
| Standard HID gamepad | Partial | Raw reports may use the generic HID path; descriptor parsing and normalized controls are absent. No general gamepad verification is recorded. |
| Xbox / Onza | Partial; hardware-tested transport only | The recorded CDC output shows interface 0 at IN `0x81` / OUT `0x02`, a completed player-1 LED transfer, a 20-byte report, and recurring 3-byte transfers. The targeted driver only forwards 20-byte reports as `XBOX360_REPORT` in [`xpad_host.c`](../firmware/src/xpad_host.c#L208). Button, d-pad, trigger, and stick interpretation are missing. This is not general Xbox support. |
| UART protocol core | Implemented and software-tested | Version 1 uses COBS, CRC-16/CCITT-FALSE, delimiter resynchronization, a 16-bit sequence, and complete keyboard snapshots in [`protocol.c`](../firmware/src/protocol.c) and [`protocol.md`](protocol.md). `tests/protocol_test.c` covers CRC vectors, round trips, malformed frames, resynchronization, state diffs, sequence gaps/wrap, rollover, and expiry. |
| UART hardware transport | Implemented but unverified | UART0 GP0/GP1 at 115200 8N1 with a bounded 7-packet usable queue is in [`uart_transport.c`](../firmware/src/uart_transport.c). The service writes only while the FIFO is ready, and queue overflow is counted. No physical UART receiver, saturation test, or throughput/latency measurement is recorded. |
| Loss/disconnect recovery | Implemented in protocol core; hardware-unverified | Keyboard snapshots repeat every 250 ms; disconnect emits a lifecycle frame; the C host API applies sequence gaps and supports timeout clearing. Unit tests cover the host state logic. Firmware queue behavior and physical unplug/reconnect are not tested. A press and release wholly lost during a stall cannot be reconstructed. |
| Host-side C API | Implemented and software-tested | The portable CMake target wraps the protocol core. The decoder/state API is tested on PC and has no KAGURA dependency. There is no UART platform adapter or runnable serial example yet. |
| CI | Missing before this change | This change adds host protocol tests in normal and Address/UndefinedBehavior Sanitizer configurations. Firmware cross-compilation is not yet part of CI. |
| Hardware and electrical design | Proposal only; unverified | [`hardware.md`](hardware.md) and [`breadboard-wiring.md`](breadboard-wiring.md) describe GP6/GP7 USB data, GP0/GP1 UART, series resistors, and a protected VBUS supply concept. No released schematic/BOM, measured power path, reverse-current test, ESD validation, or signal-integrity evidence exists. Do not treat the proposal as a verified circuit. |

## Known gaps and risks

- The priority completion criterion—keyboard key-down/up, modifiers,
  simultaneous keys, hold, unplug/replug, and received UART frames—has not
  been demonstrated on hardware.
- UART TX queue overflow discards packets. Periodic complete state snapshots
  can restore held state after recovery, but cannot restore a short tap lost
  during the stall. Queue saturation behavior itself has no test.
- Firmware only surfaces a TX dropped-packet count through a best-effort
  protocol error frame. Enumeration failures, transfer errors, and total
  transmitted-frame counters are not comprehensively tracked.
- Native USB CDC diagnostics are separate from binary UART. The short Onza
  reports currently generate diagnostics and are ignored; repeated messages
  can fill the bounded CDC diagnostic queue, whose overflow is silent.
- The Onza driver relies on TinyUSB's private `usbh_pvt.h` class-driver API,
  so TinyUSB upgrades require explicit compatibility review.
- PIO/DMA resource use, exact firmware SRAM/flash use, UART bandwidth margin,
  latency, and long-duration stability are not measured.
- VBUS feed, current limiting, reverse-current blocking, connector shield,
  ESD, power sequencing, and USB signal integrity remain electrical
  verification blockers. No USB 5 V is sourced from RP2040 GPIO.
- The development USB CDC descriptor uses VID/PID `CAFE:4001`; this is not a
  production VID/PID assignment.

## Recommended PR sequence

1. **Audit / Test Baseline (this PR):** record verified scope and add PC
   protocol CI without changing the protocol ABI.
2. **USB Keyboard E2E:** run the existing boot-keyboard path with a physical
   keyboard and capture both CDC state and GP0 UART frames; fix only issues
   demonstrated by that test.
3. **UART Reliability:** factor transport behavior for host testing; test
   ordering, saturation/drop reporting, state snapshots, and recovery.
4. **Disconnect / Recovery:** validate repeated physical reconnect and add
   measured diagnostics for USB lifecycle and transfer errors.
5. **Mouse Support:** define and test normalized mouse reports separately
   from the keyboard snapshot.
6. **Gamepad Separation:** keep vendor-specific adapters isolated; analyze
   real Onza report changes before defining parsed Xbox events.
7. **Hardware / OSS readiness:** freeze a protected schematic/BOM only after
   part selection and electrical verification; add platform transport
   examples and reproducible flashing steps.

## Required bench verification

Use the protected VBUS path and a low-power boot keyboard. Capture the binary
UART with a 3.3 V USB-UART adapter (XIAO GP0 TX to adapter RX, shared ground);
COM9 is native USB CDC diagnostics, not the UART stream. Verify key down/up,
left/right modifiers, multiple simultaneous keys, long holds, unplug/replug,
then repeat while the receiving side is stopped. Record board revision,
keyboard VID/PID, circuit parts and current measurements, firmware revision,
UART baud, frame sequences, and failures. Do not connect a keyboard until the
VBUS source, current limit, and reverse-current path have been checked.

Without a logic analyzer, end-to-end ordering and framing can still be checked
with a PC serial adapter and the C decoder. Precise signal-integrity and
latency characterization remain unavailable until suitable measurement
equipment is available.
