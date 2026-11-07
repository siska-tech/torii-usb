#ifndef TORII_PROTOCOL_H
#define TORII_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TORII_PROTOCOL_VERSION 1u
#define TORII_MAX_PAYLOAD_SIZE 32u
#define TORII_MAX_PACKET_SIZE 40u
#define TORII_MAX_COBS_SIZE 41u
#define TORII_MAX_ENCODED_SIZE 42u
#define TORII_KEY_COUNT 6u
#define TORII_HOST_STATE_TIMEOUT_MS 1000u

typedef enum {
    TORII_MSG_READY = 0x01,
    TORII_MSG_DEVICE_CONNECTED = 0x10,
    TORII_MSG_DEVICE_DISCONNECTED = 0x11,
    TORII_MSG_USB_DEVICE_STATUS = 0x12,
    TORII_MSG_KEYBOARD_STATE = 0x20,
    TORII_MSG_HID_REPORT = 0x21,
    TORII_MSG_XBOX360_REPORT = 0x22,
    TORII_MSG_ERROR = 0x7f
} torii_message_type_t;

typedef struct {
    uint8_t version;
    uint8_t type;
    uint8_t device_id;
    uint16_t sequence;
    uint8_t payload_len;
    uint8_t payload[TORII_MAX_PAYLOAD_SIZE];
} torii_frame_t;

typedef enum {
    TORII_CODEC_OK = 0,
    TORII_CODEC_INVALID_ARGUMENT,
    TORII_CODEC_OUTPUT_TOO_SMALL,
    TORII_CODEC_MALFORMED_COBS,
    TORII_CODEC_INVALID_PACKET
} torii_codec_result_t;

typedef struct {
    uint8_t encoded[TORII_MAX_COBS_SIZE];
    size_t encoded_len;
    bool dropping;
} torii_stream_decoder_t;

typedef enum {
    TORII_STREAM_NONE = 0,
    TORII_STREAM_FRAME,
    TORII_STREAM_MALFORMED
} torii_stream_result_t;

typedef struct {
    bool have_sequence;
    uint16_t last_sequence;
    bool connected;
    uint8_t device_id;
    uint8_t device_class;
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t modifiers;
    uint8_t keys[TORII_KEY_COUNT];
    bool have_activity_timestamp;
    uint32_t last_activity_ms;
} torii_host_state_t;

typedef struct {
    uint8_t modifiers_pressed;
    uint8_t modifiers_released;
    uint8_t keys_pressed[TORII_KEY_COUNT];
    uint8_t keys_released[TORII_KEY_COUNT];
    uint8_t keys_pressed_count;
    uint8_t keys_released_count;
    bool device_connected;
    bool device_disconnected;
} torii_input_events_t;

typedef enum {
    TORII_STATE_APPLIED = 0,
    TORII_STATE_SEQUENCE_GAP,
    TORII_STATE_IGNORED,
    TORII_STATE_UNSUPPORTED,
    TORII_STATE_INVALID
} torii_state_result_t;

uint16_t torii_crc16_ccitt_false(const uint8_t *data, size_t length);

torii_codec_result_t torii_encode_frame(
    const torii_frame_t *frame,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length);

torii_codec_result_t torii_decode_packet(
    const uint8_t *encoded,
    size_t encoded_length,
    torii_frame_t *frame);

void torii_stream_decoder_init(torii_stream_decoder_t *decoder);
torii_stream_result_t torii_stream_decoder_push(
    torii_stream_decoder_t *decoder,
    uint8_t byte,
    torii_frame_t *frame);

void torii_host_state_init(torii_host_state_t *state);
torii_state_result_t torii_host_state_apply(
    torii_host_state_t *state,
    const torii_frame_t *frame,
    uint32_t now_ms);
bool torii_host_state_expire(
    torii_host_state_t *state,
    uint32_t now_ms,
    uint32_t timeout_ms);
void torii_host_state_diff(
    const torii_host_state_t *before,
    const torii_host_state_t *after,
    torii_input_events_t *events);

#endif
