#!/usr/bin/env python3
"""Receive and inspect TORII version 1 frames from a UART serial port."""

from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass
from typing import Iterable

PROTOCOL_VERSION = 1
MAX_PAYLOAD_SIZE = 32
MAX_ENCODED_SIZE = 41

READY = 0x01
DEVICE_CONNECTED = 0x10
DEVICE_DISCONNECTED = 0x11
USB_DEVICE_STATUS = 0x12
KEYBOARD_STATE = 0x20
HID_REPORT = 0x21
XBOX360_REPORT = 0x22
ERROR = 0x7F
CDC_DEBUG_VID_PID = (0xCAFE, 0x4001)

MESSAGE_NAMES = {
    READY: "READY",
    DEVICE_CONNECTED: "DEVICE_CONNECTED",
    DEVICE_DISCONNECTED: "DEVICE_DISCONNECTED",
    USB_DEVICE_STATUS: "USB_DEVICE_STATUS",
    KEYBOARD_STATE: "KEYBOARD_STATE",
    HID_REPORT: "HID_REPORT",
    XBOX360_REPORT: "XBOX360_REPORT",
    ERROR: "ERROR",
}

MODIFIER_NAMES = (
    "LCTRL",
    "LSHIFT",
    "LALT",
    "LGUI",
    "RCTRL",
    "RSHIFT",
    "RALT",
    "RGUI",
)


class FrameError(ValueError):
    """A COBS, packet, or CRC validation failure."""

    def __init__(self, reason: str) -> None:
        super().__init__(reason)
        self.reason = reason


@dataclass(frozen=True)
class Frame:
    version: int
    message_type: int
    device_id: int
    sequence: int
    payload: bytes


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def cobs_decode(encoded: bytes) -> bytes:
    if not encoded:
        raise FrameError("empty COBS packet")

    decoded = bytearray()
    offset = 0
    while offset < len(encoded):
        code = encoded[offset]
        offset += 1
        if code == 0:
            raise FrameError("zero byte inside COBS packet")
        count = code - 1
        if count > len(encoded) - offset:
            raise FrameError("truncated COBS block")
        decoded.extend(encoded[offset : offset + count])
        offset += count
        if code != 0xFF and offset < len(encoded):
            decoded.append(0)
    return bytes(decoded)


def _validate_payload(message_type: int, device_id: int, payload: bytes) -> None:
    if message_type == READY:
        valid = device_id == 0 and len(payload) == 0
    elif message_type == DEVICE_CONNECTED:
        valid = device_id != 0 and len(payload) == 5
    elif message_type == DEVICE_DISCONNECTED:
        valid = device_id != 0 and len(payload) == 0
    elif message_type == USB_DEVICE_STATUS:
        valid = device_id == 0 and len(payload) == 6 and payload[0] in (0, 1)
    elif message_type == KEYBOARD_STATE:
        valid = device_id != 0 and len(payload) == 8 and payload[1] == 0
    elif message_type in (HID_REPORT, XBOX360_REPORT):
        valid = device_id != 0 and len(payload) <= MAX_PAYLOAD_SIZE
    elif message_type == ERROR:
        valid = len(payload) == 3
    else:
        valid = len(payload) <= MAX_PAYLOAD_SIZE

    if not valid:
        raise FrameError("invalid message payload")


def decode_frame(encoded: bytes) -> Frame:
    if len(encoded) > MAX_ENCODED_SIZE:
        raise FrameError("encoded packet too large")
    packet = cobs_decode(encoded)
    if len(packet) < 8:
        raise FrameError("packet shorter than header and CRC")

    version, message_type, device_id = packet[:3]
    sequence = packet[3] | (packet[4] << 8)
    payload_length = packet[5]
    if payload_length > MAX_PAYLOAD_SIZE or len(packet) != 8 + payload_length:
        raise FrameError("invalid packet length")

    payload_end = 6 + payload_length
    expected_crc = packet[payload_end] | (packet[payload_end + 1] << 8)
    if crc16_ccitt_false(packet[:payload_end]) != expected_crc:
        raise FrameError("CRC mismatch")
    if version != PROTOCOL_VERSION:
        raise FrameError(f"unsupported protocol version {version}")
    payload = packet[6:payload_end]
    _validate_payload(message_type, device_id, payload)
    return Frame(version, message_type, device_id, sequence, payload)


