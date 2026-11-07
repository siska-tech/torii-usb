#include "tusb.h"

#include <string.h>

#define TORII_USB_VID 0xcafeu
#define TORII_USB_PID 0x4001u
#define TORII_USB_BCD 0x0200u

enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_TOTAL
};

#define EPNUM_CDC_NOTIF 0x81
#define EPNUM_CDC_OUT 0x02
#define EPNUM_CDC_IN 0x82
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

static const tusb_desc_device_t device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = TORII_USB_BCD,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = TORII_USB_VID,
    .idProduct = TORII_USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0,
    .bNumConfigurations = 1
};

static const uint8_t configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(
        1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(
        ITF_NUM_CDC,
        3,
        EPNUM_CDC_NOTIF,
        8,
        EPNUM_CDC_OUT,
        EPNUM_CDC_IN,
        64)
};

static const char *const string_descriptors[] = {
    "TORII",
    "TORII development console",
    "UART/USB debug"
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&device_descriptor;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    return index == 0 ? configuration_descriptor : NULL;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    static uint16_t descriptor[32];
    (void)langid;

    uint8_t character_count;
    if (index == 0) {
        descriptor[1] = 0x0409;
        character_count = 1;
    } else {
        const size_t string_count =
            sizeof(string_descriptors) / sizeof(string_descriptors[0]);
        if (index > string_count) {
            return NULL;
        }

        const char *string = string_descriptors[index - 1u];
        character_count = (uint8_t)strlen(string);
        if (character_count > 31) {
            character_count = 31;
        }
        for (uint8_t i = 0; i < character_count; ++i) {
            descriptor[1u + i] = (uint8_t)string[i];
        }
    }

    descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8) |
                               (2u * character_count + 2u));
    return descriptor;
}
