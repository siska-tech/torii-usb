#include "usb_host.h"

#include <stdbool.h>
#include <stdint.h>

#include "pio_usb.h"
#include "pico/critical_section.h"
#include "tusb.h"
#include "uart_transport.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define TORII_KEYBOARD_DEVICE_ID 1u
#define TORII_RAW_HID_DEVICE_ID 2u
#define TORII_DIAGNOSTIC_QUEUE_CAPACITY 16u
#define TORII_DIAGNOSTIC_LINE_SIZE 128u
#define TORII_DEVICE_CLASS_HID 3u
#define TORII_ERROR_QUEUE_FULL 1u
#define TORII_ERROR_RECEIVE_REPORT 2u
#define TORII_ERROR_INVALID_REPORT 3u
#define TORII_ERROR_HOST_INIT 4u
#define TORII_ERROR_HID_INTERFACE_LIMIT 5u
#define TORII_ERROR_HID_REPORT_TOO_LARGE 6u
#define TORII_SNAPSHOT_PERIOD_MS 250u

static bool keyboard_mounted;
static uint8_t keyboard_dev_addr;
static uint8_t keyboard_instance;
static bool raw_hid_mounted;
static uint8_t raw_hid_dev_addr;
static uint8_t raw_hid_instance;
static bool raw_hid_report_too_large_reported;
static uint8_t keyboard_modifiers;
static uint8_t keyboard_keys[6];
static uint16_t tx_sequence;
static uint16_t last_reported_drop_count;
static uint32_t last_snapshot_ms;
static uint8_t raw_last_report[TORII_MAX_PAYLOAD_SIZE];
static uint8_t raw_last_report_length;
static uint32_t raw_last_log_ms;
static bool raw_have_last_report;

static critical_section_t diagnostic_lock;
static char diagnostic_queue[TORII_DIAGNOSTIC_QUEUE_CAPACITY]
                            [TORII_DIAGNOSTIC_LINE_SIZE];
static uint8_t diagnostic_head;
static uint8_t diagnostic_tail;

static bool send_message(
    uint8_t type,
    uint8_t device_id,
    const uint8_t *payload,
    uint8_t payload_length)
{
    torii_frame_t frame = {0};
    if (payload_length > TORII_MAX_PAYLOAD_SIZE ||
        (payload_length != 0 && payload == NULL)) {
        return false;
    }
    frame.version = TORII_PROTOCOL_VERSION;
    frame.type = type;
    frame.device_id = device_id;
    frame.sequence = tx_sequence++;
    frame.payload_len = payload_length;
    if (payload_length != 0) {
        memcpy(frame.payload, payload, payload_length);
    }
    return uart_transport_send(&frame);
}

static void diagnostic_printf(const char *format, ...)
{
    char line[TORII_DIAGNOSTIC_LINE_SIZE];
    va_list args;
    va_start(args, format);
    int length = vsnprintf(line, sizeof(line) - 2u, format, args);
    va_end(args);
    if (length < 0) {
        return;
    }
    if ((size_t)length > sizeof(line) - 3u) {
        length = (int)(sizeof(line) - 3u);
    }
    line[length++] = '\r';
    line[length++] = '\n';
    line[length] = '\0';

    critical_section_enter_blocking(&diagnostic_lock);
    const uint8_t next_tail =
        (uint8_t)((diagnostic_tail + 1u) % TORII_DIAGNOSTIC_QUEUE_CAPACITY);
    if (next_tail != diagnostic_head) {
        memcpy(diagnostic_queue[diagnostic_tail], line, (size_t)length + 1u);
        diagnostic_tail = next_tail;
    }
    critical_section_exit(&diagnostic_lock);
}

void usb_host_diagnostic_line(const char *line)
{
    if (line != NULL) {
        diagnostic_printf("%s", line);
    }
}

void usb_host_send_device_connected(
    uint8_t device_id,
    uint16_t vendor_id,
    uint16_t product_id)
{
    usb_host_send_device_connected_class(
        device_id,
        TORII_DEVICE_CLASS_HID,
        vendor_id,
        product_id);
}