class StreamDecoder:
    """Incrementally split a UART byte stream at COBS zero delimiters."""

    def __init__(self) -> None:
        self._encoded = bytearray()
        self._dropping = False

    def feed(self, data: bytes) -> tuple[list[Frame], list[FrameError]]:
        frames: list[Frame] = []
        errors: list[FrameError] = []
        for byte in data:
            if byte == 0:
                if self._dropping:
                    errors.append(FrameError("oversized frame discarded"))
                    self._dropping = False
                    self._encoded.clear()
                elif self._encoded:
                    try:
                        frames.append(decode_frame(bytes(self._encoded)))
                    except FrameError as error:
                        errors.append(error)
                    self._encoded.clear()
            elif not self._dropping:
                if len(self._encoded) == MAX_ENCODED_SIZE:
                    self._encoded.clear()
                    self._dropping = True
                else:
                    self._encoded.append(byte)
        return frames, errors


def _hex_usages(usages: Iterable[int]) -> str:
    values = [usage for usage in usages if usage]
    return " ".join(f"{usage:02X}" for usage in values) if values else "-"


def _modifier_names(modifiers: int) -> list[str]:
    return [name for bit, name in enumerate(MODIFIER_NAMES) if modifiers & (1 << bit)]


class Monitor:
    """Track sequence and keyboard state and format useful diagnostic events."""

    def __init__(self, verbose: bool = False) -> None:
        self.verbose = verbose
        self.last_sequence: int | None = None
        self.keyboard_states: dict[int, tuple[int, frozenset[int]]] = {}
        self._keyboard_updated_at: dict[int, float] = {}
        self.raw_reports: dict[tuple[int, int], bytes] = {}
        self.frames_received = 0
        self.bad_frames = 0
        self.sequence_gaps = 0

    def handle_errors(self, errors: Iterable[FrameError]) -> list[str]:
        messages = []
        for error in errors:
            self.bad_frames += 1
            messages.append(f"INVALID frame: {error.reason}")
        return messages

    def handle_frame(self, frame: Frame, now: float | None = None) -> list[str]:
        messages: list[str] = []
        self.frames_received += 1
        if now is None:
            now = time.monotonic()

        if frame.message_type == READY:
            for device_id, (modifiers, keys) in self.keyboard_states.items():
                if modifiers or keys:
                    messages.append(
                        f"STATE RESET id={device_id} release_modifiers="
                        f"{','.join(_modifier_names(modifiers)) or '-'}"
                        f" release_usages={_hex_usages(sorted(keys))}"
                    )
            self.keyboard_states.clear()
            self._keyboard_updated_at.clear()
            self.raw_reports.clear()
            self.last_sequence = frame.sequence
            messages.append(f"READY protocol={frame.version} seq={frame.sequence}")
            return messages

        if self.last_sequence is not None:
            expected = (self.last_sequence + 1) & 0xFFFF
            if frame.sequence != expected:
                missing = (frame.sequence - expected) & 0xFFFF
                self.sequence_gaps += 1
                messages.append(
                    f"SEQUENCE GAP expected={expected} received={frame.sequence} "
                    f"missing~={missing}"
                )
        self.last_sequence = frame.sequence

        message_type = frame.message_type
        payload = frame.payload
        if message_type == DEVICE_CONNECTED:
            device_class = payload[0]
            vendor_id = payload[1] | (payload[2] << 8)
            product_id = payload[3] | (payload[4] << 8)
            old_state = self.keyboard_states.pop(frame.device_id, None)
            self._keyboard_updated_at.pop(frame.device_id, None)
            self._clear_raw_reports(frame.device_id)
            if old_state and (old_state[0] or old_state[1]):
                messages.append(
                    f"STATE RESET id={frame.device_id} release_modifiers="
                    f"{','.join(_modifier_names(old_state[0])) or '-'}"
                    f" release_usages={_hex_usages(sorted(old_state[1]))}"
                )
            messages.append(
                f"CONNECTED id={frame.device_id} class=0x{device_class:02X} "
                f"VID:PID={vendor_id:04X}:{product_id:04X}"
            )
        elif message_type == DEVICE_DISCONNECTED:
            old_state = self.keyboard_states.pop(frame.device_id, None)
            self._keyboard_updated_at.pop(frame.device_id, None)
            self._clear_raw_reports(frame.device_id)
            message = f"DISCONNECTED id={frame.device_id}"
            if old_state:
                modifiers, keys = old_state
                released_modifiers = _modifier_names(modifiers)
                released_keys = _hex_usages(keys)
                if released_modifiers or keys:
                    message += (
                        f" release_modifiers={','.join(released_modifiers) or '-'}"
                        f" release_usages={released_keys}"
                    )
            messages.append(message)
        elif message_type == USB_DEVICE_STATUS:
            configured, address = payload[:2]
            vendor_id = payload[2] | (payload[3] << 8)
            product_id = payload[4] | (payload[5] << 8)
            status = "configured" if configured else "unconfigured"
            messages.append(
                f"USB {status} addr={address} VID:PID={vendor_id:04X}:{product_id:04X}"
            )
        elif message_type == KEYBOARD_STATE:
            modifiers = payload[0]
            report_keys = payload[2:8]
            rollover = 0x01 in report_keys
            keys = frozenset() if rollover else frozenset(report_keys)
            previous = self.keyboard_states.get(frame.device_id, (0, frozenset()))
            pressed = keys - previous[1]
            released = previous[1] - keys
            modifier_pressed = modifiers & ~previous[0]
            modifier_released = previous[0] & ~modifiers
            self.keyboard_states[frame.device_id] = (modifiers, keys)
            self._keyboard_updated_at[frame.device_id] = now
            if pressed or released or modifier_pressed or modifier_released or rollover:
                messages.append(
                    f"KEYBOARD id={frame.device_id}"
                    f" down={_hex_usages(sorted(pressed))}"
                    f" up={_hex_usages(sorted(released))}"
                    f" modifiers_down={','.join(_modifier_names(modifier_pressed)) or '-'}"
                    f" modifiers_up={','.join(_modifier_names(modifier_released)) or '-'}"
                    f" rollover={'yes' if rollover else 'no'}"
                    f" held={_hex_usages(sorted(keys))}"
                )
            elif self.verbose:
                messages.append(
                    f"KEYBOARD id={frame.device_id} unchanged "
                    f"held={_hex_usages(sorted(keys))}"
                )
        elif message_type in (HID_REPORT, XBOX360_REPORT):
            key = (message_type, frame.device_id)
            if self.verbose or self.raw_reports.get(key) != payload:
                self.raw_reports[key] = payload
                name = MESSAGE_NAMES[message_type]
                messages.append(
                    f"{name} id={frame.device_id} len={len(payload)} data={payload.hex(' ').upper() or '-'}"
                )
        elif message_type == ERROR:
            code = payload[0]
            detail = payload[1] | (payload[2] << 8)
            messages.append(f"ERROR code={code} detail={detail}")
        elif self.verbose:
            messages.append(
                f"{MESSAGE_NAMES.get(message_type, f'UNKNOWN_0x{message_type:02X}')}"
                f" id={frame.device_id} len={len(payload)} data={payload.hex(' ').upper() or '-'}"
            )

        return messages

    def _clear_raw_reports(self, device_id: int) -> None:
        for key in list(self.raw_reports):
            if key[1] == device_id:
                self.raw_reports.pop(key)

    def expire_keyboard_states(self, now: float, timeout_seconds: float) -> list[str]:
        if timeout_seconds <= 0:
            return []
        messages = []
        for device_id, (modifiers, keys) in list(self.keyboard_states.items()):
            state_time = self._keyboard_updated_at[device_id]
            if now - state_time >= timeout_seconds:
                self.keyboard_states.pop(device_id, None)
                self._keyboard_updated_at.pop(device_id, None)
                messages.append(
                    f"STALE id={device_id} release_modifiers="
                    f"{','.join(_modifier_names(modifiers)) or '-'}"
                    f" release_usages={_hex_usages(sorted(keys))}"
                )
        return messages


