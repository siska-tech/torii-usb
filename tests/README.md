# Tests

Host-buildable tests cover COBS/CRC known-answer vectors, frame round trips,
malformed packet handling and resynchronization, output-capacity bounds,
keyboard state transitions, sequence gaps/wrap, rollover, disconnect reset,
and stale-state timeout after UART silence.

Run with `cmake -S . -B build/host -G Ninja`, `cmake --build build/host`, and
`ctest --test-dir build/host --output-on-failure`. Physical UART-stall and
keyboard tests remain outstanding.