void usb_host_send_device_connected_class(
    uint8_t device_id,
    uint8_t device_class,
    uint16_t vendor_id,
    uint16_t product_id)
{
    const uint8_t payload[] = {
        device_class,
        (uint8_t)(vendor_id & 0xffu),
        (uint8_t)(vendor_id >> 8),
        (uint8_t)(product_id & 0xffu),
        (uint8_t)(product_id >> 8)
    };
    (void)send_message(
        TORII_MSG_DEVICE_CONNECTED,
        device_id,
        payload,
        sizeof(payload));
}

void usb_host_send_device_disconnected(uint8_t device_id)
{
    (void)send_message(
        TORII_MSG_DEVICE_DISCONNECTED,
        device_id,
        NULL,
        0);
}

bool usb_host_send_hid_report(
    uint8_t device_id,
    const uint8_t *report,
    uint8_t report_length)
{
    return send_message(
        TORII_MSG_HID_REPORT,
        device_id,
        report,
        report_length);
}

bool usb_host_send_xbox360_report(
    uint8_t device_id,
    const uint8_t *report,
    uint8_t report_length)
{
    return send_message(
        TORII_MSG_XBOX360_REPORT,
        device_id,
        report,
        report_length);
}

void usb_host_diagnostics_init(void)
{
    critical_section_init(&diagnostic_lock);
    diagnostic_head = 0;
    diagnostic_tail = 0;
}

void usb_host_diagnostics_service(void)
{
    if (!tud_cdc_connected()) {
        return;
    }

    for (;;) {
        char line[TORII_DIAGNOSTIC_LINE_SIZE];
        critical_section_enter_blocking(&diagnostic_lock);
        if (diagnostic_head == diagnostic_tail) {
            critical_section_exit(&diagnostic_lock);
            return;
        }
        memcpy(line, diagnostic_queue[diagnostic_head], sizeof(line));
        critical_section_exit(&diagnostic_lock);

        const size_t length = strlen(line);
        if (tud_cdc_write_available() < length) {
            return;
        }
        const uint32_t written = tud_cdc_write(line, (uint32_t)length);
        if (written != length) {
            return;
        }

        critical_section_enter_blocking(&diagnostic_lock);
        diagnostic_head =
            (uint8_t)((diagnostic_head + 1u) % TORII_DIAGNOSTIC_QUEUE_CAPACITY);
        critical_section_exit(&diagnostic_lock);
    }
}

static void send_hid_connected(uint8_t dev_addr, uint8_t device_id)
{
    uint16_t vendor_id = 0;
    uint16_t product_id = 0;
    (void)tuh_vid_pid_get(dev_addr, &vendor_id, &product_id);
    usb_host_send_device_connected(device_id, vendor_id, product_id);
    diagnostic_printf(
        "[HID] mounted id=%u addr=%u VID=%04X PID=%04X",
        device_id,
        dev_addr,
        vendor_id,
        product_id);
}

static void report_drop_count(void)
{
    const uint16_t dropped = uart_transport_dropped_packets();
    if (dropped == last_reported_drop_count) {
        return;
    }

    const uint8_t payload[] = {
        TORII_ERROR_QUEUE_FULL,
        (uint8_t)(dropped & 0xffu),
        (uint8_t)(dropped >> 8)
    };
    if (send_message(TORII_MSG_ERROR, 0, payload, sizeof(payload))) {
        last_reported_drop_count = dropped;
    }
}

static void send_keyboard_state(void)
{
    uint8_t payload[8] = {
        keyboard_modifiers,
        0,
        keyboard_keys[0],
        keyboard_keys[1],
        keyboard_keys[2],
        keyboard_keys[3],
        keyboard_keys[4],
        keyboard_keys[5]
    };
    (void)send_message(
        TORII_MSG_KEYBOARD_STATE,
        TORII_KEYBOARD_DEVICE_ID,
        payload,
        sizeof(payload));
}

