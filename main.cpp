#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"

#include "i2c_interface.hpp"
#include "display.hpp"
#include "aps6404.hpp"

#include "constants.hpp"

// These magic value go into the Watchdog registers, allowing the RP2040 to boot
// from the entry point in RAM without trying to boot from flash.
uint32_t __attribute__((section(".wd_data.boot"))) boot_args[4] = {0xb007c0d3, 0x6ff83f2c, 0x15004000, 0x20000001};

#if !SUPPORT_WIDE_MODES
#define FRAME_WIDTH 1312
#define FRAME_HEIGHT 576
#else
#define FRAME_WIDTH 1280
#define FRAME_HEIGHT 720
#endif

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

uint32_t colour_buf[1][FRAME_WIDTH / 2];

void make_rainbow(APS6404& aps6404) {
    constexpr int stride = FRAME_WIDTH * 2;

    aps6404.init();

    uint32_t addr = 0;
    {
        uint32_t* buf = colour_buf[0];
        buf[0] = 0x4F434950;
        buf[1] = 0x01010101;
        //buf[2] = 0x05000000; //1280
        //buf[2] = 0x03c00000; //960
        //buf[2] = 0x03200000; //800
        //buf[2] = 0x02d00000; //720
        buf[2] = 0x02800000; //640
        //buf[3] = 0x02d00000; //720
        //buf[3] = 0x02580000;  // 600
        //buf[3] = 0x02400000; // 576
        //buf[3] = 0x021c0000; // 540
        buf[3] = 0x01e00000;  // 480
        //buf[3] = 0x01c20000; // 450
        buf[4] = 0x00000001;
        buf[5] = 0x00010000 + FRAME_HEIGHT;
        buf[6] = 0x00000000;
        aps6404.write(addr, buf, 7);
        addr += 7 * 4;

        // Frame table
        constexpr int max_i = 9;
        constexpr int max_j = FRAME_HEIGHT / max_i;
        for (int i = 0; i < max_i; ++i) {
            for (int j = 0; j < max_j; ++j) {
                buf[j] = 0x80100000 + (i * max_j + j) * stride;
            }
            aps6404.write(addr, buf, max_j);
            aps6404.wait_for_finish_blocking();
            addr += max_j * 4;
        }
        printf("Written header\n");

#if 0
        // Sprite table
        printf("Writing sprite table at %lx\n", addr);
        uint32_t* ptr = buf;
        *ptr++ = 0x100c0000;
        *ptr++ = 0x100c0318;
        *ptr++ = 0x100c0620;
        *ptr++ = 0x100c0928;
        *ptr++ = 0x100c0c40;
#if 0
        *ptr++ = 0x20001820;
        for (int y = 0; y < 24; y += 2) {
            *ptr++ = 0x20002000;
        }
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
        addr = 0x0c0000;
        uint32_t* sbuf = (uint32_t*)0x10038000;
        for (int i = 0; i < 0x10000; i += APS6404::PAGE_SIZE >> 2) {
            aps6404.write(addr + (i << 2), sbuf, APS6404::PAGE_SIZE >> 2);
            sbuf += APS6404::PAGE_SIZE >> 2;
        }
#endif
        aps6404.wait_for_finish_blocking();
#endif
    }

#if 0
    // This is to display the vista image.  Enable and change stride above to 1280.
    addr = 0x100000;
    uint32_t* buf = (uint32_t*)0x1003c000;
    for (int i = 0; i < FRAME_HEIGHT * FRAME_WIDTH / 2; i += APS6404::PAGE_SIZE >> 4) {
        aps6404.write(addr + (i << 2), buf, APS6404::PAGE_SIZE >> 4);
        aps6404.wait_for_finish_blocking();
        aps6404.read_blocking(addr + (i << 2), colour_buf[0], APS6404::PAGE_SIZE >> 4);
        if (memcmp(buf, colour_buf[0], APS6404::PAGE_SIZE >> 2)) {
            printf("Colour buf mismatch at addr %lx\n", addr + (i << 2));
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
        buf += APS6404::PAGE_SIZE >> 4;
    }
#else
    uint16_t* buf;
    addr = 0x100000;
    for (int y = 0; y < FRAME_HEIGHT; ++y) {
        buf = (uint16_t*)(colour_buf[0]);
        for (int x = 0; x < FRAME_WIDTH; ++x) {
            *buf++ = 0x0001 * y + 0x0400 * (x & 0x1F); //from_hsv((1.0f * x) / (FRAME_WIDTH / 2), (1.0f * y) / FRAME_HEIGHT, (1.0f * (y % 20)) / 20);
            //*buf++ = from_hsv((1.0f * x) / FRAME_WIDTH, 1.f, 1.f);
            //*buf++ = x + y * FRAME_WIDTH;
        }

        for (int i = 0; i < FRAME_WIDTH / 2; i += APS6404::PAGE_SIZE >> 2) {
            aps6404.write(addr + (i << 2), &colour_buf[0][i], APS6404::PAGE_SIZE >> 2);
        }
        aps6404.wait_for_finish_blocking();
        #if 0
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
        #endif
        addr += stride;
    }
#endif
}

DisplayDriver display;

// Return default value in 50mV units
static uint8_t get_default_voltage_for_clock(uint32_t clock_khz) {
    if (clock_khz < 300000) {
        return 1200 / 50;
    }
    else if (clock_khz < 380000) {
        return 1250 / 50;
    }
    else {
        return 1300 / 50;
    }
}

static uint8_t get_vreg_select_for_voltage(uint8_t voltage_50mv) {
    if (voltage_50mv < 1000 / 50) voltage_50mv = 1000 / 50;
    if (voltage_50mv > 1300 / 50) voltage_50mv = 1300 / 50;

    return voltage_50mv - (1000 / 50) + VREG_VOLTAGE_1_00;
}

void handle_i2c_reg_write(uint8_t reg, uint8_t end_reg, uint8_t* regs) {
    // Subtract 0xC0 from regs so that register numbers match addresses
    regs -= 0xC0;

    #define REG_WRITTEN(R) (reg <= R && end_reg >= R)
    #define REG_WRITTEN2(R_START, R_END) (reg <= R_END && end_reg >= R_START)

    if (REG_WRITTEN(0xD3)) {
        display.clear_peak_scanline_time();
    }
    if (REG_WRITTEN(0xD4)) {
        display.clear_late_scanlines();
    }

    if (REG_WRITTEN2(0xF3, 0xF0)) {
        int offset = (regs[0xF3] << 24) |
                     (regs[0xF2] << 16) |
                     (regs[0xF1] << 8) |
                     (regs[0xF0] & 0xFC);
        display.set_frame_data_address_offset(offset);
    }
    if (REG_WRITTEN(0xFA)) {
        display.set_spi_mode(regs[0xFA] != 0);
    }
    if (REG_WRITTEN(0xFF)) {
        if (regs[0xFF] == 0x01) {
            printf("Resetting\n");
            watchdog_reboot(0x20000001, 0x15004000, 0);
        }
        if (regs[0xFF] == 0x02) {
            printf("Resetting to DFU mode\n");
            reset_usb_boot(0, 0);
        }
    }

    #undef REG_WRITTEN
    #undef REG_WRITTEN2
}

void handle_i2c_sprite_write(uint8_t sprite, uint8_t end_sprite, uint8_t* sprite_data) {
    for (int i = sprite; i <= end_sprite; ++i) {
        uint8_t* sprite_ptr = sprite_data + 7 * i;

        int16_t sprite_idx = (int8_t(sprite_ptr[2]) << 8) | sprite_ptr[1];
        int16_t x = (sprite_ptr[4] << 8) | sprite_ptr[3];
        int16_t y = (sprite_ptr[6] << 8) | sprite_ptr[5];
        display.set_sprite(i, sprite_idx, (pico_stick::BlendMode)sprite_ptr[0], x, y);
    }
}

void set_i2c_reg_data_for_frame(uint8_t* regs, const DisplayDriver::Diags& diags) {
    regs -= 0xC0;

    regs[0xC0] = gpio_get_all() >> 23;
    regs[0xC8] = sio_hw->gpio_hi_in;

    regs[0xD0] = (diags.vsync_time * 200) / diags.available_vsync_time;
    regs[0xD1] = ((diags.scanline_total_prep_time[0] + diags.scanline_total_prep_time[1]) * 100) / diags.available_total_scanline_time;
    regs[0xD2] = std::max(diags.scanline_max_prep_time[0], diags.scanline_max_prep_time[1]);
    regs[0xD3] = diags.peak_scanline_time;
    regs[0xD4] = diags.total_late_scanlines;
    regs[0xD5] = (diags.total_late_scanlines) >> 8;
    regs[0xD6] = (diags.total_late_scanlines) >> 16;
    regs[0xD7] = (diags.total_late_scanlines) >> 24;
    regs[0xD8] = std::max(diags.scanline_max_sprites[0], diags.scanline_max_sprites[1]);
}

void handle_display_diags_callback(const DisplayDriver::Diags& diags) {
    set_i2c_reg_data_for_frame(i2c_slave_if::get_high_reg_table(), diags);
}

void setup_i2c_reg_data(uint8_t* regs) {
    set_i2c_reg_data_for_frame(regs, display.get_diags());

    // Subtract 0xC0 from regs so that register numbers match addresses
    regs -= 0xC0;

    regs[0xC1] = 2; // LED defaults to heartbeat

    // System info
    uint32_t clock_10khz = display.get_clock_khz() / 10;
    regs[0xDC] = clock_10khz & 0xFF;
    regs[0xDD] = clock_10khz >> 8;
    regs[0xDE] = get_default_voltage_for_clock(display.get_clock_khz());
}

int main() {
	stdio_init_all();
    i2c_slave_if::deinit();

    display.init();
    display.diags_callback = handle_display_diags_callback;
    printf("DV Display Driver Initialised\n");

    uint8_t* regs = i2c_slave_if::init(handle_i2c_sprite_write, handle_i2c_reg_write);
    setup_i2c_reg_data(regs);
    regs -= 0xC0;

    //make_rainbow(display.get_ram());
    //printf("Rainbow written...\n");

    // Wait for I2C to indicate we should start
    while (regs[0xF9] == 0) __wfe();
    printf("DV Driver: Starting\n");

    // Deinit I2C before adjusting clock
    i2c_slave_if::deinit();

    // Set voltage to value from I2C register - in future this might have been altered since boot.
    uint vreg_select = get_vreg_select_for_voltage(i2c_slave_if::get_high_reg_table()[0xDE - 0xC0]);
    hw_write_masked(&vreg_and_chip_reset_hw->vreg, vreg_select << VREG_AND_CHIP_RESET_VREG_VSEL_LSB, VREG_AND_CHIP_RESET_VREG_VSEL_BITS);
	sleep_ms(10);

	set_sys_clock_khz(display.get_clock_khz(), true);

	stdio_init_all();
    display.get_ram().adjust_clock();

    // Reinit I2C now clock is set.
    i2c_slave_if::init(handle_i2c_sprite_write, handle_i2c_reg_write);

    printf("DV Driver: Clock configured\n");

    display.run();

    // If run ever exits, the magic number in the RAM was wrong.
    // For now we reboot if that happens
    printf("DV Driver: Display failed\n");

    printf("DV Driver: Resetting\n");
    watchdog_reboot(0x20000001, 0x15004000, 0);
    
    while (true);
}