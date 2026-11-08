import unittest

from torii_debug import (
    DEVICE_CONNECTED,
    DEVICE_DISCONNECTED,
    ERROR,
    KEYBOARD_STATE,
    READY,
    Frame,
    FrameError,
    Monitor,
    StreamDecoder,
    crc16_ccitt_false,
    decode_frame,
)


def cobs_encode(data: bytes) -> bytes:
    encoded = bytearray([0])
    code_index = 0
    code = 1
    for byte in data:
        if byte == 0:
            encoded[code_index] = code
            code_index = len(encoded)
            encoded.append(0)
            code = 1
        else:
            encoded.append(byte)
            code += 1
            if code == 0xFF:
                encoded[code_index] = code
                code_index = len(encoded)
                encoded.append(0)
                code = 1
    encoded[code_index] = code
    return bytes(encoded)


def encode_frame(message_type: int, device_id: int, sequence: int, payload: bytes) -> bytes:
    packet = bytearray(
        (
            1,
            message_type,
            device_id,
            sequence & 0xFF,
            sequence >> 8,
            len(payload),
        )
    )
    packet.extend(payload)
    crc = crc16_ccitt_false(packet)
    packet.extend((crc & 0xFF, crc >> 8))
    return cobs_encode(packet) + b"\x00"