void tuh_hid_mount_cb(
    uint8_t dev_addr,
    uint8_t instance,
    uint8_t const *desc_report,
    uint16_t desc_len)
{
    (void)desc_report;
    (void)desc_len;

    const uint8_t interface_protocol =
        tuh_hid_interface_protocol(dev_addr, instance);
    if (interface_protocol != HID_ITF_PROTOCOL_KEYBOARD || keyboard_mounted) {
        if (raw_hid_mounted) {
            const uint8_t error[] = {TORII_ERROR_HID_INTERFACE_LIMIT, 0, 0};
            (void)send_message(TORII_MSG_ERROR, 0, error, sizeof(error));
            return;
        }
        raw_hid_mounted = true;
        raw_hid_dev_addr = dev_addr;
        raw_hid_instance = instance;
        raw_hid_report_too_large_reported = false;
        raw_have_last_report = false;
        send_hid_connected(dev_addr, TORII_RAW_HID_DEVICE_ID);
        if (!tuh_hid_receive_report(dev_addr, instance)) {
            const uint8_t error[] = {TORII_ERROR_RECEIVE_REPORT, 0, 0};
            (void)send_message(TORII_MSG_ERROR, 0, error, sizeof(error));
        }
        return;
    }

    keyboard_mounted = true;
    keyboard_dev_addr = dev_addr;
    keyboard_instance = instance;
    keyboard_modifiers = 0;
    memset(keyboard_keys, 0, sizeof(keyboard_keys));
    last_snapshot_ms = to_ms_since_boot(get_absolute_time());
    send_hid_connected(dev_addr, TORII_KEYBOARD_DEVICE_ID);
    send_keyboard_state();

    if (!tuh_hid_receive_report(dev_addr, instance)) {
        const uint8_t error[] = {TORII_ERROR_RECEIVE_REPORT, 0, 0};
        (void)send_message(TORII_MSG_ERROR, 0, error, sizeof(error));
    }
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    if (keyboard_mounted &&
        keyboard_dev_addr == dev_addr &&
        keyboard_instance == instance) {
        (void)send_message(
            TORII_MSG_DEVICE_DISCONNECTED,
            TORII_KEYBOARD_DEVICE_ID,
            NULL,
            0);
        keyboard_mounted = false;
        keyboard_modifiers = 0;
        memset(keyboard_keys, 0, sizeof(keyboard_keys));
        diagnostic_printf("[HID] unmounted id=%u", TORII_KEYBOARD_DEVICE_ID);
    }

    if (raw_hid_mounted &&
        raw_hid_dev_addr == dev_addr &&
        raw_hid_instance == instance) {
        (void)send_message(
            TORII_MSG_DEVICE_DISCONNECTED,
            TORII_RAW_HID_DEVICE_ID,
            NULL,
            0);
        raw_hid_mounted = false;
        raw_hid_report_too_large_reported = false;
        raw_have_last_report = false;
        diagnostic_printf("[HID] unmounted id=%u", TORII_RAW_HID_DEVICE_ID);
    }
}

