# Compatibility and validation matrix

**Status:** XIAO firmware and native USB CDC are verified. The connected Razer
Onza Tournament Edition has been tested with the device-specific driver;
keyboard and UART hardware behavior remain unverified. Last reviewed:
2026-11-07.

| Device / feature | Phase | Current expectation | Known limitation / validation |
|---|---:|---|---|
| USB HID boot keyboard | 1 | Primary target through TinyUSB Host HID | Validate enumeration/report handling on XIAO. Boot reports are 6KRO; keyboards without usable boot protocol may not work. |
| Composite keyboard interfaces | 1 | Not guaranteed | Test interface selection and device limits; defer broad compatibility claim. |
| NKRO keyboard reports | Later | Not supported by the Phase 1 six-key state packet | Requires descriptor-aware report parsing and a versioned protocol extension or explicit normalized-state capacity. |
| USB HID mouse | 2 | Candidate for TinyUSB HID Host | Relative deltas, buttons, wheel, report IDs, and unusual descriptors need hardware tests. |
| Standard HID gamepad | Bring-up | HID interface is mounted and raw reports (up to 32 bytes) are forwarded as `HID_REPORT` | No descriptor parsing or normalized buttons/axes yet; test the exact controller and parse reports on the host. |
| XInput / vendor-specific devices | Bring-up | Only the Razer Onza Tournament Edition (`1689:FD00`) has a targeted driver | Not general XInput support; other controllers and normalized button/axis parsing are not implemented. |
| Razer Onza Tournament Edition (`1689:FD00`) | Hardware bring-up | Interface 0 opened with IN `0x81` / OUT `0x02`; player-1 LED transfer completed and a 20-byte input report was received | Three-byte transfers were also observed and ignored. Button/axis parsing, UART reception, and disconnect recovery are unverified. |
| Multiple devices / hubs | Later | Not Phase 1 | Upstream project advertises hub support, but TORII configuration and resource limits remain unvalidated. |

## Test matrix for Phase 1

Record each device's vendor/product ID, manufacturer/model, keyboard protocol,
USB report interval if available, test firmware revision, and pass/fail.

| Test | Expected result |
|---|---|
| Single key down/up | Correct HID Usage ID appears in state and clears on release. |
| Modifier combinations | Left/right modifier bits remain distinct and combine with key state. |
| Simultaneous keys | Up to six ordinary key usages are reported; rollover is handled without inventing keys. |
| Device connect/disconnect | Lifecycle frames emitted; receiver clears all state at disconnect. |
| UART disconnect/stall | USB servicing continues; overflow is visible and latest state restores held-key state after recovery. |
| Corrupt/truncated bytes | Decoder rejects bad frame and resumes at a later COBS delimiter. |
| Repeated insert/remove | No stale device state or unrecovered host stack failure. |
| Long run | 24-hour test with periodic state and no stuck key after recovery. |

Passing this matrix supports only the specific tested combinations. It does
not establish universal USB HID compatibility or USB certification.

The raw HID path only applies if TinyUSB recognizes and mounts the device's
interface as HID. Xbox-branded controllers vary by generation and connection
mode. On the connected prototype, VID `1689`, PID `FD00` was identified as a
Razer Onza Tournament Edition. Its vendor-specific driver now opens the
interrupt endpoints, sends the player-1 LED command successfully, and receives
a 20-byte Xbox 360-compatible input report. Three-byte transfers also arrive
and are ignored. Buttons and axes are not yet decoded, UART output has not
been captured, and this result does not establish general Xbox controller
compatibility.

## Upstream references

- [TinyUSB Host stack and supported classes](https://github.com/hathach/tinyusb)
- [Pico-PIO-USB status and example list](https://github.com/sekigon-gonnoc/Pico-PIO-USB)
- [Pico-PIO-USB Host HID-to-CDC example](https://github.com/sekigon-gonnoc/Pico-PIO-USB/blob/main/examples/host_hid_to_device_cdc/host_hid_to_device_cdc.c)
- [Linux `xpad` device table](https://github.com/torvalds/linux/blob/master/drivers/input/joystick/xpad.c): maps `1689:fd00` to Razer Onza Tournament Edition and `XTYPE_XBOX360`; this is evidence of Linux's classification, not a guarantee of compatibility with this implementation.
