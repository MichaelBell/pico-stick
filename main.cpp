#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "hardware/structs/pads_qspi.h"

#include "i2c_interface.hpp"
#include "display.hpp"
#include "aps6404.hpp"
#include "edid.hpp"

#include "pins.hpp"
#include "constants.hpp"

// These magic value go into the Watchdog registers, allowing the RP2040 to boot
// from the entry point in RAM without trying to boot from flash.
uint32_t __attribute__((section(".wd_data.boot"))) boot_args[4] = {0xb007c0d3, 0x6ff83f2c, 0x15004000, 0x20000001};

using namespace pimoroni;

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

void setup_i2c_reg_data(uint8_t* regs);

void handle_i2c_reg_write(uint8_t reg, uint8_t end_reg, uint8_t* regs) {
    // Subtract 0xC0 from regs so that register numbers match addresses
    regs -= 0xC0;

    #define REG_WRITTEN(R) (reg <= R && end_reg >= R)
    #define REG_WRITTEN2(R_START, R_END) (reg <= R_END && end_reg >= R_START)

    if (REG_WRITTEN(0xC1)) {
        display.enable_heartbeat(regs[0xC1] == 2);

        if (regs[0xC1] == 0) pwm_set_gpio_level(PIN_LED, 0);
        else if (regs[0xC1] == 1) pwm_set_gpio_level(PIN_LED, 65535);
        else if (regs[0xC1] >= 128) {
            uint val = (regs[0xC1] - 128) << 1;
            pwm_set_gpio_level(PIN_LED, val * val);
        }
    }

    if (REG_WRITTEN(0xC9)) {
        sio_hw->gpio_hi_out = regs[0xC9] & 0x3F;
    }
    if (REG_WRITTEN(0xCA)) {
        sio_hw->gpio_hi_oe = regs[0xCA] & 0x3F;
    }
    if (REG_WRITTEN2(0xCB, 0xCC)) {
        constexpr uint8_t gpio_to_pad_map[] = { 0, 2, 3, 4, 5, 1 };
        for (uint i = 0; i < NUM_QSPI_GPIOS; ++i) {
            uint32_t val = 0x62;
            if (regs[0xCB] & (1 << gpio_to_pad_map[i])) val |= 8;
            if (regs[0xCC] & (1 << gpio_to_pad_map[i])) val |= 4;
            pads_qspi_hw->io[i] = val;
        }
    }

    if (REG_WRITTEN(0xD3)) {
        display.clear_peak_scanline_time();
    }
    if (REG_WRITTEN(0xD4)) {
        display.clear_late_scanlines();
    }

    if (REG_WRITTEN(0xEF)) {
        display.set_frame_counter(regs[0xEF]);
    }

    for (int i = 0; i < 3; ++i) {
        if (REG_WRITTEN2(0xF0 + 4*i, 0xF3 + 4*i)) {
            uint8_t* reg_base = &regs[0xF0 + 4*i];
            int offset = (reg_base[3] << 24) |
                        (reg_base[2] << 16) |
                        (reg_base[1] << 8) |
                        (reg_base[0]);
            display.set_frame_data_address_offset(i+1, offset);
        }
    }
    if (REG_WRITTEN(0xFC)) {
        if (regs[0xFD] == 0) { // If not started, can change mode
            display.set_res((pico_stick::Resolution)regs[0xFC]);
            setup_i2c_reg_data(regs + 0xC0);
        }
        regs[0xFC] = display.get_res();
    }
    if (REG_WRITTEN(0xFE)) {
        display.set_spi_mode(regs[0xFE] != 0);
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
        display.set_sprite(i, sprite_idx, (pico_stick::BlendMode)(sprite_ptr[0] & 0x7), x, y, (sprite_ptr[0] >> 3) + 1);
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
    
    regs[0xFC] = display.get_res();
}

int main() {
	stdio_init_all();
    
    // Setup switches B and C
    gpio_init(PIN_SW_B);
    gpio_init(PIN_SW_C);
    gpio_pull_up(PIN_SW_B);
    gpio_pull_up(PIN_SW_C);
    
    // Set up I2S (not used yet, but make sure we don't interfere)
    gpio_init(PIN_I2S_DATA);
    gpio_init(PIN_I2S_BCLK);
    gpio_init(PIN_I2S_LRCLK);
    gpio_disable_pulls(PIN_I2S_DATA);
    gpio_disable_pulls(PIN_I2S_BCLK);
    gpio_disable_pulls(PIN_I2S_LRCLK);

    // Set up GPIO
    gpio_init(PIN_ADC);
    gpio_disable_pulls(PIN_ADC);
    sio_hw->gpio_hi_oe = 0;
    for (uint i = 0; i < NUM_QSPI_GPIOS; ++i) {
        pads_qspi_hw->io[i] = 0x62;
    }

    // Setup heartbeat LED
    gpio_set_function(PIN_LED, GPIO_FUNC_PWM);
    {
        pwm_config config = pwm_get_default_config();
        pwm_config_set_clkdiv(&config, 4.f);
        pwm_init(PIN_LED_PWM_SLICE_NUM, &config, true);
        pwm_set_gpio_level(PIN_LED, 0);
    }

    uint8_t* regs = i2c_slave_if::init(handle_i2c_sprite_write, handle_i2c_reg_write);
    setup_i2c_reg_data(regs);
    regs -= 0xC0;
    printf("DV Display Driver I2C Initialised\n");

    read_edid();

    // Wait for I2C to indicate we should start
    while (regs[0xFD] == 0) __wfe();
    display.init();
    display.diags_callback = handle_display_diags_callback;
    printf("DV Display Driver Initialised\n");

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