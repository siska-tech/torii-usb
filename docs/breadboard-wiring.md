# XIAO RP2040 breadboard wiring

**Status:** prototype wiring proposal; the USB-A host port and VBUS circuit
have not yet been electrically verified. Last reviewed: 2026-11-07.

This is for a **single low-power USB keyboard** on a short bench prototype.
It is not a production schematic or a guarantee that USB Full-Speed will be
reliable on a solderless breadboard.

## Wiring diagram

```text
PC USB power/debug ---> USB-C
                         +-------------------+
                         | XIAO RP2040       |
                         |                   |
                         | D4 / GP6 ---------+---[22 ohm]---- USB-A D+ (pin 3)
                         | D5 / GP7 ---------+---[22 ohm]---- USB-A D- (pin 2)
                         | GND -------------+--------------- USB-A GND (pin 4)
                         | D6 / GP0 UART TX -+----> UART adapter RX (optional)
                         | D7 / GP1 UART RX -+<---- UART adapter TX (unused)
                         | 5V / VBUS --------+---[USB load switch]-- USB-A VBUS (pin 1)
                         +-------------------+
                           UART signals are 3.3 V only.

  USB keyboard plugs into the USB-A receptacle.
```

The XIAO 5V/VBUS pad is used as the source for the protected USB-A VBUS.
This assumes the XIAO is powered through USB-C. The USB-C source must be able
to supply the XIAO and keyboard together; its available current is not
guaranteed by the 5V pad.

The firmware configures GP6 as D+ and GP7 as D− (Pico-PIO-USB default
adjacent-DM mapping). Each data line has a separate 22 ohm series resistor.
Put the resistors close to the XIAO GPIO pins. Do not connect the USB-A data
lines to the XIAO USB-C D+/D− pins.

### USB-A receptacle contacts

| USB 2.0 Type-A contact | Signal | Connect to |
|---:|---|---|
| 1 | VBUS | XIAO 5V/VBUS pad through a current-limited, reverse-blocking load switch |
| 2 | D− | XIAO D5 / GP7 through 22 ohm |
| 3 | D+ | XIAO D4 / GP6 through 22 ohm |
| 4 | GND | XIAO GND |
| Shell | Shield | Leave unconnected for this first functional prototype; handle in a later PCB/ESD design |

Use the **connector breakout's signal labels or its datasheet** to identify
contacts. The apparent left/right order changes depending on whether the
receptacle is viewed from the mating face or solder side.

## Connections at a glance

| From | To | Notes |
|---|---|---|
| XIAO D4 / GP6 | USB-A D+ | In series with 22 ohm |
| XIAO D5 / GP7 | USB-A D− | In series with 22 ohm |
| XIAO GND | USB-A GND | Keep this ground return short |
| XIAO 5V / VBUS | USB-A VBUS | Through a current-limited, reverse-blocking USB load switch |
| XIAO D6 / GP0 (UART TX) | UART adapter RX | Optional protocol capture; 3.3 V UART, 115200 8N1 |
| XIAO D7 / GP1 (UART RX) | UART adapter TX | Not used by current firmware; leave disconnected |

Windows COM9 is the XIAO's native USB CDC **debug console**, not the GP0 UART
protocol output. A separate 3.3 V USB-UART adapter is needed to capture the
binary protocol. Do not connect the adapter's 5 V output or use a 5 V/RS-232
logic-level interface.

## Power and safety checks

1. Keep the keyboard unplugged. Power the XIAO over USB-C for programming and
   debugging. Connect the XIAO 5V/VBUS pad to USB-A VBUS through a USB load
   switch/eFuse with an appropriate current limit and reverse-current
   blocking.
2. Use a USB-C power source that can supply both the XIAO and the keyboard.
   The XIAO 5V pad is connected to its USB VBUS rail; it is not a separately
   regulated or guaranteed-current host supply. **Do not connect another 5 V
   source to the XIAO 5V/VBUS pad or USB-A VBUS.**
3. Before plugging in a keyboard, measure at the USB-A receptacle: VBUS to
   GND must have correct polarity and be near 5 V; D+/D− must not be shorted
   to VBUS or each other. Check the load switch's current limit against the
   USB-C source's available current budget and verify reverse current is
   blocked for the intended power sequence.
4. Start with one low-power keyboard. Monitor the USB-C source current and
   USB-A VBUS voltage while connecting it. If the current limit trips,
   voltage droops, or anything heats, disconnect power and investigate.
5. Do not attach a hub or power-hungry device. Do not add USB pull-up or
   pull-down resistors unless the pinned Pico-PIO-USB hardware guidance
   specifically requires them.

If a suitable current-limited, reverse-blocking load switch is not available,
stop at the unpowered signal-wiring check; do not plug in the keyboard. The
XIAO 5V pad is the board's USB VBUS rail, not a protected host-port supply.

## Breadboard limitations

USB Full-Speed uses 12 Mbit/s signaling. Long jumper wires, loose contacts,
stubs, and separated D+/D− paths may prevent enumeration or cause intermittent
errors. Keep D+/D− together and as short as possible; use a USB-A receptacle
breakout rather than loose connector wires. A failure on a breadboard does
not by itself show that the firmware or USB Host implementation is defective.
Successful enumeration on a breadboard also does not establish USB compliance,
ESD protection, or production reliability.

The USB-A connector shell is intentionally not tied in this first wiring
sketch. A final schematic must define shield/chassis bonding, ESD protection,
VBUS capacitance/inrush, and the protected power path.
