#ifndef TORII_UART_TRANSPORT_H
#define TORII_UART_TRANSPORT_H

#include "torii_protocol.h"

#include <stdbool.h>
#include <stdint.h>

void uart_transport_init(void);
bool uart_transport_send(const torii_frame_t *frame);
void uart_transport_service(void);
uint16_t uart_transport_dropped_packets(void);

#endif
