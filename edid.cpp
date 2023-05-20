#include <stdio.h>
#include "hardware/i2c.h"
#include "hardware/gpio.h"

#define HDMI_I2C_SDA 4
#define HDMI_I2C_SCL 5

void read_edid() {
    i2c_init(i2c0, 100 * 1000);
    gpio_set_function(HDMI_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(HDMI_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(HDMI_I2C_SDA);
    gpio_pull_up(HDMI_I2C_SCL);

    printf("Read EDID\n");
    uint8_t rxdata[128];
    int ret = i2c_read_blocking(i2c0, 0x50, rxdata, 1, false);
    if (ret < 0) {
        printf("No EDID found\n");
        return;
    }

    rxdata[0] = 0;
    i2c_write_blocking(i2c0, 0x50, rxdata, 1, true);
    i2c_read_blocking(i2c0, 0x50, rxdata, 128, false);

    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 16; ++j) {
            printf("%02x ", rxdata[i*16 + j]);
        }
        printf("\n");
    }

    printf("Done.\n");
}