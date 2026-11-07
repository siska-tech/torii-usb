#include "torii_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static torii_frame_t make_frame(
    uint8_t type,
    uint8_t device_id,
    uint16_t sequence,
    const uint8_t *payload,
    uint8_t payload_length)
{
    torii_frame_t frame = {0};
    frame.version = TORII_PROTOCOL_VERSION;
    frame.type = type;
    frame.device_id = device_id;
    frame.sequence = sequence;
    frame.payload_len = payload_length;
    if (payload_length != 0) {
        memcpy(frame.payload, payload, payload_length);
    }
    return frame;
}

static void test_crc_vector(void)
{
    static const uint8_t input[] = "123456789";
    assert(torii_crc16_ccitt_false(input, sizeof(input) - 1) == 0x29b1u);
}

static void test_golden_ready_packet(void)
{
    static const uint8_t expected[] = {
        0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x03, 0xe1, 0xe1, 0x00
    };
    uint8_t encoded[TORII_MAX_ENCODED_SIZE];
    size_t encoded_length = 0;
    torii_frame_t frame = make_frame(TORII_MSG_READY, 0, 0, NULL, 0);

    assert(torii_encode_frame(
        &frame, encoded, sizeof(encoded), &encoded_length) == TORII_CODEC_OK);
    assert(encoded_length == sizeof(expected));
    assert(memcmp(encoded, expected, sizeof(expected)) == 0);
}

static void test_output_capacity(void)
{
    uint8_t encoded[1] = {0xa5};
    size_t encoded_length = 99;
    torii_frame_t frame = make_frame(TORII_MSG_READY, 0, 0, NULL, 0);

    assert(torii_encode_frame(
        &frame, encoded, sizeof(encoded), &encoded_length) ==
        TORII_CODEC_OUTPUT_TOO_SMALL);
    assert(encoded[0] == 0xa5);
    assert(encoded_length == 99);
}

