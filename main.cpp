#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"

#include "i2c_interface.hpp"
#include "display.hpp"
#include "aps6404.hpp"

#define FRAME_WIDTH 1152
#define FRAME_HEIGHT 480

using namespace pimoroni;

uint16_t from_hsv(float h, float s, float v) {
    uint8_t r, g, b;

    float i = floorf(h * 6.0f);
    float f = h * 6.0f - i;
    v *= 255.0f;
    uint8_t p = v * (1.0f - s);
    uint8_t q = v * (1.0f - f * s);
    uint8_t t = v * (1.0f - (1.0f - f) * s);

    switch (int(i) % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        case 5: r = v; g = p; b = q; break;
        default: __builtin_unreachable();
    }

    return (uint16_t(r >> 3) << 11) | (uint16_t(g >> 2) << 5) | (b >> 3);
}

uint32_t colour_buf[2][FRAME_WIDTH / 2];

void make_rainbow(APS6404& aps6404) {
    constexpr int stride = FRAME_WIDTH * 2;

    uint32_t addr = 0;
    {
        uint32_t* buf = colour_buf[0];
        buf[0] = 0x4F434950;
        buf[1] = 0x01010101;
        buf[2] = 0x02800000;
        buf[3] = 0x01e00000;
        buf[4] = 0x00000001;
        buf[5] = 0x000101e0;
        buf[6] = 0x00010000;
        aps6404.write(addr, buf, 7);
        addr += 7 * 4;

        // Frame table
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 120; ++j) {
                buf[j] = 0x100000 + (i * 120 + j) * stride;
            }
            aps6404.write(addr, buf, 120);
            aps6404.wait_for_finish_blocking();
            addr += 120 * 4;
        }

        // Sprite table
        printf("Writing sprite table at %lx\n", addr);
        uint32_t* ptr = buf;
        *ptr++ = addr + 4;
        *ptr++ = 0x20001820;
        for (int y = 0; y < 24; y += 2) {
            *ptr++ = 0x20002000;
        }
#if 0
        for (int y = 0; y < 32; y += 2) {
            for (int x = 0; x < 32; x += 2) {
                *ptr++ = 0xFFFFF800;
            }
            for (int x = 0; x < 32; x += 2) {
                *ptr++ = 0x001F001F;
            }
        }
        int len = ptr - buf;
        uint32_t* write_ptr = buf;
        while (len > 0) {
            aps6404.write(addr, write_ptr, std::min(len, 128));
            addr += 512;
            len -= 128;
            write_ptr += 128;
        }
#else
        aps6404.write(addr, buf, ptr - buf);
        addr += (ptr - buf) * 4;
        uint32_t* sbuf = (uint32_t*)0x10038100;
        for (int i = 0; i < 22*32 / 2; i += APS6404::PAGE_SIZE >> 2) {
            aps6404.write(addr + (i << 2), sbuf, APS6404::PAGE_SIZE >> 2);
            sbuf += APS6404::PAGE_SIZE >> 2;
        }
#endif
        aps6404.wait_for_finish_blocking();
    }

#if 1
    // This is to display the vista image.  Enable and change stride above to 1280.
    addr = 0x100000;
    uint32_t* buf = (uint32_t*)0x1003c000;
    for (int i = 0; i < FRAME_HEIGHT * FRAME_WIDTH / 2; i += APS6404::PAGE_SIZE >> 2) {
        aps6404.write(addr + (i << 2), buf, APS6404::PAGE_SIZE >> 2);
        buf += APS6404::PAGE_SIZE >> 2;
    }
#else
    uint16_t* buf;
    addr = 0x100000;
    for (int y = 0; y < FRAME_HEIGHT; ++y) {
        buf = (uint16_t*)(colour_buf[0]);
        for (int x = 0; x < FRAME_WIDTH; ++x) {
            *buf++ = from_hsv((1.0f * x) / (FRAME_WIDTH / 2), (1.0f * y) / FRAME_HEIGHT, (1.0f * (y % 20)) / 20);
            //*buf++ = from_hsv((1.0f * x) / FRAME_WIDTH, 1.f, 1.f);
            //*buf++ = x + y * FRAME_WIDTH;
        }

        for (int i = 0; i < FRAME_WIDTH / 2; i += APS6404::PAGE_SIZE >> 2) {
            aps6404.write(addr + (i << 2), &colour_buf[0][i], APS6404::PAGE_SIZE >> 2);
        }
        aps6404.wait_for_finish_blocking();
        aps6404.read_blocking(addr, colour_buf[1], FRAME_WIDTH / 2);
        if (memcmp(colour_buf[0], colour_buf[1], FRAME_WIDTH * 2)) {
            printf("Colour buf mismatch at addr %lx\n", addr);
#if 0
            if (addr < 0x8000) {
                for (int i = 0; i < COLOUR_BUF_WORDS; ++i) {
                    if (colour_buf[0][i] != colour_buf[1][i]) {
                        printf("%lx: %lx != %lx\n", addr + (i << 2), colour_buf[0][i], colour_buf[1][i]);
                    }
                }
            }
#endif
        }
        addr += stride;
    }
#endif
}

DisplayDriver display;

void handle_i2c_reg_write(uint8_t reg, uint8_t end_reg, uint8_t* data) {
    if (reg == 0x10) {
        // X scroll
        //printf("Set X scroll to %hhu\n", *data);
        display.set_frame_data_address_offset(data[0] * 4);
    }
    if (reg == 0xFF) {
        if (*data == 0x01) {
            printf("Resetting\n");
            watchdog_reboot(0, 0, 0);
        }
        if (*data == 0x02) {
            printf("Resetting to DFU mode\n");
            reset_usb_boot(0, 0);
        }
    }
}

int main() {
	set_sys_clock_khz(252000, true);

	stdio_init_all();

    //sleep_ms(5000);
    printf("Starting\n");
    i2c_slave_if::init(handle_i2c_reg_write);

    display.init();
    printf("APS Init\n");

    make_rainbow(display.get_ram());
    printf("Rainbow written...\n");

    display.run();
    printf("Display failed\n");
    
    while (true);
}
