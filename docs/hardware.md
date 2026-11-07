# Hardware proposal and validation

**Status:** firmware uses the proposed GP6/GP7 and GP0/GP1 mapping; no physical
host-port circuit, VBUS protection, or signal integrity has been verified.
This is not a released schematic. Last reviewed: 2026-11-07.

## XIAO RP2040 pin candidates

The standard XIAO RP2040 exposes D0-D10; Seeed maps D4/D5 to GP6/GP7 and
D6/D7 to GP0/GP1. Pico-PIO-USB's default pinout expects DM = DP + 1. Therefore
the following candidate uses exposed adjacent GPIOs and separates USB data
from UART:

| Function | XIAO pad | RP2040 GPIO | Notes |
|---|---|---:|---|
| PIO USB D+ | D4 | GP6 | Candidate only; configure PIO USB DP pin as 6 |
| PIO USB D− | D5 | GP7 | Candidate only; matches default DPDM adjacency |
| UART TX | D6 / TX | GP0 | TORII TX connects to receiving host RX |
| UART RX | D7 / RX | GP1 | Optional in the one-way MVP; host TX connects here |
| Ground | GND | — | Common ground with host |
| Downstream USB power | 5V / VBUS | — | Candidate upstream source only through a protected switch |

Keep the XIAO USB-C port for programming/debugging. Do not connect the
USB-A D+/D− lines to the board's native USB D+/D−. The Seeed pin map lists
5V as VBUS and warns that general-purpose I/O is 3.3 V; never apply USB 5 V
to RP2040 GPIO or UART.

Before layout, verify the selected GPIOs against the exact XIAO revision,
Pico-PIO-USB pinout/configuration, UART mux, and any board onboard LED or
other pin use. A XIAO RP2040 Plus has additional pads/features and is not the
baseline assumed here.

## Suggested USB-A host port circuit

For a breadboard-oriented connection drawing and power-up checklist, see
[XIAO RP2040 breadboard wiring](breadboard-wiring.md).

```text
XIAO 5V/VBUS source
       |
 current-limited load switch / eFuse
       |---- USB-A VBUS
       `---- controlled enable (optional MCU GPIO)

GP6 --- series resistor --- USB-A D+
GP7 --- series resistor --- USB-A D-
GND ----------------------- USB-A GND / shield strategy
```

- Use a USB downstream-port power switch/eFuse with specified current limit,
  thermal protection, and reverse-current blocking (or otherwise prove the
  reverse-power path is prevented). Set the limit from the intended USB
  device/current budget and upstream supply; do not infer a safe value from
  the XIAO 5V pin alone.
- The XIAO's 5V/VBUS pin is not a guaranteed dedicated downstream supply.
  When XIAO is powered through USB-C, a keyboard fed from this pin draws from
  that upstream supply. Measure the combined board + keyboard current and
  ensure it stays within the USB-C source's available current budget. The
  load switch must also prevent reverse current for the intended power
  sequence. Avoid connecting independent 5V sources without a power-path
  design.
- Pico-PIO-USB recommends 22 ohm series resistors as beneficial. Confirm
  resistor placement/value against the pinned upstream hardware guidance and
  signal measurements before freezing a schematic. Place a low-capacitance
  USB-rated ESD protector near the connector; verify its capacitance and
  topology are suitable for D+/D−.
- Keep D+/D− short, adjacent, and over a continuous ground reference. The
  GPIO-based PIO implementation is a USB signaling design, not a claim of
  certified USB compliance. Validate with real devices and inspect waveforms
  if enumeration is marginal.
- Connector shield/chassis bonding, VBUS capacitance/inrush, and enclosure
  ESD strategy remain schematic/layout decisions.
- The current CDC debug descriptor uses TinyUSB's example VID/PID
  `0xCAFE:0x4001` for development only. Replace it with an authorized project
  VID/PID before a public firmware release; this ID is not a production
  assignment.

USB 2.0 bus-powered devices may initially draw up to one unit load before
configuration; permitted configured current depends on the applicable USB
version and the device descriptor. The first test should use a low-power
keyboard and a measured/protected source, not a bus-powered hub or
power-hungry device.

## UART wiring

- Logic: 3.3 V, common GND, UART 115200 baud, 8 data bits, no parity, 1 stop
  bit (8N1) for the initial protocol.
- Cross the data lines: TORII TX -> host RX; TORII RX <- host TX if used.
- The initial MVP transmits events/state from TORII; RX is reserved and has
  no command protocol yet. Never connect UART directly to RS-232 voltage
  levels or a 5 V-only input.
- Measure actual baud tolerance and frame loss with the selected host UART
  receiver before considering higher rates.

## Phase 0 bench procedure

1. Build a pin-mapping prototype without attaching USB VBUS to the keyboard.
   Verify GP6/GP7 mapping and no conflict with native USB-C programming.
2. Add the protected VBUS switch. Verify polarity, no reverse current with
   each power sequence (USB-C only, USB-A device inserted/removed, power off),
   current limit, switch temperature, and upstream source current.
3. Run the upstream PIO USB Host HID-to-CDC example on XIAO as a baseline;
   then route equivalent report data to the proposed hardware UART.
4. Test keyboard connect/disconnect and repeated key reports at the proposed
   baud; measure USB-report-to-UART latency, supply droop, and signal quality.
5. Record board revision, exact wiring/component values, dependency commits,
   keyboard VID/PID, current readings, and failures. Only then promote this
   proposal to a validated schematic.

## Source references

- [Seeed XIAO RP2040 hardware/pin map](https://wiki.seeedstudio.com/XIAO-RP2040/)
- [Pico-PIO-USB README and resource notes](https://github.com/sekigon-gonnoc/Pico-PIO-USB)
- [Pico-PIO-USB configuration, including DP/DM pin adjacency](https://github.com/sekigon-gonnoc/Pico-PIO-USB/blob/main/src/pio_usb_configuration.h)
