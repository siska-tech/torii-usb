# Firmware dependencies

The firmware build uses external dependencies and does not copy their source
into this repository. Pico-PIO-USB is fetched by CMake at the exact commit
below; Pico SDK and its TinyUSB submodule are selected by `PICO_SDK_PATH`.

| Dependency | Version / commit | License | Purpose |
|---|---|---|---|
| Raspberry Pi Pico SDK | 2.2.0 (`a1438dff1d38bd9c65dbd693f0e5db4b9ae91779`) | BSD-3-Clause | RP2040 build system, peripherals, multicore and runtime |
| TinyUSB | 0.18.0 (`86ad6e56c1700e85f1c5678607a762cfe3aa2f47`), Pico SDK submodule | MIT | USB Host HID and native USB CDC |
| Pico-PIO-USB | 0.7.3 (`b186e36bea0f3bcc2ee94ca1fa81e7f71281443a`) | MIT | RP2040 PIO USB Host controller |

Primary sources:

- <https://github.com/raspberrypi/pico-sdk>
- <https://github.com/hathach/tinyusb>
- <https://github.com/sekigon-gonnoc/Pico-PIO-USB>

The CMake build fetches Pico-PIO-USB by commit hash. For an offline/local
checkout, pass `-DPICO_PIO_USB_PATH=/path/to/Pico-PIO-USB`. The user-provided
Pico SDK checkout should be version 2.2.0 with its `lib/tinyusb` submodule
initialized. Recheck upstream licenses and notices before redistributing
firmware binaries.
