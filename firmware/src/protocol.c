#include "torii_protocol.h"

#include <string.h>

#define TORII_HEADER_SIZE 6u
#define TORII_CRC_SIZE 2u
#define HID_KEY_ERROR_ROLLOVER 0x01u

static void clear_keyboard_state(torii_host_state_t *state)
{
    state->connected = false;
    state->device_id = 0;
    state->device_class = 0;
    state->vendor_id = 0;
    state->product_id = 0;
    state->modifiers = 0;
    memset(state->keys, 0, sizeof(state->keys));
    state->have_activity_timestamp = false;
    state->last_activity_ms = 0;
}

static bool frame_payload_is_valid(const torii_frame_t *frame)
{
    switch (frame->type) {
    case TORII_MSG_READY:
        return frame->device_id == 0 && frame->payload_len == 0;
    case TORII_MSG_DEVICE_CONNECTED:
        return frame->device_id != 0 && frame->payload_len == 5;
    case TORII_MSG_DEVICE_DISCONNECTED:
        return frame->device_id != 0 && frame->payload_len == 0;
    case TORII_MSG_KEYBOARD_STATE:
        return frame->device_id != 0 && frame->payload_len == 8 &&
               frame->payload[1] == 0;
    case TORII_MSG_ERROR:
        return frame->payload_len == 3;
    default:
        return true;
    }
}

uint16_t torii_crc16_ccitt_false(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xffffu;

    if (data == NULL && length != 0) {
        return 0;
    }

    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000u) != 0
                ? (uint16_t)((crc << 1) ^ 0x1021u)
                : (uint16_t)(crc << 1);
        }
    }

    return crc;
}

static size_t cobs_encode(
    const uint8_t *input,
    size_t input_length,
    uint8_t *output)
{
    size_t code_index = 0;
    size_t output_index = 1;
    uint8_t code = 1;

    for (size_t i = 0; i < input_length; ++i) {
        if (input[i] == 0) {
            output[code_index] = code;
            code_index = output_index++;
            code = 1;
        } else {
            output[output_index++] = input[i];
            ++code;
            if (code == 0xffu) {
                output[code_index] = code;
                code_index = output_index++;
                code = 1;
            }
        }
    }

    output[code_index] = code;
    return output_index;
}

static torii_codec_result_t cobs_decode(
    const uint8_t *input,
    size_t input_length,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length)
{
    size_t input_index = 0;
    size_t output_index = 0;

    if (input_length == 0) {
        return TORII_CODEC_MALFORMED_COBS;
    }

    while (input_index < input_length) {
        const uint8_t code = input[input_index++];
        const size_t copy_length = (size_t)code - 1u;

        if (code == 0 || copy_length > input_length - input_index) {
            return TORII_CODEC_MALFORMED_COBS;
        }
        if (copy_length > output_capacity - output_index) {
            return TORII_CODEC_INVALID_PACKET;
        }

        memcpy(output + output_index, input + input_index, copy_length);
        input_index += copy_length;
        output_index += copy_length;

        if (code != 0xffu && input_index < input_length) {
            if (output_index >= output_capacity) {
                return TORII_CODEC_INVALID_PACKET;
            }
            output[output_index++] = 0;
        }
    }

    *output_length = output_index;
    return TORII_CODEC_OK;
}

torii_codec_result_t torii_encode_frame(
    const torii_frame_t *frame,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length)
{
    uint8_t packet[TORII_MAX_PACKET_SIZE];
    uint8_t encoded[TORII_MAX_COBS_SIZE];
    size_t packet_length;
    size_t encoded_length;

    if (frame == NULL || output == NULL || output_length == NULL) {
        return TORII_CODEC_INVALID_ARGUMENT;
    }
    if (frame->version != TORII_PROTOCOL_VERSION ||
        frame->payload_len > TORII_MAX_PAYLOAD_SIZE ||
        !frame_payload_is_valid(frame)) {
        return TORII_CODEC_INVALID_PACKET;
    }

    packet[0] = frame->version;
    packet[1] = frame->type;
    packet[2] = frame->device_id;
    packet[3] = (uint8_t)(frame->sequence & 0xffu);
    packet[4] = (uint8_t)(frame->sequence >> 8);
    packet[5] = frame->payload_len;
    memcpy(packet + TORII_HEADER_SIZE, frame->payload, frame->payload_len);

    packet_length = TORII_HEADER_SIZE + frame->payload_len;
    const uint16_t crc = torii_crc16_ccitt_false(packet, packet_length);
    packet[packet_length++] = (uint8_t)(crc & 0xffu);
    packet[packet_length++] = (uint8_t)(crc >> 8);

    encoded_length = cobs_encode(packet, packet_length, encoded);
    if (encoded_length + 1u > output_capacity) {
        return TORII_CODEC_OUTPUT_TOO_SMALL;
    }

    memcpy(output, encoded, encoded_length);
    output[encoded_length++] = 0;
    *output_length = encoded_length;
    return TORII_CODEC_OK;
}

