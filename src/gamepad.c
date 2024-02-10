#include <string.h>

#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "pio_usb.h"
#include "usb_crc.h"

#define GPIO_MASK 0b00011100011111111111111111111100
#define GPIO_SHIFT 2
#define REPORT_SIZE 4

static usb_device_t* usb_device = NULL;

const uint8_t desc_device[] = {
    0x12,        // bLength
    0x01,        // bDescriptorType (Device)
    0x10, 0x01,  // bcdUSB 1.10
    0x00,        // bDeviceClass (Use class information in the Interface Descriptors)
    0x00,        // bDeviceSubClass
    0x00,        // bDeviceProtocol
    0x40,        // bMaxPacketSize0 64
    0xFE, 0xCA,  // idVendor 0xCAFE
    0x66, 0x06,  // idProduct 0x0666
    0x00, 0x01,  // bcdDevice 2.00
    0x01,        // iManufacturer (String Index)
    0x02,        // iProduct (String Index)
    0x00,        // iSerialNumber (String Index)
    0x01,        // bNumConfigurations 1
};

const uint8_t hid_report_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x35, 0x00,        //   Physical Minimum (0)
    0x45, 0x01,        //   Physical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x1C,        //   Report Count (28)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (0x01)
    0x29, 0x1C,        //   Usage Maximum (0x1C)
    0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
    0x25, 0x07,        //   Logical Maximum (7)
    0x46, 0x3B, 0x01,  //   Physical Maximum (315)
    0x75, 0x04,        //   Report Size (4)
    0x95, 0x01,        //   Report Count (1)
    0x65, 0x14,        //   Unit (System: English Rotation, Length: Centimeter)
    0x09, 0x39,        //   Usage (Hat switch)
    0x81, 0x42,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,Null State)
    0xC0,              // End Collection
};

const uint8_t* report_desc[] = { hid_report_descriptor };

const uint8_t desc_configuration[] = {
    0x09,        // bLength
    0x02,        // bDescriptorType (Configuration)
    0x22, 0x00,  // wTotalLength 34
    0x01,        // bNumInterfaces 1
    0x01,        // bConfigurationValue
    0x00,        // iConfiguration (String Index)
    0x80,        // bmAttributes
    0x32,        // bMaxPower 100mA

    0x09,  // bLength
    0x04,  // bDescriptorType (Interface)
    0x00,  // bInterfaceNumber 0
    0x00,  // bAlternateSetting
    0x01,  // bNumEndpoints 1
    0x03,  // bInterfaceClass
    0x00,  // bInterfaceSubClass
    0x00,  // bInterfaceProtocol
    0x00,  // iInterface (String Index)

    0x09,                                 // bLength
    0x21,                                 // bDescriptorType (HID)
    0x11, 0x01,                           // bcdHID 1.11
    0x00,                                 // bCountryCode
    0x01,                                 // bNumDescriptors
    0x22,                                 // bDescriptorType[0] (HID)
    sizeof(hid_report_descriptor), 0x00,  // wDescriptorLength[0]

    0x07,        // bLength
    0x05,        // bDescriptorType (Endpoint)
    0x81,        // bEndpointAddress (IN/D2H)
    0x03,        // bmAttributes (Interrupt)
    0x40, 0x00,  // wMaxPacketSize 64
    0x01,        // bInterval 1 (unit depends on device speed)
};

const char* string_descriptors_base[] = {
    [0] = (const char[]){ 0x09, 0x04 },
    [1] = "RP2040",
    [2] = "Zero Latency Gamepad",
};
static string_descriptor_t str_desc[3];

static void init_string_desc(void) {
    for (int idx = 0; idx < 3; idx++) {
        uint8_t len = 0;
        uint16_t* wchar_str = (uint16_t*) &str_desc[idx];
        if (idx == 0) {
            wchar_str[1] = string_descriptors_base[0][0] |
                           ((uint16_t) string_descriptors_base[0][1] << 8);
            len = 1;
        } else if (idx <= 3) {
            len = strnlen(string_descriptors_base[idx], 31);
            for (int i = 0; i < len; i++) {
                wchar_str[i + 1] = string_descriptors_base[idx][i];
            }

        } else {
            len = 0;
        }

        wchar_str[0] = (0x03 << 8) | (2 * len + 2);
    }
}

static usb_descriptor_buffers_t desc = {
    .device = desc_device,
    .config = desc_configuration,
    .hid_report = report_desc,
    .string = str_desc
};

static void init_gpio() {
    gpio_init_mask(GPIO_MASK);
    for (int i = 0; i < 32; i++) {
        if ((GPIO_MASK >> i) & 1) {
            gpio_pull_up(i);
        }
    }
}

uint8_t dpad_lut[] = { 0x80, 0x60, 0x20, 0x80, 0x00, 0x70, 0x10, 0x00, 0x40, 0x50, 0x30, 0x40, 0x80, 0x60, 0x20, 0x80 };

void last_minute_update(uint8_t* buffer) {
    uint32_t gpio_state = (~gpio_get_all() & GPIO_MASK) >> GPIO_SHIFT;
    uint8_t dpad_state = gpio_state & 0x0F;

    gpio_state >>= 4;

    buffer[0] = gpio_state;
    buffer[1] = gpio_state >> 8;
    buffer[2] = gpio_state >> 16;
    buffer[3] = gpio_state >> 24;

    buffer[3] |= dpad_lut[dpad_state];

    uint16_t crc = 0xffff;

    for (int idx = 0; idx < REPORT_SIZE; idx++) {
        crc = (crc >> 8) ^ crc16_tbl[(crc ^ buffer[idx]) & 0xff];
    }

    crc ^= 0xffff;

    buffer[REPORT_SIZE] = crc & 0xff;
    buffer[REPORT_SIZE + 1] = crc >> 8;
}

void core1_main() {
    sleep_ms(10);

    init_gpio();

    static pio_usb_configuration_t config = PIO_USB_DEFAULT_CONFIG;
    init_string_desc();
    usb_device = pio_usb_device_init(&config, &desc);

    while (true) {
        pio_usb_device_task();
    }
}

int main() {
    set_sys_clock_khz(240000, true);

    sleep_ms(10);

    multicore_reset_core1();
    multicore_launch_core1(core1_main);

    uint8_t report[REPORT_SIZE] = { 0, 0, 0, 0 };

    while (true) {
        if (usb_device != NULL) {
            endpoint_t* ep = pio_usb_get_endpoint(usb_device, 1);
            pio_usb_set_out_data(ep, report, sizeof(report));
        }
        sleep_us(100);
    }
}
