#include "uart_transport.h"

#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "pico/critical_section.h"

#include <string.h>

#define TORII_UART uart0
#define TORII_UART_BAUD 115200u
#define TORII_UART_TX_PIN 0u
#define TORII_UART_RX_PIN 1u
#define TORII_TX_QUEUE_CAPACITY 8u

typedef struct {
    uint8_t bytes[TORII_MAX_ENCODED_SIZE];
    size_t length;
} tx_packet_t;

static critical_section_t queue_lock;
static tx_packet_t queue[TORII_TX_QUEUE_CAPACITY];
static uint8_t queue_head;
static uint8_t queue_tail;
static uint16_t dropped_packets;

static uint8_t active_packet[TORII_MAX_ENCODED_SIZE];
static size_t active_length;
static size_t active_offset;

void uart_transport_init(void)
{
    critical_section_init(&queue_lock);
    queue_head = 0;
    queue_tail = 0;
    dropped_packets = 0;
    active_length = 0;
    active_offset = 0;

    uart_init(TORII_UART, TORII_UART_BAUD);
    gpio_set_function(TORII_UART_TX_PIN, UART_FUNCSEL_NUM(TORII_UART, TORII_UART_TX_PIN));
    gpio_set_function(TORII_UART_RX_PIN, UART_FUNCSEL_NUM(TORII_UART, TORII_UART_RX_PIN));
    uart_set_format(TORII_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(TORII_UART, true);
}

bool uart_transport_send(const torii_frame_t *frame)
{
    uint8_t encoded[TORII_MAX_ENCODED_SIZE];
    size_t encoded_length = 0;

    if (frame == NULL ||
        torii_encode_frame(
            frame, encoded, sizeof(encoded), &encoded_length) != TORII_CODEC_OK) {
        return false;
    }

    critical_section_enter_blocking(&queue_lock);
    const uint8_t next_tail =
        (uint8_t)((queue_tail + 1u) % TORII_TX_QUEUE_CAPACITY);
    if (next_tail == queue_head) {
        if (dropped_packets != UINT16_MAX) {
            ++dropped_packets;
        }
        critical_section_exit(&queue_lock);
        return false;
    }

    memcpy(queue[queue_tail].bytes, encoded, encoded_length);
    queue[queue_tail].length = encoded_length;
    queue_tail = next_tail;
    critical_section_exit(&queue_lock);
    return true;
}

static bool start_next_packet(void)
{
    bool available = false;

    critical_section_enter_blocking(&queue_lock);
    if (queue_head != queue_tail) {
        active_length = queue[queue_head].length;
        memcpy(active_packet, queue[queue_head].bytes, active_length);
        active_offset = 0;
        queue_head =
            (uint8_t)((queue_head + 1u) % TORII_TX_QUEUE_CAPACITY);
        available = true;
    }
    critical_section_exit(&queue_lock);
    return available;
}

void uart_transport_service(void)
{
    if (active_offset == active_length && !start_next_packet()) {
        return;
    }

    while (active_offset < active_length && uart_is_writable(TORII_UART)) {
        uart_putc_raw(TORII_UART, active_packet[active_offset++]);
    }
}

uint16_t uart_transport_dropped_packets(void)
{
    uint16_t count;
    critical_section_enter_blocking(&queue_lock);
    count = dropped_packets;
    critical_section_exit(&queue_lock);
    return count;
}