void tuh_hid_report_received_cb(
    uint8_t dev_addr,
    uint8_t instance,
    uint8_t const *report,
    uint16_t len)
{
    bool supported_interface = false;
    if (keyboard_mounted &&
        keyboard_dev_addr == dev_addr &&
        keyboard_instance == instance &&
        tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD) {
        supported_interface = true;
        if (report == NULL || len < sizeof(hid_keyboard_report_t)) {
            const uint8_t error[] = {TORII_ERROR_INVALID_REPORT, 0, 0};
            (void)send_message(TORII_MSG_ERROR, 0, error, sizeof(error));
        } else {
            const hid_keyboard_report_t *keyboard_report =
                (const hid_keyboard_report_t *)report;
            if (keyboard_modifiers != keyboard_report->modifier ||
                memcmp(keyboard_keys, keyboard_report->keycode,
                       sizeof(keyboard_keys)) != 0) {
                keyboard_modifiers = keyboard_report->modifier;
                memcpy(keyboard_keys, keyboard_report->keycode,
                       sizeof(keyboard_keys));
                send_keyboard_state();
                last_snapshot_ms = to_ms_since_boot(get_absolute_time());
            }
        }
    } else if (raw_hid_mounted &&
               raw_hid_dev_addr == dev_addr &&
               raw_hid_instance == instance) {
        supported_interface = true;
        if (report == NULL && len != 0) {
            const uint8_t error[] = {TORII_ERROR_INVALID_REPORT, 0, 0};
            (void)send_message(TORII_MSG_ERROR, 0, error, sizeof(error));
        } else if (len > TORII_MAX_PAYLOAD_SIZE) {
            if (!raw_hid_report_too_large_reported) {
                const uint8_t error[] = {
                    TORII_ERROR_HID_REPORT_TOO_LARGE,
                    (uint8_t)(len & 0xffu),
                    (uint8_t)((len >> 8) & 0xffu)
                };
                (void)send_message(TORII_MSG_ERROR, 0, error, sizeof(error));
                raw_hid_report_too_large_reported = true;
            }
        } else {
            (void)send_message(
                TORII_MSG_HID_REPORT,
                TORII_RAW_HID_DEVICE_ID,
                report,
                (uint8_t)len);
            const uint32_t now_ms =
                to_ms_since_boot(get_absolute_time());
            if ((!raw_have_last_report || len != raw_last_report_length ||
                 memcmp(raw_last_report, report, len) != 0) &&
                (!raw_have_last_report ||
                 (uint32_t)(now_ms - raw_last_log_ms) >= 50u)) {
                char report_hex[TORII_MAX_PAYLOAD_SIZE * 3u + 1u];
                size_t offset = 0;
                for (uint16_t i = 0; i < len; ++i) {
                    const int count = snprintf(
                        report_hex + offset,
                        sizeof(report_hex) - offset,
                        i == 0 ? "%02X" : " %02X",
                        report[i]);
                    if (count < 0 || (size_t)count >= sizeof(report_hex) - offset) {
                        break;
                    }
                    offset += (size_t)count;
                }
                report_hex[offset] = '\0';
                diagnostic_printf("[HID] report len=%u data=%s", len, report_hex);
                memcpy(raw_last_report, report, len);
                raw_last_report_length = (uint8_t)len;
                raw_last_log_ms = now_ms;
                raw_have_last_report = true;
            }
        }
    }

    if (supported_interface && !tuh_hid_receive_report(dev_addr, instance)) {
        const uint8_t error[] = {TORII_ERROR_RECEIVE_REPORT, 0, 0};
        (void)send_message(TORII_MSG_ERROR, 0, error, sizeof(error));
    }
}

void tuh_mount_cb(uint8_t dev_addr)
{
    uint16_t vendor_id = 0;
    uint16_t product_id = 0;
    (void)tuh_vid_pid_get(dev_addr, &vendor_id, &product_id);
    const uint8_t payload[] = {
        1,
        dev_addr,
        (uint8_t)(vendor_id & 0xffu),
        (uint8_t)(vendor_id >> 8),
        (uint8_t)(product_id & 0xffu),
        (uint8_t)(product_id >> 8)
    };
    (void)send_message(
        TORII_MSG_USB_DEVICE_STATUS,
        0,
        payload,
        sizeof(payload));
    diagnostic_printf(
        "[USB] configured addr=%u VID=%04X PID=%04X",
        dev_addr,
        vendor_id,
        product_id);
}

void tuh_umount_cb(uint8_t dev_addr)
{
    const uint8_t payload[] = {0, dev_addr, 0, 0, 0, 0};
    (void)send_message(
        TORII_MSG_USB_DEVICE_STATUS,
        0,
        payload,
        sizeof(payload));
    diagnostic_printf("[USB] unconfigured addr=%u", dev_addr);
}

void usb_host_task(void)
{
    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    pio_cfg.pin_dp = 6;

    tuh_configure(
        1,
        TUH_CFGID_RPI_PIO_USB_CONFIGURATION,
        &pio_cfg);
    while (!tuh_init(1)) {
        const uint8_t error[] = {TORII_ERROR_HOST_INIT, 0, 0};
        (void)send_message(TORII_MSG_ERROR, 0, error, sizeof(error));
        sleep_ms(1000);
    }

    (void)send_message(TORII_MSG_READY, 0, NULL, 0);

    while (true) {
        tuh_task();
        const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if ((uint32_t)(now_ms - last_snapshot_ms) >= TORII_SNAPSHOT_PERIOD_MS) {
            report_drop_count();
            if (keyboard_mounted) {
                send_keyboard_state();
            }
            last_snapshot_ms = now_ms;
        }
        tight_loop_contents();
    }
}