torii_codec_result_t torii_decode_packet(
    const uint8_t *encoded,
    size_t encoded_length,
    torii_frame_t *frame)
{
    uint8_t packet[TORII_MAX_PACKET_SIZE];
    size_t packet_length = 0;
    torii_codec_result_t result;

    if (encoded == NULL || frame == NULL) {
        return TORII_CODEC_INVALID_ARGUMENT;
    }
    if (encoded_length > TORII_MAX_COBS_SIZE) {
        return TORII_CODEC_INVALID_PACKET;
    }

    result = cobs_decode(
        encoded, encoded_length, packet, sizeof(packet), &packet_length);
    if (result != TORII_CODEC_OK) {
        return result;
    }
    if (packet_length < TORII_HEADER_SIZE + TORII_CRC_SIZE) {
        return TORII_CODEC_INVALID_PACKET;
    }

    const uint8_t payload_length = packet[5];
    if (payload_length > TORII_MAX_PAYLOAD_SIZE ||
        packet_length != TORII_HEADER_SIZE + payload_length + TORII_CRC_SIZE) {
        return TORII_CODEC_INVALID_PACKET;
    }

    const size_t data_length = TORII_HEADER_SIZE + payload_length;
    const uint16_t expected_crc = (uint16_t)packet[data_length] |
        ((uint16_t)packet[data_length + 1u] << 8);
    if (torii_crc16_ccitt_false(packet, data_length) != expected_crc) {
        return TORII_CODEC_INVALID_PACKET;
    }

    frame->version = packet[0];
    frame->type = packet[1];
    frame->device_id = packet[2];
    frame->sequence = (uint16_t)packet[3] |
        ((uint16_t)packet[4] << 8);
    frame->payload_len = payload_length;
    memcpy(frame->payload, packet + TORII_HEADER_SIZE, payload_length);

    if (frame->version != TORII_PROTOCOL_VERSION ||
        !frame_payload_is_valid(frame)) {
        return TORII_CODEC_INVALID_PACKET;
    }

    return TORII_CODEC_OK;
}

void torii_stream_decoder_init(torii_stream_decoder_t *decoder)
{
    if (decoder != NULL) {
        decoder->encoded_len = 0;
        decoder->dropping = false;
    }
}

torii_stream_result_t torii_stream_decoder_push(
    torii_stream_decoder_t *decoder,
    uint8_t byte,
    torii_frame_t *frame)
{
    if (decoder == NULL || frame == NULL) {
        return TORII_STREAM_MALFORMED;
    }

    if (byte == 0) {
        if (decoder->dropping) {
            decoder->dropping = false;
            decoder->encoded_len = 0;
            return TORII_STREAM_MALFORMED;
        }
        if (decoder->encoded_len == 0) {
            return TORII_STREAM_NONE;
        }

        const torii_codec_result_t result = torii_decode_packet(
            decoder->encoded, decoder->encoded_len, frame);
        decoder->encoded_len = 0;
        return result == TORII_CODEC_OK
            ? TORII_STREAM_FRAME
            : TORII_STREAM_MALFORMED;
    }

    if (decoder->dropping) {
        return TORII_STREAM_NONE;
    }
    if (decoder->encoded_len == sizeof(decoder->encoded)) {
        decoder->encoded_len = 0;
        decoder->dropping = true;
        return TORII_STREAM_NONE;
    }

    decoder->encoded[decoder->encoded_len++] = byte;
    return TORII_STREAM_NONE;
}

void torii_host_state_init(torii_host_state_t *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }
}

