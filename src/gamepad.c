// Copyright (c) 2026 Jacek Fedorynski
// SPDX-License-Identifier: MIT

#include <stdlib.h>
#include <string.h>

#include "bsp/board.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/structs/usb.h"
#include "tusb.h"

#include "gpio_sample.pio.h"

#define USB_VID 0xCAFE
#define USB_PID 0xBAFF
#define EPNUM_HID_IN 0x81
#define NUM_SAMPLED_GPIOS 8

char const* string_desc_arr[] = {
    (const char[]){ 0x09, 0x04 },  // 0: English (0x0409)
    "Arasaka",                     // 1: Manufacturer
    "Zero Latency Gamepad",        // 2: Product
};

uint8_t const desc_hid_report[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,  // Usage (Game Pad)
    0xA1, 0x01,  // Collection (Application)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x35, 0x00,  //   Physical Minimum (0)
    0x45, 0x01,  //   Physical Maximum (1)
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x08,  //   Report Count (8)
    0x05, 0x09,  //   Usage Page (Button)
    0x19, 0x01,  //   Usage Minimum (0x01)
    0x29, 0x08,  //   Usage Maximum (0x08)
    0x81, 0x02,  //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,        // End Collection
};

uint8_t report[1];

int main(void) {
    board_init();
    tusb_init();

    memset(report, 0, sizeof(report));

    while (true) {
        tud_task();
        if (tud_hid_ready()) {
            tud_hid_report(0, report, sizeof(report));
        }
    }

    return 0;
}

static void gpio_dma_start(uint8_t* dest) {
    static bool pio_initialized = false;
    static PIO pio;
    static uint sm;
    static int dma_a, dma_b;

    if (!pio_initialized) {
        uint offset;
        bool ok = pio_claim_free_sm_and_add_program_for_gpio_range(&gpio_sample_program, &pio, &sm, &offset, 0, NUM_SAMPLED_GPIOS, false);
        hard_assert(ok);
        for (uint i = 0; i < NUM_SAMPLED_GPIOS; i++) {
            pio_gpio_init(pio, i);
        }
        // pio_gpio_init() gives the pins to the PIO; make them inputs again.
        pio_sm_set_consecutive_pindirs(pio, sm, 0, NUM_SAMPLED_GPIOS, false);

        // Pull-up and invert so they read as 1 when low (button pressed).
        for (uint i = 0; i < NUM_SAMPLED_GPIOS; i++) {
            gpio_pull_up(i);
            gpio_set_inover(i, GPIO_OVERRIDE_INVERT);
        }

        pio_sm_config c = gpio_sample_program_get_default_config(offset);
        sm_config_set_in_pins(&c, 0);
        sm_config_set_in_shift(&c, false, true, NUM_SAMPLED_GPIOS);
        sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
        sm_config_set_clkdiv(&c, 4);
        pio_sm_init(pio, sm, offset, &c);

        dma_a = dma_claim_unused_channel(true);
        dma_b = dma_claim_unused_channel(true);
        pio_initialized = true;
    }

    // The destination changes if the endpoint buffers get reallocated on a
    // re-enumeration, so (re)configure the channels every time.
    pio_sm_set_enabled(pio, sm, false);
    dma_channel_abort(dma_a);
    dma_channel_abort(dma_b);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);

    // Two channels, each doing a single transfer and chaining to the other.
    // The transfer count is reloaded on every trigger, so this never ends.
    // The PIO pushes a byte at a time (autopush at 8 bits), so the FIFO's
    // low byte is read and only a byte is written to the destination.
    int channels[2] = { dma_a, dma_b };
    for (int i = 0; i < 2; i++) {
        dma_channel_config dc = dma_channel_get_default_config(channels[i]);
        channel_config_set_transfer_data_size(&dc, DMA_SIZE_8);
        channel_config_set_read_increment(&dc, false);
        channel_config_set_write_increment(&dc, false);
        channel_config_set_dreq(&dc, pio_get_dreq(pio, sm, false));
        channel_config_set_chain_to(&dc, channels[1 - i]);
        dma_channel_configure(channels[i], &dc, dest, &pio->rxf[sm], 1, false);
    }

    pio_sm_set_enabled(pio, sm, true);
    dma_channel_start(dma_a);
}

// The endpoint buffer for EP1 IN lives in USB DPRAM; its offset is in the
// endpoint control register.
void tud_mount_cb(void) {
    uint32_t offset = usb_dpram->ep_ctrl[(EPNUM_HID_IN & 0x7f) - 1].in & 0xffff;
    gpio_dma_start((uint8_t*) ((uintptr_t) usb_dpram + offset));
}

tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,

    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x00,

    .bNumConfigurations = 0x01
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

uint8_t const desc_configuration[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN, 0, 100),

    // Interface number, string index, protocol, report descriptor len, EP IN address, size, polling interval
    TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report), EPNUM_HID_IN, CFG_TUD_HID_EP_BUFSIZE, 1)
};

uint8_t const* tud_descriptor_device_cb(void) {
    return (uint8_t const*) &desc_device;
}

uint8_t const* tud_hid_descriptor_report_cb(uint8_t itf) {
    return desc_hid_report;
}

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    return 0;
}

void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
}

uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
    return desc_configuration;
}

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    static uint16_t desc_str[32];

    size_t len;

    if (index == 0) {
        memcpy(&desc_str[1], string_desc_arr[0], 2);
        len = 1;
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) {
            return NULL;
        }

        const char* str = string_desc_arr[index];

        len = strlen(str);
        size_t const maxlen = sizeof(desc_str) / sizeof(desc_str[0]) - 1;
        if (len > maxlen) {
            len = maxlen;
        }

        for (size_t i = 0; i < len; i++) {
            desc_str[1 + i] = str[i];
        }
    }

    desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * len + 2));

    return desc_str;
}