class ProtocolTests(unittest.TestCase):
    def test_crc_and_published_ready_vector(self) -> None:
        self.assertEqual(crc16_ccitt_false(b"123456789"), 0x29B1)
        frame = decode_frame(bytes.fromhex("03 01 01 01 01 01 03 E1 E1"))
        self.assertEqual(frame, Frame(1, READY, 0, 0, b""))

    def test_keyboard_snapshot_and_recovery_after_invalid_crc(self) -> None:
        decoder = StreamDecoder()
        key_down = encode_frame(KEYBOARD_STATE, 1, 10, bytes((0x02, 0, 0x04, 0, 0, 0, 0, 0)))
        key_up = encode_frame(KEYBOARD_STATE, 1, 11, bytes((0, 0, 0, 0, 0, 0, 0, 0)))
        corrupt = bytearray(key_down)
        corrupt[-2] ^= 0x01

        frames, errors = decoder.feed(bytes(corrupt) + key_up)
        self.assertEqual(len(errors), 1)
        self.assertEqual(errors[0].reason, "CRC mismatch")
        self.assertEqual([frame.sequence for frame in frames], [11])

        monitor = Monitor()
        pressed = monitor.handle_frame(
            Frame(1, KEYBOARD_STATE, 1, 10, bytes((0x02, 0, 0x04, 0, 0, 0, 0, 0))),
            now=1.0,
        )
        self.assertIn("down=04", pressed[0])
        self.assertIn("modifiers_down=LSHIFT", pressed[0])

        released = monitor.handle_frame(
            Frame(1, KEYBOARD_STATE, 1, 11, bytes((0, 0, 0, 0, 0, 0, 0, 0))),
            now=1.1,
        )
        self.assertIn("up=04", released[0])
        self.assertIn("modifiers_up=LSHIFT", released[0])

    def test_rollover_does_not_report_error_usage_as_a_pressed_key(self) -> None:
        monitor = Monitor()
        monitor.handle_frame(
            Frame(1, KEYBOARD_STATE, 1, 1, bytes((0, 0, 0x04, 0, 0, 0, 0, 0))),
            now=1.0,
        )
        messages = monitor.handle_frame(
            Frame(1, KEYBOARD_STATE, 1, 2, bytes((0, 0, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01))),
            now=1.1,
        )
        self.assertIn("up=04", messages[0])
        self.assertIn("rollover=yes", messages[0])
        self.assertEqual(monitor.keyboard_states[1][1], frozenset())

    def test_device_lifecycle_and_stale_key_release(self) -> None:
        monitor = Monitor()
        monitor.handle_frame(
            Frame(1, DEVICE_CONNECTED, 1, 0, bytes((3, 0x34, 0x12, 0x78, 0x56))),
            now=1.0,
        )
        monitor.handle_frame(
            Frame(1, KEYBOARD_STATE, 1, 1, bytes((0, 0, 0x04, 0, 0, 0, 0, 0))),
            now=1.0,
        )
        stale = monitor.expire_keyboard_states(now=2.1, timeout_seconds=1.0)
        self.assertIn("release_usages=04", stale[0])
        self.assertNotIn(1, monitor.keyboard_states)

        connected = monitor.handle_frame(
            Frame(1, DEVICE_CONNECTED, 2, 2, bytes((3, 0x89, 0x16, 0x00, 0xFD))),
            now=2.2,
        )
        self.assertIn("VID:PID=1689:FD00", connected[0])
        disconnected = monitor.handle_frame(
            Frame(1, DEVICE_DISCONNECTED, 2, 3, b""),
            now=2.3,
        )
        self.assertIn("DISCONNECTED id=2", disconnected[0])

    def test_sequence_wrap_and_missing_frame(self) -> None:
        monitor = Monitor()
        monitor.handle_frame(Frame(1, READY, 0, 0xFFFF, b""))
        error = monitor.handle_frame(Frame(1, ERROR, 0, 0, b"\x01\x00\x00"))
        self.assertIn("ERROR code=1 detail=0", error[0])
        gap = monitor.handle_frame(Frame(1, ERROR, 0, 2, b"\x01\x00\x00"))
        self.assertIn("SEQUENCE GAP expected=1 received=2 missing~=1", gap[0])
        self.assertEqual(monitor.sequence_gaps, 1)

    def test_disconnect_and_ready_clear_held_state(self) -> None:
        monitor = Monitor()
        monitor.handle_frame(
            Frame(1, KEYBOARD_STATE, 1, 1, bytes((0, 0, 0x04, 0, 0, 0, 0, 0))),
            now=1.0,
        )
        disconnected = monitor.handle_frame(Frame(1, DEVICE_DISCONNECTED, 1, 2, b""))
        self.assertIn("release_usages=04", disconnected[0])

        monitor.handle_frame(
            Frame(1, KEYBOARD_STATE, 1, 3, bytes((0, 0, 0x05, 0, 0, 0, 0, 0))),
            now=1.1,
        )
        reset = monitor.handle_frame(Frame(1, READY, 0, 0, b""))
        self.assertIn("STATE RESET id=1", reset[0])
        self.assertIn("READY protocol=1", reset[1])

    def test_onza_raw_report_is_shown_only_when_changed(self) -> None:
        monitor = Monitor()
        report = bytes.fromhex("00 14 00 00 00 00 00 FE 00 00 00 00 00 00 00 00 00 00 00 00")
        frame = Frame(1, 0x22, 3, 10, report)
        first = monitor.handle_frame(frame)
        second = monitor.handle_frame(
            Frame(1, 0x22, 3, 11, report),
        )
        self.assertIn("XBOX360_REPORT id=3 len=20", first[0])
        self.assertIn("00 14", first[0])
        self.assertEqual(second, [])
        monitor.handle_frame(
            Frame(1, DEVICE_CONNECTED, 3, 12, bytes((0xFF, 0x89, 0x16, 0x00, 0xFD))),
        )
        reconnected = monitor.handle_frame(Frame(1, 0x22, 3, 13, report))
        self.assertIn("XBOX360_REPORT id=3 len=20", reconnected[0])

    def test_stream_discards_oversized_frame_until_delimiter(self) -> None:
        decoder = StreamDecoder()
        frames, errors = decoder.feed(b"A" * 50 + b"\x00" + encode_frame(READY, 0, 0, b""))
        self.assertEqual(len(errors), 1)
        self.assertEqual(errors[0].reason, "oversized frame discarded")
        self.assertEqual(frames[0].message_type, READY)

    def test_bad_type_specific_payload_is_rejected(self) -> None:
        with self.assertRaises(FrameError):
            decode_frame(encode_frame(DEVICE_CONNECTED, 1, 0, b"")[:-1])


if __name__ == "__main__":
    unittest.main()