static void test_round_trip_and_stream_recovery(void)
{
    static const uint8_t payload[] = {
        0x03, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    uint8_t encoded[TORII_MAX_ENCODED_SIZE];
    size_t encoded_length = 0;
    torii_frame_t source = make_frame(
        TORII_MSG_KEYBOARD_STATE, 1, 0x1234, payload, sizeof(payload));
    torii_frame_t decoded = {0};
    torii_stream_decoder_t stream;

    assert(torii_encode_frame(
        &source, encoded, sizeof(encoded), &encoded_length) == TORII_CODEC_OK);
    torii_stream_decoder_init(&stream);
    assert(torii_stream_decoder_push(&stream, 0, &decoded) == TORII_STREAM_NONE);
    for (size_t i = 0; i < encoded_length; ++i) {
        const torii_stream_result_t result =
            torii_stream_decoder_push(&stream, encoded[i], &decoded);
        if (i + 1 == encoded_length) {
            assert(result == TORII_STREAM_FRAME);
        } else {
            assert(result == TORII_STREAM_NONE);
        }
    }
    assert(decoded.type == source.type);
    assert(decoded.device_id == source.device_id);
    assert(decoded.sequence == source.sequence);
    assert(decoded.payload_len == source.payload_len);
    assert(memcmp(decoded.payload, source.payload, source.payload_len) == 0);
}

static void test_raw_hid_report_round_trip(void)
{
    static const uint8_t payload[] = {0x01, 0x80, 0x7f, 0x00, 0x22};
    uint8_t encoded[TORII_MAX_ENCODED_SIZE];
    size_t encoded_length = 0;
    torii_frame_t source = make_frame(
        TORII_MSG_HID_REPORT, 2, 0x42, payload, sizeof(payload));
    torii_frame_t decoded = {0};

    assert(torii_encode_frame(
        &source, encoded, sizeof(encoded), &encoded_length) == TORII_CODEC_OK);
    assert(torii_decode_packet(
        encoded, encoded_length - 1, &decoded) == TORII_CODEC_OK);
    assert(decoded.type == TORII_MSG_HID_REPORT);
    assert(decoded.device_id == 2);
    assert(decoded.payload_len == sizeof(payload));
    assert(memcmp(decoded.payload, payload, sizeof(payload)) == 0);
}

static void test_xbox360_report_round_trip(void)
{
    static const uint8_t payload[] = {
        0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };
    uint8_t encoded[TORII_MAX_ENCODED_SIZE];
    size_t encoded_length = 0;
    torii_frame_t source = make_frame(
        TORII_MSG_XBOX360_REPORT, 3, 0x43, payload, sizeof(payload));
    torii_frame_t decoded = {0};

    assert(torii_encode_frame(
        &source, encoded, sizeof(encoded), &encoded_length) == TORII_CODEC_OK);
    assert(torii_decode_packet(
        encoded, encoded_length - 1, &decoded) == TORII_CODEC_OK);
    assert(decoded.type == TORII_MSG_XBOX360_REPORT);
    assert(decoded.device_id == 3);
    assert(decoded.payload_len == sizeof(payload));
    assert(memcmp(decoded.payload, payload, sizeof(payload)) == 0);
}

static void test_maximum_payload_frame(void)
{
    uint8_t encoded[TORII_MAX_ENCODED_SIZE];
    size_t encoded_length = 0;
    torii_frame_t source = make_frame(0x55, 1, 0xabcd, NULL, 0);
    torii_frame_t decoded = {0};
    source.payload_len = TORII_MAX_PAYLOAD_SIZE;
    memset(source.payload, 0, sizeof(source.payload));

    assert(torii_encode_frame(
        &source, encoded, sizeof(encoded), &encoded_length) == TORII_CODEC_OK);
    assert(encoded_length <= sizeof(encoded));
    assert(encoded[encoded_length - 1] == 0);
    assert(torii_decode_packet(
        encoded, encoded_length - 1, &decoded) == TORII_CODEC_OK);
    assert(decoded.payload_len == TORII_MAX_PAYLOAD_SIZE);
    assert(memcmp(decoded.payload, source.payload, TORII_MAX_PAYLOAD_SIZE) == 0);
}

static void test_malformed_crc_resynchronizes(void)
{
    uint8_t encoded[TORII_MAX_ENCODED_SIZE];
    size_t encoded_length = 0;
    torii_frame_t frame = make_frame(TORII_MSG_READY, 0, 1, NULL, 0);
    torii_frame_t decoded = {0};
    torii_stream_decoder_t stream;

    assert(torii_encode_frame(
        &frame, encoded, sizeof(encoded), &encoded_length) == TORII_CODEC_OK);
    encoded[encoded_length - 2] ^= 0x01;
    torii_stream_decoder_init(&stream);
    for (size_t i = 0; i < encoded_length; ++i) {
        const torii_stream_result_t result =
            torii_stream_decoder_push(&stream, encoded[i], &decoded);
        if (i + 1 == encoded_length) {
            assert(result == TORII_STREAM_MALFORMED);
        }
    }

    frame.sequence++;
    assert(torii_encode_frame(
        &frame, encoded, sizeof(encoded), &encoded_length) == TORII_CODEC_OK);
    for (size_t i = 0; i < encoded_length; ++i) {
        const torii_stream_result_t result =
            torii_stream_decoder_push(&stream, encoded[i], &decoded);
        if (i + 1 == encoded_length) {
            assert(result == TORII_STREAM_FRAME);
        }
    }
}

static void test_bad_lengths_and_unknown_types(void)
{
    uint8_t encoded[TORII_MAX_ENCODED_SIZE];
    size_t encoded_length = 0;
    torii_frame_t frame = make_frame(TORII_MSG_READY, 0, 0, NULL, 0);
    torii_frame_t decoded = {0};

    frame.device_id = 1;
    assert(torii_encode_frame(
        &frame, encoded, sizeof(encoded), &encoded_length) ==
        TORII_CODEC_INVALID_PACKET);

    frame = make_frame(0x55, 1, 7, NULL, 0);
    assert(torii_encode_frame(
        &frame, encoded, sizeof(encoded), &encoded_length) == TORII_CODEC_OK);
    assert(torii_decode_packet(
        encoded, encoded_length - 1, &decoded) == TORII_CODEC_OK);
    assert(decoded.type == 0x55);

    encoded[0] = 0;
    assert(torii_decode_packet(
        encoded, encoded_length - 1, &decoded) == TORII_CODEC_MALFORMED_COBS);
}

static void test_state_gap_recovery_and_disconnect(void)
{
    static const uint8_t connected_payload[] = {0x03, 0x34, 0x12, 0x78, 0x56};
    static const uint8_t keyboard_payload[] = {
        0x02, 0, 0x04, 0, 0, 0, 0, 0
    };
    torii_host_state_t state;
    torii_host_state_init(&state);

    torii_frame_t frame = make_frame(TORII_MSG_READY, 0, 10, NULL, 0);
    assert(torii_host_state_apply(&state, &frame, 0) == TORII_STATE_APPLIED);

    frame = make_frame(
        TORII_MSG_DEVICE_CONNECTED, 1, 11,
        connected_payload, sizeof(connected_payload));
    assert(torii_host_state_apply(&state, &frame, 10) == TORII_STATE_APPLIED);
    assert(state.connected);
    assert(state.vendor_id == 0x1234);
    assert(state.product_id == 0x5678);

    frame = make_frame(
        TORII_MSG_KEYBOARD_STATE, 1, 13,
        keyboard_payload, sizeof(keyboard_payload));
    assert(torii_host_state_apply(&state, &frame, 20) ==
           TORII_STATE_SEQUENCE_GAP);
    assert(state.modifiers == 0x02);
    assert(state.keys[0] == 0x04);

    frame.sequence = 14;
    frame.payload[2] = 0x01;
    assert(torii_host_state_apply(&state, &frame, 30) == TORII_STATE_APPLIED);
    assert(state.keys[0] == 0);

    frame = make_frame(TORII_MSG_DEVICE_DISCONNECTED, 1, 15, NULL, 0);
    assert(torii_host_state_apply(&state, &frame, 40) == TORII_STATE_APPLIED);
    assert(!state.connected);
    assert(state.modifiers == 0);
    assert(state.keys[0] == 0);
}

static void test_sequence_wrap_and_stale_state_timeout(void)
{
    static const uint8_t connected_payload[] = {0x03, 0x34, 0x12, 0x78, 0x56};
    static const uint8_t keyboard_payload[] = {
        0x01, 0, 0x04, 0, 0, 0, 0, 0
    };
    torii_host_state_t state;
    torii_host_state_init(&state);

    torii_frame_t frame = make_frame(
        TORII_MSG_READY, 0, 0xfffeu, NULL, 0);
    assert(torii_host_state_apply(&state, &frame, 0xfffffff0u) ==
           TORII_STATE_APPLIED);
    frame = make_frame(
        TORII_MSG_DEVICE_CONNECTED, 1, 0xffffu,
        connected_payload, sizeof(connected_payload));
    assert(torii_host_state_apply(&state, &frame, 0xfffffff0u) ==
           TORII_STATE_APPLIED);
    frame = make_frame(
        TORII_MSG_KEYBOARD_STATE, 1, 0,
        keyboard_payload, sizeof(keyboard_payload));
    assert(torii_host_state_apply(&state, &frame, 0xfffffff0u) ==
           TORII_STATE_APPLIED);
    assert(state.keys[0] == 0x04);

    assert(!torii_host_state_expire(&state, 0x000003d7u, 1000));
    assert(torii_host_state_expire(&state, 0x000003d8u, 1000));
    assert(!state.connected);
    assert(state.modifiers == 0);
    assert(state.keys[0] == 0);
    assert(!state.have_activity_timestamp);

    frame.sequence = 1;
    assert(torii_host_state_apply(&state, &frame, 0x000003d9u) ==
           TORII_STATE_APPLIED);
    assert(state.connected);
    assert(state.device_id == 1);
    assert(state.vendor_id == 0);
    assert(state.keys[0] == 0x04);
}

static void test_host_state_emits_key_edges(void)
{
    static const uint8_t connected_payload[] = {0x03, 0x34, 0x12, 0x78, 0x56};
    static const uint8_t first_keyboard_payload[] = {
        0x02, 0, 0x04, 0x05, 0, 0, 0, 0
    };
    static const uint8_t reordered_keyboard_payload[] = {
        0x02, 0, 0x05, 0x04, 0, 0, 0, 0
    };
    static const uint8_t changed_keyboard_payload[] = {
        0x03, 0, 0x05, 0x06, 0, 0, 0, 0
    };
    torii_host_state_t state;
    torii_host_state_t previous;
    torii_input_events_t events;
    torii_host_state_init(&state);

    torii_frame_t frame = make_frame(
        TORII_MSG_DEVICE_CONNECTED, 1, 1,
        connected_payload, sizeof(connected_payload));
    previous = state;
    assert(torii_host_state_apply(&state, &frame, 1) == TORII_STATE_APPLIED);
    torii_host_state_diff(&previous, &state, &events);
    assert(events.device_connected);

    frame = make_frame(
        TORII_MSG_KEYBOARD_STATE, 1, 2,
        first_keyboard_payload, sizeof(first_keyboard_payload));
    previous = state;
    assert(torii_host_state_apply(&state, &frame, 2) == TORII_STATE_APPLIED);
    torii_host_state_diff(&previous, &state, &events);
    assert(events.modifiers_pressed == 0x02);
    assert(events.keys_pressed_count == 2);
    assert(events.keys_released_count == 0);

    frame = make_frame(
        TORII_MSG_KEYBOARD_STATE, 1, 3,
        reordered_keyboard_payload, sizeof(reordered_keyboard_payload));
    previous = state;
    assert(torii_host_state_apply(&state, &frame, 3) == TORII_STATE_APPLIED);
    torii_host_state_diff(&previous, &state, &events);
    assert(events.keys_pressed_count == 0);
    assert(events.keys_released_count == 0);

    frame = make_frame(
        TORII_MSG_KEYBOARD_STATE, 1, 4,
        changed_keyboard_payload, sizeof(changed_keyboard_payload));
    previous = state;
    assert(torii_host_state_apply(&state, &frame, 4) == TORII_STATE_APPLIED);
    torii_host_state_diff(&previous, &state, &events);
    assert(events.modifiers_pressed == 0x01);
    assert(events.modifiers_released == 0);
    assert(events.keys_pressed_count == 1);
    assert(events.keys_pressed[0] == 0x06);
    assert(events.keys_released_count == 1);
    assert(events.keys_released[0] == 0x04);

    frame = make_frame(TORII_MSG_DEVICE_DISCONNECTED, 1, 5, NULL, 0);
    previous = state;
    assert(torii_host_state_apply(&state, &frame, 5) == TORII_STATE_APPLIED);
    torii_host_state_diff(&previous, &state, &events);
    assert(events.device_disconnected);
    assert(events.modifiers_released == 0x03);
    assert(events.keys_released_count == 2);
}

static void test_stream_oversize_discards_to_delimiter(void)
{
    torii_stream_decoder_t stream;
    torii_frame_t decoded = {0};
    torii_stream_decoder_init(&stream);

    for (size_t i = 0; i < TORII_MAX_COBS_SIZE + 1u; ++i) {
        assert(torii_stream_decoder_push(&stream, 0x7f, &decoded) ==
               TORII_STREAM_NONE);
    }
    assert(torii_stream_decoder_push(&stream, 0, &decoded) ==
           TORII_STREAM_MALFORMED);
    assert(torii_stream_decoder_push(&stream, 0, &decoded) ==
           TORII_STREAM_NONE);
}

int main(void)
{
    test_crc_vector();
    test_golden_ready_packet();
    test_output_capacity();
    test_round_trip_and_stream_recovery();
    test_raw_hid_report_round_trip();
    test_xbox360_report_round_trip();
    test_maximum_payload_frame();
    test_malformed_crc_resynchronizes();
    test_bad_lengths_and_unknown_types();
    test_state_gap_recovery_and_disconnect();
    test_sequence_wrap_and_stale_state_timeout();
    test_host_state_emits_key_edges();
    test_stream_oversize_discards_to_delimiter();
    puts("TORII protocol tests passed");
    return 0;
}
