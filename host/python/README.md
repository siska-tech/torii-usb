# Python diagnostic tools

`torii_debug.py` is a PC-side receiver for the TORII v1 COBS/CRC UART stream.
It prints device lifecycle, keyboard Usage ID press/release and modifier
changes, raw HID/Xbox reports, protocol errors, malformed frames, and sequence
gaps. It does not convert Usage IDs to characters or generate key repeats.
Repeated state snapshots do not appear as repeated key presses; use
`--verbose` to show unchanged snapshots and every raw report.

## Requirements and usage

Python 3.10 or later and pyserial are required:

```sh
python -m pip install -r host/python/requirements.txt
python host/python/torii_debug.py --list-ports
python host/python/torii_debug.py --port COM5
```

The default serial format is 115200 baud, 8N1. On Windows, select the COM
port belonging to a **3.3 V USB-UART adapter** connected to XIAO GP0/TX -> RX
and shared GND. Do not connect a 5 V UART signal to the XIAO.

The XIAO's native USB-C CDC port (currently COM9, VID:PID `CAFE:4001`) is a
text diagnostic console, not the binary GP0 UART stream. The tool identifies
this development CDC VID/PID and refuses to decode it as UART data. To view
those text diagnostics (including changed Onza raw reports), use:

```sh
python host/python/torii_debug.py --port COM9 --cdc
```

`--cdc` enables DTR. Add `--duration 10` for a bounded capture. This mode does
not decode keyboard input from CDC; key-state frames are sent over GP0 UART.
A live five-second capture on the connected prototype displayed 247 changed
Onza input-report lines. These are raw device reports and do not establish
button/axis interpretation or binary UART operation.

By default, held keyboard state is cleared locally after one second without a
valid keyboard snapshot, and a `STALE` line is printed. Override with
`--state-timeout SECONDS`, or use `0` to disable. Sequence gaps are reported;
the next full keyboard snapshot replaces the displayed current state. A
missing brief tap cannot be reconstructed by snapshots.

## Tests

The parser and state monitor use only the Python standard library. Run their
tests without opening a serial port:

```sh
python -m unittest discover -s host/python -p "test_*.py" -v
```

The tests cover the published READY vector, CRC validation, framing recovery,
oversized packets, keyboard down/up and modifiers, rollover, disconnect and
stale-state clearing, sequence wrap/gaps, and the observed Onza 20-byte report.
Physical UART traffic still requires the adapter and must be tested separately.
