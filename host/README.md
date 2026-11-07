# Host libraries

The Phase 1 C decoder is implemented in the `torii_protocol` library and
re-exported to C consumers through the `torii_host_decoder` CMake target. It
parses COBS/CRC frames, tracks sequence gaps, derives keyboard state from
snapshots, derives press/release edges, clears on disconnect, and exposes a
stale-state timeout/recovery helper.
It has PC unit tests but has not yet been exercised against live UART traffic.
MicroPython and Python tooling remain later roadmap items.

See [the version 1 protocol specification](../docs/protocol.md).
