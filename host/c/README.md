# C host decoder

The `torii_host_decoder` CMake interface target exposes the portable
`torii_protocol` library and its `torii_protocol.h` API. Link against this
target to validate/decode version 1 frames and apply keyboard state recovery
without KAGURA-specific dependencies. `torii_host_state_diff()` returns
modifier and key edges by comparing key sets, not report-slot positions.
