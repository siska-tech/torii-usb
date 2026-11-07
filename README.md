# TORII

**Tiny Open Router for Input Interfaces**

TORII is a proposed USB HID-to-UART input controller for embedded systems that
do not have a practical USB Host interface. The first target is a Seeed Studio
XIAO RP2040 with an external USB Type-A host port and a 3.3 V UART.

> **Status: early implementation.** The version 1 COBS/CRC protocol and XIAO
> RP2040 PIO USB Host firmware build and run on the connected board. The
> Razer Onza Tournament Edition (`1689:FD00`) is recognized by a
> device-specific driver; CDC diagnostics confirmed the LED command completed
> and a 20-byte input report was received. Keyboard hardware, physical UART
> output, the proposed VBUS circuit, and end-to-end behavior remain unverified.

## Goal

TORII should enumerate a USB input device, interpret its HID reports, and send
normalized input state over UART. The receiving system needs only a UART
receiver and a TORII protocol decoder; USB Host support stays on TORII.

The initial priority is one USB keyboard and reliable key state delivery.
TORII reports USB HID Usage IDs, not ASCII or Unicode. Keyboard layout, text
input, and repeat policy belong to the receiving host.

## Target architecture

```text
USB keyboard -> RP2040 PIO USB Host -> HID keyboard -> UART protocol
                                                        |
                                                        v
                                                 embedded host
```

The leading software candidate is Raspberry Pi Pico SDK + TinyUSB Host +
[Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB). Its upstream
project provides a host HID-to-native-USB-CDC example. TORII will use a
hardware UART for the host link and keep the XIAO USB-C port available for
programming/debugging.

## Initial status

| Area | Status |
|---|---|
| USB Host feasibility | PIO Host configured; the connected Onza is detected and produces input reports |
| USB keyboard | Boot-keyboard path implemented; not yet tested on hardware |
| USB device enumeration | CDC diagnostic reports configured VID/PID; Razer Onza (`1689:FD00`) interface 0 is opened |
| UART framing/protocol | Version 1 implemented; PC unit tests pass |
| GPIO and VBUS wiring | Candidate in [docs/hardware.md](docs/hardware.md); bench verification pending |
| Firmware / C host decoder | Firmware build and C decoder implemented; hardware verification pending |
| Other HID interfaces | Raw report forwarding implemented (one interface, max 32-byte reports); no descriptor parser |
| Razer Onza | Player-1 LED transfer succeeded; 20-byte input report observed; short 3-byte transfers also occur and are ignored |
| Mouse / gamepad | No normalized parsing; other XInput/vendor-specific devices unsupported |

## Build and wiring

### Build on WSL Ubuntu

Install the ARM compiler and Pico SDK (Pico-PIO-USB is fetched at a pinned
revision by CMake):

```sh
sudo apt update
sudo apt install cmake ninja-build gcc-arm-none-eabi libnewlib-arm-none-eabi \
  libstdc++-arm-none-eabi-newlib
git clone --branch 2.2.0 --depth 1 https://github.com/raspberrypi/pico-sdk.git \
  "$HOME/pico/pico-sdk"
git -C "$HOME/pico/pico-sdk" submodule update --init lib/tinyusb
```

Build the PC tests and firmware:

```sh
cmake -S . -B build/host -G Ninja
cmake --build build/host
ctest --test-dir build/host --output-on-failure

cmake -S . -B "$HOME/build/torii-usb-xiao" -G Ninja \
  -DTORII_BUILD_FIRMWARE=ON \
  -DPICO_SDK_PATH="$HOME/pico/pico-sdk" \
  -DPICO_BOARD=seeed_xiao_rp2040 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$HOME/build/torii-usb-xiao"
```

When building from WSL, keep the firmware build directory on the WSL Linux
filesystem; the Windows picotool executable may not understand `/mnt/c/...`
output paths. The firmware build produces
`$HOME/build/torii-usb-xiao/firmware/torii_firmware.uf2`.
Dependency revisions and licenses are listed in
[docs/dependencies.md](docs/dependencies.md).

Firmware currently configures GP6/GP7 (XIAO D4/D5) for PIO USB D+/D− and
GP0/GP1 (D6/D7) for 115200 baud UART TX/RX. These pin choices have not yet
been verified on a prototype. Do not connect a keyboard until the USB-A VBUS
current limit, reverse-current path, and signal protection are verified as
described in [docs/hardware.md](docs/hardware.md). UART is 3.3 V logic; connect
grounds and cross TX/RX.
The breadboard proposal sources USB-A VBUS from the XIAO 5V/VBUS pad through
a current-limited, reverse-blocking load switch; confirm the USB-C source can
power both the board and keyboard. For the wiring details, follow the
[breadboard wiring diagram](docs/breadboard-wiring.md). Do not plug in a
keyboard until the VBUS current limit and reverse-current protection are in
place.

## Roadmap

1. **Phase 0 — feasibility:** XIAO PIO USB Host detects the connected Onza and
   receives a 20-byte report. Next, characterize its short transfers, verify
   UART events and keyboard enumeration, and validate safe VBUS power.
2. **Phase 1 — MVP:** Protocol, C decoder, keyboard path, and PC tests are
   implemented. Remaining work is hardware E2E and measured acceptance.
3. **Phase 2 — expand input:** Mouse, multiple devices, queue robustness, and
   MicroPython driver.
4. **Phase 3 — generalize:** Parsed standard HID gamepads, reference hardware,
   examples, and release documentation.

See [docs/architecture.md](docs/architecture.md),
[docs/compatibility.md](docs/compatibility.md), and
[docs/hardware.md](docs/hardware.md) for design rationale and validation
criteria. The source-based implementation audit and prioritized next steps
are in [docs/audit.md](docs/audit.md).

## License

The Software uses the MIT License; see [LICENSE](LICENSE).
Hardware design files are not yet published or licensed. A separate hardware
license decision is required before schematic or PCB files are released.