def _parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Decode TORII COBS/CRC frames from a 3.3 V UART adapter."
    )
    parser.add_argument("--port", help="serial port, e.g. COM5 or /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200, help="UART baud (default: 115200)")
    parser.add_argument("--verbose", action="store_true", help="show unchanged snapshots and raw reports")
    parser.add_argument(
        "--cdc",
        action="store_true",
        help="read native USB CDC text diagnostics instead of binary UART frames",
    )
    parser.add_argument(
        "--duration",
        type=float,
        default=0,
        metavar="SECONDS",
        help="stop after this many seconds (default: run until Ctrl+C)",
    )
    parser.add_argument(
        "--state-timeout",
        type=float,
        default=1.0,
        metavar="SECONDS",
        help="release displayed held keys after this much silence (0 disables; default: 1.0)",
    )
    parser.add_argument("--list-ports", action="store_true", help="list serial ports and exit")
    args = parser.parse_args(argv)
    if not args.list_ports and not args.port:
        parser.error("--port is required unless --list-ports is used")
    if args.baud <= 0:
        parser.error("--baud must be positive")
    if args.state_timeout < 0:
        parser.error("--state-timeout cannot be negative")
    if args.duration < 0:
        parser.error("--duration cannot be negative")
    return args


def _list_ports() -> int:
    try:
        from serial.tools import list_ports
    except ImportError:
        print("pyserial is required; install with: python -m pip install -r requirements.txt", file=sys.stderr)
        return 2
    ports = list(list_ports.comports())
    if not ports:
        print("No serial ports found.")
        return 0
    for port in ports:
        description = port.description or "serial port"
        if (port.vid, port.pid) == CDC_DEBUG_VID_PID:
            description += " [TORII CDC diagnostics, not the binary UART]"
        print(f"{port.device}: {description}")
    return 0


def _run_serial(
    port: str,
    baud: int,
    verbose: bool,
    state_timeout: float,
    duration: float,
) -> int:
    try:
        import serial
    except ImportError:
        print("pyserial is required; install with: python -m pip install -r requirements.txt", file=sys.stderr)
        return 2

    try:
        from serial.tools import list_ports
    except ImportError:
        print("pyserial is required; install with: python -m pip install -r requirements.txt", file=sys.stderr)
        return 2
    selected_port = next(
        (candidate for candidate in list_ports.comports() if candidate.device == port),
        None,
    )
    if selected_port and (selected_port.vid, selected_port.pid) == CDC_DEBUG_VID_PID:
        print(
            "This is TORII's native CDC diagnostic console, not the binary UART. "
            "Connect a 3.3 V USB-UART adapter to GP0/TX and GND.",
            file=sys.stderr,
        )
        return 2

    decoder = StreamDecoder()
    monitor = Monitor(verbose=verbose)
    print(
        f"Listening on {port} at {baud} baud, 8N1. "
        "Connect TORII GP0/TX to adapter RX and share GND; Ctrl+C to stop.",
        flush=True,
    )
    started_at = time.monotonic()
    try:
        with serial.Serial(
            port=port,
            baudrate=baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.1,
        ) as connection:
            while duration == 0 or time.monotonic() - started_at < duration:
                chunk = connection.read(256)
                frames, errors = decoder.feed(chunk)
                for message in monitor.handle_errors(errors):
                    print(message, flush=True)
                for frame in frames:
                    for message in monitor.handle_frame(frame):
                        print(message, flush=True)
                for message in monitor.expire_keyboard_states(time.monotonic(), state_timeout):
                    print(message, flush=True)
        print(
            f"Stopped. frames={monitor.frames_received} "
            f"invalid={monitor.bad_frames} sequence_gaps={monitor.sequence_gaps}",
            flush=True,
        )
        return 0
    except serial.SerialException as error:
        print(f"Serial error: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print(
            f"\nStopped. frames={monitor.frames_received} "
            f"invalid={monitor.bad_frames} sequence_gaps={monitor.sequence_gaps}",
            flush=True,
        )
        return 0


def _run_cdc(port: str, baud: int, duration: float) -> int:
    try:
        import serial
    except ImportError:
        print("pyserial is required; install with: python -m pip install -r requirements.txt", file=sys.stderr)
        return 2

    print(
        f"Listening for text CDC diagnostics on {port}; DTR is enabled. "
        "This is not the binary GP0 UART stream.",
        flush=True,
    )
    connection = serial.Serial(
        port=None,
        baudrate=baud,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=0.1,
    )
    connection.port = port
    connection.dtr = True
    started_at = time.monotonic()
    line_count = 0
    input_report_count = 0
    try:
        connection.open()
        while duration == 0 or time.monotonic() - started_at < duration:
            raw_line = connection.readline()
            if not raw_line:
                continue
            line = raw_line.decode("utf-8", errors="replace").rstrip()
            if not line:
                continue
            line_count += 1
            if line.startswith("[XPAD] input "):
                input_report_count += 1
            print(line, flush=True)
        print(
            f"Stopped. lines={line_count} changed_input_reports={input_report_count}",
            flush=True,
        )
        return 0
    except serial.SerialException as error:
        print(f"Serial error: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print(
            f"\nStopped. lines={line_count} changed_input_reports={input_report_count}",
            flush=True,
        )
        return 0
    finally:
        if connection.is_open:
            connection.close()


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv)
    if args.list_ports:
        return _list_ports()
    if args.cdc:
        return _run_cdc(args.port, args.baud, args.duration)
    return _run_serial(
        args.port,
        args.baud,
        args.verbose,
        args.state_timeout,
        args.duration,
    )


if __name__ == "__main__":
    raise SystemExit(main())
