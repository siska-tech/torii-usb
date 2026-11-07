#include "usb_host.h"

#include "tusb.h"
#include "host/usbh_pvt.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ONZA_VENDOR_ID 0x1689u
#define ONZA_TOURNAMENT_PRODUCT_ID 0xfd00u
#define ONZA_DEVICE_ID 3u
#define XBOX360_INPUT_REPORT_SIZE 20u
#define XBOX360_INPUT_BUFFER_SIZE 64u
#define XBOX360_LED_REPORT_SIZE 3u

static bool onza_mounted;
static uint8_t onza_device_address;
static uint8_t onza_interface_number;
static uint8_t onza_endpoint_in;
static uint8_t onza_endpoint_out;
static uint16_t onza_input_packet_size;
static uint8_t onza_input_report[XBOX360_INPUT_BUFFER_SIZE];
static uint8_t onza_led_report[XBOX360_LED_REPORT_SIZE] = {0x01, 0x03, 0x06};
static uint8_t onza_last_report[XBOX360_INPUT_REPORT_SIZE];
static bool onza_have_last_report;

static bool onza_start_input_transfer(void)
{
    if (!usbh_edpt_claim(onza_device_address, onza_endpoint_in)) {
        return false;
    }
    if (!usbh_edpt_xfer(
            onza_device_address,
            onza_endpoint_in,
            onza_input_report,
            onza_input_packet_size)) {
        (void)usbh_edpt_release(onza_device_address, onza_endpoint_in);
        return false;
    }
    return true;
}

static bool onza_send_led_report(void)
{
    if (!usbh_edpt_claim(onza_device_address, onza_endpoint_out)) {
        return false;
    }
    if (!usbh_edpt_xfer(
            onza_device_address,
            onza_endpoint_out,
            onza_led_report,
            sizeof(onza_led_report))) {
        (void)usbh_edpt_release(onza_device_address, onza_endpoint_out);
        return false;
    }
    return true;
}

static bool onza_driver_init(void)
{
    onza_mounted = false;
    return true;
}

static bool onza_driver_deinit(void)
{
    return true;
}

static bool onza_driver_open(
    uint8_t rhport,
    uint8_t dev_addr,
    tusb_desc_interface_t const *interface_desc,
    uint16_t max_len)
{
    (void)rhport;

    uint16_t vendor_id = 0;
    uint16_t product_id = 0;
    if (!tuh_vid_pid_get(dev_addr, &vendor_id, &product_id) ||
        vendor_id != ONZA_VENDOR_ID ||
        product_id != ONZA_TOURNAMENT_PRODUCT_ID ||
        interface_desc->bInterfaceNumber != 0 ||
        interface_desc->bInterfaceClass != TUSB_CLASS_VENDOR_SPECIFIC ||
        interface_desc->bNumEndpoints < 2) {
        return false;
    }

    uint16_t offset = interface_desc->bLength;
    uint8_t endpoint_in = 0;
    uint8_t endpoint_out = 0;
    uint16_t input_packet_size = 0;
    uint16_t remaining_endpoints = interface_desc->bNumEndpoints;

    while (offset + 2u <= max_len && remaining_endpoints != 0) {
        const uint8_t *current =
            (const uint8_t *)interface_desc + offset;
        const uint8_t length = current[0];
        if (length < 2u || offset + length > max_len) {
            return false;
        }
        if (current[1] == TUSB_DESC_INTERFACE) {
            break;
        }
        if (current[1] == TUSB_DESC_ENDPOINT) {
            if (length < sizeof(tusb_desc_endpoint_t)) {
                return false;
            }
            const tusb_desc_endpoint_t *endpoint =
                (const tusb_desc_endpoint_t *)current;
            if (endpoint->bmAttributes.xfer != TUSB_XFER_INTERRUPT) {
                return false;
            }
            if (tu_edpt_dir(endpoint->bEndpointAddress) == TUSB_DIR_IN) {
                if (endpoint_in != 0 ||
                    tu_edpt_packet_size(endpoint) < XBOX360_INPUT_REPORT_SIZE ||
                    tu_edpt_packet_size(endpoint) > XBOX360_INPUT_BUFFER_SIZE) {
                    return false;
                }
                endpoint_in = endpoint->bEndpointAddress;
                input_packet_size = tu_edpt_packet_size(endpoint);
            } else {
                if (endpoint_out != 0 ||
                    tu_edpt_packet_size(endpoint) < XBOX360_LED_REPORT_SIZE) {
                    return false;
                }
                endpoint_out = endpoint->bEndpointAddress;
            }
            --remaining_endpoints;
        }
        offset += length;
    }

    if (remaining_endpoints != 0 || endpoint_in == 0 || endpoint_out == 0) {
        return false;
    }

    const uint8_t *endpoint_desc =
        (const uint8_t *)interface_desc + interface_desc->bLength;
    uint8_t opened_endpoints = 0;
    while (endpoint_desc < (const uint8_t *)interface_desc + max_len) {
        if (endpoint_desc[0] < 2u) {
            return false;
        }
        if (endpoint_desc[1] == TUSB_DESC_INTERFACE) {
            break;
        }
        if (endpoint_desc[1] == TUSB_DESC_ENDPOINT) {
            const tusb_desc_endpoint_t *endpoint =
                (const tusb_desc_endpoint_t *)endpoint_desc;
            if (!tuh_edpt_open(dev_addr, endpoint)) {
                return false;
            }
            ++opened_endpoints;
        }
        endpoint_desc += endpoint_desc[0];
    }
    if (opened_endpoints != interface_desc->bNumEndpoints) {
        return false;
    }

    onza_device_address = dev_addr;
    onza_interface_number = interface_desc->bInterfaceNumber;
    onza_endpoint_in = endpoint_in;
    onza_endpoint_out = endpoint_out;
    onza_input_packet_size = input_packet_size;
    onza_mounted = true;
    onza_have_last_report = false;

    char line[96];
    (void)snprintf(
        line,
        sizeof(line),
        "[XPAD] Onza interface=%u IN=%02X OUT=%02X packet=%u",
        onza_interface_number,
        onza_endpoint_in,
        onza_endpoint_out,
        onza_input_packet_size);
    usb_host_diagnostic_line(line);
    return true;
}

