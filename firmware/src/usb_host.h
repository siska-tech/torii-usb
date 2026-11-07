#ifndef TORII_USB_HOST_H
#define TORII_USB_HOST_H

#include <stdbool.h>
#include <stdint.h>

void usb_host_diagnostic_line(const char *line);
void usb_host_send_device_connected(
    uint8_t device_id,
    uint16_t vendor_id,
    uint16_t product_id);
void usb_host_send_device_connected_class(
    uint8_t device_id,
    uint8_t device_class,
    uint16_t vendor_id,
    uint16_t product_id);
void usb_host_send_device_disconnected(uint8_t device_id);
bool usb_host_send_hid_report(
    uint8_t device_id,
    const uint8_t *report,
    uint8_t report_length);
bool usb_host_send_xbox360_report(
    uint8_t device_id,
    const uint8_t *report,
    uint8_t report_length);
void usb_host_diagnostics_init(void);
void usb_host_diagnostics_service(void);
void usb_host_task(void);

#endif