torii_state_result_t torii_host_state_apply(
    torii_host_state_t *state,
    const torii_frame_t *frame,
    uint32_t now_ms)
{
    bool sequence_gap = false;

    if (state == NULL || frame == NULL ||
        frame->version != TORII_PROTOCOL_VERSION ||
        frame->payload_len > TORII_MAX_PAYLOAD_SIZE ||
        !frame_payload_is_valid(frame)) {
        return TORII_STATE_INVALID;
    }

    if (state->have_sequence) {
        const uint16_t expected = (uint16_t)(state->last_sequence + 1u);
        sequence_gap = frame->sequence != expected;
    }
    state->have_sequence = true;
    state->last_sequence = frame->sequence;

    switch (frame->type) {
    case TORII_MSG_READY:
        clear_keyboard_state(state);
        break;
    case TORII_MSG_DEVICE_CONNECTED:
        clear_keyboard_state(state);
        state->connected = true;
        state->device_id = frame->device_id;
        state->device_class = frame->payload[0];
        state->vendor_id = (uint16_t)frame->payload[1] |
            ((uint16_t)frame->payload[2] << 8);
        state->product_id = (uint16_t)frame->payload[3] |
            ((uint16_t)frame->payload[4] << 8);
        state->have_activity_timestamp = true;
        state->last_activity_ms = now_ms;
        break;
    case TORII_MSG_DEVICE_DISCONNECTED:
        if (state->connected && state->device_id == frame->device_id) {
            clear_keyboard_state(state);
        }
        break;
    case TORII_MSG_KEYBOARD_STATE:
        if (state->connected && state->device_id != frame->device_id) {
            return sequence_gap
                ? TORII_STATE_SEQUENCE_GAP
                : TORII_STATE_IGNORED;
        }
        if (!state->connected) {
            state->connected = true;
            state->device_id = frame->device_id;
            state->device_class = 3;
        }
        state->modifiers = frame->payload[0];
        memcpy(state->keys, frame->payload + 2, sizeof(state->keys));
        state->have_activity_timestamp = true;
        state->last_activity_ms = now_ms;
        for (size_t i = 0; i < TORII_KEY_COUNT; ++i) {
            if (state->keys[i] == HID_KEY_ERROR_ROLLOVER) {
                memset(state->keys, 0, sizeof(state->keys));
                break;
            }
        }
        break;
    case TORII_MSG_ERROR:
        break;
    default:
        return sequence_gap
            ? TORII_STATE_SEQUENCE_GAP
            : TORII_STATE_UNSUPPORTED;
    }

    return sequence_gap ? TORII_STATE_SEQUENCE_GAP : TORII_STATE_APPLIED;
}

bool torii_host_state_expire(
    torii_host_state_t *state,
    uint32_t now_ms,
    uint32_t timeout_ms)
{
    if (state == NULL || !state->connected || !state->have_activity_timestamp ||
        timeout_ms == 0 ||
        (uint32_t)(now_ms - state->last_activity_ms) < timeout_ms) {
        return false;
    }

    clear_keyboard_state(state);
    return true;
}

static bool key_present(
    const uint8_t keys[TORII_KEY_COUNT],
    uint8_t key)
{
    if (key == 0) {
        return false;
    }
    for (size_t i = 0; i < TORII_KEY_COUNT; ++i) {
        if (keys[i] == key) {
            return true;
        }
    }
    return false;
}

void torii_host_state_diff(
    const torii_host_state_t *before,
    const torii_host_state_t *after,
    torii_input_events_t *events)
{
    if (before == NULL || after == NULL || events == NULL) {
        return;
    }

    memset(events, 0, sizeof(*events));
    const bool device_changed =
        before->connected && after->connected &&
        before->device_id != after->device_id;
    events->device_connected =
        after->connected && (!before->connected || device_changed);
    events->device_disconnected =
        before->connected && (!after->connected || device_changed);
    events->modifiers_pressed =
        (uint8_t)(after->modifiers & (uint8_t)~before->modifiers);
    events->modifiers_released =
        (uint8_t)(before->modifiers & (uint8_t)~after->modifiers);

    for (size_t i = 0; i < TORII_KEY_COUNT; ++i) {
        const uint8_t key = before->keys[i];
        if (key != 0 && !key_present(after->keys, key) &&
            !key_present(events->keys_released, key)) {
            events->keys_released[events->keys_released_count++] = key;
        }
    }
    for (size_t i = 0; i < TORII_KEY_COUNT; ++i) {
        const uint8_t key = after->keys[i];
        if (key != 0 && !key_present(before->keys, key) &&
            !key_present(events->keys_pressed, key)) {
            events->keys_pressed[events->keys_pressed_count++] = key;
        }
    }
}