static bool onza_driver_set_config(uint8_t dev_addr, uint8_t interface_number)
{
    if (onza_mounted &&
        onza_device_address == dev_addr &&
        onza_interface_number == interface_number) {
        usb_host_send_device_connected_class(
            ONZA_DEVICE_ID,
            TUSB_CLASS_VENDOR_SPECIFIC,
            ONZA_VENDOR_ID,
            ONZA_TOURNAMENT_PRODUCT_ID);
        usb_host_diagnostic_line("[XPAD] sending player-1 LED command");
        if (!onza_send_led_report()) {
            usb_host_diagnostic_line("[XPAD] LED command queue failed");
        }
        if (!onza_start_input_transfer()) {
            usb_host_diagnostic_line("[XPAD] initial input transfer failed");
        }
    }

    usbh_driver_set_config_complete(dev_addr, interface_number);
    return true;
}

static bool onza_driver_xfer(
    uint8_t dev_addr,
    uint8_t endpoint_address,
    xfer_result_t result,
    uint32_t transferred_bytes)
{
    if (!onza_mounted || dev_addr != onza_device_address) {
        return true;
    }

    if (endpoint_address == onza_endpoint_out) {
        usb_host_diagnostic_line(
            result == XFER_RESULT_SUCCESS
                ? "[XPAD] LED command sent"
                : "[XPAD] LED command transfer failed");
        return true;
    }
    if (endpoint_address != onza_endpoint_in) {
        return true;
    }

    if (result != XFER_RESULT_SUCCESS) {
        usb_host_diagnostic_line("[XPAD] input transfer failed; retrying");
    } else if (transferred_bytes != XBOX360_INPUT_REPORT_SIZE) {
        char line[80];
        (void)snprintf(
            line,
            sizeof(line),
            "[XPAD] unexpected input length=%lu",
            (unsigned long)transferred_bytes);
        usb_host_diagnostic_line(line);
    } else {
        (void)usb_host_send_xbox360_report(
            ONZA_DEVICE_ID,
            onza_input_report,
            XBOX360_INPUT_REPORT_SIZE);
        if (!onza_have_last_report ||
            memcmp(onza_last_report, onza_input_report, sizeof(onza_last_report)) != 0) {
            char line[96];
            size_t offset = 0;
            for (size_t i = 0; i < XBOX360_INPUT_REPORT_SIZE; ++i) {
                const int count = snprintf(
                    line + offset,
                    sizeof(line) - offset,
                    i == 0 ? "%02X" : " %02X",
                    onza_input_report[i]);
                if (count < 0 || (size_t)count >= sizeof(line) - offset) {
                    break;
                }
                offset += (size_t)count;
            }
            line[offset] = '\0';
            char diagnostic[112];
            (void)snprintf(diagnostic, sizeof(diagnostic), "[XPAD] input %s", line);
            usb_host_diagnostic_line(diagnostic);
            memcpy(onza_last_report, onza_input_report, sizeof(onza_last_report));
            onza_have_last_report = true;
        }
    }

    if (!onza_start_input_transfer()) {
        usb_host_diagnostic_line("[XPAD] next input transfer failed");
    }
    return true;
}

static void onza_driver_close(uint8_t dev_addr)
{
    if (onza_mounted && dev_addr == onza_device_address) {
        usb_host_send_device_disconnected(ONZA_DEVICE_ID);
        usb_host_diagnostic_line("[XPAD] disconnected");
        onza_mounted = false;
        onza_have_last_report = false;
    }
}

usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *driver_count)
{
    static const usbh_class_driver_t onza_driver = {
        .name = "XBOX360_ONZA",
        .init = onza_driver_init,
        .deinit = onza_driver_deinit,
        .open = onza_driver_open,
        .set_config = onza_driver_set_config,
        .xfer_cb = onza_driver_xfer,
        .close = onza_driver_close
    };

    *driver_count = 1;
    return &onza_driver;
}
