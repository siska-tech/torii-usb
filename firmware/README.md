# Firmware

The current firmware builds for the XIAO RP2040 using Pico SDK 2.2.0,
TinyUSB 0.18.0, and Pico-PIO-USB 0.7.3. It configures PIO USB D+/D− on GP6/7,
enumerates a boot keyboard through TinyUSB Host, emits key-state snapshots
over UART0 GP0/1 at 115200 baud, forwards reports from one additional
non-keyboard HID interface as raw protocol frames, and provides a TinyUSB CDC
debug console on the native USB port. A device-specific TinyUSB class driver
also recognizes the Razer Onza Tournament Edition (`1689:FD00`), opens its
vendor-specific interrupt endpoints, sends a player-1 LED command, and
forwards 20-byte Xbox 360-compatible input reports as `XBOX360_REPORT`.
It does not normalize buttons/axes or support other XInput devices.

The version 1 protocol encoder is in `src/protocol.c`. UART transmission uses
a bounded packet queue and does not wait for the UART to become writable in
the USB task. The Onza driver was verified on the connected XIAO: diagnostics showed
interface 0 with IN `0x81` and OUT `0x02`, a completed LED transfer, and a
20-byte input report. Three-byte transfers were also observed and are
currently ignored. Button/axis interpretation and the UART output have not
been verified on hardware. USB-A power behavior remains unverified.

To inspect diagnostics, open COM9 at 115200 8N1 with DTR enabled. The baud
setting is conventional for serial-terminal compatibility; native USB CDC
does not use that physical bit rate.

Build instructions are in the [root README](../README.md); pinned dependency
revisions are in [docs/dependencies.md](../docs/dependencies.md).
