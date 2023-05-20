#include <stdio.h>
#include <string.h>
#include "hardware/i2c.h"
#include "hardware/gpio.h"

#define HDMI_I2C_SDA 4
#define HDMI_I2C_SCL 5

static uint8_t __attribute__((section(".usb_ram.frame_table"))) edid_data[128];

uint8_t* read_edid() {
    i2c_init(i2c0, 100 * 1000);
    gpio_set_function(HDMI_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(HDMI_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(HDMI_I2C_SDA);
    gpio_pull_up(HDMI_I2C_SCL);

    printf("Read EDID\n");
    int ret = i2c_read_blocking(i2c0, 0x50, edid_data, 1, false);
    if (ret < 0) {
        printf("No EDID found\n");
        memset(edid_data, 0, 128);
        return edid_data;
    }

    edid_data[0] = 0;
    i2c_write_blocking(i2c0, 0x50, edid_data, 1, true);
    i2c_read_blocking(i2c0, 0x50, edid_data, 128, false);

#if 0
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 16; ++j) {
            printf("%02x ", edid_data[i*16 + j]);
        }
        printf("\n");
    }
#endif

    return edid_data;
}

uint8_t* get_edid_data() {
    return edid_data;
}