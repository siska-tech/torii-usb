# UART protocol draft

**Status:** version 1 format is implemented and PC-tested; UART hardware
traffic has not been validated.
Last reviewed: 2026-11-07.

## Link and framing

- Initial link: 115200 baud, 8N1, 3.3 V UART, TORII-to-host.
- Each packet is encoded with COBS and terminated by `0x00`.
- The decoder treats each zero byte as a frame boundary. Empty or malformed
  frames are discarded; the next boundary restores synchronization.
- All multibyte integers are little-endian.

After COBS decoding, the packet is:

| Field | Bytes | Meaning |
|---|---:|---|
| `version` | 1 | Protocol major version; `0x01` |
| `type` | 1 | Message type below |
| `device_id` | 1 | `0` for controller/global messages; `1..255` assigned to a connected device |
| `sequence` | 2 | Per-controller unsigned counter, incremented for each emitted packet; wraps modulo 65536 |
| `payload_len` | 1 | Payload length in bytes, maximum 32 |
| `payload` | 0..32 | Message-specific data |
| `crc16` | 2 | CRC-16/CCITT-FALSE over header and payload; init `0xFFFF`, poly `0x1021`, xorout `0x0000` |

CRC bytes are transmitted little-endian. The maximum decoded packet is
40 bytes; its COBS-encoded form plus zero delimiter is at most 42 bytes.
Reject packets with an unknown major version, invalid type-specific length,
oversized payload, invalid CRC, or invalid COBS encoding.

## Message types

| Type | Name | Device ID | Payload |
|---:|---|---|---|
| `0x01` | `READY` | `0` | Empty. Sent after UART/protocol initialization. |
| `0x10` | `DEVICE_CONNECTED` | nonzero | Class `u8`, VID `u16`, PID `u16` (5 bytes). |
| `0x11` | `DEVICE_DISCONNECTED` | nonzero | Empty. Receiver clears that device's state. |
| `0x12` | `USB_DEVICE_STATUS` | `0` | Configured flag `u8`, USB address `u8`, VID `u16`, PID `u16` (6 bytes; VID/PID are zero on unconfigure). |
| `0x20` | `KEYBOARD_STATE` | nonzero | Boot report state: modifiers `u8`, reserved `u8`, six Usage IDs `u8` each (8 bytes). |
| `0x21` | `HID_REPORT` | nonzero | Raw bytes from a non-boot-keyboard HID interface report (0..32 bytes). |
| `0x22` | `XBOX360_REPORT` | nonzero | Raw Xbox 360-compatible input report (0..32 bytes); report-specific parsing is outside protocol v1. |
| `0x7F` | `ERROR` | `0` or nonzero | Error code `u8`, dropped-packet count `u16` (3 bytes). |

The firmware currently assigns device ID `1` to its boot-keyboard path and
device ID `2` to one other HID interface. For `HID_REPORT`, payload bytes are
the report buffer delivered by TinyUSB, without further interpretation by
TORII. The report descriptor is not parsed, and analog axes/buttons are not
normalized. Reports larger than 32 bytes are dropped and reported as error
code `6`. This raw forwarding path is intended for bring-up and
device-specific host parsers.

The Onza Tournament Edition bring-up path uses device ID `3` and reports USB
class `0xFF` in `DEVICE_CONNECTED`, because its interface is vendor-specific,
not HID. If its Xbox 360-compatible 20-byte input transfer is received, the
firmware forwards it unchanged as `XBOX360_REPORT`; no buttons, triggers, or
axes are decoded by protocol v1.

`USB_DEVICE_STATUS` reports that TinyUSB configured or unconfigured a USB
device. It includes VID/PID on configuration even when no supported class
driver mounts; this indicates USB enumeration, not usable input support.

Modifier bits follow the USB HID boot keyboard order: left Ctrl, left Shift,
left Alt, left GUI, right Ctrl, right Shift, right Alt, right GUI (bit 0 to
bit 7). The six key slots contain USB HID Keyboard/Keypad Usage IDs, not
characters. A zero slot is empty. A HID rollover/error usage invalidates
ordinary key slots until a valid report arrives; modifier state is still
reported.

`KEYBOARD_STATE` is a complete current snapshot, sent on state changes and
periodically (every 250 ms). The firmware suppresses identical interrupt
reports and sends a state frame only when the current key/modifier state
changes or the periodic refresh is due. The host computes edges by
comparing snapshots. Repeated snapshots are valid and provide recovery after
missed frames. Device disconnect clears all held state.
The C reference API exposes `torii_host_state_diff()` to derive key/modifier
press and release transitions as sets; key-slot ordering does not affect the
result.

## Loss and recovery behavior

- A sequence gap means one or more packets were lost; expose the gap to the
  application and replace the keyboard state with the next valid snapshot.
- The C reference decoder provides `torii_host_state_apply()` and
  `torii_host_state_expire()`. Call the expiry helper with a monotonic
  millisecond clock and a timeout (recommended 1000 ms) to clear stale device
  and key state if refresh/disconnect frames stop arriving. A later keyboard
  snapshot can re-establish the device state if the disconnect/connect frame
  was lost.
- On queue pressure, do not block USB servicing. The current bounded packet
  queue drops a frame and increments a saturating counter when full; the
  periodic snapshot repairs current held-key state after transport recovers.
  A short press/release wholly inside the stall cannot be restored.
- The host must clear a device's key state on disconnect. A host-side timeout
  may clear stale keys if no valid snapshots arrive; the timeout policy belongs
  to the host library and should be configurable/documented.
- State snapshots restore which keys are currently down, but cannot recover a
  short press and release both lost while UART is stalled.
- There is no host-to-TORII command, ACK/retry, or flow-control message in
  version 1. These are future extensions, not silently assumed capabilities.

## Encoding and test vectors

The C test suite verifies CRC-16/CCITT-FALSE with the standard
`"123456789" -> 0x29B1` check value and verifies this complete READY frame
vector (including delimiter), independently calculated from the specified
fields:

```text
03 01 01 01 01 01 03 E1 E1 00
```

Tests also cover round trips, malformed CRC/COBS, small output buffers,
delimiter resynchronization, sequence wrap/gaps, device disconnect clearing,
rollover, and stale-state timeout. Hardware behavior remains unvalidated.

Error codes currently emitted by the firmware are: `1` TX queue overflow
(payload also carries the saturating dropped-packet count), `2` failure to
request the next HID report, `3` invalid/short HID report, and `4` Host stack
initialization retry, `5` more than one non-keyboard HID interface, and `6`
HID report larger than the protocol payload limit (the payload's second and
third bytes carry the report length). Error code `5` means the one raw HID
interface slot is already occupied. Other error codes are reserved.

## Compatibility policy

Version 1 uses explicit length, version, CRC, and sequence fields. New
incompatible layouts require a new major version. Do not reinterpret HID
Usage IDs as ASCII. A version 1 receiver must ignore an unknown message type
only after validating its complete frame and CRC; report the unsupported type
to diagnostics.
